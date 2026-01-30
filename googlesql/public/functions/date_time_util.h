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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_DATE_TIME_UTIL_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_DATE_TIME_UTIL_H_

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "google/protobuf/timestamp.pb.h"
#include "google/type/date.pb.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "googlesql/base/status.h"

// GoogleSQL dates are represented as an int32 value, indicating the offset
// in days from the epoch 1970-01-01.  GoogleSQL dates are not timezone aware,
// and do not correspond to any particular 24 hour period.
//
// GoogleSQL timestamps are unix timestamps (scaled to include fractional
// seconds), are stored as an int64 value, and identify a specific point in
// time as an offset in appropriate <scale> units from the epoch
// 1970-01-01 00:00:00+00.  Valid timestamp <scale> values are seconds(0),
// milliseconds(3), microseconds(6), and nanoseconds(9).
//
// Converting between GoogleSQL timestamps and strings requires timezone
// awareness - the timezone must be known in order to map a string
// representation of a timestamp to a specific point in time or vice versa.
// At the GoogleSQL level, the timezone can be embedded in the string or can
// be provided via a default or separately specified value.  In this library,
// the caller must always provide the relevant time zone to an operation
// when one is required (this library does not have its own default time zone).

namespace googlesql {
namespace functions {

// Returns the SQL fragment corresponding to 'date_part' (which can be used in
// error messages).
absl::string_view DateTimestampPartToSQL(DateTimestampPart date_part);

// Returns the empty string if 'date_part' is not a valid DateTimestampPart.
inline absl::string_view DateTimestampPartToSQL(int date_part) {
  return DateTimestampPartToSQL(static_cast<DateTimestampPart>(date_part));
}

// TODO: Move this to datetime.proto.
enum TimestampScale {
  kSeconds = 0,
  kMilliseconds = 3,
  kMicroseconds = 6,
  kNanoseconds = 9,
  kPicoseconds = 12,
};

// Checks that scale specifies an accepted time precision.
absl::Status ValidateTimeScale(TimestampScale scale);

// Checks that a date value falls between 0001-01-01 and 9999-12-31 (inclusive).
ABSL_MUST_USE_RESULT bool IsValidDate(int32_t date);

// Checks that the specified year, month, day, are valid (i.e., month is
// 1-12, day is valid for the specified month, etc.).
ABSL_MUST_USE_RESULT bool IsValidDay(absl::civil_year_t year, int month,
                                     int day);

// Checks that a timestamp value falls between 0001-01-01 and 9999-12-31 UTC
// for timestamps with at most microseconds scale.  For nanoseconds, all int64
// values are considered valid.
ABSL_MUST_USE_RESULT bool IsValidTimestamp(int64_t timestamp,
                                           TimestampScale scale);

// Valid time zone offsets are -14:00 to +14:00, consistent with the SQL
// standard.  The input offset must be at minutes granularity.
ABSL_MUST_USE_RESULT bool IsValidTimeZone(int timezone_minutes_offset);

// Checks that a absl::Time value falls between 0001-01-01 and 9999-12-31 UTC.
ABSL_MUST_USE_RESULT bool IsValidTime(absl::Time time);

// Loads the TimeZone related to the string <timezone_string> into <timezone>,
// returning success or failure if <timezone_string> is invalid.  Supports
// a time zone of either the GoogleSQL canonical time zone format ('-08:00')
// or time zone name ('America/Los_Angeles', etc.):
//
//   ((+|-)[D]D[:[D]D]) | (<time zone name>)
//
// Named time zones are loaded from the system's zoneinfo directory (typically
// /usr/share/zoneinfo, /usr/share/lib/zoneinfo, etc.).  As per the base/time
// library, time zone names are case sensitive.
absl::Status MakeTimeZone(absl::string_view timezone_string,
                          absl::TimeZone* timezone);

// Creates a absl::Time from an int64 based timestamp value.
ABSL_DEPRECATED("Use MakeTimeFromInt64() instead.")
ABSL_MUST_USE_RESULT absl::Time MakeTime(int64_t timestamp,
                                         TimestampScale scale);

// Creates a absl::Time from an int64 based timestamp value.
absl::StatusOr<absl::Time> MakeTimeFromInt64(int64_t timestamp,
                                             TimestampScale scale);

// Converts a absl::Time value into an int64 timestamp for the given <scale>.
// Returns false if the value is outside the representable range.
ABSL_MUST_USE_RESULT bool FromTime(absl::Time base_time, TimestampScale scale,
                                   int64_t* output);

// Returns the absl::Weekday corresponding to `part`, which must be one of the
// WEEK values.
absl::StatusOr<absl::Weekday> GetFirstWeekDayOfWeek(DateTimestampPart part);

// Returns a string of format "YYYY-MM-DD" for this date.  Returns error
// status if conversion fails.
absl::Status ConvertDateToString(int32_t date, std::string* out);

// Returns an absl::CivilDay for this date, or an error status if conversion
// fails.
absl::StatusOr<absl::CivilDay> ConvertDateToCivilDay(int32_t date);

// Populates <out> in canonical timestamp format based on the specified
// <timezone>, for example:
//   "2014-01-31 12:22:34.123456789-08".
// The maximum number of fractional second digits produced is defined by
// <scale>, and the number of fractional digits is a multiple of three
// with trailing zeros truncated if necessary.
// Returns error status if conversion fails.
absl::Status ConvertTimestampToStringWithTruncation(int64_t timestamp,
                                                    TimestampScale scale,
                                                    absl::TimeZone timezone,
                                                    std::string* out);

absl::Status ConvertTimestampToStringWithTruncation(const PicoTime& timestamp,
                                                    absl::TimeZone timezone,
                                                    std::string* out);

// Optimized version of ConvertTimestampToStringWithTruncation for microsecond
// precision.
absl::Status ConvertTimestampMicrosToStringWithTruncation(
    int64_t timestamp, absl::TimeZone timezone, std::string* out);

// Invokes MakeTimeZone() on <timezone_string> and invokes the prior function.
// Returns error status if <timezone_string> is invalid or conversion fails.
absl::Status ConvertTimestampToStringWithTruncation(
    int64_t timestamp, TimestampScale scale, absl::string_view timezone_string,
    std::string* out);

// Populates <out> in canonical timestamp format based on the specified
// <timezone>, for example:
//   "2014-01-31 12:22:34.123456789-08".
// The number of fractional second digits produced is defined by <scale>.
// Returns error status if conversion fails.
// Note - these functions do not reflect GoogleSQL semantics for the TIMESTAMP
// type, but they are still being used for the legacy timestamp types.
// They are also useful for debug or test output since the number of
// subsecond digits accurately reflect the timestamp scale.
absl::Status ConvertTimestampToStringWithoutTruncation(int64_t timestamp,
                                                       TimestampScale scale,
                                                       absl::TimeZone timezone,
                                                       std::string* out);
// Invokes MakeTimeZone() on <timezone_string> and invokes the prior function.
// Returns error status if <timezone_string> is invalid or conversion fails.
absl::Status ConvertTimestampToStringWithoutTruncation(
    int64_t timestamp, TimestampScale scale, absl::string_view timezone_string,
    std::string* out);

absl::Status ConvertTimestampToStringWithoutTruncation(const PicoTime&,
                                                       absl::TimeZone timezone,
                                                       std::string* out);

// Populates a absl::Time timestamp into a string of canonical format. For
// example: "2014-01-31 12:22:34.123456789-08".
// The maximum number of fractional second digits produced is defined by
// <scale>, and the number of fractional digits is a multiple of three
// with trailing zeros truncated if necessary.
// Returns error status if conversion fails.
absl::Status ConvertTimestampToString(absl::Time input, TimestampScale scale,
                                      absl::TimeZone timezone,
                                      std::string* output);

absl::Status ConvertTimestampToString(absl::Time input, TimestampScale scale,
                                      absl::string_view timezone_string,
                                      std::string* output);

absl::Status ConvertTimestampToString(const PicoTime& input,
                                      absl::TimeZone timezone,
                                      std::string* output);

// Populates a Time into a string of canonical format.
// For example: "13:14:15.123456789" for nano precision.
// Use scale to control the maximum number of digits for sub-second. Only
// kMicroSeconds and kNanoSeconds are acceptable. Note that trailing 000's will
// be trimmed.
// Returns error status if conversion fails.
absl::Status ConvertTimeToString(TimeValue time, TimestampScale scale,
                                 std::string* out);

// Populates a Datetime into a string of canonical format.
// For example: "2014-01-31 13:14:15.123456789"
// Use scale to control the maximum number of digits for sub-second. Only
// kMicroSeconds and kNanoSeconds are acceptable. Note that trailing 000's will
// be trimmed.
// Returns error status if conversion fails.
absl::Status ConvertDatetimeToString(DatetimeValue datetime,
                                     TimestampScale scale, std::string* out);

// Populates <out> using the <format_str> as defined by absl::FormatTime()
// in base/time.h.  Assumes <timestamp> is the number of microseconds from
// 1970-01-01 UTC.
// Returns error status if conversion fails.
//
// The valid format elements are documented in the GoogleSQL function
// specification for the TIMESTAMP_FORMAT() function at:
//
// (broken link)
//
// Note that some format elements are locale-sensitive.  For GoogleSQL
// the locale is en-US.
absl::Status FormatTimestampToString(absl::string_view format_str,
                                     int64_t timestamp, absl::TimeZone timezone,
                                     std::string* out);

// Invokes MakeTimeZone() on <timezone_string> and invokes the prior function.
// Returns error status if <timezone_string> is invalid or conversion fails.
absl::Status FormatTimestampToString(absl::string_view format_str,
                                     int64_t timestamp,
                                     absl::string_view timezone_string,
                                     std::string* out);

absl::Status FormatTimestampToString(absl::string_view format_string,
                                     absl::Time timestamp,
                                     absl::TimeZone timezone, std::string* out);

absl::Status FormatTimestampToString(absl::string_view format_string,
                                     absl::Time timestamp,
                                     absl::string_view timezone_string,
                                     std::string* out);

absl::Status FormatTimestampToString(absl::string_view format_string,
                                     const PicoTime& timestamp,
                                     absl::TimeZone timezone, std::string* out);

// Populates <out> using the <format_string> as defined by absl::FormatTime()
// in base/time.h. Assumes <date> is the number of days from 1970-01-01.
//
// Returns error status if the conversion fails.
absl::Status FormatDateToString(absl::string_view format_string, int32_t date,
                                std::string* out);

// Options to enable smooth transition for supporting quarter and ISO day of
// year format qualifiers.
struct FormatDateTimestampOptions {
  bool expand_Q;  // Expand %Q, quarter. NOLINT
  bool expand_J;  // Expand %J, ISO day of year. NOLINT
};

// Functions to enable smooth transition for supporting quarter and ISO day of
// year format qualifiers. This function adds <format_options> to control
// whether %Q and/or %J should be expanded to quarter and/or ISO day of year
// respectively.
absl::Status FormatTimestampToString(
    absl::string_view format_str, absl::Time timestamp,
    absl::string_view timezone_string,
    const FormatDateTimestampOptions& format_options, std::string* out);

absl::Status FormatTimestampToString(
    absl::string_view format_str, absl::Time timestamp, absl::TimeZone timezone,
    const FormatDateTimestampOptions& format_options, std::string* out);

absl::Status FormatTimestampToString(
    absl::string_view format_str, int64_t timestamp,
    absl::string_view timezone_string,
    const FormatDateTimestampOptions& format_options, std::string* out);

absl::Status FormatTimestampToString(
    absl::string_view format_str, int64_t timestamp, absl::TimeZone timezone,
    const FormatDateTimestampOptions& format_options, std::string* out);

absl::Status FormatTimestampToString(
    absl::string_view format_string, const PicoTime& timestamp,
    absl::TimeZone timezone, const FormatDateTimestampOptions& format_options,
    std::string* out);

absl::Status FormatDatetimeToStringWithOptions(
    absl::string_view format_string, const DatetimeValue& datetime,
    const FormatDateTimestampOptions& format_options, std::string* out);

absl::Status FormatDateToString(
    absl::string_view format_string, int64_t date,
    const FormatDateTimestampOptions& format_options, std::string* out);

// Populates <out> using the <format_string> as defined by absl::FormatTime() in
// base/time.h. Returns error status if conversion fails.
//
// The valid format elements are the same as TIMESTAMP_FORMAT() defined in
// (broken link), except that any time zone related
// elements are not allowed.
absl::Status FormatDatetimeToString(absl::string_view format_string,
                                    const DatetimeValue& datetime,
                                    std::string* out);

// Populates <out> using the <format_string> as defined by absl::FormatTime() in
// base/time.h. Returns error status if conversion fails.
//
// The valid format elements are those defined in
// (broken link) that are only related to hours,
// minutes, seconds and subseconds.
absl::Status FormatTimeToString(absl::string_view format_string,
                                const TimeValue& time, std::string* out);

// Converts the string representation of a date to a date value.
// Supported format: "YYYY-[M]M-[D]D".
// Returns error status if conversion fails.
absl::Status ConvertStringToDate(absl::string_view str, int32_t* date);

// Converts a civil time to a date value, or an error status if conversion
// fails.
absl::StatusOr<int32_t> ConvertCivilDayToDate(absl::CivilDay civil_day);

// Converts the string representation of a timestamp to a timestamp integer
// value of the specified <scale>.  The currently supported format is
// the date format optionally followed by a time and/or time zone
// specified as either of:
//   'YYYY-[M]M-[D]D( |T)[[H]H:[M]M:[S]S[.DDDDDDDDD]][<canonical time zone>]'
//   'YYYY-[M]M-[D]D( |T)[[H]H:[M]M:[S]S[.DDDDDDDDD]][<space> <time zone name>]'
//
// If hours (range 0-23) are specified then minutes (range 0-59) and seconds
// (range 0-59) are required while fractional seconds (1-9 digits) are
// optional.
//
// For example:
//   '2014-01-31'
//   '2014-01-31 -08'
//   '2014-01-31 12:12:34-08'
//   '2014-01-31 12:12:34.1234567-08'
//   '2014-01-31 12:12:34.123456789+08'
//   '2014-01-31T12:12:34.123456789Z'
//   '2014-01-31 12:12:34.123456789'
//   '2014-01-31 12:12:34.123456789 America/Los_Angeles'
//
// If the time zone is not included in the string, then <default_timezone> is
// applied for the conversion.
//
// Returns error status if there are more fractional digits than <scale>, or
// conversion otherwise fails.
// DEPRECATED, prefer to use ConvertStringToTimestamp with allow_tz_in_str
// below.
absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale, int64_t* timestamp);

// If the time zone is not included in the string, then <default_timezone> is
// applied for the conversion.  Uses the canonical timestamp string format.
// If the time zone is included in the string and allow_tz_in_str is set to
// false, an error will be returned.
absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      bool allow_tz_in_str, int64_t* timestamp);

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      bool allow_tz_in_str, absl::Time* output);

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      bool allow_tz_in_str, PicoTime* output,
                                      int* precision = nullptr);

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      bool allow_tz_in_str, PicoTime* output,
                                      int* precision = nullptr);

// Converts the string representation of a time to a time value of the specified
// <scale>. Returns error status if there are more fractional digits than
// <scale>, or conversion otherwise fails.
// Supported format: "[H]H:[M]M:[S]S[.DDDDDDDDD]".
//
// Note that leap second (:60) is allowed as input, and it will be normalized to
// the first second of the next minute while the sub-second part will be cleared
// if there is any. Comparing to keeping the sub-second, clearing sub-second for
// leap second values:
// 1) introduces less inaccuracy;
// 2) preserves the relative ordering for inputs around the leap seconds.
// For example, for the three value around a leap second:
//   A = 01:02:59.333
//   B = 01:02:60.222
//   C = 01:02:60.444
//   D = 01:03:00.111
// The actual order is A < B < C < D. B and C will be normalized due to the leap
// second.
// If the sub-second part is kept, B' = 01:03:00.222 and C' = 01:03:00.444. The
// order becomes A < D < B' < C'.
// If the sub-second part is cleared, B" = C" = 01:03:00.000. The order becomes
// A < B" = C" < D. Although both B" and C" are collapsed on the same exact leap
// second, their relative order with D are preserved.
absl::Status ConvertStringToTime(absl::string_view str, TimestampScale scale,
                                 TimeValue* output);

// Converts the string representation of a datetime to a datetime value of the
// specified <scale>. Returns error status if there are more fractional digits
// than <scale>, or conversion otherwise fails.
// Supported format: "YYYY-[M]M-[D]D( |T)[[H]H:[M]M:[S]S[.DDDDDDDDD]]"
//
// Same as ConvertStringToTime, leap second (:60) is allowed as input, and it
// will be normalized to the first second of the next minute while the
// sub-second part will be cleared. See the comments on ConvertStringToTime for
// the reasoning of sub-second clearing for input with leap second.
absl::Status ConvertStringToDatetime(absl::string_view str,
                                     TimestampScale scale,
                                     DatetimeValue* output);

// Invokes MakeTimeZone() on <default_timezone_string> and invokes the prior
// function.  Returns error status if <default_timezone_string> is invalid
// or conversion fails.
// DEPRECATED, prefer to use ConvertStringToTimestamp with allow_tz_in_str
// below.
// Only used in compliance tests.
absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::string_view default_timezone_string,
                                      TimestampScale scale, int64_t* timestamp);

// If the time zone is not included in the string, then
// <default_timezone_string> is applied for the conversion.  Uses the
// canonical timestamp string format.
// If the time zone is included in the string and allow_tz_in_str is set to
// false, an error will be returned.
absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::string_view default_timezone_string,
                                      TimestampScale scale,
                                      bool allow_tz_in_str, int64_t* timestamp);

// Populate the <output> with the values from input if the values on all fields
// are valid, return error otherwise.
absl::Status ConstructDate(int year, int month, int day, int32_t* output);

// Populate <output> with the values from the input if the values on all fields
// are valid, return error otherwise. Note that 60 is considered valid for
// <second>, but the result <output> will be the first second of the next
// minute.
absl::Status ConstructTime(int hour, int minute, int second, TimeValue* output);

// Populate the <output> with the values from the input if the values on all
// fields are valid, return error otherwise. Note that 60 is considered valid
// for <second>, but the result <output> will be the first second of the next
// minute (which could make the final <output> invalid if it goes out-of-range).
absl::Status ConstructDatetime(int year, int month, int day, int hour,
                               int minute, int second, DatetimeValue* output);

// Populate the <output> with the values from the input if the values on all
// fields are valid, return error otherwise.
absl::Status ConstructDatetime(int32_t date, const TimeValue& time,
                               DatetimeValue* output);

// Extracts a DateTimestampPart from the given date
// Returns error status if the input date value is invalid, or if the
// requested DateTimestampPart is not applicable to Date.
absl::Status ExtractFromDate(DateTimestampPart part, int32_t date,
                             int32_t* output);

// Extracts a DateTimestampPart from the given timestamp as of the specified
// <timezone_string>.  Returns error status if the input timestamp or timezone
// is invalid.  Extracting the DATE part from a timestamp effectively converts
// a timestamp to a date.
ABSL_DEPRECATED("int32_t* output type is deprecated; use int64_t* instead")
// TODO: Remove with int32_t output type dependencies.
absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale, absl::TimeZone timezone,
                                  int32_t* output);

absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale, absl::TimeZone timezone,
                                  int64_t* output);

ABSL_DEPRECATED("int32_t* output type is deprecated; use int64_t* instead")
// TODO: Remove with int32_t output type dependencies.
absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale,
                                  absl::string_view timezone_string,
                                  int32_t* output);

absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale,
                                  absl::string_view timezone_string,
                                  int64_t* output);

ABSL_DEPRECATED("int32_t* output type is deprecated; use int64_t* instead")
// TODO: Remove with int32_t output type dependencies.
absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::TimeZone timezone, int32_t* output);

absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::TimeZone timezone, int64_t* output);

ABSL_DEPRECATED("int32_t* output type is deprecated; use int64_t* instead")
// TODO: Remove with int32_t output type dependencies.
absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::string_view timezone_string,
                                  int32_t* output);

absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::string_view timezone_string,
                                  int64_t* output);

absl::StatusOr<int64_t> ExtractFromTimestamp(DateTimestampPart part,
                                             const PicoTime& timestamp,
                                             absl::string_view timezone_string);

absl::StatusOr<int64_t> ExtractFromTimestamp(DateTimestampPart part,
                                             const PicoTime& timestamp,
                                             absl::TimeZone timezone);

// Extracts a DateTimestampPart from the given TIME value. Returns error status
// if the input TIME value is invalid.
ABSL_DEPRECATED("int32_t* output type is deprecated; use int64_t* instead")
// TODO: b/412458859 - Remove with int32_t output type dependencies.
absl::Status ExtractFromTime(DateTimestampPart part, const TimeValue& time,
                             int32_t* output);
absl::Status ExtractFromTime(DateTimestampPart part, const TimeValue& time,
                             int64_t* output);

// TODO: b/412458859 - Remove with int32_t output type dependencies.
inline absl::Status ExtractFromTime32(DateTimestampPart part,
                                      const TimeValue& time, int32_t* output) {
  return ExtractFromTime(part, time, output);
}

// Extracts a DateTimestampPart from the given DATETIME value. Returns error
// status if the input DATETIME value is invalid. Extracting the DATE part from
// a DATETIME value effectively converts a DATETIME to a date.
ABSL_DEPRECATED("int32_t* output type is deprecated; use int64_t* instead")
// TODO: b/412458859 - Remove with int32_t output type dependencies.
absl::Status ExtractFromDatetime(DateTimestampPart part,
                                 const DatetimeValue& datetime,
                                 int32_t* output);
absl::Status ExtractFromDatetime(DateTimestampPart part,
                                 const DatetimeValue& datetime,
                                 int64_t* output);

// TODO: b/412458859 - Remove with int32_t output type dependencies.
inline absl::Status ExtractFromDatetime32(DateTimestampPart part,
                                          const DatetimeValue& datetime,
                                          int32_t* output) {
  return ExtractFromDatetime(part, datetime, output);
}

// Extracts a TIME from the given DATETIME value. Returns error
// status if the input DATETIME value is invalid.
absl::Status ExtractTimeFromDatetime(const DatetimeValue& datetime,
                                     TimeValue* time);

// Convert a Datetime value to a Timestamp value with the specified timezone.
// Return error status:
// 1. if the input Datetime value is invalid, or
// 2. if the timezone is invalid, or
// 2. if the resulting output Timestamp value is out-of-range.
absl::Status ConvertDatetimeToTimestamp(const DatetimeValue& datetime,
                                        absl::TimeZone timezone,
                                        absl::Time* output);
absl::Status ConvertDatetimeToTimestamp(const DatetimeValue& datetime,
                                        absl::string_view timezone_string,
                                        absl::Time* output);

// Convert a Timestamp value to a Datetime value with the specified timezone.
// Returns error status:
// 1. if the input Timestamp value is invalid, or
// 2. if the timezone is invalid, or
// 2. if the resulting output Datetime value is out-of-range.
absl::Status ConvertTimestampToDatetime(absl::Time base_time,
                                        absl::TimeZone timezone,
                                        DatetimeValue* output);
absl::Status ConvertTimestampToDatetime(absl::Time base_time,
                                        absl::string_view timezone_string,
                                        DatetimeValue* output);

// Convert a Timestamp value to a Time value with the specified timezone.
// Returns error status if the input timestamp or timezone is invalid.
// Deprecated, use the version with an extra <scale> argument below.
absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::TimeZone timezone, TimeValue* output);
absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::TimeZone timezone,
                                    TimestampScale scale, TimeValue* output);
// Deprecated, use the version with an extra <scale> argument below.
absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::string_view timezone_string,
                                    TimeValue* output);
absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::string_view timezone_string,
                                    TimestampScale scale, TimeValue* output);

// Converts a Date value to a Timestamp value with the specified timezone.
// Returns error status:
// 1. if the input date value is invalid.
// 2. if the input date value is invalid or if the resulting
// <output> timestamp value does not fit into an int64 (when <scale> is
// nanoseconds).
absl::Status ConvertDateToTimestamp(int32_t date, TimestampScale scale,
                                    absl::TimeZone timezone, int64_t* output);

absl::Status ConvertDateToTimestamp(int32_t date, TimestampScale scale,
                                    absl::string_view timezone_string,
                                    int64_t* output);

// These 2 functions below have same contract as above except the output is
// always valid if the input date is valid.
absl::Status ConvertDateToTimestamp(int32_t date, absl::TimeZone timezone,
                                    absl::Time* output);

absl::Status ConvertDateToTimestamp(int32_t date,
                                    absl::string_view timezone_string,
                                    absl::Time* output);

// Converts a Timestamp value to another Timestamp type value.
// Returns error status if the input timestamp value is invalid.
absl::Status ConvertBetweenTimestamps(int64_t input_timestamp,
                                      TimestampScale input_scale,
                                      TimestampScale output_scale,
                                      int64_t* output);

// Converts a Proto3 Timestamp type (google/protobuf/timestamp.proto) to a
// Timestamp value. The Proto3 Timestamp type is represented with
// nanoseconds precision. Therefore precision loss, in the form of truncation
// towards the past, may occur when converting to the <output_scale>. Returns
// error status if the input timestamp value is invalid.
absl::Status ConvertProto3TimestampToTimestamp(
    const google::protobuf::Timestamp& input_timestamp,
    TimestampScale output_scale, int64_t* output);
absl::Status ConvertProto3TimestampToTimestamp(
    const google::protobuf::Timestamp& input_timestamp, absl::Time* output);

// Converts a Timestamp value to a Proto3 Timestamp type. Returns error status
// if the input timestamp value is invalid.
absl::Status ConvertTimestampToProto3Timestamp(
    int64_t input_timestamp, TimestampScale scale,
    google::protobuf::Timestamp* output);
absl::Status ConvertTimestampToProto3Timestamp(
    absl::Time input_timestamp, google::protobuf::Timestamp* output);

// Converts a Proto3 Date type (google/type/date.proto) to a Date value. The
// Proto3 Date type supports year and day values of zero, which are not
// currently supported by GoogleSQL Date type. An error status will be returned
// in these cases. An error status is also returned if the input date value is
// invalid.
absl::Status ConvertProto3DateToDate(const google::type::Date& input,
                                     int32_t* output);

// Converts a Date value to a Proto3 Date type (google/type/date.proto). Returns
// error status if the input date value is invalid.
absl::Status ConvertDateToProto3Date(int32_t input, google::type::Date* output);

// Add an interval to a part of the given date value.
// Returns error status if the input date value is invalid (out of range), or
// the output date value is invalid (out of range), or the DateTimestampPart
// does not apply to a Date type.
// Allowed DateTimestampPart is YEAR, MONTH, QUARTER, WEEK or DAY.
// A negative interval means a subtraction instead.
absl::Status AddDate(int32_t date, DateTimestampPart part, int64_t interval,
                     int32_t* output);

// Similar to AddDate, but it doesn't create an error when overflow occurs.
absl::Status AddDateOverflow(int32_t date, DateTimestampPart part,
                             int32_t interval, int32_t* output,
                             bool* had_overflow);

// DATE + INTERVAL = DATETIME
absl::Status AddDate(int32_t date, IntervalValue interval,
                     DatetimeValue* output);

// Same as above, but subtracts the interval from a part of the date value.
absl::Status SubDate(int32_t date, DateTimestampPart part, int64_t interval,
                     int32_t* output);

// Returns the diff (signed integer) of the specified DateTimestampPart
// between a given date pair, based on date1 - date2.
// The result DateTimetampPart value of the input dates is not rounded by
// calendar interval, but simply the difference of part values extracted from
// the dates.
// Some examples:
//   DiffDates("2001-12-31", "2001-01-01", YEAR) --> 0
//   DiffDates("2001-01-01", "2000-12-31", YEAR) --> 1
//   DiffDates("2001-02-28", "2001-02-01", MONTH) --> 0
//   DiffDates("2001-02-28", "2000-02-01", MONTH) --> 12
//   DiffDates("2001-02-01", "2000-02-28", MONTH) --> 12
//   DiffDates("2001-02-01", "2001-01-31", MONTH) --> 1
//   DiffDates("2001-02-01", "2002-01-31", MONTH) --> -11
// Returns error status if either of the input date values is invalid (out of
// range), or the DateTimestampPart does not apply to a Date type.
// DateTimestampPart can only be one of YEAR, MONTH, QUARTER, WEEK, WEEK_*, and
// DAY.
absl::Status DiffDates(int32_t date1, int32_t date2, DateTimestampPart part,
                       int32_t* output);

// Interval difference between dates: date1 - date2
absl::StatusOr<IntervalValue> IntervalDiffDates(int32_t date1, int32_t date2);

// Add an interval to a part of the given datetime value.
// Returns error status if the input datetime value is invalid (out of range),
// or the output datetime value is invalid (out of range), or the
// DateTimestampPart does not apply to a Datetime type.
// Allowed DateTimestampPart is YEAR, QUARTER, MONTH, WEEK, DAY, HOUR, MINUTE,
// SECOND, MILLISECOND, MICROSECOND or NANOSECOND.
// A negative interval means a subtraction instead.
absl::Status AddDatetime(const DatetimeValue& datetime, DateTimestampPart part,
                         int64_t interval, DatetimeValue* output);

// Same as above, but subtracts the interval from a part of the datetime value.
// Prefer this over invoking AddDatetime with a negated interval in order to
// avoid overflow.
absl::Status SubDatetime(const DatetimeValue& datetime, DateTimestampPart part,
                         int64_t interval, DatetimeValue* output);

// DATETIME + INTERVAL
absl::Status AddDatetime(DatetimeValue datetime, IntervalValue interval,
                         DatetimeValue* output);

// Returns the diff (signed integer) of the specified DateTimestampPart
// between a given datetime pair, based on datetime1 - datetime2.
// The result DateTimetampPart value of the input datetimes is not rounded by
// clock/calendar interval, but simply the difference of part values extracted
// from the datetimes.
// Some examples:
//   DiffDatetimes("2001-12-31", "2001-01-01", YEAR) --> 0
//   DiffDatetimes("2001-01-01", "2000-12-31", YEAR) --> 1
//   DiffDatetimes("2001-02-28", "2001-02-01", MONTH) --> 0
//   DiffDatetimes("2001-02-01", "2001-01-31", MONTH) --> 1
//   DiffDatetimes("2001-02-01 00:59:59", "2001-02-01 00:00:00", HOUR) --> 0
//   DiffDatetimes("2001-02-01 01:00:00", "2001-02-01 00:59:59", HOUR) --> 1
//   DiffDatetimes("2001-02-02 01:00:00", "2001-02-01 00:59:59", HOUR) --> 25
// Returns error status if either of the input datetime values is invalid (out
// of range), or the DateTimestampPart does not apply to a Datetime type, or the
// result overflows an int64 type (e.g., the difference in nanos between max and
// min nano datetime).
// <part> can only be one of YEAR, QUARTER, MONTH, WEEK, WEEK_*, DAY, HOUR,
// MINUTE, SECOND, MILLISECOND, MICROSECOND and NANOSECOND.
absl::Status DiffDatetimes(const DatetimeValue& datetime1,
                           const DatetimeValue& datetime2,
                           DateTimestampPart part, int64_t* output);

// Interval difference between datetimes: datetime1 - datetime2
absl::StatusOr<IntervalValue> IntervalDiffDatetimes(
    const DatetimeValue& datetime1, const DatetimeValue& datetime2);

// Truncates the datetime to the beginning of the specified DateTimestampPart
// granularity.
//
// For example:
//   TruncateDatetime("2016-11-02 12:34:56.456789", YEAR) --> "2016-01-01"
//   TruncateDatetime("2016-11-02 12:34:56.456789", QUARTER) --> "2016-10-01"
//   TruncateDatetime("2016-11-02 12:34:56.456789", DAY) --> "2016-11-02"
//   TruncateDatetime("2016-11-02 12:34:56.456789", MINUTE)
//     --> "2016-11-02 12:34:00"
//   TruncateDatetime("2016-11-02 12:34:56.456789", MILLISECOND)
//     --> "2016-11-02 12:34:56.456"
// Specially, truncating to WEEK returns the beginning of the last Sunday that's
// not later than the datetime.
//   TruncateDatetime("2016-04-20 12:34:56.456789", WEEK) --> "2016-04-17"
// Truncating to WEEK_<WEEKDAY> is similar, returning the beginning of the
// last <WEEKDAY> that's not later than the datetime.
//   TruncateDatetime("2016-04-20 12:34:56.456789", WEEK_MONDAY)
//     --> "2016-04-18"
//
// Allowed DateTimestampParts are YEAR, QUARTER, MONTH, WEEK, WEEK_<WEEKDAY>,
// DAY, HOUR, MINUTE, SECOND, MILLISECOND, MICROSECOND or NANOSECOND.
// Returns error if the datetime is invalid (out of range), or the
// DateTimestampPart is not allowed, or the result is not a valid datetime value
// (e.g. when truncate "0001-01-01 12:34:56" (a Saturday) to WEEK, which suppose
// to return the Sunday before it).
absl::Status TruncateDatetime(const DatetimeValue& datetime,
                              DateTimestampPart part, DatetimeValue* output);

// Returns the last day of the specified date part that contains the date.
// Commonly used to return the last day of the month.
// part is the date part for which the last day is returned.
// Possible values are YEAR, QUARTER, MONTH, WEEK or WEEK(MONDAY/SUNDAY etc..),
// ISO_WEEK and ISO_YEAR.
// When date_part is WEEK, users can use WEEK(<WEEKDAY>) to specify the starting
// date of the week. For example, WEEK(MONDAY) means a week starts on MONDAY.
// The rule is the same as the current GoogleSQL date_part.
// For example:
//  LastDayOfDate("2001-01-01", YEAR) -> "2001-12-31"
//  LastDayOfDate("2001-01-01", ISOYEAR) -> "2001-12-30"
//  LastDayOfDate("2001-01-01", QUARTER) -> "2001-03-31"
//  LastDayOfDate("2001-12-31", WEEK) -> "2002-01-05"
//  LastDayOfDate("2001-12-31", ISOWEEK) -> "2002-01-06"
//  LastDayOfDate("2001-12-31", WEEK_FRIDAY) -> "2002-01-03"
absl::Status LastDayOfDate(int32_t date, DateTimestampPart part,
                           int32_t* output);

absl::Status LastDayOfDatetime(const DatetimeValue& datetime,
                               DateTimestampPart part, int32_t* output);

// Returns the DATE of the next day of the specified DATE or DATETIME.
//
// `dow` is the case-insensitive day of the week, or its three-letter
// abbreviation, such as "Monday", "Mon", etc.
//
// If the date is already on the specified day of the week, then the result is
// the date plus seven days.
//
//   NextDayOfDate('2025-09-30', 'Thursday') -> '2025-10-02'
//   NextDayOfDate('2025-09-30', 'Tue') -> '2025-10-07'
absl::Status NextDayOfDate(int32_t date, absl::string_view dow,
                           int32_t* output);
absl::Status NextDayOfDatetime(const DatetimeValue& datetime,
                               absl::string_view dow, int32_t* output);

// Adds or subtracts a specified number of months to a DATE or DATETIME.
//
// If the input is the last day of the month or if the resulting month has fewer
// days than the input's day component, then the result is the last day of the
// new month. Otherwise, the result day has the same day component as the input.
//   AddMonths('2025-01-31', 1) -> '2025-02-28'  -- result month has fewer days
//   AddMonths('2025-02-28', 1) -> '2025-03-31'  -- retain end-of-month info
//   AddMonths('2025-03-15', -2) -> '2025-01-15' -- can go backwards
//
// The time component of the resulting day is the same as the input when input
// is DATETIME.
//  AddMonths('2025-02-28 23:59:59', 1) -> '2025-03-31 23:59:59'
//
// This differs from AddDate (or date arithmetic) in that it retains
// end-of-month information. For example, compare:
//   AddDate('2025-02-28', MONTH, 1) -> '2025-03-28'
//   AddMonths('2025-02-28', 1) -> '2025-03-31'
absl::Status AddMonths(int32_t date, int64_t months, int32_t* output);
absl::Status AddMonths(const DatetimeValue& datetime, int64_t months,
                       DatetimeValue* output);

// Calculate an estimate of the months between two dates or datetimes,
// represented as a floating point number. (See (broken link) for
// more details.)
//
// At a high level, this computes (date_end - date_start) and returns a
// floating-point number representing the whole month difference and a
// fractional portion based on a 31-day month.
//
// The result is calculated from two components:
// 1. A whole-month portion, computed from the year and month values only
// 2. A fractional portion, computed from the day and time values only and
//    based on a 31-day month
//    - Special case: if the days are the same, or are both the ends of their
//      respective months, this fractional portion is zero
// These are added together to form the resulting value.
//
// Examples:
//   MonthsBetween('2025-11-14', '2025-10-13') -> 1 + 1/31 = 1.032258
//
//   MonthsBetween('2025-11-14', '2025-10-16') -> 1 - 2/31 = 0.935483
//   MonthsBetween('2025-10-16', '2025-11-14') -> -1 + 2/31 = -0.935483
//
//   MonthsBetween('2025-11-14', '2025-10-14') -> 1
//   MonthsBetween('2025-11-30', '2025-10-31') -> 1
//
//   MonthsBetween('2008-02-29 00:00:00.000',
//                 '2008-02-28 12:00:00.000')  -> 0 + 0.5/31 = 0.016129
absl::Status MonthsBetween(int32_t date_end, int32_t date_start,
                           double* output);
absl::Status MonthsBetween(const DatetimeValue& datetime_end,
                           const DatetimeValue& datetime_start, double* output);

// Returns the diff (signed integer) of the specified DateTimestampPart
// between a given timestamp pair, based on timestamp1 - timestamp2.
// Returns the number of whole <part> differences between the two
// timestamps (i.e., the number of whole hours, seconds, etc.).
// Returns error status if either the specified date part is unsupported
// (YEAR, DAY, etc.), or the result overflows an int64 type (i.e., the
// difference in nanos between max and min nano timestamps).  Does not
// range check the arguments.
absl::Status TimestampDiff(int64_t timestamp1, int64_t timestamp2,
                           TimestampScale scale, DateTimestampPart part,
                           int64_t* output);

// This function has exact same contract as the function above except it takes 2
// absl::Time as input.
absl::Status TimestampDiff(absl::Time timestamp1, absl::Time timestamp2,
                           DateTimestampPart part, int64_t* output);

absl::StatusOr<absl::int128> TimestampDiff(const PicoTime& pico_time1,
                                           const PicoTime& pico_time2,
                                           DateTimestampPart part);

// Interval difference between timestamps: timestamp1 - timestamp2
absl::StatusOr<IntervalValue> IntervalDiffTimestamps(absl::Time timestamp1,
                                                     absl::Time timestamp2);

// Add an interval to a part of the given Timestamp value.
// Returns error status if the input Timestamp value is invalid (out of range),
// or the interval is invalid (out of range on the DateTimestampPart),
// or the output Timestamp value is invalid (out of range), or the
// DateTimestampPart does not apply to a Timestamp type.
// Allowed DateTimestampPart is DAY, HOUR, MINUTE, SECOND, MILLISECOND,
// MICROSECOND or NANOSECOND.
// DATE, DAYOFWEEK and DAYOFYEAR are not allowed, nor are WEEK, MONTH,
// QUARTER, or YEAR.
// A negative interval means a subtraction instead.
//
// Valid interval ranges are:
// DAY:     [-213300, 213300]            213300 = kDateMax - kDateMin
// HOUR:    [-5119223, 5119223]         5119223 = 213301 * 24 - 1
// MINUTE:  [-307153439, 307153439]   307153439 = 5119224 * 60 - 1
// SECOND:  [-18429206399, 18429206399]
//          18429206399 = kTimestampSecondsMax - kTimestampSecondsMin
// MILLISECOND:
//          [-18429206399999, 18429206399999]
//          18429206399999 = kTimestampMillisMax - kTimestampMillisMin
// MICROSECOND:
//          [-18429206399999999, 18429206399999999]
//          18429206399999999 = kTimestampMicrosMax - kTimestampMicrosMin
// NANOSECOND:
//          [int64min, int64max], because
//          kTimestampNanosMin + kint64max < kTimestampNanosMax
//          kTimestampNanosMax + kint64min > kTimestampNanosMin
//
// Adding a valid non-NANOS interval to an non-NANOS Timestamp value can only
// guarantee there is no arithmetic int64 overflow. But the result timestamp
// may still be invalid (out of range).
// Adding a NANOSECOND interval to a TIMESTAMP_NANOS value may cause int64
// arithmetic overflow and it will be reported as an error as well.
//
// Note - the timezone is irrelevant for the computations, and is only used
// for error messaging.
absl::Status AddTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::TimeZone timezone, DateTimestampPart part,
                          int64_t interval, int64_t* output);

absl::Status AddTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          int64_t* output);

absl::StatusOr<PicoTime> AddTimestamp(const PicoTime& pico_time,
                                      DateTimestampPart part,
                                      absl::int128 interval);

// The 2 functions below take absl::Time as input parameter and output
// parameter. We don't need TimestampScale as input parameter.
// The differences between these 2 functions and AddTimestamp() above are
// the following:
// Adding an interval of granularity smaller than a day will not cause
// arithmetic overflow. But it still can cause out of range.
absl::Status AddTimestamp(absl::Time timestamp, absl::TimeZone timezone,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output);

absl::Status AddTimestamp(absl::Time timestamp,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output);

// TIMESTAMP + INTERVAL
absl::Status AddTimestamp(absl::Time timestamp, absl::TimeZone timezone,
                          IntervalValue interval, absl::Time* output);

// TIMESTAMP + INTERVAL
absl::StatusOr<PicoTime> AddTimestamp(PicoTime timestamp,
                                      IntervalValue interval);

// Similar to AddTimestamp() above, but it doesn't return an error when overflow
// occurs.
absl::Status AddTimestampOverflow(absl::Time timestamp, absl::TimeZone timezone,
                                  DateTimestampPart part, int64_t interval,
                                  absl::Time* output, bool* had_overflow);

// Same as above, but subtracts an interval from part of the given Timestamp
// value. Prefer these over invoking AddTimestamp with a negated interval in
// order to avoid overflow.
absl::Status SubTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::TimeZone timezone, DateTimestampPart part,
                          int64_t interval, int64_t* output);

absl::Status SubTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          int64_t* output);

absl::StatusOr<PicoTime> SubTimestamp(const PicoTime& pico_time,
                                      DateTimestampPart part,
                                      absl::int128 interval);

// Same as the 2 function above except these 2 functions below take absl::Time
// as input parameter and output parameter. We don't need TimestampScale as
// input parameter.
absl::Status SubTimestamp(absl::Time timestamp, absl::TimeZone timezone,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output);

absl::Status SubTimestamp(absl::Time timestamp,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output);

// Add an interval to a part of the given time value.
// Returns error status if the input time value is invalid (out of range),
// or the DateTimestampPart does not apply to a Time type.
// Note that if addition of the interval results in a TimeValue outside of the
// [00:00:00, 24:00:00) range, the result will be wrapped around to stay within
// 24 hours. For example, adding 2 seconds to 23:59:59 will get 00:00:01.
// Allowed DateTimestampPart is HOUR, MINUTE, SECOND, MILLISECOND, MICROSECOND
// or NANOSECOND.
// A negative interval means a subtraction instead.
absl::Status AddTime(const TimeValue& time, DateTimestampPart part,
                     int64_t interval, TimeValue* output);

// Same as above, but subtracts the interval from a part of the time value.
// Prefer these over invoking AddTime with a negated interval in order to
// avoid overflow.
absl::Status SubTime(const TimeValue& time, DateTimestampPart part,
                     int64_t interval, TimeValue* output);

// Returns the diff (signed integer) of the specified DateTimestampPart
// between a given time pair, based on time1 - time2.
// The result DateTimestampPart value of the input times is not rounded by
// clock interval, but simply the difference of part values extracted from the
// time.
// Some examples:
//   DiffTimes("00:59:59", "00:00:00", HOUR) --> 0
//   DiffTimes("01:00:00", "00:59:59", HOUR) --> 1
//   DiffTimes("00:59:59", "01:00:00", HOUR) --> -1
//   DiffTimes("00:59:59", "00:00:00", MINUTE) --> 59
//   DiffTimes("01:59:59", "00:00:00", MINUTE) --> 119
// Returns error status if either of the input time values is invalid (out of
// range), or the DateTimestampPart does not apply to a Time type.
// <part> can only be one of HOUR, MINUTE, SECOND, MILLISECOND,
// MICROSECOND and NANOSECOND.
absl::Status DiffTimes(const TimeValue& time1, const TimeValue& time2,
                       DateTimestampPart part, int64_t* output);

// Interval difference between times: time1 - time2
absl::StatusOr<IntervalValue> IntervalDiffTimes(const TimeValue& time1,
                                                const TimeValue& time2);

// Truncates the time to the beginning of the specified DateTimestampPart
// granularity.
// For example:
//   TruncateTime("12:34:56.987654", HOUR) --> "12:00:00"
//   TruncateTime("12:34:56.987654", SECOND) --> "12:34:56"
//   TruncateTime("12:34:56.987654", MILLISECOND) --> "12:34:56.987"
// Allowed DateTimestampPart is HOUR, MINUTE, SECOND, MILLISECOND, MICROSECOND
// and NANOSECOND.
// Returns error if the time is invalid (out of range), or the DateTimestampPart
// is not allowed.
absl::Status TruncateTime(const TimeValue& time, DateTimestampPart part,
                          TimeValue* output);

// Truncates the date to the beginning of the specified DateTimestampPart
// granularity.
// For example: TruncateDate("2013-12-03", YEAR) --> "2013-01-01".
// Allowed DateTimestampParts are YEAR, QUARTER, MONTH, WEEK, WEEK_<WEEKDAY>,
// and DAY.
// Returns error if the date is invalid (out of range), or the DateTimestampPart
// is not allowed.
absl::Status TruncateDate(int32_t date, DateTimestampPart part,
                          int32_t* output);

// Truncates the timestamp to the beginning of the specified DateTimestampPart
// granularity.  Assumes that the input <timestamp> is at MICROSECOND precision.
// For example: TIMESTAMP_TRUNC("2013-12-03 12:34:56.987654", SECOND)
//                  --> "2013-12-03 12:34:56"
// Allowed DateTimestampParts are YEAR, QUARTER, MONTH, WEEK, WEEK_<WEEKDAY>,
// DAY, HOUR, MINUTE, SECOND, MILLISECOND, or MICROSECOND (a no-op).
// Returns an error if the timestamp is invalid (out of range), or the
// DateTimestampPart is not allowed (NANOSECOND, DAYOFWEEK, etc.).
//
// This function is independent of time zone for date parts SECOND,
// MILLISECOND, and MICROSECOND.  In other words, doing truncation to these
// parts for any time zone produces the same TIMESTAMP result.
//
// The function result is sensitive to time zone for other supported date
// parts (YEAR to MINUTE).  For those date parts, the timestamp is
// interpreted as of the specified <timezone> and truncation occurs to the
// <part> in that <timezone>.
absl::Status TimestampTrunc(int64_t timestamp, absl::TimeZone timezone,
                            DateTimestampPart part, int64_t* output);
absl::Status TimestampTrunc(int64_t timestamp,
                            absl::string_view timezone_string,
                            DateTimestampPart part, int64_t* output);

absl::StatusOr<PicoTime> TimestampTrunc(const PicoTime& timestamp,
                                        absl::TimeZone timezone,
                                        DateTimestampPart part);

// The 2 functions below have same contract as the 2 above except the
// following:
// 1. It takes absl::Time as input and output.
// 2. It is up to NANOSECOND precision so truncate a absl::Time at NONASECOND is
// not a noop any more since absl::Time could go up to 1/4 nano second.
//
// It does not share the implementation with the 2 above since we don't know if
// this will introduce performance regression.
absl::Status TimestampTrunc(absl::Time timestamp, absl::TimeZone timezone,
                            DateTimestampPart part, absl::Time* output);
absl::Status TimestampTrunc(absl::Time timestamp,
                            absl::string_view timezone_string,
                            DateTimestampPart part, absl::Time* output);

// Truncates the timestamp to the beginning of the specified DateTimestampPart
// granularity.
// For example: TruncateTimestamp("2013-12-03 12:34:56", MINUTE)
//                  --> "2013-12-03 12:34:00"
// Allowed DateTimestampPart is YEAR, QUARTER, MONTH, WEEK, DAY, HOUR, MINUTE,
// SECOND, MILLISECOND, MICROSECOND or NANOSECOND.
// Returns error if the timestamp is invalid (out of range), or the
// DateTimestampPart is not allowed, or the DateTimestampPart does not apply to
// the Timestamp type. (For example, truncating from TIMESTAMP_SECOND to
// MICROSECOND is not supported.)
// DEPRECATED.  Should only be used for legacy timestamp types, as the
// generated error messages are specific to those timestamp types.
// TIMESTAMP_TRUNC() on the new TIMESTAMP type should use TimestampTrunc()
// above instead.
absl::Status TruncateTimestamp(int64_t timestamp, TimestampScale scale,
                               absl::TimeZone timezone, DateTimestampPart part,
                               int64_t* output);
absl::Status TruncateTimestamp(int64_t timestamp, TimestampScale scale,
                               absl::string_view timezone_string,
                               DateTimestampPart part, int64_t* output);

// Returns the name of the specified <scale>.
std::string TimestampScale_Name(TimestampScale scale);

// Returns the enum value of a DateTimestampPart
// Returns -1 if the name is not a valid DateTimestampPart enum name.
int DateTimestampPart_FromName(absl::string_view name);

// Decode a date value formatted as <format> into the default googlesql
// DATE format.  <*output_is_null> will be set to true if this encoded value
// should decode as NULL.  (This happens for DATE_DECIMAL format for value 0.)
// Return an error if the format is unsupported or the encoded value is invalid.
absl::Status DecodeFormattedDate(int64_t input_formatted_date,
                                 FieldFormat::Format format,
                                 int32_t* output_date, bool* output_is_null);

// Encode a googlesql DATE value using <format>.
// Return an error if the format is unsupported or the input value cannot
// encoded into that format.
absl::Status EncodeFormattedDate(int32_t input_date, FieldFormat::Format format,
                                 int32_t* output_formatted_date);

// Returns current date as of the specified timezone.
ABSL_MUST_USE_RESULT int32_t CurrentDate(absl::TimeZone timezone);

// Converts <timezone_string> to absl::TimeZone and invokes previous
// function.  If <timezone_string> is not a valid TimeZone, returns
// an error.
absl::Status CurrentDate(absl::string_view timezone_string, int32_t* date);

// Returns the current timestamp as the number of microseconds from epoch.
ABSL_MUST_USE_RESULT int64_t CurrentTimestamp();

// Given a timestamp as a absl::Time and an initial scale, returns the narrowest
// scale that can still accurately represent the timestamp.
// TODO: Change the signature to return the result as the return
// value.
void NarrowTimestampScaleIfPossible(absl::Time time, TimestampScale* scale);

TimestampScale NarrowTimestampScaleIfPossible(PicoTime time);

inline bool IntervalUnaryMinus(IntervalValue in, IntervalValue* out,
                               absl::Status* error) {
  *out = -in;
  return true;
}

// "1950-01-01 00:00:00" is the default origin for time bucketing functions.
static constexpr absl::CivilSecond kDefaultTimeBucketOrigin =
    absl::CivilSecond(1950, 1, 1, 0, 0, 0);

// Assigns <input> timestamp to a specific bucket using the specified
// <bucket_width> INTERVAL and returns the bucket start timestamp.
//
// This function only supports nanoseconds/milliseconds and days encoded parts.
// (Months part is not supported). Days part is equivalent to 24 hours.
//
// <origin> controls the alignment of the buckets. It can be less than, equal to
// or greater than <input>.
//
// <timezone> is only used for string formatting in error messages.
//
// For example: TIMESTAMP_BUCKET(
//                  TIMESTAMP "2022-01-19 13:38:30 UTC",
//                  INTERVAL 3 HOUR,
//                  TIMESTAMP "1950-01-01 00:00:00 UTC")
//                  --> "2022-01-19 12:00:00 UTC"
//
// In the above example:
// - <bucket_width> INTERVAL 3 HOUR indicates that the function divides
// - timestamp values into 3-hour buckets
// - <origin> "1950-01-01 00:00:00" indicates that starting from this timestamp
//   we divide the range of all timestamp values (forwards and backwards) into
//   <bucket_width> subranges.
// - <input> "2022-01-19 13:38:30" gets assgigned to bucket
//   ["2022-01-19 12:00:00", "2022-01-19 15:00:00") and the start of the bucket
//   ("2022-01-19 12:00:00") is returned.
//
ABSL_DEPRECATED("Use the overload with pico precision instead.")
absl::Status TimestampBucket(absl::Time input, IntervalValue bucket_width,
                             absl::Time origin, absl::TimeZone timezone,
                             TimestampScale scale, absl::Time* output);

// This class allows for more efficient implementation of TIMESTAMP_BUCKET
// function when only the first argument (input timestamp) is non-const.
class TimestampBucketizer {
 public:
  TimestampBucketizer(const TimestampBucketizer& other) = default;
  TimestampBucketizer& operator=(const TimestampBucketizer& other) = default;
  TimestampBucketizer(TimestampBucketizer&& other) = default;
  TimestampBucketizer& operator=(TimestampBucketizer&& other) = default;

  // Validates whether the input bucket width can be used for timestamp
  // bucketing. This function is guaranteed to be in sync with the validation
  // performed by the Create function.
  static absl::Status ValidateBucketWidth(IntervalValue bucket_width,
                                          TimestampScale scale);

  static absl::StatusOr<TimestampBucketizer> Create(IntervalValue bucket_width,
                                                    absl::Time origin,
                                                    absl::TimeZone timezone,
                                                    TimestampScale scale);

  absl::Status Compute(absl::Time input, absl::Time* output) const;

 private:
  TimestampBucketizer(absl::Duration bucket_width, absl::Time origin,
                      absl::TimeZone timezone)
      : bucket_width_(std::move(bucket_width)),
        origin_(std::move(origin)),
        timezone_(std::move(timezone)) {}

  const absl::Duration bucket_width_;
  const absl::Time origin_;
  const absl::TimeZone timezone_;
};

// Same as TimestampBucket, except input, origin and result are at
// Picosecond-precision.
absl::StatusOr<PicoTime> TimestampPicosBucket(const PicoTime& input,
                                              IntervalValue bucket_width,
                                              const PicoTime& origin,
                                              TimestampScale scale);

// This class allows for more efficient implementation of TIMESTAMP_BUCKET
// function when the first argument (input timestamp) is non-const, and the rest
// are constants. The caller can reuse the same instance of this class to
// compute the buckets for different inputs.
class TimestampPicosBucketizer {
 public:
  TimestampPicosBucketizer(const TimestampPicosBucketizer& other) = default;
  TimestampPicosBucketizer& operator=(const TimestampPicosBucketizer& other) =
      default;
  TimestampPicosBucketizer(TimestampPicosBucketizer&& other) = default;
  TimestampPicosBucketizer& operator=(TimestampPicosBucketizer&& other) =
      default;

  static absl::StatusOr<TimestampPicosBucketizer> Create(
      IntervalValue bucket_width, const PicoTime& origin, TimestampScale scale);

  absl::StatusOr<PicoTime> Compute(const PicoTime& input) const;

 private:
  TimestampPicosBucketizer(absl::int128 bucket_width, const PicoTime& origin)
      : bucket_width_(bucket_width), origin_(origin.ToUnixPicos()) {}

  absl::int128 bucket_width_;
  absl::int128 origin_;
};

// Assigns <input> datetime to a specific bucket using the specified
// <bucket_width> INTERVAL and returns the bucket start datetime.
//
// <origin> controls the alignment of the buckets. It can be less than, equal to
// or greater than <input>.
//
// For example: DATETIME_BUCKET(
//                  DATETIME "2022-01-19 13:38:30",
//                  INTERVAL 3 HOUR,
//                  DATETIME "1950-01-01 00:00:00")
//                  --> "2022-01-19 12:00:00"
//
// In the above example:
// - <bucket_width> INTERVAL 3 HOUR indicates that the function divides
// - datetime values into 3-hour buckets
// - <origin> "1950-01-01 00:00:00" indicates that starting from this datetime
//   we divide the range of all datetime values (forwards and backwards) into
//   <bucket_width> subranges.
// - <input> "2022-01-19 13:38:30" gets assigned to bucket
//   ["2022-01-19 12:00:00", "2022-01-19 15:00:00") and the start of the bucket
//   ("2022-01-19 12:00:00") is returned.
//
absl::Status DatetimeBucket(const DatetimeValue& input,
                            IntervalValue bucket_width,
                            const DatetimeValue& origin, TimestampScale scale,
                            DatetimeValue* output);

// This class allows for more efficient implementation of DATETIME_BUCKET
// function when only the first argument (input datetime) is non-const.
class DatetimeBucketizer {
 public:
  DatetimeBucketizer(const DatetimeBucketizer& other) = default;
  DatetimeBucketizer& operator=(const DatetimeBucketizer& other) = default;
  DatetimeBucketizer(DatetimeBucketizer&& other) = default;
  DatetimeBucketizer& operator=(DatetimeBucketizer&& other) = default;

  // Validates whether the input bucket width can be used for datetime
  // bucketing. This function is guaranteed to be in sync with the validation
  // performed by the Create function.
  static absl::Status ValidateBucketWidth(IntervalValue bucket_width,
                                          TimestampScale scale);

  static absl::StatusOr<DatetimeBucketizer> Create(IntervalValue bucket_width,
                                                   DatetimeValue origin,
                                                   TimestampScale scale);

  absl::Status Compute(const DatetimeValue& input, DatetimeValue* output) const;

 private:
  // Contains precomputed parts of bucket width and origin for MONTH interval.
  struct MonthBucketState {
    int64_t bucket_width_months;
    absl::CivilSecond origin;
    int origin_nanos_part;
    bool origin_last_day_of_month;
  };

  // Contains precomputed parts of bucket width and origin for DAY and
  // MICROS/NANOS interval.
  struct NanosBucketState {
    absl::int128 bucket_width_nanos;
    absl::int128 origin_nanos;
  };

  // We use year -10,000 as an epoch to avoid handling negative number of
  // nanoseconds in the calculations.
  static constexpr absl::CivilSecond kEpochCivil{-10000, 0, 0, 0, 0, 0};

  explicit DatetimeBucketizer(
      std::variant<MonthBucketState, NanosBucketState> state)
      : state_(std::move(state)) {}

  static DatetimeValue ComputeForMonthsBucket(const MonthBucketState& state,
                                              const DatetimeValue& input);

  static DatetimeValue ComputeForNanosBucket(const NanosBucketState& state,
                                             const DatetimeValue& input);

  // The following two functions convert DatetimeValue to and from internal
  // (to the class) nanoseconds representation. The nanaseconds value is
  // relative to kEpochCivil. Note that this representatio doesn't have any
  // meaning outside of this class.
  static absl::int128 DatetimeToNanos(const DatetimeValue& datetime);
  static DatetimeValue NanosToDatetime(absl::int128 nanos);

  // Holds either MonthBucketState or NanosBucketState, depending on INTERVAL
  // used in bucket_width.
  const std::variant<MonthBucketState, NanosBucketState> state_;
};

// Assigns <input_date> to a specific bucket using the specified
// <bucket_width> INTERVAL and returns the bucket start date.
//
// <origin_date> controls the alignment of the buckets. It can be less than,
// equal to or greater than <input_date>.
//
// For example: DATE_BUCKET(
//                  DATE "2022-02-19",
//                  INTERVAL 2 MONTH,
//                  DATE "1950-01-01")
//                  --> "2022-01-01"
//
// In the above example:
// - <bucket_width> INTERVAL 2 MONTH indicates that the function divides
// - date values into 2-month buckets
// - <origin_date> "1950-01-01" indicates that starting from this date we divid
//   the range of all date values (forwards and backwards) into <bucket_width>
//   subranges.
// - <input_date> "2022-02-19" gets assigned to bucket
//   ["2022-01-01", "2022-02-28") and the start of the bucket ("2022-01-01") is
//   returned.
//
absl::Status DateBucket(int32_t input_date, IntervalValue bucket_width,
                        int32_t origin_date, int32_t* output_date);

// This class allows for more efficient implementation of DATE_BUCKET function
// when only the first argument (input date) is non-const.
class DateBucketizer {
 public:
  DateBucketizer(const DateBucketizer& other) = default;
  DateBucketizer& operator=(const DateBucketizer& other) = default;
  DateBucketizer(DateBucketizer&& other) = default;
  DateBucketizer& operator=(DateBucketizer&& other) = default;

  // Validates whether the input bucket width can be used for date bucketing.
  // This function is guaranteed to be in sync with the validation performed by
  // the Create function.
  static absl::Status ValidateBucketWidth(IntervalValue bucket_width);

  static absl::StatusOr<DateBucketizer> Create(IntervalValue bucket_width,
                                               int32_t origin_date);

  absl::Status Compute(int32_t input_date, int32_t* output_date) const;

 private:
  // Contains precomputed parts of bucket width and origin for MONTH interval.
  struct MonthBucketState {
    int64_t bucket_width_months;
    absl::CivilDay origin;
    bool origin_last_day_of_month;
  };

  // Contains precomputed parts of bucket width and origin for DAY interval.
  struct DaysBucketState {
    int64_t bucket_width_days;
    int32_t origin_date;
  };

  explicit DateBucketizer(std::variant<MonthBucketState, DaysBucketState> state)
      : state_(std::move(state)) {}

  static int32_t ComputeForMonthsBucket(const MonthBucketState& state,
                                        int32_t input_date);

  static int32_t ComputeForDaysBucket(const DaysBucketState& state,
                                      int32_t input_date);

  // Holds either MonthBucketState or DaysBucketState, depending on INTERVAL
  // used in bucket_width.
  const std::variant<MonthBucketState, DaysBucketState> state_;
};

// The namespace 'internal_functions' includes the internal implementation
// details and is not part of the public api.
namespace internal_functions {

struct ExpansionOptions {
  bool truncate_tz;
  bool expand_quarter;
  bool expand_iso_dayofyear;
};

absl::Status ExpandPercentZQJ(absl::string_view format_string,
                              absl::Time base_time, absl::TimeZone timezone,
                              const ExpansionOptions& expansion_options,
                              std::string* expanded_format_string);

// 1==Sun, ..., 7=Sat
int DayOfWeekIntegerSunToSat1To7(absl::Weekday weekday);

// Returns the timezone sign, hour and minute.
void GetSignHourAndMinuteTimeZoneOffset(const absl::TimeZone::CivilInfo& info,
                                        bool* positive_offset,
                                        int32_t* hour_offset,
                                        int32_t* minute_offset);

// Returns <timezone> if the timezone offset for (<base_time>, <timezone>)
// is minute aligned.  Otherwise returns a fixed-offset timezone using that
// offset truncated to a minute boundary (i.e., eliminating the non-zero
// seconds part of the offset).
//
// GoogleSQL does not support rendering numeric timezones with sub-minute
// offsets (neither do ISO6801 or RFC3339).  If the timezone has a sub-minute
// offset like -07:52:58 (which happens for the America/Los_Angeles time zone
// in years before 1884), we instead treat it (and display it) as -07:52.
absl::TimeZone GetNormalizedTimeZone(absl::Time base_time,
                                     absl::TimeZone timezone);

// Because absl::FormatTime only supports nanosecond precision, we need to
// generate subnanosecond output ourselves. This function takes a format string
// and a subnanosecond value, and returns a new format string that includes the
// needed subnanosecond value.
//
// Exampless:
// - format string is "%Y-%m-%d %H:%M:%E12S", and the subnanosecond
//   value is 123, then the output format string will be
//   "%Y-%m-%d %H:%M:%E9S123".
// - format string is "%Y-%m-%d %H:%M:%E*S",
//   - the subnanosecond value is 123, then the output format string will be
//     "%Y-%m-%d %H:%M:%E*S123".
//   - the subnanosecond value is 120, then the output format string will be
//     "%Y-%m-%d %H:%M:%E*S12".
absl::StatusOr<std::string> AddPicosecondsInFormatString(
    absl::string_view format_string, uint32_t subnanosecond);

}  // namespace internal_functions
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_DATE_TIME_UTIL_H_
