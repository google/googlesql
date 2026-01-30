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

#include "googlesql/public/types/array_type.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/container_type.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/base/attributes.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

struct HashableValueContentContainerElementIgnoringFloat {
  explicit HashableValueContentContainerElementIgnoringFloat(
      const internal::NullableValueContent element, const Type* type)
      : element(element), type(type) {}
  const internal::NullableValueContent element;
  const Type* type;

  template <typename H>
  H Hash(H h) const {
    type->HashValueContent(element.value_content(),
                           absl::HashState::Create(&h));
    return h;
  }

  template <typename H>
  friend H AbslHashValue(
      H h, const HashableValueContentContainerElementIgnoringFloat& v) {
    const bool is_null = v.element.is_null();
    if (is_null) {
      return H::combine(std::move(h), is_null);
    }
    switch (v.type->kind()) {
      case TYPE_FLOAT:
        h = H::combine(std::move(h), TYPE_FLOAT);
        break;
      case TYPE_DOUBLE:
        h = H::combine(std::move(h), TYPE_DOUBLE);
        break;
      case TYPE_ARRAY: {
        // TODO: we should use H::combine_unordered instead of summing the
        // hashes of elements.
        absl::Hash<HashableValueContentContainerElementIgnoringFloat>
            element_hasher;
        size_t combined_hash = 1;
        const internal::ValueContentOrderedList* container =
            v.element.value_content()
                .GetAs<internal::ValueContentOrderedListRef*>()
                ->value();
        for (int i = 0; i < container->num_elements(); i++) {
          const Type* element_type = v.type->AsArray()->element_type();
          combined_hash +=
              element_hasher(HashableValueContentContainerElementIgnoringFloat(
                  container->element(i), element_type));
        }
        h = H::combine(std::move(h), combined_hash, container->num_elements(),
                       TYPE_ARRAY);
        break;
      }
      case TYPE_STRUCT: {
        const internal::ValueContentOrderedList* container =
            v.element.value_content()
                .GetAs<internal::ValueContentOrderedListRef*>()
                ->value();
        absl::Hash<HashableValueContentContainerElementIgnoringFloat>
            field_hasher;
        for (int i = 0; i < container->num_elements(); i++) {
          const StructType* struct_type = v.type->AsStruct();
          const Type* field_type = struct_type->field(i).type;
          h = H::combine(
              std::move(h),
              field_hasher(HashableValueContentContainerElementIgnoringFloat(
                  container->element(i), field_type)));
        }
        h = H::combine(std::move(h), container->num_elements(), TYPE_STRUCT);
        break;
      }
      case TYPE_PROTO: {
        bool has_floating_point_fields = v.type->HasFloatingPointFields();
        if (has_floating_point_fields) {
          h = H::combine(std::move(h), TYPE_PROTO, has_floating_point_fields);
          break;
        }
        h = H::combine(std::move(h), has_floating_point_fields);
        ABSL_FALLTHROUGH_INTENDED;
      }
      default:
        h = v.Hash(std::move(h));
        break;
    }
    return H::combine(std::move(h), is_null);
  }
};

// Hasher used by EqualElementMultiSet in tests only.
struct MultisetValueContentContainerElementHasher {
  explicit MultisetValueContentContainerElementHasher(
      FloatMargin float_margin_arg, const Type* type)
      : float_margin(float_margin_arg), type(type) {}

  size_t operator()(const internal::NullableValueContent& x) const {
    if (!float_margin.IsExactEquality()) {
      return absl::Hash<HashableValueContentContainerElementIgnoringFloat>()(
          HashableValueContentContainerElementIgnoringFloat(x, type));
    }
    return absl::Hash<ListBackedType::HashableNullableValueContent>()(
        ListBackedType::HashableNullableValueContent{x, type});
  }

 private:
  FloatMargin float_margin;
  const Type* type;
};

ArrayType::ArrayType(const TypeFactoryBase* factory, const Type* element_type)
    : ListBackedType(factory, TYPE_ARRAY), element_type_(element_type) {
  ABSL_CHECK(!element_type->IsArray());  // Blocked in MakeArrayType.
}

ArrayType::~ArrayType() = default;

bool ArrayType::IsSupportedType(const LanguageOptions& language_options) const {
  return element_type()->IsSupportedType(language_options);
}

bool ArrayType::EqualsForSameKind(const Type* that, bool equivalent) const {
  const ArrayType* other = that->AsArray();
  ABSL_DCHECK(other);
  return EqualsImpl(this, other, equivalent);
}

void ArrayType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                                std::string* debug_string) const {
  absl::StrAppend(debug_string, "ARRAY<");
  stack->push_back(">");
  stack->push_back(element_type());
}

bool ArrayType::SupportsOrdering(const LanguageOptions& language_options,
                                 std::string* type_description) const {
  if (language_options.LanguageFeatureEnabled(FEATURE_ARRAY_ORDERING) &&
      element_type()->SupportsOrdering(language_options,
                                       /*type_description=*/nullptr)) {
    return true;
  }
  if (type_description != nullptr) {
    if (language_options.LanguageFeatureEnabled(FEATURE_ARRAY_ORDERING)) {
      // If the ARRAY ordering feature is on, then arrays with orderable
      // elements are also orderable.  So return a <type_description> that
      // also indicates the type of the unorderable element.
      *type_description = absl::StrCat(
          TypeKindToString(this->kind(), language_options.product_mode()),
          " containing ",
          TypeKindToString(this->element_type()->kind(),
                           language_options.product_mode()));
    } else {
      // If the ARRAY ordering feature is not enabled then the returned
      // <type_description> is simply ARRAY.
      *type_description = TypeKindToString(this->kind(),
                                           language_options.product_mode());
    }
  }
  return false;
}

bool ArrayType::SupportsEquality() const {
  return element_type()->SupportsEquality();
}

bool ArrayType::SupportsGroupingImpl(const LanguageOptions& language_options,
                                     const Type** no_grouping_type) const {
  if (!language_options.LanguageFeatureEnabled(FEATURE_GROUP_BY_ARRAY)) {
    if (no_grouping_type != nullptr) {
      *no_grouping_type = this;
    }
    return false;
  }

  if (!element_type()->SupportsGroupingImpl(language_options,
                                            no_grouping_type)) {
    return false;
  }
  if (no_grouping_type != nullptr) {
    *no_grouping_type = nullptr;
  }
  return true;
}

bool ArrayType::SupportsPartitioningImpl(
    const LanguageOptions& language_options,
    const Type** no_partitioning_type) const {
  if (!language_options.LanguageFeatureEnabled(FEATURE_GROUP_BY_ARRAY)) {
    if (no_partitioning_type != nullptr) {
      *no_partitioning_type = this;
    }
    return false;
  }

  if (!element_type()->SupportsPartitioningImpl(language_options,
                                                no_partitioning_type)) {
    return false;
  }
  if (no_partitioning_type != nullptr) {
    *no_partitioning_type = nullptr;
  }
  return true;
}

absl::Status ArrayType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind_);
  return element_type()->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto->mutable_array_type()->mutable_element_type(),
      file_descriptor_set_map);
}

std::string ArrayType::ShortTypeName(ProductMode mode,
                                     bool use_external_float32) const {
  return absl::StrCat(
      "ARRAY<", element_type_->ShortTypeName(mode, use_external_float32), ">");
}

std::string ArrayType::TypeName(ProductMode mode,
                                bool use_external_float32) const {
  return absl::StrCat("ARRAY<",
                      element_type_->TypeName(mode, use_external_float32), ">");
}

absl::StatusOr<std::string> ArrayType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode,
    bool use_external_float32) const {
  const TypeParameters& type_params = type_modifiers.type_parameters();
  if (!type_params.IsEmpty() && type_params.num_children() != 1) {
    return MakeSqlError()
           << "Input type parameter does not correspond to ArrayType";
  }

  const Collation& collation = type_modifiers.collation();
  if (!collation.HasCompatibleStructure(this)) {
    return MakeSqlError() << "Input collation " << collation.DebugString()
                          << " is not compatible with type " << DebugString();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string element_type_name,
      element_type_->TypeNameWithModifiers(
          TypeModifiers::MakeTypeModifiers(
              type_params.IsEmpty() ? TypeParameters() : type_params.child(0),
              collation.Empty() ? Collation() : collation.child(0)),
          mode, use_external_float32));
  return absl::StrCat("ARRAY<", element_type_name, ">");
}

std::string ArrayType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_ARRAY);  // Crash OK
  // TODO: Audit use of DebugString. Should this use CapitalizedName?
  return absl::StrCat("Array<", this->AsArray()->element_type()->DebugString(),
                      ">");
}

absl::StatusOr<TypeParameters> ArrayType::ValidateAndResolveTypeParameters(
    const std::vector<TypeParameterValue>& type_parameter_values,
    ProductMode mode) const {
  return MakeSqlError() << ShortTypeName(mode, /*use_external_float32=*/false)
                        << " type cannot have type parameters by itself, it "
                           "can only have type parameters on its element type";
}

absl::Status ArrayType::ValidateResolvedTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) const {
  // type_parameters must be empty or has the one child.
  if (type_parameters.IsEmpty()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK_EQ(type_parameters.num_children(), 1);
  return element_type_->ValidateResolvedTypeParameters(type_parameters.child(0),
                                                       mode);
}

bool ArrayType::EqualsImpl(const ArrayType* const type1,
                           const ArrayType* const type2, bool equivalent) {
  return type1->element_type()->EqualsImpl(type2->element_type(), equivalent);
}

void ArrayType::CopyValueContent(const ValueContent& from,
                                 ValueContent* to) const {
  from.GetAs<internal::ValueContentOrderedListRef*>()->Ref();
  *to = from;
}

void ArrayType::ClearValueContent(const ValueContent& value) const {
  value.GetAs<internal::ValueContentOrderedListRef*>()->Unref();
}

absl::HashState ArrayType::HashTypeParameter(absl::HashState state) const {
  // Array types are equivalent if their element types are equivalent,
  // so we hash the element type kind.
  return element_type()->Hash(std::move(state));
}

absl::HashState ArrayType::HashValueContent(const ValueContent& value,
                                            absl::HashState state) const {
  const internal::ValueContentOrderedList* container =
      value.GetAs<internal::ValueContentOrderedListRef*>()->value();
  // We must hash arrays as if unordered to support hash_map and hash_set of
  // values containing arrays with order_kind()=kIgnoresOrder.
  // absl::Hash lacks support for unordered containers, so we create a
  // cheapo solution of just adding the hashcodes.
  size_t combined_hash = 1;
  for (int i = 0; i < container->num_elements(); i++) {
    NullableValueContentHasher hasher(element_type());
    combined_hash += hasher(container->element(i));
  }
  return absl::HashState::combine(std::move(state), combined_hash,
                                  container->num_elements());
}

// Compares arrays as multisets. Used in tests only. The current algorithm,
// which counts the number of the same elements, may return false negatives if
// !float_margin.IsExactEquality(). Specifically, the method may return 'false'
// on almost-equal bags if those contain elements for which approximate equality
// is non-transitive, e.g., {a, b, c} such that a~b==true, b~c==true,
// a~c==false. See a repro in value_test.cc:AlmostEqualsStructArray.
// TODO: potential fix is to implement Hopcroft-Karp algorithm:
// http://en.wikipedia.org/wiki/Hopcroft%E2%80%93Karp_algorithm
// Its complexity is O(|E|*sqrt(|V|)). Computing E requires |V|^2 comparisons,
// so we get O(|V|^2.5).
bool ArrayType::EqualElementMultiSet(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const internal::ValueContentOrderedList* x_container =
      x.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::ValueContentOrderedList* y_container =
      y.GetAs<internal::ValueContentOrderedListRef*>()->value();
  std::string* reason = options.reason;
  using CountMap =
      std::unordered_map<internal::NullableValueContent, int,
                         MultisetValueContentContainerElementHasher,
                         NullableValueContentEq>;

  MultisetValueContentContainerElementHasher hasher(options.float_margin,
                                                    element_type());
  NullableValueContentEq eq(options, element_type());
  CountMap x_multiset(x_container->num_elements(), hasher, eq);
  CountMap y_multiset(y_container->num_elements(), hasher, eq);
  ABSL_DCHECK_EQ(x_container->num_elements(), y_container->num_elements());
  for (int i = 0; i < x_container->num_elements(); i++) {
    x_multiset[x_container->element(i)]++;
    y_multiset[y_container->element(i)]++;
  }
  const auto& format_options = DebugFormatValueContentOptions();
  for (const auto& p : x_multiset) {
    const internal::NullableValueContent& element = p.first;
    auto it = y_multiset.find(element);
    if (it == y_multiset.end()) {
      if (reason) {
        absl::StrAppend(
            reason,
            absl::Substitute("Multiset element $0 of $1 is missing in $2\n",
                             FormatNullableValueContent(element, element_type(),
                                                        format_options),
                             FormatValueContent(x, format_options),
                             FormatValueContent(y, format_options)));
      }
      return false;
    }
    if (it->second != p.second) {
      if (reason) {
        absl::StrAppend(
            reason,
            absl::Substitute(
                "Number of occurrences of multiset element $0 is $1 and $2 "
                "respectively in multisets $3 and $4\n",
                FormatNullableValueContent(element, element_type(),
                                           format_options),
                p.second, it->second, FormatValueContent(x, format_options),
                FormatValueContent(y, format_options)));
      }
      return false;
    }
  }
  if (x_multiset.size() == y_multiset.size()) {
    return true;  // All of x is in y and the sizes agree.
  }
  if (reason) {
    // There exists an element in y that's missing from x. Report it.
    for (const auto& p : y_multiset) {
      const internal::NullableValueContent& element = p.first;
      if (x_multiset.find(element) == x_multiset.end()) {
        absl::StrAppend(
            reason,
            absl::Substitute("Multiset element $0 of $1 is missing in $2\n",
                             FormatNullableValueContent(element, element_type(),
                                                        format_options),
                             FormatValueContent(y, format_options),
                             FormatValueContent(x, format_options)));
      }
    }
    ABSL_DCHECK(!reason->empty());
  }
  return false;
}

bool ArrayType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const internal::ValueContentOrderedList* x_container =
      x.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::ValueContentOrderedList* y_container =
      y.GetAs<internal::ValueContentOrderedListRef*>()->value();
  if (x_container->num_elements() != y_container->num_elements()) {
    if (options.reason) {
      const auto& format_options = DebugFormatValueContentOptions();
      absl::StrAppend(
          options.reason,
          absl::Substitute(
              "Number of array elements is {$0} and {$1} in respective "
              "arrays {$2} and {$3}\n",
              x_container->num_elements(), y_container->num_elements(),
              FormatValueContent(x, format_options),
              FormatValueContent(y, format_options)));
    }
    return false;
  }

  // By default use options provided in arguments
  ValueEqualityCheckOptions const* element_options = &options;
  std::unique_ptr<ValueEqualityCheckOptions> options_copy = nullptr;
  if (options.deep_order_spec != nullptr) {
    options_copy = std::make_unique<ValueEqualityCheckOptions>(options);
    options_copy->deep_order_spec = &options.deep_order_spec->children[0];
    element_options = options_copy.get();
    if (options.deep_order_spec->ignores_order) {
      return EqualElementMultiSet(x, y, *element_options);
    }
  }

  NullableValueContentEq eq(*element_options, element_type());
  for (int i = 0; i < x_container->num_elements(); i++) {
    if (!eq(x_container->element(i), y_container->element(i))) {
      return false;
    }
  }
  return true;
}

bool ArrayType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                                 const Type* other_type) const {
  const internal::ValueContentOrderedList* x_container =
      x.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const internal::ValueContentOrderedList* y_container =
      y.GetAs<internal::ValueContentOrderedListRef*>()->value();
  const Type* x_element_type = element_type();
  const Type* y_element_type = other_type->AsArray()->element_type();
  for (int i = 0;
       i < std::min(x_container->num_elements(), y_container->num_elements());
       ++i) {
    const std::optional<bool> is_less = NullableValueContentLess(
        x_container->element(i), y_container->element(i), x_element_type,
        y_element_type);
    if (is_less.has_value()) return *is_less;
  }
  return x_container->num_elements() < y_container->num_elements();
}

absl::Status ArrayType::SerializeValueContent(const ValueContent& value,
                                              ValueProto* value_proto) const {
  const internal::ValueContentOrderedList* array_content =
      value.GetAs<internal::ValueContentOrderedListRef*>()->value();
  auto* array_value_proto = value_proto->mutable_array_value();

  for (int i = 0; i < array_content->num_elements(); ++i) {
    auto* element_value_proto = array_value_proto->add_element();
    const internal::NullableValueContent& element_value_content =
        array_content->element(i);
    if (!element_value_content.is_null()) {
      GOOGLESQL_RETURN_IF_ERROR(element_type()->SerializeValueContent(
          element_value_content.value_content(), element_value_proto));
    }
  }
  return absl::OkStatus();
}

absl::Status ArrayType::DeserializeValueContent(const ValueProto& value_proto,
                                                ValueContent* value) const {
  // TODO: b/365163099 - Implement the deserialization logic here, instead of in
  // Value.
  return absl::FailedPreconditionError(
      "DeserializeValueContent should not be called for ARRAY. The "
      "deserialization logic is implemented directly in the Value class.");
}

void ArrayType::FormatValueContentDebugModeImpl(
    const internal::ValueContentOrderedListRef* container_ref,
    const FormatValueContentOptions& options, std::string* result) const {
  const internal::ValueContentOrderedList* container = container_ref->value();

  if (options.verbose) {
    absl::StrAppend(
        result, container->num_elements() == 0 ? CapitalizedName() : "Array");
  }
  absl::StrAppend(result, "[");
  if (options.include_array_ordereness && container->num_elements() > 1) {
    absl::StrAppend(result, container_ref->preserves_order()
                                ? "known order: "
                                : "unknown order: ");
  }

  for (int i = 0; i < container->num_elements(); ++i) {
    const internal::NullableValueContent& element_value_content =
        container->element(i);
    if (i > 0) {
      absl::StrAppend(result, ", ");
    }

    absl::StrAppend(result,
                    DebugFormatNullableValueContentForContainer(
                        element_value_content, element_type(), options));
  }
  absl::StrAppend(result, "]");
}

void ArrayType::FormatValueContentSqlModeImpl(
    const internal::ValueContentOrderedListRef* container_ref,
    const FormatValueContentOptions& options, std::string* result) const {
  const internal::ValueContentOrderedList* container = container_ref->value();

  if (options.mode == Type::FormatValueContentOptions::Mode::kSQLExpression) {
    absl::StrAppend(
        result, TypeName(options.product_mode, options.use_external_float32));
  }
  absl::StrAppend(result, "[");

  for (int i = 0; i < container->num_elements(); ++i) {
    const internal::NullableValueContent& element_value_content =
        container->element(i);
    if (i > 0) {
      absl::StrAppend(result, ", ");
    }
    absl::StrAppend(result,
                    FormatNullableValueContent(element_value_content,
                                               element_type(), options));
  }
  absl::StrAppend(result, "]");
}

std::string ArrayType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  if (!ThreadHasEnoughStack()) {
    return std::string(kFormatValueContentOutOfStackError);
  }

  const internal::ValueContentOrderedListRef* container_ref =
      value.GetAs<internal::ValueContentOrderedListRef*>();
  std::string result;

  switch (options.mode) {
    case Type::FormatValueContentOptions::Mode::kDebug:
      FormatValueContentDebugModeImpl(container_ref, options, &result);
      return result;
    case Type::FormatValueContentOptions::Mode::kSQLLiteral:
    case Type::FormatValueContentOptions::Mode::kSQLExpression:
      FormatValueContentSqlModeImpl(container_ref, options, &result);
      return result;
  }
}

}  // namespace googlesql
