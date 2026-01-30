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

#include <string>
#include <vector>

#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
// This 3-char string has a 3-byte utf8 character in it.  It is "a?b".
constexpr absl::string_view kUtf8String =
    "a"
    "\u8C37"
    "b";
}  // namespace

// Test cases for both binary comparison and comparison with collation with
// STRING or BYTES.
std::vector<QueryParamsWithResult> GetFunctionTestsLikeCommon() {
  // NOTE: We are not compliance testing invalid utf8 on the lhs of LIKE.
  // The language doesn't have a rule for whether an error should happen here.

  // All one-byte chars that aren't LIKE control chars and are valid in utf8.
  std::string all_chars;
  for (int i = 1; i < 128; ++i) {
    if (i != '%' && i != '_' && i != '\\') {
      all_chars += static_cast<char>(i);
    }
  }
  // High ascii one-byte chars valid in bytes but not strings.
  std::string all_bytes;
  for (int i = 128; i < 256; ++i) {
    all_bytes += static_cast<char>(i);
  }

  const std::string a0c = std::string("a\0c", 3);
  ABSL_CHECK_EQ(a0c.size(), 3);

  // Tests for value LIKE value -> bool that work for string or bytes.
  std::vector<QueryParamsWithResult> common_tests = {
      {{"", ""}, True()},
      {{"abc", "abc"}, True()},
      {{"ABc", NullString()}, NullBool()},
      {{NullString(), "abc"}, NullBool()},
      {{NullString(), NullString()}, NullBool()},

      {{"abc", ""}, False()},
      {{"", "abc"}, False()},
      {{"abc", "ab"}, False()},
      {{"ab", "abc"}, False()},

      // % as a suffix.
      {{"abc", "abc%"}, True()},
      {{"abcd", "abc%"}, True()},
      {{"abcde", "abc%"}, True()},
      {{"ab", "abc%"}, False()},
      {{"acc", "abc%"}, False()},
      {{"xabc", "abc%"}, False()},
      {{"bc", "abc%"}, False()},

      // % as a prefix.
      {{"abc", "%abc"}, True()},
      {{"xabc", "%abc"}, True()},
      {{"xxabc", "%abc"}, True()},
      {{"bc", "%abc"}, False()},
      {{"acc", "%abc"}, False()},
      {{"abcx", "%abc"}, False()},
      {{"ab", "%abc"}, False()},

      // % in the middle.
      {{"abc", "a%c"}, True()},
      {{"ac", "a%c"}, True()},
      {{"axxxxxxc", "a%c"}, True()},
      {{"a%c", "a%c"}, True()},
      {{"a%%c", "a%c"}, True()},
      {{"acx", "a%c"}, False()},
      {{"xac", "a%c"}, False()},

      {{"abcdef", "ab%cd%ef"}, True()},
      {{"abxxxxcdyyyef", "ab%cd%ef"}, True()},
      {{"abxxxxcyyyef", "ab%cd%ef"}, False()},

      // Escaping \%.
      {{"abc", "ab\\%"}, False()},
      {{"ab%", "ab\\%"}, True()},
      {{"ab\\%", "ab\\%"}, False()},
      {{"ab\\", "ab\\%"}, False()},
      {{"ab\\x", "ab\\%"}, False()},
      {{"ab\\xx", "ab\\%"}, False()},

      // Escaping \_.
      {{"ab", "ab\\_"}, False()},
      {{"ab_", "ab\\_"}, True()},
      {{"abc", "ab\\_"}, False()},
      {{"ab\\x", "ab\\_"}, False()},
      {{"ab\\_", "ab\\_"}, False()},

      // Escaping \\.
      {{"a\\c", "a\\\\c"}, True()},
      {{"a\\\\c", "a\\\\c"}, False()},
      {{"abc", "a\\\\c"}, False()},
      {{"abc", "a\\\\%c"}, False()},
      {{"a\\c", "a\\\\%c"}, True()},
      {{"a\\xxxc", "a\\\\%c"}, True()},

      // Invalid escaping - odd numbers of trailing escapes.
      {{"", "\\"}, False(), OUT_OF_RANGE},
      {{"\\", "\\\\"}, True()},
      {{"", "\\\\\\"}, False(), OUT_OF_RANGE},

      // Escaping other characters is allowed.
      {{"a", "\\a"}, True()},
      {{"b", "\\a"}, False()},
      {{"\\a", "\\a"}, False()},

      // Regex chars not leaking through.  (Tested more with all_chars.)
      {{"abc", "a.c"}, False()},
      {{"a.c", "a.c"}, True()},
      {{"abc", "a.*"}, False()},
      {{"a.*", "a.*"}, True()},

      // \0 char doesn't truncate or cause problems.
      {{a0c, "a"}, False()},
      {{"a", a0c}, False()},
      {{a0c, "abc"}, False()},
      {{"abc", a0c}, False()},
      {{a0c, a0c}, True()},

      {{all_chars, all_chars}, True()},

      // Some more complicated cases.
      {{"foobarfoo", "%bar%"}, True()},
      {{"foobar", "foo%bar"}, True()},
      {{"foobarfoobarfoo", "foo%foo"}, True()},
      {{"foobarfoobarfoo", "foo%bar"}, False()},
      {{"foobarbarbaz", "%foo%foo%"}, False()},
      {{"ababa", "aba%aba"}, False()},
      {{"ababa", "abababa"}, False()},
      {{"abababa", "ababa"}, False()},
      {{"%barfoobar", "\\%barfoo%"}, True()},
      {{"barfoobar", "\\%foo%"}, False()},
      {{"barfoobar", "bar\\%foobar"}, False()},
      {{"_barfoobar", "\\_barfoo%"}, True()},
      {{"barfoobar", "\\_foo%"}, False()},
      {{"barfoobar", "bar\\_foobar"}, False()},
      {{"", "%%%%%"}, True()},
      {{"barfoobar", "%%%%%"}, True()},
      {{"barfoobar", ""}, False()},
  };

  // Starting with all possible non-UTF8 chars (except 0, %, _), if we change
  // any one char on either side, LIKE returns false.  That's too many tests,
  // so we do them in three batches, stripping every third character.
  const int kNumBatches = 3;
  for (int batch = 0; batch < kNumBatches; ++batch) {
    std::string modified_chars = all_chars;
    for (int i = batch; i < all_chars.size(); i += kNumBatches) {
      if (all_chars[i] == 'A') continue;
      modified_chars[i] = 'A';
    }
    common_tests.push_back(
        QueryParamsWithResult({all_chars, modified_chars}, False()));
    common_tests.push_back(
        QueryParamsWithResult({modified_chars, all_chars}, False()));
  }

  return common_tests;
}

// Test cases for both binary comparison and comparison with collation with
// STRING.
std::vector<QueryParamsWithResult> GetFunctionTestsLikeStringCommon() {
  ABSL_LOG(INFO) << "kUtf8String: " << kUtf8String;
  return {
      {{kUtf8String, kUtf8String}, True()},

      // Invalid utf8 character in the pattern.
      {{"", "\xC2"}, False(), OUT_OF_RANGE},

      {{kUtf8String, "a%b"}, True()},

      // Splitting the utf8 character in the pattern will not work.
      // We should get an error.
      {{kUtf8String, absl::StrCat(kUtf8String.substr(0, 2), "%")},
       False(),
       OUT_OF_RANGE},
      {{kUtf8String, absl::StrCat(kUtf8String.substr(0, 3))},
       False(),
       OUT_OF_RANGE},
  };
}

// Test cases for binary comparison with STRING.
std::vector<QueryParamsWithResult> GetFunctionTestsLikeString() {
  std::vector<QueryParamsWithResult> common_tests =
      GetFunctionTestsLikeCommon();

  // Tests for value LIKE value -> bool that work for string or bytes.
  std::vector<QueryParamsWithResult> binary_common_tests = {
      {{"abc", "ABC"}, False()},
      {{"ABC", "abc"}, False()},
      {{"ABc", "aBC"}, False()},

      // _
      {{"abc", "_bc"}, True()},
      {{"abc", "a_c"}, True()},
      {{"ac", "a_c"}, False()},
      {{"abbc", "a_c"}, False()},
      {{"abc", "ab_"}, True()},
      {{"abc", "___"}, True()},
      {{"abcd", "___"}, False()},
      {{"aXcXeX", "a_c_e_"}, True()},
  };

  common_tests.insert(common_tests.end(), binary_common_tests.begin(),
                      binary_common_tests.end());

  std::vector<QueryParamsWithResult> string_tests =
      GetFunctionTestsLikeStringCommon();

  // Tests that work on STRING but not BYTES.
  std::vector<QueryParamsWithResult> binary_string_tests = {
      // The utf8 character counts as one character in binary comparison.
      {{kUtf8String, "a_b"}, True()},
      {{kUtf8String, "a_%b"}, True()},
      {{kUtf8String, "a__b"}, False()},
      {{kUtf8String, "a__%b"}, False()},
      {{kUtf8String, "a___b"}, False()},

      // Splitting the utf8 character in the pattern will not work.
      // We should get an error.
      {{kUtf8String, absl::StrCat(kUtf8String.substr(0, 3), "_")},
       False(),
       OUT_OF_RANGE},
      {{kUtf8String, absl::StrCat(kUtf8String.substr(0, 3), "__")},
       False(),
       OUT_OF_RANGE},
  };

  string_tests.insert(string_tests.end(), binary_string_tests.begin(),
                      binary_string_tests.end());

  std::vector<QueryParamsWithResult> all_tests;
  all_tests.insert(all_tests.end(), common_tests.begin(), common_tests.end());
  all_tests.insert(all_tests.end(), string_tests.begin(), string_tests.end());

  return all_tests;
}

// Test cases for binary comparison with STRING and BYTES.
std::vector<QueryParamsWithResult> GetFunctionTestsLike() {
  // High ascii one-byte chars valid in bytes but not strings.
  std::string all_bytes;
  for (int i = 128; i < 256; ++i) {
    all_bytes += static_cast<char>(i);
  }

  std::vector<QueryParamsWithResult> string_tests =
      GetFunctionTestsLikeString();

  // Tests that work on BYTES but not STRING.
  std::vector<QueryParamsWithResult> bytes_tests = {
      {{Bytes(all_bytes), Bytes(all_bytes)}, True()},

      // The utf8 character counts as three bytes.
      {{Bytes(kUtf8String), Bytes("a%b")}, True()},
      {{Bytes(kUtf8String), Bytes("a_b")}, False()},
      {{Bytes(kUtf8String), Bytes("a__b")}, False()},
      {{Bytes(kUtf8String), Bytes("a___b")}, True()},
      {{Bytes(kUtf8String), Bytes("a____b")}, False()},

      // Splitting the utf8 character will work fine.
      {{Bytes(kUtf8String), Bytes(absl::StrCat(kUtf8String.substr(0, 2), "%"))},
       True()},
      {{Bytes(kUtf8String), Bytes(absl::StrCat(kUtf8String.substr(0, 3), "%"))},
       True()},
      {{Bytes(kUtf8String),
        Bytes(absl::StrCat(kUtf8String.substr(0, 3), "%b"))},
       True()},
      {{Bytes(kUtf8String),
        Bytes(absl::StrCat(kUtf8String.substr(0, 3), "__"))},
       True()},
      {{Bytes(kUtf8String),
        Bytes(absl::StrCat(kUtf8String.substr(0, 3), "___"))},
       False()},
      {{Bytes(kUtf8String), Bytes(absl::StrCat(kUtf8String.substr(0, 2), "_",
                                               kUtf8String.substr(3)))},
       True()},
  };

  // Starting with all possible non-UTF8 chars (except 0, %, _), if we change
  // any one char on either side, LIKE returns false.  That's too many tests,
  // so we do them in three batches, stripping every third character.
  const int kNumBatches = 3;
  for (int batch = 0; batch < kNumBatches; ++batch) {
    std::string modified_bytes = all_bytes;
    for (int i = batch; i < all_bytes.size(); i += kNumBatches) {
      modified_bytes[i] = 'A';
    }
    bytes_tests.push_back(QueryParamsWithResult(
        {Bytes(all_bytes), Bytes(modified_bytes)}, False()));
    bytes_tests.push_back(QueryParamsWithResult(
        {Bytes(modified_bytes), Bytes(all_bytes)}, False()));
  }

  std::vector<QueryParamsWithResult> all_tests;
  all_tests.insert(all_tests.end(), string_tests.begin(), string_tests.end());
  all_tests.insert(all_tests.end(), bytes_tests.begin(), bytes_tests.end());

  // Add the BYTES version of all tests in common_tests.
  for (const QueryParamsWithResult& test : GetFunctionTestsLikeCommon()) {
    all_tests.push_back(QueryParamsWithResult(
        {StringToBytes(test.param(0)), StringToBytes(test.param(1))},
        test.result(), test.status()));
  }

  return all_tests;
}

// Test cases for STRING with collation.
std::vector<QueryParamsWithResult> GetFunctionTestsLikeWithCollation() {
  std::vector<QueryParamsWithResult> common_tests =
      GetFunctionTestsLikeCommon();

  // Tests for value LIKE value -> bool that work for string with collation.
  std::vector<QueryParamsWithResult> collation_common_tests = {
      {{"abc", "ABC"}, True()},
      {{"ABC", "abc"}, True()},
      {{"ABc", "aBC"}, True()},

      // _
      {{"abc", "_bc"}, False(), OUT_OF_RANGE},
      {{"abc", "a_c"}, False(), OUT_OF_RANGE},
      {{"ac", "a_c"}, False(), OUT_OF_RANGE},
      {{"abbc", "a_c"}, False(), OUT_OF_RANGE},
      {{"abc", "ab_"}, False(), OUT_OF_RANGE},
      {{"abc", "___"}, False(), OUT_OF_RANGE},
      {{"abcd", "___"}, False(), OUT_OF_RANGE},
      {{"aXcXeX", "a_c_e_"}, False(), OUT_OF_RANGE},

      // Ignorable characters.
      {{"", "\u0001"}, True()},
      {{"", "\u0001%%"}, True()},
      {{"", "%\u0001%"}, True()},
      {{"", "%%\u0001"}, True()},
      {{"foo", "foo\u0001"}, True()},
      {{"foo", "\u0001foo"}, True()},
      {{"foobar", "foo%\u0001%bar"}, True()},
      {{"foobar", "foo%\u0001%foo"}, False()},
      {{"foobar", "foo%\u0001%foo%bar"}, False()},
      {{"foobar", "foo\u0001bar"}, True()},
      {{"foobar", "foo%\u0001bar"}, True()},
      {{"foobar", "foo%\u0001bar%baz"}, False()},
      {{"foobarbaz", "foo%\u0001bar%baz"}, True()},
      {{"foobar", "foo%bar\u0001"}, True()},
      {{"ababa", "aba%\u0001%aba"}, False()},
      {{"ababa", "aba\u0001ba"}, True()},
      {{"\u0001", ""}, True()},
      {{"\u0001foobar", "foobar"}, True()},
      {{"foo\u0001bar", "foobar"}, True()},
      {{"foobar\u0001", "foobar"}, True()},
      {{"foobar\u0001", "foobar%"}, True()},
      {{"\u0001foobar", "%foobar"}, True()},
      {{"foo\u0001bar", "foo%bar"}, True()},
      {{"ab\u0001aba", "aba%aba"}, False()},

      // Below cases could have different results for different collations.
      // Using und:ci as an example.
      {{"CamelCase", "camelcase"}, True()},
      {{"CamelCase", "CAMELCASE"}, True()},
      {{"camelcase", "CamelCase"}, True()},
      {{"CAMELCASE", "CamelCase"}, True()},
      {{"foo\u0001bar", "FOOBAR"}, True()},
      {{"foobar\u0001", "%FOO%BAR%"}, True()},
      {{"foobar", "%%%%%FOOBAR"}, True()},
      {{"foobar", "FOO%%%%%BAR"}, True()},
      {{"foobar", "FOOBAR%%%%%"}, True()},
      {{"%barfoobar", "\\%BarFoo%"}, True()},
      {{"barfoobar", "\\%Foo%"}, False()},
      {{"barfoobar", "Bar\\%FooBar"}, False()},
      {{"_barfoobar", "\\_BarFoo%"}, True()},
      {{"barfoobar", "\\_Foo%"}, False()},
      {{"barfoobar", "bar\\_Foobar"}, False()},
      {{"foobarfoobarfoo", "Foo%Foo"}, True()},
      {{"foobarfoobarfoo", "Foo%Bar"}, False()},
      {{"foobarbarbaz", "%Foo%Foo%"}, False()},
      {{"ababa", "aBa%AbA"}, False()},
      {{"ababa", "AbAbAbA"}, False()},
      {{"abababa", "aBaBa"}, False()},
      // 'ß' and 'ẞ'
      {{"\u1E9E", "\u00DF"}, True()},
      // 'Å' and 'A''◌̊'
      {{"\u0041\u030A", "\u00C5"}, True()},
      // 'ự' and 'u''◌̣''◌̛'
      {{"\u0075\u031B\u0323", "\u1EF1"}, True()},
      {{"\u1EF1", "\u0075%\u0323"}, False()},
      {{"\u1EF1", "u%"}, False()},
      {{"ア", "あ"}, True()},
      // "MW" and '㎿'
      {{"\u33BF", "MW"}, True()},
      {{"M\u33BF", "MM%"}, False()},
      {{"Å", "A"}, False()},
      // 'ß' and "SS"
      {{"SS", "\u00DF"}, False()},
      // 's' and 'ſ'
      {{"\u017F", "s"}, False()},
      // "1/2" and '½'
      {{"\u00BD", "1/2"}, False()},
      // Strings are not normalized.
      {{"\u0078\u0323\u031B", "\u0078\u031B\u0323"}, False()},
      // 'Å' and 'a''◌̊'
      {{"\u0061\u030A", "\u00C5"}, True()},
      // 'ự' and 'U''◌̣''◌̛'
      {{"\u0055\u031B\u0323", "\u1EF1"}, True()},
      // "mw" and '㎿'
      {{"\u33BF", "mw"}, True()},
  };

  common_tests.insert(common_tests.end(), collation_common_tests.begin(),
                      collation_common_tests.end());

  std::vector<QueryParamsWithResult> string_tests =
      GetFunctionTestsLikeStringCommon();

  std::vector<QueryParamsWithResult> all_tests;
  all_tests.insert(all_tests.end(), common_tests.begin(), common_tests.end());
  all_tests.insert(all_tests.end(), string_tests.begin(), string_tests.end());

  return all_tests;
}

}  // namespace googlesql
