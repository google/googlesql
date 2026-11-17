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
#include "absl/status/status.h"
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
using ::absl_testing::StatusIs;

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
      /*element_tables=*/{}, &node_type));
  ASSERT_THAT(node_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(node_type));

  // Edge type
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"aml"}, GraphElementType::kEdge,
      {{"transfer_id", int_type}, {"amount", int_type}},
      /*element_tables=*/{}, &edge_type));
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
     GetPropertyBackingColumnsAndKeyColumnPropertiesTest) {
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
  SimpleGraphPropertyDeclaration readonly_key_decl(
      "readonly_key", graph_name_path, types::Int64Type(),
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

  EXPECT_THAT(GetElementKeyColumns(node_table),
              IsOkAndHolds(ElementsAre(base_table.GetColumn(0))));

  EXPECT_THAT(AreAllKeyColumnsExposedAsProperties(node_table,
                                                  /*must_be_writable=*/false),
              IsOkAndHolds(true));
  EXPECT_THAT(AreAllKeyColumnsExposedAsProperties(node_table,
                                                  /*must_be_writable=*/true),
              IsOkAndHolds(true));

  std::string error_message;
  SimpleGraphNodeTable unexposed_key_node_table(
      "NodeTable", graph_name_path, &base_table, /*key_cols=*/{0, 1},
      /*labels=*/{&label}, /*property_definitions=*/{});
  EXPECT_THAT(GetElementKeyColumns(unexposed_key_node_table),
              IsOkAndHolds(ElementsAre(base_table.GetColumn(0),
                                       base_table.GetColumn(1))));
  EXPECT_THAT(
      AreAllKeyColumnsExposedAsProperties(
          unexposed_key_node_table, /*must_be_writable=*/false, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'id' is not exposed "
                        "as a property"));

  error_message.clear();
  EXPECT_THAT(
      AreAllKeyColumnsExposedAsProperties(
          unexposed_key_node_table, /*must_be_writable=*/true, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'id' is not exposed "
                        "as a writable property"));

  // Key column is read-only and not exposed as a property.
  SimpleGraphNodeTable readonly_unexposed_key_node_table(
      "NodeTable", graph_name_path, &base_table, /*key_cols=*/{2},
      /*labels=*/{&label}, /*property_definitions=*/{});
  error_message.clear();
  EXPECT_THAT(AreAllKeyColumnsExposedAsProperties(
                  readonly_unexposed_key_node_table, /*must_be_writable=*/false,
                  &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'readonly_key' is not "
                        "exposed as a property"));

  error_message.clear();
  EXPECT_THAT(AreAllKeyColumnsExposedAsProperties(
                  readonly_unexposed_key_node_table, /*must_be_writable=*/true,
                  &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'readonly_key' is not "
                        "writable"));

  // Key column is read-only, but is exposed as a property:
  // AreAllKeyColumnsExposedAsProperties succeeds when must_be_writable=false,
  // and fails when must_be_writable=true.
  auto readonly_key_col_ref =
      MakeResolvedCatalogColumnRef(types::Int64Type(), base_table.GetColumn(2));
  auto readonly_key_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      &readonly_key_decl, "readonly_key");
  InternalPropertyGraph::InternalSetResolvedExpr(readonly_key_prop_def.get(),
                                                 readonly_key_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      readonly_key_prop_defs;
  readonly_key_prop_defs.push_back(std::move(readonly_key_prop_def));
  SimpleGraphElementLabel readonly_key_label(
      "ReadonlyKeyLabel", graph_name_path, {&readonly_key_decl});
  SimpleGraphNodeTable readonly_exposed_key_node_table(
      "NodeTable", graph_name_path, &base_table, /*key_cols=*/{2},
      /*labels=*/{&readonly_key_label}, std::move(readonly_key_prop_defs));
  EXPECT_THAT(
      AreAllKeyColumnsExposedAsProperties(readonly_exposed_key_node_table,
                                          /*must_be_writable=*/false),
      IsOkAndHolds(true));
  error_message.clear();
  EXPECT_THAT(AreAllKeyColumnsExposedAsProperties(
                  readonly_exposed_key_node_table,
                  /*must_be_writable=*/true, &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'NodeTable' is not updatable "
                        "because the element key column 'readonly_key' is not "
                        "writable"));
}

TEST(GraphElementUtilsTest, BackingColumnsAndIsTargetTableInsertableTest) {
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

  // Insertable when not inserting a dynamic label, but returns false when
  // needs_dynamic_label=true because dyn_label is read-only.
  EXPECT_THAT(IsTargetTableInsertable(src_node_table,
                                      /*needs_dynamic_label=*/false),
              IsOkAndHolds(true));
  std::string error_message;
  EXPECT_THAT(
      IsTargetTableInsertable(src_node_table,
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

  // Edge table is not insertable because dst_node_table does not expose dst_id
  // as a property.
  error_message.clear();
  EXPECT_THAT(IsTargetTableInsertable(edge_table, /*needs_dynamic_label=*/false,
                                      &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("The edge table 'EdgeTable' is not updatable because "
                        "the destination node table 'DstTable' does not "
                        "expose the element key column 'dst_id' as a "
                        "property"));

  // Edge table referencing src_node_table as both source and destination is
  // insertable because src_node_table exposes its key column as a property and
  // all edge key and endpoint columns are writable.
  auto insertable_edge_id_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      insertable_edge_id_prop_def.get(), edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      insertable_edge_prop_defs;
  insertable_edge_prop_defs.push_back(std::move(insertable_edge_id_prop_def));
  auto insertable_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto insertable_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{0});
  SimpleGraphEdgeTable insertable_edge_table(
      "InsertableEdgeTable", graph_name_path, &edge_base_table,
      /*key_cols=*/{0},
      /*labels=*/{&src_label}, std::move(insertable_edge_prop_defs),
      std::move(insertable_src_ref), std::move(insertable_dst_ref));
  EXPECT_THAT(IsTargetTableInsertable(insertable_edge_table,
                                      /*needs_dynamic_label=*/false),
              IsOkAndHolds(true));
  error_message.clear();
  EXPECT_THAT(
      IsTargetTableInsertable(insertable_edge_table,
                              /*needs_dynamic_label=*/true, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message, HasSubstr("Graph element table "
                                       "'InsertableEdgeTable' does not support "
                                       "dynamic labels"));

  // Edge table with read-only element key and read-only endpoint key columns
  // is not insertable.
  std::vector<std::unique_ptr<const Column>> readonly_edge_cols;
  readonly_edge_cols.push_back(std::make_unique<SimpleColumn>(
      "ReadonlyEdgeTable", "edge_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  readonly_edge_cols.push_back(std::make_unique<SimpleColumn>(
      "ReadonlyEdgeTable", "src_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  readonly_edge_cols.push_back(std::make_unique<SimpleColumn>(
      "ReadonlyEdgeTable", "dst_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  SimpleTable readonly_edge_base_table("ReadonlyEdgeTable",
                                       std::move(readonly_edge_cols));
  auto readonly_edge_id_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), readonly_edge_base_table.GetColumn(0));
  auto readonly_edge_id_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      readonly_edge_id_prop_def.get(), readonly_edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      readonly_edge_prop_defs;
  readonly_edge_prop_defs.push_back(std::move(readonly_edge_id_prop_def));
  auto readonly_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto readonly_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{0});
  SimpleGraphEdgeTable readonly_edge_table(
      "ReadonlyEdgeTable", graph_name_path, &readonly_edge_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(readonly_edge_prop_defs), std::move(readonly_src_ref),
      std::move(readonly_dst_ref));
  EXPECT_THAT(IsTargetTableInsertable(readonly_edge_table,
                                      /*needs_dynamic_label=*/false),
              IsOkAndHolds(false));

  SimpleGraphPropertyDeclaration ref_decl(
      "ref_col", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);

  // 1. Endpoint node table exposes its referenced column as a property, but
  //    does not expose its own element key column -> IsTargetTableInsertable
  //    returns false with "does not expose the element key column".
  std::vector<std::unique_ptr<const Column>> unexposed_key_cols;
  unexposed_key_cols.push_back(std::make_unique<SimpleColumn>(
      "UnexposedKeyNodeTable", "node_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  unexposed_key_cols.push_back(std::make_unique<SimpleColumn>(
      "UnexposedKeyNodeTable", "ref_col", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable unexposed_key_base_table("UnexposedKeyNodeTable",
                                       std::move(unexposed_key_cols));
  auto ref_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), unexposed_key_base_table.GetColumn(1));
  auto ref_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&ref_decl, "ref_col");
  InternalPropertyGraph::InternalSetResolvedExpr(ref_prop_def.get(),
                                                 ref_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      unexposed_key_prop_defs;
  unexposed_key_prop_defs.push_back(std::move(ref_prop_def));
  SimpleGraphNodeTable unexposed_key_node_table(
      "UnexposedKeyNodeTable", graph_name_path, &unexposed_key_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(unexposed_key_prop_defs));

  auto unexposed_key_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto unexposed_key_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &unexposed_key_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{1});
  auto unexposed_key_edge_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      unexposed_key_edge_prop_def.get(), edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      unexposed_key_edge_prop_defs;
  unexposed_key_edge_prop_defs.push_back(
      std::move(unexposed_key_edge_prop_def));
  SimpleGraphEdgeTable unexposed_key_edge_table(
      "UnexposedKeyEdgeTable", graph_name_path, &edge_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(unexposed_key_edge_prop_defs), std::move(unexposed_key_src_ref),
      std::move(unexposed_key_dst_ref));
  error_message.clear();
  EXPECT_THAT(
      IsTargetTableInsertable(unexposed_key_edge_table,
                              /*needs_dynamic_label=*/false, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("The edge table 'UnexposedKeyEdgeTable' is not "
                        "updatable because the destination node table "
                        "'UnexposedKeyNodeTable' does not expose the element "
                        "key column 'node_key' as a property"));

  // 2. Endpoint node table exposes its own element key column as a property,
  //    but does not expose a non-key referenced column ->
  //    IsTargetTableInsertable returns false with "does not expose the
  //    referenced column".
  std::vector<std::unique_ptr<const Column>> unexposed_ref_cols;
  unexposed_ref_cols.push_back(std::make_unique<SimpleColumn>(
      "UnexposedRefNodeTable", "node_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  unexposed_ref_cols.push_back(std::make_unique<SimpleColumn>(
      "UnexposedRefNodeTable", "ref_col", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable unexposed_ref_base_table("UnexposedRefNodeTable",
                                       std::move(unexposed_ref_cols));
  auto node_key_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), unexposed_ref_base_table.GetColumn(0));
  auto node_key_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "node_key");
  InternalPropertyGraph::InternalSetResolvedExpr(node_key_prop_def.get(),
                                                 node_key_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      unexposed_ref_prop_defs;
  unexposed_ref_prop_defs.push_back(std::move(node_key_prop_def));
  SimpleGraphNodeTable unexposed_ref_node_table(
      "UnexposedRefNodeTable", graph_name_path, &unexposed_ref_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(unexposed_ref_prop_defs));

  auto unexposed_ref_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto unexposed_ref_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &unexposed_ref_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{1});
  auto unexposed_ref_edge_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      unexposed_ref_edge_prop_def.get(), edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      unexposed_ref_edge_prop_defs;
  unexposed_ref_edge_prop_defs.push_back(
      std::move(unexposed_ref_edge_prop_def));
  SimpleGraphEdgeTable unexposed_ref_edge_table(
      "UnexposedRefEdgeTable", graph_name_path, &edge_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(unexposed_ref_edge_prop_defs), std::move(unexposed_ref_src_ref),
      std::move(unexposed_ref_dst_ref));
  error_message.clear();
  EXPECT_THAT(
      IsTargetTableInsertable(unexposed_ref_edge_table,
                              /*needs_dynamic_label=*/false, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("The edge table 'UnexposedRefEdgeTable' is not "
                        "updatable because the destination node table "
                        "'UnexposedRefNodeTable' does not expose the "
                        "referenced column 'ref_col' as a property"));

  // 3. Edge table whose source and destination references share an underlying
  //    edge base table column -> IsTargetTableInsertable returns false with
  //    "is not updatable because its source and destination keys share
  //    underlying edge table column".
  auto shared_col_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto shared_col_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto shared_col_edge_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(shared_col_edge_prop_def.get(),
                                                 edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      shared_col_edge_prop_defs;
  shared_col_edge_prop_defs.push_back(std::move(shared_col_edge_prop_def));
  SimpleGraphEdgeTable shared_col_edge_table(
      "SharedColEdgeTable", graph_name_path, &edge_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(shared_col_edge_prop_defs), std::move(shared_col_src_ref),
      std::move(shared_col_dst_ref));
  error_message.clear();
  EXPECT_THAT(
      IsTargetTableInsertable(shared_col_edge_table,
                              /*needs_dynamic_label=*/false, &error_message),
      IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("The edge table 'SharedColEdgeTable' is not updatable "
                        "because its source and destination keys share "
                        "underlying edge table column 'src_id'"));

  // 4. Positive: Endpoint node table references a non-key column and exposes
  //    both its element key column and the referenced column as properties ->
  //    IsTargetTableInsertable returns true.
  std::vector<std::unique_ptr<const Column>> exposed_ref_cols;
  exposed_ref_cols.push_back(std::make_unique<SimpleColumn>(
      "ExposedRefNodeTable", "node_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  exposed_ref_cols.push_back(std::make_unique<SimpleColumn>(
      "ExposedRefNodeTable", "ref_col", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  SimpleTable exposed_ref_base_table("ExposedRefNodeTable",
                                     std::move(exposed_ref_cols));
  auto exposed_node_key_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), exposed_ref_base_table.GetColumn(0));
  auto exposed_node_key_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "node_key");
  InternalPropertyGraph::InternalSetResolvedExpr(
      exposed_node_key_prop_def.get(), exposed_node_key_col_ref.get());
  auto exposed_ref_col_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), exposed_ref_base_table.GetColumn(1));
  auto exposed_ref_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&ref_decl, "ref_col");
  InternalPropertyGraph::InternalSetResolvedExpr(exposed_ref_prop_def.get(),
                                                 exposed_ref_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      exposed_ref_prop_defs;
  exposed_ref_prop_defs.push_back(std::move(exposed_node_key_prop_def));
  exposed_ref_prop_defs.push_back(std::move(exposed_ref_prop_def));
  SimpleGraphNodeTable exposed_ref_node_table(
      "ExposedRefNodeTable", graph_name_path, &exposed_ref_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(exposed_ref_prop_defs));

  auto exposed_ref_src_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &src_node_table, /*edge_table_columns=*/std::vector<int>{1},
      /*node_table_columns=*/std::vector<int>{0});
  auto exposed_ref_dst_ref = std::make_unique<SimpleGraphNodeTableReference>(
      &exposed_ref_node_table, /*edge_table_columns=*/std::vector<int>{2},
      /*node_table_columns=*/std::vector<int>{1});
  auto exposed_ref_edge_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      exposed_ref_edge_prop_def.get(), edge_id_col_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      exposed_ref_edge_prop_defs;
  exposed_ref_edge_prop_defs.push_back(std::move(exposed_ref_edge_prop_def));
  SimpleGraphEdgeTable exposed_ref_edge_table(
      "ExposedRefEdgeTable", graph_name_path, &edge_base_table,
      /*key_cols=*/{0}, /*labels=*/{&src_label},
      std::move(exposed_ref_edge_prop_defs), std::move(exposed_ref_src_ref),
      std::move(exposed_ref_dst_ref));
  error_message.clear();
  EXPECT_THAT(
      IsTargetTableInsertable(exposed_ref_edge_table,
                              /*needs_dynamic_label=*/false, &error_message),
      IsOkAndHolds(true));
  EXPECT_EQ(error_message, "");

  // BuildGraphElementTypeForTable for static edge table.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const GraphElementType* edge_elem_type,
      BuildGraphElementTypeForTable(edge_table, graph_name_path, type_factory,
                                    updatable_lang_options));
  EXPECT_TRUE(edge_elem_type->IsEdge());
  EXPECT_FALSE(edge_elem_type->is_dynamic());
  EXPECT_THAT(edge_elem_type->element_tables(), ElementsAre("EdgeTable"));
}

TEST(GraphElementUtilsTest, IsTargetTableUpdatableTest) {
  const std::vector<std::string> graph_name_path = {"test_graph"};
  SimpleGraphPropertyDeclaration id_decl(
      "id", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr, GraphPropertyDeclaration::Kind::kScalar);
  SimpleGraphPropertyDeclaration measure_decl(
      "m", graph_name_path, types::Int64Type(),
      /*type_annotation_map=*/nullptr,
      GraphPropertyDeclaration::Kind::kMeasure);
  SimpleGraphElementLabel label("ElementLabel", graph_name_path,
                                {&id_decl, &measure_decl});

  std::vector<std::unique_ptr<const Column>> node_cols;
  node_cols.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "writable_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = true}));
  node_cols.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "readonly_key", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  node_cols.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "dyn_label", types::StringType(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  node_cols.push_back(std::make_unique<SimpleColumn>(
      "NodeTable", "dyn_props", types::JsonType(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  SimpleTable node_base_table("NodeTable", std::move(node_cols));

  auto make_scalar_prop_def = [&id_decl, &node_base_table](int col_idx) {
    auto col_ref = MakeResolvedCatalogColumnRef(
        types::Int64Type(), node_base_table.GetColumn(col_idx));
    auto prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
        &id_decl, node_base_table.GetColumn(col_idx)->Name());
    InternalPropertyGraph::InternalSetResolvedExpr(prop_def.get(),
                                                   col_ref.get());
    return std::pair(std::move(col_ref), std::move(prop_def));
  };

  // 1. Node table with writable exposed key column is updatable.
  auto [writable_key_ref, writable_key_prop] = make_scalar_prop_def(0);
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      writable_key_props;
  writable_key_props.push_back(std::move(writable_key_prop));
  SimpleGraphNodeTable writable_key_node_table(
      "NodeTable", graph_name_path, &node_base_table, /*key_cols=*/{0},
      /*labels=*/{&label}, std::move(writable_key_props));
  EXPECT_THAT(IsTargetTableUpdatable(writable_key_node_table),
              IsOkAndHolds(true));

  // 2. Node table with read-only exposed key column (plus read-only dynamic
  // label and dynamic properties columns) is still updatable at the table
  // level.
  auto [readonly_key_ref, readonly_key_prop] = make_scalar_prop_def(1);
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      readonly_key_props;
  readonly_key_props.push_back(std::move(readonly_key_prop));
  auto dyn_label_ref = MakeResolvedCatalogColumnRef(
      types::StringType(), node_base_table.GetColumn(2));
  auto dyn_label = std::make_unique<SimpleGraphDynamicLabel>("dyn_label");
  InternalPropertyGraph::InternalSetResolvedExpr(dyn_label.get(),
                                                 dyn_label_ref.get());
  auto dyn_props_ref = MakeResolvedCatalogColumnRef(
      types::JsonType(), node_base_table.GetColumn(3));
  auto dyn_props = std::make_unique<SimpleGraphDynamicProperties>("dyn_props");
  InternalPropertyGraph::InternalSetResolvedExpr(dyn_props.get(),
                                                 dyn_props_ref.get());
  SimpleGraphNodeTable readonly_key_node_table(
      "NodeTable", graph_name_path, &node_base_table, /*key_cols=*/{1},
      /*labels=*/{&label}, std::move(readonly_key_props), std::move(dyn_label),
      std::move(dyn_props));
  EXPECT_THAT(IsTargetTableUpdatable(readonly_key_node_table),
              IsOkAndHolds(true));

  // 3. Node table whose key column is not exposed (or only exposed as a measure
  // property) is not updatable.
  auto measure_key_ref = MakeResolvedCatalogColumnRef(
      types::Int64Type(), node_base_table.GetColumn(0));
  auto measure_key_prop =
      std::make_unique<SimpleGraphPropertyDefinition>(&measure_decl, "m");
  InternalPropertyGraph::InternalSetResolvedExpr(measure_key_prop.get(),
                                                 measure_key_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> measure_key_props;
  measure_key_props.push_back(std::move(measure_key_prop));
  SimpleGraphNodeTable unexposed_key_node_table(
      "UnexposedNodeTable", graph_name_path, &node_base_table, /*key_cols=*/{0},
      /*labels=*/{&label}, std::move(measure_key_props));
  std::string error_message;
  EXPECT_THAT(IsTargetTableUpdatable(unexposed_key_node_table, &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'UnexposedNodeTable' is not "
                        "updatable because the element key column "
                        "'writable_key' is not exposed as a property"));

  // 4. Edge table with read-only element key, read-only endpoint key columns,
  // and an endpoint node table that does not expose its key column is updatable
  // iff the edge table exposes its own element key column.
  std::vector<std::unique_ptr<const Column>> edge_cols;
  edge_cols.push_back(std::make_unique<SimpleColumn>(
      "EdgeTable", "edge_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  edge_cols.push_back(std::make_unique<SimpleColumn>(
      "EdgeTable", "src_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  edge_cols.push_back(std::make_unique<SimpleColumn>(
      "EdgeTable", "dst_id", types::Int64Type(),
      SimpleColumn::Attributes{.is_writable_column = false}));
  SimpleTable edge_base_table("EdgeTable", std::move(edge_cols));

  auto edge_id_ref = MakeResolvedCatalogColumnRef(types::Int64Type(),
                                                  edge_base_table.GetColumn(0));
  auto edge_id_prop =
      std::make_unique<SimpleGraphPropertyDefinition>(&id_decl, "edge_id");
  InternalPropertyGraph::InternalSetResolvedExpr(edge_id_prop.get(),
                                                 edge_id_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> edge_props;
  edge_props.push_back(std::move(edge_id_prop));

  SimpleGraphEdgeTable exposed_edge_table(
      "EdgeTable", graph_name_path, &edge_base_table, /*key_cols=*/{0},
      /*labels=*/{&label}, std::move(edge_props),
      std::make_unique<SimpleGraphNodeTableReference>(
          &unexposed_key_node_table, /*edge_table_columns=*/std::vector<int>{1},
          /*node_table_columns=*/std::vector<int>{0}),
      std::make_unique<SimpleGraphNodeTableReference>(
          &unexposed_key_node_table, /*edge_table_columns=*/std::vector<int>{2},
          /*node_table_columns=*/std::vector<int>{0}));
  EXPECT_THAT(IsTargetTableUpdatable(exposed_edge_table), IsOkAndHolds(true));

  SimpleGraphEdgeTable unexposed_edge_table(
      "UnexposedEdgeTable", graph_name_path, &edge_base_table, /*key_cols=*/{0},
      /*labels=*/{&label}, /*property_definitions=*/{},
      std::make_unique<SimpleGraphNodeTableReference>(
          &writable_key_node_table, /*edge_table_columns=*/std::vector<int>{1},
          /*node_table_columns=*/std::vector<int>{0}),
      std::make_unique<SimpleGraphNodeTableReference>(
          &writable_key_node_table, /*edge_table_columns=*/std::vector<int>{2},
          /*node_table_columns=*/std::vector<int>{0}));
  error_message.clear();
  EXPECT_THAT(IsTargetTableUpdatable(unexposed_edge_table, &error_message),
              IsOkAndHolds(false));
  EXPECT_THAT(error_message,
              HasSubstr("Graph element table 'UnexposedEdgeTable' is not "
                        "updatable because the element key column 'edge_id' is "
                        "not exposed as a property"));
}

TEST(GraphElementUtilsTest, GetTargetElementTablesTest) {
  TypeFactory type_factory;
  const std::vector<std::string> graph_name_path = {"test_graph"};

  std::vector<std::unique_ptr<const Column>> cols_a;
  cols_a.push_back(
      std::make_unique<SimpleColumn>("TableA", "id", types::Int64Type()));
  SimpleTable base_table_a("TableA", std::move(cols_a));

  std::vector<std::unique_ptr<const Column>> cols_b;
  cols_b.push_back(
      std::make_unique<SimpleColumn>("TableB", "id", types::Int64Type()));
  SimpleTable base_table_b("TableB", std::move(cols_b));

  auto label = std::make_unique<SimpleGraphElementLabel>(
      "NodeLabel", graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{});
  const GraphElementLabel* label_ptr = label.get();

  auto node_table_a = std::make_unique<SimpleGraphNodeTable>(
      "TableA", graph_name_path, &base_table_a,
      /*key_cols=*/std::vector<int>{0},
      /*labels=*/absl::flat_hash_set<const GraphElementLabel*>{label_ptr},
      /*property_definitions=*/
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>{});
  const GraphNodeTable* node_table_a_ptr = node_table_a.get();

  auto node_table_b = std::make_unique<SimpleGraphNodeTable>(
      "TableB", graph_name_path, &base_table_b,
      /*key_cols=*/std::vector<int>{0},
      /*labels=*/absl::flat_hash_set<const GraphElementLabel*>{label_ptr},
      /*property_definitions=*/
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>{});
  const GraphNodeTable* node_table_b_ptr = node_table_b.get();

  std::vector<std::unique_ptr<const Column>> edge_cols;
  edge_cols.push_back(std::make_unique<SimpleColumn>("EdgeTable", "edge_id",
                                                     types::Int64Type()));
  edge_cols.push_back(std::make_unique<SimpleColumn>("EdgeTable", "src_id",
                                                     types::Int64Type()));
  edge_cols.push_back(std::make_unique<SimpleColumn>("EdgeTable", "dst_id",
                                                     types::Int64Type()));
  SimpleTable base_edge_table("EdgeTable", std::move(edge_cols));

  auto edge_table = std::make_unique<SimpleGraphEdgeTable>(
      "EdgeTable", graph_name_path, &base_edge_table,
      /*key_cols=*/std::vector<int>{0},
      /*labels=*/absl::flat_hash_set<const GraphElementLabel*>{label_ptr},
      /*property_definitions=*/
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>{},
      std::make_unique<SimpleGraphNodeTableReference>(
          node_table_a_ptr, /*edge_table_columns=*/std::vector<int>{1},
          /*node_table_columns=*/std::vector<int>{0}),
      std::make_unique<SimpleGraphNodeTableReference>(
          node_table_b_ptr, /*edge_table_columns=*/std::vector<int>{2},
          /*node_table_columns=*/std::vector<int>{0}));
  const GraphEdgeTable* edge_table_ptr = edge_table.get();

  SimplePropertyGraph graph(graph_name_path);
  graph.AddLabel(std::move(label));
  graph.AddNodeTable(std::move(node_table_a));
  graph.AddNodeTable(std::move(node_table_b));
  graph.AddEdgeTable(std::move(edge_table));

  // 1. Empty element_tables() returns an empty vector.
  const GraphElementType* empty_tables_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      graph_name_path, GraphElementType::ElementKind::kNode,
      /*property_types=*/{}, /*element_tables=*/{}, &empty_tables_type));
  EXPECT_THAT(GetTargetElementTables(graph, *empty_tables_type),
              IsOkAndHolds(IsEmpty()));

  // 2. Multiple element_tables() (including case-insensitive lookup) resolve in
  // exact order.
  const GraphElementType* multi_tables_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      graph_name_path, GraphElementType::ElementKind::kNode,
      /*property_types=*/{}, /*element_tables=*/{"tableb", "TableA"},
      &multi_tables_type));
  EXPECT_THAT(GetTargetElementTables(graph, *multi_tables_type),
              IsOkAndHolds(ElementsAre(node_table_b_ptr, node_table_a_ptr)));

  // 3. Edge element_tables() resolve to SimpleGraphEdgeTable.
  const GraphElementType* edge_tables_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      graph_name_path, GraphElementType::ElementKind::kEdge,
      /*property_types=*/{}, /*element_tables=*/{"EdgeTable"},
      &edge_tables_type));
  EXPECT_THAT(GetTargetElementTables(graph, *edge_tables_type),
              IsOkAndHolds(ElementsAre(edge_table_ptr)));

  // 4. Unknown table name returns NotFoundError.
  const GraphElementType* unknown_table_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      graph_name_path, GraphElementType::ElementKind::kNode,
      /*property_types=*/{}, /*element_tables=*/{"TableA", "MissingTable"},
      &unknown_table_type));
  EXPECT_THAT(GetTargetElementTables(graph, *unknown_table_type),
              StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace googlesql
