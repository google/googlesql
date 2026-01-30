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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/graph_element_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

using ::googlesql::test_values::DynamicGraphEdge;
using ::googlesql::test_values::DynamicGraphNode;
using ::googlesql::test_values::MakeDynamicGraphElementType;
using ::googlesql::test_values::GraphEdge;
using ::googlesql::test_values::GraphNode;
using ::googlesql::test_values::MakeGraphElementType;
using ::googlesql::types::Int32Type;
using ::googlesql::types::StringType;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

// Parameterized common test that applies to both node and edge.
class GraphElementValueTest
    : public TestWithParam<GraphElementType::ElementKind> {
 protected:
  bool IsNode() const { return GetParam() == GraphElementType::kNode; }
  bool IsEdge() const { return GetParam() == GraphElementType::kEdge; }

  absl::StatusOr<Value> MakeStaticGraphElementByType(
      const GraphElementType* type, std::string identifier,
      std::vector<Value::Property> properties, std::vector<std::string> labels,
      std::string definition_name
  ) {
    return IsNode() ? Value::MakeGraphNode(
                          type,
                          Value::GraphElementLabelsAndProperties{
                              .static_labels = std::move(labels),
                              .static_properties = std::move(properties)},
                          Value::GraphNodeKeysAndElementTableNames{
                              .identifier = std::move(identifier),
                              .element_table_name = std::move(definition_name)}
                          )
                    : Value::MakeGraphEdge(
                          type,
                          Value::GraphElementLabelsAndProperties{
                              .static_labels = std::move(labels),
                              .static_properties = std::move(properties)},
                          Value::GraphEdgeKeysAndElementTableNames{
                              .identifier = std::move(identifier),
                              .element_table_name = std::move(definition_name),
                              .source = {.identifier = "src_node_id"},
                              .dest = {.identifier = "dst_node_id"}}
                      );
  }

  Value MakeElement(absl::Span<const std::string> graph_reference,
                    std::string identifier,
                    std::vector<Value::Property> properties,
                    absl::Span<const std::string> labels,
                    std::string definition_name
  ) {
    auto value =
        IsNode() ? GraphNode(graph_reference, identifier, properties, labels,
                             definition_name
                             )
                 : GraphEdge(graph_reference, identifier, properties, labels,
                             definition_name, "src_node_id",
                             "dst_node_id"
                   );
    GOOGLESQL_CHECK_OK(value);
    return *value;
  }

  absl::StatusOr<Value> MakeDynamicGraphElementByType(
      const GraphElementType* type, std::string identifier,
      std::vector<Value::Property> static_properties,
      std::vector<Value::Property> dynamic_properties,
      std::vector<std::string> static_labels,
      std::vector<std::string> dynamic_labels, std::string definition_name) {
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                     MakePropertiesJsonValue(absl::MakeSpan(dynamic_properties),
                                             language_options_));
    return IsNode()
               ? Value::MakeGraphNode(
                     type, std::move(identifier),
                     Value::GraphElementLabelsAndProperties{
                         .static_labels = std::move(static_labels),
                         .static_properties = std::move(static_properties),
                         .dynamic_labels = std::move(dynamic_labels),
                         .dynamic_properties = json_value.GetConstRef()},
                     std::move(definition_name))
               : Value::MakeGraphEdge(
                     type, std::move(identifier),
                     Value::GraphElementLabelsAndProperties{
                         .static_labels = std::move(static_labels),
                         .static_properties = std::move(static_properties),
                         .dynamic_labels = std::move(dynamic_labels),
                         .dynamic_properties = json_value.GetConstRef()},
                     std::move(definition_name), "src_node_id", "dst_node_id");
  }

  absl::StatusOr<Value> MakeDynamicElement(
      absl::Span<const std::string> graph_reference, std::string identifier,
      std::vector<Value::Property> static_properties,
      std::vector<Value::Property> dynamic_properties,
      std::vector<std::string> static_labels,
      std::vector<std::string> dynamic_labels, std::string definition_name) {
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                     MakePropertiesJsonValue(absl::MakeSpan(dynamic_properties),
                                             language_options_));
    return IsNode() ? DynamicGraphNode(
                          graph_reference, identifier, static_properties,
                          /*dynamic_properties=*/json_value.GetConstRef(),
                          static_labels, dynamic_labels, definition_name)
                    : DynamicGraphEdge(
                          graph_reference, identifier, static_properties,
                          /*dynamic_properties=*/json_value.GetConstRef(),
                          static_labels, dynamic_labels, definition_name,
                          "src_node_id", "dst_node_id");
  }

  const LanguageOptions language_options_ = LanguageOptions::MaximumFeatures();
};

INSTANTIATE_TEST_SUITE_P(Common, GraphElementValueTest,
                         ValuesIn({GraphElementType::kNode,
                                   GraphElementType::kEdge}));

TEST_P(GraphElementValueTest, GraphElementNull) {
  const auto* type = MakeGraphElementType({"graph_name"}, GetParam(), {});
  Value value = Value::Null(type);
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.property_values(), "Null value");
}

TEST_P(GraphElementValueTest, InvalidConstructionDifferentPropertyNames) {
  const Value property_value = Value::String("v0");
  const GraphElementType* static_type =
      MakeGraphElementType({"graph_name"}, GetParam(), {{"p0", StringType()}});
  const std::vector<Value::Property> properties = {{"p1", property_value}};

  EXPECT_THAT(
      MakeStaticGraphElementByType(static_type, "id", properties, {"label"},
                                   "ElementTable"),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Unknown property: p1")));
}

TEST_P(GraphElementValueTest,
       StaticGraphElementNonExistentPropertiesThrowsError) {
  const Value property_value = Value::Int32(1);
  const GraphElementType* type = MakeGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", StringType()}, {"p1", Int32Type()}});
  const std::vector<Value::Property> properties = {{"p1", property_value}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_value,
                       MakeStaticGraphElementByType(type, "id", properties,
                                                    {"label"}, "ElementTable"));

  // p0 does not exist in graph_value and cannot be found with name.
  EXPECT_THAT(graph_value.FindValidPropertyValueByName("p0"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(graph_value.FindPropertyByName("p0"),
              IsOkAndHolds(Eq(Value::Null(StringType()))));

  // p1 exists in graph_value and can be found with name.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value p1_value,
                       graph_value.FindValidPropertyValueByName("p1"));
  EXPECT_EQ(p1_value, property_value);
  EXPECT_EQ(graph_value.DebugString(), "{p0:NULL, p1:1}");
}

TEST_P(GraphElementValueTest, InvalidConstructionDifferentPropertyTypes) {
  const Value p0_value = Value::String("v0");
  const GraphElementType* type = MakeGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", StringType()}, {"p1", Int32Type()}});
  const std::vector<Value::Property> properties = {{"p0", p0_value},
                                                   {"p1", p0_value}};
  EXPECT_THAT(
      MakeStaticGraphElementByType(type, "id", properties, {"label"},
                                   "ElementTable"),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Expected property value type: INT32, got: STRING")));
}

TEST_P(GraphElementValueTest, EmptyIdentifierCausesConstructionToFail) {
  const GraphElementType* type =
      MakeGraphElementType({"graph_name"}, GetParam(), {});
  const std::vector<Value::Property> properties;
  EXPECT_THAT(
      MakeStaticGraphElementByType(type, "", properties, {"label"},
                                   "ElementTable"),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Empty identifier")));
}

TEST_P(GraphElementValueTest, StaticGraphElementCommonTest) {
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  const Value element =
      MakeElement({"graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                  {"label1", "label2"}, "ElementTable");
  EXPECT_EQ(element.type_kind(), TYPE_GRAPH_ELEMENT);
  EXPECT_THAT(element.type()->AsGraphElement()->graph_reference(),
              ElementsAre("graph_name"));
  EXPECT_EQ(element.GetIdentifier(), "id");
  EXPECT_THAT(element.property_values(),
              ElementsAre(Eq(p0_value), Eq(p1_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("p0"),
              IsOkAndHolds(Eq(p0_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("p1"),
              IsOkAndHolds(Eq(p1_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("unknown"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(element.GetLabels(), ElementsAre("label1", "label2"));
  EXPECT_EQ(element.GetDefinitionName(), "ElementTable");

  const Value copied_element = element;
  EXPECT_EQ(copied_element.type_kind(), TYPE_GRAPH_ELEMENT);
  EXPECT_THAT(copied_element.type()->AsGraphElement()->graph_reference(),
              ElementsAre("graph_name"));
  EXPECT_EQ(copied_element.GetIdentifier(), "id");
  EXPECT_THAT(copied_element.property_values(),
              ElementsAre(Eq(p0_value), Eq(p1_value)));
  EXPECT_THAT(copied_element.GetLabels(), ElementsAre("label1", "label2"));
  EXPECT_EQ(copied_element.GetDefinitionName(), "ElementTable");

  EXPECT_TRUE(element.Equals(copied_element));
  EXPECT_FALSE(element.LessThan(copied_element));
  EXPECT_FALSE(copied_element.LessThan(element));
  EXPECT_EQ(element.HashCode(), copied_element.HashCode());

  const Value element_with_same_graph_and_id =
      MakeElement({"graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                  {"label1", "label2"}, "ElementTable");
  EXPECT_TRUE(element.Equals(element_with_same_graph_and_id));
  EXPECT_FALSE(element.LessThan(element_with_same_graph_and_id));
  EXPECT_FALSE(element_with_same_graph_and_id.LessThan(element));
  EXPECT_EQ(element.HashCode(), element_with_same_graph_and_id.HashCode());

  const Value element_with_same_graph_different_id =
      MakeElement({"graph_name"}, "id2", {{"p0", p0_value}, {"p1", p1_value}},
                  {"label1", "label2"}, "ElementTable");
  EXPECT_FALSE(element.Equals(element_with_same_graph_different_id));
  EXPECT_TRUE(element.LessThan(element_with_same_graph_different_id));
  EXPECT_FALSE(element_with_same_graph_different_id.LessThan(element));
  EXPECT_NE(element.HashCode(),
            element_with_same_graph_different_id.HashCode());

  const Value element_with_different_graph_same_id = MakeElement(
      {"another_graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
      {"label1", "label2"}, "ElementTable");
  EXPECT_FALSE(element.Equals(element_with_different_graph_same_id));
  EXPECT_FALSE(element.LessThan(element_with_different_graph_same_id));
  EXPECT_FALSE(element_with_different_graph_same_id.LessThan(element));
  EXPECT_NE(element.HashCode(),
            element_with_different_graph_same_id.HashCode());
}

TEST_P(GraphElementValueTest, GetLabels) {
  // GetLabels sorts labels in alphabet order case-insensitively and preserves
  // the original case.
  const Value element = MakeElement(
      {"graph_name"}, "id", {}, {"label2", "laBel3", "Label1"}, "ElementTable");
  EXPECT_THAT(element.GetLabels(), ElementsAre("Label1", "label2", "laBel3"));
}

TEST_P(GraphElementValueTest, GetLabelsMultiDynamicLabels) {
  const GraphElementType* dynamic_type = MakeDynamicGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", StringType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value element,
      MakeDynamicGraphElementByType(
          dynamic_type, "id", /*static_properties=*/{},
          /*dynamic_properties=*/{}, /*static_labels=*/{},
          /*dynamic_labels=*/{"label3", "label1", "label2"}, "ElementTable"));
  EXPECT_THAT(element.GetLabels(), ElementsAre("label1", "label2", "label3"));
}

TEST_P(GraphElementValueTest, GetLabelsMultiDynamicAndStaticLabel) {
  const GraphElementType* dynamic_type = MakeDynamicGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", StringType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value element,
      MakeDynamicGraphElementByType(
          dynamic_type, "id", /*static_properties=*/{},
          /*dynamic_properties=*/{}, /*static_labels=*/{"label1", "label0"},
          /*dynamic_labels=*/{"label3", "label2"}, "ElementTable"));
  EXPECT_THAT(element.GetLabels(),
              ElementsAre("label0", "label1", "label2", "label3"));
}

TEST_P(GraphElementValueTest,
       DynamicGraphElementInvalidConstructionDifferentPropertyNames) {
  const std::vector<Value::Property> properties = {{"p1", Value::String("v0")}};
  const GraphElementType* dynamic_type = MakeDynamicGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", StringType()}});
  EXPECT_THAT(
      MakeDynamicGraphElementByType(
          dynamic_type, "id", /*static_properties=*/properties,
          /*dynamic_properties=*/{}, /*static_labels=*/{"label1"},
          /*dynamic_labels=*/{"label2"}, "ElementTable"),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Unknown property: p1")));
}

TEST_P(GraphElementValueTest,
       DynamicGraphElementInvalidConstructionDifferentPropertyTypes) {
  const Value p0_value = Value::String("v0");
  const std::vector<Value::Property> properties = {{"p0", p0_value},
                                                   {"p1", p0_value}};
  const GraphElementType* dynamic_type = MakeDynamicGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", StringType()}, {"p1", Int32Type()}});
  EXPECT_THAT(
      MakeDynamicGraphElementByType(
          dynamic_type, "id", /*static_properties=*/properties,
          /*dynamic_properties=*/{}, /*static_labels=*/{"label1"},
          /*dynamic_labels=*/{"label2"}, "ElementTable"),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Expected property value type: INT32, got: STRING")));
}

TEST_P(GraphElementValueTest, DynamicGraphElementCommonTest) {
  bool p2 = true;
  double p3 = 3.14;
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  const Value p2_value = Value::Bool(p2);
  const Value p3_value = Value::Double(p3);
  const Value null_json_value = Value::NullJson();
  JSONValue p2_json(p2);
  JSONValue p3_json(p3);
  const Value p2_json_value = Value::Json(std::move(p2_json));
  const Value p3_json_value = Value::Json(std::move(p3_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value element,
      MakeDynamicElement(
          {"graph_name"}, "id",
          /*static_properties=*/{{"p0", p0_value}, {"p1", p1_value}},
          /*dynamic_properties=*/{{"p2", p2_value}, {"p3", p3_value}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  EXPECT_EQ(element.type_kind(), TYPE_GRAPH_ELEMENT);
  EXPECT_THAT(element.type()->AsGraphElement()->graph_reference(),
              ElementsAre("graph_name"));
  EXPECT_EQ(element.GetIdentifier(), "id");
  EXPECT_THAT(element.property_values(),
              ElementsAre(Eq(p0_value), Eq(p1_value), Eq(p2_json_value),
                          Eq(p3_json_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("p0"),
              IsOkAndHolds(Eq(p0_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("p1"),
              IsOkAndHolds(Eq(p1_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));
  EXPECT_THAT(element.FindValidPropertyValueByName("p3"),
              IsOkAndHolds(Eq(p3_json_value)));

  EXPECT_THAT(element.FindValidPropertyValueByName("unknown"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(element.FindPropertyByName("unknown"),
              IsOkAndHolds(Eq(null_json_value)));
  EXPECT_THAT(element.GetLabels(), ElementsAre("label1", "label2"));
  EXPECT_EQ(element.GetDefinitionName(), "ElementTable");

  // Static labels order takes precedence over dynamic labels when deduplicated
  // case-insensitively.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value element2,
      MakeDynamicElement(
          {"graph_name"}, "id",
          /*static_properties=*/{{"p0", p0_value}, {"p1", p1_value}},
          /*dynamic_properties=*/{{"p2", p2_value}, {"p3", p3_value}},
          /*static_labels=*/{"label2", "Label1"}, /*dynamic_labels=*/{"laBel1"},
          "ElementTable"));
  EXPECT_THAT(element2.GetLabels(), ElementsAre("Label1", "label2"));
}

TEST_P(GraphElementValueTest,
       DynamicGraphElementPropertyNameDynamicShadowsStatic) {
  const Value p1_value_1 = Value::String("v0");
  const Value p1_value_2 = Value::Bool(true);
  const std::vector<Value::Property> static_properties1 = {{"p1", p1_value_1}};
  const std::vector<Value::Property> dynamic_properties = {{"p1", p1_value_2}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value element1,
      MakeDynamicElement({"graph_name"}, "id", static_properties1,
                         dynamic_properties,
                         /*static_labels=*/{"label1", "label2"},
                         /*dynamic_labels=*/{"label3"}, "ElementTable"));
  EXPECT_THAT(element1.FindValidPropertyValueByName("p1"),
              IsOkAndHolds(Eq(p1_value_1)));

  const std::vector<Value::Property> static_properties2 = {};
  const GraphElementType* dynamic_type = MakeDynamicGraphElementType(
      {"graph_name"}, GetParam(), {{"p0", Int32Type()}, {"p1", StringType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value element2,
                       MakeDynamicGraphElementByType(
                           dynamic_type, "id", static_properties2,
                           dynamic_properties, /*static_labels=*/{"label1"},
                           /*dynamic_labels=*/{"label2"}, "ElementTable"));
  EXPECT_THAT(element2.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(Value::Null(StringType()))));
}

TEST_P(GraphElementValueTest,
       DynamicGraphElementLabelNameDynamicShadowsStatic) {
  const Value p1_value_1 = Value::String("v0");
  const Value p1_value_2 = Value::Bool(true);
  const std::vector<Value::Property> static_properties = {{"p1", p1_value_1}};
  const std::vector<Value::Property> dynamic_properties = {{"p1", p1_value_2}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value element,
      MakeDynamicElement({"graph_name"}, "id", static_properties,
                         dynamic_properties,
                         /*static_labels=*/{"label1", "label2"},
                         /*dynamic_labels=*/{"label1"}, "ElementTable"));
  EXPECT_THAT(element.GetLabels(), ElementsAre("label1", "label2"));
}

TEST_P(GraphElementValueTest,
       DynamicGraphElementValueContentEqualsTestDifferentValue) {
  // Dynamic elements are not equal if they have different static property
  // values.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_element1,
      MakeDynamicElement(
          {"graph_name"}, "id1",
          /*static_properties=*/
          {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"p2", Value::Bool(true)}, {"p3", Value::Double(3.14)}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_element2,
      MakeDynamicElement(
          {"graph_name"}, "id2",
          /*static_properties=*/
          {{"p0", Value::String("v0")}, {"p1", Value::Int32(2)}},
          /*dynamic_properties=*/
          {{"p2", Value::Bool(true)}, {"p3", Value::Double(3.14)}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  EXPECT_FALSE(dynamic_element1.Equals(dynamic_element2));

  // Dynamic elements are not equal if they have different dynamic property
  // values.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_element3,
      MakeDynamicElement(
          {"graph_name"}, "id1",
          /*static_properties=*/
          {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"p2", Value::Bool(true)}, {"p3", Value::Double(3.14)}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_element4,
      MakeDynamicElement(
          {"graph_name"}, "id2",
          /*static_properties=*/
          {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"p2", Value::Bool(true)}, {"p3", Value::Double(100000.0)}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  EXPECT_FALSE(dynamic_element3.Equals(dynamic_element4));
}

TEST_P(GraphElementValueTest,
       DynamicGraphElementValueContentEqualsTestDifferentIdentifier) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_element1,
      MakeDynamicElement(
          {"graph_name"}, "id1",
          /*static_properties=*/
          {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"p2", Value::Bool(true)}, {"p3", Value::Double(3.14)}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_element2,
      MakeDynamicElement(
          {"graph_name"}, "id2",
          /*static_properties=*/
          {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"p2", Value::Bool(true)}, {"p3", Value::Double(3.14)}},
          /*static_labels=*/{"label1", "label2"}, /*dynamic_labels=*/{"Label1"},
          "ElementTable"));
  EXPECT_FALSE(dynamic_element1.Equals(dynamic_element2));
}

TEST(GraphElementValueTest,
     EmptyIdentifierForSourceOrDestNodeFailsEdgeConstruction) {
  const GraphElementType* type =
      MakeGraphElementType({"graph_name"}, GraphElementType::kEdge, {});
  const std::vector<Value::Property> properties;
  EXPECT_THAT(
      Value::MakeGraphEdge(
          type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label"}, .static_properties = properties},
          Value::GraphEdgeKeysAndElementTableNames{
              .identifier = "id",
              .element_table_name = "ElementTable",
              .source = {.identifier = "", .element_table_name = ""},
              .dest = {.identifier = "dst_node_id", .element_table_name = ""}}),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Invalid source node identifier")));
  EXPECT_THAT(
      Value::MakeGraphEdge(
          type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label"}, .static_properties = properties},
          Value::GraphEdgeKeysAndElementTableNames{
              .identifier = "id",
              .element_table_name = "ElementTable",
              .source = {.identifier = "src_node_id", .element_table_name = ""},
              .dest = {.identifier = "", .element_table_name = ""}}),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Invalid destination node identifier")));
}

TEST(GraphElementValueTest, GraphNodeSpecificTest) {
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node,
      GraphNode({"graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                {"label1", "label2"}, "ElementTable"));
  EXPECT_TRUE(node.IsNode());
  EXPECT_FALSE(node.IsEdge());
  EXPECT_DEBUG_DEATH(node.GetSourceNodeIdentifier(), "Not an edge");
  EXPECT_DEBUG_DEATH(node.GetDestNodeIdentifier(), "Not an edge");
  EXPECT_EQ(node.ShortDebugString(), "{p0:\"v0\", p1:1}");
  EXPECT_EQ(node.FullDebugString(),
            "GraphNode{$name:\"ElementTable\", $id:b\"id\", "
            "$labels:[\"label1\", \"label2\"], "
            "$is_dynamic:0, "
            "p0:String(\"v0\"), "
            "p1:Int32(1)}\n"
            " property_name_to_index: {\n"
            "  p0: 0\n"
            "  p1: 1\n"
            " }");
}

TEST(GraphElementValueTest, GraphEdgeSpecificTest) {
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge,
      GraphEdge({"graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                {"label1", "label2"}, "ElementTable", "src_node_id",
                "dst_node_id"));
  EXPECT_FALSE(edge.IsNode());
  EXPECT_TRUE(edge.IsEdge());
  EXPECT_EQ(edge.GetSourceNodeIdentifier(), "src_node_id");
  EXPECT_EQ(edge.GetDestNodeIdentifier(), "dst_node_id");
  EXPECT_EQ(edge.ShortDebugString(), "{p0:\"v0\", p1:1}");
  EXPECT_EQ(edge.FullDebugString(),
            "GraphEdge{$name:\"ElementTable\", $id:b\"id\", "
            "$labels:[\"label1\", \"label2\"], "
            "$source_node_id:b\"src_node_id\", $dest_node_id:b\"dst_node_id\", "
            "$is_dynamic:0, "
            "p0:String(\"v0\"), "
            "p1:Int32(1)}\n"
            " property_name_to_index: {\n"
            "  p0: 0\n"
            "  p1: 1\n"
            " }");

  const Value copied_edge = edge;
  EXPECT_EQ(copied_edge.GetIdentifier(), "id");
  EXPECT_THAT(copied_edge.property_values(),
              ElementsAre(Eq(p0_value), Eq(p1_value)));
  EXPECT_EQ(copied_edge.GetSourceNodeIdentifier(), "src_node_id");
  EXPECT_EQ(copied_edge.GetDestNodeIdentifier(), "dst_node_id");
}

// TODO: Nested graph element should be rejected.
TEST(GraphElementValueTest, NestedGraphElementTest) {
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node,
      GraphNode({"graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                {"label"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value nested_node,
                       GraphNode({"graph_name"}, "id2", {{"p", node}},
                                 {"label"}, "ElementTable"));
  GOOGLESQL_EXPECT_OK(nested_node.FindValidPropertyValueByName("p"));
}

TEST(GraphElementValueTest, ValueContentEqualsTestSameValue) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      GraphNode({"graph_name"}, "id1",
                {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
                {"label"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      GraphNode({"graph_name"}, "id1",
                {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
                {"label"}, "ElementTable"));
  EXPECT_TRUE(node1.Equals(node2));
}

TEST(GraphElementValueTest, ValueContentEqualsTestSameValueWithNulls) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      GraphNode({"graph_name"}, "id1",
                {{"p0", Value::String("v0")}, {"p1", Value::NullInt32()}},
                {"label"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      GraphNode({"graph_name"}, "id1",
                {{"p0", Value::String("v0")}, {"p1", Value::NullInt32()}},
                {"label"}, "ElementTable"));
  EXPECT_TRUE(node1.Equals(node2));
}

TEST_P(GraphElementValueTest, ValueContentEqualsTestDifferentValue) {
  const Value static_element1 =
      MakeElement({"graph_name"}, "id1",
                  {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
                  {"label1", "label2"}, "ElementTable");
  const Value static_element2 =
      MakeElement({"graph_name"}, "id2",
                  {{"p0", Value::String("v0")}, {"p1", Value::Int32(2)}},
                  {"label1", "label2"}, "ElementTable");
  EXPECT_FALSE(static_element1.Equals(static_element2));
}

TEST(GraphElementValueTest, ValueContentEqualsTestDifferentValueWithNulls) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      GraphNode({"graph_name"}, "id1",
                {{"p0", Value::String("v0")}, {"p1", Value::NullInt32()}},
                {"label"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      GraphNode({"graph_name"}, "id2",
                {{"p0", Value::String("v0")}, {"p1", Value::Int32(2)}},
                {"label"}, "ElementTable"));
  EXPECT_FALSE(node1.Equals(node2));
}

TEST_P(GraphElementValueTest, ValueContentEqualsTestDifferentIdentifier) {
  const Value static_element1 =
      MakeElement({"graph_name"}, "id1",
                  {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
                  {"label1", "label2"}, "ElementTable");
  const Value static_element2 =
      MakeElement({"graph_name"}, "id2",
                  {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
                  {"label1", "label2"}, "ElementTable");
  EXPECT_FALSE(static_element1.Equals(static_element2));
}

TEST(GraphElementValueTest, ValueContentEqualsTestDifferentColumns) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      GraphNode({"graph_name"}, "id1",
                {{"p0", Value::String("v0")}, {"p1", Value::Int32(1)}},
                {"label"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value node2,
                       GraphNode({"graph_name"}, "id1",
                                 {{"p0", Value::String("v0")},
                                  {"p1", Value::Int32(1)},
                                  {"p2", Value::Bool(true)}},
                                 {"label"}, "ElementTable"));
  EXPECT_FALSE(node1.Equals(node2));
}

}  // namespace

}  // namespace googlesql
