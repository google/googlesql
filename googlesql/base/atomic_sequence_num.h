/*
 *
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ATOMIC_SEQUENCE_NUM_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ATOMIC_SEQUENCE_NUM_H_
// Atomic operation to generate unique sequence numbers from a counter.
//
//     googlesql_base::SequenceNumber x;
//      ...
//     y = x.GetNext();     // get the next sequence number

#include <atomic>

namespace googlesql_base {

class SequenceNumber {
 public:
  constexpr SequenceNumber() : word_(0) {}

  SequenceNumber(const SequenceNumber&) = delete;
  SequenceNumber& operator=(const SequenceNumber&) = delete;

  ~SequenceNumber() = default;

  // We use intptr_t as a proxy for a type most likely to be sufficiently
  // large and lock free.
  using Value = intptr_t;

  // Return the integer one greater than was returned by the previous call on
  // this instance, or 0 if there have been no such calls.
  // Provided overflow does not occur, no two calls on the same instance will
  // return the same value, even in the face of concurrency.
  Value GetNext() {
    // As always, clients may not assume properties implied by the
    // implementation, which may change.
    return word_.fetch_add(1, std::memory_order_relaxed);
  }

  // SequenceNumber is implemented as a class specifically to stop clients
  // from reading the value of word_ without also incrementing it.
  // Please do not add such a call.

 private:
  std::atomic<Value> word_;
};

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ATOMIC_SEQUENCE_NUM_H_
