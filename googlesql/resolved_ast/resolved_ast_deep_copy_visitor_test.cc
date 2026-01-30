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

#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::UnorderedElementsAre;
using ::absl_testing::StatusIs;

// This is a visitor that copies resolved ASTs and modifies the join type
// by incrementing the enum (e.g. INNER -> LEFT).
//
// This is an example of copy-then-mutate during a deep copy.
class ModifyJoinScan : public ResolvedASTDeepCopyVisitor {
 private:
  absl::Status VisitResolvedJoinScan(
      const ResolvedJoinScan* node) override {
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedJoinScan(node));

    ResolvedJoinScan* join = GetUnownedTopOfStack<ResolvedJoinScan>();

    // Just rotate the JoinType one position forwards in the enum.
    ResolvedJoinScan::JoinType new_join_type =
        static_cast<ResolvedJoinScan::JoinType>((join->join_type() + 1) % 4);

    join->set_join_type(new_join_type);

    return absl::OkStatus();
  }
};

// This is a visitor that copies resolved ASTs and adds an explicit CAST
// around the WHERE clause.
//
// This is an example of the pop / recreate / push method of rewriting a node
// during copying.  The simpler method above is preferred.
class CastFilterScan : public ResolvedASTDeepCopyVisitor {
 private:
  absl::Status VisitResolvedFilterScan(
      const ResolvedFilterScan* node) override {
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedFilterScan(node));

    std::unique_ptr<ResolvedFilterScan> filter =
        ConsumeTopOfStack<ResolvedFilterScan>();

    const Type* type = filter->filter_expr()->type();
    auto new_expr =
        MakeResolvedCast(types::StringType(), filter->release_filter_expr(),
                         /*return_null_on_error=*/false);
    new_expr = MakeResolvedCast(type, std::move(new_expr),
                                /* return_null_on_error=*/false);

    // Allocate a new ResolvedFilterScan, releasing the copied node for the
    // old child scan.
    auto new_filter = MakeResolvedFilterScan(filter->column_list(),
                                             filter->release_input_scan(),
                                             std::move(new_expr));

    PushNodeToStack(std::move(new_filter));
    return absl::OkStatus();
  }
};

// This is a more complex example of modifying an AST.
// This class does one thing: adds a WHERE clause to a simple query. It works
// by visiting all table scans, and when it finds a table scan on the column
// name and table provided, will add a >= comparison for the given threshold.
//
// Because this is just an example, corner cases are likely not covered.
class AddFilterToTableScan : public ResolvedASTDeepCopyVisitor {
 public:
  // Constructor for AddFilterToTableScan. Takes a table name and column
  // name to apply filter to, and the threshold in wihch to apply the >= on.
  AddFilterToTableScan(absl::string_view table_name,
                       absl::string_view column_name, int64_t threshold,
                       SimpleCatalog* catalog)
      : table_name_(table_name),
        column_name_(column_name),
        threshold_(threshold),
        catalog_(catalog) {}

 private:
  absl::Status VisitResolvedFilterScan(
      const ResolvedFilterScan* node) override {
    // Code does not exist for this case, so just return failure. In a real
    // example, you would likely try to make an association between the
    // table scan children of this node and add an AND clause to combine
    // the existing filter with the added one as appropriate.
    return absl::Status(absl::StatusCode::kCancelled,
                        "No filter scans allowed.");
  }

  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    // Visit a table scan. If it is on the correct table/column, add the filter.
    int column_id_in_scan = -1;

    // Check if it has the column.
    for (const auto& column : node->column_list()) {
      if (column.table_name() == table_name_ && column.name() == column_name_) {
        column_id_in_scan = column.column_id();
        break;
      }
    }

    // If the ResolvedTableScan is over the correct column, add the filter.
    if (column_id_in_scan >= 0) {
      // Get a copy of the ResolvedTableScan to use as an input scan for the
      // filter scan. We push it to the stack and pop it to get a deep copy.
      EXPECT_EQ(node->for_system_time_expr(), nullptr);
      PushNodeToStack(
          MakeResolvedTableScan(node->column_list(), node->table(), nullptr));
      auto table_scan_copy = ConsumeTopOfStack<ResolvedTableScan>();
      table_scan_copy->set_column_index_list(node->column_index_list());

      // Create the function for the filter expression.
      const Function* function = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(catalog_->FindFunction(
          {FunctionSignatureIdToName(FN_GREATER_OR_EQUAL)}, &function));

      // Create a new column for the input of >= comparison.
      ResolvedColumn input_column(
          column_id_in_scan, googlesql::IdString::MakeGlobal(table_name_),
          googlesql::IdString::MakeGlobal(column_name_), types::Int32Type());

      // We have two arguments for our function call, the column that we compare
      // and the literal value.
      auto argument_list = MakeNodeVector(
          MakeResolvedColumnRef(types::Int32Type(), input_column, false),
          MakeResolvedLiteral(types::Int32Type(), Value::Int32(threshold_)));

      // We have only twos argument, and the type of both are int32.
      FunctionArgumentTypeList argument_type_list = {types::Int32Type(),
                                                     types::Int32Type()};

      // The type of the result of function call. It is bool, as we are doing
      // a comparison.
      const FunctionArgumentType fn_result_type(types::BoolType());

      // Create the FunctionSignature. Return type is Bool, argument type is
      // int32. There is no context_ptr, so pass nullptr.
      FunctionSignature signature(fn_result_type, argument_type_list, nullptr);

      // Create the filter expr.
      auto filter_expr = MakeResolvedFunctionCall(
          types::BoolType(), function, signature, std::move(argument_list),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);

      // Add this new filter scan to the stack, instead of the old table scan.
      PushNodeToStack(MakeResolvedFilterScan(node->column_list(),
                                             std::move(table_scan_copy),
                                             std::move(filter_expr)));
      return absl::OkStatus();
    } else {
      // Does not contain the column, so we should not modify it.
      return CopyVisitResolvedTableScan(node);
    }
  }

 private:
  const std::string table_name_;
  const std::string column_name_;
  int64_t threshold_;
  SimpleCatalog* catalog_;
};

class ResolvedASTDeepCopyVisitorTest : public ::testing::Test {
 protected:
  ResolvedASTDeepCopyVisitorTest() : catalog_("Test catalog", nullptr) {}

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
  std::unique_ptr<ResolvedNode> ApplyCopyVisitor(
      absl::string_view query, ResolvedASTDeepCopyVisitor* visitor);

  // Create AST and deep copied AST for a given query. Verify that all
  // statuses work as expected and that the debug string matches.
  std::unique_ptr<ResolvedNode> TestDeepCopyAST(absl::string_view query);
  std::unique_ptr<ResolvedNode> TestAddFilterToTableScan(
      absl::string_view query);
  std::unique_ptr<ResolvedNode> TestModifyJoinScan(absl::string_view query);
  std::unique_ptr<ResolvedNode> TestCastFilterScan(absl::string_view query);
  void TestAddFilterToTableScanError(absl::string_view query,
                                     absl::string_view error);

  // Keeps the analyzer outputs from the tests alive without having to pass them
  // around. This is a vector because there are tests that analyze multiple
  // statements, and that need the outputs from those multiple analyses to
  // remain live at the same time.
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_outputs_;

  AnalyzerOptions options_;
  SimpleCatalog catalog_;
  TypeFactory type_factory_;
};

std::unique_ptr<ResolvedNode> ResolvedASTDeepCopyVisitorTest::TestDeepCopyAST(
    absl::string_view query) {
  // Parse query into AST.
  analyzer_outputs_.emplace_back();
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(query, options_, &catalog_, &type_factory_,
                             &analyzer_outputs_.back()));

  // Get the original debug string.
  const std::string original_debug_string =
      analyzer_outputs_.back()->resolved_statement()->DebugString();

  // Create deep copy visitor.
  ResolvedASTDeepCopyVisitor visitor;
  // Accept the visitor on the resolved query.
  GOOGLESQL_EXPECT_OK(analyzer_outputs_.back()->resolved_statement()->Accept(&visitor));

  // Consume the root to initiate deep copy.
  auto deep_copy = visitor.ConsumeRootNode<ResolvedNode>();

  // ConsumeRootNode returns StatusOr -- verify that the status was OK.
  GOOGLESQL_EXPECT_OK(deep_copy);

  // Get the value from the StatusOr.
  auto deep_copy_ast = std::move(deep_copy).value();

  // Verify that the debug string matches.
  EXPECT_EQ(original_debug_string, deep_copy_ast->DebugString());

  return deep_copy_ast;
}

std::unique_ptr<ResolvedNode> ResolvedASTDeepCopyVisitorTest::ApplyCopyVisitor(
    absl::string_view query, ResolvedASTDeepCopyVisitor* visitor) {
  // Parse query into AST.
  analyzer_outputs_.emplace_back();
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(query, options_, &catalog_, &type_factory_,
                             &analyzer_outputs_.back()));

  // Get the original debug string.
  const std::string original_debug_string =
      analyzer_outputs_.back()->resolved_statement()->DebugString();

  // Accept the visitor on the resolved query.
  GOOGLESQL_EXPECT_OK(analyzer_outputs_.back()->resolved_statement()->Accept(visitor));

  // Consume the root to initiate deep copy.
  auto deep_copy = visitor->ConsumeRootNode<ResolvedNode>();

  // ConsumeRootNode returns StatusOr -- verify that the status was OK.
  GOOGLESQL_EXPECT_OK(deep_copy);

  // Get the value from the StatusOr.
  auto deep_copy_ast = std::move(deep_copy).value();

  // Return the copied tree.
  return deep_copy_ast;
}

std::unique_ptr<ResolvedNode>
ResolvedASTDeepCopyVisitorTest::TestAddFilterToTableScan(
    absl::string_view query) {
  AddFilterToTableScan visitor("Temp", "int32_field", 50, &catalog_);
  return ApplyCopyVisitor(query, &visitor);
}

std::unique_ptr<ResolvedNode>
ResolvedASTDeepCopyVisitorTest::TestModifyJoinScan(absl::string_view query) {
  ModifyJoinScan visitor;
  return ApplyCopyVisitor(query, &visitor);
}

std::unique_ptr<ResolvedNode>
ResolvedASTDeepCopyVisitorTest::TestCastFilterScan(absl::string_view query) {
  CastFilterScan visitor;
  return ApplyCopyVisitor(query, &visitor);
}

void ResolvedASTDeepCopyVisitorTest::TestAddFilterToTableScanError(
    absl::string_view query, absl::string_view error) {
  // Parse query into AST.
  analyzer_outputs_.emplace_back();
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(query, options_, &catalog_, &type_factory_,
                             &analyzer_outputs_.back()));

  // Get the original debug string.
  const std::string original_debug_string =
      analyzer_outputs_.back()->resolved_statement()->DebugString();

  // Create deep copy visitor.
  AddFilterToTableScan visitor("Temp", "int32_field", 50, &catalog_);
  // Accept the visitor on the resolved query.
  EXPECT_THAT(analyzer_outputs_.back()->resolved_statement()->Accept(&visitor),
              StatusIs(testing::_, "No filter scans allowed."));
}

TEST_F(ResolvedASTDeepCopyVisitorTest, DeepCopyASTTest) {
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

TEST_F(ResolvedASTDeepCopyVisitorTest, DeepCopyASTTestComplex) {
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

TEST_F(ResolvedASTDeepCopyVisitorTest, TestDeepCopyASTTestHint) {
  // Tests that it works for hints.
  const std::string input_sql = "select @{ key = 5 } 123";
  TestDeepCopyAST(input_sql);
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestAddFilterScanTest) {
  // Tests that it adds filter around scan the scan.
  const std::string input_sql = "SELECT int32_field FROM Temp";
  auto ast = TestAddFilterToTableScan(input_sql);

  const std::string input_sql_with_filter =
      "SELECT int32_field FROM Temp WHERE int32_field >= 50";
  auto desired_ast = TestDeepCopyAST(input_sql_with_filter);

  ASSERT_EQ(ast->DebugString(), desired_ast->DebugString());
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestAddFilterScanTestError) {
  // Tests that it adds filter around the scan.
  const std::string input =
      "SELECT int32_field FROM Temp WHERE int32_field >= 50";
  TestAddFilterToTableScanError(input, "No filter scans allowed");
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestModifyJoinScan) {
  // Tests that it updates the join type.
  const std::string input_sql =
      "SELECT a.double_field FROM Temp a JOIN Temp b USING (int32_field)";
  auto ast = TestModifyJoinScan(input_sql);

  const std::string input_sql_modified =
      "SELECT a.double_field FROM Temp a LEFT JOIN Temp b USING (int32_field)";
  auto desired_ast = TestDeepCopyAST(input_sql_modified);

  ASSERT_EQ(ast->DebugString(), desired_ast->DebugString());
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestCastFilterScan) {
  // Tests that it updates the filter expression.
  const std::string input_sql =
      "SELECT double_field FROM Temp WHERE int32_field = 10";
  auto ast = TestCastFilterScan(input_sql);

  const std::string input_sql_modified =
      "SELECT double_field FROM Temp WHERE "
      "CAST(CAST(int32_field = 10 AS STRING) AS BOOL)";
  auto desired_ast = TestDeepCopyAST(input_sql_modified);

  ASSERT_EQ(ast->DebugString(), desired_ast->DebugString());
}

class ColumnRecordingResolvedASTDeepCopyVisitor
    : public ResolvedASTDeepCopyVisitor {
 public:
  absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
      const ResolvedColumn& column) override {
    columns_touched_.insert(column.DebugString());
    return column;
  }

  std::set<std::string> columns_touched_;
};

TEST_F(ResolvedASTDeepCopyVisitorTest, TestVisitResolvedColumnSimple) {
  constexpr absl::string_view input_sql =
      "SELECT * FROM (SELECT 1 AS a, 4 AS b, 'abc' AS c) ";
  std::unique_ptr<const AnalyzerOutput> analyzed;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(input_sql, options_, &catalog_, &type_factory_,
                             &analyzed));

  // Sanity check to make sure the test input doesn't regress.
  EXPECT_THAT(analyzed->resolved_statement()->DebugString(),
              AllOf(HasSubstr("a#1"), HasSubstr("b#2"), HasSubstr("c#3"),
                    Not(HasSubstr("#4"))));

  ColumnRecordingResolvedASTDeepCopyVisitor visitor;
  GOOGLESQL_EXPECT_OK(analyzed->resolved_statement()->Accept(&visitor));
  EXPECT_THAT(visitor.columns_touched_,
              UnorderedElementsAre("$subquery1.a#1", "$subquery1.b#2",
                                   "$subquery1.c#3"));
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestVisitResolvedColumnCorrelated) {
  constexpr absl::string_view input_sql =
      "SELECT (SELECT AS STRUCT a + b, c) as st "
      "FROM (SELECT 1 AS a, 4 AS b, 'abc' AS c) ";
  std::unique_ptr<const AnalyzerOutput> analyzed;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(input_sql, options_, &catalog_, &type_factory_,
                             &analyzed));

  // Sanity check to make sure the test input doesn't regress.
  EXPECT_THAT(
      analyzed->resolved_statement()->DebugString(),
      AllOf(HasSubstr("a#1"), HasSubstr("b#2"), HasSubstr("c#3"),
            HasSubstr("$col1#4"), HasSubstr("c#5"), HasSubstr("$struct#6"),
            HasSubstr("st#7"), Not(HasSubstr("#8"))));

  ColumnRecordingResolvedASTDeepCopyVisitor visitor;
  GOOGLESQL_EXPECT_OK(analyzed->resolved_statement()->Accept(&visitor));
  EXPECT_THAT(
      visitor.columns_touched_,
      UnorderedElementsAre("$subquery1.a#1", "$subquery1.b#2", "$subquery1.c#3",
                           "$expr_subquery.$col1#4", "$expr_subquery.c#5",
                           "$make_struct.$struct#6", "$query.st#7"));
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestVisitResolvedColumnAggregate) {
  constexpr absl::string_view input_sql =
      "SELECT DISTINCT SUM(a + b) "
      "FROM (SELECT 1 AS a, 4 AS b, 'abc' AS c) "
      "GROUP BY c";
  std::unique_ptr<const AnalyzerOutput> analyzed;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(input_sql, options_, &catalog_, &type_factory_,
                             &analyzed));

  // Sanity check to make sure the test input doesn't regress.
  EXPECT_THAT(analyzed->resolved_statement()->DebugString(),
              AllOf(HasSubstr("a#1"), HasSubstr("b#2"), HasSubstr("c#3"),
                    HasSubstr("$agg1#4"), HasSubstr("c#5"),
                    HasSubstr("$agg1#6"), Not(HasSubstr("#7"))));

  ColumnRecordingResolvedASTDeepCopyVisitor visitor;
  GOOGLESQL_EXPECT_OK(analyzed->resolved_statement()->Accept(&visitor));
  EXPECT_THAT(visitor.columns_touched_,
              UnorderedElementsAre("$subquery1.a#1", "$subquery1.b#2",
                                   "$subquery1.c#3", "$aggregate.$agg1#4",
                                   "$groupby.c#5", "$distinct.$agg1#6"));
}

TEST_F(ResolvedASTDeepCopyVisitorTest, TestVisitResolvedColumnReplace) {
  constexpr absl::string_view input_sql =
      "SELECT * REPLACE (b AS a) "
      "FROM (SELECT 1 AS a, 4 AS b, 'abc' AS c) ";
  AnalyzerOptions options = options_;
  options.mutable_language()->EnableMaximumLanguageFeatures();
  std::unique_ptr<const AnalyzerOutput> analyzed;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(input_sql, options, &catalog_, &type_factory_,
                             &analyzed));

  // Sanity check to make sure the test input doesn't regress.
  EXPECT_THAT(analyzed->resolved_statement()->DebugString(),
              AllOf(HasSubstr("a#1"), HasSubstr("b#2"), HasSubstr("c#3"),
                    Not(HasSubstr("#4"))));

  ColumnRecordingResolvedASTDeepCopyVisitor visitor;
  GOOGLESQL_EXPECT_OK(analyzed->resolved_statement()->Accept(&visitor));
  EXPECT_THAT(visitor.columns_touched_,
              UnorderedElementsAre("$subquery1.a#1", "$subquery1.b#2",
                                   "$subquery1.c#3"));
}

}  // namespace googlesql
