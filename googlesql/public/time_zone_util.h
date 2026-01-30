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

#ifndef GOOGLESQL_PUBLIC_TIME_ZONE_UTIL_H_
#define GOOGLESQL_PUBLIC_TIME_ZONE_UTIL_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace googlesql {

// Loads the TimeZone given a timezone name ('Europe/Kyiv', etc.)
//
// Names are loaded from the system's zoneinfo directory (typically
// /usr/share/zoneinfo, /usr/share/lib/zoneinfo, etc.).  As per the base/time
// library, time zone names are case sensitive.
//
// GoogleSQL code should use this helper instead of accessing absl::LoadTimeZone
// directly. This helper contains some error handling to help mitigate version
// skew when new timezones are realeased. See (broken link)
absl::Status FindTimeZoneByName(absl::string_view timezone_name,
                                absl::TimeZone* tz);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TIME_ZONE_UTIL_H_
