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

#include "googlesql/public/functions/string.h"

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/public/functions/normalize_mode.pb.h"
#include "googlesql/public/functions/util.h"
#include "googlesql/public/strings.h"
#include "googlesql/base/string_numbers.h"
#include "absl/algorithm/container.h"
#include "absl/base/casts.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "unicode/bytestream.h"
#include "unicode/casemap.h"
#include "unicode/errorcode.h"
#include "unicode/normalizer2.h"
#include "unicode/uchar.h"
#include "unicode/uniset.h"
#include "unicode/unistr.h"
#include "unicode/uset.h"
#include "unicode/utf8.h"
#include "unicode/utypes.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"

namespace googlesql {
namespace functions {

namespace {

constexpr absl::string_view kBadUtf8 = "A string is not valid UTF-8.";

constexpr absl::string_view kUtf8ReplacementChar = "\uFFFD";
constexpr UChar32 kUChar32ReplacementChar = 0xfffd;

constexpr size_t kMaxOutputSize = (1 << 20);  // 1MB
// Based on https://tools.ietf.org/html/rfc2045#section-6.8
constexpr size_t kMaxLineLengthBase64M = 76;

constexpr absl::string_view kExceededPadOutputSize =
    "Output of LPAD/RPAD exceeds max allowed output size of 1MB";
constexpr absl::string_view kExceededRepeatOutputSize =
    "Output of REPEAT exceeds max allowed output size of 1MB";
constexpr absl::string_view kExceededReplaceOutputSize =
    "Output of REPLACE exceeds max allowed output size of 1MB";
constexpr absl::string_view kExceededTranslateOutputSize =
    "Output of TRANSLATE exceeds max allowed output size of 1MB";

constexpr absl::string_view kBadOccurrenceStringPos =
    "Occurrence must be positive";
constexpr absl::string_view kBadPosStringPos = "Position must be non-zero";

// Only used by INITCAP() functions. In addition, whitespace characters are
// handled by the calling function.
constexpr absl::string_view kDefaultDelimiters =
    "[](){}/|\\<>!?@\"#$&~_,.:;*%+-^";

constexpr absl::string_view kInvalidFormat = "Invalid format '$0'";

// Verifies that the string length can be represented in a 32-bit signed int and
// returns that value. Fitting in an int32 is a requirement for icu methods.
static bool CheckAndCastStrLength(absl::string_view str, int32_t* str_length32,
                                  absl::Status* error) {
  if (str.length() > std::numeric_limits<int32_t>::max()) {
    return internal::UpdateError(
        error,
        absl::Substitute("input string size too large $0", str.length()));
  }
  *str_length32 = static_cast<int32_t>(str.length());
  return true;
}

static int32_t ClampToInt32Max(int64_t i) {
  return i > std::numeric_limits<int32_t>::max()
             ? std::numeric_limits<int32_t>::max()
             : static_cast<int32_t>(i);
}

static bool GlobalStringReplace(absl::string_view s, absl::string_view oldsub,
                                absl::string_view newsub, std::string* res,
                                absl::Status* error) {
  if (oldsub.empty()) {
    if (s.length() > kMaxOutputSize) {
      return internal::UpdateError(error, kExceededReplaceOutputSize);
    }
    res->append(s);  // If empty, append the given string.
    return true;
  }

  absl::string_view::size_type start_pos = 0;
  while (true) {
    absl::string_view::size_type pos = s.find(oldsub, start_pos);
    if (pos == absl::string_view::npos) {
      break;
    }
    const size_t total_append_size = (pos - start_pos) + newsub.length();
    if (res->size() + total_append_size > kMaxOutputSize) {
      return internal::UpdateError(error, kExceededReplaceOutputSize);
    }
    absl::StrAppend(res, s.substr(start_pos, pos - start_pos), newsub);
    // Start searching again after the "old".
    start_pos = pos + oldsub.length();
  }
  const size_t append_size = s.length() - start_pos;
  if (res->size() + append_size > kMaxOutputSize) {
    return internal::UpdateError(error, kExceededReplaceOutputSize);
  }
  res->append(s, start_pos, append_size);
  return true;
}

// Returns an icu::Normalizer2 instance according to the given <normalize_mode>.
// If the <normalize_mode> is not valid or there is any error in getting the
// normalizer instance, returns a nullptr and sets the <error>.
const icu::Normalizer2* GetNormalizerByMode(NormalizeMode normalize_mode,
                                            absl::Status* error) {
  const icu::Normalizer2* normalizer = nullptr;
  icu::ErrorCode icu_errorcode;
  switch (normalize_mode) {
    case NormalizeMode::NFC:
      normalizer = icu::Normalizer2::getNFCInstance(icu_errorcode);
      break;
    case NormalizeMode::NFD:
      normalizer = icu::Normalizer2::getNFDInstance(icu_errorcode);
      break;
    case NormalizeMode::NFKC:
      normalizer = icu::Normalizer2::getNFKCInstance(icu_errorcode);
      break;
    case NormalizeMode::NFKD:
      normalizer = icu::Normalizer2::getNFKDInstance(icu_errorcode);
      break;
    default:
      error->Update(absl::Status(absl::StatusCode::kInvalidArgument,
                                 "A valid normalize mode is required."));
      return nullptr;
  }
  if (icu_errorcode.isFailure()) {
    error->Update(absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("Failed to get a normalizer instance with error: ",
                     icu_errorcode.errorName())));
    icu_errorcode.reset();
    return nullptr;
  }
  return normalizer;
}

}  // anonymous namespace

bool Utf8Trimmer::Initialize(absl::string_view to_trim, absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(to_trim, &str_length32, error)) {
    return false;
  }
  unicode_set_ = std::make_unique<icu::UnicodeSet>();
  has_explicit_replacement_char_ = false;
  int32_t offset = 0;
  while (offset < str_length32) {
    UChar32 character;
    U8_NEXT(to_trim.data(), offset, str_length32, character);
    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    } else {
      unicode_set_->add(character);
    }
    if (character == kUChar32ReplacementChar) {
      has_explicit_replacement_char_ = true;
    }
  }
  unicode_set_->freeze();
  return true;
}

// Implementation of trim left which takes a lambda to compute whether the
// Unicode character should be trimmed.
// Note: UChar32 means "Unicode chararacter 32-bit", and is signed.
static bool TrimLeftImpl(absl::string_view str,
                         const icu::UnicodeSet& unicode_set,
                         absl::string_view* out, absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  int32_t prefix_length =
      unicode_set.spanUTF8(str.data(), str_length32, USET_SPAN_CONTAINED);
  *out = str.substr(static_cast<size_t>(prefix_length),
                    static_cast<size_t>(str_length32 - prefix_length));
  return true;
}

static bool TrimRightImpl(absl::string_view str,
                          const icu::UnicodeSet& unicode_set,
                          absl::string_view* out, absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  int32_t suffix_start =
      unicode_set.spanBackUTF8(str.data(), str_length32, USET_SPAN_CONTAINED);
  *out = str.substr(0, static_cast<size_t>(suffix_start));
  return true;
}

bool Utf8Trimmer::TrimLeft(absl::string_view str, absl::string_view* out,
                           absl::Status* error) const {
  if (unicode_set_ == nullptr) {
    // Not initialized, no characters are trimmed. Return input.
    *out = str;
    return true;
  }
  if (has_explicit_replacement_char_ && !IsWellFormedUTF8(str)) {
    return internal::UpdateError(error, kBadUtf8);
  }

  return TrimLeftImpl(str, *unicode_set_, out, error);
}

bool Utf8Trimmer::TrimRight(absl::string_view str, absl::string_view* out,
                            absl::Status* error) const {
  if (unicode_set_ == nullptr) {
    // Not initialized, no characters are trimmed. Return input.
    *out = str;
    return true;
  }
  if (has_explicit_replacement_char_ && !IsWellFormedUTF8(str)) {
    return internal::UpdateError(error, kBadUtf8);
  }

  return TrimRightImpl(str, *unicode_set_, out, error);
}

bool Utf8Trimmer::Trim(absl::string_view str, absl::string_view* out,
                       absl::Status* error) const {
  absl::string_view intermediate;
  return TrimLeft(str, &intermediate, error) &&
         TrimRight(intermediate, out, error);
}

void BytesTrimmer::Initialize(absl::string_view to_trim) {
  memset(bytes_to_trim_, 0, sizeof(bytes_to_trim_));
  for (const char ch : to_trim) {
    uint8_t byte = static_cast<uint8_t>(ch);
    bytes_to_trim_[byte] = true;
  }
}

absl::string_view BytesTrimmer::TrimLeft(absl::string_view str) {
  for (int i = 0; i < str.size(); ++i) {
    uint8_t byte = static_cast<uint8_t>(str[i]);
    if (!bytes_to_trim_[byte]) {
      return str.substr(i);
    }
  }
  // Everything got trimmed. Return an empty string.
  return "";
}

absl::string_view BytesTrimmer::TrimRight(absl::string_view str) {
  for (absl::string_view::reverse_iterator it = str.rbegin(); it != str.rend();
       ++it) {
    uint8_t byte = static_cast<uint8_t>(*it);
    if (!bytes_to_trim_[byte]) {
      return absl::string_view(str.data(), it.base() - str.begin());
    }
  }
  // Everything got trimmed. Return an empty string.
  return "";
}

absl::string_view BytesTrimmer::Trim(absl::string_view str) {
  return TrimLeft(TrimRight(str));
}

bool Utf8Capitalizer::Initialize(absl::string_view delimiters,
                                 absl::Status* error) {
  int32_t delimiter_length32;
  if (!CheckAndCastStrLength(delimiters, &delimiter_length32, error)) {
    return false;
  }
  if (unicode_set_ != nullptr) {
    // This implies we are using default delimiters.
    icu::ErrorCode cannot_fail;
    const icu::UnicodeSet* whitespace_unicode_set = icu::UnicodeSet::fromUSet(
      u_getBinaryPropertySet(UCHAR_WHITE_SPACE, cannot_fail));
    unicode_set_->addAll(*whitespace_unicode_set);
  } else {
    unicode_set_ = std::make_unique<icu::UnicodeSet>();
  }
  int32_t offset = 0;
  while (offset < delimiter_length32) {
    UChar32 character;
    U8_NEXT(delimiters.data(), offset, delimiter_length32, character);
    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    } else {
      unicode_set_->add(character);
    }
  }
  unicode_set_->freeze();
  return true;
}

bool Utf8Capitalizer::InitializeDefault(absl::Status* error) {
  unicode_set_ = std::make_unique<icu::UnicodeSet>();
  return Initialize(kDefaultDelimiters, error);
}

bool Utf8Capitalizer::Capitalize(absl::string_view str, std::string* out,
                                 absl::Status* error) {
  if (unicode_set_ == nullptr) {
    return internal::UpdateError(
        error, "Initialize much be called before calling Capitalize.");
  }
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  out->clear();
  out->reserve(str_length32);
  bool capitalize_char = true;
  int32_t offset = 0;
  while (offset < str_length32) {
    int32_t prev_offset = offset;
    UChar32 original;
    U8_NEXT(str.data(), offset, str_length32, original);
    if (original < 0) {
      return internal::UpdateError(error, kBadUtf8);
    }
    bool is_delimiter = unicode_set_->contains(original);
    UChar32 corrected = 0;
    if (is_delimiter) {
      corrected = original;
      capitalize_char = true;
    } else if (capitalize_char) {
      corrected = u_toupper(original);
      capitalize_char = false;
    } else {
      corrected = u_tolower(original);
    }

    if (corrected != original) {
      int utf8_index = 0;
      uint8_t utf8_buffer[4];  // U8_APPEND writes 0 to 4 bytes.
      bool has_error = false;
      U8_APPEND(utf8_buffer, utf8_index, sizeof(utf8_buffer), corrected,
                has_error);
      if (has_error) {
        out->clear();
        return internal::UpdateError(
            error, absl::Substitute("Invalid codepoint $0", corrected));
      }
      out->append(reinterpret_cast<char*>(utf8_buffer), utf8_index);
    } else {
      out->append(str.substr(prev_offset, offset - prev_offset));
    }
  }
  return true;
}

bool LengthUtf8(absl::string_view str, int64_t* out, absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  int utf8_length = 0;
  int32_t offset = 0;
  while (offset < str_length32) {
    UChar32 character;
    U8_NEXT(str.data(), offset, str_length32, character);
    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    }
    utf8_length++;
  }
  *out = utf8_length;
  return true;
}

bool LengthBytes(absl::string_view str, int64_t* out, absl::Status* error) {
  *out = str.length();
  return true;
}

bool StartsWithUtf8(absl::string_view str, absl::string_view substr, bool* out,
                    absl::Status* error) {
  *out = absl::StartsWith(str, substr);
  return true;
}

bool StartsWithBytes(absl::string_view str, absl::string_view substr, bool* out,
                     absl::Status* error) {
  *out = absl::StartsWith(str, substr);
  return true;
}

bool EndsWithUtf8(absl::string_view str, absl::string_view substr, bool* out,
                  absl::Status* error) {
  *out = absl::EndsWith(str, substr);
  return true;
}

bool EndsWithBytes(absl::string_view str, absl::string_view substr, bool* out,
                   absl::Status* error) {
  *out = absl::EndsWith(str, substr);
  return true;
}

bool TrimSpacesUtf8(absl::string_view str, absl::string_view* out,
                    absl::Status* error) {
  absl::string_view intermediate;
  return LeftTrimSpacesUtf8(str, &intermediate, error) &&
         RightTrimSpacesUtf8(intermediate, out, error);
}

bool LeftTrimSpacesUtf8(absl::string_view str, absl::string_view* out,
                        absl::Status* error) {
  icu::ErrorCode cannot_fail;
  const icu::UnicodeSet* whitespace_unicode_set = icu::UnicodeSet::fromUSet(
      u_getBinaryPropertySet(UCHAR_WHITE_SPACE, cannot_fail));
  return TrimLeftImpl(str, *whitespace_unicode_set, out, error);
}

bool RightTrimSpacesUtf8(absl::string_view str, absl::string_view* out,
                         absl::Status* error) {
  icu::ErrorCode cannot_fail;
  const icu::UnicodeSet* whitespace_unicode_set = icu::UnicodeSet::fromUSet(
      u_getBinaryPropertySet(UCHAR_WHITE_SPACE, cannot_fail));
  return TrimRightImpl(str, *whitespace_unicode_set, out, error);
}

bool TrimUtf8(absl::string_view str, absl::string_view chars,
              absl::string_view* out, absl::Status* error) {
  Utf8Trimmer trimmer;
  if (!trimmer.Initialize(chars, error)) return false;
  return trimmer.Trim(str, out, error);
}

bool LeftTrimUtf8(absl::string_view str, absl::string_view chars,
                  absl::string_view* out, absl::Status* error) {
  Utf8Trimmer trimmer;
  if (!trimmer.Initialize(chars, error)) return false;
  return trimmer.TrimLeft(str, out, error);
}

bool RightTrimUtf8(absl::string_view str, absl::string_view chars,
                   absl::string_view* out, absl::Status* error) {
  Utf8Trimmer trimmer;
  if (!trimmer.Initialize(chars, error)) return false;
  return trimmer.TrimRight(str, out, error);
}

bool TrimBytes(absl::string_view str, absl::string_view chars,
               absl::string_view* out, absl::Status* error) {
  BytesTrimmer trimmer;
  trimmer.Initialize(chars);
  *out = trimmer.TrimLeft(trimmer.TrimRight(str));
  return true;
}

bool LeftTrimBytes(absl::string_view str, absl::string_view chars,
                   absl::string_view* out, absl::Status* error) {
  BytesTrimmer trimmer;
  trimmer.Initialize(chars);
  *out = trimmer.TrimLeft(str);
  return true;
}

bool RightTrimBytes(absl::string_view str, absl::string_view chars,
                    absl::string_view* out, absl::Status* error) {
  BytesTrimmer trimmer;
  trimmer.Initialize(chars);
  *out = trimmer.TrimRight(str);
  return true;
}

bool LeftUtf8(absl::string_view str, int64_t length, absl::string_view* out,
              absl::Status* error) {
  if (length < 0) {
    return internal::UpdateError(
        error, "Second argument in LEFT() cannot be negative");
  }
  return SubstrWithLengthUtf8(str, 0, length, out, error);
}

bool LeftBytes(absl::string_view str, int64_t length, absl::string_view* out,
               absl::Status* error) {
  if (length < 0) {
    return internal::UpdateError(
        error, "Second argument in LEFT() cannot be negative");
  }
  return SubstrWithLengthBytes(str, 0, length, out, error);
}

bool RightUtf8(absl::string_view str, int64_t length, absl::string_view* out,
               absl::Status* error) {
  if (length < 0) {
    return internal::UpdateError(
        error, "Second argument in RIGHT() cannot be negative");
  }
  return SubstrWithLengthUtf8(str, -length, length, out, error);
}

bool RightBytes(absl::string_view str, int64_t length, absl::string_view* out,
                absl::Status* error) {
  if (length < 0) {
    return internal::UpdateError(
        error, "Second argument in RIGHT() cannot be negative");
  }
  return SubstrWithLengthBytes(str, -length, length, out, error);
}

bool SubstrUtf8(absl::string_view str, int64_t pos, absl::string_view* out,
                absl::Status* error) {
  return SubstrWithLengthUtf8(str, pos, std::numeric_limits<int64_t>::max(),
                              out, error);
}

// Move forward <num_code_points> in str starting at <str_offset> and updates
// <str_offset>. Similar to U8_FWD_N, but will detect bad utf codepoints.
// <hit_end> indicates whether we reached the end of <str> with less than
// <num_code_points> character moves.
static bool ForwardN(absl::string_view str, int32_t str_length32,
                     int64_t num_code_points, int32_t* str_offset,
                     bool* hit_end, absl::Status* error) {
  int64_t i = 0;
  for (; i < num_code_points && *str_offset < str_length32; ++i) {
    UChar32 character;
    U8_NEXT(str.data(), *str_offset, str_length32, character);
    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    }
  }
  *hit_end = (i < num_code_points);
  return true;
}

// Move backward <num_code_points> in str starting at <str_offset> and updates
// <str_offset>. Similar to U8_BACK_N, but will detect bad utf codepoints.
// <hit_start> indicates whether we reached the start of <str> with less than
// <num_code_points> character moves.
static bool BackN(absl::string_view str, int64_t num_code_points,
                  int32_t* str_offset, bool* hit_start, absl::Status* error) {
  int64_t i = 0;
  for (; i<num_code_points&& * str_offset> 0; ++i) {
    UChar32 character;
    U8_PREV(str.data(), 0, *str_offset, character);

    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    }
  }
  *hit_start = (i < num_code_points);
  return true;
}

// Helper function for StrPosOccurrenceUtf8
namespace {

// Returns the position of the <occurrence>-th <substr> in <str>, starting the
// search forward from <pos>.
// Both <pos> and the result are indexed from 1 and count one UTF8 character
// (which can span multiple bytes) as one unit.
//
// Algorithm:
// - Position the starting string offset to <pos>, keeping in mind that an UTF-8
//   character may span multiple 'string' characters.
// - For each occurrence:
//   - Search for the next match in <str>.
//   - If no match, return 0.
//   - Move the string offset to the second character in the match (overlapping
//     matches allowed).
// - Calculate the number of UTF-8 characters in <str> before the last match and
//   return that number + 1 (because the output is indexed from 1).
bool StrPositivePosUtf8(absl::string_view str, absl::string_view substr,
                        int64_t pos, int64_t occurrence, int64_t* out,
                        absl::Status* error) {
  ABSL_DCHECK_GT(pos, 0);
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  int32_t string_offset = 0;
  bool hit_end = false;
  if (!ForwardN(str, str_length32, pos - 1, &string_offset, &hit_end, error)) {
    return false;
  }
  if (hit_end) {
    *out = 0;
    return true;
  }

  for (int i = 0; i < occurrence; ++i) {
    // We start the next search at the second character of the previous
    // occurrence.
    if (i > 0 &&
        !ForwardN(str, str_length32, 1, &string_offset, &hit_end, error)) {
      return false;
    }

    // This is to account for cases when substr == "":
    // INSTR('ab', '', 1, 3) = 3
    // INSTR('ab', '', 1, 4) = 0
    // "ab".find("", 2) will match and return 2 (indexed from 0), so we need
    // to keep advancing string_offset because ForwardN stops at str.length()
    // and INSTR('ab', '', 1, N) for N > 3 will then all return 3 instead of 0.
    if (hit_end) {
      string_offset++;
    }

    // Safe cast because str.length() <= int32max.
    string_offset = static_cast<int32_t>(str.find(substr, string_offset));
    if (string_offset == absl::string_view::npos) {
      *out = 0;
      return true;
    }
  }

  str = str.substr(0, string_offset);
  if (!LengthUtf8(str, out, error)) return false;
  (*out)++;
  return true;
}

// Returns the position of the <occurrence>-th <substr> in <str>, starting the
// search backward from <pos> (-1 representing the last character).
// Both <pos> and the result are indexed from 1 and count one UTF8 character
// (which can span multiple bytes) as one unit.
//
// Algorithm:
// - Position the starting string offset to the end of <str> minus <pos>,
//   keeping in mind that an UTF-8 character may span multiple 'string'
//   characters.
// - For each occurrence:
//   - Search for the previous match in <str>.
//   - If no match, return 0.
//   - Move the string offset to the previous character (overlapping matches
//     allowed).
// - Calculate the number of UTF-8 characters in <str> before the last match and
//   return that number + 1 (because the output is indexed from 1).
bool StrNegativePosUtf8(absl::string_view str, absl::string_view substr,
                        int64_t pos, int64_t occurrence, int64_t* out,
                        absl::Status* error) {
  ABSL_DCHECK_LT(pos, 0);
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  int32_t string_offset = str_length32;
  bool hit_start = false;
  // -pos + 1 results in signed integer overflow as -int64min = int64max + 1.
  if (!BackN(str, -(pos + 1), &string_offset, &hit_start, error)) {
    return false;
  }
  if (hit_start) {
    *out = 0;
    return true;
  }

  // string_offset is indexed from 1, so we need to decrement it. However we
  // only do so if substr is not empty because substr == "" is a special case:
  //
  // Since we have INSTR('a', '', 1) = 1 and INSTR('a', '', 2) = 2, by
  // extension, we should also have INSTR('a', '', -1) = 2 and INSTR('a', '',
  // -2) = 1. Therefore we don't decrement string_offset in that case.
  if (!substr.empty() && string_offset-- == 0) {
    *out = 0;
    return true;
  }

  for (int i = 0; i < occurrence; ++i) {
    // We start the next search at the second character of the previous
    // occurrence.
    if (i > 0 && !BackN(str, 1, &string_offset, &hit_start, error)) {
      return false;
    }

    if (hit_start) {
      // We arrived at the beginning of the string and haven't found the
      // substr. Returns instead of wrapping around.
      *out = 0;
      return true;
    }

    // Safe cast because str.length() <= int32max.
    string_offset = static_cast<int32_t>(str.rfind(substr, string_offset));
    if (string_offset == absl::string_view::npos) {
      *out = 0;
      return true;
    }
  }

  str = str.substr(0, string_offset);
  if (!LengthUtf8(str, out, error)) return false;
  (*out)++;
  return true;
}

}  // namespace

bool StrPosOccurrenceUtf8(absl::string_view str, absl::string_view substr,
                          int64_t pos, int64_t occurrence, int64_t* out,
                          absl::Status* error) {
  if (occurrence < 1) {
    return internal::UpdateError(error, kBadOccurrenceStringPos);
  }

  if (pos == 0) {
    return internal::UpdateError(error, kBadPosStringPos);
  }

  if (pos > 0) {
    return StrPositivePosUtf8(str, substr, pos, occurrence, out, error);
  } else {
    return StrNegativePosUtf8(str, substr, pos, occurrence, out, error);
  }
}

bool StrPosOccurrenceBytes(absl::string_view str, absl::string_view substr,
                           int64_t pos, int64_t occurrence, int64_t* out,
                           absl::Status* error) {
  if (occurrence < 1) {
    return internal::UpdateError(error, kBadOccurrenceStringPos);
  }

  if (pos == 0) {
    return internal::UpdateError(error, kBadPosStringPos);
  }

  if (pos > 0) {
    absl::string_view::size_type start = pos - 1;
    for (int i = 0; i < occurrence; ++i) {
      start = str.find(substr, start);
      if (start == absl::string_view::npos) {
        *out = 0;
        return true;
      }
      // We start the next search at the second character of the previous
      // occurrence.
      start++;
    }

    // start has already been incremented so not need to increment it again to
    // account for the fact that absl::string_view is indexed from 0.
    *out = start;
    return true;
  } else {
    int32_t str_length32;
    if (!CheckAndCastStrLength(str, &str_length32, error)) {
      return false;
    }

    // substr == "" is a special case. Since we have
    // INSTR('a', '', 1) = 1 and INSTR('a', '', 2) = 2,
    // by extension, we should also have
    // INSTR('a', '', -1) = 2 and INSTR('a', '', -2) = 1.
    // Therefore we increment start in that case.
    if (substr.empty()) {
      pos++;
    }

    if (static_cast<int64_t>(str_length32) + pos < 0) {
      *out = 0;
      return true;
    }

    absl::string_view::size_type start = str_length32 + pos;
    for (int i = 0; i < occurrence; ++i) {
      // We start the next search at the second character of the previous
      // occurrence.
      if (i > 0 && start-- == 0) {
        // We arrived at the beginning of the string and haven't found the
        // substr. Returns instead of wrapping around.
        *out = 0;
        return true;
      }
      start = str.rfind(substr, start);
      if (start == absl::string_view::npos) {
        *out = 0;
        return true;
      }
    }

    // absl::string_view is indexed from 0.
    *out = start + 1;
    return true;
  }
}

// This function handles SUBSTR(str, pos, length) where pos is negative.
// In this case pos identifies a character counting from the end of the string.
static bool SubstrSuffixUtf8(absl::string_view str, int64_t pos, int64_t length,
                             absl::string_view* out, absl::Status* error) {
  ABSL_DCHECK_LT(pos, 0);
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  // suffix_length is the number of characters that follow resulting
  // substring. We don't know the full length of str here, and
  // -pos can be greater than the length, so here we just estimate it
  // assuming that -pos <= length(str).
  int64_t suffix_length;
  if (pos + length >= 0) {
    suffix_length = 0;
    length = -pos;
  } else {
    suffix_length = -(pos + length);
  }
  const int32_t suffix_length32 = ClampToInt32Max(suffix_length);

  int32_t suffix_end_offset = str_length32;
  // Walk the string backwards from the end counting suffix_length characters
  // to find the end of the substring.
  bool unused;
  if (!BackN(str, suffix_length32, &suffix_end_offset, &unused, error)) {
    return false;
  }
  // At this point, suffix_end_offset is just a guess, we'll fix it later.
  int32_t suffix_start_offset = suffix_end_offset;

  while (length > 0 && suffix_start_offset > 0) {
    if (!BackN(str, 1, &suffix_start_offset, &unused, error)) {
      return false;
    }
    length--;
  }

  if (length > 0) {
    const int32_t length32 = ClampToInt32Max(length);
    // Hit the start of the string, so we need to fix our estimated end point
    // by pushing it out the remaining length.
    bool unused;
    if (!ForwardN(str, str_length32, length32, &suffix_end_offset, &unused,
                  error)) {
      return false;
    }
  }
  // Cast guaranteed safe
  *out =
      str.substr(static_cast<size_t>(suffix_start_offset),
                 static_cast<size_t>(suffix_end_offset - suffix_start_offset));
  return true;
}

bool SubstrWithLengthUtf8(absl::string_view str, int64_t pos, int64_t length,
                          absl::string_view* out, absl::Status* error) {
  if (length < 0) {
    return internal::UpdateError(
        error, "Third argument in SUBSTR() cannot be negative");
  }
  if (pos < 0) {
    if (pos < std::numeric_limits<int32_t>::lowest() || -pos > str.length()) {
      // If position is more negative than the length of the string, then
      // we short circuit actual suffix computation and just start at 0.
      pos = 0;
    } else {
      return SubstrSuffixUtf8(str, pos, length, out, error);
    }
  } else if (pos > 0) {
    // pos is 1-based.
    pos -= 1;
  }
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  const int32_t length32 = ClampToInt32Max(length);

  int32_t start_offset = 0;
  if (pos >= str_length32) {
    // Conceptually, U8_FWD_N should perform this check itself, but it
    // explicitly documents that pos must be < str_length32 as a precondition.
    *out = absl::string_view("", 0);
    return true;
  }
  bool unused;
  if (!ForwardN(str, str_length32, pos, &start_offset, &unused, error)) {
    return false;
  }
  // offset is now at the start we might need to truncate.
  if (start_offset >= str_length32) {
    *out = absl::string_view("", 0);
    return true;
  }

  // Cast of start_offset is safe; we've guaranteed start_offset < str_length32.
  if (str_length32 - static_cast<int32_t>(start_offset) < length) {
    // Shortcut: length is definitely longer than the substring after start.
    // Return the entire suffix.
    *out = str.substr(start_offset);
  } else {
    int32_t end_offset = start_offset;
    if (!ForwardN(str, str_length32, length32, &end_offset, &unused, error)) {
      return false;
    }
    *out = str.substr(start_offset, end_offset - start_offset);
  }

  return true;
}

bool SubstrBytes(absl::string_view str, int64_t pos, absl::string_view* out,
                 absl::Status* error) {
  return SubstrWithLengthBytes(str, pos, std::numeric_limits<int64_t>::max(),
                               out, error);
}

bool SubstrWithLengthBytes(absl::string_view str, int64_t pos, int64_t length,
                           absl::string_view* out, absl::Status* error) {
  if (length < 0) {
    return internal::UpdateError(
        error, "Third argument in SUBSTR() cannot be negative");
  }
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  if (pos < 0) {
    pos += str.length();  // cannot overflow (just barely).
    if (pos < 0) pos = 0;
  } else if (pos > 0) {
    pos -= 1;
    if (pos > static_cast<int64_t>(str.length())) {
      // Pos is beyond the end of the string. Return an empty string.
      *out = absl::string_view("", 0);
      return true;
    }
  }
  *out = absl::ClippedSubstr(str, pos, length);
  return true;
}

// UPPER(STRING) -> STRING
bool UpperUtf8(absl::string_view str, std::string* out, absl::Status* error) {
  int32_t str_length32;  // unused
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  out->clear();
  out->reserve(str.length());

  icu::ErrorCode status;
  icu::StringByteSink<std::string> icu_out(out);
  icu::CaseMap::utf8ToUpper("" /* root locale */, 0 /* default options */, str,
                            icu_out, nullptr /* edits - unused */, status);
  if (status.isFailure()) {
    error->Update(
        absl::Status(absl::StatusCode::kInternal,
                     absl::StrCat("icu::CaseMap::utf8ToUpper error: %s",
                                  status.errorName())));
    status.reset();
    return false;
  }
  return true;
}

// LOWER(STRING) -> STRING
bool LowerUtf8(absl::string_view str, std::string* out, absl::Status* error) {
  int32_t str_length32;  // unused
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  out->clear();
  out->reserve(str.length());

  icu::ErrorCode icu_status;
  icu::StringByteSink<std::string> icu_out(out);
  icu::CaseMap::utf8ToLower("" /* root locale */, 0 /* default options */, str,
                            icu_out, nullptr /* edits - unused */, icu_status);
  if (icu_status.isFailure()) {
    error->Update(
        absl::Status(absl::StatusCode::kInternal,
                     absl::StrCat("icu::CaseMap::utf8ToUpper error: %s",
                                  icu_status.errorName())));
    icu_status.reset();
    return false;
  }
  return true;
}

bool UpperBytes(absl::string_view str, std::string* out, absl::Status* error) {
  googlesql_base::STLStringResizeUninitialized(out, str.size());
  absl::c_transform(str, out->begin(),
                    [](char c) { return absl::ascii_toupper(c); });
  return true;
}

bool LowerBytes(absl::string_view str, std::string* out, absl::Status* error) {
  googlesql_base::STLStringResizeUninitialized(out, str.size());
  absl::c_transform(str, out->begin(),
                    [](char c) { return absl::ascii_tolower(c); });
  return true;
}

bool InitialCapitalize(absl::string_view str, absl::string_view delimiters,
                       std::string* out, absl::Status* error) {
  Utf8Capitalizer capitalizer;
  if (!capitalizer.Initialize(delimiters, error)) {
    return false;
  }
  return capitalizer.Capitalize(str, out, error);
}

bool InitialCapitalizeDefault(absl::string_view str, std::string* out,
                              absl::Status* error) {
  Utf8Capitalizer capitalizer;
  if (!capitalizer.InitializeDefault(error)) {
    return false;
  }
  return capitalizer.Capitalize(str, out, error);
}

// REPLACE(STRING, STRING, STRING) -> STRING
bool ReplaceUtf8(absl::string_view str, absl::string_view oldsub,
                 absl::string_view newsub, std::string* out,
                 absl::Status* error) {
  out->clear();
  return GlobalStringReplace(str, oldsub, newsub, out, error);
}

// REPLACE(BYTES, BYTES, BYTES) -> BYTES
bool ReplaceBytes(absl::string_view str, absl::string_view oldsub,
                  absl::string_view newsub, std::string* out,
                  absl::Status* error) {
  out->clear();
  return GlobalStringReplace(str, oldsub, newsub, out, error);
}

template <class Container>
bool SplitUtf8Impl(absl::string_view str, absl::string_view delimiter,
                   Container* out, absl::Status* error) {
  if (delimiter.empty()) {
    out->clear();
    if (str.empty()) {
      out->push_back("");
      return true;
    }
    size_t offset = 0;
    const size_t length = str.length();

    while (offset < length) {
      const size_t prev = offset;
      UChar32 character;
      U8_NEXT(str.data(), offset, length, character);
      if (character < 0) {
        return internal::UpdateError(error, kBadUtf8);
      } else {
        out->emplace_back(str.substr(prev, offset - prev));
      }
    }

    return true;
  }

  if (!IsWellFormedUTF8(delimiter)) {
    return internal::UpdateError(
        error, "Delimiter in SPLIT function is not a valid UTF-8 string");
  }
  // If str is valid UTF8 string, then regular absl::StrSplit is guaranteed to
  // produce correct results, since no valid UTF8 sequence is ever a prefix of
  // another valid UTF8 sequence (one of fundamental UTF8 design points).
  // Special case empty StringView, because absl::StrSplit behaves differently
  // on StringView() vs. StringView("", 0)
  if (ABSL_PREDICT_FALSE(str.empty())) {
    str = absl::string_view("", 0);
  }
  *out = absl::StrSplit(str, delimiter);
  return true;
}

bool SplitUtf8(absl::string_view str, absl::string_view delimiter,
               std::vector<std::string>* out, absl::Status* error) {
  return SplitUtf8Impl(str, delimiter, out, error);
}

bool SplitUtf8(absl::string_view str, absl::string_view delimiter,
               std::vector<absl::string_view>* out, absl::Status* error) {
  return SplitUtf8Impl(str, delimiter, out, error);
}

bool SplitBytes(absl::string_view str, absl::string_view delimiter,
                std::vector<std::string>* out, absl::Status* error) {
  // Special case empty StringView, because absl::StrSplit behaves differently
  // on StringView() vs. StringView("", 0)
  if (ABSL_PREDICT_FALSE(str.empty())) {
    str = absl::string_view("", 0);
  }
  *out = absl::StrSplit(str, delimiter);
  return true;
}
bool SplitBytes(absl::string_view str, absl::string_view delimiter,
                std::vector<absl::string_view>* out, absl::Status* error) {
  // Special case empty StringView, because absl::StrSplit behaves differently
  // on StringView() vs. StringView("", 0)
  if (ABSL_PREDICT_FALSE(str.empty())) {
    str = absl::string_view("", 0);
  }
  *out = absl::StrSplit(str, delimiter);
  return true;
}

bool SafeConvertBytes(absl::string_view str, std::string* out,
                      absl::Status* error) {
  // Note, this implementation is _nearly_ identical to CoerceToWellFormedUTF8
  // (from utf_util), but it replaces _every byte_ with "\uFFFD" instead of each
  // almost-Unicode sequence.
  // If the 'str' contains only valid UTF-8 chars, simply copies it to 'out',
  // then returns. Otherwise, replaces the invalid UTF-8 chars with U+FFFD.
  const char* str_data = str.data();
  const size_t length = str.length();
  size_t prev = 0;
  for (size_t i = 0; i < length; /* U8_NEXT increments i */) {
    size_t start = i;
    UChar32 character;
    U8_NEXT(str_data, i, length, character);
    if (character < 0) {
      if (prev == 0) {
        // This was the first time we found a bad character, do some
        // initialization.
        // Determines the max size of the output after invalid char
        // replacements. In worst case, every char is invalid and replaced by
        // U+FFFD.
        size_t max_out_buf_size =
            start + (length - start) * kUtf8ReplacementChar.length();
        out->clear();
        out->reserve(max_out_buf_size);
      }
      if (prev < start) {
        // Append the well-formed span between the last ill-formed sequence
        // (or start of input), and the point just before the current one.
        out->append(str_data + prev, start - prev);
      }
      for (size_t j = start; j < i; ++j) {
        out->append(kUtf8ReplacementChar);
      }
      prev = i;
    }
  }
  if (prev == 0) {
    // str was actually just fine.
    *out = str;
  } else if (prev < length) {
    // Append any remaining well formed span.
    out->append(str_data + prev, length - prev);
  }

  return true;
}

bool Normalize(absl::string_view str, NormalizeMode mode, bool is_casefold,
               std::string* out, absl::Status* error) {
  const icu::Normalizer2* normalizer = GetNormalizerByMode(mode, error);
  if (!error->ok()) {
    return false;
  }
  icu::ErrorCode icu_errorcode;
  icu::UnicodeString unicode_str;
  normalizer->normalize(icu::UnicodeString::fromUTF8(str), unicode_str,
                        icu_errorcode);
  if (icu_errorcode.isFailure()) {
    return internal::UpdateError(
        error, absl::StrCat("Failed to normalize string with error: ",
                            icu_errorcode.errorName()));
  }
  if (is_casefold) {
    unicode_str.foldCase();  // Uses default U_FOLD_CASE_DEFAULT option.
  }
  out->clear();
  unicode_str.toUTF8String(*out);
  return true;
}

bool ToBase64(absl::string_view str, std::string* out, absl::Status* error) {
  absl::Base64Escape(str, out);
  return true;
}

bool FromBase64(absl::string_view str, std::string* out, absl::Status* error) {
  if (!absl::Base64Unescape(str, out)) {
    return internal::UpdateError(error,
                                 "Failed to decode invalid base64 string");
  }
  return true;
}

bool ToHex(absl::string_view str, std::string* out, absl::Status* error) {
  *out = absl::BytesToHexString(str);
  return true;
}

bool FromHex(absl::string_view str, std::string* out, absl::Status* error) {
  if (str.empty()) {
    out->clear();
    return true;
  }
  int offset = 0;
  for (char c : str) {
    if (!absl::ascii_isxdigit(c)) {
      return internal::UpdateError(
          error, absl::Substitute("Failed to decode invalid hexadecimal "
                                  "string due to character at offset $0: $1",
                                  offset, str));
    }
    ++offset;
  }
  // Account for strings with an odd number of hex digits.
  if (str.size() % 2 == 1) {
    googlesql_base::STLStringResizeUninitialized(out, (str.size() + 1) / 2);
    char* string_ptr = &(*out)[0];
    *string_ptr = googlesql_base::hex_digit_to_int(str[0]);
    ++string_ptr;
    str.remove_prefix(1);
    std::string tmp = absl::HexStringToBytes(str);
    out->replace(1, tmp.size(), tmp);
  } else {
    *out = absl::HexStringToBytes(str);
  }
  return true;
}

bool FirstCharOfStringToASCII(absl::string_view str, int64_t* out,
                              absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  if (str_length32 == 0) {
    *out = 0;
  } else {
    UChar32 character;
    size_t i = 0;
    U8_NEXT(str.data(), i, str_length32, character);
    if (character < 0 || character > 127) {
      return internal::UpdateError(
          error, absl::Substitute("Argument to ASCII is not a "
                                  "structurally valid ASCII string: '$0'",
                                  str));
    }
    *out = character;
  }
  return true;
}

bool FirstByteOfBytesToASCII(absl::string_view str, int64_t* out,
                             absl::Status* error) {
  if (str.empty()) {
    *out = 0;
  } else {
    *out = static_cast<int64_t>(absl::bit_cast<uint8_t>(str[0]));
  }
  return true;
}

bool FirstCharToCodePoint(absl::string_view str, int64_t* out,
                          absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }
  if (str_length32 == 0) {
    *out = 0;
  } else {
    UChar32 character;
    size_t i = 0;
    U8_NEXT(str.data(), i, str_length32, character);
    if (character < 0) {
      std::string hexFirstChar = absl::BytesToHexString(str.substr(0, i));
      ABSL_DCHECK_EQ(hexFirstChar.size() % 2, 0);
      std::string bytesFirstChar;
      size_t sizeOfFirstBytes = hexFirstChar.size() + hexFirstChar.size() / 2;
      bytesFirstChar.reserve(sizeOfFirstBytes);
      for (int i = 0; i < hexFirstChar.size(); i += 2) {
        bytesFirstChar.push_back('\\');
        bytesFirstChar.push_back('x');
        bytesFirstChar.push_back(hexFirstChar[i]);
        bytesFirstChar.push_back(hexFirstChar[i + 1]);
      }
      return internal::UpdateError(
          error, absl::Substitute("First char of input is not a "
                                  "structurally valid UTF-8 character: '$0'",
                                  bytesFirstChar));
    }
    *out = character;
  }
  return true;
}

bool CodePointToString(int64_t codepoint, std::string* out,
                       absl::Status* error) {
  return CodePointsToString(absl::MakeConstSpan(&codepoint, 1), out, error);
}

bool StringToCodePoints(absl::string_view str, std::vector<int64_t>* out,
                        absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  out->clear();
  for (size_t i = 0; i < str_length32; /* U8_NEXT increments i */) {
    UChar32 character;
    U8_NEXT(str.data(), i, str_length32, character);
    if (character < 0) {
      return internal::UpdateError(
          error, absl::Substitute("Argument to TO_CODE_POINTS is not a "
                                  "structurally valid UTF-8 string: '$0'",
                                  str));
    }
    out->push_back(character);
  }
  return true;
}

bool BytesToCodePoints(absl::string_view str, std::vector<int64_t>* out,
                       absl::Status* error) {
  out->clear();
  for (char c : str) {
    // Represent ASCII values in the range [0, 255].
    const unsigned char ascii_value = absl::bit_cast<uint8_t>(c);
    out->push_back(ascii_value);
  }
  return true;
}

bool CodePointsToString(absl::Span<const int64_t> codepoints, std::string* out,
                        absl::Status* error) {
  out->clear();
  if (codepoints.empty()) {
    return true;
  }
  // This method will perform either 1 or 2 resizes. The first resize assumes
  // we can encode each codepoint as a single byte, with a little extra buffer
  // to simplify the later resize logic.
  // The 2nd resize will occur if the above assumption is wrong, and will just
  // jump straight to the most pessimistic allocation (that every remaining
  // codepoint is 4 bytes).
  //
  // Start by assuming minimal sized (1-byte) codepoints, plus 3 (because we
  // want to guarantee each call has minimum capacity for 4 bytes (the max
  // utf8 byte counts).
  googlesql_base::STLStringResizeUninitialized(out, codepoints.size() + U8_MAX_LENGTH - 1);
  size_t out_offset = 0;
  for (size_t i = 0; i < codepoints.size(); ++i) {
    // We use uint32 instead of UChar32 because, for some reason, U8_APPEND
    // expects it (unlike every other `U8_` function).
    int64_t codepoint = codepoints[i];
    if (codepoint < 0 || codepoint > std::numeric_limits<uint32_t>::max()) {
      return internal::UpdateError(
          error, absl::Substitute("Invalid codepoint $0", codepoint));
    }
    uint32_t codepoint32 = static_cast<uint32_t>(codepoint);
    size_t remaining_capacity = out->size() - out_offset;
    if (remaining_capacity < U8_MAX_LENGTH) {
      // We want to guarantee we have enough headroom to avoid additional
      // allocations.
      out->resize(out_offset + (codepoints.size() - i) * U8_MAX_LENGTH);
    }
    bool is_error = false;
    U8_APPEND(out->data(), out_offset, out->size(), codepoint32, is_error);
    if (is_error) {
      out->clear();
      return internal::UpdateError(
          error, absl::Substitute("Invalid codepoint $0", codepoint));
    }
  }
  // out_offset will be exactly the size we need.
  out->resize(out_offset);
  return true;
}

bool CodePointsToBytes(absl::Span<const int64_t> codepoints, std::string* out,
                       absl::Status* error) {
  out->clear();
  for (int64_t codepoint : codepoints) {
    if (codepoint < 0 ||
        codepoint > std::numeric_limits<unsigned char>::max()) {
      return internal::UpdateError(
          error, absl::Substitute("Invalid ASCII value $0", codepoint));
    }
    out->push_back(codepoint);
  }
  return true;
}

// Helper function namespace.
namespace {

// Helper function for all padding functions.
bool VerifyPadInputs(absl::string_view input_str, absl::string_view pattern,
                     int64_t output_size, absl::Status* error) {
  if (output_size < 0) {
    return internal::UpdateError(
        error,
        "Second argument (output size) for LPAD/RPAD cannot be negative");
  } else if (output_size > kMaxOutputSize) {
    return internal::UpdateError(error, kExceededPadOutputSize);
  } else if (pattern.empty()) {
    return internal::UpdateError(
        error, "Third argument (pad pattern) for LPAD/RPAD cannot be empty");
  }
  return true;
}

// Helper function for RightPadBytes and LeftPadBytes
bool PadBytes(absl::string_view input_str, int64_t output_size_bytes,
              absl::string_view pattern, bool left_pad, std::string* out,
              absl::Status* error) {
  if (!VerifyPadInputs(input_str, pattern, output_size_bytes, error)) {
    return false;
  }
  if (output_size_bytes <= static_cast<int64_t>(input_str.length())) {
    out->assign(input_str, 0, output_size_bytes);
    return true;
  }

  out->clear();
  out->reserve(output_size_bytes);
  if (!left_pad) {
    // Fill input_str.
    absl::StrAppend(out, input_str);
  }

  // Fill pattern.
  const int64_t pad_size_bytes = output_size_bytes - input_str.length();
  if (pattern.length() == 1) {
    out->append(pad_size_bytes, pattern[0]);
  } else {
    auto div_result =
        std::div(pad_size_bytes, static_cast<int64_t>(pattern.length()));
    for (int64_t i = 0; i < div_result.quot; ++i) {
      absl::StrAppend(out, pattern);
    }
    absl::StrAppend(out, pattern.substr(0, div_result.rem));
  }

  if (left_pad) {
    // Fill input_str.
    absl::StrAppend(out, input_str);
  }
  return true;
}

// Helper function for RightPadUtf8 and LeftPadUtf8.
bool PadUtf8(absl::string_view input_str, int64_t output_size_chars,
             absl::string_view pattern, bool left_pad, std::string* out,
             absl::Status* error) {
  if (!VerifyPadInputs(input_str, pattern, output_size_chars, error)) {
    return false;
  }

  if (!IsWellFormedUTF8(input_str)) {
    return internal::UpdateError(error, kBadUtf8);
  }

  int64_t input_size_chars = 0;
  LengthUtf8(input_str, &input_size_chars, error);

  if (output_size_chars <= input_size_chars) {
    absl::string_view input_str_prefix;
    SubstrWithLengthUtf8(input_str, 0, output_size_chars, &input_str_prefix,
                         error);
    if (input_str_prefix.length() > kMaxOutputSize) {
      return internal::UpdateError(error, kExceededPadOutputSize);
    }
    *out = input_str_prefix;
    return true;
  }

  if (!IsWellFormedUTF8(pattern)) {
    return internal::UpdateError(error, kBadUtf8);
  }

  int64_t pattern_size_chars = 1;
  if (pattern.length() > 1) {
    LengthUtf8(pattern, &pattern_size_chars, error);
  }
  auto padding_div =
      std::div(output_size_chars - input_size_chars, pattern_size_chars);
  absl::string_view pattern_prefix;

  SubstrWithLengthUtf8(pattern, 0, padding_div.rem, &pattern_prefix, error);
  // To compute output_size_bytes correctly in all cases we need
  // 'lg(kMaxOutputSize) +  3' bits. If kMaxOutputSize is changed ensure that
  // 'lg(kMaxOutputSize) + 3 <= 64'.
  const size_t output_size_bytes = (padding_div.quot * pattern.length()) +
                                   pattern_prefix.length() + input_str.length();
  if (output_size_bytes > kMaxOutputSize) {
    return internal::UpdateError(error, kExceededPadOutputSize);
  }

  out->clear();
  out->reserve(output_size_bytes);
  int64_t rep_count = padding_div.quot;

  if (!left_pad) {
    // Fill input_str.
    absl::StrAppend(out, input_str);
  }

  // Fill pattern.
  if (pattern.length() == 1) {
    out->append(rep_count, pattern[0]);
  } else {
    for (int64_t i = 0; i < rep_count; ++i) {
      absl::StrAppend(out, pattern);
    }
    absl::StrAppend(out, pattern_prefix);
  }

  if (left_pad) {
    // Fill input_str.
    absl::StrAppend(out, input_str);
  }

  return true;
}

}  // namespace

bool LeftPadBytes(absl::string_view input_str, int64_t output_size_bytes,
                  absl::string_view pattern, std::string* out,
                  absl::Status* error) {
  return PadBytes(input_str, output_size_bytes, pattern, true /* left_pad */,
                  out, error);
}

bool LeftPadBytesDefault(absl::string_view input_str, int64_t output_size_bytes,
                         std::string* out, absl::Status* error) {
  return PadBytes(input_str, output_size_bytes, " ", true /* left_pad */, out,
                  error);
}

bool LeftPadUtf8(absl::string_view input_str, int64_t output_size_chars,
                 absl::string_view pattern, std::string* out,
                 absl::Status* error) {
  return PadUtf8(input_str, output_size_chars, pattern, true /* left_pad */,
                 out, error);
}

bool LeftPadUtf8Default(absl::string_view input_str, int64_t output_size_chars,
                        std::string* out, absl::Status* error) {
  return PadUtf8(input_str, output_size_chars, " ", true /* left_pad */, out,
                 error);
}

bool RightPadBytes(absl::string_view input_str, int64_t output_size_bytes,
                   absl::string_view pattern, std::string* out,
                   absl::Status* error) {
  return PadBytes(input_str, output_size_bytes, pattern, false /* left_pad */,
                  out, error);
}

bool RightPadBytesDefault(absl::string_view input_str,
                          int64_t output_size_bytes, std::string* out,
                          absl::Status* error) {
  return PadBytes(input_str, output_size_bytes, " ", false /* left_pad */, out,
                  error);
}

bool RightPadUtf8(absl::string_view input_str, int64_t output_size_chars,
                  absl::string_view pattern, std::string* out,
                  absl::Status* error) {
  return PadUtf8(input_str, output_size_chars, pattern, false /* left_pad */,
                 out, error);
}

bool RightPadUtf8Default(absl::string_view input_str, int64_t output_size_chars,
                         std::string* out, absl::Status* error) {
  return PadUtf8(input_str, output_size_chars, " ", false /* left_pad */, out,
                 error);
}

bool Repeat(absl::string_view input_str, int64_t repeat_count, std::string* out,
            absl::Status* error) {
  if (repeat_count < 0) {
    return internal::UpdateError(
        error, "Second argument (repeat count) for REPEAT cannot be negative");
  }

  out->clear();
  if (!input_str.empty()) {
    size_t rep_mem_bytes = 0;
    if (input_str.length() > kMaxOutputSize || repeat_count > kMaxOutputSize ||
        (rep_mem_bytes = repeat_count * input_str.length()) > kMaxOutputSize) {
      // repeat_count * input_str.length() should not overflow as it needs at
      // most 60 bits.
      return internal::UpdateError(error, kExceededRepeatOutputSize);
    }
    out->reserve(rep_mem_bytes);
    for (int64_t i = 0; i < repeat_count; ++i) {
      absl::StrAppend(out, input_str);
    }
  }
  return true;
}

bool ReverseBytes(absl::string_view input, std::string* out,
                  absl::Status* error) {
  out->assign(input.rbegin(), input.rend());
  return true;
}

bool ReverseUtf8(absl::string_view input, std::string* out,
                 absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(input, &str_length32, error)) {
    return false;
  }

  out->clear();
  out->reserve(input.size());

  int32_t offset = str_length32;  // start at the end
  while (offset > 0) {
    int32_t prev_offset = offset;
    UChar32 character;
    U8_PREV(input.data(), 0, offset, character);
    if (character < 0) {
      return internal::UpdateError(
          error, absl::Substitute("Argument to REVERSE is not a structurally "
                                  "valid UTF-8 string: '$0'",
                                  input));
    }
    out->append(input.begin() + offset, input.begin() + prev_offset);
  }
  return true;
}

LikeRewriteType GetRewriteForLikePattern(bool is_string,
                                         absl::string_view pattern,
                                         absl::string_view* substring) {
  // Don't attempt to rewrite LIKE where the pattern contains \ or _, the former
  // of which is an escape character and the latter of which is a wildcard. Also
  // don't attempt to rewrite LIKE where a % appears somewhere other than the
  // beginning or end.
  bool starts_with_percent = false;
  bool ends_with_percent = false;
  if (!is_string) {
    while (absl::ConsumePrefix(&pattern, "%")) {
      starts_with_percent = true;
    }
    while (absl::ConsumeSuffix(&pattern, "%")) {
      ends_with_percent = true;
    }

    for (char c : pattern) {
      switch (c) {
        case '\\':
        case '_':
        case '%':
          return LikeRewriteType::kNoRewrite;
        default:
          break;
      }
    }
  } else {
    // With UTF-8, some characters may be multiple bytes, so we instead need to
    // scan code points.
    if (!IsWellFormedUTF8(pattern)) {
      return LikeRewriteType::kNoRewrite;
    }
    while (!pattern.empty() && pattern[0] == '%' &&
           absl::ConsumePrefix(&pattern, "%")) {
      starts_with_percent = true;
    }

    absl::string_view::size_type pos = 0;
    while (pos < static_cast<int64_t>(pattern.size())) {
      absl::string_view::size_type next_pos = pos;
      // We've already tested this to be well formed; so we use the 'UNSAFE'
      // variant (it may have been modified by 'ConsumePrefix', but this is a
      // safe mutation).
      U8_FWD_1_UNSAFE(pattern.data(), next_pos);

      const absl::string_view::size_type character_length = next_pos - pos;
      if (character_length == 1) {
        switch (pattern[pos]) {
          case '\\':
          case '_':
            return LikeRewriteType::kNoRewrite;
          case '%': {
            // We allow any number of trailing '%' at the end. So make sure
            // that's all we have left here.
            for (absl::string_view::size_type j = pos + 1; j < pattern.length();
                 ++j) {
              if (pattern[j] != '%') {
                // If we see anything else, then we cannot rewrite this so
                // return.
                // Note, there is no guarantee that pattern[j] is actually
                // a one byte pattern, however, we only care that it is not an
                // ascii '%'.
                return LikeRewriteType::kNoRewrite;
              }
            }

            ends_with_percent = true;
            pattern.remove_suffix(pattern.size() - pos);
            break;
          }
          default:
            break;
        }
      }
      pos += character_length;
    }
  }

  // At this point, the pattern will have its leading and trailing '%'
  // stripped (if any) and will not contain any escape or wildcard characters.

  *substring = pattern;
  if ((starts_with_percent || ends_with_percent) && pattern.empty()) {
    // The pattern was '%' or '%%', both of which evaluate to true for any non-
    // null string input.
    return LikeRewriteType::kNotNull;
  }

  if (starts_with_percent && ends_with_percent) {
    // Rewrite "input LIKE '%pattern%'" to a containment check.
    return LikeRewriteType::kContains;
  } else if (starts_with_percent) {
    // Rewrite "input LIKE '%pattern'" to ENDS_WITH.
    return LikeRewriteType::kEndsWith;
  } else if (ends_with_percent) {
    // Rewrite "input LIKE 'pattern%'" to STARTS_WITH.
    return LikeRewriteType::kStartsWith;
  } else {
    // Rewrite "input LIKE 'pattern'" to an equality check.
    return LikeRewriteType::kEquals;
  }
}

// Helper constant for Soundex.
namespace {

// Mapping:
// Non alpha character: 7
//
// Same for lower case.
// A, E, I, O, U, Y, H, W: 0
// B, F, P, V: 1
// C, G, J, K, Q, S, X, Z: 2
// D, T: 3
// L: 4
// M, N: 5
// R: 6
//
// Multi-byte UTF8 characters have 1 as first bit in all bytes, so they will
// return '7' (invalid).
constexpr char kSoundexCodeMap[256] = {
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '0', '1', '2', '3', '0', '1', '2', '0', '0', '2',
    '2', '4', '5', '5', '0', '1', '2', '6', '2', '3', '0', '1', '0', '2', '0',
    '2', '7', '7', '7', '7', '7', '7', '0', '1', '2', '3', '0', '1', '2', '0',
    '0', '2', '2', '4', '5', '5', '0', '1', '2', '6', '2', '3', '0', '1', '0',
    '2', '0', '2', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7', '7',
    '7'};

}  // namespace

// The Soundex code for a string consists of a letter followed by three
// digits: the letter is the first letter of the string, and the digits
// encode the remaining consonants. Consonants at a similar place of
// articulation share the same digit so, for example, the labial consonants B,
// F, P, and V are each encoded as the number 1.
//
// Specifications: (broken link)
//
// Algorithm:
// - Set the first alpha character of the result to the first alpha letter of
//   string and get the corresponding code from kSoundexCodeMap.
// - Do the following until we have 4 characters in the result, or we reach the
//   end of string:
//   - Get the code for the next character in string.
//   - If it is 0 or 7, continue.
//   - If it is different from the previous code, add the code to the result.
// - If the result has less than 4 characters, pad it with '0'.
bool Soundex(absl::string_view str, std::string* out, absl::Status* error) {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  char result[] = "0000";
  int res_index = 0;

  char previous = '0';
  for (size_t str_index = 0; res_index < 4 && str_index < str_length32;
       str_index++) {
    char current = kSoundexCodeMap[absl::bit_cast<uint8_t>(str.at(str_index))];
    if (current == '7') {
      // Invalid character, skip.
      continue;
    }

    if (res_index == 0) {
      // First valid character.
      result[res_index++] = str.at(str_index);
      previous = current;
    } else if (current != '0' && current != previous) {
      result[res_index++] = current;
      previous = current;
    }
  }
  if (res_index == 0) {
    // Empty input (after removing invalid characters), return empty string.
    out->clear();
  } else {
    *out = result;
  }

  return true;
}

bool Utf8Translator::Initialize(absl::string_view source_characters,
                                absl::string_view target_characters,
                                absl::Status* error) {
  int32_t source_length32;
  if (!CheckAndCastStrLength(source_characters, &source_length32, error)) {
    return false;
  }
  int32_t target_length32;
  if (!CheckAndCastStrLength(target_characters, &target_length32, error)) {
    return false;
  }

  size_t target_offset = 0;
  for (size_t source_offset = 0; source_offset < source_length32;
       /* U8_NEXT increments source_offset */) {
    const size_t previous_source_offset = source_offset;
    UChar32 character;
    U8_NEXT(source_characters.data(), source_offset, source_length32,
            character);
    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    }

    absl::string_view target_string_view;
    if (target_offset < target_length32) {
      const size_t previous_target_offset = target_offset;
      UChar32 target_character;
      U8_NEXT(target_characters.data(), target_offset, target_length32,
              target_character);
      if (target_character < 0) {
        return internal::UpdateError(error, kBadUtf8);
      }
      target_string_view = target_characters.substr(
          previous_target_offset, target_offset - previous_target_offset);
    }
    if (!mapping_.insert({character, target_string_view}).second) {
      return internal::UpdateError(
          error, absl::Substitute(
                     "Duplicate character $0 in TRANSLATE source characters",
                     ToStringLiteral(source_characters.substr(
                         previous_source_offset,
                         source_offset - previous_source_offset))));
    }
  }
  return true;
}

// Replaces characters in <str> according to <mapping_>.
//
// Complexity: Expected O(N), N being the size of <str>.
bool Utf8Translator::Translate(absl::string_view str, std::string* out,
                               absl::Status* error) const {
  int32_t str_length32;
  if (!CheckAndCastStrLength(str, &str_length32, error)) {
    return false;
  }

  out->clear();
  // The output can be theorically 4 times as long as the input, if each
  // single-byte character is replaced by a 4-byte UTF8 character.
  // We reserve 120% of the original str size for the output string to avoid the
  // cost of re-allocating memory while not over allocating prematurely.
  // The number 120% is arbitrary.
  out->reserve(
      std::min(static_cast<size_t>(str_length32 * 1.2), kMaxOutputSize));

  absl::string_view current_character;
  for (size_t i = 0; i < str_length32; /* U8_NEXT increments i */) {
    const size_t previous = i;
    UChar32 character;
    U8_NEXT(str.data(), i, str_length32, character);
    if (character < 0) {
      return internal::UpdateError(error, kBadUtf8);
    }
    const absl::string_view* target_string_view =
        googlesql_base::FindOrNull(mapping_, character);
    if (target_string_view == nullptr) {
      // We copy the original character.
      current_character = str.substr(previous, i - previous);
      target_string_view = &current_character;
    }

    if (!target_string_view->empty()) {
      if (out->size() + target_string_view->length() > kMaxOutputSize) {
        return internal::UpdateError(error, kExceededTranslateOutputSize);
      }
      out->append(*target_string_view);
    }
  }

  return true;
}

bool BytesTranslator::Initialize(absl::string_view source_bytes,
                                 absl::string_view target_bytes,
                                 absl::Status* error) {
  std::bitset<256> assigned_bytes;

  for (size_t i = 0; i < 256; ++i) {
    mapping_[i] = static_cast<char>(i);
  }

  for (size_t i = 0; i < source_bytes.length(); ++i) {
    uint8_t source_byte = static_cast<uint8_t>(source_bytes[i]);
    if (assigned_bytes[source_byte]) {
      return internal::UpdateError(
          error,
          absl::StrFormat("Duplicate byte 0x%02x in TRANSLATE source bytes",
                          source_byte));
    }
    if (i < target_bytes.length()) {
      mapping_[source_byte] = target_bytes[i];
    } else {
      skipped_bytes_[source_byte] = true;
    }
    assigned_bytes[source_byte] = true;
  }
  initialized_ = true;
  return true;
}

// Replaces bytes in <str> according to <mapping_>.
//
// Complexity: O(N), N being the size of <str>.
bool BytesTranslator::Translate(absl::string_view str, std::string* out,
                                absl::Status* error) const {
  if (!initialized_) {
    // done use UpdateError because we want an internal error.
    *error = absl::Status(absl::StatusCode::kInternal,
                          "BytesTranslator used before Ininitialize");
    return false;
  }
  out->clear();
  out->reserve(std::min(str.length(), kMaxOutputSize));

  size_t output_size = 0;
  for (const char c : str) {
    uint8_t byte = static_cast<uint8_t>(c);
    if (skipped_bytes_[byte]) continue;

    if (++output_size > kMaxOutputSize) {
      return internal::UpdateError(error, kExceededTranslateOutputSize);
    }

    out->append(1, mapping_[byte]);
  }

  return true;
}

bool TranslateUtf8(absl::string_view str, absl::string_view source_characters,
                   absl::string_view target_characters, std::string* out,
                   absl::Status* error) {
  Utf8Translator translator;
  if (!translator.Initialize(source_characters, target_characters, error)) {
    return false;
  }
  return translator.Translate(str, out, error);
}

bool TranslateBytes(absl::string_view str, absl::string_view source_bytes,
                    absl::string_view target_bytes, std::string* out,
                    absl::Status* error) {
  BytesTranslator translator;
  if (!translator.Initialize(source_bytes, target_bytes, error)) {
    return false;
  }
  return translator.Translate(str, out, error);
}

// Helper function and constants for String/Bytes conversion functions
namespace {

void ThreeBytesToEightBase8Digits(const unsigned char* in_bytes,
                                         char* out) {
  // It's easier to just hard code this.
  // The conversion is based on the following picture of the division of a
  // 24-bit block into 8 3-byte words:
  //
  //       3   3  2   1  3   3  1  2   3   3
  //     [:::|:::|::][:|:::|:::|:][::|:::|:::]
  //
  //
  out[0] = (in_bytes[0] >> 5) + '0';
  out[1] = ((in_bytes[0] & 0x1C) >> 2) + '0';
  out[2] = ((in_bytes[0] & 0x03) << 1 | in_bytes[1] >> 7) + '0';
  out[3] = ((in_bytes[1] & 0x70) >> 4) + '0';
  out[4] = ((in_bytes[1] & 0x0E) >> 1) + '0';
  out[5] = ((in_bytes[1] & 0x01) << 2 | in_bytes[2] >> 6) + '0';
  out[6] = ((in_bytes[2] & 0x38) >> 3) + '0';
  out[7] = (in_bytes[2] & 0x07) + '0';
}

// Mapping from number N of Base8 escaped digits (0 through 7) to number M
// of unescaped bytes (that would generate N escaped digits).
// When M = 0,1,2, N = 0,3,6 respectively. When N is in [1,2] and [4,5], we
// assume the escaped bytes has omitted leading '0' digits to make N become 3
// and 6, so M would be 1 and 2 respectively. When N is 7, assuming the escaped
// bytes has omitted 1 leading '0', M would be 3 to generate these 8 escaped
// bytes.
constexpr int kBase8NumUnescapedBytes[] = {0, 1, 1, 1, 2, 2, 2, 3};

void EightBase8DigitsToThreeBytes(const char* in, unsigned char* bytes_out) {
  // It's easier to just hard code this.
  // The conversion is based on the following picture of the division of a
  // 3-byte words into 8 bytes:
  //
  //       3   3  2   1  3   3  1  2   3   3
  //     [:::|:::|::][:|:::|:::|:][::|:::|:::]
  //       0   1   2     3   4     5   6   7
  //
  const unsigned char* inval = reinterpret_cast<const unsigned char*>(in);
  for (int i = 0; i < 8; ++i) {
    ABSL_DCHECK_GE(inval[i], '0') << "Byte " << i << ", input = " << in;
    ABSL_DCHECK_LE(inval[i], '7') << "Byte " << i << ", input = " << in;
  }

  bytes_out[2] =
      (inval[7] - '0') | ((inval[6] - '0') << 3) | ((inval[5] - '0') << 6);
  bytes_out[1] = ((inval[5] - '0') >> 2) | ((inval[4] - '0') << 1) |
                 ((inval[3] - '0') << 4) | ((inval[2] - '0') << 7);
  bytes_out[0] = ((inval[2] - '0') >> 1) | ((inval[1] - '0') << 2) |
                 ((inval[0] - '0') << 5);
}
}  // namespace

// Converts a sequence of bytes into base8-encoded string.
bool ToBase8(absl::string_view str, std::string* out, absl::Status* error) {
  size_t src_size = str.size();
  size_t output_size = src_size * 8 / 3 + (src_size * 8 % 3 != 0);
  // make sure we didn't overflow
  if (output_size < src_size) {
    return internal::UpdateError(
        error, "Failed to calculate Base8 escaped string length");
  }
  out->resize(output_size);

  const unsigned char* src = reinterpret_cast<const unsigned char*>(str.data());
  char* dest = const_cast<char*>(out->c_str());

  if (src_size == 0) return true;

  char* cur_dest = dest + output_size;
  const unsigned char* cur_src = src + src_size;

  while (src_size >= 3) {
    cur_dest -= 8;
    cur_src -= 3;
    ThreeBytesToEightBase8Digits(cur_src, cur_dest);
    src_size -= 3;
  }

  if (src_size > 0) {
    // Copy the remaining part of source string (first < 3 bytes) to last_chunk,
    // left padded with \0, then convert to base8 digits and put at the
    // beginning of the dest buffer
    unsigned char last_chunk[3] = {'\0'};
    memcpy(last_chunk + 3 - src_size, src, src_size);

    char escaped_bytes[8];
    ThreeBytesToEightBase8Digits(last_chunk, escaped_bytes);
    size_t num_escaped = src_size * 8 / 3 + (src_size * 8 % 3 != 0);
    memcpy(dest, escaped_bytes + 8 - num_escaped, num_escaped);
  }

  return true;
}

// Converts a base8-encoded string into bytes.
bool FromBase8(absl::string_view str, std::string* out, absl::Status* error) {
  size_t src_size = str.size();
  const size_t output_size =
      (src_size / 8) * 3 + kBase8NumUnescapedBytes[src_size % 8];
  out->resize(output_size);

  const char* src = str.data();
  char* dest = &(*out)[0];

  if (src_size == 0) return true;

  for (size_t i = 0; i < src_size; ++i) {
    if (!(src[i] >= '0' && src[i] <= '7'))
      return internal::UpdateError(error,
                                   "Failed to decode invalid base8 string");
  }

  char* cur_dest = dest + output_size;
  const char* cur_src = src + src_size;

  while (src_size >= 8) {
    cur_dest -= 3;
    cur_src -= 8;
    EightBase8DigitsToThreeBytes(cur_src,
                                 reinterpret_cast<unsigned char*>(cur_dest));
    src_size -= 8;
  }

  if (src_size > 0) {
    // Copy the remaining part of source string (first < 8 digits) to
    // escaped_bytes buffer, left padded with '0', then convert to bytes
    char escaped_bytes[8];
    for (int i = 0; i < 8 - src_size; i++) {
      escaped_bytes[i] = '0';
    }
    unsigned char unescaped_bytes[3];
    const int num_unescaped = kBase8NumUnescapedBytes[src_size];

    memcpy(escaped_bytes + 8 - src_size, src, src_size);

    EightBase8DigitsToThreeBytes(escaped_bytes, unescaped_bytes);
    memcpy(dest, unescaped_bytes + 3 - num_unescaped, num_unescaped);
  }

  return true;
}

// Converts a sequence of bytes into base2-encoded string.
bool ToBase2(absl::string_view str, std::string* out, absl::Status* error) {
  size_t output_size = 8 * str.size();
  // make sure we didn't overflow
  if (output_size < str.size()) {
    return internal::UpdateError(
        error, "Failed to calculate Base2 escaped string length");
  }
  out->resize(output_size);

  const unsigned char* cur_src =
      reinterpret_cast<const unsigned char*>(str.data());
  char* cur_dest = &(*out)[0];

  for (size_t i = 0; i < str.size(); ++i) {
    unsigned char temp_cur_src_byte = *cur_src;
    for (int j = 7; j >= 0; --j) {
      cur_dest[j] = (temp_cur_src_byte & 1) + '0';
      temp_cur_src_byte >>= 1;
    }
    cur_src++;
    cur_dest += 8;
  }
  return true;
}

// Converts a base2-encoded string into bytes.
bool FromBase2(absl::string_view str, std::string* out, absl::Status* error) {
  size_t required_output_size = str.size() / 8;
  if (str.size() % 8) {
    required_output_size += 1;
  }
  out->resize(required_output_size);

  size_t src_size = str.size();

  char* cur_dest = &(*out)[0] + required_output_size;
  const unsigned char* cur_src =
      reinterpret_cast<const unsigned char*>(str.data()) + src_size;

  while (src_size > 0) {
    size_t num_src_chars_to_add = (src_size < 8) ? src_size : 8;

    cur_dest--;
    cur_src -= num_src_chars_to_add;

    *cur_dest = 0;

    for (int i = 0; i < num_src_chars_to_add; ++i) {
      if (!(cur_src[i] == '0' || cur_src[i] == '1')) {
        return internal::UpdateError(
            error, absl::StrFormat("Failed to decode invalid base2 string due "
                                   "to character '%c' at offset %d",
                                   cur_src[i], i));
        return false;
      }

      *cur_dest |= ((cur_src[i] - '0') << (num_src_chars_to_add - i - 1));
    }

    src_size -= num_src_chars_to_add;
  }
  return true;
}

// Checks the characters in the input str for ASCII encoding and then copies
// to the destination if all characters are valid.
bool ASCIICheckAndCopy(absl::string_view str, std::string* out,
                       absl::Status* error) {
  for (unsigned char c : str) {
    if (!absl::ascii_isascii(c)) {
      return internal::UpdateError(error,
                                   "Failed to decode invalid ASCII string");
    }
  }
  *out = str;
  return true;
}

// Checks the bytes in the input str for UTF8 encoding and then copies
// to the destination if all bytes are valid.
bool UTF8CheckAndCopy(absl::string_view str, std::string* out,
                      absl::Status* error) {
  if (!IsWellFormedUTF8(str)) {
    return internal::UpdateError(error, kBadUtf8);
  }

  *out = str;
  return true;
}

bool ToBase64m(absl::string_view str, std::string* out, absl::Status* error) {
  std::string base64;
  if (!ToBase64(str, &base64, error)) {
    return false;
  }

  if (base64.size() <= kMaxLineLengthBase64M) {
    *out = std::move(base64);
    return true;
  }

  size_t new_line_cnt = (base64.size() - 1) / kMaxLineLengthBase64M;

  out->clear();
  size_t required_size = base64.size() + new_line_cnt;
  out->reserve(required_size);

  // Copies <base64> to <out> every <kMaxLineLengthBase64M>
  // characters and adds newline characters ('\n') between lines.
  const char* src_ptr = base64.data();
  const char* src_end_ptr = base64.data() + base64.size();

  while (out->size() < required_size) {
    size_t copy_length = std::min(kMaxLineLengthBase64M,
                                  static_cast<size_t>(src_end_ptr - src_ptr));

    bool last_line = src_ptr + copy_length >= src_end_ptr;

    absl::StrAppend(out, absl::string_view(src_ptr, copy_length),
                    last_line ? "" : "\n");

    src_ptr += copy_length;
  }

  return true;
}

const ConversionFuncMap& GetConversionFuncMap() {
  static const absl::NoDestructor<ConversionFuncMap> func_map([] {
    ConversionFuncMap m;
    m.insert({{"base2", {ToBase2, FromBase2}},
              {"base8", {ToBase8, FromBase8}},
              {"base16", {ToHex, FromHex}},
              {"hex", {ToHex, FromHex}},
              {"base64", {ToBase64, FromBase64}},
              {"base64m", {ToBase64m, FromBase64}},
              {"ascii", {ASCIICheckAndCopy, ASCIICheckAndCopy}},
              {"utf8", {UTF8CheckAndCopy, UTF8CheckAndCopy}},
              {"utf-8", {UTF8CheckAndCopy, UTF8CheckAndCopy}}});
    return m;
  }());
  return *func_map;
}

absl::Status BytesToString(absl::string_view str, absl::string_view format,
                           std::string* out) {
  absl::Status status;
  const std::string lower_format = absl::AsciiStrToLower(format);
  auto result = GetConversionFuncMap().find(lower_format);
  if (result != GetConversionFuncMap().end()) {
    (result->second.first)(str, out, &status);
    return status;
  }
  internal::UpdateError(&status, absl::Substitute(kInvalidFormat, format));
  return status;
}

absl::Status StringToBytes(absl::string_view str, absl::string_view format,
                           std::string* out) {
  absl::Status status;
  const std::string lower_format = absl::AsciiStrToLower(format);
  auto result = GetConversionFuncMap().find(lower_format);
  if (result != GetConversionFuncMap().end()) {
    (result->second.second)(str, out, &status);
    return status;
  }
  internal::UpdateError(&status, absl::Substitute(kInvalidFormat, format));
  return status;
}

absl::Status ValidateFormat(absl::string_view format) {
  const std::string lower_format = absl::AsciiStrToLower(format);
  if (!GetConversionFuncMap().contains(lower_format)) {
    return absl::Status(absl::StatusCode::kOutOfRange,
                        absl::Substitute(kInvalidFormat, format));
  }
  return absl::OkStatus();
}

absl::Status SplitSubstrWithCount(absl::string_view text,
                                  absl::string_view delimiter,
                                  int64_t start_index, int64_t count,
                                  std::string* out) {
  if (delimiter.empty()) {
    return absl::Status(absl::StatusCode::kOutOfRange,
                        "Delimiter of SPLIT_SUBSTR function must be non-empty");
  }

  if (!IsWellFormedUTF8(text)) {
    return absl::Status(
        absl::StatusCode::kOutOfRange,
        "Text input to SPLIT_SUBSTR function must be valid UTF-8");
  }

  if (!IsWellFormedUTF8(delimiter)) {
    return absl::Status(
        absl::StatusCode::kOutOfRange,
        "Delimiter of SPLIT_SUBSTR function must be valid UTF-8");
  }

  if (count < 0) {
    return absl::Status(absl::StatusCode::kOutOfRange,
                        "Count of SPLIT_SUBSTR function must be non-negative");
  }

  if (count == 0 || text.empty()) {
    *out = "";
    return absl::OkStatus();
  }

  std::vector<std::string> splits = absl::StrSplit(text, delimiter);
  int64_t num_splits = splits.size();

  // Normalize positive start_index to 0-based indexing.
  if (start_index >= 1) {
    start_index -= 1;
  }

  // Normalize negative start_index.
  if (start_index < 0 && start_index >= -num_splits) {
    start_index = num_splits + start_index;
  }

  // Move start_index to first split if it is in the left of the first split.
  if (start_index < 0) {
    start_index = 0;
  }

  // Return empty string if start_index is in the right of the last split.
  if (start_index >= num_splits) {
    *out = "";
    return absl::OkStatus();
  }

  // Normalize count.
  count = std::min(count, num_splits - start_index);
  *out = absl::StrJoin(splits.begin() + start_index,
                       splits.begin() + start_index + count, delimiter);
  return absl::OkStatus();
}

absl::Status SplitSubstr(absl::string_view text, absl::string_view delimiter,
                         int64_t start_index, std::string* out) {
  return SplitSubstrWithCount(text, delimiter, start_index,
                              std::numeric_limits<int64_t>::max(), out);
}

}  // namespace functions
}  // namespace googlesql
