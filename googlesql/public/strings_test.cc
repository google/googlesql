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

#include "googlesql/public/strings.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

using testing::Eq;
using testing::HasSubstr;
using absl_testing::StatusIs;

#define EXPECT_SUBSTRING(needle, haystack)                                \
  do {                                                                    \
    const std::string tmp_needle = (needle);                              \
    const std::string tmp_haystack = (haystack);                          \
    EXPECT_TRUE(std::string::npos != tmp_haystack.find(tmp_needle))       \
        << "Couldn't find \"" << tmp_needle << "\" in \"" << tmp_haystack \
        << "\"";                                                          \
  } while (0)

namespace googlesql {

constexpr char kUnicodeNotAllowedInBytes1[] =
    "Unicode escape sequence \\u cannot be used in bytes literals";
constexpr char kUnicodeNotAllowedInBytes2[] =
    "Unicode escape sequence \\U cannot be used in bytes literals";

static void TestIdentifier(absl::string_view orig) {
  const std::string quoted = ToIdentifierLiteral(orig);
  std::string unquoted;
  std::string error_string;
  int error_offset;
  GOOGLESQL_EXPECT_OK(ParseIdentifier(quoted, LanguageOptions(), &unquoted, &error_string,
                            &error_offset))
      << orig << "\nERROR: " << error_string << " (at offset " << error_offset
      << ")";
  EXPECT_EQ(orig, unquoted) << "quoted: " << quoted;
}

static void TestAlwaysQuotedIdentifier(absl::string_view orig) {
  const std::string quoted = ToAlwaysQuotedIdentifierLiteral(orig);
  EXPECT_THAT(quoted, testing::StartsWith("`"));
  EXPECT_THAT(quoted, testing::EndsWith("`"));
  std::string unquoted;
  std::string error_string;
  int error_offset;
  GOOGLESQL_EXPECT_OK(ParseIdentifier(quoted, LanguageOptions(), &unquoted, &error_string,
                            &error_offset))
      << orig << "\nERROR: " << error_string << " (at offset " << error_offset
      << ")";
  EXPECT_EQ(orig, unquoted) << "quoted: " << quoted;
}

// <quoted> takes a string literal of the form '...', r'...', "..." or r"...".
// <unquoted> is the expected parsed form of <quoted>.
static void TestQuotedString(absl::string_view unquoted,
                             absl::string_view quoted) {
  std::string actual_unquoted, error;
  int error_offset;
  GOOGLESQL_EXPECT_OK(ParseStringLiteral(quoted, &actual_unquoted, &error, &error_offset))
      << unquoted << "\nERROR: " << error;
  EXPECT_EQ(unquoted, actual_unquoted) << "quoted: " << quoted;
}

static void TestString(absl::string_view unquoted) {
  TestQuotedString(unquoted, ToStringLiteral(unquoted));
  TestQuotedString(unquoted,
                   absl::StrCat("'''", EscapeString(unquoted), "'''"));
  TestQuotedString(unquoted,
                   absl::StrCat("\"\"\"", EscapeString(unquoted), "\"\"\""));
}

static void TestRawString(absl::string_view unquoted) {
  const std::string quote = (!absl::StrContains(unquoted, "'")) ? "'" : "\"";
  TestQuotedString(unquoted, absl::StrCat("r", quote, unquoted, quote));
  TestQuotedString(unquoted, absl::StrCat("r\"", unquoted, "\""));
  TestQuotedString(unquoted, absl::StrCat("r'''", unquoted, "'''"));
  TestQuotedString(unquoted, absl::StrCat("r\"\"\"", unquoted, "\"\"\""));
}

// <quoted> is the quoted version of <unquoted> and represents the original
// string mentioned in the test case.
// This method compares the unescaped <unquoted> against its round trip version
// i.e. after carrying out escaping followed by unescaping on it.
static void TestBytesEscaping(absl::string_view unquoted,
                              absl::string_view quoted) {
  std::string unescaped;
  GOOGLESQL_EXPECT_OK(UnescapeBytes(unquoted, &unescaped)) << quoted;
  const std::string escaped = EscapeBytes(unescaped);
  std::string unescaped2;
  GOOGLESQL_EXPECT_OK(UnescapeBytes(escaped, &unescaped2));
  EXPECT_EQ(unescaped, unescaped2);
  const std::string escaped2 =
      EscapeBytes(unescaped, true /* escape_all_bytes */);
  std::string unescaped3;
  GOOGLESQL_EXPECT_OK(UnescapeBytes(escaped2, &unescaped3));
  EXPECT_EQ(unescaped, unescaped3);
}

// <quoted> takes a byte literal of the form b'...', b'''...'''
static void TestBytesLiteral(absl::string_view quoted) {
  std::string unquoted, error;
  int error_offset;
  // Parse the literal.
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &unquoted, nullptr, nullptr))
      << quoted;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &unquoted, &error, nullptr))
      << quoted << "\nERROR: " << error;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &unquoted, nullptr, &error_offset))
      << quoted << "\nERROR OFFSET: " << error_offset;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &unquoted, &error, &error_offset))
      << quoted << "\nERROR: " << error;

  // Take the parsed literal and turn it back to a literal.
  const std::string requoted = ToBytesLiteral(unquoted);
  std::string unquoted2;
  // Parse it again.
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(requoted, &unquoted2, &error))
      << unquoted << "  " << requoted << "\nERROR: " << error;
  // Test the parsed literal forms for equality, not the unparsed forms.
  // This is because the unparsed forms can have different representations for
  // the same data, i.e., \000 and \x00.
  EXPECT_EQ(unquoted, unquoted2) << "unquoted : " << unquoted
                                 << "\nunquoted2: " << unquoted2;

  TestBytesEscaping(unquoted, quoted);
}

// <quoted> takes a raw byte literal of the form rb'...', br'...', rb'''...'''
// or br'''...'''. <unquoted> is the expected parsed form of <quoted>.
static void TestQuotedRawBytesLiteral(absl::string_view unquoted,
                                      absl::string_view quoted) {
  std::string actual_unquoted, error;
  int error_offset;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &actual_unquoted, nullptr, nullptr))
      << unquoted << "\nERROR: " << error;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &actual_unquoted, &error, nullptr))
      << unquoted << "\nERROR: " << error;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &actual_unquoted, nullptr, &error_offset))
      << unquoted << "\nERROR: " << error;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &actual_unquoted, &error, &error_offset))
      << unquoted << "\nERROR: " << error;
  EXPECT_EQ(unquoted, actual_unquoted) << "quoted: " << quoted;
}

// <unquoted> takes a string of not escaped unquoted bytes.
static void TestUnescapedBytes(absl::string_view unquoted) {
  TestBytesLiteral(ToBytesLiteral(unquoted));
}

static void TestRawBytes(absl::string_view unquoted) {
  const std::string quote = (!absl::StrContains(unquoted, "'")) ? "'" : "\"";
  TestQuotedRawBytesLiteral(unquoted,
                            absl::StrCat("rb", quote, unquoted, quote));
  TestQuotedRawBytesLiteral(unquoted,
                            absl::StrCat("br", quote, unquoted, quote));
  TestQuotedRawBytesLiteral(unquoted, absl::StrCat("rb'''", unquoted, "'''"));
  TestQuotedRawBytesLiteral(unquoted, absl::StrCat("br'''", unquoted, "'''"));
  TestQuotedRawBytesLiteral(unquoted,
                            absl::StrCat("rb\"\"\"", unquoted, "\"\"\""));
  TestQuotedRawBytesLiteral(unquoted,
                            absl::StrCat("br\"\"\"", unquoted, "\"\"\""));
}

static void TestParseString(absl::string_view orig) {
  std::string parsed;
  GOOGLESQL_EXPECT_OK(ParseStringLiteral(orig, &parsed)) << orig;
}

static void TestParseBytes(absl::string_view orig) {
  std::string parsed;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral(orig, &parsed)) << orig;
}

static void TestStringEscaping(absl::string_view orig) {
  const std::string escaped = EscapeString(orig);
  std::string unescaped;
  GOOGLESQL_EXPECT_OK(UnescapeString(escaped, &unescaped)) << orig;
  EXPECT_EQ(orig, unescaped) << "escaped: " << escaped;
}

static void TestValue(absl::string_view orig) {
  TestStringEscaping(orig);
  TestString(orig);
  TestIdentifier(orig);
  TestAlwaysQuotedIdentifier(orig);
}

// Test that <str> is treated as invalid, with error offset
// <expected_error_offset> and an error that contains substring
// <expected_error_substr>. The last arguments are optional because most
// flat-out bad inputs are rejected without further information.
static void TestInvalidString(const std::string& str,
                              int expected_error_offset = 0,
                              const std::string& expected_error_substr = "") {
  std::string out, error_string;
  int error_offset;
  // Test with all combinations of NULL/non-NULL error_string and error_offset.
  EXPECT_THAT(ParseStringLiteral(str, &out, nullptr, nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_THAT(ParseStringLiteral(str, &out, &error_string, nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_SUBSTRING(expected_error_substr, error_string);
  EXPECT_THAT(ParseStringLiteral(str, &out, nullptr, &error_offset),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_EQ(error_offset, expected_error_offset) << error_string;
  EXPECT_THAT(ParseStringLiteral(str, &out, &error_string, &error_offset),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_EQ(error_offset, expected_error_offset) << error_string;
  EXPECT_SUBSTRING(expected_error_substr, error_string);
}

// Test that <str> is treated as invalid, with error offset
// <expected_error_offset> and an error that contains substring
// <expected_error_substr>. The last arguments are optional because most
// flat-out bad inputs are rejected without further information.
static void TestInvalidBytes(const std::string& str,
                             int expected_error_offset = 0,
                             const std::string& expected_error_substr = "") {
  std::string out, error_string;
  int error_offset;
  // Test with all combinations of NULL/non-NULL error_string and error_offset.
  EXPECT_THAT(ParseBytesLiteral(str, &out, nullptr, nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_THAT(ParseBytesLiteral(str, &out, &error_string, nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_SUBSTRING(expected_error_substr, error_string);
  EXPECT_THAT(ParseBytesLiteral(str, &out, nullptr, &error_offset),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_EQ(error_offset, expected_error_offset) << error_string;
  EXPECT_THAT(ParseBytesLiteral(str, &out, &error_string, &error_offset),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_EQ(error_offset, expected_error_offset) << error_string;
  EXPECT_SUBSTRING(expected_error_substr, error_string);
}

TEST(StringsTest, TestParsingOfAllEscapeCharacters) {
  // All the valid escapes.
  const absl::btree_set<char> valid_escapes = {'a', 'b',  'f', 'n', 'r',  't',
                                               'v', '\\', '?', '"', '\'', '`',
                                               'u', 'U',  'x', 'X'};
  for (int escape_char_int = 0; escape_char_int < 255; ++escape_char_int) {
    char escape_char = static_cast<char>(escape_char_int);
    absl::string_view escape_piece(&escape_char, 1);
    if (valid_escapes.contains(escape_char)) {
      if (escape_char == '\'') {
        TestParseString(absl::StrCat("\"a\\", escape_piece, "0010ffff\""));
      }
      TestParseString(absl::StrCat("'a\\", escape_piece, "0010ffff'"));
      TestParseString(absl::StrCat("'''a\\", escape_piece, "0010ffff'''"));
    } else if (absl::ascii_isdigit(escape_char)) {
      // Can also escape 0-3.
      const std::string test_string =
          absl::StrCat("'a\\", escape_piece, "00b'");
      const std::string test_triple_quoted_string =
          absl::StrCat("'''a\\", escape_piece, "00b'''");
      if (escape_char <= '3') {
        TestParseString(test_string);
        TestParseString(test_triple_quoted_string);
      } else {
        TestInvalidString(test_string, 2, "Illegal escape sequence: ");
        TestInvalidString(test_triple_quoted_string, 4,
                          "Illegal escape sequence: ");
      }
    } else {
      if (IsWellFormedUTF8(escape_piece)) {
        const std::string expected_error =
            ((escape_char == '\n' || escape_char == '\r')
                 ? "Illegal escaped newline"
                 : "Illegal escape sequence: ");
        TestInvalidString(absl::StrCat("'a\\", escape_piece, "b'"), 2,
                          expected_error);
        TestInvalidString(absl::StrCat("'''a\\", escape_piece, "b'''"), 4,
                          expected_error);
      } else {
        TestInvalidString(absl::StrCat("'a\\", escape_piece, "b'"), 3,
                          "Structurally invalid UTF8"  //
                          " string");  // coybara:ignore(std::string)
        TestInvalidString(absl::StrCat("'''a\\", escape_piece, "b'''"), 5,
                          "Structurally invalid UTF8"  //
                          " string");  // coybara:ignore(std::string)
      }
    }
  }
}

TEST(StringsTest, TestParsingOfOctalEscapes) {
  for (int idx = 0; idx < 256; ++idx) {
    const char end_char = (idx % 8) + '0';
    const char mid_char = ((idx / 8) % 8) + '0';
    const char lead_char = (idx / 64) + '0';
    absl::string_view lead_piece(&lead_char, 1);
    absl::string_view mid_piece(&mid_char, 1);
    absl::string_view end_piece(&end_char, 1);
    const std::string test_string =
        absl::StrCat(lead_piece, mid_piece, end_piece);
    TestParseString(absl::StrCat("'\\", test_string, "'"));
    TestParseString(absl::StrCat("'''\\", test_string, "'''"));
    TestParseBytes(absl::StrCat("b'\\", test_string, "'"));
  }
  TestInvalidString("'\\'", 3, "String must end with '");
  TestInvalidString("'abc\\'", 6, "String must end with '");
  TestInvalidString("'''\\'''", 7, "String must end with '''");
  TestInvalidString("'''abc\\'''", 10, "String must end with '''");
  TestInvalidString(
      "'\\0'", 1,
      "Octal escape must be followed by 3 octal digits but saw: \\0");
  TestInvalidString(
      "'''abc\\0'''", 6,
      "Octal escape must be followed by 3 octal digits but saw: \\0");
  TestInvalidString(
      "'\\00'", 1,
      "Octal escape must be followed by 3 octal digits but saw: \\00");
  TestInvalidString(
      "'''ab\\00'''", 5,
      "Octal escape must be followed by 3 octal digits but saw: \\00");
  TestInvalidString(
      "'a\\008'", 2,
      "Octal escape must be followed by 3 octal digits but saw: \\008");
  TestInvalidString(
      "'''\\008'''", 3,
      "Octal escape must be followed by 3 octal digits but saw: \\008");
  TestInvalidString("'\\400'", 1, "Illegal escape sequence: \\4");
  TestInvalidString("'''\\400'''", 3, "Illegal escape sequence: \\4");
  TestInvalidString("'\\777'", 1, "Illegal escape sequence: \\7");
  TestInvalidString("'''\\777'''", 3, "Illegal escape sequence: \\7");
}

TEST(StringsTest, TestParsingOfHexEscapes) {
  for (int idx = 0; idx < 256; ++idx) {
    char lead_char = absl::StrFormat("%X", idx / 16)[0];
    char end_char = absl::StrFormat("%x", idx % 16)[0];
    absl::string_view lead_piece(&lead_char, 1);
    absl::string_view end_piece(&end_char, 1);
    TestParseString(absl::StrCat("'\\x", lead_piece, end_piece, "'"));
    TestParseString(absl::StrCat("'''\\x", lead_piece, end_piece, "'''"));
    TestParseString(absl::StrCat("'\\X", lead_piece, end_piece, "'"));
    TestParseString(absl::StrCat("'''\\X", lead_piece, end_piece, "'''"));
    TestParseBytes(absl::StrCat("b'\\X", lead_piece, end_piece, "'"));
  }
  TestInvalidString("'\\x'", 1);
  TestInvalidString("'''\\x'''", 3);
  TestInvalidString("'\\x0'", 1);
  TestInvalidString("'''\\x0'''", 3);
  TestInvalidString("'\\x0G'", 1);
  TestInvalidString("'''\\x0G'''", 3);
}

TEST(StringsTest, RoundTrip) {
  // Empty string is valid as a string but not an identifier.
  TestStringEscaping("");
  TestString("");

  TestValue("abc");
  TestValue("abc123");
  TestValue("123abc");
  TestValue("_abc123");
  TestValue("_123");
  TestValue("abc def");
  TestValue("a`b");
  TestValue("a77b");
  TestValue("\"abc\"");
  TestValue("'abc'");
  TestValue("`abc`");
  TestValue("aaa'bbb\"ccc`ddd");
  TestValue("\n");
  TestValue("\\");
  TestValue("\\n");
  TestValue("\x12");
  TestValue("a,g  8q483 *(YG(*$(&*98fg\\r\\n\\t\x12gb");

  // Value with an embedded zero char.
  std::string s = "abc";
  s[1] = 0;
  TestValue(s);

  // Reserved SQL keyword, which must be quoted as an identifier.
  TestValue("select");
  TestValue("SELECT");
  TestValue("SElecT");
  // Non-reserved SQL keyword, which shouldn't be quoted.
  TestValue("options");

  // Note that control characters and other odd byte values such as \0 are
  // allowed in string literals as long as they are utf8 structurally valid.
  TestValue("\x01\x31");
  TestValue("abc\xb\x42\141bc");
  TestValue("123\1\x31\x32\x33");
  TestValue("\\\"\xe8\xb0\xb7\xe6\xad\x8c\\\" is Google\\\'s Chinese name");
}

TEST(StringsTest, InvalidString) {
  const std::string kInvalidStringLiteral = "Invalid string literal";

  TestInvalidString("A", 0, kInvalidStringLiteral);    // No quote at all
  TestInvalidString("'", 0, kInvalidStringLiteral);    // No closing quote
  TestInvalidString("\"", 0, kInvalidStringLiteral);   // No closing quote
  TestInvalidString("a'", 0, kInvalidStringLiteral);   // No opening quote
  TestInvalidString("a\"", 0, kInvalidStringLiteral);  // No opening quote
  TestInvalidString("'''", 1, "String cannot contain unescaped '");
  TestInvalidString("\"\"\"", 1, "String cannot contain unescaped \"");
  TestInvalidString("''''", 1, "String cannot contain unescaped '");
  TestInvalidString("\"\"\"\"", 1, "String cannot contain unescaped \"");
  TestInvalidString("'''''", 1, "String cannot contain unescaped '");
  TestInvalidString("\"\"\"\"\"", 1, "String cannot contain unescaped \"");
  TestInvalidString("'''''''", 3, "String cannot contain unescaped '''");
  TestInvalidString("\"\"\"\"\"\"\"", 3,
                    "String cannot contain unescaped \"\"\"");
  TestInvalidString("'''''''''", 3, "String cannot contain unescaped '''");
  TestInvalidString("\"\"\"\"\"\"\"\"\"", 3,
                    "String cannot contain unescaped \"\"\"");

  TestInvalidString("abc");

  TestInvalidString("'abc'def'", 4, "String cannot contain unescaped '");
  TestInvalidString("'abc''def'", 4, "String cannot contain unescaped '");
  TestInvalidString("\"abc\"\"def\"", 4, "String cannot contain unescaped \"");
  TestInvalidString("'''abc'''def'''", 6,
                    "String cannot contain unescaped '''");
  TestInvalidString("\"\"\"abc\"\"\"def\"\"\"", 6,
                    "String cannot contain unescaped \"\"\"");

  TestInvalidString("'abc");
  TestInvalidString("\"abc");
  TestInvalidString("'''abc");
  TestInvalidString("\"\"\"abc");

  TestInvalidString("abc'");
  TestInvalidString("abc\"");
  TestInvalidString("abc'''");
  TestInvalidString("abc\"\"\"");

  TestInvalidString("\"abc'");
  TestInvalidString("'abc\"");
  TestInvalidString("'''abc'", 1, "String cannot contain unescaped '");
  TestInvalidString("'''abc\"");

  TestInvalidString("'''a'", 1, "String cannot contain unescaped '");
  TestInvalidString("\"\"\"a\"", 1, "String cannot contain unescaped \"");
  TestInvalidString("'''a''", 1, "String cannot contain unescaped '");
  TestInvalidString("\"\"\"a\"\"", 1, "String cannot contain unescaped \"");
  TestInvalidString("'''a''''", 4, "String cannot contain unescaped '''");
  TestInvalidString("\"\"\"a\"\"\"\"", 4,
                    "String cannot contain unescaped \"\"\"");

  TestInvalidString("'''abc\"\"\"");
  TestInvalidString("\"\"\"abc'");
  TestInvalidString("\"\"\"abc\"", 1, "String cannot contain unescaped \"");
  TestInvalidString("\"\"\"abc'''");
  TestInvalidString("'''\\\''''''", 5, "String cannot contain unescaped '''");
  TestInvalidString("\"\"\"\\\"\"\"\"\"\"", 5,
                    "String cannot contain unescaped \"\"\"");
  TestInvalidString("''''\\\'''''", 6, "String cannot contain unescaped '''");
  TestInvalidString("\"\"\"\"\\\"\"\"\"\"", 6,
                    "String cannot contain unescaped \"\"\"");
  TestInvalidString("\"\"\"'a' \"b\"\"\"\"", 9,
                    "String cannot contain unescaped \"\"\"");

  TestInvalidString("`abc`");

  TestInvalidString("'abc\\'", 6, "String must end with '");
  TestInvalidString("\"abc\\\"", 6, "String must end with \"");
  TestInvalidString("'''abc\\'''", 10, "String must end with '''");
  TestInvalidString("\"\"\"abc\\\"\"\"", 10, "String must end with \"\"\"");

  TestInvalidString("'\\U12345678'", 1,
                    "Value of \\U12345678 exceeds Unicode limit (0x0010FFFF)");

  // All trailing escapes.
  TestInvalidString("'\\");
  TestInvalidString("\"\\");
  TestInvalidString("''''''\\");
  TestInvalidString("\"\"\"\"\"\"\\");
  TestInvalidString("''\\\\");
  TestInvalidString("\"\"\\\\");
  TestInvalidString("''''''\\\\");
  TestInvalidString("\"\"\"\"\"\"\\\\");

  // String with an unescaped 0 byte.
  std::string s = "abc";
  s[1] = 0;
  TestInvalidString(s);
  // Note: These are C-escapes to define the invalid strings.
  TestInvalidString("'\xc1'", 1, "Structurally invalid UTF8 string");
  TestInvalidString("'\xca'", 1, "Structurally invalid UTF8 string");
  TestInvalidString("'\xcc'", 1, "Structurally invalid UTF8 string");
  TestInvalidString("'\xFA'", 1, "Structurally invalid UTF8 string");
  TestInvalidString("'\xc1\xca\x1b\x62\x19o\xcc\x04'", 1,
                    "Structurally invalid UTF8 string");

  TestInvalidString("'\xc2\xc0'", 1,
                    "Structurally invalid UTF8 string");  // First byte ok utf8,
                                                          // invalid together.
  TestValue("\xc2\xbf");            // Same first byte, good sequence.

  // These are all valid prefixes for utf8 characters, but the characters
  // are not complete.
  TestInvalidString(
      "'\xc2'", 1,
      "Structurally invalid UTF8 string");  // Should be 2 byte utf8 character.
  TestInvalidString(
      "'\xc3'", 1,
      "Structurally invalid UTF8 string");  // Should be 2 byte utf8 character.
  TestInvalidString(
      "'\xe0'", 1,
      "Structurally invalid UTF8 string");  // Should be 3 byte utf8 character.
  TestInvalidString(
      "'\xe0\xac'", 1,
      "Structurally invalid UTF8 string");  // Should be 3 byte utf8 character.
  TestInvalidString(
      "'\xf0'", 1,
      "Structurally invalid UTF8 string");  // Should be 4 byte utf8 character.
  TestInvalidString(
      "'\xf0\x90'", 1,
      "Structurally invalid UTF8 string");  // Should be 4 byte utf8 character.
  TestInvalidString(
      "'\xf0\x90\x80'", 1,
      "Structurally invalid UTF8 string");  // Should be 4 byte utf8 character.
}

TEST(BytesTest, RoundTrip) {
  TestBytesLiteral("b\"\"");
  TestBytesLiteral("b\"\"\"\"\"\"");
  TestUnescapedBytes("");

  TestBytesLiteral("b'\\000\\x00AAA\\xfF\\377'");
  TestBytesLiteral("b'''\\000\\x00AAA\\xfF\\377'''");
  TestBytesLiteral("b'\\a\\b\\f\\n\\r\\t\\v\\\\\\?\\\"\\'\\`\\x00\\Xff'");
  TestBytesLiteral("b'''\\a\\b\\f\\n\\r\\t\\v\\\\\\?\\\"\\'\\`\\x00\\Xff'''");

  TestBytesLiteral("b'\\n\\012\\x0A'");  // Different newline representations.
  TestBytesLiteral("b'''\\n\\012\\x0A'''");

  // Note the C-escaping to define the bytes.  These are invalid strings for
  // various reasons, but are valid as bytes.
  TestUnescapedBytes("\xc1");
  TestUnescapedBytes("\xca");
  TestUnescapedBytes("\xcc");
  TestUnescapedBytes("\xFA");
  TestUnescapedBytes("\xc1\xca\x1b\x62\x19o\xcc\x04");
}

TEST(BytesTest, ToBytesLiteralTests) {
  // ToBytesLiteral will choose to quote with ' if it will avoid escaping.
  // Non-printable bytes are escaped as hex.  For printable bytes, only the
  // escape character and quote character are escaped.
  EXPECT_EQ("b\"\"", ToBytesLiteral(""));
  EXPECT_EQ("b\"abc\"", ToBytesLiteral("abc"));
  EXPECT_EQ("b\"abc'def\"", ToBytesLiteral("abc'def"));
  EXPECT_EQ("b'abc\"def'", ToBytesLiteral("abc\"def"));
  EXPECT_EQ("b\"abc`def\"", ToBytesLiteral("abc`def"));
  EXPECT_EQ("b\"abc'\\\"`def\"", ToBytesLiteral("abc'\"`def"));

  // Override the quoting style to use single quotes.
  EXPECT_EQ("b''", ToSingleQuotedBytesLiteral(""));
  EXPECT_EQ("b'abc'", ToSingleQuotedBytesLiteral("abc"));
  EXPECT_EQ("b'abc\\'def'", ToSingleQuotedBytesLiteral("abc'def"));
  EXPECT_EQ("b'abc\"def'", ToSingleQuotedBytesLiteral("abc\"def"));
  EXPECT_EQ("b'abc`def'", ToSingleQuotedBytesLiteral("abc`def"));
  EXPECT_EQ("b'abc\\'\"`def'", ToSingleQuotedBytesLiteral("abc'\"`def"));

  // Override the quoting style to use double quotes.
  EXPECT_EQ("b\"\"", ToDoubleQuotedBytesLiteral(""));
  EXPECT_EQ("b\"abc\"", ToDoubleQuotedBytesLiteral("abc"));
  EXPECT_EQ("b\"abc'def\"", ToDoubleQuotedBytesLiteral("abc'def"));
  EXPECT_EQ("b\"abc\\\"def\"", ToDoubleQuotedBytesLiteral("abc\"def"));
  EXPECT_EQ("b\"abc`def\"", ToDoubleQuotedBytesLiteral("abc`def"));
  EXPECT_EQ("b\"abc'\\\"`def\"", ToDoubleQuotedBytesLiteral("abc'\"`def"));

  EXPECT_EQ("b\"\\x07-\\x08-\\x0c-\\x0a-\\x0d-\\x09-\\x0b-\\\\-?-\\\"-'-`\"",
            ToBytesLiteral("\a-\b-\f-\n-\r-\t-\v-\\-?-\"-'-`"));

  EXPECT_EQ("b\"\\x0a\"", ToBytesLiteral("\n"));

  std::string unquoted;
  std::string error;
  GOOGLESQL_EXPECT_OK(ParseBytesLiteral("b'\\n\\012\\x0a\\x0A'", &unquoted, &error))
      << error;
  EXPECT_EQ("b\"\\x0a\\x0a\\x0a\\x0a\"", ToBytesLiteral(unquoted));
}

TEST(ByesTest, InvalidBytes) {
  TestInvalidBytes("A", 0, "Invalid bytes literal");  // No quotes
  TestInvalidBytes("b'A", 0, "Invalid bytes literal");  // No ending quote
  TestInvalidBytes("'A'", 0, "Invalid bytes literal");  // No ending quote
  TestInvalidBytes("'A'", 0, "Invalid bytes literal");  // No 'b' prefix.
  TestInvalidBytes("'''A'''");
  TestInvalidBytes("b'k\\u0030'", 3, kUnicodeNotAllowedInBytes1);
  TestInvalidBytes("b'''\\u0030'''", 4, kUnicodeNotAllowedInBytes1);
  TestInvalidBytes("b'\\U00000030'", 2, kUnicodeNotAllowedInBytes2);
  TestInvalidBytes("b'''qwerty\\U00000030'''", 10, kUnicodeNotAllowedInBytes2);

  std::string unescaped;
  std::string error;
  int error_offset;
  EXPECT_FALSE(UnescapeBytes("abc\\u0030", &unescaped).ok());
  EXPECT_FALSE(UnescapeBytes("abc\\U00000030", &unescaped, &error).ok());
  EXPECT_FALSE(
      UnescapeBytes("abc\\U00000030", &unescaped, &error, &error_offset).ok());
  EXPECT_EQ(error_offset, 3);
}

TEST(RawStringsTest, ValidCases) {
  TestRawString("");
  TestRawString("1");
  TestRawString("\\x53");
  TestRawString("\\x123");
  TestRawString("\\001");
  TestRawString("a\\44'A");
  TestRawString("a\\e");
  TestRawString("\\ea");
  TestRawString("\\U1234");
  TestRawString("\\u");
  TestRawString("\\xc2\\\\");
  TestRawString("f\\(abc',(.*),def\\?");
  TestRawString("a\\\"b");
}

TEST(RawStringsTest, InvalidRawStrings) {
  TestInvalidString("r\"\\\"", 4, "String must end with \"");
  TestInvalidString("r\"\\\\\\\"", 6, "String must end with \"");
  TestInvalidString("r\"");
  TestInvalidString("r");
  TestInvalidString("rb\"\"");
  TestInvalidString("b\"\"");
  TestInvalidString("r'''", 2, "String cannot contain unescaped '");
}

TEST(RawBytesTest, ValidCases) {
  TestRawBytes("");
  TestRawBytes("1");
  TestRawBytes("\\x53");
  TestRawBytes("\\x123");
  TestRawBytes("\\001");
  TestRawBytes("a\\44'A");
  TestRawBytes("a\\e");
  TestRawBytes("\\ea");
  TestRawBytes("\\U1234");
  TestRawBytes("\\u");
  TestRawBytes("\\xc2\\\\");
  TestRawBytes("f\\(abc',(.*),def\\?");
}

TEST(RawBytesTest, InvalidRawBytes) {
  TestInvalidBytes("r''");
  TestInvalidBytes("r''''''");
  TestInvalidBytes("rrb''");
  TestInvalidBytes("brb''");
  TestInvalidBytes("rb'a\\e");
  TestInvalidBytes("rb\"\\\"", 5, "String must end with \"");
  TestInvalidBytes("br\"\\\\\\\"", 7, "String must end with \"");
  TestInvalidBytes("rb");
  TestInvalidBytes("br");
  TestInvalidBytes("rb\"");
  TestInvalidBytes("rb\"\"\"", 3, "String cannot contain unescaped \"");
  TestInvalidBytes("rb\"xyz\"\"", 6, "String cannot contain unescaped \"");
}

static void TestInvalidIdentifier(
    const std::string& str, int expected_error_offset = 0,
    const std::string& expected_error_substr = "") {
  std::string out;
  std::string error_string;
  int error_offset = 0;
  // Test with all combinations of NULL/non-NULL error_string and error_offset.
  EXPECT_THAT(ParseIdentifier(str, LanguageOptions(), &out, nullptr, nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_THAT(
      ParseIdentifier(str, LanguageOptions(), &out, nullptr, &error_offset),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(expected_error_substr)))
      << str;
  EXPECT_THAT(
      ParseIdentifier(str, LanguageOptions(), &out, &error_string, nullptr),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(expected_error_substr)))
      << str;
  EXPECT_THAT(ParseIdentifier(str, LanguageOptions(), &out, &error_string,
                              &error_offset),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(expected_error_substr)))
      << str;
  EXPECT_EQ(error_offset, expected_error_offset) << error_string;
  EXPECT_SUBSTRING(expected_error_substr, error_string);
}

TEST(StringsTest, InvalidIdentifier) {
  // Only identifiers that start with a backtick get error offset information.
  TestInvalidIdentifier("", 0, "Invalid identifier");
  TestInvalidIdentifier("`", 1, "String must end with `");
  TestInvalidIdentifier("``", 0, "Invalid empty identifier");
  TestInvalidIdentifier("`abc", 4, "String must end with `");
  TestInvalidIdentifier("abc`");
  TestInvalidIdentifier("`abc`abc`", 4, "String cannot contain unescaped `");
  TestInvalidIdentifier("`ab``abc`", 3, "String cannot contain unescaped `");
  TestInvalidIdentifier("'abc'");
  TestInvalidIdentifier("\"abc\"");
  TestInvalidIdentifier("abc def");
  TestInvalidIdentifier("abc,def");
  TestInvalidIdentifier("`abc\\`", 6, "String must end with `");
  TestInvalidIdentifier("123");
  TestInvalidIdentifier("ab!c");
  TestInvalidIdentifier(
      "`abc\\U12345678`", 4,
      "Value of \\U12345678 exceeds Unicode limit (0x0010FFFF)");
  TestInvalidIdentifier("selECT");  // A reserved keyword.
}

TEST(StringsTest, QuotedForms) {
  // EscapeString escapes all quote characters.
  EXPECT_EQ("", EscapeString(""));
  EXPECT_EQ("abc", EscapeString("abc"));
  EXPECT_EQ("abc\\'def", EscapeString("abc'def"));
  EXPECT_EQ("abc\\\"def", EscapeString("abc\"def"));
  EXPECT_EQ("abc\\`def", EscapeString("abc`def"));

  // ToStringLiteral will choose to quote with ' if it will avoid escaping.
  // Other quoted characters will not be escaped.
  EXPECT_EQ("\"\"", ToStringLiteral(""));
  EXPECT_EQ("\"abc\"", ToStringLiteral("abc"));
  EXPECT_EQ("\"abc'def\"", ToStringLiteral("abc'def"));
  EXPECT_EQ("'abc\"def'", ToStringLiteral("abc\"def"));
  EXPECT_EQ("\"abc`def\"", ToStringLiteral("abc`def"));
  EXPECT_EQ("\"abc'\\\"`def\"", ToStringLiteral("abc'\"`def"));

  // Override the quoting style to use single quotes.
  EXPECT_EQ("''", ToSingleQuotedStringLiteral(""));
  EXPECT_EQ("'abc'", ToSingleQuotedStringLiteral("abc"));
  EXPECT_EQ("'abc\\'def'", ToSingleQuotedStringLiteral("abc'def"));
  EXPECT_EQ("'abc\"def'", ToSingleQuotedStringLiteral("abc\"def"));
  EXPECT_EQ("'abc`def'", ToSingleQuotedStringLiteral("abc`def"));
  EXPECT_EQ("'abc\\'\"`def'", ToSingleQuotedStringLiteral("abc'\"`def"));

  // Override the quoting style to use double quotes.
  EXPECT_EQ("\"\"", ToDoubleQuotedStringLiteral(""));
  EXPECT_EQ("\"abc\"", ToDoubleQuotedStringLiteral("abc"));
  EXPECT_EQ("\"abc'def\"", ToDoubleQuotedStringLiteral("abc'def"));
  EXPECT_EQ("\"abc\\\"def\"", ToDoubleQuotedStringLiteral("abc\"def"));
  EXPECT_EQ("\"abc`def\"", ToDoubleQuotedStringLiteral("abc`def"));
  EXPECT_EQ("\"abc'\\\"`def\"", ToDoubleQuotedStringLiteral("abc'\"`def"));

  // ToIdentifierLiteral only has to quote `.
  EXPECT_EQ("``", ToIdentifierLiteral(""));  // Not actually a valid identifier.
  EXPECT_EQ("abc", ToIdentifierLiteral("abc"));
  EXPECT_EQ("`abc'def`", ToIdentifierLiteral("abc'def"));
  EXPECT_EQ("`abc\"def`", ToIdentifierLiteral("abc\"def"));
  EXPECT_EQ("`abc\\`def`", ToIdentifierLiteral("abc`def"));
  EXPECT_EQ("`123`", ToIdentifierLiteral("123"));
  EXPECT_EQ("_123", ToIdentifierLiteral("_123"));
  EXPECT_EQ("a12_3", ToIdentifierLiteral("a12_3"));
  EXPECT_EQ("a1", ToIdentifierLiteral("a1"));
  EXPECT_EQ("`1a`", ToIdentifierLiteral("1a"));
  // Reserved SQL keywords get quoted.
  EXPECT_EQ("`selECT`", ToIdentifierLiteral("selECT"));
  // Non-reserved SQL keywords normally don't get quoted.
  EXPECT_EQ("optIONS", ToIdentifierLiteral("optIONS"));
  EXPECT_EQ("optIONS", ToIdentifierLiteral(IdString::MakeGlobal("optIONS")));
  // Non-reserved SQL keywords do get quoted if they can be used in an
  // identifier context and take on special meaning there.
  EXPECT_EQ("`safe_cAst`", ToIdentifierLiteral("safe_cAst"));

  // ToAlwaysQuotedIdentifierLiteral always back quote.
  EXPECT_EQ("``", ToAlwaysQuotedIdentifierLiteral(
                      ""));  // Not actually a valid identifier.
  EXPECT_EQ("`abc`", ToAlwaysQuotedIdentifierLiteral("abc"));
  EXPECT_EQ("`abc'def`", ToAlwaysQuotedIdentifierLiteral("abc'def"));
  EXPECT_EQ("`abc\"def`", ToAlwaysQuotedIdentifierLiteral("abc\"def"));
  EXPECT_EQ("`abc\\`def`", ToAlwaysQuotedIdentifierLiteral("abc`def"));
  EXPECT_EQ("`123`", ToAlwaysQuotedIdentifierLiteral("123"));
  EXPECT_EQ("`_123`", ToAlwaysQuotedIdentifierLiteral("_123"));
  EXPECT_EQ("`a12_3`", ToAlwaysQuotedIdentifierLiteral("a12_3"));
  EXPECT_EQ("`a1`", ToAlwaysQuotedIdentifierLiteral("a1"));
  EXPECT_EQ("`1a`", ToAlwaysQuotedIdentifierLiteral("1a"));
  // Reserved SQL keywords get quoted.
  EXPECT_EQ("`selECT`", ToAlwaysQuotedIdentifierLiteral("selECT"));
  // Non-reserved SQL keywords normally don't get quoted.
  EXPECT_EQ("`optIONS`", ToAlwaysQuotedIdentifierLiteral("optIONS"));
  // Non-reserved SQL keywords do get quoted if they can be used in an
  // identifier context and take on special meaning there.
  EXPECT_EQ("`safe_cAst`", ToAlwaysQuotedIdentifierLiteral("safe_cAst"));
}

static void ExpectParsedString(absl::string_view expected,
                               std::vector<std::string> quoted_strings) {
  for (const std::string& quoted : quoted_strings) {
    std::string parsed, error_string;
    int error_offset;
    GOOGLESQL_EXPECT_OK(ParseStringLiteral(quoted, &parsed, nullptr, nullptr))
        << quoted << "\nERROR: " << error_string;
    EXPECT_EQ(expected, parsed);
    GOOGLESQL_EXPECT_OK(ParseStringLiteral(quoted, &parsed, &error_string, nullptr))
        << quoted << "\nERROR: " << error_string;
    EXPECT_EQ(expected, parsed);
    GOOGLESQL_EXPECT_OK(ParseStringLiteral(quoted, &parsed, nullptr, &error_offset))
        << quoted << "\nERROR: " << error_string;
    EXPECT_EQ(expected, parsed);
    GOOGLESQL_EXPECT_OK(ParseStringLiteral(quoted, &parsed, &error_string, &error_offset))
        << quoted << "\nERROR: " << error_string;
    EXPECT_EQ(expected, parsed);
  }
}

static void ExpectParsedBytes(absl::string_view expected,
                              std::vector<std::string> quoted_strings) {
  for (const std::string& quoted : quoted_strings) {
    std::string parsed, error_string;
    GOOGLESQL_EXPECT_OK(ParseBytesLiteral(quoted, &parsed, &error_string))
        << quoted << "\nERROR" << error_string;
    EXPECT_EQ(expected, parsed);
  }
}

static void ExpectParsedIdentifier(absl::string_view expected,
                                   absl::string_view quoted) {
  std::string parsed;
  std::string error_string;
  int error_offset;
  GOOGLESQL_EXPECT_OK(
      ParseIdentifier(quoted, LanguageOptions(), &parsed, nullptr, nullptr))
      << quoted;
  EXPECT_EQ(expected, parsed);
  GOOGLESQL_EXPECT_OK(ParseIdentifier(quoted, LanguageOptions(), &parsed, &error_string,
                            nullptr))
      << quoted << "\nERROR: " << error_string;
  EXPECT_EQ(expected, parsed);
  GOOGLESQL_EXPECT_OK(ParseIdentifier(quoted, LanguageOptions(), &parsed, nullptr,
                            &error_offset))
      << quoted << "\nERROR OFFSET: " << error_offset;
  EXPECT_EQ(expected, parsed);
  GOOGLESQL_EXPECT_OK(ParseIdentifier(quoted, LanguageOptions(), &parsed, &error_string,
                            &error_offset))
      << quoted << "\nERROR: " << error_string;
  EXPECT_EQ(expected, parsed);
}

TEST(StringsTest, Parse) {
  ExpectParsedString("abc",
                     {"'abc'", "\"abc\"", "'''abc'''", "\"\"\"abc\"\"\""});
  ExpectParsedIdentifier("abc", "`abc`");
  ExpectParsedIdentifier("abc", "abc");
  // Reserved keyword must be quoted.
  ExpectParsedIdentifier("SELect", "`SELect`");
  // Non-reserved keyword works with or without quotes.
  ExpectParsedIdentifier("OPTions", "`OPTions`");
  ExpectParsedIdentifier("OPTions", "OPTions");

  ExpectParsedString(
      "abc\ndef\x12ghi",
      {"'abc\\ndef\\x12ghi'", "\"abc\\ndef\\x12ghi\"",
       "'''abc\\ndef\\x12ghi'''", "\"\"\"abc\\ndef\\x12ghi\"\"\""});
  ExpectParsedIdentifier("abc\ndef\x12ghi", "`abc\\ndef\\x12ghi`");
  ExpectParsedString("\xF4\x8F\xBF\xBD",
                     {"'\\U0010FFFD'", "\"\\U0010FFFD\"", "'''\\U0010FFFD'''",
                      "\"\"\"\\U0010FFFD\"\"\""});
  ExpectParsedIdentifier("\xF4\x8F\xBF\xBD", "`\\U0010FFFD`");

  // Some more test cases for triple quoted content.
  ExpectParsedString("", {"''''''", "\"\"\"\"\"\""});
  ExpectParsedString("'\"", {"''''\"'''"});
  ExpectParsedString("''''''", {"'''''\\'''\\''''"});
  ExpectParsedString("'", {"'''\\''''"});
  ExpectParsedString("''", {"'''\\'\\''''"});
  ExpectParsedString("'\"", {"''''\"'''"});
  ExpectParsedString("'a", {"''''a'''"});
  ExpectParsedString("\"a", {"\"\"\"\"a\"\"\""});
  ExpectParsedString("''a", {"'''''a'''"});
  ExpectParsedString("\"\"a", {"\"\"\"\"\"a\"\"\""});
}

TEST(StringsTest, TestNewlines) {
  ExpectParsedString("a\nb", {"'''a\rb'''", "'''a\nb'''", "'''a\r\nb'''"});
  ExpectParsedString("a\n\nb", {"'''a\n\rb'''", "'''a\r\n\r\nb'''"});
  // Escaped newlines.
  ExpectParsedString("a\nb", {"'''a\\nb'''"});
  ExpectParsedString("a\rb", {"'''a\\rb'''"});
  ExpectParsedString("a\r\nb", {"'''a\\r\\nb'''"});
}

TEST(RawStringsTest, CompareRawAndRegularStringParsing) {
  ExpectParsedString("\\n",
                     {"r'\\n'", "r\"\\n\"", "r'''\\n'''", "r\"\"\"\\n\"\"\""});
  ExpectParsedString("\n",
                     {"'\\n'", "\"\\n\"", "'''\\n'''", "\"\"\"\\n\"\"\""});

  ExpectParsedString("\\e",
                     {"r'\\e'", "r\"\\e\"", "r'''\\e'''", "r\"\"\"\\e\"\"\""});
  TestInvalidString("'\\e'", 1, "Illegal escape sequence: \\e");
  TestInvalidString("\"\\e\"", 1, "Illegal escape sequence: \\e");
  TestInvalidString("'''\\e'''", 3, "Illegal escape sequence: \\e");
  TestInvalidString("\"\"\"\\e\"\"\"", 3, "Illegal escape sequence: \\e");

  ExpectParsedString(
      "\\x0", {"r'\\x0'", "r\"\\x0\"", "r'''\\x0'''", "r\"\"\"\\x0\"\"\""});
  constexpr char kHexError[] =
      "Hex escape must be followed by 2 hex digits but saw: \\x0";
  TestInvalidString("'\\x0'", 1, kHexError);
  TestInvalidString("\"\\x0\"", 1, kHexError);
  TestInvalidString("'''\\x0'''", 3, kHexError);
  TestInvalidString("\"\"\"\\x0\"\"\"", 3, kHexError);

  ExpectParsedString("\\'", {"r'\\\''"});
  ExpectParsedString("'", {"'\\\''"});
  ExpectParsedString("\\\"", {"r\"\\\"\""});
  ExpectParsedString("\"", {"\"\\\"\""});
  ExpectParsedString("''\\'", {"r'''\'\'\\\''''"});
  ExpectParsedString("'''", {"'''\'\'\\\''''"});
  ExpectParsedString("\"\"\\\"", {"r\"\"\"\"\"\\\"\"\"\""});
  ExpectParsedString("\"\"\"", {"\"\"\"\"\"\\\"\"\"\""});
}

TEST(RawBytesTest, CompareRawAndRegularBytesParsing) {
  ExpectParsedBytes("\\n", {"rb'\\n'", "br'\\n'", "rb\"\\n\"", "br\"\\n\""});
  ExpectParsedBytes("\n", {"b'\\n'", "b\"\\n\""});

  ExpectParsedBytes("\\u0030", {"rb'\\u0030'", "br'\\u0030'", "rb\"\\u0030\"",
                                "br\"\\u0030\""});
  TestInvalidBytes("b'\\u0030'", 2, kUnicodeNotAllowedInBytes1);
  TestInvalidBytes("b\"\\u0030\"", 2, kUnicodeNotAllowedInBytes1);
  TestInvalidBytes("b\"abc\\u0030\"", 5, kUnicodeNotAllowedInBytes1);

  ExpectParsedBytes("\\U00000030", {"rb'\\U00000030'", "br'\\U00000030'",
                                    "rb\"\\U00000030\"", "br\"\\U00000030\""});
  TestInvalidBytes("b'\\U00000030'", 2, kUnicodeNotAllowedInBytes2);
  TestInvalidBytes("b\"\\U00000030\"", 2, kUnicodeNotAllowedInBytes2);
  TestInvalidBytes("b\"abc\\U00000030\"", 5, kUnicodeNotAllowedInBytes2);

  ExpectParsedBytes("\\e", {"rb'\\e'", "br'\\e'", "rb\"\\e\"", "br\"\\e\""});
  TestInvalidBytes("b'\\e'", 2, "Illegal escape sequence: \\e");
  TestInvalidBytes("b\"\\e\"", 2, "Illegal escape sequence: \\e");
  TestInvalidBytes("b\"abcd\\e\"", 6, "Illegal escape sequence: \\e");

  ExpectParsedBytes("\\'", {"rb'\\\''", "br'\\\''"});
  ExpectParsedBytes("'", {"b'\\\''"});
  ExpectParsedBytes("\\\"", {"rb\"\\\"\"", "br\"\\\"\""});
  ExpectParsedBytes("\"", {"b\"\\\"\""});
  ExpectParsedBytes("''\\'", {"rb'''\'\'\\\''''", "br'''\'\'\\\''''"});
  ExpectParsedBytes("'''", {"b'''\'\'\\\''''"});
  ExpectParsedBytes("\"\"\\\"",
                    {"rb\"\"\"\"\"\\\"\"\"\"", "br\"\"\"\"\"\\\"\"\"\""});
  ExpectParsedBytes("\"\"\"", {"b\"\"\"\"\"\\\"\"\"\""});
}

struct epair {
  std::string escaped;
  std::string unescaped;
};

// Copied from strings/escaping_test.cc, CEscape::BasicEscaping.
TEST(StringsTest, UTF8Escape) {
  epair utf8_hex_values[] = {
      {"\x20\xe4\xbd\xa0\\t\xe5\xa5\xbd,\\r!\\n",
       "\x20\xe4\xbd\xa0\t\xe5\xa5\xbd,\r!\n"},
      {"\xe8\xa9\xa6\xe9\xa8\x93\\\' means \\\"test\\\"",
       "\xe8\xa9\xa6\xe9\xa8\x93\' means \"test\""},
      {"\\\\\xe6\x88\x91\\\\:\\\\\xe6\x9d\xa8\xe6\xac\xa2\\\\",
       "\\\xe6\x88\x91\\:\\\xe6\x9d\xa8\xe6\xac\xa2\\"},
      {"\xed\x81\xac\xeb\xa1\xac\\x08\\t\\n\\x0b\\x0c\\r",
       "\xed\x81\xac\xeb\xa1\xac\010\011\012\013\014\015"}
  };

  for (int i = 0; i < ABSL_ARRAYSIZE(utf8_hex_values); ++i) {
    std::string escaped = EscapeString(utf8_hex_values[i].unescaped);
    EXPECT_EQ(escaped, utf8_hex_values[i].escaped);
  }
}

// Originally copied from strings/escaping_test.cc, Unescape::BasicFunction,
// but changes for '\\xABCD' which only parses 2 hex digits after the escape.
TEST(StringsTest, UTF8Unescape) {
  epair tests[] =
    {{"\\u0030", "0"},
     {"\\u00A3", "\xC2\xA3"},
     {"\\u22FD", "\xE2\x8B\xBD"},
     {"\\ud7FF", "\xED\x9F\xBF"},
     {"\\u22FD", "\xE2\x8B\xBD"},
     {"\\U00010000", "\xF0\x90\x80\x80"},
     {"\\U0000E000", "\xEE\x80\x80"},
     {"\\U0001DFFF", "\xF0\x9D\xBF\xBF"},
     {"\\U0010FFFD", "\xF4\x8F\xBF\xBD"},
     {"\\xAbCD", "\xc2\xab" "CD"},
     {"\\253CD", "\xc2\xab" "CD"},
     {"\\x4141", "A41"}};
  for (int i = 0; i < ABSL_ARRAYSIZE(tests); ++i) {
    const std::string& e = tests[i].escaped;
    const std::string& u = tests[i].unescaped;
    std::string out;
    GOOGLESQL_EXPECT_OK(UnescapeString(e, &out));
    EXPECT_EQ(u, out) << "original escaped: '" << e << "'\nunescaped: '"
                      << out << "'\nexpected unescaped: '" << u << "'";
  }
  std::string bad[] = {"\\u1",  // too short
                       "\\U1",  // too short
                       "\\Uffffff",
                       "\\777"};  // exceeds 0xff
  for (int i = 0; i < ABSL_ARRAYSIZE(bad); ++i) {
    const std::string& e = bad[i];
    std::string out;
    EXPECT_THAT(UnescapeString(e, &out),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid escaped string")))
        << "original: '" << e << "'\nunescaped: '" << out << "'";
  }
}

TEST(StringsTest, TestUnescapeErrorMessages) {
  std::string error_string;
  std::string out;

  EXPECT_FALSE(UnescapeString("\\2", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: Octal escape must be followed by 3 octal "
      "digits but saw: \\2",
      error_string);

  EXPECT_FALSE(UnescapeString("\\22X0", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: Octal escape must be followed by 3 octal "
      "digits but saw: \\22X",
      error_string);

  EXPECT_FALSE(UnescapeString("\\X0", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: Hex escape must be followed by 2 hex digits "
      "but saw: \\X0",
      error_string);

  EXPECT_FALSE(UnescapeString("\\x0G0", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: Hex escape must be followed by 2 hex digits "
      "but saw: \\x0G",
      error_string);

  EXPECT_FALSE(UnescapeString("\\u00", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: \\u must be followed by 4 hex digits but saw: "
      "\\u00",
      error_string);

  EXPECT_FALSE(UnescapeString("\\ude8c", &out, &error_string).ok());
  EXPECT_EQ("Illegal escape sequence: Unicode value \\ude8c is invalid",
            error_string);

  EXPECT_FALSE(UnescapeString("\\u000G0", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: \\u must be followed by 4 hex digits but saw: "
      "\\u000G",
      error_string);

  EXPECT_FALSE(UnescapeString("\\U00", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: \\U must be followed by 8 hex digits but saw: "
      "\\U00",
      error_string);

  EXPECT_FALSE(UnescapeString("\\U000000G00", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: \\U must be followed by 8 hex digits but saw: "
      "\\U000000G0",
      error_string);

  EXPECT_FALSE(UnescapeString("\\U0000D83D", &out, &error_string).ok());
  EXPECT_EQ("Illegal escape sequence: Unicode value \\U0000D83D is invalid",
            error_string);

  EXPECT_FALSE(UnescapeString("\\UFFFFFFFF0", &out, &error_string).ok());
  EXPECT_EQ(
      "Illegal escape sequence: Value of \\UFFFFFFFF exceeds Unicode limit "
      "(0x0010FFFF)",
      error_string);
}

TEST(StringsTest, TestFatalInPlaceUnescaping) {
  std::string parse_string = "a";
  EXPECT_DEATH(UnescapeString(parse_string, &parse_string).IgnoreError(),
               "Source and destination cannot be the same");
  EXPECT_DEATH(ParseStringLiteral(parse_string, &parse_string).IgnoreError(),
               "Source and destination cannot be the same");
}

TEST(StringsTest, IdentifierPathToString) {
  // The inverses of these tests are also tested with ParseIdentifierPath.
  EXPECT_EQ("", IdentifierPathToString(std::vector<std::string>{}));
  EXPECT_EQ("abc", IdentifierPathToString({"abc"}));
  EXPECT_EQ("abc.def", IdentifierPathToString({"abc", "def"}));
  EXPECT_EQ("`a c`.def.`g h`", IdentifierPathToString({"a c", "def", "g h"}));
  // Empty identifiers are illegal, but will still print here.
  EXPECT_EQ("``.A.``", IdentifierPathToString({"", "A", ""}));

  EXPECT_EQ("", IdentifierPathToString(std::vector<IdString>{}));
  EXPECT_EQ("`a,c`.def",
            IdentifierPathToString({IdString::MakeGlobal("a,c"),
                                    IdString::MakeGlobal("def")}));
  EXPECT_EQ("a.`all`", IdentifierPathToString({"a", "all"}));
  EXPECT_EQ("a.all", IdentifierPathToString({"a", "all"},
                                            /*quote_reserved_keywords=*/false));
  EXPECT_EQ("`all`.a", IdentifierPathToString({"all", "a"},
      /*quote_reserved_keywords=*/false));

  EXPECT_EQ("`all`.all.all", IdentifierPathToString({"all", "all", "all"},
      /*quote_reserved_keywords=*/false));
  EXPECT_EQ("a.all.a.all", IdentifierPathToString({"a", "all", "a", "all"},
      /*quote_reserved_keywords=*/false));
}

// Input for a ParseIdentifierPath test. If 'error' is empty the test is assumed
// to have succeeded.
struct ParseIdentifierPathTestCase {
  std::string name;
  std::string input;
  std::vector<std::string> output;
  std::string error;

  ParseIdentifierPathTestCase(const std::string& name, const std::string& input,
                              const std::vector<std::string>& output,
                              const std::string& error = "")
      : name(name), input(input), output(output), error(error) {}
};

TEST(StringsTest, ParseIdentifierPath) {
  const std::vector<ParseIdentifierPathTestCase> test_cases({
      // Test format:
      // name | input | output | error

      // Success.
      {"SingleIdentifier", "abc", {"abc"}},
      {"SingleDot", "abc.`def`", {"abc", "def"}},
      {"NumericPathTicks", "abc.`123`", {"abc", "123"}},
      {"NumericPathNoTicks", "abc.123", {"abc", "123"}},
      {"NumericPathBothTicks", "abc.`123`.456", {"abc", "123", "456"}},
      {"EscapedBackslash", "`a\\\\`.bc", {"a\\", "bc"}},
      {"EscapedBackquote", "`\\``.ghi", {"`", "ghi"}},
      {"UnquotedKeywords",
       "abc.select.from.table",
       {"abc", "select", "from", "table"}},
      {"QuotedAndUnquotedKeywords",
       "abc.`select`.from.`table`",
       {"abc", "select", "from", "table"}},
      {"EscapedWithSpaces", "`a c`.def.`g h`", {"a c", "def", "g h"}},
      {"DotInBackquotes", "abc.def.`ghi.jkl`", {"abc", "def", "ghi.jkl"}},
      {"HexDotsBackquotes", "`abc\\x2e\\x60ghi`", {"abc.`ghi"}},
      {"EscapedNewlinesHex",
       "`abc\\ndef\\x12ghi`.suffix",
       {"abc\ndef\x12ghi", "suffix"}},
      {"HexToUnicode",
       "`\xd0\x9e\xd0\xbb\xd1\x8f.abc`.`\\U0010FFFD`",
       {"\xd0\x9e\xd0\xbb\xd1\x8f.abc", "\xf4\x8f\xbf\xbd"}},
      {"EscapedUnicode",
       "abc.`\\\"\xe8\xb0\xb7\xe6\xad\x8c\\\" is Google\\\'s Chinese name`",
       {"abc", "\"\xE8\xB0\xB7\xE6\xAD\x8C\" is Google's Chinese name"}},

      // Failure.
      {"EmptyString", "", {}, "cannot be empty"},
      {"NullByte", "\0", {}, "cannot be empty"},
      {"EmptyPathComponent", "abc..def", {}, "empty path component"},
      {"InvalidIdentifier", "abc.def`12`", {}, "invalid character"},
      {"BackquoteStart", "abc.`defghi", {}, "contains an unmatched `"},
      {"BackquoteMiddle", "abc.def`ghi", {}, "invalid character"},
      {"BackquoteTrailing", "abc.def`", {}, "invalid character"},
      {"BackquoteConsecutive", "`abc``def`", {}, "invalid character"},
      {"LeadingDot", ".abc.def.", {}, "cannot begin with `.`"},
      {"TrailingDot", "abc.def.", {}, "cannot end with `.`"},
      {"AllWhitespace", "     ", {}, "invalid character"},
      {"LeadingWhitespace", "    abc.123", {}, "invalid character"},
      {"TrailingWhitespace", "abc.123\\n", {}, "invalid character"},
      {"WhitespacePreIdentifier",
       "abc.\\r\\n\\t   123",
       {},
       "invalid character"},
      {"WhitespacePostIdentifier", "abc.123\t.def", {}, "invalid character"},
      {"WhitespaceWithinComponent", "abc.123 def", {}, "invalid character"},
      {"InvalidEscape", "abc.`'\\e'`", {}, "Illegal escape sequence"},
      {"InvalidUTF8", "abc.`\xe0\x80\x80`", {}, "invalid UTF8 string"},
      {"TrailingEscape", "abc.def\\", {}, "invalid character"},
      {"PathStartsWithNumber0", "123", {}, "Invalid identifier"},
      {"PathStartsWithNumber1", "123abc", {}, "Invalid identifier"},
      {"PathStartsWithNumber2", "123.abc", {}, "Invalid identifier"},
      {"PathHasComment", "abc./*comment*/def", {}, "invalid character"},
      // Assorted unescaped expressions.
      {"UnescapedExpression0", "(abc)", {}, "invalid character"},
      {"UnescapedExpression1", "+abc", {}, "invalid character"},
      {"UnescapedExpression2", "'abc'", {}, "invalid character"},
      {"UnescapedExpression3", "abc+def", {}, "invalid character"},
      {"UnescapedExpression4", "\"abc+def\"", {}, "invalid character"},
      {"UnescapedExpression5", "\"abc.def\"", {}, "invalid character"},
  });

  // By default, test cases should produce the expected result regardless of
  // whether FEATURE_ALLOW_SLASH_PATHS is enabled.
  for (const bool enable_slash_paths : {true, false}) {
    LanguageOptions language_options;
    if (enable_slash_paths) {
      language_options.EnableLanguageFeature(FEATURE_ALLOW_SLASH_PATHS);
    }
    for (const ParseIdentifierPathTestCase& test : test_cases) {
      // Also prepare an input char array without null terminator at the end.
      // This helps address sanitizer to validate that we stay within address
      // boudaries.
      std::unique_ptr<char[]> input_buf(new char[test.input.size()]());
      std::strncpy(input_buf.get(), test.input.c_str(), test.input.size());
      absl::string_view input_no_null_term(input_buf.get(), test.input.size());
      std::vector<std::string> path;
      if (test.error.empty()) {
        // Success.
        GOOGLESQL_EXPECT_OK(ParseIdentifierPath(test.input, language_options, &path))
            << test.input << " ERROR: parse failure";
        EXPECT_EQ(test.output, path)
            << test.name << " ERROR: unexpected output";
        // Also test that parsing works for non-null-terminated string views.
        GOOGLESQL_EXPECT_OK(
            ParseIdentifierPath(input_no_null_term, language_options, &path))
            << test.input << " ERROR: parse failure without null terminator";
        EXPECT_EQ(test.output, path)
            << test.name << " ERROR: unexpected output without null terminator";

        // Run each path through the parser to ensure that the vectors match.
        std::unique_ptr<ParserOutput> parser_output;
        ParserOptions parser_options;
        parser_options.set_language_options(language_options);
        GOOGLESQL_ASSERT_OK(ParseExpression(test.input, parser_options, &parser_output));
        const ASTPathExpression* parsed_path =
            parser_output->expression()->GetAs<ASTPathExpression>();
        EXPECT_EQ(parsed_path->ToIdentifierVector(), path)
            << test.name << " ERROR: parsed expression mismatch";
      } else {
        // Failure.
        EXPECT_THAT(
            ParseIdentifierPath(test.input, language_options, &path),
            StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(test.error)))
            << test.name << " ERROR: unexpected failure status";
        // Also test error scenarios for non-null-terminated string views.
        EXPECT_THAT(
            ParseIdentifierPath(input_no_null_term, language_options, &path),
            StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(test.error)))
            << test.name
            << " ERROR: unexpected failure status without null terminator";

        // Ensure that the output has not changed.
        EXPECT_EQ(test.output, path) << test.name << " ERROR: output modified";
      }
    }
  }
}

TEST(StringsTest, ParseIdentifierPathWithSlashes) {
  // All test cases are expected to produce an error if
  // FEATURE_ALLOW_SLASH_PATHS is disabled, otherwise they produce success
  // or error as indicated in the TestCase.
  const std::vector<ParseIdentifierPathTestCase> test_cases({
      // Test format:
      // name | input | output | error

      // Success.
      {"PathWithSingleSlash", "/root", {"/root"}},
      {"PathWithSlashes", "/root/db", {"/root/db"}},
      {"PathWithSlashAndDash", "/root/db/my-mdb-grp", {"/root/db/my-mdb-grp"}},
      {"PathWithSlashDashAndColon",
       "/root/db/my-mdb-grp:my_db",
       {"/root/db/my-mdb-grp:my_db"}},
      {"MultiComponentSlashDashAndColon",
       "/root/db/my-mdb-grp:my_db.MyTable.MyColumn",
       {"/root/db/my-mdb-grp:my_db", "MyTable", "MyColumn"}},

      // Failure.
      {"DashAfterFirstPathComponent",
       "/root.dataset-sub.Table",
       {},
       "invalid character"},
      {"PathEndingInSlash", "/root/", {}, "cannot end with `/`"},
      {"PathEndingInSlashMulti",
       "/root/global/table/",
       {},
       "cannot end with `/`"},
      {"PathEndingInColon", "/root:", {}, "cannot end with `:`"},
      {"PathEndingInDash", "/root-", {}, "cannot end with `-`"},
      {"ConsecutiveSlash",
       "/root//global.dataset-sub.Table",
       {},
       "invalid character"},
      {"ConsecutiveSlashDash",
       "/root:/global.dataset-sub.Table",
       {},
       "invalid character"},
      {"PathWithReservedKeyword1", "/END", {}, "reserved keyword"},
      {"PathWithReservedKeyword2", "/abc/END", {}, "reserved keyword"},
      {"PathWithReservedKeyword3", "/abc/END/def", {}, "reserved keyword"},
      {"SlashNextToDot", "/root/global/.Table", {}, "invalid character"},
  });

  for (const bool enable_slash_paths : {true, false}) {
    LanguageOptions language_options;
    if (enable_slash_paths) {
      language_options.EnableLanguageFeature(FEATURE_ALLOW_SLASH_PATHS);
    }
    for (const ParseIdentifierPathTestCase& test : test_cases) {
      // Also prepare an input char array without null terminator at the end.
      // This helps address sanitizer to validate that we stay within address
      // boudaries.
      std::unique_ptr<char[]> input_buf(new char[test.input.size()]());
      std::strncpy(input_buf.get(), test.input.c_str(), test.input.size());
      absl::string_view input_no_null_term(input_buf.get(), test.input.size());
      std::vector<std::string> path;

      if (enable_slash_paths && test.error.empty()) {
        // Success.
        GOOGLESQL_EXPECT_OK(ParseIdentifierPath(test.input, language_options, &path))
            << test.input << " ERROR: parse failure";
        EXPECT_EQ(test.output, path)
            << test.name << " ERROR: unexpected output";
        // Also test that parsing works for non-null-terminated string views.
        GOOGLESQL_EXPECT_OK(
            ParseIdentifierPath(input_no_null_term, language_options, &path))
            << test.input << " ERROR: parse failure without null terminator";
        EXPECT_EQ(test.output, path)
            << test.name << " ERROR: unexpected output without null terminator";

        // Verify that the parser produces the same result. Paths that start
        // with '/' are not allowed in expressions, but we can pass
        // them to the parser by substituting the string into a query "SELECT *
        // FROM <str>".
        std::unique_ptr<googlesql::ParserOutput> parser_output;
        googlesql::ParserOptions parser_options;
        parser_options.set_language_options(language_options);
        GOOGLESQL_ASSERT_OK(googlesql::ParseStatement(
            absl::StrFormat("SELECT * FROM %s", test.input), parser_options,
            &parser_output));
        const googlesql::ASTPathExpression* path_expr =
            parser_output->statement()
                ->GetAsOrDie<googlesql::ASTQueryStatement>()
                ->query()
                ->query_expr()
                ->GetAsOrDie<googlesql::ASTSelect>()
                ->from_clause()
                ->table_expression()
                ->GetAsOrDie<googlesql::ASTTablePathExpression>()
                ->path_expr();
        EXPECT_EQ(path, path_expr->ToIdentifierVector())
            << test.name << " ERROR: parsed expression mismatch";
      } else {
        const std::string expected_error_message =
            enable_slash_paths ? test.error : "invalid character '/'";
        // Failure.
        EXPECT_THAT(ParseIdentifierPath(test.input, language_options, &path),
                    StatusIs(absl::StatusCode::kInvalidArgument,
                             HasSubstr(expected_error_message)))
            << test.name << " ERROR: unexpected failure status";
        // Also test error scenarios for non-null-terminated string views.
        EXPECT_THAT(
            ParseIdentifierPath(input_no_null_term, language_options, &path),
            StatusIs(absl::StatusCode::kInvalidArgument,
                     HasSubstr(expected_error_message)))
            << test.name
            << " ERROR: unexpected failure status without null terminator";
      }
    }
  }
}

TEST(StingsTest, IsValidUnquotedIdentifier) {
  LanguageOptions language_options_default;
  LanguageOptions language_options_all_on;
  language_options_all_on.EnableMaximumLanguageFeaturesForDevelopment();

  for (bool allow_reserved_keywords : {true, false}) {
    // Keywords.
    EXPECT_THAT(IsValidUnquotedIdentifier("select", language_options_default,
                                          allow_reserved_keywords),
                Eq(allow_reserved_keywords));
    EXPECT_THAT(IsValidUnquotedIdentifier("sElEcT", language_options_default,
                                          allow_reserved_keywords),
                Eq(allow_reserved_keywords));
    EXPECT_TRUE(IsValidUnquotedIdentifier("row", language_options_default,
                                          allow_reserved_keywords));
    EXPECT_TRUE(IsValidUnquotedIdentifier("QUALIFY", language_options_default,
                                          allow_reserved_keywords));
    EXPECT_TRUE(IsValidUnquotedIdentifier("qualify", language_options_default,
                                          allow_reserved_keywords));

    // Quoted.
    EXPECT_FALSE(IsValidUnquotedIdentifier("`abc`", language_options_all_on,
                                           allow_reserved_keywords));
    EXPECT_FALSE(IsValidUnquotedIdentifier("`abc", language_options_all_on,
                                           allow_reserved_keywords));
    EXPECT_FALSE(IsValidUnquotedIdentifier("abc`", language_options_all_on,
                                           allow_reserved_keywords));
    EXPECT_FALSE(IsValidUnquotedIdentifier("'a'", language_options_all_on,
                                           allow_reserved_keywords));

    // Invalid characters.
    EXPECT_FALSE(IsValidUnquotedIdentifier("3", language_options_default,
                                           allow_reserved_keywords));
    EXPECT_FALSE(IsValidUnquotedIdentifier("abc,", language_options_default,
                                           allow_reserved_keywords));

    // Others.
    EXPECT_TRUE(IsValidUnquotedIdentifier("selected", language_options_all_on,
                                          allow_reserved_keywords));
    EXPECT_TRUE(IsValidUnquotedIdentifier("_", language_options_all_on,
                                          allow_reserved_keywords));
    EXPECT_TRUE(IsValidUnquotedIdentifier("_1", language_options_all_on,
                                          allow_reserved_keywords));
    EXPECT_FALSE(IsValidUnquotedIdentifier("", language_options_all_on,
                                           allow_reserved_keywords));
  }
}

TEST(StingsTest, IsKeyword) {
  EXPECT_TRUE(IsKeyword("select"));
  EXPECT_TRUE(IsKeyword("sElEcT"));
  EXPECT_FALSE(IsKeyword("selected"));
  EXPECT_TRUE(IsKeyword("row"));
  EXPECT_FALSE(IsKeyword(""));
  EXPECT_TRUE(IsKeyword("QUALIFY"));
  EXPECT_TRUE(IsKeyword("qualify"));
}

TEST(StingsTest, IsReservedKeyword) {
  EXPECT_TRUE(IsReservedKeyword("select"));
  EXPECT_TRUE(IsReservedKeyword("sElEcT"));
  EXPECT_FALSE(IsReservedKeyword("selected"));
  EXPECT_FALSE(IsReservedKeyword("row"));
  EXPECT_FALSE(IsReservedKeyword(""));

  // For compatibility reasons, IsReservedKeyword() considers conditionally
  // reserved keywords to not be reserved.
  EXPECT_FALSE(IsReservedKeyword("QUALIFY"));
}

TEST(StingsTest, GetReservedKeywords) {
  const absl::flat_hash_set<std::string>& keywords = GetReservedKeywords();
  EXPECT_GT(keywords.size(), 70);
  EXPECT_LT(keywords.size(), 120);
  EXPECT_TRUE(keywords.contains("FROM"));
  EXPECT_FALSE(keywords.contains("from"));
  EXPECT_FALSE(keywords.contains("TABLE"));
  EXPECT_FALSE(keywords.contains(""));

  // For compatibility reasons, GetReservedKeywords() considers conditionally
  // reserved keywords to not be reserved.
  EXPECT_FALSE(IsReservedKeyword("QUALIFY"));
}

TEST(StingsTest, IsInternalAlias) {
  EXPECT_TRUE(IsInternalAlias("$col1"));
  EXPECT_FALSE(IsInternalAlias("col1"));
  EXPECT_FALSE(IsInternalAlias(""));

  EXPECT_TRUE(IsInternalAlias(IdString::MakeGlobal("$col1")));
  EXPECT_FALSE(IsInternalAlias(IdString::MakeGlobal("col1")));
  EXPECT_FALSE(IsInternalAlias(IdString()));
}

TEST(StringsTest, ConditionallyReservedKeywordsMiscIdentifierFunctions) {
  LanguageOptions qualify_reserved;
  GOOGLESQL_ASSERT_OK(qualify_reserved.EnableReservableKeyword("QUALIFY"));

  // ParseIdentifier()
  std::string out;
  GOOGLESQL_EXPECT_OK(
      ParseIdentifier("QUALIFY", LanguageOptions(), &out, nullptr, nullptr));
  EXPECT_THAT(ParseIdentifier("QUALIFY", qualify_reserved, &out),
              StatusIs(absl::StatusCode::kInvalidArgument));

  // ParseGeneralizedIdentifier()
  GOOGLESQL_EXPECT_OK(ParseGeneralizedIdentifier("QUALIFY", &out));

  // ToIdentifierLiteral()
  EXPECT_EQ("`QUALIFY`", ToIdentifierLiteral("QUALIFY"));

  // IdentifierPathToString()
  EXPECT_EQ("`QUALIFY`.`QUALIFY`",
            IdentifierPathToString({"QUALIFY", "QUALIFY"}, true));
  EXPECT_EQ("`QUALIFY`.QUALIFY",
            IdentifierPathToString({"QUALIFY", "QUALIFY"}, false));

  // ParseIdentifierPath()
  std::vector<std::string> path;
  GOOGLESQL_EXPECT_OK(ParseIdentifierPath("QUALIFY.QUALIFY", LanguageOptions(), &path));
  EXPECT_THAT(path,
              ::testing::Eq(std::vector<std::string>{"QUALIFY", "QUALIFY"}));

  EXPECT_THAT(ParseIdentifierPath("QUALIFY.QUALIFY", qualify_reserved, &path),
              StatusIs(absl::StatusCode::kInvalidArgument));
  GOOGLESQL_EXPECT_OK(ParseIdentifierPath("`QUALIFY`.QUALIFY", qualify_reserved, &path));
  EXPECT_THAT(path,
              ::testing::Eq(std::vector<std::string>{"QUALIFY", "QUALIFY"}));
}
}  // namespace googlesql
