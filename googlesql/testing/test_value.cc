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

#include "googlesql/testing/test_value.h"

#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/base/check.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

namespace test_values {

TypeFactory* static_type_factory() {
  static TypeFactory* type_factory = new TypeFactory;
  return type_factory;
}

Value Struct(absl::Span<const std::string> names,
             absl::Span<const ValueConstructor> values,
             TypeFactory* type_factory) {
  ABSL_CHECK_EQ(names.size(), values.size());
  const StructType* struct_type;
  std::vector<StructType::StructField> fields;
  type_factory = type_factory ? type_factory : static_type_factory();
  fields.reserve(names.size());
  for (int i = 0; i < names.size(); i++) {
    fields.push_back({names[i], values[i].get().type()});
  }
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(fields, &struct_type));
  return Value::Struct(struct_type, ValueConstructor::ToValues(values));
}

Value Struct(absl::Span<const std::pair<std::string, Value>> pairs,
             TypeFactory* type_factory) {
  const StructType* struct_type;
  std::vector<StructType::StructField> fields;
  std::vector<Value> values;
  type_factory = type_factory ? type_factory : static_type_factory();
  for (const auto& p : pairs) {
    fields.push_back({p.first, p.second.type()});
    values.push_back(p.second);
  }
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(fields, &struct_type));
  return Value::Struct(struct_type, values);
}

Value Array(absl::Span<const ValueConstructor> values,
            OrderPreservationKind order_kind, TypeFactory* type_factory) {
  ABSL_CHECK(!values.empty());
  std::vector<Value> value_list = ValueConstructor::ToValues(values);
  return InternalValue::ArrayChecked(
      MakeArrayType(values[0].get().type(), type_factory), order_kind,
      std::move(value_list));
}

Value StructArray(absl::Span<const std::string> names,
                  absl::Span<const std::vector<ValueConstructor>> structs,
                  OrderPreservationKind order_kind, TypeFactory* type_factory) {
  std::vector<ValueConstructor> values;
  for (const auto& s : structs) {
    values.push_back(Struct(names, s, type_factory));
  }
  return Array(values, order_kind, type_factory);
}

Value Range(ValueConstructor start, ValueConstructor end) {
  absl::StatusOr<Value> range_value = Value::MakeRange(start.get(), end.get());
  GOOGLESQL_CHECK_OK(range_value);  // Crash ok
  return *range_value;
}

Value Map(
    absl::Span<const std::pair<ValueConstructor, ValueConstructor>> elements,
    TypeFactory* type_factory) {
  ABSL_CHECK(!elements.empty());
  std::vector<std::pair<Value, Value>> elements_list;
  elements_list.reserve(elements.size());

  const Type* key_type = elements[0].first.get().type();
  const Type* value_type = elements[0].second.get().type();
  for (const auto& [key, value] : elements) {
    elements_list.push_back(std::make_pair(key.get(), value.get()));
  }

  auto map_type = MakeMapType(key_type, value_type, type_factory);
  GOOGLESQL_CHECK_OK(map_type.status());

  auto map = Value::MakeMap(*map_type, std::move(elements_list));
  GOOGLESQL_CHECK_OK(map.status());
  return *map;
}

namespace {

const GraphElementType* InferGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const std::pair<std::string, Value>> properties,
    TypeFactory* type_factory
    ,
    bool is_dynamic = false
) {
  std::vector<GraphElementType::PropertyType> property_types;
  property_types.reserve(properties.size());
  for (const auto& property : properties) {
    property_types.emplace_back(property.first, property.second.type());
  }
  return is_dynamic ? MakeDynamicGraphElementType(graph_reference, element_kind,
                                                  property_types, type_factory)
                    : MakeGraphElementType(graph_reference, element_kind,
                                           property_types, type_factory);
}

}  // namespace

absl::StatusOr<Value> GraphNode(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    absl::Span<const std::string> static_labels,
    absl::string_view element_table_name, TypeFactory* type_factory) {
  const GraphElementType* graph_element_type =
      InferGraphElementType(graph_reference, GraphElementType::kNode,
                            static_properties, type_factory);
  Value::GraphElementLabelsAndProperties labels_and_properties{
      .static_labels = {static_labels.begin(), static_labels.end()},
      .static_properties = {static_properties.begin(),
                            static_properties.end()}};
  Value::GraphNodeKeysAndElementTableNames node_keys_and_element_table_names = {
      .identifier = identifier, .element_table_name = element_table_name};
  return Value::MakeGraphNode(graph_element_type, labels_and_properties,
                              node_keys_and_element_table_names);
}

absl::StatusOr<Value> GraphEdge(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    absl::Span<const std::string> static_labels,
    absl::string_view element_table_name,
    absl::string_view source_node_identifier,
    absl::string_view dest_node_identifier, TypeFactory* type_factory) {
  const GraphElementType* graph_element_type =
      InferGraphElementType(graph_reference, GraphElementType::kEdge,
                            static_properties, type_factory);
  Value::GraphElementLabelsAndProperties labels_and_properties{
      .static_labels = {static_labels.begin(), static_labels.end()},
      .static_properties = {static_properties.begin(),
                            static_properties.end()}};
  Value::GraphEdgeKeysAndElementTableNames edge_keys_and_element_table_names = {
      .identifier = identifier,
      .element_table_name = element_table_name,
      .source = {.identifier = source_node_identifier},
      .dest = {.identifier = dest_node_identifier}};
  return Value::MakeGraphEdge(graph_element_type, labels_and_properties,
                              edge_keys_and_element_table_names);
}

Value DynamicGraphNode(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    JSONValueConstRef dynamic_properties,
    absl::Span<const std::string> static_labels,
    absl::Span<const std::string> dynamic_labels,
    absl::string_view element_table_name, TypeFactory* type_factory) {
  const GraphElementType* graph_element_type = InferGraphElementType(
      graph_reference, GraphElementType::kNode, static_properties, type_factory,
      /*is_dynamic=*/true);
  Value::GraphElementLabelsAndProperties labels_and_properties{
      .static_labels = {static_labels.begin(), static_labels.end()},
      .static_properties = {static_properties.begin(), static_properties.end()},
      .dynamic_labels = {dynamic_labels.begin(), dynamic_labels.end()},
      .dynamic_properties = std::move(dynamic_properties),
  };
  Value::GraphNodeKeysAndElementTableNames node_keys_and_element_table_names = {
      .identifier = identifier, .element_table_name = element_table_name};
  absl::StatusOr<Value> graph_element =
      Value::MakeGraphNode(graph_element_type, labels_and_properties,
                           node_keys_and_element_table_names);
  GOOGLESQL_DCHECK_OK(graph_element);
  return *graph_element;
}

Value DynamicGraphEdge(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    JSONValueConstRef dynamic_properties,
    absl::Span<const std::string> static_labels,
    absl::Span<const std::string> dynamic_labels,
    absl::string_view element_table_name,
    absl::string_view source_node_identifier,
    absl::string_view dest_node_identifier, TypeFactory* type_factory) {
  const GraphElementType* graph_element_type = InferGraphElementType(
      graph_reference, GraphElementType::kEdge, static_properties, type_factory,
      /*is_dynamic=*/true);
  Value::GraphElementLabelsAndProperties labels_and_properties{
      .static_labels = {static_labels.begin(), static_labels.end()},
      .static_properties = {static_properties.begin(), static_properties.end()},
      .dynamic_labels = {dynamic_labels.begin(), dynamic_labels.end()},
      .dynamic_properties = std::move(dynamic_properties),
  };
  Value::GraphEdgeKeysAndElementTableNames edge_keys_and_def_names = {
      .identifier = identifier,
      .element_table_name = element_table_name,
      .source = {.identifier = source_node_identifier},
      .dest = {.identifier = dest_node_identifier}};
  absl::StatusOr<Value> graph_element = Value::MakeGraphEdge(
      graph_element_type, labels_and_properties, edge_keys_and_def_names);
  GOOGLESQL_DCHECK_OK(graph_element);
  return *graph_element;
}

const ArrayType* MakeArrayType(const Type* element_type,
                               TypeFactory* type_factory) {
  const ArrayType* array_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(element_type, &array_type));
  return array_type;
}

const StructType* MakeStructType(
    absl::Span<const StructType::StructField> fields,
    TypeFactory* type_factory) {
  const StructType* struct_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      std::vector<StructType::StructField>(fields.begin(), fields.end()),
      &struct_type));
  return struct_type;
}

const ProtoType* MakeProtoType(const google::protobuf::Descriptor* descriptor,
                               TypeFactory* type_factory) {
  const ProtoType* proto_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(descriptor, &proto_type));
  return proto_type;
}

const EnumType* MakeEnumType(const google::protobuf::EnumDescriptor* descriptor,
                             TypeFactory* type_factory) {
  const EnumType* enum_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(type_factory->MakeEnumType(descriptor, &enum_type));
  return enum_type;
}

const RangeType* MakeRangeType(const Type* element_type,
                               TypeFactory* type_factory) {
  const RangeType* range_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  absl::Status status = type_factory->MakeRangeType(element_type, &range_type);
  GOOGLESQL_CHECK_OK(status);  // Crash ok
  return range_type;
}

const GraphElementType* MakeGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const GraphElementType::PropertyType> property_types,
    TypeFactory* type_factory) {
  const GraphElementType* graph_element_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(type_factory->MakeGraphElementType(
      graph_reference, element_kind, property_types, &graph_element_type));
  return graph_element_type;
}

const GraphElementType* MakeDynamicGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const GraphElementType::PropertyType> property_types,
    TypeFactory* type_factory) {
  const GraphElementType* graph_element_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(type_factory->MakeDynamicGraphElementType(
      graph_reference, element_kind, property_types, &graph_element_type));
  return graph_element_type;
}

const GraphPathType* MakeGraphPathType(const GraphElementType* node_type,
                                       const GraphElementType* edge_type,
                                       TypeFactory* type_factory) {
  const GraphPathType* graph_path_type;
  type_factory = type_factory ? type_factory : static_type_factory();
  GOOGLESQL_CHECK_OK(
      type_factory->MakeGraphPathType(node_type, edge_type, &graph_path_type));
  return graph_path_type;
}

absl::StatusOr<const Type*> MakeMapType(const Type* key_type,
                                        const Type* value_type,
                                        TypeFactory* type_factory) {
  type_factory = type_factory != nullptr ? type_factory : static_type_factory();
  return type_factory->MakeMapType(key_type, value_type);
}

bool AlmostEqualsValue(const Value& x, const Value& y, std::string* reason) {
  return InternalValue::Equals(
      x, y,
      {.interval_compare_mode = IntervalCompareMode::kAllPartsEqual,
       .float_margin = kDefaultFloatMargin,
       .reason = reason});
}

}  // namespace test_values

}  // namespace googlesql
