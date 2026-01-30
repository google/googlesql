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

#include "googlesql/common/thread_stack.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(size_t, googlesql_enough_stack_bytes,
          2 * PTHREAD_STACK_MIN, "Error if available stack falls below this"
);

namespace googlesql {

std::ostream& operator<<(std::ostream& os, const ThreadStackStats& stats) {
  return os << absl::StrFormat(
             "ThreadStackStats{addr=%p,total=%d,peak_used=%d}",
             (void*)stats.ThreadStackLowestAddress(),
             stats.ThreadStackTotalBytes(), stats.ThreadStackPeakUsedBytes());
}

void ThreadStackStats::SetToCurrentThreadStackBoundaries() {
#if defined(__APPLE__)
  void* stack_addr = pthread_get_stackaddr_np(pthread_self());
  size_t stack_size = pthread_get_stacksize_np(pthread_self());
  // The Apple stack pointer is to the highest address, the opposite
  // of the Linux convention.
  thread_stack_high_ = absl::bit_cast<std::uintptr_t>(stack_addr);
  ABSL_DCHECK_GT(thread_stack_high_, stack_size);
  thread_stack_low_ = thread_stack_high_ - stack_size;
#else
  bool stack_limit_overridden = false;
  if (!stack_limit_overridden) {
    pthread_attr_t thread_attr;
    if (int err = pthread_getattr_np(pthread_self(), &thread_attr); err != 0) {
      return;
    }
    void* stack_addr;
    size_t stack_size;

    if (int err = pthread_attr_getstack(&thread_attr, &stack_addr, &stack_size);
        err != 0) {
      return;
    }
    thread_stack_low_ = absl::bit_cast<std::uintptr_t>(stack_addr);
    thread_stack_high_ = thread_stack_low_ + stack_size;

    if (int err = pthread_attr_destroy(&thread_attr); err != 0) {
      return;
    }
  }
#endif
}

size_t ThreadStackStats::ThreadStackUsedBytes() const {
  return ThreadStackTotalBytes() - ThreadStackAvailableBytes();
}
void ThreadStackStats::MergeStackEstimatedUsage(
    ThreadStackEstimatedUsage usage) {
  thread_stack_estimated_usage_.stack_used_min = std::min(
      thread_stack_estimated_usage_.stack_used_min, usage.stack_used_min);
  thread_stack_estimated_usage_.stack_used_max = std::max(
      thread_stack_estimated_usage_.stack_used_max, usage.stack_used_max);
}
ThreadStackEstimatedUsage ThreadStackStats::ResetPeakStackUsedBytes() {
  size_t used = ThreadStackUsedBytes();
  ThreadStackEstimatedUsage reset = {.stack_used_min = used,
                                     .stack_used_max = used};
  std::swap(thread_stack_estimated_usage_, reset);
  return reset;
}
void ThreadStackStats::ThreadStackUpdateAvailableBytes() {
  // Using a temporary on the stack to guess the stack location doesn't work
  // in asan + opt mode.
  std::uintptr_t stack_ptr =
      absl::bit_cast<uintptr_t>(__builtin_frame_address(0));
  // Prove we are on the right thread.
  ABSL_DCHECK_GE(stack_ptr, thread_stack_low_) << *this;
  ABSL_DCHECK_LT(stack_ptr, thread_stack_high_) << *this;
  size_t used = thread_stack_high_ - stack_ptr;
  thread_stack_estimated_usage_.stack_used_min =
      std::min(thread_stack_estimated_usage_.stack_used_min, used);
  thread_stack_estimated_usage_.stack_used_max =
      std::max(thread_stack_estimated_usage_.stack_used_max, used);

  thread_stack_available_ = stack_ptr - thread_stack_low_;
}

ThreadStackStats& GetCurrentThreadStackStats() {
  thread_local ThreadStackStats stats = []() {
    ThreadStackStats stats;
    stats.SetToCurrentThreadStackBoundaries();
    return stats;
  }();
  stats.ThreadStackUpdateAvailableBytes();
  return stats;
}

bool ThreadHasEnoughStack() {
#ifndef __EMSCRIPTEN__
  const size_t remaining =
      GetCurrentThreadStackStats().ThreadStackAvailableBytes();
  bool enough = remaining >= absl::GetFlag(FLAGS_googlesql_enough_stack_bytes);

  return enough;
#else
  return true;
#endif  // __EMSCRIPTEN__
}

void LogStackExhaustion(absl::string_view msg) {
  return;  // TODO: Implement LogStackExhaustion()
}

std::string CurrentStackTrace() {
  return "";  // TODO: Implement CurrentStackTrace()
}
}  // namespace googlesql
