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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_TOKEN_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_TOKEN_H_

#include <stddef.h>

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/parse_tokens.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::formatter::internal {

// String used on comments to disable formatter.
constexpr absl::string_view kDisableComment = "SQLFORMAT:OFF";

// String used on comments to reenable formatter.
constexpr absl::string_view kEnableComment = "SQLFORMAT:ON";

// Augments the ParseToken returned by the GoogleSQL tokenizer with additional
// type data.
class Token : public ParseToken {
 public:
  enum class Type {
    UNKNOWN,
    // Some tokens can act as brackets depending on the context. For instance,
    // < >.
    OPEN_BRACKET,
    CLOSE_BRACKET,
    OPEN_PROTO_EXTENSION_PARENTHESIS,
    CLOSE_PROTO_EXTENSION_PARENTHESIS,
    UNARY_OPERATOR,
    // The '=' operator in a parameter-value assignment item within an OPTIONS
    // or DEFINE TABLE list.
    //
    // NOTE: This does not include '=' in SET or CREATE CONSTANT assignments
    // since this styling is handled correctly by the 'NumFusibleTokens'
    // function already; if needed we could classify these as
    // ASSIGNMENT_OPERATOR in the future.
    ASSIGNMENT_OPERATOR,
    // Macro and table names must have a space before opening parenthesis in
    // expressions like DEFINE MACRO MACRO_NAME (..body..); and
    // DEFINE TABLE TableName (..options..)
    MACRO_NAME_IN_DEFINE_STMT,
    TABLE_NAME_IN_DEFINE_STMT,
    // Some tokens can act as top level keywords depending on the context.
    // For example, "RETURNS" and "AS".
    TOP_LEVEL_KEYWORD,
    // Tokens used as parts of privilege types within GRANT & REVOKE statements.
    PRIVILEGE_TYPE_KEYWORD,
    // Marks `ON` or `USING`, when part of a `JOIN` clause.
    JOIN_CONDITION_KEYWORD,
    // Marks keywords that are part of a `WITH OFFSET AS` sequence in a `FROM`
    // clause; note that keywords in SQL like `WITH Offset AS (SELECT * FROM
    // Foo)`, are *not* marked.
    WITH_OFFSET_AS_CLAUSE,
    // Tokens 'EXCEPT' and 'REPLACE', when they are used after '*' to adjust the
    // resulting selected columns.  (Note that 'REPLACE' has a double meaning,
    // also being a built-in string function.)
    SELECT_STAR_MODIFIER,
    // Used to denote something that looks like a reserved keyword but is
    // actually a part of an identifier.
    KEYWORD_AS_IDENTIFIER_FRAGMENT,
    // Token is marked as invalid when GoogleSQL tokenizer fails to parse it.
    INVALID_TOKEN,
    // Token representing a code block between "SQLFORMAT:OFF" and
    // "SQLFORMAT:ON" comments.
    NO_FORMAT,
    // Marks right hand operand of a legacy set statement: "SET variable value".
    // The statement doesn't follow GoogleSQL syntax: it allows hyphens in the
    // value and can end without a trailing semicolon.
    LEGACY_SET_OPERAND,
    // Token representing the block "<<EOF..EOF" in a legacy DEFINE TABLE
    // statement:
    // DEFINE TABLE name <<EOF
    //   <definition>
    // EOF;
    LEGACY_DEFINE_TABLE_BODY,
    // Marks following tokens as part of comparison operators:
    // * AND in BETWEEN..AND operator;
    // * NOT before BETWEEN, LIKE or IN.
    // Note: NOT inside "IS NOT" is ignored: we mark only the first NOT in a
    // chunk to make it easy to recognize a chunk that starts with a comparison
    // operator.
    COMPARISON_OPERATOR,
    // Marks a token that starts a list (comma-separated expressions).
    STARTS_LIST,
    // Marks a {token} in curly braces. These are not part of GoogleSQL, but
    // widely used by various templating frameworks.
    CURLY_BRACES_TEMPLATE,
    // Marks a token that starts a complex identifier, but otherwise could act
    // as something else. For example a '/' in table names: /foo/bar should not
    // be treated as a division operator.
    COMPLEX_TOKEN_START,
    // Marks a token that should be always treated as the continuation of the
    // previous token without any spaces in between. For instance,
    // `table_$DATE_SUFFIX` consists of two tokens: `table_` and `$DATE_SUFFIX`.
    COMPLEX_TOKEN_CONTINUATION,
    // Marks "AS" keyword inside CAST function.
    CAST_AS,
    // Token that might be a keyword inside a DDL statement.
    DDL_KEYWORD,
    // Marks builtin-functions, some of which may be multipart, e.g.
    // D3A_COUNT.INIT()
    BUILTIN_FUNCTION,
    // Marks open brackets inside braced (map) constructors.
    BRACED_CONSTR_OPEN_BRACKET,
    // Marks close brackets inside braced (map) constructors.
    BRACED_CONSTR_CLOSE_BRACKET,
    // Marks colons in braced constructors.
    BRACED_CONSTR_COLON,
    // Marks the beginning of a field name in braced constructor. It can be '('
    // for proto extensions.
    BRACED_CONSTR_FIELD,
    // Marks an inline comment that annotates the following string literal.
    // Example: /*sql*/'''select 1'''
    STRING_ANNOTATION_COMMENT,
    // Marks SET keyword that starts a SET statement as opposed to other usages
    // of SET keyword.
    SET_STATEMENT_START,
    // Marks the first token hat starts a SET operator, e.g., 'FULL' in
    // "FULL OUTER UNION ALL".
    SET_OPERATOR_START,
    // Marks a dot that separates chained method calls.
    CHAINED_METHOD_DOT_SEPARATOR,
  };

  explicit Token(ParseToken t)
      : ParseToken(std::move(t)), keyword_(ParseToken::GetKeyword()) {}
  Token(ParseToken t, absl::string_view keyword)
      : ParseToken(std::move(t)), keyword_(keyword) {}
  // Replaces a ParseToken with the same one with a new keyword and image.
  Token(ParseToken t, absl::string_view keyword, absl::string_view image)
      : ParseToken(t.GetLocationRange(), t.IsAdjacentToPreviousToken(),
                   std::string(image), t.kind()),
        keyword_(keyword) {}
  Token(const Token& t) = default;
  // Sets the additional semantic type of the token.
  void SetType(Type type) { type_ = type; }
  // Checks if the current token is of a given type.
  bool Is(Type type) const { return type_ == type; }
  // Checks if the current token is one of the given types.
  bool IsOneOf(std::initializer_list<Type> types) const;
  // Returns the token type.
  Token::Type GetType() const { return type_; }

  absl::string_view GetKeyword() const { return keyword_; }

  // Returns true if the token is a comment that is on the same line with the
  // next token(s) in the input statement.
  bool IsInlineComment() const;

  // Returns true if the token is an end-of-line comment, i.e., it is on the
  // same line with the previous token(s) in the input statement, but has at
  // least one line break after.
  bool IsEndOfLineComment() const;

  // Returns true if the token is a /*comment*/ which can be multiline or not.
  bool IsSlashStarComment() const;

  // Checks if the current token is a reserved keyword.
  bool IsReservedKeyword() const;

  // Returns true if the current token is marked by tokenizer as keyword and it
  // is an alphabetic keyword (as opposed to punctuation keywords).
  bool IsNonPunctuationKeyword() const;

  // Returns true if the current token is a macro call, e.g. $MY_MACRO.
  bool IsMacroCall() const;

  // Returns true if the current token is the keyword used in a CASE expression
  // (one of: "WHEN", "THEN", "ELSE", "END", but not "CASE" itself).
  bool IsCaseExprKeyword() const;

  // Returns true if the current token is a string literal.
  bool IsStringLiteral() const;

  // Returns true if the current token is a bytes literal.
  bool IsBytesLiteral() const;

  // Returns true if the current token is marked as open angle bracket: "<" (but
  // not a comparison operator).
  bool IsOpenAngleBracket() const;
  // Returns true if the current token is marked as close angle bracket: ">"
  // (but not a comparison operator).
  bool IsCloseAngleBracket() const;

  // Returns true if the current token is a comment that annotates the following
  // string literal as embedded SQL. Example: /*sql*/'''select 1'''
  bool IsEmbeddedSqlAnnotation() const;

  // Returns true if the current token is a comment that annotates the following
  // string literal as embedded proto. Example: /*proto*/'''foo: 1'''
  bool IsEmbeddedProtoAnnotation() const;

  // Checks if the current token may be an identifier (but may be also a non
  // reserved keyword).
  bool MayBeIdentifier() const;

  // Checks if the current token may act as identifier start.
  bool MayBeStartOfIdentifier() const;

  // Checks whether the current token may belong to a multi-token identifier if
  // the `prev_token` is a part of identifier too.
  bool MayBeIdentifierContinuation(const Token& prev_token) const;

  // Allows marking non-reserved keywords (which can act as identifiers) as
  // keywords.
  void MarkUsedAsKeyword() { used_as_keyword_ = true; }
  bool UsedAsKeyword() const { return used_as_keyword_; }

  // Number of empty lines before/after the token.
  void SetLineBreaksAfter(int n) { line_breaks_after_ = n; }
  int GetLineBreaksAfter() const { return line_breaks_after_; }

  void SetLineBreaksBefore(int n) { line_breaks_before_ = n; }
  int GetLineBreaksBefore() const { return line_breaks_before_; }

  // Allows marking multiline tokens (e.g., multiline comments).
  void MarkAsMultiline() { multiline_ = true; }
  bool IsMultiline() const { return multiline_; }

  // Returns a string that can be directly printed to the output.
  std::string ImageForPrinting(const FormatterOptions& options,
                               bool is_first_token, int new_column,
                               int original_column) const;

 private:
  std::string keyword_;  // Marking non-const so default copy constructor works.
  Type type_ = Type::UNKNOWN;
  bool used_as_keyword_ = false;
  int line_breaks_after_ = 0;
  int line_breaks_before_ = 0;
  bool multiline_ = false;
};

// Stores two lists pointing to externally owned tokens, allowing to
// iterate over the tokens skipping/preserving the comments.
class TokensView {
 public:
  // Creates view over the given `tokens`. Does not take the ownership of the
  // vector, so it must outlive the view.
  static TokensView FromTokens(std::vector<Token>* tokens);

  // Reserves space in the view similar to vector::reserve.
  void Reserve(size_t reserve_size);
  // Adds the `token` into the current view.
  void Add(Token* token);

  // Returns a list of all tokens added to the current view.
  const std::vector<Token*>& All() const { return all_; }
  // Returns a list of non-comment tokens added to the current view.
  const std::vector<Token*>& WithoutComments() const {
    return without_comments_;
  }

 private:
  std::vector<Token*> all_;
  std::vector<Token*> without_comments_;
};

// Parses given `sql` into tokens using GoogleSQL tokenizer. The string must
// contain a single SQL statement, otherwise InvalidArgument error is returned.
absl::StatusOr<std::vector<Token>> TokenizeStatement(
    absl::string_view sql, const FormatterOptions& options);
// Parses next statement from the input into tokens using given
// `parse_location`. Updates the location to point to the end of parsed input.
absl::StatusOr<std::vector<Token>> TokenizeNextStatement(
    ParseResumeLocation* parse_location, const FormatterOptions& options);

// Parses a token that GoogleSQL tokenizer was unable to parse. Updates
// `resume_location` to point after the parsed token. Optional end_position can
// be used to limit the maximum size of the invalid token and corresponds to the
// byte offset inside `resume_location`. -1 end limit == size of the input.
Token ParseInvalidToken(ParseResumeLocation* resume_location,
                        int end_position = -1);

// Returns true if the given tokens have at least one space or another token in
// between in the original input.
bool SpaceBetweenTokensInInput(const ParseToken& left, const ParseToken& right);

// Returns true if the given `keyword` is open parenthesis, square bracket or
// curly brace: "(", "[", "{".
bool IsOpenParenOrBracket(absl::string_view keyword);
// Returns true if the given `keyword` is close parenthesis, square bracket or
// curly brace: ")", "]", "}".
bool IsCloseParenOrBracket(absl::string_view keyword);
// Returns open parenthesis or bracket corresponding to the open bracket in
// `keyword`.
absl::string_view CorrespondingOpenBracket(absl::string_view keyword);
// Returns closing parenthesis or bracket corresponding to the open bracket in
// `keyword`.
absl::string_view CorrespondingCloseBracket(absl::string_view keyword);

// Returns true if str is a gORM bind keyword like BIND, BIND_COLUMNS, etc.
bool IsBindKeyword(absl::string_view str);

// Operator precedence levels. The later values have lower precedence.
// Note that kUnknownOperatorPrecedence is the last and so has lowest
// precedence.
//
// See also: OperatorPrecedenceLevel function.
enum class OperatorPrecedenceEnum {
  kFieldOrArrayAccessOperator,
  kUnaryOrBitwiseNegative,
  kMultiplicationOrDivision,
  kAdditionOrSubtraction,
  kBitwiseShift,
  kBitwiseAnd,
  kBitwiseXor,
  kBitwiseOr,
  kComparisonOperator,
  kLogicalNot,
  kLogicalAnd,
  kLogicalOr,
  kUnknownOperatorPrecedence
};

// Returns the operator precedence.
//
// If the keyword is not an operator at all kUnknownOperatorPrecedence is
// returned.
//
// OperatorPrecedenceLevel(A) < OperatorPrecedenceLevel(B)
//
// will return true if A is higher precedence than B.
//
// See also: (broken link).
OperatorPrecedenceEnum OperatorPrecedenceLevel(const Token& token);

// Searches for the next input range that includes a statement or a comment.
// Assumes that `start` points anywhere between statements. Returns an
// open-ended byte range [start, end), where `start` - is start of the first
// line containing the statement and `end` - end of the last line.
FormatterRange FindNextStatementOrComment(absl::string_view input, int start);

}  // namespace googlesql::formatter::internal

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_TOKEN_H_
