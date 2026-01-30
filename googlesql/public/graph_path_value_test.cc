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

#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/graph_path_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/hash/hash_testing.h"
#include "absl/status/status.h"

namespace googlesql {

namespace {

using ::googlesql::test_values::MakeGraphElementType;
using ::googlesql::test_values::MakeGraphPathType;
using ::googlesql::types::Int32Type;
using ::googlesql::types::StringType;
using ::testing::HasSubstr;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

MATCHER_P(BoolValueEquals, value, "") {
  return arg.is_valid() && !arg.is_null() && arg.bool_value() == value;
}

TEST(GraphPathValueTest, GraphPathNull) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);

  Value value = Value::Null(path_type);
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.graph_element(0), "Null value");
}

TEST(GraphPathValueTest, GraphPathCannotBeEmpty) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(
      Value::MakeGraphPath(path_type, /*graph_elements=*/{}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("got 0 graph elements")));
}

TEST(GraphPathValueTest, GraphPathCannotContainNull) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(Value::MakeGraphPath(path_type, {Value::Null(node_type)}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Path cannot have null graph elements")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable"}));

  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {node, Value::Null(edge_type), node}),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Path cannot have null graph elements")));
}

TEST(GraphPathValueTest, GraphPathTypeMismatch) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(Value::MakeGraphPath(path_type, {Value::Null(node_type)}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Path cannot have null graph elements")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(1)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e1",
                               .element_table_name = "EdgeTable",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n1"}}));

  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {node, edge, edge}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Expected node type")));
  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {node, node}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("got 2 graph elements")));
  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {edge}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Expected node type")));
  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {edge, node}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("got 2 graph elements")));
  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {edge, node, node}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Expected node type")));
}

TEST(GraphPathValueTest, SimpleGetter) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(Value::MakeGraphPath(path_type, {Value::Null(node_type)}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Path cannot have null graph elements")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable1"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("bar")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n2", .element_table_name = "NodeTable2"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(1)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e1",
                               .element_table_name = "EdgeTable",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n2"}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path,
                       Value::MakeGraphPath(path_type, {node1, edge, node2}));
  ASSERT_EQ(path.num_graph_elements(), 3);
  EXPECT_EQ(path.graph_element(0), node1);
  EXPECT_EQ(path.graph_element(1), edge);
  EXPECT_EQ(path.graph_element(2), node2);
}

TEST(GraphPathValueTest, DebugStrings) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(Value::MakeGraphPath(path_type, {Value::Null(node_type)}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Path cannot have null graph elements")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(1)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e1",
                               .element_table_name = "EdgeTable",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n1"}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path,
                       Value::MakeGraphPath(path_type, {node1, edge, node1}));

  EXPECT_EQ(path.ShortDebugString(),
            R"({node:{p0:"foo"}, edge:{p1:1}, node:{p0:"foo"}})");
  EXPECT_EQ(path.DebugString(),
            R"({node:{p0:"foo"}, edge:{p1:1}, node:{p0:"foo"}})");
  EXPECT_EQ(
      path.FullDebugString(),
      R"(GraphPath{node:GraphNode{$name:"NodeTable", $id:b"n1", $labels:["label1"], $is_dynamic:0, p0:String("foo")}
 property_name_to_index: {
  p0: 0
 }, edge:GraphEdge{$name:"EdgeTable", $id:b"e1", $labels:["label1"], $source_node_id:b"n1", $dest_node_id:b"n1", $is_dynamic:0, p1:Int32(1)}
 property_name_to_index: {
  p1: 0
 }, node:GraphNode{$name:"NodeTable", $id:b"n1", $labels:["label1"], $is_dynamic:0, p0:String("foo")}
 property_name_to_index: {
  p0: 0
 }})");
}

TEST(GraphPathValueTest, Comparisons) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(Value::MakeGraphPath(path_type, {Value::Null(node_type)}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Path cannot have null graph elements")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable1"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("bar")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n2", .element_table_name = "NodeTable2"}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge1,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(1)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e1",
                               .element_table_name = "EdgeTable1",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n2"}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge2,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(2)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e2",
                               .element_table_name = "EdgeTable2",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n2"}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path1,
                       Value::MakeGraphPath(path_type, {node1, edge1, node2}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path2,
                       Value::MakeGraphPath(path_type, {node1, edge1, node2}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path3,
                       Value::MakeGraphPath(path_type, {node2, edge1, node1}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path4,
                       Value::MakeGraphPath(path_type, {node1}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path5,
                       Value::MakeGraphPath(path_type, {node1, edge2, node2}));
  const Value path_copy = path2;

  Value null_path = Value::Null(path_type);

  EXPECT_TRUE(path1.Equals(path2));
  EXPECT_THAT(path1.SqlEquals(path2), BoolValueEquals(true));
  EXPECT_FALSE(path1.LessThan(path2));
  EXPECT_FALSE(path2.LessThan(path1));

  EXPECT_FALSE(path1.Equals(path3));
  EXPECT_THAT(path1.SqlEquals(path3), BoolValueEquals(false));
  EXPECT_TRUE(path1.LessThan(path3));
  EXPECT_FALSE(path3.LessThan(path1));

  EXPECT_FALSE(path1.Equals(path4));
  EXPECT_THAT(path1.SqlEquals(path4), BoolValueEquals(false));
  EXPECT_FALSE(path1.LessThan(path4));
  EXPECT_TRUE(path4.LessThan(path1));

  EXPECT_FALSE(path1.Equals(path5));
  EXPECT_THAT(path1.SqlEquals(path5), BoolValueEquals(false));
  EXPECT_TRUE(path1.LessThan(path5));
  EXPECT_FALSE(path5.LessThan(path1));

  EXPECT_TRUE(path1.SqlEquals(null_path).is_null());

  EXPECT_TRUE(path1.Equals(path_copy));
  EXPECT_TRUE(path_copy.Equals(path1));
  EXPECT_FALSE(path1.LessThan(path_copy));
  EXPECT_FALSE(path_copy.LessThan(path1));
  EXPECT_EQ(path1.HashCode(), path2.HashCode());
  EXPECT_EQ(path1.HashCode(), path_copy.HashCode());
  EXPECT_EQ(path1.physical_byte_size(), path2.physical_byte_size());
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {path1, path2, path3, path4, path5}));
}

TEST(GraphPathValueTest, IllformedPath) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  EXPECT_THAT(Value::MakeGraphPath(path_type, {Value::Null(node_type)}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Path cannot have null graph elements")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable1"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("bar")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n2", .element_table_name = "NodeTable2"}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge1,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(1)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e1",
                               .element_table_name = "EdgeTable1",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n2"}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge2,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(2)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e2",
                               .element_table_name = "EdgeTable2",
                               .source = {.identifier = "n2"},
                               .dest = {.identifier = "n2"}}));

  EXPECT_THAT(Value::MakeGraphPath(path_type, {node1, edge1, node2}), IsOk());
  EXPECT_THAT(Value::MakeGraphPath(path_type, {node1, edge2, node2}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Edge must connect the previous node")));
  EXPECT_THAT(Value::MakeGraphPath(path_type, {node2, edge2, node1}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Edge must connect the previous node")));
  EXPECT_THAT(Value::MakeGraphPath(path_type, {node2, edge2, node2}), IsOk());
}

TEST(GraphPathValueTest, PathWithSelfEdge) {
  const GraphElementType* node_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"p0", StringType()}});
  const GraphElementType* edge_type = MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}});
  const GraphPathType* path_type = MakeGraphPathType(node_type, edge_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("foo")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n1", .element_table_name = "NodeTable1"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      Value::MakeGraphNode(
          node_type,
          Value::GraphElementLabelsAndProperties{
              .static_labels = {"label1"},
              .static_properties = {{"p0", Value::String("bar")}}},
          Value::GraphNodeKeysAndElementTableNames{
              .identifier = "n2", .element_table_name = "NodeTable2"}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge1,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(1)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e1",
                               .element_table_name = "EdgeTable1",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n1"}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge2,
      Value::MakeGraphEdge(edge_type,
                           Value::GraphElementLabelsAndProperties{
                               .static_labels = {"label1"},
                               .static_properties = {{"p1", Value::Int32(2)}}},
                           Value::GraphEdgeKeysAndElementTableNames{
                               .identifier = "e2",
                               .element_table_name = "EdgeTable2",
                               .source = {.identifier = "n1"},
                               .dest = {.identifier = "n2"}}));

  EXPECT_THAT(Value::MakeGraphPath(path_type, {node1, edge1, node1}), IsOk());
  EXPECT_THAT(
      Value::MakeGraphPath(path_type, {node1, edge1, node1, edge2, node2}),
      IsOk());
  EXPECT_THAT(Value::MakeGraphPath(path_type, {node1, edge1, node2}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Edge must connect the previous node")));
  EXPECT_THAT(Value::MakeGraphPath(path_type, {node2, edge1, node1}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Edge must connect the previous node")));
}

}  // namespace
}  // namespace googlesql
