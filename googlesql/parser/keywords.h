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

// An API that provides information on GoogleSQL keywords.
#ifndef GOOGLESQL_PARSER_KEYWORDS_H_
#define GOOGLESQL_PARSER_KEYWORDS_H_

#include <optional>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/base/case.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace googlesql {
namespace parser {

// Metadata for a keyword.
class KeywordInfo {
 public:
  KeywordInfo(absl::string_view keyword, std::optional<Token> reserved_token,
              std::optional<Token> nonreserved_token)
      : keyword_(absl::AsciiStrToUpper(keyword)),
        reserved_token_(reserved_token),
        nonreserved_token_(nonreserved_token) {
    ABSL_DCHECK(reserved_token.has_value() || nonreserved_token.has_value())
        << "Either reserved or nonreserved must have a token";
  }

  // The keyword, in upper case.
  const std::string& keyword() const { return keyword_; }

  // The parser token for this keyword, when it is reserved.
  // Valid only when CanBeReserved() is true.
  Token reserved_token() const { return reserved_token_.value(); }

  // The parser token for this keyword, when it is nonreserved.
  // Valid only when IsAlwaysReserved() is false.
  Token nonreserved_token() const { return nonreserved_token_.value(); }

  // True if this keyword can be reserved under any LanguageOptions.
  bool CanBeReserved() const { return reserved_token_.has_value(); }

  // True if this keyword is reserved under all LanguageOptions.
  bool IsAlwaysReserved() const { return !nonreserved_token_.has_value(); }

  // True if this keyword can be either reserved or nonreserved, depending on
  // the LanguageOptions.
  bool IsConditionallyReserved() const {
    return reserved_token_.has_value() && nonreserved_token_.has_value();
  }

 private:
  std::string keyword_;

  // The parser token code when this keyword is reserved, or std::nullopt if
  // this keyword is never reserved.
  std::optional<Token> reserved_token_;

  // The parser token code when this keyword is nonreserved, or std::nullopt
  // if this keyword is always reserved.
  std::optional<Token> nonreserved_token_;
};

// Returns the KeywordInfo for keyword 'keyword' (case insensitively), or
// nullptr if 'keyword' is not a keyword.
const KeywordInfo* GetKeywordInfo(absl::string_view keyword);

// Returns the KeywordInfo for token `token`, or nullptr if the `token` is not a
// keyword token.
//
// For conditionally reserved keywords, both the reserved and nonreserved tokens
// are accepted.
const KeywordInfo* GetKeywordInfoForToken(Token token);

// Returns a vector of all keywords with their metadata.
const std::vector<KeywordInfo>& GetAllKeywords();

// Returns true if `identifier` should be treated as a keyword for
// GetParseTokens().
bool IsKeywordInTokenizer(absl::string_view identifier);

// Returns true if 'identifier' is not a keyword but it still has a special
// meaning when used as an unescaped identifier. For instance, if identifier is
// 'current_date', then emitting it as CURRENT_DATE would cause it to be
// interpreted with a special meaning (a call to the CURRENT_DATE() function),
// even though CURRENT_DATE is not a reserved keyword. As a result, the
// identifier should be escaped when it is intended to be used as the identifier
// and not the special keyword meaning.
bool NonReservedIdentifierMustBeBackquoted(absl::string_view identifier);

// For special keywords like NOT_SPECIAL or EXPECT_IN_SET_OP, gets a map from
// the parser's generated string (e.g. KW_NOT_SPECIAL) to the string we want to
// show in error messages or suggest in the autocomplete parser.
const absl::flat_hash_map<absl::string_view, absl::string_view>&
GetUserFacingImagesForSpecialKeywordsMap();

}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_KEYWORDS_H_
