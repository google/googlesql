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

// A simple implementation of an EvaluatorTableIterator that just stores
// everything in memory.

#ifndef GOOGLESQL_COMMON_SIMPLE_EVALUATOR_TABLE_ITERATOR_H_
#define GOOGLESQL_COMMON_SIMPLE_EVALUATOR_TABLE_ITERATOR_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/clock.h"

namespace googlesql {

class SimpleEvaluatorTableIterator : public EvaluatorTableIterator {
 public:
  // 'columns' is a list of the columns in the scan.
  // '(*column_major_values[i])[j]' is the values of 'columns[j]' in the
  // i-th row.
  // 'end_status' is the absl::Status to return when we have reached the end
  //  of 'column_major_values'.
  // 'filter_column_idxs' is the list of column indexes for which to enforce the
  // filters passed to SetColumnFilters().
  // 'cancel_cb' is called when Cancel() is called.
  // 'set_deadline_cb' is called when SetDeadline() is called.
  // 'clock' is used to enforce deadlines.
  SimpleEvaluatorTableIterator(
      absl::Span<const Column* const> columns,
      const std::vector<std::shared_ptr<const std::vector<Value>>>&
          column_major_values,
      int64_t num_rows, const absl::Status& end_status,
      const absl::flat_hash_set<int>& filter_column_idxs,
      const std::function<void()>& cancel_cb,
      const std::function<void(absl::Time)>& set_deadline_cb,
      googlesql_base::Clock* clock)
      : columns_(columns.begin(), columns.end()),
        end_status_(end_status),
        filter_column_idxs_(filter_column_idxs),
        cancel_cb_(cancel_cb),
        set_deadline_cb_(set_deadline_cb),
        column_major_values_(column_major_values),
        num_rows_(num_rows),
        clock_(clock) {
    ABSL_CHECK_EQ(columns.size(), column_major_values_.size());
    for (const auto& values_for_column : column_major_values_) {
      ABSL_CHECK_EQ(num_rows_, values_for_column->size());
    }
  }

  SimpleEvaluatorTableIterator(const SimpleEvaluatorTableIterator&) = delete;
  SimpleEvaluatorTableIterator& operator=(const SimpleEvaluatorTableIterator&) =
      delete;

  int NumColumns() const override { return columns_.size(); }

  std::string GetColumnName(int i) const override {
    return columns_[i]->Name();
  }

  const Type* GetColumnType(int i) const override {
    return columns_[i]->GetType();
  }

  absl::Status SetColumnFilterMap(
      absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map)
      override;

  bool NextRow() override;

  const Value& GetValue(int i) const override {
    absl::ReaderMutexLock l(&mutex_);
    return (*column_major_values_[i])[row_idx_];
  }

  absl::Status Status() const override {
    absl::ReaderMutexLock l(&mutex_);
    if (cancelled_) {
      return googlesql_base::CancelledErrorBuilder()
             << "EvaluatorTestTableIterator was cancelled";
    }
    if (deadline_exceeded_) {
      return googlesql_base::DeadlineExceededErrorBuilder()
             << "EvaluatorTestTableIterator deadline exceeded";
    }
    if (DoneLocked()) return end_status_;
    return absl::OkStatus();
  }

  absl::Status Cancel() override {
    absl::MutexLock l(&mutex_);
    cancelled_ = true;
    cancel_cb_();
    return absl::OkStatus();
  }

  void SetDeadline(absl::Time deadline) override {
    absl::MutexLock l(&mutex_);
    deadline_ = deadline;
    set_deadline_cb_(deadline);
  }

 private:
  bool DoneLocked() const ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    if (column_major_values_.empty()) return true;
    return row_idx_ >= num_rows_;
  }

  const std::vector<const Column*> columns_;
  const absl::Status end_status_;
  const absl::flat_hash_set<int> filter_column_idxs_;
  const std::function<void()> cancel_cb_;
  const std::function<void(absl::Time)> set_deadline_cb_;

  mutable absl::Mutex mutex_;

  std::vector<std::shared_ptr<const std::vector<Value>>> column_major_values_
      ABSL_GUARDED_BY(mutex_);
  int64_t num_rows_ ABSL_GUARDED_BY(mutex_);

  int64_t row_idx_ ABSL_GUARDED_BY(mutex_) = -1;
  bool cancelled_ ABSL_GUARDED_BY(mutex_) = false;
  bool deadline_exceeded_ ABSL_GUARDED_BY(mutex_) = false;
  absl::Time deadline_ ABSL_GUARDED_BY(mutex_) = absl::InfiniteFuture();
  googlesql_base::Clock* clock_ ABSL_GUARDED_BY(mutex_) ABSL_PT_GUARDED_BY(mutex_);

  // Contains the entries passed to 'filter_map' that are in
  // 'filter_column_idxs_'.
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_SIMPLE_EVALUATOR_TABLE_ITERATOR_H_
