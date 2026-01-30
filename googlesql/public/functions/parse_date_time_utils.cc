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

#include "googlesql/public/functions/parse_date_time_utils.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#include <limits>

#include "googlesql/public/functions/date_time_util.h"
#include "absl/base/optimization.h"
#include "absl/time/time.h"

namespace googlesql {
namespace functions {
namespace parse_date_time_utils {

namespace {

const char kDigits[] = "0123456789";

const int64_t powers_of_ten[] = {
    1,           10,           100,          1000,      10000,
    100000,      1000000,      10000000,     100000000, 1000000000,
    10000000000, 100000000000, 1000000000000};
}  // namespace

bool ConvertTimeToTimestamp(absl::Time time, int64_t* timestamp) {
  *timestamp = absl::ToUnixMicros(time);
  return IsValidTimestamp(*timestamp, kMicroseconds);
}

const char* ParseInt(const char* dp, const char* end_of_data, int max_width,
                     int64_t min, int64_t max, int64_t* vp) {
  if (dp == nullptr || dp >= end_of_data || max_width <= 0) {
    return nullptr;
  }

  const int64_t kmin = std::numeric_limits<int64_t>::min();
  bool neg = false;
  int64_t value = 0;
  if (*dp == '-') {
    neg = true;
    if (max_width <= 0 || --max_width != 0) {
      ++dp;
    } else {
      return nullptr;  // <max_width> was 1.
    }
  }
  if (const char* const bp = dp) {
    const char* cp;
    while (dp < end_of_data && (cp = strchr(kDigits, *dp))) {
      int d = static_cast<int>(cp - kDigits);
      if (d < 0 || d >= 10) break;  // Not a digit.
      if (ABSL_PREDICT_FALSE(value < kmin / 10)) {
        return nullptr;
      }
      value *= 10;
      if (ABSL_PREDICT_FALSE(value < kmin + d)) {
        return nullptr;
      }
      value -= d;
      dp += 1;
      if (max_width > 0 && --max_width == 0) break;
    }
    if (dp != bp && (neg || value != kmin)) {
      if (!neg || value != 0) {
        if (!neg) value = -value;  // Make positive.
        if (min <= value && value <= max) {
          *vp = value;
        } else {
          return nullptr;
        }
      } else {
        return nullptr;
      }
    } else {
      return nullptr;
    }
  }
  return dp;
}

const char* ParseInt(const char* dp, const char* end_of_data, int max_width,
                     int64_t min, int64_t max, int* vp) {
  int64_t int64_res;
  const char* res_dp =
      ParseInt(dp, end_of_data, max_width, min, max, &int64_res);

  if (res_dp == nullptr || int64_res < std::numeric_limits<int>::min() ||
      int64_res > std::numeric_limits<int>::max()) {
    return nullptr;
  }
  *vp = static_cast<int>(int64_res);
  return res_dp;
}

const char* ParseSubSeconds(const char* dp, const char* end_of_data,
                            int max_digits, TimestampScale scale,
                            absl::Duration* subseconds,
                            uint32_t* sub_nanoseconds) {
  if (dp != nullptr) {
    if (dp < end_of_data || scale != kSeconds) {
      int64_t parsed_value = 0;
      int64_t num_digits_parsed = 0;
      const char* const bp = dp;
      const char* cp;

      while (dp < end_of_data && (cp = strchr(kDigits, *dp)) &&
             (max_digits == 0 || num_digits_parsed < max_digits)) {
        int d = static_cast<int>(cp - kDigits);
        if (d < 0 || d >= 10) break;  // Not a digit.
        ++dp;
        ++num_digits_parsed;
        if (num_digits_parsed > scale) {
          // Consume but ignore digits beyond the given precision.
          continue;
        }
        parsed_value *= 10;
        parsed_value += d;
      }

      if (dp != bp) {
        if (num_digits_parsed < scale) {
          // We consumed less than precision digits, so widen parsed_value to
          // given precision.
          parsed_value *= powers_of_ten[scale - num_digits_parsed];
        }
        *sub_nanoseconds = 0;
        if (scale == kMicroseconds) {
          *subseconds = absl::Microseconds(parsed_value);
        } else if (scale == kMilliseconds) {
          *subseconds = absl::Milliseconds(parsed_value);
        } else if (scale == kNanoseconds) {
          // NANO precision.
          *subseconds = absl::Nanoseconds(parsed_value);
        } else {
          // PICO precision.
          *subseconds = absl::Nanoseconds(parsed_value / 1000);
          *sub_nanoseconds = parsed_value % 1000;
        }
      } else {
        dp = nullptr;
      }
    } else {
      dp = nullptr;
    }
  }
  return dp;
}

}  // namespace parse_date_time_utils
}  // namespace functions
}  // namespace googlesql
