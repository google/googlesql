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

#include "googlesql/reference_impl/measure_evaluation.h"

#include <utility>

#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
// Provides unified access to the table schema for scans that can return
// measure columns.
template <typename T>
struct ScanTraits;

template <>
struct ScanTraits<ResolvedTableScan> {
  static const Table* GetTable(const ResolvedTableScan& scan) {
    return scan.table();
  }
};

template <>
struct ScanTraits<ResolvedTVFScan> {
  static const Table* GetTable(const ResolvedTVFScan& scan) {
    return scan.signature()->result_table_schema();
  }
};
}  // namespace

template <typename ScanType>
absl::Status MeasureColumnToExprMapping::TrackMeasureColumnsEmittedByScan(
    const ScanType& scan) {
  // If there are no measure columns emitted by the table scan, we can skip this
  // method. We deliberately do not check `column_index_list` at this point
  // because there are many legacy cases where it is not populated.
  if (!absl::c_any_of(scan.column_list(), [](const ResolvedColumn& column) {
        return column.type()->IsMeasureType();
      })) {
    return absl::OkStatus();
  }
  // If here, we know that there are measure columns emitted by the table scan.
  // `column_index_list` must be populated.
  GOOGLESQL_RET_CHECK_EQ(scan.column_list_size(), scan.column_index_list_size());
  for (int idx = 0; idx < scan.column_list_size(); ++idx) {
    const ResolvedColumn& resolved_column = scan.column_list(idx);
    if (resolved_column.type()->IsMeasureType()) {
      const int table_column_index = scan.column_index_list(idx);
      const Table* table = ScanTraits<ScanType>::GetTable(scan);
      GOOGLESQL_RET_CHECK(table != nullptr);
      const Column* column = table->GetColumn(table_column_index);
      GOOGLESQL_RET_CHECK(column->HasMeasureExpression() &&
                column->GetExpression()->HasResolvedExpression());
      const ResolvedExpr* measure_expr =
          column->GetExpression()->GetResolvedExpression();
      GOOGLESQL_RETURN_IF_ERROR(AddMeasureColumnWithExpr(resolved_column, measure_expr));
    }
  }
  return absl::OkStatus();
}

absl::Status MeasureColumnToExprMapping::TrackMeasureColumnsEmittedByTableScan(
    const ResolvedTableScan& table_scan) {
  return TrackMeasureColumnsEmittedByScan(table_scan);
}

void MeasureColumnToExprMapping::TrackWithQueryScan(
    const ResolvedWithEntry& with_entry) {
  with_query_name_to_scan_[with_entry.with_query_name()] =
      with_entry.with_subquery();
}

absl::Status
MeasureColumnToExprMapping::TrackMeasureColumnsRenamedByWithRefScan(
    const ResolvedWithRefScan& with_ref_scan) {
  auto it = with_query_name_to_scan_.find(with_ref_scan.with_query_name());
  GOOGLESQL_RET_CHECK(it != with_query_name_to_scan_.end());
  const ResolvedScan& with_subquery_scan = *it->second;
  GOOGLESQL_RET_CHECK_EQ(with_ref_scan.column_list_size(),
               with_subquery_scan.column_list_size());
  for (int idx = 0; idx < with_ref_scan.column_list_size(); ++idx) {
    const ResolvedColumn& renamed_column = with_ref_scan.column_list(idx);
    const ResolvedColumn& original_column = with_subquery_scan.column_list(idx);
    GOOGLESQL_RET_CHECK(original_column.type()->Equals(renamed_column.type()));
    if (original_column.type()->IsMeasureType()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* measure_expr,
                       GetMeasureExpr(original_column));
      GOOGLESQL_RETURN_IF_ERROR(AddMeasureColumnWithExpr(renamed_column, measure_expr));
    }
  }
  return absl::OkStatus();
}

absl::Status MeasureColumnToExprMapping::TrackMeasureColumnsEmittedByTVFScan(
    const ResolvedTVFScan& tvf_scan) {
  return TrackMeasureColumnsEmittedByScan(tvf_scan);
}

absl::Status MeasureColumnToExprMapping::MapOriginalMeasureExprToRenamedColumn(
    const ResolvedColumn& renamed_column,
    const ResolvedColumn& original_column) {
  if (!renamed_column.type()->IsMeasureType()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(renamed_column.type()->IsMeasureType());
  GOOGLESQL_RET_CHECK(renamed_column.type()->Equals(original_column.type()));
  GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* measure_expr,
                   GetMeasureExpr(original_column));
  return AddMeasureColumnWithExpr(renamed_column, measure_expr);
}

absl::Status MeasureColumnToExprMapping::TrackMeasureColumnsRenamedByExpr(
    const ResolvedColumn& renamed_column, const ResolvedExpr& resolved_expr) {
  if (!renamed_column.type()->IsMeasureType()) {
    return absl::OkStatus();
  }

  switch (resolved_expr.node_kind()) {
    case RESOLVED_COLUMN_REF:
      return MapOriginalMeasureExprToRenamedColumn(
          renamed_column, resolved_expr.GetAs<ResolvedColumnRef>()->column());
    case RESOLVED_SUBQUERY_EXPR: {
      const ResolvedSubqueryExpr& subquery_expr =
          *resolved_expr.GetAs<ResolvedSubqueryExpr>();
      if (subquery_expr.subquery_type() == ResolvedSubqueryExpr::SCALAR) {
        GOOGLESQL_RET_CHECK_EQ(subquery_expr.subquery()->column_list_size(), 1);
        return MapOriginalMeasureExprToRenamedColumn(
            renamed_column, subquery_expr.subquery()->column_list(0));
      }
      break;
    }
    case RESOLVED_WITH_EXPR: {
      return TrackMeasureColumnsRenamedByExpr(
          renamed_column, *resolved_expr.GetAs<ResolvedWithExpr>()->expr());
    }
    default:
      break;
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Unexpected measure column: "
                   << renamed_column.DebugString();
}

absl::StatusOr<const ResolvedExpr*> MeasureColumnToExprMapping::GetMeasureExpr(
    const ResolvedColumn& column) const {
  if (auto it = measure_column_to_expr_.find(column);
      it != measure_column_to_expr_.end()) {
    return it->second;
  }
  return absl::NotFoundError(
      absl::StrCat("Column not found: ", column.DebugString()));
}

absl::Status MeasureColumnToExprMapping::AddMeasureColumnWithExpr(
    const ResolvedColumn& column, const ResolvedExpr* expr) {
  GOOGLESQL_RET_CHECK(column.type()->IsMeasureType());
  GOOGLESQL_RET_CHECK(expr != nullptr);
  auto [it, inserted] = measure_column_to_expr_.insert({column, expr});
  if (inserted) {
    return absl::OkStatus();
  }
  // If inserting the same column twice, we must be tracking the same
  // expression.
  GOOGLESQL_RET_CHECK_EQ(it->second, expr);
  return absl::OkStatus();
}

}  // namespace googlesql
