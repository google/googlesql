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

#include "googlesql/public/functions/date_time_util.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/arithmetics.h"
#include "googlesql/public/functions/date_time_util_internal.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/time_zone_util.h"
#include "googlesql/public/types/timestamp_util.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "googlesql/base/mathutil.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/time_proto_util.h"

namespace googlesql {
namespace functions {
namespace {

using date_time_util_internal::GetIsoWeek;
using date_time_util_internal::GetIsoYear;
using date_time_util_internal::NextWeekdayOrToday;
using date_time_util_internal::PrevWeekdayOrToday;

constexpr int64_t kNaiveNumSecondsPerMinute = 60;
constexpr int64_t kNaiveNumSecondsPerHour = 60 * kNaiveNumSecondsPerMinute;
constexpr int64_t kNaiveNumSecondsPerDay = 24 * kNaiveNumSecondsPerHour;
constexpr int64_t kNaiveNumMicrosPerDay = kNaiveNumSecondsPerDay * 1000 * 1000;

constexpr int64_t kNumNanosPerSecond = 1000 * 1000 * 1000;
constexpr int64_t kNumNanosPerMinute =
    kNaiveNumSecondsPerMinute * kNumNanosPerSecond;
constexpr int64_t kNumNanosPerHour =
    kNaiveNumSecondsPerHour * kNumNanosPerSecond;
constexpr int64_t kNumNanosPerDay = kNaiveNumSecondsPerDay * kNumNanosPerSecond;
constexpr absl::int128 kNumPicosPerDay = 1000 * kNumNanosPerDay;

enum NewOrLegacyTimestampType {
  NEW_TIMESTAMP_TYPE,
  // TODO: strip legacy timestamp type
  LEGACY_TIMESTAMP_TYPE,
};

const absl::CivilDay kEpochDay = absl::CivilDay(1970, 1, 1);

absl::CivilDay EpochDaysToCivilDay(int32_t days_since_epoch) {
  return kEpochDay + days_since_epoch;
}

int DaysPerMonth(int year, int month) {
  static constexpr int kDaysPerMonth[1 + 12] = {
      -1, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31  // non leap year
  };
  return kDaysPerMonth[month] +
         (month == 2 && date_time_util_internal::IsLeapYear(year));
}

bool IsLastDayOfTheMonth(int year, int month, int day) {
  return DaysPerMonth(year, month) == day;
}

// Sanitizes user strings and truncates long ones.
class UserString {
 public:
  explicit UserString(absl::string_view str) : str_(str) {}

 private:
  // Enough for a valid picosecond timestamp string like
  // '2014-01-31 18:21:43.111222333444+00:00' and a few extra bytes to aid
  // troubleshooting.
  static constexpr int64_t kMaxValueLength = 50;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, UserString s) {
    sink.Append(absl::CEscape(s.str_.substr(0, kMaxValueLength)));
    if (s.str_.size() > kMaxValueLength) {
      sink.Append("...");
    }
  }

  friend std::ostream& operator<<(std::ostream& os, UserString s) {
    os << absl::CEscape(s.str_.substr(0, kMaxValueLength));
    if (s.str_.size() > kMaxValueLength) {
      os << "...";
    }
    return os;
  }

  absl::string_view str_;
};

static absl::StatusOr<absl::Weekday> GetWeekdayFromString(
    absl::string_view dow) {
  static const absl::NoDestructor<
      absl::flat_hash_map<std::string, absl::Weekday>>
      weekday_map({
          {"SUNDAY", absl::Weekday::sunday},
          {"SUN", absl::Weekday::sunday},
          {"MONDAY", absl::Weekday::monday},
          {"MON", absl::Weekday::monday},
          {"TUESDAY", absl::Weekday::tuesday},
          {"TUE", absl::Weekday::tuesday},
          {"WEDNESDAY", absl::Weekday::wednesday},
          {"WED", absl::Weekday::wednesday},
          {"THURSDAY", absl::Weekday::thursday},
          {"THU", absl::Weekday::thursday},
          {"FRIDAY", absl::Weekday::friday},
          {"FRI", absl::Weekday::friday},
          {"SATURDAY", absl::Weekday::saturday},
          {"SAT", absl::Weekday::saturday},
      });

  std::string upper_dow = absl::AsciiStrToUpper(dow);
  if (auto it = weekday_map->find(upper_dow); it != weekday_map->end()) {
    return it->second;
  }
  return MakeEvalError() << "Invalid day of week: " << dow;
}

}  // namespace

bool IsValidDate(int32_t date) {
  return date >= googlesql::types::kDateMin &&
         date <= googlesql::types::kDateMax;
}

static int32_t CivilDayToEpochDays(absl::CivilDay day) {
  return static_cast<int32_t>(day - kEpochDay);
}

static bool IsValidCivilDay(absl::CivilDay day) {
  return IsValidDate(CivilDayToEpochDays(day));
}

bool IsValidDay(absl::civil_year_t year, int month, int day) {
  // absl::CivilDay will 'normalize' its arguments, so simply compare
  // the input against the result to check whether a YMD is valid.
  absl::CivilDay civil_day(year, month, day);
  return civil_day.year() == year && civil_day.month() == month &&
         civil_day.day() == day;
}

absl::string_view DateTimestampPartToSQL(DateTimestampPart date_part) {
  switch (date_part) {
    case functions::WEEK_MONDAY:
      return "WEEK(MONDAY)";
    case functions::WEEK_TUESDAY:
      return "WEEK(TUESDAY)";
    case functions::WEEK_WEDNESDAY:
      return "WEEK(WEDNESDAY)";
    case functions::WEEK_THURSDAY:
      return "WEEK(THURSDAY)";
    case functions::WEEK_FRIDAY:
      return "WEEK(FRIDAY)";
    case functions::WEEK_SATURDAY:
      return "WEEK(SATURDAY)";
    default:
      return DateTimestampPart_Name(date_part);
  }
}

static absl::Status* const kNoError = nullptr;

// Returns whether or not there are at least <remaining_length> characters left
// beyond <current_idx> of <str>.
static bool CheckRemainingLength(absl::string_view str, int current_idx,
                                 int remaining_length) {
  if (current_idx + remaining_length > static_cast<int64_t>(str.length())) {
    return false;
  }
  return true;
}

// Parse between <min_digits> and <max_digits> from <str> starting at
// offset <idx>, incrementing <idx> for parsed digits and returning the
// result in <part_value>.  Returns true if successful, false if not.
template <class T>
static bool ParseDigits(absl::string_view str, int min_digits, int max_digits,
                        int* idx, T* part_value) {
  static_assert(std::is_same<T, int>::value || std::is_same<T, int64_t>::value,
                "T must be int or int64_t");
  int num_digits = 0;
  *part_value = 0;
  while (num_digits < max_digits && *idx < static_cast<int64_t>(str.length()) &&
         absl::ascii_isdigit(str[*idx])) {
    *part_value *= 10;
    *part_value += str[*idx] - '0';
    ++*idx;
    ++num_digits;
  }
  if (num_digits < min_digits) {
    return false;
  }
  return true;
}

// Parse a single expected character from <str> at offset <idx>, incrementing
// <idx> if successful and return success or failure.
static bool ParseCharacter(absl::string_view str, const char character,
                           int* idx) {
  if (*idx >= str.length() || str[*idx] != character) {
    return false;
  }
  ++(*idx);
  return true;
}

// Parse <str> starting at offset <idx> into <year>, <month>, and <day> parts,
// advancing <idx> to point to the next unparsed digit. The valid format is
// YYYY-[M]M-[D]D.
// Returns success or failure.
static bool ParsePrefixToDateParts(absl::string_view str, int* idx, int* year,
                                   int* month, int* day) {
  // Minimum required length of a valid date is 8.
  if (!CheckRemainingLength(str, *idx, 8 /* remaining_length */) ||
      !ParseDigits(str, 4, 4, idx, year) || !ParseCharacter(str, '-', idx) ||
      !ParseDigits(str, 1, 2, idx, month) || !ParseCharacter(str, '-', idx) ||
      !ParseDigits(str, 1, 2, idx, day)) {
    return false;
  }
  return true;
}
// Same as ParsePrefixToDateParts, but requires the entire string to be
// consumed.
static bool ParseStringToDateParts(absl::string_view str, int* idx, int* year,
                                   int* month, int* day) {
  return ParsePrefixToDateParts(str, idx, year, month, day) &&
         *idx >= static_cast<int64_t>(str.length());
}

static const int64_t powers_of_ten[] = {
    1,           10,           100,           1000,      10000,
    100000,      1000000,      10000000,      100000000, 1000000000,
    10000000000, 100000000000, 1000000000000,
};
// Parse <str> starting at offset <idx> into <hour>, <minute>, <second> and
// <subsecond> parts, incrementing <idx> for parsed digits. <scale> indicates
// the number of subsecond digits requested. The subsecond part is normalized to
// the requested scale.
// The valid format is [H]H:[M]M:[S]S[.DDDDDDDDDDDD].
// Returns success or failure.
static bool ParsePrefixToTimeParts(absl::string_view str, TimestampScale scale,
                                   int* idx, int* hour, int* minute,
                                   int* second, int64_t* subsecond,
                                   int* precision = nullptr) {
  // Maximum number of subsecond digits that can be parsed.
  static constexpr int kMaxSubsecondDigits = 12;
  if (!CheckRemainingLength(str, *idx, 5 /* remaining_length */) ||
      !ParseDigits(str, 1, 2, idx, hour) ||
      !CheckRemainingLength(str, *idx, 4 /* remaining_length */) ||
      !ParseCharacter(str, ':', idx) || !ParseDigits(str, 1, 2, idx, minute) ||
      !CheckRemainingLength(str, *idx, 2 /* remaining_length */) ||
      !ParseCharacter(str, ':', idx) || !ParseDigits(str, 1, 2, idx, second)) {
    return false;
  }
  if (*idx >= static_cast<int64_t>(str.length()))
    return true;  // Done consuming <str>.

  // Parse the subseconds, if any.
  if (str[*idx] == '.') {
    ++(*idx);
    const int start_subsecond_idx = *idx;
    if (!ParseDigits(str, 1, kMaxSubsecondDigits, idx, subsecond)) {
      return false;
    }

    // Normalize the subseconds to the specified scale.
    const int num_parsed_subsecond_digits = *idx - start_subsecond_idx;
    if (scale - num_parsed_subsecond_digits < 0) {
      return false;
    }
    ABSL_CHECK_LE(num_parsed_subsecond_digits, kMaxSubsecondDigits);  // Crash OK

    // <scale> is at most 12, and <num_parsed_subsecond_digits> is at least
    // 1, so the difference is no larger than 11 so indexing into
    // powers_of_ten[] cannot read off the end of the array.
    *subsecond *= powers_of_ten[scale - num_parsed_subsecond_digits];

    // Calculate timestamp precision based on the number of subsecond digits
    // seen in the timestamp, including trailing zeros.
    if (precision) {
      *precision = static_cast<int>(std::ceil(
                       static_cast<double>(num_parsed_subsecond_digits) / 3)) *
                   3;
    }
  }
  return true;
}

// Same as ParsePrefixToTimeParts, but requires the entire string to be
// consumed.
static bool ParseStringToTimeParts(absl::string_view str, TimestampScale scale,
                                   int* idx, int* hour, int* minute,
                                   int* second, int64_t* subsecond) {
  return ParsePrefixToTimeParts(str, scale, idx, hour, minute, second,
                                subsecond) &&
         *idx >= static_cast<int64_t>(str.length());
}

// Parses the string into the relevant timestamp parts.  <scale> indicates
// the number of subsecond digits requested.  Parts not present in the string
// get initialized to 0 as appropriate.  The subsecond part is
// normalized to the requested scale, and if there are extra digits then
// an error is produced.
// The valid format is YYYY-[M]M-[D]D( |T)[[H]H:[M]M:[S]S[.DDDDDDDDD]].
// Returns success or failure.
static bool ParseStringToDatetimeParts(absl::string_view str,
                                       TimestampScale scale, int* year,
                                       int* month, int* day, int* hour,
                                       int* minute, int* second,
                                       int64_t* subsecond) {
  int idx = 0;
  if (!ParsePrefixToDateParts(str, &idx, year, month, day)) {
    return false;
  }
  if (idx >= static_cast<int64_t>(str.length()))
    return true;  // Done consuming <str>.
  // The rest are all optional: time, subseconds, time zone

  // Initial space, 'T', or 't' to kick off the rest.
  if ((!ParseCharacter(str, ' ', &idx) && !ParseCharacter(str, 'T', &idx) &&
       !ParseCharacter(str, 't', &idx)) ||
      !CheckRemainingLength(str, idx, 2 /* remaining_length */)) {
    return false;
  }

  if (!ParseStringToTimeParts(str, scale, &idx, hour, minute, second,
                              subsecond)) {
    return false;
  }
  return true;
}

// This function expects the timezone hour and minute to be positive values,
// with the offset direction determined by <timezone_sign>.
static bool IsValidTimeZoneParts(const char timezone_sign, int timezone_hour,
                                 int timezone_minute) {
  if (timezone_sign != '+' && timezone_sign != '-') {
    return false;
  }
  if (timezone_hour > 14 || timezone_hour < 0 || timezone_minute > 59 ||
      timezone_minute < 0) {
    return false;
  }
  // Since the negative and positive ranges are the same, we don't need
  // to worry about the sign.  This check ensures that we don't allow a
  // timezone like +14:59 that would pass the previous check.
  return IsValidTimeZone(timezone_hour * 60 + timezone_minute);
}

static bool IsValidTimeOfDay(int hour, int minute, int second) {
  // Note that Google time scales do not include leap seconds, so <second>
  // ranges only from 0-59.  However, to support potential callers that
  // expect leap second support, we allow second == 60.
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 60) {
    return false;
  }
  return true;
}

static std::string DateErrorString(int32_t date) {
  std::string out;
  if (!ConvertDateToString(date, &out).ok()) {
    out = absl::StrCat("DATE(", date, ")");
  }
  return out;
}

static std::string TimestampErrorString(int64_t timestamp, TimestampScale scale,
                                        absl::TimeZone timezone) {
  std::string out;
  if (!ConvertTimestampToStringWithoutTruncation(timestamp, scale, timezone,
                                                 &out)
           .ok()) {
    out = absl::StrCat("timestamp(", timestamp, ")");
  }
  return out;
}

static std::string DefaultTimestampFormatStr(TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return "%E4Y-%m-%d %H:%M:%S%Ez";
    case kMilliseconds:
      return "%E4Y-%m-%d %H:%M:%E3S%Ez";
    case kMicroseconds:
      return "%E4Y-%m-%d %H:%M:%E6S%Ez";
    case kNanoseconds:
      return "%E4Y-%m-%d %H:%M:%E9S%Ez";
    case kPicoseconds:
      return "%E4Y-%m-%d %H:%M:%E12S%Ez";
  }
}

// The format string returned is used by absl::StrFormat().
static std::string DefaultTimeFormatStr(TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return "%02d:%02d:%02d";
    case kMilliseconds:
      return "%02d:%02d:%02d.%03d";
    case kMicroseconds:
      return "%02d:%02d:%02d.%06d";
    case kNanoseconds:
      return "%02d:%02d:%02d.%09d";
    case kPicoseconds:
      return "%02d:%02d:%02d.%12d";
  }
}

// The format string returned is used by absl::StrFormat().
static std::string DefaultDatetimeFormatStr(TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return "%04d-%02d-%02d %02d:%02d:%02d";
    case kMilliseconds:
      return "%04d-%02d-%02d %02d:%02d:%02d.%03d";
    case kMicroseconds:
      return "%04d-%02d-%02d %02d:%02d:%02d.%06d";
    case kNanoseconds:
      return "%04d-%02d-%02d %02d:%02d:%02d.%09d";
    case kPicoseconds:
      return "%04d-%02d-%02d %02d:%02d:%02d.%12d";
  }
}

static std::string TimestampErrorString(absl::Time time,
                                        absl::TimeZone timezone) {
  std::string out;
  if (!ConvertTimestampToString(time, kMicroseconds, timezone, &out).ok()) {
    out =
        absl::StrCat("timestamp(",
                     absl::FormatTime(DefaultTimestampFormatStr(kMicroseconds),
                                      time, timezone),
                     ")");
  }
  return out;
}

static bool TimeFromParts(absl::civil_year_t year, int month, int day, int hour,
                          int minute, int second, absl::TimeZone timezone,
                          absl::Time* time) {
  if (!IsValidDay(year, month, day)) {
    return false;
  }
  if (!IsValidTimeOfDay(hour, minute, second)) {
    return false;
  }
  *time = timezone.At(absl::CivilSecond(year, month, day, hour, minute, second))
              .pre;
  return true;
}

static absl::StatusOr<absl::Duration> MakeDuration(int64_t subsecond,
                                                   TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return absl::Seconds(subsecond);
    case kMilliseconds:
      return absl::Milliseconds(subsecond);
    case kMicroseconds:
      return absl::Microseconds(subsecond);
    case kNanoseconds:
      return absl::Nanoseconds(subsecond);
    case kPicoseconds:
      return MakeEvalError() << "Picoseconds is not supported";
  }
}

// It requires the timestamp parts to be within valid bounds (i.e.,
// hour <= 23, minutes <= 59, etc.). This function does not validate the
// range of the result. The caller is responsible for this check if necessary.
static bool TimestampFromParts(absl::civil_year_t year, int month, int day,
                               int hour, int minute, int second, int subsecond,
                               TimestampScale scale, absl::TimeZone timezone,
                               absl::Time* timestamp) {
  if (!TimeFromParts(year, month, day, hour, minute, second, timezone,
                     timestamp)) {
    return false;
  }
  auto status_or_duration = MakeDuration(subsecond, scale);
  if (!status_or_duration.ok()) {
    return false;
  }
  *timestamp += status_or_duration.value();
  return true;
}

// Parse the offset-based time zone format with optional leading 'UTC':
//   [UTC]+/-HH[[:]MM].
// and returns time zone offset parts: sign, hour and minute.
static bool ParseTimeZoneOffsetFormat(absl::string_view timezone_string,
                                      char* timezone_sign, int* timezone_hour,
                                      int* timezone_minute) {
  // Optional "UTC" prefix before offset
  absl::ConsumePrefix(&timezone_string, "UTC");
  if (timezone_string.empty()) {
    return false;
  }

  // Check time zone sign.
  if (timezone_string[0] != '+' && timezone_string[0] != '-') {
    return false;
  }
  *timezone_sign = timezone_string[0];
  *timezone_hour = 0;
  *timezone_minute = 0;

  int idx = 1;
  if (!CheckRemainingLength(timezone_string, idx, 1 /* remaining_length */) ||
      !ParseDigits(timezone_string, 1, 2, &idx, timezone_hour)) {
    return false;
  }
  if (idx >= static_cast<int64_t>(timezone_string.size())) return true;

  // Skip optional ':' minutes delimiter
  ParseCharacter(timezone_string, ':', &idx);
  if (!CheckRemainingLength(timezone_string, idx, 1 /* remaining_length */) ||
      !ParseDigits(timezone_string, 1, 2, &idx, timezone_minute)) {
    return false;
  }
  if (idx >= static_cast<int64_t>(timezone_string.size())) return true;
  return false;
}

// Maps time zone sign, hour, and minute to a total offset in <scale> units.
// Returns success or failure.
static bool TimeZonePartsToOffset(const char timezone_sign,
                                  int64_t timezone_hour,
                                  int64_t timezone_minute, TimestampScale scale,
                                  int64_t* timezone_offset) {
  if (!IsValidTimeZoneParts(timezone_sign, timezone_hour, timezone_minute)) {
    return false;
  }

  *timezone_offset =
      (timezone_hour * 60 + timezone_minute) * 60 * powers_of_ten[scale];
  if (timezone_sign == '-') {
    *timezone_offset = -*timezone_offset;
  }
  return true;
}

// Parses the string into the relevant timestamp parts.  <scale> indicates
// the number of subsecond digits requested.  Parts not present in the string
// get initialized to 0 or "" as appropriate.  The subsecond part is
// normalized to the requested scale, and if there are extra digits then
// an error is produced.
static absl::Status ParseStringToTimestampParts(
    absl::string_view str, TimestampScale scale, int* year, int* month,
    int* day, int* hour, int* minute, int* second, int64_t* subsecond,
    absl::TimeZone* timezone, bool* string_includes_timezone,
    int* precision = nullptr) {
  auto eval_error = [str] {
    return MakeEvalError() << "Invalid timestamp: '" << UserString(str) << "'";
  };

  int idx = 0;

  // Minimum required length is 8 for a valid timestamp.
  if (!CheckRemainingLength(str, idx, 8 /* remaining_length */) ||
      !ParseDigits(str, 4, 5, &idx, year) || !ParseCharacter(str, '-', &idx) ||
      !ParseDigits(str, 1, 2, &idx, month) || !ParseCharacter(str, '-', &idx) ||
      !ParseDigits(str, 1, 2, &idx, day)) {
    return eval_error();
  }
  if (idx >= static_cast<int64_t>(str.length()))
    return absl::OkStatus();  // Done consuming <str>.

  // The rest are all optional: time, subseconds, time zone

  // Initial space, 'T', or 't' to kick off the rest.
  if ((!ParseCharacter(str, ' ', &idx) && !ParseCharacter(str, 'T', &idx) &&
       !ParseCharacter(str, 't', &idx)) ||
      !CheckRemainingLength(str, idx, 2 /* remaining_length */)) {
    return eval_error();
  }

  // Time is '[H]H:[M]M:[S]S'.
  if (absl::ascii_isdigit(str[idx])) {
    if (!ParsePrefixToTimeParts(str, scale, &idx, hour, minute, second,
                                subsecond, precision)) {
      return eval_error();
    }
    if (idx >= static_cast<int64_t>(str.length()))
      return absl::OkStatus();  // Done consuming <str>.
  } else if (str[idx] != '+' && str[idx] != '-') {
    return eval_error();
  }

  if (absl::ClippedSubstr(str, idx).empty()) {
    *string_includes_timezone = false;
    return absl::OkStatus();
  }

  *string_includes_timezone = true;
  // Optional UTC prefix for timezone (preceded by exactly one space)
  if (absl::StartsWith(str.substr(idx), " UTC")) {
    idx += 4;
    // If there is no offset after "UTC", it was simply UTC timezone.
    if (absl::ClippedSubstr(str, idx).empty()) {
      *timezone = absl::UTCTimeZone();
      return absl::OkStatus();
    }
  }
  switch (str[idx]) {
    case '+':
    case '-':
      // Canonical time zone form.
      return MakeTimeZone(absl::ClippedSubstr(str, idx), timezone);
    case 'Z':
    case 'z':
      if (idx + 1 != str.size()) {
        // Trailing content after 'Z'.
        return eval_error();
      }
      // UTC.
      *timezone = absl::UTCTimeZone();
      return absl::OkStatus();
    default:
      break;
  }
  // For a time zone name, it must be a space followed by the name.   Do not
  // allow a space followed by the canonical form (as indicated by a leading
  // '+/-').
  if (str[idx] != ' ' || static_cast<int64_t>(str.size()) < idx + 2 ||
      str[idx + 1] == '+' || str[idx + 1] == '-') {
    return eval_error();
  }
  ++idx;
  return MakeTimeZone(absl::ClippedSubstr(str, idx), timezone);
}

// Converts timestamps between scales.
static absl::Status ConvertBetweenTimestampsInternal(
    int64_t input, TimestampScale input_scale, TimestampScale output_scale,
    int64_t* output) {
  absl::Status error;
  if (input_scale == output_scale) {
    // No-op case.
    *output = input;
  } else if (output_scale > input_scale) {
    // Widening case.
    int64_t multipler = powers_of_ten[output_scale - input_scale];
    Multiply<int64_t>(input, multipler, output, &error);
  } else {
    // Narrowing case.
    //
    // Converting to a narrowed subsecond type before epoch (1970-01-01
    // 00:00:00) needs some special treatment. This is because the subsecond is
    // represented as 10's complement (negative) for timestamp before epoch.
    //
    // From example:
    //   1969-12-31 23:59:59.123456  -->
    //        -876544 (TIMESTAMP_MICRO)
    //   1969-12-31 23:59:59.123  -->
    //        -877 (TIMESTAMP_MILLIS)
    //  The conversion needs to compensate -1 after the division.
    int64_t dividend = powers_of_ten[input_scale - output_scale];
    if (Divide<int64_t>(input, dividend, output, &error) && input < 0) {
      int64_t r = 0;
      if (Modulo<int64_t>(input, dividend, &r, &error) && r != 0) {
        Subtract<int64_t>(*output, 1, output, &error);
      }
    }
  }
  return error;
}

// Converts a timestamp interval between different scales.
template <class T, typename = std::enable_if_t<std::is_same_v<T, int64_t> ||
                                               std::is_same_v<T, absl::int128>>>
static absl::StatusOr<T> ConvertTimestampInterval(T interval,
                                                  TimestampScale interval_scale,
                                                  TimestampScale output_scale) {
  if (interval_scale == output_scale) {
    return interval;
  }

#define FCT(scale1, scale2) (scale1 * 10 + scale2)

  switch (FCT(interval_scale, output_scale)) {
    case FCT(kSeconds, kMilliseconds):
    case FCT(kSeconds, kMicroseconds):
    case FCT(kSeconds, kNanoseconds):
    case FCT(kSeconds, kPicoseconds):
    case FCT(kMilliseconds, kMicroseconds):
    case FCT(kMilliseconds, kNanoseconds):
    case FCT(kMilliseconds, kPicoseconds):
    case FCT(kMicroseconds, kNanoseconds):
    case FCT(kMicroseconds, kPicoseconds):
    case FCT(kNanoseconds, kPicoseconds):
      T output_interval;
      if (Multiply<T>(interval, powers_of_ten[output_scale - interval_scale],
                      &output_interval, kNoError)) {
        return output_interval;
      }
      break;
    case FCT(kPicoseconds, kNanoseconds):
    case FCT(kPicoseconds, kMicroseconds):
    case FCT(kPicoseconds, kMilliseconds):
    case FCT(kPicoseconds, kSeconds):
    case FCT(kNanoseconds, kMicroseconds):
    case FCT(kNanoseconds, kMilliseconds):
    case FCT(kNanoseconds, kSeconds):
    case FCT(kMicroseconds, kMilliseconds):
    case FCT(kMicroseconds, kSeconds):
    case FCT(kMilliseconds, kSeconds):
      return interval / powers_of_ten[interval_scale - output_scale];
    default:
      break;
  }
  return MakeEvalError() << "Converting timestamp interval " << interval
                         << " at " << TimestampScale_Name(interval_scale)
                         << " scale to " << TimestampScale_Name(output_scale)
                         << " scale causes overflow";
}

// Adjust Year/Month/Day for overflow/underflow value.
// Roll year value forward if month is overflow, or backward if month is
// underflow, until month value falls between [1, 12].
// If day value is exceeding the maximum days of the month, day value will be
// truncated to the maximum day of that month, no month roll over.
static void AdjustYearMonthDay(int* year, int* month, int* day) {
  int m = *month % 12;
  *year += *month / 12;
  if (m <= 0) {
    ABSL_DCHECK_GT(m, -12);
    m += 12;
    (*year)--;
  }
  *month = m;
  if (IsValidDay(*year, *month, *day)) {
    // This is a valid day, return it.
    return;
  } else {
    // Get the day before the first day of the next month. Note, default
    // CivilDay construction will handle wrap-around (e.g. month = 12).
    absl::CivilDay last_day_of_month = absl::CivilDay(*year, *month + 1, 1) - 1;
    *day = last_day_of_month.day();
  }
}

static absl::Status CheckValidAddTimestampPart(DateTimestampPart part,
                                               bool is_legacy) {
  switch (part) {
    case YEAR:
    case QUARTER:
    case MONTH:
    case WEEK:
      if (!is_legacy) {
        return MakeEvalError()
               << "Unsupported DateTimestampPart "
               << DateTimestampPart_Name(part) << " for TIMESTAMP_ADD";
      }
      ABSL_FALLTHROUGH_INTENDED;
    case DAY:
    case HOUR:
    case MINUTE:
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND:
    case PICOSECOND:
      return absl::OkStatus();
    case DATE:
    case DAYOFWEEK:
    case DAYOFYEAR:
    case ISOYEAR:
    case ISOWEEK:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIMESTAMP_ADD";
    default:
      break;
  }
  return MakeEvalError() << "Unexpected DateTimestampPart "
                         << DateTimestampPart_Name(part)
                         << " for TIMESTAMP_ADD";
}

ABSL_MUST_USE_RESULT static bool MakeDate(int year, int month, int day,
                                          absl::CivilDay* civil_day);

// Returns false if date is out of bounds.
static bool MakeDate(int year, int month, int day, absl::CivilDay* civil_day) {
  if (year < 1 || year > 9999 || !IsValidDay(year, month, day)) {
    return false;
  }
  *civil_day = absl::CivilDay(year, month, day);
  return true;
}

static absl::Status MakeAddTimestampOverflowError(int64_t timestamp,
                                                  DateTimestampPart part,
                                                  int64_t interval,
                                                  TimestampScale scale,
                                                  absl::TimeZone timezone) {
  return MakeEvalError() << "Adding " << interval << " "
                         << DateTimestampPart_Name(part) << " to timestamp "
                         << TimestampErrorString(timestamp, scale, timezone)
                         << " causes overflow";
}

static absl::Status MakeAddTimestampOverflowError(absl::Time timestamp,
                                                  DateTimestampPart part,
                                                  int64_t interval,
                                                  absl::TimeZone timezone) {
  return MakeEvalError() << "Adding " << interval << " "
                         << DateTimestampPart_Name(part) << " to timestamp "
                         << TimestampErrorString(timestamp, timezone)
                         << " causes overflow";
}

static absl::Status MakeSubTimestampOverflowError(int64_t timestamp,
                                                  DateTimestampPart part,
                                                  int64_t interval,
                                                  TimestampScale scale,
                                                  absl::TimeZone timezone) {
  return MakeEvalError() << "Subtracting " << interval << " "
                         << DateTimestampPart_Name(part) << " from timestamp "
                         << TimestampErrorString(timestamp, scale, timezone)
                         << " causes overflow";
}

static absl::Status MakeSubTimestampOverflowError(absl::Time timestamp,
                                                  DateTimestampPart part,
                                                  int64_t interval,
                                                  absl::TimeZone timezone) {
  return MakeEvalError() << "Subtracting " << interval << " "
                         << DateTimestampPart_Name(part) << " from timestamp "
                         << TimestampErrorString(timestamp, timezone)
                         << " causes overflow";
}

// Performs no bounds checking on cs - assumed to be (conceptually) checked
// with IsValidTime.
static bool AddAtLeastDaysToCivilTime(DateTimestampPart part, int32_t interval,
                                      absl::CivilSecond cs, int subsecond,
                                      absl::TimeZone timezone,
                                      TimestampScale scale,
                                      absl::Time* result_timestamp) {
  switch (part) {
    case YEAR: {
      int year;
      // cast is safe, given method contract.
      if (!Add<int32_t>(static_cast<int32_t>(cs.year()), interval, &year,
                        kNoError)) {
        return false;
      }
      int month = cs.month();
      int day = cs.day();
      AdjustYearMonthDay(&year, &month, &day);
      if (!TimestampFromParts(year, month, day, cs.hour(), cs.minute(),
                              cs.second(), subsecond, scale, timezone,
                              result_timestamp)) {
        return false;
      }
      break;
    }
    case QUARTER:
      if (!Multiply<int32_t>(interval, 3, &interval, kNoError)) {
        return false;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case MONTH: {
      int32_t month;
      if (!Add<int32_t>(cs.month(), interval, &month, kNoError)) {
        return false;
      }
      int day = cs.day();
      // cast is safe, given method contract.
      int year = static_cast<int32_t>(cs.year());
      AdjustYearMonthDay(&year, &month, &day);
      if (!TimestampFromParts(year, month, day, cs.hour(), cs.minute(),
                              cs.second(), subsecond, scale, timezone,
                              result_timestamp)) {
        return false;
      }
      break;
    }
    case WEEK:
      if (!Multiply<int32_t>(interval, 7, &interval, kNoError)) {
        return false;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case DAY: {
      absl::CivilDay date;
      // cast is safe, given method contract.
      if (!MakeDate(static_cast<int32_t>(cs.year()), cs.month(), cs.day(),
                    &date)) {
        // This is unreachable since the input timestamp is valid and
        // therefore cannot be outside the Date range bounds.
        return false;
      }
      // This could probably be simplified to avoid bouncing back and forth
      // between civil day, but its not clear how important it is to preserve
      // exact overflow semantics, or how that would interact with
      // absl::CivilDay (which encodes days simply as 'int').
      int days = CivilDayToEpochDays(date);
      if (!Add<int32_t>(days, interval, &days, kNoError)) {
        return false;
      }
      date = EpochDaysToCivilDay(days);
      if (!TimestampFromParts(date.year(), date.month(), date.day(), cs.hour(),
                              cs.minute(), cs.second(), subsecond, scale,
                              timezone, result_timestamp)) {
        return false;
      }
      break;
    }
    default:
      ABSL_DCHECK(false) << "Should not reach here";
      return false;
  }
  return true;
}

// TODO:  This implementation requires a datetime to
// be converted to a absl::Time and then to a CivilInfo, in order to
// share the logic for adding DAY and larger intervals with the legacy
// timestamp implementation.  We should consider a more native
// implementation to avoid two conversions, if possible.
static bool AddAtLeastDaysToDatetime(const DatetimeValue& datetime,
                                     DateTimestampPart part,
                                     int64_t interval_in,
                                     DatetimeValue* output) {
  int32_t interval = interval_in;
  if (interval != interval_in) {
    // The interval overflowed an int32, for DAY or greater granularity the
    // datetime result would overflow as well.
    return false;
  }
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::Time timestamp = utc.At(datetime.ConvertToCivilSecond()).pre;
  timestamp += absl::Nanoseconds(datetime.Nanoseconds());

  const absl::TimeZone::CivilInfo info = utc.At(timestamp);

  // cast is safe, guaranteed to be less than 1 billion.
  int subsecond = static_cast<int>(absl::ToInt64Nanoseconds(info.subsecond));

  absl::Time result_timestamp;
  if (!AddAtLeastDaysToCivilTime(part, interval, info.cs, subsecond, utc,
                                 kNanoseconds, &result_timestamp)) {
    return false;
  }

  // Convert the result_timestamp back to datetime for output.
  if (!ConvertTimestampToDatetime(result_timestamp, utc, output).ok()) {
    return false;
  }
  return true;
}

static absl::Status AddDuration(absl::Time timestamp, int64_t interval,
                                DateTimestampPart part, absl::TimeZone timezone,
                                absl::Time* output, bool* had_overflow) {
  switch (part) {
    case HOUR:
      *output = timestamp + absl::Hours(interval);
      break;
    case MINUTE:
      *output = timestamp + absl::Minutes(interval);
      break;
    case SECOND:
      *output = timestamp + absl::Seconds(interval);
      break;
    case MILLISECOND:
      *output = timestamp + absl::Milliseconds(interval);
      break;
    case MICROSECOND:
      *output = timestamp + absl::Microseconds(interval);
      break;
    case NANOSECOND:
      *output = timestamp + absl::Nanoseconds(interval);
      break;
    default:
      break;
  }
  if (!IsValidTime(*output)) {
    *had_overflow = true;
    return MakeAddTimestampOverflowError(timestamp, part, interval, timezone);
  }
  return absl::OkStatus();
}

// The differences between this function and AddTimestampInternal of int64
// timestamp are the following:
// Adding an interval of granularity smaller than a day will not cause
// arithmetic overflow. But it returns error status if the <output>
// absl::Time is out of range.
static absl::Status AddTimestampInternal(absl::Time timestamp,
                                         absl::TimeZone timezone,
                                         DateTimestampPart part,
                                         int64_t interval, absl::Time* output,
                                         bool* had_overflow) {
  GOOGLESQL_RETURN_IF_ERROR(CheckValidAddTimestampPart(part, false /* is_legacy */));
  if (part == DAY) {
    // For TIMESTAMP_ADD(), the DAY interval is equivalent to 24 HOURs.
    part = HOUR;
    int64_t new_interval;
    if (!Multiply<int64_t>(interval, 24, &new_interval, kNoError)) {
      *had_overflow = true;
      return MakeEvalError()
             << "TIMESTAMP_ADD interval value  " << interval << " at "
             << DateTimestampPart_Name(part) << " precision causes overflow";
    }
    interval = new_interval;
  }
  return AddDuration(timestamp, interval, part, timezone, output, had_overflow);
}

static absl::Status AddTimestampNanos(int64_t nanos, absl::TimeZone timezone,
                                      DateTimestampPart part, int64_t interval,
                                      int64_t* output);

absl::StatusOr<TimestampScale> ToTimestampScale(DateTimestampPart part) {
  switch (part) {
    case SECOND:
      return kSeconds;
    case MILLISECOND:
      return kMilliseconds;
    case MICROSECOND:
      return kMicroseconds;
    case NANOSECOND:
      return kNanoseconds;
    case PICOSECOND:
      return kPicoseconds;
    default:
      return MakeEvalError()
             << "Cannot convert DateTimestampPart "
             << DateTimestampPart_Name(part) << " to TimestampScale";
  }
}

static absl::StatusOr<int64_t> GetNumSecondsPerUnit(DateTimestampPart part) {
  switch (part) {
    case DAY:
      return kNaiveNumSecondsPerDay;
    case HOUR:
      return kNaiveNumSecondsPerHour;
    case MINUTE:
      return kNaiveNumSecondsPerMinute;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Cannot get number of seconds from DateTimestampPart "
                       << DateTimestampPart_Name(part);
  }
}

// Convert the given time duration specified by `interval` and `part` to a
// duration at the precision specified by `output_scale`.
// Examples:
// 1. interval=1, part=SECOND, output_scale=MILLISECOND, returns 1000.
// 2. interval=1000, part=MILLISECOND, output_scale=SECOND, returns 1.
template <class T, typename = std::enable_if_t<std::is_same_v<T, int64_t> ||
                                               std::is_same_v<T, absl::int128>>>
static absl::StatusOr<T> RescaleInterval(T interval, DateTimestampPart part,
                                         TimestampScale output_scale) {
  DateTimestampPart actual_part = part;
  T actual_interval = interval;

  switch (part) {
    case DAY:
    case HOUR:
    case MINUTE: {
      // We need to multiply DAY, HOUR, MINUTE by an appropriate factor to
      // convert them into SECOND.
      GOOGLESQL_ASSIGN_OR_RETURN(int64_t factor, GetNumSecondsPerUnit(part));
      if (!Multiply<T>(interval, factor, &actual_interval, kNoError)) {
        return MakeEvalError()
               << "TIMESTAMP_ADD interval value  " << interval << " at "
               << DateTimestampPart_Name(part) << " precision causes overflow";
      }
      actual_part = SECOND;
      break;
    }
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND:
    case PICOSECOND:
      // No-op for precision at SECOND and higher.
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Cannot convert DateTimestampPart "
                       << DateTimestampPart_Name(part) << " to TimestampScale";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(TimestampScale scale, ToTimestampScale(actual_part));
  return ConvertTimestampInterval(actual_interval, scale, output_scale);
}

// Add interval to Timestamp second, millisecond, microsecond, or nanosecond.
// The caller must verify that the input timestamp is valid.
// Returns error status if the output timestamp is out of range or overflow
// occurs during computation.
// TODO: Migrate this to the Picoseconds-based implementation.
static absl::Status AddTimestampInternal(int64_t timestamp,
                                         TimestampScale scale,
                                         absl::TimeZone timezone,
                                         DateTimestampPart part,
                                         int64_t interval, int64_t* output) {
  // Expected invariant.
  ABSL_DCHECK(IsValidTimestamp(timestamp, scale));

  GOOGLESQL_RETURN_IF_ERROR(CheckValidAddTimestampPart(part, false /* is_legacy */));

  // For scale == kNanoseconds, call AddTimestampNanos().
  if (scale == kNanoseconds) {
    return AddTimestampNanos(timestamp, timezone, part, interval, output);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(int64_t new_interval,
                   RescaleInterval<int64_t>(interval, part, scale));

  if (!Add<int64_t>(timestamp, new_interval, output, kNoError) ||
      !IsValidTimestamp(*output, scale)) {
    return MakeAddTimestampOverflowError(timestamp, part, interval, scale,
                                         timezone);
  }
  return absl::OkStatus();
}

// Adds an interval to a nanosecond timestamp value.  AddTimestampInternal
// interprets the interval value at the target precision, and adds it to
// the input timestamp.  Unfortunately for nanoseconds timestamps, interpreting
// the interval value at nanoseconds precision can overflow an int64 so
// we handle this case separately.  So we perform the addition at microsecond
// precision first, then add the nanosecond parts back in.
static absl::Status AddTimestampNanos(int64_t nanos, absl::TimeZone timezone,
                                      DateTimestampPart part, int64_t interval,
                                      int64_t* output) {
  if (part == NANOSECOND) {
    if (!Add<int64_t>(nanos, interval, output, kNoError)) {
      return MakeEvalError()
             << "Adding " << interval << " NANOs to TIMESTAMP_NANOS value "
             << nanos << " causes overflow";
    }
  } else {
    int64_t micros = nanos / powers_of_ten[3];
    int32_t nano_remains = nanos % powers_of_ten[3];
    int64_t micros_out;
    GOOGLESQL_RETURN_IF_ERROR(AddTimestampInternal(micros, kMicroseconds, timezone, part,
                                         interval, &micros_out));
    *output = micros_out * 1000l + nano_remains;
    // Given that the micros value is valid, the resulting nanoseconds
    // value must also be valid.  Check this invariant.
    ABSL_DCHECK(IsValidTimestamp(*output, kNanoseconds));
  }
  return absl::OkStatus();
}

template <typename T>
static void NarrowTimestampIfPossible(T* timestamp, TimestampScale* scale) {
  static_assert(
      std::is_same<T, int64_t>::value || std::is_same<T, absl::int128>::value,
      "T must be either int64_t or absl::int128");
  while (*timestamp % 1000 == 0) {
    switch (*scale) {
      case kSeconds:
        // Seconds do not narrow.
        return;
      case kMilliseconds:
        *scale = kSeconds;
        break;
      case kMicroseconds:
        *scale = kMilliseconds;
        break;
      case kNanoseconds:
        *scale = kMicroseconds;
        break;
      case kPicoseconds:
        *scale = kNanoseconds;
        break;
    }
    *timestamp /= 1000;
  }
}

// Formats the timestamp, represented as <base_time> + <sub_nanoseconds>, as a
// string.  When <sub_nanoseconds> is empty, the timestamp to be formatted has
// nanosecond precision. Otherwise, the timestamp to be formatted has picosecond
// precision.
//
// REQUIRES: When <sub_nanoseconds> has value, the value is in the range
//   [0, 999].
static absl::Status FormatTimestampToStringInternal(
    absl::string_view format_string, absl::Time base_time,
    std::optional<uint32_t> sub_nanoseconds, absl::TimeZone timezone,
    const internal_functions::ExpansionOptions expansion_options,
    std::string* output) {
  if (!IsValidTime(base_time)) {
    return MakeEvalError() << "Invalid timestamp value: "
                           << absl::ToUnixMicros(base_time);
  }
  output->clear();
  absl::TimeZone normalized_timezone =
      internal_functions::GetNormalizedTimeZone(base_time, timezone);
  std::string updated_format_string;
  // We handle %Z, %Q, and %J here instead of passing them through to
  // FormatTime() because GoogleSQL behavior is different than FormatTime()
  // behavior.
  GOOGLESQL_RETURN_IF_ERROR(internal_functions::ExpandPercentZQJ(
      format_string, base_time, normalized_timezone, expansion_options,
      &updated_format_string));

  if (sub_nanoseconds.has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(updated_format_string,
                     internal_functions::AddPicosecondsInFormatString(
                         updated_format_string, *sub_nanoseconds));
  }

  *output =
      absl::FormatTime(updated_format_string, base_time, normalized_timezone);
  if (expansion_options.truncate_tz) {
    // If ":00" appears at the end, remove it.  This is consistent with
    // Postgres.
    if (absl::EndsWith(*output, ":00")) {
      output->erase(output->size() - 3);
    }
  }
  return absl::OkStatus();
}

static absl::Status FormatTimestampToStringInternal(
    absl::string_view format_string, absl::Time base_time,
    absl::TimeZone timezone,
    const internal_functions::ExpansionOptions expansion_options,
    std::string* output) {
  return FormatTimestampToStringInternal(format_string, base_time,
                                         /*sub_nanoseconds=*/std::nullopt,
                                         timezone, expansion_options, output);
}

static absl::Status FormatTimestampToStringInternal(
    absl::string_view format_string, const PicoTime& time,
    absl::TimeZone timezone,
    const internal_functions::ExpansionOptions expansion_options,
    std::string* output) {
  return FormatTimestampToStringInternal(format_string, time.ToAbslTime(),
                                         time.SubNanoseconds(), timezone,
                                         expansion_options, output);
}

static absl::Status ConvertTimestampToStringInternal(
    int64_t timestamp, TimestampScale scale, absl::TimeZone timezone,
    bool truncate_trailing_zeros, std::string* out) {
  // When converting timestamp to string the result has 0, 3, or 6 digits
  // with trailing sets of three zeros truncated.
  if (truncate_trailing_zeros) {
    NarrowTimestampIfPossible(&timestamp, &scale);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(const absl::Time base_time,
                   MakeTimeFromInt64(timestamp, scale));
  // Note that the DefaultTimestampFormatStr does not use %Q or %J, so the
  // 'expand_quarter' and 'expand_iso_dayofyear' settings in ExpansionOptions
  // are not relevant.
  return FormatTimestampToStringInternal(
      DefaultTimestampFormatStr(scale), base_time, timezone,
      /*expansion_options=*/{.truncate_tz = true}, out);
}

absl::StatusOr<absl::Weekday> GetFirstWeekDayOfWeek(DateTimestampPart part) {
  switch (part) {
    case WEEK:
      return absl::Weekday::sunday;
    case ISOWEEK:
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
    default:
      return MakeEvalError()
             << "Unexpected date part " << DateTimestampPart_Name(part);
  }
}

// Does not do bounds checking. base_time (in the given timezone)
// must be guaranteed valid.
// TODO: b/412458859 - Change to non templated int64_t output.
template <typename Output>
static absl::Status ExtractFromTimestampInternal(DateTimestampPart part,
                                                 absl::Time base_time,
                                                 absl::TimeZone timezone,
                                                 Output* output) {
  const absl::TimeZone::CivilInfo info = timezone.At(base_time);

  switch (part) {
    case YEAR:
      // Given contract of this method, year must be 'small'.
      *output = static_cast<Output>(info.cs.year());
      break;
    case MONTH:
      *output = info.cs.month();
      break;
    case DAY:
      *output = info.cs.day();
      break;
    case DAYOFWEEK:
      *output = internal_functions::DayOfWeekIntegerSunToSat1To7(
          absl::GetWeekday(info.cs));
      break;
    case DAYOFYEAR:
      *output = absl::GetYearDay(absl::CivilDay(info.cs));
      break;
    case QUARTER:
      *output = (info.cs.month() - 1) / 3 + 1;
      break;
    case DATE: {
      const int32_t date = CivilDayToEpochDays(absl::CivilDay(info.cs));
      if (!IsValidDate(date)) {
        // Error handling.
        std::string time_str;
        if (ConvertTimestampToString(base_time, kNanoseconds, timezone,
                                     &time_str)
                .ok()) {
          return MakeEvalError()
                 << "Invalid date extracted from timestamp " << time_str;
        }
        // Most likely should never happen.
        return MakeEvalError() << "Invalid date extracted from timestamp "
                               << absl::FormatTime(base_time, timezone);
      }
      *output = date;
      break;
    }
    case WEEK:  // _SUNDAY
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      const absl::CivilDay first_calendar_day_of_year(info.cs.year(), 1, 1);

      GOOGLESQL_ASSIGN_OR_RETURN(const absl::Weekday weekday,
                       GetFirstWeekDayOfWeek(part));
      const absl::CivilDay effective_first_day_of_year =
          NextWeekdayOrToday(first_calendar_day_of_year, weekday);

      const absl::CivilDay day(info.cs);
      if (day < effective_first_day_of_year) {
        *output = 0;
      } else {
        // cast is safe, guaranteed to be less than 52.
        *output =
            static_cast<int32_t>(((day - effective_first_day_of_year) / 7) + 1);
      }
      break;
    }
    case ISOYEAR:
      // cast is safe, year "guaranteed" safe by method contract.
      *output = static_cast<Output>(GetIsoYear(absl::CivilDay(info.cs)));
      break;
    case ISOWEEK: {
      *output = GetIsoWeek(absl::CivilDay(info.cs));
      break;
    }
    case HOUR:
      *output = info.cs.hour();
      break;
    case MINUTE:
      *output = info.cs.minute();
      break;
    case SECOND:
      *output = info.cs.second();
      break;
    case MILLISECOND:
      // cast is safe, guaranteed to be less than 1 thousand.
      *output = static_cast<Output>(absl::ToInt64Milliseconds(info.subsecond));
      break;
    case MICROSECOND:
      // cast is safe, guaranteed to be less than 1 million.
      *output = static_cast<Output>(absl::ToInt64Microseconds(info.subsecond));
      break;
    case NANOSECOND:
      // cast is safe, guaranteed to be less than 1 billion.
      *output = static_cast<Output>(absl::ToInt64Nanoseconds(info.subsecond));
      break;
    default:
      // PICOSECOND is not supported for absl::Time.
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
  return absl::OkStatus();
}

static absl::Status MakeAddDateOverflowError(int32_t date,
                                             DateTimestampPart part,
                                             int64_t interval) {
  return MakeEvalError() << "Adding " << interval << " "
                         << DateTimestampPart_Name(part) << " to date "
                         << DateErrorString(date) << " causes overflow";
}

static absl::Status MakeSubDateOverflowError(int32_t date,
                                             DateTimestampPart part,
                                             int64_t interval) {
  return MakeEvalError() << "Subtracting " << interval << " "
                         << DateTimestampPart_Name(part) << " from date "
                         << DateErrorString(date) << " causes overflow";
}

static absl::Status MakeAddDatetimeOverflowError(const DatetimeValue& datetime,
                                                 DateTimestampPart part,
                                                 int64_t interval) {
  return MakeEvalError() << "Adding " << interval << " "
                         << DateTimestampPart_Name(part) << " to datetime "
                         << datetime.DebugString() << " causes overflow";
}

static absl::Status MakeSubDatetimeOverflowError(const DatetimeValue& datetime,
                                                 DateTimestampPart part,
                                                 int64_t interval) {
  return MakeEvalError() << "Subtracting " << interval << " "
                         << DateTimestampPart_Name(part) << " from datetime "
                         << datetime.DebugString() << " causes overflow";
}

static std::string MakeInvalidTypedStrErrorMsg(absl::string_view type_name,
                                               absl::string_view str,
                                               TimestampScale scale) {
  return absl::StrCat(
      "Invalid ", type_name, " string \"", UserString(str), "\"",
      (scale != kMicroseconds
           ? absl::StrCat(" (scale ", TimestampScale_Name(scale), ")")
           : ""));
}

static std::string MakeInvalidTimestampStrErrorMsg(
    absl::string_view timestamp_str, TimestampScale scale) {
  return MakeInvalidTypedStrErrorMsg("timestamp", timestamp_str, scale);
}

static absl::Status TruncateDateImpl(int32_t date, DateTimestampPart part,
                                     bool enforce_range, int32_t* output) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  absl::CivilDay civil_day = EpochDaysToCivilDay(date);
  switch (part) {
    case YEAR:
      *output = CivilDayToEpochDays(absl::CivilDay(civil_day.year(), 1, 1));
      break;
    case ISOYEAR: {
      *output = CivilDayToEpochDays(
          date_time_util_internal::GetFirstDayOfIsoYear(civil_day));
      break;
    }
    case MONTH: {
      *output = CivilDayToEpochDays(
          absl::CivilDay(civil_day.year(), civil_day.month(), 1));
      break;
    }
    case QUARTER: {
      int m = civil_day.month();
      m = (m - 1) / 3 * 3 + 1;
      *output = CivilDayToEpochDays(absl::CivilDay(civil_day.year(), m, 1));
      break;
    }
    case WEEK:
    case ISOWEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      GOOGLESQL_ASSIGN_OR_RETURN(const absl::Weekday first_day_of_week,
                       GetFirstWeekDayOfWeek(part));
      *output =
          CivilDayToEpochDays(PrevWeekdayOrToday(civil_day, first_day_of_week));
      break;
    }
    case DAY:
      *output = date;  // nothing to truncate.
      break;
    default:
      return MakeEvalError() << "Unsupported DateTimestampPart "
                             << DateTimestampPart_Name(part);
  }
  // Truncating to WEEK and WEEK(<WEEKDAY>) can result in a date that is out
  // of bounds (i.e., before 0001-01-01), so we check the truncated date
  // result here. The other date parts do not have the potential to underflow,
  // but we validate the result anyway as a sanity check.
  if (enforce_range && !IsValidDate(*output)) {
    return MakeEvalError() << "Truncating date " << DateErrorString(date)
                           << " to " << DateTimestampPartToSQL(part)
                           << " resulted in an out of range date value: "
                           << *output;
  }
  return absl::OkStatus();
}

absl::Status LastDayOfDate(int32_t date, DateTimestampPart part,
                           int32_t* output) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  absl::CivilDay civil_day = EpochDaysToCivilDay(date);
  // For YEAR, MONTH, QUARTER, last_day is implemented by first calling AddDate
  // to add 1 to the part of the date, then truncate the date in the part,
  // finally subtract 1 day
  // However, AddDate may overflow for some input dates that can return a valid
  // last_day result, so those cases are handled first.
  if (civil_day.year() == 9999) {
    if (part == YEAR || (part == MONTH && civil_day.month() == 12) ||
        (part == QUARTER && civil_day.month() >= 10 &&
         civil_day.month() <= 12)) {
      *output = CivilDayToEpochDays(absl::CivilDay(9999, 12, 31));
      return absl::OkStatus();
    }
  }
  switch (part) {
    case YEAR:
    case MONTH:
    case QUARTER: {
      GOOGLESQL_RETURN_IF_ERROR(AddDate(date, part, 1, &date));
      GOOGLESQL_RETURN_IF_ERROR(
          TruncateDateImpl(date, part, /*enforce_range=*/false, &date));
      GOOGLESQL_RETURN_IF_ERROR(SubDate(date, DAY, 1, output));
      break;
    }
    case ISOYEAR: {
      // last day of ISOYEAR could out of range if the input is 9999,
      // the last day of ISOYEAR 9999 is 10000-01-02
      *output = CivilDayToEpochDays(
          date_time_util_internal::GetLastDayOfIsoYear(civil_day));
      break;
    }
    case WEEK:
    case ISOWEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      GOOGLESQL_ASSIGN_OR_RETURN(const absl::Weekday first_day_of_week,
                       GetFirstWeekDayOfWeek(part));
      *output = CivilDayToEpochDays(
                    PrevWeekdayOrToday(civil_day, first_day_of_week)) +
                6;
      break;
    }
    default:
      return MakeEvalError() << "Unsupported DateTimestampPart "
                             << DateTimestampPart_Name(part);
  }
  // ISO_YEAR, WEEK and WEEK(<WEEKDAY>) as part can result in a date that is out
  // of bounds (i.e., after 9999-12-31), so we check the last day
  // result here. The other date parts do not have the potential to underflow,
  // but we validate the result anyway as a sanity check.
  if (!IsValidDate(*output)) {
    return MakeEvalError() << "Last day of date " << DateErrorString(date)
                           << " to " << DateTimestampPartToSQL(part)
                           << " resulted in an out of range date value: "
                           << *output;
  }
  return absl::OkStatus();
}

absl::Status LastDayOfDatetime(const DatetimeValue& datetime,
                               DateTimestampPart part, int32_t* output) {
  int32_t date;
  GOOGLESQL_RETURN_IF_ERROR(ExtractFromDatetime(DATE, datetime, &date));
  GOOGLESQL_RETURN_IF_ERROR(LastDayOfDate(date, part, output));
  return absl::OkStatus();
}

absl::Status NextDayOfDate(int32_t date, absl::string_view dow,
                           int32_t* output) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(absl::Weekday weekday, GetWeekdayFromString(dow));
  absl::CivilDay civil_day = EpochDaysToCivilDay(date);
  absl::CivilDay next_day = absl::NextWeekday(civil_day, weekday);
  *output = CivilDayToEpochDays(next_day);
  if (!IsValidDate(*output)) {
    return MakeEvalError() << "Next day of " << DateErrorString(date)
                           << " resulted in an out of range date value: "
                           << *output;
  }
  return absl::OkStatus();
}

absl::Status NextDayOfDatetime(const DatetimeValue& datetime,
                               absl::string_view dow, int32_t* output) {
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  int32_t date;
  GOOGLESQL_RETURN_IF_ERROR(
      ConstructDate(datetime.Year(), datetime.Month(), datetime.Day(), &date));
  return NextDayOfDate(date, dow, output);
}

absl::Status AddMonths(int32_t date, int64_t months, int32_t* output) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  int32_t new_date;
  GOOGLESQL_RETURN_IF_ERROR(AddDate(date, MONTH, months, &new_date));
  absl::CivilDay civil_day = EpochDaysToCivilDay(date);
  if (IsLastDayOfTheMonth(static_cast<int>(civil_day.year()), civil_day.month(),
                          civil_day.day())) {
    GOOGLESQL_RETURN_IF_ERROR(LastDayOfDate(new_date, MONTH, output));
  } else {
    *output = new_date;
  }
  return absl::OkStatus();
}

// Implement the DATETIME version in terms of the DATE version.
// The time doesn't change.
absl::Status AddMonths(const DatetimeValue& datetime, int64_t months,
                       DatetimeValue* output) {
  int32_t date;
  TimeValue time;
  int32_t new_date;

  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }

  GOOGLESQL_RETURN_IF_ERROR(
      ConstructDate(datetime.Year(), datetime.Month(), datetime.Day(), &date));
  GOOGLESQL_RETURN_IF_ERROR(ExtractTimeFromDatetime(datetime, &time));
  GOOGLESQL_RETURN_IF_ERROR(AddMonths(date, months, &new_date));
  return ConstructDatetime(new_date, time, output);
}

absl::Status MonthsBetween(int32_t date_end, int32_t date_start,
                           double* output) {
  if (!IsValidDate(date_end)) {
    return MakeEvalError() << "Invalid date_end value: " << date_end;
  }
  if (!IsValidDate(date_start)) {
    return MakeEvalError() << "Invalid date_start value: " << date_start;
  }

  absl::CivilDay civil_end = EpochDaysToCivilDay(date_end);
  absl::CivilDay civil_start = EpochDaysToCivilDay(date_start);

  double whole_months = (civil_end.year() - civil_start.year()) * 12 +
                        (civil_end.month() - civil_start.month());
  double fractional_months = 0.0;

  // Include a fractional portion unless the days are the same or both
  // end-of-month.
  bool end_is_last_day = IsLastDayOfTheMonth(
      static_cast<int>(civil_end.year()), civil_end.month(), civil_end.day());
  bool start_is_last_day =
      IsLastDayOfTheMonth(static_cast<int>(civil_start.year()),
                          civil_start.month(), civil_start.day());
  if (!(civil_end.day() == civil_start.day() ||
        (end_is_last_day && start_is_last_day))) {
    fractional_months = (civil_end.day() - civil_start.day()) / 31.0;
  }

  *output = whole_months + fractional_months;
  return absl::OkStatus();
}

absl::Status MonthsBetween(const DatetimeValue& datetime_end,
                           const DatetimeValue& datetime_start,
                           double* output) {
  if (!datetime_end.IsValid()) {
    return MakeEvalError() << "Invalid datetime_end value: "
                           << datetime_end.DebugString();
  }
  if (!datetime_start.IsValid()) {
    return MakeEvalError() << "Invalid datetime_start value: "
                           << datetime_start.DebugString();
  }

  double whole_months = (datetime_end.Year() - datetime_start.Year()) * 12 +
                        (datetime_end.Month() - datetime_start.Month());

  // If days are the same or both end-of-month return a whole number.
  bool end_is_last_day = IsLastDayOfTheMonth(
      datetime_end.Year(), datetime_end.Month(), datetime_end.Day());
  bool start_is_last_day = IsLastDayOfTheMonth(
      datetime_start.Year(), datetime_start.Month(), datetime_start.Day());
  if (datetime_end.Day() == datetime_start.Day() ||
      (end_is_last_day && start_is_last_day)) {
    *output = whole_months;
    return absl::OkStatus();
  }

  // Otherwise calculate the fractional month based on the day and time.
  absl::Duration duration_end =
      (absl::Hours(datetime_end.Hour()) + absl::Minutes(datetime_end.Minute()) +
       absl::Seconds(datetime_end.Second()) +
       absl::Nanoseconds(datetime_end.Nanoseconds()));
  absl::Duration duration_start =
      (absl::Hours(datetime_start.Hour()) +
       absl::Minutes(datetime_start.Minute()) +
       absl::Seconds(datetime_start.Second()) +
       absl::Nanoseconds(datetime_start.Nanoseconds()));

  double whole_days = (datetime_end.Day() - datetime_start.Day());
  double fractional_days =
      absl::ToDoubleHours(duration_end - duration_start) / 24.0;
  double fractional_months = (whole_days + fractional_days) / 31.0;

  *output = whole_months + fractional_months;
  return absl::OkStatus();
}

static absl::Status TimestampTruncAtLeastMinute(absl::Time timestamp,
                                                TimestampScale scale,
                                                absl::TimeZone timezone,
                                                DateTimestampPart part,
                                                absl::Time* output) {
  const absl::TimeZone::CivilInfo info = timezone.At(timestamp);

  // It is assumed (but not validated) that the input <timestamp> is valid.
  // Given a valid timestamp, it is expected that reconstructing the timestamp
  // from the truncated parts will not fail, so we GOOGLESQL_RET_CHECK the results here.
  // We will always check the resulting truncated timestamp for validity before
  // returning.
  switch (part) {
    case YEAR:
      GOOGLESQL_RET_CHECK(TimestampFromParts(info.cs.year(), 1 /* month */, 1 /* mday */,
                                   0 /* hour */, 0 /* minute */, 0 /* second */,
                                   0 /* subsecond */, scale, timezone, output));
      break;
    case ISOYEAR: {
      absl::CivilDay day = absl::CivilDay(info.cs);
      if (!IsValidCivilDay(day)) {
        return MakeEvalError()
               << "Invalid date value: " << CivilDayToEpochDays(day);
      }
      absl::CivilDay iso_civil_day =
          date_time_util_internal::GetFirstDayOfIsoYear(day);

      GOOGLESQL_RET_CHECK(TimestampFromParts(iso_civil_day.year(), iso_civil_day.month(),
                                   iso_civil_day.day(), 0 /* hour */,
                                   0 /* minute */, 0 /* second */,
                                   0 /* subsecond */, scale, timezone, output));
      break;
    }
    case QUARTER:
      GOOGLESQL_RET_CHECK(TimestampFromParts(
          info.cs.year(), (info.cs.month() - 1) / 3 * 3 + 1, 1 /* mday */,
          0 /* hour */, 0 /* minute */, 0 /* second */, 0 /* subsecond */,
          scale, timezone, output));
      break;
    case MONTH:
      GOOGLESQL_RET_CHECK(TimestampFromParts(info.cs.year(), info.cs.month(),
                                   1 /* mday */, 0 /* hour */, 0 /* minute */,
                                   0 /* second */, 0 /* subsecond */, scale,
                                   timezone, output));
      break;
    case WEEK:
    case ISOWEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      // Convert to a CivilDay...
      GOOGLESQL_ASSIGN_OR_RETURN(absl::Weekday weekday, GetFirstWeekDayOfWeek(part));
      absl::CivilDay week_truncated_day =
          PrevWeekdayOrToday(absl::CivilDay(info.cs), weekday);
      if (week_truncated_day.year() < 1) {
        return MakeEvalError()
               << "Truncating " << TimestampErrorString(timestamp, timezone)
               << " to the nearest week causes overflow";
      }
      GOOGLESQL_RET_CHECK(TimestampFromParts(
          week_truncated_day.year(), week_truncated_day.month(),
          week_truncated_day.day(), 0 /* hour */, 0 /* minute */,
          0 /* second */, 0 /* subsecond */, scale, timezone, output));
      break;
    }
    case DAY:
      GOOGLESQL_RET_CHECK(TimestampFromParts(info.cs.year(), info.cs.month(),
                                   info.cs.day(), 0 /* hour */, 0 /* minute */,
                                   0 /* second */, 0 /* subsecond */, scale,
                                   timezone, output));
      break;
    case HOUR:
    case MINUTE: {
      // For HOUR or MINUTE truncation of a timestamp, we identify the
      // minute/second/subsecond parts as viewed via the specified time zone,
      // and subtract that interval from the timestamp.
      //
      // This operation is performed by adding the timezone seconds offset from
      // UTC (given the input timestamp) to 'align' the minute/seconds parts
      // for truncation.  The minute/seconds parts can then be effectively
      // truncated from the value.  We then re-adjust the result timestamp by
      // the seconds offset value.
      //
      // Note that with these semantics, when you truncate a timestamp based
      // on a specified time zone and then convert the resulting truncated
      // timestamp to a civil time in that same time zone, you are not
      // guaranteed to get a civil time that is at the hour or minute boundary
      // in that time zone.  For instance, this will happen when the truncation
      // crosses a DST change and the DST change is not a (multiple of an)
      // hour.  See the unit tests for specific examples.

      // Note that as documented, usage of CivilInfo.offset is discouraged.
      const int64_t seconds_offset_east_of_UTC = info.offset;

      int64_t timestamp_seconds;
      // Note: This conversion truncates the subseconds part
      GOOGLESQL_RET_CHECK(FromTime(timestamp, kSeconds, &timestamp_seconds))
          << "timestamp: " << timestamp
          << ", scale: seconds, timestamp_seconds: " << timestamp_seconds;

      // Adjust the timestamp by the time zone offset.
      timestamp_seconds += seconds_offset_east_of_UTC;

      // Truncate seconds from the timestamp to the hour/minute boundary
      const int64_t truncate_granularity = (part == HOUR ? 3600 : 60);
      int64_t num_seconds_to_truncate =
          timestamp_seconds % truncate_granularity;
      if (num_seconds_to_truncate < 0) {
        num_seconds_to_truncate += truncate_granularity;
      }
      timestamp_seconds -= num_seconds_to_truncate;

      // Re-adjust the timestamp by the time zone offset.
      timestamp_seconds -= seconds_offset_east_of_UTC;
      auto status_or_time = MakeTimeFromInt64(timestamp_seconds, kSeconds);
      if (!status_or_time.ok()) {
        return status_or_time.status();
      }
      *output = status_or_time.value();
      break;
    }
    case DATE:
    case DAYOFWEEK:
    case DAYOFYEAR:
      return MakeEvalError() << "Unsupported DateTimestampPart "
                             << DateTimestampPart_Name(part);
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND:
      GOOGLESQL_RET_CHECK_FAIL() << "Should not reach here for part="
                       << DateTimestampPart_Name(part);
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
  // Validate that the truncated result is a valid time.
  if (!IsValidTime(*output)) {
    return MakeEvalError() << "Truncating to the nearest "
                           << DateTimestampPart_Name(part)
                           << " causes underflow at timezone "
                           << timezone.name();
  }
  return absl::OkStatus();
}

static absl::Status TimestampTruncImpl(int64_t timestamp, TimestampScale scale,
                                       NewOrLegacyTimestampType timestamp_type,
                                       absl::TimeZone timezone,
                                       DateTimestampPart part,
                                       int64_t* output) {
  if (!IsValidTimestamp(timestamp, scale)) {
    return MakeEvalError() << "Invalid timestamp value: " << timestamp;
  }
  switch (scale) {
    case kSeconds:
      GOOGLESQL_RET_CHECK_EQ(LEGACY_TIMESTAMP_TYPE, timestamp_type);
      if (part == SECOND) {
        *output = timestamp;  // nothing to truncate;
        return absl::OkStatus();
      }
      if (part == MILLISECOND || part == MICROSECOND || part == NANOSECOND) {
        return MakeEvalError()
               << "Cannot truncate a TIMESTAMP_SECONDS value to "
               << DateTimestampPart_Name(part);
      }
      break;
    case kMilliseconds:
      GOOGLESQL_RET_CHECK_EQ(LEGACY_TIMESTAMP_TYPE, timestamp_type);
      if (part == SECOND) {
        // Truncating subsecond of a timestamp before epoch
        // (1970-01-01 00:00:00) to another subsecond needs special
        // treatment.  This is because the subsecond is represented as 10's
        // complement (negative) for timestamp before epoch.
        // From example:
        //   1969-12-31 23:59:59.123456  -->
        //        -876544 (TIMESTAMP_MICRO)
        //   1969-12-31 23:59:59.123  -->
        //        -877 (TIMESTAMP_MILLIS)
        // The truncation needs to compensate by -1 after the division if the
        // truncated part is not zero.
        *output = timestamp / powers_of_ten[3];
        if (timestamp < 0 && timestamp % powers_of_ten[3] != 0) {
          *output -= 1;
        }
        *output *= powers_of_ten[3];
        return absl::OkStatus();
      }
      if (part == MILLISECOND) {
        *output = timestamp;  // nothing to truncate;
        return absl::OkStatus();
      }
      if (part == MICROSECOND || part == NANOSECOND) {
        return MakeEvalError() << "Cannot truncate a TIMESTAMP_MILLIS value to "
                               << DateTimestampPart_Name(part);
      }
      break;
    case kMicroseconds:
      // This could be either the new TIMESTAMP type or the legacy
      // TIMESTAMP_MICROS type.
      if (part == SECOND) {
        *output = timestamp / powers_of_ten[6];
        if (timestamp < 0 && timestamp % powers_of_ten[6] != 0) {
          *output -= 1;
        }
        *output *= powers_of_ten[6];
        return absl::OkStatus();
      }
      if (part == MILLISECOND) {
        *output = timestamp / powers_of_ten[3];
        if (timestamp < 0 && timestamp % powers_of_ten[3] != 0) {
          *output -= 1;
        }
        *output *= powers_of_ten[3];
        return absl::OkStatus();
      }
      if (part == MICROSECOND) {
        *output = timestamp;  // nothing to truncate;
        return absl::OkStatus();
      }
      if (part == NANOSECOND) {
        return MakeEvalError()
               << "Cannot truncate a "
               << (timestamp_type == LEGACY_TIMESTAMP_TYPE ? "TIMESTAMP_MICROS"
                                                           : "TIMESTAMP")
               << " value to " << DateTimestampPart_Name(part);
      }
      break;
    case kNanoseconds:
      GOOGLESQL_RET_CHECK_EQ(LEGACY_TIMESTAMP_TYPE, timestamp_type);
      if (part == SECOND) {
        *output = timestamp / powers_of_ten[9];
        if (timestamp < 0 && timestamp % powers_of_ten[9] != 0) {
          *output -= 1;
        }
        *output *= powers_of_ten[9];
        return absl::OkStatus();
      }
      if (part == MILLISECOND) {
        *output = timestamp / powers_of_ten[6];
        if (timestamp < 0 && timestamp % powers_of_ten[6] != 0) {
          *output -= 1;
        }
        *output *= powers_of_ten[6];
        return absl::OkStatus();
      }
      if (part == MICROSECOND) {
        *output = timestamp / powers_of_ten[3];
        if (timestamp < 0 && timestamp % powers_of_ten[3] != 0) {
          *output -= 1;
        }
        *output *= powers_of_ten[3];
        return absl::OkStatus();
      }
      if (part == NANOSECOND) {
        *output = timestamp;  // nothing to truncate;
        return absl::OkStatus();
      }
      break;
    case kPicoseconds:
      return MakeEvalError() << "Picoseconds is not supported";
  }
  GOOGLESQL_ASSIGN_OR_RETURN(const absl::Time base_time,
                   MakeTimeFromInt64(timestamp, scale));
  absl::Time output_base_time;
  GOOGLESQL_RETURN_IF_ERROR(TimestampTruncAtLeastMinute(base_time, scale, timezone, part,
                                              &output_base_time));
  // In this case we know we have a valid timestamp input to the function, so
  // the truncated timestamp must be valid as well.
  GOOGLESQL_RET_CHECK(FromTime(output_base_time, scale, output))
      << "base_time: " << base_time
      << "\noutput_base_time: " << output_base_time << ", scale: " << scale
      << ", output: " << *output;
  return absl::OkStatus();
}

bool IsValidTimestamp(int64_t timestamp, TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return timestamp >= googlesql::types::kTimestampMin / 1000000 &&
             timestamp <= googlesql::types::kTimestampMax / 1000000;
    case kMilliseconds:
      return timestamp >= googlesql::types::kTimestampMin / 1000 &&
             timestamp <= googlesql::types::kTimestampMax / 1000;
    case kMicroseconds:
      return timestamp >= googlesql::types::kTimestampMin &&
             timestamp <= googlesql::types::kTimestampMax;
    case kNanoseconds:
      // There is no invalid range for int64 timestamps with nanoseconds scale
      return true;
    case kPicoseconds:
      // There is no invalid range for int64 timestamps with picoseconds scale
      return true;
  }
}

bool IsValidTime(absl::Time time) {
  static constexpr absl::Time kTimeMin =
      absl::FromUnixMicros(googlesql::types::kTimestampMin);
  static constexpr absl::Time kTimeMax =
      absl::FromUnixMicros(googlesql::types::kTimestampMax + 1);
  return time >= kTimeMin && time < kTimeMax;
}

bool IsValidTimeZone(int timezone_minutes_offset) {
  static constexpr int64_t kTimezoneOffsetMin = -60 * 14;
  static constexpr int64_t kTimezoneOffsetMax = 60 * 14;
  return timezone_minutes_offset >= kTimezoneOffsetMin &&
         timezone_minutes_offset <= kTimezoneOffsetMax;
}

absl::StatusOr<absl::Time> MakeTimeFromInt64(int64_t timestamp,
                                             TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return absl::FromUnixSeconds(timestamp);
    case kMilliseconds:
      return absl::FromUnixMillis(timestamp);
    case kMicroseconds:
      return absl::FromUnixMicros(timestamp);
    case kNanoseconds:
      return absl::FromUnixNanos(timestamp);
    case kPicoseconds:
      return MakeEvalError() << "TimestampScale kPicoseconds is not supported";
  }
}

absl::Time MakeTime(int64_t timestamp, TimestampScale scale) {
  auto time_or_status = MakeTimeFromInt64(timestamp, scale);
  if (time_or_status.ok()) {
    return time_or_status.value();
  }

  // This method should never be called with scale == kPicoseconds.
  ABSL_LOG(FATAL) << time_or_status.status();  // Crash OK
}

bool FromTime(absl::Time base_time, TimestampScale scale, int64_t* output) {
  switch (scale) {
    case kSeconds:
      *output = absl::ToUnixSeconds(base_time);
      break;
    case kMilliseconds:
      *output = absl::ToUnixMillis(base_time);
      break;
    case kMicroseconds:
      *output = absl::ToUnixMicros(base_time);
      break;
    case kNanoseconds: {
      if (base_time <
              absl::FromUnixNanos(std::numeric_limits<int64_t>::lowest()) ||
          base_time >
              absl::FromUnixNanos(std::numeric_limits<int64_t>::max())) {
        return false;
      }
      *output = absl::ToUnixNanos(base_time);
      break;
    }
    case kPicoseconds:
      // Picoseconds is not supported.
      return false;
  }

  return functions::IsValidTimestamp(*output, scale);
}

absl::Status ConvertDateToString(int32_t date, std::string* out) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  absl::CivilDay day = EpochDaysToCivilDay(date);
  *out = absl::StrFormat("%04d-%02d-%02d", day.year(), day.month(), day.day());
  return absl::OkStatus();
}

absl::StatusOr<absl::CivilDay> ConvertDateToCivilDay(int32_t date) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  return EpochDaysToCivilDay(date);
}

absl::Status ConvertTimestampToStringWithoutTruncation(int64_t timestamp,
                                                       TimestampScale scale,
                                                       absl::TimeZone timezone,
                                                       std::string* out) {
  return ConvertTimestampToStringInternal(
      timestamp, scale, timezone, false /* truncate_trailing_zeros */, out);
}

absl::Status ConvertTimestampToStringWithoutTruncation(
    int64_t timestamp, TimestampScale scale, absl::string_view timezone_string,
    std::string* out) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertTimestampToStringWithoutTruncation(timestamp, scale, timezone,
                                                   out);
}

absl::Status ConvertTimestampToStringWithoutTruncation(const PicoTime& input,
                                                       absl::TimeZone timezone,
                                                       std::string* out) {
  // Note that the DefaultTimestampFormatStr does not use %Q or %J, so the
  // 'expand_quarter' and 'expand_iso_dayofyear' settings in ExpansionOptions
  // are not relevant.
  return FormatTimestampToStringInternal(
      DefaultTimestampFormatStr(kPicoseconds), input, timezone,
      {.truncate_tz = true}, out);
}

// ConvertTimestampToStringWithTruncation is a popular function, because it is
// used for CASTs from TIMESTAMP to STRING, and in STRING conversion. This
// function is optimized for this conversion by hardcoding the rules of applying
// "%E4Y-%m-%d %H:%M:%E(0|3|6)S%Ez" formatting.
absl::Status ConvertTimestampMicrosToStringWithTruncation(
    int64_t timestamp, absl::TimeZone timezone, std::string* out) {
  // The custom formatting code for %E4Y below will produce wrong result when
  // civil year value is 10000, which might happen for timestamps close to the
  // kTimestampMax in time zones with positive offset from UTC. Fall back to the
  // default implementation when year value is large enough - the cutoff doesn't
  // need to be precise as this won't be happening often in practice.
  constexpr int64_t kFourDigitYearCutOff =
      types::kTimestampMax - kNaiveNumMicrosPerDay;
  if (ABSL_PREDICT_FALSE(timestamp >= kFourDigitYearCutOff)) {
    return ConvertTimestampToStringInternal(timestamp, kMicroseconds, timezone,
                                            /*truncate_trailing_zeros=*/true,
                                            out);
  }

  absl::Time time = absl::FromUnixMicros(timestamp);
  if (ABSL_PREDICT_FALSE(!IsValidTime(time))) {
    return MakeEvalError() << "Invalid timestamp value: " << timestamp;
  }
  absl::TimeZone normalized_timezone =
      internal_functions::GetNormalizedTimeZone(time, timezone);

  absl::TimeZone::CivilInfo info = normalized_timezone.At(time);

  // YYYY-mm-dd HH:MM:SS.ssssss+oo:oo
  // 01234567890123456789012345678901
  // 0         1         2         3
  // Maximum possible string size for the result.
  static constexpr size_t kFormatYMDHMSSize = 32;
  out->resize(kFormatYMDHMSSize);
  char* data = out->data();

  // YYYY-mm-dd HH:MM:SS.ssssss+oooo
  // 0123456789012345678901234567890
  // 0         1         2
  // Carefully put characters corresponding to different parts of formatted
  // string to the right positions.
  data[0] = info.cs.year() / 1000 + '0';
  data[1] = (info.cs.year() % 1000) / 100 + '0';
  data[2] = (info.cs.year() % 100) / 10 + '0';
  data[3] = info.cs.year() % 10 + '0';
  data[4] = '-';
  data[5] = info.cs.month() / 10 + '0';
  data[6] = info.cs.month() % 10 + '0';
  data[7] = '-';
  data[8] = info.cs.day() / 10 + '0';
  data[9] = info.cs.day() % 10 + '0';
  data[10] = ' ';
  data[11] = info.cs.hour() / 10 + '0';
  data[12] = info.cs.hour() % 10 + '0';
  data[13] = ':';
  data[14] = info.cs.minute() / 10 + '0';
  data[15] = info.cs.minute() % 10 + '0';
  data[16] = ':';
  data[17] = info.cs.second() / 10 + '0';
  data[18] = info.cs.second() % 10 + '0';

  // Deal with subsecond truncation to 0, 3 or 6 digits.
  size_t pos = 19;
  int64_t sub_seconds = timestamp % 1000000;
  if (sub_seconds < 0) {
    sub_seconds += 1000000;
  }
  if (sub_seconds > 0) {
    data[19] = '.';
    if (sub_seconds % 1000 > 0) {
      for (int i = 5; i >= 0; i--) {
        data[20 + i] = (sub_seconds % 10) + '0';
        sub_seconds /= 10;
      }
      pos += 7;
    } else {
      sub_seconds /= 1000;
      for (int i = 2; i >= 0; i--) {
        data[20 + i] = (sub_seconds % 10) + '0';
        sub_seconds /= 10;
      }
      pos += 4;
    }
  }

  bool positive_offset;
  int32_t hour_offset;
  int32_t minute_offset;
  internal_functions::GetSignHourAndMinuteTimeZoneOffset(
      info, &positive_offset, &hour_offset, &minute_offset);
  if (positive_offset) {
    data[pos++] = '+';
  } else {
    data[pos++] = '-';
  }
  // Write timezone offset using HH:mm format
  data[pos++] = hour_offset / 10 + '0';
  data[pos++] = hour_offset % 10 + '0';
  if (minute_offset > 0) {
    data[pos++] = ':';
    data[pos++] = minute_offset / 10 + '0';
    data[pos++] = minute_offset % 10 + '0';
  }
  // Initially we resized result for maximum possible size, here we set actual
  // size.
  out->resize(pos);
  return absl::OkStatus();
}

absl::Status ConvertTimestampToStringWithTruncation(int64_t timestamp,
                                                    TimestampScale scale,
                                                    absl::TimeZone timezone,
                                                    std::string* out) {
  return ConvertTimestampToStringInternal(
      timestamp, scale, timezone, true /* truncate_trailing_zeros */, out);
}

absl::Status ConvertTimestampToStringWithTruncation(
    int64_t timestamp, TimestampScale scale, absl::string_view timezone_string,
    std::string* out) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertTimestampToStringWithTruncation(timestamp, scale, timezone,
                                                out);
}

absl::Status ConvertTimestampToStringWithTruncation(const PicoTime& timestamp,
                                                    absl::TimeZone timezone,
                                                    std::string* out) {
  // When converting timestamp to string the result has 0, 3, 6, 9 or 12 digits
  // with trailing sets of three zeros truncated.
  TimestampScale scale = kPicoseconds;
  absl::int128 picos = timestamp.ToUnixPicos();
  NarrowTimestampIfPossible(&picos, &scale);

  // Note that the DefaultTimestampFormatStr does not use %Q or %J, so the
  // 'expand_quarter' and 'expand_iso_dayofyear' settings in ExpansionOptions
  // are not relevant.
  return FormatTimestampToStringInternal(
      DefaultTimestampFormatStr(scale), timestamp, timezone,
      /*expansion_options=*/{.truncate_tz = true}, out);
}

absl::Status ValidateTimeScale(TimestampScale scale) {
  switch (scale) {
    case kMicroseconds:
    case kNanoseconds:
    case kPicoseconds:
      return absl::OkStatus();
    case kSeconds:
    case kMilliseconds:
      GOOGLESQL_RET_CHECK_FAIL()
          << "Only kMicroseconds, kNanoseconds and kPicoseconds are acceptable "
             "values for scale";
  }
}

absl::Status ConvertTimeToString(TimeValue time, TimestampScale scale,
                                 std::string* out) {
  // TODO: Support Time with picosecond precision.
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  if (!time.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time.DebugString();
  }
  int64_t fraction_second =
      (scale == kMicroseconds ? time.Microseconds() : time.Nanoseconds());
  NarrowTimestampIfPossible(&fraction_second, &scale);
  std::unique_ptr<absl::ParsedFormat<'d', 'd', 'd', 'd'>> format =
      absl::ParsedFormat<'d', 'd', 'd', 'd'>::NewAllowIgnored(
          DefaultTimeFormatStr(scale));
  GOOGLESQL_RET_CHECK(format != nullptr);
  *out = absl::StrFormat(*format, time.Hour(), time.Minute(), time.Second(),
                         fraction_second);
  return absl::OkStatus();
}

absl::Status ConvertDatetimeToString(DatetimeValue datetime,
                                     TimestampScale scale, std::string* out) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  int64_t fraction_second = (scale == kMicroseconds ? datetime.Microseconds()
                                                    : datetime.Nanoseconds());
  NarrowTimestampIfPossible(&fraction_second, &scale);
  auto format =
      absl::ParsedFormat<'d', 'd', 'd', 'd', 'd', 'd', 'd'>::NewAllowIgnored(
          DefaultDatetimeFormatStr(scale));
  GOOGLESQL_RET_CHECK(format != nullptr);
  *out = absl::StrFormat(*format, datetime.Year(), datetime.Month(),
                         datetime.Day(), datetime.Hour(), datetime.Minute(),
                         datetime.Second(), fraction_second);
  return absl::OkStatus();
}

// Sanitizes the <format_string> so that any character in the
// <elements_to_escape> will be escaped and pass through as is. If an element
// should be escaped, then any extension applied on it with %E or %O will also
// be escaped.
static void SanitizeFormat(absl::string_view format_string,
                           const char* elements_to_escape, std::string* out) {
  const char* cur = format_string.data();
  const char* pending = cur;
  const char* end = cur + format_string.size();

  while (cur != end) {
    // Moves cur to the next percent sign.
    while (cur != end && *cur != '%') ++cur;

    // Span the sequential percent signs.
    const char* percent = cur;
    while (cur != end && *cur == '%') ++cur;

    if (cur != pending) {
      out->append(pending, cur - pending);
      pending = cur;
    }

    // Loop unless we have an unescaped percent.
    if (cur == end || (cur - percent) % 2 == 0) {
      continue;
    }

    // Escape width modifier.
    while (cur != end && absl::ascii_isdigit(*cur)) ++cur;

    // Checks if the format should be escaped
    if (cur != end && strchr(elements_to_escape, *cur)) {
      out->push_back('%');  // Escape
      out->append(pending, ++cur - pending);
      pending = cur;
      continue;
    }

    if (cur != end) {
      if ((*cur != 'E' && *cur != 'O') || ++cur == end) {
        out->push_back(*pending++);
        continue;
      }
      if (*pending == 'E') {
        // Check %E extensions.
        if (strchr(elements_to_escape, *cur) ||
            // If %S (second) should be escaped, then %E#S and %E*S should also
            // be escaped.
            (strchr(elements_to_escape, 'S') &&
             ((*cur == '*' || absl::ascii_isdigit(*cur)) && ++cur != end &&
              *cur == 'S')) ||
            // If %Y (year) should be escaped, then %E4Y should also be escaped.
            (strchr(elements_to_escape, 'Y') && *cur == '4' && ++cur != end &&
             *cur == 'Y')) {
          cur++;
          out->push_back('%');  // Escape
        }
      } else if (*pending == 'O') {
        // Check %O extensions.
        if (strchr(elements_to_escape, *cur)) {
          cur++;
          out->push_back('%');  // Escape
        }
      }
    }

    if (cur != pending) {
      out->append(pending, cur - pending);
      pending = cur;
    }
  }
}

// Sanitizes the <format_string> to only allow ones applicable to the date type
// and produces a valid format string for date in <out>.
// For non-date related formats such as Hour/Minute/Second/Timezone etc.,
// escapes them to pass through as is.
static void SanitizeDateFormat(absl::string_view format_string,
                               std::string* out) {
  return SanitizeFormat(format_string, "cHIklMPpRrSsTXZz", out);
}

// Similar to SanitizeDateFormat, but escape those format elements for
// Year/Month/Week/Day/Timezone etc..
static void SanitizeTimeFormat(absl::string_view format_string,
                               std::string* out) {
  return SanitizeFormat(format_string, "AaBbhCcDdeFGgjmQsUuVWwxYyZz", out);
}

// Similar to SanitizeDateFormat, but escape the format elements for Timezone.
static void SanitizeDatetimeFormat(absl::string_view format_string,
                                   std::string* out) {
  return SanitizeFormat(format_string, "Zz", out);
}

absl::Status FormatDateToString(
    absl::string_view format_string, int64_t date,
    const FormatDateTimestampOptions& format_options, std::string* out) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  std::string date_format_string;
  SanitizeDateFormat(format_string, &date_format_string);
  // Treats it as a timestamp at midnight on that date and invokes the
  // format_timestamp function.
  int64_t date_timestamp = static_cast<int64_t>(date) * kNaiveNumMicrosPerDay;
  GOOGLESQL_RETURN_IF_ERROR(FormatTimestampToString(date_format_string, date_timestamp,
                                          absl::UTCTimeZone(), format_options,
                                          out));
  return absl::OkStatus();
}

absl::Status FormatDateToString(absl::string_view format_string, int32_t date,
                                std::string* out) {
  return FormatDateToString(format_string, date,
                            {.expand_Q = true, .expand_J = false}, out);
}

absl::Status FormatDatetimeToStringWithOptions(
    absl::string_view format_string, const DatetimeValue& datetime,
    const FormatDateTimestampOptions& format_options, std::string* out) {
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  std::string datetime_format_string;
  SanitizeDatetimeFormat(format_string, &datetime_format_string);
  absl::Time datetime_in_utc =
      absl::UTCTimeZone().At(datetime.ConvertToCivilSecond()).pre;
  datetime_in_utc += absl::Nanoseconds(datetime.Nanoseconds());

  GOOGLESQL_RETURN_IF_ERROR(FormatTimestampToString(datetime_format_string,
                                          datetime_in_utc, absl::UTCTimeZone(),
                                          format_options, out));
  return absl::OkStatus();
}

absl::Status FormatDatetimeToString(absl::string_view format_string,
                                    const DatetimeValue& datetime,
                                    std::string* out) {
  // By default, expand %Q but not %J.
  return FormatDatetimeToStringWithOptions(
      format_string, datetime, {.expand_Q = true, .expand_J = false}, out);
}

absl::Status FormatTimeToString(absl::string_view format_string,
                                const TimeValue& time, std::string* out) {
  if (!time.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time.DebugString();
  }
  std::string time_format_string;
  SanitizeTimeFormat(format_string, &time_format_string);
  absl::Time time_in_epoch_day =
      absl::UTCTimeZone()
          .At(absl::CivilSecond(1970, 1, 1, time.Hour(), time.Minute(),
                                time.Second()))
          .pre;
  time_in_epoch_day += absl::Nanoseconds(time.Nanoseconds());

  // TIME does not support %Q or %J, so 'expand_Q' and 'expand_J' are not
  // relevant.
  GOOGLESQL_RETURN_IF_ERROR(FormatTimestampToString(
      time_format_string, time_in_epoch_day, absl::UTCTimeZone(),
      {.expand_Q = false, .expand_J = false}, out));
  return absl::OkStatus();
}

absl::Status FormatTimestampToString(
    absl::string_view format_str, absl::Time timestamp, absl::TimeZone timezone,
    const FormatDateTimestampOptions& format_options, std::string* out) {
  return FormatTimestampToStringInternal(
      format_str, timestamp, timezone,
      {.truncate_tz = false,
       .expand_quarter = format_options.expand_Q,
       .expand_iso_dayofyear = format_options.expand_J},
      out);
}

absl::Status FormatTimestampToString(
    absl::string_view format_str, absl::Time timestamp,
    absl::string_view timezone_string,
    const FormatDateTimestampOptions& format_options, std::string* out) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return FormatTimestampToString(format_str, timestamp, timezone,
                                 format_options, out);
}

absl::Status FormatTimestampToString(
    absl::string_view format_str, int64_t timestamp,
    absl::string_view timezone_string,
    const FormatDateTimestampOptions& format_options, std::string* out) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  GOOGLESQL_ASSIGN_OR_RETURN(auto time, MakeTimeFromInt64(timestamp, kMicroseconds));
  return FormatTimestampToString(format_str, time, timezone, format_options,
                                 out);
}

absl::Status FormatTimestampToString(
    absl::string_view format_str, int64_t timestamp, absl::TimeZone timezone,
    const FormatDateTimestampOptions& format_options, std::string* out) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto time, MakeTimeFromInt64(timestamp, kMicroseconds));
  return FormatTimestampToString(format_str, time, timezone, format_options,
                                 out);
}

absl::Status FormatTimestampToString(absl::string_view format_str,
                                     int64_t timestamp, absl::TimeZone timezone,
                                     std::string* out) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto time, MakeTimeFromInt64(timestamp, kMicroseconds));
  return FormatTimestampToStringInternal(format_str, time, timezone,
                                         {.truncate_tz = false,
                                          .expand_quarter = true,
                                          .expand_iso_dayofyear = false},
                                         out);
}

absl::Status FormatTimestampToString(absl::string_view format_str,
                                     int64_t timestamp,
                                     absl::string_view timezone_string,
                                     std::string* out) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return FormatTimestampToString(format_str, timestamp, timezone, out);
}

absl::Status FormatTimestampToString(absl::string_view format_str,
                                     absl::Time timestamp,
                                     absl::TimeZone timezone,
                                     std::string* out) {
  return FormatTimestampToStringInternal(format_str, timestamp, timezone,
                                         {.truncate_tz = false,
                                          .expand_quarter = true,
                                          .expand_iso_dayofyear = false},
                                         out);
}

absl::Status FormatTimestampToString(absl::string_view format_string,
                                     absl::Time timestamp,
                                     absl::string_view timezone_string,
                                     std::string* out) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return FormatTimestampToStringInternal(format_string, timestamp, timezone,
                                         {.truncate_tz = false,
                                          .expand_quarter = true,
                                          .expand_iso_dayofyear = false},
                                         out);
}

absl::Status FormatTimestampToString(absl::string_view format_string,
                                     const PicoTime& timestamp,
                                     absl::TimeZone timezone,
                                     std::string* out) {
  return FormatTimestampToStringInternal(format_string, timestamp, timezone,
                                         {.truncate_tz = false,
                                          .expand_quarter = true,
                                          .expand_iso_dayofyear = false},
                                         out);
}

absl::Status FormatTimestampToString(
    absl::string_view format_string, const PicoTime& timestamp,
    absl::TimeZone timezone, const FormatDateTimestampOptions& format_options,
    std::string* out) {
  return FormatTimestampToStringInternal(
      format_string, timestamp, timezone,
      {.truncate_tz = false,
       .expand_quarter = format_options.expand_Q,
       .expand_iso_dayofyear = format_options.expand_J},
      out);
}

absl::Status ConvertTimestampToString(absl::Time input, TimestampScale scale,
                                      absl::TimeZone timezone,
                                      std::string* output) {
  NarrowTimestampScaleIfPossible(input, &scale);
  return FormatTimestampToStringInternal(DefaultTimestampFormatStr(scale),
                                         input, timezone,
                                         {.truncate_tz = true,
                                          .expand_quarter = true,
                                          .expand_iso_dayofyear = false},
                                         output);
}

absl::Status ConvertTimestampToString(absl::Time input, TimestampScale scale,
                                      absl::string_view timezone_string,
                                      std::string* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertTimestampToString(input, scale, timezone, output);
}

absl::Status ConvertTimestampToString(const PicoTime& input,
                                      absl::TimeZone timezone,
                                      std::string* output) {
  return ConvertTimestampToStringWithTruncation(input, timezone, output);
}

absl::Status MakeTimeZone(absl::string_view timezone_string,
                          absl::TimeZone* timezone) {
  // An empty time zone is an error.  There is no inherent default.
  if (timezone_string.empty()) {
    return MakeEvalError() << "Invalid empty time zone";
  }

  // First try to parse the time zone as of the canonical form (+HH:MM) since
  // that is not supported by the time library.
  char timezone_sign;
  int timezone_hour;
  int timezone_minute;
  if (ParseTimeZoneOffsetFormat(timezone_string, &timezone_sign, &timezone_hour,
                                &timezone_minute)) {
    int64_t seconds_offset;
    if (!TimeZonePartsToOffset(timezone_sign, timezone_hour, timezone_minute,
                               kSeconds, &seconds_offset)) {
      return MakeEvalError() << "Invalid time zone: " << timezone_string;
    }
    *timezone = absl::FixedTimeZone(seconds_offset);
    return absl::OkStatus();
  }

  // Otherwise, try to look the time zone up from by name.
  return FindTimeZoneByName(timezone_string, timezone);
}

absl::Status ConvertStringToDate(absl::string_view str, int32_t* date) {
  int year = 0, month = 0, day = 0, idx = 0;
  if (!ParseStringToDateParts(str, &idx, &year, &month, &day) ||
      !IsValidDay(year, month, day)) {
    return MakeEvalError() << "Invalid date: '" << UserString(str) << "'";
  }
  absl::CivilDay civil_day;
  if (!MakeDate(year, month, day, &civil_day)) {
    return MakeEvalError() << "Date value out of range: '" << UserString(str)
                           << "'";
  }
  *date = CivilDayToEpochDays(civil_day);
  ABSL_DCHECK(IsValidDate(*date));  // Invariant if MakeDate() succeeds.
  return absl::OkStatus();
}

absl::StatusOr<int32_t> ConvertCivilDayToDate(absl::CivilDay civil_day) {
  const int32_t date = CivilDayToEpochDays(civil_day);
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Date value out of range: '" << civil_day << "'";
  }
  return date;
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      int64_t* timestamp) {
  return ConvertStringToTimestamp(str, default_timezone, scale, true,
                                  timestamp);
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      bool allow_tz_in_str,
                                      int64_t* timestamp) {
  absl::Time base_time;
  GOOGLESQL_RETURN_IF_ERROR(ConvertStringToTimestamp(str, default_timezone, scale,
                                           allow_tz_in_str, &base_time));
  if (!FromTime(base_time, scale, timestamp)) {
    return MakeEvalError() << MakeInvalidTimestampStrErrorMsg(str, scale);
  }
  if (IsValidTimestamp(*timestamp, scale)) {
    return absl::OkStatus();
  }
  return MakeEvalError() << MakeInvalidTimestampStrErrorMsg(str, scale);
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::string_view default_timezone_string,
                                      TimestampScale scale,
                                      bool allow_tz_in_str,
                                      int64_t* timestamp) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(default_timezone_string, &timezone));
  return ConvertStringToTimestamp(str, timezone, scale, allow_tz_in_str,
                                  timestamp);
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::string_view default_timezone_string,
                                      TimestampScale scale,
                                      int64_t* timestamp) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(default_timezone_string, &timezone));
  return ConvertStringToTimestamp(str, timezone, scale, timestamp);
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      bool allow_tz_in_str,
                                      absl::Time* output) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  int64_t subsecond = 0;
  bool string_includes_timezone = false;
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(ParseStringToTimestampParts(
      str, scale, &year, &month, &day, &hour, &minute, &second, &subsecond,
      &timezone, &string_includes_timezone));
  if (!IsValidDay(year, month, day) ||
      !IsValidTimeOfDay(hour, minute, second)) {
    return MakeEvalError() << MakeInvalidTimestampStrErrorMsg(str, scale);
  }
  if (string_includes_timezone && !allow_tz_in_str) {
    return MakeEvalError() << "Timezone is not allowed in \"" << UserString(str)
                           << "\"";
  }
  if (!string_includes_timezone) {
    timezone = default_timezone;
  }
  const absl::CivilSecond cs(year, month, day, hour, minute, second);
  GOOGLESQL_ASSIGN_OR_RETURN(auto duration, MakeDuration(subsecond, scale));
  *output = timezone.At(cs).pre + duration;
  if (!IsValidTime(*output)) {
    return MakeEvalError() << MakeInvalidTimestampStrErrorMsg(str, scale);
  }
  return absl::OkStatus();
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      bool allow_tz_in_str, PicoTime* output,
                                      int* precision) {
  return ConvertStringToTimestamp(str, default_timezone, kPicoseconds,
                                  allow_tz_in_str, output, precision);
}

absl::Status ConvertStringToTimestamp(absl::string_view str,
                                      absl::TimeZone default_timezone,
                                      TimestampScale scale,
                                      bool allow_tz_in_str, PicoTime* output,
                                      int* precision) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  int64_t subsecond = 0;
  bool string_includes_timezone = false;
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(ParseStringToTimestampParts(
      str, scale, &year, &month, &day, &hour, &minute, &second, &subsecond,
      &timezone, &string_includes_timezone, precision));
  if (!IsValidDay(year, month, day) ||
      !IsValidTimeOfDay(hour, minute, second)) {
    return MakeEvalError() << MakeInvalidTimestampStrErrorMsg(str, scale);
  }
  if (string_includes_timezone && !allow_tz_in_str) {
    return MakeEvalError() << "Timezone is not allowed in \"" << UserString(str)
                           << "\"";
  }
  if (!string_includes_timezone) {
    timezone = default_timezone;
  }
  const absl::CivilSecond cs(year, month, day, hour, minute, second);
  int64_t picoseconds;
  if (scale == kPicoseconds) {
    picoseconds = subsecond;
  } else if (scale == kNanoseconds) {
    picoseconds = subsecond * 1'000;
  } else if (scale == kMicroseconds) {
    picoseconds = subsecond * 1'000'000;
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Unexpected scale " << scale
                     << " when converting STRING to TIMESTAMP.";
  }

  auto status_or_time = PicoTime::Create(timezone.At(cs).pre, picoseconds);
  if (!status_or_time.ok()) {
    return MakeEvalError() << MakeInvalidTimestampStrErrorMsg(str, scale);
  }
  *output = status_or_time.value();
  return absl::OkStatus();
}

absl::Status ConvertStringToTime(absl::string_view str, TimestampScale scale,
                                 TimeValue* output) {
  // TODO: Support Time with picosecond precision.
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  int hour = 0, minute = 0, second = 0;
  int64_t subsecond = 0;
  int idx = 0;
  if (!ParseStringToTimeParts(str, scale, &idx, &hour, &minute, &second,
                              &subsecond) ||
      !IsValidTimeOfDay(hour, minute, second)) {
    return MakeEvalError() << MakeInvalidTypedStrErrorMsg("time", str, scale);
  }

  // Because we have checked for validity of the fields already, the only
  // situation where the input will be still be invalid here is due to the leap
  // second. In that case, we clear its sub-second part. See the comments in
  // header for explanation.
  if (second == 60) {
    subsecond = 0;
  }

  if (scale == kMicroseconds) {
    *output =
        TimeValue::FromHMSAndMicrosNormalized(hour, minute, second, subsecond);
  } else {
    *output =
        TimeValue::FromHMSAndNanosNormalized(hour, minute, second, subsecond);
  }
  return absl::OkStatus();
}

absl::Status ConvertStringToDatetime(absl::string_view str,
                                     TimestampScale scale,
                                     DatetimeValue* output) {
  // TODO: Support DateTime with picosecond precision.
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  int64_t subsecond = 0;
  if (!ParseStringToDatetimeParts(str, scale, &year, &month, &day, &hour,
                                  &minute, &second, &subsecond) ||
      !IsValidDay(year, month, day) ||
      !IsValidTimeOfDay(hour, minute, second)) {
    return MakeEvalError() << MakeInvalidTypedStrErrorMsg("datetime", str,
                                                          scale);
  }

  // Because we have checked for validity of the fields already, the only
  // situation where the input will be still be invalid here is due to the leap
  // second. In that case, we clear its sub-second part. See the comments in
  // header for explanation.
  if (second == 60) {
    subsecond = 0;
  }

  if (scale == kMicroseconds) {
    *output = DatetimeValue::FromYMDHMSAndMicrosNormalized(
        year, month, day, hour, minute, second, subsecond);
  } else {
    *output = DatetimeValue::FromYMDHMSAndNanosNormalized(
        year, month, day, hour, minute, second, subsecond);
  }
  // Leap second is acceptable as input, but invalid for TimeValue,
  // normalize it to the first second of the next minute.
  // The only fail case in this context is "9999-12-31 23:59:60".
  if (!output->IsValid()) {
    return MakeEvalError() << MakeInvalidTypedStrErrorMsg("datetime", str,
                                                          scale);
  }

  return absl::OkStatus();
}

absl::Status ConstructDate(int year, int month, int day, int32_t* output) {
  absl::CivilDay civil_day;
  if (MakeDate(year, month, day, &civil_day)) {
    *output = CivilDayToEpochDays(civil_day);
    return absl::OkStatus();
  }
  return MakeEvalError() << "Input calculates to invalid date: "
                         << absl::StrFormat("%04d-%02d-%02d", year, month, day);
}

absl::Status ConstructTime(int hour, int minute, int second,
                           TimeValue* output) {
  if (IsValidTimeOfDay(hour, minute, second)) {
    *output = TimeValue::FromHMSAndMicrosNormalized(hour, minute, second,
                                                    /*microsecond=*/0);
    return absl::OkStatus();
  }
  return MakeEvalError() << "Input calculates to invalid time: "
                         << absl::StrFormat("%02d:%02d:%02d", hour, minute,
                                            second);
}

absl::Status ConstructDatetime(int year, int month, int day, int hour,
                               int minute, int second, DatetimeValue* output) {
  if (IsValidDay(year, month, day) && IsValidTimeOfDay(hour, minute, second)) {
    *output = DatetimeValue::FromYMDHMSAndMicrosNormalized(
        year, month, day, hour, minute, second, /*microsecond=*/0);
    if (output->IsValid()) {
      return absl::OkStatus();
    }
  }
  return MakeEvalError() << "Input calculates to invalid datetime: "
                         << absl::StrFormat("%04d-%02d-%02d %04d:%02d:%02d",
                                            year, month, day, hour, minute,
                                            second);
}

absl::Status ConstructDatetime(int32_t date, const TimeValue& time,
                               DatetimeValue* output) {
  if (IsValidDate(date) && time.IsValid()) {
    absl::CivilDay day = EpochDaysToCivilDay(date);
    *output = DatetimeValue::FromYMDHMSAndNanosNormalized(
        // We check "IsValidDate", guaranteed safe.
        // cast is safe, checked with IsValidDate.
        static_cast<int32_t>(day.year()), day.month(), day.day(), time.Hour(),
        time.Minute(), time.Second(), time.Nanoseconds());
    if (output->IsValid()) {
      return absl::OkStatus();
    }
  }
  return MakeEvalError() << "Input calculates to invalid datetime: "
                         << DateErrorString(date) << " " << time.DebugString();
}

// TODO: b/412458859 - revert to int64_t* output and ExtractFromTimestamp name.
template <typename Output>
absl::Status ExtractFromTimestampImpl(DateTimestampPart part, int64_t timestamp,
                                      TimestampScale scale,
                                      absl::TimeZone timezone, Output* output) {
  if (!IsValidTimestamp(timestamp, scale)) {
    return MakeEvalError() << "Invalid timestamp value: " << timestamp;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto time, MakeTimeFromInt64(timestamp, scale));
  return ExtractFromTimestampInternal(part, time, timezone, output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale, absl::TimeZone timezone,
                                  int32_t* output) {
  return ExtractFromTimestampImpl(part, timestamp, scale, timezone, output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale, absl::TimeZone timezone,
                                  int64_t* output) {
  return ExtractFromTimestampImpl(part, timestamp, scale, timezone, output);
}

// TODO: b/412458859 - revert to int64_t* output and ExtractFromTimestamp name.
template <typename Output>
absl::Status ExtractFromTimestampImpl(DateTimestampPart part, int64_t timestamp,
                                      TimestampScale scale,
                                      absl::string_view timezone_string,
                                      Output* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ExtractFromTimestampImpl(part, timestamp, scale, timezone, output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale,
                                  absl::string_view timezone_string,
                                  int32_t* output) {
  return ExtractFromTimestampImpl(part, timestamp, scale, timezone_string,
                                  output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, int64_t timestamp,
                                  TimestampScale scale,
                                  absl::string_view timezone_string,
                                  int64_t* output) {
  return ExtractFromTimestampImpl(part, timestamp, scale, timezone_string,
                                  output);
}

// TODO: b/412458859 - revert to int64_t* output and ExtractFromTimestamp name.
template <typename Output>
absl::Status ExtractFromTimestampImpl(DateTimestampPart part,
                                      absl::Time base_time,
                                      absl::TimeZone timezone, Output* output) {
  if (IsValidTime(base_time)) {
    return ExtractFromTimestampInternal(part, base_time, timezone, output);
  }
  // Error handling.
  std::string time_str;
  if (ConvertTimestampToString(base_time, kNanoseconds, timezone, &time_str)
          .ok()) {
    return MakeEvalError() << "Invalid timestamp: " << time_str;
  }
  // Most likely should never happen.
  return MakeEvalError() << "Invalid timestamp: "
                         << absl::FormatTime(base_time, timezone);
}

// TODO: b/412458859 - revert to int64_t* output and ExtractFromTimestamp name.
absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::TimeZone timezone, int32_t* output) {
  return ExtractFromTimestampImpl(part, base_time, timezone, output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::TimeZone timezone, int64_t* output) {
  return ExtractFromTimestampImpl(part, base_time, timezone, output);
}

// TODO: b/412458859 - revert to int64_t* output and ExtractFromTimestamp name.
template <typename Output>
absl::Status ExtractFromTimestampImpl(DateTimestampPart part,
                                      absl::Time base_time,
                                      absl::string_view timezone_string,
                                      Output* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ExtractFromTimestampImpl(part, base_time, timezone, output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::string_view timezone_string,
                                  int32_t* output) {
  return ExtractFromTimestampImpl(part, base_time, timezone_string, output);
}

absl::Status ExtractFromTimestamp(DateTimestampPart part, absl::Time base_time,
                                  absl::string_view timezone_string,
                                  int64_t* output) {
  return ExtractFromTimestampImpl(part, base_time, timezone_string, output);
}

absl::StatusOr<int64_t> ExtractFromTimestamp(
    DateTimestampPart part, const PicoTime& timestamp,
    absl::string_view timezone_string) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ExtractFromTimestamp(part, timestamp, timezone);
}

template <typename Output>
absl::Status ExtractFromTimestampImpl(DateTimestampPart part,
                                      const PicoTime& timestamp,
                                      absl::TimeZone timezone, Output* output) {
  if (part == DateTimestampPart::PICOSECOND) {
    auto [seconds, picoseconds] = timestamp.SecondsAndPicoseconds();
    *output = picoseconds;
    return absl::OkStatus();
  }
  return ExtractFromTimestampImpl(part, timestamp.ToAbslTime(), timezone,
                                  output);
}

absl::StatusOr<int64_t> ExtractFromTimestamp(DateTimestampPart part,
                                             const PicoTime& timestamp,
                                             absl::TimeZone timezone) {
  int64_t output;
  GOOGLESQL_RETURN_IF_ERROR(ExtractFromTimestampImpl(part, timestamp, timezone, &output));
  return output;
}

absl::Status ExtractFromDate(DateTimestampPart part, int32_t date,
                             int32_t* output) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  absl::CivilDay day = EpochDaysToCivilDay(date);
  switch (part) {
    case YEAR:
      // Year for valid dates fits into int32
      *output = static_cast<int32_t>(day.year());
      break;
    case QUARTER:
      *output = (day.month() - 1) / 3 + 1;
      break;
    case MONTH:
      *output = day.month();
      break;
    case DAY:
      *output = day.day();
      break;
    case ISOYEAR:
      // Year for valid dates fits into int32
      *output = static_cast<int32_t>(GetIsoYear(day));
      break;
    case WEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      const absl::CivilDay first_calendar_day_of_year(day.year(), 1, 1);
      GOOGLESQL_ASSIGN_OR_RETURN(const absl::Weekday weekday,
                       GetFirstWeekDayOfWeek(part));
      const absl::CivilDay effective_first_day_of_year =
          NextWeekdayOrToday(first_calendar_day_of_year, weekday);
      if (day < effective_first_day_of_year) {
        *output = 0;
      } else {
        // cast is safe, guaranteed to be less than 52.
        *output =
            static_cast<int32_t>(((day - effective_first_day_of_year) / 7) + 1);
      }
      break;
    }
    case ISOWEEK:
      *output = GetIsoWeek(day);
      break;
    case DAYOFWEEK:
      *output = internal_functions::DayOfWeekIntegerSunToSat1To7(
          absl::GetWeekday(day));
      break;
    case DAYOFYEAR:
      *output = absl::GetYearDay(day);
      break;
    case DATE:
    case HOUR:
    case MINUTE:
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " to extract from date";
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
  return absl::OkStatus();
}

// TODO: b/412458859 - Revert to non-templated int64_t and ExtractFromTime name.
template <typename Output>
absl::Status ExtractFromTimeImpl(DateTimestampPart part, const TimeValue& time,
                                 Output* output) {
  if (!time.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time.DebugString();
  }
  switch (part) {
    case YEAR:
    case MONTH:
    case DAY:
    case DAYOFWEEK:
    case DAYOFYEAR:
    case QUARTER:
    case DATE:
    case WEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY:
    case DATETIME:
    case TIME:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " to extract from time";
    case HOUR:
      *output = time.Hour();
      break;
    case MINUTE:
      *output = time.Minute();
      break;
    case SECOND:
      *output = time.Second();
      break;
    case MILLISECOND:
      *output = time.Microseconds() / 1000;
      break;
    case MICROSECOND:
      *output = time.Microseconds();
      break;
    case NANOSECOND:
      *output = time.Nanoseconds();
      break;
    // TODO: b/412458859 - Add PICOSECOND case.
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
  return absl::OkStatus();
}

// TODO: b/412458859 - Remove with int32_t output type dependencies.
absl::Status ExtractFromTime(DateTimestampPart part, const TimeValue& time,
                             int32_t* output) {
  return ExtractFromTimeImpl(part, time, output);
}

// TODO: b/412458859 - Remove after refactoring ExtractFromTimeImpl.
absl::Status ExtractFromTime(DateTimestampPart part, const TimeValue& time,
                             int64_t* output) {
  return ExtractFromTimeImpl(part, time, output);
}

// TODO: b/412458859 - Remove template parameter, use int64_t, revert name.
template <typename Output>
absl::Status ExtractFromDatetimeImpl(DateTimestampPart part,
                                     const DatetimeValue& datetime,
                                     Output* output) {
  GOOGLESQL_RET_CHECK(part != TIME)
      << "Use ExtractTimeFromDatetime() for extracting time from datetime";
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  switch (part) {
    case YEAR:
      *output = datetime.Year();
      break;
    case QUARTER:
      *output = (static_cast<Output>(datetime.Month()) - 1) / 3 + 1;
      break;
    case MONTH:
      *output = datetime.Month();
      break;
    case DAY:
      *output = datetime.Day();
      break;
    case HOUR:
      *output = datetime.Hour();
      break;
    case MINUTE:
      *output = datetime.Minute();
      break;
    case SECOND:
      *output = datetime.Second();
      break;
    case MILLISECOND:
      *output = static_cast<Output>(datetime.Microseconds()) / 1000;
      break;
    case MICROSECOND:
      *output = datetime.Microseconds();
      break;
    case NANOSECOND:
      *output = datetime.Nanoseconds();
      break;
    // TODO: b/412458859 - Add PICOSECOND case.
    case DATE:
      int32_t output32;
      GOOGLESQL_RETURN_IF_ERROR(ConstructDate(datetime.Year(), datetime.Month(),
                                    datetime.Day(), &output32));
      *output = static_cast<Output>(output32);
      break;
    case DAYOFWEEK:
      *output = internal_functions::DayOfWeekIntegerSunToSat1To7(
          absl::GetWeekday(absl::CivilDay(datetime.Year(), datetime.Month(),
                                          datetime.Day())));
      break;
    case DAYOFYEAR:
      *output = absl::GetYearDay(
          absl::CivilDay(datetime.Year(), datetime.Month(), datetime.Day()));
      break;
    case WEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      const absl::CivilDay first_calendar_day_of_year(datetime.Year(), 1, 1);

      GOOGLESQL_ASSIGN_OR_RETURN(const absl::Weekday weekday,
                       GetFirstWeekDayOfWeek(part));
      const absl::CivilDay effective_first_day_of_year =
          NextWeekdayOrToday(first_calendar_day_of_year, weekday);

      const absl::CivilDay day(datetime.Year(), datetime.Month(),
                               datetime.Day());
      if (day < effective_first_day_of_year) {
        *output = 0;
      } else {
        // cast is safe, guaranteed to be less than 52.
        *output =
            static_cast<Output>(((day - effective_first_day_of_year) / 7) + 1);
      }
      break;
    }
    case ISOYEAR:
      // cast is safe, year "guaranteed" safe by method contract.
      *output = static_cast<Output>(GetIsoYear(
          absl::CivilDay(datetime.Year(), datetime.Month(), datetime.Day())));
      break;
    case ISOWEEK:
      *output = GetIsoWeek(
          absl::CivilDay(datetime.Year(), datetime.Month(), datetime.Day()));
      break;
    case DATETIME:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " to extract from datetime";
    case TIME:
      // Should never reach this.
      GOOGLESQL_RET_CHECK_FAIL()
          << "Use ExtractTimeFromDatetime() for extracting time from datetime";
      break;
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
  return absl::OkStatus();
}

// TODO: b/412458859 - Remove with int32_t output type dependencies.
absl::Status ExtractFromDatetime(DateTimestampPart part,
                                 const DatetimeValue& datetime,
                                 int32_t* output) {
  return ExtractFromDatetimeImpl(part, datetime, output);
}

// TODO: b/412458859 - Remove after refactoring ExtractFromDatetimeImpl.
absl::Status ExtractFromDatetime(DateTimestampPart part,
                                 const DatetimeValue& datetime,
                                 int64_t* output) {
  return ExtractFromDatetimeImpl(part, datetime, output);
}

absl::Status ExtractTimeFromDatetime(const DatetimeValue& datetime,
                                     TimeValue* time) {
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  *time = TimeValue::FromHMSAndNanos(datetime.Hour(), datetime.Minute(),
                                     datetime.Second(), datetime.Nanoseconds());
  // Check should never fail because the fields taken from a valid datetime
  // should always be valid for a time value.
  GOOGLESQL_RET_CHECK(time->IsValid());
  return absl::OkStatus();
}

absl::Status ConvertDatetimeToTimestamp(const DatetimeValue& datetime,
                                        absl::TimeZone timezone,
                                        absl::Time* output) {
  if (datetime.IsValid()) {
    if (TimestampFromParts(datetime.Year(), datetime.Month(), datetime.Day(),
                           datetime.Hour(), datetime.Minute(),
                           datetime.Second(), datetime.Nanoseconds(),
                           kNanoseconds, timezone, output) &&
        IsValidTime(*output)) {
      return absl::OkStatus();
    }
    return MakeEvalError() << "Cannot convert Datetime "
                           << datetime.DebugString() << " at timezone "
                           << timezone.name() << " to a Timestamp";
  }
  return MakeEvalError() << "Invalid datetime: " << datetime.DebugString();
}

absl::Status ConvertDatetimeToTimestamp(const DatetimeValue& datetime,
                                        absl::string_view timezone_string,
                                        absl::Time* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertDatetimeToTimestamp(datetime, timezone, output);
}

absl::Status ConvertTimestampToDatetime(absl::Time base_time,
                                        absl::TimeZone timezone,
                                        DatetimeValue* output) {
  if (IsValidTime(base_time)) {
    const absl::TimeZone::CivilInfo info = timezone.At(base_time);
    *output = DatetimeValue::FromYMDHMSAndNanos(
        // cast is safe, since we check 'IsValidTime.
        static_cast<int32_t>(info.cs.year()), info.cs.month(), info.cs.day(),
        info.cs.hour(), info.cs.minute(), info.cs.second(),
        // cast is safe, guaranteed to be less than 1 billion.
        static_cast<int32_t>(info.subsecond / absl::Nanoseconds(1)));
    if (output->IsValid()) {
      return absl::OkStatus();
    }
    return MakeEvalError() << "Invalid Datetime " << output->DebugString()
                           << "extracted from timestamp "
                           << TimestampErrorString(base_time, timezone);
  }
  // Error handling.
  return MakeEvalError() << "Invalid timestamp: "
                         << TimestampErrorString(base_time, timezone);
}

absl::Status ConvertTimestampToDatetime(absl::Time base_time,
                                        absl::string_view timezone_string,
                                        DatetimeValue* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertTimestampToDatetime(base_time, timezone, output);
}

absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::TimeZone timezone,
                                    TimestampScale scale, TimeValue* output) {
  // TODO: Support Time with picosecond precision.
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  if (IsValidTime(base_time)) {
    const absl::TimeZone::CivilInfo info = timezone.At(base_time);
    if (scale == kNanoseconds) {
      *output = TimeValue::FromHMSAndNanos(
          info.cs.hour(), info.cs.minute(), info.cs.second(),
          // cast is safe, guaranteed less than 1 billion
          static_cast<int32_t>(absl::ToInt64Nanoseconds(info.subsecond)));
    } else {
      *output = TimeValue::FromHMSAndMicros(
          info.cs.hour(), info.cs.minute(), info.cs.second(),
          // cast is safe, guaranteed less than 1 million
          static_cast<int32_t>(absl::ToInt64Microseconds(info.subsecond)));
    }
    if (output->IsValid()) {
      return absl::OkStatus();
    }
    return MakeEvalError() << "Invalid Time " << output->DebugString()
                           << "extracted from timestamp "
                           << TimestampErrorString(base_time, timezone);
  }
  // Error handling.
  return MakeEvalError() << "Invalid timestamp: "
                         << TimestampErrorString(base_time, timezone);
}

absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::string_view timezone_string,
                                    TimestampScale scale, TimeValue* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertTimestampToTime(base_time, timezone, scale, output);
}

absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::TimeZone timezone,
                                    TimeValue* output) {
  return ConvertTimestampToTime(base_time, timezone, kNanoseconds, output);
}

absl::Status ConvertTimestampToTime(absl::Time base_time,
                                    absl::string_view timezone_string,
                                    TimeValue* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertTimestampToTime(base_time, timezone, kNanoseconds, output);
}

absl::Status ConvertDateToTimestamp(int32_t date, absl::TimeZone timezone,
                                    absl::Time* output) {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }
  *output = timezone.At(absl::CivilSecond(EpochDaysToCivilDay(date))).pre;
  return absl::OkStatus();
}

absl::Status ConvertDateToTimestamp(int32_t date,
                                    absl::string_view timezone_string,
                                    absl::Time* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertDateToTimestamp(date, timezone, output);
}

absl::Status ConvertDateToTimestamp(int32_t date, TimestampScale scale,
                                    absl::TimeZone timezone, int64_t* output) {
  absl::Time base_time;
  GOOGLESQL_RETURN_IF_ERROR(ConvertDateToTimestamp(date, timezone, &base_time));
  if (!FromTime(base_time, scale, output)) {
    return MakeEvalError() << "Cannot convert date " << DateErrorString(date)
                           << " to timestamp";
  }
  return absl::OkStatus();
}

absl::Status ConvertDateToTimestamp(int32_t date, TimestampScale scale,
                                    absl::string_view timezone_string,
                                    int64_t* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return ConvertDateToTimestamp(date, scale, timezone, output);
}

absl::Status ConvertProto3TimestampToTimestamp(
    const google::protobuf::Timestamp& input_timestamp,
    TimestampScale output_scale, int64_t* output) {
  absl::Time time;
  GOOGLESQL_RETURN_IF_ERROR(ConvertProto3TimestampToTimestamp(input_timestamp, &time));
  if (!FromTime(time, output_scale, output)) {
    return MakeEvalError() << "Invalid Proto3 Timestamp input: "
                           << absl::StrCat(input_timestamp);
  }
  return absl::OkStatus();
}

absl::Status ConvertProto3TimestampToTimestamp(
    const google::protobuf::Timestamp& input_timestamp, absl::Time* output) {
  auto result_or = googlesql_base::DecodeGoogleApiProto(input_timestamp);
  if (!result_or.ok()) {
    return MakeEvalError()
           << "Invalid Proto3 Timestamp input: "
                        << input_timestamp.DebugString();
  }
  *output = result_or.value();
  // DecodeGoogleApiProto enforces the same valid timestamp range as GoogleSQL.
  // This check is meant to give us protection in case the contract of
  // DecodeGoogleApiProto changes in the future.
  ABSL_DCHECK(IsValidTime(*output));
  return absl::OkStatus();
}

absl::Status ConvertTimestampToProto3Timestamp(
    int64_t input_timestamp, TimestampScale scale,
    google::protobuf::Timestamp* output) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto time, MakeTimeFromInt64(input_timestamp, scale));
  return ConvertTimestampToProto3Timestamp(time, output);
}

absl::Status ConvertTimestampToProto3Timestamp(
    absl::Time input_timestamp, google::protobuf::Timestamp* output) {
  // We don't need to explicity check the validity of <input_timestamp> as
  // EncodeGoogleApiProto enforces the same valid timestamp range as GoogleSQL.
  return googlesql_base::EncodeGoogleApiProto(input_timestamp, output);
}

absl::Status ConvertProto3DateToDate(const google::type::Date& input,
                                     int32_t* output) {
  return ConstructDate(input.year(), input.month(), input.day(), output);
}

absl::Status ConvertDateToProto3Date(int32_t input,
                                     google::type::Date* output) {
  if (!IsValidDate(input)) {
    return MakeEvalError() << "Input is outside of Proto3 Date range: "
                           << input;
  }
  absl::CivilDay converted_date = EpochDaysToCivilDay(input);
  // cast is guaranteed safe, since we check with IsValidDate.
  output->set_year(static_cast<int32_t>(converted_date.year()));
  output->set_month(converted_date.month());
  output->set_day(converted_date.day());
  return absl::OkStatus();
}

absl::Status ConvertBetweenTimestamps(int64_t input_timestamp,
                                      TimestampScale input_scale,
                                      TimestampScale output_scale,
                                      int64_t* output) {
  if (!IsValidTimestamp(input_timestamp, input_scale)) {
    return MakeEvalError() << "Invalid timestamp value: " << input_timestamp;
  }
  return ConvertBetweenTimestampsInternal(input_timestamp, input_scale,
                                          output_scale, output);
}

absl::Status AddDateOverflow(int32_t date, DateTimestampPart part,
                             int32_t interval, int32_t* output,
                             bool* had_overflow) {
  *had_overflow = false;
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }

  // Special cases to avoid computing converted_date.
  if (part == DAY) {
    if (!Add<int32_t>(date, interval, output, kNoError)) {
      *had_overflow = true;
      return absl::OkStatus();
    }
  } else if (part == WEEK) {
    if (!Multiply<int32_t>(7, interval, &interval, kNoError) ||
        !Add<int32_t>(date, interval, output, kNoError)) {
      *had_overflow = true;
      return absl::OkStatus();
    }
  } else if (part == YEAR || part == QUARTER || part == MONTH) {
    absl::CivilDay civil_day = EpochDaysToCivilDay(date);
    // cast is safe, checked with IsValidDate.
    int y = static_cast<int32_t>(civil_day.year());
    int month = civil_day.month();
    int day = civil_day.day();
    switch (part) {
      case YEAR: {
        if (!Add<int32_t>(y, interval, &y, kNoError)) {
          *had_overflow = true;
          return absl::OkStatus();
        }
        AdjustYearMonthDay(&y, &month, &day);
        absl::CivilDay date_value;
        if (!MakeDate(y, month, day, &date_value)) {
          *had_overflow = true;
          return absl::OkStatus();
        }
        *output = CivilDayToEpochDays(date_value);
        break;
      }
      case QUARTER:
        if (!Multiply<int32_t>(3, interval, &interval, kNoError)) {
          *had_overflow = true;
          return absl::OkStatus();
        }
        ABSL_FALLTHROUGH_INTENDED;
      case MONTH: {
        int32_t m;
        if (!Add<int32_t>(month, interval, &m, kNoError)) {
          *had_overflow = true;
          return absl::OkStatus();
        }

        AdjustYearMonthDay(&y, &m, &day);
        absl::CivilDay date_value;
        if (!MakeDate(y, m, day, &date_value)) {
          *had_overflow = true;
          return absl::OkStatus();
        }
        *output = CivilDayToEpochDays(date_value);
        break;
      }
      default:
        // Do nothing.
        break;
    }
  } else {  // Other DateTimestampPart are not supported.
    return MakeEvalError() << "Unsupported DateTimestampPart "
                           << DateTimestampPart_Name(part);
  }
  if (!IsValidDate(*output)) {
    *had_overflow = true;
    return absl::OkStatus();
  }
  return absl::OkStatus();
}

absl::Status AddDate(int32_t date, DateTimestampPart part, int64_t interval,
                     int32_t* output) {
  // The interval is an int64 to match the GoogleSQL function signature.
  // Below we will do safe casting with it, so it must be in
  // the domain of int32 numbers.
  if (interval > std::numeric_limits<int32_t>::max() ||
      interval < std::numeric_limits<int32_t>::lowest()) {
    return MakeAddDateOverflowError(date, part, interval);
  }

  bool had_overflow = false;
  GOOGLESQL_RETURN_IF_ERROR(AddDateOverflow(date, part, static_cast<int32_t>(interval),
                                  output, &had_overflow));
  if (had_overflow) {
    return MakeAddDateOverflowError(date, part, interval);
  }
  return absl::OkStatus();
}

absl::Status AddDate(int32_t date, IntervalValue interval,
                     DatetimeValue* output) {
  DatetimeValue datetime;
  GOOGLESQL_RETURN_IF_ERROR(ConstructDatetime(date, TimeValue(), &datetime));
  GOOGLESQL_RETURN_IF_ERROR(AddDatetime(datetime, interval, output));
  return absl::OkStatus();
}

absl::Status SubDate(int32_t date, DateTimestampPart part, int64_t interval,
                     int32_t* output) {
  // The negation of std::numeric_limits<int64>::lowest() is undefined, so
  // protect against it explicitly.
  if (interval == std::numeric_limits<int64_t>::lowest()) {
    return MakeSubDateOverflowError(date, part, interval);
  }
  return AddDate(date, part, -interval, output);
}

absl::Status DiffDates(int32_t date1, int32_t date2, DateTimestampPart part,
                       int32_t* output) {
  if (!IsValidDate(date1)) {
    return MakeEvalError() << "Invalid date value: " << date1;
  }
  if (!IsValidDate(date2)) {
    return MakeEvalError() << "Invalid date value: " << date2;
  }

  switch (part) {
    case DAY:
      *output = date1 - date2;
      break;
    case WEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY:
    case ISOWEEK:
      // TODO: Refactor the WEEK implementation to not rely on
      // a hacky version of TruncateDateImpl that can return a Date that
      // is out of the valid range (and remove the <enforce_range> argument).
      GOOGLESQL_RETURN_IF_ERROR(
          TruncateDateImpl(date1, part, /*enforce_range=*/false, &date1));
      GOOGLESQL_RETURN_IF_ERROR(
          TruncateDateImpl(date2, part, /*enforce_range=*/false, &date2));
      *output = (date1 - date2) / 7;
      break;
    case YEAR:
    case ISOYEAR:
    case QUARTER:
    case MONTH: {
      absl::CivilDay civil_day1 = EpochDaysToCivilDay(date1);
      absl::civil_year_t y1 = civil_day1.year();
      int32_t m1 = civil_day1.month();
      absl::CivilDay civil_day2 = EpochDaysToCivilDay(date2);
      absl::civil_year_t y2 = civil_day2.year();
      int32_t m2 = civil_day2.month();
      switch (part) {
        case YEAR:
          *output = y1 - y2;
          break;
        case ISOYEAR:
          // cast is safe because dates are severely restricted by IsValidDate.
          *output = static_cast<int32_t>(GetIsoYear(civil_day1) -
                                         GetIsoYear(civil_day2));
          break;
        case MONTH:
          *output = (y1 - y2) * 12 + (m1 - m2);
          break;
        case QUARTER:
          *output = (y1 * 12 + m1 - 1) / 3 - (y2 * 12 + m2 - 1) / 3;
          break;
        default:
          break;
      }
      break;
    }
    default:
      return MakeEvalError() << "Unsupported DateTimestampPart "
                             << DateTimestampPart_Name(part);
  }
  return absl::OkStatus();
}

// INTERVAL difference between DATEs is DAY granularity.
// Months and nanos are always zero.
absl::StatusOr<IntervalValue> IntervalDiffDates(int32_t date1, int32_t date2) {
  return IntervalValue::FromDays(date1 - date2);
}

// Valid <part> for this function are only HOUR, MINUTE, SECOND, MILLISECOND,
// MICROSECOND and NANOSECOND.
// For all valid input, overflow could happen only when <part> is NANOSECOND.
// When overflow happens, use the supplied <nano_overflow_error_maker> to make
// an appropriate error message to pass out.
static absl::Status DiffWithPartsSmallerThanDay(
    const absl::CivilSecond civil_second_1, int64_t nanosecond_1,
    const absl::CivilSecond civil_second_2, int64_t nanosecond_2,
    DateTimestampPart part,
    const std::function<absl::Status()>& nano_overflow_error_maker,
    int64_t* output) {
  if (part == HOUR) {
    *output = absl::CivilHour(civil_second_1) - absl::CivilHour(civil_second_2);
  } else if (part == MINUTE) {
    *output =
        absl::CivilMinute(civil_second_1) - absl::CivilMinute(civil_second_2);
  } else {
    int64_t diff_in_seconds = civil_second_1 - civil_second_2;
    switch (part) {
      case NANOSECOND:
        if (std::numeric_limits<int64_t>::lowest() / powers_of_ten[9] <=
                diff_in_seconds &&
            std::numeric_limits<int64_t>::max() / powers_of_ten[9] >=
                diff_in_seconds) {
          // std::numeric_limits<int64>::lowest() < nano_diff_1 <
          // std::numeric_limits<int64>::max() should always hold.
          int64_t nano_diff_1 = diff_in_seconds * powers_of_ten[9];
          int64_t nano_diff_2 = nanosecond_1 - nanosecond_2;
          // The output is valid only when
          // std::numeric_limits<int64>::lowest() <= nano_diff_1 + nano_diff_2
          // <= std::numeric_limits<int64>::max()
          if ((nano_diff_2 >= 0 &&
               std::numeric_limits<int64_t>::max() - nano_diff_2 >=
                   nano_diff_1) ||
              (nano_diff_2 < 0 &&
               std::numeric_limits<int64_t>::lowest() - nano_diff_2 <=
                   nano_diff_1)) {
            *output = nano_diff_1 + nano_diff_2;
            return absl::OkStatus();
          }
        }
        return nano_overflow_error_maker();
      case MICROSECOND:
        *output =
            diff_in_seconds * powers_of_ten[6] +
            (nanosecond_1 / powers_of_ten[3] - nanosecond_2 / powers_of_ten[3]);
        break;
      case MILLISECOND:
        *output =
            diff_in_seconds * powers_of_ten[3] +
            (nanosecond_1 / powers_of_ten[6] - nanosecond_2 / powers_of_ten[6]);
        break;
      case SECOND:
        *output = diff_in_seconds;
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected DateTimestampPart "
                         << DateTimestampPart_Name(part);
    }
  }
  return absl::OkStatus();
}

absl::Status DiffDatetimes(const DatetimeValue& datetime1,
                           const DatetimeValue& datetime2,
                           DateTimestampPart part, int64_t* output) {
  if (!datetime1.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime1.DebugString();
  }
  if (!datetime2.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime2.DebugString();
  }

  switch (part) {
    case YEAR:
      *output = absl::CivilYear(datetime1.ConvertToCivilSecond()) -
                absl::CivilYear(datetime2.ConvertToCivilSecond());
      break;
    case QUARTER: {
      // This logic is the same as the one in DiffDates()
      auto civil_time_1 = datetime1.ConvertToCivilSecond();
      auto civil_time_2 = datetime2.ConvertToCivilSecond();
      *output = (civil_time_1.year() * 12 + civil_time_1.month() - 1) / 3 -
                (civil_time_2.year() * 12 + civil_time_2.month() - 1) / 3;
      break;
    }
    case MONTH:
      *output = absl::CivilMonth(datetime1.ConvertToCivilSecond()) -
                absl::CivilMonth(datetime2.ConvertToCivilSecond());
      break;
    case WEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY:
    case ISOYEAR:
    case ISOWEEK: {
      int32_t date1;
      int32_t date2;
      int32_t int32_diff;
      GOOGLESQL_RETURN_IF_ERROR(ExtractFromDatetime(DATE, datetime1, &date1));
      GOOGLESQL_RETURN_IF_ERROR(ExtractFromDatetime(DATE, datetime2, &date2));
      GOOGLESQL_RETURN_IF_ERROR(DiffDates(date1, date2, part, &int32_diff));
      *output = int32_diff;
      break;
    }
    case DAY:
      *output = absl::CivilDay(datetime1.ConvertToCivilSecond()) -
                absl::CivilDay(datetime2.ConvertToCivilSecond());
      break;
    case HOUR:
    case MINUTE:
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND: {
      auto DatetimeDiffOverflowErrorMaker = [&datetime1, &datetime2,
                                             part]() -> absl::Status {
        const std::string error_shared_prefix = absl::StrCat(
            "DATETIME_DIFF at ", DateTimestampPart_Name(part),
            " precision between datetime ", datetime1.DebugString(), " and ",
            datetime2.DebugString());
        if (part == NANOSECOND) {
          return MakeEvalError() << error_shared_prefix << " causes overflow";
        }
        GOOGLESQL_RET_CHECK_FAIL() << error_shared_prefix
                         << " should never have overflow error";
      };
      return DiffWithPartsSmallerThanDay(
          datetime1.ConvertToCivilSecond(), datetime1.Nanoseconds(),
          datetime2.ConvertToCivilSecond(), datetime2.Nanoseconds(), part,
          DatetimeDiffOverflowErrorMaker, output);
    }
    case DAYOFWEEK:
    case DAYOFYEAR:
    case DATE:
    case DATETIME:
    case TIME:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " for DATETIME_DIFF";
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part)
             << " for DATETIME_DIFF";
  }
  return absl::OkStatus();
}

// INTERVAL difference between DATETIMEs is DAY TO SECOND granularity.
// Months is always zero, days is the full number of days difference between two
// datetimes (based on 24 hour days), and any DATETIME remainder is encoded as
// nanos.
absl::StatusOr<IntervalValue> IntervalDiffDatetimes(
    const DatetimeValue& datetime1, const DatetimeValue& datetime2) {
  int64_t seconds;
  GOOGLESQL_RETURN_IF_ERROR(DiffDatetimes(datetime1, datetime2, SECOND, &seconds));
  __int128 nanos = IntervalValue::kNanosInSecond * seconds;
  nanos += (datetime1.Nanoseconds() - datetime2.Nanoseconds());
  int64_t days = nanos / IntervalValue::kNanosInDay;
  nanos = nanos % IntervalValue::kNanosInDay;
  return IntervalValue::FromMonthsDaysNanos(0, days, nanos);
}

// This internal wrapper is shared by both datetime_add and datetime_sub. The
// extra function is used to generate appropriate error messages when
// out-of-range error happens.
static absl::Status AddDatetimeInternal(
    const DatetimeValue& datetime, DateTimestampPart part, int64_t interval,
    DatetimeValue* output,
    const std::function<absl::Status()>& overflow_error_maker) {
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  DatetimeValue result;
  if (CheckValidAddTimestampPart(part, false /* is_legacy */).ok()) {
    // We can re-use the AddTimestampInternal implementation by converting
    // the datetime to a absl::Time at UTC, and then converting back to
    // datetime.
    absl::Time datetime_in_utc =
        absl::UTCTimeZone().At(datetime.ConvertToCivilSecond()).pre;
    datetime_in_utc += absl::Nanoseconds(datetime.Nanoseconds());

    absl::Time result_timestamp;
    bool had_overflow_unused;
    if (!AddTimestampInternal(datetime_in_utc, absl::UTCTimeZone(), part,
                              interval, &result_timestamp, &had_overflow_unused)
             .ok()) {
      return overflow_error_maker();
    }
    GOOGLESQL_RETURN_IF_ERROR(ConvertTimestampToDatetime(result_timestamp,
                                               absl::UTCTimeZone(), &result));
  } else {
    // We are adding WEEK or larger granularity.  Adding WEEK or larger is
    // not supported by TIMESTAMP_ADD/AddTimestamp(), so we have a separate
    // function for adding larger date parts to civil datetimes.
    if (!AddAtLeastDaysToDatetime(datetime, part, interval, &result)) {
      return overflow_error_maker();
    }
  }

  if (!result.IsValid()) {
    return overflow_error_maker();
  }

  *output = result;
  return absl::OkStatus();
}

absl::Status AddDatetime(const DatetimeValue& datetime, DateTimestampPart part,
                         int64_t interval, DatetimeValue* output) {
  return AddDatetimeInternal(
      datetime, part, interval, output, [datetime, part, interval] {
        return MakeAddDatetimeOverflowError(datetime, part, interval);
      });
}

absl::Status SubDatetime(const DatetimeValue& datetime, DateTimestampPart part,
                         int64_t interval, DatetimeValue* output) {
  auto error_maker = [datetime, part, interval]() {
    return MakeSubDatetimeOverflowError(datetime, part, interval);
  };
  // The negation of std::numeric_limits<int64>::lowest() is undefined so we
  // handle it specially here. Subtracting std::numeric_limits<int64>::lowest()
  // is equivalent to adding std::numeric_limits<int64>::max() (which is
  // -1*(std::numeric_limits<int64>::lowest()+1)) intervals and 1 extra one.
  if (interval == std::numeric_limits<int64_t>::lowest()) {
    GOOGLESQL_RETURN_IF_ERROR(AddDatetimeInternal(datetime, part,
                                        std::numeric_limits<int64_t>::max(),
                                        output, error_maker));
    return AddDatetimeInternal(*output, part, 1, output, error_maker);
  }

  return AddDatetimeInternal(datetime, part, -interval, output, error_maker);
}

absl::Status AddDatetime(DatetimeValue datetime, IntervalValue interval,
                         DatetimeValue* output) {
  if (interval.get_months() != 0) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddDatetime(datetime, MONTH, interval.get_months(), &datetime));
  }
  if (interval.get_days() != 0) {
    GOOGLESQL_RETURN_IF_ERROR(AddDatetime(datetime, DAY, interval.get_days(), &datetime));
  }
  bool had_overflow = false;
  if (interval.get_micros() != 0) {
    // AddDatetimeInternal allows to pass custom lambda function which is called
    // in case of overflow. But the 'datetime' is not updated when overflow
    // happens. This can happen in valid cases, because IntervalValue is
    // normalized to always have positive nano_fractions. So -1 nanosecond is
    // encoded as -1 microsecond and +999 nanoseconds. The -1 microsecond may
    // cause overflow but it will be later compensated by +999 nanoseconds. So
    // we opportunistically add 1 extra microsecond in the hope that overflow
    // won't happen with it, and later remember to subtract it from final
    // result.
    GOOGLESQL_RETURN_IF_ERROR(AddDatetimeInternal(datetime, MICROSECOND,
                                        interval.get_micros(), &datetime,
                                        [&had_overflow] {
                                          had_overflow = true;
                                          return absl::OkStatus();
                                        }));
    if (had_overflow) {
      GOOGLESQL_RETURN_IF_ERROR(AddDatetime(datetime, MICROSECOND,
                                  interval.get_micros() + 1, &datetime));
    }
  }
  if (interval.get_nano_fractions() != 0) {
    GOOGLESQL_RETURN_IF_ERROR(AddDatetime(datetime, NANOSECOND,
                                interval.get_nano_fractions(), &datetime));
  }
  if (had_overflow) {
    GOOGLESQL_RETURN_IF_ERROR(AddDatetime(datetime, MICROSECOND, -1, &datetime));
  }

  *output = datetime;
  return absl::OkStatus();
}

absl::StatusOr<PicoTime> AddTimestamp(const PicoTime& pico_time,
                                      DateTimestampPart part,
                                      absl::int128 interval) {
  absl::int128 picos = pico_time.ToUnixPicos();

  GOOGLESQL_RETURN_IF_ERROR(CheckValidAddTimestampPart(part, /*is_legacy=*/false));

  GOOGLESQL_ASSIGN_OR_RETURN(absl::int128 pico_seconds_interval,
                   RescaleInterval<absl::int128>(interval, part, kPicoseconds));

  // Note the interval value at PICOSECOND precision is capped at the semantic
  // domain of Timestamp, and not the max/min values of absl::int128.
  absl::int128 max_interval_picos =
      PicoTime::MaxValue().ToUnixPicos() - PicoTime::MinValue().ToUnixPicos();

  if (pico_seconds_interval > max_interval_picos ||
      pico_seconds_interval < -max_interval_picos) {
    return MakeEvalError() << "TIMESTAMP_ADD interval value " << interval
                           << " at " << DateTimestampPart_Name(part)
                           << " exceeds allowed range";
  }

  return PicoTime::FromUnixPicos(picos + pico_seconds_interval);
}

absl::Status AddTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::TimeZone timezone, DateTimestampPart part,
                          int64_t interval, int64_t* output) {
  if (!IsValidTimestamp(timestamp, scale)) {
    return MakeEvalError() << "Invalid timestamp: " << timestamp;
  }
  GOOGLESQL_RETURN_IF_ERROR(
      AddTimestampInternal(timestamp, scale, timezone, part, interval, output));
  if (!IsValidTimestamp(*output, scale)) {
    return MakeAddTimestampOverflowError(timestamp, part, interval, scale,
                                         timezone);
  }
  return absl::OkStatus();
}

absl::Status AddTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          int64_t* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return AddTimestamp(timestamp, scale, timezone, part, interval, output);
}

absl::Status AddTimestamp(absl::Time timestamp, absl::TimeZone timezone,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output) {
  bool had_overflow_unused;
  GOOGLESQL_RETURN_IF_ERROR(AddTimestampInternal(timestamp, timezone, part, interval,
                                       output, &had_overflow_unused));
  if (!IsValidTime(*output)) {
    return MakeAddTimestampOverflowError(timestamp, part, interval, timezone);
  }
  return absl::OkStatus();
}

absl::Status AddTimestamp(absl::Time timestamp,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return AddTimestamp(timestamp, timezone, part, interval, output);
}

absl::Status AddTimestamp(absl::Time timestamp, absl::TimeZone timezone,
                          IntervalValue interval, absl::Time* output) {
  if (interval.get_months() != 0) {
    return MakeEvalError() << "TIMESTAMP +/- INTERVAL is not supported for "
                              "intervals with non-zero MONTH or YEAR part.";
  }
  if (interval.get_days() != 0) {
    GOOGLESQL_RETURN_IF_ERROR(AddTimestamp(timestamp, timezone, DAY, interval.get_days(),
                                 &timestamp));
  }
  bool had_overflow = false;
  if (interval.get_micros() != 0) {
    // AddTimestampInternal returns OUT_OF_RANGE only when there is overflow in
    // the operation. The 'timestamp' is still updated, but outside of allowed
    // range of values, and 'had_overflow' is set to true.
    // This can happen in valid cases, because IntervalValue is normalized to
    // always have positive nano_fractions. So -1 nanosecond is encoded as
    // -1 microsecond and +999 nanoseconds. The -1 microsecond may cause
    // overflow but it will be later compensated by +999 nanoseconds, so we
    // ignore error here and check for valid result at the end.
    AddTimestampInternal(timestamp, timezone, MICROSECOND,
                         interval.get_micros(), &timestamp, &had_overflow)
        .IgnoreError();
  }
  if (interval.get_nano_fractions() != 0) {
    GOOGLESQL_RETURN_IF_ERROR(AddTimestamp(timestamp, timezone, NANOSECOND,
                                 interval.get_nano_fractions(), &timestamp));
  }
  if (ABSL_PREDICT_FALSE(had_overflow)) {
    if (!IsValidTime(timestamp)) {
      return MakeAddTimestampOverflowError(timestamp, MICROSECOND,
                                           interval.get_micros(), timezone);
    }
  }
  *output = timestamp;
  return absl::OkStatus();
}

absl::Status AddTimestampOverflow(absl::Time timestamp, absl::TimeZone timezone,
                                  DateTimestampPart part, int64_t interval,
                                  absl::Time* output, bool* had_overflow) {
  *had_overflow = false;
  absl::Status status = AddTimestampInternal(timestamp, timezone, part,
                                             interval, output, had_overflow);
  return *had_overflow ? absl::OkStatus() : status;
}

absl::StatusOr<PicoTime> AddTimestamp(PicoTime timestamp,
                                      IntervalValue interval) {
  if (interval.get_months() != 0) {
    return MakeEvalError() << "TIMESTAMP +/- INTERVAL is not supported for "
                              "intervals with non-zero MONTH or YEAR part.";
  }

  // Note we should convert the entirety of `interval` to an int128 first,
  // instead of calling AddTimestamp on `DAY`, `MICROSECOND`, and `NANOSECOND`
  // parts incrementally.
  // The nano fraction of the interval will always be positive:
  // https://github.com/google/googlesql/blob/master/googlesql/public/interval_value.h;l=46;rcl=775796179
  // {days, micros, nanos} could represent a very large negative interval
  // which would push `timestamp` close to the lower end of allowed timestamp
  // ranges. If we add {days, micros} portion of the interval first, we might
  // push 'timestamp' over already and cause an error.
  GOOGLESQL_ASSIGN_OR_RETURN(
      absl::int128 picos_from_days,
      RescaleInterval<absl::int128>(interval.get_days(), DAY, kPicoseconds));

  GOOGLESQL_ASSIGN_OR_RETURN(absl::int128 picos_from_micros,
                   RescaleInterval<absl::int128>(interval.get_micros(),
                                                 MICROSECOND, kPicoseconds));

  GOOGLESQL_ASSIGN_OR_RETURN(absl::int128 picos_from_nanos,
                   RescaleInterval<absl::int128>(interval.get_nano_fractions(),
                                                 NANOSECOND, kPicoseconds));

  return AddTimestamp(timestamp, PICOSECOND,
                      picos_from_days + picos_from_micros + picos_from_nanos);
}

absl::Status SubTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::TimeZone timezone, DateTimestampPart part,
                          int64_t interval, int64_t* output) {
  if (!IsValidTimestamp(timestamp, scale)) {
    return MakeEvalError() << "Invalid timestamp: " << timestamp;
  }
  if (interval == std::numeric_limits<int64_t>::lowest()) {
    return MakeSubTimestampOverflowError(timestamp, part, interval, scale,
                                         timezone);
  }

  GOOGLESQL_RETURN_IF_ERROR(AddTimestampInternal(timestamp, scale, timezone, part,
                                       -interval, output));
  if (!IsValidTimestamp(*output, scale)) {
    return MakeSubTimestampOverflowError(timestamp, part, interval, scale,
                                         timezone);
  }
  return absl::OkStatus();
}

absl::Status SubTimestamp(int64_t timestamp, TimestampScale scale,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          int64_t* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return SubTimestamp(timestamp, scale, timezone, part, interval, output);
}

absl::StatusOr<PicoTime> SubTimestamp(const PicoTime& pico_time,
                                      DateTimestampPart part,
                                      absl::int128 interval) {
  return AddTimestamp(pico_time, part, -interval);
}

absl::Status SubTimestamp(absl::Time timestamp, absl::TimeZone timezone,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output) {
  if (!IsValidTime(timestamp)) {
    return MakeEvalError() << "Invalid timestamp: " << timestamp;
  }
  bool had_overflow_unused;
  if (interval == std::numeric_limits<int64_t>::lowest() ||
      !AddTimestampInternal(timestamp, timezone, part, -interval, output,
                            &had_overflow_unused)
           .ok() ||
      !IsValidTime(*output)) {
    return MakeSubTimestampOverflowError(timestamp, part, interval, timezone);
  }
  return absl::OkStatus();
}

absl::Status SubTimestamp(absl::Time timestamp,
                          absl::string_view timezone_string,
                          DateTimestampPart part, int64_t interval,
                          absl::Time* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return SubTimestamp(timestamp, timezone, part, interval, output);
}

// Helper function that adds <interval> to <field>, where <field> is in the
// range [0, <radix>), and addition that overflows wraps around.  The number
// of times wrapped around is returned in <*carry>.
//
// In our use case, the maximum <radix> is 1000000000 for nanoseconds, and the
// minimum <radix> is 24 for hours. For any field from a valid TimeValue where
// the input <field> is smaller than <radix>, there is no risk of overflow.
//
// Requires that <radix> is positive, and <*field> is in range [0, <radix>).
static void AddOnField(int64_t interval, int64_t radix, int* field,
                       int64_t* carry) {
  // Expected invariants.
  ABSL_DCHECK_LE(0, *field);
  ABSL_DCHECK_LT(*field, radix);
  ABSL_DCHECK_LE(radix, 1000000000);  // number of nanos in a second.

  // <modulus> is the number of parts that we want to add to <field> for
  // the given <interval>.  For instance, if we want to add 30 hours
  // to an existing Time of 3:45, then:
  //     <interval> = 30
  //     <radix> = 24
  //     <*field> = 3
  // We compute the number of hours to add to <field> to produce the resulting
  // hours part of the clock time by computing MOD(30, 24) = 6.
  const int64_t modulus = googlesql_base::MathUtil::NonnegativeMod(interval, radix);

  // Adding <modulus> to <*field> cannot overflow because <*field> and
  // <modulus> are both less than <radix>, and <radix> is not greater than
  // 1000000000.
  *field += modulus;
  *carry = googlesql_base::MathUtil::FloorOfRatio(interval, radix);

  // Because both the original <*field> and the <modulus> should be in
  // [0, <radix>), then the updated <*field> should always be in
  // [0, <radix> * 2).
  ABSL_DCHECK(*field >= 0 && *field < radix * 2)
      << "AddOnField() produced an unexpected result " << *field
      << " by adding " << interval << " on a field of radix " << radix;

  if (*field >= radix) {
    // Adjust <*field> and <*carry> if adding <modulus> causes us to wrap
    // around once more.  For instance, if:
    //     <interval> = 30
    //     <radix> = 24
    //     <*field> = 22
    // Then when we add the <modulus> 6 to <*field> we get 28, which wraps
    // around to 4 and adds 1 to <*carry>.
    *field -= radix;
    *carry += 1;
  }
}

// Note that this function will return an error only when the input <time> is
// invalid or the input <part> is not valid for TIME, and never produce an
// overflow because of the addition.
// Note that if addition of the interval results in a TimeValue outside of the
// [00:00:00, 24:00:00) range, the result will be wrapped around to stay within
// 24 hours.
static absl::Status AddTimeInternal(const TimeValue& time,
                                    DateTimestampPart part, int64_t interval,
                                    TimeValue* output) {
  if (!time.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time.DebugString();
  }
  if (!(part == HOUR || part == MINUTE || part == SECOND ||
        part == MILLISECOND || part == MICROSECOND || part == NANOSECOND)) {
    return MakeEvalError() << "Unsupported DateTimestampPart "
                           << DateTimestampPart_Name(part);
  }

  int hour = time.Hour();
  int minute = time.Minute();
  int second = time.Second();
  int nanoseconds = time.Nanoseconds();
  while (interval != 0 && part != DAY) {
    switch (part) {
      case NANOSECOND:
        AddOnField(interval, 1000000000, &nanoseconds, &interval);
        part = SECOND;
        break;
      case MICROSECOND: {
        int microseconds = 0;
        int64_t carry_1;
        AddOnField(interval, 1000000, &microseconds, &carry_1);
        int64_t carry_2;
        AddOnField(microseconds * 1000, 1000000000, &nanoseconds, &carry_2);
        interval = carry_1 + carry_2;
        part = SECOND;
        break;
      }
      case MILLISECOND: {
        int millisecond = 0;
        int64_t carry_1;
        AddOnField(interval, 1000, &millisecond, &carry_1);
        int64_t carry_2;
        AddOnField(millisecond * 1000000, 1000000000, &nanoseconds, &carry_2);
        interval = carry_1 + carry_2;
        part = SECOND;
        break;
      }
      case SECOND: {
        AddOnField(interval, 60, &second, &interval);
        part = MINUTE;
        break;
      }
      case MINUTE: {
        AddOnField(interval, 60, &minute, &interval);
        part = HOUR;
        break;
      }
      case HOUR: {
        AddOnField(interval, 24, &hour, &interval);
        part = DAY;
        break;
      }
      default:
        break;
    }
  }
  *output = TimeValue::FromHMSAndNanos(hour, minute, second, nanoseconds);
  // This check should never fail as all fields should be in range.
  ABSL_DCHECK(output->IsValid()) << output->DebugString();
  return absl::OkStatus();
}

absl::Status AddTime(const TimeValue& time, DateTimestampPart part,
                     int64_t interval, TimeValue* output) {
  return AddTimeInternal(time, part, interval, output);
}

absl::Status SubTime(const TimeValue& time, DateTimestampPart part,
                     int64_t interval, TimeValue* output) {
  if (interval == std::numeric_limits<int64_t>::lowest()) {
    GOOGLESQL_RETURN_IF_ERROR(AddTimeInternal(
        time, part, std::numeric_limits<int64_t>::max(), output));
    return AddTimeInternal(*output, part, 1, output);
  }
  return AddTimeInternal(time, part, -interval, output);
}

absl::Status DiffTimes(const TimeValue& time1, const TimeValue& time2,
                       DateTimestampPart part, int64_t* output) {
  if (!time1.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time1.DebugString();
  }
  if (!time2.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time2.DebugString();
  }

  const absl::CivilSecond civil_time_1(1970, 1, 1, time1.Hour(), time1.Minute(),
                                       time1.Second());
  const absl::CivilSecond civil_time_2(1970, 1, 1, time2.Hour(), time2.Minute(),
                                       time2.Second());
  switch (part) {
    case HOUR:
    case MINUTE:
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND:
      return DiffWithPartsSmallerThanDay(
          civil_time_1, time1.Nanoseconds(), civil_time_2, time2.Nanoseconds(),
          part,
          []() -> absl::Status {
            GOOGLESQL_RET_CHECK_FAIL() << "TIME_DIFF should never have overflow error";
          } /* nano_overflow_error_maker */,
          output);
    case YEAR:
    case MONTH:
    case DAY:
    case DAYOFWEEK:
    case DAYOFYEAR:
    case QUARTER:
    case DATE:
    case WEEK:
    case DATETIME:
    case TIME:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIME_DIFF";
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIME_DIFF";
  }
}

// INTERVAL difference between TIMEs is HOUR TO SECOND granularity.
// Months and days are always zero.
absl::StatusOr<IntervalValue> IntervalDiffTimes(const TimeValue& time1,
                                                const TimeValue& time2) {
  int64_t nanos;
  GOOGLESQL_RETURN_IF_ERROR(DiffTimes(time1, time2, NANOSECOND, &nanos));
  return IntervalValue::FromNanos(nanos);
}

absl::Status TruncateTime(const TimeValue& time, DateTimestampPart part,
                          TimeValue* output) {
  if (!time.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time.DebugString();
  }
  switch (part) {
    case HOUR:
      *output = TimeValue::FromHMSAndNanos(time.Hour(), 0, 0, 0);
      break;
    case MINUTE:
      *output = TimeValue::FromHMSAndNanos(time.Hour(), time.Minute(), 0, 0);
      break;
    case SECOND:
      *output = TimeValue::FromHMSAndNanos(time.Hour(), time.Minute(),
                                           time.Second(), 0);
      break;
    case MILLISECOND:
      *output = TimeValue::FromHMSAndNanos(
          time.Hour(), time.Minute(), time.Second(),
          time.Nanoseconds() / powers_of_ten[6] * powers_of_ten[6]);
      break;
    case MICROSECOND:
      *output = TimeValue::FromHMSAndNanos(
          time.Hour(), time.Minute(), time.Second(),
          time.Nanoseconds() / powers_of_ten[3] * powers_of_ten[3]);
      break;
    case NANOSECOND:
      *output = time;
      break;
    case YEAR:
    case MONTH:
    case DAY:
    case DAYOFWEEK:
    case DAYOFYEAR:
    case QUARTER:
    case DATE:
    case WEEK:
    case DATETIME:
    case TIME:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIME_TRUNC";
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIME_TRUNC";
  }
  return absl::OkStatus();
}

absl::Status TruncateDate(int32_t date, DateTimestampPart part,
                          int32_t* output) {
  return TruncateDateImpl(date, part, /*enforce_range=*/true, output);
}

absl::Status TimestampTrunc(int64_t timestamp, absl::TimeZone timezone,
                            DateTimestampPart part, int64_t* output) {
  return TimestampTruncImpl(timestamp, kMicroseconds, NEW_TIMESTAMP_TYPE,
                            timezone, part, output);
}

absl::Status TimestampTrunc(int64_t timestamp,
                            absl::string_view timezone_string,
                            DateTimestampPart part, int64_t* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return TimestampTrunc(timestamp, timezone, part, output);
}

absl::StatusOr<PicoTime> TimestampTrunc(const PicoTime& timestamp,
                                        absl::TimeZone timezone,
                                        DateTimestampPart part) {
  switch (part) {
    case PICOSECOND:
      // No-op if the truncating at PICOSECOND, which is the precision
      // `timestamp` came in.
      return timestamp;

    case YEAR:
    case MONTH:
    case DAY:
    case DAYOFWEEK:
    case DAYOFYEAR:
    case QUARTER:
    case HOUR:
    case MINUTE:
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND:
    case DATE:
    case WEEK:
    case DATETIME:
    case TIME:
    case ISOYEAR:
    case ISOWEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY: {
      // If the truncation precision is lower than PICOSECOND, we can ignore the
      // sub-nano second portion of the input timestamp and perform truncation
      // as if the input timestamp was in Nanosecond precision.
      absl::Time output;
      GOOGLESQL_RETURN_IF_ERROR(
          TimestampTrunc(timestamp.ToAbslTime(), timezone, part, &output));
      return PicoTime::Create(output);
    }

    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected DateTimestampPart "
                       << DateTimestampPart_Name(part);
  }
}

absl::Status TimestampTrunc(absl::Time timestamp, absl::TimeZone timezone,
                            DateTimestampPart part, absl::Time* output) {
  if (!IsValidTime(timestamp)) {
    return MakeEvalError() << "Invalid timestamp value: "
                           << TimestampErrorString(timestamp, timezone);
  }
  switch (part) {
    case SECOND: {
      *output = absl::FromUnixSeconds(absl::ToUnixSeconds(timestamp));
      break;
    }
    case MILLISECOND: {
      *output = absl::FromUnixMillis(absl::ToUnixMillis(timestamp));
      break;
    }
    case MICROSECOND: {
      *output = absl::FromUnixMicros(absl::ToUnixMicros(timestamp));
      break;
    }
    case NANOSECOND: {
      *output = absl::UnixEpoch() + absl::Floor(timestamp - absl::UnixEpoch(),
                                                absl::Nanoseconds(1));
      break;
    }
    default:
      return TimestampTruncAtLeastMinute(timestamp, kNanoseconds, timezone,
                                         part, output);
  }
  return absl::OkStatus();
}

absl::Status TimestampTrunc(absl::Time timestamp,
                            absl::string_view timezone_string,
                            DateTimestampPart part, absl::Time* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return TimestampTrunc(timestamp, timezone, part, output);
}

absl::Status TruncateTimestamp(int64_t timestamp, TimestampScale scale,
                               absl::TimeZone timezone, DateTimestampPart part,
                               int64_t* output) {
  return TimestampTruncImpl(timestamp, scale, LEGACY_TIMESTAMP_TYPE, timezone,
                            part, output);
}

absl::Status TruncateTimestamp(int64_t timestamp, TimestampScale scale,
                               absl::string_view timezone_string,
                               DateTimestampPart part, int64_t* output) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return TruncateTimestamp(timestamp, scale, timezone, part, output);
}

absl::Status TruncateDatetime(const DatetimeValue& datetime,
                              DateTimestampPart part, DatetimeValue* output) {
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }
  switch (part) {
    case YEAR:
    case ISOYEAR:
    case QUARTER:
    case MONTH:
    case WEEK:
    case ISOWEEK:
    case WEEK_MONDAY:
    case WEEK_TUESDAY:
    case WEEK_WEDNESDAY:
    case WEEK_THURSDAY:
    case WEEK_FRIDAY:
    case WEEK_SATURDAY:
    case DAY: {
      int32_t date;
      GOOGLESQL_RETURN_IF_ERROR(ExtractFromDatetime(DATE, datetime, &date));
      GOOGLESQL_RETURN_IF_ERROR(TruncateDate(date, part, &date));
      if (!IsValidDate(date)) {
        return MakeEvalError() << "Truncating " << datetime.DebugString()
                               << " to " << DateTimestampPart_Name(part)
                               << " produces an invalid Datetime value";
      }
      return ConstructDatetime(date, TimeValue() /* 00:00:00 */, output);
    }
    case HOUR:
    case MINUTE:
    case SECOND:
    case MILLISECOND:
    case MICROSECOND:
    case NANOSECOND: {
      int32_t date;
      GOOGLESQL_RETURN_IF_ERROR(ExtractFromDatetime(DATE, datetime, &date));

      TimeValue time;
      GOOGLESQL_RETURN_IF_ERROR(ExtractTimeFromDatetime(datetime, &time));
      GOOGLESQL_RETURN_IF_ERROR(TruncateTime(time, part, &time));

      return ConstructDatetime(date, time, output);
    }
    case DAYOFWEEK:
    case DAYOFYEAR:
    case DATE:
    case DATETIME:
    case TIME:
      return MakeEvalError()
             << "Unsupported DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIME_TRUNC";
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part)
             << " for TIME_TRUNC";
  }
}

// TODO: Migrate this to the Picoseconds-based implementation.
absl::Status TimestampDiff(int64_t timestamp1, int64_t timestamp2,
                           TimestampScale scale, DateTimestampPart part,
                           int64_t* output) {
  GOOGLESQL_ASSIGN_OR_RETURN(absl::Time base_time1, MakeTimeFromInt64(timestamp1, scale));
  GOOGLESQL_ASSIGN_OR_RETURN(absl::Time base_time2, MakeTimeFromInt64(timestamp2, scale));
  return TimestampDiff(base_time1, base_time2, part, output);
}

absl::Status TimestampDiff(absl::Time timestamp1, absl::Time timestamp2,
                           DateTimestampPart part, int64_t* output) {
  absl::Duration duration = timestamp1 - timestamp2;
  absl::Duration rem;
  absl::Duration divide;
  switch (part) {
    case DAY:
      divide = absl::Hours(24);
      break;
    case HOUR:
      divide = absl::Hours(1);
      break;
    case MINUTE:
      divide = absl::Minutes(1);
      break;
    case SECOND:
      divide = absl::Seconds(1);
      break;
    case MILLISECOND:
      divide = absl::Milliseconds(1);
      break;
    case MICROSECOND:
      divide = absl::Microseconds(1);
      break;
    case NANOSECOND:
      divide = absl::Nanoseconds(1);
      break;
    case YEAR:
    case QUARTER:
    case MONTH:
    case DATE:
    case DAYOFWEEK:
    case DAYOFYEAR:
    case WEEK:
      return MakeEvalError() << "Unsupported DateTimestampPart "
                             << DateTimestampPart_Name(part);
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
  *output = absl::IDivDuration(duration, divide, &rem);
  // If we make sure the input timestamps are always valid, we can only do this
  // check for nano.
  if ((*output == std::numeric_limits<int64_t>::max() ||
       *output == std::numeric_limits<int64_t>::lowest()) &&
      rem != absl::ZeroDuration()) {
    return MakeEvalError() << "TIMESTAMP_DIFF at "
                           << DateTimestampPart_Name(part)
                           << " precision between values of " << timestamp1
                           << " and " << timestamp2 << " causes overflow";
  }
  return absl::OkStatus();
}

// Return the factor from `PICOSECOND` to the unit of time represented by
// `part`.
// Example: the factor from `PICOSECOND` to `NANOSECOND` is 1000
//          the factor from `PICOSECOND` to `SECOND` to is 1e12.
absl::StatusOr<absl::int128> FactorFromPicosecond(DateTimestampPart part) {
  switch (part) {
    case DAY:
      return kNaiveNumSecondsPerDay * powers_of_ten[12];
    case HOUR:
      return kNaiveNumSecondsPerHour * powers_of_ten[12];
    case MINUTE:
      return kNaiveNumSecondsPerMinute * powers_of_ten[12];
    case SECOND:
      return powers_of_ten[12];
    case MILLISECOND:
      return powers_of_ten[9];
    case MICROSECOND:
      return powers_of_ten[6];
    case NANOSECOND:
      return powers_of_ten[3];
    case PICOSECOND:
      return 1;
    default:
      return MakeEvalError()
             << "Unexpected DateTimestampPart " << DateTimestampPart_Name(part);
  }
}
absl::StatusOr<absl::int128> TimestampDiff(const PicoTime& pico_time1,
                                           const PicoTime& pico_time2,
                                           DateTimestampPart part) {
  absl::int128 duration = pico_time1.ToUnixPicos() - pico_time2.ToUnixPicos();
  GOOGLESQL_ASSIGN_OR_RETURN(absl::int128 factor, FactorFromPicosecond(part));
  return duration / factor;
}

// INTERVAL difference between TIMESTAMPs is HOUR TO SECOND granularity.
// Months and days are always zero.
absl::StatusOr<IntervalValue> IntervalDiffTimestamps(absl::Time timestamp1,
                                                     absl::Time timestamp2) {
  absl::Duration duration = timestamp1 - timestamp2;
  absl::Duration nanos_rem;
  int64_t micros =
      absl::IDivDuration(duration, absl::Microseconds(1), &nanos_rem);
  absl::Duration rem;
  int64_t nano_fractions =
      absl::IDivDuration(nanos_rem, absl::Nanoseconds(1), &rem);
  GOOGLESQL_RET_CHECK(rem == absl::ZeroDuration());
  __int128 nanos = IntervalValue::kNanosInMicro128 * micros + nano_fractions;
  return IntervalValue::FromNanos(nanos);
}

std::string TimestampScale_Name(TimestampScale scale) {
  switch (scale) {
    case kSeconds:
      return "TIMESTAMP_SECOND";
    case kMilliseconds:
      return "TIMESTAMP_MILLISECOND";
    case kMicroseconds:
      return "TIMESTAMP_MICROSECOND";
    case kNanoseconds:
      return "TIMESTAMP_NANOSECOND";
    case kPicoseconds:
      return "TIMESTAMP_PICOSECOND";
  }
}

int DateTimestampPart_FromName(absl::string_view name) {
  const std::string upper_name = absl::AsciiStrToUpper(name);
  DateTimestampPart part;
  bool is_valid = DateTimestampPart_Parse(upper_name, &part);
  return is_valid ? part : -1;
}

// TODO These encode/decode interfaces will probably get generalized
// some to support more field types and more encodings, particularly once
// we start supporting NWP wrappers.
absl::Status DecodeFormattedDate(int64_t input_formatted_date,
                                 FieldFormat::Format format,
                                 int32_t* output_date, bool* output_is_null) {
  // Check for int64 values that don't fit in int32.
  if (static_cast<int32_t>(input_formatted_date) != input_formatted_date) {
    return MakeEvalError() << "Invalid non-int32 date: "
                           << input_formatted_date;
  }

  *output_is_null = false;
  switch (format) {
    case FieldFormat::DATE:
      *output_date = input_formatted_date;
      break;

    case FieldFormat::DATE_DECIMAL: {
      if (input_formatted_date == 0) {
        // For DATE_DECIMAL, the integer 0 decodes to NULL.
        *output_date = 0;
        *output_is_null = true;
      } else {
        const int day = input_formatted_date % 100;
        const int month = (input_formatted_date / 100) % 100;
        const int year = input_formatted_date / (100 * 100);
        absl::CivilDay date;
        if (!MakeDate(year, month, day, &date)) {
          return MakeEvalError()
                 << "Invalid DATE_DECIMAL: " << input_formatted_date;
        }
        *output_date = CivilDayToEpochDays(date);
      }
      break;
    }

    case FieldFormat::DEFAULT_FORMAT:
    default:
      return MakeEvalError() << "Invalid date decode format: " << format;
  }

  return absl::OkStatus();
}

absl::Status EncodeFormattedDate(int32_t input_date, FieldFormat::Format format,
                                 int32_t* output_formatted_date) {
  switch (format) {
    case FieldFormat::DATE:
      *output_formatted_date = input_date;
      return absl::OkStatus();

    case FieldFormat::DATE_DECIMAL: {
      if (!IsValidDate(input_date)) {
        return MakeEvalError()
               << "Invalid input date for encoding: " << input_date;
      }
      const absl::CivilDay date = EpochDaysToCivilDay(input_date);
      *output_formatted_date =
          // Cast to int32 is safe; IsValidDate guarantees small numbers.
          static_cast<int32_t>(date.year()) * 100 * 100 + date.month() * 100 +
          date.day();
      return absl::OkStatus();
    }

    case FieldFormat::DEFAULT_FORMAT:
    default:
      return MakeEvalError() << "Invalid date decode format: " << format;
  }
}

int32_t CurrentDate(absl::TimeZone timezone) {
  return CivilDayToEpochDays(absl::ToCivilDay(absl::Now(), timezone));
}

absl::Status CurrentDate(absl::string_view timezone_string, int32_t* date) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  *date = CurrentDate(timezone);
  return absl::OkStatus();
}

int64_t CurrentTimestamp() { return absl::ToUnixMicros(absl::Now()); }

// TODO: Consider replace NarrowTimestampIfPossible with
// NarrowTimestampScaleIfPossible. Need to run some perf/benchmark first.
// TODO: Change the signature to return the scale.
void NarrowTimestampScaleIfPossible(absl::Time time, TimestampScale* scale) {
  // We only care about the subseconds of the absl::Time, so timezone doesn't
  // matter (assuming all UTC offsets in all timezones are whole seconds).
  int64_t subsecond = absl::ToInt64Nanoseconds(
      time - absl::FromUnixSeconds(absl::ToUnixSeconds(time)));
  const TimestampScale narrowed_scale =
      (subsecond == 0)  // (subsecond % 1000000000 == 0)
          ? kSeconds
          : (subsecond % 1000000 == 0) ? kMilliseconds
            : (subsecond % 1000 == 0)  ? kMicroseconds
                                       : kNanoseconds;
  if (narrowed_scale < *scale) {
    // return narrowed scale;
    *scale = narrowed_scale;
  }
}

TimestampScale NarrowTimestampScaleIfPossible(PicoTime time) {
  TimestampScale scale = kPicoseconds;
  if (time.SubNanoseconds() == 0) {
    NarrowTimestampScaleIfPossible(time.ToAbslTime(), &scale);
  }
  return scale;
}

absl::Status TimestampBucketizer::ValidateBucketWidth(
    IntervalValue bucket_width, TimestampScale scale) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  if (scale == kMicroseconds && bucket_width.get_nano_fractions() != 0) {
    return MakeEvalError() << "Bucket width INTERVAL with nanoseconds "
                              "precision is not allowed";
  }
  if (bucket_width.get_months() != 0) {
    return MakeEvalError()
           << "Bucket width INTERVAL with non-zero MONTH part is not allowed";
  }
  // Nano fractions can't be negative, so only checking days and micros here.
  if (bucket_width.get_days() < 0 || bucket_width.get_micros() < 0) {
    return MakeEvalError() << "Negative bucket width INTERVAL is not allowed";
  }
  if (bucket_width.get_days() != 0) {
    if (scale == kMicroseconds) {
      if (bucket_width.get_micros() != 0) {
        return MakeEvalError() << "Bucket width INTERVAL with mixed DAY and "
                                  "subsecond parts is not allowed";
      }
    } else {
      if (bucket_width.get_micros() != 0 ||
          bucket_width.get_nano_fractions() != 0) {
        return MakeEvalError() << "Bucket width INTERVAL with mixed DAY and "
                                  "subsecond parts is not allowed";
      }
    }
  }
  if (bucket_width.get_days() == 0 && bucket_width.get_micros() == 0 &&
      bucket_width.get_nano_fractions() == 0) {
    return MakeEvalError() << "Zero bucket width INTERVAL is not allowed";
  }
  return absl::OkStatus();
}

absl::StatusOr<TimestampBucketizer> TimestampBucketizer::Create(
    IntervalValue bucket_width, absl::Time origin, absl::TimeZone timezone,
    TimestampScale scale) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateBucketWidth(bucket_width, scale));

  absl::Duration bucket_size;
  if (bucket_width.get_days() > 0) {
    // Only 23 bits are used by days in IntervalValue type, so it's safe to
    // multiple by 24 without the risk of overflowing int64.
    bucket_size =
        absl::Hours(bucket_width.get_days() * static_cast<int64_t>(24));
  } else {
    bucket_size = absl::Microseconds(bucket_width.get_micros());
    if (bucket_width.get_nano_fractions() > 0) {
      // Note that we can't construct Duration directly from nanoseconds, since
      // it would overflow int64.
      bucket_size += absl::Nanoseconds(bucket_width.get_nano_fractions());
    }
  }

  return TimestampBucketizer(std::move(bucket_size), std::move(origin),
                             std::move(timezone));
}

absl::Status TimestampBucketizer::Compute(absl::Time input,
                                          absl::Time* output) const {
  absl::Duration rem = (input - origin_) % bucket_width_;
  absl::Time result = input - rem;
  if (rem < absl::ZeroDuration()) {
    // Negative remainder indicates that input < origin. When input precedes
    // origin we need shift the result backwards by one bucket.
    result -= bucket_width_;
  }

  if (!IsValidTime(result)) {
    return MakeEvalError() << "Bucket for "
                           << TimestampErrorString(input, timezone_)
                           << " is outside of timestamp range";
  }
  *output = result;
  return absl::OkStatus();
}

absl::Status TimestampBucket(absl::Time input, IntervalValue bucket_width,
                             absl::Time origin, absl::TimeZone timezone,
                             TimestampScale scale, absl::Time* output) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      TimestampBucketizer bucketizer,
      TimestampBucketizer::Create(bucket_width, origin, timezone, scale));
  return bucketizer.Compute(input, output);
}

absl::StatusOr<TimestampPicosBucketizer> TimestampPicosBucketizer::Create(
    IntervalValue bucket_width, const PicoTime& origin, TimestampScale scale) {
  // Note currently IntervalValue does not support Picosecond precision and we
  // are using the existing logic from TimestampBucketizer to validate
  // `bucket_width`.
  GOOGLESQL_RETURN_IF_ERROR(
      TimestampBucketizer::ValidateBucketWidth(bucket_width, scale));

  absl::int128 bucket_size_picos;
  if (bucket_width.get_days() > 0) {
    bucket_size_picos = bucket_width.get_days() * kNumPicosPerDay;
  } else {
    bucket_size_picos =
        static_cast<absl::int128>(bucket_width.get_micros()) * 1'000'000;
    if (bucket_width.get_nano_fractions() > 0) {
      bucket_size_picos += bucket_width.get_nano_fractions() * 1000;
    }
  }

  return TimestampPicosBucketizer(bucket_size_picos, origin);
}

absl::StatusOr<PicoTime> TimestampPicosBucketizer::Compute(
    const PicoTime& input) const {
  absl::int128 input_picos = input.ToUnixPicos();
  absl::int128 rem = (input_picos - origin_) % bucket_width_;
  absl::int128 result = input_picos - rem;

  if (rem < 0) {
    // Negative remainder indicates that input < origin. When input precedes
    // origin we need to shift the result backwards by one bucket.
    result -= bucket_width_;
  }

  auto result_picos = PicoTime::FromUnixPicos(result);
  if (!result_picos.ok() &&
      result_picos.status().code() == absl::StatusCode::kOutOfRange) {
    return MakeEvalError() << "Bucket for " << input.DebugString()
                           << " is outside of timestamp range";
  }
  return result_picos;
}

absl::StatusOr<PicoTime> TimestampPicosBucket(const PicoTime& input,
                                              IntervalValue bucket_width,
                                              const PicoTime& origin,
                                              TimestampScale scale) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      TimestampPicosBucketizer bucketizer,
      TimestampPicosBucketizer::Create(bucket_width, origin, scale));
  return bucketizer.Compute(input);
}

absl::Status DatetimeBucketizer::ValidateBucketWidth(IntervalValue bucket_width,
                                                     TimestampScale scale) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  if (scale == kMicroseconds && bucket_width.get_nano_fractions() != 0) {
    return MakeEvalError()
           << "Bucket width INTERVAL with nanoseconds precision is not allowed";
  }
  // Nano fractions can't be negative, so only checking months, days and micros
  // here.
  if (bucket_width.get_months() < 0 || bucket_width.get_days() < 0 ||
      bucket_width.get_micros() < 0) {
    return MakeEvalError() << "Negative bucket width INTERVAL is not allowed";
  }
  // We count micros and nano_fractions as one field since they are logically
  // represent one field - nanoseconds.
  int fields_set =
      (bucket_width.get_months() > 0 ? 1 : 0) +
      (bucket_width.get_days() > 0 ? 1 : 0) +
      ((bucket_width.get_micros() > 0 || bucket_width.get_nano_fractions() > 0)
           ? 1
           : 0);
  if (fields_set != 1) {
    return MakeEvalError()
           << "Exactly one non-zero INTERVAL part in bucket width is required";
  }
  return absl::OkStatus();
}

absl::StatusOr<DatetimeBucketizer> DatetimeBucketizer::Create(
    IntervalValue bucket_width, DatetimeValue origin, TimestampScale scale) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateBucketWidth(bucket_width, scale));
  // Here we branch out into handling MONTHs and other interval types.
  // MONTH is special since it's a non-fixed interval, therefore it requires
  // use of civil time library to perform all arithmetic on dates.
  if (bucket_width.get_months() > 0) {
    absl::CivilSecond origin_civil = origin.ConvertToCivilSecond();
    bool origin_last_day_of_month =
        IsLastDayOfTheMonth(static_cast<int>(origin_civil.year()),
                            origin_civil.month(), origin_civil.day());
    return DatetimeBucketizer(MonthBucketState{
        .bucket_width_months = bucket_width.get_months(),
        .origin = origin_civil,
        .origin_nanos_part = origin.Nanoseconds(),
        .origin_last_day_of_month = origin_last_day_of_month,
    });
  } else {
    // In this branch we either have an interval with days part or nanos part.
    // In DATETIME we can assume that a day is equal to 24 hours, therefore
    // we just convert a day part to nanoseconds.
    absl::int128 bucket_width_nanos =
        bucket_width.get_days() > 0
            ? absl::int128(bucket_width.get_days()) * IntervalValue::kNanosInDay
            : bucket_width.get_nanos();
    return DatetimeBucketizer(NanosBucketState{
        .bucket_width_nanos = bucket_width_nanos,
        .origin_nanos = DatetimeToNanos(origin),
    });
  }
}

absl::Status DatetimeBucketizer::Compute(const DatetimeValue& input,
                                         DatetimeValue* output) const {
  if (std::holds_alternative<MonthBucketState>(state_)) {
    *output = ComputeForMonthsBucket(std::get<MonthBucketState>(state_), input);
  } else {
    *output = ComputeForNanosBucket(std::get<NanosBucketState>(state_), input);
  }
  if (!output->IsValid()) {
    return MakeEvalError() << "Bucket for " << input.DebugString()
                           << " is outside of datetime range";
  }
  return absl::OkStatus();
}

DatetimeValue DatetimeBucketizer::ComputeForMonthsBucket(
    const MonthBucketState& state, const DatetimeValue& input) {
  const absl::CivilSecond& origin = state.origin;
  absl::CivilMonth input_civil_month =
      absl::CivilMonth(input.ConvertToCivilSecond());
  absl::CivilMonth origin_civil_month = absl::CivilMonth(origin);
  int64_t rem =
      (input_civil_month - origin_civil_month) % state.bucket_width_months;
  absl::CivilMonth result = input_civil_month - rem;

  // We consider input and origin day to be equal when they both are the
  // ends of the month, so when that happens we just set input day to the
  // origin day, which we only use for comparison purposes.
  int input_day = input.Day();
  int origin_day = origin.day();
  if (state.origin_last_day_of_month &&
      IsLastDayOfTheMonth(input.Year(), input.Month(), input.Day())) {
    input_day = origin_day;
  }

  auto to_nanos = [](int day, int hour, int minute, int second,
                     int nanosecond) -> int64_t {
    return kNumNanosPerDay * day + kNumNanosPerHour * hour +
           kNumNanosPerMinute * minute + kNumNanosPerSecond * second +
           nanosecond;
  };
  int64_t input_sub_month_parts_nanos =
      to_nanos(input_day, input.Hour(), input.Minute(), input.Second(),
               input.Nanoseconds());
  int64_t origin_sub_month_parts_nanos =
      to_nanos(origin_day, origin.hour(), origin.minute(), origin.second(),
               state.origin_nanos_part);

  // Negative remainder indicates that input < origin. When input precedes
  // origin we need shift the result backwards by one bucket.
  //
  // We also shift the result to the previous bucket when the input's
  // sub-month parts are less than origin's. This compensates for the fact
  // that when we did the math on CivilMonth we completely discarded
  // sub-month parts.
  if (rem < 0 || (rem == 0 &&
                  input_sub_month_parts_nanos < origin_sub_month_parts_nanos)) {
    result -= state.bucket_width_months;
  }

  // cast is safe, given method contract.
  int year = static_cast<int>(result.year());
  int month = result.month();
  int day = origin.day();
  // AdjustYearMonthDay takes care of handling last day of the month case: if
  // the resulting month has fewer days than the origin's month, then the
  // result day is the last day of the result month.
  AdjustYearMonthDay(&year, &month, &day);
  return DatetimeValue::FromYMDHMSAndNanos(year, month, day, origin.hour(),
                                           origin.minute(), origin.second(),
                                           state.origin_nanos_part);
}

DatetimeValue DatetimeBucketizer::ComputeForNanosBucket(
    const NanosBucketState& state, const DatetimeValue& input) {
  const absl::int128& origin_nanos = state.origin_nanos;
  const absl::int128& bucket_width_nanos = state.bucket_width_nanos;
  // Note that since we do all calculation in int128 we don't have to check
  // for overflows. Representing 10,000 years (max value of DATETIME) in
  // nanoseconds only requires 69 bits.
  absl::int128 input_nanos = DatetimeToNanos(input);
  absl::int128 rem = (input_nanos - origin_nanos) % bucket_width_nanos;
  absl::int128 result = input_nanos - rem;
  if (rem < 0) {
    // Negative remainder indicates that input < origin. When input precedes
    // origin we need shift the result backwards by one bucket.
    result -= bucket_width_nanos;
  }
  return NanosToDatetime(result);
}

absl::int128 DatetimeBucketizer::DatetimeToNanos(
    const DatetimeValue& datetime) {
  return absl::int128(datetime.ConvertToCivilSecond() - kEpochCivil) *
             IntervalValue::kNanosInSecond +
         datetime.Nanoseconds();
}

DatetimeValue DatetimeBucketizer::NanosToDatetime(absl::int128 nanos) {
  // 40 bits are required to represent number of seconds in 10,000 years.
  absl::CivilSecond civil_seconds_part =
      kEpochCivil + static_cast<int64_t>(nanos / IntervalValue::kNanosInSecond);
  // nanos is guaranteed to be a positive number due to a choice of
  // kEpochCivil, thefore we don't need handle a case when civil_seconds_part
  // is negative.
  int32_t nanos_part =
      static_cast<int32_t>(nanos % IntervalValue::kNanosInSecond);
  return DatetimeValue::FromYMDHMSAndNanos(
      static_cast<int32_t>(civil_seconds_part.year()),
      civil_seconds_part.month(), civil_seconds_part.day(),
      civil_seconds_part.hour(), civil_seconds_part.minute(),
      civil_seconds_part.second(), nanos_part);
}

absl::Status DatetimeBucket(const DatetimeValue& input,
                            IntervalValue bucket_width,
                            const DatetimeValue& origin, TimestampScale scale,
                            DatetimeValue* output) {
  GOOGLESQL_ASSIGN_OR_RETURN(DatetimeBucketizer bucketizer,
                   DatetimeBucketizer::Create(bucket_width, origin, scale));
  return bucketizer.Compute(input, output);
}

absl::Status DateBucketizer::ValidateBucketWidth(IntervalValue bucket_width) {
  if (bucket_width.get_micros() > 0 || bucket_width.get_nano_fractions() > 0) {
    return MakeEvalError()
           << "Only MONTH and DAY parts are allowed in bucket width INTERVAL";
  }
  if (bucket_width.get_months() < 0 || bucket_width.get_days() < 0) {
    return MakeEvalError() << "Negative bucket width INTERVAL is not allowed";
  }
  if ((bucket_width.get_months() > 0) == (bucket_width.get_days() > 0)) {
    return MakeEvalError()
           << "Exactly one non-zero INTERVAL part in bucket width is required";
  }
  return absl::OkStatus();
}

absl::StatusOr<DateBucketizer> DateBucketizer::Create(
    IntervalValue bucket_width, int32_t origin_date) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateBucketWidth(bucket_width));
  // Here we branch out into handling MONTHs and DAY interval types.
  // MONTH is special since it's a non-fixed interval, therefore it requires
  // use of civil time library to perform all arithmetic on dates.
  if (bucket_width.get_months() > 0) {
    absl::CivilDay origin_civil = EpochDaysToCivilDay(origin_date);
    bool origin_last_day_of_month =
        IsLastDayOfTheMonth(static_cast<int>(origin_civil.year()),
                            origin_civil.month(), origin_civil.day());
    return DateBucketizer(MonthBucketState{
        .bucket_width_months = bucket_width.get_months(),
        .origin = origin_civil,
        .origin_last_day_of_month = origin_last_day_of_month,
    });
  } else {
    return DateBucketizer(DaysBucketState{
        .bucket_width_days = bucket_width.get_days(),
        .origin_date = origin_date,
    });
  }
}

absl::Status DateBucketizer::Compute(int32_t input_date,
                                     int32_t* output_date) const {
  if (std::holds_alternative<MonthBucketState>(state_)) {
    *output_date =
        ComputeForMonthsBucket(std::get<MonthBucketState>(state_), input_date);
  } else {
    *output_date =
        ComputeForDaysBucket(std::get<DaysBucketState>(state_), input_date);
  }
  if (ABSL_PREDICT_FALSE(!IsValidDate(*output_date))) {
    std::string input_date_str;
    GOOGLESQL_RETURN_IF_ERROR(ConvertDateToString(input_date, &input_date_str));
    return MakeEvalError() << "Bucket for " << input_date_str
                           << " is outside of date range";
  }
  return absl::OkStatus();
}

int32_t DateBucketizer::ComputeForMonthsBucket(const MonthBucketState& state,
                                               int32_t input_date) {
  absl::CivilDay input_civil = EpochDaysToCivilDay(input_date);
  absl::CivilDay origin_civil = state.origin;
  absl::CivilMonth input_month = absl::CivilMonth(input_civil);
  absl::CivilMonth origin_month = absl::CivilMonth(origin_civil);
  int64_t rem = (input_month - origin_month) % state.bucket_width_months;
  absl::CivilMonth result = input_month - rem;

  // We consider input and origin day to be equal when they both are the
  // ends of the month, so when that happens we just set input day to the
  // origin day, which we only use for comparison purposes.
  int input_day = input_civil.day();
  int origin_day = origin_civil.day();
  if (state.origin_last_day_of_month &&
      IsLastDayOfTheMonth(static_cast<int>(input_civil.year()),
                          input_civil.month(), input_civil.day())) {
    input_day = origin_civil.day();
  }

  // Negative remainder indicates that input < origin. When input precedes
  // origin we need shift the result backwards by one bucket.
  //
  // We also shift the result to the previous bucket when the input day is
  // less than origin day. This compensates for the fact that when we did the
  // math on CivilMonth we completely discarded days.
  if (rem < 0 || (rem == 0 && input_day < origin_day)) {
    result -= state.bucket_width_months;
  }

  // cast is safe, given method contract.
  int year = static_cast<int32_t>(result.year());
  int month = result.month();
  int day = origin_civil.day();
  // AdjustYearMonthDay takes care of handling last day of the month case: if
  // the resulting month has fewer days than the origin's month, then the
  // result day is the last day of the result month.
  AdjustYearMonthDay(&year, &month, &day);
  return CivilDayToEpochDays(absl::CivilDay(year, month, day));
}

int32_t DateBucketizer::ComputeForDaysBucket(const DaysBucketState& state,
                                             int32_t input_date) {
  int64_t bucket_size = state.bucket_width_days;
  int64_t rem =
      (static_cast<int64_t>(input_date) - state.origin_date) % bucket_size;
  int64_t result = static_cast<int64_t>(input_date) - rem;
  if (rem < 0) {
    // Negative remainder indicates that input < origin. When input precedes
    // origin we need shift the result backwards by one bucket.
    result -= bucket_size;
  }
  // Check for an overflow.
  if (ABSL_PREDICT_FALSE(result > std::numeric_limits<int32_t>::max() ||
                         result < std::numeric_limits<int32_t>::min())) {
    static_assert(std::numeric_limits<int32_t>::max() > types::kDateMax,
                  "Max int32_t value is expected to be larger than max DATE");
    // If the result overflows int32_t, we can just set the result to max
    // int32_t to preserve the overflow state, but with a value representable in
    // int32_t.
    result = std::numeric_limits<int32_t>::max();
  }
  // Cast is safe here, since we checked for an overflow.
  return static_cast<int32_t>(result);
}

absl::Status DateBucket(int32_t input_date, IntervalValue bucket_width,
                        int32_t origin_date, int32_t* output_date) {
  GOOGLESQL_ASSIGN_OR_RETURN(DateBucketizer bucketizer,
                   DateBucketizer::Create(bucket_width, origin_date));
  return bucketizer.Compute(input_date, output_date);
}

namespace internal_functions {

// Expand "%Z" in <format_string> to the GoogleSQL-defined format:
//   'UTC[+/-HHMM]'
// The format produced by absl::FormatTime() is different from the GoogleSQL
// format and has not been stable over time, so we handle %Z here rather than
// pass it through to FormatTime().
//
// Expand "%Q" in <format_string> into 1 based quarter number.  This is
// GoogleSQL's extension to strftime format strings (see b/26564776).  We have
// to do it here because absl::FormatTime does not support %Q.
//
// Expand "%J" in <format_string> into an ISO day of year number.  This is a
// GoogleSQL extension to strftime, and we have to do it here because
// absl::FormatTime does not support %J.
//
// Requires that the <expanded_format_string> is empty, and if %Z is present
// then the <timezone> is normalized to an hours/minutes offset
// (i.e., <timezone> does not include any 'seconds' offset).
absl::Status ExpandPercentZQJ(absl::string_view format_string,
                              absl::Time base_time, absl::TimeZone timezone,
                              const ExpansionOptions& expansion_options,
                              std::string* expanded_format_string) {
  GOOGLESQL_RET_CHECK(expanded_format_string->empty());
  if (format_string.empty()) {
    return absl::OkStatus();
  }
  expanded_format_string->reserve(format_string.size());

  for (size_t index = 0;; index += 2) {
    const size_t pct = format_string.find('%', index);
    if (pct == format_string.size() - 1 ||
        pct == std::string::npos) {  // no "%?"
      absl::StrAppend(
          expanded_format_string,
          format_string.substr(index, format_string.size() - index));
      break;
    }
    if (pct != index) {
      absl::StrAppend(expanded_format_string,
                      format_string.substr(index, pct - index));
      index = pct;
    }
    if (expansion_options.expand_quarter && format_string[pct + 1] == 'Q') {
      // Handle %Q, computing quarter from month.
      absl::StrAppend(
          expanded_format_string,
          absl::StrFormat(
              "%d",
              (absl::ToCivilMonth(base_time, timezone).month() - 1) / 3 + 1));
    } else if (format_string[pct + 1] == 'Z') {
      // Handle %Z, computing the GoogleSQL defined timezone format.
      absl::StrAppend(expanded_format_string, "UTC");
      if (int seconds = timezone.At(base_time).offset) {
        const char sign = (seconds < 0 ? '-' : '+');
        int minutes = seconds / 60;
        seconds %= 60;
        if (sign == '-') {
          if (seconds > 0) {
            seconds -= 60;
            minutes += 1;
          }
          seconds = -seconds;
          minutes = -minutes;
        }
        int hours = minutes / 60;
        minutes %= 60;
        expanded_format_string->push_back(sign);
        GOOGLESQL_RET_CHECK_EQ(seconds, 0);
        if (minutes != 0) {
          absl::StrAppend(expanded_format_string,
                          absl::StrFormat("%02d%02d", hours, minutes));
        } else {
          absl::StrAppend(expanded_format_string, absl::StrFormat("%d", hours));
        }
      }
    } else if (expansion_options.expand_iso_dayofyear &&
               format_string[pct + 1] == 'J') {
      return MakeEvalError() << "Format element %J not supported yet";
    } else {
      // None of %J, %Q, %Z. Copy as is.
      absl::StrAppend(expanded_format_string, format_string.substr(index, 2));
    }
  }
  return absl::OkStatus();
}

int DayOfWeekIntegerSunToSat1To7(absl::Weekday weekday) {
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

// Returns the timezone sign, hour and minute.
void GetSignHourAndMinuteTimeZoneOffset(const absl::TimeZone::CivilInfo& info,
                                        bool* positive_offset,
                                        int32_t* hour_offset,
                                        int32_t* minute_offset) {
  int32_t second_offset = info.offset;
  if (info.offset >= 0) {
    *positive_offset = true;
  } else {
    *positive_offset = false;
    second_offset = -second_offset;
  }

  *hour_offset = second_offset / 60 / 60;
  *minute_offset = (second_offset / 60) % 60;
}

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
                                     absl::TimeZone timezone) {
  const int timezone_offset = timezone.At(base_time).offset;
  if (const int seconds_offset = timezone_offset % 60)
    return absl::FixedTimeZone(timezone_offset - seconds_offset);
  return timezone;
}

// Expands the subsecond format elements, "%E<number>S", "%E*S", "%E<number>f"
// or "%E*f", in <format_string> to contains the subnanosecond value
// <subnanosecond> and returns the new format string.
//
// E.g.
// - <format_string> is "%E*S", and <subnanosecond> is 120, the return value is
//   "%E9S12".
// - <format_string> is "%E*f", and <subnanosecond> is 120, the return value is
//   "%E9f12".
// - <format_string> is "%E12S", and <subnanosecond> is 120, the return value is
//   "%E9S120".
// - <format_string> is "%E12f", and <subnanosecond> is 120, the return value is
//   "%E9f120".
//
// REQUIRES: <subnanoseconds> is in the range [0, 999].
absl::StatusOr<std::string> AddPicosecondsInFormatString(
    absl::string_view format_string, uint32_t subnanosecond) {
  if (subnanosecond > 999) {
    return MakeEvalError() << "Valid range of subnanosecond is [0, 999], but "
                              "the input value is "
                           << subnanosecond;
  }

  // The string representation of the subnanosecond value. E.g. when
  // subnanosecond is 120, its value will be "120".
  std::string picos_str;

  // The string representation of the subnanosecond value, without trailing
  // zeros. E.g. when subnanosecond is 120, its value will be "12".
  std::string picos_str_without_trailing_zeros;

  // Generating picos_str_without_trailing_zeros and picos_str, in reverse.
  uint32_t v = subnanosecond;
  for (int i = 0; i < 3; ++i) {
    if (v % 10 != 0 || !picos_str_without_trailing_zeros.empty()) {
      absl::StrAppend(&picos_str_without_trailing_zeros, v % 10);
    }

    absl::StrAppend(&picos_str, v % 10);
    v /= 10;
  }

  // Reverse the strings to get the final result.
  std::reverse(picos_str.begin(), picos_str.end());
  std::reverse(picos_str_without_trailing_zeros.begin(),
               picos_str_without_trailing_zeros.end());

  // Scanning the format string to find "%E<number>S"/"%E*S" and replace them
  // with new strings. A state machine is used. The initial state is kDefault.
  // The state machine works as follows:
  // - In state kDefault:
  //   - Input is "%E", state is changed to kPercentEParsed.
  //   - Input is "%%", state is unchanged.
  //   - Otherwise, state is unchanged.
  // - In state kPercentEParsed:
  //   - Input is "*S" (or "*f"). "%E*S" (or "%E*f") is found. State is changed
  //     to kDefault;
  //   - Input is a digit, state is changed to kDigitParsed;
  //   - Otherwise, state is changed to kDefault;
  // - In state kDigitParsed:
  //   - Input is a digit, state remains in kDigitParsed;
  //   - Input is "S" (or "f"). "%E<number>S" (or "%E<number>f")  is
  //     found. State is changed to kDefault;
  //   - Otherwise, state is changed to kDefault;
  enum State {
    kDefault,
    kPercentEParsed,
    kDigitParsed,
  };

  std::string format_string_with_picos;
  enum State state = kDefault;
  size_t i = 0;

  // The index of character "%" when "%E" is parsed; It's npos if "%E" is not
  // parsed.
  size_t idx_percent_e = std::string::npos;

  while (i < format_string.length()) {
    switch (state) {
      case kDefault:
        if (i + 1 < format_string.length() && format_string[i] == '%' &&
            format_string[i + 1] == 'E') {
          state = kPercentEParsed;
          idx_percent_e = i;
          i += 2;
        } else if (i + 1 < format_string.length() && format_string[i] == '%' &&
                   format_string[i + 1] == '%') {
          format_string_with_picos.push_back('%');
          format_string_with_picos.push_back('%');
          i += 2;
        } else {
          format_string_with_picos.push_back(format_string[i]);
          i++;
        }
        break;
      case kPercentEParsed:
        if (i + 1 < format_string.length() && format_string[i] == '*' &&
            (format_string[i + 1] == 'S' || format_string[i + 1] == 'f')) {
          // "%E*S" (or "%E*f") is found.
          if (subnanosecond == 0) {
            // The subnanosecond is 0. In this, there is no need to replace this
            // format element. Copy it as-is to the output.
            format_string_with_picos.append(
                format_string.substr(idx_percent_e, i + 2 - idx_percent_e));
          } else {
            absl::StrAppend(&format_string_with_picos, "%E9",
                            std::string(1, format_string[i + 1]),
                            picos_str_without_trailing_zeros);
          }
          state = kDefault;
          idx_percent_e = std::string::npos;

          // Increment by 2 to skip "*S" (or "*f").
          i += 2;
        } else if (absl::ascii_isdigit(format_string[i])) {
          state = kDigitParsed;
          i++;
        } else {
          format_string_with_picos.append(
              format_string.substr(idx_percent_e, i - idx_percent_e));
          state = kDefault;
          idx_percent_e = std::string::npos;

          // If the format string starts with "%E%", the execution will reach
          // this point, with i pointing at the 2nd percent sign. Since that
          // percent sign can be the first character of a valid
          // "%E<number>S"/"%E*S" format element, we do not increment i in this
          // case, so that the format element, if exists, can be recognized and
          // processed.
        }
        break;
      case kDigitParsed:
        if (absl::ascii_isdigit(format_string[i])) {
          i++;
        } else if (format_string[i] == 'S' || format_string[i] == 'f') {
          // "%E<number>S" (or "%E<number>f") is found.
          int num_digits;
          if (!absl::SimpleAtoi(format_string.substr(idx_percent_e + 2,
                                                     i - idx_percent_e - 2),
                                &num_digits)) {
            num_digits = 1025;
          }

          // In absl::FormatTime(), if the number in "%E<number>S" is greater
          // than 1024 (See
          // (broken link);l=549;rcl=716441858),
          // then the format element is just copied as is in the output. We
          // duplicate this behavior here.
          if (num_digits > 9 && num_digits <= 1024) {
            // The maximum number of subsecond digits is 18. See
            // (broken link);l=557;rcl=716441858.
            if (num_digits > 18) {
              num_digits = 18;
            }
            format_string_with_picos.append("%E9");
            format_string_with_picos += format_string[i];

            // Append subnanosecond digits.
            for (int k = 0; k < num_digits - 9; k++) {
              format_string_with_picos.push_back(
                  (k < picos_str.length() ? picos_str[k] : '0'));
            }
          } else {
            // The precision is less than or equal to nanosecond, or num_digits
            // > 1024. In this, there is no need to replace this format
            // element. Copy it as-is to the output.
            format_string_with_picos.append(
                format_string.substr(idx_percent_e, i - idx_percent_e + 1));
          }
          state = kDefault;
          idx_percent_e = std::string::npos;
          i++;
        } else {
          format_string_with_picos.append(
              format_string.substr(idx_percent_e, i - idx_percent_e));
          idx_percent_e = std::string::npos;
          state = kDefault;

          // If the format string starts with "%E<number>%", the execution will
          // reach this point, with i pointing at the 2nd percent sign. Since
          // that percent sign can be the first character of a valid
          // "%E<number>S"/"%E*S" format element, we do not increment i in this
          // case, so that the format element, if exists, can be recognized and
          // processed.
        }
        break;
    }
  }

  if (idx_percent_e != std::string::npos) {
    // For example, if the format string ends with "%E12", we'll reach this
    // point. Because "%E12" has not been copied to the output yet, we need to
    // copy it here.
    format_string_with_picos.append(format_string.substr(
        idx_percent_e, format_string.length() - idx_percent_e));
  }

  return format_string_with_picos;
}

}  // namespace internal_functions
}  // namespace functions
}  // namespace googlesql
