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

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value_test_util.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT (build/include)
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/util/time_util.h"

namespace googlesql {

constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;

namespace {

using ::google::protobuf::util::TimeUtil;

std::vector<QueryParamsWithResult> WrapFeatureIntervalType(
    absl::Span<const QueryParamsWithResult> params) {
  std::vector<QueryParamsWithResult> wrapped_params;
  wrapped_params.reserve(params.size());
  for (const auto& param : params) {
    wrapped_params.emplace_back(param.WrapWithFeature(FEATURE_INTERVAL_TYPE));
  }
  return wrapped_params;
}

std::vector<QueryParamsWithResult> WrapFeaturesIntervalAndCivilTimeTypes(
    absl::Span<const QueryParamsWithResult> params) {
  std::vector<QueryParamsWithResult> wrapped_params;
  wrapped_params.reserve(params.size());
  for (const auto& param : params) {
    wrapped_params.emplace_back(
        param.WrapWithFeatureSet({FEATURE_CIVIL_TIME, FEATURE_INTERVAL_TYPE}));
  }
  return wrapped_params;
}

std::vector<FunctionTestCall> WrapFeatureIntervalType(
    absl::Span<const FunctionTestCall> tests) {
  std::vector<FunctionTestCall> wrapped_tests;
  wrapped_tests.reserve(tests.size());
  for (auto call : tests) {
    call.params = call.params.WrapWithFeature(FEATURE_INTERVAL_TYPE);
    wrapped_tests.emplace_back(call);
  }
  return wrapped_tests;
}

Value Years(int64_t value) {
  return Value::Interval(interval_testing::Years(value));
}
Value Months(int64_t value) {
  return Value::Interval(interval_testing::Months(value));
}
Value Days(int64_t value) {
  return Value::Interval(interval_testing::Days(value));
}
Value Hours(int64_t value) {
  return Value::Interval(interval_testing::Hours(value));
}
Value Minutes(int64_t value) {
  return Value::Interval(interval_testing::Minutes(value));
}
Value Seconds(int64_t value) {
  return Value::Interval(interval_testing::Seconds(value));
}
Value Micros(int64_t value) {
  return Value::Interval(interval_testing::Micros(value));
}
Value Nanos(__int128 value) {
  return Value::Interval(interval_testing::Nanos(value));
}
Value YMDHMS(int64_t year, int64_t month, int64_t day, int64_t hour,
             int64_t minute, int64_t second) {
  return Value::Interval(
      interval_testing::YMDHMS(year, month, day, hour, minute, second));
}
Value FromString(absl::string_view str) {
  return Value::Interval(
      *IntervalValue::ParseFromString(str, /*allow_nanos=*/true));
}
Value SecondsNanos(int64_t seconds, __int128 nanos) {
  return Nanos(seconds * IntervalValue::kNanosInSecond + nanos);
}

Value NullProtoDuration() { return Value::Null(ProtoDurationType()); }

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsIntervalConstructor() {
  std::vector<FunctionTestCall> tests = {
      {"", {NullInt64(), "YEAR"}, NullInterval()},
      {"", {Int64(0), "YEAR"}, Years(0)},
      {"", {Int64(1), "YEAR"}, Years(1)},
      {"", {Int64(-10000), "YEAR"}, Years(-10000)},
      {"", {Int64(0), "QUARTER"}, Months(0)},
      {"", {Int64(40000), "QUARTER"}, Years(10000)},
      {"", {Int64(-3), "MONTH"}, Months(-3)},
      {"", {Int64(120000), "MONTH"}, Months(120000)},
      {"", {Int64(4), "WEEK"}, Days(28)},
      {"", {Int64(-522857), "WEEK"}, Days(-3659999)},
      {"", {Int64(-5), "DAY"}, Days(-5)},
      {"", {Int64(3660000), "DAY"}, Days(3660000)},
      {"", {Int64(6), "HOUR"}, Hours(6)},
      {"", {Int64(-87840000), "HOUR"}, Hours(-87840000)},
      {"", {Int64(-7), "MINUTE"}, Minutes(-7)},
      {"", {Int64(5270400000), "MINUTE"}, Minutes(5270400000)},
      {"", {Int64(8), "SECOND"}, Seconds(8)},
      {"", {Int64(-316224000000), "SECOND"}, Seconds(-316224000000)},
      {"", {Int64(123), "MILLISECOND"}, Micros(123000)},
      {"", {Int64(-123), "MILLISECOND"}, Micros(-123000)},
      {"", {Int64(316224000000000), "MILLISECOND"}, Micros(316224000000000000)},
      {"",
       {Int64(-316224000000000), "MILLISECOND"},
       Micros(-316224000000000000)},
      {"", {Int64(123), "MICROSECOND"}, Micros(123)},
      {"", {Int64(-123), "MICROSECOND"}, Micros(-123)},
      {"",
       {Int64(316224000000000000), "MICROSECOND"},
       Micros(316224000000000000)},
      {"",
       {Int64(-316224000000000000), "MICROSECOND"},
       Micros(-316224000000000000)},
      {"", QueryParamsWithResult({Int64(123), "NANOSECOND"}, Nanos(123))
               .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"", QueryParamsWithResult({Int64(-123), "NANOSECOND"}, Nanos(-123))
               .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},

      // Exceeds maximum allowed value
      {"", {Int64(10001), "YEAR"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(-40001), "QUARTER"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(120001), "MONTH"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(522858), "WEEK"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(-3660001), "DAY"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(87840001), "HOUR"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(-5270400001), "MINUTE"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(316224000001), "SECOND"}, NullInterval(), OUT_OF_RANGE},
      {"",
       {Int64(316224000000001), "MILLISECOND"},
       NullInterval(),
       OUT_OF_RANGE},
      {"",
       {Int64(-316224000000001), "MILLISECOND"},
       NullInterval(),
       OUT_OF_RANGE},
      {"",
       {Int64(316224000000000001), "MICROSECOND"},
       NullInterval(),
       OUT_OF_RANGE},
      {"",
       {Int64(-316224000000000001), "MICROSECOND"},
       NullInterval(),
       OUT_OF_RANGE},

      // Overflow in multiplication
      {"",
       {Int64(9223372036854775807), "QUARTER"},
       NullInterval(),
       OUT_OF_RANGE},
      {"", {Int64(9223372036854775807), "WEEK"}, NullInterval(), OUT_OF_RANGE},

      // Invalid datetime part fields
      {"", {Int64(0), "DAYOFWEEK"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(0), "DAYOFYEAR"}, NullInterval(), OUT_OF_RANGE},
      {"", QueryParamsWithResult({Int64(0), "NANOSECOND"}, NullInterval(),
                                 OUT_OF_RANGE)
               .AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS)},
      {"", {Int64(0), "DATE"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(0), "DATETIME"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(0), "TIME"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(0), "ISOYEAR"}, NullInterval(), OUT_OF_RANGE},
      {"", {Int64(0), "ISOWEEK"}, NullInterval(), OUT_OF_RANGE},
  };
  return WrapFeatureIntervalType(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsIntervalComparisons() {
  // We only add tests for "=" and "<", because the test driver automatically
  // generates all comparison functions for every test case.
  std::vector<FunctionTestCall> tests = {
      {"=", {NullInterval(), Years(0)}, NullBool()},
      {"=", {Years(0), NullInterval()}, NullBool()},
      {"=", {NullInterval(), NullInterval()}, NullBool()},

      {"<", {NullInterval(), Years(0)}, NullBool()},
      {"<", {Years(0), NullInterval()}, NullBool()},
      {"<", {NullInterval(), NullInterval()}, NullBool()},

      {"=", {Years(0), Years(0)}, Bool(true)},
      {"=", {Months(0), Days(0)}, Bool(true)},
      {"=", {Months(0), Micros(0)}, Bool(true)},
      {"=", {Months(0), Nanos(0)}, Bool(true)},

      {"=", {Months(10), Months(10)}, Bool(true)},
      {"=", {Days(-3), Days(-3)}, Bool(true)},
      {"=", {Micros(12345), Micros(12345)}, Bool(true)},
      {"=", QueryParamsWithResult({Nanos(-9876), Nanos(-9876)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},

      {"<", QueryParamsWithResult({Nanos(999), Micros(1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", {Micros(999999), Seconds(1)}, Bool(true)},
      {"<", {Seconds(59), Minutes(1)}, Bool(true)},
      {"<", {Minutes(59), Hours(1)}, Bool(true)},
      {"<", {Hours(23), Days(1)}, Bool(true)},
      {"<", {Days(29), Months(1)}, Bool(true)},
      {"<", {Months(11), Years(1)}, Bool(true)},
      {"<", {Years(1), YMDHMS(1, 0, 0, 0, 0, 1)}, Bool(true)},

      {"<", {Months(-1), Days(0)}, Bool(true)},
      {"<", {Days(1), Months(1)}, Bool(true)},
      {"<", {Days(-1), Micros(1)}, Bool(true)},

      {"=", {Days(30), Months(1)}, Bool(true)},
      {"=", {Months(-2), Days(-60)}, Bool(true)},
      {"=", {Days(360), Months(12)}, Bool(true)},
      {"<", {Days(1), Months(1)}, Bool(true)},
      {"<", {Days(29), Months(1)}, Bool(true)},
      {"<", {Days(-31), Months(-1)}, Bool(true)},
      {"<", {Months(1), Days(31)}, Bool(true)},
      {"<", {Months(-1), Days(25)}, Bool(true)},
      {"<", {Months(12), Days(365)}, Bool(true)},

      {"=", {Micros(IntervalValue::kMicrosInDay), Days(1)}, Bool(true)},
      {"<", {Micros(IntervalValue::kMicrosInDay - 1), Days(1)}, Bool(true)},
      {"<", {Days(1), Micros(IntervalValue::kMicrosInDay + 1)}, Bool(true)},
      {"=", {Nanos(-IntervalValue::kNanosInDay), Days(-1)}, Bool(true)},
      {"<", QueryParamsWithResult(
                {Nanos(-IntervalValue::kNanosInDay - 1), Days(-1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"=", {Micros(IntervalValue::kMicrosInMonth), Months(1)}, Bool(true)},
      {"=", {Nanos(-IntervalValue::kNanosInMonth), Months(-1)}, Bool(true)},

      {"=", {Micros(-1), Nanos(-1000)}, Bool(true)},
      {"=", {Micros(7), Nanos(7000)}, Bool(true)},
      {"<", QueryParamsWithResult({Micros(1), Nanos(1001)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", QueryParamsWithResult({Micros(-1), Nanos(900)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", QueryParamsWithResult({Nanos(999), Micros(1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", QueryParamsWithResult({Nanos(-1001), Micros(-1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", QueryParamsWithResult({Nanos(1), Micros(1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", QueryParamsWithResult({Micros(-1), Nanos(1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},
      {"<", QueryParamsWithResult({Nanos(1), Micros(1)}, Bool(true))
                .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS)},

      {"=", {YMDHMS(1, 0, 1, 0, 0, 0), Days(361)}, Bool(true)},
      {"=", {YMDHMS(-1, 0, 360, 0, 2, 0), Minutes(2)}, Bool(true)},
      {"=", {YMDHMS(0, 1, 1, 0, 0, 0), Days(31)}, Bool(true)},
      {"=", {YMDHMS(0, 1, -1, 0, 0, 0), Days(29)}, Bool(true)},
      {"=", {YMDHMS(0, -1, 1, 0, 0, 0), Days(-29)}, Bool(true)},
      {"=", {YMDHMS(0, -1, -1, 0, 0, 0), Days(-31)}, Bool(true)},
      {"=", {YMDHMS(0, 2, -61, 0, 0, 0), Hours(-24)}, Bool(true)},
      {"=", {YMDHMS(0, -1, 30, 0, 0, 1), Seconds(1)}, Bool(true)},

      {"<", {YMDHMS(0, 1, 1, -1, 0, 0), Days(31)}, Bool(true)},
      {"<", {YMDHMS(0, 0, 0, 23, 59, 59), Days(1)}, Bool(true)},
      {"<", {YMDHMS(0, -1, 30, 0, 0, 1), Seconds(2)}, Bool(true)},
  };
  return WrapFeatureIntervalType(tests);
}

std::vector<QueryParamsWithResult> GetFunctionTestsIntervalUnaryMinus() {
  std::vector<QueryParamsWithResult> tests = {
      {{NullInterval()}, NullInterval()},
      {{Days(0)}, Years(0)},
      {{Nanos(IntervalValue::kMaxNanos)}, Nanos(-IntervalValue::kMaxNanos)},
      {{Micros(-987654)}, Micros(987654)},
      {{Seconds(1000)}, Seconds(-1000)},
      {{Minutes(-1)}, Minutes(1)},
      {{Hours(12345)}, Hours(-12345)},
      {{Days(-321)}, Days(321)},
      {{Months(2)}, Months(-2)},
      {{Years(-10000)}, Years(10000)},
      {{YMDHMS(0, 0, 0, 100, 200, 300)}, YMDHMS(0, 0, 0, -100, -200, -300)},
      {{YMDHMS(-123, 456, -789, 0, 0, 0)}, YMDHMS(123, -456, 789, 0, 0, 0)},
      {{YMDHMS(1, -2, 3, -4, 5, -6)}, YMDHMS(-1, 2, -3, 4, -5, 6)},
  };
  return WrapFeatureIntervalType(tests);
}

Value Date(absl::string_view str) {
  int32_t date;
  GOOGLESQL_QCHECK_OK(functions::ConvertStringToDate(str, &date));
  return Value::Date(date);
}

Value Timestamp(absl::string_view str) {
  absl::Time t;
  GOOGLESQL_QCHECK_OK(functions::ConvertStringToTimestamp(
      str, absl::UTCTimeZone(), functions::kNanoseconds,
      /* allow_tz_in_str = */ true, &t));
  return Value::Timestamp(t);
}

Value Datetime(absl::string_view str) {
  DatetimeValue datetime;
  GOOGLESQL_QCHECK_OK(functions::ConvertStringToDatetime(str, functions::kNanoseconds,
                                               &datetime));
  return Value::Datetime(datetime);
}

Value Time(absl::string_view str) {
  TimeValue time;
  GOOGLESQL_QCHECK_OK(
      functions::ConvertStringToTime(str, functions::kMicroseconds, &time));
  return Value::Time(time);
}

Value Interval(absl::string_view str) {
  IntervalValue interval =
      *IntervalValue::ParseFromString(str, /*allow_nanos=*/true);
  return Value::Interval(interval);
}

std::vector<QueryParamsWithResult> GetDateTimestampIntervalSubtractionsBase() {
  // The result is of type STRING, not INTERVAL, because we want to make sure
  // that correct date parts are populated. Intervals with different date parts
  // can still compare equal, so we will cast result to string and compare it.
  std::vector<QueryParamsWithResult> tests = {
      {{NullDate(), NullDate()}, NullString()},
      {{Date("1970-01-01"), NullDate()}, NullString()},
      {{NullDate(), Date("1970-01-01")}, NullString()},
      {{Date("1990-01-23"), Date("1990-01-23")}, "0-0 0 0:0:0"},
      {{Date("2000-01-01"), Date("1900-01-01")}, "0-0 36524 0:0:0"},
      {{Date("1910-01-01"), Date("1900-01-01")}, "0-0 3652 0:0:0"},
      {{Date("1901-01-01"), Date("1900-01-01")}, "0-0 365 0:0:0"},
      {{Date("1900-12-31"), Date("1900-01-01")}, "0-0 364 0:0:0"},
      {{Date("1900-12-01"), Date("1900-01-01")}, "0-0 334 0:0:0"},
      {{Date("1900-02-01"), Date("1900-01-01")}, "0-0 31 0:0:0"},
      {{Date("1900-01-31"), Date("1900-01-01")}, "0-0 30 0:0:0"},
      {{Date("1900-01-02"), Date("1900-01-01")}, "0-0 1 0:0:0"},
      {{Date("1900-01-01"), Date("1900-01-01")}, "0-0 0 0:0:0"},
      {{Date("0001-01-01"), Date("9999-12-31")}, "0-0 -3652058 0:0:0"},

      {{NullTimestamp(), NullTimestamp()}, NullString()},
      {{Timestamp("1970-01-01 10:20:30"), NullTimestamp()}, NullString()},
      {{NullTimestamp(), Timestamp("1970-01-01 00:00:01")}, NullString()},
      {{Timestamp("1955-12-25 11:22:33"), Timestamp("1955-12-25 11:22:33")},
       "0-0 0 0:0:0"},
      {{Timestamp("2000-01-01 00:00:00"), Timestamp("1900-01-01 00:00:00")},
       "0-0 0 876576:0:0"},
      {{Timestamp("1910-01-01 00:00:00"), Timestamp("1900-01-01 00:00:00")},
       "0-0 0 87648:0:0"},
      {{Timestamp("1901-01-01 00:00:00"), Timestamp("1900-01-01 00:00:00")},
       "0-0 0 8760:0:0"},
      {{Timestamp("1900-12-31 00:00:00"), Timestamp("1900-01-01 00:00:00")},
       "0-0 0 8736:0:0"},
      {{Timestamp("1900-02-01 00:00:00"), Timestamp("1900-01-01 00:00:00")},
       "0-0 0 744:0:0"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 00:00:00")},
       "0-0 0 24:0:0"},
      {{Timestamp("1900-01-02 00:00:00"),
        Timestamp("1900-01-01 00:00:00.000001")},
       "0-0 0 23:59:59.999999"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 00:00:00.001")},
       "0-0 0 23:59:59.999"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 00:00:01")},
       "0-0 0 23:59:59"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 00:01:00")},
       "0-0 0 23:59:0"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 01:00:00")},
       "0-0 0 23:0:0"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 23:59:59")},
       "0-0 0 0:0:1"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-01 23:59:59.999")},
       "0-0 0 0:0:0.001"},
      {{Timestamp("1900-01-02 00:00:00"),
        Timestamp("1900-01-01 23:59:59.999999")},
       "0-0 0 0:0:0.000001"},
      {{Timestamp("1900-01-02 00:00:00"), Timestamp("1900-01-02 00:00:00")},
       "0-0 0 0:0:0"},
      {{Timestamp("0001-01-01 00:00:00"),
        Timestamp("9999-12-31 23:59:59.999999")},
       "0-0 0 -87649415:59:59.999999"},
  };
  return tests;
}

std::vector<QueryParamsWithResult> GetDateTimestampIntervalSubtractions() {
  return WrapFeatureIntervalType(GetDateTimestampIntervalSubtractionsBase());
}

std::vector<QueryParamsWithResult> GetDatetimeTimeIntervalSubtractionsBase() {
  // The result is of type STRING, not INTERVAL, because we want to make sure
  // that correct date parts are populated. Intervals with different date parts
  // can still compare equal, so we will cast result to string and compare it.
  std::vector<QueryParamsWithResult> tests = {
      {{NullDatetime(), NullDatetime()}, NullString()},
      {{Datetime("1970-01-01 01:02:03"), NullDatetime()}, NullString()},
      {{NullDatetime(), Datetime("1970-01-01 22:22:22")}, NullString()},
      {{Datetime("2015-05-06 01:02:03"), Datetime("2015-05-06 01:02:03")},
       "0-0 0 0:0:0"},
      {{Datetime("2000-01-01 00:00:00"), Datetime("1900-01-01 00:00:00")},
       "0-0 36524 0:0:0"},
      {{Datetime("1910-01-01 00:00:00"), Datetime("1900-01-01 00:00:00")},
       "0-0 3652 0:0:0"},
      {{Datetime("1901-01-01 00:00:00"), Datetime("1900-01-01 00:00:00")},
       "0-0 365 0:0:0"},
      {{Datetime("1900-12-31 00:00:00"), Datetime("1900-01-01 00:00:00")},
       "0-0 364 0:0:0"},
      {{Datetime("1900-02-01 00:00:00"), Datetime("1900-01-01 00:00:00")},
       "0-0 31 0:0:0"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 00:00:00")},
       "0-0 1 0:0:0"},
      {{Datetime("1900-01-02 00:00:00"),
        Datetime("1900-01-01 00:00:00.000001")},
       "0-0 0 23:59:59.999999"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 00:00:00.001")},
       "0-0 0 23:59:59.999"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 00:00:01")},
       "0-0 0 23:59:59"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 00:01:00")},
       "0-0 0 23:59:0"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 01:00:00")},
       "0-0 0 23:0:0"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 23:59:59")},
       "0-0 0 0:0:1"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-01 23:59:59.999")},
       "0-0 0 0:0:0.001"},
      {{Datetime("1900-01-02 00:00:00"),
        Datetime("1900-01-01 23:59:59.999999")},
       "0-0 0 0:0:0.000001"},
      {{Datetime("1900-01-02 00:00:00"), Datetime("1900-01-02 00:00:00")},
       "0-0 0 0:0:0"},
      {{Datetime("0001-01-01 00:00:00"),
        Datetime("9999-12-31 23:59:59.999999")},
       "0-0 -3652058 -23:59:59.999999"},

      {{NullTime(), NullTime()}, NullString()},
      {{Time("01:02:03"), NullTime()}, NullString()},
      {{NullTime(), Time("20:30:40")}, NullString()},
      {{Time("12:34:56.789"), Time("12:34:56.789")}, "0-0 0 0:0:0"},
      {{Time("00:00:00"), Time("23:59:59.999999")}, "0-0 0 -23:59:59.999999"},
      {{Time("00:00:00"), Time("23:59:59.999")}, "0-0 0 -23:59:59.999"},
      {{Time("00:00:00"), Time("23:59:59.1")}, "0-0 0 -23:59:59.100"},
      {{Time("00:00:00"), Time("23:59:59")}, "0-0 0 -23:59:59"},
      {{Time("00:00:00"), Time("23:59:00")}, "0-0 0 -23:59:0"},
      {{Time("00:00:00"), Time("23:00:00")}, "0-0 0 -23:0:0"},
      {{Time("00:00:00"), Time("01:00:00")}, "0-0 0 -1:0:0"},
      {{Time("00:00:00"), Time("00:01:00")}, "0-0 0 -0:1:0"},
      {{Time("00:00:00"), Time("00:00:01")}, "0-0 0 -0:0:1"},
      {{Time("00:00:00"), Time("00:00:00.1")}, "0-0 0 -0:0:0.100"},
      {{Time("00:00:00"), Time("00:00:00.001")}, "0-0 0 -0:0:0.001"},
      {{Time("00:00:00"), Time("00:00:00.000001")}, "0-0 0 -0:0:0.000001"},
      {{Time("10:20:30.456789"), Time("01:02:03.987654")},
       "0-0 0 9:18:26.469135"},
  };
  return tests;
}

std::vector<QueryParamsWithResult> GetDatetimeTimeIntervalSubtractions() {
  return WrapFeaturesIntervalAndCivilTimeTypes(
      GetDatetimeTimeIntervalSubtractionsBase());
}

// String representation of function arguments and result - so same test case
// can be reused for different data types, like DATETIME and TIMESTAMP
struct BinaryFunctionArgumentsAndResult {
  std::string input1;
  std::string input2;
  std::string output;
};

// INTERVAL DAY to SECOND tests are shared between DATETIME and TIMESTAMP
const struct BinaryFunctionArgumentsAndResult
    kAddDatetimeIntervalDayToSecondTests[] = {
        {"2000-01-01 00:00:00", "0:0:0.000000001",
         "2000-01-01 00:00:00.000000001"},
        {"2000-01-01 00:00:00", "-0:0:0.000000001",
         "1999-12-31 23:59:59.999999999"},
        {"2000-01-01 00:00:00", "0:0:0.000001", "2000-01-01 00:00:00.000001"},
        {"2000-01-01 00:00:00", "-0:0:0.000001", "1999-12-31 23:59:59.999999"},
        {"2000-01-01 00:00:00", "0:0:0.001", "2000-01-01 00:00:00.001"},
        {"2000-01-01 00:00:00", "-0:0:0.001", "1999-12-31 23:59:59.999"},
        {"2000-01-01 00:00:00", "0:0:0.1", "2000-01-01 00:00:00.100"},
        {"2000-01-01 00:00:00", "-0:0:0.1", "1999-12-31 23:59:59.900"},
        {"2000-01-01 00:00:00", "0:0:1", "2000-01-01 00:00:01"},
        {"2000-01-01 00:00:00", "-0:0:1", "1999-12-31 23:59:59"},
        {"2000-01-01 00:00:00", "0:1:0", "2000-01-01 00:01:00"},
        {"2000-01-01 00:00:00", "-0:1:0", "1999-12-31 23:59:00"},
        {"2000-01-01 00:00:00", "0:1:2", "2000-01-01 00:01:02"},
        {"2000-01-01 00:00:00", "-0:1:2", "1999-12-31 23:58:58"},
        {"2000-01-01 00:00:00", "1:0:0", "2000-01-01 01:00:00"},
        {"2000-01-01 00:00:00", "-1:0:0", "1999-12-31 23:00:00"},
        {"2000-01-01 00:00:00", "25:0:0", "2000-01-02 01:00:00"},
        {"2000-01-01 00:00:00", "-25:0:0", "1999-12-30 23:00:00"},
        {"2000-01-01 00:00:00", "1:2:3.456", "2000-01-01 01:02:03.456"},
        {"2000-01-01 00:00:00", "-1:2:3.456", "1999-12-31 22:57:56.544"},
        {"2000-01-01 00:00:00", "1 0:0:0", "2000-01-02 00:00:00"},
        {"2000-01-01 00:00:00", "-1 0:0:0", "1999-12-31 00:00:00"},
        {"2000-02-28 00:00:00", "1 0:0:0", "2000-02-29 00:00:00"},
        {"2000-03-01 00:00:00", "-1 0:0:0", "2000-02-29 00:00:00"},
        {"2001-02-28 00:00:00", "1 0:0:0", "2001-03-01 00:00:00"},
        {"2001-03-01 00:00:00", "-1 0:0:0", "2001-02-28 00:00:00"},
        {"2000-01-01 00:00:00", "31 0:0:0", "2000-02-01 00:00:00"},
        {"2000-01-01 00:00:00", "-31 0:0:0", "1999-12-01 00:00:00"},
        {"2000-02-01 00:00:00", "29 0:0:0", "2000-03-01 00:00:00"},
        {"2000-03-31 00:00:00", "-31 0:0:0", "2000-02-29 00:00:00"},
        {"2000-02-01 00:00:00", "30 0:0:0", "2000-03-02 00:00:00"},
        {"2000-03-31 00:00:00", "-32 0:0:0", "2000-02-28 00:00:00"},
        {"2001-02-01 00:00:00", "28 0:0:0", "2001-03-01 00:00:00"},
        {"2001-03-31 00:00:00", "-31 0:0:0", "2001-02-28 00:00:00"},
        {"2000-01-01 00:00:00", "365 0:0:0", "2000-12-31 00:00:00"},
        {"2000-01-01 00:00:00", "-365 0:0:0", "1999-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "366 0:0:0", "2001-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-366 0:0:0", "1998-12-31 00:00:00"},
        {"2000-01-01 00:00:00", "36525 0:0:0", "2100-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-36524 0:0:0", "1900-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "1 2:3:4.123456", "2000-01-02 02:03:04.123456"},
        {"2000-01-01 00:00:00", "-1 -2:3:4.123456",
         "1999-12-30 21:56:55.876544"},
        {"2000-01-01 00:00:00", "1 2:3:4.123456789",
         "2000-01-02 02:03:04.123456789"},
        {"2000-01-01 00:00:00", "-1 -2:3:4.123456789",
         "1999-12-30 21:56:55.876543211"},
        {"2000-01-01 00:00:00", "1 -1:0:0", "2000-01-01 23:00:00"},
        {"2000-01-01 00:00:00", "-1 1:0:0", "1999-12-31 01:00:00"},
        {"2000-01-01 00:00:00", "1 -24:0:0", "2000-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-1 24:0:0", "2000-01-01 00:00:00"},
        {"1900-10-11 12:13:14.1500006", "1 1:1:1.0100001",
         "1900-10-12 13:14:15.160000700"},
        {"1900-10-11 12:13:14.1500006", "-1 -1:1:1.0100001",
         "1900-10-10 11:12:13.140000500"},
        {"0001-01-01 0:0:0", "3652058 0:0:0", "9999-12-31 00:00:00"},
        {"9999-12-31 23:59:59", "-3652058 0:0:0", "0001-01-01 23:59:59"},
        {"0001-01-01 0:0:0", "87649415:0:0", "9999-12-31 23:00:00"},
        {"9999-12-31 23:59:59", "-87649415:0:0", "0001-01-01 00:59:59"},
        {"0001-01-01 0:0:0", "87649415:59:59", "9999-12-31 23:59:59"},
        {"9999-12-31 23:59:59", "-87649415:59:59", "0001-01-01 00:00:00"},
        {"0001-01-01 0:0:0", "87649415:59:59.999999",
         "9999-12-31 23:59:59.999999"},
        {"9999-12-31 23:59:59.999999", "-87649415:59:59.999999",
         "0001-01-01 00:00:00"},
        {"0001-01-01 0:0:0", "87649415:59:59.999999999",
         "9999-12-31 23:59:59.999999999"},
        {"9999-12-31 23:59:59.999999999", "-87649415:59:59.999999999",
         "0001-01-01 00:00:00"},
};

// Remaining INTERVAL YEAR TO SECOND are for DATETIME only
const struct BinaryFunctionArgumentsAndResult
    kAddDatetimeIntervalYearToSecondTests[] = {
        {"2000-01-01 00:00:00", "1 0 0:0:0", "2000-02-01 00:00:00"},
        {"2000-01-01 00:00:00", "-1 0 0:0:0", "1999-12-01 00:00:00"},
        {"2000-02-28 00:00:00", "1 0 0:0:0", "2000-03-28 00:00:00"},
        {"2000-02-28 00:00:00", "-1 0 0:0:0", "2000-01-28 00:00:00"},
        {"2000-02-29 00:00:00", "1 0 0:0:0", "2000-03-29 00:00:00"},
        {"2000-02-29 00:00:00", "-1 0 0:0:0", "2000-01-29 00:00:00"},
        {"2000-01-31 00:00:00", "1 0 0:0:0", "2000-02-29 00:00:00"},
        {"2000-03-31 00:00:00", "-1 0 0:0:0", "2000-02-29 00:00:00"},
        {"2000-02-28 00:00:00", "1-0 0 0:0:0", "2001-02-28 00:00:00"},
        {"2001-03-01 00:00:00", "-1-0 0 0:0:0", "2000-03-01 00:00:00"},
        {"2000-02-29 00:00:00", "1-0 0 0:0:0", "2001-02-28 00:00:00"},
        {"2000-02-29 00:00:00", "-1-0 0 0:0:0", "1999-02-28 00:00:00"},
        {"1999-02-28 00:00:00", "1-0 0 0:0:0", "2000-02-28 00:00:00"},
        {"2001-02-28 00:00:00", "-1-0 0 0:0:0", "2000-02-28 00:00:00"},
        {"2000-01-01 00:00:00", "1 0 0:0:0", "2000-02-01 00:00:00"},
        {"2000-01-01 00:00:00", "-1 0 0:0:0", "1999-12-01 00:00:00"},
        {"2000-01-01 00:00:00", "12 0 0:0:0", "2001-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-12 0 0:0:0", "1999-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "100 0 0:0:0", "2008-05-01 00:00:00"},
        {"2000-01-01 00:00:00", "-100 0 0:0:0", "1991-09-01 00:00:00"},
        {"2000-01-01 00:00:00", "1 2 3:4:5", "2000-02-03 03:04:05"},
        {"2000-01-01 00:00:00", "-1 -2 -3:4:5", "1999-11-28 20:55:55"},
        {"2000-01-01 00:00:00", "1 -2 3:4:5", "2000-01-30 03:04:05"},
        {"2000-01-01 00:00:00", "-1 2 -3:4:5", "1999-12-02 20:55:55"},
        {"2000-01-01 00:00:00", "1-0 0 0:0:0", "2001-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-1-0 0 0:0:0", "1999-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "101-0 0 0:0:0", "2101-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-101-0 0 0:0:0", "1899-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "1-2 3 4:5:6", "2001-03-04 04:05:06"},
        {"2000-01-01 00:00:00", "-1-2 -3 -4:5:6", "1998-10-28 19:54:54"},
        {"2000-01-01 00:00:00", "1-2 -3 -4:5:6", "2001-02-25 19:54:54"},
        {"2000-01-01 00:00:00", "-1-2 3 4:5:6", "1998-11-04 04:05:06"},
        {"2000-01-01 00:00:00", "0-1 -31 0:0:0", "2000-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-0-1 31 0:0:0", "2000-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "1-0 -366 0:0:0", "2000-01-01 00:00:00"},
        {"2000-01-01 00:00:00", "-1-0 365 0:0:0", "2000-01-01 00:00:00"},
        {"1900-10-11 12:13:14.1500006", "1-1 1 1:1:1.0100001",
         "1901-11-12 13:14:15.160000700"},
        {"1900-10-11 12:13:14.1500006", "-1-1 -1 -1:1:1.0100001",
         "1899-09-10 11:12:13.140000500"},
};

std::vector<QueryParamsWithResult> GetDatetimeAddSubIntervalBase() {
  std::vector<QueryParamsWithResult> tests = {
      {{NullDatetime(), NullInterval()}, NullDatetime()},
      {{NullDatetime(), Years(100)}, NullDatetime()},
      {{Datetime("1000-01-01 00:00:00"), NullInterval()}, NullDatetime()},
      {{Datetime("9999-12-01 00:00:00"), Interval("1 0 0:0:0")},
       NullDatetime(),
       OUT_OF_RANGE},
      {{Datetime("0001-01-31 00:00:00"), Interval("-1 0 0:0:0")},
       NullDatetime(),
       OUT_OF_RANGE},
      {{Datetime("9999-01-01 00:00:00"), Interval("1-0 0 0:0:0")},
       NullDatetime(),
       OUT_OF_RANGE},
      {{Datetime("0001-12-31 00:00:00"), Interval("-1-0 0 0:0:0")},
       NullDatetime(),
       OUT_OF_RANGE},
  };

  for (const auto& p : kAddDatetimeIntervalDayToSecondTests) {
    IntervalValue interval =
        *IntervalValue::ParseFromString(p.input2, /*allow_nanos=*/true);
    // Original test case
    tests.push_back(
        {{Datetime(p.input1), Value::Interval(interval)}, Datetime(p.output)});
    // Test case derived through transformation: x+y=z <=> z-y=x
    tests.push_back(
        {{Datetime(p.output), Value::Interval(-interval)}, Datetime(p.input1)});
  }

  for (const auto& p : kAddDatetimeIntervalYearToSecondTests) {
    IntervalValue interval =
        *IntervalValue::ParseFromString(p.input2, /*allow_nanos=*/true);
    tests.push_back(
        {{Datetime(p.input1), Value::Interval(interval)}, Datetime(p.output)});
    // Note: for Year/Month date parts, x+y=z <=> z-y=x is no longer true, so
    // no additional test cases.
  }

  return tests;
}

std::vector<QueryParamsWithResult> GetDatetimeAddSubInterval() {
  std::vector<QueryParamsWithResult> tests = GetDatetimeAddSubIntervalBase();

  // Use test cases for DATETIME-DATETIME=INTERVAL, applying transformation
  // x-y=z => z+y=x
  for (const auto& test : GetDatetimeTimeIntervalSubtractionsBase()) {
    ABSL_QCHECK_EQ(2, test.num_params());
    if (test.result().is_null() || !test.param(0).type()->IsDatetime()) {
      continue;
    }
    IntervalValue interval = *IntervalValue::ParseFromString(
        test.result().string_value(), /*allow_nanos=*/true);
    tests.push_back(
        {{test.param(1), Value::Interval(interval)}, test.param(0)});
  }

  return WrapFeaturesIntervalAndCivilTimeTypes(tests);
}

std::vector<QueryParamsWithResult> GetTimestampAddSubIntervalBase() {
  std::vector<QueryParamsWithResult> tests = {
      {{NullTimestamp(), NullInterval()}, NullTimestamp()},
      {{NullTimestamp(), Seconds(-1)}, NullTimestamp()},
      {{Timestamp("1000-01-01 00:00:00"), NullInterval()}, NullTimestamp()},
      {{Timestamp("2000-01-01 00:00:00"), Interval("0-1 0 0:0:0")},
       NullTimestamp(),
       OUT_OF_RANGE},
      {{Timestamp("2000-01-01 00:00:00"), Interval("-0-1 0 0:0:0")},
       NullTimestamp(),
       OUT_OF_RANGE},
      {{Timestamp("2000-01-01 00:00:00"), Interval("1-0 0 0:0:0")},
       NullTimestamp(),
       OUT_OF_RANGE},
      {{Timestamp("2000-01-01 00:00:00"), Interval("-1-0 0 0:0:0")},
       NullTimestamp(),
       OUT_OF_RANGE},
  };

  // Reuse test cases for Datetime +/- Interval for DAY TO SECOND granularity
  for (const auto& p : kAddDatetimeIntervalDayToSecondTests) {
    IntervalValue interval =
        *IntervalValue::ParseFromString(p.input2, /*allow_nanos=*/true);
    // Original test case
    tests.push_back({{Timestamp(p.input1), Value::Interval(interval)},
                     Timestamp(p.output)});
    // Test case derived through transformation: x+y=z <=> z-y=x
    tests.push_back({{Timestamp(p.output), Value::Interval(-interval)},
                     Timestamp(p.input1)});
  }

  return tests;
}

std::vector<QueryParamsWithResult> GetTimestampAddSubInterval() {
  std::vector<QueryParamsWithResult> tests = GetTimestampAddSubIntervalBase();

  // Use test cases for TIMESTAMP-TIMESTAMP=INTERVAL, applying transformation
  // x-y=z => z+y=x
  for (const auto& test : GetDateTimestampIntervalSubtractionsBase()) {
    ABSL_QCHECK_EQ(2, test.num_params());
    if (test.result().is_null() || !test.param(0).type()->IsTimestamp()) {
      continue;
    }
    IntervalValue interval = *IntervalValue::ParseFromString(
        test.result().string_value(), /*allow_nanos=*/true);
    tests.push_back(
        {{test.param(1), Value::Interval(interval)}, test.param(0)});
  }

  return WrapFeatureIntervalType(tests);
}

std::vector<QueryParamsWithResult> GetFunctionTestsIntervalAddBase() {
  std::vector<QueryParamsWithResult> tests = {
      {{NullInterval(), NullInterval()}, NullInterval()},
      {{Years(1), NullInterval()}, NullInterval()},
      {{NullInterval(), Months(2)}, NullInterval()},
      {{Years(1), Years(2)}, Years(3)},
      {{Months(1), Months(2)}, Months(3)},
      {{Days(1), Days(2)}, Days(3)},
      {{Hours(1), Hours(2)}, Hours(3)},
      {{Minutes(1), Minutes(2)}, Minutes(3)},
      {{Seconds(1), Seconds(2)}, Seconds(3)},
      {{Micros(1), Micros(2)}, Micros(3)},
      {{Nanos(1), Nanos(2)}, Nanos(3)},
      {{Years(1), Months(2)}, Interval("1-2 0 0:0:0")},
      {{Years(1), Days(2)}, Interval("1-0 2 0:0:0")},
      {{Years(1), Hours(2)}, Interval("1-0 0 2:0:0")},
      {{Years(1), Minutes(2)}, Interval("1-0 0 0:2:0")},
      {{Years(1), Seconds(2)}, Interval("1-0 0 0:0:2")},
      {{Years(1), Micros(2)}, Interval("1-0 0 0:0:0.000002")},
      {{Years(1), Nanos(2)}, Interval("1-0 0 0:0:0.000000002")},
      {{Years(10000), Days(3660000)}, Interval("10000-0 3660000 0:0:0")},
      {{Years(10000), Hours(87840000)}, Interval("10000-0 0 87840000:0:0")},
      {{Days(3660000), Hours(87840000)}, Interval("0-0 3660000 87840000:0:0")},
      {{YMDHMS(1, 2, 3, 4, 5, 6), YMDHMS(1, 1, 1, 1, 1, 1)},
       YMDHMS(2, 3, 4, 5, 6, 7)},
      {{YMDHMS(1, -2, 3, -4, 5, -6), YMDHMS(-1, 2, -3, 4, -5, 6)}, Days(0)},
      {{Years(-10000), Months(-1)}, NullInterval(), OUT_OF_RANGE},
      {{Days(3660000), Days(1)}, NullInterval(), OUT_OF_RANGE},
      {{Hours(87840000), Nanos(1)}, NullInterval(), OUT_OF_RANGE},
  };
  return tests;
}

std::vector<QueryParamsWithResult> GetFunctionTestsIntervalAdd() {
  return WrapFeatureIntervalType(GetFunctionTestsIntervalAddBase());
}

std::vector<QueryParamsWithResult> GetFunctionTestsIntervalSub() {
  std::vector<QueryParamsWithResult> tests;
  for (const auto& test : GetFunctionTestsIntervalAddBase()) {
    if (test.result().is_null()) {
      continue;
    }
    // x+y=z <=> z-x=y <=> z-y=x
    ABSL_QCHECK_EQ(2, test.num_params());
    tests.emplace_back(
        QueryParamsWithResult({{test.result(), test.param(1)}, test.param(0)}));
    tests.emplace_back(
        QueryParamsWithResult({{test.result(), test.param(0)}, test.param(1)}));
  }
  return WrapFeatureIntervalType(tests);
}

std::vector<QueryParamsWithResult> GetFunctionTestsIntervalMultiply() {
  // Only INTERVAL * INT64 signatures, the harness will automatically generate
  // tests for INT64 * INTERVAL.
  std::vector<QueryParamsWithResult> tests = {
      {{NullInterval(), NullInt64()}, NullInterval()},
      {{NullInterval(), 1}, NullInterval()},
      {{Years(1), NullInt64()}, NullInterval()},

      {{Years(2), 10000}, NullInterval(), OUT_OF_RANGE},
      {{Days(3660000), -2}, NullInterval(), OUT_OF_RANGE},
      {{Days(999999), 99999999999}, NullInterval(), OUT_OF_RANGE},
  };

  for (int64_t v : {0, 1, -1, 3, -3, 11, -11, 1007, -1007, 9999, -9999}) {
    tests.emplace_back(QueryParamsWithResult{{Years(1), v}, Years(v)});
    tests.emplace_back(QueryParamsWithResult{{Months(1), v}, Months(v)});
    tests.emplace_back(QueryParamsWithResult{{Days(1), v}, Days(v)});
    tests.emplace_back(QueryParamsWithResult{{Hours(1), v}, Hours(v)});
    tests.emplace_back(QueryParamsWithResult{{Minutes(1), v}, Minutes(v)});
    tests.emplace_back(QueryParamsWithResult{{Seconds(1), v}, Seconds(v)});
    tests.emplace_back(QueryParamsWithResult{{Micros(1), v}, Micros(v)});
    tests.emplace_back(QueryParamsWithResult{{Nanos(1), v}, Nanos(v)});
    tests.emplace_back(
        QueryParamsWithResult{{YMDHMS(0, 1, 2, 3, 4, 5), v},
                              YMDHMS(0, v, 2 * v, 3 * v, 4 * v, 5 * v)});
    tests.emplace_back(
        QueryParamsWithResult{{YMDHMS(0, -1, -2, -3, -4, -5), v},
                              YMDHMS(0, -v, -2 * v, -3 * v, -4 * v, -5 * v)});
  }

  return WrapFeatureIntervalType(tests);
}

std::vector<QueryParamsWithResult> GetFunctionTestsIntervalDivide() {
  std::vector<QueryParamsWithResult> tests = {
      {{NullInterval(), NullInt64()}, NullInterval()},
      {{NullInterval(), 1}, NullInterval()},
      {{Years(1), NullInt64()}, NullInterval()},

      {{Years(1), 2}, Months(6)},
      {{Years(1), -2}, Months(-6)},
      {{Months(1), 2}, Days(15)},
      {{Months(1), -2}, Days(-15)},
      {{Days(1), 2}, Hours(12)},
      {{Days(1), -2}, Hours(-12)},
      {{Hours(1), 2}, Minutes(30)},
      {{Hours(1), -2}, Minutes(-30)},
      {{Minutes(1), 2}, Seconds(30)},
      {{Minutes(1), -2}, Seconds(-30)},
      {{Seconds(1), 2}, Micros(500000)},
      {{Seconds(1), -2}, Micros(-500000)},
      QueryParamsWithResult({Micros(1), 2}, Nanos(500))
          .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS),
      QueryParamsWithResult({Micros(1), -2}, Nanos(-500))
          .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS),
      QueryParamsWithResult({Micros(1), 2}, Nanos(0))
          .AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS),
      QueryParamsWithResult({Micros(1), -2}, Nanos(0))
          .AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS),
      {{Nanos(1), 2}, Nanos(0)},
      {{Nanos(1), -2}, Nanos(0)},
      {{YMDHMS(2, 2, 2, 2, 2, 2), 2}, YMDHMS(1, 1, 1, 1, 1, 1)},
      {{YMDHMS(2, 2, 2, 2, 2, 2), -2}, YMDHMS(-1, -1, -1, -1, -1, -1)},
      {{FromString("1-2 3 4:5:6.789"), 2}, FromString("0-7 1 14:2:33.3945")},
      {{FromString("1-2 3 4:5:6.789"), 3}, FromString("0-4 21 1:21:42.263")},
      {{FromString("1-2 3 4:5:6.789"), 10}, FromString("0-1 12 7:36:30.6789")},
      {{FromString("1-2 3 4:5:6.789"), 100}, FromString("0-0 4 5:33:39.06789")},

      {{NullInterval(), 0}, NullInterval()},
      {{Years(1), 0}, NullInterval(), OUT_OF_RANGE},
      {{Nanos(-1), 0}, NullInterval(), OUT_OF_RANGE},
  };

  return WrapFeatureIntervalType(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsFormatInterval() {
  std::vector<FunctionTestCall> tests = {
      {"format", {NullString(), Seconds(5)}, NullString()},
      {"format", {NullString(), NullInterval()}, NullString()},

      {"format", {String("%t"), NullInterval()}, "NULL"},
      {"format", {"%t", Years(0)}, "0-0 0 0:0:0"},
      {"format", {"%t", Years(10)}, "10-0 0 0:0:0"},
      {"format", {"%t", Months(-20)}, "-1-8 0 0:0:0"},
      {"format", {"%t", Days(30)}, "0-0 30 0:0:0"},
      {"format", {"%t", Hours(-40)}, "0-0 0 -40:0:0"},
      {"format", {"%t", Minutes(50)}, "0-0 0 0:50:0"},
      {"format", {"%t", Seconds(-59)}, "0-0 0 -0:0:59"},
      {"format", {"%t", Micros(123456)}, "0-0 0 0:0:0.123456"},
      {"format", {"%t", Micros(-123400)}, "0-0 0 -0:0:0.123400"},
      {"format", {"%t", Micros(123000)}, "0-0 0 0:0:0.123"},
      {"format", {"%t", Micros(-120000)}, "0-0 0 -0:0:0.120"},
      {"format", {"%t", Micros(123)}, "0-0 0 0:0:0.000123"},
      {"format", {"%t", Micros(1)}, "0-0 0 0:0:0.000001"},
      {"format", {"%t", Nanos(-1)}, "0-0 0 -0:0:0.000000001"},
      {"format", {"%t", YMDHMS(1, 2, 3, 4, 5, 6)}, "1-2 3 4:5:6"},
      {"format", {"%t", YMDHMS(-1, -2, -3, -4, -5, -6)}, "-1-2 -3 -4:5:6"},

      {"format", {String("%T"), NullInterval()}, "NULL"},
      {"format", {"%T", Years(10)}, "INTERVAL \"10-0 0 0:0:0\" YEAR TO SECOND"},
      {"format",
       {"%T", YMDHMS(1, 2, 3, 4, 5, 6)},
       "INTERVAL \"1-2 3 4:5:6\" YEAR TO SECOND"},
  };

  return WrapFeatureIntervalType(tests);
}

std::vector<QueryParamsWithResult> GetFunctionTestsExtractInterval() {
  std::vector<QueryParamsWithResult> tests = {
      {{NullInterval(), "YEAR"}, NullInt64()},
      {{Years(0), "YEAR"}, Int64(0)},
      {{Days(10000), "YEAR"}, Int64(0)},
      {{Hours(-1000000), "YEAR"}, Int64(0)},
      {{Micros(1), "YEAR"}, Int64(0)},
      {{Nanos(-1), "YEAR"}, Int64(0)},
      {{Years(7), "YEAR"}, Int64(7)},
      {{Years(-7), "YEAR"}, Int64(-7)},
      {{Years(10000), "YEAR"}, Int64(10000)},
      {{Years(-10000), "YEAR"}, Int64(-10000)},
      {{Months(11), "YEAR"}, Int64(0)},
      {{Months(12), "YEAR"}, Int64(1)},
      {{Months(13), "YEAR"}, Int64(1)},
      {{Months(-11), "YEAR"}, Int64(0)},
      {{Months(-12), "YEAR"}, Int64(-1)},
      {{Months(-13), "YEAR"}, Int64(-1)},
      {{Months(11), "MONTH"}, Int64(11)},
      {{Months(12), "MONTH"}, Int64(0)},
      {{Months(13), "MONTH"}, Int64(1)},
      {{Months(-11), "MONTH"}, Int64(-11)},
      {{Months(-12), "MONTH"}, Int64(0)},
      {{Months(-13), "MONTH"}, Int64(-1)},
      {{Days(100), "DAY"}, Int64(100)},
      {{Days(-100), "DAY"}, Int64(-100)},
      {{Hours(200), "HOUR"}, Int64(200)},
      {{Hours(-200), "HOUR"}, Int64(-200)},
      {{Minutes(59), "MINUTE"}, Int64(59)},
      {{Minutes(60), "MINUTE"}, Int64(0)},
      {{Minutes(61), "MINUTE"}, Int64(1)},
      {{Minutes(-59), "MINUTE"}, Int64(-59)},
      {{Minutes(-60), "MINUTE"}, Int64(0)},
      {{Minutes(-61), "MINUTE"}, Int64(-1)},
      {{Seconds(59), "SECOND"}, Int64(59)},
      {{Seconds(60), "SECOND"}, Int64(0)},
      {{Seconds(61), "SECOND"}, Int64(1)},
      {{Seconds(-59), "SECOND"}, Int64(-59)},
      {{Seconds(-60), "SECOND"}, Int64(0)},
      {{Seconds(-61), "SECOND"}, Int64(-1)},
      {{Micros(999), "MILLISECOND"}, Int64(0)},
      {{Micros(1001), "MILLISECOND"}, Int64(1)},
      {{Micros(1002003), "MILLISECOND"}, Int64(2)},
      {{Micros(-999), "MILLISECOND"}, Int64(0)},
      {{Micros(-1001), "MILLISECOND"}, Int64(-1)},
      {{Micros(-1002003), "MILLISECOND"}, Int64(-2)},
      {{Nanos(999), "MICROSECOND"}, Int64(0)},
      {{Nanos(1001), "MICROSECOND"}, Int64(1)},
      {{Nanos(-999), "MICROSECOND"}, Int64(0)},
      {{Nanos(-1001), "MICROSECOND"}, Int64(-1)},
      {{Micros(999999), "MICROSECOND"}, Int64(999999)},
      {{Micros(-999999), "MICROSECOND"}, Int64(-999999)},
      {{Nanos(999), "NANOSECOND"}, Int64(999)},
      {{Nanos(-999), "NANOSECOND"}, Int64(-999)},
      {{Nanos(999999999), "NANOSECOND"}, Int64(999999999)},
      {{Nanos(-999999999), "NANOSECOND"}, Int64(-999999999)},
      {{FromString("1-2 3 4:5:6.123456789"), "YEAR"}, Int64(1)},
      {{FromString("1-2 3 4:5:6.123456789"), "MONTH"}, Int64(2)},
      {{FromString("1-2 3 4:5:6.123456789"), "DAY"}, Int64(3)},
      {{FromString("1-2 3 4:5:6.123456789"), "HOUR"}, Int64(4)},
      {{FromString("1-2 3 4:5:6.123456789"), "MINUTE"}, Int64(5)},
      {{FromString("1-2 3 4:5:6.123456789"), "SECOND"}, Int64(6)},
      {{FromString("1-2 3 4:5:6.123456789"), "MILLISECOND"}, Int64(123)},
      {{FromString("1-2 3 4:5:6.123456789"), "MICROSECOND"}, Int64(123456)},
      {{FromString("1-2 3 4:5:6.123456789"), "NANOSECOND"}, Int64(123456789)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "YEAR"}, Int64(-1)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "MONTH"}, Int64(-2)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "DAY"}, Int64(-3)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "HOUR"}, Int64(-4)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "MINUTE"}, Int64(-5)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "SECOND"}, Int64(-6)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "MILLISECOND"}, Int64(-123)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "MICROSECOND"}, Int64(-123456)},
      {{FromString("-1-2 -3 -4:5:6.123456789"), "NANOSECOND"},
       Int64(-123456789)},

      // DATE is a compile time error handled by the resolver.
      {{Years(0), "QUARTER"}, NullInt64(), OUT_OF_RANGE},
      {{Years(0), "WEEK"}, NullInt64(), OUT_OF_RANGE},
      {{Years(0), "ISOWEEK"}, NullInt64(), OUT_OF_RANGE},
  };
  return WrapFeatureIntervalType(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsJustifyInterval() {
  // Although JUSTIFY_XXX family of functions returns INTERVAL, we put STRING
  // as result, because we really need to inspect the individual datetime parts
  // of the resulting interval.
  std::vector<FunctionTestCall> tests = {
      {"justify_hours", {NullInterval()}, NullString()},
      {"justify_days", {NullInterval()}, NullString()},
      {"justify_interval", {NullInterval()}, NullString()},

      {"justify_hours", {FromString("0 12:0:0")}, "0-0 0 12:0:0"},
      {"justify_hours", {FromString("0 -12:0:0")}, "0-0 0 -12:0:0"},
      {"justify_hours", {FromString("0 24:0:0")}, "0-0 1 0:0:0"},
      {"justify_hours", {FromString("0 -24:0:0")}, "0-0 -1 0:0:0"},
      {"justify_hours", {FromString("0 24:0:0.1")}, "0-0 1 0:0:0.100"},
      {"justify_hours", {FromString("0 -24:0:0.1")}, "0-0 -1 -0:0:0.100"},
      {"justify_hours", {FromString("0 25:0:0")}, "0-0 1 1:0:0"},
      {"justify_hours", {FromString("0 -25:0:0")}, "0-0 -1 -1:0:0"},
      {"justify_hours", {FromString("0 240:0:0")}, "0-0 10 0:0:0"},
      {"justify_hours", {FromString("0 -240:0:0")}, "0-0 -10 0:0:0"},
      {"justify_hours", {FromString("1 25:0:0")}, "0-0 2 1:0:0"},
      {"justify_hours", {FromString("-1 -25:0:0")}, "0-0 -2 -1:0:0"},
      {"justify_hours", {FromString("1 -1:0:0")}, "0-0 0 23:0:0"},
      {"justify_hours", {FromString("-1 1:0:0")}, "0-0 0 -23:0:0"},
      {"justify_hours",
       {FromString("3660000 -0:0:0.000001")},
       "0-0 3659999 23:59:59.999999"},
      {"justify_hours",
       {FromString("-3660000 0:0:0.000001")},
       "0-0 -3659999 -23:59:59.999999"},
      {"justify_hours", {FromString("1 0 -1:0:0")}, "0-1 0 -1:0:0"},
      {"justify_hours", {FromString("-1 0 1:0:0")}, "-0-1 0 1:0:0"},

      {"justify_days", {FromString("0-0 29")}, "0-0 29 0:0:0"},
      {"justify_days", {FromString("0-0 -29")}, "0-0 -29 0:0:0"},
      {"justify_days", {FromString("0-0 30")}, "0-1 0 0:0:0"},
      {"justify_days", {FromString("0-0 -30")}, "-0-1 0 0:0:0"},
      {"justify_days", {FromString("0-0 31")}, "0-1 1 0:0:0"},
      {"justify_days", {FromString("0-0 -31")}, "-0-1 -1 0:0:0"},
      {"justify_days", {FromString("0-1 30")}, "0-2 0 0:0:0"},
      {"justify_days", {FromString("-0-1 -30")}, "-0-2 0 0:0:0"},
      {"justify_days", {FromString("1-11 30")}, "2-0 0 0:0:0"},
      {"justify_days", {FromString("-1-11 -30")}, "-2-0 0 0:0:0"},
      {"justify_days", {FromString("0-1 -1")}, "0-0 29 0:0:0"},
      {"justify_days", {FromString("-0-1 1")}, "0-0 -29 0:0:0"},
      {"justify_days", {FromString("10000-0 -1")}, "9999-11 29 0:0:0"},
      {"justify_days", {FromString("-10000-0 1")}, "-9999-11 -29 0:0:0"},
      {"justify_days", {FromString("0-0 3600000")}, "10000-0 0 0:0:0"},
      {"justify_days", {FromString("0-0 -3600000")}, "-10000-0 0 0:0:0"},
      {"justify_days", {FromString("0-0 3600010")}, "10000-0 10 0:0:0"},
      {"justify_days", {FromString("0-0 -3600010")}, "-10000-0 -10 0:0:0"},
      {"justify_days", {FromString("29 240:0:0")}, "0-0 29 240:0:0"},
      {"justify_days", {FromString("-29 -240:0:0")}, "0-0 -29 -240:0:0"},

      {"justify_interval", {FromString("0 0 23")}, "0-0 0 23:0:0"},
      {"justify_interval", {FromString("0 0 -23")}, "0-0 0 -23:0:0"},
      {"justify_interval", {FromString("0 0 24")}, "0-0 1 0:0:0"},
      {"justify_interval", {FromString("0 0 -24")}, "0-0 -1 0:0:0"},
      {"justify_interval", {FromString("0 29 24")}, "0-1 0 0:0:0"},
      {"justify_interval", {FromString("0 -29 -24")}, "-0-1 0 0:0:0"},
      {"justify_interval", {FromString("1 0 -1")}, "0-0 29 23:0:0"},
      {"justify_interval", {FromString("-1 0 1")}, "0-0 -29 -23:0:0"},
      {"justify_interval", {FromString("1 -1 1")}, "0-0 29 1:0:0"},
      {"justify_interval", {FromString("-1 1 -1")}, "0-0 -29 -1:0:0"},
      {"justify_interval", {FromString("1 -1 -1")}, "0-0 28 23:0:0"},
      {"justify_interval", {FromString("-1 1 1")}, "0-0 -28 -23:0:0"},
      {"justify_interval", {FromString("0 3600000 241")}, "10000-0 10 1:0:0"},
      {"justify_interval",
       {FromString("0 -3600000 -241")},
       "-10000-0 -10 -1:0:0"},

      {"justify_hours",
       {FromString("0 3660000 24:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"justify_hours",
       {FromString("0 -3660000 -24:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"justify_days", {FromString("10000-0 30")}, NullString(), OUT_OF_RANGE},
      {"justify_days",
       {FromString("-10000-0 -30")},
       NullString(),
       OUT_OF_RANGE},
      {"justify_days", {FromString("0-0 3660000")}, NullString(), OUT_OF_RANGE},
      {"justify_days",
       {FromString("0-0 -3660000")},
       NullString(),
       OUT_OF_RANGE},
      {"justify_interval",
       {FromString("10000-0 29 24:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"justify_interval",
       {FromString("-10000-0 -29 -24:0:0")},
       NullString(),
       OUT_OF_RANGE},
  };
  return WrapFeatureIntervalType(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsToSecondsInterval() {
  std::vector<FunctionTestCall> tests = {
      {"to_seconds_interval", {NullInterval()}, NullString()},
      {"to_seconds_interval", {FromString("0-0 0 0:0:0")}, "0-0 0 0:0:0"},

      // Testcases where only a single interval part (months, days or nanos) is
      // set.
      {"to_seconds_interval",
       {FromString("0:0:0.000000001")},
       "0-0 0 0:0:0.000000001"},
      {"to_seconds_interval",
       {FromString("-0:0:0.000000001")},
       "0-0 0 -0:0:0.000000001"},
      {"to_seconds_interval", {FromString("0:0:0.1")}, "0-0 0 0:0:0.100"},
      {"to_seconds_interval", {FromString("-0:0:0.1")}, "0-0 0 -0:0:0.100"},
      {"to_seconds_interval", {FromString("0:0:1")}, "0-0 0 0:0:1"},
      {"to_seconds_interval", {FromString("-0:0:1")}, "0-0 0 -0:0:1"},
      {"to_seconds_interval", {FromString("0:1:0")}, "0-0 0 0:1:0"},
      {"to_seconds_interval", {FromString("-0:1:0")}, "0-0 0 -0:1:0"},
      {"to_seconds_interval", {FromString("1:0:0")}, "0-0 0 1:0:0"},
      {"to_seconds_interval", {FromString("-1:0:0")}, "0-0 0 -1:0:0"},
      {"to_seconds_interval", {FromString("1 0:0:0")}, "0-0 0 24:0:0"},
      {"to_seconds_interval", {FromString("-1 0:0:0")}, "0-0 0 -24:0:0"},
      {"to_seconds_interval", {FromString("0-1 0 0:0:0")}, "0-0 0 720:0:0"},
      {"to_seconds_interval", {FromString("-0-1 0 0:0:0")}, "0-0 0 -720:0:0"},
      {"to_seconds_interval", {FromString("1-0 0 0:0:0")}, "0-0 0 8640:0:0"},
      {"to_seconds_interval", {FromString("-1-0 0 0:0:0")}, "0-0 0 -8640:0:0"},
      {"to_seconds_interval", {FromString("1-1 0 0:0:0")}, "0-0 0 9360:0:0"},
      {"to_seconds_interval", {FromString("-1-1 0 0:0:0")}, "0-0 0 -9360:0:0"},

      // Testcases where only the nanos interval part is set (hours, minutes,
      // seconds, subseconds). No change expected in how the interval is encoded
      // as the months and days parts are already zero, only the string
      // representation gets normalized.
      {"to_seconds_interval", {FromString("1:2:3.45")}, "0-0 0 1:2:3.450"},
      {"to_seconds_interval", {FromString("-1:2:3.45")}, "0-0 0 -1:2:3.450"},
      {"to_seconds_interval", {FromString("1:1:100")}, "0-0 0 1:2:40"},
      {"to_seconds_interval", {FromString("1:100:1")}, "0-0 0 2:40:1"},
      {"to_seconds_interval",
       {FromString("100:100:100.10000")},
       "0-0 0 101:41:40.100"},

      // Testcases where multiple interval parts (months, days and nanos) are
      // set.
      {"to_seconds_interval",
       {FromString("1 2:3:4.56789")},
       "0-0 0 26:3:4.567890"},
      {"to_seconds_interval",
       {FromString("1 -2:3:4.56789")},
       "0-0 0 21:56:55.432110"},
      {"to_seconds_interval",
       {FromString("-1 2:3:4.56789")},
       "0-0 0 -21:56:55.432110"},
      {"to_seconds_interval",
       {FromString("11-11 11 11:11:11.11")},
       "0-0 0 103235:11:11.110"},
      {"to_seconds_interval",
       {FromString("-11-11 -11 -11:11:11.11")},
       "0-0 0 -103235:11:11.110"},
      {"to_seconds_interval",
       {FromString("-11-11 11 11:11:11.11")},
       "0-0 0 -102684:48:48.890"},
      {"to_seconds_interval",
       {FromString("11-11 -11 11:11:11.11")},
       "0-0 0 102707:11:11.110"},
      {"to_seconds_interval",
       {FromString("11-11 11 -11:11:11.11")},
       "0-0 0 103212:48:48.890"},

      // Testcases with the max value of each interval part.
      {"to_seconds_interval",
       {FromString("10000-0 0 0:0:0")},
       "0-0 0 86400000:0:0"},
      {"to_seconds_interval",
       {FromString("-10000-0 0 0:0:0")},
       "0-0 0 -86400000:0:0"},
      {"to_seconds_interval",
       {FromString("0-120000 0 0:0:0")},
       "0-0 0 86400000:0:0"},
      {"to_seconds_interval",
       {FromString("-0-120000 0 0:0:0")},
       "0-0 0 -86400000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 3660000 0:0:0")},
       "0-0 0 87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 -3660000 0:0:0")},
       "0-0 0 -87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 0 87840000:0:0")},
       "0-0 0 87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 0 -87840000:0:0")},
       "0-0 0 -87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 0 0:5270400000:0")},
       "0-0 0 87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 0 -0:5270400000:0")},
       "0-0 0 -87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 0 0:0:316224000000")},
       "0-0 0 87840000:0:0"},
      {"to_seconds_interval",
       {FromString("0-0 0 -0:0:316224000000")},
       "0-0 0 -87840000:0:0"},

      // Testcases where the interval itself is valid but the interval value in
      // nanos is out of the range [kMinNanos, kMaxNanos].
      {"to_seconds_interval",
       {FromString("10000-0 100000 0:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("-10000-0 -100000 0:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 3660000 1:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 -3660000 -1:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 1 87840000:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 -1 -87840000:0:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 1 0:5270400000:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 -1 -0:5270400000:0")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 1 0:0:316224000000")},
       NullString(),
       OUT_OF_RANGE},
      {"to_seconds_interval",
       {FromString("0-0 -1 -0:0:316224000000")},
       NullString(),
       OUT_OF_RANGE},
  };
  return WrapFeatureIntervalType(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsFromProtoDuration() {
  // Tests cases where the timestamp scale is MICROSECONDS (currently determined
  // based on the FEATURE_TIMESTAMP_NANOS language feature flag).
  std::vector<FunctionTestCall> tests_scale_micro = {
      {"from_proto", {NullProtoDuration()}, NullInterval()},
      {"from_proto", {ProtoDuration(0, 0)}, Seconds(0)},
      {"from_proto", {ProtoDuration(0, 10)}, Micros(0)},
      {"from_proto", {ProtoDuration(0, -10)}, Micros(0)},
      {"from_proto", {ProtoDuration(0, 12345)}, Micros(12)},
      {"from_proto", {ProtoDuration(0, -12345)}, Micros(-12)},
      {"from_proto", {ProtoDuration(10, 10)}, Seconds(10)},
      {"from_proto", {ProtoDuration(-10, -10)}, Seconds(-10)},
      {"from_proto",
       {ProtoDuration(123456, 123456)},
       SecondsNanos(123456, 123000)},
      {"from_proto",
       {ProtoDuration(-123456, -123456)},
       SecondsNanos(-123456, -123000)},
      {"from_proto",
       {ProtoDuration(TimeUtil::kDurationMaxSeconds,
                      TimeUtil::kDurationMaxNanoseconds)},
       SecondsNanos(TimeUtil::kDurationMaxSeconds, 999999000)},
      {"from_proto",
       {ProtoDuration(TimeUtil::kDurationMinSeconds,
                      TimeUtil::kDurationMinNanoseconds)},
       SecondsNanos(TimeUtil::kDurationMinSeconds, -999999000)},
      // Bad input (value out of range)
      {"from_proto",
       {ProtoDuration(TimeUtil::kDurationMinSeconds - 1, 0)},
       NullInterval(),
       OUT_OF_RANGE},
      {"from_proto",
       {ProtoDuration(TimeUtil::kDurationMaxSeconds + 1, 0)},
       NullInterval(),
       OUT_OF_RANGE},
      {"from_proto",
       {ProtoDuration(0, TimeUtil::kDurationMinNanoseconds - 1)},
       NullInterval(),
       OUT_OF_RANGE},
      {"from_proto",
       {ProtoDuration(0, TimeUtil::kDurationMaxNanoseconds + 1)},
       NullInterval(),
       OUT_OF_RANGE},
      // Bad input - For durations of one second or more, a non-zero value for
      // the nanos field must be of the same sign as the seconds field.
      {"from_proto", {ProtoDuration(1, -1)}, NullInterval(), OUT_OF_RANGE},
      {"from_proto", {ProtoDuration(-1, 1)}, NullInterval(), OUT_OF_RANGE},
  };
  for (FunctionTestCall& test : tests_scale_micro) {
    test.params.AddRequiredFeatures(
        {FEATURE_INTERVAL_TYPE, FEATURE_FROM_AND_TO_PROTO_INTERVAL});
  }

  // Tests cases where the timestamp precision is set to NANOSECONDS.
  std::vector<FunctionTestCall> tests_scale_nano = {
      {"from_proto", {ProtoDuration(0, 10)}, Nanos(10)},
      {"from_proto", {ProtoDuration(0, -10)}, Nanos(-10)},
      {"from_proto", {ProtoDuration(0, 12345)}, Nanos(12345)},
      {"from_proto", {ProtoDuration(0, -12345)}, Nanos(-12345)},
      {"from_proto", {ProtoDuration(10, 10)}, SecondsNanos(10, 10)},
      {"from_proto", {ProtoDuration(-10, -10)}, SecondsNanos(-10, -10)},
      {"from_proto",
       {ProtoDuration(123456, 123456)},
       SecondsNanos(123456, 123456)},
      {"from_proto",
       {ProtoDuration(-123456, -123456)},
       SecondsNanos(-123456, -123456)},
      {"from_proto",
       {ProtoDuration(TimeUtil::kDurationMaxSeconds,
                      TimeUtil::kDurationMaxNanoseconds)},
       SecondsNanos(TimeUtil::kDurationMaxSeconds,
                    TimeUtil::kDurationMaxNanoseconds)},
      {"from_proto",
       {ProtoDuration(TimeUtil::kDurationMinSeconds,
                      TimeUtil::kDurationMinNanoseconds)},
       SecondsNanos(TimeUtil::kDurationMinSeconds,
                    TimeUtil::kDurationMinNanoseconds)},
  };
  for (FunctionTestCall& test : tests_scale_nano) {
    test.params.AddRequiredFeatures({FEATURE_INTERVAL_TYPE,
                                     FEATURE_FROM_AND_TO_PROTO_INTERVAL,
                                     FEATURE_TIMESTAMP_NANOS});
  }

  std::vector<FunctionTestCall> tests;
  tests.reserve(tests_scale_micro.size() + tests_scale_nano.size());
  tests.insert(tests.end(), tests_scale_micro.begin(), tests_scale_micro.end());
  tests.insert(tests.end(), tests_scale_nano.begin(), tests_scale_nano.end());
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsToProtoInterval() {
  std::vector<FunctionTestCall> tests = {
      {"to_proto", {NullInterval()}, NullProtoDuration()},
      {"to_proto", {Nanos(0)}, ProtoDuration(0, 0)},
      {"to_proto", {Nanos(10)}, ProtoDuration(0, 10)},
      {"to_proto", {Nanos(-10)}, ProtoDuration(0, -10)},
      {"to_proto", {Nanos(12345)}, ProtoDuration(0, 12345)},
      {"to_proto", {Nanos(-12345)}, ProtoDuration(0, -12345)},
      {"to_proto", {Seconds(10)}, ProtoDuration(10, 0)},
      {"to_proto", {Seconds(-10)}, ProtoDuration(-10, 0)},
      {"to_proto", {SecondsNanos(10, 10)}, ProtoDuration(10, 10)},
      {"to_proto", {SecondsNanos(-10, -10)}, ProtoDuration(-10, -10)},
      {"to_proto",
       {SecondsNanos(123456, 123456)},
       ProtoDuration(123456, 123456)},
      {"to_proto",
       {SecondsNanos(-123456, -123456)},
       ProtoDuration(-123456, -123456)},

      // Testcases resulting in maximum/minimum value of a Duration proto.
      {"to_proto",
       {SecondsNanos(TimeUtil::kDurationMaxSeconds,
                     TimeUtil::kDurationMaxNanoseconds)},
       ProtoDuration(TimeUtil::kDurationMaxSeconds,
                     TimeUtil::kDurationMaxNanoseconds)},
      {"to_proto",
       {SecondsNanos(TimeUtil::kDurationMinSeconds,
                     TimeUtil::kDurationMinNanoseconds)},
       ProtoDuration(TimeUtil::kDurationMinSeconds,
                     TimeUtil::kDurationMinNanoseconds)},

      // Intervals with NANOS-only part, but the interval value is larger /
      // smaller than the range of a Duration proto.
      {"to_proto",
       {SecondsNanos(TimeUtil::kDurationMaxSeconds + 1, 0)},
       NullProtoDuration(),
       OUT_OF_RANGE},
      {"to_proto",
       {SecondsNanos(TimeUtil::kDurationMinSeconds - 1, 0)},
       NullProtoDuration(),
       OUT_OF_RANGE},

      // Intervals with MONTHS or DAYS part are not allowed.
      {"to_proto", {Years(1)}, NullProtoDuration(), OUT_OF_RANGE},
      {"to_proto", {Years(-1)}, NullProtoDuration(), OUT_OF_RANGE},
      {"to_proto", {Months(1)}, NullProtoDuration(), OUT_OF_RANGE},
      {"to_proto", {Months(-1)}, NullProtoDuration(), OUT_OF_RANGE},
      {"to_proto", {Days(1)}, NullProtoDuration(), OUT_OF_RANGE},
      {"to_proto", {Days(-1)}, NullProtoDuration(), OUT_OF_RANGE},
  };
  for (FunctionTestCall& test : tests) {
    test.params.AddRequiredFeatures(
        {FEATURE_INTERVAL_TYPE, FEATURE_FROM_AND_TO_PROTO_INTERVAL});
  }
  return tests;
}

}  // namespace googlesql
