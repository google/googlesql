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

#include "googlesql/tools/formatter/internal/chunk.h"

#include <stddef.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/formatter_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_tokens.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/tools/formatter/internal/fusible_tokens.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "googlesql/base/flat_set.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::formatter::internal {
namespace {

// Limits the size of the lhs operand that can be followed with the operator on
// the same line even if the line gets too big. The number is arbitrary and can
// be adjusted based on the owners taste :) For instance:
//
// T.a_column_name.sub_field IN (   # "IN (" stays on the same line with lhs.
//   1, 2, 3)
// T.even_longer_name.sub_field     # lhs is too long, "IN (" goes to next line.
//   IN (
//     1, 2, 3)
// There are more conditions for the operator to stay on the same line, see
// function `TokenIsOperatorThatCanBeFusedWithLeftOperand` below.
constexpr int kMaxLeftOperandLengthFusibleWithOperator = 25;

bool AllowSpaceBefore(absl::string_view keyword) {
  static const auto* forbidden =
      new googlesql_base::flat_set<absl::string_view>({".", ",", ";", ")", "]", "}", ":"});
  return !forbidden->contains(keyword);
}

bool AllowSpaceAfter(absl::string_view keyword) {
  static const auto* forbidden = new googlesql_base::flat_set<absl::string_view>(
      {"(", ".", "[", "{", "@", "@@", "$", ":"});
  return !forbidden->contains(keyword);
}

bool IsTopLevelClauseKeyword(const Token& token) {
  // NOTE: This list should contain only reserved keywords, that can only act
  // as top level clause keywords. For instance, "ON" - is reserved keyword, but
  // is not a top-level keyword in JOIN clause; "GRANT" is top-level only if it
  // starts a statement (for such cases - update the list in
  // `MarkAllStatementStarts`); "WITH" is not top-level when
  // introducing a `WITH OFFSET` clause.
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>(
      {"ASSERT_ROWS_MODIFIED", "CREATE", "DEFINE", "FROM", "GROUP", "HAVING",
       "JOIN", "LIMIT", "ORDER", "PARTITION", "QUALIFY", "RANGE", "ROWS",
       "SELECT", "SET", "TO", "WHERE", "WINDOW"});
  return token.Is(Token::Type::TOP_LEVEL_KEYWORD) ||
         (!token.IsOneOf({Token::Type::COMPARISON_OPERATOR,
                          Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT,
                          Token::Type::PRIVILEGE_TYPE_KEYWORD,
                          Token::Type::DDL_KEYWORD}) &&
          allowed->contains(token.GetKeyword()));
}

bool CanBeFusedWithOpenParenOrBracket(const Token& token) {
  static const auto* forbidden = new googlesql_base::flat_set<absl::string_view>(
      {"CASE", "WHEN", "THEN", "ELSE", "}"});
  const absl::string_view keyword = token.GetKeyword();
  return !token.IsValue() &&
         (!(IsTopLevelClauseKeyword(token) || forbidden->contains(keyword)) ||
          keyword == "ASSERT" || keyword == "WITH" || token.IsMacroCall());
}

bool CanBeFusedWithOpenParenOrBracket(const Chunk& chunk,
                                      const Token* last_token = nullptr) {
  static const auto* forbidden_chunk_start =
      new googlesql_base::flat_set<absl::string_view>(
          {"ADD", "INSERT", "REPLACE", "SELECT"});
  const std::vector<Token*>& tokens = chunk.Tokens().WithoutComments();
  if (tokens.empty()) {
    return true;
  }
  if (last_token == nullptr) {
    last_token = tokens.back();
  }
  if (chunk.EndsWithMacroCall()) {
    // Macros can appear anywhere, so we trust user to add a line break between
    // macro call and opening parenthesis if needed.
    return last_token->GetLineBreaksAfter() == 0;
  }
  return CanBeFusedWithOpenParenOrBracket(*last_token) &&
         !(tokens.size() > 1 && IsTopLevelClauseKeyword(*tokens.front()) &&
           forbidden_chunk_start->contains(tokens.front()->GetKeyword()));
}

bool IsSetOperatorToken(const Token& token) {
  static const auto* allowed =
      new googlesql_base::flat_set<absl::string_view>({"UNION", "INTERSECT", "EXCEPT"});
  return !token.Is(Token::Type::SELECT_STAR_MODIFIER) &&
         allowed->contains(token.GetKeyword());
}

bool IsChainableBooleanOperatorKeyword(absl::string_view keyword) {
  static const auto* allowed =
      new googlesql_base::flat_set<absl::string_view>({"AND", "OR"});
  return allowed->contains(keyword);
}

bool IsComparisonOperatorKeyword(absl::string_view keyword) {
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>(
      {"=", "!=", "<>", "<", "<=", ">", ">=", "BETWEEN", "IN", "IS", "LIKE"});
  return allowed->contains(keyword);
}

// Returns true if the keyword is an operator that can be repeated in a row,
// for instance "a + b + c".
bool IsRepeatableOperatorKeyword(absl::string_view keyword) {
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>(
      {"+", "*", "-", "/", "&", "^", "|", "||", ">>", "<<", "AND", "OR"});
  return allowed->contains(keyword);
}

// This is for operators that can be "chained", i.e. the input and output types
// are compatible. For example, writing "x AND y AND z" is reasonable, but "x IN
// y IN z" is a syntax error.
// Note that some operators cannot be repeated, for instance, "a = b = c"
// is not a valid SQL, but "a = b + c" is.
bool IsChainableOperator(const Token& token) {
  // Tokens "<" and ">" can be chainable operators, but not when they are used
  // as brackets in a type declaration, e.g., STRUCT<INT64>.
  // "=" can be either comparison or assignment.
  if (token.IsOneOf({Token::Type::CLOSE_BRACKET, Token::Type::OPEN_BRACKET,
                     Token::Type::ASSIGNMENT_OPERATOR,
                     Token::Type::UNARY_OPERATOR,
                     Token::Type::COMPLEX_TOKEN_START})) {
    return false;
  }
  if (token.Is(Token::Type::COMPARISON_OPERATOR)) {
    return true;
  }
  return IsRepeatableOperatorKeyword(token.GetKeyword()) ||
         IsComparisonOperatorKeyword(token.GetKeyword());
}

// Returns true if the token is a repeatable operator, e.g., "a + b + c".
bool IsRepeatableOperator(const Token& token) {
  return IsRepeatableOperatorKeyword(token.GetKeyword()) &&
         // AND can be part of BETWEEN..AND comparison operator. In this case
         // it is not repeatable.
         // + and - can be unary operators.
         !token.IsOneOf(
             {Token::Type::COMPARISON_OPERATOR, Token::Type::UNARY_OPERATOR});
}

// Returns true if the given keyword _can_ be an unary operator.
bool CanBeUnaryOperator(absl::string_view keyword) {
  static const auto* allowed =
      new googlesql_base::flat_set<absl::string_view>({"-", "+", "~"});
  return allowed->contains(keyword);
}

// Returns true if the keyword can be a part of a continuous expression (e.g.,
// "max + min + offset" or "date AND time").
bool CanBePartOfExpression(const Token& token) {
  static const auto* forbidden = new googlesql_base::flat_set<absl::string_view>(
      {"}", ";", ",", "BY", "DEFAULT", "DEFINE", "CASE", "ELSE", "INTERVAL",
       "ON", "THEN", "WHEN"});
  return !IsTopLevelClauseKeyword(token) &&
         !forbidden->contains(token.GetKeyword());
}

// Returns true if the given token can follow a type declaration, e.g., in
// expression "STRUCT<INT64>(1)", '(' follows the declaration.
bool CanFollowTypeDeclaration(absl::string_view keyword) {
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>(
      {">", ")", "(", ",", "[", ";", "AS", "DEFAULT"});
  return allowed->contains(keyword);
}

// Returns true if the given keyword can start a type declaration.
bool CanBeTypeDeclarationStart(absl::string_view keyword) {
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>(
      {"ARRAY", "ENUM", "MAP", "PROTO", "RANGE", "STRUCT", "TABLE"});
  return allowed->contains(keyword);
}

// Returns true if the current keyword can be an end of a type declaration.
bool CanBeTypeDeclarationEnd(absl::string_view current,
                             absl::string_view next) {
  if (current != ">") {
    return false;
  }
  return next.empty() || CanFollowTypeDeclaration(next);
}

// Returns true if the token is an inline comment with function argument name,
// like: "/*arg_name=*/NULL"
bool IsArgumentNameInlineComment(const Token& comment) {
  if (!comment.IsComment()) {
    return false;
  }
  absl::string_view comment_string = comment.GetImage();
  if (!absl::ConsumeSuffix(&comment_string, "*/")) {
    return false;
  }
  comment_string = absl::StripTrailingAsciiWhitespace(comment_string);
  return comment_string.size() > 3 && comment_string.back() == '=';
}

// Helper function that returns position of the matching closing bracket for
// the opening bracket at `index`. Returns tokens.size() if no matching
// parentheses found. Works for (), [] and {}.
int FindMatchingClosingBracket(const std::vector<Token*>& tokens, int index) {
  const absl::string_view open_bracket = tokens[index]->GetKeyword();
  const absl::string_view close_bracket =
      CorrespondingCloseBracket(open_bracket);
  int bracket_count = 1;
  for (int t = index + 1; t < tokens.size(); ++t) {
    if (tokens[t]->GetKeyword() == open_bracket) {
      ++bracket_count;
    } else if (tokens[t]->GetKeyword() == close_bracket) {
      if (--bracket_count == 0) {
        return t;
      }
    }
  }
  return static_cast<int>(tokens.size());
}

// Calculates line and column location in the input sql for the given token.
// The location is 1-based.
absl::StatusOr<std::pair<int, int>> TranslateLocation(
    const ParseLocationTranslator& location_translator,
    const ParseToken& token) {
  return location_translator.GetLineAndColumnAfterTabExpansion(
      token.GetLocationRange().start());
}

// Returns true if the token `t` starts a braced map constructor.
bool MaybeAnOpenBraceForMapConstructor(const std::vector<Token*>& tokens,
                                       int t) {
  return tokens[t]->GetKeyword() == "{" &&
         // Skip various templates like ${param} or %{template}.
         (t == 0 || (tokens[t - 1]->GetImage() != "$" &&
                     tokens[t - 1]->GetKeyword() != "%" &&
                     tokens[t - 1]->GetKeyword() != "@"));
}

// Returns the index of the next or previous non-comment token.
// step: 1 to search forward, -1 to search backward.
int GetTokenIndexSkippingComments(
    int current_idx, const std::vector<formatter::internal::Token*>& tokens,
    int step) {
  int idx = current_idx + step;
  while (idx >= 0 && idx < tokens.size() &&
         tokens[idx]->kind() == googlesql::ParseToken::COMMENT) {
    idx += step;
  }
  return idx;
}

// Returns the index of the next non-comment token.
int GetNextTokenIndex(int current_idx,
                      const std::vector<formatter::internal::Token*>& tokens) {
  return GetTokenIndexSkippingComments(current_idx, tokens, /*step=*/1);
}

// Returns the index of the previous non-comment token.
int GetPrevTokenIndexIndex(
    int current_idx, const std::vector<formatter::internal::Token*>& tokens) {
  return GetTokenIndexSkippingComments(current_idx, tokens, /*step=*/-1);
}

// Checks if a token is a valid prefix for a chained method call, i.e.,
// if it can appear immediately before the dot. GoogleSQL requires most
// expressions (like column names) to be in parentheses, e.g.,
// `(column).func()`. This function allows prefixes that don't need extra
// parentheses. Allowed prefixes include:
//  - Closing parentheses or brackets: `)` or `]`
//  - String and Bytes literals: `'abc'`
//  - Boolean and NULL literals: `TRUE`, `FALSE`, `NULL`
bool IsAllowedChainedMethodPrefix(const Token& token) {
  const std::string image(token.GetImage());

  // Case 1: Closing parentheses from function calls, parenthesized expressions,
  // or STRUCT literals. Case 2: Closing brackets from ARRAY literals.
  if (image == ")" || image == "]") {
    return true;
  }

  // Case 3 & 4: String or Bytes literals.
  // This also covers the terminal string token of DATE, TIME, DATETIME,
  // TIMESTAMP, NUMERIC, BIGNUMERIC, JSON, RANGE, and INTERVAL literals.
  if (token.IsStringLiteral() || token.IsBytesLiteral()) {
    return true;
  }

  // Case 5: Boolean and NULL keyword literals.
  if (token.IsKeyword()) {
    std::string upper_image = absl::AsciiStrToUpper(image);
    if (upper_image == "TRUE" || upper_image == "FALSE" ||
        upper_image == "NULL") {
      return true;
    }
  }

  // Other tokens, such as identifiers (column names), numeric literals
  // (123, 45.67), operators, etc., require the expression to be wrapped in
  // parentheses, e.g., (my_column).CHAINED_FUNC(), (123).APPLY(...). The token
  // preceding the '.' in those wrapped cases would be ')', which is handled
  // above.
  return false;
}

// Checks if the token at start_idx begins a single-part function call
// (e.g., "FUNCTION()").
bool IsSinglepartFunctionCall(
    int start_idx, const std::vector<formatter::internal::Token*>& tokens) {
  if (!tokens[start_idx]->IsIdentifier() && !tokens[start_idx]->IsKeyword()) {
    return false;
  }
  int lparen_idx = GetNextTokenIndex(start_idx, tokens);
  return lparen_idx < tokens.size() && tokens[lparen_idx]->GetImage() == "(";
}

// Returns the index of the opening parenthesis of the argument list if a
// multipart function call is found, otherwise returns nullopt.
std::optional<int> GetMultipartCallArgParenIndex(
    const std::vector<Token*>& tokens, int first_paren_idx) {
  // Expected pattern: .(part1.part2...)()
  int current_idx = GetNextTokenIndex(first_paren_idx, tokens);

  // A multi-part function name is a dot-separated list of identifiers. This
  // loop consumes all parts of the name.
  do {
    if (current_idx >= tokens.size() || !tokens[current_idx]->IsIdentifier()) {
      return std::nullopt;
    }
    int dot_idx = GetNextTokenIndex(current_idx, tokens);
    if (dot_idx >= tokens.size() || tokens[dot_idx]->GetKeyword() != ".") {
      current_idx = dot_idx;  // This will point to the token after the last
                              // identifier part.
      break;
    }
    current_idx = GetNextTokenIndex(dot_idx, tokens);
  } while (true);

  // Expect a closing parenthesis for the multi-part name.
  if (current_idx >= tokens.size() ||
      tokens[current_idx]->GetKeyword() != ")") {
    return std::nullopt;
  }

  // Expect the function call parentheses.
  int call_paren_idx = GetNextTokenIndex(current_idx, tokens);
  if (call_paren_idx >= tokens.size() ||
      tokens[call_paren_idx]->GetKeyword() != "(") {
    return std::nullopt;
  }

  return call_paren_idx;
}

}  // namespace

// Marks all dots that are used to separate chained method calls.
// This function iterates through the tokens and identifies dots that
// follow an allowed prefix and precede a function call (either single or
// multi-part).
void MarkChainedMethodDotSeparators(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.All();
  for (int i = 0; i < tokens.size(); ++i) {
    if (tokens[i]->GetKeyword() != ".") {
      continue;
    }
    // Found a dot. Check if the token preceding it is an expression that can
    // be legally followed by a method call without being enclosed in
    // parentheses (e.g., literals, or expressions ending in ')' or ']').
    int prev_idx = GetPrevTokenIndexIndex(i, tokens);
    if (prev_idx < 0 || !IsAllowedChainedMethodPrefix(*tokens[prev_idx])) {
      continue;
    }

    // Check if the token following the dot is part of a function call.
    int next_idx = GetNextTokenIndex(i, tokens);
    if (next_idx >= tokens.size()) {
      continue;
    }

    if (tokens[next_idx]->GetKeyword() == "(") {
      // If dot is followed by '(', it could be a multi-part function call
      // enclosed in parentheses, e.g., `.(SAFE.SQRT)()`.
      std::optional<int> lparen_args_idx =
          GetMultipartCallArgParenIndex(tokens, next_idx);
      if (lparen_args_idx.has_value()) {
        // It's a valid multi-part call. Mark the dot and assign type
        // COMPLEX_TOKEN_CONTINUATION to function call tokens up to
        // argument parenthesis to keep them in the same chunk.
        tokens[i]->SetType(Token::Type::CHAINED_METHOD_DOT_SEPARATOR);
        for (int k = next_idx; k <= *lparen_args_idx; ++k) {
          tokens[k]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
        }
        i = *lparen_args_idx;
      }
    } else if (IsSinglepartFunctionCall(next_idx, tokens)) {
      // If dot is followed by an identifier and '(', it's a single-part
      // function call like `.LOWER()`. Mark the dot.
      tokens[i]->SetType(Token::Type::CHAINED_METHOD_DOT_SEPARATOR);
    }
  }
}

absl::StatusOr<Chunk> Chunk::CreateEmptyChunk(
    const bool starts_with_space, const int position_in_query,
    const ParseLocationTranslator& location_translator,
    const Token& first_token) {
  // Columns are 1-based, but we use 0 by default to catch a bug if we
  // accidentally use non-initialized column.
  int original_column = 0;
  if (first_token.IsComment() && first_token.IsMultiline()) {
    // Calculate actual location only for multiline comments: only these need
    // the original column when printing the final output. We skip all other
    // tokens, because ParseLocationTranslator is very slow for inputs with
    // very long lines.
    GOOGLESQL_ASSIGN_OR_RETURN(auto location,
                     TranslateLocation(location_translator, first_token));
    original_column = location.second;
  }
  return Chunk(starts_with_space, position_in_query, original_column);
}

bool Chunk::SpaceBetweenTokens(const Token& token_before,
                               const Token& token_after) const {
  if ((token_before.IsInlineComment() || token_after.IsInlineComment()) &&
      !SpaceBetweenTokensInInput(token_before, token_after)) {
    // Preserve no space before/after inline token.
    return false;
  }
  if (token_after.IsEndOfLineComment()) {
    return true;
  }
  if (token_before.Is(Token::Type::CLOSE_BRACKET) &&
      token_after.GetKeyword() == "DEFAULT") {
    return true;
  }
  if (token_before.IsOneOf({Token::Type::UNARY_OPERATOR,
                            Token::Type::OPEN_BRACKET,
                            Token::Type::CLOSE_BRACKET})) {
    return false;
  }
  if (token_after.Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) {
    return false;
  }
  if (token_before.Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT)) {
    return SpaceBetweenTokensInInput(token_before, token_after);
  }
  if (token_before.Is(Token::Type::TABLE_NAME_IN_DEFINE_STMT) &&
      token_after.Is(Token::Type::TABLE_NAME_IN_DEFINE_STMT)) {
    return false;
  }
  // Map constructor:
  // Always have space after ':'
  // e.g.: "field1: { a: 1 ..."
  if (token_before.Is(Token::Type::BRACED_CONSTR_COLON)) {
    return true;
    // Always have space after '{' unless it is empty message '{}'.
  } else if (token_before.Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET) &&
             token_before.GetKeyword() == "{") {
    return !(token_after.GetKeyword() == "}");
    // Otherwise, add space before '}'.
  } else if (token_after.Is(Token::Type::BRACED_CONSTR_CLOSE_BRACKET) &&
             token_after.GetKeyword() == "}") {
    return true;
    // Keep space before '{' only if when it goes after another map constructor
    // bracket, otherwise default SQL spacing rules apply, e.g.:
    //   repeated_field: [ { ...
    // but:
    //   ARRAY<Foo>[{ ...
  } else if (token_after.Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET) &&
             token_before.Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET)) {
    return true;
    // After '}' leave space only when next token is another map constructor
    // bracket, otherwise use default rules. e.g.:
    //   repeated_field: [ { ... } ]
    // but:
    //   ARRAY<Foo>[{ ... }]
  } else if (token_before.Is(Token::Type::BRACED_CONSTR_CLOSE_BRACKET) &&
             token_after.Is(Token::Type::BRACED_CONSTR_CLOSE_BRACKET)) {
    return true;
  }

  static const auto* kRequireSpaceBeforeParenthesis =
      new googlesql_base::flat_set<absl::string_view>({"AS", "ASSERT", "DISTINCT",
                                            "INTERVAL", "NOT", "OPTIONS", "ON",
                                            "OVER", "USING", ",", "=>", "->"});
  if (token_after.GetKeyword() == "(") {
    if (token_before.GetKeyword() == "UNNEST" ||
        token_before.GetKeyword() == "WITH") {
      return false;
    }
    if ((token_before.GetKeyword() == ")" &&
         token_before.Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) ||
        token_before.IsMacroCall()) {
      return SpaceBetweenTokensInInput(token_before, token_after);
    } else if (IsTopLevelClauseKeyword(token_before) || IsSetOperator() ||
               IsChainableOperator(token_before) ||
               kRequireSpaceBeforeParenthesis->contains(
                   token_before.GetKeyword()) ||
               token_before.IsOneOf({Token::Type::MACRO_NAME_IN_DEFINE_STMT,
                                     Token::Type::TABLE_NAME_IN_DEFINE_STMT,
                                     Token::Type::PRIVILEGE_TYPE_KEYWORD,
                                     Token::Type::SELECT_STAR_MODIFIER,
                                     Token::Type::ASSIGNMENT_OPERATOR}) ||
               !CanBeFusedWithOpenParenOrBracket(*this, &token_before)) {
      return true;
    }
    return false;
  }
  if (token_after.GetKeyword() == "[") {
    if ((token_before.MayBeIdentifier() &&
         // Script names can be escaped with squared brackets
         // (i.e. `RUN [script_name]()`). In these cases we shouldn't remove the
         // space before the bracket.
         token_before.GetKeyword() != "RUN") ||
        token_before.GetKeyword() == "ARRAY" ||
        token_before.GetKeyword() == ")" || token_before.GetKeyword() == "]" ||
        !AllowSpaceAfter(token_before.GetKeyword())) {
      return false;
    }
    return true;
  }
  // If token_after is "<" or ">", there should be space if this is a comparison
  // operator, and no space if this is a type declaration, e.g., STRUCT<INT64>.
  if (token_after.GetKeyword() == "<") {
    return !token_after.Is(Token::Type::OPEN_BRACKET);
  }
  if (token_after.GetKeyword() == ">") {
    return !token_after.Is(Token::Type::CLOSE_BRACKET);
  }
  if (token_before.GetKeyword() == "RUN" &&
      token_before.Is(Token::Type::TOP_LEVEL_KEYWORD)) {
    return true;
  }

  return AllowSpaceBefore(token_after.GetKeyword()) &&
         AllowSpaceAfter(token_before.GetKeyword());
}

bool Chunk::SpaceBeforeToken(int index) const {
  if (index < 1 || index >= tokens_.All().size()) {
    return false;
  }
  return SpaceBetweenTokens(*tokens_.All()[index - 1], *tokens_.All()[index]);
}

std::string Chunk::PrintableString(const FormatterOptions& options, int column,
                                   bool is_line_end) const {
  std::string t;

  const std::vector<Token*>& tokens = tokens_.All();
  for (int i = 0; i < tokens.size(); ++i) {
    if (i && SpaceBeforeToken(i)) {
      if (is_line_end && i + 1 == tokens.size() && tokens[i]->IsComment()) {
        // Current token is end of line comment.
        absl::StrAppend(&t, std::string("  "));
      } else {
        absl::StrAppend(&t, std::string(" "));
      }
    }
    absl::StrAppend(&t, tokens[i]->ImageForPrinting(options, i == 0, column,
                                                    original_column_));
  }
  return t;
}

int Chunk::PrintableLength(bool is_line_end) const {
  int length = 0;

  const std::vector<Token*>& tokens = tokens_.All();
  for (int i = 0; i < tokens.size(); ++i) {
    if (i && SpaceBeforeToken(i)) {
      ++length;
      if (is_line_end && i + 1 == tokens.size() && tokens[i]->IsComment()) {
        // Current token is end of line comment - add one more space.
        ++length;
      }
    }
    if (tokens[i]->IsComment()) {
      // Comments capture trailing line breaks in their image.
      length +=
          absl::StripTrailingAsciiWhitespace(tokens[i]->GetImage()).size();
    } else {
      length += tokens[i]->GetImage().size();
    }
  }

  return length;
}

std::string Chunk::DebugString(const bool verbose) const {
  std::string result = PrintableString(FormatterOptions(), /*column=*/0,
                                       /*is_line_end=*/false);
  if (!verbose) {
    return result;
  }
  result = absl::StrCat("\"", result, "\"");
  // Append token types if there are any special tokens.
  if (absl::c_any_of(tokens_.All(), [](const Token* t) {
        return !t->Is(Token::Type::UNKNOWN);
      })) {
    absl::StrAppend(&result, " t(",
                    absl::StrJoin(tokens_.All(), ",",
                                  [](std::string* out, const Token* t) {
                                    return absl::StrAppend(out, t->GetType());
                                  }),
                    ")");
  }
  return result;
}

std::ostream& operator<<(std::ostream& os, const Chunk* const chunk) {
  if (chunk == nullptr) {
    return os << "<null>";
  }
  return os << '"' << chunk->DebugString() << '"';
}

absl::string_view Chunk::FirstKeyword() const { return NonCommentKeyword(0); }

absl::string_view Chunk::SecondKeyword() const { return NonCommentKeyword(1); }

absl::string_view Chunk::LastKeyword() const { return NonCommentKeyword(-1); }

const Token& Chunk::FirstToken() const { return NonCommentToken(0); }

const Token& Chunk::LastToken() const { return NonCommentToken(-1); }

absl::string_view Chunk::NonCommentKeyword(int i) const {
  if (Empty()) {
    return "";
  }
  return NonCommentToken(i).GetKeyword();
}

const Token& Chunk::NonCommentToken(int i) const {
  static Token* empty_token = new Token(ParseToken());
  int token_index =
      i >= 0 ? i : static_cast<int>(tokens_.WithoutComments().size()) + i;
  if (token_index < 0 || token_index >= tokens_.WithoutComments().size()) {
    return *empty_token;
  }
  return *tokens_.WithoutComments()[token_index];
}

const Chunk* Chunk::NextNonCommentChunk() const {
  const Chunk* next_chunk = NextChunk();
  while (next_chunk != nullptr) {
    if (!next_chunk->IsCommentOnly()) {
      return next_chunk;
    }
    next_chunk = next_chunk->NextChunk();
  }
  return nullptr;
}

const Chunk* Chunk::PreviousNonCommentChunk() const {
  const Chunk* prev_chunk = PreviousChunk();
  while (prev_chunk != nullptr) {
    if (!prev_chunk->IsCommentOnly()) {
      return prev_chunk;
    }
    prev_chunk = prev_chunk->PreviousChunk();
  }
  return nullptr;
}

bool Chunk::EndsWithComment() const {
  return !Empty() && tokens_.All().back()->IsComment();
}

bool Chunk::EndsWithSingleLineComment() const {
  return !Empty() && tokens_.All().back()->IsComment() &&
         !tokens_.All().back()->IsSlashStarComment();
}

bool Chunk::IsCommentOnly() const {
  return !Empty() && tokens_.WithoutComments().empty();
}

bool Chunk::IsImport() const {
  const absl::string_view second_keyword = SecondKeyword();
  return !second_keyword.empty() &&
         FirstToken().Is(Token::Type::TOP_LEVEL_KEYWORD) &&
         FirstKeyword() == "IMPORT" &&
         (second_keyword == "MODULE" || second_keyword == "PROTO");
}

bool Chunk::IsModuleDeclaration() const {
  return !Empty() && FirstToken().Is(Token::Type::TOP_LEVEL_KEYWORD) &&
         FirstKeyword() == "MODULE";
}

bool Chunk::IsTopLevelClauseChunk() const {
  return !Empty() && IsTopLevelClauseKeyword(FirstToken());
}

bool Chunk::IsSetOperator() const {
  return !Empty() && FirstToken().Is(Token::Type::SET_OPERATOR_START);
}

bool Chunk::LastKeywordsAre(absl::string_view k1, absl::string_view k2) const {
  return LastKeyword() == k2 && NonCommentKeyword(-2) == k1;
}

bool Chunk::MayBeFunctionSignatureModifier() const {
  static const auto* kOneWordModifiers = new googlesql_base::flat_set<absl::string_view>(
      {"DETERMINISTIC", "IMMUTABLE", "LANGUAGE", "STABLE", "VOLATILE",
       "RETURNS"});

  return kOneWordModifiers->contains(FirstKeyword()) ||
         (FirstKeyword() == "SQL" && SecondKeyword() == "SECURITY") ||
         (FirstKeyword() == "NOT" && SecondKeyword() == "DETERMINISTIC");
}

bool Chunk::IsFunctionSignatureModifier() const {
  return FirstToken().Is(Token::Type::TOP_LEVEL_KEYWORD) &&
         MayBeFunctionSignatureModifier();
}

bool Chunk::IsStartOfNewQuery() const {
  return !Empty() && ((FirstKeyword() == "SELECT" &&
                       !FirstToken().Is(Token::Type::PRIVILEGE_TYPE_KEYWORD)) ||
                      IsTopLevelWith());
}

bool Chunk::IsStartOfCreateStatement() const {
  return !Empty() && FirstKeyword() == "CREATE";
}

bool Chunk::IsStartOfExportDataStatement() const {
  return !Empty() && FirstKeyword() == "EXPORT" && SecondKeyword() == "DATA";
}

bool Chunk::IsStartOfDefineMacroStatement() const {
  return absl::c_any_of(tokens_.WithoutComments(), [](const Token* t) {
    return t->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT);
  });
}

bool Chunk::IsStartOfList() const {
  return absl::c_any_of(tokens_.WithoutComments(), [](const Token* t) {
    return t->Is(Token::Type::STARTS_LIST);
  });
}

bool Chunk::IsCreateOrExportIndentedClauseChunk() const {
  if (LastKeyword() == "BY") {
    return FirstKeyword() == "CLUSTER" || FirstKeyword() == "PARTITION";
  }
  if (FirstKeyword() == "DEFAULT") {
    return SecondKeyword() == "COLLATE";
  }
  return LastKeywordsAre("OPTIONS", "(");
}

bool Chunk::IsMultiline() const {
  for (const Token* token : tokens_.All()) {
    if (token->IsMultiline()) {
      return true;
    }
  }
  return false;
}

bool Chunk::IsTopLevelWith() const {
  return !Empty() && FirstToken().Is(Token::Type::TOP_LEVEL_KEYWORD) &&
         FirstKeyword() == "WITH";
}

bool Chunk::OpensParenBlock() const { return !Empty() && LastKeyword() == "("; }

bool Chunk::OpensParenOrBracketBlock() const {
  return LastToken().Is(Token::Type::OPEN_BRACKET) ||
         LastToken().Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET) ||
         IsOpenParenOrBracket(LastKeyword());
}

bool Chunk::ClosesParenBlock() const {
  return !Empty() && FirstKeyword() == ")";
}

bool Chunk::ClosesParenOrBracketBlock() const {
  return FirstToken().Is(Token::Type::CLOSE_BRACKET) ||
         FirstToken().Is(Token::Type::BRACED_CONSTR_CLOSE_BRACKET) ||
         IsCloseParenOrBracket(FirstKeyword());
}

bool Chunk::IsFromKeyword() const {
  return absl::c_any_of(tokens_.WithoutComments(), [](const Token* t) {
    return t->GetKeyword() == "FROM";
  });
}

bool Chunk::IsJoin() const {
  return absl::c_any_of(tokens_.WithoutComments(), [](const Token* t) {
    return t->GetKeyword() == "JOIN";
  });
}

bool Chunk::IsWithOffsetAsClause() const {
  return absl::c_any_of(tokens_.WithoutComments(), [](const Token* t) {
    return t->Is(Token::Type::WITH_OFFSET_AS_CLAUSE);
  });
}

bool Chunk::IsTypeDeclarationStart() const {
  if (tokens_.WithoutComments().size() < 2 || LastKeyword() != "<") {
    return false;
  }
  const absl::string_view second_to_last = NonCommentKeyword(-2);
  return CanBeTypeDeclarationStart(second_to_last);
}

bool Chunk::IsTypeDeclarationEnd() const {
  return absl::c_any_of(tokens_.WithoutComments(), [](const Token* t) {
    return t->IsCloseAngleBracket();
  });
}

bool Chunk::CanBePartOfExpression() const {
  return !Empty() && LastKeyword() != "," &&
         !FirstToken().Is(Token::Type::BRACED_CONSTR_FIELD) &&
         googlesql::formatter::internal::CanBePartOfExpression(FirstToken());
}

bool Chunk::StartsWithChainableOperator() const {
  return !Empty() && IsChainableOperator(FirstToken());
}

bool Chunk::StartsWithRepeatableOperator() const {
  return !Empty() && IsRepeatableOperator(FirstToken());
}

bool Chunk::StartsWithChainableBooleanOperator() const {
  return !Empty() && IsChainableBooleanOperatorKeyword(FirstKeyword());
}

bool Chunk::EndsWithMacroCall() const {
  const std::vector<Token*> tokens = tokens_.WithoutComments();
  for (auto it = tokens.crbegin(); it != tokens.crend(); ++it) {
    const Token* token = *it;
    if (token->IsMacroCall()) {
      return true;
    }
    if (!token->Is(Token::Type::COMPLEX_TOKEN_CONTINUATION) &&
        token->GetKeyword() != "(" && token->GetKeyword() != ")") {
      return false;
    }
  }
  return false;
}

Chunk* Chunk::JumpToTopOpeningBracketIfAny() {
  Chunk* matching_chunk = MatchingOpeningChunk();
  while (matching_chunk != nullptr &&
         matching_chunk->HasMatchingOpeningChunk()) {
    matching_chunk = matching_chunk->MatchingOpeningChunk();
  }
  return matching_chunk == nullptr ? this : matching_chunk;
}

void Chunk::SetMatchingOpeningChunk(Chunk* matching_chunk) {
  ABSL_DCHECK_NE(this, matching_chunk);
  if (matching_chunk != this) {
    matching_open_chunk_ = matching_chunk;
  }
}

void Chunk::SetMatchingClosingChunk(Chunk* matching_chunk) {
  ABSL_DCHECK_NE(this, matching_chunk);
  if (matching_chunk != this) {
    matching_close_chunk_ = matching_chunk;
  }
}

int Chunk::GetLineBreaksBefore() const {
  return Empty() ? 0 : tokens_.All().front()->GetLineBreaksBefore();
}

int Chunk::GetLineBreaksAfter() const {
  return Empty() ? 0 : tokens_.All().back()->GetLineBreaksAfter();
}

bool ShouldNeverBeFollowedByNewline(const Token& token) {
  // "*" and "=" are handled specially in function IsPartOfSameChunk below:
  //   -- "*" is missing because it CAN be followed by new line, for example in
  //      "SELECT * FROM potato".
  //   -- "=" is missing because it might be separate from the subsequent token,
  //      in cases where it ends a fusible group, e.g. ("SET foo =", "3").
  //   -- "<" and ">" are missing, because the formatting is different when
  //      these are part of type declaration, e.g., "STRUCT<INT64>".
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>({
      "!=",   "&",       "+",       "-",      ".",     "/",     "<<",    "<=",
      ">=",   ">>",      "@",       "@@",     "^",     "|",     "||",    "~",
      "AND",  "BETWEEN", "CROSS",   "DEFINE", "FULL",  "IN",    "INNER", "LEFT",
      "LIKE", "NOT",     "OPTIONS", "OR",     "OUTER", "RIGHT",
  });

  // Some tokens should not be followed by a line break only if they are used
  // as top level keywords.
  static const auto* only_if_top_level = new googlesql_base::flat_set<absl::string_view>(
      {"INSERT", "LANGUAGE", "LOAD", "MODULE", "RUN", "SHOW", "SOURCE"});
  return allowed->contains(token.GetKeyword()) ||
         token.Is(Token::Type::COMPARISON_OPERATOR) ||
         (token.Is(Token::Type::TOP_LEVEL_KEYWORD) &&
          only_if_top_level->contains(token.GetKeyword()));
}

bool ShouldNeverBePrecededByNewline(absl::string_view keyword) {
  // DESC is also used as a short form of DESCRIBE in statements like
  // "DESCRIBE TABLE Foo". There, DESC is always the first token in the
  // statement, so it will not be fused with a previous one.
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>(
      {".", ",", ":", ";", "ASC", "BY", "DESC", "->"});
  return allowed->contains(keyword);
}

bool Chunk::ShouldNeverBeFollowedByNewline() const {
  if (EndsWithSingleLineComment()) {
    return false;
  }
  return internal::ShouldNeverBeFollowedByNewline(LastToken());
}

void Chunk::UpdateUnaryOperatorTypeIfNeeded(const Chunk* const previous_chunk,
                                            const Token* const previous_token) {
  const std::vector<Token*>& tokens = tokens_.WithoutComments();
  if (tokens.empty()) {
    return;
  }
  const int current_token_idx = static_cast<int>(tokens.size()) - 1;
  Token& current_token = *tokens[current_token_idx];
  if (current_token.Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) {
    return;
  }

  if (current_token_idx > 0 &&
      tokens[current_token_idx - 1]->Is(Token::Type::UNARY_OPERATOR) &&
      !googlesql::formatter::internal::CanBePartOfExpression(current_token)) {
    // Previous token is marked as unary operator, check if the current token
    // can be unary operand.
    // Note that previous token in current chunk might be different from
    // `previous_token` input argument.
    tokens[current_token_idx - 1]->SetType(Token::Type::UNKNOWN);
  }

  if (!CanBeUnaryOperator(current_token.GetKeyword())) {
    return;
  }
  // Current token can be unary operator, check whether the previous token is
  // part of the same expression (e.g., "a - b").
  if (previous_token == nullptr) {
    // This is the first meaningful token in the stream -> token is unary.
    current_token.SetType(Token::Type::UNARY_OPERATOR);
    return;
  }

  const absl::string_view previous_keyword = previous_token->GetKeyword();
  const Chunk& chunk_with_previous_token =
      (current_token_idx > 0 || previous_chunk == nullptr) ? *this
                                                           : *previous_chunk;
  if (!chunk_with_previous_token.CanBePartOfExpression() ||
      IsChainableOperator(*previous_token) ||
      IsOpenParenOrBracket(previous_keyword) || previous_keyword == "~" ||
      previous_keyword == "=>" || previous_keyword == "->") {
    current_token.SetType(Token::Type::UNARY_OPERATOR);
  }
}

// Adjusts `token_index` to point to the next non-comment token.
// Returns false if there are no such tokens left inside `tokens`.
bool FindNextNonCommentToken(const std::vector<Token*>& tokens,
                             int* token_index) {
  int next_index = *token_index + 1;
  while (next_index < tokens.size() && tokens[next_index]->IsComment()) {
    ++next_index;
  }
  *token_index = next_index;
  return next_index < tokens.size();
}

// Returns true if the token at `token_index` is an operator that can be fused
// with the `left_operand`. We allow fusing only if
// * left operand is not a complex expression with parentheses and is short;
// * operator is followed by an opening parenthesis, for instance "+ (". There
//   might be a function call after the operator: "* SUM(...)";
// * operator is not AND, OR or BETWEEN;
// * parentheses are not followed by another repeatable operator, otherwise
//   fusing makes indentation inconsistent:
//    first + (second - third)  # this "+" should be aligned with the next one.
//      + (fourth - fifth)
bool TokenIsOperatorThatCanBeFusedWithLeftOperand(
    const Chunk& left_operand, const std::vector<Token*>& tokens,
    int token_index) {
  // Check that left operand can be fused with the operator.
  if (left_operand.Empty() || left_operand.ClosesParenOrBracketBlock() ||
      left_operand.OpensParenOrBracketBlock() ||
      (left_operand.StartsWithRepeatableOperator() &&
       !IsChainableBooleanOperatorKeyword(left_operand.FirstKeyword())) ||
      !CanBePartOfExpression(left_operand.LastToken())) {
    return false;
  }

  // Check that the next token is a chainable operator.
  const Token* token = tokens[token_index];
  if (!IsChainableOperator(*token)) {
    return false;
  }

  // AND, OR and BETWEEN operators cannot be fused with lhs.
  if (IsChainableBooleanOperatorKeyword(token->GetKeyword()) ||
      token->GetKeyword() == "BETWEEN") {
    return false;
  }

  int next_index = token_index;
  if (!FindNextNonCommentToken(tokens, &next_index)) {
    return false;
  }

  if (tokens[next_index]->GetKeyword() == ">" &&
      tokens[next_index]->Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) {
    // This is the second half of a broken `<>` or `>>` operator. Skip to the
    // next real token.
    if (!FindNextNonCommentToken(tokens, &next_index)) {
      return false;
    }
  }

  // If the operator is IS, allow "IS NOT(".
  if (token->GetKeyword() == "IS" &&
      tokens[next_index]->GetKeyword() == "NOT") {
    if (!FindNextNonCommentToken(tokens, &next_index)) {
      return false;
    }
  }

  // If operator starts with NOT, allow "NOT IN (" and "NOT LIKE (".
  if (token->GetKeyword() == "NOT") {
    token = tokens[next_index];
    if (token->GetKeyword() != "IN" && token->GetKeyword() != "LIKE") {
      return false;
    }
    if (!FindNextNonCommentToken(tokens, &next_index)) {
      return false;
    }
  }

  // If the operator is IN, allow "IN UNNEST(".
  if (token->GetKeyword() == "IN" &&
      tokens[next_index]->GetKeyword() == "UNNEST") {
    if (!FindNextNonCommentToken(tokens, &next_index)) {
      return false;
    }
  }

  // Allow a function call after operator, e.g., "* SUM(...)".
  if (tokens[next_index]->MayBeIdentifier()) {
    if (!FindNextNonCommentToken(tokens, &next_index)) {
      return false;
    }
  }

  // There should be an opening parenthesis after operator.
  if ((tokens[next_index]->GetKeyword() != "(" &&
       tokens[next_index]->GetKeyword() != "[") ||
      tokens[next_index]->Is(Token::Type::OPEN_PROTO_EXTENSION_PARENTHESIS)) {
    return false;
  }

  // Left operand must be short, otherwise, the trailing operator becomes less
  // visible at the end of the line.
  if (left_operand.PrintableLength(false) >
      kMaxLeftOperandLengthFusibleWithOperator) {
    return false;
  }

  next_index = FindMatchingClosingBracket(tokens, next_index);

  if (!FindNextNonCommentToken(tokens, &next_index)) {
    return true;
  }

  // We don't allow 'operator (' to be fused, if parentheses are followed by
  // another repeatable operator, since this might confuse the indentation if
  // the following operator has same or higher precedence. Exception is boolean
  // operators which normally appear around comparison operators in where
  // clause: these are known to have lower precedence, e.g.:
  // WHERE
  //   foo = (
  //     1 + 2)
  //   AND bar IN (
  //     3, 4)
  return IsChainableBooleanOperatorKeyword(tokens[next_index]->GetKeyword()) ||
         !IsRepeatableOperatorKeyword(tokens[next_index]->GetKeyword());
}

// Returns true if `previous_keyword` and the `next_token` form a date literal.
bool IsDateOrTimeLiteral(absl::string_view previous_keyword,
                         const Token& next_token) {
  static const auto* kDateOrTimeLiteralQualifiers =
      new googlesql_base::flat_set<absl::string_view>(
          {"DATE", "DATETIME", "TIME", "TIMESTAMP"});
  return !previous_keyword.empty() && next_token.IsValue() &&
         next_token.GetValue().type_kind() == TYPE_STRING &&
         kDateOrTimeLiteralQualifiers->contains(previous_keyword);
}

// Returns true if token in <tokens> at <token_index> should be part of <chunk>.
bool IsPartOfSameChunk(const Chunk& chunk, const std::vector<Token*>& tokens,
                       int previous_non_comment_token_index, int token_index) {
  if (chunk.IsCommentOnly()) {
    if (chunk.Tokens().All().back()->IsMultiline()) {
      // Chunk is a multiline comment - never put anything after it.
      return false;
    }
    if (chunk.GetLineBreaksAfter() == 0) {
      // Chunk contains inline comment on the same line with the current token.
      return true;
    }
  }
  if (token_index > 0 && tokens[token_index - 1]->IsComment()) {
    if (tokens[token_index - 1]->Is(Token::Type::STRING_ANNOTATION_COMMENT) &&
        tokens[token_index]->Is(Token::Type::OPEN_BRACKET)) {
      // Group together annotation comment and the opening quotes of the
      // string literal, e.g.: /*sql*/'''
      return true;
    } else if (tokens[token_index - 1]->GetLineBreaksAfter() > 0) {
      // Previous token is either a stand-alone or end-of-line comment.
      return false;
    }
  }
  // All comment-related cases are handled above. If previous index is < 0 it
  // means there are no tokens before.
  if (previous_non_comment_token_index < 0) {
    return false;
  }

  // Shortcuts for previous and current tokens and keywords.
  Token* current_token = tokens[token_index];
  const absl::string_view current = current_token->GetKeyword();
  Token* previous_token = tokens[previous_non_comment_token_index];
  const absl::string_view previous = previous_token->GetKeyword();

  // If the token is a comment, then if it is on the same line, it's part of
  // the chunk, otherwise no.
  if (current_token->IsComment()) {
    return current_token->GetLineBreaksBefore() == 0 &&
           // Annotation comments like /*sql*/ should be attached to the next
           // token not to the previous one.
           !current_token->Is(Token::Type::STRING_ANNOTATION_COMMENT) &&
           // Special case: '/*arg_name=*/value' should be attached to the
           // value, not to the previous token.
           !IsArgumentNameInlineComment(*current_token);
  }

  if (current_token->Is(Token::Type::CHAINED_METHOD_DOT_SEPARATOR)) {
    // The dot separator for a chained method call always starts a new chunk.
    // This allows for a potential line break before the method call.
    return false;
  }
  if (current_token->Is(Token::Type::ASSIGNMENT_OPERATOR)) {
    // For OPTIONS '=' assignments, fuse '=' with the option parameter.
    return true;
  }
  if (previous_token->Is(Token::Type::ASSIGNMENT_OPERATOR)) {
    int next_token = token_index + 1;
    while (next_token < tokens.size() && tokens[next_token]->IsComment()) {
      ++next_token;
    }
    if (next_token == tokens.size() ||
        tokens[next_token]->GetKeyword() == "," ||
        IsCloseParenOrBracket(tokens[next_token]->GetKeyword()) ||
        IsOpenParenOrBracket(tokens[next_token]->GetKeyword())) {
      // In the case of a single-token option value, fuse the value with the
      // option assignment even if line length is exceeded; this makes it easier
      // to do mass find/replace with options values.  Note that we do not fuse
      // multitoken values, or values with attached inline comments.
      //
      // Example:
      //   path = '....<long_path>....',
      // Is preferable over:
      //   path =
      //     '....<long_path>....',
      return true;
    }
    return false;
  }

  // In proto constructors colon acts similar to the assignment operator:
  // field_name: value
  if (previous_token->Is(Token::Type::BRACED_CONSTR_COLON)) {
    return current_token->Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET);
  } else if (current_token->Is(Token::Type::BRACED_CONSTR_COLON)) {
    return true;
  }
  // Otherwise, colons may be inside identifiers (not part of GoogleSQL
  // standard).
  if (previous_token->GetKeyword() == ":") {
    return true;
  }

  // Never combine pipe operator with anything else.
  if (current == "|>") {
    return false;
  }
  if (previous == "|>") {
    current_token->SetType(Token::Type::TOP_LEVEL_KEYWORD);
    return false;
  }

  // For module declaration statements and imports, we basically ignore the line
  // length limit, so everything between before the ";" is a single chunk.
  if (chunk.IsModuleDeclaration() || chunk.IsImport()) {
    if (previous == ";") {
      return false;
    }
    // If a user has a missing semicolon, try to detect that the next token
    // is a beginning of a new statement.
    if (SpaceBetweenTokensInInput(*previous_token, *current_token) &&
        IsTopLevelClauseKeyword(*current_token)) {
      return false;
    }
    return true;
  }

  // A set operator (e.g. 'UNION ALL') is always its own chunk.
  if (chunk.IsSetOperator() || IsSetOperatorToken(*current_token)) {
    return false;
  }

  // Fuse `WITH OFFSET` when `WITH` isn't a top-level keyword.  In addition,
  // mark both tokens as `WITH_OFFSET_AS_CLAUSE` so we can further fuse
  // `[AS] <identifier>` (noting that `AS` is optional).
  if (previous == "WITH" &&
      !previous_token->Is(Token::Type::TOP_LEVEL_KEYWORD) &&
      current == "OFFSET") {
    previous_token->SetType(Token::Type::WITH_OFFSET_AS_CLAUSE);
    current_token->SetType(Token::Type::WITH_OFFSET_AS_CLAUSE);
    return true;
  }
  // Fuse `OFFSET [AS]` with the next token, if we're in a `WITH OFFSET AS` join
  // condition.
  if ((previous == "OFFSET" || previous == "AS") &&
      previous_token->Is(Token::Type::WITH_OFFSET_AS_CLAUSE) &&
      (current == "AS" || current_token->MayBeIdentifier())) {
    current_token->SetType(Token::Type::WITH_OFFSET_AS_CLAUSE);
    return true;
  }

  // Never fuse anything with the macro name in DEFINE MACRO statement.
  if (previous_token->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT)) {
    return false;
  }

  if (current_token->Is(Token::Type::COMPLEX_TOKEN_CONTINUATION) ||
      current_token->Is(Token::Type::TABLE_NAME_IN_DEFINE_STMT)) {
    return true;
  }

  if (ShouldNeverBeFollowedByNewline(*previous_token)) {
    return true;
  }
  if (ShouldNeverBePrecededByNewline(current)) {
    return true;
  }
  if (previous == ",") {
    return false;
  }
  if (previous == "END") {
    return false;
  }

  if (TokenIsOperatorThatCanBeFusedWithLeftOperand(chunk, tokens,
                                                   token_index)) {
    return true;
  }

  // Handle parentheses.  Note special handling for extensions within a proto
  // path.
  if (previous == "(") {
    return previous_token->Is(Token::Type::OPEN_PROTO_EXTENSION_PARENTHESIS) ||
           current == ")";
  }
  if (previous == "[") {
    return current == "]";
  }
  if (previous == "{") {
    return current == "}";
  }
  if (current == "(") {
    return (previous != ")" ||
            previous_token->Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) &&
           CanBeFusedWithOpenParenOrBracket(chunk);
  }
  if (current == ")") {
    return current_token->Is(Token::Type::CLOSE_PROTO_EXTENSION_PARENTHESIS);
  }
  if (current == "[" || current == "{" ||
      current_token->Is(Token::Type::OPEN_BRACKET)) {
    return CanBeFusedWithOpenParenOrBracket(chunk);
  }
  if (current_token->IsCloseAngleBracket()) {
    // Type declarations have closing bracket always attached to the previous
    // token.
    return true;
  }
  if (current == "]" || current == "}" ||
      current_token->Is(Token::Type::CLOSE_BRACKET)) {
    return false;
  }

  // * is tricky. It's the multiplication operator, but it can also be part of
  // various different "SELECT *", "SELECT a.*" etc.
  if (current == "*") {
    return previous == ".";
  }
  if (previous == "*") {
    if (current_token->Is(Token::Type::SELECT_STAR_MODIFIER)) {
      // For `SELECT * EXCEPT` and `SELECT * REPLACE`, the `EXCEPT`/`REPLACE`
      // keywords should be in an indented child node, not part of the same
      // chunk.
      return false;
    }
    return !IsTopLevelClauseKeyword(*current_token);
  }

  // = is also tricky, as it doesn't join in the next token if it ends a
  // fusible group (e.g. 'SET foo =', '3').
  if (previous == "=") {
    return !chunk.IsTopLevelClauseChunk();
  }

  // Special handling for <> signs, since these could be both comparison
  // operators and part of a type declaration, like ARRAY<INT64>.
  if (previous == ">") {
    if (previous_token->Is(Token::Type::CLOSE_BRACKET)) {
      // Group ">" together in a nested type declaration,
      // like "STRUCT<ARRAY<INT64>>".
      return current == ">";
    }
    if (previous_token->Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) {
      // This is the second half of a broken `<>` or `>>` operators. Look back
      // one more token to figure out which.
      ABSL_DCHECK_GE(previous_non_comment_token_index, 1);
      previous_token = tokens[previous_non_comment_token_index - 1];
      return previous_token->GetKeyword() == ">";
    }
    // Arithmetic expression like "> 1".
    return true;
  }
  if (previous == "<") {
    // Group with previous "<" only if it is not a type declaration.
    return !previous_token->Is(Token::Type::OPEN_BRACKET);
  }

  // Fuse multiline string literals with the previous token, e.g.:
  // AS """
  //    multiline
  //    string"""
  if (current_token->IsValue() && current_token->IsMultiline()) {
    return !IsTopLevelClauseKeyword(*previous_token);
  }

  // AS is another multi-purpose keyword. It can be used to rename, change type
  // or define a function.
  if (previous == "AS") {
    return !IsTopLevelClauseKeyword(*current_token);
  }

  if (IsDateOrTimeLiteral(previous, *current_token)) {
    return true;
  }

  // Named arguments, e.g.: "Foo(param1 => 1, param2 => TRUE)"
  if (current == "=>") {
    return true;
  }

  if (current_token->IsValue() &&
      current_token->GetValue().type_kind() == TYPE_STRING) {
    return previous == "MATERIALIZE";
  }

  // Java gORM: (broken link)
  if (IsBindKeyword(chunk.FirstKeyword()) &&
      chunk.FirstToken().Is(Token::Type::TOP_LEVEL_KEYWORD) &&
      SpaceBetweenTokensInInput(*previous_token, *current_token)) {
    return true;
  }

  // These could be fused in fusible_tokens.cc, but there is a conflict with
  // other fusible tokens that start with DEFAULT and have to be DDL_KEYWORDs.
  // fusible_tokens.cc doesn't support at the moment sequences that start from
  // the same keyword but should have different type.
  if (current == "COLLATE") {
    return previous == "DEFAULT";
  }

  // Fuse hints like "@<number>" with keywords, e.g., "JOIN @100"
  if (current == "@" && previous_token->IsKeyword()) {
    return token_index + 1 >= tokens.size() ||
           tokens[token_index + 1]->IsValue();
  }

  if (current_token->Is(Token::Type::LEGACY_DEFINE_TABLE_BODY)) {
    return true;
  }

  return false;
}

// Annotates all "<" and ">" tokens, which are part of type declarations,
// e.g.: "ARRAY<STRING>"
void MarkAllTypeDeclarations(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  std::vector<int> type_starts;
  for (int t = 1; t < tokens.size(); ++t) {
    const absl::string_view current = tokens[t]->GetKeyword();
    const absl::string_view previous = tokens[t - 1]->GetKeyword();
    if (current == "<" && CanBeTypeDeclarationStart(previous)) {
      // Found a potential type declaration start.
      if (t > 1 && tokens[t - 2]->GetKeyword() == "RETURNS") {
        // RETURNS is always followed by a type. Mark this token as type
        // declaration.
        tokens[t]->SetType(Token::Type::OPEN_BRACKET);
      }
      // Advance iterator past "<", otherwise the check below notices an invalid
      // "<" inside a type declaration.
      type_starts.push_back(t++);
    } else if (!type_starts.empty() && previous == ">" &&
               (tokens[type_starts.back()]->Is(Token::Type::OPEN_BRACKET) ||
                CanBeTypeDeclarationEnd(previous, current))) {
      // Found a full type declaration.
      tokens[type_starts.back()]->SetType(Token::Type::OPEN_BRACKET);
      tokens[t - 1]->SetType(Token::Type::CLOSE_BRACKET);
      type_starts.pop_back();
    } else if (IsTopLevelClauseKeyword(*tokens[t - 1]) ||
               IsChainableOperator(*tokens[t - 1])) {
      // TODO: b/298028709 - Allow keywords in type declarations.
      // Found a token that cannot be inside a type declaration. Drop all type
      // declaration starts found so far.
      type_starts.clear();
    }
  }
  if (!type_starts.empty() &&
      CanBeTypeDeclarationEnd(tokens.back()->GetKeyword(), "")) {
    // Declaration end at the very end of the input.
    tokens[type_starts.back()]->SetType(Token::Type::OPEN_BRACKET);
    tokens.back()->SetType(Token::Type::CLOSE_BRACKET);
  }
}

// Annotates WITH keywords if the WITH opens a top-level clause block (e.g.,
// 'WITH <table_name> AS ...' (top-level), vs. 'WITH OFFSET' in a FROM clause
// (not top-level).
void MarkAllTopLevelWithKeywords(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  if (tokens.empty()) {
    return;
  }

  static const auto* may_start_query =
      new googlesql_base::flat_set<absl::string_view>({"AS", "EXPLAIN", "("});

  bool may_be_start_of_query = true;
  for (int t = 0; t < tokens.size(); ++t) {
    if (may_be_start_of_query) {
      if (tokens[t]->GetKeyword() == "WITH") {
        tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
        may_be_start_of_query = false;
        continue;
      } else if (tokens[t]->IsMacroCall() && t + 1 < tokens.size()) {
        // Allow a query to start with a macro call.
        if (IsOpenParenOrBracket(tokens[t + 1]->GetKeyword())) {
          const int after_macro = FindMatchingClosingBracket(tokens, t + 1) + 1;
          if (after_macro < tokens.size() &&
              tokens[after_macro]->GetKeyword() == "WITH") {
            tokens[after_macro]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
          }
          may_be_start_of_query = false;
        }
        continue;
      } else if (tokens[t]->Is(Token::Type::CURLY_BRACES_TEMPLATE)) {
        // Allow a query to start with a curly braces template.
        continue;
      }
      may_be_start_of_query = false;
    }
    if (tokens[t]->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT) ||
        may_start_query->contains(tokens[t]->GetKeyword()) ||
        tokens[t]->GetKeyword() == "(") {
      may_be_start_of_query = true;
    }
    // Special handling for INSERT statements. The grammar allows multiple
    // syntaxes, so we just look for a query start after INSERT, if any.
    if (tokens[t]->Is(Token::Type::TOP_LEVEL_KEYWORD) &&
        tokens[t]->GetKeyword() == "INSERT") {
      static const auto* stop_search_at = new googlesql_base::flat_set<absl::string_view>(
          {"SELECT", "VALUES", "WITH", ";"});
      while (++t < tokens.size()) {
        if (stop_search_at->contains(tokens[t]->GetKeyword()) &&
            !tokens[t]->IsOneOf(
                {Token::Type::COMPLEX_TOKEN_CONTINUATION,
                 Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT})) {
          if (tokens[t]->GetKeyword() == "WITH") {
            tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
          }
          break;
        }
      }
    }
  }
}

// Annotates privilege type keywords within GRANT/REVOKE statements.  For
// example, in the statement:
//
//   GRANT SELECT, INSERT (col1, col2) ON TABLE Foo To 'foo@google.com'
//
// the keywords SELECT and INSERT are privilege type keywords.
void MarkAllPrivilegeTypeKeywords(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();

  int statement_start = 0;
  if (tokens.size() > 3 &&
      tokens[2]->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT)) {
    statement_start = 3;
  }
  if (tokens.empty() || !(tokens[statement_start]->GetKeyword() == "GRANT" ||
                          tokens[statement_start]->GetKeyword() == "REVOKE")) {
    return;
  }
  int paren_depth = 0;
  for (int i = statement_start + 1; i < tokens.size(); ++i) {
    Token* current_token = tokens[i];
    if (current_token->GetKeyword() == "ON") {
      return;
    }

    Token* previous_token = tokens[i - 1];
    if (current_token->GetKeyword() == "(") {
      ++paren_depth;
    } else if (current_token->GetKeyword() == ")") {
      --paren_depth;
    } else if (paren_depth == 0 && current_token->GetKeyword() != "," &&
               (i == statement_start + 1 ||
                previous_token->GetKeyword() == "," ||
                previous_token->Is(Token::Type::PRIVILEGE_TYPE_KEYWORD))) {
      current_token->SetType(Token::Type::PRIVILEGE_TYPE_KEYWORD);
    }
  }
}

// Annotates `EXCEPT` and `REPLACE` keywords when they are used to modify the
// columns returns by `SELECT *` or similar.
void MarkAllSelectStarModifiers(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int t = 1; t + 1 < tokens.size(); ++t) {
    const absl::string_view previous = tokens[t - 1]->GetKeyword();
    const absl::string_view current = tokens[t]->GetKeyword();
    const absl::string_view next = tokens[t + 1]->GetKeyword();

    // If we detect the sequence {"*", "<EXCEPT or REPLACE>", "("}, mark the
    // middle token as a "select * modifier".
    if ((current == "EXCEPT" || current == "REPLACE") && previous == "*" &&
        next == "(") {
      tokens[t]->SetType(Token::Type::SELECT_STAR_MODIFIER);
      tokens[t]->MarkUsedAsKeyword();

      // Look ahead to the corresponding close parenthesis and see if we have
      // another chained {"<EXCEPT or REPLACE>", "("} (there can be at most 2).
      int maybe_another_t = 1 + FindMatchingClosingBracket(tokens, t + 1);
      if (maybe_another_t + 1 < tokens.size() &&
          (tokens[maybe_another_t]->GetKeyword() == "EXCEPT" ||
           tokens[maybe_another_t]->GetKeyword() == "REPLACE") &&
          tokens[maybe_another_t + 1]->GetKeyword() == "(") {
        tokens[maybe_another_t]->SetType(Token::Type::SELECT_STAR_MODIFIER);
        tokens[maybe_another_t]->MarkUsedAsKeyword();
      }
    }
  }
}

// Marks all tokens that represent a name inside DEFINE TABLE <name> and DEFINE
// MACRO <name> statements.
void MarkAllMacroAndTableDefinitions(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int t = 2; t < tokens.size(); ++t) {
    if (tokens[t - 2]->GetKeyword() == "DEFINE" &&
        tokens[t]->MayBeIdentifier()) {
      const absl::string_view define_type = tokens[t - 1]->GetKeyword();
      Token::Type type = Token::Type::UNKNOWN;
      if (define_type == "TABLE") {
        type = Token::Type::TABLE_NAME_IN_DEFINE_STMT;
      } else if (define_type == "MACRO") {
        type = Token::Type::MACRO_NAME_IN_DEFINE_STMT;
      }
      if (type != Token::Type::UNKNOWN) {
        tokens[t]->SetType(type);
        while (++t < tokens.size() &&
               tokens[t]->MayBeIdentifierContinuation(*tokens[t - 1]) &&
               !SpaceBetweenTokensInInput(*tokens[t - 1], *tokens[t])) {
          tokens[t]->SetType(type);
        }
      }
      t += 2;
    }
  }
}

// A helper function for 'MarkAllAssignmentOperators'; iterates through tokens
// starting from `index` and returns the position of a bracket that starts a
// parameter list.
int FindNextParamList(const std::vector<Token*>& tokens, int index) {
  if (index < 1) {
    index = 1;
  }
  for (; index < tokens.size(); ++index) {
    // CASE: 'OPTIONS (a = b, c = d)'.
    if (tokens[index - 1]->GetKeyword() == "OPTIONS" &&
        tokens[index]->GetKeyword() == "(") {
      // Mark OPTIONS as keyword.
      tokens[index - 1]->MarkUsedAsKeyword();
      return index;
    }

    // CASE: '@{a = b, c = d}'.
    if (tokens[index - 1]->GetKeyword() == "@" &&
        tokens[index]->GetKeyword() == "{") {
      return index;
    }

    // CASE: 'DEFINE TABLE <identifier> (a = b, c = d)'.
    if (index >= 3 &&
        tokens[index - 1]->Is(Token::Type::TABLE_NAME_IN_DEFINE_STMT)) {
      // We've already consumed 'DEFINE TABLE'; now, we need to consume all
      // tokens in <identifier> (which may be dot-delimited e.g. 'foo.bar').
      while (tokens[index]->MayBeIdentifierContinuation(*tokens[index - 1])) {
        if (++index >= tokens.size()) {
          return index;
        }
      }

      if (tokens[index]->GetKeyword() == "(") {
        return index;
      }
    }

    // CASE: 'RUN script::name(a = b)'.
    if (index == 1 && tokens.size() > 2 && tokens[0]->GetKeyword() == "RUN" &&
        tokens[index]->MayBeStartOfIdentifier()) {
      ++index;
      while (tokens[index]->MayBeIdentifierContinuation(*tokens[index - 1])) {
        if (++index >= tokens.size()) {
          return index;
        }
      }

      if (tokens[index]->GetKeyword() == "(") {
        return index;
      }
    }

    // CASE: '[sqltest_option = a]' and '[DEFAULT sqltest_option = a]'.
    if (index + 1 < tokens.size() && tokens[index - 1]->GetKeyword() == "[") {
      if (tokens[index + 1]->GetKeyword() == "=") return --index;
      if (tokens[index]->GetKeyword() == "DEFAULT") return index;
    }

    // CASE: '|> SET a = b, c = d'. (This is not strictly a parameter list,
    // but '=' here is used as assignment operator)
    if (tokens[index - 1]->GetKeyword() == "|>" &&
        tokens[index]->GetKeyword() == "SET") {
      return index;
    }
  }

  return index;
}

// Annotates '=' in parameter-value assignments.
void MarkAllAssignmentOperators(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  int bracket_depth = 0;
  int param_start = -1;
  for (int t = 1; t < tokens.size(); ++t) {
    if (bracket_depth == 0) {
      t = FindNextParamList(tokens, t);
      bracket_depth = 1;
      param_start = t + 1;
    } else if (t == param_start) {
      // Skip complex parameter names like `foo.bar`.
      while (t + 1 < tokens.size() &&
             tokens[t + 1]->MayBeIdentifierContinuation(*tokens[t])) {
        ++t;
      }
      // Parameter name may be a single reserved keyword. Mark it as identifier.
      // Keywords in complex identifiers (e.g., `table.select.all`) are marked
      // separately.
      if (t == param_start && tokens[t]->IsReservedKeyword()) {
        tokens[t]->SetType(Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT);
      }
      // Check the next token after the parameter name.
      if (t + 1 < tokens.size() && tokens[t + 1]->GetKeyword() == "=") {
        tokens[++t]->SetType(Token::Type::ASSIGNMENT_OPERATOR);
      }
    } else if (bracket_depth > 0) {
      // Count brackets to skip contents of nested brackets inside a parameter
      // list. Note that assignments are always at level=1 depth. (Nested
      // assignments are not possible.)
      const absl::string_view current_kwd = tokens[t]->GetKeyword();
      if (IsOpenParenOrBracket(current_kwd)) {
        ++bracket_depth;
      } else if (IsCloseParenOrBracket(current_kwd)) {
        --bracket_depth;
      } else if (bracket_depth == 1 && current_kwd == ",") {
        // Found next parameter start.
        param_start = t + 1;
      }
    }
  }
}

// Annotates proto extension parentheses in proto paths, e.g.
// 'metadata.(adeventslog.verify_metadata_ext).verified_v_state_metrics'.
void MarkAllProtoExtensionParentheses(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int i = 1; i < tokens.size(); ++i) {
    absl::string_view previous_kwd = tokens[i - 1]->GetKeyword();
    absl::string_view current_kwd = tokens[i]->GetKeyword();
    if (previous_kwd != "." || current_kwd != "(") continue;
    // We've found a ".(" sequence which potentially introduces an inline proto
    // extension.  Only mark the "(" as a proto extension opening parenthesis
    // if we can find the corresponding closing parenthesis.
    for (int j = i + 1; j < tokens.size(); ++j) {
      current_kwd = tokens[j]->GetKeyword();
      if (current_kwd == ")") {
        tokens[i]->SetType(Token::Type::OPEN_PROTO_EXTENSION_PARENTHESIS);
        tokens[j]->SetType(Token::Type::CLOSE_PROTO_EXTENSION_PARENTHESIS);
        i = j;  // We can advance 'i' to the end of the marked block.
        break;
      }
      if (current_kwd != "." && !tokens[j]->MayBeIdentifier()) {
        // We went past the current identifier, closing parenthesis is missing.
        i = j;
        break;
      }
    }
  }
}

// Annotates array brackets in proto paths, e.g.
// 'metadata.adeventslog[OFFSET(0)].verify_metadata_ext'.
void MarkAllProtoBrackets(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int i = 2; i < tokens.size(); ++i) {
    absl::string_view previous_kwd = tokens[i - 1]->GetKeyword();
    absl::string_view current_kwd = tokens[i]->GetKeyword();
    if (previous_kwd != "]" || current_kwd != ".") continue;
    // We've found a "]." sequence which indicates array brackets within proto.
    // Only mark if we find the opening bracket.
    // Only look for strings less than 5 tokens long, ignore anything "too
    // complex" (i.e. j >= i - 6).
    for (int j = i - 2; j >= std::max(0, i - 6); --j) {
      current_kwd = tokens[j]->GetKeyword();
      if (current_kwd == "[") {
        // Mark everything between the opening and closing bracket so that they
        // remain on the same line.
        for (int k = j; k < i; ++k) {
          tokens[k]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
        }
        break;
      } else if (current_kwd == "]" || current_kwd == ".") {
        // We went too far, no opening bracket or token pattern "too complex".
        break;
      }
    }
  }
}

// Annotates reserved keywords, which are part of complex identifier. For
// instance: "proto.field.select", "select::from::group".
void MarkAllKeywordsInComplexIdentifiers(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  Token* current_token = nullptr;
  for (int t = 1; t < static_cast<int>(tokens.size()) - 1; ++t) {
    current_token = tokens[t];
    if (current_token->GetImage() == "." || current_token->GetImage() == ":") {
      Token* prev = tokens[t - 1];
      if (prev->IsReservedKeyword()) {
        prev->SetType(Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT);
      }
      Token* next = tokens[t + 1];
      if (current_token->GetKeyword() == ":" && next->GetKeyword() != ":" &&
          prev->GetKeyword() != ":") {
        continue;
      }

      if (next->IsReservedKeyword()) {
        next->SetType(Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT);
      } else if (!SpaceBetweenTokensInInput(*current_token, *next) &&
                 // Curly braces have their own logic for fuzing with
                 // adjacent tokens if they are part of a complex token.
                 !next->Is(Token::Type::CURLY_BRACES_TEMPLATE)) {
        next->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
      }
    }
  }
}

// Annotates reserved keywords, which are used as:
// * @parameters and @@parameters
// * Gorm BIND statements.
void MarkAllKeywordsUsedAsParams(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  Token* current_token = nullptr;
  for (int t = 1; t < static_cast<int>(tokens.size()) - 1; ++t) {
    current_token = tokens[t];
    if (current_token->IsReservedKeyword()) {
      const absl::string_view previous = tokens[t - 1]->GetKeyword();
      if (previous.empty()) {
        continue;
      }
      if (previous[0] == '@' ||
          (IsBindKeyword(previous) &&
           tokens[t - 1]->Is(Token::Type::TOP_LEVEL_KEYWORD))) {
        current_token->SetType(Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT);
      }
    }
  }
}

// Annotates as comparison operators:
// * "AND" within BETWEEN..AND operator;
// * "NOT", which is part of "NOT BETWEEN", "NOT IN" or "NOT LIKE" operators.
// * "FROM", which is part of "IS DISTINCT FROM" operator.
void MarkBetweenAndNotFromAsComparisonOperators(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();

  for (int t = 0; t < tokens.size(); ++t) {
    const absl::string_view current_keyword = tokens[t]->GetKeyword();
    if (current_keyword == "NOT" && t + 1 < tokens.size()) {
      // Check if NOT starts a comparison operator.
      const absl::string_view next_keyword = tokens[t + 1]->GetKeyword();
      if (next_keyword == "BETWEEN" || next_keyword == "IN" ||
          next_keyword == "LIKE") {
        tokens[t]->SetType(Token::Type::COMPARISON_OPERATOR);
      }
    } else if (current_keyword == "BETWEEN") {
      // Mark AND after BETWEEN as a part of comparison operator.
      int brackets_depth = 0;
      for (int i = t + 1; i < tokens.size(); ++i) {
        if (brackets_depth == 0 && tokens[i]->GetKeyword() == "AND") {
          tokens[i]->SetType(Token::Type::COMPARISON_OPERATOR);
          break;
        }
        if (tokens[i]->GetKeyword() == "(") {
          brackets_depth++;
        } else if (tokens[i]->GetKeyword() == ")") {
          brackets_depth--;
        }
      }
    } else if (current_keyword == "DISTINCT" && t + 1 < tokens.size() &&
               t > 0) {
      absl::string_view maybe_is = tokens[t - 1]->GetKeyword();
      const absl::string_view next_keyword = tokens[t + 1]->GetKeyword();
      if (maybe_is == "NOT" && t > 1) {
        maybe_is = tokens[t - 2]->GetKeyword();
      }
      if (maybe_is == "IS" && next_keyword == "FROM") {
        // Mark FROM after IS DISTINCT or IS NOT DISTINCT as a part of
        // comparison operator.
        tokens[t + 1]->SetType(Token::Type::COMPARISON_OPERATOR);
      }
    }
  }
}

// Marks all tokens, which start a list expression. E.g. in "SELECT a, b;"
// SELECT starts a list. Note that this function is not marking complex type
// declarations as lists (e.g. "STRUCT<a INT64, b INT64>"), but this may be
// implemented later if needed.
void MarkAllTokensStartingAList(const TokensView& tokens_view) {
  std::vector<Token*> list_starters;
  for (Token* token : tokens_view.WithoutComments()) {
    const absl::string_view keyword = token->GetKeyword();
    if ((token->Is(Token::Type::UNKNOWN) && keyword == "SELECT") ||
        (!token->Is(Token::Type::OPEN_PROTO_EXTENSION_PARENTHESIS) &&
         IsOpenParenOrBracket(keyword))) {
      list_starters.push_back(token);
      continue;
    }
    if (list_starters.empty()) {
      continue;
    }
    if (IsTopLevelClauseKeyword(*token) &&
        list_starters.back()->GetKeyword() == "SELECT") {
      list_starters.pop_back();
      continue;
    } else if (IsCloseParenOrBracket(keyword) &&
               !token->Is(Token::Type::CLOSE_PROTO_EXTENSION_PARENTHESIS)) {
      const absl::string_view matching_open_bracket =
          CorrespondingOpenBracket(keyword);
      while (!list_starters.empty() &&
             list_starters.back()->GetKeyword() != matching_open_bracket) {
        list_starters.pop_back();
      }
      if (!list_starters.empty()) {
        list_starters.pop_back();
      }
      continue;
    }
    if (keyword == ",") {
      list_starters.back()->SetType(Token::Type::STARTS_LIST);
    }
  }
}

// Returns true if the given macro call token may be a disguised top level
// clause (or entire statement) inside a macro definition. For instance:
// DEFINE MACRO CALL_OTHER_MACRO
//   MACRO1($1)
//   MACRO2($2);
// To reduce the chance of false positives, we check if the macro call is the
// only thing on the line and has at most 2 spaces before it.
bool MacroCallMayBeATopLevelClause(
    const ParseLocationTranslator& location_translator,
    const std::vector<Token*>& tokens, int macro_call) {
  // Require no other tokens before the macro call.
  if (tokens[macro_call]->GetLineBreaksBefore() == 0) {
    return false;
  }
  // Require at most two spaces before (location is 1-based).
  const absl::StatusOr<std::pair<int, int>> macro_location =
      TranslateLocation(location_translator, *tokens[macro_call]);
  if (!macro_location.ok() || macro_location->second > 3) {
    return false;
  }

  int macro_end = macro_call;
  // Find the end of macro arguments.
  if (macro_call + 1 < tokens.size() &&
      tokens[macro_call + 1]->GetKeyword() == "(") {
    macro_end = FindMatchingClosingBracket(tokens, macro_call + 1);
  }

  if (macro_end < tokens.size() &&
      tokens[macro_end]->GetLineBreaksAfter() > 0) {
    // Nothing on the same line.
    return true;
  }

  if (macro_end + 1 < tokens.size()) {
    if (tokens[macro_end + 1]->GetKeyword() == ";") {
      return true;
    }
    // Check if the next non-comment token is on a different line.
    // This way we allow end of line comments after the macro (`tokens` vector
    // skips comment tokens).
    const absl::StatusOr<std::pair<int, int>> next_token_location =
        TranslateLocation(location_translator, *tokens[macro_end + 1]);
    if (next_token_location.ok() &&
        next_token_location->first != macro_location->first) {
      return true;
    }
  }

  return false;
}

// Marks the first token in the statement as a top level keyword.
// Some statements start with a non-reserved keyword; this function allows us to
// distinguish those from identifiers.
void MarkAllStatementStarts(
    const TokensView& tokens_view,
    const ParseLocationTranslator& location_translator) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  if (tokens.empty()) {
    return;
  }

  // NOTE: This list only contains non-reserved keywords, or keywords that can
  // appear elsewhere in a statement. For reserved keywords that are always
  // top-level, update the list in `IsTopLevelClauseKeyword`.
  // MATERIALIZE is a legacy syntax not supported by GoogleSQL.
  static const auto* allowed = new googlesql_base::flat_set<absl::string_view>({
      "ABORT",       "ALTER",        "ASSERT",          "BEGIN",
      "BIND",        "BIND_COLUMNS", "BIND_EXPRESSION", "BIND_ST_QUERY_CONFIG",
      "CALL",        "COMMIT",       "DELETE",          "DESCRIBE",
      "DROP",        "EXECUTE",      "EXPLAIN",         "EXPORT",
      "GRANT",       "IMPORT",       "INSERT",          "LOAD",
      "MATERIALIZE", "MODULE",       "RENAME",          "REVOKE",
      "ROLLBACK",    "RUN",          "SET_ALIAS_TYPE",  "SHOW",
      "SOURCE",      "START",        "TRUNCATE",        "UPDATE",
      "WITH",
  });
  Token* first_token = tokens.front();
  if (allowed->contains(first_token->GetKeyword()) ||
      first_token->IsMacroCall()) {
    first_token->SetType(Token::Type::TOP_LEVEL_KEYWORD);
  } else if (first_token->GetKeyword() == "SET") {
    first_token->SetType(Token::Type::SET_STATEMENT_START);
  }

  if (tokens.size() < 4 ||
      !tokens[2]->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT)) {
    return;
  }
  // Macro definitions may contain multiple statements not separated with
  // semicolons. Try to identify all of them. E.g.:
  // DEFINE MACRO SET_PARAMS
  //   SET Foo 1
  //   SET Bar 2;
  int t = 3;
  if (tokens[t]->GetKeyword() == "SET") {
    tokens[t]->SetType(Token::Type::SET_STATEMENT_START);
  } else if (allowed->contains(tokens[t]->GetKeyword())) {
    tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
  } else if (!IsTopLevelClauseKeyword(*tokens[t]) &&
             !tokens[t]->IsMacroCall()) {
    return;
  }

  // Try to find more statements.
  for (++t; t < tokens.size(); ++t) {
    // Skip expressions in parentheses.
    if (IsOpenParenOrBracket(tokens[t]->GetKeyword())) {
      t = FindMatchingClosingBracket(tokens, t);
      continue;
    }
    // Check only tokens that start a new line.
    if (tokens[t]->GetLineBreaksBefore() == 0) {
      continue;
    }
    if (allowed->contains(tokens[t]->GetKeyword())) {
      tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
    } else if (tokens[t]->IsMacroCall() &&
               MacroCallMayBeATopLevelClause(location_translator, tokens, t)) {
      tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
    } else if (tokens[t]->GetKeyword() == "SET") {
      tokens[t]->SetType(Token::Type::SET_STATEMENT_START);
    }
  }
}

// The macro arguments may be arbitrary strings, which are not necessarily a
// valid SQL. For instance, in "$LOAD_PROTO(path/to/file.proto);", the file path
// is not in quotes, so GoogleSQL tokenizer will assume each path part to be a
// separate token, as if it was division operation. To prevent formatter adding
// spaces where it shouldn't, we preserve the original input: formatter would
// split tokens inside a macro call only if there were spaces in the user input.
void MarkAllMacroCallsThatShouldBeFused(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int t = 0; t < static_cast<int>(tokens.size()) - 2; ++t) {
    if (tokens[t]->IsMacroCall() && tokens[t + 1]->GetKeyword() == "(" &&
        tokens[t + 2]->GetKeyword() != ")") {
      const int first_param = t + 2;
      const int call_end = FindMatchingClosingBracket(tokens, t + 1);
      // Check if the macro call contains spaces between arguments.
      bool space_found = false;
      for (int i = first_param + 1; i < call_end; ++i) {
        if (SpaceBetweenTokensInInput(*tokens[i - 1], *tokens[i])) {
          space_found = true;
          break;
        }
      }
      if (!space_found) {
        for (int i = first_param + 1; i < call_end; ++i) {
          tokens[i]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
        }
        // If there was no space also before the first argument, fuse
        // parentheses as well to make sure formatter never splits the macro
        // call.
        if (!SpaceBetweenTokensInInput(*tokens[first_param - 1],
                                       *tokens[first_param])) {
          tokens[first_param]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
          if (call_end < tokens.size()) {
            tokens[call_end]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
          }
        }
        t = call_end;
      }
    }
  }
}

// Often, macro body consists of a single item, which consists of multiple
// GoogleSQL tokens, for instance, an unquoted path:
// DEFINE MACRO PATH a/b/c.data;
// This function checks if the original user input had any whitespaces in the
// macro body. If not - it marks all tokens inside the body to be a single
// complex token that should never split on multiple lines.
void MarkAllMacroBodiesThatShouldBeFused(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  if (tokens.size() < 5 ||
      !tokens[2]->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT)) {
    return;
  }
  int macro_body_start = 3;
  int macro_body_end = static_cast<int>(tokens.size());
  if (IsOpenParenOrBracket(tokens[macro_body_start]->GetKeyword())) {
    macro_body_end = FindMatchingClosingBracket(tokens, macro_body_start);
    ++macro_body_start;
  }

  for (int t = macro_body_start + 1; t < macro_body_end; ++t) {
    if (tokens[t]->GetKeyword() == ";") {
      macro_body_end = t;
      break;
    }
    if (SpaceBetweenTokensInInput(*tokens[t - 1], *tokens[t])) {
      return;
    }
  }
  for (int t = macro_body_start + 1; t < macro_body_end; ++t) {
    tokens[t]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
  }
}

// GetParseTokens sometimes returns operators as multiple subsequent tokens
// instead of a single token. This function marks the trailing tokens of such
// operators as complex token continuations.
void MarkSqlTokensThatArePartOfBrokenOperator(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int t = 1; t < tokens.size(); ++t) {
    // We already marked '>' tokens that close type declarations. Otherwise
    // when two '>' occur without space they are the bitwise right-shift token,
    // and '<>' is the sql style not-equal operator.
    if (!tokens[t]->Is(Token::Type::CLOSE_BRACKET) &&
        tokens[t]->GetKeyword() == ">" &&
        !SpaceBetweenTokensInInput(*tokens[t - 1], *tokens[t])) {
      if (!tokens[t - 1]->Is(Token::Type::CLOSE_BRACKET) &&
          tokens[t - 1]->GetKeyword() == ">") {
        // This is a broken `>>` bitwise shift operator.
        tokens[t]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
      }
      if (!tokens[t - 1]->Is(Token::Type::OPEN_BRACKET) &&
          tokens[t - 1]->GetKeyword() == "<") {
        // This is a broken `<>` comparison operator.
        tokens[t - 1]->SetType(Token::Type::COMPARISON_OPERATOR);
        tokens[t]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
      }
    }
  }
}

// $MACRO calls, ${parameters} and various {templates} can appear anywhere in
// the query. We check if the original input had spaces to determine if they
// should be a part of the previous token.
// CREATE TABLE my_table_${date};     # ${date} is token continuation.
// CREATE TABLE my_table ${options};  # ${options} is individual token.
void MarkNonSqlTokensThatArePartOfComplexToken(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int t = 0; t < tokens.size(); ++t) {
    if (tokens[t]->IsMacroCall() ||
        tokens[t]->Is(Token::Type::CURLY_BRACES_TEMPLATE) ||
        (t + 1 < tokens.size() && tokens[t]->GetImage() == "%" &&
         tokens[t + 1]->Is(Token::Type::CURLY_BRACES_TEMPLATE))) {
      // Check if there is no space before the macro/parameter.
      if (t > 0 && !SpaceBetweenTokensInInput(*tokens[t - 1], *tokens[t]) &&
          // Never fuse with enclosing brackets.
          !IsOpenParenOrBracket(tokens[t - 1]->GetKeyword()) &&
          !tokens[t - 1]->Is(Token::Type::OPEN_BRACKET)) {
        tokens[t]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
      }
      if (t + 1 >= tokens.size()) {
        break;
      }

      int next_token = t + 1;
      if ((tokens[t]->GetImage() == "$" || tokens[t]->GetImage() == "%") &&
          tokens[next_token]->GetKeyword() == "{") {
        // "${param}" or "%{param}" - fuse "${" and "%{" and find the token
        // after '}'.
        tokens[next_token]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
        size_t end_of_param =
            std::min(static_cast<int>(tokens.size()) - 1,
                     FindMatchingClosingBracket(tokens, next_token));
        // Check if there are any spaces between curly braces {}. If no - fuse
        // all tokens inside.
        bool no_spaces = true;
        for (int i = next_token + 1; i <= end_of_param; ++i) {
          if (SpaceBetweenTokensInInput(*tokens[i - 1], *tokens[i])) {
            no_spaces = false;
            break;
          }
        }
        if (no_spaces) {
          while (++next_token <= end_of_param) {
            tokens[next_token]->SetType(
                Token::Type::COMPLEX_TOKEN_CONTINUATION);
          }
        }
      } else if (tokens[t]->IsMacroCall() &&
                 tokens[next_token]->GetKeyword() == "(") {
        // "$FOO(args)" - find the token after ')'.
        next_token = FindMatchingClosingBracket(tokens, next_token) + 1;
      }
      if (next_token >= tokens.size()) {
        continue;
      }

      // Check if there is no space after the macro/parameter.
      if (!SpaceBetweenTokensInInput(*tokens[next_token - 1],
                                     *tokens[next_token]) &&
          // Never fuse with enclosing brackets.
          !IsCloseParenOrBracket(tokens[next_token]->GetKeyword()) &&
          !IsOpenParenOrBracket(tokens[next_token]->GetKeyword()) &&
          !tokens[next_token]->IsOneOf(
              {Token::Type::CLOSE_BRACKET, Token::Type::OPEN_BRACKET,
               // Do not overwrite the type if the next token is curly brace
               // parameter - it will be handled by the next iteration of this
               // loop.
               Token::Type::CURLY_BRACES_TEMPLATE})) {
        tokens[next_token]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
      }
    }
  }
}

void MarkUnquotedPaths(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  if (tokens.size() < 4) {
    return;
  }
  int statement_start = 0;
  if (tokens.size() > 6 &&
      tokens[2]->Is(Token::Type::MACRO_NAME_IN_DEFINE_STMT)) {
    statement_start = 3;
  }
  int path_start = static_cast<int>(tokens.size());
  // "USE DATABASE /db/path;"
  if (tokens[statement_start]->GetKeyword() == "USE" &&
      tokens[statement_start + 1]->GetKeyword() == "DATABASE") {
    path_start = statement_start + 2;
    // USE /span/universe/namespace:local_name;
    // This syntax is used for (broken link)
  } else if (tokens[statement_start]->GetKeyword() == "USE") {
    path_start = statement_start + 1;
    // "DEFINE TABLE name format:/path/to/data*;"
  } else if (tokens[statement_start]->GetKeyword() == "DEFINE") {
    int t = statement_start + 1;
    if (tokens[t]->GetKeyword() == "INLINE" ||
        tokens[t]->GetKeyword() == "PERMANENT") {
      ++t;
    }
    if (tokens[t]->GetKeyword() != "TABLE") {
      return;
    }
    // Skip the table name which might consist of multiple tokens.
    for (t += 2; t < tokens.size(); ++t) {
      if (tokens[t]->Is(Token::Type::LEGACY_DEFINE_TABLE_BODY) ||
          tokens[t]->GetKeyword() == "(") {
        // This is either legacy "DEFINE TABLE name <<EOF...EOF;" or
        // a proper GoogleSQL "DEFINE TABLE name (..options..);".
        return;
      }
      if (SpaceBetweenTokensInInput(*tokens[t - 1], *tokens[t])) {
        path_start = t;
        break;
      }
    }
    // [sql_test_option = some/path]
  } else if (tokens[statement_start]->GetKeyword() == "[") {
    int t = statement_start + 1;
    if (tokens[t]->GetKeyword() == "DEFAULT") {
      ++t;
    }
    if (tokens[t + 1]->GetKeyword() == "=") {
      path_start = t + 2;
    }
  }

  if (path_start >= tokens.size()) {
    return;
  }
  tokens[path_start]->SetType(Token::Type::INVALID_TOKEN);
  static const auto* statement_terminators =
      new googlesql_base::flat_set<absl::string_view>({";", "]"});
  for (int i = path_start + 1; i < tokens.size(); ++i) {
    if (i + 1 < tokens.size() && tokens[i]->GetKeyword() == "," &&
        tokens[i + 1]->MayBeStartOfIdentifier()) {
      tokens[i + 1]->SetType(Token::Type::INVALID_TOKEN);
      ++i;
      continue;
    }
    if (statement_terminators->contains(tokens[i]->GetKeyword()) ||
        SpaceBetweenTokensInInput(*tokens[i - 1], *tokens[i])) {
      break;
    }
    tokens[i]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
  }
}

// Marks all VALUES keywords inside INSERT statement.
void MarkAllTopLevelValuesClauses(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  int t = 0;
  for (; t < tokens.size(); ++t) {
    if (tokens[t]->GetKeyword() == "INSERT" &&
        tokens[t]->Is(Token::Type::TOP_LEVEL_KEYWORD)) {
      break;
    }
  }
  for (++t; t < static_cast<int>(tokens.size()) - 1; ++t) {
    if (tokens[t]->GetKeyword() == "VALUES" &&
        tokens[t + 1]->GetKeyword() == "(") {
      tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
    }
  }
}

// Marks all tokens that may be a keyword in a DDL statement.
void MarkAllDdlKeywords(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  int t = 0;
  while (t < tokens.size()) {
    // First, find an ALTER TABLE or CREATE TABLE statement.
    for (; t < tokens.size(); ++t) {
      if ((tokens[t]->Is(Token::Type::TOP_LEVEL_KEYWORD) &&
           tokens[t]->GetKeyword() == "ALTER") ||
          tokens[t]->GetKeyword() == "CREATE") {
        break;
      }
    }
    // Search for TABLE keyword.
    int table_pos = 0;
    for (++t; table_pos < 6 && t < tokens.size(); ++table_pos, ++t) {
      if (tokens[t]->GetKeyword() == "TABLE") {
        break;
      }
    }
    // TABLE keyword can be at max 5 words away from CREATE in
    // "CREATE OR REPLACE PUBLIC EXTERNAL TABLE"
    if (t == tokens.size() || table_pos > 4) {
      return;
    }
    if (t + 1 < tokens.size() && tokens[t + 1]->GetKeyword() == "FUNCTION") {
      // Exclude CREATE TABLE FUNCTION, even though GoogleSQL parser allows
      // DDL statements for functions. This is done to protect from marking
      // DDL keywords accidentally in a normal CREATE TABLE FUNCTION AS SELECT.
      return;
    }

    static const auto* ddl_keywords = new googlesql_base::flat_set<absl::string_view>(
        {"AFTER", "CHECK", "CONSTRAINT", "DEFAULT", "ENFORCED", "FILL",
         "FOLLOWING", "FOR", "FOREIGN", "GENERATED", "HIDDEN", "OPTIONS",
         "PRECEDING", "PRIMARY", "REFERENCES", "SET", "STORED"});
    // TODO: Check also for TYPE (which can be INT even though
    // googlesql doesn't have it).
    for (++t; t < tokens.size(); ++t) {
      if (tokens[t]->GetKeyword() == "SELECT" &&
          (tokens[t - 1]->GetKeyword() == "(" ||
           tokens[t - 1]->GetKeyword() == "AS")) {
        // We are inside CREATE TABLE AS SELECT - this is not table schema, but
        // a normal SELECT statement.
        break;
      }
      if (tokens[t]->Is(Token::Type::UNKNOWN) &&
          ddl_keywords->contains(tokens[t]->GetKeyword())) {
        tokens[t]->SetType(Token::Type::DDL_KEYWORD);
        // FOR acts as a top level keyword in legacy statement:
        // ALTER TABLE .. ADD FILTER .. FOR ..
        if (tokens[t]->GetKeyword() == "FOR" && t + 1 < tokens.size() &&
            tokens[t + 1]->IsValue()) {
          tokens[t]->SetType(Token::Type::TOP_LEVEL_KEYWORD);
        }
      }
    }
  }
}

// Marks all slashed identifiers supported by GoogleSQL grammar for table names.
// The slashed identifier always starts with '/' and may contain dashes ('-'),
// colons (':') and dots. The dashed identifiers might start with 'something-'
// and then are allowed to have more dots or dashes.
void MarkAllSlashedIdentifiers(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  // We consider a slashed identifier something that has at least 4 tokens:
  // '/a/b', '/a-b', '/a:b', '/a.b'. GoogleSQL grammar doesn't have such
  // limitation but we add it to avoid false positives and assuming that real
  // SQL would contain long paths.
  if (tokens.size() < 4) {
    return;
  }
  static const auto* allowed_separators =
      new googlesql_base::flat_set<absl::string_view>({":", "-", "/", "."});

  int start;
  for (int t = 0; t < tokens.size() - 3; ++t) {
    start = -1;
    // The identifier can start with '/' and there should be some other
    // separator after the first path part.
    if (tokens[t]->GetKeyword() == "/" &&
        allowed_separators->contains(tokens[t + 2]->GetKeyword()) &&
        // Previous token shouldn't be something that could have been lhs
        // operand of the divide operator at tokens[t].
        (t == 0 || !CanBePartOfExpression(*tokens[t - 1]) ||
         tokens[t - 1]->GetKeyword() == "(")) {
      start = t;
      // or identifier can start with 'something-something' - in this case, the
      // start is previous token to '-'.
    } else if (t > 0 && tokens[t]->GetKeyword() == "-" &&
               tokens[t - 1]->MayBeIdentifier() &&
               allowed_separators->contains(tokens[t + 2]->GetKeyword()) &&
               // Previous token shouldn't be something that indicates we are in
               // the middle of an expression.
               (t == 1 || !IsRepeatableOperator(*tokens[t - 2]))) {
      start = t - 1;
    }
    if (start < 0) {
      continue;
    }

    // Find the end of identifier. Look only for tokens that don't have any
    // spaces in between in the original input.
    int end = start + 1;
    while (end < tokens.size() &&
           !SpaceBetweenTokensInInput(*tokens[end - 1], *tokens[end]) &&
           (tokens[end]->MayBeIdentifier() ||
            tokens[end]->IsNonPunctuationKeyword() ||
            // A part of path expression could be an integer, e.g. '/path-1'.
            (tokens[end]->IsValue() &&
             tokens[end]->GetValue().type_kind() != googlesql::TYPE_STRING) ||
            allowed_separators->contains(tokens[end]->GetKeyword()))) {
      ++end;
    }
    if (end <= start + 3) {
      // The smallest path should be at least 3 tokens long
      continue;
    }
    if (tokens[start]->Is(Token::Type::UNKNOWN)) {
      tokens[start]->SetType(Token::Type::COMPLEX_TOKEN_START);
    }
    for (int i = start + 1; i < end; ++i) {
      tokens[i]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
    }
    t = end - 1;
  }
}

// Marks all script name identifiers in RUN statements.
// Script names may contain several symbols such as dashes ('-'), colons (':')
// or dots, and should not be split.
void MarkAllComplexScriptNames(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  if (tokens.size() < 3) {
    return;
  }
  static const auto* kIdentifierTerminators =
      new googlesql_base::flat_set<absl::string_view>({";", "("});
  for (int t = 0; t < tokens.size(); ++t) {
    if (tokens[t]->Is(Token::Type::TOP_LEVEL_KEYWORD) &&
        tokens[t]->GetKeyword() == "RUN") {
      ++t;
      // Find the end of identifier. Look only for tokens that don't have any
      // spaces in between in the original input.
      int end = t + 1;
      while (end < tokens.size() &&
             !SpaceBetweenTokensInInput(*tokens[end - 1], *tokens[end]) &&
             !kIdentifierTerminators->contains(tokens[end]->GetKeyword())) {
        ++end;
      }
      for (int i = t + 1; i < end; ++i) {
        tokens[i]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
      }
    }
  }
}

// Marks all colons and field names inside braced constructors.
void MarkTokensInBracedConstructors(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  for (int t = 0; t < tokens.size(); ++t) {
    if (!tokens[t]->Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET) &&
        !MaybeAnOpenBraceForMapConstructor(tokens, t)) {
      continue;
    }
    // Found braced constructor start.
    tokens[t]->SetType(Token::Type::BRACED_CONSTR_OPEN_BRACKET);
    const int end = FindMatchingClosingBracket(tokens, t);
    if (end < tokens.size()) {
      tokens[end]->SetType(Token::Type::BRACED_CONSTR_CLOSE_BRACKET);
    }
    while (++t < end) {
      bool found_field_name = false;
      if (tokens[t]->GetKeyword() == "{") {
        if (MaybeAnOpenBraceForMapConstructor(tokens, t)) {
          tokens[t]->SetType(Token::Type::BRACED_CONSTR_OPEN_BRACKET);
          if (tokens[t - 1]->GetKeyword() != ":" &&
              !IsOpenParenOrBracket(tokens[t - 1]->GetKeyword())) {
            found_field_name = true;
          }
        } else {
          // { starts some template rather than braced constructor. Skip it.
          t = FindMatchingClosingBracket(tokens, t);
          continue;
        }
      } else if (tokens[t]->GetKeyword() == ":") {
        tokens[t]->SetType(Token::Type::BRACED_CONSTR_COLON);
        found_field_name = true;
      } else if (tokens[t]->GetKeyword() == "}" &&
                 tokens[t]->Is(Token::Type::UNKNOWN)) {
        tokens[t]->SetType(Token::Type::BRACED_CONSTR_CLOSE_BRACKET);
        // [] and () in map constructors behave similarly to {} if they
        // surround the entire value and are not part of bigger expression.
      } else if ((tokens[t]->GetKeyword() == "[" ||
                  tokens[t]->GetKeyword() == "(") &&
                 tokens[t - 1]->GetKeyword() == ":") {
        // Check if the brackets are not followed by another SQL expression,
        // e.g.: "foo: (1 + 2) * 3". In this case, brackets are formatted
        // according to SQL rules.
        const int closing_bracket = FindMatchingClosingBracket(tokens, t);
        if (closing_bracket < tokens.size()) {
          const int next_token = closing_bracket + 1;
          if (next_token < tokens.size() &&
              (tokens[next_token]->GetKeyword() == "(" ||
               tokens[next_token]->GetKeyword() == "," ||
               tokens[next_token]->GetKeyword() == "}" ||
               tokens[next_token]->MayBeStartOfIdentifier())) {
            tokens[t]->SetType(Token::Type::BRACED_CONSTR_OPEN_BRACKET);
            tokens[closing_bracket]->SetType(
                Token::Type::BRACED_CONSTR_CLOSE_BRACKET);
          }
        }
      }
      if (found_field_name) {
        int name_start = t - 1;
        // The field name might be a proto extension in parentheses,
        // mark it as a single identifier to avoid splitting it.
        if (tokens[name_start]->GetKeyword() == ")" && name_start > 1) {
          while (name_start > 0 && tokens[name_start]->GetKeyword() != "(" &&
                 tokens[name_start]->GetKeyword() != "," &&
                 tokens[name_start]->GetKeyword() != "{") {
            --name_start;
          }
          if (tokens[name_start]->GetKeyword() == "(") {
            for (int i = name_start + 1; i < t; ++i) {
              tokens[i]->SetType(Token::Type::COMPLEX_TOKEN_CONTINUATION);
            }
          }
        } else {
          while (
              name_start > 0 &&
              tokens[name_start]->Is(Token::Type::COMPLEX_TOKEN_CONTINUATION)) {
            --name_start;
          }
        }
        tokens[name_start]->SetType(Token::Type::BRACED_CONSTR_FIELD);
      }
    }
  }
}

// Builds a hash-set of built-in function names within GoogleSQL.
absl::flat_hash_set<std::string>* GetBuiltinFunctions() {
  static absl::flat_hash_set<std::string>* function_names = []() {
    TypeFactory type_factory;
    LanguageOptions language_options;
    language_options.EnableMaximumLanguageFeaturesForDevelopment();
    language_options.set_product_mode(PRODUCT_INTERNAL);
    absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions;
    absl::flat_hash_map<std::string, const Type*> types_ignored;
    GOOGLESQL_CHECK_OK(
        GetBuiltinFunctionsAndTypes(BuiltinFunctionOptions(language_options),
                                    type_factory, functions, types_ignored));
    absl::flat_hash_set<std::string>* function_names =
        new absl::flat_hash_set<std::string>();
    function_names->reserve(functions.size());
    for (const auto& function_mapping : functions) {
      function_names->insert(absl::AsciiStrToUpper(function_mapping.first));
    }
    return function_names;
  }();
  return function_names;
}

// Annotates all tokens which are a part of a Builtin Function call.
void MarkAllBuiltinFunctions(const TokensView& tokens_view) {
  const std::vector<Token*>& tokens = tokens_view.WithoutComments();
  if (tokens.empty()) {
    return;
  }
  absl::flat_hash_set<std::string>* builtin_function_names =
      GetBuiltinFunctions();
  for (int i = 0; i < tokens.size() - 1; ++i) {
    // Function call should be followed by an open-parenthesis.
    if (tokens[i + 1]->GetKeyword() == "(") {
      int start = i;
      std::string s = "";
      while (start > 0 &&
             !SpaceBetweenTokensInInput(*tokens[start], *tokens[start + 1])) {
        s = absl::StrCat(tokens[start--]->GetKeyword(), s);
      }
      if (builtin_function_names->contains(s)) {
        for (; start <= i; ++start) {
          tokens[start]->SetType(Token::Type::BUILTIN_FUNCTION);
        }
      }
    }
  }
}

void AnnotateTokens(const TokensView& tokens,
                    const ParseLocationTranslator& location_translator,
                    const FormatterOptions& formatter_options) {
  MarkAllKeywordsInComplexIdentifiers(tokens);
  MarkAllTypeDeclarations(tokens);
  MarkSqlTokensThatArePartOfBrokenOperator(tokens);
  MarkNonSqlTokensThatArePartOfComplexToken(tokens);
  MarkAllMacroAndTableDefinitions(tokens);
  MarkAllMacroCallsThatShouldBeFused(tokens);
  MarkAllMacroBodiesThatShouldBeFused(tokens);
  MarkAllStatementStarts(tokens, location_translator);
  MarkAllComplexScriptNames(tokens);
  MarkAllTopLevelWithKeywords(tokens);
  MarkAllPrivilegeTypeKeywords(tokens);
  MarkAllSelectStarModifiers(tokens);
  MarkAllAssignmentOperators(tokens);
  MarkAllProtoExtensionParentheses(tokens);
  MarkBetweenAndNotFromAsComparisonOperators(tokens);
  MarkAllTokensStartingAList(tokens);
  MarkUnquotedPaths(tokens);
  MarkAllTopLevelValuesClauses(tokens);
  MarkAllDdlKeywords(tokens);
  MarkAllKeywordsUsedAsParams(tokens);
  MarkAllProtoBrackets(tokens);
  MarkChainedMethodDotSeparators(tokens);
  MarkAllSlashedIdentifiers(tokens);
  MarkTokensInBracedConstructors(tokens);
  if (formatter_options.IsCapitalizeFunctions()) {
    MarkAllBuiltinFunctions(tokens);
  }
}

void MarkAllAngleBracketPairs(std::vector<Chunk>* chunks) {
  std::vector<Chunk*> open_brackets;
  for (int i = 0; i < chunks->size(); i++) {
    Chunk* chunk = &chunks->at(i);
    const std::vector<Token*>& tokens = chunk->Tokens().WithoutComments();
    if (tokens.empty()) {
      continue;
    }
    if (tokens.back()->IsOpenAngleBracket()) {
      open_brackets.push_back(chunk);
      continue;
    }
    if (open_brackets.empty()) {
      continue;
    }
    for (const auto* token : tokens) {
      if (token->IsCloseAngleBracket()) {
        Chunk* open_bracket = open_brackets.back();
        open_brackets.pop_back();
        open_bracket->SetMatchingClosingChunk(chunk);
        chunk->SetMatchingOpeningChunk(open_bracket);
        if (open_brackets.empty()) {
          break;
        }
      }
    }
  }
}

absl::StatusOr<std::vector<Chunk>> ChunksFromTokens(
    const TokensView& tokens_view,
    const ParseLocationTranslator& location_translator,
    const FormatterOptions& formatter_options) {
  AnnotateTokens(tokens_view, location_translator, formatter_options);

  std::vector<Chunk> chunks;

  int t = 0;
  int previous_non_comment_token = -1;
  int chunk_count = 0;

  const std::vector<Token*>& tokens = tokens_view.All();
  if (tokens.empty()) {
    return chunks;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(Chunk chunk, Chunk::CreateEmptyChunk(
                                    /*starts_with_space=*/false, chunk_count,
                                    location_translator, *tokens[t]));
  while (t < tokens.size()) {
    if (!IsPartOfSameChunk(chunk, tokens, previous_non_comment_token, t)) {
      if (!chunk.Empty()) {
        chunks.emplace_back(chunk);
        bool starts_with_space = false;
        if (t > 0) {
          starts_with_space =
              chunks.back().SpaceBetweenTokens(*tokens[t - 1], *tokens[t]);
        }
        GOOGLESQL_ASSIGN_OR_RETURN(
            chunk, Chunk::CreateEmptyChunk(starts_with_space, ++chunk_count,
                                           location_translator, *tokens[t]));
      }
    }
    chunk.AddToken(tokens[t]);
    chunk.UpdateUnaryOperatorTypeIfNeeded(
        chunks.empty() ? nullptr : &chunks.back(),
        previous_non_comment_token < 0 ? nullptr
                                       : tokens[previous_non_comment_token]);
    const int max_fusible_token = FindMaxFusibleTokenIndex(tokens, t);
    while (t < max_fusible_token) {
      if (!tokens[t]->IsComment()) {
        previous_non_comment_token = t;
      }
      t++;
      chunk.AddToken(tokens[t]);
    }

    if (!tokens[t]->IsComment()) {
      previous_non_comment_token = t;
    }
    t++;
  }

  if (!chunk.Empty()) {
    chunks.emplace_back(chunk);
  }

  // Build a double-linked list out of chunks.
  for (int i = 1; i < chunks.size(); ++i) {
    chunks[i - 1].SetNextChunk(&chunks[i]);
    chunks[i].SetPreviousChunk(&chunks[i - 1]);
  }

  MarkAllAngleBracketPairs(&chunks);
  return chunks;
}

ChunkBlock::ChunkBlock(ChunkBlockFactory* block_factory)
    : block_factory_(ABSL_DIE_IF_NULL(block_factory)),
      parent_(nullptr),
      chunk_(nullptr) {}

ChunkBlock::ChunkBlock(ChunkBlockFactory* block_factory, ChunkBlock* parent)
    : block_factory_(ABSL_DIE_IF_NULL(block_factory)),
      parent_(ABSL_DIE_IF_NULL(parent)),
      chunk_(nullptr) {
  level_ = parent->IsTopLevel()
               ? 0
               : parent->level_ + block_factory_->IndentationSpaces();
}

ChunkBlock::ChunkBlock(ChunkBlockFactory* block_factory, ChunkBlock* parent,
                       class Chunk* chunk)
    : block_factory_(ABSL_DIE_IF_NULL(block_factory)),
      parent_(ABSL_DIE_IF_NULL(parent)),
      chunk_(ABSL_DIE_IF_NULL(chunk)) {
  level_ = parent->IsTopLevel() ? 0 : parent->level_;
  extra_offset_ = parent->extra_offset_;
  chunk->SetChunkBlock(this);
}

class Chunk* ChunkBlock::FirstChunkUnder() const {
  if (IsLeaf()) {
    return Chunk();
  }
  for (const auto& child : children_) {
    class Chunk* c = child->FirstChunkUnder();
    // Skip empty non-leaf blocks.
    if (c != nullptr) {
      return c;
    }
  }
  return nullptr;
}

class Chunk* ChunkBlock::LastChunkUnder() const {
  std::vector<
      std::pair<const ChunkBlock*, ChunkBlock::Children::reverse_iterator>>
      queue;
  queue.emplace_back(this, this->children_.rbegin());
  while (!queue.empty()) {
    auto [block, current_child] = queue.back();
    queue.pop_back();
    if (block->IsLeaf()) {
      return block->Chunk();
    }
    if (current_child != block->children_.rend()) {
      const ChunkBlock* child = *current_child;
      queue.emplace_back(block, ++current_child);
      queue.emplace_back(child, child->children_.rbegin());
    }
  }
  return nullptr;
}

ChunkBlock* ChunkBlock::AddSameLevelChunk(class Chunk* chunk) {
  if (IsTopLevel()) {
    // For top, there is no same level. There is only indented block.
    return AddIndentedChunk(chunk);
  } else {
    return parent_->AddChildChunk(chunk);
  }
}

ChunkBlock* ChunkBlock::AddSameLevelCousinChunk(class Chunk* chunk) {
  if (IsTopLevel()) {
    // For top, there is no same level. There is only indented block.
    return AddIndentedChunk(chunk);
  } else {
    return parent_->AddIndentedChunk(chunk);
  }
}

ChunkBlock* ChunkBlock::AddIndentedChunk(class Chunk* chunk) {
  if (IsTopLevel()) {
    // Top is a bit special, because it never contains chunks directly and
    // doesn't have a parent. The operation is the same, but we don't perform it
    // on the parent, but rather the node itself.
    return AddChildBlockWithGrandchildChunk(chunk);
  } else {
    return parent_->AddChildBlockWithGrandchildChunk(chunk);
  }
}

ChunkBlock* ChunkBlock::AddSameLevelChunkImmediatelyBefore(class Chunk* chunk) {
  if (IsTopLevel()) {
    // This doesn't make any sense for top, since it has no before.
    return AddIndentedChunk(chunk);
  } else {
    return parent_->AddChunkBefore(chunk, this);
  }
}

ChunkBlock* ChunkBlock::AddIndentedChunkImmediatelyBefore(class Chunk* chunk) {
  if (IsTopLevel()) {
    // This doesn't make any sense for top, since it has no before.
    return AddIndentedChunk(chunk);
  } else {
    return parent_->AddIndentedChunkBefore(chunk, this);
  }
}

ChunkBlock* ChunkBlock::AddChildChunk(class Chunk* chunk) {
  if (IsTopLevel()) {
    // For top, all children are indented.
    return AddIndentedChunk(chunk);
  } else {
    ChunkBlock* new_block = block_factory_->NewChunkBlock(this, chunk);
    children_.push_back(new_block);
    return new_block;
  }
}

ChunkBlock* ChunkBlock::AddChildBlockWithGrandchildChunk(class Chunk* chunk) {
  ChunkBlock* new_block = block_factory_->NewChunkBlock(this);
  children_.push_back(new_block);
  return new_block->AddChildChunk(chunk);
}

ChunkBlock* ChunkBlock::AddChunkBefore(class Chunk* chunk, ChunkBlock* block) {
  if (IsTopLevel()) {
    return AddIndentedChunk(chunk);
  }

  for (auto it = children_.begin(); it != children_.end(); ++it) {
    if (*it == block) {
      return *children_.insert(it, block_factory_->NewChunkBlock(this, chunk));
    }
  }
  ABSL_DLOG(FATAL) << "Given block is not a child of this chunk block.\n"
                 "*this:\n"
              << DebugString() << "\nblock:\n"
              << block->DebugString();
  return nullptr;
}

ChunkBlock* ChunkBlock::AddIndentedChunkBefore(class Chunk* chunk,
                                               ChunkBlock* block) {
  for (auto it = children_.begin(); it != children_.end(); ++it) {
    if (*it == block) {
      Children::iterator new_block =
          children_.insert(it, block_factory_->NewChunkBlock(this));
      return (*new_block)->AddChildChunk(chunk);
    }
  }
  ABSL_DLOG(FATAL) << "Given block is not a child of this chunk block.\n"
                 "*this:\n"
              << DebugString() << "\nblock:\n"
              << block->DebugString();
  return nullptr;
}

void ChunkBlock::AdoptChildBlock(ChunkBlock* block) {
  block->parent_ = this;

  // Fix the levels of all children. Avoid recursion for stack protection.
  int level_offset = level_ - block->level_;
  if (!block->IsLeaf()) {
    level_offset += block_factory_->IndentationSpaces();
  }
  if (level_offset != 0) {
    std::queue<ChunkBlock*> blocks;
    blocks.push(block);

    while (!blocks.empty()) {
      ChunkBlock* b = blocks.front();
      blocks.pop();

      b->level_ = b->level_ + level_offset;
      for (auto it = b->children().begin(); it != b->children().end(); it++) {
        blocks.push(*it);
      }
    }
  }

  children_.push_back(block);
}

ChunkBlock* ChunkBlock::GroupAndIndentChildrenUnderNewBlock(
    const Children::reverse_iterator& start,
    const Children::reverse_iterator& end) {
  ChunkBlock* new_block = block_factory_->NewChunkBlock(this);

  // We have to use .base() here and go from end to start to preserve the order
  // of the nodes.
  for (Children::iterator i = end.base();
       i != children_.end() && i != start.base(); ++i) {
    new_block->AdoptChildBlock(*i);
  }
  children_.erase(end.base(), start.base());

  children_.push_back(new_block);
  return new_block;
}

ChunkBlock* ChunkBlock::FindNextSiblingBlock() const {
  if (IsTopLevel()) {
    return nullptr;
  }

  Children siblings = parent_->children();
  for (auto it = siblings.begin(); it != siblings.end(); ++it) {
    if (*it == this) {
      if (++it != siblings.end()) {
        return *it;
      }
      break;
    }
  }
  return nullptr;
}

ChunkBlock* ChunkBlock::FindPreviousSiblingBlock() const {
  if (IsTopLevel()) {
    return nullptr;
  }

  Children siblings = parent_->children();
  for (auto it = siblings.begin(); it != siblings.end(); ++it) {
    if (*it == this) {
      if (it != siblings.begin()) {
        return *--it;
      }
      break;
    }
  }
  return nullptr;
}

void ChunkBlock::SetMinOffset(int min_offset) {
  // Minimal offset can be set only for leaf blocks (with chunks) that already
  // have non-0 offset.
  if (IsTopLevel() || !IsLeaf() || level_ == 0) {
    return;
  }
  const int current_offset = level_ - parent_->parent_->level_;
  if (current_offset < min_offset) {
    extra_offset_ = min_offset - current_offset;
  }
  parent_->extra_offset_ = extra_offset_;
}

std::string ChunkBlock::DebugString(int indent) const {
  std::string padding;
  padding.append(indent * 2, ' ');
  std::string contents;
  if (IsLeaf()) {
    contents = absl::StrCat(chunk_->DebugString(/*verbose=*/true), "  #",
                            chunk_->PositionInQuery());
    if (chunk_->HasMatchingClosingChunk()) {
      absl::StrAppend(&contents, "->#",
                      chunk_->MatchingClosingChunk()->PositionInQuery(), "=\"",
                      chunk_->MatchingClosingChunk()->DebugString(), "\"");
    }
    if (chunk_->HasMatchingOpeningChunk()) {
      absl::StrAppend(&contents, "<-#",
                      chunk_->MatchingOpeningChunk()->PositionInQuery(), "=\"",
                      chunk_->MatchingOpeningChunk()->DebugString(), "\"");
    }
  } else if (children_.empty()) {
    contents = "[]";
  } else {
    // Recursively print children.
    contents = absl::StrFormat(
        "[(%d)\n%s\n%s]", children_.size(),
        absl::StrJoin(
            children_, "\n",
            [indent](std::string* out, const ChunkBlock* const block) {
              absl::StrAppend(out, block->DebugString(indent + 1));
            }),
        padding);
  }
  return absl::StrFormat("%s%d%s: %s", padding, Level(),
                         IsTopLevel() ? "(top)" : "", contents);
}

std::ostream& operator<<(std::ostream& os, const ChunkBlock* const block) {
  if (block == nullptr) {
    return os << "<null>";
  }
  return os << block->DebugString();
}

ChunkBlockFactory::ChunkBlockFactory(int indentation_spaces)
    : indentation_spaces_(indentation_spaces) {
  // Constructor creates a root block to guarantee it is always available.
  NewChunkBlock();
}

ChunkBlock* ChunkBlockFactory::NewChunkBlock() {
  // Using `new` to access a non-public constructor.
  blocks_.emplace_back(absl::WrapUnique(new ChunkBlock(this)));
  return blocks_.back().get();
}

ChunkBlock* ChunkBlockFactory::NewChunkBlock(ChunkBlock* parent) {
  // Using `new` to access a non-public constructor.
  blocks_.emplace_back(absl::WrapUnique(new ChunkBlock(this, parent)));
  return blocks_.back().get();
}

ChunkBlock* ChunkBlockFactory::NewChunkBlock(ChunkBlock* parent, Chunk* chunk) {
  // Using `new` to access a non-public constructor.
  blocks_.emplace_back(absl::WrapUnique(new ChunkBlock(this, parent, chunk)));
  return blocks_.back().get();
}

void ChunkBlockFactory::Reset() {
  blocks_.clear();
  NewChunkBlock();
}

}  // namespace googlesql::formatter::internal
