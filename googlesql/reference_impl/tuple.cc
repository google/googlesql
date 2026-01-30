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

#include "googlesql/reference_impl/tuple.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

// Check that `num_bytes` can be represented as an int64_t. If not, populates
// `status` with a ResourceExhaustedError and returns false. Otherwise, returns
// true and sets `res` to the int64_t representation of `num_bytes`.
bool SafeUInt64ToInt64NumBytes(uint64_t num_bytes, int64_t* res,
                               absl::Status* status) {
  if (num_bytes > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    *status = absl::ResourceExhaustedError(absl::Substitute(
        "Out of memory: impossible to request $0 bytes.", num_bytes));
    return false;
  }
  *res = static_cast<int64_t>(num_bytes);
  return true;
}

}  // namespace

// -------------------------------------------------------
// MemoryAccountant
// -------------------------------------------------------

bool MemoryAccountant::RequestUInt64Bytes(uint64_t num_bytes,
                                          absl::Status* status) {
  int64_t num_bytes_int64;
  if (!SafeUInt64ToInt64NumBytes(num_bytes, &num_bytes_int64, status)) {
    return false;
  }
  return RequestBytes(num_bytes_int64, status);
}

// -------------------------------------------------------
// TupleSchema
// -------------------------------------------------------

TupleSchema::TupleSchema(absl::Span<const VariableId> variables)
    : variables_(variables.begin(), variables.end()) {
  vars_to_idxs_.reserve(variables_.size());
  for (int i = 0; i < variables_.size(); ++i) {
    vars_to_idxs_.insert(i);
  }
}

void TupleSchema::AddVariable(VariableId variable) {
  variables_.push_back(variable);
  vars_to_idxs_.reserve(variables_.size());
  vars_to_idxs_.insert(static_cast<int>(variables_.size() - 1));
}

std::optional<int> TupleSchema::FindIndexForVariable(
    const VariableId& variable) const {
  auto iter = vars_to_idxs_.find(variable);
  if (iter == vars_to_idxs_.end()) {
    return std::nullopt;
  } else {
    return *iter;
  }
}

std::string TupleSchema::DebugString() const {
  std::vector<std::string> vars;
  vars.reserve(variables_.size());
  for (const VariableId& var : variables_) {
    vars.push_back(var.ToString());
  }
  return absl::StrCat("<", absl::StrJoin(vars, ","), ">");
}

// -------------------------------------------------------
// TupleSlot
// -------------------------------------------------------

int64_t TupleSlot::GetPhysicalByteSize() const {
  if (!value_.is_valid()) return sizeof(TupleSlot);
  int64_t num_bytes = value_.physical_byte_size() + sizeof(shared_proto_state_);
  if (shared_proto_state_ != nullptr) {
    num_bytes += sizeof(*shared_proto_state_);
    if (shared_proto_state_->has_value()) {
      num_bytes += sizeof(**shared_proto_state_);
      for (const auto& entry : **shared_proto_state_) {
        num_bytes += sizeof(entry);
        const std::unique_ptr<ProtoFieldValueList>& values = entry.second;
        if (values != nullptr) {
          for (const absl::StatusOr<Value>& status_or_value : *values) {
            num_bytes += sizeof(status_or_value);
            if (status_or_value.ok()) {
              num_bytes += status_or_value.value().physical_byte_size();
            }
          }
        }
      }
    }
  }
  return num_bytes;
}

// -------------------------------------------------------
// Tuple-related functions
// -------------------------------------------------------

std::string Tuple::DebugString(bool verbose) const {
  std::vector<std::string> fields;
  fields.reserve(schema->num_variables());
  for (int var_idx = 0; var_idx < schema->num_variables(); ++var_idx) {
    const VariableId& var = schema->variable(var_idx);
    const Value& value = data->slot(var_idx).value();
    fields.push_back(
        absl::StrCat(var.ToString(), ":", value.DebugString(verbose)));
  }
  return absl::StrCat(verbose ? "Tuple<" : "<", absl::StrJoin(fields, ","),
                      ">");
}

Tuple ConcatTuples(absl::Span<const Tuple> tuples,
                   std::unique_ptr<TupleSchema>* new_schema,
                   std::unique_ptr<TupleData>* new_data) {
  std::vector<VariableId> vars;
  std::vector<TupleSlot> slots;
  for (const Tuple& tuple : tuples) {
    const std::vector<VariableId>& tuple_vars = tuple.schema->variables();
    vars.insert(vars.end(), tuple_vars.begin(), tuple_vars.end());
    const std::vector<TupleSlot>& tuple_slots = tuple.data->slots();
    slots.insert(slots.end(),
                 // Drop any extra slots in 'tuple_slots'.
                 tuple_slots.begin(), tuple_slots.begin() + tuple_vars.size());
  }
  *new_schema = std::make_unique<TupleSchema>(vars);
  *new_data = std::make_unique<TupleData>(slots);
  return Tuple(new_schema->get(), new_data->get());
}

// -------------------------------------------------------
// TupleDataDeque
// -------------------------------------------------------

absl::Status TupleDataDeque::SetSlot(int slot_idx, std::vector<Value> values) {
  GOOGLESQL_RET_CHECK_EQ(values.size(), datas_.size());
  absl::Status status;
  int64_t i = 0;
  for (Entry& entry : datas_) {
    int64_t& byte_size = entry.first;
    TupleData* tuple = entry.second.get();

    TupleSlot* slot = tuple->mutable_slot(slot_idx);
    const int64_t old_slot_size = slot->GetPhysicalByteSize();
    slot->SetValue(std::move(values[i]));
    const int64_t new_slot_size = slot->GetPhysicalByteSize();

    accountant_->ReturnBytes(byte_size);
    byte_size += (new_slot_size - old_slot_size);
    if (!accountant_->RequestBytes(byte_size, &status)) {
      return status;
    }

    ++i;
  }
  return absl::OkStatus();
}

void TupleDataDeque::Sort(const TupleComparator& comparator,
                          bool use_stable_sort) {
  auto entry_comparator = [&comparator](const Entry& entry1,
                                        const Entry& entry2) {
    return comparator(entry1.second, entry2.second);
  };
  if (use_stable_sort) {
    std::stable_sort(datas_.begin(), datas_.end(), entry_comparator);
  } else {
    std::sort(datas_.begin(), datas_.end(), entry_comparator);
  }
}

// -------------------------------------------------------
// ReorderingTupleIterator
// -------------------------------------------------------

TupleData* ReorderingTupleIterator::Next() {
  called_next_ = true;

  if (num_read_from_current_batch_ == current_batch_.size()) {
    // Done with the current batch.
    if (done_status_.has_value()) {
      // There are no more values left in the underlying iterator, or there was
      // an error.
      status_ = done_status_.value();
      return nullptr;
    }

    // Read another batch. We use a constant batch size to avoid storing too
    // much data in memory. We choose the batch size to be large enough so that
    // the reference implementation compliance tests (which depend on the
    // particular scrambling algorithm used until b/110995528 is fixed) still
    // pass because all the data is in one batch.
    const int kBatchSize = 100;
    // TODO: Consider allocating the batch in the constructor to avoid
    // clearing and reserving it each time.
    current_batch_.clear();
    current_batch_.reserve(kBatchSize);
    for (int i = 0; i < kBatchSize; ++i) {
      const TupleData* data = iter_->Next();
      if (data == nullptr) {
        done_status_ = iter_->Status();
        break;
      }
      current_batch_.push_back(*data);
    }
    num_read_from_current_batch_ = 0;

    return Next();
  }

  int index = num_read_from_current_batch_;
  if (reorder_) {
    const int i = num_read_from_current_batch_;
    const int half_size = current_batch_.size() / 2;
    // Iterates over odd indexes, then even indexes. Example for 5 tuples:
    // 0 -> 1  // [0 .. size/2) is mapped to odd indexes
    // 1 -> 3
    // 2 -> 0  // [size/2 .. size) is mapped to even indexes
    // 3 -> 2
    // 4 -> 4
    index = (i < half_size) ? (i * 2 + 1) : (2 * (i - half_size));
  }
  ++num_read_from_current_batch_;
  return &current_batch_[index];
}

}  // namespace googlesql
