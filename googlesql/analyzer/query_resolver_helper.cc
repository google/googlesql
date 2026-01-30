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

#include "googlesql/analyzer/query_resolver_helper.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "googlesql/analyzer/analytic_function_resolver.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parse_tree_visitor.h"
#include "googlesql/parser/visit_result.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Converts a list of ResolvedComputedColumn to a list ResolvedColumnRef.
std::vector<std::unique_ptr<const ResolvedColumnRef>> MakeResolvedColumnRefs(
    absl::Span<const ResolvedComputedColumn* const> column_list) {
  absl::flat_hash_set<ResolvedColumn> distinct_column_set;
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_ref_list;
  for (const ResolvedComputedColumn* computed_column : column_list) {
    std::unique_ptr<ResolvedColumnRef> column_ref = MakeResolvedColumnRef(
        computed_column->column().type(), computed_column->column(),
        /*is_correlated=*/false);
    // Don't duplicate columns in a the grouping set.
    if (googlesql_base::InsertIfNotPresent(&distinct_column_set, column_ref->column())) {
      column_ref_list.push_back(std::move(column_ref));
    }
  }
  return column_ref_list;
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>>
CreateGroupingSetList(const GroupingSetInfoList& grouping_set_info_list) {
  std::vector<std::unique_ptr<const ResolvedGroupingSetBase>> grouping_set_list;
  for (const GroupingSetInfo& grouping_set : grouping_set_info_list) {
    if (grouping_set.kind == GroupingSetKind::kGroupingSet) {
      std::vector<const ResolvedComputedColumn*> grouping_set_columns;
      for (const ResolvedComputedColumnList& column_list :
           grouping_set.grouping_set_item_list) {
        GOOGLESQL_RET_CHECK_LE(column_list.size(), 1)
            << "There should be at most one column in the column_list for a "
               "grouping set";
        // An empty list means an empty grouping set.
        if (!column_list.empty()) {
          grouping_set_columns.push_back(column_list.front());
        }
      }
      grouping_set_list.push_back(MakeResolvedGroupingSet(
          MakeResolvedColumnRefs(absl::MakeSpan(grouping_set_columns))));
    } else {
      std::vector<std::unique_ptr<const ResolvedGroupingSetMultiColumn>>
          multi_columns;
      for (const ResolvedComputedColumnList& column_list :
           grouping_set.grouping_set_item_list) {
        std::vector<std::unique_ptr<const ResolvedColumnRef>> column_ref_list =
            MakeResolvedColumnRefs(absl::MakeSpan(column_list));
        GOOGLESQL_RET_CHECK_GT(column_ref_list.size(), 0)
            << "At least one column in the rollup or cube's column list";
        multi_columns.push_back(
            MakeResolvedGroupingSetMultiColumn(std::move(column_ref_list)));
      }
      GOOGLESQL_RET_CHECK_GT(multi_columns.size(), 0)
          << "rollup or cube column list can not be empty";
      if (grouping_set.kind == GroupingSetKind::kRollup) {
        grouping_set_list.push_back(
            MakeResolvedRollup(std::move(multi_columns)));
      } else {
        grouping_set_list.push_back(MakeResolvedCube(std::move(multi_columns)));
      }
    }
  }

  return grouping_set_list;
}

// Releases rollup list to rollup_column_list and grouping_set_list, this is the
// legacy way to represent rollup list in ResolvedAggregateScan. The method will
// called only when FEATURE_GROUPING_SETS isn't enabled, and it will be
// deprecated soon using the new representation of rollup, see more in
// (broken link).
absl::Status ReleaseLegacyRollupColumnList(
    std::vector<GroupingSetInfo>& grouping_set_info_list,
    std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>*
        grouping_set_list,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>* rollup_column_list) {
  // Extract the rollup column list from the grouping_set_info_list.
  std::vector<const ResolvedComputedColumn*> rollup_columns;
  for (const GroupingSetInfo& grouping_set : grouping_set_info_list) {
    GOOGLESQL_RET_CHECK(grouping_set.kind == GroupingSetKind::kRollup);
    for (const ResolvedComputedColumnList& column_list :
         grouping_set.grouping_set_item_list) {
      GOOGLESQL_RET_CHECK_EQ(column_list.size(), 1)
          << "There should be exactly one column in the column_list";
      rollup_columns.push_back(column_list.front());
    }
  }

  if (rollup_columns.empty()) {
    return absl::InvalidArgumentError("rollup column list is empty");
  }
  // group_by_info_.rollup_column_list stores the rollup columns. Grouping
  // sets are computed from the prefixes of
  // group_by_info_.rollup_column_list, and references to the same column
  // are deduplicated within a particular grouping set.
  //
  // For example, suppose that group_by_info_.rollup_column_list contains
  // columns b, a, c, a. This function will return:
  //
  // rollup_list: b, a, c, a.
  // grouping_set_list: {b, a, c}, {b, a, c}, {b, a}, {b}, {}.

  // Unowned pointers.
  std::vector<const ResolvedColumnRef*> current_grouping_set;
  std::set<ResolvedColumn> distinct_rollup_columns;

  // Add the empty grouping set.
  grouping_set_list->push_back(MakeResolvedGroupingSet());

  for (const ResolvedComputedColumn* rollup_column : rollup_columns) {
    auto rollup_column_ref = MakeResolvedColumnRef(
        rollup_column->column().type(), rollup_column->column(),
        /*is_correlated=*/false);
    // Don't duplicate columns in the grouping sets.
    if (googlesql_base::InsertIfNotPresent(&distinct_rollup_columns,
                                rollup_column_ref->column())) {
      current_grouping_set.push_back(rollup_column_ref.get());
    }
    rollup_column_list->push_back(std::move(rollup_column_ref));
    std::vector<std::unique_ptr<const ResolvedColumnRef>> grouping_set_columns;
    grouping_set_columns.reserve(current_grouping_set.size());
    for (const ResolvedColumnRef* grouping_column : current_grouping_set) {
      grouping_set_columns.push_back(MakeResolvedColumnRef(
          grouping_column->column().type(), grouping_column->column(),
          /*is_correlated=*/false));
    }
    grouping_set_list->push_back(
        MakeResolvedGroupingSet(std::move(grouping_set_columns)));
  }
  // Order of the rows resulting from ROLLUP are not guaranteed, but engines
  // will generally want to compute aggregates from more to less granular
  // levels of subtotals, e.g. (a, b, c), (a, b), (a), and then ().
  std::reverse(grouping_set_list->begin(), grouping_set_list->end());

  grouping_set_info_list.clear();
  return absl::OkStatus();
}

// Traverse the parser AST for an `ASTSelectColumn` and look for evidence that
// the column should have it's first-pass resolution deferred until after the
// GROUP BY clause is resolved. A column should have it's first-pass resolution
// deferred if it uses `GROUP ROWS` or a `GROUP BY` aggregate function modifier
// outside of a subquery. See comments in `ResolveSelectListExprsFirstPass` for
// more details.
//
// For instance, the following expression should have it's resolution deferred:
//    `1 + SUM(... GROUP BY...) + SUM(X) WITH GROUP ROWS (...)`
// But the following expression should not:
//    `(SELECT 1 + SUM(... GROUP BY...) + SUM(X) WITH GROUP ROWS (...) FROM...)`
class DeferredResolutionFinder : public NonRecursiveParseTreeVisitor {
 public:
  DeferredResolutionFinder() = default;
  DeferredResolutionFinder(const DeferredResolutionFinder&) = delete;
  DeferredResolutionFinder(DeferredResolutionFinder&&) = delete;
  DeferredResolutionFinder& operator=(const DeferredResolutionFinder&) = delete;
  DeferredResolutionFinder& operator=(DeferredResolutionFinder&&) = delete;

  absl::StatusOr<VisitResult> defaultVisit(const ASTNode* node) override {
    return VisitResult::VisitChildren(node);
  }

  absl::StatusOr<VisitResult> visitASTQuery(const ASTQuery* node) override {
    return VisitResult::Empty();
  }

  absl::StatusOr<VisitResult> visitASTFunctionCall(
      const ASTFunctionCall* node) override {
    if (node->group_by() != nullptr) {
      info_.has_outer_group_rows_or_group_by_modifiers = true;
    }
    return VisitResult::VisitChildren(node);
  };

  absl::StatusOr<VisitResult> visitASTAnalyticFunctionCall(
      const ASTAnalyticFunctionCall* node) override {
    info_.has_outer_analytic_function = true;
    return VisitResult::VisitChildren(node);
  };

  DeferredResolutionSelectColumnInfo GetDeferredResolutionSelectColumnInfo()
      const {
    return info_;
  }

 private:
  DeferredResolutionSelectColumnInfo info_;
};

}  // namespace

void QueryGroupByAndAggregateInfo::Reset() {
  has_group_by = false;
  has_aggregation = false;
  is_group_by_all = false;
  aggregate_expr_map.clear();
  group_by_column_state_list.clear();
  group_by_expr_map.clear();
  grouping_call_list.clear();
  grouping_output_columns.clear();
  grouping_set_product_inputs.clear();
  aggregate_columns_to_compute.clear();
  match_recognize_aggregate_columns_to_compute.clear();
  group_by_valid_field_info_map.Clear();
  is_post_distinct = false;
}

const Type* SelectColumnState::GetType() const {
  if (resolved_select_column.IsInitialized()) {
    return resolved_select_column.type();
  }
  if (resolved_expr != nullptr) {
    return resolved_expr->type();
  }
  return nullptr;
}

std::string SelectColumnState::DebugString(absl::string_view indent) const {
  std::string debug_string;
  absl::StrAppend(&debug_string, indent, "expr:\n   ", ast_expr->DebugString(),
                  "\n");
  absl::StrAppend(&debug_string, indent, "alias: ", alias.ToStringView(), "\n");
  absl::StrAppend(&debug_string, indent, "is_explicit: ", is_explicit, "\n");
  absl::StrAppend(&debug_string, indent,
                  "select_list_position: ", select_list_position, "\n");
  absl::StrAppend(
      &debug_string, indent, "resolved_expr:\n  ",
      (resolved_expr != nullptr ? resolved_expr->DebugString() : "<null>"),
      "\n");
  absl::StrAppend(&debug_string, indent, "resolved_computed_column:\n  ",
                  (resolved_computed_column != nullptr
                       ? resolved_computed_column->DebugString()
                       : "<null>"),
                  "\n");
  absl::StrAppend(&debug_string, indent,
                  "has_aggregation: ", expr_findings.has_aggregation, "\n");
  absl::StrAppend(&debug_string, indent,
                  "has_analytic: ", expr_findings.has_analytic, "\n");
  absl::StrAppend(&debug_string, indent,
                  "is_group_by_column: ", is_group_by_column, "\n");
  absl::StrAppend(&debug_string, indent, "resolved_select_column: ",
                  (resolved_select_column.IsInitialized()
                       ? resolved_select_column.DebugString()
                       : "<uninitialized>"),
                  "\n");
  absl::StrAppend(&debug_string, indent,
                  "resolved_pre_group_by_select_column: ",
                  (resolved_pre_group_by_select_column.IsInitialized()
                       ? resolved_pre_group_by_select_column.DebugString()
                       : "<uninitialized>"));
  return debug_string;
}

void LateralReferenceState::MarkAsReferencedLaterally() {
  is_referenced_laterally_ = true;
  if (dot_star_source_expr_info_ != nullptr) {
    dot_star_source_expr_info_->MarkAsReferencedLaterally();
  }
}

void LateralReferenceState::PinToPreGroupingContext() {
  is_pinned_to_pre_grouping_context_ = true;
  for (SelectColumnState* select_col : referenced_select_columns_) {
    select_col->lateral_reference_state->PinToPreGroupingContext();
  }
  if (dot_star_source_expr_info_ != nullptr) {
    dot_star_source_expr_info_->PinToPreGroupingContext();
  }
}

void SelectColumnStateList::AddSelectColumn(
    const ASTSelectColumn* ast_select_column, IdString alias, bool is_explicit,
    ExprFindings expr_findings,
    std::unique_ptr<const ResolvedExpr> resolved_expr,
    DotStarSourceExprInfo* dot_star_source_expr_info) {
  AddSelectColumn(std::make_unique<SelectColumnState>(
      ast_select_column, alias, is_explicit, expr_findings,
      std::move(resolved_expr),
      /*columns_referenced_laterally=*/std::vector<SelectColumnState*>(),
      dot_star_source_expr_info));
}

absl::Status SelectColumnStateList::ReplaceSelectColumn(
    int index, std::unique_ptr<SelectColumnState> new_select_column_state) {
  GOOGLESQL_RET_CHECK_LT(index, select_column_state_list_.size());
  select_column_state_list_[index] = std::move(new_select_column_state);
  return absl::OkStatus();
}

void SelectColumnStateList::AddSelectColumn(
    std::unique_ptr<SelectColumnState> select_column_state) {
  ABSL_DCHECK_EQ(select_column_state->select_list_position, -1);
  select_column_state->select_list_position =
      static_cast<int>(select_column_state_list_.size());
  // Save a mapping from the alias to this SelectColumnState. The mapping is
  // later used for validations performed by
  // FindAndValidateSelectColumnStateByAlias().
  const IdString alias = select_column_state->alias;
  if (!IsInternalAlias(alias)) {
    if (!googlesql_base::InsertIfNotPresent(&column_alias_to_state_list_position_, alias,
                                 select_column_state->select_list_position)) {
      // Now ambiguous.
      column_alias_to_state_list_position_[alias] = -1;
    }
  }
  select_column_state_list_.emplace_back(std::move(select_column_state));
}

absl::Status SelectColumnStateList::FindAndValidateSelectColumnStateByAlias(
    const char* clause_name, const ASTNode* ast_location, IdString alias,
    const ExprResolutionInfo* expr_resolution_info,
    const SelectColumnState** select_column_state) const {
  *select_column_state = nullptr;
  // TODO Should probably do this more generally with name scoping.
  const int* state_list_position =
      googlesql_base::FindOrNull(column_alias_to_state_list_position_, alias);
  if (state_list_position != nullptr) {
    if (*state_list_position == -1) {
      return MakeSqlErrorAt(ast_location)
             << "Name " << alias << " in " << clause_name
             << " is ambiguous; it may refer to multiple columns in the"
                " SELECT-list";
    } else {
      const SelectColumnState* found_select_column_state =
          select_column_state_list_[*state_list_position].get();
      GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateAndAnalyticSupport(
          alias.ToStringView(), ast_location, found_select_column_state,
          expr_resolution_info));
      *select_column_state = found_select_column_state;
    }
  }
  return absl::OkStatus();
}

absl::Status SelectColumnStateList::FindAndValidateSelectColumnStateByOrdinal(
    absl::string_view expr_description, const ASTNode* ast_location,
    const int64_t ordinal, const ExprResolutionInfo* expr_resolution_info,
    const SelectColumnState** select_column_state) const {
  *select_column_state = nullptr;
  if (ordinal < 1 || ordinal > select_column_state_list_.size()) {
    return MakeSqlErrorAt(ast_location)
           << expr_description
           << " is out of SELECT column number range: " << ordinal;
  }
  const SelectColumnState* found_select_column_state =
      select_column_state_list_[ordinal - 1].get();  // Convert to 0-based.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateAndAnalyticSupport(
      absl::StrCat(ordinal), ast_location, found_select_column_state,
      expr_resolution_info));
  *select_column_state = found_select_column_state;
  return absl::OkStatus();
}

absl::Status SelectColumnStateList::ValidateAggregateAndAnalyticSupport(
    const absl::string_view column_description, const ASTNode* ast_location,
    const SelectColumnState* select_column_state,
    const ExprResolutionInfo* expr_resolution_info) {
  // If `contains_outer_group_rows_or_group_by_modifiers` is true, then
  // `has_aggregation` should also be true. This condition is just to help
  // provide a better error message if a user writes something like
  // 'SCALAR_FUNCTION(...) WITH GROUP ROWS (...)'.
  if (select_column_state->contains_outer_group_rows_or_group_by_modifiers &&
      !expr_resolution_info->allows_aggregation) {
    GOOGLESQL_RET_CHECK(select_column_state->expr_findings.has_aggregation);
    return MakeSqlErrorAt(ast_location)
           << "Column " << column_description
           << " contains a GROUP ROWS subquery or a GROUP BY modifier, which "
              "is not allowed in "
           << expr_resolution_info->clause_name
           << (expr_resolution_info->is_post_distinct()
                   ? " after SELECT DISTINCT"
                   : "");
  }
  if (select_column_state->expr_findings.has_aggregation &&
      !expr_resolution_info->allows_aggregation) {
    return MakeSqlErrorAt(ast_location)
           << "Column " << column_description
           << " contains an aggregation function, which is not allowed in "
           << expr_resolution_info->clause_name
           << (expr_resolution_info->is_post_distinct()
                   ? " after SELECT DISTINCT"
                   : "");
  }
  if (select_column_state->expr_findings.has_analytic &&
      !expr_resolution_info->allows_analytic) {
    return MakeSqlErrorAt(ast_location)
           << "Column " << column_description
           << " contains an analytic function, which is not allowed in "
           << expr_resolution_info->clause_name
           << (expr_resolution_info->is_post_distinct()
                   ? " after SELECT DISTINCT"
                   : "");
  }
  return absl::OkStatus();
}

SelectColumnState* SelectColumnStateList::GetSelectColumnState(
    int select_list_position) {
  ABSL_CHECK_GE(select_list_position, 0);
  ABSL_CHECK_LT(select_list_position, select_column_state_list_.size());
  return select_column_state_list_[select_list_position].get();
}

const SelectColumnState* SelectColumnStateList::GetSelectColumnState(
    int select_list_position) const {
  ABSL_CHECK_GE(select_list_position, 0);
  ABSL_CHECK_LT(select_list_position, select_column_state_list_.size());
  return select_column_state_list_[select_list_position].get();
}

const std::vector<std::unique_ptr<SelectColumnState>>&
SelectColumnStateList::select_column_state_list() const {
  return select_column_state_list_;
}

size_t SelectColumnStateList::Size() const {
  return select_column_state_list_.size();
}

std::string SelectColumnStateList::DebugString() const {
  std::string debug_string("SelectColumnStateList, size = ");
  absl::StrAppend(&debug_string, Size(), "\n");
  for (int idx = 0; idx < Size(); ++idx) {
    absl::StrAppend(&debug_string, "    [", idx, "]:\n",
                    GetSelectColumnState(idx)->DebugString("       "), "\n");
  }
  absl::StrAppend(&debug_string, "  alias map:\n");
  for (const auto& alias_to_position : column_alias_to_state_list_position_) {
    absl::StrAppend(&debug_string, "    ",
                    alias_to_position.first.ToStringView(), " : ",
                    alias_to_position.second, "\n");
  }
  return debug_string;
}

QueryResolutionInfo::QueryResolutionInfo(Resolver* resolver,
                                         const QueryResolutionInfo* parent) {
  select_column_state_list_ = std::make_unique<SelectColumnStateList>();
  analytic_resolver_ = std::make_unique<AnalyticFunctionResolver>(resolver);
  if (parent != nullptr) {
    scoped_aggregation_state_ = parent->scoped_aggregation_state_;
    is_nested_aggregation_ = true;
  }
}

// Keep destructor impl in .cc to resolve circular deps.
QueryResolutionInfo::~QueryResolutionInfo() = default;

const ResolvedComputedColumn*
QueryResolutionInfo::AddGroupByComputedColumnIfNeeded(
    const ResolvedColumn& column, std::unique_ptr<const ResolvedExpr> expr,
    const ResolvedExpr* pre_group_by_expr, bool override_existing_column) {
  group_by_info_.has_group_by = true;
  const ResolvedComputedColumn*& stored_column =
      group_by_info_.group_by_expr_map[expr.get()];
  if (stored_column != nullptr && !override_existing_column) {
    return stored_column;
  }
  auto new_column = MakeResolvedComputedColumn(column, std::move(expr));
  stored_column = new_column.get();
  group_by_info_.group_by_column_state_list.emplace_back(std::move(new_column),
                                                         pre_group_by_expr);
  return stored_column;
}

const ResolvedComputedColumn*
QueryResolutionInfo::GetEquivalentGroupByComputedColumnOrNull(
    const ResolvedExpr* expr) const {
  return googlesql_base::FindPtrOrNull(group_by_info_.group_by_expr_map, expr);
}

void QueryResolutionInfo::AddGroupingSetList(
    GroupingSetInfoList&& grouping_set_list) {
  group_by_info_.grouping_set_product_inputs.push_back(
      std::move(grouping_set_list));
}

void QueryResolutionInfo::AddGroupingColumn(
    std::unique_ptr<const ResolvedGroupingCall> column) {
  group_by_info_.grouping_call_list.push_back(std::move(column));
}

// Add the grouping column to the expr map, but since at this point it's an
// AggregateFunctionCall, don't add it to the grouping_call_list yet. That will
// happen during the second pass of the selectList, where the
// correct group_by column reference is resolved.
absl::Status QueryResolutionInfo::AddGroupingColumnToExprMap(
    const ASTFunctionCall* ast_function_call,
    std::unique_ptr<const ResolvedComputedColumn> grouping_output_col) {
  group_by_info_.has_aggregation = true;
  GOOGLESQL_RET_CHECK(ast_function_call != nullptr);
  googlesql_base::InsertIfNotPresent(&group_by_info_.aggregate_expr_map, ast_function_call,
                          grouping_output_col.get());
  group_by_info_.grouping_output_columns.push_back(
      std::move(grouping_output_col));
  return absl::OkStatus();
}

absl::Status QueryResolutionInfo::ReleaseGroupingSetsAndRollupList(
    std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>*
        grouping_set_list,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>* rollup_column_list,
    const LanguageOptions& language_options) {
  if (group_by_info_.grouping_set_product_inputs.empty()) {
    return absl::OkStatus();
  }
  // Release the rollup column list to the legacy resolved ast representation
  // when grouping sets feature isn't enabled.
  if (!language_options.LanguageFeatureEnabled(FEATURE_GROUPING_SETS)) {
    GOOGLESQL_RET_CHECK(group_by_info_.grouping_set_product_inputs.size() == 1)
        << "When FEATURE_GROUPING_SETS is disabled, the input list should only "
           "have a single ROLLUP column list.";
    auto status = ReleaseLegacyRollupColumnList(
        group_by_info_.grouping_set_product_inputs[0], grouping_set_list,
        rollup_column_list);
    group_by_info_.grouping_set_product_inputs.clear();
    return status;
  }

  if (group_by_info_.grouping_set_product_inputs.size() == 1) {
    // Non-multi grouping sets case.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<const ResolvedGroupingSetBase>> tmp_list,
        CreateGroupingSetList(group_by_info_.grouping_set_product_inputs[0]));
    absl::c_move(tmp_list, std::back_inserter(*grouping_set_list));
  } else {
    GOOGLESQL_RET_CHECK(
        language_options.LanguageFeatureEnabled(FEATURE_MULTI_GROUPING_SETS));
    // Calculates the cross product of the grouping sets list.
    std::vector<std::unique_ptr<const ResolvedGroupingSetBase>> input_list;
    for (const auto& grouping_sets :
         group_by_info_.grouping_set_product_inputs) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<std::unique_ptr<const ResolvedGroupingSetBase>> tmp_list,
          CreateGroupingSetList(grouping_sets));
      GOOGLESQL_RET_CHECK(!tmp_list.empty());
      if (tmp_list.size() == 1) {
        input_list.push_back(std::move(tmp_list[0]));
      } else {
        input_list.push_back(MakeResolvedGroupingSetList(std::move(tmp_list)));
      }
    }
    grouping_set_list->push_back(
        MakeResolvedGroupingSetProduct(std::move(input_list)));
  }
  group_by_info_.grouping_set_product_inputs.clear();
  return absl::OkStatus();
}

absl::Status QueryResolutionInfo::PinToRowRange(
    std::optional<IdString> pattern_variable) {
  GOOGLESQL_RET_CHECK(
      !scoped_aggregation_state_->target_pattern_variable_ref.has_value());

  scoped_aggregation_state_->row_range_determined = true;
  scoped_aggregation_state_->target_pattern_variable_ref = pattern_variable;

  // Move any pending aggregate columns to the appropriate list.
  // The unscoped list may be non-empty if we have intermediate aggregations
  // that are not referencing any input columns. For example, the min()
  // aggregation in order by in:
  //      agg(...ORDER BY min(correlated_col + @p)
  //
  // Note that the destination variable's list may itself also be non-empty,
  // if there were previous aggregations that were completely resolved, e.g.
  // MEASURES max(a.x) + min(y) + .. /*here, the first 2 aggs are already added
  // to the appropriate lists.

  // Use the empty IdString to indicate the full range as a key in the map.
  IdString target_range_name = pattern_variable.value_or(IdString());
  for (auto& agg : release_unscoped_aggregate_columns_to_compute()) {
    group_by_info_
        .match_recognize_aggregate_columns_to_compute[target_range_name]
        .push_back(std::move(agg));
  }
  if (scoped_aggregate_columns_to_compute().empty() &&
      !pattern_variable.has_value()) {
    // If we have no aggregations at all and we're pinning to the full range,
    // ensure an empty list exists for the full range, in case it's accessed.
    // Calling "reserve(0)" to avoid a reassignment.
    ABSL_DCHECK(target_range_name.empty());
    group_by_info_
        .match_recognize_aggregate_columns_to_compute[target_range_name]
        .reserve(0);
  }
  return absl::OkStatus();
}

void QueryResolutionInfo::AddAggregateComputedColumn(
    const ASTFunctionCall* ast_function_call,
    std::unique_ptr<const ResolvedComputedColumnBase> column) {
  group_by_info_.has_aggregation = true;
  if (ast_function_call != nullptr) {
    googlesql_base::InsertIfNotPresent(&group_by_info_.aggregate_expr_map,
                            ast_function_call, column.get());
  }
  if (scoped_aggregation_state_->row_range_determined) {
    IdString target_range =
        scoped_aggregation_state_->target_pattern_variable_ref.value_or(
            IdString());
    group_by_info_.match_recognize_aggregate_columns_to_compute[target_range]
        .push_back(std::move(column));
  } else {
    group_by_info_.aggregate_columns_to_compute.push_back(std::move(column));
  }
}

// Registers a dot star column whose resolved column is updated.
// The expanded columns need to remap to the new column in the second pass.
absl::Status QueryResolutionInfo::AddDotStarColumnToRemap(
    const ResolvedColumn& old_column, const ResolvedColumn& new_column) {
  GOOGLESQL_RET_CHECK_NE(old_column.column_id(), new_column.column_id());
  GOOGLESQL_RET_CHECK(old_column.type()->Equals(new_column.type()))
      << old_column.DebugString() << " vs. " << new_column.DebugString();
  GOOGLESQL_RET_CHECK(old_column.type_annotation_map() ==
            new_column.type_annotation_map());
  GOOGLESQL_RET_CHECK(old_column.IsInitialized());
  GOOGLESQL_RET_CHECK(new_column.IsInitialized());
  auto [it, inserted] =
      dot_star_columns_to_remap_.insert({old_column, new_column});
  if (!inserted) {
    GOOGLESQL_RET_CHECK(it->second == new_column)
        << "Dot-star column remapping for column " << old_column.DebugString()
        << " collides with previous remapping to " << it->second.DebugString()
        << " with new remapping to " << new_column.DebugString();
  }
  return absl::OkStatus();
}

// Returns the new column to remap to, or nullptr if there is no remapping.
const ResolvedColumn* QueryResolutionInfo::GetDotStarColumnToRemapOrNull(
    const ResolvedColumn& column) const {
  auto it = dot_star_columns_to_remap_.find(column);
  return it == dot_star_columns_to_remap_.end() ? nullptr : &it->second;
}

absl::Status QueryResolutionInfo::SelectListColumnHasAnalytic(
    const ResolvedColumn& column, bool* has_analytic) const {
  for (const std::unique_ptr<SelectColumnState>& select_column_state :
       select_column_state_list_->select_column_state_list()) {
    if (select_column_state->resolved_select_column == column) {
      *has_analytic = select_column_state->expr_findings.has_analytic;
      return absl::OkStatus();
    }
  }
  GOOGLESQL_RET_CHECK_FAIL() << "SelectListColumnHasAnalytic <column> is not a SELECT "
                      "list resolved column";
}

absl::Status QueryResolutionInfo::GetAndRemoveSelectListColumnsWithoutAnalytic(
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        select_columns_without_analytic_out) {
  // Split <select_list_columns_to_compute_> into those that contain analytic
  // functions and those that do not. We do not add columns to
  // <select_columns_without_analytic_out> until the end since otherwise
  // if an error occurs, an ResolvedComputedColumn may appear in both
  // <select_columns_without_analytic_out> and
  // <select_columns_without_analytic_out>, and be deleted twice.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      select_columns_without_analytic;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      select_columns_with_analytic;
  for (std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       select_list_columns_to_compute_) {
    bool has_analytic = false;
    GOOGLESQL_RETURN_IF_ERROR(
        SelectListColumnHasAnalytic(computed_column->column(), &has_analytic));
    if (has_analytic) {
      select_columns_with_analytic.push_back(std::move(computed_column));
    } else {
      select_columns_without_analytic.push_back(std::move(computed_column));
    }
  }

  *select_columns_without_analytic_out =
      std::move(select_columns_without_analytic);
  select_list_columns_to_compute_ = std::move(select_columns_with_analytic);
  return absl::OkStatus();
}

ResolvedColumnList QueryResolutionInfo::GetResolvedColumnList() const {
  ResolvedColumnList resolved_column_list;
  resolved_column_list.reserve(
      select_column_state_list_->select_column_state_list().size() +
      pipe_extra_select_items_.size());
  for (const PipeExtraSelectItem& item : pipe_extra_select_items_) {
    resolved_column_list.push_back(item.column);
  }
  for (const std::unique_ptr<SelectColumnState>& select_column_state :
       select_column_state_list_->select_column_state_list()) {
    resolved_column_list.push_back(select_column_state->resolved_select_column);
  }
  return resolved_column_list;
}

bool QueryResolutionInfo::HasAnalytic() const {
  return analytic_resolver_->HasAnalytic();
}

bool QueryResolutionInfo::HasAggregation() const {
  return group_by_info_.has_aggregation;
}

void QueryResolutionInfo::SetHasAggregation(bool value) {
  group_by_info_.has_aggregation = value;
}

// TODO: Rename to Operator.
bool QueryResolutionInfo::IsPipeOp() const {
  switch (select_form_) {
    case SelectForm::kPipeSelect:
    case SelectForm::kPipeExtend:
    case SelectForm::kPipeAggregate:
    case SelectForm::kPipeWindow:
      return true;
    default:
      return false;
  }
}

bool QueryResolutionInfo::SelectFormAllowsSelectStar() const {
  switch (select_form_) {
    case SelectForm::kClassic:
    case SelectForm::kPipeSelect:
    case SelectForm::kGqlReturn:
    case SelectForm::kGqlWith:
      return true;
    default:
      return false;
  }
}

bool QueryResolutionInfo::SelectFormAllowsAggregation() const {
  switch (select_form_) {
    case SelectForm::kClassic:
    case SelectForm::kPipeAggregate:
    case SelectForm::kGqlReturn:
    case SelectForm::kGqlWith:
      return true;
    default:
      return false;
  }
}

bool QueryResolutionInfo::SelectFormAllowsAnalytic() const {
  switch (select_form_) {
    case SelectForm::kClassic:
    case SelectForm::kPipeWindow:
    case SelectForm::kPipeSelect:
    case SelectForm::kPipeExtend:
    case SelectForm::kGqlReturn:
    case SelectForm::kGqlWith:
      return true;
    default:
      return false;
  }
}

const char* QueryResolutionInfo::SelectFormClauseName() const {
  switch (select_form_) {
    case SelectForm::kClassic:
      return "SELECT";
    case SelectForm::kNoFrom:
      return "SELECT without FROM clause";
    case SelectForm::kPipeSelect:
      return "pipe SELECT";
    case SelectForm::kPipeExtend:
      return "pipe EXTEND";
    case SelectForm::kPipeAggregate:
      return "pipe AGGREGATE";
    case SelectForm::kPipeWindow:
      return "pipe WINDOW";
    case SelectForm::kGqlReturn:
      return "GRAPH RETURN";
    case SelectForm::kGqlWith:
      return "GRAPH WITH";
  }
}

void QueryResolutionInfo::ResetAnalyticResolver(Resolver* resolver) {
  analytic_resolver_ = std::make_unique<AnalyticFunctionResolver>(
      resolver, analytic_resolver_->ReleaseNamedWindowInfoMap());
}

absl::Status QueryResolutionInfo::CheckComputedColumnListsAreEmpty() const {
  GOOGLESQL_RET_CHECK(select_list_columns_to_compute_before_aggregation_.empty());
  GOOGLESQL_RET_CHECK(select_list_columns_to_compute_.empty());
  GOOGLESQL_RET_CHECK(group_by_info_.group_by_column_state_list.empty());
  GOOGLESQL_RET_CHECK(group_by_info_.aggregate_columns_to_compute.empty());
  if (scoped_aggregation_state_->row_range_determined) {
    // If there were no aggregations, sometimes release() may not have been
    // called.
    if (!scoped_aggregate_columns_to_compute().empty()) {
      GOOGLESQL_RET_CHECK_EQ(scoped_aggregate_columns_to_compute().size(), 1);
      GOOGLESQL_RET_CHECK(scoped_aggregate_columns_to_compute().begin()->first.empty());
      GOOGLESQL_RET_CHECK(scoped_aggregate_columns_to_compute().begin()->second.empty());
    }
  } else {
    GOOGLESQL_RET_CHECK(
        group_by_info_.match_recognize_aggregate_columns_to_compute.empty());
  }
  GOOGLESQL_RET_CHECK(group_by_info_.grouping_set_product_inputs.empty());
  GOOGLESQL_RET_CHECK(order_by_columns_to_compute_.empty());
  GOOGLESQL_RET_CHECK(!analytic_resolver_->HasWindowColumnsToCompute());
  return absl::OkStatus();
}

void QueryResolutionInfo::ClearGroupByInfo() { group_by_info_.Reset(); }

std::string QueryResolutionInfo::DebugString() const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "\nselect_column_state_list: ",
                  (select_column_state_list_ == nullptr
                       ? "NULL"
                       : select_column_state_list_->DebugString()),
                  "\n");
  absl::StrAppend(&debug_string, "has_order_by: ", has_order_by_, "\n");
  absl::StrAppend(&debug_string, "has_group_by: ", group_by_info_.has_group_by,
                  "\n");
  absl::StrAppend(&debug_string,
                  "has_aggregation: ", group_by_info_.has_aggregation, "\n");
  absl::StrAppend(&debug_string,
                  "is_group_by_all: ", group_by_info_.is_group_by_all, "\n");

  absl::StrAppend(&debug_string, "with_modifier_mode: ",
                  WithModifierModeToString(with_modifier_mode_), "\n");
  absl::StrAppend(&debug_string, "group_by_column_state_list(size ",
                  group_by_info_.group_by_column_state_list.size(), "):\n");
  for (const GroupByColumnState& group_by_column_state :
       group_by_info_.group_by_column_state_list) {
    absl::StrAppend(&debug_string, group_by_column_state.DebugString("  "),
                    "\n");
  }
  absl::StrAppend(&debug_string, "aggregate_columns(size ",
                  group_by_info_.aggregate_columns_to_compute.size(), "):\n");
  for (const auto& column : group_by_info_.aggregate_columns_to_compute) {
    absl::StrAppend(&debug_string, "  ", column->DebugString(), "\n");
  }
  absl::StrAppend(&debug_string,
                  "is_nested_aggregation: ", is_nested_aggregation_, "\n");
  absl::StrAppend(&debug_string, "MatchRecognizeAggregationState {\n");
  absl::StrAppend(&debug_string, "  row_range_determined: ",
                  scoped_aggregation_state_->row_range_determined, "\n");
  absl::StrAppend(
      &debug_string, "  target_pattern_variable_ref: ",
      scoped_aggregation_state_->target_pattern_variable_ref.has_value()
          ? scoped_aggregation_state_->target_pattern_variable_ref
                ->ToStringView()
          : "<none>",
      "\n");
  absl::StrAppend(&debug_string, "}\n");

  absl::StrAppend(
      &debug_string, "scoped aggregate_columns(size ",
      group_by_info_.match_recognize_aggregate_columns_to_compute.size(),
      " groups):\n");
  for (const auto& [var, aggs] :
       group_by_info_.match_recognize_aggregate_columns_to_compute) {
    absl::StrAppend(&debug_string, "{", ToIdentifierLiteral(var.ToStringView()),
                    ":[\n");
    for (const auto& column : aggs) {
      absl::StrAppend(&debug_string, "  ", column->DebugString(), "\n");
    }
    absl::StrAppend(&debug_string, "}\n");
  }
  absl::StrAppend(&debug_string, "grouping_call_list(size ",
                  group_by_info_.grouping_call_list.size(), "):\n");
  for (const auto& grouping_call : group_by_info_.grouping_call_list) {
    absl::StrAppend(&debug_string, "  ", grouping_call->DebugString(), "\n");
  }
  absl::StrAppend(&debug_string, "grouping_output_columns(size ",
                  group_by_info_.grouping_output_columns.size(), "):\n");
  for (const auto& column : group_by_info_.grouping_output_columns) {
    absl::StrAppend(&debug_string, "  ", column->DebugString(), "\n");
  }
  absl::StrAppend(&debug_string, "aggregate_expr_map size: ",
                  group_by_info_.aggregate_expr_map.size(), "\n");
  absl::StrAppend(
      &debug_string, "group_by_valid_field_info:\n",
      group_by_info_.group_by_valid_field_info_map.DebugString("  "));
  absl::StrAppend(&debug_string, "select_list_valid_field_info:\n",
                  select_list_valid_field_info_map_.DebugString("  "));
  absl::StrAppend(
      &debug_string, "select_list_columns_to_compute_before_aggregation [",
      absl::StrJoin(select_list_columns_to_compute_before_aggregation_, ", ",
                    [](std::string* out, const auto& column) {
                      absl::StrAppend(out, column->column().DebugString());
                    }),
      "]\n");
  return debug_string;
}

const ResolvedExpr* UntypedLiteralMap::Find(const ResolvedColumn& column) {
  if (column_id_to_untyped_literal_map_ == nullptr) {
    if (scan_ == nullptr || scan_->node_kind() != RESOLVED_PROJECT_SCAN) {
      return nullptr;
    }

    // Populate column_id_to_untyped_literal_map_.
    column_id_to_untyped_literal_map_ =
        std::make_unique<absl::flat_hash_map<int, const ResolvedExpr*>>();
    for (const auto& computed_column :
         scan_->GetAs<ResolvedProjectScan>()->expr_list()) {
      const ResolvedExpr* expr = computed_column->expr();
      if (expr != nullptr && expr->node_kind() == RESOLVED_LITERAL) {
        const ResolvedLiteral* literal = expr->GetAs<ResolvedLiteral>();
        if (!literal->has_explicit_type() &&
            (literal->value().is_null() || literal->value().is_empty_array())) {
          column_id_to_untyped_literal_map_->emplace(
              computed_column->column().column_id(), expr);
        }
      }
    }
  }

  return googlesql_base::FindWithDefault(*column_id_to_untyped_literal_map_,
                              column.column_id());
}

absl::StatusOr<DeferredResolutionSelectColumnInfo>
GetDeferredResolutionSelectColumnInfo(const ASTSelectColumn* column) {
  DeferredResolutionFinder deferred_resolution_finder;
  GOOGLESQL_RETURN_IF_ERROR(column->TraverseNonRecursive(&deferred_resolution_finder));
  return deferred_resolution_finder.GetDeferredResolutionSelectColumnInfo();
}

}  // namespace googlesql
