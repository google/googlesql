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

// API for providing a table to the GoogleSQL Evaluator (see evaluator.h).

#ifndef GOOGLESQL_PUBLIC_EVALUATOR_TABLE_ITERATOR_H_
#define GOOGLESQL_PUBLIC_EVALUATOR_TABLE_ITERATOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/public/value.h"
#include "googlesql/base/status.h"

namespace googlesql {

class ColumnFilter;

// Iterator interface for a user-supplied table in a PreparedQuery.
//
// Example:
//   Table* table = ... Get table from catalog ...
//   GOOGLESQL_ASSIGN_OR_RETURN(
//     std::unique_ptr<EvaluatorTableIterator> iter,
//     table->CreateEvaluatorTableIterator(column_names));
//   while (true) {
//     if (!iter->NextRow()) {
//       GOOGLESQL_RETURN_IF_ERROR(iter->Status());
//       break;
//     }
//     ... Do something with 'iter->GetValue(...)' ...
//   }
class EvaluatorTableIterator {
 public:
  virtual ~EvaluatorTableIterator() = default;

  // Returns the number of columns returned by the iterator.
  virtual int NumColumns() const = 0;

  // Returns the name of the i-th column. Anonymous columns have empty
  // names. There may be more than one column with the same name. 'i' must be in
  // [0, 'NumColumns()').
  virtual std::string GetColumnName(int i) const = 0;

  // Returns the type of the i-th column. 'i' must be in [0, 'NumColumns()').
  virtual const Type* GetColumnType(int i) const = 0;

  // This method is called just before the first call to NextRow() to indicate
  // that the iterator may skip any rows for a column that do not match
  // 'filter_map'. This filtering is optional and best-effort. The evaluator
  // will re-apply filtering on all returning rows. The purpose of this method
  // is simply to facilitate the optimization of skipping rows that definitely
  // won't affect the result of the query.
  //
  // This method should return quickly. All non-trivial processing should be
  // done by NextRow().
  //
  // One implementation strategy is to store the table sorted by some column,
  // then translate the filters into a key range that can be easily scanned.
  // Another strategy is to store the table in a hash map keyed by column, then
  // discard the range filters and use the kInList filters to determine the hash
  // keys that are used.
  //
  // Here are some examples of WHERE clauses where the evaluator API calls
  // SetColumnFilters() in a useful way.
  //
  //   - WHERE Column = 10
  //
  //   - WHERE Column > 10 (similarly for >=, <, and <=)
  //
  //   - WHERE Column BETWEEN 10 AND 20
  //
  //   - WHERE Column > 10 AND Column < 20
  //
  //   - WHERE Column IN (10, 15, 20)
  //
  //   - WHERE Column IN UNNEST([10, 15, 20])
  //
  //   - WHERE Column1 = 10 AND (Column2 BETWEEN 100 AND 200)
  //
  // Note that the algebrizer is not able to express ORs or NOTs through this
  // API, so queries may have to be rewritten for performance reasons if that
  // proves to be important.
  //
  // The evaluator implements this functionality with a very limited form of
  // filter pushdown that only supports pushdown through FilterScans,
  // ProjectScans, and JoinScans, and does not support filter inferencing. (The
  // evaluator does not have a real optimizer.) Thus, to leverage this feature,
  // it is often necessary to ensure the WHERE clause is very close to the
  // corresponding table scan. Here are some examples that will work with this
  // feature:
  //
  //   - SELECT * FROM Table WHERE Column > 10
  //
  //   - SELECT * FROM Table1, Table2
  //     WHERE Table1.Key = Table2.Key AND Table1.Value > 10
  //
  // Here are examples that must be manually rewritten to leverage this feature:
  //
  //       Query: SELECT *
  //              FROM ((SELECT * FROM Table) UNION ALL (SELECT* FROM TABLE))
  //              WHERE Column > 10
  //
  //     Rewrite: SELECT *
  //              FROM (SELECT * FROM Table WHERE Column > 10 UNION ALL
  //                    SELECT * FROM Table WHERE Column > 10)
  //
  //       Query: SELECT * FROM Table1, Table2
  //              WHERE Table1.Key = Table2.Key AND Table1.Key > 10
  //
  //     Rewrite: SELECT * FROM Table1, Table2
  //              WHERE Table1.Key = Table2.Key AND
  //                    Table1.Key > 10 AND Table2.Key > 10
  //
  // 'filter_map' contains the ColumnFilters to apply. It is keyed on the index
  // of a column in the scan (not the Table).
  virtual absl::Status SetColumnFilterMap(
      absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map) {
    return absl::OkStatus();
  }

  // Indicates that the iterator should read from a snapshot of the table at the
  // given moment in time, rather than the current table content. This function
  // must be called prior to the first call to NextRow().
  //
  // This function should return InvalidArgumentError if the table is unreadable
  // at 'read_time'.
  virtual absl::Status SetReadTime(absl::Time read_time) {
    return absl::UnimplementedError(
        "EvaluatorTableIterator::SetReadTime() not implemented");
  }

  // Returns false if there is no next row. The caller must then check
  // 'Status()'. If NextRow() returns false, the only allowed operations on this
  // iterator are NumColumns(), GetColumnName(), GetColumnType(), and Status().
  virtual bool NextRow() = 0;

  // Returns the value of the i-th column of this iterator.
  // 'i' must be in [0, 'NumColumns()').
  // NextRow() must have been called at least once and the last call must have
  // returned true.
  virtual const Value& GetValue(int i) const = 0;

  // Returns OK unless the last call to NextRow() returned false because of an
  // error (including cancellation).
  virtual absl::Status Status() const = 0;

  // Best-effort cancellation that can be called from any thread. This must not
  // block for a long time.  The implementation could set a cancel bit and then
  // check for that inside processing loops or in NextRow().  The returned
  // Status does not necessarily indicate that the Cancel succeeded or failed,
  // but rather is just used for logging and can have
  // implementation-specific meaning.
  virtual absl::Status Cancel() = 0;

  // Best-effort deadline support. This may not be called after the first call
  // to NextRow(). This must not block for a long time. The implementation could
  // set a deadline member and check for its expiration inside processing loops
  // or in NextRow().
  virtual void SetDeadline(absl::Time deadline) {}

  // Gets the current memory usage of this iterator in bytes. Returns
  // std::nullopt if memory usage tracking is not supported.
  virtual std::optional<int64_t> GetCurrentMemoryUsage() const {
    return std::nullopt;
  }

  // Gets the peak memory usage of this iterator in bytes. Returns std::nullopt
  // if memory usage tracking is not supported.
  virtual std::optional<int64_t> GetPeakMemoryUsage() const {
    return std::nullopt;
  }
};

// Represents a restriction of values needed by a scan for a particular
// column. For example, a kRange ColumnFilter with boundaries [10, 15]
// represents that the scan can omit any rows whose value for that column is
// less than 10 or greater than 15. The EvaluatorTableIterator implementation
// can use this information to avoid returning rows that will not affect the
// result of the query.
//
// For all filters expressed with this class:
// - Comparisons are done with SQL semantics. (Comparisons with NULLs or NaNs
//   cannot return true.) However, any filter expressed with this class can
//   never match a NULL or NaN value, so the implementer can skip all such
//   values.
// - Comparisons are allowed between types that are directly comparable with =
//   or < operators without even implicit casts. For example, comparison between
//   int64 and uint64 is allowed.
// Value::SqlLessThan() and Value::SqlEquals() implement comparison with these
// semantics.
class ColumnFilter {
 public:
  enum Kind {
    // Represents a range of non-NULL/non-NaN values.
    kRange,
    // Represents a list of non-NULL/non-NaN values.
    kInList,
    // Switches must have a default case to allow us to add more kinds of
    // ValueFilters to the API.
    __Kind__switches_must_have_a_default
  };

  // Constructs a kRange ColumnFilter. 'lower_bound' and 'upper_bound' may each
  // be invalid (i.e., Valid::is_valid() returns false) to represent +/-
  // infinity. They will never be NULL or NaN. If both are valid, 'lower_bound'
  // must be at most 'upper_bound' in the SQL sense.
  ColumnFilter(const Value& lower_bound, const Value& upper_bound)
      : kind_(kRange), values_({lower_bound, upper_bound}) {}

  // Constructs a kInList ColumnFilter. The elements of 'in_list' must be
  // non-NULL and non-NaN. 'in_list' may be empty to represent a filter that
  // drops all rows.
  explicit ColumnFilter(absl::Span<const Value> in_list)
      : kind_(kInList), values_(in_list.begin(), in_list.end()) {}

  Kind kind() const { return kind_; }

  // Returns the range boundaries of this filter. 'kind()' must be kRange. The
  // lower bound and upper bound are non-NULL and non-NaN and the lower bound is
  // at most the upper bound in the SQL sense.
  const Value& lower_bound() const { return values_[0]; }
  const Value& upper_bound() const { return values_[1]; }

  // Returns the in list for this filter, which must have 'kind()' kInList. No
  // elements of this list are NULL or NaN.
  const std::vector<Value>& in_list() const { return values_; }

 private:
  Kind kind_;
  // If 'kind_ == kRange', this has two elements, the lower bound and the upper
  // bound.
  std::vector<Value> values_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_EVALUATOR_TABLE_ITERATOR_H_
