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

#include "googlesql/common/simple_evaluator_table_iterator.h"

#include <cstdint>
#include <memory>

#include "absl/flags/flag.h"

ABSL_FLAG(int64_t, googlesql_simple_iterator_call_time_now_rows_period, 1000,
          "Only call googlesql_base::Clock::TimeNow() every this many rows");

namespace googlesql {

absl::Status SimpleEvaluatorTableIterator::SetColumnFilterMap(
    absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map) {
  filter_map_.clear();
  for (auto& entry : filter_map) {
    if (filter_column_idxs_.contains(entry.first)) {
      GOOGLESQL_RET_CHECK(filter_map_.insert(std::move(entry)).second);
    }
  }
  return absl::OkStatus();
}

bool SimpleEvaluatorTableIterator::NextRow() {
  absl::MutexLock l(&mutex_);
  if (cancelled_) return false;

  for (++row_idx_; row_idx_ < num_rows_; ++row_idx_) {
    if ((row_idx_ %
             absl::GetFlag(
                 FLAGS_googlesql_simple_iterator_call_time_now_rows_period) ==
         0) &&
        clock_->TimeNow() > deadline_) {
      deadline_exceeded_ = true;
      return false;
    }

    bool keep_row = true;
    for (const auto& entry : filter_map_) {
      if (!keep_row) break;

      const int column_idx = entry.first;
      const std::unique_ptr<ColumnFilter>& filter = entry.second;

      const Value& value = (*column_major_values_[column_idx])[row_idx_];
      switch (filter->kind()) {
        case ColumnFilter::kRange: {
          const Value& lower_bound = filter->lower_bound();
          const Value& upper_bound = filter->upper_bound();
          keep_row = !lower_bound.is_valid() ||
                     (lower_bound.SqlLessThan(value) == values::True()) ||
                     (lower_bound.SqlEquals(value) == values::True());
          if (!keep_row) break;
          keep_row = !upper_bound.is_valid() ||
                     (value.SqlLessThan(upper_bound) == values::True()) ||
                     (value.SqlEquals(upper_bound) == values::True());
          break;
        }
        case ColumnFilter::kInList:
          keep_row = false;
          for (const Value& element : filter->in_list()) {
            if (value.SqlEquals(element) == values::True()) {
              keep_row = true;
              break;
            }
          }
          break;
        default:
          // Skip this unknown column filter.
          keep_row = true;
          break;
      }
    }

    if (keep_row) return true;
  }

  return false;
}

}  // namespace googlesql
