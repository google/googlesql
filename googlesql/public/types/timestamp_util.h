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

#ifndef GOOGLESQL_PUBLIC_TYPES_TIMESTAMP_UTIL_H_
#define GOOGLESQL_PUBLIC_TYPES_TIMESTAMP_UTIL_H_

#include <cstdint>

#include "absl/time/time.h"

namespace googlesql::types {

// This file contains a set of timestamp/date related constants.

// The valid date range is [ 0001-01-01, 9999-12-31 ].
inline constexpr int64_t kDateMin = -719162;
inline constexpr int64_t kDateMax = 2932896;

// The valid timestamp range for timestamps is:
//   [ 0001-01-01 00:00:00 UTC, 9999-12-31 23:59:59.999999 UTC ]
inline constexpr int64_t kTimestampMin = -62135596800LL * 1000000;
inline constexpr int64_t kTimestampMax = 253402300800LL * 1000000 - 1;

// The valid timestamp range for absl::Time is:
// [ 0001-01-01 00:00:00 UTC, 9999-12-31 23:59:59.999999999 UTC ]
constexpr absl::Time TimestampMinBaseTime() {
  return absl::Time(absl::FromUnixMicros(types::kTimestampMin));
}

absl::Time TimestampMaxBaseTime();

// The range of values of the legacy timestamp types is from the beginning of
// 1678-01-01 to the end of 2261-12-31 UTC.  These are the years fully
// representable in nanos in an int64.  The bounds were computed with an online
// date converter, and verified with C library date formatting with TZ=UTC in
// unittest.
// TODO: Deprecated TIMESTAMP_XXX types, to be removed.
inline constexpr int64_t kTimestampNanosMin = -9214560000LL * 1000000000;
inline constexpr int64_t kTimestampNanosMax = 9214646400LL * 1000000000 - 1;
inline constexpr int64_t kTimestampMicrosMin = kTimestampNanosMin / 1000;
inline constexpr int64_t kTimestampMillisMin = kTimestampNanosMin / 1000000;
inline constexpr int64_t kTimestampSecondsMin = kTimestampNanosMin / 1000000000;
inline constexpr int64_t kTimestampMicrosMax = kTimestampNanosMax / 1000;
inline constexpr int64_t kTimestampMillisMax = kTimestampNanosMax / 1000000;
inline constexpr int64_t kTimestampSecondsMax = kTimestampNanosMax / 1000000000;

}  // namespace googlesql::types

#endif  // GOOGLESQL_PUBLIC_TYPES_TIMESTAMP_UTIL_H_
