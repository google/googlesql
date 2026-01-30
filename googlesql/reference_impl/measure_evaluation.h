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

#ifndef GOOGLESQL_REFERENCE_IMPL_MEASURE_EVALUATION_H_
#define GOOGLESQL_REFERENCE_IMPL_MEASURE_EVALUATION_H_

#include <string>

#include "googlesql/public/catalog.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Maintains the mapping between a MEASURE-typed ResolvedColumn and the
// expression used to compute the measure. The expression used to compute the
// measure is stored in the catalog (not the ResolvedColumn or the type), hence
// why we need to track it here.
class MeasureColumnToExprMapping {
 public:
  MeasureColumnToExprMapping() = default;
  // Disallow copy, but allow move.
  MeasureColumnToExprMapping(const MeasureColumnToExprMapping& other) = delete;
  MeasureColumnToExprMapping& operator=(
      const MeasureColumnToExprMapping& other) = delete;
  MeasureColumnToExprMapping(MeasureColumnToExprMapping&& other) = default;
  MeasureColumnToExprMapping& operator=(MeasureColumnToExprMapping&& other) =
      default;

  // Track expressions for any measure columns emitted by `table_scan`.
  absl::Status TrackMeasureColumnsEmittedByTableScan(
      const ResolvedTableScan& table_scan);

  // Track the with query scan for the given `with_entry`. This is used to
  // correctly track measure expressions for measure columns that propagate past
  // ResolvedWithRefScans.
  void TrackWithQueryScan(const ResolvedWithEntry& with_entry);

  // Resolved measure columns may be renamed with a different column id when
  // propagating past certain types of scans.  (e.g. ResolvedWithRefScans). This
  // method associates renamed measure columns emitted by `with_ref_scan` with
  // the measure expression currently associated with measure columns emitted by
  // the corresponding ResolvedWithEntry.
  absl::Status TrackMeasureColumnsRenamedByWithRefScan(
      const ResolvedWithRefScan& with_ref_scan);

  // Track expressions for any measure columns emitted by `tvf_scan`.
  absl::Status TrackMeasureColumnsEmittedByTVFScan(
      const ResolvedTVFScan& tvf_scan);

  // Tracks a measure column that is renamed by the given `resolved_expr`.
  // If `renamed_column` is not a measure column, this method is a no-op.
  absl::Status TrackMeasureColumnsRenamedByExpr(
      const ResolvedColumn& renamed_column, const ResolvedExpr& resolved_expr);

  // Find the measure expression for the given `column`.
  absl::StatusOr<const ResolvedExpr*> GetMeasureExpr(
      const ResolvedColumn& column) const;

 private:
  // Tracks measure columns emitted by `scan`.
  template <typename ScanType>
  absl::Status TrackMeasureColumnsEmittedByScan(const ScanType& scan);

  // Adds a MEASURE-typed `column` with the given `expr`.
  absl::Status AddMeasureColumnWithExpr(const ResolvedColumn& column,
                                        const ResolvedExpr* expr);

  // Resolved measure columns may be renamed with a different column id when
  // referenced by a computed column ref. This method associates the renamed
  // measure column with the measure expression currently associated with the
  // original measure column.
  absl::Status MapOriginalMeasureExprToRenamedColumn(
      const ResolvedColumn& renamed_column,
      const ResolvedColumn& original_column);

  absl::flat_hash_map<ResolvedColumn, const ResolvedExpr*>
      measure_column_to_expr_;
  // A mapping from the name of a with query to the ResolvedScan node
  // containing the with query.
  absl::flat_hash_map<std::string, const ResolvedScan*>
      with_query_name_to_scan_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_MEASURE_EVALUATION_H_
