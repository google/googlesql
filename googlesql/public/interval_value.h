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

#ifndef GOOGLESQL_PUBLIC_INTERVAL_VALUE_H_
#define GOOGLESQL_PUBLIC_INTERVAL_VALUE_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class IntervalValue final {
  // The INTERVAL value is composed of 3 fields:
  // 1. Number of months
  // 2. Number of days
  // 3. Number of nanoseconds
  //
  // Each field should be able to cover 10,000 years with sign. The required
  // number of bits for each field is:
  // Months       - 18 bits
  // Days         - 23 bits
  // Microseconds - 59 bits
  // Nanoseconds  - 69 bits
  // Nanoseconds fraction of microseconds - 10 bits.

  // Two of the most used fields - micros and days get int64 and int32
  // parts of interval.
  //
  // 0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6
  // |    micros                     |  days         | months and    |
  //                                                   nano fractions
  //
  // Months and nano fractions of microsecond take the rest of 32 bits.
  //
  // 01234567890123456789012345678901
  // |     months       | nano      |
  // |                  | fractions |
  //
  // Months are always stored as positive numbers, and the highest bit is used
  // to store the sign information: 0 for positive, 1 for negative.
  // Nano fractions are always positive, when needed micros value is adjusted.
  // This allows single canonical representation of nanos, i.e. for nanos=-1,
  // it will be stored as micros=-1 and nano_fractions=999.
  static const uint32_t kMonthSignMask = 0x80000000;
  static const uint32_t kMonthsMask = 0x7FFFE000;
  static const uint32_t kMonthsShift = 13;
  static const uint32_t kNanosMask = 0x000003FF;
  static const uint32_t kNanosShift = 0;

 public:
  static const int64_t kMonthsInYear = 12;
  static const int64_t kMonthsInQuarter = 3;
  static const int64_t kHoursInDay = 24;
  static const int64_t kMinutesInHour = 60;
  static const int64_t kSecondsInMinute = 60;
  static const int64_t kMicrosInMilli = 1000;
  static const int64_t kMillisInSecond = 1000;
  static const int64_t kMicrosInSecond = kMillisInSecond * kMicrosInMilli;
  static const int64_t kMicrosInMinute = kSecondsInMinute * kMicrosInSecond;
  static const int64_t kMicrosInHour = kMinutesInHour * kMicrosInMinute;
  static const int64_t kMicrosInDay = kHoursInDay * kMicrosInHour;
  static const int64_t kDaysInMonth = 30;
  static const int64_t kDaysInWeek = 7;
  static const int64_t kMicrosInMonth = kDaysInMonth * kMicrosInDay;
  static const int64_t kNanosInMicro = 1000;
  static const __int128 kNanosInMicro128 = static_cast<__int128>(kNanosInMicro);
  static const __int128 kNanosInMilli = kMicrosInMilli * kNanosInMicro128;
  static const __int128 kNanosInSecond = kMicrosInSecond * kNanosInMicro128;
  static const __int128 kNanosInMinute = kMicrosInMinute * kNanosInMicro128;
  static const __int128 kNanosInHour = kMicrosInHour * kNanosInMicro128;
  static const __int128 kNanosInDay = kNanosInMicro128 * kMicrosInDay;
  static const __int128 kNanosInMonth = kNanosInMicro128 * kMicrosInMonth;
  static const __int128 kNanosInYear = kNanosInMonth * kMonthsInYear;

  static const int64_t kMaxYears = 10000;
  static const int64_t kMaxMonths = 12 * kMaxYears;
  static const int64_t kMaxDays = 366 * kMaxYears;
  static const int64_t kMaxHours = kMaxDays * kHoursInDay;
  static const int64_t kMaxMinutes = kMaxHours * kMinutesInHour;
  static const int64_t kMaxSeconds = kMaxMinutes * kSecondsInMinute;
  static const int64_t kMaxMicros = kMicrosInDay * kMaxDays;
  static const __int128 kMaxNanos = kNanosInMicro128 * kMaxMicros;

  static const int64_t kMinMonths = -kMaxMonths;
  static const int64_t kMinDays = -kMaxDays;
  static const int64_t kMinMicros = -kMaxMicros;
  static const __int128 kMinNanos = -kMaxNanos;

  // Builds interval value from [Y]ears, [M]onths, [D]ays, [H]ours, [M]inutes
  // and [S]econds.
  static absl::StatusOr<IntervalValue> FromYMDHMS(int64_t years, int64_t months,
                                                  int64_t days, int64_t hours,
                                                  int64_t minutes,
                                                  int64_t seconds);

  static absl::StatusOr<IntervalValue> FromMonthsDaysMicros(int64_t months,
                                                            int64_t days,
                                                            int64_t micros) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMonths(months));
    GOOGLESQL_RETURN_IF_ERROR(ValidateDays(days));
    GOOGLESQL_RETURN_IF_ERROR(ValidateMicros(micros));
    return IntervalValue(months, days, micros);
  }

  static absl::StatusOr<IntervalValue> FromMonthsDaysNanos(int64_t months,
                                                           int64_t days,
                                                           __int128 nanos) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMonths(months));
    GOOGLESQL_RETURN_IF_ERROR(ValidateDays(days));
    GOOGLESQL_RETURN_IF_ERROR(ValidateNanos(nanos));
    return IntervalValue(months, days, nanos);
  }

  static absl::StatusOr<IntervalValue> FromMonths(int64_t months) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMonths(months));
    return IntervalValue(months, 0);
  }

  static absl::StatusOr<IntervalValue> FromDays(int64_t days) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateDays(days));
    return IntervalValue(0, days);
  }

  static absl::StatusOr<IntervalValue> FromMicros(int64_t micros) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMicros(micros));
    return IntervalValue(0, 0, micros);
  }

  static absl::StatusOr<IntervalValue> FromNanos(__int128 nanos) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateNanos(nanos));
    return IntervalValue(0, 0, nanos);
  }

  static IntervalValue MaxValue() {
    return IntervalValue(kMaxMonths, kMaxDays, kMaxNanos);
  }

  static IntervalValue MinValue() {
    return IntervalValue(kMinMonths, kMinDays, kMinNanos);
  }

  // Default constructor, constructs a zero value.
  constexpr IntervalValue() = default;

  // Convert interval value to micros. Note, that the resulting number of
  // micros can be bigger (up to 3 times) than the maximum number of micros
  // allowed in interval.
  int64_t GetAsMicros() const {
    return get_months() * kMicrosInMonth + get_days() * kMicrosInDay +
           get_micros();
  }

  // Convert interval value to nanos. Note, that the resulting number of
  // nanos can be bigger (up to 3 times) than the maximum number of nanos
  // allowed in interval.
  __int128 GetAsNanos() const {
    return get_months() * kNanosInMonth + get_days() * kNanosInDay +
           get_nanos();
  }

  // Get the months part of interval
  int64_t get_months() const {
    int64_t months = ((months_nanos_ & kMonthsMask) >> kMonthsShift);
    return (months_nanos_ & kMonthSignMask) ? -months : months;
  }

  // Get the days part of interval
  int64_t get_days() const { return days_; }

  // Get the micros part of interval
  int64_t get_micros() const { return micros_; }

  // Get the nanos part of interval
  __int128 get_nanos() const {
    return kNanosInMicro128 * micros_ + get_nano_fractions();
  }

  // Get only the nano fractions part [0 to 999]
  int64_t get_nano_fractions() const {
    return (months_nanos_ & kNanosMask) >> kNanosShift;
  }

  // Comparison operators.
  bool operator==(const IntervalValue& v) const {
    return get_nano_fractions() == v.get_nano_fractions() &&
           GetAsMicros() == v.GetAsMicros();
  }
  bool operator!=(const IntervalValue& v) const {
    return GetAsMicros() != v.GetAsMicros() ||
           get_nano_fractions() != v.get_nano_fractions();
  }
  bool operator<(const IntervalValue& v) const {
    int64_t micros = GetAsMicros();
    int64_t v_micros = v.GetAsMicros();
    return micros < v_micros || (micros == v_micros &&
                                 get_nano_fractions() < v.get_nano_fractions());
  }
  bool operator>(const IntervalValue& v) const {
    int64_t micros = GetAsMicros();
    int64_t v_micros = v.GetAsMicros();
    return micros > v_micros || (micros == v_micros &&
                                 get_nano_fractions() > v.get_nano_fractions());
  }
  bool operator<=(const IntervalValue& v) const {
    int64_t micros = GetAsMicros();
    int64_t v_micros = v.GetAsMicros();
    return micros < v_micros ||
           (micros == v_micros &&
            get_nano_fractions() <= v.get_nano_fractions());
  }
  bool operator>=(const IntervalValue& v) const {
    int64_t micros = GetAsMicros();
    int64_t v_micros = v.GetAsMicros();
    return micros > v_micros ||
           (micros == v_micros &&
            get_nano_fractions() >= v.get_nano_fractions());
  }

  // Unary minus operator
  IntervalValue operator-() const {
    int64_t months = get_months();
    int64_t days = get_days();
    __int128 nanos = get_nanos();
    return IntervalValue(-months, -days, -nanos);
  }

  // Binary plus operator
  absl::StatusOr<IntervalValue> operator+(const IntervalValue& v) const {
    return IntervalValue::FromMonthsDaysNanos(get_months() + v.get_months(),
                                              get_days() + v.get_days(),
                                              get_nanos() + v.get_nanos());
  }

  // Binary minus operator
  absl::StatusOr<IntervalValue> operator-(const IntervalValue& v) const {
    return IntervalValue::FromMonthsDaysNanos(get_months() - v.get_months(),
                                              get_days() - v.get_days(),
                                              get_nanos() - v.get_nanos());
  }

  // Multiply by integer operator
  absl::StatusOr<IntervalValue> operator*(int64_t v) const;

  // Divide by integer operator
  ABSL_ATTRIBUTE_ALWAYS_INLINE
  absl::StatusOr<IntervalValue> operator/(int64_t v) const {
    return Divide(v, /*round_to_micros=*/false);
  }

  // Multiply by the given integer.
  ABSL_ATTRIBUTE_ALWAYS_INLINE
  absl::StatusOr<IntervalValue> Multiply(int64_t v) const {
    return (*this) * v;
  }

  // Divide by the given integer.
  // When <round_to_micros> is true, do a rounding towards zero on trailing
  // nanos precision digits. When the second argument is not set, its default
  // value is false.
  absl::StatusOr<IntervalValue> Divide(int64_t v, bool round_to_micros) const;

  // The same as above, but uses round_to_micros=false by default.
  ABSL_DEPRECATED("Inline me!")
  absl::StatusOr<IntervalValue> Divide(int64_t v) const {
    return Divide(v, /*round_to_micros=*/false);
  }

  // Aggregates multiple INTERVAL values and produces sum and average of all
  // values. This class handles a temporary overflow while adding values.
  // OUT_OF_RANGE error is generated only if the result is outside of the valid
  // INTERVAL range.
  class SumAggregator final {
   public:
    // Adds an INTERVAL value to the sum.
    void Add(IntervalValue value);
    // Subtracts an INTERVAL value from the sum.
    void Subtract(IntervalValue value) { Add(-value); }

    // Returns sum of all input values. Returns OUT_OF_RANGE error on overflow.
    absl::StatusOr<IntervalValue> GetSum() const;

    // Returns sum of all input values divided by the specified divisor.
    // Returns OUT_OF_RANGE error on overflow of the division result.
    // Note, that with the proper invocation of AVG function, overflow is not
    // possible.
    // Caller must ensure that <count> is positive non-zero.
    // When <round_to_micros> is true, do a rounding towards zero on trailing
    // nanos precision digits. When the second argument is not set, its default
    // value is false.
    absl::StatusOr<IntervalValue> GetAverage(
        int64_t count, bool round_to_micros = false) const;

    // Merges the state with other SumAggregator instance's state.
    void MergeWith(const SumAggregator& other);

    // Serialization and deserialization methods for INTERVAL values that are
    // intended to be used to store them in protos. The encoding is variable in
    // length with max size of 56 bytes. SerializeAndAppendToProtoBytes is
    // typically more efficient due to fewer memory allocations.
    std::string SerializeAsProtoBytes() const;
    void SerializeAndAppendToProtoBytes(std::string* bytes) const;
    static absl::StatusOr<SumAggregator> DeserializeFromProtoBytes(
        absl::string_view bytes);

    std::string DebugString() const;

   private:
    __int128 months_ = 0;
    __int128 days_ = 0;
    FixedInt<64, 3> nanos_;
  };
  // Returns hash code for the value.
  size_t HashCode() const;

  template <typename H>
  friend H AbslHashValue(H h, const IntervalValue& v);

  absl::StatusOr<int64_t> Extract(functions::DateTimestampPart part) const;

  // Serialization and deserialization methods for interval values.
  void SerializeAndAppendToBytes(std::string* bytes) const;
  std::string SerializeAsBytes() const {
    std::string bytes;
    SerializeAndAppendToBytes(&bytes);
    return bytes;
  }
  static absl::StatusOr<IntervalValue> DeserializeFromBytes(
      absl::string_view bytes);

  // Serialize and deserialize this interval value as an int128.
  absl::int128 SerializeAsInt128() const;
  static absl::StatusOr<IntervalValue> DeserializeFromInt128(
      absl::int128 value);

  // Builds fully expanded string representation of interval.
  std::string ToString() const {
    std::string result;
    AppendToString(&result);
    return result;
  }

  // Appends fully expanded string representation of interval to the output
  // string. When the output string is reused across calls, AppendToString is
  // typically more efficient than ToString due to fewer memory allocations.
  void AppendToString(std::string* output) const;

  // Builds ISO 8601 Duration compliant string representation of interval.
  std::string ToISO8601() const;

  // Parses interval from string, automatically detects datetime fields.
  // If allow_nanos=false, an error is return when nanosecond part is present.
  static absl::StatusOr<IntervalValue> ParseFromString(absl::string_view input,
                                                       bool allow_nanos);
  // Same as above, but with allow_nanos=true.
  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<IntervalValue> ParseFromString(
      absl::string_view input) {
    return ParseFromString(input, /*allow_nanos=*/true);
  }

  // Parses interval from string for single datetime field.
  // If allow_nanos=false, an error is return when nanosecond part is present.
  static absl::StatusOr<IntervalValue> ParseFromString(
      absl::string_view input, functions::DateTimestampPart part,
      bool allow_nanos);
  // Same as above, but with allow_nanos=true.
  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<IntervalValue> ParseFromString(
      absl::string_view input, functions::DateTimestampPart part) {
    return ParseFromString(input, part, /*allow_nanos=*/true);
  }

  // Parses interval from string for two datetime fields.
  // If allow_nanos=false, an error is return when nanosecond part is present.
  static absl::StatusOr<IntervalValue> ParseFromString(
      absl::string_view input, functions::DateTimestampPart from,
      functions::DateTimestampPart to, bool allow_nanos);
  // Same as above, but with allow_nanos=true.
  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<IntervalValue> ParseFromString(
      absl::string_view input, functions::DateTimestampPart from,
      functions::DateTimestampPart to) {
    return ParseFromString(input, from, to, /*allow_nanos=*/true);
  }

  // Parses interval from ISO 8601 Duration.
  // If allow_nanos=false, an error is return when nanosecond part is present.
  static absl::StatusOr<IntervalValue> ParseFromISO8601(absl::string_view input,
                                                        bool allow_nanos);
  // Same as above, but with allow_nanos=true.
  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<IntervalValue> ParseFromISO8601(
      absl::string_view input) {
    return ParseFromISO8601(input, /*allow_nanos=*/true);
  }

  // Parses either canonical interval string representation (ParseFromString) or
  // ISO 8601 Duration representation - detects automatically the format.
  // If allow_nanos=false, an error is return when nanosecond part is present.
  static absl::StatusOr<IntervalValue> Parse(absl::string_view input,
                                             bool allow_nanos);
  // Same as above, but with allow_nanos=true.
  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<IntervalValue> Parse(absl::string_view input) {
    return Parse(input, /*allow_nanos=*/true);
  }

  // Interval constructor from integer for given datetime part field.
  // If allow_nanos=false, an error is return when NANOSECOND part is provided.
  static absl::StatusOr<IntervalValue> FromInteger(
      int64_t value, functions::DateTimestampPart part, bool allow_nanos);

 private:
  IntervalValue(int64_t months, int64_t days, int64_t micros = 0) {
    micros_ = micros;
    days_ = static_cast<int32_t>(days);
    if (months >= 0) {
      months_nanos_ = static_cast<uint32_t>(months) << kMonthsShift;
    } else {
      months_nanos_ =
          (static_cast<uint32_t>(-months) << kMonthsShift) | kMonthSignMask;
    }
  }

  IntervalValue(int64_t months, int64_t days, __int128 nanos) {
    micros_ = nanos / kNanosInMicro;
    days_ = static_cast<int32_t>(days);
    if (months >= 0) {
      months_nanos_ = static_cast<uint32_t>(months) << kMonthsShift;
    } else {
      months_nanos_ =
          (static_cast<uint32_t>(-months) << kMonthsShift) | kMonthSignMask;
    }
    int64_t nano_fractions = nanos % kNanosInMicro;
    if (nano_fractions < 0) {
      // Make sure nano_fractions are always positive by adjusting micros.
      nano_fractions = kNanosInMicro + nano_fractions;
      micros_--;
    }
    months_nanos_ |= static_cast<uint32_t>(nano_fractions) << kNanosShift;
  }

  template <typename T>
  static absl::Status ValidateField(T value, T min, T max,
                                    absl::string_view field_name) {
    if (ABSL_PREDICT_TRUE(value <= max && value >= min)) {
      return absl::OkStatus();
    }
    return ::googlesql_base::OutOfRangeErrorBuilder()
           << "Interval field " << field_name << " '" << absl::int128(value)
           << "' is out of range " << absl::int128(min) << " to "
           << absl::int128(max);
  }

  static absl::Status ValidateMonths(int64_t months) {
    return ValidateField(months, kMinMonths, kMaxMonths, "months");
  }
  static absl::Status ValidateDays(int64_t days) {
    return ValidateField(days, kMinDays, kMaxDays, "days");
  }
  static absl::Status ValidateMicros(int64_t micros) {
    return ValidateField(micros, kMinMicros, kMaxMicros, "microseconds");
  }
  static absl::Status ValidateNanos(__int128 nanos) {
    return ValidateField(nanos, kMinNanos, kMaxNanos, "nanoseconds");
  }

  int64_t micros_ = 0;
  int32_t days_ = 0;
  uint32_t months_nanos_ = 0;
};

static_assert(sizeof(IntervalValue) == 16, "IntervalValue must be 16 bytes");

template <typename H>
inline H AbslHashValue(H h, const IntervalValue& v) {
  return H::combine(std::move(h), v.GetAsMicros(), v.get_nano_fractions());
}

// Allow INTERVAL values to be logged.
std::ostream& operator<<(std::ostream& out, IntervalValue value);

// Normalizes 24 hour time periods into full days. Adjusts nanos and days to
// have the same sign.
absl::StatusOr<IntervalValue> JustifyHours(const IntervalValue& v);

// Normalizes 30 day time periods into full months. Adjusts days and months to
// have the same sign.
absl::StatusOr<IntervalValue> JustifyDays(const IntervalValue& v);

// Normalizes 24 hour time periods into full days, and after than 30 day time
// periods into full months. Adjusts all date parts to have the same sign.
absl::StatusOr<IntervalValue> JustifyInterval(const IntervalValue& v);

// Normalization function that encodes the entire interval value into the nanos
// part. Returns an error if the absolute interval value exceeds 10k years.
absl::StatusOr<IntervalValue> ToSecondsInterval(const IntervalValue& v);

// Checks if the two INTERVAL values are identical.
// This is different from the behavior of the equality operator, which treats
// some different INTERVAL values as equal (e.g.
// INTERVAL 1 MONTH == INTERVAL 30 DAY). This functions treats INTERVAL values
// as identical only when all their parts are equal.
bool IdenticalIntervals(const IntervalValue& v1, const IntervalValue& v2);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_INTERVAL_VALUE_H_
