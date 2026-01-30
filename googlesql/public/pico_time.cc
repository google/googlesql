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

#include "googlesql/public/pico_time.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace googlesql {

absl::StatusOr<PicoTime> PicoTime::Create(absl::Time time,
                                          uint64_t picoseconds) {
  int64_t seconds = absl::ToUnixSeconds(time);
  if (time - absl::FromUnixSeconds(seconds) != absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "Time argument must not contain subseconds value");
  }

  if (picoseconds >= kNumPicosPerSecond) {
    return absl::InvalidArgumentError(
        absl::StrCat("Picoseconds argument must be less than ",
                     kNumPicosPerSecond, ". Found ", picoseconds));
  }

  PicoTime picoTime(seconds, picoseconds);
  if (picoTime > PicoTime::MaxValue() || picoTime < PicoTime::MinValue()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "PicoTime is out of range between ", PicoTime::MinValue().DebugString(),
        " and ", PicoTime::MaxValue().DebugString()));
  }
  return picoTime;
}

absl::StatusOr<PicoTime> PicoTime::Create(absl::Time time) {
  int64_t seconds = absl::ToUnixSeconds(time);

  // Extract the subseconds value from `time`, which has nanosecond precision.
  uint64_t nanos =
      absl::ToInt64Nanoseconds(time - absl::FromUnixSeconds(seconds));
  uint64_t picos = nanos * 1000;

  return PicoTime::Create(absl::FromUnixSeconds(seconds), picos);
}

std::string PicoTime::ToString(absl::TimeZone timezone) const {
  // The string representation of the picos value, without trailing
  // zeros. E.g. when pico is 120, its value will be "12".
  std::string picos_str_without_trailing_zeros;

  // Generating picos_str_without_trailing_zeros, in reverse.
  uint32_t v = picos_;
  for (int i = 0; i < 3; ++i) {
    if (v % 10 != 0 || !picos_str_without_trailing_zeros.empty()) {
      absl::StrAppend(&picos_str_without_trailing_zeros, v % 10);
    }
    v /= 10;
  }
  // Reverse the strings to get the final result.
  std::reverse(picos_str_without_trailing_zeros.begin(),
               picos_str_without_trailing_zeros.end());

  std::string output =
      picos_ == 0
          ? absl::FormatTime("%E4Y-%m-%d %H:%M:%E*S%Ez", time_, timezone)
          : absl::StrCat(
                absl::FormatTime("%E4Y-%m-%d %H:%M:%E9S", time_, timezone),
                picos_str_without_trailing_zeros,
                absl::FormatTime("%Ez", time_, timezone));
  // Truncate timezone: if ":00" appears at the end, remove it.
  if (absl::EndsWith(output, ":00")) {
    output.erase(output.size() - 3);
  }
  return output;
}

absl::StatusOr<PicoTime> PicoTime::FromUnixPicos(absl::int128 unix_picos) {
  const absl::int128 kPicosMin = MinValue().ToUnixPicos();
  const absl::int128 kPicosMax = MaxValue().ToUnixPicos();
  if (unix_picos < kPicosMin || unix_picos > kPicosMax) {
    return absl::OutOfRangeError(
        absl::StrCat("Unix picoseconds value is out of allowed range between ",
                     kPicosMin, " to ", kPicosMax));
  }

  int64_t seconds = static_cast<int64_t>(unix_picos / kNumPicosPerSecond);
  int64_t picos = static_cast<int64_t>(unix_picos % kNumPicosPerSecond);

  // sub-second part should always be positive.
  if (picos < 0) {
    seconds--;
    picos += kNumPicosPerSecond;
  }

  return PicoTime(seconds, picos);
}

int PicoTime::Precision() const {
  if (picos_ != 0) {
    return 12;
  }
  int64_t nanos = absl::ToInt64Nanoseconds(
      time_ - absl::FromUnixSeconds(absl::ToUnixSeconds(time_)));
  if (nanos == 0) {
    return 0;
  } else if (nanos % 1000000 == 0) {
    return 3;
  } else if (nanos % 1000 == 0) {
    return 6;
  } else {
    return 9;
  }
}

}  // namespace googlesql
