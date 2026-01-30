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

#include "googlesql/public/timestamp_picos_value.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/pico_time.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<TimestampPicosValue> TimestampPicosValue::Create(
    absl::Time time) {
  if (time < MinValue().ToAbslTime() || time > MaxValue().ToAbslTime()) {
    return absl::OutOfRangeError(
        absl::StrCat("timestamp is out of allowed range: ", time));
  }

  int64_t seconds = absl::ToUnixSeconds(time);
  int64_t nanos =
      absl::ToInt64Nanoseconds(time - absl::FromUnixSeconds(seconds));
  uint64_t picos = nanos * 1000;

  GOOGLESQL_ASSIGN_OR_RETURN(PicoTime pico_time,
                   PicoTime::Create(absl::FromUnixSeconds(seconds), picos));
  return TimestampPicosValue(pico_time);
}

void TimestampPicosValue::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  absl::uint128 unix_picos = Encode(ToUnixPicos());
  bytes->append(reinterpret_cast<const char*>(&unix_picos), sizeof(unix_picos));
}

absl::StatusOr<TimestampPicosValue>
TimestampPicosValue::DeserializeFromProtoBytes(absl::string_view bytes) {
  if (bytes.size() != sizeof(TimestampPicosValue)) {
    return absl::OutOfRangeError(
        absl::StrCat("Invalid serialized TIMESTAMP_PICO size, expected ",
                     sizeof(TimestampPicosValue), " bytes, but got ",
                     bytes.size(), " bytes."));
  }
  return FromUnixPicos(Decode(bytes.data()));
}

size_t TimestampPicosValue::HashCode() const {
  return absl::Hash<absl::int128>()(ToUnixPicos());
}

absl::StatusOr<TimestampPicosValue> TimestampPicosValue::FromString(
    absl::string_view str, absl::TimeZone default_timezone,
    bool allow_tz_in_str) {
  PicoTime timestamp;
  GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToTimestamp(
      str, default_timezone,
      /*allow_tz_in_str=*/allow_tz_in_str, &timestamp));
  return TimestampPicosValue(timestamp);
}

std::ostream& operator<<(std::ostream& out, const TimestampPicosValue& value) {
  return out << value.ToString();
}

}  // namespace googlesql
