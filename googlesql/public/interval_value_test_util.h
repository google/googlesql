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

#ifndef GOOGLESQL_PUBLIC_INTERVAL_VALUE_TEST_UTIL_H_
#define GOOGLESQL_PUBLIC_INTERVAL_VALUE_TEST_UTIL_H_

#include <cstdint>

#include "googlesql/public/interval_value.h"
#include "gmock/gmock.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"

namespace googlesql {

namespace interval_testing {

inline IntervalValue Months(int64_t months) {
  return IntervalValue::FromMonths(months).value();
}

inline IntervalValue Days(int64_t days) {
  return IntervalValue::FromDays(days).value();
}

inline IntervalValue Micros(int64_t micros) {
  return IntervalValue::FromMicros(micros).value();
}

inline IntervalValue Nanos(__int128 nanos) {
  return IntervalValue::FromNanos(nanos).value();
}

inline IntervalValue MonthsDaysMicros(int64_t months, int64_t days,
                                      int64_t micros) {
  return IntervalValue::FromMonthsDaysMicros(months, days, micros).value();
}

inline IntervalValue MonthsDaysNanos(int64_t months, int64_t days,
                                     __int128 nanos) {
  return IntervalValue::FromMonthsDaysNanos(months, days, nanos).value();
}

inline IntervalValue YMDHMS(int64_t years, int64_t months, int64_t days,
                            int64_t hours, int64_t minutes, int64_t seconds) {
  return IntervalValue::FromYMDHMS(years, months, days, hours, minutes, seconds)
      .value();
}

inline IntervalValue Years(int64_t years) {
  return YMDHMS(years, 0, 0, 0, 0, 0);
}

inline IntervalValue Hours(int64_t hours) {
  return YMDHMS(0, 0, 0, hours, 0, 0);
}

inline IntervalValue Minutes(int64_t minutes) {
  return YMDHMS(0, 0, 0, 0, minutes, 0);
}

inline IntervalValue Seconds(int64_t seconds) {
  return YMDHMS(0, 0, 0, 0, 0, seconds);
}

inline IntervalValue GenerateRandomInterval(absl::BitGen* gen) {
  int64_t months =
      absl::Uniform(*gen, IntervalValue::kMinMonths, IntervalValue::kMaxMonths);
  int64_t days =
      absl::Uniform(*gen, IntervalValue::kMinDays, IntervalValue::kMaxDays);
  int64_t micros =
      absl::Uniform(*gen, IntervalValue::kMinMicros, IntervalValue::kMaxMicros);
  int64_t nano_fractions = absl::Uniform(*gen, -999, 999);
  __int128 nanos = static_cast<__int128>(micros) * 1000 + nano_fractions;

  return MonthsDaysNanos(months, days, nanos);
}

// Invalid serialized INTERVAL values which will fail deserialization.
constexpr absl::string_view kSerializedIntervalWithNotEnoughBytes(
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 15);
static_assert(kSerializedIntervalWithNotEnoughBytes.size() == 15);

constexpr absl::string_view kSerializedIntervalWithTooManyBytes(
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 17);
static_assert(kSerializedIntervalWithTooManyBytes.size() == 17);

constexpr absl::string_view kSerializedIntervalWithInvalidFields(
    "\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01", 16);
static_assert(kSerializedIntervalWithInvalidFields.size() == 16);

// The following gmock matcher can be used to verify that two INTERVAL values
// are identical. This is different from the behavior of the default equality
// operator, which treats some different INTERVAL values as equal (e.g.
// INTERVAL 1 MONTH == INTERVAL 30 DAY). The matcher treats INTERVAL values as
// identical only when all their parts are equal.
MATCHER_P(IdenticalInterval, value,
          absl::StrFormat("is identical to INTERVAL '%s'", value.ToString())) {
  return IdenticalIntervals(arg, value);
}

}  // namespace interval_testing

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_INTERVAL_VALUE_TEST_UTIL_H_
