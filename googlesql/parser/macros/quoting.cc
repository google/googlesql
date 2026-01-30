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

#include "googlesql/parser/macros/quoting.h"

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

// Returns the actual string of a quote kind.
absl::string_view QuoteStr(const QuoteKind& quote_kind) {
  switch (quote_kind) {
    case QuoteKind::kNone:
      return "";
    case QuoteKind::kBacktick:
      return "`";
    case QuoteKind::kOneSingleQuote:
      return "'";
    case QuoteKind::kThreeSingleQuotes:
      return "'''";
    case QuoteKind::kOneDoubleQuote:
      return "\"";
    case QuoteKind::kThreeDoubleQuotes:
      return "\"\"\"";
  }
}

static absl::string_view QuoteKindDescription(const QuoteKind quote_kind) {
  switch (quote_kind) {
    case QuoteKind::kNone:
      return "<none>";
    case QuoteKind::kBacktick:
      return "backtick";
    case QuoteKind::kOneSingleQuote:
      return "single quote";
    case QuoteKind::kThreeSingleQuotes:
      return "three single quotes";
    case QuoteKind::kOneDoubleQuote:
      return "double quote";
    case QuoteKind::kThreeDoubleQuotes:
      return "three double quotes";
  }
}

std::string QuotingSpec::Description() const {
  switch (literal_kind()) {
    case LiteralTokenKind::kNonLiteral:
      return "non literal";
    case LiteralTokenKind::kBacktickedIdentifier:
      return "quoted identifier";
    case LiteralTokenKind::kStringLiteral:
      return absl::StrFormat("string literal(%s%s)",
                             QuoteKindDescription(quote_kind()),
                             prefix().length() == 1 ? " raw" : "");
    case LiteralTokenKind::kBytesLiteral:
      return absl::StrFormat("bytes literal(%s%s)",
                             QuoteKindDescription(quote_kind()),
                             prefix().length() == 2 ? " raw" : "");
  }
}

// Given a valid SQL literal or backticked identifier, retrieve the string value
// of its contents.
// REQUIRES: the input text must represent a single, valid SQL literal or
//           backticked identifier, and the input quoting must be the result of
//           calling FindQuotingKind() on the input literal.
static absl::string_view GetLiteralContents(const absl::string_view literal,
                                            const QuotingSpec quoting_spec) {
  absl::string_view quote = QuoteStr(quoting_spec.quote_kind());
  return literal.substr(
      quoting_spec.prefix().length() + quote.length(),
      literal.length() - quoting_spec.prefix().length() - 2 * quote.length());
}

absl::StatusOr<QuotingSpec> QuotingSpec::FindQuotingKind(
    const absl::string_view text) {
  GOOGLESQL_RET_CHECK_GE(text.length(), 2);

  if (text.front() == '`') {
    GOOGLESQL_RET_CHECK_EQ(text.front(), text.back());
    return QuotingSpec::BacktickedIdentifier();
  }

  // This a string or a bytes literal
  int prefix_end_offset = 0;
  while (prefix_end_offset < text.length() && text[prefix_end_offset] != '\'' &&
         text[prefix_end_offset] != '"') {
    prefix_end_offset++;
  }

  absl::string_view prefix = text.substr(0, prefix_end_offset);
  GOOGLESQL_RET_CHECK_LE(prefix.length(), 2);
  bool is_bytes = false;
  for (char c : prefix) {
    if (c == 'b' || c == 'B') {
      is_bytes = true;
    }
  }

  absl::string_view literal_without_prefix = text.substr(prefix_end_offset);
  GOOGLESQL_RET_CHECK_EQ(literal_without_prefix.front(), literal_without_prefix.back());

  bool is_triple_quoted =
      (literal_without_prefix.length() >= 6 &&
       literal_without_prefix.front() == literal_without_prefix[1]);

  QuoteKind quote = QuoteKind::kNone;
  if (literal_without_prefix.front() == '\'') {
    quote = is_triple_quoted ? QuoteKind::kThreeSingleQuotes
                             : QuoteKind::kOneSingleQuote;
  } else {
    GOOGLESQL_RET_CHECK_EQ(literal_without_prefix.front(), '"');
    quote = is_triple_quoted ? QuoteKind::kThreeDoubleQuotes
                             : QuoteKind::kOneDoubleQuote;
  }

  return is_bytes ? QuotingSpec::BytesLiteral(prefix, quote)
                  : QuotingSpec::StringLiteral(prefix, quote);
}

absl::StatusOr<QuotingSpec> QuotingSpec::FindQuotingKind(
    const absl::string_view text, absl::string_view& out_literal_contents) {
  GOOGLESQL_ASSIGN_OR_RETURN(QuotingSpec quoting, QuotingSpec::FindQuotingKind(text));
  out_literal_contents = GetLiteralContents(text, quoting);
  return quoting;
}

std::string QuoteText(const absl::string_view escaped_text,
                      const QuotingSpec& quoting_spec) {
  absl::string_view quote = QuoteStr(quoting_spec.quote_kind());
  return absl::StrCat(quoting_spec.prefix(), quote, escaped_text, quote);
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
