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
#include "googlesql/base/clock.h"

#include <limits>
#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace googlesql_base {
namespace {

TEST(SimulatedClockTest, TimeInitializedToZero) {
  SimulatedClock simclock;
  EXPECT_EQ(absl::UnixEpoch(), simclock.TimeNow());
}

TEST(SimulatedClockTest, Now_SetTime) {
  SimulatedClock simclock;
  absl::Time now = simclock.TimeNow();

  now += absl::Seconds(123);
  simclock.SetTime(now);
  EXPECT_EQ(now, simclock.TimeNow());

  now += absl::Seconds(123);
  simclock.SetTime(now);
  EXPECT_EQ(now, simclock.TimeNow());

  now += absl::ZeroDuration();
  simclock.SetTime(now);
  EXPECT_EQ(now, simclock.TimeNow());
}

TEST(SimulatedClockTest, Now_AdvanceTime) {
  SimulatedClock simclock;
  absl::Time now = simclock.TimeNow();

  simclock.AdvanceTime(absl::Seconds(123));
  now += absl::Seconds(123);
  EXPECT_EQ(now, simclock.TimeNow());

  simclock.AdvanceTime(absl::Seconds(123));
  now += absl::Seconds(123);
  EXPECT_EQ(now, simclock.TimeNow());

  simclock.AdvanceTime(absl::ZeroDuration());
  now += absl::ZeroDuration();
  EXPECT_EQ(now, simclock.TimeNow());
}

}  // namespace
}  // namespace googlesql_base
