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

#include "googlesql/public/time_zone_util.h"

#include "googlesql/common/errors.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace googlesql {

absl::Status FindTimeZoneByName(absl::string_view timezone_name,
                                absl::TimeZone* tz) {
  // This ultimately looks into the zoneinfo directory (typically
  // /usr/share/zoneinfo, /usr/share/lib/zoneinfo, etc.).
  if (absl::LoadTimeZone(timezone_name, tz)) {
    return absl::OkStatus();
  }
  constexpr absl::string_view kEuropeKyiv = "Europe/Kyiv";
  constexpr absl::string_view kEuropeKiev = "Europe/Kiev";
  if (timezone_name == kEuropeKyiv) {
    if (absl::LoadTimeZone(kEuropeKiev, tz)) {
      // This exception mitigates timezone version skew.
      // See (broken link)
      return absl::OkStatus();
    }
  } else if (timezone_name == kEuropeKiev) {
    if (absl::LoadTimeZone(kEuropeKyiv, tz)) {
      // This exception mitigates potential version skew if the "Kiev"
      // spelling is removed from a future timezone dataset.
      // See (broken link)
      return absl::OkStatus();
    }
  }
  return MakeEvalError() << "Invalid time zone: " << timezone_name;
}

}  // namespace googlesql
