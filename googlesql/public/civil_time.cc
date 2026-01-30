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

#include "googlesql/public/civil_time.h"

#include <cstdint>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "googlesql/base/mathutil.h"

namespace {

// With sub-second part stripped, Y-m-D H:M:S is encoded as the following:
//        6         5         4         3         2         1
// MSB 3210987654321098765432109876543210987654321098765432109876543210 LSB
//                             |--- year ---||m || D || H ||  M ||  S |
static const int kSecondShift = 0;
static const uint64_t kSecondMask = 0b111111;
static const int kMinuteShift = 6;
static const uint64_t kMinuteMask = 0b111111 << kMinuteShift;
static const int kHourShift = 12;
static const uint64_t kHourMask = 0b11111 << kHourShift;
static const int kDayShift = 17;
static const uint64_t kDayMask = 0b11111 << kDayShift;
static const int kMonthShift = 22;
static const uint64_t kMonthMask = 0b1111 << kMonthShift;
static const int kYearShift = 26;

static constexpr int64_t kNanosPerSecond = 1000000000;

inline bool IsValidNanoseconds(int64_t nanoseconds) {
  return nanoseconds >= 0 && nanoseconds < kNanosPerSecond;
}
// A day is strictly 24 hours, an hour is 60 minutes and a minute is 60 seconds.
// Leap seconds are not allowed.
inline bool IsValidTimeFields(int64_t hour, int64_t minute, int64_t second,
                              int64_t nanosecond) {
  return hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 &&
         second < 60 && IsValidNanoseconds(nanosecond);
}

// A day is strictly 24 hours, an hour is 60 minutes and a minute is 60 seconds.
// Leap seconds are not allowed.
inline bool IsValidDatetimeFields(int64_t year, int64_t month, int64_t day,
                                  int64_t hour, int64_t minute, int64_t second,
                                  int64_t nanosecond) {
  // CivilDay helps to determine if the specified day is valid for the month,
  // and in the case of February, the year.
  const absl::CivilDay civil_day(year, month, day);
  return year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
         day <= 31 && civil_day.day() == day &&
         IsValidTimeFields(hour, minute, second, nanosecond);
}

inline int64_t GetPartFromBitField(uint64_t bit_field, uint64_t mask,
                                   int shift) {
  return absl::bit_cast<int64_t>((bit_field & mask) >> shift);
}

// Do not mask the bits when getting the largest part - hour for TimeValue and
// year for DatetimeValue - so that if there is anything on the higher unused
// bits, it will be carried to the largest part and make it out of valid range.
inline int64_t GetLargestPartFromBitField(uint64_t bit_field, int shift) {
  return absl::bit_cast<int64_t>(bit_field >> shift);
}

// Unpack the hours, minutes, and seconds from a packed time representation.
void UnpackHMS(uint64_t bit_field, int64_t* h, int64_t* m, int64_t* s) {
  *h = GetLargestPartFromBitField(bit_field, kHourShift);
  *m = GetPartFromBitField(bit_field, kMinuteMask, kMinuteShift);
  *s = GetPartFromBitField(bit_field, kSecondMask, kSecondShift);
}

// Normalize time parts by carrying any overage of the legal range of each part
// into adjacent fields. Hours are wrapped around the 24 hour clock, so
// hour 24 -> hour 0, hour 25 -> hour 1, hour -1 -> hour 23, etc.
void NormalizeTime(int32_t* h, int32_t* m, int32_t* s, int64_t* ns) {
  int64_t carry_seconds = googlesql_base::MathUtil::FloorOfRatio(*ns, kNanosPerSecond);
  absl::CivilSecond cs(1970, 1, 1, *h, *m, *s);
  cs += carry_seconds;
  *h = cs.hour();
  *m = cs.minute();
  *s = cs.second();
  *ns -= (carry_seconds * kNanosPerSecond);
  // The CivilTime constructor should have coerced all the values to the
  // appropriate range.
  ABSL_DCHECK(IsValidTimeFields(*h, *m, *s, *ns));
}

// Normalize date and time parts by carrying any overage of the legal range of
// each part into adjacent fields.
void NormalizeDatetime(int64_t* y, int32_t* mo, int32_t* d, int32_t* h,
                       int32_t* m, int32_t* s, int64_t* ns) {
  int64_t carry_seconds = googlesql_base::MathUtil::FloorOfRatio(*ns, kNanosPerSecond);
  absl::CivilSecond cs(*y, *mo, *d, *h, *m, *s);
  cs += carry_seconds;
  *y = cs.year();
  *mo = cs.month();
  *d = cs.day();
  *h = cs.hour();
  *m = cs.minute();
  *s = cs.second();
  *ns -= (carry_seconds * kNanosPerSecond);
  // The CivilTime constructor should have coerced all the time values to the
  // appropriate range.
  ABSL_DCHECK(IsValidTimeFields(*h, *m, *s, *ns));
}

}  // namespace

namespace googlesql {

static_assert(sizeof(TimeValue) <= 8, "TimeValue is larger than 8 bytes");

TimeValue::TimeValue()
    : valid_(true), hour_(0), minute_(0), second_(0), nanosecond_(0) {}

TimeValue TimeValue::FromHMSAndNanos(int32_t hour, int32_t minute,
                                     int32_t second, int32_t nanosecond) {
  return FromHMSAndNanosInternal(hour, minute, second, nanosecond);
}

TimeValue TimeValue::FromHMSAndNanosNormalized(int32_t hour, int32_t minute,
                                               int32_t second,
                                               int32_t nanosecond) {
  int64_t nanos64 = static_cast<int64_t>(nanosecond);
  NormalizeTime(&hour, &minute, &second, &nanos64);
  TimeValue ret = FromHMSAndNanosInternal(hour, minute, second, nanos64);
  ABSL_DCHECK(ret.IsValid());
  return ret;
}

TimeValue TimeValue::FromHMSAndMicros(int32_t hour, int32_t minute,
                                      int32_t second, int32_t microsecond) {
  int64_t nanosecond = static_cast<int64_t>(microsecond) * 1000;
  return FromHMSAndNanosInternal(hour, minute, second, nanosecond);
}

TimeValue TimeValue::FromHMSAndMicrosNormalized(int32_t hour, int32_t minute,
                                                int32_t second,
                                                int32_t microsecond) {
  int64_t nanos64 = static_cast<int64_t>(microsecond) * 1000;
  NormalizeTime(&hour, &minute, &second, &nanos64);
  TimeValue ret = FromHMSAndNanosInternal(hour, minute, second, nanos64);
  ABSL_DCHECK(ret.IsValid());
  return ret;
}

TimeValue TimeValue::FromHMSAndNanosInternal(int64_t hour, int64_t minute,
                                             int64_t second,
                                             int64_t nanosecond) {
  TimeValue ret;
  ret.valid_ = IsValidTimeFields(hour, minute, second, nanosecond);
  if (ret.valid_) {
    // These are narrowing casts. We know they do not lose information because
    // IsValidTimeFields checks that the value ranges are appropriate.
    ret.hour_ = static_cast<int8_t>(hour);
    ret.minute_ = static_cast<int8_t>(minute);
    ret.second_ = static_cast<int8_t>(second);
    ret.nanosecond_ = static_cast<int32_t>(nanosecond);
  } else {
    // When TimeValue is invalid, also set hour to -1 to make it more likely
    // the difference between an invalid and default initialized TimeValue will
    // be noticed.
    ret.hour_ = -1;
  }
  return ret;
}

TimeValue TimeValue::InternalFromPacked64SecondsAndNanos(
    uint64_t bit_field_time_seconds, int64_t nanosecond) {
  int64_t hour, minute, second;
  UnpackHMS(bit_field_time_seconds, &hour, &minute, &second);
  return FromHMSAndNanosInternal(hour, minute, second, nanosecond);
}

TimeValue TimeValue::FromPacked64Micros(int64_t bit_field_time_micros) {
  uint64_t bit_field = absl::bit_cast<uint64_t>(bit_field_time_micros);
  int64_t microsecond =
      GetPartFromBitField(bit_field, kMicrosMask, /*shift=*/0);
  // Cannot overflow because micros is less than 1 << 20.
  ABSL_DCHECK_LT(microsecond, 1 << 20);
  int64_t nanosecond = microsecond * 1000;
  return InternalFromPacked64SecondsAndNanos(bit_field >> kMicrosShift,
                                             nanosecond);
}

TimeValue TimeValue::FromPacked64Nanos(int64_t bit_field_time_nanos) {
  uint64_t bit_field = absl::bit_cast<uint64_t>(bit_field_time_nanos);
  int64_t nanosecond = GetPartFromBitField(bit_field, kNanosMask, /*shift=*/0);
  return InternalFromPacked64SecondsAndNanos(bit_field >> kNanosShift,
                                             nanosecond);
}

TimeValue TimeValue::FromPacked32SecondsAndNanos(int32_t bit_field_time_seconds,
                                                 int32_t nanosecond) {
  uint32_t bit_field = absl::bit_cast<uint32_t>(bit_field_time_seconds);
  return InternalFromPacked64SecondsAndNanos(bit_field, nanosecond);
}

TimeValue TimeValue::FromPacked32SecondsAndMicros(
    int32_t bit_field_time_seconds, int32_t microsecond) {
  uint32_t bit_field = absl::bit_cast<uint32_t>(bit_field_time_seconds);
  int64_t nanosecond = static_cast<int64_t>(microsecond) * 1000;
  return InternalFromPacked64SecondsAndNanos(bit_field, nanosecond);
}

int32_t TimeValue::Packed32TimeSeconds() const {
  // When the Hour-Minute-Second value is incorrect, hour_ is set to -1.
  // It is undefied behavior to left shift a negative number, hence hour_
  // is static_cast-ed to uin8t before being left-shifted and packed into
  // a int32_t value. (b/262060332)
  return (static_cast<uint8_t>(hour_) << kHourShift) |
         (minute_ << kMinuteShift) | (second_ << kSecondShift);
}

int64_t TimeValue::Packed64TimeMicros() const {
  return (static_cast<uint64_t>(Packed32TimeSeconds()) << kMicrosShift) |
         Microseconds();
}

int64_t TimeValue::Packed64TimeNanos() const {
  return (static_cast<uint64_t>(Packed32TimeSeconds()) << kNanosShift) |
         nanosecond_;
}

std::string TimeValue::DebugString() const {
  if (!IsValid()) {
    return "[INVALID]";
  }
  std::string raw_output = absl::StrFormat("%02d:%02d:%02d.%09d", hour_,
                                           minute_, second_, nanosecond_);
  absl::string_view output(raw_output);
  while (absl::ConsumeSuffix(&output, "000")) {
    // Do nothing more
  }
  absl::ConsumeSuffix(&output, ".");
  return std::string(output);
}

static_assert(sizeof(DatetimeValue) <= 12,
              "DatetimeValue is larger than 12 bytes");

DatetimeValue::DatetimeValue()
    : year_(1970),
      month_(1),
      day_(1),
      hour_(0),
      minute_(0),
      second_(0),
      valid_(true),
      nanosecond_(0) {}

DatetimeValue DatetimeValue::FromYMDHMSAndNanosInternal(
    int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute,
    int64_t second, int64_t nanosecond) {
  DatetimeValue ret;
  ret.valid_ =
      IsValidDatetimeFields(year, month, day, hour, minute, second, nanosecond);
  if (ret.valid_) {
    ret.year_ = static_cast<int16_t>(year);
    ret.month_ = static_cast<int8_t>(month);
    ret.day_ = static_cast<int8_t>(day);
    ret.hour_ = static_cast<int8_t>(hour);
    ret.minute_ = static_cast<int8_t>(minute);
    ret.second_ = static_cast<int8_t>(second);
    ret.nanosecond_ = static_cast<int32_t>(nanosecond);
  } else {
    // When DatetimeValue is invalid, also set year to -1 to make it more likely
    // the difference between an invalid and default initialized DatetimeValue
    // will be noticed.
    ret.year_ = -1;
  }
  return ret;
}

DatetimeValue DatetimeValue::FromYMDHMSAndMicros(int32_t year, int32_t month,
                                                 int32_t day, int32_t hour,
                                                 int32_t minute, int32_t second,
                                                 int32_t microsecond) {
  int64_t nanos64 = static_cast<int64_t>(microsecond) * 1000;
  return FromYMDHMSAndNanosInternal(year, month, day, hour, minute, second,
                                    nanos64);
}

DatetimeValue DatetimeValue::FromYMDHMSAndMicrosNormalized(
    int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
    int32_t second, int32_t microsecond) {
  int64_t nanos64 = static_cast<int64_t>(microsecond) * 1000;
  int64_t year64 = static_cast<int64_t>(year);
  NormalizeDatetime(&year64, &month, &day, &hour, &minute, &second, &nanos64);
  return FromYMDHMSAndNanosInternal(year64, month, day, hour, minute, second,
                                    nanos64);
}

DatetimeValue DatetimeValue::FromYMDHMSAndNanos(int32_t year, int32_t month,
                                                int32_t day, int32_t hour,
                                                int32_t minute, int32_t second,
                                                int32_t nanosecond) {
  int64_t nanos64 = static_cast<int64_t>(nanosecond);
  return FromYMDHMSAndNanosInternal(year, month, day, hour, minute, second,
                                    nanos64);
}

DatetimeValue DatetimeValue::FromYMDHMSAndNanosNormalized(
    int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
    int32_t second, int32_t nanosecond) {
  int64_t nanos64 = static_cast<int64_t>(nanosecond);
  int64_t year64 = static_cast<int64_t>(year);
  NormalizeDatetime(&year64, &month, &day, &hour, &minute, &second, &nanos64);
  return FromYMDHMSAndNanosInternal(year64, month, day, hour, minute, second,
                                    nanos64);
}

DatetimeValue DatetimeValue::FromCivilSecondAndMicros(
    absl::CivilSecond civil_second, int32_t microsecond) {
  int64_t nanos64 = static_cast<int64_t>(microsecond) * 1000;
  return FromCivilSecondAndNanosInternal(civil_second, nanos64);
}

DatetimeValue DatetimeValue::FromCivilSecondAndNanos(
    absl::CivilSecond civil_second, int32_t nanosecond) {
  int64_t nanos64 = static_cast<int64_t>(nanosecond);
  return FromCivilSecondAndNanosInternal(civil_second, nanos64);
}

DatetimeValue DatetimeValue::FromCivilSecondAndNanosInternal(
    absl::CivilSecond civil_second, int64_t nanosecond) {
  return FromYMDHMSAndNanosInternal(civil_second.year(), civil_second.month(),
                                    civil_second.day(), civil_second.hour(),
                                    civil_second.minute(),
                                    civil_second.second(), nanosecond);
}

DatetimeValue DatetimeValue::FromPacked64SecondsAndMicros(
    int64_t bit_field_datetime_seconds, int32_t microsecond) {
  uint64_t bit_field = static_cast<uint64_t>(bit_field_datetime_seconds);
  int64_t nanos64 = static_cast<int64_t>(microsecond) * 1000;
  return FromPacked64SecondsAndNanosInternal(bit_field, nanos64);
}

DatetimeValue DatetimeValue::FromPacked64SecondsAndNanos(
    int64_t bit_field_datetime_seconds, int32_t nanosecond) {
  uint64_t bit_field = static_cast<uint64_t>(bit_field_datetime_seconds);
  int64_t nanos64 = static_cast<int64_t>(nanosecond);
  return FromPacked64SecondsAndNanosInternal(bit_field, nanos64);
}

DatetimeValue DatetimeValue::FromPacked64Micros(
    int64_t bit_field_datetime_micros) {
  uint64_t bit_field = static_cast<uint64_t>(bit_field_datetime_micros);
  int64_t nanos64 = (bit_field & kMicrosMask) * 1000;
  return FromPacked64SecondsAndNanosInternal(bit_field >> kMicrosShift,
                                             nanos64);
}

DatetimeValue DatetimeValue::FromPacked64SecondsAndNanosInternal(
    uint64_t bit_field, int64_t nanosecond) {
  return FromYMDHMSAndNanosInternal(
      GetLargestPartFromBitField(bit_field, kYearShift),
      GetPartFromBitField(bit_field, kMonthMask, kMonthShift),
      GetPartFromBitField(bit_field, kDayMask, kDayShift),
      GetPartFromBitField(bit_field, kHourMask, kHourShift),
      GetPartFromBitField(bit_field, kMinuteMask, kMinuteShift),
      GetPartFromBitField(bit_field, kSecondMask, kSecondShift), nanosecond);
}

int64_t DatetimeValue::Packed64DatetimeSeconds() const {
  return (static_cast<uint64_t>(year_) << kYearShift) |
         (month_ << kMonthShift) | (day_ << kDayShift) | (hour_ << kHourShift) |
         (minute_ << kMinuteShift) | (second_ << kSecondShift);
}

int64_t DatetimeValue::Packed64DatetimeMicros() const {
  return static_cast<int64_t>(
      (static_cast<uint64_t>(Packed64DatetimeSeconds()) << kMicrosShift) |
      (nanosecond_ / 1000));
}

std::string DatetimeValue::DebugString() const {
  if (!valid_) {
    return "[INVALID]";
  }
  std::string raw_output =
      absl::StrFormat("%04d-%02d-%02d %02d:%02d:%02d.%09d", year_, month_, day_,
                      hour_, minute_, second_, nanosecond_);
  absl::string_view output(raw_output);
  while (absl::ConsumeSuffix(&output, "000")) {
    // Do nothing more
  }
  absl::ConsumeSuffix(&output, ".");
  return std::string(output);
}

}  // namespace googlesql
