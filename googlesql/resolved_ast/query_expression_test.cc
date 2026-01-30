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

#include "googlesql/resolved_ast/query_expression.h"

#include <string>

#include "gtest/gtest.h"

namespace googlesql {

using TargetSyntaxMode = QueryExpression::TargetSyntaxMode;

template <unsigned int N>
struct TestCase {
  TargetSyntaxMode from_clause_syntax_mode;
  TargetSyntaxMode subquery_target_syntax_mode;
  TargetSyntaxMode target_syntax_mode;
  std::string expected_sql;
  unsigned int dummy = N;
};

using QueryExpressionWrapTest1 = ::testing::TestWithParam<TestCase<1>>;

TEST_P(QueryExpressionWrapTest1, TestWrapImpl1) {
  const TestCase<1>& test_case = GetParam();
  QueryExpression query_expression(test_case.from_clause_syntax_mode);
  EXPECT_TRUE(query_expression.TrySetFromClause("(SELECT Key FROM KeyValue)"));
  EXPECT_TRUE(query_expression.TrySetSelectClause({{"*", ""}}, ""));
  query_expression.WrapImpl("alias", test_case.subquery_target_syntax_mode,
                            test_case.target_syntax_mode);
  EXPECT_TRUE(query_expression.TrySetSelectClause({{"1", ""}}, ""));
  EXPECT_EQ(query_expression.GetSQLQuery(test_case.target_syntax_mode),
            test_case.expected_sql);
}

INSTANTIATE_TEST_SUITE_P(
    QueryExpressionWrapTest1, QueryExpressionWrapTest1,
    testing::ValuesIn<TestCase<1>>({
        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM (SELECT * FROM (SELECT Key FROM KeyValue)) AS alias"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM (SELECT * FROM ((SELECT Key FROM KeyValue))) AS alias"},

        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kPipe,
         "SELECT * FROM (SELECT Key FROM KeyValue) |> AS alias |> SELECT 1"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kPipe,
         "SELECT * FROM ((SELECT Key FROM KeyValue)) |> AS alias |> SELECT 1"},

        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM ((SELECT Key FROM KeyValue) |> SELECT *) AS alias"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM ((SELECT Key FROM KeyValue) |> SELECT *) AS alias"},

        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kPipe,
         "(SELECT Key FROM KeyValue) |> SELECT * |> AS alias |> SELECT 1"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kPipe,
         "(SELECT Key FROM KeyValue) |> SELECT * |> AS alias |> SELECT 1"},
    }));

using QueryExpressionWrapTest2 = ::testing::TestWithParam<TestCase<2>>;

TEST_P(QueryExpressionWrapTest2, TestWrapImpl2) {
  const TestCase<2>& test_case = GetParam();
  QueryExpression query_expression(test_case.from_clause_syntax_mode);
  EXPECT_TRUE(query_expression.TrySetFromClause("KeyValue"));
  EXPECT_TRUE(query_expression.TrySetSelectClause({{"*", ""}}, ""));
  query_expression.WrapImpl("alias", test_case.subquery_target_syntax_mode,
                            test_case.target_syntax_mode);
  EXPECT_TRUE(query_expression.TrySetSelectClause({{"1", ""}}, ""));
  EXPECT_EQ(query_expression.GetSQLQuery(test_case.target_syntax_mode),
            test_case.expected_sql);
}

INSTANTIATE_TEST_SUITE_P(
    QueryExpressionWrapTest2, QueryExpressionWrapTest2,
    testing::ValuesIn<TestCase<2>>({
        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM (SELECT * FROM KeyValue) AS alias"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM (SELECT * FROM KeyValue) AS alias"},

        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kPipe,
         "SELECT * FROM KeyValue |> AS alias |> SELECT 1"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kStandard,
         TargetSyntaxMode::kPipe,
         "SELECT * FROM KeyValue |> AS alias |> SELECT 1"},

        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM (FROM KeyValue |> SELECT *) AS alias"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kStandard,
         "SELECT 1 FROM (FROM KeyValue |> SELECT *) AS alias"},

        {TargetSyntaxMode::kStandard, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kPipe,
         "FROM KeyValue |> SELECT * |> AS alias |> SELECT 1"},
        {TargetSyntaxMode::kPipe, TargetSyntaxMode::kPipe,
         TargetSyntaxMode::kPipe,
         "FROM KeyValue |> SELECT * |> AS alias |> SELECT 1"},
    }));

}  // namespace googlesql
