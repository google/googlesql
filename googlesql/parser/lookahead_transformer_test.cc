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

#include "googlesql/parser/lookahead_transformer.h"

#include <memory>
#include <optional>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::parser {

using ::testing::ContainerEq;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

using Location = ParseLocationRange;

// This class is a friend of the tokenizer so that it can help us test the
// private API.
class TokenTestThief {
 public:
  static Token Lookahead1(LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.Lookahead1();
  }
  static Token Lookahead2(LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.Lookahead2();
  }
  static Token Lookahead3(LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.Lookahead3();
  }
  static Token Lookback1(LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.Lookback1();
  }
  static Token Lookback2(LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.Lookback2();
  }

  static std::optional<TokenWithOverrideError> GetCurrentToken(
      const LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.current_token_;
  }

  static std::optional<TokenWithOverrideError> GetPreviousToken(
      const LookaheadTransformer& lookahead_transformer) {
    return lookahead_transformer.lookback_1_;
  }
};

class LookaheadTransformerTest : public ::testing::Test {
 public:
  std::vector<Token> GetAllTokens(ParserMode mode, absl::string_view sql) {
    StackFrame::StackFrameFactory stack_frame_factory;
    auto tokenizer = LookaheadTransformer::Create(
        mode, "fake_file", sql, 0, options_, MacroExpansionMode::kNone,
        /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory);
    GOOGLESQL_DCHECK_OK(tokenizer);
    Location location;
    std::vector<Token> tokens;
    do {
      absl::string_view text;
      tokens.emplace_back(tokenizer.value()->GetNextToken(&text, &location));
    } while (tokens.back() != Token::EOI);
    return tokens;
  }

 protected:
  LanguageOptions options_;
};

TEST_F(LookaheadTransformerTest, ParameterKeywordStatementMode) {
  EXPECT_THAT(GetAllTokens(ParserMode::kStatement, "a @select c"),
              ElementsAre(Token::IDENTIFIER, Token::ATSIGN, Token::IDENTIFIER,
                          Token::IDENTIFIER, Token::EOI));
}

TEST_F(LookaheadTransformerTest, ParameterKeywordTokenizerMode) {
  EXPECT_THAT(GetAllTokens(ParserMode::kTokenizer, "a @select c"),
              ElementsAre(Token::IDENTIFIER, Token::ATSIGN, Token::IDENTIFIER,
                          Token::IDENTIFIER, Token::EOI));
}

TEST_F(LookaheadTransformerTest, SysvarKeywordStatementMode) {
  EXPECT_THAT(GetAllTokens(ParserMode::kStatement, "a @@where c"),
              ElementsAre(Token::IDENTIFIER, Token::KW_DOUBLE_AT,
                          Token::IDENTIFIER, Token::IDENTIFIER, Token::EOI));
}

TEST_F(LookaheadTransformerTest, SysvarKeywordTokenizerMode) {
  EXPECT_THAT(GetAllTokens(ParserMode::kTokenizer, "a @@where c"),
              ElementsAre(Token::IDENTIFIER, Token::KW_DOUBLE_AT,
                          Token::IDENTIFIER, Token::IDENTIFIER, Token::EOI));
}

TEST_F(LookaheadTransformerTest, QueryParamCurrentDate) {
  EXPECT_THAT(GetAllTokens(ParserMode::kStatement, "a @current_date c"),
              ElementsAre(Token::IDENTIFIER, Token::ATSIGN, Token::IDENTIFIER,
                          Token::IDENTIFIER, Token::EOI));
}

TEST_F(LookaheadTransformerTest, SysvarWithDotId) {
  EXPECT_THAT(GetAllTokens(ParserMode::kStatement, "SELECT @@ORDER.WITH.c"),
              ElementsAre(Token::KW_SELECT, Token::KW_DOUBLE_AT,
                          Token::IDENTIFIER, Token::DOT, Token::IDENTIFIER,
                          Token::DOT, Token::IDENTIFIER, Token::EOI));
}

absl::StatusOr<Token> GetNextToken(LookaheadTransformer& tokenizer,
                                   Location& location) {
  Token token_kind;
  GOOGLESQL_RETURN_IF_ERROR(tokenizer.GetNextToken(&location, &token_kind));
  return token_kind;
}

TEST_F(LookaheadTransformerTest, Lookahead1) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "a 1 SELECT", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));
  Location location;
  LookaheadTransformer& tokenizer = *lexer;
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::IDENTIFIER);
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::IDENTIFIER);
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::IDENTIFIER);

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::IDENTIFIER));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer),
            Token::DECIMAL_INTEGER_LITERAL);

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::INTEGER_LITERAL));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::KW_SELECT);

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::KW_SELECT));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);

  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));

  // Then even after YYEOF
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
}

TEST_F(LookaheadTransformerTest, Lookahead2) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "a 1 SELECT", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::IDENTIFIER);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer),
            Token::DECIMAL_INTEGER_LITERAL);

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::IDENTIFIER));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer),
            Token::DECIMAL_INTEGER_LITERAL);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::KW_SELECT);

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::INTEGER_LITERAL));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::KW_SELECT);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::EOI);

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::KW_SELECT));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::EOI);

  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::EOI);
}

TEST_F(LookaheadTransformerTest, Lookahead2BeforeLookahead1) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "a 1 SELECT", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  // Calling Lookahead2 before Lookahead1 returns the correct token.
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer),
            Token::DECIMAL_INTEGER_LITERAL);
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::IDENTIFIER);

  // Repeated calling Lookahead2 returns the same token.
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer),
            Token::DECIMAL_INTEGER_LITERAL);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer),
            Token::DECIMAL_INTEGER_LITERAL);
}

TEST_F(LookaheadTransformerTest, Lookahead2NoEnoughTokens) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto lexer, LookaheadTransformer::Create(
                                       ParserMode::kStatement, "fake_file", "",
                                       0, options_, MacroExpansionMode::kNone,
                                       /*macro_catalog=*/nullptr,
                                       /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::EOI);

  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::EOI);

  // Fetching more tokens returns YYEOF.
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_EQ(TokenTestThief::Lookahead2(tokenizer), Token::EOI);
  EXPECT_EQ(TokenTestThief::Lookahead1(tokenizer), Token::EOI);
}

TEST_F(LookaheadTransformerTest, LookaheadTransformerReturnsYyeofWhenErrors) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT * EXCEPT 1", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::KW_SELECT));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::MULT));

  Token token_kind;
  GOOGLESQL_EXPECT_OK(tokenizer.GetNextToken(&location, &token_kind));
  EXPECT_EQ(token_kind, Token::KW_EXCEPT_IN_UNEXPECTED_CONTEXT);
}

TEST_F(LookaheadTransformerTest, IsEoiTestBasic) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT 1; SELECT 2", 0,
          options_, MacroExpansionMode::kNone, /*macro_catalog=*/nullptr,
          /*arena=*/nullptr, stack_frame_factory));
  EXPECT_FALSE(lexer->IsAtEoi());

  ParseLocationRange loc;
  int i = 0;
  for (Token token :
       {Token::KW_SELECT, Token::INTEGER_LITERAL, Token::SEMICOLON,
        Token::KW_SELECT, Token::INTEGER_LITERAL, Token::EOI}) {
    EXPECT_THAT(GetNextToken(*lexer, loc), IsOkAndHolds(token));
    EXPECT_EQ(lexer->IsAtEoi(), ++i >= 5) << i;
  }
}

TEST_F(LookaheadTransformerTest, IsEoiTestBasicTrailingSemicolon) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT 1; SELECT 2;", 0,
          options_, MacroExpansionMode::kNone, /*macro_catalog=*/nullptr,
          /*arena=*/nullptr, stack_frame_factory));
  ParseLocationRange loc;
  int i = 0;
  for (Token token : {Token::KW_SELECT, Token::INTEGER_LITERAL,
                      Token::SEMICOLON, Token::KW_SELECT,
                      Token::INTEGER_LITERAL, Token::SEMICOLON, Token::EOI}) {
    EXPECT_THAT(GetNextToken(*lexer, loc), IsOkAndHolds(token));
    // IsEoi() returns true both for the last semicolon or for the real eof
    // token.
    EXPECT_EQ(lexer->IsAtEoi(), ++i >= 6) << i;
    ;
  }
}

TEST_F(LookaheadTransformerTest, IsEoiTestTokenError) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT * 'abc", 0, options_,
          MacroExpansionMode::kNone, /*macro_catalog=*/nullptr,
          /*arena=*/nullptr, stack_frame_factory));
  ParseLocationRange loc;

  EXPECT_THAT(GetNextToken(*lexer, loc), IsOkAndHolds(Token::KW_SELECT));
  EXPECT_THAT(GetNextToken(*lexer, loc), IsOkAndHolds(Token::MULT));

  Token token_kind;
  absl::Status status = lexer->GetNextToken(&loc, &token_kind);
  EXPECT_FALSE(lexer->IsAtEoi());
}

static void AdvanceLexer(LookaheadTransformer& tokenizer, Location& location) {
  absl::string_view unused;
  tokenizer.GetNextToken(&unused, &location);
}

MATCHER_P2(TokenIs, expected_kind, status_matcher, "") {
  return ExplainMatchResult(
      Optional(FieldsAre(Field(&TokenWithLocation::kind, Eq(expected_kind)),
                         Eq(Token::UNAVAILABLE), status_matcher)),
      arg, result_listener);
}

MATCHER_P(TokenIs, expected_kind, "") {
  return ExplainMatchResult(TokenIs(expected_kind, Eq(absl::OkStatus())), arg,
                            result_listener);
}

TEST_F(LookaheadTransformerTest, LookaheadTransformerHasCorrectPrevToken) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT 1 FULL UNION ALL", 0,
          options_, MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer),
              TokenIs(Token::KW_SELECT));

  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer),
              TokenIs(Token::INTEGER_LITERAL));

  // The stored token should be KW_FULL_IN_SET_OP rather than KW_FULL.
  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer),
              TokenIs(Token::KW_FULL_IN_SET_OP));
}

TEST_F(LookaheadTransformerTest,
       LookaheadTransformerHasCorrectPrevTokenAndError) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT * 'abc 1", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer),
              TokenIs(Token::KW_SELECT));

  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer), TokenIs(Token::MULT));

  constexpr absl::string_view error_message =
      R"(Syntax error: Unclosed string literal)";

  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer),
              TokenIs(Token::EOI, StatusIs(absl::StatusCode::kInvalidArgument,
                                           HasSubstr(error_message))));

  // Further advancing the lexer returns the same token and error.
  AdvanceLexer(tokenizer, location);
  EXPECT_THAT(TokenTestThief::GetCurrentToken(tokenizer),
              TokenIs(Token::EOI, StatusIs(absl::StatusCode::kInvalidArgument,
                                           HasSubstr(error_message))));
}

constexpr int kFurtherLookaheadBeyondEof = 5;

// Keep calling GetNextToken after YYEOF is returned with no errors.
TEST_F(LookaheadTransformerTest,
       LookaheadTransformerGetNextTokenAfterEofNoError) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT *", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::KW_SELECT));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::MULT));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));

  // Keep calling GetNextToken should always return YYEOF.
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  }
}

// Keep calling GetNextToken after YYEOF is returned with errors.
TEST_F(LookaheadTransformerTest,
       LookaheadTransformerGetNextTokenAfterEofWithError) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "SELECT * 'abc 1", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetNextToken(tokenizer, location),
              IsOkAndHolds(Token::KW_SELECT));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::MULT));

  constexpr absl::string_view error_message =
      R"(Syntax error: Unclosed string literal)";
  EXPECT_THAT(
      GetNextToken(tokenizer, location),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(error_message)));

  // Keep calling GetNextToken should always return the same error.
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    EXPECT_THAT(
        GetNextToken(tokenizer, location),
        StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(error_message)));
  }
}

TEST_F(LookaheadTransformerTest,
       LookaheadTransformerOverrideLookbackInvalidAnduselessCalls) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kScript, "fake_file", "BEGIN BEGIN END END", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));
  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(tokenizer.OverrideNextTokenLookback(
                  /*parser_lookahead_is_empty=*/true, Token::KW_BEGIN,
                  Token::LB_BEGIN_AT_STATEMENT_START),
              StatusIs(absl::StatusCode::kInternal));

  EXPECT_THAT(tokenizer.OverrideNextTokenLookback(
                  /*parser_lookahead_is_empty=*/false, Token::KW_BEGIN,
                  Token::LB_BEGIN_AT_STATEMENT_START),
              StatusIs(absl::StatusCode::kInternal));

  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::UNAVAILABLE));
  // Signal a lookback override of an irrelevant token. This should be ignored
  GOOGLESQL_ASSERT_OK(tokenizer.OverrideNextTokenLookback(
      /*parser_lookahead_is_empty=*/true, Token::KW_LOOP,
      Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(TokenTestThief::Lookahead1(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_BEGIN));
  // Signal a lookback override of an irrelevant token. This should be ignored
  GOOGLESQL_ASSERT_OK(tokenizer.OverrideNextTokenLookback(
      /*parser_lookahead_is_empty=*/false, Token::KW_LOOP,
      Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(TokenTestThief::Lookahead1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookahead1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::KW_END));
}

TEST_F(LookaheadTransformerTest,
       LookaheadTransformerOverrideLookbackForEmptyParserLA) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kScript, "fake_file", "BEGIN BEGIN END END", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::UNAVAILABLE));
  // Now signal that the next token (the second BEGIN) is the first token in a
  // statement.
  GOOGLESQL_ASSERT_OK(tokenizer.OverrideNextTokenLookback(
      /*parser_lookahead_is_empty=*/true, Token::KW_BEGIN,
      Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(TokenTestThief::Lookahead1(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(TokenTestThief::Lookahead1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_END));
  // This is where we first see the effect of the statement start.
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer),
              Eq(Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookahead1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_END));
  // And this is where we first see the effect in lookback2
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer),
              Eq(Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback2(tokenizer), Eq(Token::KW_END));
}

TEST_F(LookaheadTransformerTest,
       LookaheadTransformerOverrideLookbackForNonEmptyParserLA) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kScript, "fake_file", "BEGIN BEGIN END END", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::UNAVAILABLE));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_BEGIN));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::UNAVAILABLE));
  // Now signal that the previously consumed token (the first BEGIN) is the
  // first token in statement.
  GOOGLESQL_ASSERT_OK(tokenizer.OverrideNextTokenLookback(
      /*parser_lookahead_is_empty=*/false, Token::KW_BEGIN,
      Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_BEGIN));
  // This is where we first see the effect of the statement start.
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer),
              Eq(Token::LB_BEGIN_AT_STATEMENT_START));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_BEGIN));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::KW_END));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_END));
  EXPECT_THAT(GetNextToken(tokenizer, location), IsOkAndHolds(Token::EOI));
  EXPECT_THAT(TokenTestThief::Lookback1(tokenizer), Eq(Token::KW_END));
}

TEST_F(LookaheadTransformerTest,
       LookaheadTransformerIdentifyingStartOfExplainExplian) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "EXPLAIN EXPLAIN SELECT 1", 0,
          options_, MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));

  Location location;
  std::vector<Token> seen_tokens, seen_lookbacks;
  do {
    seen_tokens.push_back(*GetNextToken(*lexer, location));
    seen_lookbacks.push_back(TokenTestThief::Lookback1(*lexer));
  } while (seen_lookbacks.back() != Token::EOI);
  EXPECT_THAT(seen_tokens, ContainerEq(std::vector<Token>{
                               Token::KW_EXPLAIN,
                               Token::KW_EXPLAIN,
                               Token::KW_SELECT,
                               Token::INTEGER_LITERAL,
                               Token::EOI,
                               Token::EOI,
                           }));
  EXPECT_THAT(seen_lookbacks, ContainerEq(std::vector<Token>{
                                  Token::UNAVAILABLE,
                                  Token::LB_EXPLAIN_SQL_STATEMENT,
                                  Token::LB_EXPLAIN_SQL_STATEMENT,
                                  Token::KW_SELECT,
                                  Token::INTEGER_LITERAL,
                                  Token::EOI,
                              }));
}

TEST_F(LookaheadTransformerTest,
       LookaheadTransformerIdentifyingStartOfHintExplainHint) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto lexer, LookaheadTransformer::Create(
                                       ParserMode::kStatement, "fake_file",
                                       "@5 EXPLAIN @{a = 1} EXPLAIN SELECT 1",
                                       0, options_, MacroExpansionMode::kNone,
                                       /*macro_catalog=*/nullptr,
                                       /*arena=*/nullptr, stack_frame_factory));

  Location location;
  std::vector<Token> seen_tokens, seen_lookbacks, seen_lookback2s;
  int i = 0;
  do {
    seen_tokens.push_back(*GetNextToken(*lexer, location));
    if (int current = i++; current == 1 || current == 8) {
      // Simulate having parser context to identify end of statement level
      // hints.
      GOOGLESQL_EXPECT_OK(lexer->OverrideCurrentTokenLookback(
          Token::LB_END_OF_STATEMENT_LEVEL_HINT));
    }
    seen_lookbacks.push_back(TokenTestThief::Lookback1(*lexer));
    seen_lookback2s.push_back(TokenTestThief::Lookback2(*lexer));
  } while (seen_lookbacks.back() != Token::EOI);
  EXPECT_THAT(seen_tokens, ContainerEq(std::vector<Token>{
                               Token::KW_OPEN_INTEGER_HINT,
                               Token::INTEGER_LITERAL,
                               Token::KW_EXPLAIN,
                               Token::KW_OPEN_HINT,
                               Token::LBRACE,
                               Token::IDENTIFIER,
                               Token::ASSIGN,
                               Token::INTEGER_LITERAL,
                               Token::RBRACE,
                               Token::KW_EXPLAIN,
                               Token::KW_SELECT,
                               Token::INTEGER_LITERAL,
                               Token::EOI,
                               Token::EOI,
                           }));
  EXPECT_THAT(seen_lookbacks, ContainerEq(std::vector<Token>{
                                  Token::UNAVAILABLE,
                                  Token::KW_OPEN_INTEGER_HINT,
                                  Token::LB_END_OF_STATEMENT_LEVEL_HINT,
                                  Token::LB_EXPLAIN_SQL_STATEMENT,
                                  Token::KW_OPEN_HINT,
                                  Token::LBRACE,
                                  Token::IDENTIFIER,
                                  Token::ASSIGN,
                                  Token::INTEGER_LITERAL,
                                  Token::LB_END_OF_STATEMENT_LEVEL_HINT,
                                  Token::LB_EXPLAIN_SQL_STATEMENT,
                                  Token::KW_SELECT,
                                  Token::INTEGER_LITERAL,
                                  Token::EOI,
                              }));
  EXPECT_THAT(seen_lookback2s, ContainerEq(std::vector<Token>{
                                   Token::UNAVAILABLE,
                                   Token::UNAVAILABLE,
                                   Token::KW_OPEN_INTEGER_HINT,
                                   Token::LB_END_OF_STATEMENT_LEVEL_HINT,
                                   Token::LB_EXPLAIN_SQL_STATEMENT,
                                   Token::KW_OPEN_HINT,
                                   Token::LBRACE,
                                   Token::IDENTIFIER,
                                   Token::ASSIGN,
                                   Token::INTEGER_LITERAL,
                                   Token::LB_END_OF_STATEMENT_LEVEL_HINT,
                                   Token::LB_EXPLAIN_SQL_STATEMENT,
                                   Token::KW_SELECT,
                                   Token::INTEGER_LITERAL,
                               }));
}

static TokenWithOverrideError GetTokenKindAndError(
    LookaheadTransformer& tokenizer) {
  absl::string_view unused_text;
  Location unused_location;
  Token token_kind = tokenizer.GetNextToken(&unused_text, &unused_location);
  return {.token = {.kind = token_kind}, .error = tokenizer.GetOverrideError()};
}

MATCHER_P2(TokenKindIs, expected_kind, status_matcher, "") {
  return ExplainMatchResult(
      FieldsAre(Field(&TokenWithLocation::kind, Eq(expected_kind)),
                Eq(Token::UNAVAILABLE), status_matcher),
      arg, result_listener);
}

MATCHER_P(TokenKindIs, expected_kind, "") {
  return ExplainMatchResult(TokenKindIs(expected_kind, Eq(absl::OkStatus())),
                            arg, result_listener);
}

TEST_F(LookaheadTransformerTest, GetNextTokenContinuesToReturnYyeof) {
  constexpr absl::string_view kInput = "SELECT";
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(ParserMode::kStatement, "fake_file", kInput,
                                   0, options_, MacroExpansionMode::kNone,
                                   /*macro_catalog=*/nullptr,
                                   /*arena=*/nullptr, stack_frame_factory));

  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::KW_SELECT));

  // Fetching more tokens should always return YYEOF with no errors because the
  // last token is YYEOF without errors.
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::EOI));
  }
}

TEST_F(LookaheadTransformerTest, GetNextTokenContinuesToReturnTheSameError) {
  constexpr absl::string_view kInput = "SELECT `";
  constexpr absl::string_view kError =
      "Syntax error: Unclosed identifier literal";

  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(ParserMode::kStatement, "fake_file", kInput,
                                   0, options_, MacroExpansionMode::kNone,
                                   /*macro_catalog=*/nullptr,
                                   /*arena=*/nullptr, stack_frame_factory));
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::KW_SELECT));
  EXPECT_THAT(
      GetTokenKindAndError(tokenizer),
      TokenKindIs(Token::EOI, StatusIs(absl::StatusCode::kInvalidArgument,
                                       HasSubstr(kError))));

  // Fetching more tokens should always return YYEOF with the same error.
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    EXPECT_THAT(
        GetTokenKindAndError(tokenizer),
        TokenKindIs(Token::EOI, StatusIs(absl::StatusCode::kInvalidArgument,
                                         HasSubstr(kError))));
  }
}

MATCHER_P(IsSameOptionalToken, token, "") {
  if (arg.has_value() != token.has_value()) {
    *result_listener << "expected.has_value() = " << token.has_value()
                     << " but actual.has_value() = " << arg.has_value();
    return false;
  }
  if (!arg.has_value()) {
    return true;
  }
  if (!ExplainMatchResult(Eq(token->error), arg->error, result_listener)) {
    return false;
  }
  const TokenWithLocation& expected = token->token;
  return ExplainMatchResult(
      AllOf(Field(&TokenWithLocation::kind, Eq(expected.kind)),
            Field(&TokenWithLocation::location, Eq(expected.location)),
            Field(&TokenWithLocation::text, Eq(expected.text)),
            Field(&TokenWithLocation::preceding_whitespaces,
                  Eq(expected.preceding_whitespaces))),
      arg->token, result_listener);
}

TEST_F(LookaheadTransformerTest, PreviousTokensAreCorrectNoErrors) {
  constexpr absl::string_view kInput = "SELECT 1 *";
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(ParserMode::kStatement, "fake_file", kInput,
                                   0, options_, MacroExpansionMode::kNone,
                                   /*macro_catalog=*/nullptr,
                                   /*arena=*/nullptr, stack_frame_factory));
  LookaheadTransformer& tokenizer = *lexer;

  std::optional<TokenWithOverrideError> previous_current_token;
  do {
    EXPECT_THAT(TokenTestThief::GetPreviousToken(tokenizer),
                IsSameOptionalToken(previous_current_token));
    previous_current_token = TokenTestThief::GetCurrentToken(tokenizer);

    Location unused_location;
    AdvanceLexer(tokenizer, unused_location);
  } while (TokenTestThief::GetCurrentToken(tokenizer)->token.kind !=
           Token::EOI);

  EXPECT_THAT(TokenTestThief::GetPreviousToken(tokenizer),
              IsSameOptionalToken(previous_current_token));

  // The previous tokens should remain YYEOF.
  Location unused_location;
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    AdvanceLexer(tokenizer, unused_location);
    EXPECT_THAT(TokenTestThief::GetPreviousToken(tokenizer),
                TokenIs(Token::EOI));
  }
}

TEST_F(LookaheadTransformerTest, PreviousTokensAreCorrectWithErrors) {
  constexpr absl::string_view kInput = "SELECT `";
  constexpr absl::string_view kError =
      "Syntax error: Unclosed identifier literal";
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(ParserMode::kStatement, "fake_file", kInput,
                                   0, options_, MacroExpansionMode::kNone,
                                   /*macro_catalog=*/nullptr,
                                   /*arena=*/nullptr, stack_frame_factory));
  LookaheadTransformer& tokenizer = *lexer;

  std::optional<TokenWithOverrideError> previous_current_token;
  do {
    EXPECT_THAT(TokenTestThief::GetPreviousToken(tokenizer),
                IsSameOptionalToken(previous_current_token));
    previous_current_token = TokenTestThief::GetCurrentToken(tokenizer);

    Location unused_location;
    AdvanceLexer(tokenizer, unused_location);
  } while (TokenTestThief::GetCurrentToken(tokenizer)->token.kind !=
           Token::EOI);

  EXPECT_THAT(TokenTestThief::GetPreviousToken(tokenizer),
              IsSameOptionalToken(previous_current_token));

  // The previous token of the lookahead_transformer should remain YYEOF with
  // the same error.
  Location unused_location;
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    AdvanceLexer(tokenizer, unused_location);
    EXPECT_THAT(TokenTestThief::GetPreviousToken(tokenizer),
                TokenIs(Token::EOI, StatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr(kError))));
  }
}

TEST_F(LookaheadTransformerTest, TokenFusion) {
  constexpr absl::string_view kInput = ">>";
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(ParserMode::kStatement, "fake_file", kInput,
                                   0, options_, MacroExpansionMode::kNone,
                                   /*macro_catalog=*/nullptr,
                                   /*arena=*/nullptr, stack_frame_factory));
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetTokenKindAndError(tokenizer),
              TokenKindIs(Token::KW_SHIFT_RIGHT));
  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::EOI));
}

TEST_F(LookaheadTransformerTest, TokensWithWhitespacesInBetweenCannotFuse) {
  constexpr absl::string_view kInput = "> >";
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(ParserMode::kStatement, "fake_file", kInput,
                                   0, options_, MacroExpansionMode::kNone,
                                   /*macro_catalog=*/nullptr,
                                   /*arena=*/nullptr, stack_frame_factory));
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::GT));
  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::GT));
  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::EOI));
}

static constexpr absl::string_view kDefsFileName = "defs.sql";

static absl::Status RegisterMacros(absl::string_view source,
                                   const LanguageOptions& language_options,
                                   macros::MacroCatalog& macro_catalog) {
  ParseResumeLocation location =
      ParseResumeLocation::FromStringView(kDefsFileName, source);
  bool at_end_of_input = false;
  while (!at_end_of_input) {
    std::unique_ptr<ParserOutput> output;
    GOOGLESQL_RETURN_IF_ERROR(ParseNextStatement(
        &location,
        ParserOptions(language_options, MacroExpansionMode::kLenient), &output,
        &at_end_of_input));
    GOOGLESQL_RET_CHECK(output->statement() != nullptr);
    auto def_macro_stmt =
        output->statement()->GetAsOrNull<ASTDefineMacroStatement>();
    GOOGLESQL_RET_CHECK(def_macro_stmt != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(macro_catalog.RegisterMacro(
        {.source_text = source,
         .location = def_macro_stmt->location(),
         .name_location = def_macro_stmt->name()->location(),
         .body_location = def_macro_stmt->body()->location()}));
  }
  return absl::OkStatus();
}

TEST_F(LookaheadTransformerTest, TokensFromDifferentFilesCannotFuse) {
  macros::MacroCatalog macro_catalog;
  GOOGLESQL_ASSERT_OK(
      RegisterMacros("DEFINE MACRO greater_than >", options_, macro_catalog));
  auto arena = std::make_unique<googlesql_base::UnsafeArena>(/*block_size=*/4096);
  StackFrame::StackFrameFactory stack_frame_factory;

  constexpr absl::string_view kInput = ">$greater_than";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer, LookaheadTransformer::Create(
                      ParserMode::kStatement, "fake_file", kInput, 0, options_,
                      MacroExpansionMode::kLenient, &macro_catalog, arena.get(),
                      stack_frame_factory));
  LookaheadTransformer& tokenizer = *lexer;

  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::GT));
  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::GT));
  EXPECT_THAT(GetTokenKindAndError(tokenizer), TokenKindIs(Token::EOI));
}

TEST_F(LookaheadTransformerTest, RightShiftIsAllowedAfterParentheses) {
  EXPECT_THAT(GetAllTokens(ParserMode::kStatement, "ARRAY<TYPEOF(1 >> 2)>"),
              ElementsAreArray(std::vector<Token>{
                  Token::KW_ARRAY,
                  Token::LT,
                  Token::IDENTIFIER,
                  Token::LPAREN,
                  Token::INTEGER_LITERAL,
                  Token::KW_SHIFT_RIGHT,
                  Token::INTEGER_LITERAL,
                  Token::RPAREN,
                  Token::GT,
                  Token::EOI,
              }));
}

TEST_F(LookaheadTransformerTest, RightShiftIsAllowedAfterUnpairedParentheses) {
  EXPECT_THAT(GetAllTokens(ParserMode::kStatement, "ARRAY<)>>>>>"),
              ElementsAreArray(std::vector<Token>{
                  Token::KW_ARRAY,
                  Token::LT,
                  Token::RPAREN,
                  Token::KW_SHIFT_RIGHT,
                  Token::KW_SHIFT_RIGHT,
                  Token::GT,
                  Token::EOI,
              }));
}

TEST_F(LookaheadTransformerTest, Lookahead3) {
  StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lexer,
      LookaheadTransformer::Create(
          ParserMode::kStatement, "fake_file", "a 1 SELECT SELECT", 0, options_,
          MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, /*arena=*/nullptr, stack_frame_factory));
  Location location;
  LookaheadTransformer& tokenizer = *lexer;

  // Ok to examine lookaheads before fetching any tokens.
  EXPECT_EQ(TokenTestThief::Lookahead3(tokenizer), Token::KW_SELECT);

  AdvanceLexer(tokenizer, location);
  EXPECT_EQ(TokenTestThief::Lookahead3(tokenizer), Token::KW_SELECT);

  AdvanceLexer(tokenizer, location);
  EXPECT_EQ(TokenTestThief::Lookahead3(tokenizer), Token::EOI);

  // Continue fetching tokens returns the same YYEOF.
  for (int i = 0; i < kFurtherLookaheadBeyondEof; ++i) {
    AdvanceLexer(tokenizer, location);
    EXPECT_EQ(TokenTestThief::Lookahead3(tokenizer), Token::EOI);
  }
}

}  // namespace googlesql::parser
