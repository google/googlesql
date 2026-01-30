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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_DATE_TIME_UTIL_INTERNAL_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_DATE_TIME_UTIL_INTERNAL_H_

#include "absl/time/civil_time.h"

namespace googlesql {
namespace functions {
namespace date_time_util_internal {

// If `day` is a `weekday`, return `day`, otherwise return the next
// `weekday`.
inline absl::CivilDay NextWeekdayOrToday(absl::CivilDay day,
                                         absl::Weekday weekday) {
  return absl::PrevWeekday(day, weekday) + 7;
}

// If `day` is a `weekday`, return `day`, otherwise return the previous
// `weekday`.
inline absl::CivilDay PrevWeekdayOrToday(absl::CivilDay day,
                                         absl::Weekday weekday) {
  return absl::NextWeekday(day, weekday) - 7;
}

// Weeks start on Monday. The 'year' of a given week is defined as
// the Gregorian calendar year of the Thursday of that week.
//
// [See https://en.wikipedia.org/wiki/ISO_week_date]
// Example:
//
//  [Gregorian] December 2018
//
//  Mon Tue Wed Thu Fri Sat Sun
//   03  04  05  06  07  08  09 -- 2018 Week 49
//   10  11  12  13  14  15  16 -- 2018 Week 50
//   17  18  19  20  21  22  23 -- 2018 Week 51
//   24  26  26  27  28  29  30 -- 2018 Week 52
//   31b 01  02  03a 04  05  06 -- 2019 Week 1
//   07  08  09  10  11  12  13 -- 2019 Week 2
//
//   a 2019-01-03 is a Thursday in iso year 2019.
//   b 2018-12-31 is the Monday prior to that Thursday, making it the first
//     day of the iso year 2019.
//

// Returns the first day of the iso year that `day` is part of. Note, the `year`
// that day is in is defined by the Gregorian year of the Thursday of the same
// week (starting Monday) of that day:
//
// GetIsoYear(day).year() == GetIsoYear(GetFirstDayOfIsoYear(day)).year()
// however, day.year() may not equal GetFirstDayOfIsoYear().year().
absl::CivilDay GetFirstDayOfIsoYear(absl::CivilDay day);

// Returns the last day of the iso year that `day` is part of. Note, the `year`
// that day is in is defined by the Gregorian year of the Thursday of the same
// week (starting Monday) of that day:
//
// GetIsoYear(day).year() == GetIsoYear(GetLastDayOfIsoYear(day)).year()
// however, day.year() may not equal GetLastDayOfIsoYear().year().
absl::CivilDay GetLastDayOfIsoYear(absl::CivilDay day);

// Returns the ISO year for any date between 0001-01-01 and 9999-12-31.
// See above for the definition of a 'year'.
absl::civil_year_t GetIsoYear(absl::CivilDay day);

// Return the ISO week for the given day. Returned values are between 1 and 53
// inclusive.
int GetIsoWeek(absl::CivilDay day);

// Ruturns true if the given year is a leap year, false otherwise.
bool IsLeapYear(int64_t year);

}  // namespace date_time_util_internal
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_DATE_TIME_UTIL_INTERNAL_H_
