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

#ifndef GOOGLESQL_COMMON_THREAD_STACK_H_
#define GOOGLESQL_COMMON_THREAD_STACK_H_

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Determine whether there is comfortably enough stack left.
// Useful placed at the top of recursive functions.
ABSL_MUST_USE_RESULT bool ThreadHasEnoughStack();

// Prints the top frames in the stack along with the amount of space
// each one takes, to help debug stack overflow issues.
// Attempts to not call new or malloc.
void LogStackExhaustion(absl::string_view msg);

// This macro creates the status only once so we don't have to call malloc
// when we are short of stack. Therefore the msg must be a string literal
// and this is enforced by concatenating it explicitly with ""
#define GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(msg)                  \
  do {                                                             \
    static const absl::Status* exhausted_error =                   \
        new absl::Status(absl::ResourceExhaustedError(("" msg)));  \
    if (!ABSL_PREDICT_TRUE(::googlesql::ThreadHasEnoughStack())) { \
      ::googlesql::LogStackExhaustion(msg);                        \
      return *exhausted_error;                                     \
    }                                                              \
  } while (0)

struct ThreadStackEstimatedUsage {
  size_t stack_used_min;
  size_t stack_used_max;
};

// ThreadStackStats encapsulates the per-thread stack usage and boundaries
// and is stored in a thread local.
class ThreadStackStats {
 public:
  size_t ThreadStackTotalBytes() const {
    return thread_stack_high_ - thread_stack_low_;
  }
  size_t ThreadStackPeakUsedBytes() const {
    return thread_stack_estimated_usage_.stack_used_max -
           thread_stack_estimated_usage_.stack_used_min;
  }

  uintptr_t ThreadStackLowestAddress() const { return thread_stack_low_; }

  size_t ThreadStackAvailableBytes() const { return thread_stack_available_; }
  size_t ThreadStackUsedBytes() const;

  ThreadStackEstimatedUsage ResetPeakStackUsedBytes();

  void MergeStackEstimatedUsage(ThreadStackEstimatedUsage usage);

 protected:
  void SetToCurrentThreadStackBoundaries();

 private:
  std::uintptr_t thread_stack_low_ = 0;
  std::uintptr_t thread_stack_high_ =
      std::numeric_limits<std::uintptr_t>::max();
  ThreadStackEstimatedUsage thread_stack_estimated_usage_ = {
      .stack_used_min = std::numeric_limits<std::uintptr_t>::max(),
      .stack_used_max = std::numeric_limits<std::uintptr_t>::min()};
  size_t thread_stack_available_ = 0;

  void ThreadStackUpdateAvailableBytes();

  friend ThreadStackStats& GetCurrentThreadStackStats();
};
std::ostream& operator<<(std::ostream& os, const ThreadStackStats&);

// Returns the thread local stats. It is mutable so that the estimated usage
// bounds can be reset.
ABSL_MUST_USE_RESULT ThreadStackStats& GetCurrentThreadStackStats();

// Returns a string representing a stack trace of the current
// thread.
//
// This function is intended for logging/debugging purposes only; it is computed
// on a best-effort basis, and the format is unspecified.
//
std::string CurrentStackTrace();

std::optional<pthread_t> ThreadStackDumperThread();
}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_THREAD_STACK_H_
