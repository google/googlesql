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

#ifndef GOOGLESQL_PARSER_MACROS_QUOTING_H_
#define GOOGLESQL_PARSER_MACROS_QUOTING_H_

#include <algorithm>
#include <string>

#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace parser {
namespace macros {

enum class LiteralTokenKind {
  kNonLiteral,
  kBacktickedIdentifier,
  kStringLiteral,
  kBytesLiteral,
};

enum class QuoteKind {
  kNone,  // Used for non-literals and for backticked identifiers
  kBacktick,
  kOneSingleQuote,
  kOneDoubleQuote,
  kThreeSingleQuotes,
  kThreeDoubleQuotes,
};

// Describes the quoting spec of a SQL token. There are 4 main kinds,
// represented by literal_token_kind. STRING and BYTES literals have variations,
// e.g. one single quote, raw bytes with three double quotes, etc.
// 'quote_kind' and 'prefix' capture these finer variations. The 'prefix'
// contains the optional 'r' or 'b' modifiers that indicate raw or bytes.
// Note that the prefix doesn't care about modifier order or case, e.g. 'bR' and
// 'rB' are equivalent. Instead of normalizing them, we keep the original prefix
// for round-tripping, to avoid changing the user input when serializing the
// final expanded result.
// The 'prefix' is a string_view from the input used in creating the
// QuotingSpec. The caller is expected to make the source string outlive this
// QuotingSpec.
class QuotingSpec {
 public:
  static QuotingSpec NonLiteral() {
    return QuotingSpec(LiteralTokenKind::kNonLiteral, QuoteKind::kNone,
                       /*prefix=*/"");
  }

  static QuotingSpec BacktickedIdentifier() {
    return QuotingSpec(LiteralTokenKind::kBacktickedIdentifier,
                       QuoteKind::kBacktick,
                       /*prefix=*/"");
  }

  static QuotingSpec StringLiteral(absl::string_view prefix,
                                   QuoteKind quote_kind) {
    ABSL_DCHECK(quote_kind != QuoteKind::kNone);
    ABSL_DCHECK(quote_kind != QuoteKind::kBacktick);
    ABSL_DCHECK_LE(prefix.length(), 1);
    return QuotingSpec(LiteralTokenKind::kStringLiteral, quote_kind, prefix);
  }

  static QuotingSpec BytesLiteral(absl::string_view prefix,
                                  QuoteKind quote_kind) {
    ABSL_DCHECK(quote_kind != QuoteKind::kNone);
    ABSL_DCHECK(quote_kind != QuoteKind::kBacktick);
    ABSL_DCHECK_GE(prefix.length(), 1);
    ABSL_DCHECK_LE(prefix.length(), 2);
    return QuotingSpec(LiteralTokenKind::kBytesLiteral, quote_kind, prefix);
  }

  // Finds the quoting kind (e.g. one single quote, raw 3 double quotes, ...
  // etc) of the given token. If successful, 'out_literal_contents' takes on
  // the contents of the literal. out_literal_contents is always a substring of
  // the input 'text' - ultimately, it's a view into the input to the expander.
  // REQUIRES: the input text must represent a single, valid SQL token.
  static absl::StatusOr<QuotingSpec> FindQuotingKind(
      absl::string_view text, absl::string_view& out_literal_contents);

  LiteralTokenKind literal_kind() const { return literal_kind_; }
  QuoteKind quote_kind() const { return quote_kind_; }
  absl::string_view prefix() const { return prefix_; }

  // Returns a user-friendly string describing this quoting spec. Used only in
  // error messages, does not need to be round-trippable.
  std::string Description() const;

 private:
  QuotingSpec(LiteralTokenKind literal_kind, QuoteKind quote_kind,
              absl::string_view prefix)
      : literal_kind_(literal_kind), quote_kind_(quote_kind), prefix_(prefix) {}

  // REQUIRES: the input text must represent a single, valid SQL token.
  static absl::StatusOr<QuotingSpec> FindQuotingKind(absl::string_view text);

  const LiteralTokenKind literal_kind_;
  const QuoteKind quote_kind_;
  const absl::string_view prefix_;
};

// Returns a string representation of the quote (without the prefix, and only
// one side) For example, for kThreeDoubleQuotes, returns R"(""")".
absl::string_view QuoteStr(const QuoteKind& quote_kind);

// Wrap the input text with the given quoting spec, *WITHOUT* escaping.
std::string QuoteText(absl::string_view text, const QuotingSpec& quoting_spec);

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_QUOTING_H_
