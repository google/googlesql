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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_CAST_DATE_TIME_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_CAST_DATE_TIME_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/type.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// CastStringToTimestamp functions are used for CAST(input AS
// Timestamp FORMAT '...') syntax.
//
// Parses an input <timestamp_string> with the given input <format_string>,
// and produces the appropriate timestamp as output. Timestamp parts that are
// unspecified in the format are derived from 'current_year-current_month-01
// 00:00:00.000000' at the <default_timezone> ('current_year' and
// 'current_month' are from <current_timestamp> at the <default_timezone>).
// Produces <timestamp_micros> at microseconds precision, and returns an error
// if the resulting timestamp is not in the GoogleSQL valid range.
//
// Requires that the string_view arguments are UTF8.
//
// The supported format elements and their semantics are different from those
// for ParseStringToTimestamp functions and they are defined in:
// (broken link).
absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   absl::TimeZone default_timezone,
                                   absl::Time current_timestamp,
                                   int64_t* timestamp_micros);

// Invokes MakeTimeZone() on <default_timezone_string> and invokes the prior
// function. Returns error status if <default_timezone_string> is invalid
// or conversion fails.
absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   absl::string_view default_timezone_string,
                                   absl::Time current_timestamp,
                                   int64_t* timestamp_micros);

// The 2 functions below are similar to the above functions but support
// nanoseconds precision.
absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   absl::TimeZone default_timezone,
                                   absl::Time current_timestamp,
                                   absl::Time* timestamp);

absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   absl::string_view default_timezone_string,
                                   absl::Time current_timestamp,
                                   absl::Time* timestamp);

// Similar to the above functions but supports PicoTime.
absl::StatusOr<PicoTime> CastStringToTimestamp(
    absl::string_view format_string, absl::string_view timestamp_string,
    absl::string_view default_timezone_string, absl::Time current_timestamp);

absl::StatusOr<PicoTime> CastStringToTimestamp(
    absl::string_view format_string, absl::string_view timestamp_string,
    absl::TimeZone default_timezone, absl::Time current_timestamp);

// CastStringToDate function is used for CAST(input AS Date FORMAT '...')
// syntax.
//
// Parses an input <date_string> with the given input <format_string>, and
// produces the appropriate date as output. Date parts that are unspecified
// in the format are derived from 'current_year-current_month-01'
// ('current_year' and 'current_month' are from <current_date>). Returns an
// error if the <format_string> contains format elements unsupported for DATE or
// the resulting date is not in the GoogleSQL valid range.
//
// Requires that the string_view arguments are UTF8.
//
// The supported format elements and their semantics are different from those
// for ParseStringToDate functions and they are defined in:
// (broken link).
absl::Status CastStringToDate(absl::string_view format_string,
                              absl::string_view date_string,
                              int32_t current_date, int32_t* date);

// CastStringToTime function is used for CAST(input AS Time FORMAT '...')
// syntax.
//
// Parses an input <time_string> with the given input <format_string>, and
// produces the appropriate time as output. Time parts that are unspecified in
// the format are derived from '00:00:00.000000000'. Returns an error if the
// <format_string> contains a format element unsupported for TIME or the
// resulting time is not in the range of [00:00:00, 24:00:00).
//
// <scale> is used to specify the maximum precision supported for the format
// elements of type "kFFN", which will be 6 and 9 for micros and nanos,
// respectively. The parsed result value also respects this specified <scale>,
// which means that for the format element of "kFFN" type, while parsing will
// consume as many numeric digits as present, the parsed result will truncate
// any digits beyond 6 or 9 for micros and nanos, respectively. The same
// behavior also applys to the <scale> argument for CastStringToDateTime below.
//
// Requires that the string_view arguments are UTF8.
//
// The supported format elements and their semantics are different from those
// for ParseStringToTime function and they are defined in:
// (broken link).
absl::Status CastStringToTime(absl::string_view format_string,
                              absl::string_view time_string,
                              TimestampScale scale, TimeValue* time);

// CastStringToDatetime function is used for CAST(input AS
// Datetime FORMAT '...') syntax.
//
// Parses an input <datetime_string> with the given input <format_string>, and
// produces the appropriate datetime as output. Date and time parts that are
// unspecified in the format are derived from 'current_year-current_month-01
// 00:00:00.000000000' ('current_year' and 'current_month' are from
// <current_date>). Returns an error if the <format_string> contains a format
// element unsupported for DATETIME or the resulting datetime is not in the
// GoogleSQL valid range.
//
// Requires that the string_view arguments are UTF8.
//
// The supported format elements and their semantics are different from those
// for ParseStringToDatetime function and they are defined in:
// (broken link).
absl::Status CastStringToDatetime(absl::string_view format_string,
                                  absl::string_view datetime_string,
                                  TimestampScale scale, int32_t current_date,
                                  DatetimeValue* datetime);

// Perform validations on the <format_string> that is used for parse the given
// <out_type> according to specifications at (broken link).
absl::Status ValidateFormatStringForParsing(absl::string_view format_string,
                                            googlesql::TypeKind out_type);

// Perform validations on the <format_string> that is used to format the given
// <out_type>, according to specifications at (broken link).
absl::Status ValidateFormatStringForFormatting(absl::string_view format_string,
                                               googlesql::TypeKind out_type);

// Verifies that a date format contains year and month values. This is necessary
// to ensure that the output of CastStringToX will not depend on the current
// date.
absl::Status ValidateFormatStringContainsYearAndMonth(
    absl::string_view format_string);

// Populates <out> using the <format_string> following the formatting rules from
// (broken link).
//
// Assumes <timestamp> is the number of microseconds from
// 1970-01-01 UTC.
// Returns error status if conversion fails.
//
// <format_string> must be a valid utf-8 string, else this and all the other
// CastFormat functions will fail.
//
// Note, this method is not locale aware and generally formats in an en-US
// style. For example, months and days of week will use the en-US names.
absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         int64_t timestamp_micros,
                                         absl::TimeZone timezone,
                                         std::string* out);

// Invokes MakeTimeZone() on <timezone_string> and invokes the prior function.
// Returns error status if <timezone_string> is invalid or conversion fails.
absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         int64_t timestamp_micros,
                                         absl::string_view timezone_string,
                                         std::string* out);

absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         absl::Time timestamp,
                                         absl::TimeZone timezone,
                                         std::string* out);

absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         absl::Time timestamp,
                                         absl::string_view timezone_string,
                                         std::string* out);

absl::StatusOr<std::string> CastFormatTimestampToString(
    absl::string_view format_string, PicoTime timestamp,
    absl::string_view timezone_string);

absl::StatusOr<std::string> CastFormatTimestampToString(
    absl::string_view format_string, PicoTime timestamp,
    absl::TimeZone timezone);

// Populates <out> using the <format_str> following the formatting rules from
// (broken link).
// Assumes <date>: number of days since the epoch (1970-01-01)
//
// Does not allow timezone or time format elements.
// Returns error status if the conversion fails.
absl::Status CastFormatDateToString(absl::string_view format_string,
                                    int32_t date, std::string* out);

// Populates <out> using the <format_str> following the formatting rules from
// (broken link).
//
// Does not allow timezone related format elements.
// Returns error status if the conversion fails.
absl::Status CastFormatDatetimeToString(absl::string_view format_string,
                                        const DatetimeValue& datetime,
                                        std::string* out);

// Populates <out> using the <format_string> as defined by absl::FormatTime() in
// base/time.h. Returns error status if conversion fails.
//
// Does not allow timezone or date format elements.
absl::Status CastFormatTimeToString(absl::string_view format_string,
                                    const TimeValue& time, std::string* out);

namespace cast_date_time_internal {
enum class FormatElementCategory {
  kFormatElementCategoryUnspecified = 0,
  kLiteral,
  kYear,
  kMonth,
  kDay,
  kHour,
  kMinute,
  kSecond,
  kMeridianIndicator,
  kTimeZone,
  kCentury,
  kQuarter,
  kWeek,
  kEraIndicator,
  kMisc,
};

enum class FormatElementType {
  kFormatElementTypeUnspecified = 0,
  kSimpleLiteral,
  kDoubleQuotedLiteral,
  kWhitespace,
  kYYYY,
  kYYY,
  kYY,
  kY,
  kRRRR,
  kRR,
  kYCommaYYY,
  kIYYY,
  kIYY,
  kIY,
  kI,
  kSYYYY,
  kYEAR,
  kSYEAR,
  kMM,
  kMON,
  kMONTH,
  kRM,
  kDDD,
  kDD,
  kD,
  kDAY,
  kDY,
  kJ,
  kHH,
  kHH12,
  kHH24,
  kMI,
  kSS,
  kSSSSS,
  kFFN,
  kAM,
  kPM,
  kAMWithDots,
  kPMWithDots,
  kTZH,
  kTZM,
  kCC,
  kSCC,
  kQ,
  kIW,
  kWW,
  kW,
  kAD,
  kBC,
  kADWithDots,
  kBCWithDots,
  kSP,
  kTH,
  kSPTH,
  kTHSP,
  kFM
};

// This enum is used to specify the cases of the output letters when
// formatting timestamp with the format element.
enum FormatCasingType {
  kFormatCasingTypeUnspecified = 0,
  // Preserves casing of the output letters in the original input format string.
  kPreserveCase,
  // All of the letters in the output are capitalized, e.g.
  // "TWELVE THIRTY-FOUR".
  kAllLettersUppercase,
  // For each word in the output, only the first letter is capitalized, and the
  // other letters are lowercase, e.g. "Twelve Thirty-Four".
  kOnlyFirstLetterUppercase,
  // All of the letters in the output are lowercase, e.g. "twelve thirty-four".
  kAllLettersLowercase
};

struct DateTimeFormatElement {
  FormatElementType type = FormatElementType::kFormatElementTypeUnspecified;
  FormatElementCategory category =
      FormatElementCategory::kFormatElementCategoryUnspecified;
  // Length of the original format element string in the input format string.
  int len_in_format_str = 0;
  FormatCasingType format_casing_type =
      FormatCasingType::kFormatCasingTypeUnspecified;
  // <literal_value> is set only for the format element of "kSimpleLiteral" or
  // "kDoubleQuotedLiteral" type. If the element is of "kSimpleLiteral" type,
  // <literal_value> is the same as the original format element string; if the
  // element is of "kDoubleQuotedLiteral" type, format element string will be
  // first unquoted and unescaped. For example, if the format element string is
  // R"("abc\\")", the <literal_value> of the format element is R"(abc\)".
  std::string literal_value;
  // <subsecond_digit_count> is only set for elements of "kFFN" type to
  // indicate number of digits of subsecond part.
  int subsecond_digit_count = 0;
  // Returns single quoted string to represent the format element. If the
  // element is not in "kLiteral" category, the cases of output string may be
  // different from its original form in the user input format string. The
  // output string is intended to be visible to end users.
  std::string ToString() const;
};

absl::StatusOr<std::vector<DateTimeFormatElement>> GetDateTimeFormatElements(
    absl::string_view format_str);

}  // namespace cast_date_time_internal

class StringToDateCaster {
 public:
  static absl::StatusOr<StringToDateCaster> Create(
      absl::string_view format_string);

  // Cast the string to Date using the format string.
  absl::Status Cast(absl::string_view date_string, int32_t current_date,
                    int32_t* date) const;

 private:
  explicit StringToDateCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class StringToTimeCaster {
 public:
  static absl::StatusOr<StringToTimeCaster> Create(
      absl::string_view format_string);

  // Cast the string to Time using the format string.
  absl::Status Cast(absl::string_view time_string, TimestampScale scale,
                    TimeValue* time) const;

 private:
  explicit StringToTimeCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class StringToDatetimeCaster {
 public:
  static absl::StatusOr<StringToDatetimeCaster> Create(
      absl::string_view format_string);

  // Cast the string to Datetime using the format string.
  absl::Status Cast(absl::string_view datetime_string, TimestampScale scale,
                    int32_t current_date, DatetimeValue* datetime) const;

 private:
  explicit StringToDatetimeCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class StringToTimestampCaster {
 public:
  static absl::StatusOr<StringToTimestampCaster> Create(
      absl::string_view format_string);

  // Cast the string to Timestamp using the format string.
  absl::Status Cast(absl::string_view timestamp_string,
                    absl::TimeZone default_timezone,
                    absl::Time current_timestamp,
                    int64_t* timestamp_micros) const;

 private:
  explicit StringToTimestampCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class DateToStringCaster {
 public:
  static absl::StatusOr<DateToStringCaster> Create(
      absl::string_view format_string);

  // Cast the date to string using the format string.
  absl::Status Cast(int32_t date, std::string* out) const;

 private:
  explicit DateToStringCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class DatetimeToStringCaster {
 public:
  static absl::StatusOr<DatetimeToStringCaster> Create(
      absl::string_view format_string);

  // Cast the datetime to string using the format string.
  absl::Status Cast(const DatetimeValue& datetime, std::string* out) const;

 private:
  explicit DatetimeToStringCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class TimeToStringCaster {
 public:
  static absl::StatusOr<TimeToStringCaster> Create(
      absl::string_view format_string);

  // Cast the time to string using the format string.
  absl::Status Cast(const TimeValue& time, std::string* out) const;

 private:
  explicit TimeToStringCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

class TimestampToStringCaster {
 public:
  static absl::StatusOr<TimestampToStringCaster> Create(
      absl::string_view format_string);

  // Cast the timestamp to string using the format string.
  absl::Status Cast(int64_t timestamp_micros, absl::TimeZone timezone,
                    std::string* out) const;
  absl::Status Cast(absl::Time timestamp, absl::TimeZone timezone,
                    std::string* out) const;
  absl::StatusOr<std::string> Cast(PicoTime timestamp,
                                   absl::TimeZone timezone) const;

 private:
  explicit TimestampToStringCaster(
      std::vector<cast_date_time_internal::DateTimeFormatElement>&&
          format_elements)
      : format_elements_(std::move(format_elements)) {}

  std::vector<cast_date_time_internal::DateTimeFormatElement> format_elements_;
};

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_CAST_DATE_TIME_H_
