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

#include "googlesql/parser/macros/token_provider.h"

#include <optional>
#include <ostream>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/parse_location.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

using ::testing::_;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::FieldsAre;
using ::absl_testing::IsOk;

// Template specialization to print tokens in failed test messages.
static void PrintTo(const TokenWithLocation& token, std::ostream* os) {
  *os << absl::StrFormat(
      "(kind: %i, location: %s, topmost_invocation_location:%s, text: '%s', "
      "prev_spaces: '%s')",
      token.kind, token.location.GetString(),
      token.topmost_invocation_location.GetString(), token.text,
      token.preceding_whitespaces);
}

MATCHER_P(IsOkAndHoldsToken, expected, "") {
  return ExplainMatchResult(IsOk(), arg, result_listener) &&
         ExplainMatchResult(
             FieldsAre(Eq(expected.kind), Eq(expected.location),
                       Eq(expected.text), Eq(expected.preceding_whitespaces),
                       Eq(expected.topmost_invocation_location),
                       _ /* parent */),
             arg.value(), result_listener);
}

static absl::string_view kFileName = "<filename>";

static ParseLocationRange MakeLocation(int start_offset, int end_offset) {
  ParseLocationRange location;
  location.set_start(
      ParseLocationPoint::FromByteOffset(kFileName, start_offset));
  location.set_end(ParseLocationPoint::FromByteOffset(kFileName, end_offset));
  return location;
}

static TokenProvider MakeTokenProvider(absl::string_view input) {
  return TokenProvider(kFileName, input,
                       /*start_offset=*/0, /*end_offset=*/std::nullopt,
                       /*offset_in_original_input=*/0);
}

TEST(TokenProviderTest, RawTokenizerMode) {
  absl::string_view input = "/*comment*/ 123";

  TokenProvider token_provider = MakeTokenProvider(input);
  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(TokenWithLocation{
                  .kind = Token::COMMENT,
                  .location = MakeLocation(0, 11),
                  .text = "/*comment*/",
                  .preceding_whitespaces = "",
              }));

  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(TokenWithLocation{
                  .kind = Token::DECIMAL_INTEGER_LITERAL,
                  .location = MakeLocation(12, 15),
                  .text = "123",
                  .preceding_whitespaces = " ",
              }));
}

TEST(TokenProviderTest, AlwaysEndsWithEOF) {
  absl::string_view input = "\t\t";
  TokenProvider token_provider = MakeTokenProvider(input);
  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(TokenWithLocation{
                  .kind = Token::EOI,
                  .location = MakeLocation(2, 2),
                  .text = "",
                  .preceding_whitespaces = "\t\t",
              }));
  // No preceding whitespaces for the second EOF.
  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(TokenWithLocation{
                  .kind = Token::EOI,
                  .location = MakeLocation(2, 2),
                  .text = "",
                  .preceding_whitespaces = "",
              }));
}

TEST(TokenProviderTest, CanPeekToken) {
  absl::string_view input = "\t123 identifier";
  TokenProvider token_provider = MakeTokenProvider(input);
  const TokenWithLocation int_token{
      .kind = Token::DECIMAL_INTEGER_LITERAL,
      .location = MakeLocation(1, 4),
      .text = "123",
      .preceding_whitespaces = "\t",
  };
  EXPECT_THAT(token_provider.PeekNextToken(), IsOkAndHoldsToken(int_token));
  EXPECT_THAT(token_provider.PeekNextToken(), IsOkAndHoldsToken(int_token));
  EXPECT_THAT(token_provider.ConsumeNextToken(), IsOkAndHoldsToken(int_token));

  const TokenWithLocation identifier_token{
      .kind = Token::IDENTIFIER,
      .location = MakeLocation(5, 15),
      .text = "identifier",
      .preceding_whitespaces = " ",
  };
  EXPECT_THAT(token_provider.PeekNextToken(),
              IsOkAndHoldsToken(identifier_token));
  EXPECT_THAT(token_provider.PeekNextToken(),
              IsOkAndHoldsToken(identifier_token));
  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(identifier_token));
}

TEST(TokenProviderTest, TracksCountOfConsumedTokensIncludingEOF) {
  absl::string_view input = "SELECT";
  TokenProvider token_provider = MakeTokenProvider(input);

  TokenWithLocation first_token{
      .kind = Token::KW_SELECT,
      .location = MakeLocation(0, 6),
      .text = "SELECT",
      .preceding_whitespaces = "",
  };

  EXPECT_THAT(token_provider.PeekNextToken(), IsOkAndHoldsToken(first_token));
  EXPECT_EQ(token_provider.num_consumed_tokens(), 0);

  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(first_token));
  EXPECT_EQ(token_provider.num_consumed_tokens(), 1);

  TokenWithLocation second_token{
      .kind = Token::EOI,
      .location = MakeLocation(6, 6),
      .text = "",
      .preceding_whitespaces = "",
  };

  EXPECT_THAT(token_provider.PeekNextToken(), IsOkAndHoldsToken(second_token));
  EXPECT_EQ(token_provider.num_consumed_tokens(), 1);

  EXPECT_THAT(token_provider.ConsumeNextToken(),
              IsOkAndHoldsToken(second_token));
  EXPECT_EQ(token_provider.num_consumed_tokens(), 2);
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
