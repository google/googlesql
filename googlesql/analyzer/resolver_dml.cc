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

// This file contains the implementation of DML-related resolver methods
// from resolver.h.
#include <stddef.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/function_resolver.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/path_expression_span.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/common/errors.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
// This includes common macro definitions to define in the resolver cc files.
#include "googlesql/analyzer/resolver_common_inl.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// These are constant identifiers used mostly for generated column or table
// names.  We use a single IdString for each so we never have to allocate
// or copy these strings again.
STATIC_IDSTRING(kElementId, "$element");
STATIC_IDSTRING(kInsertId, "$insert");
STATIC_IDSTRING(kInsertCastId, "$insert_cast");

absl::Status Resolver::ResolveDMLTargetTable(
    const ASTPathExpression* target_path, const ASTAlias* target_path_alias,
    const ASTHint* hint, IdString* alias,
    std::unique_ptr<const ResolvedTableScan>* resolved_table_scan,
    std::shared_ptr<const NameList>* name_list,
    ResolvedColumnToCatalogColumnHashMap&
        resolved_columns_to_catalog_columns_for_target_scan) {
  GOOGLESQL_RET_CHECK(target_path != nullptr);
  GOOGLESQL_RET_CHECK(alias != nullptr);
  GOOGLESQL_RET_CHECK(resolved_table_scan != nullptr);
  GOOGLESQL_RET_CHECK(name_list != nullptr);

  name_list->reset(new NameList());
  const ASTNode* alias_location = nullptr;
  bool has_explicit_alias;
  if (target_path_alias != nullptr) {
    *alias = target_path_alias->GetAsIdString();
    alias_location = target_path_alias;
    has_explicit_alias = true;
  } else {
    *alias = GetAliasForExpression(target_path);
    alias_location = target_path;
    has_explicit_alias = false;
  }
  GOOGLESQL_RET_CHECK(!alias->empty());

  GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsTableScan(
      target_path, *alias, has_explicit_alias, alias_location, hint,
      /*for_system_time=*/nullptr, empty_name_scope_.get(),
      /*read_as_row_type_error_kind=*/"DML statements",
      /*remaining_names=*/nullptr, resolved_table_scan, name_list,
      /*output_column_name_list=*/nullptr,
      resolved_columns_to_catalog_columns_for_target_scan));
  GOOGLESQL_RET_CHECK((*name_list)->HasRangeVariable(*alias));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDeleteStatement(
    const ASTDeleteStatement* ast_statement,
    std::unique_ptr<ResolvedDeleteStmt>* output) {
  IdString target_alias;
  std::unique_ptr<const ResolvedTableScan> resolved_table_scan;
  std::shared_ptr<const NameList> name_list;
  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* target_path,
                   ast_statement->GetTargetPathForNonNested());
  GOOGLESQL_RETURN_IF_ERROR(ResolveDMLTargetTable(
      target_path, ast_statement->alias(), ast_statement->hint(), &target_alias,
      &resolved_table_scan, &name_list, resolved_columns_from_table_scans_));
  if (ast_statement->offset() != nullptr) {
    return MakeSqlErrorAt(ast_statement->offset())
           << "Non-nested DELETE statement does not support WITH OFFSET";
  }

  const std::unique_ptr<const NameScope> delete_scope(
      new NameScope(*name_list));
  return ResolveDeleteStatementImpl(ast_statement, target_alias, name_list,
                                    delete_scope.get(),
                                    std::move(resolved_table_scan), output);
}

absl::Status Resolver::ResolveDeleteStatementImpl(
    const ASTDeleteStatement* ast_statement, IdString target_alias,
    const std::shared_ptr<const NameList>& target_name_list,
    const NameScope* scope,
    std::unique_ptr<const ResolvedTableScan> resolved_table_scan,
    std::unique_ptr<ResolvedDeleteStmt>* output) {
  std::unique_ptr<ResolvedColumnHolder> resolved_array_offset_column;
  std::unique_ptr<NameScope> new_scope_owner;
  if (ast_statement->offset() != nullptr) {
    if (!language().LanguageFeatureEnabled(
            FEATURE_NESTED_UPDATE_DELETE_WITH_OFFSET)) {
      return MakeSqlErrorAt(ast_statement->offset())
             << "DELETE does not support WITH OFFSET";
    }

    const ASTWithOffset* offset = ast_statement->offset();
    const IdString offset_alias = offset->alias() != nullptr
                                      ? offset->alias()->GetAsIdString()
                                      : kOffsetAlias;
    if (offset_alias == target_alias) {
      const ASTNode* ast_location =
          offset->alias() != nullptr
              ? static_cast<const ASTNode*>(offset->alias())
              : offset;
      return MakeSqlErrorAt(ast_location)
             << "Duplicate OFFSET alias " << ToIdentifierLiteral(offset_alias)
             << " in nested DELETE";
    }

    const ResolvedColumn offset_column(AllocateColumnId(),
                                       /*table_name=*/kArrayId, offset_alias,
                                       types::Int64Type());
    resolved_array_offset_column = MakeResolvedColumnHolder(offset_column);

    // Stack a scope to include the offset column.  Stacking a scope is not
    // ideal because it makes the error messages worse (no more "Did you mean
    // ...?"), and we also have to perform the manual check above to handle the
    // case where 'offset_alias == target_alias'. A possible alternative to
    // stacking is to have this method create 'scope' in the first place rather
    // than passing it in and then stacking something on top of it. But that
    // does not seem worth the complexity.
    std::shared_ptr<NameList> offset_column_list(new NameList);
    GOOGLESQL_RETURN_IF_ERROR(offset_column_list->AddColumn(
        offset_column.name_id(), offset_column, /*is_explicit=*/false));
    new_scope_owner = std::make_unique<NameScope>(scope, offset_column_list);
    scope = new_scope_owner.get();
  }

  if (ast_statement->where() == nullptr) {
    return MakeSqlErrorAt(ast_statement) << "DELETE must have a WHERE clause";
  }
  std::unique_ptr<const ResolvedExpr> resolved_where_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_statement->where(), scope,
                                    "WHERE clause", &resolved_where_expr));
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(ast_statement->where(), "WHERE clause",
                                   &resolved_where_expr));

  std::unique_ptr<const ResolvedAssertRowsModified>
      resolved_assert_rows_modified;
  if (ast_statement->assert_rows_modified() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveAssertRowsModified(
        ast_statement->assert_rows_modified(), &resolved_assert_rows_modified));
  }

  std::unique_ptr<const ResolvedReturningClause> resolved_returning_clause;
  if (ast_statement->returning() != nullptr) {
    if (!language().LanguageFeatureEnabled(FEATURE_DML_RETURNING)) {
      return MakeSqlErrorAt(ast_statement->returning())
             << "THEN RETURN is not supported";
    }
    if (target_name_list == nullptr) {
      return MakeSqlErrorAt(ast_statement->returning())
             << "THEN RETURN is not allowed in nested DELETE statements";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveReturningClause(ast_statement->returning(),
                                           target_alias, target_name_list,
                                           scope, &resolved_returning_clause));
  }

  *output = MakeResolvedDeleteStmt(
      std::move(resolved_table_scan), std::move(resolved_assert_rows_modified),
      std::move(resolved_returning_clause),
      std::move(resolved_array_offset_column), std::move(resolved_where_expr));
  MaybeRecordResolvedNodeOperatorKeywordLocation(ast_statement, output->get());
  return absl::OkStatus();
}

absl::Status Resolver::ResolveTruncateStatement(
    const ASTTruncateStatement* ast_statement,
    std::unique_ptr<ResolvedTruncateStmt>* output) {
  std::unique_ptr<const ResolvedTableScan> resolved_table_scan;
  auto name_list = std::make_shared<const NameList>();
  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* name_path,
                   ast_statement->GetTargetPathForNonNested());
  GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsTableScan(
      name_path, GetAliasForExpression(name_path), /*has_explicit_alias=*/false,
      /*alias_location=*/name_path, /*hints=*/nullptr,
      /*for_system_time=*/nullptr, empty_name_scope_.get(),
      /*read_as_row_type_error_kind=*/"DML statements",
      /*remaining_names=*/nullptr, &resolved_table_scan, &name_list,
      /*output_column_name_list=*/nullptr, resolved_columns_from_table_scans_));

  const NameScope truncate_scope(*name_list);
  std::unique_ptr<const ResolvedExpr> resolved_where_expr;

  if (ast_statement->where() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_statement->where(), &truncate_scope,
                                      "WHERE clause", &resolved_where_expr));
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(ast_statement->where(), "WHERE clause",
                                     &resolved_where_expr));
  }

  *output = MakeResolvedTruncateStmt(std::move(resolved_table_scan),
                                     std::move(resolved_where_expr));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveInsertValuesRow(
    const ASTInsertValuesRow* ast_insert_values_row, const NameScope* scope,
    const ResolvedColumnList& insert_columns,
    std::unique_ptr<const ResolvedInsertRow>* output) {
  if (ast_insert_values_row->values().size() != insert_columns.size()) {
    return MakeSqlErrorAt(ast_insert_values_row)
           << "Inserted row has wrong column count; Has "
           << ast_insert_values_row->values().size() << ", expected "
           << insert_columns.size();
  }

  std::vector<std::unique_ptr<const ResolvedDMLValue>> dml_values;
  dml_values.reserve(ast_insert_values_row->values().size());
  for (int i = 0; i < ast_insert_values_row->values().size(); ++i) {
    const ASTExpression* value = ast_insert_values_row->values()[i];
    std::unique_ptr<const ResolvedDMLValue> resolved_dml_value;
    auto make_error_msg = [&column = insert_columns[i]](
                              absl::string_view target_t,
                              absl::string_view arg_t) {
      return absl::Substitute(
          "Value has type $0 which cannot be inserted into column $2, which "
          "has type $1",
          arg_t, target_t, column.name());
    };
    GOOGLESQL_RETURN_IF_ERROR(ResolveDMLValue(
        value, insert_columns[i].annotated_type(), scope,
        /*clause_name=*/"INSERT VALUES", make_error_msg, &resolved_dml_value));
    dml_values.push_back(std::move(resolved_dml_value));
  }

  // Checks whether dml_values are valid for a particular subset of columns
  // mentioned in an INSERT statement.
  GOOGLESQL_RET_CHECK_EQ(dml_values.size(), insert_columns.size());

  *output = MakeResolvedInsertRow(std::move(dml_values));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveInsertValuesRow(
    const ASTNode* ast_location, const ResolvedColumnList& value_columns,
    const ResolvedColumnList& insert_columns,
    std::unique_ptr<const ResolvedInsertRow>* output) {
  GOOGLESQL_RET_CHECK(ast_location != nullptr);
  GOOGLESQL_RET_CHECK(output != nullptr);

  std::vector<std::unique_ptr<const ResolvedDMLValue>> dml_values;
  dml_values.reserve(value_columns.size());
  for (int i = 0; i < value_columns.size(); ++i) {
    std::unique_ptr<const ResolvedDMLValue> resolved_dml_value;
    auto make_error_msg = [&column = insert_columns[i]](
                              absl::string_view target_t,
                              absl::string_view arg_t) {
      return absl::Substitute(
          "Value has type $0 which cannot be inserted into column $2, which "
          "has type $1",
          arg_t, target_t, column.name());
    };
    GOOGLESQL_RETURN_IF_ERROR(ResolveDMLValue(ast_location, value_columns[i],
                                    insert_columns[i].annotated_type(),
                                    make_error_msg, &resolved_dml_value));
    dml_values.push_back(std::move(resolved_dml_value));
  }
  GOOGLESQL_RET_CHECK_EQ(dml_values.size(), insert_columns.size());

  *output = MakeResolvedInsertRow(std::move(dml_values));
  return absl::OkStatus();
}

// <insert_columns> is the list of columns inserted into the target table.
// <output_column_list> returns the list of columns produced by <output> that
// map positionally into <insert_columns>.
absl::Status Resolver::ResolveInsertQuery(
    const ASTNode* ast_location, const ASTQuery* query,
    const NameScope* nested_scope, const ResolvedColumnList& insert_columns,
    const NameList* pipe_input_name_list,
    std::unique_ptr<const ResolvedScan> pipe_input_scan,
    std::unique_ptr<const ResolvedScan>* output,
    ResolvedColumnList* output_column_list,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>* parameter_list) {
  // We have either an input ASTQuery or a pipe input table.
  GOOGLESQL_RET_CHECK_NE(query != nullptr, pipe_input_name_list != nullptr);
  GOOGLESQL_RET_CHECK_EQ(pipe_input_name_list != nullptr, pipe_input_scan != nullptr);
  const int num_insert_columns = insert_columns.size();

  std::unique_ptr<CorrelatedColumnsSet> correlated_columns_set;
  std::unique_ptr<NameScope> name_scope_owner;
  const NameScope* name_scope = empty_name_scope_.get();
  const bool is_nested = (nested_scope != nullptr);
  if (is_nested) {
    correlated_columns_set = std::make_unique<CorrelatedColumnsSet>();
    name_scope_owner =
        std::make_unique<NameScope>(nested_scope, correlated_columns_set.get());
    name_scope = name_scope_owner.get();
  }

  std::unique_ptr<const ResolvedScan> resolved_query;
  std::shared_ptr<const NameList> owned_query_name_list;
  const NameList* query_name_list = nullptr;

  if (query != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveQuery(query, name_scope, kInsertId, &resolved_query,
                                 &owned_query_name_list,
                                 {.is_outer_query = !is_nested}));
    query_name_list = owned_query_name_list.get();
  } else {
    GOOGLESQL_RET_CHECK(pipe_input_name_list != nullptr);
    GOOGLESQL_RET_CHECK(pipe_input_scan != nullptr);
    // For pipe INSERT, we use the pipe input query's NameList and the
    // ResolvedScan representing the pipe input table.
    query_name_list = pipe_input_name_list;
    resolved_query = std::move(pipe_input_scan);
  }
  GOOGLESQL_RET_CHECK(query_name_list != nullptr);

  if (correlated_columns_set != nullptr) {
    FetchCorrelatedSubqueryParameters(*correlated_columns_set, parameter_list);
  }

  *output_column_list = query_name_list->GetResolvedColumns();
  if (output_column_list->size() != num_insert_columns) {
    return MakeSqlErrorAt(ast_location)
           << "Inserted row has wrong column count; Has "
           << output_column_list->size() << ", expected " << num_insert_columns;
  }

  // All input columns are read and thus can't be pruned.
  RecordColumnAccess(*output_column_list);

  bool needs_cast = false;
  UntypedLiteralMap untyped_literal_map(resolved_query.get());
  for (int i = 0; i < num_insert_columns; ++i) {
    const Type* current_type = (*output_column_list)[i].type();
    const Type* insert_type = insert_columns[i].type();
    if (!current_type->Equals(insert_type)) {
      needs_cast = true;
      InputArgumentType input_argument_type(current_type,
                                            /*is_query_parameter=*/false);
      SignatureMatchResult unused;
      if (!coercer_.AssignableTo(input_argument_type, insert_type,
                                 /* is_explicit = */ false, &unused) &&
          untyped_literal_map.Find((*output_column_list)[i]) == nullptr) {
        return MakeSqlErrorAt(ast_location)
               << "Query column " << (i + 1) << " has type "
               << current_type->ShortTypeName(product_mode())
               << " which cannot be inserted into column "
               << insert_columns[i].name() << ", which has type "
               << insert_columns[i].type()->ShortTypeName(product_mode());
      }
    }
  }

  if (needs_cast) {
    GOOGLESQL_RETURN_IF_ERROR(CreateWrapperScanWithCasts(ast_location, insert_columns,
                                               kInsertCastId, &resolved_query,
                                               output_column_list));
  }
  GOOGLESQL_RET_CHECK_EQ(output_column_list->size(), insert_columns.size());

  *output = std::move(resolved_query);
  return absl::OkStatus();
}

absl::Status
Resolver::GeneratedColumnTopoSorter::TopologicallySortGeneratedColumns() {
  for (int generated_column_index : generated_column_indices_) {
    if (visited_column_indices_.contains(generated_column_index)) {
      continue;
    }
    const Column* generated_column = table_->GetColumn(generated_column_index);
    GOOGLESQL_RET_CHECK(generated_column != nullptr);
    GOOGLESQL_RET_CHECK(generated_column->HasGeneratedExpression());
    GOOGLESQL_RET_CHECK(generated_column->GetExpression().has_value());
    CycleDetector::ObjectInfo object(generated_column->FullName(),
                                     generated_column, cycle_detector_);
    const ResolvedExpr* resolved_expr =
        generated_column->GetExpression()->GetResolvedExpression();
    GOOGLESQL_RETURN_IF_ERROR(resolved_expr->Accept(this));
    visited_column_indices_.insert(generated_column_index);
    auto itr = catalog_columns_to_resolved_columns_map_.find(generated_column);
    GOOGLESQL_RET_CHECK(itr != catalog_columns_to_resolved_columns_map_.end());
    topologically_sorted_generated_columns_.push_back(
        {.column_index = generated_column_index,
         .resolved_column_id = itr->second.column_id()});
  }
  return absl::OkStatus();
}

absl::Status Resolver::GeneratedColumnTopoSorter::DefaultVisit(
    const ResolvedNode* node) {
  // TODO: Add checks for ResolvedCatalogColumnRef here whenever
  // it becomes relevant.
  if (node->Is<ResolvedExpressionColumn>()) {
    const Column* column = table_->FindColumnByName(
        node->GetAs<ResolvedExpressionColumn>()->name());
    GOOGLESQL_RET_CHECK(column != nullptr);
    bool duplicate = false;
    int column_index = -1;
    Resolver::FindColumnIndex(table_, column->Name(), &column_index,
                              &duplicate);
    GOOGLESQL_RET_CHECK_GE(column_index, 0);
    GOOGLESQL_RET_CHECK(!duplicate);
    if (column->HasGeneratedExpression()) {
      if (visited_column_indices_.contains(column_index)) {
        return absl::OkStatus();
      }
      GOOGLESQL_RET_CHECK(column->GetExpression().has_value());
      CycleDetector::ObjectInfo object(column->FullName(), column,
                                       cycle_detector_);
      absl::Status status = object.DetectCycle("generated column");
      if (!status.ok()) {
        const std::vector<std::string>& cycle_names =
            cycle_detector_->ObjectNames();
        return MakeSqlError()
               << "Recursive dependencies detected while evaluating "
                  "generated column expression values for "
               << column->FullName()
               << ". The expression indicates the column depends on "
                  "itself. Columns forming the cycle : "
               << absl::StrJoin(cycle_names, ", ");
      }
      GOOGLESQL_RETURN_IF_ERROR(
          column->GetExpression()->GetResolvedExpression()->Accept(this));
      visited_column_indices_.insert(column_index);
      auto itr = catalog_columns_to_resolved_columns_map_.find(column);
      GOOGLESQL_RET_CHECK(itr != catalog_columns_to_resolved_columns_map_.end());
      topologically_sorted_generated_columns_.push_back(
          {.column_index = column_index,
           .resolved_column_id = itr->second.column_id()});
    }
  }
  return ResolvedASTVisitor::DefaultVisit(node);
}

absl::Status Resolver::TopologicallySortGeneratedColumns(
    std::vector<int>& generated_columns, const Table* table,
    CycleDetector* cycle_detector,
    absl::node_hash_map<const Column*, ResolvedColumn>&
        catalog_columns_to_resolved_columns_map,
    std::vector<GeneratedColumnIndexAndResolvedId>&
        out_topologically_sorted_generated_columns) {
  GeneratedColumnTopoSorter sorter(table, cycle_detector, generated_columns,
                                   catalog_columns_to_resolved_columns_map);
  GOOGLESQL_RETURN_IF_ERROR(sorter.TopologicallySortGeneratedColumns());
  out_topologically_sorted_generated_columns =
      sorter.release_topologically_sorted_generated_columns();
  return absl::OkStatus();
}

namespace {
// Visitor to rewrite ResolvedExpressionColumn to the appropriate
// ResolvedColumnRef.
class ResolvedExpressionColumnRewriter : public ResolvedASTRewriteVisitor {
 public:
  ResolvedExpressionColumnRewriter(
      const Table* table,
      const absl::node_hash_map<const Column*, ResolvedColumn>&
          catalog_columns_to_resolved_columns_map,
      std::function<std::unique_ptr<ResolvedColumnRef>(const ResolvedColumn&)>
          make_column_ref)
      : table_(table),
        catalog_columns_to_resolved_columns_map_(
            catalog_columns_to_resolved_columns_map),
        make_column_ref_(make_column_ref) {}

  // This function rewrites the generated column expressions
  // (original_value_expr)to replace the ResolvedExpressionColumns to
  // corresponding ResolvedColumnRef and returns the rewritten expression.
  // ResolvedExpressionColumns were created as a placeholder earlier as there
  // were no ResolvedColumns at the point.
  // catalog_columns_to_resolved_columns_map is a map of Catalog Column vs
  // the corresponding ResolvedColumn. This is in context of the target table
  // scan and not the entire query. make_column_ref is a lamda function which
  // calls Resolver::MakeColumnRef to make corresponding ResolvedColumnRef and
  // record the column access.
  static absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  RewriteValueExpression(
      const ResolvedExpr* original_value_expr, const Table* table,
      const absl::node_hash_map<const Column*, ResolvedColumn>&
          catalog_columns_to_resolved_columns_map,
      std::function<std::unique_ptr<ResolvedColumnRef>(const ResolvedColumn&)>
          make_column_ref) {
    ResolvedExpressionColumnRewriter rewriter(
        table, catalog_columns_to_resolved_columns_map, make_column_ref);
    // We copied the <original_value_expr> (instead of directly rewrite it)
    // because <original_value_expr> is owned by the Column
    // catalog object. As a result, we cannot directly pass it to
    // ResolvedExpressionColumnRewriter API which takes a unique_ptr of the
    // input node.Furthermore, the same expression may be referenced multiple
    // times, for example, multiple statements across a script, or in the same
    // process, etc.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> copied_node,
                     ResolvedASTDeepCopyVisitor::Copy(original_value_expr));
    return rewriter.VisitAll<ResolvedExpr>(std::move(copied_node));
  }

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedExpressionColumn(
      std::unique_ptr<const ResolvedExpressionColumn> ref) override {
    const Column* column = table_->FindColumnByName(ref->name());
    GOOGLESQL_RET_CHECK(column != nullptr);
    auto itr = catalog_columns_to_resolved_columns_map_.find(column);
    GOOGLESQL_RET_CHECK(itr != catalog_columns_to_resolved_columns_map_.end());
    // Resolver::MakeColumnRef also records the referenced column access.
    return make_column_ref_(itr->second);
  }

 private:
  const Table* table_;
  const absl::node_hash_map<const Column*, ResolvedColumn>&
      catalog_columns_to_resolved_columns_map_;
  std::function<std::unique_ptr<ResolvedColumnRef>(const ResolvedColumn&)>
      make_column_ref_;
};

absl::Status RewriteGeneratedColumnExpressions(
    std::vector<Resolver::GeneratedColumnIndexAndResolvedId>
        topologically_sorted_generated_columns,
    const Table* table,
    absl::node_hash_map<const Column*, ResolvedColumn>&
        catalog_columns_to_resolved_columns_map,
    std::vector<std::unique_ptr<const ResolvedExpr>>&
        out_generated_column_expr_list,
    std::vector<int>& out_topologically_sorted_column_ids,
    std::function<std::unique_ptr<ResolvedColumnRef>(const ResolvedColumn&)>
        make_column_ref) {
  for (auto const& [generated_column_index, generated_column_id] :
       topologically_sorted_generated_columns) {
    const Column* generated_column = table->GetColumn(generated_column_index);
    GOOGLESQL_RET_CHECK(generated_column != nullptr);
    GOOGLESQL_RET_CHECK(generated_column->HasGeneratedExpression());
    const ResolvedExpr* resolved_expr =
        generated_column->GetExpression()->GetResolvedExpression();
    GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> rewritten_expr,
        ResolvedExpressionColumnRewriter::RewriteValueExpression(
            resolved_expr, table, catalog_columns_to_resolved_columns_map,
            make_column_ref));
    out_generated_column_expr_list.push_back(std::move(rewritten_expr));
    out_topologically_sorted_column_ids.push_back(generated_column_id);
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status Resolver::ResolveInsertStatement(
    const ASTInsertStatement* ast_statement,
    const NameList* pipe_input_name_list,
    std::unique_ptr<const ResolvedScan> pipe_input_scan,
    std::unique_ptr<ResolvedInsertStmt>* output) {
  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* target_path,
                   ast_statement->GetTargetPathForNonNested());
  std::shared_ptr<const NameList> name_list(new NameList);

  IdString target_alias = GetAliasForExpression(target_path);
  std::unique_ptr<const ResolvedTableScan> resolved_table_scan;
  ResolvedColumnToCatalogColumnHashMap
      out_resolved_columns_to_catalog_columns_for_target_scan;
  GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsTableScan(
      target_path, target_alias, /*has_explicit_alias=*/false,
      /*alias_location=*/target_path, ast_statement->hint(),
      /*for_system_time=*/nullptr, empty_name_scope_.get(),
      /*read_as_row_type_error_kind=*/"DML statements",
      /*remaining_names=*/nullptr, &resolved_table_scan, &name_list,
      /*output_column_name_list=*/nullptr,
      out_resolved_columns_to_catalog_columns_for_target_scan));

  // Insert the resolved columns from current scan to all scans map.
  resolved_columns_from_table_scans_.insert(
      out_resolved_columns_to_catalog_columns_for_target_scan.begin(),
      out_resolved_columns_to_catalog_columns_for_target_scan.end());

  IdStringHashMapCase<ResolvedColumn> table_scan_columns;
  IdStringHashSetCase ambiguous_column_names;
  for (const ResolvedColumn& column : resolved_table_scan->column_list()) {
    if (!googlesql_base::InsertIfNotPresent(&table_scan_columns, column.name_id(),
                                 column)) {
      googlesql_base::InsertIfNotPresent(&ambiguous_column_names, column.name_id());
    }
  }

  ResolvedColumnList insert_columns;
  IdStringHashSetCase visited_column_names;

  const bool has_column_list = ast_statement->column_list() != nullptr;
  if (has_column_list) {
    for (const ASTIdentifier* column_name :
         ast_statement->column_list()->identifiers()) {
      const IdString column_name_id = column_name->GetAsIdString();
      if (!googlesql_base::InsertIfNotPresent(&visited_column_names, column_name_id)) {
        return MakeSqlErrorAt(column_name)
               << "INSERT has columns with duplicate name: " << column_name_id;
      }

      const ResolvedColumn* column =
          googlesql_base::FindOrNull(table_scan_columns, column_name_id);
      if (column != nullptr) {
        if (ambiguous_column_names.contains(column_name_id)) {
          return MakeSqlErrorAt(column_name)
                 << "Column " << column_name_id
                 << " is ambiguous and cannot be referenced";
        }
        GOOGLESQL_RETURN_IF_ERROR(
            VerifyTableScanColumnIsWritable(column_name, *column, "INSERT"));
        insert_columns.push_back(*column);
      } else {
        return MakeSqlErrorAt(column_name)
               << "Column " << column_name_id << " is not present in table "
               << resolved_table_scan->table()->FullName();
      }
    }

    // We will be writing to column with default values, either with user input
    // or with their default expressions, so record the access here.
    for (auto const& [resolved_column, catalog_column] :
         resolved_columns_from_table_scans_) {
      if (catalog_column->IsWritableColumn() &&
          catalog_column->HasDefaultExpression()) {
        RecordColumnAccess(resolved_column, ResolvedStatement::WRITE);
      }
    }

  } else {
    if (resolved_table_scan->table()->IsValueTable()) {
      const Table* table = resolved_table_scan->table();
      GOOGLESQL_RETURN_IF_ERROR(CheckValidValueTable(target_path, *table));
      insert_columns.push_back(resolved_table_scan->column_list(0));
    } else {
      if (language().LanguageFeatureEnabled(FEATURE_OMIT_INSERT_COLUMN_LIST)) {
        // Implicitly expand column list to all writable non-pseudo columns.
        for (const NamedColumn& named_column : name_list->columns()) {
          const IdString column_name_id = named_column.name();
          const ResolvedColumn& column = named_column.column();
          if (ambiguous_column_names.contains(column_name_id)) {
            return MakeSqlErrorAt(ast_statement)
                   << "Column " << column_name_id
                   << " is ambiguous and cannot be referenced";
          }
          GOOGLESQL_ASSIGN_OR_RETURN(bool is_writable, IsColumnWritable(column));
          if (is_writable) {
            insert_columns.push_back(column);
          }
        }
        if (insert_columns.empty()) {
          return MakeSqlErrorAt(ast_statement)
                 << "No writable column in target table";
        }
      } else {
        return MakeSqlErrorAt(ast_statement)
               << "INSERT must specify a column list";
      }
    }
  }

  // Avoid pruning the insert columns on the ResolvedTableScan that we are
  // inserting into.
  RecordColumnAccess(insert_columns, ResolvedStatement::WRITE);

  return ResolveInsertStatementImpl(
      ast_statement, target_alias, name_list, std::move(resolved_table_scan),
      insert_columns,
      /*nested_scope=*/nullptr, pipe_input_name_list,
      std::move(pipe_input_scan),
      out_resolved_columns_to_catalog_columns_for_target_scan, output);
}

absl::Status Resolver::ResolveInsertStatementImpl(
    const ASTInsertStatement* ast_statement, IdString target_alias,
    const std::shared_ptr<const NameList>& target_name_list,
    std::unique_ptr<const ResolvedTableScan> resolved_table_scan,
    const ResolvedColumnList& insert_columns, const NameScope* nested_scope,
    const NameList* pipe_input_name_list,
    std::unique_ptr<const ResolvedScan> pipe_input_scan,
    ResolvedColumnToCatalogColumnHashMap&
        resolved_columns_to_catalog_columns_for_target_scan,
    std::unique_ptr<ResolvedInsertStmt>* output) {
  ResolvedInsertStmt::InsertMode insert_mode;
  switch (ast_statement->insert_mode()) {
    case ASTInsertStatement::IGNORE:
      insert_mode = ResolvedInsertStmt::OR_IGNORE;
      break;
    case ASTInsertStatement::REPLACE:
      insert_mode = ResolvedInsertStmt::OR_REPLACE;
      break;
    case ASTInsertStatement::UPDATE:
      insert_mode = ResolvedInsertStmt::OR_UPDATE;
      break;
    case ASTInsertStatement::DEFAULT_MODE:
      insert_mode = ResolvedInsertStmt::OR_ERROR;
      break;
  }

  const bool is_nested = nested_scope != nullptr;
  if (is_nested && insert_mode != ResolvedInsertStmt::OR_ERROR) {
    std::string insert_mode_str;
    switch (insert_mode) {
      case ResolvedInsertStmt::OR_IGNORE:
        insert_mode_str = "IGNORE";
        break;
      case ResolvedInsertStmt::OR_REPLACE:
        insert_mode_str = "REPLACE";
        break;
      case ResolvedInsertStmt::OR_UPDATE:
        insert_mode_str = "UPDATE";
        break;
      case ResolvedInsertStmt::OR_ERROR:
        GOOGLESQL_RET_CHECK_FAIL() << "Should not have made it here for OR_ERROR";
    }
    return MakeSqlErrorAt(ast_statement)
           << "Nested INSERTs cannot have insert mode " << insert_mode_str;
  }

  std::vector<std::unique_ptr<const ResolvedInsertRow>> row_list;
  std::unique_ptr<const ResolvedScan> resolved_query;
  ResolvedColumnList query_output_column_list;
  std::vector<std::unique_ptr<const ResolvedColumnRef>> query_parameter_list;
  if (ast_statement->rows() != nullptr) {
    GOOGLESQL_RET_CHECK(ast_statement->query() == nullptr);
    GOOGLESQL_RET_CHECK(pipe_input_name_list == nullptr);
    row_list.reserve(ast_statement->rows()->rows().size());
    const NameScope* value_scope =
        is_nested ? nested_scope : empty_name_scope_.get();
    for (const ASTInsertValuesRow* row : ast_statement->rows()->rows()) {
      std::unique_ptr<const ResolvedInsertRow> resolved_insert_row;
      GOOGLESQL_RETURN_IF_ERROR(ResolveInsertValuesRow(row, value_scope, insert_columns,
                                             &resolved_insert_row));
      row_list.push_back(std::move(resolved_insert_row));
    }
  } else {
    // We either have a query in the AST or we're in pipe INSERT and have
    // a pipe_input_name_list.
    GOOGLESQL_RET_CHECK_NE(ast_statement->query() != nullptr,
                 pipe_input_name_list != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ResolveInsertQuery(
        ast_statement->query() != nullptr
            ? static_cast<const ASTNode*>(ast_statement->query())
            : ast_statement,
        ast_statement->query(), nested_scope, insert_columns,
        pipe_input_name_list, std::move(pipe_input_scan), &resolved_query,
        &query_output_column_list, &query_parameter_list));
  }

  std::unique_ptr<const ResolvedAssertRowsModified>
      resolved_assert_rows_modified;
  if (ast_statement->assert_rows_modified() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveAssertRowsModified(
        ast_statement->assert_rows_modified(), &resolved_assert_rows_modified));
  }

  std::unique_ptr<const ResolvedReturningClause> resolved_returning_clause;
  if (ast_statement->returning() != nullptr) {
    if (!language().LanguageFeatureEnabled(FEATURE_DML_RETURNING)) {
      return MakeSqlErrorAt(ast_statement->returning())
             << "THEN RETURN is not supported";
    }
    if (target_name_list == nullptr) {
      return MakeSqlErrorAt(ast_statement->returning())
             << "THEN RETURN is not allowed in nested INSERT statements";
    }
    const std::unique_ptr<const NameScope> target_scope(
        new NameScope(*target_name_list));
    GOOGLESQL_RETURN_IF_ERROR(ResolveReturningClause(
        ast_statement->returning(), target_alias, target_name_list,
        target_scope.get(), &resolved_returning_clause));
  }

  std::unique_ptr<const ResolvedOnConflictClause> resolved_on_conflict_clause;
  if (ast_statement->on_conflict() != nullptr) {
    if (!language().LanguageFeatureEnabled(FEATURE_INSERT_ON_CONFLICT_CLAUSE)) {
      return MakeSqlErrorAt(ast_statement->on_conflict())
             << "ON CONFLICT is not supported";
    }
    if (insert_mode != ResolvedInsertStmt::OR_ERROR) {
      return MakeSqlErrorAt(ast_statement->on_conflict())
             << "ON CONFLICT is not allowed with insert mode: "
             << ResolvedInsertStmt::InsertModeToString(insert_mode);
    }
    if (is_nested) {
      return MakeSqlErrorAt(ast_statement->on_conflict())
             << "Nested INSERTs cannot have ON CONFLICT clause";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* target_path,
                     ast_statement->GetTargetPathForNonNested());
    GOOGLESQL_RETURN_IF_ERROR(ResolveOnConflictClause(
        target_path, ast_statement->on_conflict(), resolved_table_scan.get(),
        target_name_list, &resolved_on_conflict_clause));
  }

  // For nested INSERTs, 'insert_columns' contains a single reference to the
  // element_column field of the enclosing UPDATE, and
  // ResolvedInsertStmt.insert_columns is implicit.
  GOOGLESQL_RET_CHECK(!is_nested || insert_columns.size() == 1);

  std::vector<int> out_topologically_sorted_generated_column_ids;
  std::vector<std::unique_ptr<const ResolvedExpr>>
      out_generated_column_expr_list;
  if (resolved_table_scan != nullptr) {
    // Cases like nested DML on proto may have resolved table as NULL.
    GOOGLESQL_RETURN_IF_ERROR(ResolveGeneratedColumnsForDml(
        resolved_table_scan->table(),
        resolved_columns_to_catalog_columns_for_target_scan,
        &out_topologically_sorted_generated_column_ids,
        &out_generated_column_expr_list));
  }

  *output = MakeResolvedInsertStmt(
      std::move(resolved_table_scan), insert_mode,
      std::move(resolved_assert_rows_modified),
      std::move(resolved_returning_clause),
      is_nested ? ResolvedColumnList() : insert_columns,
      std::move(query_parameter_list), std::move(resolved_query),
      query_output_column_list, std::move(row_list),
      std::move(resolved_on_conflict_clause),
      out_topologically_sorted_generated_column_ids,
      std::move(out_generated_column_expr_list));
  MaybeRecordResolvedNodeOperatorKeywordLocation(ast_statement, output->get());
  return absl::OkStatus();
}

absl::StatusOr<ResolvedOnConflictClause::ConflictAction> GetOnConflictAction(
    const ASTOnConflictClause* ast_node) {
  switch (ast_node->conflict_action()) {
    case ASTOnConflictClause::NOTHING:
      return ResolvedOnConflictClause::NOTHING;
    case ASTOnConflictClause::UPDATE:
      return ResolvedOnConflictClause::UPDATE;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << absl::StrCat("Unexpected conflict action: ",
                                       ast_node->conflict_action());
  }
}

absl::Status Resolver::ResolveOnConflictClause(
    const ASTPathExpression* insert_target_path,
    const ASTOnConflictClause* ast_node,
    const ResolvedTableScan* target_table_scan,
    const std::shared_ptr<const NameList>& target_name_list,
    std::unique_ptr<const ResolvedOnConflictClause>* output) {
  // 1. Get the conflict action.
  GOOGLESQL_ASSIGN_OR_RETURN(ResolvedOnConflictClause::ConflictAction conflict_action,
                   GetOnConflictAction(ast_node));

  // 2. If the conflict target is defined, then populate the conflict target
  // columns.
  ResolvedColumnList conflict_target_column_list;
  if (ast_node->conflict_target() != nullptr) {
    IdStringHashMapCase<ResolvedColumn> table_scan_columns;
    for (const ResolvedColumn& column : target_table_scan->column_list()) {
      googlesql_base::InsertIfNotPresent(&table_scan_columns, column.name_id(), column);
    }

    IdStringHashSetCase visited_column_names;
    for (const ASTIdentifier* column_name :
         ast_node->conflict_target()->identifiers()) {
      const IdString column_name_id = column_name->GetAsIdString();
      // Allow repetition of column name in the conflict target.
      if (!visited_column_names.insert(column_name_id).second) {
        continue;
      }

      const ResolvedColumn* column =
          googlesql_base::FindOrNull(table_scan_columns, column_name_id);
      if (column == nullptr) {
        return MakeSqlErrorAt(column_name)
               << "Column " << column_name->GetAsString()
               << " is not present in table "
               << target_table_scan->table()->FullName();
      }
      // Conflict target must have READ access as they are read to check if
      // constraint is violated.
      conflict_target_column_list.push_back(*column);
      RecordColumnAccess(*column, ResolvedStatement::READ);
    }
  }

  // 3. Get Unique constraint name, if present.
  IdString unique_constraint_name;
  if (ast_node->unique_constraint_name() != nullptr &&
      !ast_node->unique_constraint_name()->GetAsString().empty()) {
    GOOGLESQL_RET_CHECK(ast_node->conflict_target() == nullptr);
    unique_constraint_name =
        ast_node->unique_constraint_name()->GetAsIdString();
  }

  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  std::unique_ptr<const ResolvedExpr> resolved_where_expr;

  // 4. If action is NOTHING, no more fields are populated. Construct the
  // ResolvedOnConflictClause and return.
  if (conflict_action == ResolvedOnConflictClause::NOTHING) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        *output,
        ResolvedOnConflictClauseBuilder()
            .set_conflict_action(conflict_action)
            .set_conflict_target_column_list(
                std::move(conflict_target_column_list))
            .set_unique_constraint_name(unique_constraint_name.ToString())
            .set_insert_row_scan(nullptr)
            .set_update_item_list(std::move(update_item_list))
            .set_update_where_expression(std::move(resolved_where_expr))
            .Build());
    return absl::OkStatus();
  }

  // Resolve ON CONFLICT DO UPDATE action.

  // Conflict target is mandatory for DO UPDATE conflict action.
  if (unique_constraint_name.empty() && conflict_target_column_list.empty()) {
    return MakeSqlErrorAt(ast_node)
           << "ON CONFLICT DO UPDATE requires a constraint inference or "
           << "unique constraint name";
  }

  GOOGLESQL_RET_CHECK(ast_node->update_item_list() != nullptr)
      << "DO UPDATE must specify an update list";

  // 5. Build scan `insert_row_scan` to get column references from insert row.
  // The column references are used in SET RHS and WHERE expressions.
  std::unique_ptr<const ResolvedTableScan> insert_row_scan;
  std::shared_ptr<const NameList> insert_row_name_list(new NameList);
  ResolvedColumnToCatalogColumnHashMap
      out_resolved_columns_to_catalog_columns_for_target_scan;
  GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsTableScan(
      insert_target_path, id_string_pool_->Make("excluded"),
      /*has_explicit_alias=*/true,
      /*alias_location=*/insert_target_path, nullptr,
      /*for_system_time=*/nullptr, empty_name_scope_.get(),
      /*read_as_row_type_error_kind=*/"DML statements",
      /*remaining_names=*/nullptr, &insert_row_scan, &insert_row_name_list,
      /*output_column_name_list=*/nullptr,
      out_resolved_columns_to_catalog_columns_for_target_scan));

  resolved_columns_from_table_scans_.insert(
      out_resolved_columns_to_catalog_columns_for_target_scan.begin(),
      out_resolved_columns_to_catalog_columns_for_target_scan.end());

  // 6. Construct the update item list from UPDATE..SET clause and the optional
  // WHERE clause expression.

  // `update_scope` sets the columns that can be read/referenced in UPDATE SET
  // and WHERE clause. It is a union of columns from table row
  // (i.e target table scan) and the insert row (i.e. insert row scan built
  // above).
  std::unique_ptr<NameList> update_name_list = std::make_unique<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(
      update_name_list->MergeFrom(*target_name_list, insert_target_path));
  GOOGLESQL_RETURN_IF_ERROR(update_name_list->MergeFrom(*insert_row_name_list, ast_node));
  const std::unique_ptr<const NameScope> update_scope(
      new NameScope(*update_name_list));

  // `target_scope` sets the column that can be updated i.e. LHS of SET clause.
  // Only columns in the target table can be updated.
  const std::unique_ptr<const NameScope> target_scope(
      new NameScope(*target_name_list));

  // Resolve UPDATE SET clause
  GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateItemList(ast_node->update_item_list(),
                                        /*is_nested=*/false, target_scope.get(),
                                        update_scope.get(), &update_item_list));

  // Resolve UPDATE...WHERE clause
  std::unique_ptr<const ResolvedExpr> update_where_expr;
  if (ast_node->update_where_clause() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_node->update_where_clause(),
                                      update_scope.get(), "DO UPDATE scope",
                                      &update_where_expr));
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(ast_node->update_where_clause(),
                                     "WHERE clause", &update_where_expr));
  }

  // 7. Build and return the ResolvedOnConflictClause.

  // It is possible that UPDATE SET and/or WHERE clause does not references any
  // columns from insert row, in which case, `insert_row_scan` is not needed.
  // We cannot rely on automatic pruning of column lists as it will leave a
  // dangling table scan with no columns.
  // So, we manually handle it by not adding the insert row scan to
  // ResolvedOnConflictClause if none of its column are referenced.
  int num_referenced_columns = 0;
  for (const auto column : insert_row_scan->column_list()) {
    if (googlesql_base::ContainsKey(referenced_column_access_, column)) {
      num_referenced_columns++;
    }
  }
  bool insert_row_referenced_in_update_clause = (num_referenced_columns > 0);
  GOOGLESQL_ASSIGN_OR_RETURN(
      *output,
      ResolvedOnConflictClauseBuilder()
          .set_conflict_action(conflict_action)
          .set_conflict_target_column_list(
              std::move(conflict_target_column_list))
          .set_unique_constraint_name(unique_constraint_name.ToString())
          .set_insert_row_scan(insert_row_referenced_in_update_clause
                                   ? std::move(insert_row_scan)
                                   : nullptr)
          .set_update_item_list(std::move(update_item_list))
          .set_update_where_expression(std::move(update_where_expr))
          .Build());
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDMLValue(
    const ASTExpression* ast_value, AnnotatedType annotated_target_type,
    const NameScope* scope, const char* clause_name,
    CoercionErrorMessageFunction coercion_err_msg,
    std::unique_ptr<const ResolvedDMLValue>* output) {
  GOOGLESQL_RET_CHECK(ast_value != nullptr);
  std::unique_ptr<const ResolvedExpr> resolved_value;
  if (ast_value->node_kind() == AST_DEFAULT_LITERAL) {
    resolved_value = MakeResolvedDMLDefault(annotated_target_type.type);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_value, scope, clause_name,
                                      &resolved_value,
                                      annotated_target_type.type));
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(ast_value, annotated_target_type,
                                     kImplicitAssignment, coercion_err_msg,
                                     &resolved_value));
  }

  *output = MakeResolvedDMLValue(std::move(resolved_value));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDMLValue(
    const ASTNode* ast_location, const ResolvedColumn& referenced_column,
    AnnotatedType annotated_target_type,
    CoercionErrorMessageFunction coercion_err_msg,
    std::unique_ptr<const ResolvedDMLValue>* output) {
  std::unique_ptr<const ResolvedExpr> resolved_value =
      MakeColumnRef(referenced_column);
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(ast_location, annotated_target_type,
                                   kImplicitAssignment, coercion_err_msg,
                                   &resolved_value));
  *output = MakeResolvedDMLValue(std::move(resolved_value));
  return absl::OkStatus();
}

// Returns the path to the target to be updated inside the <ast_update_item>.
static const ASTGeneralizedPathExpression* GetTargetPath(
    const ASTUpdateItem* ast_update_item) {
  if (ast_update_item->set_value() != nullptr) {
    return ast_update_item->set_value()->path();
  } else if (ast_update_item->delete_statement() != nullptr) {
    return ast_update_item->delete_statement()->GetTargetPathForNested();
  } else if (ast_update_item->update_statement() != nullptr) {
    return ast_update_item->update_statement()->GetTargetPathForNested();
  } else {
    ABSL_DCHECK(ast_update_item->insert_statement() != nullptr);
    return ast_update_item->insert_statement()->GetTargetPathForNested();
  }
}

// Returns a string representation of <path> for use in error messages. Array
// index expressions are omitted for the sake of clarity. For example, for
// a.b[<huge expression>].c, we simply return "a.b[].c".
static std::string GeneralizedPathAsString(
    const ASTGeneralizedPathExpression* path) {
  GOOGLESQL_DCHECK_OK(ASTGeneralizedPathExpression::VerifyIsPureGeneralizedPathExpression(
      path));
  switch (path->node_kind()) {
    case AST_PATH_EXPRESSION:
      return path->GetAsOrDie<ASTPathExpression>()->ToIdentifierPathString();
    case AST_DOT_GENERALIZED_FIELD: {
      const auto* dot_generalized_field =
          path->GetAsOrDie<ASTDotGeneralizedField>();
      return absl::StrCat(
          GeneralizedPathAsString(
              static_cast<const ASTGeneralizedPathExpression*>(
                  dot_generalized_field->expr())),
          ".(", dot_generalized_field->path()->ToIdentifierPathString(), ")");
    }
    case AST_DOT_IDENTIFIER: {
      const auto* dot_identifier = path->GetAsOrDie<ASTDotIdentifier>();
      return absl::StrCat(
          GeneralizedPathAsString(
              static_cast<const ASTGeneralizedPathExpression*>(
                  dot_identifier->expr())),
          ".", ToIdentifierLiteral(dot_identifier->name()->GetAsIdString()));
    }
    case AST_ARRAY_ELEMENT: {
      const auto* array_element = path->GetAsOrDie<ASTArrayElement>();
      return absl::StrCat(GeneralizedPathAsString(
                              static_cast<const ASTGeneralizedPathExpression*>(
                                  array_element->array())),
                          "[]");
    }
    default:
      const std::string ret =
          absl::StrCat("Unexpected node kind in GeneralizedPathAsString: ",
                       path->GetNodeKindString());
      ABSL_DCHECK(false) << ret;
      return ret;
  }
}

// Returns the alias of the target to be updated inside the <ast_update_item>.
static const ASTAlias* GetTargetAlias(const ASTUpdateItem* ast_update_item) {
  if (ast_update_item->set_value() != nullptr ||
      ast_update_item->insert_statement() != nullptr) {
    return nullptr;
  } else if (ast_update_item->delete_statement() != nullptr) {
    return ast_update_item->delete_statement()->alias();
  } else {
    ABSL_DCHECK(ast_update_item->update_statement() != nullptr);
    return ast_update_item->update_statement()->alias();
  }
}

// All the resolved expression arguments to the helper static methods (mentioned
// below) should pass VerifyUpdateTarget(...), i.e. those expressions should
// either resolve to a column or field path inside a column.

static int GetFieldPathDepth(const ResolvedExpr* expr) {
  const ResolvedNodeKind node_kind = expr->node_kind();
  if (node_kind == RESOLVED_GET_PROTO_FIELD) {
    return 1 + GetFieldPathDepth(expr->GetAs<ResolvedGetProtoField>()->expr());
  } else if (node_kind == RESOLVED_GET_STRUCT_FIELD) {
    return 1 + GetFieldPathDepth(expr->GetAs<ResolvedGetStructField>()->expr());
  } else if (node_kind == RESOLVED_GET_JSON_FIELD) {
    return 1 + GetFieldPathDepth(expr->GetAs<ResolvedGetJsonField>()->expr());
  } else {
    ABSL_DCHECK_EQ(node_kind, RESOLVED_COLUMN_REF);
    return 0;
  }
}

static const ResolvedExpr* StripLastnFields(const ResolvedExpr* expr, int n) {
  if (n == 0) {
    // Nothing to strip. Simply return the given expr.
    return expr;
  }

  const ResolvedNodeKind node_kind = expr->node_kind();
  if (node_kind == RESOLVED_GET_PROTO_FIELD) {
    return StripLastnFields(expr->GetAs<ResolvedGetProtoField>()->expr(),
                            n - 1);
  } else if (node_kind == RESOLVED_GET_JSON_FIELD) {
    return StripLastnFields(expr->GetAs<ResolvedGetJsonField>()->expr(), n - 1);
  } else {
    ABSL_DCHECK_EQ(node_kind, RESOLVED_GET_STRUCT_FIELD);
    return StripLastnFields(expr->GetAs<ResolvedGetStructField>()->expr(),
                            n - 1);
  }
}

// Returns true:
// - if is_subscript_update and IsSameFieldPath(<field_path1>, <field_path2>,
// kUpdateItemElement) is true.
// - if !is_subscript_update and IsSameFieldPath(<field_path1>, <field_path2>,
// kFieldPath) is true.
// - if <field_path2> is a direct field or is any descendent sub-field inside
//   the object <field_path1> or vice versa.
// Otherwise false.
static bool AreFieldPathsOverlapping(const ResolvedExpr* field_path1,
                                     const ResolvedExpr* field_path2,
                                     bool is_subscript_update) {
  const int field_path1_depth = GetFieldPathDepth(field_path1);
  const int field_path2_depth = GetFieldPathDepth(field_path2);
  const int compare_depth = std::min(field_path1_depth, field_path2_depth);
  field_path1 =
      StripLastnFields(field_path1, field_path1_depth - compare_depth);
  field_path2 =
      StripLastnFields(field_path2, field_path2_depth - compare_depth);
  bool same_field_path = IsSameFieldPath(
      field_path1, field_path2,
      is_subscript_update ? FieldPathMatchingOption::kUpdateItemElement
                          : FieldPathMatchingOption::kFieldPath);
  return same_field_path;
}

absl::Status Resolver::ResolveUpdateItemList(
    const ASTUpdateItemList* ast_update_item_list, bool is_nested,
    const NameScope* target_scope, const NameScope* update_scope,
    std::vector<std::unique_ptr<const ResolvedUpdateItem>>* update_item_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::vector<UpdateItemAndLocation> update_items;
  for (const ASTUpdateItem* ast_update_item :
       ast_update_item_list->update_items()) {
    std::unique_ptr<ResolvedUpdateItem> resolved_update_item;
    GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateItem(ast_update_item, is_nested, target_scope,
                                      update_scope, &update_items));
  }

  for (UpdateItemAndLocation& update_item : update_items) {
    update_item_list->push_back(std::move(update_item.resolved_update_item));
  }

  return absl::OkStatus();
}

namespace {
bool IsLiteralWithEqualValue(const ResolvedExpr* expr1,
                             const ResolvedExpr* expr2) {
  if (expr1->Is<ResolvedLiteral>() && expr2->Is<ResolvedLiteral>()) {
    return expr1->GetAs<ResolvedLiteral>()->value() ==
           expr2->GetAs<ResolvedLiteral>()->value();
  }
  return false;
}
}  // namespace

absl::StatusOr<bool> Resolver::AreOverlappingUpdateItemsWithSubscript(
    absl::Span<const UpdateTargetInfo> update_target_infos,
    const ResolvedUpdateItem* resolved_update_item, bool is_subscript_update) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Base Case. If there are no more update targets, then they must have been
  // overlapping.
  if (update_target_infos.empty()) {
    return true;
  }
  bool update_target_has_subscript =
      update_target_infos.front().subscript_expr != nullptr;
  bool update_item_has_subscript =
      !resolved_update_item->update_item_element_list().empty();
  // If both the update target and the update item have subscripts, then
  // it is necessary to check that they are the same field path to see if they
  // are overlapping. For example json.a[0](.b.c) and json.a[0](.d.e.). This
  // will check that json.a and json.a are the same, then check that the literal
  // 0 in the subscript is the same, then recurse into checking b.c against d.e.
  if (update_target_has_subscript && update_item_has_subscript) {
    // If `is_subscript_update` is true, then do not check for the same
    // `RESOLVE_COLUMN_REF` between update targets.
    bool same_field_path = IsSameFieldPath(
        update_target_infos.front().target.get(),
        resolved_update_item->target(),
        is_subscript_update ? FieldPathMatchingOption::kUpdateItemElement
                            : FieldPathMatchingOption::kFieldPath);
    if (same_field_path &&
        update_target_infos.front().subscript_expr->Is<ResolvedLiteral>()) {
      // Check to make sure that the subscripts are the same. If not, then
      // the update is not overlapping. Otherwise, we recurse.
      for (const auto& element :
           resolved_update_item->update_item_element_list()) {
        if (IsLiteralWithEqualValue(
                update_target_infos.front().subscript_expr.get(),
                element->subscript())) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              bool are_overlapping,
              AreOverlappingUpdateItemsWithSubscript(
                  absl::MakeSpan(update_target_infos).subspan(1),
                  element->update_item(), /*is_subscript_update=*/true));
          if (are_overlapping) {
            return true;
          }
        }
      }
    }
  }
  // In the case where exactly one of the update target or update item has a
  // subscript then they can only be overlapping if the target not
  // containing the subscript is a prefix of the update item. If neither have
  // a subscript then still check if they are overlapping.
  //
  // This check is necessary for a case like
  // UPDATE json["a"], json.b
  // where json overlaps with json.b, but json["a"] does not.
  int update_target_depth =
      GetFieldPathDepth(update_target_infos.front().target.get());
  int update_item_depth = GetFieldPathDepth(resolved_update_item->target());
  // Check for overlap
  bool check_overlap =
      // 1. If neither have a subscript.
      (!update_target_has_subscript && !update_item_has_subscript) ||
      // 2. If the target which has a subscript has a longer field path than
      // the one which does not. For example check for overlap between
      // json.a.b["c"] and json.a but not json["a"](.b.c) and json.a.b.
      (update_target_has_subscript && !update_item_has_subscript &&
       update_target_depth >= update_item_depth) ||
      (!update_target_has_subscript && update_item_has_subscript &&
       update_item_depth >= update_target_depth);
  if (check_overlap) {
    return AreFieldPathsOverlapping(update_target_infos.front().target.get(),
                                    resolved_update_item->target(),
                                    is_subscript_update);
  }
  return false;
}

absl::Status Resolver::ResolveUpdateItem(
    const ASTUpdateItem* ast_update_item, bool is_nested,
    const NameScope* target_scope, const NameScope* update_scope,
    std::vector<UpdateItemAndLocation>* update_items) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ASTGeneralizedPathExpression* target_path =
      GetTargetPath(ast_update_item);
  auto target_no_aggregation =
      std::make_unique<ExprResolutionInfo>(target_scope, "UPDATE clause");
  std::vector<UpdateTargetInfo> update_target_infos;
  GOOGLESQL_RETURN_IF_ERROR(PopulateUpdateTargetInfos(
      ast_update_item, is_nested, target_path, target_no_aggregation.get(),
      &update_target_infos));
  GOOGLESQL_RET_CHECK(!update_target_infos.empty());
  // Look for an existing ResolvedUpdateItem node to merge with this update,
  // detecting conflicts in the process.
  for (UpdateItemAndLocation& update_item : *update_items) {
    bool merge = false;
    // Also catches conflicts between two targets.
    GOOGLESQL_RETURN_IF_ERROR(ShouldMergeWithUpdateItem(
        ast_update_item, update_target_infos, update_item, &merge));
    if (merge) {
      return MergeWithUpdateItem(update_scope, ast_update_item,
                                 &update_target_infos, &update_item);
    }
  }

  UpdateItemAndLocation new_update_item;
  GOOGLESQL_RETURN_IF_ERROR(MergeWithUpdateItem(update_scope, ast_update_item,
                                      &update_target_infos, &new_update_item));
  update_items->emplace_back(std::move(new_update_item));

  return absl::OkStatus();
}

static bool TypeSupportsSubscriptElementUpdate(
    const Type* type, const LanguageOptions& language_options) {
  switch (type->kind()) {
    case TYPE_ARRAY:
      return language_options.LanguageFeatureEnabled(
          FEATURE_ARRAY_ELEMENTS_WITH_SET);
    case TYPE_JSON:
      return language_options.LanguageFeatureEnabled(
          FEATURE_JSON_SUBFIELDS_WITH_SET);
    case TYPE_MAP:
      return language_options.LanguageFeatureEnabled(
          FEATURE_MAP_SUBSCRIPT_WITH_SET);
    default:
      return false;
  }
}

absl::Status Resolver::VerifyIfElementUpdateIsAllowed(
    const Type* update_target_type, googlesql::LanguageOptions language_options,
    const ASTArrayElement* array_element) {
  if (!TypeSupportsSubscriptElementUpdate(update_target_type,
                                          language_options)) {
    if (update_target_type->IsArray()) {
      return MakeSqlErrorAt(array_element->position())
             << "UPDATE ... SET does not support array modification with []";
    }

    return MakeSqlErrorAt(array_element->position())
           << "UPDATE ... SET does not support value modification with [] "
           << "for type " << update_target_type->ShortTypeName(product_mode());
  }

  return absl::OkStatus();
}

absl::Status Resolver::PopulateUpdateTargetInfos(
    const ASTUpdateItem* ast_update_item, bool is_nested,
    const ASTGeneralizedPathExpression* path,
    ExprResolutionInfo* expr_resolution_info,
    std::vector<UpdateTargetInfo>* update_target_infos) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK_OK(
      ASTGeneralizedPathExpression::VerifyIsPureGeneralizedPathExpression(
          path));
  switch (path->node_kind()) {
    case AST_PATH_EXPRESSION: {
      // The first UpdateTargetInfo tells us what column we are modifying.
      // Any remaining UpdateTargetInfos just contain more information about how
      // we are modifying that column.
      const bool is_write = update_target_infos->empty();

      UpdateTargetInfo info;
      GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsExpression(
          PathExpressionSpan(*path->GetAsOrDie<ASTPathExpression>()),
          expr_resolution_info,
          is_write ? ResolvedStatement::WRITE : ResolvedStatement::READ,
          &info.target));
      // We must check whether the column is writable.
      if (is_write && !is_nested) {
        const ASTExpression* value = nullptr;
        if (ast_update_item->set_value() != nullptr) {
          value = ast_update_item->set_value()->value();
        }
        GOOGLESQL_RETURN_IF_ERROR(
            VerifyUpdateTargetIsWritable(path, info.target.get(), value));
      }
      update_target_infos->emplace_back(std::move(info));
      return absl::OkStatus();
    }
    case AST_DOT_GENERALIZED_FIELD: {
      const auto* dot_generalized_field =
          path->GetAsOrDie<ASTDotGeneralizedField>();
      if (!language().LanguageFeatureEnabled(
              FEATURE_PROTO_EXTENSIONS_WITH_SET)) {
        // Using "proto" in the error message is clearer than "generalized field
        // access", but we can't do that for external engines that don't support
        // protos at all.
        return MakeSqlErrorAt(dot_generalized_field->path())
               << "UPDATE ... SET does not support "
               << (language().SupportsProtoTypes()
                       ? "proto extensions"
                       : "generalized field access");
      }
      GOOGLESQL_RETURN_IF_ERROR(PopulateUpdateTargetInfos(
          ast_update_item, is_nested,
          static_cast<const ASTGeneralizedPathExpression*>(
              dot_generalized_field->expr()),
          expr_resolution_info, update_target_infos));
      GOOGLESQL_RET_CHECK(!update_target_infos->empty());
      UpdateTargetInfo& info = update_target_infos->back();
      return ResolveExtensionFieldAccess(
          std::move(info.target), ResolveExtensionFieldOptions(),
          dot_generalized_field->path(), &expr_resolution_info->flatten_state,
          &info.target);
    }
    case AST_DOT_IDENTIFIER: {
      const auto* dot_identifier = path->GetAsOrDie<ASTDotIdentifier>();
      GOOGLESQL_RETURN_IF_ERROR(PopulateUpdateTargetInfos(
          ast_update_item, is_nested,
          static_cast<const ASTGeneralizedPathExpression*>(
              dot_identifier->expr()),
          expr_resolution_info, update_target_infos));
      GOOGLESQL_RET_CHECK(!update_target_infos->empty());
      UpdateTargetInfo& info = update_target_infos->back();
      return ResolveFieldAccess(
          std::move(info.target), dot_identifier->location(),
          dot_identifier->name(), &expr_resolution_info->flatten_state,
          &info.target);
    }
    // Note: The naming of AST_ARRAY_ELEMENT is overly specific. This node
    // may also represent any subscript[] access.
    case AST_ARRAY_ELEMENT: {
      const auto* array_element = path->GetAsOrDie<ASTArrayElement>();

      // We do not allow nested DML statements to use array element
      // modification. To see why, consider this example:
      //     UPDATE T SET
      //       (DELETE a[OFFSET(0)].b WHERE b = 0),
      //       (UPDATE a[OFFSET(0)].b SET b = 2 WHERE b = 1)
      //     WHERE true
      //
      // Ideally, this statement would run (nested DELETEs and UPDATEs are
      // allowed to modify the same target as long as DELETEs occur before
      // UPDATEs). But because we have decided for the array element
      // modification feature to result in simple resolved ASTs that can be
      // easily executed by the engine, we would have to require the engine to
      // fail this statement on the grounds that both nested statements modify
      // the same element of T.a. That would be confusing behavior, so we choose
      // to simply disallow the above statement. Performing a modification
      // involving two levels of nested arrays is a somewhat complicated
      // operation anyway, so it is ok to require the more verbose variant:
      //
      //     UPDATE T SET
      //       (UPDATE a WITH OFFSET offset SET
      //          (DELETE b WHERE b = 0),
      //          (UPDATE b SET b = 2 WHERE b = 1),
      //        WHERE offset = 0)
      //     WHERE true
      GOOGLESQL_RET_CHECK_EQ(ast_update_item->set_value() == nullptr,
                   (ast_update_item->insert_statement() != nullptr) ||
                       (ast_update_item->delete_statement() != nullptr) ||
                       (ast_update_item->update_statement() != nullptr));
      if (ast_update_item->set_value() == nullptr) {
        return MakeSqlErrorAt(array_element->position())
               << "The target of a nested DML statement cannot reference an "
               << "array element";
      }

      GOOGLESQL_RETURN_IF_ERROR(PopulateUpdateTargetInfos(
          ast_update_item, is_nested,
          static_cast<const ASTGeneralizedPathExpression*>(
              array_element->array()),
          expr_resolution_info, update_target_infos));
      GOOGLESQL_RET_CHECK(!update_target_infos->empty());
      UpdateTargetInfo& info = update_target_infos->back();

      GOOGLESQL_RETURN_IF_ERROR(VerifyIfElementUpdateIsAllowed(
          info.target->type(), language(), array_element));

      absl::string_view function_name;
      std::vector<std::string> function_name_path;
      const ASTExpression* unwrapped_ast_position_expr;
      bool is_array = info.target->type()->IsArray();
      std::string original_wrapper_name("");
      if (is_array) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveArrayElementAccess(
            info.target.get(), array_element->position(), expr_resolution_info,
            &function_name, &unwrapped_ast_position_expr, &info.subscript_expr,
            &original_wrapper_name));
      } else {
        std::unique_ptr<const ResolvedExpr> resolved_position;
        std::string original_wrapper_name("");
        GOOGLESQL_RETURN_IF_ERROR(ResolveNonArraySubscriptElementAccess(
            info.target.get(), array_element->position(), expr_resolution_info,
            &function_name_path, &unwrapped_ast_position_expr,
            &info.subscript_expr, &original_wrapper_name));
        if (info.target->type()->IsJson()) {
          AnnotatedType int64_type = AnnotatedType(types::Int64Type());
          AnnotatedType string_type = AnnotatedType(types::StringType());
          auto coerce_to_int64 = CoerceExprToType(
              array_element->position(), int64_type, kImplicitCoercion,
              /*error_template=*/"", &info.subscript_expr);
          auto coerce_to_string = CoerceExprToType(
              array_element->position(), string_type, kImplicitCoercion,
              /*error_template=*/"", &info.subscript_expr);
          if (!coerce_to_int64.ok() && !coerce_to_string.ok()) {
            return MakeSqlErrorAt(array_element->position())
                   << "JSON subscript expression in [] must be coercible to "
                      "type STRING or INT64 but has type: "
                   << info.subscript_expr->type()->ShortTypeName(
                          product_mode());
          }
        } else if (info.target->type()->IsMap()) {
          GOOGLESQL_RET_CHECK(info.target->type_annotation_map() == nullptr)
              << "Unexpected for a MAP type to allow annotations";
          AnnotatedType annotated_map_key =
              AnnotatedType(info.target->type()->AsMap()->key_type(), {});
          GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
              array_element->position(), annotated_map_key, kImplicitCoercion,
              "Cannot coerce type $1 to MAP key type $0.",
              &info.subscript_expr));
        } else {
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type with subscript in "
                              "PopulateUpdateTargetInfos: "
                           << info.target->type()->DebugString();
        }
        function_name = function_name_path.back();
      }

      if (function_name == kSafeArrayAtOffset) {
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support array[SAFE_OFFSET(...)]; "
               << "use OFFSET instead";
      } else if (function_name == kSafeArrayAtOrdinal) {
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support array[SAFE_ORDINAL(...)]; "
               << "use ORDINAL instead";
      } else if (function_name == kSafeSubscriptWithOffset) {
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support [SAFE_OFFSET(...)] access "
                  "on "
               << info.target->type()->ShortTypeName(product_mode());
      } else if (function_name == kSafeSubscriptWithOrdinal) {
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support [SAFE_ORDINAL(...)] access "
                  "on "
               << info.target->type()->ShortTypeName(product_mode());
      } else if (function_name == kProtoMapAtKey ||
                 function_name == kSafeProtoMapAtKey) {
        // GoogleSQL does not currently support updating proto map entries
        // specified by key.
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support updating proto map entries "
               << "by key";
      } else if (function_name == kArrayAtOffset ||
                 function_name == kArrayAtOrdinal) {
        GOOGLESQL_RET_CHECK(is_array);
      } else if (function_name == kSubscriptWithKey) {
        info.element_presence_constraint =
            ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_REQUIRED;
      } else if (function_name == kSubscriptWithOffset) {
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support [OFFSET(...)] access on "
               << info.target->type()->ShortTypeName(product_mode());
      } else if (function_name == kSubscriptWithOrdinal) {
        return MakeSqlErrorAt(array_element->position())
               << "UPDATE ... SET does not support [ORDINAL(...)] access on "
               << info.target->type()->ShortTypeName(product_mode());
      } else if (function_name == kSubscript) {
        GOOGLESQL_RET_CHECK(info.target->type()->IsJson() ||
                  info.target->type()->IsMap());
      } else {
        GOOGLESQL_RET_CHECK_FAIL()
            << "Unexpected subscript function in PopulateUpdateTargetInfos: "
            << function_name;
      }

      // Apply a simple subtraction rewrite for ORDINAL() to make it 0-based.
      if (function_name == kArrayAtOrdinal) {
        // 'info.array_offset' is 1-based. Subtract 1 to make it 0-based.
        absl::string_view subtraction_name =
            FunctionResolver::BinaryOperatorToFunctionName(
                ASTBinaryExpression::MINUS, /*is_not=*/false,
                /*not_handled=*/nullptr);

        std::vector<std::unique_ptr<const ResolvedExpr>> subtraction_args;
        subtraction_args.push_back(std::move(info.subscript_expr));
        subtraction_args.push_back(
            MakeResolvedLiteralWithoutLocation(Value::Int64(1)));

        // This is not expected to fail, so just pretend
        // 'array_element->position()' is the corresponding AST node and that
        // both arg locations are 'unwrapped_ast_position_expr'. The AST nodes
        // are only used for error logging.
        GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
            array_element->position(),
            /*arg_locations=*/
            {unwrapped_ast_position_expr, unwrapped_ast_position_expr},
            /*match_internal_signatures=*/false, subtraction_name,
            std::move(subtraction_args), /*named_arguments=*/{},
            expr_resolution_info, &info.subscript_expr));
      }

      const Type* update_target_type;
      if (info.target->type()->IsArray()) {
        update_target_type = info.target->type()->AsArray()->element_type();
      } else if (info.target->type()->IsMap()) {
        update_target_type = info.target->type()->AsMap()->value_type();
      } else if (info.target->type()->IsJson()) {
        update_target_type = info.target->type();
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type in PopulateUpdateTargetInfos: "
                         << info.target->type()->DebugString();
      }

      info.subscripted_element = std::make_unique<ResolvedColumn>(
          AllocateColumnId(), /*table_name=*/kArrayId,
          /*column_name=*/kElementId, update_target_type);

      std::unique_ptr<ResolvedColumnRef> ref = MakeResolvedColumnRef(
          info.subscripted_element->type(), *info.subscripted_element,
          /*is_correlated=*/false);
      info.subscripted_element_ref = ref.get();

      UpdateTargetInfo new_info;
      new_info.target = std::move(ref);
      update_target_infos->emplace_back(std::move(new_info));
      return absl::OkStatus();
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected node kind in PopulateUpdateTargetInfos: "
                       << path->GetNodeKindString();
  }
}

absl::Status Resolver::VerifyUpdateTargetIsWritable(
    const ASTNode* ast_location, const ResolvedExpr* target,
    const ASTExpression* value) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  switch (target->node_kind()) {
    case RESOLVED_COLUMN_REF:
      return VerifyTableScanColumnIsWritable(
          ast_location, target->GetAs<ResolvedColumnRef>()->column(), "UPDATE",
          value);
    case RESOLVED_GET_PROTO_FIELD: {
      const ResolvedGetProtoField* get_proto_field =
          target->GetAs<ResolvedGetProtoField>();
      if (get_proto_field->get_has_bit()) {
        return MakeSqlErrorAt(ast_location)
               << "UPDATE ... SET cannot modify proto has bit";
      }
      return VerifyUpdateTargetIsWritable(ast_location, get_proto_field->expr(),
                                          value);
    }
    case RESOLVED_GET_STRUCT_FIELD:
      return VerifyUpdateTargetIsWritable(
          ast_location, target->GetAs<ResolvedGetStructField>()->expr(), value);
    case RESOLVED_GET_JSON_FIELD:
      if (language().LanguageFeatureEnabled(FEATURE_JSON_SUBFIELDS_WITH_SET)) {
        return absl::OkStatus();
      } else {
        return MakeSqlErrorAt(ast_location)
               << "UPDATE ... SET does not support modifying a JSON field";
      }
    case RESOLVED_MAKE_STRUCT:
      return MakeSqlErrorAt(ast_location)
             << "UPDATE ... SET does not support updating the entire row";
    case RESOLVED_CONSTANT:
      return MakeSqlErrorAt(ast_location)
             << "Constant "
             << target->GetAs<ResolvedConstant>()->constant()->Name()
             << " cannot be used as a column to update";
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "Unexpected node kind in VerifyUpdateTargetIsWritable: "
          << ast_location->GetNodeKindString();
  }
}

absl::StatusOr<bool> Resolver::IsColumnWritable(const ResolvedColumn& column) {
  // ROW types don't work in DML yet. This gives an error to avoid RET_CHECks.
  if (column.type()->IsRow()) {
    return false;
  }
  const googlesql::Column** catalog_column =
      googlesql_base::FindOrNull(resolved_columns_from_table_scans_, column);
  GOOGLESQL_RET_CHECK(catalog_column);
  return (*catalog_column)->IsWritableColumn();
}

namespace {

// Returns true if catalog_column is writable or, if value is not nullptr, if
// catalog_column can be updated to value.
bool IsColumnWritableOrCanBeUpdatedToValue(
    const googlesql::Column* catalog_column, const ASTExpression* value) {
  if (catalog_column->IsWritableColumn()) return true;
  return value != nullptr && value->Is<ASTDefaultLiteral>() &&
         catalog_column->CanUpdateUnwritableToDefault();
}

}  // namespace

absl::Status Resolver::VerifyTableScanColumnIsWritable(
    const ASTNode* ast_location, const ResolvedColumn& column,
    const char* statement_type, const ASTExpression* value) {
  bool writable = true;
  std::string name;
  if (column.type()->IsRow()) {
    // ROW types don't work in DML yet. This gives an error to avoid RET_CHECks.
    writable = false;
    name = "ROW-typed value";
  } else {
    const googlesql::Column** catalog_column =
        googlesql_base::FindOrNull(resolved_columns_from_table_scans_, column);
    GOOGLESQL_RET_CHECK(catalog_column);
    if (!IsColumnWritableOrCanBeUpdatedToValue(*catalog_column, value)) {
      writable = false;
      name = (*catalog_column)->Name();
    }
  }
  if (!writable) {
    return MakeSqlErrorAt(ast_location)
           << "Cannot " << statement_type
           << " value on non-writable column: " << name;
  }

  return absl::OkStatus();
}

// Verify that the nested statements inside <resolved_update_item> (referencing
// the same field) are added in the order: DELETE, UPDATE, INSERT.
// <is_nested_delete> and <is_nested_update> denotes the type of nested
// statement to be added to <resolved_update_item>.
static absl::Status VerifyNestedStatementsOrdering(
    const ASTNode* ast_location,
    const ASTGeneralizedPathExpression* target_path,
    const ResolvedUpdateItem* resolved_update_item, bool is_nested_delete,
    bool is_nested_update) {
  // Both cannot be true for a nested statement.
  ABSL_DCHECK(!is_nested_delete || !is_nested_update);

  const std::string err_message_suffix =
      "nested statements referencing the same field must be written in the"
      " order DELETE, UPDATE, INSERT";
  if (is_nested_delete) {
    if (resolved_update_item->update_list_size() > 0) {
      return MakeSqlErrorAt(ast_location)
             << "DELETE occurs after UPDATE for "
             << GeneralizedPathAsString(target_path) << "; "
             << err_message_suffix;
    }
    if (resolved_update_item->insert_list_size() > 0) {
      return MakeSqlErrorAt(ast_location)
             << "DELETE occurs after INSERT for "
             << GeneralizedPathAsString(target_path) << "; "
             << err_message_suffix;
    }
  }
  if (is_nested_update) {
    if (resolved_update_item->insert_list_size() > 0) {
      return MakeSqlErrorAt(ast_location)
             << "UPDATE occurs after INSERT for "
             << GeneralizedPathAsString(target_path) << "; "
             << err_message_suffix;
    }
  }
  return absl::OkStatus();
}

namespace {
bool UpdateItemElementHasJsonSubscript(const ResolvedExpr* target,
                                       const ResolvedExpr* subscript_expr) {
  return subscript_expr != nullptr && target->type()->IsJson();
}

bool UpdateItemElementHasJsonSubscript(
    const ResolvedExpr* target, const ResolvedColumnHolder* element_column) {
  return element_column != nullptr && target->type()->IsJson();
}

}  // namespace

absl::Status Resolver::ShouldMergeWithUpdateItem(
    const ASTUpdateItem* ast_update_item,
    absl::Span<const UpdateTargetInfo> update_target_infos,
    const UpdateItemAndLocation& update_item, bool* merge) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(!update_target_infos.empty());
  *merge = false;

  const ASTGeneralizedPathExpression* target_path =
      GetTargetPath(ast_update_item);
  // We only check the first update target info. In the case where this is not
  // a json update the overlap only checks up until the first subscript and does
  // not check if the subscripts match. In the case of a json update, the
  // overlap check will recurse through the entire `update_target_infos`.
  const UpdateTargetInfo& update_target_info = update_target_infos.front();
  // This checks that both updates being compared are json field updates.
  // Notably, `is_json_field_updates` is
  // 1. false for top level column updates: UPDATE T SET json = JSON '1'
  // 2. true for json field updates: UPDATE T SET json.a = '1'
  // 3. true for json subscript updates: UPDATE T SET json["a"]["b"] = '1'
  // 4. false for non-json updates: UPDATE T SET string_col = '1'
  // 5. false for any top level container json updates:
  //    UPDATE T SET struct.json = JSON '1'
  // 6. (TODO: b/447116823) Catch this in analyzer) false for any json field
  // updates where
  //    the json field is not at the top level. Suppose A is an array of
  //    a struct containing a json field and string field:
  //    UPDATE T SET A[0].S.a = 1
  bool is_json_field_updates =
      (update_target_info.target->Is<ResolvedGetJsonField>() ||
       UpdateItemElementHasJsonSubscript(
           update_target_info.target.get(),
           update_target_info.subscript_expr.get())) &&
      (update_item.resolved_update_item->target()->Is<ResolvedGetJsonField>() ||
       UpdateItemElementHasJsonSubscript(
           update_item.resolved_update_item->target(),
           update_item.resolved_update_item->element_column()));
  // For JSON updates we check for overlapping updates for literals in
  // subscripts. For example, `json["a"]["b"]` and `json["a"]["b"]["c"]` are
  // overlapping updates. This also makes sure we do not reject cases such as
  // json["a"] and json.b as overlapping updates.
  if (is_json_field_updates &&
      language().LanguageFeatureEnabled(FEATURE_JSON_SUBFIELDS_WITH_SET)) {
    if (AreOverlappingUpdateItemsWithSubscript(
            update_target_infos, update_item.resolved_update_item.get(),
            /*is_subscript_update=*/false)
            .value_or(false)) {
      return MakeSqlErrorAt(target_path)
             << "Update item " << GeneralizedPathAsString(target_path)
             << " overlaps with "
             << GeneralizedPathAsString(update_item.one_target_path);
    }
    *merge = IsSameFieldPath(update_target_infos.front().target.get(),
                             update_item.resolved_update_item->target(),
                             FieldPathMatchingOption::kFieldPath);
    return absl::OkStatus();
  }
  if (!IsSameFieldPath(update_target_infos.front().target.get(),
                       update_item.resolved_update_item->target(),
                       FieldPathMatchingOption::kFieldPath)) {
    if (AreFieldPathsOverlapping(update_target_infos.front().target.get(),
                                 update_item.resolved_update_item->target(),
                                 /*is_subscript_update=*/false)) {
      return MakeSqlErrorAt(target_path)
             << "Update item " << GeneralizedPathAsString(target_path)
             << " overlaps with "
             << GeneralizedPathAsString(update_item.one_target_path);
    }

    return absl::OkStatus();
  }

  const bool ast_is_set_value = ast_update_item->set_value() != nullptr;
  const bool ast_is_nested_delete =
      ast_update_item->delete_statement() != nullptr;
  const bool ast_is_nested_update =
      ast_update_item->update_statement() != nullptr;
  const bool ast_is_nested_insert =
      ast_update_item->insert_statement() != nullptr;

  GOOGLESQL_RET_CHECK_EQ(1, ast_is_set_value + ast_is_nested_delete +
                      ast_is_nested_update + ast_is_nested_insert);

  ResolvedUpdateItem* resolved_update_item =
      update_item.resolved_update_item.get();
  const bool resolved_update_item_is_nested_dml =
      resolved_update_item->delete_list_size() > 0 ||
      resolved_update_item->update_list_size() > 0 ||
      resolved_update_item->insert_list_size() > 0;

  if (update_target_infos.size() > 1) {
    // Sanity check that we don't allow [] in a nested dml target. See
    // PopulateUpdateTargetInfos() for details.
    GOOGLESQL_RET_CHECK(ast_is_set_value);

    // The error messages here log 'update_item->one_target_path' instead of
    // 'target_path', because 'target_path' may look like <array>[...].<extra
    // stuff> and we just want to log <array>.
    if (resolved_update_item->set_value() != nullptr) {
      return MakeSqlErrorAt(target_path)
             << "Cannot assign a value to "
             << GeneralizedPathAsString(update_item.one_target_path)
             << " and also modify one of its elements";
    }
    if (resolved_update_item_is_nested_dml) {
      return MakeSqlErrorAt(target_path)
             << "Cannot modify an element of "
             << GeneralizedPathAsString(update_item.one_target_path)
             << " and also modify it with a nested statement";
    }
    // GoogleSQL disallows two SET update items to modify the same array
    // element.  However, the resolver cannot in general detect if the specified
    // array elements are the same, so it currently always produces a
    // ResolvedAST with all the specified SET update items. Engines must
    // determine if the same element is modified more than once, and fail the
    // query as per GoogleSQL semantics. TODO: Consider detecting
    // overlaps for simple literal offsets and fail the statement in the
    // resolver. Also, consider extending GoogleSQL semantics to allow
    // non-overlapping cases like SET a[0].b = 4, a[0].c = 5.
    GOOGLESQL_RET_CHECK(resolved_update_item->update_item_element_list_size() > 0);
  } else if (ast_is_set_value) {
    // SET <x> = <y> where <x> does not contain an array element
    // modification. E.g., we might be setting a field or column (which might
    // have ARRAY type).
    if (resolved_update_item->set_value() != nullptr) {
      return MakeSqlErrorAt(target_path)
             << "Update item " << GeneralizedPathAsString(target_path)
             << " assigned more than once";
    }
    if (resolved_update_item->update_item_element_list_size() > 0) {
      return MakeSqlErrorAt(target_path)
             << "Cannot assign a value to "
             << GeneralizedPathAsString(target_path)
             << " and also modify one of its elements";
    }
    GOOGLESQL_RET_CHECK(resolved_update_item_is_nested_dml);
    return MakeSqlErrorAt(target_path)
           << "Update item " << GeneralizedPathAsString(target_path)
           << " cannot be assigned and also updated with a nested statement";
  } else {
    GOOGLESQL_RET_CHECK(ast_is_nested_delete || ast_is_nested_update ||
              ast_is_nested_insert);
    const char* nested_str = ast_is_nested_delete
                                 ? "DELETE"
                                 : (ast_is_nested_update ? "UPDATE" : "INSERT");
    if (resolved_update_item->set_value() != nullptr) {
      return MakeSqlErrorAt(target_path)
             << "Update item " << GeneralizedPathAsString(target_path)
             << " cannot be updated with a nested " << nested_str
             << " and also assigned a value";
    }
    if (resolved_update_item->update_item_element_list_size() > 0) {
      return MakeSqlErrorAt(target_path)
             << "Cannot modify " << GeneralizedPathAsString(target_path)
             << " with a nested statement and also modify one of its elements";
    }
    // Nested DML statements can share the same target, so there is no error
    // here if 'resolved_update_item' corresponds to nested DML. However, the
    // engine must fail an UPDATE if any element of an array matches the WHERE
    // clause in two of its nested UPDATE statements.
    GOOGLESQL_RET_CHECK(resolved_update_item_is_nested_dml);
    GOOGLESQL_RETURN_IF_ERROR(VerifyNestedStatementsOrdering(
        ast_update_item, target_path, resolved_update_item,
        ast_is_nested_delete, ast_is_nested_update));
  }

  *merge = true;
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ResolvedUpdateItemElement>>
Resolver::ResolveUpdateItemElementFromUpdateTargetInfoList(
    std::vector<UpdateTargetInfo>& update_target_infos,
    ResolvedUpdateItem*& deepest_new_resolved_update_item) {
  std::unique_ptr<ResolvedUpdateItemElement> update_item_element;

  for (size_t i = update_target_infos.size() - 1; i > 0; --i) {
    UpdateTargetInfo& target_info = update_target_infos[i];

    const bool deepest = i == (update_target_infos.size() - 1);

    GOOGLESQL_RET_CHECK_EQ(deepest, target_info.subscripted_element == nullptr);
    GOOGLESQL_RET_CHECK_EQ(deepest, target_info.subscripted_element_ref == nullptr);
    // For the first iteration, 'target_info.subscript_expr' is always null
    // because it corresponds to the last UpdateTargetInfo. For subsequent
    // iterations, it was moved into 'subscript_expr' at the end of the previous
    // iteration.
    GOOGLESQL_RET_CHECK(target_info.subscript_expr == nullptr);

    // Create the ResolvedUpdateItem child of what will go in 'subscript_expr'
    // at the end of this iteration.
    std::unique_ptr<ResolvedUpdateItem> child_resolved_update_item =
        MakeResolvedUpdateItem();
    child_resolved_update_item->set_target(std::move(target_info.target));
    GOOGLESQL_RET_CHECK_EQ(deepest, update_item_element == nullptr);
    if (deepest) {
      deepest_new_resolved_update_item = child_resolved_update_item.get();
    } else {
      child_resolved_update_item->set_element_column(
          MakeResolvedColumnHolder(*target_info.subscripted_element));
      child_resolved_update_item->add_update_item_element_list(
          std::move(update_item_element));
    }
    GOOGLESQL_RET_CHECK(update_item_element == nullptr);

    UpdateTargetInfo& parent_target_info = update_target_infos[i - 1];
    GOOGLESQL_RET_CHECK(parent_target_info.subscript_expr != nullptr);

    // If there is no element presence constraint specified, we infer the
    // constraint from the type being subscripted.
    if (parent_target_info.element_presence_constraint ==
        ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_UNSPECIFIED) {
      const Type* parent_target_type = parent_target_info.target->type();
      GOOGLESQL_RET_CHECK(parent_target_type != nullptr);
      switch (parent_target_type->kind()) {
        case TYPE_ARRAY:
          parent_target_info.element_presence_constraint =
              ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_REQUIRED;
          break;
        case TYPE_JSON:
        case TYPE_MAP:
          parent_target_info.element_presence_constraint =
              ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_OPTIONAL;
          break;
        default:
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type: "
                           << parent_target_type->DebugString();
      }
    }

    update_item_element = MakeResolvedUpdateItemElement(
        std::move(parent_target_info.subscript_expr),
        std::move(child_resolved_update_item),
        parent_target_info.element_presence_constraint);
  }

  return update_item_element;
}

absl::Status Resolver::MergeWithUpdateItem(
    const NameScope* update_scope, const ASTUpdateItem* ast_input_update_item,
    std::vector<UpdateTargetInfo>* input_update_target_infos,
    UpdateItemAndLocation* merged_update_item) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ASTGeneralizedPathExpression* target_path =
      GetTargetPath(ast_input_update_item);

  // Set the target in 'merged_update_item' if it is a new UpdateItem.
  GOOGLESQL_RET_CHECK_EQ(merged_update_item->resolved_update_item == nullptr,
               merged_update_item->one_target_path == nullptr);
  if (merged_update_item->resolved_update_item == nullptr) {
    merged_update_item->resolved_update_item = MakeResolvedUpdateItem();
    merged_update_item->one_target_path = target_path;
    // Note: because setting `resolved_update_item->target` requires an
    // std::move out of `input_update_target_infos`, it is set below after the
    // ResolvedUpdateItemElements' targets are processed.
  } else {
    GOOGLESQL_RET_CHECK(merged_update_item->resolved_update_item->target() != nullptr);
    GOOGLESQL_RET_CHECK(
        IsSameFieldPath(merged_update_item->resolved_update_item->target(),
                        input_update_target_infos->front().target.get(),
                        FieldPathMatchingOption::kFieldPath))
        << "Unexpectedly different field paths:\n"
        << merged_update_item->resolved_update_item->target()->DebugString()
        << " and " << input_update_target_infos->front().target->DebugString();
  }

  GOOGLESQL_RET_CHECK(input_update_target_infos != nullptr);

  // Initially, deepest_new_resolved_update_item is set to the top level
  // ResolvedUpdateItem. If there is any nesting, it will be updated to the
  // to the innermost ResolvedUpdateItem by
  // MakeResolvedUpdateItemElementFromUpdateTargetInfos.
  ResolvedUpdateItem* deepest_new_resolved_update_item =
      merged_update_item->resolved_update_item.get();
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedUpdateItemElement> update_item_element,
      ResolveUpdateItemElementFromUpdateTargetInfoList(
          *input_update_target_infos, deepest_new_resolved_update_item));

  if (merged_update_item->resolved_update_item->target() == nullptr) {
    // Now that the target is no longer needed in the UpdateTargetInfos, we can
    // move it into the ResolvedUpdateItem if not already set.
    merged_update_item->resolved_update_item->set_target(
        std::move(input_update_target_infos->front().target));
  }
  // Now install 'update_item_element', being careful to ensure that its
  // ResolvedUpdateItem references the correct element_column if we are merging
  // 'update_item_element' into an existing UpdateItem.
  GOOGLESQL_RET_CHECK_EQ(update_item_element == nullptr,
               input_update_target_infos->size() == 1);
  if (update_item_element != nullptr) {
    // Sanity check that we are not attempting to create a
    // ResolvedUpdateItemElement for a nested DML statement.
    GOOGLESQL_RET_CHECK(ast_input_update_item->set_value() != nullptr);

    if (merged_update_item->resolved_update_item->element_column() == nullptr) {
      merged_update_item->resolved_update_item->set_element_column(
          MakeResolvedColumnHolder(
              *input_update_target_infos->front().subscripted_element));
    } else {
      ResolvedColumnRef* array_element_ref =
          input_update_target_infos->front().subscripted_element_ref;
      GOOGLESQL_RET_CHECK(array_element_ref != nullptr);
      array_element_ref->set_column(
          merged_update_item->resolved_update_item->element_column()->column());
    }

    merged_update_item->resolved_update_item->add_update_item_element_list(
        std::move(update_item_element));
  }

  // If necessary, populate 'deepest_new_resolved_update_item->set_value'.
  if (ast_input_update_item->set_value() != nullptr) {
    std::unique_ptr<const ResolvedDMLValue> set_value;
    const ASTExpression* ast_value =
        ast_input_update_item->set_value()->value();
    AnnotatedType annotated_target_type =
        deepest_new_resolved_update_item->target()->annotated_type();
    // If it's a resolved column ref then we are just setting the top level
    // column in which case we don't wrap in a TO_JSON call.
    bool is_table_column =
        deepest_new_resolved_update_item->target()->Is<ResolvedColumnRef>() &&
        merged_update_item->resolved_update_item->element_column() == nullptr;
    if (language().LanguageFeatureEnabled(FEATURE_JSON_SUBFIELDS_WITH_SET) &&
        annotated_target_type.type->IsJson() && !is_table_column) {
      std::unique_ptr<const ResolvedExpr> resolved_value;
      GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_value, update_scope,
                                        /*clause_name=*/"UPDATE clause",
                                        &resolved_value,
                                        annotated_target_type.type));
      bool convert_to_json = annotated_target_type.type->IsJson() &&
                             resolved_value != nullptr &&
                             !resolved_value->type()->IsJson();
      if (convert_to_json) {
        std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
        arguments.push_back(std::move(resolved_value));
        std::unique_ptr<const ResolvedExpr> resolved_to_json_wrapper;
        ExprResolutionInfo expr_resolution_info(update_scope, "UPDATE clause");
        GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
            ast_value, {ast_value}, /*match_internal_signatures=*/false,
            /*function_name=*/"TO_JSON", std::move(arguments),
            /*named_arguments=*/{}, &expr_resolution_info,
            &resolved_to_json_wrapper));
        set_value = MakeResolvedDMLValue(std::move(resolved_to_json_wrapper));
      } else {
        set_value = MakeResolvedDMLValue(std::move(resolved_value));
      }
    } else {
      auto make_error_msg = [target_path](absl::string_view target_t,
                                          absl::string_view arg_t) {
        return absl::Substitute(
            "Value of type $0 cannot be assigned to $2, which has type $1",
            arg_t, target_t, GeneralizedPathAsString(target_path));
      };
      GOOGLESQL_RETURN_IF_ERROR(ResolveDMLValue(
          ast_value, annotated_target_type, update_scope,
          /*clause_name=*/"UPDATE clause", make_error_msg, &set_value));
    }
    deepest_new_resolved_update_item->set_set_value(std::move(set_value));
  }

  // Populate any applicable fields in 'deepest_new_resolved_update_item' (which
  // is the same as 'update_item->resolved_update_item.get()' for nested dml
  // statements.
  const bool is_nested_delete =
      ast_input_update_item->delete_statement() != nullptr;
  const bool is_nested_update =
      ast_input_update_item->update_statement() != nullptr;
  const bool is_nested_insert =
      ast_input_update_item->insert_statement() != nullptr;
  if (is_nested_delete || is_nested_update || is_nested_insert) {
    // Sanity check that we don't allow [] in a nested dml target. See
    // PopulateUpdateTargetInfos() for details.
    GOOGLESQL_RET_CHECK_EQ(input_update_target_infos->size(), 1);
    ResolvedUpdateItem& resolved_update_item =
        *merged_update_item->resolved_update_item;
    GOOGLESQL_RET_CHECK_EQ(deepest_new_resolved_update_item, &resolved_update_item);

    const Type* target_type = resolved_update_item.target()->type();
    if (!target_type->IsArray()) {
      const std::string nested_statement_type = is_nested_delete   ? "DELETE"
                                                : is_nested_update ? "UPDATE"
                                                                   : "INSERT";
      return MakeSqlErrorAt(target_path)
             << "Update target " << GeneralizedPathAsString(target_path)
             << " for nested " << nested_statement_type
             << " must be of type ARRAY, but was "
             << target_type->ShortTypeName(product_mode());
    }

    const ASTAlias* ast_target_alias = GetTargetAlias(ast_input_update_item);
    IdString target_alias;
    if (ast_target_alias != nullptr) {
      target_alias = ast_target_alias->GetAsIdString();
    } else {
      target_alias = GetAliasForExpression(target_path);
      if (target_alias.empty()) {
        target_alias = kElementId;
      }
    }
    GOOGLESQL_RET_CHECK(!target_alias.empty());

    if (resolved_update_item.element_column() == nullptr) {
      resolved_update_item.set_element_column(
          MakeResolvedColumnHolder(ResolvedColumn(
              AllocateColumnId(), /*table_name=*/kArrayId,
              /*name=*/target_alias, target_type->AsArray()->element_type())));
    }

    // We create a target scope here for nested statements that contains only
    // the alias for the array element being updated.
    std::shared_ptr<NameList> nested_target_name_list(new NameList);
    GOOGLESQL_RETURN_IF_ERROR(nested_target_name_list->AddValueTableColumn(
        target_alias, resolved_update_item.element_column()->column(),
        target_path));

    // The array element alias is visible for DELETE and UPDATE, but not
    // for INSERT. The visibility of other names depends on
    // whether FEATURE_CORRELATED_REFS_IN_NESTED_DML is enabled.
    std::unique_ptr<NameScope> new_nested_dml_scope_owner;
    const NameScope* nested_dml_scope = nullptr;
    if (is_nested_insert) {
      if (language().LanguageFeatureEnabled(
              FEATURE_CORRELATED_REFS_IN_NESTED_DML)) {
        // Use the update scope for nested INSERT statements.
        nested_dml_scope = update_scope;
      } else {
        // Use an empty scope for nested INSERT statements.
        nested_dml_scope = empty_name_scope_.get();
      }
    } else {
      GOOGLESQL_RET_CHECK(is_nested_delete || is_nested_update);
      if (language().LanguageFeatureEnabled(
              FEATURE_CORRELATED_REFS_IN_NESTED_DML)) {
        // Construct the scope for nested DELETE and UPDATE statements by
        // stacking the target alias on top of 'update_scope'.
        new_nested_dml_scope_owner =
            std::make_unique<NameScope>(update_scope, nested_target_name_list);
      } else {
        // The scope for nested DELETE and UPDATE statements contains only the
        // target alias.
        new_nested_dml_scope_owner =
            std::make_unique<NameScope>(*nested_target_name_list);
      }
      nested_dml_scope = new_nested_dml_scope_owner.get();
    }

    if (is_nested_delete) {
      // Nested DML ordering is checked by ShouldMergeWithUpdateItem().
      GOOGLESQL_RET_CHECK_EQ(0, resolved_update_item.update_list_size());
      GOOGLESQL_RET_CHECK_EQ(0, resolved_update_item.insert_list_size());

      if (ast_input_update_item->delete_statement()->hint() != nullptr) {
        return MakeSqlErrorAt(ast_input_update_item->delete_statement()->hint())
               << "Nested DELETE statement does not support hints";
      }

      std::unique_ptr<ResolvedDeleteStmt> resolved_stmt;
      GOOGLESQL_RETURN_IF_ERROR(ResolveDeleteStatementImpl(
          ast_input_update_item->delete_statement(), target_alias,
          /*target_name_list=*/nullptr, nested_dml_scope,
          /*table_scan=*/nullptr, &resolved_stmt));
      resolved_update_item.add_delete_list(std::move(resolved_stmt));
    } else if (is_nested_update) {
      // Nested DML ordering is checked by ShouldMergeWithUpdateItem().
      GOOGLESQL_RET_CHECK_EQ(0, resolved_update_item.insert_list_size());

      // The target scope contains only the array element being updated.
      const NameScope nested_target_scope(*nested_target_name_list);

      if (ast_input_update_item->update_statement()->hint() != nullptr) {
        return MakeSqlErrorAt(ast_input_update_item->update_statement()->hint())
               << "Nested UPDATE statement does not support hints";
      }

      std::unique_ptr<ResolvedUpdateStmt> resolved_stmt;
      GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateStatementImpl(
          ast_input_update_item->update_statement(), /*is_nested=*/true,
          target_alias, &nested_target_scope, /*target_name_list=*/nullptr,
          nested_dml_scope, /*table_scan=*/nullptr, /*from_scan=*/nullptr,
          resolved_columns_from_table_scans_, &resolved_stmt));
      resolved_update_item.add_update_list(std::move(resolved_stmt));
    } else {
      GOOGLESQL_RET_CHECK(is_nested_insert);

      if (ast_input_update_item->insert_statement()->hint() != nullptr) {
        return MakeSqlErrorAt(ast_input_update_item->insert_statement()->hint())
               << "Nested INSERT statement does not support hints";
      }

      std::unique_ptr<ResolvedInsertStmt> resolved_stmt;
      ResolvedColumnList insert_columns;
      insert_columns.emplace_back(
          resolved_update_item.element_column()->column());
      GOOGLESQL_RETURN_IF_ERROR(ResolveInsertStatementImpl(
          ast_input_update_item->insert_statement(), target_alias,
          /*target_name_list=*/nullptr, /*table_scan=*/nullptr, insert_columns,
          nested_dml_scope,
          /*pipe_input_name_list=*/nullptr,
          /*pipe_input_scan=*/nullptr, resolved_columns_from_table_scans_,
          &resolved_stmt));
      resolved_update_item.add_insert_list(std::move(resolved_stmt));
    }
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveUpdateStatement(
    const ASTUpdateStatement* ast_statement,
    std::unique_ptr<ResolvedUpdateStmt>* output) {
  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* target_path,
                   ast_statement->GetTargetPathForNonNested());
  IdString target_alias;
  std::unique_ptr<const ResolvedTableScan> resolved_table_scan;
  std::shared_ptr<const NameList> target_name_list;
  ResolvedColumnToCatalogColumnHashMap
      out_resolved_columns_to_catalog_columns_for_target_scan;
  GOOGLESQL_RETURN_IF_ERROR(ResolveDMLTargetTable(
      target_path, ast_statement->alias(), ast_statement->hint(), &target_alias,
      &resolved_table_scan, &target_name_list,
      out_resolved_columns_to_catalog_columns_for_target_scan));

  // Insert the resolved columns from current scan to all scans map.
  resolved_columns_from_table_scans_.insert(
      out_resolved_columns_to_catalog_columns_for_target_scan.begin(),
      out_resolved_columns_to_catalog_columns_for_target_scan.end());

  if (ast_statement->offset() != nullptr) {
    return MakeSqlErrorAt(ast_statement->offset())
           << "Non-nested UPDATE statement does not support WITH OFFSET";
  }

  std::unique_ptr<const ResolvedScan> resolved_from_scan;
  std::shared_ptr<const NameList> from_name_list(new NameList);
  if (ast_statement->from_clause() != nullptr) {
    // If the UPDATE statement has a FROM clause then this is an UPDATE with
    // JOINs. Please see (broken link) for details about
    // syntax and semantics of such statements.
    if (!language().LanguageFeatureEnabled(FEATURE_DML_UPDATE_WITH_JOIN)) {
      return MakeSqlErrorAt(ast_statement) << "Update with joins not supported";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveTableExpression(
        ast_statement->from_clause()->table_expression(),
        empty_name_scope_.get(), empty_name_scope_.get(),
        /*is_leftmost=*/true, /*on_rhs_of_right_or_full_join=*/false,
        &resolved_from_scan, &from_name_list));
  }

  // With the exception of the target expression in the SET clause, the rest of
  // the update statement should see names from the target table and from the
  // FROM clause, so prepare a combined name list.
  std::unique_ptr<NameList> update_name_list = std::make_unique<NameList>();
  if (ast_statement->from_clause() != nullptr) {
    if (from_name_list->HasRangeVariable(target_alias)) {
      return MakeSqlErrorAt(ast_statement->from_clause())
             << "Alias " << ToIdentifierLiteral(target_alias)
             << " in the FROM clause was already defined as the UPDATE target";
    }
    GOOGLESQL_RETURN_IF_ERROR(update_name_list->MergeFrom(*from_name_list,
                                                ast_statement->from_clause()));
  }
  GOOGLESQL_RETURN_IF_ERROR(update_name_list->MergeFrom(*target_name_list, target_path));

  std::shared_ptr<const NameList> shared_update_name_list(
      update_name_list.release());

  const std::unique_ptr<const NameScope> target_scope(
      new NameScope(*target_name_list));
  const std::unique_ptr<const NameScope> update_scope(
      new NameScope(*shared_update_name_list));
  return ResolveUpdateStatementImpl(
      ast_statement, /*is_nested=*/false, target_alias, target_scope.get(),
      target_name_list, update_scope.get(), std::move(resolved_table_scan),
      std::move(resolved_from_scan),
      out_resolved_columns_to_catalog_columns_for_target_scan, output);
}

absl::Status Resolver::ResolveUpdateStatementImpl(
    const ASTUpdateStatement* ast_statement, bool is_nested,
    IdString target_alias, const NameScope* target_scope,
    const std::shared_ptr<const NameList>& target_name_list,
    const NameScope* update_scope,
    std::unique_ptr<const ResolvedTableScan> resolved_table_scan,
    std::unique_ptr<const ResolvedScan> resolved_from_scan,
    ResolvedColumnToCatalogColumnHashMap&
        resolved_columns_to_catalog_columns_for_target_scan,
    std::unique_ptr<ResolvedUpdateStmt>* output) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::unique_ptr<ResolvedColumnHolder> resolved_array_offset_column;
  std::unique_ptr<NameScope> new_update_scope_owner;
  if (ast_statement->offset() != nullptr) {
    if (!language().LanguageFeatureEnabled(
            FEATURE_NESTED_UPDATE_DELETE_WITH_OFFSET)) {
      return MakeSqlErrorAt(ast_statement->offset())
             << "UPDATE ... SET does not support WITH OFFSET";
    }

    const ASTWithOffset* offset = ast_statement->offset();
    const IdString offset_alias = offset->alias() != nullptr
                                      ? offset->alias()->GetAsIdString()
                                      : kOffsetAlias;
    if (offset_alias == target_alias) {
      const ASTNode* ast_location =
          offset->alias() != nullptr
              ? static_cast<const ASTNode*>(offset->alias())
              : offset;
      return MakeSqlErrorAt(ast_location)
             << "Duplicate OFFSET alias " << ToIdentifierLiteral(offset_alias)
             << " in nested UPDATE";
    }

    const ResolvedColumn offset_column(AllocateColumnId(),
                                       /*table_name=*/kArrayId, offset_alias,
                                       types::Int64Type());
    resolved_array_offset_column = MakeResolvedColumnHolder(offset_column);

    // Stack a scope on top of 'update_scope' to include the offset column.
    // Stacking a scope is not ideal because it makes the error messages worse
    // (no more "Did you mean ...?"), and we also have to perform the manual
    // check above to handle the case where 'offset_alias == target_alias'. A
    // possible alternative to stacking is to have this method create
    // 'update_scope' in the first place rather than passing it in and then
    // stacking something on top of it. But that does not seem worth the
    // complexity.
    std::shared_ptr<NameList> offset_column_list(new NameList);
    GOOGLESQL_RETURN_IF_ERROR(offset_column_list->AddColumn(
        offset_column.name_id(), offset_column, /*is_explicit=*/false));
    new_update_scope_owner =
        std::make_unique<NameScope>(update_scope, offset_column_list);
    update_scope = new_update_scope_owner.get();
  }

  if (ast_statement->where() == nullptr) {
    return MakeSqlErrorAt(ast_statement) << "UPDATE must have a WHERE clause";
  }
  std::unique_ptr<const ResolvedExpr> resolved_where_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_statement->where(), update_scope,
                                    "UPDATE scope", &resolved_where_expr));
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(ast_statement->where(), "WHERE clause",
                                   &resolved_where_expr));

  std::unique_ptr<const ResolvedAssertRowsModified>
      resolved_assert_rows_modified;
  if (ast_statement->assert_rows_modified() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveAssertRowsModified(
        ast_statement->assert_rows_modified(), &resolved_assert_rows_modified));
  }

  std::unique_ptr<const ResolvedReturningClause> resolved_returning_clause;
  if (ast_statement->returning() != nullptr) {
    if (!language().LanguageFeatureEnabled(FEATURE_DML_RETURNING)) {
      return MakeSqlErrorAt(ast_statement->returning())
             << "THEN RETURN is not supported";
    }
    if (is_nested) {
      GOOGLESQL_RET_CHECK_EQ(target_name_list, nullptr);
      return MakeSqlErrorAt(ast_statement->returning())
             << "THEN RETURN is not allowed in nested UPDATE statements";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveReturningClause(
        ast_statement->returning(), target_alias, target_name_list,
        target_scope, &resolved_returning_clause));
  }

  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  if (ast_statement->update_item_list() == nullptr) {
    return MakeSqlErrorAt(ast_statement)
           << "UPDATE must specify an update list";
  }
  GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateItemList(ast_statement->update_item_list(),
                                        is_nested, target_scope, update_scope,
                                        &update_item_list));

  std::vector<int> out_topologically_sorted_generated_column_ids;
  std::vector<std::unique_ptr<const ResolvedExpr>>
      out_generated_column_expr_list;
  if (resolved_table_scan != nullptr) {
    // Cases like nested DML on proto may have resolved table as NULL.
    GOOGLESQL_RETURN_IF_ERROR(ResolveGeneratedColumnsForDml(
        resolved_table_scan->table(), resolved_columns_from_table_scans_,
        &out_topologically_sorted_generated_column_ids,
        &out_generated_column_expr_list));
  }

  *output = MakeResolvedUpdateStmt(
      std::move(resolved_table_scan), std::move(resolved_assert_rows_modified),
      std::move(resolved_returning_clause),
      std::move(resolved_array_offset_column), std::move(resolved_where_expr),
      std::move(update_item_list), std::move(resolved_from_scan),
      out_topologically_sorted_generated_column_ids,
      std::move(out_generated_column_expr_list));
  MaybeRecordResolvedNodeOperatorKeywordLocation(ast_statement, output->get());
  return absl::OkStatus();
}

absl::Status Resolver::ResolveMergeStatement(
    const ASTMergeStatement* statement,
    std::unique_ptr<ResolvedMergeStmt>* output) {
  const ASTPathExpression* target_path = statement->target_path();
  IdString alias;
  std::unique_ptr<const ResolvedTableScan> resolved_target_table_scan;
  std::shared_ptr<const NameList> target_name_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveDMLTargetTable(
      target_path, statement->alias(),
      /*hint=*/nullptr, &alias, &resolved_target_table_scan, &target_name_list,
      resolved_columns_from_table_scans_));

  std::unique_ptr<const ResolvedScan> resolved_from_scan;
  std::shared_ptr<const NameList> source_name_list(new NameList);

  GOOGLESQL_RETURN_IF_ERROR(ResolveTableExpression(
      statement->table_expression(), empty_name_scope_.get(),
      empty_name_scope_.get(), /*is_leftmost=*/true,
      /*on_rhs_of_right_or_full_join=*/false, &resolved_from_scan,
      &source_name_list));
  if (source_name_list->HasRangeVariable(alias)) {
    return MakeSqlErrorAt(statement->table_expression())
           << "Alias " << ToIdentifierLiteral(alias) << " in the source "
           << "was already defined as the MERGE target";
  }

  // Prepares a combined name list from source and target. They may be used by
  // merge condition as well as WHEN clauses.
  //
  // If there is column name with conflicts between source and target, it will
  // be marked as ambiguous in all_name_list. When such ambiguous column is
  // referenced,
  //   1. If there is no explicit table alias, it will be reported as error.
  //   2. If there is explicit table alias, the alias will be used to look for
  //      range variable and the associated column list. Then, the expected
  //      column can be found within the column list.
  std::unique_ptr<NameList> all_name_list = std::make_unique<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(all_name_list->MergeFrom(*source_name_list,
                                           statement->table_expression()));
  GOOGLESQL_RETURN_IF_ERROR(all_name_list->MergeFrom(*target_name_list, target_path));

  const std::unique_ptr<const NameScope> all_scope(
      new NameScope(*all_name_list));
  const std::unique_ptr<const NameScope> target_scope(
      new NameScope(*target_name_list));
  const std::unique_ptr<const NameScope> source_scope(
      new NameScope(*source_name_list));

  std::unique_ptr<const ResolvedExpr> resolved_merge_condition_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(statement->merge_condition(),
                                    all_scope.get(), "merge condition",
                                    &resolved_merge_condition_expr));
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(statement->merge_condition(),
                                   "merge condition",
                                   &resolved_merge_condition_expr));

  IdStringHashMapCase<ResolvedColumn> target_table_columns;
  for (const ResolvedColumn& column :
       resolved_target_table_scan->column_list()) {
    googlesql_base::InsertIfNotPresent(&target_table_columns, column.name_id(), column);
  }

  std::vector<std::unique_ptr<const ResolvedMergeWhen>> when_clause_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveMergeWhenClauseList(
      statement->when_clauses(), &target_table_columns, target_scope.get(),
      source_scope.get(), all_scope.get(), target_name_list.get(),
      source_name_list.get(), &when_clause_list));
  *output = MakeResolvedMergeStmt(
      std::move(resolved_target_table_scan), std::move(resolved_from_scan),
      std::move(resolved_merge_condition_expr), std::move(when_clause_list));
  MaybeRecordResolvedNodeOperatorKeywordLocation(statement, output->get());

  return absl::OkStatus();
}

absl::Status Resolver::ResolveMergeWhenClauseList(
    const ASTMergeWhenClauseList* when_clause_list,
    const IdStringHashMapCase<ResolvedColumn>* target_table_columns,
    const NameScope* target_name_scope, const NameScope* source_name_scope,
    const NameScope* all_name_scope, const NameList* target_name_list,
    const NameList* source_name_list,
    std::vector<std::unique_ptr<const ResolvedMergeWhen>>*
        resolved_when_clauses) {
  GOOGLESQL_RET_CHECK(when_clause_list != nullptr);
  GOOGLESQL_RET_CHECK(target_table_columns != nullptr);
  GOOGLESQL_RET_CHECK(target_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(source_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(all_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(target_name_list != nullptr);
  GOOGLESQL_RET_CHECK(source_name_list != nullptr);
  GOOGLESQL_RET_CHECK(resolved_when_clauses != nullptr);

  resolved_when_clauses->clear();
  for (const ASTMergeWhenClause* when_clause :
       when_clause_list->clause_list()) {
    ResolvedMergeWhen::MatchType match_type;
    const NameScope* visible_name_scope = nullptr;
    switch (when_clause->match_type()) {
      case ASTMergeWhenClause::MATCHED:
        match_type = ResolvedMergeWhen::MATCHED;
        visible_name_scope = all_name_scope;
        break;
      case ASTMergeWhenClause::NOT_MATCHED_BY_SOURCE:
        match_type = ResolvedMergeWhen::NOT_MATCHED_BY_SOURCE;
        visible_name_scope = target_name_scope;
        break;
      case ASTMergeWhenClause::NOT_MATCHED_BY_TARGET:
        match_type = ResolvedMergeWhen::NOT_MATCHED_BY_TARGET;
        visible_name_scope = source_name_scope;
        break;
      case ASTMergeWhenClause::NOT_SET:
        GOOGLESQL_RET_CHECK_FAIL();
    }

    std::unique_ptr<const ResolvedExpr> resolved_match_condition_expr;
    if (when_clause->search_condition() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(when_clause->search_condition(),
                                        visible_name_scope, "match condition",
                                        &resolved_match_condition_expr));
      GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(when_clause->search_condition(),
                                       "match condition",
                                       &resolved_match_condition_expr));
    }

    const ASTMergeAction* action = when_clause->action();
    ResolvedMergeWhen::ActionType action_type;
    std::vector<std::unique_ptr<const ResolvedUpdateItem>>
        resolved_update_item_list;
    if (action->action_type() == ASTMergeAction::UPDATE) {
      if (match_type == ResolvedMergeWhen::NOT_MATCHED_BY_TARGET) {
        return MakeSqlErrorAt(action)
               << "UPDATE is not allowed for NOT MATCHED BY TARGET clause";
      }
      GOOGLESQL_RETURN_IF_ERROR(ResolveMergeUpdateAction(
          action->update_item_list(), target_name_scope, visible_name_scope,
          &resolved_update_item_list));
      action_type = ResolvedMergeWhen::UPDATE;
    }

    ResolvedColumnList resolved_insert_column_list;
    std::unique_ptr<const ResolvedInsertRow> resolved_insert_row;
    if (action->action_type() == ASTMergeAction::INSERT) {
      if (match_type != ResolvedMergeWhen::NOT_MATCHED_BY_TARGET) {
        return MakeSqlErrorAt(action)
               << "INSERT is only allowed for NOT MATCHED BY TARGET clause";
      }
      GOOGLESQL_RETURN_IF_ERROR(ResolveMergeInsertAction(
          action, target_table_columns, target_name_scope, visible_name_scope,
          target_name_list, source_name_list, &resolved_insert_column_list,
          &resolved_insert_row));
      action_type = ResolvedMergeWhen::INSERT;
    }

    if (action->action_type() == ASTMergeAction::DELETE) {
      if (match_type == ResolvedMergeWhen::NOT_MATCHED_BY_TARGET) {
        return MakeSqlErrorAt(action)
               << "DELETE is not allowed for NOT MATCHED BY TARGET clause";
      }
      action_type = ResolvedMergeWhen::DELETE;
    }

    resolved_when_clauses->push_back(MakeResolvedMergeWhen(
        match_type, std::move(resolved_match_condition_expr), action_type,
        resolved_insert_column_list, std::move(resolved_insert_row),
        std::move(resolved_update_item_list)));
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveMergeUpdateAction(
    const ASTUpdateItemList* update_item_list,
    const NameScope* target_name_scope, const NameScope* all_name_scope,
    std::vector<std::unique_ptr<const ResolvedUpdateItem>>*
        resolved_update_item_list) {
  GOOGLESQL_RET_CHECK(update_item_list != nullptr);
  GOOGLESQL_RET_CHECK(target_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(all_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(resolved_update_item_list != nullptr);

  resolved_update_item_list->clear();
  for (const auto& ast_update_item : update_item_list->update_items()) {
    if (ast_update_item->delete_statement() != nullptr ||
        ast_update_item->update_statement() != nullptr ||
        ast_update_item->insert_statement() != nullptr) {
      return MakeSqlErrorAt(ast_update_item)
             << "Merge does not support nested DML statements";
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateItemList(update_item_list, /*is_nested=*/false,
                                        target_name_scope, all_name_scope,
                                        resolved_update_item_list));

  return absl::OkStatus();
}

absl::Status Resolver::ResolveMergeInsertAction(
    const ASTMergeAction* merge_action,
    const IdStringHashMapCase<ResolvedColumn>* target_table_columns,
    const NameScope* target_name_scope, const NameScope* all_name_scope,
    const NameList* target_name_list, const NameList* source_name_list,
    ResolvedColumnList* resolved_insert_column_list,
    std::unique_ptr<const ResolvedInsertRow>* resolved_insert_row) {
  GOOGLESQL_RET_CHECK(merge_action != nullptr);
  GOOGLESQL_RET_CHECK(target_table_columns != nullptr);
  GOOGLESQL_RET_CHECK(target_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(all_name_scope != nullptr);
  GOOGLESQL_RET_CHECK(target_name_list != nullptr);
  GOOGLESQL_RET_CHECK(source_name_list != nullptr);
  GOOGLESQL_RET_CHECK(resolved_insert_column_list != nullptr);
  GOOGLESQL_RET_CHECK(resolved_insert_row != nullptr);

  const ASTColumnList* insert_column_list = merge_action->insert_column_list();
  const ASTInsertValuesRow* insert_row = merge_action->insert_row();
  GOOGLESQL_RET_CHECK(insert_row != nullptr);

  resolved_insert_column_list->clear();

  if (!language().LanguageFeatureEnabled(FEATURE_OMIT_INSERT_COLUMN_LIST)) {
    if (insert_column_list == nullptr) {
      return MakeSqlErrorAt(merge_action) << "Missing insert column list";
    }
    if (insert_row->values().empty()) {
      return MakeSqlErrorAt(merge_action) << "Missing insert value list";
    }
  }

  if (insert_column_list == nullptr) {
    // Insert column list is omitted. Implicitly expand it to all writable
    // normal columns of target table.
    for (const ResolvedColumn& column :
         target_name_list->GetResolvedColumns()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const bool is_writable, IsColumnWritable(column));
      if (is_writable) {
        resolved_insert_column_list->push_back(column);
      }
    }
    if (resolved_insert_column_list->empty()) {
      return MakeSqlErrorAt(merge_action)
             << "No writable column in target table";
    }
  } else {
    IdStringHashSetCase visited_column_names;
    for (const ASTIdentifier* column_name : insert_column_list->identifiers()) {
      const IdString& column_name_id = column_name->GetAsIdString();
      if (!googlesql_base::InsertIfNotPresent(&visited_column_names, column_name_id)) {
        return MakeSqlErrorAt(column_name)
               << "MERGE has INSERT operation with duplicated column names: "
               << column_name_id;
      }

      const auto target_column =
          googlesql_base::FindOrNull(*target_table_columns, column_name_id);
      if (target_column != nullptr) {
        resolved_insert_column_list->push_back(*target_column);
        GOOGLESQL_RETURN_IF_ERROR(VerifyTableScanColumnIsWritable(
            column_name, *target_column, "INSERT"));
      } else {
        return MakeSqlErrorAt(column_name)
               << "Column " << column_name_id
               << " is not present in target table ";
      }
    }
  }

  size_t insert_column_count = resolved_insert_column_list->size();
  size_t insert_value_count =
      insert_row->values().empty()
          ? static_cast<size_t>(source_name_list->num_columns())
          : insert_row->values().size();
  if (insert_column_count != insert_value_count) {
    return MakeSqlErrorAt(insert_row)
           << "Inserted row has wrong column count; Has " << insert_value_count
           << ", expected " << insert_column_count;
  }

  if (insert_row->values().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveInsertValuesRow(
        insert_row, source_name_list->GetResolvedColumns(),
        *resolved_insert_column_list, resolved_insert_row));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(ResolveInsertValuesRow(insert_row, all_name_scope,
                                           *resolved_insert_column_list,
                                           resolved_insert_row));
  }
  // Avoids pruning the insert columns that we are inserting into.
  RecordColumnAccess(*resolved_insert_column_list, ResolvedStatement::WRITE);

  return absl::OkStatus();
}

absl::Status Resolver::ResolveAssertRowsModified(
    const ASTAssertRowsModified* ast_node,
    std::unique_ptr<const ResolvedAssertRowsModified>* output) {
  GOOGLESQL_RET_CHECK(ast_node != nullptr);
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_node->num_rows(),
                                    empty_name_scope_.get(),
                                    "assert_rows_modified", &resolved_expr));
  ABSL_DCHECK(resolved_expr != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
      "ASSERT_ROWS_MODIFIED" /* clause_name */, ast_node->num_rows(),
      &resolved_expr));
  *output = MakeResolvedAssertRowsModified(std::move(resolved_expr));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveReturningClause(
    const ASTReturningClause* ast_node, IdString target_alias,
    const std::shared_ptr<const NameList>& from_clause_name_list,
    const NameScope* from_scan_scope,
    std::unique_ptr<const ResolvedReturningClause>* output) {
  GOOGLESQL_RET_CHECK_NE(ast_node, nullptr);

  const ASTSelectList* select_list = ast_node->select_list();
  GOOGLESQL_RET_CHECK_NE(select_list, nullptr);

  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
  query_resolution_info->set_is_resolving_returning_clause();

  std::unique_ptr<NameScope> unused_scope;
  std::shared_ptr<NameList> unused_name_list;

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectListExprsFirstPass(
      select_list, from_scan_scope, from_clause_name_list,
      query_resolution_info.get(), target_alias, unused_scope,
      unused_name_list));

  if (query_resolution_info->HasGroupByOrAggregation()) {
    return MakeSqlErrorAt(ast_node)
           << "THEN RETURN clause cannot use aggregation";
  }

  if (query_resolution_info->HasAnalytic()) {
    return MakeSqlErrorAt(ast_node)
           << "THEN RETURN clause cannot use analytic functions";
  }

  query_resolution_info->set_has_group_by(false);
  query_resolution_info->set_has_having(false);
  query_resolution_info->set_has_order_by(false);

  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();
  GOOGLESQL_RET_CHECK_NE(select_column_state_list, nullptr);

  GOOGLESQL_RETURN_IF_ERROR(FinalizeSelectColumnStateList(
      select_list, target_alias,
      /*force_new_columns_for_projected_outputs=*/false,
      query_resolution_info.get(), select_column_state_list));

  // build up the output name list
  auto output_name_list = std::make_shared<NameList>();

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectListExprsSecondPass(
      target_alias, from_scan_scope, &output_name_list,
      query_resolution_info.get()));

  // Appends the WITH ACTION AS alias clause to the output column list
  std::unique_ptr<ResolvedColumnHolder> action_column;
  if (ast_node->action_alias() != nullptr) {
    IdString action_alias = ast_node->action_alias()->GetAsIdString();
    GOOGLESQL_RET_CHECK(!action_alias.empty());

    const ResolvedColumn column(AllocateColumnId(),
                                /*table_name=*/kWithActionId,
                                /*name=*/action_alias,
                                type_factory_->get_string());

    action_column = MakeResolvedColumnHolder(column);
    GOOGLESQL_RETURN_IF_ERROR(output_name_list->AddValueTableColumn(
        action_alias, action_column->column(), ast_node->action_alias()));
  }

  std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;
  for (const NamedColumn& named_column : output_name_list->columns()) {
    output_column_list.push_back(MakeResolvedOutputColumn(
        named_column.name().ToString(), named_column.column()));
  }

  std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns =
      query_resolution_info->release_select_list_columns_to_compute();

  GOOGLESQL_RET_CHECK_EQ(select_column_state_list->Size() +
                   (ast_node->action_alias() == nullptr ? 0 : 1),
               output_name_list->num_columns());

  // All columns to compute have been consumed.
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->CheckComputedColumnListsAreEmpty());

  *output = MakeResolvedReturningClause(std::move(output_column_list),
                                        std::move(action_column),
                                        std::move(computed_columns));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveGeneratedColumnsForDml(
    const Table* target_table,
    const ResolvedColumnToCatalogColumnHashMap&
        resolved_columns_to_catalog_columns_for_target_scan,
    std::vector<int>* out_topologically_sorted_generated_column_ids,
    std::vector<std::unique_ptr<const ResolvedExpr>>*
        out_generated_column_expr_list) {
  GOOGLESQL_RET_CHECK_NE(target_table, nullptr);
  GOOGLESQL_RET_CHECK(out_generated_column_expr_list != nullptr);
  GOOGLESQL_RET_CHECK(out_topologically_sorted_generated_column_ids != nullptr);

  // Check cycles and topologically sort generated columns.
  std::vector<int> generated_column_indices;
  absl::node_hash_map<const Column*, ResolvedColumn>
      catalog_columns_to_resolved_columns_map;
  for (auto const& [resolved_column, catalog_column] :
       resolved_columns_to_catalog_columns_for_target_scan) {
    auto itr = catalog_columns_to_resolved_columns_map.find(catalog_column);
    if (itr == catalog_columns_to_resolved_columns_map.end()) {
      const auto [_, success] = catalog_columns_to_resolved_columns_map.insert(
          {catalog_column, resolved_column});
      GOOGLESQL_RET_CHECK(success);
    }
    if (catalog_column->HasGeneratedExpression() &&
        resolved_column.table_name() == target_table->Name()) {
      int column_index = -1;
      bool duplicate = false;
      FindColumnIndex(target_table, catalog_column->Name(), &column_index,
                      &duplicate);
      GOOGLESQL_RET_CHECK_GE(column_index, 0);
      GOOGLESQL_RET_CHECK(!duplicate);
      generated_column_indices.push_back(column_index);
    }
  }

  CycleDetector* cycle_detector =
      analyzer_options_.find_options().cycle_detector();
  GOOGLESQL_RET_CHECK(cycle_detector != nullptr)
      << "Cycle detector needs to be set in AnalyzerOptions when analyzing "
         "DML statements";

  std::vector<GeneratedColumnIndexAndResolvedId>
      topologically_sorted_generated_columns;
  GOOGLESQL_RETURN_IF_ERROR(TopologicallySortGeneratedColumns(
      generated_column_indices, target_table, cycle_detector,
      catalog_columns_to_resolved_columns_map,
      topologically_sorted_generated_columns));
  out_topologically_sorted_generated_column_ids->reserve(
      topologically_sorted_generated_columns.size());
  out_generated_column_expr_list->reserve(
      topologically_sorted_generated_columns.size());
  GOOGLESQL_RETURN_IF_ERROR(RewriteGeneratedColumnExpressions(
      topologically_sorted_generated_columns, target_table,
      catalog_columns_to_resolved_columns_map, *out_generated_column_expr_list,
      *out_topologically_sorted_generated_column_ids,
      [this](const ResolvedColumn& c) { return MakeColumnRef(c); }));
  GOOGLESQL_RET_CHECK_EQ(out_generated_column_expr_list->size(),
               out_topologically_sorted_generated_column_ids->size());

  return absl::OkStatus();
}
}  // namespace googlesql
