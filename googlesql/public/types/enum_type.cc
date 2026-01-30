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

#include "googlesql/public/types/enum_type.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/internal_proto_utils.h"
#include "googlesql/public/types/internal_utils.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/algorithm/container.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Gets an enumerator value from the given enumeration value content.
static int32_t GetEnumValue(const ValueContent& value) {
  return value.GetAs<int32_t>();
}

EnumType::EnumType(const TypeFactory* factory,
                   const google::protobuf::EnumDescriptor* enum_descr,
                   const internal::CatalogName* catalog_name, bool is_opaque)
    : Type(factory, TYPE_ENUM),
      enum_descriptor_(enum_descr),
      catalog_name_(catalog_name),
      is_opaque_(is_opaque) {
  ABSL_CHECK(enum_descriptor_ != nullptr);
}

EnumType::~EnumType() = default;

bool EnumType::EqualsForSameKind(const Type* that, bool equivalent) const {
  const EnumType* other = that->AsEnum();
  ABSL_DCHECK(other);
  return EnumType::EqualsImpl(this, other, equivalent);
}

void EnumType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                               std::string* debug_string) const {
  if (catalog_name_ != nullptr) {
    absl::StrAppend(debug_string, *catalog_name_->path_string, ".");
  }

  absl::StrAppend(debug_string, "ENUM<", RawEnumName());
  if (details) {
    absl::StrAppend(debug_string,
                    ", file name: ", enum_descriptor_->file()->name(), ", <",
                    enum_descriptor_->DebugString(), ">");
    if (is_opaque_) {
      absl::StrAppend(debug_string, ", opaque: ", is_opaque_);
    }
  }
  absl::StrAppend(debug_string, ">");
}

const google::protobuf::EnumDescriptor* EnumType::enum_descriptor() const {
  return enum_descriptor_;
}

absl::Status EnumType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind());
  EnumTypeProto* enum_type_proto = type_proto->mutable_enum_type();
  enum_type_proto->set_enum_name(enum_descriptor_->full_name());
  enum_type_proto->set_enum_file_name(enum_descriptor_->file()->name());
  // Note that right now we are not supporting TypeProto extensions.  The
  // FileDescriptorSet can be derived from the enum descriptor's FileDescriptor
  // dependencies.
  int set_index;
  GOOGLESQL_RETURN_IF_ERROR(internal::PopulateDistinctFileDescriptorSets(
      options, enum_descriptor_->file(), file_descriptor_set_map, &set_index));
  if (set_index != 0) {
    enum_type_proto->set_file_descriptor_set_index(set_index);
  }

  if (catalog_name_ != nullptr) {
    absl::c_copy(catalog_name_->path,
                 google::protobuf::RepeatedFieldBackInserter(
                     enum_type_proto->mutable_catalog_name_path()));
  }
  if (is_opaque_) {
    enum_type_proto->set_is_opaque(true);
  }
  return absl::OkStatus();
}

absl::string_view EnumType::RawEnumName() const {
  if (is_opaque_) {
    const OpaqueEnumTypeOptions& options =
        enum_descriptor()->options().GetExtension(opaque_enum_type_options);
    if (!options.sql_opaque_enum_name().empty()) {
      return options.sql_opaque_enum_name();
    }
  }
  return enum_descriptor()->full_name();
}

std::string EnumType::TypeName() const {
  std::string catalog_name_path;
  if (catalog_name_ != nullptr) {
    absl::StrAppend(&catalog_name_path, *catalog_name_->path_string, ".");
  }
  return absl::StrCat(catalog_name_path, ToIdentifierLiteral(RawEnumName()));
}

std::string EnumType::ShortTypeName() const {
  // Special case for built-in googlesql enums. Since ShortTypeName is used in
  // the user facing error messages, we need to make these enum names look
  // as special language elements.
  // These represent a legacy pseudo-opaque naming scheme that has been
  // used in some cases.
  if (enum_descriptor()->full_name() ==
      "googlesql.functions.DateTimestampPart") {
    return "DATE_TIME_PART";
  } else if (enum_descriptor()->full_name() ==
             "googlesql.functions.NormalizeMode") {
    return "NORMALIZE_MODE";
  }

  std::string catalog_name_path;
  if (catalog_name_ != nullptr) {
    absl::StrAppend(&catalog_name_path, *catalog_name_->path_string, ".");
  }

  absl::StrAppend(&catalog_name_path, RawEnumName());

  return catalog_name_path;
}

std::string EnumType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_ENUM);  // Crash OK
  if (AsEnum()->IsOpaque()) {
    return AsEnum()->ShortTypeName(ProductMode::PRODUCT_EXTERNAL);
  } else {
    return absl::StrCat("Enum<", AsEnum()->enum_descriptor()->full_name(), ">");
  }
}

std::string EnumType::TypeName(ProductMode mode_unused) const {
  return TypeName();
}

bool EnumType::FindName(int number, absl::string_view* name) const {
  *name = "";
  const google::protobuf::EnumValueDescriptor* value_descr =
      enum_descriptor_->FindValueByNumber(number);
  if (value_descr == nullptr) {
    return false;
  } else if (is_opaque_ && value_descr->options()
                               .GetExtension(opaque_enum_value_options)
                               .invalid_enum_value()) {
    return false;
  }
  *name = value_descr->name();
  return true;
}

absl::Span<const std::string> EnumType::CatalogNamePath() const {
  if (catalog_name_ == nullptr) {
    return {};
  } else {
    return catalog_name_->path;
  }
}

bool EnumType::IsValidEnumValue(
    const google::protobuf::EnumValueDescriptor* value_descriptor) const {
  if (value_descriptor == nullptr) {
    return false;
  }
  if (IsOpaque()) {
    // Opaque enum may mark some values as invalid, even though the exist
    // in the proto enum.
    return !value_descriptor->options()
                .GetExtension(opaque_enum_value_options)
                .invalid_enum_value();
  }
  return true;
}

bool EnumType::FindNumber(absl::string_view name, int* number) const {
  const google::protobuf::EnumValueDescriptor* value_descr =
      enum_descriptor_->FindValueByName(name);
  if (!IsValidEnumValue(value_descr)) {
    *number = std::numeric_limits<int32_t>::min();
    return false;
  }
  *number = value_descr->number();
  return true;
}

bool EnumType::EqualsImpl(const EnumType* const type1,
                          const EnumType* const type2, bool equivalent) {
  const internal::CatalogName* catalog_name1 = type1->catalog_name_;
  const internal::CatalogName* catalog_name2 = type2->catalog_name_;
  const bool catalogs_are_empty =
      catalog_name1 == nullptr && catalog_name2 == nullptr;
  const bool catalogs_are_equal =
      catalog_name1 != nullptr && catalog_name2 != nullptr &&
      *catalog_name1->path_string == *catalog_name2->path_string;
  const bool opaque_are_equal = type1->IsOpaque() == type2->IsOpaque();
  if (type1->enum_descriptor() == type2->enum_descriptor() &&
      opaque_are_equal && (catalogs_are_empty || catalogs_are_equal)) {
    return true;
  }

  if (equivalent && opaque_are_equal &&
      type1->enum_descriptor()->full_name() ==
          type2->enum_descriptor()->full_name()) {
    return true;
  }
  return false;
}

bool EnumType::IsSupportedType(const LanguageOptions& language_options) const {
  auto status_or_is_supported = IsSupported(language_options);
  GOOGLESQL_DCHECK_OK(status_or_is_supported);
  return status_or_is_supported.value_or(false);
}

// TODO: b/328508766 - respect is_opaque_ for builtin enum type instead of
// language feature
absl::StatusOr<bool> EnumType::IsSupported(
    const LanguageOptions& language_options) const {
  if (Equivalent(types::DifferentialPrivacyReportFormatEnumType())) {
    return language_options.LanguageFeatureEnabled(
               FEATURE_DIFFERENTIAL_PRIVACY) &&
           language_options.LanguageFeatureEnabled(
               FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS);
  }
  // If below enums were not created as a builtin type, falls through to
  // the generic logic below.
  GOOGLESQL_ASSIGN_OR_RETURN(const EnumType* rank_type_enum_type,
                   types::RankTypeEnumType());
  if (is_opaque_ && (Equivalent(types::ArrayZipModeEnumType()) ||
                     Equivalent(types::UnsupportedFieldsEnumType()) ||
                     Equivalent(types::BitwiseAggModeEnumType()) ||
                     Equivalent(rank_type_enum_type))) {
    return true;
  }

  if (Equivalent(types::RangeSessionizeModeEnumType())) {
    return language_options.LanguageFeatureEnabled(FEATURE_RANGE_TYPE);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      const EnumType* count_distinct_contribution_bounding_strategy_enum_type,
      types::
          DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType());  // NOLINT
  if (Equivalent(count_distinct_contribution_bounding_strategy_enum_type)) {
    return language_options.LanguageFeatureEnabled(
        FEATURE_DIFFERENTIAL_PRIVACY);
  }

  if (language_options.LanguageFeatureEnabled(FEATURE_PROTO_BASE)) {
    return true;
  }

  if (language_options.product_mode() == ProductMode::PRODUCT_EXTERNAL &&
      !Equivalent(types::DatePartEnumType()) &&
      !Equivalent(types::NormalizeModeEnumType()) &&
      !Equivalent(types::RoundingModeEnumType())) {
    return false;
  }

  return true;
}

absl::HashState EnumType::HashTypeParameter(absl::HashState state) const {
  // Enum types are equivalent if they have the same full name, so hash it.
  return absl::HashState::combine(std::move(state),
                                  enum_descriptor()->full_name(), is_opaque_);
}

absl::HashState EnumType::HashValueContent(const ValueContent& value,
                                           absl::HashState state) const {
  return absl::HashState::combine(std::move(state), GetEnumValue(value));
}

bool EnumType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  return GetEnumValue(x) == GetEnumValue(y);
}

bool EnumType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                                const Type* other_type) const {
  return GetEnumValue(x) < GetEnumValue(y);
}

std::string EnumType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  absl::string_view enum_name;
  int32_t enum_value = GetEnumValue(value);
  if (!FindName(enum_value, &enum_name)) {
    if (options.mode == FormatValueContentOptions::Mode::kDebug ||
        options.as_literal()) {
      return absl::StrCat(enum_value);
    }
    return internal::GetCastExpressionString(absl::StrCat(enum_value), this,
                                             options.product_mode);
  }

  if (options.mode == FormatValueContentOptions::Mode::kDebug) {
    return options.verbose ? absl::StrCat(enum_name, ":", enum_value)
                           : std::string(enum_name);
  }

  std::string literal = ToStringLiteral(enum_name);
  return options.as_literal() ? literal
                              : internal::GetCastExpressionString(
                                    literal, this, options.product_mode);
}

absl::Status EnumType::SerializeValueContent(const ValueContent& value,
                                             ValueProto* value_proto) const {
  value_proto->set_enum_value(GetEnumValue(value));
  return absl::OkStatus();
}

absl::Status EnumType::DeserializeValueContent(const ValueProto& value_proto,
                                               ValueContent* value) const {
  if (!value_proto.has_enum_value()) {
    return TypeMismatchError(value_proto);
  }

  const google::protobuf::EnumValueDescriptor* value_descriptor =
      enum_descriptor()->FindValueByNumber(value_proto.enum_value());

  if (!EnumAllowsUnnamedValues() && !IsValidEnumValue(value_descriptor)) {
    return absl::Status(absl::StatusCode::kOutOfRange,
                        absl::StrCat("Invalid value for ", DebugString(), ": ",
                                     value_proto.enum_value()));
  }

  value->set(value_proto.enum_value());

  return absl::OkStatus();
}

}  // namespace googlesql
