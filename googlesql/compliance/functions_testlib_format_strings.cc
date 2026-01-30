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

// The file functions_testlib.cc has been split into multiple files prefixed
// with "functions_testlib_" because an optimized compile with ASAN of the
// original single file timed out at 900 seconds.

#include <string>
#include <vector>

#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsFormatStrings() {
  const std::string x_zero_y("x\0y", 3);

  return std::vector<FunctionTestCall>({
      {"format", {NullString()}, NullString()},
      {"format", {NullString(), 1, "abc"}, NullString()},
      {"format", {""}, ""},
      {"format", {"abc"}, "abc"},
      {"format", {"abc%%de%%f"}, "abc%de%f"},
      {"format", {"abc%s%s%sdef", "123", "", "xxx"}, "abc123xxxdef"},
      {"format", {"abc%s%s%sdef", "123", NullString(), "xxx"}, NullString()},

      // Formatting strings.
      {"format", {"%s", "abc"}, "abc"},
      {"format", {"%'s", "abc"}, "abc"},
      {"format", {"'%s'", "abc"}, "'abc'"},
      {"format", {"%3s", "abc"}, "abc"},
      {"format", {"%2s", "abc"}, "abc"},
      {"format", {"%-2s", "abc"}, "abc"},
      {"format", {"%5s", "abc"}, "  abc"},
      {"format", {"%-5s", "abc"}, "abc  "},
      {"format", {"%+5s", "abc"}, "  abc"},
      {"format", {"%#5s", "abc"}, "  abc"},
      {"format", {"%05s", "abc"}, "  abc"},
      {"format", {"%05s", "abc"}, "  abc"},
      {"format", {"%0s", "abc"}, "abc"},
      {"format", {"%.5s", "abd"}, "abd"},
      {"format", {"%.3s", "abc"}, "abc"},
      {"format", {"%.2s", "abc"}, "ab"},
      {"format", {"%.1s", "abc"}, "a"},
      {"format", {"%.0s", "abc"}, ""},
      {"format", {"%5.2s", "abc"}, "   ab"},
      {"format", {"%-5.2s", "abc"}, "ab   "},
      {"format", {"%.s", "abc"}, ""},
      {"format", {"%5.s", "abc"}, "     "},

      // Strings with * width/precision arguments.
      {"format", {"%*s", 5, "abc"}, "  abc"},
      {"format", {"%'*s", 5, "abc"}, "  abc"},
      {"format", {"%*s", 2, "abc"}, "abc"},
      {"format", {"%*s", 0, "abc"}, "abc"},
      {"format", {"%*s", -2, "abc"}, "abc"},
      {"format", {"%*s", -5, "abc"}, "abc  "},
      {"format", {"%.*s", 5, "abc"}, "abc"},
      {"format", {"%.*s", 2, "abc"}, "ab"},
      {"format", {"%.*s", 0, "abc"}, ""},
      {"format", {"%.*s", -2, "abc"}, "abc"},
      {"format", {"%.*s", -5, "abc"}, "abc"},
      {"format", {"%*.*s", 5, 2, "abc"}, "   ab"},
      {"format", {"%-*.*s", 5, 2, "abc"}, "ab   "},
      {"format", {"%*.*s", -5, 2, "abc"}, "ab   "},
      {"format", {"%-*.*s", -5, 2, "abc"}, "ab   "},
      {"format", {"%5.*s", 2, "abc"}, "   ab"},
      {"format", {"%*.2s", 5, "abc"}, "   ab"},

      // This input string has a multi byte utf character. The character should
      // only count as one position for width and precision accounting.
      {"format", {"%5s", String("*€*")}, "  *€*"},
      {"format", {"%-5s", String("*€*")}, "*€*  "},
      {"format", {"%.2s", String("*€*")}, "*€"},
      {"format", {"%5.2s", String("*€*")}, "   *€"},
      {"format", {"%-5.2s", String("*€*")}, "*€   "},

      // Fun slicing multi-codepoint graphemes! UTF-8 length, including for
      // FORMAT, is defined on codepoints not graphemes. Each new version of
      // Unicode specifies new ways that codepoint combinations can combine into
      // a single grapheme, so counting graphemes requires a Unicode version
      // number. Maybe we will want to do that someday. It will require thoughts
      // about how to specify unicode version (which is somewhat like locale
      // settings for grouping or radix formats).

      // This emoji is 2 codepoints (PERSON SURFING, SKIN TONE) that occur
      // between the two pipe characters. Depending on what level of Unicode
      // your brower/OS support, it may render as one or two graphemes for you.
      // We should be able to slice the emoji with format precision to drop just
      // the skin tone codepoint.
      {"format", {"%.2s|", String("|🏄🏾|")}, "|🏄|"},
      // This one combines a slice of the woman farmer emoji (we take the first
      // three of four codepoints from farmer [woman, skin tone, ZWJ] each of
      // which is multi-byte) and replace the last codepoint [corn] with the
      // personal computer emoji to get the woman technologist. The complete
      // graphemes may or may not render on your system.
      {"format", {"|%.3s💻|", String("👩🏾‍🌾")}, "|👩🏾‍💻|"},

      // Bytes
      {"format", {"%t", Bytes("")}, ""},
      {"format", {"%t", Bytes("xxx")}, "xxx"},
      {"format", {"%t", Value::NullBytes()}, "NULL"},
      {"format", {"%T", Bytes("")}, "b\"\""},
      {"format", {"%T", Bytes("xxx")}, "b\"xxx\""},
      {"format", {"%T", Value::NullBytes()}, "NULL"},
      {"format", {"%s", Bytes("xxx")}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Value::NullBytes()}, NullString(), OUT_OF_RANGE},

      // For non-printable bytes, we'll just print them escaped, and not
      // try to treat them as UTF-8 or strings at all. Quotes don't get escaped.
      {"format",
       {"%t", Bytes("abc123\x1\x2\xAB\xFF")},
       "abc123\\x01\\x02\\xab\\xff"},
      {"format", {"%t", Bytes("quote'\"quote")}, "quote\\'\\\"quote"},
      {"format",
       {"%T", Bytes("abc123\x1\x2\xAB\xFF")},
       "b\"abc123\\x01\\x02\\xab\\xff\""},
      {"format", {"%T", Bytes("quote'\"quote")}, "b\"quote'\\\"quote\""},
      {"format", {"%t", Bytes("quote\"quote")}, "quote\\\"quote"},
      {"format", {"%T", Bytes("quote\"quote")}, "b'quote\"quote'"},

      // UTF-8 strings in the pattern and in substituted values.
      {"format", {"ъ %s xy", Value::String("ьь")}, "ъ ьь xy"},
      {"format", {"%t", Value::String("ьь")}, "ьь"},
      {"format", {"%T", Value::String("ьь")}, "\"ьь\""},
      {"format", {"%t", Value::Bytes("ьь")}, "\\xd1\\x8c\\xd1\\x8c"},
      {"format", {"%T", Value::Bytes("ьь")}, "b\"\\xd1\\x8c\\xd1\\x8c\""},

      // Invalid UTF-8 characters in a Bytes value.
      {"format", {"%t", Value::Bytes("\xd7")}, "\\xd7"},
      {"format", {"%T", Value::Bytes("\xd7")}, "b\"\\xd7\""},
      // Note: We don't test invalid UTF-8 in a string because that may not
      // be possible in some engines.

      // Strings with embedded zeros.
      {"format", {x_zero_y}, String(x_zero_y)},
      {"format", {"%t", Bytes(x_zero_y)}, "x\\x00y"},
      {"format", {"%T", Bytes(x_zero_y)}, "b\"x\\x00y\""},
      // This one doesn't actually work right because our sprintf implementation
      // uses C strings that cut off at 0.
      // TODO It would be nice to fix this.
      // {"format", {"%s", String(x_zero_y)}, "x\x0y"},
  });
}

}  // namespace googlesql
