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

#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"

namespace googlesql {

// This is a visitor that copies resolved ASTs and modifies the join type
// by incrementing the enum (e.g. INNER -> LEFT).
//
// This is an example of copy-then-mutate during a deep copy.
class ModifyJoinScanCopyVisitor : public ResolvedASTRewriteVisitor {
 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedJoinScan(
      std::unique_ptr<const ResolvedJoinScan> node) override {
    ResolvedJoinScanBuilder builder = ToBuilder(std::move(node));

    ResolvedJoinScan::JoinType new_join_type =
        static_cast<ResolvedJoinScan::JoinType>((builder.join_type() + 1) % 4);

    return std::move(builder).set_join_type(new_join_type).Build();
  }
};

// This is a visitor that copies resolved ASTs and adds an explicit CAST
// around the WHERE clause.
//
// This is an example of the pop / recreate / push method of rewriting a node
// during copying.  The simpler method above is preferred.
class CastFilterScanCopyVisitor : public ResolvedASTRewriteVisitor {
 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFilterScan(
      std::unique_ptr<const ResolvedFilterScan> node) override {
    ResolvedFilterScanBuilder builder = ToBuilder(std::move(node));

    std::unique_ptr<const ResolvedExpr> filter_expr =
        builder.release_filter_expr();
    const Type* type = filter_expr->type();

    return std::move(builder)
        .set_filter_expr(ResolvedCastBuilder().set_type(type).set_expr(
            ResolvedCastBuilder()
                .set_type(types::StringType())
                .set_expr(std::move(filter_expr))))
        .Build();
  }
};

// This class will produce an error at the stated staged when
// it encounters a ResolvedLiteral of the given int value.
class ErrorCopyVisitor : public ResolvedASTRewriteVisitor {
 public:
  ErrorCopyVisitor(int pre_visit_error_value, int post_visit_error_value,
                   int wrong_type_error_value)
      : pre_visit_error_value_(pre_visit_error_value),
        post_visit_error_value_(post_visit_error_value),
        wrong_type_error_value_(wrong_type_error_value) {}

 private:
  absl::Status PreVisitResolvedLiteral(const ResolvedLiteral& node) override {
    if (node.value().type()->IsInteger() && !node.value().is_null() &&
        node.value().int64_value() == pre_visit_error_value_) {
      return absl::Status(absl::StatusCode::kCancelled, "pre visit error");
    }
    return absl::OkStatus();
  }
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedLiteral(
      std::unique_ptr<const ResolvedLiteral> node) override {
    if (node->value().type()->IsInteger() && !node->value().is_null() &&
        node->value().int64_value() == post_visit_error_value_) {
      return absl::Status(absl::StatusCode::kCancelled, "post visit error");
    }
    if (node->value().type()->IsInteger() && !node->value().is_null() &&
        node->value().int64_value() == wrong_type_error_value_) {
      // The actual type doesn't matter, but it doesn't share any class
      // hierarchy with ResolvedLiteral, so it's guaranteed to produce an
      // error when returned (except at top-level).
      return ResolvedObjectUnitBuilder().Build();
    }

    return node;
  }
  const int pre_visit_error_value_;
  const int post_visit_error_value_;
  const int wrong_type_error_value_;
};

class ResolvedASTRewriteVisitorTest : public ::testing::Test {
 protected:
  ResolvedASTRewriteVisitorTest() : catalog_("Test catalog", nullptr) {}

  void SetUp() override {
    // Must add GoogleSQL functions or the catalog will not be parsed properly.
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());

    // All tests will use this catalog, which defines the schema of the tables
    // that we are running SQL queries on. Set up a schema for all queries.

    // Add a table called adwords_impressions. Used for complex mock queries.
    catalog_.AddOwnedTable(new SimpleTable(
        "adwords_impressions",
        {
            {"campaign_id", type_factory_.get_string()},
            {"demographics_gender", type_factory_.get_string()},
            {"demographics_age_group", type_factory_.get_string()},
            {"company_id", type_factory_.get_string()},
            {"customer_id", type_factory_.get_string()},
        }));

    // Add a table called campaign. Used for complex mock queries.
    catalog_.AddOwnedTable(new SimpleTable(
        "campaign", {
                        {"campaign_id", type_factory_.get_string()},
                        {"campaign_name", type_factory_.get_string()},
                    }));

    // Add a table called gender. Used for complex mock queries.
    catalog_.AddOwnedTable(new SimpleTable(
        "gender", {
                      {"gender_id", type_factory_.get_string()},
                      {"gender_name", type_factory_.get_string()},
                  }));

    // Add a table called age_group. Used for complex mock queries.
    catalog_.AddOwnedTable(new SimpleTable(
        "age_group", {
                         {"age_group_id", type_factory_.get_string()},
                         {"age_group_name", type_factory_.get_string()},
                     }));

    // Add a table called Temp. Used for conversion test.
    catalog_.AddOwnedTable(new SimpleTable(
        "Temp", {
                    {"double_field", type_factory_.get_double()},
                    {"int32_field", type_factory_.get_int32()},
                }));

    // Must explicitly add analytic functions, or GoogleSQL parser will not
    // parse queries with analytics.
    options_.mutable_language()->EnableLanguageFeature(
        FEATURE_ANALYTIC_FUNCTIONS);
  }

  // Analyze <query> and then apply <visitor> to copy its resolved AST,
  // then return the copy.
  std::unique_ptr<const ResolvedNode> ApplyCopyVisitor(
      absl::string_view query, ResolvedASTRewriteVisitor* visitor);

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> ApplyCopyVisitorImpl(
      absl::string_view query, ResolvedASTRewriteVisitor* visitor);

  // Create AST and deep copied AST for a given query. Verify that all
  // statuses work as expected and that the debug string matches.
  std::unique_ptr<const ResolvedNode> TestDeepCopyAST(absl::string_view query);
  std::unique_ptr<const ResolvedNode> TestModifyJoinScanCopyVisitor(
      absl::string_view query);
  std::unique_ptr<const ResolvedNode> TestCastFilterScanCopyVisitor(
      absl::string_view query);
  absl::Status TestErrorPropagationInVisitor(absl::string_view query,
                                             int pre_visit_error_value,
                                             int post_visit_error_value,
                                             int wrong_type_error_value);

  // Keeps the analyzer outputs from the tests alive without having to pass them
  // around. This is a vector because there are tests that analyze multiple
  // statements, and that need the outputs from those multiple analyses to
  // remain live at the same time.
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_outputs_;

  AnalyzerOptions options_;
  SimpleCatalog catalog_;
  TypeFactory type_factory_;
  IdStringPool id_string_pool_;
};

std::unique_ptr<const ResolvedNode>
ResolvedASTRewriteVisitorTest::TestDeepCopyAST(absl::string_view query) {
  // Parse query into AST.
  analyzer_outputs_.emplace_back();
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(query, options_, &catalog_, &type_factory_,
                             &analyzer_outputs_.back()));

  const ResolvedNode* original = analyzer_outputs_.back()->resolved_statement();

  // Need to use deep copy visitor to get a mutable copy, since analyzer
  // output is const.
  absl::StatusOr<std::unique_ptr<ResolvedNode>> deep_copy =
      ResolvedASTDeepCopyVisitor::Copy(original);
  GOOGLESQL_CHECK_OK(deep_copy);

  // Create rewrite visitor.
  ResolvedASTRewriteVisitor visitor;
  // Accept the visitor on the resolved query.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> rewrite_copy =
      visitor.VisitAll<ResolvedNode>(std::move(deep_copy).value());
  GOOGLESQL_CHECK_OK(rewrite_copy);
  // Verify that the debug string matches.
  EXPECT_EQ(original->DebugString(), (*rewrite_copy)->DebugString());

  return std::move(rewrite_copy).value();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
ResolvedASTRewriteVisitorTest::ApplyCopyVisitorImpl(
    absl::string_view query, ResolvedASTRewriteVisitor* visitor) {
  // Parse query into AST.
  analyzer_outputs_.emplace_back();
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(query, options_, &catalog_, &type_factory_,
                             &analyzer_outputs_.back()));

  const ResolvedNode* original = analyzer_outputs_.back()->resolved_statement();

  // Need to use deep copy visitor to get a mutable copy, since analyzer
  // output is const.
  absl::StatusOr<std::unique_ptr<ResolvedNode>> deep_copy =
      ResolvedASTDeepCopyVisitor::Copy(original);
  GOOGLESQL_CHECK_OK(deep_copy);

  // Accept the visitor on the resolved query.
  return visitor->VisitAll<ResolvedNode>(std::move(deep_copy).value());
}

std::unique_ptr<const ResolvedNode>
ResolvedASTRewriteVisitorTest::ApplyCopyVisitor(
    absl::string_view query, ResolvedASTRewriteVisitor* visitor) {
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> rewrite_copy =
      ApplyCopyVisitorImpl(query, visitor);
  GOOGLESQL_CHECK_OK(rewrite_copy);

  // Return the copied tree.
  return std::move(rewrite_copy).value();
}

std::unique_ptr<const ResolvedNode>
ResolvedASTRewriteVisitorTest::TestModifyJoinScanCopyVisitor(
    absl::string_view query) {
  ModifyJoinScanCopyVisitor visitor;
  return ApplyCopyVisitor(query, &visitor);
}

std::unique_ptr<const ResolvedNode>
ResolvedASTRewriteVisitorTest::TestCastFilterScanCopyVisitor(
    absl::string_view query) {
  CastFilterScanCopyVisitor visitor;
  return ApplyCopyVisitor(query, &visitor);
}

absl::Status ResolvedASTRewriteVisitorTest::TestErrorPropagationInVisitor(
    absl::string_view query, int pre_visit_error_value,
    int post_visit_error_value, int wrong_type_error_value) {
  ErrorCopyVisitor visitor(pre_visit_error_value, post_visit_error_value,
                           wrong_type_error_value);
  return ApplyCopyVisitorImpl(query, &visitor).status();
}

TEST_F(ResolvedASTRewriteVisitorTest, DeepCopyASTTest) {
  // Test that we are able to perform deep copy of the AST with no errors
  // and a matching debug string.

  // Test query.
  const std::string input_sql = R"(
  SELECT adwords_impressions.company_id as aliased_company_id, gender_name,
         COUNT(gender_name)
  FROM gender
  JOIN adwords_impressions ON
       adwords_impressions.demographics_gender=gender.gender_id
  GROUP BY 1, 2
  HAVING COUNT(DISTINCT aliased_company_id) >= 50)";

  // Create AST deep copy and verify.
  TestDeepCopyAST(input_sql);
}

TEST_F(ResolvedASTRewriteVisitorTest, TestCopyASTTestComplex) {
  // Test that DeepCopy works for a more complex query.
  const std::string input_sql = R"(
      WITH companies AS (
      SELECT
        campaign_id,
        demographics_gender as gender_id,
        demographics_age_group as age_group_id,
        company_id,
        COUNT(*) AS imps
      FROM adwords_impressions
      WHERE customer_id = "999999"
      GROUP BY 1, 2, 3, 4
    ),
    campaign_totals AS (
      SELECT
        campaign_id,
        SUM(imps) AS campaign_impressions
      FROM companies
      GROUP BY 1
    ),
    report AS (
      SELECT
        campaign_id,
        gender_id,
        age_group_id,
        COUNT(*) AS companies,
        SUM(imps) AS impressions
      FROM companies
      GROUP BY 1, 2, 3
    )
    SELECT *, companies AS company_count
    FROM report
      LEFT JOIN campaign_totals USING (campaign_id)
      LEFT JOIN campaign USING (campaign_id)
      LEFT JOIN gender USING (gender_id)
      LEFT JOIN age_group  USING (age_group_id))";

  TestDeepCopyAST(input_sql);
}

TEST_F(ResolvedASTRewriteVisitorTest, TestCopyASTTestHint) {
  // Tests that it works for hints.
  const std::string input_sql = "select @{ key = 5 } 123";
  TestDeepCopyAST(input_sql);
}

TEST_F(ResolvedASTRewriteVisitorTest, TestModifyJoinScan) {
  // Tests that it updates the join type.
  const std::string input_sql =
      "SELECT a.double_field FROM Temp a JOIN Temp b USING (int32_field)";
  auto ast = TestModifyJoinScanCopyVisitor(input_sql);

  const std::string input_sql_modified =
      "SELECT a.double_field FROM Temp a LEFT JOIN Temp b USING (int32_field)";
  auto desired_ast = TestDeepCopyAST(input_sql_modified);

  ASSERT_EQ(ast->DebugString(), desired_ast->DebugString());
}

TEST_F(ResolvedASTRewriteVisitorTest, TestCastFilterScan) {
  // Tests that it updates the filter expression.
  const std::string input_sql =
      "SELECT double_field FROM Temp WHERE int32_field = 10";
  auto ast = TestCastFilterScanCopyVisitor(input_sql);

  const std::string input_sql_modified =
      "SELECT double_field FROM Temp WHERE "
      "CAST(CAST(int32_field = 10 AS STRING) AS BOOL)";
  auto desired_ast = TestDeepCopyAST(input_sql_modified);

  ASSERT_EQ(ast->DebugString(), desired_ast->DebugString());
}

TEST_F(ResolvedASTRewriteVisitorTest, TestErrorPropagation) {
  // Queries are expected to fail at specific stages.
  {
    const std::string input_sql = "SELECT 1";
    EXPECT_THAT(TestErrorPropagationInVisitor(input_sql, 1, 0, 0),
                absl_testing::StatusIs(absl::StatusCode::kCancelled,
                                          "pre visit error"));
    EXPECT_THAT(TestErrorPropagationInVisitor(input_sql, 0, 1, 0),
                absl_testing::StatusIs(absl::StatusCode::kCancelled,
                                          "post visit error"));
    EXPECT_THAT(TestErrorPropagationInVisitor(input_sql, 0, 0, 1),
                absl_testing::StatusIs(absl::StatusCode::kInternal));
  }
  {
    // Slightly more complicated.
    const std::string input_sql = "SELECT * from (select 5) where 1 + 1 = 1";
    EXPECT_THAT(TestErrorPropagationInVisitor(input_sql, 1, 0, 0),
                absl_testing::StatusIs(absl::StatusCode::kCancelled,
                                          "pre visit error"));
    EXPECT_THAT(TestErrorPropagationInVisitor(input_sql, 0, 1, 0),
                absl_testing::StatusIs(absl::StatusCode::kCancelled,
                                          "post visit error"));
    EXPECT_THAT(TestErrorPropagationInVisitor(input_sql, 0, 0, 1),
                absl_testing::StatusIs(absl::StatusCode::kInternal));
  }
}

TEST_F(ResolvedASTRewriteVisitorTest, TestOrderByNotRepropagated) {
  ResolvedColumn column;
  const SimpleTable t1{"t1"};

  auto scan = ResolvedLimitOffsetScanBuilder()
                  .add_column_list(column)
                  .set_offset(MakeResolvedLiteral(Value::Int64(1)))
                  .set_limit(MakeResolvedLiteral(Value::Int64(1)))
                  .set_input_scan(ResolvedTableScanBuilder()
                                      .add_column_list(column)
                                      .set_table(&t1)
                                      .set_is_ordered(true))
                  .Build()
                  .value();
  // Verify that the default behavior will be to propagate order from
  // the input scan.
  EXPECT_TRUE(scan->is_ordered());
  scan = ToBuilder(std::move(scan)).set_is_ordered(false).Build().value();
  // Now force it false.
  EXPECT_FALSE(scan->is_ordered());
  ResolvedASTRewriteVisitor identity_visitor;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto new_scan,
      identity_visitor.VisitAll<ResolvedLimitOffsetScan>(std::move(scan)));

  EXPECT_FALSE(new_scan->is_ordered());
}

class VisitOrderInspectingVisitor : public ResolvedASTRewriteVisitor {
 public:
  absl::Status PreVisitResolvedFunctionCall(
      const ResolvedFunctionCall& call) override {
    GOOGLESQL_RET_CHECK_EQ(call.hint_list().size(), 1);

    full_hint_stack_.push_back(call.hint_list(0)->name());
    hint_stack_.push_back(call.hint_list(0)->name());
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionCall(
      std::unique_ptr<const ResolvedFunctionCall> call) override {
    GOOGLESQL_RET_CHECK_EQ(call->hint_list().size(), 1);
    GOOGLESQL_RET_CHECK_EQ(call->hint_list(0)->name(), hint_stack_.back());
    hint_stack_.pop_back();

    return call;
  }

  std::vector<std::string> full_hint_stack_;
  std::vector<std::string> hint_stack_;
};

TEST_F(ResolvedASTRewriteVisitorTest, TestVisitOrder) {
  constexpr absl::string_view input_sql =
      R"(SELECT concat(
                  (select concat((select concat(1) @{C=1} )) @{B=1} )
                  ) @{A=1},
                concat(1) @{D=1})";
  std::unique_ptr<const AnalyzerOutput> analyzed;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(input_sql, options_, &catalog_, &type_factory_,
                             &analyzed));

  VisitOrderInspectingVisitor visitor;
  ApplyCopyVisitor(input_sql, &visitor);
  EXPECT_THAT(visitor.hint_stack_, testing::IsEmpty());
  EXPECT_THAT(visitor.full_hint_stack_,
              testing::ElementsAre("A", "B", "C", "D"));
}

}  // namespace googlesql
