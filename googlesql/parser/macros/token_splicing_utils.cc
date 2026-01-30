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

#include "googlesql/parser/macros/token_splicing_utils.h"

#include <cctype>

#include "googlesql/parser/keywords.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

bool CanCharStartAnIdentifier(const char c) {
  return c == '_' || std::isalpha(c);
}

bool IsQuotedIdentifier(const TokenWithLocation& token) {
  return token.kind == Token::IDENTIFIER && token.text.front() == '`';
}

bool IsIdentifierCharacter(const char c) {
  return CanCharStartAnIdentifier(c) || std::isdigit(c);
}

bool IsKeywordOrUnquotedIdentifier(Token token_kind,
                                   absl::string_view token_text) {
  return GetKeywordInfo(token_text) != nullptr ||
         (token_kind == Token::IDENTIFIER && token_text.front() != '`');
}

bool IsKeywordOrUnquotedIdentifier(const TokenWithLocation& token) {
  return IsKeywordOrUnquotedIdentifier(token.kind, token.text);
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
