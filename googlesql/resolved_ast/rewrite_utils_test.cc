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

#include "googlesql/resolved_ast/rewrite_utils.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "google/protobuf/timestamp.pb.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/interval_value_test_util.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "googlesql/resolved_ast/test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

using ::googlesql::MakeResolvedTVFScan;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::Values;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

TEST(RewriteUtilsTest, CopyAndReplaceColumns) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<ResolvedScan> input = MakeResolvedTableScan(
      /*column_list=*/{factory.MakeCol("t", "c", types::Int64Type())},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);
  EXPECT_EQ(input->column_list(0).column_id(), 1);

  // Copy 'input' several times. The first time a new column is allocated but
  // subsequent copies will use the column already populated in 'map'.
  ColumnReplacementMap map;
  for (int i = 0; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ResolvedScan> output,
                         CopyResolvedASTAndRemapColumns(*input, factory, map));
    EXPECT_EQ(output->column_list(0).column_id(), 2);
    EXPECT_EQ(map.size(), 1);
  }

  // Repeat the experiment but feed the output of each iteration into the
  // input of the next. In this case we should get a new column each iteration
  // with a incremented column_id.
  map = {};
  for (int i = 1; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ResolvedScan> output,
                         CopyResolvedASTAndRemapColumns(*input, factory, map));
    // 2 columns for setup and first loop plus 1 for each iteration of this loop
    EXPECT_EQ(output->column_list(0).column_id(), i + 2);
    EXPECT_EQ(map.size(), i);
    input = std::move(output);
  }
}

TEST(RewriteUtilsTest, GetMaxColumnId) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);

  ResolvedColumn col1 = factory.MakeCol("t", "c1", types::Int64Type());  // id 1
  ResolvedColumn col2 = factory.MakeCol("t", "c2", types::Int64Type());  // id 2

  auto column_ref1 =
      MakeResolvedColumnRef(types::Int64Type(), col1, /*is_correlated=*/false);
  auto column_ref2 =
      MakeResolvedColumnRef(types::Int64Type(), col2, /*is_correlated=*/false);

  // Scan with col1
  auto scan = MakeResolvedTableScan({col1}, /*table=*/nullptr,
                                    /*for_system_time_expr=*/nullptr);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(int max_id, GetMaxColumnId(scan.get()));
  EXPECT_EQ(max_id, 1);

  // Expression with col1
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(max_id, GetMaxColumnId(column_ref1.get()));
  EXPECT_EQ(max_id, 1);

  // Expression with col2
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(max_id, GetMaxColumnId(column_ref2.get()));
  EXPECT_EQ(max_id, 2);

  // Computed column with both
  auto computed = MakeResolvedComputedColumn(col1, std::move(column_ref2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(max_id, GetMaxColumnId(computed.get()));
  EXPECT_EQ(max_id, 2);
}

TEST(RewriteUtilsTest, ContainsResolvedColumnVisitor) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  ResolvedColumn col = factory.MakeCol("t", "c", types::Int64Type());

  // Test scan with a ResolvedColumn.
  {
    ContainsResolvedColumnVisitor visitor;
    auto scan = MakeResolvedTableScan({col}, /*table=*/nullptr,
                                      /*for_system_time_expr=*/nullptr);
    GOOGLESQL_ASSERT_OK(scan->Accept(&visitor));
    EXPECT_TRUE(visitor.ContainsResolvedColumn());
    EXPECT_FALSE(visitor.ContainsResolvedExpressionColumn());
  }

  // Test ResolvedColumnRef containing a ResolvedColumn.
  {
    ContainsResolvedColumnVisitor visitor;
    auto column_ref =
        MakeResolvedColumnRef(types::Int64Type(), col, /*is_correlated=*/false);
    GOOGLESQL_ASSERT_OK(column_ref->Accept(&visitor));
    EXPECT_TRUE(visitor.ContainsResolvedColumn());
    EXPECT_FALSE(visitor.ContainsResolvedExpressionColumn());
  }

  // Test literal which has no columns.
  {
    ContainsResolvedColumnVisitor visitor;
    auto literal = MakeResolvedLiteral(Value::Int64(1));
    GOOGLESQL_ASSERT_OK(literal->Accept(&visitor));
    EXPECT_FALSE(visitor.ContainsResolvedColumn());
    EXPECT_FALSE(visitor.ContainsResolvedExpressionColumn());
  }

  // Test ResolvedExpressionColumn.
  {
    ContainsResolvedColumnVisitor visitor;
    auto expr_col = MakeResolvedExpressionColumn(types::Int64Type(), "name");
    GOOGLESQL_ASSERT_OK(expr_col->Accept(&visitor));
    EXPECT_FALSE(visitor.ContainsResolvedColumn());
    EXPECT_TRUE(visitor.ContainsResolvedExpressionColumn());
  }
}

TEST(RewriteUtilsTest, ContainsResolvedColumnVisitorVisitAll) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  ResolvedColumn col = factory.MakeCol("t", "c", types::Int64Type());

  // Test VisitAll with a ResolvedColumn.
  {
    ContainsResolvedColumnVisitor visitor;
    auto scan = MakeResolvedTableScan({col}, /*table=*/nullptr,
                                      /*for_system_time_expr=*/nullptr);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto result, visitor.VisitAll(std::move(scan)));
    EXPECT_TRUE(visitor.ContainsResolvedColumn());
    EXPECT_FALSE(visitor.ContainsResolvedExpressionColumn());
    EXPECT_NE(result, nullptr);
  }

  // Test VisitAll with a ResolvedExpressionColumn.
  {
    ContainsResolvedColumnVisitor visitor;
    auto expr_col = MakeResolvedExpressionColumn(types::Int64Type(), "name");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto result, visitor.VisitAll(std::move(expr_col)));
    EXPECT_FALSE(visitor.ContainsResolvedColumn());
    EXPECT_TRUE(visitor.ContainsResolvedExpressionColumn());
    EXPECT_NE(result, nullptr);
  }
}

TEST(RewriteUtilsTest, ShallowCopyAndReplaceAllColumns) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<const ResolvedTableScan> input = MakeResolvedTableScan(
      /*column_list=*/{factory.MakeCol("t", "c", types::Int64Type())},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);
  EXPECT_EQ(input->column_list(0).column_id(), 1);

  // Shallow copy `input` several times, feeding the output of each iteration
  // into the input of the next. In this case we should get a new column each
  // iteration with a incremented column_id.
  ColumnReplacementMap map;
  for (int i = 1; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(input,
                         RemapAllColumns(std::move(input), factory, map));

    EXPECT_EQ(input->column_list(0).column_id(), i + 1);
    EXPECT_EQ(map.size(), i);
  }
}

TEST(RewriteUtilsTest, ShallowCopyAndReplaceSpecifiedColumns) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<const ResolvedTableScan> input = MakeResolvedTableScan(
      /*column_list=*/{factory.MakeCol("t", "c1", types::Int64Type()),
                       factory.MakeCol("t", "c2", types::StringType())},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);
  EXPECT_EQ(input->column_list(0).column_id(), 1);

  // Rewrite `input` several times. When the map is empty, it should be a no-op.
  ColumnReplacementMap map;
  for (int i = 0; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(input, RemapSpecifiedColumns(std::move(input), map));
    EXPECT_EQ(input->column_list(0).column_id(), 1);
    EXPECT_EQ(map.size(), 0);
  }

  map[input->column_list(0)] = factory.MakeCol("t", "c3", types::Int64Type());
  // Repeat the experiment with a non-empty map. Only the first iteration
  // should be a no-op.
  for (int i = 1; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedTableScan> output,
                         RemapSpecifiedColumns(std::move(input), map));
    // The first column should be replaced with a new column once.
    EXPECT_EQ(output->column_list(0).column_id(), 3);
    EXPECT_EQ(map.size(), 1);
    input = std::move(output);
  }
}

TEST(RewriteUtilsTest, RemapSpecifiedColumnsInColumnList) {
  IdStringPool id_string_pool;
  ColumnFactory factory(0, id_string_pool,
                        std::make_unique<googlesql_base::SequenceNumber>());
  ResolvedColumnList columns;
  columns.emplace_back(factory.MakeCol("t", "c1", types::Int64Type()));
  columns.emplace_back(factory.MakeCol("t", "c2", types::StringType()));
  EXPECT_EQ(columns[0].column_id(), 1);
  EXPECT_EQ(columns[0].name(), "c1");
  EXPECT_EQ(columns[1].column_id(), 2);

  ColumnReplacementMap map;
  map[columns[0]] = factory.MakeCol("t", "c3", types::DoubleType());
  ResolvedColumnList output = RemapColumnList(columns, map);
  EXPECT_EQ(output[0].column_id(), 3);
  EXPECT_EQ(output[0].name(), "c3");
  EXPECT_EQ(output[1].column_id(), 2);
}

TEST(RewriteUtilsTest, RemoveUnusedColumnRefs) {
  const Type* type = types::BoolType();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  column_refs.emplace_back(
      MakeResolvedColumnRef(type, cola, /*is_correlated=*/true));
  column_refs.emplace_back(
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/false));

  std::unique_ptr<const ResolvedScan> single_row_scan =
      MakeResolvedSingleRowScan(/*column_list=*/{});
  GOOGLESQL_ASSERT_OK(RemoveUnusedColumnRefs(*single_row_scan, column_refs));
  EXPECT_THAT(column_refs, IsEmpty());

  std::unique_ptr<const ResolvedExpr> filter_expr =
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/true);
  std::unique_ptr<const ResolvedScan> filter_scan = MakeResolvedFilterScan(
      /*column_list=*/{},
      /*input_scan=*/std::move(single_row_scan),
      /*filter_expr=*/std::move(filter_expr));

  column_refs.emplace_back(
      MakeResolvedColumnRef(type, cola, /*is_correlated=*/true));
  column_refs.emplace_back(
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK(RemoveUnusedColumnRefs(*filter_scan, column_refs));
  EXPECT_THAT(column_refs, SizeIs(1));
  EXPECT_EQ(column_refs.front()->column(), colb);
}

TEST(RewriteUtilsTest, PruneUnusedCorrelatedParameters_SubqueryExpr) {
  const Type* type = types::BoolType();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);

  // Build a subquery whose scan only references `colb`, while `in_expr`
  // references `cola` in the enclosing scope and `parameter_list` lists both
  // `cola` and `colb`.
  auto subquery_scan = MakeResolvedFilterScan(
      /*column_list=*/{},
      /*input_scan=*/MakeResolvedSingleRowScan(/*column_list=*/{}),
      /*filter_expr=*/
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/true));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedSubqueryExpr> subquery_expr,
      ResolvedSubqueryExprBuilder()
          .set_type(type)
          .set_subquery_type(ResolvedSubqueryExpr::SCALAR)
          .set_in_expr(
              MakeResolvedColumnRef(type, cola, /*is_correlated=*/false))
          .add_parameter_list(
              MakeResolvedColumnRef(type, cola, /*is_correlated=*/false))
          .add_parameter_list(
              MakeResolvedColumnRef(type, colb, /*is_correlated=*/false))
          .set_subquery(std::move(subquery_scan))
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedNode> pruned_node,
      PruneUnusedCorrelatedParameters(std::move(subquery_expr)));
  const auto* pruned_subquery = pruned_node->GetAs<ResolvedSubqueryExpr>();
  ASSERT_THAT(pruned_subquery->parameter_list(), SizeIs(1));
  EXPECT_EQ(pruned_subquery->parameter_list(0)->column(), colb);
}

TEST(RewriteUtilsTest, PruneUnusedCorrelatedParameters_InlineLambda) {
  const Type* type = types::Int64Type();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn arg_col = factory.MakeCol("lambda", "a", type);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedInlineLambda> lambda,
      ResolvedInlineLambdaBuilder()
          .add_argument_list(arg_col)
          .add_parameter_list(
              MakeResolvedColumnRef(type, cola, /*is_correlated=*/false))
          .add_parameter_list(
              MakeResolvedColumnRef(type, colb, /*is_correlated=*/false))
          .set_body(MakeResolvedColumnRef(type, colb, /*is_correlated=*/true))
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedNode> pruned_node,
                       PruneUnusedCorrelatedParameters(std::move(lambda)));
  const auto* pruned_lambda = pruned_node->GetAs<ResolvedInlineLambda>();
  ASSERT_THAT(pruned_lambda->parameter_list(), SizeIs(1));
  EXPECT_EQ(pruned_lambda->parameter_list(0)->column(), colb);
}

TEST(RewriteUtilsTest, PruneUnusedCorrelatedParameters_LateralJoinScan) {
  const Type* type = types::BoolType();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);

  // `left_scan` references `cola`, while `right_scan` only references `colb`.
  // `JoinScan::parameter_list` scopes to `right_scan`, so `cola` should be
  // pruned even though it appears in `left_scan`.
  auto left_scan = MakeResolvedFilterScan(
      /*column_list=*/{},
      /*input_scan=*/MakeResolvedSingleRowScan(/*column_list=*/{}),
      /*filter_expr=*/
      MakeResolvedColumnRef(type, cola, /*is_correlated=*/false));
  auto right_scan = MakeResolvedFilterScan(
      /*column_list=*/{},
      /*input_scan=*/MakeResolvedSingleRowScan(/*column_list=*/{}),
      /*filter_expr=*/
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/true));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedJoinScan> join_scan,
                       ResolvedJoinScanBuilder()
                           .set_join_type(ResolvedJoinScan::INNER)
                           .set_left_scan(std::move(left_scan))
                           .set_right_scan(std::move(right_scan))
                           .set_is_lateral(true)
                           .add_parameter_list(MakeResolvedColumnRef(
                               type, cola, /*is_correlated=*/false))
                           .add_parameter_list(MakeResolvedColumnRef(
                               type, colb, /*is_correlated=*/false))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedNode> pruned_node,
                       PruneUnusedCorrelatedParameters(std::move(join_scan)));
  const auto* pruned_join = pruned_node->GetAs<ResolvedJoinScan>();
  ASSERT_THAT(pruned_join->parameter_list(), SizeIs(1));
  EXPECT_EQ(pruned_join->parameter_list(0)->column(), colb);
}

TEST(RewriteUtilsTest, PruneUnusedCorrelatedParameters_GraphCallScan) {
  const Type* type = types::BoolType();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);

  // `input_scan` references `cola`, while `subquery` only references `colb`.
  // `GraphCallScan::parameter_list` scopes to `subquery`, so `cola` should be
  // pruned.
  auto input_scan = MakeResolvedFilterScan(
      /*column_list=*/{},
      /*input_scan=*/MakeResolvedSingleRowScan(/*column_list=*/{}),
      /*filter_expr=*/
      MakeResolvedColumnRef(type, cola, /*is_correlated=*/false));
  auto subquery_scan = MakeResolvedFilterScan(
      /*column_list=*/{},
      /*input_scan=*/MakeResolvedSingleRowScan(/*column_list=*/{}),
      /*filter_expr=*/
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/true));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedGraphCallScan> call_scan,
                       ResolvedGraphCallScanBuilder()
                           .set_optional(false)
                           .set_input_scan(std::move(input_scan))
                           .set_subquery(std::move(subquery_scan))
                           .add_parameter_list(MakeResolvedColumnRef(
                               type, cola, /*is_correlated=*/false))
                           .add_parameter_list(MakeResolvedColumnRef(
                               type, colb, /*is_correlated=*/false))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedNode> pruned_node,
                       PruneUnusedCorrelatedParameters(std::move(call_scan)));
  const auto* pruned_call = pruned_node->GetAs<ResolvedGraphCallScan>();
  ASSERT_THAT(pruned_call->parameter_list(), SizeIs(1));
  EXPECT_EQ(pruned_call->parameter_list(0)->column(), colb);
}

TEST(RewriteUtilsTest, PruneUnusedCorrelatedParameters_NestedBottomUp) {
  const Type* type = types::Int64Type();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);
  ResolvedColumn inner_out = factory.MakeCol("inner", "out", type);
  ResolvedColumn outer_out = factory.MakeCol("outer", "out", type);

  // Inner subquery lists `cola` in its `parameter_list`, but its body does not
  // reference `cola`.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedSubqueryExpr> inner_subquery,
      ResolvedSubqueryExprBuilder()
          .set_type(type)
          .set_subquery_type(ResolvedSubqueryExpr::SCALAR)
          .add_parameter_list(
              MakeResolvedColumnRef(type, cola, /*is_correlated=*/true))
          .set_subquery(MakeResolvedSingleRowScan(/*column_list=*/{}))
          .Build());

  // Outer subquery lists both `cola` and `colb` in its `parameter_list`. Once
  // the inner subquery prunes `cola` bottom-up, `cola` becomes unreferenced in
  // the outer subquery as well, while `colb` remains referenced.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(inner_out, std::move(inner_subquery)));
  expr_list.push_back(MakeResolvedComputedColumn(
      outer_out, MakeResolvedColumnRef(type, colb, /*is_correlated=*/true)));
  auto outer_scan =
      MakeResolvedProjectScan({inner_out, outer_out}, std::move(expr_list),
                              MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedSubqueryExpr> outer_subquery,
      ResolvedSubqueryExprBuilder()
          .set_type(type)
          .set_subquery_type(ResolvedSubqueryExpr::SCALAR)
          .add_parameter_list(
              MakeResolvedColumnRef(type, cola, /*is_correlated=*/false))
          .add_parameter_list(
              MakeResolvedColumnRef(type, colb, /*is_correlated=*/false))
          .set_subquery(std::move(outer_scan))
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedNode> pruned_node,
      PruneUnusedCorrelatedParameters(std::move(outer_subquery)));
  const auto* pruned_outer = pruned_node->GetAs<ResolvedSubqueryExpr>();
  ASSERT_THAT(pruned_outer->parameter_list(), SizeIs(1));
  EXPECT_EQ(pruned_outer->parameter_list(0)->column(), colb);

  const auto* pruned_project =
      pruned_outer->subquery()->GetAs<ResolvedProjectScan>();
  const auto* pruned_inner =
      pruned_project->expr_list(0)->expr()->GetAs<ResolvedSubqueryExpr>();
  EXPECT_THAT(pruned_inner->parameter_list(), IsEmpty());
}

TEST(RewriteUtilsTest, SortUniqueColumnRefs) {
  const Type* type = types::StringType();
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);
  ResolvedColumn colc = factory.MakeCol("table", "colc", type);

  bool kCorrelated = true;
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  column_refs.emplace_back(MakeResolvedColumnRef(type, colb, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, cola, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, cola, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, colb, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, colc, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, colc, !kCorrelated));
  SortUniqueColumnRefs(column_refs);

  ASSERT_EQ(column_refs.size(), 4);
  EXPECT_EQ(column_refs[0]->column(), cola);
  EXPECT_EQ(column_refs[1]->column(), colb);
  EXPECT_EQ(column_refs[2]->column(), colc);
  EXPECT_EQ(column_refs[3]->column(), colc);
  EXPECT_FALSE(column_refs[2]->is_correlated());
  EXPECT_TRUE(column_refs[3]->is_correlated());
}

class GetCorrelatedColumnSetTest : public ::testing::Test {
 public:
  GetCorrelatedColumnSetTest()
      : column_factory_(0, &sequence_),
        col_a_(column_factory_.MakeCol("table", "a", types::Int64Type())),
        col_b_(column_factory_.MakeCol("table", "b", types::Int64Type())),
        col_c_(column_factory_.MakeCol("table", "c", types::Int64Type())) {}

 protected:
  googlesql_base::SequenceNumber sequence_;
  ColumnFactory column_factory_;
  ResolvedColumn col_a_;
  ResolvedColumn col_b_;
  ResolvedColumn col_c_;
};

TEST_F(GetCorrelatedColumnSetTest, NoColumnRefs) {
  std::unique_ptr<const ResolvedScan> scan =
      MakeResolvedSingleRowScan(/*column_list=*/{});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns, GetCorrelatedColumnSet(*scan));
  EXPECT_THAT(correlated_columns, IsEmpty());
}

TEST_F(GetCorrelatedColumnSetTest, OnlyUncorrelatedRefs) {
  std::unique_ptr<const ResolvedExpr> ref1 =
      MakeResolvedColumnRef(col_a_.type(), col_a_, /*is_correlated=*/false);
  std::unique_ptr<const ResolvedExpr> ref2 =
      MakeResolvedColumnRef(col_b_.type(), col_b_, /*is_correlated=*/false);

  ResolvedColumn computed_col1 =
      column_factory_.MakeCol("t", "x", col_a_.type());
  ResolvedColumn computed_col2 =
      column_factory_.MakeCol("t", "y", col_b_.type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col1, std::move(ref1)));
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col2, std::move(ref2)));

  auto project_scan = MakeResolvedProjectScan(
      {computed_col1, computed_col2}, std::move(expr_list),
      MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns,
                       GetCorrelatedColumnSet(*project_scan));
  EXPECT_THAT(correlated_columns, IsEmpty());
}

TEST_F(GetCorrelatedColumnSetTest, OnlyCorrelatedRefs) {
  std::unique_ptr<const ResolvedExpr> ref1 =
      MakeResolvedColumnRef(col_a_.type(), col_a_, /*is_correlated=*/true);
  std::unique_ptr<const ResolvedExpr> ref2 =
      MakeResolvedColumnRef(col_b_.type(), col_b_, /*is_correlated=*/true);

  ResolvedColumn computed_col1 =
      column_factory_.MakeCol("t", "x", col_a_.type());
  ResolvedColumn computed_col2 =
      column_factory_.MakeCol("t", "y", col_b_.type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col1, std::move(ref1)));
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col2, std::move(ref2)));

  auto project_scan = MakeResolvedProjectScan(
      {computed_col1, computed_col2}, std::move(expr_list),
      MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns,
                       GetCorrelatedColumnSet(*project_scan));
  EXPECT_THAT(correlated_columns, UnorderedElementsAre(col_a_, col_b_));
}

TEST_F(GetCorrelatedColumnSetTest, MixedCorrelatedAndUncorrelatedRefs) {
  std::unique_ptr<const ResolvedExpr> ref1 =
      MakeResolvedColumnRef(col_a_.type(), col_a_, /*is_correlated=*/true);
  std::unique_ptr<const ResolvedExpr> ref2 =
      MakeResolvedColumnRef(col_b_.type(), col_b_, /*is_correlated=*/false);

  ResolvedColumn computed_col1 =
      column_factory_.MakeCol("t", "x", col_a_.type());
  ResolvedColumn computed_col2 =
      column_factory_.MakeCol("t", "y", col_b_.type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col1, std::move(ref1)));
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col2, std::move(ref2)));

  auto project_scan = MakeResolvedProjectScan(
      {computed_col1, computed_col2}, std::move(expr_list),
      MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns,
                       GetCorrelatedColumnSet(*project_scan));
  EXPECT_THAT(correlated_columns, UnorderedElementsAre(col_a_));
}

TEST(RewriteUtilsTest, SafePreconditionWithIferrorOverride) {
  SimpleCatalog catalog("test_catalog");
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  AnalyzerOptions analyzer_options;

  GOOGLESQL_EXPECT_OK(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog));

  // If we remove IFEROR from the catalog, we should fail the precondition
  // checks.
  auto is_iferror = [](const Function* fn) {
    return googlesql_base::CaseEqual(fn->Name(), "iferror");
  };
  std::vector<std::unique_ptr<const Function>> removed;
  catalog.RemoveFunctions(is_iferror, removed);
  ASSERT_EQ(removed.size(), 1);

  EXPECT_THAT(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog),
      Not(IsOk()));

  // Adding the function back to the catalog should still work.
  const Function* iferror = removed.back().get();
  catalog.AddFunction(iferror);
  GOOGLESQL_EXPECT_OK(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog));

  // Replacing iferror with an identical copy should still satisfy the
  // preconditions.
  Function iferror_copy(iferror->Name(), iferror->GetGroup(), iferror->mode(),
                        iferror->signatures(), iferror->function_options());
  ASSERT_EQ(catalog.RemoveFunctions(is_iferror), 1);
  catalog.AddFunction(&iferror_copy);
  GOOGLESQL_EXPECT_OK(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog));

  // Replacing iferror with a non-builtin group copy should not satisfy the
  // preconditions.
  Function non_builtin_iferror(iferror->Name(), /*group=*/"non-builtin",
                               iferror->mode(), iferror->signatures(),
                               iferror->function_options());
  ASSERT_EQ(catalog.RemoveFunctions(is_iferror), 1);
  catalog.AddFunction(&non_builtin_iferror);
  EXPECT_THAT(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog),
      Not(IsOk()));
}

TEST(RewriteUtilsTest, SafePreconditionWithIferrorLookupFailure) {
  class ErrorThrowingCatalog : public SimpleCatalog {
   public:
    ErrorThrowingCatalog() : SimpleCatalog("error_throwing_catalog") {
      AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
    }
    absl::Status GetFunction(
        const std::string& name, const Function** function,
        const FindOptions& options = FindOptions()) override {
      GOOGLESQL_RET_CHECK_FAIL() << "fail-for-test";
    }
  };
  ErrorThrowingCatalog catalog;
  AnalyzerOptions analyzer_options;
  EXPECT_THAT(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog),
      StatusIs(absl::StatusCode::kInternal));
}

static AnalyzerOptions MakeAnalyzerOptions() {
  AnalyzerOptions options;
  options.mutable_language()->SetSupportsAllStatementKinds();
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_COLLATION_SUPPORT);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_ANNOTATION_FRAMEWORK);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_TIME_BUCKET_FUNCTIONS);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_INTERVAL_TYPE);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_JSON_TYPE);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_JSON_CONSTRUCTOR_FUNCTIONS);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_JSON_MUTATOR_FUNCTIONS);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_DATE_ARITHMETICS);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_SQL_GRAPH);
  return options;
}

class FunctionCallBuilderTest : public ::testing::Test {
 public:
  FunctionCallBuilderTest()
      : analyzer_options_(MakeAnalyzerOptions()),
        catalog_("function_builder_catalog"),
        fn_builder_(analyzer_options_, catalog_, type_factory_) {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions(analyzer_options_.language()));
  }

  AnalyzerOptions analyzer_options_;
  SimpleCatalog catalog_;
  TypeFactory type_factory_;
  FunctionCallBuilder fn_builder_;
};

TEST_F(FunctionCallBuilderTest, LikeTest) {
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::StringType(), Value::String("bar"), /*has_explicit_type=*/true);
  ASSERT_NE(input, nullptr);
  std::unique_ptr<ResolvedExpr> pattern = MakeResolvedLiteral(
      types::StringType(), Value::String("%r"), /*has_explicit_type=*/true);
  ASSERT_NE(pattern, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> like_fn,
                       fn_builder_.Like(std::move(input), std::move(pattern)));
  EXPECT_EQ(like_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$like(STRING, STRING) -> BOOL)
+-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
+-Literal(type=STRING, value="%r", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, FromProtoTest) {
  const Type* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      google::protobuf::Timestamp::descriptor(), &proto_type));
  ASSERT_NE(proto_type, nullptr);

  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(proto_type, Value::Null(proto_type),
                          /*has_explicit_type=*/true);
  ASSERT_NE(input, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> from_proto_fn,
      fn_builder_.FromProto(std::move(input), FN_FROM_PROTO_TIMESTAMP,
                            types::TimestampType()));
  EXPECT_EQ(from_proto_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:from_proto(PROTO<google.protobuf.Timestamp>) -> TIMESTAMP)
+-Literal(type=PROTO<google.protobuf.Timestamp>, value=NULL, has_explicit_type=TRUE)
)"));
}

static absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeArrayOfStrings(
    FunctionCallBuilder& fn_builder, absl::Span<const std::string> strings) {
  std::vector<std::unique_ptr<const ResolvedExpr>> make_array_args;
  for (const std::string& str : strings) {
    make_array_args.emplace_back(MakeResolvedLiteral(
        types::StringType(), Value::String(str), /*has_explicit_type=*/true));
  }

  const Type* type = make_array_args[0]->type();
  return fn_builder.MakeArray(type, std::move(make_array_args));
}

TEST_F(FunctionCallBuilderTest, MakeArray) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> make_arr_fn,
                       MakeArrayOfStrings(fn_builder_, {"foo", "bar"}));
  EXPECT_EQ(make_arr_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
+-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
+-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, JsonObject) {
  std::vector<std::unique_ptr<const ResolvedExpr>> keys;
  keys.push_back(MakeResolvedLiteral(types::StringType(), Value::String("name"),
                                     /*has_explicit_type=*/true));
  keys.push_back(MakeResolvedLiteral(types::StringType(), Value::String("age"),
                                     /*has_explicit_type=*/true));
  std::vector<std::unique_ptr<const ResolvedExpr>> values;
  values.push_back(MakeResolvedLiteral(types::StringType(),
                                       Value::String("Alice"),
                                       /*has_explicit_type=*/true));
  values.push_back(MakeResolvedLiteral(types::Int64Type(), Value::Int64(30),
                                       /*has_explicit_type=*/true));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> json_obj_fn,
      fn_builder_.JsonObject(std::move(keys), std::move(values)));
  EXPECT_EQ(json_obj_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:json_object(repeated(1) STRING, repeated(1) STRING, repeated(1) STRING, repeated(1) INT64) -> JSON)
+-Literal(type=STRING, value="name", has_explicit_type=TRUE)
+-Literal(type=STRING, value="Alice", has_explicit_type=TRUE)
+-Literal(type=STRING, value="age", has_explicit_type=TRUE)
+-Literal(type=INT64, value=30, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, JsonObjectEmptyKeysFails) {
  std::vector<std::unique_ptr<const ResolvedExpr>> keys;
  std::vector<std::unique_ptr<const ResolvedExpr>> values;
  EXPECT_THAT(fn_builder_.JsonObject(std::move(keys), std::move(values)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, JsonSet) {
  // Single path/value pair (repeated arguments have num_occurrences = 0).
  {
    std::unique_ptr<const ResolvedExpr> json_doc = MakeResolvedLiteral(
        types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);
    std::vector<std::unique_ptr<const ResolvedExpr>> paths;
    paths.push_back(MakeResolvedLiteral(types::StringType(),
                                        Value::String("$.name"),
                                        /*has_explicit_type=*/true));
    std::vector<std::unique_ptr<const ResolvedExpr>> values;
    values.push_back(MakeResolvedLiteral(types::StringType(),
                                         Value::String("Alice"),
                                         /*has_explicit_type=*/true));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedFunctionCall> json_set_fn,
        fn_builder_.JsonSet(std::move(json_doc), std::move(paths),
                            std::move(values)));
    EXPECT_EQ(json_set_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:json_set(JSON, STRING, STRING, repeated(0) STRING, repeated ANY TYPE, optional(1) BOOL create_if_missing) -> JSON)
+-Literal(type=JSON, value=NULL, has_explicit_type=TRUE)
+-Literal(type=STRING, value="$.name", has_explicit_type=TRUE)
+-Literal(type=STRING, value="Alice", has_explicit_type=TRUE)
+-Literal(type=BOOL, value=true)
)"));
  }

  // Multiple path/value pairs.
  {
    std::unique_ptr<const ResolvedExpr> json_doc = MakeResolvedLiteral(
        types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);
    std::vector<std::unique_ptr<const ResolvedExpr>> paths;
    paths.push_back(MakeResolvedLiteral(types::StringType(),
                                        Value::String("$.name"),
                                        /*has_explicit_type=*/true));
    paths.push_back(MakeResolvedLiteral(types::StringType(),
                                        Value::String("$.age"),
                                        /*has_explicit_type=*/true));
    std::vector<std::unique_ptr<const ResolvedExpr>> values;
    values.push_back(MakeResolvedLiteral(types::StringType(),
                                         Value::String("Alice"),
                                         /*has_explicit_type=*/true));
    values.push_back(MakeResolvedLiteral(types::Int64Type(), Value::Int64(30),
                                         /*has_explicit_type=*/true));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedFunctionCall> json_set_fn,
        fn_builder_.JsonSet(std::move(json_doc), std::move(paths),
                            std::move(values)));
    EXPECT_EQ(json_set_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:json_set(JSON, STRING, STRING, repeated(1) STRING, repeated(1) INT64, optional(1) BOOL create_if_missing) -> JSON)
+-Literal(type=JSON, value=NULL, has_explicit_type=TRUE)
+-Literal(type=STRING, value="$.name", has_explicit_type=TRUE)
+-Literal(type=STRING, value="Alice", has_explicit_type=TRUE)
+-Literal(type=STRING, value="$.age", has_explicit_type=TRUE)
+-Literal(type=INT64, value=30, has_explicit_type=TRUE)
+-Literal(type=BOOL, value=true)
)"));
  }
}

TEST_F(FunctionCallBuilderTest, JsonRemove) {
  // Single path (trailing repeated argument omitted from concrete_args).
  {
    std::unique_ptr<const ResolvedExpr> json_doc = MakeResolvedLiteral(
        types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);
    std::vector<std::unique_ptr<const ResolvedExpr>> remove_paths;
    remove_paths.push_back(MakeResolvedLiteral(types::StringType(),
                                               Value::String("$.age"),
                                               /*has_explicit_type=*/true));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedFunctionCall> json_remove_fn,
        fn_builder_.JsonRemove(std::move(json_doc), std::move(remove_paths)));
    EXPECT_EQ(json_remove_fn->DebugString(),
              absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:json_remove(JSON, STRING) -> JSON)
+-Literal(type=JSON, value=NULL, has_explicit_type=TRUE)
+-Literal(type=STRING, value="$.age", has_explicit_type=TRUE)
)"));
  }

  // Multiple paths (trailing repeated argument populated in concrete_args).
  {
    std::unique_ptr<const ResolvedExpr> json_doc = MakeResolvedLiteral(
        types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);
    std::vector<std::unique_ptr<const ResolvedExpr>> remove_paths;
    remove_paths.push_back(MakeResolvedLiteral(types::StringType(),
                                               Value::String("$.name"),
                                               /*has_explicit_type=*/true));
    remove_paths.push_back(MakeResolvedLiteral(types::StringType(),
                                               Value::String("$.age"),
                                               /*has_explicit_type=*/true));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedFunctionCall> json_remove_fn,
        fn_builder_.JsonRemove(std::move(json_doc), std::move(remove_paths)));
    EXPECT_EQ(json_remove_fn->DebugString(),
              absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:json_remove(JSON, STRING, repeated(1) STRING) -> JSON)
+-Literal(type=JSON, value=NULL, has_explicit_type=TRUE)
+-Literal(type=STRING, value="$.name", has_explicit_type=TRUE)
+-Literal(type=STRING, value="$.age", has_explicit_type=TRUE)
)"));
  }
}

TEST_F(FunctionCallBuilderTest, JsonObjectUnsupportedTypeFails) {
  std::vector<std::unique_ptr<const ResolvedExpr>> keys;
  keys.push_back(MakeResolvedLiteral(types::StringType(),
                                     Value::String("token"),
                                     /*has_explicit_type=*/true));
  std::vector<std::unique_ptr<const ResolvedExpr>> values;
  values.push_back(MakeResolvedLiteral(types::TokenListType(),
                                       Value::NullTokenList(),
                                       /*has_explicit_type=*/true));
  EXPECT_THAT(fn_builder_.JsonObject(std::move(keys), std::move(values)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, MakeArrayWithAnnotation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> make_arr_fn,
                       fn_builder_.MakeArray(type, std::move(args)));

  EXPECT_EQ(make_arr_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
+-type_annotation_map=<{Collation:"und:ci"}>
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, MakeArrayWithMixedAnnotation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "binary"}},
                           analyzer_options_, catalog_, type_factory_));

  const Type* type = args[0]->type();
  EXPECT_THAT(fn_builder_.MakeArray(type, std::move(args)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, ArrayConcat) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> just_foo_array,
                       MakeArrayOfStrings(fn_builder_, {"foo"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> bar_baz_array,
                       MakeArrayOfStrings(fn_builder_, {"bar", "baz"}));
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.emplace_back(std::move(just_foo_array));
  args.emplace_back(std::move(bar_baz_array));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeArrayType(types::StringType(), &array_type));
  args.emplace_back(
      MakeResolvedLiteral(array_type, Value::EmptyArray(array_type)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_concat_fn,
                       fn_builder_.ArrayConcat(std::move(args)));

  EXPECT_EQ(array_concat_fn->DebugString(),
            absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_concat(ARRAY<STRING>, repeated(2) ARRAY<STRING>) -> ARRAY<STRING>)
+-FunctionCall(GoogleSQL:$make_array(repeated(1) STRING) -> ARRAY<STRING>)
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="baz", has_explicit_type=TRUE)
+-Literal(type=ARRAY<STRING>, value=[])
)"));
}

TEST_F(FunctionCallBuilderTest, CaseNoValueElseTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> condition_args;
  std::vector<std::unique_ptr<const ResolvedExpr>> result_args;

  condition_args.push_back(MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true));
  result_args.push_back(MakeResolvedLiteral(
      types::StringType(), Value::String("foo"), /*has_explicit_type=*/true));
  condition_args.push_back(MakeResolvedLiteral(
      types::BoolType(), Value::Bool(false), /*has_explicit_type=*/true));
  result_args.push_back(MakeResolvedLiteral(
      types::StringType(), Value::String("bar"), /*has_explicit_type=*/true));

  std::unique_ptr<ResolvedExpr> else_result = MakeResolvedLiteral(
      types::StringType(), Value::String("baz"), /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> case_fn,
      fn_builder_.CaseNoValue(std::move(condition_args), std::move(result_args),
                              std::move(else_result)));
  EXPECT_EQ(case_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$case_no_value(repeated(2) BOOL, repeated(2) STRING, STRING) -> STRING)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
+-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
+-Literal(type=BOOL, value=false, has_explicit_type=TRUE)
+-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
+-Literal(type=STRING, value="baz", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, NotTest) {
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  ASSERT_NE(input, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> not_fn,
                       fn_builder_.Not(std::move(input)));
  EXPECT_EQ(not_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$not(BOOL) -> BOOL)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, IfNullTest) {
  std::unique_ptr<ResolvedExpr> arg =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("test"),
                          /*has_explicit_type=*/true);
  auto annotation_map = AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation<CollationAnnotation>(
      googlesql::SimpleValue::String("und_ci"));
  arg->set_type_annotation_map(annotation_map.get());
  std::unique_ptr<ResolvedExpr> null_case =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("default"),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> ifnull_fn,
      fn_builder_.IfNull(std::move(arg), std::move(null_case)));
  EXPECT_EQ(ifnull_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:ifnull(STRING, STRING) -> STRING)
+-type_annotation_map={Collation:"und_ci"}
+-Literal(type=STRING, type_annotation_map={Collation:"und_ci"}, value="test", has_explicit_type=TRUE)
+-Literal(type=STRING, value="default", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, IfNullTestConflictingCollations) {
  std::unique_ptr<ResolvedExpr> arg =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("test"),
                          /*has_explicit_type=*/true);
  auto annotation_map = AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation<CollationAnnotation>(
      googlesql::SimpleValue::String("und_ci"));
  arg->set_type_annotation_map(annotation_map.get());
  std::unique_ptr<ResolvedExpr> null_case =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("default"),
                          /*has_explicit_type=*/true);
  auto annotation_map_case = AnnotationMap::Create(types::StringType());
  annotation_map_case->SetAnnotation<CollationAnnotation>(
      googlesql::SimpleValue::String("binary"));
  null_case->set_type_annotation_map(annotation_map_case.get());

  EXPECT_THAT(fn_builder_.IfNull(std::move(arg), std::move(null_case)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, NullIfErrorTest) {
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("test"),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> nulliferror_fn,
                       fn_builder_.NullIfError(std::move(input)));
  EXPECT_EQ(nulliferror_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:nulliferror(STRING) -> STRING)
+-Literal(type=STRING, value="test", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, EqualTest) {
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("true"),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("false"),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> equal_fn,
                       fn_builder_.Equal(std::move(input), std::move(input2)));
  EXPECT_EQ(equal_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(STRING, STRING) -> BOOL)
+-Literal(type=STRING, value="true", has_explicit_type=TRUE)
+-Literal(type=STRING, value="false", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, EqualArgumentTypeMismatchTest) {
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("true"),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.Equal(std::move(input), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, EqualArgumentTypeDoesNotSupportEqualityTest) {
  // TokenList type does not support equality.
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::TokenListType(), Value::NullTokenList(),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::TokenListType(), Value::NullTokenList(),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.Equal(std::move(input), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualForStringsTest) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("string1"),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("string2"),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$greater_or_equal(STRING, STRING) -> BOOL)
+-Literal(type=STRING, value="string1", has_explicit_type=TRUE)
+-Literal(type=STRING, value="string2", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualForInt64Test) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$greater_or_equal(INT64, INT64) -> BOOL)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest,
       GreaterOrEqualRefusesToCompareStringsAndIntegers) {
  std::unique_ptr<ResolvedExpr> input1 = MakeResolvedLiteral(
      types::StringType(), Value::StringValue("some_string"),
      /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualRefusesToCompareInt32AndInt64) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int32Type(), Value::Int32(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest,
       GreaterOrEqualCanCompareSignedAndUnsignedInt64) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Uint64Type(), Value::Uint64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$greater_or_equal(UINT64, INT64) -> BOOL)
+-Literal(type=UINT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualTypeDoesNotSupportOrdering) {
  std::unique_ptr<ResolvedExpr> input1 = MakeResolvedLiteral(
      types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, AdditionTestForInt64) {
  // Test the `$add(INT64, INT64) -> INT64` builtin signature.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Add(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, SubtractionTestForInt64) {
  // Test the `$subtract(INT64, INT64) -> INT64` builtin signature.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Subtract(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$subtract(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, SubtractionTestForDateMinusInt64) {
  // Datetime - Int64 is a built-in signature type in the `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 = MakeResolvedLiteral(
      types::DateType(), Value::Date(/*3rd Nov. 1992*/ 8342),
      /*has_explicit_type=*/true);

  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(10),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Subtract(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$subtract(DATE, INT64) -> DATE)
+-Literal(type=DATE, value=1992-11-03, has_explicit_type=TRUE)
+-Literal(type=INT64, value=10, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest,
       SubtractRefusesToWorkOnSignedAndUnsignedIntegers) {
  // Int64 - Uint64 is *not* a built-in function in `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Uint64Type(), Value::Uint64(1),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.Subtract(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, SafeSubtractionTestForInt64) {
  // Int64 - Int64 is a built-in signature type in the `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.SafeSubtract(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:safe_subtract(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest,
       SafeSubtractRefusesToWorkOnSignedAndUnsignedIntegers) {
  // Int64 - Uint64 is *not* a built-in function in `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Uint64Type(), Value::Uint64(1),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.SafeSubtract(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, AndTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(false), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));
  expressions.push_back(std::move(input2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> and_fn,
                       fn_builder_.And(std::move(expressions)));
  EXPECT_EQ(and_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$and(BOOL, repeated(1) BOOL) -> BOOL)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
+-Literal(type=BOOL, value=false, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, OrTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(false), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));
  expressions.push_back(std::move(input2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> or_fn,
                       fn_builder_.Or(std::move(expressions)));
  EXPECT_EQ(or_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$or(BOOL, repeated(1) BOOL) -> BOOL)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
+-Literal(type=BOOL, value=false, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, AndTooFewExpressionsTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));

  EXPECT_THAT(fn_builder_.And(std::move(expressions)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, AndInvalidExpressionsTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::Int64Type(), Value::Int64(1), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));
  expressions.push_back(std::move(input2));

  EXPECT_THAT(fn_builder_.And(std::move(expressions)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, ErrorFunctionWithCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:cs", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn1,
                       fn_builder_.Error(std::move(expr1)));
  EXPECT_EQ(resolved_fn1->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:error(STRING) -> INT64)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="a")
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> resolved_fn2,
      fn_builder_.Error(std::move(expr2), types::StringType()));
  EXPECT_EQ(resolved_fn2->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:error(STRING) -> STRING)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:cs"}
  +-Literal(type=STRING, value="A")
  +-Literal(type=STRING, value="und:cs", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, NotEqualWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:ci", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.NotEqual(std::move(expr1), std::move(expr2)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$not_equal(STRING, STRING) -> BOOL)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="a")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="A")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, NotEqualWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:cs", analyzer_options_, catalog_, type_factory_));

  ASSERT_THAT(fn_builder_.NotEqual(std::move(expr1), std::move(expr2)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, LeastWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Least(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:least(repeated(2) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, LeastWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  ASSERT_THAT(fn_builder_.Least(std::move(args)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, GreatestWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<const ResolvedExpr>> args,
      testing::BuildResolvedLiteralsWithCollationForTest(
          {{"foo", "und:ci"}, {"FOO", "und:ci"}, {"BaR", "und:ci"}},
          analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Greatest(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:greatest(repeated(3) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="FOO", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="BaR", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, GreatestWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  ASSERT_THAT(fn_builder_.Greatest(std::move(args)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, CoalesceWithCommonSuperTypeTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Int64(1))
                           .set_type(types::Int64Type())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Int32(2))
                           .set_type(types::Int32Type())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a3,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Uint32(300))
                           .set_type(types::Uint32Type())
                           .Build());

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(a1));
  args.push_back(std::move(a2));
  args.push_back(std::move(a3));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Coalesce(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:coalesce(repeated(3) INT64) -> INT64)
+-Literal(type=INT64, value=1)
+-Literal(type=INT32, value=2)
+-Literal(type=UINT32, value=300)
)"));
}

TEST_F(FunctionCallBuilderTest, CoalesceWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<const ResolvedExpr>> args,
      testing::BuildResolvedLiteralsWithCollationForTest(
          {{"foo", "und:ci"}, {"FOO", "und:ci"}, {"BaR", "und:ci"}},
          analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Coalesce(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:coalesce(repeated(3) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="FOO", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="BaR", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, CoalesceWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  EXPECT_THAT(fn_builder_.Coalesce(std::move(args)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, LessWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:ci", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.Less(std::move(expr1), std::move(expr2)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$less(STRING, STRING) -> BOOL)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="a")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="A")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayLengthWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArrayLength(std::move(array_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_length(ARRAY<STRING>) -> INT64)
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
  +-type_annotation_map=<{Collation:"und:ci"}>
  +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  | +-type_annotation_map={Collation:"und:ci"}
  | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
  | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
  +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
    +-type_annotation_map={Collation:"und:ci"}
    +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
    +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayAtOffsetTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  std::unique_ptr<const ResolvedExpr> offset_expr =
      MakeResolvedLiteral(Value::Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.ArrayAtOffset(std::move(array_expr), std::move(offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$array_at_offset(ARRAY<STRING>, INT64) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-type_annotation_map=<{Collation:"und:ci"}>
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| | +-type_annotation_map={Collation:"und:ci"}
| | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
|   +-type_annotation_map={Collation:"und:ci"}
|   +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
|   +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-Literal(type=INT64, value=0)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Int64(100)));
  args.push_back(MakeResolvedLiteral(Value::Int64(101)));
  args.push_back(MakeResolvedLiteral(Value::Int64(102)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> array_expr,
      fn_builder_.MakeArray(types::Int64Type(), std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::Int64(2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArraySlice(std::move(array_expr),
                                              std::move(start_offset_expr),
                                              std::move(end_offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_slice(ARRAY<INT64> array_to_slice, INT64 start_offset, INT64 end_offset) -> ARRAY<INT64>)
+-FunctionCall(GoogleSQL:$make_array(repeated(3) INT64) -> ARRAY<INT64>)
| +-Literal(type=INT64, value=100)
| +-Literal(type=INT64, value=101)
| +-Literal(type=INT64, value=102)
+-Literal(type=INT64, value=1)
+-Literal(type=INT64, value=2)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(0));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArraySlice(std::move(array_expr),
                                              std::move(start_offset_expr),
                                              std::move(end_offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_slice(ARRAY<STRING> array_to_slice, INT64 start_offset, INT64 end_offset) -> ARRAY<STRING>)
+-type_annotation_map=<{Collation:"und:ci"}>
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-type_annotation_map=<{Collation:"und:ci"}>
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| | +-type_annotation_map={Collation:"und:ci"}
| | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
|   +-type_annotation_map={Collation:"und:ci"}
|   +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
|   +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-Literal(type=INT64, value=0)
+-Literal(type=INT64, value=1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceWithInvalidArgumentsTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Int64(100)));
  args.push_back(MakeResolvedLiteral(Value::Int64(101)));
  args.push_back(MakeResolvedLiteral(Value::Int64(102)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> array_expr,
      fn_builder_.MakeArray(types::Int64Type(), std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::String("foo"));
  EXPECT_THAT(fn_builder_.ArraySlice(std::move(array_expr),
                                     std::move(start_offset_expr),
                                     std::move(end_offset_expr)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, ModInt64Test) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(Value::Int64(1)),
                      /*divisor_expr=*/MakeResolvedLiteral(Value::Int64(2))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1)
+-Literal(type=INT64, value=2)
)"));
}

TEST_F(FunctionCallBuilderTest, ModUint64Test) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(Value::Uint64(1)),
                      /*divisor_expr=*/MakeResolvedLiteral(Value::Uint64(2))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(UINT64, UINT64) -> UINT64)
+-Literal(type=UINT64, value=1)
+-Literal(type=UINT64, value=2)
)"));
}

// The catalog does not have the FN_MOD_NUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModNumericNoSignatureTest) {
  EXPECT_THAT(
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                          Value::Numeric(NumericValue(1))),
                      /*divisor_expr=*/MakeResolvedLiteral(
                          Value::Numeric(NumericValue(2)))),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr(
              "The provided catalog does not have the FN_MOD_NUMERIC "
              "signature. Did you forget to enable FEATURE_NUMERIC_TYPE?")));
}

// The new catalog has the FN_MOD_NUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModNumericTest) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_NUMERIC_TYPE);
  SimpleCatalog catalog("mod_numeric_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                                          Value::Numeric(NumericValue(1))),
                                      /*divisor_expr=*/MakeResolvedLiteral(
                                          Value::Numeric(NumericValue(2)))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(NUMERIC, NUMERIC) -> NUMERIC)
+-Literal(type=NUMERIC, value=1)
+-Literal(type=NUMERIC, value=2)
)"));
}

// The catalog does not have the FN_MOD_BIGNUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModBigNumericNoSignatureTest) {
  EXPECT_THAT(
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                          Value::BigNumeric(BigNumericValue(1))),
                      /*divisor_expr=*/MakeResolvedLiteral(
                          Value::BigNumeric(BigNumericValue(2)))),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr(
              "The provided catalog does not have the FN_MOD_BIGNUMERIC "
              "signature. Did you forget to enable FEATURE_BIGNUMERIC_TYPE?")));
}

// The new catalog has the FN_MOD_BIGNUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModBigNumericTest) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_BIGNUMERIC_TYPE);
  SimpleCatalog catalog("mod_big_numeric_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                         Value::BigNumeric(BigNumericValue(1))),
                     /*divisor_expr=*/MakeResolvedLiteral(
                         Value::BigNumeric(BigNumericValue(2)))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(BIGNUMERIC, BIGNUMERIC) -> BIGNUMERIC)
+-Literal(type=BIGNUMERIC, value=1)
+-Literal(type=BIGNUMERIC, value=2)
)"));
}

TEST_F(FunctionCallBuilderTest, ModInvalidInputTypeTest) {
  EXPECT_THAT(
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(Value::String("a")),
                      /*divisor_expr=*/MakeResolvedLiteral(Value::String("b"))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr("Unsupported input type for mod: STRING")));
}

TEST_F(FunctionCallBuilderTest, CountInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Int32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT32) -> INT64)
+-ColumnRef(type=INT32, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountUInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT32) -> INT64)
+-ColumnRef(type=UINT32, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountUInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT64) -> INT64)
+-ColumnRef(type=UINT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountStringTest) {
  auto column =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(STRING) -> INT64)
+-ColumnRef(type=STRING, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountFloatTest) {
  auto column =
      MakeResolvedColumnRef(types::FloatType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(FLOAT) -> INT64)
+-ColumnRef(type=FLOAT, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountDoubleTest) {
  auto column =
      MakeResolvedColumnRef(types::DoubleType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(DOUBLE) -> INT64)
+-ColumnRef(type=DOUBLE, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Int32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT32) -> INT64)
+-ColumnRef(type=INT32, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctUInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT32) -> INT64)
+-ColumnRef(type=UINT32, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctUInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT64) -> INT64)
+-ColumnRef(type=UINT64, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctStringTest) {
  auto column =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(STRING) -> INT64)
+-ColumnRef(type=STRING, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctFloatTest) {
  auto column =
      MakeResolvedColumnRef(types::FloatType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(FLOAT) -> INT64)
+-ColumnRef(type=FLOAT, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctDoubleTest) {
  auto column =
      MakeResolvedColumnRef(types::DoubleType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(DOUBLE) -> INT64)
+-ColumnRef(type=DOUBLE, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctGetStructFieldTest) {
  std::unique_ptr<ResolvedExpr> column_ref =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  auto column = MakeResolvedGetStructField(types::Int64Type(),
                                           std::move(column_ref), 0, true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-GetStructField
| +-type=INT64
| +-expr=
| | +-ColumnRef(type=INT64, column=.#-1)
| +-field_idx=0
| +-field_expr_is_positional=TRUE
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctGetProtoTest) {
  std::unique_ptr<ResolvedExpr> column_ref =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  auto field_descriptor =
      ResolvedColumnProto::descriptor()->FindFieldByName("column_id");
  auto column = MakeResolvedGetProtoField(
      /*type=*/types::Int64Type(),
      /*expr=*/std::move(column_ref),
      /*field_descriptor=*/field_descriptor,
      /*default_value=*/googlesql::Value(),
      /*get_has_bit=*/false,
      /*format=*/FieldFormat::DEFAULT_FORMAT,
      /*return_default_value_when_unset=*/false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-GetProtoField
| +-type=INT64
| +-expr=
| | +-ColumnRef(type=INT64, column=.#-1)
| +-field_descriptor=column_id
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatString) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::String("a")));
  args.push_back(MakeResolvedLiteral(Value::String("b")));
  args.push_back(MakeResolvedLiteral(Value::String("c")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Concat(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:concat(STRING, repeated(2) STRING) -> STRING)
+-Literal(type=STRING, value="a")
+-Literal(type=STRING, value="b")
+-Literal(type=STRING, value="c")
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatStringWithCollation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<const ResolvedExpr>> args,
      testing::BuildResolvedLiteralsWithCollationForTest(
          {{"foo", "und:ci"}, {"bar", "und:ci"}, {"bar", "und:ci"}},
          analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Concat(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:concat(STRING, repeated(2) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatBytes) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Bytes("a")));
  args.push_back(MakeResolvedLiteral(Value::Bytes("b")));
  args.push_back(MakeResolvedLiteral(Value::Bytes("c")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Concat(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:concat(BYTES, repeated(2) BYTES) -> BYTES)
+-Literal(type=BYTES, value=b"a")
+-Literal(type=BYTES, value=b"b")
+-Literal(type=BYTES, value=b"c")
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatInvalidInputType) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Numeric(NumericValue(1))));
  args.push_back(MakeResolvedLiteral(Value::Numeric(NumericValue(2))));
  EXPECT_THAT(fn_builder_.Concat(std::move(args)),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Invalid element type: NUMERIC")));
}

TEST_F(FunctionCallBuilderTest, ConcatEmptyArgList) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  EXPECT_THAT(
      fn_builder_.Concat(std::move(args)),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("!elements.empty() ")));
}

TEST_F(FunctionCallBuilderTest, IsNotNull) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.IsNotNull(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$not(BOOL) -> BOOL)
+-FunctionCall(GoogleSQL:$is_null(INT64) -> BOOL)
  +-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, In) {
  auto annotation_map = AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation<CollationAnnotation>(
      googlesql::SimpleValue::String("und_ci"));

  std::unique_ptr<ResolvedExpr> search =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("search"),
                          /*has_explicit_type=*/true);
  search->set_type_annotation_map(annotation_map.get());

  std::unique_ptr<ResolvedExpr> opt1 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("opt1"),
                          /*has_explicit_type=*/true);
  opt1->set_type_annotation_map(annotation_map.get());

  std::unique_ptr<ResolvedExpr> opt2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("opt2"),
                          /*has_explicit_type=*/true);
  opt2->set_type_annotation_map(annotation_map.get());

  std::vector<std::unique_ptr<const ResolvedExpr>> in_exprs;
  in_exprs.push_back(std::move(opt1));
  in_exprs.push_back(std::move(opt2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.In(std::move(search), std::move(in_exprs)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$in(STRING, repeated(2) STRING) -> BOOL)
+-Literal(type=STRING, type_annotation_map={Collation:"und_ci"}, value="search", has_explicit_type=TRUE)
+-Literal(type=STRING, type_annotation_map={Collation:"und_ci"}, value="opt1", has_explicit_type=TRUE)
+-Literal(type=STRING, type_annotation_map={Collation:"und_ci"}, value="opt2", has_explicit_type=TRUE)
+-collation_list=[und_ci]
)"));
}

TEST_F(FunctionCallBuilderTest, InTestConflictingCollations) {
  std::unique_ptr<ResolvedExpr> search =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("search"),
                          /*has_explicit_type=*/true);
  auto annotation_map = AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation<CollationAnnotation>(
      googlesql::SimpleValue::String("und_ci"));
  search->set_type_annotation_map(annotation_map.get());

  std::unique_ptr<ResolvedExpr> opt1 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("opt1"),
                          /*has_explicit_type=*/true);
  auto annotation_map_opt1 = AnnotationMap::Create(types::StringType());
  annotation_map_opt1->SetAnnotation<CollationAnnotation>(
      googlesql::SimpleValue::String("binary"));
  opt1->set_type_annotation_map(annotation_map_opt1.get());

  std::vector<std::unique_ptr<const ResolvedExpr>> in_exprs;
  in_exprs.push_back(std::move(opt1));

  EXPECT_THAT(fn_builder_.In(std::move(search), std::move(in_exprs)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, InArray) {
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  auto search = MakeResolvedLiteral(Value::String("search"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.InArray(std::move(search), std::move(array)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$in_array(STRING, ARRAY<STRING>) -> BOOL)
+-Literal(type=STRING, value="search")
+-ColumnRef(type=ARRAY<STRING>, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayIncludesAll) {
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  auto search = MakeResolvedLiteral(array_of_strings,
                                    googlesql::values::StringArray({"search"}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.ArrayIncludesAll(std::move(array), std::move(search)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_includes_all(ARRAY<STRING> array_to_search, ARRAY<STRING> search_values) -> BOOL)
+-ColumnRef(type=ARRAY<STRING>, column=.#-1)
+-Literal(type=ARRAY<STRING>, value=["search"])
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayIncludesAny) {
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  auto search = MakeResolvedLiteral(array_of_strings,
                                    googlesql::values::StringArray({"search"}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.ArrayIncludesAny(std::move(array), std::move(search)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_includes_any(ARRAY<STRING> array_to_search, ARRAY<STRING> search_values) -> BOOL)
+-ColumnRef(type=ARRAY<STRING>, column=.#-1)
+-Literal(type=ARRAY<STRING>, value=["search"])
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayToString) {
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  auto delimiter = MakeResolvedLiteral(Value::String("delimiter"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.ArrayToString(std::move(array), std::move(delimiter)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_to_string(ARRAY<STRING>, STRING) -> STRING)
+-ColumnRef(type=ARRAY<STRING>, column=.#-1)
+-Literal(type=STRING, value="delimiter")
)"));
}

TEST_F(FunctionCallBuilderTest, RegexpContainsString) {
  auto value =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);
  auto regexp = MakeResolvedLiteral(Value::String("regexp"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.RegexpContainsString(std::move(value), std::move(regexp)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:regexp_contains(STRING, STRING) -> BOOL)
+-ColumnRef(type=STRING, column=.#-1)
+-Literal(type=STRING, value="regexp")
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayAgg) {
  auto element =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.ArrayAgg(std::move(element), nullptr));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:array_agg(STRING) -> ARRAY<STRING>)
+-ColumnRef(type=STRING, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayAggWithHaving) {
  ResolvedColumn col;
  auto element = MakeResolvedColumnRef(types::StringType(), col, false);
  auto having = MakeResolvedColumnRef(types::StringType(), col, false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.ArrayAgg(std::move(element), std::move(having),
                           ResolvedAggregateHavingModifier::MAX));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:array_agg(STRING) -> ARRAY<STRING>)
+-ColumnRef(type=STRING, column=.#-1)
+-having_modifier=
  +-AggregateHavingModifier
    +-kind=MAX
    +-having_expr=
      +-ColumnRef(type=STRING, column=.#-1)
)"));
}

// FEATURE_ANALYTIC_FUNCTIONS is required to load the IS_FIRST function in the
// catalog.
TEST_F(FunctionCallBuilderTest, IsFirstKCatalogMissingFn) {
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(
      fn_builder_.IsFirstK(std::move(input)),
      StatusIs(absl::StatusCode::kNotFound,
               ::testing::HasSubstr("Function not found: is_first not found in "
                                    "catalog function_builder_catalog")));
}

TEST_F(FunctionCallBuilderTest, IsFirstKInt64Literal) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ANALYTIC_FUNCTIONS);
  SimpleCatalog catalog("is_first_k_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder.IsFirstK(std::move(input)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AnalyticFunctionCall(GoogleSQL:is_first(INT64) -> BOOL)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
)"));
}

// IS_FIRST(k) requires the argument to be a literal or a parameter.
TEST_F(FunctionCallBuilderTest, IsFirstKInt64ColumnRef) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ANALYTIC_FUNCTIONS);
  SimpleCatalog catalog("is_first_k_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  EXPECT_THAT(fn_builder.IsFirstK(std::move(input)),
              StatusIs(absl::StatusCode::kInternal));
}

// IS_FIRST(k) requires the argument to be INT64.
TEST_F(FunctionCallBuilderTest, IsFirstKWrongType) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ANALYTIC_FUNCTIONS);
  SimpleCatalog catalog("is_first_k_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::StringType(), Value::String("abc"));

  EXPECT_THAT(fn_builder.IsFirstK(std::move(input)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, MakeNullIfEmptyArray) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory column_factory(10, &sequence);
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.MakeNullIfEmptyArray(column_factory, std::move(array)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
WithExpr
+-type=ARRAY<STRING>
+-assignment_list=
| +-$out#11 := ColumnRef(type=ARRAY<STRING>, column=.#-1)
+-expr=
  +-FunctionCall(GoogleSQL:if(BOOL, ARRAY<STRING>, ARRAY<STRING>) -> ARRAY<STRING>)
    +-FunctionCall(GoogleSQL:$greater_or_equal(INT64, INT64) -> BOOL)
    | +-FunctionCall(GoogleSQL:array_length(ARRAY<STRING>) -> INT64)
    | | +-ColumnRef(type=ARRAY<STRING>, column=null_if_empty_array.$out#11)
    | +-Literal(type=INT64, value=1)
    +-ColumnRef(type=ARRAY<STRING>, column=null_if_empty_array.$out#11)
    +-Literal(type=ARRAY<STRING>, value=NULL)
)"));
}

TEST_F(FunctionCallBuilderTest, AreEqualGraphElements) {
  TypeFactory type_factory;
  const GraphElementType* graph_node_type1 = nullptr;
  const GraphElementType* graph_node_type2 = nullptr;
  // super type.
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"graph_node_type"}, GraphElementType::kNode,
      {{"p1", types::Int64Type()}, {"p2", types::StringType()}},
      /*element_tables=*/{}, &graph_node_type1));
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"graph_node_type"}, GraphElementType::kNode,
      {{"p1", types::Int64Type()}},
      /*element_tables=*/{}, &graph_node_type2));

  // lhs is super type.
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  args.emplace_back(MakeResolvedColumnRef(graph_node_type2, ResolvedColumn(),
                                          /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> function,
                       fn_builder_.AreEqualGraphElements(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>) -> BOOL)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
+-Cast(GRAPH_NODE(graph_node_type)<p1 INT64> -> GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>)
  +-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64>, column=.#-1)
)"));

  // rhs is super type.
  args.clear();
  args.emplace_back(MakeResolvedColumnRef(graph_node_type2, ResolvedColumn(),
                                          /*is_correlated=*/false));
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(function,
                       fn_builder_.AreEqualGraphElements(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>) -> BOOL)
+-Cast(GRAPH_NODE(graph_node_type)<p1 INT64> -> GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>)
| +-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64>, column=.#-1)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
)"));

  // lhs and rhs are the same type.
  args.clear();
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(function,
                       fn_builder_.AreEqualGraphElements(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>) -> BOOL)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, ElementDefinitionNameIs) {
  TypeFactory type_factory;
  const GraphElementType* graph_node_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"graph_node_type"}, GraphElementType::kNode,
      {{"p1", types::Int64Type()}}, /*element_tables=*/{}, &graph_node_type));

  std::unique_ptr<const ResolvedExpr> node_ref = MakeResolvedColumnRef(
      graph_node_type, ResolvedColumn(), /*is_correlated=*/false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> function,
      fn_builder_.ElementDefinitionNameIs(std::move(node_ref), "Person"));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:element_definition_name_is(GRAPH_NODE(graph_node_type)<p1 INT64>, STRING) -> BOOL)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64>, column=.#-1)
+-Literal(type=STRING, value="Person", has_explicit_type=TRUE, preserve_in_literal_remover=TRUE)
)"));

  EXPECT_THAT(fn_builder_.ElementDefinitionNameIs(nullptr, "Person"),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(fn_builder_.ElementDefinitionNameIs(
                  MakeResolvedColumnRef(graph_node_type, ResolvedColumn(),
                                        /*is_correlated=*/false),
                  ""),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(fn_builder_.ElementDefinitionNameIs(
                  MakeResolvedLiteral(Value::Int64(1)), "Person"),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, ElementDefinitionName) {
  TypeFactory type_factory;
  const GraphElementType* graph_node_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"graph_node_type"}, GraphElementType::kNode,
      {{"p1", types::Int64Type()}}, /*element_tables=*/{}, &graph_node_type));

  std::unique_ptr<const ResolvedExpr> node_ref = MakeResolvedColumnRef(
      graph_node_type, ResolvedColumn(), /*is_correlated=*/false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> function,
                       fn_builder_.ElementDefinitionName(std::move(node_ref)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:element_definition_name(GRAPH_NODE(graph_node_type)<p1 INT64>) -> STRING)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64>, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest,
       IsNotDistinctFromArgumentTypeMismatchReturnsError) {
  std::unique_ptr<ResolvedExpr> left =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> right =
      MakeResolvedLiteral(types::StringType(), Value::String("test"),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.IsNotDistinctFrom(std::move(left), std::move(right)),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::HasSubstr("Inconsistent types")));
}

TEST_F(FunctionCallBuilderTest, IsNotDistinctFrom) {
  std::unique_ptr<ResolvedExpr> left =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> right =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.IsNotDistinctFrom(std::move(left), std::move(right)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$is_not_distinct_from(INT64, INT64) -> BOOL)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitUnsupportedTypeTest) {
  auto column =
      MakeResolvedColumnRef(types::TimeType(), ResolvedColumn(), false);
  // HLL_COUNT.INIT does not support TimeType.
  ASSERT_THAT(fn_builder_.HllInit(std::move(column)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, HllInitInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Int32Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(INT32) -> BYTES)
+-ColumnRef(type=INT32, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(INT64) -> BYTES)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitUint64Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(UINT64) -> BYTES)
+-ColumnRef(type=UINT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitStringTest) {
  auto column =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(STRING) -> BYTES)
+-ColumnRef(type=STRING, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(BYTES) -> BYTES)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergePartialBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMergePartial(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge_partial(BYTES) -> BYTES)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergePartialInt64TypeTest) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMergePartial(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge_partial(INT64) -> BYTES)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergeBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMerge(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge(BYTES) -> INT64)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergeInt64TypeTest) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMerge(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllExtractBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllExtract(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:hll_count.extract(BYTES) -> INT64)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllExtractInt64TypeTest) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllExtract(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:hll_count.extract(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, TimestampBucketTestTwoArgs) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(
      types::TimestampType(),
      Value::Timestamp(
          absl::FromUnixSeconds(1728864000)),  // 2024-10-14 00:00:00 UTC
      /*has_explicit_type=*/true));
  args.push_back(MakeResolvedLiteral(
      types::IntervalType(), Value::Interval(interval_testing::Hours(1)),
      /*has_explicit_type=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.TimestampBucket(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:timestamp_bucket(TIMESTAMP, INTERVAL) -> TIMESTAMP)
+-Literal(type=TIMESTAMP, value=2024-10-14 00:00:00+00, has_explicit_type=TRUE)
+-Literal(type=INTERVAL, value=0-0 0 1:0:0, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, TimestampBucketTestThreeArgs) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(
      types::TimestampType(),
      Value::Timestamp(
          absl::FromUnixSeconds(1728864000)),  // 2024-10-14 00:00:00 UTC
      /*has_explicit_type=*/true));
  args.push_back(MakeResolvedLiteral(
      types::IntervalType(), Value::Interval(interval_testing::Hours(1)),
      /*has_explicit_type=*/true));
  args.push_back(
      MakeResolvedLiteral(types::TimestampType(),
                          Value::Timestamp(absl::FromUnixSeconds(0)),  // Epoch
                          /*has_explicit_type=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.TimestampBucket(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:timestamp_bucket(TIMESTAMP, INTERVAL, optional(1) TIMESTAMP) -> TIMESTAMP)
+-Literal(type=TIMESTAMP, value=2024-10-14 00:00:00+00, has_explicit_type=TRUE)
+-Literal(type=INTERVAL, value=0-0 0 1:0:0, has_explicit_type=TRUE)
+-Literal(type=TIMESTAMP, value=1970-01-01 00:00:00+00, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, TimestampBucketTestWrongArgCount) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(
      MakeResolvedLiteral(types::TimestampType(),
                          Value::Timestamp(absl::FromUnixSeconds(1728864000)),
                          /*has_explicit_type=*/true));

  EXPECT_THAT(
      fn_builder_.TimestampBucket(std::move(args)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("arguments.size() == 2 || arguments.size() == 3")));
}

TEST_F(FunctionCallBuilderTest, ExtractFromIntervalTest) {
  std::unique_ptr<const ResolvedExpr> interval_expr = MakeResolvedLiteral(
      types::IntervalType(), Value::Interval(interval_testing::Months(13)),
      /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Extract(std::move(interval_expr), functions::YEAR));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
+-Literal(type=INTERVAL, value=1-1 0 0:0:0, has_explicit_type=TRUE)
+-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=YEAR)
)"));
}

TEST_F(FunctionCallBuilderTest, GenerateTimestampArrayWithDatePartTest) {
  std::unique_ptr<const ResolvedExpr> start_timestamp = MakeResolvedLiteral(
      types::TimestampType(),
      Value::Timestamp(absl::FromUnixSeconds(1728864000)),  // 2024-10-14
      /*has_explicit_type=*/true);
  std::unique_ptr<const ResolvedExpr> end_timestamp = MakeResolvedLiteral(
      types::TimestampType(),
      Value::Timestamp(absl::FromUnixSeconds(1729036800)),  // 2024-10-16
      /*has_explicit_type=*/true);
  std::unique_ptr<const ResolvedExpr> step =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  const EnumType* date_part_enum_type = types::DatePartEnumType();
  std::unique_ptr<const ResolvedExpr> date_part = MakeResolvedLiteral(
      date_part_enum_type, Value::Enum(date_part_enum_type, functions::DAY));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.GenerateTimestampArrayWithDatePart(
                           std::move(start_timestamp), std::move(end_timestamp),
                           std::move(step), std::move(date_part)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:generate_timestamp_array(TIMESTAMP, TIMESTAMP, INT64, ENUM<googlesql.functions.DateTimestampPart>) -> ARRAY<TIMESTAMP>)
+-Literal(type=TIMESTAMP, value=2024-10-14 00:00:00+00, has_explicit_type=TRUE)
+-Literal(type=TIMESTAMP, value=2024-10-16 00:00:00+00, has_explicit_type=TRUE)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=DAY)
)"));
}

TEST_F(FunctionCallBuilderTest, IntervalToNanosComplex) {
  // Test with the following interval: 1 day, 2 hours, 3 minutes, 4 seconds, 5
  // milliseconds, 6 nanoseconds
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(IntervalValue complex_interval,
                       IntervalValue::FromDays(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(complex_interval,
                       complex_interval + interval_testing::Hours(2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(complex_interval,
                       complex_interval + interval_testing::Minutes(3));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(complex_interval,
                       complex_interval + interval_testing::Seconds(4));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(complex_interval,
                       complex_interval + interval_testing::Nanos(5000006));

  std::unique_ptr<const ResolvedExpr> interval_expr = MakeResolvedLiteral(
      types::IntervalType(), Value::Interval(complex_interval), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> nanos_expr,
      fn_builder_.IntervalToNanos(std::move(interval_expr), "TEST_FUNC"));

  EXPECT_TRUE(nanos_expr->type()->IsInt64());
  EXPECT_EQ(nanos_expr->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:if(BOOL, INT64, INT64) -> INT64)
+-FunctionCall(GoogleSQL:$or(BOOL, repeated(1) BOOL) -> BOOL)
| +-FunctionCall(GoogleSQL:$not_equal(INT64, INT64) -> BOOL)
| | +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
| | | +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
| | | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=MONTH)
| | +-Literal(type=INT64, value=0)
| +-FunctionCall(GoogleSQL:$not_equal(INT64, INT64) -> BOOL)
|   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
|   | +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
|   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=YEAR)
|   +-Literal(type=INT64, value=0)
+-FunctionCall(GoogleSQL:error(STRING) -> INT64)
| +-Literal(type=STRING, value="Intervals with MONTH or YEAR parts are not supported in TEST_FUNC.")
+-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  | +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  | | +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  | | | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  | | | | +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  | | | | | +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
  | | | | | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=DAY)
  | | | | +-Literal(type=INT64, value=86400000000000)
  | | | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  | | |   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  | | |   | +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
  | | |   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=HOUR)
  | | |   +-Literal(type=INT64, value=3600000000000)
  | | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  | |   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  | |   | +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
  | |   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=MINUTE)
  | |   +-Literal(type=INT64, value=60000000000)
  | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  |   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  |   | +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
  |   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=SECOND)
  |   +-Literal(type=INT64, value=1000000000)
  +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
    +-Literal(type=INTERVAL, value=0-0 1 2:3:4.005000006, has_explicit_type=TRUE)
    +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=NANOSECOND)
)"));
}

TEST_F(FunctionCallBuilderTest, IntervalToNanosZero) {
  std::unique_ptr<const ResolvedExpr> interval_expr = MakeResolvedLiteral(
      types::IntervalType(), Value::Interval(IntervalValue()), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> nanos_expr,
      fn_builder_.IntervalToNanos(std::move(interval_expr), "TEST_FUNC"));

  EXPECT_TRUE(nanos_expr->type()->IsInt64());
}

TEST_F(FunctionCallBuilderTest, IntervalToNanosNegative) {
  IntervalValue negative_interval = interval_testing::Days(-1);
  std::unique_ptr<const ResolvedExpr> interval_expr = MakeResolvedLiteral(
      types::IntervalType(), Value::Interval(negative_interval), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> nanos_expr,
      fn_builder_.IntervalToNanos(std::move(interval_expr), "TEST_FUNC"));

  EXPECT_EQ(nanos_expr->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:if(BOOL, INT64, INT64) -> INT64)
+-FunctionCall(GoogleSQL:$or(BOOL, repeated(1) BOOL) -> BOOL)
| +-FunctionCall(GoogleSQL:$not_equal(INT64, INT64) -> BOOL)
| | +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
| | | +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
| | | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=MONTH)
| | +-Literal(type=INT64, value=0)
| +-FunctionCall(GoogleSQL:$not_equal(INT64, INT64) -> BOOL)
|   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
|   | +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
|   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=YEAR)
|   +-Literal(type=INT64, value=0)
+-FunctionCall(GoogleSQL:error(STRING) -> INT64)
| +-Literal(type=STRING, value="Intervals with MONTH or YEAR parts are not supported in TEST_FUNC.")
+-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  | +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  | | +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
  | | | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  | | | | +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  | | | | | +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
  | | | | | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=DAY)
  | | | | +-Literal(type=INT64, value=86400000000000)
  | | | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  | | |   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  | | |   | +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
  | | |   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=HOUR)
  | | |   +-Literal(type=INT64, value=3600000000000)
  | | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  | |   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  | |   | +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
  | |   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=MINUTE)
  | |   +-Literal(type=INT64, value=60000000000)
  | +-FunctionCall(GoogleSQL:$multiply(INT64, INT64) -> INT64)
  |   +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
  |   | +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
  |   | +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=SECOND)
  |   +-Literal(type=INT64, value=1000000000)
  +-FunctionCall(GoogleSQL:$extract(INTERVAL, ENUM<googlesql.functions.DateTimestampPart>) -> INT64)
    +-Literal(type=INTERVAL, value=0-0 -1 0:0:0, has_explicit_type=TRUE)
    +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=NANOSECOND)
)"));
}

class ValidateArgumentsDoNotContainCorrelationTest : public ::testing::Test {
 protected:
  ValidateArgumentsDoNotContainCorrelationTest()
      : column_factory_(0, &pool_, &seq_), catalog_("test_catalog") {}

  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    int64_type_ = type_factory_.get_int64();
    bool_type_ = type_factory_.get_bool();

    dummy_tvf_scan_ = MakeResolvedTVFScan(
        /*column_list=*/{},
        /*tvf=*/nullptr,
        /*signature=*/nullptr,
        /*argument_list=*/{},
        /*column_index_list=*/{},
        /*alias=*/"tvf1",
        /*function_call_signature=*/nullptr);
  }

  TypeFactory type_factory_;
  IdStringPool pool_;
  googlesql_base::SequenceNumber seq_;
  ColumnFactory column_factory_;
  SimpleCatalog catalog_;
  AnalyzerOptions analyzer_options_;
  const Type* int64_type_;
  const Type* bool_type_;
  std::unique_ptr<ResolvedTVFScan> dummy_tvf_scan_;
};

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentWithExternalCorrelation) {
  // An argument that CONTAINS a correlated column reference to outside the
  // expression. Conceptual SQL:
  // ... MY_FUNC(..., outer_table.a, ...) ...
  ResolvedColumn outer_column =
      column_factory_.MakeCol("outer_table", "a", int64_type_);
  auto correlated_expr =
      MakeResolvedColumnRef(int64_type_, outer_column, /*is_correlated=*/true);

  EXPECT_THAT(
      ValidateArgumentsDoNotContainCorrelation(dummy_tvf_scan_.get(), "MY_FUNC",
                                               {correlated_expr.get()}),
      StatusIs(
          absl::StatusCode::kUnimplemented,
          HasSubstr(
              "MY_FUNC arguments must not contain correlated references to "
              "columns "
              "defined outside the scope of the argument expression itself")));
}

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentWithInternalCorrelation) {
  // An argument containing a subquery with a correlated column reference,
  // but the correlation is *within* the argument's expression, not external.
  // Conceptual SQL:
  // ... MY_FUNC(..., (SELECT y.b FROM y WHERE y.a = outer_table.a), ...) ...
  // This is fine because the correlation is self-contained within the subquery.

  ResolvedColumn y_a = column_factory_.MakeCol("y", "a", int64_type_);
  ResolvedColumn y_b = column_factory_.MakeCol("y", "b", int64_type_);
  ResolvedColumn inner_outer_a =
      column_factory_.MakeCol("outer_table", "a", int64_type_);

  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list;
  parameter_list.push_back(MakeResolvedColumnRef(int64_type_, inner_outer_a,
                                                 /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto filter_expr,
      FunctionCallBuilder(analyzer_options_, catalog_, type_factory_)
          .Equal(
              MakeResolvedColumnRef(int64_type_, y_a, /*is_correlated=*/false),
              MakeResolvedColumnRef(int64_type_, inner_outer_a,
                                    /*is_correlated=*/true)));

  auto subquery = MakeResolvedSubqueryExpr(
      int64_type_, ResolvedSubqueryExpr::SCALAR,
      /*parameter_list=*/std::move(parameter_list),
      /*in_expr=*/nullptr,
      MakeResolvedProjectScan(
          /*column_list=*/{y_b},
          /*expr_list=*/{},
          /*input_scan=*/
          MakeResolvedFilterScan(
              /*column_list=*/{y_a, y_b},
              /*input_scan=*/
              MakeResolvedTableScan(
                  /*column_list=*/{y_a, y_b},
                  /*table=*/nullptr,
                  /*for_system_time_expr=*/nullptr),
              /*filter_expr=*/std::move(filter_expr))));

  GOOGLESQL_EXPECT_OK(ValidateArgumentsDoNotContainCorrelation(
      dummy_tvf_scan_.get(), "MY_FUNC", {subquery.get()}));
}

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentsWithoutCorrelation) {
  // Arguments that do NOT contain any externally correlated column refs.
  // Conceptual SQL:
  // ... MY_FUNC(..., local_table.b, 42, ...) ...
  ResolvedColumn local_column =
      column_factory_.MakeCol("local_table", "b", int64_type_);
  auto non_correlated_expr =
      MakeResolvedColumnRef(int64_type_, local_column, /*is_correlated=*/false);
  auto literal_expr = MakeResolvedLiteral(Value::Int64(42));

  GOOGLESQL_EXPECT_OK(ValidateArgumentsDoNotContainCorrelation(
      dummy_tvf_scan_.get(), "MY_FUNC",
      {non_correlated_expr.get(), literal_expr.get()}));
}

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentWithNonCorrelatedFunctionCall) {
  // Tests that a complex argument built using FunctionCallBuilder,
  // containing various function calls and only non-externally-correlated
  // column references, is correctly identified as not having external
  // correlations. Conceptual SQL:
  // ... MY_FUNC(..., local_table.b > 0 AND local_table.c < 0, ...) ...
  ResolvedColumn col_b =
      column_factory_.MakeCol("local_table", "b", int64_type_);
  ResolvedColumn col_c =
      column_factory_.MakeCol("local_table", "c", int64_type_);

  FunctionCallBuilder fn_builder(analyzer_options_, catalog_, type_factory_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto greater_expr,
      fn_builder.Greater(MakeResolvedColumnRef(int64_type_, col_b, false),
                         MakeResolvedLiteral(Value::Int64(0))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto less_expr,
      fn_builder.Less(MakeResolvedColumnRef(int64_type_, col_c, false),
                      MakeResolvedLiteral(Value::Int64(0))));

  std::vector<std::unique_ptr<const ResolvedExpr>> and_args;
  and_args.push_back(std::move(greater_expr));
  and_args.push_back(std::move(less_expr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto expr, fn_builder.And(std::move(and_args)));

  GOOGLESQL_EXPECT_OK(ValidateArgumentsDoNotContainCorrelation(dummy_tvf_scan_.get(),
                                                     "MY_FUNC", {expr.get()}));
}

class TvfFakes : public TableValuedFunction {
 public:
  explicit TvfFakes(absl::string_view name)
      : TableValuedFunction(
            {std::string(name)},
            FunctionSignature(
                FunctionArgumentType::RelationWithSchema(
                    TVFRelation({}),
                    /*extra_relation_input_columns_allowed=*/false),
                {}, /*non existing signature id*/ (int64_t)123456789)) {}

  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* tvf_signature) const override {
    *tvf_signature =
        std::make_shared<TVFSignature>(actual_arguments, TVFRelation({}));
    return absl::OkStatus();
  }
};

class TVFUtilsTest : public ::testing::Test {
 protected:
  TVFUtilsTest()
      : column_factory_(0, &pool_, &seq_), catalog_("test_catalog") {}

  void SetUp() override {
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        LanguageFeature::FEATURE_TUMBLE_HOP_TVFS);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        LanguageFeature::FEATURE_INTERVAL_TYPE);
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions(analyzer_options_.language()));
    type_factory_.get_int64();
    type_factory_.get_string();
    types::TimestampType();
  }

  TypeFactory type_factory_;
  IdStringPool pool_;
  googlesql_base::SequenceNumber seq_;
  ColumnFactory column_factory_;
  SimpleCatalog catalog_;
  AnalyzerOptions analyzer_options_;
};

TEST_F(TVFUtilsTest, ValidateTvfIsBuiltinAndSignatureMatches_Success) {
  const TableValuedFunction* tvf = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_.FindTableValuedFunction({"TUMBLE"}, &tvf));
  ASSERT_NE(tvf, nullptr);

  const FunctionSignature* function_signature = tvf->GetSignature(0);
  ASSERT_NE(function_signature, nullptr);
  ASSERT_EQ(function_signature->context_id(), FunctionSignatureId::FN_TUMBLE);

  std::shared_ptr<TVFSignature> tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{}, TVFRelation({}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/tvf,
      /*signature=*/tvf_signature,
      /*argument_list=*/{},
      /*column_index_list=*/{},
      /*alias=*/"fake_tvf",
      /*function_call_signature=*/nullptr);

  GOOGLESQL_EXPECT_OK(ValidateTvfIsBuiltinAndSignatureMatches(
      tvf_scan.get(), "TUMBLE", FunctionSignatureId::FN_TUMBLE));
}

TEST_F(TVFUtilsTest, ValidateTvfIsBuiltinAndSignatureMatches_NotBuiltin) {
  TvfFakes tvf_fake("non_builtin_tvf");

  std::shared_ptr<TVFSignature> signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{}, TVFRelation({}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/&tvf_fake,
      /*signature=*/signature,
      /*argument_list=*/{},
      /*column_index_list=*/{},
      /*alias=*/"fake_tvf",
      /*function_call_signature=*/nullptr);

  EXPECT_THAT(ValidateTvfIsBuiltinAndSignatureMatches(
                  tvf_scan.get(), "MY_TVF", FunctionSignatureId::FN_TUMBLE),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("TVF is not a GoogleSQL builtin")));
}

TEST_F(TVFUtilsTest, ValidateTvfIsBuiltinAndSignatureMatches_WrongSignatureId) {
  const TableValuedFunction* tvf = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_.FindTableValuedFunction({"TUMBLE"}, &tvf));
  ASSERT_NE(tvf, nullptr);

  const FunctionSignature* function_signature = tvf->GetSignature(0);
  ASSERT_NE(function_signature, nullptr);

  std::shared_ptr<TVFSignature> tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{}, TVFRelation({}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/tvf,
      /*signature=*/tvf_signature,
      /*argument_list=*/{},
      /*column_index_list=*/{},
      /*alias=*/"tumble_tvf",
      /*function_call_signature=*/nullptr);

  EXPECT_THAT(ValidateTvfIsBuiltinAndSignatureMatches(
                  tvf_scan.get(), "TUMBLE", FunctionSignatureId::FN_HOP),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("TVF signature for TUMBLE is not supported")));
}

TEST_F(TVFUtilsTest, FindAndValidateTimestampColumnInScan_Success) {
  ResolvedColumn ts_col =
      column_factory_.MakeCol("input", "time_col", types::TimestampType());
  ResolvedColumn other_col =
      column_factory_.MakeCol("input", "val", type_factory_.get_int64());
  auto input_scan = MakeResolvedSingleRowScan({ts_col, other_col});

  std::vector<std::unique_ptr<const ResolvedTVFArgument>> arguments;
  arguments.push_back(MakeResolvedTVFArgument(
      /*expr=*/MakeResolvedLiteral(types::StringType(),
                                   Value::String("time_col")),
      /*scan=*/nullptr,
      /*model=*/nullptr,
      /*connection=*/nullptr,
      /*descriptor_arg=*/nullptr,
      /*argument_column_list=*/{}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/nullptr,
      /*signature=*/nullptr,
      /*argument_list=*/std::move(arguments),
      /*column_index_list=*/{},
      /*alias=*/"test_tvf",
      /*function_call_signature=*/nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedColumn* found_col,
      FindAndValidateTimestampColumnInScan(*tvf_scan->argument_list()[0],
                                           "time_col", *input_scan, "MY_TVF",
                                           ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ(found_col->name(), "time_col");
  EXPECT_TRUE(found_col->type()->IsTimestamp());
}

TEST_F(TVFUtilsTest, FindAndValidateTimestampColumnInScan_AmbiguousTsColumn) {
  ResolvedColumn ts_col =
      column_factory_.MakeCol("input", "time_col", types::TimestampType());
  ResolvedColumn other_col =
      column_factory_.MakeCol("input", "time_col", type_factory_.get_int64());
  auto input_scan = MakeResolvedSingleRowScan({ts_col, other_col});

  std::vector<std::unique_ptr<const ResolvedTVFArgument>> arguments;
  arguments.push_back(MakeResolvedTVFArgument(
      /*expr=*/MakeResolvedLiteral(types::StringType(),
                                   Value::String("time_col")),
      /*scan=*/nullptr,
      /*model=*/nullptr,
      /*connection=*/nullptr,
      /*descriptor_arg=*/nullptr,
      /*argument_column_list=*/{}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/nullptr,
      /*signature=*/nullptr,
      /*argument_list=*/std::move(arguments),
      /*column_index_list=*/{},
      /*alias=*/"test_tvf",
      /*function_call_signature=*/nullptr);

  EXPECT_THAT(FindAndValidateTimestampColumnInScan(
                  *tvf_scan->argument_list()[0], "time_col", *input_scan,
                  "MY_TVF", ProductMode::PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("is ambiguous in the input table")));
}

TEST_F(TVFUtilsTest, FindAndValidateTimestampColumnInScan_NotFound) {
  ResolvedColumn other_col =
      column_factory_.MakeCol("input", "val", type_factory_.get_int64());
  auto input_scan = MakeResolvedSingleRowScan({other_col});

  std::vector<std::unique_ptr<const ResolvedTVFArgument>> arguments;
  arguments.push_back(MakeResolvedTVFArgument(
      /*expr=*/MakeResolvedLiteral(types::StringType(),
                                   Value::String("time_col")),
      /*scan=*/nullptr,
      /*model=*/nullptr,
      /*connection=*/nullptr,
      /*descriptor_arg=*/nullptr,
      /*argument_column_list=*/{}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/nullptr,
      /*signature=*/nullptr,
      /*argument_list=*/std::move(arguments),
      /*column_index_list=*/{},
      /*alias=*/"test_tvf",
      /*function_call_signature=*/nullptr);

  EXPECT_THAT(FindAndValidateTimestampColumnInScan(
                  *tvf_scan->argument_list()[0], "time_col", *input_scan,
                  "MY_TVF", ProductMode::PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Timestamp column 'time_col' not found")));
}

TEST_F(TVFUtilsTest, FindAndValidateTimestampColumnInScan_WrongType) {
  ResolvedColumn ts_col = column_factory_.MakeCol(
      "input", "time_col", type_factory_.get_string());  // Wrong type
  auto input_scan = MakeResolvedSingleRowScan({ts_col});

  std::vector<std::unique_ptr<const ResolvedTVFArgument>> arguments;
  arguments.push_back(MakeResolvedTVFArgument(
      /*expr=*/MakeResolvedLiteral(types::StringType(),
                                   Value::String("time_col")),
      /*scan=*/nullptr,
      /*model=*/nullptr,
      /*connection=*/nullptr,
      /*descriptor_arg=*/nullptr,
      /*argument_column_list=*/{}));

  auto tvf_scan = MakeResolvedTVFScan(
      /*column_list=*/{},
      /*tvf=*/nullptr,
      /*signature=*/nullptr,
      /*argument_list=*/std::move(arguments),
      /*column_index_list=*/{},
      /*alias=*/"test_tvf",
      /*function_call_signature=*/nullptr);

  EXPECT_THAT(
      FindAndValidateTimestampColumnInScan(*tvf_scan->argument_list()[0],
                                           "time_col", *input_scan, "MY_TVF",
                                           ProductMode::PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("must be of type TIMESTAMP, but column 'time_col' has "
                         "type STRING")));
}

class FakeExpr : public ResolvedExpr {
 public:
  FakeExpr()
      : ResolvedExpr(types::Int64Type(),
                     ResolvedNode::ConstructorOverload::NEW_CONSTRUCTOR) {}
  ~FakeExpr() override = default;

  ResolvedNodeKind node_kind() const override { return RESOLVED_LITERAL; }
  std::string node_kind_string() const override { return "Literal"; }
  absl::Status Accept(ResolvedASTVisitor* visitor) const override {
    return visitor->DefaultVisit(this);
  }
  absl::Status SaveTo(Type::FileDescriptorSetMap* file_descriptor_set_map,
                      AnyResolvedExprProto* proto) const override {
    return absl::UnimplementedError("SaveTo not implemented for FakeExpr");
  }
};

TEST(RewriteUtilsTest, CorrelateColumnRefsHandlesCopyFailureGracefully) {
  auto fake_expr = std::make_unique<FakeExpr>();
  auto subquery =
      MakeResolvedSubqueryExpr(types::Int64Type(), ResolvedSubqueryExpr::SCALAR,
                               /*parameter_list=*/{},
                               /*in_expr=*/std::move(fake_expr),
                               MakeResolvedSingleRowScan(/*column_list=*/{}));

  EXPECT_THAT(CorrelateColumnRefs(*subquery),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unhandled node type in deep copy")));
}

TEST_F(TVFUtilsTest, CteQueryNameGeneratorScansExistingCteNamesInAst) {
  auto with_entry_1 =
      MakeResolvedWithEntry("cte_1", MakeResolvedSingleRowScan());
  auto with_entry_base =
      MakeResolvedWithEntry("cte", MakeResolvedSingleRowScan());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto with_scan,
                       ResolvedWithScanBuilder()
                           .add_with_entry_list(std::move(with_entry_1))
                           .add_with_entry_list(std::move(with_entry_base))
                           .set_query(MakeResolvedSingleRowScan())
                           .set_recursive(false)
                           .Build());

  CteQueryNameGenerator generator;
  GOOGLESQL_EXPECT_OK(with_scan->Accept(&generator));

  // Passing empty base_name falls back to "cte", which collides with existing
  // "cte" and "cte_1" in AST, producing "cte_2".
  EXPECT_EQ(generator.MakeUniqueCteName(""), "cte_2");
  EXPECT_EQ(generator.MakeUniqueCteName(""), "cte_3");

  // First time using "hop_params" -> uses "hop_params" directly since no
  // collision
  EXPECT_EQ(generator.MakeUniqueCteName("hop_params"), "hop_params");

  // Second time using "hop_params" -> collides, gets "hop_params_4" (since
  // max_seen_index is 4)
  EXPECT_EQ(generator.MakeUniqueCteName("hop_params"), "hop_params_4");
  EXPECT_EQ(generator.max_seen_index(), 4);

  // Third time using "hop_params" -> collides, gets "hop_params_5"
  EXPECT_EQ(generator.MakeUniqueCteName("hop_params"), "hop_params_5");
  EXPECT_EQ(generator.max_seen_index(), 5);

  // First time using "tumble_params" -> uses "tumble_params" directly
  EXPECT_EQ(generator.MakeUniqueCteName("tumble_params"), "tumble_params");
}

TEST(RewriteUtilsTest, ReplaceScanColumnsSimple) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<ResolvedTableScan> input = MakeResolvedTableScan(
      /*column_list=*/{factory.MakeCol("tab", "col", types::Int64Type())},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);
  input->add_column_index_list(0);

  EXPECT_EQ(input->column_list(0).column_id(), 1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedScan> output,
      ReplaceScanColumns(factory, *input, input->column_index_list(),
                         input->column_list()));

  EXPECT_EQ(output->column_list(0).column_id(), 1);
}

TEST(RewriteUtilsTest, ReplaceScanColumnsOutOfOrder) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {
                               {"col1", types::Int64Type()},
                               {"col2", types::Int64Type()},
                           });
  std::unique_ptr<ResolvedTableScan> input = MakeResolvedTableScan(
      /*column_list=*/{factory.MakeCol("tab", "col1", types::Int64Type()),
                       factory.MakeCol("tab", "col2", types::Int64Type())},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  EXPECT_EQ(input->column_list(0).column_id(), 1);
  EXPECT_EQ(input->column_list(1).column_id(), 2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedScan> output,
      ReplaceScanColumns(factory, *input,
                         {1, 0},  // Note the out-of-order indices.
                         input->column_list()));

  EXPECT_EQ(output->column_list(0).column_id(), 2);
  EXPECT_EQ(output->column_list(1).column_id(), 1);
}

TEST(RewriteUtilsTest, ReplaceScanColumnsDuplicateColumns) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});

  ResolvedColumn col = factory.MakeCol("tab", "col", types::Int64Type());
  ResolvedColumn col2 = factory.MakeCol("tab", "col", types::Int64Type());
  std::unique_ptr<ResolvedScan> input =
      MakeResolvedProjectScan({col, col}, {},
                              MakeResolvedTableScan(
                                  /*column_list=*/
                                  {col},
                                  /*table=*/&table,
                                  /*for_system_time_expr=*/nullptr));

  EXPECT_EQ(input->column_list(0).column_id(), 1);
  EXPECT_EQ(input->column_list(1).column_id(), 1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedScan> output,
      ReplaceScanColumns(factory, *input, {0, 1}, {col, col2}));

  EXPECT_EQ(output->column_list(0).column_id(), 1);
  EXPECT_EQ(output->column_list(1).column_id(), 2);
  const ResolvedComputedColumn* computed_column =
      output->GetAs<ResolvedProjectScan>()->expr_list(0);
  EXPECT_EQ(computed_column->column().column_id(), 2);
  EXPECT_EQ(computed_column->column().name(), "col");
  EXPECT_EQ(
      computed_column->expr()->GetAs<ResolvedColumnRef>()->column().column_id(),
      1);
}

TEST(RewriteUtilsTest, ReplaceScanColumnsOutOfOrderDuplicateColumns) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});

  ResolvedColumn col = factory.MakeCol("tab", "col", types::Int64Type());
  ResolvedColumn col2 = factory.MakeCol("tab", "col", types::Int64Type());
  std::unique_ptr<ResolvedScan> input =
      MakeResolvedProjectScan({col, col}, {},
                              MakeResolvedTableScan(
                                  /*column_list=*/
                                  {col},
                                  /*table=*/&table,
                                  /*for_system_time_expr=*/nullptr));

  EXPECT_EQ(input->column_list(0).column_id(), 1);
  EXPECT_EQ(input->column_list(1).column_id(), 1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedScan> output,
      ReplaceScanColumns(factory, *input,
                         {1, 0},  // Note the out-of-order indices.
                         {col, col2}));

  EXPECT_EQ(output->column_list(0).column_id(), 2);
  EXPECT_EQ(output->column_list(1).column_id(), 1);
  const ResolvedComputedColumn* computed_column =
      output->GetAs<ResolvedProjectScan>()->expr_list(0);
  EXPECT_EQ(computed_column->column().column_id(), 2);
  EXPECT_EQ(computed_column->column().name(), "col");
  EXPECT_EQ(
      computed_column->expr()->GetAs<ResolvedColumnRef>()->column().column_id(),
      1);
}

TEST(RewriteUtilsTest, ReplaceScanColumnsMultipleDuplicateColumns) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table(
      "tab", {{"colx", types::Int64Type()}, {"coly", types::Int64Type()}});

  ResolvedColumn colx = factory.MakeCol("tab", "colx", types::Int64Type());
  ResolvedColumn coly = factory.MakeCol("tab", "coly", types::Int64Type());
  ResolvedColumn colx2 = factory.MakeCol("tab", "colx", types::Int64Type());
  ResolvedColumn coly2 = factory.MakeCol("tab", "coly", types::Int64Type());
  std::unique_ptr<ResolvedScan> input =
      MakeResolvedProjectScan({colx, coly, colx, coly}, {},
                              MakeResolvedTableScan(
                                  /*column_list=*/
                                  {colx, coly},
                                  /*table=*/&table,
                                  /*for_system_time_expr=*/nullptr));

  EXPECT_EQ(input->column_list(0).column_id(), 1);
  EXPECT_EQ(input->column_list(1).column_id(), 2);
  EXPECT_EQ(input->column_list(2).column_id(), 1);
  EXPECT_EQ(input->column_list(3).column_id(), 2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedScan> output,
                       ReplaceScanColumns(factory, *input, {0, 1, 2, 3},
                                          {colx, coly, colx2, coly2}));

  EXPECT_EQ(output->column_list(0).column_id(), 1);
  EXPECT_EQ(output->column_list(1).column_id(), 2);
  EXPECT_EQ(output->column_list(2).column_id(), 3);
  EXPECT_EQ(output->column_list(3).column_id(), 4);
  ASSERT_TRUE(output->Is<ResolvedProjectScan>());
  const ResolvedProjectScan* project_scan =
      output->GetAs<ResolvedProjectScan>();
  ASSERT_EQ(project_scan->expr_list_size(), 2);
  const ResolvedComputedColumn* computed_column_x = project_scan->expr_list(0);
  EXPECT_EQ(computed_column_x->column().column_id(), 3);
  EXPECT_EQ(computed_column_x->column().name(), "colx");
  EXPECT_EQ(computed_column_x->expr()
                ->GetAs<ResolvedColumnRef>()
                ->column()
                .column_id(),
            1);
  const ResolvedComputedColumn* computed_column_y = project_scan->expr_list(1);
  EXPECT_EQ(computed_column_y->column().column_id(), 4);
  EXPECT_EQ(computed_column_y->column().name(), "coly");
  EXPECT_EQ(computed_column_y->expr()
                ->GetAs<ResolvedColumnRef>()
                ->column()
                .column_id(),
            2);
}

TEST(RewriteUtilsTest, CreateReplacementColumnsEmpty) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);

  ColumnReplacementMap replaced_column_map;
  std::vector<ResolvedColumn> replacement_columns = CreateReplacementColumns(
      factory, /*column_list=*/{}, replaced_column_map);

  EXPECT_THAT(replacement_columns, IsEmpty());
  EXPECT_TRUE(replaced_column_map.empty());
}

TEST(RewriteUtilsTest, CreateReplacementColumnsBasic) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);

  ResolvedColumn col1 =
      factory.MakeCol("tab", "col1", types::Int64Type());  // id 1
  ResolvedColumn col2 =
      factory.MakeCol("tab", "col2", types::StringType());  // id 2

  ColumnReplacementMap replaced_column_map;
  std::vector<ResolvedColumn> replacement_columns = CreateReplacementColumns(
      factory, /*column_list=*/{col1, col2}, replaced_column_map);

  ASSERT_THAT(replacement_columns, SizeIs(2));
  EXPECT_EQ(replacement_columns[0].column_id(), 3);
  EXPECT_EQ(replacement_columns[0].table_name(), "tab");
  EXPECT_EQ(replacement_columns[0].name(), "col1");
  EXPECT_TRUE(replacement_columns[0].type()->Equals(types::Int64Type()));

  EXPECT_EQ(replacement_columns[1].column_id(), 4);
  EXPECT_EQ(replacement_columns[1].table_name(), "tab");
  EXPECT_EQ(replacement_columns[1].name(), "col2");
  EXPECT_TRUE(replacement_columns[1].type()->Equals(types::StringType()));

  EXPECT_EQ(replaced_column_map.size(), 2);
  EXPECT_EQ(replaced_column_map[replacement_columns[0]], col1);
  EXPECT_EQ(replaced_column_map[replacement_columns[1]], col2);
}

TEST(RewriteUtilsTest, CreateReplacementColumnsWithDuplicates) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);

  ResolvedColumn col1 =
      factory.MakeCol("tab", "col1", types::Int64Type());  // id 1

  ColumnReplacementMap replaced_column_map;
  std::vector<ResolvedColumn> replacement_columns = CreateReplacementColumns(
      factory, /*column_list=*/{col1, col1}, replaced_column_map);

  ASSERT_THAT(replacement_columns, SizeIs(2));
  EXPECT_EQ(replacement_columns[0].column_id(), 2);
  EXPECT_EQ(replacement_columns[1].column_id(), 3);
  EXPECT_NE(replacement_columns[0], replacement_columns[1]);

  EXPECT_EQ(replaced_column_map.size(), 2);
  EXPECT_EQ(replaced_column_map[replacement_columns[0]], col1);
  EXPECT_EQ(replaced_column_map[replacement_columns[1]], col1);
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanEmptyScan) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab");
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  ColumnReplacementMap column_map;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ResolvedExecuteAsRoleScan> result,
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/&table,
                                      /*original_inlined_tvf=*/nullptr));

  ASSERT_NE(result, nullptr);
  EXPECT_THAT(result->column_list(), IsEmpty());
  EXPECT_EQ(result->original_inlined_view(), &table);
  EXPECT_EQ(result->original_inlined_tvf(), nullptr);
  ASSERT_NE(result->input_scan(), nullptr);
  EXPECT_THAT(result->input_scan()->column_list(), IsEmpty());
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanAllColumnsMapped) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table(
      "tab", {{"col1", types::Int64Type()}, {"col2", types::StringType()}});

  ResolvedColumn scan_col1 =
      factory.MakeCol("tab", "col1", types::Int64Type());  // id 1
  ResolvedColumn scan_col2 =
      factory.MakeCol("tab", "col2", types::StringType());  // id 2
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{scan_col1, scan_col2},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  ResolvedColumn orig_col1 =
      factory.MakeCol("orig_tab", "orig_col1", types::Int64Type());  // id 3
  ResolvedColumn orig_col2 =
      factory.MakeCol("orig_tab", "orig_col2", types::StringType());  // id 4

  ColumnReplacementMap column_map;
  column_map.insert({scan_col1, orig_col1});
  column_map.insert({scan_col2, orig_col2});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ResolvedExecuteAsRoleScan> result,
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/&table,
                                      /*original_inlined_tvf=*/nullptr));

  ASSERT_NE(result, nullptr);
  ASSERT_THAT(result->column_list(), SizeIs(2));
  EXPECT_EQ(result->column_list(0), orig_col1);
  EXPECT_EQ(result->column_list(0).table_name(), "orig_tab");
  EXPECT_EQ(result->column_list(0).name(), "orig_col1");
  EXPECT_TRUE(result->column_list(0).type()->Equals(types::Int64Type()));

  EXPECT_EQ(result->column_list(1), orig_col2);
  EXPECT_EQ(result->column_list(1).table_name(), "orig_tab");
  EXPECT_EQ(result->column_list(1).name(), "orig_col2");
  EXPECT_TRUE(result->column_list(1).type()->Equals(types::StringType()));

  // No new columns should have been allocated during execute as role scan
  // creation.
  EXPECT_EQ(factory.max_column_id(), orig_col2.column_id());
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanNoColumnsMapped) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table(
      "tab", {{"col1", types::Int64Type()}, {"col2", types::StringType()}});

  ResolvedColumn scan_col1 =
      factory.MakeCol("tab", "col1", types::Int64Type());  // id 1
  ResolvedColumn scan_col2 =
      factory.MakeCol("tab", "col2", types::StringType());  // id 2
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{scan_col1, scan_col2},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  ColumnReplacementMap column_map;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ResolvedExecuteAsRoleScan> result,
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/&table,
                                      /*original_inlined_tvf=*/nullptr));

  ASSERT_NE(result, nullptr);
  ASSERT_THAT(result->column_list(), SizeIs(2));

  // First column should have same attributes as scan_col1 but a newly allocated
  // column ID.
  EXPECT_EQ(result->column_list(0).column_id(), 3);
  EXPECT_NE(result->column_list(0), scan_col1);
  EXPECT_EQ(result->column_list(0).table_name(), "tab");
  EXPECT_EQ(result->column_list(0).name(), "col1");
  EXPECT_TRUE(result->column_list(0).type()->Equals(types::Int64Type()));

  // Second column should have same attributes as scan_col2 but a newly
  // allocated column ID.
  EXPECT_EQ(result->column_list(1).column_id(), 4);
  EXPECT_NE(result->column_list(1), scan_col2);
  EXPECT_EQ(result->column_list(1).table_name(), "tab");
  EXPECT_EQ(result->column_list(1).name(), "col2");
  EXPECT_TRUE(result->column_list(1).type()->Equals(types::StringType()));

  EXPECT_EQ(factory.max_column_id(), 4);
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanSubsetColumnsMapped) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {{"c0", types::Int64Type()},
                            {"c1", types::StringType()},
                            {"c2", types::BoolType()},
                            {"c3", types::DoubleType()}});

  ResolvedColumn scan_c0 =
      factory.MakeCol("tab", "c0", types::Int64Type());  // id 1
  ResolvedColumn scan_c1 =
      factory.MakeCol("tab", "c1", types::StringType());  // id 2
  ResolvedColumn scan_c2 =
      factory.MakeCol("tab", "c2", types::BoolType());  // id 3
  ResolvedColumn scan_c3 =
      factory.MakeCol("tab", "c3", types::DoubleType());  // id 4

  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{scan_c0, scan_c1, scan_c2, scan_c3},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  ResolvedColumn orig_c0 =
      factory.MakeCol("orig", "orig0", types::Int64Type());  // id 5
  ResolvedColumn orig_c2 =
      factory.MakeCol("orig", "orig2", types::BoolType());  // id 6

  ColumnReplacementMap column_map;
  column_map.insert({scan_c0, orig_c0});
  column_map.insert({scan_c2, orig_c2});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ResolvedExecuteAsRoleScan> result,
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/&table,
                                      /*original_inlined_tvf=*/nullptr));

  ASSERT_NE(result, nullptr);
  ASSERT_THAT(result->column_list(), SizeIs(4));

  // Index 0: matched scan_c0 -> orig_c0
  EXPECT_EQ(result->column_list(0), orig_c0);
  EXPECT_EQ(result->column_list(0).column_id(), 5);
  EXPECT_EQ(result->column_list(0).table_name(), "orig");
  EXPECT_EQ(result->column_list(0).name(), "orig0");
  EXPECT_TRUE(result->column_list(0).type()->Equals(types::Int64Type()));

  // Index 1: not mapped -> new column with attributes of scan_c1
  EXPECT_EQ(result->column_list(1).column_id(), 7);
  EXPECT_EQ(result->column_list(1).table_name(), "tab");
  EXPECT_EQ(result->column_list(1).name(), "c1");
  EXPECT_TRUE(result->column_list(1).type()->Equals(types::StringType()));

  // Index 2: matched scan_c2 -> orig_c2
  EXPECT_EQ(result->column_list(2), orig_c2);
  EXPECT_EQ(result->column_list(2).column_id(), 6);
  EXPECT_EQ(result->column_list(2).table_name(), "orig");
  EXPECT_EQ(result->column_list(2).name(), "orig2");
  EXPECT_TRUE(result->column_list(2).type()->Equals(types::BoolType()));

  // Index 3: not mapped -> new column with attributes of scan_c3
  EXPECT_EQ(result->column_list(3).column_id(), 8);
  EXPECT_EQ(result->column_list(3).table_name(), "tab");
  EXPECT_EQ(result->column_list(3).name(), "c3");
  EXPECT_TRUE(result->column_list(3).type()->Equals(types::DoubleType()));
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanDuplicateColumns) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {{"c0", types::Int64Type()}});

  ResolvedColumn scan_c0 =
      factory.MakeCol("tab", "c0", types::Int64Type());  // id 1

  // Scan with duplicated column scan_c0
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{scan_c0, scan_c0},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  ResolvedColumn orig_c0 =
      factory.MakeCol("orig", "orig0", types::Int64Type());  // id 2

  ColumnReplacementMap column_map;
  column_map.insert({scan_c0, orig_c0});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ResolvedExecuteAsRoleScan> result,
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/&table,
                                      /*original_inlined_tvf=*/nullptr));

  ASSERT_NE(result, nullptr);
  ASSERT_THAT(result->column_list(), SizeIs(2));

  // First occurrence of scan_c0 matches column_map -> orig_c0
  EXPECT_EQ(result->column_list(0), orig_c0);
  EXPECT_EQ(result->column_list(0).column_id(), 2);
  EXPECT_EQ(result->column_list(0).table_name(), "orig");
  EXPECT_EQ(result->column_list(0).name(), "orig0");
  EXPECT_TRUE(result->column_list(0).type()->Equals(types::Int64Type()));

  // Second occurrence of scan_c0 was already seen, so it receives a newly
  // allocated column ID instead of duplicating orig_c0.
  EXPECT_EQ(result->column_list(1).column_id(), 3);
  EXPECT_NE(result->column_list(1), orig_c0);
  EXPECT_NE(result->column_list(1), scan_c0);
  EXPECT_EQ(result->column_list(1).table_name(), "tab");
  EXPECT_EQ(result->column_list(1).name(), "c0");
  EXPECT_TRUE(result->column_list(1).type()->Equals(types::Int64Type()));
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanWithTVF) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab", {{"c0", types::Int64Type()}});

  ResolvedColumn scan_c0 =
      factory.MakeCol("tab", "c0", types::Int64Type());  // id 1
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{scan_c0},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  TvfFakes tvf("my_tvf");
  ColumnReplacementMap column_map;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ResolvedExecuteAsRoleScan> result,
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/nullptr,
                                      /*original_inlined_tvf=*/&tvf));

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->original_inlined_view(), nullptr);
  EXPECT_EQ(result->original_inlined_tvf(), &tvf);
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanBothViewAndTVFNull) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab");
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  ColumnReplacementMap column_map;
  EXPECT_THAT(
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/nullptr,
                                      /*original_inlined_tvf=*/nullptr),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Only one of original_inlined_view or "
                         "original_inlined_tvf should be set.")));
}

TEST(RewriteUtilsTest, CreateResolvedExecuteAsRoleScanBothViewAndTVFSet) {
  IdStringPool id_string_pool;
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, id_string_pool, sequence);
  SimpleTable table("tab");
  std::unique_ptr<ResolvedTableScan> scan = MakeResolvedTableScan(
      /*column_list=*/{},
      /*table=*/&table,
      /*for_system_time_expr=*/nullptr);

  TvfFakes tvf("my_tvf");
  ColumnReplacementMap column_map;
  EXPECT_THAT(
      CreateResolvedExecuteAsRoleScan(factory, column_map, std::move(scan),
                                      /*original_inlined_view=*/&table,
                                      /*original_inlined_tvf=*/&tvf),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Only one of original_inlined_view or "
                         "original_inlined_tvf should be set.")));
}

TEST(RewriteUtilsTest, HasNonTerminalInsertScan) {
  EXPECT_FALSE(HasNonTerminalInsertScan(nullptr));

  SimpleTable table("tab");
  auto table_scan = MakeResolvedTableScan({}, &table, nullptr);
  EXPECT_FALSE(HasNonTerminalInsertScan(table_scan.get()));

  // Terminal insert scan: FinishScan wrapping an InsertScan with single row
  // query.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto terminal_insert_stmt,
      ResolvedInsertStmtBuilder()
          .set_table_scan(MakeResolvedTableScan({}, &table, nullptr))
          .set_insert_mode(ResolvedInsertStmt::OR_ERROR)
          .set_query(MakeResolvedSingleRowScan())
          .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto terminal_insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(terminal_insert_stmt))
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(terminal_insert_scan))
                           .Build());
  EXPECT_FALSE(HasNonTerminalInsertScan(finish_scan.get()));

  // Non-terminal insert scan: InsertScan not wrapped by FinishScan.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto non_terminal_insert_stmt,
      ResolvedInsertStmtBuilder()
          .set_table_scan(MakeResolvedTableScan({}, &table, nullptr))
          .set_insert_mode(ResolvedInsertStmt::OR_ERROR)
          .set_query(MakeResolvedSingleRowScan())
          .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto non_terminal_insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(non_terminal_insert_stmt))
                           .Build());
  EXPECT_TRUE(HasNonTerminalInsertScan(non_terminal_insert_scan.get()));

  // Nested insert scan in query: FinishScan wrapping an InsertScan, but the
  // InsertScan's query contains another InsertScan.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto inner_insert_stmt,
      ResolvedInsertStmtBuilder()
          .set_table_scan(MakeResolvedTableScan({}, &table, nullptr))
          .set_insert_mode(ResolvedInsertStmt::OR_ERROR)
          .set_query(MakeResolvedSingleRowScan())
          .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto inner_insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(inner_insert_stmt))
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto outer_insert_stmt,
      ResolvedInsertStmtBuilder()
          .set_table_scan(MakeResolvedTableScan({}, &table, nullptr))
          .set_insert_mode(ResolvedInsertStmt::OR_ERROR)
          .set_query(std::move(inner_insert_scan))
          .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto outer_insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(outer_insert_stmt))
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto outer_finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(outer_insert_scan))
                           .Build());
  EXPECT_TRUE(HasNonTerminalInsertScan(outer_finish_scan.get()));

  // FinishScan wrapping a non-insert scan (TableScan) with no insert scans.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto finish_table_scan,
      ResolvedFinishScanBuilder()
          .set_input_scan(MakeResolvedTableScan({}, &table, nullptr))
          .Build());
  EXPECT_FALSE(HasNonTerminalInsertScan(finish_table_scan.get()));

  // FinishScan wrapping a non-insert scan (TableScan), while a non-terminal
  // insert scan exists in a WithScan entry.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto with_insert_stmt,
      ResolvedInsertStmtBuilder()
          .set_table_scan(MakeResolvedTableScan({}, &table, nullptr))
          .set_insert_mode(ResolvedInsertStmt::OR_ERROR)
          .set_query(MakeResolvedSingleRowScan())
          .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto with_insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(with_insert_stmt))
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto with_scan,
                       ResolvedWithScanBuilder()
                           .add_with_entry_list(MakeResolvedWithEntry(
                               "cte", std::move(with_insert_scan)))
                           .set_query(std::move(finish_table_scan))
                           .set_recursive(false)
                           .Build());
  EXPECT_TRUE(HasNonTerminalInsertScan(with_scan.get()));
}

TEST(RewriteUtilsTest, RemapCteNames_Basic) {
  // Construct: WITH cte AS (SELECT 1) SELECT * FROM cte
  auto with_entry = MakeResolvedWithEntry("cte", MakeResolvedSingleRowScan());
  auto with_ref = MakeResolvedWithRefScan(/*column_list=*/{}, "cte");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto with_scan,
                       ResolvedWithScanBuilder()
                           .add_with_entry_list(std::move(with_entry))
                           .set_query(std::move(with_ref))
                           .set_recursive(false)
                           .BuildMutable());

  int cte_counter = 0;
  GOOGLESQL_ASSERT_OK(RemapCteNames(with_scan.get(), cte_counter));

  // Counter must increment to 1
  EXPECT_EQ(cte_counter, 1);

  // Both definition and reference must be remapped to the same unique name
  EXPECT_EQ(with_scan->with_entry_list(0)->with_query_name(),
            "$inlined_cte_0_cte");
  EXPECT_EQ(with_scan->query()->GetAs<ResolvedWithRefScan>()->with_query_name(),
            "$inlined_cte_0_cte");
}

TEST(RewriteUtilsTest, RemapCteNames_NestedShadowing) {
  // Construct:
  // WITH cte AS (SELECT 1) -- outer
  // SELECT * FROM (
  //   WITH cte AS (SELECT 2) -- inner (shadows outer)
  //   SELECT * FROM cte -- references inner
  // ) JOIN cte -- references outer
  auto inner_entry = MakeResolvedWithEntry("cte", MakeResolvedSingleRowScan());
  auto inner_ref = MakeResolvedWithRefScan(/*column_list=*/{}, "cte");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto inner_with_scan,
                       ResolvedWithScanBuilder()
                           .add_with_entry_list(std::move(inner_entry))
                           .set_query(std::move(inner_ref))
                           .set_recursive(false)
                           .Build());

  auto outer_entry = MakeResolvedWithEntry("cte", MakeResolvedSingleRowScan());
  auto outer_ref = MakeResolvedWithRefScan(/*column_list=*/{}, "cte");

  // Join inner_with_scan with outer_ref
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto join_scan,
                       ResolvedJoinScanBuilder()
                           .set_left_scan(std::move(inner_with_scan))
                           .set_right_scan(std::move(outer_ref))
                           .set_join_type(ResolvedJoinScan::INNER)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto outer_with_scan,
                       ResolvedWithScanBuilder()
                           .add_with_entry_list(std::move(outer_entry))
                           .set_query(std::move(join_scan))
                           .set_recursive(false)
                           .BuildMutable());

  int cte_counter = 0;
  GOOGLESQL_ASSERT_OK(RemapCteNames(outer_with_scan.get(), cte_counter));

  // Both outer and inner CTEs were remapped
  EXPECT_EQ(cte_counter, 2);

  // Outer CTE definition is $inlined_cte_0_cte
  EXPECT_EQ(outer_with_scan->with_entry_list(0)->with_query_name(),
            "$inlined_cte_0_cte");

  const auto* main_join = outer_with_scan->query()->GetAs<ResolvedJoinScan>();
  const auto* inner_scan = main_join->left_scan()->GetAs<ResolvedWithScan>();

  // Inner CTE definition and reference are $inlined_cte_1_cte
  EXPECT_EQ(inner_scan->with_entry_list(0)->with_query_name(),
            "$inlined_cte_1_cte");
  EXPECT_EQ(
      inner_scan->query()->GetAs<ResolvedWithRefScan>()->with_query_name(),
      "$inlined_cte_1_cte");

  // Outer CTE reference correctly resolves to $inlined_cte_0_cte (NOT inner!)
  EXPECT_EQ(
      main_join->right_scan()->GetAs<ResolvedWithRefScan>()->with_query_name(),
      "$inlined_cte_0_cte");
}

}  // namespace
}  // namespace googlesql
