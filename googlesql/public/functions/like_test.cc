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

#include "googlesql/public/functions/like.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/substitute.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

struct LikeMatchTestParams {
  const char *pattern;
  const char *input;
  TypeKind type;
  bool expected_outcome;
};

typedef testing::TestWithParam<LikeMatchTestParams> LikeMatchTest;

std::vector<LikeMatchTestParams> LikeMatchTestCases() {
  return {
      // '_' matches single character...
      {"_", "", TYPE_STRING, false},
      {"_", "a", TYPE_STRING, true},
      {"_", "ab", TYPE_STRING, false},
      // ... any Unicode character actually.
      {"_", "ф", TYPE_STRING, true},
      // Unless we're dealing with TYPE_BYTES where it's two bytes.
      {"_", "ф", TYPE_BYTES, false},
      {"__", "ф", TYPE_BYTES, true},

      // Escaped '_' matches itself
      {"\\_", "_", TYPE_STRING, true},
      {"\\_", "a", TYPE_STRING, false},

      // '_' matches CR and LF
      {"_", "\n", TYPE_STRING, true},
      {"_", "\r", TYPE_STRING, true},

      // The pattern is not a valid UTF-8 string, so the regexp fails to compile
      // with TYPE_STRING, but should still work with TYPE_BYTES.
      {"\xC2", "\xC2", TYPE_BYTES, true},

      // Now the string itself is not valid UTF-8. Should fail
      // in TYPE_STRING mode.
      {"_", "\xC2", TYPE_STRING, false},
      {"_", "\xC2", TYPE_BYTES, true},

      // Special regexp characters should be properly escaped.
      {".", ".", TYPE_BYTES, true},
      {".", "a", TYPE_BYTES, false},
      {"*", "a", TYPE_BYTES, false},
      {"*", "*", TYPE_BYTES, true},
      {"(", "(", TYPE_BYTES, true},
      {"(", ")", TYPE_BYTES, false},

      // Escaped character in the pattern should be unescaped.
      {"\\\\", "\\", TYPE_BYTES, true},
      {"\\%\\_", "%_", TYPE_BYTES, true},
      {"\\%\\_", "ab", TYPE_BYTES, false},

      // '%' should match any string
      {"%", "", TYPE_STRING, true},
      {"%", "abc", TYPE_STRING, true},
      {"%", "фюы", TYPE_STRING, true},
      {"%", "abc", TYPE_BYTES, true},
      {"%", "фюы", TYPE_BYTES, true},
      {"%%", "a", TYPE_BYTES, true},
      {"%%", "abc", TYPE_BYTES, true},
      {"% matches LF, CR, and %", "\r\n matches LF, CR, and \t\r\n\b anything",
       TYPE_STRING, true},

      // A few more more complex expressions
      {"a(%)b", "a()b", TYPE_STRING, true},
      {"a(%)b", "a(z)b", TYPE_STRING, true},
      {"a(_%)b", "a()b", TYPE_STRING, false},
      {"a(_%)b", "a(z)b", TYPE_STRING, true},
      {"a(_\\%)b", "a(z)b", TYPE_STRING, false},
      {"\\a\\(_%\\)\\b", "a(z)b", TYPE_STRING, true},
      {"a%b%c", "abc", TYPE_STRING, true},
      {"a%b%c", "axyzbxyzc", TYPE_STRING, true},
      {"a%xyz%c", "abxybyzbc", TYPE_STRING, false},
      {"a%xyz%c", "abxybyzbxyzbc", TYPE_STRING, true},

      {"foo", "foo", TYPE_STRING, true},
      {"foo", "bar", TYPE_STRING, false},
      {"%bar", "foobar", TYPE_STRING, true},
      {"foo%", "foobar", TYPE_STRING, true},
      {"foo", "foob", TYPE_STRING, false},
      {"%bar%", "foobarfoo", TYPE_STRING, true},
      {"foo%bar", "foobar", TYPE_STRING, true},
      {"foo%foo", "foobarfoobarfoo", TYPE_STRING, true},
      {"foo%bar", "foobarfoobarfoo", TYPE_STRING, false},
      {"%foo%foo%", "foobarbarbaz", TYPE_STRING, false},
      {"aba%aba", "ababa", TYPE_STRING, false},
      {"abababa", "ababa", TYPE_STRING, false},
      {"ababa", "abababa", TYPE_STRING, false},
      {"\\%barfoo%", "%barfoobar", TYPE_STRING, true},
      {"\\%foo%", "barfoobar", TYPE_STRING, false},
      {"bar\\%foobar", "barfoobar", TYPE_STRING, false},
      {"\\_barfoo%", "_barfoobar", TYPE_STRING, true},
      {"\\_foo%", "barfoobar", TYPE_STRING, false},
      {"bar\\_foobar", "barfoobar", TYPE_STRING, false},
      {"%%%%%", "", TYPE_STRING, true},
      {"%%%%%", "barfoobar", TYPE_STRING, true},
      {"", "barfoobar", TYPE_STRING, false},
  };
}

INSTANTIATE_TEST_SUITE_P(LikeMatchTest, LikeMatchTest,
                         testing::ValuesIn(LikeMatchTestCases()));

TEST_P(LikeMatchTest, MatchTest) {
  const LikeMatchTestParams& params = GetParam();

  std::unique_ptr<RE2> re;

  SCOPED_TRACE(absl::Substitute("Matching pattern \"$0\" with string \"$1\"",
                                params.pattern, params.input));
  absl::Status status = CreateLikeRegexp(params.pattern, params.type, &re);
  ASSERT_TRUE(status.ok()) << status;

  ASSERT_EQ(params.expected_outcome, RE2::FullMatch(params.input, *re));
}

TEST(LikeTest, BadPatternUTF8) {
  std::unique_ptr<RE2> re;
  absl::Status status = CreateLikeRegexp("\xC2", TYPE_STRING, &re);
  ASSERT_FALSE(status.ok());
  EXPECT_TRUE(re == nullptr);
  EXPECT_EQ(absl::StatusCode::kOutOfRange, status.code());
}

TEST(LikeTest, BadPatternEscape) {
  std::unique_ptr<RE2> re;
  absl::Status status = CreateLikeRegexp("\\", TYPE_STRING, &re);
  ASSERT_FALSE(status.ok());
  EXPECT_TRUE(re == nullptr);
  EXPECT_EQ(absl::StatusCode::kOutOfRange, status.code());
}

}  // namespace functions
}  // namespace googlesql
