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

#ifndef GOOGLESQL_LOCAL_SERVICE_STATE_H_
#define GOOGLESQL_LOCAL_SERVICE_STATE_H_

#include <stddef.h>

#include <cstdint>
#include <map>
#include <memory>
#include <type_traits>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "googlesql/base/map_util.h"

namespace googlesql {
namespace local_service {

class GenericState;

// Pool of saved states that can be shared by multiple statements.
// The state class T must extend GenericState and must be thread safe.
template<class T>
class SharedStatePool {
 public:
  SharedStatePool() : next_id_(0) {}
  SharedStatePool(const SharedStatePool&) = delete;
  SharedStatePool& operator=(const SharedStatePool&) = delete;

  // Register a state object into the pool. The pool takes ownership.
  // Will return -1 if the state is null or already registered.
  int64_t Register(std::shared_ptr<T> state) {
    if (state == nullptr) {
      return -1;
    }

    absl::MutexLock lock(&mutex_);
    int64_t id = next_id_++;
    if (!state->SetId(id)) {
      return -1;
    }
    saved_states_[id] = state;
    return id;
  }

  // Register a state object into the pool. The pool takes ownership.
  // Will return -1 if the state is null or already registered.
  int64_t Register(T* state) {
    // We reimplement this, because calling the shared_ptr<T> version would
    // pass ownership to a shared_ptr, which can deallocate in the case of error
    // So, if someone registers twice, it will deallocate rather than just
    // returning -1 as expected.
    if (state == nullptr) {
      return -1;
    }

    absl::MutexLock lock(&mutex_);
    int64_t id = next_id_++;
    if (!state->SetId(id)) {
      return -1;
    }
    saved_states_[id].reset(state);
    return id;
  }

  bool Has(int64_t id) const {
    absl::MutexLock lock(&mutex_);
    return googlesql_base::ContainsKey(saved_states_, id);
  }

  // Get a state object with given id, ownership is shared by the pool and all
  // threads that currently hold the state object.
  std::shared_ptr<T> Get(int64_t id) {
    absl::MutexLock lock(&mutex_);
    std::shared_ptr<T>* result = googlesql_base::FindOrNull(saved_states_, id);
    if (result == nullptr) {
      return nullptr;
    } else {
      return *result;
    }
  }

  // Removes a state object from the pool. The state will be deleted immediately
  // if not held by any other threads, or after all threads releasing it.
  bool Delete(int64_t id) {
    absl::MutexLock lock(&mutex_);
    if (!googlesql_base::ContainsKey(saved_states_, id)) {
      return false;
    }
    saved_states_.erase(id);
    return true;
  }

  size_t NumSavedStates() {
    absl::MutexLock lock(&mutex_);
    return saved_states_.size();
  }

 private:
  mutable absl::Mutex mutex_;
  int64_t next_id_ ABSL_GUARDED_BY(mutex_);
  std::map<int64_t, std::shared_ptr<T>> saved_states_ ABSL_GUARDED_BY(mutex_);

  static_assert(
      std::is_base_of<GenericState, T>::value,
      "SharedStatePool only works with subclass of GenericState");
};

// Base class of saved states with an int64 id.
class GenericState {
 public:
  GenericState() = default;
  virtual ~GenericState() = default;

  int64_t GetId() const { return id_; }
  bool IsRegistered() { return id_ != -1; }

 private:
  int64_t id_ = -1;

  // Should only be called by SharedStatePool.
  bool SetId(int64_t id) {
    if (id_ == -1) {
      id_ = id;
      return true;
    }
    return false;
  }

  template<class T> friend class SharedStatePool;

  GenericState(const GenericState&) = delete;
  GenericState& operator=(const GenericState&) = delete;
};

}  // namespace local_service
}  // namespace googlesql

#endif  // GOOGLESQL_LOCAL_SERVICE_STATE_H_
