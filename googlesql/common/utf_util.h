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

#ifndef GOOGLESQL_COMMON_UTF_UTIL_H_
#define GOOGLESQL_COMMON_UTF_UTIL_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "unicode/utf8.h"

// TODO: Refactor references and add common functions from
// .../public/functions/string.cc. Specific functions are BackwardN, ForwardN,
// and CheckAndCastStrLength.
namespace googlesql {

// Returns the length of `s` that is well formed UTF8. This will return
// `s.length()` if it is completely well formed UTF8.
absl::string_view::size_type SpanWellFormedUTF8(absl::string_view s);

bool IsWellFormedUTF8(absl::string_view s);

// Returns a well-formed Unicode string. Replaces any ill-formed
// subsequences with the Unicode REPLACEMENT CHARACTER (U+FFFD).
// This is usually rendered as a diamond with a question mark in the middle.
std::string CoerceToWellFormedUTF8(absl::string_view input);

// Truncate the given UTF8 string to ensure it is no more than max_bytes.
// If truncated, attempts to create a well formed unicode string, and append an
// (ascii) ellipsis.  If max_bytes is < 3, no ellipsis is appended.
std::string PrettyTruncateUTF8(absl::string_view input, int max_bytes);

// Verifies that the string length can be represented in a 32-bit signed int and
// returns that value. Fitting in an int32 is a requirement for icu methods.
ABSL_MUST_USE_RESULT bool CheckAndCastStrLength(absl::string_view str,
                                                int32_t* str_length32);

// Returns the offset needed to forward `str` by `num_code_points` or an empty
// optional if an invalid UTF-8 codepoint is detected.
// Similar to U8_FWD_N, but will detect bad utf codepoints.
std::optional<int32_t> ForwardN(absl::string_view str, int32_t str_length32,
                                int64_t num_code_points);

// Returns the number of code points in the given UTF-8 string, or a failed
// status if <str> is not a valid utf-8 string.
absl::StatusOr<int32_t> LengthUtf8(absl::string_view str);

// Transforms <str> into a single line string, guaranteed to fit within
// <max_code_points> UTF-8 code points, while preserving as many useful parts of
// the input string as possible.
//
// The following describes the transformations performed:
//   1) Leading and trailing whitespace is skipped.
//   2) All whitespace characters are replaced with " ". The "\r\n" newline
//     combination is replaced with a single " ".
//   3) If the resultant string has no more than <hard_max_chars> UTF-8
//     characters, it is returned as is. If not, the returned string contains
//     the first few characters of the string, followed by "...", followed by
//     the last few characters.
//
//     The number of characters to include before and after the "..." is
//     determined heuristically, with goals of minimizing whitespace and
//     avoiding breaking up words.
//
// A failed status is returned if <str> is not a valid UTF-8 string, or if
// <max_code_points> is not at least 5 (the minimum length to hold "...", plus
// one character before and after).
absl::StatusOr<std::string> GetSummaryString(absl::string_view str,
                                             int max_code_points);
}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_UTF_UTIL_H_
