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

#include "googlesql/public/functions/parse_date_time_utils.h"

#include <time.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/types/timestamp_util.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace googlesql {
namespace functions {
namespace parse_date_time_utils {
namespace {

constexpr size_t EXPECT_ERROR = 0;
constexpr int64_t k_int64min = std::numeric_limits<int64_t>::min();
constexpr int64_t k_int64max = std::numeric_limits<int64_t>::max();

static void TestConvertTimeToTimestamp(absl::Time time,
                                       int64_t expected_timestamp,
                                       bool expected_success) {
  int64_t timestamp;
  const bool res = ConvertTimeToTimestamp(time, &timestamp);
  if (expected_success) {
    EXPECT_TRUE(res);
    EXPECT_EQ(timestamp, expected_timestamp);
  } else {
    EXPECT_FALSE(res);
  }
}

// We use <expected_offset> = 0 to indicate that we expect this is an error
// test case.
static void TestParseSubSeconds(
    const char* dp, const char* end_of_data, int max_digits,
    TimestampScale scale, size_t expected_offset,
    absl::Duration expected_subseconds = absl::Duration(),
    uint32_t expected_sub_nanoseconds = 0) {
  absl::Duration subseconds;
  uint32_t sub_nanoseconds;
  const char* res = ParseSubSeconds(dp, end_of_data, max_digits, scale,
                                    &subseconds, &sub_nanoseconds);
  if (expected_offset > 0) {
    EXPECT_EQ(res - dp, expected_offset);
    EXPECT_EQ(subseconds, expected_subseconds);
    EXPECT_EQ(sub_nanoseconds, expected_sub_nanoseconds);
  } else {
    EXPECT_EQ(res, nullptr);
  }
}

static void TestParseSubSeconds(
    absl::string_view input_str, int max_digits, TimestampScale scale,
    size_t expected_offset,
    absl::Duration expected_subseconds = absl::Duration(),
    uint32_t expected_sub_nanoseconds = 0) {
  return TestParseSubSeconds(
      input_str.data(), input_str.data() + input_str.size(), max_digits, scale,
      expected_offset, expected_subseconds, expected_sub_nanoseconds);
}

// Type <T> here indicates the output type when calling ParseInt function in the
// test.
template <typename T>
static void TestParseInt(const char* dp, const char* end_of_data, int max_width,
                         int64_t min, int64_t max, size_t expected_offset,
                         T expected_int = 0) {
  static_assert(std::is_same_v<T, int64_t> || std::is_same_v<T, int32_t>);
  T output_int;
  const char* res = ParseInt(dp, end_of_data, max_width, min, max, &output_int);
  if (expected_offset > 0) {
    EXPECT_EQ(res - dp, expected_offset);
    EXPECT_EQ(output_int, expected_int);
  } else {
    EXPECT_EQ(res, nullptr);
  }
}

// We use <expected_offset> = 0 to indicate that we expect this is an error
// test case.
template <typename T>
static void TestParseInt(absl::string_view input_str, int max_width,
                         int64_t min, int64_t max, size_t expected_offset,
                         T expected_int = 0) {
  return TestParseInt(input_str.data(), input_str.data() + input_str.size(),
                      max_width, min, max, expected_offset, expected_int);
}

// Type <T> here indicates the output type when calling ParseInt function in the
// test cases inside.
template <typename T>
static void ExecuteParseIntTest(T int_to_test) {
  static_assert(std::is_same_v<T, int64_t> || std::is_same_v<T, int32_t>);
  std::string int_string = absl::StrCat(int_to_test);
  int int_length = static_cast<int>(int_string.size());
  // We make sure <int_to_test> is in the range of
  // [<min_to_test>, <max_to_test>].
  int64_t max_to_test = int_to_test;
  int64_t min_to_test = int_to_test;
  if (max_to_test <= k_int64max - 1) {
    max_to_test += 1;
  }

  if (min_to_test >= k_int64min + 1) {
    min_to_test -= 1;
  }
  TestParseInt(int_string, int_length, min_to_test, max_to_test, int_length,
               int_to_test);
  // Test with non-empty postfix and longer <max_width>.
  std::string dummy_postfix = "-+dummy+";
  TestParseInt(absl::StrCat(int_string, dummy_postfix),
               int_length + static_cast<int>(dummy_postfix.size()) / 2,
               min_to_test, max_to_test, int_length, int_to_test);
  // Test with changing <min>/<max> arguments.
  if (min_to_test < int_to_test) {
    TestParseInt<T>(int_string, int_length, min_to_test, min_to_test,
                    EXPECT_ERROR);
  }
  if (max_to_test > int_to_test) {
    TestParseInt<T>(int_string, int_length, max_to_test, max_to_test,
                    EXPECT_ERROR);
  }
  // Test case where <max_width> is shorter than <int_length>.
  if (int_to_test <= -10 || int_to_test >= 10) {
    std::string shorter_int_string = int_string.substr(0, int_length - 1);
    int64_t shorter_int = int_to_test / 10;
    TestParseInt(shorter_int_string, int_length - 1, shorter_int, shorter_int,
                 int_length - 1, shorter_int);
  }
}

TEST(ParseDateTimeInternalTests, ConvertTimeToTimestamp) {
  const int64_t timestamp_valid_min_micros = types::kTimestampMin;
  const int64_t timestamp_valid_max_micros = types::kTimestampMax;
  std::vector<std::pair<int64_t, bool>> timestamp_with_results = {
      {timestamp_valid_min_micros - 1, false},
      {timestamp_valid_min_micros, true},
      {timestamp_valid_min_micros + 1, true},
      {timestamp_valid_max_micros - 1, true},
      {timestamp_valid_max_micros, true},
      {timestamp_valid_max_micros, true},
      {timestamp_valid_max_micros + 1, false}};

  for (const auto& test_pair : timestamp_with_results) {
    TestConvertTimeToTimestamp(absl::FromUnixMicros(test_pair.first),
                               test_pair.first, test_pair.second);
  }
}

TEST(ParseDateTimeInternalTests, ParseInt) {
  const int k_intmin = std::numeric_limits<int>::min();
  const int k_intmax = std::numeric_limits<int>::max();

  ExecuteParseIntTest<int>(k_intmin);
  ExecuteParseIntTest<int64_t>(k_intmin);
  ExecuteParseIntTest<int>(k_intmin + 1);
  ExecuteParseIntTest<int64_t>(k_intmin + 1);
  ExecuteParseIntTest<int>(-100000);
  ExecuteParseIntTest<int64_t>(-100000);
  ExecuteParseIntTest<int>(-999);
  ExecuteParseIntTest<int64_t>(-999);
  ExecuteParseIntTest<int>(1);
  ExecuteParseIntTest<int64_t>(1);
  ExecuteParseIntTest<int>(234);
  ExecuteParseIntTest<int64_t>(234);
  ExecuteParseIntTest<int>(6789);
  ExecuteParseIntTest<int64_t>(6789);
  ExecuteParseIntTest<int>(k_intmax - 1);
  ExecuteParseIntTest<int64_t>(k_intmax - 1);
  ExecuteParseIntTest<int>(k_intmax);
  ExecuteParseIntTest<int64_t>(k_intmax);

  ExecuteParseIntTest<int64_t>(k_int64min);
  ExecuteParseIntTest<int64_t>(static_cast<int64_t>(k_intmin) - 1);
  ExecuteParseIntTest<int64_t>(static_cast<int64_t>(k_intmax) + 1);
  ExecuteParseIntTest<int64_t>(k_int64max);

  // Error cases.
  int dummy_int = 123;
  std::string dummy_int_str = absl::StrCat(dummy_int);
  const char* dummy_int_str_end = dummy_int_str.data() + dummy_int_str.size();
  int dummy_int_length = static_cast<int>(dummy_int_str.size());
  std::string plus_sign_dummy_int_str = absl::StrCat("+", dummy_int_str);
  // Use successful test case below as comparison to other cases causing
  // errors.
  TestParseInt<int>(dummy_int_str.data(), dummy_int_str_end, dummy_int_length,
                    k_intmin, k_intmax, dummy_int_str.size(), dummy_int);

  TestParseInt<int>(nullptr, dummy_int_str_end, dummy_int_length, k_intmin,
                    k_intmax, EXPECT_ERROR);
  TestParseInt<int>(dummy_int_str.data(), dummy_int_str.data(),
                    dummy_int_length, k_intmin, k_intmax, EXPECT_ERROR);
  TestParseInt<int>(dummy_int_str.data(), dummy_int_str.data() - 1,
                    dummy_int_length, k_intmin, k_intmax, EXPECT_ERROR);
  TestParseInt<int>(dummy_int_str.data(), dummy_int_str_end, /*max_width*/ 0,
                    k_intmin, k_intmax, EXPECT_ERROR);
  TestParseInt<int>(plus_sign_dummy_int_str,
                    static_cast<int>(plus_sign_dummy_int_str.size()), k_intmin,
                    k_intmax, EXPECT_ERROR);
  // Test case where input string is "-0".
  TestParseInt<int>("-0", static_cast<int>(std::string("-0").size()), k_intmin,
                    k_intmax, EXPECT_ERROR);

  // Overflow test cases.
  const std::string k_int64min_str = absl::StrCat(k_int64min);
  const std::string k_int64max_str = absl::StrCat(k_int64max);
  // We append this digit to the end of <k_int64min_str>/<k_int64max_str> to
  // make the output string represent an out of range number of type int64.
  std::string digit_to_add = "0";
  TestParseInt<int64_t>(
      absl::StrCat(k_int64min_str, digit_to_add),
      static_cast<int>(k_int64min_str.size() + digit_to_add.size()), k_int64min,
      k_int64max, EXPECT_ERROR);
  TestParseInt<int64_t>(
      absl::StrCat(k_int64max_str, digit_to_add),
      static_cast<int>(k_int64max_str.size() + digit_to_add.size()), k_int64min,
      k_int64max, EXPECT_ERROR);

  // <k_int64min> and <k_int64max> are overflow for int32 type.
  TestParseInt<int>(k_int64min_str, static_cast<int>(k_int64min_str.size()),
                    k_int64min, k_int64max, EXPECT_ERROR);
  TestParseInt<int>(k_int64max_str, static_cast<int>(k_int64max_str.size()),
                    k_int64min, k_int64max, EXPECT_ERROR);
}

TEST(ParseDateTimeInternalTests, ParseSubSeconds) {
  std::string input_str = "1234567891postfix";
  int digits_length = static_cast<int>(input_str.find("postfix"));
  TestParseSubSeconds(input_str, kMilliseconds - 1, kMilliseconds,
                      kMilliseconds - 1, absl::Milliseconds(120));
  TestParseSubSeconds(input_str, kMilliseconds, kMilliseconds, kMilliseconds,
                      absl::Milliseconds(123));
  TestParseSubSeconds(input_str, kMilliseconds + 1, kMilliseconds,
                      kMilliseconds + 1, absl::Milliseconds(123));

  TestParseSubSeconds(input_str, kMicroseconds - 1, kMicroseconds,
                      kMicroseconds - 1, absl::Microseconds(123450));
  TestParseSubSeconds(input_str, kMicroseconds, kMicroseconds, kMicroseconds,
                      absl::Microseconds(123456));
  TestParseSubSeconds(input_str, kMicroseconds + 1, kMicroseconds,
                      kMicroseconds + 1, absl::Microseconds(123456));

  TestParseSubSeconds(input_str, kNanoseconds - 1, kNanoseconds,
                      kNanoseconds - 1, absl::Nanoseconds(123456780));
  TestParseSubSeconds(input_str, kNanoseconds, kNanoseconds, kNanoseconds,
                      absl::Nanoseconds(123456789));
  TestParseSubSeconds(input_str, kNanoseconds + 1, kNanoseconds,
                      kNanoseconds + 1, absl::Nanoseconds(123456789));
  // <input_str> only has 10 (kNanoseconds + 1) consecutive digits, therefore
  // only kNanoseconds + 1 chars will be parsed even though <max_digits> is
  // set to be kNanoseconds + 2.
  TestParseSubSeconds(input_str, kNanoseconds + 2, kNanoseconds,
                      kNanoseconds + 1, absl::Nanoseconds(123456789));

  // <max_digits> = 0 means "unbounded" for ParseSubSeconds function.
  TestParseSubSeconds(input_str, /*max_digits*/ 0, kNanoseconds, digits_length,
                      absl::Nanoseconds(123456789));

  // Test sub_nanoseconds.
  TestParseSubSeconds(input_str, /*max_digits*/ 0, kPicoseconds, digits_length,
                      absl::Nanoseconds(123456789), 100);
  TestParseSubSeconds("1234567891234postfix", /*max_digits*/ 0, kPicoseconds,
                      13, absl::Nanoseconds(123456789), 123);

  // Error cases.
  TestParseSubSeconds(input_str, kSeconds - 1, kSeconds, EXPECT_ERROR);
  TestParseSubSeconds(nullptr, input_str.data() + input_str.size(),
                      kSeconds - 1, kSeconds, EXPECT_ERROR);
  TestParseSubSeconds(input_str.data(), input_str.data(), kSeconds - 1,
                      kSeconds, EXPECT_ERROR);
  TestParseSubSeconds(input_str.data(), input_str.data() - 1, kSeconds - 1,
                      kSeconds, EXPECT_ERROR);
  TestParseSubSeconds(absl::StrCat("-", input_str), kNanoseconds, kNanoseconds,
                      EXPECT_ERROR);
}

}  // namespace
}  // namespace parse_date_time_utils
}  // namespace functions
}  // namespace googlesql
