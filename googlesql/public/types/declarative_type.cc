//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "googlesql/public/types/declarative_type.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/internal_utils.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value.pb.h"
#include "absl/functional/overload.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

size_t DeclarativeTypeDescriptor::GetEstimatedOwnedMemoryBytesSize() const {
  return sizeof(*this) +
         // Data is stored on the heap
         sizeof(Data) +
         // Add just the heap memory, since the stack is already accounted
         // for in sizeof(Data).
         // Heap memory for the display name.
         internal::GetExternallyAllocatedMemoryEstimate(data_->display_name) +
         // Heap memory for the feature set.
         internal::GetExternallyAllocatedMemoryEstimate(
             data_->additional_required_language_features);
}

DeclarativeType::DeclarativeType(const TypeFactoryBase& factory,
                                 DeclarativeTypeDescriptor data)
    : Type(factory, TYPE_DECLARATIVE), data_(std::move(data)) {
  // The only supertype for a declarative type is the backing type *IF* it
  // specifies that implicit coercion to the backing type is allowed.
  if (data_.coercion_to_backing_type() ==
      DeclarativeTypeDescriptor::AllowCoercionMode::kAllowAllCoercion) {
    candidate_super_types_.push_back(data_.backing_type());
  }
}

std::string DeclarativeType::ShortTypeName(ProductMode mode) const {
  return data_.display_name();
}

std::string DeclarativeType::TypeName(ProductMode mode) const {
  return data_.display_name();
}

absl::StatusOr<std::string> DeclarativeType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode) const {
  if (type_modifiers.IsEmpty()) {
    return data_.display_name();
  }
  GOOGLESQL_RET_CHECK(type_modifiers.collation().Empty())
      << "Collation is not supported for declarative types";
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedTypeParameters(type_modifiers.type_parameters(), mode));
  return absl::StrCat(data_.display_name(),
                      type_modifiers.type_parameters().ToParenthesizedString());
}

absl::StatusOr<TypeParameters>
DeclarativeType::ValidateAndResolveTypeParameters(
    const std::vector<TypeParameterValue>& type_parameter_values,
    ProductMode mode) const {
  return std::visit(
      absl::Overload(
          [&](const DeclarativeTypeDescriptor::TypeParamsCustom& custom) {
            return custom.resolve_callback()(type_parameter_values, mode);
          },
          [&](DeclarativeTypeDescriptor::TypeParamsDisallowed)
              -> absl::StatusOr<TypeParameters> {
            return MakeSqlError() << "Type " << ShortTypeName(mode)
                                  << " does not support type parameters";
          }),
      descriptor().type_params_strategy());
}

absl::Status DeclarativeType::ValidateResolvedTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) const {
  if (type_parameters.IsEmpty()) {
    return absl::OkStatus();
  }
  return std::visit(
      absl::Overload(
          [&](const DeclarativeTypeDescriptor::TypeParamsCustom& custom) {
            return custom.validate_callback()(type_parameters, mode);
          },
          [&](DeclarativeTypeDescriptor::TypeParamsDisallowed) -> absl::Status {
            GOOGLESQL_RET_CHECK(type_parameters.IsEmpty())
                << "Type " << ShortTypeName(mode)
                << " does not support type parameters";
            return absl::OkStatus();
          }),
      descriptor().type_params_strategy());
}

TypeListView DeclarativeType::ComponentTypes() const { return {}; }

// Returns true if the descriptor allows the given coercion.
// This helper function is used only for the coercion to/from the backing type,
// and `coercion_spec` is for the corresponding direction.
static bool IsBackingTypeCoercionAllowed(
    DeclarativeTypeDescriptor::AllowCoercionMode coercion_spec,
    bool is_explicit) {
  switch (coercion_spec) {
    case DeclarativeTypeDescriptor::AllowCoercionMode::kNoCoercion:
      return false;
    case DeclarativeTypeDescriptor::AllowCoercionMode::kExplicitOnly:
      return is_explicit;
    case DeclarativeTypeDescriptor::AllowCoercionMode::kAllowAllCoercion:
      return true;
  }
}

bool DeclarativeType::CanCoerceTo(const Type* to_type, bool is_explicit) const {
  if (!to_type->Equals(backing_type())) {
    return false;
  }
  return IsBackingTypeCoercionAllowed(data_.coercion_to_backing_type(),
                                      is_explicit);
}

bool DeclarativeType::CanCoerceFrom(const Type* from_type,
                                    bool is_explicit) const {
  if (!from_type->Equals(backing_type())) {
    return false;
  }
  return IsBackingTypeCoercionAllowed(data_.coercion_from_backing_type(),
                                      is_explicit);
}

bool DeclarativeType::SupportsFormatting() const {
  return !std::holds_alternative<
      DeclarativeTypeDescriptor::FormattingDisallowed>(
      data_.formatting_strategy());
}

bool DeclarativeType::SupportsEquality() const {
  return std::visit(
      absl::Overload(
          [](DeclarativeTypeDescriptor::EqualityDisallowed) { return false; },
          [&](DeclarativeTypeDescriptor::EqualityDelegated) {
            return backing_type()->SupportsEquality();
          }),
      data_.equality_strategy());
}

bool DeclarativeType::SupportsGroupingImpl(
    const LanguageOptions& language_options,
    const Type** no_grouping_type) const {
  bool supports_grouping = std::visit(
      absl::Overload(
          [](DeclarativeTypeDescriptor::EqualityDisallowed) { return false; },
          [&](DeclarativeTypeDescriptor::EqualityDelegated) {
            // We do not care which part of the backing type caused the issue.
            // We will report the current type as the one not supporting
            // grouping below, before returning.
            return backing_type()->SupportsGroupingImpl(
                language_options,
                /*no_grouping_type=*/nullptr);
          }),
      data_.equality_strategy());

  if (!supports_grouping && no_grouping_type != nullptr) {
    *no_grouping_type = this;
  }
  return supports_grouping;
}

bool DeclarativeType::SupportsPartitioningImpl(
    const LanguageOptions& language_options,
    const Type** no_partitioning_type) const {
  bool supports_partitioning = std::visit(
      absl::Overload(
          [](DeclarativeTypeDescriptor::EqualityDisallowed) { return false; },
          [&](DeclarativeTypeDescriptor::EqualityDelegated) {
            // We do not care which part of the backing type caused the issue.
            // We will report the current type as the one not supporting
            // partitioning below, before returning.
            return backing_type()->SupportsPartitioningImpl(
                language_options, /*no_partitioning_type=*/nullptr);
          }),
      data_.equality_strategy());
  if (!supports_partitioning && no_partitioning_type != nullptr) {
    *no_partitioning_type = this;
  }
  return supports_partitioning;
}

bool DeclarativeType::SupportsReturningImpl(
    const LanguageOptions& language_options,
    const Type** no_returning_type) const {
  bool supports_returning = std::visit(
      absl::Overload(
          [](DeclarativeTypeDescriptor::ReturningDisallowed) { return false; },
          [&](DeclarativeTypeDescriptor::ReturningDelegated) {
            // We do not care which part of the backing type caused the issue.
            // We will report the current type as the one not supporting
            // returning below, before returning.
            return backing_type()->SupportsReturningImpl(
                language_options, /*no_returning_type=*/nullptr);
          }),
      data_.returning_strategy());
  if (!supports_returning && no_returning_type != nullptr) {
    *no_returning_type = this;
  }
  return supports_returning;
}

bool DeclarativeType::SupportsOrdering(const LanguageOptions& language_options,
                                       std::string* type_description) const {
  // TODO: Support ordering strategies.
  return false;
}

void DeclarativeType::ClearValueContent(const ValueContent& value) const {
  if (backing_type()->UsesExtendedInlineValueContent()) {
    value.GetAs<internal::ValueContentRef*>()->Unref();
  } else {
    backing_type()->ClearValueContent(value);
  }
}

void DeclarativeType::CopyValueContent(const ValueContent& from,
                                       ValueContent* to) const {
  if (backing_type()->UsesExtendedInlineValueContent()) {
    from.GetAs<internal::ValueContentRef*>()->Ref();
    *to = from;
  } else {
    backing_type()->CopyValueContent(from, to);
  }
}

std::string DeclarativeType::CapitalizedName() const {
  return data_.display_name();
}

bool DeclarativeType::IsSupportedType(
    const LanguageOptions& language_options) const {
  if (!backing_type()->IsSupportedType(language_options)) {
    return false;
  }
  for (LanguageFeature feature :
       data_.additional_required_language_features()) {
    if (!language_options.LanguageFeatureEnabled(feature)) {
      return false;
    }
  }
  return true;
}

int64_t DeclarativeType::GetEstimatedOwnedMemoryBytesSize() const {
  return sizeof(*this) +
         internal::GetExternallyAllocatedMemoryEstimate(
             candidate_super_types_) +
         data_.GetEstimatedOwnedMemoryBytesSize() - sizeof(data_);
}

uint64_t DeclarativeType::GetValueContentExternallyAllocatedByteSize(
    const ValueContent& value) const {
  if (backing_type()->UsesExtendedInlineValueContent()) {
    return value.GetAs<internal::ValueContentRef*>()->physical_byte_size();
  }
  return backing_type()->GetValueContentExternallyAllocatedByteSize(value);
}

static DeclarativeTypeProto::AllowCoercionMode CoercionModeToProto(
    DeclarativeTypeDescriptor::AllowCoercionMode coercion_mode) {
  switch (coercion_mode) {
    case DeclarativeTypeDescriptor::AllowCoercionMode::kNoCoercion:
      return DeclarativeTypeProto::ALLOW_COERCION_MODE_NO_COERCION;
    case DeclarativeTypeDescriptor::AllowCoercionMode::kExplicitOnly:
      return DeclarativeTypeProto::ALLOW_COERCION_MODE_EXPLICIT_ONLY;
    case DeclarativeTypeDescriptor::AllowCoercionMode::kAllowAllCoercion:
      return DeclarativeTypeProto::ALLOW_COERCION_MODE_ALLOW_ALL_COERCION;
  }
}

absl::Status DeclarativeType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind());
  DeclarativeTypeProto* declarative_type_proto =
      type_proto->mutable_declarative_type();
  DeclarativeTypeProto::TypeIdProto* type_id_proto =
      declarative_type_proto->mutable_type_id();

  type_id_proto->set_name_space(id().name_space);
  type_id_proto->set_local_id(id().local_id);
  type_id_proto->set_version_id(id().version_id);
  declarative_type_proto->set_display_name(data_.display_name());

  declarative_type_proto->set_coercion_from_backing_type(
      CoercionModeToProto(data_.coercion_from_backing_type()));

  declarative_type_proto->set_coercion_to_backing_type(
      CoercionModeToProto(data_.coercion_to_backing_type()));

  DeclarativeTypeProto::ReturningStrategy returning_strategy;
  std::visit(
      absl::Overload(
          [&](DeclarativeTypeDescriptor::ReturningDelegated) {
            returning_strategy = DeclarativeTypeProto::RETURNING_DELEGATED;
          },
          [&](DeclarativeTypeDescriptor::ReturningDisallowed) {
            returning_strategy = DeclarativeTypeProto::RETURNING_DISALLOWED;
          }),
      data_.returning_strategy());
  declarative_type_proto->set_returning_strategy(returning_strategy);

  DeclarativeTypeProto::EqualityStrategy equality_strategy;
  std::visit(absl::Overload(
                 [&](DeclarativeTypeDescriptor::EqualityDelegated) {
                   equality_strategy = DeclarativeTypeProto::EQUALITY_DELEGATED;
                 },
                 [&](DeclarativeTypeDescriptor::EqualityDisallowed) {
                   equality_strategy =
                       DeclarativeTypeProto::EQUALITY_DISALLOWED;
                 }),
             data_.equality_strategy());
  declarative_type_proto->set_equality_strategy(equality_strategy);

  // Serialize the type parameters strategy.
  // If the type has an opaque C++ callback, obviously we cannot serialize the
  // callback itself, so we indicate that the strategy is TYPE_PARAMS_CUSTOM.
  //
  // It is the deserializer's responsibility to ensure that the correct
  // callback is set.
  DeclarativeTypeProto::TypeParamsStrategy type_params_strategy;
  std::visit(
      absl::Overload(
          [&](DeclarativeTypeDescriptor::TypeParamsDisallowed) {
            type_params_strategy = DeclarativeTypeProto::TYPE_PARAMS_DISALLOWED;
          },
          [&](const TypeParameterHandlers&) {
            type_params_strategy = DeclarativeTypeProto::TYPE_PARAMS_CUSTOM;
          }),
      data_.type_params_strategy());
  declarative_type_proto->set_type_params_strategy(type_params_strategy);

  // Serialize the formatting strategy.
  // If the type has an opaque C++ callback, obviously we cannot serialize the
  // callback itself, but we indicate that the strategy is FORMATTING_CUSTOM.
  //
  // It is the deserializer's responsibility to ensure that the correct callback
  // is set.
  DeclarativeTypeProto::FormattingStrategy formatting_strategy;
  std::visit(
      absl::Overload(
          [&](DeclarativeTypeDescriptor::FormattingDisallowed) {
            formatting_strategy = DeclarativeTypeProto::FORMATTING_DISALLOWED;
          },
          [&](const DeclarativeTypeDescriptor::FormattingCustom&) {
            formatting_strategy = DeclarativeTypeProto::FORMATTING_CUSTOM;
          }),
      data_.formatting_strategy());
  declarative_type_proto->set_formatting_strategy(formatting_strategy);

  // Serialize the backing type
  GOOGLESQL_RETURN_IF_ERROR(
      backing_type()->SerializeToProtoAndDistinctFileDescriptorsImpl(
          options, declarative_type_proto->mutable_backing_type(),
          file_descriptor_set_map));

  // Serialize additional required language features deterministically
  std::vector<LanguageFeature> sorted_features(
      data_.additional_required_language_features().begin(),
      data_.additional_required_language_features().end());
  std::sort(sorted_features.begin(), sorted_features.end());
  for (LanguageFeature feature : sorted_features) {
    declarative_type_proto->add_additional_required_language_features(feature);
  }

  return absl::OkStatus();
}

static bool AreSame(
    const DeclarativeTypeDescriptor::ReturningStrategy& returning_strategy1,
    const DeclarativeTypeDescriptor::ReturningStrategy& returning_strategy2) {
  return returning_strategy1.index() == returning_strategy2.index();
}

static bool AreSame(
    const DeclarativeTypeDescriptor::EqualityStrategy& equality_strategy1,
    const DeclarativeTypeDescriptor::EqualityStrategy& equality_strategy2) {
  return equality_strategy1.index() == equality_strategy2.index();
}


bool DeclarativeType::IsIdenticalTo(const DeclarativeType* other) const {
  if (other == nullptr) {
    return false;
  }
  return data_.IsIdenticalTo(other->data_);
}

bool DeclarativeTypeDescriptor::IsIdenticalTo(
    const DeclarativeTypeDescriptor& other) const {
  return type_id() == other.type_id() &&
         display_name() == other.display_name() &&
         backing_type()->Equals(other.backing_type()) &&
         coercion_from_backing_type() == other.coercion_from_backing_type() &&
         coercion_to_backing_type() == other.coercion_to_backing_type() &&
         AreSame(returning_strategy(), other.returning_strategy()) &&
         AreSame(equality_strategy(), other.equality_strategy()) &&
         formatting_strategy() == other.formatting_strategy() &&
         type_params_strategy() == other.type_params_strategy() &&
         additional_required_language_features() ==
             other.additional_required_language_features();
}

bool DeclarativeType::EqualsForSameKind(const Type* that,
                                        bool equivalent) const {
  const DeclarativeType* other = that->AsDeclarativeType();
  ABSL_DCHECK(other != nullptr);
  if (id() == other->id()) {
    ABSL_DCHECK(IsIdenticalTo(other));
    return true;
  }
  return false;
}

void DeclarativeType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                                      std::string* debug_string) const {
  absl::StrAppend(debug_string, data_.display_name());
}

const ValueContent& DeclarativeType::GetBackingContent(
    const ValueContent& value_content) const {
  if (!backing_type()->UsesExtendedInlineValueContent()) {
    return value_content;
  }
  // The ValueContent is just a pointer to the wider content which is stored
  // externally on the heap via ValueContentRef. This is the case for TIME
  // and DATETIME which use a 12-byte ValueContent that cannot fit in
  // Value::Metadata.
  return value_content.GetAs<internal::ValueContentRef*>()->value();
}

bool DeclarativeType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  // We do not consider the SQL-level equality trait, which is controlled by the
  // descriptor and is used by the resolver to reject queries attempting the
  // equality operation in the first place.
  //
  // This is the low-level C++ comparison, so we blindly delegate to the
  // backing type. This is needed for compliance testing, for example, where we
  // have to compare the engine result vs the golden result.
  return backing_type()->ValueContentEquals(GetBackingContent(x),
                                            GetBackingContent(y), options);
}

bool DeclarativeType::ValueContentLess(const ValueContent& x,
                                       const ValueContent& y,
                                       const Type* other_type) const {
  // When we define `ordering_strategy`, we should ABSL_DCHECK() that it's delegated
  // to the backing type. Any custom ordering will be handled before we hit
  // this point, and we should never execute this code path in that case.
  return backing_type()->ValueContentLess(GetBackingContent(x),
                                          GetBackingContent(y), backing_type());
}

absl::HashState DeclarativeType::HashValueContent(const ValueContent& value,
                                                  absl::HashState state) const {
  return backing_type()->HashValueContent(GetBackingContent(value),
                                          std::move(state));
}

absl::HashState DeclarativeType::HashValueContentIgnoringFloat(
    const ValueContent& value, absl::HashState state) const {
  return backing_type()->HashValueContentIgnoringFloat(GetBackingContent(value),
                                                       std::move(state));
}

absl::HashState DeclarativeType::HashTypeParameter(
    absl::HashState state) const {
  // Combine the type's unique DeclarativeTypeId into the hash state so that
  // declarative values with identical backing content but distinct declarative
  // types produce distinct hash codes.
  ABSL_DCHECK(ComponentTypes().empty());
  return absl::HashState::combine(std::move(state), id());
}

using FormatOptions =
    DeclarativeTypeDescriptor::FormattingCustom::FormatOptions;

std::string DeclarativeType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  // The formatting strategy dictates the logic.
  return std::visit(
      absl::Overload(
          // `FormattingDisallowed` means the type doesn't support formatting
          // in the first place. We should never hit this code path.
          [](DeclarativeTypeDescriptor::FormattingDisallowed) -> std::string {
            return "ERROR('Unimplemented')";
          },
          // Opaque callback: call the specified custom callback.
          [&](const DeclarativeTypeDescriptor::FormattingCustom& custom)
              -> std::string {
            ABSL_DCHECK(custom.callback != nullptr);
            if (custom.callback == nullptr) {
              // This should never happen. With this strategy, the callback must
              // always be set.
              return "<ERROR>";
            }

            FormatOptions opts;
            switch (options.mode) {
              case FormatValueContentOptions::Mode::kDebug:
                opts.mode = FormatOptions::Mode::kDebug;
                break;
              case FormatValueContentOptions::Mode::kSQLLiteral:
                opts.mode = FormatOptions::Mode::kSQLLiteral;
                break;
              case FormatValueContentOptions::Mode::kSQLExpression:
                opts.mode = FormatOptions::Mode::kSQLExpression;
                break;
            }
            return custom.callback(GetBackingContent(value), opts);
          }),
      data_.formatting_strategy());
}

absl::Status DeclarativeType::SerializeValueContent(
    const ValueContent& value, ValueProto* value_proto) const {
  return backing_type()->SerializeValueContent(GetBackingContent(value),
                                               value_proto);
}

absl::Status DeclarativeType::DeserializeValueContent(
    const ValueProto& value_proto, ValueContent* value) const {
  return absl::FailedPreconditionError(
      "DeserializeValueContent should not be called for DECLARATIVE.");
}

}  // namespace googlesql
