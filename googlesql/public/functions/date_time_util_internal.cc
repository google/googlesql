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

#include "googlesql/public/functions/date_time_util_internal.h"

#include "googlesql/base/logging.h"
#include "absl/time/civil_time.h"

namespace googlesql {
namespace functions {
namespace date_time_util_internal {

absl::civil_year_t GetIsoYear(absl::CivilDay day) {
  // ISO year numbers diverge from Gregorian calendar year numbers at the
  // beginnings and ends of some years. Specifically, if this date is at
  // least 12/29 and the week that the date falls in has four or more days
  // in the following year, then the ISO year is that of the following
  // year. A similar principal applies for dates between 1/1 and 1/3.
  absl::CivilDay monday_of_week =
      PrevWeekdayOrToday(day, absl::Weekday::monday);
  absl::CivilDay day_defining_year_of_week =
      absl::NextWeekday(monday_of_week, absl::Weekday::thursday);
  return day_defining_year_of_week.year();
}

absl::CivilDay GetFirstDayOfIsoYear(absl::CivilDay day) {
  absl::civil_year_t iso_year = GetIsoYear(day);

  // Find the first Thursday.
  absl::CivilDay first_day(iso_year, 1, 1);
  absl::CivilDay first_thursay =
      NextWeekdayOrToday(first_day, absl::Weekday::thursday);
  // The first day of the iso year is the prior monday.
  return absl::PrevWeekday(first_thursay, absl::Weekday::monday);
}

absl::CivilDay GetLastDayOfIsoYear(absl::CivilDay day) {
  absl::civil_year_t iso_year = GetIsoYear(day);
  // Find the last Thursday.
  absl::CivilDay last_day(iso_year, 12, 31);
  absl::CivilDay last_thursday =
      PrevWeekdayOrToday(last_day, absl::Weekday::thursday);
  // The last day of the iso year is the next sunday.
  return absl::NextWeekday(last_thursday, absl::Weekday::sunday);
}

int GetIsoWeek(absl::CivilDay day) {
  absl::CivilDay monday_of_week =
      PrevWeekdayOrToday(day, absl::Weekday::monday);
  absl::CivilDay first_monday_of_iso_year = GetFirstDayOfIsoYear(day);
  absl::civil_diff_t iso_week =
      ((monday_of_week - first_monday_of_iso_year) / 7) + 1;
  ABSL_CHECK_GE(iso_week, 1);
  ABSL_CHECK_LE(iso_week, 53);
  return static_cast<int>(iso_week);
}

// See https://en.wikipedia.org/wiki/Leap_year#Algorithm for the definition of
// leap year.
bool IsLeapYear(int64_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

}  // namespace date_time_util_internal
}  // namespace functions
}  // namespace googlesql
