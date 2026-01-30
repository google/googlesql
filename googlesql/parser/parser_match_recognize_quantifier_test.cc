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

#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/proto/logging.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace googlesql {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Combine;
using ::testing::DescribeMatcher;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Values;
using ::testing::ValuesIn;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

static std::vector<absl::string_view> GetOperands() {
  return {"()", "a", "(a)", "`a`", "(a b)", "(a | b)"};
}

static std::vector<absl::string_view> GetPossibleBounds() {
  return {"1", "@p", "?"};
}

static std::vector<std::string> GetValidGreedyBoundedQuantifiers() {
  std::vector<absl::string_view> bounds = GetPossibleBounds();

  std::vector<std::string> result;
  const size_t N = bounds.size();

  // N fixed bounds, e.g. {1}, as well as (N+1)^2 for 2-bounded quantifiers,
  // each side allowing a possibly empty bound, hence the +1.
  result.reserve(N + (N + 1) * (N + 1));

  for (absl::string_view bound : GetPossibleBounds()) {
    result.push_back(absl::Substitute("{$0}", bound));
  }

  // For optional bounds, such as the upper one in `{1, }`, or both in `{,}`
  bounds.push_back("");

  for (absl::string_view lower_bound : GetPossibleBounds()) {
    for (absl::string_view upper_bound : GetPossibleBounds()) {
      result.push_back(absl::Substitute("{$0, $1}", lower_bound, upper_bound));
    }
  }
  return result;
}

static std::vector<std::string> GetValidGreedyQuantifiers() {
  std::vector<std::string> result = GetValidGreedyBoundedQuantifiers();
  result.push_back("?");
  result.push_back("+");
  result.push_back("*");
  return result;
}

static LanguageOptions GetLanguageOptions() {
  LanguageOptions options;
  GOOGLESQL_CHECK_OK(options.EnableReservableKeyword("MATCH_RECOGNIZE"));
  return options;
}

MATCHER_P(ProducesError, error_matcher,
          absl::StrCat("query which when parsed produces an error that ",
                       DescribeMatcher<std::string>(error_matcher, negation))) {
  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView(arg);
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  absl::Status parse_status =
      ParseNextStatement(&resume_location, ParserOptions(GetLanguageOptions()),
                         &parser_output, &at_end_of_input);
  *result_listener << "parse_status: " << parse_status << ", ";
  return ExplainMatchResult(AllOf(Not(IsOk()), StatusIs(_, error_matcher)),
                            parse_status, result_listener);
}

// Ensures that the query built from the template above parses correctly.
MATCHER_P(IsMatchRecognizeQueryWithPattern, pattern_matcher,
          absl::StrCat("query with a single FROM t with a MATCH_RECOGNIZE "
                       "clause whose pattern ",
                       DescribeMatcher<const ASTRowPatternExpression*>(
                           pattern_matcher, negation))) {
  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView(arg);
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  absl::Status parse_status =
      ParseNextStatement(&resume_location, ParserOptions(GetLanguageOptions()),
                         &parser_output, &at_end_of_input);
  if (!ExplainMatchResult(IsOk(), parse_status, result_listener)) {
    *result_listener << "Failed to parse query: " << parse_status << ", ";
    return false;
  }

  if (!ExplainMatchResult(IsTrue(), at_end_of_input, result_listener)) {
    *result_listener << "Not at end of input, ";
    return false;
  }

  if (!ExplainMatchResult(IsTrue(), parser_output->statement() != nullptr,
                          result_listener)) {
    *result_listener << "Not a statement: "
                     << parser_output->node()->DebugString() << ", ";
    return false;
  }

  const auto* query_stmt =
      parser_output->statement()->GetAsOrNull<ASTQueryStatement>();
  if (!ExplainMatchResult(IsTrue(), query_stmt != nullptr, result_listener)) {
    *result_listener << "Not a query statement: "
                     << parser_output->statement()->DebugString();
    return false;
  }

  const auto* query =
      query_stmt->query()->query_expr()->GetAsOrNull<ASTSelect>();
  if (!ExplainMatchResult(IsTrue(), query != nullptr, result_listener)) {
    *result_listener << "Not a SELECT query: " << query_stmt->DebugString()
                     << ", ";
    return false;
  }

  const auto* from_clause = query->from_clause();
  if (!ExplainMatchResult(IsTrue(), from_clause != nullptr, result_listener)) {
    *result_listener << "No from clause: " << query->DebugString() << ", ";
    return false;
  }
  const auto* table_expr =
      from_clause->table_expression()->GetAsOrNull<ASTTablePathExpression>();
  if (!ExplainMatchResult(IsTrue(), table_expr != nullptr, result_listener)) {
    *result_listener << "Not a table path expression: "
                     << from_clause->table_expression()->DebugString();
    return false;
  }
  if (!ExplainMatchResult(Eq(1), table_expr->postfix_operators().size(),
                          result_listener)) {
    *result_listener << "Expected exactly 1 postfix operator: "
                     << from_clause->table_expression()->DebugString();
    return false;
  }

  const auto* match_recognize_clause =
      table_expr->postfix_operators(0)->GetAsOrNull<ASTMatchRecognizeClause>();
  if (!ExplainMatchResult(IsTrue(), match_recognize_clause != nullptr,
                          result_listener)) {
    *result_listener << "Not a match recognize clause: "
                     << table_expr->postfix_operators(0)->DebugString();
    return false;
  }

  return ExplainMatchResult(pattern_matcher, match_recognize_clause->pattern(),
                            result_listener);
}

MATCHER_P(IsQuantification, quantifier_matcher,
          absl::StrCat("quantification whose quantifier ",
                       DescribeMatcher<const ASTQuantifier*>(quantifier_matcher,
                                                             negation))) {
  const auto* quantification =
      arg->template GetAsOrNull<ASTRowPatternQuantification>();
  return ExplainMatchResult(IsTrue(), quantification != nullptr,
                            result_listener) &&
         ExplainMatchResult(quantifier_matcher, quantification->quantifier(),
                            result_listener);
}

MATCHER_P(Reluctant, is_reluctant,
          absl::StrCat("ASTQuantifier that is ", negation ? "not " : "",
                       "reluctant")) {
  return ExplainMatchResult(Eq(is_reluctant), arg->is_reluctant(),
                            result_listener);
}

MATCHER_P2(HasByteOffsets, start_offset, end_offset, "") {
  auto range = arg->GetParseLocationRange();
  if (!ExplainMatchResult(Eq(start_offset), range.start().GetByteOffset(),
                          result_listener)) {
    *result_listener << "Expected start byte offset " << start_offset
                     << " but got " << range.start().GetByteOffset();
    return false;
  }

  if (!ExplainMatchResult(Eq(end_offset), range.end().GetByteOffset(),
                          result_listener)) {
    *result_listener << "Expected end byte offset " << end_offset << " but got "
                     << range.end().GetByteOffset();
    return false;
  }

  return true;
}

constexpr absl::string_view kQueryTemplate = R"(
  SELECT * FROM t MATCH_RECOGNIZE(
    ORDER BY k
    MEASURES min(x) AS m
    PATTERN( $0$1$2$3 )
    DEFINE a AS true
  )
)";

// Args are: operand, separator, quantifier, reluctant
class ValidQuantifierTest : public ::testing::TestWithParam<
                                std::tuple<absl::string_view, absl::string_view,
                                           std::string, absl::string_view>> {};

TEST_P(ValidQuantifierTest, TestValidQuantifiers) {
  absl::string_view operand = std::get<0>(GetParam());
  absl::string_view separator = std::get<1>(GetParam());
  absl::string_view quantifier = std::get<2>(GetParam());
  absl::string_view reluctant = std::get<3>(GetParam());

  size_t length = operand.length() + separator.length() + quantifier.length() +
                  reluctant.length();

  std::string query = absl::Substitute(kQueryTemplate, operand, separator,
                                       quantifier, reluctant);

  if (!reluctant.empty() &&
      (quantifier == "{1}" || quantifier == "{?}" || quantifier == "{@p}")) {
    // Special case: exact quantifiers cannot be marked as reluctant.
    EXPECT_THAT(query, ProducesError(HasSubstr("Syntax error")));
  } else {
    // 89 is the byte offset of the pattern expr in the query template.
    EXPECT_THAT(query, IsMatchRecognizeQueryWithPattern(AllOf(
                           IsQuantification(Reluctant(!reluctant.empty())),
                           HasByteOffsets(89, 89 + length))));
  }
}

INSTANTIATE_TEST_SUITE_P(ValidQuantifierTest, ValidQuantifierTest,
                         Combine(ValuesIn(GetOperands()), Values("", " "),
                                 ValuesIn(GetValidGreedyQuantifiers()),
                                 Values("", "?", " ?")));

// Args are: operand, separator, quantifier, reluctant
class InvalidQuantifierTest
    : public ::testing::TestWithParam<
          std::tuple<absl::string_view, absl::string_view, std::string,
                     absl::string_view>> {};

TEST_P(InvalidQuantifierTest, TestInvalidQuantifiers) {
  absl::string_view operand = std::get<0>(GetParam());
  absl::string_view separator = std::get<1>(GetParam());
  absl::string_view quantifier = std::get<2>(GetParam());
  absl::string_view reluctant = std::get<3>(GetParam());

  std::string query = absl::Substitute(kQueryTemplate, operand, separator,
                                       quantifier, reluctant);

  // Special case: exact quantifiers cannot be marked as reluctant.
  EXPECT_THAT(query, ProducesError(HasSubstr("Syntax error")));
}

// Adding a double "??" for reluctant is always a syntax error
INSTANTIATE_TEST_SUITE_P(DoubleReluctantMarkers, InvalidQuantifierTest,
                         Combine(ValuesIn(GetOperands()), Values("", " "),
                                 ValuesIn(GetValidGreedyQuantifiers()),
                                 Values("??", "? ?", " ? ?", "+", "*")));

INSTANTIATE_TEST_SUITE_P(InvalidBoundedQuantifiers, InvalidQuantifierTest,
                         Combine(ValuesIn(GetOperands()), Values("", " "),
                                 Values("{}", "{,,}", "{@p, 1, 1}"),
                                 Values("", "+", "*", "3")));

}  // namespace googlesql
