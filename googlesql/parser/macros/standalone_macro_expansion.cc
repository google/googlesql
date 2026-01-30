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

#include "googlesql/parser/macros/standalone_macro_expansion.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "googlesql/parser/macros/token_splicing_utils.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "absl/container/btree_map.h"
#include "googlesql/base/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace parser {
namespace macros {

static bool IsIntegerOrFloatingPointLiteral(const TokenWithLocation& token) {
  ABSL_DCHECK(token.kind != Token::INTEGER_LITERAL)
      << "Macor expander should not see INTEGER_LITERAL directly. Instead it "
         "should see DECIMAL_INTEGER_LITERAL or HEX_INTEGER_LITERAL";
  return token.kind == Token::DECIMAL_INTEGER_LITERAL ||
         token.kind == Token::HEX_INTEGER_LITERAL ||
         token.kind == Token::FLOATING_POINT_LITERAL;
}

static bool SplicingTokensCouldStartComment(
    const TokenWithLocation& previous_token,
    const TokenWithLocation& current_token) {
  return (previous_token.kind == Token::MINUS &&
          current_token.kind == Token::MINUS) ||
         (previous_token.kind == Token::DIV &&
          current_token.kind == Token::DIV) ||
         (previous_token.kind == Token::DIV &&
          current_token.kind == Token::MULT);
}

static bool TokensRequireExplicitSeparation(
    const TokenWithLocation& previous_token,
    const TokenWithLocation& current_token) {
  if (current_token.text.empty()) {
    // YYEOF doesn't need separation.
    return false;
  }

  // Macro invocation, keyword or unquoted identifier followed by a character
  // that can continue it.
  if (previous_token.kind == Token::MACRO_INVOCATION ||
      previous_token.kind == Token::STANDALONE_EXPONENT_SIGN ||
      previous_token.kind == Token::EXP_IN_FLOAT_NO_SIGN ||
      IsKeywordOrUnquotedIdentifier(previous_token)) {
    return IsIdentifierCharacter(current_token.text.front());
  }
  // Macro argument reference followed by a decimal digit.
  if (previous_token.kind == Token::MACRO_ARGUMENT_REFERENCE) {
    return std::isdigit(current_token.text.front());
  }

  // Avoid comment-outs, where symbols inadvertently become the start of a
  // comment.
  if (SplicingTokensCouldStartComment(previous_token, current_token)) {
    return true;
  }

  // Integer and floating-point literals should not splice
  if (IsIntegerOrFloatingPointLiteral(previous_token) &&
      IsIntegerOrFloatingPointLiteral(current_token)) {
    return true;
  }

  // Two ">"s should not be fused together. For example with
  // `DEFINE MACRO gt >`, `$gt()$gt()` should not be printed as ">>" (one right
  // shift) but "> >" (two greater than symbols).
  if (previous_token.kind == Token::GT && current_token.kind == Token::GT) {
    return true;
  }

  // OK to have no space.
  return false;
}

void AppendTokenWithWhitespace(
    std::string& expanded_sql, absl::string_view token_text,
    absl::string_view whitespace, int token_index,
    absl::btree_map<size_t, int>* location_to_token_index) {
  absl::StrAppend(&expanded_sql, whitespace, token_text);
  if (location_to_token_index != nullptr) {
    location_to_token_index->insert({expanded_sql.size(), token_index});
  }
}

void AppendToken(std::string& expanded_sql, absl::string_view token_text,
                 int token_index,
                 absl::btree_map<size_t, int>* location_to_token_index) {
  AppendTokenWithWhitespace(expanded_sql, token_text, /*whitespace=*/"",
                            token_index, location_to_token_index);
}

std::string TokensToString(
    absl::Span<const TokenWithLocation> tokens,
    absl::btree_map<size_t, int>* location_to_token_index) {
  std::string expanded_sql;
  int index = 0;
  for (auto it = tokens.begin(); it != tokens.end(); ++it, ++index) {
    const auto& current_token = *it;
    absl::string_view whitespace = current_token.preceding_whitespaces;
    if (it == tokens.begin()) {
      AppendTokenWithWhitespace(expanded_sql, current_token.text, whitespace,
                                index, location_to_token_index);
      continue;
    }
    const TokenWithLocation& previous_token = *(it - 1);
    if (previous_token.AdjacentlyPrecedes(current_token)) {
      // Do not insert spaces between adjacent tokens. For example, two adjacent
      // ">" tokens should not become "> >".
      AppendToken(expanded_sql, current_token.text, index,
                  location_to_token_index);
      continue;
    }
    if (whitespace.empty() &&
        TokensRequireExplicitSeparation(previous_token, current_token)) {
      // Prevent token splicing by forcing an extra space.
      whitespace = " ";
    }
    AppendTokenWithWhitespace(expanded_sql, current_token.text, whitespace,
                              index, location_to_token_index);
  }
  return expanded_sql;
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
