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

#include "googlesql/analyzer/rewriters/measure_source_scan_rewrite_util.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/rewriters/measure_collector.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using NameToResolvedColumn =
    absl::flat_hash_map<std::string, ResolvedColumn,
                        googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;
using CaseInsensitiveStringSet =
    absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;

static constexpr char kReferencedColumnsFieldName[] = "referenced_columns";
static constexpr char kKeyColumnsFieldName[] = "key_columns";

// Provides scan-type-specific information for measure source scans.
template <typename ScanType>
struct MeasureSourceTraits {};

template <>
struct MeasureSourceTraits<ResolvedTableScan> {
  static const Table* GetTable(const ResolvedTableScan* scan) {
    return scan->table();
  }
};

template <>
struct MeasureSourceTraits<ResolvedTVFScan> {
  static const Table* GetTable(const ResolvedTVFScan* scan) {
    return scan->signature()->result_table_schema();
  }
};

// Wraps the `referenced_columns_struct_expr` and `key_columns_struct_expr`
// with a STRUCT<referenced_columns STRUCT<...>, key_columns STRUCT<...>.
static absl::StatusOr<std::unique_ptr<ResolvedMakeStruct>> MakeWrappingStruct(
    std::unique_ptr<ResolvedExpr> referenced_columns_struct_expr,
    std::unique_ptr<ResolvedExpr> key_columns_struct_expr,
    TypeFactory& type_factory) {
  std::vector<StructField> final_struct_fields;
  final_struct_fields.push_back(StructField(
      kReferencedColumnsFieldName, referenced_columns_struct_expr->type()));
  final_struct_fields.push_back(
      StructField(kKeyColumnsFieldName, key_columns_struct_expr->type()));
  const StructType* final_struct_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructTypeFromVector(final_struct_fields,
                                                        &final_struct_type));
  std::vector<std::unique_ptr<const ResolvedExpr>> final_struct_field_exprs;
  final_struct_field_exprs.push_back(std::move(referenced_columns_struct_expr));
  final_struct_field_exprs.push_back(std::move(key_columns_struct_expr));
  return MakeResolvedMakeStruct(final_struct_type,
                                std::move(final_struct_field_exprs));
}

class ExpressionColumnNameCollector : public ResolvedASTVisitor {
 public:
  static absl::StatusOr<CaseInsensitiveStringSet> GetExpressionColumnNames(
      const ResolvedExpr* expr) {
    ExpressionColumnNameCollector collector;
    GOOGLESQL_RETURN_IF_ERROR(expr->Accept(&collector));
    return collector.column_names_;
  }

  absl::Status VisitResolvedExpressionColumn(
      const ResolvedExpressionColumn* node) override {
    column_names_.insert(node->name());
    return absl::OkStatus();
  }

 private:
  CaseInsensitiveStringSet column_names_;
};

// Used to build the `referenced_columns` and `key_columns` structs when
// building the closure struct in BuildClosureColumn().
static absl::StatusOr<std::unique_ptr<ResolvedMakeStruct>>
MakeStructFromColumnListWithCustomNames(
    absl::Span<const std::pair<std::string, ResolvedColumn>> named_columns,
    TypeFactory& type_factory) {
  std::vector<StructField> struct_fields;
  std::vector<std::unique_ptr<const ResolvedExpr>> struct_field_exprs;
  for (const auto& [name, column] : named_columns) {
    struct_fields.push_back(StructField(name, column.type()));
    struct_field_exprs.push_back(
        MakeResolvedColumnRef(column.type(), column,
                              // `is_correlated` is always false because these
                              // columns are from the source scan.
                              /*is_correlated=*/false));
  }
  const StructType* struct_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory.MakeStructTypeFromVector(struct_fields, &struct_type));
  return MakeResolvedMakeStruct(struct_type, std::move(struct_field_exprs));
}

// Holds information about a measure source column.
struct MeasureSourceInfo {
  // The measure column from table scan's column_list.
  ResolvedColumn measure_col;

  // The measure expression of `measure_col` from catalog.
  const ResolvedExpr* measure_expr;

  // The set of column names referenced in `measure_expr`.
  CaseInsensitiveStringSet referenced_column_names;

  // The row identity column indices for this measure source column.
  std::vector<int> row_identity_column_indices;
};

// Rewrites a ResolvedTableScan or ResolvedTVFScan if it contains AGG'ed
// measure source columns.
//
// If measure columns are present on the scan, this class:
// 1. Builds a closure column, which is a STRUCT containing:
//    - referenced_columns: a STRUCT of columns referenced by any measure
//      expression on the scan.
//    - key_columns: a STRUCT of row identity columns of the table.
// 2. Creates a ProjectScan on top of the input scan to project this closure
//    column.
// 3. Removes measure columns that have measure expressions from scan's column
//    list, and adds any columns referenced by measure expressions but not
//    present in scan's column list to the scan.
// 4. Registers measure definitions with `measure_collector_` for later rewrite
//    of ResolvedMeasureReference.
template <typename ScanType>
class MeasureSourceColumnReplacer {
 public:
  MeasureSourceColumnReplacer(std::unique_ptr<const ScanType> scan,
                              MeasureCollector& measure_collector,
                              TypeFactory& type_factory,
                              ColumnFactory& column_factory)
      : scan_(std::move(scan)),
        measure_collector_(measure_collector),
        type_factory_(type_factory),
        column_factory_(column_factory) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Replace() {
    // Step 1: Collect measure column information.
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<MeasureSourceInfo> measure_infos,
                     CollectMeasureInfos());
    if (measure_infos.empty()) {
      // No measure definitions are found, nothing to rewrite.
      return std::move(scan_);
    }
    // Step 2: Build the closure struct.
    NameToResolvedColumn missing_columns_from_scan;
    absl::flat_hash_set<ResolvedColumn> measure_cols_with_expr_set;
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedComputedColumn> closure,
        BuildClosureColumn(measure_infos, missing_columns_from_scan,
                           measure_cols_with_expr_set));
    // Step 3: Store the relevant information for each measure definition.
    const Table* table = GetTable();
    for (const auto& info : measure_infos) {
      GOOGLESQL_RET_CHECK(info.measure_col.type()->IsMeasureType());
      absl::btree_set<std::string, googlesql_base::CaseLess>
          row_identity_column_names;
      for (const int index : info.row_identity_column_indices) {
        const std::string column_name = table->GetColumn(index)->Name();
        GOOGLESQL_RET_CHECK(row_identity_column_names.insert(column_name).second)
            << "Duplicate row identity column name: " << column_name;
      }
      GOOGLESQL_RETURN_IF_ERROR(measure_collector_.AddMeasureInfo(
          info.measure_col.type()->AsMeasure(),
          {.measure_expr = info.measure_expr,
           .closure_struct = closure->column(),
           .row_identity_column_names = std::move(row_identity_column_names),
           .measure_source_column = info.measure_col}));
    }

    // Step 4: Add a ProjectScan to project the closure column.
    return RebuildScanAndCreateProjectScan(measure_cols_with_expr_set,
                                           missing_columns_from_scan,
                                           std::move(closure));
  }

 private:
  const Table* GetTable() const {
    const Table* table = MeasureSourceTraits<ScanType>::GetTable(scan_.get());
    ABSL_DCHECK(table != nullptr);
    return table;
  }

  absl::StatusOr<std::vector<MeasureSourceInfo>> CollectMeasureInfos() {
    std::vector<MeasureSourceInfo> measure_infos;
    for (int i = 0; i < scan_->column_list_size(); ++i) {
      const ResolvedColumn& col = scan_->column_list(i);
      if (!col.type()->IsMeasureType()) {
        continue;
      }
      if (!measure_collector_.IsAgged(col.type()->AsMeasure())) {
        continue;
      }
      const int col_idx_in_table = scan_->column_index_list(i);
      const Table* table = GetTable();
      const Column* catalog_column = table->GetColumn(col_idx_in_table);
      GOOGLESQL_RET_CHECK(catalog_column->HasMeasureExpression() &&
                catalog_column->GetExpression()->HasResolvedExpression());

      const ResolvedExpr* measure_expr =
          catalog_column->GetExpression()->GetResolvedExpression();

      GOOGLESQL_ASSIGN_OR_RETURN(CaseInsensitiveStringSet referenced_column_names,
                       ExpressionColumnNameCollector::GetExpressionColumnNames(
                           measure_expr));

      std::vector<int> row_identity_column_indices;
      if (std::optional<std::vector<int>> column_level_row_identity_columns =
              catalog_column->GetExpression()->RowIdentityColumns();
          column_level_row_identity_columns.has_value()) {
        row_identity_column_indices =
            *std::move(column_level_row_identity_columns);
      } else {
        row_identity_column_indices =
            table->RowIdentityColumns().value_or(std::vector<int>{});
      }
      GOOGLESQL_RET_CHECK(!row_identity_column_indices.empty());
      absl::c_sort(row_identity_column_indices);

      measure_infos.push_back({
          .measure_col = col,
          .measure_expr = measure_expr,
          .referenced_column_names = referenced_column_names,
          .row_identity_column_indices = row_identity_column_indices,
      });
    }
    return measure_infos;
  }

  // Builds and returns a ResolvedComputedColumn representing the closure
  // for all measure columns on the scan. The expression is a struct that
  // contains all columns referenced by measures defined in `measure_infos`, as
  // well as row identity columns, i.e.,
  //
  // STRUCT(
  //   referenced_columns: STRUCT(
  //     <column_name>: ResolvedColumn,
  //     ...
  //   ),
  //   key_columns: STRUCT(
  //     <column index>: ResolvedColumn,
  //     ...
  //   )
  // )
  //
  // Populates `missing_columns_from_scan` with columns that are needed for
  // building the closure but are not present in `scan_`. Populates
  // `measure_cols_with_expr_set` with measure columns that have measure
  // expressions.
  absl::StatusOr<std::unique_ptr<ResolvedComputedColumn>> BuildClosureColumn(
      absl::Span<const MeasureSourceInfo> measure_infos,
      NameToResolvedColumn& missing_columns_from_scan,
      absl::flat_hash_set<ResolvedColumn>& measure_cols_with_expr_set) {
    // Step 1: Aggregates all `referenced_column_names` and
    // `row_identity_column_indices` across all measures so that we have one
    // closure struct per scan instead of per measure column.

    // Column names in ResolvedExpressionColumn are always lowercase, so we
    // use case-insensitive comparison.
    absl::btree_set<std::string, googlesql_base::CaseLess>
        all_referenced_column_names;
    absl::btree_set<int> all_row_identity_column_indices;

    for (const auto& info : measure_infos) {
      measure_cols_with_expr_set.insert(info.measure_col);
      all_referenced_column_names.insert(info.referenced_column_names.begin(),
                                         info.referenced_column_names.end());
      all_row_identity_column_indices.insert(
          info.row_identity_column_indices.begin(),
          info.row_identity_column_indices.end());
    }

    // Step 2: Create a substruct that contains all referenced columns where
    // the field names are column names.
    std::vector<std::pair<std::string, ResolvedColumn>> referenced_columns;
    const Table* table = GetTable();
    for (int table_col_idx = 0; table_col_idx < table->NumColumns();
         ++table_col_idx) {
      const Column* column = table->GetColumn(table_col_idx);
      if (all_referenced_column_names.contains(column->Name())) {
        referenced_columns.push_back(
            {column->Name(),
             GetOrProjectColumn(table_col_idx, missing_columns_from_scan)});
      }
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedMakeStruct> referenced_columns_struct,
        MakeStructFromColumnListWithCustomNames(referenced_columns,
                                                type_factory_));

    // Step 3: Create a substruct that contains all row identity columns where
    // the field names are column names.
    std::vector<std::pair<std::string, ResolvedColumn>> row_identity_columns;
    row_identity_columns.reserve(all_row_identity_column_indices.size());
    for (int row_id_col_idx : all_row_identity_column_indices) {
      row_identity_columns.push_back(
          {table->GetColumn(row_id_col_idx)->Name(),
           GetOrProjectColumn(row_id_col_idx, missing_columns_from_scan)});
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedMakeStruct> key_columns_struct,
                     MakeStructFromColumnListWithCustomNames(
                         row_identity_columns, type_factory_));

    // Step 4: Create the final struct that wraps the referenced columns struct
    // and the row identity columns struct.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedExpr> closure_expr,
        MakeWrappingStruct(std::move(referenced_columns_struct),
                           std::move(key_columns_struct), type_factory_));

    const std::string closure_column_name =
        absl::StrCat("struct_for_measures_from_table_", table->Name());
    ResolvedColumn closure_column = column_factory_.MakeCol(
        table->Name(), closure_column_name, closure_expr->type());
    return MakeResolvedComputedColumn(closure_column, std::move(closure_expr));
  }

  // Rebuilds `scan_` to remove measure columns and add columns in
  // `missing_columns_from_scan`. Then, creates a ProjectScan on top of
  // `scan_` which adds `closure` to the column list.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  RebuildScanAndCreateProjectScan(
      const absl::flat_hash_set<ResolvedColumn>& measure_cols_with_expr_set,
      const NameToResolvedColumn& missing_columns_from_scan,
      std::unique_ptr<ResolvedComputedColumn> closure) {
    // Step 1: Remove the AGG'ed measure columns from the source_scan - they
    // are replaced by the struct closure column on this scan.
    std::vector<std::pair<int, ResolvedColumn>> indexed_columns;
    ResolvedColumnList project_column_list;

    for (int i = 0; i < scan_->column_list_size(); ++i) {
      if (!measure_cols_with_expr_set.contains(scan_->column_list(i))) {
        indexed_columns.push_back(
            {scan_->column_index_list(i), scan_->column_list(i)});
        project_column_list.push_back(scan_->column_list(i));
      }
    }

    // Step 2: Add columns in `missing_columns_from_scan` to the
    // source_scan. These columns are referenced in measure expressions but
    // not in scan_'s column list.
    const Table* table = GetTable();
    for (int i = 0; i < table->NumColumns(); ++i) {
      const Column* column = table->GetColumn(i);
      if (missing_columns_from_scan.contains(column->Name())) {
        indexed_columns.push_back(
            {i, missing_columns_from_scan.at(column->Name())});
      }
    }

    GOOGLESQL_RETURN_IF_ERROR(RebuildScanColumns(indexed_columns));

    // Step 3: Build ProjectScan to add `closure`.
    project_column_list.push_back(closure->column());
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> project_exprs;
    project_exprs.push_back(std::move(closure));

    return MakeResolvedProjectScan(project_column_list,
                                   std::move(project_exprs), std::move(scan_));
  }

  // Gets the ResolvedColumn corresponding to `table_col_idx`. If it is not
  // present in `scan_->column_list()`, creates a new ResolvedColumn for it
  // and adds it to scan_->column_list() first.
  ResolvedColumn GetOrProjectColumn(
      int table_col_idx, NameToResolvedColumn& missing_columns_from_scan) {
    for (int i = 0; i < scan_->column_index_list_size(); ++i) {
      if (scan_->column_index_list(i) == table_col_idx) {
        return scan_->column_list(i);
      }
    }
    const Table* table = GetTable();
    const Column* column = table->GetColumn(table_col_idx);
    if (missing_columns_from_scan.contains(column->Name())) {
      return missing_columns_from_scan.at(column->Name());
    }
    ResolvedColumn new_col = column_factory_.MakeCol(
        table->Name(), column->Name(), column->GetType());
    missing_columns_from_scan[column->Name()] = new_col;
    return new_col;
  }

  // Rebuilds `column_list` and `column_index_list` of `scan_` with columns and
  // indices in `indexed_columns`.
  absl::Status RebuildScanColumns(
      std::vector<std::pair<int, ResolvedColumn>>& indexed_columns) {
    std::sort(indexed_columns.begin(), indexed_columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    ResolvedColumnList new_column_list;
    std::vector<int> new_column_index_list;
    new_column_list.reserve(indexed_columns.size());
    new_column_index_list.reserve(indexed_columns.size());
    for (const auto& [index, column] : indexed_columns) {
      new_column_index_list.push_back(index);
      new_column_list.push_back(column);
    }

    GOOGLESQL_ASSIGN_OR_RETURN(scan_, ToBuilder(std::move(scan_))
                                .set_column_list(new_column_list)
                                .set_column_index_list(new_column_index_list)
                                .Build());
    return absl::OkStatus();
  }

  std::unique_ptr<const ScanType> scan_;
  MeasureCollector& measure_collector_;
  TypeFactory& type_factory_;
  ColumnFactory& column_factory_;
};

// Injects closure columns for measure source scans that contain AGG'ed measures
// with measure expressions.
//
// This class identifies the possible source scans for measure columns, and
// delegates the actual rewrite to MeasureSourceColumnReplacer.
class ClosureInjector : public ResolvedASTRewriteVisitor {
 public:
  ClosureInjector(MeasureCollector& measure_collector,
                  TypeFactory& type_factory, ColumnFactory& column_factory)
      : measure_collector_(measure_collector),
        type_factory_(type_factory),
        column_factory_(column_factory) {}

 protected:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedTableScan(
      std::unique_ptr<const ResolvedTableScan> scan) override {
    return MeasureSourceColumnReplacer<ResolvedTableScan>(
               std::move(scan), measure_collector_, type_factory_,
               column_factory_)
        .Replace();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedTVFScan(
      std::unique_ptr<const ResolvedTVFScan> scan) override {
    return MeasureSourceColumnReplacer<ResolvedTVFScan>(
               std::move(scan), measure_collector_, type_factory_,
               column_factory_)
        .Replace();
  }

 private:
  MeasureCollector& measure_collector_;
  TypeFactory& type_factory_;
  ColumnFactory& column_factory_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>> AddClosures(
    MeasureCollector& measure_collector,
    std::unique_ptr<const ResolvedNode> resolved_ast, TypeFactory& type_factory,
    ColumnFactory& column_factory) {
  ClosureInjector visitor(measure_collector, type_factory, column_factory);
  return visitor.VisitAll(std::move(resolved_ast));
}

}  // namespace googlesql
