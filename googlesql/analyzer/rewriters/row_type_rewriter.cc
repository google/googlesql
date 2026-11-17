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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/annotation_propagator.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/table_valued_function.h"
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
#include "absl/container/flat_hash_set.h"
#include "absl/container/linked_hash_map.h"
#include "absl/container/linked_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"

// This is the rewriter for RowOrTableTypes, which includes RowType and
// TableRefType. See (broken link).
//
// RowOrTableTypes are used in initial analysis as non-concrete types which are
// never stored or returned. A RowType (ROW<T>) is a reference to a row of table
// T, and a TableRefType (TABLE<ROW<T>> or TABLE UNIQUE<ROW<T>>) is a virtual
// table that can produce rows of table T, originating as a join column on a
// table.
//
// The rewriter replaces the RowOrTableTypes with concrete types (usually
// STRUCTs) encapsulating the information needed to produce the columns needed
// when accessing the RowOrTableType. For example, if columns x, y, and z are
// read from ROW<T>, then it will be replaced with STRUCT<x, y, z>.
//
// There are several cases:
// 1. RowType representing a row produced by a table scan.
// 2. RowType representing a row or column produced by a TVF scan.
// 3. TableRefType with UNIQUE (IsSingleRowTable()==true).
//    These represent an N:1 join column producing at most one row.
// 4. TableRefType without UNIQUE. (IsMultiRowTable()==true)
//    These represent a N:N join column that could produce multiple rows.
//
// Case 1, 2, and 3 support ResolvedGetRowField expressions that extract a
// column from the row.
//
// Case 3 and 4 support ResolvedArrayScans that return rows produced by the join
// (at most one row for case 3). The output is a ROW<T> type, i.e. it looks
// like a ROW<T> produced from a table scan. The ArrayScan here acts like a
// join, and will be rewritten as an actual join.
//
// ResolvedFlatten expressions are also supported for all cases, and look like
// chains of sequential GetField and ArrayScan operators.
//
// Rewrite strategy for RowTypes
// -----------------------------
// ROW<T> types represent rows of table T, and support ResolvedGetRowField.
// These ROW<T> are produced in ResolvedTableScans with `read_as_row_type`, or
// by ResolvedTVFScans with a ROW type in the result schema. The rewrite
// replaces the ROW<T> type with a STRUCT containing all column values that will
// required from that row.
//
// For ResolvedTableScan:
// The original TableScan (producing just the ROW<T> column) is replaced by a
// TableScan reading all required columns, and then doing a ProjectScan with a
// MakeStruct expression storing those columns.
//
// For ResolvedTVFScan:
// The TVFScan column_list and result schema are update to replace ROW types
// with a corresponding STRUCT, containing all required columns read from the
// ROW type. The result schema is updated via a callback provided by the TVF for
// this purpose.
//
// For ResolvedGetRowField:
// ResolvedGetRowField expressions on ROW<T> values can be replaced with
// ResolvedGetStructField expressions extracting the column bound into the
// replacement STRUCT.
//
// Rewrite strategy for TableRefTypes
// ----------------------------------
// TableRefTypes are used for join columns (on table T1), which represent rows
// that can be fetched from another table T2 using a join. This basically looks
// like traversing a foreign key join, which could produce 0, 1, or multiple
// rows. (Cases known to produce at most 1 row have UNIQUE.)
//
// The initial table scan of T1 doesn't read table T2. The join column
// represents a join that could be run later, if needed.
//
// This works by having the initial TableScan of T1, when reading the join
// column (with element_type ROW<T2>), instead produce a replacement STRUCT
// binding in the join keys (read from columns in T1) necessary to fetch the
// corresponding rows from T2 later.
//
// The initial ResolvedTableScan of T1 is rewritten to read those needed keys
// and then make the needed STRUCTs with a ProjectScan.
//
// ResolvedArrayScans of TableRefTypes iterate over the joined rows, producing
// ROW<T2> rows as output. These ResolvedArrays are rewritten as
// ResolvedJoinScans, joining a ResolvedTableScan of T2 with a
// ResolvedProjectScan producing STRUCTs binding in all required columns (as
// described in the RowType rewrite above).
//
// TABLE types with UNIQUE also support ResolvedGetRowField expressions
// that read a column of the row.  These are rewritten as expression subqueries
// that fetch the row (found using the keys bound in the STRUCT) and return
// the requested column. (If no row is found, the subquery returns NULL.)
//
// TODO: This could potentially be optimized to fetch multiple
// columns earlier with a single subquery, rather than using a separate
// subquery to fetch each column (and rely on engines to optimize that).
//
// STRUCT optimization
// -------------------
// The rewrites described above always make STRUCTs binding in the list of
// required columns. In many cases, those STRUCTs would just have a single
// column. In those cases, we omit the STRUCT and just pass around that
// single value.  MakeStruct and GetStructField expressions are omitted,
// and the bound columns can be read directly with a ColumnRef.
//
// TODO: This optimization is turned off, because that makes the test output
// more consistent and understandable. Not all code in this rewriter is tested
// with this optimization, so care is required to enable it.
//
// How it the implementation works
// -------------------------------
// The `State` class holds state across rewrite passes. It's mostly a map
// holding a RewriteTypeState for each type being rewritten. RewriteTypeState
// tracks how the type gets used (e.g. what columns are accessed under it),
// and holds its replacement Type, once computed.
//
// The rewrite happens in 4 steps.
//
// 1. Traverse the resolved AST to collect information.
//    (RowTypeCollectorVisitor)
//    - The list of all RowOrTableTypes that occur.
//    - For each RowOrTableType, the list of Columns extracted from it with
//      ResolvedGetRowField operations.
//
// 2. Process the collected state and compute replacement types.
//    (State::MakeReplacementTypes)
//    - For each RowOrTableType, derive the replacement STRUCT type, with the
//      list of Columns it needs to bind in.
//
// 3. Traverse the resolved AST and rewrite all nodes that actively process
//    RowOrTableTypes. (RowTypeRewriterVisitor)
//    - Rewrite TableScans, ArrayScans, GetRowField, and Flatten to produce or
//      consume replacement STRUCTs rather than RowOrTableType.
//
// 4. Traverse the resolved AST and rewrite everything else that propagates
//    RowOrTableTypes. (RowTypeColumnRewriterVisitor)
//    - Every ResolvedColumn with a RowOrTableType type is replaced by a column
//      with its replacement type.  This creates replacement columns lazily.
//    - For every Type field (e.g. Types in ResolvedExpression, including
//      ResolvedColumnRefs), if the Type is a RowOrTableType, replace it with
//      its replacement type.
//    - Update FunctionSignatures containing RowTypes to use their replacement
//      types.
//
// The rewrites are all done locally and independently. Intermediate resolved
// ASTs may be invalid, but after all rewrites are done, input and output
// ResolvedColumns and Types line up for all nodes and the Resolved AST is
// valid.
//
// All column propagation happens automatically, including in operations that
// create new ResolvedColumns (derived from old columns with RowTypes),
// including in columns produced by CTEs.

// TODO: Issues to fix before removing in_devlopment for RowType:
//   - Remove all ABSL_LOG statements or make them VLOGs. They are useful while this
//     is in development.

// TODO: TableRefType support is incomplete, even after this
// rewriter has in_development=true removed. Known issues:
//   - Support+test or block TableRefType with nested RowType.
//   - Support+test annotations (e.g. collation) in TableRefType code paths.

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

bool HasRewriteType(const FunctionArgumentType& arg_type) {
  return arg_type.type() != nullptr && arg_type.type()->IsRowOrTable();
}

// State collected for a RowOrTableType being written.
// This holds the replacement_type (after State::MakeReplacementTypes).
// This tracks the Columns referenced through this RowOrTableType.
class RewriteTypeState {
 public:
  explicit RewriteTypeState(AnnotationPropagator& annotation_propagator)
      : annotation_propagator_(annotation_propagator) {};
  // Get the Type this RowOrTableType is rewritten to.
  const Type* replacement_type() const {
    ABSL_DCHECK(replacement_type_ != nullptr)
        << "Called replacement_type() before MakeReplacementTypes";
    return replacement_type_;
  }
  // Get the AnnotationMap for the type this RowOrTableType is rewritten to.
  const AnnotationMap* replacement_type_annotation_map() const {
    ABSL_DCHECK(replacement_type_ != nullptr)
        << "Called replacement_type_annotation_map() before "
           "MakeReplacementTypes";
    return replacement_type_annotation_map_;
  }

  // Return true if this RowOrTableType is rewritten to a STRUCT.
  // False means it's a single value that doesn't need a STRUCT wrapper.
  bool made_struct() const { return made_struct_; }

  // Get the Columns referenced through this RowOrTableType.
  const std::vector<const Column*>& GetReferencedTableColumns() const {
    return table_columns_;
  }

  // Add a Column referenced through this RowOrTableType.
  void AddReferencedTableColumn(const Column* table_column) {
    if (googlesql_base::InsertIfNotPresent(&table_columns_map_, table_column,
                                table_columns_map_.size())) {
      table_columns_.push_back(table_column);
    }
  }

  // Get the unique index for a Column referenced through this RowOrTableType.
  // This is the field number for that column in the replacement STRUCT.
  absl::StatusOr<int> GetFieldIdxForReferencedTableColumn(
      const Column* table_column) const {
    GOOGLESQL_RET_CHECK(made_struct_);

    const int* field_idx = googlesql_base::FindOrNull(table_columns_map_, table_column);
    GOOGLESQL_RET_CHECK(field_idx != nullptr)
        << "Missing column: " << table_column->FullName();
    return *field_idx;
  }

  // Make a ResolvedExpr for the replacement object for this RowOrTableType.
  // If multiple column values are needed, this will make a STRUCT.
  // If only one column value is needed, that value will be used directly.
  // `fields` are ResolvedExprs for the input columns.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeStructIfNecessary(
      std::vector<std::unique_ptr<const ResolvedExpr>> fields) const {
    if (made_struct_) {
      GOOGLESQL_RET_CHECK(replacement_type_->IsStruct());
      GOOGLESQL_RET_CHECK_EQ(replacement_type_->AsStruct()->num_fields(), fields.size());
      std::unique_ptr<ResolvedMakeStruct> make_struct_expr =
          MakeResolvedMakeStruct(replacement_type_, std::move(fields));
      GOOGLESQL_RETURN_IF_ERROR(annotation_propagator_.CheckAndPropagateAnnotations(
          /*error_node=*/nullptr, make_struct_expr.get()));
      return make_struct_expr;
    } else {
      GOOGLESQL_RET_CHECK_EQ(fields.size(), 1);
      return std::move(fields[0]);
    }
  }

  std::string DebugString(const RowOrTableType* rewrite_type) const {
    std::string result;
    if (rewrite_type->IsTable()) {
      const TableRefType* table_type = rewrite_type->AsTableRefType();
      absl::StrAppend(
          &result, "\n    bound_columns: ",
          DebugStringColumnNames(table_type->bound_columns()),
          "\n    bound_source_table: ",
          table_type->bound_source_table()->FullName(),
          "\n    bound_source_columns: ",
          DebugStringColumnNames(table_type->bound_source_columns()));
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

  // The Type that this RewriteTypeState represents.
  const RowOrTableType* type_ = nullptr;

  // The Column corresponding to the ResolvedGetRowField that produced this.
  // Nullptr if this was not extracted by ResolvedGetRowField.
  const Column* column_ = nullptr;

  // Non-RowType, fields accessed from this RowType.
  // Ordered for determinism.
  absl::linked_hash_set<const Column*> non_row_field_accesses_;

  // RowType fields accessed from this RowOrTableType.
  // Ordered for determinism.
  absl::linked_hash_map<const RowType*, RewriteTypeState*> row_children_;

  // Ordered list of unique Columns that will be needed in GetRowField calls
  // on this RowOrTableType.  The order makes output deterministic.
  std::vector<const Column*> table_columns_;

  // Map each Column to its index in `table_columns_`.
  absl::flat_hash_map<const Column*, int> table_columns_map_;

  // The replacement type for this RowOrTableType, if needed.
  // If multiple fields are needed, it will be a STRUCT.
  // If only one field is needed, we can bypass making a STRUCT and just use
  // that field's type directly.  `made_struct_` will be false in this case.
  const Type* replacement_type_ = nullptr;
  // Annotation map for the replacement type.
  const AnnotationMap* replacement_type_annotation_map_ = nullptr;

  bool made_struct_ = false;
  // When `made_struct_` is false, this records the name that would have been
  // used as the struct field name (from the column that would have been bound).
  std::string struct_field_name_;

  AnnotationPropagator& annotation_propagator_;

  friend class State;
};

// State carried across phases of the rewriter.
class State {
 public:
  explicit State(ColumnFactory& column_factory,
                 AnnotationPropagator& annotation_propagator)
      : column_factory_(column_factory),
        annotation_propagator_(annotation_propagator) {}

  // Get the RewriteTypeState for a RowOrTableType.  It must exist already.
  // The returned pointers are always non-null.
  // These could return references but that doesn't build in googlesql.
  absl::StatusOr<const RewriteTypeState*> GetRewriteTypeState(
      const RowOrTableType* rewrite_type) const {
    const std::unique_ptr<RewriteTypeState>* rewrite_type_state_ptr =
        googlesql_base::FindOrNull(rewrite_type_state_map_, rewrite_type);
    GOOGLESQL_RET_CHECK(rewrite_type_state_ptr != nullptr)
        << "RewriteTypeState not found for " << rewrite_type->DebugString();
    GOOGLESQL_RET_CHECK(*rewrite_type_state_ptr != nullptr);
    return rewrite_type_state_ptr->get();
  }
  absl::StatusOr<RewriteTypeState*> GetMutableRewriteTypeState(
      const RowOrTableType* rewrite_type) {
    const std::unique_ptr<RewriteTypeState>* rewrite_type_state_ptr =
        googlesql_base::FindOrNull(rewrite_type_state_map_, rewrite_type);
    GOOGLESQL_RET_CHECK(rewrite_type_state_ptr != nullptr)
        << "RewriteTypeState not found for " << rewrite_type->DebugString();
    GOOGLESQL_RET_CHECK(*rewrite_type_state_ptr != nullptr);
    return rewrite_type_state_ptr->get();
  }

  // Register a RowOrTableType if it isn't already, return its RewriteTypeState.
  //
  // `is_root` indicates this Type was seen sourced from an underlying table,
  //   (e.g. via a scan), not nested from another RowType.
  RewriteTypeState& RegisterRewriteType(const RowOrTableType* rewrite_type,
                                        bool is_root) {
    if (is_root && rewrite_type->IsRow()) {
      root_row_types_.insert(rewrite_type->AsRowType());
    }
    return AddOrGetRewriteTypeState(rewrite_type);
  }

  // Register the rewrite types for a ResolvedGetRowField node.
  absl::Status RegisterGetRowField(const RowOrTableType* parent_type,
                                   const Type* child_type,
                                   const Column* column) {
    RewriteTypeState& parent_type_state =
        RegisterRewriteType(parent_type, /*is_root=*/false);
    if (parent_type->IsTable()) {
      // For RowTypes, referenced columns are registered when the replacement
      // type is created.
      parent_type_state.AddReferencedTableColumn(column);
    }

    if (!child_type->IsRow()) {
      parent_type_state.non_row_field_accesses_.insert(column);
    }
    if (!child_type->IsRowOrTable()) {
      return absl::OkStatus();
    }

    const RowOrTableType* child_type_rt = child_type->AsRowOrTable();
    // Reading a RowType from a TableRefType is not defined or tested.
    GOOGLESQL_RET_CHECK(!child_type->IsRow() || !parent_type->IsTable());
    RewriteTypeState& child_type_state =
        RegisterRewriteType(child_type_rt, false);
    if (child_type_rt->IsRow()) {
      // ResolvedGetRowField guarantees produced RowTypes are unique.
      GOOGLESQL_RET_CHECK(child_type_state.column_ == nullptr);
    }
    GOOGLESQL_RET_CHECK(child_type_state.column_ == nullptr ||
              child_type_state.column_ == column);
    child_type_state.column_ = column;

    // Record the RowType parent:child nesting relationship.
    if (child_type_rt->IsRow()) {
      parent_type_state.row_children_[child_type_rt->AsRowType()] =
          &child_type_state;
    }

    return absl::OkStatus();
  }

  absl::Status ValidateAccessPathGraph() {
    absl::flat_hash_set<const RowType*> visited;

    // Helper lambda for recursive validation on a tree.
    //
    // Validates that there are no cycles or multiple paths to the same node
    // in the subgraph rooted at `node`.
    std::function<absl::Status(const RewriteTypeState&)> validate_tree =
        [&](const RewriteTypeState& node) -> absl::Status {
      const RowType* row_type = node.type_->AsRowType();
      GOOGLESQL_RET_CHECK(row_type != nullptr);
      if (!visited.insert(row_type).second) {
        return absl::InternalError(
            absl::StrCat("Cycle or multiple paths detected at RowType: ",
                         row_type->DebugString()));
      }
      for (const auto& [child_row_type, child_node] : node.row_children_) {
        GOOGLESQL_RETURN_IF_ERROR(validate_tree(*child_node));
      }
      return absl::OkStatus();
    };

    // Validate each RowType root and its subtree.
    for (const RowType* root : root_row_types_) {
      auto it = rewrite_type_state_map_.find(root);
      GOOGLESQL_RET_CHECK(it != rewrite_type_state_map_.end())
          << "Root not found in graph: " << root->DebugString();
      GOOGLESQL_RETURN_IF_ERROR(validate_tree(*it->second));
    }

    // Validate that all RowType nodes in the graph are reachable from a root.
    for (const auto& [type, node] : rewrite_type_state_map_) {
      const RowType* row_type = type->AsRowType();
      GOOGLESQL_RET_CHECK(row_type == nullptr || visited.contains(row_type))
          << "Path to RowType not found in graph: " << row_type->DebugString();
    }

    return absl::OkStatus();
  }

  // Get the replacement column for a ResolvedColumn that has a RowOrTableType.
  // Make that replacement column if we haven't seen `orig_column` before.
  // This works in the second-pass visitor after RowOrTableType replacements
  // have been created with MakeReplacementTypes.
  absl::StatusOr<ResolvedColumn> FindOrAddReplacementColumn(
      const ResolvedColumn& orig_column) {
    GOOGLESQL_RET_CHECK(orig_column.type()->IsRowOrTable());

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
  // Make the replacement_type_ for each rewrite_type found in the first pass.
  absl::Status MakeReplacementTypes(TypeFactory& type_factory,
                                    bool tableref_enabled) {
    GOOGLESQL_RET_CHECK_EQ(rewrite_types_.size(), rewrite_type_state_map_.size());

    // The first pass handles the TableRefTypes.  The second pass for RowTypes
    // may reference those as column types under the RowTypes (for join columns
    // that are columns on the ROW's table).
    for (const RowOrTableType* rewrite_type : rewrite_types_) {
      if (!rewrite_type->IsTable()) {
        continue;
      }
      GOOGLESQL_RET_CHECK(tableref_enabled);
      const TableRefType* table_type = rewrite_type->AsTableRefType();

      GOOGLESQL_ASSIGN_OR_RETURN(RewriteTypeState * rewrite_type_state,
                       GetMutableRewriteTypeState(rewrite_type));
      GOOGLESQL_RET_CHECK(rewrite_type_state->replacement_type_ == nullptr);

      // For ROW types representing join columns, the struct includes the
      // `bound_columns` from the RowOrTableType.
      const Table* table = table_type->table();
      // So far, we only support joins to tables with DEFAULT ColumnListMode.
      GOOGLESQL_RET_CHECK(table->GetColumnListMode() == Table::ColumnListMode::DEFAULT);
      GOOGLESQL_RET_CHECK(!table_type->bound_columns().empty());
      GOOGLESQL_RET_CHECK(!table_type->bound_source_columns().empty());

      std::vector<StructType::StructField> struct_fields;
      for (const Column* column : table_type->bound_columns()) {
        GOOGLESQL_RET_CHECK(!column->GetType()->IsRowOrTable());
        struct_fields.emplace_back(column->Name(), column->GetType());
      }
      GOOGLESQL_RET_CHECK(!struct_fields.empty());

      // TODO: Enable skipping STRUCT types when not needed.
      if (/* DISABLES CODE */ (true) || struct_fields.size() != 1) {
        GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(
            struct_fields, &rewrite_type_state->replacement_type_));
        rewrite_type_state->made_struct_ = true;
      } else {
        rewrite_type_state->replacement_type_ = struct_fields[0].type;
        rewrite_type_state->made_struct_ = false;
        rewrite_type_state->struct_field_name_ = struct_fields[0].name;
      }

      GOOGLESQL_RET_CHECK(rewrite_type_state->replacement_type_ != nullptr);
      ABSL_LOG(ERROR) << "Made replacement_type for " << table_type->DebugString()
                 << ": "
                 << rewrite_type_state->replacement_type_->DebugString();
    }

    // The second pass handles the RowTypes.
    GOOGLESQL_RETURN_IF_ERROR(ValidateAccessPathGraph());
    // Recursively merge the root RowTypes and transitive children.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<CatalogColumnNode>> merged_roots,
        MergeAccessPathGraph());
    // Recursively compute replacement types for root RowTypes and transitive
    // children.
    for (const auto& root : merged_roots) {
      GOOGLESQL_RETURN_IF_ERROR(MakeRowTypeReplacementTypes(*root, type_factory));
    }

    return absl::OkStatus();
  }

  std::string DebugString() const {
    std::string result = "RowOrTableType rewrite state:";

    absl::StrAppend(&result, "\nRow types:");
    for (const RowOrTableType* rewrite_type : rewrite_types_) {
      absl::StrAppend(&result, "\n  ", rewrite_type->DebugString());
      const std::unique_ptr<RewriteTypeState>* rewrite_type_state_ptr =
          googlesql_base::FindOrNull(rewrite_type_state_map_, rewrite_type);
      if (rewrite_type_state_ptr == nullptr) continue;
      if (*rewrite_type_state_ptr == nullptr) continue;
      absl::StrAppend(&result,
                      (*rewrite_type_state_ptr)->DebugString(rewrite_type));
    }

    absl::StrAppend(&result, "\ncolumn_replacement_map:");
    for (const auto& it : column_replacement_map_) {
      absl::StrAppend(&result, "\n  ", ColDebugString(it.first), " -> ",
                      ColDebugString(it.second));
    }
    return result;
  }

  ColumnFactory& column_factory() { return column_factory_; }
  AnnotationPropagator& annotation_propagator() {
    return annotation_propagator_;
  }

 private:
  ColumnFactory& column_factory_;
  AnnotationPropagator& annotation_propagator_;

  // List types found that will be rewritten.  This gives a deterministic order,
  // which we don't get from keys of the map.
  std::vector<const RowOrTableType*> rewrite_types_;

  // RewriteTypeState for each RowOrTableType seen.
  // For non-join TableScans, the resolver creates a unique RowOrTableType
  // instance for each unique TableScan. The value is a unique_ptr so the
  // objects won't move around.
  absl::flat_hash_map<const RowOrTableType*, std::unique_ptr<RewriteTypeState>>
      rewrite_type_state_map_;

  // Set of root RowTypes seen. Root types are the source RowTypes produced by
  // scans, and not extracted from another RowType.
  absl::flat_hash_set<const RowType*> root_row_types_;

  // Map of ResolvedColumns to replacement ResolvedColumns.
  // This will get an entry for every ResolvedColumn with a RowOrTableType.
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> column_replacement_map_;

  // Get the RewriteTypeState for a RowOrTableType. Create it if necessary.
  RewriteTypeState& AddOrGetRewriteTypeState(
      const RowOrTableType* rewrite_type) {
    std::unique_ptr<RewriteTypeState>& ptr =
        rewrite_type_state_map_[rewrite_type];
    if (ptr == nullptr) {
      rewrite_types_.push_back(rewrite_type);
      ptr = std::make_unique<RewriteTypeState>(annotation_propagator_);
      ptr->type_ = rewrite_type;
    }
    return *ptr;
  }

  // This makes a replacement ResolvedColumn for a ResolvedColumn with
  // RowOrTableType. This does not add the new column in the map.
  absl::StatusOr<ResolvedColumn> MakeReplacementColumn(
      const ResolvedColumn& orig_column) const {
    GOOGLESQL_RET_CHECK(orig_column.type()->IsRowOrTable());
    const RowOrTableType* rewrite_type = orig_column.type()->AsRowOrTable();
    GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                     GetRewriteTypeState(rewrite_type));

    const Type* replacement_type_ = rewrite_type_state->replacement_type_;
    GOOGLESQL_RET_CHECK(replacement_type_ != nullptr);

    std::string field_suffix;
    /* TODO: Enable non-STRUCT simplification, and handle naming.
    if (!rewrite_type_state->made_struct()) {
      GOOGLESQL_RET_CHECK(!rewrite_type_state->struct_field_name_.empty());
      field_suffix = absl::StrCat("$", rewrite_type_state->struct_field_name_);
    }
    */
    GOOGLESQL_RET_CHECK(rewrite_type_state->made_struct());
    if (!rewrite_type->IsTable()) {
      field_suffix = "$scanrow";
    } else {
      field_suffix =
          rewrite_type->IsMultiRowTable() ? "$join_multirow" : "$join_row";
    }

    ResolvedColumn new_column = column_factory_.MakeCol(
        orig_column.table_name(),
        absl::StrCat(orig_column.name().starts_with('$') ? "" : "$",
                     orig_column.name(), field_suffix),
        AnnotatedType(replacement_type_,
                      rewrite_type_state->replacement_type_annotation_map_));

    ABSL_LOG(INFO) << "Made replacement column: " << ColDebugString(orig_column)
              << " -> " << ColDebugString(new_column);
    return new_column;
  }

  // Represents a merged row field access from the same root type and via the
  // same path of Column field accesses.
  struct CatalogColumnNode {
    // All RowType instances that were merged into this node.
    absl::flat_hash_set<const RowType*> row_types;

    // Non-RowType fields accessed from this column across all merged RowTypes.
    // Ordered for determinism.
    absl::linked_hash_set<const Column*> non_row_field_accesses;

    // Merged child nodes, mapped by their Column.
    // Ordered for determinism.
    absl::linked_hash_map<const Column*, std::unique_ptr<CatalogColumnNode>>
        children;
  };

  // Helper to merge a set of `RewriteTypeState` objects for equivalent
  // RowTypes, recursively merge their children.
  absl::StatusOr<std::unique_ptr<CatalogColumnNode>>
  MergeAccessPathGraphInternal(
      const std::vector<const RewriteTypeState*>& nodes) {
    GOOGLESQL_RET_CHECK(!nodes.empty());
    const Column* column = nodes[0]->column_;
    auto catalog_node = std::make_unique<CatalogColumnNode>();

    // Track the Column groupings of all RowType children, so that we can
    // recursively merge each group of children.
    absl::linked_hash_map<const Column*, std::vector<const RewriteTypeState*>>
        children_groups;

    // Process all input nodes.
    for (const RewriteTypeState* node : nodes) {
      // Validate.
      GOOGLESQL_RET_CHECK_EQ(node->column_, column)
          << "Attempted to merge nodes with different columns: "
          << (node->column_ ? node->column_->FullName() : "nullptr") << " vs "
          << (column ? column->FullName() : "nullptr");
      GOOGLESQL_RET_CHECK(node->type_->IsRow()) << "Attempted to merge non-RowType node";

      // Track all RowTypes that this grouping refers to.
      catalog_node->row_types.insert(node->type_->AsRowType());

      // Aggregate all non-RowType field accesses.
      for (const Column* col : node->non_row_field_accesses_) {
        catalog_node->non_row_field_accesses.insert(col);
      }

      // Add all RowType children to the appropriate child group for recursive
      // processing.
      for (const auto& [child_row_type, child_node] : node->row_children_) {
        children_groups[child_node->column_].push_back(child_node);
      }
    }

    // Recursively merge all groups of children.
    for (const auto& [child_column, child_nodes] : children_groups) {
      GOOGLESQL_ASSIGN_OR_RETURN(auto merged_child,
                       MergeAccessPathGraphInternal(child_nodes));
      GOOGLESQL_RET_CHECK(
          catalog_node->children.insert({child_column, std::move(merged_child)})
              .second);
    }

    return catalog_node;
  }

  // Recursively merge the root RowTypes and their transitive children, grouping
  // identical paths (e.g., multiple occurrences of `r1.r2`).
  //
  // This is required because each ResolvedGetRowField produces a unique RowType
  // pointer, we need to merge RowTypes which are equivalent (come from the same
  // root and same path of ResolvedGetRowField Column accesses).
  absl::StatusOr<std::vector<std::unique_ptr<CatalogColumnNode>>>
  MergeAccessPathGraph() {
    std::vector<std::unique_ptr<CatalogColumnNode>> merged_roots;
    for (const RowType* root : root_row_types_) {
      GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                       GetRewriteTypeState(root));
      GOOGLESQL_ASSIGN_OR_RETURN(auto merged_root,
                       MergeAccessPathGraphInternal({rewrite_type_state}));
      merged_roots.push_back(std::move(merged_root));
    }
    return merged_roots;
  }

  // Helper to recursively compute replacement Struct types starting from a
  // merged CatalogColumnNode.
  //
  // The structure of the computed STRUCT type is:
  //   STRUCT<
  //     non_row_field_1 TYPE, ..., non_row_field_N TYPE,
  //     nested_row_field_1 STRUCT<...>, ..., nested_row_field_M STRUCT<...>
  //   >
  //
  // Where:
  // - Direct non-RowType field accesses (stored in `non_row_field_accesses`)
  //   are placed first, in the order they were first encountered.
  // - Nested RowType field accesses (stored in `children`) are placed next,
  //   also in the order they were first encountered.
  //
  // Ordering ensures that the generated Struct type is deterministic.
  absl::Status MakeRowTypeReplacementTypes(const CatalogColumnNode& node,
                                           TypeFactory& type_factory) {
    // Recursively make the replacement types for all RowType children, so
    // their replacement types are available.
    for (const auto& [column, child] : node.children) {
      GOOGLESQL_RETURN_IF_ERROR(MakeRowTypeReplacementTypes(*child, type_factory));
    }

    size_t num_fields =
        node.non_row_field_accesses.size() + node.children.size();
    std::vector<StructType::StructField> struct_fields;
    struct_fields.reserve(num_fields);
    std::vector<const Column*> ordered_columns;
    ordered_columns.reserve(num_fields);
    std::vector<const AnnotationMap*> annotation_maps;
    annotation_maps.reserve(num_fields);

    // Add all non-RowType children to the computed Struct type.
    for (const Column* col : node.non_row_field_accesses) {
      const Type* type = col->GetType();
      if (type->IsTable()) {
        // If the field access returns a TableRefType, we need to use its
        // replacement type inside the rewritten Struct of the row type.
        GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                         GetRewriteTypeState(type->AsTableRefType()));
        type = rewrite_type_state->replacement_type_;
      }
      struct_fields.push_back(StructType::StructField(col->Name(), type));
      ordered_columns.push_back(col);
      annotation_maps.push_back(col->GetTypeAnnotationMap());
    }

    // Add all RowType children to the computed STRUCT type.
    for (const auto& [column, child] : node.children) {
      GOOGLESQL_RET_CHECK(!child->row_types.empty());
      const RowType* child_row_type = *child->row_types.begin();
      GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                       GetRewriteTypeState(child_row_type));
      const Type* child_replacement_type =
          rewrite_type_state->replacement_type_;

      struct_fields.push_back(
          StructType::StructField(column->Name(), child_replacement_type));
      ordered_columns.push_back(column);
      annotation_maps.push_back(
          rewrite_type_state->replacement_type_annotation_map_);
    }

    // Make the replacement Struct type.
    const StructType* struct_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(struct_fields, &struct_type));

    // Build the Struct AnnotationMap from the child fields annotations.
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_type);
    StructAnnotationMap* struct_annotation_map = annotation_map->AsStructMap();
    GOOGLESQL_RET_CHECK(struct_annotation_map != nullptr);
    bool has_any_annotations = false;
    for (int i = 0; i < annotation_maps.size(); ++i) {
      if (!AnnotationMap::IsNullOrEmpty(annotation_maps[i])) {
        has_any_annotations = true;
        GOOGLESQL_RETURN_IF_ERROR(
            struct_annotation_map->CloneIntoField(i, annotation_maps[i]));
      }
    }
    // Only set the annotation map on the Struct if any field had annotations.
    const AnnotationMap* annotation_map_ptr = nullptr;
    if (has_any_annotations) {
      GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_ptr,
                       type_factory.TakeOwnership(std::move(annotation_map)));
    }

    // Set the replacement Struct info on all merged RowTypes.
    for (const RowType* row_type : node.row_types) {
      GOOGLESQL_ASSIGN_OR_RETURN(RewriteTypeState * rewrite_type_state,
                       GetMutableRewriteTypeState(row_type));
      rewrite_type_state->replacement_type_ = struct_type;
      rewrite_type_state->replacement_type_annotation_map_ = annotation_map_ptr;
      rewrite_type_state->made_struct_ = true;
      for (const Column* read_col : ordered_columns) {
        // Record the Columns and their index in the rewrite Struct.
        rewrite_type_state->AddReferencedTableColumn(read_col);
      }
    }

    return absl::OkStatus();
  }
};

// This first-pass visitor collects the RowOrTableTypes, and lists of all
// Columns read from each RowOrTableType with ResolvedGetRowField.
class RowTypeCollectorVisitor : public ResolvedASTVisitor {
 public:
  explicit RowTypeCollectorVisitor(State& state, bool tableref_enabled)
      : state_(state), tableref_enabled_(tableref_enabled) {}

  // Find all RowOrTableTypes created by ResolvedTableScan and register them.
  // They can be created for the table with `read_as_row_type` or for
  // join columns in the `column_list`.
  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    ABSL_LOG(INFO) << "VisitResolvedTableScan";

    for (const ResolvedColumn& column : node->column_list()) {
      if (column.type()->IsRow()) {
        GOOGLESQL_RET_CHECK_EQ(node->column_list_size(), 1);
        GOOGLESQL_RET_CHECK(node->read_as_row_type());
        const RowType* rewrite_type = column.type()->AsRowType();
        state_.RegisterRewriteType(rewrite_type, /*is_root=*/true);
      } else if (column.type()->IsTable()) {
        GOOGLESQL_RET_CHECK(tableref_enabled_);
        const TableRefType* rewrite_type = column.type()->AsTableRefType();
        state_.RegisterRewriteType(rewrite_type, /*is_root=*/true);
      }
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedTVFScan(const ResolvedTVFScan* node) override {
    ABSL_LOG(INFO) << "VisitResolvedTvfScan";

    for (const ResolvedColumn& column : node->column_list()) {
      if (column.type()->IsRow()) {
        const RowType* rewrite_type = column.type()->AsRowType();
        state_.RegisterRewriteType(rewrite_type, /*is_root=*/true);
      } else if (column.type()->IsTable()) {
        GOOGLESQL_RET_CHECK_FAIL() << "TVF returning a TableRef Type is not supported";
      }
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedArrayScan(const ResolvedArrayScan* node) override {
    ABSL_LOG(INFO) << "VisitResolvedArrayScan";

    // ArrayScan over TableRefType can only have 1 item in array_expr_list.
    if (node->array_expr_list_size() != 1 ||
        !node->array_expr_list()[0]->type()->IsTable()) {
      return ResolvedASTVisitor::VisitResolvedArrayScan(node);
    }
    GOOGLESQL_RET_CHECK(tableref_enabled_);

    GOOGLESQL_RET_CHECK(node->element_column_list_size() == 1);
    const ResolvedColumn& column = node->element_column_list()[0];
    GOOGLESQL_RET_CHECK(!column.type()->IsTable());

    if (column.type()->IsRow()) {
      // ArrayScan over TableRefType is akin (and will be rewritten into) a
      // TableScan, so this is the source for the RowType.
      state_.RegisterRewriteType(column.type()->AsRowType(), /*is_root=*/true);
    }

    return ResolvedASTVisitor::VisitResolvedArrayScan(node);
  }

  // Register the RowOrTableTypes referenced or returned by ResolvedGetRowField.
  // Also record the Columns read of each RowOrTableType.
  absl::Status VisitResolvedGetRowField(
      const ResolvedGetRowField* node) override {
    ABSL_LOG(INFO) << "VisitResolvedGetRowField";

    const RowOrTableType* parent_type = node->expr()->type()->AsRowOrTable();
    GOOGLESQL_RET_CHECK(parent_type != nullptr);
    GOOGLESQL_RET_CHECK(tableref_enabled_ || !parent_type->IsTable());
    const Type* child_type = node->type();
    GOOGLESQL_RET_CHECK(child_type != nullptr);
    GOOGLESQL_RET_CHECK(tableref_enabled_ || !child_type->IsTable());
    const Column* column = node->column();
    GOOGLESQL_RET_CHECK(column != nullptr);

    GOOGLESQL_RETURN_IF_ERROR(
        state_.RegisterGetRowField(parent_type, child_type, column));

    return node->ChildrenAccept(this);
  }

 private:
  State& state_;

  // Whether TableRef Type is enabled.
  bool tableref_enabled_;
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
  // ReadColumnSet.  `rewrite_type_state` is used to get the replacement_type
  // for the replacement struct.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ReadStructWithColumns(
      const std::vector<const Column*>& columns,
      const RewriteTypeState* rewrite_type_state) {
    GOOGLESQL_RET_CHECK(!columns.empty());

    std::vector<std::unique_ptr<const ResolvedExpr>> struct_field_exprs;
    for (const Column* bound_column : columns) {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn resolved_column,
                       GetResolvedColumn(bound_column));
      ABSL_LOG(INFO) << "Added column in ReadStructWithColumns: "
                << ColDebugString(resolved_column);

      // This MakeResolvedColumnRef builder propagates annotations.
      struct_field_exprs.push_back(
          MakeResolvedColumnRef(resolved_column, /*is_correlated=*/false));
    }
    // Both ColumnRefs are reported as already using `replacement_type` here
    // so that comparison will be allowed.
    GOOGLESQL_ASSIGN_OR_RETURN(auto make_struct_expr,
                     rewrite_type_state->MakeStructIfNecessary(
                         std::move(struct_field_exprs)));
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

// The second-pass rewriter replaces the nodes that act directly on
// RowOrTableTypes.
// This includes:
// * Replace ResolvedTableScans, returning replacement STRUCTs.
// * Replace ResolvedGetRowField with one of:
//   - A ResolvedGetStructField, to get a field out of a replacement STRUCT.
//   - A ResolvedColumnRef, if the replacement was a single non-STRUCT value.
//   - A subquery, if this requires expanding a TABLE to multiple ROWs.
// * Replace ResolvedArrayScan of a TableRefType with a subquery fetching
//   rows of the joined table.
class RowTypeRewriterVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit RowTypeRewriterVisitor(State& state,
                                  FunctionCallBuilder& function_call_builder,
                                  TypeFactory& type_factory,
                                  bool tableref_enabled)
      : state_(state),
        tableref_enabled_(tableref_enabled),
        function_call_builder_(function_call_builder),
        type_factory_(type_factory) {}

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedTableScan(
      std::unique_ptr<const ResolvedTableScan> node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedTVFScan(
      std::unique_ptr<const ResolvedTVFScan> node) override;

  absl::Status PreVisitResolvedGetRowField(
      const ResolvedGetRowField& node) override {
    const Type* expr_type = node.expr()->type();
    GOOGLESQL_RET_CHECK(expr_type->IsRowOrTable());
    getrowfield_original_type_stack_.push(expr_type->AsRowOrTable());
    return absl::OkStatus();
  }

  absl::Status PreVisitResolvedArrayScan(
      const ResolvedArrayScan& node) override {
    // ArrayScans for TableRefType must have exactly 1 expr. We also track
    // non-TableRefType exprs here, so that PostVisitResolvedArrayScan can get
    // the pre-rewrite type and directly check if it's a TableRefType.
    if (node.array_expr_list_size() == 1) {
      arrayscan_original_type_stack_.push(node.array_expr_list()[0]->type());
    }
    return absl::OkStatus();
  }

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
  // table for the original ResolvedTableScan.  This can include other columns
  // with non-rewrite types.
  //
  // `orig_table_columns` matches 1:1 with `orig_resolved_columns`.
  // For non-rewrite types, this is the Column being read.
  // Row rewrite types, this can be nullptr.  (There is no Column for a
  // ResolvedColumn produced as a `read_as_row_type` read.)
  absl::StatusOr<std::unique_ptr<const ResolvedScan>> MakeRewrittenTableScan(
      const Table* table,
      absl::Span<const ResolvedColumn> orig_resolved_columns,
      const std::vector<const Column*>& orig_table_columns,
      absl::string_view alias = "");

  // Make the rewrite expression for the input column with `rewrite_type`.
  // This adds any needed input columns to `read_columns_set`, and then
  // makes a ResolvedExpr to build the output replacement type for
  // `rewrite_type`.
  //
  // This can be called recursively once (if `is_inner` is false) to do the
  // same for join columns inside the ROW for table scan row.  (When a row
  // contains a join column, the row's replacement struct will have the
  // join column's replacement struct as one of its fields.)
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeRewriteExprForColumn(
      const RowOrTableType* rewrite_type, bool is_inner,
      ReadColumnsSet& read_columns_set);

  State& state_;

  // Whether TableRef Type is enabled.
  bool tableref_enabled_;

  FunctionCallBuilder& function_call_builder_;

  TypeFactory& type_factory_;

  // Used by PreVisitX functions to store the original types before rewriting
  // so that they can be used in the corresponding PostVisitX functions.
  std::stack<const RowOrTableType*> getrowfield_original_type_stack_;
  std::stack<const Type*> arrayscan_original_type_stack_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RowTypeRewriterVisitor::PostVisitResolvedTableScan(
    std::unique_ptr<const ResolvedTableScan> node) {
  // We need a rewrite if the TableScan has `read_as_row_type` or it
  // produces any RowOrTableType columns.
  bool need_rewrite = node->read_as_row_type();
  if (!need_rewrite) {
    for (const ResolvedColumn& column : node->column_list()) {
      if (column.type()->IsRowOrTable()) {
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
  GOOGLESQL_RET_CHECK(node->table_column_list_size() == 0);
  std::vector<const Column*> orig_table_columns;
  orig_table_columns.reserve(node->column_index_list_size());
  for (int column_idx : node->column_index_list()) {
    orig_table_columns.push_back(table->GetColumn(column_idx));
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
RowTypeRewriterVisitor::PostVisitResolvedTVFScan(
    std::unique_ptr<const ResolvedTVFScan> node) {
  bool need_rewrite = false;
  // Unreferenced columns may be pruned from the column list, so look at the
  // result schema to see if any RowTypes need to be rewritten.
  for (const TVFSchemaColumn& column :
       node->signature()->result_schema().columns()) {
    GOOGLESQL_RET_CHECK(!column.type->IsTable())
        << "RowType rewriter does not support TableRefType in TVFScan result "
           "schema";
    if (column.type->IsRow()) {
      need_rewrite = true;
    }
  }
  if (!need_rewrite) {
    return std::move(node);
  }
  // TVFs must set this option to indicate that it's semantically valid to
  // rewrite the ROW type to an equivalent STRUCT, and provide a callback the
  // rewriter can use to construct a new TVFSignature object.
  GOOGLESQL_RET_CHECK(node->signature()->options().row_type_rewrite_callback != nullptr);

  ResolvedTVFScanBuilder builder = ToBuilder(std::move(node));

  const TVFSignature& signature = *builder.signature();
  const TVFRelation& result_schema = signature.result_schema();
  std::vector<ResolvedColumn> column_list = builder.release_column_list();
  const std::vector<int>& column_index_list = builder.column_index_list();
  GOOGLESQL_RET_CHECK(column_index_list.size() == column_list.size());

  // Determining a replacement type for a ROW type depends on ResolvedColumn,
  // create a map from schema column index to column_list index. std::nullopt
  // represents column which was pruned from the column_list.
  std::vector<std::optional<int>> schema_idx_to_column_list_idx(
      result_schema.num_columns(), std::nullopt);
  for (int i = 0; i < column_index_list.size(); i++) {
    schema_idx_to_column_list_idx[column_index_list[i]] = i;
  }

  // In column list, replace ROW type with STRUCT replacement.
  for (int i = 0; i < column_list.size(); i++) {
    if (column_list[i].type()->IsRow()) {
      GOOGLESQL_ASSIGN_OR_RETURN(column_list[i],
                       state_.FindOrAddReplacementColumn(column_list[i]));
    }
  }

  // In signature result schema, replace ROW type with STRUCT replacement.
  GOOGLESQL_RET_CHECK(result_schema.num_columns() >= 1);
  std::vector<TVFSchemaColumn> columns;
  columns.reserve(result_schema.num_columns());
  for (int i = 0; i < result_schema.num_columns(); i++) {
    const Type* replacement_type;
    std::optional<int> column_list_idx = schema_idx_to_column_list_idx[i];
    if (!result_schema.column(i).type->IsRow()) {
      // Not a ROW: don't change the type.
      replacement_type = result_schema.column(i).type;
    } else if (column_list_idx.has_value()) {
      // Is a ROW, column is in column_list: use the type from column_list.
      const ResolvedColumn& column = column_list[*column_list_idx];
      replacement_type = column.type();
    } else {
      // Column was pruned from column_list, so must not be used in the AST:
      // replace with an empty STRUCT.
      GOOGLESQL_RETURN_IF_ERROR(type_factory_.MakeStructTypeFromVector(
          /*fields=*/{}, &replacement_type));
    }

    TVFSchemaColumn column_copy = result_schema.column(i);
    column_copy.type = replacement_type;
    columns.push_back(column_copy);
  }

  std::unique_ptr<TVFRelation> new_result_schema;
  if (result_schema.is_value_table()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const TVFRelation relation,
                     TVFRelation::ValueTable(std::move(columns)));
    new_result_schema = std::make_unique<TVFRelation>(std::move(relation));
  } else {
    new_result_schema = std::make_unique<TVFRelation>(std::move(columns));
  }

  // TVFSignature provides a callback to construct a new TVFSignature based on
  // the new result schema.
  GOOGLESQL_ASSIGN_OR_RETURN(auto new_signature,
                   signature.options().row_type_rewrite_callback(
                       signature, *new_result_schema));

  builder.set_column_list(column_list);
  builder.set_signature(new_signature);

  return std::move(builder).Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RowTypeRewriterVisitor::PostVisitResolvedGetRowField(
    std::unique_ptr<const ResolvedGetRowField> get_row_field) {
  ABSL_LOG(INFO) << "PostVisitResolvedGetRowField:\n"
            << get_row_field->DebugString();

  // Get the parent type before it was rewritten.
  GOOGLESQL_RET_CHECK(!getrowfield_original_type_stack_.empty());
  const RowOrTableType* rewrite_type = getrowfield_original_type_stack_.top();
  getrowfield_original_type_stack_.pop();

  GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                   state_.GetRewriteTypeState(rewrite_type));

  if (rewrite_type->IsRow()) {
    // The expr of a GetRowField must be a ColumnRef, or it could have been a
    // GetRowField, which would now have been rewritten to a GetStructField.
    // We can directly use a GetStructField expr, but need to create a
    // ColumnRef pointed to the replacement column.
    GOOGLESQL_RET_CHECK(get_row_field->expr()->Is<ResolvedColumnRef>() ||
              get_row_field->expr()->Is<ResolvedGetStructField>())
        << "\n"
        << get_row_field->DebugString();

    const Column* orig_field_column = get_row_field->column();
    auto builder = ToBuilder(std::move(get_row_field));
    std::unique_ptr<const ResolvedExpr> expr = builder.release_expr();
    GOOGLESQL_RET_CHECK_NE(expr, nullptr);

    if (expr->Is<ResolvedColumnRef>()) {
      const ResolvedColumnRef* orig_column_ref =
          expr->GetAs<ResolvedColumnRef>();
      const ResolvedColumn& orig_column = orig_column_ref->column();

      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn replacement_column,
                       state_.FindOrAddReplacementColumn(orig_column));

      // This MakeResolvedColumnRef builder propagates annotations.
      expr = MakeResolvedColumnRef(replacement_column,
                                   orig_column_ref->is_correlated());
    }

    if (rewrite_type_state->made_struct()) {
      GOOGLESQL_ASSIGN_OR_RETURN(int field_idx,
                       rewrite_type_state->GetFieldIdxForReferencedTableColumn(
                           orig_field_column));
      GOOGLESQL_RET_CHECK(rewrite_type_state->replacement_type_->IsStruct());
      const StructType* struct_type =
          rewrite_type_state->replacement_type_->AsStruct();

      std::unique_ptr<ResolvedExpr> new_expr = MakeResolvedGetStructField(
          struct_type->field(field_idx).type, std::move(expr), field_idx);
      GOOGLESQL_RETURN_IF_ERROR(
          state_.annotation_propagator().CheckAndPropagateAnnotations(
              /*error_node=*/nullptr, new_expr.get()));
      expr = std::move(new_expr);
    }
    return expr;
  } else if (rewrite_type->IsTable()) {
    GOOGLESQL_RET_CHECK(tableref_enabled_);
    // This is GetField on a single-row TABLE type.
    GOOGLESQL_RET_CHECK(rewrite_type->IsSingleRowTable());
    const TableRefType* table_type = rewrite_type->AsTableRefType();

    // For ResolvedGetRowField on TableRefTypes, generate a subquery that
    // fetches the requested column from the joined table.
    const Type* replacement_type = rewrite_type_state->replacement_type();
    GOOGLESQL_RET_CHECK(replacement_type != nullptr);

    const Table* read_table = rewrite_type->table();
    const Column* read_column = get_row_field->column();
    GOOGLESQL_RET_CHECK(read_table != nullptr);
    GOOGLESQL_RET_CHECK(read_column != nullptr);

    // We have `<table>.<field>`, where <table> has single-row TABLE type.
    // We are trying to fetch `read_column` from a row of `read_table`.
    // `<table>` will be replaced by a struct later, containing the bound
    // columns of `read_table`.
    //
    // We'll generate a ResolvedSubqueryExpr with a query like:
    //   FROM <read_table>
    //   |> WHERE <outer_table_type_column> = MakeStruct(<bound_columns>)
    //   |> SELECT <read_column>
    //
    // If the input <expr> is more than just a ColumnRef, then we'll also
    // wrap it with
    //   WITH(<outer_table_type_column> AS <expr>, <the SubqueryExpr>)

    ResolvedColumn outer_table_type_column;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> with_assignments;

    if (get_row_field->expr()->Is<ResolvedColumnRef>()) {
      // The expression we want to reference from the subquery is just a column,
      // so we reference it with a correlated ResolvedColumnRef.
      outer_table_type_column =
          get_row_field->expr()->GetAs<ResolvedColumnRef>()->column();
    } else {
      // The expression we want to reference from the subquery is more than
      // just a column.  We'll make a WITH expression to compute it and give
      // it a ResolvedColumn, which we can reference from the subquery.
      outer_table_type_column = state_.column_factory().MakeCol(
          "$with_expr", "$with_col",
          AnnotatedType(replacement_type,
                        get_row_field->expr()->type_annotation_map()));

      auto node_builder = ToBuilder(std::move(get_row_field));
      with_assignments.push_back(MakeResolvedComputedColumn(
          outer_table_type_column, node_builder.release_expr()));
    }
    // Make a correlated ResolvedColumnRef to point at the RowOrTableType column
    // from outside the subquery (maybe in a WITH expression).
    // This ResolvedColumnRef reports its type as `replacement_type` so
    // generating the Equals comparison below works.
    // The ResolvedColumn inside will get replaced later.
    std::unique_ptr<const ResolvedColumnRef> table_type_column_ref =
        MakeResolvedColumnRef(replacement_type, outer_table_type_column,
                              /*is_correlated=*/true);

    // Compute Columns and ResolvedColumns we'll need to read.
    ReadColumnsSet read_columns_set(state_, read_table);

    // The `bound_columns` on the RowOrTableType are the join key.
    // Build a struct holding those columns, from TableScan columns.
    GOOGLESQL_ASSIGN_OR_RETURN(auto make_struct_expr,
                     read_columns_set.ReadStructWithColumns(
                         table_type->bound_columns(), rewrite_type_state));

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
                                           std::move(table_type_column_ref)));

    ResolvedColumnList final_column_list = {read_resolved_column};
    new_scan = MakeResolvedFilterScan(final_column_list, std::move(new_scan),
                                      std::move(filter_expr));

    // Make the correlated ResolvedSubqueryExpr containing those Scans.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_refs;
    parameter_refs.push_back(MakeResolvedColumnRef(outer_table_type_column,
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
  GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type in ResolvedGetRowField: "
                   << rewrite_type->DebugString();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RowTypeRewriterVisitor::PostVisitResolvedArrayScan(
    std::unique_ptr<const ResolvedArrayScan> array_scan) {
  if (array_scan->array_expr_list_size() != 1) {
    // ArrayScans for TableRefType (to be rewritten) must have exactly 1 expr.
    return std::move(array_scan);
  }

  // Get the parent type before it was rewritten.
  GOOGLESQL_RET_CHECK(!arrayscan_original_type_stack_.empty());
  const Type* original_type = std::move(arrayscan_original_type_stack_.top());
  arrayscan_original_type_stack_.pop();
  GOOGLESQL_RET_CHECK(!original_type->IsRow());
  if (!original_type->IsTable()) {
    return std::move(array_scan);
  }
  GOOGLESQL_RET_CHECK(tableref_enabled_);

  ABSL_LOG(INFO) << "Rewriting ArrayScan:\n" << array_scan->DebugString();

  // We have a ResolvedArrayScan of `array_expr` (a join RowOrTableType)
  // producing `element_column` (a non-join ROW, which will be read like a
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

  // Get the TableRefType that's being scanned like an array.
  const TableRefType* table_type = original_type->AsTableRefType();

  GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* array_rewrite_type_state,
                   state_.GetRewriteTypeState(table_type));

  // Get the element type. It should be a RowType (not a TableRefType).
  GOOGLESQL_RET_CHECK_EQ(array_scan->element_column_list_size(), 1);
  const ResolvedColumn& element_column = array_scan->element_column_list(0);

  GOOGLESQL_RET_CHECK(element_column.type()->IsRow())
      << "Bad element type: " << ColDebugString(element_column);
  const RowType* element_row_type = element_column.type()->AsRowType();

  GOOGLESQL_RET_CHECK_EQ(element_column.type(), table_type->element_type())
      << ColDebugString(element_column) << ", "
      << table_type->element_type()->DebugString();

  // This is the table the join column points at, so it's the table to scan.
  const Table* element_table = element_row_type->table();

  // Store the Columns and ResolvedColumns we'll read for the struct.
  ReadColumnsSet read_columns_set(state_, element_table);

  // The `bound_columns` on the TableRefType are the join key.
  // Build a struct holding those columns, from TableScan columns.
  GOOGLESQL_ASSIGN_OR_RETURN(auto struct_expr,
                   read_columns_set.ReadStructWithColumns(
                       table_type->bound_columns(), array_rewrite_type_state));

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
  // It'll be Equals comparison between the TableRefType's replacement struct
  // (containing the TableRefTypes's `bound_columns` for the join) and the
  // MakeStruct expression from the join rhs.

  // The ArrayScan expression is the input TableRefType value.
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
  mutable_array_expr->set_type(array_rewrite_type_state->replacement_type());
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

  // Collect the non-RowOrTableType columns we're reading first.
  // Reuse the ResolvedColumns that already existed in the original TableScan.
  for (int idx = 0; idx < orig_resolved_columns.size(); ++idx) {
    const ResolvedColumn& col = orig_resolved_columns[idx];
    if (col.type()->IsRowOrTable()) {
      continue;
    }

    const Column* column = orig_table_columns[idx];
    GOOGLESQL_RET_CHECK(column != nullptr);

    GOOGLESQL_RETURN_IF_ERROR(read_columns_set.AddMappedColumn(column, col));
    final_output_column_list.push_back(col);
  }

  // Now handle all the RowOrTableType output columns, and figure out the
  // STRUCTs we need to add in a ProjectScan, and any extra columns we need to
  // read in the `read_columns_set`.
  for (const ResolvedColumn& col : orig_resolved_columns) {
    if (!col.type()->IsRowOrTable()) {
      continue;
    }
    GOOGLESQL_RET_CHECK(tableref_enabled_ || !col.type()->IsTable());
    ABSL_LOG(INFO) << "Rewriting TableScan column " << ColDebugString(col);

    const RowOrTableType* rewrite_type = col.type()->AsRowOrTable();

    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn replacement_column,
                     state_.FindOrAddReplacementColumn(col));

    ABSL_LOG(INFO) << "replacement_column: " << ColDebugString(replacement_column);
    final_output_column_list.push_back(replacement_column);

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> expr,
        MakeRewriteExprForColumn(rewrite_type,
                                 /*is_inner=*/false, read_columns_set));

    std::unique_ptr<ResolvedComputedColumn> computed_col =
        MakeResolvedComputedColumn(replacement_column, std::move(expr));
    GOOGLESQL_RETURN_IF_ERROR(state_.annotation_propagator().CheckAndPropagateAnnotations(
        /*error_node=*/nullptr, computed_col.get()));
    project_exprs.push_back(std::move(computed_col));
  }

  GOOGLESQL_RET_CHECK_EQ(read_columns_set.resolved_columns().size(),
               read_columns_set.table_columns().size());

  // The ResolvedTableScanBuilder reads the columns from `read_columns_set`.
  ResolvedTableScanBuilder builder;
  builder.set_table(table);
  builder.set_alias(alias);
  builder.set_column_list(read_columns_set.resolved_columns());
  builder.set_table_column_list(read_columns_set.table_columns());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> new_scan,
                   std::move(builder).BuildMutable());

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
    const RowOrTableType* rewrite_type, bool is_inner,
    ReadColumnsSet& read_columns_set) {
  GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                   state_.GetRewriteTypeState(rewrite_type));

  // Compute the set of Columns we need to collect and bind in for this
  // RowOrTableType.
  std::vector<const Column*> bound_source_columns;
  if (rewrite_type->IsRow()) {
    bound_source_columns = rewrite_type_state->GetReferencedTableColumns();
  } else {
    bound_source_columns =
        rewrite_type->AsTableRefType()->bound_source_columns();
    GOOGLESQL_RET_CHECK(!bound_source_columns.empty());
  }

  // Compute a ResolvedExpr for each column, which we can use to make a
  // struct, if necessary.
  std::vector<std::unique_ptr<const ResolvedExpr>> make_struct_args;
  for (const Column* table_column : bound_source_columns) {
    if (table_column->GetType()->IsRowOrTable()) {
      // For join columns, we need to make an inner replacement type inside
      // the outer (row-level) replacement struct.
      // Call this method recursively (at most once) for that inner type.
      const RowOrTableType* inner_row_type =
          table_column->GetType()->AsRowOrTable();
      GOOGLESQL_RET_CHECK(inner_row_type->IsTable());
      GOOGLESQL_RET_CHECK(!is_inner);  // Don't recurse more than once.

      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedExpr> inner_expr,
          MakeRewriteExprForColumn(inner_row_type,
                                   /*is_inner=*/true, read_columns_set));

      make_struct_args.push_back(std::move(inner_expr));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn scan_col,
                       read_columns_set.GetResolvedColumn(table_column));

      // This MakeResolvedColumnRef builder propagates annotations.
      make_struct_args.push_back(
          MakeResolvedColumnRef(scan_col, /*is_correlated=*/false));
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> output_expr,
      rewrite_type_state->MakeStructIfNecessary(std::move(make_struct_args)));

  return std::move(output_expr);
}

// The final rewriter cleans up all remaining ResolvedColumns and Types to
// propagate replacement columns and types cleanly.
// This includes:
//
// * Replace any ResolvedColumn with RowOrTableType with a replacement column.
//   - Create the replacement column when we see each column for the first time.
//   - This doesn't distinguish ResolvedColumn creation from ResolvedColumn
//     references. It doesn't matter which is seen first.
//
// * Replace any Type field (with type RowOrTableType) with the replacement
// type.
//   - This includes ResolvedColumnRefs and any other expressions that
//     originally returned RowOrTableTypes.
//
// * Rewrite signatures in ResolvedFunctionCalls that reference RowOrTableTypes.
//   - These must all be templated functions that had ANY types, so we can
//     rewrite the concrete signatures to use replacement types.
//
// These rewrites, and those in the earlier rewriter, are all done
// independently.  Intermediate Resolved ASTs may be invalid, but after all
// rewrites are done, input and output ResolvedColumns and Types line up
// for all nodes and the Resolved AST is valid.
class RowTypeColumnRewriterVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit RowTypeColumnRewriterVisitor(State& state, bool tableref_enabled)
      : state_(state), tableref_enabled_(tableref_enabled) {}

 private:
  absl::StatusOr<const Type*> GetRewriteType(const Type* type) {
    GOOGLESQL_RET_CHECK(type->IsRowOrTable());
    const RowOrTableType* rewrite_type = type->AsRowOrTable();
    GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                     state_.GetRewriteTypeState(rewrite_type));
    GOOGLESQL_RET_CHECK_NE(rewrite_type_state->replacement_type(), nullptr);
    return rewrite_type_state->replacement_type();
  }

  absl::StatusOr<AnnotatedType> GetRewriteAnnotatedType(const Type* type) {
    GOOGLESQL_RET_CHECK(type->IsRowOrTable());
    const RowOrTableType* rewrite_type = type->AsRowOrTable();
    GOOGLESQL_ASSIGN_OR_RETURN(const RewriteTypeState* rewrite_type_state,
                     state_.GetRewriteTypeState(rewrite_type));
    GOOGLESQL_RET_CHECK_NE(rewrite_type_state->replacement_type(), nullptr);
    return AnnotatedType(rewrite_type_state->replacement_type(),
                         rewrite_type_state->replacement_type_annotation_map());
  }

  absl::StatusOr<ResolvedColumn> PostVisitResolvedColumn(
      const ResolvedColumn& column) override {
    if (!HasRewriteType(column.type())) {
      return column;
    }
    return state_.FindOrAddReplacementColumn(column);
  }

  absl::StatusOr<AnnotatedType> PostVisitAnnotatedType(
      const Type* type,
      std::optional<const AnnotationMap*> annotation_map) override {
    if (!HasRewriteType(type)) {
      return AnnotatedType(type, annotation_map.value_or(nullptr));
    }
    return GetRewriteAnnotatedType(type);
  }

  // If a FunctionArgumentType reference RowOrTableType, return a rewrite
  // referencing the replacement type.
  absl::StatusOr<FunctionArgumentType> MapFunctionArgumentType(
      const FunctionArgumentType& arg_type) {
    if (!HasRewriteType(arg_type)) {
      return arg_type;
    }
    // TODO: Support annotations here.
    GOOGLESQL_ASSIGN_OR_RETURN(const Type* new_type, GetRewriteType(arg_type.type()));
    return FunctionArgumentType(new_type, arg_type.options(),
                                arg_type.num_occurrences());
  }

  // Common PostVisit handler for all ResolvedFunctionCallBase subclasses.
  // RowType and TableType cannot normally be passed as function inputs, but
  // some rewriters may add them as function arguments. For example, we allow:
  //   - Passing TableRefType to FLATTEN, the FLATTEN rewriter may pass them to
  //     function calls.
  //   - Referencing RowType fields from MEASURE expressions/grain locking, the
  //     MEASURE rewriter may pass them to function calls.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  RewriteAnyResolvedFunctionCallBase(
      std::unique_ptr<const ResolvedFunctionCallBase> node,
      bool tableref_enabled) {
    const FunctionSignature& signature = node->signature();

    bool has_rewrite_type = HasRewriteType(signature.result_type());
    if (!has_rewrite_type) {
      for (const FunctionArgumentType& arg_type : signature.arguments()) {
        if (HasRewriteType(arg_type)) {
          has_rewrite_type = true;
          break;
        }
      }
    }
    if (!has_rewrite_type) {
      return std::move(node);
    }

    bool has_row_type = signature.result_type().type() != nullptr &&
                        signature.result_type().type()->IsRow();
    if (!has_row_type) {
      for (const FunctionArgumentType& arg_type : signature.arguments()) {
        if (arg_type.type() != nullptr && arg_type.type()->IsRow()) {
          has_row_type = true;
          break;
        }
      }
    }

    // If the function uses a RowType, get the replacement function context_id.
    int64_t new_context_id = signature.context_id();
    if (has_row_type) {
      GOOGLESQL_RET_CHECK(node->node_kind() == RESOLVED_FUNCTION_CALL)
          << "There are no non-ResolvedFunctionCall nodes which allow "
             "RowTypes.";
      GOOGLESQL_RET_CHECK(node->function()->IsGoogleSQLBuiltin())
          << "There are no non-builtin Functions which allow RowTypes.";
      switch (signature.context_id()) {
        case FN_IS_NULL:
          // TODO: Stop producing FN_IS_NULL with RowType from
          // Measure rewriter, and remove this case. This is only included to
          // avoid breaking some tests that depend on this.
          new_context_id = FN_IS_NULL;
          break;
        case FN_IS_NULL_ROW:
          new_context_id = FN_IS_NULL;
          break;
        default:
          GOOGLESQL_RET_CHECK_FAIL() << "Unsupported signature context_id: "
                           << signature.context_id();
      }
    }

    ABSL_LOG(INFO) << "PostVisitResolvedFunctionCallBase:\n" << node->DebugString();

    FunctionArgumentTypeList arguments;
    arguments.reserve(signature.arguments().size());
    for (const FunctionArgumentType& argument : signature.arguments()) {
      GOOGLESQL_RET_CHECK(tableref_enabled || !argument.type()->IsTable());
      GOOGLESQL_ASSIGN_OR_RETURN(FunctionArgumentType new_argument,
                       MapFunctionArgumentType(argument));
      arguments.push_back(new_argument);
    }
    GOOGLESQL_RET_CHECK(tableref_enabled || !signature.result_type().type()->IsTable());
    GOOGLESQL_ASSIGN_OR_RETURN(FunctionArgumentType result_type,
                     MapFunctionArgumentType(signature.result_type()));

    FunctionSignature new_signature(result_type, arguments, new_context_id,
                                    signature.options());

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
    return RewriteAnyResolvedFunctionCallBase(std::move(node),
                                              tableref_enabled_);
  }
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateFunctionCall(
      std::unique_ptr<const ResolvedAggregateFunctionCall> node) override {
    return RewriteAnyResolvedFunctionCallBase(std::move(node),
                                              tableref_enabled_);
  }
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAnalyticFunctionCall(
      std::unique_ptr<const ResolvedAnalyticFunctionCall> node) override {
    return RewriteAnyResolvedFunctionCallBase(std::move(node),
                                              tableref_enabled_);
  }

  State& state_;

  // Whether TableRef Type is enabled.
  bool tableref_enabled_;
};

class RowTypeRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> scan,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, *options.id_string_pool(),
                                 *options.column_id_sequence_number());

    FunctionCallBuilder function_call_builder(options, catalog, type_factory);
    bool tableref_enabled =
        options.language().LanguageFeatureEnabled(FEATURE_TABLE_TYPE);

    ABSL_LOG(INFO) << "Before rewrite:\n" << scan->DebugString();

    State state(column_factory, function_call_builder.annotation_propagator());
    RowTypeCollectorVisitor collector(state, tableref_enabled);
    GOOGLESQL_RETURN_IF_ERROR(scan->Accept(&collector));

    GOOGLESQL_RETURN_IF_ERROR(state.MakeReplacementTypes(type_factory, tableref_enabled));

    RowTypeRewriterVisitor rewriter1(state, function_call_builder, type_factory,
                                     tableref_enabled);
    GOOGLESQL_ASSIGN_OR_RETURN(scan, rewriter1.VisitAll(std::move(scan)));
    ABSL_LOG(INFO) << "After first-pass rewrite:\n" << scan->DebugString();

    RowTypeColumnRewriterVisitor rewriter2(state, tableref_enabled);
    GOOGLESQL_ASSIGN_OR_RETURN(scan, rewriter2.VisitAll(std::move(scan)));
    ABSL_LOG(INFO) << "After final rewrite:\n" << scan->DebugString();

    return scan;
  }

  std::string Name() const override { return "RowTypeRewriter"; }
};

}  // namespace

const Rewriter* GetRowTypeRewriter() {
  static const auto* const kRewriter = new RowTypeRewriter;
  return kRewriter;
}

}  // namespace googlesql
