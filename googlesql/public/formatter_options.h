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

#ifndef GOOGLESQL_PUBLIC_FORMATTER_OPTIONS_H_
#define GOOGLESQL_PUBLIC_FORMATTER_OPTIONS_H_

#include <stdbool.h>

#include <string>

#include "googlesql/public/formatter_options.pb.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Formatting options for lenient GoogleSQL formatter.
class FormatterOptions {
 public:
  // Tells formatter to detect new line separator from the input sql.
  // If the input is a single line, default new line separator is "\n".
  static constexpr char kDetectNewLineType[] = "";
  // New line separator used in unix-like systems.
  static constexpr char kUnixNewLineType[] = "\n";

  // Creates default options for the formatter.
  FormatterOptions()
      : new_line_type_(kDetectNewLineType),
        line_length_limit_(80),
        indentation_spaces_(2),
        allow_invalid_tokens_(true),
        capitalize_functions_(false),
        capitalize_keywords_(true),
        preserve_line_breaks_(false),
        expand_format_ranges_(false),
        enforce_single_quotes_(false),
        format_structured_strings_(true),
        format_comments_(true) {}

  // Creates options overwriting defaults using the given proto. Not set fields
  // in the proto are ignored.
  explicit FormatterOptions(const FormatterOptionsProto& proto);

  // The new line separator characters to use in the formatted output.
  // Usually one of "\n", "\r", "\n\r", or "\r\n".
  // Use `kDetectNewLineType` to use the same type as the input.
  void SetNewLineType(absl::string_view new_line_type) {
    new_line_type_ = new_line_type;
  }
  const std::string& NewLineType() const { return new_line_type_; }
  bool IsLineTypeDetectionEnabled() const { return new_line_type_.empty(); }

  // The line length limit, in characters.
  void SetLineLengthLimit(int line_length_limit) {
    line_length_limit_ = line_length_limit;
  }
  int LineLengthLimit() const { return line_length_limit_; }

  // Number of spaces per level of indentation.
  void SetIndentationSpaces(int indentation_spaces) {
    indentation_spaces_ = indentation_spaces;
  }
  int IndentationSpaces() const { return indentation_spaces_; }

  // If true, formatter tries to proceed even if it encounters invalid GoogleSQL
  // tokens. For instance, 2020q1 - is invalid identifier and is not a numeric
  // literal.
  void AllowInvalidTokens(bool allow_invalid_tokens) {
    allow_invalid_tokens_ = allow_invalid_tokens;
  }
  bool IsAllowedInvalidTokens() const { return allow_invalid_tokens_; }

  // If true, formatter capitalizes reserved GoogleSQL functions.
  void CapitalizeFunctions(bool capitalize_functions) {
    capitalize_functions_ = capitalize_functions;
  }
  bool IsCapitalizeFunctions() const { return capitalize_functions_; }

  // If true, formatter capitalizes reserved GoogleSQL keywords.
  void CapitalizeKeywords(bool capitalize_keywords) {
    capitalize_keywords_ = capitalize_keywords;
  }
  bool IsCapitalizeKeywords() const { return capitalize_keywords_; }

  // If true, formatter tries to preserve existing correct line breaks in the
  // input even if without the breaking the expression fits on a single line.
  void TryPreservingExistingLineBreaks(bool preserve_line_breaks) {
    preserve_line_breaks_ = preserve_line_breaks;
  }
  bool IsPreservingExistingLineBreaks() const { return preserve_line_breaks_; }

  // If true and formatting only certain input ranges, the ranges are expanded
  // on both ends if needed to include entire statements.
  // Has no effect if formatting the entire input.
  void SetExpandRangesToFullStatements(bool expand_format_ranges) {
    expand_format_ranges_ = expand_format_ranges;
  }
  bool GetExpandRangesToFullStatements() const { return expand_format_ranges_; }

  // If true, formatter replaces doubles quotes to single quotes for enclosing
  // string literals.
  void EnforceSingleQuotes(bool enforce_single_quotes) {
    enforce_single_quotes_ = enforce_single_quotes;
  }
  bool IsEnforcingSingleQuotes() const { return enforce_single_quotes_; }

  // If true, formatter attempts to format the contents of annotated string
  // literals with structured content. Supported annotations:
  // /*sql*/ - for embedded SQL strings,
  // /*proto*/, /*txtpb*/ - for embedded textproto.
  void FormatStructuredStrings(bool format_structured_strings) {
    format_structured_strings_ = format_structured_strings;
  }
  bool IsFormattingStructuredStrings() const {
    return format_structured_strings_;
  }

  // If true, formatter fixes comments: adds space after comment symbol and
  // trims trailing whitespaces.
  void FormatComments(bool format_comments) {
    format_comments_ = format_comments;
  }
  bool IsFormattingComments() const { return format_comments_; }

 private:
  std::string new_line_type_;
  int line_length_limit_;
  int indentation_spaces_;
  bool allow_invalid_tokens_;
  bool capitalize_functions_;
  bool capitalize_keywords_;
  bool preserve_line_breaks_;
  bool expand_format_ranges_;
  bool enforce_single_quotes_;
  bool format_structured_strings_;
  bool format_comments_;
};

// Represents a range in the input to be formatted. The range may be in byte
// offsets or line indices depending on the function it is used in.
struct FormatterRange {
  int start;
  int end;
};

constexpr bool operator<(FormatterRange lhs, FormatterRange rhs) {
  return lhs.start < rhs.start;
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FORMATTER_OPTIONS_H_
