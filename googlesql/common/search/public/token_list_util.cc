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

#include "googlesql/common/search/public/token_list_util.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/strings.h"
#include "absl/base/no_destructor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql::search {
namespace {

using tokens::TextAttribute;
using tokens::TextToken;
using tokens::Token;
using tokens::TokenList;
using tokens::TokenListBuilder;

// Formats token text and attribute into 'out'.
void FormatToken(std::string& out, absl::string_view text, uint64_t attribute) {
  absl::StrAppendFormat(&out, "'%s':%d", absl::Utf8SafeCEscape(text),
                        attribute);
}

// Sorts and returns the data in 'tokens'.
std::vector<std::pair<absl::string_view, uint64_t>> SortedTokens(
    absl::Span<const Token> tokens) {
  std::vector<std::pair<absl::string_view, uint64_t>> token_info;
  for (const Token& t : tokens) {
    token_info.emplace_back(t.text(), t.attribute());
  }
  std::sort(token_info.begin(), token_info.end());
  return token_info;
}

// Formats a text token into 'out'. The text attribute is formatted using
// `format_attribute`.
void FormatTextTokenHelper(std::string& out, const TextToken& token,
                           AttributeFormatter& format_attribute) {
  absl::StrAppend(&out, "{");
  absl::StrAppendFormat(&out, "text: '%s'",
                        absl::Utf8SafeCEscape(token.text()));
  absl::StrAppend(&out, ", attribute: ");
  format_attribute(out, TextAttribute(token.attribute()));
  absl::StrAppendFormat(&out, ", index_tokens: [%s]",
                        absl::StrJoin(SortedTokens(token.index_tokens()),
                                      ", ",
                                      [](std::string* out, auto t) {
                                        FormatToken(*out, std::get<0>(t),
                                                    std::get<1>(t));
                                      }));
  absl::StrAppend(&out, "}");
}

}  // namespace

const TextToken& CanonicalGapToken() {
  static const absl::NoDestructor<TextToken> gap(TextToken::Make(""));
  return *gap;
}

void AppendPhraseGap(TokenListBuilder& builder, const int size) {
  for (int i = 0; i < size; ++i) {
    builder.Add(CanonicalGapToken());
  }
}

void DefaultFormatAttribute(std::string& out, TextAttribute attribute) {
  absl::StrAppend(&out, static_cast<uint64_t>(attribute.value()));
}

std::string FormatTokenList(const TokenList& token_list, const bool debug_mode,
                            const bool collapse_identical_tokens,
                            AttributeFormatter format_attribute) {
  if (!debug_mode) {
    // TOKENLIST doesn't have literals.
    // TODO: generate expression with standard constructor
    // functions when available.
    return absl::StrCat("CAST(", ToBytesLiteral(token_list.GetBytes()),
                        " AS TOKENLIST)");
  }

  std::vector<std::string> lines = FormatTokenLines(
      token_list, collapse_identical_tokens, std::move(format_attribute));
  if (lines.empty()) lines = {"<empty>"};
  return absl::StrJoin(lines, "\n");
}

// Returns the debug content of the TokenList as a sequence of lines, each
// representing a single token.
std::vector<std::string> FormatTokenLines(const TokenList& token_list,
                                          const bool collapse_identical_tokens,
                                          AttributeFormatter format_attribute) {
  auto iter = token_list.GetIterator();
  if (!iter.ok()) {
    return {"Invalid tokenlist encoding"};
  }
  TextToken buf, cur;
  int run_length = 0;
  std::vector<std::string> lines;
  auto add_token_line = [&](const TextToken& token, int run_length) {
    std::string out;
    FormatTextTokenHelper(out, token, format_attribute);
    if (run_length > 1) absl::StrAppend(&out, " (", run_length, " times)");
    lines.push_back(std::move(out));
  };

  while (!iter->done()) {
    if (!iter->Next(cur).ok()) {
      return {"Invalid tokenlist encoding"};
    }

    if (!collapse_identical_tokens) {
      std::string tok_str;
      FormatTextTokenHelper(tok_str, cur, format_attribute);
      lines.push_back(std::move(tok_str));
    } else {
      if (buf == cur) {
        ++run_length;
      } else {
        if (run_length > 0) {
          add_token_line(buf, run_length);
        }
        buf = std::move(cur);
        run_length = 1;
      }
    }
  }
  if (collapse_identical_tokens && run_length > 0) {
    // As long as there was at least one token, buf will contain a token that
    // has yet to be printed.
    add_token_line(buf, run_length);
  }
  return lines;
}

void FormatTextToken(std::string& out, const TextToken& token,
                     AttributeFormatter format_attribute) {
  FormatTextTokenHelper(out, token, format_attribute);
}

}  // namespace googlesql::search
