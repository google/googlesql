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

#ifndef GOOGLESQL_PUBLIC_STRINGS_H_
#define GOOGLESQL_PUBLIC_STRINGS_H_

#include <string>
#include <vector>

#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status.h"

namespace googlesql {

// These escaping and unescaping interfaces were originally copied from
// strings/escaping.h, matching Utf8SafeCHexEscape and CUnescape, but
// customized for SQL-style quote characters to allow 'abc"def', "abc'def"
// and `abc"'def`, provide different hex and octal escaping semantics, and
// also give errors for strings like 'abc'def'.

// Expand escaped characters according to GoogleSQL escaping rules.
// This is for raw strings with no quoting.  If an error occurs and
// <error_string> is not NULL, then it is populated with the relevant error
// message. If <error_offset> is not NULL, it is populated with the offset in
// <str> at which the invalid input occurred.
absl::Status UnescapeString(absl::string_view str, std::string* out,
                            std::string* error_string = nullptr,
                            int* error_offset = nullptr);

// Expand escaped characters according to GoogleSQL escaping rules.
// Rules for bytes values are slightly different than those for strings.  This
// is for raw literals with no quoting.  If an error occurs and <error_string>
// is not NULL then it is populated with the relevant error message.  If
// <error_offset> is not NULL, it is populated with the offset in  <str> at
// which the invalid input occurred.
absl::Status UnescapeBytes(absl::string_view str, std::string* out,
                           std::string* error_string = nullptr,
                           int* error_offset = nullptr);

// Escape a string without quoting it. All quote characters are escaped.
std::string EscapeString(absl::string_view str);

// Escape a bytes value without quoting it.  Escaped bytes use hex escapes.
// If <escape_all_bytes> is true then all bytes are escaped.  Otherwise only
// unprintable bytes and escape/quote characters are escaped.
// If <escape_quote_char> is not 0, then quotes that do not match are not
// escaped.
std::string EscapeBytes(absl::string_view str, bool escape_all_bytes = false,
                        char escape_quote_char = 0);

// Unquote and unescape a quoted GoogleSQL string literal (of the form '...',
// "...", r'...' or r"...").
// If an error occurs and <error_string> is not NULL, then it is populated with
// the relevant error message. If <error_offset> is not NULL, it is populated
// with the offset in <str> at which the invalid input occurred.
absl::Status ParseStringLiteral(absl::string_view str, std::string* out,
                                std::string* error_string = nullptr,
                                int* error_offset = nullptr);

// Unquote and unescape a GoogleSQL bytes literal (of the form b'...',
// b"...", rb'...', rb"...", br'...' or br"...").
// If an error occurs and <error_string> is not NULL, then it is populated with
// the relevant error message. If <error_offset> is not NULL, it is populated
// with the offset in <str> at which the invalid input occurred.
absl::Status ParseBytesLiteral(absl::string_view str, std::string* out,
                               std::string* error_string = nullptr,
                               int* error_offset = nullptr);

// Return a quoted and escaped GoogleSQL string literal for <str>.
// May choose to quote with ' or " to produce nicer output.
std::string ToStringLiteral(absl::string_view str);

// Return a quoted and escaped GoogleSQL string literal for <str>.
// Always uses single quotes.
std::string ToSingleQuotedStringLiteral(absl::string_view str);

// Return a quoted and escaped GoogleSQL string literal for <str>.
// Always uses double quotes.
std::string ToDoubleQuotedStringLiteral(absl::string_view str);

// Return a quoted and escaped GoogleSQL bytes literal for <str>.
// Prefixes with b and may choose to quote with ' or " to produce nicer output.
std::string ToBytesLiteral(absl::string_view str);

// Return a quoted and escaped GoogleSQL bytes literal for <str>.
// Prefixes with b and always uses single quotes.
std::string ToSingleQuotedBytesLiteral(absl::string_view str);

// Return a quoted and escaped GoogleSQL bytes literal for <str>.
// Prefixes with b and always uses double quotes.
std::string ToDoubleQuotedBytesLiteral(absl::string_view str);

// Parse a GoogleSQL identifier.
// The string may be quoted with backticks.  If so, unquote and unescape it.
// Otherwise, the string must be a valid GoogleSQL identifier.
// If an error occurs and <error_string> is not NULL, then it is populated with
// the relevant error message. If <error_offset> is not NULL, it is populated
// with the offset in <str> at which the invalid input occurred.
//
// Reserved keywords are rejected.
absl::Status ParseIdentifier(absl::string_view str,
                             const LanguageOptions& language_options,
                             std::string* out,
                             std::string* error_string = nullptr,
                             int* error_offset = nullptr);

// Same as above, but allow reserved keywords to be identifiers. For example,
// "SELECT" is valid as a generalized identifier but not as an identifier.
// "`SELECT`" is valid for either.
absl::Status ParseGeneralizedIdentifier(absl::string_view str, std::string* out,
                                        std::string* error_string = nullptr,
                                        int* error_offset = nullptr);

// Convert a string to a GoogleSQL identifier literal.
// The output will be quoted (with backticks) and escaped if necessary.
// If <quote_reserved_keywords> is true then all reserved keywords (including
// conditionally reserved keywords) are quoted and escaped.
std::string ToIdentifierLiteral(absl::string_view str,
                                bool quote_reserved_keywords = true);
std::string ToIdentifierLiteral(IdString str,
                                bool quote_reserved_keywords = true);
// The output will always be quoted with backticks.
std::string ToAlwaysQuotedIdentifierLiteral(absl::string_view str);

// Convert an identifier path to a string, quoting identifiers if necessary.
// If <quote_reserved_keywords> is true then all reserved (including
// conditionally reserved) keywords are quoted and escaped. Otherwise only quote
// and escape the keyword when it is first in the path.
std::string IdentifierPathToString(absl::Span<const std::string> path,
                                   bool quote_reserved_keywords = true);
std::string IdentifierPathToString(absl::Span<const IdString> path,
                                   bool quote_reserved_keywords = true);

// Convert an identifier string to a vector of path components, unquoting
// identifiers if necessary. This inverts IdentifierPathToString and also
// handles paths that contain valid unquoted identifiers (e.g. abc.123 in
// addition to abc.`123`, and abc.select in addition to abc.`select`).
// (More formally, this function supports paths that rely on the DOT_IDENTIFIER
// mode in the lexer to handle generalized identifiers after the first
// component.)
//
// Reserved keywords (as specified by the LanguageOptions) are rejected at the
// start of an identifier path.
//
//  Examples:
//   "abc.123" => {"abc", "123"}
//   "abc.`123`" => {"abc", "123"}
//   "abc.select.from.table" => {"abc", "select", "from", "table"}
//   "abc.`def.ghi`" => {"abc", "def.ghi"}
//   "123" => error
//
// If FEATURE_ALLOW_SLASH_PATHS is enabled in <language_options>, then
// this function also supports paths that start with '/' and that may also
// contain '-' or ':' in the first path component.
//
//  Examples:
//   "/abc/def.123" => {"/abc/def", "123"}
//   "/abc:def.123" => {"/abc:def", "123"}
//   "/abc/def-ghi:123.jkl" => {"/abc/def-ghi:123", "jkl"}
absl::Status ParseIdentifierPath(absl::string_view str,
                                 const LanguageOptions& language_options,
                                 std::vector<std::string>* out);

// Return true if <str> is guaranteed to be a valid identifier without quoting.
// If <allow_reserved_keywords> is false, returns false if <str> is a reserved
// or conditionally reserved keyword per <language_options>.
//
// <str> is case-insensitive.
bool IsValidUnquotedIdentifier(absl::string_view str,
                               const LanguageOptions& language_options,
                               bool allow_reserved_keywords);

// Return true if the string is a GoogleSQL keyword.
// Note: The set of strings for which this function returns true may increase
// over time as new keywords get added to the GoogleSQL language.
//
// Input strings are case-insensitive.
bool IsKeyword(absl::string_view str);

// This method exists for backward compatibility with existing callers from
// before conditionally reserved keywords existed.
//
// It simply calls IsAlwaysReservedKeyword(), which matches the behavior of
// returning true only for keywords that were reserved at that time.
//
// New callers should use LanguageOptions::IsReservedKeyword() instead.
ABSL_DEPRECATED("Use LanguageOptions::IsReservedKeyword() instead")
bool IsReservedKeyword(absl::string_view str);

// Get a minimal set of reserved keywords in GoogleSQL. Keywords in this list
// are reserved under all LanguageOptions. These strings need to be
// backtick-quoted to be used as identifiers.
//
// Additional keywords, not in this list, may require backquoting also,
// depending on which LanguageOptions are used to parse queries.
//
ABSL_DEPRECATED(
    "To determine is a keyword is reserved, use "
    "LanguageOptions::IsReservedKeyword(), rather than check if the keyword "
    "belongs to this set")
const absl::flat_hash_set<std::string>& GetReservedKeywords();

// Return true for an alias generated internally for an anonymous column.
// These names are not meant to be user-visible.
// Currently, internal names start with '$'.
// TODO Consider removing this.
bool IsInternalAlias(absl::string_view alias);
bool IsInternalAlias(IdString alias);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_STRINGS_H_
