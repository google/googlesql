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

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/common/errors.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/macro_expander.h"
#include "googlesql/parser/macros/token_provider.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Implementation of the wrapper calls forward-declared in parser_internal.h.
// This workaround is to avoid creating an interface and incurring a v-table
// lookup on every token.
namespace parser {
namespace internal {
using googlesql::parser::LookaheadTransformer;
using googlesql::parser::ParserMode;
using parser::Token;

void PushParserMode(LookaheadTransformer& lookahead_transformer,
                    ParserMode mode) {
  return lookahead_transformer.PushParserMode(mode);
}
void PopParserMode(LookaheadTransformer& lookahead_transformer) {
  return lookahead_transformer.PopParserMode();
}
Token GetNextToken(LookaheadTransformer& lookahead_transformer,
                   absl::string_view* text, ParseLocationRange* location) {
  return lookahead_transformer.GetNextToken(text, location);
}
absl::Status OverrideNextTokenLookback(
    LookaheadTransformer& lookahead_transformer, bool parser_lookahead_is_empty,
    Token expected_next_token, Token lookback_token) {
  return lookahead_transformer.OverrideNextTokenLookback(
      parser_lookahead_is_empty, expected_next_token, lookback_token);
}

absl::Status OverrideCurrentTokenLookback(
    LookaheadTransformer& lookahead_transformer, Token new_token_kind) {
  return lookahead_transformer.OverrideCurrentTokenLookback(new_token_kind);
}
}  // namespace internal

// Include the helpful type aliases in the namespace within the C++ file so
// that they are useful for free helper functions as well as class member
// functions.
using Location = ParseLocationRange;
using DiagnosticOptions = macros::DiagnosticOptions;
using TokenProvider = macros::TokenProvider;
using MacroCatalog = macros::MacroCatalog;
using MacroExpander = macros::MacroExpander;
using MacroExpanderBase = macros::MacroExpanderBase;

static constexpr Token kInTemplatedType = Token::LT;

static bool IsLookbackToken(Token token) {
  return token > Token::SENTINEL_LB_TOKEN_START &&
         token < Token::SENTINEL_LB_TOKEN_END;
}

static bool IsReservedKeywordToken(Token token) {
  // We need to add sentinels before and after each block of keywords to make
  // this safe.
  return token > Token::SENTINEL_RESERVED_KW_START &&
         token < Token::SENTINEL_RESERVED_KW_END;
}

static bool IsNonreservedKeywordToken(Token token) {
  // We need to add sentinels before and after each block of keywords to make
  // this safe.
  return token > Token::SENTINEL_NONRESERVED_KW_START &&
         token < Token::SENTINEL_NONRESERVED_KW_END;
}

static bool IsKeywordToken(Token token) {
  return IsReservedKeywordToken(token) || IsNonreservedKeywordToken(token);
}

static bool IsIdentifierOrKeyword(Token token) {
  switch (token) {
    case Token::IDENTIFIER:
    case Token::EXP_IN_FLOAT_NO_SIGN:
    case Token::STANDALONE_EXPONENT_SIGN:
      return true;
    default:
      return IsKeywordToken(token);
  }
}

static bool IsIdentifierOrNonreservedKeyword(Token token) {
  switch (token) {
    case Token::IDENTIFIER:
    case Token::EXP_IN_FLOAT_NO_SIGN:
    case Token::STANDALONE_EXPONENT_SIGN:
      return true;
    default:
      return IsNonreservedKeywordToken(token);
  }
}

// Returns whether `token` is a keyword or an unquoted identifier.
static bool IsKeywordOrUnquotedIdentifier(const TokenWithLocation& token) {
  switch (token.kind) {
    case Token::EXP_IN_FLOAT_NO_SIGN:
    case Token::STANDALONE_EXPONENT_SIGN:
      return true;
    case Token::IDENTIFIER:
      ABSL_DCHECK(!token.text.empty());
      return token.text.front() != '`';
    default:
      return IsKeywordToken(token.kind);
  }
}

static absl::Status MakeError(absl::string_view error_message,
                              const Location& yylloc) {
  return MakeSqlErrorAtPoint(yylloc.start()) << error_message;
}

static Token GetLookbackToken(
    const std::optional<TokenWithOverrideError>& lookback_slot) {
  if (!lookback_slot.has_value()) {
    return Token::UNAVAILABLE;
  }
  if (lookback_slot->lookback_override != Token::UNAVAILABLE) {
    return lookback_slot->lookback_override;
  }
  return lookback_slot->token.kind;
}

bool LookaheadTransformer::IsValidPreviousTokenToSqlStatement(
    const std::optional<TokenWithOverrideError>& lookback_slot) const {
  if (!lookback_slot.has_value()) {
    switch (mode_) {
      case ParserMode::kNextScriptStatement:
      case ParserMode::kNextStatementKind:
      case ParserMode::kNextStatement:
      case ParserMode::kScript:
      case ParserMode::kStatement:
        return true;
      default:
        return false;
    }
  }
  Token token = GetLookbackToken(lookback_slot);
  switch (token) {
    case Token::SEMICOLON:
    case Token::LB_EXPLAIN_SQL_STATEMENT:
    case Token::LB_END_OF_STATEMENT_LEVEL_HINT:
    case Token::LB_OPEN_STATEMENT_BLOCK:
    case Token::LB_BEGIN_AT_STATEMENT_START:
    case Token::KW_ELSE:
    case Token::KW_THEN:
      return true;
    default:
      return false;
  }
}

bool LookaheadTransformer::IsValidLookbackToStartQuery() const {
  if (IsValidPreviousTokenToSqlStatement(lookback_1_)) {
    return true;
  }
  switch (Lookback1()) {
    case Token::LB_PAREN_OPENS_QUERY:
      return true;
    case Token::LB_AS_BEFORE_QUERY:
      // Case ... CREATE VIEW ... AS ● SELECT
      return true;
    case Token::LB_CLOSE_ALIASED_QUERY:
      // Case ... WITH t AS (...) ● SELECT
      return true;
    case Token::LB_END_OF_WITH_RECURSIVE:
      // Case .. WITH t AS (...) WITH DEPTH ● SELECT
      return true;
    case Token::LB_CLOSE_COLUMN_LIST:
      // Case ... CORRESPONDING BY (column list) ● SELECT
      return true;
    case Token::LB_SET_OP_QUANTIFIER:
      // Case ... UNION ALL ● SELECT
      return true;
    case Token::KW_CORRESPONDING:
      return
          // Case ... UNION ALL CORRESPONDING ● SELECT
          Lookback2() == Token::LB_SET_OP_QUANTIFIER ||
          // Case ... UNION ALL STRICT CORRESPONDING ● SELECT
          (Lookback2() == Token::KW_STRICT &&
           Lookback3() == Token::LB_SET_OP_QUANTIFIER);
    default:
      return false;
  }
}

bool LookaheadTransformer::IsCurrentTokenScriptLabel() const {
  // When lookback_1_ is unset we are at the beginning of the input. A script
  // label is allowed at the beginning of the input, or if the previous token
  // is one of the following.
  if (lookback_1_.has_value()) {
    switch (Lookback1()) {
      case Token::SEMICOLON:
      case Token::LB_END_OF_STATEMENT_LEVEL_HINT:
      case Token::LB_OPEN_STATEMENT_BLOCK:
      case Token::LB_BEGIN_AT_STATEMENT_START:
      case Token::KW_ELSE:
      case Token::KW_THEN:
        break;
      default:
        return false;
    }
  }

  const Token token = current_token_->token.kind;
  if (!IsIdentifierOrKeyword(token)) {
    return false;
  }
  if (Lookahead1() != Token::COLON) {
    return false;
  }
  switch (Lookahead2()) {
    case Token::KW_BEGIN:
    case Token::KW_WHILE:
    case Token::KW_LOOP:
    case Token::KW_REPEAT:
    case Token::KW_FOR:
      return true;
    default:
      return false;
  }
}

void LookaheadTransformer::ApplyConditionallyReservedKeywords(Token& kind) {
  switch (kind) {
    case Token::KW_GRAPH_TABLE_NONRESERVED:
      if (language_options_.IsReservedKeyword("GRAPH_TABLE")) {
        kind = Token::KW_GRAPH_TABLE_RESERVED;
      }
      break;
    case Token::KW_QUALIFY_NONRESERVED:
      if (language_options_.IsReservedKeyword("QUALIFY")) {
        kind = Token::KW_QUALIFY_RESERVED;
      }
      break;
    case Token::KW_MATCH_RECOGNIZE_NONRESERVED:
      if (language_options_.IsReservedKeyword("MATCH_RECOGNIZE")) {
        kind = Token::KW_MATCH_RECOGNIZE_RESERVED;
      }
      break;
    default:
      break;
  }
}

void LookaheadTransformer::FetchNextToken(
    const std::optional<TokenWithOverrideError>& current,
    std::optional<TokenWithOverrideError>& next) {
  if (current.has_value() && current->token.kind == Token::EOI) {
    // If the current token is already YYEOF, do not continue the fetch.
    // Instead, return the same token directly so that future calls to
    // GetNextToken() and GetOverrideError() return the same token kind and
    // error.
    //
    // This is ok because we do not allow token transformation from YYEOF to
    // non-YYEOF, so `current` will always remain YYEOF.
    next = *current;
    return;
  }
  next.emplace();
  absl::StatusOr<TokenWithLocation> next_token =
      macro_expander_->GetNextToken();
  if (mode_ != ParserMode::kTokenizerPreserveComments) {
    // Skip comment tokens if we do not need to preserve comments.
    while (next_token.ok() && next_token->kind == Token::COMMENT) {
      next_token = macro_expander_->GetNextToken();
    }
  }
  if (next_token.ok()) {
    next->token = *next_token;
    ApplyConditionallyReservedKeywords(next->token.kind);
    next->error = absl::OkStatus();
  } else {
    next->token.kind = Token::EOI;
    // TODO: Correctly update the `slot` token location once the
    // macro expander is updated to return TokenWithOverrideError.
    next->token.location = Location();
    next->error = std::move(next_token.status());
  }
}

// Returns whether `token1` and `token2` are adjacent and `token` precedes
// `token2`.
static bool IsAdjacentPrecedingToken(
    const std::optional<TokenWithOverrideError>& token1,
    const std::optional<TokenWithOverrideError>& token2) {
  if (!token1.has_value() || !token2.has_value()) {
    return false;
  }
  // YYEOF could mean tokens have errors, in which case we do not have the
  // correct location information, so we return false to disallow token fusions.
  if (token1->token.kind == Token::EOI || token2->token.kind == Token::EOI) {
    return false;
  }
  return token1->token.AdjacentlyPrecedes(token2->token);
}

// Merges the token texts of `token1` and `token2` into a single text. `token1`
// and `token2` must be adjacent.
static absl::string_view GetFusedText(const TokenWithLocation& token1,
                                      const TokenWithLocation& token2) {
  absl::string_view::size_type total_size =
      token1.text.size() + token2.text.size();
  return absl::string_view(token1.text.data(), total_size);
}

// Fuses `token1` and `token2` into a new token with token kind being
// `target_token_kind`. `token1` must precede `token2` and they must be
// adjacent.
static TokenWithLocation FuseTokensIntoTokenKind(
    Token target_token_kind, const TokenWithLocation& token1,
    const TokenWithLocation& token2) {
  ABSL_DCHECK(token1.AdjacentlyPrecedes(token2));
  return {
      .kind = target_token_kind,
      .location = Location(token1.location.start(), token2.location.end()),
      .text = GetFusedText(token1, token2),
      .preceding_whitespaces = token1.preceding_whitespaces,
  };
}

void LookaheadTransformer::FuseLookahead1IntoCurrent(Token fused_token_kind) {
  ABSL_DCHECK(current_token_.has_value());
  ABSL_DCHECK(IsAdjacentPrecedingToken(current_token_, lookahead_1_));
  current_token_->token = FuseTokensIntoTokenKind(
      fused_token_kind, current_token_->token, lookahead_1_->token);
  lookahead_1_.swap(lookahead_2_);
  lookahead_2_.swap(lookahead_3_);
  FetchNextToken(lookahead_2_, lookahead_3_);
}

// Detects whether a token that could be a literal (in `lookback_token`) is
// followed by an adjacent unquoted IDENTIFIER or non-reserved keyword token (in
// `current_token`). This is sometimes used to generate error messages in cases
// like `SELECT 123abc` where it appears the user missed a whitespace between a
// column value and its alias.
//
// `lookback1` is the token kind for `lookback_token` when it is used as
// lookbacks, which can be different from `lookback_token.kind`. See
// the comment of Lookback1() for more information.
static bool IsLiteralBeforeAdjacentUnquotedIdentifier(
    Token lookback1,
    const std::optional<TokenWithOverrideError>& lookback_token,
    const std::optional<TokenWithOverrideError>& current_token) {
  if (!IsKeywordOrUnquotedIdentifier(current_token->token)) {
    return false;
  }
  if (lookback1 != Token::FLOATING_POINT_LITERAL &&
      lookback1 != Token::INTEGER_LITERAL) {
    return false;
  }
  if (!IsAdjacentPrecedingToken(lookback_token, current_token)) {
    return false;
  }
  // Inputs like "123.abc" are allowed by the lexer and are tokenized into two
  // tokens: FLOATING_POINT_LITERAL ("123.") and IDENTIFIER ("abc"). We preserve
  // the behavior here.
  if (lookback_token.has_value() && lookback_token->token.text.back() == '.') {
    return false;
  }
  return true;
}

// The token disambiguation rules are allowed to see a fixed-length sequence of
// tokens produced by the lexical rules and may change the kind of `token` based
// on the kinds of the other tokens in the window.
//
// For now, the window available is:
//   [token, Lookahead1()]
//
// `token` is the token that is about to be dispensed to the consuming
//     component.
// `Lookahead1()` is the next token that will be disambiguated on the subsequent
//     call to GetNextToken.
//
// USE WITH CAUTION:
// For any given sequence of tokens, there may be many different shift/reduce
// sequences in the parser that "accept" that token sequence. It's critical
// when adding a token disambiguation rule that all parts of the grammar that
// accept the sequence of tokens are identified to verify that changing the kind
// of `token` does not break any unanticipated cases where that sequence would
// currently be accepted.
Token LookaheadTransformer::ApplyTokenDisambiguation(
    const TokenWithLocation& token_with_location) {
  const Token token = token_with_location.kind;
  const Location& location = token_with_location.location;

  switch (mode_) {
    case ParserMode::kTokenizer:
    case ParserMode::kTokenizerPreserveComments:
      // Tokenizer modes are used to extract tokens for error messages among
      // other things. The rules below are mostly intended to support the bison
      // parser, and aren't necessary in tokenizer mode.
      // For keywords that have context-dependent variations, return the
      // "standard" one.
      switch (Lookback1()) {
        case Token::LB_DOT_IN_PATH_EXPRESSION:
        case Token::ATSIGN:
        case Token::KW_DOUBLE_AT:
          if (IsKeywordToken(token)) {
            // This keyword is used as an identifier.
            return Token::IDENTIFIER;
          }
          break;
        default:
          break;
      }
      if (IsLiteralBeforeAdjacentUnquotedIdentifier(Lookback1(), lookback_1_,
                                                    current_token_)) {
        return Token::ATTACHED_ALIAS;
      }
      switch (token) {
        case Token::KW_DEFINE_FOR_MACROS:
          return Token::KW_DEFINE;
        case Token::KW_OPEN_HINT:
        case Token::KW_OPEN_INTEGER_HINT:
          return Token::ATSIGN;
        // The following token fusions need to be performed even in the
        // `kTokenizer` and `kTokenizerPreserveComments` to make sure the
        // formatter and other clients of GetParseTokens do not have to
        // reimplement fusions of floating point literals.
        case Token::DECIMAL_INTEGER_LITERAL:
        case Token::HEX_INTEGER_LITERAL:
          TransformIntegerLiteral();
          return current_token_->token.kind;
        case Token::DOT:
          return TransformDotSymbol();
        case Token::EXP_IN_FLOAT_NO_SIGN:
        case Token::STANDALONE_EXPONENT_SIGN:
        case Token::IDENTIFIER:
          return Token::IDENTIFIER;
        default:
          return token;
      }
    case ParserMode::kMacroBody:
      switch (token) {
        case Token::SEMICOLON:
        case Token::EOI:
          return token;
        default:
          return Token::MACRO_BODY_TOKEN;
      }
    default:
      break;
  }

  // The rules in this block are changing state based on lookback overrides.
  // These should happen before any token transformations when we are running
  // in a mode that is driven by the parser.
  switch (Lookback1()) {
    case Token::LB_OPEN_TYPE_TEMPLATE: {
      PushState(kInTemplatedType);
      break;
    }
    case Token::LB_CLOSE_TYPE_TEMPLATE: {
      bool popped = PopStateIfMatch(kInTemplatedType);
      ABSL_DCHECK(popped);
      break;
    }
    default: {
      break;
    }
  }

  // WARNING: This transformation must come before other transformations for
  // keywords and identifiers because it force-emits the SCRIPT_LABEL token,
  // even if a keyword is present.
  if (IsCurrentTokenScriptLabel()) {
    if (IsReservedKeywordToken(token)) {
      return SetOverrideErrorAndReturnEof(
          absl::StrCat("Reserved keyword '", token_with_location.text,
                       "' may not be used as a label name without backticks"),
          location);
    }
    return Token::SCRIPT_LABEL;
  }

  if (IsLiteralBeforeAdjacentUnquotedIdentifier(Lookback1(), lookback_1_,
                                                current_token_)) {
    return Token::ATTACHED_ALIAS;
  }

  switch (Lookback1()) {
    case Token::LB_DOT_IN_PATH_EXPRESSION:
    case Token::ATSIGN:
    case Token::KW_DOUBLE_AT:
      if (IsKeywordToken(token)) {
        // This keyword is used as an identifier.
        return Token::IDENTIFIER;
      }
      break;
    case Token::LB_END_OF_WITH_RECURSIVE:
      switch (token) {
        case Token::KW_AS:
        case Token::ATSIGN:
        case Token::KW_DOUBLE_AT:
          lookahead_1_->lookback_override = Token::LB_END_OF_WITH_RECURSIVE;
          break;
        case Token::KW_BETWEEN:
        case Token::KW_MAX:
        case Token::KW_AND:
        case Token::KW_UNBOUNDED:
        case Token::QUEST:
        case Token::DECIMAL_INTEGER_LITERAL:
        case Token::HEX_INTEGER_LITERAL:
          current_token_->lookback_override = Token::LB_END_OF_WITH_RECURSIVE;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  switch (token) {
    case Token::KW_TABLE:
      if (IsValidLookbackToStartQuery()) {
        return Token::KW_TABLE_FOR_TABLE_CLAUSE;
      }
      if (Lookback1() == Token::LPAREN) {
        // This case the word 'TABLE' is the first token in a parenthesized
        // expression, parenthesized query, parenthesized join, or other
        // parenthesized construct where the parser isn't setting a specific
        // lookback override token for the left paren.
        //
        // <KW_TABLE IDENTIFIER> will directly follow an open paren in these
        // cases (cases were exhaustively enumerated by querying the parser's
        // graph):
        //   1) As a subquery: (TABLE my_data ...)
        //     1.1) As a scalar subquery in a pipes operator where the operator
        //          is a non-reserved keyword:
        //           FROM t |> AGGREGATE (TABLE t) + COUNT(*)
        //           FROM t |> EXTEND (TABLE t) + COUNT(*)
        //   2) As the beginning of a parenthsized join: (TABLE my_data...JOIN
        //   3) As the first argument in a table function or procedure call
        //      argument list: MyTVF(TABLE my_data...)
        //   4) As the first parameter in a UDF or TVF declaration parameter
        //      list: CREATE FUNCTION MyUDF(TABLE my_type...)
        //   5) As the first parameter in a parenthesized "select_list" as in
        //      the CREATE MODEL statement or similar structures in the PIVOT
        //      operator:
        //      CREATE MODEL ... TRANSFORM(TABLE as_alas, ...)
        //      PIVOT(TABLE as_alias, ...)
        //   6) Delete from T then return with action (table T)
        //
        // There are certain tokens that will never preceded the open parne and
        // never follow the identifier in the parenthesized join case or
        // subquery cases. In all such cases where the token following
        // identifier is not possible for the parenthesized join but are
        // possible for the parenthesized subquery.
        if (Lookback2() == Token::KW_IN || Lookback2() == Token::IDENTIFIER ||
            (IsNonreservedKeywordToken(Lookback2()) &&
             Lookback3() != Token::KW_PIPE &&
             !(Lookback2() == Token::KW_ACTION &&
               Lookback3() == Token::KW_WITH))) {
          // This return avoids cases (4), (5), and (6) above, while ensuring
          // case 1.A is allowed.
          return token;
        }
        if (IsIdentifierOrNonreservedKeyword(Lookahead1())) {
          switch (Lookahead2()) {
            case Token::KW_WHERE:             // (TABLE table_name WHERE
            case Token::KW_UNION:             // (TABLE table_name UNION
            case Token::KW_INTERSECT:         // (TABLE table_name INTERSECT
            case Token::KW_EXCEPT_IN_SET_OP:  // (TABLE table_name EXCEPT
            case Token::KW_ORDER:             // (TABLE table_name ORDER
            case Token::KW_LIMIT:             // (TABLE table_name LIMIT
            case Token::DOT:                  // (TABLE schema_name.
            case Token::RPAREN:               // (TABLE table_name)
              return Token::KW_TABLE_FOR_TABLE_CLAUSE;
            case Token::LPAREN:  // (TABLE tvf_name(
              // There is an ambiguity when a TVF name is KW_PIVOT or
              // KW_UNPIVOT. It is not clear whether the sequence is a table
              // named `table` being pivoted or a TVF named `pivot` in a table
              // clause. The PIVOT and UNPIVOT operator existed first, so we
              // prefer that resolution.
              switch (Lookahead1()) {
                case Token::KW_PIVOT:
                case Token::KW_UNPIVOT:
                  break;
                default:
                  return Token::KW_TABLE_FOR_TABLE_CLAUSE;
              }
              break;
            default:
              break;
          }
        }
      }
      break;
    case Token::KW_DEPTH:
      if (Lookback1() == Token::KW_WITH &&
          Lookback2() == Token::LB_CLOSE_ALIASED_QUERY) {
        // This case is the depth clause after the WITH RECURSIVE clause.
        current_token_->lookback_override = Token::LB_END_OF_WITH_RECURSIVE;
      }
      break;
    case Token::KW_OPTIONS:
      if (Lookback2() == Token::LB_WITH_IN_WITH_OPTIONS &&
          Lookahead1() == Token::LPAREN) {
        return Token::KW_OPTIONS_IN_WITH_OPTIONS;
      }
      break;
    case Token::KW_UPDATE:
    case Token::KW_REPLACE:
      if (Lookback1() == Token::KW_INSERT) {
        // The INSERT token is interesting when it starts the INSERT statement
        // whether a top level statement or nested DML. The first few tokens
        // include several unreserved keywords and identifiers
        bool insert_starts_statement =
            IsValidPreviousTokenToSqlStatement(lookback_2_) ||
            Lookback2() == Token::LB_OPEN_NESTED_DML;
        // Ideally we would like to treat UPDATE and REPLACE as if they were
        // reserved keyword here so they work like KW_IGNORE. However, the hard
        // coded mini-parser initially implemented for the Bison parser consumed
        // an entire `generalized_path_expression` and checked whether its text
        // image was 'UPDATE' or 'REPLACE'. That meant `update.whatever` was
        // the path expression, and its image was not simply 'UPDATE', so it
        // was not recognized as the insert mode. This check preserves that
        // behavior.
        bool token_starts_path =
            Lookahead1() == Token::DOT || Lookahead1() == Token::LBRACK;
        if (insert_starts_statement && !token_starts_path) {
          return token == Token::KW_UPDATE ? Token::KW_UPDATE_AFTER_INSERT
                                           : Token::KW_REPLACE_AFTER_INSERT;
        }
      }
      break;
    case Token::KW_EXPLAIN:
      if (IsValidPreviousTokenToSqlStatement(lookback_1_)) {
        current_token_->lookback_override = Token::LB_EXPLAIN_SQL_STATEMENT;
      }
      break;
    case Token::KW_NOT: {
      // This returns a different token because returning KW_NOT would confuse
      // the operator precedence parsing. Boolean NOT has a different
      // precedence than NOT BETWEEN/IN/LIKE/DISTINCT.
      switch (Lookahead1()) {
        case Token::KW_BETWEEN:
        case Token::KW_IN:
        case Token::KW_LIKE:
        case Token::KW_DISTINCT:
          return Token::KW_NOT_SPECIAL;
        default:
          break;
      }
      break;
    }
    case Token::KW_WITH: {
      // The WITH expression uses a function-call like syntax and is followed by
      // the open parenthesis and at least one variable definition consisting
      // of an identifier followed by KW_AS.
      if (Lookahead1() == Token::LPAREN &&
          (IsIdentifierOrNonreservedKeyword(Lookahead2())) &&
          Lookahead3() == Token::KW_AS) {
        return Token::KW_WITH_STARTING_WITH_EXPRESSION;
      }
      break;
    }
    case Token::KW_EXCEPT: {
      // EXCEPT is used in two locations of the language. And when the parser is
      // exploding the rules it detects that two rules can be used for the same
      // syntax.
      //
      // This rule generates a special token for an EXCEPT that is followed by a
      // hint, ALL or DISTINCT which is distinctly the set operator use.
      switch (Lookahead1()) {
        case Token::LPAREN:
          // This is the SELECT * EXCEPT (column...) case.
          return Token::KW_EXCEPT;
        case Token::KW_ALL:
        case Token::KW_DISTINCT:
          // This is the {query} EXCEPT (ALL|DISTINCT) {query} case.
          return Token::KW_EXCEPT_IN_SET_OP;
        case Token::ATSIGN:
          switch (Lookahead2()) {
            case Token::DECIMAL_INTEGER_LITERAL:
            case Token::HEX_INTEGER_LITERAL:
            case Token::LBRACE:
              // This is the {query} EXCEPT opt_hint (ALL|DISTINCT) {query}
              // case.
              return Token::KW_EXCEPT_IN_SET_OP;
            default:
              break;
          }
          break;
        default:
          break;
      }
      return Token::KW_EXCEPT_IN_UNEXPECTED_CONTEXT;
    }
    // Looking ahead to see if the next token is UPDATE to avoid a shift/reduce
    // conflict with FOR SYSTEM_TIME and FOR SYSTEM.
    case Token::KW_FOR:
      if (Lookahead1() == Token::KW_UPDATE) {
        return Token::KW_FOR_BEFORE_LOCK_MODE;
      }
      break;
    case Token::KW_FULL:
    case Token::KW_LEFT:
    case Token::KW_INNER: {
      // If FULL, LEFT, or INNER are used in set operations, return
      // KW_*_IN_SET_OP instead.
      Token lookahead =
          Lookahead1() == Token::KW_OUTER ? Lookahead2() : Lookahead1();
      switch (lookahead) {
        case Token::KW_UNION:
        case Token::KW_INTERSECT:
        case Token::KW_EXCEPT: {
          switch (token) {
            case Token::KW_FULL:
              return Token::KW_FULL_IN_SET_OP;
            case Token::KW_LEFT:
              return Token::KW_LEFT_IN_SET_OP;
            case Token::KW_INNER:
              return Token::KW_INNER_IN_SET_OP;
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
      break;
    }
    case Token::KW_SEQUENCE: {
      // Force the KW_SEQUENCEs to IDENTIFIERs if they are followed by
      // KW_CLAMPED to allow the resolution for statements like
      // SELECT some_func(sequence CLAMPED BETWEEN 1 AND 2).
      //
      // Without this transformation, bison believes CLAMPED is a sequence arg
      // and reports "Syntax error: Expected ")" but got keyword BETWEEN".
      //
      // See the comment section "AMBIGUOUS CASE 13: SEQUENCE CLAMPED" in
      // googlesql.tm for more information.
      if (Lookahead1() == Token::KW_CLAMPED) {
        return Token::IDENTIFIER;
      }
      break;
    }
    case Token::ATSIGN: {
      switch (Lookahead1()) {
        case Token::DECIMAL_INTEGER_LITERAL:
        case Token::HEX_INTEGER_LITERAL:
          if (Lookahead2() == Token::ATSIGN && Lookahead3() == Token::LBRACE) {
            // This is a hint with both the integer and the key-value list.
            // Like: @5 @{a=b}. We give a special prefix token here so that the
            // parser can handle this case without lookahead. Avoiding lookahead
            // here lets us, in turn, better identify where the statement starts
            // after a statement level hint.
            return Token::OPEN_INTEGER_PREFIX_HINT;
          }
          return Token::KW_OPEN_INTEGER_HINT;
        case Token::LBRACE:
          return Token::KW_OPEN_HINT;
        default:
          break;
      }
      break;
    }
    case Token::LT: {
      // Adjacent "<" and ">" become "<>".
      if (Lookback1() != Token::KW_STRUCT && Lookahead1() == Token::GT &&
          IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
        FuseLookahead1IntoCurrent(Token::KW_NOT_EQUALS_SQL_STYLE);
      }
      return current_token_->token.kind;
    }
    case Token::GT: {
      // Adjacent ">" and ">" become ">>".
      if (!IsInTemplatedTypeState() && Lookahead1() == Token::GT &&
          IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
        FuseLookahead1IntoCurrent(Token::KW_SHIFT_RIGHT);
      }
      return current_token_->token.kind;
    }
    case Token::LPAREN: {
      if (IsValidLookbackToStartQuery()) {
        current_token_->lookback_override = Token::LB_PAREN_OPENS_QUERY;
      }
      PushState(Token::LPAREN);
      break;
    }
    case Token::RPAREN: {
      if (!PopStateIfMatch(Token::LPAREN)) {
        // This is an unmatched ')'. We push it onto `state_stack_` to end
        // the kInTemplatedType state, if it exists, to preserve the Flex
        // behavior.
        // TODO: b/333926361 - Report an error directly.
        PushState(Token::RPAREN);
      }
      break;
    }
    case Token::DECIMAL_INTEGER_LITERAL:
    case Token::HEX_INTEGER_LITERAL: {
      TransformIntegerLiteral();
      return current_token_->token.kind;
    }
    case Token::DOT: {
      return TransformDotSymbol();
    }
    case Token::EXP_IN_FLOAT_NO_SIGN:
    case Token::STANDALONE_EXPONENT_SIGN: {
      return Token::IDENTIFIER;
    }
    // TODO: b/333926361 - If the token is YYEOF without errors, check whether
    // `state_stack_` has '(' and if yes, report an error.
    default: {
      break;
    }
  }

  return token;
}

absl::Status LookaheadTransformer::OverrideNextTokenLookback(
    bool parser_lookahead_is_empty, Token expected_next_token,
    Token lookback_token) {
  GOOGLESQL_RET_CHECK(current_token_.has_value()) << "current_token_ not populated.";
  TokenWithOverrideError& next_token =
      parser_lookahead_is_empty ? *lookahead_1_ : *current_token_;
  if (next_token.token.kind != expected_next_token) {
    return absl::OkStatus();
  }
  next_token.lookback_override = lookback_token;
  return absl::OkStatus();
}

bool LookaheadTransformer::LookbackTokenCanBeBeforeDotInPathExpression(
    Token token_kind) const {
  ABSL_DCHECK(token_kind != Token::EXP_IN_FLOAT_NO_SIGN);
  ABSL_DCHECK(token_kind != Token::STANDALONE_EXPONENT_SIGN);
  switch (token_kind) {
    case Token::IDENTIFIER:
    case Token::RPAREN:
    case Token::RBRACK:
    case Token::QUEST:
      return true;
    default:
      break;
  }
  return IsNonreservedKeywordToken(token_kind);
}

static bool IsPlusOrMinus(Token token_kind) {
  return token_kind == Token::PLUS || token_kind == Token::MINUS;
}

bool LookaheadTransformer::FuseExponentPartIntoFloatingPointLiteral() {
  if (!IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
    return false;
  }
  switch (Lookahead1()) {
    case Token::STANDALONE_EXPONENT_SIGN: {
      // The first token is 'E', check whether it has a sign ('+' or '-') and an
      // integer following it to form floats like "E+10".
      if (!IsPlusOrMinus(Lookahead2()) ||
          !IsAdjacentPrecedingToken(lookahead_1_, lookahead_2_)) {
        return false;
      }
      if (Lookahead3() != Token::DECIMAL_INTEGER_LITERAL ||
          !IsAdjacentPrecedingToken(lookahead_2_, lookahead_3_)) {
        return false;
      }
      // Now we have adjacent tokens that can form the exponential part of a
      // floating point literal, for example "E+10". Fuse the three tokens
      // together.
      FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
      FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
      FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
      return true;
    }
    case Token::EXP_IN_FLOAT_NO_SIGN: {
      // For example "E10".
      FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
      return true;
    }
    default: {
      return false;
    }
  }
}

Token LookaheadTransformer::TransformDotSymbol() {
  if (LookbackTokenCanBeBeforeDotInPathExpression(Lookback1())) {
    // This dot is part of a path expression, return '.' directly.
    current_token_->lookback_override = Token::LB_DOT_IN_PATH_EXPRESSION;
    return Token::DOT;
  }
  if (Lookahead1() == Token::DECIMAL_INTEGER_LITERAL &&
      IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
    // This dot is the start of a floating point literal, e.g. ".1". Fuse it
    // with the integer literal and potentially an exponent part after it, if it
    // exists.
    FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
    FuseExponentPartIntoFloatingPointLiteral();
    return Token::FLOATING_POINT_LITERAL;
  }
  return Token::DOT;
}

void LookaheadTransformer::TransformIntegerLiteral() {
  Token initial_kind = current_token_->token.kind;
  ABSL_DCHECK(initial_kind == Token::DECIMAL_INTEGER_LITERAL ||
         initial_kind == Token::HEX_INTEGER_LITERAL);

  if (Lookback1() == Token::LB_DOT_IN_PATH_EXPRESSION) {
    // Integer literals, for example "123" or "0x01", and identifiers that start
    // with digits, for example "123abc" are allowed in path expressions.
    if (IsKeywordOrUnquotedIdentifier(lookahead_1_->token) &&
        IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
      FuseLookahead1IntoCurrent(Token::IDENTIFIER);
    } else {
      current_token_->token.kind = Token::IDENTIFIER;
    }
    return;
  }
  // Converts Token::DECIMAL_INTEGER_LITERAL and Token::HEX_INTEGER_LITERAL to
  // be Token::INTEGER_LITERAL.
  current_token_->token.kind = Token::INTEGER_LITERAL;
  // Decimal integers can be the start of a floating point literal, hex integers
  // cannot.
  if (initial_kind != Token::DECIMAL_INTEGER_LITERAL) {
    return;
  }
  switch (Lookahead1()) {
    case Token::DOT:
      if (!IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
        return;
      }
      // This is a floating point literal, for example "1.". Check whether it
      // has (1) digits after the floating point, and (2) an exponent part.
      FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
      if (Lookahead1() == Token::DECIMAL_INTEGER_LITERAL &&
          IsAdjacentPrecedingToken(current_token_, lookahead_1_)) {
        // The floating point literal has digits after the dot, e.g. "1.1".
        FuseLookahead1IntoCurrent(Token::FLOATING_POINT_LITERAL);
      }
      // Check whether it has an exponent part as well.
      FuseExponentPartIntoFloatingPointLiteral();
      return;
    case Token::EXP_IN_FLOAT_NO_SIGN:
    case Token::STANDALONE_EXPONENT_SIGN:
      // This can be a floating point literal without dot, e.g. "2E10".
      FuseExponentPartIntoFloatingPointLiteral();
      break;
    default:
      break;
  }
}

Token LookaheadTransformer::SetOverrideErrorAndReturnEof(
    absl::string_view error_message, const Location& error_location) {
  if (!current_token_.has_value()) {
    current_token_.emplace();
  }
  current_token_->token.kind = Token::EOI;
  current_token_->error = MakeError(error_message, error_location);
  return Token::EOI;
}

namespace {
class NoOpExpander : public MacroExpanderBase {
 public:
  explicit NoOpExpander(std::unique_ptr<TokenProvider> token_provider)
      : token_provider_(std::move(token_provider)) {}
  absl::StatusOr<TokenWithLocation> GetNextToken() override {
    return token_provider_->ConsumeNextToken();
  }
  int num_unexpanded_tokens_consumed() const override {
    return token_provider_->num_consumed_tokens();
  }

 private:
  std::unique_ptr<TokenProvider> token_provider_;
};
}  // namespace

Token LookaheadTransformer::Lookahead1() const {
  return lookahead_1_->token.kind;
}

Token LookaheadTransformer::Lookahead2() const {
  return lookahead_2_->token.kind;
}

Token LookaheadTransformer::Lookahead3() const {
  return lookahead_3_->token.kind;
}

void LookaheadTransformer::PopulateLookaheads() {
  if (!lookahead_1_.has_value()) {
    FetchNextToken(current_token_, lookahead_1_);
  }
  if (!lookahead_2_.has_value()) {
    FetchNextToken(lookahead_1_, lookahead_2_);
  }
  if (!lookahead_3_.has_value()) {
    FetchNextToken(lookahead_2_, lookahead_3_);
  }
}

Token LookaheadTransformer::Lookback1() const {
  return GetLookbackToken(lookback_1_);
}

Token LookaheadTransformer::Lookback2() const {
  return GetLookbackToken(lookback_2_);
}

Token LookaheadTransformer::Lookback3() const {
  return GetLookbackToken(lookback_3_);
}

Token LookaheadTransformer::GetNextToken(absl::string_view* text,
                                         Location* yylloc) {
  // Advance the token buffers.
  lookback_3_.swap(lookback_2_);
  lookback_2_.swap(lookback_1_);
  lookback_1_.swap(current_token_);
  current_token_.swap(lookahead_1_);
  lookahead_1_.swap(lookahead_2_);
  lookahead_2_.swap(lookahead_3_);
  FetchNextToken(lookahead_2_, lookahead_3_);

  current_token_->token.kind = ApplyTokenDisambiguation(current_token_->token);
  // If the current token is Token::EOI after disambiguation, set all the
  // lookaheads to be the same as `current_token_` so that future calls to
  // GetNextToken() and GetOverrideError() return the same token kind and error.
  if (current_token_->token.kind == Token::EOI) {
    ResetToEof(*current_token_, lookahead_1_);
    ResetToEof(*current_token_, lookahead_2_);
    ResetToEof(*current_token_, lookahead_3_);
  }

  *text = current_token_->token.text;

  // Location offsets must be valid for the source they refer to.
  // Currently, the parser & analyzer only have the unexpanded source, so we
  // use the unexpanded offset.
  // In the future, the resolver should show the expanded location and where
  // it was expanded from. The expander would have the full location map and
  // the sources of macro definitions as well, so we would not need this
  // adjustment nor the `topmost_invocation_location` at all, since the
  // expander will be able to provide the stack. All layers, however, will
  // need to ask for that mapping.
  *yylloc = current_token_->token.topmost_invocation_location.IsValid()
                ? current_token_->token.topmost_invocation_location
                : current_token_->token.location;

  // LB tokens should only be used in the lookback_override variable. They
  // should never be returned as the current token.
  ABSL_DCHECK(!IsLookbackToken(current_token_->token.kind));
  return current_token_->token.kind;
}

static const MacroCatalog* empty_macro_catalog() {
  static MacroCatalog* empty_macro_catalog = new MacroCatalog();
  return empty_macro_catalog;
}

absl::StatusOr<std::unique_ptr<LookaheadTransformer>>
LookaheadTransformer::Create(
    ParserMode mode, absl::string_view filename, absl::string_view input,
    int start_offset, const LanguageOptions& language_options,
    MacroExpansionMode macro_expansion_mode,
    const macros::MacroCatalog* macro_catalog, googlesql_base::UnsafeArena* arena,
    StackFrame::StackFrameFactory& stack_frame_factory) {
  // TODO: take the token_provider as an injected dependency.
  auto token_provider = std::make_unique<TokenProvider>(
      filename, input, start_offset, /*end_offset=*/std::nullopt,
      /*offset_in_original_input=*/0);

  std::unique_ptr<MacroExpanderBase> macro_expander;
  if (macro_expansion_mode != MacroExpansionMode::kNone) {
    if (macro_catalog == nullptr) {
      macro_catalog = empty_macro_catalog();
    }
    parser::macros::MacroExpanderOptions macro_expander_options{
        .is_strict = macro_expansion_mode == MacroExpansionMode::kStrict,
    };
    macro_expander = std::make_unique<MacroExpander>(
        std::move(token_provider), *macro_catalog, arena, stack_frame_factory,
        macro_expander_options,
        /*parent_location=*/nullptr);
  } else {
    GOOGLESQL_RET_CHECK(macro_catalog == nullptr);
    macro_expander = std::make_unique<NoOpExpander>(std::move(token_provider));
  }
  return absl::WrapUnique(new LookaheadTransformer(mode, language_options,
                                                   std::move(macro_expander)));
}

LookaheadTransformer::LookaheadTransformer(
    ParserMode mode, const LanguageOptions& language_options,
    std::unique_ptr<MacroExpanderBase> expander)
    : mode_(mode),
      language_options_(language_options),
      macro_expander_(std::move(expander)) {
  // Actively fetch lookaheads.
  PopulateLookaheads();
}

int64_t LookaheadTransformer::num_lexical_tokens() const {
  return num_inserted_tokens_ +
         macro_expander_->num_unexpanded_tokens_consumed();
}

// TODO: this overload should also be updated to return the image, and
// all callers should be updated. In fact, all callers should simply use
// TokenWithLocation, and maybe have the image attached there.
absl::Status LookaheadTransformer::GetNextToken(ParseLocationRange* location,
                                                Token* token) {
  absl::string_view image;
  *token = GetNextToken(&image, location);
  return GetOverrideError();
}

bool LookaheadTransformer::IsAtEoi() const {
  if (!current_token_.has_value() || !current_token_->error.ok()) {
    return false;  // Either the token stream hasn't started or errored.
  }
  if (current_token_->token.kind == Token::EOI) {
    return true;
  }
  if (Lookahead1() == Token::EOI) {
    return lookahead_1_->error.ok();
  }
  return false;
}

void LookaheadTransformer::ResetToEof(
    const TokenWithOverrideError& template_token,
    std::optional<TokenWithOverrideError>& lookahead) const {
  lookahead = template_token;
  lookahead->token.kind = Token::EOI;
}

void LookaheadTransformer::PushParserMode(ParserMode mode) {
  restore_modes_.push(mode_);
  mode_ = mode;
}

void LookaheadTransformer::PopParserMode() {
  ABSL_DCHECK(!restore_modes_.empty());
  mode_ = restore_modes_.top();
  restore_modes_.pop();
}

absl::Status LookaheadTransformer::OverrideCurrentTokenLookback(
    Token new_token_kind) {
  GOOGLESQL_RET_CHECK(current_token_.has_value());
  current_token_->lookback_override = new_token_kind;
  return absl::OkStatus();
}

void LookaheadTransformer::PushState(StateType state) {
  state_stack_.push(state);
}

bool LookaheadTransformer::PopStateIfMatch(StateType target_state) {
  if (state_stack_.empty() || state_stack_.top() != target_state) {
    return false;
  }
  state_stack_.pop();
  return true;
}

bool LookaheadTransformer::IsInTemplatedTypeState() const {
  return !state_stack_.empty() && state_stack_.top() == kInTemplatedType;
}

}  // namespace parser
}  // namespace googlesql
