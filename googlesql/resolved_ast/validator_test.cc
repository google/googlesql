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
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/no_destructor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace testing {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
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
  static googlesql_base::NoDestructor<Function> dp_function("count", "test_group",
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
          /*is_remote=*/true,
          /*connection=*/nullptr);

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
          /*is_remote=*/true,
          /*connection=*/nullptr);

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
                            {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
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
          /*is_remote=*/true,
          /*connection=*/nullptr);

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
          /*is_remote=*/true,
          /*connection=*/nullptr);
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
          /*is_remote=*/true,
          /*connection=*/nullptr);

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
          /*is_remote=*/false, MakeResolvedConnection(&connection));

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
          /*is_remote=*/false,
          /*connection=*/nullptr);

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
          /*is_remote=*/false,
          /*connection=*/nullptr);

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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/nullptr,
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/MakeResolvedConnection(&connection),
          /*language=*/"PYTHON",
          /*code=*/"",
          /*external_security=*/
          ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED);

  Validator validator;
  ASSERT_THAT(
      validator.ValidateResolvedStatement(create_procedure_stmt.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("stmt->connection()")));
}

TEST(ValidateTest, CreateProcedureStmtNonSQLFeatureEnabledMissingLanguage) {
  std::unique_ptr<ResolvedCreateProcedureStmt> create_procedure_stmt =
      MakeResolvedCreateProcedureStmt(
          /*name_path=*/{"foo"},
          /*create_scope=*/ResolvedCreateStatement::CREATE_DEFAULT_SCOPE,
          /*create_mode=*/ResolvedCreateStatement::CREATE_DEFAULT,
          /*argument_name_list=*/{},
          /*signature=*/
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"sql",
          /*connection=*/nullptr,
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"body",
          /*connection=*/nullptr,
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/nullptr,
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/nullptr,
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/MakeResolvedConnection(&connection),
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/MakeResolvedConnection(&connection),
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
          FunctionSignature(FunctionArgumentType(ARG_TYPE_VOID), {}, nullptr),
          /*option_list=*/{},
          /*procedure_body=*/"",
          /*connection=*/MakeResolvedConnection(&connection),
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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*connection=*/MakeResolvedConnection(&connection));

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
      /*connection=*/MakeResolvedConnection(&connection));

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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*connection=*/MakeResolvedConnection(&connection));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_REMOTE_MODEL);
  Validator validator(language_options);
  EXPECT_THAT(
      validator.ValidateResolvedStatement(statement.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("stmt->connection()")));
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
      /*is_remote=*/false,
      /*connection=*/{});

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
      /*property_types=*/{}, &graph_element_type));

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
      /*static_property_types=*/{}, &dynamic_graph_element_type));

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
      /*static_property_types=*/{}, &dynamic_graph_element_type));

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
      ARG_TYPE_RELATION,
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

}  // namespace
}  // namespace testing

TEST(ValidatorTest, InvalidDynamicGraphGetElementProperty) {
  std::string graph_name = "graph_name";
  const SimplePropertyGraph graph(std::vector<std::string>{graph_name});
  TypeFactory factory;
  const GraphElementType* dynamic_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {graph_name}, GraphElementType::ElementKind::kNode,
      /*static_property_types=*/{}, &dynamic_graph_element_type));

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
      /*static_property_types=*/{}, &dynamic_graph_element_type));

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

}  // namespace googlesql
