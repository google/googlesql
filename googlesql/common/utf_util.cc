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

#include "googlesql/common/utf_util.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "googlesql/base/logging.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "unicode/utf8.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

constexpr absl::string_view kReplacementCharacter = "\uFFFD";

static int SpanWellFormedUTF8(const char* s, int length) {
  for (int i = 0; i < length;) {
    int start = i;
    UChar32 c;
    U8_NEXT(s, i, length, c);
    if (c < 0) {
      return start;
    }
  }
  return length;
}

absl::string_view::size_type SpanWellFormedUTF8(absl::string_view s) {
  return static_cast<absl::string_view::size_type>(
      SpanWellFormedUTF8(s.data(), static_cast<int>(s.length())));
}

bool IsWellFormedUTF8(absl::string_view s) {
  return SpanWellFormedUTF8(s) == s.length();
}

std::string CoerceToWellFormedUTF8(absl::string_view input) {
  const char* s = input.data();
  size_t length = input.length();
  size_t prev = 0;
  std::string out;
  for (size_t i = 0; i < length;) {
    size_t start = i;
    UChar32 c;
    U8_NEXT(s, i, length, c);
    if (c < 0) {
      if (prev < start) {
        // Append the well-formed span between the last ill-formed sequence
        // (or start of input), and the point just before the current one.
        out.append(s + prev, start - prev);
      }
      out.append(kReplacementCharacter.data(), kReplacementCharacter.size());
      prev = i;
    }
  }
  if (prev < length) {
    // Append any remaining well formed span.
    out.append(s + prev, length - prev);
  }
  return out;
}

std::string PrettyTruncateUTF8(absl::string_view input, int max_bytes) {
  if (max_bytes <= 0) {
    return "";
  }
  if (input.size() <= max_bytes) {
    // Already small enough, take no action.
    return std::string(input);
  }
  const bool append_ellipsis = max_bytes > 3;
  int new_width = append_ellipsis ? max_bytes - 3 : max_bytes;

  const uint8_t* str_ptr = reinterpret_cast<const uint8_t*>(input.data());
  // Handles the edge case that a unicode character is cut in half. Finds
  // a safe width that has complete unicode characters. No (further) truncation
  // occurs for totally invalid unicode.
  U8_SET_CP_START(str_ptr, 0, new_width);

  if (append_ellipsis)
    return absl::StrCat(input.substr(0, new_width), "...");
  else
    return std::string(input.substr(0, new_width));
}

bool CheckAndCastStrLength(absl::string_view str, int32_t* str_length32) {
  if (str.length() > std::numeric_limits<int32_t>::max()) {
    return false;
  }
  *str_length32 = static_cast<int32_t>(str.length());
  return true;
}

std::optional<int32_t> ForwardN(absl::string_view str, int32_t str_length32,
                                int64_t num_code_points) {
  int32_t str_offset = 0;
  for (int64_t i = 0; i < num_code_points && str_offset < str_length32; ++i) {
    UChar32 character;
    U8_NEXT(str, str_offset, str_length32, character);
    if (character < 0) {
      return std::nullopt;
    }
  }
  return str_offset;
}

absl::StatusOr<int32_t> LengthUtf8(absl::string_view str) {
  GOOGLESQL_RET_CHECK_LE(str.size(), std::numeric_limits<int32_t>::max());
  int32_t str_length32 = static_cast<int32_t>(str.size());

  int utf8_length = 0;
  int32_t offset = 0;
  while (offset < str_length32) {
    UChar32 character;
    U8_NEXT(str.data(), offset, str_length32, character);
    if (character < 0) {
      return absl::InvalidArgumentError("Invalid utf8");
    }
    utf8_length++;
  }
  return utf8_length;
}

namespace {
// Helper function for GetStringWithEllipses().
// Invoked when the caller has already determined that
// 1) <str> is a valid UTF-8 string and
// 2) <str> is long enough that truncation is required.
//
// Computes the string to appear before the "...", along with its character
// length.
absl::Status ComputePrefixBeforeEllipses(absl::string_view str,
                                         int min_prefix_code_points,
                                         int max_total_code_points,
                                         std::string& prefix,
                                         int& prefix_char_len) {
  // Set the maximum prefix length to allow enough room for "..." and a suffix
  // of equal length to come after it, without exceeding <max_chars>.
  int max_prefix_chars = max_total_code_points - 3 - min_prefix_code_points;
  int32_t offset = 0;
  int32_t str_len = static_cast<int32_t>(str.size());
  prefix.clear();
  prefix_char_len = 0;
  bool in_word = false;
  int num_trailing_spaces = 0;
  while (offset < str_len) {
    int32_t prev_offset = offset;
    bool prev_in_word = in_word;

    // Fetch the next Unicode character. We already checked that <str> is
    // valid UTF-8, so this call should never fail.
    UChar32 character;
    U8_NEXT(str.data(), offset, str_len, character);
    GOOGLESQL_RET_CHECK_GE(character, 0);

    in_word = absl::ascii_isalnum(character) || character == '_';
    if (prefix_char_len >= min_prefix_code_points &&
        (!prev_in_word || !in_word)) {
      break;
    }

    // Character is part of prefix
    absl::StrAppend(&prefix, str.substr(prev_offset, offset - prev_offset));
    ++prefix_char_len;

    if (absl::ascii_isspace(character)) {
      ++num_trailing_spaces;
    } else {
      num_trailing_spaces = 0;
    }

    if (prefix_char_len >= max_prefix_chars) {
      break;
    }
  }
  absl::StripTrailingAsciiWhitespace(&prefix);
  prefix_char_len -= num_trailing_spaces;
  return absl::OkStatus();
}

// Helper function for GetStringWithEllipses().
// Invoked when the caller has already determined that
// 1) <str> is a valid UTF-8 string and
// 2) <str> is long enough that truncation is required.
//
// Computes the string to appear after the "...".
absl::StatusOr<std::string> ComputeSuffixAfterEllipses(
    absl::string_view str, int min_suffix_code_points,
    int max_total_code_points) {
  // Build the suffix in reverse order, then reverse it at the end.
  std::string suffix;
  int suffix_char_len = 0;
  int offset = static_cast<int>(str.size());
  bool in_word = false;
  int max_suffix_chars = max_total_code_points - 3;
  while (offset >= 0) {
    int32_t prev_offset = offset;
    bool prev_in_word = in_word;

    UChar32 character;
    U8_PREV(str.data(), 0, offset, character);

    in_word = absl::ascii_isalnum(character) || character == '_';
    if (suffix_char_len >= min_suffix_code_points &&
        (!prev_in_word || !in_word)) {
      break;
    }

    int char_bytelen = prev_offset - offset;
    absl::StrAppend(&suffix, str.substr(offset, char_bytelen));
    ++suffix_char_len;

    // Reverse the order of the bytes of the just-appended character in 'suffix'
    // so that the bytes will be in the correct order after the final call to
    // std::reverse(), below.
    std::reverse(suffix.end() - char_bytelen, suffix.end());

    if (suffix_char_len >= max_suffix_chars) {
      break;
    }
  }
  std::reverse(suffix.begin(), suffix.end());
  absl::StripLeadingAsciiWhitespace(&suffix);
  return suffix;
}
}  // namespace

absl::StatusOr<std::string> GetSummaryString(absl::string_view str,
                                             int max_code_points) {
  GOOGLESQL_RET_CHECK_LE(str.size(), std::numeric_limits<int32_t>::max());
  GOOGLESQL_RET_CHECK_GE(max_code_points, 5);  // minimum length to hold "a...b"

  // Simplify whitespace:
  //  - Strip leading/trailing whitespace completely
  //  - Replace all other whitespace characters with " ", replacing the "\r\n"
  //    newline combination with a single space.
  std::string str_normalized =
      absl::StrReplaceAll(absl::StripAsciiWhitespace(str), {{"\r\n", " "}});
  std::replace_if(str_normalized.begin(), str_normalized.end(),
                  absl::ascii_isspace, ' ');

  // Check that <str> is valid UTF-8 and compute total UTF-8 length of input
  // string to see if we need to truncate it.
  GOOGLESQL_ASSIGN_OR_RETURN(int32_t total_code_points, LengthUtf8(str_normalized));
  std::string result;
  if (total_code_points <= max_code_points) {
    return std::string(str_normalized);  // No truncation necessary
  }

  // Determine the minimum number of characters to include before and after
  // the "...". It must be small enough to allow two strings of that length,
  // plus "..." to not exceed <max_chars>. But, hueristically, it is good to
  // have it even smaller (assuming max_chars is not very small) so that we
  // have more flexibility to choose a prefix and length that doesn't break in
  // the middle of a word.
  int min_prefix_suffix_chars =
      std::min((max_code_points - 3) / 2, max_code_points / 3);

  std::string prefix;
  int prefix_char_len;
  GOOGLESQL_RETURN_IF_ERROR(
      ComputePrefixBeforeEllipses(str_normalized, min_prefix_suffix_chars,
                                  max_code_points, prefix, prefix_char_len));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string suffix,
      ComputeSuffixAfterEllipses(str_normalized, min_prefix_suffix_chars,
                                 max_code_points - prefix_char_len));
  return absl::StrCat(prefix, "...", suffix);
}
}  // namespace googlesql
