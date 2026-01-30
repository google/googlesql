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

#include "googlesql/public/types/map_type.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

std::string MapType::ShortTypeName(ProductMode mode,
                                   bool use_external_float32) const {
  return absl::StrCat(
      "MAP<", key_type_->ShortTypeName(mode, use_external_float32), ", ",
      value_type_->ShortTypeName(mode, use_external_float32), ">");
}

std::string MapType::TypeName(ProductMode mode,
                              bool use_external_float32) const {
  return absl::StrCat("MAP<", key_type_->TypeName(mode, use_external_float32),
                      ", ", value_type_->TypeName(mode, use_external_float32),
                      ">");
}

absl::StatusOr<std::string> MapType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode,
    bool use_external_float32) const {
  const TypeParameters& type_params = type_modifiers.type_parameters();
  const Collation& collation = type_modifiers.collation();

  // TODO: b/323931806 - Implement collation support for key and value.
  if (!collation.Empty()) {
    return MakeSqlError() << "MAP does not support collation on key and value";
  }

  TypeModifiers key_type_modifiers;
  TypeModifiers value_type_modifiers;

  if (type_params.num_children() == 2) {
    key_type_modifiers =
        TypeModifiers::MakeTypeModifiers(type_params.child(0), collation);
    value_type_modifiers =
        TypeModifiers::MakeTypeModifiers(type_params.child(1), collation);
  } else {
    if (!type_params.IsEmpty()) {
      return MakeSqlError() << "Type parameters are only supported on MAP key "
                               "and value, not on MAP itself";
    }
    key_type_modifiers =
        TypeModifiers::MakeTypeModifiers(TypeParameters(), Collation());
    value_type_modifiers =
        TypeModifiers::MakeTypeModifiers(TypeParameters(), Collation());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view key_type_name,
                   key_type_->TypeNameWithModifiers(key_type_modifiers, mode,
                                                    use_external_float32));
  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view value_type_name,
                   value_type_->TypeNameWithModifiers(
                       value_type_modifiers, mode, use_external_float32));

  return absl::StrCat("MAP<", key_type_name, ", ", value_type_name, ">");
}

std::string MapType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_MAP);  // Crash OK
  return absl::StrCat("Map<", GetMapKeyType(this)->CapitalizedName(), ", ",
                      GetMapValueType(this)->CapitalizedName(), ">");
}

bool MapType::SupportsOrdering(const LanguageOptions& language_options,
                               std::string* type_description) const {
  if (type_description != nullptr) {
    *type_description =
        TypeKindToString(this->kind(), language_options.product_mode());
  }
  return false;
}
bool MapType::SupportsEquality() const { return false; }

bool MapType::IsSupportedType(const LanguageOptions& language_options) const {
  return language_options.LanguageFeatureEnabled(FEATURE_MAP_TYPE) &&
         key_type_->IsSupportedType(language_options) &&
         key_type_->SupportsGrouping(language_options) &&
         value_type_->IsSupportedType(language_options);
}

int MapType::nesting_depth() const {
  return std::max(key_type_->nesting_depth(), value_type_->nesting_depth()) + 1;
}

absl::Status MapType::ValidateResolvedTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) const {
  // type_parameters must be empty or have 2 children.
  if (type_parameters.IsEmpty()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK_EQ(type_parameters.num_children(), 2);
  GOOGLESQL_RETURN_IF_ERROR(key_type_->ValidateResolvedTypeParameters(
      type_parameters.child(0), mode));
  return value_type_->ValidateResolvedTypeParameters(type_parameters.child(1),
                                                     mode);
}

MapType::MapType(const TypeFactoryBase* factory, const Type* key_type,
                 const Type* value_type)
    : ContainerType(factory, TYPE_MAP),
      key_type_(key_type),
      value_type_(value_type) {}
MapType::~MapType() = default;

bool MapType::SupportsGroupingImpl(const LanguageOptions& language_options,
                                   const Type** no_grouping_type) const {
  // Map does not currently support grouping. (broken link).
  *no_grouping_type = this;
  return false;
}

bool MapType::SupportsPartitioningImpl(
    const LanguageOptions& language_options,
    const Type** no_partitioning_type) const {
  // Map does not currently support partitioning. (broken link).
  *no_partitioning_type = this;
  return false;
}

absl::Status MapType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind_);
  GOOGLESQL_RETURN_IF_ERROR(key_type()->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto->mutable_map_type()->mutable_key_type(),
      file_descriptor_set_map));
  return value_type()->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto->mutable_map_type()->mutable_value_type(),
      file_descriptor_set_map);
}

bool MapType::EqualsForSameKind(const Type* that, bool equivalent) const {
  const MapType* other = that->AsMap();
  ABSL_DCHECK(other != nullptr)
      << DebugString() << "::EqualsForSameKind cannot compare to non-MAP type "
      << that->DebugString();
  return this->key_type()->EqualsImpl(other->key_type(), equivalent) &&
         this->value_type()->EqualsImpl(other->value_type(), equivalent);
}

void MapType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                              std::string* debug_string) const {
  absl::StrAppend(debug_string, "MAP<");
  stack->push_back(">");
  stack->push_back(value_type());
  stack->push_back(", ");
  stack->push_back(key_type());
}

void MapType::CopyValueContent(const ValueContent& from,
                               ValueContent* to) const {
  from.GetAs<internal::ValueContentMapRef*>()->Ref();
  *to = from;
}

void MapType::ClearValueContent(const ValueContent& value) const {
  value.GetAs<internal::ValueContentMapRef*>()->Unref();
}

absl::HashState MapType::HashTypeParameter(absl::HashState state) const {
  return value_type()->Hash(key_type()->Hash(std::move(state)));
}

struct MapType::ValueContentMapElementHasher {
  explicit ValueContentMapElementHasher(const Type* key_type,
                                        const Type* value_type)
      : key_type(key_type), value_type(value_type) {}

  size_t operator()(const internal::NullableValueContent& key,
                    const internal::NullableValueContent& value) const {
    // Since mapping a->b is not the same as mapping b->a, this hash must be
    // ordered.
    return absl::HashOf(absl::Hash<HashableNullableValueContent>()(
                            HashableNullableValueContent{key, key_type}),
                        absl::Hash<HashableNullableValueContent>()(
                            HashableNullableValueContent{value, value_type}));
  }

 private:
  const Type* key_type;
  const Type* value_type;
};

absl::HashState MapType::HashValueContent(const ValueContent& value,
                                          absl::HashState state) const {
  const internal::ValueContentMap* value_content_map =
      value.GetAs<internal::ValueContentMapRef*>()->value();
  std::vector<size_t> hashes;
  hashes.reserve(value_content_map->num_elements());
  ValueContentMapElementHasher hasher(key_type_, value_type_);
  for (const auto& [key, value] : *value_content_map) {
    hashes.push_back(hasher(key, value));
  }

  return absl::HashState::combine_unordered(absl::HashState::Create(&state),
                                            hashes.begin(), hashes.end());
}

bool MapType::LookupMapEntryEqualsExpected(
    const internal::ValueContentMap* lookup_map,
    const internal::NullableValueContent& lookup_key,
    const internal::NullableValueContent& expected_value,
    const NullableValueContentEq& value_eq,
    const ValueContent& formattable_lookup_content,
    const ValueContent& formattable_expected_content,
    const ValueEqualityCheckOptions& options,
    const ValueEqualityCheckOptions& key_equality_options) const {
  auto lookup_value_or_missing = lookup_map->GetContentMapValueByKey(
      lookup_key, key_type_, key_equality_options);

  if (!lookup_value_or_missing.has_value()) {
    if (options.reason) {
      const auto& format_options = DebugFormatValueContentOptions();
      absl::StrAppend(
          options.reason,
          absl::Substitute(
              "Key {$0} did not exist in both maps. Present in "
              "{$1} but not present in {$2}.\n",
              FormatNullableValueContent(lookup_key, key_type_, format_options),
              FormatValueContent(formattable_expected_content, format_options),
              FormatValueContent(formattable_lookup_content, format_options)));
    }
    return false;
  }
  if (!value_eq(expected_value, lookup_value_or_missing.value())) {
    if (options.reason) {
      const auto& format_options = DebugFormatValueContentOptions();
      absl::StrAppend(
          options.reason,
          absl::Substitute(
              "The value for key {$0} did not match. Value was {$1} and {$2} "
              "in respective maps {$3} and {$4}.\n",
              FormatNullableValueContent(lookup_key, key_type_, format_options),
              FormatNullableValueContent(expected_value, value_type_,
                                         format_options),
              FormatNullableValueContent(lookup_value_or_missing.value(),
                                         value_type_, format_options),
              FormatValueContent(formattable_expected_content, format_options),
              FormatValueContent(formattable_lookup_content, format_options)));
    }
    return false;
  }
  return true;
}

bool MapType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const internal::ValueContentMap* x_map =
      x.GetAs<internal::ValueContentMapRef*>()->value();
  const internal::ValueContentMap* y_map =
      y.GetAs<internal::ValueContentMapRef*>()->value();

  if (x_map == y_map) {
    return true;
  }

  if (x_map->num_elements() != y_map->num_elements()) {
    if (options.reason) {
      const auto& format_options = DebugFormatValueContentOptions();
      absl::StrAppend(
          options.reason,
          absl::Substitute("Number of map entries was not equal: {$0} and {$1} "
                           "in respective maps {$2} and {$3}\n",
                           x_map->num_elements(), y_map->num_elements(),
                           FormatValueContent(x, format_options),
                           FormatValueContent(y, format_options)));
    }
    return false;
  }

  auto key_options_ptr = std::make_unique<ValueEqualityCheckOptions>(options);
  auto value_options_ptr = std::make_unique<ValueEqualityCheckOptions>(options);
  ValueEqualityCheckOptions& key_options = *key_options_ptr;
  ValueEqualityCheckOptions& value_options = *value_options_ptr;

  // Additional comparisons are done (ex: checking key equality when doing key
  // lookups) so reason should not be propagated in order to avoid unrelated
  // messages.
  key_options.reason = nullptr;
  value_options.reason = nullptr;

  // If "allow_bags" is true, create a copy of options with populated
  // deep_order_spec
  if (options.deep_order_spec != nullptr) {
    ABSL_DCHECK_EQ(options.deep_order_spec->children.size(), 2)
        << "Malformed deep_order_spec. MAP spec should always have exactly 2 "
           "direct children representing key and value.";

    constexpr int kSpecMapKeyIndex = 0;
    constexpr int kSpecMapValueIndex = 1;

    key_options.deep_order_spec =
        &options.deep_order_spec->children[kSpecMapKeyIndex];
    value_options.deep_order_spec =
        &options.deep_order_spec->children[kSpecMapValueIndex];
  }

  NullableValueContentEq key_eq =
      NullableValueContentEq(key_options, key_type_);
  NullableValueContentEq value_eq =
      NullableValueContentEq(value_options, value_type_);

  auto x_it = x_map->begin();
  auto y_it = y_map->begin();

  // First, try a fast path for equality. For equal maps without types
  // affected by equality options, this will only require O(num_entries())
  // comparisons
  for (; x_it != x_map->end() && y_it != y_map->end(); ++x_it, ++y_it) {
    auto& [x_key, x_value] = *x_it;
    auto& [y_key, y_value] = *y_it;
    if ((!key_eq(x_key, y_key) || !value_eq(x_value, y_value))) {
      // If this fails, fall back to a more extensive equality check.
      break;
    }
  }

  // If the first loop fails to match all elements, we must compare remaining
  // entries by looking up each map's remaining keys in the opposite map and
  // comparing values.
  for (; x_it != x_map->end(); ++x_it) {
    auto& [x_key, x_value] = *x_it;
    if (!LookupMapEntryEqualsExpected(
            /*lookup_map=*/y_map,
            /*lookup_key=*/x_key,
            /*expected_value=*/x_value,
            /*value_eq=*/value_eq,
            /*formattable_lookup_content=*/y,
            /*formattable_expected_content=*/x,
            /*options=*/options,
            /*key_equality_options=*/key_options)) {
      return false;
    }
  }
  for (; y_it != y_map->end(); ++y_it) {
    auto& [y_key, y_value] = *y_it;
    if (!LookupMapEntryEqualsExpected(
            /*lookup_map=*/x_map,
            /*lookup_key=*/y_key,
            /*expected_value=*/y_value,
            /*value_eq=*/value_eq,
            /*formattable_lookup_content=*/x,
            /*formattable_expected_content=*/y,
            /*options=*/options,
            /*key_equality_options=*/key_options)) {
      return false;
    }
  }
  return true;
}

absl::Status MapType::SerializeValueContent(const ValueContent& value,
                                            ValueProto* value_proto) const {
  const internal::ValueContentMap* value_content_map =
      value.GetAs<internal::ValueContentMapRef*>()->value();
  auto* map_proto = value_proto->mutable_map_value();

  for (const auto& [key, value] : *value_content_map) {
    auto* map_entry = map_proto->add_entry();
    if (!key.is_null()) {
      GOOGLESQL_RETURN_IF_ERROR(key_type_->SerializeValueContent(
          key.value_content(), map_entry->mutable_key()));
    } else {
      // Populate the key with an empty ValueProto.
      map_entry->mutable_key();
    }
    if (!value.is_null()) {
      GOOGLESQL_RETURN_IF_ERROR(value_type_->SerializeValueContent(
          value.value_content(), map_entry->mutable_value()));
    } else {
      // Populate the value with an empty ValueProto.
      map_entry->mutable_value();
    }
  }
  return absl::OkStatus();
}

absl::Status MapType::DeserializeValueContent(const ValueProto& value_proto,
                                              ValueContent* value) const {
  // TODO: b/365163099 - Implement the deserialization logic here, instead of in
  // Value.
  GOOGLESQL_RET_CHECK_FAIL()
      << "DeserializeValueContent should not be called for MAP. The "
         "deserialization logic is implemented directly in the Value class.";
}

bool MapType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                               const Type* other_type) const {
  ABSL_LOG(FATAL) << "Cannot compare " << DebugString() << " to "  // Crash OK
             << other_type->DebugString();
  return false;
}

std::string MapType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  if (!ThreadHasEnoughStack()) {
    return std::string(kFormatValueContentOutOfStackError);
  }

  const internal::ValueContentMap* value_content_map =
      value.GetAs<internal::ValueContentMapRef*>()->value();
  std::string result;

  switch (options.mode) {
    case Type::FormatValueContentOptions::Mode::kDebug:
      FormatValueContentDebugModeImpl(value_content_map, options, &result);
      return result;
    case Type::FormatValueContentOptions::Mode::kSQLLiteral:
    case Type::FormatValueContentOptions::Mode::kSQLExpression:
      FormatValueContentSqlModeImpl(value_content_map, options, &result);
      return result;
  }
}

void MapType::FormatValueContentDebugModeImpl(
    const internal::ValueContentMap* value_content_map,
    const FormatValueContentOptions& options, std::string* result) const {
  if (options.verbose) {
    absl::StrAppend(result, "Map");
  }

  absl::StrAppend(result, "{");
  absl::StrAppend(
      result,
      absl::StrJoin(*value_content_map, ", ",
                    [options, this](std::string* out, const auto& map_entry) {
                      auto& [key, value] = map_entry;
                      std::string key_str =
                          DebugFormatNullableValueContentForContainer(
                              key, this->key_type_, options);
                      std::string value_str =
                          DebugFormatNullableValueContentForContainer(
                              value, this->value_type_, options);
                      absl::StrAppend(out, key_str, ": ", value_str);
                    }));
  absl::StrAppend(result, "}");
}

void MapType::FormatValueContentSqlModeImpl(
    const internal::ValueContentMap* value_content_map,
    const FormatValueContentOptions& options, std::string* result) const {
  // TODO: b/413134417 - Use MAP literal syntax once it is implemented.
  absl::StrAppend(result, "MAP_FROM_ARRAY(");

  // Include explicit type for the ARRAY<STRUCT<K, V>> argument to
  // MAP_FROM_ARRAY if either of the following is true:
  // 1. We are generating a SQL expression, which means we should include
  //    explicit type information.
  // 2. We are generating SQL for an empty MAP. Explicit type is always required
  //    here, because there are no array entries to infer the type from.
  if (options.mode == Type::FormatValueContentOptions::Mode::kSQLExpression ||
      value_content_map->num_elements() == 0) {
    absl::StrAppend(
        result, "ARRAY<STRUCT<",
        key_type_->TypeName(options.product_mode, options.use_external_float32),
        ", ",
        value_type_->TypeName(options.product_mode,
                              options.use_external_float32),
        ">>");
  }

  absl::StrAppend(
      result, "[",
      absl::StrJoin(*value_content_map, ", ",
                    [options, this](std::string* out, const auto& map_entry) {
                      auto& [key, value] = map_entry;
                      std::string key_str = FormatNullableValueContent(
                          key, this->key_type_, options);
                      std::string value_str = FormatNullableValueContent(
                          value, this->value_type_, options);
                      absl::StrAppend(out, "(", key_str, ", ", value_str, ")");
                    }),
      "])");
}

const Type* GetMapKeyType(const Type* map_type) {
  ABSL_DCHECK(map_type->AsMap() != nullptr);
  return map_type->AsMap()->key_type();
}
const Type* GetMapValueType(const Type* map_type) {
  ABSL_DCHECK(map_type->AsMap() != nullptr);
  return map_type->AsMap()->value_type();
}

}  // namespace googlesql
