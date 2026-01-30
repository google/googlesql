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

#include "googlesql/analyzer/rewriters/row_type_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/row_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// This is the rewriter for RowTypes, as described in (broken link).
//
// RowTypes are used in initial analysis as non-concrete types which are
// never stored or returned.  A ROW<T> type is a reference to a row of table T,
// and a MULTIROW<T> is a reference to multiple rows of table T.
//
// The rewriter replaces the RowTypes with concrete types (usually STRUCTs)
// encapsulating the information needed to produce the columns needed when
// processing the RowType.
//
// There are three forms of RowTypes:
// 1. Non-join RowTypes representing a row produced by a table scan.
//    These have `IsJoin() = false`, and are always ROW<T>.
//
// 2. Join RowTypes with type ROW<T>.
//    These represent an N:1 join column producing at most one row.
//
// 3. Join RowTypes with type MULTIROW<T>.
//    These represent a 1:N join column that could produce multiple rows.
//
// ROW<T> values support ResolvedGetRowField expressions that extract a column
// from the row.
//
// Join RowTypes support ResolvedArrayScans that return rows produced by
// the join (at most one row for ROW types).  The output is a ROW<T> type,
// which is not a join row.  i.e. It looks like a ROW<T> produced as a table
// scan.  The ArrayScan here acts like a Join, and will be rewritten as an
// actual join.
//
// ResolvedFlatten expressions are also supported on RowTypes, and look like
// chains of sequential GetField and ArrayScan operators.
//
// Rewrite strategy for non-join ROW<T>
// ------------------------------------
// Non-join ROW<T> types represent rows of table T, and support
// ResolvedGetRowField.  These ROW<T> are produced in ResolvedTableScans
// with `read_as_row_type`.  The rewrite replaces the ROW<T> type with a STRUCT
// containing all column values that will required from that row.
//
// The original TableScan (producing just the ROW<T> column) is replaced by
// a TableScan reading all required columns, and then doing a ProjectScan with
// a MakeStruct expression storing those columns.
//
// ResolvedGetRowField expressions on ROW<T> values can then be replaced with
// ResolvedGetStructField expressions extracting the column bound into the
// STRUCT.
//
// Rewrite strategy for join RowTypes
// ----------------------------------
// Join RowTypes are used for join columns (on table T1), which represent rows
// that can be fetched from another table T2 using a join.  This basically
// looks like traversing a foreign key join, which could produce 0, 1, or
// multiple rows.  (Cases known to produce at most 1 row have ROW<T2> type.)
//
// The initial table scan of T1 doesn't read table T2.  The join column
// represents a join that could be run later, if needed.
//
// This works by having the initial TableScan of T1, when reading the join
// column with type ROW<T2> (or MULTIROW<T2>), instead produce a replacement
// STRUCT binding in the join keys (read from columns in T1) necessary to fetch
// the corresponding rows from T2 later.
//
// The initial ResolvedTableScan of T1 is rewritten to read those needed keys
// and then make the needed STRUCTs with a ProjectScan.
//
// ResolvedArrayScans of join RowTypes iterate over the joined rows, producing
// ROW<T2> rows as output.
// These ResolvedArrays are rewritten as ResolvedJoinScans, joining a
// ResolvedTableScan of T2 with a ResolvedProjectScan producing STRUCTs
// binding in all required columns (as described in the non-join ROW<T>
// rewrite above).
//
// Join RowTypes with ROW<T> type also support ResolvedGetRowField expressions
// that read a column of the row.  These are rewritten as expression subqueries
// that fetch the row (found using the keys bound in the STRUCT) and return
// the requested column.  (If no row is found, the subquery returns NULL.)
//
// TODO: This could potentially be optimized to fetch multiple
// columns earlier with a single subquery, rather than using a separate
// subquery to fetch each column (and rely on engines to optimzie that).
//
// STRUCT optimization
// -------------------
// The rewrites described above always make STRUCTs binding in the list of
// required columns.  In many cases, those STRUCTs would just have a single
// column.  In those cases, we omit the STRUCT and just pass around that
// single value.  MakeStruct and GetStructField expressions are omitted,
// and the bound columns can be read directly with a ColumnRef.
//
// TODO This optimization is turned off right now, because that
// makes the test output more consistent and understandable.
//
// How it the implementation works
// -------------------------------
// The rewrite happens in 4 steps.
//
// 1. Traverse the resolved AST to collect information. (RowTypeRewriterVisitor)
//    - The list of all RowTypes that occur.
//    - For each RowType, the list of Columns extracted from it with
//      ResolvedGetRowField operations.
//
// 2. Process the collected state and compute replacement types.
//    (State::MakeReplacementTypes)
//    - For each RowType, derive the replacement STRUCT type, with the list
//      of Columns it needs to bind in.
//
// 3. Traverse the resolved AST and rewrite all nodes that actively process
//    RowTypes. (RowTypeRewriterVisitor)
//    - Rewrite TableScans, ArrayScans, GetRowField, and Flatten to produce or
//      consume replacement STRUCTs rather than RowType.
//
// 4. Traverse the resolved AST and rewrite everything else that propagates
//    RowTypes. (RowTypeColumnRewriterVisitor)
//    - Every ResolvedColumn with a RowType type is replaced by a column with
//      its replacement type.  This creates replacement columns lazily.
//    - For every Type field (e.g. ResolvedExpression types, including n
//      ResolvedColumnRefs), if the Type is a RowType, replace it with its
//      replacement type.
//    - Update FunctionSignatures containing RowTypes to use their replacement
//      types.
//
// The rewrites are all done locally and independently.  Intermediate resolved
// ASTs may be invalid, but after all rewrites are done, input and output
// ResolvedColumns and Types line up for all nodes and the Resolved AST is
// valid.
//
// All column propagation happens automatically, including in operations that
// create new ResolvedColumns (derived from old columns with RowTypes),
// including in columns produced by CTEs.

// TODO Remove all the ABSL_LOG statements or make them VLOGs.
// They are useful while this is in development.

namespace googlesql {
namespace {

// Return a ResolvedColumn DebugString followed by its type.
std::string ColDebugString(const ResolvedColumn& column) {
  return absl::StrCat(
      column.DebugString(), " (",
      column.type() != nullptr ? column.type()->DebugString() : "<no type>",
      ")");
}

// Return comma-separated column names.
std::string DebugStringColumnNames(const std::vector<const Column*>& columns) {
  std::string result;
  bool first = true;
  for (const Column* column : columns) {
    absl::StrAppend(&result, first ? "" : ", ", column->Name());
    first = false;
  }
  return result;
}

// State collected for a RowType being written.
// This holds the replacement_type (after State::MakeReplacementTypes).
// This tracks the Columns referenced through this RowType.
class RowTypeState {
 public:
  // Get the Type this RowType is rewritten to.
  const Type* replacement_type() const {
    ABSL_DCHECK(replacement_type_ != nullptr)
        << "Called replacement_type() before MakeReplacementTypes";
    return replacement_type_;
  }

  // Return true if this RowType is rewritten to a STRUCT.
  // False means it's a single value that doesn't need a STRUCT wrapper.
  bool made_struct() const { return made_struct_; }

  // Get the Columns referenced through this RowType.
  const std::vector<const Column*>& GetReferencedTableColumns() const {
    return table_columns_;
  }

  // Add a Column referenced through this RowType.
  void AddReferencedTableColumn(const Column* table_column) {
    if (googlesql_base::InsertIfNotPresent(&table_columns_map_, table_column,
                                table_columns_map_.size())) {
      table_columns_.push_back(table_column);
    }
  }

  // Get the unique index for a Column referenced through this RowType.
  // This is the field number for that column in the replacement STRUCT.
  absl::StatusOr<int> GetFieldIdxForReferencedTableColumn(
      const Column* table_column) const {
    GOOGLESQL_RET_CHECK(made_struct_);

    const int* field_idx = googlesql_base::FindOrNull(table_columns_map_, table_column);
    GOOGLESQL_RET_CHECK(field_idx != nullptr)
        << "Missing column: " << table_column->FullName();
    return *field_idx;
  }

  // Make a ResolvedExpr for the replacement object for this RowType.
  // If multiple column values are needed, this will make a STRUCT.
  // If only one column value is needed, that value will be used directly.
  // `fields` are ResolvedExprs for the input columns.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeStructIfNecessary(
      std::vector<std::unique_ptr<const ResolvedExpr>> fields) const {
    if (made_struct_) {
      GOOGLESQL_RET_CHECK(replacement_type_->IsStruct());
      GOOGLESQL_RET_CHECK_EQ(replacement_type_->AsStruct()->num_fields(), fields.size());
      return MakeResolvedMakeStruct(replacement_type_, std::move(fields));
    } else {
      GOOGLESQL_RET_CHECK_EQ(fields.size(), 1);
      return std::move(fields[0]);
    }
  }

  std::string DebugString(const RowType* row_type) const {
    std::string result;
    if (!row_type->bound_columns().empty()) {
      absl::StrAppend(&result, "\n    bound_columns: ",
                      DebugStringColumnNames(row_type->bound_columns()));
    }
    if (row_type->bound_source_table() != nullptr) {
      absl::StrAppend(&result, "\n    bound_source_table: ",
                      row_type->bound_source_table()->FullName());
    }
    if (!row_type->bound_source_columns().empty()) {
      absl::StrAppend(&result, "\n    bound_source_columns: ",
                      DebugStringColumnNames(row_type->bound_source_columns()));
    }
    absl::StrAppend(&result, "\n    replacement_type_: ",
                    replacement_type_ != nullptr
                        ? replacement_type_->DebugString()
                        : "nullptr");
    if (!table_columns_.empty()) {
      absl::StrAppend(&result, "\n    table_columns: ",
                      DebugStringColumnNames(table_columns_));
    }
    return result;
  }

  // Ordered list of unique Columns that will be needed in GetRowField calls
  // on this RowType.  The order makes output deterministic.
  std::vector<const Column*> table_columns_;

  // Map each Column to its index in `table_columns_`.
  absl::flat_hash_map<const Column*, int> table_columns_map_;

  // The replacement type for this RowType, if needed.
  // If multiple fields are needed, it will be a STRUCT.
  // If only one field is needed, we can bypass making a STRUCT and just use
  // that field's type directly.  `made_struct_` will be false in this case.
  const Type* replacement_type_ = nullptr;

  bool made_struct_ = false;
  // When `made_struct_` is false, this records the name that would have been
  // used as the struct field name (from the column that would have been bound).
  std::string struct_field_name_;

  friend class State;
};

// State carried across phases of the rewriter.
class State {
 public:
  explicit State(ColumnFactory& column_factory)
      : column_factory_(column_factory) {}

  // Get the RowTypeState for a RowType.  It must exist already.
  // The returned pointers are always non-null.
  // These could return references but that doesn't build in googlesql oss.
  absl::StatusOr<const RowTypeState*> GetRowTypeState(
      const RowType* row_type) const {
    const std::unique_ptr<RowTypeState>* row_type_state_ptr =
        googlesql_base::FindOrNull(row_type_state_map_, row_type);
    GOOGLESQL_RET_CHECK(row_type_state_ptr != nullptr)
        << "RowTypeState not found for " << row_type->DebugString();
    GOOGLESQL_RET_CHECK(*row_type_state_ptr != nullptr);
    return row_type_state_ptr->get();
  }
  absl::StatusOr<RowTypeState*> GetMutableRowTypeState(
      const RowType* row_type) {
    const std::unique_ptr<RowTypeState>* row_type_state_ptr =
        googlesql_base::FindOrNull(row_type_state_map_, row_type);
    GOOGLESQL_RET_CHECK(row_type_state_ptr != nullptr)
        << "RowTypeState not found for " << row_type->DebugString();
    GOOGLESQL_RET_CHECK(*row_type_state_ptr != nullptr);
    return row_type_state_ptr->get();
  }

  // Register a RowType if it isn't already.
  // Return the RowTypeState for that RowType.
  // For join columns, also register the corresponding element RowType.
  RowTypeState& RegisterRowType(const RowType* row_type) {
    if (row_type->IsJoin()) {
      AddOrGetRowTypeState(row_type->element_type());
    }
    return AddOrGetRowTypeState(row_type);
  }

  // Get the replacement column for a ResolvedColumn that has a RowType.
  // Make that replacement column if we haven't seen `orig_column` before.
  // This works in the second-pass visitor after RowType replacements have
  // been created with MakeReplacementTypes.
  absl::StatusOr<ResolvedColumn> FindOrAddReplacementColumn(
      const ResolvedColumn& orig_column) {
    GOOGLESQL_RET_CHECK(orig_column.type()->IsRow());

    ResolvedColumn* found_column =
        googlesql_base::FindOrNull(column_replacement_map_, orig_column);
    if (found_column != nullptr) {
      return *found_column;
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn new_column,
                       MakeReplacementColumn(orig_column));
      GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&column_replacement_map_, orig_column,
                                        new_column));
      return new_column;
    }
  }

  // Make the replacement_type_ for each row_type found in the first pass.
  absl::Status MakeReplacementTypes(TypeFactory& type_factory) {
    GOOGLESQL_RET_CHECK_EQ(row_types_.size(), row_type_state_map_.size());

    // The first pass handles the join RowTypes.  The second pass for non-join
    // RowTypes may reference those as inner RowTypes (for join columns that
    // are columns of the outer RowType's table).
    for (const RowType* row_type : row_types_) {
      GOOGLESQL_ASSIGN_OR_RETURN(RowTypeState * row_type_state,
                       GetMutableRowTypeState(row_type));
      GOOGLESQL_RET_CHECK(row_type_state->replacement_type_ == nullptr);

      if (!row_type->IsJoin()) {
        continue;
      }

      // For ROW types representing join columns, the struct includes the
      // `bound_columns` from the RowType.
      const Table* table = row_type->table();
      // So far, we only support joins to tables with DEFAULT ColumnListMode.
      GOOGLESQL_RET_CHECK(table->GetColumnListMode() == Table::ColumnListMode::DEFAULT);

      std::vector<StructType::StructField> struct_fields;
      for (const Column* column : row_type->bound_columns()) {
        GOOGLESQL_RET_CHECK(!column->GetType()->IsRow());
        struct_fields.emplace_back(column->Name(), column->GetType());
      }
      GOOGLESQL_RET_CHECK(!struct_fields.empty());

      // TODO Enable skipping STRUCT types when not needed.
      if (/* DISABLES CODE */ (true) || struct_fields.size() != 1) {
        GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(
            struct_fields, &row_type_state->replacement_type_));
        row_type_state->made_struct_ = true;
      } else {
        row_type_state->replacement_type_ = struct_fields[0].type;
        row_type_state->made_struct_ = false;
        row_type_state->struct_field_name_ = struct_fields[0].name;
      }

      GOOGLESQL_RET_CHECK(row_type_state->replacement_type_ != nullptr);
      ABSL_LOG(ERROR) << "Made replacement_type for " << row_type->DebugString()
                 << ": " << row_type_state->replacement_type_->DebugString();
    }

    // The second pass handles the non-join RowTypes.
    for (const RowType* row_type : row_types_) {
      GOOGLESQL_ASSIGN_OR_RETURN(RowTypeState * row_type_state,
                       GetMutableRowTypeState(row_type));

      if (row_type->IsJoin()) {
        continue;
      }

      // For ROW types representing TableScans, the struct includes all
      // columns that will be read from that ROW later.
      GOOGLESQL_RET_CHECK_EQ(row_type_state->table_columns_.size(),
                   row_type_state->table_columns_map_.size());

      // The outer struct has an entry for each Column being read.
      // When those columns are join columns (with RowTypes), use that RowType's
      // replacement type as the field type.
      std::vector<StructType::StructField> outer_struct_fields;
      for (const Column* table_column : row_type_state->table_columns_) {
        const Type* type = table_column->GetType();
        if (type->IsRow()) {
          const RowType* inner_row_type = type->AsRow();

          GOOGLESQL_ASSIGN_OR_RETURN(const RowTypeState* inner_row_type_state,
                           GetRowTypeState(inner_row_type));
          GOOGLESQL_RET_CHECK(inner_row_type_state->replacement_type_ != nullptr);
          type = inner_row_type_state->replacement_type_;
        }

        outer_struct_fields.emplace_back(table_column->Name(), type);
      }

      // Make the replacement type, with a STRUCT if necessary.
      GOOGLESQL_RET_CHECK(row_type_state->replacement_type_ == nullptr);
      // TODO Enable skipping STRUCT types when not needed.
      // This always used STRUCT for MULTIROW types because they sometimes show
      // up in ArrayScan or other places that was confusing other code.
      if (/* DISABLES CODE */ (true) || outer_struct_fields.size() != 1 ||
          row_type->IsMultiRow()) {
        GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(
            outer_struct_fields, &row_type_state->replacement_type_));
        row_type_state->made_struct_ = true;
      } else {
        row_type_state->replacement_type_ = outer_struct_fields[0].type;
        row_type_state->made_struct_ = false;
        row_type_state->struct_field_name_ = outer_struct_fields[0].name;
      }

      GOOGLESQL_RET_CHECK(row_type_state->replacement_type_ != nullptr);
      ABSL_LOG(ERROR) << "Made type for " << row_type->DebugString() << ": "
                 << row_type_state->replacement_type_->DebugString();
    }

    return absl::OkStatus();
  }

  std::string DebugString() const {
    std::string result = "RowType rewrite state:";

    absl::StrAppend(&result, "\nRow types:");
    for (const RowType* row_type : row_types_) {
      absl::StrAppend(&result, "\n  ", row_type->DebugString());
      const std::unique_ptr<RowTypeState>* row_type_state_ptr =
          googlesql_base::FindOrNull(row_type_state_map_, row_type);
      if (row_type_state_ptr == nullptr) continue;
      if (*row_type_state_ptr == nullptr) continue;
      absl::StrAppend(&result, (*row_type_state_ptr)->DebugString(row_type));
    }

    absl::StrAppend(&result, "\ncolumn_replacement_map:");
    for (const auto& it : column_replacement_map_) {
      absl::StrAppend(&result, "\n  ", ColDebugString(it.first), " -> ",
                      ColDebugString(it.second));
    }
    return result;
  }

  ColumnFactory& column_factory() { return column_factory_; }

 private:
  ColumnFactory& column_factory_;

  // List row types found.  These give a deterministic order, which we don't
  // get from keys of the map.
  std::vector<const RowType*> row_types_;

  // RowTypeState for each RowType seen.
  // For non-join TableScans, the resolver creates a unique RowType instance
  // for each unique TableScan.
  // The value is a unique_ptr so the objects won't move around.
  absl::flat_hash_map<const RowType*, std::unique_ptr<RowTypeState>>
      row_type_state_map_;

  // Map of ResolvedColumns to replacement ResolvedColumns.
  // This will get an entry for every ResolvedColumn with a RowType.
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> column_replacement_map_;

  // Get the RowTypeState for a RowType.  Create it if necessary.
  RowTypeState& AddOrGetRowTypeState(const RowType* row_type) {
    std::unique_ptr<RowTypeState>& ptr = row_type_state_map_[row_type];
    if (ptr == nullptr) {
      row_types_.push_back(row_type);
      ptr = std::make_unique<RowTypeState>();
    }
    return *ptr;
  }

  // This makes a replacement ResolvedColumn for a ResolvedColumn with RowType.
  // This does not add the new column in the map.
  absl::StatusOr<ResolvedColumn> MakeReplacementColumn(
      const ResolvedColumn& orig_column) const {
    GOOGLESQL_RET_CHECK(orig_column.type()->IsRow());
    const RowType* row_type = orig_column.type()->AsRow();
    GOOGLESQL_ASSIGN_OR_RETURN(const RowTypeState* row_type_state,
                     GetRowTypeState(row_type));

    const Type* replacement_type_ = row_type_state->replacement_type_;
    GOOGLESQL_RET_CHECK(replacement_type_ != nullptr);

    std::string field_suffix;
    /* TODO Enable non-STRUCT simplification, and handle naming.
    if (!row_type_state->made_struct()) {
      GOOGLESQL_RET_CHECK(!row_type_state->struct_field_name_.empty());
      field_suffix = absl::StrCat("$", row_type_state->struct_field_name_);
    }
    */
    GOOGLESQL_RET_CHECK(row_type_state->made_struct());
    if (!row_type->IsJoin()) {
      field_suffix = "$scanrow";
    } else {
      field_suffix = row_type->IsMultiRow() ? "$join_multirow" : "$join_row";
    }

    // Preserve annotations from the original column.
    // TODO Figure out how to validate behavior with annotations.
    ResolvedColumn new_column = column_factory_.MakeCol(
        orig_column.table_name(),
        absl::StrCat(orig_column.name().starts_with("$") ? "" : "$",
                     orig_column.name(), field_suffix),
        AnnotatedType(replacement_type_, orig_column.type_annotation_map()));

    ABSL_LOG(INFO) << "Made replacement column: " << ColDebugString(orig_column)
              << " -> " << ColDebugString(new_column);
    return new_column;
  }
};

// This first-pass visitor collects the RowTypes, and lists of all Columns
// read from each RowType with ResolvedGetRowField.
class RowTypeCollectorVisitor : public ResolvedASTVisitor {
 public:
  explicit RowTypeCollectorVisitor(State& state) : state_(state) {}

  // Find all RowTypes created by ResolvedTableScan and register them.
  // They can be created for the table with `read_as_row_type` or for
  // join columns in the `column_list`.
  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    ABSL_LOG(INFO) << "VisitResolvedTableScan";

    for (const ResolvedColumn& column : node->column_list()) {
      if (!column.type()->IsRow()) continue;
      const RowType* row_type = column.type()->AsRow();
      state_.RegisterRowType(row_type);

      // Check assumptions about RowTypes in ResolvedTableScans.
      if (node->read_as_row_type()) {
        GOOGLESQL_RET_CHECK_EQ(node->column_list_size(), 1);
        GOOGLESQL_RET_CHECK(!row_type->IsJoin());
        GOOGLESQL_RET_CHECK(row_type->IsSingleRow());
      } else {
        // Any other RowTyped column produced by the TableScan is
        // a join column.
        GOOGLESQL_RET_CHECK(row_type->IsJoin());
      }
    }
    return absl::OkStatus();
  }

  // Register the RowTypes referenced or returned by ResolvedGetRowField.
  // Also record the Columns read of each RowType.
  absl::Status VisitResolvedGetRowField(
      const ResolvedGetRowField* node) override {
    ABSL_LOG(INFO) << "VisitResolvedGetRowField";

    const Type* expr_type = node->expr()->type();
    GOOGLESQL_RET_CHECK(expr_type->IsRow());
    const RowType* row_type = expr_type->AsRow();
    ABSL_LOG(INFO) << "Handle expr type " << row_type->DebugString();
    RowTypeState& row_type_state = state_.RegisterRowType(row_type);

    row_type_state.AddReferencedTableColumn(node->column());

    if (node->type()->IsRow()) {
      const RowType* field_row_type = node->type()->AsRow();
      // Output RowTypes from ResolvedGetRowField are always join columns.
      GOOGLESQL_RET_CHECK(field_row_type->IsJoin());
      state_.RegisterRowType(field_row_type);
    }

    return node->ChildrenAccept(this);
  }

 private:
  State& state_;
};

// This helper class stores a set of unique Columns to read from `table`, with a
// ResolvedColumn for each of them.
class ReadColumnsSet {
 public:
  explicit ReadColumnsSet(State& state, const Table* table)
      : state_(state), table_(table) {}

  // Get the ordered lists of Columns and ResolvedColumns to read.
  const std::vector<const Column*>& table_columns() const {
    return table_columns_;
  }
  const std::vector<ResolvedColumn>& resolved_columns() const {
    return resolved_columns_;
  }

  // Add an entry for `table_column` if one doesn't exist yet, creating
  // a ResolvedColumn for it.
  // Return the ResolvedColumn for this Column.
  absl::StatusOr<ResolvedColumn> GetResolvedColumn(const Column* table_column) {
    ResolvedColumn resolved_column;
    const ResolvedColumn* found_resolved_column =
        googlesql_base::FindOrNull(column_map_, table_column);
    if (found_resolved_column != nullptr) {
      resolved_column = *found_resolved_column;
    } else {
      resolved_column = state_.column_factory().MakeCol(
          table_->Name(), table_column->Name(),
          AnnotatedType(table_column->GetType(),
                        table_column->GetTypeAnnotationMap()));
      ABSL_LOG(INFO) << "Added column in GetResolvedColumn: "
                << ColDebugString(resolved_column);
      GOOGLESQL_RETURN_IF_ERROR(AddMappedColumn(table_column, resolved_column));
    }
    return resolved_column;
  }

  // Add an entry for a Column, with a ResolvedColumn that already exists.
  // There must not be a existing entry for `table_column`.
  absl::Status AddMappedColumn(const Column* table_column,
                               const ResolvedColumn& resolved_column) {
    GOOGLESQL_RET_CHECK(
        googlesql_base::InsertIfNotPresent(&column_map_, table_column, resolved_column))
        << "Added column twice in AddMappedColumn: "
        << ColDebugString(resolved_column);

    resolved_columns_.push_back(resolved_column);
    table_columns_.push_back(table_column);
    return absl::OkStatus();
  }

  // Return a ResolvedExpr constructed to build a replacement struct
  // (if necessary) with fields containing values from `columns`.
  // The `columns` (needed to produce the struct) get added to this
  // ReadColumnSet.  `row_type_state` is used to get the replacement_type
  // for the replacement struct.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ReadStructWithColumns(
      const std::vector<const Column*>& columns,
      const RowTypeState* row_type_state) {
    GOOGLESQL_RET_CHECK(!columns.empty());

    std::vector<std::unique_ptr<const ResolvedExpr>> struct_field_exprs;
    for (const Column* bound_column : columns) {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn resolved_column,
                       GetResolvedColumn(bound_column));
      ABSL_LOG(INFO) << "Added column in ReadStructWithColumns: "
                << ColDebugString(resolved_column);

      struct_field_exprs.push_back(
          MakeResolvedColumnRef(resolved_column, /*is_correlated=*/false));
    }
    // Both ColumnRefs are reported as already using `replacement_type` here
    // so that comparison will be allowed.
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto make_struct_expr,
        row_type_state->MakeStructIfNecessary(std::move(struct_field_exprs)));
    return make_struct_expr;
  }

 private:
  State& state_;
  const Table* table_;

  // Map of `table_columns_` to `resolved_columns_`.
  absl::flat_hash_map<const Column*, ResolvedColumn> column_map_;

  // Ordered lists of Columns and ResolvedColumns to read.
  std::vector<const Column*> table_columns_;
  std::vector<ResolvedColumn> resolved_columns_;
};

// The second-pass rewriter replaces the nodes that act directly on RowTypes.
// This includes:
// * Replace ResolvedTableScans, returning replacement types instead RowTypes.
// * Replace ResolvedGetRowField with one of:
//   - a ResolvedGetStructField, to get a field out of a replacement STRUCT.
//   - a ResolvedColumnRef, if the replacement was a single non-STRUCT value.
//   - a subquery, if this requires expanding a join ROW.
// * Replace ResolvedArrayScan of a (join) RowType with a subquery fetching
//   rows of the joined table.
class RowTypeRewriterVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit RowTypeRewriterVisitor(State& state,
                                  FunctionCallBuilder& function_call_builder)
      : state_(state), function_call_builder_(function_call_builder) {}

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedTableScan(
      std::unique_ptr<const ResolvedTableScan> node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedGetRowField(
      std::unique_ptr<const ResolvedGetRowField> get_row_field) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedArrayScan(
      std::unique_ptr<const ResolvedArrayScan> array_scan) override;

  // Make a ResolvedScan that scans `table`, returning the post-rewrite
  // columns.
  //
  // `orig_resolved_columns` are the pre-rewrite columns requested from this
  // table for the original ResolvedTableScan.  This can include non-RowTypes.
  //
  // `orig_table_columns` matches 1:1 with `orig_resolved_columns`.
  // For non-RowTypes, this is the Column being read.
  // Row RowTypes, this can be nullptr.  (There is no Column for a
  // ResolvedColumn produced as a `read_as_row_type` read.)
  absl::StatusOr<std::unique_ptr<const ResolvedScan>> MakeRewrittenTableScan(
      const Table* table,
      absl::Span<const ResolvedColumn> orig_resolved_columns,
      const std::vector<const Column*>& orig_table_columns,
      absl::string_view alias = "");

  // Make the rewrite expression for the input column with `row_type`.
  // This adds any needed input columns to `read_columns_set`, and then
  // makes a ResolvedExpr to build the output replacement type for `row_type`.
  //
  // This can be called recursively once (if `is_inner` is false) to do the
  // same for join columns inside the ROW for table scan row.  (When a row
  // contains a join column, the row's replacement struct will have the
  // join column's replacement struct as one of its fields.)
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeRewriteExprForColumn(
      const RowType* row_type, bool is_inner, ReadColumnsSet& read_columns_set);

  State& state_;

  FunctionCallBuilder& function_call_builder_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RowTypeRewriterVisitor::PostVisitResolvedTableScan(
    std::unique_ptr<const ResolvedTableScan> node) {
  // We need a rewrite if the TableScan has `read_as_row_type` or it
  // produces any RowTyped columns.
  bool need_rewrite = node->read_as_row_type();
  if (!need_rewrite) {
    for (const ResolvedColumn& column : node->column_list()) {
      if (column.type()->IsRow()) {
        need_rewrite = true;
        break;
      }
    }
  }
  if (!need_rewrite) {
    return std::move(node);
  }

  const Table* table = node->table();
  const std::vector<ResolvedColumn>& orig_resolved_columns =
      node->column_list();
  std::vector<const Column*> orig_table_columns = node->table_column_list();
  if (orig_table_columns.empty()) {
    for (int column_idx : node->column_index_list()) {
      orig_table_columns.push_back(table->GetColumn(column_idx));
    }
  }

  if (node->read_as_row_type()) {
    // With `read_as_row_type`, the TableScan can optionally output one
    // ROW_typed ResolvedColumn.  There are no Columns to read.
    GOOGLESQL_RET_CHECK_EQ(orig_table_columns.size(), 0);
    GOOGLESQL_RET_CHECK_LE(orig_resolved_columns.size(), 1);
    if (orig_resolved_columns.size() == 1) {
      orig_table_columns.push_back(nullptr);
    }
  }
  GOOGLESQL_RET_CHECK_EQ(orig_resolved_columns.size(), orig_table_columns.size());

  return MakeRewrittenTableScan(table, orig_resolved_columns,
                                orig_table_columns, node->alias());
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RowTypeRewriterVisitor::PostVisitResolvedGetRowField(
    std::unique_ptr<const ResolvedGetRowField> get_row_field) {
  ABSL_LOG(INFO) << "PostVisitResolvedGetRowField:\n"
            << get_row_field->DebugString();

  const Type* input_type = get_row_field->expr()->type();
  GOOGLESQL_RET_CHECK(input_type->IsRow());
  const RowType* row_type = input_type->AsRow();

  GOOGLESQL_ASSIGN_OR_RETURN(const RowTypeState* row_type_state,
                   state_.GetRowTypeState(row_type));

  if (!row_type->IsJoin()) {
    // So far, no other expressions returning non-join ROW types are allowed.
    // They act like range variables in the query, and cannot be passed to
    // functions.
    GOOGLESQL_RET_CHECK(get_row_field->expr()->Is<ResolvedColumnRef>())
        << "\n"
        << get_row_field->DebugString();
    const ResolvedColumnRef* orig_column_ref =
        get_row_field->expr()->GetAs<ResolvedColumnRef>();
    const ResolvedColumn& orig_column = orig_column_ref->column();

    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn replacement_column,
                     state_.FindOrAddReplacementColumn(orig_column));

    // TODO: Keep annotations, here and below?
    std::unique_ptr<const ResolvedExpr> expr = MakeResolvedColumnRef(
        replacement_column, orig_column_ref->is_correlated());
    if (row_type_state->made_struct()) {
      GOOGLESQL_ASSIGN_OR_RETURN(int field_idx,
                       row_type_state->GetFieldIdxForReferencedTableColumn(
                           get_row_field->column()));

      GOOGLESQL_RET_CHECK(expr->type()->IsStruct()) << "\n" << expr->DebugString();
      expr = MakeResolvedGetStructField(get_row_field->type(), std::move(expr),
                                        field_idx);
    }
    return expr;
  } else {
    // For ResolvedGetRowField on join columns, generate a subquery that
    // fetches the requested column from the joined table.
    const Type* replacement_type = row_type_state->replacement_type();
    GOOGLESQL_RET_CHECK(replacement_type != nullptr);

    const Table* read_table = row_type->table();
    const Column* read_column = get_row_field->column();
    GOOGLESQL_RET_CHECK(read_table != nullptr);
    GOOGLESQL_RET_CHECK(read_column != nullptr);

    // We have `<row>.<field>`.
    // We are trying to fetch `read_column` from a row of `read_table`.
    // `<row>` will be replaced by a struct later, containing the bound columns
    // of `read_table`.
    //
    // We'll generate a ResolvedSubqueryExpr with a query like:
    //   FROM <read_table>
    //   |> WHERE <outer_row_type_column> = MakeStruct(<bound_columns>)
    //   |> SELECT <read_column>
    //
    // If the input <expr> is more than just a ColumnRef, then we'll also
    // wrap it with
    //   WITH(<outer_row_type_column> AS <expr>, <the SubqueryExpr>)

    ResolvedColumn outer_row_type_column;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> with_assignments;

    if (get_row_field->expr()->Is<ResolvedColumnRef>()) {
      // The expression we want to reference from the subquery is just a column,
      // so we reference it with a correlated ResolvedColumnRef.
      outer_row_type_column =
          get_row_field->expr()->GetAs<ResolvedColumnRef>()->column();
    } else {
      // The expression we want to reference from the subquery is more than
      // just a column.  We'll make a WITH expression to compute it and give
      // it a ResolvedColumn, which we can reference from the subquery.
      outer_row_type_column = state_.column_factory().MakeCol(
          "$with_expr", "$with_col",
          AnnotatedType(replacement_type,
                        get_row_field->expr()->type_annotation_map()));

      auto node_builder = ToBuilder(std::move(get_row_field));
      with_assignments.push_back(MakeResolvedComputedColumn(
          outer_row_type_column, node_builder.release_expr()));
    }
    // Make a correlated ResolvedColumnRef to point at the RowType column
    // from outside the subquery (maybe in a WITH expression).
    // This ResolvedColumnRef reports its type as `replacement_type` so
    // generating the Equals comparison below works.
    // The ResolvedColumn inside will get replaced later.
    std::unique_ptr<const ResolvedColumnRef> row_type_column_ref =
        MakeResolvedColumnRef(replacement_type, outer_row_type_column,
                              /*is_correlated=*/true);

    // Compute Columns and ResolvedColumns we'll need to read.
    ReadColumnsSet read_columns_set(state_, read_table);

    // The `bound_columns` on the RowType are the join key.
    // Build a struct holding those columns, from TableScan columns.
    GOOGLESQL_ASSIGN_OR_RETURN(auto make_struct_expr,
                     read_columns_set.ReadStructWithColumns(
                         row_type->bound_columns(), row_type_state));

    // Also read the column we're actually trying to fetch and return.
    // It might overlap with one of the columns we read for the join key above.
    // The ReadColumnsSet will deduplicate them.
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn read_resolved_column,
                     read_columns_set.GetResolvedColumn(read_column));
    ABSL_LOG(INFO) << "Added column in PostVisitResolvedGetRowField #2: "
              << ColDebugString(read_resolved_column);

    // Do the TableScan to get all the physical columns we need.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedScan> new_scan,
        MakeRewrittenTableScan(read_table, read_columns_set.resolved_columns(),
                               read_columns_set.table_columns()));

    // Add the FilterScan that performs the join.
    // This does an Equals comparison on the structs holding the join keys.
    GOOGLESQL_ASSIGN_OR_RETURN(auto filter_expr, function_call_builder_.Equal(
                                           std::move(make_struct_expr),
                                           std::move(row_type_column_ref)));

    ResolvedColumnList final_column_list = {read_resolved_column};
    new_scan = MakeResolvedFilterScan(final_column_list, std::move(new_scan),
                                      std::move(filter_expr));

    // Make the correlated ResolvedSubqueryExpr containing those Scans.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_refs;
    parameter_refs.push_back(MakeResolvedColumnRef(outer_row_type_column,
                                                   /*is_correlated=*/false));

    std::unique_ptr<const ResolvedExpr> new_expr = MakeResolvedSubqueryExpr(
        read_column->GetType(), ResolvedSubqueryExpr::SCALAR,
        std::move(parameter_refs),
        /*in_expr=*/nullptr, std::move(new_scan));

    // If we need a WITH expression to produce the input ResolvedColumn,
    // wrap it around the SubqueryExpr.
    if (!with_assignments.empty()) {
      new_expr = MakeResolvedWithExpr(read_column->GetType(),
                                      std::move(with_assignments),
                                      std::move(new_expr));
    }

    ABSL_LOG(INFO) << "Rewrite ResolvedGetRowField generated:\n"
              << new_expr->DebugString();
    return std::move(new_expr);
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RowTypeRewriterVisitor::PostVisitResolvedArrayScan(
    std::unique_ptr<const ResolvedArrayScan> array_scan) {
  // See if we have any ROW types.
  bool found_row = false;
  for (const auto& array_expr : array_scan->array_expr_list()) {
    if (array_expr->type()->IsRow()) {
      found_row = true;
    }
  }
  // If not, there's nothing to rewrite.
  if (!found_row) {
    return std::move(array_scan);
  }

  ABSL_LOG(INFO) << "Rewriting ArrayScan:\n" << array_scan->DebugString();

  // We have a ResolvedArrayScan of `array_expr` (a join RowType) producing
  // `element_column` (a non-join ROW, which will be read like a
  // `read_as_row_type` table, but directly into the rewritten form where
  // we build a struct containing the bound columns).
  //
  // The input scan (with a ResolvedArrayScan) is roughly:
  //   <input scan>
  //   |> JOIN UNNEST(<array_expr>) AS <element_column>
  //
  // This output is roughly:
  //   <input scan>
  //   |> JOIN <read_table> AS t
  //      ON <array_expr> = MakeStruct(<bound_columns>)
  //   |> SELECT ..., MakeStruct(t.col1, t.col2, ...) AS <element_column>
  //
  // The replacement `element_column` is the replacement struct for the
  // ROW type produced by scanning `read_table`, which binds in all columns
  // that are fetched from that ROW type with ResolvedGetRowField later.
  //
  // The ResolvedArrayScan could have `is_outer` and/or a `join_expr`.
  // If present, those are added to the new ResolvedJoinScan.

  GOOGLESQL_RET_CHECK(array_scan->array_offset_column() == nullptr);
  GOOGLESQL_RET_CHECK_EQ(array_scan->array_expr_list_size(), 1);
  const ResolvedExpr* array_expr = array_scan->array_expr_list(0);

  // Get the RowType that's being scanned like an array.
  // It must be a join RowType.  (It could be ROW or MULTIROW.)
  GOOGLESQL_RET_CHECK(array_expr->type()->IsRow());
  const RowType* array_row_type = array_expr->type()->AsRow();
  GOOGLESQL_RET_CHECK(array_row_type->IsJoin());

  GOOGLESQL_ASSIGN_OR_RETURN(const RowTypeState* array_row_type_state,
                   state_.GetRowTypeState(array_row_type));

  // Get the element RowType. It should be a non-join ROW.
  GOOGLESQL_RET_CHECK_EQ(array_scan->element_column_list_size(), 1);
  const ResolvedColumn& element_column = array_scan->element_column_list(0);

  GOOGLESQL_RET_CHECK(element_column.type()->IsSingleRow())
      << "Bad element type: " << ColDebugString(element_column);
  const RowType* element_row_type = element_column.type()->AsRow();
  GOOGLESQL_RET_CHECK(!element_row_type->IsJoin());

  GOOGLESQL_RET_CHECK_EQ(element_column.type(), array_row_type->element_type())
      << ColDebugString(element_column) << ", "
      << array_row_type->element_type()->DebugString();

  // This is the table the join column points at, so it's the table to scan.
  const Table* element_table = element_row_type->table();

  // Store the Columns and ResolvedColumns we'll read for the struct.
  ReadColumnsSet read_columns_set(state_, element_table);

  // The `bound_columns` on the RowType are the join key.
  // Build a struct holding those columns, from TableScan columns.
  GOOGLESQL_ASSIGN_OR_RETURN(auto struct_expr,
                   read_columns_set.ReadStructWithColumns(
                       array_row_type->bound_columns(), array_row_type_state));

  // Now take apart the ArrayScan and build the JoinScan.
  ResolvedArrayScanBuilder array_scan_builder =
      ToBuilder(std::move(array_scan));

  ResolvedJoinScanBuilder join_builder;
  if (array_scan_builder.input_scan() != nullptr) {
    join_builder.set_left_scan(array_scan_builder.release_input_scan());
  } else {
    // The ArrayScan may have no `input_scan` if it's referencing a
    // correlated array.  We can just use a SingleRowScan.
    join_builder.set_left_scan(MakeResolvedSingleRowScan());
  }
  if (array_scan_builder.is_outer()) {
    join_builder.set_join_type(ResolvedJoinScan::LEFT);
  }
  // The final column list will be the same as in the original ArrayScan.
  join_builder.set_column_list(array_scan_builder.column_list());

  // We read the columns needed for the key struct for the join, plus the
  // ROW-typed element column for the actual ArrayScan output.
  ResolvedColumnList rhs_scan_resolved_columns =
      read_columns_set.resolved_columns();
  std::vector<const Column*> rhs_scan_table_columns =
      read_columns_set.table_columns();

  rhs_scan_resolved_columns.push_back(element_column);
  rhs_scan_table_columns.push_back(nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedScan> rhs_scan,
      MakeRewrittenTableScan(element_table, rhs_scan_resolved_columns,
                             rhs_scan_table_columns));
  join_builder.set_right_scan(
      GetAsResolvedNode<ResolvedScan>(std::move(rhs_scan)));

  // Make the join condition.
  // It'll be Equals comparison between the RowType's replacement struct
  // (containing the RowType's `bound_columns` for the join) and the
  // MakeStruct expression from the join rhs.

  // The ArrayScan expression is the input RowType value.
  // It'll be replaced by a struct storing the join key we need to use.
  GOOGLESQL_RET_CHECK_EQ(array_scan_builder.array_expr_list().size(), 1);
  std::unique_ptr<const ResolvedExpr> array_expr_val =
      std::move(array_scan_builder.release_array_expr_list()[0]);
  ABSL_LOG(INFO) << "array_expr:\n" << array_expr_val->DebugString();

  // Hack the type to be the replacement struct.  For now, it'll mismatch the
  // content of the ResolvedExpr, but the expression body wil get replaced
  // itself later.  Building Equals below requires matching types.
  ResolvedExpr* mutable_array_expr =
      const_cast<ResolvedExpr*>(array_expr_val.get());
  mutable_array_expr->set_type(array_row_type_state->replacement_type());
  ABSL_LOG(INFO) << "Hacked array_expr:\n" << array_expr_val->DebugString();

  GOOGLESQL_ASSIGN_OR_RETURN(auto join_expr,
                   function_call_builder_.Equal(std::move(struct_expr),
                                                std::move(array_expr_val)));

  // If the original ArrayScan had a join condition, add that into the new
  // join condition.  That original `join_expr` can only reference lhs columns
  // or the ArrayScan output element so no other rewrites are necessary.
  if (array_scan_builder.join_expr() != nullptr) {
    std::vector<std::unique_ptr<const ResolvedExpr>> and_inputs;
    and_inputs.push_back(std::move(join_expr));
    and_inputs.push_back(array_scan_builder.release_join_expr());
    GOOGLESQL_ASSIGN_OR_RETURN(join_expr,
                     function_call_builder_.And(std::move(and_inputs)));
  }

  ABSL_LOG(INFO) << "Generated join_expr:\n" << join_expr->DebugString();
  join_builder.set_join_expr(std::move(join_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(auto new_scan, std::move(join_builder).Build());

  ABSL_LOG(INFO) << "Rewritten ArrayScan:\n" << new_scan->DebugString();
  return std::move(new_scan);
}

// If we have a `read_as_row_type` ResolvedColumn, it'll have a nullptr in
// `orig_table_columns`.
// `orig_table_columns` elements can be nullptr for all ROW types.
absl::StatusOr<std::unique_ptr<const ResolvedScan>>
RowTypeRewriterVisitor::MakeRewrittenTableScan(
    const Table* table, absl::Span<const ResolvedColumn> orig_resolved_columns,
    const std::vector<const Column*>& orig_table_columns,
    absl::string_view alias) {
  GOOGLESQL_RET_CHECK_EQ(orig_resolved_columns.size(), orig_table_columns.size());

  ABSL_LOG(INFO) << "MakeRewrittenTableScan";
  for (const ResolvedColumn& column : orig_resolved_columns) {
    ABSL_LOG(INFO) << "  orig_resolved_column: " << ColDebugString(column);
  }

  // `final_output_column_list` is the rewritten output column list,
  // corresponding to the `orig_resolved_column` list produced by the original
  // ResolvedScan. This does not preserve ordering or match 1:1.
  std::vector<ResolvedColumn> final_output_column_list;
  final_output_column_list.reserve(orig_resolved_columns.size());

  // Store any ResolvedComputedColumns we need to compute.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> project_exprs;

  // Set of unique Columns we need to read, with a ResolvedColumn for each.
  // Some could be returned in `final_output_column_list, and some could be
  // needed as inputs to `project_exprs`.  (Some could be both.)
  ReadColumnsSet read_columns_set(state_, table);

  // Collect the non-ROW columns we're reading first.
  // Reuse the ResolvedColumns that already existed in the original TableScan.
  for (int idx = 0; idx < orig_resolved_columns.size(); ++idx) {
    const ResolvedColumn& col = orig_resolved_columns[idx];

    if (!col.type()->IsRow()) {
      const Column* column = orig_table_columns[idx];
      GOOGLESQL_RET_CHECK(column != nullptr);

      GOOGLESQL_RETURN_IF_ERROR(read_columns_set.AddMappedColumn(column, col));
      final_output_column_list.push_back(col);
    }
  }

  // Now handle all the RowType output columns, and figure out the STRUCTs we
  // need to add in a ProjectScan, and any extra columns we need to read in the
  // `read_columns_set`.
  for (const ResolvedColumn& col : orig_resolved_columns) {
    if (!col.type()->IsRow()) continue;
    ABSL_LOG(INFO) << "Rewriting TableScan column " << ColDebugString(col);

    const RowType* row_type = col.type()->AsRow();

    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn replacement_column,
                     state_.FindOrAddReplacementColumn(col));

    ABSL_LOG(INFO) << "replacement_column: " << ColDebugString(replacement_column);
    final_output_column_list.push_back(replacement_column);

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> expr,
        MakeRewriteExprForColumn(row_type,
                                 /*is_inner=*/false, read_columns_set));

    project_exprs.push_back(
        MakeResolvedComputedColumn(replacement_column, std::move(expr)));
  }

  GOOGLESQL_RET_CHECK_EQ(read_columns_set.resolved_columns().size(),
               read_columns_set.table_columns().size());

  // The ResolvedTableScanBuilder reads the columns from `read_columns_set`.
  ResolvedTableScanBuilder builder;
  builder.set_table(table);
  builder.set_alias(alias);
  builder.set_column_list(read_columns_set.resolved_columns());
  builder.set_table_column_list(read_columns_set.table_columns());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> new_scan,
                   std::move(builder).Build());

  // Then we add a ResolvedProjectScan if necessary.
  if (!project_exprs.empty()) {
    new_scan =
        MakeResolvedProjectScan(final_output_column_list,
                                std::move(project_exprs), std::move(new_scan));
  }

  ABSL_LOG(INFO) << "Made rewritten TableScan:\n" << new_scan->DebugString();
  return std::move(new_scan);
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
RowTypeRewriterVisitor::MakeRewriteExprForColumn(
    const RowType* row_type, bool is_inner, ReadColumnsSet& read_columns_set) {
  GOOGLESQL_ASSIGN_OR_RETURN(const RowTypeState* row_type_state,
                   state_.GetRowTypeState(row_type));

  // Compute the set of Columns we need to collect and bind in for this RowType.
  std::vector<const Column*> bound_source_columns;
  if (!row_type->IsJoin()) {
    bound_source_columns = row_type_state->GetReferencedTableColumns();
  } else {
    GOOGLESQL_RET_CHECK(!row_type->bound_source_columns().empty());
    bound_source_columns = row_type->bound_source_columns();
  }

  // Compute a ResolvedExpr for each column, which we can use to make a
  // struct, if necessary.
  std::vector<std::unique_ptr<const ResolvedExpr>> make_struct_args;
  for (const Column* table_column : bound_source_columns) {
    if (table_column->GetType()->IsRow()) {
      // For join columns, we need to make an inner replacement type inside
      // the outer (row-level) replacement struct.
      // Call this method recursively (at most once) for that inner type.
      const RowType* inner_row_type = table_column->GetType()->AsRow();
      GOOGLESQL_RET_CHECK(inner_row_type->IsJoin());
      GOOGLESQL_RET_CHECK(!is_inner);  // Don't recurse more than once.

      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedExpr> inner_expr,
          MakeRewriteExprForColumn(inner_row_type,
                                   /*is_inner=*/true, read_columns_set));

      make_struct_args.push_back(std::move(inner_expr));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn scan_col,
                       read_columns_set.GetResolvedColumn(table_column));

      // TODO: Handle annotations?
      make_struct_args.push_back(
          MakeResolvedColumnRef(scan_col, /*is_correlated=*/false));
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> output_expr,
      row_type_state->MakeStructIfNecessary(std::move(make_struct_args)));

  return std::move(output_expr);
}

// The final rewriter cleans up all remaining ResolvedColumns and Types to
// propagate replacement columns and types cleanly.
// This includes:
//
// * Replace any ResolvedColumn with RowType with a replacement column.
//   - Create the replacement column when we see each column for the first time.
//   - This doesn't distinguish ResolvedColumn creation from ResolvedColumn
//     references.  It doesn't matter which is seen first.
//
// * Replace any Type field (with type RowType) with the replacement type.
//   - This includes ResolvedColumnRefs and any other expressions that
//     originally returned RowTypes.
//
// * Rewrite signatures in ResolvedFunctionCalls that reference RowTypes.
//   - These must all be templated functions that had ANY types, so we can
//     rewrite the concrete signatures to use replacement types.
//
// These rewrites, and those in the earlier rewriter, are all done
// independently.  Intermediate Resolved ASTs may be invalid, but after all
// rewrites are done, input and output ResolvedColumns and Types line up
// for all nodes and the Resolved AST is valid.
class RowTypeColumnRewriterVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit RowTypeColumnRewriterVisitor(State& state) : state_(state) {}

 private:
  absl::StatusOr<ResolvedColumn> PostVisitResolvedColumn(
      const ResolvedColumn& column) override {
    if (column.type()->IsRow()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn replacement_column,
                       state_.FindOrAddReplacementColumn(column));
      return replacement_column;
    } else {
      return column;
    }
  }

  absl::StatusOr<const Type*> PostVisitType(const Type* type) override {
    if (type->IsRow()) {
      const RowType* row_type = type->AsRow();
      GOOGLESQL_ASSIGN_OR_RETURN(const RowTypeState* row_type_state,
                       state_.GetRowTypeState(row_type));

      GOOGLESQL_RET_CHECK(row_type_state->replacement_type() != nullptr);
      return row_type_state->replacement_type();
    } else {
      return type;
    }
  }

  static bool HasRowType(const FunctionArgumentType& arg_type) {
    return arg_type.type() != nullptr && arg_type.type()->IsRow();
  }

  // If a FunctionArgumentType reference RowType, return a rewrite
  // referencing the replacement type.
  absl::StatusOr<FunctionArgumentType> MapFunctionArgumentType(
      const FunctionArgumentType& arg_type) {
    if (!HasRowType(arg_type)) {
      return arg_type;
    }

    // PostVisitType computes the same mapping we need here.
    GOOGLESQL_ASSIGN_OR_RETURN(const Type* new_type, PostVisitType(arg_type.type()));

    return FunctionArgumentType(new_type, arg_type.options(),
                                arg_type.num_occurrences());
  }

  // Common PostVisit handler for all ResolvedFunctionCallBase subclasses.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  RewriteAnyResolvedFunctionCallBase(
      std::unique_ptr<const ResolvedFunctionCallBase> node) {
    const FunctionSignature& signature = node->signature();

    bool has_row = HasRowType(signature.result_type());
    if (!has_row) {
      for (const FunctionArgumentType& arg_type : signature.arguments()) {
        if (HasRowType(arg_type)) {
          has_row = true;
          break;
        }
      }
    }
    if (!has_row) {
      return std::move(node);
    }

    ABSL_LOG(INFO) << "PostVisitResolvedFunctionCallBase:\n" << node->DebugString();

    FunctionArgumentTypeList arguments;
    arguments.reserve(signature.arguments().size());
    for (const FunctionArgumentType& argument : signature.arguments()) {
      GOOGLESQL_ASSIGN_OR_RETURN(FunctionArgumentType new_argument,
                       MapFunctionArgumentType(argument));
      arguments.push_back(new_argument);
    }
    GOOGLESQL_ASSIGN_OR_RETURN(FunctionArgumentType result_type,
                     MapFunctionArgumentType(signature.result_type()));

    FunctionSignature new_signature(
        result_type, arguments, signature.context_id(), signature.options());

    // We can't use a Builder easily because we're working on a superclass node.
    ResolvedFunctionCallBase* mutable_node =
        const_cast<ResolvedFunctionCallBase*>(node.get());
    mutable_node->set_signature(new_signature);

    ABSL_LOG(INFO) << "Rewritten FunctionCall:\n" << node->DebugString();

    return std::move(node);
  }

  // All subclasses of ResolvedFunctionCallBase use the method above.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionCall(
      std::unique_ptr<const ResolvedFunctionCall> node) override {
    return RewriteAnyResolvedFunctionCallBase(std::move(node));
  }
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateFunctionCall(
      std::unique_ptr<const ResolvedAggregateFunctionCall> node) override {
    return RewriteAnyResolvedFunctionCallBase(std::move(node));
  }
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAnalyticFunctionCall(
      std::unique_ptr<const ResolvedAnalyticFunctionCall> node) override {
    return RewriteAnyResolvedFunctionCallBase(std::move(node));
  }

  State& state_;
};

class RowTypeTableScanRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> scan,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());

    FunctionCallBuilder function_call_builder(options, catalog, type_factory);

    ABSL_LOG(INFO) << "Before rewrite:\n" << scan->DebugString();

    State state(column_factory);
    RowTypeCollectorVisitor collector(state);
    GOOGLESQL_RETURN_IF_ERROR(scan->Accept(&collector));

    GOOGLESQL_RETURN_IF_ERROR(state.MakeReplacementTypes(type_factory));

    RowTypeRewriterVisitor rewriter1(state, function_call_builder);
    GOOGLESQL_ASSIGN_OR_RETURN(scan, rewriter1.VisitAll(std::move(scan)));
    ABSL_LOG(INFO) << "After first-pass rewrite:\n" << scan->DebugString();

    RowTypeColumnRewriterVisitor rewriter2(state);
    GOOGLESQL_ASSIGN_OR_RETURN(scan, rewriter2.VisitAll(std::move(scan)));
    ABSL_LOG(INFO) << "After final rewrite:\n" << scan->DebugString();

    return scan;
  }

  std::string Name() const override { return "RowTypeTableScanRewriter"; }
};

}  // namespace

const Rewriter* GetRowTypeTableScanRewriter() {
  static const auto* const kRewriter = new RowTypeTableScanRewriter;
  return kRewriter;
}

}  // namespace googlesql
