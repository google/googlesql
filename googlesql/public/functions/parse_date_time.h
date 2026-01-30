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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_PARSE_DATE_TIME_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_PARSE_DATE_TIME_H_

#include <cstdint>
#include <string>

#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/pico_time.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// Parses an input <timestamp_string> with the given input <format_string>,
// and produces the appropriate timestamp as output.  Timestamp parts that are
// unspecified in the format are derived from '1970-01-01 00:00:00.000000'
// at the <default_timezone>.  Produces <timestamp> at microseconds precision,
// and returns an error if the resulting timestamp is not in the
// GoogleSQL valid range.
//
// Requires that the string_view arguments are UTF8.  The last byte of the
// string_view can be (but is not required to be) a null-byte.
//
// The supported format elements and their semantics are defined in:
//   (broken link)
//
// <parse_version_2> indicates whether or not new format elements are
// supported:
//    %G - ISO year
//    %g - ISO year (00-99)
//    %V - ISO week number 01-53
//    %J - ISO day of year (001-364 or 001-371)
//    %U - week number (00-53, Sunday is the first day of the week)
//    %W - week number (00-53, Monday is the first day of the week)
//    %j - day of year (001-365 or 001-366)
//    %u - weekday 1-7 (Monday is the first day of the week)
//    %w - weekday 0-6 (Sunday is the first day of the week)
//    %A - full weekday name
//    %a - abbreviated weekday name
//    %P - Meridian indicator (am, pm), equivalent to %p
//
// Note: The methods in cast_date_time.h, such as CastStringToTimestamp use a
// different format style.
absl::Status ParseStringToTimestamp(absl::string_view format_string,
                                    absl::string_view timestamp_string,
                                    absl::TimeZone default_timezone,
                                    bool parse_version2, int64_t* timestamp);

// Invokes MakeTimeZone() on <default_timezone_string> and invokes the prior
// function.  Returns error status if <default_timezone_string> is invalid
// or conversion fails.
absl::Status ParseStringToTimestamp(absl::string_view format_string,
                                    absl::string_view timestamp_string,
                                    absl::string_view default_timezone_string,
                                    bool parse_version2, int64_t* timestamp);

// The 2 functions below support nanoseconds precision.
absl::Status ParseStringToTimestamp(absl::string_view format_string,
                                    absl::string_view timestamp_string,
                                    absl::string_view default_timezone_string,
                                    bool parse_version2, absl::Time* timestamp);

absl::Status ParseStringToTimestamp(absl::string_view format_string,
                                    absl::string_view timestamp_string,
                                    absl::TimeZone default_timezone,
                                    bool parse_version2, absl::Time* timestamp);

absl::Status ParseStringToTimestamp(absl::string_view format_string,
                                    absl::string_view timestamp_string,
                                    absl::string_view default_timezone_string,
                                    int64_t precision, PicoTime* timestamp);

enum class ParseTimestampError {
  // No parsing error.
  kOk,

  // Error that the input string is malformed, and cannot be parsed as a
  // timestamp.
  kMalformed,

  // Error that the input string represents a timestamp that is greater than
  // the valid upper bound.
  kGreaterThanUpperBound,

  // Error that the input string represents a timestamp that is less than
  // the valid lower bound.
  kLessThanLowerBound,
};

// If the return value is OkStatus(), the value of the output parameter <error>
// remains unchanged. Otherwise, the value of <error> indicates whether the
// failure is due to parsing error, and if so, what the parsing error is.
absl::Status ParseStringToTimestamp(absl::string_view format_string,
                                    absl::string_view timestamp_string,
                                    absl::TimeZone default_timezone,
                                    int64_t precision, PicoTime* timestamp,
                                    ParseTimestampError* /*absl_nullable*/ error);

// Same as above, but defaults to Picos precision.
inline absl::Status ParseStringToTimestamp(
    absl::string_view format_string, absl::string_view timestamp_string,
    absl::TimeZone default_timezone, PicoTime* timestamp,
    ParseTimestampError* /*absl_nullable*/ error) {
  return ParseStringToTimestamp(format_string, timestamp_string,
                                default_timezone, /*precision=*/12, timestamp,
                                error);
}

// Parses an input <date_string> with the given input <format_string>,
// and produces the appropriate date as output. Date parts that are
// unspecified in the format are derived from '1970-01-01'.
// Returns an error if the <format_string> contains a format char
// unsupported for DATE or the resulting date is not in the GoogleSQL
// valid range.
//
// Requires that the string_view arguments are UTF8.  The last byte of the
// string_view can be (but is not required to be) a null-byte.
absl::Status ParseStringToDate(absl::string_view format_string,
                               absl::string_view date_string,
                               bool parse_version2, int32_t* date);

// Parses an input <time_string> with the given input <format_string>, and
// produces the appropriate TIME as output. Time parts that are unspecified in
// the format are derived from '00:00:00.000000'.
// Returns an error if the <format_string> contains a format element unsupported
// for TIME or the resulting TIME is not in the range of [00:00:00, 24:00:00).
//
// <scale> is used to specify the maximum precision supported for the format
// element "%E#S", which will be 6 and 9 for micros and nanos, respectively. The
// parsed result value also respects this specified <scale>, which means that
// for the format element "%E*S", while parsing will consume as many numeric
// digits as present, the parsed result will truncate any digits beyond 6 or 9
// for micros and nanos, respectively. The same behavior also applys to the
// <scale> argument for ParseStringToDatetime() below.
//
// Requires that the string_view arguments are UTF8.  The last byte of the
// string_view can be (but is not required to be) a null-byte.
absl::Status ParseStringToTime(absl::string_view format_string,
                               absl::string_view time_string,
                               TimestampScale scale,
                               TimeValue* time);

// Parses an input <datetime_string> with the given input <format_string>, and
// produces the appropriate DATETIME as output. Date and time parts that are
// unspecified in the format are derived from '1970-01-01 00:00:00.000000'.
// Returns an error if the <format_string> contains a format element unsupported
// for DATETIME or the resulting DATETIME is not in the GoogleSQL valid range.
//
// Requires that the string_view arguments are UTF8.  The last byte of the
// string_view can be (but is not required to be) a null-byte.
absl::Status ParseStringToDatetime(absl::string_view format_string,
                                   absl::string_view datetime_string,
                                   TimestampScale scale, bool parse_version2,
                                   DatetimeValue* datetime);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_PARSE_DATE_TIME_H_
