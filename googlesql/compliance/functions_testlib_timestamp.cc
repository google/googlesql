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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/compliance/functions_testlib.h"  
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;

constexpr int64_t kNaiveNumSecondsPerDay = 24 * 60 * 60;
}  // namespace

using functions::DateTimestampPart;
using functions::kMicroseconds;
using functions::kNanoseconds;

using functions::DAY;
using functions::HOUR;
using functions::ISOWEEK;
using functions::ISOYEAR;
using functions::MICROSECOND;
using functions::MILLISECOND;
using functions::MINUTE;
using functions::MONTH;
using functions::QUARTER;
using functions::SECOND;
using functions::WEEK;
using functions::WEEK_FRIDAY;
using functions::WEEK_MONDAY;
using functions::WEEK_SATURDAY;
using functions::WEEK_THURSDAY;
using functions::WEEK_TUESDAY;
using functions::WEEK_WEDNESDAY;
using functions::YEAR;

template <typename Type>
static std::vector<Type> ConcatTests(
    absl::Span<const std::vector<Type>> test_vectors) {
  std::vector<Type> result;
  for (const auto& v : test_vectors) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

std::vector<FunctionTestCall> GetFunctionTestsDateAdd() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  auto date_add = [part_enum](std::string date, int64_t interval,
                              std::string part, std::string result) {
    return FunctionTestCall(
        "date_add", {DateFromStr(date), Int64(interval), Enum(part_enum, part)},
        DateFromStr(result));
  };
  auto date_add_error = [part_enum](std::string date, int64_t interval,
                                    std::string part) {
    return FunctionTestCall(
        "date_add", {DateFromStr(date), Int64(interval), Enum(part_enum, part)},
        NullDate(), OUT_OF_RANGE);
  };
  auto max_day_span = types::kDateMax - types::kDateMin;

  return {
      // NULL handling
      {"date_add", {NullDate(), Int64(1), Enum(part_enum, "YEAR")}, NullDate()},
      {"date_add",
       {DateFromStr("2001-01-01"), NullInt64(), Enum(part_enum, "YEAR")},
       NullDate()},
      {"date_add",
       {NullDate(), NullInt64(), Enum(part_enum, "YEAR")},
       NullDate()},
      // Tests for regression of b/19829778
      date_add_error("2000-01-01",
                     static_cast<int64_t>(std::numeric_limits<int32_t>::max())
                         << 1,
                     "YEAR"),
      date_add_error(
          "2000-01-01",
          -((-static_cast<int64_t>(std::numeric_limits<int32_t>::lowest()))
            << 1),
          "YEAR"),
      // This set of tests exercises each of the internal arithmetic overflow
      // checks in the shared AddDate function in googlesql/public/functions
      date_add_error("2001-01-01",
                     std::numeric_limits<int32_t>::max() + int64_t{1}, "DAY"),
      date_add_error("2001-01-01",
                     std::numeric_limits<int32_t>::lowest() - int64_t{1},
                     "DAY"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::lowest(),
                     "DAY"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::lowest(),
                     "WEEK"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::lowest(),
                     "MONTH"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::max(),
                     "MONTH"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::lowest(),
                     "QUARTER"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::lowest(),
                     "YEAR"),
      date_add_error("0001-01-01", std::numeric_limits<int32_t>::max(), "YEAR"),
      date_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                     "DAY"),
      date_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                     "WEEK"),
      date_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                     "MONTH"),
      date_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                     "QUARTER"),
      date_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                     "YEAR"),
      // YEAR part
      date_add("2001-01-01", 0, "YEAR", "2001-01-01"),
      date_add("2000-01-01", 1, "YEAR", "2001-01-01"),
      date_add("2001-01-01", -1, "YEAR", "2000-01-01"),
      date_add("1678-01-01", 583, "YEAR", "2261-01-01"),
      date_add("2261-12-31", -583, "YEAR", "1678-12-31"),
      date_add("2000-02-29", 1, "YEAR", "2001-02-28"),
      date_add("2000-02-29", -1, "YEAR", "1999-02-28"),
      date_add("2001-02-28", -1, "YEAR", "2000-02-28"),
      date_add("1999-02-28", 1, "YEAR", "2000-02-28"),
      date_add("9999-01-01", -9998, "YEAR", "0001-01-01"),
      date_add("0001-01-01", 9998, "YEAR", "9999-01-01"),
      date_add_error("9999-01-01", 1, "YEAR"),
      date_add_error("9999-01-01", -9999, "YEAR"),
      date_add_error("0001-01-01", -1, "YEAR"),
      date_add_error("0001-01-01", 9999, "YEAR"),
      // QUARTER part
      date_add("2000-01-01", 0, "QUARTER", "2000-01-01"),
      date_add("2000-01-01", 1, "QUARTER", "2000-04-01"),
      date_add("2000-12-31", 1, "QUARTER", "2001-03-31"),
      date_add("2000-12-31", 4, "QUARTER", "2001-12-31"),
      date_add("2000-12-31", -2, "QUARTER", "2000-06-30"),
      date_add("2000-01-01", -1, "QUARTER", "1999-10-01"),
      date_add("2000-02-29", 4, "QUARTER", "2001-02-28"),
      date_add("2000-02-29", -4, "QUARTER", "1999-02-28"),
      date_add("2001-02-28", -1, "QUARTER", "2000-11-28"),
      date_add("2001-02-28", 1, "QUARTER", "2001-05-28"),
      date_add("9999-12-31", (-4 * 9999) + 1, "QUARTER", "0001-03-31"),
      date_add("0001-01-01", (4 * 9999) - 1, "QUARTER", "9999-10-01"),
      date_add_error("9999-10-01", 1, "QUARTER"),
      date_add_error("9999-10-01", -4 * 9999, "QUARTER"),
      date_add_error("0001-01-01", -1, "QUARTER"),
      date_add_error("0001-01-01", 4 * 9999, "QUARTER"),
      // MONTH part
      date_add("2000-01-01", 0, "MONTH", "2000-01-01"),
      date_add("1678-01-01", 584 * 12 - 1, "MONTH", "2261-12-01"),
      date_add("2261-12-31", -583 * 12, "MONTH", "1678-12-31"),
      date_add("2000-12-31", 1, "MONTH", "2001-01-31"),  // end of month 31
      date_add("2000-01-31", -1, "MONTH", "1999-12-31"),
      date_add("2000-01-31", -11, "MONTH", "1999-02-28"),
      date_add("2000-01-31", 12, "MONTH", "2001-01-31"),
      date_add("2000-01-31", -12, "MONTH", "1999-01-31"),
      date_add("2000-01-31", -13, "MONTH", "1998-12-31"),
      date_add("2000-01-31", -2, "MONTH", "1999-11-30"),
      date_add("2000-05-31", -1, "MONTH", "2000-04-30"),  // end of month 30
      date_add("2000-05-31", 1, "MONTH", "2000-06-30"),
      date_add("2000-04-30", 1, "MONTH", "2000-05-30"),
      date_add("2000-04-30", -1, "MONTH", "2000-03-30"),
      date_add("2000-05-31", 23, "MONTH", "2002-04-30"),
      date_add("2000-05-31", 24, "MONTH", "2002-05-31"),
      date_add("2000-05-31", 25, "MONTH", "2002-06-30"),
      date_add("2000-05-31", -27, "MONTH", "1998-02-28"),
      date_add("2000-02-29", 1, "MONTH", "2000-03-29"),  // feb and leap year
      date_add("2000-01-31", 1, "MONTH", "2000-02-29"),
      date_add("1999-01-31", 1, "MONTH", "1999-02-28"),
      date_add("2000-02-29", -1, "MONTH", "2000-01-29"),
      date_add("2000-03-31", -1, "MONTH", "2000-02-29"),
      date_add("1999-03-31", -1, "MONTH", "1999-02-28"),
      date_add("2000-01-31", 1, "MONTH", "2000-02-29"),
      date_add("2000-02-29", -12, "MONTH", "1999-02-28"),
      date_add("2000-02-29", 12, "MONTH", "2001-02-28"),
      date_add("2001-02-28", -1, "MONTH", "2001-01-28"),
      date_add("2001-02-28", 1, "MONTH", "2001-03-28"),
      date_add("9999-12-31", (-12 * 9999) + 1, "MONTH", "0001-01-31"),
      date_add("0001-01-01", (12 * 9999) - 1, "MONTH", "9999-12-01"),
      date_add_error("9999-12-01", 1, "MONTH"),
      date_add_error("9999-12-01", -12 * 9999, "MONTH"),
      date_add_error("0001-01-01", -1, "MONTH"),
      date_add_error("0001-01-01", 12 * 9999, "MONTH"),
      // WEEK part
      date_add("2000-01-01", 0, "WEEK", "2000-01-01"),
      date_add("2000-01-01", 1, "WEEK", "2000-01-08"),
      date_add("2000-01-01", -1, "WEEK", "1999-12-25"),
      date_add("2000-02-22", 1, "WEEK", "2000-02-29"),
      date_add("2001-02-22", 1, "WEEK", "2001-03-01"),
      date_add("2000-03-07", -1, "WEEK", "2000-02-29"),
      date_add("2000-02-27", 1, "WEEK", "2000-03-05"),
      date_add("2000-03-05", -1, "WEEK", "2000-02-27"),
      date_add("1999-02-27", 1, "WEEK", "1999-03-06"),
      date_add("1999-03-06", -1, "WEEK", "1999-02-27"),
      date_add("2000-05-31", 3, "WEEK", "2000-06-21"),
      date_add("2000-02-01", 4, "WEEK", "2000-02-29"),
      date_add("2000-05-31", 52, "WEEK", "2001-05-30"),
      date_add("2000-05-31", -52, "WEEK", "1999-06-02"),
      date_add("9999-12-31", -std::floor(max_day_span / 7), "WEEK",
               "0001-01-05"),
      date_add("0001-01-01", std::floor(max_day_span / 7), "WEEK",
               "9999-12-27"),
      date_add_error("9999-12-25", 1, "WEEK"),
      date_add_error("9999-12-25", -(std::floor(max_day_span / 7) + 1), "WEEK"),
      date_add_error("0001-01-01", -1, "WEEK"),
      date_add_error("0001-01-01", std::floor(max_day_span / 7) + 1, "WEEK"),
      // DAY part
      date_add("2000-01-01", 0, "DAY", "2000-01-01"),
      date_add("2000-01-01", 1, "DAY", "2000-01-02"),
      date_add("2000-01-01", -1, "DAY", "1999-12-31"),
      date_add("2000-02-29", 365, "DAY", "2001-02-28"),
      date_add("2000-02-29", -365, "DAY", "1999-03-01"),
      date_add("2000-12-31", 1, "DAY", "2001-01-01"),
      date_add("2000-1-31", 1, "DAY", "2000-02-01"),
      date_add("2001-2-27", 1, "DAY", "2001-02-28"),
      date_add("2001-2-27", 2, "DAY", "2001-03-01"),
      date_add("2001-3-01", -1, "DAY", "2001-02-28"),
      date_add("2001-3-01", -2, "DAY", "2001-02-27"),
      date_add("2000-2-27", 2, "DAY", "2000-02-29"),
      date_add("2000-3-01", -1, "DAY", "2000-02-29"),
      date_add("9999-12-31", -max_day_span, "DAY", "0001-01-01"),
      date_add("0001-01-01", max_day_span, "DAY", "9999-12-31"),
      date_add_error("9999-12-31", 1, "DAY"),
      date_add_error("9999-12-31", -(max_day_span + 1), "DAY"),
      date_add_error("0001-01-01", -1, "DAY"),
      date_add_error("0001-01-01", max_day_span + 1, "DAY"),
  };
}

std::vector<FunctionTestCall> GetFunctionTestsDateSub() {
  // Reuse all of the DATE_ADD tests but change the function name and
  // invert the interval value.
  std::vector<FunctionTestCall> sub_tests = GetFunctionTestsDateAdd();
  for (FunctionTestCall& test_fn : sub_tests) {
    test_fn.function_name = "date_sub";
    if (!test_fn.params.param(1).is_null()) {
      uint64_t interval = test_fn.params.param(1).int64_value();
      *(test_fn.params.mutable_param(1)) = Int64(-1 * interval);
    }
  }
  // And add one more to exercise a corner case unique to subtraction.
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));
  sub_tests.push_back(FunctionTestCall(
      "date_sub",
      {DateFromStr("2001-01-01"), Int64(std::numeric_limits<int64_t>::lowest()),
       Enum(part_enum, "YEAR")},
      NullDate(), OUT_OF_RANGE));
  return sub_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsDateAddSub() {
  return ConcatTests<FunctionTestCall>({
      GetFunctionTestsDateAdd(),
      GetFunctionTestsDateSub(),
  });
}

std::vector<FunctionTestCall> GetFunctionTestsAddDate() {
  std::vector<FunctionTestCall> add_tests = GetFunctionTestsDateAdd();
  for (FunctionTestCall& test_fn : add_tests) {
    test_fn.function_name = "adddate";
  }
  return add_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsSubDate() {
  // Reuse all of the DATE_ADD tests but change the function name and
  // invert the interval value.
  std::vector<FunctionTestCall> sub_tests = GetFunctionTestsDateAdd();
  for (FunctionTestCall& test_fn : sub_tests) {
    test_fn.function_name = "subdate";
    if (!test_fn.params.param(1).is_null()) {
      uint64_t interval = test_fn.params.param(1).int64_value();
      *(test_fn.params.mutable_param(1)) = Int64(-1 * interval);
    }
  }
  // And add one more to exercise a corner case unique to subtraction.
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));
  sub_tests.push_back(FunctionTestCall(
      "subdate",
      {DateFromStr("2001-01-01"), Int64(std::numeric_limits<int64_t>::lowest()),
       Enum(part_enum, "YEAR")},
      NullDate(), OUT_OF_RANGE));
  return sub_tests;
}

static QueryParamsWithResult::FeatureSet GetFeatureSetForDateTimestampPart(
    DateTimestampPart date_part) {
  switch (date_part) {
    // WEEK does not require any features to be set.
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY:
      return {FEATURE_WEEK_WITH_WEEKDAY};
    default:
      return {};
  }
}
static QueryParamsWithResult::FeatureSet GetFeatureSetForDateTimestampPart(
    int date_part) {
  return GetFeatureSetForDateTimestampPart(
      static_cast<DateTimestampPart>(date_part));
}

std::vector<FunctionTestCall> GetFunctionTestsLastDay() {
  const EnumType* part_type = types::DatePartEnumType();
  auto last_day_date = [part_type](std::string date, std::string part,
                                  std::string result) {
    const Value part_value = Enum(part_type, part);
    return FunctionTestCall(
        "last_day", {DateFromStr(date), part_value}, DateFromStr(result));
  };

  auto last_day_date_error = [part_type](std::string date, std::string part) {
    const Value part_value = Enum(part_type, part);
    return FunctionTestCall(
        "last_day", {DateFromStr(date), part_value}, NullDate(), OUT_OF_RANGE);
  };

  auto last_day_datetime = [part_type](Value datetime, std::string part,
                                std::string result) {
    const Value part_value = Enum(part_type, part);
    return FunctionTestCall("last_day", {datetime, part_value},
                            DateFromStr(result));
  };

  auto last_day_datetime_error = [part_type](Value datetime, std::string part) {
    const Value part_value = Enum(part_type, part);
    return FunctionTestCall("last_day", {datetime, part_value}, NullDate(),
                            OUT_OF_RANGE);
  };

  return {
      // NULL handling. Don't try to create a NULL DatePart, though, because the
      // resolver won't let us do that.
      {"last_day", {NullDate(), Enum(part_type, "YEAR")}, NullDate()},
      {"last_day", {NullDate(), Enum(part_type, "MONTH")}, NullDate()},
      {"last_day", {NullDate(), Enum(part_type, "ISOYEAR")}, NullDate()},
      {"last_day", {NullDate(), Enum(part_type, "QUARTER")}, NullDate()},
      {"last_day", {NullDate(), Enum(part_type, "WEEK")}, NullDate()},
      {"last_day", {NullDate(), Enum(part_type, "WEEK_THURSDAY")}, NullDate()},
      // Non-NULL arguments.
      // YEAR
      last_day_date("2001-01-01", "YEAR", "2001-12-31"),
      last_day_date("1900-12-31", "YEAR", "1900-12-31"),
      last_day_date("0001-01-01", "YEAR", "0001-12-31"),
      last_day_date("9999-11-01", "YEAR", "9999-12-31"),
      last_day_datetime(DatetimeMicros(9999, 11, 1, 12, 34, 56, 789123),
                       "YEAR", "9999-12-31"),
      last_day_datetime(DatetimeMicros(1, 1, 1, 1, 3, 5, 789),
                       "YEAR", "0001-12-31"),
      // ISOYEAR
      // ISO years always start on Monday and end on Sunday
      // January 1 of a year is sometimes in week 52 or 53 of the previous year.
      // Similarly, December 31 is sometimes in week 1 of the following year.
      // 2001-01-01 is Monday, 2001-12-30 is a Sunday
      last_day_date("2001-01-01", "ISOYEAR", "2001-12-30"),
      // 1900-12-31 is a Monday, 1901-12-29 is a Sunday
      last_day_date("1900-12-31", "ISOYEAR", "1901-12-29"),
      last_day_date("1900-12-30", "ISOYEAR", "1900-12-30"),
      // 1-1-1 is a Saturday, 1-1-2 is a Sunday
      last_day_date("0001-01-03", "ISOYEAR", "0001-12-30"),
      last_day_datetime(DatetimeMicros(1, 1, 3, 12, 34, 56, 789123),
                       "ISOYEAR", "0001-12-30"),
      last_day_datetime(DatetimeMicros(1900, 12, 31, 12, 34, 56, 789123),
                       "ISOYEAR", "1901-12-29"),
      // 9999-12-31 is Friday, so the last day of isoyear 9999 will overflow
      last_day_date_error("9999-11-01", "ISOYEAR"),
      last_day_datetime_error(DatetimeMicros(9999, 11, 1, 12, 34, 56, 789123),
                             "ISOYEAR"),
      // QUARTER
      last_day_date("9999-11-01", "QUARTER", "9999-12-31"),
      last_day_date("2001-10-01", "QUARTER", "2001-12-31"),
      last_day_date("1900-07-31", "QUARTER", "1900-09-30"),
      last_day_date("2001-01-01", "QUARTER", "2001-03-31"),
      last_day_date("1900-09-30", "QUARTER", "1900-09-30"),
      last_day_date("1900-05-31", "QUARTER", "1900-06-30"),
      last_day_datetime(DatetimeMicros(1900, 7, 31, 12, 34, 56, 789123),
                       "QUARTER", "1900-09-30"),
      last_day_datetime(DatetimeMicros(1900, 10, 31, 12, 34, 56, 789123),
                       "QUARTER", "1900-12-31"),
      // MONTH
      last_day_date("2020-02-29", "MONTH", "2020-02-29"),
      last_day_date("2000-02-01", "MONTH", "2000-02-29"),
      last_day_date("1900-02-01", "MONTH", "1900-02-28"),
      last_day_date("1800-02-01", "MONTH", "1800-02-28"),
      last_day_date("1700-02-01", "MONTH", "1700-02-28"),
      last_day_date("1600-02-01", "MONTH", "1600-02-29"),
      last_day_date("2001-12-31", "MONTH", "2001-12-31"),
      last_day_date("2001-12-30", "MONTH", "2001-12-31"),
      last_day_date("2001-12-01", "MONTH", "2001-12-31"),
      last_day_date("1900-12-30", "MONTH", "1900-12-31"),
      last_day_date("2020-01-30", "MONTH", "2020-01-31"),
      last_day_date("2020-02-10", "MONTH", "2020-02-29"),
      last_day_date("2019-02-01", "MONTH", "2019-02-28"),
      last_day_date("2020-03-10", "MONTH", "2020-03-31"),
      last_day_date("2020-04-10", "MONTH", "2020-04-30"),
      last_day_date("2020-05-10", "MONTH", "2020-05-31"),
      last_day_date("2020-06-10", "MONTH", "2020-06-30"),
      last_day_date("2020-07-10", "MONTH", "2020-07-31"),
      last_day_date("2020-08-10", "MONTH", "2020-08-31"),
      last_day_date("2020-09-10", "MONTH", "2020-09-30"),
      last_day_date("2020-10-10", "MONTH", "2020-10-31"),
      last_day_date("2020-11-10", "MONTH", "2020-11-30"),
      last_day_date("2020-12-10", "MONTH", "2020-12-31"),
      last_day_date("9999-12-01", "MONTH", "9999-12-31"),
      last_day_date("9999-12-31", "MONTH", "9999-12-31"),
      last_day_datetime(DatetimeMicros(1900, 12, 30, 12, 34, 56, 789123),
                       "MONTH", "1900-12-31"),
      last_day_datetime(DatetimeMicros(2020, 2, 10, 12, 34, 56, 789123),
                       "MONTH", "2020-02-29"),
      last_day_datetime(DatetimeMicros(2019, 2, 10, 12, 34, 56, 789123),
                       "MONTH", "2019-02-28"),
      // WEEK
      // 2001-12-30 is a Sunday
      last_day_date("2001-12-30", "WEEK", "2002-01-05"),
      last_day_date("2001-12-30", "ISOWEEK", "2001-12-30"),
      last_day_date("2001-12-30", "WEEK_MONDAY", "2001-12-30"),
      last_day_date("2001-12-30", "WEEK_TUESDAY", "2001-12-31"),
      last_day_date("2001-12-30", "WEEK_WEDNESDAY", "2002-01-01"),
      last_day_date("2001-12-30", "WEEK_THURSDAY", "2002-01-02"),
      last_day_date("2001-12-30", "WEEK_FRIDAY", "2002-01-03"),
      last_day_date("2001-12-30", "WEEK_SATURDAY", "2002-01-04"),
      last_day_datetime(DatetimeMicros(2001, 12, 30, 12, 34, 56, 789123),
                       "WEEK", "2002-01-05"),
      // 2001-12-31 is a Monday
      last_day_date("2001-12-31", "WEEK", "2002-01-05"),
      last_day_date("2001-12-31", "ISOWEEK", "2002-01-06"),
      last_day_date("2001-12-31", "WEEK_MONDAY", "2002-01-06"),
      last_day_date("2001-12-31", "WEEK_TUESDAY", "2001-12-31"),
      last_day_date("2001-12-31", "WEEK_WEDNESDAY", "2002-01-01"),
      last_day_date("2001-12-31", "WEEK_THURSDAY", "2002-01-02"),
      last_day_date("2001-12-31", "WEEK_FRIDAY", "2002-01-03"),
      last_day_date("2001-12-31", "WEEK_SATURDAY", "2002-01-04"),
      last_day_datetime(DatetimeMicros(2001, 12, 31, 12, 34, 56, 789123),
                       "ISOWEEK", "2002-01-06"),
      // 1970-1-1 is Thursday
      last_day_date("1970-01-01", "WEEK", "1970-01-03"),
      last_day_date("1970-01-01", "ISOWEEK", "1970-01-04"),
      last_day_date("1970-01-01", "WEEK_MONDAY", "1970-01-04"),
      last_day_date("1970-01-01", "WEEK_TUESDAY", "1970-01-05"),
      last_day_date("1970-01-01", "WEEK_WEDNESDAY", "1970-01-06"),
      last_day_date("1970-01-01", "WEEK_THURSDAY", "1970-01-07"),
      last_day_date("1970-01-01", "WEEK_FRIDAY", "1970-01-01"),
      last_day_date("1970-01-01", "WEEK_SATURDAY", "1970-01-02"),
      last_day_datetime(DatetimeMicros(1970, 1, 1, 12, 34, 56, 789123),
                       "WEEK_SATURDAY", "1970-01-02"),
      // 9999-12-28 is a Tuesday
      last_day_date_error("9999-12-28", "WEEK"),
      last_day_date_error("9999-12-28", "ISOWEEK"),
      last_day_date_error("9999-12-28", "WEEK_MONDAY"),
      last_day_date_error("9999-12-28", "WEEK_TUESDAY"),
      last_day_date("9999-12-28", "WEEK_WEDNESDAY", "9999-12-28"),
      last_day_date("9999-12-28", "WEEK_THURSDAY", "9999-12-29"),
      last_day_date("9999-12-28", "WEEK_FRIDAY", "9999-12-30"),
      last_day_date("9999-12-28", "WEEK_SATURDAY", "9999-12-31"),
      };
}

std::vector<FunctionTestCall> GetFunctionTestsNextDay() {
  struct testcase {
    std::string input;
    std::string dow;
    std::string result;
    absl::StatusCode code = absl::StatusCode::kOk;
  };
  std::vector<testcase> testcases = {
      // Test every day of the week (including the case that the input day is
      // already that day of week).
      {"2025-09-30", "Mon", "2025-10-06"},
      {"2025-09-30", "Monday", "2025-10-06"},
      {"2025-09-30", "Tue", "2025-10-07"},  // Already a Tuesday.
      {"2025-09-30", "Tuesday", "2025-10-07"},
      {"2025-09-30", "Wed", "2025-10-01"},
      {"2025-09-30", "Wednesday", "2025-10-01"},
      {"2025-09-30", "Thu", "2025-10-02"},
      {"2025-09-30", "Thursday", "2025-10-02"},
      {"2025-09-30", "Fri", "2025-10-03"},
      {"2025-09-30", "Friday", "2025-10-03"},
      {"2025-09-30", "Sat", "2025-10-04"},
      {"2025-09-30", "Saturday", "2025-10-04"},
      {"2025-09-30", "Sun", "2025-10-05"},
      {"2025-09-30", "Sunday", "2025-10-05"},
      // Test some case and length combinations.
      {"2025-09-08", "fRiDAY", "2025-09-12"},
      {"2025-09-08", "FRI", "2025-09-12"},
      // Test invalid day of week.
      {"2025-09-19", "Ahoy", "2025-09-19", absl::StatusCode::kOutOfRange},
      // Test near- and out-of-range.
      {"0001-01-01", "Mon", "0001-01-08"},
      {"9999-12-24", "Fri", "9999-12-31"},
      {"9999-12-31", "Wed", "9999-12-31", absl::StatusCode::kOutOfRange},
  };
  auto next_day_date_test = [](QueryParamsWithResult params) {
    return FunctionTestCall(
        "next_day",
        params.AddRequiredFeature(FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS));
  };
  auto next_day_datetime_test = [](QueryParamsWithResult params) {
    return FunctionTestCall(
        "next_day",
        params.AddRequiredFeatures(
            {FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS, FEATURE_CIVIL_TIME}));
  };

  std::vector<FunctionTestCall> tests;
  // Tests for NULL params.
  tests.push_back(
      next_day_date_test({{NullDate(), String("Sun")}, NullDate()}));
  tests.push_back(next_day_date_test(
      {{DateFromStr("1998-09-04"), NullString()}, NullDate()}));
  tests.push_back(next_day_date_test({{NullDate(), NullString()}, NullDate()}));

  // The DATE and DATETIME tests.
  for (auto& tc : testcases) {
    tests.push_back(next_day_date_test({{DateFromStr(tc.input), String(tc.dow)},
                                        DateFromStr(tc.result),
                                        tc.code}));
    tests.push_back(next_day_datetime_test(
        {{DatetimeFromStr(tc.input + " 01:33:12"), String(tc.dow)},
         DateFromStr(tc.result),
         tc.code}));
  }

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsAddMonths() {
  struct testcase {
    std::string input;
    int64_t months;
    std::string result;
    absl::StatusCode code = absl::StatusCode::kOk;
  };
  std::vector<testcase> testcases = {
      {"1998-09-04", 0, "1998-09-04"},   // no change
      {"1925-03-15", 14, "1926-05-15"},  // not eom
      {"2025-02-28", 1, "2025-03-31"},   // preserve eom fwd
      {"2025-02-28", -1, "2025-01-31"},  // preserve eom back
      {"2025-01-31", 1, "2025-02-28"},   // normalize eom fwd
      {"2025-12-31", -1, "2025-11-30"},  // normalize eom back
      // Near and out of range tests.
      {"9999-02-28", 10, "9999-12-31"},
      {"0001-06-30", -5, "0001-01-31"},
      {"9999-12-25", 1, "9999-12-25", absl::StatusCode::kOutOfRange},
      {"0001-12-25", -13, "0001-12-25", absl::StatusCode::kOutOfRange},
  };
  auto add_months_date_test = [](QueryParamsWithResult params) {
    return FunctionTestCall(
        "add_months",
        params.AddRequiredFeature(FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS));
  };
  auto add_months_datetime_test = [](QueryParamsWithResult params) {
    return FunctionTestCall(
        "add_months",
        params.AddRequiredFeatures(
            {FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS, FEATURE_CIVIL_TIME}));
  };

  std::vector<FunctionTestCall> tests;

  // Tests for NULL DATE params.
  tests.push_back(add_months_date_test({{NullDate(), Int64(123)}, NullDate()}));
  tests.push_back(add_months_date_test(
      {{DateFromStr("1998-09-04"), NullInt64()}, NullDate()}));
  tests.push_back(
      add_months_date_test({{NullDate(), NullInt64()}, NullDate()}));

  // Tests for NULL DATETIME params.
  tests.push_back(
      add_months_datetime_test({{NullDatetime(), Int64(123)}, NullDatetime()}));
  tests.push_back(add_months_datetime_test(
      {{DatetimeFromStr("1998-09-04 01:33:12"), NullInt64()}, NullDatetime()}));
  tests.push_back(add_months_datetime_test(
      {{NullDatetime(), NullInt64()}, NullDatetime()}));

  // The DATE and DATETIME tests.
  for (const auto& tc : testcases) {
    tests.push_back(
        add_months_date_test({{DateFromStr(tc.input), Int64(tc.months)},
                              DateFromStr(tc.result),
                              tc.code}));
    const std::string time = " 09:30:04";
    tests.push_back(add_months_datetime_test(
        {{DatetimeFromStr(tc.input + time), Int64(tc.months)},
         DatetimeFromStr(tc.result + time),
         tc.code}));
  }
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsMonthsBetween() {
  const int64_t micros_per_day = absl::ToInt64Microseconds(absl::Hours(24));
  struct testcase {
    std::string date1;
    std::string date2;
    double result;
    absl::StatusCode code = absl::StatusCode::kOk;
  };
  std::vector<testcase> testcases = {
      // Basic test for a month and a day.
      {"2025-11-30", "2025-10-29", 1 + 1 / 31.0},
      // Switching args switches the sign.
      {"2025-10-29", "2025-11-30", -1 - 1 / 31.0},
      // Fractional day.
      {"2025-11-14", "2025-10-16", 1 - 2 / 31.0},
      // Same day: whole number.
      {"2025-11-14", "2025-10-14", 1.0},
      // Both end-of-month: whole number.  Also tests the year difference.
      {"2025-11-30", "2024-10-31", 13.0},
  };
  auto months_between_date_test = [](QueryParamsWithResult params) {
    return FunctionTestCall(
        "months_between",
        params.AddRequiredFeature(FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS));
  };
  auto months_between_datetime_test = [](QueryParamsWithResult params) {
    return FunctionTestCall(
        "months_between",
        params.AddRequiredFeatures(
            {FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS, FEATURE_CIVIL_TIME}));
  };

  std::vector<FunctionTestCall> tests;

  // Tests for NULL DATE params.
  tests.push_back(months_between_date_test(
      {{NullDate(), DateFromStr("2025-01-01")}, NullDouble()}));
  tests.push_back(months_between_date_test(
      {{DateFromStr("2025-01-01"), NullDate()}, NullDouble()}));
  tests.push_back(
      months_between_date_test({{NullDate(), NullDate()}, NullDouble()}));

  // Tests for NULL DATETIME params.
  tests.push_back(months_between_datetime_test(
      {{NullDatetime(), DatetimeFromStr("2025-01-01 01:33:12")},
       NullDouble()}));
  tests.push_back(months_between_datetime_test(
      {{DatetimeFromStr("2025-01-01 01:33:12"), NullDatetime()},
       NullDouble()}));
  tests.push_back(months_between_datetime_test(
      {{NullDatetime(), NullDatetime()}, NullDouble()}));

  // DATE and basic DATETIME tests.  We don't include a time portion for the
  // DATETIME tests here so we can reuse the same testcases.  The time portion
  // is tested in the DATETIME-specific tests below.
  for (const auto& tc : testcases) {
    tests.push_back(months_between_date_test(
        {{DateFromStr(tc.date1), DateFromStr(tc.date2)},
         Double(tc.result),
         tc.code}));
    tests.push_back(months_between_datetime_test(
        {{DatetimeFromStr(tc.date1), DatetimeFromStr(tc.date2)},
         Double(tc.result),
         tc.code}));
  }

  // DATETIME tests with time.
  // - Basic fractional day.
  tests.push_back(
      months_between_datetime_test({{DatetimeFromStr("2008-02-29 00:00:00"),
                                     DatetimeFromStr("2008-02-28 12:00:00")},
                                    Double(0.5 / 31.0)}));
  // - Test that HH:MM:SS is included. (We spell out the computation to make it
  //   clear where the result comes from, in case it fails and someone has
  //   to debug it.)
  tests.push_back(months_between_datetime_test(
      {{DatetimeFromStr("2008-04-15 06:15:45"),
        DatetimeFromStr("2008-05-10 12:30:15")},
       Double((4 - 5) +                    // Whole months.
              ((15 - 10) +                 // Days for fractional month:
               (06 - 12) / 24.0 +          // ... from hour
               (15 - 30) / 60.0 / 24 +     // ... from minute
               (45 - 15) / 60.0 / 60 / 24  // ... from second
               ) / 31.0)}));
  // - Test that fractional microseconds are included.
  tests.push_back(months_between_datetime_test(
      {{DatetimeFromStr("2008-02-29 00:00:00.123456"),
        DatetimeFromStr("2008-02-28 00:00:00.789012")},
       Double(((29 - 28) + (123456.0 - 789012.0) / micros_per_day) / 31.0)}));
  // - Test that time is ignored when day is the same or end-of-month.
  tests.push_back(
      months_between_datetime_test({{DatetimeFromStr("2025-03-14 23:00:00"),
                                     DatetimeFromStr("2025-02-14 00:00:00")},
                                    Double(1.0)}));
  tests.push_back(
      months_between_datetime_test({{DatetimeFromStr("2025-02-28 23:00:00"),
                                     DatetimeFromStr("2025-03-31 00:00:00")},
                                    Double(-1.0)}));

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsDateTrunc() {
  const EnumType* part_type = types::DatePartEnumType();
  auto date_trunc = [part_type](std::string date, std::string part,
                                std::string result) {
    const Value part_value = Enum(part_type, part);
    QueryParamsWithResult test_case({DateFromStr(date), part_value},
                                    DateFromStr(result));
    test_case.AddRequiredFeatures(
        GetFeatureSetForDateTimestampPart(part_value.enum_value()));
    return FunctionTestCall("date_trunc", test_case);
  };

  auto date_trunc_error = [part_type](std::string date, std::string part) {
    const Value part_value = Enum(part_type, part);
    QueryParamsWithResult test_case({DateFromStr(date), part_value}, NullDate(),
                                    OUT_OF_RANGE);
    test_case.AddRequiredFeatures(
        GetFeatureSetForDateTimestampPart(part_value.enum_value()));
    return FunctionTestCall("date_trunc", test_case);
  };

  return {
      // NULL handling. Don't try to create a NULL DatePart, though, because the
      // resolver won't let us do that.
      {"date_trunc", {NullDate(), Enum(part_type, "YEAR")}, NullDate()},
      // Non-NULL arguments.
      date_trunc("2001-12-31", "YEAR", "2001-01-01"),
      date_trunc("1900-12-31", "YEAR", "1900-01-01"),
      date_trunc("0001-01-01", "YEAR", "0001-01-01"),
      date_trunc("9999-12-31", "YEAR", "9999-01-01"),
      date_trunc("2001-12-31", "QUARTER", "2001-10-01"),
      date_trunc("1900-07-31", "QUARTER", "1900-07-01"),
      date_trunc("2001-12-31", "MONTH", "2001-12-01"),
      date_trunc("1900-12-31", "MONTH", "1900-12-01"),
      date_trunc("2001-12-31", "WEEK", "2001-12-30"),
      date_trunc("2001-12-30", "ISOWEEK", "2001-12-24"),
      date_trunc("2001-12-30", "WEEK_MONDAY", "2001-12-24"),
      date_trunc("2001-12-30", "WEEK_TUESDAY", "2001-12-25"),
      date_trunc("2001-12-30", "WEEK_WEDNESDAY", "2001-12-26"),
      date_trunc("2001-12-30", "WEEK_THURSDAY", "2001-12-27"),
      date_trunc("2001-12-30", "WEEK_FRIDAY", "2001-12-28"),
      date_trunc("2001-12-30", "WEEK_SATURDAY", "2001-12-29"),
      date_trunc("1970-01-01", "WEEK", "1969-12-28"),
      date_trunc("1970-01-01", "ISOWEEK", "1969-12-29"),
      date_trunc("1970-01-01", "WEEK_MONDAY", "1969-12-29"),
      date_trunc("1970-01-01", "WEEK_TUESDAY", "1969-12-30"),
      date_trunc("1970-01-01", "WEEK_WEDNESDAY", "1969-12-31"),
      date_trunc("1970-01-01", "WEEK_THURSDAY", "1970-01-01"),
      date_trunc("1970-01-01", "WEEK_FRIDAY", "1969-12-26"),
      date_trunc("1970-01-01", "WEEK_SATURDAY", "1969-12-27"),
      date_trunc("1900-12-31", "WEEK", "1900-12-30"),
      date_trunc("1900-12-30", "ISOWEEK", "1900-12-24"),
      date_trunc("1900-12-30", "WEEK_MONDAY", "1900-12-24"),
      date_trunc("1900-12-30", "WEEK_TUESDAY", "1900-12-25"),
      date_trunc("1900-12-30", "WEEK_WEDNESDAY", "1900-12-26"),
      date_trunc("1900-12-30", "WEEK_THURSDAY", "1900-12-27"),
      date_trunc("1900-12-30", "WEEK_FRIDAY", "1900-12-28"),
      date_trunc("1900-12-30", "WEEK_SATURDAY", "1900-12-29"),
      // 0001-01-01 is a Monday on the Gregorian calendar, so truncation to WEEK
      // is out of bounds but truncation to WEEK_<WEEKDAY> is ok.
      date_trunc_error("0001-01-01", "WEEK"),
      date_trunc_error("0001-01-06", "WEEK"),
      date_trunc("0001-01-01", "ISOWEEK", "0001-01-01"),
      date_trunc("0001-01-01", "WEEK_MONDAY", "0001-01-01"),
      date_trunc("0001-01-07", "WEEK", "0001-01-07"),
      date_trunc("2001-12-31", "DAY", "2001-12-31"),
      date_trunc("1900-12-31", "DAY", "1900-12-31"),
  };
}

// A shorthand to construct and return a vector of FunctionTestCall with the
// <function_name> and wrapped results from the vector of CivilTimeTestCase.
static std::vector<FunctionTestCall> PrepareCivilTimeTestCaseAsFunctionTestCall(
    absl::Span<const CivilTimeTestCase> test_cases,
    absl::string_view function_name) {
  std::vector<QueryParamsWithResult> cases_with_features;
  cases_with_features.reserve(test_cases.size());
  for (const auto& each : test_cases) {
    AddTestCaseWithWrappedResultForCivilTimeAndNanos(each,
                                                     &cases_with_features);
  }
  std::vector<FunctionTestCall> out;
  out.reserve(test_cases.size());
  for (const auto& each : cases_with_features) {
    out.emplace_back(function_name, each);
  }
  return out;
}

static absl::Status FunctionEvalError() {
  return absl::Status(absl::StatusCode::kOutOfRange,
                      "Function Evaluation Error");
}

std::vector<CivilTimeTestCase> GetFunctionTestsDatetimeAdd() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  const Value datetime_micros = DatetimeMicros(2016, 3, 16, 18, 49, 32, 123456);

  std::vector<CivilTimeTestCase> civil_time_test_cases({
      // NULL handling
      {{{NullDatetime(), Int64(1), Enum(part_enum, "YEAR")}}, NullDatetime()},
      {{{datetime_micros, NullInt64(), Enum(part_enum, "YEAR")}},
       NullDatetime()},
      {{{NullDatetime(), NullInt64(), Enum(part_enum, "YEAR")}},
       NullDatetime()},

      // Successful cases, more will be added later when reusing the test cases
      // from TimestampAdd and DateAdd.
      // Nanoseconds
      {{{datetime_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       DatetimeNanos(1723, 12, 6, 19, 2, 15, 268680192)},
      {{{datetime_micros, Int64(std::numeric_limits<int64_t>::lowest() + 1),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       DatetimeNanos(1723, 12, 6, 19, 2, 15, 268680193)},
      {{{datetime_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       DatetimeNanos(2308, 6, 26, 18, 36, 48, 978231807)},
      {{{datetime_micros, Int64(123456789987654321),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       DatetimeNanos(2020, 2, 13, 16, 22, 42, 111110321)},
      {{{datetime_micros, Int64(-123456789987654321),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       DatetimeNanos(2012, 4, 17, 21, 16, 22, 135801679)},
      {{{datetime_micros, Int64(0), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       datetime_micros},
      // Year 2038 does not affect anything
      {{{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0),
         Int64(std::numeric_limits<int32_t>::max()),
         Enum(part_enum, "SECOND")}},
       DatetimeMicros(2038, 1, 19, 3, 14, 7, 0)},
  });

  // Overflow cases and max/min datetime cases
  {
    const Value max_datetime_micros =
        DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999);
    const Value min_datetime_micros = DatetimeMicros(1, 1, 1, 0, 0, 0, 0);
    const std::vector<std::string> parts = {
        "YEAR", "QUARTER", "MONTH",  "WEEK",        "DAY",
        "HOUR", "MINUTE",  "SECOND", "MILLISECOND", "MICROSECOND"};
    for (const auto& part : parts) {
      // Note that adding int64 as NANOSECOND never overflows with recent
      // datetime.
      civil_time_test_cases.push_back(
          {{{datetime_micros, Int64(std::numeric_limits<int64_t>::max()),
             Enum(part_enum, part)}},
           FunctionEvalError() /* expected_output */,
           DatetimeType()});
      civil_time_test_cases.push_back(
          {{{datetime_micros, Int64(std::numeric_limits<int64_t>::lowest()),
             Enum(part_enum, part)}},
           FunctionEvalError() /* expected_output */,
           DatetimeType()});
      civil_time_test_cases.push_back(
          {{{datetime_micros, Int64(std::numeric_limits<int64_t>::lowest() + 1),
             Enum(part_enum, part)}},
           FunctionEvalError() /* expected_output */,
           DatetimeType()});

      // Adding any positive interval on the maximum datetime will lead to an
      // overflow.
      civil_time_test_cases.push_back(
          {{{max_datetime_micros, Int64(1), Enum(part_enum, part)}},
           FunctionEvalError() /* expected_output */,
           DatetimeType()});
      // Adding any negative interval on the minimum datetime will also cause
      // overflow.
      civil_time_test_cases.push_back(
          {{{min_datetime_micros, Int64(-1), Enum(part_enum, part)}},
           FunctionEvalError() /* expected_output */,
           DatetimeType()});

      // Adding zero interval does not change the result
      civil_time_test_cases.push_back(
          {{{min_datetime_micros, Int64(0), Enum(part_enum, part)}},
           min_datetime_micros});
      civil_time_test_cases.push_back(
          {{{max_datetime_micros, Int64(0), Enum(part_enum, part)}},
           max_datetime_micros});
      civil_time_test_cases.push_back(
          {{{datetime_micros, Int64(0), Enum(part_enum, part)}},
           datetime_micros});
    }

    // Maximum/minimum cases for nanoseconds.
    const Value max_datetime_nanos =
        DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999999);
    const Value min_datetime_nanos =
        DatetimeNanos(0001, 01, 01, 0, 0, 0, 0);
    civil_time_test_cases.push_back(
        {{{max_datetime_nanos, Int64(1), Enum(part_enum, "NANOSECOND")}},
         /*expected_output=*/FunctionEvalError(),
         DatetimeType(),
         {FEATURE_TIMESTAMP_NANOS}});
    civil_time_test_cases.push_back(
        {{{max_datetime_nanos, Int64(0), Enum(part_enum, "NANOSECOND")}},
         /*expected_output=*/max_datetime_nanos,
         DatetimeType(),
         {FEATURE_TIMESTAMP_NANOS}});
    civil_time_test_cases.push_back(
        {{{min_datetime_nanos, Int64(-1), Enum(part_enum, "NANOSECOND")}},
         /*expected_output=*/FunctionEvalError(),
         DatetimeType(),
         {FEATURE_TIMESTAMP_NANOS}});
    civil_time_test_cases.push_back(
        {{{min_datetime_nanos, Int64(0), Enum(part_enum, "NANOSECOND")}},
         /*expected_output=*/min_datetime_nanos,
         DatetimeType(),
         {FEATURE_TIMESTAMP_NANOS}});
  }

  // Reuse all the successful test cases from TimestampAdd
  for (const FunctionTestCall& test_fn : GetFunctionTestsTimestampAdd()) {
    if (test_fn.params.status().ok() && !test_fn.params.result().is_null()) {
      DatetimeValue datetime_input;
      DatetimeValue datetime_output;

      // The datetime input/output constructed here should always be valid if
      // the timestamps in the original test cases are valid, which is
      // guaranteed because the test case has a successful status.
      GOOGLESQL_CHECK_OK(functions::ConvertTimestampToDatetime(
          test_fn.params.param(0).ToTime(), "UTC", &datetime_input));
      GOOGLESQL_CHECK_OK(functions::ConvertTimestampToDatetime(
          test_fn.params.result().ToTime(), "UTC", &datetime_output));

      civil_time_test_cases.push_back(
          {{{Value::Datetime(datetime_input), test_fn.params.param(1),
             test_fn.params.param(2)}},
           Value::Datetime(datetime_output)});
    }
  }

  // Reuse all the successful test cases from DateAdd
  for (const FunctionTestCall& test_fn : GetFunctionTestsDateAdd()) {
    if (test_fn.params.status().ok() && !test_fn.params.result().is_null()) {
      DatetimeValue datetime_input;
      DatetimeValue datetime_output;

      // The datetime input/output constructed here should always be valid if
      // the dates in the original test cases are valid, which is guaranteed
      // because the test case has a successful status.
      static const TimeValue midnight;
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_fn.params.param(0).date_value(), midnight, &datetime_input));
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_fn.params.result().date_value(), midnight, &datetime_output));
      civil_time_test_cases.push_back(
          {{{Value::Datetime(datetime_input), test_fn.params.param(1),
             test_fn.params.param(2)}},
           Value::Datetime(datetime_output)});

      // Use a different time in the day does not affect anything because the
      // part is always greater than a day here.
      static const TimeValue some_time =
          TimeValue::FromHMSAndMicros(12, 34, 56, 654321);
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_fn.params.param(0).date_value(), some_time, &datetime_input));
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_fn.params.result().date_value(), some_time, &datetime_output));
      civil_time_test_cases.push_back(
          {{{Value::Datetime(datetime_input), test_fn.params.param(1),
             test_fn.params.param(2)}},
           Value::Datetime(datetime_output)});
    }
  }
  return civil_time_test_cases;
}

static std::vector<CivilTimeTestCase> GetFunctionTestsDatetimeSub() {
  std::vector<CivilTimeTestCase> civil_time_test_cases(
      GetFunctionTestsDatetimeAdd());
  std::vector<int> index_of_elements_to_remove;
  for (int i = 0; i < civil_time_test_cases.size(); ++i) {
    auto& test_case = civil_time_test_cases[i];
    const auto& arguments = test_case.input;
    if (!arguments[1].get().is_null()) {
      if (arguments[1].get().int64_value() !=
          std::numeric_limits<int64_t>::lowest()) {
        // Adding an <interval> to a datetime should produce the same result as
        // subtracting (-1 * <interval>) from the same datetime.
        std::vector<ValueConstructor> new_arguments(
            {arguments[0], Int64(-1 * arguments[1].get().int64_value()),
             arguments[2]});
        test_case.input.swap(new_arguments);
      } else {
        // For cases with interval == std::numeric_limits<int64>::lowest(), -1 *
        // interval will overflow int64, which is not a valid input for
        // datetime_sub().
        if (test_case.nanos_output.ok() || test_case.micros_output.ok()) {
          // If the test case with a std::numeric_limits<int64>::lowest()
          // interval is expecting a successful result, mark this test case for
          // removal later.
          index_of_elements_to_remove.push_back(i);
        } else {
          // However, most of the test cases in GetFunctionTestsDatetimeAdd are
          // expecting errors with interval ==
          // std::numeric_limits<int64>::lowest(), which is the same expected
          // result when the same arguments are passed into datetime_sub(), thus
          // we can reuse these test cases directly.
        }
      }
    }
  }
  for (auto itr = index_of_elements_to_remove.rbegin();
       itr != index_of_elements_to_remove.rend(); ++itr) {
    civil_time_test_cases.erase(civil_time_test_cases.begin() + *itr);
  }

  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));
  civil_time_test_cases.push_back(
      {{{DatetimeMicros(2016, 3, 16, 18, 49, 32, 123456),
         Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       DatetimeNanos(2308, 6, 26, 18, 36, 48, 978231808)});

  return civil_time_test_cases;
}

std::vector<FunctionTestCall> GetFunctionTestsDatetimeAddSub() {
  return ConcatTests<FunctionTestCall>({
      PrepareCivilTimeTestCaseAsFunctionTestCall(GetFunctionTestsDatetimeAdd(),
                                                 "datetime_add"),
      PrepareCivilTimeTestCaseAsFunctionTestCall(GetFunctionTestsDatetimeSub(),
                                                 "datetime_sub"),
  });
}

static std::vector<FunctionTestCall> GetFunctionTestsDateDiff();

// For each test case of datetime_diff or time_diff (arg_1, arg_2,
// expected_result), also add the test case (arg_2, arg_1, - expected_result)
// when the expected_result has a valid negation.
//
// Note that it's possible for (arg_1 - arg_2) to be int64max + 1 so it
// overflows in the original test case. However, (arg_2 - arg_1) gives int64min,
// which is a valid int64 result. This function cannot handle such cases, so it
// should not be included in the original test cases.
static void AddSwappedDiffTestCases(
    std::vector<CivilTimeTestCase>* diff_test_cases) {
  int num_original_test_cases = diff_test_cases->size();
  for (int i = 0; i < num_original_test_cases; ++i) {
    const auto& test_case = (*diff_test_cases)[i];
    auto MakeNegativeOutput = [](const absl::StatusOr<Value>& original_result) {
      absl::StatusOr<Value> negative_result;
      if (original_result.ok() && !original_result.value().is_null()) {
        int64_t result_value = original_result.value().int64_value();
        if (result_value == std::numeric_limits<int64_t>::lowest()) {
          negative_result = FunctionEvalError();
        } else {
          negative_result = Int64(-result_value);
        }
      } else {
        negative_result = original_result;
      }
      return negative_result;
    };
    diff_test_cases->push_back(
        {{{test_case.input[1], test_case.input[0], test_case.input[2]}},
         MakeNegativeOutput(test_case.micros_output),
         MakeNegativeOutput(test_case.nanos_output),
         test_case.output_type,
         test_case.required_features});
  }
}

std::vector<FunctionTestCall> GetFunctionTestsDatetimeDiff() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  static const Value datetime_micros_1 =
      DatetimeMicros(2016, 4, 8, 14, 49, 32, 123456);
  static const Value datetime_micros_2 =
      DatetimeMicros(1969, 7, 21, 2, 56, 13, 456789);
  static const Value max_datetime_micros =
      DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999);
  static const Value min_datetime = DatetimeMicros(1, 1, 1, 0, 0, 0, 0);

  std::vector<CivilTimeTestCase> civil_time_test_cases({
      // NULL handling
      {{{NullDatetime(), datetime_micros_2, Enum(part_enum, "YEAR")}},
       NullInt64()},
      {{{datetime_micros_1, NullDatetime(), Enum(part_enum, "YEAR")}},
       NullInt64()},
      {{{NullDatetime(), NullDatetime(), Enum(part_enum, "YEAR")}},
       NullInt64()},

      // The test cases for date parts of YEAR, QUARTER, MONTH and DAY will be
      // adopted from GetFunctionTestsDateDiff() later.

      // HOUR
      {{{DatetimeMicros(2016, 4, 8, 14, 0, 0, 0),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         Enum(part_enum, "HOUR")}},
       Int64(0)},
      {{{DatetimeMicros(2016, 4, 8, 23, 0, 0, 0),
         DatetimeMicros(2016, 4, 8, 0, 59, 59, 123456),
         Enum(part_enum, "HOUR")}},
       Int64(23)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         DatetimeMicros(2016, 4, 8, 15, 0, 0, 0), Enum(part_enum, "HOUR")}},
       Int64(-1)},
      {{{datetime_micros_1, datetime_micros_2, Enum(part_enum, "HOUR")}},
       Int64(409524)},
      {{{max_datetime_micros, min_datetime, Enum(part_enum, "HOUR")}},
       Int64(87649415)},

      // MINUTE
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 0, 0),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         Enum(part_enum, "MINUTE")}},
       Int64(0)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 0, 0),
         DatetimeMicros(2016, 4, 8, 14, 0, 59, 123456),
         Enum(part_enum, "MINUTE")}},
       Int64(59)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         DatetimeMicros(2016, 4, 8, 15, 0, 0, 0), Enum(part_enum, "MINUTE")}},
       Int64(-1)},
      {{{datetime_micros_1, datetime_micros_2, Enum(part_enum, "MINUTE")}},
       Int64(24571433)},
      {{{max_datetime_micros, min_datetime, Enum(part_enum, "MINUTE")}},
       Int64(5258964959)},

      // SECOND
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 0),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         Enum(part_enum, "SECOND")}},
       Int64(0)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 0),
         DatetimeMicros(2016, 4, 8, 14, 59, 0, 123456),
         Enum(part_enum, "SECOND")}},
       Int64(59)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         DatetimeMicros(2016, 4, 8, 15, 0, 0, 0), Enum(part_enum, "SECOND")}},
       Int64(-1)},
      {{{datetime_micros_1, datetime_micros_2, Enum(part_enum, "SECOND")}},
       Int64(1474285999)},
      {{{max_datetime_micros, min_datetime, Enum(part_enum, "SECOND")}},
       Int64(315537897599)},

      // MILLISECOND
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 0),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 123),
         Enum(part_enum, "MILLISECOND")}},
       Int64(0)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 123000),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 123999),
         Enum(part_enum, "MILLISECOND")}},
       Int64(0)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 654321),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 567987),
         Enum(part_enum, "MILLISECOND")}},
       Int64(87)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         DatetimeMicros(2016, 4, 8, 15, 0, 0, 0),
         Enum(part_enum, "MILLISECOND")}},
       Int64(-877)},
      {{{datetime_micros_1, datetime_micros_2, Enum(part_enum, "MILLISECOND")}},
       Int64(1474285998667)},
      {{{max_datetime_micros, min_datetime, Enum(part_enum, "MILLISECOND")}},
       Int64(315537897599999)},

      // MICROSECOND
      {{{DatetimeNanos(2016, 4, 8, 14, 59, 59, 0),
         DatetimeNanos(2016, 4, 8, 14, 59, 59, 123),
         Enum(part_enum, "MICROSECOND")}},
       Int64(0)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 999999),
         DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         Enum(part_enum, "MICROSECOND")}},
       Int64(876543)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 999999),
         DatetimeMicros(2016, 4, 8, 15, 0, 1, 0),
         Enum(part_enum, "MICROSECOND")}},
       Int64(-1000001)},
      {{{DatetimeMicros(2016, 4, 8, 14, 59, 59, 123456),
         DatetimeMicros(2016, 4, 8, 15, 0, 0, 0),
         Enum(part_enum, "MICROSECOND")}},
       Int64(-876544)},
      {{{datetime_micros_1, datetime_micros_2, Enum(part_enum, "MICROSECOND")}},
       Int64(1474285998666667)},
      {{{max_datetime_micros, min_datetime, Enum(part_enum, "MICROSECOND")}},
       Int64(315537897599999999)},

      // NANOSECOND
      {{{DatetimeNanos(2016, 4, 8, 14, 49, 32, 123456789),
         DatetimeNanos(2016, 4, 8, 14, 49, 32, 123456789),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(0)},
      {{{DatetimeNanos(2016, 4, 8, 14, 49, 32, 123456789),
         DatetimeNanos(2016, 4, 8, 14, 49, 32, 0),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(123456789)},
      {{{DatetimeNanos(2016, 4, 8, 14, 49, 32, 123456789),
         DatetimeNanos(2016, 4, 8, 14, 49, 33, 0),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(-876543211)},
      {{{DatetimeNanos(2308, 6, 26, 18, 36, 48, 978231807),
         DatetimeNanos(2016, 3, 16, 18, 49, 32, 123456000),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(std::numeric_limits<int64_t>::max())},
      {{{DatetimeNanos(2016, 3, 16, 18, 49, 32, 123456000),
         DatetimeNanos(2308, 6, 26, 18, 36, 48, 978231808),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /*micros_output */,
       Int64(std::numeric_limits<int64_t>::lowest())},
      {{{DatetimeNanos(2016, 3, 16, 18, 49, 32, 123456000),
         DatetimeNanos(2308, 6, 26, 18, 36, 48, 978231809),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* overflows int64 */,
       Int64Type()},
  });

  // Reuse all test case for date_diff. The time within the day does not really
  // matter in this situation because the part will always be at least DAY.
  for (const auto& test_case : GetFunctionTestsDateDiff()) {
    static TimeValue time_1 = TimeValue::FromHMSAndMicros(7, 8, 9, 123456);
    static TimeValue time_2 = TimeValue::FromHMSAndMicros(17, 18, 19, 456789);

    for (const auto& entry : test_case.params.results()) {
      const QueryParamsWithResult::FeatureSet& required_features = entry.first;
      const Value& result = entry.second.result;

      if (result.is_null()) continue;

      DatetimeValue datetime_1, datetime_2;
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_case.params.param(0).date_value(), time_1, &datetime_1));
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_case.params.param(1).date_value(), time_2, &datetime_2));
      civil_time_test_cases.push_back(
          {{{Datetime(datetime_1), Datetime(datetime_2),
             test_case.params.param(2)}},
           result,
           /*output_type=*/nullptr,
           required_features});

      // Changing the time within the day does not change the result.
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_case.params.param(0).date_value(), time_2, &datetime_1));
      GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
          test_case.params.param(1).date_value(), time_1, &datetime_2));
      civil_time_test_cases.push_back(
          {{{Datetime(datetime_1), Datetime(datetime_2),
             test_case.params.param(2)}},
           result,
           /*output_type=*/nullptr,
           required_features});
    }
  }

  AddSwappedDiffTestCases(&civil_time_test_cases);

  return PrepareCivilTimeTestCaseAsFunctionTestCall(civil_time_test_cases,
                                                    "datetime_diff");
}

// Forward declaration of GetTimeTruncTestCases(). The test cases
// produced by this function will be used to produce some of the test cases in
// GetFunctionTestsDatetimeTrunc().
static std::vector<CivilTimeTestCase> GetTimeTruncTestCases();

std::vector<FunctionTestCall> GetFunctionTestsDatetimeTrunc() {
  const EnumType* part_type;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_type));

  const Value datetime_micros = DatetimeMicros(2016, 4, 21, 11, 43, 27, 987654);
  const Value datetime_nanos =
      DatetimeNanos(2017, 9, 22, 12, 34, 49, 876543210);
  const Value datetime_max_micros =
      DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999);
  const Value datetime_max_nanos =
      DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999999);
  const Value datetime_min = DatetimeMicros(1, 1, 1, 0, 0, 0, 0);

  auto make_test = [part_type](Value datetime, std::string part, Value result) {
    const Value part_value = Enum(part_type, part);
    return CivilTimeTestCase(
        {datetime, part_value}, result, /*output_type=*/nullptr,
        GetFeatureSetForDateTimestampPart(part_value.enum_value()));
  };

  auto make_error_test = [part_type](Value datetime, std::string part) {
    const Value part_value = Enum(part_type, part);
    return CivilTimeTestCase(
        {datetime, part_value}, FunctionEvalError(), DatetimeType(),
        GetFeatureSetForDateTimestampPart(part_value.enum_value()));
  };

  std::vector<CivilTimeTestCase> civil_time_test_cases{
      // NULL handling
      {{{NullDatetime(), Enum(part_type, "YEAR")}}, NullDatetime()},

      // YEAR
      make_test(datetime_micros, "YEAR",
                DatetimeMicros(2016, 1, 1, 0, 0, 0, 0)),
      make_test(datetime_nanos, "YEAR", DatetimeMicros(2017, 1, 1, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "YEAR",
                DatetimeMicros(9999, 1, 1, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "YEAR",
                DatetimeMicros(9999, 1, 1, 0, 0, 0, 0)),
      make_test(datetime_min, "YEAR", DatetimeMicros(1, 1, 1, 0, 0, 0, 0)),

      // ISOYEAR
      make_test(datetime_micros, "ISOYEAR",
                DatetimeMicros(2016, 1, 4, 0, 0, 0, 0)),
      make_test(datetime_nanos, "ISOYEAR",
                DatetimeMicros(2017, 1, 2, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "ISOYEAR",
                DatetimeMicros(9999, 1, 4, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "ISOYEAR",
                DatetimeMicros(9999, 1, 4, 0, 0, 0, 0)),
      make_test(datetime_min, "ISOYEAR", DatetimeMicros(1, 1, 1, 0, 0, 0, 0)),

      // QUARTER
      make_test(datetime_micros, "QUARTER",
                DatetimeMicros(2016, 4, 1, 0, 0, 0, 0)),
      make_test(datetime_nanos, "QUARTER",
                DatetimeMicros(2017, 7, 1, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "QUARTER",
                DatetimeMicros(9999, 10, 1, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "QUARTER",
                DatetimeMicros(9999, 10, 1, 0, 0, 0, 0)),
      make_test(datetime_min, "QUARTER", DatetimeMicros(1, 1, 1, 0, 0, 0, 0)),

      // MONTH
      make_test(datetime_micros, "MONTH",
                DatetimeMicros(2016, 4, 1, 0, 0, 0, 0)),
      make_test(datetime_nanos, "MONTH",
                DatetimeMicros(2017, 9, 1, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "MONTH",
                DatetimeMicros(9999, 12, 1, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "MONTH",
                DatetimeMicros(9999, 12, 1, 0, 0, 0, 0)),
      make_test(datetime_min, "MONTH", DatetimeMicros(1, 1, 1, 0, 0, 0, 0)),

      // WEEK, which means the beginning of Sunday no later than the
      // datetime.
      make_test(datetime_micros, "WEEK",
                DatetimeMicros(2016, 4, 17, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK",
                DatetimeMicros(2017, 9, 17, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK",
                DatetimeMicros(9999, 12, 26, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK",
                DatetimeMicros(9999, 12, 26, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so the Sunday before
      // that will be out-of-range.
      make_error_test(datetime_min, "WEEK"),
      // 2016-04-24 is a Sunday, so truncating to WEEK will return the beginning
      // of the same day.
      make_test(DatetimeMicros(2016, 4, 24, 12, 34, 56, 789123), "WEEK",
                DatetimeMicros(2016, 4, 24, 0, 0, 0, 0)),
      // 2016-03-04 is a Friday. The Sunday before it is 2016-02-28.
      make_test(DatetimeMicros(2016, 3, 4, 12, 34, 56, 789123), "WEEK",
                DatetimeMicros(2016, 2, 28, 0, 0, 0, 0)),
      // 2015-01-03 is a Saturday. The Sunday before it is 2014-12-28.
      make_test(DatetimeMicros(2015, 1, 3, 12, 34, 56, 789123), "WEEK",
                DatetimeMicros(2014, 12, 28, 0, 0, 0, 0)),

      // ISOWEEK (same as WEEK_MONDAY)
      make_test(datetime_micros, "ISOWEEK",
                DatetimeMicros(2016, 4, 18, 0, 0, 0, 0)),
      make_test(datetime_nanos, "ISOWEEK",
                DatetimeMicros(2017, 9, 18, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "ISOWEEK",
                DatetimeMicros(9999, 12, 27, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "ISOWEEK",
                DatetimeMicros(9999, 12, 27, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so it is impossible
      // to get an out-of-range value by truncating to ISOWEEK.
      make_test(datetime_min, "ISOWEEK", datetime_min),
      // 2016-04-25 is a Monday, so truncating to ISOWEEK will return the
      // beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 25, 12, 34, 56, 789123), "ISOWEEK",
                DatetimeMicros(2016, 4, 25, 0, 0, 0, 0)),
      // 2016-03-04 is a Friday.
      make_test(DatetimeMicros(2016, 3, 4, 12, 34, 56, 789123), "ISOWEEK",
                DatetimeMicros(2016, 2, 29, 0, 0, 0, 0)),
      // 2015-01-03 is a Saturday.
      make_test(DatetimeMicros(2015, 1, 3, 12, 34, 56, 789123), "ISOWEEK",
                DatetimeMicros(2014, 12, 29, 0, 0, 0, 0)),

      // WEEK_MONDAY
      make_test(datetime_micros, "WEEK_MONDAY",
                DatetimeMicros(2016, 4, 18, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK_MONDAY",
                DatetimeMicros(2017, 9, 18, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK_MONDAY",
                DatetimeMicros(9999, 12, 27, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK_MONDAY",
                DatetimeMicros(9999, 12, 27, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so it is impossible
      // to get an out-of-range value by truncating to WEEK_MONDAY.
      make_test(datetime_min, "WEEK_MONDAY", datetime_min),
      // 2016-04-25 is a Monday, so truncating to WEEK_MONDAY will return the
      // beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 25, 12, 34, 56, 789123), "WEEK_MONDAY",
                DatetimeMicros(2016, 4, 25, 0, 0, 0, 0)),
      // 2016-03-04 is a Friday.
      make_test(DatetimeMicros(2016, 3, 4, 12, 34, 56, 789123), "WEEK_MONDAY",
                DatetimeMicros(2016, 2, 29, 0, 0, 0, 0)),
      // 2015-01-03 is a Saturday.
      make_test(DatetimeMicros(2015, 1, 3, 12, 34, 56, 789123), "WEEK_MONDAY",
                DatetimeMicros(2014, 12, 29, 0, 0, 0, 0)),

      // WEEK_TUESDAY
      make_test(datetime_micros, "WEEK_TUESDAY",
                DatetimeMicros(2016, 4, 19, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK_TUESDAY",
                DatetimeMicros(2017, 9, 19, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK_TUESDAY",
                DatetimeMicros(9999, 12, 28, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK_TUESDAY",
                DatetimeMicros(9999, 12, 28, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so the Tuesday before
      // that is out of range.
      make_error_test(datetime_min, "WEEK_TUESDAY"),
      // 2016-04-26 is a Tuesday, so truncating to WEEK_TUESDAY will return the
      // beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 26, 12, 34, 56, 789123), "WEEK_TUESDAY",
                DatetimeMicros(2016, 4, 26, 0, 0, 0, 0)),
      // 2016-03-04 is a Friday.
      make_test(DatetimeMicros(2016, 3, 4, 12, 34, 56, 789123), "WEEK_TUESDAY",
                DatetimeMicros(2016, 3, 1, 0, 0, 0, 0)),
      // 2015-01-03 is a Saturday.
      make_test(DatetimeMicros(2015, 1, 3, 12, 34, 56, 789123), "WEEK_TUESDAY",
                DatetimeMicros(2014, 12, 30, 0, 0, 0, 0)),

      // WEEK_WEDNESDAY
      make_test(datetime_micros, "WEEK_WEDNESDAY",
                DatetimeMicros(2016, 4, 20, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK_WEDNESDAY",
                DatetimeMicros(2017, 9, 20, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK_WEDNESDAY",
                DatetimeMicros(9999, 12, 29, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK_WEDNESDAY",
                DatetimeMicros(9999, 12, 29, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so the Wednesday
      // before that is out of range.
      make_error_test(datetime_min, "WEEK_WEDNESDAY"),
      // 2016-04-27 is a Wednesday, so truncating to WEEK_WEDNESDAY will return
      // the beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 27, 12, 34, 56, 789123),
                "WEEK_WEDNESDAY", DatetimeMicros(2016, 4, 27, 0, 0, 0, 0)),
      // 2016-03-04 is a Friday.
      make_test(DatetimeMicros(2016, 3, 4, 12, 34, 56, 789123),
                "WEEK_WEDNESDAY", DatetimeMicros(2016, 3, 2, 0, 0, 0, 0)),
      // 2015-01-03 is a Saturday.
      make_test(DatetimeMicros(2015, 1, 3, 12, 34, 56, 789123),
                "WEEK_WEDNESDAY", DatetimeMicros(2014, 12, 31, 0, 0, 0, 0)),

      // WEEK_THURSDAY
      make_test(datetime_micros, "WEEK_THURSDAY",
                DatetimeMicros(2016, 4, 21, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK_THURSDAY",
                DatetimeMicros(2017, 9, 21, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK_THURSDAY",
                DatetimeMicros(9999, 12, 30, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK_THURSDAY",
                DatetimeMicros(9999, 12, 30, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so the Thursday
      // before that is out of range.
      make_error_test(datetime_min, "WEEK_THURSDAY"),
      // 2016-04-28 is a Thursday, so truncating to WEEK_THURSDAY will return
      // the beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 28, 12, 34, 56, 789123),
                "WEEK_THURSDAY", DatetimeMicros(2016, 4, 28, 0, 0, 0, 0)),
      // 2016-03-04 is a Friday.
      make_test(DatetimeMicros(2016, 3, 4, 12, 34, 56, 789123), "WEEK_THURSDAY",
                DatetimeMicros(2016, 3, 3, 0, 0, 0, 0)),
      // 2015-01-03 is a Saturday.
      make_test(DatetimeMicros(2015, 1, 3, 12, 34, 56, 789123), "WEEK_THURSDAY",
                DatetimeMicros(2015, 1, 1, 0, 0, 0, 0)),

      // WEEK_FRIDAY
      make_test(datetime_micros, "WEEK_FRIDAY",
                DatetimeMicros(2016, 4, 15, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK_FRIDAY",
                DatetimeMicros(2017, 9, 22, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK_FRIDAY",
                DatetimeMicros(9999, 12, 31, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK_FRIDAY",
                DatetimeMicros(9999, 12, 31, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so the Friday
      // before that is out of range.
      make_error_test(datetime_min, "WEEK_FRIDAY"),
      // 2016-04-29 is a Friday, so truncating to WEEK_FRIDAY will return
      // the beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 29, 12, 34, 56, 789123), "WEEK_FRIDAY",
                DatetimeMicros(2016, 4, 29, 0, 0, 0, 0)),
      // 2016-03-05 is a Saturday.
      make_test(DatetimeMicros(2016, 3, 5, 12, 34, 56, 789123), "WEEK_FRIDAY",
                DatetimeMicros(2016, 3, 4, 0, 0, 0, 0)),
      // 2015-01-04 is a Sunday.
      make_test(DatetimeMicros(2015, 1, 4, 12, 34, 56, 789123), "WEEK_FRIDAY",
                DatetimeMicros(2015, 1, 2, 0, 0, 0, 0)),

      // WEEK_SATURDAY
      make_test(datetime_micros, "WEEK_SATURDAY",
                DatetimeMicros(2016, 4, 16, 0, 0, 0, 0)),
      make_test(datetime_nanos, "WEEK_SATURDAY",
                DatetimeMicros(2017, 9, 16, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "WEEK_SATURDAY",
                DatetimeMicros(9999, 12, 25, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "WEEK_SATURDAY",
                DatetimeMicros(9999, 12, 25, 0, 0, 0, 0)),
      // 0001-01-01 is a Monday on the Gregorian calendar, so the Saturday
      // before that is out of range.
      make_error_test(datetime_min, "WEEK_SATURDAY"),
      // 2016-04-30 is a Saturday, so truncating to WEEK_SATURDAY will return
      // the beginning of the same day.
      make_test(DatetimeMicros(2016, 4, 30, 12, 34, 56, 789123),
                "WEEK_SATURDAY", DatetimeMicros(2016, 4, 30, 0, 0, 0, 0)),
      // 2016-03-06 is a Sunday.
      make_test(DatetimeMicros(2016, 3, 6, 12, 34, 56, 789123), "WEEK_SATURDAY",
                DatetimeMicros(2016, 3, 5, 0, 0, 0, 0)),
      // 2015-01-05 is a Monday.
      make_test(DatetimeMicros(2015, 1, 5, 12, 34, 56, 789123), "WEEK_SATURDAY",
                DatetimeMicros(2015, 1, 3, 0, 0, 0, 0)),

      // DAY
      make_test(datetime_micros, "DAY",
                DatetimeMicros(2016, 4, 21, 0, 0, 0, 0)),
      make_test(datetime_nanos, "DAY", DatetimeMicros(2017, 9, 22, 0, 0, 0, 0)),
      make_test(datetime_max_micros, "DAY",
                DatetimeMicros(9999, 12, 31, 0, 0, 0, 0)),
      make_test(datetime_max_nanos, "DAY",
                DatetimeMicros(9999, 12, 31, 0, 0, 0, 0)),
      make_test(datetime_min, "DAY", DatetimeMicros(1, 1, 1, 0, 0, 0, 0))};

  // Reuse the test case for time_trunc for date parts smaller than DAY.
  // Given a Time, when it was associated with an arbitrary Date to construct a
  // Datetime, and truncate to a date part that's smaller than DAY, the result
  // Datetime will be the same as truncating the Time first and associating it
  // with the same Date.
  for (const auto& test_case : GetTimeTruncTestCases()) {
    static const std::vector<int32_t> test_dates = {
        DateFromStr("2016-04-21").date_value(),
        DateFromStr("9999-12-31").date_value(),
        DateFromStr("0001-01-01").date_value(),
    };

    for (int32_t date : test_dates) {
      Value input;
      if (test_case.input[0].get().is_null()) {
        input = NullDatetime();
      } else {
        DatetimeValue datetime;
        GOOGLESQL_CHECK_OK(functions::ConstructDatetime(
            date, test_case.input[0].get().time_value(), &datetime));
        input = Datetime(datetime);
      }

      auto BuildTestCaseOutput = [](int32_t date,
                                    const absl::StatusOr<Value>& time_output) {
        if (time_output.ok()) {
          const Value& time_value = time_output.value();
          if (time_value.is_null()) {
            return absl::StatusOr<Value>(NullDatetime());
          } else {
            DatetimeValue datetime;
            GOOGLESQL_CHECK_OK(functions::ConstructDatetime(date, time_value.time_value(),
                                                  &datetime));
            return absl::StatusOr<Value>(Datetime(datetime));
          }
        } else {
          return time_output;
        }
      };

      civil_time_test_cases.push_back(
          {{{input, test_case.input[1].get() /* date part */}},
           BuildTestCaseOutput(date, test_case.micros_output),
           BuildTestCaseOutput(date, test_case.nanos_output),
           DatetimeType()});
    }
  }

  return PrepareCivilTimeTestCaseAsFunctionTestCall(civil_time_test_cases,
                                                    "datetime_trunc");
}

static std::vector<CivilTimeTestCase> GetFunctionTestsTimeAdd() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  // Shared by micros and nanos precision.
  static const Value midnight = TimeMicros(0, 0, 0, 0);
  // Test values for micros precision.
  static const Value before_midnight_micros = TimeMicros(23, 59, 59, 999999);
  static const Value time_micros = TimeMicros(12, 34, 56, 654321);
  // Test values for nanos precision.
  static const Value before_midnight_nanos = TimeNanos(23, 59, 59, 999999999);
  static const Value time_nanos = TimeNanos(12, 34, 56, 987654321);

  std::vector<CivilTimeTestCase> civil_time_test_cases({
      // NULL handling
      {{{NullTime(), Int64(1), Enum(part_enum, "SECOND")}}, NullTime()},
      {{{time_micros, NullInt64(), Enum(part_enum, "SECOND")}}, NullTime()},
      {{{NullTime(), NullInt64(), Enum(part_enum, "SECOND")}}, NullTime()},

      // HOUR
      {{{midnight, Int64(1), Enum(part_enum, "HOUR")}}, TimeMicros(1, 0, 0, 0)},
      {{{midnight, Int64(-1), Enum(part_enum, "HOUR")}},
       TimeMicros(23, 0, 0, 0)},
      {{{midnight, Int64(30), Enum(part_enum, "HOUR")}},
       TimeMicros(6, 0, 0, 0)},
      {{{midnight, Int64(-30), Enum(part_enum, "HOUR")}},
       TimeMicros(18, 0, 0, 0)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(7, 0, 0, 0)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(16, 0, 0, 0)},

      {{{before_midnight_micros, Int64(1), Enum(part_enum, "HOUR")}},
       TimeMicros(0, 59, 59, 999999)},
      {{{before_midnight_micros, Int64(-1), Enum(part_enum, "HOUR")}},
       TimeMicros(22, 59, 59, 999999)},
      {{{before_midnight_micros, Int64(30), Enum(part_enum, "HOUR")}},
       TimeMicros(5, 59, 59, 999999)},
      {{{before_midnight_micros, Int64(-30), Enum(part_enum, "HOUR")}},
       TimeMicros(17, 59, 59, 999999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(6, 59, 59, 999999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(15, 59, 59, 999999)},

      {{{time_micros, Int64(1), Enum(part_enum, "HOUR")}},
       TimeMicros(13, 34, 56, 654321)},
      {{{time_micros, Int64(-1), Enum(part_enum, "HOUR")}},
       TimeMicros(11, 34, 56, 654321)},
      {{{time_micros, Int64(30), Enum(part_enum, "HOUR")}},
       TimeMicros(18, 34, 56, 654321)},
      {{{time_micros, Int64(-30), Enum(part_enum, "HOUR")}},
       TimeMicros(6, 34, 56, 654321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(19, 34, 56, 654321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(4, 34, 56, 654321)},

      // MINUTE
      {{{midnight, Int64(1), Enum(part_enum, "MINUTE")}},
       TimeMicros(0, 1, 0, 0)},
      {{{midnight, Int64(-1), Enum(part_enum, "MINUTE")}},
       TimeMicros(23, 59, 0, 0)},
      {{{midnight, Int64(70), Enum(part_enum, "MINUTE")}},
       TimeMicros(1, 10, 0, 0)},
      {{{midnight, Int64(-70), Enum(part_enum, "MINUTE")}},
       TimeMicros(22, 50, 0, 0)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(18, 7, 0, 0)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(5, 52, 0, 0)},

      {{{before_midnight_micros, Int64(1), Enum(part_enum, "MINUTE")}},
       TimeMicros(0, 0, 59, 999999)},
      {{{before_midnight_micros, Int64(-1), Enum(part_enum, "MINUTE")}},
       TimeMicros(23, 58, 59, 999999)},
      {{{before_midnight_micros, Int64(70), Enum(part_enum, "MINUTE")}},
       TimeMicros(1, 9, 59, 999999)},
      {{{before_midnight_micros, Int64(-70), Enum(part_enum, "MINUTE")}},
       TimeMicros(22, 49, 59, 999999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(18, 6, 59, 999999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(5, 51, 59, 999999)},

      {{{time_micros, Int64(1), Enum(part_enum, "MINUTE")}},
       TimeMicros(12, 35, 56, 654321)},
      {{{time_micros, Int64(-1), Enum(part_enum, "MINUTE")}},
       TimeMicros(12, 33, 56, 654321)},
      {{{time_micros, Int64(70), Enum(part_enum, "MINUTE")}},
       TimeMicros(13, 44, 56, 654321)},
      {{{time_micros, Int64(-70), Enum(part_enum, "MINUTE")}},
       TimeMicros(11, 24, 56, 654321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(6, 41, 56, 654321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(18, 26, 56, 654321)},

      // SECOND
      {{{midnight, Int64(1), Enum(part_enum, "SECOND")}},
       TimeMicros(0, 0, 1, 0)},
      {{{midnight, Int64(-1), Enum(part_enum, "SECOND")}},
       TimeMicros(23, 59, 59, 0)},
      {{{midnight, Int64(70), Enum(part_enum, "SECOND")}},
       TimeMicros(0, 1, 10, 0)},
      {{{midnight, Int64(-70), Enum(part_enum, "SECOND")}},
       TimeMicros(23, 58, 50, 0)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(15, 30, 7, 0)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(8, 29, 52, 0)},

      {{{before_midnight_micros, Int64(1), Enum(part_enum, "SECOND")}},
       TimeMicros(0, 0, 0, 999999)},
      {{{before_midnight_micros, Int64(-1), Enum(part_enum, "SECOND")}},
       TimeMicros(23, 59, 58, 999999)},
      {{{before_midnight_micros, Int64(70), Enum(part_enum, "SECOND")}},
       TimeMicros(0, 1, 9, 999999)},
      {{{before_midnight_micros, Int64(-70), Enum(part_enum, "SECOND")}},
       TimeMicros(23, 58, 49, 999999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(15, 30, 6, 999999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(8, 29, 51, 999999)},

      {{{time_micros, Int64(1), Enum(part_enum, "SECOND")}},
       TimeMicros(12, 34, 57, 654321)},
      {{{time_micros, Int64(-1), Enum(part_enum, "SECOND")}},
       TimeMicros(12, 34, 55, 654321)},
      {{{time_micros, Int64(70), Enum(part_enum, "SECOND")}},
       TimeMicros(12, 36, 6, 654321)},
      {{{time_micros, Int64(-70), Enum(part_enum, "SECOND")}},
       TimeMicros(12, 33, 46, 654321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(4, 5, 3, 654321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(21, 4, 48, 654321)},

      // MILLISECOND
      {{{midnight, Int64(1), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(0, 0, 0, 1000)},
      {{{midnight, Int64(-1), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(23, 59, 59, 999000)},
      {{{midnight, Int64(9876), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(0, 0, 9, 876000)},
      {{{midnight, Int64(-9876), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(23, 59, 50, 124000)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(7, 12, 55, 807000)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(16, 47, 4, 192000)},

      {{{before_midnight_micros, Int64(1), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(0, 0, 0, 999)},
      {{{before_midnight_micros, Int64(-1), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(23, 59, 59, 998999)},
      {{{before_midnight_micros, Int64(9876), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(0, 0, 9, 875999)},
      {{{before_midnight_micros, Int64(-9876), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(23, 59, 50, 123999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(7, 12, 55, 806999)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(16, 47, 4, 191999)},

      {{{time_micros, Int64(1), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(12, 34, 56, 655321)},
      {{{time_micros, Int64(-1), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(12, 34, 56, 653321)},
      {{{time_micros, Int64(9876), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(12, 35, 6, 530321)},
      {{{time_micros, Int64(-9876), Enum(part_enum, "MILLISECOND")}},
       TimeMicros(12, 34, 46, 778321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(19, 47, 52, 461321)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(5, 22, 0, 846321)},

      // MICROSECOND
      {{{midnight, Int64(1), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(0, 0, 0, 1)},
      {{{midnight, Int64(-1), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(23, 59, 59, 999999)},
      {{{midnight, Int64(987654321), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(0, 16, 27, 654321)},
      {{{midnight, Int64(-987654321), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(23, 43, 32, 345679)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(4, 0, 54, 775807)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(19, 59, 5, 224192)},

      {{{before_midnight_micros, Int64(1), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(0, 0, 0, 0)},
      {{{before_midnight_micros, Int64(-1), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(23, 59, 59, 999998)},
      {{{before_midnight_micros, Int64(987654321),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(0, 16, 27, 654320)},
      {{{before_midnight_micros, Int64(-987654321),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(23, 43, 32, 345678)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(4, 0, 54, 775806)},
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(19, 59, 5, 224191)},

      {{{time_micros, Int64(1), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(12, 34, 56, 654322)},
      {{{time_micros, Int64(-1), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(12, 34, 56, 654320)},
      {{{time_micros, Int64(987654321), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(12, 51, 24, 308642)},
      {{{time_micros, Int64(-987654321), Enum(part_enum, "MICROSECOND")}},
       TimeMicros(12, 18, 29, 0)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(16, 35, 51, 430128)},
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(8, 34, 1, 878513)},

      // NANOSECOND
      {{{midnight, Int64(1), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 0, 0, 1)},
      {{{midnight, Int64(-1), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 59, 59, 999999999)},
      {{{midnight, Int64(123456789123), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 2, 3, 456789123)},
      {{{midnight, Int64(-123456789123), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 57, 56, 543210877)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 47, 16, 854775807)},
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 12, 43, 145224192)},

      {{{before_midnight_nanos, Int64(1), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 0, 0, 0)},
      {{{before_midnight_nanos, Int64(-1), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 59, 59, 999999998)},
      {{{before_midnight_nanos, Int64(123456789123),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 2, 3, 456789122)},
      {{{before_midnight_nanos, Int64(-123456789123),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 57, 56, 543210876)},
      {{{before_midnight_nanos, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 47, 16, 854775806)},
      {{{before_midnight_nanos, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 12, 43, 145224191)},

      {{{time_nanos, Int64(1), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 34, 56, 987654322)},
      {{{time_nanos, Int64(-1), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 34, 56, 987654320)},
      {{{time_nanos, Int64(123456789123), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 37, 0, 444443444)},
      {{{time_nanos, Int64(-123456789123), Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 32, 53, 530865198)},
      {{{time_nanos, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 22, 13, 842430128)},
      {{{time_nanos, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 47, 40, 132878513)},
  });
  return civil_time_test_cases;
}

static std::vector<CivilTimeTestCase> GetFunctionTestsTimeSub() {
  std::vector<CivilTimeTestCase> civil_time_test_cases(
      GetFunctionTestsTimeAdd());
  std::vector<int> index_of_elements_to_remove;
  for (int i = 0; i < civil_time_test_cases.size(); ++i) {
    auto& test_case = civil_time_test_cases[i];
    const auto& arguments = test_case.input;
    if (!arguments[1].get().is_null()) {
      if (arguments[1].get().int64_value() !=
          std::numeric_limits<int64_t>::lowest()) {
        // Adding an <interval> to a time should produce the same result as
        // subtracting (-1 * <interval>) from the same time.
        std::vector<ValueConstructor> new_arguments(
            {arguments[0], Int64(-1 * arguments[1].get().int64_value()),
             arguments[2]});
        test_case.input.swap(new_arguments);
      } else {
        // For cases with interval == std::numeric_limits<int64>::lowest(), -1 *
        // interval will overflow int64, which is not a valid input for
        // time_sub(). These test cases will be removed.
        index_of_elements_to_remove.push_back(i);
      }
    }
  }
  for (auto itr = index_of_elements_to_remove.rbegin();
       itr != index_of_elements_to_remove.rend(); ++itr) {
    civil_time_test_cases.erase(civil_time_test_cases.begin() + *itr);
  }

  // Add all std::numeric_limits<int64>::lowest() and
  // std::numeric_limits<int64>::max() test cases
  // time_sub(std::numeric_limits<int64>::lowest()) == time_add(-kint64min) ==
  // time_add(std::numeric_limits<int64>::max() + 1)
  // time_sub(std::numeric_limits<int64>::max()) == time_add(-kint64max) ==
  // time_add(std::numeric_limits<int64>::lowest() + 1)
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));
  // Shared by micros and nanos precision.
  static const Value midnight = TimeMicros(0, 0, 0, 0);
  // Test values for micros precision.
  static const Value before_midnight_micros = TimeMicros(23, 59, 59, 999999);
  static const Value time_micros = TimeMicros(12, 34, 56, 654321);
  // Test values for nanos precision.
  static const Value before_midnight_nanos = TimeNanos(23, 59, 59, 999999999);
  static const Value time_nanos = TimeNanos(12, 34, 56, 987654321);

  // HOUR
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(8, 0, 0, 0)});
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(17, 0, 0, 0)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(7, 59, 59, 999999)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(16, 59, 59, 999999)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(20, 34, 56, 654321)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "HOUR")}},
       TimeMicros(5, 34, 56, 654321)});

  // MINUTE
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(5, 53, 0, 0)});
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(18, 8, 0, 0)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(5, 52, 59, 999999)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(18, 7, 59, 999999)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(18, 27, 56, 654321)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MINUTE")}},
       TimeMicros(6, 42, 56, 654321)});

  // SECOND
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(8, 29, 53, 0)});
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(15, 30, 8, 0)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(8, 29, 52, 999999)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(15, 30, 7, 999999)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(21, 4, 49, 654321)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "SECOND")}},
       TimeMicros(4, 5, 4, 654321)});

  // MILLISECOND
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(16, 47, 4, 193000)});
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(7, 12, 55, 808000)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(16, 47, 4, 192999)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(7, 12, 55, 807999)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(5, 22, 0, 847321)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MILLISECOND")}},
       TimeMicros(19, 47, 52, 462321)});

  // MICROSECOND
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(19, 59, 5, 224193)});
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(4, 0, 54, 775808)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(19, 59, 5, 224192)});
  civil_time_test_cases.push_back(
      {{{before_midnight_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(4, 0, 54, 775807)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(8, 34, 1, 878514)});
  civil_time_test_cases.push_back(
      {{{time_micros, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "MICROSECOND")}},
       TimeMicros(16, 35, 51, 430129)});

  // NANOSECOND
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 12, 43, 145224193)});
  civil_time_test_cases.push_back(
      {{{midnight, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 47, 16, 854775808)});
  civil_time_test_cases.push_back(
      {{{before_midnight_nanos, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(0, 12, 43, 145224192)});
  civil_time_test_cases.push_back(
      {{{before_midnight_nanos, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 47, 16, 854775807)});
  civil_time_test_cases.push_back(
      {{{time_nanos, Int64(std::numeric_limits<int64_t>::max()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 47, 40, 132878514)});
  civil_time_test_cases.push_back(
      {{{time_nanos, Int64(std::numeric_limits<int64_t>::lowest()),
         Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 22, 13, 842430129)});

  return civil_time_test_cases;
}

std::vector<FunctionTestCall> GetFunctionTestsTimeAddSub() {
  return ConcatTests<FunctionTestCall>({
      PrepareCivilTimeTestCaseAsFunctionTestCall(GetFunctionTestsTimeAdd(),
                                                 "time_add"),
      PrepareCivilTimeTestCaseAsFunctionTestCall(GetFunctionTestsTimeSub(),
                                                 "time_sub"),
  });
}

std::vector<FunctionTestCall> GetFunctionTestsTimeDiff() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  static const Value time_micros_1 = TimeMicros(12, 34, 56, 987654);
  static const Value time_micros_2 = TimeMicros(2, 56, 13, 456789);
  static const Value time_nanos_1 = TimeNanos(12, 34, 56, 987654321);
  static const Value time_nanos_2 = TimeNanos(2, 56, 13, 456789123);
  static const Value before_midnight_micros = TimeMicros(23, 59, 59, 999999);
  static const Value before_midnight_nanos = TimeNanos(23, 59, 59, 999999999);
  static const Value midnight = TimeMicros(0, 0, 0, 0);

  std::vector<CivilTimeTestCase> civil_time_test_cases({
      // NULL handling
      {{{NullTime(), time_micros_2, Enum(part_enum, "HOUR")}}, NullInt64()},
      {{{time_micros_1, NullTime(), Enum(part_enum, "HOUR")}}, NullInt64()},
      {{{NullTime(), NullTime(), Enum(part_enum, "HOUR")}}, NullInt64()},

      // HOUR
      {{{time_micros_1, time_micros_2, Enum(part_enum, "HOUR")}}, Int64(10)},
      {{{time_micros_1, midnight, Enum(part_enum, "HOUR")}}, Int64(12)},
      {{{time_micros_2, midnight, Enum(part_enum, "HOUR")}}, Int64(2)},
      {{{before_midnight_micros, midnight, Enum(part_enum, "HOUR")}},
       Int64(23)},

      // MINUTE
      {{{time_micros_1, time_micros_2, Enum(part_enum, "MINUTE")}}, Int64(578)},
      {{{time_micros_1, midnight, Enum(part_enum, "MINUTE")}}, Int64(754)},
      {{{time_micros_2, midnight, Enum(part_enum, "MINUTE")}}, Int64(176)},
      {{{before_midnight_micros, midnight, Enum(part_enum, "MINUTE")}},
       Int64(1439)},

      // SECOND
      {{{time_micros_1, time_micros_2, Enum(part_enum, "SECOND")}},
       Int64(34723)},
      {{{time_micros_1, midnight, Enum(part_enum, "SECOND")}}, Int64(45296)},
      {{{time_micros_2, midnight, Enum(part_enum, "SECOND")}}, Int64(10573)},
      {{{before_midnight_micros, midnight, Enum(part_enum, "SECOND")}},
       Int64(86399)},

      // MILLISECOND
      {{{time_micros_1, time_micros_2, Enum(part_enum, "MILLISECOND")}},
       Int64(34723531)},
      {{{time_micros_1, midnight, Enum(part_enum, "MILLISECOND")}},
       Int64(45296987)},
      {{{time_micros_2, midnight, Enum(part_enum, "MILLISECOND")}},
       Int64(10573456)},
      {{{before_midnight_micros, midnight, Enum(part_enum, "MILLISECOND")}},
       Int64(86399999)},

      // MICROSECOND
      {{{time_micros_1, time_micros_2, Enum(part_enum, "MICROSECOND")}},
       Int64(34723530865)},
      {{{time_micros_1, midnight, Enum(part_enum, "MICROSECOND")}},
       Int64(45296987654)},
      {{{time_micros_2, midnight, Enum(part_enum, "MICROSECOND")}},
       Int64(10573456789)},
      {{{before_midnight_micros, midnight, Enum(part_enum, "MICROSECOND")}},
       Int64(86399999999)},

      // NANOSECOND
      {{{time_nanos_1, time_nanos_2, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(34723530865198)},
      {{{time_nanos_1, midnight, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(45296987654321)},
      {{{time_nanos_2, midnight, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(10573456789123)},
      {{{before_midnight_nanos, midnight, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       Int64(86399999999999)},
  });

  AddSwappedDiffTestCases(&civil_time_test_cases);

  return PrepareCivilTimeTestCaseAsFunctionTestCall(civil_time_test_cases,
                                                    "time_diff");
}

static std::vector<CivilTimeTestCase> GetTimeTruncTestCases() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  static const Value time_micros = TimeMicros(12, 34, 56, 876543);
  static const Value time_nanos = TimeNanos(21, 43, 57, 987654321);
  static const Value before_midnight_micros = TimeMicros(23, 59, 59, 999999);
  static const Value before_midnight_nanos = TimeNanos(23, 59, 59, 999999999);
  static const Value midnight = TimeMicros(0, 0, 0, 0);

  return std::vector<CivilTimeTestCase>({
      // NULL handling
      {{{NullTime(), Enum(part_enum, "HOUR")}}, NullTime()},

      // HOUR
      {{{time_micros, Enum(part_enum, "HOUR")}}, TimeMicros(12, 0, 0, 0)},
      {{{time_nanos, Enum(part_enum, "HOUR")}}, TimeMicros(21, 0, 0, 0)},
      {{{before_midnight_micros, Enum(part_enum, "HOUR")}},
       TimeMicros(23, 0, 0, 0)},
      {{{before_midnight_nanos, Enum(part_enum, "HOUR")}},
       TimeMicros(23, 0, 0, 0)},
      {{{midnight, Enum(part_enum, "HOUR")}}, TimeMicros(0, 0, 0, 0)},

      // MINUTE
      {{{time_micros, Enum(part_enum, "MINUTE")}}, TimeMicros(12, 34, 0, 0)},
      {{{time_nanos, Enum(part_enum, "MINUTE")}}, TimeMicros(21, 43, 0, 0)},
      {{{before_midnight_micros, Enum(part_enum, "MINUTE")}},
       TimeMicros(23, 59, 0, 0)},
      {{{before_midnight_nanos, Enum(part_enum, "MINUTE")}},
       TimeMicros(23, 59, 0, 0)},
      {{{midnight, Enum(part_enum, "MINUTE")}}, TimeMicros(0, 0, 0, 0)},

      // SECOND
      {{{time_micros, Enum(part_enum, "SECOND")}}, TimeMicros(12, 34, 56, 0)},
      {{{time_nanos, Enum(part_enum, "SECOND")}}, TimeMicros(21, 43, 57, 0)},
      {{{before_midnight_micros, Enum(part_enum, "SECOND")}},
       TimeMicros(23, 59, 59, 0)},
      {{{before_midnight_nanos, Enum(part_enum, "SECOND")}},
       TimeMicros(23, 59, 59, 0)},
      {{{midnight, Enum(part_enum, "SECOND")}}, TimeMicros(0, 0, 0, 0)},

      // MILLISECOND
      {{{time_micros, Enum(part_enum, "MILLISECOND")}},
       TimeMicros(12, 34, 56, 876000)},
      {{{time_nanos, Enum(part_enum, "MILLISECOND")}},
       TimeMicros(21, 43, 57, 987000)},
      {{{before_midnight_micros, Enum(part_enum, "MILLISECOND")}},
       TimeMicros(23, 59, 59, 999000)},
      {{{before_midnight_nanos, Enum(part_enum, "MILLISECOND")}},
       TimeMicros(23, 59, 59, 999000)},
      {{{midnight, Enum(part_enum, "MILLISECOND")}}, TimeMicros(0, 0, 0, 0)},

      // MICROSECOND
      {{{time_micros, Enum(part_enum, "MICROSECOND")}},
       TimeMicros(12, 34, 56, 876543)},
      {{{time_nanos, Enum(part_enum, "MICROSECOND")}},
       TimeMicros(21, 43, 57, 987654)},
      {{{before_midnight_micros, Enum(part_enum, "MICROSECOND")}},
       TimeMicros(23, 59, 59, 999999)},
      {{{before_midnight_nanos, Enum(part_enum, "MICROSECOND")}},
       TimeMicros(23, 59, 59, 999999)},
      {{{midnight, Enum(part_enum, "MICROSECOND")}}, TimeMicros(0, 0, 0, 0)},

      // NANOSECOND
      {{{time_micros, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(12, 34, 56, 876543000)},
      {{{time_nanos, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(21, 43, 57, 987654321)},
      {{{before_midnight_micros, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 59, 59, 999999000)},
      {{{before_midnight_nanos, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeNanos(23, 59, 59, 999999999)},
      {{{midnight, Enum(part_enum, "NANOSECOND")}},
       FunctionEvalError() /* micros_output */,
       TimeMicros(0, 0, 0, 0)},
  });
}

std::vector<FunctionTestCall> GetFunctionTestsTimeTrunc() {
  return PrepareCivilTimeTestCaseAsFunctionTestCall(GetTimeTruncTestCases(),
                                                    "time_trunc");
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampAdd() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  auto timestamp_add = [part_enum](absl::string_view timestamp,
                                   int64_t interval, absl::string_view part,
                                   absl::string_view result) {
    return FunctionTestCall(
        "timestamp_add",
        {TimestampFromStr(timestamp), Int64(interval), Enum(part_enum, part)},
        TimestampFromStr(result));
  };
  auto timestamp_add_error = [part_enum](absl::string_view timestamp,
                                         int64_t interval,
                                         absl::string_view part) {
    return FunctionTestCall(
        "timestamp_add",
        {TimestampFromStr(timestamp), Int64(interval), Enum(part_enum, part)},
        NullDate(), OUT_OF_RANGE);
  };

  return {
      // NULL handling
      {"timestamp_add",
       {NullTimestamp(), Int64(1), Enum(part_enum, "HOUR")},
       NullTimestamp()},
      {"timestamp_add",
       {TimestampFromStr("2001-01-01"), NullInt64(), Enum(part_enum, "HOUR")},
       NullTimestamp()},
      {"timestamp_add",
       {NullTimestamp(), NullInt64(), Enum(part_enum, "HOUR")},
       NullTimestamp()},
      // This set of tests exercises each of the internal arithmetic overflow
      // checks in the shared AddTimestamp function in
      // googlesql/public/functions
      timestamp_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                          "MICROSECOND"),
      timestamp_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                          "MILLISECOND"),
      timestamp_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                          "SECOND"),
      timestamp_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                          "MINUTE"),
      timestamp_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                          "HOUR"),
      timestamp_add_error("2001-01-01", std::numeric_limits<int64_t>::lowest(),
                          "DAY"),
      // MICROSECOND part
      timestamp_add("2000-01-01 00:11:22.345678", 0, "MICROSECOND",
                    "2000-01-01 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 1, "MICROSECOND",
                    "2000-01-01 00:11:22.345679"),
      timestamp_add("2000-01-01 00:11:22.345678", -1, "MICROSECOND",
                    "2000-01-01 00:11:22.345677"),
      timestamp_add("2000-01-01 00:11:22.345678", 322, "MICROSECOND",
                    "2000-01-01 00:11:22.346"),
      timestamp_add("2000-01-01 00:11:22.345678", -678, "MICROSECOND",
                    "2000-01-01 00:11:22.345"),
      timestamp_add("2015-05-01", 0, "MICROSECOND", "2015-05-01"),
      timestamp_add("2015-05-01", 1, "MICROSECOND",
                    "2015-05-01 00:00:00.000001"),
      timestamp_add("2015-05-01", -1, "MICROSECOND",
                    "2015-04-30 23:59:59.999999"),
      timestamp_add("2015-05-01", 456, "MICROSECOND",
                    "2015-05-01 00:00:00.000456"),
      timestamp_add("2015-12-31 23:59:59.899980", 100020, "MICROSECOND",
                    "2016-01-01"),
      timestamp_add_error("9999-12-31 23:59:59.999999", 1, "MICROSECOND"),
      timestamp_add_error("0001-01-01", -1, "MICROSECOND"),
      // MILLISECOND part
      timestamp_add("2000-01-01 00:11:22.345678", 0, "MILLISECOND",
                    "2000-01-01 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 1, "MILLISECOND",
                    "2000-01-01 00:11:22.346678"),
      timestamp_add("2000-01-01 00:11:22.345678", -1, "MILLISECOND",
                    "2000-01-01 00:11:22.344678"),
      timestamp_add("2000-01-01 00:11:22.345678", 655, "MILLISECOND",
                    "2000-01-01 00:11:23.000678"),
      timestamp_add("2000-01-01 00:11:22.345678", -345, "MILLISECOND",
                    "2000-01-01 00:11:22.000678"),
      timestamp_add_error("9999-12-31 23:59:59.998999", 2, "MILLISECOND"),
      timestamp_add_error("0001-01-01", -2, "MILLISECOND"),
      // SECOND part
      timestamp_add("2000-01-01 00:11:22.345678", 0, "SECOND",
                    "2000-01-01 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 1, "SECOND",
                    "2000-01-01 00:11:23.345678"),
      timestamp_add("2000-01-01 00:11:21.345678", -1, "SECOND",
                    "2000-01-01 00:11:20.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 38, "SECOND",
                    "2000-01-01 00:12:00.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", -22, "SECOND",
                    "2000-01-01 00:11:00.345678"),
      timestamp_add("9999-12-31 23:59:55", 4, "SECOND", "9999-12-31 23:59:59"),
      timestamp_add("0001-01-01 00:00:09", -9, "SECOND", "0001-01-01 00:00:00"),
      timestamp_add_error("9999-12-31 23:59:55", 5, "SECOND"),
      timestamp_add_error("9999-12-31 23:59:55", 6, "SECOND"),
      timestamp_add_error("0001-01-01", -1, "SECOND"),
      timestamp_add_error("0001-01-01 00:00:09", -10, "SECOND"),
      // MINUTE part
      timestamp_add("2000-01-01 00:11:22.345678", 0, "MINUTE",
                    "2000-01-01 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 1, "MINUTE",
                    "2000-01-01 00:12:22.345678"),
      timestamp_add("2000-01-01 00:11:21.345678", -1, "MINUTE",
                    "2000-01-01 00:10:21.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 49, "MINUTE",
                    "2000-01-01 01:00:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", -11, "MINUTE",
                    "2000-01-01 00:00:22.345678"),
      timestamp_add("9999-12-31 23:55:00", 4, "MINUTE", "9999-12-31 23:59:00"),
      timestamp_add("0001-01-01 00:09:00", -9, "MINUTE", "0001-01-01 00:00:00"),
      timestamp_add_error("9999-12-31 23:55:00", 5, "MINUTE"),
      timestamp_add_error("9999-12-31 23:55:00", 6, "MINUTE"),
      timestamp_add_error("0001-01-01", -1, "MINUTE"),
      timestamp_add_error("0001-01-01 00:09:00", -10, "MINUTE"),
      // HOUR part
      timestamp_add("2000-01-01 00:11:22.345678", 0, "HOUR",
                    "2000-01-01 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 1, "HOUR",
                    "2000-01-01 01:11:22.345678"),
      timestamp_add("2000-01-01 00:11:21.345678", -1, "HOUR",
                    "1999-12-31 23:11:21.345678"),
      timestamp_add("2000-01-01 05:11:22.345678", 15, "HOUR",
                    "2000-01-01 20:11:22.345678"),
      timestamp_add("2000-01-01 05:11:22.345678", 49, "HOUR",
                    "2000-01-03 06:11:22.345678"),
      timestamp_add("2000-01-01 06:11:22.345678", -11, "HOUR",
                    "1999-12-31 19:11:22.345678"),
      timestamp_add("9999-12-31 19:00:00", 4, "HOUR", "9999-12-31 23:00:00"),
      timestamp_add("0001-01-01 09:00:00", -9, "HOUR", "0001-01-01 00:00:00"),
      timestamp_add_error("9999-12-31 19:00:00", 5, "HOUR"),
      timestamp_add_error("9999-12-31 19:00:00", 6, "HOUR"),
      timestamp_add_error("0001-01-01", -1, "HOUR"),
      timestamp_add_error("0001-01-01 09:00:00", -10, "HOUR"),
      // DAY part
      timestamp_add("2000-01-01 00:11:22.345678", 0, "DAY",
                    "2000-01-01 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:22.345678", 1, "DAY",
                    "2000-01-02 00:11:22.345678"),
      timestamp_add("2000-01-01 00:11:21.345678", -1, "DAY",
                    "1999-12-31 00:11:21.345678"),
      timestamp_add("2000-01-01 05:11:22.345678", 15, "DAY",
                    "2000-01-16 05:11:22.345678"),
      timestamp_add("2000-01-01 05:11:22.345678", 49, "DAY",
                    "2000-02-19 05:11:22.345678"),
      timestamp_add("2000-01-05 06:11:22.345678", -11, "DAY",
                    "1999-12-25 06:11:22.345678"),
      timestamp_add("9999-12-27 23:59:00", 4, "DAY", "9999-12-31 23:59:00"),
      timestamp_add("0001-01-09 00:00:00", -8, "DAY", "0001-01-01 00:00:00"),
      timestamp_add_error("9999-12-28 00:00:00", 5, "DAY"),
      timestamp_add_error("0001-01-01", -1, "DAY"),

      // Some tests around the DST boundaries.
      // Before 'fall back' boundary for America/Los_Angeles, this corresponds
      // to the 'first' 1:01am.
      timestamp_add("2016-11-06 08:01:01.123400+00", 1, "DAY",
                    "2016-11-07 08:01:01.123400+00"),
      timestamp_add("2016-11-06 08:01:01.123400+00", -1, "DAY",
                    "2016-11-05 08:01:01.123400+00"),
      // Inside 'fall back' boundary for America/Los_Angeles, this corresponds
      // to the 'second' 1:01am.
      timestamp_add("2016-11-06 09:01:01.123400+00", 1, "DAY",
                    "2016-11-07 09:01:01.123400+00"),
      timestamp_add("2016-11-06 09:01:01.123400+00", -1, "DAY",
                    "2016-11-05 09:01:01.123400+00"),
      // Inside 'fall back' boundary for America/Los_Angeles, this corresponds
      // to 2:01am.
      timestamp_add("2016-11-06 10:01:01.123400+00", 1, "DAY",
                    "2016-11-07 10:01:01.123400+00"),
      timestamp_add("2016-11-06 10:01:01.123400+00", -1, "DAY",
                    "2016-11-05 10:01:01.123400+00"),
      // Before 'spring forward' boundary for America/Los_Angeles, this
      // corresponds to 1:01am.
      timestamp_add("2016-03-13 09:01:01.123400+00", 1, "DAY",
                    "2016-03-14 09:01:01.123400+00"),
      timestamp_add("2016-03-13 09:01:01.123400+00", -1, "DAY",
                    "2016-03-12 09:01:01.123400+00"),
      // After 'spring forward' boundary for America/Los_Angeles, this
      // corresponds to 3:01am
      timestamp_add("2016-03-13 10:01:01.123400+00", 1, "DAY",
                    "2016-03-14 10:01:01.123400+00"),
      timestamp_add("2016-03-13 10:01:01.123400+00", -1, "DAY",
                    "2016-03-12 10:01:01.123400+00"),
  };
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampAddAndAliases() {
  std::vector<FunctionTestCall> result;
  for (const FunctionTestCall& test : GetFunctionTestsTimestampAdd()) {
    result.push_back(test);
    result.push_back(FunctionTestCall(
        "date_add",
        test.params.WrapWithFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)));
  }
  return result;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampSub() {
  // Reuse all of the TIMESTAMP_ADD tests but change the function name and
  // invert the interval value.
  std::vector<FunctionTestCall> sub_tests = GetFunctionTestsTimestampAdd();
  for (FunctionTestCall& test_fn : sub_tests) {
    test_fn.function_name = "timestamp_sub";
    if (!test_fn.params.param(1).is_null()) {
      uint64_t interval = test_fn.params.param(1).int64_value();
      *(test_fn.params.mutable_param(1)) = Int64(-1 * interval);
    }
  }
  return sub_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampSubAndAliases() {
  std::vector<FunctionTestCall> result;
  for (const FunctionTestCall& test : GetFunctionTestsTimestampSub()) {
    result.push_back(test);
    result.push_back(FunctionTestCall(
        "date_sub",
        test.params.WrapWithFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)));
  }
  return result;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampAddSub() {
  return ConcatTests<FunctionTestCall>({
      GetFunctionTestsTimestampAddAndAliases(),
      GetFunctionTestsTimestampSubAndAliases(),
  });
}

static std::vector<FunctionTestCall> GetFunctionTestsDateDiff() {
  const EnumType* part_type;
  const google::protobuf::EnumDescriptor* part_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(part_descriptor, &part_type));

  auto date_diff = [part_type](absl::string_view date1, absl::string_view date2,
                               absl::string_view part, int64_t result) {
    const Value part_value = Enum(part_type, part);
    QueryParamsWithResult test_case(
        {DateFromStr(date1), DateFromStr(date2), part_value}, Int64(result));
    test_case.AddRequiredFeatures(
        GetFeatureSetForDateTimestampPart(part_value.enum_value()));
    return FunctionTestCall("date_diff", test_case);
  };

  std::vector<FunctionTestCall> tests({
      {"date_diff",
       {NullDate(), DateFromStr("2001-01-01"), Enum(part_type, "YEAR")},
       NullInt64()},
      {"date_diff",
       {DateFromStr("2001-01-01"), NullDate(), Enum(part_type, "YEAR")},
       NullInt64()},
      {"date_diff",
       {NullDate(), NullDate(), Enum(part_type, "YEAR")},
       NullInt64()},

      date_diff("2001-01-01", "2001-12-31", "YEAR", 0),
      date_diff("2000-01-01", "1999-12-31", "YEAR", 1),
      date_diff("2001-01-01", "2001-01-01", "YEAR", 0),
      date_diff("1678-01-01", "2261-12-31", "YEAR", -583),

      date_diff("2001-01-01", "2001-12-31", "ISOYEAR", -1),
      date_diff("2001-01-01", "2001-12-30", "ISOYEAR", 0),
      date_diff("2000-01-01", "1999-12-31", "ISOYEAR", 0),
      date_diff("2000-01-03", "1999-12-31", "ISOYEAR", 1),
      date_diff("2001-01-01", "2001-01-01", "ISOYEAR", 0),
      date_diff("1678-01-01", "2261-12-31", "ISOYEAR", -585),
      date_diff("9999-12-31", "0001-01-01", "ISOYEAR", 9998),

      date_diff("2000-01-01", "2000-01-01", "MONTH", 0),
      date_diff("2000-01-01", "2000-01-31", "MONTH", 0),
      date_diff("2000-01-01", "2001-01-31", "MONTH", -12),
      date_diff("2000-01-01", "2005-01-30", "MONTH", -60),
      date_diff("2000-02-29", "2001-02-28", "MONTH", -12),
      date_diff("2000-02-29", "2001-03-01", "MONTH", -13),
      date_diff("2261-12-31", "1678-01-01", "MONTH", 583 * 12 + 11),

      date_diff("2000-01-01", "2000-01-01", "QUARTER", 0),
      date_diff("2000-01-01", "2000-03-31", "QUARTER", 0),
      date_diff("2000-01-01", "2000-04-01", "QUARTER", -1),
      date_diff("2000-03-31", "2000-04-01", "QUARTER", -1),
      date_diff("2000-01-01", "2000-12-31", "QUARTER", -3),
      date_diff("2000-01-01", "2001-01-01", "QUARTER", -4),
      date_diff("2261-12-31", "1678-01-01", "QUARTER", (583 * 12 + 11) / 3),

      // 2000-01-01 is a Saturday.
      date_diff("2000-01-01", "2000-01-01", "WEEK", 0),
      date_diff("2000-01-01", "2000-01-03", "WEEK", -1),
      date_diff("2000-01-01", "2000-01-05", "WEEK", -1),
      date_diff("2000-01-07", "2000-01-01", "WEEK", 1),
      date_diff("2000-01-08", "2000-01-01", "WEEK", 1),
      date_diff("2000-01-09", "2000-01-01", "WEEK", 2),
      date_diff("2000-01-01", "2000-01-31", "WEEK", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK", -53),
      date_diff("2001-01-01", "2002-01-01", "WEEK", -52),
      date_diff("9999-12-31", "0001-01-01", "WEEK", 521722),
      date_diff("0001-01-01", "9999-12-31", "WEEK", -521722),

      // 2000-01-02 is a Sunday.
      date_diff("2000-01-02", "2000-01-02", "WEEK_MONDAY", 0),
      date_diff("2000-01-02", "2000-01-04", "WEEK_MONDAY", -1),
      date_diff("2000-01-02", "2000-01-06", "WEEK_MONDAY", -1),
      date_diff("2000-01-08", "2000-01-02", "WEEK_MONDAY", 1),
      date_diff("2000-01-09", "2000-01-02", "WEEK_MONDAY", 1),
      date_diff("2000-01-10", "2000-01-02", "WEEK_MONDAY", 2),
      date_diff("2000-01-02", "2000-02-01", "WEEK_MONDAY", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK_MONDAY", -53),
      date_diff("2001-01-01", "2002-01-01", "WEEK_MONDAY", -52),
      date_diff("9999-12-31", "0001-01-01", "WEEK_MONDAY", 521722),
      date_diff("0001-01-01", "9999-12-31", "WEEK_MONDAY", -521722),

      // 2000-01-03 is a Monday.
      date_diff("2000-01-03", "2000-01-03", "WEEK_TUESDAY", 0),
      date_diff("2000-01-03", "2000-01-05", "WEEK_TUESDAY", -1),
      date_diff("2000-01-03", "2000-01-07", "WEEK_TUESDAY", -1),
      date_diff("2000-01-09", "2000-01-03", "WEEK_TUESDAY", 1),
      date_diff("2000-01-10", "2000-01-03", "WEEK_TUESDAY", 1),
      date_diff("2000-01-11", "2000-01-03", "WEEK_TUESDAY", 2),
      date_diff("2000-01-03", "2000-02-02", "WEEK_TUESDAY", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK_TUESDAY", -52),
      date_diff("2001-01-01", "2002-01-01", "WEEK_TUESDAY", -53),
      date_diff("9999-12-31", "0001-01-01", "WEEK_TUESDAY", 521723),
      date_diff("0001-01-01", "9999-12-31", "WEEK_TUESDAY", -521723),

      // 2000-01-04 is a Tuesday.
      date_diff("2000-01-04", "2000-01-04", "WEEK_WEDNESDAY", 0),
      date_diff("2000-01-04", "2000-01-06", "WEEK_WEDNESDAY", -1),
      date_diff("2000-01-04", "2000-01-08", "WEEK_WEDNESDAY", -1),
      date_diff("2000-01-10", "2000-01-04", "WEEK_WEDNESDAY", 1),
      date_diff("2000-01-11", "2000-01-04", "WEEK_WEDNESDAY", 1),
      date_diff("2000-01-12", "2000-01-04", "WEEK_WEDNESDAY", 2),
      date_diff("2000-01-04", "2000-02-03", "WEEK_WEDNESDAY", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK_WEDNESDAY", -52),
      date_diff("2001-01-01", "2002-01-01", "WEEK_WEDNESDAY", -52),
      date_diff("9999-12-31", "0001-01-01", "WEEK_WEDNESDAY", 521723),
      date_diff("0001-01-01", "9999-12-31", "WEEK_WEDNESDAY", -521723),

      // 2000-01-05 is a Wednesday.
      date_diff("2000-01-05", "2000-01-05", "WEEK_THURSDAY", 0),
      date_diff("2000-01-05", "2000-01-07", "WEEK_THURSDAY", -1),
      date_diff("2000-01-05", "2000-01-09", "WEEK_THURSDAY", -1),
      date_diff("2000-01-11", "2000-01-05", "WEEK_THURSDAY", 1),
      date_diff("2000-01-12", "2000-01-05", "WEEK_THURSDAY", 1),
      date_diff("2000-01-13", "2000-01-05", "WEEK_THURSDAY", 2),
      date_diff("2000-01-05", "2000-02-04", "WEEK_THURSDAY", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK_THURSDAY", -52),
      date_diff("2001-01-01", "2002-01-01", "WEEK_THURSDAY", -52),
      date_diff("9999-12-31", "0001-01-01", "WEEK_THURSDAY", 521723),
      date_diff("0001-01-01", "9999-12-31", "WEEK_THURSDAY", -521723),

      // 2000-01-06 is a Thursday.
      date_diff("2000-01-06", "2000-01-06", "WEEK_FRIDAY", 0),
      date_diff("2000-01-06", "2000-01-08", "WEEK_FRIDAY", -1),
      date_diff("2000-01-06", "2000-01-10", "WEEK_FRIDAY", -1),
      date_diff("2000-01-12", "2000-01-06", "WEEK_FRIDAY", 1),
      date_diff("2000-01-13", "2000-01-06", "WEEK_FRIDAY", 1),
      date_diff("2000-01-14", "2000-01-06", "WEEK_FRIDAY", 2),
      date_diff("2000-01-06", "2000-02-05", "WEEK_FRIDAY", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK_FRIDAY", -52),
      date_diff("2001-01-01", "2002-01-01", "WEEK_FRIDAY", -52),
      date_diff("9999-12-31", "0001-01-01", "WEEK_FRIDAY", 521723),
      date_diff("0001-01-01", "9999-12-31", "WEEK_FRIDAY", -521723),

      // 2000-01-07 is a Friday.
      date_diff("2000-01-07", "2000-01-07", "WEEK_SATURDAY", 0),
      date_diff("2000-01-07", "2000-01-09", "WEEK_SATURDAY", -1),
      date_diff("2000-01-07", "2000-01-11", "WEEK_SATURDAY", -1),
      date_diff("2000-01-13", "2000-01-07", "WEEK_SATURDAY", 1),
      date_diff("2000-01-14", "2000-01-07", "WEEK_SATURDAY", 1),
      date_diff("2000-01-15", "2000-01-07", "WEEK_SATURDAY", 2),
      date_diff("2000-01-07", "2000-02-06", "WEEK_SATURDAY", -5),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "WEEK_SATURDAY", -52),
      date_diff("2001-01-01", "2002-01-01", "WEEK_SATURDAY", -52),
      date_diff("9999-12-31", "0001-01-01", "WEEK_SATURDAY", 521722),
      date_diff("0001-01-01", "9999-12-31", "WEEK_SATURDAY", -521722),

      date_diff("2000-01-01", "2000-01-01", "DAY", 0),
      date_diff("2000-01-01", "2000-01-31", "DAY", -30),
      // Leap year
      date_diff("2000-01-01", "2001-01-01", "DAY", -366),
      date_diff("2001-01-01", "2002-01-01", "DAY", -365),
      date_diff("9999-12-31", "0001-01-01", "DAY",
                googlesql::types::kDateMax - googlesql::types::kDateMin),
  });

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampDiffInternal() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  const Value day(Enum(part_enum, "DAY"));
  const Value hour(Enum(part_enum, "HOUR"));
  const Value minute(Enum(part_enum, "MINUTE"));
  const Value second(Enum(part_enum, "SECOND"));
  const Value millisecond(Enum(part_enum, "MILLISECOND"));
  const Value microsecond(Enum(part_enum, "MICROSECOND"));
  const Value nanosecond(Enum(part_enum, "NANOSECOND"));

  std::vector<FunctionTestCall> v = {
      {"timestamp_diff", {Timestamp(0), Timestamp(0), day}, Int64(0)},
      {"timestamp_diff", {Timestamp(0), Timestamp(0), hour}, Int64(0)},
      {"timestamp_diff", {Timestamp(0), Timestamp(0), minute}, Int64(0)},
      {"timestamp_diff", {Timestamp(0), Timestamp(0), second}, Int64(0)},
      {"timestamp_diff", {Timestamp(0), Timestamp(0), millisecond}, Int64(0)},
      {"timestamp_diff", {Timestamp(0), Timestamp(0), microsecond}, Int64(0)},

      {"timestamp_diff",
       {Timestamp(3600000000 * 24 - 1), Timestamp(0), day},
       Int64(0)},
      {"timestamp_diff",
       {Timestamp(3600000000 * 24), Timestamp(0), day},
       Int64(1)},
      {"timestamp_diff",
       {Timestamp(3600000000 * 24 + 1), Timestamp(0), day},
       Int64(1)},

      {"timestamp_diff",
       {Timestamp(0), Timestamp(3600000000 * 24 - 1), day},
       Int64(0)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(3600000000 * 24), day},
       Int64(-1)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(3600000000 * 24 + 1), day},
       Int64(-1)},

      {"timestamp_diff", {Timestamp(3599999999), Timestamp(0), hour}, Int64(0)},
      {"timestamp_diff", {Timestamp(3600000000), Timestamp(0), hour}, Int64(1)},
      {"timestamp_diff", {Timestamp(3600000001), Timestamp(0), hour}, Int64(1)},

      {"timestamp_diff", {Timestamp(0), Timestamp(3599999999), hour}, Int64(0)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(3600000000), hour},
       Int64(-1)},

      {"timestamp_diff",
       {Timestamp(0), Timestamp(5400000000), hour},
       Int64(-1)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(5400000000), minute},
       Int64(-90)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(5400000000), second},
       Int64(-5400)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(5400000000), millisecond},
       Int64(-5400000)},
      {"timestamp_diff",
       {Timestamp(0), Timestamp(5400000000), microsecond},
       Int64(-5400000000)},

      {"timestamp_diff", {Timestamp(1), Timestamp(3600000000), hour}, Int64(0)},
      {"timestamp_diff",
       {Timestamp(1), Timestamp(3600000000), minute},
       Int64(-59)},
      {"timestamp_diff",
       {Timestamp(1), Timestamp(3600000000), second},
       Int64(-3599)},
      {"timestamp_diff",
       {Timestamp(1), Timestamp(3600000000), millisecond},
       Int64(-3599999)},
      {"timestamp_diff",
       {Timestamp(1), Timestamp(3600000000), microsecond},
       Int64(-3599999999)},

      {"timestamp_diff", {Timestamp(0), Timestamp(999999), second}, Int64(0)},
      {"timestamp_diff", {Timestamp(0), Timestamp(1000000), second}, Int64(-1)},
      {"timestamp_diff", {Timestamp(0), Timestamp(1000001), second}, Int64(-1)},
      {"timestamp_diff",
       {Timestamp(-499999), Timestamp(500000), second},
       Int64(0)},
      {"timestamp_diff",
       {Timestamp(-499999), Timestamp(500001), second},
       Int64(-1)},

      {"timestamp_diff",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), day},
       Int64(-3652058)},
      {"timestamp_diff",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), hour},
       Int64(-87649415)},
      {"timestamp_diff",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), minute},
       Int64(-5258964959)},
      {"timestamp_diff",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), second},
       Int64(-315537897599)},
      {"timestamp_diff",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), millisecond},
       Int64(-315537897599999)},
      {"timestamp_diff",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), microsecond},
       Int64(-315537897599999999)},

      // Test with NULL arguments.
      {"timestamp_diff",
       {NullTimestamp(), Timestamp(0), microsecond},
       NullInt64()},
      {"timestamp_diff",
       {Timestamp(0), NullTimestamp(), microsecond},
       NullInt64()},
  };
  return v;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampDiff() {
  std::vector<FunctionTestCall> result;
  for (const FunctionTestCall& test : GetFunctionTestsTimestampDiffInternal()) {
    result.push_back(test);
    result.push_back(FunctionTestCall(
        "date_diff",
        test.params.WrapWithFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)));
  }
  return result;
}

static Value GetDatePartValue(DateTimestampPart part) {
  return Enum(types::DatePartEnumType(), part);
}
static Value Second() {
  return GetDatePartValue(SECOND);
}
static Value Millisecond() {
  return GetDatePartValue(MILLISECOND);
}
static Value Microsecond() {
  return GetDatePartValue(MICROSECOND);
}

// Helper function for constructing simple TIMESTAMP_TRUNC tests.
// The <timezone> is used both for interpreting the strings
// <timestamp_string> and <expected_result> as timestamps, and
// as the time zone to use for truncation.
FunctionTestCall TimestampTruncTest(const std::string& timestamp_string,
                                    DateTimestampPart part,
                                    const std::string& expected_result,
                                    absl::string_view timezone = "UTC") {
  int64_t timestamp;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(timestamp_string, timezone, kMicroseconds,
                                    &timestamp)) << timestamp_string;
  int64_t expected_timestamp;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(expected_result, timezone, kMicroseconds,
                                    &expected_timestamp))
      << expected_result;
  QueryParamsWithResult test_case(
      {Timestamp(timestamp), GetDatePartValue(part), timezone},
      Timestamp(expected_timestamp));
  test_case.AddRequiredFeatures(GetFeatureSetForDateTimestampPart(part));
  return {"timestamp_trunc", test_case};
}

// Helper function for constructing TIMESTAMP_TRUNC tests for error cases.
// The <timezone> is used both for interpreting the strings <timestamp_string>
// as timestamps and as the time zone to use for truncation.
FunctionTestCall TimestampTruncErrorTest(const std::string& timestamp_string,
                                         DateTimestampPart part,
                                         absl::StatusCode code,
                                         absl::string_view timezone = "UTC") {
  int64_t timestamp;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(timestamp_string, timezone, kMicroseconds,
                                    &timestamp)) << timestamp_string;
  return {"timestamp_trunc",
          {Timestamp(timestamp), GetDatePartValue(part), timezone},
          NullTimestamp(),
          code};
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampTruncInternal() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  std::vector<FunctionTestCall> v = {
      // Truncate all the supported parts at the epoch.
      TimestampTruncTest("1970-01-01 00:00:00", YEAR, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", ISOYEAR, "1969-12-29 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", QUARTER, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", MONTH, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK, "1969-12-28 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", ISOWEEK, "1969-12-29 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK_MONDAY,
                         "1969-12-29 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK_TUESDAY,
                         "1969-12-30 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK_WEDNESDAY,
                         "1969-12-31 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK_THURSDAY,
                         "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK_FRIDAY,
                         "1969-12-26 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", WEEK_SATURDAY,
                         "1969-12-27 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", DAY, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", HOUR, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", MINUTE, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", SECOND, "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", MILLISECOND,
                         "1970-01-01 00:00:00"),
      TimestampTruncTest("1970-01-01 00:00:00", MICROSECOND,
                         "1970-01-01 00:00:00"),

      TimestampTruncTest("1970-01-01 00:00:00.000001", SECOND,
                         "1970-01-01 00:00:00.000000"),
      TimestampTruncTest("1970-01-01 00:00:00.000001", MILLISECOND,
                         "1970-01-01 00:00:00.000000"),
      TimestampTruncTest("1970-01-01 00:00:00.000001", MICROSECOND,
                         "1970-01-01 00:00:00.000001"),

      TimestampTruncTest("1970-01-01 00:00:00.000111", SECOND,
                         "1970-01-01 00:00:00.000000"),
      TimestampTruncTest("1970-01-01 00:00:00.000111", MILLISECOND,
                         "1970-01-01 00:00:00.000000"),
      TimestampTruncTest("1970-01-01 00:00:00.000111", MICROSECOND,
                         "1970-01-01 00:00:00.000111"),

      TimestampTruncTest("1970-01-01 00:00:00.011111", SECOND,
                         "1970-01-01 00:00:00.000000"),
      TimestampTruncTest("1970-01-01 00:00:00.011111", MILLISECOND,
                         "1970-01-01 00:00:00.011000"),
      TimestampTruncTest("1970-01-01 00:00:00.011111", MICROSECOND,
                         "1970-01-01 00:00:00.011111"),

      TimestampTruncTest("1970-01-01 00:00:00.999999", SECOND,
                         "1970-01-01 00:00:00.000000"),
      TimestampTruncTest("1970-01-01 00:00:00.999999", MILLISECOND,
                         "1970-01-01 00:00:00.999000"),
      TimestampTruncTest("1970-01-01 00:00:00.999999", MICROSECOND,
                         "1970-01-01 00:00:00.999999"),

      TimestampTruncTest("1970-01-01 00:00:01.000001", SECOND,
                         "1970-01-01 00:00:01.000000"),
      TimestampTruncTest("1970-01-01 00:00:01.000001", MILLISECOND,
                         "1970-01-01 00:00:01.000000"),
      TimestampTruncTest("1970-01-01 00:00:01.000001", MICROSECOND,
                         "1970-01-01 00:00:01.000001"),

      TimestampTruncTest("1970-01-01 00:00:01.999999", SECOND,
                         "1970-01-01 00:00:01.000000"),
      TimestampTruncTest("1970-01-01 00:00:01.999999", MILLISECOND,
                         "1970-01-01 00:00:01.999000"),
      TimestampTruncTest("1970-01-01 00:00:01.999999", MICROSECOND,
                         "1970-01-01 00:00:01.999999"),

      TimestampTruncTest("1969-12-31 23:59:59.999999", SECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.999999", MILLISECOND,
                         "1969-12-31 23:59:59.999000"),
      TimestampTruncTest("1969-12-31 23:59:59.999999", MICROSECOND,
                         "1969-12-31 23:59:59.999999"),

      TimestampTruncTest("1969-12-31 23:59:59.999900", SECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.999900", MILLISECOND,
                         "1969-12-31 23:59:59.999000"),
      TimestampTruncTest("1969-12-31 23:59:59.999900", MICROSECOND,
                         "1969-12-31 23:59:59.999900"),

      TimestampTruncTest("1969-12-31 23:59:59.990000", SECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.990000", MILLISECOND,
                         "1969-12-31 23:59:59.990000"),
      TimestampTruncTest("1969-12-31 23:59:59.990000", MICROSECOND,
                         "1969-12-31 23:59:59.990000"),

      TimestampTruncTest("1969-12-31 23:59:59.000001", SECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.000001", MILLISECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.000001", MICROSECOND,
                         "1969-12-31 23:59:59.000001"),

      TimestampTruncTest("1969-12-31 23:59:59.000000", SECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.000000", MILLISECOND,
                         "1969-12-31 23:59:59.000000"),
      TimestampTruncTest("1969-12-31 23:59:59.000000", MICROSECOND,
                         "1969-12-31 23:59:59.000000"),

      TimestampTruncTest("1969-12-31 23:59:58.999999", SECOND,
                         "1969-12-31 23:59:58.000000"),
      TimestampTruncTest("1969-12-31 23:59:58.999999", MILLISECOND,
                         "1969-12-31 23:59:58.999000"),
      TimestampTruncTest("1969-12-31 23:59:58.999999", MICROSECOND,
                         "1969-12-31 23:59:58.999999"),

      // Min and max timestamps.
      TimestampTruncErrorTest("0001-01-01 00:00:00.000000", WEEK, OUT_OF_RANGE),
      TimestampTruncTest("0001-01-01 00:00:00.000000", SECOND,
                         "0001-01-01 00:00:00.000000"),
      TimestampTruncTest("0001-01-01 00:00:00.000000", MILLISECOND,
                         "0001-01-01 00:00:00.000000"),
      TimestampTruncTest("0001-01-01 00:00:00.000000", MICROSECOND,
                         "0001-01-01 00:00:00.000000"),

      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK,
                         "9999-12-26 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", ISOWEEK,
                         "9999-12-27 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK_MONDAY,
                         "9999-12-27 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK_TUESDAY,
                         "9999-12-28 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK_WEDNESDAY,
                         "9999-12-29 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK_THURSDAY,
                         "9999-12-30 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK_FRIDAY,
                         "9999-12-31 00:00:00.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", WEEK_SATURDAY,
                         "9999-12-25 00:00:00.000000"),

      TimestampTruncTest("9999-12-31 23:59:59.999999", SECOND,
                         "9999-12-31 23:59:59.000000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", MILLISECOND,
                         "9999-12-31 23:59:59.999000"),
      TimestampTruncTest("9999-12-31 23:59:59.999999", MICROSECOND,
                         "9999-12-31 23:59:59.999999"),

      // All the supported parts for a post-epoch timestamp.
      TimestampTruncTest("2015-02-03 01:02:03.456789", MICROSECOND,
                         "2015-02-03 01:02:03.456789"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", MILLISECOND,
                         "2015-02-03 01:02:03.456000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", SECOND,
                         "2015-02-03 01:02:03.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", MINUTE,
                         "2015-02-03 01:02:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", HOUR,
                         "2015-02-03 01:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", DAY,
                         "2015-02-03 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK,
                         "2015-02-01 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", ISOWEEK,
                         "2015-02-02 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_MONDAY,
                         "2015-02-02 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_TUESDAY,
                         "2015-02-03 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_WEDNESDAY,
                         "2015-01-28 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_THURSDAY,
                         "2015-01-29 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_FRIDAY,
                         "2015-01-30 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_SATURDAY,
                         "2015-01-31 00:00:00.000000"),

      TimestampTruncTest("2015-02-03 01:02:03.456789", MONTH,
                         "2015-02-01 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", QUARTER,
                         "2015-01-01 00:00:00.000000"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", QUARTER,
                         "2015-10-01 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", YEAR,
                         "2015-01-01 00:00:00.000000"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", ISOYEAR,
                         "2014-12-29 00:00:00.000000"),

      // All the supported parts for a pre-epoch timestamp.
      TimestampTruncTest("1967-02-03 01:02:03.456789", MICROSECOND,
                         "1967-02-03 01:02:03.456789"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", MILLISECOND,
                         "1967-02-03 01:02:03.456000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", SECOND,
                         "1967-02-03 01:02:03.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", MINUTE,
                         "1967-02-03 01:02:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", HOUR,
                         "1967-02-03 01:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", DAY,
                         "1967-02-03 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK,
                         "1967-01-29 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", ISOWEEK,
                         "1967-01-30 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_MONDAY,
                         "1967-01-30 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_TUESDAY,
                         "1967-01-31 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_WEDNESDAY,
                         "1967-02-01 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_THURSDAY,
                         "1967-02-02 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_FRIDAY,
                         "1967-02-03 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_SATURDAY,
                         "1967-01-28 00:00:00.000000"),

      TimestampTruncTest("1967-02-03 01:02:03.456789", MONTH,
                         "1967-02-01 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", QUARTER,
                         "1967-01-01 00:00:00.000000"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", QUARTER,
                         "1967-10-01 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", YEAR,
                         "1967-01-01 00:00:00.000000"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", ISOYEAR,
                         "1967-01-02 00:00:00.000000"),
      TimestampTruncTest("1967-01-01 01:02:03.456789", ISOYEAR,
                         "1966-01-03 00:00:00.000000"),
      // Truncate day light saving time at MILLISECOND.
      TimestampTruncTest("2016-11-06 09:01:01.1234+00", MILLISECOND,
                         "2016-11-06 09:01:01.123+00", "America/Los_Angeles"),
      // Some tests with more interesting time zones.
      // Same tests as above, but with America/Los_Angeles and Pacific/Chatham.
      TimestampTruncTest("2015-02-03 01:02:03.456789", MICROSECOND,
                         "2015-02-03 01:02:03.456789", "America/Los_Angeles"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", MILLISECOND,
                         "2015-02-03 01:02:03.456000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", SECOND,
                         "2015-02-03 01:02:03.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", MINUTE,
                         "2015-02-03 01:02:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", HOUR,
                         "2015-02-03 01:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", DAY,
                         "2015-02-03 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK,
                         "2015-02-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK,
                         "2015-11-29 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", ISOWEEK,
                         "2015-02-02 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", ISOWEEK,
                         "2015-11-30 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_MONDAY,
                         "2015-02-02 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK_MONDAY,
                         "2015-11-30 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_TUESDAY,
                         "2015-02-03 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK_TUESDAY,
                         "2015-12-01 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_WEDNESDAY,
                         "2015-01-28 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK_WEDNESDAY,
                         "2015-12-02 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_THURSDAY,
                         "2015-01-29 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK_THURSDAY,
                         "2015-12-03 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_FRIDAY,
                         "2015-01-30 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK_FRIDAY,
                         "2015-11-27 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", WEEK_SATURDAY,
                         "2015-01-31 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", WEEK_SATURDAY,
                         "2015-11-28 00:00:00.000000", "Pacific/Chatham"),

      TimestampTruncTest("2015-02-03 01:02:03.456789", MONTH,
                         "2015-02-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", QUARTER,
                         "2015-01-01 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("2015-12-03 01:02:03.456789", QUARTER,
                         "2015-10-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-02-03 01:02:03.456789", YEAR,
                         "2015-01-01 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", MICROSECOND,
                         "1967-02-03 01:02:03.456789", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", MILLISECOND,
                         "1967-02-03 01:02:03.456000", "Pacific/Chatham"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", SECOND,
                         "1967-02-03 01:02:03.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", MINUTE,
                         "1967-02-03 01:02:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", HOUR,
                         "1967-02-03 01:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", DAY,
                         "1967-02-03 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK,
                         "1967-01-29 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK,
                         "1967-12-03 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", ISOWEEK,
                         "1967-01-30 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", ISOWEEK,
                         "1967-11-27 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_MONDAY,
                         "1967-01-30 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK_MONDAY,
                         "1967-11-27 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_TUESDAY,
                         "1967-01-31 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK_TUESDAY,
                         "1967-11-28 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_WEDNESDAY,
                         "1967-02-01 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK_WEDNESDAY,
                         "1967-11-29 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_THURSDAY,
                         "1967-02-02 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK_THURSDAY,
                         "1967-11-30 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_FRIDAY,
                         "1967-02-03 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK_FRIDAY,
                         "1967-12-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", WEEK_SATURDAY,
                         "1967-01-28 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", WEEK_SATURDAY,
                         "1967-12-02 00:00:00.000000", "America/Los_Angeles"),

      TimestampTruncTest("1967-02-03 01:02:03.456789", MONTH,
                         "1967-02-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", QUARTER,
                         "1967-01-01 00:00:00.000000", "Pacific/Chatham"),
      TimestampTruncTest("1967-12-03 01:02:03.456789", QUARTER,
                         "1967-10-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("1967-02-03 01:02:03.456789", YEAR,
                         "1967-01-01 00:00:00.000000", "Pacific/Chatham"),

      TimestampTruncTest("1970-01-01 01:02:03.456789", YEAR,
                         "1970-01-01 00:00:00.000000", "+08:23"),
      TimestampTruncTest("1970-01-01 01:02:03.456789", MINUTE,
                         "1970-01-01 01:02:00.000000", "+08:23"),
      TimestampTruncTest("1970-01-01 01:02:03.456789", YEAR,
                         "1970-01-01 00:00:00.000000", "-08:23"),
      TimestampTruncTest("1970-01-01 01:02:03.456789", MINUTE,
                         "1970-01-01 01:02:00.000000", "-08:23"),

      TimestampTruncTest("1969-12-31 23:59:03.456789", YEAR,
                         "1969-01-01 00:00:00.000000", "+08:23"),
      TimestampTruncTest("1969-12-31 23:59:03.456789", MINUTE,
                         "1969-12-31 23:59:00.000000", "+08:23"),
      TimestampTruncTest("1969-12-31 23:59:03.456789", YEAR,
                         "1969-01-01 00:00:00.000000", "-08:23"),
      TimestampTruncTest("1969-12-31 23:59:03.456789", MINUTE,
                         "1969-12-31 23:59:00.000000", "-08:23"),

      // Validate a week timestamp truncation across a 'spring forward' DST
      // boundary.
      // Crosses a US DST boundary, but in a non-DST timezone.
      // DST transition occurs at 2016-03-13 01:59:59 -> 2016-03-13 03:00:00.
      TimestampTruncTest("2016-03-17 00:01:00.000000", WEEK,
                         "2016-03-13 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", ISOWEEK,
                         "2016-03-07 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_MONDAY,
                         "2016-03-07 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_TUESDAY,
                         "2016-03-08 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_WEDNESDAY,
                         "2016-03-09 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_THURSDAY,
                         "2016-03-10 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_FRIDAY,
                         "2016-03-11 00:00:00.000000", "-8"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_SATURDAY,
                         "2016-03-12 00:00:00.000000", "-8"),
      // Crosses a US DST boundary, but in a DST enabled timezone.
      // DST transition occurs at 2016-03-13 01:59:59 -> 2016-03-13 03:00:00.
      TimestampTruncTest("2016-03-17 00:01:00.000000", WEEK,
                         "2016-03-13 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", ISOWEEK,
                         "2016-03-07 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_MONDAY,
                         "2016-03-07 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_TUESDAY,
                         "2016-03-08 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_WEDNESDAY,
                         "2016-03-09 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_THURSDAY,
                         "2016-03-10 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_FRIDAY,
                         "2016-03-11 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 12:00:00.000000", WEEK_SATURDAY,
                         "2016-03-12 00:00:00.000000", "America/Los_Angeles"),

      // Validate a week timestamp truncation across a 'fall back' DST boundary.
      // Crosses a US DST boundary, but in a non-DST timezone.
      // DST transition occurs at 2015-11-01 01:59:59 -> 2015-11-01 01:00:00.
      TimestampTruncTest("2015-11-03 23:59:00.000000", WEEK,
                         "2015-11-01 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", ISOWEEK,
                         "2015-10-26 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_MONDAY,
                         "2015-10-26 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_TUESDAY,
                         "2015-10-27 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_WEDNESDAY,
                         "2015-10-28 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_THURSDAY,
                         "2015-10-29 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_FRIDAY,
                         "2015-10-30 00:00:00.000000", "-8"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_SATURDAY,
                         "2015-10-31 00:00:00.000000", "-8"),

      // Crosses a US DST boundary, but in a DST enabled timezone.
      // DST transition occurs at 2015-11-01 01:59:59 -> 2015-11-01 01:00:00.
      TimestampTruncTest("2015-11-03 23:59:00.000000", WEEK,
                         "2015-11-01 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", ISOWEEK,
                         "2015-10-26 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_MONDAY,
                         "2015-10-26 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_TUESDAY,
                         "2015-10-27 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_WEDNESDAY,
                         "2015-10-28 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_THURSDAY,
                         "2015-10-29 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_FRIDAY,
                         "2015-10-30 00:00:00.000000", "America/Los_Angeles"),
      TimestampTruncTest("2015-11-01 12:00:00.000000", WEEK_SATURDAY,
                         "2015-10-31 00:00:00.000000", "America/Los_Angeles"),

      // Spring forward in America/Los_Angeles is at:
      //   2016-03-13 01:59:59     -> 2016-03-13 03:00:00
      // which in UTC is:
      //   2016-03-13 09:59:59+00  -> 2016-03-13 10:00:00+00
      TimestampTruncTest("2016-03-13 08:01:01.1234+00", MILLISECOND,
                         "2016-03-13 08:01:01.123+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 09:01:01.1234+00", MILLISECOND,
                         "2016-03-13 09:01:01.123+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 10:01:01.1234+00", MILLISECOND,
                         "2016-03-13 10:01:01.123+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 08:01:01.1234+00", SECOND,
                         "2016-03-13 08:01:01+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 09:01:01.1234+00", SECOND,
                         "2016-03-13 09:01:01+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 10:01:01.1234+00", SECOND,
                         "2016-03-13 10:01:01+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 08:01:01.1234+00", HOUR,
                         "2016-03-13 08:00:00+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 09:01:01.1234+00", HOUR,
                         "2016-03-13 09:00:00+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 10:01:01.1234+00", HOUR,
                         "2016-03-13 10:00:00+00", "America/Los_Angeles"),

      TimestampTruncTest("2016-03-13 00:00:00 America/Los_Angeles", DAY,
                         "2016-03-13 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 00:00:00.000001 America/Los_Angeles", DAY,
                         "2016-03-13 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 09:59:59.1234+00", DAY,
                         "2016-03-13 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 10:01:01.1234+00", DAY,
                         "2016-03-13 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-03-13 23:59:59.1234 America/Los_Angeles", DAY,
                         "2016-03-13 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),

      // Fall back in America/Los_Angeles is at:
      //   2016-11-06 01:59:59     -> 2016-11-06 01:00:00
      // which in UTC is:
      //   2016-11-06 08:59:59+00  -> 2016-11-06 09:00:00+00
      TimestampTruncTest("2016-11-06 08:01:01.1234+00", MILLISECOND,
                         "2016-11-06 08:01:01.123+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 09:01:01.1234+00", MILLISECOND,
                         "2016-11-06 09:01:01.123+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 10:01:01.1234+00", MILLISECOND,
                         "2016-11-06 10:01:01.123+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 08:01:01.1234+00", SECOND,
                         "2016-11-06 08:01:01+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 09:01:01.1234+00", SECOND,
                         "2016-11-06 09:01:01+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 10:01:01.1234+00", SECOND,
                         "2016-11-06 10:01:01+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 08:01:01.1234+00", HOUR,
                         "2016-11-06 08:00:00+00", "America/Los_Angeles"),
      // Test case from b/31020480.
      TimestampTruncTest("2016-11-06 09:01:01.1234+00", HOUR,
                         "2016-11-06 09:00:00+00", "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 10:01:01.1234+00", HOUR,
                         "2016-11-06 10:00:00+00", "America/Los_Angeles"),

      TimestampTruncTest("2016-11-06 00:00:00 America/Los_Angeles", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 00:00:00.000001 America/Los_Angeles", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 07:01:01.1234+00", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 08:01:01.1234+00", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 09:01:01.1234+00", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 10:01:01.1234+00", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),
      TimestampTruncTest("2016-11-06 23:59:59.1234 America/Los_Angeles", DAY,
                         "2016-11-06 00:00:00 America/Los_Angeles",
                         "America/Los_Angeles"),

      // An example that truncates a timestamp to an instant in time that maps
      // to the second occurrence of a civil time in the specified timezone.
      // If we start with 1491055500 == 2017-04-02 02:50:00 +12:45, the 2nd
      // 02:50:00 on that day in Pacific/Chatham, and truncate to a civil hour
      // we truncate by 50 minutes.  This yields 1491052500, which corresponds
      // to 2017-04-02 03:00:00 +13:45.
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(1491055500)), GetDatePartValue(HOUR),
        "Pacific/Chatham"},
       Timestamp(absl::FromUnixSeconds(1491052500))},
      // The input timestamp is an hour before the input timestamp from the
      // previous test, and the result is also an hour before the previous
      // result.
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(1491055500 - 3600)),
        GetDatePartValue(HOUR), "Pacific/Chatham"},
       Timestamp(absl::FromUnixSeconds(1491052500 - 3600))},

      // This timestamp is exactly at the point in time where the clock was
      // moved forward by 30 minutes, and in local time it is:
      //   1895-02-01 00:16:40.
      // So 16:40 (1000 seconds) are subtracted from the timestamp.
      //
      // Note that the time zone offset also changed during this transition.
      // Before the DST change the offset was UTC+08:43:20, while after the
      // change the offset was UTC+09.
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(-2364108200)), GetDatePartValue(HOUR),
        "Australia/Darwin"},
       Timestamp(absl::FromUnixSeconds(-2364109200))},

      // This input timestamp is 1 second prior to the previous test, and is
      // 1 second prior to the clock moving forward and the time zone offset
      // changing in a non-trivial way.  The timezone offset at this timestamp
      // is UTC+08:43:20, which is different than the time zone
      // offset 1 second later (UTC+09).  The local time is:
      //   1895-01-31 23:59:59
      // So 59:59 (3599 seconds) are subtracted from the timestamp.
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(-2364108201)), GetDatePartValue(HOUR),
        "Australia/Darwin"},
       Timestamp(absl::FromUnixSeconds(-2364108201 - 3599))},

      // In Asia/Yangon, after 1945-05-02 23:59:59 the clocks moved
      // backwards to become 1945-05-02 21:30:00, and the time zone offset
      // changed.
      //
      // timestamp:        In Asia/Yangon          time zone offset
      // -778410001        1945-05-02 23:59:59     UTC+09
      // -778410000        1945-05-02 21:30:00     UTC+06:30
      //
      // For the first timestamp, we truncate 59:59 (3599 seconds).
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(-778410001)), GetDatePartValue(HOUR),
        "Asia/Yangon"},
       Timestamp(absl::FromUnixSeconds(-778410001 - 3599))},
      // For the second timestamp, we truncate 30:00 (1800 seconds).
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(-778410000)), GetDatePartValue(HOUR),
        "Asia/Yangon"},
       Timestamp(absl::FromUnixSeconds(-778410000 - 1800))},

      // The America/Los_Angeles time zone offset changed from UTC-07:52:58
      // to UTC-08 in 1883:
      //
      // timestamp:        In America/Los_Angeles  time zone offset
      // -2717640001       1883-11-18 12:07:01     UTC-07:52:58
      // -2717640000       1883-11-18 12:00:00     UTC-08
      //
      // For the first timestamp, we truncate 07:01 (421 seconds)
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(-2717640001)), GetDatePartValue(HOUR),
        "America/Los_Angeles"},
       Timestamp(absl::FromUnixSeconds(-2717640001 - 421))},
      // For the second timestamp, we do not truncate any time since it is
      // at the hour boundary.
      {"timestamp_trunc",
       {Timestamp(absl::FromUnixSeconds(-2717640000)), GetDatePartValue(HOUR),
        "America/Los_Angeles"},
       Timestamp(absl::FromUnixSeconds(-2717640000))},

      // TIMESTAMP_TRUNC() to second or subsecond parts provides the same answer
      // regardless of timezone.  Add a few such tests at that precision that do
      // not have the optional third timezone argument since every engine should
      // give the same/correct answer independent of what their default time
      // zone is.
      //
      // Note - while the comment above is true in the tests below for
      // the default time zones currently being used by engines
      // (UTC or America/Los_Angeles), it is not in general true.  For early
      // dates the seconds offset of America/Los_Angeles contained a non-zero
      // seconds part, and there are other time zones that include a non-zero
      // seconds offset for some dates as well.  If these tests become
      // problematic then we'll need to change them.
      {"timestamp_trunc", {Timestamp(111), Second()}, Timestamp(0)},
      {"timestamp_trunc", {Timestamp(111), Millisecond()}, Timestamp(0)},
      {"timestamp_trunc", {Timestamp(111), Microsecond()}, Timestamp(111)},

      {"timestamp_trunc", {Timestamp(11111), Second()}, Timestamp(0)},
      {"timestamp_trunc", {Timestamp(11111), Millisecond()}, Timestamp(11000)},
      {"timestamp_trunc", {Timestamp(11111), Microsecond()}, Timestamp(11111)},

      {"timestamp_trunc", {Timestamp(1234567), Second()}, Timestamp(1000000)},
      {"timestamp_trunc",
       {Timestamp(1234567), Millisecond()},
       Timestamp(1234000)},
      {"timestamp_trunc",
       {Timestamp(1234567), Microsecond()},
       Timestamp(1234567)},
  };
  return v;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampTrunc() {
  std::vector<FunctionTestCall> result;
  for (const FunctionTestCall& test :
       GetFunctionTestsTimestampTruncInternal()) {
    result.push_back(test);
    if (test.params.required_features().empty()) {
      result.push_back(FunctionTestCall(
          "date_trunc",
          test.params.WrapWithFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)));
    }
  }
  return result;
}

// Adapted from TEST(ExtractTimestamp, DateTest) in date_time_util_test.cc.
std::vector<FunctionTestCall> GetFunctionTestsDateFromTimestamp() {
  return {// Tests involving NULL arguments.
          {"date", {NullTimestamp()}, NullDate()},
          {"date", {Timestamp(10), NullString()}, NullDate()},
          {"date", {NullTimestamp(), String("+0")}, NullDate()},
          {"date", {NullTimestamp(), NullString()}, NullDate()},
          // Tests of the normal case. The code-based tests do not allow setting
          // the default timezone, so the test for that case is in the
          // date_from_timestamp test in
          // timestamp_with_default_time_zone{,_2}.test.
          {"date",
           {TimestampFromStr("2014-01-31 18:12:34")},
           DateFromStr("2014-01-31")},
          {"date",
           {TimestampFromStr("2014-01-31 18:12:34"), String("+0")},
           DateFromStr("2014-01-31")},
          {"date",
           {TimestampFromStr("2014-01-31 18:12:34"), String("UTC")},
           DateFromStr("2014-01-31")},
          {"date",
           {TimestampFromStr("2014-01-31 18:12:34"), String("+12")},
           DateFromStr("2014-02-01")},
          // Extracts DATE from "0000-12-31 16:12:34 PST8PDT", which is a valid
          // timestamp, ends up with an invalid DATE(0000-12-31).
          {"date",
           {TimestampFromStr("0000-12-31 16:12:34 PST8PDT"), String("PST8PDT")},
           NullDate(),
           OUT_OF_RANGE},
          // Extracts DATE from "10000-01-01 02:12:34 Asia/Shanghai", which is a
          // valid timestamp, ends up with an invalid DATE(10000-01-01).
          {"date",
           {TimestampFromStr("10000-01-01 02:12:34 Asia/Shanghai"),
            String("Asia/Shanghai")},
           NullDate(),
           OUT_OF_RANGE}};
}

std::vector<FunctionTestCall> GetFunctionTestsDateFromUnixDate() {
  std::vector<FunctionTestCall> tests({
      {"unix_date",           {NullDate()},           NullInt64()},
      {"unix_date",           {Date(0)},              Int64(0)},
      {"unix_date",           {Date(date_min)},       Int64(date_min)},
      {"unix_date",           {Date(date_max)},       Int64(date_max)},

      {"date_from_unix_date", {NullInt64()},          NullDate()},
      {"date_from_unix_date", {Int64(0)},             Date(0)},
      {"date_from_unix_date", {Int64(date_min)},      Date(date_min)},
      {"date_from_unix_date", {Int64(date_max)},      Date(date_max)},

      {"date_from_unix_date", {Int64(date_min - 1)},
       NullDate(), OUT_OF_RANGE},
      {"date_from_unix_date", {Int64(date_max + 1)},
       NullDate(), OUT_OF_RANGE},

      {"date_from_unix_date", {Int64(int32min)},
       NullDate(), OUT_OF_RANGE},
      {"date_from_unix_date", {Int64(int64min)},
       NullDate(), OUT_OF_RANGE},
      {"date_from_unix_date", {Int64(int32max)},
       NullDate(), OUT_OF_RANGE},
      {"date_from_unix_date", {Int64(int64max)},
       NullDate(), OUT_OF_RANGE},
  });

  return tests;
}

// Defines which parts need to be supported for this format string to work:
// - DATE means this format works with types that include a date:
//   date, timestamp, datetime.
// - TIME means this format works with types that include a time:
//   timestamp, datetime, time.
// - DATE_AND_TIME means that this format works with types that include both
//   a date and a time: timestamp, datetime
// - TIMEZONE means that this format works with types that include
//   a timezone: timestamp.
// - ANY means this works the same on any data type:
//   date, time, timestamp, datetime.
enum TestFormatDateTimeParts {
  TEST_FORMAT_DATE = 0,
  TEST_FORMAT_TIME = 1,
  TEST_FORMAT_DATE_AND_TIME = 2,
  TEST_FORMAT_TIMEZONE = 3,
  TEST_FORMAT_ANY = 4
};

// Defines the common tests that can be used for testing format_timestamp
// and format_date functions.
// For all the tests whose format_string is valid for TIMESTAMP but not
// applicable to DATE, the corresponding test added for DATE will be
// a no-op conversion of format_string, which means format_string with
// mixed formats are not expected.
struct FormatDateTimestampCommonTest {
  std::string format_string;
  TestFormatDateTimeParts parts;
  std::string expected_result;
};

// Timestamp, timezone and date used for the common tests.
static char kCommonTestTimestamp[] = "2015-07-08 01:02:03.456789";
static char kCommonTestTimezone[] = "America/Los_Angeles";
static char kCommonTestDate[] = "2015-07-08";

// Constructs a list of tests that can be used for testing both
// format_timestamp and format_date functions.
static std::vector<FormatDateTimestampCommonTest>
    GetFormatDateTimestampCommonTests() {
  std::vector<FormatDateTimestampCommonTest> tests = {
      // Normal characters are ok and come through undisturbed.
      {"", TEST_FORMAT_ANY, ""},
      {"abcd", TEST_FORMAT_ANY, "abcd"},
      {"abcd%Yefgh", TEST_FORMAT_DATE, "abcd2015efgh"},
      {"%%%%%%Y", TEST_FORMAT_ANY, "%%%Y"},
      // UTF-8 as well.
      {"%ыфщ%E4Yыфщ", TEST_FORMAT_DATE, "%ыфщ2015ыфщ"},

      // Basic tests for all the supported strftime format elements, as per
      // third_party/tz/newstrftime.3.txt.
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2015-07-08"},
      {"%A", TEST_FORMAT_DATE, "Wednesday"},
      {"%a", TEST_FORMAT_DATE, "Wed"},
      {"%B", TEST_FORMAT_DATE, "July"},
      {"%b", TEST_FORMAT_DATE, "Jul"},
      {"%C", TEST_FORMAT_DATE, "20"},  // century

      {"%c", TEST_FORMAT_DATE_AND_TIME, "Wed Jul  8 01:02:03 2015"},

      {"%D", TEST_FORMAT_DATE, "07/08/15"},
      {"%d", TEST_FORMAT_DATE, "08"},  // month day
      {"%e", TEST_FORMAT_DATE, " 8"},  // month day
      {"%F", TEST_FORMAT_DATE, "2015-07-08"},
      {"%G", TEST_FORMAT_DATE, "2015"},
      {"%g", TEST_FORMAT_DATE, "15"},

      {"%H", TEST_FORMAT_TIME, "01"},  // hour (0-23)

      {"%h", TEST_FORMAT_DATE, "Jul"},

      {"%I", TEST_FORMAT_TIME, "01"},  // hour (1-12)

      {"%j", TEST_FORMAT_DATE, "189"},  // year day
      {"%J", TEST_FORMAT_DATE, "192"},  // ISO year day

      {"%k", TEST_FORMAT_TIME, " 1"},  // hour (0-23)
      {"%l", TEST_FORMAT_TIME, " 1"},  // hour (1-12)
      {"%M", TEST_FORMAT_TIME, "02"},  // minute

      {"%m", TEST_FORMAT_DATE, "07"},
      {"%n", TEST_FORMAT_ANY, "\n"},  // newline

      {"%p", TEST_FORMAT_TIME, "AM"},
      {"%R", TEST_FORMAT_TIME, "01:02"},
      {"%r", TEST_FORMAT_TIME, "01:02:03 AM"},
      {"%S", TEST_FORMAT_TIME, "03"},  // second

      // Seconds since 'epoch', which *is* equivalent to the GoogleSQL epoch.
      {"%s", TEST_FORMAT_DATE_AND_TIME, "1436342523"},
      {"%T", TEST_FORMAT_TIME, "01:02:03"},

      {"%t", TEST_FORMAT_ANY, "\t"},   // tab
      {"%U", TEST_FORMAT_DATE, "27"},  // week
      {"%u", TEST_FORMAT_DATE, "3"},   // week day
      {"%V", TEST_FORMAT_DATE, "28"},  // week
      {"%W", TEST_FORMAT_DATE, "27"},  // week
      {"%w", TEST_FORMAT_DATE, "3"},   // week day

      {"%X", TEST_FORMAT_TIME, "01:02:03"},

      {"%x", TEST_FORMAT_DATE, "07/08/15"},
      {"%Y", TEST_FORMAT_DATE, "2015"},
      {"%y", TEST_FORMAT_DATE, "15"},

      {"%Z", TEST_FORMAT_TIMEZONE, "UTC-7"},
      {"%z", TEST_FORMAT_TIMEZONE, "-0700"},

      {"%%", TEST_FORMAT_ANY, "%"},
      {"%F", TEST_FORMAT_DATE, "2015-07-08"},

      {"%Q", TEST_FORMAT_DATE, "3"},

      // Unsupported patterns come through as-is.
      {"%", TEST_FORMAT_ANY, "%"},
      {"%E", TEST_FORMAT_ANY, "%E"},
      {"%f", TEST_FORMAT_ANY, "%f"},
      {"%i", TEST_FORMAT_ANY, "%i"},
      {"%K", TEST_FORMAT_ANY, "%K"},
      {"%L", TEST_FORMAT_ANY, "%L"},
      {"%N", TEST_FORMAT_ANY, "%N"},
      {"%O", TEST_FORMAT_ANY, "%O"},
      {"%o", TEST_FORMAT_ANY, "%o"},
      {"%q", TEST_FORMAT_ANY, "%q"},
      {"%v", TEST_FORMAT_ANY, "%v"},
      {"%1", TEST_FORMAT_ANY, "%1"},
      {"%-", TEST_FORMAT_ANY, "%-"},

      // strftime() modifiers 'E' and 'O' as per:
      // http://www.cplusplus.com/reference/ctime/strftime/
      // a) modifier 'E' uses locale's alternative representation
      // b) modifier 'O' uses locale's alternative numeric symbols
      // Illustrate behavior both with E/O modifier and without.  Note that
      // there are not any differences for the en-US locale.
      {"%EC", TEST_FORMAT_DATE, "20"},
      {"%C", TEST_FORMAT_DATE, "20"},

      {"%Ec", TEST_FORMAT_DATE_AND_TIME, "Wed Jul  8 01:02:03 2015"},
      {"%c", TEST_FORMAT_DATE_AND_TIME, "Wed Jul  8 01:02:03 2015"},
      {"%EX", TEST_FORMAT_TIME, "01:02:03"},
      {"%X", TEST_FORMAT_TIME, "01:02:03"},

      {"%Ex", TEST_FORMAT_DATE, "07/08/15"},
      {"%x", TEST_FORMAT_DATE, "07/08/15"},
      {"%EY", TEST_FORMAT_DATE, "2015"},
      {"%Y", TEST_FORMAT_DATE, "2015"},
      {"%Ey", TEST_FORMAT_DATE, "15"},
      {"%y", TEST_FORMAT_DATE, "15"},
      {"%Od", TEST_FORMAT_DATE, "08"},
      {"%d", TEST_FORMAT_DATE, "08"},
      {"%Oe", TEST_FORMAT_DATE, " 8"},
      {"%e", TEST_FORMAT_DATE, " 8"},

      {"%OH", TEST_FORMAT_TIME, "01"},
      {"%H", TEST_FORMAT_TIME, "01"},
      {"%OI", TEST_FORMAT_TIME, "01"},
      {"%OM", TEST_FORMAT_TIME, "02"},
      {"%M", TEST_FORMAT_TIME, "02"},

      {"%Om", TEST_FORMAT_DATE, "07"},
      {"%m", TEST_FORMAT_DATE, "07"},

      {"%OS", TEST_FORMAT_TIME, "03"},
      {"%S", TEST_FORMAT_TIME, "03"},

      {"%OU", TEST_FORMAT_DATE, "27"},
      {"%U", TEST_FORMAT_DATE, "27"},
      {"%Ou", TEST_FORMAT_DATE, "3"},
      {"%u", TEST_FORMAT_DATE, "3"},
      {"%OV", TEST_FORMAT_DATE, "28"},
      {"%V", TEST_FORMAT_DATE, "28"},
      {"%OW", TEST_FORMAT_DATE, "27"},
      {"%W", TEST_FORMAT_DATE, "27"},
      {"%Ow", TEST_FORMAT_DATE, "3"},
      {"%w", TEST_FORMAT_DATE, "3"},
      {"%Oy", TEST_FORMAT_DATE, "15"},
      {"%y", TEST_FORMAT_DATE, "15"},

      // Extended format elements as per absl::FormatTime()
      {"%Ez", TEST_FORMAT_TIMEZONE, "-07:00"},
      {"%E0S", TEST_FORMAT_TIME, "03"},
      {"%E1S", TEST_FORMAT_TIME, "03.4"},
      {"%E2S", TEST_FORMAT_TIME, "03.45"},
      {"%E3S", TEST_FORMAT_TIME, "03.456"},
      {"%E4S", TEST_FORMAT_TIME, "03.4567"},
      {"%E5S", TEST_FORMAT_TIME, "03.45678"},
      {"%E6S", TEST_FORMAT_TIME, "03.456789"},
      {"%E7S", TEST_FORMAT_TIME, "03.4567890"},
      {"%E8S", TEST_FORMAT_TIME, "03.45678900"},
      {"%E9S", TEST_FORMAT_TIME, "03.456789000"},
      {"%E*S", TEST_FORMAT_TIME, "03.456789"},

      {"%E4Y", TEST_FORMAT_DATE, "2015"},

      // Modifiers 'E' and 'O' without an immediately following 'valid' special
      // character(s) come through as-is.
      {"%E", TEST_FORMAT_ANY, "%E"},
      {"%O", TEST_FORMAT_ANY, "%O"},
      {"%E w", TEST_FORMAT_ANY, "%E w"},
      {"%O W", TEST_FORMAT_ANY, "%O W"},
      {"%EO", TEST_FORMAT_ANY, "%EO"},
      {"%OE", TEST_FORMAT_ANY, "%OE"},
      {"%E4", TEST_FORMAT_ANY, "%E4"},
      {"%E9", TEST_FORMAT_ANY, "%E9"},
      {"%E*", TEST_FORMAT_ANY, "%E*"},
      {"%Eb", TEST_FORMAT_ANY, "%Eb"},
      {"%EB", TEST_FORMAT_ANY, "%EB"},

      // Lowercase 'e' and 'o' are not special modifiers.  Lowercase 'e' is
      // actually a valid format element itself.
      {"%EX", TEST_FORMAT_TIME, "01:02:03"},

      {"%eX", TEST_FORMAT_DATE, " 8X"},
      {"%OV", TEST_FORMAT_DATE, "28"},
      {"%oV", TEST_FORMAT_ANY, "%oV"},

      // Other weird cases.
      {"%\x00", TEST_FORMAT_ANY, "%\x00"},
      {"%\x01", TEST_FORMAT_ANY, "%\x01"},

      // Deviations from strftime (third_party/tz/newstrftime.3.txt)
      //
      // According to strftime, '%+' produces the date and time in c++ date(1)
      // format, whose default output format depends on the locale.  However,
      // it is not treated as a special pattern in GoogleSQL and gets passed
      // through as-is.
      {"%+", TEST_FORMAT_ANY, "%+"},

      // According to strftime, '%P' is passed through as-is since it is not a
      // special format element, but it gets interpreted as lowercase
      // 'am' or 'pm' in GoogleSQL.
      {"%P", TEST_FORMAT_TIME, "am"},

      // Additional week number testing.
      // %U - Sunday is first day of the week, 00-53
      // %V - Monday is first day of the week, 01-53
      // %W - Monday is first day of the week, 00-53
      {"%U", TEST_FORMAT_DATE, "27"},
      {"%V", TEST_FORMAT_DATE, "28"},
      {"%W", TEST_FORMAT_DATE, "27"},
  };

  return tests;
}

struct FormatTimestampTest {
  std::string format_string;  // format string
  int64_t timestamp;          // int64 timestamp (micros) value to format
  std::string timezone;    // time zone to use for formatting
  std::string expected_result;  // expected output string
};

// The following test cases do not have any '%z' or '%Z' in the format string,
// so they can be adapted as format_datetime() test cases.
static std::vector<FormatTimestampTest> GetTimezoneFreeFormatTimestampTests() {
  int64_t timestamp;
  const std::string timezone = "America/Los_Angeles";
  std::vector<FormatTimestampTest> tests;

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2013-12-31 00:00:00", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%U", timestamp, timezone, "52"});
  tests.push_back({"%V", timestamp, timezone, "01"});
  tests.push_back({"%W", timestamp, timezone, "52"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2014-01-01 00:00:00", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%U", timestamp, timezone, "52"});
  tests.push_back({"%V", timestamp, timezone, "01"});
  tests.push_back({"%W", timestamp, timezone, "52"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2014-01-02 00:00:00", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%U", timestamp, timezone, "00"});
  tests.push_back({"%V", timestamp, timezone, "01"});
  tests.push_back({"%W", timestamp, timezone, "00"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2015-12-31 00:00:00", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%U", timestamp, timezone, "52"});
  tests.push_back({"%V", timestamp, timezone, "53"});
  tests.push_back({"%W", timestamp, timezone, "52"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2016-01-01 00:00:00", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%U", timestamp, timezone, "52"});
  tests.push_back({"%V", timestamp, timezone, "53"});
  tests.push_back({"%W", timestamp, timezone, "52"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2016-01-02 00:00:00", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%U", timestamp, timezone, "00"});
  tests.push_back({"%V", timestamp, timezone, "53"});
  tests.push_back({"%W", timestamp, timezone, "00"});

  // Additional hour format testing.
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2014-01-01 23:45:01", "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%H", timestamp, "UTC", "23"});  // hour (00-23)
  tests.push_back({"%k", timestamp, "UTC", "23"});  // hour (0-23)
  tests.push_back({"%I", timestamp, "UTC", "11"});  // hour (01-12)
  tests.push_back({"%l", timestamp, "UTC", "11"});  // hour (1-12)
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2014-01-01 01:45:01",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%H", timestamp, "UTC", "01"});  // hour (00-23)
  tests.push_back({"%k", timestamp, "UTC", " 1"});  // hour (0-23)
  tests.push_back({"%I", timestamp, "UTC", "01"});  // hour (01-12)
  tests.push_back({"%l", timestamp, "UTC", " 1"});  // hour (1-12)

  // Day of year, with leading zeros.
  tests.push_back({"%j", timestamp, "UTC", "001"});  // year day
  tests.push_back({"%J", timestamp, "UTC", "003"});  // year day

  // Additional week day testing.
  // %u - Monday = 1, Sunday = 7
  // %w - Sunday = 0, Saturday = 6
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2015-02-09 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%u", timestamp, "UTC", "1"});  // Monday
  tests.push_back({"%w", timestamp, "UTC", "1"});  // Monday
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2015-02-08 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%u", timestamp, "UTC", "7"});  // Sunday
  tests.push_back({"%w", timestamp, "UTC", "0"});  // Sunday
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2015-02-07 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%u", timestamp, "UTC", "6"});  // Saturday
  tests.push_back({"%w", timestamp, "UTC", "6"});  // Saturday

  // Additional year testing.
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "1899-01-01 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y", timestamp, "UTC", "1899"});
  tests.push_back({"%y", timestamp, "UTC", "99"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "1900-01-01 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y", timestamp, "UTC", "1900"});
  tests.push_back({"%y", timestamp, "UTC", "00"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "1999-01-01 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y", timestamp, "UTC", "1999"});
  tests.push_back({"%y", timestamp, "UTC", "99"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y", timestamp, "UTC", "2000"});
  tests.push_back({"%y", timestamp, "UTC", "00"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2099-01-01 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y", timestamp, "UTC", "2099"});
  tests.push_back({"%y", timestamp, "UTC", "99"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2100-01-01 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y", timestamp, "UTC", "2100"});
  tests.push_back({"%y", timestamp, "UTC", "00"});

  // Test the subsecond '%E*S' pattern.
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.234567",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01.234567"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.234560",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01.23456"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.234500",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01.2345"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.234000",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01.234"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.230000",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01.23"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.200000",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01.2"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "2000-01-01 00:00:01.000000",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%E*S", timestamp, timezone, "01"});

  return tests;
}

static std::vector<FormatTimestampTest> GetFormatTimestampTests() {
  int64_t epoch_timestamp = 0;
  int64_t timestamp;
  const std::string timezone = "America/Los_Angeles";

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      kCommonTestTimestamp, kCommonTestTimezone, kMicroseconds, &timestamp));

  std::vector<FormatTimestampTest> tests = {
      {"%Y-%m-%d %H:%M:%S", epoch_timestamp, "UTC", "1970-01-01 00:00:00"},

      // Test the hour in different time zones.
      {"%H", timestamp, "America/Los_Angeles", "01"},
      {"%H", timestamp, "Asia/Yangon", "14"},
      {"%H", timestamp, "UTC", "08"},

      // Basic test of full timestamp format.
      {"%Y-%m-%d %H:%M:%E6S%z", timestamp, timezone,
       "2015-07-08 01:02:03.456789-0700"},

      // Multiple percents.
      {"%%%%%H", timestamp, timezone, "%%01"},
  };

  // Adds all the common tests.
  for (const FormatDateTimestampCommonTest& common_test
          : GetFormatDateTimestampCommonTests()) {
    tests.push_back({common_test.format_string, timestamp,
                     kCommonTestTimezone, common_test.expected_result});
  }

  // Adds all timezone-free format test cases.
  for (const FormatTimestampTest& test_case :
       GetTimezoneFreeFormatTimestampTests()) {
    tests.push_back(test_case);
  }

  // Timestamp range boundary testing.
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "0001-01-01 00:00:00.000000",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%4Y-%m-%d %H:%M:%E6S%Ez", timestamp, "UTC",
                   "0001-01-01 00:00:00.000000+00:00"});

  // As with casting a timestamp to string, even though the canonical time
  // zone offset for America/Timezone is -07:52:58 in year 1, we normalize
  // the time zone offset to only include the hour and minute parts.
  tests.push_back({"%4Y-%m-%d %H:%M:%E6S%Ez", timestamp, timezone,
                   "0000-12-31 16:08:00.000000-07:52"});
  tests.push_back({"%4Y-%m-%d %H:%M:%E6S%Ez", timestamp, "Asia/Yangon",
                   "0001-01-01 06:24:00.000000+06:24"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "9999-12-31 23:59:59.999999",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%Y-%m-%d %H:%M:%E6S%Ez", timestamp, "UTC",
                   "9999-12-31 23:59:59.999999+00:00"});
  tests.push_back({"%Y-%m-%d %H:%M:%E6S%Ez", timestamp, timezone,
                   "9999-12-31 15:59:59.999999-08:00"});
  tests.push_back({"%Y-%m-%d %H:%M:%E6S%Ez", timestamp, "Asia/Yangon",
                   "10000-01-01 06:29:59.999999+06:30"});
  // Same test as before, but with %4Y instead of %Y.  As per FormatTime()
  // comments, %Y produces as many characters as it takes to fully render
  // the year.  A year outside of [-999:9999] when formatted with %E4Y will
  // produce more than four characters, just like %Y.
  tests.push_back({"%4Y-%m-%d %H:%M:%E6S%Ez", timestamp, "Asia/Yangon",
                   "10000-01-01 06:29:59.999999+06:30"});

  // Check that FormatTimestamp removes subsecond timezone offset
  // (before 1884 PST timezone had offset of 7h 52m 58s, but FormatTimestamp
  // should only use 7h 52m).
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp("1812-09-07 07:52:01", "UTC", kMicroseconds,
                                    &timestamp));
  tests.push_back(
      {"%F %T", timestamp, "America/Los_Angeles", "1812-09-07 00:00:01"});
  tests.push_back({"%F %T%z", timestamp, "America/Los_Angeles",
                   "1812-09-07 00:00:01-0752"});
  tests.push_back({"%Y%m%d", timestamp, "America/Los_Angeles", "18120907"});
  tests.push_back({"%H:%M:%S", timestamp, "America/Los_Angeles", "00:00:01"});
  tests.push_back(
      {"%c", timestamp, "America/Los_Angeles", "Mon Sep  7 00:00:01 1812"});

  // Check that %F and %Y remove leading zeros
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "0001-02-03 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%F %T", timestamp, "UTC", "1-02-03 00:00:00"});
  tests.push_back({"%Y-%m-%d %T", timestamp, "UTC", "1-02-03 00:00:00"});
  tests.push_back({"%Y%m%d", timestamp, "UTC", "10203"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "0100-02-03 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%F %T", timestamp, "UTC", "100-02-03 00:00:00"});
  tests.push_back({"%Y-%m-%d %T", timestamp, "UTC", "100-02-03 00:00:00"});
  tests.push_back({"%Y%m%d", timestamp, "UTC", "1000203"});
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(
      "1000-02-03 00:00:00",  "UTC", kMicroseconds, &timestamp));
  tests.push_back({"%F %T", timestamp, "UTC", "1000-02-03 00:00:00"});
  tests.push_back({"%Y-%m-%d %T", timestamp, "UTC", "1000-02-03 00:00:00"});
  tests.push_back({"%Y%m%d", timestamp, "UTC", "10000203"});

  return tests;
}

struct FormatDateTest {
  std::string format_string;    // format string
  std::string date;             // date string to format
  std::string expected_result;  // expected output string
};

static std::vector<FormatDateTest> GetFormatDateTests() {
  const std::string epoch_date = "1970-01-01";

  std::vector<FormatDateTest> tests = {
    {"%Y-%m-%d", epoch_date, "1970-01-01"},
  };

  // Adds all the common tests.
  for (const FormatDateTimestampCommonTest& common_test
          : GetFormatDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_DATE ||
        common_test.parts == TEST_FORMAT_ANY) {
      tests.push_back({common_test.format_string, kCommonTestDate,
                       common_test.expected_result});
    } else {
      // Passes through the format_string as is.
      tests.push_back({common_test.format_string, kCommonTestDate,
                       common_test.format_string});
    }
  }

  // Multiple percents.
  tests.push_back({"%%%%%H", kCommonTestDate, "%%%H"});

  // Mixed valid/unsupported formats.
  tests.push_back({"%Y-%m-%d %H:%M:%E6S%z", kCommonTestDate,
                   "2015-07-08 %H:%M:%E6S%z"});
  tests.push_back({"%Y-%%%%%m-%%%%%M", kCommonTestDate, "2015-%%07-%%%M"});
  tests.push_back({"%Y-%m-%M-%f", kCommonTestDate, "2015-07-%M-%f"});

  // Additional tests with year/month/week boundaries.
  std::string date = "2014-01-01";
  tests.push_back({"%U", date, "00"});
  tests.push_back({"%V", date, "01"});
  tests.push_back({"%W", date, "00"});

  date = "2014-01-02";
  tests.push_back({"%U", date, "00"});
  tests.push_back({"%V", date, "01"});
  tests.push_back({"%W", date, "00"});

  date = "2016-01-01";
  tests.push_back({"%U", date, "00"});
  tests.push_back({"%V", date, "53"});
  tests.push_back({"%W", date, "00"});

  date = "2016-01-02";
  tests.push_back({"%U", date, "00"});
  tests.push_back({"%V", date, "53"});
  tests.push_back({"%W", date, "00"});

  // Day of year, with leading zeros.
  tests.push_back({"%j", date, "002"});  // year day
  tests.push_back({"%J", date, "370"});  // ISO year day

  // %H does not work for Date, nor should %2H
  tests.push_back({"%H", date, "%H"});
  tests.push_back({"%2H", date, "%2H"});
  tests.push_back({"%3H", date, "%3H"});
  tests.push_back({"%EH", date, "%EH"});
  tests.push_back({"%OH", date, "%OH"});
  tests.push_back({"%Ez", date, "%Ez"});
  tests.push_back({"%E#s", date, "%E#s"});
  tests.push_back({"%E2s", date, "%E2s"});
  tests.push_back({"%E*s", date, "%E*s"});

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsFormatDateTimestamp() {
  // There are generally no FORMAT_TIMESTAMP tests here with the optional third
  // timezone argument missing, since that would require that all engines have
  // the same default time zone in order to be in compliance.  GoogleSQL does
  // not enforce or require that.
  std::vector<FunctionTestCall> tests = {
    // The result is NULL if any of the arguments are NULL.
    {"format_timestamp", {NullString(), NullTimestamp(), NullString()},
      NullString()},
    {"format_timestamp", {String("a"), NullTimestamp(), NullString()},
      NullString()},
    {"format_timestamp", {NullString(), Timestamp(0), NullString()},
      NullString()},
    {"format_timestamp", {NullString(), NullTimestamp(), String("")},
      NullString()},

    {"format_timestamp", {NullString(), NullTimestamp()}, NullString()},
    {"format_timestamp", {String("a"), NullTimestamp()}, NullString()},
    {"format_timestamp", {NullString(), Timestamp(0)}, NullString()},

    // format_date tests with NULL arguments.
    {"format_date", {NullString(), NullDate()}, NullString()},
    {"format_date", {String("a"), NullDate()}, NullString()},
    {"format_date", {NullString(), Date(0)}, NullString()},
  };

  for (const FormatTimestampTest& test : GetFormatTimestampTests()) {
    tests.push_back({"format_timestamp",
                     {String(test.format_string), Timestamp(test.timestamp),
                       String(test.timezone)},
                     String(test.expected_result)});

    tests.push_back(FunctionTestCall(
        "format_date", tests.back().params.WrapWithFeature(
                           FEATURE_EXTENDED_DATE_TIME_SIGNATURES)));
  }

  for (const FormatDateTest& test : GetFormatDateTests()) {
    tests.push_back({"format_date",
                     {String(test.format_string), DateFromStr(test.date)},
                     String(test.expected_result)});
  }

  // Some tests for invalid inputs.  As far as I can tell there are no
  // invalid format strings, and there is no way to construct an invalid
  // timestamp value.  So the only thing to test for here is an invalid
  // time zone.
  tests.push_back({"format_timestamp",
                   {String(""), Timestamp(0), String("invalid_timezone")},
                   NullString(), OUT_OF_RANGE});
  // This was b/74112042.  If the timezone is an empty string that is an
  // error (it is not treated as the default timezone).
  tests.push_back({"format_timestamp",
                   {String(""), Timestamp(0), String("")},
                   NullString(), OUT_OF_RANGE});

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsFormatDatetime() {
  static const Value common_datetime =
      DatetimeMicros(2015, 7, 8, 1, 2, 3, 456789);
  ABSL_CHECK_EQ(kCommonTestTimestamp, common_datetime.DebugString());

  std::vector<CivilTimeTestCase> test_cases = {
      // NULL handling
      {{{NullString(), common_datetime}}, NullString()},
      {{{String(""), NullDatetime()}}, NullString()},
      {{{NullString(), NullDatetime()}}, NullString()},

      // Datetime range boundary testing
      {{{String("%E4Y-%m-%d %H:%M:%E6S"), DatetimeMicros(1, 1, 1, 0, 0, 0, 0)}},
       String("0001-01-01 00:00:00.000000")},
      {{{String("%E4Y-%m-%d %H:%M:%E6S"),
         DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999)}},
       String("9999-12-31 23:59:59.999999")},
  };

  for (const FormatTimestampTest& test_case :
       GetTimezoneFreeFormatTimestampTests()) {
    DatetimeValue datetime;
    GOOGLESQL_CHECK_OK(functions::ConvertTimestampToDatetime(
        absl::FromUnixMicros(test_case.timestamp), test_case.timezone,
        &datetime));
    test_cases.push_back(CivilTimeTestCase(
        {{String(test_case.format_string), Datetime(datetime)}},
        String(test_case.expected_result)));
  }

  // Adds applicable common tests
  for (const FormatDateTimestampCommonTest& common_test :
       GetFormatDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_TIMEZONE) {
      // Test cases with TEST_FORMAT_TIMEZONE include timezone-sensitive format
      // elements that are passed through as-is for datetime. The results of
      // these test cases are exactly the same as the input format string.
      test_cases.push_back(CivilTimeTestCase(
          {{String(common_test.format_string), common_datetime}},
          String(common_test.format_string)));
    } else {
      if (common_test.format_string == "%s") {
        // This particular test case is ignored. Instead, the "%s" format
        // element will be covered in a separate test case later.
        continue;
      } else {
        test_cases.push_back(CivilTimeTestCase(
            {{String(common_test.format_string), common_datetime}},
            String(common_test.expected_result)));
      }
    }
  }

  test_cases.push_back(CivilTimeTestCase(
      {{String("%s"), common_datetime}, String("1436317323")}));

  return PrepareCivilTimeTestCaseAsFunctionTestCall(test_cases,
                                                    "format_datetime");
}

std::vector<FunctionTestCall> GetFunctionTestsFormatTime() {
  static const Value common_time = TimeMicros(1, 2, 3, 456789);
  std::vector<CivilTimeTestCase> test_cases = {
      // NULL handling
      {{{NullString(), common_time}}, NullString()},
      {{{String(""), NullTime()}}, NullString()},
      {{{NullString(), NullTime()}}, NullString()},

      // hour 00-23
      {{{String("%H"), TimeMicros(23, 45, 1, 0)}}, String("23")},
      {{{String("%H"), TimeMicros(1, 45, 1, 0)}}, String("01")},

      // hour 0-23
      {{{String("%k"), TimeMicros(23, 45, 1, 0)}}, String("23")},
      {{{String("%k"), TimeMicros(1, 45, 1, 0)}}, String(" 1")},

      // hour 01-12
      {{{String("%I"), TimeMicros(23, 45, 1, 0)}}, String("11")},
      {{{String("%I"), TimeMicros(1, 45, 1, 0)}}, String("01")},
      {{{String("%I"), TimeMicros(0, 45, 1, 0)}}, String("12")},

      // hour 1-12
      {{{String("%l"), TimeMicros(23, 45, 1, 0)}}, String("11")},
      {{{String("%l"), TimeMicros(1, 45, 1, 0)}}, String(" 1")},
      {{{String("%l"), TimeMicros(0, 45, 1, 0)}}, String("12")},

      // Test the subsecond '%E*S' pattern.
      {{{String("%E*S"), TimeMicros(0, 0, 1, 234567)}}, String("01.234567")},
      {{{String("%E*S"), TimeMicros(0, 0, 1, 234560)}}, String("01.23456")},
      {{{String("%E*S"), TimeMicros(0, 0, 1, 234500)}}, String("01.2345")},
      {{{String("%E*S"), TimeMicros(0, 0, 1, 234000)}}, String("01.234")},
      {{{String("%E*S"), TimeMicros(0, 0, 1, 230000)}}, String("01.23")},
      {{{String("%E*S"), TimeMicros(0, 0, 1, 200000)}}, String("01.2")},
      {{{String("%E*S"), TimeMicros(0, 0, 1, 0)}}, String("01")},

      {{{String("%E9S"), TimeMicros(0, 0, 1, 123456)}}, String("01.123456000")},

      // Datetime range boundary testing
      {{{String("%H:%M:%E6S"), TimeMicros(0, 0, 0, 0)}},
       String("00:00:00.000000")},
      {{{String("%H:%M:%E6S"), TimeMicros(23, 59, 59, 999999)}},
       String("23:59:59.999999")},

      // %Y does not work for Time, nor should %4Y.
      {{{String("%Y"), common_time}}, String("%Y")},
      {{{String("%4Y"), common_time}}, String("%4Y")},
      {{{String("%E4Y"), common_time}}, String("%E4Y")},
      {{{String("%EY"), common_time}}, String("%EY")},
      {{{String("%OY"), common_time}}, String("%OY")},

      // %Q does not work for Time
      {{{String("%Q"), common_time}}, String("%Q")},

      // Day of year does not work for Time
      {{{String("%j"), common_time}}, String("%j")},
      {{{String("%J"), common_time}}, String("%J")},

      // Week does not work for Time
      {{{String("%U"), common_time}}, String("%U")},
      {{{String("%V"), common_time}}, String("%V")},
      {{{String("%W"), common_time}}, String("%W")},

      // Day of week does not work for time.
      {{{String("%u"), common_time}}, String("%u")},
      {{{String("%w"), common_time}}, String("%w")},
      {{{String("%A"), common_time}}, String("%A")},
      {{{String("%a"), common_time}}, String("%a")},
  };

  // Adds applicable common tests
  for (const auto& common_test : GetFormatDateTimestampCommonTests()) {
    if ((common_test.parts == TEST_FORMAT_TIME ||
         common_test.parts == TEST_FORMAT_ANY) &&
        // "%s" is not a valid format element for TIME
        common_test.format_string != "%s") {
      test_cases.push_back(
          CivilTimeTestCase({{String(common_test.format_string), common_time}},
                            String(common_test.expected_result)));
    } else {
      test_cases.push_back(
          CivilTimeTestCase({{String(common_test.format_string), common_time}},
                            String(common_test.format_string)));
    }
  }

  return PrepareCivilTimeTestCaseAsFunctionTestCall(test_cases,
                                                    "format_time");
}

// Represents the structure of a common test that can be used for testing
// parsing different DATE/TIME types depending on the format <parts>
// (see comments of TestFormatDateTimeType for the exact meaning of each type).
//
// When testing a DATE/TIME TYPE, if the TYPE supports the specified <parts>,
// then:
//   - Parse input_string using format_string with PARSE_<TYPE>.
//   - Cast expected_result to TYPE using the default format.
//   - Expect that they give the same TYPE value.
// If TYPE does not support the specified <parts>, then try to parse
// input_string using format_string and expect an error.
//
// Because the effective formatting elements for parse_date() and parse_time()
// are mutually exclusive, test cases with TEST_FORMAT_ANY must not have a
// format_string with any meaningful formatting element if the expected_result
// does not indicate an error. It is safe to assume the output of such cases
// are always initialized from 1970-01-01 00:00:00.0
struct ParseDateTimestampCommonTest {
  std::string format_string;      // The format string specified.
  TestFormatDateTimeParts parts;  // The format parts need to be supported.
  std::string input_string;       // The input date/time string to parse.
  // Note that for TIME tests to be reused by TIMESTAMP, the test driver
  // needs some special handling to add a date part to the <expected_result>.
  std::string
      expected_result;  // The expected result, empty indicates an error.
};

// Helper for ParseTimestamp invalid test result definitions.
#define EXPECT_ERROR ""

static bool IsExpectedError(absl::string_view result) { return result.empty(); }

// For the test cases returned by this function, they will have microsecond
// precision if the subsecond part is relevant.
static std::vector<ParseDateTimestampCommonTest>
GetParseDateTimestampCommonTests() {
  const char kTestExpected19700101[] = "1970-01-01";
  const char* kTestExpectedDefault = kTestExpected19700101;
  const char kTestExpected20000101[] = "2000-01-01";
  const char kTestExpected20000401[] = "2000-04-01";
  const char kTestExpected20010203[] = "2001-02-03";
  const char kTestExpected01123456[] = "00:00:01.123456";

  // Any unspecified field is initialized from 1970-01-01 00:00:00 in the
  // default time zone.
  std::vector<ParseDateTimestampCommonTest> tests({
      {"", TEST_FORMAT_ANY, "", kTestExpectedDefault},
      {"%y", TEST_FORMAT_DATE, "7", "2007-01-01"},
      {"%m", TEST_FORMAT_DATE, "8", "1970-08-01"},
      {"%d", TEST_FORMAT_DATE, "9", "1970-01-09"},

      {"%Y-%m-%d %H:%M:%S", TEST_FORMAT_DATE_AND_TIME, "1970-01-01 00:00:00",
       "1970-01-01 00:00:00"},

      // Test different ordering of year/month/day parts, they should all
      // give the same result.
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2001-02-03", kTestExpected20010203},
      {"%Y-%d-%m", TEST_FORMAT_DATE, "2001-03-02", kTestExpected20010203},
      {"%m-%Y-%d", TEST_FORMAT_DATE, "02-2001-03", kTestExpected20010203},
      {"%m-%d-%Y", TEST_FORMAT_DATE, "02-03-2001", kTestExpected20010203},
      {"%d-%m-%Y", TEST_FORMAT_DATE, "03-02-2001", kTestExpected20010203},
      {"%d-%Y-%m", TEST_FORMAT_DATE, "03-2001-02", kTestExpected20010203},
      {"%y-%m-%d", TEST_FORMAT_DATE, "01-02-03", kTestExpected20010203},
      {"%y-%d-%m", TEST_FORMAT_DATE, "01-03-02", kTestExpected20010203},
      {"%m-%y-%d", TEST_FORMAT_DATE, "02-01-03", kTestExpected20010203},
      {"%m-%d-%y", TEST_FORMAT_DATE, "02-03-01", kTestExpected20010203},
      {"%d-%m-%y", TEST_FORMAT_DATE, "03-02-01", kTestExpected20010203},
      {"%d-%y-%m", TEST_FORMAT_DATE, "03-01-02", kTestExpected20010203},
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2001-2-3", kTestExpected20010203},
      {"%y-%m-%d", TEST_FORMAT_DATE, "1-2-3", kTestExpected20010203},

      {"%Y/%m/%d", TEST_FORMAT_DATE, "2001/02/03", kTestExpected20010203},
      {"%y/%m/%d", TEST_FORMAT_DATE, "01/02/03", kTestExpected20010203},
      {"%Ya%mB%d", TEST_FORMAT_DATE, "2001a02B03", kTestExpected20010203},
      {"%Y %m %d", TEST_FORMAT_DATE, "2001 02 03", kTestExpected20010203},

      {"%Y-%m-%d", TEST_FORMAT_DATE, "1-2-3", "0001-02-03"},
      {"%Y-%m-%d", TEST_FORMAT_DATE, "11-2-3", "0011-02-03"},
      {"%Y-%m-%d", TEST_FORMAT_DATE, "111-2-3", "0111-02-03"},
      {"%Y-%m-%d", TEST_FORMAT_DATE, "1111-2-3", "1111-02-03"},

      // Non-matching separator.
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2000 08 09", EXPECT_ERROR},
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2000/08/09", EXPECT_ERROR},

      // Date does not exist.
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2001-02-30", EXPECT_ERROR},
      {"%Y-%m-%d", TEST_FORMAT_DATE, "2001-09-31", EXPECT_ERROR},

      {"%Y/%m/%d %H:%M:%S%z", TEST_FORMAT_TIMEZONE, "2015/06/08 08:08:00-0700",
       "2015-06-08 08:08:00-07"},
      {"%Y/%m/%d %T%z", TEST_FORMAT_TIMEZONE, "2015/06/08 08:08:00-0700",
       "2015-06-08 08:08:00-07"},

      // Min/max boundaries.
      {"%Y-%m-%d %H:%M:%E6S", TEST_FORMAT_DATE_AND_TIME,
       "9999-12-31 23:59:59.999999", "9999-12-31 23:59:59.999999"},
      {"%Y-%m-%d %H:%M:%E6S", TEST_FORMAT_DATE_AND_TIME,
       "0001-01-01 00:00:00.000000", "0001-01-01 00:00:00.000000"},

      {"%Ya%m", TEST_FORMAT_DATE, "2000a09", "2000-09-01"},

      // %C - century 00-100.  A %C value f '0' or '100' by itself is invalid
      //      since the corresponding timestamp is out of the valid GoogleSQL
      //      range.
      {"%C", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%C", TEST_FORMAT_DATE, "01", "0100-01-01"},
      {"%C", TEST_FORMAT_DATE, "10", "1000-01-01"},
      {"%C", TEST_FORMAT_DATE, "99", "9900-01-01"},
      {"%C", TEST_FORMAT_DATE, "100", EXPECT_ERROR},

      {"%ma", TEST_FORMAT_DATE, "-a", EXPECT_ERROR},

      // Combinations of am/pm with both 12-based and 24-based hours.
      // When using %H (hours 00-23), the am/pm setting is IGNORED.
      {"%H:%M %p", TEST_FORMAT_TIME, "12:01 AM", "12:01:00"},
      {"%H:%M %p", TEST_FORMAT_TIME, "12:02 pm", "12:02:00"},
      {"%H:%M %p", TEST_FORMAT_TIME, "13:01 AM", "13:01:00"},
      {"%H:%M %p", TEST_FORMAT_TIME, "13:02 pm", "13:02:00"},

      // When using %I or %l (hours 1-12), the am/pm setting is RESPECTED.
      // %p takes either lower or uppercase am/pm.
      {"%I:%M %p", TEST_FORMAT_TIME, "2:01 AM", "02:01:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, "2:02 pm", "14:02:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, " 2:01 AM", "02:01:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, " 2:02 pm", "14:02:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, "02:01 aM", "02:01:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, "02:02 Pm", "14:02:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, "12:01 AM", "00:01:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, "12:02 pm", "12:02:00"},
      {"%I:%M %p", TEST_FORMAT_TIME, "13:01 AM", EXPECT_ERROR},
      {"%I:%M %p", TEST_FORMAT_TIME, "13:02 pm", EXPECT_ERROR},
      {"%I:%M %p", TEST_FORMAT_TIME, "A:01 AM", EXPECT_ERROR},

      {"%l:%M %p", TEST_FORMAT_TIME, "2:01 Am", "02:01:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, "2:02 pM", "14:02:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, " 2:01 AM", "02:01:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, " 2:02 pm", "14:02:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, "02:01 Am", "02:01:00"},
      // The following test covers b/119571179.
      {"%l:%M %p", TEST_FORMAT_TIME, "02:02 pM", "14:02:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, "12:01 AM", "00:01:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, "12:02 pm", "12:02:00"},
      {"%l:%M %p", TEST_FORMAT_TIME, "13:01 AM", EXPECT_ERROR},
      {"%l:%M %p", TEST_FORMAT_TIME, "13:02 pm", EXPECT_ERROR},
      {"%l:%M %p", TEST_FORMAT_TIME, "13:02 ZM", EXPECT_ERROR},

      // Test timestamp boundaries expressed in non-UTC timezones.
      // These should work because the corresponding timestamp value
      // is within supported bounds.
      {"%Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "0000-12-31 16:08:00.000000-07:52", "0001-01-01 00:00:00+0"},
      {"%Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "10000-01-01 06:29:59.999999+06:30", "9999-12-31 23:59:59.999999+0"},

      // %C/%EC (century) also works for values 00 and 100.
      {"%C-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "00-12-31 16:08:00.000000-07:52", "0001-01-01 00:00:00+0"},
      {"%C-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "100-01-01 06:29:59.999999+06:30", "9999-12-31 23:59:59.999999+0"},
      {"%EC-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "00-12-31 16:08:00.000000-07:52", "0001-01-01 00:00:00+0"},
      {"%EC-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "100-01-01 06:29:59.999999+06:30", "9999-12-31 23:59:59.999999+0"},

      // %4Y works for year 0000, but not 10000 because %4Y requires exactly
      // 4 digits.
      {"%4Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "0000-12-31 23:00:00.000000-01:00", EXPECT_ERROR},
      {"%4Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "10000-01-01 06:29:59.999999+06:30", EXPECT_ERROR},

      {"%Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "0001-01-01 00:00:00.000000+01", EXPECT_ERROR},
      {"%Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "9999-12-31 23:59:59.999999-01", EXPECT_ERROR},

      // A couple of tests with UTF8 characters, they work fine.
      {"%%ыфщ%Yыфщ", TEST_FORMAT_DATE, "%ыфщ2015ыфщ", "2015-01-01"},
      {"%%ы%Yф%mщ%dы%Hф%Mщ%S", TEST_FORMAT_DATE_AND_TIME,
       "%ы2015ф12щ31ы12ф34щ56", "2015-12-31 12:34:56"},
      {"Мо%Yша Пас%mуманс%dкий, школа 23", TEST_FORMAT_DATE,
       "Мо1969ша Пас09уманс03кий, школа 23", "1969-09-03"},

      // Embedded null bytes cause problems.  In theory, this first one
      // should work since both the format and the timestamp string
      // have corresponding null bytes used as separators in the same
      // relative position.  However, note that UTF8 character strings
      // cannot have embedded null bytes in them, so assuming valid
      // GoogleSQL UTF8 strings this is not an actual issue.  If we
      // ever allow ParseTimestamp on bytes, then we will need to
      // address this.
      {"%Y\0%m\0%d", TEST_FORMAT_DATE, "2011\012\031", EXPECT_ERROR},
      // This should fail since there is no month in the timestamp string.
      {"%Y\0%m", TEST_FORMAT_DATE, "2012", "2012-01-01"},
      // This should fail since there is no null byte in the timestamp string.
      {"%Y\0", TEST_FORMAT_DATE, "2013", "2013-01-01"},
      // This should fail since there is an extra null byte in the timestamp.
      {"%Y", TEST_FORMAT_DATE, "2014\0", "2014-01-01"},
      // This should fail since there is no day in the timestamp string.
      // Note that the result does not include the month either.
      {"%Y\0%m\0%d", TEST_FORMAT_DATE, "2015\012", "2015-01-01"},
      // This should fail since there is an extra day in the timestamp string.
      {"%Y\0%m", TEST_FORMAT_DATE, "2016\012\031", EXPECT_ERROR},

      // Day names consume exactly the name or abbreviated name.
      {"%a1", TEST_FORMAT_DATE, "Tue1", kTestExpected19700101},
      {"%As", TEST_FORMAT_DATE, "Tues", kTestExpected19700101},
      {"%A1", TEST_FORMAT_DATE, "Tuesday1", kTestExpected19700101},
      {"%as", TEST_FORMAT_DATE, "Tuesdays", kTestExpected19700101},
      {"%as", TEST_FORMAT_DATE, "Wednesdays", kTestExpected19700101},
      {"%a%d", TEST_FORMAT_DATE, "Wednesday1", kTestExpected19700101},

      // Month names consume exactly the name or abbreviated name.
      {"%b1", TEST_FORMAT_DATE, "Jan1", kTestExpected19700101},
      {"%Bu", TEST_FORMAT_DATE, "Janu", kTestExpected19700101},
      {"%b1", TEST_FORMAT_DATE, "January1", kTestExpected19700101},
      {"%Bu", TEST_FORMAT_DATE, "Januaryu", kTestExpected19700101},
      {"%bu", TEST_FORMAT_DATE, "Septemberu", "1970-09-01"},
      {"%b%d", TEST_FORMAT_DATE, "September02", "1970-09-02"},

      {"%a%b%Y", TEST_FORMAT_DATE, "WednesdayJan1970", kTestExpected19700101},

      // %C, %EC - century.  Consumes the next 1-3 digits.
      {"%C", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%C", TEST_FORMAT_DATE, "1", "0100-01-01"},
      {"%C", TEST_FORMAT_DATE, "11", "1100-01-01"},
      {"%C", TEST_FORMAT_DATE, "0011", EXPECT_ERROR},

      {"%Ca", TEST_FORMAT_DATE, "1a", "0100-01-01"},
      {"%Ca", TEST_FORMAT_DATE, "11a", "1100-01-01"},

      {"%C", TEST_FORMAT_DATE, "01", "0100-01-01"},
      {"%C", TEST_FORMAT_DATE, "001", "0100-01-01"},
      {"%C", TEST_FORMAT_DATE, "0001", EXPECT_ERROR},

      // %C consumes all these digits, even though if it consumed all but the
      // last then this would work.
      {"%C1", TEST_FORMAT_DATE, "11", EXPECT_ERROR},
      {"%C0", TEST_FORMAT_DATE, "100", EXPECT_ERROR},

      {"%EC", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%EC", TEST_FORMAT_DATE, "1", "0100-01-01"},
      {"%EC", TEST_FORMAT_DATE, "01", "0100-01-01"},
      {"%EC", TEST_FORMAT_DATE, "11", "1100-01-01"},
      {"%EC", TEST_FORMAT_DATE, "100", EXPECT_ERROR},

      // %m consumes up to 1-2 numeric digits (2 if possible).
      // Leading zeros count towards the consumed digits so neither of
      // these work.
      {"%m", TEST_FORMAT_DATE, "001", EXPECT_ERROR},
      {"%m", TEST_FORMAT_DATE, "012", EXPECT_ERROR},

      // The first %m consumes the first two digits, the second %m
      // consumes the rest.
      {"%m%m", TEST_FORMAT_DATE, "112", "1970-02-01"},
      {"%m%m", TEST_FORMAT_DATE, "1112", "1970-12-01"},
      {"%m%m", TEST_FORMAT_DATE, "0308", "1970-08-01"},
      {"%m%m", TEST_FORMAT_DATE, "0308", "1970-08-01"},

      // %m consumes both digits even though the result value is invalid.
      {"%m", TEST_FORMAT_DATE, "99", EXPECT_ERROR},

      // %m greedily consumes the first two digits regardless of their value.
      // Each of these would be ok if the first %m consumed only one digit with
      // the second %m consuming the remainder.
      {"%m%m", TEST_FORMAT_DATE, "23", EXPECT_ERROR},
      {"%m%m", TEST_FORMAT_DATE, "212", EXPECT_ERROR},
      {"%m%m", TEST_FORMAT_DATE, "912", EXPECT_ERROR},

      // We can run format elements together, and successfully parse the string
      // as long as the maximum digits for each element are present in the
      // string.
      {"%E4Y%m%d", TEST_FORMAT_DATE, "20050308", "2005-03-08"},
      // Same test as previous, succeeds because %Y will only consume 4 digits
      // if it is immediately followed by another format element.
      {"%Y%m%d", TEST_FORMAT_DATE, "20050308", "2005-03-08"},
      // Same test as previous, but this fails because %Y won't consume a
      // 0-prefixed 5 digit string if immediately followed by another format
      // element.
      {"%Y%m%d", TEST_FORMAT_DATE, "020050308", EXPECT_ERROR},
      // Same tests as the previous 2, but validateing behavior with %C.
      {"%C%m%d", TEST_FORMAT_DATE, "200308", "2000-03-08"},
      {"%C%m%d", TEST_FORMAT_DATE, "0200308", EXPECT_ERROR},
      // '%Y' will not accept a 5th digit when format elements are run together,
      // even when doing so would parse as a valid 5 digit year.
      // There is an earlier version of this test that starts with %Y-%m-%d that
      // asserts a successful parse.
      {"%Y%m%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "100000101 06:29:59.999999+06:30", EXPECT_ERROR},
      // '%C' will not accept a 3rd digit when format elements are run together,
      // event when doing so whould parse as a valid 3 digit century.
      // There is an earlier version of this test that starts with %C-%m-%d that
      // asserts a successful parse.
      {"%C%m%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "1000101 06:29:59.999999+06:30", EXPECT_ERROR},

      // %z - time zone offset of form [+/-]HHMM (2 required hour digits and
      //      2 optional minute digits).
      {"%z", TEST_FORMAT_TIMEZONE, "+0", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "+000", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "+00000", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "+000000", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "-1401", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "-1400", "1970-01-01 00:00:00-14"},
      {"%z", TEST_FORMAT_TIMEZONE, "-0100", "1970-01-01 00:00:00-01"},
      {"%z", TEST_FORMAT_TIMEZONE, "-01", "1970-01-01 00:00:00-01"},
      {"%z", TEST_FORMAT_TIMEZONE, "-0060", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "-005", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "00", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "0500", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "+00", "1970-01-01 00:00:00+00"},
      {"%z", TEST_FORMAT_TIMEZONE, "+0000", "1970-01-01 00:00:00+00"},
      {"%z", TEST_FORMAT_TIMEZONE, "+005", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "+0059", "1970-01-01 00:00:00+00:59"},
      {"%z", TEST_FORMAT_TIMEZONE, "+0060", EXPECT_ERROR},
      {"%z", TEST_FORMAT_TIMEZONE, "+01", "1970-01-01 00:00:00+01"},
      {"%z", TEST_FORMAT_TIMEZONE, "+0100", "1970-01-01 00:00:00+01"},
      {"%z", TEST_FORMAT_TIMEZONE, "+12", "1970-01-01 00:00:00+12"},
      {"%z", TEST_FORMAT_TIMEZONE, "+1200", "1970-01-01 00:00:00+12"},
      {"%z", TEST_FORMAT_TIMEZONE, "+1400", "1970-01-01 00:00:00+14"},
      {"%z", TEST_FORMAT_TIMEZONE, "+1401", EXPECT_ERROR},

      // %Z - time zone name.
      {"%Z", TEST_FORMAT_TIMEZONE, "+0", "1970-01-01 00:00:00+00"},
      {"%Z", TEST_FORMAT_TIMEZONE, "1970-01-01 00:00:00+00"},
      {"%Z", TEST_FORMAT_TIMEZONE, "invalid_name", EXPECT_ERROR},
      {"%Z", TEST_FORMAT_TIMEZONE, "Z", EXPECT_ERROR},

      // %Ez - RFC3339-compatible numeric time zone of form '[+/-]hh[:mm]', with
      //       exactly two hours digits and two optional minutes digits.
      //       GoogleSQL supports the range of -14:00 to +14:00.
      {"%Ez", TEST_FORMAT_TIMEZONE, "-14:01", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "-14:00", "1970-01-01 00:00:00-14"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "-00:60", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "-01:00", "1970-01-01 00:00:00-01"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "-1:00", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "-01", "1970-01-01 00:00:00-01"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+00", "1970-01-01 00:00:00+00"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+00:00", "1970-01-01 00:00:00+00"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+00:59", "1970-01-01 00:00:00+00:59"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+0:59", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+:59", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+00:60", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+01", "1970-01-01 00:00:00+01"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+1:00", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+01:00", "1970-01-01 00:00:00+01"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+01:0", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+1:0", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+12:0", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "12:00", EXPECT_ERROR},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+14:00", "1970-01-01 00:00:00+14"},
      {"%Ez", TEST_FORMAT_TIMEZONE, "+14:01", EXPECT_ERROR},

      // Beginning and trailing whitespaces in the timestamp string are
      // ignored.  A space, '%t', or '%n' in the format string matches
      // zero or more whitespaces in the timestamp string.
      //
      // The whitespace format elements are ' ', '%t', and '%n'.
      //
      // The currently supported whitespace characters in the timestamp string
      // are:
      //   space: ' '
      //   form feed: '\f'
      //   line feed: '\n'
      //   carriage return: '\r'
      //   horizontal tab: '\t'
      //   vertical tab: '\v'

      {"%Y", TEST_FORMAT_DATE, "2000 ", kTestExpected20000101},
      {"%Y", TEST_FORMAT_DATE, " 2000", kTestExpected20000101},

      {"%Y ", TEST_FORMAT_DATE, "2000", kTestExpected20000101},
      {" %Y ", TEST_FORMAT_DATE, "2000", kTestExpected20000101},

      {"%Y ", TEST_FORMAT_DATE, "2000 ", kTestExpected20000101},
      {"%Y ", TEST_FORMAT_DATE, "2000   ", kTestExpected20000101},
      {" %Y", TEST_FORMAT_DATE, " 2000", kTestExpected20000101},
      {" %Y", TEST_FORMAT_DATE, "   2000", kTestExpected20000101},

      {"  %Y  ", TEST_FORMAT_DATE, "2000", kTestExpected20000101},
      {"  %Y  ", TEST_FORMAT_DATE, " 2000 ", kTestExpected20000101},
      {"  %Y  ", TEST_FORMAT_DATE, "         2000         ",
       kTestExpected20000101},

      {"%Y %m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y %m", TEST_FORMAT_DATE, "2000    04", kTestExpected20000401},

      {"%Y- -%m", TEST_FORMAT_DATE, "2000--04", kTestExpected20000401},
      {"%Y- -%m", TEST_FORMAT_DATE, "2000- -04", kTestExpected20000401},
      {"%Y- -%m", TEST_FORMAT_DATE, "2000-  -04", kTestExpected20000401},

      // If there is no space in the format string, there cannot be one in
      // the timestamp string.  The following is an error.
      {"%Y--%m", TEST_FORMAT_DATE, "2000- -04", EXPECT_ERROR},

      // Leading/trailing whitespace is ignored.
      {" \f\n\r\t\v%Y\v\t\r\n\f ", TEST_FORMAT_DATE, "2000 ",
       kTestExpected20000101},

      {" %Y ", TEST_FORMAT_DATE, " \f\n\r\t\v2000\v\t\r\n\f ",
       kTestExpected20000101},
      {"%t%Y%t", TEST_FORMAT_DATE, " \f\n\r\t\v2000\v\t\r\n\f ",
       kTestExpected20000101},
      {"%n%Y%n", TEST_FORMAT_DATE, " \f\n\r\t\v2000\v\t\r\n\f ",
       kTestExpected20000101},

      {"%Y %m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y\f%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y\n%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y\r%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y\t%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y\v%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},
      {"%Y\v%m", TEST_FORMAT_DATE, "2000  04", kTestExpected20000401},
      {"%Y\v%m", TEST_FORMAT_DATE, "2000   04", kTestExpected20000401},
      {"%Y\v%m", TEST_FORMAT_DATE, "2000 \f \n \r \t \v 04",
       kTestExpected20000401},

      // The white space elements ' ', '%t', and '%n' are all equivalent.
      {"%Y %m", TEST_FORMAT_DATE, "2000 \f\n\r\t\v04", kTestExpected20000401},
      {"%Y%t%m", TEST_FORMAT_DATE, "2000 \f\n\r\t\v04", kTestExpected20000401},
      {"%Y%n%m", TEST_FORMAT_DATE, "2000 \f\n\r\t\v04", kTestExpected20000401},

      {"%Y\f\n\r\t\v%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},

      {"%Y %m", TEST_FORMAT_DATE, "2000\f04", kTestExpected20000401},
      {"%Y\f%m", TEST_FORMAT_DATE, "2000\n04", kTestExpected20000401},
      {"%Y\n%m", TEST_FORMAT_DATE, "2000\r04", kTestExpected20000401},
      {"%Y\r%m", TEST_FORMAT_DATE, "2000\t04", kTestExpected20000401},
      {"%Y\t%m", TEST_FORMAT_DATE, "2000\v04", kTestExpected20000401},
      {"%Y\v%m", TEST_FORMAT_DATE, "2000 04", kTestExpected20000401},

      {"%Y %m", TEST_FORMAT_DATE, "2000\f\n\r\t\v04", kTestExpected20000401},
      {"%Y\f%m", TEST_FORMAT_DATE, "2000\f\n\r\t\v04", kTestExpected20000401},
      {"%Y\n%m", TEST_FORMAT_DATE, "2000\f\n\r\t\v04", kTestExpected20000401},
      {"%Y\r%m", TEST_FORMAT_DATE, "2000\f\n\r\t\v04", kTestExpected20000401},
      {"%Y\t%m", TEST_FORMAT_DATE, "2000\f\n\r\t\v04", kTestExpected20000401},
      {"%Y\v%m", TEST_FORMAT_DATE, "2000\f\n\r\t\v04", kTestExpected20000401},

      // Test the 'E' and 'O' modifiers, i.e., %Ec, %OH.
      // These are basic example tests of the modifiers.  More comprehensive
      // tests for equivalence to their non-modified counterparts (assuming
      // the en_US locale) appear above.
      {"%Ec", TEST_FORMAT_DATE_AND_TIME, "Tuesday Feb 28 12:34:56 2006",
       "2006-02-28 12:34:56"},
      {"%EC", TEST_FORMAT_DATE, "01", "0100-01-01"},        // century
      {"%Ex", TEST_FORMAT_DATE, "01/02/03", "2003-01-02"},  // date
      {"%EX", TEST_FORMAT_TIME, "00:00:01", "00:00:01"},    // time
      // two digit year
      {"%Ey", TEST_FORMAT_DATE, "07", "2007-01-01"},
      // four digit year
      {"%EY", TEST_FORMAT_DATE, "2000", "2000-01-01"},
      {"%Od", TEST_FORMAT_DATE, "02", "1970-01-02"},  // day of month
      {"%Oe", TEST_FORMAT_DATE, "03", "1970-01-03"},  // day of month
      // hour (00-23)
      {"%OH", TEST_FORMAT_TIME, "23", "23:00:00"},
      // hour (01-12)
      {"%OI", TEST_FORMAT_TIME, "11", "11:00:00"},
      {"%Om", TEST_FORMAT_DATE, "11", "1970-11-01"},  // month
      {"%OM", TEST_FORMAT_TIME, "59", "00:59:00"},    // minute
      {"%OS", TEST_FORMAT_TIME, "59", "00:00:59"},    // seconds
      {"%Ou", TEST_FORMAT_DATE, "7", "1970-01-01"},   // day of week
      {"%OU", TEST_FORMAT_DATE, "52", "1970-12-27"},  // week of year
      {"%OU", TEST_FORMAT_DATE, "53", EXPECT_ERROR},  // week of year
      {"%OV", TEST_FORMAT_DATE, "52", "1970-01-01"},  // week of year
      {"%OV", TEST_FORMAT_DATE, "53", "1970-01-01"},  // week of year
      {"%Ow", TEST_FORMAT_DATE, "6", "1970-01-01"},   // day of week
      {"%OW", TEST_FORMAT_DATE, "52", "1970-12-28"},  // week of year
      {"%OW", TEST_FORMAT_DATE, "53", EXPECT_ERROR},  // week of year
      {"%Oy", TEST_FORMAT_DATE, "09", "2009-01-01"},  // two digit year

      // Invalid format elements provide an error.
      {"%+", TEST_FORMAT_ANY, "%+", EXPECT_ERROR},
      {"%E", TEST_FORMAT_ANY, "%E", EXPECT_ERROR},
      {"%Ec", TEST_FORMAT_ANY, "%E", EXPECT_ERROR},
      {"%EX", TEST_FORMAT_ANY, "%E", EXPECT_ERROR},
      {"a%Eb", TEST_FORMAT_ANY, "a%Eb", EXPECT_ERROR},
      {"%f", TEST_FORMAT_ANY, "%f", EXPECT_ERROR},
      {"%i", TEST_FORMAT_ANY, "%i", EXPECT_ERROR},
      {"%J", TEST_FORMAT_ANY, "%J", EXPECT_ERROR},
      {"%K", TEST_FORMAT_ANY, "%K", EXPECT_ERROR},
      {"%L", TEST_FORMAT_ANY, "%L", EXPECT_ERROR},
      {"%N", TEST_FORMAT_ANY, "%N", EXPECT_ERROR},
      {"%O", TEST_FORMAT_ANY, "%O", EXPECT_ERROR},
      {"%OH", TEST_FORMAT_ANY, "%OH", EXPECT_ERROR},
      {"%OI", TEST_FORMAT_ANY, "%OI", EXPECT_ERROR},
      {"%Om", TEST_FORMAT_ANY, "%O", EXPECT_ERROR},
      {"%o", TEST_FORMAT_ANY, "%o", EXPECT_ERROR},
      {"%q", TEST_FORMAT_ANY, "%q", EXPECT_ERROR},
      {"%v", TEST_FORMAT_ANY, "%v", EXPECT_ERROR},
      {"%1", TEST_FORMAT_ANY, "%1", EXPECT_ERROR},
      {"%-", TEST_FORMAT_ANY, "%-", EXPECT_ERROR},
      {"% ", TEST_FORMAT_ANY, "% ", EXPECT_ERROR},
      {"%", TEST_FORMAT_ANY, "%", EXPECT_ERROR},
      {"%-S", TEST_FORMAT_ANY, "9", EXPECT_ERROR},
      {"%E9S", TEST_FORMAT_ANY, "a", EXPECT_ERROR},
      {"%E1s", TEST_FORMAT_ANY, "1", EXPECT_ERROR},

      // %s - seconds since GoogleSQL epoch.  Negative values are allowed to get
      //      to pre-epoch timestamps.
      {"%s", TEST_FORMAT_DATE_AND_TIME, "-62135596801", EXPECT_ERROR},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "-62135596800", "0001-01-01 00:00:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "-60", "1969-12-31 23:59:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "-1", "1969-12-31 23:59:59"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "0", "1970-01-01 00:00:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "00", "1970-01-01 00:00:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "1", "1970-01-01 00:00:01"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "60", "1970-01-01 00:01:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "3600", "1970-01-01 01:00:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "86400", "1970-01-02 00:00:00"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "253402300799", "9999-12-31 23:59:59"},
      {"%s", TEST_FORMAT_DATE_AND_TIME, "253402300800", EXPECT_ERROR},

      {"%b", TEST_FORMAT_DATE, "sunday", EXPECT_ERROR},
      {"%b", TEST_FORMAT_DATE, "a", EXPECT_ERROR},
      {"%b", TEST_FORMAT_DATE, "1", EXPECT_ERROR},

      {"%bch", TEST_FORMAT_DATE, "march", EXPECT_ERROR},
      {"%bch", TEST_FORMAT_DATE, "marchch", "1970-03-01"},
      {"%bch", TEST_FORMAT_DATE, "janch", "1970-01-01"},
      {"%bch", TEST_FORMAT_DATE, "januarych", "1970-01-01"},

      // Periods not allowed in am/pm.
      {"%p", TEST_FORMAT_TIME, "a.m.", EXPECT_ERROR},
      {"%p", TEST_FORMAT_TIME, "P.M.", EXPECT_ERROR},
      {"%P", TEST_FORMAT_TIME, "a.m.", EXPECT_ERROR},
      {"%P", TEST_FORMAT_TIME, "P.M.", EXPECT_ERROR},

      {"%Y9", TEST_FORMAT_DATE, "009999", "0999-01-01"},
      {"%EY9", TEST_FORMAT_DATE, "009999", "0999-01-01"},

      // %E4Y - Four-character years.  Requires exactly 4 digits.
      //        Leading zeros are significant.
      {"%E4Y", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "1", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "10", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "100", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "0000", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "0001", "0001-01-01"},
      {"%E4Y", TEST_FORMAT_DATE, "0999", "0999-01-01"},
      {"%E4Y", TEST_FORMAT_DATE, "1000", "1000-01-01"},
      {"%E4Y", TEST_FORMAT_DATE, "01000", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "1970", "1970-01-01"},
      {"%E4Y", TEST_FORMAT_DATE, "2015", "2015-01-01"},
      {"%E4Y", TEST_FORMAT_DATE, "9999", "9999-01-01"},
      {"%E4Y", TEST_FORMAT_DATE, "09999", EXPECT_ERROR},
      {"%E4Y", TEST_FORMAT_DATE, "10000", EXPECT_ERROR},

      // %E*S - seconds with 'full' fractional precision.
      //        As many digits are parsed and consumed as are present.
      // The tests for more than 6 digits are in
      // GetParseNanoAndPicoTimestampSensitiveTests.
      {"%E*S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E*S", TEST_FORMAT_TIME, "1.", EXPECT_ERROR},
      {"%E*S", TEST_FORMAT_TIME, "1.1", "00:00:01.100"},
      {"%E*S", TEST_FORMAT_TIME, "1.12", "00:00:01.120"},
      {"%E*S", TEST_FORMAT_TIME, "1.123", "00:00:01.123"},
      {"%E*S", TEST_FORMAT_TIME, "1.1234", "00:00:01.123400"},
      {"%E*S", TEST_FORMAT_TIME, "1.12345", "00:00:01.123450"},

      {"%E*S", TEST_FORMAT_TIME, "1.123456", kTestExpected01123456},

      // For %E#S, the valid is range is [0, 6] for micro precision and [0, 9]
      // for nano precision, [0-12] for pico precision. The test cases of [7,
      // 12] are in GetParseNanoAndPicoTimestampSensitiveTests.
      {"%E0S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E1S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E2S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E3S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E4S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E5S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E6S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E13S", TEST_FORMAT_TIME, "1", EXPECT_ERROR},

      // For %E#S, up to # digits are parsed and consumed.
      {"%E0S", TEST_FORMAT_TIME, "1", "00:00:01"},

      {"%E1S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E1S", TEST_FORMAT_TIME, "1.1", "00:00:01.1"},
      {"%E1S", TEST_FORMAT_TIME, "1.12", EXPECT_ERROR},

      {"%E2S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E2S", TEST_FORMAT_TIME, "1.1", "00:00:01.1"},
      {"%E2S", TEST_FORMAT_TIME, "1.12", "00:00:01.12"},
      {"%E2S", TEST_FORMAT_TIME, "1.123", EXPECT_ERROR},

      {"%E3S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E3S", TEST_FORMAT_TIME, "1.1", "00:00:01.100"},
      {"%E3S", TEST_FORMAT_TIME, "1.12", "00:00:01.120"},
      {"%E3S", TEST_FORMAT_TIME, "1.123", "00:00:01.123"},
      {"%E3S", TEST_FORMAT_TIME, "1.1234", EXPECT_ERROR},

      {"%E4S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E4S", TEST_FORMAT_TIME, "1.1", "00:00:01.100"},
      {"%E4S", TEST_FORMAT_TIME, "1.12", "00:00:01.120"},
      {"%E4S", TEST_FORMAT_TIME, "1.123", "00:00:01.123"},
      {"%E4S", TEST_FORMAT_TIME, "1.1234", "00:00:01.123400"},
      {"%E4S", TEST_FORMAT_TIME, "1.12345", EXPECT_ERROR},

      {"%E5S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E5S", TEST_FORMAT_TIME, "1.1", "00:00:01.100"},
      {"%E5S", TEST_FORMAT_TIME, "1.12", "00:00:01.120"},
      {"%E5S", TEST_FORMAT_TIME, "1.123", "00:00:01.123"},
      {"%E5S", TEST_FORMAT_TIME, "1.1234", "00:00:01.123400"},
      {"%E5S", TEST_FORMAT_TIME, "1.12345", "00:00:01.123450"},
      {"%E5S", TEST_FORMAT_TIME, "1.123456", EXPECT_ERROR},

      {"%E6S", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"%E6S", TEST_FORMAT_TIME, "1.1", "00:00:01.100"},
      {"%E6S", TEST_FORMAT_TIME, "1.12", "00:00:01.120"},
      {"%E6S", TEST_FORMAT_TIME, "1.123", "00:00:01.123"},
      {"%E6S", TEST_FORMAT_TIME, "1.1234", "00:00:01.123400"},
      {"%E6S", TEST_FORMAT_TIME, "1.12345", "00:00:01.123450"},
      {"%E6S", TEST_FORMAT_TIME, "1.123456", "00:00:01.123456"},
      {"%E6S", TEST_FORMAT_TIME, "1.1234567", EXPECT_ERROR},

      // This tests format elements that are parsed and (at least partially)
      // validated, but do not affect the result timestamp.  For instance format
      // elements %a and %A represent the weekday, and a valid weekday name must
      // be present in the input or an error is provided.  However, the weekday
      // name is not matched against the actual date to see if they are
      // consistent.  As another example, %j represents day of year and that
      // must be from 1-366, but we do not set the date from this day of year
      // nor do we validate that it matches the result date.

      // %A, %a - locale's full or abbreviated weekday name
      // GoogleSQL initially supports the en/US locale.
      {"%A", TEST_FORMAT_DATE, "Monday", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Tuesday", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Wednesday", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Thursday", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Friday", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Saturday", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Sunday", kTestExpected19700101},

      // The %A is not checked against an explicit date either.
      {"%Y-%m-%d %A", TEST_FORMAT_DATE, "2001-02-03 Monday",
       kTestExpected20010203},
      {"%Y-%m-%d %A", TEST_FORMAT_DATE, "2001-02-03 Tuesday",
       kTestExpected20010203},

      // Abbreviations also work.
      {"%A", TEST_FORMAT_DATE, "Sun", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Mon", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Tue", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Wed", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Thu", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Fri", kTestExpected19700101},
      {"%A", TEST_FORMAT_DATE, "Sat", kTestExpected19700101},

      // Partial does not work.
      {"%A", TEST_FORMAT_DATE, "Sunda", EXPECT_ERROR},

      // Invalid strings are an error.
      {"%A", TEST_FORMAT_DATE, "S", EXPECT_ERROR},
      {"%A", TEST_FORMAT_DATE, "1", EXPECT_ERROR},
      {"%A", TEST_FORMAT_DATE, "Blahday", EXPECT_ERROR},

      // %a - locale's abbreviated weekday name, these are for en_US.
      {"%a", TEST_FORMAT_DATE, "Mon", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Tue", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Wed", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Thu", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Fri", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Sat", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Sun", kTestExpected19700101},

      // Full weekday name also works.
      {"%a", TEST_FORMAT_DATE, "Sunday", kTestExpected19700101},
      {"%a", TEST_FORMAT_DATE, "Monday", kTestExpected19700101},

      // Wrong abbreviation with more or less letters results in error.
      {"%a", TEST_FORMAT_DATE, "Tues", EXPECT_ERROR},
      {"%a", TEST_FORMAT_DATE, "Thur", EXPECT_ERROR},
      {"%a", TEST_FORMAT_DATE, "W", EXPECT_ERROR},
      {"%a", TEST_FORMAT_DATE, "We", EXPECT_ERROR},
      {"%a", TEST_FORMAT_DATE, "Wedn", EXPECT_ERROR},
      {"%a", TEST_FORMAT_DATE, "Wedne", EXPECT_ERROR},
      {"%a", TEST_FORMAT_DATE, "Wednes", EXPECT_ERROR},

      // Invalid strings are an error.
      {"%a", TEST_FORMAT_DATE, "Blahday", EXPECT_ERROR},

      // %G - ISO 8601 year with century, out of range values fail.
      {"%G", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "a", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "-0", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "-1999", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "+0", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "+1999", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "10000", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "10001", EXPECT_ERROR},
      {"%G", TEST_FORMAT_DATE, "99999", EXPECT_ERROR},

      // Valid ISO Year values map to the first day of the ISO year.
      {"%G", TEST_FORMAT_DATE, "2", "0001-12-31"},
      {"%G", TEST_FORMAT_DATE, "22", "0022-01-03"},
      {"%G", TEST_FORMAT_DATE, "222", "0221-12-31"},
      {"%G", TEST_FORMAT_DATE, "2222", "2221-12-31"},
      {"%G", TEST_FORMAT_DATE, "2015", "2014-12-29"},
      {"%G", TEST_FORMAT_DATE, "9999", "9999-01-04"},

      // %g - ISO 8601 two-digit year without century 00-99.  More than
      //      two digits fails.
      {"%g", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"%g", TEST_FORMAT_DATE, "a", EXPECT_ERROR},
      {"%g", TEST_FORMAT_DATE, "000", EXPECT_ERROR},
      {"%g", TEST_FORMAT_DATE, "100", EXPECT_ERROR},
      {"%g", TEST_FORMAT_DATE, "222", EXPECT_ERROR},

      {"%g", TEST_FORMAT_DATE, "0", "2000-01-03"},
      {"%g", TEST_FORMAT_DATE, "00", "2000-01-03"},
      {"%g", TEST_FORMAT_DATE, "2", "2001-12-31"},
      {"%g", TEST_FORMAT_DATE, "02", "2001-12-31"},
      {"%g", TEST_FORMAT_DATE, "22", "2022-01-03"},
      {"%g", TEST_FORMAT_DATE, "99", "1999-01-04"},

      // %j - day of year 001-366.  A number outside that range fails.
      {"%j", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%j", TEST_FORMAT_DATE, "366", EXPECT_ERROR},
      {"%j", TEST_FORMAT_DATE, "367", EXPECT_ERROR},
      {"%j", TEST_FORMAT_DATE, "999", EXPECT_ERROR},
      {"%j", TEST_FORMAT_DATE, "9999", EXPECT_ERROR},

      {"%j", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%j", TEST_FORMAT_DATE, "01", kTestExpected19700101},
      {"%j", TEST_FORMAT_DATE, "001", kTestExpected19700101},
      {"%j", TEST_FORMAT_DATE, "31", "1970-01-31"},
      {"%j", TEST_FORMAT_DATE, "032", "1970-02-01"},
      {"%j", TEST_FORMAT_DATE, "365", "1970-12-31"},

      // %n - a whitespace character, equivalent to ' ' or '%t'
      {"%n", TEST_FORMAT_ANY, "\n", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, " \n ", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, "", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, " ", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, "\f", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, "\r", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, "\t", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, "\v", kTestExpectedDefault},
      // DOS newline.
      {"%n", TEST_FORMAT_ANY, "\r\n", kTestExpectedDefault},
      {"%n", TEST_FORMAT_ANY, " \f\n\r\t\v\v\r\t\n\f ", kTestExpectedDefault},
      {"a%nb", TEST_FORMAT_ANY, "a\nb", kTestExpectedDefault},

      // %t - a whitespace character, equivalent to ' ' or '%n'
      {"%t", TEST_FORMAT_ANY, "\t", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, " \t ", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, "", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, " ", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, "\f", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, "\r", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, "\n", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, "\v", kTestExpectedDefault},
      // DOS newline.
      {"%t", TEST_FORMAT_ANY, "\r\n", kTestExpectedDefault},
      {"%t", TEST_FORMAT_ANY, " \f\n\r\t\v\v\r\t\n\f ", kTestExpectedDefault},
      {"a%tb", TEST_FORMAT_ANY, "a\tb", kTestExpectedDefault},

      // %u - weekday 1-7, Monday = 1, leading zeros not allowed.
      //      '%Ou' is treated as '%u' in en_US locale.
      {"%u", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%u", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%u", TEST_FORMAT_DATE, "01", EXPECT_ERROR},
      {"%u", TEST_FORMAT_DATE, "7", kTestExpected19700101},
      {"%u", TEST_FORMAT_DATE, "8", EXPECT_ERROR},
      {"%Ou", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%Ou", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%Ou", TEST_FORMAT_DATE, "01", EXPECT_ERROR},
      {"%Ou", TEST_FORMAT_DATE, "7", kTestExpected19700101},
      {"%Ou", TEST_FORMAT_DATE, "8", EXPECT_ERROR},

      // %V - week of year 01-53, Monday as first day of week
      //      '%OV' is treated as '%V' in en_US locale.
      // %V and %OV are validated but ignored if ISO year is not present.
      {"%V", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"%V", TEST_FORMAT_DATE, "00", EXPECT_ERROR},
      {"%V", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%V", TEST_FORMAT_DATE, "01", kTestExpected19700101},
      {"%V", TEST_FORMAT_DATE, "53", kTestExpected19700101},
      {"%V", TEST_FORMAT_DATE, "54", EXPECT_ERROR},
      {"%OV", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"%OV", TEST_FORMAT_DATE, "00", EXPECT_ERROR},
      {"%OV", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%OV", TEST_FORMAT_DATE, "01", kTestExpected19700101},
      {"%OV", TEST_FORMAT_DATE, "53", kTestExpected19700101},
      {"%OV", TEST_FORMAT_DATE, "54", EXPECT_ERROR},

      // %w - weekday 0-6, Sunday = 0, leading zeros not allowed.
      //      '%Ow' is treated as '%w' in en_US locale.
      {"%w", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"%w", TEST_FORMAT_DATE, "0", kTestExpected19700101},
      {"%w", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%w", TEST_FORMAT_DATE, "01", EXPECT_ERROR},
      {"%w", TEST_FORMAT_DATE, "6", kTestExpected19700101},
      {"%w", TEST_FORMAT_DATE, "7", EXPECT_ERROR},
      {"%Ow", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"%Ow", TEST_FORMAT_DATE, "0", kTestExpected19700101},
      {"%Ow", TEST_FORMAT_DATE, "1", kTestExpected19700101},
      {"%Ow", TEST_FORMAT_DATE, "01", EXPECT_ERROR},
      {"%Ow", TEST_FORMAT_DATE, "6", kTestExpected19700101},
      {"%Ow", TEST_FORMAT_DATE, "7", EXPECT_ERROR},

      // %% - the '%' character
      {"a%%b", TEST_FORMAT_ANY, "a%b", kTestExpected19700101},
      {"a%%b", TEST_FORMAT_ANY, "a%%b", EXPECT_ERROR},
      {"a%%%%b", TEST_FORMAT_ANY, "a%%b", kTestExpected19700101},

      // Supported format elements do not match the same string, since they
      // expect a value that matches the element.
      {"%Y", TEST_FORMAT_ANY, "%Y", EXPECT_ERROR},
      {"%Q", TEST_FORMAT_ANY, "%Q", EXPECT_ERROR},

      // If some date parts have overlapping elements, the last one generally
      // wins.  The exception to that is that the last %s (number of seconds
      // from GoogleSQL epoch) always wins regardless of where it shows up.
      {"%Y %Y", TEST_FORMAT_DATE, "2001 1999", "1999-01-01"},

      // Year comes from %D (06), Month comes from %m (08), Day comes
      // from %d (05)
      {"%Y-%m-%d %D %m", TEST_FORMAT_DATE, "2000-02-03 04/05/06 08",
       "2006-08-05"},

      // The %y combines with %C, yielding year 1234.
      {"%C %y", TEST_FORMAT_DATE, "12 34", "1234-01-01"},
      // The %C combines with %y, yielding year 1122.
      {"%y %C", TEST_FORMAT_DATE, "22 11", "1122-01-01"},
      // The %C combines with %y, so the year 0022 is not an error.
      {"%y %C", TEST_FORMAT_DATE, "22 00", "0022-01-01"},
      // The %C combines with %y, but the year 0000 is invalid so it is an
      // error.
      {"%y %C", TEST_FORMAT_DATE, "00 00", EXPECT_ERROR},
      // If '%y' overwrites another '%y', ensure that the century part is
      // accurate. In this case 10 defaults to 2010 and 85 defaults to 1985.
      {"%y %y", TEST_FORMAT_DATE, "10 85", "1985-01-01"},
      // When %Y overrides one of %C or %y, that value doesn't propagate to a
      // subsequent format element of the other type.
      {"%y %Y %C", TEST_FORMAT_DATE, "11 1234 22", "2200-01-01"},
      {"%C %Y %y", TEST_FORMAT_DATE, "22 1234 11", "2011-01-01"},

      // %F is equivalent to %Y-%m-%d.
      {"%F %Y", TEST_FORMAT_DATE, "2001-02-03 2002", "2002-02-03"},
      {"%Y %F", TEST_FORMAT_DATE, "2002 2001-02-03", "2001-02-03"},

      // %Q - quarter 1-4, single-digit.  More than one digits fails.
      {"%Q", TEST_FORMAT_DATE, "1", "1970-01-01"},
      {"%Q", TEST_FORMAT_DATE, "2", "1970-04-01"},
      {"%Q", TEST_FORMAT_DATE, "3", "1970-07-01"},
      {"%Q", TEST_FORMAT_DATE, "4", "1970-10-01"},
      {"%Y-Q%Q", TEST_FORMAT_DATE, "2018-Q2", "2018-04-01"},
      {"%Y Q%Q %H", TEST_FORMAT_DATE_AND_TIME, "2018 Q3 15",
       "2018-07-01 15:00:00"},
      {"%Q", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"%Q", TEST_FORMAT_DATE, "5", EXPECT_ERROR},
      {"%Q", TEST_FORMAT_DATE, "01", EXPECT_ERROR},
      {"%Q", TEST_FORMAT_DATE, "A", EXPECT_ERROR},
      {"%Q", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},

      // %s (seconds since GoogleSQL epoch) overrides all other format elements,
      // regardless of where it occurs in the string.
      {"%Y-%m-%d %H:%M:%E6S%Ez %s", TEST_FORMAT_TIMEZONE,
       "2000-02-03 12:34:56.123456+06:30 0", "1970-01-01 00:00:00+00"},
      {"%s %Y-%m-%d %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "0 2000-02-03 12:34:56.123456+06:30", "1970-01-01 00:00:00+00"},
      {"%Y-%m-%d %s %H:%M:%E6S%Ez", TEST_FORMAT_TIMEZONE,
       "2000-02-03 0 12:34:56.123456+06:30", "1970-01-01 00:00:00+00"},

      // If there are two %s, the second one wins.
      {"%Y-%m-%d %s %H:%M:%E6S%Ez %s", TEST_FORMAT_TIMEZONE,
       "2000-02-03 1 12:34:56.123456+06:30 2", "1970-01-01 00:00:02+00"},
  });

  // %b or %B or %h - locale's month name, abbreviated or full.
  //                  GoogleSQL initially supports the en_US locale.
  //                  Month names and abbreviations are case insensitive.
  const std::string month_name_elements[] = {"%b", "%B", "%h"};
  for (const std::string& month : month_name_elements) {
    tests.push_back({month, TEST_FORMAT_DATE, "JAN", "1970-01-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Jan", "1970-01-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "jan", "1970-01-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Feb", "1970-02-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Mar", "1970-03-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Apr", "1970-04-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "May", "1970-05-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Jun", "1970-06-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Jul", "1970-07-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Aug", "1970-08-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Sep", "1970-09-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Oct", "1970-10-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Nov", "1970-11-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "Dec", "1970-12-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "March", "1970-03-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "April", "1970-04-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "June", "1970-06-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "july", "1970-07-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "July", "1970-07-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "JULY", "1970-07-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "December", "1970-12-01"});
  }

  // %c - locale's appropriate date and time representation.  For the
  //      supported en_US locale this format is equivalent to
  //      '%a %b %e %H:%M:%S %Y'.  Yes, this format is ugly.
  //      It uses the default time zone to generate the timestamp.
  const std::string datetime_elements[] = {"%c", "%Ec", "%a %b %e %H:%M:%S %Y"};
  for (const std::string& dt : datetime_elements) {
    tests.push_back({dt, TEST_FORMAT_DATE_AND_TIME,
                     "Tuesday Feb 28 12:34:56 2006",
                     "2006-02-28 12:34:56"});
    // The parsing ignores whatever day of week was specified.  It does
    // not matter as long as it is a valid day name.
    tests.push_back({dt, TEST_FORMAT_DATE_AND_TIME,
                     "Monday Feb 28 12:34:56 2006", "2006-02-28 12:34:56"});
  }

  // %d, %e - day of month.  Parses single digit days, as well as single
  //          digit days with leading zero.
  const std::string day_elements[] = {"%d", "%Od", "%e", "%Oe"};
  for (const std::string& day : day_elements) {
    tests.push_back({day, TEST_FORMAT_DATE, "1",  "1970-01-01"});
    tests.push_back({day, TEST_FORMAT_DATE, "01", "1970-01-01"});
    tests.push_back({day, TEST_FORMAT_DATE, "9",  "1970-01-09"});
    tests.push_back({day, TEST_FORMAT_DATE, "09", "1970-01-09"});
    tests.push_back({day, TEST_FORMAT_DATE, "31", "1970-01-31"});
  }

  // %D - equivalent to date format %m/%d/%y
  // %x - locale's appropriate date representation.  For en_US, equivalent to
  //      '%D' and '%m/%d/%y'.
  const std::string mdy_elements[] = {"%D", "%x", "%Ex", "%m/%d/%y"};
  for (const std::string& mdy : mdy_elements) {
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/02/03", "2003-01-02"});
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/0203", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "0102/03", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "010203", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "13/02/03", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/32/03", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/32/03", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "a/b/c", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/02/00", "2000-01-02"});
    // Only the two digit year is allowed.
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/02/003", EXPECT_ERROR});
    tests.push_back({mdy, TEST_FORMAT_DATE, "01/02/2003", EXPECT_ERROR});
  }

  // %F - equivalent to date format '%Y-%m-%d'
  const std::string Ymd_elements[] = {"%F", "%Y-%m-%d"};
  for (const std::string& Ymd : Ymd_elements) {
    tests.push_back({Ymd, TEST_FORMAT_DATE, "1-2-3", "0001-02-03"});
    tests.push_back({Ymd, TEST_FORMAT_DATE, "11-2-3", "0011-02-03"});
    tests.push_back({Ymd, TEST_FORMAT_DATE, "111-2-03", "0111-02-03"});
    tests.push_back({Ymd, TEST_FORMAT_DATE, "1111-02-3", "1111-02-03"});
  }

  // %H, %k - hours 00-23
  const std::string hour24_elements[] = {"%H", "%OH", "%k"};
  for (const std::string& hour24 : hour24_elements) {
    tests.push_back({hour24, TEST_FORMAT_TIME, "0",  "00:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "00", "00:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "03", "03:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "9",  "09:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "12", "12:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "13", "13:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "23", "23:00:00"});
    tests.push_back({hour24, TEST_FORMAT_TIME, "24", EXPECT_ERROR});
  }

  // %I, %l - hours 01-12
  const std::string hour12_elements[] = {"%I", "%OI", "%l"};
  for (const std::string& hour12 : hour12_elements) {
    tests.push_back({hour12, TEST_FORMAT_TIME, "0",  EXPECT_ERROR});
    tests.push_back({hour12, TEST_FORMAT_TIME, "00", EXPECT_ERROR});

    tests.push_back({hour12, TEST_FORMAT_TIME, "01", "01:00:00"});
    tests.push_back({hour12, TEST_FORMAT_TIME, "03", "03:00:00"});
    tests.push_back({hour12, TEST_FORMAT_TIME, "9",  "09:00:00"});
    // This one seems a bit odd, but note that hour '12' here corresponds to
    // 12am so the resulting timestamp is actually the hour starting
    // at midnight, not noon.
    tests.push_back({hour12, TEST_FORMAT_TIME, "12", "00:00:00"});
    tests.push_back({hour12, TEST_FORMAT_TIME, "13", EXPECT_ERROR});
    tests.push_back({hour12, TEST_FORMAT_TIME, "23", EXPECT_ERROR});

    // Weird stuff inherited from strptime
    //   Whitespace characteristics - consumes any amount of prefix whitespace
    //   including none.
    tests.push_back({absl::StrCat("x", hour12, "y"), TEST_FORMAT_TIME, "x 01y",
                     "01:00:00"});
    tests.push_back(
        {absl::StrCat("x", hour12, "y"), TEST_FORMAT_TIME, "x 1y", "01:00:00"});
    tests.push_back({absl::StrCat("x", hour12, "y"), TEST_FORMAT_TIME, "x 11y",
                     "11:00:00"});
    tests.push_back({absl::StrCat("x", hour12, "y"), TEST_FORMAT_TIME,
                     "x\r\n\t11y", "11:00:00"});
    tests.push_back({absl::StrCat("x", hour12, "y"), TEST_FORMAT_TIME, "x11 y",
                     EXPECT_ERROR});
    tests.push_back({absl::StrCat("x", hour12, "y"), TEST_FORMAT_TIME, "x011y",
                     EXPECT_ERROR});
  }

  // %m - month 1-12, with an optional leading 0.  More than two digits
  //      not allowed.
  const std::string month_elements[] = {"%m", "%Om"};
  for (const std::string& month : month_elements) {
    tests.push_back({month, TEST_FORMAT_DATE, "0",   EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "01",  "1970-01-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "1",   "1970-01-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "2",   "1970-02-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "3",   "1970-03-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "4",   "1970-04-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "5",   "1970-05-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "6",   "1970-06-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "7",   "1970-07-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "08",  "1970-08-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "9",   "1970-09-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "10",  "1970-10-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "11",  "1970-11-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "12",  "1970-12-01"});
    tests.push_back({month, TEST_FORMAT_DATE, "13",  EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "001", EXPECT_ERROR});

    // Invalid month number.
    tests.push_back({month, TEST_FORMAT_DATE, "0", EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "-", EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "-1", EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "2147483647", EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "21474836470", EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "9223372036854775807",
                        EXPECT_ERROR});
    tests.push_back({month, TEST_FORMAT_DATE, "92233720368547758070",
                        EXPECT_ERROR});
  }

  // %M - minutes 00-59.  More than two digits not allowed.
  const std::string minute_elements[] = {"%M", "%OM"};
  for (const std::string& minute : minute_elements) {
    tests.push_back({minute, TEST_FORMAT_TIME, "0", "00:00:00"});
    tests.push_back({minute, TEST_FORMAT_TIME, "00", "00:00:00"});
    tests.push_back({minute, TEST_FORMAT_TIME, "000", EXPECT_ERROR});
    tests.push_back({minute, TEST_FORMAT_TIME, "1", "00:01:00"});
    tests.push_back({minute, TEST_FORMAT_TIME, "01", "00:01:00"});
    tests.push_back({minute, TEST_FORMAT_TIME, "001", EXPECT_ERROR});
    tests.push_back({minute, TEST_FORMAT_TIME, "30", "00:30:00"});
    tests.push_back({minute, TEST_FORMAT_TIME, "59", "00:59:00"});
    tests.push_back({minute, TEST_FORMAT_TIME, "60", EXPECT_ERROR});
  }

  // %p, %P - locale's equivalent of AM/PM
  // Note that alone it has no impact on the timestamp.  It only affects
  // the timestamp when in combination with a non-default 1-12 based hour.
  // Those are tested together in the CombinationTests.
  const std::string ampm_elements[] = {"%p", "%P"};
  for (const std::string& ampm : ampm_elements) {
    // Parses both upper and lowercase.
    tests.push_back({ampm, TEST_FORMAT_TIME, "am", "00:00:00"});
    tests.push_back({ampm, TEST_FORMAT_TIME, "pm", "00:00:00"});
    tests.push_back({ampm, TEST_FORMAT_TIME, "AM", "00:00:00"});
    tests.push_back({ampm, TEST_FORMAT_TIME, "PM", "00:00:00"});
    tests.push_back({ampm, TEST_FORMAT_TIME, "aM", "00:00:00"});
    tests.push_back({ampm, TEST_FORMAT_TIME, "Pm", "00:00:00"});
    tests.push_back({ampm, TEST_FORMAT_TIME, "P", EXPECT_ERROR});
    tests.push_back({ampm, TEST_FORMAT_TIME, "A", EXPECT_ERROR});
    tests.push_back({ampm, TEST_FORMAT_TIME, "Ax", EXPECT_ERROR});
    tests.push_back({ampm, TEST_FORMAT_TIME, "Px", EXPECT_ERROR});
    tests.push_back({ampm, TEST_FORMAT_TIME, "xM", EXPECT_ERROR});
    tests.push_back({ampm, TEST_FORMAT_TIME, "xm", EXPECT_ERROR});
  }
  // %R - equivalent to time format %H:%M, leading 0's are optional.
  const std::string hour_minute_elements[] = {"%R", "%H:%M"};
  for (const std::string& hm : hour_minute_elements) {
    tests.push_back({hm, TEST_FORMAT_TIME, "00:00", "00:00:00"});
    tests.push_back({hm, TEST_FORMAT_TIME, "01:01", "01:01:00"});
    tests.push_back({hm, TEST_FORMAT_TIME, "23:59", "23:59:00"});
    tests.push_back({hm, TEST_FORMAT_TIME, "3:59", "03:59:00"});
    tests.push_back({hm, TEST_FORMAT_TIME, "23:9", "23:09:00"});
    tests.push_back({hm, TEST_FORMAT_TIME, "24:00", EXPECT_ERROR});
  }

  // %r - locale's representation of 12-hour clock using AM/PM notation.
  //      Equivalent to '%I:%M:%S %p'.
  const std::string clock_elements[] = {"%r", "%I:%M:%S %p"};
  for (const std::string& clock : clock_elements) {
    tests.push_back({clock, TEST_FORMAT_TIME, "00:00:00 pm", EXPECT_ERROR});
    tests.push_back({clock, TEST_FORMAT_TIME, "12:00 am", EXPECT_ERROR});
    tests.push_back({clock, TEST_FORMAT_TIME, "12:00:00 am", "00:00:00"});
    tests.push_back({clock, TEST_FORMAT_TIME, "12:00:00am", "00:00:00"});
    tests.push_back({clock, TEST_FORMAT_TIME, "12:00:00   am", "00:00:00"});
    tests.push_back({clock, TEST_FORMAT_TIME, "01:00:11 am", "01:00:11"});
    tests.push_back({clock, TEST_FORMAT_TIME, "11:59:59 am", "11:59:59"});
    tests.push_back({clock, TEST_FORMAT_TIME, "12:00:00 pm", "12:00:00"});
    tests.push_back({clock, TEST_FORMAT_TIME, "1:00:11 pm", "13:00:11"});
    // Allow arbitrary whitespace before the hour string. and before the am/pm.
    tests.push_back({absl::StrCat("x", clock), TEST_FORMAT_TIME,
                     "x\n1:00:11\t\rpm", "13:00:11"});
    tests.push_back({clock, TEST_FORMAT_TIME, "11:59:59 Pm", "23:59:59"});
    tests.push_back({clock, TEST_FORMAT_TIME, "13:00:00 pm", EXPECT_ERROR});
  }

  // %S - seconds 00-60.  Second 60 is second 0 of the next minute.
  const std::string seconds_elements[] = {"%S", "%OS"};
  for (const std::string& seconds : seconds_elements) {
    tests.push_back({seconds, TEST_FORMAT_TIME, "00", "00:00:00"});
    tests.push_back({seconds, TEST_FORMAT_TIME, "01", "00:00:01"});
    tests.push_back({seconds, TEST_FORMAT_TIME, "1", "00:00:01"});
    tests.push_back({seconds, TEST_FORMAT_TIME, "59", "00:00:59"});
    tests.push_back({seconds, TEST_FORMAT_TIME, "60", "00:01:00"});
    tests.push_back({seconds, TEST_FORMAT_TIME, "61", EXPECT_ERROR});
  }
  tests.push_back({"%H:%M:%S", TEST_FORMAT_DATE_AND_TIME, "23:59:60",
                      "1970-01-02 00:00:00"});

  // %T - equivalent to time format %H:%M:%S.
  // %X - locale's appropriate time representation.  For en_US, equivalent to
  //      '%T' and '%H:%M:%S'.
  const std::string time_elements[] = {"%T", "%H:%M:%S", "%X", "%EX"};
  for (const std::string& t : time_elements) {
    tests.push_back({t, TEST_FORMAT_TIME, "00:00:00", "00:00:00"});
    tests.push_back({t, TEST_FORMAT_TIME, "12:34:56", "12:34:56"});
    tests.push_back({t, TEST_FORMAT_TIME, "1:3:6", "01:03:06"});
    tests.push_back({t, TEST_FORMAT_TIME, "23:59:59", "23:59:59"});
    tests.push_back({t, TEST_FORMAT_DATE_AND_TIME, "23:59:60",
                        "1970-01-02 00:00:00"});
    // Does not accept subseconds.
    tests.push_back({t, TEST_FORMAT_TIME, "12:34:56.123", EXPECT_ERROR});
  }

  // %y - year without century.
  //      '%Ey' and '%Oy' are treated equivalent to '%y' in en_US locale.
  //      Years 00-68 are 2000s, 69-99 are 1900s.
  //      Leading 0's are significant.
  const std::string year2_elements[] = {"%y", "%Ey", "%Oy"};
  for (const std::string& year2 : year2_elements) {
    tests.push_back({year2, TEST_FORMAT_DATE, "-1", EXPECT_ERROR});
    tests.push_back({year2, TEST_FORMAT_DATE, "0", "2000-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "1", "2001-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "01", "2001-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "001", EXPECT_ERROR});
    tests.push_back({year2, TEST_FORMAT_DATE, "67", "2067-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "68", "2068-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "69", "1969-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "99", "1999-01-01"});
    tests.push_back({year2, TEST_FORMAT_DATE, "100", EXPECT_ERROR});
  }

  // %Y - year with century.
  //      '%EY' is treated equivalent to '%Y' in en_US locale.
  //      '%C%y' is treated equivalent to '%Y' when the strings are 4 digits
  //      long.
  //      Leading zeroes are allowed but do not change the number of digits a
  //      format will consume.
  const std::string year_elements[] = {"%Y", "%EY"};
  for (const std::string& year : year_elements) {
    tests.push_back({year, TEST_FORMAT_DATE, "-1", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "0", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "1", "0001-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "01", "0001-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "10", "0010-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "100", "0100-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "001", "0001-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "09999", "9999-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "00001", "0001-01-01"});
  }
  const std::string year_elements_with_century[] = {"%Y", "%EY", "%C%y",
                                                    "%EC%y"};
  for (const std::string& year : year_elements_with_century) {
    tests.push_back({year, TEST_FORMAT_DATE, "0000", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "0001", "0001-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "1970", "1970-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "2015", "2015-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "9999", "9999-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "0999", "0999-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "1000", "1000-01-01"});
    tests.push_back({year, TEST_FORMAT_DATE, "10000", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "009999", EXPECT_ERROR});

    // Input that does not match the expected element.
    tests.push_back({year, TEST_FORMAT_DATE, "", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "abcd", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "9abcd", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "20abcd", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "200abcd", EXPECT_ERROR});
    tests.push_back({year, TEST_FORMAT_DATE, "2000abcd", EXPECT_ERROR});
  }

  // %E#S - seconds with # digits fractional precision.
  //
  // %E0S means there is no fractional part, and if a fractional part is
  // present then an error is provided.  Equivalent to %S.
  const std::string second_elements[] = {"%E0S", "%S"};
  for (const std::string& second : second_elements) {
    tests.push_back({second, TEST_FORMAT_TIME, "00", "00:00:00"});
    tests.push_back({second, TEST_FORMAT_TIME, "01", "00:00:01"});
    tests.push_back({second, TEST_FORMAT_TIME, "1", "00:00:01"});
    tests.push_back({second, TEST_FORMAT_TIME, "1.", EXPECT_ERROR});
    tests.push_back({second, TEST_FORMAT_TIME, "1.0", EXPECT_ERROR});
    tests.push_back({second, TEST_FORMAT_TIME, "1.1", EXPECT_ERROR});
    tests.push_back({second, TEST_FORMAT_TIME, "59", "00:00:59"});
    tests.push_back({second, TEST_FORMAT_TIME, "60", "00:01:00"});
    tests.push_back({second, TEST_FORMAT_TIME, "61", EXPECT_ERROR});
  }

  // %U - week of year 00-53, Sunday as first day of week
  // %W - week of year 00-53, Monday as first day of week
  //
  // 1970 only has weeks 00-52.
  const std::string week_elements[] = {"%U", "%OU", "%W", "%OW"};
  for (const std::string& week : week_elements) {
    tests.push_back({week, TEST_FORMAT_DATE, "-1", EXPECT_ERROR});
    tests.push_back({week, TEST_FORMAT_DATE, "53", EXPECT_ERROR});
    tests.push_back({week, TEST_FORMAT_DATE, "54", EXPECT_ERROR});
  }

  const std::string u_week_elements[] = {"%U", "%OU"};
  for (const std::string& week : u_week_elements) {
    tests.push_back({week, TEST_FORMAT_DATE, "0", "1969-12-28"});
    tests.push_back({week, TEST_FORMAT_DATE, "00", "1969-12-28"});
    tests.push_back({week, TEST_FORMAT_DATE, "01", "1970-01-04"});
    tests.push_back({week, TEST_FORMAT_DATE, "52", "1970-12-27"});
  }

  const std::string w_week_elements[] = {"%W", "%OW"};
  for (const std::string& week : w_week_elements) {
    tests.push_back({week, TEST_FORMAT_DATE, "0", "1969-12-29"});
    tests.push_back({week, TEST_FORMAT_DATE, "00", "1969-12-29"});
    tests.push_back({week, TEST_FORMAT_DATE, "01", "1970-01-05"});
    tests.push_back({week, TEST_FORMAT_DATE, "52", "1970-12-28"});
  }

  return tests;
}  // NOLINT(readability/fn_size)

class ParseTimestampTest {
 public:
  ParseTimestampTest(absl::string_view format,
                     absl::string_view timestamp_string,
                     absl::string_view default_time_zone,
                     absl::string_view expected_result)
      : format_(format),
        timestamp_string_(timestamp_string),
        default_time_zone_(default_time_zone),
        expected_result_(expected_result),
        nano_expected_result_(expected_result),
        pico_expected_result_(expected_result) {}

  ParseTimestampTest(absl::string_view format,
                     absl::string_view timestamp_string,
                     absl::string_view default_time_zone,
                     absl::string_view expected_result,
                     absl::string_view nano_expected_result)
      : format_(format),
        timestamp_string_(timestamp_string),
        default_time_zone_(default_time_zone),
        expected_result_(expected_result),
        nano_expected_result_(nano_expected_result),
        pico_expected_result_(nano_expected_result) {}

  ParseTimestampTest(absl::string_view format,
                     absl::string_view timestamp_string,
                     absl::string_view default_time_zone,
                     absl::string_view expected_result,
                     absl::string_view nano_expected_result,
                     absl::string_view pico_expected_result)
      : format_(format),
        timestamp_string_(timestamp_string),
        default_time_zone_(default_time_zone),
        expected_result_(expected_result),
        nano_expected_result_(nano_expected_result),
        pico_expected_result_(pico_expected_result) {}

  ~ParseTimestampTest() {}

  const std::string& format() const { return format_; }
  const std::string& timestamp_string() const { return timestamp_string_; }
  const std::string& default_time_zone() const { return default_time_zone_; }
  const std::string& expected_result() const { return expected_result_; }
  const std::string& nano_expected_result() const {
    return nano_expected_result_;
  }
  const std::string& pico_expected_result() const {
    return pico_expected_result_;
  }

 private:
  std::string format_;                // format string
  std::string timestamp_string_;      // timestamp string to parse
  std::string default_time_zone_;     // default time zone
  std::string expected_result_;       // "" indicates an error is expected
  std::string nano_expected_result_;  // "" indicates an error is expected
  std::string pico_expected_result_;  // "" indicates an error is expected
  // Copyable.
};

static std::vector<ParseTimestampTest> GetParseTimestampSpecificTests() {
  std::vector<ParseTimestampTest> tests({
      {"", "", "UTC", "1970-01-01 00:00:00+00"},
      {"", "", "America/Los_Angeles", "1970-01-01 00:00:00-08"},

      {"%Y-%m-%d %H:%M:%E6S", "0000-12-31 16:08:00.000000", "-07:52",
       "0001-01-01 00:00:00+0"},

      {"%Y-%m-%d %H:%M:%E6S", "10000-01-01 06:29:59.999999", "Asia/Yangon",
       "9999-12-31 23:59:59.999999+0"},

      // Invalid time zones.
      {"", "", "invalid_time_zone", EXPECT_ERROR},
      {"", "", "", EXPECT_ERROR},

      // Tests that %Ez accepts 'Z' in the timestamp string.  This first
      // test is the original test case for b/31088612.
      {"%Y-%m-%dT%H:%M:%E3S%Ez", "2016-06-30T00:00:00.000Z", "UTC",
       "2016-06-30 00:00:00 UTC"},
      {"%Y-%m-%d %H:%M:%S%Ez", "2016-08-24 12:34:56Z", "UTC",
       "2016-08-24 12:34:56 UTC"},
      {"%Y-%m-%d %H:%M:%E6S%Ez", "2016-08-24 12:34:56.123456Z", "UTC",
       "2016-08-24 12:34:56.123456 UTC"},
      // Since the time zone is in the parse string, the default time zone
      // is irrelevant.
      {"%Ez", "Z", "America/Los_Angeles", "1970-01-01 00:00:00 UTC"},

      // %Ez is valid, %EZ is not.
      {"%EZ", "Z", "UTC", EXPECT_ERROR},
      // 'Z' is valid, 'z' is not.
      {"%Ez", "z", "UTC", EXPECT_ERROR},

      // Extra stuff after 'Z' is not valid (though leading/trailing
      // spaces are ok).
      {"%Ez", "  Z  ", "UTC", "1970-01-01 00:00:00 UTC"},
      {"%Ez", "ZZ", "UTC", EXPECT_ERROR},
      {"%Ez", "Z1", "UTC", EXPECT_ERROR},
      {"%Ez", "Za", "UTC", EXPECT_ERROR},
      // But if the extra stuff is in the format string, then it is ok.
      {"%Eza", "Za", "UTC", "1970-01-01 00:00:00 UTC"},
      // And the %Ez can be embedded between other format elements.
      {"%Y-%Ez-%m", "2016-Z-04", "UTC", "2016-04-01 00:00:00 UTC"},
      {"%Y%Ez%m", "2016Z04", "UTC", "2016-04-01 00:00:00 UTC"},

      // The time zone offset yields timestamps outside the supported range.
      {"%Y-%m-%d %H:%M:%E6S", "0001-01-01 00:00:00.000000", "+01",
       EXPECT_ERROR},
      {"%Y-%m-%d %H:%M:%E6S%Ez", "9999-12-31 23:59:59.999999", "-01",
       EXPECT_ERROR},

      // %C, %EC - century.  Consumes the next 1-3 digits.
      {"%C", "100", "+01", "9999-12-31 23:00:00+00"},
      {"%C1", "1001", "+01", "9999-12-31 23:00:00+00"},
      {"%Ca", "100a", "+01", "9999-12-31 23:00:00+00"},
      {"%C1 %m-%d %H:%M:%S", "0001 12-31 23:00:00", "-01",
       "0001-01-01 00:00:00+00"},

      // %C consumes all these digits, even though if it consumed all but the
      // last then this would work.
      {"%EC", "100", "+01", "9999-12-31 23:00:00+00"},

      // %Z takes a time zone name or canonical format time zone (with optional
      // "UTC" prefix).
      {"%Z", "America/Los_Angeles", "UTC", "1970-01-01 00:00:00-08"},
      {"%Z", "+00", "UTC", "1970-01-01 00:00:00+00"},
      {"%Z", "-00", "UTC", "1970-01-01 00:00:00+00"},
      {"%Z", "+00:30", "UTC", "1970-01-01 00:00:00+00:30"},
      {"%Z", "-00:30", "UTC", "1970-01-01 00:00:00-00:30"},
      {"%Z", "UTC+8:30", "UTC", "1970-01-01 00:00:00+08:30"},
      {"%Z", "UTC-0540", "UTC", "1970-01-01 00:00:00-05:40"},
      {"%Z", "Z", "UTC", EXPECT_ERROR},

      // %z takes a time zone offset of the form +/-HHMM (not a
      // time zone name).
      {"%z", "America/Los_Angeles", "UTC", EXPECT_ERROR},
      {"%z", "+00", "UTC", "1970-01-01 00:00:00+00"},
      {"%z", "-00", "UTC", "1970-01-01 00:00:00+00"},
      {"%z", "+0030", "UTC", "1970-01-01 00:00:00+00:30"},
      {"%z", "-0030", "UTC", "1970-01-01 00:00:00-00:30"},
      {"%z", "UTC+0030", "UTC", EXPECT_ERROR},
      {"%z", "Z", "UTC", EXPECT_ERROR},

      // %Ez takes a time zone offset of the form +/-HH:MM, or 'Z' (but
      // not time zone names in general).
      {"%Ez", "America/Los_Angeles", "UTC", EXPECT_ERROR},
      {"%Ez", "+00", "UTC", "1970-01-01 00:00:00+00"},
      {"%Ez", "-00", "UTC", "1970-01-01 00:00:00+00"},
      {"%Ez", "+00:30", "UTC", "1970-01-01 00:00:00+00:30"},
      {"%Ez", "-00:30", "UTC", "1970-01-01 00:00:00-00:30"},
      {"%Ez", "UTC-00:30", "UTC", EXPECT_ERROR},
      {"%Ez", "Z", "UTC", "1970-01-01 00:00:00+00"},
  });

  const std::string datetime_elements[] = {"%c", "%Ec", "%a %b %e %H:%M:%S %Y"};
  for (const std::string& dt : datetime_elements) {
    tests.emplace_back(dt, "Thu July 23 12:00:00 2015", "America/Los_Angeles",
                       "2015-07-23 12:00:00-07");
  }

  return tests;
}

// The format string in the test cases returned by this function should only
// have the element "%E*S" or "%E#S".
static std::vector<ParseTimestampTest>
GetParseNanoAndPicoTimestampSensitiveTests() {
  const char kTestExpected01123456[] = "1970-01-01 00:00:01.123456";
  const char kTestExpected011234567[] = "1970-01-01 00:00:01.1234567";
  const char kTestExpected0112345678[] = "1970-01-01 00:00:01.12345678";
  const char kTestExpected01123456789[] = "1970-01-01 00:00:01.123456789";
  const char kTestExpected011234567891[] = "1970-01-01 00:00:01.1234567891";
  const char kTestExpected01123456789123[] = "1970-01-01 00:00:01.123456789123";

  // Additional digits beyond the supported precision are truncated (6 for
  // micros, 9 for nanos, 12 for picos)
  std::vector<ParseTimestampTest> tests({
      {"%E*S", "1.1234567", "UTC", kTestExpected01123456,
       kTestExpected011234567},
      {"%E*S", "1.12345678", "UTC", kTestExpected01123456,
       kTestExpected0112345678},
      {"%E*S", "1.123456789", "UTC", kTestExpected01123456,
       kTestExpected01123456789},
      {"%E*S", "1.1234567891", "UTC", kTestExpected01123456,
       kTestExpected01123456789, kTestExpected011234567891},
      {"%E*S", "1.123456789123456", "UTC", kTestExpected01123456,
       kTestExpected01123456789, kTestExpected01123456789123},
      {"%E10S", "1", "UTC", EXPECT_ERROR, EXPECT_ERROR, "1970-01-01 00:00:01"},
      {"%E7S", "1", "UTC", EXPECT_ERROR, "1970-01-01 00:00:01"},
      {"%E9S", "1.123456789", "UTC", EXPECT_ERROR, kTestExpected01123456789},
      {"%E10S", "1.1234567891", "UTC", EXPECT_ERROR, EXPECT_ERROR,
       kTestExpected011234567891},
      {"%E12S", "1.123456789123", "UTC", EXPECT_ERROR, EXPECT_ERROR,
       kTestExpected01123456789123},
  });
  return tests;
}

static std::vector<ParseTimestampTest> GetParseTimestampCommonTests() {
  std::vector<ParseTimestampTest> tests;

  // Adds all the common tests.
  for (const ParseDateTimestampCommonTest& common_test :
       GetParseDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_TIME &&
        !common_test.expected_result.empty()) {
      tests.push_back(
          {common_test.format_string, common_test.input_string, "UTC",
           absl::StrCat("1970-01-01 ", common_test.expected_result, "+00")});
    } else {
      tests.push_back({common_test.format_string, common_test.input_string,
                       "UTC", common_test.expected_result});
    }
  }

  return tests;
}

static std::vector<FunctionTestCall> GetFunctionTestsParseTime() {
  std::vector<CivilTimeTestCase> test_cases = {
      {{{NullString(), String("")}}, NullTime()},
      {{{String(""), NullString()}}, NullTime()},
      {{{NullString(), NullString()}}, NullTime()},

      {{{String(""), String("")}}, TimeMicros(0, 0, 0, 0)},
      {{{String("%H:%M:%S"), String("12:34:56")}}, TimeMicros(12, 34, 56, 0)},

      // "%E#S" does not consume more digits than specified.
      {{{String("%E1S%M"), String("4.321")}},
        TimeMicros(0, 21, 4, 300000)},

      // Time edge cases
      {{{String("%H:%M:%E*S"), String("00:00:00.000000")}},
       TimeMicros(0, 0, 0, 0)},
      {{{String("%H:%M:%E*S"), String("23:59:59.999999")}},
       TimeMicros(23, 59, 59, 999999)},
      {{{String("%H:%M:%E*S"), String("23:59:59.999999999")}},
       TimeMicros(23, 59, 59, 999999),
       TimeNanos(23, 59, 59, 999999999)},
      {{{String("%H:%M:%E*S"), String("23:59:59.9999999999")}},
       TimeMicros(23, 59, 59, 999999),
       TimeNanos(23, 59, 59, 999999999)},
      {{{String("%H:%M:%E9S"), String("23:59:59.999999999")}},
       FunctionEvalError(),
       TimeNanos(23, 59, 59, 999999999)},

      // Out-of-range cases
      {{{String("%S"), String("-1")}}, FunctionEvalError(), TimeType()},
      {{{String("%M:%S"), String("-1:00")}}, FunctionEvalError(), TimeType()},
      {{{String("%M:%S"), String("01:-1")}}, FunctionEvalError(), TimeType()},
      {{{String("%H:%S"), String("-1:00")}}, FunctionEvalError(), TimeType()},
      {{{String("%H:%S"), String("01:-1")}}, FunctionEvalError(), TimeType()},
      {{{String("%H:%M"), String("-1:00")}}, FunctionEvalError(), TimeType()},
      {{{String("%H:%M"), String("01:-1")}}, FunctionEvalError(), TimeType()},
      {{{String("%H:%M:%S"), String("24:00:00")}},
       FunctionEvalError(),
       TimeType()},
      {{{String("%H:%M:%S"), String("-1:00:00")}},
       FunctionEvalError(),
       TimeType()},

      // Invalid %E4Y for time, more invalid elements will be added in
      // subsequent test cases.
      {{{String("%E4Y"), String("")}}, FunctionEvalError(), TimeType()},
  };

  // Add test cases with invalid elements
  for (const char element : absl::string_view("AaBbhCcDdeFGgjmsUuVWwxYyZz")) {
    test_cases.push_back(CivilTimeTestCase(
        {String(absl::StrCat("%", std::string(1, element))), String("1")},
        FunctionEvalError(), TimeType()));
  }

  // Add common test cases, all with micro precision.
  for (const ParseDateTimestampCommonTest& test :
       GetParseDateTimestampCommonTests()) {
    if (test.parts == TEST_FORMAT_ANY || test.parts == TEST_FORMAT_TIME) {
      if (IsExpectedError(test.expected_result)) {
        // By definition, this means an error.
        test_cases.push_back(CivilTimeTestCase(
            {String(test.format_string), String(test.input_string)},
            FunctionEvalError(), TimeType()));
        continue;
      }
      if (test.parts == TEST_FORMAT_TIME) {
        TimeValue expected_time;
        GOOGLESQL_CHECK_OK(functions::ConvertStringToTime(test.expected_result,
                                                kMicroseconds, &expected_time));
        test_cases.push_back(CivilTimeTestCase(
            {String(test.format_string), String(test.input_string)},
            Time(expected_time)));
      } else if (test.parts == TEST_FORMAT_ANY) {
        // This has to be the default value of Time.
        test_cases.push_back(CivilTimeTestCase(
            {String(test.format_string), String(test.input_string)},
            TimeMicros(0, 0, 0, 0)));
      }
    }
  }

  // Add nano test cases.
  for (const ParseTimestampTest& test :
       GetParseNanoAndPicoTimestampSensitiveTests()) {
    auto ConvertTimestampStringToTime = [](absl::string_view timestamp_string,
                                           absl::string_view timezone_string,
                                           functions::TimestampScale scale) {
      if (IsExpectedError(timestamp_string)) {
        return absl::StatusOr<Value>(FunctionEvalError());
      }
      absl::Time timestamp;
      absl::TimeZone timezone;
      GOOGLESQL_CHECK_OK(functions::MakeTimeZone(timezone_string, &timezone));
      GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
          timestamp_string, timezone, scale,  false /* allow_tz_in_str */,
          &timestamp));
      TimeValue time;
      GOOGLESQL_CHECK_OK(functions::ConvertTimestampToTime(timestamp, timezone, &time));
      ABSL_CHECK(time.IsValid());
      return absl::StatusOr<Value>(Value::Time(time));
    };
    test_cases.push_back(CivilTimeTestCase(
        {String(test.format()), String(test.timestamp_string())},
        ConvertTimestampStringToTime(test.expected_result(),
                                     test.default_time_zone(), kMicroseconds),
        ConvertTimestampStringToTime(test.nano_expected_result(),
                                     test.default_time_zone(), kNanoseconds),
        TimeType()));
  }

  return PrepareCivilTimeTestCaseAsFunctionTestCall(test_cases, "parse_time");
}

static std::vector<FunctionTestCall> GetFunctionTestsParseDatetime() {
  std::vector<CivilTimeTestCase> test_cases = {
      {{{NullString(), String("")}}, NullDatetime()},
      {{{String(""), NullString()}}, NullDatetime()},
      {{{NullString(), NullString()}}, NullDatetime()},

      {{{String(""), String("")}}, DatetimeMicros(1970, 1, 1, 0, 0, 0, 0)},
      {{{String("%Y-%m-%d %H:%M:%S"), String("2016-06-21 12:34:56")}},
       DatetimeMicros(2016, 6, 21, 12, 34, 56, 0)},

      // Datetime edge cases
      {{{String("%Y-%m-%d %H:%M:%E*S"), String("0001-01-01 00:00:00.000000")}},
       DatetimeMicros(1, 1, 1, 0, 0, 0, 0)},
      {{{String("%Y-%m-%d %H:%M:%E*S"), String("9999-12-31 23:59:59.999999")}},
       DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999)},
      {{{String("%Y-%m-%d %H:%M:%E*S"),
         String("9999-12-31 23:59:59.999999999")}},
       DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999),
       DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999999)},
      {{{String("%Y-%m-%d %H:%M:%E9S"),
         String("9999-12-31 23:59:59.999999999")}},
       FunctionEvalError(),
       DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999999)},

      // Out-of-range cases
      {{{String("%Y-%m-%d %H:%M:%E*S"), String("0000-01-02 03:04:05.123456")}},
       FunctionEvalError(),
       DatetimeType()},
      {{{String("%Y-%m-%d %H:%M:%E*S"), String("10000-01-02 03:04:05.123456")}},
       FunctionEvalError(),
       DatetimeType()},

      // Invalid elements
      {{{String("%z"), String("1")}}, FunctionEvalError(), DatetimeType()},
      {{{String("%Z"), String("1")}}, FunctionEvalError(), DatetimeType()},
  };

  // Add common test cases.
  for (const ParseDateTimestampCommonTest& test :
       GetParseDateTimestampCommonTests()) {
    if (test.parts == TEST_FORMAT_TIMEZONE ||
        IsExpectedError(test.expected_result)) {
      test_cases.push_back(CivilTimeTestCase(
          {String(test.format_string), String(test.input_string)},
          FunctionEvalError(), DatetimeType()));
      continue;
    }
    std::string expected_string;
    if (test.parts == TEST_FORMAT_TIME) {
      expected_string = absl::StrCat("1970-01-01 ", test.expected_result);
    } else {
      expected_string = test.expected_result;
    }
    // Assuming that expected_string is a well constructed datetime string.
    DatetimeValue datetime;
    GOOGLESQL_CHECK_OK(functions::ConvertStringToDatetime(expected_string, kMicroseconds,
                                                &datetime));
    test_cases.push_back(CivilTimeTestCase(
        {String(test.format_string), String(test.input_string)},
        Datetime(datetime)));
  }

  // Add nano test cases.
  for (const ParseTimestampTest& test :
       GetParseNanoAndPicoTimestampSensitiveTests()) {
    auto ConvertTimestampStringToDatetime =
        [](absl::string_view timestamp_string,
           absl::string_view timezone_string, functions::TimestampScale scale) {
          if (IsExpectedError(timestamp_string)) {
            return absl::StatusOr<Value>(FunctionEvalError());
          }
          absl::Time timestamp;
          absl::TimeZone timezone;
          GOOGLESQL_CHECK_OK(functions::MakeTimeZone(timezone_string, &timezone));
          GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
              timestamp_string, timezone, scale, false /* allow_tz_in_str */,
              &timestamp));
          DatetimeValue datetime;
          GOOGLESQL_CHECK_OK(functions::ConvertTimestampToDatetime(timestamp, timezone,
                                                         &datetime));
          ABSL_CHECK(datetime.IsValid());
          return absl::StatusOr<Value>(Value::Datetime(datetime));
        };
    test_cases.push_back(CivilTimeTestCase(
        {String(test.format()), String(test.timestamp_string())},
        ConvertTimestampStringToDatetime(
            test.expected_result(), test.default_time_zone(), kMicroseconds),
        ConvertTimestampStringToDatetime(test.nano_expected_result(),
                                         test.default_time_zone(),
                                         kNanoseconds),
        DatetimeType()));
  }

  return PrepareCivilTimeTestCaseAsFunctionTestCall(test_cases,
                                                    "parse_datetime");
}

static std::vector<ParseTimestampTest> GetParseTimestampTests() {
  return ConcatTests<ParseTimestampTest>({
      GetParseNanoAndPicoTimestampSensitiveTests(),
      GetParseTimestampSpecificTests(),
      GetParseTimestampCommonTests(),
  });
}

struct ParseDateTest {
  std::string format_string;    // format string
  std::string date_string;      // date string to parse
  std::string expected_result;  // expected output string
};

static std::vector<ParseDateTest> GetParseDateTests() {
  std::vector<ParseDateTest> tests;

  // Adds all the common tests.
  for (const ParseDateTimestampCommonTest& common_test
          : GetParseDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_DATE ||
        common_test.parts == TEST_FORMAT_ANY) {
      tests.push_back({common_test.format_string, common_test.input_string,
                       common_test.expected_result});
    } else {
      // Returns error for unsupported formats.
      tests.push_back(
          {common_test.format_string, common_test.input_string, ""});
    }
  }

  return tests;
}

static Value GetMicrosResult(const ParseTimestampTest& test) {
  if (test.expected_result().empty()) {
    return NullTimestamp();
  }
  absl::Time timestamp;
  absl::TimeZone zone;
  GOOGLESQL_CHECK_OK(functions::MakeTimeZone(test.default_time_zone(), &zone));
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(test.expected_result(), zone,
                                               kNanoseconds, true, &timestamp));
  return Value::Timestamp(timestamp);
}

static Value GetNanosResult(const ParseTimestampTest& test) {
  if (test.nano_expected_result().empty()) {
    return NullTimestamp();
  }
  absl::Time timestamp;
  absl::TimeZone zone;
  GOOGLESQL_CHECK_OK(functions::MakeTimeZone(test.default_time_zone(), &zone));
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      test.nano_expected_result(), zone, kNanoseconds, true, &timestamp));
  return Value::Timestamp(timestamp);
}

static void AddTestCasesForParseTimestampTest(
    const ParseTimestampTest& test, absl::string_view function_name,
    absl::Span<const ValueConstructor> args,
    std::vector<FunctionTestCall>& tests) {
  if (test.expected_result().empty() && test.nano_expected_result().empty()) {
    tests.emplace_back(function_name, args, NullTimestamp(),
                       absl::StatusCode::kOutOfRange);
    return;
  }
  Value micros_value = GetMicrosResult(test);
  Value nanos_value = GetNanosResult(test);
  if (test.expected_result() == test.nano_expected_result()) {
    tests.emplace_back(function_name,
                       QueryParamsWithResult(args, micros_value));
    return;
  }
  absl::StatusCode micros_status = IsExpectedError(test.expected_result())
                                       ? absl::StatusCode::kOutOfRange
                                       : absl::StatusCode::kOk;
  tests.emplace_back(function_name,
                     QueryParamsWithResult(args, micros_value, micros_status)
                         .AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS));
  absl::StatusCode nanos_status = IsExpectedError(test.nano_expected_result())
                                      ? absl::StatusCode::kOutOfRange
                                      : absl::StatusCode::kOk;
  tests.emplace_back(function_name,
                     QueryParamsWithResult(args, nanos_value, nanos_status)
                         .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS));
}

std::vector<FunctionTestCall> GetFunctionTestsParseDateTimestamp() {
  std::vector<FunctionTestCall> tests;
  for (const ParseTimestampTest& test : GetParseTimestampTests()) {
    std::vector<ValueConstructor> args = {String(test.format()),
                                          String(test.timestamp_string()),
                                          String(test.default_time_zone())};
    AddTestCasesForParseTimestampTest(test, "parse_timestamp", args, tests);
  }

  // Adds PARSE_DATE() tests.
  for (const ParseDateTest& test : GetParseDateTests()) {
    if (test.expected_result.empty()) {
      // An error is expected.
      tests.push_back(
          { "parse_date",
            { String(test.format_string), String(test.date_string)},
            NullDate(),
            OUT_OF_RANGE});
    } else {
      tests.push_back(
          { "parse_date",
            { String(test.format_string), String(test.date_string)},
            DateFromStr(test.expected_result)});
    }
  }

  // Adds PARSE_DATETIME() tests
  for (const FunctionTestCall& test : GetFunctionTestsParseDatetime()) {
    tests.push_back(test);
  }
  // Adds PARSE_TIME() tests
  for (const FunctionTestCall& test : GetFunctionTestsParseTime()) {
    tests.push_back(test);
  }

  return tests;
}

// This struct is the same as ParseDateTimestampCommonTest except that it has
// an additional field <current_date> to construct <current_date> or
// <current_timestamp> arguments for test cases.
struct CastStringToDateTimestampCommonTest : ParseDateTimestampCommonTest {
  int32_t current_date;
  std::string nano_expected_result;
  std::string pico_expected_result;
  CastStringToDateTimestampCommonTest(absl::string_view format_string_in,
                                      TestFormatDateTimeParts parts_in,
                                      absl::string_view input_string_in,
                                      int32_t current_date_in,
                                      absl::string_view expected_result_in)
      : CastStringToDateTimestampCommonTest(
            format_string_in, parts_in, input_string_in, current_date_in,
            expected_result_in,
            /*nano_expected_result_in=*/expected_result_in) {}

  CastStringToDateTimestampCommonTest(absl::string_view format_string_in,
                                      TestFormatDateTimeParts parts_in,
                                      absl::string_view input_string_in,
                                      int32_t current_date_in,
                                      absl::string_view expected_result_in,
                                      absl::string_view nano_expected_result_in)
      : ParseDateTimestampCommonTest{std::string(format_string_in), parts_in,
                                     std::string(input_string_in),
                                     std::string(expected_result_in)},
        current_date(current_date_in),
        nano_expected_result(nano_expected_result_in),
        pico_expected_result(nano_expected_result_in) {}

  CastStringToDateTimestampCommonTest(absl::string_view format_string_in,
                                      TestFormatDateTimeParts parts_in,
                                      absl::string_view input_string_in,
                                      absl::string_view expected_result_in)
      : CastStringToDateTimestampCommonTest(
            format_string_in, parts_in, input_string_in, expected_result_in,
            /*nano_expected_result_in=*/expected_result_in) {}

  CastStringToDateTimestampCommonTest(absl::string_view format_string_in,
                                      TestFormatDateTimeParts parts_in,
                                      absl::string_view input_string_in,
                                      absl::string_view expected_result_in,
                                      absl::string_view nano_expected_result_in)
      : CastStringToDateTimestampCommonTest(
            format_string_in, parts_in, input_string_in, expected_result_in,
            nano_expected_result_in,
            /*pico_expected_result_in=*/nano_expected_result_in) {}

  CastStringToDateTimestampCommonTest(absl::string_view format_string_in,
                                      TestFormatDateTimeParts parts_in,
                                      absl::string_view input_string_in,
                                      absl::string_view expected_result_in,
                                      absl::string_view nano_expected_result_in,
                                      absl::string_view pico_expected_result_in)
      : ParseDateTimestampCommonTest{std::string(format_string_in), parts_in,
                                     std::string(input_string_in),
                                     std::string(expected_result_in)},
        nano_expected_result(nano_expected_result_in),
        pico_expected_result(pico_expected_result_in) {
    // Sets value of <current_date> to 1970-1-1 if not provided in constructor
    // arguments.
    GOOGLESQL_CHECK_OK(functions::ConstructDate(1970, 1, 1, &current_date));
  }
};

// For the test cases returned by this function, they will have microsecond
// precision if the subsecond part is relevant.
static std::vector<CastStringToDateTimestampCommonTest>
GetCastStringToDateTimestampCommonTests() {
  const char kTestExpected19700101[] = "1970-01-01";
  const char* kTestExpectedDefault = kTestExpected19700101;
  const char kTestExpected20010203[] = "2001-02-03";
  const char* dummy_input_timestamp = "1";
  int32_t date_2002_1_1, date_2002_2_2, date_2299_2_1;
  GOOGLESQL_CHECK_OK(functions::ConstructDate(2002, 1, 1, &date_2002_1_1));
  GOOGLESQL_CHECK_OK(functions::ConstructDate(2002, 2, 2, &date_2002_2_2));
  GOOGLESQL_CHECK_OK(functions::ConstructDate(2299, 2, 1, &date_2299_2_1));

  std::vector<CastStringToDateTimestampCommonTest> tests({
      // Any unspecified field is initialized from
      // current_year(from <current_date>)-current_month(from <current_date>)-01
      // 00:00:00 in the default time zone. Value of <current_date> is set to
      // 1970-1-1 when it is not present.
      {"", TEST_FORMAT_ANY, "", kTestExpectedDefault},
      {"", TEST_FORMAT_DATE, "", date_2002_2_2, "2002-02-01"},

      // Elements of "kLiteral" type.
      {"-", TEST_FORMAT_ANY, "-", kTestExpectedDefault},
      {"-", TEST_FORMAT_ANY, "--", EXPECT_ERROR},
      {"-", TEST_FORMAT_ANY, "?", EXPECT_ERROR},
      {".", TEST_FORMAT_ANY, ".", kTestExpectedDefault},
      {".", TEST_FORMAT_ANY, "..", EXPECT_ERROR},
      {".", TEST_FORMAT_ANY, "?", EXPECT_ERROR},
      {"/", TEST_FORMAT_ANY, "/", kTestExpectedDefault},
      {"/", TEST_FORMAT_ANY, "//", EXPECT_ERROR},
      {"/", TEST_FORMAT_ANY, "?", EXPECT_ERROR},
      {",", TEST_FORMAT_ANY, ",", kTestExpectedDefault},
      {",", TEST_FORMAT_ANY, ",,", EXPECT_ERROR},
      {",", TEST_FORMAT_ANY, "?", EXPECT_ERROR},
      {"'", TEST_FORMAT_ANY, "'", kTestExpectedDefault},
      {"'", TEST_FORMAT_ANY, "''", EXPECT_ERROR},
      {"'", TEST_FORMAT_ANY, "?", EXPECT_ERROR},
      {";", TEST_FORMAT_ANY, ";", kTestExpectedDefault},
      {";", TEST_FORMAT_ANY, ";;", EXPECT_ERROR},
      {";", TEST_FORMAT_ANY, "?", EXPECT_ERROR},
      {":", TEST_FORMAT_ANY, ":", kTestExpectedDefault},
      {":", TEST_FORMAT_ANY, "::", EXPECT_ERROR},
      {":", TEST_FORMAT_ANY, "?", EXPECT_ERROR},

      // Elements of "kWhitespace" type match one or more consecutive Unicode
      // whitespace characters. "\u1680" refers to a Unicode space "U+1680".
      {"- -", TEST_FORMAT_ANY, "- \n\t\u1680-", kTestExpectedDefault},
      {"- -", TEST_FORMAT_ANY, "- ?\t\u1680-", EXPECT_ERROR},
      {"- -", TEST_FORMAT_ANY, "- \n\t\u1680\u1680\n\t -",
       kTestExpectedDefault},
      {"-  -", TEST_FORMAT_ANY, "- \n\t\u1680-", kTestExpectedDefault},
      {"-                -", TEST_FORMAT_ANY, "- \n\t\u1680-",
       kTestExpectedDefault},
      {"- -", TEST_FORMAT_ANY, "--", EXPECT_ERROR},
      // A Sequence of whitespace characters other than ' ' (ASCII 32) will not
      // be parsed as an elemenet of "kWhitespace" type, therefore cannot match
      // any Unicode whitespace characters.
      {"-\n\r\t\u1680-", TEST_FORMAT_ANY, "-\n\r\t\u1680-", EXPECT_ERROR},

      // Elements of "kDoubleQuotedLiteral" type.
      {R"("abc")", TEST_FORMAT_ANY, "abc", kTestExpectedDefault},
      {R"("")", TEST_FORMAT_ANY, "", kTestExpectedDefault},
      // Escape sequences inside the format element of "kDoubleQuotedLiteral"
      // type.
      {R"("abc\\")", TEST_FORMAT_ANY, R"(abc\)", kTestExpectedDefault},
      {R"("abc\"")", TEST_FORMAT_ANY, R"(abc")", kTestExpectedDefault},
      // Only "\\" and "\"" escape sequences are supported inside format
      // elements of "kDoubleQuotedLiteral" type.
      {R"("abc\t")", TEST_FORMAT_ANY, "abc\t", EXPECT_ERROR},
      // Missing the ending '"' to close the format element of
      // "kDoubleQuotedLiteral" type.
      {R"("abc)", TEST_FORMAT_ANY, "abc", EXPECT_ERROR},
      {R"("abc\")", TEST_FORMAT_ANY, "abc\"", EXPECT_ERROR},
      {R"("%ыфщыфщ")", TEST_FORMAT_ANY, "%ыфщыфщ", kTestExpectedDefault},
      // Consecutive space characters (ASCII 32) inside format element of
      // "kDoubleQuotedLiteral" type can only match the same number of space
      // characters.
      {R"(-"  "-)", TEST_FORMAT_ANY, "-  -", kTestExpectedDefault},
      {R"(-"  "-)", TEST_FORMAT_ANY, "- -", EXPECT_ERROR},
      {R"(-"  "-)", TEST_FORMAT_ANY, "- \n-", EXPECT_ERROR},
      {R"(-"  "-)", TEST_FORMAT_ANY, "- \u1680-", EXPECT_ERROR},
      {R"(-./,';: "abc")", TEST_FORMAT_ANY, "-./,';:\n\t\u1680abc",
       kTestExpectedDefault},

      // Leading and trailing whitespaces in timestamp string.
      {"-,", TEST_FORMAT_ANY, "\u1680  -,   \u1680", kTestExpectedDefault},
      // Trailing empty format elements in the format string.
      {"--.\"\"\"\"", TEST_FORMAT_ANY, "--.", kTestExpectedDefault},

      // Elements in "kYear" category.
      // "YYYY" - year value.
      // TODO: "YYYY"/"RRRR" can successfully match integer strings
      // whose values are 0 ("0", "00", etc.) or 10000 with other format
      // elements. Change related cases when elements of "TIMEZONE" type are
      // added.
      {"YYYY", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "1", "0001-01-01"},
      {"YYYY", TEST_FORMAT_DATE, "12", "0012-01-01"},
      {"YYYY", TEST_FORMAT_DATE, "123", "0123-01-01"},
      {"YYYY", TEST_FORMAT_DATE, "1234", "1234-01-01"},
      {"YYYY", TEST_FORMAT_DATE, "01234", "1234-01-01"},
      {"YYYY", TEST_FORMAT_DATE, "001234", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "9999", "9999-01-01"},
      {"YYYY", TEST_FORMAT_DATE, "10000", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "10001", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "123456", EXPECT_ERROR},
      {"YYYY", TEST_FORMAT_DATE, "1234", date_2002_2_2, "1234-02-01"},
      // "YYY" - the last 3 digits of the year value.
      {"YYY", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"YYY", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"YYY", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"YYY", TEST_FORMAT_DATE, "0", "1000-01-01"},
      {"YYY", TEST_FORMAT_DATE, "1", "1001-01-01"},
      {"YYY", TEST_FORMAT_DATE, "12", "1012-01-01"},
      {"YYY", TEST_FORMAT_DATE, "123", "1123-01-01"},
      {"YYY", TEST_FORMAT_DATE, "0123", EXPECT_ERROR},
      {"YYY", TEST_FORMAT_DATE, "00123", EXPECT_ERROR},
      {"YYY", TEST_FORMAT_DATE, "999", "1999-01-01"},
      {"YYY", TEST_FORMAT_DATE, "123", date_2002_2_2, "2123-02-01"},
      // "YY" - the last 2 digits of the year value.
      {"YY", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"YY", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"YY", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"YY", TEST_FORMAT_DATE, "0", "1900-01-01"},
      {"YY", TEST_FORMAT_DATE, "1", "1901-01-01"},
      {"YY", TEST_FORMAT_DATE, "12", "1912-01-01"},
      {"YY", TEST_FORMAT_DATE, "012", EXPECT_ERROR},
      {"YY", TEST_FORMAT_DATE, "0012", EXPECT_ERROR},
      {"YY", TEST_FORMAT_DATE, "99", "1999-01-01"},
      {"YY", TEST_FORMAT_DATE, "12", date_2002_2_2, "2012-02-01"},
      // "Y" - the last digit of the year value.
      {"Y", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"Y", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"Y", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"Y", TEST_FORMAT_DATE, "0", "1970-01-01"},
      {"Y", TEST_FORMAT_DATE, "1", "1971-01-01"},
      {"Y", TEST_FORMAT_DATE, "01", EXPECT_ERROR},
      {"Y", TEST_FORMAT_DATE, "001", EXPECT_ERROR},
      {"Y", TEST_FORMAT_DATE, "9", "1979-01-01"},
      {"Y", TEST_FORMAT_DATE, "1", date_2002_2_2, "2001-02-01"},
      // "RRRR" - year value. It has the same behavior as format element "YYYY".
      {"RRRR", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "1", "0001-01-01"},
      {"RRRR", TEST_FORMAT_DATE, "12", "0012-01-01"},
      {"RRRR", TEST_FORMAT_DATE, "123", "0123-01-01"},
      {"RRRR", TEST_FORMAT_DATE, "1234", "1234-01-01"},
      {"RRRR", TEST_FORMAT_DATE, "01234", "1234-01-01"},
      {"RRRR", TEST_FORMAT_DATE, "001234", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "9999", "9999-01-01"},
      {"RRRR", TEST_FORMAT_DATE, "10000", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "10001", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "123456", EXPECT_ERROR},
      {"RRRR", TEST_FORMAT_DATE, "1234", date_2002_2_2, "1234-02-01"},
      // "RR" - the last 2 digits of the year value. The first 2 digits of the
      // output year value can be different from that of the current year from
      // <current_date> (more details at (broken link))
      {"RR", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"RR", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"RR", TEST_FORMAT_DATE, "-1", EXPECT_ERROR},
      {"RR", TEST_FORMAT_DATE, "0", date_2002_1_1, "2000-01-01"},
      {"RR", TEST_FORMAT_DATE, "12", date_2002_1_1, "2012-01-01"},
      {"RR", TEST_FORMAT_DATE, "51", date_2002_1_1, "1951-01-01"},
      {"RR", TEST_FORMAT_DATE, "12", date_2299_2_1, "2312-02-01"},
      {"RR", TEST_FORMAT_DATE, "51", date_2299_2_1, "2251-02-01"},
      {"RR", TEST_FORMAT_DATE, "051", date_2299_2_1, EXPECT_ERROR},
      {"RR", TEST_FORMAT_DATE, "0051", date_2299_2_1, EXPECT_ERROR},
      // "Y,YYY" - year value in the pattern of "X,XXX" or "XX,XXX".
      {"Y,YYY", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "-0,001", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "0,000", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, ",111", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "0,111", "0111-01-01"},
      {"Y,YYY", TEST_FORMAT_DATE, "1,000", "1000-01-01"},
      {"Y,YYY", TEST_FORMAT_DATE, "01,000", "1000-01-01"},
      {"Y,YYY", TEST_FORMAT_DATE, "001,000", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "0001,000", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "9,222", "9222-01-01"},
      {"Y,YYY", TEST_FORMAT_DATE, "9,", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "9,2", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "9,22", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "9,2222", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "9,22222", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "1,012", "1012-01-01"},
      {"Y,YYY", TEST_FORMAT_DATE, "9,999", "9999-01-01"},
      {"Y,YYY", TEST_FORMAT_DATE, "10,000", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "10,001", EXPECT_ERROR},
      {"Y,YYY", TEST_FORMAT_DATE, "9,999", date_2002_2_2, "9999-02-01"},

      // Elements in "kMonth" category.
      // "MM" - month 1-12.
      {"MM", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"MM", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"MM", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"MM", TEST_FORMAT_DATE, "1", "1970-01-01"},
      {"MM", TEST_FORMAT_DATE, "01", "1970-01-01"},
      {"MM", TEST_FORMAT_DATE, "001", EXPECT_ERROR},
      {"MM", TEST_FORMAT_DATE, "0001", EXPECT_ERROR},
      {"MM", TEST_FORMAT_DATE, "2", "1970-02-01"},
      {"MM", TEST_FORMAT_DATE, "12", "1970-12-01"},
      {"MM", TEST_FORMAT_DATE, "13", EXPECT_ERROR},
      {"MM", TEST_FORMAT_DATE, "03", date_2002_2_2, "2002-03-01"},
      // "MON" - abbreviated month name. The matching is case insensitive.
      {"MON", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"MON", TEST_FORMAT_DATE, "11", EXPECT_ERROR},
      {"MON", TEST_FORMAT_DATE, "JAN", "1970-01-01"},
      {"MON", TEST_FORMAT_DATE, "Jan", "1970-01-01"},
      {"MON", TEST_FORMAT_DATE, "jan", "1970-01-01"},
      {"MON", TEST_FORMAT_DATE, "Feb", "1970-02-01"},
      {"MON", TEST_FORMAT_DATE, "Mar", "1970-03-01"},
      {"MON", TEST_FORMAT_DATE, "Apr", "1970-04-01"},
      {"MON", TEST_FORMAT_DATE, "May", "1970-05-01"},
      {"MON", TEST_FORMAT_DATE, "Jun", "1970-06-01"},
      {"MON", TEST_FORMAT_DATE, "Jul", "1970-07-01"},
      {"MON", TEST_FORMAT_DATE, "Aug", "1970-08-01"},
      {"MON", TEST_FORMAT_DATE, "Sep", "1970-09-01"},
      {"MON", TEST_FORMAT_DATE, "Sept", EXPECT_ERROR},
      {"MON", TEST_FORMAT_DATE, "Oct", "1970-10-01"},
      {"MON", TEST_FORMAT_DATE, "Nov", "1970-11-01"},
      {"MON", TEST_FORMAT_DATE, "Dec", "1970-12-01"},
      {"MON", TEST_FORMAT_DATE, "NAM", EXPECT_ERROR},
      {"MON", TEST_FORMAT_DATE, "Oct", date_2002_2_2, "2002-10-01"},
      // "MONTH" - full month name. The matching is case insensitive.
      {"MONTH", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"MONTH", TEST_FORMAT_DATE, "11", EXPECT_ERROR},
      {"MONTH", TEST_FORMAT_DATE, "January", "1970-01-01"},
      {"MONTH", TEST_FORMAT_DATE, "JANUARY", "1970-01-01"},
      {"MONTH", TEST_FORMAT_DATE, "january", "1970-01-01"},
      {"MONTH", TEST_FORMAT_DATE, "jAnUaRY", "1970-01-01"},
      {"MONTH", TEST_FORMAT_DATE, "February", "1970-02-01"},
      {"MONTH", TEST_FORMAT_DATE, "March", "1970-03-01"},
      {"MONTH", TEST_FORMAT_DATE, "April", "1970-04-01"},
      {"MONTH", TEST_FORMAT_DATE, "May", "1970-05-01"},
      {"MONTH", TEST_FORMAT_DATE, "June", "1970-06-01"},
      {"MONTH", TEST_FORMAT_DATE, "July", "1970-07-01"},
      {"MONTH", TEST_FORMAT_DATE, "August", "1970-08-01"},
      {"MONTH", TEST_FORMAT_DATE, "September", "1970-09-01"},
      {"MONTH", TEST_FORMAT_DATE, "October", "1970-10-01"},
      {"MONTH", TEST_FORMAT_DATE, "November", "1970-11-01"},
      {"MONTH", TEST_FORMAT_DATE, "December", "1970-12-01"},
      {"MONTH", TEST_FORMAT_DATE, "NotAMonth", EXPECT_ERROR},
      {"MONTH", TEST_FORMAT_DATE, "July", date_2002_2_2, "2002-07-01"},

      // Elements in "kDay" category.
      // "DD" - day of month 1-31.
      {"DD", TEST_FORMAT_DATE, "", EXPECT_ERROR},
      {"DD", TEST_FORMAT_DATE, "non_digit", EXPECT_ERROR},
      {"DD", TEST_FORMAT_DATE, "0", EXPECT_ERROR},
      {"DD", TEST_FORMAT_DATE, "1", "1970-01-01"},
      {"DD", TEST_FORMAT_DATE, "01", "1970-01-01"},
      {"DD", TEST_FORMAT_DATE, "001", EXPECT_ERROR},
      {"DD", TEST_FORMAT_DATE, "9", "1970-01-09"},
      {"DD", TEST_FORMAT_DATE, "09", "1970-01-09"},
      {"DD", TEST_FORMAT_DATE, "31", "1970-01-31"},
      {"DD", TEST_FORMAT_DATE, "32", EXPECT_ERROR},
      {"DD", TEST_FORMAT_DATE, "15", date_2002_2_2, "2002-02-15"},

      // Test different ordering and cases of "YYYY", "MM", "DD" format
      // elements, and they should all give the same result.
      {"YYYY MM DD", TEST_FORMAT_DATE, "2001 02 03", kTestExpected20010203},
      {"YyYY dd Mm", TEST_FORMAT_DATE, "2001 03 02", kTestExpected20010203},
      {"mM YyyY Dd", TEST_FORMAT_DATE, "02 2001 03", kTestExpected20010203},
      {"mm dD YyYy", TEST_FORMAT_DATE, "02 03 2001", kTestExpected20010203},
      {"DD YYYy mM", TEST_FORMAT_DATE, "03 2001 02", kTestExpected20010203},
      {"dd Mm yyyy", TEST_FORMAT_DATE, "03 02 2001", kTestExpected20010203},

      // Dates that do not exist.
      {"YYYY MM DD", TEST_FORMAT_DATE, "1234 4 31", EXPECT_ERROR},
      {"YYYY MM DD", TEST_FORMAT_DATE, "2001 09 31", EXPECT_ERROR},
      {"YYYY MM DD", TEST_FORMAT_DATE, "7777 02 29", EXPECT_ERROR},
      {"YYYY MM DD", TEST_FORMAT_DATE, "1900 02 29", EXPECT_ERROR},
      {"YYYY MM DD", TEST_FORMAT_DATE, "2004 02 29", "2004-02-29"},
      {"YYYY MM DD", TEST_FORMAT_DATE, "1600 02 29", "1600-02-29"},

      // Date boundaries and out-of-range cases.
      {"YYYY-MM-DD", TEST_FORMAT_DATE, "0001-01-01", "0001-01-01"},
      {"YYYY-MM-DD", TEST_FORMAT_DATE, "9999-12-31", "9999-12-31"},
      {"YYYY-MM-DD", TEST_FORMAT_DATE, "0000-12-31", EXPECT_ERROR},
      {"YYYY-MM-DD", TEST_FORMAT_DATE, "10000-01-01", EXPECT_ERROR},

      // Elements in "kHour" category.
      // "HH24" - hour value of a 24-hour clock.
      {"HH24", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"HH24", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"HH24", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"HH24", TEST_FORMAT_TIME, "0", "00:00:00"},
      {"HH24", TEST_FORMAT_TIME, "1", "01:00:00"},
      {"HH24", TEST_FORMAT_TIME, "01", "01:00:00"},
      {"HH24", TEST_FORMAT_TIME, "001", EXPECT_ERROR},
      {"HH24", TEST_FORMAT_TIME, "0001", EXPECT_ERROR},
      {"HH24", TEST_FORMAT_TIME, "2", "02:00:00"},
      {"HH24", TEST_FORMAT_TIME, "12", "12:00:00"},
      {"HH24", TEST_FORMAT_TIME, "13", "13:00:00"},
      {"HH24", TEST_FORMAT_TIME, "23", "23:00:00"},
      {"HH24", TEST_FORMAT_TIME, "24", EXPECT_ERROR},
      {"HH24", TEST_FORMAT_DATE_AND_TIME, "15", date_2002_2_2,
       "2002-02-01 15:00:00"},

      // Elements in "kMinute" category.
      // "MI" - minutes 00-59.
      {"MI", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"MI", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"MI", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"MI", TEST_FORMAT_TIME, "0", "00:00:00"},
      {"MI", TEST_FORMAT_TIME, "1", "00:01:00"},
      {"MI", TEST_FORMAT_TIME, "01", "00:01:00"},
      {"MI", TEST_FORMAT_TIME, "001", EXPECT_ERROR},
      {"MI", TEST_FORMAT_TIME, "0001", EXPECT_ERROR},
      {"MI", TEST_FORMAT_TIME, "30", "00:30:00"},
      {"MI", TEST_FORMAT_TIME, "59", "00:59:00"},
      {"MI", TEST_FORMAT_TIME, "60", EXPECT_ERROR},
      {"MI", TEST_FORMAT_DATE_AND_TIME, "15", date_2002_2_2,
       "2002-02-01 00:15:00"},

      // Elements in "kSecond" category.
      // "SS" - seconds 00-59.
      {"SS", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"SS", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"SS", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"SS", TEST_FORMAT_TIME, "0", "00:00:00"},
      {"SS", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"SS", TEST_FORMAT_TIME, "01", "00:00:01"},
      {"SS", TEST_FORMAT_TIME, "001", EXPECT_ERROR},
      {"SS", TEST_FORMAT_TIME, "0001", EXPECT_ERROR},
      {"SS", TEST_FORMAT_TIME, "30", "00:00:30"},
      {"SS", TEST_FORMAT_TIME, "59", "00:00:59"},
      {"SS", TEST_FORMAT_TIME, "60", EXPECT_ERROR},
      {"SS", TEST_FORMAT_DATE_AND_TIME, "15", date_2002_2_2,
       "2002-02-01 00:00:15"},

      // "SSSSS" - number of seconds past midnight.
      {"SSSSS", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"SSSSS", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"SSSSS", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"SSSSS", TEST_FORMAT_TIME, "0", "00:00:00"},
      {"SSSSS", TEST_FORMAT_TIME, "1", "00:00:01"},
      {"SSSSS", TEST_FORMAT_TIME, "01", "00:00:01"},
      {"SSSSS", TEST_FORMAT_TIME, "001", "00:00:01"},
      {"SSSSS", TEST_FORMAT_TIME, "0001", "00:00:01"},
      {"SSSSS", TEST_FORMAT_TIME, "00001", "00:00:01"},
      {"SSSSS", TEST_FORMAT_TIME, "000001", EXPECT_ERROR},
      {"SSSSS", TEST_FORMAT_TIME, "0000001", EXPECT_ERROR},
      {"SSSSS", TEST_FORMAT_TIME, "60", "00:01:00"},
      {"SSSSS", TEST_FORMAT_TIME, "3661", "01:01:01"},
      {"SSSSS", TEST_FORMAT_TIME, "36061", "10:01:01"},
      {"SSSSS", TEST_FORMAT_TIME, absl::StrCat(kNaiveNumSecondsPerDay - 1),
       "23:59:59"},
      {"SSSSS", TEST_FORMAT_TIME, absl::StrCat(kNaiveNumSecondsPerDay),
       EXPECT_ERROR},
      {"SSSSS", TEST_FORMAT_DATE_AND_TIME, "3661", date_2002_2_2,
       "2002-02-01 01:01:01"},

      // "FFN" - subsecond value. The value of "N" indicates the maximal number
      // of digits to parse.
      {"FF1", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF1", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF1", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF1", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF1", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF1", TEST_FORMAT_TIME, "12", EXPECT_ERROR},
      {"FF1", TEST_FORMAT_TIME, "123", EXPECT_ERROR},
      {"FF1", TEST_FORMAT_DATE_AND_TIME, "1", date_2002_2_2,
       "2002-02-01 00:00:00.1"},

      {"FF2", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF2", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF2", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF2", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF2", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF2", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF2", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF2", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF2", TEST_FORMAT_TIME, "123", EXPECT_ERROR},
      {"FF2", TEST_FORMAT_TIME, "1234", EXPECT_ERROR},
      {"FF2", TEST_FORMAT_DATE_AND_TIME, "12", date_2002_2_2,
       "2002-02-01 00:00:00.12"},

      {"FF3", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF3", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF3", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF3", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF3", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF3", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF3", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF3", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF3", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF3", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF3", TEST_FORMAT_TIME, "1234", EXPECT_ERROR},
      {"FF3", TEST_FORMAT_TIME, "12345", EXPECT_ERROR},
      {"FF3", TEST_FORMAT_DATE_AND_TIME, "123", date_2002_2_2,
       "2002-02-01 00:00:00.123"},

      {"FF4", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF4", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF4", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF4", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF4", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF4", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF4", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF4", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF4", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF4", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF4", TEST_FORMAT_TIME, "1234", "00:00:00.1234"},
      {"FF4", TEST_FORMAT_TIME, "12345", EXPECT_ERROR},
      {"FF4", TEST_FORMAT_TIME, "123456", EXPECT_ERROR},
      {"FF4", TEST_FORMAT_DATE_AND_TIME, "1234", date_2002_2_2,
       "2002-02-01 00:00:00.1234"},

      {"FF5", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF5", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF5", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF5", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF5", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF5", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF5", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF5", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF5", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF5", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF5", TEST_FORMAT_TIME, "1234", "00:00:00.1234"},
      {"FF5", TEST_FORMAT_TIME, "12345", "00:00:00.12345"},
      {"FF5", TEST_FORMAT_TIME, "123456", EXPECT_ERROR},
      {"FF5", TEST_FORMAT_TIME, "1234567", EXPECT_ERROR},
      {"FF5", TEST_FORMAT_DATE_AND_TIME, "12345", date_2002_2_2,
       "2002-02-01 00:00:00.12345"},

      {"FF6", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF6", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF6", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF6", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF6", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF6", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF6", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF6", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF6", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF6", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF6", TEST_FORMAT_TIME, "1234", "00:00:00.1234"},
      {"FF6", TEST_FORMAT_TIME, "12345", "00:00:00.12345"},
      {"FF6", TEST_FORMAT_TIME, "123456", "00:00:00.123456"},
      {"FF6", TEST_FORMAT_TIME, "1234567", EXPECT_ERROR},
      {"FF6", TEST_FORMAT_TIME, "12345678", EXPECT_ERROR},
      {"FF6", TEST_FORMAT_TIME, "123456789", EXPECT_ERROR},
      {"FF6", TEST_FORMAT_DATE_AND_TIME, "123456", date_2002_2_2,
       "2002-02-01 00:00:00.123456"},

      {"FF7", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF7", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF7", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF7", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF7", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF7", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF7", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF7", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF7", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF7", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF7", TEST_FORMAT_TIME, "1234", "00:00:00.1234"},
      {"FF7", TEST_FORMAT_TIME, "12345", "00:00:00.12345"},
      {"FF7", TEST_FORMAT_TIME, "123456", "00:00:00.123456"},
      {"FF7", TEST_FORMAT_TIME, "1234567", "00:00:00.123456",
       "00:00:00.1234567"},
      {"FF7", TEST_FORMAT_TIME, "12345678", EXPECT_ERROR},
      {"FF7", TEST_FORMAT_TIME, "123456789", EXPECT_ERROR},
      {"FF7", TEST_FORMAT_DATE_AND_TIME, "1234567", date_2002_2_2,
       "2002-02-01 00:00:00.123456", "2002-02-01 00:00:00.1234567"},

      {"FF8", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF8", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF8", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF8", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF8", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF8", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF8", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF8", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF8", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF8", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF8", TEST_FORMAT_TIME, "1234", "00:00:00.1234"},
      {"FF8", TEST_FORMAT_TIME, "12345", "00:00:00.12345"},
      {"FF8", TEST_FORMAT_TIME, "123456", "00:00:00.123456"},
      {"FF8", TEST_FORMAT_TIME, "1234567", "00:00:00.123456",
       "00:00:00.1234567"},
      {"FF8", TEST_FORMAT_TIME, "12345678", "00:00:00.123456",
       "00:00:00.12345678"},
      {"FF8", TEST_FORMAT_TIME, "123456789", EXPECT_ERROR},
      {"FF8", TEST_FORMAT_TIME, "1234567890", EXPECT_ERROR},
      {"FF8", TEST_FORMAT_DATE_AND_TIME, "12345678", date_2002_2_2,
       "2002-02-01 00:00:00.123456", "2002-02-01 00:00:00.12345678"},

      {"FF9", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF9", TEST_FORMAT_TIME, "non_digit", EXPECT_ERROR},
      {"FF9", TEST_FORMAT_TIME, "-1", EXPECT_ERROR},
      {"FF9", TEST_FORMAT_TIME, "0", "00:00:00.0"},
      {"FF9", TEST_FORMAT_TIME, "1", "00:00:00.1"},
      {"FF9", TEST_FORMAT_TIME, "01", "00:00:00.01"},
      {"FF9", TEST_FORMAT_TIME, "10", "00:00:00.1"},
      {"FF9", TEST_FORMAT_TIME, "010", "00:00:00.01"},
      {"FF9", TEST_FORMAT_TIME, "12", "00:00:00.12"},
      {"FF9", TEST_FORMAT_TIME, "123", "00:00:00.123"},
      {"FF9", TEST_FORMAT_TIME, "1234", "00:00:00.1234"},
      {"FF9", TEST_FORMAT_TIME, "12345", "00:00:00.12345"},
      {"FF9", TEST_FORMAT_TIME, "123456", "00:00:00.123456"},
      {"FF9", TEST_FORMAT_TIME, "1234567", "00:00:00.123456",
       "00:00:00.1234567"},
      {"FF9", TEST_FORMAT_TIME, "12345678", "00:00:00.123456",
       "00:00:00.12345678"},
      {"FF9", TEST_FORMAT_TIME, "123456789", "00:00:00.123456",
       "00:00:00.123456789"},
      {"FF9", TEST_FORMAT_TIME, "1234567890", EXPECT_ERROR},
      {"FF9", TEST_FORMAT_TIME, "12345678901", EXPECT_ERROR},
      {"FF9", TEST_FORMAT_DATE_AND_TIME, "123456789", date_2002_2_2,
       "2002-02-01 00:00:00.123456", "2002-02-01 00:00:00.123456789"},

      {"FF12", TEST_FORMAT_TIME, "", EXPECT_ERROR},
      {"FF12", TEST_FORMAT_TIME, "1", EXPECT_ERROR, EXPECT_ERROR, "00:00:00.1"},
      {"FF12", TEST_FORMAT_TIME, "123456789321", EXPECT_ERROR, EXPECT_ERROR,
       "00:00:00.123456789321"},
      {"FF12", TEST_FORMAT_TIME, "1234567893210", EXPECT_ERROR},

      {"FF13", TEST_FORMAT_TIME, "1", EXPECT_ERROR},
      {"FF14", TEST_FORMAT_TIME, "1", EXPECT_ERROR},

      // Combined Time format elements.
      {"HH24:MI:SS.FF6", TEST_FORMAT_TIME, "01:02:03.040000",
       "01:02:03.040000"},
      {"HH:MI:SS.FF6 P.M.", TEST_FORMAT_TIME, "01:02:04.110000 A.M.",
       "01:02:04.110000"},
      {"HH12:MI:SS.FF6 A.M.", TEST_FORMAT_TIME, "01:12:05.123456 P.M.",
       "13:12:05.123456"},
      {"HH:MI:SS.FF9 A.M.", TEST_FORMAT_TIME, "01:12:05.123456789 P.M.",
       "13:12:05.123456", "13:12:05.123456789"},

      // Time edge cases.
      {"HH24:MI:SS.FF6", TEST_FORMAT_TIME, "00:00:00.000000",
       "00:00:00.000000"},
      {"HH12:MI:SS.FF6 A.M.", TEST_FORMAT_TIME, "12:00:00.000000 A.M.",
       "00:00:00.000000"},
      {"HH24:MI:SS.FF6", TEST_FORMAT_TIME, "23:59:59.999999",
       "23:59:59.999999"},
      {"HH:MI:SS.FF6 A.M.", TEST_FORMAT_TIME, "11:59:59.999999 P.M.",
       "23:59:59.999999"},
      {"HH24:MI:SS.FF6", TEST_FORMAT_TIME, "-1:59:59.999999", EXPECT_ERROR},
      {"HH24:MI:SS.FF6", TEST_FORMAT_TIME, "24:00:00.000000", EXPECT_ERROR},

      {"HH24:MI:SS.FF9", TEST_FORMAT_TIME, "00:00:00.000000000",
       "00:00:00.000000"},
      {"HH24:MI:SS.FF9", TEST_FORMAT_TIME, "23:59:59.999999999",
       "23:59:59.999999", "23:59:59.999999999"},
      {"HH24:MI:SS.FF9", TEST_FORMAT_TIME, "-1:59:59.999999999", EXPECT_ERROR},
      {"HH24:MI:SS.FF9", TEST_FORMAT_TIME, "24:00:00.000000000", EXPECT_ERROR},

      // Elements in "kTimeZone" category.
      // "TZH" - hour value of time zone offset with a leading sign from
      // {'+', '-', ' ' (same as '+')}.
      {"TZH", TEST_FORMAT_TIMEZONE, "", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "UTC", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "11", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "+non_digit", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "-15", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "-14", "1970-01-01 00:00:00-14"},
      {"TZH", TEST_FORMAT_TIMEZONE, "-8", "1970-01-01 00:00:00-08"},
      {"TZH", TEST_FORMAT_TIMEZONE, "-08", "1970-01-01 00:00:00-08"},
      {"TZH", TEST_FORMAT_TIMEZONE, "-008", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "-0008", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "-1", "1970-01-01 00:00:00-01"},
      {"TZH", TEST_FORMAT_TIMEZONE, "-0", "1970-01-01 00:00:00+00"},
      // We use a preceding "." format element so that the space character in
      // the <input_string> will not be trimmed in parsing.
      {"TZH", TEST_FORMAT_TIMEZONE, "+0", "1970-01-01 00:00:00+00"},
      {".TZH", TEST_FORMAT_TIMEZONE, ". 0", "1970-01-01 00:00:00+00"},
      {"TZH", TEST_FORMAT_TIMEZONE, "+1", "1970-01-01 00:00:00+01"},
      {".TZH", TEST_FORMAT_TIMEZONE, ". 1", "1970-01-01 00:00:00+01"},
      {"TZH", TEST_FORMAT_TIMEZONE, "+12", "1970-01-01 00:00:00+12"},
      {".TZH", TEST_FORMAT_TIMEZONE, ". 12", "1970-01-01 00:00:00+12"},
      {"TZH", TEST_FORMAT_TIMEZONE, "+14", "1970-01-01 00:00:00+14"},
      {".TZH", TEST_FORMAT_TIMEZONE, ". 14", "1970-01-01 00:00:00+14"},
      {"TZH", TEST_FORMAT_TIMEZONE, "+15", EXPECT_ERROR},
      {".TZH", TEST_FORMAT_TIMEZONE, ". 15", EXPECT_ERROR},
      {"TZH", TEST_FORMAT_TIMEZONE, "+1", date_2002_2_2,
       "2002-02-01 00:00:00+01"},

      // "TZM" - minute value of time zone offset.
      {"TZM", TEST_FORMAT_TIMEZONE, "", EXPECT_ERROR},
      {"TZM", TEST_FORMAT_TIMEZONE, "non_digit", EXPECT_ERROR},
      {"TZM", TEST_FORMAT_TIMEZONE, "-1", EXPECT_ERROR},
      {"TZM", TEST_FORMAT_TIMEZONE, "0", "1970-01-01 00:00:00+00:00"},
      {"TZM", TEST_FORMAT_TIMEZONE, "1", "1970-01-01 00:00:00+00:01"},
      {"TZM", TEST_FORMAT_TIMEZONE, "01", "1970-01-01 00:00:00+00:01"},
      {"TZM", TEST_FORMAT_TIMEZONE, "001", EXPECT_ERROR},
      {"TZM", TEST_FORMAT_TIMEZONE, "0001", EXPECT_ERROR},
      {"TZM", TEST_FORMAT_TIMEZONE, "30", "1970-01-01 00:00:00+00:30"},
      {"TZM", TEST_FORMAT_TIMEZONE, "59", "1970-01-01 00:00:00+00:59"},
      {"TZM", TEST_FORMAT_TIMEZONE, "60", EXPECT_ERROR},
      {"TZM", TEST_FORMAT_TIMEZONE, "15", date_2002_2_2,
       "2002-02-01 00:00:00+00:15"},

      // Combiend Timezone format elements.
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "UTC", EXPECT_ERROR},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "-1401", EXPECT_ERROR},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "-1400", "1970-01-01 00:00:00-14:00"},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "-0100", "1970-01-01 00:00:00-01:00"},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "-0050", "1970-01-01 00:00:00-00:50"},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "+0050", "1970-01-01 00:00:00+00:50"},
      {".TZHTZM", TEST_FORMAT_TIMEZONE, ". 0050", "1970-01-01 00:00:00+00:50"},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "+0159", "1970-01-01 00:00:00+01:59"},
      {".TZHTZM", TEST_FORMAT_TIMEZONE, ". 0159", "1970-01-01 00:00:00+01:59"},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "+1400", "1970-01-01 00:00:00+14:00"},
      {".TZHTZM", TEST_FORMAT_TIMEZONE, ". 1400", "1970-01-01 00:00:00+14:00"},
      {"TZHTZM", TEST_FORMAT_TIMEZONE, "+1401", EXPECT_ERROR},
      {".TZHTZM", TEST_FORMAT_TIMEZONE, ". 1401", EXPECT_ERROR},

      // Datetime/timestamp edge cases.
      {"YYYY-MM-DD HH24:MI:SS.FF6", TEST_FORMAT_DATE_AND_TIME,
       "0001-01-01 00:00:00.000000", "0001-01-01 00:00:00.000000"},
      {"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", TEST_FORMAT_TIMEZONE,
       "0000-12-31 16:08:00.000000-07:52", "0001-01-01 00:00:00.000000 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF6", TEST_FORMAT_DATE_AND_TIME,
       "9999-12-31 23:59:59.999999", "9999-12-31 23:59:59.999999"},
      {"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", TEST_FORMAT_TIMEZONE,
       "10000-01-01 06:29:59.999999+06:30", "9999-12-31 23:59:59.999999 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF6", TEST_FORMAT_DATE_AND_TIME,
       "0000-12-31 23:59:59.999999", EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", TEST_FORMAT_TIMEZONE,
       "0000-12-31 16:07:59.999999-07:52", EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF6", TEST_FORMAT_DATE_AND_TIME,
       "10000-01-01 00:00:00.000000", EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", TEST_FORMAT_TIMEZONE,
       "10000-01-01 07:30:00.000000+06:30", EXPECT_ERROR},

      {"YYYY-MM-DD HH24:MI:SS.FF9", TEST_FORMAT_DATE_AND_TIME,
       "0001-01-01 00:00:00.000000000", "0001-01-01 00:00:00.000000"},
      {"YYYY-MM-DD HH24:MI:SS.FF9TZH:TZM", TEST_FORMAT_TIMEZONE,
       "0000-12-31 16:08:00.000000000-07:52", "0001-01-01 00:00:00.000000 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF9", TEST_FORMAT_DATE_AND_TIME,
       "9999-12-31 23:59:59.999999999", "9999-12-31 23:59:59.999999",
       "9999-12-31 23:59:59.999999999"},
      {"YYYY-MM-DD HH24:MI:SS.FF9TZH:TZM", TEST_FORMAT_TIMEZONE,
       "10000-01-01 06:29:59.999999999+06:30", "9999-12-31 23:59:59.999999 UTC",
       "9999-12-31 23:59:59.999999999 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF9", TEST_FORMAT_DATE_AND_TIME,
       "0000-12-31 23:59:59.999999999", EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF9TZH:TZM", TEST_FORMAT_TIMEZONE,
       "0000-12-31 16:07:59.999999999-07:52", EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF9", TEST_FORMAT_DATE_AND_TIME,
       "10000-01-01 00:00:00.000000000", EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF9TZH:TZM", TEST_FORMAT_TIMEZONE,
       "10000-01-01 07:30:00.000000000+06:30", EXPECT_ERROR},

      // Test different cases of format elements, and they should all give the
      // same result.
      {"YYYY-MM-DD HH24:MI:SS.FF5", TEST_FORMAT_DATE_AND_TIME,
       "1234-02-03 11:22:33.12345", "1234-02-03 11:22:33.12345"},
      {"YyYY-Mm-dD hh24:mI:sS.fF5", TEST_FORMAT_DATE_AND_TIME,
       "1234-02-03 11:22:33.12345", "1234-02-03 11:22:33.12345"},
      {"YyyY-mm-Dd hH24:mi:Ss.ff5", TEST_FORMAT_DATE_AND_TIME,
       "1234-02-03 11:22:33.12345", "1234-02-03 11:22:33.12345"},
      {"yyYY-mM-dd hH24:Mi:Ss.Ff5", TEST_FORMAT_DATE_AND_TIME,
       "1234-02-03 11:22:33.12345", "1234-02-03 11:22:33.12345"},
      {"yyYY-mM-dd hH24:Mi:Ss.Ff5Tzh:tzM", TEST_FORMAT_TIMEZONE,
       "1234-02-03 11:22:33.12345 07:52", "1234-02-03 11:22:33.12345+07:52"},
      {"yyYY-mM-dd hH24:Mi:Ss.Ff5tzH:Tzm", TEST_FORMAT_TIMEZONE,
       "1234-02-03 11:22:33.12345 07:52", "1234-02-03 11:22:33.12345+07:52"},

      // Test cases about DigitCountRange of element. Please see details at
      // ComputeDigitCountRanges function in cast_date_time.cc.
      // There is no element that parses characters after "YYYY", so it can
      // consume up to 5 digits (note that element R"("")" consumes no
      // characters).
      {"YYYY", TEST_FORMAT_DATE, "01212", "1212-01-01"},
      {R"(YYYY"")", TEST_FORMAT_DATE, "1", "0001-01-01"},
      {R"(YYYY"""")", TEST_FORMAT_DATE, "12", "0012-01-01"},
      {R"(YYYY"""")", TEST_FORMAT_DATE, "123", "0123-01-01"},
      {R"(YYYY"""""")", TEST_FORMAT_DATE, "1234", "1234-01-01"},
      {R"(YYYY"")", TEST_FORMAT_DATE, "01234", "1234-01-01"},
      // The first character parsed by the elements after "YYYY" is not a digit,
      // so "YYYY" can parse up to 5 digits.
      {"YYYY-MM", TEST_FORMAT_DATE, "1-12", "0001-12-01"},
      {"YYYY,-MM", TEST_FORMAT_DATE, "12,-2", "0012-02-01"},
      {"YYYY   MM", TEST_FORMAT_DATE, "123\n12", "0123-12-01"},
      {R"(YYYY"":""MM)", TEST_FORMAT_DATE, "1234:2", "1234-02-01"},
      {R"(YYYY"""""";;MM)", TEST_FORMAT_DATE, "01234;;12", "1234-12-01"},
      {R"(YYYY""MON)", TEST_FORMAT_DATE, "00123apr", "0123-04-01"},
      {R"(YYYY""""MONTH)", TEST_FORMAT_DATE, "1octoBer", "0001-10-01"},
      {R"(YYYY""A.M.HH)", TEST_FORMAT_DATE_AND_TIME, "00123A.M.11",
       "0123-01-01 11:00:00"},
      {R"(YYYY""""P.M.HH)", TEST_FORMAT_DATE_AND_TIME, "1a.m.11",
       "0001-01-01 11:00:00"},
      {R"(YYYY""""TZH)", TEST_FORMAT_TIMEZONE, "123+1",
       "0123-01-01 00:00:00+01"},
      // The first character parsed by the elements after "YYYY" is a digit, so
      // "YYYY" parses exactly 4 digits from the input.
      {"YYYYMM", TEST_FORMAT_DATE, "01212", "0121-02-01"},
      {R"(YYYY""MM)", TEST_FORMAT_DATE, "01212", "0121-02-01"},
      {R"(YYYY""""""MM)", TEST_FORMAT_DATE, "01212", "0121-02-01"},
      {R"(YYYY""""""MM)", TEST_FORMAT_DATE, "0121", EXPECT_ERROR},
      // Similarly, "RR" parses exactly 2 digits with the first element that
      // consumes characters after it being "MM".
      {"RRMM", TEST_FORMAT_DATE, "111", date_2002_2_2, "2011-01-01"},
      {R"(RR""MM)", TEST_FORMAT_DATE, "111", date_2002_2_2, "2011-01-01"},
      {R"(RR""MM)", TEST_FORMAT_DATE, "11", date_2002_2_2, EXPECT_ERROR},
      {"MMYYYY", TEST_FORMAT_DATE, "02212", "0212-02-01"},
      {"MMYYYY", TEST_FORMAT_DATE, "02", EXPECT_ERROR},
      {"DDMM", TEST_FORMAT_DATE, "0212", "1970-12-02"},
      {"DDMM", TEST_FORMAT_DATE, "02", EXPECT_ERROR},
      {"A.M.HH12DD", TEST_FORMAT_DATE_AND_TIME, "a.m.1102",
       "1970-01-02 11:00:00"},
      {"A.M.HH12DD", TEST_FORMAT_DATE_AND_TIME, "a.m.11", EXPECT_ERROR},
      {"A.M.HHDD", TEST_FORMAT_DATE_AND_TIME, "a.m.1102",
       "1970-01-02 11:00:00"},
      {"A.M.HHDD", TEST_FORMAT_DATE_AND_TIME, "a.m.11", EXPECT_ERROR},
      {R"(MI""HHA.M.)", TEST_FORMAT_TIME, "111A.M.", "01:11:00"},
      {R"(MI""""""HHA.M.)", TEST_FORMAT_TIME, "11A.M.", EXPECT_ERROR},
      {R"(MI""HH12A.M.)", TEST_FORMAT_TIME, "111A.M.", "01:11:00"},
      {R"(MI""""""HH12A.M.)", TEST_FORMAT_TIME, "11A.M.", EXPECT_ERROR},
      {R"(HH24""MI)", TEST_FORMAT_TIME, "231", "23:01:00"},
      {"HH24MI", TEST_FORMAT_TIME, "23", EXPECT_ERROR},
      {R"(SS""HH24)", TEST_FORMAT_TIME, "231", "01:00:23"},
      {"SSHH24", TEST_FORMAT_TIME, "23", EXPECT_ERROR},
      {R"(SSSSS""MM)", TEST_FORMAT_DATE_AND_TIME, "036612",
       "1970-02-01 01:01:01"},
      {R"(SSSSS""""""MM)", TEST_FORMAT_DATE_AND_TIME, "03661", EXPECT_ERROR},
      {R"(FF4""SS)", TEST_FORMAT_DATE_AND_TIME, "01212",
       "1970-01-01 00:00:02.0121"},
      {R"(FF4""""""SS)", TEST_FORMAT_DATE_AND_TIME, "0121", EXPECT_ERROR},
      {R"(FF4""SSSSS)", TEST_FORMAT_DATE_AND_TIME, "01212",
       "1970-01-01 00:00:02.0121"},
      {R"(FF4""""""SSSSS)", TEST_FORMAT_DATE_AND_TIME, "0121", EXPECT_ERROR},
      {R"(TZM""FF4)", TEST_FORMAT_TIMEZONE, "121",
       "1970-01-01 00:00:00.1+00:12"},
      {R"(TZM""""""FF4)", TEST_FORMAT_TIMEZONE, "12", EXPECT_ERROR},
      {R"(TZH""TZM)", TEST_FORMAT_TIMEZONE, "+121",
       "1970-01-01 00:00:00+12:01"},
      {R"(TZH""""""TZM)", TEST_FORMAT_TIMEZONE, "+12", EXPECT_ERROR},

      // Whether the first character parsed by a "kDoubleQuotedLiteral" element
      // depends on the first characters inside quote.
      // The first character parsed by element R"("abc")" is not a digit since
      // the first character inside its quote ('a') is not a digit. Therefore,
      // the preceding element "YYYY" can parse up to 5 digits.
      {R"(YYYY"abc")", TEST_FORMAT_DATE, "1abc", "0001-01-01"},
      {R"(YYYY"abc")", TEST_FORMAT_DATE, "12abc", "0012-01-01"},
      {R"(YYYY"abc")", TEST_FORMAT_DATE, "012abc", "0012-01-01"},
      {R"(YYYY"abc")", TEST_FORMAT_DATE, "0012abc", "0012-01-01"},
      {R"(YYYY"abc")", TEST_FORMAT_DATE, "00012abc", "0012-01-01"},
      // The first character parsed by element R"("9abc")" is a digit since the
      // first character inside its quote ('9') is a digit. Therefore, the
      // preceding element "YYYY" parses exactly 4 digits.
      {R"(YYYY"9abc")", TEST_FORMAT_DATE, "00129abc", "0012-01-01"},
      {R"(YYYY"9abc")", TEST_FORMAT_DATE, "0129abc", EXPECT_ERROR},
      {R"(YYYY"9abc")", TEST_FORMAT_DATE, "129abc", EXPECT_ERROR},
      {R"(YYYY"9abc")", TEST_FORMAT_DATE, "000129abc", EXPECT_ERROR},
      // More complicated cases.
      {R"(YYYY"9abc"MM"""""def")", TEST_FORMAT_DATE, "00129abc3def",
       "0012-03-01"},
      {R"(YYYY"""abc"MM"""2def")", TEST_FORMAT_DATE, "12abc012def",
       "0012-01-01"},
      {R"(MM"""abc"YYYY"""2def")", TEST_FORMAT_DATE, "2abc20122def",
       "2012-02-01"},
      {"HH24MISSFF9", TEST_FORMAT_TIME, "123456789", "12:34:56.789"},

      // Error cases.
      // Returns error if input strings are not valid UTF-8.
      {"£\xff£", TEST_FORMAT_ANY, "£\xff£", EXPECT_ERROR},
      // Unrecognized format element.
      {"invalid_element", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      // Format elements not supported for parsing yet.
      {"IYYY", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"IYY", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"IY", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"I", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"SYYYY", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"YEAR", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"SYEAR", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"RM", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"D", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"DAY", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"DY", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"J", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"CC", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"SCC", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"Q", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"IW", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"WW", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"W", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"AD", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"BC", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"A.D.", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"B.C.", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"SP", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"TH", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"SPTH", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"THSP", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      {"FM", TEST_FORMAT_ANY, dummy_input_timestamp, EXPECT_ERROR},
      // Format strings are missing specific format elements or contain
      // format elements that cannot be present simultaneously.
      {"Mi MI", TEST_FORMAT_ANY, "50 50", EXPECT_ERROR},
      {"fF2 Ff2", TEST_FORMAT_ANY, "50 50", EXPECT_ERROR},
      {"YYYY RR", TEST_FORMAT_ANY, "1950 50", EXPECT_ERROR},
      {"Month MM", TEST_FORMAT_ANY, "FEBRUARY 11", EXPECT_ERROR},
      {"HH24 HHA.M.", TEST_FORMAT_ANY, "13 11A.M.", EXPECT_ERROR},
      {"HH24 hh12A.M.", TEST_FORMAT_ANY, "13 11A.M.", EXPECT_ERROR},
      {"hh", TEST_FORMAT_ANY, "11", EXPECT_ERROR},
      {"Hh12", TEST_FORMAT_ANY, "11", EXPECT_ERROR},
      {"A.M.", TEST_FORMAT_ANY, "A.M.", EXPECT_ERROR},
      {"P.M.", TEST_FORMAT_ANY, "A.M.", EXPECT_ERROR},
      {"SSSSS HH12A.M.", TEST_FORMAT_ANY, "3601 11A.M.", EXPECT_ERROR},
      {"SSSSS Mi", TEST_FORMAT_ANY, "3601 50", EXPECT_ERROR},
      {"Ss SSSSS", TEST_FORMAT_ANY, "50 3601", EXPECT_ERROR},
  });

  // Elements in "kHour" category.
  // "HH"/"HH12" (along with "A.M."/"P.M.") - hour value of a 12-hour clock.
  std::vector<absl::string_view> hour12_elements = {"HH", "HH12"};
  std::vector<absl::string_view> meridian_indicator_elements = {"A.M.", "P.M."};
  for (absl::string_view hour12_element : hour12_elements) {
    for (absl::string_view mi_element : meridian_indicator_elements) {
      std::string hour12_mi_format = absl::StrCat(hour12_element, mi_element);
      std::string mi_hour12_format = absl::StrCat(mi_element, hour12_element);

      tests.push_back({hour12_mi_format, TEST_FORMAT_TIME, "", EXPECT_ERROR});
      tests.push_back({mi_hour12_format, TEST_FORMAT_TIME, "", EXPECT_ERROR});

      tests.push_back({hour12_mi_format, TEST_FORMAT_TIME, "01", EXPECT_ERROR});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "A.M.", EXPECT_ERROR});

      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "0A.M.", EXPECT_ERROR});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "1A.M.", "01:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "01a.M.", "01:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "001a.m.", EXPECT_ERROR});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "0001a.m.", EXPECT_ERROR});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "1AM", EXPECT_ERROR});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "1pm", EXPECT_ERROR});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "1Zm", EXPECT_ERROR});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "A.m.1", "01:00:00"});

      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "2A.M.", "02:00:00"});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "a.M.2", "02:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "12A.m.", "00:00:00"});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "a.m.12", "00:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "13A.M.", EXPECT_ERROR});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "a.M.13", EXPECT_ERROR});

      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "0P.M.", EXPECT_ERROR});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "1p.M.", "13:00:00"});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "P.m.1", "13:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "2p.M.", "14:00:00"});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "P.M.2", "14:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "12P.M.", "12:00:00"});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "P.m.12", "12:00:00"});
      tests.push_back(
          {hour12_mi_format, TEST_FORMAT_TIME, "13P.M.", EXPECT_ERROR});
      tests.push_back(
          {mi_hour12_format, TEST_FORMAT_TIME, "P.M.13", EXPECT_ERROR});

      tests.push_back({hour12_mi_format, TEST_FORMAT_DATE_AND_TIME, "11a.m.",
                       date_2002_2_2, "2002-02-01 11:00:00"});
      tests.push_back({mi_hour12_format, TEST_FORMAT_DATE_AND_TIME, "P.M.2",
                       date_2002_2_2, "2002-02-01 14:00:00"});
    }
  }

  return tests;
}  // NOLINT(readability/fn_size)

// This struct is used to construct test cases to verify that a format element
// is not supported for certain output types (e.g. element "HH" is not supported
// for the output type DATE).
//
// See comments of TestFormatDateTimeParts for the exact meaning of each type.
//
// The <input_string> is naive so it would be simple to verify that it can be
// parsed successfully by if the <format_string> is supported by the output
// type.
struct CastStringToDateTimestampInvalidElementTest {
  std::string format_string;      // The format string specified.
  TestFormatDateTimeParts parts;  // The format parts needed to be supported.
  std::string input_string;       // The input date/time string to parse.
};

static std::vector<CastStringToDateTimestampInvalidElementTest>
GetCastStringToDateTimestampInvalidElementTests() {
  std::vector<CastStringToDateTimestampInvalidElementTest> tests({
      {"YYYY", TEST_FORMAT_DATE, "1111"},
      {"YYY", TEST_FORMAT_DATE, "111"},
      {"YY", TEST_FORMAT_DATE, "11"},
      {"Y", TEST_FORMAT_DATE, "1"},
      {"RRRR", TEST_FORMAT_DATE, "1111"},
      {"RR", TEST_FORMAT_DATE, "11"},
      {"Y,YYY", TEST_FORMAT_DATE, "1,111"},
      {"MM", TEST_FORMAT_DATE, "11"},
      {"MON", TEST_FORMAT_DATE, "JAN"},
      {"MONTH", TEST_FORMAT_DATE, "JANUARY"},
      {"DD", TEST_FORMAT_DATE, "11"},
      {"HHA.M.", TEST_FORMAT_TIME, "11A.M."},
      {"HHP.M.", TEST_FORMAT_TIME, "11A.M."},
      {"HH12A.M.", TEST_FORMAT_TIME, "11A.M."},
      {"HH12P.M.", TEST_FORMAT_TIME, "11A.M."},
      {"HH24", TEST_FORMAT_TIME, "11"},
      {"MI", TEST_FORMAT_TIME, "11"},
      {"SS", TEST_FORMAT_TIME, "11"},
      {"SSSSS", TEST_FORMAT_TIME, "11111"},
      {"FF1", TEST_FORMAT_TIME, "1"},
      {"FF2", TEST_FORMAT_TIME, "11"},
      {"FF3", TEST_FORMAT_TIME, "111"},
      {"FF4", TEST_FORMAT_TIME, "1111"},
      {"FF5", TEST_FORMAT_TIME, "11111"},
      {"FF6", TEST_FORMAT_TIME, "111111"},
      {"FF7", TEST_FORMAT_TIME, "1111111"},
      {"FF8", TEST_FORMAT_TIME, "11111111"},
      {"FF9", TEST_FORMAT_TIME, "111111111"},
      {"TZH", TEST_FORMAT_TIMEZONE, "+11"},
      {"TZM", TEST_FORMAT_TIMEZONE, "11"},
  });

  return tests;
}

class CastStringToTimestampTest : public ParseTimestampTest {
 public:
  CastStringToTimestampTest(absl::string_view format,
                            absl::string_view timestamp_string,
                            absl::string_view default_time_zone,
                            absl::Time current_timestamp,
                            absl::string_view expected_result)
      : ParseTimestampTest(format, timestamp_string, default_time_zone,
                           expected_result),
        current_timestamp_(current_timestamp) {}

  CastStringToTimestampTest(absl::string_view format,
                            absl::string_view timestamp_string,
                            absl::string_view default_time_zone,
                            absl::Time current_timestamp,
                            absl::string_view expected_result,
                            absl::string_view nano_expected_result)
      : ParseTimestampTest(format, timestamp_string, default_time_zone,
                           expected_result, nano_expected_result),
        current_timestamp_(current_timestamp) {}

  CastStringToTimestampTest(absl::string_view format,
                            absl::string_view timestamp_string,
                            absl::string_view default_time_zone,
                            absl::Time current_timestamp,
                            absl::string_view expected_result,
                            absl::string_view nano_expected_result,
                            absl::string_view pico_expected_result)
      : ParseTimestampTest(format, timestamp_string, default_time_zone,
                           expected_result, nano_expected_result,
                           pico_expected_result),
        current_timestamp_(current_timestamp) {}

  ~CastStringToTimestampTest() {}

  absl::Time current_timestamp() const { return current_timestamp_; }

 private:
  // Current date value to reflect current year and month.
  absl::Time current_timestamp_;
};

static std::vector<CastStringToTimestampTest>
GetCastStringToTimestampCommonTests() {
  std::vector<CastStringToTimestampTest> tests;

  // Add common test cases.
  for (const CastStringToDateTimestampCommonTest& common_test :
       GetCastStringToDateTimestampCommonTests()) {
    absl::Time current_timestamp;
    GOOGLESQL_CHECK_OK(functions::ConvertDateToTimestamp(common_test.current_date, "UTC",
                                               &current_timestamp));
    if (common_test.parts == TEST_FORMAT_TIME) {
      std::string micro_result =
          common_test.expected_result.empty()
              ? ""
              : absl::StrCat("1970-01-01 ", common_test.expected_result, "+00");
      std::string nano_result =
          common_test.nano_expected_result.empty()
              ? ""
              : absl::StrCat("1970-01-01 ", common_test.nano_expected_result,
                             "+00");
      std::string pico_result =
          common_test.pico_expected_result.empty()
              ? ""
              : absl::StrCat("1970-01-01 ", common_test.pico_expected_result,
                             "+00");
      tests.push_back({common_test.format_string, common_test.input_string,
                       "UTC", current_timestamp, micro_result, nano_result,
                       pico_result});
    } else {
      tests.push_back({
          common_test.format_string,
          common_test.input_string,
          "UTC",
          current_timestamp,
          common_test.expected_result,
          common_test.nano_expected_result,
          common_test.pico_expected_result,
      });
    }
  }

  return tests;
}

static std::vector<CastStringToTimestampTest> GetCastStringToTimestampTests() {
  absl::Time ts_1970_1_1_utc;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      "1970-01-01", absl::UTCTimeZone(), kNanoseconds, true, &ts_1970_1_1_utc));
  int32_t date_2002_1_1;
  GOOGLESQL_CHECK_OK(functions::ConstructDate(2002, 1, 1, &date_2002_1_1));
  std::vector<CastStringToTimestampTest> tests({
      // Test cases under <default_time_zone> different from "UTC". The current
      // timestamp "1970-1-1 UTC" is "1969-12-31 -01:00" under timezone
      // "-01:00", so the default year and default month become 1969 and 12 for
      // the test cases with "-01:00" as <default_time_zone> string (note
      // default day value is always 1).
      {"YYYY", "1234", "-01:00", ts_1970_1_1_utc, "1234-12-01 00:00:00-01:00"},
      {"YYY", "123", "-01:00", ts_1970_1_1_utc, "1123-12-01 00:00:00-01:00"},
      {"YY", "12", "-01:00", ts_1970_1_1_utc, "1912-12-01 00:00:00-01:00"},
      {"Y", "1", "-01:00", ts_1970_1_1_utc, "1961-12-01 00:00:00-01:00"},
      {"RRRR", "1234", "-01:00", ts_1970_1_1_utc, "1234-12-01 00:00:00-01:00"},
      {"RR", "12", "-01:00", ts_1970_1_1_utc, "2012-12-01 00:00:00-01:00"},
      {"Y,YYY", "1,234", "-01:00", ts_1970_1_1_utc,
       "1234-12-01 00:00:00-01:00"},
      {"MM", "03", "-01:00", ts_1970_1_1_utc, "1969-03-01 00:00:00-01:00"},
      {"MON", "MAR", "-01:00", ts_1970_1_1_utc, "1969-03-01 00:00:00-01:00"},
      {"MONTH", "June", "-01:00", ts_1970_1_1_utc, "1969-06-01 00:00:00-01:00"},
      {"YYMM", "1202", "-01:00", ts_1970_1_1_utc, "1912-02-01 00:00:00-01:00"},
      {"DD", "02", "-01:00", ts_1970_1_1_utc, "1969-12-02 00:00:00-01:00"},
      {"HHA.M.", "02a.m.", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 02:00:00-01:00"},
      {"HHP.M.", "02a.m.", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 02:00:00-01:00"},
      {"HH12A.M.", "02a.m.", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 02:00:00-01:00"},
      {"HH12P.M.", "02a.m.", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 02:00:00-01:00"},
      {"HH24", "6", "-01:00", ts_1970_1_1_utc, "1969-12-01 06:00:00-01:00"},
      {"MI", "24", "-01:00", ts_1970_1_1_utc, "1969-12-01 00:24:00-01:00"},
      {"SS", "24", "-01:00", ts_1970_1_1_utc, "1969-12-01 00:00:24-01:00"},
      {"SSSSS", "24", "-01:00", ts_1970_1_1_utc, "1969-12-01 00:00:24-01:00"},
      {"FF3", "234", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 00:00:00.234-01:00"},
      {"FF7", "2345678", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 00:00:00.234567-01:00", "1969-12-01 00:00:00.2345678-01:00"},
      {"HH24MISS", "062411", "-01:00", ts_1970_1_1_utc,
       "1969-12-01 06:24:11-01:00"},
      {"TZH", "+01", "-01:59", ts_1970_1_1_utc, "1969-12-01 00:00:00+01:00"},
      {"TZM", "13", "-01:59", ts_1970_1_1_utc, "1969-12-01 00:00:00+00:13"},
      // The offset of "Pacific/Honolulu" time zone is always -10:00 and not
      // affected by Daylight Saving Time.
      {"TZH", "+02", "Pacific/Honolulu", ts_1970_1_1_utc,
       "1969-12-01 00:00:00+02:00"},
      {"", "", "Pacific/Honolulu", ts_1970_1_1_utc,
       "1969-12-01 00:00:00-10:00"},
      // Boundary cases with non-UTC <default_time_zone>.
      {"YYYY-MM-DD HH24:MI:SS.FF6", "0000-12-31 16:08:00.000000", "-07:52",
       ts_1970_1_1_utc, "0001-01-01 00:00:00.000000 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF6", "0000-12-31 16:07:59.999999", "-07:52",
       ts_1970_1_1_utc, EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF6", "10000-01-01 06:29:59.999999", "+06:30",
       ts_1970_1_1_utc, "9999-12-31 23:59:59.999999 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF6", "10000-01-01 06:30:00.000000", "+06:30",
       ts_1970_1_1_utc, EXPECT_ERROR},

      {"YYYY-MM-DD HH24:MI:SS.FF9", "0000-12-31 16:08:00.000000000", "-07:52",
       ts_1970_1_1_utc, "0001-01-01 00:00:00.000000 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF9", "0000-12-31 16:07:59.999999999", "-07:52",
       ts_1970_1_1_utc, EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS.FF9", "10000-01-01 06:29:59.999999999", "+06:30",
       ts_1970_1_1_utc, "9999-12-31 23:59:59.999999 UTC",
       "9999-12-31 23:59:59.999999999 UTC"},
      {"YYYY-MM-DD HH24:MI:SS.FF9", "10000-01-01 06:30:00.000000000", "+06:30",
       ts_1970_1_1_utc, EXPECT_ERROR},
      // 'YYYY'/'RRRR'/'Y,YYY' format elements can match integer values of 0 and
      // 10000 by changing <default_time_zone> values.
      {"YYYY-MM-DD", "10000-1-1", "+1:00", ts_1970_1_1_utc,
       "9999-12-31 23:00:00 UTC"},
      {"RRRR-MM-DD", "10000-1-1", "+1:00", ts_1970_1_1_utc,
       "9999-12-31 23:00:00 UTC"},
      {"Y,YYY-MM-DD", "10,000-1-1", "+1:00", ts_1970_1_1_utc,
       "9999-12-31 23:00:00 UTC"},
      {"YYYY-MM-DD HH24", "0000-12-31 23", "-1:00", ts_1970_1_1_utc,
       "0001-1-1 00:00:00 UTC"},
      {"RRRR-MM-DD HH24", "0000-12-31 23", "-1:00", ts_1970_1_1_utc,
       "0001-1-1 00:00:00 UTC"},
      {"Y,YYY-MM-DD HH24", "0,000-12-31 23", "-1:00", ts_1970_1_1_utc,
       "0001-1-1 00:00:00 UTC"},
      // The time zone offset yields timestamps outside the valid range.
      {"YYYY-MM-DD HH24:MI:SS", "9999-12-31 23:00:00", "-1:00", ts_1970_1_1_utc,
       EXPECT_ERROR},
      {"YYYY-MM-DD HH24:MI:SS", "0001-01-01 00:00:00", "+1:00", ts_1970_1_1_utc,
       EXPECT_ERROR},
      // Invalid default time zones.
      {"", "", "£\xff£", ts_1970_1_1_utc, EXPECT_ERROR},
      {"", "", "", ts_1970_1_1_utc, EXPECT_ERROR},
      {"", "", "random_timezone", ts_1970_1_1_utc, EXPECT_ERROR},
  });

  // Add common tests.
  for (const CastStringToTimestampTest& test :
       GetCastStringToTimestampCommonTests()) {
    tests.push_back(test);
  }

  return tests;
}

struct CastStringToDateTest : ParseDateTest {
  int32_t current_date;

  CastStringToDateTest(absl::string_view format_string_in,
                       absl::string_view date_string_in,
                       int32_t current_date_in,
                       absl::string_view expected_result_in)
      : ParseDateTest{std::string(format_string_in),
                      std::string(date_string_in),
                      std::string(expected_result_in)},
        current_date(current_date_in) {}
};

static std::vector<CastStringToDateTest> GetCastStringToDateTests() {
  std::vector<CastStringToDateTest> tests;
  // Add common test cases.
  for (const CastStringToDateTimestampCommonTest& common_test :
       GetCastStringToDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_DATE ||
        common_test.parts == TEST_FORMAT_ANY) {
      tests.push_back({common_test.format_string, common_test.input_string,
                       common_test.current_date, common_test.expected_result});
    }
  }

  // Add test cases for invalid elements when casting to Date type.
  int32_t date_1970_1_1;
  GOOGLESQL_CHECK_OK(functions::ConstructDate(1970, 1, 1, &date_1970_1_1));
  for (const CastStringToDateTimestampInvalidElementTest& test :
       GetCastStringToDateTimestampInvalidElementTests()) {
    if (test.parts == TEST_FORMAT_TIME || test.parts == TEST_FORMAT_TIMEZONE) {
      tests.push_back(
          {test.format_string, test.input_string, date_1970_1_1, EXPECT_ERROR});
    }
  }
  return tests;
}

#undef EXPECT_ERROR

static std::vector<FunctionTestCall> GetFunctionTestsCastStringToTime() {
  std::vector<CivilTimeTestCase> test_cases;
  // Add common test cases.
  for (const CastStringToDateTimestampCommonTest& common_test :
       GetCastStringToDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_TIME ||
        common_test.parts == TEST_FORMAT_ANY) {
      auto ConvertTimeStringToTime = [](absl::string_view time_string,
                                        functions::TimestampScale scale) {
        if (IsExpectedError(time_string)) {
          return absl::StatusOr<Value>(FunctionEvalError());
        }
        TimeValue time;
        GOOGLESQL_CHECK_OK(functions::ConvertStringToTime(time_string, scale, &time));
        ABSL_CHECK(time.IsValid());
        return absl::StatusOr<Value>(Value::Time(time));
      };
      std::string micro_result = common_test.expected_result;
      std::string nano_result = common_test.nano_expected_result;
      // When common_test.parts == TEST_FORMAT_ANY, we only test format elements
      // in "kLiteral" category, so the result is default value of Time if not
      // error.
      if (common_test.parts == TEST_FORMAT_ANY && !micro_result.empty()) {
        micro_result = "00:00:00";
      }

      if (common_test.parts == TEST_FORMAT_ANY && !nano_result.empty()) {
        nano_result = "00:00:00";
      }
      test_cases.push_back(
          {{String(common_test.format_string),
            String(common_test.input_string)},
           ConvertTimeStringToTime(micro_result, kMicroseconds),
           ConvertTimeStringToTime(nano_result, kNanoseconds),
           TimeType()});
    }
  }

  // Add test cases for invalid elements when casting to Time type.
  for (const CastStringToDateTimestampInvalidElementTest& test :
       GetCastStringToDateTimestampInvalidElementTests()) {
    if (test.parts == TEST_FORMAT_DATE || test.parts == TEST_FORMAT_TIMEZONE) {
      test_cases.push_back(
          {{String(test.format_string), String(test.input_string)},
           FunctionEvalError(),
           TimeType()});
    }
  }

  // Null arguments handling.
  test_cases.push_back({{NullString(), String("")}, NullTime()});
  test_cases.push_back({{String(""), NullString()}, NullTime()});
  test_cases.push_back({{NullString(), NullString()}, NullTime()});

  // Construct a vector of FunctionTestCall by wrapping the test cases with
  // necessary features such as FEATURE_CIVIL_TIME.
  return PrepareCivilTimeTestCaseAsFunctionTestCall(test_cases,
                                                    "cast_string_to_time");
}

static std::vector<FunctionTestCall> GetFunctionTestsCastStringToDatetime() {
  int32_t date_1970_1_1;
  GOOGLESQL_CHECK_OK(functions::ConstructDate(1970, 1, 1, &date_1970_1_1));
  std::vector<CivilTimeTestCase> test_cases;
  // Add common test cases.
  for (const CastStringToDateTimestampCommonTest& common_test :
       GetCastStringToDateTimestampCommonTests()) {
    if (common_test.parts == TEST_FORMAT_TIMEZONE) {
      continue;
    }
    auto ConvertDatetimeStringToDateTime =
        [](absl::string_view datetime_string, functions::TimestampScale scale) {
          if (IsExpectedError(datetime_string)) {
            return absl::StatusOr<Value>(FunctionEvalError());
          }
          DatetimeValue datetime;
          GOOGLESQL_CHECK_OK(functions::ConvertStringToDatetime(datetime_string, scale,
                                                      &datetime));
          ABSL_CHECK(datetime.IsValid());
          return absl::StatusOr<Value>(Value::Datetime(datetime));
        };

    std::string micro_result = common_test.expected_result;
    std::string nano_result = common_test.nano_expected_result;
    if (common_test.parts == TEST_FORMAT_TIME && !micro_result.empty()) {
      micro_result = absl::StrCat("1970-01-01 ", micro_result);
    }
    if (common_test.parts == TEST_FORMAT_TIME && !nano_result.empty()) {
      nano_result = absl::StrCat("1970-01-01 ", nano_result);
    }

    test_cases.push_back(
        {{String(common_test.format_string), String(common_test.input_string),
          Int32(common_test.current_date)},
         ConvertDatetimeStringToDateTime(micro_result, kMicroseconds),
         ConvertDatetimeStringToDateTime(nano_result, kNanoseconds),
         DatetimeType()});
  }

  // Add test cases for invalid elements when casting to Datetime type.
  for (const CastStringToDateTimestampInvalidElementTest& test :
       GetCastStringToDateTimestampInvalidElementTests()) {
    if (test.parts == TEST_FORMAT_TIMEZONE) {
      test_cases.push_back({{String(test.format_string),
                             String(test.input_string), Int32(date_1970_1_1)},
                            FunctionEvalError(),
                            DatetimeType()});
    }
  }

  // Null arguments handling.
  test_cases.push_back(
      {{NullString(), String(""), Int32(date_1970_1_1)}, NullDatetime()});
  test_cases.push_back(
      {{String(""), NullString(), Int32(date_1970_1_1)}, NullDatetime()});
  test_cases.push_back({{String(""), String(""), NullInt32()}, NullDatetime()});
  test_cases.push_back(
      {{NullString(), NullString(), NullInt32()}, NullDatetime()});

  // Construct a vector of FunctionTestCall by wrapping the test cases with
  // necessary features such as FEATURE_CIVIL_TIME.
  return PrepareCivilTimeTestCaseAsFunctionTestCall(test_cases,
                                                    "cast_string_to_datetime");
}

std::vector<FunctionTestCall> GetFunctionTestsCastStringToDateTimestamp() {
  std::vector<FunctionTestCall> tests;

  // Adds CAST_STRING_TO_TIMESTAMP() tests.
  for (const CastStringToTimestampTest& test :
       GetCastStringToTimestampTests()) {
    std::vector<ValueConstructor> args = {
        String(test.format()), String(test.timestamp_string()),
        String(test.default_time_zone()), Timestamp(test.current_timestamp())};
    AddTestCasesForParseTimestampTest(test, "cast_string_to_timestamp", args,
                                      tests);
  }

  // Null arguments handling.
  absl::Time ts_1970_1_1_utc;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      "1970-01-01", absl::UTCTimeZone(), kNanoseconds, true, &ts_1970_1_1_utc));
  tests.push_back(
      {"cast_string_to_timestamp",
       {NullString(), String(""), String("UTC"), Timestamp(ts_1970_1_1_utc)},
       NullTimestamp()});
  tests.push_back(
      {"cast_string_to_timestamp",
       {String(""), NullString(), String("UTC"), Timestamp(ts_1970_1_1_utc)},
       NullTimestamp()});
  tests.push_back(
      {"cast_string_to_timestamp",
       {String(""), String(""), NullString(), Timestamp(ts_1970_1_1_utc)},
       NullTimestamp()});
  tests.push_back({"cast_string_to_timestamp",
                   {String(""), String(""), String("UTC"), NullTimestamp()},
                   NullTimestamp()});
  tests.push_back({"cast_string_to_timestamp",
                   {NullString(), NullString(), NullString(), NullTimestamp()},
                   NullTimestamp()});

  // Adds CAST_STRING_TO_DATE() tests.
  for (const CastStringToDateTest& test : GetCastStringToDateTests()) {
    if (test.expected_result.empty()) {
      tests.push_back({"cast_string_to_date",
                       {String(test.format_string), String(test.date_string),
                        Int32(test.current_date)},
                       NullDate(),
                       OUT_OF_RANGE});
    } else {
      tests.push_back({"cast_string_to_date",
                       {String(test.format_string), String(test.date_string),
                        Int32(test.current_date)},
                       DateFromStr(test.expected_result)});
    }
  }

  // Null arguments handling.
  int32_t date_1970_1_1;
  GOOGLESQL_CHECK_OK(functions::ConstructDate(1970, 1, 1, &date_1970_1_1));
  tests.push_back({"cast_string_to_date",
                   {NullString(), String(""), Int32(date_1970_1_1)},
                   NullDate()});
  tests.push_back({"cast_string_to_date",
                   {String(""), NullString(), Int32(date_1970_1_1)},
                   NullDate()});
  tests.push_back({"cast_string_to_date",
                   {String(""), String(""), NullInt32()},
                   NullDate()});
  tests.push_back({"cast_string_to_date",
                   {NullString(), NullString(), NullInt32()},
                   NullDate()});

  // Adds CAST_STRING_TO_TIME() tests.
  for (const FunctionTestCall& test : GetFunctionTestsCastStringToTime()) {
    tests.push_back(test);
  }

  // Adds CAST_STRING_TO_DATETIME() tests.
  for (const FunctionTestCall& test : GetFunctionTestsCastStringToDatetime()) {
    tests.push_back(test);
  }

  return tests;
}

// Given the input date as year-month-day, add 3 tests to extract the date part
// from DATE, TIMESTAMP and DATETIME constructed from that input date.
void AddExtractTestsWithDate(
    int year, int month, int day, const Value& part_enum_value,
    const Value& expected_result, std::vector<FunctionTestCall>* tests,
    std::vector<CivilTimeTestCase>* civil_time_test_cases) {
  const std::string date_string =
      absl::StrFormat("%04d-%02d-%02d", year, month, day);
  Value date = DateFromStr(date_string);
  tests->push_back({"extract", {date, part_enum_value}, expected_result});
  Value ts = TimestampFromStr(date_string);
  tests->push_back({"extract", {ts, part_enum_value, "UTC"}, expected_result});
  Value datetime = DatetimeMicros(year, month, day, 0, 0, 0, 0);
  civil_time_test_cases->push_back(
      CivilTimeTestCase({{datetime, part_enum_value}}, expected_result));
}

// Formats the given date and returns the result as an Int64 Value.
Value DateFormatToInt64(absl::string_view date_format, int32_t date) {
  std::string output_string;
  GOOGLESQL_CHECK_OK(functions::FormatDateToString(date_format, date, &output_string));
  int64_t result;
  ABSL_CHECK(absl::SimpleAtoi(output_string, &result)) << output_string;
  return Int64(result);
}

// Invokes AddExtractTestsWithDate for the given date with ISOYEAR, ISOWEEK, and
// WEEK.
void AddIsoYearWeekExtractTestsWithDate(
    int year, int month, int day, const EnumType* part_enum,
    std::vector<FunctionTestCall>* tests,
    std::vector<CivilTimeTestCase>* civil_time_test_cases) {
  // The easiest way to get the week, ISO year and ISO week is through the
  // formatting functions.
  int32_t date;
  GOOGLESQL_CHECK_OK(functions::ConstructDate(year, month, day, &date));
  AddExtractTestsWithDate(year, month, day, Enum(part_enum, "ISOYEAR"),
                          DateFormatToInt64("%G", date), tests,
                          civil_time_test_cases);
  AddExtractTestsWithDate(year, month, day, Enum(part_enum, "ISOWEEK"),
                          DateFormatToInt64("%V", date), tests,
                          civil_time_test_cases);
  AddExtractTestsWithDate(year, month, day, Enum(part_enum, "WEEK"),
                          DateFormatToInt64("%U", date), tests,
                          civil_time_test_cases);
}

// Returns the feature set required for the date part (either empty or
// {FEATURE_WEEK_WITH_WEEKDAY}.
static QueryParamsWithResult::FeatureSet GetCustomWeekRequiredFeatures(
    int date_part) {
  switch (date_part) {
    case WEEK:
    case ISOWEEK:
      return {};
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY:
      return {FEATURE_WEEK_WITH_WEEKDAY};
    default:
      ABSL_LOG(FATAL) << "Unexpected date part: " << date_part;
  }
}

// 1==Sun, ..., 7=Sat
static int DayOfWeekIntegerSunToSat1To7(absl::Weekday weekday) {
  switch (weekday) {
    case absl::Weekday::sunday:
      return 1;
    case absl::Weekday::monday:
      return 2;
    case absl::Weekday::tuesday:
      return 3;
    case absl::Weekday::wednesday:
      return 4;
    case absl::Weekday::thursday:
      return 5;
    case absl::Weekday::friday:
      return 6;
    case absl::Weekday::saturday:
      return 7;
  }
}

std::vector<FunctionTestCall> GetFunctionTestsExtractFrom() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  std::vector<FunctionTestCall> tests;
  std::vector<CivilTimeTestCase> civil_time_test_cases;

  // Test NULL handling for supported date parts at >= DAY granularity.
  for (absl::string_view date_part :
       {"YEAR", "QUARTER", "MONTH", "DAY", "DAYOFYEAR", "DAYOFWEEK", "WEEK",
        "ISOYEAR", "ISOWEEK"}) {
    tests.push_back(
        {"extract", {NullDate(), Enum(part_enum, date_part)}, NullInt64()});
    tests.push_back({"extract",
                     {NullTimestamp(), Enum(part_enum, date_part)},
                     NullInt64()});
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{NullDatetime(), Enum(part_enum, date_part)}}, NullInt64()));
  }
  for (absl::string_view date_part :
       {"WEEK_MONDAY", "WEEK_TUESDAY", "WEEK_WEDNESDAY", "WEEK_THURSDAY",
        "WEEK_FRIDAY", "WEEK_SATURDAY"}) {
    const Value part_enum_value = Enum(part_enum, date_part);
    QueryParamsWithResult::FeatureSet required_feature_set =
        GetCustomWeekRequiredFeatures(part_enum_value.enum_value());

    tests.push_back(
        {"extract",
         QueryParamsWithResult({NullDate(), part_enum_value}, NullInt64())
             .AddRequiredFeatures(required_feature_set)});
    tests.push_back(
        {"extract",
         QueryParamsWithResult({NullTimestamp(), part_enum_value}, NullInt64())
             .AddRequiredFeatures(required_feature_set)});
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{NullDatetime(), Enum(part_enum, date_part)}}, NullInt64(),
        /*output_type=*/nullptr, required_feature_set));
  }

  // Test NULL handling for supported date parts at < DAY granularity.
  for (absl::string_view date_part :
       {"HOUR", "MINUTE", "SECOND", "MILLISECOND", "MICROSECOND"}) {
    tests.push_back({"extract",
                     {NullTimestamp(), Enum(part_enum, date_part)},
                     NullInt64()});
    tests.push_back({"extract",
                     {NullTimestamp(), Enum(part_enum, date_part), "UTC"},
                     NullInt64()});
    tests.push_back({"extract",
                     {TimestampFromStr("2000-01-01 00:00:00"),
                      Enum(part_enum, date_part), NullString()},
                     NullInt64()});
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{NullTime(), Enum(part_enum, date_part)}}, NullInt64()));
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{NullDatetime(), Enum(part_enum, date_part)}}, NullInt64()));
  }

  tests.push_back(
      {"extract", {NullTimestamp(), Enum(part_enum, "DATE")}, NullDate()});
  tests.push_back({"extract",
                   {NullTimestamp(), Enum(part_enum, "DATE"), "UTC"},
                   NullDate()});
  tests.push_back({"extract",
                   {TimestampFromStr("2000-01-01 00:00:00"),
                    Enum(part_enum, "DATE"), NullString()},
                   NullDate()});
  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{NullDatetime(), Enum(part_enum, "TIME")}}, NullTime()));

  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{NullTimestamp(), Enum(part_enum, "TIME")}}, NullTime()));
  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{NullTimestamp(), Enum(part_enum, "TIME"), "UTC"}}, NullTime()));
  civil_time_test_cases.push_back(
      CivilTimeTestCase({{TimestampFromStr("2000-01-01 00:00:00"),
                          Enum(part_enum, "TIME"), NullString()}},
                        NullTime()));

  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{NullTimestamp(), Enum(part_enum, "DATETIME")}}, NullDatetime()));
  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{NullTimestamp(), Enum(part_enum, "DATETIME"), "UTC"}}, NullDatetime()));
  civil_time_test_cases.push_back(
      CivilTimeTestCase({{TimestampFromStr("2000-01-01 00:00:00"),
                          Enum(part_enum, "DATETIME"), NullString()}},
                        NullDatetime()));

  // DAYOFYEAR, YEAR, ISOYEAR, MONTH, WEEK, ISOWEEK, DAY, DAYOFYEAR
  for (int year : {   1, 9999,  100,  800, 1600, 1700,
                   1900, 1945, 1947, 1961, 1969, 1970,
                   1985, 1987, 1990, 1993, 1998, 1999,
                   2000, 2010, 2011, 2012, 2013, 2014,
                   2015, 2016, 2020, 2100, 2400, 2500}) {
    // Leap year is every 4 years, but not every 100 years, but every 400 years
    const bool is_leap =
        (year % 400 == 0) || ((year % 100 != 0) && (year % 4 == 0));
    AddExtractTestsWithDate(year, 1, 1, Enum(part_enum, "DAYOFYEAR"), Int64(1),
                            &tests, &civil_time_test_cases);
    if (is_leap) {
      AddExtractTestsWithDate(year, 2, 29, Enum(part_enum, "YEAR"), Int64(year),
                              &tests, &civil_time_test_cases);
      AddExtractTestsWithDate(year, 2, 29, Enum(part_enum, "MONTH"), Int64(2),
                              &tests, &civil_time_test_cases);
      AddExtractTestsWithDate(year, 2, 29, Enum(part_enum, "DAY"), Int64(29),
                              &tests, &civil_time_test_cases);
    }
    AddExtractTestsWithDate(year, 3, 1, Enum(part_enum, "DAYOFYEAR"),
                            Int64(is_leap ? 31 + 29 + 1 : 31 + 28 + 1), &tests,
                            &civil_time_test_cases);
    AddExtractTestsWithDate(year, 12, 31, Enum(part_enum, "YEAR"), Int64(year),
                            &tests, &civil_time_test_cases);
    AddExtractTestsWithDate(year, 12, 31, Enum(part_enum, "DAYOFYEAR"),
                            Int64(is_leap ? 366 : 365), &tests,
                            &civil_time_test_cases);
    // The most interesting week, ISO year and ISO week cases are around the end
    // of December and start of January.
    AddIsoYearWeekExtractTestsWithDate(year, 1, 1, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 1, 2, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 1, 3, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 1, 4, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 1, 5, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 1, 15, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 3, 4, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 5, 31, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 12, 27, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 12, 28, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 12, 29, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 12, 30, part_enum, &tests,
                                       &civil_time_test_cases);
    AddIsoYearWeekExtractTestsWithDate(year, 12, 31, part_enum, &tests,
                                       &civil_time_test_cases);
  }

  // Extracting YEAR, QUARTER, MONTH, DAY, DAYOFWEEK from DATE, TIMESTAMP and
  // DATETIME, and also extracting DATETIME for TIMESTAMP.
  absl::CivilDay civil_day(1999, 8, 7);
  for (int i = 0; i < 366; i++) {
    Value date = DateFromStr(::absl::FormatCivilTime(civil_day));
    tests.push_back(
        {"extract", {date, Enum(part_enum, "YEAR")}, Int64(civil_day.year())});
    tests.push_back({"extract",
                     {date, Enum(part_enum, "QUARTER")},
                     Int64((civil_day.month() - 1) / 3 + 1)});
    tests.push_back({"extract",
                     {date, Enum(part_enum, "MONTH")},
                     Int64(civil_day.month())});
    tests.push_back(
        {"extract", {date, Enum(part_enum, "DAY")}, Int64(civil_day.day())});
    int weekday = DayOfWeekIntegerSunToSat1To7(absl::GetWeekday(civil_day));
    tests.push_back({"extract",
                     {date, Enum(part_enum, "DAYOFWEEK")},
                     Int64(weekday)});

    Value ts = TimestampFromStr(::absl::FormatCivilTime(civil_day));
    tests.push_back({"extract",
                     {ts, Enum(part_enum, "YEAR"), "UTC"},
                     Int64(civil_day.year())});
    tests.push_back({"extract",
                     {ts, Enum(part_enum, "QUARTER"), "UTC"},
                     Int64((civil_day.month() - 1) / 3 + 1)});
    tests.push_back({"extract",
                     {ts, Enum(part_enum, "MONTH"), "UTC"},
                     Int64(civil_day.month())});
    tests.push_back({"extract",
                     {ts, Enum(part_enum, "DAY"), "UTC"},
                     Int64(civil_day.day())});
    // CivilDay starts week with Monday, GoogleSQL starts week with Sunday.
    tests.push_back({"extract",
                     {ts, Enum(part_enum, "DAYOFWEEK"), "UTC"},
                     Int64(weekday)});

    Value datetime = DatetimeMicros(static_cast<int>(civil_day.year()),
                                    civil_day.month(),
                                    civil_day.day(), 0, 0, 0, 0);
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{datetime, Enum(part_enum, "YEAR")}}, Int64(civil_day.year())));
    civil_time_test_cases.push_back(
        CivilTimeTestCase({{datetime, Enum(part_enum, "QUARTER")}},
                          Int64((civil_day.month() - 1) / 3 + 1)));
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{datetime, Enum(part_enum, "MONTH")}}, Int64(civil_day.month())));
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{datetime, Enum(part_enum, "DAY")}}, Int64(civil_day.day())));
    // WeekdayToMon1Sun7 starts week with Monday, GoogleSQL starts week with
    // Sunday.
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{datetime, Enum(part_enum, "DAYOFWEEK")}},
                     Int64(weekday)));

    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{ts, Enum(part_enum, "DATETIME"), "UTC"}}, datetime));

    ++civil_day;
  }

  // Tests for WEEK_*, including some additional tests for WEEK.
  //
  // For each weekday, we test with a year that starts with that weekday (1 =
  // Monday, ..., 7 = Sunday).
  const std::map<absl::Weekday, int> weekday_to_year = {
      {absl::Weekday::monday, 2001},    {absl::Weekday::tuesday, 2002},
      {absl::Weekday::wednesday, 2003}, {absl::Weekday::thursday, 2004},
      {absl::Weekday::friday, 2010},    {absl::Weekday::saturday, 2005},
      {absl::Weekday::sunday, 2006}};
  // Sanity check that we have all the weekdays.
  ABSL_CHECK_EQ(7, weekday_to_year.size());
  for (const auto& weekday_year : weekday_to_year) {
    const absl::Weekday weekday = weekday_year.first;
    const absl::CivilYear year(weekday_year.second);
    // Sanity check that the year begins on the expected day.
    ABSL_CHECK_EQ(weekday, absl::GetWeekday(year));

    for (const DateTimestampPart date_part :
         {WEEK, WEEK_MONDAY, WEEK_TUESDAY, WEEK_WEDNESDAY, WEEK_THURSDAY,
          WEEK_FRIDAY, WEEK_SATURDAY}) {
      auto weekday_for_date_part = [](DateTimestampPart date_part) {
        switch (date_part) {
          case WEEK_MONDAY:
            return absl::Weekday::monday;
          case WEEK_TUESDAY:
            return absl::Weekday::tuesday;
          case WEEK_WEDNESDAY:
            return absl::Weekday::wednesday;
          case WEEK_THURSDAY:
            return absl::Weekday::thursday;
          case WEEK_FRIDAY:
            return absl::Weekday::friday;
          case WEEK_SATURDAY:
            return absl::Weekday::saturday;
          // For WEEK, Sunday is the first day of the week.
          case WEEK:
            return absl::Weekday::sunday;
          default:
            ABSL_LOG(FATAL) << "Unexpected date part: "
                       << DateTimestampPart_Name(date_part);
        }
      };
      const absl::Weekday first_weekday_in_week =
          weekday_for_date_part(date_part);

      // Advance from the beginning of the year one day at a time, keeping
      // track of the week number. We test with a bunch of weeks (enough to
      // get well into the second month).
      absl::CivilDay day = year;
      int week_number = 0;
      for (int i = 0; i < 6 * 7; ++i, ++day) {
        if (absl::GetWeekday(day) == first_weekday_in_week) {
          ++week_number;
        }

        const Value part_enum_value = Enum(part_enum, date_part);
        QueryParamsWithResult::FeatureSet required_feature_set =
            GetCustomWeekRequiredFeatures(part_enum_value.enum_value());

        // No timezone because EXTRACT from DATE does not support AT TIME ZONE.
        tests.push_back(
            {"extract",
             QueryParamsWithResult(
                 {Date(static_cast<int32_t>(day - absl::CivilDay(1970))),
                  part_enum_value},
                 Int64(week_number))
                 .AddRequiredFeatures(required_feature_set)});

        tests.push_back(
            {"extract", QueryParamsWithResult({Timestamp(absl::FromCivil(
                                                   day, absl::UTCTimeZone())),
                                               part_enum_value, "UTC"},
                                              Int64(week_number))
                            .AddRequiredFeatures(required_feature_set)});

        // No timezone because EXTRACT from DATETIME does not support AT TIME
        // ZONE.
        civil_time_test_cases.push_back(CivilTimeTestCase(
            {{DatetimeMicros(static_cast<int>(day.year()),
                             day.month(), day.day(), 0, 0, 0, 0),
              part_enum_value}},
            Int64(week_number), /*output_type=*/nullptr, required_feature_set));
      }
    }
  }

  // Cycle through all possible timezones
  Value ts = TimestampFromStr("2000-01-01 00:11:22.345678");
  Value datetime = DatetimeMicros(2000, 1, 1, 0, 11, 22, 345678);
  Value time = TimeMicros(0, 11, 22, 345678);

  tests.push_back({"extract", {ts, Enum(part_enum, "MINUTE")}, Int64(11)});
  tests.push_back({"extract", {ts, Enum(part_enum, "SECOND")}, Int64(22)});
  tests.push_back(
      {"extract", {ts, Enum(part_enum, "MILLISECOND")}, Int64(345)});
  tests.push_back(
      {"extract", {ts, Enum(part_enum, "MICROSECOND")}, Int64(345678)});

  civil_time_test_cases.push_back(
      CivilTimeTestCase({{datetime, Enum(part_enum, "MINUTE")}, Int64(11)}));
  civil_time_test_cases.push_back(
      CivilTimeTestCase({{datetime, Enum(part_enum, "SECOND")}, Int64(22)}));
  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{datetime, Enum(part_enum, "MILLISECOND")}, Int64(345)}));
  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{datetime, Enum(part_enum, "MICROSECOND")}, Int64(345678)}));

  civil_time_test_cases.push_back(
      CivilTimeTestCase({{time, Enum(part_enum, "MINUTE")}, Int64(11)}));
  civil_time_test_cases.push_back(
      CivilTimeTestCase({{time, Enum(part_enum, "SECOND")}, Int64(22)}));
  civil_time_test_cases.push_back(
      CivilTimeTestCase({{time, Enum(part_enum, "MILLISECOND")}, Int64(345)}));
  civil_time_test_cases.push_back(CivilTimeTestCase(
      {{time, Enum(part_enum, "MICROSECOND")}, Int64(345678)}));

  for (int timezone_offset = 0; timezone_offset <= 12; timezone_offset++) {
    tests.push_back(
        {"extract",
         {ts, Enum(part_enum, "HOUR"), absl::StrCat("+", timezone_offset)},
         Int64(timezone_offset)});

    Value adjusted_time = TimeMicros(timezone_offset, 11, 22, 345678);
    civil_time_test_cases.push_back(CivilTimeTestCase(
        {{ts, Enum(part_enum, "TIME"), absl::StrCat("+", timezone_offset)}},
        adjusted_time));

    if (timezone_offset > 0) {
      tests.push_back(
          {"extract",
           {ts, Enum(part_enum, "HOUR"), absl::StrCat("-", timezone_offset)},
           Int64(24 - timezone_offset)});

      Value adjusted_time = TimeMicros(24 - timezone_offset, 11, 22, 345678);
      civil_time_test_cases.push_back(CivilTimeTestCase(
          {{ts, Enum(part_enum, "TIME"), absl::StrCat("-", timezone_offset)}},
          adjusted_time));
    }
  }

  // extract DATE from TIMESTAMP
  tests.push_back({"extract",
                   {TimestampFromStr("2000-01-01 00:00:01+00"),
                    Enum(part_enum, "DATE"), "UTC"},
                   DateFromStr("2000-01-01")});
  tests.push_back({"extract",
                   {TimestampFromStr("2000-01-01 00:00:01+00"),
                    Enum(part_enum, "DATE"), "-01"},
                   DateFromStr("1999-12-31")});
  tests.push_back({"extract",
                   {TimestampFromStr("2009-12-31 23:59:59+00"),
                    Enum(part_enum, "DATE"), "UTC"},
                   DateFromStr("2009-12-31")});
  tests.push_back({"extract",
                   {TimestampFromStr("2009-12-31 23:59:59+00"),
                    Enum(part_enum, "DATE"), "+01"},
                   DateFromStr("2010-01-01")});
  // extract DATE on timestamp boundaries.
  tests.push_back({"extract",
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "DATE"), "UTC"},
                   DateFromStr("0001-01-01")});
  tests.push_back({"extract",
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "DATE"), "-01"},
                   NullDate(), OUT_OF_RANGE});
  tests.push_back({"extract",
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "DATE"), "+01"},
                   DateFromStr("0001-01-01")});
  tests.push_back({"extract",
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "DATE"), "UTC"},
                   DateFromStr("9999-12-31")});
  tests.push_back({"extract",
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "DATE"), "-01"},
                   DateFromStr("9999-12-31")});
  tests.push_back({"extract",
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "DATE"), "+01"},
                   NullDate(), OUT_OF_RANGE});

  // extract DATETIME from TIMESTAMP
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2000-01-01 00:00:01+00"),
                    Enum(part_enum, "DATETIME"), "UTC"}},
                   DatetimeMicros(2000, 1, 1, 0, 0, 1, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2000-01-01 00:00:01+00"),
                    Enum(part_enum, "DATETIME"), "-01"}},
                   DatetimeMicros(1999, 12, 31, 23, 0, 1, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2009-12-31 23:59:59+00"),
                    Enum(part_enum, "DATETIME"), "UTC"}},
                   DatetimeMicros(2009, 12, 31, 23, 59, 59, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2009-12-31 23:59:59+00"),
                    Enum(part_enum, "DATETIME"), "+01"}},
                   DatetimeMicros(2010, 1, 1, 0, 59, 59, 0)));
  // extract DATETIME on timestamp boundaries.
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "DATETIME"), "UTC"}},
                   DatetimeMicros(1, 1, 1, 0, 0, 0, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "DATETIME"), "-01"}},
                    FunctionEvalError(), DatetimeType()));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "DATETIME"), "+01"}},
                   DatetimeMicros(1, 1, 1, 1, 0, 0, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "DATETIME"), "UTC"}},
                   DatetimeMicros(9999, 12, 31, 23, 59, 59, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "DATETIME"), "-01"}},
                   DatetimeMicros(9999, 12, 31, 22, 59, 59, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "DATETIME"), "+01"}},
                    FunctionEvalError(), DatetimeType()));

  // extract TIME from TIMESTAMP
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2000-01-01 00:00:01+00"),
                    Enum(part_enum, "TIME"), "UTC"}},
                   TimeMicros(0, 0, 1, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2000-01-01 00:00:01+00"),
                    Enum(part_enum, "TIME"), "-01"}},
                   TimeMicros(23, 0, 1, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2009-12-31 23:59:59+00"),
                    Enum(part_enum, "TIME"), "UTC"}},
                   TimeMicros(23, 59, 59, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("2009-12-31 23:59:59+00"),
                    Enum(part_enum, "TIME"), "+01"}},
                   TimeMicros(0, 59, 59, 0)));
  // extract TIME on timestamp boundaries.
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "TIME"), "UTC"}},
                   TimeMicros(0, 0, 0, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "TIME"), "-01"}},
                   TimeMicros(23, 0, 0, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("0001-01-01 00:00:00+00"),
                    Enum(part_enum, "TIME"), "+01"}},
                   TimeMicros(1, 0, 0, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "TIME"), "UTC"}},
                   TimeMicros(23, 59, 59, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "TIME"), "-01"}},
                   TimeMicros(22, 59, 59, 0)));
  civil_time_test_cases.push_back(CivilTimeTestCase({
                   {TimestampFromStr("9999-12-31 23:59:59+00"),
                    Enum(part_enum, "TIME"), "+01"}},
                   TimeMicros(0, 59, 59, 0)));

  // Invalid timezones for extract.
  tests.push_back({"extract",
                   {TimestampFromStr("1970-01-01 00:00:00+00"),
                    Enum(part_enum, "MONTH"), "invalid time zone"},
                   NullInt64(),
                   OUT_OF_RANGE});
  tests.push_back({"extract",
                   {TimestampFromStr("1970-01-01 00:00:00+00"),
                    Enum(part_enum, "DATE"), "invalid time zone"},
                   NullDate(),
                   OUT_OF_RANGE});
  civil_time_test_cases.push_back(CivilTimeTestCase({
        {TimestampFromStr("1970-01-01 00:00:00+00"),
              Enum(part_enum, "TIME"), "invalid time zone"}},
      FunctionEvalError(), TimeType()));
  civil_time_test_cases.push_back(CivilTimeTestCase({
        {TimestampFromStr("1970-01-01 00:00:00+00"),
              Enum(part_enum, "DATETIME"), "invalid time zone"}},
      FunctionEvalError(), DatetimeType()));
  // An empty time zone is invalid.
  tests.push_back({"extract",
                   {TimestampFromStr("1970-01-01 00:00:00+00"),
                    Enum(part_enum, "MONTH"), ""},
                   NullInt64(),
                   OUT_OF_RANGE});
  tests.push_back({"extract",
                   {TimestampFromStr("1970-01-01 00:00:00+00"),
                    Enum(part_enum, "DATE"), ""},
                   NullInt64(),
                   OUT_OF_RANGE});
  civil_time_test_cases.push_back(CivilTimeTestCase({
        {TimestampFromStr("1970-01-01 00:00:00+00"),
              Enum(part_enum, "TIME"), ""}},
      FunctionEvalError(), TimeType()));
  civil_time_test_cases.push_back(CivilTimeTestCase({
        {TimestampFromStr("1970-01-01 00:00:00+00"),
              Enum(part_enum, "DATETIME"), ""}},
      FunctionEvalError(), DatetimeType()));

  // Wrap all the civil time test cases and add them to the tests.
  std::vector<FunctionTestCall> additional_test_cases =
      PrepareCivilTimeTestCaseAsFunctionTestCall(civil_time_test_cases,
                                                 "extract");
  tests.insert(tests.end(), additional_test_cases.begin(),
               additional_test_cases.end());

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsDateAndTimestampDiff() {
  return ConcatTests<FunctionTestCall>({
      GetFunctionTestsDateDiff(),
      GetFunctionTestsTimestampDiff(),
  });
}

std::vector<FunctionTestCall> GetFunctionTestsDateTimeStandardFunctionCalls() {
  auto list = ConcatTests<FunctionTestCall>({
      GetFunctionTestsDateFromUnixDate(),
      GetFunctionTestsFormatDateTimestamp(),
      GetFunctionTestsParseDateTimestamp(),
  });
  list = ConcatTests<FunctionTestCall>(
      {list, GetFunctionTestsFormatDatetime(), GetFunctionTestsFormatTime()});
  return list;
}

std::vector<FunctionTestCall> GetFunctionTestsDateTime() {
  return ConcatTests<FunctionTestCall>({
      GetFunctionTestsDateAndTimestampDiff(),
      GetFunctionTestsDateTimeStandardFunctionCalls(),
      GetFunctionTestsTimestampTrunc(),
  });
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampConversion() {
  std::vector<FunctionTestCall> tests{
      // NULL handling
      {"timestamp_seconds", {NullInt64()}, NullTimestamp()},
      {"timestamp_millis", {NullInt64()}, NullTimestamp()},
      {"timestamp_micros", {NullInt64()}, NullTimestamp()},
      {"timestamp_from_unix_seconds", {NullTimestamp()}, NullTimestamp()},
      {"timestamp_from_unix_millis", {NullTimestamp()}, NullTimestamp()},
      {"timestamp_from_unix_micros", {NullTimestamp()}, NullTimestamp()},
      {"timestamp", {NullString()}, NullTimestamp()},
      {"timestamp", {NullString(), NullString()}, NullTimestamp()},
      {"timestamp", {String("2000-01-01"), NullString()}, NullTimestamp()},
      {"timestamp", {NullString(), "+00"}, NullTimestamp()},
      // Returns NULL on NULL input, even if the time zone is invalid.
      {"timestamp", {NullString(), "invalid time zone"}, NullTimestamp()},
      {"timestamp",
       {String("2000-01-01"), /*timezone=*/""},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"unix_seconds", {NullTimestamp()}, NullInt64()},
      {"unix_millis", {NullTimestamp()}, NullInt64()},
      {"unix_micros", {NullTimestamp()}, NullInt64()},
      // Epoch
      {"timestamp_seconds", {Int64(0)}, TimestampFromUnixMicros(0)},
      {"timestamp_millis", {Int64(0)}, TimestampFromUnixMicros(0)},
      {"timestamp_micros", {Int64(0)}, TimestampFromUnixMicros(0)},
      {"timestamp",
       {String("1970-01-01 00:00:00+00")},
       TimestampFromUnixMicros(0)},
      {"timestamp",
       {String("1970-01-01 00:00:00"), String("UTC")},
       TimestampFromUnixMicros(0)},
      {"unix_seconds", {TimestampFromUnixMicros(0)}, Int64(0)},
      {"unix_millis", {TimestampFromUnixMicros(0)}, Int64(0)},
      {"unix_micros", {TimestampFromUnixMicros(0)}, Int64(0)},
      // Some timestamps (before and after epoch).
      {"timestamp_seconds",
       {Int64(-22070932459)},
       TimestampFromUnixMicros(-22070932459000000)},
      {"timestamp_millis",
       {Int64(-22070932459123)},
       TimestampFromUnixMicros(-22070932459123000)},
      {"timestamp_micros",
       {Int64(-22070932459123456)},
       TimestampFromUnixMicros(-22070932459123456)},
      {"timestamp_seconds",
       {Int64(1438939541)},
       TimestampFromUnixMicros(1438939541000000)},
      {"timestamp_millis",
       {Int64(1438939541123)},
       TimestampFromUnixMicros(1438939541123000)},
      {"timestamp_micros",
       {Int64(1438939541123456)},
       TimestampFromUnixMicros(1438939541123456)},
      {"timestamp",
       {String("1970-01-01 00:00:00.000001+00")},
       TimestampFromUnixMicros(1)},
      {"timestamp",
       {String("1969-12-31 23:59:59.999999+00")},
       TimestampFromUnixMicros(-1)},
      {"unix_seconds",
       {TimestampFromUnixMicros(-22070932459000000)},
       Int64(-22070932459)},
      {"unix_millis",
       {TimestampFromUnixMicros(-22070932459123000)},
       Int64(-22070932459123)},
      {"unix_micros",
       {TimestampFromUnixMicros(-22070932459123456)},
       Int64(-22070932459123456)},
      {"unix_seconds",
       {TimestampFromUnixMicros(1438939541000000)},
       Int64(1438939541)},
      {"unix_millis",
       {TimestampFromUnixMicros(1438939541789000)},
       Int64(1438939541789)},
      {"unix_micros",
       {TimestampFromUnixMicros(1438939541789123)},
       Int64(1438939541789123)},
      // Millis and seconds conversion does truncation downwards.
      {"unix_seconds", {TimestampFromUnixMicros(1)}, Int64(0)},
      {"unix_seconds",
       {TimestampFromUnixMicros(1500000000999999)},
       Int64(1500000000)},
      {"unix_millis", {TimestampFromUnixMicros(1)}, Int64(0)},
      {"unix_millis",
       {TimestampFromUnixMicros(1500000000999999)},
       Int64(1500000000999)},
      {"unix_seconds", {TimestampFromUnixMicros(-1)}, Int64(-1)},
      {"unix_seconds",
       {TimestampFromUnixMicros(-1500000000999999)},
       Int64(-1500000001)},
      {"unix_millis", {TimestampFromUnixMicros(-1)}, Int64(-1)},
      {"unix_millis",
       {TimestampFromUnixMicros(-1500000000999999)},
       Int64(-1500000001000)},
      // Boundaries
      {"timestamp_micros",
       {Int64(types::kTimestampMin)},
       TimestampFromUnixMicros(types::kTimestampMin)},
      {"timestamp_micros",
       {Int64(types::kTimestampMax)},
       TimestampFromUnixMicros(types::kTimestampMax)},
      {"timestamp_from_unix_seconds",
       {TimestampFromUnixMicros(123456789)},
       TimestampFromUnixMicros(123456789)},
      {"timestamp_from_unix_millis",
       {TimestampFromUnixMicros(types::kTimestampMin)},
       TimestampFromUnixMicros(types::kTimestampMin)},
      {"timestamp_from_unix_micros",
       {TimestampFromUnixMicros(types::kTimestampMax)},
       TimestampFromUnixMicros(types::kTimestampMax)},
      {"timestamp",
       {String("0001-01-01 00:00:00+00")},
       TimestampFromUnixMicros(types::kTimestampMin)},
      {"timestamp",
       {String("9999-12-31 23:59:59.999999+00")},
       TimestampFromUnixMicros(types::kTimestampMax)},
      // Beyond boundary causes error
      {"timestamp_micros",
       {Int64(types::kTimestampMin - 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_micros",
       {Int64(types::kTimestampMax + 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp",
       {String("0001-01-01 00:00:00"), String("+01")},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp",
       {String("9999-12-31 23:59:59.999999"), String("-01")},
       NullTimestamp(),
       OUT_OF_RANGE},
      // Multiplication overflows cause error (not wrong results)
      {"timestamp_millis",
       {Int64(std::numeric_limits<int64_t>::max())},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_millis",
       {Int64(std::numeric_limits<int64_t>::min())},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_millis",
       {Int64(types::kTimestampMin / 1000 - 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_millis",
       {Int64(types::kTimestampMax / 1000 + 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_seconds",
       {Int64(std::numeric_limits<int64_t>::max())},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_seconds",
       {Int64(std::numeric_limits<int64_t>::min())},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_seconds",
       {Int64(types::kTimestampMin / 1000000 - 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_seconds",
       {Int64(types::kTimestampMax / 1000000 + 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
  };
  size_t num_tests = tests.size();
  for (int i = 0; i < num_tests; i++) {
    const FunctionTestCall& test = tests[i];
    // For every timestamp_xxx test add exactly same test but with
    // timestamp_from_unix_xxx function.
    if (test.function_name == "timestamp_seconds") {
      tests.push_back(
          FunctionTestCall("timestamp_from_unix_seconds", test.params));
    } else if (test.function_name == "timestamp_millis") {
      tests.push_back(
          FunctionTestCall("timestamp_from_unix_millis", test.params));
    } else if (test.function_name == "timestamp_micros") {
      tests.push_back(
          FunctionTestCall("timestamp_from_unix_micros", test.params));
    }
  }

  std::vector<FunctionTestCall> uint64_tests{
      // TIMESTAMP_FROM_UNIX_SECONDS(<UINT64>)
      {"timestamp_from_unix_seconds",
       {Uint64(std::numeric_limits<uint64_t>::min())},
       TimestampFromUnixMicros(0)},
      {"timestamp_from_unix_seconds",
       {Uint64(1438939541)},
       TimestampFromUnixMicros(1438939541000000)},
      {"timestamp_from_unix_seconds",
       {Uint64(types::kTimestampMax / 1000000 + 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_from_unix_seconds",
       {Uint64(std::numeric_limits<uint64_t>::max())},
       NullTimestamp(),
       OUT_OF_RANGE},

      // TIMESTAMP_FROM_UNIX_MILLIS(<UINT64>)
      {"timestamp_from_unix_millis",
       {Uint64(std::numeric_limits<uint64_t>::min())},
       TimestampFromUnixMicros(0)},
      {"timestamp_from_unix_millis",
       {Uint64(1438939541123)},
       TimestampFromUnixMicros(1438939541123000)},
      {"timestamp_from_unix_millis",
       {Uint64(types::kTimestampMax / 1000 + 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_from_unix_millis",
       {Uint64(std::numeric_limits<uint64_t>::max())},
       NullTimestamp(),
       OUT_OF_RANGE},

      // TIMESTAMP_FROM_UNIX_MICROS(<UINT64>)
      {"timestamp_from_unix_micros",
       {Uint64(std::numeric_limits<uint64_t>::min())},
       TimestampFromUnixMicros(0)},
      {"timestamp_from_unix_micros",
       {Uint64(1438939541123456)},
       TimestampFromUnixMicros(1438939541123456)},
      {"timestamp_from_unix_micros",
       {Uint64(types::kTimestampMax)},
       TimestampFromUnixMicros(types::kTimestampMax)},
      {"timestamp_from_unix_micros",
       {Uint64(types::kTimestampMax + 1)},
       NullTimestamp(),
       OUT_OF_RANGE},
      {"timestamp_from_unix_micros",
       {Uint64(std::numeric_limits<uint64_t>::max())},
       NullTimestamp(),
       OUT_OF_RANGE},
  };
  // Require the language feature for all uint64 tests.
  for (FunctionTestCall& uint64_test : uint64_tests) {
    uint64_test.params.AddRequiredFeature(
        FEATURE_TIMESTAMP_FROM_UNIX_FUNCTIONS_WITH_UINT64);
    tests.push_back(uint64_test);
  }
  return tests;
}

// Adapted from TEST(DateTimeUtilTest, ConvertDateToTimestamp) from
// date_time_util_test.cc.
std::vector<FunctionTestCall> GetFunctionTestsTimestampFromDate() {
  using googlesql::types::kDateMax;
  using googlesql::types::kDateMin;
  using googlesql::types::kTimestampMax;
  using googlesql::types::kTimestampMin;
  constexpr int64_t kNaiveNumMicrosPerDay =
      24ll * 60ll * 60ll * 1000ll * 1000ll;

  return {
      // NULL handling
      {"timestamp", {NullDate()}, NullTimestamp()},
      {"timestamp", {NullDate(), NullString()}, NullTimestamp()},
      {"timestamp", {NullDate(), String("+0")}, NullTimestamp()},
      // Returns NULL on NULL input, even if the time zone is invalid.
      {"timestamp", {NullDate(), String("invalid time zone")}, NullTimestamp()},
      {"timestamp", {Date(1), NullString()}, NullTimestamp()},
      // Timezone = "+0"
      {"timestamp", {Date(1), String("+0")}, Timestamp(kNaiveNumMicrosPerDay)},
      {"timestamp",
       {Date(-1), String("+0")},
       Timestamp(-kNaiveNumMicrosPerDay)},
      {"timestamp", {Date(kDateMin), String("+0")}, Timestamp(kTimestampMin)},
      {"timestamp",
       {Date(kDateMax), String("+0")},
       Timestamp(kTimestampMax - kNaiveNumMicrosPerDay + 1)},
      // Timezone offset. The code-based tests do not allow setting the default
      // timezone, so the test for that case is in the date_from_timestamp test
      // in timestamp_with_default_time_zone{,_2}.test.
      {"timestamp",
       {{Date(kDateMin), String("-08:30")},
        Timestamp(kTimestampMin + (510 * 60) * int64_t{1000} * int64_t{1000})}},
      {"timestamp",
       {Date(kDateMax), String("+08:30")},
       Timestamp(kTimestampMax - kNaiveNumMicrosPerDay + 1 -
                 (510 * 60) * int64_t{1000} * int64_t{1000})},
      // Out of range
      {"timestamp",
       {Date(kDateMin), String("+1")},
       NullTimestamp(),
       OUT_OF_RANGE}};
}

// The test cases for the civil time construction functions are essentially the
// same as the various cast test cases from STRING in
// compliance/functions_testlib_cast.cc

std::vector<FunctionTestCall> GetFunctionTestsDateConstruction() {
  return {
    {"date", {Int64(1970), Int64(1), Int64(1)}, Date(0)},
    {"date", {Int32(1970), Int32(1), Int32(1)}, Date(0)},

    {"date", {1969, 12, 31}, Date(-1)},
    {"date", {1970, 1, 1},   Date(0)},
    {"date", {1970, 1, 2},   Date(1)},
    {"date", {2014, 2, 1},   Date(16102)},
    {"date", {2009, 2, 1},   Date(14276)},
    {"date", {2009, 2, 13},  Date(14288)},
    {"date", {2009, 12, 1},  Date(14579)},
    {"date", {2000, 2, 29},  Date(11016)},

    {"date", {1678, 1, 1},   Date(-106650)},
    {"date", {2261, 12, 31}, Date(106650)},

    {"date", {1, 1, 1},      Date(date_min)},
    {"date", {9999, 12, 31}, Date(date_max)},

    // Note that 0 on any field for date itself is invalid, but null values in
    // these cases takes precedence to produces a null value.
    {"date", {NullInt64(), 0, 0}, NullDate()},
    {"date", {0, NullInt64(), 0}, NullDate()},
    {"date", {0, 0, NullInt64()}, NullDate()},

    // Error cases
    {"date", {2009, 2, 29}, NullDate(), OUT_OF_RANGE},
    {"date", {2009, 1, 32}, NullDate(), OUT_OF_RANGE},
    {"date", {2009, 1, 0}, NullDate(), OUT_OF_RANGE},
    {"date", {2009, 1, -1}, NullDate(), OUT_OF_RANGE},
    {"date", {2009, 13, 30}, NullDate(), OUT_OF_RANGE},
    {"date", {2009, 0, 30}, NullDate(), OUT_OF_RANGE},
    {"date", {2009, -1, 30}, NullDate(), OUT_OF_RANGE},
    {"date", {0, 12, 31}, NullDate(), OUT_OF_RANGE},
    {"date", {10000, 1, 1}, NullDate(), OUT_OF_RANGE},
    {"date", {-2009, 2, 13}, NullDate(), OUT_OF_RANGE},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsTimeConstruction() {
  std::vector<CivilTimeTestCase> time_construction_test_cases = {
      {{Int64(12), Int64(34), Int64(56)}, TimeMicros(12, 34, 56, 0)},
      {{Int32(12), Int32(34), Int32(56)}, TimeMicros(12, 34, 56, 0)},

      {{0, 0, 0},    TimeMicros(0, 0, 0, 0)},
      {{23, 59, 59}, TimeMicros(23, 59, 59, 0)},
      {{1, 2, 3},    TimeMicros(1, 2, 3, 0)},
      {{23, 59, 60}, TimeMicros(0, 0, 0, 0)},
      {{12, 34, 60}, TimeMicros(12, 35, 0, 0)},

      {{NullInt64(), 0, 0}, NullTime()},
      {{0, NullInt64(), 0}, NullTime()},
      {{0, 0, NullInt64()}, NullTime()},

      // Error cases
      {{24, 0, 0}, FunctionEvalError(), TimeType()},
      {{-1, 0, 0}, FunctionEvalError(), TimeType()},
      {{12, 60, 0}, FunctionEvalError(), TimeType()},
      {{12, 34, 61}, FunctionEvalError(), TimeType()},
      {{12, 34, -1}, FunctionEvalError(), TimeType()},
  };
  return PrepareCivilTimeTestCaseAsFunctionTestCall(
      time_construction_test_cases, "time");
}

std::vector<FunctionTestCall> GetFunctionTestsDatetimeConstruction() {
  std::vector<CivilTimeTestCase> datetime_construction_test_cases = {
      // year-month-day hour-minute-second cases
      {{Int64(2006), Int64(1), Int64(2), Int64(3), Int64(4), Int64(5)},
       DatetimeMicros(2006, 1, 2, 3, 4, 5, 0)},
      {{Int32(2006), Int32(1), Int32(2), Int32(3), Int32(4), Int32(5)},
       DatetimeMicros(2006, 1, 2, 3, 4, 5, 0)},

      {{1, 1, 1, 0, 0, 0},         DatetimeMicros(1, 1, 1, 0, 0, 0, 0)},
      {{9999, 12, 31, 23, 59, 59}, DatetimeMicros(9999, 12, 31, 23, 59, 59, 0)},
      {{2006, 1, 2, 3, 4, 5},      DatetimeMicros(2006, 1, 2, 3, 4, 5, 0)},
      {{2015, 12, 16, 23, 59, 60}, DatetimeMicros(2015, 12, 17, 0, 0, 0, 0)},
      {{2015, 12, 16, 12, 59, 60}, DatetimeMicros(2015, 12, 16, 13, 0, 0, 0)},

      // date and time cases
      {{Date(0), TimeMicros(0, 0, 0, 0)},
        DatetimeMicros(1970, 1, 1, 0, 0, 0, 0)},
      {{Date(date_min), TimeMicros(0, 0, 0, 0)},
        DatetimeMicros(1, 1, 1, 0, 0, 0, 0)},
      {{Date(date_max), TimeMicros(23, 59, 59, 999999)},
        DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999)},
      // Date(16102) == "2014-02-01"
      {{Date(16102), TimeMicros(12, 34, 56, 0)},
        DatetimeMicros(2014, 2, 1, 12, 34, 56, 0)},
      {{Date(16102), TimeMicros(12, 34, 56, 789)},
        DatetimeMicros(2014, 2, 1, 12, 34, 56, 789)},

      // Note that 0 on any field for date itself is invalid, but null values in
      // these cases takes precedence to produces a null value.
      {{NullInt64(), 0, 0, 0, 0, 0}, NullDatetime()},
      {{0, NullInt64(), 0, 0, 0, 0}, NullDatetime()},
      {{0, 0, NullInt64(), 0, 0, 0}, NullDatetime()},
      {{0, 0, 0, NullInt64(), 0, 0}, NullDatetime()},
      {{0, 0, 0, 0, NullInt64(), 0}, NullDatetime()},
      {{0, 0, 0, 0, 0, NullInt64()}, NullDatetime()},

      // Error cases
      {{10000, 1, 1, 0, 0, 0}, FunctionEvalError(), DatetimeType()},
      {{9999, 12, 31, 23, 59, 60}, FunctionEvalError(), DatetimeType()},
      {{0, 12, 31, 23, 59, 59}, FunctionEvalError(), DatetimeType()},
      {{2015, 13, 6, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, -1, 6, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 32, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, -1, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 0, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, 11, 31, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, 2, 29, 12, 34, 56}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 16, 24, 0, 0}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 16, -1, 0, 0}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 16, 12, 60, 0}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 16, 12, -1, 0}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 16, 12, 34, 61}, FunctionEvalError(), DatetimeType()},
      {{2015, 12, 16, 12, 34, -1}, FunctionEvalError(), DatetimeType()},
  };
  return PrepareCivilTimeTestCaseAsFunctionTestCall(
      datetime_construction_test_cases, "datetime");
}

std::vector<FunctionTestCall> GetFunctionTestsConvertDatetimeToTimestamp() {
  std::vector<CivilTimeTestCase> convert_datetime_to_timestamp_test_cases = {
    {{NullDatetime()}, NullTimestamp()},
    {{NullDatetime(), NullString()}, NullTimestamp()},
    {{NullDatetime(), "UTC"}, NullTimestamp()},
    {{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0), NullString()}, NullTimestamp()},

    {{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0), "UTC"},
     TimestampFromStr("1970-01-01")},
    {{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0), "+08:00"},
     TimestampFromStr("1969-12-31 16:00:00")},
    {{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0), "-08:00"},
     TimestampFromStr("1970-01-01 08:00:00")},
    {{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0), "America/Los_Angeles"},
     TimestampFromStr("1970-01-01 08:00:00")},
    {{DatetimeMicros(1970, 1, 1, 0, 0, 0, 0)},  // default timezone
     TimestampFromStr("1970-01-01 08:00:00")},

    {{DatetimeMicros(2006, 1, 2, 3, 4, 5, 0), "UTC"},
     TimestampFromStr("2006-01-02 03:04:05")},
    {{DatetimeMicros(2006, 1, 2, 3, 4, 5, 0), "+08:00"},
     TimestampFromStr("2006-01-01 19:04:05")},
    {{DatetimeMicros(2006, 1, 2, 3, 4, 5, 0), "-08:00"},
     TimestampFromStr("2006-01-02 11:04:05")},
    {{DatetimeMicros(2006, 1, 2, 3, 4, 5, 0), "America/Los_Angeles"},
     TimestampFromStr("2006-01-02 11:04:05")},
    {{DatetimeMicros(2006, 1, 2, 3, 4, 5, 0)},  // default timezone
     TimestampFromStr("2006-01-02 11:04:05")},

    {{DatetimeMicros(1, 1, 1, 0, 0, 0, 0), "UTC"},
     TimestampFromStr("0001-01-01")},
    {{DatetimeMicros(1, 1, 1, 0, 0, 0, 0), "+08:00"},
     FunctionEvalError(),
     TimestampType()},
    {{DatetimeMicros(1, 1, 1, 0, 0, 0, 0), "-08:00"},
     TimestampFromStr("0001-01-01 08:00:00")},
    // Before 1884, timezone offset for America/Los_Angeles is -07:52:58
    {{DatetimeMicros(1, 1, 1, 0, 0, 0, 0), "America/Los_Angeles"},
     TimestampFromStr("0001-01-01 07:52:58")},
    {{DatetimeMicros(1, 1, 1, 0, 0, 0, 0)},  // default timezone
     TimestampFromStr("0001-01-01 07:52:58")},

    {{DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999), "UTC"},
     TimestampFromStr("9999-12-31 23:59:59.999999")},
    {{DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999), "+08:00"},
     TimestampFromStr("9999-12-31 15:59:59.999999")},
    {{DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999), "-08:00"},
     FunctionEvalError(), TimestampType()},
    {{DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999),
      "America/Los_Angeles"}, FunctionEvalError(), TimestampType()},
    {{DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999)},  // default timezone
     FunctionEvalError(), TimestampType()},

      // While passing Datetime parameter with nano precision, Reference
      // implementation produces nanos. Add FEATURE_TIMESTAMP_NANOS to the
      // feature set for nano tests.
      {{DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999912), "UTC"},
       TimestampFromStr("9999-12-31 23:59:59.999999912", kNanoseconds),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999913), "+08:00"},
       TimestampFromStr("9999-12-31 15:59:59.999999913", kNanoseconds),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999914), "-08:00"},
       FunctionEvalError(),
       TimestampType(),
       {FEATURE_TIMESTAMP_NANOS}},
      {{DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999915),
        "America/Los_Angeles"},
       FunctionEvalError(),
       TimestampType(),
       {FEATURE_TIMESTAMP_NANOS}},
      {{DatetimeNanos(9999, 12, 31, 23, 59, 59,
                      999999916)},  // default timezone
       FunctionEvalError(),
       TimestampType(),
       {FEATURE_TIMESTAMP_NANOS}},

    // Daylight savings transition, effectively using the timezone offset
    // before the transition period.
    {{DatetimeMicros(2015, 3, 8, 2, 30, 0, 0), "America/Los_Angeles"},
     TimestampFromStr("2015-03-08 02:30:00-08:00")},
    {{DatetimeMicros(2015, 11, 1, 1, 30, 0, 0), "America/Los_Angeles"},
     TimestampFromStr("2015-11-01 01:30:00-07:00")},
  };
  return PrepareCivilTimeTestCaseAsFunctionTestCall(
      convert_datetime_to_timestamp_test_cases, "timestamp");
}

std::vector<FunctionTestCall> GetFunctionTestsConvertTimestampToTime() {
  std::vector<CivilTimeTestCase> convert_timestamp_to_time_test_cases = {
    {{NullTimestamp()}, NullTime()},
    {{NullTimestamp(), NullString()}, NullTime()},
    {{NullTimestamp(), "UTC"}, NullTime()},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), NullString()},
     NullTime()},

    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "UTC"},
     TimeMicros(0, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "+08:00"},
     TimeMicros(8, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "-08:00"},
     TimeMicros(16, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "America/Los_Angeles"},
     TimeMicros(16, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC")},  // default timezone
     TimeMicros(16, 0, 0, 0)},

    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "UTC"},
     TimeMicros(0, 0, 0, 0)},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "+08:00"},
     TimeMicros(8, 0, 0, 0)},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "-08:00"},
     TimeMicros(16, 0, 0, 0)},
    // Before 1884, timezone offset for America/Los_Angeles is -07:52:58
    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "America/Los_Angeles"},
     TimeMicros(16, 7, 2, 0)},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC")},  // default timezone
     TimeMicros(16, 7, 2, 0)},

    {{TimestampFromStr("2012-03-11 02:30:40.000001 UTC")},  // default timezone
     TimeMicros(18, 30, 40, 1)},
    {{TimestampFromStr("2012-03-11 02:30:40.000001 America/Los_Angeles"),
                       "America/Los_Angeles"},
     TimeMicros(3, 30, 40, 1)},

    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"), "UTC"},
     TimeMicros(23, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"), "+08:00"},
     TimeMicros(7, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"), "-08:00"},
     TimeMicros(15, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"),
       "America/Los_Angeles"},
     TimeMicros(15, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC")},  // default timezone
     TimeMicros(15, 59, 59, 999999)},

      // If the input timestamp comes with nanos, which mean nano support must
      // have been enabled, then the output time should also come with nanos.
      {{TimestampFromStr("9999-12-31 23:59:59.999999996 UTC", kNanoseconds),
        "UTC"},
       TimeNanos(23, 59, 59, 999999996),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{TimestampFromStr("9999-12-31 23:59:59.999999997 UTC", kNanoseconds),
        "+08:00"},
       TimeNanos(7, 59, 59, 999999997),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{TimestampFromStr("9999-12-31 23:59:59.999999998 UTC", kNanoseconds),
        "-08:00"},
       TimeNanos(15, 59, 59, 999999998),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{TimestampFromStr("9999-12-31 23:59:59.999999910 UTC", kNanoseconds),
        "America/Los_Angeles"},
       TimeNanos(15, 59, 59, 999999910),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      // default timezone
      {{TimestampFromStr("9999-12-31 23:59:59.999999911 UTC", kNanoseconds)},
       TimeNanos(15, 59, 59, 999999911),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
  };
  return PrepareCivilTimeTestCaseAsFunctionTestCall(
      convert_timestamp_to_time_test_cases, "time");
}

std::vector<FunctionTestCall> GetFunctionTestsConvertTimestampToDatetime() {
  std::vector<CivilTimeTestCase> convert_timestamp_to_datetime_test_cases = {
    {{NullTimestamp()}, NullDatetime()},
    {{NullTimestamp(), NullString()}, NullDatetime()},
    {{NullTimestamp(), "UTC"}, NullDatetime()},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), NullString()},
     NullDatetime()},

    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "UTC"},
     DatetimeMicros(1970, 1, 1, 0, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "+08:00"},
     DatetimeMicros(1970, 1, 1, 8, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "-08:00"},
     DatetimeMicros(1969, 12, 31, 16, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC"), "America/Los_Angeles"},
     DatetimeMicros(1969, 12, 31, 16, 0, 0, 0)},
    {{TimestampFromStr("1970-01-01 00:00:00 UTC")},  // default timezone
     DatetimeMicros(1969, 12, 31, 16, 0, 0, 0)},

    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "UTC"},
     DatetimeMicros(1, 1, 1, 0, 0, 0, 0)},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "+08:00"},
     DatetimeMicros(1, 1, 1, 8, 0, 0, 0)},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "-08:00"},
     FunctionEvalError(), DatetimeType()},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC"), "America/Los_Angeles"},
     FunctionEvalError(), DatetimeType()},
    {{TimestampFromStr("0001-01-01 00:00:00 UTC")},  // default timezone
     FunctionEvalError(), DatetimeType()},

    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"), "UTC"},
     DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"), "+08:00"},
     FunctionEvalError(), DatetimeType()},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"), "-08:00"},
     DatetimeMicros(9999, 12, 31, 15, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC"),
       "America/Los_Angeles"},
     DatetimeMicros(9999, 12, 31, 15, 59, 59, 999999)},
    {{TimestampFromStr("9999-12-31 23:59:59.999999 UTC")},  // default timezone
     DatetimeMicros(9999, 12, 31, 15, 59, 59, 999999)},

      // If the input timestamp comes with nanos, which mean nano support must
      // have been enabled, then the output datetime should also come with
      // nanos.
      {{TimestampFromStr("9999-12-31 23:59:59.999999991 UTC", kNanoseconds),
        "UTC"},
       DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999991),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{TimestampFromStr("9999-12-31 23:59:59.999999992 UTC", kNanoseconds),
        "+08:00"},
       FunctionEvalError(),
       DatetimeType(),
       {FEATURE_TIMESTAMP_NANOS}},
      {{TimestampFromStr("9999-12-31 23:59:59.999999993 UTC", kNanoseconds),
        "-08:00"},
       DatetimeNanos(9999, 12, 31, 15, 59, 59, 999999993),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      {{TimestampFromStr("9999-12-31 23:59:59.999999994 UTC", kNanoseconds),
        "America/Los_Angeles"},
       DatetimeNanos(9999, 12, 31, 15, 59, 59, 999999994),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
      // default timezone
      {{TimestampFromStr("9999-12-31 23:59:59.999999995 UTC", kNanoseconds)},
       DatetimeNanos(9999, 12, 31, 15, 59, 59, 999999995),
       /*output_type=*/nullptr,
       {FEATURE_TIMESTAMP_NANOS}},
  };
  return PrepareCivilTimeTestCaseAsFunctionTestCall(
      convert_timestamp_to_datetime_test_cases, "datetime");
}

#define EXPECT_ERROR ""
// Tests for CAST format timestamps.
// Tests that should pass regardless of the time type.
struct CastFormatCommonTest {
  std::string format_string;
  std::string expected_result;
};

static std::vector<CastFormatCommonTest> GetCastFormatLiteralTests() {
  // Tests that should pass against kCommonTestTimestamp and kCommonTestDate.
  std::vector<CastFormatCommonTest> tests = {
      // Test literal characters and quote escaped strings.
      {R"("abcd")", "abcd"},
      {R"("abcd"YYYY"efgh")", "abcd2015efgh"},
      // UTF-8 as well.
      {R"("%ыфщ"YYYY"ыфщ")", "%ыфщ2015ыфщ"},
  };
  return tests;
}

static std::vector<CastFormatCommonTest> GetCommonCastFormatDateTests() {
  // Tests that should pass against kCommonTestTimestamp, kCommonTestDate, and
  // common_datetime.
  std::vector<CastFormatCommonTest> tests = {
      {"YYYY", "2015"},  {"RRRR", "2015"},
      {"YYY", "015"},    {"YY", "15"},
      {"RR", "15"},      {"Y", "5"},
      {"D", "4"},        {"DD", "08"},
      {"DDD", "189"},    {"Day", "Wednesday"},
      {"Dy", "Wed"},     {"DAy", "WEDNESDAY"},
      {"Month", "July"}, {"Mon", "Jul"},
      {"MM", "07"},      {"YYYY-MM-DD", "2015-07-08"},
  };
  return tests;
}

static std::vector<CastFormatCommonTest> GetCommonCastFormatTimeTests() {
  // Tests that should pass against kCommonTestTimestamp, common_datetime,
  // and common_time.
  std::vector<CastFormatCommonTest> tests = {
      {"HH", "01"},         {"HH12", "01"},
      {"HH24", "01"},       {"MI", "02"},
      {"SS", "03"},         {"SSSSS", "03723"},
      {"FF1", "4"},         {"FF2", "45"},
      {"FF3", "456"},       {"FF4", "4567"},
      {"FF5", "45678"},     {"FF6", "456789"},
      {"FF7", "4567890"},   {"FF8", "45678900"},
      {"FF9", "456789000"}, {"A.M.", "A.M."},
      {"P.M.", "A.M."},     {"AM", "AM"},
      {"P.m.", "A.M."},     {"A.m.", "A.M."},
      {"a.m.", "a.m."},     {"a.M.", "a.m."},
      {"PM", "AM"},         {"HH:MI:SS.FF9 PM", "01:02:03.456789000 AM"}};
  return tests;
}

// Formats that should fail for Date types.
static std::vector<std::string> GetInvalidDateCastFormats() {
  std::vector<std::string> tests = {
      "HH",  "HH12", "HH24", "MI",  "SS",  "SSSSS", "FF1",  "FF2", "FF3", "FF4",
      "FF5", "FF6",  "FF7",  "FF8", "FF9", "A.M.",  "P.M.", "TZH", "TZM"};
  return tests;
}

// Formats that should fail for Time types.
static std::vector<std::string> GetInvalidTimeCastFormats() {
  std::vector<std::string> tests = {
      "YYYY",  "YYY", "YY", "Y",   "RRRR", "RR", "MM",  "MON",
      "MONTH", "DAY", "DY", "DDD", "DD",   "D",  "TZH", "TZM"};
  return tests;
}

// Formats that should fail for DateTime types.
static std::vector<std::string> GetInvalidDatetimeCastFormats() {
  std::vector<std::string> tests = {"TZH", "TZM"};
  return tests;
}

static std::vector<FormatDateTest> GetCastFormatDateTests() {
  std::vector<FormatDateTest> tests;
  for (const auto& literal_test : GetCastFormatLiteralTests()) {
    tests.push_back({literal_test.format_string, kCommonTestDate,
                     literal_test.expected_result});
  }

  for (const auto& date_test : GetCommonCastFormatDateTests()) {
    // Test first char uppercase rest char lowercase.
    tests.push_back(
        {date_test.format_string, kCommonTestDate, date_test.expected_result});
    // Test all uppercase.
    tests.push_back({absl::AsciiStrToUpper(date_test.format_string),
                     kCommonTestDate,
                     absl::AsciiStrToUpper(date_test.expected_result)});
    // Test all lowercase.
    tests.push_back({absl::AsciiStrToLower(date_test.format_string),
                     kCommonTestDate,
                     absl::AsciiStrToLower(date_test.expected_result)});
  }

  // Tests edge dates
  tests.push_back({"DDD", "2000-01-01", "001"});
  tests.push_back({"DDD", "2000-12-31", "366"});
  tests.push_back({"DDD", "2001-12-31", "365"});

  // Tests failed formats.
  for (const std::string& failed_test : GetInvalidDateCastFormats()) {
    tests.push_back({failed_test, kCommonTestDate, EXPECT_ERROR});
  }
  return tests;
}

static std::vector<FormatTimestampTest> GetCastFormatTimestampTests() {
  int64_t epoch_timestamp = 0;
  int64_t timestamp;
  const std::string timezone = "America/Los_Angeles";
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(kCommonTestTimestamp, kCommonTestTimezone,
                                    kMicroseconds, &timestamp));

  std::vector<FormatTimestampTest> tests = {
      {"YYYY-MM-DD HH24:MI:SS", epoch_timestamp, "UTC", "1970-01-01 00:00:00"},

      // Test the hour in different time zones.
      {"HH24", timestamp, "America/Los_Angeles", "01"},
      {"HH24", timestamp, "Asia/Yangon", "14"},
      {"HH24", timestamp, "UTC", "08"},
  };

  for (const auto& literal_test : GetCastFormatLiteralTests()) {
    tests.push_back({literal_test.format_string, timestamp, kCommonTestTimezone,
                     literal_test.expected_result});
  }
  for (const auto& date_test : GetCommonCastFormatDateTests()) {
    tests.push_back({date_test.format_string, timestamp, kCommonTestTimezone,
                     date_test.expected_result});
  }
  for (const auto& time_test : GetCommonCastFormatTimeTests()) {
    tests.push_back({time_test.format_string, timestamp, kCommonTestTimezone,
                     time_test.expected_result});
  }

  // Timestamp range boundary testing.
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp("0001-01-01 00:00:00.000000", "UTC",
                                    kMicroseconds, &timestamp));
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", timestamp, "UTC",
                   "0001-01-01 00:00:00.000000+00:00"});

  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp("9999-12-31 23:59:59.999999", "UTC",
                                    kMicroseconds, &timestamp));
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", timestamp, "UTC",
                   "9999-12-31 23:59:59.999999+00:00"});
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", timestamp, timezone,
                   "9999-12-31 15:59:59.999999-08:00"});
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF6TZH:TZM", timestamp, "Asia/Yangon",
                   "10000-01-01 06:29:59.999999+06:30"});

  // Check that CastFormatTimestamp removes subsecond timezone offset
  // (before 1884 PST timezone had offset of 7h 52m 58s, but FormatTimestamp
  // should only use 7h 52m).
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp("1812-09-07 07:52:01", "UTC", kMicroseconds,
                                    &timestamp));
  tests.push_back({"YYYY-MM-DD HH24:MI:SS", timestamp, "America/Los_Angeles",
                   "1812-09-07 00:00:01"});
  tests.push_back({"YYYY-MM-DD HH24:MI:SSTZHTZM", timestamp,
                   "America/Los_Angeles", "1812-09-07 00:00:01-0752"});
  tests.push_back({"YYYYMMDD", timestamp, "America/Los_Angeles", "18120907"});
  tests.push_back({"HH24:MI:SS", timestamp, "America/Los_Angeles", "00:00:01"});
  tests.push_back({"Dy Mon  DD HH24:MI:SS YYYY", timestamp,
                   "America/Los_Angeles", "Mon Sep  07 00:00:01 1812"});

  return tests;
}

struct FormatTimestampPicosSuccessTest {
  std::string format_string;    // format string
  PicoTime timestamp;           // timestamp value to format
  std::string timezone;         // time zone to use for formatting
  std::string expected_result;  // expected output string
};

struct FormatTimestampPicosFailureTest {
  std::string format_string;   // format string
  PicoTime timestamp;          // timestamp value to format
  std::string timezone;        // time zone to use for formatting
  std::string expected_error;  // expected error
};

static std::vector<FormatTimestampPicosSuccessTest>
GetCastFormatTimestampPicosSuccessTests() {
  PicoTime epoch_timestamp = PicoTime();
  PicoTime timestamp;
  const std::string timezone_str = "America/Los_Angeles";
  absl::TimeZone timezone;
  GOOGLESQL_CHECK_OK(functions::MakeTimeZone(timezone_str, &timezone));
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      kCommonTestTimestamp, timezone, /*allow_tz_in_str=*/false, &timestamp));

  std::vector<FormatTimestampPicosSuccessTest> tests = {
      {"YYYY-MM-DD HH24:MI:SS", epoch_timestamp, "UTC", "1970-01-01 00:00:00"},

      // Test the hour in different time zones.
      {"HH24", timestamp, "America/Los_Angeles", "01"},
      {"HH24", timestamp, "Asia/Yangon", "14"},
      {"HH24", timestamp, "UTC", "08"},
  };

  for (const auto& literal_test : GetCastFormatLiteralTests()) {
    tests.push_back({literal_test.format_string, timestamp, kCommonTestTimezone,
                     literal_test.expected_result});
  }
  for (const auto& date_test : GetCommonCastFormatDateTests()) {
    tests.push_back({date_test.format_string, timestamp, kCommonTestTimezone,
                     date_test.expected_result});
  }
  for (const auto& time_test : GetCommonCastFormatTimeTests()) {
    tests.push_back({time_test.format_string, timestamp, kCommonTestTimezone,
                     time_test.expected_result});
  }

  // Timestamp range boundary testing.
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      "0001-01-01 00:00:00.000000", absl::UTCTimeZone(),
      /*allow_tz_in_str=*/false, &timestamp));
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF12TZH:TZM", timestamp, "UTC",
                   "0001-01-01 00:00:00.000000000000+00:00"});

  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      "9999-12-31 23:59:59.999999999999", absl::UTCTimeZone(),
      /*allow_tz_in_str=*/false, &timestamp));
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF12TZH:TZM", timestamp, "UTC",
                   "9999-12-31 23:59:59.999999999999+00:00"});
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF12TZH:TZM", timestamp, timezone_str,
                   "9999-12-31 15:59:59.999999999999-08:00"});
  tests.push_back({"YYYY-MM-DD HH24:MI:SS.FF12TZH:TZM", timestamp,
                   "Asia/Yangon", "10000-01-01 06:29:59.999999999999+06:30"});

  // Check that CastFormatTimestamp removes subsecond timezone offset
  // (before 1884 PST timezone had offset of 7h 52m 58s, but FormatTimestamp
  // should only use 7h 52m).
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      "1812-09-07 07:52:01", absl::UTCTimeZone(),
      /*allow_tz_in_str=*/false, &timestamp));
  tests.push_back({"YYYY-MM-DD HH24:MI:SS", timestamp, "America/Los_Angeles",
                   "1812-09-07 00:00:01"});
  tests.push_back({"YYYY-MM-DD HH24:MI:SSTZHTZM", timestamp,
                   "America/Los_Angeles", "1812-09-07 00:00:01-0752"});
  tests.push_back({"YYYYMMDD", timestamp, "America/Los_Angeles", "18120907"});
  tests.push_back({"HH24:MI:SS", timestamp, "America/Los_Angeles", "00:00:01"});
  tests.push_back({"Dy Mon  DD HH24:MI:SS YYYY", timestamp,
                   "America/Los_Angeles", "Mon Sep  07 00:00:01 1812"});

  // Fractional sconds.
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      "2024-12-31 23:59:59.123456789123", absl::UTCTimeZone(),
      /*allow_tz_in_str=*/false, &timestamp));
  tests.push_back({"FF1", timestamp, "UTC", "1"});
  tests.push_back({"FF2", timestamp, "UTC", "12"});
  tests.push_back({"FF3", timestamp, "UTC", "123"});
  tests.push_back({"FF4", timestamp, "UTC", "1234"});
  tests.push_back({"FF5", timestamp, "UTC", "12345"});
  tests.push_back({"FF6", timestamp, "UTC", "123456"});
  tests.push_back({"FF7", timestamp, "UTC", "1234567"});
  tests.push_back({"FF8", timestamp, "UTC", "12345678"});
  tests.push_back({"FF9", timestamp, "UTC", "123456789"});
  tests.push_back({"FF10", timestamp, "UTC", "1234567891"});
  tests.push_back({"FF11", timestamp, "UTC", "12345678912"});
  tests.push_back({"FF12", timestamp, "UTC", "123456789123"});

  return tests;
}

static std::vector<FormatTimestampPicosFailureTest>
GetCastFormatTimestampPicosFailureTests() {
  PicoTime timestamp;
  std::vector<FormatTimestampPicosFailureTest> tests;

  tests.push_back({"xyz", timestamp, "America/Los_Angeles",
                   "Cannot find matched format element at 0"});
  tests.push_back({"YYYY", timestamp, "xyz", "Invalid time zone: xyz"});

  return tests;
}

struct FormatValueTest {
  std::string format_string;
  Value value;
  std::string expected_result;
};

static std::vector<FormatValueTest> GetCastFormatDatetimeTests() {
  const Value common_datetime = DatetimeMicros(2015, 7, 8, 1, 2, 3, 456789);
  std::vector<FormatValueTest> tests = {
      // Boundary cases.
      {"YYYY-MM-DD HH24:MI:SS.FF6", DatetimeMicros(1, 1, 1, 0, 0, 0, 0),
       "0001-01-01 00:00:00.000000"},
      {"YYYY-MM-DD HH24:MI:SS.FF6",
       DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999),
       "9999-12-31 23:59:59.999999"},
  };

  for (const auto& literal_test : GetCastFormatLiteralTests()) {
    tests.push_back({literal_test.format_string, common_datetime,
                     literal_test.expected_result});
  }
  for (const auto& date_test : GetCommonCastFormatDateTests()) {
    tests.push_back(
        {date_test.format_string, common_datetime, date_test.expected_result});
  }
  for (const auto& time_test : GetCommonCastFormatTimeTests()) {
    tests.push_back(
        {time_test.format_string, common_datetime, time_test.expected_result});
  }

  // Tests failed formats.
  for (const std::string& failed_test : GetInvalidDatetimeCastFormats()) {
    tests.push_back({failed_test, common_datetime, EXPECT_ERROR});
  }

  return tests;
}

static std::vector<FormatValueTest> GetCastFormatTimeTests() {
  const Value common_time = TimeMicros(1, 2, 3, 456789);
  std::vector<FormatValueTest> tests = {
      // Boundary cases.
      // hour 00-23
      {"HH24", TimeMicros(23, 45, 1, 0), "23"},
      {"HH24", TimeMicros(1, 45, 1, 0), "01"},
      {"HH24", TimeMicros(0, 45, 1, 0), "00"},

      // hour 01-12
      {"HH A.M.", TimeMicros(23, 45, 1, 0), "11 P.M."},
      {"HH P.M.", TimeMicros(12, 45, 1, 0), "12 P.M."},
      {"HH AM", TimeMicros(11, 45, 1, 0), "11 AM"},
      {"HH PM", TimeMicros(0, 45, 1, 0), "12 AM"},

      {"HH12 AM", TimeMicros(23, 45, 1, 0), "11 PM"},
      {"HH12 PM", TimeMicros(12, 45, 1, 0), "12 PM"},
      {"HH12 A.M.", TimeMicros(11, 45, 1, 0), "11 A.M."},
      {"HH12 P.M.", TimeMicros(0, 45, 1, 0), "12 A.M."},

      // Time range boundary testing
      {"HH24:MI:SS.FF6", TimeMicros(0, 0, 0, 0), "00:00:00.000000"},
      {"HH24:MI:SS.FF6", TimeMicros(23, 59, 59, 999999), "23:59:59.999999"}};

  for (const auto& time_test : GetCommonCastFormatTimeTests()) {
    tests.push_back(
        {time_test.format_string, common_time, time_test.expected_result});
  }

  // Tests failed formats.
  for (const std::string& failed_test : GetInvalidTimeCastFormats()) {
    tests.push_back({failed_test, common_time, EXPECT_ERROR});
  }

  return tests;
}

#undef EXPECT_ERROR
std::vector<FunctionTestCall> GetFunctionTestsCastFormatDateTimestamp() {
  std::vector<FunctionTestCall> tests;
  for (const auto& test : GetCastFormatTimestampTests()) {
    if (IsExpectedError(test.expected_result)) {
      tests.push_back({"cast_format_timestamp",
                       {Timestamp(test.timestamp), String(test.format_string),
                        String(test.timezone)},
                       NullString(),
                       OUT_OF_RANGE});
    } else {
      tests.push_back({"cast_format_timestamp",
                       {Timestamp(test.timestamp), String(test.format_string),
                        String(test.timezone)},
                       String(test.expected_result)});
    }
  }
  for (const auto& test : GetCastFormatDateTests()) {
    if (IsExpectedError(test.expected_result)) {
      tests.push_back({"cast_format_date",
                       {DateFromStr(test.date), String(test.format_string)},
                       NullString(),
                       OUT_OF_RANGE});
    } else {
      tests.push_back({"cast_format_date",
                       {DateFromStr(test.date), String(test.format_string)},
                       String(test.expected_result)});
    }
  }
  for (const auto& test : GetCastFormatDatetimeTests()) {
    if (IsExpectedError(test.expected_result)) {
      tests.push_back({"cast_format_datetime",
                       {test.value, String(test.format_string)},
                       NullString(),
                       OUT_OF_RANGE});
    } else {
      tests.push_back({"cast_format_datetime",
                       {test.value, String(test.format_string)},
                       String(test.expected_result)});
    }
    tests.back().params.AddRequiredFeature(FEATURE_CIVIL_TIME);
  }
  for (const auto& test : GetCastFormatTimeTests()) {
    if (IsExpectedError(test.expected_result)) {
      tests.push_back({"cast_format_time",
                       {test.value, String(test.format_string)},
                       NullString(),
                       OUT_OF_RANGE});
    } else {
      tests.push_back({"cast_format_time",
                       {test.value, String(test.format_string)},
                       String(test.expected_result)});
    }
    tests.back().params.AddRequiredFeature(FEATURE_CIVIL_TIME);
  }
  return tests;
}

// Helper function for constructing simple TIMESTAMP_BUCKET tests.
// UTC timezone is used both for interpreting the strings
// <timestamp_string>, <origin_string> and <expected_result> as timestamps.
FunctionTestCall TimestampBucketTest(
    absl::string_view timestamp_string, absl::string_view interval_string,
    absl::string_view origin_string, absl::string_view expected_result,
    googlesql::functions::TimestampScale scale = kMicroseconds) {
  absl::TimeZone timezone = absl::UTCTimeZone();

  absl::Time timestamp;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(timestamp_string, timezone, scale,
                                    /*allow_tz_in_str=*/true, &timestamp))
      << timestamp_string;

  IntervalValue interval =
      *IntervalValue::ParseFromString(interval_string, /*allow_nanos=*/true);
  absl::Time origin;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(origin_string, timezone, scale,
                                    /*allow_tz_in_str=*/true, &origin))
      << origin_string;

  absl::Time expected_timestamp;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(expected_result, timezone, scale,
                                    /*allow_tz_in_str=*/true,
                                    &expected_timestamp))
      << expected_result;

  FunctionTestCall call{
      "timestamp_bucket",
      {Timestamp(timestamp), Value::Interval(interval), Timestamp(origin)},
      {Timestamp(expected_timestamp)}};

  call.params = call.params.AddRequiredFeatures(
      {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE});
  return call;
}

// Helper function for constructing TIMESTAMP_BUCKET tests for error cases.
// UTC timezone is for interpreting the strings <timestamp_string> and
// <origin_string>.
FunctionTestCall TimestampBucketErrorTest(
    absl::string_view timestamp_string, absl::string_view interval_string,
    absl::string_view origin_string, absl::string_view expected_error,
    googlesql::functions::TimestampScale scale = kMicroseconds) {
  absl::TimeZone timezone = absl::UTCTimeZone();

  absl::Time timestamp;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(timestamp_string, timezone, scale,
                                    /*allow_tz_in_str=*/true, &timestamp))
      << timestamp_string;

  IntervalValue interval =
      *IntervalValue::ParseFromString(interval_string, /*allow_nanos=*/true);

  absl::Time origin;
  GOOGLESQL_CHECK_OK(ConvertStringToTimestamp(origin_string, timezone, scale,
                                    /*allow_tz_in_str=*/true, &origin))
      << origin_string;

  FunctionTestCall call{
      "timestamp_bucket",
      {Timestamp(timestamp), Value::Interval(interval), Timestamp(origin)},
      NullTimestamp(),
      absl::OutOfRangeError(expected_error)};

  call.params = call.params.AddRequiredFeatures(
      {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE});
  return call;
}

std::vector<FunctionTestCall> GetFunctionTestsTimestampBucket() {
  std::vector<FunctionTestCall> v = {
      // Default origin and different intervals
      TimestampBucketTest("2020-03-15 14:57:39.646565731",
                          "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                          "1950-01-01 00:00:00",
                          "2020-03-15 14:57:39.646565600", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                          "1950-01-01 00:00:00", "2020-03-15 14:57:39.646500"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:0.100",  // INTERVAL 100 MILLISECOND
                          "1950-01-01 00:00:00", "2020-03-15 14:57:39.600"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:15",  // INTERVAL 15 SECOND
                          "1950-01-01 00:00:00", "2020-03-15 14:57:30"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0:20:00",  // INTERVAL 20 MINUTE
                          "1950-01-01 00:00:00", "2020-03-15 14:40:00"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "3:00:00",  // INTERVAL 3 HOUR
                          "1950-01-01 00:00:00", "2020-03-15 12:00:00"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0-0 7",  // INTERVAL 7 DAY
                          "1950-01-01 00:00:00", "2020-03-15 00:00:00"),
      // Non-default origin and different intervals
      TimestampBucketTest("2020-03-15 14:57:39.646565731",
                          "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                          "1950-01-01 00:00:00.000000100",
                          "2020-03-15 14:57:39.646565700", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.646565731",
                          "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                          "1950-01-01 00:00:00.000000200",
                          "2020-03-15 14:57:39.646565600", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                          "1950-01-01 00:00:00.000100",
                          "2020-03-15 14:57:39.646100"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                          "1950-01-01 00:00:00.000500",
                          "2020-03-15 14:57:39.646500"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:0.100",  // INTERVAL 100 MILLISECOND
                          "1950-01-01 00:00:00.050", "2020-03-15 14:57:39.550"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:0.100",  // INTERVAL 100 MILLISECOND
                          "1950-01-01 00:00:00.100", "2020-03-15 14:57:39.600"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:15",  // INTERVAL 15 SECOND
                          "1950-01-01 00:00:05.500", "2020-03-15 14:57:35.500"),
      TimestampBucketTest("2020-03-15 14:57:39.646565",
                          "0:0:15",  // INTERVAL 15 SECOND
                          "1950-01-01 00:00:15", "2020-03-15 14:57:30"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0:20:00",  // INTERVAL 20 MINUTE
                          "1950-01-01 00:10:20", "2020-03-15 14:50:20"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0:20:00",  // INTERVAL 20 MINUTE
                          "1950-01-01 00:20:00", "2020-03-15 14:40:00"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "3:00:00",  // INTERVAL 3 HOUR
                          "1950-01-01 01:00:00", "2020-03-15 13:00:00"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "3:00:00",  // INTERVAL 3 HOUR
                          "1950-01-01 03:00:00", "2020-03-15 12:00:00"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0-0 7",  // INTERVAL 7 DAY
                          "1950-01-02 13:55:27.13456",
                          "2020-03-09 13:55:27.13456"),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0-0 7",  // INTERVAL 7 DAY
                          "1950-01-02 13:55:27.13456789",
                          "2020-03-09 13:55:27.13456789", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39",
                          "0-0 7",  // INTERVAL 7 DAY
                          "1950-01-08 00:00:00", "2020-03-15 00:00:00"),
      // input < origin
      TimestampBucketTest("2020-03-15 14:57:39", "3:00:00",
                          "2051-01-01 00:00:00", "2020-03-15 12:00:00"),
      TimestampBucketTest("2020-03-15 14:57:39", "0-0 7", "2051-01-01 00:00:00",
                          "2020-03-15 00:00:00"),
      // input == origin
      TimestampBucketTest("2020-03-15 14:57:39", "3:35:17",
                          "2020-03-15 14:57:39", "2020-03-15 14:57:39"),
      TimestampBucketTest("0001-01-01 00:00:00", "3:35:17",
                          "0001-01-01 00:00:00", "0001-01-01 00:00:00"),
      TimestampBucketTest("9999-12-31 23:59:59.999999", "3:35:17",
                          "9999-12-31 23:59:59.999999",
                          "9999-12-31 23:59:59.999999"),
      TimestampBucketTest("2020-03-15 14:57:39", "0-0 7", "2020-03-15 14:57:39",
                          "2020-03-15 14:57:39"),
      TimestampBucketTest("0001-01-01 00:00:00", "0-0 7", "0001-01-01 00:00:00",
                          "0001-01-01 00:00:00"),
      TimestampBucketTest("9999-12-31 23:59:59.999999", "0-0 7",
                          "9999-12-31 23:59:59.999999",
                          "9999-12-31 23:59:59.999999"),
      // input is in the bucket right before origin
      TimestampBucketTest("1949-12-31 23:59:59.999999", "0-0 1",
                          "1950-01-01 00:00:00", "1949-12-31 00:00:00"),
      TimestampBucketTest("1949-12-31 23:59:59.999999999", "0-0 1",
                          "1950-01-01 00:00:00", "1949-12-31 00:00:00",
                          kNanoseconds),
      TimestampBucketTest("1949-12-31 00:00:00", "0-0 1", "1950-01-01 00:00:00",
                          "1949-12-31 00:00:00"),
      TimestampBucketTest("1949-12-31 13:27:34", "0-0 1", "1950-01-01 00:00:00",
                          "1949-12-31 00:00:00"),
      // input is in the origin bucket
      TimestampBucketTest("1950-01-01 00:00:00.000001", "0-0 1",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      TimestampBucketTest("1950-01-01 00:00:00.000000001", "0-0 1",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                          kNanoseconds),
      TimestampBucketTest("1950-01-01 23:59:59.999999", "0-0 1",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      TimestampBucketTest("1950-01-01 23:59:59.999999999", "0-0 1",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                          kNanoseconds),
      TimestampBucketTest("1950-01-01 13:27:34", "0-0 1", "1950-01-01 00:00:00",
                          "1950-01-01 00:00:00"),
      // input is in the bucket right after origin
      TimestampBucketTest("1950-01-02 23:59:59.999999", "0-0 1",
                          "1950-01-01 00:00:00", "1950-01-02 00:00:00"),
      TimestampBucketTest("1950-01-02 23:59:59.999999999", "0-0 1",
                          "1950-01-01 00:00:00", "1950-01-02 00:00:00",
                          kNanoseconds),
      TimestampBucketTest("1950-01-02 00:00:00", "0-0 1", "1950-01-01 00:00:00",
                          "1950-01-02 00:00:00"),
      TimestampBucketTest("1950-01-02 13:27:34", "0-0 1", "1950-01-01 00:00:00",
                          "1950-01-02 00:00:00"),
      // input is on the boundaries
      TimestampBucketTest("0001-01-01 00:00:00", "3:00:00",
                          "1950-01-01 00:00:00", "0001-01-01 00:00:00"),
      TimestampBucketTest("9999-12-31 23:59:59.999999", "3:00:00",
                          "1950-01-01 00:00:00", "9999-12-31 21:00:00"),
      TimestampBucketTest("0001-01-01 00:00:00", "0-0 1", "1950-01-01 00:00:00",
                          "0001-01-01 00:00:00"),
      TimestampBucketTest("9999-12-31 23:59:59.999999", "0-0 1",
                          "1950-01-01 00:00:00", "9999-12-31 00:00:00"),
      // origin is on the boundaries
      TimestampBucketTest("2020-03-15 14:57:39", "3:00:00",
                          "0001-01-01 00:00:00", "2020-03-15 12:00:00"),
      TimestampBucketTest("2020-03-15 14:57:39", "3:00:00",
                          "9999-12-31 23:59:59.999999",
                          "2020-03-15 11:59:59.999999"),
      TimestampBucketTest("2020-03-15 14:57:39", "3:00:00",
                          "9999-12-31 23:59:59.999999999",
                          "2020-03-15 11:59:59.999999999", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39" /* Sunday */, "0-0 7",
                          "0001-01-01 00:00:00", /* Monday */
                          "2020-03-09 00:00:00" /* Monday */),
      TimestampBucketTest("2020-03-15 14:57:39" /* Sunday */, "0-0 7",
                          "9999-12-31 23:59:59.999999", /* Friday */
                          "2020-03-13 23:59:59.999999" /* Friday */),
      TimestampBucketTest("2020-03-15 14:57:39" /* Sunday */, "0-0 7",
                          "9999-12-31 23:59:59.999999999", /* Friday */
                          "2020-03-13 23:59:59.999999999" /* Friday */,
                          kNanoseconds),
      // MONTH part is not supported
      TimestampBucketErrorTest(
          "2020-03-15 14:57:39", "0-1 0", "1950-01-01 00:00:00",
          "Bucket width INTERVAL with non-zero MONTH part is not allowed"),
      TimestampBucketErrorTest(
          "2020-03-15 14:57:39", "-0-1 0", "1950-01-01 00:00:00",
          "Bucket width INTERVAL with non-zero MONTH part is not allowed"),
      // Zero INTERVAL is not supported
      TimestampBucketErrorTest("2020-03-15 14:57:39", "0:0:0",
                               "1950-01-01 00:00:00",
                               "Zero bucket width INTERVAL is not allowed"),
      // Negative MICROSECOND part is not supported
      TimestampBucketErrorTest("2020-03-15 14:57:39", "-0:0:1",
                               "1950-01-01 00:00:00",
                               "Negative bucket width INTERVAL is not allowed"),
      // Negative DAY part is not supported
      TimestampBucketErrorTest("2020-03-15 14:57:39", "0-0 -1",
                               "1950-01-01 00:00:00",
                               "Negative bucket width INTERVAL is not allowed"),
      // Mixing DAY and MICROSECOND/NANOSECOND parts is not supported
      TimestampBucketErrorTest("2020-03-15 14:57:39", "0-0 1 0:0:0.000100",
                               "1950-01-01 00:00:00",
                               "Bucket width INTERVAL with mixed DAY and "
                               "subsecond parts is not allowed"),
      TimestampBucketErrorTest("2020-03-15 14:57:39", "0-0 1 0:0:0.000001",
                               "1950-01-01 00:00:00",
                               "Bucket width INTERVAL with mixed DAY and "
                               "subsecond parts is not allowed"),
      TimestampBucketErrorTest("2020-03-15 14:57:39", "0-0 1 0:0:0.000000001",
                               "1950-01-01 00:00:00",
                               "Bucket width INTERVAL with mixed DAY and "
                               "subsecond parts is not allowed",
                               kNanoseconds),
      // Mixing MICROSECONDs and NANOSECONDs.
      TimestampBucketTest("2020-03-15 14:57:39.646565731", "0:0:0.000002500",
                          "1950-01-01 00:00:00",
                          "2020-03-15 14:57:39.646565000", kNanoseconds),
      // Bucket is outside of TIMESTAMP range. Output needs to be at 0000-12-31
      TimestampBucketErrorTest("0001-01-01", "0-0 2", "0001-01-02",
                               "Bucket for 0001-01-01 00:00:00+00 "
                               "is outside of timestamp range"),
      // Max number of days (10000 * 366)
      TimestampBucketTest("2020-03-15 14:57:39", "0-0 3660000",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      // Max number of DAYs (10000 * 366), bucket is outside of TIMESTAMP range.
      // Output needs to be at -7970-04-06
      TimestampBucketErrorTest("2020-03-15 14:57:39", "0-0 3660000",
                               "2051-01-01 00:00:00",
                               "Bucket for 2020-03-15 14:57:39+00 "
                               "is outside of timestamp range"),
      // Max number of MICROSECONDs (10000 * 366 * 24 hours)
      TimestampBucketTest("2020-03-15 14:57:39", "87840000:0:0",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      // Max number of MICROSECONDs (10000 * 366 * 24 hours), bucket is outside
      // of TIMESTAMP range. Output needs to be at -7970-04-06
      TimestampBucketErrorTest("2020-03-15 14:57:39", "87840000:0:0",
                               "2051-01-01 00:00:00",
                               "Bucket for 2020-03-15 14:57:39+00 "
                               "is outside of timestamp range"),
      // Close to max number of NANOSECONDs
      TimestampBucketTest("2020-03-15 14:57:39", "87839999:59:59.999999999",
                          "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                          kNanoseconds),
      // Very large bucket width (1000 * 366 * 24 hours) and NANOSECOND
      // precision
      TimestampBucketTest("5220-03-15 14:57:39.123456789",
                          "8784000:00:00.000000005", "1950-01-01 00:00:00",
                          "4956-03-22 00:00:00.000000015", kNanoseconds),
      TimestampBucketTest("2220-03-15 14:57:39.123456789",
                          "8784000:00:00.000000005", "5000-01-01 00:00:00",
                          "1993-10-11 23:59:59.999999985", kNanoseconds),
      // Buckets with NANOSECOND width to test for overflows
      TimestampBucketTest("2200-03-15 14:57:39.123456789",
                          "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                          "1900-01-01 00:00:00",
                          "2200-03-15 14:57:39.123456789", kNanoseconds),
      TimestampBucketTest("2000-03-15 14:57:39.123456789",
                          "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                          "2301-01-01", "2000-03-15 14:57:39.123456789",
                          kNanoseconds),
      TimestampBucketTest("9999-12-31 23:59:59.123456789",
                          "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                          "0001-01-01 00:00:00",
                          "9999-12-31 23:59:59.123456789", kNanoseconds),
      TimestampBucketTest("0001-01-01 00:00:00.123456789",
                          "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                          "9999-12-31 23:59:59",
                          "0001-01-01 00:00:00.123456789", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.123456734",
                          "0:0:0.000000005",  // INTERVAL 5 NANOSECOND
                          "1950-01-01 00:00:00",
                          "2020-03-15 14:57:39.123456730", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.123456734",
                          "0:0:0.000000005",  // INTERVAL 5 NANOSECOND
                          "2050-01-01 00:00:00",
                          "2020-03-15 14:57:39.123456730", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.123456741",
                          "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                          "1950-01-01 00:00:00",
                          "2020-03-15 14:57:39.123456725", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.123456741",
                          "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                          "2050-01-01 00:00:00",
                          "2020-03-15 14:57:39.123456725", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.123456741",
                          "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                          "1950-01-01 00:00:00.000000002",
                          "2020-03-15 14:57:39.123456727", kNanoseconds),
      TimestampBucketTest("2020-03-15 14:57:39.123456741",
                          "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                          "2050-01-01 00:00:00.000000002",
                          "2020-03-15 14:57:39.123456727", kNanoseconds),
  };
  return v;
}

// Helper function for constructing simple DATETIME_BUCKET tests.
FunctionTestCall DatetimeBucketTest(
    absl::string_view datetime_string, absl::string_view interval_string,
    absl::string_view origin_string, absl::string_view expected_result,
    googlesql::functions::TimestampScale scale = kMicroseconds) {
  DatetimeValue datetime;
  GOOGLESQL_CHECK_OK(ConvertStringToDatetime(datetime_string, scale, &datetime))
      << datetime_string;
  IntervalValue interval =
      *IntervalValue::ParseFromString(interval_string, /*allow_nanos=*/true);
  DatetimeValue origin;
  GOOGLESQL_CHECK_OK(ConvertStringToDatetime(origin_string, scale, &origin))
      << origin_string;
  DatetimeValue expected_datetime;
  GOOGLESQL_CHECK_OK(ConvertStringToDatetime(expected_result, scale, &expected_datetime))
      << expected_result;

  FunctionTestCall call{"datetime_bucket",
                        {Value::Datetime(datetime), Value::Interval(interval),
                         Value::Datetime(origin)},
                        {Value::Datetime(expected_datetime)}};

  QueryParamsWithResult::FeatureSet feature_set;
  if (scale == kNanoseconds) {
    feature_set = {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE,
                   FEATURE_CIVIL_TIME, FEATURE_TIMESTAMP_NANOS};
  } else {
    feature_set = {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE,
                   FEATURE_CIVIL_TIME};
  }
  call.params = call.params.WrapWithFeatureSet(feature_set);
  return call;
}

// Helper function for constructing DATETIME_BUCKET tests for error cases.
FunctionTestCall DatetimeBucketErrorTest(
    absl::string_view datetime_string, absl::string_view interval_string,
    absl::string_view origin_string, absl::string_view expected_error,
    googlesql::functions::TimestampScale scale = kMicroseconds) {
  DatetimeValue datetime;
  GOOGLESQL_CHECK_OK(ConvertStringToDatetime(datetime_string, scale, &datetime))
      << datetime_string;
  IntervalValue interval =
      *IntervalValue::ParseFromString(interval_string, /*allow_nanos=*/true);
  DatetimeValue origin;
  GOOGLESQL_CHECK_OK(ConvertStringToDatetime(origin_string, scale, &origin))
      << origin_string;

  FunctionTestCall call{"datetime_bucket",
                        {Value::Datetime(datetime), Value::Interval(interval),
                         Value::Datetime(origin)},
                        NullDatetime(),
                        absl::OutOfRangeError(expected_error)};

  QueryParamsWithResult::FeatureSet feature_set;
  if (scale == kNanoseconds) {
    feature_set = {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE,
                   FEATURE_CIVIL_TIME, FEATURE_TIMESTAMP_NANOS};
  } else {
    feature_set = {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE,
                   FEATURE_CIVIL_TIME};
  }
  call.params = call.params.WrapWithFeatureSet(feature_set);
  return call;
}

std::vector<FunctionTestCall> GetFunctionTestsDatetimeBucket() {
  std::vector<FunctionTestCall> v = {
      // Default origin and different intervals
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.646565600",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.646500"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.646500",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.600"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.600",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:15",  // INTERVAL 15 SECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:30"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:15",  // INTERVAL 15 SECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:30",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:20:00",  // INTERVAL 20 MINUTE
                         "1950-01-01 00:00:00", "2020-03-15 14:40:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:20:00",  // INTERVAL 20 MINUTE
                         "1950-01-01 00:00:00", "2020-03-15 14:40:00",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "3:00:00",  // INTERVAL 3 HOUR
                         "1950-01-01 00:00:00", "2020-03-15 12:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "3:00:00",  // INTERVAL 3 HOUR
                         "1950-01-01 00:00:00", "2020-03-15 12:00:00",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0-0 7",  // INTERVAL 7 DAY
                         "1950-01-01 00:00:00", "2020-03-15 00:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0-0 7",  // INTERVAL 7 DAY
                         "1950-01-01 00:00:00", "2020-03-15 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0-2",  // INTERVAL 2 MONTH
                         "1950-01-01 00:00:00", "2020-03-01 00:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0-2",  // INTERVAL 2 MONTH
                         "1950-01-01 00:00:00", "2020-03-01 00:00:00",
                         kNanoseconds),
      // Non-default origin and different intervals
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                         "1950-01-01 00:00:00.000000100",
                         "2020-03-15 14:57:39.646565700", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                         "1950-01-01 00:00:00.000000231",
                         "2020-03-15 14:57:39.646565631", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                         "1950-01-01 00:00:00.000100",
                         "2020-03-15 14:57:39.646100"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                         "1950-01-01 00:00:00.000500",
                         "2020-03-15 14:57:39.646500"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                         "1950-01-01 00:00:00.000500789",
                         "2020-03-15 14:57:39.646500789", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00.050", "2020-03-15 14:57:39.550"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00.100456",
                         "2020-03-15 14:57:39.600456"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00.100456789",
                         "2020-03-15 14:57:39.600456789", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:15",  // INTERVAL 15 SECOND
                         "1950-01-01 00:00:15", "2020-03-15 14:57:30"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0:0:15",  // INTERVAL 15 SECOND
                         "1950-01-01 00:00:05.123456",
                         "2020-03-15 14:57:35.123456"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0:0:15",  // INTERVAL 15 SECOND
                         "1950-01-01 00:00:05.123456789",
                         "2020-03-15 14:57:35.123456789", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0:20:00",  // INTERVAL 20 MINUTE
                         "1950-01-01 00:10:20", "2020-03-15 14:50:20"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0:20:00",  // INTERVAL 20 MINUTE
                         "1950-01-01 00:20:00.123456",
                         "2020-03-15 14:40:00.123456"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0:20:00",  // INTERVAL 20 MINUTE
                         "1950-01-01 00:20:00.123456789",
                         "2020-03-15 14:40:00.123456789", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "3:00:00",  // INTERVAL 3 HOUR
                         "1950-01-01 03:15:47.123456",
                         "2020-03-15 12:15:47.123456"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "3:00:00",  // INTERVAL 3 HOUR
                         "1950-01-01 03:15:47.123456789",
                         "2020-03-15 12:15:47.123456789", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0-0 7",  // INTERVAL 7 DAY
                         "1950-01-02 13:55:27.123456",
                         "2020-03-09 13:55:27.123456"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0-0 7",  // INTERVAL 7 DAY
                         "1950-01-02 13:55:27.123456789",
                         "2020-03-09 13:55:27.123456789", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0-2",  // INTERVAL 2 MONTH
                         "1950-12-07 13:55:27.123456",
                         "2020-02-07 13:55:27.123456"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0-2",  // INTERVAL 2 MONTH
                         "1950-12-07 13:55:27.123456789",
                         "2020-02-07 13:55:27.123456789", kNanoseconds),
      // input < origin
      DatetimeBucketTest("2020-03-15 14:57:39.646565731",
                         "0:0:0.000000200",  // INTERVAL 200 NANOSECOND
                         "2051-01-01 00:00:00", "2020-03-15 14:57:39.646565600",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.000500",  // INTERVAL 500 MICROSECOND
                         "2051-01-01 00:00:00", "2020-03-15 14:57:39.646500"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "2051-01-01 00:00:00", "2020-03-15 14:57:39.600"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0:0:15",  // INTERVAL 15 SECOND
                         "2051-01-01 00:00:00", "2020-03-15 14:57:30"),
      DatetimeBucketTest("2020-03-15 14:57:39",
                         "0:20:00",  // INTERVAL 20 MINUTE
                         "2051-01-01 00:00:00", "2020-03-15 14:40:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "3:00:00",  // INTERVAL 3 HOUR
                         "2051-01-01 00:00:00", "2020-03-15 12:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0-0 7",  // INTERVAL 7 DAY
                         "1950-01-01 00:00:00", "2020-03-15 00:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39.646565",
                         "0-2",  // INTERVAL 2 MONTH
                         "2051-01-01 00:00:00", "2020-03-01 00:00:00"),
      // Input aligns with a bucket, but an offset may shift it to a previous
      // bucket
      DatetimeBucketTest("2020-03-15 00:00:00.000000500",
                         "0:0:0.000000100",  // INTERVAL 100 NANOSECOND
                         "1950-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-03-15 00:00:00.000000401", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:00:00.000000500",
                         "0:0:0.000000100",  // INTERVAL 100 NANOSECOND
                         "2051-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-03-15 00:00:00.000000401", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:00:00.000500",
                         "0:0:0.000100",  // INTERVAL 100 MICROSECOND
                         "1950-01-01 00:00:00.000001",  // microsecond offset
                         "2020-03-15 00:00:00.000401"),
      DatetimeBucketTest("2020-03-15 00:00:00.000500",
                         "0:0:0.000100",  // INTERVAL 100 MICROSECOND
                         "2051-01-01 00:00:00.000001",  // microsecond offset
                         "2020-03-15 00:00:00.000401"),
      DatetimeBucketTest("2020-03-15 00:00:00.000500",
                         "0:0:0.000100",  // INTERVAL 100 MICROSECOND
                         "1950-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-03-15 00:00:00.000400001", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:00:00.000500",
                         "0:0:0.000100",  // INTERVAL 100 MICROSECOND
                         "2051-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-03-15 00:00:00.000400001", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:00:00.500",
                         "0:0:0.100",                // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00.001",  // millisecond offset
                         "2020-03-15 00:00:00.401"),
      DatetimeBucketTest("2020-03-15 00:00:00.500",
                         "0:0:0.100",                // INTERVAL 100 MILLISECOND
                         "2051-01-01 00:00:00.001",  // millisecond offset
                         "2020-03-15 00:00:00.401"),
      DatetimeBucketTest("2020-03-15 00:00:00.500",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00.000001",  // microsecond offset
                         "2020-03-15 00:00:00.400001"),
      DatetimeBucketTest("2020-03-15 00:00:00.500",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "2051-01-01 00:00:00.000001",  // microsecond offset
                         "2020-03-15 00:00:00.400001"),
      DatetimeBucketTest("2020-03-15 00:00:00.500",
                         "0:0:0.100",  // INTERVAL 100 MILLISECOND
                         "1950-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-03-15 00:00:00.400000001", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:00:30",
                         "0:0:10",               // INTERVAL 10 SECOND
                         "1950-01-01 00:00:31",  // second offset
                         "2020-03-15 00:00:21"),
      DatetimeBucketTest("2020-03-15 00:00:30",
                         "0:0:10",               // INTERVAL 10 SECOND
                         "2051-01-01 00:00:31",  // second offset
                         "2020-03-15 00:00:21"),
      DatetimeBucketTest("2020-03-15 00:00:30",
                         "0:0:10",                   // INTERVAL 10 SECOND
                         "1950-01-01 00:00:30.001",  // millisecond offset
                         "2020-03-15 00:00:20.001"),
      DatetimeBucketTest("2020-03-15 00:00:30",
                         "0:0:10",                   // INTERVAL 10 SECOND
                         "2051-01-01 00:00:30.001",  // millisecond offset
                         "2020-03-15 00:00:20.001"),
      DatetimeBucketTest("2020-03-15 00:00:30",
                         "0:0:10",                         // INTERVAL 10 SECOND
                         "1950-01-01 00:00:30.000000001",  // nanosecond offset
                         "2020-03-15 00:00:20.000000001", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:30:00",
                         "0:10:00",              // INTERVAL 10 MINUTE
                         "1950-01-01 00:31:00",  // minute offset
                         "2020-03-15 00:21:00"),
      DatetimeBucketTest("2020-03-15 00:30:00",
                         "0:10:00",              // INTERVAL 10 MINUTE
                         "2051-01-01 00:31:00",  // minute offset
                         "2020-03-15 00:21:00"),
      DatetimeBucketTest("2020-03-15 00:30:00",
                         "0:10:00",              // INTERVAL 10 MINUTE
                         "1950-01-01 00:30:01",  // second offset
                         "2020-03-15 00:20:01"),
      DatetimeBucketTest("2020-03-15 00:30:00",
                         "0:10:00",              // INTERVAL 10 MINUTE
                         "2051-01-01 00:30:01",  // second offset
                         "2020-03-15 00:20:01"),
      DatetimeBucketTest("2020-03-15 00:30:00",
                         "0:10:00",                     // INTERVAL 10 MINUTE
                         "1950-01-01 00:30:00.000001",  // millisecond offset
                         "2020-03-15 00:20:00.000001"),
      DatetimeBucketTest("2020-03-15 00:30:00",
                         "0:10:00",                        // INTERVAL 10 MINUTE
                         "1950-01-01 00:30:00.000000001",  // nanosecond offset
                         "2020-03-15 00:20:00.000000001", kNanoseconds),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",              // INTERVAL 4 HOUR
                         "1950-01-01 13:00:00",  // hour offset
                         "2020-03-15 09:00:00"),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",              // INTERVAL 4 HOUR
                         "2051-01-01 13:00:00",  // hour offset
                         "2020-03-15 09:00:00"),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",              // INTERVAL 4 HOUR
                         "1950-01-01 12:01:00",  // minute offset
                         "2020-03-15 08:01:00"),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",              // INTERVAL 4 HOUR
                         "2051-01-01 12:01:00",  // minute offset
                         "2020-03-15 08:01:00"),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",              // INTERVAL 4 HOUR
                         "1950-01-01 12:00:01",  // second offset
                         "2020-03-15 08:00:01"),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",                     // INTERVAL 4 HOUR
                         "1950-01-01 12:00:00.000001",  // millisecond offset
                         "2020-03-15 08:00:00.000001"),
      DatetimeBucketTest("2020-03-15 12:00:00",
                         "4:00:00",                        // INTERVAL 4 HOUR
                         "1950-01-01 12:00:00.000000001",  // nanosecond offset
                         "2020-03-15 08:00:00.000000001", kNanoseconds),
      DatetimeBucketTest("2020-03-15 00:00:00",
                         "0-0 7",                // INTERVAL 7 DAY
                         "1950-01-02 00:00:00",  // day offset
                         "2020-03-09 00:00:00"),
      DatetimeBucketTest("2020-03-15 00:00:00",
                         "0-0 7",                // INTERVAL 7 DAY
                         "2051-01-02 00:00:00",  // day offset
                         "2020-03-09 00:00:00"),
      DatetimeBucketTest("2020-03-15 00:00:00",
                         "0-0 7",                // INTERVAL 7 DAY
                         "1950-01-01 00:00:01",  // minute offset
                         "2020-03-08 00:00:01"),
      DatetimeBucketTest("2020-03-15 00:00:00",
                         "0-0 7",                // INTERVAL 7 DAY
                         "2051-01-01 00:00:01",  // minute offset
                         "2020-03-08 00:00:01"),
      DatetimeBucketTest("2020-03-15 00:00:00",
                         "0-0 7",                       // INTERVAL 7 DAY
                         "1950-01-01 00:00:00.000001",  // millisecond offset
                         "2020-03-08 00:00:00.000001"),
      DatetimeBucketTest("2020-03-15 00:00:00",
                         "0-0 7",                          // INTERVAL 7 DAY
                         "1950-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-03-08 00:00:00.000000001", kNanoseconds),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "1950-02-01 00:00:00",  // month offset
                         "2020-06-01 00:00:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "2051-02-01 00:00:00",  // month offset
                         "2020-06-01 00:00:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "1950-01-02 00:00:00",  // day offset
                         "2020-05-02 00:00:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "2051-01-02 00:00:00",  // day offset
                         "2020-05-02 00:00:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "1950-01-01 01:00:00",  // hour offset
                         "2020-05-01 01:00:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "2051-01-01 01:00:00",  // hour offset
                         "2020-05-01 01:00:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "1950-01-01 00:01:00",  // minute offset
                         "2020-05-01 00:01:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "2051-01-01 00:01:00",  // minute offset
                         "2020-05-01 00:01:00"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "1950-01-01 00:00:01",  // second offset
                         "2020-05-01 00:00:01"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                  // INTERVAL 4 MONTH
                         "2051-01-01 00:00:01",  // second offset
                         "2020-05-01 00:00:01"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                         // INTERVAL 4 MONTH
                         "1950-01-01 00:00:00.000001",  // microsecond offset
                         "2020-05-01 00:00:00.000001"),
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-4",                            // INTERVAL 4 MONTH
                         "1950-01-01 00:00:00.000000001",  // nanosecond offset
                         "2020-05-01 00:00:00.000000001", kNanoseconds),
      DatetimeBucketTest("2020-09-02 00:00:00",
                         "0-1",                  // INTERVAL 1 MONTH
                         "1950-01-01 12:55:30",  // mixed sub-day parts offset
                         "2020-09-01 12:55:30"),
      DatetimeBucketTest("2020-09-02 00:00:00",
                         "0-1",                  // INTERVAL 1 MONTH
                         "2051-01-01 12:55:30",  // mixed sub-day parts offset
                         "2020-09-01 12:55:30"),
      // input == origin
      DatetimeBucketTest("2020-03-15 14:57:39", "3:35:17",
                         "2020-03-15 14:57:39", "2020-03-15 14:57:39"),
      DatetimeBucketTest("0001-01-01 00:00:00", "3:35:17",
                         "0001-01-01 00:00:00", "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "3:35:17",
                         "9999-12-31 23:59:59.999999",
                         "9999-12-31 23:59:59.999999"),
      DatetimeBucketTest("2020-03-15 14:57:39", "0-0 7", "2020-03-15 14:57:39",
                         "2020-03-15 14:57:39"),
      DatetimeBucketTest("0001-01-01 00:00:00", "0-0 7", "0001-01-01 00:00:00",
                         "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-0 7",
                         "9999-12-31 23:59:59.999999",
                         "9999-12-31 23:59:59.999999"),
      // input is in the bucket right before origin
      DatetimeBucketTest("1949-12-31 23:59:59.999999", "0-0 1",
                         "1950-01-01 00:00:00", "1949-12-31 00:00:00"),
      DatetimeBucketTest("1949-12-31 23:59:59.999999999", "0-0 1",
                         "1950-01-01 00:00:00", "1949-12-31 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1949-12-31 00:00:00", "0-0 1", "1950-01-01 00:00:00",
                         "1949-12-31 00:00:00"),
      DatetimeBucketTest("1949-12-31 13:27:34", "0-0 1", "1950-01-01 00:00:00",
                         "1949-12-31 00:00:00"),
      DatetimeBucketTest("1949-12-31 23:59:59.999999", "0-1",
                         "1950-01-01 00:00:00", "1949-12-01 00:00:00"),
      DatetimeBucketTest("1949-12-31 23:59:59.999999999", "0-1",
                         "1950-01-01 00:00:00", "1949-12-01 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1949-12-01 00:00:00", "0-1", "1950-01-01 00:00:00",
                         "1949-12-01 00:00:00"),
      // input is in the origin bucket
      DatetimeBucketTest("1950-01-01 00:00:00.000001", "0-0 1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      DatetimeBucketTest("1950-01-01 00:00:00.000000001", "0-0 1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1950-01-01 00:00:00.000001", "0-1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      DatetimeBucketTest("1950-01-01 00:00:00.000000001", "0-1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1950-01-01 23:59:59.999999", "0-0 1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      DatetimeBucketTest("1950-01-01 23:59:59.999999999", "0-0 1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1950-01-01 23:59:59.999999", "0-1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      DatetimeBucketTest("1950-01-31 23:59:59.999999999", "0-1",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1950-01-01 13:27:34", "0-0 1", "1950-01-01 00:00:00",
                         "1950-01-01 00:00:00"),
      DatetimeBucketTest("1950-01-17 13:27:34", "0-1", "1950-01-01 00:00:00",
                         "1950-01-01 00:00:00"),
      // input is in the bucket right after origin
      DatetimeBucketTest("1950-01-02 23:59:59.999999", "0-0 1",
                         "1950-01-01 00:00:00", "1950-01-02 00:00:00"),
      DatetimeBucketTest("1950-01-02 23:59:59.999999999", "0-0 1",
                         "1950-01-01 00:00:00", "1950-01-02 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1950-02-28 23:59:59.999999", "0-1",
                         "1950-01-01 00:00:00", "1950-02-01 00:00:00"),
      DatetimeBucketTest("1950-02-28 23:59:59.999999999", "0-1",
                         "1950-01-01 00:00:00", "1950-02-01 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("1950-01-02 00:00:00", "0-0 1", "1950-01-01 00:00:00",
                         "1950-01-02 00:00:00"),
      DatetimeBucketTest("1950-01-02 13:27:34", "0-0 1", "1950-01-01 00:00:00",
                         "1950-01-02 00:00:00"),
      DatetimeBucketTest("1950-02-01 00:00:00", "0-1", "1950-01-01 00:00:00",
                         "1950-02-01 00:00:00"),
      // input is on the boundaries
      DatetimeBucketTest("0001-01-01 00:00:00", "3:00:00",
                         "1950-01-01 00:00:00", "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "3:00:00",
                         "1950-01-01 00:00:00", "9999-12-31 21:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999999", "3:00:00",
                         "1950-01-01 00:00:00", "9999-12-31 21:00:00",
                         kNanoseconds),
      DatetimeBucketTest("0001-01-01 00:00:00", "0-0 1", "1950-01-01 00:00:00",
                         "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-0 1",
                         "1950-01-01 00:00:00", "9999-12-31 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999999", "0-0 1",
                         "1950-01-01 00:00:00", "9999-12-31 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("0001-01-01 00:00:00", "0-1", "1950-01-01 00:00:00",
                         "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-1",
                         "1950-01-01 00:00:00", "9999-12-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999999", "0-1",
                         "1950-01-01 00:00:00", "9999-12-01 00:00:00",
                         kNanoseconds),
      // origin is on the boundaries
      DatetimeBucketTest("2020-03-15 14:57:39", "3:00:00",
                         "0001-01-01 00:00:00", "2020-03-15 12:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39", "3:00:00",
                         "9999-12-31 23:59:59.999999",
                         "2020-03-15 11:59:59.999999"),
      DatetimeBucketTest("2020-03-15 14:57:39", "3:00:00",
                         "9999-12-31 23:59:59.999999999",
                         "2020-03-15 11:59:59.999999999", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39" /* Sunday */, "0-0 7",
                         "0001-01-01 00:00:00", /* Monday */
                         "2020-03-09 00:00:00" /* Monday */),
      DatetimeBucketTest("2020-03-15 14:57:39" /* Sunday */, "0-0 7",
                         "9999-12-31 23:59:59.999999", /* Friday */
                         "2020-03-13 23:59:59.999999" /* Friday */),
      DatetimeBucketTest("2020-03-15 14:57:39" /* Sunday */, "0-0 7",
                         "9999-12-31 23:59:59.999999999", /* Friday */
                         "2020-03-13 23:59:59.999999999" /* Friday */,
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39", "0-1", "0001-01-01 00:00:00",
                         "2020-03-01 00:00:00"),
      DatetimeBucketTest("2020-03-15 14:57:39", "0-1",
                         "9999-12-31 23:59:59.999999",
                         "2020-02-29 23:59:59.999999"),
      DatetimeBucketTest("2020-03-15 14:57:39", "0-1",
                         "9999-12-31 23:59:59.999999999",
                         "2020-02-29 23:59:59.999999999", kNanoseconds),
      // input and origin are on the boundaries
      DatetimeBucketTest("0001-01-01 00:00:00", "3:00:00",
                         "0001-01-01 00:00:00", "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "3:00:00",
                         "9999-12-31 23:59:59.999999",
                         "9999-12-31 23:59:59.999999"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "3:00:00",
                         "0001-01-01 00:00:00", "9999-12-31 21:00:00"),
      DatetimeBucketErrorTest("0001-01-01 00:00:00", "3:00:00",
                              "9999-12-31 23:59:59.999999",
                              "Bucket for 0001-01-01 00:00:00 "
                              "is outside of datetime range"),
      DatetimeBucketTest("0001-01-01 00:00:00", "0-0 1", "0001-01-01 00:00:00",
                         "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-0 1",
                         "9999-12-31 23:59:59.999999",
                         "9999-12-31 23:59:59.999999"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-0 1",
                         "0001-01-01 00:00:00", "9999-12-31 00:00:00"),
      DatetimeBucketErrorTest("0001-01-01 00:00:00", "0-0 1",
                              "9999-12-31 23:59:59.999999",
                              "Bucket for 0001-01-01 00:00:00 "
                              "is outside of datetime range"),
      DatetimeBucketTest("0001-01-01 00:00:00", "0-1", "0001-01-01 00:00:00",
                         "0001-01-01 00:00:00"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-1",
                         "9999-12-31 23:59:59.999999",
                         "9999-12-31 23:59:59.999999"),
      DatetimeBucketTest("9999-12-31 23:59:59.999999", "0-1",
                         "0001-01-01 00:00:00", "9999-12-01 00:00:00"),
      DatetimeBucketErrorTest("0001-01-01 00:00:00", "0-1",
                              "9999-12-31 23:59:59.999999",
                              "Bucket for 0001-01-01 00:00:00 "
                              "is outside of datetime range"),
      // Zero INTERVAL is not supported
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0:0:0", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      // Negative MICROSECOND part is not supported
      DatetimeBucketErrorTest("2020-03-15 14:57:39", "-0:0:1",
                              "1950-01-01 00:00:00",
                              "Negative bucket width INTERVAL is not allowed"),
      // Negative DAY part is not supported
      DatetimeBucketErrorTest("2020-03-15 14:57:39", "0-0 -1",
                              "1950-01-01 00:00:00",
                              "Negative bucket width INTERVAL is not allowed"),
      // Negative MONTH part is not supported
      DatetimeBucketErrorTest("2020-03-15 14:57:39", "-0-1",
                              "1950-01-01 00:00:00",
                              "Negative bucket width INTERVAL is not allowed"),
      // Mixing MONTH, DAY and MICROSECOND/NANOSECOND parts is not supported
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0-1 1", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0-1 0 0:0:0.000100", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0-1 0 0:0:0.000000001", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required",
          kNanoseconds),
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0-0 1 0:0:0.000100", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0-0 1 0:0:0.000001", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      DatetimeBucketErrorTest(
          "2020-03-15 14:57:39", "0-0 1 0:0:0.000000001", "1950-01-01 00:00:00",
          "Exactly one non-zero INTERVAL part in bucket width is required",
          kNanoseconds),
      // Mixing MICROSECONDs and NANOSECONDs.
      DatetimeBucketTest("2020-03-15 14:57:39.646565731", "0:0:0.000002500",
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.646565000",
                         kNanoseconds),
      // Bucket is outside of DATETIME range. Output needs to be at 0000-12-31
      DatetimeBucketErrorTest("0001-01-01", "0-0 2", "0001-01-02",
                              "Bucket for 0001-01-01 00:00:00 "
                              "is outside of datetime range"),
      // Bucket is outside of DATETIME range. Output needs to be at 0000-12-01
      DatetimeBucketErrorTest("0001-01-01", "0-2", "0001-02-01",
                              "Bucket for 0001-01-01 00:00:00 "
                              "is outside of datetime range"),
      // Max number of days (10000 * 366)
      DatetimeBucketTest("2020-03-15 14:57:39", "0-0 3660000",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      // Max number of months (10000 * 12)
      DatetimeBucketTest("2020-03-15 14:57:39", "10000-0",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      // Max number of months (10000 * 12), bucket is outside of DATETIME range.
      // Output needs to be at -7949-01-01
      DatetimeBucketErrorTest("2020-03-15 14:57:39", "10000-0",
                              "2051-01-01 00:00:00",
                              "Bucket for 2020-03-15 14:57:39 "
                              "is outside of datetime range"),
      // Max number of DAYs (10000 * 366), bucket is outside of DATETIME range.
      // Output needs to be at -7970-04-06
      DatetimeBucketErrorTest("2020-03-15 14:57:39", "0-0 3660000",
                              "2051-01-01 00:00:00",
                              "Bucket for 2020-03-15 14:57:39 "
                              "is outside of datetime range"),
      // Max number of MICROSECONDs (10000 * 366 * 24 hours)
      DatetimeBucketTest("2020-03-15 14:57:39", "87840000:0:0",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00"),
      // Max number of MICROSECONDs (10000 * 366 * 24 hours), bucket is outside
      // of DATETIME range. Output needs to be at -7970-04-06
      DatetimeBucketErrorTest("2020-03-15 14:57:39", "87840000:0:0",
                              "2051-01-01 00:00:00",
                              "Bucket for 2020-03-15 14:57:39 "
                              "is outside of datetime range"),
      // Close to max number of NANOSECONDs
      DatetimeBucketTest("2020-03-15 14:57:39", "87839999:59:59.999999999",
                         "1950-01-01 00:00:00", "1950-01-01 00:00:00",
                         kNanoseconds),
      // Very large bucket width (1000 * 366 * 24 hours) and NANOSECOND
      // precision
      DatetimeBucketTest("5220-03-15 14:57:39.123456789",
                         "8784000:00:00.000000005", "1950-01-01 00:00:00",
                         "4956-03-22 00:00:00.000000015", kNanoseconds),
      DatetimeBucketTest("2220-03-15 14:57:39.123456789",
                         "8784000:00:00.000000005", "5000-01-01 00:00:00",
                         "1993-10-11 23:59:59.999999985", kNanoseconds),
      // Buckets with NANOSECOND width to test for overflows
      DatetimeBucketTest("2200-03-15 14:57:39.123456789",
                         "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                         "1900-01-01 00:00:00", "2200-03-15 14:57:39.123456789",
                         kNanoseconds),
      DatetimeBucketTest("2000-03-15 14:57:39.123456789",
                         "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                         "2301-01-01", "2000-03-15 14:57:39.123456789",
                         kNanoseconds),
      DatetimeBucketTest("9999-12-31 23:59:59.123456789",
                         "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                         "0001-01-01 00:00:00", "9999-12-31 23:59:59.123456789",
                         kNanoseconds),
      DatetimeBucketTest("0001-01-01 00:00:00.123456789",
                         "0:0:0.000000001",  // INTERVAL 1 NANOSECOND
                         "9999-12-31 23:59:59", "0001-01-01 00:00:00.123456789",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.123456734",
                         "0:0:0.000000005",  // INTERVAL 5 NANOSECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.123456730",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.123456734",
                         "0:0:0.000000005",  // INTERVAL 5 NANOSECOND
                         "2051-01-01 00:00:00", "2020-03-15 14:57:39.123456730",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.123456741",
                         "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                         "1950-01-01 00:00:00", "2020-03-15 14:57:39.123456725",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.123456741",
                         "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                         "2051-01-01 00:00:00", "2020-03-15 14:57:39.123456725",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.123456741",
                         "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                         "1950-01-01 00:00:00.000000002",
                         "2020-03-15 14:57:39.123456727", kNanoseconds),
      DatetimeBucketTest("2020-03-15 14:57:39.123456741",
                         "0:0:0.000000025",  // INTERVAL 25 NANOSECOND
                         "2051-01-01 00:00:00.000000002",
                         "2020-03-15 14:57:39.123456727", kNanoseconds),
      // Interesting MONTH part cases.
      // Origin is on the first day of the month:
      DatetimeBucketTest("2020-09-01 00:00:00",
                         "0-3",  // INTERVAL 3 MONTH
                         "1950-01-01 00:00:00", "2020-07-01 0:00:00"),
      DatetimeBucketTest("2020-09-30 23:59:59.999999999",
                         "0-4",  // INTERVAL 4 MONTH
                         "1950-01-01 00:00:00", "2020-09-01 0:00:00",
                         kNanoseconds),
      // Origin is on the last day of the month (31 day origin, 31 day output):
      DatetimeBucketTest("2020-08-31 00:00:00",
                         "0-3",  // INTERVAL 3 MONTH
                         "1950-01-31 00:00:00", "2020-07-31 0:00:00"),
      DatetimeBucketTest("2020-09-29 23:59:59.999999999",
                         "0-4",  // INTERVAL 4 MONTH
                         "1950-01-31 00:00:00", "2020-05-31 0:00:00",
                         kNanoseconds),
      // Origin is on the last day of the month (31 day origin, 30 day output):
      DatetimeBucketTest("2020-06-15 14:57:39",
                         "0-3",  // INTERVAL 3 MONTH
                         "1950-01-31 00:00:00", "2020-04-30 0:00:00"),
      // Origin is on the last day of the month (30 day origin, 30 day output):
      DatetimeBucketTest("2020-07-31 21:54:12.123456",
                         "0-2",  // INTERVAL 2 MONTH
                         "1950-04-30 00:00:00", "2020-06-30 00:00:00"),
      DatetimeBucketTest("2020-09-30 21:54:12.123456",
                         "0-2",  // INTERVAL 2 MONTH
                         "1950-04-30 00:00:00", "2020-08-30 00:00:00"),
      // Origin and input are on the last day of the month (31 day origin,
      // 30 day input):
      DatetimeBucketTest("2020-04-30 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-03-31 00:00:00", "2020-04-30 00:00:00"),
      DatetimeBucketTest("2020-04-30 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "2050-03-31 00:00:00", "2020-04-30 00:00:00"),
      // Origin and input are on the last day of the month (30 day origin,
      // 31 day input):
      DatetimeBucketTest("2020-05-31 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-04-30 00:00:00", "2020-05-30 00:00:00"),
      DatetimeBucketTest("2020-05-31 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "2050-04-30 00:00:00", "2020-05-30 00:00:00"),
      // Origin and input are on the last day of the month (28 day origin,
      // 31 day input):
      DatetimeBucketTest("2020-03-31 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-02-28 00:00:00", "2020-03-28 00:00:00"),
      // February edge cases:
      DatetimeBucketTest("2021-02-28 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:00", "2021-02-28 00:00:00"),
      DatetimeBucketTest("2021-02-28 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:01", "2021-01-31 00:00:01"),
      DatetimeBucketTest("2021-02-28 00:00:01",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:01", "2021-02-28 00:00:01"),
      DatetimeBucketTest("2021-02-28 10:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:01", "2021-02-28 00:00:01"),
      DatetimeBucketTest("2021-02-28 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:00", "2021-02-28 00:00:00"),
      DatetimeBucketTest("2021-02-28 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "2051-01-31 00:00:00", "2021-02-28 00:00:00"),
      DatetimeBucketTest("2020-02-28 00:00:00",  // last day of the month - 1
                         "0-1",                  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:00", "2020-01-31 00:00:00"),
      DatetimeBucketTest("2020-02-28 00:00:00",  // last day of the month - 1
                         "0-1",                  // INTERVAL 1 MONTH
                         "2051-01-31 00:00:00", "2020-01-31 00:00:00"),
      DatetimeBucketTest("2020-02-29 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "1950-01-31 00:00:00", "2020-02-29 00:00:00"),
      DatetimeBucketTest("2020-02-29 00:00:00",
                         "0-1",  // INTERVAL 1 MONTH
                         "2051-01-31 00:00:00", "2020-02-29 00:00:00"),
      DatetimeBucketTest("2021-03-30 23:59:59.999999",  // instant before 31
                         "0-1",                         // INTERVAL 1 MONTH
                         "1950-01-31 00:00:00", "2021-02-28 00:00:00"),
      DatetimeBucketTest("2021-03-30 23:59:59.999999",  // instant before 31
                         "0-1",                         // INTERVAL 1 MONTH
                         "2051-01-31 00:00:00", "2021-02-28 00:00:00"),
      DatetimeBucketTest("2020-03-30 23:59:59.999999999",  // instant before 31
                         "0-1",                            // INTERVAL 1 MONTH
                         "1950-01-31 00:00:00", "2020-02-29 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-30 23:59:59.999999999",  // instant before 31
                         "0-1",                            // INTERVAL 1 MONTH
                         "2051-01-31 00:00:00", "2020-02-29 00:00:00",
                         kNanoseconds),
      DatetimeBucketTest("2020-03-30 00:00:00",
                         "1-0",  // INTERVAL 1 YEAR
                         "1950-02-28 00:00:00", "2020-02-28 00:00:00"),
      DatetimeBucketTest("2020-03-30 00:00:00",
                         "1-0",  // INTERVAL 1 YEAR
                         "2052-02-29 00:00:00", "2020-02-29 00:00:00"),
      DatetimeBucketTest("2021-03-30 00:00:00",
                         "1-0",  // INTERVAL 1 YEAR
                         "2052-02-29 00:00:00", "2021-02-28 00:00:00"),
  };
  return v;
}

// Helper function for constructing simple DATE_BUCKET tests.
FunctionTestCall DateBucketTest(absl::string_view date_string,
                                absl::string_view interval_string,
                                absl::string_view origin_string,
                                absl::string_view expected_result) {
  int32_t date;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDate(date_string, &date)) << date_string;
  IntervalValue interval =
      *IntervalValue::ParseFromString(interval_string, /*allow_nanos=*/true);
  int32_t origin;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDate(origin_string, &origin))
      << origin_string;
  int32_t expected_date;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDate(expected_result, &expected_date))
      << expected_result;

  FunctionTestCall call{
      "date_bucket",
      {Value::Date(date), Value::Interval(interval), Value::Date(origin)},
      {Value::Date(expected_date)}};
  call.params = call.params.WrapWithFeatureSet(
      {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE});
  return call;
}

// Helper function for constructing DATE_BUCKET tests for error cases.
FunctionTestCall DateBucketErrorTest(absl::string_view date_string,
                                     absl::string_view interval_string,
                                     absl::string_view origin_string,
                                     absl::string_view expected_error) {
  int32_t date;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDate(date_string, &date)) << date_string;
  IntervalValue interval =
      *IntervalValue::ParseFromString(interval_string, /*allow_nanos=*/true);
  int32_t origin;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDate(origin_string, &origin))
      << origin_string;

  FunctionTestCall call{
      "date_bucket",
      {Value::Date(date), Value::Interval(interval), Value::Date(origin)},
      NullDate(),
      absl::OutOfRangeError(expected_error)};
  call.params = call.params.WrapWithFeatureSet(
      {FEATURE_TIME_BUCKET_FUNCTIONS, FEATURE_INTERVAL_TYPE});
  return call;
}

std::vector<FunctionTestCall> GetFunctionTestsDateBucket() {
  std::vector<FunctionTestCall> v = {
      // Default origin and different intervals
      DateBucketTest("2020-03-15",
                     "0-0 7",  // INTERVAL 7 DAY
                     "1950-01-01", "2020-03-15"),
      DateBucketTest("2020-03-15",
                     "0-2",  // INTERVAL 2 MONTH
                     "1950-01-01", "2020-03-01"),
      // Non-default origin and different intervals
      DateBucketTest("2020-03-15",
                     "0-0 7",  // INTERVAL 7 DAY
                     "1950-01-02", "2020-03-09"),
      DateBucketTest("2020-03-15",
                     "0-2",  // INTERVAL 2 MONTH
                     "1950-12-07", "2020-02-07"),
      // input < origin
      DateBucketTest("2020-03-15",
                     "0-0 7",  // INTERVAL 7 DAY
                     "1950-01-01", "2020-03-15"),
      DateBucketTest("2020-03-15",
                     "0-2",  // INTERVAL 2 MONTH
                     "2051-01-01", "2020-03-01"),
      // Input aligns with a bucket, but an offset may shift it to a previous
      // bucket
      DateBucketTest("2020-03-15",
                     "0-0 7",       // INTERVAL 7 DAY
                     "1950-01-02",  // day offset
                     "2020-03-09"),
      DateBucketTest("2020-03-15",
                     "0-0 7",       // INTERVAL 7 DAY
                     "2051-01-02",  // day offset
                     "2020-03-09"),
      DateBucketTest("2020-09-01",
                     "0-4",         // INTERVAL 4 MONTH
                     "1950-02-01",  // month offset
                     "2020-06-01"),
      DateBucketTest("2020-09-01",
                     "0-4",         // INTERVAL 4 MONTH
                     "2051-02-01",  // month offset
                     "2020-06-01"),
      DateBucketTest("2020-09-01",
                     "0-4",         // INTERVAL 4 MONTH
                     "1950-01-02",  // day offset
                     "2020-05-02"),
      DateBucketTest("2020-09-01",
                     "0-4",         // INTERVAL 4 MONTH
                     "2051-01-02",  // day offset
                     "2020-05-02"),
      // input == origin

      DateBucketTest("2020-03-15", "0-0 7", "2020-03-15", "2020-03-15"),
      DateBucketTest("0001-01-01", "0-0 7", "0001-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "0-0 7", "9999-12-31", "9999-12-31"),
      // input is in the bucket right before origin
      DateBucketTest("1949-12-31", "0-1", "1950-01-01", "1949-12-01"),
      DateBucketTest("1949-12-01", "0-1", "1950-01-01", "1949-12-01"),
      // input is in the origin bucket
      DateBucketTest("1950-01-17", "0-1", "1950-01-01", "1950-01-01"),
      // input is in the bucket right after origin
      DateBucketTest("1950-01-02", "0-0 1", "1950-01-01", "1950-01-02"),
      DateBucketTest("1950-02-28", "0-1", "1950-01-01", "1950-02-01"),
      DateBucketTest("1950-02-01", "0-1", "1950-01-01", "1950-02-01"),
      // input is on the boundaries
      DateBucketTest("0001-01-01", "0-0 1", "1950-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "0-0 1", "1950-01-01", "9999-12-31"),
      DateBucketTest("0001-01-01", "0-1", "1950-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "0-1", "1950-01-01", "9999-12-01"),
      // origin is on the boundaries
      DateBucketTest("2020-03-15" /* Sunday */, "0-0 7",
                     "0001-01-01", /* Monday */
                     "2020-03-09" /* Monday */),
      DateBucketTest("2020-03-15" /* Sunday */, "0-0 7",
                     "9999-12-31", /* Friday */
                     "2020-03-13" /* Friday */),
      DateBucketTest("2020-03-15", "0-1", "0001-01-01", "2020-03-01"),
      DateBucketTest("2020-03-15", "0-1", "9999-12-31", "2020-02-29"),
      // input and origin are on the boundaries
      DateBucketTest("0001-01-01", "0-0 1", "0001-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "0-0 1", "9999-12-31", "9999-12-31"),
      DateBucketTest("9999-12-31", "0-0 1", "0001-01-01", "9999-12-31"),
      DateBucketTest("0001-01-01", "0-0 1", "9999-12-31", "0001-01-01"),
      DateBucketTest("0001-01-01", "0-1", "0001-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "0-1", "9999-12-31", "9999-12-31"),
      DateBucketTest("9999-12-31", "0-1", "0001-01-01", "9999-12-01"),
      DateBucketErrorTest("0001-01-01", "0-1", "9999-12-31",
                          "Bucket for 0001-01-01 is outside of date range"),
      DateBucketErrorTest(
          "2020-03-15", "0-0", "1950-01-01",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      // MICROSECOND part is not supported
      DateBucketErrorTest(
          "2020-03-15", "24:00:00", "1950-01-01",
          "Only MONTH and DAY parts are allowed in bucket width INTERVAL"),
      // Negative DAY part is not supported
      DateBucketErrorTest("2020-03-15", "0-0 -1", "1950-01-01",
                          "Negative bucket width INTERVAL is not allowed"),
      // Negative MONTH part is not supported
      DateBucketErrorTest("2020-03-15", "-0-1", "1950-01-01",
                          "Negative bucket width INTERVAL is not allowed"),
      // Mixing MONTH and DAY parts is not supported
      DateBucketErrorTest(
          "2020-03-15", "0-1 1", "1950-01-01",
          "Exactly one non-zero INTERVAL part in bucket width is required"),
      // Bucket is outside of DATE range. Output needs to be at 0000-12-31
      DateBucketErrorTest("0001-01-01", "0-0 2", "0001-01-02",
                          "Bucket for 0001-01-01 is outside of date range"),
      // Bucket is outside of DATE range. Output needs to be at 0000-12-01
      DateBucketErrorTest("0001-01-01", "0-2", "0001-02-01",
                          "Bucket for 0001-01-01 is outside of date range"),
      // Max number of days (10000 * 366)
      DateBucketTest("2020-03-15", "0-0 3660000", "1950-01-01", "1950-01-01"),
      DateBucketErrorTest("0001-01-01", "0-0 3660000", "9999-12-31",
                          "Bucket for 0001-01-01 is outside of date range"),
      DateBucketTest("9999-12-31", "0-0 3660000", "0001-01-01", "0001-01-01"),
      DateBucketTest("0001-01-02", "0-0 3660000", "0001-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "0-0 3660000", "9999-12-30", "9999-12-30"),
      // Max number of months (10000 * 12)
      DateBucketTest("2020-03-15", "10000-0", "1950-01-01", "1950-01-01"),
      DateBucketErrorTest("0001-01-01", "10000-0", "9999-12-31",
                          "Bucket for 0001-01-01 is outside of date range"),
      DateBucketTest("9999-12-31", "10000-0", "0001-01-01", "0001-01-01"),
      DateBucketTest("0001-02-01", "10000-0", "0001-01-01", "0001-01-01"),
      DateBucketTest("9999-12-31", "10000-0", "9999-11-30", "9999-11-30"),
      // Max number of days (10000 * 366), bucket is outside of DATE range.
      // Output needs to be at -7970-04-06
      DateBucketErrorTest("2020-03-15", "0-0 3660000", "2051-01-01",
                          "Bucket for 2020-03-15 is outside of date range"),
      // Max number of months (10000 * 12), bucket is outside of DATE range.
      // Output needs to be at -7949-01-01
      DateBucketErrorTest("2020-03-15", "10000-0", "2051-01-01",
                          "Bucket for 2020-03-15 is outside of date range"),
      // Interesting MONTH part cases.
      // Origin is on the first day of the month:
      DateBucketTest("2020-09-01",
                     "0-3",  // INTERVAL 3 MONTH
                     "1950-01-01", "2020-07-01"),
      DateBucketTest("2020-09-30",
                     "0-4",  // INTERVAL 4 MONTH
                     "1950-01-01", "2020-09-01"),
      // Origin is on the last day of the month (31 day origin, 31 day output):
      DateBucketTest("2020-08-31",
                     "0-3",  // INTERVAL 3 MONTH
                     "1950-01-31", "2020-07-31"),
      DateBucketTest("2020-09-29",
                     "0-4",  // INTERVAL 4 MONTH
                     "1950-01-31", "2020-05-31"),
      // Origin is on the last day of the month (31 day origin, 30 day output):
      DateBucketTest("2020-06-15",
                     "0-3",  // INTERVAL 3 MONTH
                     "1950-01-31", "2020-04-30"),
      // Origin is on the last day of the month (30 day origin, 30 day output):
      DateBucketTest("2020-07-31",
                     "0-2",  // INTERVAL 2 MONTH
                     "1950-04-30", "2020-06-30"),
      DateBucketTest("2020-09-30",
                     "0-2",  // INTERVAL 2 MONTH
                     "1950-04-30", "2020-08-30"),
      // Origin and input are on the last day of the month (31 day origin,
      // 30 day input):
      DateBucketTest("2020-04-30",
                     "0-1",  // INTERVAL 1 MONTH
                     "1950-03-31", "2020-04-30"),
      DateBucketTest("2020-04-30",
                     "0-1",  // INTERVAL 1 MONTH
                     "2050-03-31", "2020-04-30"),
      // Origin and input are on the last day of the month (30 day origin,
      // 31 day input):
      DateBucketTest("2020-05-31",
                     "0-1",  // INTERVAL 1 MONTH
                     "1950-04-30", "2020-05-30"),
      DateBucketTest("2020-05-31",
                     "0-1",  // INTERVAL 1 MONTH
                     "2050-04-30", "2020-05-30"),
      // Origin and input are on the last day of the month (28 day origin,
      // 31 day input):
      DateBucketTest("2020-03-31",
                     "0-1",  // INTERVAL 1 MONTH
                     "1950-02-28", "2020-03-28"),
      // February edge cases:
      DateBucketTest("2021-02-28",  // last day of the month
                     "0-1",         // INTERVAL 1 MONTH
                     "1950-01-31", "2021-02-28"),
      DateBucketTest("2021-02-28",  // last day of the month
                     "0-1",         // INTERVAL 1 MONTH
                     "2051-01-31", "2021-02-28"),
      DateBucketTest("2020-02-28",  // last day of the month - 1
                     "0-1",         // INTERVAL 1 MONTH
                     "1950-01-31", "2020-01-31"),
      DateBucketTest("2020-02-28",  // last day of the month - 1
                     "0-1",         // INTERVAL 1 MONTH
                     "2051-01-31", "2020-01-31"),
      DateBucketTest("2020-02-29",
                     "0-1",  // INTERVAL 1 MONTH
                     "1950-01-31", "2020-02-29"),
      DateBucketTest("2020-02-29",
                     "0-1",  // INTERVAL 1 MONTH
                     "2051-01-31", "2020-02-29"),
      DateBucketTest("2021-03-30",  // day before 31
                     "0-1",         // INTERVAL 1 MONTH
                     "1950-01-31", "2021-02-28"),
      DateBucketTest("2021-03-30",  // day before 31
                     "0-1",         // INTERVAL 1 MONTH
                     "2051-01-31", "2021-02-28"),
      DateBucketTest("2020-03-30",
                     "1-0",  // INTERVAL 1 YEAR
                     "1950-02-28", "2020-02-28"),
      DateBucketTest("2020-03-30",
                     "1-0",  // INTERVAL 1 YEAR
                     "2052-02-29", "2020-02-29"),
      DateBucketTest("2021-03-30",
                     "1-0",  // INTERVAL 1 YEAR
                     "2052-02-29", "2021-02-28"),
  };
  return v;
}

}  // namespace googlesql
