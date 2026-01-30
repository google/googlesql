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

// Utilities for testing things that work with tuples.

#ifndef GOOGLESQL_REFERENCE_IMPL_TUPLE_TEST_UTIL_H_
#define GOOGLESQL_REFERENCE_IMPL_TUPLE_TEST_UTIL_H_

#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "googlesql/common/internal_value.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/variable_id.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// A simple iterator over a vector of tuples. If 'preserves_order' is false, it
// returns the tuples in the reverse order.
class TestTupleIterator : public TupleIterator {
 public:
  static std::string GetDebugString() { return "TestTupleIterator"; }

  TestTupleIterator(absl::Span<const VariableId> variables,
                    absl::Span<const TupleData> values, bool preserves_order,
                    const absl::Status& end_status)
      : schema_(variables),
        end_status_(end_status),
        preserves_order_(preserves_order),
        values_(values.begin(), values.end()) {
    if (!preserves_order_) {
      std::reverse(values_.begin(), values_.end());
    }
  }

  TestTupleIterator(const TestTupleIterator&) = delete;
  TestTupleIterator& operator=(const TestTupleIterator&) = delete;

  const TupleSchema& Schema() const override { return schema_; }

  TupleData* Next() override {
    if (index_ == values_.size()) {
      status_ = end_status_;
      return nullptr;
    }
    TupleData& current = values_[index_];
    ++index_;
    return &current;
  }

  absl::Status Status() const override { return status_; }

  bool PreservesOrder() const override { return preserves_order_; }

  absl::Status DisableReordering() override {
    if (!preserves_order_) {
      GOOGLESQL_RET_CHECK_EQ(index_, 0);
      std::reverse(values_.begin(), values_.end());
    }
    preserves_order_ = true;
    return absl::OkStatus();
  }

  std::string DebugString() const override { return GetDebugString(); }

 private:
  const TupleSchema schema_;
  const absl::Status end_status_;
  bool preserves_order_;
  std::vector<TupleData> values_;
  int index_ = 0;
  absl::Status status_;
};

// Returns all the tuples in 'iter' and populates 'end_status' with the final
// status.
inline std::vector<TupleData> ReadFromTupleIteratorFull(
    TupleIterator* iter, absl::Status* end_status) {
  std::vector<TupleData> tuples;
  while (true) {
    const TupleData* next = iter->Next();
    if (next == nullptr) {
      *end_status = iter->Status();
      return tuples;
    }
    tuples.push_back(*next);
  }
}

// Simpler form of the above that returns an error if the final status is an
// error.
inline absl::StatusOr<std::vector<TupleData>> ReadFromTupleIterator(
    TupleIterator* iter) {
  absl::Status end_status;
  std::vector<TupleData> data = ReadFromTupleIteratorFull(iter, &end_status);
  GOOGLESQL_RETURN_IF_ERROR(end_status);
  return data;
}

// Returns a TupleData corresponding to 'values' where all slots have trivial
// SharedProtoStates, which are also added to 'shared_states' if it is non-NULL.
inline TupleData CreateTestTupleData(
    absl::Span<const Value> values,
    std::vector<const TupleSlot::SharedProtoState*>* shared_states = nullptr) {
  TupleData data(values.size());
  if (shared_states != nullptr) {
    shared_states->reserve(values.size());
  }
  for (int i = 0; i < data.num_slots(); ++i) {
    TupleSlot* slot = data.mutable_slot(i);
    slot->SetValue(values[i]);
    if (shared_states != nullptr) {
      shared_states->push_back(slot->mutable_shared_proto_state()->get());
    }
  }
  return data;
}

// Returns a std::vector<TupleData> corresponding to 'values' where all slots
// have empty maps, which are also added to 'shared_states' if it is non-NULL
inline std::vector<TupleData> CreateTestTupleDatas(
    absl::Span<const std::vector<Value>> values,
    std::vector<std::vector<const TupleSlot::SharedProtoState*>>*
        shared_states = nullptr) {
  std::vector<TupleData> datas;
  datas.reserve(values.size());
  if (shared_states != nullptr) {
    shared_states->reserve(values.size());
  }
  for (const std::vector<Value>& data_values : values) {
    std::vector<const TupleSlot::SharedProtoState*> data_shared_states;
    datas.push_back(CreateTestTupleData(data_values, &data_shared_states));
    if (shared_states != nullptr) {
      shared_states->push_back(data_shared_states);
    }
  }
  return datas;
}

// Matches a std::shared_pointer<SharedProtoState> against its raw pointer.
MATCHER_P(HasRawPointer, raw_pointer, "") { return arg.get() == raw_pointer; }

// Teach googletest how to print TupleSlots.
inline void PrintTo(const TupleSlot& slot, std::ostream* os) {
  std::shared_ptr<TupleSlot::SharedProtoState> shared_state =
      *slot.mutable_shared_proto_state();
  *os << "<value: " << slot.value() << ", shared_state: " << shared_state.get()
      << ">";
}

// Matches a TupleSlot against 'expected_value' and 'shared_state_matcher',
// which must be castable to Matcher<std::shared_ptr<SharedProtoState>>.
MATCHER_P2(IsTupleSlotWith, expected_value, shared_state_matcher, "") {
  std::string reason;
  if (!InternalValue::Equals(expected_value, arg.value(),
                             ValueEqualityCheckOptions{.reason = &reason})) {
    *result_listener << reason;
    return false;
  }

  auto matcher = static_cast<
      ::testing::Matcher<std::shared_ptr<TupleSlot::SharedProtoState>>>(
      shared_state_matcher);
  if (!matcher.MatchAndExplain(*arg.mutable_shared_proto_state(),
                               result_listener)) {
    return false;
  }
  return true;
}

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_TUPLE_TEST_UTIL_H_
