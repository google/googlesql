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

#include "googlesql/common/graph_element_utils.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/internal_property_graph.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/functions/json.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::UnorderedElementsAre;
using ::absl_testing::IsOkAndHolds;

TEST(GraphElementUtilsTest, IsOrContainsGraphElementTest) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* int_type = factory.get_int64();
  const Type* bytes_type = factory.get_bytes();

  // Basic types
  ASSERT_FALSE(TypeIsOrContainsGraphElement(string_type));
  ASSERT_FALSE(TypeIsOrContainsGraphElement(int_type));
  ASSERT_FALSE(TypeIsOrContainsGraphElement(bytes_type));

  // Node type
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"aml"}, GraphElementType::ElementKind::kNode,
      {{"id", int_type}, {"name", string_type}, {"data", bytes_type}},
      &node_type));
  ASSERT_THAT(node_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(node_type));

  // Edge type
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"aml"}, GraphElementType::kEdge,
      {{"transfer_id", int_type}, {"amount", int_type}}, &edge_type));
  ASSERT_THAT(edge_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(edge_type));

  // Path type
  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  ASSERT_THAT(path_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(path_type));

  // Array of GraphNode
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(node_type, &array_type));
  ASSERT_THAT(array_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(array_type));

  // Struct of GraphEdge and GraphPath
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"edge", edge_type}, {"path", path_type}},
                                   &struct_type));
  ASSERT_THAT(struct_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(struct_type));
}

TEST(GraphElementUtilsTest, MakePropertiesJsonValueTest) {
  const LanguageOptions language_options = LanguageOptions::MaximumFeatures();
  const Value p1_value = Value::Bool(true);
  const Value p2_value = Value::Double(3.14);
  std::vector<Value::Property> properties = {{"p1", p1_value},
                                             {"p2", p2_value}};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue json_value,
      MakePropertiesJsonValue(absl::MakeSpan(properties), language_options));
  EXPECT_EQ(json_value.GetConstRef().GetMembers().size(), 2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const bool p1,
      functions::ConvertJsonToBool(json_value.GetConstRef().GetMember("p1")));
  EXPECT_EQ(p1, p1_value.bool_value());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const double p2,
      functions::ConvertJsonToDouble(json_value.GetConstRef().GetMember("p2"),
                                     functions::WideNumberMode::kExact,
                                     ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ(p2, p2_value.double_value());
}

TEST(GraphElementUtilsTest, IsRestrictivePathModeTest) {
  EXPECT_TRUE(IsRestrictivePathMode(ResolvedGraphPathMode::TRAIL));
  EXPECT_TRUE(IsRestrictivePathMode(ResolvedGraphPathMode::ACYCLIC));
  EXPECT_TRUE(IsRestrictivePathMode(ResolvedGraphPathMode::SIMPLE));
  EXPECT_FALSE(IsRestrictivePathMode(ResolvedGraphPathMode::WALK));
}

TEST(GraphElementUtilsTest, HasSearchPrefixTest) {
  EXPECT_TRUE(HasSearchPrefix(ResolvedGraphPathSearchPrefix::ANY));
  EXPECT_TRUE(HasSearchPrefix(ResolvedGraphPathSearchPrefix::SHORTEST));
  EXPECT_TRUE(HasSearchPrefix(ResolvedGraphPathSearchPrefix::CHEAPEST));
  EXPECT_FALSE(HasSearchPrefix(
      ResolvedGraphPathSearchPrefix::PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED));
}

TEST(GraphElementUtilsTest, IntersectPeriodsTest) {
  auto period = [](std::optional<int64_t> start, std::optional<int64_t> end) {
    return test_values::Range(
        start ? Value::TimestampFromUnixMicros(*start) : Value::NullTimestamp(),
        end ? Value::TimestampFromUnixMicros(*end) : Value::NullTimestamp());
  };

  EXPECT_THAT(IntersectPeriods(period(1000, 2000), period(1500, 2500)),
              IsOkAndHolds(Optional(period(1500, 2000))));
  // NULL bounds are unbounded.
  EXPECT_THAT(
      IntersectPeriods(period(std::nullopt, 2500), period(1000, std::nullopt)),
      IsOkAndHolds(Optional(period(1000, 2500))));
  EXPECT_THAT(
      IntersectPeriods(period(1000, std::nullopt), period(std::nullopt, 2500)),
      IsOkAndHolds(Optional(period(1000, 2500))));
  EXPECT_THAT(IntersectPeriods(period(std::nullopt, std::nullopt),
                               period(std::nullopt, std::nullopt)),
              IsOkAndHolds(Optional(period(std::nullopt, std::nullopt))));
  EXPECT_THAT(
      IntersectPeriods(period(std::nullopt, std::nullopt), period(1000, 2000)),
      IsOkAndHolds(Optional(period(1000, 2000))));
  // Disjoint, adjacent (the end is exclusive) and half-unbounded disjoint
  // periods have an empty intersection.
  EXPECT_THAT(IntersectPeriods(period(1000, 2000), period(3000, 4000)),
              IsOkAndHolds(std::nullopt));
  EXPECT_THAT(IntersectPeriods(period(1000, 2000), period(2000, 3000)),
              IsOkAndHolds(std::nullopt));
  EXPECT_THAT(
      IntersectPeriods(period(std::nullopt, 1000), period(1000, std::nullopt)),
      IsOkAndHolds(std::nullopt));
}

TEST(GraphElementUtilsTest, PeriodContainsPointTest) {
  Value start1 = Value::TimestampFromUnixMicros(1000);
  Value end1 = Value::TimestampFromUnixMicros(2000);
  Value period1 = test_values::Range(start1, end1);

  Value point_inside = Value::TimestampFromUnixMicros(1500);
  Value point_outside_before = Value::TimestampFromUnixMicros(500);
  Value point_outside_after = Value::TimestampFromUnixMicros(2500);

  EXPECT_THAT(PeriodContainsPoint(period1, point_inside), IsOkAndHolds(true));
  // The start is inclusive and the end is exclusive.
  EXPECT_THAT(PeriodContainsPoint(period1, start1), IsOkAndHolds(true));
  EXPECT_THAT(PeriodContainsPoint(period1, end1), IsOkAndHolds(false));
  EXPECT_THAT(PeriodContainsPoint(period1, point_outside_before),
              IsOkAndHolds(false));
  EXPECT_THAT(PeriodContainsPoint(period1, point_outside_after),
              IsOkAndHolds(false));
}

TEST(GraphElementUtilsTest, GetPropertyBackingColumnTest) {
  const std::vector<std::string> graph_name_path = {"test_graph"};
  SimpleColumn writable_col("NodeTable", "id", types::Int64Type(),
                            {.is_writable_column = true});
  SimpleColumn readonly_col("NodeTable", "readonly_col", types::StringType(),
                            {.is_writable_column = false});

  SimpleGraphPropertyDeclaration scalar_decl(
      "id", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);
  SimpleGraphPropertyDeclaration readonly_decl(
      "readonly_prop", graph_name_path, types::StringType(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);
  SimpleGraphPropertyDeclaration derived_decl(
      "derived_prop", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);
  SimpleGraphPropertyDeclaration measure_decl(
      "measure_prop", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr,
      GraphPropertyDeclaration::Kind::kMeasure);

  auto writable_col_ref =
      MakeResolvedCatalogColumnRef(types::Int64Type(), &writable_col);
  SimpleGraphPropertyDefinition writable_prop_def(&scalar_decl, "id");
  InternalPropertyGraph::InternalSetResolvedExpr(&writable_prop_def,
                                                 writable_col_ref.get());
  EXPECT_THAT(GetPropertyBackingColumn(writable_prop_def),
              IsOkAndHolds(&writable_col));

  auto readonly_col_ref =
      MakeResolvedCatalogColumnRef(types::StringType(), &readonly_col);
  SimpleGraphPropertyDefinition readonly_prop_def(&readonly_decl,
                                                  "readonly_col");
  InternalPropertyGraph::InternalSetResolvedExpr(&readonly_prop_def,
                                                 readonly_col_ref.get());
  EXPECT_THAT(GetPropertyBackingColumn(readonly_prop_def),
              IsOkAndHolds(&readonly_col));

  auto literal_expr = MakeResolvedLiteral(Value::Int64(42));
  SimpleGraphPropertyDefinition derived_prop_def(&derived_decl, "42");
  InternalPropertyGraph::InternalSetResolvedExpr(&derived_prop_def,
                                                 literal_expr.get());
  EXPECT_THAT(GetPropertyBackingColumn(derived_prop_def),
              IsOkAndHolds(nullptr));

  SimpleGraphPropertyDefinition measure_prop_def(&measure_decl, "SUM(id)");
  EXPECT_THAT(GetPropertyBackingColumn(measure_prop_def),
              IsOkAndHolds(nullptr));
}

TEST(GraphElementUtilsTest,
     GetPropertyBackingColumnsAndAreAllKeyColumnsWritablePropertiesTest) {
  const std::vector<std::string> graph_name_path = {"test_graph"};
  std::vector<std::unique_ptr<const Column>> columns;
  columns.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  columns.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "unexposed_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  columns.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "readonly_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  columns.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "measure_col", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable base_table("NodeTable", std::move(columns));

  SimpleGraphPropertyDeclaration id_decl(
      "id", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);
  SimpleGraphPropertyDeclaration measure_decl(
      "m", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr,
      GraphPropertyDeclaration::Kind::kMeasure);

  auto id_col_ref =
      MakeResolvedCatalogColumnRef(types::Int64Type(), base_table.GetColumn(0));
  auto id_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "id");
  InternalPropertyGraph::InternalSetResolvedExpr(id_prop_def.get(),
                                                 id_col_ref.get());

  auto measure_col_ref =
      MakeResolvedCatalogColumnRef(types::Int64Type(), base_table.GetColumn(3));
  auto measure_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&measure_decl, "m");
  InternalPropertyGraph::InternalSetResolvedExpr(measure_prop_def.get(),
                                                 measure_col_ref.get());

  std::vector<std::unique_ptr<const GraphPropertyDefinition>> prop_defs;
  prop_defs.push_back(std::move(id_prop_def));
  prop_defs.push_back(std::move(measure_prop_def));

  SimpleGraphElementLabel label("NodeLabel", graph_name_path,
                                {&id_decl, &measure_decl});
  SimpleGraphNodeTable node_table("NodeTable", graph_name_path, &base_table,
                                  /*key_cols=*/{0},
                                  /*labels=*/{&label}, std::move(prop_defs));

  // Only non-measure properties with direct catalog column references are
  // returned.
  EXPECT_THAT(GetPropertyBackingColumns(node_table),
              IsOkAndHolds(UnorderedElementsAre(base_table.GetColumn(0))));

  EXPECT_THAT(AreAllKeyColumnsWritableProperties(node_table),
              IsOkAndHolds(true));

  std::string error_message;
  SimpleGraphNodeTable unexposed_key_node_table(
      "NodeTable", graph_name_path, &base_table, /*key_cols=*/{0, 1},
      /*labels=*/{&label}, /*property_definitions=*/{});
  EXPECT_THAT(AreAllKeyColumnsWritableProperties(unexposed_key_node_table,
                                                 &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'id' is not exposed "
                        "as a writable property"));

  SimpleGraphNodeTable readonly_key_node_table(
      "NodeTable", graph_name_path, &base_table, /*key_cols=*/{2},
      /*labels=*/{&label}, /*property_definitions=*/{});
  error_message.clear();
  EXPECT_THAT(AreAllKeyColumnsWritableProperties(readonly_key_node_table,
                                                 &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'readonly_key' is not "
                        "writable"));
}

TEST(GraphElementUtilsTest, BackingColumnsAndIsTargetTableUpdatableTest) {
  TypeFactory type_factory;
  LanguageOptions updatable_lang_options;
  updatable_lang_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_UPDATABLE_GRAPH_ELEMENT_TYPE);
  const std::vector<std::string> graph_name_path = {"test_graph"};

  // Source node table with key column 0 ("id"), dynamic label column 1
  // ("dyn_label"), and dynamic properties column 2 ("dyn_props").
  std::vector<std::unique_ptr<const Column>> src_cols;
  src_cols.push_back(std::make_unique<SimpleColumn>(
      "SrcTable", "id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  src_cols.push_back(std::make_unique<SimpleColumn>(
      "SrcTable", "dyn_label", types::StringType(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  src_cols.push_back(std::make_unique<SimpleColumn>(
      "SrcTable", "dyn_props", types::JsonType(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable src_base_table("SrcTable", std::move(src_cols));

  SimpleGraphPropertyDeclaration id_decl(
      "id", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);
  auto src_id_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), src_base_table.GetColumn(0));
  auto src_id_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "id");
  InternalPropertyGraph::InternalSetResolvedExpr(src_id_prop_def.get(),
                                                 src_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> src_prop_defs;
  src_prop_defs.push_back(std::move(src_id_prop_def));

  auto dyn_label_col_ref = MakeResolvedCatalogColumnRef(
      types::StringType(), src_base_table.GetColumn(1));
  auto dyn_label = std::make_unique<SimpleGraphDynamicLabel>("dyn_label");
  InternalPropertyGraph::InternalSetResolvedExpr(dyn_label.get(),
                                                 dyn_label_col_ref.get());

  auto dyn_props_col_ref = MakeResolvedCatalogColumnRef(
      types::JsonType(), src_base_table.GetColumn(2));
  auto dyn_props = std::make_unique<SimpleGraphDynamicProperties>("dyn_props");
  InternalPropertyGraph::InternalSetResolvedExpr(dyn_props.get(),
                                                 dyn_props_col_ref.get());

  SimpleGraphElementLabel src_label("SrcLabel", graph_name_path, {&id_decl});
  SimpleGraphNodeTable src_node_table(
      "SrcTable", graph_name_path, &src_base_table, /*key_cols=*/{0},
      /*labels=*/{&src_label}, std::move(src_prop_defs), std::move(dyn_label),
      std::move(dyn_props));

  EXPECT_THAT(GetDynamicLabelBackingColumn(src_node_table),
              IsOkAndHolds(src_base_table.GetColumn(1)));
  EXPECT_THAT(GetDynamicPropertiesBackingColumn(src_node_table),
              IsOkAndHolds(src_base_table.GetColumn(2)));

  // Updatable when not inserting a dynamic label, but returns false when
  // needs_dynamic_label=true because dyn_label is read-only.
  EXPECT_THAT(IsTargetTableUpdatable(src_node_table,
                                     /*needs_dynamic_label=*/false),
              IsOkAndHolds(true));
  std::string error_message;
  EXPECT_THAT(
      IsTargetTableUpdatable(src_node_table,
                             /*needs_dynamic_label=*/true, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Cannot insert dynamic label because the dynamic "
                        "label backing column 'dyn_label' is read-only"));

  // BuildGraphElementTypeForTable for dynamic node table.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const GraphElementType* src_elem_type,
      BuildGraphElementTypeForTable(src_node_table, graph_name_path,
                                    type_factory, updatable_lang_options));
  EXPECT_TRUE(src_elem_type->IsNode());
  EXPECT_TRUE(src_elem_type->is_dynamic());
  EXPECT_THAT(src_elem_type->element_tables(), ElementsAre("SrcTable"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const GraphElementType* non_updatable_src_elem_type,
      BuildGraphElementTypeForTable(src_node_table, graph_name_path,
                                    type_factory, LanguageOptions()));
  EXPECT_THAT(non_updatable_src_elem_type->element_tables(), IsEmpty());

  // Destination node table whose key column is not exposed as a property.
  std::vector<std::unique_ptr<const Column>> dst_cols;
  dst_cols.push_back(std::make_unique<SimpleColumn>(
      "DstTable", "dst_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable dst_base_table("DstTable", std::move(dst_cols));
  SimpleGraphNodeTable dst_node_table(
      "DstTable", graph_name_path, &dst_base_table, /*key_cols=*/{0},
      /*labels=*/{&src_label}, /*property_definitions=*/{});

  EXPECT_THAT(GetDynamicLabelBackingColumn(dst_node_table),
              IsOkAndHolds(nullptr));
  EXPECT_THAT(GetDynamicPropertiesBackingColumn(dst_node_table),
              IsOkAndHolds(nullptr));

  // Edge table referencing src_node_table and dst_node_table.
  std::vector<std::unique_ptr<const Column>> edge_cols;
  edge_cols.push_back(std::make_unique<SimpleColumn>(
      "EdgeTable", "edge_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  edge_cols.push_back(std::make_unique<SimpleColumn>(
      "EdgeTable", "src_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  edge_cols.push_back(std::make_unique<SimpleColumn>(
      "EdgeTable", "dst_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable edge_base_table("EdgeTable", std::move(edge_cols));

  auto edge_id_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), edge_base_table.GetColumn(0));
  auto edge_id_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(edge_id_prop_def.get(),
                                                 edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> edge_prop_defs;
  edge_prop_defs.push_back(std::move(edge_id_prop_def));

  auto src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &dst_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{0});

  SimpleGraphEdgeTable edge_table(
      "EdgeTable", graph_name_path, &edge_base_table, /*key_cols=*/{0},
      /*labels=*/{&src_label}, std::move(edge_prop_defs), std::move(src_ref),
      std::move(dst_ref));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(EdgeEndpointBackingColumns endpoint_cols,
                       GetEdgeEndpointBackingColumns(edge_table));
  EXPECT_THAT(endpoint_cols.source_key_columns,
              UnorderedElementsAre(edge_base_table.GetColumn(1)));
  EXPECT_THAT(endpoint_cols.dest_key_columns,
              UnorderedElementsAre(edge_base_table.GetColumn(2)));

  // Edge table is not updatable because dst_node_table does not expose dst_id
  // as a property.
  error_message.clear();
  EXPECT_THAT(IsTargetTableUpdatable(edge_table, /*needs_dynamic_label=*/false,
                                     &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("The edge table 'EdgeTable' is not updatable because "
                        "the destination node table 'DstTable' does not "
                        "expose the element key column 'dst_id' as a "
                        "property"));

  // Edge table referencing src_node_table as both source and destination is
  // updatable because src_node_table exposes its key column as a property and
  // all edge key and endpoint columns are writable.
  auto updatable_edge_id_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      updatable_edge_id_prop_def.get(), edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      updatable_edge_prop_defs;
  updatable_edge_prop_defs.push_back(std::move(updatable_edge_id_prop_def));
  auto updatable_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto updatable_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{0});
  SimpleGraphEdgeTable updatable_edge_table(
      "UpdatableEdgeTable", graph_name_path, &edge_base_table, /*key_cols=*/{0},
      /*labels=*/{&src_label}, std::move(updatable_edge_prop_defs),
      std::move(updatable_src_ref), std::move(updatable_dst_ref));
  EXPECT_THAT(IsTargetTableUpdatable(updatable_edge_table,
                                     /*needs_dynamic_label=*/false),
              IsOkAndHolds(true));
  error_message.clear();
  EXPECT_THAT(
      IsTargetTableUpdatable(updatable_edge_table,
                             /*needs_dynamic_label=*/true, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message, HasSubstr("Graph element table "
                                       "'UpdatableEdgeTable' does not support "
                                       "dynamic labels"));

  // BuildGraphElementTypeForTable for static edge table.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const GraphElementType* edge_elem_type,
      BuildGraphElementTypeForTable(edge_table, graph_name_path, type_factory,
                                    updatable_lang_options));
  EXPECT_TRUE(edge_elem_type->IsEdge());
  EXPECT_FALSE(edge_elem_type->is_dynamic());
  EXPECT_THAT(edge_elem_type->element_tables(), ElementsAre("EdgeTable"));
}

}  // namespace
}  // namespace googlesql
