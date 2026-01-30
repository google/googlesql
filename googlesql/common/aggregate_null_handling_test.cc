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

#include "googlesql/common/aggregate_null_handling.h"

#include <memory>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testdata/sample_catalog.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

class AggregateNullHandlingTest : public ::testing::Test {
 public:
  void SetUp() override {
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_NULL_HANDLING_MODIFIER_IN_AGGREGATE);
  }

 protected:
  absl::Status AnalyzeQuery(absl::string_view sql) {
    return AnalyzeStatement(sql, analyzer_options_, catalog_.catalog(),
                            catalog_.type_factory(), &analyzer_output_);
  }

  // Extracts the aggregate function call from analyzer output.
  // Assumptions:
  //  - AnalyzeQuery() is called first (and succeeds)
  //  - Query takes the exact form:
  //      SELECT <agg-function>(<argument list>) FROM <table>
  const ResolvedAggregateFunctionCall* ExtractAggregateFunctionCall() {
    const ResolvedAggregateScan* agg_scan =
        analyzer_output_->resolved_statement()
            ->GetAs<ResolvedQueryStmt>()
            ->query()
            ->GetAs<ResolvedProjectScan>()
            ->input_scan()
            ->GetAs<ResolvedAggregateScan>();
    return agg_scan->aggregate_list()
        .front()
        ->GetAs<ResolvedComputedColumn>()
        ->expr()
        ->GetAs<ResolvedAggregateFunctionCall>();
  }

 private:
  AnalyzerOptions analyzer_options_;
  std::unique_ptr<const AnalyzerOutput> analyzer_output_;
  SampleCatalog catalog_;
};

TEST_F(AggregateNullHandlingTest, BuiltinIgnoreNullsImplicit) {
  std::string sql = "SELECT SUM(Key) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_TRUE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, BuiltinRespectNullsImplicit) {
  std::string sql = "SELECT ARRAY_AGG(Key) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_FALSE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, BuiltinIgnoreNullsExplicit) {
  std::string sql = "SELECT ARRAY_AGG(Key IGNORE NULLS) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_TRUE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, BuiltinRespectNullsExplicit) {
  std::string sql = "SELECT ARRAY_AGG(Key RESPECT NULLS) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_FALSE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, NonBuiltinIgnoreNullsImplicit) {
  std::string sql =
      "SELECT uda_valid_templated_return_sum_int64_arg(Key) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_FALSE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, NonBuiltinIgnoreNullsExplicit) {
  std::string sql =
      "SELECT uda_valid_templated_return_sum_int64_arg(Key IGNORE NULLS) FROM "
      "KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_TRUE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, NonBuiltinRespectNullsExplicit) {
  std::string sql =
      "SELECT uda_valid_templated_return_sum_int64_arg(Key RESPECT NULLS) FROM "
      "KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_FALSE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, BuiltinZeroArg) {
  std::string sql = "SELECT COUNT(*) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_TRUE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

TEST_F(AggregateNullHandlingTest, BuiltinTwoArgIgnoreNullsImplicit) {
  std::string sql = "SELECT CORR(Key, Key + 1) FROM KeyValue";
  GOOGLESQL_ASSERT_OK(AnalyzeQuery(sql));
  EXPECT_TRUE(IgnoresNullArguments(ExtractAggregateFunctionCall()));
}

}  // namespace
}  // namespace googlesql
