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

#include "googlesql/common/match_recognize/test_pattern_resolver.h"

#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::absl_testing::StatusIs;

class TestPatternResolverTest : public testing::Test {
 protected:
  TestPatternResolver resolver_;
};

MATCHER_P(IsPatternVarWithName, expected_name, "") {
  if (arg == nullptr) {
    *result_listener << "is null";
    return false;
  }
  if (!arg->template Is<ResolvedMatchRecognizePatternVariableRef>()) {
    *result_listener << "expected a pattern ref, but found "
                     << arg->DebugString();
    return false;
  }

  return ExplainMatchResult(
      Eq(expected_name),
      arg->template GetAs<ResolvedMatchRecognizePatternVariableRef>()->name(),
      result_listener);
}

TEST_F(TestPatternResolverTest, PatternTree) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A B"));

  EXPECT_TRUE(scan->pattern()->Is<ResolvedMatchRecognizePatternOperation>());
  const ResolvedMatchRecognizePatternOperation* op =
      scan->pattern()->GetAs<ResolvedMatchRecognizePatternOperation>();
  EXPECT_EQ(op->op_type(), ResolvedMatchRecognizePatternOperationEnums::CONCAT);
  EXPECT_EQ(op->operand_list_size(), 2);
  EXPECT_TRUE(
      op->operand_list(0)->Is<ResolvedMatchRecognizePatternVariableRef>());
  EXPECT_THAT(op->operand_list(), ElementsAre(IsPatternVarWithName("A"),
                                              IsPatternVarWithName("B")));
}

TEST_F(TestPatternResolverTest, PatternVariableList) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A B"));

  // For purposes of testing the state machine code, we only care about the
  // names of the pattern variables, not the predicate expressions.
  EXPECT_EQ(scan->pattern_variable_definition_list_size(), 2);
  EXPECT_EQ(scan->pattern_variable_definition_list(0)->name(), "A");
  EXPECT_EQ(scan->pattern_variable_definition_list(1)->name(), "B");
}

TEST_F(TestPatternResolverTest, MultipleScans) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan1,
                       resolver_.ResolvePattern("A B"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan2,
                       resolver_.ResolvePattern("C D"));

  // Sanity check that multiple calls to ResolvePattern() on the same
  // TestPatternResolver object work.
  EXPECT_EQ(scan1->pattern_variable_definition_list(0)->name(), "A");
  EXPECT_EQ(scan1->pattern_variable_definition_list(1)->name(), "B");
  EXPECT_EQ(scan2->pattern_variable_definition_list(0)->name(), "C");
  EXPECT_EQ(scan2->pattern_variable_definition_list(1)->name(), "D");
}

TEST_F(TestPatternResolverTest, PatternSyntaxError) {
  EXPECT_THAT(resolver_.ResolvePattern("(A"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TestPatternResolverTest, UnsupportedQueryParam) {
  EXPECT_THAT(resolver_.ResolvePattern("A{@foo}"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TestPatternResolverTest, NamedQueryParam) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern(
          "A{@foo, @bar}",
          {.parameters = QueryParametersMap{{"foo", types::Int64Type()},
                                            {"bar", types::Int64Type()}}}));

  ASSERT_TRUE(
      scan->pattern()->Is<ResolvedMatchRecognizePatternQuantification>());
  const ResolvedMatchRecognizePatternQuantification* quantification =
      scan->pattern()->GetAs<ResolvedMatchRecognizePatternQuantification>();
  ASSERT_TRUE(quantification->lower_bound()->Is<ResolvedParameter>());
  EXPECT_EQ(quantification->lower_bound()->GetAs<ResolvedParameter>()->name(),
            "foo");
  ASSERT_TRUE(quantification->upper_bound()->Is<ResolvedParameter>());
  EXPECT_EQ(quantification->upper_bound()->GetAs<ResolvedParameter>()->name(),
            "bar");
}

TEST_F(TestPatternResolverTest, PositionalQueryParam) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?, ?}",
                               {.parameters = std::vector<const Type*>{
                                    types::Int64Type(), types::Int64Type()}}));
  ASSERT_TRUE(
      scan->pattern()->Is<ResolvedMatchRecognizePatternQuantification>());
  const ResolvedMatchRecognizePatternQuantification* quantification =
      scan->pattern()->GetAs<ResolvedMatchRecognizePatternQuantification>();
  ASSERT_TRUE(quantification->lower_bound()->Is<ResolvedParameter>());
  EXPECT_EQ(
      quantification->lower_bound()->GetAs<ResolvedParameter>()->position(), 1);
  ASSERT_TRUE(quantification->upper_bound()->Is<ResolvedParameter>());
  EXPECT_EQ(
      quantification->upper_bound()->GetAs<ResolvedParameter>()->position(), 2);
}

TEST_F(TestPatternResolverTest, AfterMatchSkipPastLastRowIsDefault) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A B"));

  EXPECT_EQ(scan->after_match_skip_mode(),
            ResolvedMatchRecognizeScanEnums::END_OF_MATCH);
}

TEST_F(TestPatternResolverTest, AfterMatchSkipToNextRow) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern(
          "A B", {.after_match_skip_mode =
                      ResolvedMatchRecognizeScanEnums::NEXT_ROW}));

  EXPECT_EQ(scan->after_match_skip_mode(),
            ResolvedMatchRecognizeScanEnums::NEXT_ROW);
}

TEST_F(TestPatternResolverTest, LongestMatchModeOffByDefault) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A B"));
  EXPECT_EQ(scan->option_list_size(), 0);
}

TEST_F(TestPatternResolverTest, LongestMatchModeExplicit) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A B", {.longest_match_mode_sql = "TRUE"}));

  EXPECT_EQ(scan->option_list_size(), 1);
  EXPECT_EQ(scan->option_list(0)->name(), "use_longest_match");
  EXPECT_TRUE(scan->option_list(0)->value()->type()->IsBool());
  EXPECT_TRUE(scan->option_list(0)->value()->Is<ResolvedLiteral>());
  EXPECT_TRUE(scan->option_list(0)
                  ->value()
                  ->GetAs<ResolvedLiteral>()
                  ->value()
                  .bool_value());
}

TEST_F(TestPatternResolverTest, LongestMatchModeAsNamedParam) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern(
          "A B", {.longest_match_mode_sql = "@p",
                  .parameters = QueryParametersMap{{"p", types::BoolType()}}}));

  EXPECT_EQ(scan->option_list_size(), 1);
  EXPECT_EQ(scan->option_list(0)->name(), "use_longest_match");
  EXPECT_TRUE(scan->option_list(0)->value()->type()->IsBool());
  EXPECT_TRUE(scan->option_list(0)->value()->Is<ResolvedParameter>());
  EXPECT_EQ(scan->option_list(0)->value()->GetAs<ResolvedParameter>()->name(),
            "p");
}

TEST_F(TestPatternResolverTest, LongestMatchModeAsPositionalParam) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern(
          "A B", {.longest_match_mode_sql = "?",
                  .parameters = std::vector<const Type*>{types::BoolType()}}));

  EXPECT_EQ(scan->option_list_size(), 1);
  EXPECT_EQ(scan->option_list(0)->name(), "use_longest_match");
  EXPECT_TRUE(scan->option_list(0)->value()->type()->IsBool());
  EXPECT_TRUE(scan->option_list(0)->value()->Is<ResolvedParameter>());
  EXPECT_EQ(
      scan->option_list(0)->value()->GetAs<ResolvedParameter>()->position(), 1);
}

}  // namespace googlesql::functions::match_recognize
