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

//
// This file provides base implementation for GoogleSQL string functions.
// (broken link)
//
// All functions have signature that looks as follows:
//
//  bool <Function>{Utf8|Bytes}(arguments, <OutType>* out, absl::Status* error)
//
// absl::string_view is used to pass arguments that have type STRING or BYTES.
// Functions with "Utf8" suffix assume that inputs are encoded as UTF-8.
//
// If computation is successful these functions save the result in *out and
// return true. If an error occurs they update *error and return false.
//
// All "*Utf8" string parameters must be well-formed (aka structurally
// valid) UTF-8 strings. Whenever it's safe to do so these functions will
// skip UTF-8 validity checks. They will never crash if a string is not valid
// but they may return invalid results.
//
// Some functions that return strings do so by taking a pointer to a
// absl::string_view. For those functions output is a substring of an input
// string and it will be valid as long as input strings stay valid.
//

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_STRING_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_STRING_H_

#include <bitset>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/functions/normalize_mode.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "unicode/casemap.h"
#include "unicode/uniset.h"
#include "unicode/utypes.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// INSTR(STRING, STRING, INT64, INT64) -> INT64
// Returns the position of the <occurrence>-th <substr> in <str>, starting the
// search forward from <pos> if negative and backward if <pos> is negative, with
// -1 referencing the last character.
// Both <pos> and the result are indexed from 1 and count one UTF8 character
// (which can span multiple bytes) as one unit.
bool StrPosOccurrenceUtf8(absl::string_view str, absl::string_view substr,
                          int64_t pos, int64_t occurrence, int64_t* out,
                          absl::Status* error);

// INSTR(BYTES, BYTES, INT64, INT64) -> INT64
// Returns the position of the <occurrence>-th <substr> in <str>, starting the
// search forward from <pos> if negative and backward if <pos> is negative, with
// -1 referencing the last character.
// Both <pos> and the result are indexed from 1 and count one byte as one unit.
bool StrPosOccurrenceBytes(absl::string_view str, absl::string_view substr,
                           int64_t pos, int64_t occurrence, int64_t* out,
                           absl::Status* error);

// STRPOS(STRING, STRING) -> INT64
inline bool StrposUtf8(absl::string_view str, absl::string_view substr,
                       int64_t* out, absl::Status* error) {
  return StrPosOccurrenceUtf8(str, substr, /*pos=*/1, /*occurrence=*/1, out,
                              error);
}

// STRPOS(BYTES, BYTES) -> INT64
inline bool StrposBytes(absl::string_view str, absl::string_view substr,
                        int64_t* out, absl::Status* error) {
  return StrPosOccurrenceBytes(str, substr, /*pos=*/1, /*occurrence=*/1, out,
                               error);
}

// LOCATE(STRING, STRING) -> INT64
inline bool LocateUtf8(absl::string_view substr, absl::string_view str,
                       int64_t* out, absl::Status* error) {
  return StrPosOccurrenceUtf8(str, substr, /*pos=*/1, /*occurrence=*/1, out,
                              error);
}

// LOCATE(BYTES, BYTES) -> INT64
inline bool LocateBytes(absl::string_view substr, absl::string_view str,
                        int64_t* out, absl::Status* error) {
  return StrPosOccurrenceBytes(str, substr, /*pos=*/1, /*occurrence=*/1, out,
                               error);
}

// LENGTH(STRING) -> INT64
bool LengthUtf8(absl::string_view str, int64_t* out, absl::Status* error);

// LENGTH(BYTES) -> INT64
bool LengthBytes(absl::string_view str, int64_t* out, absl::Status* error);

// STARTS_WITH(STRING, STRING) -> BOOL
bool StartsWithUtf8(absl::string_view str, absl::string_view substr, bool* out,
                    absl::Status* error);
// STARTS_WITH(BYTES, BYTES) -> BOOL
bool StartsWithBytes(absl::string_view str, absl::string_view substr, bool* out,
                     absl::Status* error);

// ENDS_WITH(STRING, STRING) -> BOOL
bool EndsWithUtf8(absl::string_view str, absl::string_view substr, bool* out,
                  absl::Status* error);
// ENDS_WITH(BYTES, BYTES) -> BOOL
bool EndsWithBytes(absl::string_view str, absl::string_view substr, bool* out,
                   absl::Status* error);

// This class allows for a more efficient implementation of TRIM(), LTRIM()
// and RTRIM() functions when the second argument is constant, by only
// processing the list of characters to trim once.
class Utf8Trimmer {
 public:
  Utf8Trimmer() = default;
  Utf8Trimmer(const Utf8Trimmer&) = delete;
  Utf8Trimmer& operator=(const Utf8Trimmer&) = delete;

  // Initializes this trimmer using "to_trim" as a list of characters to trim.
  // Returns false and updates *error if to_trim is not a valid UTF-8 string.
  bool Initialize(absl::string_view to_trim, absl::Status* error);

  // Trims UTF-8 characters from the left.
  bool TrimLeft(absl::string_view str, absl::string_view* out,
                absl::Status* error) const;

  // Trims UTF-8 characters from the right.
  bool TrimRight(absl::string_view str, absl::string_view* out,
                 absl::Status* error) const;

  // Trims UTF-8 characters from both left and right.
  bool Trim(absl::string_view str, absl::string_view* out,
            absl::Status* error) const;

 private:
  std::unique_ptr<icu::UnicodeSet> unicode_set_;
  // We use icu::UnicodeSet::spanUtf8, which automatically 'fixes' ill formed
  // spans with the replacement character before deciding to trim or not, which
  // is not the behavior we want. It only matters if the user has explicitly
  // specified the replacement character in the trim set.  In that case, we
  // first validate the input string (rejecting it as an error if it is
  // ill-formed).  We do this conditionally, as it is more expensive, since
  // it requires two passes over the input.
  bool has_explicit_replacement_char_ = false;
};

// This class allows for a more efficient implementation of TRIM(), LTRIM()
// and RTRIM() functions when the second argument is constant, by only
// processing the list of bytes to trim once.
class BytesTrimmer {
 public:
  BytesTrimmer() = default;
  BytesTrimmer(const BytesTrimmer&) = delete;
  BytesTrimmer& operator=(const BytesTrimmer&) = delete;

  // Initializes this trimmer using "to_trim" as a list of bytes to trim.
  void Initialize(absl::string_view to_trim);

  // Trims bytes from the left.
  absl::string_view TrimLeft(absl::string_view str);

  // Trims bytes from the right.
  absl::string_view TrimRight(absl::string_view str);

  // Trims bytes from both left and right.
  absl::string_view Trim(absl::string_view str);

 private:
  bool bytes_to_trim_[256] = {};
};

// TRIM(STRING) -> STRING
bool TrimSpacesUtf8(absl::string_view str, absl::string_view* out,
                    absl::Status* error);
// LTRIM(STRING) -> STRING
bool LeftTrimSpacesUtf8(absl::string_view str, absl::string_view* out,
                        absl::Status* error);
// RTRIM(STRING) -> STRING
bool RightTrimSpacesUtf8(absl::string_view str, absl::string_view* out,
                         absl::Status* error);

// TRIM(STRING, STRING) -> STRING
bool TrimUtf8(absl::string_view str, absl::string_view chars,
              absl::string_view* out, absl::Status* error);
// LTRIM(STRING, STRING) -> STRING
bool LeftTrimUtf8(absl::string_view str, absl::string_view chars,
                  absl::string_view* out, absl::Status* error);
// RTRIM(STRING, STRING) -> STRING
bool RightTrimUtf8(absl::string_view str, absl::string_view chars,
                   absl::string_view* out, absl::Status* error);

// TRIM(BYTES, BYTES) -> BYTES
bool TrimBytes(absl::string_view str, absl::string_view chars,
               absl::string_view* out, absl::Status* error);
// LTRIM(BYTES, BYTES) -> BYTES
bool LeftTrimBytes(absl::string_view str, absl::string_view chars,
                   absl::string_view* out, absl::Status* error);
// RTRIM(BYTES, BYTES) -> BYTES
bool RightTrimBytes(absl::string_view str, absl::string_view chars,
                    absl::string_view* out, absl::Status* error);

// LEFT(STRING, INT64) -> STRING
bool LeftUtf8(absl::string_view str, int64_t length, absl::string_view* out,
              absl::Status* error);
// LEFT(BYTES, INT64) -> BYTES
bool LeftBytes(absl::string_view str, int64_t length, absl::string_view* out,
               absl::Status* error);
// RIGHT(STRING, INT64) -> STRING
bool RightUtf8(absl::string_view str, int64_t length, absl::string_view* out,
               absl::Status* error);
// RIGHT(BYTES, INT64) -> BYTES
bool RightBytes(absl::string_view str, int64_t length, absl::string_view* out,
                absl::Status* error);

// SUBSTR(STRING, INT64) -> STRING
bool SubstrUtf8(absl::string_view str, int64_t pos, absl::string_view* out,
                absl::Status* error);
// SUBSTR(STRING, INT64, INT64) -> STRING
bool SubstrWithLengthUtf8(absl::string_view str, int64_t pos, int64_t length,
                          absl::string_view* out, absl::Status* error);
// SUBSTR(BYTES, INT64) -> BYTES
bool SubstrBytes(absl::string_view str, int64_t pos, absl::string_view* out,
                 absl::Status* error);
// SUBSTR(BYTES, INT64, INT64) -> BYTES
bool SubstrWithLengthBytes(absl::string_view str, int64_t pos, int64_t length,
                           absl::string_view* out, absl::Status* error);

// UPPER(STRING) -> STRING
bool UpperUtf8(absl::string_view str, std::string* out, absl::Status* error);

// LOWER(STRING) -> STRING
bool LowerUtf8(absl::string_view str, std::string* out, absl::Status* error);

// UPPER(BYTES) -> BYTES
bool UpperBytes(absl::string_view str, std::string* out, absl::Status* error);
// LOWER(BYTES) -> BYTES
bool LowerBytes(absl::string_view str, std::string* out, absl::Status* error);

// This class allows for a more efficient implementation of InitialCapitalize
// and InitialCapitalizeDefault by only processing the set of delimiters once.
class Utf8Capitalizer {
 public:
  Utf8Capitalizer() = default;
  Utf8Capitalizer(const Utf8Capitalizer&) = delete;
  Utf8Capitalizer& operator=(const Utf8Capitalizer&) = delete;

  // Initializes this capitalizer using <delimiters>. Returns false and updates
  // <error> if <delimiters> is not a valid UTF-8 string.
  bool Initialize(absl::string_view delimiters, absl::Status* error);

  // Initializes this capitalizer using the default set of delimiters. Returns
  // false and updates <error> if delimiters is not a valid UTF-8 string(this
  // should never happen).
  bool InitializeDefault(absl::Status* error);

  // Canonical implementation of INITCAP. Capitalizes the first chracter in
  // each word in <str> and writes it to <out>. Words are delmited by the set
  // specified during initialization.
  // Returns false if <str> is not a valid UTF-8 and updates <error>.
  bool Capitalize(absl::string_view str, std::string* out, absl::Status* error);

 private:
  // Stores the set of delimiters.
  std::unique_ptr<icu::UnicodeSet> unicode_set_;
};

// INITCAP(STRING, STRING) -> STRING
bool InitialCapitalize(absl::string_view str, absl::string_view delimiters,
                       std::string* out, absl::Status* error);
// INITCAP(STRING) -> STRING
bool InitialCapitalizeDefault(absl::string_view str, std::string* out,
                              absl::Status* error);

// REPLACE(STRING, STRING, STRING) -> STRING
bool ReplaceUtf8(absl::string_view str, absl::string_view oldsub,
                 absl::string_view newsub, std::string* out,
                 absl::Status* error);

// REPLACE(BYTES, BYTES, BYTES) -> BYTES
bool ReplaceBytes(absl::string_view str, absl::string_view oldsub,
                  absl::string_view newsub, std::string* out,
                  absl::Status* error);

// LPAD(BYTES, INT64, BYTES)
bool LeftPadBytes(absl::string_view input_str, int64_t output_size_bytes,
                  absl::string_view pattern, std::string* out,
                  absl::Status* error);

// LPAD(BYTES, INT64)
bool LeftPadBytesDefault(absl::string_view input_str, int64_t output_size_bytes,
                         std::string* out, absl::Status* error);

// RPAD(BYTES, INT64, BYTES)
bool RightPadBytes(absl::string_view input_str, int64_t output_size_bytes,
                   absl::string_view pattern, std::string* out,
                   absl::Status* error);

// RPAD(BYTES, INT64)
bool RightPadBytesDefault(absl::string_view input_str,
                          int64_t output_size_bytes, std::string* out,
                          absl::Status* error);

// LPAD(STRING, INT64, STRING)
bool LeftPadUtf8(absl::string_view input_str, int64_t output_size_chars,
                 absl::string_view pattern, std::string* out,
                 absl::Status* error);

// LPAD(STRING, INT64)
bool LeftPadUtf8Default(absl::string_view input_str, int64_t output_size_chars,
                        std::string* out, absl::Status* error);

// RPAD(STRING, INT64, STRING)
bool RightPadUtf8(absl::string_view input_str, int64_t output_size_chars,
                  absl::string_view pattern, std::string* out,
                  absl::Status* error);

// RPAD(STRING, INT64)
bool RightPadUtf8Default(absl::string_view input_str, int64_t output_size_chars,
                         std::string* out, absl::Status* error);

// REPEAT(BYTES, INT64)
// REPEAT(STRING, INT64)
bool Repeat(absl::string_view input_str, int64_t repeat_count, std::string* out,
            absl::Status* error);

// REVERSE(BYTES)
bool ReverseBytes(absl::string_view input, std::string* out,
                  absl::Status* error);

// REVERSE(STRING)
bool ReverseUtf8(absl::string_view input, std::string* out,
                 absl::Status* error);

// SPLIT(STRING, STRING) -> ARRAY<STRING>
bool SplitUtf8(absl::string_view str, absl::string_view delimiter,
               std::vector<std::string>* out, absl::Status* error);
bool SplitUtf8(absl::string_view str, absl::string_view delimiter,
               std::vector<absl::string_view>* out, absl::Status* error);

// SPLIT(BYTES, BYTES) -> ARRAY<BYTES>
bool SplitBytes(absl::string_view str, absl::string_view delimiter,
                std::vector<std::string>* out, absl::Status* error);
bool SplitBytes(absl::string_view str, absl::string_view delimiter,
                std::vector<absl::string_view>* out, absl::Status* error);

// SAFE_CONVERT_BYTES_TO_STRING(BYTES) -> STRING
bool SafeConvertBytes(absl::string_view str, std::string* out,
                      absl::Status* error);

// NORMALIZE(STRING, NORMALIZE_MODE) -> STRING
// NORMALIZE_AND_CASEFOLD(STRING, NORMALIZE_MODE) -> STRING
// Returns false if an invalid normalize <mode> is given or there is an error
// during normalization.
// Invalid UTF8 chars will replaced by the U+FFFD before normalization.
bool Normalize(absl::string_view str, NormalizeMode mode, bool is_casefold,
               std::string* out, absl::Status* error);

// Converts from bytes to a base64-encoded string.
bool ToBase64(absl::string_view str, std::string* out, absl::Status* error);

// Converts from a base64-encoded string to bytes.
bool FromBase64(absl::string_view str, std::string* out, absl::Status* error);

// Converts from bytes to a hexadecimal-encoded string.
// Each byte is encoded based on its character value as two hex characters, such
// as 0a, 48, or f7.
bool ToHex(absl::string_view str, std::string* out, absl::Status* error);

// Converts from a hexadecimal-encoded string to bytes.
// Raises an error if one of the characters in the string is not in the range of
// 0-9, a-f, A-F. Every two hexadecimal characters are converted to a single
// byte. If the string has an odd number of characters, the left-most
// character is interpreted as if it had a 0 to its left.
bool FromHex(absl::string_view str, std::string* out, absl::Status* error);

// Converts from the first character of a string to ASCII code in [0, 127].
// Returns an error if the input is not a structurally valid ASCII string.
bool FirstCharOfStringToASCII(absl::string_view str, int64_t* out,
                              absl::Status* error);

// Converts from the first byte of a bytes to extended ASCII values in the
// range [0, 255]. Always return true.
bool FirstByteOfBytesToASCII(absl::string_view str, int64_t* out,
                             absl::Status* error);

// Converts from the first Unicode char in a string to codepoint. Returns an
// error if the first character of input is not a structurally valid UTF8 char.
bool FirstCharToCodePoint(absl::string_view str, int64_t* out,
                          absl::Status* error);

// Converts from a codepoint to a UTF8 string. Returns an error if the input
// is not a valid UTF8 codepoint.
bool CodePointToString(int64_t codepoint, std::string* out,
                       absl::Status* error);

// Converts from a UTF8 string to codepoints. Returns an error if the input is
// not a structurally valid UTF8 string.
bool StringToCodePoints(absl::string_view str, std::vector<int64_t>* out,
                        absl::Status* error);

// Converts from bytes to extended ASCII values in the range [0, 255]. Always
// returns success.
bool BytesToCodePoints(absl::string_view str, std::vector<int64_t>* out,
                       absl::Status* error);

// Converts from codepoints to a UTF8 string. Returns an error if any of the
// elements in 'codepoints' is not a valid UTF8 codepoint.
bool CodePointsToString(absl::Span<const int64_t> codepoints, std::string* out,
                        absl::Status* error);

// Converts from extended ASCII values to bytes. Returns an error if the input
// values are not in the range [0, 255].
bool CodePointsToBytes(absl::Span<const int64_t> codepoints, std::string* out,
                       absl::Status* error);

// Represents a potential rewrite of a LIKE pattern, e.g. "s LIKE pattern" to
// "STRING_FUNCTION(s, modified_pattern)" for some different function and
// potentially-modified pattern.
enum class LikeRewriteType {
  // No rewrite found for the LIKE pattern.
  kNoRewrite = 0,
  // s LIKE pattern can be rewritten to ENDS_WITH(s, modified_pattern).
  kEndsWith = 1,
  // s LIKE pattern can be rewritten to STARTS_WITH(s, modified_pattern).
  kStartsWith = 2,
  // s LIKE pattern can be rewritten to STRPOS(s, modified_pattern) > 0, or an
  // equivalent containment check.
  kContains = 3,
  // s LIKE pattern can be rewritten to s = modified_pattern.
  kEquals = 4,
  // s LIKE pattern can be rewritten to s IS NOT NULL.
  kNotNull = 5,
};

// Given a LIKE pattern, such as "%foo", returns whether the LIKE operator can
// expressed as a different function such as ENDS_WITH. is_string indicates
// whether the pattern is a string. If false, the pattern is interpreted as
// bytes.
//
// Note that this function only supports substring matches, not more generalized
// patterns such as "foo%bar".
//
// * Given "%foo", returns kEndsWith, and sets substring to "foo".
// * Given "foo%", returns kStartsWith, and sets substring to "foo".
// * Given "%foo%", returns kContains, and sets substring to "foo".
// * Given "foo", returns kEquals, and sets substring to "foo".
// * Given "%" or "%%", returns kNotNull, and sets substring to "".
// * Given any pattern that contains \ or _ or has % in a place other than the
//   start or end of the pattern, returns kNoRewrite and does not set substring.
// * Returns kNoRewrite and does not set substring if is_string is true and the
//   pattern is not a structurally valid UTF-8 string.
LikeRewriteType GetRewriteForLikePattern(bool is_string,
                                         absl::string_view pattern,
                                         absl::string_view* substring);

// SOUNDEX(STRING) -> STRING
// Converts `str` into a Soundex (https://en.wikipedia.org/wiki/Soundex)
bool Soundex(absl::string_view str, std::string* out, absl::Status* error);

// Those classes allows for a more efficient implementation of TRANSLATE()
// when the source and target character sets are constant, by only processing
// the list of characters to be translated once.
class Utf8Translator {
 public:
  Utf8Translator() = default;
  Utf8Translator(const Utf8Translator&) = delete;
  Utf8Translator& operator=(const Utf8Translator&) = delete;

  // Initializes this translator with a mapping from <source_characters> to
  // <target_characters>.
  // Returns false and updates *error if there are no-UTF8 characters in either
  // <source_characters> or <target_characters>, or if a character is repeated
  // in <source_characters>.
  bool Initialize(absl::string_view source_characters,
                  absl::string_view target_characters, absl::Status* error);

  // Translate <str> using the mapping.
  bool Translate(absl::string_view str, std::string* out,
                 absl::Status* error) const;

 private:
  // Mapping from UTF8 character to the string characters to be replaced with.
  absl::flat_hash_map<UChar32, absl::string_view> mapping_;
};

class BytesTranslator {
 public:
  BytesTranslator() = default;
  BytesTranslator(const BytesTranslator&) = delete;
  BytesTranslator& operator=(const BytesTranslator&) = delete;

  // Initializes this translator with a mapping from <source_bytes> to
  // <target_bytes>.
  // Returns false and updates *error if a byte is repeated in <source_bytes>.
  bool Initialize(absl::string_view source_bytes,
                  absl::string_view target_bytes, absl::Status* error);

  // Translate <str> using the mapping.
  bool Translate(absl::string_view str, std::string* out,
                 absl::Status* error) const;

 private:
  std::bitset<256> skipped_bytes_;
  char mapping_[256];
  bool initialized_ = false;
};

// TRANSLATE(STRING, STRING, STRING) -> STRING
bool TranslateUtf8(absl::string_view str, absl::string_view source_characters,
                   absl::string_view target_characters, std::string* out,
                   absl::Status* error);

// TRANSLATE(BYTES, BYTES, BYTES) -> BYTES
bool TranslateBytes(absl::string_view str, absl::string_view source_bytes,
                    absl::string_view target_bytes, std::string* out,
                    absl::Status* error);

// Converts from bytes to a encoded string with specific encoding format
// (Proposal doc (broken link)).
absl::Status BytesToString(absl::string_view str, absl::string_view format,
                           std::string* out);

// Converts from a encoded string with specific encoding format to bytes
// (Proposal doc (broken link)).
absl::Status StringToBytes(absl::string_view str, absl::string_view format,
                           std::string* out);

// Validates the format string used by BytesToString() and StringToBytes().
absl::Status ValidateFormat(absl::string_view format);

typedef bool (*ConversionFunc)(absl::string_view str, std::string* out,
                               absl::Status* error);

// A map that maps the format string to conversion functions between bytes and
// string for that format. The first element of the value is the
// bytes to string conversion function, while the second element of the value is
// the string to bytes conversion function.
typedef absl::flat_hash_map<std::string,
                            std::pair<ConversionFunc, ConversionFunc>>
    ConversionFuncMap;

const ConversionFuncMap& GetConversionFuncMap();

// Splits the `text` based on specified `delimiter` and returns the substring
// from the `start_index` split to the end of the `text`.
// (Proposal doc (broken link)).
absl::Status SplitSubstr(absl::string_view text, absl::string_view delimiter,
                         int64_t start_index, std::string* out);

// Splits the `text` based on specified `delimiter` and returns the substring
// from the `start_index` split, combining `count` number of splits.
// (Proposal doc (broken link)).
absl::Status SplitSubstrWithCount(absl::string_view text,
                                  absl::string_view delimiter,
                                  int64_t start_index, int64_t count,
                                  std::string* out);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_STRING_H_
