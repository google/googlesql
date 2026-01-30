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

#include "googlesql/analyzer/substitute.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/testing/test_catalog.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {
using testing::ElementsAre;
using testing::HasSubstr;
using absl_testing::StatusIs;

class ExpressionSubstitutorTest : public ::testing::Test {
 public:
  ExpressionSubstitutorTest()
      : catalog_("catalog"),
        test_table_("TestTable"),
        test_column_("TestTable", "Int64Col", types::Int64Type()) {}

 protected:
  void SetUp() override {
    // Setup TestTable
    options_.CreateDefaultArenasIfNotSet();
    seq_.GetNext();  // Avoid column id's of 0, which are forbidden.
    options_.set_column_id_sequence_number(&seq_);
    options_.mutable_language()->EnableLanguageFeature(
        FEATURE_INLINE_LAMBDA_ARGUMENT);
    options_.mutable_language()->EnableLanguageFeature(
        FEATURE_COLLATION_SUPPORT);
    options_.mutable_language()->EnableLanguageFeature(
        FEATURE_ANNOTATION_FRAMEWORK);
    options_.set_record_parse_locations(true);

    catalog_.AddBuiltinFunctions(BuiltinFunctionOptions(options_.language()));
    GOOGLESQL_CHECK_OK(test_table_.AddColumn(&test_column_, false));
    catalog_.AddTable(&test_table_);

    col1_ = ResolvedColumn(
        static_cast<int>(options_.column_id_sequence_number()->GetNext()),
        googlesql::IdString::MakeGlobal("t"),
        googlesql::IdString::MakeGlobal("col1"), type_factory_.get_int64());
    col2_ = ResolvedColumn(
        static_cast<int>(options_.column_id_sequence_number()->GetNext()),
        googlesql::IdString::MakeGlobal("t"),
        googlesql::IdString::MakeGlobal("col2"), type_factory_.get_int64());
    col1_ref_ =
        MakeResolvedColumnRef(col1_.type(), col1_, /*is_correlated=*/false);
    col2_ref_ =
        MakeResolvedColumnRef(col2_.type(), col2_, /*is_correlated=*/false);

    // It's tricky to construct a lambda directly, so we analyze an expression
    // that includes a lambda then use the contained lambda for substitute. This
    // works out well because it's how we also expect substitute to typically be
    // used.
    constexpr absl::string_view sql_with_array_filter = R"sql(
  ( SELECT ARRAY_FILTER([1, 2, 3], (a,b) -> a+b<Int64Col)
    FROM TestTable t )
  )sql";
    // Disable rewrites to preserve the ResolvedInlineLambdas.
    options_.set_enabled_rewrites({});
    GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
        sql_with_array_filter, options_, &catalog_, &type_factory_,
        types::Int64ArrayType(), &filter_lambda_output_));
    const ResolvedFunctionCall* array_filter_call =
        filter_lambda_output_->resolved_expr()
            ->GetAs<ResolvedSubqueryExpr>()
            ->subquery()
            ->GetAs<ResolvedProjectScan>()
            ->expr_list(0)
            ->expr()
            ->GetAs<ResolvedFunctionCall>();
    filter_lambda_ =
        array_filter_call->generic_argument_list(1)->inline_lambda();
  }

  googlesql_base::SequenceNumber seq_;
  AnalyzerOptions options_;
  TypeFactory type_factory_;
  TestCatalog catalog_;
  SimpleTable test_table_;
  SimpleColumn test_column_;

  ResolvedColumn col1_;
  ResolvedColumn col2_;
  std::unique_ptr<ResolvedColumnRef> col1_ref_;
  std::unique_ptr<ResolvedColumnRef> col2_ref_;

  std::unique_ptr<const AnalyzerOutput> filter_lambda_output_;
  const ResolvedInlineLambda* filter_lambda_;

  std::unique_ptr<const AnalyzerOutput> transform_lambda_output_;
  const ResolvedInlineLambda* transform_lambda_;
};

TEST_F(ExpressionSubstitutorTest, RequiresArenas) {
  {
    AnalyzerOptions options = options_;
    options.set_column_id_sequence_number(nullptr);
    EXPECT_THAT(AnalyzeSubstitute(options, catalog_, type_factory_, "1 + 2", {})
                    .status(),
                absl_testing::StatusIs(absl::StatusCode::kInternal));
  }
  {
    AnalyzerOptions options = options_;
    options.set_id_string_pool(nullptr);
    EXPECT_THAT(AnalyzeSubstitute(options, catalog_, type_factory_, "1 + 2", {})
                    .status(),
                absl_testing::StatusIs(absl::StatusCode::kInternal));
  }
  {
    AnalyzerOptions options = options_;
    options.set_arena(nullptr);
    EXPECT_THAT(AnalyzeSubstitute(options, catalog_, type_factory_, "1 + 2", {})
                    .status(),
                absl_testing::StatusIs(absl::StatusCode::kInternal));
  }
}

TEST_F(ExpressionSubstitutorTest, ColumnReferences) {
  // Make sure the subquery's parameter list is set up properly when the
  // expressions being substituted in contain column references.
  auto result = *AnalyzeSubstitute(
      options_, catalog_, type_factory_, "col1 + col2 + col1 + col2",
      {{"col1", col1_ref_.get()}, {"col2", col2_ref_.get()}});
  ASSERT_TRUE(result->type()->IsInt64());
  const auto& parameter_list =
      result->GetAs<ResolvedSubqueryExpr>()->parameter_list();
  ASSERT_EQ(parameter_list.size(), 2);
  ASSERT_EQ(parameter_list[0]->column(), col1_);
  ASSERT_EQ(parameter_list[1]->column(), col2_);
  ASSERT_FALSE(parameter_list[0]->is_correlated());
  ASSERT_FALSE(parameter_list[1]->is_correlated());
}

TEST_F(ExpressionSubstitutorTest, SubstituteExpressions) {
  std::unique_ptr<const AnalyzerOutput> array_arg_analyzer_output;
  GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
      "[1, 2, 3, 4, 5, 6, 7, 8, 9]", options_, &catalog_, &type_factory_,
      types::Int64ArrayType(), &array_arg_analyzer_output));

  std::unique_ptr<const AnalyzerOutput> base_arg_analyzer_output;
  GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
      "(SELECT x FROM UNNEST([1, 2, 3]) x WHERE x > 2)", options_, &catalog_,
      &type_factory_, types::Int64Type(), &base_arg_analyzer_output));

  auto result = *AnalyzeSubstitute(
      options_, catalog_, type_factory_,
      "ARRAY(SELECT x FROM UNNEST(array_arg) x WHERE MOD(x, "
      "base_arg) = 0)",
      {{"array_arg", array_arg_analyzer_output->resolved_expr()},
       {"base_arg", base_arg_analyzer_output->resolved_expr()}});

  GOOGLESQL_VLOG(1) << result->DebugString();

  EXPECT_EQ(result->DebugString(), R"(SubqueryExpr
+-type=ARRAY<INT64>
+-subquery_type=SCALAR
+-subquery=
  +-ProjectScan
    +-column_list=[$expr_subquery.$col1#11]
    +-expr_list=
    | +-$col1#11 :=
    |   +-SubqueryExpr
    |     +-type=ARRAY<INT64>
    |     +-subquery_type=ARRAY
    |     +-parameter_list=
    |     | +-ColumnRef(type=ARRAY<INT64>, column=$subquery1.array_arg#8)
    |     | +-ColumnRef(type=INT64, column=$subquery1.base_arg#9)
    |     +-subquery=
    |       +-ProjectScan
    |         +-column_list=[$array.x#10]
    |         +-input_scan=
    |           +-FilterScan
    |             +-column_list=[$array.x#10]
    |             +-input_scan=
    |             | +-ArrayScan
    |             |   +-column_list=[$array.x#10]
    |             |   +-array_expr_list=
    |             |   | +-ColumnRef(type=ARRAY<INT64>, column=$subquery1.array_arg#8, is_correlated=TRUE)
    |             |   +-element_column_list=[$array.x#10]
    |             +-filter_expr=
    |               +-FunctionCall(GoogleSQL:$equal(INT64, INT64) -> BOOL)
    |                 +-FunctionCall(GoogleSQL:mod(INT64, INT64) -> INT64)
    |                 | +-ColumnRef(type=INT64, column=$array.x#10)
    |                 | +-ColumnRef(type=INT64, column=$subquery1.base_arg#9, is_correlated=TRUE)
    |                 +-Literal(type=INT64, value=0)
    +-input_scan=
      +-ProjectScan
        +-column_list=$subquery1.[array_arg#8, base_arg#9]
        +-expr_list=
        | +-array_arg#8 := Literal(parse_location=0-27, type=ARRAY<INT64>, value=[1, 2, 3, 4, 5, 6, 7, 8, 9])
        | +-base_arg#9 :=
        |   +-SubqueryExpr
        |     +-parse_location=1-46
        |     +-type=INT64
        |     +-subquery_type=SCALAR
        |     +-subquery=
        |       +-ProjectScan
        |         +-parse_location=1-46
        |         +-column_list=[$array.x#7]
        |         +-input_scan=
        |           +-FilterScan
        |             +-column_list=[$array.x#7]
        |             +-input_scan=
        |             | +-ArrayScan
        |             |   +-column_list=[$array.x#7]
        |             |   +-array_expr_list=
        |             |   | +-Literal(parse_location=22-31, type=ARRAY<INT64>, value=[1, 2, 3])
        |             |   +-element_column_list=[$array.x#7]
        |             +-filter_expr=
        |               +-FunctionCall(GoogleSQL:$greater(INT64, INT64) -> BOOL)
        |                 +-parse_location=41-46
        |                 +-ColumnRef(type=INT64, column=$array.x#7)
        |                 +-Literal(parse_location=45-46, type=INT64, value=2)
        +-input_scan=
          +-SingleRowScan
)");

  EXPECT_THAT(PreparedExpression(result.get(), {}).Execute()->elements(),
              ElementsAre(Value::Int64(3), Value::Int64(6), Value::Int64(9)));
}

TEST_F(ExpressionSubstitutorTest, SubstituteLambda) {
  // Run substitute
  constexpr absl::string_view input_sql = R"sql(
  ARRAY(SELECT x FROM UNNEST([1,2,3]) x WITH OFFSET off
        WHERE mylambda(x, off))
  )sql";
  auto result =
      *AnalyzeSubstitute(options_, catalog_, type_factory_, input_sql, {},
                         /*lambdas=*/{{"mylambda", filter_lambda_}});

  GOOGLESQL_VLOG(1) << result->DebugString();
  // NOTE:
  //   * This snippet is for ARRAY_FILTER call.
  //   * The $less function call is using 'x' and 'off' columns instead of 'a'
  //   and 'b' columns.
  //   * TestTable.Int64Col ref is put into parameter_list of two SubqueryExprs.
  //   * TestTable.Int64Col ref in the outermost is not correlated.
  //   * TestTable.Int64Col ref in the inner one is correlated.
  EXPECT_EQ(result->DebugString(), R"(SubqueryExpr
+-type=ARRAY<INT64>
+-subquery_type=SCALAR
+-parameter_list=
| +-ColumnRef(type=INT64, column=TestTable.Int64Col#3)
+-subquery=
  +-ProjectScan
    +-column_list=[$expr_subquery.$col1#9]
    +-expr_list=
    | +-$col1#9 :=
    |   +-SubqueryExpr
    |     +-type=ARRAY<INT64>
    |     +-subquery_type=ARRAY
    |     +-parameter_list=
    |     | +-ColumnRef(type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |     +-subquery=
    |       +-ProjectScan
    |         +-column_list=[$array.x#7]
    |         +-input_scan=
    |           +-FilterScan
    |             +-column_list=[$array.x#7, $array_offset.off#8]
    |             +-input_scan=
    |             | +-ArrayScan
    |             |   +-column_list=[$array.x#7, $array_offset.off#8]
    |             |   +-array_expr_list=
    |             |   | +-Literal(type=ARRAY<INT64>, value=[1, 2, 3])
    |             |   +-element_column_list=[$array.x#7]
    |             |   +-array_offset_column=
    |             |     +-ColumnHolder(column=$array_offset.off#8)
    |             +-filter_expr=
    |               +-FunctionCall(GoogleSQL:$less(INT64, INT64) -> BOOL)
    |                 +-parse_location=45-57
    |                 +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    |                 | +-parse_location=45-48
    |                 | +-ColumnRef(type=INT64, column=$array.x#7)
    |                 | +-ColumnRef(type=INT64, column=$array_offset.off#8)
    |                 +-ColumnRef(parse_location=49-57, type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    +-input_scan=
      +-SingleRowScan
)");
}

TEST_F(ExpressionSubstitutorTest, SubstituteLambdaWithCorrelatedArgument) {
  // Run substitute
  // NOTE: x and off arguments of 'mylambda' are correlated ColumnRefs.
  constexpr absl::string_view input_sql = R"sql(
  ARRAY(SELECT x FROM UNNEST([1,2,3]) x WITH OFFSET off
        WHERE (SELECT mylambda(x, off)))
  )sql";
  auto result =
      *AnalyzeSubstitute(options_, catalog_, type_factory_, input_sql, {},
                         /*lambdas=*/{{"mylambda", filter_lambda_}});

  GOOGLESQL_VLOG(1) << result->DebugString();
  // Note: GoogleSQL:$add arguments are correlated.
  EXPECT_EQ(result->DebugString(), R"(SubqueryExpr
+-type=ARRAY<INT64>
+-subquery_type=SCALAR
+-parameter_list=
| +-ColumnRef(type=INT64, column=TestTable.Int64Col#3)
+-subquery=
  +-ProjectScan
    +-column_list=[$expr_subquery.$col1#10]
    +-expr_list=
    | +-$col1#10 :=
    |   +-SubqueryExpr
    |     +-type=ARRAY<INT64>
    |     +-subquery_type=ARRAY
    |     +-parameter_list=
    |     | +-ColumnRef(type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |     +-subquery=
    |       +-ProjectScan
    |         +-column_list=[$array.x#7]
    |         +-input_scan=
    |           +-FilterScan
    |             +-column_list=[$array.x#7, $array_offset.off#8]
    |             +-input_scan=
    |             | +-ArrayScan
    |             |   +-column_list=[$array.x#7, $array_offset.off#8]
    |             |   +-array_expr_list=
    |             |   | +-Literal(type=ARRAY<INT64>, value=[1, 2, 3])
    |             |   +-element_column_list=[$array.x#7]
    |             |   +-array_offset_column=
    |             |     +-ColumnHolder(column=$array_offset.off#8)
    |             +-filter_expr=
    |               +-SubqueryExpr
    |                 +-type=BOOL
    |                 +-subquery_type=SCALAR
    |                 +-parameter_list=
    |                 | +-ColumnRef(type=INT64, column=$array.x#7)
    |                 | +-ColumnRef(type=INT64, column=$array_offset.off#8)
    |                 | +-ColumnRef(type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |                 +-subquery=
    |                   +-ProjectScan
    |                     +-column_list=[$expr_subquery.$col1#9]
    |                     +-expr_list=
    |                     | +-$col1#9 :=
    |                     |   +-FunctionCall(GoogleSQL:$less(INT64, INT64) -> BOOL)
    |                     |     +-parse_location=45-57
    |                     |     +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    |                     |     | +-parse_location=45-48
    |                     |     | +-ColumnRef(type=INT64, column=$array.x#7, is_correlated=TRUE)
    |                     |     | +-ColumnRef(type=INT64, column=$array_offset.off#8, is_correlated=TRUE)
    |                     |     +-ColumnRef(parse_location=49-57, type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |                     +-input_scan=
    |                       +-SingleRowScan
    +-input_scan=
      +-SingleRowScan
)");
}

TEST_F(ExpressionSubstitutorTest, SubstituteErrors) {
  // It's tricky to construct a lambda directly, so we analyze an expression
  // that includes a lambda then use the contained lambda for substitute. This
  // works out well because it's how we also expect substitute to typically be
  // used.
  constexpr absl::string_view sql_with_array_filter = R"sql(
  ( SELECT ARRAY_FILTER([1, 2, 3], (a,b) -> a+b<Int64Col)
    FROM TestTable t )
  )sql";
  std::unique_ptr<const AnalyzerOutput> filter_lambda_output;
  // Disable rewrites to preserve the ResolvedInlineLambdas.
  options_.set_enabled_rewrites({});
  GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
      sql_with_array_filter, options_, &catalog_, &type_factory_,
      types::Int64ArrayType(), &filter_lambda_output));
  const ResolvedFunctionCall* array_filter_call =
      filter_lambda_output->resolved_expr()
          ->GetAs<ResolvedSubqueryExpr>()
          ->subquery()
          ->GetAs<ResolvedProjectScan>()
          ->expr_list(0)
          ->expr()
          ->GetAs<ResolvedFunctionCall>();
  const ResolvedInlineLambda* lambda =
      array_filter_call->generic_argument_list(1)->inline_lambda();

  {
    // Use of undefined variable
    constexpr absl::string_view input_sql = "col2 + 1";
    auto result = AnalyzeSubstitute(options_, catalog_, type_factory_,
                                    input_sql, {{"col1", col1_ref_.get()}});
    ASSERT_THAT(result.status(), StatusIs(absl::StatusCode::kInvalidArgument,
                                          HasSubstr("Unrecognized name")));
  }

  {
    // Attempting to use lambda as a query parameter when undefined produces
    // an error.
    constexpr absl::string_view input_sql = "@lambda AND FALSE";
    auto result =
        AnalyzeSubstitute(options_, catalog_, type_factory_, input_sql,
                          {{"col1", col1_ref_.get()}}, {{"lambda", lambda}});
    ASSERT_THAT(result.status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Query parameter 'lambda' not found")));
  }

  {
    // Lambda not found
    constexpr absl::string_view input_sql =
        "(SELECT lambda2(element) FROM (SELECT 1 as element))";
    auto result =
        AnalyzeSubstitute(options_, catalog_, type_factory_, input_sql,
                          {{"col1", col1_ref_.get()}}, {{"lambda", lambda}});
    ASSERT_THAT(result.status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Function not found: lambda2")));
  }
}

TEST_F(ExpressionSubstitutorTest, MultipleLambdas) {
  // Introduce a second ResolvedInlineLambda, produced using the following
  // SQL snippet that calls ARRAY_TRANSFORM.
  constexpr absl::string_view sql_with_array_transform = R"sql(
  ( SELECT ARRAY_TRANSFORM([1, 2, 3], (e) -> e+20)
    FROM TestTable t )
  )sql";
  // Disable rewrites to preserve the ResolvedInlineLambdas.
  options_.set_enabled_rewrites({});
  GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
      sql_with_array_transform, options_, &catalog_, &type_factory_,
      types::Int64ArrayType(), &transform_lambda_output_));
  const ResolvedFunctionCall* array_transform_call =
      transform_lambda_output_->resolved_expr()
          ->GetAs<ResolvedSubqueryExpr>()
          ->subquery()
          ->GetAs<ResolvedProjectScan>()
          ->expr_list(0)
          ->expr()
          ->GetAs<ResolvedFunctionCall>();
  transform_lambda_ =
      array_transform_call->generic_argument_list(1)->inline_lambda();

  // Test AnalyzeSubstitute on input SQL that contains two lambda expressions.
  constexpr absl::string_view input_sql = R"sql(
  ARRAY(SELECT x FROM UNNEST([1,2,3]) x WITH OFFSET off
        WHERE (SELECT lambda1(x, off)) AND EXISTS(SELECT lambda2(x)))
  )sql";
  auto result = *AnalyzeSubstitute(
      options_, catalog_, type_factory_, input_sql, {},
      /*lambdas=*/
      {{"lambda1", filter_lambda_}, {"lambda2", transform_lambda_}});

  GOOGLESQL_VLOG(1) << result->DebugString();

  EXPECT_EQ(result->DebugString(), R"(SubqueryExpr
+-type=ARRAY<INT64>
+-subquery_type=SCALAR
+-parameter_list=
| +-ColumnRef(type=INT64, column=TestTable.Int64Col#3)
+-subquery=
  +-ProjectScan
    +-column_list=[$expr_subquery.$col1#14]
    +-expr_list=
    | +-$col1#14 :=
    |   +-SubqueryExpr
    |     +-type=ARRAY<INT64>
    |     +-subquery_type=ARRAY
    |     +-parameter_list=
    |     | +-ColumnRef(type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |     +-subquery=
    |       +-ProjectScan
    |         +-column_list=[$array.x#10]
    |         +-input_scan=
    |           +-FilterScan
    |             +-column_list=[$array.x#10, $array_offset.off#11]
    |             +-input_scan=
    |             | +-ArrayScan
    |             |   +-column_list=[$array.x#10, $array_offset.off#11]
    |             |   +-array_expr_list=
    |             |   | +-Literal(type=ARRAY<INT64>, value=[1, 2, 3])
    |             |   +-element_column_list=[$array.x#10]
    |             |   +-array_offset_column=
    |             |     +-ColumnHolder(column=$array_offset.off#11)
    |             +-filter_expr=
    |               +-FunctionCall(GoogleSQL:$and(BOOL, repeated(1) BOOL) -> BOOL)
    |                 +-SubqueryExpr
    |                 | +-type=BOOL
    |                 | +-subquery_type=SCALAR
    |                 | +-parameter_list=
    |                 | | +-ColumnRef(type=INT64, column=$array.x#10)
    |                 | | +-ColumnRef(type=INT64, column=$array_offset.off#11)
    |                 | | +-ColumnRef(type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |                 | +-subquery=
    |                 |   +-ProjectScan
    |                 |     +-column_list=[$expr_subquery.$col1#12]
    |                 |     +-expr_list=
    |                 |     | +-$col1#12 :=
    |                 |     |   +-FunctionCall(GoogleSQL:$less(INT64, INT64) -> BOOL)
    |                 |     |     +-parse_location=45-57
    |                 |     |     +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    |                 |     |     | +-parse_location=45-48
    |                 |     |     | +-ColumnRef(type=INT64, column=$array.x#10, is_correlated=TRUE)
    |                 |     |     | +-ColumnRef(type=INT64, column=$array_offset.off#11, is_correlated=TRUE)
    |                 |     |     +-ColumnRef(parse_location=49-57, type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |                 |     +-input_scan=
    |                 |       +-SingleRowScan
    |                 +-SubqueryExpr
    |                   +-type=BOOL
    |                   +-subquery_type=EXISTS
    |                   +-parameter_list=
    |                   | +-ColumnRef(type=INT64, column=$array.x#10)
    |                   +-subquery=
    |                     +-ProjectScan
    |                       +-column_list=[$expr_subquery.$col1#13]
    |                       +-expr_list=
    |                       | +-$col1#13 :=
    |                       |   +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    |                       |     +-parse_location=46-50
    |                       |     +-ColumnRef(type=INT64, column=$array.x#10, is_correlated=TRUE)
    |                       |     +-Literal(parse_location=48-50, type=INT64, value=20)
    |                       +-input_scan=
    |                         +-SingleRowScan
    +-input_scan=
      +-SingleRowScan
)");
}

TEST_F(ExpressionSubstitutorTest, LambdaNameConflictsWithExistingFunction) {
  // When a named lambda conflicts with a function name in the supplied catalog
  // to AnalyzeSubstitute, precedence is given to the named lambda.
  constexpr absl::string_view input_sql =
      "(SELECT SAFE_DIVIDE(element, element) FROM (SELECT 1 as element))";
  auto result = *AnalyzeSubstitute(options_, catalog_, type_factory_, input_sql,
                                   {{"col1", col1_ref_.get()}},
                                   {{"SAFE_DIVIDE", filter_lambda_}});

  GOOGLESQL_VLOG(1) << result->DebugString();

  EXPECT_EQ(result->DebugString(), R"(SubqueryExpr
+-type=BOOL
+-subquery_type=SCALAR
+-parameter_list=
| +-ColumnRef(type=INT64, column=t.col1#1)
| +-ColumnRef(type=INT64, column=TestTable.Int64Col#3)
+-subquery=
  +-ProjectScan
    +-column_list=[$expr_subquery.$col1#10]
    +-expr_list=
    | +-$col1#10 :=
    |   +-SubqueryExpr
    |     +-type=BOOL
    |     +-subquery_type=SCALAR
    |     +-parameter_list=
    |     | +-ColumnRef(type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |     +-subquery=
    |       +-ProjectScan
    |         +-column_list=[$expr_subquery.$col1#9]
    |         +-expr_list=
    |         | +-$col1#9 :=
    |         |   +-FunctionCall(GoogleSQL:$less(INT64, INT64) -> BOOL)
    |         |     +-parse_location=45-57
    |         |     +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    |         |     | +-parse_location=45-48
    |         |     | +-ColumnRef(type=INT64, column=$subquery2.element#8)
    |         |     | +-ColumnRef(type=INT64, column=$subquery2.element#8)
    |         |     +-ColumnRef(parse_location=49-57, type=INT64, column=TestTable.Int64Col#3, is_correlated=TRUE)
    |         +-input_scan=
    |           +-ProjectScan
    |             +-column_list=[$subquery2.element#8]
    |             +-expr_list=
    |             | +-element#8 := Literal(type=INT64, value=1)
    |             +-input_scan=
    |               +-SingleRowScan
    +-input_scan=
      +-ProjectScan
        +-column_list=[$subquery1.col1#7]
        +-expr_list=
        | +-col1#7 := ColumnRef(type=INT64, column=t.col1#1, is_correlated=TRUE)
        +-input_scan=
          +-SingleRowScan
)");
}

const StructType* MakeStructType(
    absl::Span<const StructType::StructField> fields,
    TypeFactory* type_factory) {
  const StructType* struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      std::vector<StructType::StructField>(fields.begin(), fields.end()),
      &struct_type));
  return struct_type;
}

TEST_F(ExpressionSubstitutorTest, SubstituteWithTargetStructType) {
  // Renaming of fields in STRUCT type
  {
    constexpr absl::string_view input_sql = R"sql(
  STRUCT<a INT64, b STRING>(1, "str")
  )sql";

    // Make a struct with different field names.
    const StructType* struct_type =
        MakeStructType({{"c", types::Int64Type()}, {"d", types::StringType()}},
                       &type_factory_);

    auto result = *AnalyzeSubstitute(
        options_, catalog_, type_factory_, input_sql, {},
        /*lambdas=*/{}, AnnotatedType(struct_type, /*annotation_map=*/nullptr));

    EXPECT_TRUE(result->Is<ResolvedCast>());
    EXPECT_TRUE(result->GetAs<ResolvedCast>()->type()->Equals(struct_type));

    GOOGLESQL_VLOG(1) << result->DebugString();
    EXPECT_EQ(result->DebugString(),
              R"(Cast(STRUCT<a INT64, b STRING> -> STRUCT<c INT64, d STRING>)
+-SubqueryExpr
  +-type=STRUCT<a INT64, b STRING>
  +-subquery_type=SCALAR
  +-subquery=
    +-ProjectScan
      +-column_list=[$expr_subquery.$col1#7]
      +-expr_list=
      | +-$col1#7 := Literal(type=STRUCT<a INT64, b STRING>, value={a:1, b:"str"}, has_explicit_type=TRUE)
      +-input_scan=
        +-SingleRowScan
)");
  }
  // Expression does not have collation, target type has collation.
  {
    constexpr absl::string_view input_sql = R"sql(
  STRUCT<a INT64, b STRING>(1, "str")
  )sql";

    const StructType* struct_type =
        MakeStructType({{"c", types::Int64Type()}, {"d", types::StringType()}},
                       &type_factory_);

    const AnnotationMap* annotation_map_struct_with_string_ci;
    {
      std::unique_ptr<AnnotationMap> annotation_map =
          AnnotationMap::Create(struct_type);
      annotation_map->AsStructMap()
          ->mutable_field(1)
          ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(
          annotation_map_struct_with_string_ci,
          type_factory_.TakeOwnership(std::move(annotation_map)));
    }

    auto result = *AnalyzeSubstitute(
        options_, catalog_, type_factory_, input_sql, {},
        /*lambdas=*/{},
        AnnotatedType(struct_type,
                      /*annotation_map=*/annotation_map_struct_with_string_ci));

    EXPECT_TRUE(result->Is<ResolvedCast>());
    EXPECT_TRUE(result->GetAs<ResolvedCast>()->type()->Equals(struct_type));

    GOOGLESQL_VLOG(1) << result->DebugString();
    EXPECT_EQ(result->DebugString(),
              R"(Cast(STRUCT<a INT64, b STRING> -> STRUCT<c INT64, d STRING>)
+-type_annotation_map=<_,{Collation:"und:ci"}>
+-SubqueryExpr
| +-type=STRUCT<a INT64, b STRING>
| +-subquery_type=SCALAR
| +-subquery=
|   +-ProjectScan
|     +-column_list=[$expr_subquery.$col1#8]
|     +-expr_list=
|     | +-$col1#8 := Literal(type=STRUCT<a INT64, b STRING>, value={a:1, b:"str"}, has_explicit_type=TRUE)
|     +-input_scan=
|       +-SingleRowScan
+-type_modifiers=collation:[_,und:ci]
)");
  }
  // Expression has collation, target type does not have collation
  {
    constexpr absl::string_view input_sql = R"sql(
  STRUCT(1 AS a, COLLATE("str", "und:ci") AS b)
  )sql";

    const StructType* struct_type =
        MakeStructType({{"c", types::Int64Type()}, {"d", types::StringType()}},
                       &type_factory_);

    auto result =
        *AnalyzeSubstitute(options_, catalog_, type_factory_, input_sql, {},
                           /*lambdas=*/{},
                           AnnotatedType(struct_type,
                                         /*annotation_map=*/nullptr));

    EXPECT_TRUE(result->Is<ResolvedCast>());
    EXPECT_TRUE(result->GetAs<ResolvedCast>()->type()->Equals(struct_type));

    GOOGLESQL_VLOG(1) << result->DebugString();
    EXPECT_EQ(result->DebugString(),
              R"(Cast(STRUCT<a INT64, b STRING> -> STRUCT<c INT64, d STRING>)
+-SubqueryExpr
  +-type=STRUCT<a INT64, b STRING>
  +-type_annotation_map=<_,{Collation:"und:ci"}>
  +-subquery_type=SCALAR
  +-subquery=
    +-ProjectScan
      +-column_list=[$expr_subquery.$col1#9<_,{Collation:"und:ci"}>]
      +-expr_list=
      | +-$col1#9 :=
      |   +-MakeStruct
      |     +-type=STRUCT<a INT64, b STRING>
      |     +-type_annotation_map=<_,{Collation:"und:ci"}>
      |     +-field_list=
      |       +-Literal(type=INT64, value=1)
      |       +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
      |         +-type_annotation_map={Collation:"und:ci"}
      |         +-Literal(type=STRING, value="str")
      |         +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
      +-input_scan=
        +-SingleRowScan
)");
  }
  // Expression has collation, target type has distinct collation
  {
    constexpr absl::string_view input_sql = R"sql(
  STRUCT(1 AS a, COLLATE("str", "binary") AS b)
  )sql";

    const StructType* struct_type =
        MakeStructType({{"c", types::Int64Type()}, {"d", types::StringType()}},
                       &type_factory_);

    const AnnotationMap* annotation_map_struct_with_string_ci;
    {
      std::unique_ptr<AnnotationMap> annotation_map =
          AnnotationMap::Create(struct_type);
      annotation_map->AsStructMap()
          ->mutable_field(1)
          ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(
          annotation_map_struct_with_string_ci,
          type_factory_.TakeOwnership(std::move(annotation_map)));
    }

    auto result = *AnalyzeSubstitute(
        options_, catalog_, type_factory_, input_sql, {},
        /*lambdas=*/{},
        AnnotatedType(struct_type,
                      /*annotation_map=*/annotation_map_struct_with_string_ci));

    EXPECT_TRUE(result->Is<ResolvedCast>());
    EXPECT_TRUE(result->GetAs<ResolvedCast>()->type()->Equals(struct_type));

    GOOGLESQL_VLOG(1) << result->DebugString();
    EXPECT_EQ(result->DebugString(),
              R"(Cast(STRUCT<a INT64, b STRING> -> STRUCT<c INT64, d STRING>)
+-type_annotation_map=<_,{Collation:"und:ci"}>
+-SubqueryExpr
| +-type=STRUCT<a INT64, b STRING>
| +-type_annotation_map=<_,{Collation:"binary"}>
| +-subquery_type=SCALAR
| +-subquery=
|   +-ProjectScan
|     +-column_list=[$expr_subquery.$col1#10<_,{Collation:"binary"}>]
|     +-expr_list=
|     | +-$col1#10 :=
|     |   +-MakeStruct
|     |     +-type=STRUCT<a INT64, b STRING>
|     |     +-type_annotation_map=<_,{Collation:"binary"}>
|     |     +-field_list=
|     |       +-Literal(type=INT64, value=1)
|     |       +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
|     |         +-type_annotation_map={Collation:"binary"}
|     |         +-Literal(type=STRING, value="str")
|     |         +-Literal(type=STRING, value="binary", preserve_in_literal_remover=TRUE)
|     +-input_scan=
|       +-SingleRowScan
+-type_modifiers=collation:[_,und:ci]
)");
  }
}

}  // namespace
}  // namespace googlesql
