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

#include "googlesql/parser/textmapper_lexer_adapter.h"

#include <string>
#include <vector>

#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/public/language_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql::parser {
namespace {

TEST(GoogleSqlTextmapperLexerTest, TestInstantiate) {
  Lexer lexer = Lexer(ParserMode::kTokenizer, "filename", "SELECT 1",
                      /*start_offset=*/0, LanguageOptions(),
                      /*macro_expansion_mode*/ MacroExpansionMode::kNone,
                      /*macro_catalog=*/nullptr, /*arena=*/nullptr);
}

TEST(GoogleSqlTextmapperLexerTest, TestCopy) {
  Lexer lexer = Lexer(ParserMode::kNextStatement, "filename", "SELECT 1",
                      /*start_offset=*/0, LanguageOptions(),
                      /*macro_expansion_mode*/ MacroExpansionMode::kNone,
                      /*macro_catalog=*/nullptr, /*arena=*/nullptr);
  Lexer lookahead = lexer;
  EXPECT_EQ(lookahead.Next(), Token::KW_SELECT);
  EXPECT_EQ(lexer.Text(), "");
  EXPECT_EQ(lookahead.Next(), Token::INTEGER_LITERAL);
  EXPECT_EQ(lexer.Text(), "");

  EXPECT_EQ(lexer.Next(), Token::KW_SELECT);
  EXPECT_EQ(lexer.Text(), "SELECT");
  EXPECT_EQ(lexer.Next(), Token::INTEGER_LITERAL);
  EXPECT_EQ(lexer.Text(), "1");
}

TEST(GoogleSqlTextmapperLexerTest, TestLastFunctions) {
  Lexer lexer = Lexer(ParserMode::kNextStatement, "filename", "SELECT a.1b",
                      /*start_offset=*/0, LanguageOptions(),
                      /*macro_expansion_mode=*/MacroExpansionMode::kNone,
                      /*macro_catalog=*/nullptr, /*arena=*/nullptr);
  EXPECT_EQ(lexer.Next(), Token::KW_SELECT);
  EXPECT_EQ(lexer.Last(), Token::KW_SELECT);
  EXPECT_EQ(lexer.Last(), Token::KW_SELECT);
  EXPECT_EQ(lexer.Text(), "SELECT");
  EXPECT_EQ(lexer.LastTokenLocation().start().GetByteOffset(), 0);
  EXPECT_EQ(lexer.LastTokenLocation().end().GetByteOffset(), 6);
  EXPECT_FALSE(lexer.LastLastTokenLocation().IsValid());

  EXPECT_EQ(lexer.Next(), Token::IDENTIFIER);
  EXPECT_EQ(lexer.Last(), Token::IDENTIFIER);
  EXPECT_EQ(lexer.Last(), Token::IDENTIFIER);
  EXPECT_EQ(lexer.Text(), "a");
  EXPECT_EQ(lexer.LastTokenLocation().start().GetByteOffset(), 7);
  EXPECT_EQ(lexer.LastTokenLocation().end().GetByteOffset(), 8);
  EXPECT_EQ(lexer.LastLastTokenLocation().start().GetByteOffset(), 0);
  EXPECT_EQ(lexer.LastLastTokenLocation().end().GetByteOffset(), 6);
}

TEST(GoogleSqlTextmapperLexerTest, TestDotIdentifier) {
  TextMapperLexerAdapter lexer =
      TextMapperLexerAdapter(ParserMode::kTokenizer, "filename", "SELECT a.1b",
                             /*start_offset=*/0, LanguageOptions(),
                             /*macro_expansion_mode*/ MacroExpansionMode::kNone,
                             /*macro_catalog=*/nullptr,
                             /*arena=*/nullptr);
  std::vector<Token> tokens;
  Token next_token;
  do {
    next_token = lexer.Next();
    tokens.push_back(next_token);
  } while (next_token != Token::EOI);

  EXPECT_THAT(tokens,
              testing::ElementsAre(Token::KW_SELECT, Token::IDENTIFIER,
                                   Token::DOT, Token::IDENTIFIER, Token::EOI));
}

struct TokenizerTestCase {
  std::string input;
  std::vector<Token> expected_tokens;
};

using TokenizerTest = testing::TestWithParam<TokenizerTestCase>;

TEST_P(TokenizerTest, TestTokenization) {
  TextMapperLexerAdapter lexer = TextMapperLexerAdapter(
      ParserMode::kTokenizer, "filename", GetParam().input,
      /*start_offset=*/0, LanguageOptions(),
      /*macro_expansion_mode*/ MacroExpansionMode::kNone,
      /*macro_catalog=*/nullptr,
      /*arena=*/nullptr);

  std::vector<Token> actual_tokens;
  while (lexer.Next() != Token::EOI) {
    actual_tokens.push_back(lexer.Last());
  }

  EXPECT_THAT(actual_tokens,
              testing::ElementsAreArray(GetParam().expected_tokens));
}

INSTANTIATE_TEST_SUITE_P(
    DollarSignTokenizerTests, TokenizerTest,
    testing::ValuesIn<TokenizerTestCase>({
        {.input = "$", .expected_tokens = {Token::DOLLAR_SIGN}},
        {.input = "$$",
         .expected_tokens = {Token::DOLLAR_SIGN, Token::DOLLAR_SIGN}},
        {.input = "$foo", .expected_tokens = {Token::MACRO_INVOCATION}},
        {.input = "$$foo",
         .expected_tokens = {Token::MACRO_BUILTIN_INVOCATION}},
        {.input = "$$ foo",
         .expected_tokens = {Token::DOLLAR_SIGN, Token::DOLLAR_SIGN,
                             Token::IDENTIFIER}},
        {.input = "$ $foo",
         .expected_tokens = {Token::DOLLAR_SIGN, Token::MACRO_INVOCATION}},
        {.input = "$$$$foo",
         .expected_tokens = {Token::DOLLAR_SIGN, Token::DOLLAR_SIGN,
                             Token::MACRO_BUILTIN_INVOCATION}},
        {.input = "$foo$$foo$$$foo$$",
         .expected_tokens = {Token::MACRO_INVOCATION,
                             Token::MACRO_BUILTIN_INVOCATION,
                             Token::DOLLAR_SIGN,
                             Token::MACRO_BUILTIN_INVOCATION,
                             Token::DOLLAR_SIGN, Token::DOLLAR_SIGN}},
        {.input = "$0", .expected_tokens = {Token::MACRO_ARGUMENT_REFERENCE}},
        {.input = "$$0",
         .expected_tokens = {Token::DOLLAR_SIGN,
                             Token::MACRO_ARGUMENT_REFERENCE}},
        {.input = "$0$$0$$$0",
         .expected_tokens = {Token::MACRO_ARGUMENT_REFERENCE,
                             Token::DOLLAR_SIGN,
                             Token::MACRO_ARGUMENT_REFERENCE,
                             Token::DOLLAR_SIGN, Token::DOLLAR_SIGN,
                             Token::MACRO_ARGUMENT_REFERENCE}},
        {.input = "${foo}",
         .expected_tokens = {Token::DOLLAR_SIGN, Token::LBRACE,
                             Token::IDENTIFIER, Token::RBRACE}},
        {.input = "$${foo}",
         .expected_tokens = {Token::DOLLAR_SIGN, Token::DOLLAR_SIGN,
                             Token::LBRACE, Token::IDENTIFIER, Token::RBRACE}},
    }));

}  // namespace
}  // namespace googlesql::parser
