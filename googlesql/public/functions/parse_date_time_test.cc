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

#include "googlesql/public/functions/parse_date_time.h"

#include <time.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/timestamp_util.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace {

using testing::HasSubstr;
using testing::Not;
using absl_testing::IsOk;
using absl_testing::StatusIs;

const FormatDateTimestampOptions kExpandQandJ =
    {.expand_Q = true, .expand_J = true};

// Test parsing a timestamp from a string.  The <expected_ result> must be in
// a form that ConvertStringToTimestamp() will work on it (for example,
// the canonical form '2015-01-31 12:34:56.999999+0').  If <expected_string>
// is empty then an error is expected and we validate that parsing the string
// fails.  This is a helper for TestParseSecondsSinceEpoch().
static void TestParseStringToTimestamp(absl::string_view format,
                                       absl::string_view timestamp_string,
                                       absl::string_view default_time_zone,
                                       absl::string_view expected_result) {
  int64_t timestamp = std::numeric_limits<int64_t>::max();
  if (expected_result.empty()) {
    // An error is expected.
    absl::Status status = ParseStringToTimestamp(
        format, timestamp_string, default_time_zone, /*parse_version2=*/true,
        &timestamp);
    std::string result_timestamp_string;
    if (status.ok()) {
      EXPECT_TRUE(IsValidTimestamp(timestamp, kMicroseconds));
      GOOGLESQL_EXPECT_OK(ConvertTimestampToStringWithoutTruncation(
          timestamp, kMicroseconds, "UTC", &result_timestamp_string));
    }
    EXPECT_FALSE(status.ok())
        << "format: " << format << "\ntimestamp_string: '" << timestamp_string
        << "'"
        << "\nresult timestamp: " << timestamp
        << "\nresult timestamp string: " << result_timestamp_string;
    return;
  }
  int64_t expected;
  GOOGLESQL_EXPECT_OK(ConvertStringToTimestamp(expected_result,
                                     default_time_zone, kMicroseconds,
                                     &expected));
  GOOGLESQL_EXPECT_OK(ParseStringToTimestamp(format, timestamp_string,
                                   default_time_zone, /*parse_version2=*/true,
                                   &timestamp))
      << "format: " << format
      << "\ntimestamp: " << timestamp_string;
  std::string converted_timestamp_string;
  GOOGLESQL_EXPECT_OK(ConvertTimestampToStringWithoutTruncation(
      timestamp, kMicroseconds, default_time_zone,
      &converted_timestamp_string));
  EXPECT_EQ(expected, timestamp)
      << "format: " << format
      << "\ninput_timestamp: " << timestamp_string
      << "\nconverted_timestamp:  " << converted_timestamp_string
      << "\nexpected:  " << expected_result;
}

static void TestParseSecondsSinceEpoch(int64_t timestamp_seconds) {
  std::string seconds_string;
  absl::StrAppend(&seconds_string, timestamp_seconds);

  std::string expected_timestamp_string;
  GOOGLESQL_EXPECT_OK(ConvertTimestampToStringWithoutTruncation(
      timestamp_seconds, kSeconds, "UTC", &expected_timestamp_string));

  TestParseStringToTimestamp(
      "%s", seconds_string, "UTC", expected_timestamp_string);
}

static void TestParseTimestamp(const FunctionTestCall& test) {
  // Ignore tests that do not have the time zone explicitly specified since
  // the function library requires a timezone.  Also ignore test cases
  // with NULL value inputs.  The date/time function library is only
  // implemented for non-NULL values.
  if (test.params.params().size() != 3 || test.params.param(0).is_null() ||
      test.params.param(1).is_null() || test.params.param(2).is_null()) {
    return;
  }

  const Value& format_param = test.params.param(0);
  const Value& timestamp_string_param = test.params.param(1);
  const Value& timezone_param = test.params.param(2);
  int64_t result_timestamp;
  absl::Time base_time_result;

  absl::Status status = ParseStringToTimestamp(
      format_param.string_value(), timestamp_string_param.string_value(),
      timezone_param.string_value(), /*parse_version2=*/true,
      &result_timestamp);

  absl::Status base_time_status = ParseStringToTimestamp(
      format_param.string_value(), timestamp_string_param.string_value(),
      timezone_param.string_value(), /*parse_version2=*/true,
      &base_time_result);

  std::string test_string = absl::Substitute(
      absl::StrCat(test.function_name, "($0, $1, $2)"),
      format_param.DebugString(), timestamp_string_param.DebugString(),
      timezone_param.DebugString());

  // Test micros, unless the test case requires nanos precision.
  if (!googlesql_base::ContainsKey(test.params.required_features(),
                        FEATURE_TIMESTAMP_NANOS)) {
    if (test.params.status().ok()) {
      GOOGLESQL_EXPECT_OK(status) << test_string;
      if (status.ok()) {
        EXPECT_EQ(TYPE_TIMESTAMP, test.params.result().type_kind())
            << test_string;
        EXPECT_EQ(test.params.result().ToTime(),
                  absl::FromUnixMicros(result_timestamp))
            << test_string << "\nexpected: "
            << absl::FormatTime(
                   absl::FromUnixMicros(test.params.result().ToUnixMicros()))
            << "\nactual: "
            << absl::FormatTime(absl::FromUnixMicros(result_timestamp));
      }
    } else {
      EXPECT_FALSE(status.ok())
          << test_string << "\nexpected status: " << test.params.status();
    }
  }
  // Test nanos, unless the test case prohibits nanos precision.
  if (!googlesql_base::ContainsKey(test.params.prohibited_features(),
                        FEATURE_TIMESTAMP_NANOS)) {
    if (test.params.status().ok()) {
      GOOGLESQL_EXPECT_OK(base_time_status) << test_string;
      EXPECT_EQ(TYPE_TIMESTAMP, test.params.result().type_kind())
          << test_string;
      EXPECT_EQ(test.params.result().ToTime(), base_time_result)
          << test_string << ": "
          << absl::FormatTime(test.params.result().ToTime()) << " vs "
          << absl::FormatTime(base_time_result);
    } else {
      EXPECT_FALSE(base_time_status.ok())
          << test_string << "\nstatus: " << test.params.status();
    }
  }
}

static void TestParseDate(const FunctionTestCall& test) {
  // Ignore test cases with NULL value inputs. The date/time function library
  // is only implemented for non-NULL values.
  if (test.params.params().size() != 2 ||
      test.params.param(0).is_null() || test.params.param(1).is_null()) {
    return;
  }

  const Value& format_param = test.params.param(0);
  const Value& date_string_param = test.params.param(1);
  int32_t result_date;

  absl::Status status = ParseStringToDate(
      format_param.string_value(), date_string_param.string_value(),
      /*parse_version2=*/true, &result_date);
  std::string test_string = absl::Substitute(
      absl::StrCat(test.function_name, "($0, $1)"), format_param.DebugString(),
      date_string_param.DebugString());

  if (test.params.status().ok()) {
    GOOGLESQL_ASSERT_OK(status) << test_string;
    EXPECT_EQ(TYPE_DATE, test.params.result().type_kind()) << test_string;
    EXPECT_EQ(test.params.result().date_value(), result_date)
        << test_string;
  } else {
    EXPECT_FALSE(status.ok()) << test_string;
  }
}

// Use the supplied <result_validator> to validate that the <actual_status> and
// <actual_output_string_value> matches the expected <result>.
static void ValidateResult(
    const QueryParamsWithResult::Result* result,
    const absl::Status& actual_status,
    const std::string& actual_output_string_value,
    const std::function<bool(const Value& expected_result,
                             const std::string& actual_string_value)>&
        result_validator) {
  ABSL_CHECK(result != nullptr);
  const absl::Status& expected_status = result->status;
  const Value& expected_result = result->result;

  if (expected_status.ok()) {
    GOOGLESQL_ASSERT_OK(actual_status) << expected_result.DebugString();
    EXPECT_TRUE(result_validator(expected_result, actual_output_string_value))
        << "Expected result string: " << expected_result.DebugString()
        << "\nActual result string: " << actual_output_string_value;
  } else {
    EXPECT_FALSE(actual_status.ok()) << actual_output_string_value;
  }
}

// TODO: This a modified copy of a very similar functions defined in
// date_time_util_test.cc, in which there are multiple similar functions to test
// other civil time related functions. We should extract this function into a
// shared library and avoid repeatedly define them.
static void TestCivilTimeFunction(
    const FunctionTestCall& testcase,
    const std::function<bool(const FunctionTestCall& testcase)>&
        should_skip_test_case,
    const std::function<absl::Status(const FunctionTestCall& testcase,
                                     std::string* output_string_value)>&
        function_to_test_for_micro,
    const std::function<absl::Status(const FunctionTestCall& testcase,
                                     std::string* output_string_value)>&
        function_to_test_for_nano,
    const std::function<bool(const Value& expected_result,
                             const std::string& actual_string_value)>&
        result_validator) {
  if (should_skip_test_case(testcase)) {
    return;
  }
  for (auto& [features, result] : testcase.params.results()) {
    // Validate micro result
    if (!googlesql_base::ContainsKey(features, FEATURE_TIMESTAMP_NANOS)) {
      std::string actual_micro_string_value;
      absl::Status actual_micro_status =
          function_to_test_for_micro(testcase, &actual_micro_string_value);
      ValidateResult(&result, actual_micro_status, actual_micro_string_value,
                     result_validator);
    }
    // Validate nano result
    if (!googlesql_base::ContainsKey(testcase.params.prohibited_features(),
                          FEATURE_TIMESTAMP_NANOS)) {
      std::string actual_nano_string_value;
      absl::Status actual_nano_status =
          function_to_test_for_nano(testcase, &actual_nano_string_value);
      ValidateResult(&result, actual_nano_status, actual_nano_string_value,
                     result_validator);
    }
  }
}

static void TestParseTime(const FunctionTestCall& testcase) {
  auto ShouldSkipTestCase = [](const FunctionTestCall& testcase) {
    ABSL_DCHECK_EQ(testcase.params.num_params(), 2);
    // Ignore test cases with NULL value inputs.  The date/time function
    // library is only implemented for non-NULL values.
    if (testcase.params.param(0).is_null() ||
        testcase.params.param(1).is_null()) {
      return true;
    }
    return false;
  };
  auto GetParseTimeFunc = [](TimestampScale scale) {
    return [scale](const FunctionTestCall& testcase,
                   std::string* output_string) -> absl::Status {
      TimeValue time;
      GOOGLESQL_RETURN_IF_ERROR(ParseStringToTime(testcase.params.param(0).string_value(),
                                        testcase.params.param(1).string_value(),
                                        scale, &time));
      *output_string = time.DebugString();
      return absl::OkStatus();
    };
  };
  auto ParseTimeResultValidator = [](const Value& expected_result,
                                     absl::string_view actual_string) {
    return expected_result.type_kind() == TYPE_TIME &&
           expected_result.DebugString() == actual_string;
  };
  TestCivilTimeFunction(
      testcase, ShouldSkipTestCase, GetParseTimeFunc(kMicroseconds),
      GetParseTimeFunc(kNanoseconds), ParseTimeResultValidator);
}

static void TestParseDatetime(const FunctionTestCall& testcase) {
  auto ShouldSkipTestCase = [](const FunctionTestCall& testcase) {
    ABSL_DCHECK_EQ(testcase.params.num_params(), 2);
    // Ignore test cases with NULL value inputs.  The date/time function
    // library is only implemented for non-NULL values.
    if (testcase.params.param(0).is_null() ||
        testcase.params.param(1).is_null()) {
      return true;
    }
    return false;
  };
  auto GetParseDatetimeFunc = [](TimestampScale scale) {
    return [scale](const FunctionTestCall& testcase,
                   std::string* output_string) -> absl::Status {
      DatetimeValue datetime;
      GOOGLESQL_RETURN_IF_ERROR(ParseStringToDatetime(
          testcase.params.param(0).string_value(),
          testcase.params.param(1).string_value(), scale,
          /*parse_version2=*/true, &datetime));
      *output_string = datetime.DebugString();
      return absl::OkStatus();
    };
  };
  auto ParseDatetimeResultValidator = [](const Value& expected_result,
                                         absl::string_view actual_string) {
    return expected_result.type_kind() == TYPE_DATETIME &&
           expected_result.DebugString() == actual_string;
  };
  TestCivilTimeFunction(
      testcase, ShouldSkipTestCase, GetParseDatetimeFunc(kMicroseconds),
      GetParseDatetimeFunc(kNanoseconds), ParseDatetimeResultValidator);
}

static void ParseDateTimeFunctionTest(const FunctionTestCall& test) {
  if (test.function_name == "parse_timestamp") {
    TestParseTimestamp(test);
  } else if (test.function_name == "parse_date") {
    TestParseDate(test);
  } else if (test.function_name == "parse_datetime") {
    TestParseDatetime(test);
  } else if (test.function_name == "parse_time") {
    TestParseTime(test);
  } else {
    ASSERT_FALSE(true) << "Test cases do not support function: "
                       << test.function_name;
  }
}

class ParseDateTimeTestWithParam
  : public ::testing::TestWithParam<FunctionTestCall> {};

TEST_P(ParseDateTimeTestWithParam, ParseStringToDateTimestampTests) {
  const FunctionTestCall& test = GetParam();
  ParseDateTimeFunctionTest(test);
}

// These tests are populated in googlesql/compliance/functions_testlib.cc.
INSTANTIATE_TEST_SUITE_P(
    ParseDateTimestampTests, ParseDateTimeTestWithParam,
    testing::ValuesIn(GetFunctionTestsParseDateTimestamp()));

TEST(StringToTimestampTests, ParseTimestampSecondsSinceEpochTests) {
  // These tests validate results by comparing the parsed string result vs.
  // converting that int64 seconds to timestamp using
  // ConvertTimestampToStringWithoutTruncation().
  TestParseSecondsSinceEpoch(types::kTimestampMin / 1000000);
  TestParseSecondsSinceEpoch(-31536000000);
  TestParseSecondsSinceEpoch(-3153600000);
  TestParseSecondsSinceEpoch(-315360000);
  TestParseSecondsSinceEpoch(-3600);
  TestParseSecondsSinceEpoch(-60);
  TestParseSecondsSinceEpoch(0);
  TestParseSecondsSinceEpoch(60);
  TestParseSecondsSinceEpoch(3600);
  TestParseSecondsSinceEpoch(315360000);
  TestParseSecondsSinceEpoch(3153600000);
  TestParseSecondsSinceEpoch(31536000000);
  TestParseSecondsSinceEpoch(252288000000);
  TestParseSecondsSinceEpoch(types::kTimestampMax / 1000000);
}

TEST(StringToTimestampTests, StrptimeTests) {
  // A few tests of strptime() showing behavior with duplicate format
  // elements.  When there is element overlap, the last one wins.
  char* strptime_result;
  std::string format_string("%Y %Y");
  std::string timestamp_string("2006 2005");
  struct tm tm = { 0 };
  strptime_result = strptime(timestamp_string.c_str(), format_string.c_str(),
                             &tm);
  EXPECT_EQ(105, tm.tm_year);

  format_string = "%D";  // shorthand for %m/%d/%y (with two digit year)
  timestamp_string = "03/04/05";
  tm = { 0 };
  strptime_result = strptime(timestamp_string.c_str(), format_string.c_str(),
                             &tm);

  EXPECT_EQ(105, tm.tm_year) << "year: " << tm.tm_year
                             << ", month: " << tm.tm_mon
                             << ", day: " << tm.tm_mday;
  EXPECT_EQ(2, tm.tm_mon);
  EXPECT_EQ(4, tm.tm_mday);

  format_string = "%Y-%m-%d %D %m";
  timestamp_string = "2000-02-03 04/05/06 08";
  tm = { 0 };
  strptime_result = strptime(timestamp_string.c_str(), format_string.c_str(),
                             &tm);

  EXPECT_EQ(106, tm.tm_year) << "year: " << tm.tm_year
                             << ", month: " << tm.tm_mon
                             << ", day: " << tm.tm_mday;
  EXPECT_EQ(7, tm.tm_mon);
  EXPECT_EQ(5, tm.tm_mday);

#if !defined(__APPLE__)  // Works fine on APPLE version
  // The '%Ey' element is not working in strptime so we have to roll our own.
  format_string = "%Ey";  // two digit year
  timestamp_string = "07";
  tm = { 0 };
  strptime_result = strptime(timestamp_string.c_str(), format_string.c_str(),
                             &tm);
  EXPECT_THAT(strptime_result, testing::IsNull());

  // The '%Ou' element is not working in strptime so we have to roll our own.
  format_string = "%Ou";  // day of week
  timestamp_string = "7";
  tm = { 0 };
  strptime_result = strptime(timestamp_string.c_str(), format_string.c_str(),
                             &tm);
  EXPECT_THAT(strptime_result, testing::IsNull());
#endif  // !defined(__APPLE__)
}

TEST(StringToTimestampTests, NonNullTerminatedStringViewTests) {
  // Tests for string_views that are not null-terminated.

  // Base case, the format and timestamp strings are both null-terminated.
  std::string good_format_string("%M");
  std::string good_timestamp_string("26");

  TimeValue time;
  GOOGLESQL_EXPECT_OK(ParseStringToTime(good_format_string, good_timestamp_string,
                              kMicroseconds, &time));
  EXPECT_EQ(time.Packed64TimeMicros(),
            TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros());

  absl::string_view bad_format_string("%Mabc", 2);
  absl::string_view bad_timestamp_string("26xyz", 2);

  // Only the format string is not null-terminated.
  GOOGLESQL_EXPECT_OK(ParseStringToTime(bad_format_string, good_timestamp_string,
                              kMicroseconds, &time));
  EXPECT_EQ(time.Packed64TimeMicros(),
            TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros());

  // Only the timestamp string is not null-terminated.
  GOOGLESQL_EXPECT_OK(ParseStringToTime(good_format_string, bad_timestamp_string,
                              kMicroseconds, &time));
  EXPECT_EQ(time.Packed64TimeMicros(),
            TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros());

  // The format and timestamp strings are both not null-terminated..
  GOOGLESQL_EXPECT_OK(ParseStringToTime(bad_format_string, bad_timestamp_string,
                              kMicroseconds, &time));
  EXPECT_EQ(time.Packed64TimeMicros(),
            TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros());

  // Related to b/124474887
  absl::string_view bad_format_string_2("%m%d%Y", 6);
  absl::string_view bad_timestamp_string_2("122320189", 8);

  DatetimeValue datetime;
  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_2, bad_timestamp_string_2,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 12, 23, 0, 0, 0, 0).Packed64DatetimeMicros());

  // In this test case, "123" is out of 'bad_timestamp_string_3' with length 23.
  // The actual 'bad_timestamp_string_3' is "2018-09-26 23:42:13.350"
  // But because 'fmt' is ended with "%E*S", if we don't stop at
  // bad_timestamp_string_3[23], there will be an out-of-range error.
  absl::string_view bad_format_string_3("%Y-%m-%d %H:%M:%E*S", 19);
  absl::string_view bad_timestamp_string_3("2018-09-26 23:42:13.350123", 23);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_3, bad_timestamp_string_3,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 9, 26, 23, 42, 13, 350000).Packed64DatetimeMicros());

  absl::string_view bad_format_string_4("%Y-%m-%d %H:%M:%E3S", 19);
  absl::string_view bad_timestamp_string_4("2018-09-26 23:42:13.350123", 21);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_4, bad_timestamp_string_4,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 9, 26, 23, 42, 13, 300000).Packed64DatetimeMicros());

  absl::string_view bad_format_string_5("%Y-%m-%d %H:%M:%E3S", 19);
  absl::string_view bad_timestamp_string_5("2018-09-26 23:42:13.3", 21);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_5, bad_timestamp_string_5,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 9, 26, 23, 42, 13, 300000).Packed64DatetimeMicros());

  absl::string_view bad_format_string_6("%Y-%m-%d %H:%M:%E3S", 19);
  absl::string_view bad_timestamp_string_6("2018-09-26 23:42:13.", 19);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_6, bad_timestamp_string_6,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 9, 26, 23, 42, 13, 000000).Packed64DatetimeMicros());

  absl::string_view bad_format_string_7("%Y-%m-%d %H:%M:%E3S", 19);
  absl::string_view bad_timestamp_string_7("2018-09-26 23:42:13", 19);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_7, bad_timestamp_string_7,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 9, 26, 23, 42, 13, 000000).Packed64DatetimeMicros());

  absl::string_view bad_format_string_8("%Y-%m-%d %H:%M:%S%", 17);
  absl::string_view bad_timestamp_string_8("   2018-09-26 23:42:13", 22);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_8, bad_timestamp_string_8,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 9, 26, 23, 42, 13, 000000).Packed64DatetimeMicros());

  absl::string_view bad_format_string_9("%EY%", 3);
  absl::string_view bad_timestamp_string_9("2018-", 4);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_9, bad_timestamp_string_9,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(
                2018, 1, 1, 0, 0, 0, 0).Packed64DatetimeMicros());

  std::string timestamp_string_10("Monday Feb 28 12:34:56 2006");
  char char_array_10[27];
  for (int i = 0; i < 27; i++) {
    char_array_10[i] = timestamp_string_10[i];
  }
  absl::string_view bad_format_string_10("%a %b %e %H:%M:%S %Y", 20);
  absl::string_view bad_timestamp_string_10(char_array_10, 27);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_10, bad_timestamp_string_10,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(2006, 2, 28, 12, 34, 56, 0)
                .Packed64DatetimeMicros());

  std::string format_string_11("%a %b %e %H:%M:%S %Y");
  char char_array_11[20];
  for (int i = 0; i < 20; i++) {
    char_array_11[i] = format_string_11[i];
  }
  absl::string_view bad_format_string_11(char_array_11, 20);
  absl::string_view bad_timestamp_string_11("Thu July 23 12:00:00 20155", 25);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_11, bad_timestamp_string_11,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(2015, 7, 23, 12, 00, 00, 0)
                .Packed64DatetimeMicros());

  std::string timestamp_string_12("1991-01-01 02:00:00.000000+01");
  char char_array_12[29];
  for (int i = 0; i < 29; i++) {
    char_array_12[i] = timestamp_string_12[i];
  }
  absl::string_view bad_format_string_12("%Y-%m-%d %H:%M:%E6S%Ez", 22);
  absl::string_view bad_timestamp_string_12(char_array_12, 29);

  absl::Time result_timestamp;
  GOOGLESQL_EXPECT_OK(ParseStringToTimestamp(
      bad_format_string_12, bad_timestamp_string_12, "UTC",
      /*parse_version2=*/true, &result_timestamp));
  GOOGLESQL_EXPECT_OK(ConvertTimestampToDatetime(result_timestamp, "UTC", &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(1991, 01, 01, 01, 00, 00, 0)
                .Packed64DatetimeMicros());

  std::string timestamp_string_13(
      " \x0c\n\r\t\x0b\x32\x30\x30\x30\x0b\t\r\n\x0c ");
  char char_array_13[16];
  for (int i = 0; i < 16; i++) {
    char_array_13[i] = timestamp_string_13[i];
  }
  // Bug in __APPLE__
  absl::string_view bad_format_string_13("%t%Y%t", 6);
  absl::string_view bad_timestamp_string_13(char_array_13, 16);

  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(bad_format_string_13, bad_timestamp_string_13,
                                  kMicroseconds, /*parse_version2=*/true,
                                  &datetime));
  EXPECT_EQ(datetime.Packed64DatetimeMicros(),
            DatetimeValue::FromYMDHMSAndMicros(2000, 1, 1, 00, 00, 00, 0)
                .Packed64DatetimeMicros());

  char char_array_14[1];
  char_array_14[0] = '-';
  absl::string_view bad_format_string_14("%Y", 2);
  absl::string_view bad_timestamp_string_14(char_array_14, 1);

  EXPECT_THAT(
      ParseStringToDatetime(bad_format_string_14, bad_timestamp_string_14,
                            kMicroseconds, /*parse_version2=*/true, &datetime),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Failed to parse input string")));
}

TEST(StringToTimestampTests, NullTerminatedStringViewTests) {
  // The last byte of the string_view can be a null byte.
  absl::string_view good_format_string("%M", 2);
  absl::string_view good_format_string_2("%M\0", 3);
  absl::string_view good_timestamp_string("26xyz", 2);
  absl::string_view good_timestamp_string_2("26\0xyz", 3);
  std::vector<absl::string_view> good_format_strings =
      {good_format_string, good_format_string_2};
  std::vector<absl::string_view> good_timestamp_strings =
      {good_timestamp_string, good_timestamp_string_2};

  TimeValue time;
  for (const absl::string_view& format_string : good_format_strings) {
    for (const absl::string_view& timestamp_string : good_timestamp_strings) {
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format_string, timestamp_string,
                                  kMicroseconds, &time))
          << "\nformat_string: '" << format_string << "'"
          << "\ntimestamp_string: '" << timestamp_string << "'";
      EXPECT_EQ(time.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros());
    }
  }

  absl::string_view good_format_string_3("%M\0xy%Hzx", 8);
  absl::string_view good_timestamp_string_3("26\0xy13zx", 8);
  GOOGLESQL_EXPECT_OK(ParseStringToTime(good_format_string_3, good_timestamp_string_3,
                              kMicroseconds, &time));
  EXPECT_EQ(time.Packed64TimeMicros(),
            TimeValue::FromHMSAndMicros(13, 26, 0, 0).Packed64TimeMicros());
}

TEST(StringToTimestampTests, EmptyStringViewTests) {
  absl::string_view whitespace_format_string("     \n     ", 11);
  absl::string_view empty_format_string("", 0);
  absl::string_view null_format_string(nullptr, 0);
  std::vector<absl::string_view> format_strings =
      {whitespace_format_string, empty_format_string, null_format_string};

  absl::string_view whitespace_timestamp_string(" \n ", 3);
  absl::string_view empty_timestamp_string("", 0);
  absl::string_view null_timestamp_string(nullptr, 0);
  std::vector<absl::string_view> timestamp_strings =
      {whitespace_timestamp_string, empty_timestamp_string,
       null_timestamp_string};

  TimeValue time;
  for (const absl::string_view& format_string : format_strings) {
    for (const absl::string_view& timestamp_string : timestamp_strings) {
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format_string, timestamp_string,
                                  kMicroseconds, &time))
          << "\nformat_string("
          << (format_string.data() != nullptr ?
              absl::StrCat("'", format_string, "'") :
              "nullptr")
          << ", " << format_string.length() << "): '"
          << format_string << "'"
          << "\ntimestamp_string: '"
          << (timestamp_string.data() != nullptr ?
              absl::StrCat("'", timestamp_string, "'") :
              "nullptr")
          << ", " << timestamp_string.length() << "): '"
          << timestamp_string << "'";
      EXPECT_EQ(time.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(0, 0, 0, 0).Packed64TimeMicros());
    }
  }

  absl::string_view nonempty_format_string("a");
  absl::string_view nonempty_timestamp_string("b");

  // Matching an empty format string to non-empty timestamp string (or vice
  // versa) should never succeed.
  for (const absl::string_view& format_string : format_strings) {
    EXPECT_THAT(ParseStringToTime(format_string, nonempty_timestamp_string,
                                  kMicroseconds, &time),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Failed to parse input string")));
  }
  for (const absl::string_view& timestamp_string : timestamp_strings) {
    EXPECT_THAT(ParseStringToTime(nonempty_format_string, timestamp_string,
                                  kMicroseconds, &time),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Failed to parse input string")));
  }
}

TEST(StringToTimeTest, ErrorMessageDetail) {
  TimeValue time_value;
  EXPECT_THAT(
      ParseStringToTime("abcd", "abee", kMicroseconds, &time_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr("Mismatch between format character 'c' and string "
                    "character 'e' at string index: 2 with format: 'abcd'")));
}

TEST(StringToTimestampTests, LeadingAndTrailingWhitespaceTests) {
  // %n and %t both consume arbitrary whitespace
  absl::string_view format0("");
  absl::string_view format1(" ");
  absl::string_view format2("    ");
  absl::string_view format3("%n");
  absl::string_view format4(" %n");
  absl::string_view format5(" %n ");
  absl::string_view format6("%n ");
  absl::string_view format7("%t");
  absl::string_view format8(" %t");
  absl::string_view format9(" %t ");
  absl::string_view format10("%t ");
  absl::string_view format11(" %n   %t %n   ");
  std::vector<absl::string_view> formats =
      {format0, format1, format2, format3, format4, format5, format6, format7,
       format8, format9, format10, format11};

  absl::string_view time1("");
  absl::string_view time2(" ");
  absl::string_view time3("\n");
  absl::string_view time4(" \t");
  absl::string_view time5("\v ");
  absl::string_view time6(" \f ");
  absl::string_view time7("   \r   ");
  absl::string_view time8(" \f \t\v\n \r  ");
  std::vector<absl::string_view> times =
      {time1, time2, time3, time4, time5, time6, time7, time8};

  TimeValue time_value;
  for (absl::string_view format : formats) {
    for (absl::string_view time : times) {
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format, time, kMicroseconds, &time_value));
      EXPECT_EQ(time_value.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(0, 0, 0, 0).Packed64TimeMicros());
    }
  }

  // One additional test for an empty timestamp string, where the format
  // string ends in '%' (which is invalid).
  for (absl::string_view time : times) {
    EXPECT_THAT(
        ParseStringToTime("  %", time, kMicroseconds, &time_value),
        StatusIs(absl::StatusCode::kOutOfRange,
                 HasSubstr("Format string cannot end with a single '%'")));
  }

  // Tests with a significant leading format element, and trailing whitespace.
  format0 = absl::string_view("%M");
  format1 = absl::string_view("%M ");
  format2 = absl::string_view("%M    ");
  format3 = absl::string_view("%M%n");
  format4 = absl::string_view("%M %n");
  format5 = absl::string_view("%M %n ");
  format6 = absl::string_view("%M%n ");
  format7 = absl::string_view("%M%t");
  format8 = absl::string_view("%M %t");
  format9 = absl::string_view("%M %t ");
  format10 = absl::string_view("%M%t ");
  format11 = absl::string_view("%M %n   %t %n   ");
  formats = {format0, format1, format2, format3, format4, format5, format6,
             format7, format8, format9, format10, format11};

  time1 = absl::string_view("26");
  time2 = absl::string_view("26 ");
  time3 = absl::string_view("26\n");
  time4 = absl::string_view("26 \t");
  time5 = absl::string_view("26\v ");
  time6 = absl::string_view("26 \f ");
  time7 = absl::string_view("26 \r ");
  time8 = absl::string_view("26 \t\v\n\f\r ");
  times = {time1, time2, time3, time4, time5, time6, time7, time8};


  for (const absl::string_view& format : formats) {
    for (const absl::string_view& time : times) {
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format, time, kMicroseconds, &time_value))
          << "format: " << format << "\ntime: " << time;
      EXPECT_EQ(time_value.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros())
          << "format: " << format << "\ntime: " << time;
    }
  }

  // Tests with leading whitespace and trailing whitespace and a significant
  // format element in the middle.
  format0 = absl::string_view("%M");
  format1 = absl::string_view(" %M ");
  format2 = absl::string_view("   %M   ");
  format3 = absl::string_view("%n%M%n");
  format4 = absl::string_view(" %n%M %n");
  format5 = absl::string_view(" %n %M %n ");
  format6 = absl::string_view("%n %M%n ");
  format7 = absl::string_view("%t%M%t");
  format8 = absl::string_view(" %t%M %t");
  format9 = absl::string_view(" %t %M %t ");
  format10 = absl::string_view("%t %M%t ");
  format11 = absl::string_view(" %n   %t %n   %M %n   %t %n   ");
  formats = {format0, format1, format2, format3, format4, format5, format6,
             format7, format8, format9, format10, format11};

  time1 = absl::string_view("26");
  time2 = absl::string_view(" 26 ");
  time3 = absl::string_view("\n26\n");
  time4 = absl::string_view(" \t26 \t");
  time5 = absl::string_view("\v 26\v ");
  time6 = absl::string_view(" \f 26 \f ");
  time7 = absl::string_view("\r26\r");
  time8 = absl::string_view("\n \r \v\t\f26 \t\n\v\f\r   ");
  times = {time1, time2, time3, time4, time5, time6, time7, time8};

  for (const absl::string_view& format : formats) {
    for (const absl::string_view& time : times) {
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format, time, kMicroseconds, &time_value))
          << "format: " << format << "\ntime: " << time;
      EXPECT_EQ(time_value.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(0, 26, 0, 0).Packed64TimeMicros())
          << "format: " << format << "\ntime: " << time;
    }
  }
}

TEST(StringToTimestampTests, IntermediateWhitespaceTests) {
  // %n and %t both consume arbitrary whitespace
  absl::string_view format1("%M %H");
  absl::string_view format2("%M%n%H");
  absl::string_view format3("%M%t%H");
  absl::string_view format4("%M %t %n %t  %n   %H");
  std::vector<absl::string_view> formats =
      {format1, format2, format3, format4};

  absl::string_view time1("2611");
  absl::string_view time2("26   11");
  absl::string_view time3("26\n11");
  absl::string_view time4("26 \t11");
  absl::string_view time5("26\v 11");
  absl::string_view time6("26 \f 11");
  absl::string_view time7("26   \r   11");
  absl::string_view time8("26 \f   \t\v\n \r  11");
  std::vector<absl::string_view> times =
      {time1, time2, time3, time4, time5, time6, time7, time8};

  TimeValue time_value;
  for (const absl::string_view& format : formats) {
    for (const absl::string_view& time : times) {
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format, time, kMicroseconds, &time_value))
          << "format: " << format
          << "\ntime: " << time;
      EXPECT_EQ(time_value.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(11, 26, 0, 0).Packed64TimeMicros())
          << "format: " << format << "\ntime: " << time;
    }
  }
}

TEST(StringToTimestampTests,
     SecondsWithMoreThanOneDigitOfFractionalPrecisionTests) {
  // Only %E0S to %E9S is supported (0-9 subseconds digits).
  TimeValue time_value;
  EXPECT_THAT(ParseStringToTime("%E10S", "1", kMicroseconds, &time_value),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Failed to parse input string")));
}

// This test runs round trip test cases, where we take a date, format it,
// parse it, and re-format it.  We verify that the original and parsed
// dates match, and that the original and second formats match.  The dates
// tested are described below.
TEST(ParseFormatDateTimestampTests, RoundTripTests) {
  // The date ranges here cover 21 days, that include the last 11 days of a
  // civil year and the first 10 days of the next civil year.  These intervals
  // cover both leap years and non-leap years, and these intervals cover ISO
  // years with various start/end dates, and various lengths (52 and 53 weeks).
  std::vector<std::pair<absl::CivilDay, absl::CivilDay>> test_ranges = {
    {absl::CivilDay(1998, 12, 20), absl::CivilDay(1999, 1, 10)},
    {absl::CivilDay(1999, 12, 20), absl::CivilDay(2000, 1, 10)},
    {absl::CivilDay(2000, 12, 20), absl::CivilDay(2001, 1, 10)},
    {absl::CivilDay(2001, 12, 20), absl::CivilDay(2002, 1, 10)},
    {absl::CivilDay(2002, 12, 20), absl::CivilDay(2003, 1, 10)},
    {absl::CivilDay(2003, 12, 20), absl::CivilDay(2004, 1, 10)},
    {absl::CivilDay(2004, 12, 20), absl::CivilDay(2005, 1, 10)},
    {absl::CivilDay(2005, 12, 20), absl::CivilDay(2006, 1, 10)},
    {absl::CivilDay(2006, 12, 20), absl::CivilDay(2007, 1, 10)},
    {absl::CivilDay(2007, 12, 20), absl::CivilDay(2008, 1, 10)},
    {absl::CivilDay(2008, 12, 20), absl::CivilDay(2009, 1, 10)},
    {absl::CivilDay(2009, 12, 20), absl::CivilDay(2010, 1, 10)},
    {absl::CivilDay(2010, 12, 20), absl::CivilDay(2011, 1, 10)}};

  // Run all permutations of each of these element lists in order to get
  // full coverage.
  std::vector<std::vector<std::string>> round_trip_format_lists = {
    {"%Y", "%j"},     // year, dayofyear

    {"%Y", "%U", "%u"},  // year, week, weekday
    {"%Y", "%U", "%w"},  // year, week, weekday
    {"%Y", "%U", "%A"},  // year, week, weekday
    {"%Y", "%U", "%a"},  // year, week, weekday

    {"%Y", "%W", "%u"},  // year, week, weekday
    {"%Y", "%W", "%w"},  // year, week, weekday
    {"%Y", "%W", "%A"},  // year, week, weekday
    {"%Y", "%W", "%a"},  // year, week, weekday

    {"%G", "%J" },       // ISO year, ISO dayofyear
    {"%g", "%J" },       // ISO year, ISO dayofyear

    {"%G", "%V", "%u"},  // ISO year, ISO week, weekday
    {"%G", "%V", "%w"},  // ISO year, ISO week, weekday
    {"%G", "%V", "%A"},  // ISO year, ISO week, weekday
    {"%G", "%V", "%a"},  // ISO year, ISO week, weekday

    {"%g", "%V", "%u"},  // ISO year, ISO week, weekday
    {"%g", "%V", "%w"},  // ISO year, ISO week, weekday
    {"%g", "%V", "%A"},  // ISO year, ISO week, weekday
    {"%g", "%V", "%a"},  // ISO year, ISO week, weekday
  };

  for (const auto& test : test_ranges) {
    for (absl::CivilDay civil_day = test.first; civil_day <= test.second;
         civil_day++) {
      for (const std::vector<std::string>& round_trip_format_array :
               round_trip_format_lists) {
        std::vector<std::string> format_array_copy = round_trip_format_array;
        std::sort(format_array_copy.begin(), format_array_copy.end());
        do {
          // Generate the format string.
          std::string format = absl::StrJoin(format_array_copy, " ");
          std::string test_info = absl::StrCat("CivilDay: ",
                                               absl::FormatCivilTime(civil_day),
                                               ", format: '", format, "'");

          std::string formatted_date;
          int64_t test_date = civil_day - absl::CivilDay(1970, 01, 01);
          GOOGLESQL_EXPECT_OK(FormatDateToString(format, test_date, kExpandQandJ,
                                       &formatted_date))
              << test_info;
          absl::StrAppend(&test_info, ", formatted_date: '", formatted_date,
                          "'");

          int32_t round_trip_date;
          GOOGLESQL_EXPECT_OK(ParseStringToDate(
              format, formatted_date, /*parse_version2=*/true,
              &round_trip_date)) << test_info;

          std::string formatted_round_trip_date;
          GOOGLESQL_EXPECT_OK(FormatDateToString(
              format, round_trip_date, kExpandQandJ,
              &formatted_round_trip_date))
              << test_info;
          absl::StrAppend(&test_info, ", formatted_round_trip_date: '",
                          formatted_round_trip_date, "'");

          EXPECT_EQ(test_date, round_trip_date)
              << test_info;
        } while (std::next_permutation(format_array_copy.begin(),
                                       format_array_copy.end()));
      }
    }
  }
}

// Helper methods for parse date/timestamp tests.
namespace {

int64_t ToDateInt64(absl::CivilDay civil_day) {
  return civil_day - absl::CivilDay(1970, 01, 01);
}

std::string FormatDate(int32_t date) {
  std::string formatted_date;
  GOOGLESQL_EXPECT_OK(FormatDateToString("%Y-%m-%d", date, kExpandQandJ,
                               &formatted_date)) << date;
  return formatted_date;
}

std::string FormatDayOfWeek(int32_t date) {
  std::string formatted_dow;
  GOOGLESQL_EXPECT_OK(FormatDateToString("%a", date, kExpandQandJ,
                               &formatted_dow)) << FormatDate(date);
  return formatted_dow;
}

std::string FormatDayOfWeek(const DatetimeValue& datetime) {
  std::string formatted_dow;
  GOOGLESQL_EXPECT_OK(FormatDatetimeToString("%a", datetime, &formatted_dow))
      << datetime.DebugString();
  return formatted_dow;
}

struct ParseDatetimeTest {
  // Constructor for positive test cases.  These tests are run for all of
  // DATE, DATETIME, and TIMESTAMP functions.
  ParseDatetimeTest(const std::string& format_in,
                    const std::string& input_string_in,
                    const absl::CivilDay result_date_in)
      : format(format_in), input_string(input_string_in),
        result_date(result_date_in) {}

  // Similar to the above, but also allows the expected day-of-week to be
  // specified.
  ParseDatetimeTest(const std::string& format_in,
                    const std::string& input_string_in,
                    const absl::CivilDay result_date_in,
                    absl::string_view expected_dow_in)
      : format(format_in),
        input_string(input_string_in),
        result_date(result_date_in),
        expected_dow(expected_dow_in) {}

  // Constructor for tests that only apply to DATETIME and TIMESTAMP types
  // (when including format elements that specify hour/minute/second/etc.).
  ParseDatetimeTest(const std::string& format_in,
                    const std::string& input_string_in,
                    const DatetimeValue& result_datetime_in)
      : format(format_in), input_string(input_string_in),
        result_datetime(result_datetime_in), excludes_date(true) {}

  // Constructor for error test cases.  <excludes_date_in> should be set to
  // true if the test does not apply to DATE functions (e.g., if it includes
  // hour/minute/second elements).
  ParseDatetimeTest(const std::string& format_in,
                    const std::string& input_string_in,
                    absl::string_view error_substr_in,
                    bool excludes_date_in = false)
      : format(format_in),
        input_string(input_string_in),
        error_substr(error_substr_in),
        excludes_date(excludes_date_in) {}

  std::string DebugString() const {
    return absl::StrCat(
        "format: ", format, ", input_string: ", input_string,
        (result_date.has_value() ?
         absl::StrCat(", expected: ",
                      absl::FormatCivilTime(result_date.value())) : ""),
        (result_datetime.has_value() ?
         absl::StrCat(", expected: ", result_datetime.value().DebugString()) :
         ""),
        (error_substr.has_value() ?
         absl::StrCat(", expected error: '", error_substr.value(), "'") : ""));
  }

  std::string format;
  std::string input_string;

  // Only one of <result_date>, <result_datetime>, or <error_substr> will be
  // set (by construction).
  std::optional<absl::CivilDay> result_date;
  std::optional<DatetimeValue> result_datetime;
  std::optional<std::string> error_substr;

  // Will optionally be set if <result_date> is set.
  std::optional<std::string> expected_dow;

  // Whether or not the test is excluded for DATE functions.
  bool excludes_date = false;
};

void CheckParseTimestampResultImpl(const ParseDatetimeTest& test,
                                   const absl::Status& parse_status,
                                   std::optional<int32_t> parsed_date,
                                   std::optional<DatetimeValue> parsed_datetime,
                                   absl::string_view test_type) {
  if (test.result_date.has_value()) {
    GOOGLESQL_EXPECT_OK(parse_status) << " test: " << test.DebugString();

    if (parsed_date.has_value()) {
      // This handles cases of date-element tests for the DATE type.
      EXPECT_EQ(parsed_date.value(), ToDateInt64(test.result_date.value()))
        << "test_type(" << test_type << "), test: " << test.DebugString()
        << ", actual result date: " << FormatDate(parsed_date.value());
    }
    if (parsed_datetime.has_value()) {
      // This handles cases of date-element tests for DATETIME/TIMESTAMP.
      // Extract the DATE from the DATETIME value
      int32_t parsed_datetime_date;
      GOOGLESQL_EXPECT_OK(ExtractFromDatetime(DATE, parsed_datetime.value(),
                                    &parsed_datetime_date));
      EXPECT_EQ(parsed_datetime_date, ToDateInt64(test.result_date.value()))
        << "test_type(" << test_type << "), test: " << test.DebugString()
        << ", actual result date: " << FormatDate(parsed_datetime_date);
    }
    if (test.expected_dow.has_value()) {
      if (parsed_date.has_value()) {
        EXPECT_EQ(FormatDayOfWeek(parsed_date.value()),
                  test.expected_dow.value())
            << "test_type(" << test_type << "), test: " << test.DebugString();
      }
      if (parsed_datetime.has_value()) {
        EXPECT_EQ(FormatDayOfWeek(parsed_datetime.value()),
                  test.expected_dow.value())
            << "test_type(" << test_type << "), test: " << test.DebugString();
      }
    }
  } else if (test.result_datetime.has_value()) {
    GOOGLESQL_EXPECT_OK(parse_status) << "test_type(" << test_type << "), test: "
                            << test.DebugString();
    EXPECT_FALSE(parsed_date.has_value())
        << "test_type(" << test_type << "), parsed_date has_value";
    EXPECT_FALSE(test.expected_dow.has_value());

    if (parsed_datetime.has_value()) {
      EXPECT_EQ(parsed_datetime.value().DebugString(),
                test.result_datetime.value().DebugString())
        << "test_type(" << test_type << "), "
        << "format: " << test.format << ", input: " << test.input_string;
    }
  } else {
    EXPECT_TRUE(test.error_substr.has_value())
        << "test_type(" << test_type << "), test: " << test.DebugString();
    EXPECT_FALSE(parsed_date.has_value())
        << "test_type(" << test_type << "), test: " << test.DebugString()
        << ", parsed_date has value: " << FormatDate(parsed_date.value());
    EXPECT_FALSE(parsed_datetime.has_value())
        << "test_type(" << test_type << "), test: " << test.DebugString()
        << ", parsed_datetime has value: "
        << parsed_datetime.value().DebugString();

    const std::string successfully_parsed_datetime =
        ((parse_status.ok() && parsed_date.has_value()) ?
         FormatDate(parsed_date.value()) : "");
    EXPECT_THAT(parse_status,
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr(test.error_substr.value())))
        << "test_type(" << test_type << "), "
        << "format: " << test.format << ", input: " << test.input_string
        << successfully_parsed_datetime;
  }
}

void CheckParseTimestampResult(const ParseDatetimeTest& test,
                               const absl::Status& parse_status,
                               std::optional<int32_t> parsed_date,
                               absl::string_view test_type) {
  CheckParseTimestampResultImpl(test, parse_status, parsed_date, std::nullopt,
                                test_type);
}

void CheckParseTimestampResult(const ParseDatetimeTest& test,
                               const absl::Status& parse_status,
                               std::optional<DatetimeValue> parsed_datetime,
                               absl::string_view test_type) {
  CheckParseTimestampResultImpl(test, parse_status, std::nullopt,
                                parsed_datetime, test_type);
}

void RunParseDatetimeTest(const ParseDatetimeTest& test) {
  absl::Status parse_status;

  // Run the test for DATE, if needed.
  if (!test.excludes_date) {
    int32_t parsed_date;
    parse_status =
        ParseStringToDate(test.format, test.input_string,
                          /*parse_version2=*/true, &parsed_date);
    std::optional<int32_t> date = std::nullopt;
    if (parse_status.ok()) {
      date = parsed_date;
    }
    CheckParseTimestampResult(test, parse_status, date, "DATE");
  }

  // Run the test for DATETIME
  DatetimeValue parsed_datetime;
  parse_status =
      ParseStringToDatetime(test.format, test.input_string,
                            /*scale=*/kMicroseconds, /*parse_version2=*/true,
                            &parsed_datetime);

  std::optional<DatetimeValue> datetime = std::nullopt;
  if (parse_status.ok()) {
    datetime = parsed_datetime;
  }
  CheckParseTimestampResult(test, parse_status, datetime, "DATETIME");

  // Run the test for TIMESTAMP
  int64_t parsed_timestamp;
  parse_status =
      ParseStringToTimestamp(test.format, test.input_string,
                             absl::UTCTimeZone(), /*parse_version2=*/true,
                             &parsed_timestamp);

  datetime = std::nullopt;
  if (parse_status.ok()) {
    // Convert the parsed TIMESTAMP to DATETIME.  We first convert the
    // int64 timestamp to absl::Time, and then absl::Time to DatetimeValue
    absl::Time parsed_time = absl::FromUnixMicros(parsed_timestamp);
    GOOGLESQL_ASSERT_OK(ConvertTimestampToDatetime(parsed_time, absl::UTCTimeZone(),
                                         &parsed_datetime));
    datetime = parsed_datetime;
  }
  CheckParseTimestampResult(test, parse_status, datetime, "TIMESTAMP");
}

}  // namespace

TEST(ParseFormatDateTimestampTests, SpotTests) {
  // Spot test latest list of supported format elements:
  //
  // %j - day of year
  // %J - ISO day of year
  //
  // %U - non-ISO week number, weeks starting on Sunday, 00-53
  // %V - ISO week number, weeks starting on ???, ??-??
  // %W - non-ISO week number, weeks starting on Monday, 00-53
  //
  // %u - day of week, Monday first day of the week, 1-7
  // %w - day of week, Sunday first day of the week, 0-6
  // %A - full weekday name
  // %a - abbreviated weekday name

  std::vector<ParseDatetimeTest> tests = {
    // %j - day of year (non-ISO).
    {"%Y-%j", "1999-365", absl::CivilDay(1999, 12, 31)},

    // 1999 is not a leap year so day 366 fails.
    {"%Y-%j", "1999-366", "Year 1999 has 365 days, but the specified"},

    // 2000 is a leap year with 366 days.  Leading 0s are ok.
    {"%Y-%j", "2000-60",  absl::CivilDay(2000, 2, 29)},
    {"%Y-%j", "2000-060", absl::CivilDay(2000, 2, 29)},

    {"%Y-%j", "2000-366", absl::CivilDay(2000, 12, 31)},

    // %J - ISO day of year
    //
    // Example dates that are the first day of an ISO year:
    // 1) 1978-01-02 (ISO year 1978 has 364 days)
    // 2) 1979-01-01 (ISO year 1979 has 364 days)
    // 3) 1979-12-31 (ISO year 1980 has 364 days, even though it's a leap year)
    // 4) 1980-12-29 (ISO year 1981 has 371 days, 53 weeks)
    //
    // ISO weeks always start on Monday, so the first day of the ISO year
    // is also always a Monday.
    {"%G-%J", "1978-001", absl::CivilDay(1978, 1, 2), "Mon"},
    {"%G-%J", "1978-364", absl::CivilDay(1978, 12, 31)},
    {"%G-%J", "1978-365", "ISO Year 1978 has 364 days, but the specified day"},
    {"%G-%J", "1979-001", absl::CivilDay(1979, 1, 1), "Mon"},
    {"%G-%J", "1979-364", absl::CivilDay(1979, 12, 30)},
    {"%G-%J", "1980-001", absl::CivilDay(1979, 12, 31), "Mon"},
    {"%G-%J", "1980-364", absl::CivilDay(1980, 12, 28)},
    {"%G-%J", "1981-001", absl::CivilDay(1980, 12, 29), "Mon"},
    {"%G-%J", "1981-371", absl::CivilDay(1982, 1, 3)},
    {"%G-%J", "1982-001", absl::CivilDay(1982, 1, 4), "Mon"},

    // %U - non-ISO week number, weeks starting on Sunday, 00-53
    //
    // Week 0 of 1999 corresponds to the first day of that week, where the first
    // day of the week is a Sunday.
    // 1999-01-01 is a Friday.   The preceding Sunday is 1998-12-27.
    // 1999 only has 52 weeks, so week 53 is invalid, as are other out of range
    // week values.

    {"%Y-%U", "1999-0",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U", "1999-1",  absl::CivilDay(1999, 1, 3), "Sun"},
    {"%Y-%U", "1999-52", absl::CivilDay(1999, 12, 26), "Sun"},
    {"%Y-%U", "1999-53", "Week number 53 is invalid for year 1999"},
    {"%Y-%U", "1999-54", "Failed to parse input string"},

    // Results are equivalent to '1999-52' above.
    {"%Y-%U", "2000-00", absl::CivilDay(1999, 12, 26), "Sun"},
    {"%Y-%U", "2000-01", absl::CivilDay(2000, 1, 2), "Sun"},
    {"%Y-%U", "2000-52", absl::CivilDay(2000, 12, 24), "Sun"},
    {"%Y-%U", "2000-53", absl::CivilDay(2000, 12, 31), "Sun"},

    // January 1, 1995 is a Sunday, so 1995 starts with week 1, not week 0.
    {"%Y-%U", "1995-00", "Week number 0 is invalid for year 1995"},
    {"%Y-%U", "1995-01", absl::CivilDay(1995, 1, 1), "Sun"},
    {"%Y-%U", "1995-52", absl::CivilDay(1995, 12, 24), "Sun"},
    {"%Y-%U", "1995-53", absl::CivilDay(1995, 12, 31), "Sun"},

    // %W - non-ISO week number, weeks starting on Monday, 00-53
    //
    // Week 0 of 1999 corresponds to the first day of that week, where the first
    // day of the week is a Sunday.
    // 1999-01-01 is a Friday.   The preceding Monday is 1998-12-28.
    {"%Y-%W", "1999-0",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%W", "1999-1",  absl::CivilDay(1999, 1, 4), "Mon"},
    {"%Y-%W", "1999-52", absl::CivilDay(1999, 12, 27), "Mon"},
    {"%Y-%W", "1999-53", "Week number 53 is invalid for year 1999"},
    {"%Y-%W", "1999-54", "Failed to parse input string"},

    // Results are equivalent to '1999-52' above.
    {"%Y-%W", "2000-0",  absl::CivilDay(1999, 12, 27), "Mon"},
    {"%Y-%W", "2000-01", absl::CivilDay(2000, 1, 3), "Mon"},
    {"%Y-%W", "2000-52", absl::CivilDay(2000, 12, 25), "Mon"},
    {"%Y-%W", "2000-53", "Week number 53 is invalid for year 2000"},

    // January 1, 1996 is a Monday, so 1996 starts with week 1, not week 0.
    {"%Y-%W", "1996-00", "Week number 0 is invalid for year 1996"},
    {"%Y-%W", "1996-01", absl::CivilDay(1996, 1, 1), "Mon"},
    {"%Y-%W", "1996-52", absl::CivilDay(1996, 12, 23), "Mon"},
    {"%Y-%W", "1996-53", absl::CivilDay(1996, 12, 30), "Mon"},

    // %V - ISO week number, weeks starting on Monday, 01-53
    // Some ISO years have 52 weeks, some ISO years have 53 weeks.
    // Invalid week numbers for an ISO year produces an error.
    //
    // Example dates that are the first day of an ISO year:
    // 1) 1975-12-29 (ISO year 1976 has 371 days, 53 weeks)
    // 2) 1977-01-03 (ISO year 1977 has 364 days)
    // 3) 1978-01-02 (ISO year 1978 has 364 days)
    // 4) 1979-01-01 (ISO year 1979 has 364 days)
    // 5) 1979-12-31 (ISO year 1980 has 364 days, even though it's a leap year)
    // 6) 1980-12-29 (ISO year 1981 has 371 days, 53 weeks)
    {"%G-%V", "1976-01", absl::CivilDay(1975, 12, 29), "Mon"},
    {"%G-%V", "1976-53", absl::CivilDay(1976, 12, 27), "Mon"},
    {"%G-%V", "1976-54", "Failed to parse input string"},
    {"%G-%V", "1977-01", absl::CivilDay(1977, 1, 3), "Mon"},
    {"%G-%V", "1977-52", absl::CivilDay(1977, 12, 26), "Mon"},
    {"%G-%V", "1977-53", "Invalid ISO week 53 specified for ISO year 1977"},
    {"%G-%V", "1978-01", absl::CivilDay(1978, 1, 2), "Mon"},
    {"%G-%V", "1978-52", absl::CivilDay(1978, 12, 25), "Mon"},
    {"%G-%V", "1979-01", absl::CivilDay(1979, 1, 1), "Mon"},
    {"%G-%V", "1979-52", absl::CivilDay(1979, 12, 24), "Mon"},
    {"%G-%V", "1980-01", absl::CivilDay(1979, 12, 31), "Mon"},
    {"%G-%V", "1980-52", absl::CivilDay(1980, 12, 22), "Mon"},
    {"%G-%V", "1981-01", absl::CivilDay(1980, 12, 29), "Mon"},
    {"%G-%V", "1981-52", absl::CivilDay(1981, 12, 21), "Mon"},
    {"%G-%V", "1981-53", absl::CivilDay(1981, 12, 28), "Mon"},

    // %u - day of week, Monday first day of the week, 1-7
    //
    {"%Y-%U", "1999-0",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %u", "1999-0 1",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%U %u", "1999-0 7",  absl::CivilDay(1998, 12, 27), "Sun"},

    {"%Y-%W", "1999-0",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%W %u", "1999-0 1",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%W %u", "1999-0 7",  absl::CivilDay(1999, 1, 3), "Sun"},

    {"%G-%V", "1976-01", absl::CivilDay(1975, 12, 29), "Mon"},
    {"%G-%V %u", "1976-01 1", absl::CivilDay(1975, 12, 29), "Mon"},
    {"%G-%V %u", "1976-01 7", absl::CivilDay(1976, 1, 4), "Sun"},

    // %w - day of week, Sunday first day of the week, 0-6
    //
    {"%Y-%U", "1999-0",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %w", "1999-0 0",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %w", "1999-0 6",  absl::CivilDay(1999, 1, 2), "Sat"},

    {"%Y-%W", "1999-0",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%W %w", "1999-0 0",  absl::CivilDay(1999, 1, 3), "Sun"},
    {"%Y-%W %w", "1999-0 6",  absl::CivilDay(1999, 1, 2), "Sat"},

    {"%G-%V", "1976-01", absl::CivilDay(1975, 12, 29), "Mon"},
    {"%G-%V %w", "1976-01 0", absl::CivilDay(1976, 1, 4), "Sun"},
    {"%G-%V %w", "1976-01 6", absl::CivilDay(1976, 1, 3), "Sat"},

    // %A - full weekday name
    //
    {"%Y-%U", "1999-0",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %A", "1999-0 Sunday",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %A", "1999-0 Saturday",  absl::CivilDay(1999, 1, 2), "Sat"},

    {"%Y-%W", "1999-0",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%W %A", "1999-0 SunDay",  absl::CivilDay(1999, 1, 3), "Sun"},
    {"%Y-%W %A", "1999-0 Saturday",  absl::CivilDay(1999, 1, 2), "Sat"},

    {"%G-%V", "1976-01", absl::CivilDay(1975, 12, 29), "Mon"},
    {"%G-%V %A", "1976-01 SUNDAY", absl::CivilDay(1976, 1, 4), "Sun"},
    {"%G-%V %A", "1976-01 saturday", absl::CivilDay(1976, 1, 3), "Sat"},

    // %a - abbreviated weekday name
    {"%Y-%U", "1999-0",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %a", "1999-0 Sun",  absl::CivilDay(1998, 12, 27), "Sun"},
    {"%Y-%U %a", "1999-0 SAT",  absl::CivilDay(1999, 1, 2), "Sat"},

    {"%Y-%W", "1999-0",  absl::CivilDay(1998, 12, 28), "Mon"},
    {"%Y-%W %a", "1999-0 Sun",  absl::CivilDay(1999, 1, 3), "Sun"},
    {"%Y-%W %a", "1999-0 Sat",  absl::CivilDay(1999, 1, 2), "Sat"},

    {"%G-%V", "1976-01", absl::CivilDay(1975, 12, 29), "Mon"},
    {"%G-%V %a", "1976-01 sun", absl::CivilDay(1976, 1, 4), "Sun"},
    {"%G-%V %a", "1976-01 Sat", absl::CivilDay(1976, 1, 3), "Sat"},
  };

  for (const ParseDatetimeTest& test : tests) {
    RunParseDatetimeTest(test);
  }
}

TEST(ParseFormatDateTimestampTests, ParseWithModifiers) {
  // These tests are defined in pairs, where the first test of the pair
  // is without any modifier, and the second is the same basic test with
  // the modifier.  Currently, modifiers don't impact the result in any way
  // because there is no way for an engine to specify a different locale.
  std::vector<ParseDatetimeTest> tests = {
    // E modifier - using the locale's alternative representation.
    //
    // %Ec - date and time
    {"%c",  "Mon Jul 19 12:34:56 2021",
     DatetimeValue::FromYMDHMSAndMicros(2021, 7, 19, 12, 34, 56, 0)},
    {"%Ec", "Mon Jul 19 12:34:56 2021",
     DatetimeValue::FromYMDHMSAndMicros(2021, 7, 19, 12, 34, 56, 0)},

    // %EC - century
    {"%C", "20", absl::CivilDay(2000, 1, 1)},
    {"%EC", "20", absl::CivilDay(2000, 1, 1)},

    // %Ex - date
    {"%x", "07/19/21", absl::CivilDay(2021, 7, 19)},
    {"%Ex", "07/19/21", absl::CivilDay(2021, 7, 19)},

    // %EX - time
    {"%X",  "12:34:56",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 12, 34, 56, 0)},
    {"%EX",  "12:34:56",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 12, 34, 56, 0)},

    // %Ey - year
    {"%y", "21", absl::CivilDay(2021, 1, 1)},
    {"%Ey", "21", absl::CivilDay(2021, 1, 1)},

    // %EY - year
    {"%Y", "2021", absl::CivilDay(2021, 1, 1)},
    {"%EY", "2021", absl::CivilDay(2021, 1, 1)},

    // O modifier - using the locale's alternative numeric symbols.
    //
    // %Od - day of month
    {"%d", "11", absl::CivilDay(1970, 1, 11)},
    {"%Od", "11", absl::CivilDay(1970, 1, 11)},

    // %Oe - day of month
    {"%e", "11", absl::CivilDay(1970, 1, 11)},
    {"%Oe", "11", absl::CivilDay(1970, 1, 11)},

    // %OH - hour (00-23)
    {"%H",  "13",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 13, 0, 0, 0)},
    {"%OH",  "13",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 13, 0, 0, 0)},

    // %OI - hour (01-12)
    {"%I %p",  "11 am",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 11, 0, 0, 0)},
    {"%OI %p",  "11 AM",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 11, 0, 0, 0)},
    {"%I %p",  "11 pm",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 23, 0, 0, 0)},
    {"%OI %p",  "11 PM",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 23, 0, 0, 0)},

    // %Om - month (01-12)
    {"%m", "11", absl::CivilDay(1970, 11, 1)},
    {"%Om", "11", absl::CivilDay(1970, 11, 1)},

    // %OM - minute (00-59)
    {"%M",  "34",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 34, 0, 0)},
    {"%OM",  "34",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 34, 0, 0)},

    // %OS - second (00-60)
    {"%S",  "56",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 0, 56, 0)},
    {"%OS",  "56",
     DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 0, 56, 0)},

    // %Ou - weekday (1-7)
    {"%U %u", "2 3", absl::CivilDay(1970, 1, 14)},
    {"%U %Ou", "2 3", absl::CivilDay(1970, 1, 14)},
    {"%W %u", "2 3", absl::CivilDay(1970, 1, 14)},
    {"%W %Ou", "2 3", absl::CivilDay(1970, 1, 14)},

    // %OU - week number (00-53)
    {"%U",  "2", absl::CivilDay(1970, 1, 11)},
    {"%OU", "2", absl::CivilDay(1970, 1, 11)},

    // %OV - ISO week number (01-53)
    {"%G %V",  "2000 15", absl::CivilDay(2000, 4, 10)},
    {"%G %OV", "2000 15", absl::CivilDay(2000, 4, 10)},

    // %Ow - weekday (0-6)
    {"%U %w",  "2 3", absl::CivilDay(1970, 1, 14)},
    {"%U %Ow",  "2 3", absl::CivilDay(1970, 1, 14)},
    {"%W %w",  "2 3", absl::CivilDay(1970, 1, 14)},
    {"%W %Ow",  "2 3", absl::CivilDay(1970, 1, 14)},

    // %OW - week number (00-53)
    {"%W",  "2", absl::CivilDay(1970, 1, 12)},
    {"%OW", "2", absl::CivilDay(1970, 1, 12)},

    // %Oy - year
    {"%y",  "21", absl::CivilDay(2021, 1, 1)},
    {"%Oy", "21", absl::CivilDay(2021, 1, 1)},
  };

  for (const ParseDatetimeTest& test : tests) {
    RunParseDatetimeTest(test);
  }
}

TEST(ParseFormatDateTimestampTests, CollisionTestsNewParts) {
  // Tests format strings with overlapping/colliding element specifications.
  std::vector<ParseDatetimeTest> tests = {
    // For redundant elements, rightmost wins.
    {"%Y-%j-%j", "1978-001-007", absl::CivilDay(1978, 1, 7)},
    {"%J-%G-%J", "300-1978-001", absl::CivilDay(1978, 1, 2)},

    {"%Y-%U-%U", "1999-50-1",  absl::CivilDay(1999, 1, 3)},
    {"%Y-%W-%U", "1999-50-1",  absl::CivilDay(1999, 1, 3)},
    {"%Y-%U-%W", "1999-50-1",  absl::CivilDay(1999, 1, 4)},
    {"%Y-%W-%W", "1999-50-1",  absl::CivilDay(1999, 1, 4)},

    {"%G-%V-%V", "1976-50-01", absl::CivilDay(1975, 12, 29)},

    // Test the various elements for day of week (%u %w %A %a'),
    // in different orders.
    //
    // The base date is 1999 week 1, which is 1999-01-04.  The last day-of-week
    // element value is always Monday in these test cases, so the result is the
    // same for each case.
    {"%Y-%W %u %w %A %a", "1999-1 3 4 Sunday Mon", absl::CivilDay(1999, 1, 4)},
    {"%Y-%W %u %w %a %A", "1999-1 3 4 Sun Monday", absl::CivilDay(1999, 1, 4)},
    {"%Y-%W %u %A %a %w", "1999-1 4 Sunday Tue 1", absl::CivilDay(1999, 1, 4)},
    {"%Y-%W %w %A %a %u", "1999-1 3 Sunday Tue 1", absl::CivilDay(1999, 1, 4)},

    // Similar tests with ISO specifications.
    {"%G-%V %u %w %A %a", "1978-1 3 4 Sunday Mon", absl::CivilDay(1978, 1, 2)},
    {"%G-%V %u %w %a %A", "1978-1 3 4 Sun Monday", absl::CivilDay(1978, 1, 2)},
    {"%G-%V %u %A %a %w", "1978-1 4 Sunday Tue 1", absl::CivilDay(1978, 1, 2)},
    {"%G-%V %w %A %a %u", "1978-1 3 Sunday Tue 1", absl::CivilDay(1978, 1, 2)},

    // Day of year overrides week if dayofweek is unspecified, regardless of
    // position.
    {"%Y-%U-%j", "1978-50-007", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%U", "1978-007-50", absl::CivilDay(1978, 1, 7)},
    {"%Y-%W-%j", "1978-50-007", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%W", "1978-007-50", absl::CivilDay(1978, 1, 7)},

    {"%G-%V-%J", "1978-50-001", absl::CivilDay(1978, 1, 2)},
    {"%G-%J-%V", "1978-001-50", absl::CivilDay(1978, 1, 2)},

    // If day-of-year and week+weekday are both specified, then the rightmost
    // of day-of-year and week wins (the position of day-of-week is ignored).
    {"%Y-%U-%j-%A", "1978-50-007-Sunday", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%U-%a", "1978-007-50-Sun", absl::CivilDay(1978, 12, 10), "Sun"},
    {"%Y-%W-%j-%u", "1978-50-007-1", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%W-%w", "1978-007-50-1", absl::CivilDay(1978, 12, 11), "Mon"},

    // Similar tests for ISO day-of-year vs. week+weekday.
    {"%G-%V-%J-%A", "1978-50-001-Monday", absl::CivilDay(1978, 1, 2)},
    {"%G-%J-%V-%a", "1978-001-50-Tue", absl::CivilDay(1978, 12, 12), "Tue"},
    {"%G-%V-%J-%u", "1978-50-001-1", absl::CivilDay(1978, 1, 2)},
    {"%G-%J-%V-%w", "1978-001-50-2", absl::CivilDay(1978, 12, 12), "Tue"},
  };

  for (const ParseDatetimeTest& test : tests) {
    RunParseDatetimeTest(test);
  }
}

TEST(ParseFormatDateTimestampTests, CollisionTestsOriginalParts) {
  // Tests format strings with overlapping/colliding element specifications,
  // new elements vs. original elements.
  std::vector<ParseDatetimeTest> tests = {
    // For redundant elements, rightmost wins.
    // %B, %b, %h - month
    {"%Y-%B-%j", "1978-Dec-007", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%B", "1978-008-Dec", absl::CivilDay(1978, 12, 8)},
    // We compute day 60 from 1978 to initially set the day/month (03/01),
    // but then override the month.
    {"%Y-%j-%B", "1978-060-Dec", absl::CivilDay(1978, 12, 1)},

    // %b and %h behave the same as similar case above with %B
    {"%Y-%b-%j", "1978-Dec-007", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%b", "1978-008-Dec", absl::CivilDay(1978, 12, 8)},
    {"%Y-%h-%j", "1978-Dec-007", absl::CivilDay(1978, 1, 7)},
    {"%Y-%j-%h", "1978-008-Dec", absl::CivilDay(1978, 12, 8)},

    // %C - century (sort of an orthogonal case because it only overlaps
    // with %Y, not %j since century doesn't overlap with month/day).
    {"%Y-%C-%j", "1978-20-060", absl::CivilDay(2000, 2, 29)},
    {"%Y-%j-%C", "1978-060-20", absl::CivilDay(2000, 2, 29)},

    // %c - date and time representation in the format '%a %b %e %T %Y'.
    {"%Y-%c-%j", "2001-Tue Jul 20 12:34:56 2021-001",
     DatetimeValue::FromYMDHMSAndMicros(2021, 1, 1, 12, 34, 56, 0)},
    {"%Y-%j-%c", "2001-001-Tue Jul 20 12:34:56 2021",
     DatetimeValue::FromYMDHMSAndMicros(2021, 7, 20, 12, 34, 56, 0)},
    {"%j-%c-%Y", "001-Tue Jul 20 12:34:56 2021-2001",
     DatetimeValue::FromYMDHMSAndMicros(2001, 7, 20, 12, 34, 56, 0)},

    // %D - date
    {"%Y %D %j", "1978 12/31/00 060", absl::CivilDay(2000, 2, 29)},
    {"%Y %j %D", "1978 007 12/31/00", absl::CivilDay(2000, 12, 31)},

    // %d - day of month
    {"%Y %d %j", "1978 31 007", absl::CivilDay(1978, 1, 7)},
    {"%Y %j %d", "1978 070 31", absl::CivilDay(1978, 3, 31)},
    {"%Y %j %d", "2000 032 29", absl::CivilDay(2000, 2, 29)},
    // Corner case - we compute the month using %j which identifies February,
    // then try to set the day to 31.  This fails since February of 1978 only
    // as 28 days.
    {"%Y %j %d", "1978 032 29", "Out-of-range datetime field"},
    {"%Y %j %d", "2000 032 30", "Out-of-range datetime field"},

    // %e - day of month
    {"%Y %e %j", "1978 31 033", absl::CivilDay(1978, 2, 2)},
    {"%Y %j %e", "1978 033 28", absl::CivilDay(1978, 2, 28)},
    {"%Y %j %e", "1978 033 29", "Out-of-range datetime field"},

    // %F - date
    {"%Y %F %j", "1978 2000-12-31 033", absl::CivilDay(2000, 2, 2)},
    {"%Y %j %F", "1978 033 2000-12-31", absl::CivilDay(2000, 12, 31)},
    {"%F %j %Y", "2000-12-31 033 1978", absl::CivilDay(1978, 2, 2)},
    {"%j %F %Y", "033 2000-12-31 1978", absl::CivilDay(1978, 12, 31)},

    // %Q - quarter
    {"%Y %Q %j", "1978 4 007", absl::CivilDay(1978, 1, 7)},
    {"%Y %j %Q", "1978 007 4", absl::CivilDay(1978, 10, 1)},

    // %x - date
    {"%Y %x %j", "1978 12/31/00 033", absl::CivilDay(2000, 2, 2)},
    {"%Y %j %x", "1978 007 12/31/00", absl::CivilDay(2000, 12, 31)},
    {"%x %j %Y", "12/31/00 033 1978", absl::CivilDay(1978, 2, 2)},
    {"%j %x %Y", "033 12/31/00 1978", absl::CivilDay(1978, 12, 31)},

    // %Y - year
    // %j is computed based on the last/final value of year.  Note that
    // day 60 is different in 2000 vs. 2001 because 2000 is a leap year.
    {"%Y %j %Y", "2000 60 2001", absl::CivilDay(2001, 3, 1)},
    {"%Y %j %Y", "2001 60 2000", absl::CivilDay(2000, 2, 29)},

    // %y - year
    {"%y %j %y", "00 60 01", absl::CivilDay(2001, 3, 1)},
    {"%y %j %y", "01 60 00", absl::CivilDay(2000, 2, 29)},

    // %y and %Y - year
    {"%Y %j %y", "2000 60 01", absl::CivilDay(2001, 3, 1)},
    {"%y %j %Y", "01 60 2000", absl::CivilDay(2000, 2, 29)},

    // Similar tests for week (%W) as those above with %j.
    //
    // %B, %b, %h - month
    // Note - the first day of 1978 week 6 is 02/06 which is a Monday.
    {"%Y-%W", "1978-06", absl::CivilDay(1978, 2, 6)},
    {"%Y-%B-%W", "1978-Dec-06", absl::CivilDay(1978, 2, 6)},
    {"%Y-%B-%W-%u", "1978-Dec-06-1", absl::CivilDay(1978, 2, 6)},
    {"%Y-%B-%W-%u", "1978-Dec-06-2", absl::CivilDay(1978, 2, 7)},

    // Mind bender.  %w labels days 0-6, Sunday through Saturday.  So
    // identifying Monday using '1' corresponds to the first day of
    // week (%W) 6.  But day (%w) 0 is Sunday, which is the last day
    // of week (%W) 6.
    {"%Y-%B-%W-%w", "1978-Dec-06-1", absl::CivilDay(1978, 2, 6)},
    {"%Y-%B-%W-%w", "1978-Dec-06-0", absl::CivilDay(1978, 2, 12)},

    // We compute week 6 from 1978 to initially set the day/month (02/06),
    // but then override the month.
    {"%Y-%W-%B", "1978-06-Dec", absl::CivilDay(1978, 12, 6)},
    {"%Y-%W-%B-%w", "1978-06-Dec-2", absl::CivilDay(1978, 12, 7)},
    {"%Y-%W-%w-%B", "1978-06-2-Dec", absl::CivilDay(1978, 12, 7)},

    // %b and %h behave the same as similar case above with %B
    {"%Y-%b-%W", "1978-Dec-06", absl::CivilDay(1978, 2, 6)},
    {"%Y-%W-%b", "1978-06-Dec", absl::CivilDay(1978, 12, 6)},
    {"%Y-%h-%W", "1978-Dec-06", absl::CivilDay(1978, 2, 6)},
    {"%Y-%W-%h", "1978-6-Dec", absl::CivilDay(1978, 12, 6)},

    // %C - century (sort of an orthogonal case because it only overlaps
    // with %Y, not %W since century doesn't overlap with month/day).
    {"%Y-%W", "1999-10", absl::CivilDay(1999, 3, 8)},
    {"%Y-%W", "2000-10", absl::CivilDay(2000, 3, 6)},
    {"%Y-%C-%W", "1999-20-10", absl::CivilDay(2000, 3, 6)},
    {"%Y-%W-%C", "1999-10-20", absl::CivilDay(2000, 3, 6)},

    // %c - date and time representation in the format '%a %b %e %T %Y'.
    {"%Y-%c-%W", "2000-Tue Jul 20 12:34:56 1999-10",
     DatetimeValue::FromYMDHMSAndMicros(1999, 3, 8, 12, 34, 56, 0)},
    {"%Y-%W-%c", "2000-10-Tue Jul 20 12:34:56 1999",
     DatetimeValue::FromYMDHMSAndMicros(1999, 7, 20, 12, 34, 56, 0)},
    {"%W-%c-%Y", "10-Tue Jul 20 12:34:56 1999-2000",
     DatetimeValue::FromYMDHMSAndMicros(2000, 7, 20, 12, 34, 56, 0)},
    {"%c-%W-%Y", "Tue Jul 20 12:34:56 1999-10-2000",
     DatetimeValue::FromYMDHMSAndMicros(2000, 3, 6, 12, 34, 56, 0)},

    // %D - date
    {"%Y %D %W", "1999 12/31/00 10", absl::CivilDay(2000, 3, 6)},
    {"%Y %W %D", "1999 10 12/31/00", absl::CivilDay(2000, 12, 31)},
    {"%Y %W %D %u", "1999 10 12/31/00 2", absl::CivilDay(2000, 12, 31)},

    // %d - day of month
    {"%Y %d %W", "1999 31 10", absl::CivilDay(1999, 3, 8)},
    {"%Y %W %d", "1999 10 31", absl::CivilDay(1999, 3, 31)},

    // Week 9 of 2000 starts on 2/28, it's a leap year so we can set the
    // day of month to 29.
    {"%Y %W", "2000 09", absl::CivilDay(2000, 2, 28)},
    {"%Y %W %d", "2000 09 29", absl::CivilDay(2000, 2, 29)},

    // Corner case - we compute the month using %W which identifies February,
    // then try to set the day to 29.  This fails since February of 1999 only
    // has 28 days.  Similar test for day 30 of February 2000.
    {"%Y %W %d", "1999 08 29", "Out-of-range datetime field"},
    {"%Y %W %d", "2000 09 30", "Out-of-range datetime field"},

    // %e - day of month (similar to above tests)
    {"%Y %e %W", "1999 31 10", absl::CivilDay(1999, 3, 8)},
    {"%Y %W %e", "1999 10 31", absl::CivilDay(1999, 3, 31)},
    {"%Y %W %e", "2000 09 29", absl::CivilDay(2000, 2, 29)},
    {"%Y %W %e", "1999 08 29", "Out-of-range datetime field"},
    {"%Y %W %e", "2000 09 30", "Out-of-range datetime field"},

    // %F - date
    {"%Y %F %W", "1999 2000-12-31 10", absl::CivilDay(2000, 3, 6)},
    {"%Y %W %F", "1999 10 2000-12-31", absl::CivilDay(2000, 12, 31)},
    {"%F %W %Y", "2000-12-31 10 1999", absl::CivilDay(1999, 3, 8)},
    {"%W %F %Y", "10 2000-12-31 1999", absl::CivilDay(1999, 12, 31)},

    // %Q - quarter
    {"%Y %Q %W", "1999 4 10", absl::CivilDay(1999, 3, 8)},
    {"%Y %W %Q", "1999 10 4", absl::CivilDay(1999, 10, 1)},

    // %x - date (similar tests as for %F)
    {"%Y %x %W", "1999 12/31/00 10", absl::CivilDay(2000, 3, 6)},
    {"%Y %W %x", "1999 10 12/31/00", absl::CivilDay(2000, 12, 31)},
    {"%x %W %Y", "12/31/00 10 1999", absl::CivilDay(1999, 3, 8)},
    {"%W %x %Y", "10 12/31/00 1999", absl::CivilDay(1999, 12, 31)},

    // %Y - year
    // %W is computed based on the last/final value of year.  Note that
    // week 10 is different in 2000 vs. 2001 because 2000 is a leap year.
    {"%Y %W %Y", "2000 10 1999", absl::CivilDay(1999, 3, 8)},
    {"%Y %W %Y", "1999 10 2000", absl::CivilDay(2000, 3, 6)},

    // %y - year
    {"%y %W %y", "00 10 99", absl::CivilDay(1999, 3, 8)},
    {"%y %W %y", "99 10 00", absl::CivilDay(2000, 3, 6)},
  };

  for (const ParseDatetimeTest& test : tests) {
    RunParseDatetimeTest(test);
  }
}

TEST(ParseFormatDateTimestampTests, ISOandNonISOTests) {
  // Tests where both ISO and non-ISO parts are present. ISO parts are always
  // ignored in this case.

  struct NonISOInfo {
    std::string element;
    std::string value;
    std::string expected_result;
  };

  // ISO parts are ignored if any non-ISO parts are present,
  // regardless of position.  This list does not include ISO parts, nor
  // does it include day of week parts (%A, %a, %u, %w).
  std::vector<NonISOInfo> non_iso_elements = {
    {"%B", "December", "1970-12-01"},    // month
    {"%b", "Dec", "1970-12-01"},         // abbreviated month
    {"%h", "Dec", "1970-12-01"},         // abbreviated month
    {"%C", "20", "2000-01-01"},          // century
    {"%D", "12/31/00", "2000-12-31"},    // date
    {"%Y", "2000", "2000-01-01"},        // year
  };

  // The ISO specified parts %G-%J map to 1979-01-01.  The result should
  // never match this date, since the ISO parts should be ignored given that
  // a non-ISO element is also specified.
  for (const NonISOInfo& element_info : non_iso_elements) {
    std::string format(element_info.element);
    absl::StrAppend(&format, "-%G-%J");
    std::string input(element_info.value);
    absl::StrAppend(&input, "-1979-001");
    int32_t parsed_date;
    GOOGLESQL_EXPECT_OK(ParseStringToDate(format, input, /*parse_version2=*/true,
                                &parsed_date))
        << "format: " << format << ", input: " << input;
    EXPECT_EQ(FormatDate(parsed_date), element_info.expected_result)
        << "format: " << format << ", input: " << input;

    // Similar to the above, but place ISO elements at the start rather than
    // at the end.
    std::string prefix_format(element_info.element);
    absl::StrAppend(&prefix_format, "%G-%J-");
    std::string prefix_input(element_info.value);
    absl::StrAppend(&prefix_input, "1979-001-");
    GOOGLESQL_EXPECT_OK(ParseStringToDate(prefix_format, prefix_input,
                                /*parse_version2=*/true, &parsed_date))
        << "prefix_format: " << prefix_format << ", prefix_input: "
        << prefix_input;
    EXPECT_EQ(FormatDate(parsed_date), element_info.expected_result)
        << "prefix_format: " << prefix_format << ", prefix_input: "
        << prefix_input;
  }

  // One extra test for %c, which only works for datetime/timestamp (not date).
  DatetimeValue parsed_datetime;
  const std::string datetime_string = "Tue Jul 20 12:34:56 2021";
  GOOGLESQL_EXPECT_OK(ParseStringToDatetime(
      "%c-%G-%J", absl::StrCat(datetime_string, "-2001-001"),
      /*scale=*/kMicroseconds, /*parse_version2=*/true, &parsed_datetime));
  std::string formatted_string;
  GOOGLESQL_EXPECT_OK(FormatDatetimeToString("%c", parsed_datetime, &formatted_string));
  EXPECT_EQ(datetime_string, formatted_string);
}

TEST(ParseFormatDateTimestampTests, ParseMeridianIndicatorTests) {
  // The %p element parses meridian indicators ("am","pm") in a case
  // insensitive manner. For parse_version2, %P behaves the same as %p.

  const std::string format_strings[] = {"%p", "%P"};
  const std::string datetime_strings[] = {"am", "pm", "AM", "aM",
                                          "Am", "pM", "Pm"};
  for (const std::string& format_string : format_strings) {
    for (const std::string& datetime_string : datetime_strings) {
      TimeValue time;
      GOOGLESQL_EXPECT_OK(ParseStringToTime(format_string, datetime_string, kMicroseconds,
                                  &time));
      EXPECT_EQ(time.Packed64TimeMicros(),
                TimeValue::FromHMSAndMicros(0, 0, 0, 0).Packed64TimeMicros());

      DatetimeValue datetime;
      GOOGLESQL_EXPECT_OK(ParseStringToDatetime(format_string, datetime_string,
                                      kMicroseconds,
                                      /*parse_version2=*/true, &datetime));
      EXPECT_EQ(datetime.Packed64DatetimeMicros(),
                DatetimeValue::FromYMDHMSAndMicros(1970, 01, 01, 00, 00, 00, 0)
                    .Packed64DatetimeMicros());

      absl::Time result_timestamp;
      GOOGLESQL_EXPECT_OK(ParseStringToTimestamp(format_string, datetime_string, "UTC",
                                       /*parse_version2=*/true,
                                       &result_timestamp));
      GOOGLESQL_EXPECT_OK(ConvertTimestampToDatetime(result_timestamp, "UTC", &datetime));
      EXPECT_EQ(datetime.Packed64DatetimeMicros(),
                DatetimeValue::FromYMDHMSAndMicros(1970, 01, 01, 00, 00, 00, 0)
                    .Packed64DatetimeMicros());
    }
  }
}

struct ParseStringToPicosSuccessTestCase {
  std::string input_string;
};

class ParseStringToPicosSuccessTest
    : public testing::TestWithParam<ParseStringToPicosSuccessTestCase> {};

TEST_P(ParseStringToPicosSuccessTest, ParseStringToPicosTest) {
  const ParseStringToPicosSuccessTestCase& test_case = GetParam();

  PicoTime pico_time;
  ParseTimestampError error = ParseTimestampError::kOk;
  GOOGLESQL_EXPECT_OK(ParseStringToTimestamp("%Y-%m-%d %H:%M:%E*S%Ez",
                                   test_case.input_string, absl::UTCTimeZone(),
                                   &pico_time, &error));
  EXPECT_EQ(error, ParseTimestampError::kOk);
}

INSTANTIATE_TEST_SUITE_P(
    ParseStringToPicosSuccessTests, ParseStringToPicosSuccessTest,
    testing::ValuesIn<ParseStringToPicosSuccessTestCase>({
        // lower bound.
        {"0001-01-01 00:00:00+00"},

        // Because of the time zone specified, the timestamp value is within the
        // valid range.
        {"0000-12-31 23:59:59.999999999999-05"},

        // upper bound.
        {"9999-12-31 23:59:59.999999999999+00"},

        // Because of the time zone specified, the timestamp value is within the
        // valid range.
        {"10000-01-01 00:00:00+01"},
    }));

struct ParseStringToPicosFailureTestCase {
  std::string input_string;
  ParseTimestampError expected_error;
};

class ParseStringToPicosFailureTest
    : public testing::TestWithParam<ParseStringToPicosFailureTestCase> {};

TEST_P(ParseStringToPicosFailureTest, ParseStringToPicosTest) {
  const ParseStringToPicosFailureTestCase& test_case = GetParam();

  PicoTime pico_time;
  ParseTimestampError error;
  EXPECT_THAT(
      ParseStringToTimestamp("%Y-%m-%d %H:%M:%E*S%Ez", test_case.input_string,
                             absl::UTCTimeZone(), &pico_time, &error),
      StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_EQ(error, test_case.expected_error);
}

INSTANTIATE_TEST_SUITE_P(
    ParseStringToPicosFailureTests, ParseStringToPicosFailureTest,
    testing::ValuesIn<ParseStringToPicosFailureTestCase>({
        // less that lower bound error case.
        {"0000-12-31 23:59:59.999999999999+00",
         ParseTimestampError::kLessThanLowerBound},

        // Because of the time zone specified, the timestamp value is lower than
        // the valid lower bound.
        {"0001-01-01 00:00:00+05", ParseTimestampError::kLessThanLowerBound},

        // greater than upper bound error case.
        {"10000-01-01 00:00:00+00",
         ParseTimestampError::kGreaterThanUpperBound},

        // Because of the time zone specified, the timestamp value is greater
        // than
        // the valid upper bound.
        {"9999-12-31 23:59:59.999999999999-05",
         ParseTimestampError::kGreaterThanUpperBound},

        // other error case.
        {"abcd", ParseTimestampError::kMalformed},
    }));

}  // namespace
}  // namespace functions
}  // namespace googlesql
