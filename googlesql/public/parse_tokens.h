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

#ifndef GOOGLESQL_PUBLIC_PARSE_TOKENS_H_
#define GOOGLESQL_PUBLIC_PARSE_TOKENS_H_

#include <string>
#include <vector>

#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// ParseToken represents one element in a statement fragment parsed by
// GetParseTokens.  The token will be fully parsed, with any quoting or
// escaping removed, and with literals resolved to concrete googlesql::Values.
//
// Each token is one of the following:
// * a keyword (or symbol)
// * an identifier
// * a literal value (e.g. int64, uint64, string or bytes)
// * end_of_input
//
// A particular token may be valid as both a keyword and an identifier.
//
// This interface does not distinguish reserved keywords, other than
// the core set of googlesql reserved keywords.  An unquoted identifier can
// be treated as either a keyword or an identifier.
// All supported non-keyword symbols are also returned as keywords.
//
//                             |  IsIdentifier()  |  IsKeyword()
// ---------------------------------------------------------------
// googlesql reserved keyword  |  false           |  true
// ---------------------------------------------------------------
// unquoted identifier         |  true            |  true
// (not a reserved keyword)    |                  |
// ---------------------------------------------------------------
// quoted identifier           |  true            |  false
// ---------------------------------------------------------------
// symbol (e.g. punctation)    |  false           |  true
// ---------------------------------------------------------------
//
// The Get() methods return an empty string if the token is not of that type.
// For example, the statement "DROP TABLE Name;" could be matched as follows:
//   if (parse_tokens.size() == 4 &&
//       parse_tokens[0].GetKeyword() == "DROP" &&
//       parse_tokens[1].GetKeyword() == "TABLE" &&
//       parse_tokens[2].IsIdentifier()) {
//       parse_tokens[3].GetKeyword() == ";") {
//     DropTable(parse_tokens[2].GetIdentifier());
//   }
class ParseToken {
 public:
  bool IsEndOfInput() const { return kind_ == END_OF_INPUT; }

  // True if this token can be treated as a keyword.
  bool IsKeyword() const {
    return kind_ == KEYWORD || kind_ == IDENTIFIER_OR_KEYWORD;
  }
  // True if this token can be treated as an identifier.
  bool IsIdentifier() const {
    return kind_ == IDENTIFIER || kind_ == IDENTIFIER_OR_KEYWORD;
  }
  // True if this token is a value.
  bool IsValue() const { return kind_ == VALUE; }
  // True if this token is a comment.
  bool IsComment() const { return kind_ == COMMENT; }

  // True if there is no whitespace between this token and the previous token.
  bool IsAdjacentToPreviousToken() const { return adjacent_to_prior_token_; }

  // Get the keyword, or "".  Returns the keyword in upper case.
  std::string GetKeyword() const;

  // Get the identifier, or "".  Returns the identifier with the original case,
  // with quotes and escaping resolved.
  std::string GetIdentifier() const;

  // Returns the exact SQL string that was the input for this token.
  absl::string_view GetImage() const;

  // Get the Value for a token of kind VALUE.  Return invalid value for
  // other token types.
  //
  // Values are returned for literals of types STRING, BYTES, INT64,
  // UINT64 or DOUBLE only.
  // NULL is returned as a keyword, so the Value is never NULL.
  // TRUE/FALSE are returned as keywords, so the Value never has type BOOL.
  // Other complex multi-token literals like DATE "2011-02-03" and array
  // or struct construction literals will be returned as token sequences.
  // Negative numeric literals will be returned as a "-" token followed by
  // an integer or double value literal.
  Value GetValue() const;

  // Get a SQL string for this token.  This includes quoting and escaping as
  // necessary, but is not necessarily what was included in the original input.
  // Concatenating GetSQL() strings together (with spaces) will give a statement
  // that can be parsed back to the same tokens.
  std::string GetSQL() const;

  // Get a descriptive string for this token that includes the token kind and
  // the value.
  std::string DebugString() const;

  // Returns the location of the token in the input.
  ParseLocationRange GetLocationRange() const { return location_range_; }

  // The declarations below are intended for internal use.

  enum Kind {
    KEYWORD,                // A googlesql keyword or symbol.
    IDENTIFIER,             // An identifier that was quoted.
    IDENTIFIER_OR_KEYWORD,  // An unquoted identifier.
    VALUE,                  // A literal value.
    COMMENT,                // A comment.
    END_OF_INPUT,           // The end of the input string was reached.
  };

  Kind kind() const { return kind_; }

  // The constructors are generally only called internally.
  ParseToken();

  // <image> and <value> are passed by value so they can be moved into place.
  ParseToken(ParseLocationRange location_range, bool adjacent_to_prior_token,
             std::string image, Kind kind);
  ParseToken(ParseLocationRange location_range, bool adjacent_to_prior_token,
             std::string image, Kind kind, Value value);

 private:
  Kind kind_;
  bool adjacent_to_prior_token_;
  std::string image_;
  ParseLocationRange location_range_;
  Value value_;

  // Copyable
};

struct ParseTokenOptions {
  // Return at most this many tokens (only if positive). It is not possible to
  // resume a GetParseTokens() call for which max_tokens was set.
  int max_tokens = 0;

  // Stop parsing after a ";" token.  The last token returned will be either
  // a ";" or an EOF.
  bool stop_at_end_of_statement = false;

  // Return the comments in the ParseToken vector or silently drop them.
  bool include_comments = false;

  LanguageOptions language_options;
};

// Gets a vector of ParseTokens starting from <resume_location>, and updates
// <resume_location> to point at the location after the tokens that were
// parsed. This is used to tokenize and (partially or fully) parse a string,
// following googlesql tokenization rules for comments, quoting, literals, etc.
// Returns an error on any tokenization failure, like bad characters,
// unclosed quotes, invalid escapes, etc.
//
// By default, the entire string will be consumed and converted to
// ParseTokens.  The output will always have at least one token, and the
// last token will always have IsEndOfInput() true.
//
// With some <options>, the entire input string may not be consumed in one
// call.  In those cases, GetParseTokens can be called in a loop using
// a ParseResumeLocation to continue where it left off.  The loop can stop
// once one iteration returns a vector ending with an IsEndOfInput() token.
// This usage can be mixed with AnalyzeNextStatement and GetNextStatementKind.
// Only calls to GetParseTokens that parse an entire statement can be resumed;
// i.e., a call to GetParseTokens with option "max_tokens" cannot be resumed.
//
// Returned Statuses may be annotated with an ErrorLocation payload to indicate
// where an error occurred.
//
// Example usage that parses the statement "DROP TABLE Name;":
//
//   string statement = ...;
//   ParseResumeLocation resume_location(statement);
//   ParseTokenOptions options;
//   vector<ParseToken> parse_tokens;
//   GOOGLESQL_RETURN_IF_ERROR(GetParseTokens(options, &resume_location, &parse_tokens));
//   if (parse_tokens.size() == 4 &&
//       parse_tokens[0].GetKeyword() == "DROP" &&
//       parse_tokens[1].GetKeyword() == "TABLE" &&
//       parse_tokens[2].IsIdentifier()) {
//       parse_tokens[3].GetKeyword() == ";") {
//     DropTable(parse_tokens[2].GetIdentifier());
//   } else {
//     ...
//   }
absl::Status GetParseTokens(const ParseTokenOptions& options,
                            ParseResumeLocation* resume_location,
                            std::vector<ParseToken>* tokens);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_PARSE_TOKENS_H_
