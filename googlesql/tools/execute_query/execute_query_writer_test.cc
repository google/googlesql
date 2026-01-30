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

#include "googlesql/tools/execute_query/execute_query_writer.h"

#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

TEST(ExecuteQueryStreamWriterTest, Log) {
  std::ostringstream output;
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.log("message1"));
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.log("message2\nmessage3"));
  EXPECT_EQ(output.str(), "message1\nmessage2\nmessage3\n");
}

TEST(ExecuteQueryStreamWriterTest, Parsed) {
  std::ostringstream output;
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.parsed("short parser string"));
  EXPECT_EQ(output.str(), "short parser string\n");
}

TEST(ExecuteQueryStreamWriterTest, ResolvedSelect) {
  SimpleCatalog catalog("simple_catalog");
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatement("SELECT 1", {}, &catalog, &type_factory,
                             &analyzer_output));
  std::ostringstream output;
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.resolved(
      *analyzer_output->resolved_statement(), /*post_rewrite=*/false));
  EXPECT_EQ(output.str(), R"(QueryStmt
+-output_column_list=
| +-$query.$col1#1 AS `$col1` [INT64]
+-query=
  +-ProjectScan
    +-column_list=[$query.$col1#1]
    +-expr_list=
    | +-$col1#1 := Literal(type=INT64, value=1)
    +-input_scan=
      +-SingleRowScan

)");
}

TEST(ExecuteQueryStreamWriterTest, Explain) {
  SimpleCatalog catalog("simple_catalog");
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatement("SELECT 1", {}, &catalog, &type_factory,
                             &analyzer_output));
  std::ostringstream output;
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.explained(
      *analyzer_output->resolved_statement(), "short explanation"));
  EXPECT_EQ(output.str(), "short explanation\n");
}

TEST(ExecuteQueryStreamWriterTest, Executed) {
  SimpleCatalog catalog("simple_catalog");
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatement("SELECT 1", {}, &catalog, &type_factory,
                             &analyzer_output));
  SimpleTable test_table{"TestTable", {{"col", types::Int64Type()}}};
  test_table.SetContents({{values::Int64(123)}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       test_table.CreateEvaluatorTableIterator({0}));
  std::ostringstream output;
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.executed(
      *analyzer_output->resolved_statement(), std::move(iter)));
  EXPECT_EQ(output.str(), R"(+-----+
| col |
+-----+
| 123 |
+-----+

)");
}

// Tests executed_multi with a mix of successful and failed results.
TEST(ExecuteQueryStreamWriterTest, ExecutedMulti) {
  SimpleCatalog catalog("simple_catalog");
  TypeFactory type_factory;
  // The implementation does not use the resolved node, so we just need
  // something to pass in.
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatement("SELECT 1", {}, &catalog, &type_factory,
                             &analyzer_output));

  // First table.
  SimpleTable test_table1{"TestTable1", {{"col1", types::Int64Type()}}};
  test_table1.SetContents({{values::Int64(123)}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter1,
                       test_table1.CreateEvaluatorTableIterator({0}));

  // Second table.
  SimpleTable test_table2{
      "TestTable2",
      {{"col_a", types::StringType()}, {"col_b", types::BoolType()}}};
  test_table2.SetContents({{values::String("foo"), values::Bool(true)},
                           {values::String("bar"), values::Bool(false)}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter2,
                       test_table2.CreateEvaluatorTableIterator({0, 1}));

  // Create three results to test a mix of success and failure.
  std::vector<absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>> results;
  results.push_back(std::move(iter1));
  results.push_back(absl::InvalidArgumentError("Test error"));
  results.push_back(std::move(iter2));

  std::ostringstream output;
  GOOGLESQL_EXPECT_OK(ExecuteQueryStreamWriter{output}.executed_multi(
      *analyzer_output->resolved_statement(), std::move(results)));
  EXPECT_EQ(output.str(), R"(+------+
| col1 |
+------+
| 123  |
+------+

Error: Test error
+-------+-------+
| col_a | col_b |
+-------+-------+
| foo   | true  |
| bar   | false |
+-------+-------+

)");
}

}  // namespace googlesql
