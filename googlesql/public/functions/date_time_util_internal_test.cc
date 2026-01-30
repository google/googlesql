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

#include <ostream>

#include "gtest/gtest.h"
#include "absl/time/civil_time.h"

namespace googlesql {
namespace functions {
namespace date_time_util_internal {
namespace {
struct IsoWeekTestCase {
  struct Day {
    int y;
    int m;
    int d;
  };
  Day input;
  Day expected_iso_1st_day;
  int expected_iso_year;
  int expected_iso_week;
};

std::ostream& operator<<(std::ostream& out, const IsoWeekTestCase& test) {
  return out << "{"
             << "{" << test.input.y << ", " << test.input.m << ", "
             << test.input.d << "}, {" << test.expected_iso_1st_day.y << ", "
             << test.expected_iso_1st_day.m << ", "
             << test.expected_iso_1st_day.d << "}, " << test.expected_iso_year
             << ", " << test.expected_iso_week << "}";
}

const IsoWeekTestCase kIsoWeekTestCases[] = {
    {{0000, 1, 1}, {-1, 1, 4}, -1, 52},
    {{9999, 12, 31}, {9999, 1, 4}, 9999, 52},

    // 2004 is a really interesting "year"!
    //   Start: 2003-12-29
    //   End  : 2005-01-02
    {{2003, 12, 29}, {2003, 12, 29}, 2004, 1},
    {{2004, 1, 1}, {2003, 12, 29}, 2004, 1},
    {{2005, 1, 1}, {2003, 12, 29}, 2004, 53},
    {{2005, 1, 2}, {2003, 12, 29}, 2004, 53},

    {{2005, 12, 31}, {2005, 1, 3}, 2005, 52},

    {{2007, 1, 1}, {2007, 1, 1}, 2007, 1},
    {{2007, 12, 30}, {2007, 1, 1}, 2007, 52},
    {{2007, 12, 31}, {2007, 12, 31}, 2008, 1},

    {{2008, 1, 1}, {2007, 12, 31}, 2008, 1},
    {{2008, 12, 28}, {2007, 12, 31}, 2008, 52},
    {{2008, 12, 29}, {2008, 12, 29}, 2009, 1},
    {{2008, 12, 30}, {2008, 12, 29}, 2009, 1},
    {{2008, 12, 31}, {2008, 12, 29}, 2009, 1},

    {{2009, 1, 1}, {2008, 12, 29}, 2009, 1},
    {{2009, 12, 31}, {2008, 12, 29}, 2009, 53},

    {{2010, 1, 1}, {2008, 12, 29}, 2009, 53},
    {{2010, 1, 2}, {2008, 12, 29}, 2009, 53},
    {{2010, 1, 3}, {2008, 12, 29}, 2009, 53},
    {{2010, 1, 4}, {2010, 1, 4}, 2010, 1},

    {{2018, 10, 7}, {2018, 1, 1}, 2018, 40},   // sunday
    {{2018, 10, 8}, {2018, 1, 1}, 2018, 41},   // monday
    {{2018, 10, 9}, {2018, 1, 1}, 2018, 41},   // tuesday
    {{2018, 10, 10}, {2018, 1, 1}, 2018, 41},  // wednesday
    {{2018, 10, 11}, {2018, 1, 1}, 2018, 41},  // thursday
    {{2018, 10, 12}, {2018, 1, 1}, 2018, 41},  // friday
    {{2018, 10, 13}, {2018, 1, 1}, 2018, 41},  // saturday
    {{2018, 10, 14}, {2018, 1, 1}, 2018, 41},  // sunday
    {{2018, 10, 15}, {2018, 1, 1}, 2018, 42},  // monday
};

}  // namespace

class IsoWeekTest : public ::testing::TestWithParam<IsoWeekTestCase> {};

TEST_P(IsoWeekTest, ISOWeekNumberTest) {
  IsoWeekTestCase test = GetParam();
  absl::CivilDay day(test.input.y, test.input.m, test.input.d);
  int iso_week = GetIsoWeek(day);
  int iso_year = static_cast<int>(GetIsoYear(day));
  absl::CivilDay iso_1st_day = GetFirstDayOfIsoYear(day);
  EXPECT_EQ(iso_week, test.expected_iso_week) << test;
  EXPECT_EQ(iso_year, test.expected_iso_year) << test;
  EXPECT_EQ(iso_1st_day, absl::CivilDay(test.expected_iso_1st_day.y,
                                        test.expected_iso_1st_day.m,
                                        test.expected_iso_1st_day.d))
      << test;
}

INSTANTIATE_TEST_SUITE_P(IsoWeekTests, IsoWeekTest,
                         ::testing::ValuesIn(kIsoWeekTestCases));

namespace {
struct IsLeapYearTestCase {
  int64_t year;
  bool leap;
};

const IsLeapYearTestCase kIsLeapYearTestCases[] = {
    // clang-format off
    {.year = 0, .leap = true},
    {.year = 4, .leap = true},
    {.year = 8, .leap = true},
    {.year = 12, .leap = true},
    {.year = 16, .leap = true},
    {.year = 20, .leap = true},
    {.year = 24, .leap = true},
    {.year = 1996, .leap = true},
    {.year = 2004, .leap = true},
    {.year = 2020, .leap = true},
    {.year = 2020, .leap = true},
    {.year = 400, .leap = true},
    {.year = 800, .leap = true},
    {.year = 1200, .leap = true},
    {.year = 1600, .leap = true},
    {.year = 1972, .leap = true},
    {.year = 2004, .leap = true},
    {.year = 2040, .leap = true},
    {.year = 2052, .leap = true},
    {.year = 2400, .leap = true},
    {.year = 8400, .leap = true},
    {.year = 10000, .leap = true},
    {.year = 1, .leap = false},
    {.year = 2, .leap = false},
    {.year = 3, .leap = false},
    {.year = 5, .leap = false},
    {.year = 6, .leap = false},
    {.year = 7, .leap = false},
    {.year = 9, .leap = false},
    {.year = 10, .leap = false},
    {.year = 11, .leap = false},
    {.year = 13, .leap = false},
    {.year = 14, .leap = false},
    {.year = 15, .leap = false},
    {.year = 18, .leap = false},
    {.year = 100, .leap = false},
    {.year = 200, .leap = false},
    {.year = 300, .leap = false},
    {.year = 500, .leap = false},
    {.year = 600, .leap = false},
    {.year = 700, .leap = false},
    {.year = 900, .leap = false},
    {.year = 1000, .leap = false},
    {.year = 1100, .leap = false},
    {.year = 1300, .leap = false},
    {.year = 1900, .leap = false},
    {.year = 1970, .leap = false},
    {.year = 2001, .leap = false},
    {.year = 2010, .leap = false},
    {.year = 2011, .leap = false},
    {.year = 2050, .leap = false},
    {.year = 2100, .leap = false},
    {.year = 2200, .leap = false},
    {.year = 2300, .leap = false},
    {.year = 8100, .leap = false},
    {.year = 10002, .leap = false},
    // clang-format on
};
}  // namespace

class IsLeapYearTest : public ::testing::TestWithParam<IsLeapYearTestCase> {};

TEST_P(IsLeapYearTest, IsLeapYearTest) {
  IsLeapYearTestCase test = GetParam();
  EXPECT_EQ(IsLeapYear(test.year), test.leap)
      << "IsLeapYear(" << test.year << ")";
}

INSTANTIATE_TEST_SUITE_P(IsLeapYearTests, IsLeapYearTest,
                         ::testing::ValuesIn(kIsLeapYearTestCases));

}  // namespace date_time_util_internal
}  // namespace functions
}  // namespace googlesql
