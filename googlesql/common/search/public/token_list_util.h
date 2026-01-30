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

#ifndef GOOGLESQL_COMMON_SEARCH_PUBLIC_TOKEN_LIST_UTIL_H_
#define GOOGLESQL_COMMON_SEARCH_PUBLIC_TOKEN_LIST_UTIL_H_

#include <string>
#include <vector>

#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/simple_token_list.h"
#include "absl/functional/any_invocable.h"

namespace googlesql::search {

// Returns a gap token used for inserting a positional gap.
const tokens::TextToken& CanonicalGapToken();

// A typical number of positional gaps for phrase break.
constexpr int kTokenListPhraseGapSize = 25;

// Appends a number of gap tokens to the TokenListBuilder for phrase break.
void AppendPhraseGap(tokens::TokenListBuilder& builder,
                     int size = kTokenListPhraseGapSize);

// Formats the attribute as a decimal integer.
void DefaultFormatAttribute(std::string& out, tokens::TextAttribute attribute);

// A callback that formats a TextAttribute.
using AttributeFormatter =
    absl::AnyInvocable<void(std::string&, tokens::TextAttribute)>;

// Returns a string representation of a TokenList. If `debug_mode` is false,
// returns a cast expression for the TokenList, otherwise returns a
// human-readable representation. If `collapse_identical_tokens` is true,
// continuous identical tokens are collapsed into a single token string with a
// run length suffix in the debug mode. `format_attribute` is used to format
// tokens' text attributes in the debug mode.
std::string FormatTokenList(
    const tokens::TokenList& token_list, bool debug_mode = false,
    bool collapse_identical_tokens = false,
    AttributeFormatter format_attribute = DefaultFormatAttribute);

// Format the TokenList as a sequence of human-readable lines, each representing
// a single token.
std::vector<std::string> FormatTokenLines(
    const tokens::TokenList& token_list, bool collapse_identical_tokens = false,
    AttributeFormatter format_attribute = DefaultFormatAttribute);

// Returns a human-readable string representation of a TextToken.
void FormatTextToken(
    std::string& out, const tokens::TextToken& token,
    AttributeFormatter format_attribute = DefaultFormatAttribute);

}  // namespace googlesql::search

#endif  // GOOGLESQL_COMMON_SEARCH_PUBLIC_TOKEN_LIST_UTIL_H_
