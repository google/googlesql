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

// This file implements inline methods of googlesql::Value. It is not intended
// to be read or included by users.



#ifndef GOOGLESQL_PUBLIC_VALUE_INL_H_
#define GOOGLESQL_PUBLIC_VALUE_INL_H_

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "googlesql/base/logging.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.h"  
#include "googlesql/public/value_content.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/map_view.h"
#include "googlesql/base/compact_reference_counted.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

inline uint64_t GetVectorValuesPhysicalByteSize(
    const std::vector<Value>& values) {
  uint64_t size = sizeof(std::vector<Value>);
  for (const Value& value : values) {
    size += value.physical_byte_size();
  }
  return size;
}

class Value::TypedList : public internal::ValueContentOrderedList {
 public:
  explicit TypedList(std::vector<Value>&& values)
      : values_(std::move(values)) {}
  TypedList(const TypedList&) = delete;
  TypedList& operator=(const TypedList&) = delete;
  ~TypedList() override;

  const std::vector<Value>& values() const { return values_; }

  uint64_t physical_byte_size() const override {
    uint64_t size = sizeof(TypedList);
    for (const Value& value : values_) {
      size += value.physical_byte_size();
    }
    return size;
  }

  internal::NullableValueContent element(int i) const override {
    if (values_.at(i).is_null()) {
      return internal::NullableValueContent();
    }
    return internal::NullableValueContent(values_.at(i).GetContent());
  }

  int64_t num_elements() const override { return values_.size(); }

 private:
  std::vector<Value> values_;
};

// Compares two `Value`s using `Value::LessThan()`. Note that this is not SQL
// inequality; see comments on `Value::LessThan()` for more details.
struct ValueComparator {
  bool operator()(const Value& a, const Value& b) const {
    return a.LessThan(b);
  }
};

using ValueMap = absl::btree_map<Value, Value, ValueComparator>;
using ValidPropertyNameToIndexMap =
    absl::btree_map<std::string, int, googlesql_base::CaseLess>;

class Value::TypedMap : public internal::ValueContentMap {
 public:
  explicit TypedMap(const std::vector<std::pair<Value, Value>>& values);
  explicit TypedMap(googlesql_base::MapView<Value, Value> values);
  TypedMap(const TypedMap&) = delete;
  TypedMap& operator=(const TypedMap&) = delete;
  ~TypedMap() override;

  const ValueMap& entries() const;
  uint64_t physical_byte_size() const override;
  int64_t num_elements() const override;
  std::optional<internal::NullableValueContent> GetContentMapValueByKey(
      const internal::NullableValueContent& search_key, const Type* key_type,
      const ValueEqualityCheckOptions& options) const override;

 private:
  class Iterator;

  std::unique_ptr<internal::ValueContentMap::IteratorImpl> begin_internal()
      const override;
  std::unique_ptr<internal::ValueContentMap::IteratorImpl> end_internal()
      const override;

  ValueMap map_;
};

class Value::TypedMeasure : public internal::ValueContentMeasure {
 public:
  TypedMeasure(const TypedMeasure&) = delete;
  TypedMeasure& operator=(const TypedMeasure&) = delete;

  static absl::StatusOr<std::unique_ptr<TypedMeasure>> Create(
      Value captured_values_as_struct, std::vector<int> key_indices,
      const LanguageOptions& language_options);

  uint64_t physical_byte_size() const override;

  const Value& GetCapturedValuesAsStructValue() const {
    return captured_values_as_struct_;
  }

  const internal::ValueContentOrderedList* GetCapturedValues() const override;

  const StructType* GetCapturedValuesStructType() const override;

  const std::vector<int>& KeyIndices() const override;

 private:
  TypedMeasure(Value captured_values_as_struct, std::vector<int> key_indices);

  Value captured_values_as_struct_;
  std::vector<int> key_indices_;
};

class Value::GraphElementValue final
    : public googlesql_base::refcount::CompactReferenceCounted<Value::GraphElementValue>,
      public internal::GraphElementContainer {
 public:
  // TODO: b/386262274 - Consolidate graph element value constructors to take
  // structs instead of loose fields, once Spandex side doesn't use this
  // interface any more.
  // Node constructor.
  GraphElementValue(const GraphElementType* type, std::string identifier,
                    std::vector<Value> properties,
                    ValidPropertyNameToIndexMap property_name_to_index,
                    std::vector<std::string> labels,
                    std::string element_table_name,
                    std::vector<Value> keys
                    )
      : type_(type),
        identifier_(std::move(identifier)),
        properties_(std::move(properties)),
        valid_property_name_to_index_(std::move(property_name_to_index)),
        labels_(std::move(labels)),
        element_table_name_(std::move(element_table_name)),
        node_keys_(std::move(keys)) {
    ABSL_DCHECK(IsNode()) << "Not a node";
  }

  // Edge constructor.
  GraphElementValue(const GraphElementType* type, std::string identifier,
                    std::vector<Value> properties,
                    ValidPropertyNameToIndexMap property_name_to_index,
                    std::vector<std::string> labels,
                    std::string element_table_name,
                    std::string source_node_identifier,
                    std::string dest_node_identifier,
                    std::vector<Value> source_keys,
                    std::vector<Value> dest_keys,
                    std::string source_node_element_table_name,
                    std::string dest_node_element_table_name
                    )
      : type_(type),
        identifier_(std::move(identifier)),
        properties_(std::move(properties)),
        valid_property_name_to_index_(std::move(property_name_to_index)),
        labels_(std::move(labels)),
        element_table_name_(std::move(element_table_name)),
        source_node_identifier_(std::move(source_node_identifier)),
        dest_node_identifier_(std::move(dest_node_identifier)),
        source_keys_(std::move(source_keys)),
        dest_keys_(std::move(dest_keys)),
        source_node_element_table_name_(
            std::move(source_node_element_table_name)),
        dest_node_element_table_name_(std::move(dest_node_element_table_name)) {
    ABSL_DCHECK(IsEdge()) << "Not an edge";
  }

  GraphElementValue(const GraphElementValue&) = delete;
  GraphElementValue& operator=(const GraphElementValue&) = delete;

  GraphElementType::ElementKind element_kind() const {
    return type_->element_kind();
  }
  bool IsNode() const { return type_->IsNode(); }
  bool IsEdge() const { return type_->IsEdge(); }

  // Returns the value of the property that is valid in the element, with the
  // given `name`; otherwise, returns an error.
  absl::StatusOr<Value> FindValidPropertyValueByName(
      const std::string& name) const {
    auto it = valid_property_name_to_index_.find(name);
    if (it != valid_property_name_to_index_.end()) {
      return properties_.values().at(it->second);
    }
    return absl::NotFoundError(absl::StrCat("No such property: ", name));
  }

  // Returns the value of the property that is part of the union-ed graph
  // element type, with the given `name`; otherwise, returns an error.
  absl::StatusOr<Value> FindPropertyByName(const std::string& name) const {
    int index;
    if (type_->HasField(name, &index) == Type::HAS_FIELD) {
      Value property_value = properties_.values().at(index);
      if (property_value.is_valid()) {
        return property_value;
      } else {
        return Value::Null(type_->property_types().at(index).value_type);
      }
    }
    if (type_->is_dynamic()) {
      auto it = valid_property_name_to_index_.find(name);
      if (it != valid_property_name_to_index_.end()) {
        return properties_.values().at(it->second);
      } else {
        return Value::NullJson();
      }
    }
    return absl::NotFoundError(absl::StrCat("No such property: ", name));
  }

  // Returns the value of the static property with given name; returns an error
  // status if no property with such name found.
  absl::StatusOr<Value> FindStaticPropertyByName(
      const std::string& name) const {
    auto it = valid_property_name_to_index_.find(name);
    // A static property must have an index in `property_name_to_index_` that is
    // less than the number of static properties recorded in the
    // GraphElementType.
    if (it != valid_property_name_to_index_.end() &&
        it->second < type_->property_types().size()) {
      Value property_value = properties_.values().at(it->second);
      if (property_value.is_valid()) {
        return property_value;
      }
    }
    return absl::NotFoundError(absl::StrCat("No such static property: ", name));
  }

  // Returns an ordered list of property names with valid values.
  std::vector<std::string> property_names() const {
    std::vector<std::string> names;
    googlesql_base::AppendKeysFromMap(valid_property_name_to_index_, &names);
    return names;
  }

  // Returns all property values.
  // This includes:
  // - all static properties
  // - dynamic properties with valid values
  absl::Span<const Value> property_values() const {
    return properties_.values();
  }

  // ValueContentOrderedList implementation:
  internal::NullableValueContent element(int i) const override {
    if (!properties_.values().at(i).is_valid()) {
      return internal::NullableValueContent();
    }
    return properties_.element(i);
  }

  int64_t num_elements() const override { return properties_.num_elements(); }

  uint64_t physical_byte_size() const override {
    uint64_t keys_size = 0;
    if (IsNode()) {
      keys_size = GetVectorValuesPhysicalByteSize(node_keys_);
    } else {
      keys_size = GetVectorValuesPhysicalByteSize(source_keys_) +
                  GetVectorValuesPhysicalByteSize(dest_keys_);
    }

    uint64_t element_table_names_size = 0;
    if (IsEdge()) {
      element_table_names_size = source_node_element_table_name_.length() +
                                 dest_node_element_table_name_.length();
    }

    return absl::c_accumulate(labels_,
                              sizeof(GraphElementValue) + identifier_.length() +
                                  element_table_name_.length() +
                                  source_node_identifier_.length() +
                                  dest_node_identifier_.length() +
                                  properties_.physical_byte_size() + keys_size +
                                  element_table_names_size
                              ,
                              [](uint64_t size, absl::string_view label) {
                                return size + label.length();
                              });
  }

  // GraphElementContainer implementation:
  absl::string_view GetIdentifier() const override { return identifier_; }

  absl::Span<const std::string> GetLabels() const override { return labels_; }

  absl::string_view GetDefinitionName() const override {
    return element_table_name_;
  }

  const ValidPropertyNameToIndexMap& GetValidPropertyNameToIndexMap()
      const override {
    return valid_property_name_to_index_;
  }

  // REQUIRES: IsEdge()
  absl::string_view GetSourceNodeIdentifier() const override {
    ABSL_DCHECK(IsEdge()) << "Not an edge";  // Crash OK
    return source_node_identifier_;
  }

  // REQUIRES: IsEdge()
  absl::string_view GetDestNodeIdentifier() const override {
    ABSL_DCHECK(IsEdge()) << "Not an edge";  // Crash OK
    return dest_node_identifier_;
  }

  absl::Span<const Value> GetNodeKeys() const {
    ABSL_DCHECK(IsNode());
    return node_keys_;
  }

  absl::Span<const Value> GetSourceKeys() const {
    ABSL_DCHECK(IsEdge());
    return source_keys_;
  }

  absl::Span<const Value> GetDestKeys() const {
    ABSL_DCHECK(IsEdge());
    return dest_keys_;
  }

  absl::string_view GetSourceNodeElementTableName() const {
    ABSL_DCHECK(IsEdge()) << "Not an edge";
    return source_node_element_table_name_;
  }

  absl::string_view GetDestNodeElementTableName() const {
    ABSL_DCHECK(IsEdge()) << "Not an edge";
    return dest_node_element_table_name_;
  }

 private:
  const GraphElementType* type_;
  const std::string identifier_;
  const Value::TypedList properties_;
  // Maps property names to their indices in `properties_`.
  const ValidPropertyNameToIndexMap valid_property_name_to_index_;
  const std::vector<std::string> labels_;
  const std::string element_table_name_;
  const std::vector<Value> node_keys_;  // Only used for nodes.

  const std::string source_node_identifier_;
  const std::string dest_node_identifier_;
  const std::vector<Value> source_keys_;              // Only used for edges.
  const std::vector<Value> dest_keys_;                // Only used for edges.
  const std::string source_node_element_table_name_;  // Only used for edges.
  const std::string dest_node_element_table_name_;    // Only used for edges.
};

class Value::GraphPathValue final
    : public googlesql_base::refcount::CompactReferenceCounted<Value::GraphPathValue>,
      public internal::ValueContentOrderedList {
 public:
  explicit GraphPathValue(std::vector<Value> graph_elements
                          )
      : graph_elements_(std::move(graph_elements))
  {}

  GraphPathValue(const GraphPathValue&) = delete;
  GraphPathValue& operator=(const GraphPathValue&) = delete;

  const Value& graph_element(int i) const {
    ABSL_DCHECK(graph_elements_.values().at(i).is_valid()) << i;
    ABSL_DCHECK(!graph_elements_.values().at(i).is_null()) << i;
    ABSL_DCHECK(graph_elements_.values().at(i).type()->IsGraphElement());
    if (i % 2 == 0) {
      ABSL_DCHECK(graph_elements_.values().at(i).type()->AsGraphElement()->IsNode());
    } else {
      ABSL_DCHECK(graph_elements_.values().at(i).type()->AsGraphElement()->IsEdge());
    }
    return graph_elements_.values().at(i);
  }

  absl::Span<const Value> graph_elements() const {
    return graph_elements_.values();
  }

  internal::NullableValueContent element(int i) const override {
    return internal::NullableValueContent(graph_element(i).GetContent());
  }

  int64_t num_elements() const override {
    return graph_elements_.num_elements();
  }

  uint64_t physical_byte_size() const override {
    return sizeof(GraphPathValue) +
           graph_elements_.physical_byte_size();
  }

 private:
  const Value::TypedList graph_elements_;
};

// -------------------------------------------------------
// Value
// -------------------------------------------------------

// Invalid value.
#ifndef SWIG
constexpr
#else
inline
#endif
    Value::Value() = default;

inline Value::Value(const Value& that) { CopyFrom(that); }

inline void Value::Clear() {
  if (!is_valid()) return;

  if (!metadata_.has_type_pointer()) {
    // For simple types, we just need to clear the content and set metadata_
    // to invalid. Doing this via direct dispatch to SimpleType avoids the cost
    // of figuring out a type pointer from the type_kind and the cost of
    // dispatching virtually through that type pointer to SimpleType. This
    // significantly increases the speed of this function in the common
    // SimpleType case.
    if (!metadata_.is_null()) {
      SimpleType::ClearValueContent(metadata_.type_kind(), GetContent());
    }
  } else {
    if (!metadata_.is_null()) {
      metadata_.type()->ClearValueContent(GetContent());
    }
    internal::TypeStoreHelper::UnrefFromValue(metadata_.type()->type_store_);
  }

  metadata_ = Metadata::Invalid();
}

inline const Value& Value::operator=(const Value& that) {
  // Self-copying must not clear the contents of the value.
  if (this == &that) {
    return *this;
  }
  Clear();
  CopyFrom(that);
  return *this;
}

// GCC does not like our use of memcpy here. So we disable its warnings.
#if defined(__GNUC__) && (((__GNUC__ * 100) + __GNUC_MINOR__) >= 800)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
inline Value::Value(Value&& that) noexcept {  // NOLINT(build/c++11)
  memcpy(static_cast<void*>(this), static_cast<const void*>(&that),
         sizeof(Value));
  // Invalidate 'that' to disable its destructor.
  that.metadata_ = Metadata::Invalid();
}

// Self-move of an rvalue reference leaves 'that' and consequently 'this' in an
// unspecified state. Hence, we don't need to check for 'this == &that' and
// simply set 'that' to an invalid value. See
// http://en.cppreference.com/w/cpp/utility/move for more context.
inline Value& Value::operator=(Value&& that) noexcept {  // NOLINT(build/c++11)
  // Clear "this" to destroy all pointers.
  Clear();
  memcpy(static_cast<void*>(this), static_cast<const void*>(&that),
         sizeof(Value));
  // Invalidate 'that' to disable its destructor.
  that.metadata_ = Metadata::Invalid();
  return *this;
}

#if defined(__GNUC__) && (((__GNUC__ * 100) + __GNUC_MINOR__) >= 800)
#pragma GCC diagnostic pop
#endif

inline Value::Value(int32_t value)
    : metadata_(TypeKind::TYPE_INT32), int32_value_(value) {}

inline Value::Value(int64_t value)
    : metadata_(TypeKind::TYPE_INT64), int64_value_(value) {}

inline Value::Value(uint32_t value)
    : metadata_(TypeKind::TYPE_UINT32), uint32_value_(value) {}

inline Value::Value(uint64_t value)
    : metadata_(TypeKind::TYPE_UINT64), uint64_value_(value) {}

inline Value::Value(bool value)
    : metadata_(TypeKind::TYPE_BOOL), bool_value_(value) {}

inline Value::Value(float value)
    : metadata_(TypeKind::TYPE_FLOAT), float_value_(value) {}

inline Value::Value(double value)
    : metadata_(TypeKind::TYPE_DOUBLE), double_value_(value) {}

inline Value::Value(TypeKind type_kind, std::string value)
    : metadata_(type_kind),
      string_ptr_(new internal::StringRef(std::move(value))) {
  ABSL_CHECK(type_kind == TYPE_STRING || type_kind == TYPE_BYTES);  // Crash OK
}

inline Value::Value(const NumericValue& numeric)
    : metadata_(TypeKind::TYPE_NUMERIC),
      numeric_ptr_(new internal::NumericRef(numeric)) {}

inline Value::Value(const BigNumericValue& bignumeric)
    : metadata_(TypeKind::TYPE_BIGNUMERIC),
      bignumeric_ptr_(new internal::BigNumericRef(bignumeric)) {}

inline Value::Value(internal::JSONRef* json_ptr)
    : metadata_(TypeKind::TYPE_JSON), json_ptr_(json_ptr) {
  ABSL_CHECK(json_ptr != nullptr);
}

inline Value::Value(const IntervalValue& interval)
    : metadata_(TypeKind::TYPE_INTERVAL),
      interval_ptr_(new internal::IntervalRef(interval)) {}

inline Value::Value(tokens::TokenList tokenlist)
    : metadata_(TypeKind::TYPE_TOKENLIST),
      tokenlist_ptr_(new internal::TokenListRef(std::move(tokenlist))) {}

inline Value::Value(const UuidValue& uuid)
    : metadata_(TypeKind::TYPE_UUID), uuid_ptr_(new internal::UuidRef(uuid)) {}

inline absl::StatusOr<Value> Value::MakeStruct(const StructType* type,
                                               std::vector<Value>&& values) {
  return MakeStructInternal(/*already_validated=*/false, type,
                            std::vector<Value>(std::move(values)));
}
inline absl::StatusOr<Value> Value::MakeStruct(const StructType* type,
                                               absl::Span<const Value> values) {
  return MakeStructInternal(/*already_validated=*/false, type,
                            std::vector<Value>(values.begin(), values.end()));
}
inline absl::StatusOr<Value> Value::MakeStruct(
    const StructType* type, std::initializer_list<Value> values) {
  // This variant is required to disambiguate vector&& and span
  return MakeStructInternal(/*already_validated=*/false, type,
                            std::vector<Value>(values.begin(), values.end()));
}
inline absl::StatusOr<Value> Value::MakeStructFromValidatedInputs(
    const StructType* type, std::vector<Value>&& values) {
  return MakeStructInternal(/*already_validated=*/true, type,
                            std::move(values));
}

inline absl::StatusOr<Value> Value::MakeRange(const Value& start,
                                              const Value& end) {
  return MakeRangeInternal(/*is_validated=*/false, start, end);
}

inline absl::StatusOr<Value> Value::MakeRangeFromValidatedInputs(
    const RangeType* range_type, const Value& start, const Value& end) {
  return MakeRangeInternal(/*is_validated=*/true, start, end, range_type);
}

inline absl::StatusOr<Value> Value::MakeArray(const ArrayType* array_type,
                                              absl::Span<const Value> values) {
  return MakeArrayInternal(/*already_validated=*/false, array_type,
                           kPreservesOrder,
                           std::vector<Value>(values.begin(), values.end()));
}

inline absl::StatusOr<Value> Value::MakeArray(const ArrayType* array_type,
                                              std::vector<Value>&& values) {
  return MakeArrayInternal(/*already_validated=*/false, array_type,
                           kPreservesOrder, std::move(values));
}

inline absl::StatusOr<Value> Value::MakeArray(
    const ArrayType* array_type, std::initializer_list<Value> values) {
  return MakeArrayInternal(/*already_validated=*/false, array_type,
                           kPreservesOrder,
                           std::vector<Value>(values.begin(), values.end()));
}

inline absl::StatusOr<Value> Value::MakeArrayFromValidatedInputs(
    const ArrayType* array_type, std::vector<Value>&& values) {
  return MakeArrayInternal(/*already_validated=*/true, array_type,
                           kPreservesOrder, std::move(values));
}

inline Value Value::EmptyArray(const ArrayType* array_type) {
  // Should not be possible for this to fail.
  return *MakeArrayFromValidatedInputs(array_type, std::vector<Value>{});
}

inline absl::StatusOr<Value> Value::MakeGraphNode(
    const GraphElementType* graph_element_type,
    const GraphElementLabelsAndProperties& labels_and_properties,
    const GraphNodeKeysAndElementTableNames&
        node_keys_and_element_table_names) {
  return MakeGraphElement(graph_element_type, labels_and_properties,
                          node_keys_and_element_table_names,
                          /*edge_data=*/
                          {}
  );
}

inline absl::StatusOr<Value> Value::MakeGraphNode(
    const GraphElementType* graph_element_type, absl::string_view identifier,
    const GraphElementLabelsAndProperties& labels_and_properties,
    absl::string_view element_table_name) {
  return MakeGraphElement(graph_element_type, labels_and_properties,
                          /*node_data=*/
                          {.identifier = identifier,
                           .keys = {},
                           .element_table_name = element_table_name},
                          /*edge_data=*/
                          {}
  );
}

inline absl::StatusOr<Value> Value::MakeGraphEdge(
    const GraphElementType* graph_element_type,
    const Value::GraphElementLabelsAndProperties& labels_and_properties,
    const GraphEdgeKeysAndElementTableNames&
        edge_keys_and_element_table_names) {
  return MakeGraphElement(graph_element_type, labels_and_properties,
                          /*node_data=*/{},
                          edge_keys_and_element_table_names
  );
}

inline absl::StatusOr<Value> Value::MakeGraphEdge(
    const GraphElementType* graph_element_type, absl::string_view identifier,
    const Value::GraphElementLabelsAndProperties& labels_and_properties,
    absl::string_view element_table_name,
    absl::string_view source_node_identifier,
    absl::string_view dest_node_identifier) {
  return MakeGraphElement(graph_element_type, labels_and_properties,
                          /*node_data=*/{},
                          /*edge_data=*/
                          {.identifier = identifier,
                           .element_table_name = element_table_name,
                           .source = {.identifier = source_node_identifier,
                                      .keys = {},
                                      .element_table_name = ""},
                           .dest = {.identifier = dest_node_identifier,
                                    .keys = {},
                                    .element_table_name = ""}}
  );
}

inline absl::StatusOr<Value> Value::MakeMap(
    const Type* map_type,
    absl::Span<const std::pair<const Value, const Value>> map_entries) {
  return MakeMapInternal(map_type, std::vector<std::pair<Value, Value>>(
                                       map_entries.begin(), map_entries.end()));
}
inline absl::StatusOr<Value> Value::MakeMap(
    const Type* map_type, std::vector<std::pair<Value, Value>>&& map_entries) {
  return MakeMapInternal(map_type, map_entries);
}
inline absl::StatusOr<Value> Value::MakeMap(
    const Type* map_type,
    std::initializer_list<std::pair<Value, Value>> map_entries) {
  return MakeMapInternal(map_type, std::vector<std::pair<Value, Value>>(
                                       map_entries.begin(), map_entries.end()));
}
inline absl::StatusOr<Value> Value::MakeMap(
    const Type* map_type, googlesql_base::MapView<Value, Value> source_map) {
  return MakeMapInternal(map_type, source_map);
}

inline Value Value::Int32(int32_t v) { return Value(v); }
inline Value Value::Int64(int64_t v) { return Value(v); }
inline Value Value::Uint32(uint32_t v) { return Value(v); }
inline Value Value::Uint64(uint64_t v) { return Value(v); }
inline Value Value::Bool(bool v) { return Value(v); }
inline Value Value::Float(float v) { return Value(v); }
inline Value Value::Double(double v) { return Value(v); }

inline Value Value::StringValue(std::string v) {
  return Value(TYPE_STRING, std::move(v));
}
inline Value Value::String(absl::string_view v) {
  return Value(TYPE_STRING, std::string(v));
}
inline Value Value::String(const absl::Cord& v) {
  return Value(TYPE_STRING, std::string(v));
}

template <size_t N>
inline Value Value::String(const char (&str)[N]) {
  return Value::String(std::string(str, N - 1));
}

inline Value Value::Bytes(std::string v) {
  return Value(TYPE_BYTES, std::move(v));
}
inline Value Value::Bytes(absl::string_view v) {
  return Value(TYPE_BYTES, std::string(v));
}
inline Value Value::Bytes(const absl::Cord& v) {
  return Value(TYPE_BYTES, std::string(v));
}
template <size_t N>
inline Value Value::Bytes(const char (&str)[N]) {
  return Value::Bytes(std::string(str, N - 1));
}

inline Value Value::Date(int32_t v) { return Value(TYPE_DATE, v); }
inline Value Value::Timestamp(absl::Time t) { return Value(t); }
inline Value Value::Timestamp(TimestampPicosValue t) {
  return Value(t, TypeKind::TYPE_TIMESTAMP);
}
inline Value Value::Time(TimeValue time) { return Value(time); }
inline Value Value::Datetime(DatetimeValue datetime) { return Value(datetime); }
inline Value Value::Interval(IntervalValue interval) { return Value(interval); }
inline Value Value::Numeric(NumericValue v) { return Value(v); }
inline Value Value::BigNumeric(BigNumericValue v) { return Value(v); }
inline Value Value::UnvalidatedJsonString(std::string v) {
  return Value(new internal::JSONRef(std::move(v)));
}
inline Value Value::Json(JSONValue v) {
  return Value(new internal::JSONRef(std::move(v)));
}
inline Value Value::TokenList(tokens::TokenList value) {
  return Value(std::move(value));
}

inline Value Value::Enum(const EnumType* type, int64_t value,
                         bool allow_unknown_enum_values) {
  return Value(type, value, allow_unknown_enum_values);
}
inline Value Value::Enum(const EnumType* type, absl::string_view name) {
  return Value(type, name);
}
inline Value Value::Proto(const ProtoType* type, absl::Cord value) {
  return Value(type, std::move(value));
}
inline Value Value::Extended(const ExtendedType* type,
                             const ValueContent& value) {
  return Value(type, value);
}
inline Value Value::Uuid(UuidValue v) { return Value(v); }

inline Value Value::NullInt32() { return Value(TypeKind::TYPE_INT32); }
inline Value Value::NullInt64() { return Value(TypeKind::TYPE_INT64); }
inline Value Value::NullUint32() { return Value(TypeKind::TYPE_UINT32); }
inline Value Value::NullUint64() { return Value(TypeKind::TYPE_UINT64); }
inline Value Value::NullBool() { return Value(TypeKind::TYPE_BOOL); }
inline Value Value::NullFloat() { return Value(TypeKind::TYPE_FLOAT); }
inline Value Value::NullDouble() { return Value(TypeKind::TYPE_DOUBLE); }
inline Value Value::NullString() { return Value(TypeKind::TYPE_STRING); }
inline Value Value::NullBytes() { return Value(TypeKind::TYPE_BYTES); }
inline Value Value::NullDate() { return Value(TypeKind::TYPE_DATE); }
inline Value Value::NullTimestamp() { return Value(TypeKind::TYPE_TIMESTAMP); }
inline Value Value::NullTime() { return Value(TypeKind::TYPE_TIME); }
inline Value Value::NullDatetime() { return Value(TypeKind::TYPE_DATETIME); }
inline Value Value::NullInterval() { return Value(TypeKind::TYPE_INTERVAL); }
inline Value Value::NullGeography() { return Value(TypeKind::TYPE_GEOGRAPHY); }
inline Value Value::NullNumeric() { return Value(TypeKind::TYPE_NUMERIC); }
inline Value Value::NullBigNumeric() {
  return Value(TypeKind::TYPE_BIGNUMERIC);
}
inline Value Value::NullJson() { return Value(TypeKind::TYPE_JSON); }
inline Value Value::NullTokenList() { return Value(types::TokenListType()); }
inline Value Value::NullUuid() { return Value(TypeKind::TYPE_UUID); }
inline Value Value::EmptyGeography() {
  ABSL_CHECK(false);
  return NullGeography();
}
inline Value Value::UnboundedStartDate() { return NullDate(); }
inline Value Value::UnboundedEndDate() { return NullDate(); }
inline Value Value::UnboundedStartDatetime() { return NullDatetime(); }
inline Value Value::UnboundedEndDatetime() { return NullDatetime(); }
inline Value Value::UnboundedStartTimestamp() { return NullTimestamp(); }
inline Value Value::UnboundedEndTimestamp() { return NullTimestamp(); }

inline Value Value::Null(const Type* type) { return Value(type); }

inline Value::~Value() { Clear(); }

inline TypeKind Value::type_kind() const {
  ABSL_CHECK(is_valid()) << DebugString();
  return metadata_.type_kind();
}

inline bool Value::is_null() const {
  ABSL_CHECK(is_valid()) << DebugString();
  return metadata_.is_null();
}

inline bool Value::is_empty_array() const {
  ABSL_CHECK(is_valid()) << DebugString();
  return type()->IsArray() && !is_null() && empty();
}

inline bool Value::order_kind() const {
  ABSL_CHECK_EQ(TYPE_ARRAY, metadata_.type_kind());
  return metadata_.preserves_order();
}

inline bool Value::is_valid() const {
  static_assert(TYPE_UNKNOWN == 0 && kInvalidTypeKind == -1,
                "Revisit implementation");
  // This check assumes that valid TypeKind values are positive.
  return static_cast<int32_t>(metadata_.type_kind()) > 0;
}

inline bool Value::has_content() const {
  return is_valid() && !metadata_.is_null();
}

inline int32_t Value::int32_value() const {
  ABSL_CHECK_EQ(TYPE_INT32, metadata_.type_kind()) << "Not an int32 value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return int32_value_;
}

inline int64_t Value::int64_value() const {
  ABSL_CHECK_EQ(TYPE_INT64, metadata_.type_kind()) << "Not an int64 value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return int64_value_;
}

inline uint32_t Value::uint32_value() const {
  ABSL_CHECK_EQ(TYPE_UINT32, metadata_.type_kind()) << "Not a uint32 value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return uint32_value_;
}

inline uint64_t Value::uint64_value() const {
  ABSL_CHECK_EQ(TYPE_UINT64, metadata_.type_kind()) << "Not a uint64 value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return uint64_value_;
}

inline bool Value::bool_value() const {
  ABSL_CHECK_EQ(TYPE_BOOL, metadata_.type_kind()) << "Not a bool value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return bool_value_;
}

inline float Value::float_value() const {
  ABSL_CHECK_EQ(TYPE_FLOAT, metadata_.type_kind()) << "Not a float value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return float_value_;
}

inline double Value::double_value() const {
  ABSL_CHECK_EQ(TYPE_DOUBLE, metadata_.type_kind()) << "Not a double value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return double_value_;
}

inline const std::string& Value::string_value() const {
  ABSL_CHECK_EQ(TYPE_STRING, metadata_.type_kind()) << "Not a string value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return string_ptr_->value();
}

inline const std::string& Value::bytes_value() const {
  ABSL_CHECK_EQ(TYPE_BYTES, metadata_.type_kind()) << "Not a bytes value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return string_ptr_->value();
}

inline int32_t Value::date_value() const {
  ABSL_CHECK_EQ(TYPE_DATE, metadata_.type_kind()) << "Not a date value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return int32_value_;
}

inline int32_t Value::enum_value() const {
  ABSL_CHECK_EQ(TYPE_ENUM, metadata_.type_kind()) << "Not an enum value";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return enum_value_;
}

inline const absl::Cord& Value::proto_value() const {
  ABSL_DCHECK_EQ(TYPE_PROTO, metadata_.type_kind()) << "Not a proto value";
  ABSL_DCHECK(!metadata_.is_null()) << "Null value";
  return proto_ptr_->value();
}

inline TimeValue Value::time_value() const {
  return TimeValue::FromPacked32SecondsAndNanos(bit_field_32_value_,
                                                subsecond_nanos());
}

inline DatetimeValue Value::datetime_value() const {
  return DatetimeValue::FromPacked64SecondsAndNanos(bit_field_64_value_,
                                                    subsecond_nanos());
}

inline const IntervalValue& Value::interval_value() const {
  ABSL_CHECK_EQ(TYPE_INTERVAL, metadata_.type_kind()) << "Not an interval type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return interval_ptr_->value();
}

inline const NumericValue& Value::numeric_value() const {
  ABSL_CHECK_EQ(TYPE_NUMERIC, metadata_.type_kind()) << "Not a numeric type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return numeric_ptr_->value();
}

inline const BigNumericValue& Value::bignumeric_value() const {
  ABSL_CHECK_EQ(TYPE_BIGNUMERIC, metadata_.type_kind()) << "Not a bignumeric type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return bignumeric_ptr_->value();
}

inline bool Value::is_validated_json() const {
  return metadata_.type_kind() == TYPE_JSON && !metadata_.is_null() &&
         json_ptr_->unparsed_string() == nullptr;
}

inline bool Value::is_unparsed_json() const {
  return metadata_.type_kind() == TYPE_JSON && !metadata_.is_null() &&
         json_ptr_->unparsed_string() != nullptr;
}

inline const std::string& Value::json_value_unparsed() const {
  ABSL_CHECK_EQ(TYPE_JSON, metadata_.type_kind()) << "Not a json type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  ABSL_CHECK(is_unparsed_json()) << "Not an unparsed json value";
  return *json_ptr_->unparsed_string();
}

inline JSONValueConstRef Value::json_value() const {
  ABSL_CHECK_EQ(TYPE_JSON, metadata_.type_kind()) << "Not a json type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  ABSL_CHECK(is_validated_json()) << "Non a validated json value";
  return json_ptr_->document().value();
}

inline std::string Value::json_string() const {
  ABSL_CHECK_EQ(TYPE_JSON, metadata_.type_kind()) << "Not a json type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";

  if (json_ptr_->unparsed_string() == nullptr) {
    return json_ptr_->document().value().ToString();
  }

  return *json_ptr_->unparsed_string();
}

inline const tokens::TokenList& Value::tokenlist_value() const {
  ABSL_CHECK_EQ(TYPE_TOKENLIST, metadata_.type_kind()) << "Not a tokenlist type";
  ABSL_CHECK(!metadata_.is_null()) << "Null value";
  return tokenlist_ptr_->value();
}

inline bool Value::empty() const { return elements().empty(); }

inline int Value::num_elements() const {
  if (type()->IsMap()) {
    return static_cast<int>(map_entries().size());
  }
  return static_cast<int>(elements().size());
}

inline int Value::num_fields() const { return fields().size(); }

inline const Value& Value::field(int i) const { return fields()[i]; }

inline const Value& Value::element(int i) const {
  ABSL_CHECK(type()->IsArray());
  return elements()[i];
}

inline bool Value::Equals(const Value& that) const {
  return EqualsInternal(*this, that, /*allow_bags=*/false, /*options=*/{});
}

inline const Value& Value::start() const {
  ABSL_CHECK_EQ(TYPE_RANGE, metadata_.type_kind())  // Crash ok
      << "Not a range value";
  ABSL_CHECK(!is_null()) << "Null value";  // Crash ok
  ABSL_CHECK(type()->IsRange());           // Crash ok
  const internal::ValueContentOrderedList* const container_ptr =
      container_ptr_->value();
  const TypedList* const list_ptr =
      static_cast<const TypedList* const>(container_ptr);
  ABSL_CHECK_EQ(list_ptr->values().size(), 2);  // Crash ok
  return list_ptr->values().at(0);
}

inline const Value& Value::end() const {
  ABSL_CHECK_EQ(TYPE_RANGE, metadata_.type_kind())  // Crash ok
      << "Not a range value";
  ABSL_CHECK(!is_null()) << "Null value";  // Crash ok
  ABSL_CHECK(type()->IsRange());           // Crash ok
  const internal::ValueContentOrderedList* const container_ptr =
      container_ptr_->value();
  const TypedList* const list_ptr =
      static_cast<const TypedList* const>(container_ptr);
  ABSL_CHECK_EQ(list_ptr->values().size(), 2);  // Crash ok
  return list_ptr->values().at(1);
}

inline const Value::GraphElementValue* Value::graph_element_value() const {
  ABSL_CHECK_EQ(metadata_.type_kind(), TYPE_GRAPH_ELEMENT)  // Crash ok
      << "Not a graph element value";
  ABSL_CHECK(!is_null()) << "Null value";  // Crash ok
  return static_cast<const Value::GraphElementValue*>(container_ptr_->value());
}

inline bool Value::IsNode() const { return graph_element_value()->IsNode(); }

inline bool Value::IsEdge() const { return graph_element_value()->IsEdge(); }

inline absl::string_view Value::GetIdentifier() const {
  return graph_element_value()->GetIdentifier();
}

inline absl::Span<const std::string> Value::GetLabels() const {
  return graph_element_value()->GetLabels();
}

inline absl::string_view Value::GetDefinitionName() const {
  return graph_element_value()->GetDefinitionName();
}

inline absl::string_view Value::GetSourceNodeIdentifier() const {
  return graph_element_value()->GetSourceNodeIdentifier();
}

inline absl::Span<const Value> Value::GetGraphNodeKeys() const {
  return graph_element_value()->GetNodeKeys();
}

inline absl::Span<const Value> Value::GetGraphEdgeSourceKeys() const {
  return graph_element_value()->GetSourceKeys();
}

inline absl::Span<const Value> Value::GetGraphEdgeDestKeys() const {
  return graph_element_value()->GetDestKeys();
}

inline std::vector<Value> Value::GetGraphEdgeKeys() const {
  ABSL_DCHECK(IsEdge());
  absl::Span<const Value> source_keys = GetGraphEdgeSourceKeys();
  absl::Span<const Value> dest_keys = GetGraphEdgeDestKeys();
  std::vector<Value> edge_keys;
  edge_keys.reserve(source_keys.size() + dest_keys.size());
  edge_keys.insert(edge_keys.end(), source_keys.begin(), source_keys.end());
  edge_keys.insert(edge_keys.end(), dest_keys.begin(), dest_keys.end());
  return edge_keys;
}

inline absl::string_view Value::GetSourceNodeElementTableName() const {
  return graph_element_value()->GetSourceNodeElementTableName();
}

inline absl::string_view Value::GetDestNodeElementTableName() const {
  return graph_element_value()->GetDestNodeElementTableName();
}

inline absl::string_view Value::GetDestNodeIdentifier() const {
  return graph_element_value()->GetDestNodeIdentifier();
}

inline std::vector<std::string> Value::property_names() const {
  return graph_element_value()->property_names();
}

inline absl::Span<const Value> Value::property_values() const {
  return graph_element_value()->property_values();
}

inline absl::StatusOr<Value> Value::FindValidPropertyValueByName(
    const std::string& name) const {
  return graph_element_value()->FindValidPropertyValueByName(name);
}

inline absl::StatusOr<Value> Value::FindPropertyByName(
    const std::string& name) const {
  return graph_element_value()->FindPropertyByName(name);
}

inline absl::StatusOr<Value> Value::FindStaticPropertyByName(
    const std::string& name) const {
  return graph_element_value()->FindStaticPropertyByName(name);
}

inline const Value::GraphPathValue* Value::graph_path_value() const {
  ABSL_CHECK_EQ(metadata_.type_kind(), TYPE_GRAPH_PATH)  // Crash ok
      << "Not a graph path value";
  ABSL_CHECK(!is_null()) << "Null value";  // Crash ok
  return static_cast<const Value::GraphPathValue*>(container_ptr_->value());
}

inline int Value::num_graph_elements() const {
  return static_cast<int>(graph_path_value()->num_elements());
}

inline const Value& Value::graph_element(int i) const {
  return graph_path_value()->graph_element(i);
}

inline absl::Span<const Value> Value::graph_elements() const {
  return graph_path_value()->graph_elements();
}

template <typename H>
H AbslHashValue(H h, const Value& v) {
  return v.HashValueInternal<H>(std::move(h));
}

template <typename H>
H Value::HashValueInternal(H h) const {
  // Hash type parameter (e.g. enum's name or array's element type).
  // Struct's type parameter can be inferred from the list of its field values,
  // thus from performance considerations we don't hash it separately.
  const bool not_struct = is_valid() && metadata_.type_kind() != TYPE_STRUCT;
  if (not_struct) {
    type()->HashTypeParameter(absl::HashState::Create(&h));
  }

  const bool has_value = is_valid() && !is_null();
  if (has_value) {
    type()->HashValueContent(GetContent(), absl::HashState::Create(&h));
  }
  // Hash the type kind. Values are only equal if they have the
  // same/equivalent types, and we want to avoid hash collisions between values
  // of different types (e.g. INT32 0 vs. UINT32 0).
  // Note that invalid Values have their own TypeKind, so hash codes for
  // invalid Values do not collide with hash codes for NULL values.
  h = H::combine(std::move(h), metadata_.type_kind());
  // In order to satisfy the requirement of unequal values having hash
  // expansions that aren't suffixes of each other, we need to hash these bools
  // last so that we distinguish the optional parts of the hash expansion.
  return H::combine(std::move(h), not_struct, has_value);
}

template <>
inline int32_t Value::Get<int32_t>() const {
  return int32_value();
}
template <>
inline int64_t Value::Get<int64_t>() const {
  return int64_value();
}
template <>
inline uint32_t Value::Get<uint32_t>() const {
  return uint32_value();
}
template <>
inline uint64_t Value::Get<uint64_t>() const {
  return uint64_value();
}
template <>
inline bool Value::Get<bool>() const {
  return bool_value();
}
template <>
inline float Value::Get<float>() const {
  return float_value();
}
template <>
inline double Value::Get<double>() const {
  return double_value();
}
template <>
inline NumericValue Value::Get<NumericValue>() const {
  return numeric_value();
}
template <>
inline BigNumericValue Value::Get<BigNumericValue>() const {
  return bignumeric_value();
}
template <>
inline IntervalValue Value::Get<IntervalValue>() const {
  return interval_value();
}

// We use pointer tagging to distinguish between the case when we store type
// pointer and type kind together with value. We expect all Type pointers to be
// 8 bytes aligned (which should be the case if standard allocation mechanism is
// used since std::malloc is required to return an allocation that is suitably
// aligned for any scalar type). We use 3 lowest bits to encode is_null,
// preserves_order and has_type. These bits must never overlap with int32
// value_. Thus we use different structure layout depending on system
// endianness.
class Value::Metadata::Content {
  static constexpr uint64_t kTagMask = static_cast<uint64_t>(7);
  static constexpr uint64_t kTypeMask = ~static_cast<uint64_t>(kTagMask);
  static constexpr uint64_t kHasTypeTag = 1;
  static constexpr uint64_t kIsNullTag = 1 << 1;
  static constexpr uint64_t kPreserverOrderTag = 1 << 2;

  constexpr uint64_t GetTagValue(bool has_type, bool is_null,
                                 bool preserves_ordering) {
    return (preserves_ordering ? kPreserverOrderTag : 0) |
           (is_null ? kIsNullTag : 0) | (has_type ? kHasTypeTag : 0);
  }

 protected:
  union {
    uint64_t type_;

    struct {
      // Note: in this struct, tags_placeholder_ occupies the same memory as
      // the least significant two bytes of type_. That allows us to to store
      // the tags in the placeholder and access them with the tag mask on type_.
#if defined(ABSL_IS_BIG_ENDIAN)
      int32_t value_extended_content_;
      int16_t kind_;
      uint16_t tags_placeholder_;
#elif defined(ABSL_IS_LITTLE_ENDIAN)
      uint16_t tags_placeholder_;
      int16_t kind_;
      int32_t value_extended_content_;
#else  // !ABSL_IS_BIG_ENDIAN and !ABSL_IS_LITTLE_ENDIAN
      static_assert(false,
                    "Platform is not supported: neither big nor little endian");
#endif
#ifndef __EMSCRIPTEN__
      static_assert(
          sizeof(void*) == 8,
          "Platform is not supported: size of pointer is not 8 bytes");
#endif  // __EMSCRIPTEN__
    };
  };

 public:
  Content(const Type* type, bool is_null, bool preserves_order)
      : type_(reinterpret_cast<uint64_t>(type) |
              GetTagValue(/*has_type=*/true, is_null, preserves_order)) {}

  // clang-format off
  constexpr Content(TypeKind kind, bool is_null, bool preserves_order,
                             int32_t value_extended_content)
#if defined(ABSL_IS_BIG_ENDIAN)
      : value_extended_content_(value_extended_content),
        kind_(kind),
        tags_placeholder_(
            GetTagValue(/*has_type=*/false, is_null, preserves_order)){}
#elif defined(ABSL_IS_LITTLE_ENDIAN)
      : tags_placeholder_(
            GetTagValue(/*has_type=*/false, is_null, preserves_order)),
        kind_(kind),
        value_extended_content_(value_extended_content) {}
#else
#error Platform is not supported: neither big nor little endian;
#endif

  int16_t kind() const {
    return kind_;
  }
  // TODO: wait for fixed clang-format
  // clang-format on

  const Type* type() const {
    return reinterpret_cast<const Type*>(type_ & kTypeMask);
  }
  int32_t value_extended_content() const { return value_extended_content_; }
  bool is_null() const { return type_ & kIsNullTag; }
  bool preserves_order() const { return type_ & kPreserverOrderTag; }
  bool has_type_pointer() const { return type_ & kHasTypeTag; }
  uint64_t raw_type() const { return type_; }

  friend constexpr Value::Metadata::Metadata(TypeKind kind, bool is_null,
                                             bool preserves_order,
                                             int32_t value_extended_content);
};

constexpr Value::Metadata::Metadata(TypeKind kind, bool is_null,
                                    bool preserves_order,
                                    int32_t value_extended_content)
    // To maintain constexpr consistency under C++17 we pass the int64_t from
    // the union type to the data_ member.
    : data_(static_cast<int64_t>(
          Content(kind, is_null, preserves_order, value_extended_content)
              .raw_type())) {}

namespace values {

inline Value Int32(int32_t v) { return Value::Int32(v); }
inline Value Int64(int64_t v) { return Value::Int64(v); }
inline Value Uint32(uint32_t v) { return Value::Uint32(v); }
inline Value Uint64(uint64_t v) { return Value::Uint64(v); }
inline Value Bool(bool v) { return Value::Bool(v); }
inline Value Float(float v) { return Value::Float(v); }
inline Value Double(double v) { return Value::Double(v); }
inline Value String(absl::string_view v) { return Value::String(v); }
inline Value String(const absl::Cord& v) { return Value::String(v); }
template <size_t N>
inline Value String(const char (&str)[N]) {
  return Value::String(str);
}
inline Value Bytes(absl::string_view v) { return Value::Bytes(v); }
inline Value Bytes(const absl::Cord& v) { return Value::Bytes(v); }
template <size_t N>
inline Value Bytes(const char (&str)[N]) {
  return Value::Bytes(str);
}
inline Value Date(int32_t v) { return Value::Date(v); }
inline Value Date(absl::CivilDay day) {
  static constexpr absl::CivilDay kEpochDay = absl::CivilDay(1970, 1, 1);
  return Value::Date(static_cast<int32_t>(day - kEpochDay));
}
inline Value Timestamp(absl::Time time) { return Value::Timestamp(time); }
inline Value Timestamp(TimestampPicosValue t) { return Value::Timestamp(t); }
inline Value TimestampFromUnixMicros(int64_t v) {
  return Value::TimestampFromUnixMicros(v);
}

inline Value Time(TimeValue time) { return Value::Time(time); }
inline Value TimeFromPacked64Micros(int64_t v) {
  return Value::TimeFromPacked64Micros(v);
}
inline Value Datetime(DatetimeValue datetime) {
  return Value::Datetime(datetime);
}
inline Value DatetimeFromPacked64Micros(int64_t v) {
  return Value::DatetimeFromPacked64Micros(v);
}
inline Value Interval(IntervalValue interval) {
  return Value::Interval(interval);
}
inline Value Numeric(NumericValue v) { return Value::Numeric(v); }

inline Value Numeric(int64_t v) { return Value::Numeric(NumericValue(v)); }

inline Value BigNumeric(BigNumericValue v) { return Value::BigNumeric(v); }

inline Value BigNumeric(int64_t v) {
  return Value::BigNumeric(BigNumericValue(v));
}

inline Value Json(JSONValue v) { return Value::Json(std::move(v)); }

inline Value Enum(const EnumType* enum_type, int32_t value,
                  bool allow_unnamed_values) {
  return Value::Enum(enum_type, value, allow_unnamed_values);
}
inline Value Enum(const EnumType* enum_type, absl::string_view name) {
  return Value::Enum(enum_type, name);
}
inline Value Struct(const StructType* type, absl::Span<const Value> values) {
  return Value::Struct(type, values);
}
inline Value UnsafeStruct(const StructType* type, std::vector<Value>&& values) {
  return Value::UnsafeStruct(type, std::move(values));
}
inline Value Uuid(UuidValue v) { return Value::Uuid(v); }

inline Value Proto(const ProtoType* proto_type, absl::Cord value) {
  return Value::Proto(proto_type, std::move(value));
}
inline Value Proto(const ProtoType* proto_type, const google::protobuf::Message& msg) {
  absl::Cord bytes;
  ABSL_CHECK(msg.SerializeToCord(&bytes));
  return Value::Proto(proto_type, std::move(bytes));
}
inline Value EmptyArray(const ArrayType* type) {
  return Value::Array(type, {});
}
inline Value Array(const ArrayType* type, absl::Span<const Value> values) {
  return Value::Array(type, values);
}
inline Value UnsafeArray(const ArrayType* type, std::vector<Value>&& values) {
  return Value::UnsafeArray(type, std::move(values));
}
inline Value True() { return Value::Bool(true); }
inline Value False() { return Value::Bool(false); }
inline Value EmptyGeography() { return Value::EmptyGeography(); }
inline Value Range(Value start, Value end) {
  absl::StatusOr<Value> value = Value::MakeRange(start, end);
  GOOGLESQL_DCHECK_OK(value);
  return std::move(value).value();
}
inline Value NullInt32() { return Value::NullInt32(); }
inline Value NullInt64() { return Value::NullInt64(); }
inline Value NullUint32() { return Value::NullUint32(); }
inline Value NullUint64() { return Value::NullUint64(); }
inline Value NullBool() { return Value::NullBool(); }
inline Value NullFloat() { return Value::NullFloat(); }
inline Value NullDouble() { return Value::NullDouble(); }
inline Value NullString() { return Value::NullString(); }
inline Value NullBytes() { return Value::NullBytes(); }
inline Value NullDate() { return Value::NullDate(); }
inline Value NullTimestamp() { return Value::NullTimestamp(); }
inline Value NullTime() { return Value::NullTime(); }
inline Value NullDatetime() { return Value::NullDatetime(); }
inline Value NullInterval() { return Value::NullInterval(); }
inline Value NullGeography() { return Value::NullGeography(); }
inline Value NullNumeric() { return Value::NullNumeric(); }
inline Value NullBigNumeric() { return Value::NullBigNumeric(); }
inline Value NullJson() { return Value::NullJson(); }
inline Value NullTokenList() { return Value::NullTokenList(); }
inline Value NullUuid() { return Value::NullUuid(); }
inline Value Null(const Type* type) { return Value::Null(type); }

inline Value Invalid() { return Value::Invalid(); }

}  // namespace values
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_VALUE_INL_H_
