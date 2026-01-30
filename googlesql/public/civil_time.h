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

#ifndef GOOGLESQL_PUBLIC_CIVIL_TIME_H_
#define GOOGLESQL_PUBLIC_CIVIL_TIME_H_

#include <compare>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/time/civil_time.h"
#include "absl/types/compare.h"

namespace googlesql {

// The TimeValue and DatetimeValue classes below are used to represent TIME
// and DATETIME values. These wrapper classes include conversion methods to
// convert the values to/from an integer bitfield encoding.
//
// The valid range and number of bits required by each date/time field is as the
// following:
//
// Field     Range           #googlesql_base::Bits
// Year      [1, 9999]          14
// Month     [1, 12]             4
// Day       [1, 31]             5
// Hour      [0, 23]             5
// Minute    [0, 59]             6
// Second    [0, 59]*            6
// Micros    [0, 999999]        20
// Nanos     [0, 999999999]     30
//
// * Leap second is not supported.
//
// Generally, when encoding the TIME or DATETIME into a bit field, larger
// date/time field is on the more significant side.
//
//
// At whole-second precision:
//
// TIME values, containing hour/minute/second, are encoded into a 4-byte
// bit fields as the following:
//      3         2         1
// MSB 10987654321098765432109876543210 LSB
//                    | H ||  M ||  S |
//
// DATETIME values, containing year/month/day/hour/minute/second, are
// encoded into 8-byte bit fields as the following:
//        6         5         4         3         2         1
// MSB 3210987654321098765432109876543210987654321098765432109876543210 LSB
//                             |--- year ---||m || D || H ||  M ||  S |
//
//
// At microsecond precision:
//
// TIME values, containing hour/minute/second/micros, are encoded into 8-byte
// bit fields as the following:
//        6         5         4         3         2         1
// MSB 3210987654321098765432109876543210987654321098765432109876543210 LSB
//                                | H ||  M ||  S ||-------micros-----|
//
// DATETIME values, containing year/month/day/hour/minute/second/micros, are
// encoded into 8-byte bit fields as the following:
//        6         5         4         3         2         1
// MSB 3210987654321098765432109876543210987654321098765432109876543210 LSB
//         |--- year ---||m || D || H ||  M ||  S ||-------micros-----|
//
//
// At nanosecond precision:
//
// TIME values, containing hour/minute/second/nanos, are encoded into 8-byte
// bit fields as the following:
//        6         5         4         3         2         1
// MSB 3210987654321098765432109876543210987654321098765432109876543210 LSB
//                      | H ||  M ||  S ||---------- nanos -----------|
//
// However, DATETIME values with nanosecond precision cannot fit into 8-byte
// bit fields, thus there's no encoding method for it.

// A struct for TIME data type, keeping H:M:S.D* within 24 hours.
//
// Valid range is [00:00:00, 24:00:00)
//
// Each time part also has a valid range:
// * Hour: [00, 24)
// * Minute: [00, 60)
// * Second: [00, 60)
// * Nanosecond: [00, 1000000000)
//
// This class does not support leap seconds, so 60 is considered out of range
// for the Second part.
//
// When the TimeValue is invalid, the time parts stored in the TimeValue
// instance are undefined.
//
// Internally this class always keeps sub-seconds with nanosecond precision,
// but it can be constructed and used with microsecond if desired.
//
// Some factory methods perform normalization of time parts that are out of
// range. The normalization will coerce each field within its valid range, and
// adjust adjacent fields to accommodate any relevant carry or borrow. Values
// will wrap around within 24 hours during normalization.
//
// For example:
//   // no special handling for leap second
//   12:34:60   ->   12:35:00
//   12:34:-01   ->   12:33:59
//   12:34:60::123456789   ->  12:35:00::123456789
//   12:34:60::-000000001  ->  12:34:59::999999999
//
class TimeValue {
 public:
  // Construct a valid TimeValue initialized to midnight: 00:00:00.000000000.
  TimeValue();

  // Constructs a TimeValue with hour, minute, second and microseconds.
  //
  // Returns an invalid TimeValue if any time part is outside the valid range.
  static TimeValue FromHMSAndMicros(int32_t hour, int32_t minute,
                                    int32_t second, int32_t microsecond);

  // Like FromHMSAndMicros but will normalize any time parts outside of their
  // expected range and thus always returns a valid TimeValue.
  static TimeValue FromHMSAndMicrosNormalized(int32_t hour, int32_t minute,
                                              int32_t second,
                                              int32_t microsecond);

  // Construct a TimeValue with a bit field encoding hour/minute/second, and
  // another integer for micros.
  //
  // Returns an invalid TimeValue if any time part is outside the valid range.
  static TimeValue FromPacked32SecondsAndMicros(int32_t bit_field_time_seconds,
                                                int32_t microsecond);

  // Construct a TimeValue with a bit field encoding
  // hour/minute/second/micros.
  //
  // Returns an invalid TimeValue if any time part is outside the valid range.
  static TimeValue FromPacked64Micros(int64_t bit_field_time_micros);

  // Construct a TimeValue with hour, minute, second and nanoseconds.
  //
  // Returns an invalid TimeValue if any time part is outside the valid range.
  static TimeValue FromHMSAndNanos(int32_t hour, int32_t minute, int32_t second,
                                   int32_t nanosecond);

  // Like FromHMSAndNanos but will normalize any time parts outside of their
  // expected range and thus always returns a valid TimeValue.
  static TimeValue FromHMSAndNanosNormalized(int32_t hour, int32_t minute,
                                             int32_t second,
                                             int32_t nanosecond);

  // Construct a TimeValue with a bit field encoding hour/minute/second, and
  // another integer for nanoseconds.
  //
  // Returns an invalid TimeValue if any time part is outside the valid range.
  static TimeValue FromPacked32SecondsAndNanos(int32_t bit_field_time_seconds,
                                               int32_t nanosecond);

  // Construct a TimeValue with a bit field encoding hour/minute/second/nanos.
  //
  // Returns an invalid TimeValue if any time part is outside the valid range.
  static TimeValue FromPacked64Nanos(int64_t bit_field_time_nanos);

  // Return a debug string like:
  //   "03:04:05.123456789"
  // where trailing 000's in the sub-second part will be trimmed, and if
  // sub-second part is 0, the trailing . will also be trimmed.
  std::string DebugString() const;

  // A TimeValue is invalid when one of the time parts (e.g. hours, minutes,
  // etc) supplied to a non-normalized factory function is outside the specified
  // range.
  bool IsValid() const { return valid_; }

  int Hour() const { return hour_; }
  int Minute() const { return minute_; }
  int Second() const { return second_; }
  // Truncation will be applied when getting the sub-seconds at micros
  // precision. For example, for a TimeValue 01:02:03.123456789,
  // getting sub-seconds at micros precision will return 123456.
  int Microseconds() const { return nanosecond_ / 1000; }
  int Nanoseconds() const { return nanosecond_; }

  // Pack the hour/minute/second into a bit field.
  int32_t Packed32TimeSeconds() const;
  // Pack the hour/minute/second/micros into a bit field.
  int64_t Packed64TimeMicros() const;
  // Pack the hour/minute/second/nanos into a bit field.
  int64_t Packed64TimeNanos() const;

  // Note that TimeValues with IsValid() false will compare unequal to
  // everything, including other TimeValues with IsValid() false and the exact
  // same contents, which naturally have the same hash. This violates the
  // expectation that is should be rare to find objects with equal hashes that
  // are unequal.
  template <typename H>
  friend H AbslHashValue(H h, const TimeValue& time_value) {
    return H::combine(std::move(h), time_value.IsValid(),
                      time_value.Packed64TimeNanos());
  }

#ifdef __cpp_impl_three_way_comparison
  friend std::partial_ordering operator<=>(const TimeValue& lhs,
                                           const TimeValue& rhs) {
    if (!lhs.IsValid() || !rhs.IsValid()) {
      return std::partial_ordering::unordered;
    }
    return lhs.Packed64TimeNanos() <=> rhs.Packed64TimeNanos();
  }
#endif

 private:
  static TimeValue FromHMSAndNanosInternal(int64_t hour, int64_t minute,
                                           int64_t second, int64_t nanosecond);

  static TimeValue InternalFromPacked64SecondsAndNanos(
      uint64_t bit_field_time_seconds, int64_t nanosecond);

  bool valid_;
  int8_t hour_;
  int8_t minute_;
  int8_t second_;
  int32_t nanosecond_;

  // Copyable
};
inline bool operator==(const TimeValue& lhs, const TimeValue& rhs) {
  return (&lhs == &rhs) || (lhs.IsValid() && rhs.IsValid() &&
                            lhs.Packed64TimeNanos() == rhs.Packed64TimeNanos());
}
inline bool operator!=(const TimeValue& lhs, const TimeValue& rhs) {
  return !(lhs == rhs);
}

// A struct for DATETIME data type, keeping Y-m-d H:M:S.D*.
// Valid range is [0001-01-01 00:00:00, 10000-01-01 00:00:00)
// Leap second is not allowed, so 60 in the SECOND field is considered invalid.
// Internally this class always keeps sub-seconds with nanosecond precision,
// but it can be constructed and used with microsecond if desired.
//
// Some factory functions perform normalization of date or time parts that are
// out of range. The normalization will coerce each field within its valid
// range, and adjust adjacent fields to accommodate any relevant carry or
// borrow. If the years field is out of range after other fields are normalized
// then the produced DatetimeValue is invalid.
//
// For example:
//   2015-11-09 12:34:60   ->   2015-11-09 12:35:00  // leap second
//   2015-11-09 12:34:70   ->   2015-11-09 12:35:10
//   2015-11-09 -5:34:70   ->   2015-11-08 19:35:10
//   2015-02-29 12:34:56   ->   2015-03-01 12:34:56
//   2015-12-31 23:59:60   ->   2016-01-01 00:00:00  // leap second
//
//   0 or negative value is not valid for day-of-month, and it's normalized to
//   the day(s) before the first day of the month.
//   2015-11-00 12:34:56   ->   2015-10-31 12:34:56
//   2015-11--1 12:34:56   ->   2015-10-30 12:34:56
//   2015-11-01 -5:23:56   ->   2015-10-31 19:34:56
//
//   0 or negative value is not valid for month, and it's normalized to
//   the month(s) before the first month of the year
//   2015-00-15 12:34:56   ->   2014-12-15 12:34:56
//
//   It's possible for the day-of-month to be invalid for the month after
//   month normalization, then it will be normalized again to adjust the month
//   and day-of-month.
//   2015--1-31 12:34:56   ->   2014-11-31 12:34:56   ->   2014-12-01 12:34:56
//
//   It's possible for the normalization result to be out-of-range and become
//   invalid.
//   9999-12-31 23:59:60   ->  10000-01-01 00:00:00  // invalid result
//
// Note that when parsing a DatetimeValue value from a string (using
// functions/date_time_util.h), there is a special case for leap seconds.
// Literal times with second :60 will have their subsecond part truncated to
// preserve time ordering as closely as possible.
class DatetimeValue {
 public:
  // Default constructor, constructing an object representing
  // 1970-01-01 00:00:00.000000000.
  DatetimeValue();

  // Construct a DatetimeValue with year, month, day, hour, minute, second and
  // microseconds.
  static DatetimeValue FromYMDHMSAndMicros(int32_t year, int32_t month,
                                           int32_t day, int32_t hour,
                                           int32_t minute, int32_t second,
                                           int32_t microsecond);

  // Like FromYMDHMSAndMicros but values are normalized.
  static DatetimeValue FromYMDHMSAndMicrosNormalized(
      int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
      int32_t second, int32_t microsecond);

  // Construct a DatetimeValue with a absl::CivilSecond object and an
  // integer for micros.
  static DatetimeValue FromCivilSecondAndMicros(absl::CivilSecond civil_second,
                                                int32_t microsecond);

  // Construct a DatetimeValue with a bit field encoding
  // year/month/day/hour/minute/second, and another integer for micros.
  static DatetimeValue FromPacked64SecondsAndMicros(
      int64_t bit_field_datetime_seconds, int32_t microsecond);

  // Construct a DatetimeValue with a bit field encoding
  // year/month/day/hour/minute/second/micros.
  static DatetimeValue FromPacked64Micros(int64_t bit_field_datetime_micros);

  // Construct a DatetimeValue with year, month, day, hour, minute, second and
  // nanoseconds.
  static DatetimeValue FromYMDHMSAndNanos(int32_t year, int32_t month,
                                          int32_t day, int32_t hour,
                                          int32_t minute, int32_t second,
                                          int32_t nanosecond);

  // Like FromYMDHMSAndNanos but values are normalized.
  static DatetimeValue FromYMDHMSAndNanosNormalized(int32_t year, int32_t month,
                                                    int32_t day, int32_t hour,
                                                    int32_t minute,
                                                    int32_t second,
                                                    int32_t nanosecond);

  // Construct a DatetimeValue with a absl::CivilSecond object and an
  // integer for nanos.
  static DatetimeValue FromCivilSecondAndNanos(absl::CivilSecond civil_second,
                                               int32_t nanosecond);

  // Construct a DatetimeValue with a bit field encoding
  // year/month/day/hour/minute/second, and another integer for nanos.
  static DatetimeValue FromPacked64SecondsAndNanos(
      int64_t bit_field_datetime_seconds, int32_t nanosecond);

  // It's impossible to encode all fields for Datetime with nano precision in a
  // single 8 byte integer, so there is no factory function for building a
  // DatetimeValue from a single int64.

  // Return a debug string like:
  //   "2006-01-02 03:04:05.123456789"
  // where trailing 000's in the sub-second part will be trimmed, and if
  // sub-second part is 0, the trailing . will also be trimmed.
  //
  // Invalid value will return "[INVALID]".
  std::string DebugString() const;

  // A DatetimeValue is invalid when one of the date or time parts (e.g. year,
  // hour, etc) supplied to a non-normalized factory function is outside the
  // specified range, or if normailization forces the year part out of range.
  bool IsValid() const { return valid_; }

  int Year() const { return year_; }
  int Month() const { return month_; }
  int Day() const { return day_; }
  int Hour() const { return hour_; }
  int Minute() const { return minute_; }
  int Second() const { return second_; }
  // Truncation will be applied when getting the sub-seconds at micros
  // precision. For example, for a TimeValue 01:02:03.123456789,
  // getting sub-seconds at micros precision will return 123456.
  int Microseconds() const { return nanosecond_ / 1000; }
  int Nanoseconds() const { return nanosecond_; }

  // Pack the year/month/day/hour/minute/second into a bit field.
  int64_t Packed64DatetimeSeconds() const;
  // Pack the year/month/day/hour/minute/second/micros into a bit field.
  int64_t Packed64DatetimeMicros() const;
  // It's impossible to encode all fields for Datetime with nano precision in a
  // single 8 byte integer, so there is no packing function for that.

  absl::CivilSecond ConvertToCivilSecond() const {
    return absl::CivilSecond(year_, month_, day_, hour_, minute_, second_);
  }

  // Note that DatetimeValues with IsValid() false will compare unequal to
  // everything, including other DatetimeValues with IsValid() false and the
  // exact same contents, which naturally have the same hash. This violates the
  // expectation that is should be rare to find objects with equal hashes that
  // are unequal.
  template <typename H>
  friend H AbslHashValue(H h, const DatetimeValue& datetime_value) {
    return H::combine(std::move(h), datetime_value.IsValid(),
                      datetime_value.Packed64DatetimeSeconds(),
                      datetime_value.Nanoseconds());
  }

#ifdef __cpp_impl_three_way_comparison
  friend std::partial_ordering operator<=>(const DatetimeValue& lhs,
                                           const DatetimeValue& rhs) {
    if (!lhs.IsValid() || !rhs.IsValid()) {
      return std::partial_ordering::unordered;
    }
    return std::make_pair(lhs.Packed64DatetimeSeconds(), lhs.Nanoseconds()) <=>
           std::make_pair(rhs.Packed64DatetimeSeconds(), rhs.Nanoseconds());
  }
#endif

 private:
  static DatetimeValue FromYMDHMSAndNanosInternal(int64_t year, int64_t month,
                                                  int64_t day, int64_t hour,
                                                  int64_t minute,
                                                  int64_t second,
                                                  int64_t nanosecond);
  static DatetimeValue FromCivilSecondAndNanosInternal(
      absl::CivilSecond civil_second, int64_t nanosecond);
  static DatetimeValue FromPacked64SecondsAndNanosInternal(uint64_t bit_field,
                                                           int64_t nanosecond);

  int16_t year_;
  int8_t month_;
  int8_t day_;
  int8_t hour_;
  int8_t minute_;
  int8_t second_;
  bool valid_;
  int32_t nanosecond_;

  // Copyable
};

inline bool operator==(const DatetimeValue& lhs, const DatetimeValue& rhs) {
  return (&lhs == &rhs) ||
         (lhs.IsValid() && rhs.IsValid() &&
          lhs.Packed64DatetimeSeconds() == rhs.Packed64DatetimeSeconds() &&
          lhs.Nanoseconds() == rhs.Nanoseconds());
}
inline bool operator!=(const DatetimeValue& lhs, const DatetimeValue& rhs) {
  return !(lhs == rhs);
}
// Masks of micros and nanos are always used on the least significant bits.
static const unsigned int kMicrosMask = 0xFFFFF;     // 20 bits
static const int kMicrosShift = 20;
static const unsigned int kNanosMask  = 0x3FFFFFFF;  // 30 bits
static const int kNanosShift = 30;

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_CIVIL_TIME_H_
