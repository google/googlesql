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

#ifndef GOOGLESQL_PUBLIC_PICO_TIME_H_
#define GOOGLESQL_PUBLIC_PICO_TIME_H_

#include <compare>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>

#include "absl/numeric/int128.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace googlesql {

// This class represents an absolute time with picoseconds precision.
//
// Internally the values are stored using two properties:
// - absl::Time time_, which stores the value up to nanosecond precision,
// - uint32_t picos_, which stores the sub-nanosecond value.
//
// For example, for PicoTime "2234-01-02 03:04:05.123456789123+00", time_ is
// "2234-01-02 03:04:05.123456789", and picos_ is 123.
class PicoTime final {
 public:
  // Representing the Unix epoch, i.e.  "1970-01-01 00:00:00.000000000000 UTC".
  explicit PicoTime() : PicoTime(0, 0) {}

  PicoTime(const PicoTime&) = default;
  PicoTime(PicoTime&&) = default;
  PicoTime& operator=(const PicoTime&) = default;
  PicoTime& operator=(PicoTime&&) = default;

  // Create PicoTime from an absl::Time, which contains the seconds value, and
  // subsecond picoseconds value.
  // For example
  // - if 'time' represents "1234-01-02 03:04:05+00", and picoseconds
  //   is 123456789123, the created PicoTime is "1234-01-02
  //   03:04:05.123456789123+00".
  // - if 'time' represents "1234-01-02 03:04:05+00", and picoseconds
  //   is 123456, the created PicoTime is "1234-01-02
  //   03:04:05.000000123456+00".
  //
  // REQUIRES: `time` does not contain subsecond value, and is within range
  // [0001-01-01, 9999-12-31].
  // REQUIRES: `picoseconds` is less than 1e12.
  static absl::StatusOr<PicoTime> Create(absl::Time time, uint64_t picoseconds);

  // Create PicoTime from an absl::Time.
  static absl::StatusOr<PicoTime> Create(absl::Time time);

  std::string DebugString() const { return ToString(); }

  std::string ToString(absl::TimeZone timezone = absl::UTCTimeZone()) const;

  // Returns the PicoTime value as absl::Time with nanosecond precision. For
  // example, if the PocTime is "1234-01-02 03:04:05.123456789321+00", the
  // return value is absl::Time representing "1234-01-02 03:04:05.123456789+00".
  absl::Time ToAbslTime() const { return time_; }

  // Returns the subnanoseconds value of the PicoTime. For example, if the
  // PicoTime is "1234-01-02 03:04:05.123456789321+00", the return value is 321.
  uint32_t SubNanoseconds() const { return picos_; }

  // Creates a PicoTime from Unix epoch picoseconds.
  static absl::StatusOr<PicoTime> FromUnixPicos(absl::int128 unix_picos);

  // Returns timestamp value as Unix epoch picoseconds.
  absl::int128 ToUnixPicos() const {
    timespec ts = absl::ToTimespec(time_);
    return absl::int128(ts.tv_sec) * kNumPicosPerSecond +
           absl::int128(ts.tv_nsec) * 1000 + absl::int128(picos_);
  }

  // Returns the maximum and minimum PicoTime values.
  // Minimum value is 0001-01-01 00:00:00.000000000000 UTC
  // Maximum value is 9999-12-31 23:59:59.999999999999 UTC
  static constexpr PicoTime MinValue() { return PicoTime(-62135596800, 0); }

  static constexpr PicoTime MaxValue() {
    return PicoTime(253402300799, kNumPicosPerSecond - 1);
  }

  // Returns the value of the PicoTime as a pair of {seconds, picoseconds}. The
  // picoseconds value is non-negative, and is in the range of [0,
  // 999999999999].
  //
  // For example, if the PicoTime is "1234-01-02 03:04:05.123456789321+00", the
  // return value is {second value of timestamp "1234-01-02 03:04:05",
  // 123456789321}.
  std::pair<int64_t, int64_t> SecondsAndPicoseconds() const {
    timespec ts = absl::ToTimespec(time_);
    return {ts.tv_sec, ts.tv_nsec * 1000 + picos_};
  }

  // Returns the smallest system precision setting that can fit the PicoTime
  // value based on the number of digits after timestamp decimal place.
  // 0 digits -> 0 precision (seconds)
  // 1-3 digits -> 3 precision (milliseconds)
  // 4-6 digits -> 6 precision (microseconds)
  // 7-9 digits -> 9 precision (nanoseconds)
  // 10-12 digits -> 12 precision (picoseconds)
  int Precision() const;

  // Comparison operators.
  bool operator==(const PicoTime& rh) const;
  bool operator!=(const PicoTime& rh) const;
  bool operator<(const PicoTime& rh) const;
  bool operator>(const PicoTime& rh) const;
  bool operator>=(const PicoTime& rh) const;
  bool operator<=(const PicoTime& rh) const;
#ifdef __cpp_impl_three_way_comparison
  std::strong_ordering operator<=>(const PicoTime& rh) const;
#endif

 private:
  constexpr PicoTime(int64_t unix_seconds, uint64_t picoseconds) {
    time_ = absl::FromUnixSeconds(unix_seconds) +
            absl::Nanoseconds(picoseconds / 1000);
    picos_ = picoseconds % 1000;
  }

  static constexpr uint64_t kNumPicosPerSecond = 1'000'000'000'000;

  // Nanosecond level precision is stored in time_;
  absl::Time time_;

  // Sub-nanosecond value is stored in picos_. The valid value range is [0,999]
  // inclusive.
  uint32_t picos_ = 0;
};

inline bool PicoTime::operator==(const PicoTime& rhs) const {
  return time_ == rhs.time_ && picos_ == rhs.picos_;
}

inline bool PicoTime::operator!=(const PicoTime& rhs) const {
  return !(*this == rhs);
}

inline bool PicoTime::operator<(const PicoTime& rhs) const {
  if (time_ < rhs.time_) {
    return true;
  }
  if (time_ > rhs.time_) {
    return false;
  }
  return picos_ < rhs.picos_;
}

inline bool PicoTime::operator>(const PicoTime& rhs) const {
  if (time_ > rhs.time_) {
    return true;
  }
  if (time_ < rhs.time_) {
    return false;
  }
  return picos_ > rhs.picos_;
}

inline bool PicoTime::operator>=(const PicoTime& rhs) const {
  return !(*this < rhs);
}

inline bool PicoTime::operator<=(const PicoTime& rhs) const {
  return !(*this > rhs);
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_PICO_TIME_H_
