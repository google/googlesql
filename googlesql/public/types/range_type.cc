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

#include "googlesql/public/types/range_type.h"

#include <string>
#include <utility>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

std::string RangeType::ShortTypeName(ProductMode mode) const {
  return absl::StrCat("RANGE<", element_type_->ShortTypeName(mode), ">");
}

std::string RangeType::TypeName(ProductMode mode) const {
  return absl::StrCat("RANGE<", element_type_->TypeName(mode), ">");
}

absl::StatusOr<std::string> RangeType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode) const {
  const TypeParameters& type_params = type_modifiers.type_parameters();
  if (!type_params.IsEmpty() && type_params.num_children() != 1) {
    return MakeSqlError()
           << "Input type parameter does not correspond to RangeType";
  }

  const Collation& collation = type_modifiers.collation();
  // TODO: Implement logic to print type name with collation.
  GOOGLESQL_RET_CHECK(collation.Empty());
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string element_type_name,
      element_type_->TypeNameWithModifiers(
          TypeModifiers::MakeTypeModifiers(
              type_params.IsEmpty() ? TypeParameters() : type_params.child(0),
              Collation()),
          mode));
  return absl::StrCat("RANGE<", element_type_name, ">");
}

std::string RangeType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_RANGE);  // Crash OK
  // TODO: Audit use of DebugString. Should this use CapitalizedName?
  return absl::StrCat("Range<", element_type_->DebugString(), ">");
}

bool RangeType::IsSupportedType(const LanguageOptions& language_options) const {
  if (!language_options.LanguageFeatureEnabled(FEATURE_RANGE_TYPE)) {
    return false;
  }

  return IsValidElementType(element_type_) &&
         element_type_->IsSupportedType(language_options);
}

RangeType::RangeType(const TypeFactoryBase* factory, const Type* element_type)
    : ListBackedType(factory, TYPE_RANGE), element_type_(element_type) {
  // Also blocked in TypeFactory::MakeRangeType.
  ABSL_DCHECK(IsValidElementType(element_type_));
}
RangeType::~RangeType() = default;

bool RangeType::IsValidElementType(const Type* element_type) {
  // Range element types must be equatable and orderable.
  if (!element_type->SupportsOrdering() || !element_type->SupportsEquality()) {
    return false;
  }
  return IsSupportedElementTypeKind(element_type->kind());
}

absl::Status RangeType::ValidateResolvedTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) const {
  // type_parameters must be empty or has the one child.
  if (type_parameters.IsEmpty()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK_EQ(type_parameters.num_children(), 1);
  return element_type_->ValidateResolvedTypeParameters(type_parameters.child(0),
                                                       mode);
}

bool RangeType::IsSupportedElementTypeKind(const TypeKind element_type_kind) {
  return element_type_kind == TYPE_DATE || element_type_kind == TYPE_DATETIME ||
         element_type_kind == TYPE_TIMESTAMP;
}

bool RangeType::EqualsImpl(const RangeType* const type1,
                           const RangeType* const type2, bool equivalent) {
  return type1->element_type()->EqualsImpl(type2->element_type(), equivalent);
}

absl::Status RangeType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind_);
  return element_type()->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto->mutable_range_type()->mutable_element_type(),
      file_descriptor_set_map);
}

bool RangeType::EqualsForSameKind(const Type* that, bool equivalent) const {
  const RangeType* other = that->AsRange();
  ABSL_DCHECK(other);
  return EqualsImpl(this, other, equivalent);
}

void RangeType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                                std::string* debug_string) const {
  absl::StrAppend(debug_string, "RANGE<");
  stack->push_back(">");
  stack->push_back(element_type());
}

void RangeType::CopyValueContent(const ValueContent& from,
                                 ValueContent* to) const {
  from.GetAs<internal::ValueContentOrderedListRef*>()->Ref();
  *to = from;
}

void RangeType::ClearValueContent(const ValueContent& value) const {
  value.GetAs<internal::ValueContentOrderedListRef*>()->Unref();
}

absl::HashState RangeType::HashTypeParameter(absl::HashState state) const {
  // Range types are equivalent if their element types are equivalent,
  // so we hash the element type kind.
  return element_type()->Hash(std::move(state));
}

absl::HashState RangeType::HashValueContent(const ValueContent& value,
                                            absl::HashState state) const {
  absl::HashState result = absl::HashState::Create(&state);
  const internal::ValueContentOrderedList* container =
      value.GetAs<internal::ValueContentOrderedListRef*>()->value();
  ABSL_DCHECK_EQ(container->num_elements(), 2);
  NullableValueContentHasher hasher(element_type());
  const internal::NullableValueContent& start = container->element(0);
  result = absl::HashState::combine(std::move(result), hasher(start));
  const internal::NullableValueContent& end = container->element(1);
  result = absl::HashState::combine(std::move(result), hasher(end));
  return result;
}

std::string RangeType::FormatNullableValueContent(
    const internal::NullableValueContent& element,
    const Type::FormatValueContentOptions& options) const {
  std::string result;
  if (element.is_null()) {
    if (options.mode == Type::FormatValueContentOptions::Mode::kSQLLiteral ||
        options.mode == Type::FormatValueContentOptions::Mode::kSQLExpression) {
      return "UNBOUNDED";
    }
    result = "NULL";
  } else {
    Type::FormatValueContentOptions element_format_options = options;
    // Set mode to Debug to get elements formatted without added type prefix
    element_format_options.mode = Type::FormatValueContentOptions::Mode::kDebug;
    result = element_type()->FormatValueContent(element.value_content(),
                                                element_format_options);
  }

  if (options.mode == Type::FormatValueContentOptions::Mode::kDebug &&
      options.verbose) {
    return absl::StrCat(element_type()->CapitalizedName(), "(", result, ")");
  }
  return result;
}

std::string RangeType::FormatValueContent(
    const ValueContent& value,
    const Type::FormatValueContentOptions& options) const {
  if (!ThreadHasEnoughStack()) {
    return std::string(kFormatValueContentOutOfStackError);
  }

  const internal::ValueContentOrderedList* container =
      value.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::NullableValueContent& start = container->element(0);
  const internal::NullableValueContent& end = container->element(1);

  std::string boundaries =
      absl::StrCat("[", FormatNullableValueContent(start, options), ", ",
                   FormatNullableValueContent(end, options), ")");
  if (options.mode == Type::FormatValueContentOptions::Mode::kDebug) {
    if (options.verbose) {
      return absl::StrCat("Range", boundaries);
    }

    return boundaries;
  }
  return absl::StrCat(TypeName(options.product_mode), " ",
                      ToStringLiteral(boundaries));
}

bool RangeType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const internal::ValueContentOrderedList* x_container =
      x.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::ValueContentOrderedList* y_container =
      y.GetAs<internal::ValueContentOrderedListRef*>()->value();

  const internal::NullableValueContent& x_start = x_container->element(0);
  const internal::NullableValueContent& x_end = x_container->element(1);
  const internal::NullableValueContent& y_start = y_container->element(0);
  const internal::NullableValueContent& y_end = y_container->element(1);

  NullableValueContentEq eq(options, element_type());

  return eq(x_start, y_start) && eq(x_end, y_end);
}

bool RangeType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                                 const Type* other_type) const {
  const internal::ValueContentOrderedList* x_container =
      x.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::ValueContentOrderedList* y_container =
      y.GetAs<internal::ValueContentOrderedListRef*>()->value();

  const internal::NullableValueContent& x_start = x_container->element(0);
  const internal::NullableValueContent& x_end = x_container->element(1);
  const internal::NullableValueContent& y_start = y_container->element(0);
  const internal::NullableValueContent& y_end = y_container->element(1);

  const Type* x_element_type = element_type();
  const Type* y_element_type = other_type->AsRange()->element_type();

  ValueEqualityCheckOptions options;
  NullableValueContentEq eq(options, element_type());

  if (!eq(x_start, y_start)) {
    // [x_start, x_end) > [UNBOUNDED, y_end)
    // Unbounded start orders smallest, so if y has an unbounded start, then x
    // is always bigger than y.
    if (y_start.is_null()) {
      return false;
    }
    // [UNBOUNDED, x_end) < [y_start, y_end)
    // Unbounded start orders smallest, so if x has an unbounded start, then x
    // is always smaller than y.
    return x_start.is_null() ||
           NullableValueContentLess(x_start, y_start, x_element_type,
                                    y_element_type)
               .value_or(false);  // Otherwise, compare the start values.
  } else {
    // Starts are equal, so compare the ends.
    if (x_end.is_null() && y_end.is_null()) {
      // [S, UNBOUNDED) == [S, UNBOUNDED)
      return false;
    } else if (x_end.is_null()) {
      // [S, UNBOUNDED) > [S, y_end)
      // Unbounded end orders largest, so if x has an unbounded end, then for
      // equal start, x is always larger than y.
      return false;
    } else if (y_end.is_null()) {
      // [S, x_end) < [S, UNBOUNDED)
      // Unbounded end orders largest, so if y has an unbounded end, then for
      // equal start, y is always larger than x.
      return true;
    }
    return NullableValueContentLess(x_end, y_end, x_element_type,
                                    y_element_type)
        .value_or(false);
  }
}

absl::Status RangeType::SerializeValueContent(const ValueContent& value,
                                              ValueProto* value_proto) const {
  auto* range_proto = value_proto->mutable_range_value();
  const internal::ValueContentOrderedList* range_container =
      value.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::NullableValueContent& start = range_container->element(0);
  if (start.is_null()) {
    range_proto->mutable_start()->Clear();
  } else {
    GOOGLESQL_RETURN_IF_ERROR(element_type()->SerializeValueContent(
        start.value_content(), range_proto->mutable_start()));
  }
  const internal::NullableValueContent& end = range_container->element(1);
  if (end.is_null()) {
    range_proto->mutable_end()->Clear();
  } else {
    GOOGLESQL_RETURN_IF_ERROR(element_type()->SerializeValueContent(
        end.value_content(), range_proto->mutable_end()));
  }
  return absl::OkStatus();
}

absl::Status RangeType::DeserializeValueContent(const ValueProto& value_proto,
                                                ValueContent* value) const {
  // TODO: b/365163099 - Implement the deserialization logic here, instead of in
  // Value.
  return absl::FailedPreconditionError(
      "DeserializeValueContent should not be called for RANGE. The "
      "deserialization logic is implemented directly in the Value class.");
}

}  // namespace googlesql
