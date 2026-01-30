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

#ifndef GOOGLESQL_PARSER_MACROS_TOKEN_SPLICING_UTILS_H_
#define GOOGLESQL_PARSER_MACROS_TOKEN_SPLICING_UTILS_H_

#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

// Indicates whether the given character `c` can start an unquoted identifier.
bool CanCharStartAnIdentifier(char c);

// Returns true if `token` is a quoted identifier. Note that `token.kind` is
// IDENTIFIER, whether quoted or not, so the text is needed to tell the
// difference.
bool IsQuotedIdentifier(const TokenWithLocation& token);

// Indicates whether this character can be part of an unquoted identifier.
bool IsIdentifierCharacter(char c);

// Returns true if the token's text is a keyword or an unquoted identifier.
bool IsKeywordOrUnquotedIdentifier(Token token_kind,
                                   absl::string_view token_text);

// Returns true if the token's text is a keyword or an unquoted identifier.
// Convenience overload.
bool IsKeywordOrUnquotedIdentifier(const TokenWithLocation& token);

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_TOKEN_SPLICING_UTILS_H_
