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

#include "googlesql/base/logging.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace googlesql_base {

namespace {

// -----------------------------------------------------------------
// RealTimeClock
//
// This class is thread-safe.
class RealTimeClock : public Clock {
 public:
  ~RealTimeClock() override {
    ABSL_LOG(FATAL) << "RealTimeClock should never be destroyed";
  }

  absl::Time TimeNow() override { return absl::Now(); }
};

}  // namespace

Clock* Clock::RealClock() {
  static RealTimeClock* rtclock = new RealTimeClock();
  return rtclock;
}

SimulatedClock::SimulatedClock(absl::Time t) : now_(t) {}

absl::Time SimulatedClock::TimeNow() {
  absl::ReaderMutexLock l(&lock_);
  return now_;
}

void SimulatedClock::SetTime(absl::Time t) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  UpdateTime([this, t]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { now_ = t; });
}

void SimulatedClock::AdvanceTime(absl::Duration d)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  UpdateTime([this, d]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { now_ += d; });
}

template <class T>
void SimulatedClock::UpdateTime(const T& now_updater) {
  lock_.Lock();
  now_updater();  // reset now_
  lock_.Unlock();
}

}  // namespace googlesql_base
