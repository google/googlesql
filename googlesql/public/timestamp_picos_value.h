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

#ifndef GOOGLESQL_PUBLIC_TIMESTAMP_PICOS_VALUE_H_
#define GOOGLESQL_PUBLIC_TIMESTAMP_PICOS_VALUE_H_

#include <compare>
#include <cstddef>
#include <ostream>
#include <string>

#include "googlesql/public/pico_time.h"
#include "absl/base/port.h"
#include "absl/numeric/int128.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// TODO: Code for this class is currently still under
// development; do NOT take a dependency on it.
//
// This class represents values of the GoogleSQL Timestamp_pico type.
//
// Internally the values are stored using two 64-bit integers.
class TimestampPicosValue final {
 public:
  // Representing "1970-01-01 00:00:00.000000000000 UTC".
  TimestampPicosValue() = default;

  TimestampPicosValue(const TimestampPicosValue&) = default;
  TimestampPicosValue(TimestampPicosValue&&) = default;
  TimestampPicosValue& operator=(const TimestampPicosValue&) = default;
  TimestampPicosValue& operator=(TimestampPicosValue&&) = default;

  explicit constexpr TimestampPicosValue(const PicoTime& time) : time_(time) {}

  static absl::StatusOr<TimestampPicosValue> Create(absl::Time time);

  // Returns timestamp at nanosecond precision.
  absl::Time ToAbslTime() const { return time_.ToAbslTime(); }

  const PicoTime& ToPicoTime() const { return time_; }

  // Returns the maximum and minimum UUID values.
  // Minimum value is 0001-01-01 00:00:00.000000000000 UTC
  // Maximum value is 9999-12-31 23:59:59.999999999999 UTC
  static constexpr TimestampPicosValue MinValue() {
    return TimestampPicosValue(PicoTime::MinValue());
  }
  static constexpr TimestampPicosValue MaxValue() {
    return TimestampPicosValue(PicoTime::MaxValue());
  }

  // Serializes into a fixed-size 16-byte representation and appends
  // it directly to the provided 'bytes' string.
  void SerializeAndAppendToProtoBytes(std::string* bytes) const;

  // Serializes into a fixed-size 16-byte representation and returns
  // it as a string.
  std::string SerializeAsProtoBytes() const {
    std::string result;
    SerializeAndAppendToProtoBytes(&result);
    return result;
  }

  // Deserializes from fixed-size 16-byte representation. Returns an
  // error if the given input doesn't contains exactly 16 bytes of data.
  static absl::StatusOr<TimestampPicosValue> DeserializeFromProtoBytes(
      absl::string_view bytes);

  size_t HashCode() const;

  std::string DebugString() const { return ToString(); }

  std::string ToString(absl::TimeZone timezone = absl::UTCTimeZone()) const {
    return time_.ToString(timezone);
  }

  // If the time zone is not included in the string, then <default_timezone> is
  // applied for the conversion.  Uses the canonical timestamp string format.
  // If the time zone is included in the string and allow_tz_in_str is set to
  // false, an error will be returned.
  static absl::StatusOr<TimestampPicosValue> FromString(
      absl::string_view str, absl::TimeZone default_timezone,
      bool allow_tz_in_str);

  // Returns timestamp value as Unix epoch picoseconds.
  absl::int128 ToUnixPicos() const { return time_.ToUnixPicos(); }

  // Creates a TimestampPicosValue from Unix epoch picoseconds.
  static absl::StatusOr<TimestampPicosValue> FromUnixPicos(
      absl::int128 unix_picos) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto time, PicoTime::FromUnixPicos(unix_picos));
    return TimestampPicosValue(time);
  }

  // Helper functions that encode and decode int128 values.
  static absl::uint128 Encode(absl::int128 value) {
    return googlesql_base::LittleEndian::FromHost128(value);
  }
  static absl::int128 Decode(const char* input) {
    return static_cast<absl::int128>(googlesql_base::LittleEndian::Load128(input));
  }

  // Comparison operators.
  bool operator==(const TimestampPicosValue& rh) const;
  bool operator!=(const TimestampPicosValue& rh) const;
  bool operator<(const TimestampPicosValue& rh) const;
  bool operator>(const TimestampPicosValue& rh) const;
  bool operator>=(const TimestampPicosValue& rh) const;
  bool operator<=(const TimestampPicosValue& rh) const;
#ifdef __cpp_impl_three_way_comparison
  std::strong_ordering operator<=>(const TimestampPicosValue& rh) const;
#endif

 private:
  PicoTime time_;
};

// Allow TimestampPicosValue values to be logged.
std::ostream& operator<<(std::ostream& out, const TimestampPicosValue& value);

inline bool TimestampPicosValue::operator==(
    const TimestampPicosValue& rh) const {
  return time_ == rh.time_;
}

inline bool TimestampPicosValue::operator!=(
    const TimestampPicosValue& rh) const {
  return !(*this == rh);
}

inline bool TimestampPicosValue::operator<(
    const TimestampPicosValue& rh) const {
  return time_ < rh.time_;
}

inline bool TimestampPicosValue::operator>(
    const TimestampPicosValue& rh) const {
  return time_ > rh.time_;
}

inline bool TimestampPicosValue::operator>=(
    const TimestampPicosValue& rh) const {
  return !(*this < rh);
}

inline bool TimestampPicosValue::operator<=(
    const TimestampPicosValue& rh) const {
  return !(*this > rh);
}
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TIMESTAMP_PICOS_VALUE_H_
