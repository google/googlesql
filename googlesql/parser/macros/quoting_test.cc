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

#include "googlesql/parser/macros/quoting.h"

#include <tuple>

#include "googlesql/base/testing/status_matchers.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

using ::testing::AllOf;
using ::testing::Combine;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Not;
using ::testing::Property;
using ::testing::Values;
using ::absl_testing::IsOk;

MATCHER_P(QuotingMatches, quoting, "") {
  return ExplainMatchResult(
      AllOf(Property(&QuotingSpec::literal_kind, Eq(quoting.literal_kind())),
            Property(&QuotingSpec::quote_kind, Eq(quoting.quote_kind())),
            Property(&QuotingSpec::prefix, Eq(quoting.prefix()))),
      arg, result_listener);
}

MATCHER_P2(HasQuotingAndContents, expected_quoting, expected_contents, "") {
  absl::string_view actual_contents;
  absl::StatusOr<QuotingSpec> actual_quoting =
      QuotingSpec::FindQuotingKind(arg, actual_contents);

  if (!ExplainMatchResult(IsOk(), actual_quoting.status(), result_listener)) {
    return false;
  }
  return ExplainMatchResult(QuotingMatches(expected_quoting),
                            actual_quoting.value(), result_listener) &&
         ExplainMatchResult(Eq(expected_contents), actual_contents,
                            result_listener);
}

MATCHER_P4(MatchesQuotingAndContents, expected_token_kind, expected_quote,
           expected_prefix, expected_contents, "") {
  absl::string_view actual_contents;
  absl::StatusOr<QuotingSpec> actual_quoting =
      QuotingSpec::FindQuotingKind(arg, actual_contents);

  if (!ExplainMatchResult(IsOk(), actual_quoting.status(), result_listener)) {
    return false;
  }
  return ExplainMatchResult(
             AllOf(
                 Property(&QuotingSpec::literal_kind, Eq(expected_token_kind)),
                 Property(&QuotingSpec::quote_kind, Eq(expected_quote)),
                 Property(&QuotingSpec::prefix, Eq(expected_prefix))),
             actual_quoting.value(), result_listener) &&
         ExplainMatchResult(Eq(expected_contents), actual_contents,
                            result_listener);
}

TEST(QuotingTest, DetectsInvalidQuotingTokens) {
  // None of these are literal tokens
  absl::string_view contents;
  EXPECT_THAT(QuotingSpec::FindQuotingKind("abc", contents), Not(IsOk()));
  EXPECT_THAT(QuotingSpec::FindQuotingKind("+", contents), Not(IsOk()));
  EXPECT_THAT(QuotingSpec::FindQuotingKind("-", contents), Not(IsOk()));
}

TEST(QuotingTest, DetectsCorrectQuotingFromBacktickedIdentifiers) {
  EXPECT_THAT("`a`",
              HasQuotingAndContents(QuotingSpec::BacktickedIdentifier(), "a"));
  EXPECT_THAT(
      "`unic😀de`",
      HasQuotingAndContents(QuotingSpec::BacktickedIdentifier(), "unic😀de"));
}

class QuotingStringLiteralTest
    : public ::testing::TestWithParam<
          std::tuple</*prefix*/ absl::string_view,
                     /*contents*/ absl::string_view,
                     /*expected_literal_kind*/ LiteralTokenKind>> {};

TEST_P(QuotingStringLiteralTest, DetectsCorrectQuotingFromLiterals) {
  const auto& [prefix, contents, expected_literal_kind] = GetParam();
  EXPECT_THAT(
      absl::StrFormat("%s'%s'", prefix, contents),
      MatchesQuotingAndContents(expected_literal_kind,
                                QuoteKind::kOneSingleQuote, prefix, contents));
  EXPECT_THAT(
      absl::StrFormat("%s\"%s\"", prefix, contents),
      MatchesQuotingAndContents(expected_literal_kind,
                                QuoteKind::kOneDoubleQuote, prefix, contents));
  EXPECT_THAT(absl::StrFormat("%s'''%s'''", prefix, contents),
              MatchesQuotingAndContents(expected_literal_kind,
                                        QuoteKind::kThreeSingleQuotes, prefix,
                                        contents));
  EXPECT_THAT(absl::StrFormat(R"(%s"""%s""")", prefix, contents),
              MatchesQuotingAndContents(expected_literal_kind,
                                        QuoteKind::kThreeDoubleQuotes, prefix,
                                        contents));
}

INSTANTIATE_TEST_SUITE_P(StringLiteralQuotingDetectionTest,
                         QuotingStringLiteralTest,
                         Combine(Values("", "r", "R"),
                                 Values("", "a", "xyz 123", "unic😀de"),
                                 Values(LiteralTokenKind::kStringLiteral)));

INSTANTIATE_TEST_SUITE_P(
    RawStringLiteralQuotingDetectionTest_MultilineAndEscapes,
    QuotingStringLiteralTest,
    Combine(Values("r", "R"),
            Values(R"(line1  unic😀de
                      line2)",
                   "\n'\\\"\"\r"),
            Values(LiteralTokenKind::kStringLiteral)));

INSTANTIATE_TEST_SUITE_P(BytesLiteralQuotingDetectionTest,
                         QuotingStringLiteralTest,
                         Combine(Values("b", "B", "rb", "rB", "Rb", "RB", "br",
                                        "bR", "Br", "BR"),
                                 Values("", "a", "xyz 123", "unic😀de"),
                                 Values(LiteralTokenKind::kBytesLiteral)));

INSTANTIATE_TEST_SUITE_P(
    RawBytesLiteralQuotingDetectionTest_MultilineAndEscapes,
    QuotingStringLiteralTest,
    Combine(Values("rb", "rB", "Rb", "RB", "br", "bR", "Br", "BR"),
            Values(R"(line1  unic😀de
                      line2)",
                   "\n'\\\"\"\r"),
            Values(LiteralTokenKind::kBytesLiteral)));

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
