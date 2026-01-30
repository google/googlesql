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

#include "googlesql/common/timer_util.h"

#include <cstdint>

#include "google/protobuf/duration.pb.h"
#include "gtest/gtest.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace googlesql::internal {

namespace {
absl::Duration ProtoDurationToAbseil(const google::protobuf::Duration& proto) {
  return absl::Seconds(proto.seconds()) + absl::Nanoseconds(proto.nanos());
}
}  // namespace

// We don't really want to test their absolute correctness, we assume the
// the timers are generally correct, we are mostly ensuring they aren't
// wildly wrong, which indicates a problem with our interaction in some way.
TEST(ScopedTimer, ScopedTimerWallTimeApproxCorrect) {
  for (int i = 0; i < 10; ++i) {
    constexpr absl::Duration sleep_duration = absl::Milliseconds(100);
    TimedValue accumulate;
    {
      auto timer = MakeScopedTimerStarted(&accumulate);
      int64_t counter = 0;
      absl::Time end = absl::Now() + sleep_duration;
      do {
        ++counter;
      } while (absl::Now() < end);
      EXPECT_GT(counter, 0);
    }
    const absl::Duration measured_wall =
        ProtoDurationToAbseil(accumulate.ToExecutionStatsProto().wall_time());
    const absl::Duration measured_cpu =
        ProtoDurationToAbseil(accumulate.ToExecutionStatsProto().cpu_time());
    EXPECT_GE(measured_wall, sleep_duration);
    EXPECT_GE(measured_wall, measured_cpu);
    EXPECT_GT(measured_cpu, absl::ZeroDuration());
    EXPECT_GT(accumulate.ToExecutionStatsProto().stack_available_bytes(), 0);
    EXPECT_GE(accumulate.ToExecutionStatsProto().stack_peak_used_bytes(), 0);
    const absl::Duration expected = sleep_duration;
    const absl::Duration tolerance = absl::Milliseconds(50);

    if (absl::AbsDuration(expected - measured_wall) < tolerance) {
      return;
    }
  }
  // The tolerance is already very very high, probably worth investigating for
  // a real bug before increasing the tolerance more.
  FAIL() << "After 10 tries, timer still not returning times within tolerance";
}

TEST(ScopedTimer, ScopedTimerCPUApproxCorrect) {
  constexpr absl::Duration work_duration = absl::Milliseconds(100);
  constexpr double allowed_thread_contention = 10;
  TimedValue accumulate;
  {
    auto timer = MakeScopedTimerStarted(&accumulate);
    absl::Time end = absl::Now() + work_duration;
    uint64_t totally_random = 0;
    absl::BitGen bitgen;
    while (absl::Now() < end || totally_random == 0) {
      // Loop to burn CPU.
      totally_random += absl::Uniform<uint64_t>(bitgen);
    }
  }
  const absl::Duration measured_wall =
      ProtoDurationToAbseil(accumulate.ToExecutionStatsProto().wall_time());
  const absl::Duration measured_cpu =
      ProtoDurationToAbseil(accumulate.ToExecutionStatsProto().cpu_time());
  EXPECT_GE(measured_wall, work_duration);
  EXPECT_GE(measured_cpu, work_duration / allowed_thread_contention);
}

TEST(TimedValue, InitializesToZero) {
  TimedValue v1;
  auto execution_stats = v1.ToExecutionStatsProto();
  const absl::Duration measured_wall =
      ProtoDurationToAbseil(execution_stats.wall_time());
  const absl::Duration measured_cpu =
      ProtoDurationToAbseil(execution_stats.cpu_time());

  EXPECT_EQ(measured_wall, absl::Nanoseconds(0));
  EXPECT_EQ(measured_cpu, absl::Nanoseconds(0));

  EXPECT_EQ(execution_stats.stack_available_bytes(), 0);
  EXPECT_EQ(execution_stats.stack_peak_used_bytes(), 0);
}

TEST(TimedValue, AccumulateDuration) {
  TimedValue v1;
  v1.Accumulate(absl::Nanoseconds(15));
  EXPECT_EQ(v1.elapsed_duration(), absl::Nanoseconds(15));

  v1.Accumulate(absl::Nanoseconds(15));
  EXPECT_EQ(v1.elapsed_duration(), absl::Nanoseconds(30));
}

TEST(TimedValue, AccumulateTimedValue) {
  TimedValue v1;
  v1.Accumulate(absl::Nanoseconds(15));

  TimedValue v2;
  v2.Accumulate(v1);
  EXPECT_EQ(v2.elapsed_duration(), absl::Nanoseconds(15));
  v2.Accumulate(v1);
  EXPECT_EQ(v2.elapsed_duration(), absl::Nanoseconds(30));
}

// We just want to make sure it's somewhere in the ballpark.
TEST(TimedValue, AccumulateTimerApproxCorrect) {
  for (int i = 0; i < 10; ++i) {
    TimedValue v1;
    constexpr absl::Duration sleep_duration = absl::Milliseconds(100);
    auto timer = MakeTimerStarted();
    absl::SleepFor(sleep_duration);
    v1.Accumulate(timer);
    const absl::Duration measured = v1.elapsed_duration();
    const absl::Duration expected = sleep_duration;
    const absl::Duration tolerance = absl::Milliseconds(30);

    if (absl::AbsDuration(expected - measured) < tolerance) {
      return;
    }
  }
  // The tolerance is already very very high, probably worth investigating for
  // a real bug before increasing the tolerance more.
  FAIL() << "After 10 tries, timer still not returning times within tolerance";
}

}  // namespace googlesql::internal
