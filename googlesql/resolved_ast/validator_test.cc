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

#include "googlesql/resolved_ast/validator.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_column_visibility_checker.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
namespace testing {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
using ::absl_testing::StatusIs;

// Similar to MakeSelect1Stmt(), except the output column list in the returned
// tree has a different column id from that produced by the ProjectScan.
std::unique_ptr<ResolvedQueryStmt> MakeSelect1StmtWithWrongColumnId(
    IdStringPool& pool) {
  std::vector<ResolvedColumn> empty_column_list;
  std::unique_ptr<ResolvedSingleRowScan> input_scan =
      MakeResolvedSingleRowScan(empty_column_list);

  ResolvedColumn column_x =
      ResolvedColumn(1, pool.Make("tbl"), pool.Make("x"), types::Int64Type());

  ResolvedColumn column_x_wrong_id =
      ResolvedColumn(2, pool.Make("tbl"), pool.Make("x"), types::Int64Type());

  std::vector<ResolvedColumn> column_list;
  column_list.push_back(column_x);

  auto project_scan = MakeResolvedProjectScan(
      column_list,
      MakeNodeVector(MakeResolvedComputedColumn(
          column_list.back(), MakeResolvedLiteral(Value::Int64(1)))),
      std::move(input_scan));

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;
  output_column_list.push_back(
      MakeResolvedOutputColumn("x", column_x_wrong_id));

  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeResolvedQueryStmt(std::move(output_column_list),
                            /*is_value_table=*/false, std::move(project_scan));
  EXPECT_EQ(R"(
QueryStmt
+-output_column_list=
| +-tbl.x#2 AS x [INT64]
+-query=
  +-ProjectScan
    +-column_list=[tbl.x#1]
    +-expr_list=
    | +-x#1 := Literal(type=INT64, value=1)
    +-input_scan=
      +-SingleRowScan
)",
            absl::StrCat("\n", query_stmt->DebugString()));
  return query_stmt;
}

// Returns a hand-constructed resolved tree representing "SELECT 1 AS x, 2 AS
// y". If <unique_column_ids> is true, uses separate column ids to represent x
// and y, which will result in a valid tree.
//
// If <unique_column_ids> is false, the same column ids will be used for x and
// y, which will result in an invalid tree.
std::unique_ptr<ResolvedQueryStmt> MakeSelectStmtWithMultipleColumns(
    IdStringPool& pool, bool unique_column_ids) {
  std::vector<ResolvedColumn> empty_column_list;
  std::unique_ptr<ResolvedSingleRowScan> input_scan =
      MakeResolvedSingleRowScan(empty_column_list);

  ResolvedColumn column_x =
      ResolvedColumn(1, pool.Make("tbl"), pool.Make("x"), types::Int64Type());
  ResolvedColumn column_y =
      ResolvedColumn(unique_column_ids ? 2 : 1, pool.Make("tbl"),
                     pool.Make("y"), types::Int64Type());

  std::unique_ptr<ResolvedProjectScan> project_scan = MakeResolvedProjectScan(
      {column_x, column_y},
      MakeNodeVector(MakeResolvedComputedColumn(
                         column_x, MakeResolvedLiteral(Value::Int64(1))),
                     MakeResolvedComputedColumn(
                         column_y, MakeResolvedLiteral(Value::Int64(2)))),
      std::move(input_scan));

  std::vector<std::unique_ptr<ResolvedOutputColumn>> output_column_list =
      MakeNodeVector(MakeResolvedOutputColumn("x", column_x),
                     MakeResolvedOutputColumn("y", column_y));

  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeResolvedQueryStmt(std::move(output_column_list),
                            /*is_value_table=*/false, std::move(project_scan));

  if (unique_column_ids) {
    EXPECT_EQ(R"(
QueryStmt
+-output_column_list=
| +-tbl.x#1 AS x [INT64]
| +-tbl.y#2 AS y [INT64]
+-query=
  +-ProjectScan
    +-column_list=tbl.[x#1, y#2]
    +-expr_list=
    | +-x#1 := Literal(type=INT64, value=1)
    | +-y#2 := Literal(type=INT64, value=2)
    +-input_scan=
      +-SingleRowScan
)",
              absl::StrCat("\n", query_stmt->DebugString()));
  } else {
    EXPECT_EQ(R"(
QueryStmt
+-output_column_list=
| +-tbl.x#1 AS x [INT64]
| +-tbl.y#1 AS y [INT64]
+-query=
  +-ProjectScan
    +-column_list=tbl.[x#1, y#1]
    +-expr_list=
    | +-x#1 := Literal(type=INT64, value=1)
    | +-y#1 := Literal(type=INT64, value=2)
    +-input_scan=
      +-SingleRowScan
)",
              absl::StrCat("\n", query_stmt->DebugString()));
  }
  return query_stmt;
}

enum class WithQueryShape {
  kValid,
  kNotScanAllColumns,
  kFailToRenameColumn,
  kColumnTypeMismatch,
};
std::unique_ptr<const ResolvedStatement> MakeWithQuery(IdStringPool& pool,
                                                       WithQueryShape shape) {
  std::string with_query_name = "with_query_name";
  ResolvedColumn column_x =
      ResolvedColumn(1, pool.Make("tbl"), pool.Make("x"), types::Int64Type());
  ResolvedColumn column_y =
      ResolvedColumn(2, pool.Make("tbl"), pool.Make("y"), types::StringType());
  ResolvedColumn column_x2 =
      ResolvedColumn(3, pool.Make("tbl"), pool.Make("x2"), types::Int64Type());
  ResolvedColumn column_y2 =
      shape == WithQueryShape::kColumnTypeMismatch
          ? ResolvedColumn(4, pool.Make("tbl"), pool.Make("y2"),
                           types::DoubleType())
          : ResolvedColumn(4, pool.Make("tbl"), pool.Make("y2"),
                           types::StringType());

  ResolvedWithRefScanBuilder with_scan =
      ResolvedWithRefScanBuilder()
          .add_column_list(column_x2)
          .set_with_query_name(with_query_name);
  switch (shape) {
    case WithQueryShape::kValid:
    case WithQueryShape::kColumnTypeMismatch:
      with_scan.add_column_list(column_y2);
      break;
    case WithQueryShape::kFailToRenameColumn:
      with_scan.add_column_list(column_y);
      break;
    case WithQueryShape::kNotScanAllColumns:
      break;
  }

  return ResolvedQueryStmtBuilder()
      .add_output_column_list(
          ResolvedOutputColumnBuilder().set_column(column_x2).set_name(
              column_x2.name()))
      .set_query(
          ResolvedWithScanBuilder()
              .set_recursive(false)
              .set_query(std::move(with_scan))
              .add_column_list(column_x2)
              .add_with_entry_list(
                  ResolvedWithEntryBuilder()
                      .set_with_query_name(with_query_name)
                      .set_with_subquery(
                          ResolvedProjectScanBuilder()
                              .add_column_list(column_x)
                              .add_expr_list(
                                  ResolvedComputedColumnBuilder()
                                      .set_column(column_x)
                                      .set_expr(
                                          ResolvedLiteralBuilder()
                                              .set_value(Value::Int64(1))
                                              .set_type(types::Int64Type())))
                              .add_column_list(column_y)
                              .add_expr_list(
                                  ResolvedComputedColumnBuilder()
                                      .set_column(column_y)
                                      .set_expr(
                                          ResolvedLiteralBuilder()
                                              .set_value(Value::String("a"))
                                              .set_type(types::StringType())))
                              .set_input_scan(MakeResolvedSingleRowScan()))))
      .Build()
      .value();
}

absl::StatusOr<std::unique_ptr<const ResolvedQueryStmt>>
MakeAggregationThresholdQuery(
    std::vector<std::unique_ptr<ResolvedOption>> options,
    IdStringPool& string_pool) {
  static absl::NoDestructor<Function> dp_function("count", "test_group",
                                                  Function::AGGREGATE);
  FunctionSignature sig(/*result_type=*/FunctionArgumentType(
                            types::Int64Type(), /*num_occurrences=*/1),
                        /*arguments=*/{},
                        /*context_id=*/static_cast<int64_t>(1234));

  GOOGLESQL_ASSIGN_OR_RETURN(auto dp_count_call, ResolvedAggregateFunctionCallBuilder()
                                           .set_type(types::Int64Type())
                                           .set_function(dp_function.get())
                                           .set_signature(std::move(sig))
                                           .Build());

  ResolvedColumn column_dp_count = ResolvedColumn(
      1, string_pool.Make("agg"), string_pool.Make("c"), types::Int64Type());
  auto aggregation_threshold_builder =
      ResolvedAggregationThresholdAggregateScanBuilder()
          .add_column_list(column_dp_count)
          .set_input_scan(MakeResolvedSingleRowScan())
          .add_aggregate_list(ResolvedComputedColumnBuilder()
                                  .set_column(column_dp_count)
                                  .set_expr(std::move(dp_count_call)));
  for (auto&& option : options) {
    aggregation_threshold_builder.add_option_list(std::move(option));
  }
  return ResolvedQueryStmtBuilder()
      .add_output_column_list(MakeResolvedOutputColumn("c", column_dp_count))
      .set_query(ResolvedProjectScanBuilder()
                     .add_column_list(column_dp_count)
                     .set_input_scan(std::move(aggregation_threshold_builder)))
      .Build();
}

enum class GroupingSetTestMode {
  kValid,
  // group by key list doesn't contain all referenced keys in the grouping
  // set list.
  kMissGroupByKey,
  // grouping set list doesn't contains references of all keys in the group
  // by list.
  KMissGroupByKeyRef,
};

absl::StatusOr<std::unique_ptr<const ResolvedQueryStmt>>
MakeGroupingSetsResolvedAST(IdStringPool& pool, GroupingSetTestMode mode) {
  // Prepare 3 group by columns, col3 will be used as the missing test key or
  // reference.
  ResolvedColumn col1 = ResolvedColumn(1, pool.Make("$groupby"),
                                       pool.Make("col1"), types::Int64Type());
  ResolvedColumn col2 = ResolvedColumn(2, pool.Make("$groupby"),
                                       pool.Make("col2"), types::Int64Type());
  ResolvedColumn col3 = ResolvedColumn(3, pool.Make("$groupby"),
                                       pool.Make("col3"), types::Int64Type());
  std::vector<ResolvedColumn> columns = {col1, col2, col3};

  // Prepare the input scan.
  ResolvedAggregateScanBuilder builder =
      ResolvedAggregateScanBuilder().set_input_scan(
          ResolvedSingleRowScanBuilder());
  // Prepare the group by list
  builder
      .add_group_by_list(
          ResolvedComputedColumnBuilder().set_column(col1).set_expr(
              ResolvedLiteralBuilder()
                  .set_value(Value::Int64(1))
                  .set_type(types::Int64Type())))
      .add_group_by_list(
          ResolvedComputedColumnBuilder().set_column(col2).set_expr(
              ResolvedLiteralBuilder()
                  .set_value(Value::Int64(2))
                  .set_type(types::Int64Type())));

  // Otherwise col3 is missing in the group by key list.
  if (mode != GroupingSetTestMode::kMissGroupByKey) {
    builder.add_group_by_list(
        ResolvedComputedColumnBuilder().set_column(col3).set_expr(
            ResolvedLiteralBuilder()
                .set_value(Value::Int64(3))
                .set_type(types::Int64Type())));
  }

  // Prepare grouping set list
  // Simulate the group by clause GROUPING SETS((col1, col3), CUBE(col1,
  // col2), ROLLUP((col1, col2)))
  auto resolved_grouping_set =
      ResolvedGroupingSetBuilder().add_group_by_column_list(
          ResolvedColumnRefBuilder()
              .set_type(col1.type())
              .set_column(col1)
              .set_is_correlated(false));
  // Otherwise the key reference of col3 is missing.
  if (mode != GroupingSetTestMode::KMissGroupByKeyRef) {
    resolved_grouping_set.add_group_by_column_list(
        ResolvedColumnRefBuilder()
            .set_type(col3.type())
            .set_column(col3)
            .set_is_correlated(false));
  }
  builder.add_grouping_set_list(resolved_grouping_set);
  builder.add_grouping_set_list(
      ResolvedCubeBuilder()
          .add_cube_column_list(
              ResolvedGroupingSetMultiColumnBuilder().add_column_list(
                  ResolvedColumnRefBuilder()
                      .set_type(col1.type())
                      .set_column(col1)
                      .set_is_correlated(false)))
          .add_cube_column_list(
              ResolvedGroupingSetMultiColumnBuilder().add_column_list(
                  ResolvedColumnRefBuilder()
                      .set_type(col2.type())
                      .set_column(col2)
                      .set_is_correlated(false))));
  builder.add_grouping_set_list(ResolvedRollupBuilder().add_rollup_column_list(
      ResolvedGroupingSetMultiColumnBuilder()
          .add_column_list(ResolvedColumnRefBuilder()
                               .set_type(col1.type())
                               .set_column(col1)
                               .set_is_correlated(false))
          .add_column_list(ResolvedColumnRefBuilder()
                               .set_type(col2.type())
                               .set_column(col2)
                               .set_is_correlated(false))));
  builder.set_column_list(columns);

  return ResolvedQueryStmtBuilder()
      .add_output_column_list(
          ResolvedOutputColumnBuilder().set_column(col1).set_name("col1"))
      .add_output_column_list(
          ResolvedOutputColumnBuilder().set_column(col2).set_name("col2"))
      .add_output_column_list(
          ResolvedOutputColumnBuilder().set_column(col3).set_name("col3"))
      .set_query(
          ResolvedProjectScanBuilder().set_column_list(columns).set_input_scan(
              builder))
      .Build();
}

// Returns a hand-constructed tree for
//   SELECT (SELECT <x>) AS y FROM (SELECT 1 AS x)
// for testing column visibility. The subquery references `x` with
// `is_correlated`=`correlated`, and lists `x` in its parameter_list if
// `in_parameter_list`. If `subquery_returns_x`, the subquery's ProjectScan
// returns `x` directly in its column_list instead of computing a new column.
absl::StatusOr<std::unique_ptr<const ResolvedQueryStmt>>
MakeSubqueryStmtForColumnVisibility(IdStringPool& pool, bool correlated,
                                    bool in_parameter_list,
                                    bool subquery_returns_x = false) {
  ResolvedColumn x(1, pool.Make("t"), pool.Make("x"), types::Int64Type());
  ResolvedColumn y(2, pool.Make("q"), pool.Make("y"), types::Int64Type());
  ResolvedColumn z(3, pool.Make("s"), pool.Make("z"), types::Int64Type());

  ResolvedProjectScanBuilder subquery_scan =
      ResolvedProjectScanBuilder().set_input_scan(MakeResolvedSingleRowScan());
  if (subquery_returns_x) {
    subquery_scan.add_column_list(x);
  } else {
    subquery_scan.add_column_list(z).add_expr_list(MakeResolvedComputedColumn(
        z, MakeResolvedColumnRef(types::Int64Type(), x, correlated)));
  }
  ResolvedSubqueryExprBuilder subquery =
      ResolvedSubqueryExprBuilder()
          .set_type(types::Int64Type())
          .set_subquery_type(ResolvedSubqueryExpr::SCALAR)
          .set_subquery(std::move(subquery_scan));
  if (in_parameter_list) {
    subquery.add_parameter_list(
        MakeResolvedColumnRef(types::Int64Type(), x, /*is_correlated=*/false));
  }

  return ResolvedQueryStmtBuilder()
      .add_output_column_list(MakeResolvedOutputColumn("y", y))
      .set_query(
          ResolvedProjectScanBuilder()
              .add_column_list(y)
              .add_expr_list(
                  ResolvedComputedColumnBuilder().set_column(y).set_expr(
                      std::move(subquery)))
              .set_input_scan(ResolvedProjectScanBuilder()
                                  .add_column_list(x)
                                  .add_expr_list(MakeResolvedComputedColumn(
                                      x, MakeResolvedLiteral(Value::Int64(1))))
                                  .set_input_scan(MakeResolvedSingleRowScan())))
      .Build();
}

ValidatorOptions ColumnVisibilityValidatorOptions() {
  ValidatorOptions options;
  options.validate_column_visibility = true;
  options.compare_column_visibility = true;
  return options;
}

TEST(ValidatorTest, ColumnVisibilityValidCorrelatedSubquery) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/true,
                                          /*in_parameter_list=*/true));
  Validator validator(LanguageOptions(), ColumnVisibilityValidatorOptions());
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(stmt.get()));
}

// Without comparison mode, the checker doesn't record environments, so the
// column sets it builds are owned by the generated Check methods.
TEST(ValidatorTest, ColumnVisibilityWithoutComparison) {
  IdStringPool pool;
  ValidatorOptions options;
  options.validate_column_visibility = true;
  options.compare_column_visibility = false;
  Validator validator(LanguageOptions(), options);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> valid_stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/true,
                                          /*in_parameter_list=*/true));
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(valid_stmt.get()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> invalid_stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/true,
                                          /*in_parameter_list=*/false));
  EXPECT_THAT(validator.ValidateResolvedStatement(invalid_stmt.get()),
              StatusIs(_, HasSubstr("not in the OUTER columns")));
}

TEST(ValidatorTest, ColumnVisibilityCorrelatedReferenceNotInParameterList) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/true,
                                          /*in_parameter_list=*/false));
  Validator validator(LanguageOptions(), ColumnVisibilityValidatorOptions());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(stmt.get()),
      StatusIs(_, AllOf(HasSubstr("Column visibility check failed: ColumnRef."
                                  "column references column t.x#1"),
                        HasSubstr("not in the OUTER columns"),
                        HasSubstr("See ColumnVisibilityChecker::CheckRoot"))));
}

TEST(ValidatorTest, ColumnVisibilityUncorrelatedReferenceToOuterColumn) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/false,
                                          /*in_parameter_list=*/true));
  Validator validator(LanguageOptions(), ColumnVisibilityValidatorOptions());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(stmt.get()),
      StatusIs(_, AllOf(HasSubstr("Column visibility check failed: ColumnRef."
                                  "column references column t.x#1"),
                        HasSubstr("not in the VISIBLE columns []"))));
}

TEST(ValidatorTest, ColumnVisibilityColumnListWithOuterColumn) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/true,
                                          /*in_parameter_list=*/true,
                                          /*subquery_returns_x=*/true));
  Validator validator(LanguageOptions(), ColumnVisibilityValidatorOptions());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(stmt.get()),
      StatusIs(_, AllOf(HasSubstr("Column visibility check failed: "
                                  "ProjectScan.column_list references "
                                  "column t.x#1 which is not in its "
                                  "column_list_scope"),
                        HasSubstr("See "
                                  "ColumnVisibilityChecker::CheckRoot"))));
}

TEST(ValidatorTest, ColumnVisibilityOffByDefault) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> stmt,
      MakeSubqueryStmtForColumnVisibility(pool, /*correlated=*/true,
                                          /*in_parameter_list=*/false));
  // The hand-written validator also catches this, with a different error.
  Validator validator(LanguageOptions{}, ValidatorOptions{});
  EXPECT_THAT(validator.ValidateResolvedStatement(stmt.get()),
              StatusIs(_, Not(HasSubstr("Column visibility"))));
}

TEST(ValidatorTest, ValidQueryStatement) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt = MakeSelect1Stmt(pool);
  Validator validator;
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query_stmt.get()));

  // Make sure the statement can be validated multiple times on the same
  // Validator object.
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, ValidExpression) {
  std::unique_ptr<ResolvedExpr> expr = MakeResolvedLiteral(Value::Int64(1));
  Validator validator;
  GOOGLESQL_ASSERT_OK(validator.ValidateStandaloneResolvedExpr(expr.get()));

  // Make sure the expression can be validated multiple times on the same
  // Validator object.
  GOOGLESQL_ASSERT_OK(validator.ValidateStandaloneResolvedExpr(expr.get()));
}

TEST(ValidatorTest, ValidWithScan) {
  IdStringPool pool;
  std::unique_ptr<const ResolvedStatement> valid_stmt =
      MakeWithQuery(pool, WithQueryShape::kValid);
  Validator validator;
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(valid_stmt.get()));

  // Make sure statement can be validated multiple times.
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(valid_stmt.get()));
}

TEST(ValidatorTest, InvalidWithScans) {
  IdStringPool pool;
  Validator validator;

  std::unique_ptr<const ResolvedStatement> missing_column =
      MakeWithQuery(pool, WithQueryShape::kNotScanAllColumns);
  EXPECT_THAT(validator.ValidateResolvedStatement(missing_column.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("ResolvedWithRefScan must scan exactly the "
                                 "columns projected from the with query")));

  std::unique_ptr<const ResolvedStatement> not_renamed_column =
      MakeWithQuery(pool, WithQueryShape::kFailToRenameColumn);
  EXPECT_THAT(validator.ValidateResolvedStatement(not_renamed_column.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Duplicate column id 2 in column tbl.y#2")));

  std::unique_ptr<const ResolvedStatement> type_mismatch =
      MakeWithQuery(pool, WithQueryShape::kColumnTypeMismatch);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(type_mismatch.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "Type mismatch between ResolvedWithRefScan and with query")));
}

TEST(ValidatorTest, InvalidExpression) {
  TypeFactory type_factory;
  ResolvedColumn column(1, googlesql::IdString::MakeGlobal("tbl"),
                        googlesql::IdString::MakeGlobal("col1"),
                        types::Int64Type());
  std::unique_ptr<ResolvedExpr> expr = WrapInFunctionCall(
      &type_factory, MakeResolvedLiteral(Value::Int64(1)),
      MakeResolvedColumnRef(types::Int64Type(), column, false),
      MakeResolvedLiteral(Value::Int64(2)));
  Validator validator;

  // Repeat twice to ensure that the validator behaves the same way when reused.
  for (int i = 0; i < 2; ++i) {
    absl::Status status = validator.ValidateStandaloneResolvedExpr(expr.get());

    // Make sure error message is as expected.
    ASSERT_THAT(
        status,
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Incorrect reference to column tbl.col1#1")));

    // Make sure the tree dump has emphasis on the expected node.
    ASSERT_THAT(status.message(),
                HasSubstr("ColumnRef(type=INT64, column=tbl.col1#1) "
                          "(validation failed here)"));
  }
}

TEST(ValidatorTest, InvalidQueryStatement) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeSelect1StmtWithWrongColumnId(pool);
  Validator validator;

  // Repeat twice to ensure that the validator behaves the same way when reused.
  for (int i = 0; i < 2; ++i) {
    // Verify error message
    absl::Status status = validator.ValidateResolvedStatement(query_stmt.get());
    ASSERT_THAT(status,
                StatusIs(absl::StatusCode::kInternal,
                         HasSubstr("Incorrect reference to column tbl.x#2")));

    // Verify node emphasized in tree dump
    ASSERT_THAT(
        status,
        StatusIs(
            _, HasSubstr("| +-tbl.x#2 AS x [INT64] (validation failed here)")));
  }
}

TEST(ValidatorTest, ValidStatementAfterInvalidStatement) {
  // Make sure that after validating an invalid statement, the validator is left
  // in a state where it can still validate another valid statement later.
  Validator validator;
  IdStringPool pool;

  std::unique_ptr<ResolvedQueryStmt> valid_query_stmt = MakeSelect1Stmt(pool);
  std::unique_ptr<ResolvedQueryStmt> invalid_query_stmt =
      MakeSelect1StmtWithWrongColumnId(pool);

  ASSERT_THAT(validator.ValidateResolvedStatement(invalid_query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Incorrect reference to column tbl.x#2")));
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(valid_query_stmt.get()));
}

TEST(ValidatorTest, ValidQueryStatementMultipleColumns) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeSelectStmtWithMultipleColumns(pool, /*unique_column_ids=*/true);
  Validator validator;
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, InvalidStatementDueToDuplicateColumnIds) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeSelectStmtWithMultipleColumns(pool, /*unique_column_ids=*/false);
  Validator validator;
  ASSERT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(ValidateTest, QueryStmtWithNullExpr) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt = MakeSelect1Stmt(pool);
  const_cast<ResolvedComputedColumn*>(
      query_stmt->query()->GetAs<ResolvedProjectScan>()->expr_list(0))
      ->release_expr();

  Validator validator;
  ASSERT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("| +-x#1 := <nullptr AST node> (validation failed here)")));
}

TEST(ValidateTest, CreateFunctionStmtWithRemoteAndInvalidLanguage) {
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int32Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int32Type()}, {}, nullptr},
          /*is_aggregate=*/false,
          /*language=*/"SQL",
          /*code=*/"",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/true, /*connection_list=*/nullptr);

  Validator validator;
  ASSERT_THAT(
      validator.ValidateResolvedStatement(create_function_stmt.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("is_remote is true iff language is \"REMOTE\"")));
}

TEST(ValidateTest, CreateFunctionStmtWithRemoteAndRemoteLanguage) {
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int32Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int32Type()}, {}, nullptr},
          /*is_aggregate=*/false,
          /*language=*/"remote",
          /*code=*/"",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/true, /*connection_list=*/nullptr);

  Validator validator;
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_function_stmt.get()));
}

TEST(ValidateTest, CreateFunctionStmtWithRemoteAndTemplatedArg) {
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::BytesType(),
          /*argument_name_list=*/{"x"},
          /*signature=*/
          FunctionSignature({types::BytesType()},
                            {FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY)},
                            nullptr),
          /*is_aggregate=*/false,
          /*language=*/"remote",
          /*code=*/"",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/true, /*connection_list=*/nullptr);

  Validator validator;
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_function_stmt.get()));
}

TEST(ValidateTest,
     CreateFunctionStmtWithRemoteAndCodeWithRemoteFunctionFeatureEnabled) {
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int32Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int32Type()}, {}, nullptr},
          /*is_aggregate=*/false,
          /*language=*/"REMOTE",
          /*code=*/"return 1;",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/true, /*connection_list=*/nullptr);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_REMOTE_FUNCTION);
  Validator validator(language_options);
  ASSERT_THAT(
      validator.ValidateResolvedStatement(create_function_stmt.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("stmt->code().empty()")));
}

TEST(ValidateTest,
     CreateFunctionStmtWithRemoteAndCodeWithRemoteFunctionFeatureNotEnabled) {
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int32Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int32Type()}, {}, nullptr},
          /*is_aggregate=*/false,
          /*language=*/"REMOTE",
          /*code=*/"return 1;",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/true, /*connection_list=*/nullptr);

  Validator validator;
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_function_stmt.get()));
}

TEST(ValidateTest, CreateFunctionStmtWithConnectionButNotRemote) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int32Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int32Type()}, {}, nullptr},
          /*is_aggregate=*/false,
          /*language=*/"SQL",
          /*code=*/"",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/false,
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  Validator validator;
  ASSERT_THAT(
      validator.ValidateResolvedStatement(create_function_stmt.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("stmt->is_remote()")));
}

TEST(ValidateTest, CreateFunctionStmtWithRemoteLanguageButNotRemote) {
  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int32Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int32Type()}, {}, nullptr},
          /*is_aggregate=*/false,
          /*language=*/"REMOTE",
          /*code=*/"",
          /*aggregate_expression_list=*/{},
          /*function_expression=*/nullptr,
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/false, /*connection_list=*/nullptr);

  Validator validator;
  ASSERT_THAT(
      validator.ValidateResolvedStatement(create_function_stmt.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("is_remote is true iff language is \"REMOTE\"")));
}

TEST(ValidateTest, CreateFunctionStmtWithInvalidResolvedArgumentRef) {
  IdStringPool pool;
  auto agg_function =
      std::make_unique<Function>("count", "test_group", Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  ResolvedColumn placeholder_column = ResolvedColumn(
      1, pool.Make("table_name"), pool.Make("name"), types::Int64Type());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedComputedColumn> placeholder_computed_column,
      ResolvedComputedColumnBuilder()
          .set_column(placeholder_column)
          .set_expr(MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
          .Build());
  std::vector<std::unique_ptr<const ResolvedExpr>> argument_list;
  argument_list.push_back(MakeResolvedArgumentRef(
      types::Int64Type(), "arg_name", ResolvedArgumentDef::AGGREGATE));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_function_call,
      ResolvedAggregateFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(agg_function.get())
          .set_signature(sig)
          .set_argument_list(std::move(argument_list))
          .add_group_by_list(std::move(placeholder_computed_column))
          .Build());

  ResolvedColumn agg_column =
      ResolvedColumn(2, pool.Make("table_name"), pool.Make("agg_col_name"),
                     types::Int64Type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      aggregate_expression_list;
  aggregate_expression_list.push_back(
      MakeResolvedComputedColumn(agg_column, std::move(agg_function_call)));

  std::unique_ptr<ResolvedCreateFunctionStmt> create_function_stmt =
      MakeResolvedCreateFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*has_explicit_return_type=*/true, types::Int64Type(),
          /*argument_name_list=*/{},
          /*signature=*/{{types::Int64Type()}, {}, nullptr},
          /*is_aggregate=*/true,
          /*language=*/"SQL",
          /*code=*/"",
          /*aggregate_expression_list=*/std::move(aggregate_expression_list),
          /*function_expression=*/
          MakeResolvedColumnRef(agg_column.type(), agg_column, false),
          /*option_list=*/{},
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*determinism_level=*/
          ResolvedCreateStatement::DETERMINISM_UNSPECIFIED,
          /*is_remote=*/false, /*connection_list=*/nullptr);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(create_function_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Incorrect reference to argument ArgumentRef(type=INT64, "
                    "name=\"arg_name\", argument_kind=AGGREGATE)")));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureNotEnabled) {
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"", /*connection_list=*/nullptr,
          /*language=*/"PYTHON",
          /*code=*/"",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  Validator validator;
  ASSERT_THAT(
      validator.ValidateResolvedStatement(create_procedure_stmt.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("stmt->language()")));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLConnectionFeatureNotEnabled) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)),
          /*language=*/"PYTHON",
          /*code=*/"",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  Validator validator;
  ASSERT_THAT(validator.ValidateResolvedStatement(create_procedure_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("stmt->connection_list()")));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureEnabledMissingLanguage) {
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"sql", /*connection_list=*/nullptr,
          /*language=*/"",
          /*code=*/"code",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);
  Validator validator(language_options);
  ASSERT_THAT(validator.ValidateResolvedStatement(create_procedure_stmt.get()),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("stmt->code()")));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureEnabledHasBodyAndLanguage) {
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"body", /*connection_list=*/nullptr,
          /*language=*/"python",
          /*code=*/"",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);
  Validator validator(language_options);
  ASSERT_THAT(validator.ValidateResolvedStatement(create_procedure_stmt.get()),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureEnabledHasLanguage) {
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"", /*connection_list=*/nullptr,
          /*language=*/"PYTHON",
          /*code=*/"",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_procedure_stmt.get()));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureEnabledHasLanguageAndCode) {
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"", /*connection_list=*/nullptr,
          /*language=*/"PYTHON",
          /*code=*/"code",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_procedure_stmt.get()));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureEnabled) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)),
          /*language=*/"PYTHON",
          /*code=*/"code",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_procedure_stmt.get()));
}

TEST(ValidateTest, CreateProcedureStmtExternalSecurityFeatureNotEnabled) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)),
          /*language=*/"PYTHON",
          /*code=*/"code",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_INVOKER);

  Validator validator;
  ASSERT_THAT(validator.ValidateResolvedStatement(create_procedure_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("stmt->external_security()")));
}

TEST(ValidateTest, CreateProcedureStmtExternalSecurityFeatureEnabled) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_KIND_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)),
          /*language=*/"PYTHON",
          /*code=*/"code",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_INVOKER);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);
  language_options.EnableLanguageFeature(FEATURE_EXTERNAL_SECURITY_PROCEDURE);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(create_procedure_stmt.get()));
}

TEST(ValidateTest, AnonymizedAggregateScan) {
  IdStringPool pool;
  auto anon_function = std::make_unique<Function>("anon_count", "test_group",
                                                  Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto anon_count_call,
                       ResolvedAggregateFunctionCallBuilder()
                           .set_type(types::Int64Type())
                           .set_function(anon_function.get())
                           .set_signature(std::move(sig))
                           .Build());

  ResolvedColumn column_anon_count =
      ResolvedColumn(1, pool.Make("agg"), pool.Make("c"), types::Int64Type());
  auto custom_option = MakeResolvedOption(
      "", "custom_option",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("c", column_anon_count))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(column_anon_count)
                         .set_input_scan(
                             ResolvedAnonymizedAggregateScanBuilder()
                                 .add_column_list(column_anon_count)
                                 .set_input_scan(MakeResolvedSingleRowScan())
                                 .add_aggregate_list(
                                     ResolvedComputedColumnBuilder()
                                         .set_column(column_anon_count)
                                         .set_expr(std::move(anon_count_call)))
                                 .set_k_threshold_expr(
                                     ResolvedColumnRefBuilder()
                                         .set_type(types::Int64Type())
                                         .set_column(column_anon_count)
                                         .set_is_correlated(false))
                                 .add_anonymization_option_list(
                                     std::move(custom_option))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ANONYMIZATION);
  AllowedHintsAndOptions allowed_hints_and_options;
  allowed_hints_and_options.AddAnonymizationOption("custom_option",
                                                   types::Int64Type());
  ValidatorOptions validator_options{.allowed_hints_and_options =
                                         allowed_hints_and_options};
  Validator validator(language_options, validator_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, ValidCreateModelStatement_Local) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("tbl"), pool.Make("x"), types::Int64Type());
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/
      MakeNodeVector(MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)),
      /*query=*/
      MakeResolvedProjectScan(
          /*column_list=*/{x},
          /*expr_list=*/
          MakeNodeVector(MakeResolvedComputedColumn(
              x, MakeResolvedLiteral(Value::Int64(1)))),
          /*input_scan=*/MakeResolvedSingleRowScan()),
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  Validator validator;
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(statement.get()));
}

TEST(ValidatorTest, ValidCreateModelStatement_AliasedQueryList) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("training_data"), pool.Make("x"),
                   types::Int64Type());
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/nullptr,
      /*aliased_query_list=*/
      MakeNodeVector(MakeResolvedCreateModelAliasedQuery(
          /*alias=*/"training_data",
          /*query=*/
          MakeResolvedProjectScan(
              /*column_list=*/{x},
              /*expr_list=*/
              MakeNodeVector(MakeResolvedComputedColumn(
                  x, MakeResolvedLiteral(Value::Int64(1)))),
              /*input_scan=*/MakeResolvedSingleRowScan()),
          /*output_column_list=*/
          MakeNodeVector(
              MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)))),
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_MODEL_WITH_ALIASED_QUERY_LIST);
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(statement.get()));
}

TEST(ValidatorTest, CreateModelStatement_DuplicateAliasedQueryList) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("training_data"), pool.Make("x"),
                   types::Int64Type());
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/nullptr,
      /*aliased_query_list=*/
      MakeNodeVector(MakeResolvedCreateModelAliasedQuery(
                         /*alias=*/"training_data",
                         /*query=*/
                         MakeResolvedProjectScan(
                             /*column_list=*/{x},
                             /*expr_list=*/
                             MakeNodeVector(MakeResolvedComputedColumn(
                                 x, MakeResolvedLiteral(Value::Int64(1)))),
                             /*input_scan=*/MakeResolvedSingleRowScan()),
                         /*output_column_list=*/
                         MakeNodeVector(MakeResolvedOutputColumn(
                             /*name=*/"x", /*column=*/x))),
                     MakeResolvedCreateModelAliasedQuery(
                         /*alias=*/"training_data",
                         /*query=*/
                         MakeResolvedProjectScan(
                             /*column_list=*/{x},
                             /*expr_list=*/
                             MakeNodeVector(MakeResolvedComputedColumn(
                                 x, MakeResolvedLiteral(Value::Int64(1)))),
                             /*input_scan=*/MakeResolvedSingleRowScan()),
                         /*output_column_list=*/
                         MakeNodeVector(MakeResolvedOutputColumn(
                             /*name=*/"x", /*column=*/x)))),
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_MODEL_WITH_ALIASED_QUERY_LIST);
  Validator validator(language_options);
  EXPECT_THAT(validator.ValidateResolvedStatement(statement.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Duplicate alias training_data")));
}

TEST(ValidatorTest, ValidCreateModelStatement_AliasedQueryListDisabled) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("training_data"), pool.Make("x"),
                   types::Int64Type());
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/
      MakeNodeVector(MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)),
      /*query=*/
      MakeResolvedProjectScan(
          /*column_list=*/{x},
          /*expr_list=*/
          MakeNodeVector(MakeResolvedComputedColumn(
              x, MakeResolvedLiteral(Value::Int64(1)))),
          /*input_scan=*/MakeResolvedSingleRowScan()),
      /*aliased_query_list=*/
      MakeNodeVector(MakeResolvedCreateModelAliasedQuery(
          /*alias=*/"training_data",
          /*query=*/
          MakeResolvedProjectScan(
              /*column_list=*/{x},
              /*expr_list=*/
              MakeNodeVector(MakeResolvedComputedColumn(
                  x, MakeResolvedLiteral(Value::Int64(1)))),
              /*input_scan=*/MakeResolvedSingleRowScan()),
          /*output_column_list=*/
          MakeNodeVector(
              MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)))),
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  Validator validator;
  EXPECT_THAT(validator.ValidateResolvedStatement(statement.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("stmt->aliased_query_list().empty()")));
}

TEST(ValidatorTest,
     InvalidCreateModelStatement_QueryAndAliasedQueryListCoexist) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("training_data"), pool.Make("x"),
                   types::Int64Type());
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/
      MakeNodeVector(MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)),
      /*query=*/
      MakeResolvedProjectScan(
          /*column_list=*/{x},
          /*expr_list=*/
          MakeNodeVector(MakeResolvedComputedColumn(
              x, MakeResolvedLiteral(Value::Int64(1)))),
          /*input_scan=*/MakeResolvedSingleRowScan()),
      /*aliased_query_list=*/
      MakeNodeVector(MakeResolvedCreateModelAliasedQuery(
          /*alias=*/"training_data",
          /*query=*/
          MakeResolvedProjectScan(
              /*column_list=*/{x},
              /*expr_list=*/
              MakeNodeVector(MakeResolvedComputedColumn(
                  x, MakeResolvedLiteral(Value::Int64(1)))),
              /*input_scan=*/MakeResolvedSingleRowScan()),
          /*output_column_list=*/
          MakeNodeVector(
              MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)))),
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_MODEL_WITH_ALIASED_QUERY_LIST);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(statement.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Query and aliased query list cannot coexist")));
}

TEST(ValidatorTest, InvalidCreateModelStatement_AliasedQueryListWithTransform) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("training_data"), pool.Make("x"),
                   types::Int64Type());
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/nullptr,
      /*aliased_query_list=*/
      MakeNodeVector(MakeResolvedCreateModelAliasedQuery(
          /*alias=*/"training_data",
          /*query=*/
          MakeResolvedProjectScan(
              /*column_list=*/{x},
              /*expr_list=*/
              MakeNodeVector(MakeResolvedComputedColumn(
                  x, MakeResolvedLiteral(Value::Int64(1)))),
              /*input_scan=*/MakeResolvedSingleRowScan()),
          /*output_column_list=*/
          MakeNodeVector(
              MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)))),
      /*transform_input_column_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"x",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/x,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_MODEL_WITH_ALIASED_QUERY_LIST);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(statement.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("stmt->transform_input_column_list().empty()")));
}

TEST(ValidatorTest, ValidCreateModelStatement_Imported) {
  IdStringPool pool;
  ResolvedColumn i1(1, pool.Make("tbl"), pool.Make("i1'"), types::Int64Type());
  ResolvedColumn i2(2, pool.Make("tbl"), pool.Make("i2"), types::DoubleType());
  ResolvedColumn o1(3, pool.Make("tbl"), pool.Make("o1"), types::BoolType());

  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/{},
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"i1",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{}),
                     MakeResolvedColumnDefinition(/*name=*/"i2",
                                                  /*type=*/types::DoubleType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i2,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*output_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"o1",
                                                  /*type=*/types::BoolType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/o1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*is_remote=*/false, /*connection_list=*/{});

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_REMOTE_MODEL);
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(statement.get()));
}

TEST(ValidatorTest, ValidCreateModelStatement_ImportedV13_Invalid) {
  IdStringPool pool;
  ResolvedColumn i1(1, pool.Make("tbl"), pool.Make("i1'"), types::Int64Type());
  ResolvedColumn i2(2, pool.Make("tbl"), pool.Make("i2"), types::DoubleType());
  ResolvedColumn o1(3, pool.Make("tbl"), pool.Make("o1"), types::BoolType());

  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/{},
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"i1",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{}),
                     MakeResolvedColumnDefinition(/*name=*/"i2",
                                                  /*type=*/types::DoubleType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i2,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*output_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"o1",
                                                  /*type=*/types::BoolType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/o1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*is_remote=*/false, /*connection_list=*/{});

  Validator validator;
  EXPECT_THAT(validator.ValidateResolvedStatement(statement.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("stmt->query() != nullptr")));
}

TEST(ValidatorTest, ValidCreateModelStatement_Remote) {
  IdStringPool pool;
  ResolvedColumn i1(1, pool.Make("tbl"), pool.Make("i1'"), types::Int64Type());
  ResolvedColumn i2(2, pool.Make("tbl"), pool.Make("i2"), types::DoubleType());
  ResolvedColumn o1(3, pool.Make("tbl"), pool.Make("o1"), types::BoolType());
  SimpleConnection connection("c");

  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/
      MakeNodeVector(MakeResolvedOption(
          "", "abc",
          MakeResolvedLiteral(types::StringType(), Value::String("def")))),
      /*output_column_list=*/{},
      /*query=*/{},
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"i1",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{}),
                     MakeResolvedColumnDefinition(/*name=*/"i2",
                                                  /*type=*/types::DoubleType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i2,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*output_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"o1",
                                                  /*type=*/types::BoolType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/o1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*is_remote=*/true,
      /*connection_list=*/
      MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_REMOTE_MODEL);
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(statement.get()));
}

TEST(ValidatorTest, ValidCreateModelStatement_RemoteV13_Invalid) {
  IdStringPool pool;
  ResolvedColumn i1(1, pool.Make("tbl"), pool.Make("i1'"), types::Int64Type());
  ResolvedColumn i2(2, pool.Make("tbl"), pool.Make("i2"), types::DoubleType());
  ResolvedColumn o1(3, pool.Make("tbl"), pool.Make("o1"), types::BoolType());
  SimpleConnection connection("c");

  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/
      MakeNodeVector(MakeResolvedOption(
          "", "abc",
          MakeResolvedLiteral(types::StringType(), Value::String("def")))),
      /*output_column_list=*/{},
      /*query=*/{},
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"i1",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{}),
                     MakeResolvedColumnDefinition(/*name=*/"i2",
                                                  /*type=*/types::DoubleType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i2,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*output_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"o1",
                                                  /*type=*/types::BoolType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/o1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*is_remote=*/true,
      /*connection_list=*/
      MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  Validator validator;
  EXPECT_THAT(validator.ValidateResolvedStatement(statement.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Remote model is not supported")));
}

TEST(ValidatorTest, ValidCreateModelStatement_SchemaAndQuery_Invalid) {
  IdStringPool pool;
  ResolvedColumn i1(1, pool.Make("tbl"), pool.Make("i1'"), types::Int64Type());
  ResolvedColumn i2(2, pool.Make("tbl"), pool.Make("i2"), types::DoubleType());
  ResolvedColumn o1(3, pool.Make("tbl"), pool.Make("o1"), types::BoolType());
  ResolvedColumn x(4, pool.Make("tbl"), pool.Make("x"), types::Int64Type());

  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/
      MakeNodeVector(MakeResolvedOutputColumn(/*name=*/"x", /*column=*/x)),
      /*query=*/
      MakeResolvedProjectScan(
          /*column_list=*/{x},
          /*expr_list=*/
          MakeNodeVector(MakeResolvedComputedColumn(
              x, MakeResolvedLiteral(Value::Int64(1)))),
          /*input_scan=*/MakeResolvedSingleRowScan()),
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"i1",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{}),
                     MakeResolvedColumnDefinition(/*name=*/"i2",
                                                  /*type=*/types::DoubleType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i2,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*output_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"o1",
                                                  /*type=*/types::BoolType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/o1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*is_remote=*/false, /*connection_list=*/{});

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_REMOTE_MODEL);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(statement.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("stmt->input_column_definition_list().empty()")));
}

TEST(ValidatorTest, ValidCreateModelStatement_ConnectionNoRemote_Invalid) {
  IdStringPool pool;
  ResolvedColumn i1(1, pool.Make("tbl"), pool.Make("i1'"), types::Int64Type());
  ResolvedColumn i2(2, pool.Make("tbl"), pool.Make("i2"), types::DoubleType());
  ResolvedColumn o1(3, pool.Make("tbl"), pool.Make("o1"), types::BoolType());
  SimpleConnection connection("c");

  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/{},
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"i1",
                                                  /*type=*/types::Int64Type(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{}),
                     MakeResolvedColumnDefinition(/*name=*/"i2",
                                                  /*type=*/types::DoubleType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/i2,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*output_column_definition_list=*/
      MakeNodeVector(MakeResolvedColumnDefinition(/*name=*/"o1",
                                                  /*type=*/types::BoolType(),
                                                  /*annotations=*/{},
                                                  /*is_hidden=*/false,
                                                  /*column=*/o1,
                                                  /*generated_column_info=*/{},
                                                  /*default_value=*/{})),
      /*is_remote=*/false,
      /*connection_list=*/
      MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_REMOTE_MODEL);
  Validator validator(language_options);
  EXPECT_THAT(validator.ValidateResolvedStatement(statement.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("stmt->connection_list()")));
}

TEST(ValidatorTest, ValidCreateModelStatement_EmptyV13_Invalid) {
  auto statement = MakeResolvedCreateModelStmt(
      /*name_path=*/{"m"},
      /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
      /*option_list=*/{},
      /*output_column_list=*/{},
      /*query=*/{},
      /*aliased_query_list=*/{},
      /*transform_input_column_list=*/{},
      /*transform_list=*/{},
      /*transform_output_column_list=*/{},
      /*transform_analytic_function_group_list=*/{},
      /*input_column_definition_list=*/{},
      /*output_column_definition_list=*/{},
      /*is_remote=*/false, /*connection_list=*/{});

  Validator validator;
  EXPECT_THAT(validator.ValidateResolvedStatement(statement.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("stmt->query() != nullptr")));
}

TEST(ValidatorTest, InvalidGraphLabelExpression) {
  const SimplePropertyGraph graph(std::vector<std::string>{"Graph"});
  TypeFactory factory;
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      /*property_types=*/{},
      /*element_tables=*/{}, &graph_element_type));

  IdStringPool pool;
  const std::vector<ResolvedColumn> col_list = {
      ResolvedColumn(1, pool.Make("tbl"), pool.Make("x"), graph_element_type)};

  // Create an invalid resolved AST where the label expression
  // has a NOT operator with an invalid number of operands (2 vs 1 expected).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphLabelNaryExpr> label_expr,
      ResolvedGraphLabelNaryExprBuilder()
          .set_op(ResolvedGraphLabelNaryExprEnums::OR)
          .add_operand_list(ResolvedGraphWildCardLabelBuilder())
          .add_operand_list(
              ResolvedGraphLabelNaryExprBuilder()
                  .set_op(ResolvedGraphLabelNaryExprEnums::NOT)
                  .add_operand_list(ResolvedGraphWildCardLabelBuilder())
                  .add_operand_list(ResolvedGraphWildCardLabelBuilder()))
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> query_stmt,
      ResolvedQueryStmtBuilder()
          .set_query(ResolvedProjectScanBuilder().set_input_scan(
              ResolvedGraphTableScanBuilder()
                  .set_property_graph(&graph)
                  .set_input_scan(
                      ResolvedGraphScanBuilder()
                          .set_column_list(col_list)
                          .set_filter_expr(nullptr)
                          .add_input_scan_list(
                              ResolvedGraphPathScanBuilder()
                                  .set_column_list(col_list)
                                  .set_filter_expr(nullptr)
                                  .set_head(col_list.front())
                                  .set_tail(col_list.back())
                                  .set_path_mode(nullptr)
                                  .add_input_scan_list(
                                      ResolvedGraphNodeScanBuilder()
                                          .set_filter_expr(nullptr)
                                          .set_label_expr(std::move(label_expr))
                                          .set_column_list(col_list))))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  Validator validator(language_options);
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("expr->operand_list_size()")));
}

TEST(ValidatorTest, InvalidDynamicGraphLabelExpression) {
  std::string graph_name = "graph_name";
  const SimplePropertyGraph graph(std::vector<std::string>{graph_name});
  TypeFactory factory;
  const GraphElementType* dynamic_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      /*static_property_types=*/{},
      /*element_tables=*/{}, &dynamic_graph_element_type));

  IdStringPool pool;
  const std::vector<ResolvedColumn> col_list = {ResolvedColumn(
      1, pool.Make("tbl"), pool.Make("x"), dynamic_graph_element_type)};

  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory.get_string());
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations{
      name_property_dcl.get()};
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", std::vector<std::string>{graph_name}, property_declarations);

  // Create an invalid resolved AST where the `label` and `label_name` fields
  // don't match.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphLabelNaryExpr> label_expr,
      ResolvedGraphLabelNaryExprBuilder()
          .set_op(ResolvedGraphLabelNaryExprEnums::OR)
          .add_operand_list(ResolvedGraphWildCardLabelBuilder())
          .add_operand_list(
              ResolvedGraphLabelBuilder()
                  .set_label(person_label.get())
                  .set_label_name(ResolvedLiteralBuilder()
                                      .set_value(Value::String("Account"))
                                      .set_type(factory.get_string())
                                      .Build())
                  .Build())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> query_stmt,
      ResolvedQueryStmtBuilder()
          .set_query(ResolvedProjectScanBuilder().set_input_scan(
              ResolvedGraphTableScanBuilder()
                  .set_property_graph(&graph)
                  .set_input_scan(
                      ResolvedGraphScanBuilder()
                          .set_column_list(col_list)
                          .set_filter_expr(nullptr)
                          .add_input_scan_list(
                              ResolvedGraphPathScanBuilder()
                                  .set_column_list(col_list)
                                  .set_filter_expr(nullptr)
                                  .set_head(col_list.front())
                                  .set_tail(col_list.back())
                                  .set_path_mode(nullptr)
                                  .add_input_scan_list(
                                      ResolvedGraphNodeScanBuilder()
                                          .set_filter_expr(nullptr)
                                          .set_label_expr(std::move(label_expr))
                                          .set_column_list(col_list))))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("label_name and label must match case-insensitively")));
}

TEST(ValidatorTest, CaseInsensitiveDynamicGraphLabelExpression) {
  std::string graph_name = "graph_name";
  const SimplePropertyGraph graph(std::vector<std::string>{graph_name});
  TypeFactory factory;
  const GraphElementType* dynamic_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      /*static_property_types=*/{},
      /*element_tables=*/{}, &dynamic_graph_element_type));

  IdStringPool pool;
  const std::vector<ResolvedColumn> col_list = {ResolvedColumn(
      1, pool.Make("tbl"), pool.Make("x"), dynamic_graph_element_type)};

  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory.get_string());
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations{
      name_property_dcl.get()};
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", std::vector<std::string>{graph_name}, property_declarations);

  // Create a resolved AST where the `label` and `label_name` fields only match
  // case-insensitively.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphLabelNaryExpr> label_expr,
      ResolvedGraphLabelNaryExprBuilder()
          .set_op(ResolvedGraphLabelNaryExprEnums::OR)
          .add_operand_list(ResolvedGraphWildCardLabelBuilder())
          .add_operand_list(
              ResolvedGraphLabelBuilder()
                  .set_label(person_label.get())
                  .set_label_name(ResolvedLiteralBuilder()
                                      .set_value(Value::String("PERSON"))
                                      .set_type(factory.get_string())
                                      .Build())
                  .Build())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedQueryStmt> query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", col_list.front()))
          .set_query(
              ResolvedProjectScanBuilder()
                  .set_column_list(col_list)
                  .set_input_scan(
                      ResolvedGraphTableScanBuilder()
                          .set_column_list(col_list)
                          .set_property_graph(&graph)
                          .set_input_scan(
                              ResolvedGraphScanBuilder()
                                  .set_column_list(col_list)
                                  .set_filter_expr(nullptr)
                                  .add_input_scan_list(
                                      ResolvedGraphPathScanBuilder()
                                          .set_column_list(col_list)
                                          .set_filter_expr(nullptr)
                                          .set_head(col_list.front())
                                          .set_tail(col_list.back())
                                          .set_path_mode(nullptr)
                                          .add_input_scan_list(
                                              ResolvedGraphNodeScanBuilder()
                                                  .set_filter_expr(nullptr)
                                                  .set_label_expr(
                                                      std::move(label_expr))
                                                  .set_column_list(
                                                      col_list))))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT);
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

struct ResolvedGraphElementTableBuildStrategy {
  // property declarations errors
  bool property_declaration_missing_name = false;
  bool duplicate_property_declaration = false;

  // labels errors
  bool duplicate_label = false;

  // node table errors
  bool table_misses_label = false;
  bool miss_key_column = false;
  bool introduce_duplicate_key_column = false;

  // edge table errors
  bool miss_reference_node = false;
  bool miss_reference_node_column = false;
  bool miss_reference_edge_column = false;
};

class ResolvedPropertyGraphStmtBuilder {
 public:
  explicit ResolvedPropertyGraphStmtBuilder(
      const ResolvedGraphElementTableBuildStrategy& strategy)
      : strategy_(strategy) {}

  absl::StatusOr<std::unique_ptr<const ResolvedCreatePropertyGraphStmt>>
  Build() {
    InitTables();
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphElementTable> node_table,
        BuildResolvedGraphElementNodeTable());
    std::vector<std::unique_ptr<const ResolvedGraphElementTable>> node_tables;
    node_tables.push_back(std::move(node_table));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphElementTable> edge_table,
        BuildResolvedGraphElementEdgeTable());
    std::vector<std::unique_ptr<const ResolvedGraphElementTable>> edge_tables;
    edge_tables.push_back(std::move(edge_table));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>
            property_declarations,
        BuildPropertyDeclarations());

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<const ResolvedGraphElementLabel>> labels,
        BuildLabels());

    return ResolvedCreatePropertyGraphStmtBuilder()
        .set_name_path({kGraphName})
        .set_create_scope(create_scope_)
        .set_create_mode(create_mode_)
        .set_node_table_list(std::move(node_tables))
        .set_edge_table_list(std::move(edge_tables))
        .set_property_declaration_list(std::move(property_declarations))
        .set_label_list(std::move(labels))
        .Build();
  }

 private:
  void InitTables() {
    std::vector<const Column*> input_node_columns;
    InitNodeTableColumns();
    input_node_table_ =
        std::make_unique<SimpleTable>("input_node", GetNodeTableColumns());
    InitEdgeTableColumns();
    input_edge_table_ =
        std::make_unique<SimpleTable>("input_edge", GetEdgeTableColumns());
  }

  void InitNodeTableColumns() {
    const Type* int_type = type_factory_.get_int64();
    const Type* string_type = type_factory_.get_string();
    const Type* bool_type = type_factory_.get_bool();
    std::unique_ptr<Column> col1 =
        std::make_unique<SimpleColumn>(kNodeName, kColId, int_type);
    std::unique_ptr<Column> col2 =
        std::make_unique<SimpleColumn>(kNodeName, kColDescription, string_type);
    std::unique_ptr<Column> col3 =
        std::make_unique<SimpleColumn>(kNodeName, kColTrusted, bool_type);
    input_node_columns_.push_back(std::move(col1));
    input_node_columns_.push_back(std::move(col2));
    input_node_columns_.push_back(std::move(col3));

    ResolvedColumn resolved_col_id(
        next_column_id_++, googlesql::IdString::MakeGlobal(kNodeName),
        googlesql::IdString::MakeGlobal(kColId), type_factory_.get_int64());
    ResolvedColumn resolved_col_description(
        next_column_id_++, googlesql::IdString::MakeGlobal(kNodeName),
        googlesql::IdString::MakeGlobal(kColDescription),
        type_factory_.get_string());
    ResolvedColumn resolved_col_trusted(
        next_column_id_++, googlesql::IdString::MakeGlobal(kNodeName),
        googlesql::IdString::MakeGlobal(kColTrusted), type_factory_.get_bool());
    input_node_resolved_columns_.push_back(resolved_col_id);
    input_node_resolved_columns_.push_back(resolved_col_description);
    input_node_resolved_columns_.push_back(resolved_col_trusted);
  }

  void InitEdgeTableColumns() {
    const Type* int_type = type_factory_.get_int64();
    const Type* string_type = type_factory_.get_string();
    std::unique_ptr<Column> col1 =
        std::make_unique<SimpleColumn>(kEdgeName, kColId, int_type);
    std::unique_ptr<Column> col2 =
        std::make_unique<SimpleColumn>(kEdgeName, kColDstId, int_type);
    std::unique_ptr<Column> col3 =
        std::make_unique<SimpleColumn>(kEdgeName, kColDescription, string_type);
    input_edge_columns_.push_back(std::move(col1));
    input_edge_columns_.push_back(std::move(col2));
    input_edge_columns_.push_back(std::move(col3));

    ResolvedColumn resolved_col_id(
        next_column_id_++, googlesql::IdString::MakeGlobal(kEdgeName),
        googlesql::IdString::MakeGlobal(kColId), type_factory_.get_int64());
    ResolvedColumn resolved_col_dst_id(
        next_column_id_++, googlesql::IdString::MakeGlobal(kEdgeName),
        googlesql::IdString::MakeGlobal(kColDstId), type_factory_.get_int64());
    ResolvedColumn resolved_col_description(
        next_column_id_++, googlesql::IdString::MakeGlobal(kEdgeName),
        googlesql::IdString::MakeGlobal(kColDescription),
        type_factory_.get_string());
    input_edge_resolved_columns_.push_back(resolved_col_id);
    input_edge_resolved_columns_.push_back(resolved_col_dst_id);
    input_edge_resolved_columns_.push_back(resolved_col_description);
  }

  std::vector<const Column*> GetNodeTableColumns() {
    std::vector<const Column*> input_node_columns;
    for (const auto& column : input_node_columns_) {
      input_node_columns.push_back(column.get());
    }
    return input_node_columns;
  }

  std::vector<const Column*> GetEdgeTableColumns() {
    std::vector<const Column*> input_edge_columns;
    for (const auto& column : input_edge_columns_) {
      input_edge_columns.push_back(column.get());
    }
    return input_edge_columns;
  }

  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphElementLabel>>>
  BuildLabels() {
    std::vector<std::unique_ptr<const ResolvedGraphElementLabel>> labels;
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphElementLabel> node_label,
        ResolvedGraphElementLabelBuilder()
            .set_name(kNodeName)
            .set_property_declaration_name_list(
                {kColId, kColDescription, kColTrusted})
            .Build());
    labels.push_back(std::move(node_label));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphElementLabel> edge_label,
        ResolvedGraphElementLabelBuilder()
            .set_name(kEdgeName)
            .set_property_declaration_name_list(
                {kColId, kColDstId, kColDescription})
            .Build());
    labels.push_back(std::move(edge_label));

    if (strategy_.duplicate_label) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedGraphElementLabel> duplicate_label,
          ResolvedGraphElementLabelBuilder()
              .set_name(kNodeName)
              .set_property_declaration_name_list(
                  {kColId, kColDescription, kColTrusted})
              .Build());
      labels.push_back(std::move(duplicate_label));
    }
    return labels;
  }

  absl::StatusOr<
      std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>>
  BuildPropertyDeclarations() {
    std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>
        property_declarations;
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphPropertyDeclaration>
                         id_property_declaration,
                     ResolvedGraphPropertyDeclarationBuilder()
                         .set_name(kColId)
                         .set_type(type_factory_.get_int64())
                         .Build());
    property_declarations.push_back(std::move(id_property_declaration));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphPropertyDeclaration>
                         dst_id_property_declaration,
                     ResolvedGraphPropertyDeclarationBuilder()
                         .set_name(kColDstId)
                         .set_type(type_factory_.get_int64())
                         .Build());
    property_declarations.push_back(std::move(dst_id_property_declaration));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphPropertyDeclaration>
                         trusted_property_declaration,
                     ResolvedGraphPropertyDeclarationBuilder()
                         .set_name(kColTrusted)
                         .set_type(type_factory_.get_bool())
                         .Build());
    property_declarations.push_back(std::move(trusted_property_declaration));

    ResolvedGraphPropertyDeclarationBuilder description_property_builder;
    description_property_builder.set_type(type_factory_.get_string());
    if (strategy_.property_declaration_missing_name) {
      description_property_builder.set_name("");
    } else {
      description_property_builder.set_name(kColDescription);
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphPropertyDeclaration>
                         description_property_declaration,
                     std::move(description_property_builder).Build());
    property_declarations.push_back(
        std::move(description_property_declaration));

    if (strategy_.duplicate_property_declaration) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphPropertyDeclaration>
                           duplicate_property_declaration,
                       ResolvedGraphPropertyDeclarationBuilder()
                           .set_name(kColId)
                           .set_type(type_factory_.get_int64())
                           .Build());
      property_declarations.push_back(
          std::move(duplicate_property_declaration));
    }
    return property_declarations;
  }

  absl::Status AddPropertyDefinitions(
      std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>&
          property_defs,
      const Column* column, const Type* type,
      absl::string_view property_declaration_name, absl::string_view sql) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> col_resolved_expr,
                     ResolvedCatalogColumnRefBuilder()
                         .set_column(column)
                         .set_type(type)
                         .Build());
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphPropertyDefinition> id_property_def,
        ResolvedGraphPropertyDefinitionBuilder()
            .set_property_declaration_name(property_declaration_name)
            .set_sql(sql)
            .set_expr(std::move(col_resolved_expr))
            .Build());
    property_defs.push_back(std::move(id_property_def));
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedGraphElementTable>>
  BuildResolvedGraphElementNodeTable() {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedTableScan> table_scan,
                     ResolvedTableScanBuilder()
                         .set_table(input_node_table_.get())
                         .add_column_list(input_node_resolved_columns_[0])
                         .add_column_list(input_node_resolved_columns_[1])
                         .add_column_list(input_node_resolved_columns_[2])
                         .Build());
    ResolvedGraphElementTableBuilder node_table_builder;
    if (!strategy_.miss_key_column) {
      std::vector<std::unique_ptr<const ResolvedExpr>> key_list;
      const ResolvedColumn& resolved_col = input_node_resolved_columns_[0];
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> key_column,
                       ResolvedColumnRefBuilder()
                           .set_type(type_factory_.get_int64())
                           .set_column(resolved_col)
                           .set_is_correlated(false)
                           .Build());
      key_list.push_back(std::move(key_column));
      node_table_builder.set_key_list(std::move(key_list));
    }
    if (!strategy_.table_misses_label) {
      node_table_builder.set_label_name_list({kNodeName});
    }
    node_table_builder.set_alias(kNodeName).set_input_scan(
        std::move(table_scan));

    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
        property_defs;
    GOOGLESQL_RETURN_IF_ERROR(
        AddPropertyDefinitions(property_defs, input_node_columns_[0].get(),
                               type_factory_.get_int64(), kColId, kColId));
    GOOGLESQL_RETURN_IF_ERROR(AddPropertyDefinitions(
        property_defs, input_node_columns_[1].get(), type_factory_.get_string(),
        kColDescription, kColDescription));
    GOOGLESQL_RETURN_IF_ERROR(AddPropertyDefinitions(
        property_defs, input_node_columns_[2].get(), type_factory_.get_bool(),
        kColTrusted, kColTrusted));
    node_table_builder.set_property_definition_list(std::move(property_defs));
    return std::move(node_table_builder).Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedGraphNodeTableReference>>
  BuildResolvedNodeTableReference(int64_t edge_ref_column_idx) {
    ResolvedGraphNodeTableReferenceBuilder node_table_ref_builder;
    node_table_ref_builder.set_node_table_identifier(kNodeName);

    // Source and destination nodes are both keyed by the Id column.
    std::vector<std::unique_ptr<const ResolvedExpr>> node_ref_columns;
    const ResolvedColumn& node_src_col = input_node_resolved_columns_[0];
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> node_ref_column,
                     ResolvedColumnRefBuilder()
                         .set_type(type_factory_.get_int64())
                         .set_column(node_src_col)
                         .set_is_correlated(false)
                         .Build());
    if (!strategy_.miss_reference_node_column) {
      node_ref_columns.push_back(std::move(node_ref_column));
    }

    std::vector<std::unique_ptr<const ResolvedExpr>> edge_ref_columns;
    const ResolvedColumn& edge_src_col =
        input_edge_resolved_columns_[edge_ref_column_idx];
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> edge_ref_column,
                     ResolvedColumnRefBuilder()
                         .set_type(type_factory_.get_int64())
                         .set_column(edge_src_col)
                         .set_is_correlated(false)
                         .Build());
    if (!strategy_.miss_reference_edge_column) {
      edge_ref_columns.push_back(std::move(edge_ref_column));
    }
    return std::move(node_table_ref_builder)
        .set_node_table_column_list(std::move(node_ref_columns))
        .set_edge_table_column_list(std::move(edge_ref_columns))
        .Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedGraphElementTable>>
  BuildResolvedGraphElementEdgeTable() {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedTableScan> table_scan,
                     ResolvedTableScanBuilder()
                         .set_table(input_edge_table_.get())
                         .add_column_list(input_edge_resolved_columns_[0])
                         .add_column_list(input_edge_resolved_columns_[1])
                         .add_column_list(input_edge_resolved_columns_[2])
                         .Build());
    std::vector<std::unique_ptr<const ResolvedExpr>> key_list;
    const ResolvedColumn& src_id_col = input_edge_resolved_columns_[0];
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> key_column_src_id,
                     ResolvedColumnRefBuilder()
                         .set_type(type_factory_.get_int64())
                         .set_column(src_id_col)
                         .set_is_correlated(false)
                         .Build());
    key_list.push_back(std::move(key_column_src_id));

    const ResolvedColumn& dst_id_col = input_edge_resolved_columns_[1];
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> key_column_dst_id,
                     ResolvedColumnRefBuilder()
                         .set_type(type_factory_.get_int64())
                         .set_column(dst_id_col)
                         .set_is_correlated(false)
                         .Build());
    key_list.push_back(std::move(key_column_dst_id));

    ResolvedGraphElementTableBuilder edge_table_builder;
    edge_table_builder.set_alias(kEdgeName)
        .set_input_scan(std::move(table_scan))
        .set_key_list(std::move(key_list))
        .set_label_name_list({kEdgeName});

    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
        property_defs;
    GOOGLESQL_RETURN_IF_ERROR(
        AddPropertyDefinitions(property_defs, input_edge_columns_[0].get(),
                               type_factory_.get_int64(), kColId, kColId));
    GOOGLESQL_RETURN_IF_ERROR(AddPropertyDefinitions(
        property_defs, input_edge_columns_[1].get(), type_factory_.get_int64(),
        kColDstId, kColDstId));
    GOOGLESQL_RETURN_IF_ERROR(AddPropertyDefinitions(
        property_defs, input_edge_columns_[2].get(), type_factory_.get_string(),
        kColDescription, kColDescription));
    edge_table_builder.set_property_definition_list(std::move(property_defs));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphNodeTableReference>
                         src_node_table_ref,
                     BuildResolvedNodeTableReference(0));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphNodeTableReference>
                         dst_node_table_ref,
                     BuildResolvedNodeTableReference(1));

    edge_table_builder.set_source_node_reference(std::move(src_node_table_ref));
    if (!strategy_.miss_reference_node) {
      edge_table_builder.set_dest_node_reference(std::move(dst_node_table_ref));
    }
    return std::move(edge_table_builder).Build();
  }

  static constexpr char kGraphName[] = "AML";
  ResolvedCreateStatement::CreateScope create_scope_ =
      ResolvedCreateStatement::CREATE_DEFAULT_SCOPE;
  ResolvedCreateStatement::CreateMode create_mode_ =
      ResolvedCreateStatement::CREATE_DEFAULT;
  static constexpr char kNodeName[] = "node_input_table";
  static constexpr char kEdgeName[] = "edge_input_table";
  static constexpr char kColId[] = "Id";
  static constexpr char kColDescription[] = "Description";
  static constexpr char kColTrusted[] = "Trusted";
  static constexpr char kColDstId[] = "DstId";

  const ResolvedGraphElementTableBuildStrategy& strategy_;

  // Owned
  std::unique_ptr<SimpleTable> input_node_table_;
  std::unique_ptr<SimpleTable> input_edge_table_;
  std::vector<std::unique_ptr<Column>> input_node_columns_;
  std::vector<std::unique_ptr<Column>> input_edge_columns_;
  std::vector<ResolvedColumn> input_node_resolved_columns_ = {};
  std::vector<ResolvedColumn> input_edge_resolved_columns_ = {};
  std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>
      property_declarations_;
  int next_column_id_ = 1;
  TypeFactory type_factory_;
};

void ExerciseValidatorOnGraphStatement(
    const ResolvedGraphElementTableBuildStrategy& strategy,
    absl::string_view expected_error_message, bool expect_ok = false) {
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedCreatePropertyGraphStmt>
                           create_property_graph_stmt,
                       builder.Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  Validator validator(language_options);

  if (expect_ok) {
    GOOGLESQL_EXPECT_OK(
        validator.ValidateResolvedStatement(create_property_graph_stmt.get()));
  } else {
    EXPECT_THAT(
        validator.ValidateResolvedStatement(create_property_graph_stmt.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr(expected_error_message)));
  }
}

TEST(ValidateTest, PropertyDeclarationHappyPath) {
  ResolvedGraphElementTableBuildStrategy strategy;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(strategy, "", /*expect_ok=*/true);
}

TEST(ValidateTest, PropertyDeclarationMissingName) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.property_declaration_missing_name = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(strategy, "!property_dcl->name().empty()");
}

TEST(ValidateTest, PropertyDeclarationDuplicateName) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.duplicate_property_declaration = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(strategy, "property_dcl_name_map");
}

TEST(ValidateTest, GraphContainsDuplicateLabel) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.duplicate_label = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(
      strategy, "label_name_set.insert(label->name()).second");
}

TEST(ValidateTest, GraphNodeMissesLabel) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.table_misses_label = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(
      strategy, "!element_table->label_name_list().empty() ");
}

TEST(ValidateTest, ElementTableKeyColumnMissing) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.miss_key_column = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(strategy,
                                    "!element_table->key_list().empty()");
}

TEST(ValidateTest, EdgeTableMissesDestinationReference) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.miss_reference_node = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(
      strategy, "element_table->dest_node_reference() != nullptr");
}

TEST(ValidateTest, EdgeTableMissingNodeReferenceColumn) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.miss_reference_node_column = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(
      strategy, "Node reference node_input_table has no column list");
}

TEST(ValidateTest, EdgeTableMissingEdgeReferenceColumn) {
  ResolvedGraphElementTableBuildStrategy strategy;
  strategy.miss_reference_edge_column = true;
  ResolvedPropertyGraphStmtBuilder builder(strategy);
  ExerciseValidatorOnGraphStatement(
      strategy, "Node reference node_input_table has no edge column list");
}

TEST(ValidateTest, DifferentialPrivacyAggregateScanWithModifierModes) {
  IdStringPool pool;
  auto dp_function =
      std::make_unique<Function>("count", "test_group", Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto dp_count_call,
                       ResolvedAggregateFunctionCallBuilder()
                           .set_type(types::Int64Type())
                           .set_function(dp_function.get())
                           .set_signature(std::move(sig))
                           .Build());

  ResolvedColumn column_dp_count =
      ResolvedColumn(1, pool.Make("agg"), pool.Make("c"), types::Int64Type());
  auto custom_option = MakeResolvedOption(
      "", "custom_option",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto differential_privacy_query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("c", column_dp_count))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(column_dp_count)
                         .set_input_scan(
                             ResolvedDifferentialPrivacyAggregateScanBuilder()
                                 .add_column_list(column_dp_count)
                                 .set_input_scan(MakeResolvedSingleRowScan())
                                 .add_aggregate_list(
                                     ResolvedComputedColumnBuilder()
                                         .set_column(column_dp_count)
                                         .set_expr(std::move(dp_count_call)))
                                 .set_group_selection_threshold_expr(
                                     ResolvedColumnRefBuilder()
                                         .set_type(types::Int64Type())
                                         .set_column(column_dp_count)
                                         .set_is_correlated(false))
                                 .add_option_list(std::move(custom_option))))
          .Build());

  AllowedHintsAndOptions allowed_hints_and_options;
  allowed_hints_and_options.AddDifferentialPrivacyOption("custom_option",
                                                         types::Int64Type());
  ValidatorOptions validator_options{.allowed_hints_and_options =
                                         allowed_hints_and_options};
  // FEATURE_DIFFERENTIAL_PRIVACY disabled.
  {
    LanguageOptions language_options;
    Validator validator(language_options, validator_options);
    EXPECT_THAT(validator.ValidateResolvedStatement(
                    differential_privacy_query_stmt.get()),
                testing::StatusIs(absl::StatusCode::kInternal));
  }
  // FEATURE_DIFFERENTIAL_PRIVACY enabled.
  {
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_DIFFERENTIAL_PRIVACY);
    Validator validator(language_options, validator_options);

    GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(
        differential_privacy_query_stmt.get()));
  }
}

TEST(ValidateTest, AggregationThresholdAggregateScanCorrect) {
  IdStringPool pool;
  auto threshold_option = MakeResolvedOption(
      /*qualifier=*/"", /*name=*/"threshold",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));
  auto max_groups_contributed_option = MakeResolvedOption(
      /*qualifier=*/"", /*name=*/"MAX_GROUPS_CONTRIBUTED",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));

  std::vector<std::unique_ptr<ResolvedOption>> options;
  options.push_back(std::move(threshold_option));
  options.push_back(std::move(max_groups_contributed_option));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto node_with_threshold,
                       MakeAggregationThresholdQuery(std::move(options), pool));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_AGGREGATION_THRESHOLD);
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(node_with_threshold.get()));
}

TEST(ValidateTest, AggregationThresholdAggregateScanFeatureDisabled) {
  IdStringPool pool;
  auto threshold_option = MakeResolvedOption(
      /*qualifier=*/"", /*name=*/"threshold",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));
  std::vector<std::unique_ptr<ResolvedOption>> options;
  options.push_back(std::move(threshold_option));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto node_with_threshold,
                       MakeAggregationThresholdQuery(std::move(options), pool));

  LanguageOptions language_options;
  Validator validator(language_options);
  EXPECT_THAT(validator.ValidateResolvedStatement(node_with_threshold.get()),
              testing::StatusIs(absl::StatusCode::kInternal));
}

TEST(ValidateTest, AggregationThresholdAggregateScanQualifier) {
  IdStringPool pool;
  auto qualifier_option = MakeResolvedOption(
      /*qualifier=*/"qualifier", /*name=*/"MAX_rows_contributed",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));

  std::vector<std::unique_ptr<ResolvedOption>> options;
  options.push_back(std::move(qualifier_option));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto node_with_qualifier_option,
                       MakeAggregationThresholdQuery(std::move(options), pool));
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_AGGREGATION_THRESHOLD);
  Validator validator(language_options);

  EXPECT_THAT(
      validator.ValidateResolvedStatement(node_with_qualifier_option.get()),
      testing::StatusIs(absl::StatusCode::kInternal));
}

TEST(ValidateTest, AggregationThresholdAggregateScanInvalidOption) {
  IdStringPool pool;

  auto invalid_option = MakeResolvedOption(
      /*qualifier=*/"", /*name=*/"invalid_option",
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)));

  std::vector<std::unique_ptr<ResolvedOption>> options;
  options.push_back(std::move(invalid_option));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto node_with_invalid_option,
                       MakeAggregationThresholdQuery(std::move(options), pool));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_AGGREGATION_THRESHOLD);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(node_with_invalid_option.get()),
      testing::StatusIs(absl::StatusCode::kInternal));
}

static std::unique_ptr<ResolvedSetOperationItem> CreateSetOperationItem(
    int column_id, absl::string_view node_source, IdStringPool& pool) {
  ResolvedColumn column = ResolvedColumn(
      column_id, pool.Make("table"), pool.Make("column"), types::Int64Type());
  std::unique_ptr<ResolvedScan> scan = MakeResolvedSingleRowScan({column});
  scan->set_node_source(node_source);
  std::unique_ptr<ResolvedSetOperationItem> item =
      MakeResolvedSetOperationItem(std::move(scan), {column});
  return item;
}

TEST(ValidateTest, ValidGroupingSetsResolvedAST) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, MakeGroupingSetsResolvedAST(
                                            pool, GroupingSetTestMode::kValid));
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_GROUPING_SETS);
  Validator validator(language_options);

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

// The ResolvedAggregateScan.grouping_set_list has additional key references
// that are not in the group_by_list.
TEST(ValidateTest, InvalidGroupingSetsResolvedASTMissingGroupByKey) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      MakeGroupingSetsResolvedAST(pool, GroupingSetTestMode::kMissGroupByKey));
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_GROUPING_SETS);
  Validator validator(language_options);

  absl::Status status = validator.ValidateResolvedStatement(query_stmt.get());
  EXPECT_THAT(status, testing::StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(status.message(), HasSubstr("Incorrect reference to column"));
}

// The ResolvedAggregateScan.grouping_set_list doesn't contain all keys in the
// group_by_list.
TEST(ValidateTest, InvalidGroupingSetsResolvedASTMissingGroupByKeyReference) {
  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt,
                       MakeGroupingSetsResolvedAST(
                           pool, GroupingSetTestMode::KMissGroupByKeyRef));
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_GROUPING_SETS);
  Validator validator(language_options);

  absl::Status status = validator.ValidateResolvedStatement(query_stmt.get());
  EXPECT_THAT(status, testing::StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(status.message(), HasSubstr("Incorrect reference to column"));
}

TEST(ValidateTest, ErrorWhenSideEffectColumnIsNotConsumed) {
  IdStringPool pool;

  ResolvedColumn main_column(/*column_id=*/1, pool.Make("table"),
                             pool.Make("main_col"), types::Int64Type());
  ResolvedColumn side_effect_column(/*column_id=*/2, pool.Make("table"),
                                    pool.Make("side_effect"),
                                    types::BytesType());

  // Manually construct an invalid ResolvedAST where the side effect column
  // is not consumed.
  Function agg_fn("agg1", "test_group", Function::AGGREGATE);
  FunctionSignature sig(/*result_type=*/FunctionArgumentType(
                            types::Int64Type(), /*num_occurrences=*/1),
                        /*arguments=*/{},
                        /*context_id=*/static_cast<int64_t>(1234));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(ResolvedOutputColumnBuilder()
                                      .set_column(main_column)
                                      .set_name("col1"))
          .set_query(
              ResolvedAggregateScanBuilder()
                  .set_input_scan(ResolvedSingleRowScanBuilder())
                  .add_column_list(main_column)
                  .add_aggregate_list(
                      ResolvedDeferredComputedColumnBuilder()
                          .set_column(main_column)
                          .set_side_effect_column(side_effect_column)
                          .set_expr(ResolvedAggregateFunctionCallBuilder()
                                        .set_type(types::Int64Type())
                                        .set_function(&agg_fn)
                                        .set_signature(sig))))
          .Build());

  LanguageOptions options_with_conditional_eval;
  options_with_conditional_eval.EnableLanguageFeature(
      FEATURE_ENFORCE_CONDITIONAL_EVALUATION);

  // Validate statement
  EXPECT_THAT(
      Validator().ValidateResolvedStatement(stmt.get()),
      testing::StatusIs(absl::StatusCode::kInternal,
                        HasSubstr("FEATURE_ENFORCE_CONDITIONAL_EVALUATION)")));

  EXPECT_THAT(
      Validator(options_with_conditional_eval)
          .ValidateResolvedStatement(stmt.get()),
      testing::StatusIs(absl::StatusCode::kInternal,
                        HasSubstr("unconsumed_side_effect_columns_.empty()")));

  // Validating a standalone expr
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto expr, ResolvedSubqueryExprBuilder()
                     .set_type(types::Int64Type())
                     .set_subquery_type(ResolvedSubqueryExpr::SCALAR)
                     .set_subquery(ToBuilder(std::move(stmt)).release_query())
                     .Build());

  EXPECT_THAT(
      Validator().ValidateStandaloneResolvedExpr(expr.get()),
      testing::StatusIs(absl::StatusCode::kInternal,
                        HasSubstr("FEATURE_ENFORCE_CONDITIONAL_EVALUATION")));
  EXPECT_THAT(
      Validator(options_with_conditional_eval)
          .ValidateStandaloneResolvedExpr(expr.get()),
      testing::StatusIs(absl::StatusCode::kInternal,
                        HasSubstr("unconsumed_side_effect_columns_.empty()")));
}

TEST(ValidateTest, MultilevelAggregationNotYetSupported) {
  IdStringPool pool;
  auto agg_function =
      std::make_unique<Function>("count", "test_group", Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  ResolvedColumn placeholder_column = ResolvedColumn(
      1, pool.Make("table_name"), pool.Make("name"), types::Int64Type());

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedComputedColumn>
            placeholder_computed_column,
        ResolvedComputedColumnBuilder()
            .set_column(placeholder_column)
            .set_expr(MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
            .Build());

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto agg_function_call,
        ResolvedAggregateFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .add_group_by_list(std::move(placeholder_computed_column))
            .Build());

    EXPECT_THAT(
        Validator().ValidateStandaloneResolvedExpr(agg_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Aggregate functions can only have a group_by_list when "
                      "FEATURE_MULTILEVEL_AGGREGATION is enabled")));
  }

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedComputedColumn>
            placeholder_computed_column,
        ResolvedComputedColumnBuilder()
            .set_column(placeholder_column)
            .set_expr(MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
            .Build());

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto agg_function_call,
        ResolvedAggregateFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .add_group_by_aggregate_list(std::move(placeholder_computed_column))
            .Build());

    EXPECT_THAT(
        Validator().ValidateStandaloneResolvedExpr(agg_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Multi-level aggregation requires a non-empty group by list")));
  }
}

TEST(ValidateTest, MultilevelAggregationWithGroupByHintAndEmptyGroupByList) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
  IdStringPool pool;
  auto agg_function =
      std::make_unique<Function>("count", "test_group", Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  ResolvedColumn placeholder_column = ResolvedColumn(
      1, pool.Make("table_name"), pool.Make("name"), types::Int64Type());

  // Valid group_by_hint, but empty group_by_list.
  std::vector<std::unique_ptr<const ResolvedOption>> group_by_hint_list;
  group_by_hint_list.push_back(MakeResolvedOption(
      "a", "b", MakeResolvedLiteral(types::Int64Type(), Value::Int64(1))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_function_call,
      ResolvedAggregateFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(agg_function.get())
          .set_signature(sig)
          .set_group_by_hint_list(std::move(group_by_hint_list))
          .Build());

  EXPECT_THAT(Validator(language_options)
                  .ValidateStandaloneResolvedExpr(agg_function_call.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Group by hints can only be specified with "
                                 "a non-empty group by list")));
}

TEST(ValidateTest, MultilevelAggregationWithBadGroupByHint) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
  IdStringPool pool;
  auto agg_function =
      std::make_unique<Function>("count", "test_group", Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  ResolvedColumn placeholder_column = ResolvedColumn(
      1, pool.Make("table_name"), pool.Make("name"), types::Int64Type());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedComputedColumn> placeholder_computed_column,
      ResolvedComputedColumnBuilder()
          .set_column(placeholder_column)
          .set_expr(MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
          .Build());

  std::vector<std::unique_ptr<const ResolvedOption>> group_by_hint_list;
  group_by_hint_list.push_back(MakeResolvedOption(
      "a", "b",
      MakeResolvedColumnRef(placeholder_column.type(), placeholder_column,
                            /*is_correlated=*/false)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_function_call,
      ResolvedAggregateFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(agg_function.get())
          .set_signature(sig)
          .add_group_by_list(std::move(placeholder_computed_column))
          .set_group_by_hint_list(std::move(group_by_hint_list))
          .Build());

  EXPECT_THAT(Validator(language_options)
                  .ValidateStandaloneResolvedExpr(agg_function_call.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Resolved AST validation failed: Incorrect "
                                 "reference to column table_name.name#1")));
}

TEST(ValidateTest, AggregateFiltering) {
  LanguageOptions language_options;
  IdStringPool pool;
  auto agg_function =
      std::make_unique<Function>("count", "test_group", Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  ResolvedColumn placeholder_column = ResolvedColumn(
      1, pool.Make("table_name"), pool.Make("name"), types::Int64Type());

  {
    // Test valid where_expr is not supported when feature is disabled.
    language_options.DisableAllLanguageFeatures();
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_filter_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto agg_function_call,
                         ResolvedAggregateFunctionCallBuilder()
                             .set_type(types::Int64Type())
                             .set_function(agg_function.get())
                             .set_signature(sig)
                             .set_where_expr(std::move(placeholder_filter_expr))
                             .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(agg_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Aggregate functions can only have a where_expr when "
                      "FEATURE_AGGREGATE_FILTERING is enabled")));
  }
  {
    // Test valid having_expr is not supported when feature is disabled.
    language_options.DisableAllLanguageFeatures();
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_having_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto agg_function_call,
        ResolvedAggregateFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_having_expr(std::move(placeholder_having_expr))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(agg_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Aggregate functions can only have a having_expr when "
                      "FEATURE_AGGREGATE_FILTERING is enabled")));
  }
  {
    // Test valid where_expr is supported when feature is enabled.
    language_options.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_where_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto agg_function_call,
                         ResolvedAggregateFunctionCallBuilder()
                             .set_type(types::Int64Type())
                             .set_function(agg_function.get())
                             .set_signature(sig)
                             .set_where_expr(std::move(placeholder_where_expr))
                             .Build());

    GOOGLESQL_EXPECT_OK(
        validator.ValidateStandaloneResolvedExpr(agg_function_call.get()));
  }
  {
    // Test valid having_expr is supported when feature is enabled.
    language_options.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
    language_options.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedComputedColumn>
            placeholder_computed_column,
        ResolvedComputedColumnBuilder()
            .set_column(placeholder_column)
            .set_expr(MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_having_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto agg_function_call,
        ResolvedAggregateFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .add_group_by_list(std::move(placeholder_computed_column))
            .set_having_expr(std::move(placeholder_having_expr))
            .Build());

    GOOGLESQL_EXPECT_OK(
        validator.ValidateStandaloneResolvedExpr(agg_function_call.get()));
  }
  {
    // Test where_expr must be a bool.
    language_options.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> bad_placeholder_where_expr,
        ResolvedLiteralBuilder()
            .set_type(types::Int64Type())
            .set_value(Value::Int64(1))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto agg_function_call,
        ResolvedAggregateFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_where_expr(std::move(bad_placeholder_where_expr))
            .Build());
    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(agg_function_call.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Expects BOOL found: INT64")));
  }
  {
    // Test having_expr must be a bool.
    language_options.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
    language_options.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedComputedColumn>
            placeholder_computed_column,
        ResolvedComputedColumnBuilder()
            .set_column(placeholder_column)
            .set_expr(MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> bad_placeholder_having_expr,
        ResolvedLiteralBuilder()
            .set_type(types::Int64Type())
            .set_value(Value::Int64(1))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto agg_function_call,
        ResolvedAggregateFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .add_group_by_list(std::move(placeholder_computed_column))
            .set_having_expr(std::move(bad_placeholder_having_expr))
            .Build());
    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(agg_function_call.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Expects BOOL found: INT64")));
  }
}

TEST(ValidateTest, AnalyticFilteringRequiresFeature) {
  LanguageOptions language_options;
  IdStringPool pool;
  FunctionOptions options(FunctionOptions::ORDER_OPTIONAL,
                          /*window_framing_support_in=*/true);
  auto agg_function = std::make_unique<Function>("count", "test_group",
                                                 Function::ANALYTIC, options);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(1234));
  ResolvedColumn placeholder_column = ResolvedColumn(
      1, pool.Make("table_name"), pool.Make("name"), types::Int64Type());
  {
    language_options.DisableAllLanguageFeatures();
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_filter_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedWindowFrame> placeholder_window_frame,
        ResolvedWindowFrameBuilder()
            .set_start_expr(MakeResolvedWindowFrameExpr(
                ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING,
                /*expression=*/nullptr))
            .set_end_expr(MakeResolvedWindowFrameExpr(
                ResolvedWindowFrameExpr::CURRENT_ROW, /*expression=*/nullptr))
            .set_frame_unit(ResolvedWindowFrameEnums::ROWS)
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto analytic_function_call,
        ResolvedAnalyticFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_window_frame(std::move(placeholder_window_frame))
            .set_where_expr(std::move(placeholder_filter_expr))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(analytic_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Analytic functions can only have a where_expr when ")));
  }
  {
    language_options.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_filter_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedWindowFrame> placeholder_window_frame,
        ResolvedWindowFrameBuilder()
            .set_start_expr(MakeResolvedWindowFrameExpr(
                ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING,
                /*expression=*/nullptr))
            .set_end_expr(MakeResolvedWindowFrameExpr(
                ResolvedWindowFrameExpr::CURRENT_ROW, /*expression=*/nullptr))
            .set_frame_unit(ResolvedWindowFrameEnums::ROWS)
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto analytic_function_call,
        ResolvedAnalyticFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_window_frame(std::move(placeholder_window_frame))
            .set_where_expr(std::move(placeholder_filter_expr))
            .Build());

    GOOGLESQL_EXPECT_OK(
        validator.ValidateStandaloneResolvedExpr(analytic_function_call.get()));
  }
}

TEST(ValidateTest, EstimatorFunctionCall) {
  LanguageOptions language_options;
  IdStringPool pool;

  auto scalar_function_no_within = std::make_unique<Function>(
      "count_no_within", "test_group", Function::SCALAR);
  auto agg_function = std::make_unique<Function>(
      "custom_count", Function::kGoogleSQLFunctionGroupName,
      Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(-1));

  {
    // Test estimator functions are not supported when feature is disabled.
    language_options.DisableAllLanguageFeatures();
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto estimator_function_call,
        ResolvedEstimatorFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_within_bounds(MakeResolvedWithinBounds(
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(estimator_function_call.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Estimator functions are not supported")));
  }
  {
    // Test within_bounds cannot be null.
    language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto estimator_function_call,
                         ResolvedEstimatorFunctionCallBuilder()
                             .set_type(types::Int64Type())
                             .set_function(agg_function.get())
                             .set_signature(sig)
                             .set_within_bounds(nullptr)
                             .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(estimator_function_call.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("call->within_bounds() != nullptr")));
  }
  {
    // Test distinct is not supported.
    language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto estimator_function_call,
        ResolvedEstimatorFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_distinct(true)
            .set_within_bounds(MakeResolvedWithinBounds(
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(estimator_function_call.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Estimator functions do not support DISTINCT")));
  }
  {
    // Test null handling modifiers are not supported.
    language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto estimator_function_call,
        ResolvedEstimatorFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_null_handling_modifier(
                ResolvedNonScalarFunctionCallBase::IGNORE_NULLS)
            .set_within_bounds(MakeResolvedWithinBounds(
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(estimator_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Estimator functions do not support NULL handling modifiers")));
  }
  {
    // Test where_expr is not supported.
    language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
    language_options.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<const ResolvedLiteral> placeholder_where_expr,
        ResolvedLiteralBuilder()
            .set_type(types::BoolType())
            .set_value(Value::Bool(true))
            .Build());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto estimator_function_call,
        ResolvedEstimatorFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_where_expr(std::move(placeholder_where_expr))
            .set_within_bounds(MakeResolvedWithinBounds(
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(estimator_function_call.get()),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Estimator functions do not support WHERE expression")));
  }
  {
    // Test function requires within clause support.
    language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto estimator_function_call,
        ResolvedEstimatorFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(scalar_function_no_within.get())
            .set_signature(sig)
            .set_within_bounds(MakeResolvedWithinBounds(
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
            .Build());

    EXPECT_THAT(
        validator.ValidateStandaloneResolvedExpr(estimator_function_call.get()),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("SupportsWithinClause")));
  }
  {
    // Test valid estimator function call succeeds.
    language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
    Validator validator(language_options);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto estimator_function_call,
        ResolvedEstimatorFunctionCallBuilder()
            .set_type(types::Int64Type())
            .set_function(agg_function.get())
            .set_signature(sig)
            .set_within_bounds(MakeResolvedWithinBounds(
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
                MakeResolvedWithinBoundExpr(
                    ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
            .Build());

    GOOGLESQL_EXPECT_OK(validator.ValidateStandaloneResolvedExpr(
        estimator_function_call.get()));
  }
}

TEST(ValidateTest, ResolvedBarrierScanBasic) {
  IdStringPool pool;
  ResolvedColumn col1(1, pool.Make("table"), pool.Make("col1'"),
                      types::Int64Type());
  ResolvedColumn col2(2, pool.Make("table"), pool.Make("col2"),
                      types::Int64Type());

  ResolvedColumnList column_list = {col1, col2};

  std::unique_ptr<ResolvedBarrierScan> barrier_scan = MakeResolvedBarrierScan(
      column_list,
      MakeResolvedProjectScan(
          column_list,
          MakeNodeVector(MakeResolvedComputedColumn(
                             col1, MakeResolvedLiteral(Value::Int64(1))),
                         MakeResolvedComputedColumn(
                             col2, MakeResolvedLiteral(Value::Int64(2)))),
          MakeResolvedSingleRowScan()));

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;
  output_column_list.push_back(MakeResolvedOutputColumn("col1", col1));
  output_column_list.push_back(MakeResolvedOutputColumn("col2", col2));

  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeResolvedQueryStmt(std::move(output_column_list),
                            /*is_value_table=*/false, std::move(barrier_scan));

  LanguageOptions language_options;
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidateTest, ResolvedBarrierScanReferencesWrongColumn) {
  IdStringPool pool;
  ResolvedColumn col1(1, pool.Make("table"), pool.Make("col1'"),
                      types::Int64Type());
  ResolvedColumn col2(2, pool.Make("table"), pool.Make("col2"),
                      types::DoubleType());

  std::unique_ptr<ResolvedBarrierScan> barrier_scan = MakeResolvedBarrierScan(
      /*column_list=*/{col1, col2},
      MakeResolvedProjectScan(
          /*column_list=*/{col1},
          MakeNodeVector(MakeResolvedComputedColumn(
              col1, MakeResolvedLiteral(Value::Int64(1)))),
          MakeResolvedSingleRowScan()));

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;
  output_column_list.push_back(MakeResolvedOutputColumn("col1", col1));
  output_column_list.push_back(MakeResolvedOutputColumn("col2", col2));

  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeResolvedQueryStmt(std::move(output_column_list),
                            /*is_value_table=*/false, std::move(barrier_scan));

  // Validation fails because `col2` is not a column in the input scan.
  LanguageOptions language_options;
  Validator validator(language_options);
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Column list contains column table.col2#2 not "
                                 "visible in scan node")));
}

// Creates a builder for a ResolvedAlignScan.
// Assigns column ID 100 for the aligned_timestamp_column.
static ResolvedAlignScanBuilder CreateAlignScanBuilder(
    IdStringPool& pool, ResolvedColumn input_col, Value input_val,
    ResolvedColumn timestamp_col, Value timestamp_val) {
  return ResolvedAlignScanBuilder()
      .set_input_scan(
          ResolvedProjectScanBuilder()
              .set_input_scan(ResolvedSingleRowScanBuilder())
              .add_column_list(input_col)
              .add_expr_list(MakeResolvedComputedColumn(
                  input_col, MakeResolvedLiteral(std::move(input_val))))
              .add_column_list(timestamp_col)
              .add_expr_list(MakeResolvedComputedColumn(
                  timestamp_col,
                  MakeResolvedLiteral(std::move(timestamp_val)))))
      .set_timestamp_column(ResolvedColumnRefBuilder()
                                .set_column(timestamp_col)
                                .set_type(timestamp_col.type())
                                .set_is_correlated(false))
      .set_period(MakeResolvedLiteral(Value::Interval(IntervalValue())))
      .set_origin(MakeResolvedLiteral(Value::Timestamp(absl::UnixEpoch())))
      .set_aligned_timestamp_column(ResolvedColumn(
          100, pool.Make("$align"), pool.Make("$aligned_timestamp"),
          types::TimestampType()))
      .set_column_list({ResolvedColumn(100, pool.Make("$align"),
                                       pool.Make("$aligned_timestamp"),
                                       types::TimestampType())})
      .set_output_within(MakeResolvedWithinBounds(
          MakeResolvedWithinBoundExpr(
              ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
          MakeResolvedWithinBoundExpr(ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                                      nullptr)));
}

static absl::StatusOr<std::unique_ptr<const ResolvedQueryStmt>>
MakeAlignScanQuery(IdStringPool& pool,
                   std::unique_ptr<const ResolvedAlignScan> align_scan) {
  ResolvedColumn aligned_timestamp_col = align_scan->aligned_timestamp_column();
  return ResolvedQueryStmtBuilder()
      .add_output_column_list(ResolvedOutputColumnBuilder()
                                  .set_column(aligned_timestamp_col)
                                  .set_name(""))
      .set_query(ResolvedProjectScanBuilder()
                     .set_input_scan(std::move(align_scan))
                     .add_column_list(aligned_timestamp_col))
      .Build();
}

// Creates a builder for a ResolvedMatchRecognizeScan with no measures.
// Assigns column IDs 100, 101 and 102 for the match state columns.
static ResolvedMatchRecognizeScanBuilder CreateMatchRecognizeScanBuilder(
    IdStringPool& pool, ResolvedColumn input_col) {
  return ResolvedMatchRecognizeScanBuilder()
      .set_input_scan(ResolvedProjectScanBuilder()
                          .set_input_scan(ResolvedSingleRowScanBuilder())
                          .add_column_list(input_col)
                          .add_expr_list(MakeResolvedComputedColumn(
                              input_col, MakeResolvedLiteral(Value::Int64(1)))))
      .add_analytic_function_group_list(
          ResolvedAnalyticFunctionGroupBuilder()
              .set_partition_by(nullptr)
              .set_order_by(
                  ResolvedWindowOrderingBuilder().add_order_by_item_list(
                      ResolvedOrderByItemBuilder().set_column_ref(
                          ResolvedColumnRefBuilder()
                              .set_column(input_col)
                              .set_type(input_col.type())
                              .set_is_correlated(false)))))
      .set_after_match_skip_mode(ResolvedMatchRecognizeScanEnums::END_OF_MATCH)
      .set_match_number_column(
          ResolvedColumn(100, pool.Make("$match_recognize"),
                         pool.Make("$match_number"), types::Int64Type()))
      .set_match_row_number_column(
          ResolvedColumn(101, pool.Make("$match_recognize"),
                         pool.Make("$match_row_number"), types::Int64Type()))
      .set_classifier_column(ResolvedColumn(102, pool.Make("$match_recognize"),
                                            pool.Make("$classifier"),
                                            types::StringType()))
      .set_pattern(
          ResolvedMatchRecognizePatternVariableRefBuilder().set_name("A"))
      .add_pattern_variable_definition_list(
          ResolvedMatchRecognizeVariableDefinitionBuilder()
              .set_name("A")
              .set_predicate(MakeResolvedLiteral(Value::Bool(true))));
}

// Intended to test pattern variable name validation in MATCH_RECOGNIZE.
// Creates a minimal MATCH_RECOGNIZE scan with a given singleton pattern (i.e.,
// one variable) and a single definition with the given name, with the literal
// TRUE.
static absl::StatusOr<std::unique_ptr<const ResolvedQueryStmt>>
MakeMatchRecognizeQuery(IdStringPool& pool, absl::string_view name_in_pattern,
                        absl::string_view name_in_define) {
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());

  return ResolvedQueryStmtBuilder()
      .add_output_column_list(
          ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
      .set_query(
          CreateMatchRecognizeScanBuilder(pool, col0)
              .add_column_list(col1)
              .add_measure_group_list(
                  ResolvedMeasureGroupBuilder().add_aggregate_list(
                      ResolvedComputedColumnBuilder().set_column(col1).set_expr(
                          MakeResolvedLiteral(Value::Double(1.0)))))
              .set_pattern(
                  ResolvedMatchRecognizePatternVariableRefBuilder().set_name(
                      name_in_pattern))
              // Clear definitions from the builder, to put our own
              .set_pattern_variable_definition_list(
                  std::vector<std::unique_ptr<
                      const ResolvedMatchRecognizeVariableDefinition>>{})
              .add_pattern_variable_definition_list(
                  ResolvedMatchRecognizeVariableDefinitionBuilder()
                      .set_name(name_in_define)
                      .set_predicate(MakeResolvedLiteral(Value::Bool(true)))))
      .Build();
}

TEST(ValidateTest, AlignScanValid) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, CreateAlignScanBuilder(
                                            pool, col0, Value::Int64(1), col1,
                                            Value::Timestamp(absl::UnixEpoch()))
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query.get()));
}

TEST(ValidateTest, AlignScanInvalidTimestampColumnType) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"), types::Int64Type());

  auto align_scan_builder = CreateAlignScanBuilder(pool, col0, Value::Int64(1),
                                                   col1, Value::Int64(1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("scan->timestamp_column()->type()->IsTimestamp()")));
}

TEST(ValidateTest, AlignScanInvalidPeriodType) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_period(MakeResolvedLiteral(Value::Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("scan->period()->type()->IsInterval()")));
}

TEST(ValidateTest, AlignScanInvalidOriginType) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_origin(MakeResolvedLiteral(Value::Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("scan->origin()->type()->IsTimestamp()")));
}

TEST(ValidateTest, AlignScanValidPartitionByColumnType) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));

  align_scan_builder.add_partition_by_list(ResolvedColumnRefBuilder()
                                               .set_column(col0)
                                               .set_type(col0.type())
                                               .set_is_correlated(false));
  align_scan_builder.set_column_list(
      {col0, align_scan_builder.aligned_timestamp_column()});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query.get()));
}

TEST(ValidateTest, AlignScanEstimatorFunctionNotAggregate) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto scalar_function =
      std::make_unique<Function>("foo", "test_group", Function::SCALAR);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(-1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto estimator_function_call,
      ResolvedEstimatorFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(scalar_function.get())
          .set_signature(sig)
          .set_within_bounds(MakeResolvedWithinBounds(
              MakeResolvedWithinBoundExpr(
                  ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING, nullptr),
              MakeResolvedWithinBoundExpr(
                  ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP, nullptr)))
          .Build());

  ResolvedColumn estimator_col(3, pool.Make("t1"), pool.Make("estimator_col"),
                               types::Int64Type());
  auto computed_column = MakeResolvedComputedColumn(
      estimator_col, std::move(estimator_function_call));

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.add_estimator_function_list(std::move(computed_column));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  EXPECT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "cannot be used in an estimator function call, since it does "
              "not support a WITHIN clause")));
}

TEST(ValidateTest, AlignScanInvalidPartitionByColumnType) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::DoubleType());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder =
      CreateAlignScanBuilder(pool, col0, Value::Double(1.0), col1,
                             Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.add_partition_by_list(ResolvedColumnRefBuilder()
                                               .set_column(col0)
                                               .set_type(col0.type())
                                               .set_is_correlated(false));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("does not support partitioning")));
}

TEST(ValidateTest, AlignScanNullTimestampColumn) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  const_cast<ResolvedAlignScan*>(query->query()
                                     ->GetAs<ResolvedProjectScan>()
                                     ->input_scan()
                                     ->GetAs<ResolvedAlignScan>())
      ->release_timestamp_column();

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("scan->timestamp_column() != nullptr")));
}

TEST(ValidateTest, AlignScanNullPeriod) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  const_cast<ResolvedAlignScan*>(query->query()
                                     ->GetAs<ResolvedProjectScan>()
                                     ->input_scan()
                                     ->GetAs<ResolvedAlignScan>())
      ->release_period();

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("scan->period() != nullptr")));
}

TEST(ValidateTest, AlignScanInvalidDuplicatePartitionByColumn) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.add_partition_by_list(ResolvedColumnRefBuilder()
                                               .set_column(col0)
                                               .set_type(col0.type())
                                               .set_is_correlated(false));
  align_scan_builder.add_partition_by_list(ResolvedColumnRefBuilder()
                                               .set_column(col0)
                                               .set_type(col0.type())
                                               .set_is_correlated(false));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Duplicate partitioning column")));
}

TEST(ValidateTest, AlignScanInvalidDuplicateAlignedTimestampColumnId) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_aligned_timestamp_column(
      ResolvedColumn(1, pool.Make("$align"), pool.Make("$aligned_timestamp"),
                     types::TimestampType()));
  align_scan_builder.set_column_list(
      {col0, col1,
       ResolvedColumn(1, pool.Make("$align"), pool.Make("$aligned_timestamp"),
                      types::TimestampType())});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Duplicate column id 1 in column")));
}

TEST(ValidateTest, AlignScanNullOutputWithinLowerBound) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  const_cast<ResolvedWithinBounds*>(query->query()
                                        ->GetAs<ResolvedProjectScan>()
                                        ->input_scan()
                                        ->GetAs<ResolvedAlignScan>()
                                        ->output_within())
      ->release_lower_bound();

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("within_bound_expr != nullptr")));
}

TEST(ValidateTest, AlignScanNullOutputWithinUpperBound) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  const_cast<ResolvedWithinBounds*>(query->query()
                                        ->GetAs<ResolvedProjectScan>()
                                        ->input_scan()
                                        ->GetAs<ResolvedAlignScan>()
                                        ->output_within())
      ->release_upper_bound();

  ASSERT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("within_bound_expr != nullptr")));
}

struct WithinBoundsParam {
  ResolvedWithinBoundExpr::BoundKind lower_kind;
  ResolvedWithinBoundExpr::BoundKind upper_kind;
};

enum class InvalidExprKind { kUnexpected, kMissing, kInvalidType };

struct WithinBoundInvalidExprParam {
  WithinBoundsParam bounds;
  InvalidExprKind invalid_kind;
  std::string expected_error;
};

class AlignScanWithinBoundInvalidExprTest
    : public ::testing::TestWithParam<WithinBoundInvalidExprParam> {};

TEST_P(AlignScanWithinBoundInvalidExprTest, InvalidBoundExpr) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  const WithinBoundInvalidExprParam& param = GetParam();

  auto create_invalid_expr =
      [](ResolvedWithinBoundExpr::BoundKind kind,
         InvalidExprKind invalid_kind) -> std::unique_ptr<ResolvedExpr> {
    switch (invalid_kind) {
      case InvalidExprKind::kMissing:
        return nullptr;
      case InvalidExprKind::kUnexpected:
      case InvalidExprKind::kInvalidType:
        return MakeResolvedLiteral(Value::String("1"));
    }
  };

  auto create_expr_for_kind = [](ResolvedWithinBoundExpr::BoundKind kind)
      -> std::unique_ptr<ResolvedExpr> {
    switch (kind) {
      case ResolvedWithinBoundExpr::PERIOD_PRECEDING:
      case ResolvedWithinBoundExpr::PERIOD_FOLLOWING:
        return MakeResolvedLiteral(Value::Int64(1));
      case ResolvedWithinBoundExpr::INTERVAL_PRECEDING:
      case ResolvedWithinBoundExpr::INTERVAL_FOLLOWING:
        return MakeResolvedLiteral(Value::Interval(IntervalValue()));
      case ResolvedWithinBoundExpr::TIMESTAMP:
        return MakeResolvedLiteral(Value::Timestamp(absl::UnixEpoch()));
      default:
        return nullptr;
    }
  };

  auto lower_expr = create_expr_for_kind(param.bounds.lower_kind);
  auto upper_expr =
      create_invalid_expr(param.bounds.upper_kind, param.invalid_kind);

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_output_within(MakeResolvedWithinBounds(
      MakeResolvedWithinBoundExpr(param.bounds.lower_kind,
                                  std::move(lower_expr)),
      MakeResolvedWithinBoundExpr(param.bounds.upper_kind,
                                  std::move(upper_expr))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr(param.expected_error)));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidExpr, AlignScanWithinBoundInvalidExprTest,
    ::testing::Values(
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING},
            InvalidExprKind::kUnexpected,
            "must not specify a bound expression"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                              ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
            InvalidExprKind::kUnexpected,
            "must not specify a bound expression"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                              ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
            InvalidExprKind::kUnexpected,
            "must not specify a bound expression"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                              ResolvedWithinBoundExpr::PERIOD_PRECEDING},
            InvalidExprKind::kMissing,
            "must specify a bound expression of type INT64 or DOUBLE"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                              ResolvedWithinBoundExpr::PERIOD_PRECEDING},
            InvalidExprKind::kInvalidType,
            "must specify a bound expression of type INT64 or DOUBLE"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
            InvalidExprKind::kMissing,
            "must specify a bound expression of type INT64 or DOUBLE"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
            InvalidExprKind::kInvalidType,
            "must specify a bound expression of type INT64 or DOUBLE"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                              ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
            InvalidExprKind::kMissing,
            "must specify a bound expression of type INTERVAL"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                              ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
            InvalidExprKind::kInvalidType,
            "must specify a bound expression of type INTERVAL"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
            InvalidExprKind::kMissing,
            "must specify a bound expression of type INTERVAL"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
            InvalidExprKind::kInvalidType,
            "must specify a bound expression of type INTERVAL"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::TIMESTAMP},
            InvalidExprKind::kMissing,
            "must specify a bound expression of type TIMESTAMP"},
        WithinBoundInvalidExprParam{
            WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                              ResolvedWithinBoundExpr::TIMESTAMP},
            InvalidExprKind::kInvalidType,
            "must specify a bound expression of type TIMESTAMP"}));

class AlignScanWithinBoundsExplicitTimestampTest
    : public ::testing::TestWithParam<WithinBoundsParam> {};

TEST_P(AlignScanWithinBoundsExplicitTimestampTest, InvalidWithinClause) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  const WithinBoundsParam& param = GetParam();

  auto create_expr_for_kind = [](ResolvedWithinBoundExpr::BoundKind kind)
      -> std::unique_ptr<ResolvedExpr> {
    switch (kind) {
      case ResolvedWithinBoundExpr::PERIOD_PRECEDING:
      case ResolvedWithinBoundExpr::PERIOD_FOLLOWING:
        return MakeResolvedLiteral(Value::Int64(1));
      case ResolvedWithinBoundExpr::INTERVAL_PRECEDING:
      case ResolvedWithinBoundExpr::INTERVAL_FOLLOWING:
        return MakeResolvedLiteral(Value::Interval(IntervalValue()));
      case ResolvedWithinBoundExpr::TIMESTAMP:
        return MakeResolvedLiteral(Value::Timestamp(absl::UnixEpoch()));
      default:
        return nullptr;
    }
  };

  auto lower_expr = create_expr_for_kind(param.lower_kind);
  auto upper_expr = create_expr_for_kind(param.upper_kind);

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_output_within(MakeResolvedWithinBounds(
      MakeResolvedWithinBoundExpr(param.lower_kind, std::move(lower_expr)),
      MakeResolvedWithinBoundExpr(param.upper_kind, std::move(upper_expr))));

  auto estimator_lower_expr = create_expr_for_kind(param.lower_kind);
  auto estimator_upper_expr = create_expr_for_kind(param.upper_kind);
  auto agg_function = std::make_unique<Function>(
      "custom_count", Function::kGoogleSQLFunctionGroupName,
      Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(-1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto estimator_function_call,
      ResolvedEstimatorFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(agg_function.get())
          .set_signature(sig)
          .set_within_bounds(MakeResolvedWithinBounds(
              MakeResolvedWithinBoundExpr(param.lower_kind,
                                          std::move(estimator_lower_expr)),
              MakeResolvedWithinBoundExpr(param.upper_kind,
                                          std::move(estimator_upper_expr))))
          .Build());
  ResolvedColumn estimator_col(3, pool.Make("t1"), pool.Make("estimator_col"),
                               types::Int64Type());
  auto computed_column = MakeResolvedComputedColumn(
      estimator_col, std::move(estimator_function_call));
  align_scan_builder.add_estimator_function_list(std::move(computed_column));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "A WITHIN clause cannot mix TIMESTAMP bound with other bounds")));
}

INSTANTIATE_TEST_SUITE_P(
    ExplicitTimestamp, AlignScanWithinBoundsExplicitTimestampTest,
    ::testing::Values(
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::TIMESTAMP}));

TEST(ValidateTest, AlignScanOutputWithinToTimestamp) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_output_within(MakeResolvedWithinBounds(
      MakeResolvedWithinBoundExpr(ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                                  nullptr),
      MakeResolvedWithinBoundExpr(
          ResolvedWithinBoundExpr::TIMESTAMP,
          MakeResolvedLiteral(Value::Timestamp(absl::UnixEpoch())))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query.get()));
}

class AlignScanWithinBoundsEmptyBoundTest
    : public ::testing::TestWithParam<WithinBoundsParam> {};

TEST_P(AlignScanWithinBoundsEmptyBoundTest, InvalidWithinClause) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  const WithinBoundsParam& param = GetParam();

  auto create_expr_for_kind = [](ResolvedWithinBoundExpr::BoundKind kind)
      -> std::unique_ptr<ResolvedExpr> {
    switch (kind) {
      case ResolvedWithinBoundExpr::PERIOD_PRECEDING:
      case ResolvedWithinBoundExpr::PERIOD_FOLLOWING:
        return MakeResolvedLiteral(Value::Int64(1));
      case ResolvedWithinBoundExpr::INTERVAL_PRECEDING:
      case ResolvedWithinBoundExpr::INTERVAL_FOLLOWING:
        return MakeResolvedLiteral(Value::Interval(IntervalValue()));
      case ResolvedWithinBoundExpr::TIMESTAMP:
        return MakeResolvedLiteral(Value::Timestamp(absl::UnixEpoch()));
      default:
        return nullptr;
    }
  };

  auto lower_expr = create_expr_for_kind(param.lower_kind);
  auto upper_expr = create_expr_for_kind(param.upper_kind);

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_output_within(MakeResolvedWithinBounds(
      MakeResolvedWithinBoundExpr(param.lower_kind, std::move(lower_expr)),
      MakeResolvedWithinBoundExpr(param.upper_kind, std::move(upper_expr))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  ASSERT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Invalid WITHIN clause, represents empty WITHIN bound")));
}

INSTANTIATE_TEST_SUITE_P(
    EmptyBounds, AlignScanWithinBoundsEmptyBoundTest,
    ::testing::Values(
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP}));

class AlignScanWithinBoundsValidBoundTest
    : public ::testing::TestWithParam<WithinBoundsParam> {};

TEST_P(AlignScanWithinBoundsValidBoundTest, ValidWithinClause) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ALIGN_OPERATOR);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("t1"), pool.Make("ts1"),
                      types::TimestampType());

  const WithinBoundsParam& param = GetParam();

  auto create_expr_for_kind = [](ResolvedWithinBoundExpr::BoundKind kind)
      -> std::unique_ptr<ResolvedExpr> {
    switch (kind) {
      case ResolvedWithinBoundExpr::PERIOD_PRECEDING:
      case ResolvedWithinBoundExpr::PERIOD_FOLLOWING:
        return MakeResolvedLiteral(Value::Int64(1));
      case ResolvedWithinBoundExpr::INTERVAL_PRECEDING:
      case ResolvedWithinBoundExpr::INTERVAL_FOLLOWING:
        return MakeResolvedLiteral(Value::Interval(IntervalValue()));
      case ResolvedWithinBoundExpr::TIMESTAMP:
        return MakeResolvedLiteral(Value::Timestamp(absl::UnixEpoch()));
      default:
        return nullptr;
    }
  };

  auto lower_expr = create_expr_for_kind(param.lower_kind);
  auto upper_expr = create_expr_for_kind(param.upper_kind);

  auto align_scan_builder = CreateAlignScanBuilder(
      pool, col0, Value::Int64(1), col1, Value::Timestamp(absl::UnixEpoch()));
  align_scan_builder.set_output_within(MakeResolvedWithinBounds(
      MakeResolvedWithinBoundExpr(param.lower_kind, std::move(lower_expr)),
      MakeResolvedWithinBoundExpr(param.upper_kind, std::move(upper_expr))));

  auto estimator_lower_expr = create_expr_for_kind(param.lower_kind);
  auto estimator_upper_expr = create_expr_for_kind(param.upper_kind);
  auto agg_function = std::make_unique<Function>(
      "custom_count", Function::kGoogleSQLFunctionGroupName,
      Function::AGGREGATE);
  FunctionSignature sig(FunctionArgumentType(types::Int64Type(), 1), {},
                        static_cast<int64_t>(-1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto estimator_function_call,
      ResolvedEstimatorFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(agg_function.get())
          .set_signature(sig)
          .set_within_bounds(MakeResolvedWithinBounds(
              MakeResolvedWithinBoundExpr(param.lower_kind,
                                          std::move(estimator_lower_expr)),
              MakeResolvedWithinBoundExpr(param.upper_kind,
                                          std::move(estimator_upper_expr))))
          .Build());
  ResolvedColumn estimator_col(3, pool.Make("t1"), pool.Make("estimator_col"),
                               types::Int64Type());
  auto computed_column = MakeResolvedComputedColumn(
      estimator_col, std::move(estimator_function_call));
  align_scan_builder.add_estimator_function_list(std::move(computed_column));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto align_scan, std::move(align_scan_builder).Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeAlignScanQuery(pool, std::move(align_scan)));

  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query.get()));
}

INSTANTIATE_TEST_SUITE_P(
    ValidBounds, AlignScanWithinBoundsValidBoundTest,
    ::testing::Values(
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP,
                          ResolvedWithinBoundExpr::TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_PRECEDING,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::PERIOD_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::INTERVAL_PRECEDING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_PRECEDING,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::PERIOD_FOLLOWING,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::PERIOD_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::INTERVAL_FOLLOWING,
                          ResolvedWithinBoundExpr::INTERVAL_FOLLOWING},
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP},
        WithinBoundsParam{ResolvedWithinBoundExpr::TIMESTAMP,
                          ResolvedWithinBoundExpr::TIMESTAMP}));

TEST(ValidateTest, MatchRecognizeRequiresTheFlag) {
  LanguageOptions language_options;
  language_options.DisableAllLanguageFeatures();
  Validator validator(language_options);

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, /*name_in_pattern=*/"A",
                                               /*name_in_define=*/"A"));

  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("MATCH_RECOGNIZE is not supported")));
}

TEST(ValidateTest, EmptyVariableNameDisallowedInPattern) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, /*name_in_pattern=*/"",
                                               /*name_in_define=*/"A"));
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Pattern variable name cannot be empty")));
}

TEST(ValidateTest, EmptyVariableNameDisallowedInDefine) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, /*name_in_pattern=*/"A",
                                               /*name_in_define=*/""));
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Pattern variable name cannot be empty")));
}

TEST(ValidateTest, PatternVariableReferenceMustHaveACorrespondingDefinition) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, /*name_in_pattern=*/"A",
                                               /*name_in_define=*/"B"));
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Pattern variable A has no definition")));
}

TEST(ValidateTest, PatternVariableReferencesMustMatchTheCaseInDefinition) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, /*name_in_pattern=*/"A",
                                               /*name_in_define=*/"a"));
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Pattern variable A has no definition")));
}

TEST(ValidateTest, MatchRecognizeOrderByListCannotBeEmpty) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col0).set_name(""))
          .set_query(CreateMatchRecognizeScanBuilder(pool, col0)
                         .add_column_list(col1)
                         // Clear the default analytic groups, to build our
                         // specific case.
                         .set_analytic_function_group_list(
                             std::vector<std::unique_ptr<
                                 const ResolvedAnalyticFunctionGroup>>{})
                         .add_analytic_function_group_list(
                             ResolvedAnalyticFunctionGroupBuilder()
                                 .set_partition_by(nullptr)
                                 .set_order_by(ResolvedWindowOrderingBuilder()))
                         .add_measure_group_list(
                             ResolvedMeasureGroupBuilder().add_aggregate_list(
                                 ResolvedComputedColumnBuilder()
                                     .set_column(col1)
                                     .set_expr(MakeResolvedLiteral(
                                         Value::Double(1.0))))))
          .Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("ORDER BY item list cannot be empty")));
}

TEST(ValidateTest, MatchRecognizeDefineListCannotBeEmpty) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(
              CreateMatchRecognizeScanBuilder(pool, col0)
                  .add_column_list(col1)
                  .set_pattern_variable_definition_list(
                      std::vector<std::unique_ptr<
                          const ResolvedMatchRecognizeVariableDefinition>>{})
                  .add_measure_group_list(
                      ResolvedMeasureGroupBuilder().add_aggregate_list(
                          ResolvedComputedColumnBuilder()
                              .set_column(col1)
                              .set_expr(
                                  MakeResolvedLiteral(Value::Double(1.0))))))
          .Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Pattern variable definition list cannot be empty")));
}

// DEFINE cannot have duplicates, even ignoring case and an identical predicate.
TEST(ValidateTest, MatchRecognizeDefineListCannotHaveDuplicates) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(
              CreateMatchRecognizeScanBuilder(pool, col0)
                  .add_column_list(col1)
                  // Clear definitions from the builder, to put our own.
                  .set_pattern_variable_definition_list(
                      std::vector<std::unique_ptr<
                          const ResolvedMatchRecognizeVariableDefinition>>{})
                  .add_pattern_variable_definition_list(
                      ResolvedMatchRecognizeVariableDefinitionBuilder()
                          .set_name("A")
                          .set_predicate(
                              MakeResolvedLiteral(Value::Bool(true))))
                  .add_pattern_variable_definition_list(
                      ResolvedMatchRecognizeVariableDefinitionBuilder()
                          .set_name("a")
                          .set_predicate(
                              MakeResolvedLiteral(Value::Bool(true))))
                  .add_measure_group_list(
                      ResolvedMeasureGroupBuilder().add_aggregate_list(
                          ResolvedComputedColumnBuilder()
                              .set_column(col1)
                              .set_expr(
                                  MakeResolvedLiteral(Value::Double(1.0))))))
          .Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Pattern variable names are supposed to be "
                                 "unique, but found duplicate name: a")));
}

// Intended to test pattern validation in MATCH_RECOGNIZE.
// Creates a minimal MATCH_RECOGNIZE scan with a single variable definition
// `DEFINE A as TRUE`, and accepts a pattern to be plugged into the tree.
static absl::StatusOr<std::unique_ptr<const ResolvedQueryStmt>>
MakeMatchRecognizeQuery(
    IdStringPool& pool,
    std::unique_ptr<const ResolvedMatchRecognizePatternExpr> pattern) {
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());

  return ResolvedQueryStmtBuilder()
      .add_output_column_list(
          ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
      .set_query(
          CreateMatchRecognizeScanBuilder(pool, col0)
              .add_column_list(col1)
              .add_measure_group_list(
                  ResolvedMeasureGroupBuilder().add_aggregate_list(
                      ResolvedComputedColumnBuilder().set_column(col1).set_expr(
                          MakeResolvedLiteral(Value::Double(1.0)))))
              .set_pattern(std::move(pattern)))
      .Build();
}

TEST(ValidateTest, MatchRecognizePatternOperationsAreNeverUnspecified) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto pattern,
      ResolvedMatchRecognizePatternOperationBuilder()
          .set_op_type(ResolvedMatchRecognizePatternOperationEnums::
                           OPERATION_TYPE_UNSPECIFIED)
          .Build());

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, std::move(pattern)));
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unsupported row pattern operation type: "
                                 "OPERATION_TYPE_UNSPECIFIED")));
}

TEST(ValidateTest, MatchRecognizeConcatRequireAtLeastTwoOperands) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto pattern,
      ResolvedMatchRecognizePatternOperationBuilder()
          .set_op_type(ResolvedMatchRecognizePatternOperationEnums::CONCAT)
          .add_operand_list(
              ResolvedMatchRecognizePatternVariableRefBuilder().set_name("A"))
          .Build());

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, std::move(pattern)));
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr(
                   "Pattern operation CONCAT requires at least two operands")));
}

TEST(ValidateTest, MatchRecognizeAlternateRequireAtLeastTwoOperands) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto pattern,
      ResolvedMatchRecognizePatternOperationBuilder()
          .set_op_type(ResolvedMatchRecognizePatternOperationEnums::ALTERNATE)
          .add_operand_list(
              ResolvedMatchRecognizePatternVariableRefBuilder().set_name("A"))
          .Build());

  IdStringPool pool;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query,
                       MakeMatchRecognizeQuery(pool, std::move(pattern)));
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "Pattern operation ALTERNATE requires at least two operands")));
}

TEST(ValidateTest, MatchRecognizeDoesNotAllowDuplicateRefsInPartitioning) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(col1)
                  .add_expr_list(MakeResolvedComputedColumn(
                      col1, MakeResolvedLiteral(Value::Double(1.0))))
                  .set_input_scan(
                      CreateMatchRecognizeScanBuilder(pool, col0)
                          // Clear the default analytic groups, to build our
                          // specific case.
                          .set_analytic_function_group_list(
                              std::vector<std::unique_ptr<
                                  const ResolvedAnalyticFunctionGroup>>{})
                          .add_analytic_function_group_list(
                              ResolvedAnalyticFunctionGroupBuilder()
                                  .set_order_by(nullptr)
                                  .set_partition_by(
                                      ResolvedWindowPartitioningBuilder()
                                          .add_partition_by_list(
                                              ResolvedColumnRefBuilder()
                                                  .set_column(col0)
                                                  .set_type(col0.type())
                                                  .set_is_correlated(false))
                                          .add_partition_by_list(
                                              ResolvedColumnRefBuilder()
                                                  .set_column(col0)
                                                  .set_type(col0.type())
                                                  .set_is_correlated(false))))))
          .Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Duplicate partitioning column")));
}

TEST(ValidateTest, MatchRecognizeScanAfterMatchSkipModeMustBeSpecified) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(col1)
                         .add_expr_list(MakeResolvedComputedColumn(
                             col1, MakeResolvedLiteral(Value::Double(1.0))))
                         .set_input_scan(
                             CreateMatchRecognizeScanBuilder(pool, col0)
                                 .set_after_match_skip_mode(
                                     ResolvedMatchRecognizeScanEnums::
                                         AFTER_MATCH_SKIP_MODE_UNSPECIFIED)))
          .Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("After match skip mode must be specified")));
}

TEST(ValidateTest, MeasureGroupCanNeverBeEmpty) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(col1)
                  .add_expr_list(MakeResolvedComputedColumn(
                      col1, MakeResolvedLiteral(Value::Double(1.0))))
                  .set_input_scan(CreateMatchRecognizeScanBuilder(pool, col0)
                                      .add_measure_group_list(
                                          ResolvedMeasureGroupBuilder())))
          .Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("measure_group->aggregate_list_size() > 0 (0 vs. 0)")));
}

TEST(ValidateTest, MeasureGroupsCanOnlyHaveOneUniversalGroup) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());

  ResolvedColumn agg_col1 =
      ResolvedColumn(3, pool.Make("agg"), pool.Make("g1"), types::Int64Type());
  ResolvedColumn agg_col2 =
      ResolvedColumn(4, pool.Make("agg"), pool.Make("g2"), types::Int64Type());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(col1)
                  .add_expr_list(MakeResolvedComputedColumn(
                      col1, MakeResolvedLiteral(Value::Double(1.0))))
                  .set_input_scan(
                      CreateMatchRecognizeScanBuilder(pool, col0)
                          .set_column_list({agg_col1, agg_col2})
                          .add_measure_group_list(
                              ResolvedMeasureGroupBuilder().add_aggregate_list(
                                  MakeResolvedComputedColumn(
                                      agg_col1,
                                      MakeResolvedLiteral(Value::Int64(1)))))
                          .add_measure_group_list(
                              ResolvedMeasureGroupBuilder().add_aggregate_list(
                                  MakeResolvedComputedColumn(
                                      agg_col2,
                                      MakeResolvedLiteral(Value::Int64(1)))))))
          .Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("!universal_group_seen ")));
}

TEST(ValidateTest, MeasureGroupsMustBeUnique) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());

  ResolvedColumn agg_col1 =
      ResolvedColumn(3, pool.Make("agg"), pool.Make("g1"), types::Int64Type());
  ResolvedColumn agg_col2 =
      ResolvedColumn(4, pool.Make("agg"), pool.Make("g2"), types::Int64Type());

  auto mr_builder =
      CreateMatchRecognizeScanBuilder(pool, col0)
          .set_column_list({agg_col1, agg_col2})
          .add_measure_group_list(
              ResolvedMeasureGroupBuilder()
                  .set_pattern_variable_ref(
                      ResolvedMatchRecognizePatternVariableRefBuilder()
                          .set_name("A"))
                  .add_aggregate_list(MakeResolvedComputedColumn(
                      agg_col1, MakeResolvedLiteral(Value::Int64(1)))))
          .add_measure_group_list(
              ResolvedMeasureGroupBuilder()
                  .set_pattern_variable_ref(
                      ResolvedMatchRecognizePatternVariableRefBuilder()
                          .set_name("A"))
                  .add_aggregate_list(MakeResolvedComputedColumn(
                      agg_col2, MakeResolvedLiteral(Value::Int64(1)))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(col1)
                         .add_expr_list(MakeResolvedComputedColumn(
                             col1, MakeResolvedLiteral(Value::Double(1.0))))
                         .set_input_scan(std::move(mr_builder)))
          .Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Pattern variable A has multiple measure groups")));
}

// Even mismatching case is considered undefined.
TEST(ValidateTest, MeasureGroupVariableMustBeDefined) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MATCH_RECOGNIZE);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn col0(1, pool.Make("t1"), pool.Make("col1"),
                      types::Int64Type());
  ResolvedColumn col1(2, pool.Make("$match_recognize_1"),
                      pool.Make("$measure_1"), types::DoubleType());

  ResolvedColumn agg_col1 =
      ResolvedColumn(3, pool.Make("agg"), pool.Make("g1"), types::Int64Type());

  auto mr_builder =
      CreateMatchRecognizeScanBuilder(pool, col0)
          .set_column_list({agg_col1})
          .add_measure_group_list(
              ResolvedMeasureGroupBuilder()
                  .set_pattern_variable_ref(
                      ResolvedMatchRecognizePatternVariableRefBuilder()
                          .set_name("a"))
                  .add_aggregate_list(MakeResolvedComputedColumn(
                      agg_col1, MakeResolvedLiteral(Value::Int64(1)))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(col1).set_name(""))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(col1)
                         .add_expr_list(MakeResolvedComputedColumn(
                             col1, MakeResolvedLiteral(Value::Double(1.0))))
                         .set_input_scan(std::move(mr_builder)))
          .Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Pattern variable a has no definition.")));
}

ResolvedJoinScanBuilder CreateLateralJoinScanBuilder(
    const ResolvedColumn& outer_correlated_column,
    const ResolvedColumn& lhs_column, const SimpleTable* lhs_table,
    const ResolvedColumn& rhs_column) {
  return ResolvedJoinScanBuilder()
      .add_column_list(lhs_column)
      .add_column_list(rhs_column)
      .set_is_lateral(true)
      .add_parameter_list(ResolvedColumnRefBuilder()
                              .set_type(outer_correlated_column.type())
                              .set_column(outer_correlated_column)
                              .set_is_correlated(true))
      .add_parameter_list(ResolvedColumnRefBuilder()
                              .set_type(lhs_column.type())
                              .set_column(lhs_column)
                              .set_is_correlated(false))
      .set_left_scan(ResolvedTableScanBuilder()
                         .add_column_list(lhs_column)
                         .set_table(lhs_table))
      .set_right_scan(
          ResolvedProjectScanBuilder()
              .add_column_list(rhs_column)
              .set_input_scan(ResolvedSingleRowScanBuilder())
              .add_expr_list(ResolvedComputedColumnBuilder()
                                 .set_column(rhs_column)
                                 .set_expr(ResolvedColumnRefBuilder()
                                               .set_type(lhs_column.type())
                                               .set_column(lhs_column)
                                               .set_is_correlated(true))));
}

TEST(ValidatorTest, NonLateralJoinCannotListLateralColumns) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_LATERAL_JOIN);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn outer_col(1, pool.Make("t1"), pool.Make("col1"),
                           types::Int64Type());
  ResolvedColumn col2(2, pool.Make("t1"), pool.Make("col2"),
                      types::Int64Type());
  ResolvedColumn col3(3, pool.Make("t1"), pool.Make("col3"),
                      types::Int64Type());
  ResolvedColumn output_col(4, pool.Make("$query"), pool.Make("output_col"),
                            types::Int64Type());

  const SimpleTable t1{"t1"};

  auto join_builder = CreateLateralJoinScanBuilder(outer_col, col2, &t1, col3);
  join_builder.set_is_lateral(false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(output_col).set_name(""))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(output_col)
                  .add_expr_list(
                      ResolvedComputedColumnBuilder()
                          .set_column(output_col)
                          .set_expr(ResolvedSubqueryExprBuilder()
                                        .set_type(types::Int64Type())
                                        .set_subquery_type(
                                            ResolvedSubqueryExpr::SCALAR)
                                        .set_subquery(std::move(join_builder))
                                        .add_parameter_list(
                                            ResolvedColumnRefBuilder()
                                                .set_type(outer_col.type())
                                                .set_column(outer_col)
                                                .set_is_correlated(false))))
                  .set_input_scan(ResolvedTableScanBuilder()
                                      .add_column_list(outer_col)
                                      .set_table(&t1)))
          .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("scan->parameter_list().empty()")));
}

TEST(ValidatorTest, LateralJoinNeedsToDeclareLateralReferencesAsNotCorrelated) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_LATERAL_JOIN);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn outer_col(1, pool.Make("t1"), pool.Make("col1"),
                           types::Int64Type());
  ResolvedColumn col2(2, pool.Make("t1"), pool.Make("col2"),
                      types::Int64Type());
  ResolvedColumn col3(3, pool.Make("t1"), pool.Make("col3"),
                      types::Int64Type());
  ResolvedColumn output_col(4, pool.Make("$query"), pool.Make("output_col"),
                            types::Int64Type());

  const SimpleTable t1{"t1"};

  auto join_builder = CreateLateralJoinScanBuilder(outer_col, col2, &t1, col3);
  ASSERT_TRUE(join_builder.is_lateral());

  auto lateral_columns = join_builder.release_parameter_list();
  ASSERT_EQ(lateral_columns.size(), 2);
  ASSERT_FALSE(lateral_columns[1]->is_correlated());

  // Set to true, pretending that the exposed LHS column is an outer, already
  // correlated column.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      lateral_columns[1],
      ToBuilder(std::move(lateral_columns[1])).set_is_correlated(true).Build());
  join_builder.set_parameter_list(std::move(lateral_columns));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(output_col).set_name(""))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(output_col)
                  .add_expr_list(
                      ResolvedComputedColumnBuilder()
                          .set_column(output_col)
                          .set_expr(ResolvedSubqueryExprBuilder()
                                        .set_type(types::Int64Type())
                                        .set_subquery_type(
                                            ResolvedSubqueryExpr::SCALAR)
                                        .set_subquery(std::move(join_builder))
                                        .add_parameter_list(
                                            ResolvedColumnRefBuilder()
                                                .set_type(outer_col.type())
                                                .set_column(outer_col)
                                                .set_is_correlated(false))))
                  .set_input_scan(ResolvedTableScanBuilder()
                                      .add_column_list(outer_col)
                                      .set_table(&t1)))
          .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Column cannot come from the left scan")));
}

TEST(ValidatorTest,
     LateralJoinNeedsToRecordOuterCorrelatedColumnsAsAlreadyCorrelated) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_LATERAL_JOIN);
  Validator validator(language_options);

  IdStringPool pool;
  ResolvedColumn outer_col(1, pool.Make("t1"), pool.Make("col1"),
                           types::Int64Type());
  ResolvedColumn col2(2, pool.Make("t1"), pool.Make("col2"),
                      types::Int64Type());
  ResolvedColumn col3(3, pool.Make("t1"), pool.Make("col3"),
                      types::Int64Type());
  ResolvedColumn output_col(4, pool.Make("$query"), pool.Make("output_col"),
                            types::Int64Type());

  const SimpleTable t1{"t1"};

  auto join_builder = CreateLateralJoinScanBuilder(outer_col, col2, &t1, col3);
  ASSERT_TRUE(join_builder.is_lateral());

  auto lateral_columns = join_builder.release_parameter_list();
  ASSERT_EQ(lateral_columns.size(), 2);
  ASSERT_TRUE(lateral_columns[0]->is_correlated());

  // Set to false to ensure the validator catches it.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(lateral_columns[0],
                       ToBuilder(std::move(lateral_columns[0]))
                           .set_is_correlated(false)
                           .Build());
  join_builder.set_parameter_list(std::move(lateral_columns));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              ResolvedOutputColumnBuilder().set_column(output_col).set_name(""))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(output_col)
                  .add_expr_list(
                      ResolvedComputedColumnBuilder()
                          .set_column(output_col)
                          .set_expr(ResolvedSubqueryExprBuilder()
                                        .set_type(types::Int64Type())
                                        .set_subquery_type(
                                            ResolvedSubqueryExpr::SCALAR)
                                        .set_subquery(std::move(join_builder))
                                        .add_parameter_list(
                                            ResolvedColumnRefBuilder()
                                                .set_type(outer_col.type())
                                                .set_column(outer_col)
                                                .set_is_correlated(false))))
                  .set_input_scan(ResolvedTableScanBuilder()
                                      .add_column_list(outer_col)
                                      .set_table(&t1)))
          .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(query.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Column must come from the left scan")));
}

TEST(ValidatorTest, RebuildActionIsNotLastActionReturnsError) {
  Validator validator;

  std::vector<std::unique_ptr<const ResolvedAlterAction>> alter_action_list;
  alter_action_list.push_back(MakeResolvedRebuildAction());
  alter_action_list.push_back(MakeResolvedDropColumnAction(
      /*is_if_exists=*/false,
      /*name=*/"Value"));

  ResolvedColumn column;
  const SimpleTable t1{"i1"};
  std::unique_ptr<const ResolvedTableScan> table_scan =
      ResolvedTableScanBuilder()
          .add_column_list(column)
          .set_table(&t1)
          .Build()
          .value();

  std::unique_ptr<ResolvedAlterIndexStmt> alter_index_stmt =
      MakeResolvedAlterIndexStmt(
          /*name_path=*/{"i1"},
          /*alter_action_list=*/std::move(alter_action_list),
          /*is_if_exists=*/false,
          /*table_name_path=*/{"KeyValue"},
          /*index_type=*/ResolvedAlterIndexStmt::INDEX_SEARCH,
          std::move(table_scan));
  EXPECT_THAT(
      validator.ValidateResolvedStatement(alter_index_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("REBUILD action must be the last action in the list")));
}

TEST(ValidatorTest, UnsupportedAlterActionInAlterIndexStmtReturnsError) {
  Validator validator;

  std::vector<std::unique_ptr<const ResolvedAlterAction>> alter_action_list;
  alter_action_list.push_back(
      MakeResolvedDropPrimaryKeyAction(/*is_if_exists=*/false));
  ResolvedColumn column;
  const SimpleTable t1{"i1"};
  std::unique_ptr<const ResolvedTableScan> table_scan =
      ResolvedTableScanBuilder()
          .add_column_list(column)
          .set_table(&t1)
          .Build()
          .value();
  std::unique_ptr<ResolvedAlterIndexStmt> alter_index_stmt =
      MakeResolvedAlterIndexStmt(
          /*name_path=*/{"i1"},
          /*alter_action_list=*/std::move(alter_action_list),
          /*is_if_exists=*/false,
          /*table_name_path=*/{"KeyValue"},
          /*index_type=*/ResolvedAlterIndexStmt::INDEX_SEARCH,
          std::move(table_scan));
  EXPECT_THAT(validator.ValidateResolvedStatement(alter_index_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unsupported alter action in ALTER "
                                 "VECTOR|SEARCH INDEX statement")));
}

TEST(ValidatorTest, AddColumnIdentifierActionInNonIndexAlterStmtReturnsError) {
  Validator validator;

  std::vector<std::unique_ptr<const ResolvedAlterAction>> alter_action_list;
  alter_action_list.push_back(MakeResolvedAddColumnIdentifierAction(
      /*name=*/"Value",
      /*options_list=*/{},
      /*is_if_not_exists=*/false));
  std::unique_ptr<ResolvedAlterTableStmt> alter_table_stmt =
      MakeResolvedAlterTableStmt(
          /*name_path=*/{"KeyValue"},
          /*alter_action_list=*/std::move(alter_action_list),
          /*is_if_exists=*/false);
  EXPECT_THAT(validator.ValidateResolvedStatement(alter_table_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("AddColumnIdentifier action is only supported "
                                 "in ALTER VECTOR|SEARCH INDEX statement")));
}

TEST(ValidatorTest, UnsetArgumentScanUsedInNonTVFScanArgumentReturnsError) {
  Validator validator;

  IdStringPool pool;
  ResolvedColumn column = ResolvedColumn(1, pool.Make("table"),
                                         pool.Make("col"), types::Int64Type());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("c", column))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(column)
                         .set_input_scan(ResolvedUnsetArgumentScanBuilder()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("UnsetArgumentScan is only allowed as an "
                                 "argument to a TVFScan")));
}

TEST(ValidatorTest, UnsetArgumentScanContainsColumnsReturnsError) {
  Validator validator;

  IdStringPool pool;
  ResolvedColumn column = ResolvedColumn(1, pool.Make("table"),
                                         pool.Make("col"), types::Int64Type());
  TVFRelation tvf_relation({{"o1", types::Int64Type()}});

  FunctionArgumentType relation_arg_type(
      ARG_KIND_RELATION,
      FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL));
  FunctionSignature fn_signature(
      FunctionArgumentType::RelationWithSchema(
          tvf_relation,
          /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{relation_arg_type},
      /*context_ptr=*/nullptr);

  relation_arg_type.set_num_occurrences(1);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          tvf_relation,
          /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{relation_arg_type},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf({"tvf0"}, {fn_signature}, tvf_relation);

  auto signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(tvf_relation)},
      tvf_relation);
  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("c", column))
          .set_query(
              ResolvedProjectScanBuilder()
                  .add_column_list(column)
                  .set_input_scan(
                      ResolvedTVFScanBuilder()
                          .add_column_list(column)
                          .set_tvf(&tvf)
                          .set_signature(signature)
                          .set_alias("")
                          .add_argument_list(
                              ResolvedFunctionArgumentBuilder().set_scan(
                                  ResolvedUnsetArgumentScanBuilder()
                                      .add_column_list(column)))
                          .set_function_call_signature(concrete_signature)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("UnsetArgumentScan should not have any columns")));
}

TEST(ValidatorTest, TemplatedFunctionFailure) {
  IdStringPool pool;
  TypeFactory type_factory;

  // Failing body: Reference an unknown column.
  // We use a column that is not available in the current context.
  ResolvedColumn unknown_column(999, pool.Make("unknown"), pool.Make("col"),
                                types::Int64Type());
  auto failing_body =
      MakeResolvedColumnRef(types::Int64Type(), unknown_column, false);

  auto templated_info = std::make_unique<TemplatedSQLFunctionCall>(
      std::move(failing_body),
      /*aggregate_expr_list=*/
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>{});

  // Use a signature with num_occurrences = 1 to ensure IsConcrete() is true.
  FunctionSignature sig(
      /*result_type=*/FunctionArgumentType(types::Int64Type(), 1),
      /*arguments=*/
      FunctionArgumentTypeList{FunctionArgumentType(types::Int64Type(), 1)},
      /*context_id=*/static_cast<int64_t>(1));

  Function templated_fn("templated_fn", Function::kGoogleSQLFunctionGroupName,
                        Function::SCALAR, {sig});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto function_call,
                       ResolvedFunctionCallBuilder()
                           .set_type(types::Int64Type())
                           .set_function(&templated_fn)
                           .set_signature(sig)
                           .add_argument_list(MakeResolvedLiteral(
                               types::Int64Type(), Value::Int64(1)))
                           .set_function_call_info(std::move(templated_info))
                           .Build());

  Validator validator;
  absl::Status status =
      validator.ValidateStandaloneResolvedExpr(function_call.get());

  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_EQ(
      status.message(),
      R"(Resolved AST validation failed: Incorrect reference to column unknown.col#999

Templated call stack:
  GoogleSQL:templated_fn((INT64) -> INT64)

ColumnRef(type=INT64, column=unknown.col#999) (validation failed here)
)");
}

TEST(ValidatorTest, NestedTemplatedFunctionFailure) {
  IdStringPool pool;
  TypeFactory type_factory;

  // Inner function failing body.
  ResolvedColumn unknown_column(999, pool.Make("unknown"), pool.Make("col"),
                                types::Int64Type());
  auto failing_body =
      MakeResolvedColumnRef(types::Int64Type(), unknown_column, false);

  auto inner_templated_info = std::make_unique<TemplatedSQLFunctionCall>(
      std::move(failing_body),
      /*aggregate_expr_list=*/
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>{});

  FunctionSignature inner_sig(
      /*result_type=*/FunctionArgumentType(types::Int64Type(), 1),
      /*arguments=*/
      FunctionArgumentTypeList{FunctionArgumentType(types::Int64Type(), 1)},
      /*context_id=*/static_cast<int64_t>(1));

  Function inner_fn("inner_fn", Function::kGoogleSQLFunctionGroupName,
                    Function::SCALAR, {inner_sig});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto inner_call,
      ResolvedFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(&inner_fn)
          .set_signature(inner_sig)
          .add_argument_list(
              MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
          .set_function_call_info(std::move(inner_templated_info))
          .Build());

  // Outer function calls inner function.
  auto outer_templated_info = std::make_unique<TemplatedSQLFunctionCall>(
      std::move(inner_call),
      /*aggregate_expr_list=*/
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>{});

  FunctionSignature outer_sig(
      /*result_type=*/FunctionArgumentType(types::Int64Type(), 1),
      /*arguments=*/
      FunctionArgumentTypeList{FunctionArgumentType(types::Int64Type(), 1)},
      /*context_id=*/static_cast<int64_t>(2));

  Function outer_fn("outer_fn", Function::kGoogleSQLFunctionGroupName,
                    Function::SCALAR, {outer_sig});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto outer_call_node,
      ResolvedFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(&outer_fn)
          .set_signature(outer_sig)
          .add_argument_list(
              MakeResolvedLiteral(types::Int64Type(), Value::Int64(1)))
          .set_function_call_info(std::move(outer_templated_info))
          .Build());

  Validator validator;
  absl::Status status =
      validator.ValidateStandaloneResolvedExpr(outer_call_node.get());

  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kInternal,
          R"(Resolved AST validation failed: Incorrect reference to column unknown.col#999

Templated call stack:
  GoogleSQL:outer_fn((INT64) -> INT64)
  GoogleSQL:inner_fn((INT64) -> INT64)

ColumnRef(type=INT64, column=unknown.col#999) (validation failed here)
)"));
}

TEST(ValidatorTest, TemplatedTVFFailure) {
  IdStringPool pool;
  TypeFactory type_factory;

  // Failing TVF body.
  ResolvedColumn unknown_column(999, pool.Make("unknown"), pool.Make("col"),
                                types::Int64Type());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto failing_query,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("col", unknown_column))
          .set_query(ResolvedProjectScanBuilder()
                         .add_column_list(unknown_column)
                         .set_input_scan(MakeResolvedSingleRowScan()))
          .Build());

  TVFRelation result_relation({TVFRelation::Column("col", types::Int64Type())});

  auto templated_tvf_signature = std::make_shared<TemplatedSQLTVFSignature>(
      /*input_arguments=*/std::vector<TVFInputArgumentType>{},
      /*output_schema=*/result_relation, TVFSignatureOptions(),
      std::move(failing_query), /*arg_name_list=*/std::vector<std::string>{});

  std::vector<std::string> path = {"templated_tvf"};
  auto sig = std::make_shared<FunctionSignature>(
      FunctionArgumentType(ARG_KIND_RELATION,
                           FunctionArgumentTypeOptions(result_relation, false),
                           1),
      /*arguments=*/
      FunctionArgumentTypeList{},
      /*context_id=*/static_cast<int64_t>(0));
  TemplatedSQLTVF templated_tvf(path, *sig,
                                /*arg_name_list=*/std::vector<std::string>{},
                                ParseResumeLocation::FromString("SELECT 1"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto tvf_scan,
                       ResolvedTVFScanBuilder()
                           .set_tvf(&templated_tvf)
                           .set_signature(templated_tvf_signature)
                           .add_column_list(unknown_column)
                           .set_alias("tvf_alias")
                           .set_function_call_signature(sig)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto statement,
                       ResolvedQueryStmtBuilder()
                           .add_output_column_list(
                               MakeResolvedOutputColumn("col", unknown_column))
                           .set_query(std::move(tvf_scan))
                           .Build());

  Validator validator;
  absl::Status status = validator.ValidateResolvedStatement(statement.get());

  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kInternal,
          R"(Resolved AST validation failed: Column list contains column unknown.col#999 not visible in scan node
ProjectScan
+-column_list=[unknown.col#999]
+-input_scan=
  +-SingleRowScan


Templated call stack:
  templated_tvf(() -> TABLE<col INT64>)

QueryStmt
+-output_column_list=
| +-unknown.col#999 AS col [INT64]
+-query=
  +-ProjectScan (validation failed here)
    +-column_list=[unknown.col#999]
    +-input_scan=
      +-SingleRowScan
)"));
}

TEST(ValidatorTest, PassthroughTVFValidOutputColumns) {
  Validator validator;

  IdStringPool pool;
  ResolvedColumn col_a(1, pool.Make("t"), pool.Make("a"), types::Int64Type());
  ResolvedColumn col_b(2, pool.Make("t"), pool.Make("b"), types::Int64Type());

  ResolvedColumn col_a_out(3, pool.Make("t"), pool.Make("a"),
                           types::Int64Type());
  ResolvedColumn col_b_out(4, pool.Make("t"), pool.Make("b"),
                           types::Int64Type());
  ResolvedColumn col_c_out(5, pool.Make("t"), pool.Make("c"),
                           types::Int64Type());

  const SimpleTable input_table(
      "input_table", {{"a", types::Int64Type()}, {"b", types::Int64Type()}});

  TVFRelation input_relation(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}});
  TVFRelation output_relation({{"a", types::Int64Type()},
                               {"b", types::Int64Type()},
                               {"c", types::Int64Type()}});

  FunctionArgumentType concrete_relation_arg_type(
      ARG_KIND_RELATION, FunctionArgumentType::REQUIRED, /*num_occurrences=*/1);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{concrete_relation_arg_type},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf(
      {"pt_tvf"}, {*concrete_signature}, output_relation,
      TableValuedFunctionOptions().set_is_passthrough(true));

  auto signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);

  std::vector<int> column_index_list = {0, 1, 2};

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("a", col_a_out))
          .add_output_column_list(MakeResolvedOutputColumn("b", col_b_out))
          .add_output_column_list(MakeResolvedOutputColumn("c", col_c_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(col_a_out)
                         .add_column_list(col_b_out)
                         .add_column_list(col_c_out)
                         .set_tvf(&tvf)
                         .set_signature(signature)
                         .set_alias("")
                         .set_column_index_list(column_index_list)
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(col_a)
                                               .add_column_list(col_b)
                                               .add_column_index_list(0)
                                               .add_column_index_list(1))
                                 .add_argument_column_list(col_a)
                                 .add_argument_column_list(col_b))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, PassthroughTVFColumnNameMismatchReturnsError) {
  Validator validator;

  IdStringPool pool;
  ResolvedColumn col_a(1, pool.Make("t"), pool.Make("a"), types::Int64Type());
  ResolvedColumn col_b(2, pool.Make("t"), pool.Make("b"), types::Int64Type());

  ResolvedColumn col_a_out(3, pool.Make("t"), pool.Make("a"),
                           types::Int64Type());
  ResolvedColumn col_d_out(4, pool.Make("t"), pool.Make("d"),
                           types::Int64Type());
  ResolvedColumn col_c_out(5, pool.Make("t"), pool.Make("c"),
                           types::Int64Type());

  const SimpleTable input_table(
      "input_table", {{"a", types::Int64Type()}, {"b", types::Int64Type()}});

  TVFRelation input_relation(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}});
  // Output has 'd' instead of 'b'
  TVFRelation output_relation({{"a", types::Int64Type()},
                               {"d", types::Int64Type()},
                               {"c", types::Int64Type()}});

  FunctionArgumentType concrete_relation_arg_type(
      ARG_KIND_RELATION, FunctionArgumentType::REQUIRED, /*num_occurrences=*/1);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{concrete_relation_arg_type},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf(
      {"pt_tvf"}, {*concrete_signature}, output_relation,
      TableValuedFunctionOptions().set_is_passthrough(true));

  auto signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);

  std::vector<int> column_index_list = {0, 1, 2};

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("a", col_a_out))
          .add_output_column_list(MakeResolvedOutputColumn("d", col_d_out))
          .add_output_column_list(MakeResolvedOutputColumn("c", col_c_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(col_a_out)
                         .add_column_list(col_d_out)
                         .add_column_list(col_c_out)
                         .set_tvf(&tvf)
                         .set_signature(signature)
                         .set_alias("")
                         .set_column_index_list(column_index_list)
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(col_a)
                                               .add_column_list(col_b)
                                               .add_column_index_list(0)
                                               .add_column_index_list(1))
                                 .add_argument_column_list(col_a)
                                 .add_argument_column_list(col_b))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "Passthrough TVF output column 1 name does not match: d vs b")));
}

TEST(ValidatorTest, PassthroughTVFColumnTypeMismatchReturnsError) {
  Validator validator;

  IdStringPool pool;
  ResolvedColumn col_a(1, pool.Make("t"), pool.Make("a"), types::Int64Type());
  ResolvedColumn col_b(2, pool.Make("t"), pool.Make("b"), types::Int64Type());

  ResolvedColumn col_a_out(3, pool.Make("t"), pool.Make("a"),
                           types::Int64Type());
  ResolvedColumn col_b_out(4, pool.Make("t"), pool.Make("b"),
                           types::StringType());

  const SimpleTable input_table(
      "input_table", {{"a", types::Int64Type()}, {"b", types::Int64Type()}});

  TVFRelation input_relation(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}});
  TVFRelation output_relation(
      {{"a", types::Int64Type()}, {"b", types::StringType()}});

  FunctionArgumentType concrete_relation_arg_type(
      ARG_KIND_RELATION, FunctionArgumentType::REQUIRED, /*num_occurrences=*/1);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{concrete_relation_arg_type},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf(
      {"pt_tvf"}, {*concrete_signature}, output_relation,
      TableValuedFunctionOptions().set_is_passthrough(true));

  auto signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);

  std::vector<int> column_index_list = {0, 1};

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("a", col_a_out))
          .add_output_column_list(MakeResolvedOutputColumn("b", col_b_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(col_a_out)
                         .add_column_list(col_b_out)
                         .set_tvf(&tvf)
                         .set_signature(signature)
                         .set_alias("")
                         .set_column_index_list(column_index_list)
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(col_a)
                                               .add_column_list(col_b)
                                               .add_column_index_list(0)
                                               .add_column_index_list(1))
                                 .add_argument_column_list(col_a)
                                 .add_argument_column_list(col_b))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Passthrough TVF output column 1 type does "
                                 "not match: STRING vs INT64")));
}

TEST(ValidatorTest, PassthroughTVFProducedFewerColumnsReturnsError) {
  Validator validator;

  IdStringPool pool;
  ResolvedColumn col_a(1, pool.Make("t"), pool.Make("a"), types::Int64Type());
  ResolvedColumn col_b(2, pool.Make("t"), pool.Make("b"), types::Int64Type());

  ResolvedColumn col_a_out(3, pool.Make("t"), pool.Make("a"),
                           types::Int64Type());

  const SimpleTable input_table(
      "input_table", {{"a", types::Int64Type()}, {"b", types::Int64Type()}});

  TVFRelation input_relation(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}});
  TVFRelation output_relation({{"a", types::Int64Type()}});

  FunctionArgumentType concrete_relation_arg_type(
      ARG_KIND_RELATION, FunctionArgumentType::REQUIRED, /*num_occurrences=*/1);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{concrete_relation_arg_type},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf(
      {"pt_tvf"}, {*concrete_signature}, output_relation,
      TableValuedFunctionOptions().set_is_passthrough(true));

  auto signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);

  std::vector<int> column_index_list = {0};

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("a", col_a_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(col_a_out)
                         .set_tvf(&tvf)
                         .set_signature(signature)
                         .set_alias("")
                         .set_column_index_list(column_index_list)
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(col_a)
                                               .add_column_list(col_b)
                                               .add_column_index_list(0)
                                               .add_column_index_list(1))
                                 .add_argument_column_list(col_a)
                                 .add_argument_column_list(col_b))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "Passthrough TVF result schema has fewer columns than input")));
}

TEST(ValidatorTest, TVFArgumentInputSchemaMismatch) {
  Validator validator;
  IdStringPool pool;
  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType output_arg = FunctionArgumentType::RelationWithSchema(
      output_relation, /*extra_relation_input_columns_allowed=*/false);

  // Signature requires input schema `a INT64`
  TVFRelation tvf_relation_int64({{"a", types::Int64Type()}});
  FunctionArgumentType relation_arg_int64 =
      FunctionArgumentType::RelationWithSchema(
          tvf_relation_int64, /*extra_relation_input_columns_allowed=*/false);
  relation_arg_int64.set_num_occurrences(1);
  FunctionSignature signature_int64(
      output_arg, FunctionArgumentTypeList{relation_arg_int64},
      /*context_ptr=*/nullptr);

  // Signature requires input schema `a STRING`
  TVFRelation tvf_relation_string({{"a", types::StringType()}});
  FunctionArgumentType relation_arg_string =
      FunctionArgumentType::RelationWithSchema(
          tvf_relation_string, /*extra_relation_input_columns_allowed=*/false);
  relation_arg_string.set_num_occurrences(1);
  FunctionSignature signature_string(
      output_arg, FunctionArgumentTypeList{relation_arg_string},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf(
      {"overloaded_tvf"}, {signature_int64, signature_string}, output_relation);

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{
          TVFInputArgumentType(tvf_relation_int64)},
      output_relation);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{relation_arg_string},
      /*context_ptr=*/nullptr);

  ResolvedColumn input_col_a(1, pool.Make("t"), pool.Make("a"),
                             types::Int64Type());
  const SimpleTable input_table("input_table", {{"a", types::Int64Type()}});
  ResolvedColumn resolved_col_out(2, pool.Make("t"), pool.Make("a"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("a", resolved_col_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(resolved_col_out)
                         .set_tvf(&tvf)
                         .set_signature(tvf_signature)
                         .set_alias("")
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(input_col_a))
                                 .add_argument_column_list(input_col_a))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("col_idx: 0 input_type: INT64 required_type: STRING")));
}

TEST(ValidatorTest, TVFArgumentInputRelationCannotHaveTypeModifiers) {
  Validator validator;
  IdStringPool pool;
  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType output_arg = FunctionArgumentType::RelationWithSchema(
      output_relation, /*extra_relation_input_columns_allowed=*/false);

  // Signature requires input schema `a STRING` without modifiers
  TVFRelation tvf_relation_string({{"a", types::StringType()}});
  FunctionArgumentType relation_arg_string =
      FunctionArgumentType::RelationWithSchema(
          tvf_relation_string, /*extra_relation_input_columns_allowed=*/false);
  relation_arg_string.set_num_occurrences(1);
  FunctionSignature signature_string(
      output_arg, FunctionArgumentTypeList{relation_arg_string},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf({"tvf_with_modifiers"}, {signature_string},
                           output_relation);

  // The actual input relation has a column with type modifiers!
  Collation collation = Collation::MakeScalar("und:ci");
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), collation);
  TVFRelation input_relation_with_modifiers(
      {TVFSchemaColumn("a", types::StringType(), /*is_pseudo_column_in=*/false,
                       /*is_passthrough_column_in=*/false, type_modifiers)});

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{
          TVFInputArgumentType(input_relation_with_modifiers)},
      output_relation);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{relation_arg_string},
      /*context_ptr=*/nullptr);

  ResolvedColumn input_col_a(1, pool.Make("t"), pool.Make("a"),
                             types::StringType());
  const SimpleTable input_table("input_table", {{"a", types::StringType()}});
  ResolvedColumn resolved_col_out(2, pool.Make("t"), pool.Make("a"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("a", resolved_col_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(resolved_col_out)
                         .set_tvf(&tvf)
                         .set_signature(tvf_signature)
                         .set_alias("")
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(input_col_a))
                                 .add_argument_column_list(input_col_a))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(
      validator.ValidateResolvedStatement(query_stmt.get()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("input relation column must not have type modifiers")));
}

TEST(ValidatorTest, TVFArgumentWithPartitionAndOrderValid) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_TABLE_ARGUMENT_PARTITION_AND_ORDER);
  Validator validator(language_options);
  IdStringPool pool;
  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType output_arg = FunctionArgumentType::RelationWithSchema(
      output_relation, /*extra_relation_input_columns_allowed=*/false);
  TVFRelation input_relation(
      {{"a", types::StringType()}, {"b", types::Int64Type()}});
  FunctionArgumentType relation_arg = FunctionArgumentType::RelationWithSchema(
      input_relation, /*extra_relation_input_columns_allowed=*/false);
  relation_arg.set_num_occurrences(1);
  FunctionSignature signature(output_arg, {relation_arg}, nullptr);
  FixedOutputSchemaTVF tvf({"tvf_test"}, {signature}, output_relation);

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      output_arg, FunctionArgumentTypeList{relation_arg}, nullptr);

  ResolvedColumn input_col_a(1, pool.Make("t"), pool.Make("a"),
                             types::StringType());
  ResolvedColumn input_col_b(2, pool.Make("t"), pool.Make("b"),
                             types::Int64Type());
  const SimpleTable input_table(
      "input_table", {{"a", types::StringType()}, {"b", types::Int64Type()}});
  ResolvedColumn resolved_col_out(3, pool.Make("t"), pool.Make("out_col"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("out_col", resolved_col_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(resolved_col_out)
                         .set_tvf(&tvf)
                         .set_signature(tvf_signature)
                         .set_alias("")
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(input_col_a)
                                               .add_column_list(input_col_b))
                                 .add_argument_column_list(input_col_a)
                                 .add_argument_column_list(input_col_b)
                                 .add_partition_by_list(
                                     ResolvedColumnRefBuilder()
                                         .set_column(input_col_a)
                                         .set_type(types::StringType())
                                         .set_is_correlated(false))
                                 .add_order_by_list(
                                     ResolvedOrderByItemBuilder()
                                         .set_column_ref(
                                             ResolvedColumnRefBuilder()
                                                 .set_column(input_col_b)
                                                 .set_type(types::Int64Type())
                                                 .set_is_correlated(false))
                                         .set_is_descending(false)))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, TVFArgumentWithPartitionAndOrderFeatureDisabledFails) {
  LanguageOptions language_options;
  Validator validator(language_options);
  IdStringPool pool;
  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType output_arg = FunctionArgumentType::RelationWithSchema(
      output_relation, /*extra_relation_input_columns_allowed=*/false);
  TVFRelation input_relation({{"a", types::StringType()}});
  FunctionArgumentType relation_arg = FunctionArgumentType::RelationWithSchema(
      input_relation, /*extra_relation_input_columns_allowed=*/false);
  relation_arg.set_num_occurrences(1);
  FunctionSignature signature(output_arg, {relation_arg}, nullptr);
  FixedOutputSchemaTVF tvf({"tvf_test"}, {signature}, output_relation);

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      output_arg, FunctionArgumentTypeList{relation_arg}, nullptr);

  ResolvedColumn input_col_a(1, pool.Make("t"), pool.Make("a"),
                             types::StringType());
  const SimpleTable input_table("input_table", {{"a", types::StringType()}});
  ResolvedColumn resolved_col_out(2, pool.Make("t"), pool.Make("out_col"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("out_col", resolved_col_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(resolved_col_out)
                         .set_tvf(&tvf)
                         .set_signature(tvf_signature)
                         .set_alias("")
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(input_col_a))
                                 .add_argument_column_list(input_col_a)
                                 .add_partition_by_list(
                                     ResolvedColumnRefBuilder()
                                         .set_column(input_col_a)
                                         .set_type(types::StringType())
                                         .set_is_correlated(false)))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(ValidatorTest, TVFArgumentWithNonPartitionableTypeFails) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_TABLE_ARGUMENT_PARTITION_AND_ORDER);
  Validator validator(language_options);
  IdStringPool pool;
  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType output_arg = FunctionArgumentType::RelationWithSchema(
      output_relation, /*extra_relation_input_columns_allowed=*/false);
  TVFRelation input_relation({{"geo", types::GeographyType()}});
  FunctionArgumentType relation_arg = FunctionArgumentType::RelationWithSchema(
      input_relation, /*extra_relation_input_columns_allowed=*/false);
  relation_arg.set_num_occurrences(1);
  FunctionSignature signature(output_arg, {relation_arg}, nullptr);
  FixedOutputSchemaTVF tvf({"tvf_test"}, {signature}, output_relation);

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      output_arg, FunctionArgumentTypeList{relation_arg}, nullptr);

  ResolvedColumn input_col_geo(1, pool.Make("t"), pool.Make("geo"),
                               types::GeographyType());
  const SimpleTable input_table("input_table",
                                {{"geo", types::GeographyType()}});
  ResolvedColumn resolved_col_out(2, pool.Make("t"), pool.Make("out_col"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("out_col", resolved_col_out))
          .set_query(ResolvedTVFScanBuilder()
                         .add_column_list(resolved_col_out)
                         .add_column_index_list(0)
                         .set_tvf(&tvf)
                         .set_signature(tvf_signature)
                         .set_alias("")
                         .add_argument_list(
                             ResolvedFunctionArgumentBuilder()
                                 .set_scan(ResolvedTableScanBuilder()
                                               .set_table(&input_table)
                                               .add_column_list(input_col_geo))
                                 .add_argument_column_list(input_col_geo)
                                 .add_partition_by_list(
                                     ResolvedColumnRefBuilder()
                                         .set_column(input_col_geo)
                                         .set_type(types::GeographyType())
                                         .set_is_correlated(false)))
                         .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("does not support partitioning")));
}

TEST(ValidatorTest, TVFArgumentWithNonOrderableTypeFails) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_TABLE_ARGUMENT_PARTITION_AND_ORDER);
  Validator validator(language_options);
  IdStringPool pool;
  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType output_arg = FunctionArgumentType::RelationWithSchema(
      output_relation, /*extra_relation_input_columns_allowed=*/false);
  TVFRelation input_relation({{"geo", types::GeographyType()}});
  FunctionArgumentType relation_arg = FunctionArgumentType::RelationWithSchema(
      input_relation, /*extra_relation_input_columns_allowed=*/false);
  relation_arg.set_num_occurrences(1);
  FunctionSignature signature(output_arg, {relation_arg}, nullptr);
  FixedOutputSchemaTVF tvf({"tvf_test"}, {signature}, output_relation);

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{TVFInputArgumentType(input_relation)},
      output_relation);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      output_arg, FunctionArgumentTypeList{relation_arg}, nullptr);

  ResolvedColumn input_col_geo(1, pool.Make("t"), pool.Make("geo"),
                               types::GeographyType());
  const SimpleTable input_table("input_table",
                                {{"geo", types::GeographyType()}});
  ResolvedColumn resolved_col_out(2, pool.Make("t"), pool.Make("out_col"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("out_col", resolved_col_out))
          .set_query(
              ResolvedTVFScanBuilder()
                  .add_column_list(resolved_col_out)
                  .add_column_index_list(0)
                  .set_tvf(&tvf)
                  .set_signature(tvf_signature)
                  .set_alias("")
                  .add_argument_list(
                      ResolvedFunctionArgumentBuilder()
                          .set_scan(ResolvedTableScanBuilder()
                                        .set_table(&input_table)
                                        .add_column_list(input_col_geo))
                          .add_argument_column_list(input_col_geo)
                          .add_order_by_list(
                              ResolvedOrderByItemBuilder()
                                  .set_column_ref(
                                      ResolvedColumnRefBuilder()
                                          .set_column(input_col_geo)
                                          .set_type(types::GeographyType())
                                          .set_is_correlated(false))
                                  .set_is_descending(false)))
                  .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());
  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("does not support ordering")));
}

}  // namespace
}  // namespace testing

TEST(ValidatorTest, CompareColumnSet) {
  IdStringPool pool;
  ResolvedColumn x(1, pool.Make("t"), pool.Make("x"), types::Int64Type());
  ResolvedColumn y(2, pool.Make("t"), pool.Make("y"), types::Int64Type());
  const ColumnVisibilityChecker::ColumnSet set_x{x};
  const ColumnVisibilityChecker::ColumnSet set_xy{x, y};
  const ColumnVisibilityChecker::ColumnSet* computed_x = &set_x;
  const ColumnVisibilityChecker::ColumnSet* computed_xy = &set_xy;

  GOOGLESQL_EXPECT_OK(Validator::CompareColumnSet("what", "VISIBLE", {x}, computed_x));
  // A null computed set is the empty set.
  GOOGLESQL_EXPECT_OK(Validator::CompareColumnSet("what", "VISIBLE", {}, nullptr));
  EXPECT_THAT(Validator::CompareColumnSet("what", "VISIBLE", {x}, nullptr),
              ::absl_testing::StatusIs(
                  ::testing::_, ::testing::HasSubstr(
                                    "Only in the validator: [t.x#1]; only in "
                                    "the annotations: []")));
  // The annotations allow more columns than the validator.
  EXPECT_THAT(
      Validator::CompareColumnSet("what", "VISIBLE", {x}, computed_xy),
      ::absl_testing::StatusIs(
          ::testing::_,
          ::testing::AllOf(
              ::testing::HasSubstr("Column visibility comparison failed in "
                                   "what: VISIBLE differs"),
              ::testing::HasSubstr("Only in the validator: []; only in the "
                                   "annotations: [t.y#2]"),
              ::testing::HasSubstr("See Validator::CompareColumnVisibility"))));
  // The validator allows more columns than the annotations.
  EXPECT_THAT(
      Validator::CompareColumnSet("what", "VISIBLE", {x, y}, computed_x),
      ::absl_testing::StatusIs(
          ::testing::_,
          ::testing::HasSubstr("Only in the validator: [t.y#2]; only in "
                               "the annotations: []")));
  // Same size, different columns.
  EXPECT_THAT(Validator::CompareColumnSet("what", "VISIBLE", {y}, computed_x),
              ::absl_testing::StatusIs(
                  ::testing::_, ::testing::HasSubstr(
                                    "Only in the validator: [t.y#2]; only in "
                                    "the annotations: [t.x#1]")));
}

TEST(ValidatorTest, InvalidDynamicGraphGetElementProperty) {
  std::string graph_name = "graph_name";
  const SimplePropertyGraph graph(std::vector<std::string>{graph_name});
  TypeFactory factory;
  const GraphElementType* dynamic_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      /*static_property_types=*/{},
      /*element_tables=*/{}, &dynamic_graph_element_type));

  IdStringPool pool;
  const ResolvedColumn element_col(1, pool.Make("tbl"), pool.Make("x"),
                                   dynamic_graph_element_type);

  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory.get_string());
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations{
      name_property_dcl.get()};
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", std::vector<std::string>{graph_name}, property_declarations);

  // Create a resolved AST where the `property` and `property_name` do not
  // match.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto get_element_prop_expr,
      ResolvedGraphGetElementPropertyBuilder()
          .set_property(name_property_dcl.get())
          .set_property_name(ResolvedLiteralBuilder()
                                 .set_value(Value::String("UnknownProperty"))
                                 .set_type(factory.get_string())
                                 .Build())
          .set_type(name_property_dcl->Type())
          .set_expr(ResolvedColumnRefBuilder()
                        .set_column(element_col)
                        .set_type(dynamic_graph_element_type)
                        .set_is_correlated(false)
                        .Build())
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedExpr(
          /*visible_columns=*/{element_col}, /*visible_parameters=*/{},
          get_element_prop_expr.get()),
      testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr(
              "property_name and property must match case-insensitively")));
}

TEST(ValidatorTest, CaseInsensitiveDynamicGraphGetElementProperty) {
  std::string graph_name = "graph_name";
  const SimplePropertyGraph graph(std::vector<std::string>{graph_name});
  TypeFactory factory;
  const GraphElementType* dynamic_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      /*static_property_types=*/{},
      /*element_tables=*/{}, &dynamic_graph_element_type));

  IdStringPool pool;
  const ResolvedColumn element_col(1, pool.Make("tbl"), pool.Make("x"),
                                   dynamic_graph_element_type);

  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory.get_string());
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations{
      name_property_dcl.get()};
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", std::vector<std::string>{graph_name}, property_declarations);

  // Create a resolved AST where the `property` and `property_name` fields only
  // match case-insensitively.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto get_element_prop_expr,
      ResolvedGraphGetElementPropertyBuilder()
          .set_property(name_property_dcl.get())
          .set_property_name(ResolvedLiteralBuilder()
                                 .set_value(Value::String("nAME"))
                                 .set_type(factory.get_string())
                                 .Build())
          .set_type(name_property_dcl->Type())
          .set_expr(ResolvedColumnRefBuilder()
                        .set_column(element_col)
                        .set_type(dynamic_graph_element_type)
                        .set_is_correlated(false)
                        .Build())
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT);
  Validator validator(language_options);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedExpr(
      /*visible_columns=*/{element_col}, /*visible_parameters=*/{},
      get_element_prop_expr.get()));
}

class ValidatorGraphTest : public ::testing::Test {
 protected:
  ValidatorGraphTest()
      : dynamic_validator_(MakeDynamicLanguageOptions()),
        table_("tbl", {{"id", factory_.get_int64()},
                       {"name", factory_.get_string()}}) {
    name_property_dcl_ = std::make_unique<SimpleGraphPropertyDeclaration>(
        "name", std::vector<std::string>{"graph_name"}, factory_.get_string());

    person_label_ = std::make_unique<SimpleGraphElementLabel>(
        "Person", std::vector<std::string>{"graph_name"},
        absl::flat_hash_set<const GraphPropertyDeclaration*>{
            name_property_dcl_.get()});

    knows_label_ = std::make_unique<SimpleGraphElementLabel>(
        "Knows", std::vector<std::string>{"graph_name"},
        absl::flat_hash_set<const GraphPropertyDeclaration*>{});

    std::vector<std::unique_ptr<const GraphPropertyDefinition>>
        node_property_defs;
    node_property_defs.push_back(
        std::make_unique<SimpleGraphPropertyDefinition>(
            name_property_dcl_.get(), "name"));

    node_table_ = std::make_unique<SimpleGraphNodeTable>(
        "dummy_nodes", std::vector<std::string>{"graph_name"}, &table_,
        std::vector<int>{0},
        absl::flat_hash_set<const GraphElementLabel*>{person_label_.get()},
        std::move(node_property_defs));

    edge_table_ = std::make_unique<SimpleGraphEdgeTable>(
        "dummy_edges", std::vector<std::string>{"graph_name"}, &table_,
        std::vector<int>{0},
        absl::flat_hash_set<const GraphElementLabel*>{knows_label_.get()},
        std::vector<std::unique_ptr<const GraphPropertyDefinition>>{},
        std::make_unique<SimpleGraphNodeTableReference>(
            node_table_.get(), std::vector<int>{0}, std::vector<int>{0}),
        std::make_unique<SimpleGraphNodeTableReference>(
            node_table_.get(), std::vector<int>{0}, std::vector<int>{0}));
  }

  static LanguageOptions MakeDynamicLanguageOptions() {
    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
    return options;
  }

  void SetUp() override {
    GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
        {"graph_name"}, GraphElementType::ElementKind::kNode, {},
        /*element_tables=*/{}, &graph_node_type_));
    GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
        {"graph_name"}, GraphElementType::ElementKind::kEdge, {},
        /*element_tables=*/{}, &graph_edge_type_));
    graph_table_ = std::make_unique<SimpleTable>(
        "graph_table", std::vector<SimpleTable::NameAndType>{
                           {"x", graph_node_type_}, {"y", graph_node_type_}});
    element_col1_ = ResolvedColumn(1, pool_.Make("graph_table"),
                                   pool_.Make("x"), graph_node_type_);
    element_col2_ = ResolvedColumn(2, pool_.Make("graph_table"),
                                   pool_.Make("y"), graph_node_type_);
    updated_element_col1_ = ResolvedColumn(10, pool_.Make("graph_table"),
                                           pool_.Make("x"), graph_node_type_);
    updated_element_col2_ = ResolvedColumn(11, pool_.Make("graph_table"),
                                           pool_.Make("y"), graph_node_type_);
    id_col_ = ResolvedColumn(20, pool_.Make("tbl"), pool_.Make("id"),
                             factory_.get_int64());
    from_col_ = ResolvedColumn(21, pool_.Make("tbl"), pool_.Make("from_id"),
                               factory_.get_int64());
  }

  std::unique_ptr<const ResolvedGraphLabel> MakeDynamicLabel(
      absl::string_view label_name = "Vip") {
    return MakeResolvedGraphLabel(
        /*label=*/nullptr,
        MakeResolvedLiteral(types::StringType(), Value::String(label_name),
                            /*has_explicit_type=*/true));
  }

  std::unique_ptr<const ResolvedGraphLabel> MakeStaticLabel(
      absl::string_view label_name = "Person") {
    return MakeResolvedGraphLabel(
        person_label_.get(),
        MakeResolvedLiteral(types::StringType(), Value::String(label_name),
                            /*has_explicit_type=*/true));
  }

  std::unique_ptr<const ResolvedGraphDMLPropertyItem> MakePropertyItemWithValue(
      absl::string_view val = "Alice") {
    return MakeResolvedGraphDMLPropertyItem(
        "name", name_property_dcl_.get(),
        MakeResolvedLiteral(Value::String(val)));
  }

  std::unique_ptr<const ResolvedGraphDMLPropertyItem>
  MakePropertyItemWithoutValue() {
    return MakeResolvedGraphDMLPropertyItem("name", name_property_dcl_.get(),
                                            /*property_value=*/nullptr);
  }

  std::unique_ptr<const ResolvedGraphDMLPropertyItem>
  MakeDynamicPropertyItemWithoutValue(const std::string& name = "name") {
    return MakeResolvedGraphDMLPropertyItem(name, /*property=*/nullptr,
                                            /*property_value=*/nullptr);
  }

  std::unique_ptr<const ResolvedGraphDMLPropertyItem>
  MakeDynamicPropertyItemWithValue(const std::string& name = "name",
                                   const std::string& val = "Alice") {
    return MakeResolvedGraphDMLPropertyItem(
        name, /*property=*/nullptr, MakeResolvedLiteral(Value::String(val)));
  }

  std::unique_ptr<const ResolvedTableScan> MakeInputScan() {
    return MakeResolvedTableScan({element_col1_, element_col2_},
                                 graph_table_.get(),
                                 /*for_system_time_expr=*/nullptr);
  }

  TypeFactory factory_;
  IdStringPool pool_;
  Validator validator_;
  Validator dynamic_validator_;
  SimpleTable table_;
  std::unique_ptr<SimpleGraphNodeTable> node_table_;
  std::unique_ptr<SimpleGraphEdgeTable> edge_table_;
  const GraphElementType* graph_node_type_;
  const GraphElementType* graph_edge_type_;
  std::unique_ptr<SimpleGraphPropertyDeclaration> name_property_dcl_;
  std::unique_ptr<SimpleGraphElementLabel> person_label_;
  std::unique_ptr<SimpleGraphElementLabel> knows_label_;
  std::unique_ptr<SimpleTable> graph_table_;
  ResolvedColumn element_col1_;
  ResolvedColumn element_col2_;
  ResolvedColumn updated_element_col1_;
  ResolvedColumn updated_element_col2_;
  ResolvedColumn id_col_;
  ResolvedColumn from_col_;
};

TEST_F(ValidatorGraphTest, ValidGraphInsertScan) {
  std::string graph_name = "graph_name";
  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory_.get_string());
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations{
      name_property_dcl.get()};

  // Node to insert
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", std::vector<std::string>{graph_name}, property_declarations);
  const GraphElementType* graph_node_type;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      {{"name", factory_.get_string()}},
      /*element_tables=*/{}, &graph_node_type));
  ResolvedColumn node_col(/*column_id=*/1, pool_.Make("inserted"),
                          pool_.Make("node"), graph_node_type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto insert_node_element,
      ResolvedGraphInsertElementBuilder()
          .set_type(graph_node_type)
          .set_element_table(node_table_.get())
          .add_label_list(ResolvedGraphLabelBuilder()
                              .set_label(person_label.get())
                              .Build()
                              .value())
          .add_property_list(
              ResolvedGraphDMLPropertyItemBuilder()
                  .set_property_name("name")
                  .set_property(name_property_dcl.get())
                  .set_property_value(ResolvedLiteralBuilder()
                                          .set_value(Value::String("Alice"))
                                          .set_type(factory_.get_string())
                                          .Build()
                                          .value())
                  .Build()
                  .value())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node,
                       ResolvedComputedColumnBuilder()
                           .set_column(node_col)
                           .set_expr(std::move(insert_node_element))
                           .Build());

  // Edge to insert
  auto knows_label = std::make_unique<SimpleGraphElementLabel>(
      "Knows", std::vector<std::string>{graph_name},
      absl::flat_hash_set<const GraphPropertyDeclaration*>{});
  const GraphElementType* graph_edge_type;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kEdge, {},
      /*element_tables=*/{}, &graph_edge_type));
  ResolvedColumn edge_col(/*column_id=*/2, pool_.Make("inserted"),
                          pool_.Make("edge"), graph_edge_type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_edge_type)
                           .set_element_table(edge_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(knows_label.get())
                                               .Build()
                                               .value())
                           .set_source_node(ResolvedColumnRefBuilder()
                                                .set_column(node_col)
                                                .set_type(node_col.type())
                                                .set_is_correlated(false)
                                                .Build()
                                                .value())
                           .set_dest_node(ResolvedColumnRefBuilder()
                                              .set_column(node_col)
                                              .set_type(node_col.type())
                                              .set_is_correlated(false)
                                              .Build()
                                              .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge,
                       ResolvedComputedColumnBuilder()
                           .set_column(edge_col)
                           .set_expr(std::move(insert_edge_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(MakeResolvedSingleRowScan())
                           .add_insert_node_list(std::move(insert_node))
                           .add_insert_edge_list(std::move(insert_edge))
                           .add_path_element_list(node_col)
                           .add_path_element_list(edge_col)
                           .add_path_element_list(node_col)
                           .add_column_list(node_col)
                           .add_column_list(edge_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("node", node_col));
  output_columns.push_back(MakeResolvedOutputColumn("edge", edge_col));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto generalized_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(generalized_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertScanNodeReferencingEdge) {
  ResolvedColumn edge_col(/*column_id=*/1, pool_.Make("inserted"),
                          pool_.Make("edge"), factory_.get_int64());

  // Node trying to reference edge (which is defined later)
  ResolvedColumn node_col(/*column_id=*/2, pool_.Make("inserted"),
                          pool_.Make("node"), factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node,
                       ResolvedComputedColumnBuilder()
                           .set_column(node_col)
                           .set_expr(ResolvedColumnRefBuilder()
                                         .set_column(edge_col)
                                         .set_type(edge_col.type())
                                         .set_is_correlated(false))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge,
                       ResolvedComputedColumnBuilder()
                           .set_column(edge_col)
                           .set_expr(ResolvedLiteralBuilder()
                                         .set_value(Value::Int64(1))
                                         .set_type(factory_.get_int64()))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(MakeResolvedSingleRowScan())
                           .add_insert_node_list(std::move(insert_node))
                           .add_insert_edge_list(std::move(insert_edge))
                           .add_column_list(node_col)
                           .add_column_list(edge_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("node", node_col));
  output_columns.push_back(MakeResolvedOutputColumn("edge", edge_col));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto generalized_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(generalized_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("Incorrect reference to column inserted.edge#1")));
}

TEST_F(ValidatorGraphTest, ValidFinishScanWithoutSideEffects) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());

  // Input scan with no side effects.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .Build());

  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      /*output_schema=*/nullptr, std::move(finish_scan));

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidFinishScanWithGraphInsertSideEffects) {
  ResolvedColumn node_col(/*column_id=*/1, pool_.Make("inserted"),
                          pool_.Make("node"), graph_node_type_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node,
                       ResolvedComputedColumnBuilder()
                           .set_column(node_col)
                           .set_expr(std::move(insert_node_element))
                           .Build());

  // Input scan with side effects.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(MakeResolvedSingleRowScan())
                           .add_insert_node_list(std::move(insert_node))
                           .add_column_list(node_col)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .Build());

  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      /*output_schema=*/nullptr, std::move(finish_scan));

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidFinishScanWithGraphDeleteSideEffects) {
  // graph_delete_scan is a valid `ResolvedGraphDeleteScan`.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(updated_element_col1_)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFinishScan> finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(graph_delete_scan))
                           .Build());

  std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt =
      MakeResolvedGeneralizedQueryStmt(
          /*output_schema=*/nullptr, std::move(finish_scan));

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidFinishScanNonEmptyColumns) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .Build());

  // Invalid: column_list of ResolvedFinishScan is not empty.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .add_column_list(id_col)
                           .Build());

  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      /*output_schema=*/nullptr, std::move(finish_scan));

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("ResolvedFinishScan must not emit columns")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDMLPropertyItemTypeMismatch) {
  std::string graph_name = "graph_name";
  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory_.get_string());

  // Invalid: property_value is Int64 but catalog property expects String.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto property_item,
      ResolvedGraphDMLPropertyItemBuilder()
          .set_property_name("name")
          .set_property(name_property_dcl.get())
          .set_property_value(ResolvedLiteralBuilder()
                                  .set_value(Value::Int64(123))
                                  .set_type(factory_.get_int64())
                                  .Build()
                                  .value())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .add_property_list(std::move(property_item))
                           .Build());

  EXPECT_THAT(
      validator_.ValidateStandaloneResolvedExpr(insert_node_element.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("Property type mismatch for name")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDMLPropertyItemNameMismatch) {
  std::string graph_name = "graph_name";
  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", std::vector<std::string>{graph_name}, factory_.get_string());

  // Invalid: property_name is "age" but catalog property expects "name".
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto property_item,
      ResolvedGraphDMLPropertyItemBuilder()
          .set_property_name("age")
          .set_property(name_property_dcl.get())
          .set_property_value(ResolvedLiteralBuilder()
                                  .set_value(Value::String("Alice"))
                                  .set_type(factory_.get_string())
                                  .Build()
                                  .value())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .add_property_list(std::move(property_item))
                           .Build());

  EXPECT_THAT(
      validator_.ValidateStandaloneResolvedExpr(insert_node_element.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("Property name mismatch")));
}

TEST_F(ValidatorGraphTest, InvalidEdgeEndpointPropertyNotFound) {
  std::string graph_name = "graph_name";

  // Endpoint has property "age" which is NOT in the expected node table
  // (dummy_nodes).
  const GraphElementType* graph_node_type_with_age;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      {{"age", factory_.get_int64()}},
      /*element_tables=*/{}, &graph_node_type_with_age));

  // Create a synthetic table that has this node type column.
  SimpleTable table_with_age_node("synthetic_tbl",
                                  {{"node_col", graph_node_type_with_age}});
  ResolvedColumn node_col_with_age(100, pool_.Make("synthetic_tbl"),
                                   pool_.Make("node_col"),
                                   graph_node_type_with_age);

  // Input scan that produces this column.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_with_age_node)
                                            .add_column_list(node_col_with_age)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto insert_edge_element,
      ResolvedGraphInsertElementBuilder()
          .set_type(graph_edge_type_)  // Edge type
          .set_element_table(edge_table_.get())
          .add_label_list(ResolvedGraphLabelBuilder()
                              .set_label(knows_label_.get())
                              .Build()
                              .value())
          .set_source_node(ResolvedColumnRefBuilder()
                               .set_column(node_col_with_age)
                               .set_type(node_col_with_age.type())
                               .set_is_correlated(false)
                               .Build()
                               .value())
          .set_dest_node(ResolvedColumnRefBuilder()
                             .set_column(node_col_with_age)
                             .set_type(node_col_with_age.type())
                             .set_is_correlated(false)
                             .Build()
                             .value())
          .Build());

  ResolvedColumn edge_col(102, pool_.Make("inserted"), pool_.Make("edge"),
                          graph_edge_type_);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge,
                       ResolvedComputedColumnBuilder()
                           .set_column(edge_col)
                           .set_expr(std::move(insert_edge_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .add_insert_edge_list(std::move(insert_edge))
                           .add_column_list(edge_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("edge", edge_col));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto generalized_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(generalized_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  testing::HasSubstr("Property age from source_node not found "
                                     "in expected source table dummy_nodes")));
}

TEST_F(ValidatorGraphTest, InvalidEdgeEndpointPropertyTypeMismatch) {
  std::string graph_name = "graph_name";

  // Endpoint has property "name" as Int64, but expected node table
  // (dummy_nodes) has it as String.
  const GraphElementType* graph_node_type_with_int_name;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      {{"name", factory_.get_int64()}},
      /*element_tables=*/{}, &graph_node_type_with_int_name));

  // Create a synthetic table that has this node type column.
  SimpleTable table_with_int_name(
      "synthetic_tbl", {{"node_col", graph_node_type_with_int_name}});
  ResolvedColumn node_col_wrong_type(101, pool_.Make("synthetic_tbl"),
                                     pool_.Make("node_col"),
                                     graph_node_type_with_int_name);

  // Input scan that produces this column.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan,
                       ResolvedTableScanBuilder()
                           .set_table(&table_with_int_name)
                           .add_column_list(node_col_wrong_type)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto insert_edge_element,
      ResolvedGraphInsertElementBuilder()
          .set_type(graph_edge_type_)  // Edge type
          .set_element_table(edge_table_.get())
          .add_label_list(ResolvedGraphLabelBuilder()
                              .set_label(knows_label_.get())
                              .Build()
                              .value())
          .set_source_node(ResolvedColumnRefBuilder()
                               .set_column(node_col_wrong_type)
                               .set_type(node_col_wrong_type.type())
                               .set_is_correlated(false)
                               .Build()
                               .value())
          .set_dest_node(ResolvedColumnRefBuilder()
                             .set_column(node_col_wrong_type)
                             .set_type(node_col_wrong_type.type())
                             .set_is_correlated(false)
                             .Build()
                             .value())
          .Build());

  ResolvedColumn edge_col(102, pool_.Make("inserted"), pool_.Make("edge"),
                          graph_edge_type_);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge,
                       ResolvedComputedColumnBuilder()
                           .set_column(edge_col)
                           .set_expr(std::move(insert_edge_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .add_insert_edge_list(std::move(insert_edge))
                           .add_column_list(edge_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("edge", edge_col));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto generalized_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(generalized_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  testing::HasSubstr("Property type mismatch for name in "
                                     "expected source table dummy_nodes")));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertElementEmptyLabels) {
  // Invalid: empty label list
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .Build());

  EXPECT_THAT(
      validator_.ValidateStandaloneResolvedExpr(insert_node_element.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("INSERT element must have at least one label")));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertElementNodeWithSrcNode) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());

  // Invalid: setting `src_node` field for node element.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .set_source_node(ResolvedColumnRefBuilder()
                                                .set_column(id_col)
                                                .set_type(id_col.type())
                                                .set_is_correlated(false)
                                                .Build()
                                                .value())
                           .Build());

  EXPECT_THAT(
      validator_.ValidateStandaloneResolvedExpr(insert_node_element.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr(
              "source_node and dest_node must only be set for edge elements")));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertScanInvalidPathElement) {
  ResolvedColumn node_col(2, pool_.Make("inserted"), pool_.Make("node"),
                          graph_node_type_);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node,
                       ResolvedComputedColumnBuilder()
                           .set_column(node_col)
                           .set_expr(std::move(insert_node_element))
                           .Build());

  // Invalid column not in scope
  ResolvedColumn invalid_col(99, pool_.Make("invalid"), pool_.Make("col"),
                             factory_.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(MakeResolvedSingleRowScan())
                           .add_insert_node_list(std::move(insert_node))
                           .add_path_element_list(invalid_col)  // Invalid
                           .add_column_list(node_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("node", node_col));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("Incorrect reference to column invalid.col#99")));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertScanPathElementNotGraphElement) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .add_path_element_list(id_col)  // Invalid type
                           .add_column_list(id_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("id", id_col));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          testing::HasSubstr("Path element must be a graph node or edge")));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertScanConsecutiveEdges) {
  ResolvedColumn node_col(2, pool_.Make("inserted"), pool_.Make("node"),
                          graph_node_type_);
  ResolvedColumn edge_col(3, pool_.Make("inserted"), pool_.Make("edge"),
                          graph_edge_type_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node,
                       ResolvedComputedColumnBuilder()
                           .set_column(node_col)
                           .set_expr(std::move(insert_node_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_edge_type_)
                           .set_element_table(edge_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(knows_label_.get())
                                               .Build()
                                               .value())
                           .set_source_node(ResolvedColumnRefBuilder()
                                                .set_column(node_col)
                                                .set_type(node_col.type())
                                                .set_is_correlated(false)
                                                .Build()
                                                .value())
                           .set_dest_node(ResolvedColumnRefBuilder()
                                              .set_column(node_col)
                                              .set_type(node_col.type())
                                              .set_is_correlated(false)
                                              .Build()
                                              .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge,
                       ResolvedComputedColumnBuilder()
                           .set_column(edge_col)
                           .set_expr(std::move(insert_edge_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto insert_scan,
      ResolvedGraphInsertScanBuilder()
          .set_input_scan(MakeResolvedSingleRowScan())
          .add_insert_node_list(std::move(insert_node))
          .add_insert_edge_list(std::move(insert_edge))
          .add_path_element_list(node_col)
          .add_path_element_list(edge_col)
          .add_path_element_list(edge_col)  // Consecutive edges
          .add_path_element_list(node_col)
          .add_column_list(node_col)
          .add_column_list(edge_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("node", node_col));
  output_columns.push_back(MakeResolvedOutputColumn("edge", edge_col));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  testing::HasSubstr("found consecutive edges")));
}

TEST_F(ValidatorGraphTest, InvalidGraphInsertScanStartsOrEndsWithEdge) {
  ResolvedColumn node_col(2, pool_.Make("inserted"), pool_.Make("node"),
                          graph_node_type_);
  ResolvedColumn edge_col(3, pool_.Make("inserted"), pool_.Make("edge"),
                          graph_edge_type_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_node_type_)
                           .set_element_table(node_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(person_label_.get())
                                               .Build()
                                               .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_node,
                       ResolvedComputedColumnBuilder()
                           .set_column(node_col)
                           .set_expr(std::move(insert_node_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge_element,
                       ResolvedGraphInsertElementBuilder()
                           .set_type(graph_edge_type_)
                           .set_element_table(edge_table_.get())
                           .add_label_list(ResolvedGraphLabelBuilder()
                                               .set_label(knows_label_.get())
                                               .Build()
                                               .value())
                           .set_source_node(ResolvedColumnRefBuilder()
                                                .set_column(node_col)
                                                .set_type(node_col.type())
                                                .set_is_correlated(false)
                                                .Build()
                                                .value())
                           .set_dest_node(ResolvedColumnRefBuilder()
                                              .set_column(node_col)
                                              .set_type(node_col.type())
                                              .set_is_correlated(false)
                                              .Build()
                                              .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_edge,
                       ResolvedComputedColumnBuilder()
                           .set_column(edge_col)
                           .set_expr(std::move(insert_edge_element))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedGraphInsertScanBuilder()
                           .set_input_scan(MakeResolvedSingleRowScan())
                           .add_insert_node_list(std::move(insert_node))
                           .add_insert_edge_list(std::move(insert_edge))
                           .add_path_element_list(edge_col)  // Starts with edge
                           .add_path_element_list(node_col)
                           .add_path_element_list(edge_col)  // Ends with edge
                           .add_column_list(node_col)
                           .add_column_list(edge_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("node", node_col));
  output_columns.push_back(MakeResolvedOutputColumn("edge", edge_col));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  testing::HasSubstr(
                      "Path element list must start and end with nodes")));
}

TEST(ValidatorTest, ValidInsertScanInputAndReturningMode) {
  IdStringPool pool;
  TypeFactory type_factory;
  Validator validator;

  // Target table columns.
  ResolvedColumn target_col(1, pool.Make("t2"), pool.Make("c1"),
                            type_factory.get_int64());

  // Input query columns.
  ResolvedColumn query_col(2, pool.Make("input"), pool.Make("c2"),
                           type_factory.get_int64());

  const SimpleTable t2{"t2", {{"c1", type_factory.get_int64()}}};

  // Create the input query (just a SingleRowScan with query_col computed).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto input_scan,
      ResolvedProjectScanBuilder()
          .add_column_list(query_col)
          .add_expr_list(ResolvedComputedColumnBuilder()
                             .set_column(query_col)
                             .set_expr(ResolvedLiteralBuilder()
                                           .set_value(Value::Int64(42))
                                           .set_type(type_factory.get_int64())
                                           .Build()
                                           .value())
                             .Build()
                             .value())
          .set_input_scan(ResolvedSingleRowScanBuilder())
          .Build());

  // Create insert statement mapping query_col to target_col.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto insert_stmt,
      ResolvedInsertStmtBuilder()
          .set_table_scan(ResolvedTableScanBuilder()
                              .add_column_list(target_col)
                              .set_table(&t2)
                              .Build()
                              .value())
          .add_insert_column_list(target_col)
          .set_query(std::move(input_scan))
          .add_query_output_column_list(query_col)
          .set_returning(
              ResolvedReturningClauseBuilder()
                  .set_action_column(nullptr)
                  .add_output_column_list(ResolvedOutputColumnBuilder()
                                              .set_column(target_col)
                                              .set_name("inserted_val")
                                              .Build()
                                              .value())
                  // Test that returning can access query_output_column_list
                  // columns.
                  .add_output_column_list(ResolvedOutputColumnBuilder()
                                              .set_column(query_col)
                                              .set_name("input_val")
                                              .Build()
                                              .value())
                  .Build()
                  .value())
          .Build());

  // Create ResolvedInsertScan.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(insert_stmt))
                           // Scan returns both the passed-through input column
                           // and the target column.
                           .add_column_list(query_col)
                           .add_column_list(target_col)
                           .Build());

  // GeneralizedQueryStmt to hold the scan.
  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("c2", query_col));
  output_columns.push_back(MakeResolvedOutputColumn("inserted_c1", target_col));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, ValidInsertScanNoReturning) {
  IdStringPool pool;
  TypeFactory type_factory;
  Validator validator;

  ResolvedColumn target_col(1, pool.Make("t2"), pool.Make("c1"),
                            type_factory.get_int64());
  ResolvedColumn query_col(2, pool.Make("input"), pool.Make("c2"),
                           type_factory.get_int64());

  const SimpleTable t2{"t2", {{"c1", type_factory.get_int64()}}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto input_scan,
      ResolvedProjectScanBuilder()
          .add_column_list(query_col)
          .add_expr_list(ResolvedComputedColumnBuilder()
                             .set_column(query_col)
                             .set_expr(ResolvedLiteralBuilder()
                                           .set_value(Value::Int64(42))
                                           .set_type(type_factory.get_int64())
                                           .Build()
                                           .value())
                             .Build()
                             .value())
          .set_input_scan(ResolvedSingleRowScanBuilder())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_stmt,
                       ResolvedInsertStmtBuilder()
                           .set_table_scan(ResolvedTableScanBuilder()
                                               .add_column_list(target_col)
                                               .set_table(&t2)
                                               .Build()
                                               .value())
                           .add_insert_column_list(target_col)
                           .set_query(std::move(input_scan))
                           .add_query_output_column_list(query_col)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(insert_stmt))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, ResolvedGeneralizedQueryStmtBuilder()
                                            .set_output_schema(nullptr)
                                            .set_query(std::move(insert_scan))
                                            .Build());

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, ValidInsertScanWithReturning) {
  IdStringPool pool;
  TypeFactory type_factory;
  Validator validator;

  ResolvedColumn target_col(1, pool.Make("t2"), pool.Make("c1"),
                            type_factory.get_int64());
  ResolvedColumn query_col(2, pool.Make("input"), pool.Make("c2"),
                           type_factory.get_int64());

  const SimpleTable t2{"t2", {{"c1", type_factory.get_int64()}}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto input_scan,
      ResolvedProjectScanBuilder()
          .add_column_list(query_col)
          .add_expr_list(ResolvedComputedColumnBuilder()
                             .set_column(query_col)
                             .set_expr(ResolvedLiteralBuilder()
                                           .set_value(Value::Int64(42))
                                           .set_type(type_factory.get_int64())
                                           .Build()
                                           .value())
                             .Build()
                             .value())
          .set_input_scan(ResolvedSingleRowScanBuilder())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_stmt,
                       ResolvedInsertStmtBuilder()
                           .set_table_scan(ResolvedTableScanBuilder()
                                               .add_column_list(target_col)
                                               .set_table(&t2)
                                               .Build()
                                               .value())
                           .add_insert_column_list(target_col)
                           .set_query(std::move(input_scan))
                           .add_query_output_column_list(query_col)
                           .set_returning(ResolvedReturningClauseBuilder()
                                              .set_action_column(nullptr)
                                              .add_output_column_list(
                                                  ResolvedOutputColumnBuilder()
                                                      .set_column(target_col)
                                                      .set_name("inserted_val")
                                                      .Build()
                                                      .value())
                                              .Build()
                                              .value())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(insert_stmt))
                           .add_column_list(target_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(
      MakeResolvedOutputColumn("inserted_val", target_col));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, ValidUpdateStmtReturningAccessFromScanColumns) {
  IdStringPool pool;
  TypeFactory type_factory;
  Validator validator;

  ResolvedColumn target_col(1, pool.Make("t2"), pool.Make("c1"),
                            type_factory.get_int64());
  ResolvedColumn from_col(2, pool.Make("from_tbl"), pool.Make("c2"),
                          type_factory.get_int64());

  const SimpleTable t2{"t2", {{"c1", type_factory.get_int64()}}};
  const SimpleTable from_tbl{"from_tbl", {{"c2", type_factory.get_int64()}}};

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(target_col.type(), target_col,
                            /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(42))), nullptr, {},
      {}, {}, {});
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(MakeResolvedOutputColumn("c1", target_col));
  returning_output_cols.push_back(MakeResolvedOutputColumn("c2", from_col));

  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .add_column_list(target_col)
                                            .add_column_index_list(0)
                                            .set_table(&t2)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto from_scan, ResolvedTableScanBuilder()
                                           .add_column_list(from_col)
                                           .set_table(&from_tbl)
                                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_from_scan(std::move(from_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(update_stmt.get()));
}

TEST(ValidatorTest, ValidInsertScanInputWithoutReturning) {
  IdStringPool pool;
  TypeFactory type_factory;
  Validator validator;

  ResolvedColumn target_col(1, pool.Make("t2"), pool.Make("c1"),
                            type_factory.get_int64());
  ResolvedColumn query_col(2, pool.Make("input"), pool.Make("c2"),
                           type_factory.get_int64());

  const SimpleTable t2{"t2", {{"c1", type_factory.get_int64()}}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto input_scan,
      ResolvedProjectScanBuilder()
          .add_column_list(query_col)
          .add_expr_list(ResolvedComputedColumnBuilder()
                             .set_column(query_col)
                             .set_expr(ResolvedLiteralBuilder()
                                           .set_value(Value::Int64(42))
                                           .set_type(type_factory.get_int64())
                                           .Build()
                                           .value())
                             .Build()
                             .value())
          .set_input_scan(ResolvedSingleRowScanBuilder())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_stmt,
                       ResolvedInsertStmtBuilder()
                           .set_table_scan(ResolvedTableScanBuilder()
                                               .add_column_list(target_col)
                                               .set_table(&t2)
                                               .Build()
                                               .value())
                           .add_insert_column_list(target_col)
                           .set_query(std::move(input_scan))
                           .add_query_output_column_list(query_col)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(insert_stmt))
                           .add_column_list(query_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("c2", query_col));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST(ValidatorTest, InvalidInsertScanWithIncorrectColumn) {
  IdStringPool pool;
  TypeFactory type_factory;
  Validator validator;

  ResolvedColumn target_col(1, pool.Make("t2"), pool.Make("c1"),
                            type_factory.get_int64());
  ResolvedColumn query_col(2, pool.Make("input"), pool.Make("c2"),
                           type_factory.get_int64());

  const SimpleTable t2{"t2", {{"c1", type_factory.get_int64()}}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto input_scan,
      ResolvedProjectScanBuilder()
          .add_column_list(query_col)
          .add_expr_list(ResolvedComputedColumnBuilder()
                             .set_column(query_col)
                             .set_expr(ResolvedLiteralBuilder()
                                           .set_value(Value::Int64(42))
                                           .set_type(type_factory.get_int64())
                                           .Build()
                                           .value())
                             .Build()
                             .value())
          .set_input_scan(ResolvedSingleRowScanBuilder())
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_stmt,
                       ResolvedInsertStmtBuilder()
                           .set_table_scan(ResolvedTableScanBuilder()
                                               .add_column_list(target_col)
                                               .set_table(&t2)
                                               .Build()
                                               .value())
                           .add_insert_column_list(target_col)
                           .set_query(std::move(input_scan))
                           .add_query_output_column_list(query_col)
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto insert_scan,
                       ResolvedInsertScanBuilder()
                           .set_insert_stmt(std::move(insert_stmt))
                           .add_column_list(target_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_columns;
  output_columns.push_back(MakeResolvedOutputColumn("c1", target_col));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(std::move(output_columns),
                                                      /*is_value_table=*/false))
          .set_query(std::move(insert_scan))
          .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal));
}

TEST_F(ValidatorGraphTest, ValidUpdateScan) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  ResolvedColumn from_col(2, pool_.Make("tbl"), pool_.Make("from_id"),
                          factory_.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});

  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));

  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_ERROR)
          .add_column_list(id_col)
          .add_column_list(from_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidUpdateScanWrongColumn) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  ResolvedColumn from_col(2, pool_.Make("tbl"), pool_.Make("from_id"),
                          factory_.get_int64());
  ResolvedColumn wrong_col(3, pool_.Make("tbl"), pool_.Make("wrong"),
                           factory_.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});

  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));

  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_ERROR)
          .add_column_list(wrong_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("Incorrect reference to column")));
}

TEST_F(ValidatorGraphTest, InvalidUpdateScanCollisionAction) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  ResolvedColumn from_col(2, pool_.Make("tbl"), pool_.Make("from_id"),
                          factory_.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});

  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));

  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_PICK_ONE)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("COLLISION_ACTION_PICK_ONE is not allowed when "
                               "output_mode is OUTPUT_UPDATED_ROWS")));
}

TEST_F(ValidatorGraphTest, InvalidUpdateScanMissingOutputMode) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .Build());

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_MODE_UNSPECIFIED)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_ERROR)
          .add_column_list(id_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal,
                                          ::testing::HasSubstr("output_mode")));
}

TEST_F(ValidatorGraphTest, InvalidUpdateScanMissingCollisionAction) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .Build());

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_UNSPECIFIED)
          .add_column_list(id_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("update_collision_action_type")));
}

TEST_F(ValidatorGraphTest, InvalidUpdateScanMissingTableScan) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .Build());

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_ERROR)
          .add_column_list(id_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal,
                                          ::testing::HasSubstr("table_scan")));
}

TEST_F(ValidatorGraphTest, InvalidUpdateScanMissingFromScan) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_ERROR)
          .add_column_list(id_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal,
                                          ::testing::HasSubstr("from_scan")));
}

TEST_F(ValidatorGraphTest, ValidUpdateScanMissingReturning) {
  ResolvedColumn id_col(1, pool_.Make("tbl"), pool_.Make("id"),
                        factory_.get_int64());
  ResolvedColumn from_col(2, pool_.Make("tbl"), pool_.Make("from_id"),
                          factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col)
                                            .add_column_index_list(1)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col)
                                            .Build());

  auto update_item = MakeResolvedUpdateItem(
      MakeResolvedColumnRef(id_col.type(), id_col, /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Value::Int64(5))), nullptr, {},
      {}, {}, {});
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .set_from_scan(std::move(input_scan))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_scan,
      ResolvedUpdateScanBuilder()
          .set_update_stmt(std::move(update_stmt))
          .set_output_mode(ResolvedUpdateScanEnums::OUTPUT_UPDATED_ROWS)
          .set_update_collision_action_type(
              ResolvedUpdateScanEnums::COLLISION_ACTION_ERROR)
          .add_column_list(from_col)
          .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : update_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(update_scan));
  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidDeleteScan) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col_)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col_)
                                            .add_column_index_list(1)
                                            .Build());

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col_);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), /*action_column=*/nullptr,
      /*expr_list=*/{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_using_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_scan,
                       ResolvedDeleteScanBuilder()
                           .set_delete_stmt(std::move(delete_stmt))
                           .add_column_list(id_col_)
                           .add_column_list(from_col_)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : delete_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(delete_scan));

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidDeleteScanOutsideGeneralizedQueryStmt) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col_)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col_)
                                            .add_column_index_list(1)
                                            .Build());

  auto returning_clause = MakeResolvedReturningClause(
      /*output_column_list=*/{}, /*action_column=*/nullptr, /*expr_list=*/{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_using_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_scan,
                       ResolvedDeleteScanBuilder()
                           .set_delete_stmt(std::move(delete_stmt))
                           .add_column_list(from_col_)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : delete_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt =
      MakeResolvedQueryStmt(std::move(query_output_columns),
                            /*is_value_table=*/false, std::move(delete_scan));

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("in_generalized_query_stmt_")));
}

TEST_F(ValidatorGraphTest,
       InvalidDeleteScanColumnNotVisibleInInputOrReturning) {
  ResolvedColumn wrong_col(3, pool_.Make("tbl"), pool_.Make("wrong"),
                           factory_.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col_)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col_)
                                            .add_column_index_list(1)
                                            .Build());

  auto returning_output_col = MakeResolvedOutputColumn("id", id_col_);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), /*action_column=*/nullptr,
      /*expr_list=*/{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_using_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_scan,
                       ResolvedDeleteScanBuilder()
                           .set_delete_stmt(std::move(delete_stmt))
                           .add_column_list(wrong_col)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : delete_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(delete_scan));

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("not visible in scan node")));
}

TEST_F(ValidatorGraphTest, InvalidDeleteScanMissingTableScan) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col_)
                                            .Build());

  auto returning_clause = MakeResolvedReturningClause(
      /*output_column_list=*/{}, /*action_column=*/nullptr, /*expr_list=*/{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_using_scan(std::move(input_scan))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_scan,
                       ResolvedDeleteScanBuilder()
                           .set_delete_stmt(std::move(delete_stmt))
                           .add_column_list(id_col_)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : delete_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(delete_scan));
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal,
                                          ::testing::HasSubstr("table_scan")));
}

TEST_F(ValidatorGraphTest, InvalidDeleteScanMissingUsingScan) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col_)
                                            .add_column_index_list(1)
                                            .Build());

  auto returning_clause = MakeResolvedReturningClause(
      /*output_column_list=*/{}, /*action_column=*/nullptr, /*expr_list=*/{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_scan,
                       ResolvedDeleteScanBuilder()
                           .set_delete_stmt(std::move(delete_stmt))
                           .add_column_list(id_col_)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : delete_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(delete_scan));
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal,
                                          ::testing::HasSubstr("using_scan")));
}

TEST_F(ValidatorGraphTest, ValidDeleteScanMissingReturning) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(id_col_)
                                            .add_column_index_list(1)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input_scan, ResolvedTableScanBuilder()
                                            .set_table(&table_)
                                            .add_column_list(from_col_)
                                            .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_using_scan(std::move(input_scan))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_scan,
                       ResolvedDeleteScanBuilder()
                           .set_delete_stmt(std::move(delete_stmt))
                           .add_column_list(from_col_)
                           .Build());

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> query_output_columns;
  for (const auto& col : delete_scan->column_list()) {
    query_output_columns.push_back(
        MakeResolvedOutputColumn(col.name_id().ToString(), col));
  }
  auto query_stmt = MakeResolvedGeneralizedQueryStmt(
      MakeResolvedOutputSchema(std::move(query_output_columns),
                               /*is_value_table=*/false),
      std::move(delete_scan));
  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

// Builds a valid CREATE PROPERTY GRAPH TYPE resolved statement describing:
//   NODE TYPES(Person)  -- default label Person exposing properties name, age
//   EDGE TYPES(Knows FROM Person TO Person)  -- label Knows exposing since
// Element type, label and property names are intentionally spelled with mixed
// case between their declarations and the references to them (e.g. node type
// `Person` referenced as label `person`, property `age` referenced as `AGE`) to
// exercise the case-insensitive validation of graph identifiers.
// `source_node_type`/`dest_node_type` of the edge can be overridden to inject
// an invalid (undefined) endpoint for negative tests.
static std::unique_ptr<ResolvedCreatePropertyGraphTypeStmt>
MakeCreatePropertyGraphTypeStmt(absl::string_view edge_source_node_type,
                                absl::string_view edge_dest_node_type) {
  std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>
      property_declarations;
  property_declarations.push_back(
      MakeResolvedGraphPropertyDeclaration("name", types::StringType()));
  property_declarations.push_back(
      MakeResolvedGraphPropertyDeclaration("age", types::Int64Type()));
  property_declarations.push_back(
      MakeResolvedGraphPropertyDeclaration("since", types::DateType()));

  std::vector<std::unique_ptr<const ResolvedGraphElementLabel>> labels;
  labels.push_back(MakeResolvedGraphElementLabel(
      "Person", /*property_declaration_name_list=*/{"Name", "AGE"},
      /*options_list=*/{}));
  labels.push_back(MakeResolvedGraphElementLabel(
      "Knows", /*property_declaration_name_list=*/{"Since"},
      /*options_list=*/{}));

  std::vector<std::unique_ptr<const ResolvedGraphElementType>> node_types;
  node_types.push_back(MakeResolvedGraphElementType(
      "Person", /*label_name_list=*/{"person"}, /*source_node_type=*/"",
      /*dest_node_type=*/""));

  std::vector<std::unique_ptr<const ResolvedGraphElementType>> edge_types;
  edge_types.push_back(
      MakeResolvedGraphElementType("Knows", /*label_name_list=*/{"KNOWS"},
                                   edge_source_node_type, edge_dest_node_type));

  return MakeResolvedCreatePropertyGraphTypeStmt(
      /*name_path=*/{"mygraphtype"},
      ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
      ResolvedCreateStatement::CREATE_DEFAULT, std::move(node_types),
      std::move(edge_types), std::move(labels),
      std::move(property_declarations), /*option_list=*/{});
}

TEST_F(ValidatorGraphTest, ValidCreatePropertyGraphTypeStmt) {
  // FROM/TO reference the `Person` node type in mixed case to confirm endpoint
  // resolution is case-insensitive.
  std::unique_ptr<ResolvedCreatePropertyGraphTypeStmt> stmt =
      MakeCreatePropertyGraphTypeStmt(/*edge_source_node_type=*/"person",
                                      /*edge_dest_node_type=*/"PERSON");
  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(stmt.get()));
}

TEST_F(ValidatorGraphTest,
       CreatePropertyGraphTypeStmtEdgeReferencesUndefinedNode) {
  // The edge type's FROM references a node type that is not declared.
  std::unique_ptr<ResolvedCreatePropertyGraphTypeStmt> stmt =
      MakeCreatePropertyGraphTypeStmt(/*edge_source_node_type=*/"Ghost",
                                      /*edge_dest_node_type=*/"Person");
  EXPECT_THAT(validator_.ValidateResolvedStatement(stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  testing::HasSubstr("references undefined node type")));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanSet) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue());

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanSetStaticLabel) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  // Setting an existing static label is allowed and idempotent.
  label_list.push_back(MakeStaticLabel("Person"));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanRemove) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakeDynamicPropertyItemWithoutValue());

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel("Vip"));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REMOVE,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_REMOVE,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(dynamic_validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanReplace) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REPLACE,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanReplaceEmptyPropertyList) {
  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REPLACE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanCombined) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue());

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col2_,
                            /*is_correlated=*/false),
      updated_element_col2_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {updated_element_col1_, updated_element_col2_}, MakeInputScan(),
      std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt,
                       ResolvedQueryStmtBuilder()
                           .add_output_column_list(MakeResolvedOutputColumn(
                               "x", updated_element_col1_))
                           .add_output_column_list(MakeResolvedOutputColumn(
                               "y", updated_element_col2_))
                           .set_query(std::move(graph_update_scan))
                           .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, ValidFinishScanWithGraphUpdateSideEffects) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  // Input scan with side effects.
  std::unique_ptr<const ResolvedGraphUpdateScan> graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFinishScan> finish_scan,
                       ResolvedFinishScanBuilder()
                           .set_input_scan(std::move(graph_update_scan))
                           .Build());

  std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt =
      MakeResolvedGeneralizedQueryStmt(
          /*output_schema=*/nullptr, std::move(finish_scan));

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanMissingInputScan) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {updated_element_col1_}, /*input_scan=*/nullptr,
      std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt,
                       ResolvedQueryStmtBuilder()
                           .add_output_column_list(MakeResolvedOutputColumn(
                               "x", updated_element_col1_))
                           .set_query(std::move(graph_update_scan))
                           .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("scan->input_scan() != nullptr")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanEmptyUpdateElementList) {
  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {element_col1_, element_col2_}, MakeInputScan(),
      /*update_element_list=*/{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("x", element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "UPDATE scan must modify at least one element")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanNonGraphElementTarget) {
  ResolvedColumn int_col(3, pool_.Make("tbl"), pool_.Make("z"),
                         factory_.get_int64());
  ResolvedColumn updated_int_col(12, pool_.Make("tbl"), pool_.Make("z"),
                                 factory_.get_int64());
  SimpleTable table("tbl", {{"x", graph_node_type_},
                            {"y", graph_node_type_},
                            {"z", factory_.get_int64()}});
  auto input_scan = ResolvedTableScanBuilder()
                        .set_table(&table)
                        .add_column_list(element_col1_)
                        .add_column_list(element_col2_)
                        .add_column_list(int_col)
                        .Build();
  GOOGLESQL_ASSERT_OK(input_scan);

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(factory_.get_int64(), int_col,
                            /*is_correlated=*/false),
      updated_int_col, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {element_col1_, element_col2_, updated_int_col}, std::move(*input_scan),
      std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("x", element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .add_output_column_list(
              MakeResolvedOutputColumn("z", updated_int_col))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Target of UPDATE element must be a graph node or edge")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanConflictingSetAndRemove) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue());

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_REMOVE,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "UPDATE element cannot specify both SET/REPLACE and "
                      "REMOVE update modes")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanSimultaneousNoUpdate) {
  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "UPDATE element cannot have NO_UPDATE for both property "
                      "and label")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanEmptyPropertyListOnSet) {
  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "property_list must not be empty when property_update_mode is")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanEmptyPropertyListOnRemove) {
  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REMOVE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      dynamic_validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "property_list must not be empty when property_update_mode is")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanEmptyLabelListOnSet) {
  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "label_list must not be empty when label_update_mode is")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanEmptyLabelListOnRemove) {
  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_REMOVE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      dynamic_validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "label_list must not be empty when label_update_mode is")));
}

TEST_F(ValidatorGraphTest,
       InvalidGraphUpdateScanPopulatedPropertyListOnNoUpdate) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue("Alice"));

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("property_list must be empty when "
                               "property_update_mode is PROPERTY_NO_UPDATE")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanPopulatedLabelListOnNoUpdate) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithValue("Alice"));

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("label_list must be empty when "
                               "label_update_mode is LABEL_NO_UPDATE")));
}

TEST_F(ValidatorGraphTest,
       InvalidGraphUpdateScanPropertyValuePopulatedOnRemove) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakeDynamicPropertyItemWithValue("name", "Alice"));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REMOVE,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      dynamic_validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("REMOVE property item must not have a value")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanPropertyValueMissingOnSet) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakePropertyItemWithoutValue());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_SET,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("SET/REPLACE property item must have a value")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanRemoveStaticProperty) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  // Static property cannot be removed.
  prop_list.push_back(MakePropertyItemWithoutValue());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REMOVE,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      dynamic_validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Static properties cannot be removed; property() must be null")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanRemoveStaticLabel) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  // Static label cannot be removed.
  label_list.push_back(MakeStaticLabel("Person"));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_REMOVE,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      dynamic_validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Static labels cannot be removed; label() must be null")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanSetLabelMismatch) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  // Static label with mismatched label_name literal.
  label_list.push_back(MakeResolvedGraphLabel(
      person_label_.get(),
      MakeResolvedLiteral(types::StringType(), Value::String("MismatchName"),
                          /*has_explicit_type=*/true)));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "label_name and label must match case-insensitively")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanDynamicLabelMissingName) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  // Dynamic label with missing label_name in SET.
  label_list.push_back(MakeResolvedGraphLabel(/*label=*/nullptr,
                                              /*label_name=*/nullptr));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("label_name must be set for dynamic label")));
}

TEST_F(ValidatorGraphTest,
       InvalidGraphUpdateScanRemoveDynamicLabelMissingName) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  // Dynamic label with missing label_name in REMOVE.
  label_list.push_back(MakeResolvedGraphLabel(/*label=*/nullptr,
                                              /*label_name=*/nullptr));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_REMOVE,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(dynamic_validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "label_name must be set for dynamic label removal")));
}

TEST_F(ValidatorGraphTest,
       InvalidGraphUpdateScanRemovePropertyFeatureDisabled) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>> prop_list;
  prop_list.push_back(MakeDynamicPropertyItemWithoutValue());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_REMOVE,
      std::move(prop_list), ResolvedGraphUpdateElement::LABEL_NO_UPDATE,
      /*label_list=*/{}));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Feature SQL_GRAPH_DYNAMIC_ELEMENT_TYPE is required for REMOVE "
              "property")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanRemoveLabelFeatureDisabled) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel("Vip"));

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_REMOVE,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({updated_element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Feature SQL_GRAPH_DYNAMIC_ELEMENT_TYPE is required for REMOVE "
              "label")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanDuplicateTargetElement) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list1;
  label_list1.push_back(MakeDynamicLabel());
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list2;
  label_list2.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list1)));
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col2_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list2)));

  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {updated_element_col1_, updated_element_col2_}, MakeInputScan(),
      std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt,
                       ResolvedQueryStmtBuilder()
                           .add_output_column_list(MakeResolvedOutputColumn(
                               "x", updated_element_col1_))
                           .add_output_column_list(MakeResolvedOutputColumn(
                               "y", updated_element_col2_))
                           .set_query(std::move(graph_update_scan))
                           .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("Target element column in "
                                       "update_element_list must be unique")));
}

TEST_F(ValidatorGraphTest,
       ValidGraphUpdateScanUpdatedColumnOmittedFromColumnList) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  // Output column_list omits updated_element_col1_ (e.g., due to column
  // pruning when updated_element_col1_ is not referenced downstream).
  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {element_col2_}, MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanUnproducedColumnInColumnList) {
  ResolvedColumn extra_unproduced_col(
      99, pool_.Make("tbl"), pool_.Make("unproduced"), factory_.get_int64());

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  // Output column_list includes an unproduced column not present in input_scan.
  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {updated_element_col1_, element_col2_, extra_unproduced_col},
      MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .add_output_column_list(
              MakeResolvedOutputColumn("extra", extra_unproduced_col))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("not visible in scan node")));
}

TEST_F(ValidatorGraphTest, ValidGraphUpdateScanKeepBothOldAndNewColumns) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      updated_element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  // Output column_list includes both old pre-update element_col1_ and
  // new post-update updated_element_col1_, allowing unambiguous reference.
  auto graph_update_scan = MakeResolvedGraphUpdateScan(
      {element_col1_, updated_element_col1_, element_col2_}, MakeInputScan(),
      std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x_old", element_col1_))
          .add_output_column_list(
              MakeResolvedOutputColumn("x_new", updated_element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest,
       InvalidGraphUpdateScanOutputColumnReusesInputColumnId) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  // Reusing element_col1_ as output_column violates SSA / unique column ID.
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      element_col1_, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({element_col1_, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(MakeResolvedOutputColumn("x", element_col1_))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("Duplicate column id 1")));
}

TEST_F(ValidatorGraphTest, InvalidGraphUpdateScanOutputColumnTypeMismatch) {
  ResolvedColumn mismatched_output_col(10, pool_.Make("graph_table"),
                                       pool_.Make("x"), graph_edge_type_);

  std::vector<std::unique_ptr<const ResolvedGraphLabel>> label_list;
  label_list.push_back(MakeDynamicLabel());

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_elements;
  update_elements.push_back(MakeResolvedGraphUpdateElement(
      MakeResolvedColumnRef(graph_node_type_, element_col1_,
                            /*is_correlated=*/false),
      mismatched_output_col, ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE,
      /*property_list=*/{}, ResolvedGraphUpdateElement::LABEL_SET,
      std::move(label_list)));

  auto graph_update_scan =
      MakeResolvedGraphUpdateScan({mismatched_output_col, element_col2_},
                                  MakeInputScan(), std::move(update_elements));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto query_stmt,
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("x", mismatched_output_col))
          .add_output_column_list(MakeResolvedOutputColumn("y", element_col2_))
          .set_query(std::move(graph_update_scan))
          .Build());

  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("output_column type must match target_element "
                               "type")));
}

TEST_F(ValidatorGraphTest, ValidGraphDeleteScan) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(updated_element_col1_)
          .add_column_list(updated_element_col2_)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col2_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col2_.type(),
                  /*column=*/element_col2_,
                  /*is_correlated=*/false)))
          // Note that any value of `is_detach_mode` has no effect on whether a
          // `ResolvedGraphDeleteScan` is valid or not.
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(
              /*output_column_list=*/
              MakeNodeVector(MakeResolvedOutputColumn(
                                 /*name=*/"updated_element_col1",
                                 /*column=*/updated_element_col1_),
                             MakeResolvedOutputColumn(
                                 /*name=*/"updated_element_col2",
                                 /*column=*/updated_element_col2_)),
              /*is_value_table=*/false))
          .set_query(std::move(graph_delete_scan))
          .Build());

  GOOGLESQL_EXPECT_OK(validator_.ValidateResolvedStatement(query_stmt.get()));
  Validator column_visibility_validator(
      LanguageOptions(), testing::ColumnVisibilityValidatorOptions());
  GOOGLESQL_EXPECT_OK(
      column_visibility_validator.ValidateResolvedStatement(query_stmt.get()));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanNoInputScan) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .set_input_scan(nullptr)
          .set_is_detach_mode(false)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(nullptr)
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because deletion can only occur with non-null input scans where at
  // least one column was matched.
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("Input scan must not be null")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanEmptyDeleteItemsList) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .set_input_scan(MakeResolvedSingleRowScan())
          .set_is_detach_mode(false)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(nullptr)
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because ResolvedGraphDeleteScanBuilder doesn't explicitly set/add
  // delete_item_list, so it is empty.
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "At least one graph element must be specified for "
                      "deletion")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanDeleteItemType) {
  // We only allow deleting graph element types (`graph_edge_type_` or
  // `graph_node_type_`)
  ResolvedColumn invalid_type_col(/*column_id=*/3,
                                  /*table_name=*/pool_.Make("invalid_type"),
                                  /*name=*/pool_.Make("node1"),
                                  /*type=*/factory_.get_int64());
  ResolvedColumn updated_invalid_type_col(
      /*column_id=*/12,
      /*table_name=*/pool_.Make("invalid_type"),
      /*name=*/pool_.Make("node1"),
      /*type=*/factory_.get_int64());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .set_input_scan(MakeResolvedTableScan(
              /*column_list=*/{element_col1_, invalid_type_col},
              /*table=*/graph_table_.get(),
              /*for_system_time_expr=*/nullptr))
          .add_delete_item_list(MakeResolvedComputedColumn(
              updated_invalid_type_col, MakeResolvedColumnRef(
                                            /*type=*/factory_.get_int64(),
                                            /*column=*/invalid_type_col,
                                            /*is_correlated=*/false)))
          // Note `element_col1_` is a valid `graph_node_type_`
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(nullptr)
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because at least one of the items being deleted is not a graph
  // element.
  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("An item being deleted must be a graph element, "
                               "but found invalid type INT64")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanUnknownGraphElementVariable) {
  ResolvedColumn unknown_col(/*column_id=*/3,
                             /*table_name=*/pool_.Make("unknown"),
                             /*name=*/pool_.Make("node"), graph_node_type_);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(element_col1_)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_,
              /*expr=*/
              MakeResolvedColumnRef(
                  /*type=*/unknown_col.type(),
                  /*column=*/unknown_col,
                  /*is_correlated=*/false)))
          .set_is_detach_mode(false)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(nullptr)
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Even though `unknown_col` is a valid graph element type, it does not refer
  // to an existing column in the input scan, so referencing it for deletion
  // fails.
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "Incorrect reference to column unknown.node#3")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanUnknownOutputColumn) {
  ResolvedColumn unknown_output_col(/*column_id=*/3,
                                    /*table_name=*/pool_.Make("unknown"),
                                    /*name=*/pool_.Make("node"),
                                    graph_node_type_);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(updated_element_col1_)
          .add_column_list(unknown_output_col)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(
              MakeNodeVector(
                  MakeResolvedOutputColumn(/*name=*/"updated_element_col1",
                                           /*column=*/updated_element_col1_),
                  MakeResolvedOutputColumn(/*name=*/"unknown_output_col",
                                           /*column=*/unknown_output_col)),
              /*is_value_table=*/false))
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because the output column list contains an unknown column that isn't
  // a new computed column for a deleted item nor a non-deleted column from the
  // input scan that we are passing through.
  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Column list contains column unknown.node#3 not visible in "
              "scan node")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanPreDeleteOutputColumn) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(element_col1_)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(
              MakeResolvedOutputSchema(MakeNodeVector(MakeResolvedOutputColumn(
                                           /*name=*/"element_col1",
                                           /*column=*/element_col1_)),
                                       /*is_value_table=*/false))
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because `element_col1_` is referenced for deletion in
  // `delete_item_list`, so the pre-delete column is no longer visible in the
  // output `column_list` of `ResolvedGraphDeleteScan`.
  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Column list contains column graph_table.x#1 not visible in "
              "scan node")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanComputedColExprIsNotColRef) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(updated_element_col1_)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_,
              /*expr=*/MakeResolvedLiteral(Value::Null(
                  /*type=*/graph_node_type_))))
          // Note that any value of `is_detach_mode` has no effect on whether a
          // `ResolvedGraphDeleteScan` is valid or not.
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(MakeResolvedOutputSchema(
              /*output_column_list=*/
              MakeNodeVector(MakeResolvedOutputColumn(
                  /*name=*/"updated_element_col1",
                  /*column=*/updated_element_col1_)),
              /*is_value_table=*/false))
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because even though we pass the check for
  // `delete_item->column().type()` == `delete_item->expr()->type()` with both
  // sides being `graph_node_type_`, the expression is a `ResolvedLiteral`
  // instead of a `ResolvedColumnRef`.
  EXPECT_THAT(validator_.ValidateResolvedStatement(query_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "Delete item `expr` must be a column reference")));
}

TEST_F(ValidatorGraphTest, InvalidGraphDeleteScanDuplicateDeleteItem) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGraphDeleteScan> graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .add_column_list(updated_element_col1_)
          .add_column_list(updated_element_col2_)
          .set_input_scan(MakeInputScan())
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col1_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .add_delete_item_list(MakeResolvedComputedColumn(
              /*column=*/updated_element_col2_, /*expr=*/MakeResolvedColumnRef(
                  /*type=*/element_col1_.type(),
                  /*column=*/element_col1_,
                  /*is_correlated=*/false)))
          .set_is_detach_mode(true)
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> query_stmt,
      ResolvedGeneralizedQueryStmtBuilder()
          .set_output_schema(nullptr)
          .set_query(std::move(graph_delete_scan))
          .Build());

  // Error because `element_col1_` is referenced more than once in
  // `delete_item_list`.
  EXPECT_THAT(
      validator_.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Duplicate column in `delete_item_list`: graph_table.x#1")));
}

TEST(ValidateTest, CreateTableFunctionStmtWithConnectionAndFeatureEnabled) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateTableFunctionStmt> stmt =
      MakeResolvedCreateTableFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/{FunctionArgumentType::AnyRelation(), {}, nullptr},
          /*has_explicit_return_schema=*/false,
          /*option_list=*/{},
          /*language=*/"PYTHON",
          /*code=*/"return 1;",
          /*query=*/nullptr,
          /*output_column_list=*/{},
          /*is_value_table=*/false,
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(stmt.get()));
}

TEST(ValidateTest, CreateTableFunctionStmtWithConnectionAndFeatureDisabled) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateTableFunctionStmt> stmt =
      MakeResolvedCreateTableFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/{FunctionArgumentType::AnyRelation(), {}, nullptr},
          /*has_explicit_return_schema=*/false,
          /*option_list=*/{},
          /*language=*/"PYTHON",
          /*code=*/"return 1;",
          /*query=*/nullptr,
          /*output_column_list=*/{},
          /*is_value_table=*/false,
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  // FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION is NOT enabled
  Validator validator(language_options);
  ASSERT_THAT(
      validator.ValidateResolvedStatement(stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("WITH CONNECTION clause is not supported")));
}

TEST(ValidateTest, CreateTableFunctionStmtWithConnectionAndEmptyLanguage) {
  SimpleConnection connection("connection_id");
  std::unique_ptr<ResolvedCreateTableFunctionStmt> stmt =
      MakeResolvedCreateTableFunctionStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/{FunctionArgumentType::AnyRelation(), {}, nullptr},
          /*has_explicit_return_schema=*/false,
          /*option_list=*/{},
          /*language=*/"",  // Empty language!
          /*code=*/"return 1;",
          /*query=*/nullptr,
          /*output_column_list=*/{},
          /*is_value_table=*/false,
          /*sql_security=*/ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED,
          /*connection_list=*/
          MakeResolvedConnectionList(MakeResolvedConnection(&connection)));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  Validator validator(language_options);
  ASSERT_THAT(validator.ValidateResolvedStatement(stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("!stmt->language().empty()")));
}

TEST(ValidateTest, CreateTableFunctionStmtWithMultiConnections) {
  SimpleConnection connection1("connection1");
  SimpleConnection connection2("connection2");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedCreateTableFunctionStmt> stmt,
      ResolvedCreateTableFunctionStmtBuilder()
          .set_name_path({"foo"})
          .set_create_scope(ResolvedCreateStatement::CREATE_DEFAULT_SCOPE)
          .set_create_mode(ResolvedCreateStatement::CREATE_DEFAULT)
          .set_signature({FunctionArgumentType::AnyRelation(), {}, nullptr})
          .set_has_explicit_return_schema(false)
          .set_language("PYTHON")
          .set_code("return 1;")
          .set_connection_list(
              ResolvedConnectionListBuilder()
                  .add_connection_kv_list(MakeResolvedConnectionKeyValuePair(
                      "key1", MakeResolvedConnection(&connection1)))
                  .add_connection_kv_list(MakeResolvedConnectionKeyValuePair(
                      "key2", MakeResolvedConnection(&connection2))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  language_options.EnableLanguageFeature(FEATURE_MULTI_CONNECTIONS);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(stmt.get()));
}

TEST(ValidateTest,
     CreateTableFunctionStmtWithMultiConnectionsAndFeatureDisabled) {
  SimpleConnection connection1("connection1");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedCreateTableFunctionStmt> stmt,
      ResolvedCreateTableFunctionStmtBuilder()
          .set_name_path({"foo"})
          .set_create_scope(ResolvedCreateStatement::CREATE_DEFAULT_SCOPE)
          .set_create_mode(ResolvedCreateStatement::CREATE_DEFAULT)
          .set_signature({FunctionArgumentType::AnyRelation(), {}, nullptr})
          .set_has_explicit_return_schema(false)
          .set_language("PYTHON")
          .set_code("return 1;")
          .set_connection_list(
              ResolvedConnectionListBuilder().add_connection_kv_list(
                  MakeResolvedConnectionKeyValuePair(
                      "key1", MakeResolvedConnection(&connection1))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  // FEATURE_MULTI_CONNECTIONS is NOT enabled.
  Validator validator(language_options);
  ASSERT_THAT(
      validator.ValidateResolvedStatement(stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Multiple connections in CONNECTION clause is not supported")));
}

TEST(ValidateTest,
     CreateTableFunctionStmtWithBothConnectionAndMultiConnections) {
  SimpleConnection connection1("connection1");
  SimpleConnection connection2("connection2");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedCreateTableFunctionStmt> stmt,
      ResolvedCreateTableFunctionStmtBuilder()
          .set_name_path({"foo"})
          .set_create_scope(ResolvedCreateStatement::CREATE_DEFAULT_SCOPE)
          .set_create_mode(ResolvedCreateStatement::CREATE_DEFAULT)
          .set_signature({FunctionArgumentType::AnyRelation(), {}, nullptr})
          .set_has_explicit_return_schema(false)
          .set_language("PYTHON")
          .set_code("return 1;")
          .set_connection_list(
              ResolvedConnectionListBuilder()
                  .set_connection(MakeResolvedConnection(&connection2))
                  .add_connection_kv_list(MakeResolvedConnectionKeyValuePair(
                      "key1", MakeResolvedConnection(&connection1))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  language_options.EnableLanguageFeature(FEATURE_MULTI_CONNECTIONS);
  Validator validator(language_options);
  ASSERT_THAT(validator.ValidateResolvedStatement(stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "Both connection and connection_kv_list are set")));
}

TEST(ValidateTest, CreateTableFunctionStmtWithEmptyConnectionList) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedCreateTableFunctionStmt> stmt,
      ResolvedCreateTableFunctionStmtBuilder()
          .set_name_path({"foo"})
          .set_create_scope(ResolvedCreateStatement::CREATE_DEFAULT_SCOPE)
          .set_create_mode(ResolvedCreateStatement::CREATE_DEFAULT)
          .set_signature({FunctionArgumentType::AnyRelation(), {}, nullptr})
          .set_has_explicit_return_schema(false)
          .set_language("PYTHON")
          .set_code("return 1;")
          .set_connection_list(ResolvedConnectionListBuilder())
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  language_options.EnableLanguageFeature(FEATURE_MULTI_CONNECTIONS);
  Validator validator(language_options);
  ASSERT_THAT(
      validator.ValidateResolvedStatement(stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "At least one of connection or connection_kv_list must be set")));
}

TEST(ValidateTest, CreateTableFunctionStmtWithEmptyKeyInMultiConnections) {
  SimpleConnection connection1("connection1");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedCreateTableFunctionStmt> stmt,
      ResolvedCreateTableFunctionStmtBuilder()
          .set_name_path({"foo"})
          .set_create_scope(ResolvedCreateStatement::CREATE_DEFAULT_SCOPE)
          .set_create_mode(ResolvedCreateStatement::CREATE_DEFAULT)
          .set_signature({FunctionArgumentType::AnyRelation(), {}, nullptr})
          .set_has_explicit_return_schema(false)
          .set_language("PYTHON")
          .set_code("return 1;")
          .set_connection_list(
              ResolvedConnectionListBuilder().add_connection_kv_list(
                  MakeResolvedConnectionKeyValuePair(
                      "", MakeResolvedConnection(&connection1))))
          .Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language_options.EnableLanguageFeature(
      FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION);
  language_options.EnableLanguageFeature(FEATURE_MULTI_CONNECTIONS);
  Validator validator(language_options);
  ASSERT_THAT(validator.ValidateResolvedStatement(stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("!kv_pair->key().empty()")));
}

TEST(ValidateTest, ResolvedFunctionArgumentWithMultiConnections) {
  SimpleConnection connection1("connection1");
  SimpleConnection connection2("connection2");

  TVFRelation output_relation({{"out_col", types::StringType()}});
  FunctionArgumentType concrete_conn_arg_type(ARG_KIND_CONNECTION,
                                              FunctionArgumentType::REQUIRED,
                                              /*num_occurrences=*/1);
  auto concrete_signature = std::make_shared<FunctionSignature>(
      FunctionArgumentType::RelationWithSchema(
          output_relation, /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList{concrete_conn_arg_type},
      /*context_ptr=*/nullptr);

  FixedOutputSchemaTVF tvf({"conn_tvf"}, {*concrete_signature},
                           output_relation);

  auto tvf_signature = std::make_shared<TVFSignature>(
      std::vector<TVFInputArgumentType>{
          TVFInputArgumentType(TVFConnectionArgument(nullptr))},
      output_relation);

  IdStringPool pool;
  ResolvedColumn resolved_col_out(1, pool.Make("t"), pool.Make("out_col"),
                                  types::StringType());

  auto query_stmt_builder =
      ResolvedQueryStmtBuilder()
          .add_output_column_list(
              MakeResolvedOutputColumn("out_col", resolved_col_out))
          .set_query(
              ResolvedTVFScanBuilder()
                  .add_column_list(resolved_col_out)
                  .set_tvf(&tvf)
                  .set_signature(tvf_signature)
                  .set_alias("")
                  .add_argument_list(
                      ResolvedFunctionArgumentBuilder().set_connection_list(
                          ResolvedConnectionListBuilder()
                              .add_connection_kv_list(
                                  MakeResolvedConnectionKeyValuePair(
                                      "key1",
                                      MakeResolvedConnection(&connection1)))
                              .add_connection_kv_list(
                                  MakeResolvedConnectionKeyValuePair(
                                      "key2",
                                      MakeResolvedConnection(&connection2)))))
                  .set_function_call_signature(concrete_signature));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto query_stmt, std::move(query_stmt_builder).Build());

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MULTI_CONNECTIONS);
  Validator validator(language_options);
  GOOGLESQL_ASSERT_OK(validator.ValidateResolvedStatement(query_stmt.get()));

  // Test failure when FEATURE_MULTI_CONNECTIONS is disabled
  LanguageOptions options_no_multi_conn;
  Validator validator_disabled(options_no_multi_conn);
  EXPECT_THAT(
      validator_disabled.ValidateResolvedStatement(query_stmt.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Multiple connections in CONNECTION clause is not supported")));
}

TEST(ValidatorTest, ValidVectorTypeParametersInCastExpr) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_DECLARATIVE_TYPE_FRAMEWORK);
  language_options.EnableLanguageFeature(FEATURE_VECTOR_TYPE);
  Validator validator(language_options);

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));

  VectorTypeParametersProto proto;
  proto.set_dims(10);
  proto.set_encoding(googlesql::VectorEncodingId::FLOAT32);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_params, Collation());
  auto cast_expr = MakeResolvedCast(
      vector_type, MakeResolvedLiteral(Value::Null(vector_type)),
      /*return_null_on_error=*/false);
  cast_expr->set_type_modifiers(type_modifiers);
  GOOGLESQL_ASSERT_OK(validator.ValidateStandaloneResolvedExpr(cast_expr.get()));
}

TEST(ValidatorTest, VectorTypeParametersForNonVectorTypeInCastExpr) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_DECLARATIVE_TYPE_FRAMEWORK);
  language_options.EnableLanguageFeature(FEATURE_VECTOR_TYPE);
  Validator validator(language_options);

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));

  StringTypeParametersProto string_proto;
  string_proto.set_max_length(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeStringTypeParameters(string_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_params, Collation());
  auto cast_expr = MakeResolvedCast(
      vector_type, MakeResolvedLiteral(Value::Null(vector_type)),
      /*return_null_on_error=*/false);
  cast_expr->set_type_modifiers(type_modifiers);
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(cast_expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("vector_params != nullptr")));
}

TEST(ValidatorTest, InvalidVectorTypeParameters) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_DECLARATIVE_TYPE_FRAMEWORK);
  language_options.EnableLanguageFeature(FEATURE_VECTOR_TYPE);
  Validator validator(language_options);

  VectorTypeParametersProto proto;
  proto.set_dims(10);
  proto.set_encoding(googlesql::VectorEncodingId::UNKNOWN_VECTOR_ENCODING);
  EXPECT_THAT(
      TypeParameters::MakeVectorTypeParameters(proto),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Unrecognized VECTOR encoding: \"UNKNOWN_VECTOR_ENCODING\"")));
}

TEST(ValidatorTest, ValidDeleteStmtWithUsingScan) {
  Validator validator;
  IdStringPool pool;
  TypeFactory type_factory;
  const SimpleTable table("KeyValue", {{"Key", type_factory.get_int64()}});
  const SimpleTable table2("KeyValue2", {{"Key", type_factory.get_int64()}});
  const ResolvedColumn target_col(1, pool.Make("KeyValue"), pool.Make("Key"),
                                  type_factory.get_int64());
  const ResolvedColumn from_col(2, pool.Make("KeyValue2"), pool.Make("Key"),
                                type_factory.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table)
                                            .add_column_list(target_col)
                                            .add_column_index_list(0)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto from_scan, ResolvedTableScanBuilder()
                                           .set_table(&table2)
                                           .add_column_list(from_col)
                                           .add_column_index_list(0)
                                           .Build());

  FunctionSignature sig(FunctionArgumentType(types::BoolType(), 1),
                        {FunctionArgumentType(types::Int64Type(), 1),
                         FunctionArgumentType(types::Int64Type(), 1)},
                        /*context_id=*/static_cast<int64_t>(1));
  Function equal_fn("equal", Function::kGoogleSQLFunctionGroupName,
                    Function::SCALAR, {sig});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto where_expr,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(&equal_fn)
          .set_signature(sig)
          .add_argument_list(MakeResolvedColumnRef(types::Int64Type(),
                                                   target_col,
                                                   /*is_correlated=*/false))
          .add_argument_list(MakeResolvedColumnRef(types::Int64Type(), from_col,
                                                   /*is_correlated=*/false))
          .Build());

  auto returning_output_col = MakeResolvedOutputColumn("Key", target_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_stmt,
                       ResolvedDeleteStmtBuilder()
                           .set_table_scan(std::move(table_scan))
                           .set_using_scan(std::move(from_scan))
                           .set_where_expr(std::move(where_expr))
                           .set_returning(std::move(returning_clause))
                           .add_column_access_list(ResolvedStatementEnums::READ)
                           .Build());

  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(delete_stmt.get()));
}

TEST(ValidatorTest, InvalidDeleteStmtWhereExprRefersToUnknownColumn) {
  Validator validator;
  IdStringPool pool;
  TypeFactory type_factory;
  const SimpleTable table("KeyValue", {{"Key", type_factory.get_int64()}});
  const SimpleTable table2("KeyValue2", {{"Key", type_factory.get_int64()}});
  const ResolvedColumn target_col(1, pool.Make("KeyValue"), pool.Make("Key"),
                                  type_factory.get_int64());
  const ResolvedColumn from_col(2, pool.Make("KeyValue2"), pool.Make("Key"),
                                type_factory.get_int64());
  const ResolvedColumn unknown_col(999, pool.Make("Unknown"), pool.Make("Col"),
                                   type_factory.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table)
                                            .add_column_list(target_col)
                                            .add_column_index_list(0)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto from_scan, ResolvedTableScanBuilder()
                                           .set_table(&table2)
                                           .add_column_list(from_col)
                                           .add_column_index_list(0)
                                           .Build());

  FunctionSignature sig(FunctionArgumentType(types::BoolType(), 1),
                        {FunctionArgumentType(types::Int64Type(), 1),
                         FunctionArgumentType(types::Int64Type(), 1)},
                        /*context_id=*/static_cast<int64_t>(1));
  Function equal_fn("equal", Function::kGoogleSQLFunctionGroupName,
                    Function::SCALAR, {sig});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto where_expr,
                       ResolvedFunctionCallBuilder()
                           .set_type(types::BoolType())
                           .set_function(&equal_fn)
                           .set_signature(sig)
                           .add_argument_list(MakeResolvedColumnRef(
                               types::Int64Type(), target_col,
                               /*is_correlated=*/false))
                           .add_argument_list(MakeResolvedColumnRef(
                               types::Int64Type(), unknown_col,
                               /*is_correlated=*/false))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto delete_stmt,
                       ResolvedDeleteStmtBuilder()
                           .set_table_scan(std::move(table_scan))
                           .set_using_scan(std::move(from_scan))
                           .set_where_expr(std::move(where_expr))
                           .add_column_access_list(ResolvedStatementEnums::READ)
                           .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(delete_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "Incorrect reference to column Unknown.Col#999")));
}

TEST(ValidatorTest, InvalidDeleteStmtReturningAccessingUsingScanColumn) {
  Validator validator;
  IdStringPool pool;
  TypeFactory type_factory;
  const SimpleTable table("KeyValue", {{"Key", type_factory.get_int64()}});
  const SimpleTable table2("KeyValue2", {{"Key", type_factory.get_int64()}});
  const ResolvedColumn target_col(1, pool.Make("KeyValue"), pool.Make("Key"),
                                  type_factory.get_int64());
  const ResolvedColumn from_col(2, pool.Make("KeyValue2"), pool.Make("Key"),
                                type_factory.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table)
                                            .add_column_list(target_col)
                                            .add_column_index_list(0)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto from_scan, ResolvedTableScanBuilder()
                                           .set_table(&table2)
                                           .add_column_list(from_col)
                                           .add_column_index_list(0)
                                           .Build());

  auto returning_output_col = MakeResolvedOutputColumn("Key", from_col);
  std::vector<std::unique_ptr<const ResolvedOutputColumn>>
      returning_output_cols;
  returning_output_cols.push_back(std::move(returning_output_col));
  auto returning_clause = MakeResolvedReturningClause(
      std::move(returning_output_cols), nullptr, {});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_using_scan(std::move(from_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_returning(std::move(returning_clause))
          .add_column_access_list(ResolvedStatementEnums::READ)
          .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(delete_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "Incorrect reference to column KeyValue2.Key#2")));
}

TEST(ValidatorTest, InvalidNestedDeleteStmtWithUsingScan) {
  Validator validator;
  IdStringPool pool;
  TypeFactory type_factory;
  const Type* array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(type_factory.get_int64(), &array_type));
  const SimpleTable table("KeyValue", {{"Arr", array_type}});
  const SimpleTable table2("KeyValue2", {{"Key", type_factory.get_int64()}});
  const ResolvedColumn target_col(1, pool.Make("KeyValue"), pool.Make("Arr"),
                                  array_type);
  const ResolvedColumn element_col(
      2, pool.Make("$element"), pool.Make("Element"), type_factory.get_int64());
  const ResolvedColumn from_col(3, pool.Make("KeyValue2"), pool.Make("Key"),
                                type_factory.get_int64());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto table_scan, ResolvedTableScanBuilder()
                                            .set_table(&table)
                                            .add_column_list(target_col)
                                            .add_column_index_list(0)
                                            .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto from_scan, ResolvedTableScanBuilder()
                                           .set_table(&table2)
                                           .add_column_list(from_col)
                                           .add_column_index_list(0)
                                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nested_delete_stmt,
      ResolvedDeleteStmtBuilder()
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_using_scan(std::move(from_scan))
          .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_item,
      ResolvedUpdateItemBuilder()
          .set_target(MakeResolvedColumnRef(target_col.type(), target_col,
                                            /*is_correlated=*/false))
          .set_element_column(MakeResolvedColumnHolder(element_col))
          .add_delete_list(std::move(nested_delete_stmt))
          .Build());

  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(std::move(update_item));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto update_stmt,
      ResolvedUpdateStmtBuilder()
          .set_table_scan(std::move(table_scan))
          .set_where_expr(MakeResolvedLiteral(Value::Bool(true)))
          .set_update_item_list(std::move(update_item_list))
          .add_column_access_list(ResolvedStatementEnums::WRITE)
          .Build());

  EXPECT_THAT(validator.ValidateResolvedStatement(update_stmt.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("stmt->using_scan() == nullptr")));
}

TEST(ValidatorTest, ValidGetVariantField) {
  std::unique_ptr<ResolvedExpr> expr = MakeResolvedGetVariantField(
      types::VariantType(), MakeResolvedParameter(types::VariantType(), "v"),
      "foo");
  Validator validator;
  GOOGLESQL_EXPECT_OK(validator.ValidateStandaloneResolvedExpr(expr.get()));
}

TEST(ValidatorTest, InvalidGetVariantFieldExprNotVariant) {
  std::unique_ptr<ResolvedExpr> expr = MakeResolvedGetVariantField(
      types::VariantType(), MakeResolvedLiteral(Value::Int64(1)), "foo");
  Validator validator;
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "get_variant_field->expr()->type()->IsVariant()")));
}

TEST(ValidatorTest, InvalidGetVariantFieldReturnTypeNotVariant) {
  std::unique_ptr<ResolvedExpr> expr = MakeResolvedGetVariantField(
      types::Int64Type(), MakeResolvedParameter(types::VariantType(), "v"),
      "foo");
  Validator validator;
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("get_variant_field->type()->IsVariant()")));
}

// ResolvedGetVariantField represents dot-notation access (v.field_name), where
// SQL identifiers cannot be empty (matching ResolvedGetJsonField). Empty keys
// in Variant objects are queried via subscript syntax (v[""]).
TEST(ValidatorTest, InvalidGetVariantFieldEmptyFieldName) {
  std::unique_ptr<ResolvedExpr> expr = MakeResolvedGetVariantField(
      types::VariantType(), MakeResolvedParameter(types::VariantType(), "v"),
      "");
  Validator validator;
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("!get_variant_field->field_name().empty()")));
}

TEST(ValidatorTest, ValidColumnListSpec) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedLiteral(values::StringArray({"a", "b"})));
  GOOGLESQL_EXPECT_OK(validator.ValidateStandaloneResolvedExpr(expr.get()));
}

TEST(ValidatorTest, ValidEmptyColumnListSpec) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedLiteral(values::EmptyArray(types::StringArrayType())));
  GOOGLESQL_EXPECT_OK(validator.ValidateStandaloneResolvedExpr(expr.get()));

  std::unique_ptr<ResolvedExpr> int64_empty_array_expr =
      MakeResolvedMakeColumnListSpec(
          types::ColumnListSpecType(),
          MakeResolvedLiteral(values::EmptyArray(types::Int64ArrayType())));
  GOOGLESQL_EXPECT_OK(
      validator.ValidateStandaloneResolvedExpr(int64_empty_array_expr.get()));
}

TEST(ValidatorTest, InvalidColumnListSpecFeatureDisabled) {
  LanguageOptions language_options;
  language_options.DisableAllLanguageFeatures();
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedLiteral(values::StringArray({"a", "b"})));
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("language_options_.LanguageFeatureEnabled("
                               "FEATURE_COLUMN_LIST_SPEC)")));
}

TEST(ValidatorTest, InvalidColumnListSpecWrongReturnType) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::StringArrayType(),
      MakeResolvedLiteral(values::StringArray({"a", "b"})));
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr(
                      "column_list_spec->type()->IsColumnListSpec()")));
}

TEST(ValidatorTest, InvalidColumnListSpecNullColumnNameList) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr =
      MakeResolvedMakeColumnListSpec(types::ColumnListSpecType(), nullptr);
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("column_name_list != nullptr")));
}

TEST(ValidatorTest, InvalidColumnListSpecNotStringArray) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedLiteral(values::Int64Array({1, 2})));
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("IsStringArray(column_name_list->type())")));
}

TEST(ValidatorTest, InvalidColumnListSpecNotAnalysisConstant) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedParameter(types::StringArrayType(), "p"));
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("IsAnalysisConstant(column_name_list)")));
}

TEST(ValidatorTest, InvalidColumnListSpecNullArrayValue) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedLiteral(Value::Null(types::StringArrayType())));
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("!array_value->is_null()")));
}

TEST(ValidatorTest, InvalidColumnListSpecNullElement) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_with_null,
      Value::MakeArray(types::StringArrayType(),
                       {Value::String("a"), Value::NullString()}));
  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(), MakeResolvedLiteral(array_with_null));
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(absl::StatusCode::kInternal,
                                  ::testing::HasSubstr("!element.is_null()")));
}

TEST(ValidatorTest, InvalidColumnListSpecEmptyStringElement) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_COLUMN_LIST_SPEC);
  Validator validator(language_options);

  std::unique_ptr<ResolvedExpr> expr = MakeResolvedMakeColumnListSpec(
      types::ColumnListSpecType(),
      MakeResolvedLiteral(values::StringArray({"a", ""})));
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("!element.string_value().empty()")));
}

namespace {

enum class UpdateItemColumnShape {
  // The item column is referenced by its own item's `expr`.
  kValid,
  // The item column is referenced by a sibling item's `expr`.
  kReferencedBySiblingItem,
  // The item column is referenced by the UPDATE constructor's `expr`.
  kReferencedByConstructorExpr,
  // The item column reuses the column id of `update_element_column`.
  kDuplicateColumnId,
  // The item column has a type that doesn't match the updated field.
  kWrongType,
};

// Returns a hand-constructed ResolvedUpdateConstructor representing
//   UPDATE (<input>) { precision: precision, scale: 5 }
// on NumericTypeParametersProto, where the `precision` item introduces
// `update_item_column`. `shape` controls where the column is referenced.
std::unique_ptr<const ResolvedUpdateConstructor> MakeUpdateConstructor(
    TypeFactory& type_factory, IdStringPool& pool,
    UpdateItemColumnShape shape) {
  const google::protobuf::Descriptor* descriptor =
      NumericTypeParametersProto::descriptor();
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory.MakeProtoType(descriptor, &proto_type));
  const google::protobuf::FieldDescriptor* precision =
      descriptor->FindFieldByName("precision");
  const google::protobuf::FieldDescriptor* scale = descriptor->FindFieldByName("scale");

  const ResolvedColumn element_column(1, pool.Make("$update_constructor"),
                                      pool.Make("update_element_column"),
                                      proto_type);
  // `precision` is an INT64 field.
  const Type* item_column_type = shape == UpdateItemColumnShape::kWrongType
                                     ? types::StringType()
                                     : types::Int64Type();
  const ResolvedColumn item_column(
      shape == UpdateItemColumnShape::kDuplicateColumnId ? 1 : 2,
      pool.Make("$update_constructor"), pool.Make("update_item_column"),
      item_column_type);

  // For kReferencedByConstructorExpr the input is
  //   REPLACE_FIELDS(<null proto>, <item_column> AS precision)
  // so that the UPDATE constructor's own `expr` references the item column.
  std::unique_ptr<const ResolvedExpr> input =
      MakeResolvedLiteral(Value::Null(proto_type));
  if (shape == UpdateItemColumnShape::kReferencedByConstructorExpr) {
    input = MakeResolvedReplaceField(
        proto_type, std::move(input),
        MakeNodeVector(MakeResolvedReplaceFieldItem(
            MakeResolvedColumnRef(types::Int64Type(), item_column,
                                  /*is_correlated=*/false),
            /*struct_index_path=*/{}, {precision})));
  }

  std::vector<std::unique_ptr<const ResolvedUpdateFieldItem>> items;
  items.push_back(MakeResolvedUpdateFieldItem(
      MakeResolvedColumnRef(item_column_type, item_column,
                            /*is_correlated=*/false),
      {precision}, ResolvedUpdateFieldItem::UPDATE_SINGLE,
      MakeResolvedColumnHolder(item_column)));
  items.push_back(MakeResolvedUpdateFieldItem(
      shape == UpdateItemColumnShape::kReferencedBySiblingItem
          ? std::unique_ptr<const ResolvedExpr>(MakeResolvedColumnRef(
                types::Int64Type(), item_column, /*is_correlated=*/false))
          : MakeResolvedLiteral(Value::Int64(5)),
      {scale}, ResolvedUpdateFieldItem::UPDATE_SINGLE,
      /*update_item_column=*/nullptr));

  return MakeResolvedUpdateConstructor(proto_type, std::move(input),
                                       /*alias=*/"", std::move(items),
                                       element_column);
}

}  // namespace

TEST(ValidatorTest, UpdateConstructorItemColumnVisibleToOwnItem) {
  TypeFactory type_factory;
  IdStringPool pool;
  std::unique_ptr<const ResolvedUpdateConstructor> expr =
      MakeUpdateConstructor(type_factory, pool, UpdateItemColumnShape::kValid);
  Validator validator;
  GOOGLESQL_EXPECT_OK(validator.ValidateStandaloneResolvedExpr(expr.get()));
}

TEST(ValidatorTest, UpdateConstructorItemColumnNotVisibleToSiblingItem) {
  TypeFactory type_factory;
  IdStringPool pool;
  std::unique_ptr<const ResolvedUpdateConstructor> expr = MakeUpdateConstructor(
      type_factory, pool, UpdateItemColumnShape::kReferencedBySiblingItem);
  Validator validator;
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("Incorrect reference to column "
                               "$update_constructor.update_item_column#2")));
}

TEST(ValidatorTest, UpdateConstructorItemColumnNotVisibleToConstructorExpr) {
  TypeFactory type_factory;
  IdStringPool pool;
  std::unique_ptr<const ResolvedUpdateConstructor> expr = MakeUpdateConstructor(
      type_factory, pool, UpdateItemColumnShape::kReferencedByConstructorExpr);
  Validator validator;
  EXPECT_THAT(
      validator.ValidateStandaloneResolvedExpr(expr.get()),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr("Incorrect reference to column "
                               "$update_constructor.update_item_column#2")));
}

TEST(ValidatorTest, UpdateConstructorItemColumnMustHaveUniqueColumnId) {
  TypeFactory type_factory;
  IdStringPool pool;
  std::unique_ptr<const ResolvedUpdateConstructor> expr = MakeUpdateConstructor(
      type_factory, pool, UpdateItemColumnShape::kDuplicateColumnId);
  Validator validator;
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("Duplicate column id 1")));
}

TEST(ValidatorTest, UpdateConstructorItemColumnMustMatchFieldType) {
  TypeFactory type_factory;
  IdStringPool pool;
  std::unique_ptr<const ResolvedUpdateConstructor> expr = MakeUpdateConstructor(
      type_factory, pool, UpdateItemColumnShape::kWrongType);
  Validator validator;
  EXPECT_THAT(validator.ValidateStandaloneResolvedExpr(expr.get()),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::HasSubstr("update_item_column has type STRING")));
}

}  // namespace googlesql
