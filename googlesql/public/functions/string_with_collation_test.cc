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

#include "googlesql/public/functions/string_with_collation.h"

#include <sys/types.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace googlesql {
namespace functions {

namespace {

struct LikeMatchTestParams {
  const char* pattern;
  const char* input;
  TypeKind type;
  bool expected_outcome;
};

std::vector<LikeMatchTestParams> LikeMatchTestCases() {
  return {
      {"", "", TYPE_STRING, true},

      // '_' matches single character...
      {"_", "", TYPE_STRING, false},
      {"_", "a", TYPE_STRING, true},
      {"_", "ab", TYPE_STRING, false},
      // ... any Unicode character actually.
      {"_", "ф", TYPE_STRING, true},

      // Escaped '_' matches itself
      {"\\_", "_", TYPE_STRING, true},
      {"\\_", "a", TYPE_STRING, false},

      // '_' matches CR and LF
      {"_", "\n", TYPE_STRING, true},
      {"_", "\r", TYPE_STRING, true},

      // Now the string itself is not valid UTF-8.
      {"_", "\xC2", TYPE_STRING, false},

      // '%' should match any string
      {"%", "", TYPE_STRING, true},
      {"%%", "", TYPE_STRING, true},
      {"%", "abc", TYPE_STRING, true},
      {"%", "фюы", TYPE_STRING, true},

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
      {"ABB%BBD%A", "ABBBDA", TYPE_STRING, false},
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

std::vector<LikeMatchTestParams> LikeWithCollationMatchTestCases() {
  return {
      // Ignorable characters.
      {"\u0001", "", TYPE_STRING, true},
      {"\u0001%%", "", TYPE_STRING, true},
      {"%\u0001%", "", TYPE_STRING, true},
      {"%%\u0001", "", TYPE_STRING, true},
      {"foo\u0001", "foo", TYPE_STRING, true},
      {"\u0001foo", "foo", TYPE_STRING, true},
      {"foo%\u0001%bar", "foobar", TYPE_STRING, true},
      {"foo%\u0001%foo", "foobar", TYPE_STRING, false},
      {"foo%\u0001%foo%bar", "foobar", TYPE_STRING, false},
      {"foo\u0001bar", "foobar", TYPE_STRING, true},
      {"foo%\u0001bar", "foobar", TYPE_STRING, true},
      {"foo%\u0001bar%baz", "foobar", TYPE_STRING, false},
      {"foo%\u0001bar%baz", "foobarbaz", TYPE_STRING, true},
      {"foo%bar\u0001", "foobar", TYPE_STRING, true},
      {"aba%\u0001%aba", "ababa", TYPE_STRING, false},
      {"aba\u0001ba", "ababa", TYPE_STRING, true},
      {"", "\u0001", TYPE_STRING, true},
      {"foobar", "\u0001foobar", TYPE_STRING, true},
      {"foobar", "foo\u0001bar", TYPE_STRING, true},
      {"foobar", "foobar\u0001", TYPE_STRING, true},
      {"foobar%", "foobar\u0001", TYPE_STRING, true},
      {"%foobar", "\u0001foobar", TYPE_STRING, true},
      {"foo%bar", "foo\u0001bar", TYPE_STRING, true},
      {"aba%aba", "ab\u0001aba", TYPE_STRING, false},
      {"\u030A\u0001\u030A\u030A\u0001", "\u030A\u030A\u030A", TYPE_STRING,
       true},

      // Below cases could have different results for different collations.
      // Using und:ci as an example.
      {"FOO", "foo", TYPE_STRING, true},
      {"CamelCase", "camelcase", TYPE_STRING, true},
      {"CamelCase", "CAMELCASE", TYPE_STRING, true},
      {"camelcase", "CamelCase", TYPE_STRING, true},
      {"CAMELCASE", "CamelCase", TYPE_STRING, true},
      {"FOOBAR", "foo\u0001bar", TYPE_STRING, true},
      {"%FOO%BAR%", "foobar\u0001", TYPE_STRING, true},
      {"%%%%%FOOBAR", "foobar", TYPE_STRING, true},
      {"FOO%%%%%BAR", "foobar", TYPE_STRING, true},
      {"FOOBAR%%%%%", "foobar", TYPE_STRING, true},
      {"\\%BarFoo%", "%barfoobar", TYPE_STRING, true},
      {"\\%Foo%", "barfoobar", TYPE_STRING, false},
      {"Bar\\%FooBar", "barfoobar", TYPE_STRING, false},
      {"\\_BarFoo%", "_barfoobar", TYPE_STRING, true},
      {"\\_Foo%", "barfoobar", TYPE_STRING, false},
      {"bar\\_Foobar", "barfoobar", TYPE_STRING, false},
      {"foo\\%", "foo\\%", TYPE_STRING, false},
      {"foo\\%", "foo%", TYPE_STRING, true},
      {"Foo%Foo", "foobarfoobarfoo", TYPE_STRING, true},
      {"Foo%Bar", "foobarfoobarfoo", TYPE_STRING, false},
      {"%Foo%Foo%", "foobarbarbaz", TYPE_STRING, false},
      {"aBa%AbA", "ababa", TYPE_STRING, false},
      {"AbAbAbA", "ababa", TYPE_STRING, false},
      {"aBaBa", "abababa", TYPE_STRING, false},
      // 'ß' and 'ẞ'
      {"\u00DF", "\u1E9E", TYPE_STRING, true},
      // 'Å' and 'A''◌̊'
      {"\u00C5", "\u0041\u030A", TYPE_STRING, true},
      // 'ự' and 'u''◌̣''◌̛'
      {"\u1EF1", "\u0075\u031B\u0323", TYPE_STRING, true},
      {"\u0075%\u0323", "\u1EF1", TYPE_STRING, false},
      {"u%", "\u1EF1", TYPE_STRING, false},
      {"あ", "ア", TYPE_STRING, true},
      // "MW" and '㎿'
      {"MW", "\u33BF", TYPE_STRING, true},
      {"MM%", "M\u33BF", TYPE_STRING, false},
      {"A", "Å", TYPE_STRING, false},
      // 'ß' and "SS"
      {"\u00DF", "SS", TYPE_STRING, false},
      // 's' and 'ſ'
      {"s", "\u017F", TYPE_STRING, false},
      // "1/2" and '½'
      {"1/2", "\u00BD", TYPE_STRING, false},
      // Strings are not normalized.
      {"\u0078\u031B\u0323", "\u0078\u0323\u031B", TYPE_STRING, false},
      // 'Å' and 'a''◌̊'
      {"\u00C5", "\u0061\u030A", TYPE_STRING, true},
      // 'ự' and 'U''◌̣''◌̛'
      {"\u1EF1", "\u0055\u031B\u0323", TYPE_STRING, true},
      // "mw" and '㎿'
      {"mw", "\u33BF", TYPE_STRING, true},
  };
}

std::vector<LikeMatchTestParams> LikeWithUnderscoreTestCases() {
  return {
      {"__", "AA", TYPE_STRING, true},
      {"__AA", "AAAA", TYPE_STRING, true},
      {"_", "A", TYPE_STRING, true},
      {"__", "AAA", TYPE_STRING, false},
      {"_", "", TYPE_STRING, false},
      {"__", "", TYPE_STRING, false},
      {"_A", "", TYPE_STRING, false},
      {"A_", "", TYPE_STRING, false},
      {"_%", "", TYPE_STRING, false},
      {"%_", "", TYPE_STRING, false},
      {"A_BA", "ABBA", TYPE_STRING, true},
      {"a_bA", "ABBA", TYPE_STRING, true},
      {"ABB_", "ABBA", TYPE_STRING, true},
      {"A_BA", "ACCBA", TYPE_STRING, false},
      {"C_A", "CCBA", TYPE_STRING, false},
      {"_C_A", "CCBA", TYPE_STRING, true},
      {"C___", "CCBA", TYPE_STRING, true},
      {"C__A", "CCBA", TYPE_STRING, true},
      {"C___A", "CCBA", TYPE_STRING, false},
      {"C__", "CCBA", TYPE_STRING, false},
      {"C___ABC", "CCBA", TYPE_STRING, false},
      {"__b_", "CCBA", TYPE_STRING, true},
      {"%c__", "CCBA", TYPE_STRING, true},
      {"%c___", "CCBA", TYPE_STRING, true},
      {"%__%", "abcde", TYPE_STRING, true},
      {"_%_", "abc", TYPE_STRING, true},
      {"A_b%c_d%", "abcdef", TYPE_STRING, false},
      {"a%c_", "abcde", TYPE_STRING, false},
      {"a%d_", "abcde", TYPE_STRING, true},
      {"a%_e", "abcde", TYPE_STRING, true},
      {"A%B_DE", "abcde", TYPE_STRING, true},

      // Backtracking cases: Combine % and _
      {"%_%%%", "", TYPE_STRING, false},
      {"%_%_%", "A", TYPE_STRING, false},
      {"_%%A%_", "Bab", TYPE_STRING, true},
      {"%%_%%A%%_", "Bab", TYPE_STRING, true},
      {"_%%A%_%%", "Bab", TYPE_STRING, true},
      {"%%_%%A%%%_%%", "Bab", TYPE_STRING, true},
      {"%_%_%", "AA", TYPE_STRING, true},
      {"_%_%", "AA", TYPE_STRING, true},
      {"%__%", "AA", TYPE_STRING, true},
      {"%_%_", "AA", TYPE_STRING, true},
      {"%A__C%", "AABBCA", TYPE_STRING, true},
      {"%B_C%", "ABBBCA", TYPE_STRING, true},
      {"%B_C%", "AAABBBBBCA", TYPE_STRING, true},
      {"%B_C%", "BBBBBCA", TYPE_STRING, true},
      {"%B_C%", "BBBBBC", TYPE_STRING, true},
      {"%B_C%_C%", "ABBBCAC", TYPE_STRING, true},
      {"%B_C%", "BBC", TYPE_STRING, true},
      {"%B_A_A_A_C%", "ABBBABABABBBBBABABABCA", TYPE_STRING, true},
      {"%B_C_AA", "ABBBCAAAAA", TYPE_STRING, false},
      {"%B_C_AA", "ABBBCAAABBCAAA", TYPE_STRING, true},
      {"%B_C%", "ABBBCA", TYPE_STRING, true},
      {"%B_C%", "ABBBCA", TYPE_STRING, true},

      // multiple backtrack attempts needed
      {"%b_n%", "babana", TYPE_STRING, true},
      {"%b_n_", "babana", TYPE_STRING, true},
      {"b_%n_", "banana", TYPE_STRING, true},
      {"b_%_n_", "banana", TYPE_STRING, true},
      {"b__%_n_", "banana", TYPE_STRING, true},
      {"b__%a__", "banana", TYPE_STRING, true},
      {"b_%_a%_a", "banana", TYPE_STRING, true},
      {"_A%", "banana", TYPE_STRING, true},
      {"__A%", "banana", TYPE_STRING, false},
      {"%1_22_333_C%", "aaa1a22a333aa11aaa22a1a22a333aaC1a22a333aCa",
       TYPE_STRING, true},
      {"%1_22_333_C", "aaa1a22a333aa11aaa22a1a22a333aaC1a22a333aC", TYPE_STRING,
       true},
      {"%1_22_333_C", "aaa1a22a333aa11aaa22a1a22a333aaC1a22a333aCa",
       TYPE_STRING, false},
      {"%1_22_333_C%", "aaa1a22a333aa11aaa22a1a22a333aaC1a22a333aaC",
       TYPE_STRING, false},
      {"%1_22_%333_1", "333a1a22333a1a22aa333aa1333a1", TYPE_STRING, true},
      {"%1_22_%333_1", "333a1a22333a1a22aa333aa1333a1a", TYPE_STRING, false},
      {"%1_22_%333_1_22", "333a1a22333a1a22aa333a1a2a1333a1a22", TYPE_STRING,
       true},
      {"%1_22_%333_1_22%", "333a1a22333a1a22aa333a1a2a1333a1a22", TYPE_STRING,
       true},
      {"%1_22_%333_1_22%", "333a1a22333a1a22aa333a1a2a1333a1a22a", TYPE_STRING,
       true},
      {"%1_22_%333_1%", "333a1a22333a1a22aa333aa1333a1a", TYPE_STRING, true},
      {"%1_22_333_1%", "333a1a22333a1a22aa333aa1333a1a", TYPE_STRING, false},
      {"%1_22_333_1%", "333a1a22333a1a22a333a1333a1a", TYPE_STRING, true},
      {"1_22_333_1%", "333a1a22333a1a22a333aa1333a1a", TYPE_STRING, false},
      {"122234%_2345", "1222345", TYPE_STRING, false},
      {"122234%2345_", "1222345", TYPE_STRING, false},
      {"122234%_2345", "1222342345", TYPE_STRING, false},
      {"122234%_%%2345%", "1222342345", TYPE_STRING, false},

      // Special Characters
      {"_ß_", "ßẞß", TYPE_STRING, true},
      // '\u0041\u030A' == '\u00C5' == 'Å'
      {"_", "\u0041\u030A", TYPE_STRING, true},
      {"__", "\u0041\u030A", TYPE_STRING, false},
      {"_\u030A", "\u0041\u030A", TYPE_STRING, false},
      {"\u0041_", "\u0041\u030A", TYPE_STRING, false},
      {"_%\u030A", "\u0041\u030A", TYPE_STRING, false},
      {"\u0041%_", "\u0041\u030A", TYPE_STRING, false},
      {"_", "℡", TYPE_STRING, true},
      {"___", "℡", TYPE_STRING, false},
      {"_ephone", "℡ephone", TYPE_STRING, true},
      // 𐧏 (U+109CF MEROITIC CURSIVE NUMBER SEVENTY) has 2 UTF-16 code units
      {"_", "𐧏", TYPE_STRING, true},
      {"%_abc", "𐧏abc", TYPE_STRING, true},
      {"%abc", "𐧏abc", TYPE_STRING, true},
      {"%__abc", "𐧏abc", TYPE_STRING, false},
      {"%_𐧏abc", "𐧏𐧏abc", TYPE_STRING, true},
      {"%_𐧏abc", "𐧏abc𐧏abc", TYPE_STRING, true},
      {"%_𐧏abcd", "𐧏abcd𐧏abc", TYPE_STRING, false},
      {"%_bc_", "abc𐧏", TYPE_STRING, true},
      {"%bc_", "abc𐧏", TYPE_STRING, true},
      // 🚲 (U+1F6B2 Bicycle) also has 2 UTF-1 code units
      {"_", "🚲", TYPE_STRING, true},
      {"%_", "a🚲aa", TYPE_STRING, true},
      {"____", "a🚲aa", TYPE_STRING, true},

      // Combining marks
      // Note: Multiple combining marks without a base character forms one
      // grapheme cluster.
      {"_", "\u030A\u030A\u030A", TYPE_STRING, true},
      {"%_%%", "\u030A\u030A\u030A", TYPE_STRING, true},
      {"_%_", "\u030A\u030A\u030A", TYPE_STRING, false},
      {"___", "\u030A\u030A\u030A", TYPE_STRING, false},
      {"%___", "\u030A\u030A\u030A", TYPE_STRING, false},
      {"%\u030A\u030A\u030A", "\u030A\u030A\u030A", TYPE_STRING, true},
      {"%\u030A\u030A\u030A", "\u030A\u030A\u030A", TYPE_STRING, true},
      {"%\u030A_\u030A_\u030A", "\u030A\u030A\u030A", TYPE_STRING, false},
      {"%__\u030A", "\u030A\u030A\u030A", TYPE_STRING, false},
      {"\u030A__", "\u030A\u030A\u030A", TYPE_STRING, false},
      {"\u030A", "\u030A\u030A\u030A", TYPE_STRING, false},
      // Ignorable characters
      {"_\u0001\u0001", "A", TYPE_STRING, true},
      {"_\u0001_", "ab", TYPE_STRING, true},
      {"\u0001_\u0001_\u0001", "ab", TYPE_STRING, true},
      {"\u0001_\u0001", "\u030A\u030A\u030A", TYPE_STRING, true},
      {"_", "\u0001\u0001", TYPE_STRING, false},
  };
}

template <typename OutType, typename FunctionType, typename... Args>
bool EvaluateFunction(FunctionType function, const GoogleSqlCollator& collator,
                      Args&&... args, OutType* out, absl::Status* status) {
  if constexpr (std::is_invocable_v<decltype(function), decltype(collator),
                                    Args..., OutType*, absl::Status*>) {
    return function(collator, args..., out, status);
  } else {
    *status = function(collator, args..., out);
    return status->ok();
  }
}

template <typename OutType, typename FunctionType, class... Args>
void TestStringFunctionWithCollation(FunctionType function,
                                     const QueryParamsWithResult& param,
                                     absl::string_view collation_spec,
                                     Args... args) {
  OutType out;
  absl::Status status;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator(collation_spec));
  bool evaluation_result = EvaluateFunction<OutType, FunctionType, Args...>(
      function, *collator, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_TRUE(param.result().Equals(Value::String(out)))
        << "Expected: " << param.result() << "\n"
        << "Actual: '" << out << "'\n";
  } else if (!param.status().message().empty()) {
    // If an error message is provided, it means we want to test the exact
    // message instead of a binary success/failure.
    EXPECT_EQ(param.status(), status);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

template <typename Arg>
std::string GetArgumentString(Arg&& arg) {
  std::ostringstream os;
  os << arg;
  return os.str();
}

template <typename First, typename... Rest>
std::string GetArgumentString(First&& first, Rest&&... rest) {
  return GetArgumentString(first) + ", " + GetArgumentString(rest...);
}

template <typename FunctionType, class... Args>
void TestStringArrayFunctionWithCollation(FunctionType function,
                                          const QueryParamsWithResult& param,
                                          absl::string_view collation_spec,
                                          Args... args) {
  std::vector<absl::string_view> out;
  absl::Status status;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator(collation_spec));
  bool evaluation_result =
      EvaluateFunction<std::vector<absl::string_view>, FunctionType, Args...>(
          function, *collator, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  if (param.status().ok()) {
    std::vector<Value> out_values;
    for (auto element : out) {
      out_values.push_back(Value::String(element));
    }
    auto out_array = Value::Array(
        googlesql::types::StringArrayType()->AsArray(), out_values);
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_TRUE(param.result().Equals(out_array))
        << "Expected: " << param.result() << "\n"
        << "Actual: '" << out_array << "'\n"
        << "Collation: " << collation_spec << "\n"
        << "Inputs: " << GetArgumentString(args...) << "\n";
  } else if (!param.status().message().empty()) {
    // If an error message is provided, it means we want to test the exact
    // message instead of a binary success/failure.
    EXPECT_EQ(param.status(), status);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out.size();
  }
}

template <typename OutType, typename FunctionType, class... Args>
void TestFunctionWithCollation(FunctionType function,
                               const QueryParamsWithResult& param,
                               absl::string_view collation_spec, Args... args) {
  OutType out;
  absl::Status status;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator(collation_spec));

  bool evaluation_result = EvaluateFunction<OutType, FunctionType, Args...>(
      function, *collator, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_TRUE(param.result().Equals(Value::Make<OutType>(out)))
        << "Expected: " << param.result() << "\n"
        << "Actual: '" << out << "'\n"
        << "Collation: " << collation_spec << "\n"
        << "Inputs: " << GetArgumentString(args...) << "\n";
  } else if (!param.status().message().empty()) {
    // If an error message is provided, it means we want to test the exact
    // message instead of a binary success/failure.
    EXPECT_EQ(param.status(), status);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

typedef testing::TestWithParam<FunctionTestCall> StringWithCollatorTemplateTest;
TEST_P(StringWithCollatorTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null and bytes arguments.
    if (arg.is_null() || arg.type_kind() == TypeKind::TYPE_BYTES) return;
  }
  if (function == "replace_with_collator") {
    TestStringFunctionWithCollation<std::string>(
        &ReplaceUtf8WithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value(), args[3].string_value());
  } else if (function == "split_with_collator") {
    TestStringArrayFunctionWithCollation(
        &SplitUtf8WithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value());
  } else if (function == "instr") {
    // Use test cases for regular INSTR for both unicode:cs and unicode:ci.
    int64_t pos = 1;
    int64_t occurrence = 1;
    if (args.size() >= 3) pos = args[2].int64_value();
    if (args.size() == 4) occurrence = args[3].int64_value();
    TestFunctionWithCollation<int64_t>(
        &StrPosOccurrenceUtf8WithCollation, param.params, "unicode:ci",
        args[0].string_value(), args[1].string_value(), pos, occurrence);
    // Verify that unicode:cs uses the non-collation version.
    TestFunctionWithCollation<int64_t>(
        &StrPosOccurrenceUtf8WithCollation, param.params, "unicode:cs",
        args[0].string_value(), args[1].string_value(), pos, occurrence);
  } else if (function == "instr_with_collator") {
    int64_t pos = 1;
    int64_t occurrence = 1;
    if (args.size() >= 4) pos = args[3].int64_value();
    if (args.size() == 5) occurrence = args[4].int64_value();
    TestFunctionWithCollation<int64_t>(&StrPosOccurrenceUtf8WithCollation,
                                       param.params, args[0].string_value(),
                                       args[1].string_value(),
                                       args[2].string_value(), pos, occurrence);
  } else if (function == "strpos_with_collator") {
    TestFunctionWithCollation<int64_t>(
        &StrposUtf8WithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value());
  } else if (function == "starts_with_collator") {
    TestFunctionWithCollation<bool>(
        &StartsWithUtf8WithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value());
  } else if (function == "ends_with_collator") {
    TestFunctionWithCollation<bool>(
        &EndsWithUtf8WithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value());
  } else {
    EXPECT_FALSE(true) << "undefined method name: " << function;
  }
}

INSTANTIATE_TEST_SUITE_P(
    String, StringWithCollatorTemplateTest,
    testing::ValuesIn(GetFunctionTestsStringWithCollator()));

INSTANTIATE_TEST_SUITE_P(String1, StringWithCollatorTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstr1()));

INSTANTIATE_TEST_SUITE_P(String2, StringWithCollatorTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstr2()));

INSTANTIATE_TEST_SUITE_P(String3, StringWithCollatorTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstr3()));

TEST(ReplaceWithCollator, HandleExplodingStringLength) {
  absl::Status error;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("unicode:ci"));
  std::string generation0 = "22222222";
  // Generation 1: 8 -> 8^2 (64)
  std::string generation1;
  EXPECT_TRUE(ReplaceUtf8WithCollation(*collator, generation0, "2", generation0,
                                       &generation1, &error));
  // Generation 2: 64 -> 64^2 (4k)
  std::string generation2;
  EXPECT_TRUE(ReplaceUtf8WithCollation(*collator, generation1, "2", generation1,
                                       &generation2, &error));
  // Generation 2: 4k -> 4k^2 (16m) TOO BIG
  std::string generation3;
  EXPECT_FALSE(ReplaceUtf8WithCollation(*collator, generation2, "2",
                                        generation2, &generation3, &error));
}

TEST(LikeWithCollationMatchTest, MatchTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("und:ci"));
  for (const LikeMatchTestParams& params : LikeMatchTestCases()) {
    if (params.type == TYPE_STRING &&
        !absl::StrContains(absl::string_view(params.pattern), '_')) {
      SCOPED_TRACE(
          absl::Substitute("Matching pattern \"$0\" with string \"$1\"",
                           params.pattern, params.input));
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool result,
                           LikeUtf8WithCollationAllowUnderscore(
                               params.input, params.pattern, *collator));
      EXPECT_EQ(params.expected_outcome, result)
          << params.input << " LIKE " << params.pattern;
      if (!absl::StrContains(absl::string_view(params.pattern), '%')) {
        absl::Status error;
        EXPECT_EQ(
            params.expected_outcome,
            collator->CompareUtf8(params.input, params.pattern, &error) == 0)
            << params.input << "==" << params.pattern;
      }
    }
  }
  for (const LikeMatchTestParams& params : LikeWithCollationMatchTestCases()) {
    SCOPED_TRACE(absl::Substitute("Matching pattern \"$0\" with string \"$1\"",
                                  params.pattern, params.input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool result,
                         LikeUtf8WithCollationAllowUnderscore(
                             params.input, params.pattern, *collator));
    EXPECT_EQ(params.expected_outcome, result)
        << params.input << " LIKE " << params.pattern;
    if (!absl::StrContains(absl::string_view(params.pattern), '%')) {
      absl::Status error;
      EXPECT_EQ(
          params.expected_outcome,
          collator->CompareUtf8(params.input, params.pattern, &error) == 0)
          << params.input << "==" << params.pattern;
    }
  }
  for (const LikeMatchTestParams& params : LikeWithUnderscoreTestCases()) {
    SCOPED_TRACE(absl::Substitute("Matching pattern \"$0\" with string \"$1\"",
                                  params.pattern, params.input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool result,
                         LikeUtf8WithCollationAllowUnderscore(
                             params.input, params.pattern, *collator));
    EXPECT_EQ(params.expected_outcome, result)
        << params.input << " LIKE " << params.pattern;
  }
}

TEST(LikeWithCollationMatchTest, BinaryMatchTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("binary"));
  std::vector<QueryParamsWithResult> params_list = GetFunctionTestsLike();
  for (const QueryParamsWithResult params : params_list) {
    Value text = params.param(0);
    Value pattern = params.param(1);
    if (text.type_kind() == TYPE_STRING && pattern.type_kind() == TYPE_STRING &&
        !text.is_null() && !pattern.is_null()) {
      SCOPED_TRACE(
          absl::Substitute("Matching pattern \"$0\" with string \"$1\"",
                           pattern.string_value(), text.string_value()));
      if (!params.status().ok()) {
        EXPECT_THAT(LikeUtf8WithCollationAllowUnderscore(
                        text.string_value(), pattern.string_value(), *collator),
                    absl_testing::StatusIs(params.status().code()));
      } else {
        GOOGLESQL_ASSERT_OK_AND_ASSIGN(
            bool result,
            LikeUtf8WithCollationAllowUnderscore(
                text.string_value(), pattern.string_value(), *collator));
        EXPECT_EQ(params.result().bool_value(), result)
            << text.string_value() << " LIKE " << pattern.string_value();
      }
    }
  }
}

TEST(LikeWithCollationMatchTest, BadPatternUTF8) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("und:ci"));
  EXPECT_THAT(LikeUtf8WithCollationAllowUnderscore("", "\xC2", *collator),
              absl_testing::StatusIs(
                  absl::StatusCode::kOutOfRange,
                  testing::HasSubstr("The second operand of LIKE operator is "
                                     "not a valid UTF-8 string")));
}

TEST(LikeWithCollationMatchTest, BadPatternEscape) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("und:ci"));
  EXPECT_THAT(LikeUtf8WithCollationAllowUnderscore("", "\\", *collator),
              absl_testing::StatusIs(
                  absl::StatusCode::kOutOfRange,
                  testing::HasSubstr("LIKE pattern ends with a backslash")));
}

TEST(LikeWithCollationMatchTest, UnderscoreNotAllowedWhenFeatureOff) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("und:ci"));
  EXPECT_THAT(
      LikeUtf8WithCollation(" ", "_", *collator),
      absl_testing::StatusIs(
          absl::StatusCode::kOutOfRange,
          testing::HasSubstr("LIKE pattern has '_' which is not "
                             "allowed when its operands have collation")));
}

TEST(LikeWithCollationMatchTest, UnderscoreOnlyAllowedForUndci) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                       MakeSqlCollator("und:cs"));
  EXPECT_THAT(
      LikeUtf8WithCollationAllowUnderscore(" ", "_", *collator),
      absl_testing::StatusIs(
          absl::StatusCode::kOutOfRange,
          testing::HasSubstr("LIKE pattern has '_' which is not "
                             "allowed when its operands have collation other "
                             "than und:ci")));
}

typedef testing::TestWithParam<FunctionTestCall> SplitSubstrTemplateTest;
TEST_P(SplitSubstrTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  int64_t num_params = param.params.num_params();
  const std::vector<Value>& args = param.params.params();

  for (int i = 0; i < num_params; ++i) {
    if (param.params.param(i).is_null()) {
      return;
    }
  }

  if (num_params == 5) {
    TestFunctionWithCollation<std::string>(
        &SplitSubstrWithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value(), args[3].int64_value(),
        args[4].int64_value());
  } else {
    TestFunctionWithCollation<std::string>(
        &SplitSubstrWithCollation, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value(), args[3].int64_value(),
        std::numeric_limits<int64_t>::max());
  }
}

INSTANTIATE_TEST_SUITE_P(
    String, SplitSubstrTemplateTest,
    testing::ValuesIn(GetFunctionTestsSplitSubstr(/*skip_collation=*/false)));

}  // anonymous namespace
}  // namespace functions
}  // namespace googlesql
