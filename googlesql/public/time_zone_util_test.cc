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


#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/functions/date_time_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace googlesql {

TEST(TimeZoneTests, KyivTimezone) {
  absl::TimeZone tz1;
  GOOGLESQL_ASSERT_OK(functions::MakeTimeZone("Europe/Kyiv", &tz1));
  absl::TimeZone tz2;
  GOOGLESQL_ASSERT_OK(functions::MakeTimeZone("Europe/Kiev", &tz2));
  // The == operator doesn't work in the obvious way for absl::TimeZone. It uses
  // an internal Impl class and is implemented on pointer equality of those
  // Impls. Instead, to approximate equality, we step forward 10,000 hours from
  // the beginning of 2022, 10 hours at a time, to make sure the timezones have
  // the same UTC offset throughout the year.
  absl::Time base_time = absl::FromCivil(
      absl::CivilSecond(2022, 1, 1, 12, 0, 0), absl::UTCTimeZone());
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(tz1.At(base_time).offset, tz2.At(base_time).offset);
    base_time += absl::Hours(10);
  }
}

}  // namespace googlesql
