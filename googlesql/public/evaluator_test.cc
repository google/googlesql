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

#include "googlesql/public/evaluator.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/evaluator_test_table.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/evaluator_base.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/sql_builder.h"
#include "googlesql/testdata/populate_sample_tables.h"
#include "googlesql/testdata/sample_catalog.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/clock.h"

extern absl::Flag<int64_t>
    FLAGS_googlesql_simple_iterator_call_time_now_rows_period;

namespace googlesql {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::UnorderedElementsAre;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ExpressionOptions = ::googlesql::PreparedExpression::ExpressionOptions;

class UDFEvalTest : public ::testing::Test {
 public:
  const int kFunctionId = 5000;

  void SetUp() override {
    catalog_ = std::make_unique<SimpleCatalog>("udf_catalog");
    catalog_->AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    GOOGLESQL_ASSERT_OK(analyzer_options_.AddQueryParameter(
        "param", types::StringType()));
  }

  SimpleCatalog* catalog() const { return catalog_.get(); }

  std::unique_ptr<SimpleCatalog> catalog_;
  AnalyzerOptions analyzer_options_;
  FunctionOptions function_options_;
};

class UDAEvalTest : public ::testing::Test {
 public:
  const int kFunctionId = 1000;

  void SetUp() override {
    catalog_ = std::make_unique<SimpleCatalog>("uda_catalog");
    catalog_->AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    GOOGLESQL_ASSERT_OK(
        analyzer_options_.AddQueryParameter("param", types::StringType()));
  }

  SimpleCatalog* catalog() const {
    return catalog_.get();
  }

  std::unique_ptr<SimpleCatalog> catalog_;
  AnalyzerOptions analyzer_options_;
  FunctionOptions function_options_;
};

TEST(EvaluatorTest, SimpleExpression) {
  PreparedExpression expr("1 + 2");
  EXPECT_EQ(Int64(3), expr.Execute().value());
  EXPECT_EQ(Int64(3), expr.Execute().value());
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
}

TEST(EvaluatorTest, ColumnExpression) {
  PreparedExpression expr("a + b");
  EXPECT_EQ(Int64(3), expr.Execute({{"a", Int64(1)}, {"b", Int64(2)}}).value());
  EXPECT_EQ(Int64(5),
            expr.Execute({{"a", Int64(-1)}, {"b", Int64(6)}}).value());
  EXPECT_THAT(
      expr.Execute({{"a", Int64(-1)}, {"b", Double(6)}}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Expected column parameter 'b' to be of type INT64")));
  // If we call Execute more than once, we must pass in the exact same
  // set of columns.
  EXPECT_THAT(expr.Execute({{"a", Int64(-1)}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Incomplete column parameters")));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
}

TEST(EvaluatorTest, NoRecoveryFromError) {
  PreparedExpression expr("a * 2");
  EXPECT_THAT(expr.Execute({{"a", Value::String("foo")}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("No matching signature")));
  EXPECT_THAT(expr.Execute({{"a", Value::Int64(1)}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression")));
  EXPECT_DEATH(expr.output_type(), "Invalid prepared expression");
}

TEST(EvaluatorTest, BooleanExpression) {
  PreparedExpression expr("(a > 1 AND a < 5) OR b IS NOT NULL");
  EXPECT_EQ(True(), expr.Execute({{"a", Int64(0)}, {"b", Int64(0)}}).value());
  EXPECT_EQ(True(),
            expr.Execute({{"a", Int64(3)}, {"b", NullInt64()}}).value());
  EXPECT_EQ(False(),
            expr.Execute({{"a", Int64(0)}, {"b", NullInt64()}}).value());
  EXPECT_TRUE(types::BoolType()->Equals(expr.output_type()));
}

TEST(EvaluatorTest, ExpressionWithSubquery) {
  PreparedExpression expr("1 + (SELECT a)");
  EXPECT_EQ(Int64(3), expr.Execute({{"a", Int64(2)}}).value());
}

TEST(EvaluatorTest, SubqueryExpression) {
  PreparedExpression expr("(SELECT 1 + a)");
  EXPECT_EQ(Int64(3), expr.Execute({{"a", Int64(2)}}).value());
}

TEST(EvaluatorTest, WithClauseSubquerySimple) {
  const std::string query("(WITH t AS (SELECT 2 as b) SELECT b FROM t)");

  // By default, the AnalyzerOptions used for Prepare/Execute do not enable
  // the LanguageFeature to support WITH clause inside subqueries.
  PreparedExpression expr(query);
  EXPECT_THAT(expr.Execute(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("WITH is not supported on subqueries in this "
                                 "language version")));

  // Prepare the expression with appropriate AnalyzerOptions.
  PreparedExpression expr2(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr2.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr2.Execute();
  EXPECT_THAT(result, IsOkAndHolds(Int64(2)));
}

TEST(EvaluatorTest, WithClauseSubquery) {
  PreparedExpression expr("(WITH t AS (SELECT 2 + @a as b) SELECT b FROM t)");
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddQueryParameter("a", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute({}, {{"a", Int64(2)}});
  EXPECT_THAT(result, IsOkAndHolds(Int64(4)));
}

TEST(EvaluatorTest, WithClauseSubqueryWithLimit) {
  // When a WITH entry is referenced only once, only rows which are needed for
  // the main query are evaluated. This allows the following query to succeed,
  // while the evaluation of all rows in the subquery would trigger a divide-by-
  // zero error.
  const std::string query = R"(ARRAY(
  WITH t AS (SELECT 8 / (5 - n) AS n FROM UNNEST([1, 2, 3, 4, 5]) AS n)
  SELECT n FROM t LIMIT 3
  ))";
  PreparedExpression expr(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, IsOkAndHolds(Array({2.0, 8.0 / 3, 4.0})));
}

TEST(EvaluatorTest, InnerWithClauseSubqueryAndOuterLimit) {
  // Here, we have an extra subquery later so that only the first three rows
  // of t are referenced in the main query, even though the body of the inner
  // with references all of the rows.
  const std::string query = R"(ARRAY(
    WITH u AS (
      WITH t AS (SELECT 8 / (5 - n) AS n FROM UNNEST([1, 2, 3, 4, 5]) AS n)
      SELECT n FROM t
    ) SELECT * FROM u LIMIT 3
  ))";
  PreparedExpression expr(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, IsOkAndHolds(Array({2.0, 8.0 / 3, 4.0})));
}

TEST(EvaluatorTest, WithClauseSubqueryWithLimitAndMultipleRefs) {
  // When a WITH entry is referenced multiple times, the whole thing is
  // evaluated up front, triggering a divide-by-zero error.
  const std::string query = R"(ARRAY(
  WITH
    t1 AS (SELECT 8 / (5 - n) AS n FROM UNNEST([1, 2, 3, 4, 5]) AS n),
    t2 AS (SELECT * FROM t1),
    t3 AS (SELECT * FROM t2)
  SELECT t2.n FROM t2 CROSS JOIN t3 LIMIT 3
  ))";
  PreparedExpression expr(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("division by zero")));
}

TEST(EvaluatorTest, WithClauseSubqueryWithLimitAndRefHasMultipleRefs) {
  // Here, t1 is referenced only from t2, but t2 has multiple references, t3
  // and t4, which are both referenced in the main query. This requires full
  // evaluation of t1, which will trigger divide-by-zero.
  const std::string query = R"(ARRAY(
  WITH
    t1 AS (SELECT 8 / (5 - n) AS n FROM UNNEST([1, 2, 3, 4, 5]) AS n),
    t2 AS (SELECT * FROM t1),
    t3 AS (SELECT * FROM t2),
    t4 AS (SELECT * FROM t2)
  SELECT t3.n FROM t3 CROSS JOIN t4 LIMIT 3
  ))";
  PreparedExpression expr(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("division by zero")));
}

TEST(EvaluatorTest, WithClauseSubqueryWithLimitAndIndirectRef) {
  // Here, t1 is referenced indirectly through t2, but it's still just once
  // reference, so only the first three rows of t1 are ever evaluated.
  const std::string query = R"(ARRAY(
  WITH
    t1 AS (SELECT 8 / (5 - n) AS n FROM UNNEST([1, 2, 3, 4, 5]) AS n),
    t2 AS (SELECT * FROM t1)
  SELECT n FROM t2 LIMIT 3
  ))";
  PreparedExpression expr(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, IsOkAndHolds(Array({2.0, 8.0 / 3, 4.0})));
}

TEST(EvaluatorTest, WithClauseSubqueryWithLimitAndUnreferencedRef) {
  // Here, t1 is referenced by both the main query and t2, but since t2, itself
  // is unreferenced in the main query (the reference from t3 doesn't count
  // since t3 is unreferenced), t1 is considered to have only one reference. So,
  // only the first three rows of t1 are evaluated.
  const std::string query = R"(ARRAY(
  WITH
    t1 AS (SELECT 8 / (5 - n) AS n FROM UNNEST([1, 2, 3, 4, 5]) AS n),
    t2 AS (SELECT * FROM t1),
    t3 AS (SELECT * FROM t2)
  SELECT n FROM t1 LIMIT 3
  ))";
  PreparedExpression expr(query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, IsOkAndHolds(Array({2.0, 8.0 / 3, 4.0})));
}

TEST(EvaluatorTest, WithRecursiveTerminatesDueToLimit) {
  // The recursive query, by itself, does not terminate, but the main query
  // referencing it uses a LIMIT clause, which terminates the recursion when
  // the LIMIT value is reached.
  //
  // As the behavior of LIMIT without ORDER BY is engine-dependent, this is not
  // a compliance test. However, the reference implementation provides
  // deterministic behavior to make the insertion of LIMIT useful for debugging
  // recursive queries. Within the reference implementation we guarantee that:
  // 1) Rows from each iteration come before rows from subsequent iterations
  // 2) The recursion halts when the number of rows in the outer LIMIT clause
  //      is reached (this guarantee only holds when the 'inlined_with_entries'
  //      optimization is enabled, which is the case in the evaluator api).
  for (const char* mode : {"ALL", "DISTINCT"}) {
    SCOPED_TRACE(absl::StrCat("UNION ", mode));
    const std::string query = absl::StrFormat(R"(ARRAY(
    WITH RECURSIVE
      Fib AS (
        SELECT 1 AS a, 1 AS b
        UNION %s
        SELECT b, a + b FROM Fib
    ) SELECT a FROM fib LIMIT 10
    ))",
                                              mode);
    PreparedExpression expr(query);
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
    language_options.EnableLanguageFeature(FEATURE_WITH_RECURSIVE);
    AnalyzerOptions analyzer_options(language_options);
    GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
    const absl::StatusOr<Value> result = expr.Execute();
    EXPECT_THAT(result,
                IsOkAndHolds(Array({Int64(1), Int64(1), Int64(2), Int64(3),
                                    Int64(5), Int64(8), Int64(13), Int64(21),
                                    Int64(34), Int64(55)})));
  }
}

TEST(EvaluatorTest, WithRecursiveNotTerminating) {
  // A non-terminating recursive query should fail cleanly when the rows
  // produced exceed memory limits.
  for (const char* mode : {"ALL", "DISTINCT"}) {
    SCOPED_TRACE(absl::StrCat("UNION ", mode));
    const std::string query = absl::StrFormat(R"(ARRAY(
      WITH RECURSIVE
        t AS (SELECT 1 AS n UNION %s SELECT n + 1 FROM t)
      SELECT * FROM t
    ))",
                                              mode);
    PreparedExpression expr(query);
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
    language_options.EnableLanguageFeature(FEATURE_WITH_RECURSIVE);
    AnalyzerOptions analyzer_options(language_options);
    GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
    const absl::StatusOr<Value> result = expr.Execute();
    EXPECT_THAT(
        result,
        StatusIs(_, HasSubstr("Cannot construct array Value larger than")));
  }
}

TEST(EvaluatorTest, WithRecursiveMemoryExhaustedByHashSet) {
  // This query does not produce any rows. But, the internal hash set of
  // accumulated rows (which is needed to filter duplicates) keeps growing.
  // Make sure that the query is properly aborted when the internal hash set
  // causes the EvaluatorOptions' memory limit to be exceeded.
  const std::string query = R"(ARRAY(
    WITH RECURSIVE
      t AS (SELECT 1 AS n UNION DISTINCT SELECT n + 1 FROM t)
    SELECT * FROM t WHERE n < 0
  ))";
  EvaluatorOptions options;
  options.max_intermediate_byte_size = 1000;
  PreparedExpression expr(query, options);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  language_options.EnableLanguageFeature(FEATURE_WITH_RECURSIVE);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  const absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(
      result,
      StatusIs(_,
               HasSubstr(
                   "Out of memory for "
                   "MemoryAccountant(max_intermediate_byte_size): requested")));
}

TEST(EvaluatorTest, WithClauseSubquery_b119901615) {
  PreparedExpression expr(
      "(WITH a AS (SELECT true as b, 15 as c) SELECT IF(b, c, -1) FROM a)");
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  AnalyzerOptions analyzer_options(language_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  absl::StatusOr<Value> result = expr.Execute();
  EXPECT_THAT(result, IsOkAndHolds(Int64(15)));
}

TEST(EvaluatorTest, QueryAsExpression) {
  PreparedExpression expr("SELECT 1 + a");
  EXPECT_THAT(expr.Execute({{"a", Int64(2)}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unexpected keyword SELECT")));
}

TEST(EvaluatorTest, QueryAsArray) {
  PreparedExpression query("ARRAY(SELECT a UNION ALL SELECT b)");
  Value result =
      query.Execute({{"a", Double(3.0)}, {"b", Double(5.0)}}).value();
  EXPECT_EQ(Array({3.0, 5.0}), result);
}

TEST(EvaluatorTest, ExpressionFromArray) {
  PreparedExpression expr("(SELECT t.y FROM UNNEST(arr) t WHERE t.x = 2)");
  Value arr1 = StructArray({"x", "y"}, {{1ll, 3.0}, {2ll, 5.0}});
  EXPECT_EQ(Double(5.0), expr.Execute({{"arr", arr1}}).value());
  Value arr2 = StructArray({"x", "y"}, {{1ll, 3.0}, {200ll, 5.0}});
  EXPECT_EQ(NullDouble(), expr.Execute({{"arr", arr2}}).value());
  Value arr3 = StructArray({"x", "y"}, {{2ll, 3.0}, {2ll, 5.0}});
  EXPECT_THAT(expr.Execute({{"arr", arr3}}),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("More than one element")));
}

TEST(EvaluatorTest, ArrayExpressionOnHeap) {
  PreparedExpression* expr =
      new PreparedExpression(
          "ARRAY(SELECT AS STRUCT 1,2 UNION ALL SELECT AS STRUCT 3,4)");
  EXPECT_EQ(StructArray({"", ""}, {{1ll, 2ll}, {3ll, 4ll}}),
            expr->Execute().value());
  delete expr;
}

TEST(EvaluatorTest, ExpressionWithSystemVariables) {
  PreparedExpression expr("(@@sysvar1 + @@sysvar2.foo) * col + @@sysvar1");

  SystemVariableValuesMap system_variables;
  system_variables[{"sysvar1"}] = Value::Int64(3);
  system_variables[{"sysvar2", "foo"}] = Value::Int64(8);

  Value result =
      expr.Execute({{"col", Value::Int64(5)}}, {}, system_variables).value();
  EXPECT_EQ(Value::Int64(58), result);
}

TEST(EvaluatorTest, ExpressionWithUnusedSystemVariables) {
  PreparedExpression expr("(@@row_count)");

  SystemVariableValuesMap system_variables;
  system_variables[{"row_count"}] = Value::Int64(3);
  system_variables[{"error", "message"}] = Value::Int64(8);

  Value result =
      expr.Execute({{"col", Value::Int64(5)}}, {}, system_variables).value();
  EXPECT_EQ(Value::Int64(3), result);
}

TEST(EvaluatorTest, ExpressionWithUnusedSystemVariables2) {
  PreparedExpression expr("(@@sysvar1 + @@sysvar2.foo) * col");

  SystemVariableValuesMap system_variables;
  system_variables[{"sysvar1"}] = Value::Int64(3);
  system_variables[{"sysvar2", "foo"}] = Value::Int64(8);
  system_variables[{"sysvar_unused"}] = Value::Int64(22);

  Value result =
      expr.Execute({{"col", Value::Int64(5)}}, {}, system_variables).value();
  EXPECT_EQ(Value::Int64(55), result);
}

TEST(EvaluatorTest, ExpressionWithSystemVariablesAndQueryParameters) {
  PreparedExpression expr("(@@sysvar1 + @@sysvar2.foo + @param1) * col");

  SystemVariableValuesMap system_variables;
  system_variables[{"sysvar1"}] = Value::Int64(3);
  system_variables[{"sysvar2", "foo"}] = Value::Int64(8);

  Value result = expr.Execute({{"col", Value::Int64(5)}},
                              {{"param1", Value::Int64(1)}}, system_variables)
                     .value();
  EXPECT_EQ(Value::Int64(60), result);
}

TEST(EvaluatorTest, ExpressionWithSystemVariablesAndPositionalQueryParameters) {
  PreparedExpression expr("(@@sysvar1 + @@sysvar2.foo + ? + ?) * col");

  SystemVariableValuesMap system_variables;
  system_variables[{"sysvar1"}] = Value::Int64(3);
  system_variables[{"sysvar2", "foo"}] = Value::Int64(8);

  Value result = expr.ExecuteWithPositionalParams(
                         {{"col", Value::Int64(5)}},
                         std::vector<Value>{Value::Int64(1), Value::Int64(2)},
                         system_variables)
                     .value();
  EXPECT_EQ(Value::Int64(70), result);
}

TEST(EvaluatorTest, ExpressionWithQueryParameters) {
  PreparedExpression expr("(@param1 + @param2) * col");
  Value result =
      expr.Execute({{"col", Value::Int64(5)}},
                   {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}})
          .value();
  EXPECT_EQ(Value::Int64(15), result);
}

TEST(EvaluatorTest, ExpressionWithPositionalQueryParameters) {
  PreparedExpression expr("(? + ?) * col");
  Value result = expr.ExecuteWithPositionalParams(
                         {{"col", Value::Int64(5)}},
                         std::vector<Value>{Value::Int64(1), Value::Int64(2)})
                     .value();
  EXPECT_EQ(Value::Int64(15), result);
}

TEST(EvaluatorTest, Prepare) {
  PreparedExpression expr("(@param1 + @param2) * col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  Value result =
      expr.Execute({{"col", Value::Int64(5)}},
                   {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}})
          .value();
  EXPECT_EQ(Value::Int64(15), result);
}

TEST(EvaluatorTest, PrepareWithSystemVariables) {
  PreparedExpression expr(
      "(@@sysvar1 + @@sysvar2 + @@sysvar1) * col - @param1");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar1"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar2"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

  SystemVariableValuesMap sysvar_values;
  sysvar_values[{"sysvar1"}] = Value::Int64(12);
  sysvar_values[{"sysvar2"}] = Value::Int64(10);

  Value result =
      expr.Execute({{"col", Value::Int64(5)}},
                   {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}},
                   sysvar_values)
          .value();
  EXPECT_EQ(Value::Int64(169), result);
}

TEST(EvaluatorTest, PrepareWithPositionalParameters) {
  PreparedExpression expr("(? + ?) * col");
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

  Value result =
      expr.ExecuteWithPositionalParams({{"col", Value::Int64(5)}},
                                       {Value::Int64(1), Value::Int64(2)})
          .value();
  EXPECT_EQ(Value::Int64(15), result);
}

TEST(EvaluatorTest, PrepareWithPositionalParametersAndSystemVariables) {
  PreparedExpression expr("(? + ?) * col + @@sysvar1 + @@sysvar2");
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar1"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar2"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

  SystemVariableValuesMap sysvar_values;
  sysvar_values[{"sysvar1"}] = Value::Int64(12);
  sysvar_values[{"sysvar2"}] = Value::Int64(10);

  Value result = expr.ExecuteWithPositionalParams(
                         {{"col", Value::Int64(5)}},
                         {Value::Int64(1), Value::Int64(2)}, sysvar_values)
                     .value();
  EXPECT_EQ(Value::Int64(37), result);
}

TEST(EvaluatorTest, ExecuteAfterPrepare) {
  PreparedExpression expr("(@param1 + @param2) * col + @@sysvar1");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar1"}, types::Int64Type()));
  // Check that ExecuteAfterPrepare gives an error if Prepare has not yet been
  // called.
  EXPECT_THAT(expr.ExecuteAfterPrepare(
                  {{"col", Value::Int64(5)}},
                  {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}},
                  {{{"sysvar1"}, Value::Int64(10)}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression")));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

  // Call ExecuteAfterPrepare against a const object: the point of having this
  // method is so it can be const, and this protects against regressions should
  // it be changed in future.
  const PreparedExpression* const_expr = &expr;
  Value result = const_expr
                     ->ExecuteAfterPrepare({{"col", Value::Int64(5)}},
                                           {{"param1", Value::Int64(1)},
                                            {"param2", Value::Int64(2)}},
                                           {{{"sysvar1"}, Value::Int64(10)}})
                     .value();
  EXPECT_EQ(Value::Int64(25), result);

  // Check that ExecuteAfterPrepare gives an error if Prepare has been called,
  // but failed.
  PreparedExpression expr2("(@param1 + @param2) * col");
  AnalyzerOptions options2;
  GOOGLESQL_ASSERT_OK(options2.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options2.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options2.AddExpressionColumn("col", types::StringType()));
  EXPECT_THAT(expr2.Prepare(options2),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("No matching signature for operator *")));
  EXPECT_THAT(expr2.ExecuteAfterPrepare(
                  {{"col", Value::Int64(5)}},
                  {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression")));
}

TEST(EvaluatorTest, ExecuteAfterPrepareWithPositionalParameters) {
  PreparedExpression expr("(? + ?) * col");
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  // Check that ExecuteAfterPrepare gives an error if Prepare has not yet been
  // called.
  EXPECT_THAT(
      expr.ExecuteAfterPrepareWithPositionalParams(
          {{"col", Value::Int64(5)}}, {Value::Int64(1), Value::Int64(2)}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid prepared expression")));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

  // Call ExecuteAfterPrepare against a const object: the point of having this
  // method is so it can be const, and this protects against regressions should
  // it be changed in future.
  const PreparedExpression* const_expr = &expr;
  Value result =
      const_expr
          ->ExecuteAfterPrepareWithPositionalParams(
              {{"col", Value::Int64(5)}}, {Value::Int64(1), Value::Int64(2)})
          .value();
  EXPECT_EQ(Value::Int64(15), result);

  // Check that ExecuteAfterPrepare gives an error if Prepare has been called,
  // but failed.
  PreparedExpression expr2("(? + ?) * col");
  AnalyzerOptions options2;
  options2.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options2.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options2.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options2.AddExpressionColumn("col", types::StringType()));
  EXPECT_THAT(expr2.Prepare(options2),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("No matching signature for operator *")));
  EXPECT_THAT(
      expr2.ExecuteAfterPrepareWithPositionalParams(
          {{"col", Value::Int64(5)}}, {Value::Int64(1), Value::Int64(2)}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid prepared expression")));
}

// Regression test for b/372558941 where parameters were declared in
// analyzer_options, but never used in the expression and the algebrizer slotted
// the system variables at the very beginning of the params schema. However,
// during execution, the positional params are provided so the system variable
// reads from the slots of the positional parameters which the algebrizer did
// not account for.
TEST(EvaluatorTest,
     ExecuteAfterPrepareWithPositionalParametersAndSystemVariables) {
  PreparedExpression expr("CONCAT(col, 'b', @@sys_var)");
  AnalyzerOptions analyzer_options;
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddPositionalQueryParameter(types::DateType()));
  GOOGLESQL_ASSERT_OK(analyzer_options.AddPositionalQueryParameter(types::DateType()));
  GOOGLESQL_ASSERT_OK(
      analyzer_options.AddSystemVariable({"sys_var"}, types::StringType()));
  GOOGLESQL_ASSERT_OK(analyzer_options.AddExpressionColumn("col", types::StringType()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  EXPECT_TRUE(types::StringType()->Equals(expr.output_type()));

  // Call ExecuteAfterPrepare against a const object: the point of having this
  // method is so it can be const, and this protects against regressions should
  // it be changed in future.
  ExpressionOptions expression_options;
  expression_options.columns = {{"col", Value::String("aaaa")}};
  expression_options.system_variables = {
      {{"sys_var"}, Value::String("google")}};
  expression_options.ordered_parameters = {Value::Date(1), Value::Date(2)};

  const PreparedExpression* const_expr = &expr;
  Value result = const_expr->ExecuteAfterPrepare(expression_options).value();
  EXPECT_EQ(Value::String("aaaabgoogle"), result);
}

// Regression test for b/372558941 where parameters were declared in
// analyzer_options, but never used in the expression and the algebrizer slotted
// the system variables at the very beginning of the params schema. However,
// during execution, the named params are provided so the system variable reads
// from the slots of the named parameters which the algebrizer did not account
// for. Named parameters were always handled correctly but this test protects
// against future regressions.
TEST(EvaluatorTest, ExecuteAfterPrepareWithNamedParametersAndSystemVariables) {
  PreparedExpression expr("CONCAT(col, 'b', @@sys_var)");
  AnalyzerOptions analyzer_options;
  analyzer_options.set_parameter_mode(PARAMETER_NAMED);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddQueryParameter("p1", types::DateType()));
  GOOGLESQL_ASSERT_OK(analyzer_options.AddQueryParameter("p2", types::DateType()));
  GOOGLESQL_ASSERT_OK(
      analyzer_options.AddSystemVariable({"sys_var"}, types::StringType()));
  GOOGLESQL_ASSERT_OK(analyzer_options.AddExpressionColumn("col", types::StringType()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  EXPECT_TRUE(types::StringType()->Equals(expr.output_type()));

  // Call ExecuteAfterPrepare against a const object: the point of having this
  // method is so it can be const, and this protects against regressions should
  // it be changed in future.
  ExpressionOptions expression_options;
  expression_options.columns = {{"col", Value::String("aaaa")}};
  expression_options.system_variables = {
      {{"sys_var"}, Value::String("google")}};
  expression_options.parameters = {{"p1", Value::Date(1)},
                                   {"p2", Value::Date(2)}};

  const PreparedExpression* const_expr = &expr;
  Value result = const_expr->ExecuteAfterPrepare(expression_options).value();
  EXPECT_EQ(Value::String("aaaabgoogle"), result);
}

TEST(EvaluatorTest, PrepareFailOnAnalysis) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::StringType()));
  EXPECT_THAT(expr.Prepare(options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("No matching signature")));
}

TEST(EvaluatorTest, PrepareExecuteExtraQueryParameter) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  Value result =
      expr.Execute({{"col", Value::Int64(5)}},
                   {{"param", Value::Int64(6)}, {"param2", Value::Int64(8)}})
          .value();
  EXPECT_EQ(result, Value::Int64(11));
}

TEST(EvaluatorTest, PrepareExecuteMissingQueryParameter) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  EXPECT_THAT(expr.Prepare(options),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Query parameter 'param' not found")));
}

TEST(EvaluatorTest, PrepareExecuteAllowUndeclaredQueryParameters) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> params,
                       expr.GetReferencedParameters());
  EXPECT_THAT(params, testing::ElementsAre("param"));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result,
      expr.ExecuteAfterPrepare(
          {{"col", Value::Int64(5)}}, {{"param", Value::Int64(6)}}));
  EXPECT_EQ(result, Value::Int64(11));
}

TEST(EvaluatorTest, PrepareExecuteAllowUndeclaredQueryParametersResolvedExpr) {
  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddExpressionColumn("col", types::Int64Type()));

  auto catalog = std::make_unique<SimpleCatalog>("foo");
  catalog->AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  TypeFactory type_factory;

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression(
      "@param + col", analyzer_options,
      catalog.get(), &type_factory, &analyzer_output));

  EvaluatorOptions evaluator_options;
  PreparedExpression expr(analyzer_output->resolved_expr(),
                          evaluator_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> params,
                       expr.GetReferencedParameters());
  EXPECT_THAT(params, testing::ElementsAre("param"));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  EXPECT_THAT(
      expr.ExecuteAfterPrepare(
          {{"col", Value::Int64(5)}}, {{"param", Value::Int64(6)}}),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Expected type not found for variable param")));
}

TEST(EvaluatorTest, PrepareExecuteWrongQueryParameterType) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  absl::Status status = expr.Execute(
      {{"col", Value::Int64(5)}}, {{"param", Value::String("foo")}}).status();
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Expected query parameter 'param' to be of type INT64")));
}

TEST(EvaluatorTest, PrepareExecuteMissingPositionalQueryParameter) {
  PreparedExpression expr("? + col");
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  EXPECT_THAT(expr.Prepare(options),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Query parameter number 1 is not defined")));
}

TEST(EvaluatorTest, PrepareExecuteAllowUndeclaredPositionalQueryParameters) {
  PreparedExpression expr("CONCAT(CAST((? + col) AS STRING), ?)");
  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  EXPECT_TRUE(types::StringType()->Equals(expr.output_type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(int count, expr.GetPositionalParameterCount());
  EXPECT_EQ(2, count);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result,
      expr.ExecuteAfterPrepareWithPositionalParams(
          {{"col", Value::Int64(5)}}, {Value::Int64(6),
                                        Value::StringValue("foo")}));
  EXPECT_EQ(result, Value::StringValue("11foo"));
}

TEST(EvaluatorTest,
     PrepareExecuteAllowUndeclaredPositionalQueryParametersResolvedExpr) {
  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddExpressionColumn("col", types::Int64Type()));

  auto catalog = std::make_unique<SimpleCatalog>("foo");
  catalog->AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  TypeFactory type_factory;

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression(
      "CONCAT(CAST((? + col) AS STRING), ?)", analyzer_options,
      catalog.get(), &type_factory, &analyzer_output));

  EvaluatorOptions evaluator_options;
  PreparedExpression expr(analyzer_output->resolved_expr(), evaluator_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  EXPECT_TRUE(types::StringType()->Equals(expr.output_type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(int count, expr.GetPositionalParameterCount());
  EXPECT_EQ(2, count);
  EXPECT_THAT(
      expr.ExecuteAfterPrepareWithPositionalParams(
          {{"col", Value::Int64(5)}}, {Value::Int64(6),
                                        Value::StringValue("foo")}),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Mismatch in number of analyzer parameters versus "
                         "algebrizer parameters")));
}

TEST(EvaluatorTest, PrepareExecuteWrongPositionalQueryParameterType) {
  PreparedExpression expr("? + col");
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  absl::Status status =
      expr.ExecuteWithPositionalParams({{"col", Value::Int64(5)}},
                                       {Value::String("foo")})
          .status();
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Expected positional parameter 1 to be of type INT64")));
}

TEST(EvaluatorTest, PrepareExecuteWrongSystemVariableType) {
  PreparedExpression expr("@@sysvar + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  absl::Status status = expr.Execute({{"col", Value::Int64(5)}}, {},
                                     {{{"sysvar"}, Value::String("foo")}})
                            .status();
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Expected system variable 'sysvar' to be of type INT64")));
}

TEST(EvaluatorTest, PrepareExecuteSystemVariableMissingValue) {
  PreparedExpression expr("@@sysvar + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  absl::Status status = expr.Execute({{"col", Value::Int64(5)}}, {}).status();
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("No value provided for system variable sysvar")));
}

TEST(EvaluatorTest, PrepareExecuteSystemMissingInAnalyzerOptions) {
  // @@sysvar2 has a value, but isn't in the analyzer options.
  // Even though @@sysvar2 is not used in the query, this is not allowed.
  PreparedExpression expr("@@sysvar1 + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddSystemVariable({"sysvar1"}, types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  absl::Status status = expr.Execute({{"col", Value::Int64(5)}}, {},
                                     {{{"sysvar1"}, Value::Int64(5)},
                                      {{"sysvar2"}, Value::Int64(6)}})
                            .status();
  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Value provided for system variable sysvar2, "
                                 "which is not in the AnalyzerOptions")));
}

TEST(EvaluatorTest, PrepareExecuteMismatchedPositionalQueryParameterCount) {
  {
    // Too few positional parameters to Prepare.
    PreparedExpression expr("? + ? + col");
    AnalyzerOptions options;
    options.set_parameter_mode(PARAMETER_POSITIONAL);
    GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
    GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
    EXPECT_THAT(
        expr.Prepare(options),
        StatusIs(
            absl::StatusCode::kInvalidArgument,
            HasSubstr("Query parameter number 2 is not defined (1 provided)")));
  }

  {
    // Too many positional parameters to Prepare (not an error; the analyzer
    // doesn't raise an error for extra parameters).
    PreparedExpression expr("? + col");
    AnalyzerOptions options;
    options.set_parameter_mode(PARAMETER_POSITIONAL);
    GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
    GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
    GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
    GOOGLESQL_EXPECT_OK(expr.Prepare(options));
  }

  {
    // Too few positional parameters to Execute.
    PreparedExpression expr("? + ? + 1");
    EXPECT_THAT(
        expr.ExecuteWithPositionalParams({}, {Value::Int64(3)}).status(),
        StatusIs(
            absl::StatusCode::kInvalidArgument,
            HasSubstr("Query parameter number 2 is not defined (1 provided)")));
  }

  {
    // More positional parameters than needed; should be allowed.
    PreparedExpression expr("? + ? + 1");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Value result,
        expr.ExecuteWithPositionalParams(
            {}, {Value::Int64(3), Value::Int64(4), Value::Int64(5)}));
    EXPECT_EQ(Value::Int64(8), result);
  }
}

TEST(EvaluatorTest, PrepareExecuteSubexpressionsWithPositionalQueryParameters) {
  const ParameterValueList positional_parameters =
      {Value::Int64(1), Value::Int64(2), Value::Int64(4), Value::Int64(8)};

  AnalyzerOptions analyzer_options;
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  for (const Value& param : positional_parameters) {
    GOOGLESQL_ASSERT_OK(analyzer_options.AddPositionalQueryParameter(param.type()));
  }
  auto catalog = std::make_unique<SimpleCatalog>("foo");
  catalog->AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  TypeFactory type_factory;

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression("(? + ?) + (? + ?)", analyzer_options,
                              catalog.get(), &type_factory, &analyzer_output));
  const ResolvedExpr* resolved_expr = analyzer_output->resolved_expr();
  ASSERT_TRUE(resolved_expr != nullptr);
  ASSERT_EQ(RESOLVED_FUNCTION_CALL, resolved_expr->node_kind());
  const auto* resolved_function_call =
      resolved_expr->GetAs<ResolvedFunctionCall>();
  ASSERT_EQ(2, resolved_function_call->argument_list_size());

  EvaluatorOptions options;
  options.type_factory = &type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result1,
      PreparedExpression(resolved_function_call->argument_list(0), options)
          .ExecuteWithPositionalParams({}, positional_parameters));
  EXPECT_EQ(Value::Int64(3), result1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result2,
      PreparedExpression(resolved_function_call->argument_list(1), options)
          .ExecuteWithPositionalParams({}, positional_parameters));
  EXPECT_EQ(Value::Int64(12), result2);
}

TEST(EvaluatorTest, PrepareExecuteSubexpressionsWithNamedQueryParameters) {
  const ParameterValueMap parameters =
      {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)},
       {"param4", Value::Int64(4)}, {"param8", Value::Int64(8)}};

  AnalyzerOptions analyzer_options;
  analyzer_options.set_parameter_mode(PARAMETER_NAMED);
  for (const auto& entry : parameters) {
    GOOGLESQL_ASSERT_OK(analyzer_options.AddQueryParameter(
        entry.first, entry.second.type()));
  }
  auto catalog = std::make_unique<SimpleCatalog>("foo");
  catalog->AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  TypeFactory type_factory;

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression("(@param1 + @param2) + (@param4 + @param8)",
                              analyzer_options, catalog.get(), &type_factory,
                              &analyzer_output));
  const ResolvedExpr* resolved_expr = analyzer_output->resolved_expr();
  ASSERT_TRUE(resolved_expr != nullptr);
  ASSERT_EQ(RESOLVED_FUNCTION_CALL, resolved_expr->node_kind());
  const auto* resolved_function_call =
      resolved_expr->GetAs<ResolvedFunctionCall>();
  ASSERT_EQ(2, resolved_function_call->argument_list_size());

  EvaluatorOptions options;
  options.type_factory = &type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result1,
      PreparedExpression(resolved_function_call->argument_list(0), options)
          .Execute({}, parameters));
  EXPECT_EQ(Value::Int64(3), result1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result2,
      PreparedExpression(resolved_function_call->argument_list(1), options)
          .Execute({}, parameters));
  EXPECT_EQ(Value::Int64(12), result2);
}

TEST(EvaluatorTest, PrepareExecuteWrongColumnParameterType) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  absl::Status status = expr.Execute(
      {{"col", Value::String("foo")}}, {{"param", Value::Int64(1)}}).status();
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Expected column parameter 'col' to be of type INT64")));
}

TEST(EvaluatorTest, PrepareExecuteWithTable) {
  SimpleTable test_table(
      "TestTable", {{"a", types::Int64Type()}, {"b", types::StringType()}});
  test_table.SetContents({{Int64(10), String("foo")},
                          {Int64(20), String("bar")},
                          {Int64(30), String("baz")}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  PreparedExpression expr("(SELECT a FROM TestTable ORDER BY a LIMIT 1)");
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions(), &catalog));
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Value::Int64(10)));
}

TEST(EvaluatorTest, PrepareTwice) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));
  EXPECT_THAT(expr.Prepare(options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Prepare called twice")));
}

TEST(EvaluatorTest, ExecuteAfterPrepareWithOrderedParamsWithQueryParameters) {
  PreparedExpression expr("(@param1 + @param2) * col");

  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_NAMED);
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));

  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_THAT(expr.GetReferencedColumns(), IsOkAndHolds(ElementsAre("col")));
  EXPECT_THAT(expr.GetReferencedParameters(),
              IsOkAndHolds(UnorderedElementsAre("param2", "param1")));
  EXPECT_THAT(expr.ExecuteAfterPrepareWithOrderedParams(
                  {Value::Int64(5)}, {Value::Int64(1), Value::Int64(2)}),
              IsOkAndHolds(Value::Int64(15)));
}

TEST(EvaluatorTest,
     ExecuteAfterPrepareWithOrderedParamsWithPositionalQueryParameters) {
  PreparedExpression expr("(? + ?) * col");

  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));

  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_THAT(expr.GetReferencedColumns(), IsOkAndHolds(ElementsAre("col")));
  EXPECT_THAT(expr.GetPositionalParameterCount(), IsOkAndHolds(2));
  EXPECT_THAT(expr.ExecuteAfterPrepareWithOrderedParams(
                  {Value::Int64(5)}, {Value::Int64(1), Value::Int64(2)}),
              IsOkAndHolds(Value::Int64(15)));
}

TEST(EvaluatorTest, ExplainAfterPrepareWithoutPrepare) {
  PreparedExpression expr("@param + col");
  EXPECT_THAT(expr.ExplainAfterPrepare(),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Prepare must be called first")));
}

TEST(EvaluatorTest, ExplainAfterPrepare) {
  PreparedExpression expr("@param + col");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_THAT(expr.ExplainAfterPrepare(), IsOkAndHolds("Add($param, $col)"));
}

TEST(EvaluatorTest, GetReferencedParametersAsProperSubset) {
  PreparedExpression expr("@param1 + @param2");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param3", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_parameters = expr.GetReferencedParameters();
  GOOGLESQL_ASSERT_OK(status_or_parameters);
  EXPECT_THAT(std::move(status_or_parameters).value(),
              UnorderedElementsAre("param1", "param2"));

  GOOGLESQL_ASSERT_OK(expr.Execute({}, {{"param1", values::Int64(1)},
        {"param2", values::Int64(2)}}));
}

TEST(EvaluatorTest, GetPositionalParameterCount) {
  PreparedExpression expr("? + ? + ?");
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  EXPECT_THAT(expr.GetPositionalParameterCount(),
              ::absl_testing::IsOkAndHolds(::testing::Eq(3)));

  EXPECT_THAT(expr.ExecuteWithPositionalParams(
                  {}, {values::Int64(1), values::Int64(2), values::Int64(3)}),
              ::absl_testing::IsOkAndHolds(::testing::Eq(values::Int64(6))));
}

TEST(EvaluatorTest, GetReferencedParametersWithMixedCases) {
  PreparedExpression expr("@PARAM");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("pArAm", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_parameters = expr.GetReferencedParameters();
  GOOGLESQL_ASSERT_OK(status_or_parameters);
  EXPECT_THAT(std::move(status_or_parameters).value(),
              UnorderedElementsAre("param"));
  GOOGLESQL_ASSERT_OK(expr.Execute({}, {{"PaRaM", values::Int64(1)}}));
}

TEST(EvaluatorTest, PrepareMismatchColumns) {
  PreparedExpression expr("col0 + col1");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col0", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col2", types::Int64Type()));
  // Set of columns involved in expression must be a subset of those
  // visible to Prepare().
  EXPECT_THAT(expr.Prepare(options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: col1")));
}

TEST(EvaluatorTest, GetReferencedColumnsAsProperSubset) {
  PreparedExpression expr("col0 + col1");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col0", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col0", "col1"));

  auto status_or_value = expr.Execute({{"col0", values::Int64(1)},
    {"col1", values::Int64(2)}});
  GOOGLESQL_ASSERT_OK(status_or_value);
}

TEST(EvaluatorTest, GetReferencedColumnsInMixedCases) {
  PreparedExpression expr("cOl");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("COL", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col"));
  GOOGLESQL_ASSERT_OK(expr.Execute({{"CoL", values::Int64(1)}}));
}

TEST(EvaluatorTest, GetReferencedColumnsUsingCallback) {
  PreparedExpression expr("col");
  AnalyzerOptions options;
  options.SetLookupExpressionColumnCallback(
      [](absl::string_view column_name, const Type** column_type) {
        if (column_name == "col") {
          *column_type = types::Int64Type();
        }
        return absl::OkStatus();
      });
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col"));
  Value result = expr.Execute({{"col", values::Int64(1)}}).value();
  EXPECT_EQ(Value::Int64(1), result);
}

TEST(EvaluatorTest, GetReferencedColumnsUsingCallbackInMixedCases) {
  PreparedExpression expr("cOl");
  AnalyzerOptions options;
  options.SetLookupExpressionColumnCallback(
      [](absl::string_view column_name, const Type** column_type) {
        if (column_name == "col") {
          *column_type = types::Int64Type();
        }
        return absl::OkStatus();
      });
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col"));
  Value result = expr.Execute({{"CoL", values::Int64(1)}}).value();
  EXPECT_EQ(Value::Int64(1), result);
}

TEST(EvaluatorTest, GetReferencedColumnsThatAreNotAdded) {
  PreparedExpression expr("col0 + col1");
  GOOGLESQL_ASSERT_OK(expr.Execute({{"col0", Int64(1)}, {"col1", Int64(2)}}));
  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col0", "col1"));
}

TEST(EvaluatorTest, GetReferencedColumnsFromExpressionsWithoutColumn) {
  PreparedExpression expr("@param = 2");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_TRUE(std::move(status_or_columns).value().empty());
}

TEST(EvaluatorTest, GetReferencedInScopeColumns) {
  TypeFactory type_factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  PreparedExpression expr("col0.int64_key_1 + int64_key_2");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.SetInScopeExpressionColumn("col0", proto_type));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col0"));
}

TEST(EvaluatorTest, GetReferencedColumnsWithUnreferencedInScopeColumns) {
  TypeFactory type_factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  PreparedExpression expr("col1 = 1");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.SetInScopeExpressionColumn("col0", proto_type));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col1"));
}

TEST(EvaluatorTest, GetReferencedColumnsInLowerCase) {
  PreparedExpression expr("col0 + COL1 + cOL2");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("col0", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("COL1", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("cOL2", types::Int64Type()));
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_THAT(std::move(status_or_columns).value(),
              UnorderedElementsAre("col0", "col1", "col2"));
}

TEST(EvaluatorTest, GetReferencedColumnsWithoutPrepare) {
  PreparedExpression expr("@param + col");
  EXPECT_THAT(expr.GetReferencedColumns(),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Expression/Query has not been prepared")));
}

TEST(EvaluatorTest, GetReferencedColumnsAfterPrepareFailure) {
  PreparedExpression expr("col0 col1");
  AnalyzerOptions options;  // Empty options
  EXPECT_FALSE(expr.Prepare(options).ok());
  EXPECT_THAT(expr.GetReferencedColumns(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

TEST(EvaluatorTest, GetReferencedColumnsFromConstantExpression) {
  PreparedExpression expr("1 + 1");
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(expr.Prepare(options));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  EXPECT_TRUE(std::move(status_or_columns).value().empty());
}

TEST(EvaluatorTest, GetReferencedColumnsAfterExecute) {
  PreparedExpression expr("col1 + col2");
  GOOGLESQL_ASSERT_OK(expr.Execute(
      {{"col1", values::Int64(1)}, {"col2", values::Int64(2)}}));

  auto status_or_columns = expr.GetReferencedColumns();
  GOOGLESQL_ASSERT_OK(status_or_columns);
  std::vector<std::string> columns = std::move(status_or_columns).value();
  EXPECT_THAT(columns, UnorderedElementsAre("col1", "col2"));
  // Execute again with the obtained subset.
  ParameterValueMap column_value_map;
  for (const auto& column : columns) {
    column_value_map[column] = values::Int64(1);
  }
  GOOGLESQL_ASSERT_OK(expr.Execute(column_value_map));
}

TEST(EvaluatorTest, ExpressionValueColumn) {
  TypeFactory type_factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  googlesql_test::KitchenSinkPB input_value;
  input_value.set_int64_key_1(5);
  input_value.set_int64_key_2(10);

  // Test using a named in-scope expression column (and a regular expression
  // column).
  {
    PreparedExpression expr(
        "int64_key_1 + value.int64_key_2 + if(has_date, 100, 0) + int_value");

    AnalyzerOptions options;
    GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("int_value", types::Int32Type()));
    GOOGLESQL_ASSERT_OK(options.SetInScopeExpressionColumn("value", proto_type));
    GOOGLESQL_ASSERT_OK(expr.Prepare(options));
    EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

    Value result =
        expr.Execute({{"int_value", values::Int32(1000)},
                      {"value", values::Proto(proto_type, input_value)}})
            .value();
    EXPECT_EQ(Value::Int64(1015), result);
  }

  // Test using an anonymous in-scope expression column.
  {
    PreparedExpression expr(
        "int64_key_1 + int64_key_2 + if(has_date, 0, 100)");

    AnalyzerOptions options;
    GOOGLESQL_ASSERT_OK(options.SetInScopeExpressionColumn("", proto_type));
    GOOGLESQL_ASSERT_OK(expr.Prepare(options));
    EXPECT_TRUE(types::Int64Type()->Equals(expr.output_type()));

    Value result =
        expr.Execute({{"", values::Proto(proto_type, input_value)}}).value();
    EXPECT_EQ(Value::Int64(115), result);
  }

  // Test using anonymous in-scope expression column with implicit Prepare.
  {
    PreparedExpression expr(
        "int64_key_1 + int64_key_2 + if(has_date, 0, 100) + int_value");

    Value result = expr.Execute({{"", values::Proto(proto_type, input_value)},
                                 {"int_value", values::Int32(1000)}})
                       .value();
    EXPECT_EQ(Value::Int64(1115), result);
  }
}

TEST(EvaluatorTest, UnpreparedUnexecutedOutputType) {
  PreparedExpression expr("1 + 2");
  EXPECT_DEATH(expr.output_type(), "Prepare or Execute must be called first");
}

// This tests that if we provide an external TypeFactory, the returned
// value will still be valid after the PreparedExpression is deleted.
TEST(EvaluatorTest, ValueLifetime) {
  TypeFactory type_factory;
  Value value;
  {
    PreparedExpression expr("(1,2)", &type_factory);
    value = expr.Execute().value();
  }
  EXPECT_EQ("STRUCT<INT64, INT64>", value.type()->DebugString());
  EXPECT_EQ("{1, 2}", value.DebugString());
}

TEST(EvaluatorTest, LanguageOptions) {
  // This is meant to test that LanguageOptions make it through to the
  // appropriate place and affect what syntax is accepted and how things
  // behave.  Currently, we don't have anything where the behavior differs, so
  // we just try calling an analytic function, and get a different error
  // depending whether analytic functions are enabled.
  AnalyzerOptions options;

  {
    PreparedExpression expr("1 + RANK()");
    EXPECT_THAT(expr.Prepare(options),
                StatusIs(_, HasSubstr("Function not found: RANK")));
  }

  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  {
    PreparedExpression expr("1 + RANK()");
    EXPECT_THAT(expr.Prepare(options),
                StatusIs(_, HasSubstr("Analytic function RANK cannot be "
                                      "called without an OVER clause")));
  }
}

TEST(EvaluatorTest, CurrentTimestamp) {
  const absl::Time test_time = absl::FromUnixMicros(1479885478000LL);
  googlesql_base::SimulatedClock clock(test_time);
  EvaluatorOptions evaluator_options;
  evaluator_options.clock = &clock;

  PreparedExpression expr("CURRENT_TIMESTAMP()", evaluator_options);
  Value value = expr.Execute().value();

  EXPECT_EQ(test_time, value.ToTime());
}

absl::Time GetTestTime() {
  absl::TimeZone gst;
  ABSL_CHECK(absl::LoadTimeZone("America/Los_Angeles", &gst));
  return absl::FromCivil(absl::CivilSecond(2016, 11, 22, 1, 2, 3), gst);
}

TEST(EvaluatorTest, CurrentDate) {
  googlesql_base::SimulatedClock clock(GetTestTime());
  EvaluatorOptions evaluator_options;
  evaluator_options.clock = &clock;

  PreparedExpression expr("CURRENT_DATE()", evaluator_options);
  Value value = expr.Execute().value();

  int32_t expected;
  GOOGLESQL_ASSERT_OK(functions::ConstructDate(2016, 11, 22, &expected));
  EXPECT_EQ(expected, value.date_value());
}

TEST(EvaluatorTest, CurrentDateTime) {
  googlesql_base::SimulatedClock clock(GetTestTime());
  EvaluatorOptions evaluator_options;
  evaluator_options.clock = &clock;
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      googlesql::LanguageFeature::FEATURE_CIVIL_TIME);

  PreparedExpression expr("CURRENT_DATETIME()", evaluator_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  Value value = expr.Execute().value();

  DatetimeValue expected;
  GOOGLESQL_ASSERT_OK(functions::ConstructDatetime(2016, 11, 22, 1, 2, 3, &expected));
  EXPECT_EQ(expected.DebugString(), value.datetime_value().DebugString());
}

TEST(EvaluatorTest, CurrentTime) {
  googlesql_base::SimulatedClock clock(GetTestTime());
  EvaluatorOptions evaluator_options;
  evaluator_options.clock = &clock;
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      googlesql::LanguageFeature::FEATURE_CIVIL_TIME);

  PreparedExpression expr("CURRENT_TIME()", evaluator_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options));
  Value value = expr.Execute().value();

  TimeValue expected;
  GOOGLESQL_ASSERT_OK(functions::ConstructTime(1, 2, 3, &expected));
  EXPECT_EQ(expected.DebugString(), value.time_value().DebugString());
}

TEST(EvaluatorTest, DeadlineExceeded) {
  googlesql_base::SimulatedClock clock(GetTestTime());
  EvaluatorOptions evaluator_options;
  evaluator_options.clock = &clock;

  PreparedExpression expr("1 + 2", evaluator_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions()));
  // Deadline has passed.
  const absl::Time deadline = GetTestTime() - absl::Seconds(1);

  ExpressionOptions options;
  options.deadline = deadline;
  EXPECT_THAT(expr.Execute(options),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       AllOf(HasSubstr("The statement has been aborted "
                                       "because the statement deadline"),
                             HasSubstr("was exceeded."))));
}

TEST(EvaluatorTest, DeadlineNotExceeded) {
  googlesql_base::SimulatedClock clock(GetTestTime());
  EvaluatorOptions evaluator_options;
  evaluator_options.clock = &clock;

  PreparedExpression expr("1 + 2", evaluator_options);
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions()));
  // Deadline will not pass.
  const absl::Time deadline = GetTestTime() + absl::Minutes(1);

  ExpressionOptions options;
  options.deadline = deadline;
  EXPECT_THAT(expr.Execute(options), IsOkAndHolds(Value::Int64(3)));
}

TEST(EvaluatorTest, NoColumnsGiven) {
  PreparedExpression expr("1 + 2", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions()));
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Value::Int64(3)));
}

TEST(EvaluatorTest, NamedAndPositionalColumnsGiven) {
  PreparedExpression expr("1 + 2", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions()));
  ExpressionOptions options;
  options.columns = ParameterValueMap();
  options.ordered_columns = ParameterValueList();
  EXPECT_THAT(
      expr.Execute(options),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("One of the columns fields has to be set, but not both")));
}

TEST(EvaluatorTest, ParameterizedWithNoParameterMap) {
  auto expr_ptr = MakeResolvedParameter(StringType(), "string_parameter");

  PreparedExpression expr(expr_ptr.get(), EvaluatorOptions());
  EXPECT_THAT(
      expr.Execute({}, {}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Incomplete query parameters string_parameter")));
}

TEST(EvaluatorTest, ForSystemTimeAsOfWithUnsupportedTable) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_FOR_SYSTEM_TIME_AS_OF);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  const std::string query(
      "(SELECT COUNT(*) FROM KeyValue FOR SYSTEM TIME AS OF '2017-01-01')");
  TypeFactory type_factory;
  SampleCatalog catalog;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression(query, analyzer_options, catalog.catalog(),
                              &type_factory, &analyzer_output));
  GOOGLESQL_ASSERT_OK(PopulateSampleTables(&type_factory, &catalog));

  PreparedExpression expr(analyzer_output->resolved_expr(), EvaluatorOptions());
  EXPECT_THAT(
      expr.Execute(),
      StatusIs(
          absl::StatusCode::kUnimplemented,
          HasSubstr("EvaluatorTableIterator::SetReadTime() not implemented")));
}

TEST(EvaluatorTest, ForSystemTimeAsOfWithSupportedTable) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_FOR_SYSTEM_TIME_AS_OF);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  const std::string query(
      "(SELECT COUNT(*) FROM KeyValueReadTimeIgnored FOR SYSTEM TIME AS OF "
      "'2017-01-01')");
  TypeFactory type_factory;
  SampleCatalog catalog;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression(query, analyzer_options, catalog.catalog(),
                              &type_factory, &analyzer_output));
  GOOGLESQL_ASSERT_OK(PopulateSampleTables(&type_factory, &catalog));

  PreparedExpression expr(analyzer_output->resolved_expr(), EvaluatorOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value result, expr.Execute());
  ASSERT_EQ(result, Value::Int64(4));
}

TEST(EvaluatorTest, QueryParamInForSystemTimeAsOfExpr) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_FOR_SYSTEM_TIME_AS_OF);
  GOOGLESQL_ASSERT_OK(analyzer_options.AddQueryParameter("query_param",
                                               types::TimestampType()));

  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  const std::string query(
      "(SELECT COUNT(*) FROM KeyValueReadTimeIgnored FOR SYSTEM TIME AS OF "
      "@query_param)");
  TypeFactory type_factory;
  SampleCatalog catalog;
  GOOGLESQL_ASSERT_OK(AnalyzeExpression(query, analyzer_options, catalog.catalog(),
                              &type_factory, &analyzer_output));
  GOOGLESQL_ASSERT_OK(PopulateSampleTables(&type_factory, &catalog));

  ParameterValueMap param_values;
  Value param_value = values::Timestamp(
      absl::FromCivil(absl::CivilSecond(2018, 1, 1), absl::UTCTimeZone()));
  googlesql_base::InsertOrDie(&param_values, "query_param", param_value);

  PreparedExpression expr(analyzer_output->resolved_expr(), EvaluatorOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value result, expr.Execute({}, param_values));
  ASSERT_EQ(result, Value::Int64(4));
}

// Returns a PreparedExpression for SQL. Unlike the other tests in this file,
// this expression is constructed from an AST, rather than by passing SQL
// into the expression.
struct PreparedExpressionFromAST {
  std::unique_ptr<SimpleCatalog> catalog;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  std::unique_ptr<PreparedExpression> expression;
};
PreparedExpressionFromAST ParseToASTAndPrepareOrDie(
    absl::string_view sql, const AnalyzerOptions& analyzer_options,
    TypeFactory* type_factory) {
  PreparedExpressionFromAST prepared_from_ast;
  prepared_from_ast.catalog = std::make_unique<SimpleCatalog>("foo");
  prepared_from_ast.catalog->AddBuiltinFunctions(
      BuiltinFunctionOptions::AllReleasedFunctions());

  GOOGLESQL_CHECK_OK(AnalyzeExpression(sql, analyzer_options,
                             prepared_from_ast.catalog.get(), type_factory,
                             &prepared_from_ast.analyzer_output));

  EvaluatorOptions evaluator_options;
  evaluator_options.type_factory = type_factory;
  prepared_from_ast.expression = std::make_unique<PreparedExpression>(
      prepared_from_ast.analyzer_output->resolved_expr(), evaluator_options);
  GOOGLESQL_CHECK_OK(prepared_from_ast.expression->Prepare(analyzer_options));
  return prepared_from_ast;
}

TEST(EvaluatorTest, PreparedFromAST_Execute) {
  TypeFactory type_factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("int_value", types::Int32Type()));
  GOOGLESQL_ASSERT_OK(options.SetInScopeExpressionColumn("value", proto_type));

  PreparedExpressionFromAST prepared = ParseToASTAndPrepareOrDie(
      "int64_key_1 + int64_key_2 + int_value", options, &type_factory);

  EXPECT_THAT(prepared.expression->GetReferencedColumns().value(),
              UnorderedElementsAre("value", "int_value"));
  EXPECT_TRUE(types::Int64Type()->Equals(prepared.expression->output_type()))
      << prepared.expression->output_type()->DebugString();

  googlesql_test::KitchenSinkPB input_value;
  input_value.set_int64_key_1(5);
  input_value.set_int64_key_2(10);
  Value result = prepared.expression
                     ->ExecuteAfterPrepare(
                         {{"int_value", values::Int32(1000)},
                          {"value", values::Proto(proto_type, input_value)}})
                     .value();
  EXPECT_EQ(Value::Int64(1015), result);
}

TEST(EvaluatorTest, PreparedFromAST_ExecuteWithWrongColumn) {
  TypeFactory type_factory;
  AnalyzerOptions options;
  GOOGLESQL_ASSERT_OK(options.AddExpressionColumn("int_value", types::Int32Type()));

  PreparedExpressionFromAST prepared =
      ParseToASTAndPrepareOrDie("int_value + 2", options, &type_factory);

  EXPECT_THAT(
      prepared.expression->ExecuteAfterPrepare(
          {{"wrong_value", values::Int32(1000)}}),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                                "Incomplete column parameters int_value"));
}

TEST(EvaluatorTest, PreparedFromAST_Parameters) {
  TypeFactory type_factory;
  AnalyzerOptions options;
  options.set_allow_undeclared_parameters(true);

  PreparedExpressionFromAST prepared =
      ParseToASTAndPrepareOrDie("@int_value + 2", options, &type_factory);

  EXPECT_THAT(prepared.expression->GetReferencedParameters().value(),
              UnorderedElementsAre("int_value"));
}

TEST(EvaluatorTest, PreparedFromAST_PositionalParameters) {
  TypeFactory type_factory;
  AnalyzerOptions options;
  options.set_allow_undeclared_parameters(true);
  options.set_parameter_mode(PARAMETER_POSITIONAL);

  PreparedExpressionFromAST prepared =
      ParseToASTAndPrepareOrDie("? + 2 = 5 AND ?", options, &type_factory);

  EXPECT_THAT(prepared.expression->GetPositionalParameterCount(),
              ::absl_testing::IsOkAndHolds(::testing::Eq(2)));
}

TEST(EvaluatorTest, PreparedFromAST_IllegalDeref) {
  ResolvedColumn fake_column(1, IdString::MakeGlobal("fake_table"),
                             IdString::MakeGlobal("fake_column"),
                             types::Int64Type());
  auto col_ref = MakeResolvedColumnRef(types::Int64Type(), fake_column, false);

  PreparedExpression expression(col_ref.get(), EvaluatorOptions());
  EXPECT_THAT(expression.Prepare(AnalyzerOptions()),
              absl_testing::StatusIs(absl::StatusCode::kInternal));
}

TEST(EvaluatorTest,
     InListFiltersIntersectDifferentTypesCorrectlyWhenPushedDown) {
  SimpleTable test_table(
      "t", {{"a", types::Int64Type()}, {"b", types::StringType()}});
  test_table.SetContents({{Int64(10), String("foo")}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  PreparedExpression expr(
      "EXISTS(SELECT * FROM t WHERE a = 10 AND a = CAST(10 AS UINT64))");
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions(), &catalog));
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Value::Bool(true)));
}

TEST(EvaluatorTest,
     InListFiltersIntersectDifferentTypesCorrectlyWhenPushedDown_Inf) {
  SimpleTable test_table(
      "t", {{"a", types::Int64Type()}, {"b", types::DoubleType()}});
  test_table.SetContents(
      {{Int64(10), Double(-std::numeric_limits<double>::infinity())}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  PreparedExpression expr(R"(
  EXISTS(
    SELECT b FROM t
    WHERE b = CAST('-INF' AS double) AND b = -CAST('INF' AS float) AND b < 0
  )
)");

  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions(), &catalog));
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Value::Bool(true)));
}

TEST(EvaluatorTest,
     InListFiltersIntersectDifferentTypesCorrectlyWhenPushedDown_NaN) {
  SimpleTable test_table(
      "t", {{"a", types::Int64Type()}, {"b", types::DoubleType()}});
  test_table.SetContents(
      {{Int64(10), Double(std::numeric_limits<double>::quiet_NaN())}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  PreparedExpression expr(R"(
  EXISTS(
    SELECT b FROM t
    WHERE b = CAST('NaN' AS double) AND b = CAST('NaN' AS float)
  )
)");

  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions(), &catalog));
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Value::Bool(false)));
}

TEST(EvaluatorTest, ExecuteWithOrderedColumns) {
  PreparedExpression expr("1 + 2", EvaluatorOptions());
  ExpressionOptions options;
  options.ordered_columns = {Value()};
  EXPECT_THAT(
      expr.Execute(options),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("`ordered_columns` cannot be set for Execute(). Did "
                         "you mean to call ExecuteAfterPrepare()?")));
}

TEST(EvaluatorTest, ExecuteAfterPrepareOnlyNamedParameters) {
  PreparedExpression expr("@param1");

  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_NAMED);
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("param1", types::Int64Type()));

  GOOGLESQL_ASSERT_OK(expr.Prepare(options));
  EXPECT_THAT(expr.GetReferencedParameters(),
              IsOkAndHolds(UnorderedElementsAre("param1")));
  ExpressionOptions expr_options;
  expr_options.parameters = {{"param1", Value::Int64(1)}};

  EXPECT_THAT(expr.ExecuteAfterPrepare(expr_options),
              IsOkAndHolds(Value::Int64(1)));
}

TEST(EvaluatorTest, ExecuteAfterPreparePositonalColumnsNamedParameters) {
  PreparedExpression expr("1 + 2", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(expr.Prepare(AnalyzerOptions()));
  ExpressionOptions options;
  options.ordered_columns = ParameterValueList();
  options.parameters = ParameterValueMap();
  EXPECT_THAT(expr.ExecuteAfterPrepare(options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Expected positional parameters since the "
                                 "columns are positional")));
}

TEST(EvaluatorTest, ResolvedExprValidatedWithCorrectLanguageOptions) {
  // When a resolved tree is passed to the PreparedQuery constructor, it
  // validates the tree. Make sure the correct language options are being
  // plumbed through to the validator.
  //
  // We choose a WITH RECURSIVE query for this, since the validator will give an
  // error if it sees a ResolvedRecursiveScan without the corresopnding language
  // option enabled.
  std::string query = R"(
    ARRAY(WITH RECURSIVE t AS (
      SELECT 1 AS x UNION ALL SELECT x + 1 FROM t WHERE x < 5)
    SELECT * FROM t))";

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_WITH_RECURSIVE);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_WITH_ON_SUBQUERY);

  SimpleCatalog catalog("TestCatalog");
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  TypeFactory type_factory;

  GOOGLESQL_ASSERT_OK(AnalyzeExpression(query, analyzer_options, &catalog, &type_factory,
                              &analyzer_output));
  const ResolvedExpr* resolved_expr = analyzer_output->resolved_expr();

  EvaluatorOptions evaluator_options;

  // Try to prepare the query with feature enabled in analyzer options
  PreparedExpression prepared_expr(resolved_expr, evaluator_options);
  GOOGLESQL_EXPECT_OK(prepared_expr.Prepare(analyzer_options));

  // Now, try to prepare the query again with the same resolved tree, but
  // feature disabled.
  PreparedExpression prepared_expr2(resolved_expr, evaluator_options);
  EXPECT_THAT(
      prepared_expr2.Prepare(AnalyzerOptions()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Found recursive scan, but WITH RECURSIVE and pipe "
                    "RECURSIVE UNION are both disabled in language features")));
}

TEST_F(UDFEvalTest, OkUDFEvaluator) {
  function_options_.set_evaluator([](const absl::Span<const Value> args) {
    // Returns string length as int64.
    return Value::Int64(args[0].string_value().size());
  });
  catalog()->AddOwnedFunction(new Function(
      "MyUdf", "udf", Function::SCALAR,
      {{types::Int64Type(), {types::StringType()}, kFunctionId}},
      function_options_));
  PreparedExpression expr("1 + myudf(@param)");
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options_, catalog()));
  Value result = expr.Execute({}, {{"param", Value::String("foo")}}).value();
  EXPECT_EQ(Value::Int64(4), result);
}

TEST_F(UDAEvalTest, UDAWithNoEvaluator) {
  catalog()->AddOwnedFunction(
      new Function("CustomSum", "udf", Function::AGGREGATE,
                   {{types::Int64Type(), {types::Int64Type()}, kFunctionId}},
                   function_options_));
  PreparedQuery query(
      "SELECT CustomSum(x) as avg FROM UNNEST([0, NULL, 2, 4, 4, 5, 8, NULL]) "
      "as x;",
      EvaluatorOptions());

  EXPECT_THAT(query.Prepare(analyzer_options_, catalog()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("has no evaluator")));
}

TEST_F(UDAEvalTest, UDAWithNullEvaluator) {
  AggregateFunctionEvaluatorFactory NullFactory =
      [](const FunctionSignature& sig) { return nullptr; };

  function_options_.set_aggregate_function_evaluator_factory(NullFactory);
  catalog()->AddOwnedFunction(
      new Function("CustomSum", "udf", Function::AGGREGATE,
                   {{types::Int64Type(), {types::Int64Type()}, kFunctionId}},
                   function_options_));
  PreparedQuery query(
      "SELECT CustomSum(x) as avg FROM UNNEST([0, NULL, 2, 4, 4, 5, 8, NULL]) "
      "as x;",
      EvaluatorOptions());

  EXPECT_THAT(
      query.Prepare(analyzer_options_, catalog()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("NULL evaluator")));
}

// User defined aggregator that always returns a fixed string.
class ReturnStringEvaluator : public AggregateFunctionEvaluator {
 public:
  explicit ReturnStringEvaluator(std::string my_string) : output_(my_string) {}
  ~ReturnStringEvaluator() override = default;

  absl::Status Reset() override { return absl::OkStatus(); }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    return absl::OkStatus();
  }

  absl::StatusOr<Value> GetFinalResult() override {
    return Value::String(output_);
  }

 private:
  std::string output_ = "";
};

TEST_F(UDAEvalTest, OkUDAEvaluatorReturnString) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<ReturnStringEvaluator>("foo");
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  {
    catalog()->AddOwnedFunction(
        new Function("ReturnString", "uda", Function::AGGREGATE,
                     {{types::StringType(), {types::Int64Type()}, kFunctionId}},
                     function_options_));

    PreparedQuery query(
        "SELECT ReturnString(x) as my_string FROM UNNEST([1,2,3]) AS x;",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("my_string", iter->GetColumnName(0));
    EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(String("foo"), iter->GetValue(0));
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
}

TEST_F(UDAEvalTest, OkUDAEvaluatorReturnStringInvalidOutputType) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<ReturnStringEvaluator>("bar");
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  // Make the function signature expect an INT64 output, which is
  // inconsistent with the String type returned by the user defined aggregator.
  catalog()->AddOwnedFunction(
      new Function("ReturnString2", "uda", Function::AGGREGATE,
                   {{types::Int64Type(), {types::Int64Type()}, kFunctionId}},
                   function_options_));
  PreparedQuery query(
      "SELECT ReturnString2(x) as my_string FROM UNNEST([1,2,3]) AS x;",
      EvaluatorOptions());

  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("my_string", iter->GetColumnName(0));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

  // Type checking on the aggregation output is only performed in debug_mode.
  if (GOOGLESQL_DEBUG_MODE) {
    ASSERT_FALSE(iter->NextRow());
    EXPECT_THAT(iter->Status(), StatusIs(absl::StatusCode::kInternal,
                                         HasSubstr("returned a bad result")));
  } else {
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(String("bar"), iter->GetValue(0));
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
}

// User defined sum aggregator that considers 0 to be 1000
class CustomSumEvaluator : public AggregateFunctionEvaluator {
 public:
  CustomSumEvaluator() = default;
  ~CustomSumEvaluator() override = default;

  absl::Status Reset() override {
    sum_ = 0;
    return absl::OkStatus();
  }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    GOOGLESQL_RET_CHECK(args.size() == 1);
    const Value value = *args[0];
    if (!value.type()->IsInt64()) {
      return ::googlesql_base::InternalErrorBuilder()
             << "Trying to accumulate value which is not of type INT64.";
    }
    if (value.int64_value() == 0) {
      sum_ += 1000;
    } else {
      sum_ += value.int64_value();
    }
    return absl::OkStatus();
  }

  absl::StatusOr<Value> GetFinalResult() override { return Value::Int64(sum_); }

 private:
  int sum_ = 0;
};

TEST_F(UDAEvalTest, OkUDAEvaluatorCustomSum) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<CustomSumEvaluator>();
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  catalog()->AddOwnedFunction(
      new Function("CustomSum", "uda", Function::AGGREGATE,
                   {{types::Int64Type(), {types::Int64Type()}, kFunctionId}},
                   function_options_));
  PreparedQuery query(
      "SELECT CustomSum(x) as total FROM UNNEST([0, NULL, 2, 4, 4, 5, 8, "
      "0]) "
      "as x;",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("total", iter->GetColumnName(0));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(2023), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

// User defined sum aggregator that considers NULL to be 1
class NullSumEvaluator : public AggregateFunctionEvaluator {
 public:
  NullSumEvaluator() = default;
  ~NullSumEvaluator() override = default;

  absl::Status Reset() override {
    sum_ = 0;
    return absl::OkStatus();
  }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    GOOGLESQL_RET_CHECK(args.size() == 1);
    const Value value = *args[0];
    // Custom NULL handling
    if (value.is_null()) {
      sum_ += 1;
      return absl::OkStatus();
    }
    if (!value.type()->IsInt64()) {
      return ::googlesql_base::InternalErrorBuilder()
             << "Trying to accumulate value which is not of type INT64.";
    }
    sum_ += value.int64_value();
    return absl::OkStatus();
  }

  absl::StatusOr<Value> GetFinalResult() override { return Value::Int64(sum_); }

  // Set IgnoreNulls() to return false so that we can add custom handling
  // of NULL values above in Accumulate.
  bool IgnoresNulls() override { return false; }

 private:
  int sum_ = 0;
};

TEST_F(UDAEvalTest, OkUDAEvaluatorNullSum) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<NullSumEvaluator>();
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  catalog()->AddOwnedFunction(
      new Function("NullSum", "uda", Function::AGGREGATE,
                   {{types::Int64Type(), {types::Int64Type()}, kFunctionId}},
                   function_options_));
  PreparedQuery query(
      "SELECT NullSum(x) as total FROM UNNEST([10, NULL, NULL, NULL]) "
      "as x;",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("total", iter->GetColumnName(0));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(13), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

// User defined count aggregator
class CustomCountEvaluator : public AggregateFunctionEvaluator {
 public:
  CustomCountEvaluator() = default;
  ~CustomCountEvaluator() override = default;

  absl::Status Reset() override {
    count_ = 0;
    return absl::OkStatus();
  }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    count_++;
    return absl::OkStatus();
  }

  absl::StatusOr<Value> GetFinalResult() override {
    return Value::Int64(count_);
  }

 private:
  int count_ = 0;
};

TEST_F(UDAEvalTest, OkUDAEvaluatorCustomCount) {
  // This test demonstrates some interactions between the aggregate function
  // evaluator and some aggregate function modifiers that can be set in
  // FunctionOptions.
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<CustomCountEvaluator>();
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  function_options_.set_supports_distinct_modifier(true);
  function_options_.set_supports_having_modifier(true);
  catalog()->AddOwnedFunction(
      new Function("CustomCount", "uda", Function::AGGREGATE,
                   {{types::Int64Type(),
                     {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                     kFunctionId}},
                   function_options_));
  // Count all rows
  {
    PreparedQuery query(
        R"sql(WITH Fruits AS (SELECT 'apple' AS fruit,
        1.20 AS price UNION ALL SELECT 'banana',
        0.50 UNION ALL SELECT 'banana', 0.60 UNION ALL SELECT 'pear',
        3.00 UNION ALL SELECT 'grapes', 2.00 UNION ALL SELECT 'grapes', 5.00)
        SELECT CustomCount(Fruits.fruit) as count FROM Fruits;)sql",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("count", iter->GetColumnName(0));
    EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(Int64(6), iter->GetValue(0));
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  // Count rows with distinct fruit
  {
    PreparedQuery query(
        R"sql(WITH Fruits AS (SELECT 'apple' AS fruit,
        1.20 AS price UNION ALL SELECT 'banana',
        0.50 UNION ALL SELECT 'banana', 0.60 UNION ALL SELECT 'pear',
        3.00 UNION ALL SELECT 'grapes', 2.00 UNION ALL SELECT 'grapes', 5.00)
        SELECT CustomCount(DISTINCT Fruits.fruit) as count FROM Fruits;)sql",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("count", iter->GetColumnName(0));
    EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(Int64(4), iter->GetValue(0));

    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  // Count rows having max price >= 3.00
  {
    PreparedQuery query(
        R"sql(WITH Fruits AS (SELECT 'apple' AS fruit,
        1.20 AS price UNION ALL SELECT 'banana',
        0.50 UNION ALL SELECT 'banana', 0.60 UNION ALL SELECT 'pear',
        3.00 UNION ALL SELECT 'grapes', 2.00 UNION ALL SELECT 'grapes', 5.00)
        SELECT CustomCount(Fruits.fruit HAVING MAX(price)>=3.00) as count
        FROM Fruits;)sql",
        EvaluatorOptions());
    AnalyzerOptions analyzer_options;
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_HAVING_IN_AGGREGATE);
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("count", iter->GetColumnName(0));
    EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(Int64(2), iter->GetValue(0));

    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  // Count all rows, grouped by fruit
  {
    PreparedQuery query(
        R"sql(WITH Fruits AS (SELECT 'apple' AS fruit,
        1.20 AS price UNION ALL SELECT 'banana',
        0.50 UNION ALL SELECT 'banana', 0.60 UNION ALL SELECT 'pear',
        3.00 UNION ALL SELECT 'grapes', 2.00 UNION ALL SELECT 'grapes', 5.00)
        SELECT fruit, CustomCount(fruit) as count FROM Fruits
        GROUP BY fruit ORDER BY fruit;)sql",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(2, iter->NumColumns());
    EXPECT_EQ("fruit", iter->GetColumnName(0));
    EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());
    EXPECT_EQ("count", iter->GetColumnName(1));
    EXPECT_EQ("INT64", iter->GetColumnType(1)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(String("apple"), iter->GetValue(0));
    EXPECT_EQ(Int64(1), iter->GetValue(1));
    EXPECT_TRUE(iter->NextRow());
    EXPECT_EQ(String("banana"), iter->GetValue(0));
    EXPECT_EQ(Int64(2), iter->GetValue(1));
    EXPECT_TRUE(iter->NextRow());
    EXPECT_EQ(String("grapes"), iter->GetValue(0));
    EXPECT_EQ(Int64(2), iter->GetValue(1));
    EXPECT_TRUE(iter->NextRow());
    EXPECT_EQ(String("pear"), iter->GetValue(0));
    EXPECT_EQ(Int64(1), iter->GetValue(1));

    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
}

// User defined aggregator for a nullary function.
// Always returns a null INT64.
// Note that 'value' for a nullary function is an empty struct.
class NullaryAggEvaluator : public AggregateFunctionEvaluator {
 public:
  NullaryAggEvaluator() = default;
  ~NullaryAggEvaluator() override = default;

  absl::Status Reset() override { return absl::OkStatus(); }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    GOOGLESQL_RET_CHECK(args.empty());
    return absl::OkStatus();
  }

  absl::StatusOr<Value> GetFinalResult() override { return Value::NullInt64(); }
};

TEST_F(UDAEvalTest, OkUDANullaryEvaluator) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<NullaryAggEvaluator>();
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  catalog()->AddOwnedFunction(
      new Function("NullaryAgg", "uda", Function::AGGREGATE,
                   {{types::Int64Type(), {}, kFunctionId}}, function_options_));

  PreparedQuery query(
      "SELECT NullaryAgg() as null_int FROM UNNEST([1,2,3]) AS x;",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("null_int", iter->GetColumnName(0));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(NullInt64(), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

// User defined aggregator for a binary function that operates on a string and
// integer and aggregates them as a concatenated string.
// Note that 'value' for an n-ary aggregate function is a struct type with
// fields corresponding to each function argument, with matching types.
class BinaryAggEvaluator : public AggregateFunctionEvaluator {
 public:
  BinaryAggEvaluator() = default;
  ~BinaryAggEvaluator() override = default;

  absl::Status Reset() override { return absl::OkStatus(); }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    GOOGLESQL_RET_CHECK(args.size() == 2);
    const Value value1 = *args[0];
    const Value value2 = *args[1];
    if (!value1.type()->IsString()) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Unexpected type for first argument");
    }
    if (!value2.type()->IsInt64()) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Unexpected type for second argument");
    }
    result_.append(absl::StrCat(value1.string_value(),
                                std::to_string(value2.int64_value())));
    return absl::OkStatus();
  }

  absl::StatusOr<Value> GetFinalResult() override {
    return Value::String(result_);
  }

 private:
  std::string result_ = "";
};

TEST_F(UDAEvalTest, OkUDABinaryEvaluator) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig) {
        return std::make_unique<BinaryAggEvaluator>();
      };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  catalog()->AddOwnedFunction(
      new Function("BinaryAgg", "uda", Function::AGGREGATE,
                   {{types::StringType(),
                     {types::StringType(), types::Int64Type()},
                     kFunctionId}},
                   function_options_));

  PreparedQuery query(
      R"sql(WITH Employees AS (SELECT 'Alice' AS name,
        1 AS id UNION ALL SELECT 'Bob',
        2 UNION ALL SELECT 'Eve', 3)
        SELECT BinaryAgg(name, id) as output FROM Employees)sql",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("output", iter->GetColumnName(0));
  EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(String("Alice1Bob2Eve3"), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(UDAEvalTest, OkUDAPolymorphicEvaluator) {
  AggregateFunctionEvaluatorFactory AggregateFn =
      [](const FunctionSignature& sig)
      -> absl::StatusOr<std::unique_ptr<AggregateFunctionEvaluator>> {
    switch (sig.ConcreteArgumentType(0)->kind()) {
      case TYPE_INT64:
        return std::make_unique<ReturnStringEvaluator>("foo");
      case TYPE_STRING:
        return std::make_unique<ReturnStringEvaluator>("bar");
      case TYPE_BOOL:
        return std::make_unique<ReturnStringEvaluator>("foobar");
      default:
        return absl::Status(absl::StatusCode::kInternal,
                            "Beg your pardon: " + sig.DebugString());
    }
  };

  function_options_.set_aggregate_function_evaluator_factory(AggregateFn);
  catalog()->AddOwnedFunction(
      new Function("PolymorphicAgg", "uda", Function::AGGREGATE,
                   {{types::StringType(),
                     {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                     kFunctionId}},
                   function_options_));

  // Aggregation on int
  {
    PreparedQuery query(
        "SELECT PolymorphicAgg(x) as output FROM UNNEST([1,2,3]) AS x;",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("output", iter->GetColumnName(0));
    EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(String("foo"), iter->GetValue(0));
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  // Aggregation on string
  {
    PreparedQuery query(
        "SELECT PolymorphicAgg(x) as output FROM UNNEST(['ab','cd','ef']) AS "
        "x;",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("output", iter->GetColumnName(0));
    EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(String("bar"), iter->GetValue(0));
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  // Aggregation on bool
  {
    PreparedQuery query(
        "SELECT PolymorphicAgg(x) as output FROM UNNEST([true, false]) AS x;",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options_, catalog()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                         query.Execute());

    EXPECT_EQ(1, iter->NumColumns());
    EXPECT_EQ("output", iter->GetColumnName(0));
    EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(String("foobar"), iter->GetValue(0));
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  // Aggregation on double
  {
    PreparedQuery query(
        "SELECT PolymorphicAgg(x) as output FROM UNNEST([1.2, 2.4]) AS x;",
        EvaluatorOptions());
    EXPECT_THAT(
        query.Prepare(analyzer_options_, catalog()),
        StatusIs(absl::StatusCode::kInternal, HasSubstr("Beg your pardon")));
  }
}

TEST_F(UDFEvalTest, OkPolymorphicUDFEvaluator) {
  static FunctionEvaluator evaluator_int64 =
      [](const absl::Span<const Value> args) {
        return Value::Int64(args[0].int64_value() * 2);
      };
  static FunctionEvaluator evaluator_string =
      [](const absl::Span<const Value> args) {
        return Value::Int64(args[0].string_value().size());
      };
  function_options_.set_evaluator_factory(
      [](const FunctionSignature& signature)
          -> absl::StatusOr<FunctionEvaluator> {
        switch (signature.ConcreteArgumentType(0)->kind()) {
          case TYPE_INT64:
            return evaluator_int64;
          case TYPE_STRING:
            return evaluator_string;
          case TYPE_BOOL:
            return FunctionEvaluator();
          default:
            return absl::Status(absl::StatusCode::kInternal,
                                "Beg your pardon: " + signature.DebugString());
        }
      });
  catalog()->AddOwnedFunction(new Function(
      "MyUdf", "udf", Function::SCALAR,
      {{types::Int64Type(), {ARG_TYPE_ANY_1}, kFunctionId}},
      function_options_));
  PreparedExpression expr("1 + myudf(myudf(@param))");
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options_, catalog()));
  Value result = expr.Execute({}, {{"param", Value::String("foo")}}).value();
  EXPECT_EQ(Value::Int64(7), result);  // 7 = 1 + length("foo") * 2

  PreparedExpression expr_double("1 + myudf(1.5)");
  absl::Status status = expr_double.Prepare(analyzer_options_, catalog());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInternal,
                               HasSubstr("Beg your pardon")));

  PreparedExpression expr_bool("1 + myudf(false)");
  status = expr_bool.Prepare(analyzer_options_, catalog());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInternal,
                               HasSubstr("NULL evaluator")));
}

TEST_F(UDFEvalTest, UndefinedUDF) {
  PreparedExpression expr("1 + myudf(@param)");
  absl::Status status = expr.Prepare(analyzer_options_);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Function not found")));
}

TEST_F(UDFEvalTest, NoUDFEvaluator) {
  catalog()->AddOwnedFunction(new Function(
      "MyUdf", "udf", Function::SCALAR,
      {{types::Int64Type(), {types::StringType()}, kFunctionId}}));
  PreparedExpression expr("1 + myudf(@param)");
  absl::Status status = expr.Prepare(analyzer_options_, catalog());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("has no evaluator")));
}

TEST_F(UDFEvalTest, UDFEvaluatorRuntimeErrors) {
  function_options_.set_evaluator(
      [](const absl::Span<const Value> args) -> absl::StatusOr<Value> {
        std::string arg = args[0].string_value();
        if (arg == "not found") {
          return absl::Status(absl::StatusCode::kNotFound, "Wrong number");
        } else if (arg == "wrong return type") {
          // Returns Value::Int32 instead of Value::Int64.
          return Value::Int32(arg.size());
        } else if (arg == "invalid") {
          return Value::Invalid();
        }
        return Value::Int64(arg.size());
      });
  catalog()->AddOwnedFunction(new Function(
      "MyUdf", "udf", Function::SCALAR,
      {{types::Int64Type(), {types::StringType()}, kFunctionId}},
      function_options_));
  PreparedExpression expr("1 + myudf(@param)");
  GOOGLESQL_ASSERT_OK(expr.Prepare(analyzer_options_, catalog()));

  // Error-free call.
  Value result = expr.Execute({}, {{"param", Value::String("foo")}}).value();
  EXPECT_EQ(Value::Int64(4), result);

  // Runtime errors.
  absl::Status status;
  status = expr.Execute(
      {}, {{"param", Value::String("not found")}}).status();
  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kNotFound, HasSubstr("Wrong number")));

  status = expr.Execute(
      {}, {{"param", Value::String("invalid")}}).status();
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInternal,
                               HasSubstr("Uninitialized value")));

  if (GOOGLESQL_DEBUG_MODE) {
    status = expr.Execute(
        {}, {{"param", Value::String("wrong return type")}}).status();
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kInternal,
                                 HasSubstr("Expected value of type: INT64")));
  } else {
    EXPECT_DEATH(
        expr.Execute({}, {{"param", Value::String("wrong return type")}})
            .IgnoreError(),
        "Not an int64");
  }
}

TEST(PreparedQuery, ExpressionQuery) {
  PreparedQuery query("select 1 a, 2 b, 'abc'", EvaluatorOptions());
  GOOGLESQL_EXPECT_OK(query.Prepare(AnalyzerOptions()));

  EXPECT_EQ(3, query.num_columns());
  EXPECT_EQ("a", query.column_name(0));
  EXPECT_EQ("b", query.column_name(1));
  EXPECT_EQ("", query.column_name(2));
  EXPECT_EQ("INT64", query.column_type(0)->DebugString());
  EXPECT_EQ("INT64", query.column_type(1)->DebugString());
  EXPECT_EQ("STRING", query.column_type(2)->DebugString());

  std::vector<PreparedQuery::NameAndType> columns = query.GetColumns();
  EXPECT_EQ(3, columns.size());
  EXPECT_EQ("a", columns[0].first);
  EXPECT_EQ("b", columns[1].first);
  // Anonymous columns get empty names.
  EXPECT_EQ("", columns[2].first);
  EXPECT_EQ("INT64", columns[0].second->DebugString());
  EXPECT_EQ("INT64", columns[1].second->DebugString());
  EXPECT_EQ("STRING", columns[2].second->DebugString());

  EXPECT_FALSE(query.is_value_table());
  EXPECT_EQ(3, query.resolved_query_stmt()->output_column_list_size());

  EXPECT_THAT(query.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(3, iter->NumColumns());
  EXPECT_EQ("a", iter->GetColumnName(0));
  EXPECT_EQ("b", iter->GetColumnName(1));
  // Anonymous columns get empty names.
  EXPECT_EQ("", iter->GetColumnName(2));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("INT64", iter->GetColumnType(1)->DebugString());
  EXPECT_EQ("STRING", iter->GetColumnType(2)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(1), iter->GetValue(0));
  EXPECT_EQ(Int64(2), iter->GetValue(1));
  EXPECT_EQ(String("abc"), iter->GetValue(2));

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, NontrivialOutputColumnNames) {
  // Query adapted from b/123093575.
  const std::string query_str =
      R"(SELECT
  A.x AS y,
  B.x AS z,
  A.x,
  B.x
FROM
  (
    SELECT
      *
    FROM
      ((
        SELECT
          1 AS x
        ) UNION ALL(
        SELECT
          2 AS x
        )
      )
  ) AS A,
  (
    SELECT
      *
    FROM
      ((
        SELECT
          1 AS x
        ) UNION ALL(
        SELECT
          2 AS x
        )
      )
  ) AS B)";

  PreparedQuery query(query_str, EvaluatorOptions());
  GOOGLESQL_EXPECT_OK(query.Prepare(AnalyzerOptions()));

  EXPECT_EQ(4, query.num_columns());
  EXPECT_EQ("y", query.column_name(0));
  EXPECT_EQ("z", query.column_name(1));
  EXPECT_EQ("x", query.column_name(2));
  EXPECT_EQ("x", query.column_name(3));
  EXPECT_EQ("INT64", query.column_type(0)->DebugString());
  EXPECT_EQ("INT64", query.column_type(1)->DebugString());
  EXPECT_EQ("INT64", query.column_type(2)->DebugString());
  EXPECT_EQ("INT64", query.column_type(3)->DebugString());

  std::vector<PreparedQuery::NameAndType> columns = query.GetColumns();
  EXPECT_EQ(4, columns.size());
  EXPECT_EQ("y", columns[0].first);
  EXPECT_EQ("z", columns[1].first);
  EXPECT_EQ("INT64", columns[0].second->DebugString());
  EXPECT_EQ("INT64", columns[1].second->DebugString());
  EXPECT_EQ("INT64", columns[2].second->DebugString());
  EXPECT_EQ("INT64", columns[3].second->DebugString());

  EXPECT_FALSE(query.is_value_table());
  EXPECT_EQ(4, query.resolved_query_stmt()->output_column_list_size());

  EXPECT_THAT(query.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(4, iter->NumColumns());
  EXPECT_EQ("y", iter->GetColumnName(0));
  EXPECT_EQ("z", iter->GetColumnName(1));
  EXPECT_EQ("x", iter->GetColumnName(2));
  EXPECT_EQ("x", iter->GetColumnName(3));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("INT64", iter->GetColumnType(1)->DebugString());
  EXPECT_EQ("INT64", iter->GetColumnType(2)->DebugString());
  EXPECT_EQ("INT64", iter->GetColumnType(3)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(1), iter->GetValue(0));
  EXPECT_EQ(Int64(1), iter->GetValue(1));
  EXPECT_EQ(Int64(1), iter->GetValue(2));
  EXPECT_EQ(Int64(1), iter->GetValue(3));

  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(1), iter->GetValue(0));
  EXPECT_EQ(Int64(2), iter->GetValue(1));
  EXPECT_EQ(Int64(1), iter->GetValue(2));
  EXPECT_EQ(Int64(2), iter->GetValue(3));

  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(2), iter->GetValue(0));
  EXPECT_EQ(Int64(1), iter->GetValue(1));
  EXPECT_EQ(Int64(2), iter->GetValue(2));
  EXPECT_EQ(Int64(1), iter->GetValue(3));

  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(2), iter->GetValue(0));
  EXPECT_EQ(Int64(2), iter->GetValue(1));
  EXPECT_EQ(Int64(2), iter->GetValue(2));
  EXPECT_EQ(Int64(2), iter->GetValue(3));

  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, FromTable) {
  SimpleTable test_table(
      "TestTable", {{"a", types::Int64Type()}, {"b", types::StringType()}});
  test_table.SetContents({{Int64(10), String("foo")},
                          {Int64(20), String("bar")},
                          {Int64(30), String("baz")}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  PreparedQuery query("select a, b from TestTable", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), &catalog));

  EXPECT_EQ(2, query.num_columns());
  EXPECT_EQ("a", query.column_name(0));
  EXPECT_EQ("b", query.column_name(1));
  EXPECT_EQ("INT64", query.column_type(0)->DebugString());
  EXPECT_EQ("STRING", query.column_type(1)->DebugString());

  EXPECT_FALSE(query.is_value_table());
  EXPECT_EQ(2, query.resolved_query_stmt()->output_column_list_size());

  EXPECT_THAT(query.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());
  EXPECT_EQ(2, iter->NumColumns());
  EXPECT_EQ("a", iter->GetColumnName(0));
  EXPECT_EQ("b", iter->GetColumnName(1));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("STRING", iter->GetColumnType(1)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(10), iter->GetValue(0));
  EXPECT_EQ(String("foo"), iter->GetValue(1));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(20), iter->GetValue(0));
  EXPECT_EQ(String("bar"), iter->GetValue(1));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(30), iter->GetValue(0));
  EXPECT_EQ(String("baz"), iter->GetValue(1));

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, PrepareExecuteMissingQueryParameter) {
  SimpleTable test_table(
      "TestTable", {{"col", types::Int64Type()}});
  test_table.SetContents({{Int64(5)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  EvaluatorOptions evaluator_options;
  PreparedQuery query("SELECT @param + col FROM TestTable", evaluator_options);
  EXPECT_THAT(query.Prepare(AnalyzerOptions(), &catalog),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Query parameter 'param' not found")));
}

TEST(PreparedQuery, PrepareExecuteAllowUndeclaredQueryParameters) {
  SimpleTable test_table(
      "TestTable", {{"col", types::Int64Type()}});
  test_table.SetContents({{Int64(5)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  EvaluatorOptions evaluator_options;
  PreparedQuery query("SELECT @param + col FROM TestTable", evaluator_options);

  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options, &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> params,
                       query.GetReferencedParameters());
  EXPECT_THAT(params, testing::ElementsAre("param"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute({{"param", Value::Int64(6)}}));
  EXPECT_EQ(1, iter->NumColumns());
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(11), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, PrepareExecuteAllowUndeclaredQueryParametersResolvedStmt) {
  SimpleTable test_table(
      "TestTable", {{"col", types::Int64Type()}});
  test_table.SetContents({{Int64(5)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement("SELECT @param + col FROM TestTable",
                             analyzer_options, &catalog, &type_factory,
                             &analyzer_output));
  ASSERT_TRUE(analyzer_output->resolved_statement()->Is<ResolvedQueryStmt>());

  EvaluatorOptions evaluator_options;
  PreparedQuery query(
      analyzer_output->resolved_statement()->GetAs<ResolvedQueryStmt>(),
      evaluator_options);

  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options, &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> params,
                       query.GetReferencedParameters());
  EXPECT_THAT(params, testing::ElementsAre("param"));
  EXPECT_THAT(query.Execute({{"param", Value::Int64(6)}}),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Expected type not found for variable param")));
}

TEST(PreparedQuery, PrepareExecuteMissingPositionalQueryParameter) {
  SimpleTable test_table(
      "TestTable", {{"col", types::Int64Type()}});
  test_table.SetContents({{Int64(5)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  EvaluatorOptions evaluator_options;
  PreparedQuery query("SELECT ? + col FROM TestTable", evaluator_options);
  AnalyzerOptions options;
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  EXPECT_THAT(query.Prepare(options, &catalog),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Query parameter number 1 is not defined")));
}

TEST(PreparedQuery, PrepareExecuteAllowUndeclaredPositionalQueryParameters) {
  SimpleTable test_table(
      "TestTable", {{"col", types::Int64Type()}});
  test_table.SetContents({{Int64(5)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  EvaluatorOptions evaluator_options;
  PreparedQuery query(
      "SELECT CONCAT(CAST((? + col) AS STRING), ?) FROM TestTable",
      evaluator_options);
  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options, &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(int count, query.GetPositionalParameterCount());
  EXPECT_EQ(2, count);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableIterator> iter,
      query.ExecuteWithPositionalParams({Value::Int64(6),
        Value::StringValue("foo")}));
  EXPECT_EQ(1, iter->NumColumns());
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Value::StringValue("11foo"), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery,
     PrepareExecuteAllowUndeclaredPositionalQueryParametersResolvedStmt) {
  SimpleTable test_table(
      "TestTable", {{"col", types::Int64Type()}});
  test_table.SetContents({{Int64(5)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  AnalyzerOptions analyzer_options;
  analyzer_options.set_allow_undeclared_parameters(true);
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(
      "SELECT CONCAT(CAST((? + col) AS STRING), ?) FROM TestTable",
      analyzer_options, &catalog, &type_factory,
      &analyzer_output));
  ASSERT_TRUE(analyzer_output->resolved_statement()->Is<ResolvedQueryStmt>());

  EvaluatorOptions evaluator_options;
  PreparedQuery query(
      analyzer_output->resolved_statement()->GetAs<ResolvedQueryStmt>(),
      evaluator_options);
  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options, &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(int count, query.GetPositionalParameterCount());
  EXPECT_EQ(2, count);
  EXPECT_THAT(
      query.ExecuteWithPositionalParams(
          {Value::Int64(6), Value::StringValue("foo")}),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Mismatch in number of analyzer parameters versus "
                         "algebrizer parameters")));
}

class PreparedTvfQuery : public testing::Test {
 protected:
  void SetUp() override {
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_TABLE_VALUED_FUNCTIONS);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_NAMED_ARGUMENTS);
  }

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> Execute(
      absl::string_view query) {
    std::unique_ptr<const AnalyzerOutput> analyzer_output;
    GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(query, analyzer_options_,
                                     catalog_.catalog(), &type_factory_,
                                     &analyzer_output));
    prepared_query_ = std::make_unique<PreparedQuery>(
        analyzer_output->resolved_statement()->GetAs<ResolvedQueryStmt>(),
        evaluator_options_);
    GOOGLESQL_RETURN_IF_ERROR(prepared_query_->Prepare(analyzer_options_));
    return prepared_query_->Execute();
  }

  SampleCatalog catalog_;
  AnalyzerOptions analyzer_options_;
  TypeFactory type_factory_;
  EvaluatorOptions evaluator_options_;
  std::unique_ptr<PreparedQuery> prepared_query_;
};

TEST_F(PreparedTvfQuery, TVF_OptionalRelation_Absent) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_optional_relation();
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  EXPECT_EQ(iter->GetColumnName(0), "");
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_OptionalRelation_Present) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_optional_relation((select key as foo from KeyValue));
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  EXPECT_EQ(iter->GetColumnName(0), "");
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(2));
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(4));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_OptionalArguments_Absent) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_optional_arguments();
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  EXPECT_EQ(iter->GetColumnName(0), "y");
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Double(5));  // 1 * 2 + 3
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_OptionalArguments_Present) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_optional_arguments(3.0, 2, 1);
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  EXPECT_EQ(iter->GetColumnName(0), "y");
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Double(7));  // 3 * 2 + 1
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_OptionalArguments_NamedOnly) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_optional_arguments(steps=>2, dx=>-1);
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  EXPECT_EQ(iter->GetColumnName(0), "y");
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Double(5));  // 1 * 2 + 3
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Double(3));  // 0 * 2 + 3
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_OptionalArguments_All) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_optional_arguments(3.0, 2, 1, steps=>2, dx=>-1);
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  EXPECT_EQ(iter->GetColumnName(0), "y");
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Double(7));  // 3 * 2 + 1
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Double(5));  // 2 * 2 + 1
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_RepeatedArguments_Absent) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_repeated_arguments();
  )"));
  EXPECT_EQ(iter->NumColumns(), 2);
  EXPECT_EQ(iter->GetColumnName(0), "key");
  EXPECT_EQ(iter->GetColumnName(1), "value");
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_RepeatedArguments_Present) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT * FROM tvf_repeated_arguments("a", 1, "b", 2, "c", 3);
  )"));
  EXPECT_EQ(iter->NumColumns(), 2);
  EXPECT_EQ(iter->GetColumnName(0), "key");
  EXPECT_EQ(iter->GetColumnName(1), "value");
  EXPECT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), String("a"));
  EXPECT_EQ(iter->GetValue(1), Int64(1));
  EXPECT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), String("b"));
  EXPECT_EQ(iter->GetValue(1), Int64(2));
  EXPECT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), String("c"));
  EXPECT_EQ(iter->GetValue(1), Int64(3));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_DefaultValues) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT key, value FROM tvf_increment_by(TABLE KeyValue);
  )"));
  EXPECT_EQ(iter->NumColumns(), 2);
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(2));
  EXPECT_EQ(iter->GetValue(1), String("a"));
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(3));
  EXPECT_EQ(iter->GetValue(1), String("b"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_OutputColumnsProjectionAndOrder) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT baz, bar, foo
    FROM tvf_increment_by(
        (SELECT value as foo, key AS bar, false as baz, key as qux FROM KeyValue),
        16);
  )"));
  EXPECT_EQ(iter->NumColumns(), 3);
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Bool(false));
  EXPECT_EQ(iter->GetValue(1), Int64(17));
  EXPECT_EQ(iter->GetValue(2), String("a"));
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Bool(false));
  EXPECT_EQ(iter->GetValue(1), Int64(18));
  EXPECT_EQ(iter->GetValue(2), String("b"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedTvfQuery, TVF_FixedSchema) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto iter, Execute(R"(
    SELECT sum
    FROM tvf_sum_diff((SELECT key as a, key AS b FROM KeyValue));
  )"));
  EXPECT_EQ(iter->NumColumns(), 1);
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(2));
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(iter->GetValue(0), Int64(4));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, ResolvedQueryValidatedWithCorrectLanguageOptions) {
  // When a resolved tree is passed to the PreparedQuery constructor, it
  // validates the tree. Make sure the correct language options are being
  // plumbed through to the validator.
  //
  // We choose a WITH RECURSIVE query for this, since the validator will give an
  // error if it sees a ResolvedRecursiveScan without the corresopnding language
  // option enabled.
  std::string query = R"(
    WITH RECURSIVE t AS (
      SELECT 1 AS x UNION ALL SELECT x + 1 FROM t WHERE x < 5)
    SELECT * FROM t;)";

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_WITH_RECURSIVE);

  SimpleCatalog catalog("TestCatalog");
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  TypeFactory type_factory;

  GOOGLESQL_ASSERT_OK(AnalyzeStatement(query, analyzer_options, &catalog, &type_factory,
                             &analyzer_output));
  const ResolvedQueryStmt* query_stmt =
      analyzer_output->resolved_statement()->GetAs<ResolvedQueryStmt>();

  EvaluatorOptions evaluator_options;

  // Try to prepare the query with feature enabled in analyzer options
  PreparedQuery prepared_query(query_stmt, evaluator_options);
  GOOGLESQL_EXPECT_OK(prepared_query.Prepare(analyzer_options));

  // Now, try to prepare the query again with the same resolved tree, but
  // feature disabled.
  PreparedQuery prepared_query2(query_stmt, evaluator_options);
  EXPECT_THAT(
      prepared_query2.Prepare(AnalyzerOptions()),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Found recursive scan, but WITH RECURSIVE and pipe "
                    "RECURSIVE UNION are both disabled in language features")));
}

class PreparedModifyTest : public ::testing::Test {
 public:
  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    AddNonValueTable();
    AddValueTableInt64RowType();

    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
  }

  Catalog* catalog() { return &catalog_; }
  const AnalyzerOptions& analyzer_options() { return analyzer_options_; }

 protected:
  void AddNonValueTable() {
    auto test_table = std::make_unique<SimpleTable>(
        kTestTable,
        std::vector<SimpleTable::NameAndType>{
            {"int_val", types::Int64Type()}, {"str_val", types::StringType()}});
    test_table->SetContents({{Int64(1), String("one")},
                            {Int64(2), String("two")},
                            {Int64(4), String("four")}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  void AddValueTableInt64RowType() {
    auto test_value_table = std::make_unique<SimpleTable>(
        kTestValueTable,
        std::vector<SimpleTable::NameAndType>{{"int_val", types::Int64Type()}});
    test_value_table->SetContents({{Int64(1)}, {Int64(2)}, {Int64(4)}});
    GOOGLESQL_ASSERT_OK(test_value_table->SetPrimaryKey({0}));
    test_value_table->set_is_value_table(true);
    catalog_.AddOwnedTable(std::move(test_value_table));
  }

  SimpleCatalog catalog_{"test_catalog"};
  AnalyzerOptions analyzer_options_;

  // Table names
  static constexpr char kTestTable[] = "test_table";
  static constexpr char kTestValueTable[] = "test_value_table";
};

TEST_F(PreparedModifyTest, ExecutesInsertIgnoreReturnAllInsertRows) {
  EvaluatorOptions evaluator_options;
  // DML output returns both inserted and ignored rows.
  evaluator_options.return_all_insert_rows_insert_ignore_dml = true;
  // Row with int_val = 1 is ignored because it already exists in the table.
  PreparedModify modify(
      "insert ignore test_table(int_val, str_val) "
      "values(1, 'one'), (3, 'three')",
      evaluator_options);
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_INSERT_STMT);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(1));
  EXPECT_EQ(iter->GetColumnValue(1), String("one"));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);

  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, ExecutesInsertIgnoreReturnOnlyInsertedRows) {
  EvaluatorOptions evaluator_options;
  // DML output does not return the ignored rows.
  evaluator_options.return_all_insert_rows_insert_ignore_dml = false;
  // Row with int_val = 1 is ignored because it already exists in the table.
  PreparedModify modify(
      "insert ignore test_table(int_val, str_val) "
      "values(1, 'one'), (3, 'three')",
      evaluator_options);
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_INSERT_STMT);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);
  EXPECT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, ExecutesInsert) {
  PreparedModify modify(
      "insert test_table(int_val, str_val) values(3, 'three')",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_INSERT_STMT);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, ExecutesInsertToValueTable) {
  PreparedModify modify("insert test_value_table(int_val) values(3)",
                        EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_INSERT_STMT);
  ASSERT_THAT(
      modify.Execute(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "PreparedModify api does not support modifying value tables"));
}

TEST_F(PreparedModifyTest, ExecutesDelete) {
  PreparedModify modify("delete test_table where int_val in (2, 4)",
                      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_DELETE_STMT);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
  EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kDelete);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
  EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kDelete);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, ExecutesDeleteFromValueTable) {
  PreparedModify modify(
      "delete test_value_table where test_value_table in (2, 4)",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_DELETE_STMT);
  ASSERT_THAT(
      modify.Execute(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "PreparedModify api does not support modifying value tables"));
}

TEST_F(PreparedModifyTest, ExecutesUpdateOnValueTable) {
  PreparedModify modify(
      "update test_value_table set test_value_table = 2 WHERE TRUE",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_UPDATE_STMT);
  ASSERT_THAT(
      modify.Execute(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "PreparedModify api does not support modifying value tables"));
}

TEST_F(PreparedModifyTest, ExecutesUpdate) {
  PreparedModify modify(
      "update test_table set str_val = 'foo' where int_val > 1",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_UPDATE_STMT);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(2));
  EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kUpdate);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(4));
  EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kUpdate);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, IteratorStillLiveOnDestruction) {
  auto query = std::make_unique<PreparedModify>(
      "delete from test_table where true", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query->Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       query->Execute());
  EXPECT_DEATH(query.reset(), "cannot outlive the PreparedQuery object");
}

TEST_F(PreparedModifyTest, InvalidStatementKind) {
  PreparedModify modify("select * from test_table", EvaluatorOptions());
  EXPECT_THAT(modify.Prepare(analyzer_options(), catalog()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not correspond to a DML statement")));
}

TEST_F(PreparedModifyTest, Parameter) {
  PreparedModify modify("insert test_table(int_val, str_val) values(@p1, @p2)",
                        EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p1", types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p2", types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p3", types::DoubleType()));

  GOOGLESQL_EXPECT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(),
              IsOkAndHolds(UnorderedElementsAre("p1", "p2")));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(0));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.Execute({{"p2", String("three")}, {"p1", Int64(3)}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, modify.Execute({{"p2", NullString()},
                                             {"p1", Int64(0)},
                                             {"p3", NullDouble()},
                                             {"p4", NullBytes()}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(0));
  EXPECT_EQ(iter->GetColumnValue(1), NullString());
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(modify.Execute({{"p2", values::NullString()}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Incomplete query parameters p1")));
}

TEST_F(PreparedModifyTest, NamedParameter) {
  PreparedModify modify(
      "insert test_table(int_val, str_val) values(@param1, @param2)",
      EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("param1", types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("param2", types::StringType()));

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(),
              IsOkAndHolds(ElementsAre("param1", "param2")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.Execute({{"param1", Int64(3)}, {"param2", String("three")}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, UndeclaredNamedParameter) {
  PreparedModify modify(
      "insert test_table(int_val, str_val) values(@param1, @param2)",
      EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_allow_undeclared_parameters(true);

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(),
              IsOkAndHolds(ElementsAre("param1", "param2")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.Execute({{"param1", Int64(3)}, {"param2", String("three")}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, UndeclaredNamedParameterResolvedStmt) {
  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_allow_undeclared_parameters(true);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(
      "insert test_table(int_val, str_val) values(@param1, @param2)",
      analyzer_options, catalog(), &type_factory,
      &analyzer_output));

  PreparedModify modify(analyzer_output->resolved_statement(),
                        EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(),
              IsOkAndHolds(ElementsAre("param1", "param2")));

  EXPECT_THAT(
      modify.Execute({{"param1", Int64(3)}, {"param2", String("three")}}),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Expected type not found for variable param1")));
}

TEST_F(PreparedModifyTest, PositionalParameter) {
  PreparedModify modify("insert test_table(int_val, str_val) values(?, ?)",
                        EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::DoubleType()));

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.ExecuteWithPositionalParams({Int64(3), String("three")}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, modify.ExecuteWithPositionalParams(
                {Int64(0), NullString(), NullDouble(), NullBytes()}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(0));
  EXPECT_EQ(iter->GetColumnValue(1), NullString());
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(modify.ExecuteWithPositionalParams({Int64(100)}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Too few positional parameters. Expected at "
                                 "least 2 but found only: 1")));
}

TEST_F(PreparedModifyTest, UndeclaredPositionalParameter) {
  PreparedModify modify("insert test_table(int_val, str_val) values(?, ?)",
                        EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_allow_undeclared_parameters(true);
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.ExecuteWithPositionalParams({Int64(3), String("three")}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyTest, UndeclaredPositionalParameterResolvedStmt) {
  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_allow_undeclared_parameters(true);
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement("insert test_table(int_val, str_val) values(?, ?)",
                             analyzer_options, catalog(), &type_factory,
                             &analyzer_output));

  PreparedModify modify(analyzer_output->resolved_statement(),
                        EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(2));

  EXPECT_THAT(
      modify.ExecuteWithPositionalParams({Int64(3), String("three")}),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Mismatch in number of analyzer parameters versus "
                    "algebrizer parameters")));
}

TEST_F(PreparedModifyTest, ExecuteAfterPrepareWithOrderedParamsWithParameter) {
  PreparedModify modify("insert test_table(int_val, str_val) values(@p1, @p2)",
                        EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p1", types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p2", types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p3", types::DoubleType()));

  GOOGLESQL_EXPECT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(),
              IsOkAndHolds(UnorderedElementsAre("p1", "p2")));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(0));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.Execute({{"p2", String("three")}, {"p1", Int64(3)}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, modify.Execute({{"p2", NullString()},
                                             {"p1", Int64(0)},
                                             {"p3", NullDouble()},
                                             {"p4", NullBytes()}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(0));
  EXPECT_EQ(iter->GetColumnValue(1), NullString());
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(modify.Execute({{"p2", values::NullString()}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Incomplete query parameters p1")));
}

TEST_F(PreparedModifyTest,
     ExecuteAfterPreparedWithOrderedParamsWithPositionalParameter) {
  PreparedModify modify("insert test_table(int_val, str_val) values(?, ?)",
                        EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::DoubleType()));

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableModifyIterator> iter,
      modify.ExecuteWithPositionalParams({Int64(3), String("three")}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  // Calling the ExecuteAfterPrepare variant.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, modify.ExecuteAfterPrepareWithOrderedParams(
                {Int64(0), NullString(), NullDouble(), NullBytes()}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(0));
  EXPECT_EQ(iter->GetColumnValue(1), NullString());
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(modify.ExecuteWithPositionalParams({Int64(100)}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Too few positional parameters. Expected at "
                                 "least 2 but found only: 1")));
}

TEST_F(PreparedModifyTest, ExplainAfterPrepareWithoutPrepare) {
  PreparedModify modify("insert test_table(int_val, str_val) values(0, null)",
                        EvaluatorOptions());
  EXPECT_THAT(modify.ExplainAfterPrepare(),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Prepare must be called first")));
}

// This test suite runs DML Returning statements using version 2 Execute API,
// and verify the returning clause results.
class PreparedDmlReturningTest : public PreparedModifyTest {
  void SetUp() override {
    PreparedModifyTest::SetUp();

    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_DML_RETURNING);
  }
};

TEST_F(PreparedDmlReturningTest, ExecutesInsert) {
  PreparedModify modify(
      "insert test_table(int_val, str_val) values(3, 'three') "
      "then return str_val, int_val + 1 as new_val",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_INSERT_STMT);

  std::unique_ptr<EvaluatorTableIterator> returning_iter;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute({}, {}, &returning_iter));

  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Validate RETURNING results.
  EXPECT_NE(returning_iter, nullptr);
  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetColumnName(0), "str_val");
  EXPECT_EQ(returning_iter->GetColumnName(1), "new_val");
  EXPECT_EQ("STRING", returning_iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("INT64", returning_iter->GetColumnType(1)->DebugString());

  EXPECT_EQ(returning_iter->GetValue(0), String("three"));
  EXPECT_EQ(returning_iter->GetValue(1), Int64(4));

  EXPECT_FALSE(returning_iter->NextRow());
  GOOGLESQL_EXPECT_OK(returning_iter->Status());
}

TEST_F(PreparedDmlReturningTest, ExecutesDelete) {
  PreparedModify modify(
      "delete test_table where int_val in (2, 4) then return *",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_DELETE_STMT);
  std::unique_ptr<EvaluatorTableIterator> returning_iter;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute({}, {}, &returning_iter));

  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
  EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kDelete);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
  EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kDelete);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Validate RETURNING results.
  EXPECT_NE(returning_iter, nullptr);
  EXPECT_EQ(returning_iter->NumColumns(), 2);
  EXPECT_EQ(returning_iter->GetColumnName(0), "int_val");
  EXPECT_EQ(returning_iter->GetColumnName(1), "str_val");
  EXPECT_EQ("INT64", returning_iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("STRING", returning_iter->GetColumnType(1)->DebugString());

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetValue(0), Int64(2));
  EXPECT_EQ(returning_iter->GetValue(1), String("two"));

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetValue(0), Int64(4));
  EXPECT_EQ(returning_iter->GetValue(1), String("four"));

  EXPECT_FALSE(returning_iter->NextRow());
  GOOGLESQL_EXPECT_OK(returning_iter->Status());
}

TEST_F(PreparedDmlReturningTest, ExecutesUpdate) {
  PreparedModify modify(
      "update test_table set str_val = 'foo' where int_val > 1 then return *",
      EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_UPDATE_STMT);
  std::unique_ptr<EvaluatorTableIterator> returning_iter;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.Execute({}, {}, &returning_iter));

  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

  EXPECT_EQ(iter->table(), table);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(2));
  EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kUpdate);

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(4));
  EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
  EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kUpdate);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Validate RETURNING results.
  EXPECT_NE(returning_iter, nullptr);
  EXPECT_EQ(returning_iter->GetColumnName(0), "int_val");
  EXPECT_EQ(returning_iter->GetColumnName(1), "str_val");
  EXPECT_EQ("INT64", returning_iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("STRING", returning_iter->GetColumnType(1)->DebugString());

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetValue(0), Int64(2));
  EXPECT_EQ(returning_iter->GetValue(1), String("foo"));

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetValue(0), Int64(4));
  EXPECT_EQ(returning_iter->GetValue(1), String("foo"));

  EXPECT_FALSE(returning_iter->NextRow());
  GOOGLESQL_EXPECT_OK(returning_iter->Status());
}

TEST_F(PreparedDmlReturningTest, PositionalParameter) {
  PreparedModify modify(
      "insert test_table(int_val, str_val) values(?, ?) "
      "then return str_val, int_val + 1 AS new_val",
      EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::DoubleType()));

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(2));

  std::unique_ptr<EvaluatorTableIterator> returning_iter;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.ExecuteWithPositionalParams(
                           {Int64(3), String("three")}, {}, &returning_iter));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
  iter.reset();

  // Validate RETURNING results.
  EXPECT_NE(returning_iter, nullptr);
  EXPECT_EQ(returning_iter->GetColumnName(0), "str_val");
  EXPECT_EQ(returning_iter->GetColumnName(1), "new_val");
  EXPECT_EQ("STRING", returning_iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("INT64", returning_iter->GetColumnType(1)->DebugString());

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetValue(0), String("three"));
  EXPECT_EQ(returning_iter->GetValue(1), Int64(4));

  EXPECT_FALSE(returning_iter->NextRow());
  GOOGLESQL_EXPECT_OK(returning_iter->Status());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, modify.ExecuteWithPositionalParams(
                {Int64(0), NullString(), NullDouble(), NullBytes()}, {},
                &returning_iter));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(0));
  EXPECT_EQ(iter->GetColumnValue(1), NullString());
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
  iter.reset();

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetColumnName(0), "str_val");
  EXPECT_EQ(returning_iter->GetValue(0), NullString());
  EXPECT_EQ(returning_iter->GetColumnName(1), "new_val");
  EXPECT_EQ(returning_iter->GetValue(1), Int64(1));

  EXPECT_THAT(
      modify.ExecuteWithPositionalParams({Int64(100)}, {}, &returning_iter),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Too few positional parameters. Expected at least 2 "
                         "but found only: 1")));
}

TEST_F(PreparedDmlReturningTest,
       ExecuteAfterPreparedWithOrderedParamsWithPositionalParameter) {
  PreparedModify modify(
      "insert test_table(int_val, str_val) values(?, ?) then return str_val, "
      "int_val + 1 AS new_val",
      EvaluatorOptions());

  AnalyzerOptions analyzer_options = PreparedModifyTest::analyzer_options();
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::DoubleType()));

  GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options, catalog()));

  EXPECT_THAT(modify.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(modify.GetPositionalParameterCount(), IsOkAndHolds(2));

  std::unique_ptr<EvaluatorTableIterator> returning_iter;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       modify.ExecuteWithPositionalParams(
                           {Int64(3), String("three")}, {}, &returning_iter));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), String("three"));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
  iter.reset();

  // Validate RETURNING results.
  EXPECT_NE(returning_iter, nullptr);
  EXPECT_EQ(returning_iter->GetColumnName(0), "str_val");
  EXPECT_EQ(returning_iter->GetColumnName(1), "new_val");
  EXPECT_EQ("STRING", returning_iter->GetColumnType(0)->DebugString());
  EXPECT_EQ("INT64", returning_iter->GetColumnType(1)->DebugString());

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetValue(0), String("three"));
  EXPECT_EQ(returning_iter->GetValue(1), Int64(4));

  EXPECT_FALSE(returning_iter->NextRow());
  GOOGLESQL_EXPECT_OK(returning_iter->Status());

  // Calling the ExecuteAfterPrepare variant.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, modify.ExecuteAfterPrepareWithOrderedParams(
                {Int64(0), NullString(), NullDouble(), NullBytes()}, {},
                &returning_iter));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(0));
  EXPECT_EQ(iter->GetColumnValue(1), NullString());
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
  iter.reset();

  ASSERT_TRUE(returning_iter->NextRow());
  EXPECT_EQ(returning_iter->GetColumnName(0), "str_val");
  EXPECT_EQ(returning_iter->GetValue(0), NullString());
  EXPECT_EQ(returning_iter->GetColumnName(1), "new_val");
  EXPECT_EQ(returning_iter->GetValue(1), Int64(1));

  EXPECT_THAT(
      modify.ExecuteWithPositionalParams({Int64(100)}, {}, &returning_iter),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Too few positional parameters. Expected at least 2 "
                         "but found only: 1")));
}

TEST_F(PreparedDmlReturningTest, VerifyReturningIteratorNullForRegularDmls) {
  const Table* table;
  PreparedQuery query("select * from test_table", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog()));

  {
    PreparedModify insert_stmt(
        "insert test_table(int_val, str_val) values(3, 'three')",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(insert_stmt.Prepare(analyzer_options(), catalog()));
    ASSERT_EQ(insert_stmt.resolved_statement()->node_kind(),
              RESOLVED_INSERT_STMT);

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> returning_iter,
                         query.Execute());
    EXPECT_NE(returning_iter, nullptr);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                         insert_stmt.Execute({}, {}, &returning_iter));
    EXPECT_EQ(returning_iter, nullptr);
    GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));
    EXPECT_EQ(iter->table(), table);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
    EXPECT_EQ(iter->GetColumnValue(1), String("three"));
    EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kInsert);
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }

  {
    PreparedModify update_stmt(
        "update test_table set str_val = 'foo' where int_val in (2, 4)",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(update_stmt.Prepare(analyzer_options(), catalog()));
    ASSERT_EQ(update_stmt.resolved_statement()->node_kind(),
              RESOLVED_UPDATE_STMT);

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> returning_iter,
                         query.Execute());
    EXPECT_NE(returning_iter, nullptr);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                         update_stmt.Execute({}, {}, &returning_iter));
    EXPECT_EQ(returning_iter, nullptr);
    GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));
    EXPECT_EQ(iter->table(), table);

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetColumnValue(0), Int64(2));
    EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kUpdate);

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetColumnValue(0), Int64(4));
    EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kUpdate);
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }

  {
    PreparedModify delete_stmt("delete test_table where int_val in (2, 4)",
                               EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(delete_stmt.Prepare(analyzer_options(), catalog()));
    ASSERT_EQ(delete_stmt.resolved_statement()->node_kind(),
              RESOLVED_DELETE_STMT);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> returning_iter,
                         query.Execute());
    EXPECT_NE(returning_iter, nullptr);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                         delete_stmt.Execute({}, {}, &returning_iter));
    EXPECT_EQ(returning_iter, nullptr);
    GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));
    EXPECT_EQ(iter->table(), table);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
    EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kDelete);

    ASSERT_TRUE(iter->NextRow());
    EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
    EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kDelete);
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
}

TEST_F(PreparedDmlReturningTest, ExecuteWithoutReturningIterator) {
  {
    PreparedModify modify(
        "insert test_table(int_val, str_val) values(3, 'three') "
        "then return str_val, int_val + 1 as new_val",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
    ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_INSERT_STMT);

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                         modify.Execute({}, {}));
    const Table* table;
    GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

    EXPECT_EQ(iter->table(), table);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
    EXPECT_EQ(iter->GetColumnValue(1), String("three"));
    EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kInsert);
    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
  {
    PreparedModify modify(
        "delete test_table where int_val in (2, 4) then return *",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
    ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_DELETE_STMT);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                         modify.Execute({}, {}));

    const Table* table;
    GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

    EXPECT_EQ(iter->table(), table);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
    EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kDelete);

    ASSERT_TRUE(iter->NextRow());
    EXPECT_FALSE(iter->GetColumnValue(0).is_valid());
    EXPECT_FALSE(iter->GetColumnValue(1).is_valid());
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kDelete);

    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }

  {
    PreparedModify modify(
        "update test_table set str_val = 'foo' where int_val in (2, 4) "
        "then return *",
        EvaluatorOptions());
    GOOGLESQL_ASSERT_OK(modify.Prepare(analyzer_options(), catalog()));
    ASSERT_EQ(modify.resolved_statement()->node_kind(), RESOLVED_UPDATE_STMT);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                         modify.Execute({}, {}));

    const Table* table;
    GOOGLESQL_ASSERT_OK(catalog()->FindTable({"test_table"}, &table));

    EXPECT_EQ(iter->table(), table);

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetColumnValue(0), Int64(2));
    EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(2));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kUpdate);

    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetColumnValue(0), Int64(4));
    EXPECT_EQ(iter->GetColumnValue(1), String("foo"));
    EXPECT_EQ(iter->GetOriginalKeyValue(0), Int64(4));
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kUpdate);

    EXPECT_FALSE(iter->NextRow());
    GOOGLESQL_EXPECT_OK(iter->Status());
  }
}

class PreparedModifyWithDefaultColumnTest : public ::testing::Test {
 public:
  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    AddTableWithDefaultColumn();

    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
  }

  Catalog* catalog() { return &catalog_; }
  const AnalyzerOptions& analyzer_options() { return analyzer_options_; }

 protected:
  void AddTableWithDefaultColumn() {
    auto test_table = std::make_unique<SimpleTable>(
        kTestTable,
        std::vector<SimpleTable::NameAndType>{{"id", types::Int64Type()}});

    const std::string default_expr = "10";
    GOOGLESQL_CHECK_OK(AnalyzeExpression(default_expr, analyzer_options_, &catalog_,
                               catalog_.type_factory(), &output_));

    SimpleColumn::ExpressionAttributes expr_attributes(
        SimpleColumn::ExpressionAttributes::ExpressionKind::DEFAULT,
        default_expr, output_->resolved_expr());

    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "d", types::Int64Type(),
                         {.column_expression = expr_attributes}),
        /*is_owned=*/true));

    test_table->SetContents({{Int64(1), Int64(1)}, {Int64(2), Int64(2)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  SimpleCatalog catalog_{"test_catalog"};
  AnalyzerOptions analyzer_options_;
  std::unique_ptr<const AnalyzerOutput> output_;

  // Table names
  static constexpr char kTestTable[] = "T";
};

TEST_F(PreparedModifyWithDefaultColumnTest, InsertWithExplicitDefault) {
  PreparedModify insert_stmt("INSERT T (id, d) VALUES (3, DEFAULT)",
                             EvaluatorOptions());

  GOOGLESQL_ASSERT_OK(insert_stmt.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(insert_stmt.resolved_statement()->node_kind(),
            RESOLVED_INSERT_STMT);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       insert_stmt.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"T"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
  EXPECT_EQ(iter->GetColumnValue(1), Int64(10));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyWithDefaultColumnTest, InsertWithImplicitDefault) {
  PreparedModify insert_stmt("INSERT T (id) VALUES (4)", EvaluatorOptions());

  GOOGLESQL_ASSERT_OK(insert_stmt.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(insert_stmt.resolved_statement()->node_kind(),
            RESOLVED_INSERT_STMT);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       insert_stmt.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"T"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(4));
  EXPECT_EQ(iter->GetColumnValue(1), Int64(10));
  EXPECT_FALSE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST_F(PreparedModifyWithDefaultColumnTest, UpdateToDefault) {
  PreparedModify insert_stmt("UPDATE T SET d=DEFAULT WHERE id=2",
                             EvaluatorOptions());

  GOOGLESQL_ASSERT_OK(insert_stmt.Prepare(analyzer_options(), catalog()));
  ASSERT_EQ(insert_stmt.resolved_statement()->node_kind(),
            RESOLVED_UPDATE_STMT);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                       insert_stmt.Execute());
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog()->FindTable({"T"}, &table));

  EXPECT_EQ(iter->table(), table);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetColumnValue(0), Int64(2));
  EXPECT_EQ(iter->GetColumnValue(1), Int64(10));
  EXPECT_TRUE(iter->GetOriginalKeyValue(0).is_valid());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kUpdate);

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

struct TestQuery {
  std::string test_name;
  std::string sql;
  std::vector<std::vector<Value>> expected_rows;
  ParameterValueMap params;
  absl::StatusCode error_code;
  std::string error_message;
};

class PreparedModifyWithGeneratedColumnTest
    : public ::testing::TestWithParam<TestQuery> {
 public:
  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
    AddTableWithGeneratedColumn();
    AddTableWithGeneratedColumnAsPK();
    AddTableWithConstantGeneratedColumn();
    AddTableWithCyclicDependency(/*for_gpk=*/true);
    AddTableWithCyclicDependency(/*for_gpk=*/false);
    AddTableWithNonCyclicDependency();
    AddTableWithNonCyclicDependencyForGPK();
    AddTableT1();
    AddTableT2();
    AddTableT3();
  }
  absl::StatusOr<std::vector<std::vector<Value>>> GetInsertRows(
      std::unique_ptr<EvaluatorTableModifyIterator> iter) {
    std::vector<std::vector<Value>> rows;
    while (iter->NextRow()) {
      std::vector<Value> row;
      int num_col = iter->table()->NumColumns();
      for (int i = 0; i < num_col; ++i) {
        row.push_back(iter->GetColumnValue(i));
      }
      rows.push_back(row);
    }
    return rows;
  }

  Catalog* catalog() { return &catalog_; }
  const AnalyzerOptions& analyzer_options() { return analyzer_options_; }

 protected:
  void AddGeneratedColumnToTable(std::string column_name,
                                 std::vector<std::string> expression_columns,
                                 std::string generated_expr,
                                 SimpleTable* table) {
    AnalyzerOptions options = analyzer_options_;
    std::unique_ptr<const AnalyzerOutput> output;
    for (const std::string& expression_column : expression_columns) {
      GOOGLESQL_ASSERT_OK(
          options.AddExpressionColumn(expression_column, types::Int64Type()));
    }
    GOOGLESQL_ASSERT_OK(AnalyzeExpression(generated_expr, options, &catalog_,
                                catalog_.type_factory(), &output));
    SimpleColumn::ExpressionAttributes expr_attributes(
        SimpleColumn::ExpressionAttributes::ExpressionKind::GENERATED,
        generated_expr, output->resolved_expr());
    GOOGLESQL_ASSERT_OK(table->AddColumn(
        new SimpleColumn(table->Name(), column_name, types::Int64Type(),
                         {.column_expression = expr_attributes}),
        /*is_owned=*/true));
    outputs_.push_back(std::move(output));
  }
  void AddTableWithCyclicDependency(bool for_gpk) {
    // Table representation
    // CREATE TABLE TCyclic (
    //  id int64 ,
    //  data int64 ,
    //  gen1 as gen2*2 ,
    //  gen2 as gen1*2 ,
    // ) PRIMARY KEY(id,gen1)
    auto test_table = std::make_unique<SimpleTable>(
        for_gpk ? "TCyclicGPK" : "TCyclic",
        std::vector<SimpleTable::NameAndType>{{"id", types::Int64Type()},
                                              {"data", types::Int64Type()}});

    // Add column gen2 as gen1*2
    AddGeneratedColumnToTable("gen2", {"gen1"}, "gen1*2", test_table.get());
    // Add column gen1 as gen2*2
    AddGeneratedColumnToTable("gen1", {"gen2"}, "gen2*2", test_table.get());
    test_table->SetContents({{Int64(1), Int64(2), Int64(8), Int64(4)},
                             {Int64(2), Int64(3), Int64(12), Int64(6)}});
    if (for_gpk) {
      GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    } else {
      GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0}));
    }

    catalog_.AddOwnedTable(std::move(test_table));
  }

  void AddTableWithGeneratedColumnAsPK() {
    // Table representation
    // CREATE TABLE T (
    //  id int64 ,
    //  data int64 ,
    //  gen2 as (gen1*2) ,
    //  gen1 as (data*2) ,
    //  gen3 as (data*3) ,
    // ) PRIMARY KEY(id,gen1)
    auto test_table = std::make_unique<SimpleTable>(
        kTestTable,
        std::vector<SimpleTable::NameAndType>{{"id", types::Int64Type()},
                                              {"data", types::Int64Type()}});
    // Add column gen2 as gen1*2
    AddGeneratedColumnToTable("gen2", {"gen1"}, "gen1*2", test_table.get());
    // Add column gen1 as data*2
    AddGeneratedColumnToTable("gen1", {"data"}, "data*2", test_table.get());
    // Add column gen3 as data*3
    AddGeneratedColumnToTable("gen3", {"data"}, "data*3", test_table.get());

    test_table->SetContents(
        {{Int64(1), Int64(2), Int64(8), Int64(4), Int64(6)},
         {Int64(2), Int64(3), Int64(12), Int64(6), Int64(9)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  void AddTableWithGeneratedColumn() {
    // Table representation
    // CREATE TABLE TGen (
    //  id int64 ,
    //  data int64 ,
    //  gen2 as (gen1*2) ,
    //  gen1 as (data*2) ,
    //  gen3 as (data*3) ,
    // ) PRIMARY KEY(id)
    auto test_table = std::make_unique<SimpleTable>(
        "TGen", std::vector<SimpleTable::NameAndType>{
                    {"id", types::Int64Type()}, {"data", types::Int64Type()}});
    // Add column gen2 as gen1*2
    AddGeneratedColumnToTable("gen2", {"gen1"}, "gen1*2", test_table.get());
    // Add column gen1 as data*2
    AddGeneratedColumnToTable("gen1", {"data"}, "data*2", test_table.get());
    // Add column gen3 as data*3
    AddGeneratedColumnToTable("gen3", {"data"}, "data*3", test_table.get());

    test_table->SetContents(
        {{Int64(1), Int64(2), Int64(8), Int64(4), Int64(6)},
         {Int64(2), Int64(3), Int64(12), Int64(6), Int64(9)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  void AddTableWithConstantGeneratedColumn() {
    // Table representation
    // CREATE TABLE TGenConst (
    //  id int64 ,
    //  data int64 ,
    //  gen1 as (7+3) ,
    // ) PRIMARY KEY(id,gen1)
    auto test_table = std::make_unique<SimpleTable>(
        "TGenConst",
        std::vector<SimpleTable::NameAndType>{{"id", types::Int64Type()},
                                              {"data", types::Int64Type()}});

    // Add column gen1 as (7+3)
    AddGeneratedColumnToTable("gen1", {}, "7+3", test_table.get());
    test_table->SetContents(
        {{Int64(1), Int64(2), Int64(10)}, {Int64(2), Int64(3), Int64(10)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  void AddTableT1() {
    // CREATE TABLE T1 (
    //   a INT64,
    //   b INT64,
    //   c INT64,
    // ) PRIMARY KEY(a);
    auto t1 = std::make_unique<SimpleTable>(
        "T1", std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()},
                                                    {"b", types::Int64Type()},
                                                    {"c", types::Int64Type()}});
    // contents of T1
    // a b c
    // 1 2 2
    // 2 3 3
    // 3 4 4
    t1->SetContents({{Int64(1), Int64(2), Int64(2)},
                     {Int64(2), Int64(3), Int64(3)},
                     {Int64(3), Int64(4), Int64(4)}});
    catalog_.AddOwnedTable(std::move(t1));
  }
  void AddTableT2() {
    // CREATE TABLE T2 (
    //   a INT64,
    //   b INT64,
    //   c INT64 AS (b*2),
    // ) PRIMARY KEY(a);
    auto t2 = std::make_unique<SimpleTable>(
        "T2", std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()},
                                                    {"b", types::Int64Type()}});
    AddGeneratedColumnToTable("c", {"b"}, "b*2", t2.get());
    t2->SetContents({{Int64(4), Int64(5), Int64(10)}});
    catalog_.AddOwnedTable(std::move(t2));
  }
  void AddTableT3() {
    // CREATE TABLE T3 (
    //   a INT64,
    //   b INT64,
    //   c INT64,
    //   d INT64 AS (b*2),
    // ) PRIMARY KEY(a);
    auto t3 = std::make_unique<SimpleTable>(
        "T3", std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()},
                                                    {"b", types::Int64Type()},
                                                    {"c", types::Int64Type()}});
    AddGeneratedColumnToTable("d", {"b"}, "b*2", t3.get());
    // contents of T3
    // a b c d
    // 1 2 4 4
    // 4 3 4 6
    // 3 3 8 6
    t3->SetContents({{Int64(1), Int64(2), Int64(4), Int64(4)},
                     {Int64(4), Int64(3), Int64(4), Int64(6)},
                     {Int64(3), Int64(3), Int64(8), Int64(6)}});
    catalog_.AddOwnedTable(std::move(t3));
  }

  void AddTableWithNonCyclicDependencyForGPK() {
    // Table representation
    // CREATE TABLE TA (
    //  A as (B+C) ,
    //  B as (C+1) ,
    //  C int64 ,
    //  D as (A+B+C) ,
    // ) PRIMARY KEY(A,C)
    auto test_table = std::make_unique<SimpleTable>(
        "TA", std::vector<SimpleTable::NameAndType>{});

    // Add column A as (B+C)
    AddGeneratedColumnToTable("A", {"B", "C"}, "B+C", test_table.get());
    // Add column B as (C+1)
    AddGeneratedColumnToTable("B", {"C"}, "C+1", test_table.get());
    // Add column C int64
    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "C", types::Int64Type()),
        /*is_owned=*/true));
    // Add column D as (A+B+C)
    AddGeneratedColumnToTable("D", {"A", "B", "C"}, "A+B+C", test_table.get());

    test_table->SetContents({{Int64(3), Int64(2), Int64(1), Int64(6)},
                             {Int64(5), Int64(3), Int64(2), Int64(10)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  void AddTableWithNonCyclicDependency() {
    // Table representation
    // CREATE TABLE TGenA (
    //  A int64,
    //  B as (C+1) ,
    //  C int64 ,
    //  D as (A+B+C) ,
    // ) PRIMARY KEY(A)
    auto test_table = std::make_unique<SimpleTable>(
        "TGenA", std::vector<SimpleTable::NameAndType>{});

    // Add column A as (B+C)
    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "A", types::Int64Type()),
        /*is_owned=*/true));
    // Add column B as (C+1)
    AddGeneratedColumnToTable("B", {"C"}, "C+1", test_table.get());
    // Add column C int64
    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "C", types::Int64Type()),
        /*is_owned=*/true));
    // Add column D as (A+B+C)
    AddGeneratedColumnToTable("D", {"A", "B", "C"}, "A+B+C", test_table.get());

    test_table->SetContents({{Int64(1), Int64(2), Int64(1), Int64(4)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0}));
    catalog_.AddOwnedTable(std::move(test_table));
  }

  absl::StatusOr<std::vector<std::vector<Value>>> ExecuteDmlQuery(
      std::string sql, ParameterValueMap params) {
    PreparedModify dml_stmt(sql, EvaluatorOptions());
    AnalyzerOptions query_options = analyzer_options();
    for (auto& [param_name, param_value] : params) {
      GOOGLESQL_RETURN_IF_ERROR(
          query_options.AddQueryParameter(param_name, types::Int64Type()));
    }
    GOOGLESQL_RETURN_IF_ERROR(dml_stmt.Prepare(query_options, catalog()));
    GOOGLESQL_RET_CHECK(
        dml_stmt.resolved_statement()->node_kind() == RESOLVED_INSERT_STMT ||
        dml_stmt.resolved_statement()->node_kind() == RESOLVED_UPDATE_STMT)
        << dml_stmt.resolved_statement()->node_kind_string();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<EvaluatorTableModifyIterator> iter,
                     dml_stmt.Execute(params));
    return GetInsertRows(std::move(iter));
  }

  SimpleCatalog catalog_{"test_catalog"};
  AnalyzerOptions analyzer_options_;
  std::vector<std::unique_ptr<const AnalyzerOutput>> outputs_;

  // Table names
  static constexpr char kTestTable[] = "T";
};

TEST_P(PreparedModifyWithGeneratedColumnTest, TestQuery) {
  TestQuery test_query = GetParam();
  auto result = ExecuteDmlQuery(test_query.sql, test_query.params);
  ASSERT_THAT(result.status(),
              StatusIs(test_query.error_code,
                       testing::MatchesRegex(test_query.error_message)));
  if (result.ok()) {
    EXPECT_EQ(result.value(), test_query.expected_rows);
  }
}

INSTANTIATE_TEST_SUITE_P(
    TestQuery, PreparedModifyWithGeneratedColumnTest,
    testing::ValuesIn<TestQuery>({
        {"simple_select",
         R"sql(INSERT INTO T2(a,b) SELECT a,b FROM T1)sql",
         {{Int64(1), Int64(2), Int64(4)},
          {Int64(2), Int64(3), Int64(6)},
          {Int64(3), Int64(4), Int64(8)}},
         {},
         absl::StatusCode::kOk,
         ""},
        // Verifies that writing to a generated column doesn't have any effect.
        // The generated column values are always derived from the generated
        // expression. Trying to copy all data from T1(a,b,c) to T2(a,b,c).
        // As T2.c is a generated column, the copied values will be ignored and
        // will be evaluated through the generated expression.
        {"write_to_generated_column_with_select",
         R"sql(INSERT INTO T2(a,b,c) SELECT * FROM T1)sql",
         {{Int64(1), Int64(2), Int64(4)},
          {Int64(2), Int64(3), Int64(6)},
          {Int64(3), Int64(4), Int64(8)}},
         {},
         absl::StatusCode::kOk,
         ""},
        // Selects two rows , say R1 & R2 from table T3 where R1.d = R2.d
        // Then adds a row R into the table with R.a = R1.a + R2.a,
        // R.b = R1.b + R2.b, R.c = R1.c + R2.c.
        {"self_joins",
         R"sql(INSERT INTO T3(a,b,c)
               SELECT P.a+Q.a,P.b+Q.b,P.c+Q.c FROM T3 P, T3 Q
               WHERE P.d = Q.d AND P.a < Q.a)sql",
         {{Int64(7), Int64(6), Int64(12), Int64(12)}},
         {},
         absl::StatusCode::kOk,
         ""},
        // Verifies if insert queries work as expected when query parameters
        // with same name as that of a columns of table being inserted to are
        // used.
        {"select_parameters_conflicting_with_inserting_columns",
         R"sql(INSERT INTO T2(a,b)
               SELECT DISTINCT @a,@b FROM T2)sql",
         {{Int64(12), Int64(10), Int64(20)}},
         // Adding parameters named `a` & `b` and selecting those to make sure
         // that these don't conflict with the column names (parameters
         // values are picked, not generated column).
         {{"b", values::Int64(10)}, {"a", values::Int64(12)}},
         absl::StatusCode::kOk,
         ""},
        // Verifies if insert queries work as expected when query parameters
        // with same name as that of target generated column of a table are
        // used.
        {"select_parameters_conflicting_with_target_gen_columns",
         R"sql(INSERT INTO T2(a,b)
               SELECT DISTINCT @b,@c FROM T2)sql",
         {{Int64(10), Int64(12), Int64(24)}},
         // Adding parameters named `b` & `c` and selecting those to make sure
         // that these don't conflict with the column names (parameters values
         // are picked, not generated column).
         {{"b", values::Int64(10)}, {"c", values::Int64(12)}},
         absl::StatusCode::kOk,
         ""},
        {"simple",
         R"sql(INSERT INTO T (id,data) VALUES (3,7),(3,6))sql",
         {{Int64(3), Int64(7), Int64(28), Int64(14), Int64(21)},
          {Int64(3), Int64(6), Int64(24), Int64(12), Int64(18)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"tgen_simple_insert",
         R"sql(INSERT INTO TGen (id,data) VALUES (3,7),(3,6))sql",
         {{Int64(3), Int64(7), Int64(28), Int64(14), Int64(21)},
          {Int64(3), Int64(6), Int64(24), Int64(12), Int64(18)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"tgen_simple_update",
         R"sql(UPDATE TGen SET data = 4 WHERE ID = 1)sql",
         {{Int64(1), Int64(4), Int64(16), Int64(8), Int64(12)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"const_gen_col",
         R"sql(INSERT INTO TGenConst (id,data) VALUES (3,7),(4,6))sql",
         {{Int64(3), Int64(7), Int64(10)}, {Int64(4), Int64(6), Int64(10)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"const_gen_col_update",
         R"sql(UPDATE TGenConst SET data = 5 WHERE id > 0)sql",
         {{Int64(1), Int64(5), Int64(10)}, {Int64(2), Int64(5), Int64(10)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"topological_order",
         R"sql(INSERT INTO TA (C) VALUES (3),(4))sql",
         {{Int64(7), Int64(4), Int64(3), Int64(14)},
          {Int64(9), Int64(5), Int64(4), Int64(18)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"topological_order_gen",
         R"sql(UPDATE TGenA SET C = 3 WHERE A = 1)sql",
         {{Int64(1), Int64(4), Int64(3), Int64(8)}},
         {},
         absl::StatusCode::kOk,
         ""},
        {"parameters_conflicting_with_target_gen_col",
         R"sql(INSERT INTO T3(a,b,c) VALUES (2,6,@d))sql",
         {{Int64(2), Int64(6), Int64(4), Int64(12)}},
         {{"d", values::Int64(4)}},
         absl::StatusCode::kOk,
         ""},
        {"parameters_conflicting_with_target_gen_col_for_update",
         R"sql(UPDATE T3 SET c = @d WHERE a = 1)sql",
         {{Int64(1), Int64(2), Int64(5), Int64(4)}},
         {{"d", values::Int64(5)}},
         absl::StatusCode::kOk,
         ""},
        {"parameters_conflicting_with_incoming_dependent_col",
         R"sql(INSERT INTO T3(a,b,c) VALUES (2,@b,4))sql",
         {{Int64(2), Int64(6), Int64(4), Int64(12)}},
         {{"b", values::Int64(6)}},
         absl::StatusCode::kOk,
         ""},
        {"parameters_conflicting_with_incoming_dependent_col_for_update",
         R"sql(UPDATE T3 SET b = @d WHERE a = 1)sql",
         {{Int64(1), Int64(6), Int64(4), Int64(12)}},
         {{"d", values::Int64(6)}},
         absl::StatusCode::kOk,
         ""},
        {"duplicate_pk",
         R"sql(INSERT INTO T (id,data) VALUES (2,3))sql",
         {},
         {},
         absl::StatusCode::kOutOfRange,
         "Failed to insert row with primary key.*id:2.*gen2:12.*"},
        {"cyclic_generated_columns",
         R"sql(INSERT INTO TCyclic (id,data) VALUES (3,7),(3,6))sql",
         {},
         {},
         absl::StatusCode::kInvalidArgument,
         "Recursive dependencies detected while evaluating generated "
         "column expression values for TCyclic.gen.. The expression "
         "indicates the column depends on itself. Columns forming "
         "the cycle : TCyclic.gen., TCyclic.gen."},
        {"cyclic_generated_columns_with_update",
         R"sql(UPDATE TCyclic SET data = 7 WHERE id = 1)sql",
         {},
         {},
         absl::StatusCode::kInvalidArgument,
         "Recursive dependencies detected while evaluating generated "
         "column expression values for TCyclic.gen.. The expression "
         "indicates the column depends on itself. Columns forming "
         "the cycle : TCyclic.gen., TCyclic.gen."},
        {"cyclic_generated_columns_gpk",
         R"sql(INSERT INTO TCyclicGPK (id,data) VALUES (3,7),(3,6))sql",
         {},
         {},
         absl::StatusCode::kInvalidArgument,
         "Recursive dependencies detected while evaluating generated "
         "column expression values for TCyclicGPK.gen.. The expression "
         "indicates the column depends on itself. Columns forming "
         "the cycle : TCyclicGPK.gen., TCyclicGPK.gen."},
    }),
    [](const testing::TestParamInfo<
        PreparedModifyWithGeneratedColumnTest::ParamType>& info) {
      return info.param.test_name;
    });

TEST(PreparedQuery, FromTableOnlySecondColumn) {
  SimpleTable test_table(
      "TestTable", {{"a", types::Int64Type()}, {"b", types::StringType()}});
  test_table.SetContents({{Int64(10), String("foo")},
                          {Int64(20), String("bar")},
                          {Int64(30), String("baz")}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  PreparedQuery query("select b from TestTable", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), &catalog));

  EXPECT_EQ(1, query.num_columns());
  EXPECT_EQ("b", query.column_name(0));
  EXPECT_EQ("STRING", query.column_type(0)->DebugString());

  EXPECT_FALSE(query.is_value_table());
  EXPECT_EQ(1, query.resolved_query_stmt()->output_column_list_size());

  EXPECT_THAT(query.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());
  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("b", iter->GetColumnName(0));
  EXPECT_EQ("STRING", iter->GetColumnType(0)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(String("foo"), iter->GetValue(0));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(String("bar"), iter->GetValue(0));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(String("baz"), iter->GetValue(0));

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, FromTableFailure) {
  const std::string error = "Failed to read row from TestTable";
  const absl::Status failure = googlesql_base::OutOfRangeErrorBuilder() << error;

  EvaluatorTestTable test_table("TestTable", {{"a", types::Int64Type()}},
                                {{Int64(10)}, {Int64(20)}, {Int64(30)}},
                                failure);

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  PreparedQuery query("select a from TestTable", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  ASSERT_EQ(1, iter->NumColumns());
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(10), iter->GetValue(0));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(20), iter->GetValue(0));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(30), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  EXPECT_THAT(iter->Status(), StatusIs(absl::StatusCode::kOutOfRange, error));
}

TEST(PreparedQuery, FromTableCancellation) {
  SimpleTable test_table("TestTable", {{"a", types::Int64Type()}});
  test_table.SetContents({{Int64(10)}, {Int64(20)}, {Int64(30)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  PreparedQuery query("select a from TestTable", EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  ASSERT_EQ(1, iter->NumColumns());
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(10), iter->GetValue(0));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(20), iter->GetValue(0));

  GOOGLESQL_EXPECT_OK(iter->Cancel());
  EXPECT_FALSE(iter->NextRow());
  EXPECT_THAT(iter->Status(), StatusIs(absl::StatusCode::kCancelled, _));
}

TEST(PreparedQuery, FromTableDeadlineExceeded) {
  absl::SetFlag(&FLAGS_googlesql_simple_iterator_call_time_now_rows_period, 1);

  googlesql_base::SimulatedClock clock(absl::UnixEpoch());

  EvaluatorTestTable test_table(
      "TestTable", {{"a", types::Int64Type()}},
      {{Int64(10)}, {Int64(20)}, {Int64(30)}},
      /*end_status=*/absl::OkStatus(),
      /*column_filter_idxs=*/{},
      /*cancel_cb=*/[]() {},
      /*set_deadline_cb=*/[](absl::Time /*deadline*/) {}, &clock);

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  EvaluatorOptions options;
  options.clock = &clock;

  PreparedQuery query("select a from TestTable", options);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());
  iter->SetDeadline(clock.TimeNow() + absl::Seconds(10));

  ASSERT_EQ(1, iter->NumColumns());
  ASSERT_TRUE(iter->NextRow()) << iter->Status();
  EXPECT_EQ(Int64(10), iter->GetValue(0));

  clock.AdvanceTime(absl::Seconds(15));
  EXPECT_FALSE(iter->NextRow());
  EXPECT_THAT(iter->Status(), StatusIs(absl::StatusCode::kDeadlineExceeded, _));
}

TEST(PreparedQuery, OutputIsValueTable) {
  PreparedQuery query("select as value 1 a", EvaluatorOptions());
  GOOGLESQL_EXPECT_OK(query.Prepare(AnalyzerOptions()));

  EXPECT_EQ(1, query.num_columns());
  EXPECT_EQ("", query.column_name(0));
  EXPECT_EQ("INT64", query.column_type(0)->DebugString());

  std::vector<PreparedQuery::NameAndType> columns = query.GetColumns();
  EXPECT_EQ(1, columns.size());
  EXPECT_EQ("", columns[0].first);
  EXPECT_EQ("INT64", columns[0].second->DebugString());

  EXPECT_TRUE(query.is_value_table());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("", iter->GetColumnName(0));
  EXPECT_EQ("INT64", iter->GetColumnType(0)->DebugString());

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(1), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, TwoIteratorsAtTheSameTime) {
  SimpleTable test_table("TestTable", {{"a", types::Int64Type()}});
  test_table.SetContents({{Int64(10)}, {Int64(20)}, {Int64(30)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  googlesql_base::SimulatedClock clock;
  EvaluatorOptions options;
  options.clock = &clock;

  PreparedQuery query(
      "select a, unix_seconds(current_timestamp()) from TestTable", options);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter1,
                       query.Execute());

  ASSERT_TRUE(iter1->NextRow());
  EXPECT_EQ(Int64(10), iter1->GetValue(0));
  EXPECT_EQ(Int64(0), iter1->GetValue(1));

  clock.AdvanceTime(absl::Seconds(1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter2,
                       query.Execute());

  ASSERT_TRUE(iter1->NextRow());
  EXPECT_EQ(Int64(20), iter1->GetValue(0));
  EXPECT_EQ(Int64(0), iter1->GetValue(1));

  ASSERT_TRUE(iter2->NextRow());
  EXPECT_EQ(Int64(10), iter2->GetValue(0));
  EXPECT_EQ(Int64(1), iter2->GetValue(1));

  clock.AdvanceTime(absl::Seconds(1));

  ASSERT_TRUE(iter1->NextRow());
  EXPECT_EQ(Int64(30), iter1->GetValue(0));
  EXPECT_EQ(Int64(0), iter1->GetValue(1));

  ASSERT_TRUE(iter2->NextRow());
  EXPECT_EQ(Int64(20), iter2->GetValue(0));
  EXPECT_EQ(Int64(1), iter2->GetValue(1));

  clock.AdvanceTime(absl::Seconds(1));

  EXPECT_FALSE(iter1->NextRow());

  ASSERT_TRUE(iter2->NextRow());
  EXPECT_EQ(Int64(30), iter2->GetValue(0));
  EXPECT_EQ(Int64(1), iter2->GetValue(1));
  EXPECT_FALSE(iter2->NextRow());
}

TEST(PreparedQuery, Error) {
  PreparedQuery query("select 1 + 'abc'", EvaluatorOptions());

  EXPECT_THAT(query.Prepare(AnalyzerOptions()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("No matching signature")));
}

TEST(PreparedQueryDeathTest, IteratorStillLiveOnDestruction) {
  SimpleTable test_table("TestTable", {{"a", types::Int64Type()}});
  test_table.SetContents({{Int64(10)}, {Int64(20)}, {Int64(30)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);

  auto query = std::make_unique<PreparedQuery>("select a from TestTable",
                                               EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(query->Prepare(AnalyzerOptions(), &catalog));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query->Execute());
  EXPECT_DEATH(query.reset(), "cannot outlive the PreparedQuery object");
}

TEST(PreparedQuery, InvalidStatementKind) {
  LanguageOptions language_options;
  language_options.SetSupportedStatementKinds({RESOLVED_DELETE_STMT});
  AnalyzerOptions analyzer_options(language_options);

  SimpleTable table("TestTable");
  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(table.Name(), &table);

  PreparedQuery query("delete from TestTable where true", EvaluatorOptions());
  EXPECT_THAT(query.Prepare(analyzer_options, &catalog),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not correspond to a query")));
}

TEST(PreparedQuery, Parameter) {
  PreparedQuery query("select @p1, @p2", EvaluatorOptions());

  AnalyzerOptions analyzer_options;
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p1", types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p2", types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p3", types::DoubleType()));

  GOOGLESQL_EXPECT_OK(query.Prepare(analyzer_options));

  EXPECT_THAT(query.GetReferencedParameters(),
              IsOkAndHolds(UnorderedElementsAre("p1", "p2")));
  EXPECT_THAT(query.GetPositionalParameterCount(), IsOkAndHolds(0));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute({{"p2", values::String("abc")},
                                      {"p1", values::Int64(123)}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(123), iter->GetValue(0));
  EXPECT_EQ(String("abc"), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, query.Execute({{"p2", values::NullString()},
                                            {"p1", values::Int64(111)},
                                            {"p3", values::NullDouble()},
                                            {"p4", values::NullBytes()}}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(111), iter->GetValue(0));
  EXPECT_EQ(NullString(), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(query.Execute({{"p2", values::NullString()}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Incomplete query parameters p1")));
}

TEST(PreparedQuery, PositionalParameter) {
  PreparedQuery query("select ?, ?", EvaluatorOptions());

  AnalyzerOptions analyzer_options;
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::DoubleType()));

  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options));

  EXPECT_THAT(query.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(query.GetPositionalParameterCount(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableIterator> iter,
      query.ExecuteWithPositionalParams({Int64(123), String("abc")}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(123), iter->GetValue(0));
  EXPECT_EQ(String("abc"), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, query.ExecuteWithPositionalParams(
                {Int64(111), NullString(), NullDouble(), NullBytes()}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(111), iter->GetValue(0));
  EXPECT_EQ(NullString(), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(query.ExecuteWithPositionalParams({Int64(100)}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Too few positional parameters. Expected at "
                                 "least 2 but found only: 1")));
}

TEST(PreparedQuery, ExecuteAfterPrepareWithOrderedParamsWithParameter) {
  PreparedQuery query("select @p1, @p2", EvaluatorOptions());

  AnalyzerOptions analyzer_options;
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p1", types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p2", types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p3", types::DoubleType()));

  GOOGLESQL_EXPECT_OK(query.Prepare(analyzer_options));

  EXPECT_THAT(query.GetReferencedParameters(),
              IsOkAndHolds(ElementsAre("p1", "p2")));
  EXPECT_THAT(query.GetPositionalParameterCount(), IsOkAndHolds(0));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare(
                           {values::Int64(123), values::String("abc")}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(123), iter->GetValue(0));
  EXPECT_EQ(String("abc"), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, query.ExecuteAfterPrepare(
                                 {values::Int64(111), values::NullString()}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(111), iter->GetValue(0));
  EXPECT_EQ(NullString(), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(
      query.ExecuteAfterPrepare({values::NullString()}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Incorrect number of named parameters")));
}

TEST(PreparedQuery,
     ExecuteAfterPreparedWithOrderedParamsWithPositionalParameter) {
  PreparedQuery query("select ?, ?", EvaluatorOptions());

  AnalyzerOptions analyzer_options;
  analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::Int64Type()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::StringType()));
  GOOGLESQL_EXPECT_OK(analyzer_options.AddPositionalQueryParameter(types::DoubleType()));

  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options));

  EXPECT_THAT(query.GetReferencedParameters(), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(query.GetPositionalParameterCount(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<EvaluatorTableIterator> iter,
      query.ExecuteAfterPrepare({Int64(123), String("abc")}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(123), iter->GetValue(0));
  EXPECT_EQ(String("abc"), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, query.ExecuteAfterPrepare(
                {Int64(111), NullString(), NullDouble(), NullBytes()}));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(111), iter->GetValue(0));
  EXPECT_EQ(NullString(), iter->GetValue(1));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  iter.reset();
  EXPECT_THAT(query.ExecuteAfterPrepare({Int64(100)}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Incorrect number of positional parameters")));
}

TEST(PreparedQuery, ExplainAfterPrepareWithoutPrepare) {
  PreparedQuery query("select a, b from TestTable", EvaluatorOptions());
  EXPECT_THAT(query.ExplainAfterPrepare(),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Prepare must be called first")));
}

TEST(PreparedQuery, ExecuteAfterPrepareOnlyNamedParams) {
  PreparedQuery query("select @p1", EvaluatorOptions());

  AnalyzerOptions analyzer_options;
  GOOGLESQL_EXPECT_OK(analyzer_options.AddQueryParameter("p1", types::Int64Type()));

  GOOGLESQL_EXPECT_OK(query.Prepare(analyzer_options));

  EXPECT_THAT(query.GetReferencedParameters(),
              IsOkAndHolds(UnorderedElementsAre("p1")));
  EXPECT_THAT(query.GetPositionalParameterCount(), IsOkAndHolds(0));

  QueryOptions options;
  options.parameters = {{"p1", values::Int64(123)}};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare(options));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(Int64(123), iter->GetValue(0));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(PreparedQuery, ExecuteAfterPrepareBuiltinFunction) {
  PreparedQuery query("select 1 + 2 as x", EvaluatorOptions());

  AnalyzerOptions analyzer_options;
  GOOGLESQL_EXPECT_OK(query.Prepare(analyzer_options));
  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(query.resolved_query_stmt()->Accept(&sql_builder));
  ASSERT_EQ(sql_builder.sql(), "SELECT 1 + 2 AS x");
}

}  // namespace

class PreparedQueryTest : public ::testing::Test {
 protected:
  void SetupContextCallback(PreparedQuery* query) {
    query->SetCreateEvaluationCallbackTestOnly([this](EvaluationContext* cb) {
      // There should only be one EvaluationContext in each of these tests.
      ABSL_CHECK(context_ == nullptr);
      context_ = cb;
    });
  }

  EvaluationContext* context_ = nullptr;
};

namespace {

TEST_F(PreparedQueryTest, TopNAccumulator) {
  SimpleTable test_table("TestTable", {{"a", types::Int64Type()}});
  test_table.SetContents({{Int64(30)}, {Int64(20)}, {Int64(10)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  PreparedQuery query(
      "select array_agg(a order by a limit 2) agg from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);

  AnalyzerOptions analyzer_options;
  LanguageOptions* language_options = analyzer_options.mutable_language();
  language_options->EnableLanguageFeature(FEATURE_ORDER_BY_IN_AGGREGATE);
  language_options->EnableLanguageFeature(FEATURE_LIMIT_IN_AGGREGATE);

  GOOGLESQL_ASSERT_OK(query.Prepare(analyzer_options, &catalog));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());

  ASSERT_EQ(iter->NumColumns(), 1);
  ASSERT_EQ(iter->GetColumnName(0), "agg");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), values::Int64Array({10, 20}));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_TRUE(context_->used_top_n_accumulator());
}

TEST_F(PreparedQueryTest, ReadZeroColumnsWithPruningUnusedColumnsEnabled) {
  SimpleTable test_table("TestTable", {{"a", types::Int64Type()}});
  test_table.SetContents({{Int64(30)}, {Int64(20)}, {Int64(10)}});

  SimpleCatalog catalog("TestCatalog");
  catalog.AddTable(test_table.Name(), &test_table);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  PreparedQuery query("select 1 from TestTable", EvaluatorOptions());

  AnalyzerOptions options;
  options.set_prune_unused_columns(true);
  GOOGLESQL_ASSERT_OK(query.Prepare(options, &catalog));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  int64_t num_rows = 0;
  while (iter->NextRow()) {
    ++num_rows;
  }
  GOOGLESQL_ASSERT_OK(iter->Status());

  EXPECT_THAT(num_rows, Eq(3));
}

// Test fixture for end-to-end tests of reading all fields from a proto in one
// shot.
class PreparedQueryProtoTest : public PreparedQueryTest {
 protected:
  void SetUp() override {
    GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
        googlesql_test::KitchenSinkPB::descriptor(), &proto_type_));

    table_ =
        absl::WrapUnique(new SimpleTable("TestTable", {{"col", proto_type_}}));
    table_->SetContents({{GetProtoValue(1)}});

    GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType({{"kitchen_sink", proto_type_}},
                                           &struct_with_just_proto_type_));
    GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
        {{"kitchen_sink", proto_type_}, {"s", struct_with_just_proto_type_}},
        &struct_with_proto_type_));

    table2_ = absl::WrapUnique(new SimpleTable(
        "TestTable2",
        {{"kitchen_sink", proto_type_}, {"s", struct_with_proto_type_}}));
    table2_->SetContents(
        {{GetProtoValue(1),
          values::Struct(
              struct_with_proto_type_,
              {GetProtoValue(10), values::Struct(struct_with_just_proto_type_,
                                                 {GetProtoValue(100)})})}});

    catalog_ = std::make_unique<SimpleCatalog>("TestCatalog");
    catalog_->AddTable(table_->Name(), table_.get());
    catalog_->AddTable(table2_->Name(), table2_.get());
    catalog_->AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());

    catalog_->AddType("googlesql_test.KitchenSinkPB", proto_type_);

    GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
        googlesql_test::TestOptionalFields::descriptor(), &optional_type_));
    catalog_->AddType("googlesql_test.TestOptionalFields", optional_type_);
  }

  Value GetProtoValue(int key) const {
    googlesql_test::KitchenSinkPB proto;
    proto.set_int64_key_1(key);
    proto.set_int64_key_2(key + 1);

    PopulateNestedProto(key, proto.mutable_nested_value());

    googlesql_test::RewrappedNullableInt* rewrapped_nullable_int =
        proto.mutable_rewrapped_nullable_int();
    rewrapped_nullable_int->mutable_value()->set_value(key * 1000);

    proto.add_nested_repeated_value()->set_nested_int64(2 * key * 10);
    proto.add_nested_repeated_value()->set_nested_int64(3 * key * 10);

    googlesql_test::KitchenSinkPB_OptionalGroup* group =
        proto.mutable_optionalgroup();
    group->set_int64_val(500);
    group->add_optionalgroupnested()->set_int64_val(key * 1000 + 1);
    group->add_optionalgroupnested()->set_int64_val(key * 1000 + 2);

    absl::Cord bytes;
    ABSL_CHECK(proto.SerializeToCord(&bytes));
    return Value::Proto(proto_type_, bytes);
  }

  static void PopulateNestedProto(
      int key, googlesql_test::KitchenSinkPB_Nested* nested) {
    nested->set_nested_int64(key * 10);
    nested->add_nested_repeated_int64(key * 100);
    nested->add_nested_repeated_int64(key * 100 + 1);
  }

  int GetNumProtoDeserializations() const {
    return context_->num_proto_deserializations();
  }

  TypeFactory type_factory_;
  std::unique_ptr<SimpleTable> table_;
  std::unique_ptr<SimpleTable> table2_;
  std::unique_ptr<SimpleCatalog> catalog_;
  const ProtoType* proto_type_ = nullptr;
  const StructType* struct_with_just_proto_type_;
  const StructType* struct_with_proto_type_;
  const ProtoType* optional_type_ = nullptr;
};

TEST_F(PreparedQueryProtoTest, SelectProtoFromTable) {
  PreparedQuery query("select col from TestTable", EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 1);
  ASSERT_EQ(iter->GetColumnName(0), "col");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), GetProtoValue(/*key=*/1));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 0);
}

TEST_F(PreparedQueryProtoTest, SelectFieldFromProto) {
  PreparedQuery query("select col.int64_key_1 from TestTable",
                      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 1);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SelectTwoFieldsFromProto) {
  PreparedQuery query("select col.int64_key_1, col.int64_key_2 from TestTable",
                      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_2");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));
  EXPECT_EQ(iter->GetValue(1), Int64(2));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SelectTwoFieldsFromProtoInTwoRows) {
  auto new_table =
      absl::WrapUnique(new SimpleTable("NewTestTable", {{"col", proto_type_}}));
  new_table->SetContents({{GetProtoValue(1)}, {GetProtoValue(3)}});
  catalog_->AddTable(new_table->Name(), new_table.get());

  PreparedQuery query(
      "select col.int64_key_1, col.int64_key_2 from NewTestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_2");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));
  EXPECT_EQ(iter->GetValue(1), Int64(2));

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(3));
  EXPECT_EQ(iter->GetValue(1), Int64(4));

  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // One deserialization per row.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, SelectSameFieldTwice) {
  PreparedQuery query("select col.int64_key_1, col.int64_key_1 from TestTable",
                      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_1");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));
  EXPECT_EQ(iter->GetValue(1), Int64(1));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SameFieldWithNoHasBitAndHasBit) {
  PreparedQuery query(
      "select col.int64_key_1, col.has_int64_key_1 from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "has_int64_key_1");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));
  EXPECT_EQ(iter->GetValue(1), Bool(true));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SameFieldWithHasBitAndNoHasBit) {
  PreparedQuery query(
      "select col.has_int64_key_1, col.int64_key_1 from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "has_int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_1");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Bool(true));
  EXPECT_EQ(iter->GetValue(1), Int64(1));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SameProtoFieldWithNoHasBitAndHasBit) {
  PreparedQuery query(
      "select col.nested_value, col.has_nested_value from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "nested_value");
  ASSERT_EQ(iter->GetColumnName(1), "has_nested_value");

  ASSERT_TRUE(iter->NextRow());

  googlesql_test::KitchenSinkPB_Nested nested_value;
  nested_value.set_nested_int64(10);
  nested_value.add_nested_repeated_int64(100);
  nested_value.add_nested_repeated_int64(101);
  absl::Cord bytes_4970;
  ABSL_CHECK(nested_value.SerializeToCord(&bytes_4970));
  absl::Cord bytes = bytes_4970;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  EXPECT_EQ(iter->GetValue(0), Value::Proto(nested_type, bytes));
  EXPECT_EQ(iter->GetValue(1), Bool(true));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SameProtoFieldWithHasBitAndNoHasBit) {
  PreparedQuery query(
      "select col.has_nested_value, col.nested_value from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "has_nested_value");
  ASSERT_EQ(iter->GetColumnName(1), "nested_value");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Bool(true));

  googlesql_test::KitchenSinkPB_Nested nested_value;
  nested_value.set_nested_int64(10);
  nested_value.add_nested_repeated_int64(100);
  nested_value.add_nested_repeated_int64(101);
  absl::Cord bytes_5006;
  ABSL_CHECK(nested_value.SerializeToCord(&bytes_5006));
  absl::Cord bytes = bytes_5006;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  EXPECT_EQ(iter->GetValue(1), Value::Proto(nested_type, bytes));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SameFieldPathTwice) {
  PreparedQuery query(
      "select col.nested_value.nested_int64, "
      "       col.nested_value.nested_int64 "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(1), "nested_int64");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(10));
  EXPECT_EQ(iter->GetValue(1), Int64(10));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Two deserializations: one for the column and one for the nested value.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, TwoFieldPaths) {
  PreparedQuery query(
      "select col.nested_value.nested_int64, "
      "       col.nested_value.nested_repeated_int64 "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(1), "nested_repeated_int64");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(10));
  EXPECT_EQ(iter->GetValue(1), values::Int64Array({100, 101}));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Two deserializations: one for the column and one for the nested value.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, ArrayPath) {
  PreparedQuery query(
      "SELECT value FROM TestTable t, "
      "UNNEST(t.col.nested_repeated_value.nested_int64) value",
      EvaluatorOptions());
  SetupContextCallback(&query);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(language_options), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 1);
  ASSERT_EQ(iter->GetColumnName(0), "value");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(20));
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(30));
  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Three deserializations: one for the column and one for each nested proto.
  EXPECT_EQ(GetNumProtoDeserializations(), 3);
}

TEST_F(PreparedQueryProtoTest, FieldAndSubfield) {
  PreparedQuery query(
      "select col.nested_value, col.nested_value.nested_int64 from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "nested_value");
  ASSERT_EQ(iter->GetColumnName(1), "nested_int64");

  ASSERT_TRUE(iter->NextRow());

  googlesql_test::KitchenSinkPB_Nested nested_value;
  PopulateNestedProto(/*key=*/1, &nested_value);

  absl::Cord bytes_5117;
  ABSL_CHECK(nested_value.SerializeToCord(&bytes_5117));
  absl::Cord bytes = bytes_5117;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  EXPECT_EQ(iter->GetValue(0), Value::Proto(nested_type, bytes));
  EXPECT_EQ(iter->GetValue(1), Int64(10));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Two deserializations: one for the column and one for the nested value.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, SubfieldAndField) {
  PreparedQuery query(
      "select col.nested_value.nested_int64, col.nested_value from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(1), "nested_value");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(10));

  googlesql_test::KitchenSinkPB_Nested nested_value;
  PopulateNestedProto(/*key=*/1, &nested_value);
  absl::Cord bytes_5151;
  ABSL_CHECK(nested_value.SerializeToCord(&bytes_5151));
  absl::Cord bytes = bytes_5151;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  EXPECT_EQ(iter->GetValue(1), Value::Proto(nested_type, bytes));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Two deserializations: one for the column and one for the nested value.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, FieldAndSubSubField) {
  PreparedQuery query(
      "select col.rewrapped_nullable_int, "
      "       col.rewrapped_nullable_int.value.value "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "rewrapped_nullable_int");
  ASSERT_EQ(iter->GetColumnName(1), "value");

  ASSERT_TRUE(iter->NextRow());

  googlesql_test::RewrappedNullableInt rewrapped_nullable_int;
  rewrapped_nullable_int.mutable_value()->set_value(1000);
  absl::Cord bytes_5185;
  ABSL_CHECK(rewrapped_nullable_int.SerializeToCord(&bytes_5185));
  absl::Cord bytes = bytes_5185;

  const ProtoType* rewrapped_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::RewrappedNullableInt::descriptor(), &rewrapped_type));

  EXPECT_EQ(iter->GetValue(0), Value::Proto(rewrapped_type, bytes));
  EXPECT_EQ(iter->GetValue(1), Int32(1000));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Three deserializations: one for the column, one for rewrapped_nullable_int,
  // and one for rewrapped_nullable_int.value.
  EXPECT_EQ(GetNumProtoDeserializations(), 3);
}

TEST_F(PreparedQueryProtoTest, SubSubFieldAndField) {
  PreparedQuery query(
      "select col.rewrapped_nullable_int.value.value, "
      "       col.rewrapped_nullable_int "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "value");
  ASSERT_EQ(iter->GetColumnName(1), "rewrapped_nullable_int");

  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int32(1000));

  googlesql_test::RewrappedNullableInt rewrapped_nullable_int;
  rewrapped_nullable_int.mutable_value()->set_value(1000);
  absl::Cord bytes_5222;
  ABSL_CHECK(rewrapped_nullable_int.SerializeToCord(&bytes_5222));
  absl::Cord bytes = bytes_5222;

  const ProtoType* rewrapped_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::RewrappedNullableInt::descriptor(), &rewrapped_type));

  EXPECT_EQ(iter->GetValue(1), Value::Proto(rewrapped_type, bytes));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Three deserializations: one for the column, one for rewrapped_nullable_int,
  // and one for rewrapped_nullable_int.value.
  EXPECT_EQ(GetNumProtoDeserializations(), 3);
}

TEST_F(PreparedQueryProtoTest, Complex) {
  PreparedQuery query(
      "select col.nested_value, "
      "       col.nested_value.nested_int64, "
      "       col.nested_value.nested_repeated_int64, "
      "       col.int64_key_1 "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 4);
  ASSERT_EQ(iter->GetColumnName(0), "nested_value");
  ASSERT_EQ(iter->GetColumnName(1), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(2), "nested_repeated_int64");
  ASSERT_EQ(iter->GetColumnName(3), "int64_key_1");

  ASSERT_TRUE(iter->NextRow());

  googlesql_test::KitchenSinkPB_Nested nested_value;
  nested_value.set_nested_int64(10);
  nested_value.add_nested_repeated_int64(100);
  nested_value.add_nested_repeated_int64(101);
  absl::Cord bytes_5263;
  ABSL_CHECK(nested_value.SerializeToCord(&bytes_5263));
  absl::Cord bytes = bytes_5263;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  EXPECT_EQ(iter->GetValue(0), Value::Proto(nested_type, bytes));
  EXPECT_EQ(iter->GetValue(1), Int64(10));
  EXPECT_EQ(iter->GetValue(2), values::Int64Array({100, 101}));
  EXPECT_EQ(iter->GetValue(3), Int64(1));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // Two deserializations: one for the column and one for the nested value.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, WithRepeatedFieldOffsets) {
  PreparedQuery query(
      "select col.nested_repeated_value[offset(0)] as v1, "
      "       col.nested_repeated_value[offset(0)] as v2 "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "v1");
  ASSERT_EQ(iter->GetColumnName(1), "v2");

  ASSERT_TRUE(iter->NextRow());

  googlesql_test::KitchenSinkPB_Nested nested_proto;
  nested_proto.set_nested_int64(20);
  absl::Cord bytes_5300;
  ABSL_CHECK(nested_proto.SerializeToCord(&bytes_5300));
  absl::Cord bytes = bytes_5300;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  const Value nested_value = Value::Proto(nested_type, bytes);

  EXPECT_EQ(iter->GetValue(0), nested_value);
  EXPECT_EQ(iter->GetValue(1), nested_value);

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // One deserialization (for the column).
  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, WithRepeatedFieldOffsetsAndFieldAccesses) {
  PreparedQuery query(
      "select col.nested_repeated_value[offset(0)].nested_int64, "
      "       col.nested_repeated_value[offset(0)].nested_int64 "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(1), "nested_int64");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(20));
  EXPECT_EQ(iter->GetValue(1), Int64(20));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // The column is only deserialized once, but nested_repeated_value[offset(0)]
  // is deserialized twice.
  EXPECT_EQ(GetNumProtoDeserializations(), 3);
}

TEST_F(PreparedQueryProtoTest,
       MessageFieldWithRepeatedFieldOffsetsAndFieldAccesses) {
  PreparedQuery query(
      "select col.OptionalGroup.OptionalGroupNested[offset(0)].int64_val, "
      "       col.OptionalGroup.OptionalGroupNested[offset(0)].int64_val "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_val");
  ASSERT_EQ(iter->GetColumnName(1), "int64_val");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(1001));
  EXPECT_EQ(iter->GetValue(1), Int64(1001));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // The column and the optional group are only deserialized once, but the array
  // element is deserialized twice.
  EXPECT_EQ(GetNumProtoDeserializations(), 4);
}

TEST_F(PreparedQueryProtoTest, Subquery) {
  PreparedQuery query(
      "select a.int64_key_1, a.int64_key_2\n"
      "from (select (new googlesql_test.KitchenSinkPB(\n"
      "                  1 as int64_key_1, 2 as int64_key_2)) a)",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_2");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(1));
  EXPECT_EQ(iter->GetValue(1), Int64(2));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // The proto is only deserialized once.
  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, ProtoFromStruct) {
  PreparedQuery query("select s.kitchen_sink from TestTable2",
                      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 1);
  ASSERT_EQ(iter->GetColumnName(0), "kitchen_sink");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), GetProtoValue(10));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // The proto is extracted from a struct but not deserialized.
  EXPECT_EQ(GetNumProtoDeserializations(), 0);
}

TEST_F(PreparedQueryProtoTest, FieldFromProtoFromStruct) {
  PreparedQuery query("select s.kitchen_sink.int64_key_1 from TestTable2",
                      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 1);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(10));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, TwoFieldsFromProtoFromStruct) {
  PreparedQuery query(
      "select s.kitchen_sink.int64_key_1, s.kitchen_sink.int64_key_2 "
      "from TestTable2",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_2");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(10));
  EXPECT_EQ(iter->GetValue(1), Int64(11));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, SameFieldTwiceFromProtoFromStruct) {
  PreparedQuery query(
      "select s.kitchen_sink.int64_key_1, s.kitchen_sink.int64_key_1 "
      "from TestTable2",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_1");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(10));
  EXPECT_EQ(iter->GetValue(1), Int64(10));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest,
       SameFieldTwiceFromProtoFromStructOnceWithHasBit) {
  PreparedQuery query(
      "select s.kitchen_sink.int64_key_1, s.kitchen_sink.has_int64_key_1 "
      "from TestTable2",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "has_int64_key_1");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(10));
  EXPECT_EQ(iter->GetValue(1), Bool(true));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_EQ(GetNumProtoDeserializations(), 1);
}

TEST_F(PreparedQueryProtoTest, MixedProtoAndStructFieldPaths) {
  PreparedQuery query(
      "select"
      " kitchen_sink.int64_key_1, kitchen_sink.int64_key_2,"
      " kitchen_sink.nested_value, kitchen_sink.nested_value.nested_int64,"
      " kitchen_sink.nested_value.nested_repeated_int64,"
      " s.kitchen_sink.int64_key_1, s.kitchen_sink.int64_key_2,"
      " s.kitchen_sink.nested_value, s.kitchen_sink.nested_value.nested_int64,"
      " s.kitchen_sink.nested_value.nested_repeated_int64,"
      " s.s.kitchen_sink.int64_key_1, s.s.kitchen_sink.int64_key_2,"
      " s.s.kitchen_sink.nested_value,"
      " s.s.kitchen_sink.nested_value.nested_int64,"
      " s.s.kitchen_sink.nested_value.nested_repeated_int64 "
      "from TestTable2",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 15);
  ASSERT_EQ(iter->GetColumnName(0), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(1), "int64_key_2");
  ASSERT_EQ(iter->GetColumnName(2), "nested_value");
  ASSERT_EQ(iter->GetColumnName(3), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(4), "nested_repeated_int64");
  ASSERT_EQ(iter->GetColumnName(5), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(6), "int64_key_2");
  ASSERT_EQ(iter->GetColumnName(7), "nested_value");
  ASSERT_EQ(iter->GetColumnName(8), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(9), "nested_repeated_int64");
  ASSERT_EQ(iter->GetColumnName(10), "int64_key_1");
  ASSERT_EQ(iter->GetColumnName(11), "int64_key_2");
  ASSERT_EQ(iter->GetColumnName(12), "nested_value");
  ASSERT_EQ(iter->GetColumnName(13), "nested_int64");
  ASSERT_EQ(iter->GetColumnName(14), "nested_repeated_int64");

  ASSERT_TRUE(iter->NextRow());

  googlesql_test::KitchenSinkPB_Nested expected_nested1;
  googlesql_test::KitchenSinkPB_Nested expected_nested2;
  googlesql_test::KitchenSinkPB_Nested expected_nested3;

  PopulateNestedProto(/*key=*/1, &expected_nested1);
  PopulateNestedProto(/*key=*/10, &expected_nested2);
  PopulateNestedProto(/*key=*/100, &expected_nested3);

  absl::Cord bytes_5568;
  ABSL_CHECK(expected_nested1.SerializeToCord(&bytes_5568));
  absl::Cord serialized_expected_nested1 = bytes_5568;
  absl::Cord bytes_5569;
  ABSL_CHECK(expected_nested2.SerializeToCord(&bytes_5569));
  absl::Cord serialized_expected_nested2 = bytes_5569;
  absl::Cord bytes_5570;
  ABSL_CHECK(expected_nested3.SerializeToCord(&bytes_5570));
  absl::Cord serialized_expected_nested3 = bytes_5570;

  const ProtoType* nested_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      googlesql_test::KitchenSinkPB_Nested::descriptor(), &nested_type));

  const Value expected_nested1_value =
      Value::Proto(nested_type, serialized_expected_nested1);
  const Value expected_nested2_value =
      Value::Proto(nested_type, serialized_expected_nested2);
  const Value expected_nested3_value =
      Value::Proto(nested_type, serialized_expected_nested3);

  EXPECT_EQ(iter->GetValue(0), Int64(1));
  EXPECT_EQ(iter->GetValue(1), Int64(2));
  EXPECT_EQ(iter->GetValue(2), expected_nested1_value);
  EXPECT_EQ(iter->GetValue(3), Int64(10));
  EXPECT_EQ(iter->GetValue(4), values::Int64Array({100, 101}));
  EXPECT_EQ(iter->GetValue(5), Int64(10));
  EXPECT_EQ(iter->GetValue(6), Int64(11));
  EXPECT_EQ(iter->GetValue(7), expected_nested2_value);
  EXPECT_EQ(iter->GetValue(8), Int64(100));
  EXPECT_EQ(iter->GetValue(9), values::Int64Array({1000, 1001}));
  EXPECT_EQ(iter->GetValue(10), Int64(100));
  EXPECT_EQ(iter->GetValue(11), Int64(101));
  EXPECT_EQ(iter->GetValue(12), expected_nested3_value);
  EXPECT_EQ(iter->GetValue(13), Int64(1000));
  EXPECT_EQ(iter->GetValue(14), values::Int64Array({10000, 10001}));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // We deserialize the following protos:
  // - kitchen_sink
  // - kitchen_sink.nested_value
  // - s.kitchen_sink
  // - s.kitchen_sink.nested_value
  // - s.s.kitchen_sink
  // - s.s.kitchen_sink.nested_value
  EXPECT_EQ(GetNumProtoDeserializations(), 6);
}

TEST_F(PreparedQueryProtoTest, ArbitraryProtoValuedExpression) {
  PreparedQuery query(
      "select new googlesql_test.TestOptionalFields().value, "
      "       new googlesql_test.TestOptionalFields().value "
      "from TestTable",
      EvaluatorOptions());
  SetupContextCallback(&query);
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(), catalog_.get()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.ExecuteAfterPrepare());
  ASSERT_EQ(iter->NumColumns(), 2);
  ASSERT_EQ(iter->GetColumnName(0), "value");
  ASSERT_EQ(iter->GetColumnName(1), "value");

  ASSERT_TRUE(iter->NextRow());

  EXPECT_EQ(iter->GetValue(0), Int64(0));
  EXPECT_EQ(iter->GetValue(1), Int64(0));

  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  // We have to deserialize the proto-valued expression twice because it is not
  // of the form column.field_path.
  EXPECT_EQ(GetNumProtoDeserializations(), 2);
}

TEST_F(PreparedQueryProtoTest, HavingMinMaxRepro) {
  // Important: keep the ORDER BY at the end of the CTE, to ensure the test
  // hits the desired order of operations, hitting the error of different array
  // lengths first before resetting to a better extremal value.
  PreparedQuery query(
      R"(
WITH tbl AS (
  SELECT 1 AS k, [] AS arr UNION ALL
  SELECT 1 AS k, [1] AS arr UNION ALL
  SELECT 2 AS k, [1,2] AS arr UNION ALL
  SELECT 2 AS k, [1,2] AS arr
  ORDER BY k
)
SELECT
    SAFE.ELEMENTWISE_SUM(arr HAVING MAX k) AS a
FROM tbl
)",
      EvaluatorOptions());

  SetupContextCallback(&query);
  auto sample_catalog = std::make_unique<SampleCatalog>();
  GOOGLESQL_ASSERT_OK(query.Prepare(AnalyzerOptions(LanguageOptions::MaximumFeatures()),
                          sample_catalog->catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       query.Execute());

  EXPECT_EQ(1, iter->NumColumns());
  EXPECT_EQ("a", iter->GetColumnName(0));
  EXPECT_TRUE(iter->GetColumnType(0)->IsArray());

  ASSERT_TRUE(iter->NextRow());
  const Value& value = iter->GetValue(0);

  // Make sure the value is *NOT* NULL. The NULL (which comes from SAFE) should
  // have been cleared once we saw the new maximal value.
  ASSERT_FALSE(value.is_null());

  EXPECT_EQ(2, value.num_elements());

  EXPECT_EQ(value.element(0), Int64(2));
  EXPECT_EQ(value.element(1), Int64(4));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());
}

TEST(EvaluatorTest, PreparedQueryExecuteFailedPrepare) {
  const std::string sql = "CREATE TABLE T AS SELECT 1 AS col;";
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->SetSupportsAllStatementKinds();
  SimpleCatalog catalog("test");
  PreparedQuery prepared_query(sql, EvaluatorOptions());
  EXPECT_THAT(
      prepared_query.Prepare(analyzer_options, &catalog),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Statement kind RESOLVED_CREATE_TABLE_AS_SELECT_STMT "
                         "does not correspond to a query")));
  EXPECT_THAT(prepared_query.Execute(QueryOptions()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

TEST(EvaluatorTest, PreparedModifyExecuteFailedPrepare) {
  const std::string sql = "SELECT 1;";
  PreparedModify prepared_modify(sql, EvaluatorOptions());
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->SetSupportsAllStatementKinds();
  SimpleCatalog catalog("test");
  EXPECT_THAT(prepared_modify.Prepare(analyzer_options, &catalog),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Statement kind RESOLVED_QUERY_STMT does not "
                                 "correspond to a DML statement")));
  EXPECT_THAT(prepared_modify.Execute(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

class PreparedStatementTest : public ::testing::Test {
 public:
  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();

    // Enable various pipe features to support testing multi-statements.
    analyzer_options_.mutable_language()->EnableLanguageFeature(FEATURE_PIPES);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_PIPE_TEE);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_PIPE_FORK);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_PIPE_CREATE_TABLE);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_PIPE_INSERT);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_CREATE_TABLE_AS_SELECT_COLUMN_LIST);
  }

  void AddTableT() {
    auto t_table = std::make_unique<SimpleTable>(
        "t",
        std::vector<SimpleTable::NameAndType>{{"col", types::Int64Type()}});
    // We need to explicitly set the contents to empty so that
    // `SimpleTable::evaluator_table_iterator_factory_` is populated.
    t_table->SetContents({});
    GOOGLESQL_ASSERT_OK(t_table->SetPrimaryKey({0}));
    catalog_.AddOwnedTable(std::move(t_table));
  }

  Catalog* catalog() { return &catalog_; }
  const AnalyzerOptions& analyzer_options() const { return analyzer_options_; }

 protected:
  SimpleCatalog catalog_{"test_catalog"};
  AnalyzerOptions analyzer_options_;
};

TEST_F(PreparedStatementTest, CoreFunctionality_SimpleCTE) {
  // Test a simple CTE followed by a SELECT that uses it.
  const std::string sql = R"sql(
     SELECT 1 AS x, 'a' AS y
     |> FORK (
       |> SELECT *
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  // The SELECT statement.
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[0]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(1));
    EXPECT_EQ(iter->GetValue(1), String("a"));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, CoreFunctionality_CTEWithSelectAndInsert) {
  // Test a CTE followed by a SELECT and an INSERT that both use the CTE.
  AddTableT();
  const std::string sql = R"sql(
     SELECT 42 AS col
     |> FORK (
       |> SELECT *
     ), (
       |> INSERT INTO t(col)
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: SELECT
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[0]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(42));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }

  // Statement 2: INSERT
  GOOGLESQL_ASSERT_OK(results[1]);
  {
    EXPECT_EQ(results[1]->kind, PreparedStatementBase::StmtKind::kDML);
    EXPECT_THAT(
        results[1]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_INSERT_STMT)));
    auto& iter = results[1]->modify_result.table_modify_iter;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kInsert);
    EXPECT_EQ(iter->GetColumnValue(0), Int64(42));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, CoreFunctionality_CTEWithNamedParameter) {
  // Test a CTE that uses a named parameter, followed by a SELECT.
  const std::string sql = R"sql(
     SELECT @p1 AS x
     |> FORK (
       |> SELECT *
     )
   )sql";

  AnalyzerOptions options = analyzer_options();
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("p1", types::Int64Type()));

  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(options, catalog()));

  QueryOptions query_options;
  query_options.parameters = {{"p1", Int64(42)}};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute(query_options));
  ASSERT_EQ(results.size(), 1);

  // The SELECT statement.
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[0]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(42));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, CoreFunctionality_CTEWithPositionalParameter) {
  // Test a CTE that uses a positional parameter, followed by a SELECT.
  const std::string sql = R"sql(
     SELECT ? AS x
     |> FORK (
       |> SELECT *
     )
   )sql";

  AnalyzerOptions options = analyzer_options();
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));

  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(options, catalog()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results,
                       prepared_stmt.ExecuteWithPositionalParams({Int64(123)}));
  ASSERT_EQ(results.size(), 1);

  // The SELECT statement.
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[0]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(123));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, CoreFunctionality_DependentCTE) {
  // Test two CTEs where the second CTE depends on the first.
  const std::string sql = R"sql(
     SELECT 1 AS x
     |> FORK (
       |> SELECT x + 1 AS y
       |> FORK (
         |> SELECT *
       )
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  // The SELECT statement.
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[0]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(2));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, ErrorHandling_FailingCTECausesCascadingFailure) {
  // Test a CTE that fails at runtime, causing the subsequent SELECT to fail as
  // well.
  const std::string sql = R"sql(
     SELECT 1/0 AS x
     |> FORK (
       |> SELECT *
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  // The SELECT statement fails because its prerequisite CTE fails.
  EXPECT_THAT(
      results[0],
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Prerequisite CreateWithEntryStmt failed, "
                         "statement index: 0 error: division by zero")));
}

TEST_F(PreparedStatementTest, ErrorHandling_SuccessfulCTEFailOnSelect) {
  // Test a successful CTE followed by a SELECT that fails at runtime.
  const std::string sql = R"sql(
     SELECT 1 AS x
     |> FORK (
       |> SELECT 1/0
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  // The SELECT statement fails.
  EXPECT_THAT(results[0], StatusIs(absl::StatusCode::kOutOfRange,
                                   HasSubstr("division by zero")));
}

TEST_F(PreparedStatementTest, ErrorHandling_FailOnDML) {
  // Test a successful CTE and SELECT, followed by a DML statement that fails
  // at runtime.
  AddTableT();
  const std::string sql = R"sql(
     SELECT 1 AS x
     |> FORK (
       |> CREATE TABLE t1
     ), (
       -- Table t has primary key on col, so this will fail.
       |> UNION ALL (SELECT 1)
       |> INSERT INTO t(col)
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: CREATE TABLE
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kCTAS);
    EXPECT_THAT(results[0]->statement,
                Pointee(Property(&ResolvedStatement::node_kind,
                                 RESOLVED_CREATE_TABLE_AS_SELECT_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(1));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }

  // Statement 2: INSERT (fails)
  EXPECT_THAT(
      results[1],
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Failed to insert row with primary key "
                         "({pk#col:1}) due to previously inserted row")));
}

TEST_F(PreparedStatementTest, ErrorHandling_CascadingFailureFromCTE) {
  // Test a failing CTE (using ERROR) followed by multiple statements that
  // should all be skipped.
  AddTableT();
  const std::string sql = R"sql(
     SELECT ERROR('cascading failure') AS x
     |> FORK (
       |> SELECT *
     ), (
       |> INSERT INTO t(col)
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: SELECT (fails)
  EXPECT_THAT(
      results[0],
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Prerequisite CreateWithEntryStmt failed, "
                         "statement index: 0 error: cascading failure")));

  // Statement 2: INSERT (fails)
  EXPECT_THAT(
      results[1],
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Prerequisite CreateWithEntryStmt failed, "
                         "statement index: 0 error: cascading failure")));
}

TEST_F(PreparedStatementTest, Stateful_DMLInvisibility) {
  // An INSERT's effects are not visible to a subsequent SELECT in the same
  // batch.
  AddTableT();
  const std::string sql = R"sql(
     FROM t
     |> FORK (
       |> INSERT INTO t(col)
     ), (
       |> AGGREGATE COUNT(*)
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: INSERT
  GOOGLESQL_ASSERT_OK(results[0]);
  EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kDML);
  EXPECT_THAT(
      results[0]->statement,
      Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_INSERT_STMT)));

  // Statement 2: SELECT COUNT(1)
  GOOGLESQL_ASSERT_OK(results[1]);
  {
    EXPECT_EQ(results[1]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[1]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[1]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    // The table 't' was initially empty. The INSERT is not visible.
    EXPECT_EQ(iter->GetValue(0), Int64(0));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, Stateful_DDLInvisibility) {
  // A CREATE OR REPLACE TABLE statement is accepted but the new table contents
  // are not visible within the batch.
  AddTableT();
  const std::string sql = R"sql(
     FROM t
     |> FORK (
       |> UNION ALL (SELECT 100000)
       |> CREATE OR REPLACE TABLE t(col INT64)
     ), (
       |> SELECT *
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: CREATE OR REPLACE TABLE
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kCTAS);
    EXPECT_THAT(results[0]->statement,
                Pointee(Property(&ResolvedStatement::node_kind,
                                 RESOLVED_CREATE_TABLE_AS_SELECT_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(100000));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }

  // Statement 2: SELECT *
  GOOGLESQL_ASSERT_OK(results[1]);
  {
    EXPECT_EQ(results[1]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[1]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[1]->table_iterator;
    ASSERT_NE(iter, nullptr);
    // The CREATE OR REPLACE is not visible to this SELECT.
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, Stateful_CTEVisibilityForMultipleStatements) {
  // A single CTE is visible to multiple subsequent statements of different
  // types.
  AddTableT();
  const std::string sql = R"sql(
     SELECT 1 AS x, 2 AS y
     |> FORK (
       |> SELECT x
     ), (
       |> SELECT x + y
       |> INSERT INTO t(col)
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: SELECT
  GOOGLESQL_ASSERT_OK(results[0]);
  {
    EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
    EXPECT_THAT(
        results[0]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
    auto& iter = results[0]->table_iterator;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetValue(0), Int64(1));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }

  // Statement 2: INSERT
  GOOGLESQL_ASSERT_OK(results[1]);
  {
    EXPECT_EQ(results[1]->kind, PreparedStatementBase::StmtKind::kDML);
    EXPECT_THAT(
        results[1]->statement,
        Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_INSERT_STMT)));
    auto& iter = results[1]->modify_result.table_modify_iter;
    ASSERT_NE(iter, nullptr);
    ASSERT_TRUE(iter->NextRow());
    EXPECT_EQ(iter->GetOperation(),
              EvaluatorTableModifyIterator::Operation::kInsert);
    EXPECT_EQ(iter->GetColumnValue(0), Int64(3));
    ASSERT_FALSE(iter->NextRow());
    GOOGLESQL_ASSERT_OK(iter->Status());
  }
}

TEST_F(PreparedStatementTest, Stateful_FailingCTEPreventsDDL) {
  // A failing CTE prevents subsequent DDL from executing.
  const std::string sql = R"sql(
     SELECT 1/0 AS x
     |> FORK (
       |> SELECT *
     ), (
       |> CREATE TABLE t1
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 2);

  // Statement 1: SELECT (fails)
  EXPECT_THAT(
      results[0],
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Prerequisite CreateWithEntryStmt failed, "
                         "statement index: 0 error: division by zero")));

  // Statement 2: CREATE TABLE (fails)
  EXPECT_THAT(
      results[1],
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Prerequisite CreateWithEntryStmt failed, "
                         "statement index: 0 error: division by zero")));
}

TEST_F(PreparedStatementTest, ExplainAfterPrepare) {
  PreparedStatement prepared_stmt(R"sql(
     SELECT 1 AS x
     |> FORK (
       |> SELECT x
     )
   )sql",
                                  EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  EXPECT_THAT(prepared_stmt.ExplainAfterPrepare(),
              IsOkAndHolds(Eq("MultiStmtExpr(\n"
                              "+-$fork_cte_1 := ArrayNestExpr(is_with_table=0\n"
                              "| +-element: NewStructExpr(\n"
                              "| | +-type: STRUCT<x INT64>,\n"
                              "| | +-0 x: $x),\n"
                              "| +-input: ComputeOp(\n"
                              "|   +-map: {\n"
                              "|   | +-$x := ConstExpr(1)},\n"
                              "|   +-input: EnumerateOp(ConstExpr(1)))),\n"
                              "+-ArrayNestExpr(is_with_table=0\n"
                              "| +-element: NewStructExpr(\n"
                              "| | +-type: STRUCT<x INT64>,\n"
                              "| | +-0 x: $x),\n"
                              "| +-input: ArrayScanOp(\n"
                              "|   +-$x := field[0]:x,\n"
                              "|   +-array: $fork_cte_1)))")));
}

TEST_F(PreparedStatementTest, StandaloneSelect) {
  const std::string sql = "SELECT 1;";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  GOOGLESQL_ASSERT_OK(results[0]);
  EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
  EXPECT_THAT(
      results[0]->statement,
      Pointee(Property(&ResolvedStatement::node_kind, RESOLVED_QUERY_STMT)));
  auto& iter = results[0]->table_iterator;
  ASSERT_NE(iter, nullptr);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));
  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_ASSERT_OK(iter->Status());
}

TEST_F(PreparedStatementTest, StandaloneSelectWithNamedParameter) {
  const std::string sql = "SELECT @p1;";
  AnalyzerOptions options = analyzer_options();
  GOOGLESQL_ASSERT_OK(options.AddQueryParameter("p1", types::Int64Type()));

  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(options, catalog()));

  QueryOptions query_options;
  query_options.parameters = {{"p1", Int64(42)}};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute(query_options));
  ASSERT_EQ(results.size(), 1);

  GOOGLESQL_ASSERT_OK(results[0]);
  EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
  auto& iter = results[0]->table_iterator;
  ASSERT_NE(iter, nullptr);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(42));
  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_ASSERT_OK(iter->Status());
}

TEST_F(PreparedStatementTest, StandaloneSelectWithPositionalParameter) {
  const std::string sql = "SELECT ?;";
  AnalyzerOptions options = analyzer_options();
  options.set_parameter_mode(PARAMETER_POSITIONAL);
  GOOGLESQL_ASSERT_OK(options.AddPositionalQueryParameter(types::Int64Type()));

  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(options, catalog()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results,
                       prepared_stmt.ExecuteWithPositionalParams({Int64(123)}));
  ASSERT_EQ(results.size(), 1);

  GOOGLESQL_ASSERT_OK(results[0]);
  EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kQuery);
  auto& iter = results[0]->table_iterator;
  ASSERT_NE(iter, nullptr);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(123));
  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_ASSERT_OK(iter->Status());
}

TEST_F(PreparedStatementTest, StandaloneInsert) {
  AddTableT();
  const std::string sql = "INSERT INTO t(col) VALUES (1);";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  GOOGLESQL_ASSERT_OK(results[0]);
  EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kDML);
  auto& iter = results[0]->modify_result.table_modify_iter;
  ASSERT_NE(iter, nullptr);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetOperation(),
            EvaluatorTableModifyIterator::Operation::kInsert);
  EXPECT_EQ(iter->GetColumnValue(0), Int64(1));
  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_ASSERT_OK(iter->Status());
}

TEST_F(PreparedStatementTest, StandaloneCreateTable) {
  const std::string sql =
      "CREATE TABLE new_table (col INT64) AS SELECT 1 AS col;";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  GOOGLESQL_ASSERT_OK(prepared_stmt.Prepare(analyzer_options(), catalog()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto results, prepared_stmt.Execute());
  ASSERT_EQ(results.size(), 1);

  GOOGLESQL_ASSERT_OK(results[0]);
  EXPECT_EQ(results[0]->kind, PreparedStatementBase::StmtKind::kCTAS);
  auto& iter = results[0]->table_iterator;
  ASSERT_NE(iter, nullptr);
  ASSERT_TRUE(iter->NextRow());
  EXPECT_EQ(iter->GetValue(0), Int64(1));
  ASSERT_FALSE(iter->NextRow());
  GOOGLESQL_ASSERT_OK(iter->Status());
}

TEST_F(PreparedStatementTest, ExecuteFailedPrepare_StandaloneQuery) {
  const std::string sql = "SELECT 1 FROM NonExistentTable;";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  EXPECT_THAT(prepared_stmt.Prepare(analyzer_options(), catalog()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Table not found: NonExistentTable")));
  EXPECT_THAT(prepared_stmt.Execute(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

TEST_F(PreparedStatementTest, ExecuteFailedPrepare_StandaloneDML) {
  AddTableT();
  const std::string sql = "INSERT INTO t(non_existent_col) VALUES (1);";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  EXPECT_THAT(
      prepared_stmt.Prepare(analyzer_options(), catalog()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Column non_existent_col is not present in table t")));
  EXPECT_THAT(prepared_stmt.Execute(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

TEST_F(PreparedStatementTest, ExecuteFailedPrepare_MultiStmt) {
  const std::string sql = R"sql(
     SELECT 1 AS x
     |> FORK (
       |> SELECT z
     )
   )sql";
  PreparedStatement prepared_stmt(sql, EvaluatorOptions());
  EXPECT_THAT(prepared_stmt.Prepare(analyzer_options(), catalog()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: z")));
  EXPECT_THAT(prepared_stmt.Execute(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

TEST_F(PreparedStatementTest,
       ConstructorWithResolvedGeneralizedQueryStmtFails) {
  const std::string sql = R"sql(
    SELECT 1 AS x
    |> FORK (
      |> SELECT x
    )
  )sql";

  AnalyzerOptions options = analyzer_options();
  options.disable_rewrite(REWRITE_GENERALIZED_QUERY_STMT);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(sql, options, catalog(), &type_factory,
                             &analyzer_output));

  const ResolvedStatement* resolved_statement =
      analyzer_output->resolved_statement();

  ASSERT_EQ(resolved_statement->node_kind(), RESOLVED_GENERALIZED_QUERY_STMT);

  PreparedStatement prepared_stmt(resolved_statement, EvaluatorOptions());
  EXPECT_THAT(prepared_stmt.Prepare(options, catalog()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Evaluator does not support statement kind: "
                                 "GeneralizedQueryStmt")));
  EXPECT_THAT(prepared_stmt.Execute(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid prepared expression/query")));
}

}  // namespace
}  // namespace googlesql
