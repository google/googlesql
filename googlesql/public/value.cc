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

#include "googlesql/public/value.h"

#include <string.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/functions/comparison.h"
#include "googlesql/public/functions/convert_string.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value_content.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "googlesql/base/map_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// TODO: Remove flag once no longer required.
ABSL_FLAG(bool, googlesql_allow_proto3_unknown_enum_values, true,
          "Accept enum values without names in proto3 enums");

using googlesql::types::BigNumericArrayType;
using googlesql::types::BoolArrayType;
using googlesql::types::BytesArrayType;
using googlesql::types::DateArrayType;
using googlesql::types::DoubleArrayType;
using googlesql::types::FloatArrayType;
using googlesql::types::Int32ArrayType;
using googlesql::types::Int64ArrayType;
using googlesql::types::JsonArrayType;
using googlesql::types::NumericArrayType;
using googlesql::types::StringArrayType;
using googlesql::types::TimestampArrayType;
using googlesql::types::Uint32ArrayType;
using googlesql::types::Uint64ArrayType;

using absl::Substitute;

namespace googlesql {

namespace {

// Returns true if the range contains the point.
// `range` and `point` must be non-NULL and have compatible types.
// A range [start, end) contains point p if start <= p < end.
// NULL start is treated as -inf, and NULL end is treated as +inf.
absl::StatusOr<bool> RangeContainsPoint(const Value& range,
                                        const Value& point) {
  GOOGLESQL_RET_CHECK(range.type()->IsRange());
  GOOGLESQL_RET_CHECK(range.type()->AsRange()->element_type()->Equals(point.type()));
  GOOGLESQL_RET_CHECK(!range.is_null());
  GOOGLESQL_RET_CHECK(!point.is_null());
  const Value& start = range.start();
  const Value& end = range.end();
  bool start_ok = start.is_null() || !point.LessThan(start);
  bool end_ok = end.is_null() || point.LessThan(end);
  return start_ok && end_ok;
}

// Returns true if the `container` range contains the `contained` range.
// Both ranges must be non-NULL.
// A range [s1, e1) contains range [s2, e2) if s1 <= s2 and e2 <= e1.
// NULL start is treated as -inf, and NULL end is treated as +inf.
absl::StatusOr<bool> RangeContainsRange(const Value& container,
                                        const Value& contained) {
  GOOGLESQL_RET_CHECK(container.type()->IsRange());
  GOOGLESQL_RET_CHECK(contained.type()->IsRange());
  GOOGLESQL_RET_CHECK(container.type()->Equals(contained.type()));
  GOOGLESQL_RET_CHECK(!container.is_null());
  GOOGLESQL_RET_CHECK(!contained.is_null());
  const Value& s1 = container.start();
  const Value& e1 = container.end();
  const Value& s2 = contained.start();
  const Value& e2 = contained.end();

  bool s1_le_s2 = s1.is_null() || (!s2.is_null() && !s2.LessThan(s1));
  bool e2_le_e1 = e1.is_null() || (!e2.is_null() && !e1.LessThan(e2));
  return s1_le_s2 && e2_le_e1;
}

// Returns the intersection of a non-empty list of ranges.
// Requires that all ranges are non-NULL and have the same element type.
// NULL start is treated as -inf, and NULL end is treated as +inf.
absl::StatusOr<Value> GetRangeIntersection(absl::Span<const Value> ranges) {
  if (ranges.empty()) {
    return absl::InvalidArgumentError(
        "GetRangeIntersection requires at least one range");
  }
  GOOGLESQL_RET_CHECK(ranges[0].type()->IsRange());
  const Type* element_type = ranges[0].type()->AsRange()->element_type();

  Value max_start = Value::Null(element_type);
  Value min_end = Value::Null(element_type);
  for (const Value& range : ranges) {
    GOOGLESQL_RET_CHECK(range.type()->IsRange());
    GOOGLESQL_RET_CHECK(range.type()->AsRange()->element_type()->Equals(element_type));
    GOOGLESQL_RET_CHECK(!range.is_null());
    const Value& r_start = range.start();
    const Value& r_end = range.end();
    if (!r_start.is_null() &&
        (max_start.is_null() || max_start.LessThan(r_start))) {
      max_start = r_start;
    }
    if (!r_end.is_null() && (min_end.is_null() || r_end.LessThan(min_end))) {
      min_end = r_end;
    }
  }
  return Value::MakeRange(max_start, min_end);
}

// Validates that `graph_elements` are valid for creating a graph path
// with the specified `graph_path_type`.
absl::Status ValidateGraphPathElements(const GraphPathType* graph_path_type,
                                       absl::Span<const Value> graph_elements) {
  GOOGLESQL_RET_CHECK_EQ(graph_elements.size() % 2, 1)
      << "Path must have an odd number of graph elements, but got "
      << graph_elements.size() << " graph elements";
  for (int i = 0; i < graph_elements.size(); ++i) {
    GOOGLESQL_RET_CHECK(!graph_elements[i].is_null())
        << "Path cannot have null graph elements";
    if (i % 2 == 0) {
      GOOGLESQL_RET_CHECK(graph_elements[i].type()->Equals(graph_path_type->node_type()))
          << "Expected node type";
    } else {
      GOOGLESQL_RET_CHECK(graph_elements[i].type()->Equals(graph_path_type->edge_type()))
          << "Expected edge type";
    }
  }
  for (int i = 1; i < graph_elements.size(); i += 2) {
    // The edge must connect the previous node to the next node in some
    // direction.
    const Value& edge = graph_elements[i];
    const Value& previous_node = graph_elements[i - 1];
    const Value& next_node = graph_elements[i + 1];
    bool forward =
        previous_node.GetIdentifier() == edge.GetSourceNodeIdentifier() &&
        next_node.GetIdentifier() == edge.GetDestNodeIdentifier();
    bool backward =
        previous_node.GetIdentifier() == edge.GetDestNodeIdentifier() &&
        next_node.GetIdentifier() == edge.GetSourceNodeIdentifier();
    GOOGLESQL_RET_CHECK(forward || backward) << "Edge must connect the previous node to "
                                      "the next node in some direction";
  }
  return absl::OkStatus();
}
}  // namespace

// -------------------------------------------------------
// Value
// -------------------------------------------------------

std::ostream& operator<<(std::ostream& out, const Value& value) {
  return out << value.FullDebugString();
}

void Value::SetMetadataForNonSimpleType(const Type* type, bool is_null,
                                        bool preserves_order) {
  ABSL_DCHECK(!type->IsSimpleType());
  metadata_ = Metadata(type, is_null, preserves_order);
  internal::TypeStoreHelper::RefFromValue(type->type_store_);
}

// Null value constructor.
Value::Value(const Type* type, bool is_null, OrderPreservationKind order_kind) {
  ABSL_CHECK(type != nullptr);

  if (type->IsMap()) {
    ABSL_DCHECK(order_kind == kIgnoresOrder);
    order_kind = kIgnoresOrder;
  }

  if (type->IsSimpleType()) {
    metadata_ = Metadata(type->kind(), is_null, order_kind,
                         /*value_extended_content=*/0);
  } else {
    SetMetadataForNonSimpleType(type, is_null, order_kind);
  }
}

void Value::CopyFrom(const Value& that) {
  // Self-copy check is done in the copy constructor. Here we just ABSL_DCHECK that.
  ABSL_DCHECK_NE(this, &that);
  memcpy(this, &that, sizeof(Value));
  if (!is_valid()) {
    return;
  }

  if (metadata_.has_type_pointer()) {
    internal::TypeStoreHelper::RefFromValue(metadata_.type()->type_store_);
    // TODO: currently struct and array maintain a reference
    // counter for their value. To improve performance of struct/array copying,
    // instead of incrementing types reference counter on each Value copy, we
    // can just do a single decrement when Value reference counter reaches zero.

    if (!is_null()) {
      ValueContent copied_content{};
      that.type()->CopyValueContent(that.GetContent(), &copied_content);
      SetContent(copied_content);
    }
  } else {
    // When we already have a type_kind, we know that we are a SimpleType.
    // Dispatching directly to the SimpleType implementation avoids unnecessary
    // virtual calls and switches.
    if (!is_null()) {
      ValueContent copied_content{};
      SimpleType::CopyValueContent(metadata_.type_kind(), that.GetContent(),
                                   &copied_content);
      SetContent(copied_content);
    }
  }
}

Value::Value(TypeKind type_kind, int64_t value) : metadata_(type_kind) {
  switch (type_kind) {
    case TYPE_DATE:
      ABSL_CHECK_LE(value, types::kDateMax);
      ABSL_CHECK_GE(value, types::kDateMin);
      int32_value_ = value;
      break;
    default:
      ABSL_LOG(FATAL) << "Invalid use of private constructor: " << type_kind;
  }
}

Value::Value(absl::Time t) {
  ABSL_CHECK(functions::IsValidTime(t));
  TimestampPicosValue timestamp_picos_value =
      TimestampPicosValue::Create(t).value();
  metadata_ = Metadata(TypeKind::TYPE_TIMESTAMP);
  timestamp_picos_ptr_ = new internal::TimestampPicosRef(timestamp_picos_value);
}

Value::Value(const TimestampPicosValue& t, TypeKind type_kind)
    : metadata_(type_kind),
      timestamp_picos_ptr_(new internal::TimestampPicosRef(t)) {}

Value::Value(TimeValue time)
    : metadata_(TypeKind::TYPE_TIME, time.Nanoseconds()),
      bit_field_32_value_(time.Packed32TimeSeconds()) {
  ABSL_CHECK(time.IsValid());
}

Value::Value(DatetimeValue datetime)
    : metadata_(TypeKind::TYPE_DATETIME, datetime.Nanoseconds()),
      bit_field_64_value_(datetime.Packed64DatetimeSeconds()) {
  ABSL_CHECK(datetime.IsValid());
}

Value Value::Enum(const EnumType* type, int64_t value) {
  return Value(type, value,
               absl::GetFlag(FLAGS_googlesql_allow_proto3_unknown_enum_values));
}

Value::Value(const EnumType* enum_type, int64_t value,
             bool allow_unknown_enum_values) {
  absl::string_view unused;

  // We confirm immediately afterwards that the cast back works, so we can be
  // sure no out of range integers are accepted.
  const int32_t int32_value = static_cast<int32_t>(value);

  if (static_cast<int64_t>(int32_value) == value &&
      ((allow_unknown_enum_values && enum_type->EnumAllowsUnnamedValues()) ||
       enum_type->FindName(int32_value, &unused))) {
    SetMetadataForNonSimpleType(enum_type);
    enum_value_ = int32_value;
  } else {
    metadata_ = Metadata::Invalid();
  }
}

Value::Value(const EnumType* enum_type, absl::string_view name) {
  int32_t number;
  if (enum_type->FindNumber(name, &number)) {
    SetMetadataForNonSimpleType(enum_type);
    enum_value_ = static_cast<int32_t>(number);
  } else {
    metadata_ = Metadata::Invalid();
  }
}

Value::Value(const ProtoType* proto_type, absl::Cord value)
    : proto_ptr_(new internal::ProtoRep(proto_type, std::move(value))) {
  SetMetadataForNonSimpleType(proto_type);
}

Value::Value(const ExtendedType* extended_type, const ValueContent& value) {
  ABSL_DCHECK_EQ(value.simple_type_extended_content_, 0);
  SetMetadataForNonSimpleType(extended_type);
  SetContent(value);
}

absl::StatusOr<UuidValue> Value::uuid_value() const {
  GOOGLESQL_RET_CHECK_EQ(TYPE_UUID, metadata_.type_kind()) << "Not a uuid type";
  GOOGLESQL_RET_CHECK(!metadata_.is_null()) << "Null value";
  return uuid_ptr_->value();
}

#ifdef NDEBUG
static constexpr bool kDebugMode = false;
#else
static constexpr bool kDebugMode = true;
#endif

absl::StatusOr<Value> Value::MakeArrayInternal(bool already_validated,
                                               const ArrayType* array_type,
                                               OrderPreservationKind order_kind,
                                               std::vector<Value> values) {
  if (!already_validated || kDebugMode) {
    GOOGLESQL_RET_CHECK(array_type != nullptr);
    for (const Value& v : values) {
      GOOGLESQL_RET_CHECK(v.is_valid() && v.type()->Equals(array_type->element_type()))
          << "Array element " << v << " must be of type "
          << array_type->element_type()->DebugString();
    }
  }

  Value result(array_type, /*is_null=*/false, order_kind);
  result.container_ptr_ = new internal::ValueContentOrderedListRef(
      std::make_unique<TypedList>(std::move(values)),
      order_kind == kPreservesOrder);
  return result;
}

absl::StatusOr<Value> Value::MakeStructInternal(bool already_validated,
                                                const StructType* struct_type,
                                                std::vector<Value> values) {
  if (!already_validated || kDebugMode) {
    // Check that values are compatible with the type.
    GOOGLESQL_RET_CHECK_EQ(struct_type->num_fields(), values.size());
    for (int i = 0; i < values.size(); ++i) {
      const Type* field_type = struct_type->field(i).type;
      const Type* value_type = values[i].type();
      GOOGLESQL_RET_CHECK(field_type->Equals(value_type))
          << "\nField type: " << field_type->DebugString()
          << "\nvs\nValue type: " << value_type->DebugString();
    }
  }

  Value result(struct_type, /*is_null=*/false, kPreservesOrder);
  result.container_ptr_ = new internal::ValueContentOrderedListRef(
      std::make_unique<TypedList>(std::move(values)), /*preserves_order=*/true);
  return result;
}

absl::StatusOr<Value> Value::MakeRangeInternal(bool is_validated,
                                               const Value& start,
                                               const Value& end,
                                               const RangeType* type) {
  const RangeType* range_type = type;

  if (!is_validated || kDebugMode) {
    range_type = types::RangeTypeFromSimpleTypeKind(start.type_kind());

    GOOGLESQL_RET_CHECK(start.type()->Equals(end.type()))
        << "Range start element and range end element must have the same "
           "type";

    // If both ends are not unbounded, then enforce that start < end.
    if (!start.is_null() && !end.is_null() && !start.LessThan(end)) {
      return absl::InvalidArgumentError(
          "Range start element must be smaller than range end element");
    }
  }

  std::vector<Value> values;
  values.push_back(start);
  values.push_back(end);

  Value result(range_type, /*is_null=*/false, kPreservesOrder);
  result.container_ptr_ = new internal::ValueContentOrderedListRef(
      std::make_unique<TypedList>(std::move(values)), /*preserves_order=*/true);
  return result;
}

using CaseInsensitiveLabelSet =
    absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;

absl::StatusOr<Value> Value::MakeGraphElement(
    const GraphElementType* graph_element_type,
    const GraphElementLabelsAndProperties& labels_and_properties,
    const GraphNodeKeysAndElementTableNames& node_data,
    const GraphEdgeKeysAndElementTableNames& edge_data
) {
  // Validates the identifiers.
  if (graph_element_type->IsNode()) {
    GOOGLESQL_RET_CHECK(!node_data.identifier.empty()) << "Empty identifier";
  } else {
    GOOGLESQL_RET_CHECK(!edge_data.identifier.empty()) << "Empty identifier";
    GOOGLESQL_RET_CHECK(!edge_data.source.identifier.empty())
        << "Invalid source node identifier";
    GOOGLESQL_RET_CHECK(!edge_data.dest.identifier.empty())
        << "Invalid destination node identifier";
  }
  GOOGLESQL_RET_CHECK_EQ(graph_element_type->is_dynamic(),
               labels_and_properties.dynamic_properties.has_value())
      << "Dynamic properties JSON value must be provided for dynamic graph "
         "element types";

  ValidPropertyNameToIndexMap property_name_to_index;
  std::vector<Value> property_values(
      graph_element_type->property_types().size(), Value::Invalid());
  // Validates the property types based on name.
  for (const auto& [name, value] : labels_and_properties.static_properties) {
    int field_index;
    GOOGLESQL_RET_CHECK(graph_element_type->HasField(name, &field_index) ==
              Type::HAS_FIELD)
        << "Unknown property: " << name;
    const PropertyType* property_type =
        graph_element_type->FindPropertyType(name);
    GOOGLESQL_RET_CHECK(property_type != nullptr);
    GOOGLESQL_RET_CHECK(property_type->value_type->Equals(value.type()))
        << "Expected property value type: "
        << property_type->value_type->DebugString()
        << ", got: " << value.type()->DebugString();
    property_values[field_index] = std::move(value);
    property_name_to_index.emplace(name, field_index);
  }
  int property_index =
      static_cast<int>(graph_element_type->property_types().size());
  if (labels_and_properties.dynamic_properties.has_value()) {
    for (auto& [k, v] :
         labels_and_properties.dynamic_properties->GetMembers()) {
        if (!property_name_to_index.contains(std::string(k))) {
        property_name_to_index.emplace(k, property_index++);
        // Dynamic properties are copied into GraphElementValue as JSON-typed
        // values.
        property_values.emplace_back(Value::Json(JSONValue::CopyFrom(v)));
      }
    }
  }

  // We keep the original case but sort labels case-insensitively.
  CaseInsensitiveLabelSet label_name_set(
      labels_and_properties.static_labels.begin(),
      labels_and_properties.static_labels.end());
  label_name_set.insert(labels_and_properties.dynamic_labels.begin(),
                        labels_and_properties.dynamic_labels.end());
  std::vector<std::string> sorted_labels(label_name_set.begin(),
                                         label_name_set.end());
  absl::c_sort(sorted_labels, googlesql_base::CaseLess());

  std::unique_ptr<internal::ValueContentOrderedList> container;
  if (graph_element_type->IsNode()) {
    const auto& [identifier, keys, element_table_name] = node_data;
    container = std::make_unique<GraphElementValue>(
        graph_element_type, std::string(identifier), std::move(property_values),
        std::move(property_name_to_index), std::move(sorted_labels),
        std::string(element_table_name),
        keys
    );
  } else {
    const auto& [identifier, element_table_name, source, dest] = edge_data;
    container = std::make_unique<GraphElementValue>(
        graph_element_type, std::string(identifier), std::move(property_values),
        std::move(property_name_to_index), std::move(sorted_labels),
        std::string(element_table_name), std::string(source.identifier),
        std::string(dest.identifier), source.keys, dest.keys,
        std::string(source.element_table_name),
        std::string(dest.element_table_name)
    );
  }
  Value result(graph_element_type, /*is_null=*/false,
               /*order_kind=*/kIgnoresOrder);
  result.container_ptr_ = new internal::ValueContentOrderedListRef(
      std::move(container), /*preserves_order=*/false);
  return result;
}

absl::StatusOr<Value> Value::MakeGraphPath(const GraphPathType* graph_path_type,
                                           std::vector<Value> graph_elements) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphPathElements(graph_path_type,
  graph_elements));
  Value result(graph_path_type, /*is_null=*/false,
      /*order_kind=*/kIgnoresOrder);
  result.container_ptr_ = new internal::ValueContentOrderedListRef(
      std::make_unique<GraphPathValue>(std::move(graph_elements)),
      /*preserves_order=*/false);
  return result;
}

template <typename T>
static absl::Status ValidateMapEntries(const MapType* map_type,
                                       const T& map_entries) {
  for (const auto& [key, value] : map_entries) {
    GOOGLESQL_RET_CHECK(key.is_valid() && key.type()->Equals(map_type->key_type()) &&
              value.is_valid() && value.type()->Equals(map_type->value_type()))
        << absl::StrCat("Values with invalid types provided to map. Expected ",
                        map_type->DebugString(),
                        " but got an entry with key of ",
                        key.type()->DebugString(), " and value of ",
                        value.type()->DebugString(), ".");
  }
  return absl::OkStatus();
}

absl::StatusOr<Value> Value::MakeMapInternal(const Type* type,
                                             googlesql_base::MapView<Value, Value> map) {
  GOOGLESQL_RET_CHECK(type->IsMap()) << type->DebugString();
  if (kDebugMode) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMapEntries(type->AsMap(), map));
  }

  Value result(type, /*is_null=*/false, kIgnoresOrder);
  result.map_ptr_ =
      new internal::ValueContentMapRef(std::make_unique<TypedMap>(map));

  return result;
}

absl::StatusOr<Value> Value::MakeMapInternal(
    const Type* type, const std::vector<std::pair<Value, Value>>& map_entries) {
  GOOGLESQL_RET_CHECK(type->IsMap()) << type->DebugString();
  if (kDebugMode) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMapEntries(type->AsMap(), map_entries));
  }

  Value result(type, /*is_null=*/false, kIgnoresOrder);
  result.map_ptr_ =
      new internal::ValueContentMapRef(std::make_unique<TypedMap>(map_entries));

  // If map_entries size is different from map size, we can infer that a
  // nonzero amount of keys are duplicates.
  if (map_entries.size() != result.map_ptr_->value()->num_elements()) {
    absl::flat_hash_set<Value> map_entries_set;
    for (const auto& [key, value] : map_entries) {
      const auto& [unused_iter, inserted] = map_entries_set.insert(key);
      if (!inserted) {
        return MakeEvalError()
               << "Duplicate map keys are not allowed, but got multiple "
                  "instances of key: "
               << key.Format(/*print_top_level_type=*/false);
      }
    }
    // If execution gets here without finding the key, something is wrong with
    // the logic.
    return MakeEvalError()
           << "Duplicate map keys are not allowed, but got multiple "
              "instances of a key";
  }

  return result;
}

const Type* Value::type() const {
  ABSL_CHECK(is_valid()) << DebugString();
  return metadata_.type();
}

const std::vector<Value>& Value::fields() const {
  ABSL_CHECK_EQ(TYPE_STRUCT, metadata_.type_kind());
  ABSL_CHECK(!is_null()) << "Null value";
  const internal::ValueContentOrderedList* const container_ptr =
      container_ptr_->value();
  const TypedList* const list_ptr =
      static_cast<const TypedList* const>(container_ptr);
  return list_ptr->values();
}

const std::vector<Value>& Value::elements() const {
  ABSL_CHECK_EQ(TYPE_ARRAY, metadata_.type_kind());
  ABSL_CHECK(!is_null()) << "Null value";
  const internal::ValueContentOrderedList* const container_ptr =
      container_ptr_->value();
  const TypedList* const list_ptr =
      static_cast<const TypedList* const>(container_ptr);
  return list_ptr->values();
}

googlesql_base::MapView<Value, Value> Value::map_entries() const {
  if (metadata_.type_kind() != TYPE_MAP || is_null()) {
    ABSL_DCHECK_EQ(TYPE_MAP, metadata_.type_kind());
    ABSL_DCHECK(!is_null()) << "Null value";

    static const absl::flat_hash_map<Value, Value>* empty_map =
        new absl::flat_hash_map<Value, Value>();
    return *empty_map;
  }
  return static_cast<const TypedMap* const>(map_ptr_->value())->entries();
}

Value Value::TimestampFromUnixMicros(int64_t v) {
  ABSL_CHECK(functions::IsValidTimestamp(v, functions::kMicroseconds)) << v;
  return Value(absl::FromUnixMicros(v));
}

Value Value::TimeFromPacked64Micros(int64_t v) {
  TimeValue time = TimeValue::FromPacked64Micros(v);
  ABSL_CHECK(time.IsValid()) << "int64 " << v
                        << " decodes to an invalid time value: "
                        << time.DebugString();
  return Value(time);
}

Value Value::DatetimeFromPacked64Micros(int64_t v) {
  DatetimeValue datetime = DatetimeValue::FromPacked64Micros(v);
  ABSL_CHECK(datetime.IsValid())
      << "int64 " << v
      << " decodes to an invalid datetime value: " << datetime.DebugString();
  return Value(datetime);
}

absl::StatusOr<absl::string_view> Value::EnumName() const {
  if (metadata_.type_kind() != TYPE_ENUM) {
    return absl::InvalidArgumentError("Not an enum value");
  }
  if (is_null()) {
    return absl::InvalidArgumentError("Null enum value");
  }
  absl::string_view enum_name;
  if (!type()->AsEnum()->FindName(enum_value(), &enum_name)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Value ", enum_value(), " not in ", type()->DebugString()));
  }
  return enum_name;
}
std::string Value::EnumDisplayName() const {
  ABSL_CHECK_EQ(TYPE_ENUM, metadata_.type_kind()) << "Not an enum value";
  ABSL_CHECK(!is_null()) << "Null value";
  absl::string_view enum_name;
  if (type()->AsEnum()->FindName(enum_value(), &enum_name)) {
    return std::string(enum_name);
  }
  return absl::StrCat(enum_value());
}

int64_t Value::ToInt64() const {
  ABSL_CHECK(!is_null()) << "Null value";
  switch (metadata_.type_kind()) {
    case TYPE_INT64:
      return int64_value_;
    case TYPE_INT32:
      return int32_value_;
    case TYPE_UINT32:
      return uint32_value_;
    case TYPE_BOOL:
      return bool_value_;
    case TYPE_DATE:
      return int32_value_;
    case TYPE_TIMESTAMP:
      return ToUnixMicros();
    case TYPE_ENUM:
      return enum_value();
    case TYPE_STRING:
    case TYPE_BYTES:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    case TYPE_PROTO:
    case TYPE_TIME:
    case TYPE_DATETIME:
    default:
      ABSL_LOG(FATAL) << "Cannot coerce " << TypeKind_Name(type_kind())
                 << " to int64";
  }
}

uint64_t Value::ToUint64() const {
  ABSL_CHECK(!is_null()) << "Null value";
  switch (metadata_.type_kind()) {
    case TYPE_UINT64:
      return uint64_value_;
    case TYPE_UINT32:
      return uint32_value_;
    case TYPE_BOOL:
      return bool_value_;
    default:
      ABSL_LOG(FATAL) << "Cannot coerce " << TypeKind_Name(type_kind())
                 << " to uint64";
      return 0;
  }
}

double Value::ToDouble() const {
  ABSL_CHECK(!is_null()) << "Null value";
  switch (metadata_.type_kind()) {
    case TYPE_BOOL:
      return bool_value_;
    case TYPE_DATE:
      return int32_value_;
    case TYPE_DOUBLE:
      return double_value_;
    case TYPE_FLOAT:
      return float_value_;
    case TYPE_INT32:
      return int32_value_;
    case TYPE_UINT32:
      return uint32_value_;
    case TYPE_UINT64:
      return uint64_value_;
    case TYPE_INT64:
      return int64_value_;
    case TYPE_NUMERIC:
      return numeric_value().ToDouble();
    case TYPE_BIGNUMERIC:
      return bignumeric_value().ToDouble();
    case TYPE_ENUM:
      return enum_value();
    case TYPE_TIMESTAMP:
    case TYPE_STRING:
    case TYPE_BYTES:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    case TYPE_PROTO:
    case TYPE_TIME:
    case TYPE_DATETIME:
    default:
      ABSL_LOG(FATAL) << "Cannot coerce " << TypeKind_Name(type_kind())
                 << " to double";
  }
}

uint64_t Value::physical_byte_size() const {
  uint64_t physical_size = sizeof(Value);
  if (!has_content()) {
    return physical_size;
  }

  if (DoesTypeUseValueList()) {
    physical_size += container_ptr_->physical_byte_size();
  } else if (DoesTypeUseValueMap()) {
    physical_size += map_ptr_->physical_byte_size();
  } else if (DoesTypeUseValueMeasure()) {
    physical_size += measure_ptr_->physical_byte_size();
  } else {
    physical_size +=
        type()->GetValueContentExternallyAllocatedByteSize(GetContent());
  }

  return physical_size;
}

absl::Cord Value::ToCord() const {
  ABSL_CHECK(!is_null()) << "Null value";
  switch (metadata_.type_kind()) {
    case TYPE_STRING:
    case TYPE_BYTES:
      return absl::Cord(string_ptr_->value());
    case TYPE_PROTO:
      return proto_ptr_->value();
    default:
      ABSL_LOG(FATAL) << "Cannot coerce " << TypeKind_Name(type_kind())
                 << " to Cord";
      return absl::Cord();
  }
}

std::string Value::ToString() const {
  ABSL_CHECK(!is_null()) << "Null value";
  switch (metadata_.type_kind()) {
    case TYPE_STRING:
    case TYPE_BYTES:
      return string_ptr_->value();
    case TYPE_PROTO:
      return std::string(proto_ptr_->value());
    default:
      ABSL_LOG(FATAL) << "Cannot coerce " << TypeKind_Name(type_kind())
                 << " to std::string";
      return std::string();
  }
}

absl::CivilDay Value::ToCivilDay() const {
  ABSL_CHECK(!is_null()) << "Null value";
  ABSL_CHECK_EQ(TYPE_DATE, metadata_.type_kind()) << "Not a date value";
  return absl::CivilDay() + date_value();
}

absl::Time Value::ToTime() const {
  ABSL_CHECK(!is_null()) << "Null value";
  ABSL_CHECK_EQ(TYPE_TIMESTAMP, metadata_.type_kind()) << "Not a timestamp value";
  ABSL_DCHECK_EQ(timestamp_picos_ptr_->value().ToPicoTime().SubNanoseconds(), 0);
  return timestamp_picos_ptr_->value().ToAbslTime();
}

int64_t Value::ToUnixMicros() const { return absl::ToUnixMicros(ToTime()); }

int64_t Value::ToPacked64TimeMicros() const {
  return (static_cast<int64_t>(bit_field_32_value_) << kMicrosShift) |
         (subsecond_nanos() / 1000);
}

int64_t Value::ToPacked64DatetimeMicros() const {
  return (bit_field_64_value_ << kMicrosShift) | (subsecond_nanos() / 1000);
}

absl::Status Value::ToUnixNanos(int64_t* nanos) const {
  if (functions::FromTime(ToTime(), functions::kNanoseconds, nanos)) {
    return absl::OkStatus();
  }
  return absl::Status(absl::StatusCode::kOutOfRange,
                      absl::StrCat("Timestamp value in Unix epoch nanoseconds "
                                   "exceeds 64 bit: ",
                                   DebugString()));
}

const TimestampPicosValue& Value::ToUnixPicos() const {
  ABSL_DCHECK_EQ(TYPE_TIMESTAMP, metadata_.type_kind()) << "Not a timestamp";
  ABSL_DCHECK(!is_null()) << "Null value";
  return timestamp_picos_ptr_->value();
}

ValueContent Value::extended_value() const {
  ABSL_CHECK_EQ(type_kind(), TYPE_EXTENDED);
  return GetContent();
}

google::protobuf::Message* Value::ToMessage(
    google::protobuf::DynamicMessageFactory* message_factory,
    bool return_null_on_error) const {
  ABSL_CHECK(type()->IsProto());
  ABSL_CHECK(!is_null());
  std::unique_ptr<google::protobuf::Message> m(
      message_factory->GetPrototype(type()->AsProto()->descriptor())->New());
  const bool success = m->ParsePartialFromCord(ToCord());
  if (!success && return_null_on_error) return nullptr;
  return m.release();
}

const Value& Value::FindFieldByName(absl::string_view name) const {
  ABSL_CHECK(type()->IsStruct());
  ABSL_CHECK(!is_null()) << "Null value";
  if (!name.empty()) {
    // Find field position.
    for (int i = 0; i < type()->AsStruct()->num_fields(); i++) {
      if (type()->AsStruct()->field(i).name == name) {
        return field(i);
      }
    }
  }
  static Value invalid_value;
  return invalid_value;
}

// Always returns false.
static bool TypesDiffer(const Value& x, const Value& y, std::string* reason) {
  if (reason) {
    absl::StrAppend(
        reason,
        absl::Substitute("Types differ: {$0} vs. {$1} respectively of "
                         "values {$2} and {$3}\n",
                         x.type()->DebugString(), y.type()->DebugString(),
                         x.DebugString(), y.DebugString()));
  }
  return false;
}

void Value::FillDeepOrderKindSpec(const Value& v, DeepOrderKindSpec* spec) {
  if (v.is_null()) {
    return;
  }

  // TODO: b/343504941 - Move type-specific logic into respective type classes.
  switch (v.type_kind()) {
    case TYPE_ARRAY:
      if (v.order_kind() == kIgnoresOrder) {
        spec->ignores_order = true;
      }
      if (spec->children.empty()) {
        spec->children.resize(1);
      }
      for (int i = 0; i < v.num_elements(); i++) {
        Value::FillDeepOrderKindSpec(v.element(i), &spec->children[0]);
      }
      break;
    case TYPE_STRUCT:
      if (spec->children.empty()) {
        spec->children.resize(v.num_fields());
      }
      ABSL_DCHECK_EQ(spec->children.size(), v.num_fields());
      for (int i = 0; i < v.num_fields(); i++) {
        Value::FillDeepOrderKindSpec(v.field(i), &spec->children[i]);
      }
      break;
    case TYPE_MAP: {
      // Note: MAP always keeps spec.ignores_order as default, since this
      // property only refers to array ordering.
      if (spec->children.empty()) {
        spec->children.resize(2);
      }

      ABSL_DCHECK_EQ(spec->children.size(), 2)
          << "Malformed deep_order_spec. MAP spec should always have exactly 2 "
             "direct children representing key and value.";

      auto& map_key_spec = spec->children[0];
      auto& map_value_spec = spec->children[1];

      for (auto& [key, value] : v.map_entries()) {
        Value::FillDeepOrderKindSpec(key, &map_key_spec);
        Value::FillDeepOrderKindSpec(value, &map_value_spec);
      }
      break;
    }
    default:
      return;
  }
}

// x is the expected value whose orderedness is taken into account when
// allow_bags = true.
bool Value::EqualsInternal(const Value& x, const Value& y,
                           const bool allow_bags,
                           const ValueEqualityCheckOptions& options) {
  std::string* reason = options.reason;
  if (!x.is_valid()) {
    return !y.is_valid();
  }
  if (!y.is_valid()) {
    return false;
  }

  if (!x.type()->Equivalent(y.type())) return TypesDiffer(x, y, reason);

  if (x.is_null() != y.is_null()) return false;
  if (x.is_null() && y.is_null()) return true;

  ValueEqualityCheckOptions const* extended_options = &options;
  std::unique_ptr<ValueEqualityCheckOptions> options_copy = nullptr;
  std::unique_ptr<DeepOrderKindSpec> owned_deep_order_spec;
  // If "allow_bags" is true, create a copy of options with populated
  // deep_order_spec
  if (allow_bags) {
    options_copy = std::make_unique<ValueEqualityCheckOptions>(options);
    owned_deep_order_spec = std::make_unique<DeepOrderKindSpec>();
    options_copy->deep_order_spec = owned_deep_order_spec.get();
    Value::FillDeepOrderKindSpec(x, options_copy->deep_order_spec);
    Value::FillDeepOrderKindSpec(y, options_copy->deep_order_spec);
    extended_options = options_copy.get();
  }
  auto result = x.type()->ValueContentEquals(x.GetContent(), y.GetContent(),
                                             *extended_options);
  return result;
}

// Function used to switch of a pair of TypeKinds
static constexpr uint64_t TYPE_KIND_PAIR(TypeKind kind1, TypeKind kind2) {
  return (static_cast<uint64_t>(kind1) << 16) | static_cast<uint64_t>(kind2);
}

static bool TypesSupportSqlEquals(const Type* type1, const Type* type2) {
  switch (TYPE_KIND_PAIR(type1->kind(), type2->kind())) {
    case TYPE_KIND_PAIR(TYPE_INT32, TYPE_INT32):
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_INT64):
    case TYPE_KIND_PAIR(TYPE_UINT32, TYPE_UINT32):
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_UINT64):
    case TYPE_KIND_PAIR(TYPE_BOOL, TYPE_BOOL):
    case TYPE_KIND_PAIR(TYPE_STRING, TYPE_STRING):
    case TYPE_KIND_PAIR(TYPE_BYTES, TYPE_BYTES):
    case TYPE_KIND_PAIR(TYPE_DATE, TYPE_DATE):
    case TYPE_KIND_PAIR(TYPE_TIMESTAMP, TYPE_TIMESTAMP):
    case TYPE_KIND_PAIR(TYPE_TIME, TYPE_TIME):
    case TYPE_KIND_PAIR(TYPE_DATETIME, TYPE_DATETIME):
    case TYPE_KIND_PAIR(TYPE_INTERVAL, TYPE_INTERVAL):
    case TYPE_KIND_PAIR(TYPE_ENUM, TYPE_ENUM):
    case TYPE_KIND_PAIR(TYPE_NUMERIC, TYPE_NUMERIC):
    case TYPE_KIND_PAIR(TYPE_BIGNUMERIC, TYPE_BIGNUMERIC):
    case TYPE_KIND_PAIR(TYPE_FLOAT, TYPE_FLOAT):
    case TYPE_KIND_PAIR(TYPE_DOUBLE, TYPE_DOUBLE):
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_UINT64):
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_INT64):
    case TYPE_KIND_PAIR(TYPE_GRAPH_ELEMENT, TYPE_GRAPH_ELEMENT):
    case TYPE_KIND_PAIR(TYPE_GRAPH_PATH, TYPE_GRAPH_PATH):
    case TYPE_KIND_PAIR(TYPE_UUID, TYPE_UUID):
    case TYPE_KIND_PAIR(TYPE_JSON, TYPE_JSON):
      return true;
    case TYPE_KIND_PAIR(TYPE_EXTENDED, TYPE_EXTENDED):
      return type1->SupportsEquality() && type1->Equivalent(type2);
    case TYPE_KIND_PAIR(TYPE_STRUCT, TYPE_STRUCT): {
      const StructType* struct_type1 = type1->AsStruct();
      const StructType* struct_type2 = type2->AsStruct();
      if (struct_type1->num_fields() != struct_type2->num_fields()) {
        return false;
      }
      for (int i = 0; i < struct_type1->num_fields(); ++i) {
        if (!TypesSupportSqlEquals(struct_type1->field(i).type,
                                   struct_type2->field(i).type)) {
          return false;
        }
      }
      return true;
    }
    case TYPE_KIND_PAIR(TYPE_ARRAY, TYPE_ARRAY):
      return TypesSupportSqlEquals(type1->AsArray()->element_type(),
                                   type2->AsArray()->element_type());
    case TYPE_KIND_PAIR(TYPE_RANGE, TYPE_RANGE):
      ABSL_DCHECK(TypesSupportSqlEquals(type1->AsRange()->element_type(),
                                   type2->AsRange()->element_type()));
      return true;
    default:
      return false;
  }
}

// This method is unit tested indirectly through the reference implementation
// compliance and unit tests.
Value Value::SqlEquals(const Value& that) const {
  if (!TypesSupportSqlEquals(type(), that.type())) return Value();

  if (is_null() || that.is_null()) return values::NullBool();

  switch (TYPE_KIND_PAIR(type_kind(), that.type_kind())) {
    case TYPE_KIND_PAIR(TYPE_INT32, TYPE_INT32):
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_INT64):
    case TYPE_KIND_PAIR(TYPE_UINT32, TYPE_UINT32):
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_UINT64):
    case TYPE_KIND_PAIR(TYPE_BOOL, TYPE_BOOL):
    case TYPE_KIND_PAIR(TYPE_STRING, TYPE_STRING):
    case TYPE_KIND_PAIR(TYPE_BYTES, TYPE_BYTES):
    case TYPE_KIND_PAIR(TYPE_DATE, TYPE_DATE):
    case TYPE_KIND_PAIR(TYPE_TIMESTAMP, TYPE_TIMESTAMP):
    case TYPE_KIND_PAIR(TYPE_TIME, TYPE_TIME):
    case TYPE_KIND_PAIR(TYPE_DATETIME, TYPE_DATETIME):
    case TYPE_KIND_PAIR(TYPE_INTERVAL, TYPE_INTERVAL):
    case TYPE_KIND_PAIR(TYPE_ENUM, TYPE_ENUM):
    case TYPE_KIND_PAIR(TYPE_NUMERIC, TYPE_NUMERIC):
    case TYPE_KIND_PAIR(TYPE_BIGNUMERIC, TYPE_BIGNUMERIC):
    case TYPE_KIND_PAIR(TYPE_UUID, TYPE_UUID):
    case TYPE_KIND_PAIR(TYPE_JSON, TYPE_JSON):
    case TYPE_KIND_PAIR(TYPE_EXTENDED, TYPE_EXTENDED):
      return Value::Bool(Equals(that));
    case TYPE_KIND_PAIR(TYPE_GRAPH_ELEMENT, TYPE_GRAPH_ELEMENT): {
      const auto* this_element = this->graph_element_value();
      const auto* that_element = that.graph_element_value();
      if (this_element->element_kind() != that_element->element_kind()) {
        return values::False();
      }
      bool eq = this_element->GetIdentifier() == that_element->GetIdentifier();
      if (this_element->IsEdge()) {
        eq = eq &&
             this_element->GetSourceNodeIdentifier() ==
                 that_element->GetSourceNodeIdentifier() &&
             this_element->GetDestNodeIdentifier() ==
                 that_element->GetDestNodeIdentifier();
      }
      return Value::Bool(eq);
    }
    case TYPE_KIND_PAIR(TYPE_GRAPH_PATH, TYPE_GRAPH_PATH): {
      // The components of a path cannot be null.
      bool equals = Equals(that);
      return Value::Bool(equals);
    }
    case TYPE_KIND_PAIR(TYPE_STRUCT, TYPE_STRUCT): {
      if (num_fields() != that.num_fields()) {
        return values::False();
      }
      bool saw_null_field_comparison = false;
      for (int i = 0; i < num_fields(); ++i) {
        const Value result = field(i).SqlEquals(that.field(i));
        if (!result.is_valid()) {
          return Value();
        }
        if (result.is_null()) {
          // We had a field comparison that was null. Remember that. We still
          // have to continue looking at the remaining fields rather than return
          // because we might have a later field that compares false which would
          // make the entire comparison false.
          saw_null_field_comparison = true;
        } else if (!result.bool_value()) {
          return values::False();
        }
      }
      if (saw_null_field_comparison) {
        return values::NullBool();
      }
      return values::True();
    }

    case TYPE_KIND_PAIR(TYPE_FLOAT, TYPE_FLOAT):
      // false if NaN
      return Value::Bool(float_value() == that.float_value());
    case TYPE_KIND_PAIR(TYPE_DOUBLE, TYPE_DOUBLE):
      // false if NaN
      return Value::Bool(double_value() == that.double_value());
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_UINT64):
      return Value::Bool(
          functions::Compare64(int64_value(), that.uint64_value()) == 0);
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_INT64):
      return Value::Bool(
          functions::Compare64(that.int64_value(), uint64_value()) == 0);
    case TYPE_KIND_PAIR(TYPE_ARRAY, TYPE_ARRAY): {
      if (num_elements() != that.num_elements()) {
        return values::False();
      }
      bool saw_null_element_comparison = false;
      for (int i = 0; i < num_elements(); ++i) {
        Value result = element(i).SqlEquals(that.element(i));
        if (result.is_null()) {
          // Keeps track of whether there was null in comparison but do not
          // return early as there might be a later element that compares false
          // which would make the entire comparison false.
          saw_null_element_comparison = true;
        } else if (!result.bool_value()) {
          return values::False();
        }
      }
      if (saw_null_element_comparison) {
        return Value::NullBool();
      }
      return values::True();
    }
    case TYPE_KIND_PAIR(TYPE_RANGE, TYPE_RANGE):
      return Value::Bool(Equals(that));
    default:
      return Value();
  }
}

size_t Value::HashCode() const { return absl::Hash<Value>()(*this); }

bool Value::LessThan(const Value& that) const {
  if (!type()->Equivalent(that.type())) {
    return false;  // Behavior is undefined, so just return false.
  }

  // Note that because we don't check type for nulls, this means we may return
  // true when the type of 'this' and 'that' is different and one of them is
  // null. E.g. when comparing two enums, if 'this' is null and 'that' is not
  // null, we return true even though they may be incompatible.
  if (is_null() && !that.is_null()) return true;
  if (that.is_null()) return false;

  return type()->ValueContentLess(GetContent(), that.GetContent(), that.type());
}

static bool TypesSupportSqlLessThan(const Type* type1, const Type* type2) {
  switch (TYPE_KIND_PAIR(type1->kind(), type2->kind())) {
    case TYPE_KIND_PAIR(TYPE_INT32, TYPE_INT32):
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_INT64):
    case TYPE_KIND_PAIR(TYPE_UINT32, TYPE_UINT32):
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_UINT64):
    case TYPE_KIND_PAIR(TYPE_BOOL, TYPE_BOOL):
    case TYPE_KIND_PAIR(TYPE_STRING, TYPE_STRING):
    case TYPE_KIND_PAIR(TYPE_BYTES, TYPE_BYTES):
    case TYPE_KIND_PAIR(TYPE_DATE, TYPE_DATE):
    case TYPE_KIND_PAIR(TYPE_TIMESTAMP, TYPE_TIMESTAMP):
    case TYPE_KIND_PAIR(TYPE_TIME, TYPE_TIME):
    case TYPE_KIND_PAIR(TYPE_DATETIME, TYPE_DATETIME):
    case TYPE_KIND_PAIR(TYPE_INTERVAL, TYPE_INTERVAL):
    case TYPE_KIND_PAIR(TYPE_ENUM, TYPE_ENUM):
    case TYPE_KIND_PAIR(TYPE_NUMERIC, TYPE_NUMERIC):
    case TYPE_KIND_PAIR(TYPE_BIGNUMERIC, TYPE_BIGNUMERIC):
    case TYPE_KIND_PAIR(TYPE_FLOAT, TYPE_FLOAT):
    case TYPE_KIND_PAIR(TYPE_DOUBLE, TYPE_DOUBLE):
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_UINT64):
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_INT64):
    case TYPE_KIND_PAIR(TYPE_UUID, TYPE_UUID):
    case TYPE_KIND_PAIR(TYPE_JSON, TYPE_JSON):
      return true;
    case TYPE_KIND_PAIR(TYPE_EXTENDED, TYPE_EXTENDED):
      return type1->SupportsEquality() && type1->Equivalent(type2);
    case TYPE_KIND_PAIR(TYPE_ARRAY, TYPE_ARRAY):
      return TypesSupportSqlLessThan(type1->AsArray()->element_type(),
                                     type2->AsArray()->element_type());
    case TYPE_KIND_PAIR(TYPE_RANGE, TYPE_RANGE):
      ABSL_DCHECK(TypesSupportSqlLessThan(type1->AsRange()->element_type(),
                                     type2->AsRange()->element_type()));
      return true;
    default:
      return false;
  }
}

// This method is unit tested indirectly through the reference implementation
// compliance and unit tests.
Value Value::SqlLessThan(const Value& that) const {
  if (!TypesSupportSqlLessThan(type(), that.type())) return Value();

  if (is_null() || that.is_null()) return values::NullBool();

  switch (TYPE_KIND_PAIR(type_kind(), that.type_kind())) {
    case TYPE_KIND_PAIR(TYPE_INT32, TYPE_INT32):
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_INT64):
    case TYPE_KIND_PAIR(TYPE_UINT32, TYPE_UINT32):
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_UINT64):
    case TYPE_KIND_PAIR(TYPE_BOOL, TYPE_BOOL):
    case TYPE_KIND_PAIR(TYPE_STRING, TYPE_STRING):
    case TYPE_KIND_PAIR(TYPE_BYTES, TYPE_BYTES):
    case TYPE_KIND_PAIR(TYPE_DATE, TYPE_DATE):
    case TYPE_KIND_PAIR(TYPE_TIMESTAMP, TYPE_TIMESTAMP):
    case TYPE_KIND_PAIR(TYPE_TIME, TYPE_TIME):
    case TYPE_KIND_PAIR(TYPE_DATETIME, TYPE_DATETIME):
    case TYPE_KIND_PAIR(TYPE_ENUM, TYPE_ENUM):
    case TYPE_KIND_PAIR(TYPE_NUMERIC, TYPE_NUMERIC):
    case TYPE_KIND_PAIR(TYPE_BIGNUMERIC, TYPE_BIGNUMERIC):
    case TYPE_KIND_PAIR(TYPE_INTERVAL, TYPE_INTERVAL):
    case TYPE_KIND_PAIR(TYPE_RANGE, TYPE_RANGE):
    case TYPE_KIND_PAIR(TYPE_UUID, TYPE_UUID):
    case TYPE_KIND_PAIR(TYPE_JSON, TYPE_JSON):
    case TYPE_KIND_PAIR(TYPE_EXTENDED, TYPE_EXTENDED):
      return Value::Bool(LessThan(that));
    case TYPE_KIND_PAIR(TYPE_FLOAT, TYPE_FLOAT):
      return Value::Bool(float_value() < that.float_value());  // false if NaN
    case TYPE_KIND_PAIR(TYPE_DOUBLE, TYPE_DOUBLE):
      return Value::Bool(double_value() < that.double_value());  // false if NaN
    case TYPE_KIND_PAIR(TYPE_INT64, TYPE_UINT64):
      return Value::Bool(
          functions::Compare64(int64_value(), that.uint64_value()) < 0);
    case TYPE_KIND_PAIR(TYPE_UINT64, TYPE_INT64):
      return Value::Bool(
          functions::Compare64(that.int64_value(), uint64_value()) > 0);

    case TYPE_KIND_PAIR(TYPE_ARRAY, TYPE_ARRAY): {
      const int shorter_array_size =
          std::min(num_elements(), that.num_elements());
      // Compare array elements one by one. If we find that the first array is
      // less or greater than the second, then ignore the remaining elements and
      // return the result. If we find a NULL element, then the comparison
      // results in NULL.
      for (int i = 0; i < shorter_array_size; ++i) {
        // Evaluate if the element of the first array is less than the element
        // of the second array.
        const Value first_result = element(i).SqlLessThan(that.element(i));
        if (first_result.is_null()) {
          // If the comparison returned NULL, then return NULL.
          return Value::NullBool();
        }
        if (first_result.bool_value()) {
          return values::True();
        }

        // Evaluate if the element of the second array is less than the element
        // of the first array.
        const Value second_result = that.element(i).SqlLessThan(element(i));
        if (second_result.is_null()) {
          // If the comparison returned NULL, then return NULL. This shouldn't
          // happen since 'first_result' was not NULL, but we check anyway just
          // to be safe.
          return Value::NullBool();
        }
        if (second_result.bool_value()) {
          return values::False();
        }

        // Otherwise the array elements are not less and not greater, but may
        // not be 'equal' (e.g., if one of the elements is NaN, which always
        // compares as false).
        const Value equals_result = element(i).SqlEquals(that.element(i));
        if (equals_result.is_null()) {
          // This shouldn't happen since 'first_result' was not NULL, but we
          // check anyway just to be safe.
          return Value::NullBool();
        }
        if (!equals_result.bool_value()) {
          return values::False();
        }
      }

      // If we got here, then the first <shorter_array_size> elements are all
      // equal. So if the left array is shorter than the right array then it is
      // less.
      return Value::Bool(num_elements() < that.num_elements());
    }
    default:
      return Value();
  }
}

std::string Value::DebugString(bool verbose) const {
  if (metadata_.type_kind() == kInvalidTypeKind) {
    return "Uninitialized value";
  }
  if (!is_valid())
    return absl::StrCat("Invalid value, type_kind: ", metadata_.type_kind());
  // Note: This method previously had problems with large stack size because
  // of recursion for structs and arrays.  Using StrCat/StrAppend in particular
  // adds large stack size per argument for the AlphaNum object.

  bool add_type_prefix = verbose;
  std::string result;
  if (is_null()) {
    result = "NULL";
  } else {
    if (DoesTypeUseValueList() || DoesTypeUseValueMap() ||
        DoesTypeUseValueMeasure()) {
      add_type_prefix = false;
    }
    Type::FormatValueContentOptions options;
    options.product_mode = ProductMode::PRODUCT_INTERNAL;
    options.mode = Type::FormatValueContentOptions::Mode::kDebug;
    options.verbose = verbose;

    result = type()->FormatValueContent(GetContent(), options);
  }
  if (add_type_prefix) {
    return type()->AddCapitalizedTypePrefix(result, is_null());
  }
  return result;
}

// Format will wrap arrays and structs.
std::string Value::Format(bool print_top_level_type) const {
  return FormatInternal(
      {.force_type_at_top_level = print_top_level_type, .indent = 0});
}

// NOTE: There is a similar method in ../resolved_ast/sql_builder.cc.
//
// This is also basically the same as GetSQLLiteral below, except this adds
// CASTs and explicit type names so the exact value comes back out.
std::string Value::GetSQL(ProductMode mode, bool use_external_float32) const {
  return GetSQLInternal<false, true>(mode, use_external_float32);
}

// This is basically the same as GetSQL() above, except this doesn't add CASTs
// or explicit type names if the literal would be valid without them.
std::string Value::GetSQLLiteral(ProductMode mode,
                                 bool use_external_float32) const {
  return GetSQLInternal<true, true>(mode, use_external_float32);
}

template <bool as_literal, bool maybe_add_simple_type_prefix>
std::string Value::GetSQLInternal(ProductMode mode,
                                  bool use_external_float32) const {
  const Type* type = this->type();

  if (is_null()) {
    return as_literal
               ? "NULL"
               : absl::StrCat("CAST(NULL AS ",
                              type->TypeName(mode, use_external_float32), ")");
  }

  Type::FormatValueContentOptions options;
  options.product_mode = mode;
  options.use_external_float32 = use_external_float32;
  if (as_literal) {
    options.mode = maybe_add_simple_type_prefix
                       ? Type::FormatValueContentOptions::Mode::kSQLLiteral
                       : Type::FormatValueContentOptions::Mode::kDebug;
  } else {
    options.mode = Type::FormatValueContentOptions::Mode::kSQLExpression;
  }

  return type->FormatValueContent(GetContent(), options);
}

std::string RepeatString(absl::string_view text, int times) {
  ABSL_CHECK_GE(times, 0);
  std::string result;
  result.reserve(text.size() * times);
  for (int i = 0; i < times; ++i) {
    result.append(text);
  }
  return result;
}

// Character used to indent.
const char* kIndentChar = " ";

// A magic number of columns that we try to fit formatted values within.
const int kWrapCols = 78;
// A maximum length for a formatted element in a single line (NONE) formatting.
// If any element exceeds this in AUTO mode it will trigger an INDENT style
// wrap.
const int kMaxSingleLineElement = 20;
// A maximum number of columns accepted for a COLUMN style indent in AUTO mode.
// Any formatting that would cause a deeper indent becomes INDENT.
const int kMaxColumnIndent = 15;

std::string Indent(int columns) { return RepeatString(kIndentChar, columns); }

// Returns the length of the longest line in a multi line formatted string.
size_t LongestLine(absl::string_view formatted) {
  int64_t longest = 0;
  for (absl::string_view line : absl::StrSplit(formatted, '\n')) {
    int64_t line_length = line.size();
    longest = std::max(longest, line_length);
  }
  return longest;
}

// Add to the indentation of all lines other than the first.
std::string ReIndentTail(absl::string_view formatted, int added_depth) {
  std::vector<std::string> lines =
      absl::StrSplit(formatted, "\n  ", absl::SkipWhitespace());
  return absl::StrJoin(lines, absl::StrCat("\n", Indent(added_depth)));
}

enum class WrapStyle {
  // [a, b, c]
  NONE,
  // [
  //   a,
  //   b,
  //   c
  // ]
  INDENT,
  // [a,
  //  b,
  //  c]
  COLUMN,
  // Formatter picks a format to fit within a column limit.
  AUTO,
};

// Finds the first instance of "$0" inside a substitution template.
static int FindSubstitutionMarker(absl::string_view block_template) {
  int marker_index = 0;
  while (marker_index < static_cast<int64_t>(block_template.size()) - 1) {
    if (block_template[marker_index] == '$') {
      // Break upon finding "$0"
      if (block_template[marker_index + 1] == '0') {
        return marker_index;
        // Skip an extra character upon seeing "$$", since this is the escape
        // sequence for a single '$'.
      } else if (block_template[marker_index + 1] == '$') {
        ++marker_index;
      }
    }
    ++marker_index;
  }
  return marker_index;
}

std::string FormatBlock(absl::string_view block_template,
                        const std::vector<std::string>& elements,
                        absl::string_view separator, int block_indent_cols,
                        WrapStyle wrap_style, int indent_step) {
  // The length of the template string preceding the substitution marker.
  // This prefix may or may not have line returns.
  int prefix_len = FindSubstitutionMarker(block_template);
  // The position of the last line-return before the substitution marker
  // or minus one.
  int last_line_start = block_template.rfind('\n', prefix_len) + 1;
  // The column at which "COLUMN" style will wrap.
  int column_wrap_len = block_indent_cols + prefix_len - last_line_start;
  // The column at which "INDENT" style will wrap.
  int indent_wrap_len = block_indent_cols + indent_step;

  if (wrap_style == WrapStyle::AUTO) {
    int count = elements.size();
    size_t sum_length = 0;
    size_t max_length = 0;
    bool multi_line_child = false;
    for (const std::string& elem : elements) {
      int line_return_pos = elem.find('\n');
      if (line_return_pos == std::string::npos) {
        sum_length += elem.size();
        max_length = std::max(max_length, elem.size());
      } else {
        multi_line_child = true;
        max_length = std::max(max_length, LongestLine(elem));
      }
    }
    int sep_size = separator.size() + 1;
    // Length of formatting to a single line.
    int single_line_length =
        (prefix_len - last_line_start) + sum_length + ((count - 1) * sep_size);
    if (count == 0) {
      wrap_style = WrapStyle::NONE;
    } else if (!multi_line_child && count > 1 &&
               max_length > kMaxSingleLineElement) {
      wrap_style = WrapStyle::INDENT;
    } else if (!multi_line_child && single_line_length < kWrapCols) {
      wrap_style = WrapStyle::NONE;
    } else if ((prefix_len - last_line_start) <= kMaxColumnIndent &&
               (column_wrap_len + max_length) < kWrapCols) {
      wrap_style = WrapStyle::COLUMN;
    } else {
      wrap_style = WrapStyle::INDENT;
    }
  }

  std::string pre = "";
  std::string sep = absl::StrCat(separator, " ");
  std::string post = "";
  std::vector<std::string> indented_elements;
  switch (wrap_style) {
    case WrapStyle::NONE:
    case WrapStyle::AUTO:
      break;
    case WrapStyle::INDENT:
      pre = absl::StrCat("\n", Indent(indent_wrap_len));
      sep = absl::StrCat(separator, "\n", Indent(indent_wrap_len));
      post = absl::StrCat("\n", Indent(block_indent_cols));
      break;
    case WrapStyle::COLUMN: {
      sep = absl::StrCat(",\n", Indent(column_wrap_len));
      // Multi-line elements were formatted assuming they are at
      // block_indent_cols. They are actually at column_wrap_len.  Fix.
      int additional_indent = column_wrap_len - indent_wrap_len + indent_step;
      for (const std::string& elem : elements) {
        indented_elements.push_back(ReIndentTail(elem, additional_indent));
      }
      break;
    }
  }
  const std::vector<std::string>& parts =
      wrap_style == WrapStyle::COLUMN ? indented_elements : elements;
  return Substitute(block_template,
                    absl::StrCat(pre, absl::StrJoin(parts, sep), post));
}

enum class ArrayElemFormat {
  ALL,
  NONE,
  FIRST_LEVEL_ONLY,
};

const int kArrayIndent = 6;   // Length of "ARRAY<"
const int kStructIndent = 7;  // Length of "STRUCT<"

// Helps FormatInternal print value types. This is a specific format for
// types, so we choose not to add this as a generally used method on Type.
std::string FormatType(const Type* type, ArrayElemFormat elem_format,
                       int indent_cols, int indent_step) {
  ArrayElemFormat continue_elem_format =
      elem_format == ArrayElemFormat::FIRST_LEVEL_ONLY ? ArrayElemFormat::NONE
                                                       : elem_format;
  if (type->IsArray()) {
    std::string element_type =
        elem_format != ArrayElemFormat::NONE
            ? FormatType(type->AsArray()->element_type(), continue_elem_format,
                         indent_cols + kArrayIndent, indent_step)
            : "";
    return Substitute("ARRAY<$0>", element_type);
  } else if (type->IsStruct()) {
    const StructType* struct_type = type->AsStruct();
    std::vector<std::string> fields(struct_type->num_fields());
    for (int i = 0; i < struct_type->num_fields(); ++i) {
      const StructType::StructField& field = struct_type->field(i);
      fields[i] = FormatType(field.type, continue_elem_format,
                             indent_cols + kStructIndent, indent_step);
      if (!field.name.empty()) {
        fields[i] = Substitute("$0 $1", field.name, fields[i]);
      }
    }
    return FormatBlock("STRUCT<$0>", fields, ",", indent_cols, WrapStyle::AUTO,
                       indent_step);
  } else if (type->IsProto()) {
    ABSL_CHECK(type->AsProto()->descriptor() != nullptr);
    return Substitute("PROTO<$0>", type->AsProto()->descriptor()->full_name());
  } else if (type->IsEnum()) {
    return Substitute("ENUM<$0>",
                      type->AsEnum()->enum_descriptor()->full_name());
  } else {
    return type->DebugString(type /* verbose */);
  }
}

std::string Value::FormatInternal(
    Type::FormatValueContentOptions options) const {
  if (type()->IsArray()) {
    // If the array is null or empty, print the whole type because there
    // are no printed elements that provide type information of nested arrays.
    // If there are values, FormatType elides element types for nested arrays.
    // This is to keep types as readable as possible.
    ArrayElemFormat elem_style = (is_null() || elements().empty())
                                     ? ArrayElemFormat::ALL
                                     : ArrayElemFormat::FIRST_LEVEL_ONLY;
    std::string type_string =
        FormatType(type(), elem_style, options.indent,
                   Type::FormatValueContentOptions::kIndentStep);
    if (is_null()) {
      return absl::StrCat(type_string, "(NULL)");
    }
    std::vector<std::string> element_strings(elements().size());
    for (int i = 0; i < elements().size(); ++i) {
      element_strings[i] =
          elements()[i].FormatInternal(options.IncreaseIndent());
    }
    // Sanitize any '$' characters before creating substitution template. "$$"
    // is replaced by "$" in the output from absl::Substitute.
    std::string sanitized_type_string =
        absl::StrReplaceAll(type_string, {{"$", "$$"}});
    std::string array_orderedness = "";
    if (options.include_array_ordereness && elements().size() > 1) {
      if (order_kind() == kPreservesOrder) {
        array_orderedness = "known order:";
      } else {
        array_orderedness = "unknown order:";
      }
    }
    std::string templ =
        absl::StrCat(sanitized_type_string, "[", array_orderedness, "$0]");
    // Force a wrap after the type if the type consumes multiple lines and
    // there is more than one element (or one element over multiple lines).
    if (absl::StrContains(type_string, '\n') &&
        (elements().size() > 1 ||
         (!elements().empty() &&
          absl::StrContains(element_strings[0], '\n')))) {
      templ = absl::StrCat(sanitized_type_string, "\n", Indent(options.indent),
                           "[", array_orderedness, "$0]");
    }
    return FormatBlock(templ, element_strings, ",", options.indent,
                       WrapStyle::AUTO,
                       Type::FormatValueContentOptions::kIndentStep);
  } else if (type()->IsStruct()) {
    std::string type_string =
        options.force_type_at_top_level
            ? FormatType(type(), ArrayElemFormat::NONE, options.indent,
                         Type::FormatValueContentOptions::kIndentStep)
            : "";
    if (is_null()) {
      return options.force_type_at_top_level
                 ? Substitute("$0(NULL)", type_string)
                 : "NULL";
    }
    const StructType* struct_type = type()->AsStruct();
    std::vector<std::string> field_strings(struct_type->num_fields());
    for (int i = 0; i < struct_type->num_fields(); i++) {
      field_strings[i] = fields()[i].FormatInternal(options.IncreaseIndent());
    }
    // Sanitize any '$' characters before creating substitution template. "$$"
    // is replaced by "$" in the output from absl::Substitute.
    std::string templ =
        absl::StrCat(absl::StrReplaceAll(type_string, {{"$", "$$"}}), "{$0}");
    return FormatBlock(templ, field_strings, ",", options.indent,
                       WrapStyle::AUTO,
                       Type::FormatValueContentOptions::kIndentStep);
  } else if (type()->IsProto()) {
    std::string type_string =
        options.force_type_at_top_level
            ? FormatType(type(), ArrayElemFormat::NONE, options.indent,
                         Type::FormatValueContentOptions::kIndentStep)
            : "";
    if (is_null()) {
      return options.force_type_at_top_level
                 ? Substitute("$0(NULL)", type_string)
                 : "NULL";
    }
    google::protobuf::DynamicMessageFactory message_factory;
    std::unique_ptr<google::protobuf::Message> m(this->ToMessage(&message_factory));
    // Split and re-wrap the proto debug string to achieve proper indentation.
    std::vector<std::string> field_strings = absl::StrSplit(
         m->DebugString(),
        '\n', absl::SkipWhitespace());
    bool wraps = field_strings.size() > 1;
    // We don't need to sanitize the type string here since proto field names
    // cannot contain '$' characters.
    return FormatBlock(absl::StrCat(type_string, "{$0}"), field_strings, "",
                       options.indent,
                       wraps ? WrapStyle::INDENT : WrapStyle::NONE,
                       Type::FormatValueContentOptions::kIndentStep);
  } else if (type()->IsRangeType()) {
    std::string type_string =
        options.force_type_at_top_level
            ? FormatType(type(), ArrayElemFormat::NONE, options.indent,
                         Type::FormatValueContentOptions::kIndentStep)
            : "";
    if (is_null()) {
      return options.force_type_at_top_level
                 ? Substitute("$0(NULL)", type_string)
                 : "NULL";
    }
    std::vector<std::string> boundaries_strings;
    boundaries_strings.push_back(
        start().FormatInternal(options.IncreaseIndent()));
    boundaries_strings.push_back(
        end().FormatInternal(options.IncreaseIndent()));
    // Sanitize any '$' characters before creating substitution template. "$$"
    // is replaced by "$" in the output from absl::Substitute.
    std::string templ =
        absl::StrCat(absl::StrReplaceAll(type_string, {{"$", "$$"}}), "[$0)");
    return FormatBlock(templ, boundaries_strings, ",", options.indent,
                       WrapStyle::AUTO,
                       Type::FormatValueContentOptions::kIndentStep);
  } else if (type()->IsTokenList() && !is_null()) {
    // Empty tokenlists are rendered as "<empty>". Following the ARRAY and
    // STRUCT pattern, we'll print the type in this case regardless of whether
    // force_type_at_top_level is true.
    std::vector<std::string> lines =
        SimpleType::FormatTokenLines(GetContent(), options);
    std::string tmpl =
        (options.force_type_at_top_level || lines.empty())
            ? type()->AddCapitalizedTypePrefix("{$0}", /*is_null=*/false)
            : "{$0}";
    if (lines.empty()) lines = {"<empty>"};
    return FormatBlock(tmpl, lines, ",", options.indent, WrapStyle::AUTO,
                       Type::FormatValueContentOptions::kIndentStep);
  } else {
    return DebugString(options.force_type_at_top_level);
  }
}

bool Value::ParseInteger(absl::string_view input, Value* value) {
  int64_t int64_value;
  uint64_t uint64_value;
  if (functions::StringToNumeric(input, &int64_value, nullptr)) {
    *value = Value::Int64(int64_value);
    return true;
  }
  // Could not parse into int64, try uint64.
  if (functions::StringToNumeric(input, &uint64_value, nullptr)) {
    *value = Value::Uint64(uint64_value);
    return true;
  }
  return false;
}

// -------------------------------------------------------
// Value constructors
// -------------------------------------------------------

namespace values {

Value Int32Array(absl::Span<const int32_t> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Int32(v));
  }
  return Value::Array(Int32ArrayType(), value_vector);
}

Value Int64Array(absl::Span<const int64_t> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Int64(v));
  }
  return Value::Array(Int64ArrayType(), value_vector);
}

Value Uint32Array(absl::Span<const uint32_t> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Uint32(v));
  }
  return Value::Array(Uint32ArrayType(), value_vector);
}

Value Uint64Array(absl::Span<const uint64_t> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Uint64(v));
  }
  return Value::Array(Uint64ArrayType(), value_vector);
}

Value BoolArray(const std::vector<bool>& values) {
  std::vector<Value> value_vector;
  value_vector.reserve(values.size());
  for (auto v : values) {
    value_vector.push_back(Bool(v));
  }
  return Value::Array(BoolArrayType(), value_vector);
}

Value FloatArray(absl::Span<const float> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Float(v));
  }
  return Value::Array(FloatArrayType(), value_vector);
}

Value DoubleArray(absl::Span<const double> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Double(v));
  }
  return Value::Array(DoubleArrayType(), value_vector);
}

Value StringArray(absl::Span<const std::string> values) {
  std::vector<Value> value_vector;
  for (const std::string& v : values) {
    value_vector.push_back(String(v));
  }
  return Value::Array(StringArrayType(), value_vector);
}

Value StringArray(absl::Span<const absl::Cord* const> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(String(*v));
  }
  return Value::Array(StringArrayType(), value_vector);
}

Value BytesArray(absl::Span<const std::string> values) {
  std::vector<Value> value_vector;
  for (const std::string& v : values) {
    value_vector.push_back(Bytes(v));
  }
  return Value::Array(BytesArrayType(), value_vector);
}

Value BytesArray(absl::Span<const absl::Cord* const> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Bytes(*v));
  }
  return Value::Array(BytesArrayType(), value_vector);
}

Value NumericArray(absl::Span<const NumericValue> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Value::Numeric(v));
  }
  return Value::Array(NumericArrayType(), value_vector);
}

Value BigNumericArray(absl::Span<const BigNumericValue> values) {
  std::vector<Value> value_vector;
  for (auto v : values) {
    value_vector.push_back(Value::BigNumeric(v));
  }
  return Value::Array(BigNumericArrayType(), value_vector);
}

Value JsonArray(absl::Span<const JSONValue> values) {
  std::vector<Value> value_vector;
  for (const auto& v : values) {
    value_vector.push_back(Value::Json(JSONValue::CopyFrom(v.GetConstRef())));
  }
  return Value::Array(JsonArrayType(), value_vector);
}

Value UnvalidatedJsonStringArray(absl::Span<const std::string> values) {
  std::vector<Value> value_vector;
  for (const auto& v : values) {
    value_vector.push_back(Value::UnvalidatedJsonString(v));
  }
  return Value::Array(JsonArrayType(), value_vector);
}

Value TimestampArray(absl::Span<const absl::Time> values) {
  std::vector<Value> value_vector;
  for (const auto& v : values) {
    value_vector.push_back(Value::Timestamp(v));
  }
  return Value::Array(TimestampArrayType(), value_vector);
}

Value TimestampArray(absl::Span<const TimestampPicosValue> values) {
  std::vector<Value> value_vector;
  for (const auto& v : values) {
    value_vector.push_back(Value::Timestamp(v));
  }
  return Value::Array(TimestampArrayType(), value_vector);
}

Value DateArray(absl::Span<const absl::CivilDay> values) {
  std::vector<Value> value_vector;
  value_vector.reserve(values.size());
  for (const auto& v : values) {
    value_vector.push_back(Date(v));
  }
  return Value::Array(DateArrayType(), value_vector);
}

}  // namespace values

absl::Status Value::Serialize(ValueProto* value_proto) const {
  value_proto->Clear();
  if (is_null()) {
    return absl::OkStatus();
  }
  return type()->SerializeValueContent(GetContent(), value_proto);
}

absl::StatusOr<Value> Value::Deserialize(const ValueProto& value_proto,
                                         const Type* type) {
  if (value_proto.value_case() == ValueProto::VALUE_NOT_SET) {
    return Null(type);
  }
  switch (type->kind()) {
    case TYPE_ARRAY: {
      if (!value_proto.has_array_value()) {
        return type->TypeMismatchError(value_proto);
      }
      std::vector<Value> elements;
      elements.reserve(value_proto.array_value().element().size());
      for (const auto& element : value_proto.array_value().element()) {
        auto status_or_value =
            Deserialize(element, type->AsArray()->element_type());
        GOOGLESQL_RETURN_IF_ERROR(status_or_value.status());
        elements.push_back(std::move(status_or_value.value()));
      }
      return MakeArray(type->AsArray(), std::move(elements));
    }
    case TYPE_STRUCT: {
      if (!value_proto.has_struct_value()) {
        return type->TypeMismatchError(value_proto);
      }
      const StructType* struct_type = type->AsStruct();
      if (value_proto.struct_value().field_size() !=
          struct_type->num_fields()) {
        return absl::Status(
            absl::StatusCode::kInternal,
            absl::StrCat("Type mismatch for struct. Type has ",
                         struct_type->num_fields(), " fields, but proto has ",
                         value_proto.struct_value().field_size(), " fields."));
      }
      std::vector<Value> fields;
      for (int i = 0; i < struct_type->num_fields(); i++) {
        auto status_or_value = Deserialize(value_proto.struct_value().field(i),
                                           struct_type->field(i).type);
        GOOGLESQL_RETURN_IF_ERROR(status_or_value.status());
        fields.emplace_back(std::move(*status_or_value));
      }
      return Struct(struct_type, fields);
    }
    case TYPE_RANGE: {
      if (!value_proto.has_range_value() ||
          !value_proto.range_value().has_start() ||
          !value_proto.range_value().has_end()) {
        return type->TypeMismatchError(value_proto);
      }
      const Type* element_type = type->AsRange()->element_type();
      GOOGLESQL_ASSIGN_OR_RETURN(
          const Value& start,
          Deserialize(value_proto.range_value().start(), element_type));
      GOOGLESQL_ASSIGN_OR_RETURN(
          const Value& end,
          Deserialize(value_proto.range_value().end(), element_type));
      return MakeRange(start, end);
    }
    case TYPE_MAP: {
      if (!value_proto.has_map_value()) {
        return type->TypeMismatchError(value_proto);
      }
      std::vector<std::pair<Value, Value>> map_entries;
      map_entries.reserve(value_proto.map_value().entry_size());
      for (const auto& entry : value_proto.map_value().entry()) {
        if (!entry.has_key() || !entry.has_value()) {
          return MakeEvalError()
                 << "Invalid MapEntry could not be deserialized: entry must "
                    "set both key and value.";
        }
        GOOGLESQL_ASSIGN_OR_RETURN(auto deserialized_key,
                         Deserialize(entry.key(), type->AsMap()->key_type()));
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto deserialized_value,
            Deserialize(entry.value(), type->AsMap()->value_type()));
        map_entries.push_back(std::make_pair(std::move(deserialized_key),
                                             std::move(deserialized_value)));
      }
      return MakeMap(type, std::move(map_entries));
    }
    // TODO: b/365163099 - The cases above are tech debt, and should be moved
    // into their respective DeserializeValueContent implementations.
    default: {
      ValueContent content;
      GOOGLESQL_RETURN_IF_ERROR(type->DeserializeValueContent(value_proto, &content));

      Value result(type);
      result.SetContent(content);
      return result;
    }
  }
}

ValueContent Value::GetContent() const {
  ABSL_DCHECK(has_content());
  // If type is less than 64bit, the padding bytes of the union uninitialized.
  // The first byte must be initialized in any case.
  // Suppress msan check for the potentially uninitialized bytes.
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(
      reinterpret_cast<const char*>(&int64_value_) + 1,
      sizeof(int64_value_) - 1);
  return ValueContent(int64_value_, metadata_.can_store_value_extended_content()
                                        ? metadata_.value_extended_content()
                                        : 0);
}

void Value::SetContent(const ValueContent& content) {
  ABSL_DCHECK(metadata_.is_valid());

  int64_value_ = content.content_;
  metadata_ = metadata_.has_type_pointer()
                  ? Metadata(metadata_.type(), /*is_null=*/false,
                             metadata_.preserves_order())
                  : Metadata(metadata_.type_kind(), /*is_null=*/false,
                             metadata_.preserves_order(),
                             content.simple_type_extended_content_);
}

Value::Metadata::Content* Value::Metadata::content() {
  static_assert(sizeof(Content) == sizeof(int64_t));
  return reinterpret_cast<Content*>(&data_);
}

const Value::Metadata::Content* Value::Metadata::content() const {
  return reinterpret_cast<const Content*>(&data_);
}

const Type* Value::Metadata::type() const {
  if (content()->has_type_pointer()) return content()->type();
  return types::TypeFromSimpleTypeKind(
      static_cast<TypeKind>(content()->kind()));
}

TypeKind Value::Metadata::type_kind() const {
  if (content()->has_type_pointer()) return content()->type()->kind();
  return static_cast<TypeKind>(content()->kind());
}

bool Value::Metadata::is_null() const { return content()->is_null(); }

bool Value::Metadata::preserves_order() const {
  return content()->preserves_order();
}

bool Value::Metadata::has_type_pointer() const {
  return content()->has_type_pointer();
}

bool Value::Metadata::can_store_value_extended_content() const {
  return !has_type_pointer();
}

int32_t Value::Metadata::value_extended_content() const {
  ABSL_CHECK(can_store_value_extended_content());
  return content()->value_extended_content();
}

bool Value::Metadata::is_valid() const {
  if (content()->has_type_pointer()) return true;
  return content()->kind() > 0;
}

Value::Metadata::Metadata(const Type* type, bool is_null,
                          bool preserves_order) {
  *content() = Content(type, is_null, preserves_order);
  ABSL_DCHECK(content()->has_type_pointer());
  ABSL_DCHECK(content()->type() == type);
  ABSL_DCHECK(content()->preserves_order() == preserves_order);
  ABSL_DCHECK(content()->is_null() == is_null);
}

Value::TypedList::~TypedList() {
}

Value::TypedMap::TypedMap(googlesql_base::MapView<Value, Value> values)
    : map_(values.begin(), values.end()) {}
Value::TypedMap::TypedMap(const std::vector<std::pair<Value, Value>>& values)
    : map_(values.begin(), values.end()) {}

const ValueMap& Value::TypedMap::entries() const { return map_; }

uint64_t Value::TypedMap::physical_byte_size() const {
  uint64_t size = sizeof(TypedMap);
  for (const auto& entry : map_) {
    size +=
        (entry.first.physical_byte_size() + entry.second.physical_byte_size());
  }
  return size;
}

int64_t Value::TypedMap::num_elements() const { return map_.size(); }

std::optional<internal::NullableValueContent>
Value::TypedMap::GetContentMapValueByKey(
    const internal::NullableValueContent& search_key, const Type* key_type,
    const ValueEqualityCheckOptions& options) const {
  // TODO: b/343467677 - Improve efficiency by using find() when possible.
  // `absl::c_find_if` is an O(n) lookup, but this could be improved to
  // O(log(n)) in many cases using the more efficient `btree_map::find()`, if
  // we can ensure that the btree_map's comparator will match the result of a
  // ValueEqualityCheckOptions-aware comparison. This is generally true, with
  // special considerations for certain cases:
  // - If any INTERVAL type is nested in the MAP key,
  //   options.interval_compare_mode must be kAllPartsEqual.
  // - If any floating point type is nested in the MAP key,
  //   options.float_margin must result in an exact equality comparison.
  // - If any ARRAY type is nested in the MAP key, comparison must not include
  //   any unordered array bags: options.deep_order_spec must be nullptr, or
  //   all nodes in the spec must have ignores_order=false.
  // - If any STRING with collation is present in key or value (note:
  //   collation is currently not allowed in MAP. b/323931806)
  // - options.reason must be nullptr.
  auto it = absl::c_find_if(map_, [&](const auto& kv) {
    if (kv.first.is_null() != search_key.is_null()) {
      return false;
    }
    if (kv.first.is_null() && search_key.is_null()) {
      return true;
    }

    return key_type->ValueContentEquals(kv.first.GetContent(),
                                        search_key.value_content(), options);
  });

  if (it == map_.end()) {
    return std::nullopt;
  }

  const Value& val = it->second;
  return val.is_null() ? internal::NullableValueContent()
                       : internal::NullableValueContent(val.GetContent());
}

// An iterator-like class implementing a forward iterator-like interface for
// traversing a TypedMap and returning pairs of NullableValueContent
// representing the key and value of the map. This cannot conform exactly to a
// standard C++ iterator interface due to ValueContent indirection.
class Value::TypedMap::Iterator : public IteratorImpl {
  friend class Value::TypedMap;

 public:
  ElementType& Deref() override {
    auto& [key, value] = *iter_;
    pair_ = ElementType{
        key.is_null() ? internal::NullableValueContent()
                      : internal::NullableValueContent(key.GetContent()),
        value.is_null() ? internal::NullableValueContent()
                        : internal::NullableValueContent(value.GetContent())};
    return pair_;
  }

  std::unique_ptr<IteratorImpl> Copy() override {
    return absl::WrapUnique(new Iterator(iter_));
  }

 private:
  explicit Iterator(ValueMap::const_iterator iter) : iter_(iter) {}

  void Increment() override { iter_++; }

  bool Equals(
      const internal::ValueContentMap::IteratorImpl& other) const override {
    const Iterator& other_ref = static_cast<const Iterator&>(other);
    return this->iter_ == other_ref.iter_;
  }

  ValueMap::const_iterator iter_;
  ElementType pair_;
};

std::unique_ptr<internal::ValueContentMap::IteratorImpl>
Value::TypedMap::begin_internal() const {
  return absl::WrapUnique(new Iterator(map_.begin()));
}
std::unique_ptr<internal::ValueContentMap::IteratorImpl>
Value::TypedMap::end_internal() const {
  return absl::WrapUnique(new Iterator(map_.end()));
}

Value::TypedMap::~TypedMap() {
}

absl::StatusOr<std::unique_ptr<Value::TypedMeasure>>
Value::TypedMeasure::Create(Value captured_values_as_struct,
                            std::vector<int> key_indices,
                            const LanguageOptions& language_options) {
  // Lambda to validate `captured_values_as_struct`.
  auto validate_captured_values = [](const Value& captured_values_as_struct) {
    if (!captured_values_as_struct.is_valid() ||
        captured_values_as_struct.is_null() ||
        !captured_values_as_struct.type()->IsStruct() ||
        captured_values_as_struct.type()->AsStruct()->num_fields() <= 0) {
      return absl::InvalidArgumentError(
          "Measure must capture a non-null STRUCT-typed value with at least "
          "one field");
    }
    absl::flat_hash_set<std::string> field_names;
    for (const StructField& field :
         captured_values_as_struct.type()->AsStruct()->fields()) {
      if (field.name.empty()) {
        return absl::InvalidArgumentError(
            "Captured measure value must contain non-empty field names");
      }
      if (!field_names.insert(field.name).second) {
        return absl::InvalidArgumentError(
            "Captured measure value must contain unique field names");
      }
    }
    return absl::OkStatus();
  };
  // Lambda to validate `key_indices`.
  auto validate_key_indices =
      [&captured_values_as_struct,
       &language_options](absl::Span<const int> key_indices) -> absl::Status {
    if (key_indices.empty()) {
      return absl::InvalidArgumentError(
          "Measure value creation requires a non-empty list of key indices");
    }
    const StructType* captured_values_struct_type =
        captured_values_as_struct.type()->AsStruct();
    for (int key_index : key_indices) {
      if (key_index < 0 ||
          key_index >= captured_values_struct_type->num_fields()) {
        return absl::InvalidArgumentError(
            "Key index for measure value creation is invalid");
      }
      const Type* key_type = captured_values_struct_type->field(key_index).type;
      if (!key_type->SupportsGrouping(language_options)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Key type does not support grouping: ",
                         key_type->ShortTypeName(PRODUCT_INTERNAL)));
      }
    }
    return absl::OkStatus();
  };

  GOOGLESQL_RETURN_IF_ERROR(validate_captured_values(captured_values_as_struct));
  GOOGLESQL_RETURN_IF_ERROR(validate_key_indices(key_indices));
  return absl::WrapUnique(new TypedMeasure(std::move(captured_values_as_struct),
                                           std::move(key_indices)));
}

Value::TypedMeasure::TypedMeasure(Value captured_values_as_struct,
                                  std::vector<int> key_indices)
    : captured_values_as_struct_(std::move(captured_values_as_struct)),
      key_indices_(std::move(key_indices)) {}

uint64_t Value::TypedMeasure::physical_byte_size() const {
  return sizeof(TypedMeasure) +
         captured_values_as_struct_.physical_byte_size() +
         (key_indices_.size() * sizeof(decltype(key_indices_)::value_type));
};

const internal::ValueContentOrderedList*
Value::TypedMeasure::GetCapturedValues() const {
  return captured_values_as_struct_.container_ptr_->value();
};

const StructType* Value::TypedMeasure::GetCapturedValuesStructType() const {
  return captured_values_as_struct_.type()->AsStruct();
}

const std::vector<int>& Value::TypedMeasure::KeyIndices() const {
  return key_indices_;
};

}  // namespace googlesql
