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

// This file contains the implementation of query-related (i.e. SELECT)
// resolver methods from resolver.h.
#include <ctype.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/varsetter.h"
#include "googlesql/analyzer/analytic_function_resolver.h"
#include "googlesql/analyzer/constant_resolver_helper.h"
#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/function_resolver.h"
#include "googlesql/analyzer/function_signature_matcher.h"
#include "googlesql/analyzer/graph_query_resolver.h"
#include "googlesql/analyzer/input_argument_type_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/path_expression_span.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/recursive_queries.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/resolver_common_inl.h"
#include "googlesql/analyzer/rewriters/rewrite_subpipeline.h"
#include "googlesql/analyzer/set_operation_resolver_base.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_output_properties.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/common/reflection.pb.h"
#include "googlesql/common/reflection_helper.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/array_zip_mode.pb.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/node_sources.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_helper.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// Some functions such as ResolveExpr() and ResolveQuery() are called frequently
// in recursive paths, and have many function calls within their bodies. When
// these function calls inlined, it leads to the stack usage of the inlined
// functions being added to the caller, producing very large stack frames. To
// prevent this, we manually annotate these functions to prevent inlining.
//
// TODO: Instead of annotating each called function, configure the
// caller to prevent bad stack behavior.
#define NOINLINE_PREVENT_HUGE_STACK_FRAMES ABSL_ATTRIBUTE_NOINLINE

namespace googlesql {

namespace {

// Extracts an expression from the given scan at the provided column,
// returning nullptr if the column doesn't exist in the scan or does not
// contain an expression.
absl::StatusOr<const ResolvedExpr*> GetColumnExpr(
    const ResolvedProjectScan* scan, const ResolvedColumn& column) {
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       scan->expr_list()) {
    const ResolvedExpr* expr = computed_column->expr();
    GOOGLESQL_RET_CHECK_NE(expr, nullptr);
    if (computed_column->column().column_id() == column.column_id()) {
      return expr;
    }
  }
  return nullptr;
}

// Validates nested struct constructor in rollup, cube and grouping sets. Only
// single-level struct constructors are allowed in the grouping set.
// See (broken link):syntax.
absl::Status ValidateNestedStructConstructor(const ASTExpression* expr,
                                             const LanguageOptions& language,
                                             absl::string_view clause_name) {
  // The expr will be null if it's an empty grouping set, a.k.a. ().
  if (expr == nullptr) {
    return absl::OkStatus();
  }
  if (!expr->Is<ASTStructConstructorWithParens>()) {
    return absl::OkStatus();
  }

  const ASTStructConstructorWithParens* struct_constructor =
      expr->GetAs<ASTStructConstructorWithParens>();
  for (const ASTExpression* field_expr :
       struct_constructor->field_expressions()) {
    if (field_expr->Is<ASTStructConstructorWithParens>()) {
      return MakeSqlErrorAt(field_expr)
             << "Nested column list is not allowed in " << clause_name;
    }
  }
  return absl::OkStatus();
}

// Validates Rollup to check the feature option is set properly, and all its
// expressions are not nested struct constructors.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status ValidateRollup(const ASTRollup* rollup,
                            const LanguageOptions& language,
                            size_t grouping_item_count) {
  GOOGLESQL_RET_CHECK(rollup != nullptr);
  if (!language.LanguageFeatureEnabled(FEATURE_GROUP_BY_ROLLUP)) {
    return MakeSqlErrorAt(rollup) << "GROUP BY ROLLUP is unsupported";
  }
  // Multi grouping sets are only supported when
  // FEATURE_MULTI_GROUPING_SETS and FEATURE_GROUPING_SETS are enabled.
  if ((!language.LanguageFeatureEnabled(FEATURE_MULTI_GROUPING_SETS) ||
       !language.LanguageFeatureEnabled(FEATURE_GROUPING_SETS)) &&
      grouping_item_count > 1) {
    return MakeSqlErrorAt(rollup)
           << "The GROUP BY clause only supports ROLLUP when there are no "
           << "other grouping elements";
  }
  // Currently Rollup allows nested struct construtor, and it will be disabled
  // under the proposal (broken link).
  if (language.LanguageFeatureEnabled(FEATURE_GROUPING_SETS)) {
    for (const ASTExpression* expr : rollup->expressions()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateNestedStructConstructor(expr, language, "ROLLUP"));
    }
  }
  return absl::OkStatus();
}

// Validates Cube to check the feature option is set properly, and all its
// expressions are not nested struct constructors.
absl::Status ValidateCube(const ASTCube* cube, const LanguageOptions& language,
                          size_t grouping_item_count) {
  GOOGLESQL_RET_CHECK(cube != nullptr);
  if (!language.LanguageFeatureEnabled(FEATURE_GROUPING_SETS)) {
    return MakeSqlErrorAt(cube) << "GROUP BY CUBE is unsupported";
  }
  if (!language.LanguageFeatureEnabled(FEATURE_MULTI_GROUPING_SETS) &&
      grouping_item_count > 1) {
    return MakeSqlErrorAt(cube)
           << "The GROUP BY clause only supports CUBE when there are no other "
           << "grouping elements";
  }
  for (const ASTExpression* expr : cube->expressions()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateNestedStructConstructor(expr, language, "CUBE"));
  }
  return absl::OkStatus();
}

// Validates GroupingSetList and all its sub grouping set.
absl::Status ValidateGroupingSetList(
    const ASTGroupingSetList* grouping_set_list,
    const LanguageOptions& language, size_t grouping_item_count) {
  GOOGLESQL_RET_CHECK(grouping_set_list != nullptr);
  if (!language.LanguageFeatureEnabled(FEATURE_GROUPING_SETS)) {
    return MakeSqlErrorAt(grouping_set_list)
           << "GROUP BY GROUPING SETS is unsupported";
  }
  if (!language.LanguageFeatureEnabled(FEATURE_MULTI_GROUPING_SETS) &&
      grouping_item_count > 1) {
    return MakeSqlErrorAt(grouping_set_list)
           << "The GROUP BY clause only supports GROUPING SETS when there are "
              "no other grouping elements";
  }
  for (const ASTGroupingSet* grouping_set :
       grouping_set_list->grouping_sets()) {
    if (grouping_set->rollup() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateRollup(grouping_set->rollup(), language,
                                     grouping_item_count));
    } else if (grouping_set->cube() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateCube(grouping_set->cube(), language, grouping_item_count));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ValidateNestedStructConstructor(
          grouping_set->expression(), language, "GROUPING SETS"));
    }
  }
  return absl::OkStatus();
}

// Returns a human-readable name for the SQL syntax that the human used to
// create the given pipe AST node, to be used in the explanation of an error
// message.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
std::string PipeOperatorName(const ASTNode* pipe_node) {
  static const auto* kPipeOperatorNameMap =
      new absl::flat_hash_map<ASTNodeKind, absl::string_view>({
          {AST_FROM_QUERY, "FROM query"},
          {AST_PIPE_JOIN, "pipe JOIN"},
          {AST_PIPE_CALL, "pipe CALL"},
          {AST_PIPE_SELECT, "pipe SELECT"},
          {AST_PIPE_AGGREGATE, "pipe AGGREGATE"},
          {AST_PIPE_DROP, "pipe DROP"},
          {AST_PIPE_SET, "pipe SET"},
          {AST_PIPE_WHERE, "pipe WHERE"},
          {AST_PIPE_LIMIT_OFFSET, "pipe LIMIT"},
          {AST_PIPE_ORDER_BY, "pipe ORDER BY"},
          {AST_PIPE_TABLESAMPLE, "pipe TABLESAMPLE"},
          {AST_PIPE_STATIC_DESCRIBE, "pipe STATIC_DESCRIBE"},
          {AST_PIPE_ASSERT, "pipe ASSERT"},
          {AST_PIPE_EXTEND, "pipe EXTEND"},
          {AST_PIPE_AS, "pipe AS"},
          {AST_PIPE_DISTINCT, "pipe DISTINCT"},
          {AST_PIPE_WINDOW, "pipe WINDOW"},
          {AST_PIPE_RENAME, "pipe RENAME"},
          {AST_PIPE_SET_OPERATION, "pipe set operation (such as UNION)"},
      });
  const absl::string_view* name =
      googlesql_base::FindOrNull(*kPipeOperatorNameMap, pipe_node->node_kind());
  if (name != nullptr) {
    return std::string(*name);
  }
  return pipe_node->GetNodeKindString();
}

// Returns an error if the given query has an unbounded list of selected
// columns, which is implicitly possible when using pipe syntax. Intended to be
// used in strict mode to avoid the number of output query columns being
// determined by the schema.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status DisallowImplicitSelectStar(
    const ASTQueryExpression* query_expr,
    absl::Span<const ASTPipeOperator* const> pipe_operator_list) {
  // For each pipe operator, if this is nullopt, then the input columns are
  // explicitly listed. Otherwise, this recalls the previous place where the
  // input columns became implicit.
  std::optional<const ASTNode*> implicit_columns_node;
  // If the initial query is a FROM query, all columns are implicitly selected.
  // Otherwise, we can assume the input query was already checked.
  if (query_expr->node_kind() == AST_FROM_QUERY) {
    implicit_columns_node = query_expr;
  }
  for (const ASTPipeOperator* pipe_operator : pipe_operator_list) {
    switch (pipe_operator->node_kind()) {
      case AST_PIPE_JOIN:
      case AST_PIPE_CALL:
        implicit_columns_node = pipe_operator;
        break;
      case AST_PIPE_SELECT:
      case AST_PIPE_AGGREGATE:
        // These operators explicitly list their output columns.
        implicit_columns_node = std::nullopt;
        break;
      case AST_PIPE_DROP:
      case AST_PIPE_SET:
      case AST_PIPE_WHERE:
      case AST_PIPE_LIMIT_OFFSET:
      case AST_PIPE_ORDER_BY:
      case AST_PIPE_TABLESAMPLE:
      case AST_PIPE_STATIC_DESCRIBE:
      case AST_PIPE_ASSERT:
        // These operators have no effect either way; the safety of the input
        // will equal the safety of the output.
        break;
      case AST_PIPE_EXTEND:
      case AST_PIPE_AS:
      case AST_PIPE_DISTINCT:
      case AST_PIPE_RENAME:
      case AST_PIPE_SET_OPERATION:
        // These operators, if encountered with an implicit list of input
        // columns, are not allowed in strict mode. Otherwise, they have no
        // effect either way.
        if (implicit_columns_node != std::nullopt) {
          return MakeSqlErrorAt(pipe_operator)
                 << "In strict mode, a " << PipeOperatorName(pipe_operator)
                 << " requires that the input columns are explicitly listed in "
                    "the query; however a preceding "
                 << PipeOperatorName(*implicit_columns_node)
                 << " implicitly selects all columns; consider adding a pipe "
                    "SELECT between them to resolve this";
        }
        break;
      default:
        // New pipe operators should be triaged and added to the appropriate
        // place in the switch above in order to work in strict mode.
        return MakeSqlErrorAt(pipe_operator)
               << "Pipe operator not handled in strict mode: "
               << pipe_operator->GetNodeKindString();
    }
  }
  if (implicit_columns_node != std::nullopt) {
    return MakeSqlErrorAt(*implicit_columns_node)
           << "A " << PipeOperatorName(*implicit_columns_node)
           << " implicitly selects all columns, which is not allowed in strict "
              "mode without a subsequent operator to select columns "
              "explicitly, such as pipe SELECT or pipe AGGREGATE";
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status CreateUnsupportedGroupingSetsError(
    const QueryResolutionInfo* query_resolution_info, const ASTSelect* select,
    absl::string_view clause_name) {
  if (!query_resolution_info->HasGroupByGroupingSets()) {
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(select != nullptr);
  GOOGLESQL_RET_CHECK(select->group_by() != nullptr);
  GOOGLESQL_RET_CHECK(!select->group_by()->grouping_items().empty());
  const ASTGroupingItem* grouping_item =
      select->group_by()->grouping_items()[0];

  std::string grouping_clause_name;
  if (grouping_item->rollup() != nullptr) {
    grouping_clause_name = "ROLLUP";
  } else if (grouping_item->cube() != nullptr) {
    grouping_clause_name = "CUBE";
  } else if (grouping_item->grouping_set_list() != nullptr) {
    grouping_clause_name = "GROUPING SETS";
  } else {
    return absl::InvalidArgumentError(
        absl::StrFormat("Expect a node with grouping set, node: %s",
                        grouping_item->DebugString()));
  }

  return MakeSqlErrorAt(grouping_item)
         << absl::StrFormat("GROUP BY %s is not supported in %s",
                            grouping_clause_name, clause_name);
}

// Check that expected scan nodes were added on top of a previous scan.
// Starting from `outer_scan` and recursively traversing input scans, expect
// to find `inner_scan`.  In between, expect to find a node of type
// `expected_scan_kind` (unless `expected_scan_kind` is std::nullopt).
// Other scan types in `allowed_extra_scan_types` are also allowed.
// `node_context` is a name for the error message.
//
// Note: This only works for the node types listed in the switch.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status CheckForExpectedScans(
    const ResolvedScan* outer_scan, const ResolvedScan* inner_scan,
    std::optional<ResolvedNodeKind> expected_scan_kind,
    const char* node_context,
    const std::set<ResolvedNodeKind>& allowed_extra_scan_types = {}) {
  const ResolvedScan* next_scan = outer_scan;
  bool found_expected = false;
  while (next_scan != inner_scan) {
    const ResolvedNodeKind kind = next_scan->node_kind();
    if (!found_expected && expected_scan_kind.has_value() &&
        kind == expected_scan_kind.value()) {
      found_expected = true;
    } else if (allowed_extra_scan_types.find(kind) ==
               allowed_extra_scan_types.end()) {
      GOOGLESQL_RET_CHECK_FAIL() << node_context << " produced unexpected scan of type "
                       << ResolvedNodeKindToString(kind);
    }

    switch (kind) {
      case RESOLVED_PROJECT_SCAN:
        next_scan = next_scan->GetAs<ResolvedProjectScan>()->input_scan();
        break;
      case RESOLVED_AGGREGATE_SCAN:
        next_scan = next_scan->GetAs<ResolvedAggregateScan>()->input_scan();
        break;
      case RESOLVED_DIFFERENTIAL_PRIVACY_AGGREGATE_SCAN:
        next_scan = next_scan->GetAs<ResolvedDifferentialPrivacyAggregateScan>()
                        ->input_scan();
        break;
      case RESOLVED_ANALYTIC_SCAN:
        next_scan = next_scan->GetAs<ResolvedAnalyticScan>()->input_scan();
        break;
      case RESOLVED_ORDER_BY_SCAN:
        next_scan = next_scan->GetAs<ResolvedOrderByScan>()->input_scan();
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << node_context << " produced unhandled scan of type "
                         << ResolvedNodeKindToString(kind);
    }
    GOOGLESQL_RET_CHECK(next_scan != nullptr);
  }

  if (expected_scan_kind.has_value() && !found_expected) {
    GOOGLESQL_RET_CHECK_FAIL() << node_context
                     << " failed to produce a scan with expected type "
                     << ResolvedNodeKindToString(expected_scan_kind.value());
  }
  return absl::OkStatus();
}

// `PartitionedComputedColumns` represents a partitioning of computed columns
// such that:
//    - Each 'dependee_column' is depended upon by at least 1 other column in
//      `dependee_computed_columns` or `non_dependee_computed_columns`.
//    - Each 'non_dependee_column' is not depended on by any other column
//      in `dependee_computed_columns` or `non_dependee_computed_columns`.
struct PartitionedComputedColumns {
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      dependee_computed_columns;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      non_dependee_computed_columns;
};

// Partition `computed_columns` into 'dependee' and 'non_dependee' computed
// column sets.
absl::StatusOr<PartitionedComputedColumns> PartitionComputedColumns(
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        computed_columns) {
  // Gather column references.
  absl::flat_hash_set<const ResolvedColumnRef*> column_refs;
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       computed_columns) {
    GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<const ResolvedColumnRef*>
                         computed_column_column_refs,
                     CollectFreeColumnRefs(*computed_column));
    column_refs.merge(computed_column_column_refs);
  }

  auto is_dependee_column =
      [&column_refs](const std::unique_ptr<const ResolvedComputedColumn>&
                         computed_column) {
        for (const ResolvedColumnRef* column_ref : column_refs) {
          if (column_ref->column() == computed_column->column()) {
            return true;
          }
        }
        return false;
      };

  // Partition `computed_columns` into 'dependee' and 'non_dependee' sets.
  // 'dependee' computed columns will be held in `computed_columns`;
  // 'non_dependee' computed columns will held in
  // `non_dependee_computed_columns`.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      non_dependee_computed_columns;
  auto it_non_dependee_computed_columns = std::stable_partition(
      computed_columns.begin(), computed_columns.end(), is_dependee_column);
  non_dependee_computed_columns.insert(
      non_dependee_computed_columns.end(),
      std::make_move_iterator(it_non_dependee_computed_columns),
      std::make_move_iterator(computed_columns.end()));
  computed_columns.erase(it_non_dependee_computed_columns,
                         computed_columns.end());
  return PartitionedComputedColumns{
      .dependee_computed_columns = std::move(computed_columns),
      .non_dependee_computed_columns =
          std::move(non_dependee_computed_columns)};
}

// Returns the original select item expression for a given select column state.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
const ResolvedExpr* GetPreGroupByResolvedExpr(
    const SelectColumnState* select_column_state) {
  if (select_column_state->original_resolved_expr != nullptr) {
    return select_column_state->original_resolved_expr;
  }
  return select_column_state->resolved_expr == nullptr
             ? select_column_state->resolved_computed_column->expr()
             : select_column_state->resolved_expr.get();
}
// Returns true when there is more than one grouping item and at least one of
// them is not a single expression. For example, `GROUP BY x, y` is not
// considered to have multi grouping sets; `GROUP BY ROLLUP(x), y` is considered
// to have multi grouping sets.
bool IsMultiGroupingSets(const ASTGroupBy* group_by) {
  if (group_by->grouping_items().size() <= 1) {
    return false;
  }
  bool all_expr = true;
  for (const ASTGroupingItem* grouping_item : group_by->grouping_items()) {
    if (grouping_item->expression() == nullptr) {
      all_expr = false;
      break;
    }
  }
  return !all_expr;
}

// Checks that the row identity columns are valid w.r.t. the `table` and are
// groupable.
//
// `measure_source_string` is used for error message printing.
//
// `column_name` represents which column the row identity columns are specified
// for. If empty, the row identity columns are for the measure source
// represented by `table`, e.g., a catalog table or a tvf output.
absl::Status CheckRowIdentityColumns(
    const ASTNode* ast_location, const std::vector<int>& row_identity_columns,
    const Table* table, const LanguageOptions& language,
    absl::string_view measure_source_string,
    absl::string_view column_name = "") {
  std::string capitalized_measure_source;
  if (!measure_source_string.empty()) {
    capitalized_measure_source =
        absl::StrCat(absl::AsciiStrToUpper(measure_source_string.substr(0, 1)),
                     measure_source_string.substr(1));
  }
  if (row_identity_columns.empty()) {
    if (column_name.empty()) {
      // TODO: b/450295734 - Improve the error message. "Row identity columns"
      // are probably not understood by users, and users might not be able
      // to fix this since they don't necessarily own the table. We should
      // revisit whether this should be a SQL error or an internal error.
      return MakeSqlErrorAt(ast_location)
             << capitalized_measure_source
             << " must have at least one row identity column if it contains "
                "a measure column.";
    }
    return MakeSqlErrorAt(ast_location)
           << "Measure column " << column_name
           << " must have at least one row identity column.";
  }

  const std::string error_column_source =
      column_name.empty() ? ""
                          : absl::StrCat(" for measure column ", column_name);
  for (int row_identity_column_idx : row_identity_columns) {
    if (row_identity_column_idx < 0 ||
        row_identity_column_idx >= table->NumColumns()) {
      return MakeSqlErrorAt(ast_location)
             << capitalized_measure_source
             << " has invalid index for row identity column"
             << error_column_source << ". Index: " << row_identity_column_idx;
    }
    const Column* row_identity_column =
        table->GetColumn(row_identity_column_idx);
    std::string type_description;
    if (!row_identity_column->GetType()->SupportsGrouping(language,
                                                          &type_description)) {
      return MakeSqlErrorAt(ast_location)
             << capitalized_measure_source << " has row identity column "
             << row_identity_column->Name() << error_column_source
             << " with type " << type_description << " which is not groupable.";
    }
  }
  return absl::OkStatus();
}

// Validates a measure column, and sets `needs_table_row_identity_columns` to
// true if table-level row identity columns are required for this measure
// column.
//
// - `column_name` is passed in separately instead of using `column->Name()`
//   because `column->Name()` can be empty.
// - `measure_source_string`: A string explanation on the source of the measure
//   column, e.g., "table x", "table valued function xy". This is used for
//   error messages.
// - `needs_table_row_identity_columns`: Output parameter, set to true if
//   table-level row identity columns are required for this measure column.
//   Must not be null.
absl::Status ValidateMeasureColumn(const ASTNode* ast_location,
                                   const Column* column, const Table* table,
                                   IdString column_name,
                                   const LanguageOptions& language,
                                   absl::string_view measure_source_string,
                                   bool* needs_table_row_identity_columns) {
  *needs_table_row_identity_columns = false;
  if (!language.LanguageFeatureEnabled(FEATURE_ENABLE_MEASURES)) {
    return MakeSqlErrorAt(ast_location)
           << "Column " << column_name << " in " << measure_source_string
           << " has unsupported type "
           << column->GetType()->TypeName(language.product_mode());
  }
  if (!column->HasMeasureExpression()) {
    // TODO: b/350555383 -
    // Attempt to re-resolve the measure expression if the sql expression
    // is available but the resolved expression is nullptr (this case can
    // occur following column deserialization).
    return MakeSqlErrorAt(ast_location)
           << "Column " << column_name << " in " << measure_source_string
           << " is a measure column but does not have a measure "
              "expression";
  } else {
    // Validate the supplied measure expression.
    googlesql::Column::ExpressionAttributes expression_attributes =
        column->GetExpression().value();
    if (!expression_attributes.HasResolvedExpression()) {
      return MakeSqlErrorAt(ast_location)
             << "Column " << column_name << " in " << measure_source_string
             << " is a measure column but does not have a measure "
                "expression";
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateMeasureExpression(
        expression_attributes.GetExpressionString(),
        *expression_attributes.GetResolvedExpression(), language,
        column_name.ToStringView()));
    if (expression_attributes.RowIdentityColumns().has_value()) {
      GOOGLESQL_RETURN_IF_ERROR(CheckRowIdentityColumns(
          ast_location, *expression_attributes.RowIdentityColumns(), table,
          language, measure_source_string, column_name.ToStringView()));
    } else {
      *needs_table_row_identity_columns = true;
    }
  }
  return absl::OkStatus();
}

}  // namespace

// These are constant identifiers used mostly for generated column or table
// names.  We use a single IdString for each so we never have to allocate
// or copy these strings again.
const IdString& Resolver::kArrayId =
    *new IdString(IdString::MakeGlobal("$array"));
const IdString& Resolver::kOffsetAlias =
    *new IdString(IdString::MakeGlobal("offset"));
const IdString& Resolver::kWeightAlias =
    *new IdString(IdString::MakeGlobal("weight"));
const IdString& Resolver::kArrayOffsetId =
    *new IdString(IdString::MakeGlobal("$array_offset"));
const IdString& Resolver::kLambdaArgId =
    *new IdString(IdString::MakeGlobal("$lambda_arg"));
const IdString& Resolver::kWithActionId =
    *new IdString(IdString::MakeGlobal("$with_action"));
const IdString& Resolver::kRecursionDepthAlias =
    *new IdString(IdString::MakeGlobal("depth"));
const IdString& Resolver::kRecursionDepthId =
    *new IdString(IdString::MakeGlobal("$recursion_depth"));

STATIC_IDSTRING(kDistinctId, "$distinct");
STATIC_IDSTRING(kPipeDistinctId, "$pipe_distinct");
STATIC_IDSTRING(kFullJoinId, "$full_join");
STATIC_IDSTRING(kGroupById, "$groupby");
STATIC_IDSTRING(kMakeProtoId, "$make_proto");
STATIC_IDSTRING(kMakeStructId, "$make_struct");
STATIC_IDSTRING(kOrderById, "$orderby");
STATIC_IDSTRING(kPreGroupById, "$pre_groupby");
STATIC_IDSTRING(kPreProjectId, "$preproject");
STATIC_IDSTRING(kProtoId, "$proto");
STATIC_IDSTRING(kStructId, "$struct");
STATIC_IDSTRING(kValueColumnId, "$value_column");
STATIC_IDSTRING(kCastedColumnId, "$casted_column");
STATIC_IDSTRING(kWeightId, "$sample_weight");
STATIC_IDSTRING(kDummyTableId, "$dummy_table");
STATIC_IDSTRING(kPivotId, "$pivot");
STATIC_IDSTRING(kUnpivotColumnId, "$unpivot");
STATIC_IDSTRING(kMatchRecognizeId, "$match_recognize");
STATIC_IDSTRING(kGraphTableId, "$graph_table");
STATIC_IDSTRING(kRowId, "$row");

absl::Status Resolver::CheckForUnwantedSelectClauseChildNodes(
    const ASTSelect* select,
    absl::flat_hash_set<const ASTNode*> allowed_children,
    const char* node_context) {
  for (int i = 0; i < select->num_children(); ++i) {
    const ASTNode* child = select->child(i);
    GOOGLESQL_RET_CHECK(child != nullptr);
    if (!allowed_children.contains(child)) {
      GOOGLESQL_RET_CHECK_FAIL() << node_context << " has unexpected child node of type "
                       << child->GetNodeKindString();
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveQueryAfterWith(
    const ASTQuery* query, const NameScope* scope, IdString query_alias,
    const Type* inferred_type_for_query,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  // Note: If <query> is the input to a PIVOT clause, force each projection
  // to create a new column. This is necessary because it is possible for a
  // pivot clause to reference some, but not all, projections of the column, and
  // the rules for PIVOT require only unreferenced columns to be used for
  // grouping; collapsing projections to their underlying column would lose this
  // distinction and cause the list of referenced columns to be calculated
  // incorrectly. As an example, consider the following query:
  //   SELECT * FROM (SELECT x AS y, x AS z FROM t)
  //                 PIVOT (SUM(y) FOR y IN (0,1))
  //
  // If y, and z were collapsed, z would be excluded in the groupby list
  // because we would see y and z as the same column, so the reference to y
  // would incorrectly count as a reference to z.
  bool force_new_columns_for_projected_outputs = query->is_pivot_input();

  // If the query has a lock mode set, then push it to the stack so that
  // subsequent ResolvedTableScan nodes created can set the lock mode. If the
  // lock mode shouldn't be propagated further, resolve calls further down will
  // prevent it.
  // E.g. for WITH statements, ResolveWithClauseIfPresent() will stop the
  // propagation..
  bool pushed_lock_mode = false;
  if (query->lock_mode() != nullptr) {
    PushLockMode(query->lock_mode());
    pushed_lock_mode = true;
  }
  absl::Cleanup cleanup = [this, pushed_lock_mode] {
    if (pushed_lock_mode) {
      PopLockMode();
    }
  };

  if (query->query_expr()->node_kind() == AST_SELECT) {
    // If we just have a single SELECT, then we treat that specially so
    // we can resolve the ORDER BY and LIMIT directly inside that SELECT.
    return ResolveSelect(query->query_expr()->GetAsOrDie<ASTSelect>(),
                         query->order_by(), query->limit_offset(), scope,
                         query_alias, force_new_columns_for_projected_outputs,
                         inferred_type_for_query, output, output_name_list);
  }

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveQueryExpression(query->query_expr(), scope, query_alias,
                             force_new_columns_for_projected_outputs, output,
                             output_name_list, inferred_type_for_query));
  GOOGLESQL_RET_CHECK(*output_name_list != nullptr);

  if (query->order_by() != nullptr) {
    const std::unique_ptr<const NameScope> query_expression_name_scope(
        new NameScope(scope, *output_name_list));
    GOOGLESQL_RETURN_IF_ERROR(ResolveOrderBySimple(query->order_by(), **output_name_list,
                                         query_expression_name_scope.get(),
                                         "ORDER BY clause after set operation",
                                         OrderBySimpleMode::kNormal, output));
  }

  if (query->limit_offset() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveLimitOffsetScan(query->limit_offset(), scope, output));
  }

  return absl::OkStatus();
}

void Resolver::AddNamedSubquery(const std::vector<IdString>& alias,
                                std::unique_ptr<NamedSubquery> named_subquery) {
  auto it = named_subquery_map_.find(alias);
  if (it == named_subquery_map_.end()) {
    named_subquery_map_[std::vector<IdString>{alias}] =
        std::vector<std::unique_ptr<NamedSubquery>>{};
    it = named_subquery_map_.find(alias);
  }
  it->second.push_back(std::move(named_subquery));
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedWithEntry>>>
Resolver::ResolveWithClauseIfPresent(const ASTWithClause* with_clause,
                                     bool is_outer_query) {
  // As we're about to resolve a WITH statement, push a nullptr lock mode
  // node onto the stack so that any subsequent queries don't inherit a lock
  // mode set by an outer query.
  PushLockMode(/*lock_mode=*/nullptr);
  absl::Cleanup cleanup = [this] { PopLockMode(); };

  std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entries;
  if (with_clause != nullptr) {
    if (!is_outer_query &&
        !language().LanguageFeatureEnabled(FEATURE_WITH_ON_SUBQUERY)) {
      return MakeSqlErrorAt(with_clause)
             << "WITH is not supported on subqueries in this language version";
    }

    // Validate with clause entries.
    IdStringHashSetCase alias_names;
    bool found_group_rows = false;
    for (int i = 0; i < with_clause->entries().size(); i++) {
      const ASTWithClauseEntry* entry = with_clause->entries(i);
      if (entry->aliased_query() != nullptr) {
        const ASTAliasedQuery* aliased_query = entry->aliased_query();
        if (!googlesql_base::InsertIfNotPresent(&alias_names,
                                     aliased_query->alias()->GetAsIdString())) {
          return MakeSqlErrorAt(aliased_query->alias())
                 << "Duplicate alias " << aliased_query->alias()->GetAsString()
                 << " for WITH subquery";
        }
      } else {
        GOOGLESQL_RET_CHECK(entry->aliased_group_rows() != nullptr);
        const ASTAliasedGroupRows* aliased_group_rows =
            entry->aliased_group_rows();
        if (is_outer_query) {
          return MakeSqlErrorAt(aliased_group_rows)
                 << "GROUP ROWS are not allowed in the outer-most query";
        }
        if (found_group_rows) {
          return MakeSqlErrorAt(aliased_group_rows)
                 << "Cannot have more than one GROUP ROWS alias in a with "
                    "clause";
        }
        if (i != 0) {
          return MakeSqlErrorAt(aliased_group_rows)
                 << "GROUP ROWS must be the first entry in a WITH clause";
        }
        found_group_rows = true;
      }
    }

    if (with_clause->recursive()) {
      if (!language().LanguageFeatureEnabled(FEATURE_WITH_RECURSIVE)) {
        return MakeSqlErrorAt(with_clause)
               << "RECURSIVE is not supported in the WITH clause";
      }

      GOOGLESQL_ASSIGN_OR_RETURN(WithEntrySortResult sort_result,
                       SortWithEntries(with_clause));

      for (const ASTAliasedQuery* with_entry : sort_result.sorted_entries) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<const ResolvedWithEntry> resolved_with_entry,
            ResolveAliasedQuery(
                with_entry,
                sort_result.self_recursive_entries.contains(with_entry)));
        with_entries.push_back(std::move(resolved_with_entry));
      }
    } else {
      // Non-recursive WITH
      for (const ASTWithClauseEntry* entry : with_clause->entries()) {
        if (entry->aliased_query() != nullptr) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              std::unique_ptr<const ResolvedWithEntry> resolved_with_entry,
              ResolveAliasedQuery(entry->aliased_query(), /*recursive=*/false));
          with_entries.push_back(std::move(resolved_with_entry));
        }
      }
    }
  }
  return with_entries;
}

absl::Status Resolver::FinishResolveWithClauseIfPresent(
    const ASTQuery* query,
    std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entries,
    std::unique_ptr<const ResolvedScan>* output) {
  if (query->with_clause() == nullptr) {
    return absl::OkStatus();
  }
  // Now remove any WITH entry mappings we added, restoring what was visible
  // outside this WITH clause.
  for (const ASTWithClauseEntry* entry : query->with_clause()->entries()) {
    if (entry->aliased_query() != nullptr) {
      const IdString with_alias =
          entry->aliased_query()->alias()->GetAsIdString();
      GOOGLESQL_RETURN_IF_ERROR(RemoveInnermostNamedSubqueryWithAlias(with_alias));
    }
  }

  // Wrap a ResolvedWithScan around the output query.
  //
  // NOTE: Now that GROUP ROWS are supported, it's possible for a WITH CLAUSE to
  //       have no with_entries. We skip creating a WithScan in this case.
  if (!with_entries.empty()) {
    const auto& tmp_column_list = (*output)->column_list();
    *output = MakeResolvedWithScan(tmp_column_list, std::move(with_entries),
                                   std::move(*output),
                                   query->with_clause()->recursive());
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveQuery(
    const ASTQuery* query, const NameScope* scope, IdString query_alias,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list,
    ResolveQueryOptions options) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // If we have pipe operators, the inferred type applies to the last pipe
  // operator, not to the initial query.
  const Type* inferred_type_for_base_query = nullptr;
  if (query->pipe_operator_list().empty()) {
    inferred_type_for_base_query = options.inferred_type_for_query;
  }

  bool pushed_lock_mode = false;
  if (options.is_expr_subquery) {
    // Can't be both an outer query and an expression subquery.
    GOOGLESQL_RET_CHECK(!options.is_outer_query);

    // We're about to resolve an expression subquery. So push a nullptr lock
    // mode node onto the stack so that any subsequent queries don't inherit a
    // lock mode set by an outer query.
    PushLockMode(/*lock_mode=*/nullptr);
    pushed_lock_mode = true;
  }
  absl::Cleanup cleanup = [this, pushed_lock_mode] {
    if (pushed_lock_mode) {
      PopLockMode();
    }
  };

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entries,
      ResolveWithClauseIfPresent(query->with_clause(), options.is_outer_query));
  GOOGLESQL_RETURN_IF_ERROR(ResolveQueryAfterWith(query, scope, query_alias,
                                        inferred_type_for_base_query, output,
                                        output_name_list));
  GOOGLESQL_RET_CHECK(*output_name_list != nullptr);

  absl::Span<const ASTPipeOperator* const> pipe_operator_list =
      query->pipe_operator_list();
  if (options.exclude_last_pipe_operator) {
    GOOGLESQL_RET_CHECK(!pipe_operator_list.empty());
    pipe_operator_list =
        pipe_operator_list.subspan(0, pipe_operator_list.size() - 1);
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolvePipeOperatorList(
      pipe_operator_list, scope, output, output_name_list,
      options.inferred_type_for_query, options.allow_terminal));

  if (in_strict_mode()) {
    GOOGLESQL_RETURN_IF_ERROR(
        DisallowImplicitSelectStar(query->query_expr(), pipe_operator_list));
  }

  if (*output_name_list == nullptr) {
    // The query has a terminal operator and doesn't return a table.
    GOOGLESQL_RET_CHECK(options.allow_terminal);
  } else {
    // Add coercions to the final column output types if needed.
    if (options.is_outer_query &&
        !analyzer_options().get_target_column_types().empty()) {
      // TODO The function below assumes the outer scan is a
      // ResolvedProjectScan, so it needs to be generalized for pipes.
      GOOGLESQL_RET_CHECK(query->pipe_operator_list().empty())
          << "Coercing statement to expected types not implemented yet "
             "for pipe queries";
      GOOGLESQL_RETURN_IF_ERROR(CoerceQueryStatementResultToTypes(
          query, analyzer_options().get_target_column_types(), output,
          output_name_list));
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(
      FinishResolveWithClauseIfPresent(query, std::move(with_entries), output));

  // Add parse location to the outermost ResolvedScan only. This is intended
  // because the outermost ResolvedScan represents a query_expr (query or
  // subquery) in the syntax
  // in (broken link)#sql-syntax.
  // This is safe for existing engines because parse locations in resolved AST
  // are mainly for debugging and indexing purposes. Existing engines don't
  // need parse locations during query execution.
  MaybeRecordParseLocation(query, const_cast<ResolvedScan*>(output->get()));

  return absl::OkStatus();
}

// Convert a NameList representing a value table query result into a
// NameList for a FROM clause scanning that value table with <alias>.
// input_name_list and output_name_list may be the same NameList.
static absl::Status ConvertValueTableNameListToNameListWithValueTable(
    const ASTNode* ast_location, IdString alias,
    const std::shared_ptr<const NameList>& input_name_list,
    std::shared_ptr<NameList>* output_name_list) {
  GOOGLESQL_RET_CHECK(input_name_list->is_value_table());
  GOOGLESQL_RET_CHECK_EQ(input_name_list->num_columns(), 1);

  std::shared_ptr<NameList> new_name_list(new NameList);
  GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddValueTableColumn(
      alias, input_name_list->column(0).column(), ast_location));
  *output_name_list = new_name_list;
  return absl::OkStatus();
}

// Create a new NameList that gives the result of converting `input_name_list`
// into a flattened row with a new alias for the row.
// The new row will have the same column list, with `alias` as a range
// variable for the row.  Existing range variables are dropped.
// If `input_name_list` is a value table, `output_name_list` will still be
// a value table.
static absl::Status UpdateNameListForTableAlias(
    const ASTNode* ast_location, IdString alias,
    const std::shared_ptr<const NameList>& input_name_list,
    std::shared_ptr<const NameList>* output_name_list) {
  if (input_name_list->is_value_table()) {
    GOOGLESQL_RET_CHECK_EQ(input_name_list->num_columns(), 1);
    auto new_name_list = std::make_shared<NameList>();
    // For a value table subquery with an alias, the NameList gets converted
    // to a regular row containing a value table.
    if (input_name_list->HasRangeVariables()) {
      // If the value table has an existing range variable, then it has to be
      // renamed to the new name.
      GOOGLESQL_RETURN_IF_ERROR(
          new_name_list->MergeFrom(*input_name_list, ast_location,
                                   {.rename_value_table_to_name = &alias}));
    } else {
      // If the value table was anonymous and didn't have an existing range
      // variable, this function will create one, using `alias` as the name.
      GOOGLESQL_RETURN_IF_ERROR(ConvertValueTableNameListToNameListWithValueTable(
          ast_location, alias, input_name_list, &new_name_list));
    }
    GOOGLESQL_RETURN_IF_ERROR(new_name_list->SetIsValueTable());
    *output_name_list = new_name_list;
    return absl::OkStatus();
  }

  // For a regular table subquery with an alias, use `flatten_to_table`
  // to drop existing range variables and convert value tables to columns,
  // and then make the new range variables pointing at the remaining columns.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto new_name_list,
      input_name_list->Copy(ast_location, {.flatten_to_table = true}));

  GOOGLESQL_ASSIGN_OR_RETURN(*output_name_list,
                   NameList::AddRangeVariableInWrappingNameList(
                       alias, ast_location, new_name_list));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeOperatorList(
    absl::Span<const ASTPipeOperator* const> pipe_operator_list,
    const NameScope* outer_scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list,
    const Type* inferred_type_for_query, bool allow_terminal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(*current_name_list != nullptr);

  if (!pipe_operator_list.empty() &&
      !language().LanguageFeatureEnabled(FEATURE_PIPES)) {
    return MakeSqlErrorAt(pipe_operator_list.front())
           << "Pipe query syntax not supported";
  }

  // Handling CTEs created by pipe WITH:
  // Pipe WITH creates CTEs which stay in scope until the end of the subquery
  // (or subpipeline), which is also the end of the pipe operator list.
  //
  // In ResolvedWithScan, the CTEs created are visible only within the
  // main query inside that scan node.  Therefore, we need the WithScan to
  // be outside the later pipe operator scans, meaning we need to reorder
  // scan nodes rather than creating them incrementally in syntax order.
  // This is done by having ResolvePipeWith add a pending node in
  // `with_scans_to_add` rather than directly onto `current_scan`.
  // Then these are added at the end, after (outside) all other pipe operators.
  //
  // Using the SQLBuilder on this, it will appear that all pipe WITH operators
  // have been moved to the beginning of the pipe operator sequence.  This is
  // okay because WITH definitions use standalone queries that cannot read
  // or modify their input query.
  //
  // This vector has the list of pending ResolvedWithScans.
  // They have placeholder values for the column list and input query.
  std::vector<std::unique_ptr<ResolvedWithScan>> with_scans_to_add;

  // Track the CTE names added by pipe operators.  They'll be unregistered
  // at the end of this method after the last pipe operator, which is the
  // end of this subquery or subpipeline.  This list can have duplicates if the
  // same name was defined multiple times.  It'll be removed multiple times.
  std::vector<IdString> named_subqueries_added;

  // After a terminal pipe operator, this stores the name of that operator,
  // which will be used in the error if there's a next operator.
  const char* terminal_operator_name = nullptr;

  for (const ASTPipeOperator* pipe_operator : pipe_operator_list) {
    // We'll infer the type for the last pipe operator only.
    // TODO: We could could push this to earlier pipe operators if later
    // ones like WHERE don't change output.
    const Type* inferred_type_for_pipe = nullptr;
    if (pipe_operator == pipe_operator_list.back()) {
      inferred_type_for_pipe = inferred_type_for_query;
    }

    if (terminal_operator_name != nullptr) {
      return MakeSqlErrorAt(pipe_operator)
             << "Additional pipe operators cannot follow the terminal "
                "pipe operator "
             << terminal_operator_name;
    }

    GOOGLESQL_RET_CHECK(*current_name_list != nullptr);
    NameScope current_scope(outer_scope, *current_name_list);

    switch (pipe_operator->node_kind()) {
      case AST_PIPE_WHERE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeWhere(pipe_operator->GetAs<ASTPipeWhere>(),
                                         &current_scope, current_scan));
        break;
      case AST_PIPE_LIMIT_OFFSET:
        GOOGLESQL_RETURN_IF_ERROR(
            ResolvePipeLimitOffset(pipe_operator->GetAs<ASTPipeLimitOffset>(),
                                   &current_scope, current_scan));
        break;
      case AST_PIPE_SELECT:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeSelect(
            pipe_operator->GetAs<ASTPipeSelect>(), &current_scope, current_scan,
            current_name_list, inferred_type_for_pipe));
        break;
      case AST_PIPE_EXTEND:
        // There is currently no type inference for columns in PipeExtend.
        // Supporting it would require matching newly added columns against
        // some expected type.
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeExtend(pipe_operator->GetAs<ASTPipeExtend>(),
                                          &current_scope, current_scan,
                                          current_name_list));
        break;
      case AST_PIPE_AGGREGATE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeAggregate(
            pipe_operator->GetAs<ASTPipeAggregate>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_ORDER_BY:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeOrderBy(
            pipe_operator->GetAs<ASTPipeOrderBy>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_CALL:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeCall(pipe_operator->GetAs<ASTPipeCall>(),
                                        outer_scope, &current_scope,
                                        current_scan, current_name_list));
        break;
      case AST_PIPE_WINDOW:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeWindow(pipe_operator->GetAs<ASTPipeWindow>(),
                                          &current_scope, current_scan,
                                          current_name_list));
        break;
      case AST_PIPE_JOIN:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeJoin(pipe_operator->GetAs<ASTPipeJoin>(),
                                        outer_scope, &current_scope,
                                        current_scan, current_name_list));
        break;
      case AST_PIPE_TABLESAMPLE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeTablesample(
            pipe_operator->GetAs<ASTPipeTablesample>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_MATCH_RECOGNIZE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeMatchRecognize(
            pipe_operator->GetAs<ASTPipeMatchRecognize>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_SET_OPERATION:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeSetOperation(
            pipe_operator->GetAs<ASTPipeSetOperation>(), outer_scope,
            current_scan, current_name_list, inferred_type_for_pipe));
        break;
      case AST_PIPE_DISTINCT:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeDistinct(
            pipe_operator->GetAsOrNull<ASTPipeDistinct>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_AS:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeAs(pipe_operator->GetAs<ASTPipeAs>(),
                                      &current_scope, current_scan,
                                      current_name_list));
        break;
      case AST_PIPE_DESCRIBE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeDescribe(
            pipe_operator->GetAs<ASTPipeDescribe>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_STATIC_DESCRIBE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeStaticDescribe(
            pipe_operator->GetAs<ASTPipeStaticDescribe>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_ASSERT:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeAssert(pipe_operator->GetAs<ASTPipeAssert>(),
                                          &current_scope, current_scan,
                                          *current_name_list));
        break;
      case AST_PIPE_LOG:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeLog(pipe_operator->GetAs<ASTPipeLog>(),
                                       outer_scope, &current_scope,
                                       current_scan, current_name_list));
        break;
      case AST_PIPE_DROP:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeDrop(pipe_operator->GetAs<ASTPipeDrop>(),
                                        &current_scope, current_scan,
                                        current_name_list));
        break;
      case AST_PIPE_RENAME:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeRename(pipe_operator->GetAs<ASTPipeRename>(),
                                          &current_scope, current_scan,
                                          current_name_list));
        break;
      case AST_PIPE_SET:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeSet(pipe_operator->GetAs<ASTPipeSet>(),
                                       &current_scope, current_scan,
                                       current_name_list));
        break;
      case AST_PIPE_PIVOT:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipePivot(pipe_operator->GetAs<ASTPipePivot>(),
                                         &current_scope, current_scan,
                                         current_name_list));
        break;
      case AST_PIPE_UNPIVOT:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeUnpivot(
            pipe_operator->GetAs<ASTPipeUnpivot>(), &current_scope,
            current_scan, current_name_list));
        break;
      case AST_PIPE_IF:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeIf(pipe_operator->GetAs<ASTPipeIf>(),
                                      outer_scope, &current_scope, current_scan,
                                      current_name_list, allow_terminal));
        if (*current_name_list == nullptr) {
          terminal_operator_name = "IF (with terminal operators inside)";
        }
        break;
      case AST_PIPE_FORK:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeFork(
            pipe_operator->GetAs<ASTPipeFork>(), outer_scope, &current_scope,
            current_scan, current_name_list, allow_terminal));
        terminal_operator_name = "FORK";
        break;
      case AST_PIPE_TEE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeTee(
            pipe_operator->GetAs<ASTPipeTee>(), outer_scope, &current_scope,
            current_scan, current_name_list, allow_terminal));
        // TEE is not a terminal operator.
        break;
      case AST_PIPE_WITH:
        // This adds a scan to `with_scans_to_add` rather than modifying
        // `current_scan`.  See comments about WITH above and on the method.
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeWith(pipe_operator->GetAs<ASTPipeWith>(),
                                        &named_subqueries_added,
                                        &with_scans_to_add));
        break;
      case AST_PIPE_EXPORT_DATA:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeExportData(
            pipe_operator->GetAs<ASTPipeExportData>(), outer_scope,
            &current_scope, current_scan, current_name_list, allow_terminal));
        terminal_operator_name = "EXPORT DATA";
        break;
      case AST_PIPE_CREATE_TABLE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeCreateTable(
            pipe_operator->GetAs<ASTPipeCreateTable>(), &current_scope,
            current_scan, current_name_list, allow_terminal));
        terminal_operator_name = "CREATE TABLE";
        break;
      case AST_PIPE_INSERT:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeInsert(
            pipe_operator->GetAs<ASTPipeInsert>(), outer_scope, &current_scope,
            current_scan, current_name_list, allow_terminal));
        terminal_operator_name = "INSERT";
        break;
      case AST_PIPE_RECURSIVE_UNION:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePipeRecursiveUnion(
            pipe_operator->GetAsOrDie<ASTPipeRecursiveUnion>(), current_scan,
            current_name_list));
        break;
      default:
        // When all operators are fully implemented, this can become
        // a GOOGLESQL_RET_CHECK.
        return MakeSqlErrorAt(pipe_operator)
               << "Unsupported pipe operator: "
               << pipe_operator->GetNodeKindString();
    }

    // Terminal operators don't return a NameList because they don't output
    // a table.
    GOOGLESQL_RET_CHECK_EQ(terminal_operator_name != nullptr,
                 *current_name_list == nullptr);
  }

  // Now add the ResolvedWithScans at the end.
  // They have to be added in reverse order of creation, since the first
  // WithScan creates CTEs that are visible to the second (which will be in
  // its child `query` field), etc.
  for (auto it = with_scans_to_add.rbegin(); it != with_scans_to_add.rend();
       ++it) {
    std::unique_ptr<ResolvedWithScan>& scan = *it;
    scan->set_column_list((*current_scan)->column_list());
    scan->set_query(std::move(*current_scan));
    *current_scan = std::move(scan);
  }

  // Any CTEs created by the pipe operators now go out of scope.
  for (IdString cte_name : named_subqueries_added) {
    GOOGLESQL_RETURN_IF_ERROR(RemoveInnermostNamedSubqueryWithAlias(cte_name));
  }

  if (*current_name_list == nullptr) {
    GOOGLESQL_RET_CHECK(allow_terminal);
  }
  if (terminal_operator_name != nullptr) {
    needs_generalized_query_stmt_ = true;
  }

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeWhere(
    const ASTPipeWhere* where, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:WHERE");

  std::unique_ptr<const ResolvedExpr> resolved_where;
  // Some error cases require different leading capitals on these strings.
  static constexpr char kPipeWhereClause[] = "pipe WHERE clause";
  static constexpr char kWhereClause[] = "WHERE clause";
  const ASTExpression* ast_expr = where->where()->expression();

  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
  query_resolution_info->analytic_resolver()->DisableNamedWindowRefs(
      kPipeWhereClause);
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info.get(), scope,
      ExprResolutionInfoOptions{.allows_aggregation = false,
                                .allows_analytic = true,
                                .clause_name = kPipeWhereClause});

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpr(ast_expr, expr_resolution_info.get(), &resolved_where));

  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(ast_expr, kWhereClause, &resolved_where));

  GOOGLESQL_RET_CHECK(!query_resolution_info->HasAggregation());
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->CheckComputedColumnListsAreEmpty());
  if (query_resolution_info->HasAnalytic()) {
    // Add an AnalyticScan if any window functions were present.
    // This also makes a ProjectScan if necessary before the AnalyticScan.
    GOOGLESQL_RETURN_IF_ERROR(AddAnalyticScan(query_resolution_info.get(), current_scan));
  }

  const auto& tmp_column_list = (*current_scan)->column_list();
  *current_scan = MakeResolvedFilterScan(
      tmp_column_list, std::move(*current_scan), std::move(resolved_where));
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeLimitOffset(
    const ASTPipeLimitOffset* limit_offset, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:LIMIT");
  return ResolveLimitOffsetScan(limit_offset->limit_offset(), scope,
                                current_scan);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeTablesample(
    const ASTPipeTablesample* tablesample, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:TABLESAMPLE");
  // This takes NameList rather than NameScope.  Mostly it does not resolve
  // expressions, although PARTITION BY is an exception, and correlated
  // references won't work.
  // It may add a weight column into `current_name_list`.
  return ResolveTablesampleClause(tablesample->sample(), current_name_list,
                                  current_scan);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeSelect(
    const ASTPipeSelect* pipe_select, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list,
    const Type* inferred_type_for_pipe) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:SELECT");
  const ASTSelect* select = pipe_select->select();

  if (select->with_modifier() != nullptr) {
    return MakeSqlErrorAt(select->with_modifier())
           << "Pipe SELECT does not support SELECT WITH";
  }
  if (select->hint() != nullptr) {
    // The reason hints aren't supported on pipe SELECT yet is because these
    // hints would show up on ResolvedProjectScan, which in normal syntax,
    // indicates a hint that applies to an entire query.  The semantics seem
    // different for the hint here, but we haven't indicated how yet.
    return MakeSqlErrorAt(select->hint())
           << "Pipe SELECT does not support hints yet";
  }
  if (select->window_clause() != nullptr &&
      !language().LanguageFeatureEnabled(FEATURE_PIPE_NAMED_WINDOWS)) {
    return MakeSqlErrorAt(select->window_clause())
           << "Pipe SELECT does not support the WINDOW clause";
  }

  // By construction in the parser, the ASTSelect shouldn't have a FROM clause
  // or any other clauses after the SELECT list other than a WINDOW clause.
  // The is_distinct modifier is allowed, but doesn't have an AST node.
  GOOGLESQL_RETURN_IF_ERROR(CheckForUnwantedSelectClauseChildNodes(
      select,
      {select->select_list(), select->select_as(), select->hint(),
       select->window_clause()},
      "Pipe SELECT"));
  // Repeating this, to assert we are resolving this like a no-FROM-clause
  // SELECT, which will block grouping and many other operations.
  GOOGLESQL_RET_CHECK(select->from_clause() == nullptr);
  GOOGLESQL_RET_CHECK(select->select_list() != nullptr);

  const ResolvedScan* original_input_scan = current_scan->get();
  const IdString query_alias = MakeIdString("$pipe_select");

  std::shared_ptr<const NameList> new_name_list;

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectAfterFrom(
      select, /*order_by=*/nullptr,
      /*limit_offset=*/nullptr, scope, query_alias, SelectForm::kPipeSelect,
      WithModifierMode::NONE,
      /*force_new_columns_for_projected_outputs=*/false, inferred_type_for_pipe,
      current_scan, *current_name_list, &new_name_list));

  *current_name_list = std::move(new_name_list);

  // Check that we added ResolvedProjectScans, a ResolvedAggregateScan if we
  // had DISTINCT, possibly a ResolvedAnalyticScan, and no other scans.
  GOOGLESQL_RETURN_IF_ERROR(CheckForExpectedScans(
      current_scan->get(), original_input_scan,
      select->distinct() ? RESOLVED_AGGREGATE_SCAN : RESOLVED_PROJECT_SCAN,
      "Pipe SELECT",
      /*allowed_extra_scan_types=*/
      {RESOLVED_PROJECT_SCAN, RESOLVED_ANALYTIC_SCAN}));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeExtend(
    const ASTPipeExtend* extend, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:EXTEND");
  const ASTSelect* select = extend->select();

  if (select->window_clause() != nullptr &&
      !language().LanguageFeatureEnabled(FEATURE_PIPE_NAMED_WINDOWS)) {
    return MakeSqlErrorAt(select->window_clause())
           << "Pipe EXTEND does not support the WINDOW clause";
  }

  // By construction in the parser, the ASTSelect shouldn't have anything
  // other than the SELECT list and optionally a WINDOW clause.
  GOOGLESQL_RETURN_IF_ERROR(CheckForUnwantedSelectClauseChildNodes(
      select, {select->select_list(), select->window_clause()}, "Pipe EXTEND"));
  GOOGLESQL_RET_CHECK_NE(select->select_list(), nullptr);
  GOOGLESQL_RET_CHECK(!select->distinct());

  const ResolvedScan* original_input_scan = current_scan->get();
  const IdString query_alias = MakeIdString("$pipe_extend");

  std::shared_ptr<const NameList> new_name_list;

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveSelectAfterFrom(select, /*order_by=*/nullptr,
                             /*limit_offset=*/nullptr, scope, query_alias,
                             SelectForm::kPipeExtend, WithModifierMode::NONE,
                             /*force_new_columns_for_projected_outputs=*/false,
                             /*inferred_type_for_query=*/nullptr, current_scan,
                             *current_name_list, &new_name_list));

  // We could get ProjectScans or AnalyticScans, but we're not required
  // to get either of them.
  GOOGLESQL_RETURN_IF_ERROR(
      CheckForExpectedScans(current_scan->get(), original_input_scan,
                            /*expected_scan_kind=*/std::nullopt, "Pipe EXTEND",
                            /*allowed_extra_scan_types=*/
                            {RESOLVED_ANALYTIC_SCAN, RESOLVED_PROJECT_SCAN}));

  *current_name_list = std::move(new_name_list);
  return absl::OkStatus();
}

// Give an error if both aggregate list and group by list are empty.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
static absl::Status CheckForEmptyPipeAggregate(const ASTSelect* select) {
  if (select->select_list()->columns().empty()) {
    bool group_by_is_empty = true;
    if (select->group_by() != nullptr) {
      for (const ASTGroupingItem* item : select->group_by()->grouping_items()) {
        // To detect "()", we have to look for the absence of the other
        // possible field types.
        if (item->expression() != nullptr || item->rollup() != nullptr ||
            item->cube() != nullptr || item->grouping_set_list() != nullptr) {
          group_by_is_empty = false;
          break;
        }
      }
    }
    if (group_by_is_empty) {
      return MakeSqlErrorAt(select)
             << "Pipe AGGREGATE cannot have both an empty aggregate list "
                "and an empty GROUP BY";
    }
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeAggregate(
    const ASTPipeAggregate* aggregate, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  WithModifierMode with_modifier_mode = WithModifierMode::NONE;
  if (aggregate->with_modifier() != nullptr) {
    GOOGLESQL_RET_CHECK_NE(aggregate->with_modifier()->identifier(), nullptr);
    absl::string_view with_modifier_mode_string =
        aggregate->with_modifier()->identifier()->GetAsStringView();
    if (!googlesql_base::CaseEqual(
            with_modifier_mode_string,
            WithModifierModeToString(WithModifierMode::DIFFERENTIAL_PRIVACY)) ||
        !language().LanguageFeatureEnabled(
            FEATURE_PIPE_AGGREGATE_WITH_DIFFERENTIAL_PRIVACY)) {
      return MakeSqlErrorAt(aggregate->with_modifier())
             << "AGGREGATE WITH "
             << absl::AsciiStrToUpper(with_modifier_mode_string)
             << " is not supported";
    }
    with_modifier_mode = WithModifierMode::DIFFERENTIAL_PRIVACY;
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:AGGREGATE");
  const ASTSelect* select = aggregate->select();

  // By construction in the parser, this ASTSelect should only have a
  // SELECT list and possibly a GROUP BY.
  GOOGLESQL_RETURN_IF_ERROR(CheckForUnwantedSelectClauseChildNodes(
      select, {select->select_list(), select->group_by()}, "Pipe AGGREGATE"));
  GOOGLESQL_RET_CHECK_NE(select->select_list(), nullptr);

  const ASTGroupBy* group_by = select->group_by();
  if (group_by != nullptr && group_by->all()) {
    // Currently unreachable because the parser doesn't let this through,
    // but that was just an artifact of how some conflicts were resolved.
    return MakeSqlErrorAt(select)
           << "Pipe AGGREGATE does not support GROUP BY ALL";
  }

  // Give an error if both aggregate list and group by list are empty.
  GOOGLESQL_RETURN_IF_ERROR(CheckForEmptyPipeAggregate(select));

  const ResolvedScan* original_input_scan = current_scan->get();
  const IdString query_alias = MakeIdString("$aggregate");

  std::shared_ptr<const NameList> new_name_list;

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveSelectAfterFrom(select, /*order_by=*/nullptr,
                             /*limit_offset=*/nullptr, scope, query_alias,
                             SelectForm::kPipeAggregate, with_modifier_mode,
                             /*force_new_columns_for_projected_outputs=*/false,
                             /*inferred_type_for_query=*/nullptr, current_scan,
                             *current_name_list, &new_name_list));

  *current_name_list = std::move(new_name_list);

  // Check that we added one AggregateScan, maybe some ProjectScans, maybe
  // an OrderByScan, and nothing else.
  GOOGLESQL_RETURN_IF_ERROR(CheckForExpectedScans(
      current_scan->get(), original_input_scan,
      with_modifier_mode == WithModifierMode::DIFFERENTIAL_PRIVACY
          ? RESOLVED_DIFFERENTIAL_PRIVACY_AGGREGATE_SCAN
          : RESOLVED_AGGREGATE_SCAN,
      "Pipe AGGREGATE",
      /*allowed_extra_scan_types=*/
      {RESOLVED_PROJECT_SCAN, RESOLVED_ORDER_BY_SCAN}));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeDistinct(
    const ASTPipeDistinct* distinct, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:DISTINCT");
  GOOGLESQL_RET_CHECK(distinct != nullptr);

  const ResolvedScan* original_input_scan = current_scan->get();
  // Use a map distinct_expr_map to filter out the duplicated columns in the
  // group_by_list of the aggregate_scan.
  DistinctExprMap distinct_expr_map;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      distinct_columns_to_compute;
  auto new_name_list = std::make_shared<NameList>();

  // A map of the input column_id to the output column. This map will be used
  // for calling CloneRangeVariablesFromNameScopeMapped() below.
  absl::flat_hash_map<int, ResolvedColumn> column_map;
  // When adding a value-table column to new_name_list, a corresponding range
  // variable will be created. So when we clone the range variables from the old
  // name scope, we need to exclude them to avoid collision.
  // This set is used for remembering the value-table range variables that have
  // already been added to new_name_list.
  IdStringHashSetCase excluded_range_variable_set;

  // Loop through the NameList's columns rather than the Scan's column_list to
  // avoid pseudo-columns.
  for (int column_pos = 0; column_pos < (*current_name_list)->num_columns();
       ++column_pos) {
    const NamedColumn& named_column = (*current_name_list)->column(column_pos);
    const ResolvedColumn& column = named_column.column();

    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedComputedColumn* resolved_computed_column,
                     AddPipeDistinctColumn(
                         distinct, column_pos, named_column.name(), column,
                         &distinct_expr_map, &distinct_columns_to_compute));
    column_map.insert({column.column_id(), resolved_computed_column->column()});

    if (named_column.is_value_table_column()) {
      GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddValueTableColumn(
          named_column.name(), resolved_computed_column->column(), distinct,
          named_column.excluded_field_names()));
      excluded_range_variable_set.insert(named_column.name());
    } else {
      GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddColumn(
          named_column.name(), resolved_computed_column->column(),
          named_column.is_explicit()));
    }
  }

  ResolvedColumnList distinct_column_list;
  distinct_column_list.reserve(distinct_columns_to_compute.size());
  for (const std::unique_ptr<const ResolvedComputedColumn>& distinct_column :
       distinct_columns_to_compute) {
    distinct_column_list.push_back(distinct_column->column());
  }
  GOOGLESQL_RET_CHECK(!distinct_column_list.empty());
  std::unique_ptr<ResolvedAggregateScan> aggregate_scan =
      MakeResolvedAggregateScan(distinct_column_list, std::move(*current_scan),
                                std::move(distinct_columns_to_compute),
                                /*aggregate_list=*/{},
                                /*grouping_set_list=*/{},
                                /*rollup_column_list=*/{},
                                /*grouping_call_list=*/{});
  *current_scan = std::move(aggregate_scan);

  // Any columns used for computing DISTINCT count as referenced and cannot be
  // pruned.
  RecordColumnAccess((*current_scan)->column_list());

  // To make range variables from inner scans visible after the distinct
  // aggregation, we need create new range variables for the aggregated output
  // columns with the same names in the inner scans.
  //
  // For example, for query
  //   FROM table t | DISTINCT | SELECT t...
  // If the FROM-clause outputs t.[a#1, b#2, c#3], then after DISTINCT, it would
  // generate new columns [a#4, b#5, c#6]. Then to make "t" visible in the
  // follow up SELECT-clause, we need to create a new range variable "t" for
  // the DISTINCT output columns, like this: t.[a#4, b#5, c#6].
  //
  // There is an interesting case with JOIN USING.  There is only one column
  // in the outer NameList, but two different columns in the two range variable
  // NameLists for each join side.  Only one of those matches the outer
  // column.  (For FULL JOIN, neither of them match.)
  //
  // This generates the post-DISTINCT scope so that only the range variable
  // columns that match the outer are included and others are dropped.
  //
  // It is correct to drop the others since they their values are no longer
  // available after DISTINCT, even for inner joins (e.g. for case insensitive
  // strings).
  //
  // Example of the case:
  //   FROM KeyValue t1 JOIN KeyValue t2 USING(Key)
  //   | DISTINCT
  //   | SELECT t1.*, t2.*
  // This gives 3 output columns from the SELECT: [t1.Key, t1.Value, t2.Value].
  // It may seem odd that t2.Key is excluded but t1.Key is not.
  // An alternate behavior would be to exclude Key from both t1 and t2, so the
  // output columns would be: [Key, t1.Value, t2.Value].
  GOOGLESQL_RETURN_IF_ERROR(new_name_list->CloneRangeVariablesFromNameScopeMapped(
      *scope, column_map, *distinct, excluded_range_variable_set));

  if ((*current_name_list)->is_value_table()) {
    // Still output as value-table if the whole input table was a value table.
    GOOGLESQL_RETURN_IF_ERROR(new_name_list->SetIsValueTable());
  }

  *current_name_list = std::move(new_name_list);

  // Check that we added one AggregateScan, and nothing else.
  GOOGLESQL_RETURN_IF_ERROR(CheckForExpectedScans(
      current_scan->get(), original_input_scan, RESOLVED_AGGREGATE_SCAN,
      "Pipe DISTINCT",
      /*allowed_extra_scan_types=*/{RESOLVED_PROJECT_SCAN}));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<const ResolvedComputedColumn*> Resolver::AddPipeDistinctColumn(
    const ASTPipeDistinct* distinct, int column_pos, IdString name,
    const ResolvedColumn& column, DistinctExprMap* distinct_expr_map,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        distinct_columns_to_compute) {
  std::string no_grouping_type;
  if (!column.type()->SupportsGrouping(language(), &no_grouping_type)) {
    return MakeSqlErrorAt(distinct)
           << "Column " << ColumnAliasOrPosition(name, column_pos)
           << " of type " << no_grouping_type
           << " is not supported in pipe DISTINCT";
  }

  // Add a computed column for the new post-DISTINCT column if needed.
  std::unique_ptr<ResolvedColumnRef> expr = MakeColumnRef(column);
  const ResolvedComputedColumn*& stored_column =
      (*distinct_expr_map)[expr.get()];
  if (stored_column != nullptr) {
    return stored_column;
  }
  // Create a new DISTINCT column.
  ResolvedColumn distinct_column = ResolvedColumn(
      AllocateColumnId(), kPipeDistinctId, name, column.annotated_type());
  auto new_column =
      MakeResolvedComputedColumn(distinct_column, std::move(expr));
  stored_column = new_column.get();
  distinct_columns_to_compute->push_back(std::move(new_column));
  return stored_column;
}

// current_name_list is passed as a shared_ptr for symmetry with other
// ResolvePipe* methods, but ORDER BY never changes it.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeOrderBy(
    const ASTPipeOrderBy* order_by, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:ORDER_BY");
  return ResolveOrderBySimple(order_by->order_by(), **current_name_list, scope,
                              "pipe ORDER BY clause", OrderBySimpleMode::kPipes,
                              current_scan);
}

// ResolvePipeCall takes `outer_scope` because TVF arguments cannot
// reference columns from the input table, which are not constant over
// the invocation of this TVF call.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeCall(
    const ASTPipeCall* call, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:CALL");
  const ASTTVF* tvf = call->tvf();

  // The input scan becomes the first table-typed TVF argument, or the
  // INPUT TABLE argument.
  ResolvedTVFArg table_arg;
  table_arg.SetScan(std::move(*current_scan), *current_name_list,
                    /*is_pipe_input_table=*/true);

  std::shared_ptr<const NameList> new_name_list;

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveTVF(tvf, outer_scope, &table_arg, current_scan, &new_name_list));

  *current_name_list = new_name_list;

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeWindow(
    const ASTPipeWindow* window, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  // Pipe WINDOW was added before EXTEND supported window functions.
  // Now that EXTEND supports window functions, WINDOW's functionality
  // is a strict subset of EXTEND, making it unnecessary, and confusing
  // users about why it exists.
  GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
      window, DeprecationWarning::PIPE_WINDOW,
      "The pipe WINDOW operator is deprecated; Use EXTEND instead"));
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:WINDOW");

  const ASTSelect* select = window->select();

  // By construction in the parser, the ASTSelect shouldn't have anything
  // other than the SELECT list.
  GOOGLESQL_RETURN_IF_ERROR(CheckForUnwantedSelectClauseChildNodes(
      select, {select->select_list()}, "Pipe WINDOW"));
  GOOGLESQL_RET_CHECK(select->select_list() != nullptr);
  GOOGLESQL_RET_CHECK(!select->distinct());

  const ResolvedScan* original_input_scan = current_scan->get();
  const IdString query_alias = MakeIdString("$pipe_window");

  std::shared_ptr<const NameList> new_name_list;

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveSelectAfterFrom(select, /*order_by=*/nullptr,
                             /*limit_offset=*/nullptr, scope, query_alias,
                             SelectForm::kPipeWindow, WithModifierMode::NONE,
                             /*force_new_columns_for_projected_outputs=*/false,
                             /*inferred_type_for_query=*/nullptr, current_scan,
                             *current_name_list, &new_name_list));

  // Check that we added a ResolvedAnalyticScan, maybe some
  // ResolvedProjectScans, and no other scan kinds.
  GOOGLESQL_RETURN_IF_ERROR(CheckForExpectedScans(
      current_scan->get(), original_input_scan, RESOLVED_ANALYTIC_SCAN,
      "Pipe WINDOW",
      /*allowed_extra_scan_types=*/{RESOLVED_PROJECT_SCAN}));

  *current_name_list = std::move(new_name_list);

  return absl::OkStatus();
}

static bool IsLateral(const ASTTableExpression* table_expr) {
  switch (table_expr->node_kind()) {
    case AST_TVF:
      return table_expr->GetAsOrDie<ASTTVF>()->is_lateral();
    case AST_TABLE_SUBQUERY:
      return table_expr->GetAsOrDie<ASTTableSubquery>()->is_lateral();
    default:
      return false;
  }
}

// ResolvePipeJoin takes `outer_scope`, which most other pipe operators don't.
// This is necessary because TVF input arguments and parenthesized joins on
// the RHS of a JOIN cannot see items from the join LHS, and can only see
// correlated references to outer queries.  The pipe input acts as the join LHS
// here, so we don't want those cases to see names from the pipe input.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeJoin(
    const ASTPipeJoin* join, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:JOIN");
  const ASTJoin* ast_join = join->join();
  GOOGLESQL_RET_CHECK(ast_join->lhs() != nullptr);
  GOOGLESQL_RET_CHECK(!IsLateral(ast_join->lhs()));
  GOOGLESQL_RET_CHECK_EQ(ast_join->lhs()->node_kind(), AST_PIPE_JOIN_LHS_PLACEHOLDER);
  GOOGLESQL_RET_CHECK(!IsLateral(ast_join->lhs()));

  GOOGLESQL_RETURN_IF_ERROR(ResolveJoinRhs(ast_join, outer_scope, scope,
                                 *current_name_list, std::move(*current_scan),
                                 /*on_rhs_of_right_or_full_join=*/false,
                                 current_scan, current_name_list));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeAs(
    const ASTPipeAs* pipe_as, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:AS");
  IdString alias = pipe_as->alias()->GetAsIdString();
  auto output_name_list = std::make_shared<const NameList>();

  GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(
      pipe_as, alias, *current_name_list, &output_name_list));
  *current_name_list = output_name_list;

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeAssert(
    const ASTPipeAssert* pipe_assert, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList> current_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_ASSERT)) {
    return MakeSqlErrorAt(pipe_assert) << "Pipe ASSERT not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:ASSERT");

  std::unique_ptr<const ResolvedExpr> condition_expr;
  static constexpr char kAssertClause[] = "ASSERT condition";
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(pipe_assert->condition(), scope,
                                    kAssertClause, &condition_expr));
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(pipe_assert->condition(), kAssertClause,
                                   &condition_expr));

  std::vector<std::unique_ptr<const ResolvedExpr>> message_expr_list;
  message_expr_list.reserve(pipe_assert->message_list().size());

  for (const ASTExpression* ast_message : pipe_assert->message_list()) {
    std::unique_ptr<const ResolvedExpr> expr;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveScalarExpr(ast_message, scope, kAssertClause, &expr));

    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        ast_message, type_factory_->get_string(), kExplicitCoercion,
        "ASSERT message value has type $1 which cannot be coerced to $0",
        &expr));

    auto expr_resolution_info =
        std::make_unique<ExprResolutionInfo>(scope, "pipe ASSERT");

    // Wrap the message in an IFNULL(<message>, "NULL") function call so that if
    // a payload is NULL, it is printed as "NULL".
    //
    // We do not add the function call for non-NULL literals because it is
    // unnecessary and non-null literals as payload can be common.
    if (!expr->Is<ResolvedLiteral>() ||
        expr->GetAs<ResolvedLiteral>()->value().is_null()) {
      std::unique_ptr<const ResolvedExpr> ifnull_expr;
      std::vector<std::unique_ptr<const ResolvedExpr>> ifnull_args;
      ifnull_args.push_back(std::move(expr));
      ifnull_args.push_back(MakeResolvedLiteral(/*ast_location=*/nullptr,
                                                Value::StringValue("NULL")));
      GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
          pipe_assert, /*arg_locations=*/{ast_message, pipe_assert},
          /*match_internal_signatures=*/false, /*function_name=*/"IFNULL",
          std::move(ifnull_args), /*named_arguments=*/{},
          expr_resolution_info.get(), &ifnull_expr));
      expr = std::move(ifnull_expr);
    }
    message_expr_list.push_back(std::move(expr));
  }

  std::unique_ptr<const ResolvedExpr> final_message_expr;
  if (message_expr_list.empty()) {
    // If there's no payload, construct a literal string message using
    // the query text of the condition expression.
    GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view substr,
                     GetSQLForASTNode(pipe_assert->condition()));

    final_message_expr = MakeResolvedLiteral(
        /*ast_location=*/nullptr, Value::StringValue(std::string(substr)));
  } else if (message_expr_list.size() == 1) {
    // The message is the single payload argument.
    final_message_expr = std::move(message_expr_list[0]);
  } else {
    // Construct a CONCAT call to build the error message, with spaces.
    std::vector<std::unique_ptr<const ResolvedExpr>> concat_args;
    std::vector<const ASTNode*> arg_locations;

    for (int i = 0; i < message_expr_list.size(); ++i) {
      if (i > 0) {
        concat_args.push_back(MakeResolvedLiteral(/*ast_location=*/nullptr,
                                                  Value::StringValue(" ")));
        arg_locations.push_back(pipe_assert);
      }

      concat_args.push_back(std::move(message_expr_list[i]));
      arg_locations.push_back(pipe_assert->message_list(i));
    }

    auto expr_resolution_info =
        std::make_unique<ExprResolutionInfo>(scope, "pipe ASSERT");

    GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
        pipe_assert, arg_locations, /*match_internal_signatures=*/false,
        "CONCAT", std::move(concat_args),
        /*named_arguments=*/{}, expr_resolution_info.get(),
        &final_message_expr));
  }

  GOOGLESQL_RET_CHECK(final_message_expr != nullptr);
  GOOGLESQL_RET_CHECK(final_message_expr->type()->IsString());

  ResolvedColumnList column_list = (*current_scan)->column_list();
  *current_scan = MakeResolvedAssertScan(column_list, std::move(*current_scan),
                                         std::move(condition_expr),
                                         std::move(final_message_expr));

  GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(NameListPtr(current_name_list),
                                             pipe_assert, "pipe ASSERT",
                                             product_mode()));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeLog(
    const ASTPipeLog* pipe_log, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_LOG)) {
    return MakeSqlErrorAt(pipe_log)
           << "Pipe LOG not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:LOG");

  ResolvedColumnList input_column_list = (*current_scan)->column_list();
  std::shared_ptr<const NameList> inside_name_list =
      (*current_name_list)->CopyWithIsValueTable();

  GOOGLESQL_ASSIGN_OR_RETURN(auto subpipeline,
                   ResolveSubpipeline(
                       pipe_log->subpipeline(), outer_scope, input_column_list,
                       (*current_scan)->is_ordered(), &inside_name_list));

  auto log_scan = MakeResolvedLogScan(
      input_column_list, std::move(*current_scan), std::move(subpipeline),
      MakeOutputSchema(*inside_name_list));

  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnListIsReturnable(
      log_scan->output_schema()->output_column_list(), pipe_log));

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(pipe_log->hint(), log_scan.get()));

  *current_scan = std::move(log_scan);
  return absl::OkStatus();
}

std::string Resolver::GetDescribeString(const ResolvedScan* current_scan,
                                        const NameList* current_name_list,
                                        bool internal_debug_string) {
  reflection::ResultTable result_table =
      current_name_list->Describe(product_mode());
  // `is_ordered` comes from the input scan, not the NameList.
  if (current_scan->is_ordered()) {
    result_table.set_is_ordered(true);
  }

  // Add visible CTEs into `result_table`.
  for (const auto& item : named_subquery_map_) {
    // The map entry is a vector, where the last entry (if non-NULL) is the
    // currently visible CTE.
    if (item.second.empty()) continue;
    const NamedSubquery* named_subquery = item.second.back().get();
    if (named_subquery == nullptr) continue;

    // This uses `back()` because the key is a path.  Multi-part names can only
    // occur inside CREATE RECURSIVE VIEW, which we're ignoring here.
    IdString cte_name = item.first.back();

    // Skip internally generated CTEs.  This occurs in pipe RECURSIVE UNION,
    // for example.
    if (IsInternalAlias(cte_name)) continue;

    reflection::TableAlias* table_alias =
        result_table.add_common_table_expression();
    table_alias->set_name(cte_name.ToString());
    // Exclude pseudo-columns.  The CTE NameList might have copied them,
    // but they are discarded when the CTE is referenced.
    named_subquery->name_list->DescribeInto(table_alias,
                                            /*include_pseudo_columns=*/false);
  }
  // Sort the list to make output deterministic.
  std::sort(
      result_table.mutable_common_table_expression()->pointer_begin(),
      result_table.mutable_common_table_expression()->pointer_end(),
      [](const reflection::TableAlias* a, const reflection::TableAlias* b) {
        return googlesql_base::CaseLess()(a->name(), b->name());
      });

  if (internal_debug_string) {
    // For STATIC_DESCRIBE, we still use NameList::DebugString for the table
    // schema, but append the additional info that DESCRIBE includes.
    // STATIC_DESCRIBE is just used internally in analyzer tests, and the
    // DebugString is a more faithful representation of NameList state.
    // This needs to change if STATIC_DESCRIBE is exposed in engines.
    return std::string(absl::StripTrailingAsciiWhitespace(absl::StrCat(
        current_name_list->DebugString(), "\n",
        reflection::FormatResultTable(result_table,
                                      /*include_table_schema=*/false))));
  } else {
    // Format the proto as a string value to return.
    return reflection::FormatResultTable(result_table);
  }
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeDescribe(
    const ASTPipeDescribe* pipe_describe, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_DESCRIBE)) {
    return MakeSqlErrorAt(pipe_describe) << "Pipe DESCRIBE not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:DESCRIBE");

  // Format DESCRIBE output as a string value to return.
  // TODO This feature might get extended so the ResultTable
  // proto can be returned directly, as structured output.
  googlesql::Value output_value = Value::String(
      GetDescribeString(current_scan->get(), current_name_list->get(),
                        /*internal_debug_string=*/false));

  const IdString scan_alias = MakeIdString("$pipe_describe");
  const IdString column_name = MakeIdString("Describe");
  ResolvedColumn column(AllocateColumnId(), scan_alias, column_name,
                        output_value.type());

  *current_scan = MakeResolvedDescribeScan(
      ResolvedColumnList({column}), std::move(*current_scan),
      MakeResolvedComputedColumn(
          column, MakeResolvedLiteral(/*ast_location=*/nullptr, output_value)));
  auto new_name_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddColumn(column_name, column,
                                           /*is_explicit=*/true));
  *current_name_list = std::move(new_name_list);

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeStaticDescribe(
    const ASTPipeStaticDescribe* pipe_static_describe, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_STATIC_DESCRIBE)) {
    return MakeSqlErrorAt(pipe_static_describe)
           << "Pipe STATIC_DESCRIBE not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:STATIC_DESCRIBE");

  const std::string output =
      GetDescribeString(current_scan->get(), current_name_list->get(),
                        /*internal_debug_string=*/true);

  // Make the no-op scan that holds the STATIC_DESCRIBE output.
  const ResolvedScan* input_scan = current_scan->get();
  *current_scan = MakeResolvedStaticDescribeScan(
      input_scan->column_list(), std::move(*current_scan), output);

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<Value> Resolver::GetConstantValue(
    const ASTExpression* ast_expr, const ResolvedExpr* resolved_expr,
    absl::string_view clause_name) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_ASSIGN_OR_RETURN(bool is_constant_expr, IsConstantExpression(resolved_expr));
  if (!is_constant_expr) {
    return MakeSqlErrorAt(ast_expr)
           << clause_name << " must be a constant expression";
  }

  // Only some limited cases are supported for getting a value so far.
  switch (resolved_expr->node_kind()) {
    case RESOLVED_LITERAL:
      return resolved_expr->GetAs<ResolvedLiteral>()->value();
    case RESOLVED_CONSTANT: {
      const Constant* constant =
          resolved_expr->GetAs<ResolvedConstant>()->constant();
      if (!constant->HasValue()) {
        return MakeSqlErrorAt(ast_expr)
               << "Constant " << constant->FullName() << " in "
               << FirstCharLower(clause_name)
               << " doesn't have a value available at analysis time";
      }
      return constant->GetValue();
    }
    // Support some specific function expressions (like NOT) as constants.
    // TODO This should go away when we have more general constant
    // expression evaluation.
    case RESOLVED_FUNCTION_CALL: {
      const ResolvedFunctionCall* call =
          resolved_expr->GetAs<ResolvedFunctionCall>();
      // Handle NOT expressions, inverting the boolean value.
      // Note we also get NOT FunctionCalls for some other expressions like
      // NOT LIKE or IS NOT NULL.
      if (call->function()->FullName() == "GoogleSQL:$not" &&
          ast_expr->node_kind() == AST_UNARY_EXPRESSION &&
          ast_expr->GetAs<ASTUnaryExpression>()->op() ==
              ASTUnaryExpression::NOT) {
        const ASTExpression* ast_arg =
            ast_expr->GetAs<ASTUnaryExpression>()->operand();

        GOOGLESQL_RET_CHECK_EQ(call->argument_list().size(), 1);
        const ResolvedExpr* resolved_arg = call->argument_list(0);
        GOOGLESQL_RET_CHECK(resolved_arg->type()->IsBool());

        GOOGLESQL_ASSIGN_OR_RETURN(const Value value,
                         GetConstantValue(ast_arg, resolved_arg, clause_name));
        if (value.is_null()) {
          return value;
        } else {
          return Value::Bool(!value.bool_value());
        }
      }
      break;
    }
    default:
      break;
  }

  return MakeSqlErrorAt(ast_expr)
         << clause_name
         << " must be evaluated to a constant value at analysis time; "
            "Only literals and constants are currently supported";
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeIf(
    const ASTPipeIf* pipe_if, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list, bool allow_terminal) {
  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_IF)) {
    return MakeSqlErrorAt(pipe_if) << "Pipe IF not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:IF");

  // Store the expression and subpipeline for each IF case.
  // If the last one is the ELSE case, its resolved_expr will be null.
  struct CaseInfo {
    CaseInfo(std::unique_ptr<const ResolvedExpr> resolved_expr,
             const ASTSubpipeline* ast_subpipeline)
        : resolved_expr(std::move(resolved_expr)),
          ast_subpipeline(ast_subpipeline) {}

    std::unique_ptr<const ResolvedExpr> resolved_expr;
    const ASTSubpipeline* ast_subpipeline = nullptr;
  };
  std::vector<CaseInfo> cases;

  // Gather CaseInfos for the IF and ELSEIF cases.
  // Resolve their condition expressions and find the first one with a true
  // value, which will be the selected case.
  int selected_case_idx = -1;
  int this_case_idx = 0;
  GOOGLESQL_RET_CHECK(!pipe_if->if_cases().empty());
  for (const ASTPipeIfCase* ast_case : pipe_if->if_cases()) {
    const char* clause_name = "Pipe IF condition expression";
    const ASTExpression* ast_expr = ast_case->condition();

    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveScalarExpr(ast_expr, scope, clause_name, &resolved_expr));
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(ast_expr, clause_name, &resolved_expr));

    // Note: If constant expressions after the selected case return an error
    // during evaluation, that's supposed to be allowed, but that cannot occur
    // so far.  We'll need to separate checking that they are all constant
    // expressions (with values) from checking which ones evaluate successfully.
    GOOGLESQL_ASSIGN_OR_RETURN(
        const Value value,
        GetConstantValue(ast_expr, resolved_expr.get(), clause_name));

    if (selected_case_idx == -1) {
      if (!value.is_null() && value.bool_value()) {
        selected_case_idx = this_case_idx;
      }
    }

    cases.emplace_back(std::move(resolved_expr), ast_case->subpipeline());
    ++this_case_idx;
  }
  GOOGLESQL_RET_CHECK_EQ(this_case_idx, cases.size());
  GOOGLESQL_RET_CHECK_GE(cases.size(), 1);

  // Add the CaseInfo for the else case.
  if (pipe_if->else_subpipeline() != nullptr) {  // ELSE is present.
    cases.emplace_back(nullptr, pipe_if->else_subpipeline());

    // If we didn't pick another case, pick the ELSE case.
    if (selected_case_idx == -1) {
      selected_case_idx = this_case_idx;
    }
  }

  std::unique_ptr<const ResolvedSubpipeline> resolved_subpipeline;
  // These are the scan and NameList for the chosen case (which could be from
  // the input scan if no case is chosen).
  const ResolvedScan* chosen_scan = nullptr;
  std::shared_ptr<const NameList> chosen_name_list;

  if (selected_case_idx != -1) {
    // Now resolve the subpipeline for the selected case only.
    CaseInfo& selected_case = cases[selected_case_idx];

    std::shared_ptr<const NameList> inside_name_list =
        (*current_name_list)->CopyWithIsValueTable();

    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_subpipeline,
        ResolveSubpipeline(selected_case.ast_subpipeline, outer_scope,
                           (*current_scan)->column_list(),
                           (*current_scan)->is_ordered(), &inside_name_list,
                           allow_terminal));

    chosen_scan = resolved_subpipeline->scan();
    chosen_name_list = inside_name_list;
  } else {
    // No conditions are true and there's no ELSE, so IF is a no-op and the
    // output is the same as the input scan.
    chosen_scan = current_scan->get();
    chosen_name_list = *current_name_list;
  }

  std::vector<std::unique_ptr<const ResolvedPipeIfCase>> resolved_cases;
  for (int idx = 0; idx < cases.size(); ++idx) {
    CaseInfo& case_info = cases[idx];

    GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view sql,
                     GetSQLForASTNode(case_info.ast_subpipeline));

    resolved_cases.push_back(MakeResolvedPipeIfCase(
        std::move(case_info.resolved_expr), sql,
        idx == selected_case_idx ? std::move(resolved_subpipeline) : nullptr));
  }
  GOOGLESQL_RET_CHECK_EQ(resolved_subpipeline, nullptr);

  auto if_scan = MakeResolvedPipeIfScan(
      chosen_scan->column_list(), std::move(*current_scan), selected_case_idx,
      std::move(resolved_cases));

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(pipe_if->hint(), if_scan.get()));
  if_scan->set_is_ordered(chosen_scan->is_ordered());

  *current_scan = std::move(if_scan);
  *current_name_list = chosen_name_list;
  return absl::OkStatus();
}

// Common code to resolve the subpipeline in FORK or TEE.
// Note: `ast_subpipeline` can be NULL, for a default empty subpipeline.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeForkOrTeeSubpipeline(
    const ASTSubpipeline* ast_subpipeline, const NameScope* outer_scope,
    const ResolvedScan* current_scan, const NameList* current_name_list,
    bool allow_terminal,
    std::vector<std::unique_ptr<const ResolvedGeneralizedQuerySubpipeline>>*
        resolved_subpipelines) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::shared_ptr<const NameList> inside_name_list =
      current_name_list->CopyWithIsValueTable();

  // Since FORK/TEE run like multiple scans of a CTE, `is_ordered` is not
  // passed into the subpipelines.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedSubpipeline> resolved_subpipeline,
      ResolveSubpipeline(
          ast_subpipeline, outer_scope, current_scan->column_list(),
          /*input_is_ordered=*/false, &inside_name_list, allow_terminal));

  // We get an OutputSchema if the subpipeline produced a table.
  std::unique_ptr<const ResolvedOutputSchema> output_schema;
  if (inside_name_list != nullptr) {
    output_schema = MakeOutputSchema(*inside_name_list);

    // This is called in ValidateStatementIsReturnable for the result table of
    // the outer statements. We need it here for the inner query result table.
    GOOGLESQL_RETURN_IF_ERROR(ValidateColumnListIsReturnable(
        output_schema->output_column_list(), ast_subpipeline));
  }

  resolved_subpipelines->push_back(MakeResolvedGeneralizedQuerySubpipeline(
      std::move(resolved_subpipeline), std::move(output_schema)));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeFork(
    const ASTPipeFork* pipe_fork, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list, bool allow_terminal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:FORK");

  GOOGLESQL_RETURN_IF_ERROR(CheckTerminalPipeOperatorAllowed(pipe_fork, FEATURE_PIPE_FORK,
                                                   "FORK", allow_terminal));

  std::vector<std::unique_ptr<const ResolvedGeneralizedQuerySubpipeline>>
      resolved_subpipelines;

  for (const auto& ast_subpipeline : pipe_fork->subpipeline_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolvePipeForkOrTeeSubpipeline(
        ast_subpipeline, outer_scope, current_scan->get(),
        current_name_list->get(), allow_terminal, &resolved_subpipelines));
  }

  auto fork_scan =
      MakeResolvedPipeForkScan(ResolvedColumnList(), std::move(*current_scan),
                               std::move(resolved_subpipelines));

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(pipe_fork->hint(), fork_scan.get()));

  *current_scan = std::move(fork_scan);
  *current_name_list = nullptr;  // There is no output table schema.
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeTee(
    const ASTPipeTee* pipe_tee, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list, bool allow_terminal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:TEE");

  // While TEE is not actually terminal, it is allowed only in places that
  // terminal operators are allowed, since it can produce multiple outputs.
  GOOGLESQL_RETURN_IF_ERROR(CheckTerminalPipeOperatorAllowed(pipe_tee, FEATURE_PIPE_TEE,
                                                   "TEE", allow_terminal));

  std::vector<std::unique_ptr<const ResolvedGeneralizedQuerySubpipeline>>
      resolved_subpipelines;

  std::vector<const ASTSubpipeline*> ast_subpipeline_list(
      pipe_tee->subpipeline_list().begin(), pipe_tee->subpipeline_list().end());

  // TEE with zero inputs just tees out the current table.
  // NULL ast_subpipelines are treated as empty subpipelines in
  // ResolveSubpipeline.
  if (ast_subpipeline_list.empty()) {
    ast_subpipeline_list.push_back(nullptr);
  }

  for (const auto& ast_subpipeline : ast_subpipeline_list) {
    GOOGLESQL_RETURN_IF_ERROR(ResolvePipeForkOrTeeSubpipeline(
        ast_subpipeline, outer_scope, current_scan->get(),
        current_name_list->get(), allow_terminal, &resolved_subpipelines));
  }

  ResolvedColumnList column_list = (*current_scan)->column_list();
  auto tee_scan =
      MakeResolvedPipeTeeScan(std::move(column_list), std::move(*current_scan),
                              std::move(resolved_subpipelines));

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(pipe_tee->hint(), tee_scan.get()));

  // Unlike FORK, for TEE the input NameList propagates to the output,
  // so we don't reset `current_name_list`.

  *current_scan = std::move(tee_scan);
  needs_generalized_query_stmt_ = true;
  return absl::OkStatus();
}

// This method has different arguments from other pipe operators.
// It doesn't use the current scan or NameList at all, and doesn't add a
// new current scan.  Instead, it creates a partially filled-in
// ResolvedWithScan and adds it to `with_scans_to_add`.  ResolvePipeOperatorList
// will add those scans after processing the full pipe operator list.
// CTE names registered are added to `named_subqueries_added` so they can
// be unregistered when they go out of scope.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeWith(
    const ASTPipeWith* pipe_with, std::vector<IdString>* named_subqueries_added,
    std::vector<std::unique_ptr<ResolvedWithScan>>* with_scans_to_add) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_WITH)) {
    return MakeSqlErrorAt(pipe_with) << "Pipe WITH not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:WITH");

  // Validate no group rows aliases.
  for (const ASTWithClauseEntry* entry : pipe_with->with_clause()->entries()) {
    if (entry->aliased_group_rows() != nullptr) {
      return MakeSqlErrorAt(entry->aliased_group_rows())
             << "GROUP ROWS not supported in Pipe WITH operator";
    }
  }

  // We pass is_outer_query=true because that's only used to control the
  // error for WITH on subqueries when FEATURE_WITH_ON_SUBQUERY isn't set.
  // This check doesn't apply for pipe WITH which is controlled by its own
  // feature.
  GOOGLESQL_RET_CHECK(pipe_with->with_clause() != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entries,
      ResolveWithClauseIfPresent(pipe_with->with_clause(),
                                 /*is_outer_query=*/true));
  GOOGLESQL_RET_CHECK(!with_entries.empty());

  // Record the CTE names created.
  for (const ASTWithClauseEntry* entry : pipe_with->with_clause()->entries()) {
    if (entry->aliased_query() != nullptr) {
      const IdString with_alias =
          entry->aliased_query()->alias()->GetAsIdString();
      named_subqueries_added->push_back(with_alias);
    }
  }

  // Create the scan with an empty column list and a placeholder input scan.
  // (Using a null input query is not allowed.)
  auto scan = MakeResolvedWithScan(
      ResolvedColumnList(), std::move(with_entries),
      MakeResolvedSingleRowScan(), pipe_with->with_clause()->recursive());
  with_scans_to_add->push_back(std::move(scan));

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeExportData(
    const ASTPipeExportData* pipe_export_data, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list, bool allow_terminal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:EXPORT_DATA");

  GOOGLESQL_RETURN_IF_ERROR(CheckTerminalPipeOperatorAllowed(
      pipe_export_data, FEATURE_PIPE_EXPORT_DATA, "EXPORT DATA",
      allow_terminal));

  // The parser won't allow an AS query.
  GOOGLESQL_RET_CHECK(pipe_export_data->export_data_statement()->query() == nullptr);

  // This intentionally skips the SupportsStatementKind check for EXPORT DATA.
  // Pipe EXPORT DATA is controlled by LanguageFeatures only.
  std::unique_ptr<ResolvedStatement> output;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExportDataStatement(pipe_export_data->export_data_statement(),
                                 current_name_list->get(), &output));

  // This is called at the end of ResolvedStatement in the statement codepath.
  GOOGLESQL_RETURN_IF_ERROR(FinishResolveStatement(
      pipe_export_data->export_data_statement(), output.get()));

  GOOGLESQL_RET_CHECK_EQ(output->node_kind(), RESOLVED_EXPORT_DATA_STMT);
  std::unique_ptr<ResolvedExportDataStmt> stmt(
      output.release()->GetAs<ResolvedExportDataStmt>());

  GOOGLESQL_RET_CHECK(stmt->query() == nullptr);
  stmt->set_query(std::move(*current_scan));

  auto scan =
      MakeResolvedPipeExportDataScan(ResolvedColumnList(), std::move(stmt));

  *current_scan = std::move(scan);
  *current_name_list = nullptr;  // There is no output table schema.
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeCreateTable(
    const ASTPipeCreateTable* pipe_create_table, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list, bool allow_terminal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:CREATE_TABLE");

  GOOGLESQL_RETURN_IF_ERROR(CheckTerminalPipeOperatorAllowed(
      pipe_create_table, FEATURE_PIPE_CREATE_TABLE, "CREATE TABLE",
      allow_terminal));

  // The parser won't allow an AS query.
  GOOGLESQL_RET_CHECK(pipe_create_table->create_table_statement()->query() == nullptr);

  // This intentionally bypasses the SupportsStatementKind check for CREATE
  // TABLE. Pipe CREATE TABLE is controlled by LanguageFeatures only.
  std::unique_ptr<ResolvedStatement> output;
  GOOGLESQL_RETURN_IF_ERROR(ResolveCreateTableStatement(
      pipe_create_table->create_table_statement(), current_scan,
      current_name_list->get(), &output));

  // This is called at the end of ResolvedStatement in the statement codepath.
  GOOGLESQL_RETURN_IF_ERROR(FinishResolveStatement(
      pipe_create_table->create_table_statement(), output.get()));

  // We expect a CTAS statement with `query` not filled in.
  GOOGLESQL_RET_CHECK_EQ(output->node_kind(), RESOLVED_CREATE_TABLE_AS_SELECT_STMT);
  std::unique_ptr<ResolvedCreateTableAsSelectStmt> stmt(
      output.release()->GetAs<ResolvedCreateTableAsSelectStmt>());
  GOOGLESQL_RET_CHECK(stmt->query() == nullptr);
  stmt->set_query(std::move(*current_scan));

  auto scan =
      MakeResolvedPipeCreateTableScan(ResolvedColumnList(), std::move(stmt));

  *current_scan = std::move(scan);
  *current_name_list = nullptr;  // There is no output table schema.
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeInsert(
    const ASTPipeInsert* pipe_insert, const NameScope* outer_scope,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list, bool allow_terminal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:INSERT");

  GOOGLESQL_RETURN_IF_ERROR(CheckTerminalPipeOperatorAllowed(
      pipe_insert, FEATURE_PIPE_INSERT, "INSERT", allow_terminal));

  const ASTInsertStatement* ast_stmt = pipe_insert->insert_statement();

  // The parser won't allow a query or VALUES.
  GOOGLESQL_RET_CHECK(ast_stmt->query() == nullptr);
  GOOGLESQL_RET_CHECK(ast_stmt->rows() == nullptr);

  if (ast_stmt->returning() != nullptr) {
    return MakeSqlErrorAt(ast_stmt->returning())
           << "Pipe INSERT does not support THEN RETURN";
  }

  // This intentionally skips the SupportsStatementKind check for INSERT.
  // Pipe INSERT is controlled by LanguageFeatures only.
  std::unique_ptr<ResolvedInsertStmt> stmt;
  GOOGLESQL_RETURN_IF_ERROR(ResolveInsertStatement(ast_stmt, current_name_list->get(),
                                         std::move(*current_scan), &stmt));

  // This is called at the end of ResolvedStatement in the statement codepath.
  GOOGLESQL_RETURN_IF_ERROR(FinishResolveStatement(ast_stmt, stmt.get()));

  GOOGLESQL_RET_CHECK(stmt->query() != nullptr);  // `*current_scan` moved to `query`.
  auto scan = MakeResolvedPipeInsertScan(ResolvedColumnList(), std::move(stmt));

  *current_scan = std::move(scan);
  *current_name_list = nullptr;  // There is no output table schema.
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::CheckTerminalPipeOperatorAllowed(
    const ASTNode* location, LanguageFeature required_language_feature,
    const char* operator_name, bool allow_terminal) {
  if (!language().LanguageFeatureEnabled(required_language_feature)) {
    return MakeSqlErrorAt(location)
           << "Pipe " << operator_name << " not supported";
  }
  if (!language().SupportsStatementKind(RESOLVED_GENERALIZED_QUERY_STMT)) {
    return MakeSqlErrorAt(location)
           << "Generalized query statements are not supported so pipe "
           << operator_name << " cannot be used";
  }
  if (!allow_terminal) {
    return MakeSqlErrorAt(location)
           << operator_name
           << " is only allowed as part of the outermost query in a statement";
  }
  return absl::OkStatus();
}

// Warning: Make sure `outer_scope` is the outer scope for correlated
// references, not the scope with the current pipe operator's columns.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<std::unique_ptr<const ResolvedSubpipeline>>
Resolver::ResolveSubpipeline(
    const ASTSubpipeline* ast_subpipeline, const NameScope* outer_scope,
    const ResolvedColumnList& input_column_list, bool input_is_ordered,
    std::shared_ptr<const NameList>* subpipeline_name_list,
    bool allow_terminal) {
  std::unique_ptr<ResolvedScan> inside_scan_mutable =
      MakeResolvedSubpipelineInputScan(input_column_list);
  inside_scan_mutable->set_is_ordered(input_is_ordered);

  std::unique_ptr<const ResolvedScan> inside_scan =
      std::move(inside_scan_mutable);

  // Null ast_subpipeline means make a default empty subpipeline.
  if (ast_subpipeline != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolvePipeOperatorList(
        ast_subpipeline->pipe_operator_list(), outer_scope, &inside_scan,
        subpipeline_name_list,
        /*inferred_type_for_query=*/nullptr,
        /*allow_terminal=*/allow_terminal));
  }

  return MakeResolvedSubpipeline(std::move(inside_scan));
}

// This is used for more helpful errors for pipe SET and pipe DROP to describe
// the NameTarget that doesn't work.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
static const char* GetTargetKindNameForPipeSetOrDropError(
    const NameTarget& target) {
  std::string target_kind;
  switch (target.kind()) {
    case NameTarget::RANGE_VARIABLE:
      return "a table alias";
    case NameTarget::FIELD_OF:
      return "a field inside a value table";
    case NameTarget::IMPLICIT_COLUMN:
    case NameTarget::EXPLICIT_COLUMN:
      // This includes at least pseudo-columns and correlated columns.
      // We don't have an easy way to tell what kind of column it is.
      return "present but is not a column on the pipe input table";
    case NameTarget::AMBIGUOUS:
      return "ambiguous";
    default:
      return "present but is not a column";
  }
}

// Shared helper for pipe DROP, RENAME and SET that fills in the map of
// columns to update, handling several error conditions.
// `item_struct_map` is map from target column name to a struct with at least
// these fields:
//   struct ItemStruct {
//     const ASTIdentifier* ast_identifier;  // Target column name
//     bool found;  // false means not found yet. This will be updated.
//   }
template <class ItemStructMap>
static absl::Status FillItemStructFromNameListColumns(
    ItemStructMap* item_struct_map, const NameList& name_list,
    const NameScope* scope, bool must_be_unambiguous_in_name_list,
    bool must_be_unambiguous_in_scope, const absl::string_view operator_name,
    const absl::string_view operator_verb) {
  // For each NameList column, update `found` on items it matches.
  for (const NamedColumn& named_column : name_list.columns()) {
    auto* item_struct = googlesql_base::FindOrNull(*item_struct_map, named_column.name());
    if (item_struct != nullptr) {
      if (must_be_unambiguous_in_name_list && item_struct->found) {
        return MakeSqlErrorAt(item_struct->ast_identifier)
               << "Column name in pipe " << operator_name
               << " exists more than once in input table: "
               << ToIdentifierLiteral(named_column.name());
      }
      item_struct->found = true;
    }
  }

  // Handle item names that weren't found in the NameList.
  for (const auto& map_entry : *item_struct_map) {
    const auto& item_struct = map_entry.second;

    // Do a NameScope lookup if we might need it below.
    NameTarget target;
    bool found_in_scope = false;
    if (!item_struct.found || must_be_unambiguous_in_scope) {
      // Pipes has no lateral column alias, so no such thing as a column
      // reference context.
      found_in_scope = scope->LookupName(map_entry.first, &target);
    }

    // Give an error if we didn't find a matching column in the NameList.
    //
    // If a name was in the NameScope (a non-column or non-local column or
    // ambiguity), the error will say what was found instead of a column.
    //
    // The second condition is used in RENAME.  Since NameScope makes range
    // variables always override columns, it never reports an ambiguity between
    // columns and range variables.  We searched the NameList for columns,
    // which won't find the range variable that's in the scope, which
    // the name would resolve to in expressions.  To avoid confusion, we make it
    // an error if a non-column was found in the NameScope lookup.
    //
    // This doesn't apply for SET and DROP, where it's allowed to drop/hide a
    // range variable, as long as there was also a matching column in the
    // NameList.
    if (!item_struct.found ||
        (must_be_unambiguous_in_scope && !target.IsColumn())) {
      if (found_in_scope) {
        return MakeSqlErrorAt(item_struct.ast_identifier)
               << "Name in pipe " << operator_name << " is "
               << GetTargetKindNameForPipeSetOrDropError(target) << "; "
               << operator_name << " can only " << operator_verb
               << " columns: " << ToIdentifierLiteral(map_entry.first);
      } else {
        return MakeSqlErrorAt(item_struct.ast_identifier)
               << "Column name in pipe " << operator_name
               << " not found in input table: "
               << ToIdentifierLiteral(map_entry.first);
      }
    }
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeDrop(
    const ASTPipeDrop* pipe_drop, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:DROP");
  // Set of names to drop, for MergeOptions.
  IdStringSetCase column_names_to_drop;

  // Map with additional info for the columns we intend to drop.
  struct DropItem {
    const ASTIdentifier* ast_identifier;
    bool found;  // false means not found yet.
  };
  absl::flat_hash_map<IdString, DropItem, IdStringCaseHash,
                      IdStringCaseEqualFunc>
      columns_to_drop;

  // Build the set and map, checking the names are valid to drop.
  for (const ASTIdentifier* identifier :
       pipe_drop->column_list()->identifier_list()) {
    const IdString column_name = identifier->GetAsIdString();
    if (IsInternalAlias(column_name)) {
      return MakeSqlErrorAt(identifier)
             << "Cannot use pipe DROP with internal alias "
             << ToIdentifierLiteral(column_name);
    }
    if (!googlesql_base::InsertIfNotPresent(&columns_to_drop, column_name,
                                 DropItem{identifier, /*found=*/false})) {
      return MakeSqlErrorAt(identifier)
             << "Duplicate column name in pipe DROP: "
             << ToIdentifierLiteral(column_name);
    }
    column_names_to_drop.insert(column_name);
  }
  GOOGLESQL_RET_CHECK(!columns_to_drop.empty());

  // Fill the map and check column names exist in the input table.
  GOOGLESQL_RETURN_IF_ERROR(FillItemStructFromNameListColumns(
      &columns_to_drop, **current_name_list, scope,
      /*must_be_unambiguous_in_name_list=*/false,
      /*must_be_unambiguous_in_scope=*/false, "DROP", "drop"));

  // Make the new NameList, dropping unwanted columns.  Range variables and
  // pseudo-columns in scope matching the name will also be dropped.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto new_name_list,
      (*current_name_list)
          ->Copy(pipe_drop, {.excluded_field_names = &column_names_to_drop}));

  if (new_name_list->num_columns() == 0) {
    return MakeSqlErrorAt(pipe_drop)
           << "Pipe DROP dropped all columns in the input table";
  }

  // We don't add a ProjectScan with a reduced column_list since that isn't
  // necessary.  Some columns may also still be referencable through the
  // original range variables, even if they were dropped as top-level columns.

  *current_name_list = std::move(new_name_list);

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeRename(
    const ASTPipeRename* pipe_rename, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:RENAME");
  NameList::MergeOptions::ColumnsToRenameMap columns_to_rename_map;

  // Map with additional info for the columns we intend to rename.
  struct RenameItem {
    const ASTIdentifier* ast_identifier;
    const ASTIdentifier* ast_new_identifier;
    bool found;  // false means not found yet.
  };
  absl::flat_hash_map<IdString, RenameItem, IdStringCaseHash,
                      IdStringCaseEqualFunc>
      columns_to_rename;

  // Build the set and map, checking the names are valid to drop.
  for (const auto& rename_item : pipe_rename->rename_item_list()) {
    const IdString old_name = rename_item->old_name()->GetAsIdString();
    const IdString new_name = rename_item->new_name()->GetAsIdString();
    if (IsInternalAlias(old_name)) {
      return MakeSqlErrorAt(rename_item->old_name())
             << "Cannot use pipe RENAME with internal alias "
             << ToIdentifierLiteral(old_name);
    }
    if (IsInternalAlias(new_name)) {
      return MakeSqlErrorAt(rename_item->new_name())
             << "Cannot use pipe RENAME with internal alias "
             << ToIdentifierLiteral(new_name);
    }

    if (!googlesql_base::InsertIfNotPresent(
            &columns_to_rename, old_name,
            RenameItem{rename_item->old_name(), rename_item->new_name(),
                       /*found=*/false})) {
      return MakeSqlErrorAt(rename_item->old_name())
             << "Duplicate column name in pipe RENAME: "
             << ToIdentifierLiteral(old_name);
    }
    GOOGLESQL_RET_CHECK(
        googlesql_base::InsertIfNotPresent(&columns_to_rename_map, old_name, new_name));
  }
  GOOGLESQL_RET_CHECK(!columns_to_rename.empty());

  // Fill the map and check column names exist in the input table.
  GOOGLESQL_RETURN_IF_ERROR(FillItemStructFromNameListColumns(
      &columns_to_rename, **current_name_list, scope,
      /*must_be_unambiguous_in_name_list=*/true,
      /*must_be_unambiguous_in_scope=*/true, "RENAME", "rename"));

  // Make the new NameList, applying the renames.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto new_name_list,
      (*current_name_list)
          ->Copy(pipe_rename, {.columns_to_rename = &columns_to_rename_map}));

  // We don't add a ProjectScan with a reduced column_list since that isn't
  // necessary.  Some columns may also still be referencable through the
  // original range variables, even if they were dropped as top-level columns.

  *current_name_list = std::move(new_name_list);
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeSet(
    const ASTPipeSet* pipe_set, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:SET");
  static constexpr char kSetClause[] = "pipe SET clause";

  // Map of columns to replace in MergeFrom.
  NameList::MergeOptions::ColumnsToReplaceMap columns_to_replace_map;

  // Map with additional info for the columns we intend to replace.
  struct ReplaceItem {
    const ASTIdentifier* ast_identifier;
    bool found;  // false means not found yet.
    ResolvedColumn new_column;
  };
  absl::flat_hash_map<IdString, ReplaceItem, IdStringCaseHash,
                      IdStringCaseEqualFunc>
      columns_to_replace;

  const IdString scan_alias = MakeIdString("$pipe_set");
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns;

  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
  query_resolution_info->analytic_resolver()->DisableNamedWindowRefs(
      kSetClause);

  // Build the two maps, checking the names are valid to replace.
  for (const ASTPipeSetItem* ast_set_item : pipe_set->set_item_list()) {
    const ASTIdentifier* identifier = ast_set_item->column();
    const IdString column_name = identifier->GetAsIdString();
    if (IsInternalAlias(column_name)) {
      return MakeSqlErrorAt(identifier)
             << "Cannot use pipe SET with internal alias "
             << ToIdentifierLiteral(column_name);
    }

    auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        query_resolution_info.get(), scope,
        ExprResolutionInfoOptions{
            .allows_aggregation = false,
            .allows_analytic = true,
            .clause_name = kSetClause,
            .top_level_ast_expr = ast_set_item->expression(),
            .column_alias = column_name,
        });

    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_set_item->expression(),
                                expr_resolution_info.get(), &resolved_expr));

    ResolvedColumn column(AllocateColumnId(), scan_alias, column_name,
                          resolved_expr->annotated_type());
    computed_columns.push_back(
        MakeResolvedComputedColumn(column, std::move(resolved_expr)));

    // Don't let the new column be pruned, even if not accessed.
    // The SQLBuilder ends up not accessing the value if the column is pruned,
    // and other engines may have the same issue.
    RecordColumnAccess(column);

    if (!googlesql_base::InsertIfNotPresent(
            &columns_to_replace, column_name,
            ReplaceItem{identifier, /*found=*/false, column})) {
      return MakeSqlErrorAt(identifier) << "Duplicate column name in pipe SET: "
                                        << ToIdentifierLiteral(column_name);
    }
    GOOGLESQL_RET_CHECK(
        googlesql_base::InsertIfNotPresent(&columns_to_replace_map, column_name, column));
  }
  GOOGLESQL_RET_CHECK(!columns_to_replace.empty());

  GOOGLESQL_RET_CHECK(!query_resolution_info->HasAggregation());
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->CheckComputedColumnListsAreEmpty());
  if (query_resolution_info->HasAnalytic()) {
    // Add an AnalyticScan if any window functions were present.
    // This also makes a ProjectScan if necessary before the AnalyticScan.
    GOOGLESQL_RETURN_IF_ERROR(AddAnalyticScan(query_resolution_info.get(), current_scan));
  }

  // Add the ProjectScan to compute the replacement columns.  We don't prune
  // the column_list here because it's not necessary, and because some
  // some columns may also still be referencable through the original range
  // variables, even if they were replaced as top-level columns.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(std::move(computed_columns),
                                                    current_scan));

  // Fill the map and check column names exist in the input table.
  GOOGLESQL_RETURN_IF_ERROR(FillItemStructFromNameListColumns(
      &columns_to_replace, **current_name_list, scope,
      /*must_be_unambiguous_in_name_list=*/true,
      /*must_be_unambiguous_in_scope=*/false, "SET", "update"));

  // Make the new NameList, applying the updates.  Range variables and
  // pseudo-columns in scope matching the updated names will also be dropped.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto new_name_list,
      (*current_name_list)
          ->Copy(pipe_set, {.columns_to_replace = &columns_to_replace_map}));

  *current_name_list = std::move(new_name_list);

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipePivot(
    const ASTPipePivot* pipe_pivot, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:PIVOT");
  // `input_is_subquery` is only used for producing errors in strict mode,
  // which is not relevant here, so just pass true to avoid the errors.
  return ResolvePivotClause(std::move(*current_scan), *current_name_list, scope,
                            /*input_is_subquery=*/true,
                            pipe_pivot->pivot_clause(), current_scan,
                            current_name_list);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeUnpivot(
    const ASTPipeUnpivot* pipe_unpivot, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:UNPIVOT");
  return ResolveUnpivotClause(std::move(*current_scan), *current_name_list,
                              scope, pipe_unpivot->unpivot_clause(),
                              current_scan, current_name_list);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeMatchRecognize(
    const ASTPipeMatchRecognize* pipe_match_recognize, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:MATCH_RECOGNIZE");
  return ResolveMatchRecognizeClause(
      std::move(*current_scan), *current_name_list, scope,
      pipe_match_recognize->match_recognize(), current_scan, current_name_list);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeSetOperation(
    const ASTPipeSetOperation* set_operation, const NameScope* outer_scope,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list,
    const Type* inferred_type_for_pipe) {
  bool is_all = set_operation->metadata()->all_or_distinct()->value() ==
                ASTSetOperation::ALL;
  switch (set_operation->metadata()->op_type()->value()) {
    case ASTSetOperation::INTERSECT:
      if (is_all) {
        analyzer_output_properties_.AddFeatureLabel(
            "PipeOperator:INTERSECT_ALL");
      } else {
        analyzer_output_properties_.AddFeatureLabel(
            "PipeOperator:INTERSECT_DISTINCT");
      }
      break;
    case ASTSetOperation::EXCEPT:
      if (is_all) {
        analyzer_output_properties_.AddFeatureLabel("PipeOperator:EXCEPT_ALL");
      } else {
        analyzer_output_properties_.AddFeatureLabel(
            "PipeOperator:EXCEPT_DISTINCT");
      }
      break;
    case ASTSetOperation::UNION:
      if (is_all) {
        analyzer_output_properties_.AddFeatureLabel("PipeOperator:UNION_ALL");
      } else {
        analyzer_output_properties_.AddFeatureLabel(
            "PipeOperator:UNION_DISTINCT");
      }
      break;
    case googlesql::ASTSetOperation::NOT_SET:
      break;
  }
  SetOperationResolver resolver(set_operation, *current_name_list, current_scan,
                                this);

  std::unique_ptr<const ResolvedScan> output_scan;
  std::shared_ptr<const NameList> output_name_list;
  GOOGLESQL_RETURN_IF_ERROR(resolver.Resolve(outer_scope, inferred_type_for_pipe,
                                   &output_scan, &output_name_list));
  *current_scan = std::move(output_scan);
  *current_name_list = std::move(output_name_list);
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
static absl::Status VerifyNoLimitOrOrderByInRecursiveQuery(
    const ASTQuery* query) {
  if (query->order_by() != nullptr) {
    return MakeSqlErrorAt(query->order_by())
           << "A recursive query may not use ORDER BY";
  }
  if (query->limit_offset() != nullptr) {
    return MakeSqlErrorAt(query->limit_offset())
           << "A recursive query may not use LIMIT";
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
static std::string GetRecursiveQueryFormatError(
    const LanguageOptions language_options) {
  static constexpr char kRecursiveQueryFormatError[] =
      "Recursive query does not have the form <non-recursive-term> "
      "UNION [ALL|DISTINCT] <recursive-term>";
  if (!language_options.LanguageFeatureEnabled(FEATURE_PIPES)) {
    return kRecursiveQueryFormatError;
  }
  // If pipes are enabled, we also allow the pipe form.
  return absl::StrCat(kRecursiveQueryFormatError,
                      " or the pipe form <non-recursive-term> |> UNION "
                      "[ALL|DISTINCT] <recursive-term>");
}

// Returns the string representation of the set operation type.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
static std::string GetSetOpTypeStr(const ASTSetOperationMetadata& metadata) {
  return absl::StrCat("$", absl::AsciiStrToLower(absl::StrReplaceAll(
                               metadata.GetSQLForOperation(), {{" ", "_"}})));
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<Resolver::SetOperationResolver>
Resolver::GetSetOperationResolverForRecursiveQuery(
    const ASTQuery* query, const std::vector<IdString>& table_alias) {
  GOOGLESQL_RETURN_IF_ERROR(VerifyNoLimitOrOrderByInRecursiveQuery(query));

  const absl::Span<const ASTPipeOperator* const> pipe_operators =
      query->pipe_operator_list();
  if (!pipe_operators.empty() &&
      !language().LanguageFeatureEnabled(FEATURE_PIPES)) {
    return MakeSqlErrorAt(pipe_operators.front())
           << "Pipe query syntax not supported";
  }
  // Register a NULL entry for the named subquery so that any references to it
  // from within the non-recursive term results in an error. We'll change this
  // to an actual NamedSubquery object when the recursive term resolves in
  // SetOperationResolver::ResolveRecursive().
  named_subquery_map_[table_alias].push_back(nullptr);

  if (pipe_operators.empty()) {
    const ASTSetOperation* ast_set_op =
        query->query_expr()->GetAsOrNull<ASTSetOperation>();
    // The set operation resolver will validate that all operators are the same.
    if (ast_set_op == nullptr || ast_set_op->metadata()
                                         ->set_operation_metadata_list(0)
                                         ->op_type()
                                         ->value() != ASTSetOperation::UNION) {
      return MakeSqlErrorAt(query) << GetRecursiveQueryFormatError(language());
    }
    return SetOperationResolver(ast_set_op, this);
  }

  const ASTPipeSetOperation* ast_pipe_set_op =
      pipe_operators.back()->GetAsOrNull<ASTPipeSetOperation>();
  if (ast_pipe_set_op == nullptr ||
      ast_pipe_set_op->metadata()->op_type()->value() !=
          ASTSetOperation::UNION) {
    return MakeSqlErrorAt(query) << GetRecursiveQueryFormatError(language());
  }

  // All pipe operators except the last one belong to the non-recursive
  // term. Resolve them here because `SetOperationResolver` expects a
  // resolved input table for pipe set operations.
  std::unique_ptr<const ResolvedScan> non_recursive_term_scan;
  std::shared_ptr<const NameList> non_recursive_term_name_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveQuery(
      query,
      // We always pass empty_name_scope_ when resolving the subquery inside
      // WITH. Those queries must stand alone and cannot reference any
      // correlated columns or other names defined outside.
      empty_name_scope_.get(),
      /*query_alias=*/
      MakeIdString(
          absl::StrCat(GetSetOpTypeStr(*ast_pipe_set_op->metadata()), 1)),
      &non_recursive_term_scan, &non_recursive_term_name_list,
      // Exclude the last pipe operator, because it is the recursive term.
      {.exclude_last_pipe_operator = true}));
  return SetOperationResolver(ast_pipe_set_op, non_recursive_term_name_list,
                              &non_recursive_term_scan, this);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ValidateAliasedQueryModifiers(
    IdString query_alias, const ASTAliasedQueryModifiers* modifiers,
    bool is_recursive) {
  if (modifiers->recursion_depth_modifier() != nullptr) {
    if (!language().LanguageFeatureEnabled(
            FEATURE_WITH_RECURSIVE_DEPTH_MODIFIER)) {
      return MakeSqlErrorAt(modifiers->recursion_depth_modifier())
             << "Recursion depth modifier is not supported";
    }
    if (!is_recursive) {
      return MakeSqlErrorAt(modifiers->recursion_depth_modifier())
             << "Recursion depth modifier is not allowed for non-recursive "
                "CTE named "
             << query_alias;
    }
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<std::unique_ptr<const ResolvedRecursionDepthModifier>>
Resolver::ResolveRecursionDepthModifier(
    const ASTRecursionDepthModifier* recursion_depth_modifier) {
  constexpr char kClauseName[] = "WITH DEPTH";
  NameScope empty_scope;
  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(&empty_scope, kClauseName);
  std::unique_ptr<const ResolvedExpr> lower, upper;
  if (recursion_depth_modifier->lower_bound()->bound() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(recursion_depth_modifier->lower_bound()->bound(),
                    expr_resolution_info.get(), &lower));
    GOOGLESQL_RETURN_IF_ERROR(ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
        kClauseName, recursion_depth_modifier->lower_bound(), &lower));
  }
  if (recursion_depth_modifier->upper_bound()->bound() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(recursion_depth_modifier->upper_bound()->bound(),
                    expr_resolution_info.get(), &upper));
    GOOGLESQL_RETURN_IF_ERROR(ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
        kClauseName, recursion_depth_modifier->upper_bound(), &upper));
  }
  if (lower != nullptr && upper != nullptr && lower->Is<ResolvedLiteral>() &&
      upper->Is<ResolvedLiteral>()) {
    // At this point, we already know lower_value and upper_value are
    // non-negative and not null. We still need to ensure lower bound is no
    // larger than upper bound.
    const Value& lower_value = lower->GetAs<ResolvedLiteral>()->value();
    const Value& upper_value = upper->GetAs<ResolvedLiteral>()->value();
    if (lower_value.int64_value() > upper_value.int64_value()) {
      return MakeSqlErrorAt(recursion_depth_modifier)
             << kClauseName << " expects lower bound (" << lower_value.Format()
             << ") no larger than upper bound (" << upper_value.Format() << ")";
    }
  }

  const IdString depth_column_alias =
      recursion_depth_modifier->alias() != nullptr
          ? recursion_depth_modifier->alias()->GetAsIdString()
          : kRecursionDepthAlias;
  GOOGLESQL_ASSIGN_OR_RETURN(auto recursion_depth_column,
                   ResolvedColumnHolderBuilder()
                       .set_column(ResolvedColumn(
                           AllocateColumnId(), kRecursionDepthId,
                           depth_column_alias, type_factory_->get_int64()))
                       .Build());
  return ResolvedRecursionDepthModifierBuilder()
      .set_lower_bound(std::move(lower))
      .set_upper_bound(std::move(upper))
      .set_recursion_depth_column(std::move(recursion_depth_column))
      .Build();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<std::unique_ptr<const ResolvedWithEntry>>
Resolver::ResolveAliasedQuery(const ASTAliasedQuery* with_entry,
                              bool recursive) {
  const IdString with_alias = with_entry->alias()->GetAsIdString();
  if (with_entry->modifiers() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateAliasedQueryModifiers(
        with_alias, with_entry->modifiers(), recursive));
  }

  // Generate a unique alias for this WITH subquery, if necessary.
  IdString unique_alias = MakeUniqueWithAlias(with_alias);

  std::unique_ptr<const ResolvedScan> resolved_subquery;
  std::shared_ptr<const NameList> subquery_name_list;
  if (recursive) {
    // WITH entry is actually recursive (not just defined using the RECURSIVE
    // keyword).
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<const ResolvedWithEntry>>
            inner_with_entries,
        ResolveWithClauseIfPresent(with_entry->query()->with_clause(),
                                   /*is_outer_query=*/false));
    GOOGLESQL_ASSIGN_OR_RETURN(SetOperationResolver setop_resolver,
                     GetSetOperationResolverForRecursiveQuery(
                         with_entry->query(), /*table_alias=*/{with_alias}));
    GOOGLESQL_RETURN_IF_ERROR(setop_resolver.ResolveRecursive(
        empty_name_scope_.get(), {with_alias}, unique_alias, &resolved_subquery,
        &subquery_name_list));

    if (with_entry->modifiers() != nullptr &&
        with_entry->modifiers()->recursion_depth_modifier() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedRecursionDepthModifier>
              recursion_depth_modifier,
          ResolveRecursionDepthModifier(
              with_entry->modifiers()->recursion_depth_modifier()));
      GOOGLESQL_RETURN_IF_ERROR(setop_resolver.AddDepthColumnToRecursiveScan(
          with_entry->modifiers()->recursion_depth_modifier(), {with_alias},
          std::move(recursion_depth_modifier), &resolved_subquery,
          &subquery_name_list));
    }
    GOOGLESQL_RETURN_IF_ERROR(FinishResolveWithClauseIfPresent(
        with_entry->query(), std::move(inner_with_entries),
        &resolved_subquery));
  } else {
    // We always pass empty_name_scope_ when resolving the subquery inside
    // WITH.  Those queries must stand alone and cannot reference any
    // correlated columns or other names defined outside.
    GOOGLESQL_RETURN_IF_ERROR(ResolveQuery(with_entry->query(), empty_name_scope_.get(),
                                 with_alias, &resolved_subquery,
                                 &subquery_name_list));

    // We don't want pseudo-columns from the subquery to make it out
    // of the WITH, so prune them away.
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForColumnPruning(*subquery_name_list,
                                                    &resolved_subquery));

    AddNamedSubquery({with_alias},
                     std::make_unique<NamedSubquery>(
                         unique_alias, /*recursive_in=*/false,
                         resolved_subquery->column_list(), subquery_name_list));
  }

  // The output columns for the WITH entry cannot be pruned because we require
  // the ResolvedWithRefScan column_list to match them exactly.
  RecordColumnAccess(resolved_subquery->column_list());

  std::unique_ptr<const ResolvedWithEntry> resolved_with_entry =
      MakeResolvedWithEntry(unique_alias.ToString(),
                            std::move(resolved_subquery));
  return resolved_with_entry;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::MaybeAddProjectForComputedColumns(
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  auto add_project_scan_for_computed_columns =
      [current_scan](std::vector<std::unique_ptr<const ResolvedComputedColumn>>
                         computed_columns) {
        if (computed_columns.empty()) {
          return;
        }

        // Any side effect columns are meant to be used immediately by the
        // consumer $with_side_effect() call and not leak further as an
        // additional column beyond the next scan.
        absl::flat_hash_set<ResolvedColumn> side_effect_columns;
        if ((*current_scan)->Is<ResolvedAggregateScan>()) {
          for (const auto& agg : (*current_scan)
                                     ->GetAs<ResolvedAggregateScan>()
                                     ->aggregate_list()) {
            if (agg->Is<ResolvedDeferredComputedColumn>()) {
              side_effect_columns.insert(
                  agg->GetAs<ResolvedDeferredComputedColumn>()
                      ->side_effect_column());
            }
          }
        }

        ResolvedColumnList wrapper_column_list;
        for (const ResolvedColumn& column : (*current_scan)->column_list()) {
          if (side_effect_columns.find(column) == side_effect_columns.end()) {
            wrapper_column_list.push_back(column);
          }
        }
        for (const auto& computed_column : computed_columns) {
          wrapper_column_list.push_back(computed_column->column());
        }
        *current_scan = MakeResolvedProjectScan(wrapper_column_list,
                                                std::move(computed_columns),
                                                std::move(*current_scan));
      };
  // 1 column or less means only 1 ProjectScan is needed.
  if (computed_columns.size() <= 1) {
    add_project_scan_for_computed_columns(std::move(computed_columns));
    return absl::OkStatus();
  }

  // Partition the computed columns into 2 sets; 'dependee' computed_columns
  // and 'non_dependee' computed_columns.
  GOOGLESQL_ASSIGN_OR_RETURN(PartitionedComputedColumns partitioned_computed_columns,
                   PartitionComputedColumns(std::move(computed_columns)));

  // Ensure that two projects scans are sufficient. Re-partition the 'dependee'
  // computed columns to ensure that they are now all 'non_dependee' columns.
  // We don't expect 'dependee' columns to remain 'dependees' after the first
  // partitioning, so explicitly check that here.
  GOOGLESQL_ASSIGN_OR_RETURN(
      PartitionedComputedColumns repartitioned_computed_columns,
      PartitionComputedColumns(
          std::move(partitioned_computed_columns.dependee_computed_columns)));
  GOOGLESQL_RET_CHECK(repartitioned_computed_columns.dependee_computed_columns.empty())
      << "Computed columns have a dependency tree of depth > 2.";

  // Add project scans that respect the dependency ordering.
  add_project_scan_for_computed_columns(
      std::move(repartitioned_computed_columns.non_dependee_computed_columns));
  add_project_scan_for_computed_columns(
      std::move(partitioned_computed_columns.non_dependee_computed_columns));
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::MaybeAddProjectForColumnPruning(
    const NameList& name_list,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const absl::flat_hash_set<ResolvedColumn> old_visible_columns(
      (*current_scan)->column_list().begin(),
      (*current_scan)->column_list().end());

  absl::flat_hash_set<ResolvedColumn> keep_columns;
  for (const ResolvedColumn& column : name_list.GetResolvedColumns()) {
    keep_columns.insert(column);
    GOOGLESQL_RET_CHECK(old_visible_columns.contains(column)) << column.DebugString();
  }

  // To minimize the changes to existing ProjectScans, rather than using the
  // new list directly, we preserve the original list in its original order,
  // just skipping unwanted columns.
  bool did_pruning = false;
  ResolvedColumnList pruned_column_list;
  for (const ResolvedColumn& column : (*current_scan)->column_list()) {
    if (keep_columns.contains(column)) {
      pruned_column_list.push_back(column);
    } else {
      did_pruning = true;
    }
  }

  if (did_pruning) {
    *current_scan = MakeResolvedProjectScan(
        pruned_column_list, /*expr_list=*/{}, std::move(*current_scan));
  }

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::AddAggregateScan(
    const ASTSelect* select, bool is_for_select_distinct,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  ResolvedColumnList column_list;
  for (const GroupByColumnState& group_by_column_state :
       query_resolution_info->group_by_column_state_list()) {
    column_list.push_back(group_by_column_state.computed_column->column());
  }
  for (const auto& aggregate_column :
       query_resolution_info->aggregate_columns_to_compute()) {
    column_list.push_back(aggregate_column->column());
    if (aggregate_column->Is<ResolvedDeferredComputedColumn>()) {
      column_list.push_back(
          aggregate_column->GetAs<ResolvedDeferredComputedColumn>()
              ->side_effect_column());
    }
  }
  for (const std::unique_ptr<const ResolvedGroupingCall>& grouping_call :
       query_resolution_info->grouping_columns_list()) {
    column_list.push_back(grouping_call->output_column());
  }

  std::vector<std::unique_ptr<const ResolvedColumnRef>> rollup_column_list;
  std::vector<std::unique_ptr<const ResolvedGroupingSetBase>> grouping_set_list;

  // Retrieve the grouping sets and rollup list for the aggregate scan, if any.
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->ReleaseGroupingSetsAndRollupList(
      &grouping_set_list, &rollup_column_list, language()));

  std::unique_ptr<ResolvedAggregateScan> aggregate_scan =
      MakeResolvedAggregateScan(
          column_list, std::move(*current_scan),
          query_resolution_info->release_group_by_columns_to_compute(),
          query_resolution_info->release_aggregate_columns_to_compute(),
          std::move(grouping_set_list), std::move(rollup_column_list),
          query_resolution_info->release_grouping_call_list());

  // If the feature is not enabled, any collation annotation that might exist on
  // the grouping expressions is ignored.
  if (language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    std::vector<ResolvedCollation> collation_list;
    bool empty = true;
    for (const auto& group_by_expr : aggregate_scan->group_by_list()) {
      ResolvedCollation resolved_collation;
      if (group_by_expr->expr()->type_annotation_map() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(resolved_collation,
                         ResolvedCollation::MakeResolvedCollation(
                             *group_by_expr->expr()->type_annotation_map()));
        empty &= resolved_collation.Empty();
      }
      collation_list.push_back(std::move(resolved_collation));
    }
    if (!empty) {
      aggregate_scan->set_collation_list(collation_list);
    }
  }
  // We might have aggregation without GROUP BY.
  if (!is_for_select_distinct && select->group_by() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveHintsForNode(select->group_by()->hint(), aggregate_scan.get()));
    if (query_resolution_info->is_group_by_all()) {
      InternalAnalyzerOutputProperties::MarkTargetSyntax(
          analyzer_output_properties_, aggregate_scan.get(),
          SQLBuildTargetSyntax::kGroupByAll);
    }
  }

  *current_scan = std::move(aggregate_scan);
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
// Returns an error if `scan` is a Anonymized / DP / Aggregation Threshold
// aggregate scan containing:
//    - An aggregate function with a GROUP BY modifier
//    - An aggregate function with a WHERE filter modifier
//    - An aggregate function with a HAVING filter modifier
//    - An `AGG` aggregate function for aggregating measures.
// Returns absl::OkStatus() otherwise.
static absl::Status EnsureAggregationsValidForPrivacyScan(
    const ResolvedScan* scan,
    const QueryResolutionInfo* query_resolution_info) {
  if (!scan->Is<ResolvedAggregateScanBase>() ||
      scan->Is<ResolvedAggregateScan>()) {
    return absl::OkStatus();
  }
  const ResolvedAggregateScanBase* aggregate_scan =
      scan->GetAs<ResolvedAggregateScanBase>();
  std::string aggregation_type;
  switch (aggregate_scan->node_kind()) {
    case RESOLVED_ANONYMIZED_AGGREGATE_SCAN:
      aggregation_type = "Anonymized Aggregation";
      break;
    case RESOLVED_DIFFERENTIAL_PRIVACY_AGGREGATE_SCAN:
      aggregation_type = "Differential Privacy Aggregation";
      break;
    case RESOLVED_AGGREGATION_THRESHOLD_AGGREGATE_SCAN:
      aggregation_type = "Aggregation Threshold Aggregation";
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected aggregate scan type: "
                       << aggregate_scan->node_kind_string();
  }

  for (const std::unique_ptr<const ResolvedComputedColumnBase>&
           computed_column : aggregate_scan->aggregate_list()) {
    GOOGLESQL_RET_CHECK(computed_column->expr()->Is<ResolvedAggregateFunctionCall>());
    const ResolvedAggregateFunctionCall* aggregate_function_call =
        computed_column->expr()->GetAs<ResolvedAggregateFunctionCall>();
    const ASTFunctionCall* ast_function_call =
        query_resolution_info->GetASTFunctionCallForAggregateColumn(
            computed_column.get());
    std::string modifier_type;
    if (!aggregate_function_call->group_by_list().empty()) {
      modifier_type = "GROUP BY";
    } else if (aggregate_function_call->where_expr() != nullptr) {
      modifier_type = "WHERE filter";
    } else if (aggregate_function_call->having_expr() != nullptr) {
      modifier_type = "HAVING filter";
    }
    if (!modifier_type.empty()) {
      if (ast_function_call != nullptr) {
        return MakeSqlErrorAt(ast_function_call)
               << aggregation_type
               << " does not support aggregate functions with " << modifier_type
               << " modifiers";
      } else {
        return MakeSqlError() << aggregation_type
                              << " does not support aggregate functions with "
                              << modifier_type << " modifiers";
      }
    }
    if (IsMeasureAggFunction(aggregate_function_call->function())) {
      if (ast_function_call != nullptr) {
        return MakeSqlErrorAt(ast_function_call)
               << aggregation_type << " does not support the "
               << aggregate_function_call->function()->SQLName() << " function";
      } else {
        return MakeSqlError()
               << aggregation_type << " does not support the "
               << aggregate_function_call->function()->SQLName() << " function";
      }
    }
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::AddAnonymizedAggregateScan(
    const ASTSelect* select, QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  if (query_resolution_info->HasGroupByGroupingSets()) {
    GOOGLESQL_RETURN_IF_ERROR(CreateUnsupportedGroupingSetsError(
        query_resolution_info, select, "anonymization queries"));
  }
  GOOGLESQL_RET_CHECK(query_resolution_info->with_modifier_mode() ==
                WithModifierMode::ANONYMIZATION ||
            query_resolution_info->with_modifier_mode() ==
                WithModifierMode::DIFFERENTIAL_PRIVACY);
  ResolvedColumnList column_list;
  for (const GroupByColumnState& group_by_column_state :
       query_resolution_info->group_by_column_state_list()) {
    column_list.push_back(group_by_column_state.computed_column->column());
  }
  for (const std::unique_ptr<const ResolvedComputedColumnBase>&
           aggregate_column :
       query_resolution_info->aggregate_columns_to_compute()) {
    column_list.push_back(aggregate_column->column());
    if (aggregate_column->Is<ResolvedDeferredComputedColumn>()) {
      column_list.push_back(
          aggregate_column->GetAs<ResolvedDeferredComputedColumn>()
              ->side_effect_column());
    }
  }
  GOOGLESQL_RET_CHECK(!column_list.empty());
  std::vector<std::unique_ptr<const ResolvedOption>>
      resolved_anonymization_options;
  const ASTOptionsList* options_list = nullptr;
  if (select->with_modifier() != nullptr &&
      select->with_modifier()->options() != nullptr) {
    options_list = select->with_modifier()->options();
  } else if (query_resolution_info->IsPipeAggregate() &&
             query_resolution_info->with_modifier_mode() ==
                 WithModifierMode::DIFFERENTIAL_PRIVACY &&
             select->parent()->Is<ASTPipeAggregate>()) {
    const ASTPipeAggregate* pipe_aggregate =
        select->parent()->GetAsOrDie<ASTPipeAggregate>();
    if (pipe_aggregate->with_modifier() != nullptr &&
        pipe_aggregate->with_modifier()->options() != nullptr) {
      options_list = pipe_aggregate->with_modifier()->options();
    }
  }

  if (options_list != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveAnonymizationOptionsList(
        options_list, *query_resolution_info, &resolved_anonymization_options));
  }

  switch (query_resolution_info->with_modifier_mode()) {
    case WithModifierMode::ANONYMIZATION: {
      auto anonymized_scan = MakeResolvedAnonymizedAggregateScan(
          column_list, std::move(*current_scan),
          query_resolution_info->release_group_by_columns_to_compute(),
          query_resolution_info->release_aggregate_columns_to_compute(),
          /*grouping_set_list=*/{}, /*rollup_column_list=*/{},
          /*grouping_call_list=*/{},
          /*k_threshold_expr=*/nullptr,
          std::move(resolved_anonymization_options));
      // We might have aggregation without GROUP BY.
      if (select->group_by() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(select->group_by()->hint(),
                                            anonymized_scan.get()));
      }
      *current_scan = std::move(anonymized_scan);
    } break;
    case WithModifierMode::DIFFERENTIAL_PRIVACY: {
      auto anonymized_scan = MakeResolvedDifferentialPrivacyAggregateScan(
          column_list, std::move(*current_scan),
          query_resolution_info->release_group_by_columns_to_compute(),
          query_resolution_info->release_aggregate_columns_to_compute(),
          /*grouping_set_list=*/{}, /*rollup_column_list=*/{},
          /*grouping_call_list=*/{},
          /*group_selection_threshold_expr=*/nullptr,
          std::move(resolved_anonymization_options));
      // We might have aggregation without GROUP BY.
      if (select->group_by() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(select->group_by()->hint(),
                                            anonymized_scan.get()));
      }
      *current_scan = std::move(anonymized_scan);
    } break;

    default:
      GOOGLESQL_RET_CHECK_FAIL();
      break;
  }
  GOOGLESQL_RETURN_IF_ERROR(EnsureAggregationsValidForPrivacyScan(current_scan->get(),
                                                        query_resolution_info));
  analyzer_output_properties_.has_anonymization = true;
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<std::unique_ptr<const ResolvedScan>>
Resolver::AddAggregationThresholdAggregateScan(
    const ASTSelect* select, QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedScan> input_scan) {
  GOOGLESQL_RET_CHECK(query_resolution_info->with_modifier_mode() ==
            WithModifierMode::AGGREGATION_THRESHOLD);
  ResolvedColumnList column_list;
  for (const GroupByColumnState& group_by_column_state :
       query_resolution_info->group_by_column_state_list()) {
    column_list.push_back(group_by_column_state.computed_column->column());
  }
  for (const std::unique_ptr<const ResolvedComputedColumnBase>&
           aggregate_column :
       query_resolution_info->aggregate_columns_to_compute()) {
    column_list.push_back(aggregate_column->column());
    if (aggregate_column->Is<ResolvedDeferredComputedColumn>()) {
      column_list.push_back(
          aggregate_column->GetAs<ResolvedDeferredComputedColumn>()
              ->side_effect_column());
    }
  }

  std::vector<std::unique_ptr<const ResolvedColumnRef>> rollup_column_list;
  std::vector<std::unique_ptr<const ResolvedGroupingSetBase>> grouping_set_list;

  // Retrieve the grouping sets and rollup list for the aggregate scan, if any.
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->ReleaseGroupingSetsAndRollupList(
      &grouping_set_list, &rollup_column_list, language()));

  GOOGLESQL_RET_CHECK(!column_list.empty());
  std::vector<std::unique_ptr<const ResolvedOption>> resolved_options;
  if (select->with_modifier() != nullptr &&
      select->with_modifier()->options() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveAggregationThresholdOptionsList(
        select->with_modifier()->options(), *query_resolution_info,
        &resolved_options));
  }
  auto aggregation_threshold_scan =
      MakeResolvedAggregationThresholdAggregateScan(
          column_list, std::move(input_scan),
          query_resolution_info->release_group_by_columns_to_compute(),
          query_resolution_info->release_aggregate_columns_to_compute(),
          std::move(grouping_set_list), std::move(rollup_column_list),
          /*grouping_call_list=*/{}, std::move(resolved_options));
  GOOGLESQL_RETURN_IF_ERROR(EnsureAggregationsValidForPrivacyScan(
      aggregation_threshold_scan.get(), query_resolution_info));
  // When GROUP BY shows up in aggregation, aggregation threshold aggregate scan
  // respects the specified hints.
  if (select->group_by() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(select->group_by()->hint(),
                                        aggregation_threshold_scan.get()));
  }
  return aggregation_threshold_scan;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::AddAnalyticScan(
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  // An analytic function in the ORDER BY can reference select list aliases,
  // so if there are any such SELECT list columns that need precomputing,
  // project them now.  We have to be careful though.  We can only project
  // SELECT list precomputed columns if they do not themselves include
  // analytic functions.  If they do, they must be computed after the
  // AnalyticScan.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      select_columns_without_analytic;
  GOOGLESQL_RETURN_IF_ERROR(
      query_resolution_info->GetAndRemoveSelectListColumnsWithoutAnalytic(
          &select_columns_without_analytic));

  // TODO: Consider using MaybeAddProject...() here, if we don't
  // care about sorting the column list.
  if (!select_columns_without_analytic.empty()) {
    const std::vector<ResolvedColumn>& column_list =
        (*current_scan)->column_list();
    ResolvedColumnList concat_columns =
        ConcatColumnListWithComputedColumnsAndSort(
            column_list, select_columns_without_analytic);

    *current_scan = MakeResolvedProjectScan(
        concat_columns, std::move(select_columns_without_analytic),
        std::move(*current_scan));
    // Avoid deletion after transfer.
    select_columns_without_analytic.clear();
  }
  return query_resolution_info->analytic_resolver()->CreateAnalyticScan(
      query_resolution_info, current_scan);
}

// Given an input pre-GROUP BY NameScope and a ValidFieldInfoMap
// returns a post-GROUP BY NameScope.  The ValidFieldInfoMap
// represents the mapping between pre-GROUP BY to post-GROUP BY columns
// and fields.  The returned NameScope includes the same previous_scope_
// as the pre-GROUP BY NameScope, while the returned NameScope's local
// NameList is created by merging the pre-GROUP BY local NameList with
// the ValidFieldInfoMap.  The resulting NameScope's local NameList
// includes all the same names as the old one, but columns/fields that are
// not available to access after GROUP BY are marked as invalid to access.
// The names/fields that are valid to access map to post-GROUP BY versions
// of those columns.
//
// WARNING: When we call this function we *MUST* only use a NameScope whose
// previous_scope_ is for an outer/correlation NameScope.  Outer/correlation
// NameScope names should and will remain accessible after GROUP BY or
// DISTINCT, but local names that are not grouped by become invalid.  If
// the 'pre_group_by_scope' used to create the post-GROUP BY Namescope
// has a previous_scope_ that is not an outer/correlation NameScope (for
// example a layered NameScope that adds new names that resolve over the
// FROM clause names) then any names in that previous_scope_ will
// incorrectly remain valid to access after creating the new post-GROUP BY
// NameScope.  This is because CreateNameScopeGivenValidNamePaths() creates
// a new NameScope from 'pre_group_by_scope' by updating its local names()
// and value_table_columns(), but names from previous scopes remain
// unchanged and valid to access in the returned NameScope.
static absl::Status CreatePostGroupByNameScope(
    const NameScope* pre_group_by_scope,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const NameScope>* post_group_by_scope_out) {
  const ValidFieldInfoMap* valid_field_info_map =
      &query_resolution_info->group_by_valid_field_info_map();
  ValidFieldInfoMap empty_map;
  if (query_resolution_info->IsPipeAggregate()) {
    // In pipe AGGREGATE, we pass an empty ValidFieldInfoMap because we
    // don't want any non-aggregated column references to be valid.
    valid_field_info_map = &empty_map;
  }

  std::unique_ptr<NameScope> post_group_by_scope;
  GOOGLESQL_RETURN_IF_ERROR(pre_group_by_scope->CreateNameScopeGivenValidNamePaths(
      *valid_field_info_map, &post_group_by_scope));

  *post_group_by_scope_out = std::move(post_group_by_scope);
  return absl::OkStatus();
}

absl::Status Resolver::WrapInSuccessiveProjectScans(
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        successive_columns_to_compute,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  for (auto& computed_column : successive_columns_to_compute) {
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> singleton;
    singleton.push_back(std::move(computed_column));
    GOOGLESQL_RETURN_IF_ERROR(
        MaybeAddProjectForComputedColumns(std::move(singleton), current_scan));
  }
  return absl::OkStatus();
}

absl::Status Resolver::AddRemainingScansForSelect(
    const ASTSelect* select, const ASTOrderBy* order_by,
    const ASTLimitOffset* limit_offset,
    const NameScope* having_and_order_by_scope,
    std::unique_ptr<const ResolvedExpr> resolved_where_expr,
    std::unique_ptr<const ResolvedExpr> resolved_having_expr,
    std::unique_ptr<const ResolvedExpr> resolved_qualify_expr,
    QueryResolutionInfo* query_resolution_info,
    std::shared_ptr<const NameList>* output_name_list,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();

  // Integrity checks for when lateral column references are disabled.
  if (!language().LanguageFeatureEnabled(FEATURE_LATERAL_COLUMN_REFERENCES)) {
    GOOGLESQL_RET_CHECK(
        query_resolution_info
            ->successive_select_list_columns_to_compute_before_aggregation()
            ->empty());
    GOOGLESQL_RET_CHECK(
        query_resolution_info->successive_columns_to_compute_after_aggregation()
            ->empty());
    GOOGLESQL_RET_CHECK(
        query_resolution_info->successive_columns_to_compute_after_analytic()
            ->empty());

    // Without lateral references, the WHERE clause is already resolved and
    // applied to the input scan.
    GOOGLESQL_RET_CHECK(resolved_where_expr == nullptr);
  }

  GOOGLESQL_RETURN_IF_ERROR(WrapInSuccessiveProjectScans(
      query_resolution_info
          ->release_successive_select_list_columns_to_compute_before_aggregation(),  // NOLINT
      current_scan));

  if (resolved_where_expr != nullptr) {
    ResolvedColumnList tmp_column_list = (*current_scan)->column_list();
    *current_scan =
        MakeResolvedFilterScan(tmp_column_list, std::move(*current_scan),
                               std::move(resolved_where_expr));
  }

  // Precompute any other columns necessary before aggregation.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
      query_resolution_info
          ->release_select_list_columns_to_compute_before_aggregation(),
      current_scan));

  if (!query_resolution_info->scoped_aggregation_state()
           ->row_range_determined) {
    GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->PinToRowRange(std::nullopt));
  }
  if (query_resolution_info->HasGroupByOrAggregation()) {
    switch (query_resolution_info->with_modifier_mode()) {
      case WithModifierMode::ANONYMIZATION:
      case WithModifierMode::DIFFERENTIAL_PRIVACY:
        GOOGLESQL_RETURN_IF_ERROR(AddAnonymizedAggregateScan(
            select, query_resolution_info, current_scan));
        break;
      case WithModifierMode::AGGREGATION_THRESHOLD: {
        GOOGLESQL_ASSIGN_OR_RETURN(*current_scan, AddAggregationThresholdAggregateScan(
                                            select, query_resolution_info,
                                            std::move(*current_scan)));
      } break;
      case WithModifierMode::NONE:
        // We know all the GROUP BY and aggregate columns, so can now create an
        // AggregateScan.
        GOOGLESQL_RETURN_IF_ERROR(AddAggregateScan(select,
                                         /*is_for_select_distinct=*/false,
                                         query_resolution_info, current_scan));
        break;
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(WrapInSuccessiveProjectScans(
      query_resolution_info
          ->release_successive_columns_to_compute_after_aggregation(),
      current_scan));

  // Precompute any other columns necessary after aggregation.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
      query_resolution_info->release_columns_to_compute_after_aggregation(),
      current_scan));

  if (resolved_having_expr != nullptr) {
    // The HAVING might reference select list aliases, so if there are
    // any such SELECT list columns that need precomputing, precompute
    // those without an analytic function.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        select_columns_without_analytic;
    GOOGLESQL_RETURN_IF_ERROR(
        query_resolution_info->GetAndRemoveSelectListColumnsWithoutAnalytic(
            &select_columns_without_analytic));

    if (!select_columns_without_analytic.empty()) {
      const std::vector<ResolvedColumn>& column_list =
          (*current_scan)->column_list();
      ResolvedColumnList concat_columns =
          ConcatColumnListWithComputedColumnsAndSort(
              column_list, select_columns_without_analytic);

      *current_scan = MakeResolvedProjectScan(
          concat_columns, std::move(select_columns_without_analytic),
          std::move(*current_scan));
    }

    const auto& tmp_column_list = (*current_scan)->column_list();
    *current_scan =
        MakeResolvedFilterScan(tmp_column_list, std::move(*current_scan),
                               std::move(resolved_having_expr));
  }

  if (query_resolution_info->HasAnalytic()) {
    GOOGLESQL_RETURN_IF_ERROR(AddAnalyticScan(query_resolution_info, current_scan));
  }

  GOOGLESQL_RETURN_IF_ERROR(WrapInSuccessiveProjectScans(
      query_resolution_info
          ->release_successive_columns_to_compute_after_analytic(),
      current_scan));

  // Precompute any other columns necessary after analytic functions.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
      query_resolution_info->release_columns_to_compute_after_analytic(),
      current_scan));

  // Make additional filter scan if QUALIFY is present.
  if (resolved_qualify_expr != nullptr) {
    // The QUALIFY clause might reference select list aliases, so if there are
    // any such SELECT list columns that need precomputing, precompute them.
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
        query_resolution_info->release_select_list_columns_to_compute(),
        current_scan));

    const auto& tmp_column_list = (*current_scan)->column_list();
    *current_scan =
        MakeResolvedFilterScan(tmp_column_list, std::move(*current_scan),
                               std::move(resolved_qualify_expr));
  }

  if (select->distinct()) {
    // If there are (aliased or non-aliased) select list columns to compute
    // then add a project first.
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
        query_resolution_info->release_select_list_columns_to_compute(),
        current_scan));

    // Note: The DISTINCT processing is very similar to the GROUP BY
    // processing.  The output of GROUP BY is used for resolving subsequent
    // clauses and expressions (e.g., the SELECT list), and the output of
    // DISTINCT is used for resolving the subsequent ORDER BY expressions.
    //
    // These steps include:
    // 1) Creating a new DISTINCT NameScope that includes names from the
    //    SELECT list that are available after DISTINCT.  Other names
    //    become invalid targets in the new DISTINCT NameScope.
    // 2) Then below, the ORDER BY expressions will be resolved against
    //    the new DISTINCT NameScope, returning an error if an invalid
    //    column/name is accessed.
    //
    // For handling DISTINCT, we use all the same machinery as GROUP BY,
    // but mark the QueryResolutionInfo so we know subsequent resolution
    // is for post-DISTINCT processing.
    GOOGLESQL_RETURN_IF_ERROR(ResolveSelectDistinct(
        select, select_column_state_list, output_name_list->get(), current_scan,
        query_resolution_info, output_name_list));
  }

  if (order_by != nullptr) {
    if (select->distinct()) {
      // Check expected state.  If DISTINCT is present, then we already
      // computed any necessary SELECT list columns before processing the
      // DISTINCT.
      GOOGLESQL_RET_CHECK(
          query_resolution_info->select_list_columns_to_compute()->empty());

      // If DISTINCT is present, then the ORDER BY expressions have *not*
      // been resolved yet.  Resolve the ORDER BY expressions to reference
      // the post-DISTINCT versions of columns.  Note that the DISTINCT
      // processing already updated <query_resolution_info> with the
      // mapping from pre-DISTINCT to post-DISTINCT versions of columns
      // and expressions, so we simply need to resolve the ORDER BY
      // expressions with the updated <query_resolution_info> and
      // post-distinct NameScope.  Resolution of ORDER BY expressions
      // against the output of DISTINCT has the same characteristics
      // as post-GROUP BY expression resolution.  ORDER BY expressions
      // resolve successfully to columns and path expressions that were
      // output from DISTINCT.  As such, any column reference that is
      // not in the SELECT list is an error.

      // Create a new NameScope for what comes out of the DISTINCT
      // AggregateScan.  It is derived from the <having_and_order_by_scope>,
      // and allows column references to resolve to the post-DISTINCT versions
      // of the columns.
      std::unique_ptr<const NameScope> distinct_scope;
      GOOGLESQL_RETURN_IF_ERROR(CreatePostGroupByNameScope(
          having_and_order_by_scope, query_resolution_info, &distinct_scope));

      // The second 'distinct_scope' NameScope argument is only
      // used for resolving the arguments to aggregate functions, but when
      // DISTINCT is present then aggregate functions are not allowed in
      // ORDER BY so we will always get an error regardless of whether or
      // not the name is visible post-DISTINCT.
      GOOGLESQL_RETURN_IF_ERROR(ResolveOrderByExprs(
          order_by, distinct_scope.get(), distinct_scope.get(),
          /*is_post_distinct=*/true, query_resolution_info));
    } else {
      // DISTINCT is *not* present so we have already resolved the ORDER BY
      // expressions in ResolveSelect() and do not resolve them here.
      // Also, the ORDER BY might have computed columns to compute so
      // add a wrapper project to compute them if necessary.
      // TODO: In many cases we can combine the two projects into
      // one, so fix this where possible.  Two projects are required when
      // an ORDER BY expression references a SELECT list alias, such as:
      //   SELECT a+1 as foo FROM T ORDER BY foo + 1;
      // In this case, we need a ProjectScan to compute foo, then another
      // ProjectScan to compute foo + 1.  Simply combining the SELECT
      // list columns to compute with the
      // query_resolution_info->order_by_columns_to_compute() does not work.
      GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
          query_resolution_info->release_select_list_columns_to_compute(),
          current_scan));
    }

    // <query_resolution_info>->order_by_columns_to_compute() already
    // contains the ORDER BY expressions that need computing, but we
    // also need to compute SELECT list computed columns before ordering,
    // so add them to the list.
    for (std::unique_ptr<const ResolvedComputedColumn>& select_computed_column :
         query_resolution_info->release_select_list_columns_to_compute()) {
      query_resolution_info->order_by_columns_to_compute()->push_back(
          std::move(select_computed_column));
    }

    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
        query_resolution_info->release_order_by_columns_to_compute(),
        current_scan));

    GOOGLESQL_RETURN_IF_ERROR(
        MakeResolvedOrderByScan(order_by->hint(), **output_name_list,
                                query_resolution_info->GetResolvedColumnList(),
                                {query_resolution_info->order_by_item_info()},
                                /*is_pipe_order_by=*/false, current_scan));
  }

  if (order_by == nullptr && !select->distinct()) {
    // TODO: For now, we always add the project if no ORDER BY and
    // no DISTINCT, including both aliased and non-aliased select list
    // columns to precompute.  This is primarily to minimize diffs
    // with the original plans.  We can probably do better about avoiding
    // unnecessary PROJECT nodes and delay projecting the non-aliased
    // expressions once this refactoring is submitted.
    // That backwards compatibility issue does not apply for pipe operators.
    // We do preserve this for pipe SELECT though, so we are consistent in
    // always producing a ProjectScan reflecting the output schema.
    if (!query_resolution_info->select_list_columns_to_compute()->empty() ||
        !query_resolution_info->IsPipeOp() ||
        query_resolution_info->IsPipeSelect()) {
      // For pipe EXTEND, if projected columns are pruned from the column_list,
      // the SQLBuilder will fail.
      if (query_resolution_info->IsPipeExtend()) {
        for (const auto& computed_column :
             *query_resolution_info->select_list_columns_to_compute()) {
          RecordColumnAccess(computed_column->column());
        }
      }

      *current_scan = MakeResolvedProjectScan(
          query_resolution_info->GetResolvedColumnList(),
          query_resolution_info->release_select_list_columns_to_compute(),
          std::move(*current_scan));
    }
  }

  // If we have ordering from the AGGREGATE list or GROUP BY (in pipe
  // AGGREGATE), add the ResolvedOrderByScan for that.
  if (!query_resolution_info->aggregate_order_by_item_info().empty() ||
      !query_resolution_info->group_by_order_by_item_info().empty()) {
    GOOGLESQL_RET_CHECK(query_resolution_info->order_by_item_info().empty());

    // All columns we need to order on already exist, so we don't need
    // to compute any additional expressions.
    // The order columns include GROUP BY items first and then AGGREGATE items.
    GOOGLESQL_RETURN_IF_ERROR(MakeResolvedOrderByScan(
        /*order_by_hint=*/nullptr, **output_name_list,
        query_resolution_info->GetResolvedColumnList(),
        {query_resolution_info->group_by_order_by_item_info(),
         query_resolution_info->aggregate_order_by_item_info()},
        /*is_pipe_order_by=*/true, current_scan));
  }

  if (limit_offset != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveLimitOffsetScan(
        limit_offset, having_and_order_by_scope, current_scan));
  }

  // Check column count here, because if there is SELECT AS STRUCT or
  // SELECT AS PROTO then the column counts will no longer match.
  // This doesn't work for pipe EXTEND or WINDOW because the column lists may
  // include pseudo-columns, which aren't counted in NameList::num_columns.
  if (!query_resolution_info->IsPipeExtendOrWindow()) {
    GOOGLESQL_RET_CHECK_EQ(query_resolution_info->pipe_extra_select_items()->size() +
                     select_column_state_list->Size(),
                 (*output_name_list)->num_columns());
  }

  if (select->select_as() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveSelectAs(select->select_as(), *select_column_state_list,
                        std::move(*current_scan), output_name_list->get(),
                        current_scan, output_name_list));
  }

  // Resolve the hint last since we want to attach it as the outermost Scan.
  if (select->hint() != nullptr) {
    // TODO Currently we always add a new ProjectScan to store the
    // hint.  We construct it mutable so we can build the hint_list.

    // To avoid undefined behavior, don't release 'current_scan' and use it in
    // the same function call.
    const std::vector<ResolvedColumn>& column_list =
        (*current_scan)->column_list();
    auto hinted_scan = MakeResolvedProjectScan(column_list, /*expr_list=*/{},
                                               std::move(*current_scan));

    GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(select->hint(), hinted_scan.get()));

    *current_scan = std::move(hinted_scan);
  }
  return absl::OkStatus();
}

absl::Status Resolver::AddColumnsForOrderByExprs(
    IdString query_alias, std::vector<OrderByItemInfo>* order_by_info,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        computed_columns) {
  for (int order_by_item_idx = 0; order_by_item_idx < order_by_info->size();
       ++order_by_item_idx) {
    OrderByItemInfo& item_info = (*order_by_info)[order_by_item_idx];
    if (!item_info.is_select_list_index()) {
      GOOGLESQL_RET_CHECK(item_info.order_expression != nullptr);
      if (item_info.order_expression->node_kind() == RESOLVED_COLUMN_REF &&
          !item_info.order_expression->GetAs<ResolvedColumnRef>()
               ->is_correlated()) {
        item_info.order_column =
            item_info.order_expression->GetAs<ResolvedColumnRef>()->column();
      } else {
        bool already_computed = false;
        for (const std::unique_ptr<const ResolvedComputedColumn>&
                 computed_column : *computed_columns) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              bool is_same,
              IsSameExpressionForGroupBy(item_info.order_expression.get(),
                                         computed_column->expr(), language()));
          if (is_same) {
            item_info.order_column = computed_column->column();
            item_info.order_expression.reset();  // not needed any more
            already_computed = true;
            break;
          }
        }
        if (already_computed) {
          continue;
        }
        const IdString order_column_alias =
            MakeIdString(absl::StrCat("$orderbycol", order_by_item_idx + 1));
        ResolvedColumn resolved_column(
            AllocateColumnId(), query_alias, order_column_alias,
            item_info.order_expression->annotated_type());
        item_info.order_column = resolved_column;
        computed_columns->emplace_back(MakeResolvedComputedColumn(
            item_info.order_column, std::move(item_info.order_expression)));
      }
    }
  }
  return absl::OkStatus();
}

// This is currently used for ORDER BY after set operations and for
// pipe ORDER BY.  Resolution of ORDER BY for SELECT is done elsewhere since
// it gets resolved in two phases.
// This is also used for resolving standalone ORDER BY statements in GQL.
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveOrderBySimple(
    const ASTOrderBy* order_by, const NameList& name_list,
    const NameScope* scope, const char* clause_name, OrderBySimpleMode mode,
    std::unique_ptr<const ResolvedScan>* scan) {
  GOOGLESQL_RET_CHECK(mode == OrderBySimpleMode::kNormal ||
            mode == OrderBySimpleMode::kPipes ||
            mode == OrderBySimpleMode::kGql);

  // We use a new QueryResolutionInfo because resolving the ORDER BY
  // outside of set operations is independent from its input subquery
  // resolution.
  std::unique_ptr<QueryResolutionInfo> query_resolution_info =
      std::make_unique<QueryResolutionInfo>(this);

  query_resolution_info->analytic_resolver()->DisableNamedWindowRefs(
      clause_name);

  bool is_gql = mode == OrderBySimpleMode::kGql;
  bool is_pipes = mode == OrderBySimpleMode::kPipes;

  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      scope, scope, scope, /*allows_aggregation_in=*/false,
      /*allows_analytic_in=*/true,
      /*use_post_grouping_columns_in=*/false, clause_name,
      query_resolution_info.get());
  GOOGLESQL_RETURN_IF_ERROR(ResolveOrderingExprs(
      order_by->ordering_expressions(), expr_resolution_info.get(),
      /*allow_ordinals=*/!is_gql, "GQL",
      query_resolution_info->mutable_order_by_item_info()));

  GOOGLESQL_RET_CHECK(!query_resolution_info->HasAggregation());

  // If the ORDER BY clause after set operations includes analytic functions,
  // then we need to create an analytic scan for them before we do ordering.
  // For example:
  //
  // SELECT a, b, c FROM t1
  // UNION ALL
  // SELECT a, b, c FROM t2
  // ORDER BY sum(a) OVER (PARTITION BY b ORDER BY c);
  //
  // The ORDER BY binds outside the UNION ALL, so the UNION ALL feeds
  // an AnalyticScan, which in turn feeds the OrderByScan.
  if (query_resolution_info->HasAnalytic()) {
    GOOGLESQL_RETURN_IF_ERROR(
        query_resolution_info->analytic_resolver()->CreateAnalyticScan(
            query_resolution_info.get(), scan));
  }

  std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsForOrderByExprs(
      /*query_alias=*/kOrderById,
      query_resolution_info->mutable_order_by_item_info(), &computed_columns));

  // The output columns of the ORDER BY are the same as the output of the
  // original input.
  const ResolvedColumnList output_columns = (*scan)->column_list();

  // If the ORDER BY requires computed columns, add a wrapper project to
  // compute them.
  GOOGLESQL_RETURN_IF_ERROR(
      MaybeAddProjectForComputedColumns(std::move(computed_columns), scan));

  return MakeResolvedOrderByScan(order_by->hint(), name_list, output_columns,
                                 {query_resolution_info->order_by_item_info()},
                                 is_pipes, scan);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveOrderByItems(
    const NameList& name_list,
    const OrderByItemInfoVectorList& order_by_info_lists, bool is_pipe_order_by,
    std::vector<std::unique_ptr<const ResolvedOrderByItem>>*
        resolved_order_by_items) {
  resolved_order_by_items->clear();

  // There is always exactly one vector of OrderByInfo for standard ORDER BY
  // and for pipe ORDER BY.
  // For pipe AGGREGATE, there can be two vectors, from the GROUP BY and
  // AGGREGATE lists, and the nested loops will concatenate them.
  GOOGLESQL_RET_CHECK(!order_by_info_lists.empty());
  for (const auto& order_by_info_vector : order_by_info_lists) {
    for (const OrderByItemInfo& item_info : order_by_info_vector.get()) {
      std::unique_ptr<const ResolvedColumnRef> resolved_column_ref;
      if (item_info.is_select_list_index()) {
        if (item_info.select_list_index < 0 ||
            item_info.select_list_index >= name_list.num_columns()) {
          if (is_pipe_order_by) {
            // Note that the error for ordinal < 1 is produced earlier, in
            // ResolveOrderByExprs.
            return MakeSqlErrorAt(item_info.ast_location)
                   << "ORDER BY column number exceeds input table column "
                      "count: "
                   << item_info.select_list_index + 1 << " vs "
                   << name_list.num_columns();
          }
          return MakeSqlErrorAt(item_info.ast_location)
                 << "ORDER BY is out of SELECT column number range: "
                 << item_info.select_list_index + 1;
        }
        // NOTE: Accessing scan column list works now as we don't deduplicate
        // anything from the column list.  Thus it matches 1:1 with the select
        // list.  If that changes, we should use name list instead.
        // Convert the select list ordinal reference to a column reference.
        resolved_column_ref = MakeColumnRef(
            name_list.column(item_info.select_list_index).column());
      } else {
        GOOGLESQL_RET_CHECK(item_info.order_column.IsInitialized());
        resolved_column_ref = MakeColumnRef(item_info.order_column);
      }

      std::unique_ptr<const ResolvedExpr> resolved_collation_name;
      if (item_info.ast_collate != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateAndResolveOrderByCollate(
            item_info.ast_collate, item_info.ast_location,
            resolved_column_ref->column().type(), &resolved_collation_name));
      }

      auto resolved_order_by_item = MakeResolvedOrderByItem(
          std::move(resolved_column_ref), std::move(resolved_collation_name),
          item_info.is_descending, item_info.null_order);

      MaybeRecordParseLocation(item_info.ast_location->location(),
                               resolved_order_by_item.get());
      if (language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
        GOOGLESQL_RETURN_IF_ERROR(
            CollationAnnotation::ResolveCollationForResolvedOrderByItem(
                resolved_order_by_item.get()));
      }
      resolved_order_by_items->push_back(std::move(resolved_order_by_item));

      if (!resolved_order_by_items->back()
               ->column_ref()
               ->type()
               ->SupportsOrdering(language(), /*type_description=*/nullptr)) {
        return MakeSqlErrorAt(item_info.ast_location)
               << "ORDER BY does not support expressions of type "
               << resolved_order_by_items->back()
                      ->column_ref()
                      ->type()
                      ->ShortTypeName(product_mode());
      }
    }
  }

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::MakeResolvedOrderByScan(
    const ASTHint* order_by_hint, const NameList& name_list,
    const std::vector<ResolvedColumn>& output_column_list,
    const OrderByItemInfoVectorList& order_by_info_lists, bool is_pipe_order_by,
    std::unique_ptr<const ResolvedScan>* scan) {
  std::vector<std::unique_ptr<const ResolvedOrderByItem>>
      resolved_order_by_items;

  GOOGLESQL_RETURN_IF_ERROR(ResolveOrderByItems(name_list, order_by_info_lists,
                                      is_pipe_order_by,
                                      &resolved_order_by_items));

  std::unique_ptr<ResolvedOrderByScan> order_by_scan =
      googlesql::MakeResolvedOrderByScan(output_column_list, std::move(*scan),
                                         std::move(resolved_order_by_items));
  order_by_scan->set_is_ordered(true);

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(order_by_hint, order_by_scan.get()));
  *scan = std::move(order_by_scan);

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveQueryExpression(
    const ASTQueryExpression* query_expr, const NameScope* scope,
    IdString query_alias, bool force_new_columns_for_projected_outputs,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list,
    const Type* inferred_type_for_query) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  switch (query_expr->node_kind()) {
    case AST_SELECT:
      return ResolveSelect(query_expr->GetAsOrDie<ASTSelect>(),
                           /*order_by=*/nullptr,
                           /*limit_offset=*/nullptr, scope, query_alias,
                           force_new_columns_for_projected_outputs,
                           inferred_type_for_query, output, output_name_list);

    case AST_SET_OPERATION:
      return ResolveSetOperation(query_expr->GetAsOrDie<ASTSetOperation>(),
                                 scope, inferred_type_for_query, output,
                                 output_name_list);

    case AST_QUERY:
      return ResolveQuery(query_expr->GetAsOrDie<ASTQuery>(), scope,
                          query_alias, output, output_name_list,
                          {.inferred_type_for_query = inferred_type_for_query});

    case AST_TABLE_CLAUSE:
      if (language().LanguageFeatureEnabled(
              FEATURE_TOP_LEVEL_TABLE_STATEMENTS)) {
        return ResolveTableClause(
            query_expr->GetAsOrDie<ASTTableClause>(), scope,
            /*resolve_argument_first=*/true,
            /*read_as_row_type_error_kind=*/"top-level TABLE statements",
            output, output_name_list);
      } else {
        return MakeSqlErrorAt(query_expr)
               << "TABLE statements are not supported";
      }

    case AST_ALIASED_QUERY_EXPRESSION:
      return ResolveAliasedQueryExpression(
          query_expr->GetAsOrDie<ASTAliasedQueryExpression>(), scope, output,
          output_name_list, inferred_type_for_query);
    case AST_GQL_QUERY: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          *output, ResolveGqlQuery(query_expr->GetAsOrDie<ASTGqlQuery>(), scope,
                                   output_name_list));
      return absl::OkStatus();
    }
    case AST_GQL_GRAPH_PATTERN_QUERY: {
      // This is a GQL EXISTS {graph_pattern} subquery, we need to expand it
      // to be a full query.
      GraphTableQueryResolver graph_query_resolver(catalog_, this);
      return graph_query_resolver.ResolveGqlGraphPatternQuery(
          query_expr->GetAs<ASTGqlGraphPatternQuery>(), scope, output,
          output_name_list);
    }
    case AST_GQL_LINEAR_OPS_QUERY: {
      // This is a GQL EXISTS {graph_linear_ops} subquery, we need to expand it
      // to be a full query.
      GraphTableQueryResolver graph_query_resolver(catalog_, this);
      return graph_query_resolver.ResolveGqlLinearOpsQuery(
          query_expr->GetAs<ASTGqlLinearOpsQuery>(), scope, output,
          output_name_list);
    }
    case AST_FROM_QUERY:
      return ResolveFromQuery(query_expr->GetAsOrDie<ASTFromQuery>(), scope,
                              output, output_name_list);

    default:
      break;
  }

  return MakeSqlErrorAt(query_expr) << "Unhandled query_expr:\n"
                                    << query_expr->DebugString();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveAliasedQueryExpression(
    const ASTAliasedQueryExpression* aliased_query, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list,
    const Type* inferred_type_for_query) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const IdString alias = aliased_query->alias()->GetAsIdString();

  std::shared_ptr<const NameList> query_name_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveQuery(
      aliased_query->query(), scope, alias, output, &query_name_list,
      {.inferred_type_for_query = inferred_type_for_query}));

  *output_name_list = std::make_shared<const NameList>();
  GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(
      aliased_query, alias, query_name_list, output_name_list));

  return absl::OkStatus();
}

// Note: inferred_type_for_query is not passed in.  In normal queries, it
// never propagates into a FROM clause.  For FROM queries with just a single
// table subquery, it could be applied, but this seems like an obscure case
// since there is little reason to write `FROM (SELECT ...);`.
absl::Status Resolver::ResolveFromQuery(
    const ASTFromQuery* from_query, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // The parser won't let ASTFromQuery through if the feature is disabled.
  GOOGLESQL_RET_CHECK(language().LanguageFeatureEnabled(FEATURE_PIPES));

  const ASTFromClause* from_clause = from_query->from_clause();
  GOOGLESQL_RET_CHECK(from_clause != nullptr);
  GOOGLESQL_RET_CHECK(from_clause->table_expression() != nullptr);

  std::shared_ptr<const NameList> name_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveTableExpression(
      from_clause->table_expression(),
      /*external_scope=*/scope, /*local_scope=*/scope,
      /*is_leftmost=*/true, /*on_rhs_of_right_or_full_join=*/false, output,
      &name_list));

  // If we have a non-value-table NameList with exactly one column, and
  // that column is a value table column, then we make a new NameList marked
  // as a value table.  This will make the final query result a value
  // table if this NameList becomes the final result.
  //
  // Value table resolution is inconsistent in ResolveTableExpression's cases.
  // Some cases set is_value_table on the outer NameList and some don't.
  if (!name_list->is_value_table() && name_list->num_columns() == 1 &&
      name_list->column(0).is_value_table_column()) {
    std::shared_ptr<NameList> new_name_list = name_list->Copy();
    GOOGLESQL_RETURN_IF_ERROR(new_name_list->SetIsValueTable());
    name_list = new_name_list;
  }

  *output_name_list = name_list;
  return absl::OkStatus();
}

static absl::StatusOr<WithModifierMode> ExtractWithModifierMode(
    const LanguageOptions& language, const ASTSelect* select) {
  if (select->with_modifier() == nullptr) {
    return WithModifierMode::NONE;
  }
  GOOGLESQL_RET_CHECK_NE(select->with_modifier()->identifier(), nullptr);
  std::vector<absl::string_view> allowed_with_identifiers;
  if (language.LanguageFeatureEnabled(FEATURE_ANONYMIZATION)) {
    allowed_with_identifiers.push_back(
        WithModifierModeToString(WithModifierMode::ANONYMIZATION));
  }
  if (language.LanguageFeatureEnabled(FEATURE_DIFFERENTIAL_PRIVACY)) {
    allowed_with_identifiers.push_back(
        WithModifierModeToString(WithModifierMode::DIFFERENTIAL_PRIVACY));
  }
  if (language.LanguageFeatureEnabled(FEATURE_AGGREGATION_THRESHOLD)) {
    allowed_with_identifiers.push_back(
        WithModifierModeToString(WithModifierMode::AGGREGATION_THRESHOLD));
  }
  if (allowed_with_identifiers.empty()) {
    return MakeSqlErrorAt(select->with_modifier()) << "Unexpected keyword WITH";
  }
  const absl::string_view select_with_identifier =
      select->with_modifier()->identifier()->GetAsStringView();
  GOOGLESQL_ASSIGN_OR_RETURN(
      WithModifierMode result, [&]() -> absl::StatusOr<WithModifierMode> {
        if (googlesql_base::CaseEqual(
                select_with_identifier,
                WithModifierModeToString(WithModifierMode::ANONYMIZATION)) &&
            language.LanguageFeatureEnabled(FEATURE_ANONYMIZATION)) {
          return WithModifierMode::ANONYMIZATION;
        }
        if (googlesql_base::CaseEqual(
                select_with_identifier,
                WithModifierModeToString(
                    WithModifierMode::DIFFERENTIAL_PRIVACY)) &&
            language.LanguageFeatureEnabled(FEATURE_DIFFERENTIAL_PRIVACY)) {
          return WithModifierMode::DIFFERENTIAL_PRIVACY;
        }
        if (googlesql_base::CaseEqual(
                select_with_identifier,
                WithModifierModeToString(
                    WithModifierMode::AGGREGATION_THRESHOLD)) &&
            language.LanguageFeatureEnabled(FEATURE_AGGREGATION_THRESHOLD)) {
          return WithModifierMode::AGGREGATION_THRESHOLD;
        }
        return MakeSqlErrorAt(select->with_modifier()->identifier())
               << "Invalid identifier after SELECT WITH; expected "
               << absl::StrJoin(allowed_with_identifiers, ", ")
               << " but got: " << select_with_identifier;
      }());

  if (select->distinct()) {
    return MakeSqlErrorAt(select)
           << "SELECT WITH " << absl::AsciiStrToUpper(select_with_identifier)
           << " does not support DISTINCT";
  }
  if (select->from_clause() == nullptr) {
    return MakeSqlErrorAt(select)
           << "SELECT without FROM clause cannot "
              "specify WITH "
           << absl::AsciiStrToUpper(select_with_identifier);
  }
  return result;
}

// Returns true if WITH <mode> is always an aggregate scan.
static constexpr bool IsAlwaysAggregateWithModifierMode(WithModifierMode mode) {
  return mode == WithModifierMode::AGGREGATION_THRESHOLD ||
         mode == WithModifierMode::ANONYMIZATION ||
         mode == WithModifierMode::DIFFERENTIAL_PRIVACY;
}

// Returns true if GROUP BY ALL should apply implicitly in this context.
static bool ShouldApplyImplicitGroupByAll(
    const LanguageOptions& language,
    const QueryResolutionInfo* query_resolution_info) {
  if ((query_resolution_info->IsGqlReturn() ||
       query_resolution_info->IsGqlWith()) &&
      query_resolution_info->HasAggregation() &&
      language.LanguageFeatureEnabled(FEATURE_SQL_GRAPH_RETURN_EXTENSIONS)) {
    return true;
  }
  return false;
}

absl::Status Resolver::ResolveSelect(
    const ASTSelect* select, const ASTOrderBy* order_by,
    const ASTLimitOffset* limit_offset, const NameScope* external_scope,
    IdString query_alias, bool force_new_columns_for_projected_outputs,
    const Type* inferred_type_for_query,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Do this first to generate errors early on unsupported features.
  GOOGLESQL_ASSIGN_OR_RETURN(WithModifierMode with_modifier_mode,
                   ExtractWithModifierMode(language(), select));

  std::unique_ptr<const ResolvedScan> scan;
  std::shared_ptr<const NameList> from_clause_name_list;

  GOOGLESQL_RETURN_IF_ERROR(ResolveFromClauseAndCreateScan(
      select, order_by, external_scope, &scan, &from_clause_name_list));

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectAfterFrom(
      select, order_by, limit_offset, external_scope, query_alias,
      select->from_clause() == nullptr ? SelectForm::kNoFrom
                                       : SelectForm::kClassic,
      with_modifier_mode, force_new_columns_for_projected_outputs,
      inferred_type_for_query, &scan, from_clause_name_list, output_name_list));

  *output = std::move(scan);
  return absl::OkStatus();
}

// Resolves a SELECT query/subquery, resolving all the expressions and
// generating scans necessary to produce its output (as defined by the
// SELECT list).  The logic is generally as follows:
//
// 1) Assume the caller has resolved the FROM clause, generated a scan for it,
//    and built a NameList for what comes out of it.
//
// 2) If present, resolve the WHERE clause against the FROM NameScope,
//    and generate a scan for it on top of the FROM scan.
//
// 3) Do resolution of the remaining clauses (the SELECT list,
//    GROUP BY, HAVING, QUALIFY and ORDER BY).  This step resolves all the
//    expressions (without creating scans), and collects information about the
//    expressions in QueryResolutionInfo.  The collected information includes
//    SELECT list columns and aliases, GROUP BY columns, aggregate
//    columns, analytic function columns, and ORDER BY columns.
//    At a high level, this is done by:
//
//    1. Resolve the SELECT list first pass.  This resolves expressions
//       against the FROM clause NameScope, including star and dot-star
//       expansion.  This first pass is necessary to allow the GROUP BY to
//       resolve against SELECT list aliases.
//    2. Resolve the QUALIFY clause first pass to detect any aggregations.
//    3. Resolve the GROUP BY expressions against SELECT list aliases and
//       the FROM clause NameScope.
//    4. Resolve the SELECT list second pass.  If GROUP BY is present, this
//       re-resolves expressions against a new NameScope that includes
//       grouped versions of the columns (expressions computed after
//       grouping/aggregation must reference the grouped versions of the
//       columns since the original columns are not visible after
//       aggregation).  In this new NameScope, non-grouped columns can still
//       be looked up by name but they provide errors if accessed.  There
//       are optimizations in place to avoid re-resolution of expressions
//       whenever possible.
//    5. Resolve the HAVING clause against another new post-grouped column
//       NameScope that includes SELECT list aliases.
//    6. Resolve the QUALIFY clause against same NameScope as HAVING.
//    7. Resolve ORDER BY expressions against this post-grouped column
//       NameScope (or if DISTINCT is present, then a post-DISTINCT
//       NameScope) that includes SELECT list aliases.
//
// 4) Generate all necessary remaining scans based on the information in
//    QueryResolutionInfo, in the following order:
//    1. PROJECT scan for dot-star columns
//    2. AGGREGATE scan
//    3. PROJECT scan for columns needed by HAVING or QUALIFY
//    4. FILTER scan for HAVING
//    5. PROJECT scan for columns needed by analytic functions
//    6. ANALYTIC scan
//    7. FILTER scan for QUALIFY
//    8. PROJECT scan if needed for DISTINCT
//    9. AGGREGATE scan for DISTINCT
//   10. PROJECT scan for columns needed by ORDER BY
//   11. ORDER BY scan
//   12. LIMIT OFFSET scan
//   13. PROJECT scan for AS STRUCT/PROTO
//   14. PROJECT scan for handling HINTs
//
// For a more detailed discussion, see (broken link).
//
// For some pipe operators (listed below), this is called from methods other
// than ResolveSelect.  We reuse this function because we are trying to share
// all the complex functionality for the SELECT list and GROUP BY and don't
// want two forks of that code.
//
// Resolving code for these clauses is not easily separable from others
// due to complex entanglement of SELECT, aggregation, window functions,
// etc in classic SQL queries.  For pipe operators, the logic becomes
// mostly no-ops since none of the other clauses are present.
//
// For pipe operators, the input scan and NameList come from the input pipe
// instead of a FROM clause.  The SelectForm enum in QueryResolutionInfo
// records which case we're in, and is used to restrict or modify behavior
// and give better error messages earlier.
//
// For pipe SELECT, called from ResolvePipeSelect:
//   * The ASTSelect has a SELECT list and nothing else.
//   * It resolves similarly to no-from-clause SELECTs. e.g. No aggregation.
//   * The result is one or more ProjectScans, and nothing else.
//
// For pipe AGGREGATE, called from ResolvePipeAggregate:
//   * The ASTSelect just has a SELECT list (representing aggregate expressions)
//     and optionally a GROUP BY.
//   * The SELECT list may be empty (if the GROUP BY is non-empty).
//   * Expressions must include aggregation.
//   * Window functions are not allowed.
//   * The final output includes the GROUP BY columns, followed by the SELECT
//     columns.
//   * Name scoping is simplified so the SELECT and GROUP BY expressions are
//     independent and cannot see each other.
//   * The result includes an AggregateScan, and sometimes ProjectScans.
//
// For pipe WINDOW, called from ResolvePipeWindow:
//   * The ASTSelect just has a SELECT list (representing window expressions).
//   * Expressions must include a window function.
//   * Aggregate functions are not allowed.
//   * The operator projects new columns onto an existing dataset, so the final
//     output includes the columns from the input NameList.
//   * The result includes an AnalyticScan, and sometimes ProjectScans.
//
// For RETURN, called from ResolveGqlReturn;
// and WITH, called from ResolvedGqlWith:
//    * The ASTSelect has a SELECT list with aggregate or non-aggregate
//      expressions, a distinctness modifier, and optionally a GROUP BY.
//    * GROUP BY ALL is supported. If a GROUP BY clause is not specified and
//      the SELECT list has aggregate expressions, GROUP BY ALL is implicit.
//    * GROUPING SETS/ROLLUP, and CUBE are not supported.
//    * Window functions are not allowed.
//    * The result includes zero or more AggregateScans (DISTINCT is resolved
//    * to an AggregateScan grouping by all keys), and sometimes ProjectScans.
absl::Status Resolver::ResolveSelectAfterFrom(
    const ASTSelect* select, const ASTOrderBy* order_by,
    const ASTLimitOffset* limit_offset, const NameScope* external_scope,
    IdString query_alias, SelectForm select_form,
    WithModifierMode with_modifier_mode,
    bool force_new_columns_for_projected_outputs,
    const Type* inferred_type_for_query,
    std::unique_ptr<const ResolvedScan>* scan,
    const std::shared_ptr<const NameList>& from_clause_name_list,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::unique_ptr<const NameScope> from_scan_scope(
      new NameScope(external_scope, from_clause_name_list));

  if (!language().LanguageFeatureEnabled(FEATURE_LATERAL_COLUMN_REFERENCES)) {
    if (select->where_clause() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveWhereClauseAndCreateScan(
          select->where_clause(), from_scan_scope.get(), scan));
    }
  }

  GOOGLESQL_RET_CHECK(select->select_list() != nullptr);
  GOOGLESQL_RET_CHECK_EQ(select->from_clause() != nullptr,
               select_form == SelectForm::kClassic);

  std::unique_ptr<QueryResolutionInfo> query_resolution_info =
      std::make_unique<QueryResolutionInfo>(this);
  query_resolution_info->set_with_modifier_mode(with_modifier_mode);
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();

  if (select->window_clause() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->analytic_resolver()->SetWindowClause(
        *select->window_clause()));
  }

  query_resolution_info->set_select_form(select_form);
  query_resolution_info->set_has_group_by(select->group_by() != nullptr);
  query_resolution_info->set_has_having(select->having() != nullptr);
  query_resolution_info->set_has_qualify(select->qualify() != nullptr);
  query_resolution_info->set_has_order_by(order_by != nullptr);

  // Remember the columns of current scan (after the FROM clause and WHERE, but
  // before SELECT).
  query_resolution_info->set_from_clause_name_list(from_clause_name_list);

  // For pipe EXTEND and WINDOW, all columns and names from the input pass
  // through, including pseudo-columns and range variables, so we preserve the
  // full input column_list.  We will also preserve the input NameList,
  // so the names from this list won't be used.
  if (query_resolution_info->IsPipeExtendOrWindow()) {
    const IdString unused_name = MakeIdString("$unused_name");
    for (const ResolvedColumn& column : (*scan)->column_list()) {
      query_resolution_info->pipe_extra_select_items()->push_back(
          PipeExtraSelectItem(unused_name, column));
    }
  }
  if (query_resolution_info->IsPipeOp() &&
      !language().LanguageFeatureEnabled(FEATURE_PIPE_NAMED_WINDOWS)) {
    query_resolution_info->analytic_resolver()->DisableNamedWindowRefs(
        query_resolution_info->SelectFormClauseName());
  }

  // We avoid passing down the inferred type to the SELECT list if we have a
  // SELECT AS query.
  const Type* inferred_type_for_select_list = nullptr;
  if (select->select_as() == nullptr) {
    inferred_type_for_select_list = inferred_type_for_query;
  }

  std::shared_ptr<NameList> updated_name_list;
  std::unique_ptr<NameScope> updated_name_scope;

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectListExprsFirstPass(
      select->select_list(), from_scan_scope.get(), from_clause_name_list,
      query_resolution_info.get(), query_alias, updated_name_scope,
      updated_name_list, inferred_type_for_select_list));

  std::unique_ptr<const ResolvedExpr> resolved_where_expr;
  static constexpr char kWhereClause[] = "WHERE clause";
  if (language().LanguageFeatureEnabled(FEATURE_LATERAL_COLUMN_REFERENCES) &&
      select->where_clause() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWhere(
        select->where_clause(), updated_name_scope.get(), kWhereClause,
        query_resolution_info.get(), &resolved_where_expr));
  }

  // At this point, query_resolution_info->HasGroupByOrAggregation()` reflects
  // either the presence of a `GROUP BY` clause, or the presence of aggregate
  // functions in the `SELECT` list. There could still be aggregate functions
  // in the `QUALIFY` clause that have not been detected yet. There may also be
  // aggregate functions in the `ORDER BY` clause, though that will be an error.
  // We need to detect the aggregation in `QUALIFY` before we continue because
  // subsequent tasks are handled differently for queries with aggregation or no
  // aggregation.
  if (!query_resolution_info->HasGroupByOrAggregation() &&
      select->qualify() != nullptr) {
    // QUALIFY may reference names in the from clause scope or explicit aliases
    // introduced in the SELECT list.
    std::shared_ptr<NameList> post_analytic_name_list(new NameList);
    for (const std::unique_ptr<SelectColumnState>& select_column_state :
         select_column_state_list->select_column_state_list()) {
      // If `contains_outer_group_rows_or_group_by_modifiers` is true, then
      // `HasGroupByOrAggregation` should have returned true.
      GOOGLESQL_RET_CHECK(!select_column_state
                     ->contains_outer_group_rows_or_group_by_modifiers);
      if (!select_column_state->alias.empty() &&
          !IsInternalAlias(select_column_state->alias)) {
        if (select_column_state->resolved_expr->Is<ResolvedColumnRef>()) {
          // The expression already resolved to a column (either correlated
          // or uncorrelated is ok), so just use it.
          GOOGLESQL_RETURN_IF_ERROR(post_analytic_name_list->AddColumn(
              select_column_state->alias,
              select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
                  ->column(),
              /*is_explicit=*/true));
        } else {
          GOOGLESQL_RET_CHECK(select_column_state->GetType() != nullptr);
          ResolvedColumn select_column(AllocateColumnId(), query_alias,
                                       select_column_state->alias,
                                       select_column_state->GetType());
          GOOGLESQL_RETURN_IF_ERROR(post_analytic_name_list->AddColumn(
              select_column_state->alias, select_column, /*is_explicit=*/true));
        }
      }
    }
    std::unique_ptr<NameScope> qualify_name_scope;
    GOOGLESQL_RETURN_IF_ERROR(from_scan_scope->CopyNameScopeWithOverridingNames(
        post_analytic_name_list, &qualify_name_scope));

    auto qualify_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        query_resolution_info.get(), qualify_name_scope.get(),
        ExprResolutionInfoOptions{
            .allows_aggregation =
                query_resolution_info->SelectFormAllowsAggregation(),
            .allows_analytic =
                query_resolution_info->SelectFormAllowsAnalytic(),
            .clause_name = query_resolution_info->SelectFormClauseName(),
            .top_level_ast_expr = select->qualify()->expression()});
    // We don't use this ResolvedExpr. For this first pass, we are only
    // resolving this for the side effect in query_resolution_info so that
    // subsequent steps get the correct value for `HasGroupByOrAggregation()`.
    std::unique_ptr<const ResolvedExpr> resolved_qualify_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(
        select->qualify()->expression(), qualify_expr_resolution_info.get(),
        &resolved_qualify_expr, /*inferred_type=*/nullptr));
    bool has_aggregation = query_resolution_info->HasAggregation();
    // Reset GroupByInfo. We will re-resolve the QUALIFY expression later with
    // the real input columns. We need to clear it here so that we don't end up
    // with duplicate entries in the group_by_expr_map after we re-resolve
    // QUALIFY.
    query_resolution_info->ClearGroupByInfo();
    query_resolution_info->SetHasAggregation(has_aggregation);
  }

  // Return an appropriate error for anonymization queries that don't perform
  // aggregation.
  // This check is required because we have to wait until after resolving the
  // SELECT list (first pass) to know if there are aggregate functions
  // present.
  if (IsAlwaysAggregateWithModifierMode(
          query_resolution_info->with_modifier_mode()) &&
      !query_resolution_info->HasGroupByOrAggregation()) {
    GOOGLESQL_RET_CHECK_GT(select->select_list()->columns().size(), 0);
    return MakeSqlErrorAt(select->select_list()->columns(0))
           << "You must use GROUP BY or aggregation function in a "
           << (query_resolution_info->IsPipeAggregate() ? "AGGREGATE"
                                                        : "SELECT")
           << " WITH " << WithModifierModeToString(with_modifier_mode)
           << " query, but it was absent";
  }

  bool has_lateral_references = false;
  for (const auto& select_column_state :
       select_column_state_list->select_column_state_list()) {
    if (select_column_state->lateral_reference_state
            ->is_referenced_laterally()) {
      has_lateral_references = true;
      break;
    }
  }

  if (has_lateral_references) {
    analyzer_output_properties_.AddFeatureLabel("lateral_alias_reference");
  }

  if (query_resolution_info->HasGroupByOrAggregation() &&
      (query_resolution_info->HasHavingOrQualifyOrOrderBy() ||
       has_lateral_references)) {
    // We have GROUP BY or aggregation in the SELECT list (we performed
    // first pass SELECT list expression resolution above), and we have
    // either HAVING, QUALIFY, ORDER BY or lateral references. This implies that
    // the expressions in HAVING or QUALIFY, ORDER BY or the SELECT columns
    // referencing other select columns (or the where clause), could reference
    // SELECT list aliases, which might need to resolve against either the pre-
    // or post- grouping version of the column. Consider:
    //
    //   SELECT key as foo
    //   FROM table
    //   GROUP BY key
    //   HAVING foo > 5
    //
    //   SELECT key as foo
    //   FROM table
    //   GROUP BY key
    //   HAVING sum(foo) > 5
    //
    // In the first query, HAVING 'foo' must resolve to the post-grouped version
    // of table.key since the HAVING gets applied after aggregation.  In the
    // second query, HAVING 'foo' must resolve to the pre-grouped version of
    // table.key since the aggregation function is applied on top of it.  To
    // address this, we need to assign and remember pre-grouped versions of
    // all the SELECT list columns that have non-internal aliases (since those
    // could get referenced in HAVING or QUALIFY or ORDER BY).
    GOOGLESQL_RETURN_IF_ERROR(AnalyzeSelectColumnsToPrecomputeBeforeAggregation(
        query_resolution_info.get()));
  }

  if (select->group_by() != nullptr) {
    if (select->group_by()->and_order_by()) {
      if (!query_resolution_info->IsPipeAggregate()) {
        return MakeSqlErrorAt(select->group_by())
               << "GROUP AND ORDER BY is not supported outside pipe AGGREGATE";
      }
      if (order_by != nullptr) {
        return MakeSqlErrorAt(select->group_by())
               << "Cannot use GROUP AND ORDER BY in a query that also "
                  "has ORDER BY";
      }
      query_resolution_info->set_group_by_has_and_order_by();
    }
    if (select->group_by()->all() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveGroupByAll(select->group_by(),
                                        from_scan_scope.get(),
                                        query_resolution_info.get()));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ResolveGroupByExprs(select->group_by(),
                                          from_scan_scope.get(),
                                          query_resolution_info.get()));
    }
  }

  if (select->group_by() == nullptr &&
      ShouldApplyImplicitGroupByAll(language(), query_resolution_info.get())) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveGroupByAll(select->group_by(), from_scan_scope.get(),
                                      query_resolution_info.get()));
  }
  // Now that GROUP BY clause is resolved, we can resolve expressions that were
  // skipped during 1st pass resolution.
  GOOGLESQL_RETURN_IF_ERROR(ResolveDeferredFirstPassSelectListExprs(
      from_scan_scope.get(), from_clause_name_list, query_resolution_info.get(),
      query_alias));

  if (!query_resolution_info->SelectFormAllowsAggregation()) {
    GOOGLESQL_RET_CHECK(!query_resolution_info->HasGroupByOrAggregation());
  }
  if (!query_resolution_info->SelectFormAllowsAnalytic()) {
    GOOGLESQL_RET_CHECK(!query_resolution_info->HasAnalytic());
  }
  if (query_resolution_info->IsPipeAggregate()) {
    GOOGLESQL_RET_CHECK(query_resolution_info->HasGroupByOrAggregation());
  } else if (query_resolution_info->IsPipeWindow()) {
    GOOGLESQL_RET_CHECK(query_resolution_info->HasAnalytic());
  }

  if (!query_resolution_info->HasGroupByOrAggregation() &&
      !query_resolution_info->HasAnalytic()) {
    // There is no GROUP BY, and no aggregation or analytic functions in the
    // SELECT list, so the initial resolution pass on the SELECT list is
    // final.  This will create ResolvedColumns for the SELECT columns, and
    // identify any columns necessary to precompute.  Once these SELECT list
    // ResolvedColumns are assigned, we avoid calling ResolveExpr() again in the
    // second pass.
    // TODO: We should be able to avoid the second pass on SELECT
    // list expressions even if there is analytic present, but we currently
    // reset the analytic resolver state before re-resolving the SELECT and
    // resolving the ORDER BY (which can contain analytic functions).
    // Fix this.
    GOOGLESQL_RETURN_IF_ERROR(FinalizeSelectColumnStateList(
        select->select_list(), query_alias,
        force_new_columns_for_projected_outputs, query_resolution_info.get(),
        select_column_state_list));
  }

  // Resolve the SELECT list against what comes out of the GROUP BY.
  // GROUP BY columns that are select list ordinals or aliases already
  // have resolved and updated column references.  Aggregate subexpressions
  // have also already been resolved.  Other expressions will resolve against
  // a GROUP BY name scope, with aggregate subexpressions mapped to their
  // already-resolved columns.

  // Create a new NameScope for what comes out of the AggregateScan.
  // It is derived from the FROM clause scope, allows column references to
  // resolve to the grouped versions of the columns, and provides errors
  // for column references that are not grouped by or aggregated.
  std::unique_ptr<const NameScope> group_by_scope;
  const NameScope* from_clause_or_group_by_scope = from_scan_scope.get();
  if (query_resolution_info->HasGroupByOrAggregation()) {
    // Create a new NameScope that reflects what names are and are not
    // available post-GROUP BY.
    GOOGLESQL_RETURN_IF_ERROR(CreatePostGroupByNameScope(
        from_scan_scope.get(), query_resolution_info.get(), &group_by_scope));
    from_clause_or_group_by_scope = group_by_scope.get();
  }

  // The analytic function resolver contains information collected during
  // the initial analysis of the SELECT list columns.  Reset and
  // re-initialize the analytic function resolver for second-pass SELECT
  // list resolution and ORDER BY resolution.
  //
  // Note that when we reset the analytic resolver here, we lose all of
  // the information about the currently resolved analytic expressions.
  // This implies that we *must* re-resolve all analytic expressions in
  // order to be able to generate appropriate AnalyticScans.
  if (query_resolution_info->HasAnalytic()) {
    query_resolution_info->ResetAnalyticResolver(this);
  }

  std::shared_ptr<NameList> final_project_name_list(new NameList);

  if (query_resolution_info->IsPipeExtendOrWindow()) {
    // For pipe EXTEND and WINDOW, we preserve all existing names from the
    // input table, including pseudo-columns and range variables.
    GOOGLESQL_RETURN_IF_ERROR(
        final_project_name_list->MergeFrom(*from_clause_name_list, select));
  } else {
    // Add the columns from pipe AGGREGATE group by items into the output
    // name list.  They'll show up first, before the aggregate columns.
    for (const PipeExtraSelectItem& item :
         *query_resolution_info->pipe_extra_select_items()) {
      GOOGLESQL_RETURN_IF_ERROR(final_project_name_list->AddColumn(
          item.alias, item.column, /*is_explicit=*/true));
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectListExprsSecondPass(
      query_alias, from_clause_or_group_by_scope, &final_project_name_list,
      query_resolution_info.get()));

  *output_name_list = final_project_name_list;

  // Create new NameLists for SELECT list columns/aliases, since they can be
  // referenced elsewhere in the query (in the GROUP BY, HAVING, and ORDER BY).
  // This NameList reflects post-grouped versions of SELECT list columns
  // (if grouping is present).
  std::shared_ptr<NameList> post_group_by_alias_name_list(new NameList);
  // This NameList reflects pre-grouped versions of SELECT list columns
  // (if grouping is present).
  std::shared_ptr<NameList> pre_group_by_alias_name_list(new NameList);
  // The 'error_name_targets' identify SELECT list aliases whose related
  // expressions contain aggregation or analytic functions.  These
  // NameTargets will be used in a NameScope for resolving HAVING clause
  // aggregate function arguments, where references to these SELECT
  // list aliases are invalid.
  IdStringHashMapCase<NameTarget> error_name_targets;
  std::set<IdString, IdStringCaseLess> select_column_aliases;
  for (const std::unique_ptr<SelectColumnState>& select_column_state :
       select_column_state_list->select_column_state_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CreateSelectNamelists(
        select_column_state.get(), post_group_by_alias_name_list.get(),
        pre_group_by_alias_name_list.get(), &error_name_targets,
        &select_column_aliases));
  }

  // The NameScope to use when resolving the HAVING and ORDER BY clauses.
  // Includes the GROUP BY scope, along with additional SELECT list aliases
  // for post-grouping versions of columns.
  // SELECT list aliases override any names in group_by_scope.
  std::unique_ptr<NameScope> having_and_order_by_scope;
  GOOGLESQL_RETURN_IF_ERROR(
      from_clause_or_group_by_scope->CopyNameScopeWithOverridingNames(
          post_group_by_alias_name_list, &having_and_order_by_scope));

  // The NameScope to use when resolving aggregate functions in the HAVING
  // and ORDER BY clauses.  It is the <from_scan_scope>, extended with
  // SELECT list aliases for pre-grouping columns.  The SELECT list aliases
  // override the names in <from_scan_scope>.
  std::unique_ptr<NameScope> original_select_list_and_from_scan_scope;
  GOOGLESQL_RETURN_IF_ERROR(from_scan_scope->CopyNameScopeWithOverridingNames(
      pre_group_by_alias_name_list, &original_select_list_and_from_scan_scope));

  // SELECT list aliases related to aggregate expressions are not valid
  // to access as aggregate function arguments, so update the
  // <original_select_list_and_from_scan_scope> to mark those aliases as
  // invalid to access.
  std::unique_ptr<NameScope> select_list_and_from_scan_scope;

  GOOGLESQL_RETURN_IF_ERROR(
      original_select_list_and_from_scan_scope
          ->CopyNameScopeWithOverridingNameTargets(
              error_name_targets, &select_list_and_from_scan_scope));

  // Analyze the HAVING.
  // TODO: Should probably move <resolved_having_expr> to
  // <query_resolution_info>.
  std::unique_ptr<const ResolvedExpr> resolved_having_expr;
  if (select->having() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveHavingExpr(select->having(), having_and_order_by_scope.get(),
                          select_list_and_from_scan_scope.get(),
                          query_resolution_info.get(), &resolved_having_expr));
  }

  std::unique_ptr<const ResolvedExpr> resolved_qualify_expr;
  if (select->qualify() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveQualifyExpr(
        select->qualify(), having_and_order_by_scope.get(),
        select_list_and_from_scan_scope.get(), query_resolution_info.get(),
        &resolved_qualify_expr));
  }

  // Analyze the ORDER BY.  If we have SELECT DISTINCT, we will resolve
  // the ORDER BY expressions after resolving the DISTINCT since we must
  // resolve the ORDER BY against post-DISTINCT versions of columns.
  if (order_by != nullptr && !select->distinct()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveOrderByExprs(
        order_by, having_and_order_by_scope.get(),
        select_list_and_from_scan_scope.get(), /*is_post_distinct=*/false,
        query_resolution_info.get()));
  }

  // We are done with analysis and can now build the remaining scans.
  // The current <scan> covers the FROM and WHERE clauses.  The remaining
  // scans are built on top of the current <scan>.
  GOOGLESQL_RETURN_IF_ERROR(AddRemainingScansForSelect(
      select, order_by, limit_offset, having_and_order_by_scope.get(),
      std::move(resolved_where_expr), std::move(resolved_having_expr),
      std::move(resolved_qualify_expr), query_resolution_info.get(),
      output_name_list, scan));

  // Exclude pipe EXTEND and WINDOW because they can include pseudo-columns,
  // which should be prunable.
  if (!query_resolution_info->IsPipeExtendOrWindow()) {
    // Any columns produced in a SELECT list (for the final query or any
    // subquery) count as referenced and cannot be pruned.
    RecordColumnAccess((*scan)->column_list());
  }

  // Some sanity checks.
  // Note that we cannot check that the number of columns in the
  // output_name_list is the same as the number in select_column_state_list,
  // because this is not true for SELECT AS STRUCT or SELECT AS PROTO.

  // All columns to compute have been consumed.
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->CheckComputedColumnListsAreEmpty());

  return absl::OkStatus();
}

absl::Status Resolver::ResolveModelTransformSelectList(
    const NameScope* input_scope, const ASTSelectList* select_list,
    const std::shared_ptr<const NameList>& input_cols_name_list,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>* transform_list,
    std::vector<std::unique_ptr<const ResolvedOutputColumn>>*
        transform_output_column_list,
    std::vector<std::unique_ptr<const ResolvedAnalyticFunctionGroup>>*
        transform_analytic_function_group_list) {
  auto query_info = std::make_unique<QueryResolutionInfo>(this);

  std::unique_ptr<NameScope> dummy_scope;
  std::shared_ptr<NameList> dummy_name_list;

  GOOGLESQL_RETURN_IF_ERROR(ResolveSelectListExprsFirstPass(
      select_list, input_scope, input_cols_name_list, query_info.get(),
      MakeIdString("$model"), dummy_scope, dummy_name_list));

  GOOGLESQL_RETURN_IF_ERROR(FinalizeSelectColumnStateList(
      select_list, kDummyTableId,
      /*force_new_columns_for_projected_outputs=*/false, query_info.get(),
      query_info->select_column_state_list()));

  // Creates ResolvedComputedColumn for each select column.
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      col_names;
  for (int i = 0; i < query_info->select_column_state_list()
                          ->select_column_state_list()
                          .size();
       ++i) {
    const SelectColumnState* select_column_state =
        query_info->select_column_state_list()->GetSelectColumnState(i);
    if (IsInternalAlias(select_column_state->alias)) {
      return MakeSqlErrorAt(select_column_state->ast_expr)
             << "Anonymous columns are disallowed in TRANSFORM clause. Please "
                "provide a column name";
    }
    if (select_column_state->expr_findings.has_aggregation) {
      return MakeSqlErrorAt(select_column_state->ast_expr)
             << "Aggregation functions are not supported in TRANSFORM clause";
    }
    if (!googlesql_base::InsertIfNotPresent(&col_names, select_column_state->alias)) {
      return MakeSqlErrorAt(select_column_state->ast_expr)
             << "Duplicate column aliases are disallowed in TRANSFORM clause";
    }
    if (select_column_state->expr_findings.has_analytic) {
      const std::vector<
          std::unique_ptr<AnalyticFunctionResolver::AnalyticFunctionGroupInfo>>&
          analytic_function_groups =
              query_info->analytic_resolver()->analytic_function_groups();
      for (const auto& analytic_function_group : analytic_function_groups) {
        if (analytic_function_group->ast_partition_by != nullptr ||
            analytic_function_group->ast_order_by != nullptr) {
          return MakeSqlErrorAt(select_column_state->ast_expr)
                 << "Analytic functions with a non-empty OVER() clause are "
                    "disallowed in the TRANSFORM clause";
        }
        // resolved_computed_columns could be empty after merging. We only add
        // non-empty ones for clarity.
        if (!analytic_function_group->resolved_computed_columns.empty()) {
          transform_analytic_function_group_list->push_back(
              MakeResolvedAnalyticFunctionGroup(
                  /*partition_by=*/nullptr, /*order_by=*/nullptr,
                  std::move(
                      analytic_function_group->resolved_computed_columns)));
        }
      }
    }

    if (select_column_state->resolved_expr != nullptr) {
      // This is a column reference without any computation.
      GOOGLESQL_RET_CHECK(
          select_column_state->resolved_expr->GetAs<ResolvedColumnRef>() !=
          nullptr)
          << "resolved_expr should be of type ResolvedColumnRef in "
             "ResolveModelTransformSelectList";
      const ResolvedColumnRef* resolved_col_ref =
          select_column_state->resolved_expr->GetAs<ResolvedColumnRef>();
      const ResolvedColumn resolved_col_cp(
          AllocateColumnId(),
          analyzer_options_.id_string_pool()->Make(
              resolved_col_ref->column().table_name()),
          analyzer_options_.id_string_pool()->Make(
              select_column_state->alias.ToString()),
          resolved_col_ref->column().type());
      transform_list->push_back(MakeResolvedComputedColumn(
          resolved_col_cp,
          MakeResolvedColumnRef(resolved_col_ref->column().type(),
                                resolved_col_ref->column(),
                                /*is_correlated=*/false)));
    } else {
      // This is a computed column.
      GOOGLESQL_RET_CHECK(select_column_state->resolved_computed_column != nullptr)
          << "resolved_computed_column cannot be nullptr in "
             "ResolveModelTransformSelectList when resolved_expr is nullptr";
      googlesql::ResolvedASTDeepCopyVisitor deep_copy_visitor;
      GOOGLESQL_RETURN_IF_ERROR(
          select_column_state->resolved_computed_column->expr()->Accept(
              &deep_copy_visitor));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> resolved_expr_copy,
                       deep_copy_visitor.ConsumeRootNode<ResolvedExpr>());
      transform_list->push_back(MakeResolvedComputedColumn(
          select_column_state->resolved_computed_column->column(),
          std::move(resolved_expr_copy)));
    }
    transform_output_column_list->push_back(MakeResolvedOutputColumn(
        select_column_state->alias.ToString(),
        transform_list->at(transform_list->size() - 1)->column()));
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveCreateModelAliasedQueryList(
    const ASTAliasedQueryList* aliased_query_list,
    std::vector<std::unique_ptr<const ResolvedCreateModelAliasedQuery>>*
        resolved_aliased_query_list) {
  IdStringHashSetCase alias_names;
  for (const ASTAliasedQuery* aliased_query :
       aliased_query_list->aliased_query_list()) {
    const IdString alias_id_string = aliased_query->alias()->GetAsIdString();
    const std::string alias_string = aliased_query->alias()->GetAsString();

    // Checks for duplicate aliases.
    if (!googlesql_base::InsertIfNotPresent(&alias_names, alias_id_string)) {
      return MakeSqlErrorAt(aliased_query->alias())
             << "Duplicate alias " << alias_string << " for aliased query list";
    }

    std::unique_ptr<const ResolvedScan> query_scan;
    std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;
    // Create model does not allow value table as input.
    bool is_value_table = false;
    // Each subquery is resolved independently.
    // We are using ResolveQueryAndOutputColumns to resolve the query which is
    // the same way as how create model is resolving its input query right now.
    // For create model statement, aliased queries are independent of each
    // other, and are essentially the same as create model's input query.
    // Therefore we can keep the behavior consistent across the places.
    GOOGLESQL_RETURN_IF_ERROR(ResolveQueryAndOutputColumns(
        aliased_query->query(), aliased_query->query(),
        /*pipe_input_name_list=*/nullptr,
        /*object_type=*/"MODEL",
        /*is_recursive_view=*/false, /*table_name_id_string=*/{},
        alias_id_string,
        /*view_explicit_column_list=*/nullptr, &query_scan, &is_value_table,
        &output_column_list, /*column_definition_list=*/nullptr));

    std::unique_ptr<const ResolvedCreateModelAliasedQuery>
        resolved_aliased_query = MakeResolvedCreateModelAliasedQuery(
            alias_string, std::move(query_scan), std::move(output_column_list));
    resolved_aliased_query_list->push_back(std::move(resolved_aliased_query));
  }
  return absl::OkStatus();
}

absl::Status Resolver::CreateSelectNamelists(
    const SelectColumnState* select_column_state,
    NameList* post_group_by_alias_name_list,
    NameList* pre_group_by_alias_name_list,
    IdStringHashMapCase<NameTarget>* error_name_targets,
    std::set<IdString, IdStringCaseLess>* select_column_aliases) {
  // Test expected invariant.
  GOOGLESQL_RET_CHECK(select_column_state->resolved_select_column.IsInitialized());

  // The alias names should be marked explicit, since they were either
  // explicitly in the query (SELECT col AS alias), were derived
  // from a path expression that was explicitly in the query
  // (SELECT table.col), or were internal (SELECT a+1) where it does
  // not matter.
  //
  // TODO: Consider only doing this for non-internal aliases
  // (i.e., if !IsInternalAlias(select_column_state->alias)), since only
  // those should be able to be referenced elsewhere in the query.  There
  // might be reasons why this will not work (for instance positional
  // references), so investigate this in a post-refactoring changelist.
  GOOGLESQL_RETURN_IF_ERROR(post_group_by_alias_name_list->AddColumn(
      select_column_state->alias, select_column_state->resolved_select_column,
      /*is_explicit=*/true));

  const ResolvedColumn target_column =
      select_column_state->HasPreGroupByResolvedColumn()
          ? select_column_state->resolved_pre_group_by_select_column
          : select_column_state->resolved_select_column;
  GOOGLESQL_RETURN_IF_ERROR(pre_group_by_alias_name_list->AddColumn(
      select_column_state->alias, target_column, /*is_explicit=*/true));
  if (select_column_state->expr_findings.has_aggregation ||
      select_column_state->expr_findings.has_analytic) {
    if (googlesql_base::ContainsKey(*select_column_aliases, select_column_state->alias)) {
      // There's already a SELECT list alias for this, so make the
      // NameTarget ambiguous as well.  Note that the NameTarget may or
      // may not exist yet in <error_name_targets>.
      googlesql_base::InsertOrUpdate(error_name_targets, select_column_state->alias,
                          NameTarget());
      return absl::OkStatus();
    }
    NameTarget name_target(target_column, /*is_explicit=*/true);
    name_target.SetAccessError(
        NameTarget::EXPLICIT_COLUMN,
        select_column_state->expr_findings.has_aggregation
            ? "Aggregations of aggregations are not allowed"
            : "Analytic functions cannot be arguments to aggregate functions");
    // This insert should succeed, since if there was already an entry for
    // this alias we would have handled the alias as ambiguous above.
    GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(error_name_targets,
                                      select_column_state->alias, name_target));
  } else {
    if (error_name_targets->contains(select_column_state->alias)) {
      // We saw a non-aggregate SELECT column with the same alias as
      // an aggregate/analytic SELECT column.  Ensure that the related
      // NameTarget is ambiguous.
      googlesql_base::InsertOrUpdate(error_name_targets, select_column_state->alias,
                          NameTarget());
      return absl::OkStatus();
    }

    // The only way for a SELECT column which depends on other SELECT columns
    // to have a pre-grouping column assigned and available is if it is a GROUP
    // BY column. Otherwise, it can only be a post-grouping column.
    if (select_column_state->lateral_reference_state
            ->has_lateral_references() &&
        !select_column_state->is_group_by_column) {
      // This column isn't available pre-grouping, so mark it as an error
      // target.
      NameTarget name_target(target_column, /*is_explicit=*/true);
      name_target.SetAccessError(
          NameTarget::EXPLICIT_COLUMN,
          "Post-grouping column cannot be used in arguments to aggregations");
      // This insert should succeed, since if there was already an entry for
      // this alias we would have handled the alias as ambiguous above.
      GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(
          error_name_targets, select_column_state->alias, name_target));
    }
  }
  select_column_aliases->insert(select_column_state->alias);
  return absl::OkStatus();
}

absl::Status Resolver::AnalyzeSelectColumnsToPrecomputeBeforeAggregation(
    QueryResolutionInfo* query_resolution_info) {
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();
  for (int idx = 0; idx < select_column_state_list->Size(); ++idx) {
    SelectColumnState* select_column_state =
        select_column_state_list->GetSelectColumnState(idx);
    // If the column has analytic or aggregation, then we do not compute
    // this before the AggregateScan.
    if (select_column_state->expr_findings.has_aggregation ||
        select_column_state->expr_findings.has_analytic) {
      continue;
    }

    // We should always process these columns even if there's no alias.
    // The only difference this could cause is changing some column ID
    // assignments in some trees. Keeping it for now until we migrate such
    // external tests that depend on the particular ID assignment.
    if (!IsInternalAlias(select_column_state->alias) ||
        select_column_state->lateral_reference_state
            ->IsInvolvedInLateralReferences()) {
      GOOGLESQL_RET_CHECK(select_column_state->resolved_expr != nullptr);
      ResolvedColumn pre_group_by_column;
      if (select_column_state->resolved_expr->node_kind() ==
          RESOLVED_COLUMN_REF) {
        // The expression already resolved to a column (either correlated
        // or uncorrelated is ok), so just use it.
        pre_group_by_column =
            select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
                ->column();
      } else {
        // The expression is not a simple column reference, it is a more
        // complicated expression that need to be computed before aggregation
        // if we GROUP BY that computed column.

        if (select_column_state->lateral_reference_state
                ->is_referenced_laterally()) {
          // Maintain the old name so that lateral references (e.g. from
          // aggregate args) can still find the column.
          GOOGLESQL_RET_CHECK(
              select_column_state->resolved_select_column.IsInitialized());
          pre_group_by_column = select_column_state->resolved_select_column;
          select_column_state->resolved_select_column = ResolvedColumn();
        } else {
          pre_group_by_column = ResolvedColumn(
              AllocateColumnId(), kPreGroupById, select_column_state->alias,
              select_column_state->resolved_expr->annotated_type());
        }

        // If the expression is a path expression then collect that
        // information in the QueryResolutionInfo so that we know that
        // accessing that path is valid post-GROUP BY, even if accessing
        // the source of the path is not.
        ResolvedColumn source_column;
        bool unused_is_correlated;
        ValidNamePath valid_name_path;
        if (GetSourceColumnAndNamePath(select_column_state->resolved_expr.get(),
                                       pre_group_by_column, &source_column,
                                       &unused_is_correlated, &valid_name_path,
                                       id_string_pool_)) {
          // We found a field access path, register it in QueryResolutionInfo.
          query_resolution_info->mutable_select_list_valid_field_info_map()
              ->InsertNamePath(source_column, valid_name_path);
        }

        if (!select_column_state->lateral_reference_state
                 ->IsInvolvedInLateralReferences()) {
          query_resolution_info
              ->select_list_columns_to_compute_before_aggregation()
              ->push_back(MakeResolvedComputedColumn(
                  pre_group_by_column,
                  std::move(select_column_state->resolved_expr)));
          // This column reference will be used when resolving the GROUP BY
          // expressions.
          select_column_state->resolved_expr =
              MakeColumnRef(pre_group_by_column);
          // Keep track of the resolved expr before it gets replaced with a
          // reference to a pre-aggregate compute column.
          select_column_state->original_resolved_expr =
              query_resolution_info
                  ->select_list_columns_to_compute_before_aggregation()
                  ->back()
                  ->expr();
        }
      }

      GOOGLESQL_RET_CHECK(pre_group_by_column.IsInitialized());
      select_column_state->resolved_pre_group_by_select_column =
          pre_group_by_column;
      GOOGLESQL_RET_CHECK(!select_column_state->resolved_select_column.IsInitialized());
    }
  }
  return absl::OkStatus();
}

class FindReferencedColumnsVisitor : public ResolvedASTVisitor {
 public:
  explicit FindReferencedColumnsVisitor(
      absl::flat_hash_set<ResolvedColumn>* columns)
      : columns_(columns) {}
  absl::Status DefaultVisit(const ResolvedNode* node) override {
    return node->ChildrenAccept(this);
  }

  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    columns_->insert(node->column());
    return absl::OkStatus();
  }

 private:
  absl::flat_hash_set<ResolvedColumn>* columns_;
};

absl::Status Resolver::ResolveQualifyExpr(
    const ASTQualify* qualify, const NameScope* having_and_order_by_scope,
    const NameScope* select_list_and_from_scan_scope,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_qualify_expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!language().LanguageFeatureEnabled(FEATURE_QUALIFY)) {
    return MakeSqlErrorAt(qualify) << "QUALIFY is not supported";
  }

  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      having_and_order_by_scope, select_list_and_from_scan_scope,
      having_and_order_by_scope,
      /*allows_aggregation_in=*/true, /*allows_analytic_in=*/true,
      query_resolution_info->HasAnalytic(), "QUALIFY clause",
      query_resolution_info);
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(qualify->expression(), expr_resolution_info.get(),
                              resolved_qualify_expr));

  GOOGLESQL_RET_CHECK(*resolved_qualify_expr != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(qualify->expression(), "QUALIFY clause",
                                   resolved_qualify_expr));

  if (!query_resolution_info->HasAnalytic()) {
    return MakeSqlErrorAt(qualify->expression())
           << "The QUALIFY clause requires analytic function to be present";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveHavingExpr(
    const ASTHaving* having, const NameScope* having_and_order_by_scope,
    const NameScope* select_list_and_from_scan_scope,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_having_expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Aggregation is only allowed if we already saw GROUP BY or aggregation.
  // In theory, the following is valid, though it's not very interesting:
  //   SELECT 1 from T HAVING sum(T.a) > 5;
  // Return an error for that case.  The only way I can think of how to
  // allow this is to do an initial resolution pass on HAVING to detect
  // aggregations, so that when we resolve the SELECT list above it
  // can detect errors when referencing non-grouped and
  // non-aggregated columns, such as:
  //   SELECT value FROM KeyValue HAVING sum(key) > 5;
  const bool already_saw_group_by_or_aggregation =
      query_resolution_info->HasGroupByOrAggregation();
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      having_and_order_by_scope, select_list_and_from_scan_scope,
      having_and_order_by_scope,
      /*allows_aggregation_in=*/true, /*allows_analytic_in=*/false,
      query_resolution_info->HasGroupByOrAggregation(), "HAVING clause",
      query_resolution_info);
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(having->expression(), expr_resolution_info.get(),
                              resolved_having_expr));

  GOOGLESQL_RET_CHECK(*resolved_having_expr != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(having->expression(), "HAVING clause",
                                   resolved_having_expr));

  if (!already_saw_group_by_or_aggregation &&
      query_resolution_info->HasGroupByOrAggregation()) {
    return MakeSqlErrorAt(having->expression())
           << "The HAVING clause only allows aggregation if GROUP BY or "
              "SELECT list aggregation is present";
  }
  // TODO: Should we move this up above, and simply bail
  // if HAVING is present without GROUP BY or aggregation in the
  // SELECT list?  We end up bailing anyway, but it is sort of nice
  // to detect the corner case and give a more specific error message
  // above.
  if (!query_resolution_info->HasGroupByOrAggregation()) {
    return MakeSqlErrorAt(having->expression())
           << "The HAVING clause requires GROUP BY or aggregation to "
              "be present";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveOrderByExprs(
    const ASTOrderBy* order_by, const NameScope* having_and_order_by_scope,
    const NameScope* select_list_and_from_scan_scope, bool is_post_distinct,
    QueryResolutionInfo* query_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Aggregation is only allowed if we already saw GROUP BY or aggregation.
  // In theory, we could support:
  // SELECT 1 from T ORDER BY sum(T.a) > 5;
  const bool already_saw_group_by_or_aggregation =
      query_resolution_info->HasGroupByOrAggregation();

  // TODO: Clean this up.  This is setting global state, in the
  // context of analyzing the ORDER BY clause.  This is ok because once we
  // get to resolving the ORDER BY expressions, we won't re-resolve an
  // analytic function where named windows are allowed.  Make sure we
  // have test coverage for this:
  // select agg() over (named_window)
  // from table
  // order by agg2() over (partition by a)
  // window named_window (partition by b)
  static const char clause_name[] = "ORDER BY clause";
  query_resolution_info->analytic_resolver()->DisableNamedWindowRefs(
      clause_name);

  // Aggregation is not allowed in ORDER BY if the query is SELECT DISTINCT.
  // Analytic functions are also currently disallowed after SELECT DISTINCT,
  // but could be allowed (it would require a bunch of additional analysis
  // logic though).  TODO: It is low priority but make this work.
  // Maybe wait until we get a feature request.
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      having_and_order_by_scope, select_list_and_from_scan_scope,
      having_and_order_by_scope,
      /*allows_aggregation_in=*/!is_post_distinct,
      /*allows_analytic_in=*/!is_post_distinct,
      query_resolution_info->HasGroupByOrAggregation(), clause_name,
      query_resolution_info);

  bool is_gql_order_by = query_resolution_info->IsGqlReturn();

  GOOGLESQL_RETURN_IF_ERROR(ResolveOrderingExprs(
      order_by->ordering_expressions(), expr_resolution_info.get(),
      /*allow_ordinals=*/!is_gql_order_by, "GQL",
      expr_resolution_info->query_resolution_info
          ->mutable_order_by_item_info()));

  GOOGLESQL_RETURN_IF_ERROR(AddColumnsForOrderByExprs(
      /*query_alias=*/kOrderById,
      query_resolution_info->mutable_order_by_item_info(),
      query_resolution_info->order_by_columns_to_compute()));

  if (!already_saw_group_by_or_aggregation &&
      query_resolution_info->HasGroupByOrAggregation()) {
    // Return an error for now.  The only way I can think of how to
    // allow this is to do an earlier pass over the ORDER BY to detect
    // aggregations, so that the select list can resolve against the
    // post grouped columns only so it can detect an error for a query
    // that looks like:
    //  SELECT value FROM KeyValue ORDER BY sum(key);
    return MakeSqlErrorAt(order_by)
           << "The ORDER BY clause only allows aggregation if GROUP BY or "
              "SELECT list aggregation is present";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveWhereClauseAndCreateScan(
    const ASTWhereClause* where_clause, const NameScope* from_scan_scope,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  static constexpr char kWhereClause[] = "WHERE clause";
  std::unique_ptr<const ResolvedExpr> resolved_where;

  // We set `query_resolution_info` to nullptr because we can only be here if
  // lateral references are not present, which is why we can roll the filter
  // into the input scan, without seeing the SELECT list. The
  // `query_resolution_info` is needed only to restrict lateral access to
  // pre-grouping versions of SELECT columns.
  GOOGLESQL_RETURN_IF_ERROR(ResolveWhere(where_clause, from_scan_scope, kWhereClause,
                               /*query_resolution_info=*/nullptr,
                               &resolved_where));

  std::vector<ResolvedColumn> tmp_column_list = (*current_scan)->column_list();
  *current_scan = MakeResolvedFilterScan(
      tmp_column_list, std::move(*current_scan), std::move(resolved_where));
  return absl::OkStatus();
}

absl::Status Resolver::FinalizeSelectColumnStateList(
    const ASTSelectList* ast_select_list, IdString query_alias,
    bool force_new_columns_for_projected_outputs,
    QueryResolutionInfo* query_resolution_info,
    SelectColumnStateList* select_column_state_list) {
  // TODO: Consider renaming SelectColumnStateList to
  // SelectListColumnStateInfo or similar.  It's just weird that we have
  // SelectColumnStateList that has a method select_column_state_list()
  // that returns a vector<SelectColumnsState*>.
  for (const std::unique_ptr<SelectColumnState>& select_column_state :
       select_column_state_list->select_column_state_list()) {
    if (!force_new_columns_for_projected_outputs &&
        select_column_state->resolved_expr->node_kind() ==
            RESOLVED_COLUMN_REF &&
        !select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
             ->is_correlated() &&
        !analyzer_options_.create_new_column_for_each_projected_output()) {
      // The expression was already resolved to a column.  If it was not
      // correlated, just use the column.
      const ResolvedColumn& select_column =
          select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
              ->column();
      select_column_state->resolved_select_column = select_column;
    } else if (select_column_state->lateral_reference_state
                   ->is_referenced_laterally()) {
      // The resolved_select_column is already assigned.
      // Other SELECT columns will reference this one. Some may even be
      // dot-star columns. If there's any necessary computation, place it on
      // the successive pre-aggregation list so they can find it.
      GOOGLESQL_RET_CHECK(select_column_state->resolved_select_column.IsInitialized())
          << "SEELCT column referenced laterally must have been assigned a "
             "resolved column already";
      if (!select_column_state->resolved_expr->Is<ResolvedColumnRef>() ||
          select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
              ->is_correlated()) {
        auto computed_column = MakeResolvedComputedColumn(
            select_column_state->resolved_select_column,
            std::move(select_column_state->resolved_expr));
        select_column_state->resolved_computed_column = computed_column.get();
        query_resolution_info
            ->successive_select_list_columns_to_compute_before_aggregation()
            ->push_back(std::move(computed_column));
      }
    } else {
      ResolvedColumn select_column(
          AllocateColumnId(), query_alias, select_column_state->alias,
          select_column_state->resolved_expr->annotated_type());
      std::unique_ptr<ResolvedComputedColumn> resolved_computed_column =
          MakeResolvedComputedColumn(
              select_column, std::move(select_column_state->resolved_expr));
      select_column_state->resolved_computed_column =
          resolved_computed_column.get();

      // TODO: Also do not include internal aliases, i.e.,
      // !IsInternalAlias(select_column_state->alias).  Do this in a
      // subsequent changelist, as it will impact where/when such columns
      // get PROJECTed.
      query_resolution_info->select_list_columns_to_compute()->push_back(
          std::move(resolved_computed_column));
      select_column_state->resolved_select_column = select_column;
    }

    select_column_state->is_finalized = true;

    DotStarSourceExprInfo* dot_star_source_expr_info =
        select_column_state->dot_star_source_expr_info;
    if (dot_star_source_expr_info != nullptr &&
        !dot_star_source_expr_info->is_finalized) {
      auto& resolved_expr = dot_star_source_expr_info->resolved_expr;
      GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
      if (!resolved_expr->Is<ResolvedColumnRef>() ||
          resolved_expr->GetAs<ResolvedColumnRef>()->is_correlated()) {
        auto* list = query_resolution_info
                         ->select_list_columns_to_compute_before_aggregation();
        if (select_column_state->lateral_reference_state
                ->is_referenced_laterally()) {
          list =
              query_resolution_info
                  ->successive_select_list_columns_to_compute_before_aggregation();  // NOLINT
        }
        list->push_back(MakeResolvedComputedColumn(
            select_column_state->dot_star_source_expr_info
                ->original_resolved_column,
            std::move(resolved_expr)));
      }

      // Reset so that other columns do not re-add the expression.
      dot_star_source_expr_info->is_finalized = true;
      // We should never need to check this expression again, since it's
      // marked as finalized.
      dot_star_source_expr_info->resolved_expr = nullptr;
    }
  }

  return absl::OkStatus();
}

IdString Resolver::ComputeSelectColumnAlias(
    const ASTSelectColumn* ast_select_column, int column_idx) const {
  IdString alias;
  if (ast_select_column->alias() != nullptr) {
    alias = ast_select_column->alias()->GetAsIdString();
  } else {
    alias = GetAliasForExpression(ast_select_column->expression());
    if (alias.empty()) {
      // Arbitrary locally unique name.
      alias = MakeIdString(absl::StrCat("$col", column_idx + 1));
    }
  }
  return alias;
}

// This struct represents sets of column names to be excluded or replaced.
// This is used for SELECT * EXCEPT (...) REPLACE (...) syntax.
// See (broken link).
struct ColumnReplacements {
  std::string DebugString() const {
    std::string debug_string;
    absl::StrAppend(&debug_string, "\nexcluded_columns: (",
                    absl::StrJoin(excluded_columns, ",", IdStringFormatter),
                    ")");
    absl::StrAppend(&debug_string, "\nreplaced_columns:");
    for (const auto& replaced_column : replaced_columns) {
      absl::StrAppend(&debug_string, "\n  (",
                      replaced_column.first.ToStringView(), ",",
                      (replaced_column.second == nullptr
                           ? "null"
                           : replaced_column.second->DebugString()),
                      ")");
    }
    return debug_string;
  }

  // Column names that should be skipped.
  IdStringHashSetCase excluded_columns;

  // Column names that should be replaced.  Each should get used only once.
  // This map stores a single SelectColumnState that will be used as a
  // replacement.  When the replacement happens, that object is moved out and
  // the map will store NULL.  Callers should assert that they don't find a
  // NULL in this map.
  IdStringHashMapCase<std::unique_ptr<SelectColumnState>> replaced_columns;
};

// Check if <column_name> should be excluded or replaced according to
// <column_replacements> and <excluded_field_names> (both of which may be NULL).
// Return true if the caller should skip adding this column.
// For replaces, the replacement column will have been added
// to <select_column_state_list>.
//
// <excluded_field_names> comes from `NamedColumn::excluded_field_names` from
// the NameScope item for a value table.
// <column_replacements> comes from EXCEPT and/or REPLACE on star expansion.
static bool ExcludeOrReplaceColumn(
    const ASTExpression* ast_expression, IdString column_name,
    const IdStringSetCase* excluded_field_names,
    ColumnReplacements* column_replacements,
    SelectColumnStateList* select_column_state_list,
    DotStarSourceExprInfo* original_column_dot_star_source_expr_info) {
  if (excluded_field_names != nullptr &&
      googlesql_base::ContainsKey(*excluded_field_names, column_name)) {
    return true;
  }
  if (column_replacements == nullptr) {
    return false;
  }
  if (column_replacements->excluded_columns.contains(column_name)) {
    return true;
  }
  if (column_replacements->replaced_columns.contains(column_name)) {
    auto& replacement_column =
        googlesql_base::FindOrDie(column_replacements->replaced_columns, column_name);
    ABSL_DCHECK(replacement_column != nullptr);
    ABSL_DCHECK(replacement_column->dot_star_source_expr_info == nullptr);
    // Set the dot-star source expression info from the original column. This is
    // so that we resolve and place the dot-star source expression in some
    // projection list even if all columns get replaced, to ensure correlated
    // column references in the dot-star source expressions are referenced in
    // the current subquery.
    replacement_column->dot_star_source_expr_info =
        original_column_dot_star_source_expr_info;
    select_column_state_list->AddSelectColumn(std::move(replacement_column));
    // I'd use GOOGLESQL_RET_CHECK here, except then I'd have to return StatusOr<bool>.
    ABSL_DCHECK(select_column_state_list->select_column_state_list().back() !=
           nullptr);
    return true;
  }
  return false;
}

absl::Status Resolver::AddNameListToSelectList(
    const ASTSelectColumn* ast_select_column,
    const std::shared_ptr<const NameList>& name_list,
    const CorrelatedColumnsSetList& correlated_columns_set_list,
    bool ignore_excluded_value_table_fields,
    QueryResolutionInfo* query_resolution_info,
    ColumnReplacements* column_replacements) {
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();
  const ASTExpression* ast_expression = ast_select_column->expression();
  const size_t orig_num_columns = select_column_state_list->Size();
  for (const NamedColumn& named_column : name_list->columns()) {
    // Process exclusions first because MakeColumnRef will add columns
    // to referenced_columns_ and then they cannot be pruned.
    if (!named_column.is_value_table_column() &&
        ExcludeOrReplaceColumn(
            ast_expression, named_column.name(),
            /*excluded_field_names=*/nullptr, column_replacements,
            select_column_state_list,
            /*original_column_dot_star_source_expr_info=*/nullptr)) {
      continue;
    }

    std::unique_ptr<const ResolvedColumnRef> column_ref =
        MakeColumnRefWithCorrelation(named_column.column(),
                                     correlated_columns_set_list);
    if (named_column.is_value_table_column()) {
      // For value tables with fields, SELECT * expands to those fields
      // rather than showing the container value.
      // For scalar-valued value tables, or values with zero fields,
      // we'll just get the value rather than its fields.
      GOOGLESQL_RET_CHECK(!named_column.name().empty());

      // Pass the same column ref itself as the dot-star source expression.
      GOOGLESQL_ASSIGN_OR_RETURN(auto column_ref_copy,
                       ResolvedASTDeepCopyVisitor::Copy(column_ref.get()));

      GOOGLESQL_RETURN_IF_ERROR(AddColumnFieldsToSelectList(
          ast_select_column, column_ref.get(),
          /*resolved_src_expr=*/std::move(column_ref_copy),
          ExprFindings{.has_aggregation = false,
                       .has_analytic = false,
                       .has_volatile = false},
          /*columns_referenced_laterally=*/{},
          /*column_alias_if_no_fields=*/named_column.name(),
          (ignore_excluded_value_table_fields
               ? &named_column.excluded_field_names()
               : nullptr),
          query_resolution_info, column_replacements));
    } else {
      select_column_state_list->AddSelectColumn(
          ast_select_column, named_column.name(), named_column.is_explicit(),
          ExprFindings{.has_aggregation = false,
                       .has_analytic = false,
                       .has_volatile = false},
          std::move(column_ref),
          /*dot_star_source_expr_info=*/nullptr);
    }
  }
  // Detect if the * ended up expanding to zero columns after applying EXCEPT,
  // and treat that as an error.
  if (orig_num_columns == select_column_state_list->Size()) {
    GOOGLESQL_RET_CHECK(column_replacements != nullptr &&
              !column_replacements->excluded_columns.empty());
    return MakeSqlErrorAt(ast_expression)
           << "SELECT * expands to zero columns after applying EXCEPT";
  }
  return absl::OkStatus();
}

// static.
std::string Resolver::ColumnAliasOrPosition(IdString alias, int column_pos) {
  return IsInternalAlias(alias) ? absl::StrCat(1 + column_pos)
                                : alias.ToString();
}

// If 'resolved_expr' is a resolved path expression (zero or more
// RESOLVED_GET_*_FIELD expressions over a ResolvedColumnRef), then insert
// a new entry into 'query_resolution_info->group_by_valid_field_info_map'
// with a source ResolvedColumn that is the 'resolved_expr' source
// ResolvedColumnRef column, the name path derived from the 'resolved_expr'
// get_*_field expressions, along with the 'target_column'.
// If 'resolved_expr' is not a resolved path expression, then this has no
// effect.
absl::Status Resolver::CollectResolvedPathExpressionInfoIfRelevant(
    QueryResolutionInfo* query_resolution_info,
    const ResolvedExpr* resolved_expr, ResolvedColumn target_column) const {
  ResolvedColumn source_column;
  bool unused_is_correlated;
  ValidNamePath valid_name_path;
  if (!GetSourceColumnAndNamePath(resolved_expr, target_column, &source_column,
                                  &unused_is_correlated, &valid_name_path,
                                  id_string_pool_)) {
    return absl::OkStatus();
  }

  // The 'source_column' might itself come from resolving a path expression.
  // If so, then we need to merge the ValidNamePath that produced the
  // 'source_column' with 'valid_name_path'.  This gives us a resulting
  // ValidNamePath that relates the original ValidNamePath source column
  // through a full (concatenated) path name list to the 'target_column'.
  ResolvedColumn new_source_column = source_column;
  for (const auto& entry :
       query_resolution_info->select_list_valid_field_info_map().map()) {
    const ValidNamePathList* select_list_valid_name_path_list =
        entry.second.get();
    GOOGLESQL_RET_CHECK(select_list_valid_name_path_list != nullptr);
    bool found = false;
    for (const ValidNamePath& select_list_valid_name_path :
         *select_list_valid_name_path_list) {
      if (source_column == select_list_valid_name_path.target_column()) {
        const size_t total_name_path_size =
            valid_name_path.name_path().size() +
            select_list_valid_name_path.name_path().size();
        std::vector<IdString> new_name_path;
        new_name_path.reserve(total_name_path_size);
        valid_name_path.mutable_name_path()->reserve(total_name_path_size);
        new_name_path.insert(new_name_path.end(),
                             select_list_valid_name_path.name_path().begin(),
                             select_list_valid_name_path.name_path().end());
        new_name_path.insert(new_name_path.end(),
                             valid_name_path.name_path().begin(),
                             valid_name_path.name_path().end());
        valid_name_path.set_name_path(new_name_path);
        new_source_column = entry.first;
        found = true;
        break;
      }
    }
    if (found) {
      // There should only be one match, so if we find it then we are
      // done.
      break;
    }
  }
  query_resolution_info->mutable_group_by_valid_field_info_map()
      ->InsertNamePath(new_source_column, valid_name_path);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveSelectDistinct(
    const ASTSelect* select, SelectColumnStateList* select_column_state_list,
    const NameList* input_name_list,
    std::unique_ptr<const ResolvedScan>* current_scan,
    QueryResolutionInfo* query_resolution_info,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // For DISTINCT processing, we will build and maintain a mapping from
  // SELECT list expressions to post-DISTINCT versions of columns.  This
  // mapping will be used when resolving ORDER BY expression after the
  // DISTINCT.

  // DISTINCT processing will re-use the GROUP BY information, so clear out
  // any old GROUP BY information that may exist.  But first, we must
  // preserve the mapping from SELECT list expressions to post-GROUP BY
  // columns so that we can update it during DISTINCT processing to capture the
  // mapping from SELECT list expressions to post-DISTINCT columns instead.
  query_resolution_info->ClearGroupByInfo();

  GOOGLESQL_RET_CHECK_EQ(select_column_state_list->Size(),
               input_name_list->num_columns());

  std::shared_ptr<NameList> name_list(new NameList);

  for (int column_pos = 0; column_pos < input_name_list->num_columns();
       ++column_pos) {
    const NamedColumn& named_column = input_name_list->column(column_pos);
    const ResolvedColumn& column = named_column.column();
    SelectColumnState* select_column_state =
        select_column_state_list->GetSelectColumnState(column_pos);
    const ASTNode* ast_column_location = select_column_state->ast_expr;

    std::string no_grouping_type;
    if (!column.type()->SupportsGrouping(language(), &no_grouping_type)) {
      return MakeSqlErrorAt(ast_column_location)
             << "Column "
             << ColumnAliasOrPosition(named_column.name(), column_pos)
             << " of type " << no_grouping_type << " cannot be used in "
             << (query_resolution_info->select_form() == SelectForm::kGqlReturn
                     ? "RETURN DISTINCT"
                 : query_resolution_info->select_form() == SelectForm::kGqlWith
                     ? "WITH DISTINCT"
                     : "SELECT DISTINCT");
    }

    const ResolvedExpr* resolved_expr =
        select_column_state->resolved_expr.get();
    if (resolved_expr == nullptr) {
      GOOGLESQL_RET_CHECK(select_column_state->resolved_computed_column != nullptr)
          << select_column_state->DebugString();
      resolved_expr = select_column_state->resolved_computed_column->expr();
    }
    GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
    const ResolvedExpr* pre_group_by_expr =
        GetPreGroupByResolvedExpr(select_column_state);

    ResolvedColumn distinct_column;
    const ResolvedComputedColumn* existing_computed_column =
        query_resolution_info->GetEquivalentGroupByComputedColumnOrNull(
            resolved_expr);
    if (existing_computed_column == nullptr) {
      // We could not find a computed column that matches the original
      // select column state expression.  Now look for a computed column
      // that matches a reference to <column>, since if a previous SELECT
      // list computed column was created in this method then it was
      // added in the AddGroupByComputedColumnIfNeeded() call below.  We
      // detect that duplicate here so that we can re-use it, and do not
      // need to compute another column for it.
      std::unique_ptr<ResolvedColumnRef> column_ref = MakeColumnRef(column);
      existing_computed_column =
          query_resolution_info->GetEquivalentGroupByComputedColumnOrNull(
              column_ref.get());
    }
    if (existing_computed_column != nullptr) {
      // Reference the existing column rather than recompute the expression.
      distinct_column = existing_computed_column->column();
    } else {
      // Create a new DISTINCT column.
      const IdString* query_alias = &kDistinctId;
      if (query_resolution_info->IsGqlReturn()) {
        query_alias = &kGraphTableId;
      }
      distinct_column =
          ResolvedColumn(AllocateColumnId(), *query_alias, column.name_id(),
                         column.annotated_type());
      // Add a computed column for the new post-DISTINCT column.
      const ResolvedComputedColumn* new_computed_column =
          query_resolution_info->AddGroupByComputedColumnIfNeeded(
              distinct_column, MakeColumnRef(column), pre_group_by_expr,
              /*override_existing_column=*/false);
      if (new_computed_column != nullptr && ast_column_location != nullptr) {
        MaybeRecordParseLocation(
            ast_column_location,
            const_cast<ResolvedExpr*>(new_computed_column->expr()));
      }
    }

    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(named_column.name(), distinct_column,
                                         named_column.is_explicit()));
    // Update the SelectListColumnState with the new post-DISTINCT
    // ResolvedColumn information.
    select_column_state->resolved_select_column = distinct_column;

    // Store the mapping of pre-DISTINCT to post-DISTINCT column.
    query_resolution_info->mutable_group_by_valid_field_info_map()
        ->InsertNamePath(column, {/*name_path=*/{}, distinct_column});

    // If the 'resolved_expr' is a path expression, we must collect
    // information in the 'query_resolution_info' about that path
    // expression and its relationship to the 'distinct_column'.
    // This information will get used later when constructing a
    // NameScope for what names are valid post-DISTINCT, where the
    // new NameScope is used when resolving a subsequent ORDER BY.
    // If the 'resolved_expr' is not a path expression then this
    // is a no-op.
    GOOGLESQL_RETURN_IF_ERROR(CollectResolvedPathExpressionInfoIfRelevant(
        query_resolution_info, resolved_expr, distinct_column));
  }

  *output_name_list = name_list;

  // Set the query resolution context so subsequent ORDER BY expression
  // resolution will know it is in the context of post-DISTINCT processing.
  query_resolution_info->set_is_post_distinct(true);

  if (query_resolution_info->scoped_aggregation_state()->row_range_determined) {
    GOOGLESQL_RET_CHECK(!query_resolution_info->scoped_aggregation_state()
                   ->target_pattern_variable_ref.has_value());
  }

  // Call pin anyway to ensure the full range list is created, in case we
  // released it earlier.
  GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->PinToRowRange(std::nullopt));
  return AddAggregateScan(select, /*is_for_select_distinct=*/true,
                          query_resolution_info, current_scan);
}

absl::Status Resolver::ResolveSelectStarModifiers(
    const ASTSelectColumn* ast_select_column, const ASTStarModifiers* modifiers,
    const NameList* name_list_for_star, const Type* type_for_star,
    const NameScope* scope, QueryResolutionInfo* query_resolution_info,
    ColumnReplacements* column_replacements) {
  GOOGLESQL_RET_CHECK(name_list_for_star != nullptr || type_for_star != nullptr);
  GOOGLESQL_RET_CHECK(name_list_for_star == nullptr || type_for_star == nullptr);
  GOOGLESQL_RET_CHECK(modifiers != nullptr);

  const ASTStarExceptList* except_list = modifiers->except_list();
  const absl::Span<const ASTStarReplaceItem* const>& replace_items =
      modifiers->replace_items();

  if (!language().LanguageFeatureEnabled(FEATURE_SELECT_STAR_EXCEPT_REPLACE)) {
    if (except_list != nullptr) {
      return MakeSqlErrorAt(ast_select_column)
             << "SELECT * EXCEPT is not supported";
    } else {
      return MakeSqlErrorAt(ast_select_column)
             << "SELECT * REPLACE is not supported";
    }
  }

  if (except_list != nullptr) {
    for (const ASTIdentifier* ast_identifier : except_list->identifiers()) {
      const IdString identifier = ast_identifier->GetAsIdString();
      if (IsInternalAlias(identifier)) {
        return MakeSqlErrorAt(ast_identifier)
               << "Cannot use EXCEPT with internal alias "
               << ToIdentifierLiteral(identifier);
      }
      const Type::HasFieldResult has_field =
          name_list_for_star != nullptr
              ? name_list_for_star->SelectStarHasColumn(identifier)
              : type_for_star->HasField(identifier.ToString(),
                                        /*field_id=*/nullptr,
                                        /*include_pseudo_fields=*/false);
      switch (has_field) {
        case Type::HAS_NO_FIELD:
          return MakeSqlErrorAt(ast_identifier)
                 << "Column " << ToIdentifierLiteral(identifier)
                 << " in SELECT * EXCEPT list does not exist";
        case Type::HAS_FIELD:
        case Type::HAS_AMBIGUOUS_FIELD:  // Duplicate columns ok for EXCEPT.
          break;
        case Type::HAS_PSEUDO_FIELD:
          // SelectStarHasColumn can never return HAS_PSEUDO_FIELD.
          // HasField with include_pseudo_fields=false can never return
          // HAS_PSEUDO_FIELD either.
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected Type::HAS_PSEUDO_FIELD value";
          break;
      }
      if (!googlesql_base::InsertIfNotPresent(&column_replacements->excluded_columns,
                                   identifier)) {
        return MakeSqlErrorAt(ast_identifier)
               << "Duplicate column " << ToIdentifierLiteral(identifier)
               << " in SELECT * EXCEPT list";
      }
    }
  }
  for (const ASTStarReplaceItem* ast_replace_item : replace_items) {
    auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        query_resolution_info, scope,
        ExprResolutionInfoOptions{
            .allows_aggregation =
                query_resolution_info->SelectFormAllowsAggregation(),
            .allows_analytic =
                query_resolution_info->SelectFormAllowsAnalytic(),
            .clause_name = query_resolution_info->SelectFormClauseName(),
            .top_level_ast_expr = ast_replace_item->expression(),
            .column_alias = ast_replace_item->alias()->GetAsIdString()});
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_replace_item->expression(),
                                expr_resolution_info.get(), &resolved_expr));
    if (expr_resolution_info->findings.has_analytic) {
      // TODO This is disallowed only because it doesn't work in
      // the current implementation, because of a problem that occurs later.
      return MakeSqlErrorAt(ast_replace_item->expression())
             << "Cannot use analytic functions inside SELECT * REPLACE";
    }

    const IdString identifier = ast_replace_item->alias()->GetAsIdString();
    if (IsInternalAlias(identifier)) {
      return MakeSqlErrorAt(ast_replace_item)
             << "Cannot use REPLACE with internal alias "
             << ToIdentifierLiteral(identifier);
    }
    if (column_replacements->excluded_columns.contains(identifier)) {
      return MakeSqlErrorAt(ast_replace_item->alias())
             << "Column " << ToIdentifierLiteral(identifier)
             << " cannot occur in both SELECT * EXCEPT and REPLACE";
    }
    const Type::HasFieldResult has_field =
        name_list_for_star != nullptr
            ? name_list_for_star->SelectStarHasColumn(identifier)
            : type_for_star->HasField(identifier.ToString(),
                                      /*field_id=*/nullptr,
                                      /*include_pseudo_fields=*/false);
    switch (has_field) {
      case Type::HAS_NO_FIELD:
        return MakeSqlErrorAt(ast_replace_item->alias())
               << "Column " << ToIdentifierLiteral(identifier)
               << " in SELECT * REPLACE list does not exist";
      case Type::HAS_FIELD:
        break;
      case Type::HAS_AMBIGUOUS_FIELD:
        return MakeSqlErrorAt(ast_replace_item->alias())
               << "Column " << ToIdentifierLiteral(identifier)
               << " in SELECT * REPLACE list is ambiguous";
      case Type::HAS_PSEUDO_FIELD:
        // SelectStarHasColumn can never return HAS_PSEUDO_FIELD.
        // HasField with include_pseudo_fields=false can never return
        // HAS_PSEUDO_FIELD either.
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected Type::HAS_PSEUDO_FIELD value";
    }

    auto select_column_state = std::make_unique<SelectColumnState>(
        ast_select_column, identifier,
        /*is_explicit=*/true, expr_resolution_info->findings,
        std::move(resolved_expr),
        std::move(expr_resolution_info->columns_referenced_laterally),
        /*dot_star_source_expr_info=*/nullptr);

    // Override the ast_expr to point at the replacement expression
    // rather than `ast_select_column`, which points at the star expression.
    select_column_state->ast_expr = ast_replace_item->expression();
    // If order suffixes become possible here, make sure we get the right one.
    GOOGLESQL_RET_CHECK(select_column_state->ast_grouping_item_order == nullptr);

    if (!column_replacements->replaced_columns
             .emplace(identifier, std::move(select_column_state))
             .second) {
      return MakeSqlErrorAt(ast_replace_item->alias())
             << "Duplicate column " << ToIdentifierLiteral(identifier)
             << " in SELECT * REPLACE list";
    }
  }

  return absl::OkStatus();
}

// NOTE: The behavior of star expansion here must match
// NameList::SelectStarHasColumn.
absl::Status Resolver::ResolveSelectStar(
    const ASTSelectColumn* ast_select_column,
    const std::shared_ptr<const NameList>& from_clause_name_list,
    const NameScope* from_scan_scope,
    QueryResolutionInfo* query_resolution_info) {
  const ASTExpression* ast_select_expr = ast_select_column->expression();

  std::string clause_name = "SELECT";
  if (query_resolution_info->IsGqlReturn()) {
    clause_name = "RETURN";
  }
  if (query_resolution_info->IsGqlWith()) {
    clause_name = "WITH";
  }

  if (in_strict_mode()) {
    return MakeSqlErrorAt(ast_select_expr) << absl::StrCat(
               clause_name, " * is not allowed in strict name resolution mode");
  }
  if (!query_resolution_info->SelectFormAllowsSelectStar()) {
    return MakeSqlErrorAt(ast_select_expr)
           << absl::StrCat(clause_name, " * must have a FROM clause");
  }
  if (from_clause_name_list->num_columns() == 0) {
    return MakeSqlErrorAt(ast_select_expr)
           << absl::StrCat(clause_name, " * would expand to zero columns");
  }

  // Process SELECT * EXCEPT(...) REPLACE(...) if present.
  ColumnReplacements column_replacements;
  if (ast_select_expr->node_kind() == AST_STAR_WITH_MODIFIERS) {
    const ASTStarWithModifiers* ast_node =
        ast_select_expr->GetAsOrDie<ASTStarWithModifiers>();
    GOOGLESQL_RETURN_IF_ERROR(ResolveSelectStarModifiers(
        ast_select_column, ast_node->modifiers(), from_clause_name_list.get(),
        /*type_for_star=*/nullptr, from_scan_scope, query_resolution_info,
        &column_replacements));
  }

  const CorrelatedColumnsSetList correlated_columns_set_list;
  GOOGLESQL_RETURN_IF_ERROR(AddNameListToSelectList(
      ast_select_column, from_clause_name_list, correlated_columns_set_list,
      /*ignore_excluded_value_table_fields=*/true, query_resolution_info,
      &column_replacements));

  return absl::OkStatus();
}

static absl::Status MakeErrorIfTypeDotStarHasNoFields(
    const ASTNode* ast_location, const Type* type, ProductMode product_mode) {
  if (!type->HasAnyFields()) {
    if (type->IsStruct()) {
      return MakeSqlErrorAt(ast_location)
             << "Star expansion is not allowed on a struct with zero fields";
    } else if (type->IsProto()) {
      return MakeSqlErrorAt(ast_location)
             << "Star expansion is not allowed on proto "
             << type->AsProto()->descriptor()->full_name()
             << " which has zero fields";
    }
    return MakeSqlErrorAt(ast_location) << "Dot-star is not supported for type "
                                        << type->ShortTypeName(product_mode);
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveSelectDotStar(
    const ASTSelectColumn* ast_select_column, const NameScope* from_scan_scope,
    QueryResolutionInfo* query_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const ASTExpression* ast_dotstar = ast_select_column->expression();
  const ASTExpression* ast_expr;
  const ASTStarModifiers* ast_modifiers = nullptr;
  if (ast_dotstar->node_kind() == AST_DOT_STAR) {
    ast_expr = ast_dotstar->GetAsOrDie<ASTDotStar>()->expr();
  } else {
    GOOGLESQL_RET_CHECK_EQ(ast_dotstar->node_kind(), AST_DOT_STAR_WITH_MODIFIERS);
    const ASTDotStarWithModifiers* ast_with_modifiers =
        ast_dotstar->GetAsOrDie<ASTDotStarWithModifiers>();
    ast_expr = ast_with_modifiers->expr();
    ast_modifiers = ast_with_modifiers->modifiers();
  }

  if (in_strict_mode()) {
    return MakeSqlErrorAt(ast_dotstar)
           << "Dot-star is not allowed in strict name resolution mode";
  }

  // If DotStar expression has exactly one identifier and resolves to a range
  // variable, add the scan columns directly to the select_column_state_list.
  // For anything else, we expect to resolve the lhs as a value that should
  // have type struct or proto.
  // Value table range variables are excluded here because we want to resolve
  // it to a value first and then expand its fields, if possible.
  if (ast_expr->node_kind() == AST_PATH_EXPRESSION) {
    const ASTPathExpression* path_expr =
        ast_expr->GetAsOrDie<ASTPathExpression>();

    // In pipe "|> AGGREGATE name.*", we want to get the error below that the
    // expression is non-aggregate, so we skip this shortcut path.
    if (path_expr->num_names() == 1 &&
        !query_resolution_info->IsPipeAggregate()) {
      NameTarget target;
      CorrelatedColumnsSetList correlated_columns_set_list;
      if (from_scan_scope->LookupName(path_expr->first_name()->GetAsIdString(),
                                      &target, &correlated_columns_set_list) &&
          target.kind() == NameTarget::RANGE_VARIABLE &&
          !target.scan_columns()->is_value_table()) {
        if (target.scan_columns()->num_columns() == 0) {
          return MakeSqlErrorAt(path_expr)
                 << "Dot-star would expand to zero columns";
        }

        // Process .* EXCEPT(...) REPLACE(...) if present.
        ColumnReplacements column_replacements;
        if (ast_modifiers != nullptr) {
          GOOGLESQL_RETURN_IF_ERROR(ResolveSelectStarModifiers(
              ast_select_column, ast_modifiers, target.scan_columns().get(),
              /*type_for_star=*/nullptr, from_scan_scope, query_resolution_info,
              &column_replacements));
        }

        GOOGLESQL_RETURN_IF_ERROR(AddNameListToSelectList(
            ast_select_column, target.scan_columns(),
            correlated_columns_set_list,
            /*ignore_excluded_value_table_fields=*/false, query_resolution_info,
            &column_replacements));

        return absl::OkStatus();
      }
    }
  }

  std::unique_ptr<const ResolvedExpr> resolved_dotstar_expr;
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info, from_scan_scope,
      ExprResolutionInfoOptions{
          .allows_aggregation =
              query_resolution_info->SelectFormAllowsAggregation(),
          .allows_analytic = query_resolution_info->SelectFormAllowsAnalytic(),
          .clause_name = query_resolution_info->SelectFormClauseName(),
      });
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_expr, expr_resolution_info.get(),
                              &resolved_dotstar_expr));

  // Check for any features required or not allowed in this expression.
  // This applies the checks from ResolveSelectColumnFirstPass here too
  // since ResolveSelectDotStar is an early exit in that function.
  GOOGLESQL_RETURN_IF_ERROR(CheckExprResolutionInfoForQuery(
      ast_expr, query_resolution_info, *expr_resolution_info));

  const Type* source_type = resolved_dotstar_expr->type();

  std::unique_ptr<const ResolvedColumnRef> src_column_ref;
  if (resolved_dotstar_expr->node_kind() == RESOLVED_COLUMN_REF &&
      !resolved_dotstar_expr->GetAs<ResolvedColumnRef>()->is_correlated()) {
    src_column_ref =
        CopyColumnRef(resolved_dotstar_expr->GetAs<ResolvedColumnRef>());
  } else {
    // We resolved the DotStar to be derived from an expression.
    const ResolvedColumn src_column(
        AllocateColumnId(), kPreProjectId,
        resolved_dotstar_expr->type()->IsStruct() ? kStructId : kProtoId,
        resolved_dotstar_expr->annotated_type());

    if (!expr_resolution_info->findings.has_analytic &&
        !expr_resolution_info->findings.has_aggregation &&
        query_resolution_info->is_resolving_returning_clause()) {
      // The dot-star source expression contains neither analytic functions
      // nor aggregate functions, so it must be computed before any aggregation
      // that might be present (the dot-star columns are effectively
      // pre-GROUP BY columns).

      // However, if this dot-star source expression is in DML returning clause,
      // we do not support pre-GROUP BY columns with Project Scan. (b/207519939)
      // PROJECT scan for dot-star columns in DML THEN RETURN are not supported.
      return MakeSqlErrorAt(ast_dotstar)
             << "Dot-star is only allowed on range variables and columns in "
                "THEN RETURN. It cannot be applied on other expressions "
                "including field access.";
    }

    src_column_ref = MakeColumnRef(src_column);
  }
  GOOGLESQL_RET_CHECK(src_column_ref != nullptr);

  GOOGLESQL_RETURN_IF_ERROR(MakeErrorIfTypeDotStarHasNoFields(ast_dotstar, source_type,
                                                    product_mode()));

  // Process .* EXCEPT(...) REPLACE(...) if present.
  ColumnReplacements column_replacements;
  if (ast_modifiers != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveSelectStarModifiers(
        ast_select_column, ast_modifiers, /*name_list_for_star=*/nullptr,
        source_type, from_scan_scope, query_resolution_info,
        &column_replacements));
  }

  const int orig_num_columns = static_cast<int>(
      query_resolution_info->select_column_state_list()->Size());
  GOOGLESQL_RETURN_IF_ERROR(AddColumnFieldsToSelectList(
      ast_select_column, src_column_ref.get(), std::move(resolved_dotstar_expr),
      expr_resolution_info->findings,
      std::move(expr_resolution_info->columns_referenced_laterally),
      /*column_alias_if_no_fields=*/IdString(),
      /*excluded_field_names=*/nullptr, query_resolution_info,
      &column_replacements));

  // Detect if the * ended up expanding to zero columns after applying EXCEPT,
  // and treat that as an error.
  if (orig_num_columns ==
      query_resolution_info->select_column_state_list()->Size()) {
    GOOGLESQL_RET_CHECK(!column_replacements.excluded_columns.empty());
    return MakeSqlErrorAt(ast_dotstar)
           << "SELECT * expands to zero columns after applying EXCEPT";
  }

  return absl::OkStatus();
}

static absl::StatusOr<DotStarSourceExprInfo*> AddDotStarSourceExprInfoIfNeeded(
    const ASTExpression* ast_expression,
    const ResolvedColumnRef* src_column_ref,
    std::unique_ptr<const ResolvedExpr> resolved_src_expr,
    ExprFindings src_column_expr_findings,

    std::vector<SelectColumnState*> columns_referenced_laterally,
    QueryResolutionInfo* query_resolution_info) {
  ResolvedColumn src_column = src_column_ref->column();

  const ASTExpression* ast_src_expr = nullptr;
  switch (ast_expression->node_kind()) {
    case AST_DOT_STAR:
      ast_src_expr = ast_expression->GetAsOrDie<ASTDotStar>()->expr();
      break;
    case AST_DOT_STAR_WITH_MODIFIERS:
      ast_src_expr =
          ast_expression->GetAsOrDie<ASTDotStarWithModifiers>()->expr();
      break;
    case AST_STAR:
    case AST_STAR_WITH_MODIFIERS:
      return nullptr;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected AST expression kind: "
                       << ast_expression->GetNodeKindString();
  }

  GOOGLESQL_RET_CHECK(ast_src_expr != nullptr);
  GOOGLESQL_RET_CHECK(resolved_src_expr != nullptr);
  return query_resolution_info->AddDotStarSourceExpression(
      {.ast_expr = ast_src_expr,
       .original_resolved_column = src_column,
       .resolved_expr = std::move(resolved_src_expr),
       .lateral_reference_state = std::make_unique<LateralReferenceState>(
           std::move(columns_referenced_laterally),
           /*dot_star_source_expr_info=*/nullptr),
       .expr_findings = src_column_expr_findings});
}

// NOTE: The behavior of star expansion here must match
// NameList::SelectStarHasColumn.
absl::Status Resolver::AddColumnFieldsToSelectList(
    const ASTSelectColumn* ast_select_column,
    const ResolvedColumnRef* src_column_ref,
    std::unique_ptr<const ResolvedExpr> resolved_src_expr,
    ExprFindings src_column_expr_findings,
    std::vector<SelectColumnState*> columns_referenced_laterally,
    IdString column_alias_if_no_fields,
    const IdStringSetCase* excluded_field_names,
    QueryResolutionInfo* query_resolution_info,
    ColumnReplacements* column_replacements) {
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();
  const ASTExpression* ast_expression = ast_select_column->expression();
  const bool allow_no_fields = !column_alias_if_no_fields.empty();
  const Type* type = src_column_ref->type();

  GOOGLESQL_ASSIGN_OR_RETURN(
      DotStarSourceExprInfo * dot_star_source_expr_info,
      AddDotStarSourceExprInfoIfNeeded(
          ast_expression, src_column_ref, std::move(resolved_src_expr),
          src_column_expr_findings, std::move(columns_referenced_laterally),
          query_resolution_info));

  // Check if the value has no fields because it either has scalar type
  // or is a compound type with zero fields.
  // Value table columns with no fields will expand in SELECT * to the
  // value itself rather than to an empty list of fields.
  if (!type->HasAnyFields()) {
    if (!allow_no_fields) {
      GOOGLESQL_RETURN_IF_ERROR(MakeErrorIfTypeDotStarHasNoFields(ast_expression, type,
                                                        product_mode()));
    }

    if (ExcludeOrReplaceColumn(ast_expression, column_alias_if_no_fields,
                               /*excluded_field_names=*/nullptr,
                               column_replacements, select_column_state_list,
                               dot_star_source_expr_info)) {
      return absl::OkStatus();
    }

    // The value doesn't have any fields, but that is allowed here.
    // Just add a ColumnRef directly.
    // is_explicit=false because the column is coming from SELECT *, even if
    // we had an explicit alias for the table.
    // This is not a strict requirement and we could change it.
    select_column_state_list->AddSelectColumn(
        ast_select_column, column_alias_if_no_fields, /*is_explicit=*/false,
        src_column_expr_findings, CopyColumnRef(src_column_ref),
        dot_star_source_expr_info);
    return absl::OkStatus();
  }

  if (type->IsStruct()) {
    const StructType* struct_type = type->AsStruct();

    for (int field_idx = 0; field_idx < struct_type->num_fields();
         ++field_idx) {
      const auto& field = struct_type->field(field_idx);
      const IdString field_name = MakeIdString(
          (field.name.empty()) ? absl::StrCat("$field", 1 + field_idx)
                               : field.name);

      if (ExcludeOrReplaceColumn(ast_expression, field_name,
                                 excluded_field_names, column_replacements,
                                 select_column_state_list,
                                 dot_star_source_expr_info)) {
        continue;
      }

      auto get_struct_field = MakeResolvedGetStructField(
          field.type, CopyColumnRef(src_column_ref), field_idx);
      GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
          /*error_node=*/nullptr, get_struct_field.get()));
      // is_explicit=false because we're extracting all fields of a struct.
      select_column_state_list->AddSelectColumn(
          ast_select_column, field_name,
          /*is_explicit=*/false, src_column_expr_findings,
          std::move(get_struct_field), dot_star_source_expr_info);
    }
  } else if (type->IsGraphElement()) {
    const GraphElementType* graph_element_type = type->AsGraphElement();
    if (graph_element_type->is_dynamic()) {
      GOOGLESQL_RET_CHECK(language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE));
      return MakeSqlErrorAt(ast_expression)
             << "Dot-star is not supported for dynamic graph element type";
    }
    const PropertyGraph* graph;
    GOOGLESQL_RETURN_IF_ERROR(
        catalog_->FindPropertyGraph(graph_element_type->graph_reference(),
                                    graph, analyzer_options_.find_options()));

    for (const auto& [name, property_type] :
         graph_element_type->property_types()) {
      const auto property_name = MakeIdString(name);
      if (ExcludeOrReplaceColumn(ast_expression, property_name,
                                 excluded_field_names, column_replacements,
                                 select_column_state_list,
                                 dot_star_source_expr_info)) {
        continue;
      }

      const GraphPropertyDeclaration* prop_dcl;
      // TODO: Convert `FindPropertyDeclarationByName` to use
      // StatusOr.
      GOOGLESQL_RETURN_IF_ERROR(graph->FindPropertyDeclarationByName(name, prop_dcl));

      auto builder = ResolvedGraphGetElementPropertyBuilder()
                         .set_type(property_type)
                         .set_expr(CopyColumnRef(src_column_ref))
                         .set_property(prop_dcl);
      if (language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
        builder.set_property_name(
            ResolvedLiteralBuilder()
                .set_value(Value::String(prop_dcl->Name()))
                .set_type(types::StringType())
                .set_has_explicit_type(true));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphGetElementProperty>
                           get_element_property,
                       std::move(builder).Build());
      select_column_state_list->AddSelectColumn(
          ast_select_column, property_name,
          /*is_explicit=*/false, src_column_expr_findings,
          std::move(get_element_property), dot_star_source_expr_info);
    }
  } else if (type->IsProto()) {
    const ProtoType* proto_type = type->AsProto();
    const google::protobuf::Descriptor* proto_descriptor = proto_type->descriptor();

    std::map<int32_t, const google::protobuf::FieldDescriptor*>
        tag_number_ordered_field_map;
    for (int proto_idx = 0; proto_idx < proto_descriptor->field_count();
         ++proto_idx) {
      const google::protobuf::FieldDescriptor* field = proto_descriptor->field(proto_idx);
      GOOGLESQL_RET_CHECK(
          googlesql_base::InsertIfNotPresent(&tag_number_ordered_field_map,
                                  std::make_pair(field->number(), field)));
    }

    for (const auto& entry : tag_number_ordered_field_map) {
      const google::protobuf::FieldDescriptor* field = entry.second;

      const IdString field_name = MakeIdString(field->name());
      if (ExcludeOrReplaceColumn(ast_expression, field_name,
                                 excluded_field_names, column_replacements,
                                 select_column_state_list,
                                 dot_star_source_expr_info)) {
        continue;
      }

      const Type* field_type;
      Value default_value;
      GOOGLESQL_RETURN_IF_ERROR(
          GetProtoFieldTypeAndDefault(
              ProtoFieldDefaultOptions::FromFieldAndLanguage(field, language()),
              field, proto_type->CatalogNamePath(), type_factory_, &field_type,
              &default_value))
          .With(LocationOverride(ast_expression));
      // TODO: This really should be check for
      // !field_type->IsSupportedType(language())
      // but that breaks existing tests :(
      if (field_type->UsingFeatureV12CivilTimeType() &&
          !language().LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) {
        return MakeSqlErrorAt(ast_expression)
               << "Dot-star expansion includes field " << field_name
               << " with unsupported type "
               << field_type->ShortTypeName(language().product_mode());
      }
      std::unique_ptr<const ResolvedExpr> resolved_expr =
          MakeResolvedGetProtoField(
              field_type, CopyColumnRef(src_column_ref), field, default_value,
              /*get_has_bit=*/false, ProtoType::GetFormatAnnotation(field),
              /*return_default_value_when_unset=*/false);
      // is_explicit=false because we're extracting all fields of a proto.
      select_column_state_list->AddSelectColumn(
          ast_select_column, field_name, /*is_explicit=*/false,
          src_column_expr_findings, std::move(resolved_expr),
          dot_star_source_expr_info);
    }
  } else if (type->IsRow()) {
    const RowType* row_type = type->AsRow();
    const Table* table = row_type->table();
    switch (table->GetColumnListMode()) {
      case Table::ColumnListMode::FIND_ONLY:
        return MakeSqlErrorAt(ast_expression)
               << "Star expansion not allowed for table " << table->FullName();
      case Table::ColumnListMode::DEFAULT:
      case Table::ColumnListMode::LAZY: {
        CycleDetector cycle_detector;
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::vector<const Column*> columns,
            table->ListLazyColumns(row_type->GetTableScanContext(),
                                   Catalog::FindOptions(&cycle_detector)));
        for (const Column* column : columns) {
          if (column->IsPseudoColumn()) {
            continue;
          }
          const IdString field_name = MakeIdString(column->Name());
          if (ExcludeOrReplaceColumn(ast_expression, field_name,
                                     excluded_field_names, column_replacements,
                                     select_column_state_list,
                                     dot_star_source_expr_info)) {
            continue;
          }

          auto get_row_field = MakeResolvedGetRowField(
              column->GetType(), CopyColumnRef(src_column_ref), column);
          GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
              /*error_node=*/nullptr, get_row_field.get()));

          // is_explicit=false because we're extracting all columns of a table.
          select_column_state_list->AddSelectColumn(
              ast_select_column, field_name,
              /*is_explicit=*/false, src_column_expr_findings,
              std::move(get_row_field), dot_star_source_expr_info);
        }
      }
    }
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type in star expansion: "
                     << type->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Resolver::CheckExprResolutionInfoForQuery(
    const ASTNode* ast_location, QueryResolutionInfo* query_resolution_info,
    const ExprResolutionInfo& expr_resolution_info) {
  // Errors for expressions that cannot occur are raised during ResolveExpr,
  // with specific locations.
  // Errors for missing required expression types are raised here.
  if (query_resolution_info->IsPipeSelect() ||
      query_resolution_info->IsPipeExtend()) {
    GOOGLESQL_RET_CHECK(!expr_resolution_info.findings.has_aggregation);
  } else if (query_resolution_info->IsPipeAggregate()) {
    GOOGLESQL_RET_CHECK(!expr_resolution_info.findings.has_analytic);
    if (!expr_resolution_info.findings.has_aggregation) {
      return MakeSqlErrorAt(ast_location)
             << "Pipe AGGREGATE cannot include non-aggregate expressions";
    }
  } else if (query_resolution_info->IsPipeWindow()) {
    // Note that ResolveSelectDotStar exits early above, so this error
    // check needs to be duplicated in that function.
    GOOGLESQL_RET_CHECK(!expr_resolution_info.findings.has_aggregation);
    if (!expr_resolution_info.findings.has_analytic) {
      return MakeSqlErrorAt(ast_location)
             << "Pipe WINDOW expression must include a window function "
                "call (with an OVER clause)";
    }
  }

  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to "Resolver::ResolveExpr" which is called from this function and
// which is also a recursive function.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveSelectColumnFirstPass(
    const ASTSelectColumn* ast_select_column, const NameScope* from_scan_scope,
    const std::shared_ptr<const NameList>& from_clause_name_list,
    std::unique_ptr<NameScope>& current_name_scope,
    IdString select_column_alias, int select_column_state_list_write_idx,
    QueryResolutionInfo* query_resolution_info, const Type* inferred_type) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK_LE(select_column_state_list_write_idx,
               query_resolution_info->select_column_state_list()->Size());

  // The parser can only produce ordering suffixes in ASTSelectColumns
  // produced for pipe AGGREGATE.
  if (!query_resolution_info->IsPipeAggregate()) {
    GOOGLESQL_RET_CHECK(ast_select_column->grouping_item_order() == nullptr);
  }

  const ASTExpression* ast_select_expr = ast_select_column->expression();

  switch (ast_select_expr->node_kind()) {
    case AST_STAR:
    case AST_STAR_WITH_MODIFIERS:
    case AST_DOT_STAR:
    case AST_DOT_STAR_WITH_MODIFIERS:
      // Overwriting a `SelectColumnState` is not permitted when resolving star
      // or dot star expansion.
      if (select_column_state_list_write_idx <
          query_resolution_info->select_column_state_list()->Size()) {
        GOOGLESQL_RET_CHECK(query_resolution_info->select_column_state_list()
                      ->GetSelectColumnState(select_column_state_list_write_idx)
                      ->contains_outer_group_rows_or_group_by_modifiers);
        return MakeSqlErrorAt(ast_select_column)
               << "Dot-star and star expansion is not permitted on expressions "
                  "that use GROUP ROWS or GROUP BY modifiers on an aggregate "
                  "function.";
      }
      if (ast_select_expr->node_kind() == AST_STAR ||
          ast_select_expr->node_kind() == AST_STAR_WITH_MODIFIERS) {
        return ResolveSelectStar(ast_select_column, from_clause_name_list,
                                 current_name_scope.get(),
                                 query_resolution_info);
      } else {
        return ResolveSelectDotStar(ast_select_column, current_name_scope.get(),
                                    query_resolution_info);
      }
      break;
    default:
      break;
  }

  if (query_resolution_info->IsGqlReturn() ||
      query_resolution_info->IsGqlWith()) {
    // SELECT * is allowed in these GQL statements, and handled in the
    // switch block above.

    // Require inferred names (trailing identifier in path expression) or
    // explicit aliases for GQL RETURN/WITH statement
    if (ast_select_column->alias() == nullptr &&
        ast_select_expr->node_kind() != AST_PATH_EXPRESSION) {
      return MakeSqlErrorAt(ast_select_column)
             << "A name must be explicitly defined for this column";
    }
  }

  // Save stack space for nested SELECT list subqueries.
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info, current_name_scope.get(),
      ExprResolutionInfoOptions{
          .allows_aggregation =
              query_resolution_info->SelectFormAllowsAggregation(),
          .allows_analytic = query_resolution_info->SelectFormAllowsAnalytic(),
          .clause_name = query_resolution_info->SelectFormClauseName(),
          .top_level_ast_expr = ast_select_expr,
          .column_alias = select_column_alias});

  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_select_expr, expr_resolution_info.get(),
                              &resolved_expr, inferred_type));

  // Check for any features required or not allowed in this expression.
  // Note that this also needs to happen inside the ResolveSelectDotStar
  // call above that exists early.
  GOOGLESQL_RETURN_IF_ERROR(CheckExprResolutionInfoForQuery(
      ast_select_expr, query_resolution_info, *expr_resolution_info));

  // We can set is_explicit=true unconditionally because this either came
  // from an AS alias or from a path in the query, or it's an internal name
  // for an anonymous column (that can't be looked up).
  auto select_column_state = std::make_unique<SelectColumnState>(
      ast_select_column, select_column_alias, /*is_explicit=*/true,
      expr_resolution_info->findings, std::move(resolved_expr),
      std::move(expr_resolution_info->columns_referenced_laterally),
      /*dot_star_source_expr_info=*/nullptr);

  if (select_column_state_list_write_idx ==
      query_resolution_info->select_column_state_list()->Size()) {
    // We can set is_explicit=true unconditionally because this either came
    // from an AS alias or from a path in the query, or it's an internal name
    // for an anonymous column (that can't be looked up).
    query_resolution_info->select_column_state_list()->AddSelectColumn(
        std::move(select_column_state));
  } else {
    select_column_state->select_list_position =
        select_column_state_list_write_idx;

    GOOGLESQL_RETURN_IF_ERROR(
        query_resolution_info->select_column_state_list()->ReplaceSelectColumn(
            select_column_state_list_write_idx,
            std::move(select_column_state)));
  }

  return absl::OkStatus();
}

static bool SelectFormAllowsLateralColumnReferences(SelectForm select_form) {
  return select_form == SelectForm::kClassic ||
         select_form == SelectForm::kNoFrom;
}

absl::Status Resolver::AddSelectColumnAliasToScopeForLateralReferences(
    SelectForm select_form,
    const std::shared_ptr<const NameList>& from_clause_name_list,
    const NameScope* from_scan_scope, IdString query_alias,
    SelectColumnState* select_column_state,
    std::shared_ptr<NameList>& updated_name_list,
    std::unique_ptr<NameScope>& updated_name_scope) {
  if (!SelectFormAllowsLateralColumnReferences(select_form)) {
    return absl::OkStatus();
  }

  IdString select_column_alias = select_column_state->alias;
  NameTarget target;
  // We check against the original list, not the current one because we
  // allow multiple aliases to be added. The key part is to give
  // the precedence to the FROM column.
  if (IsInternalAlias(select_column_alias) ||
      from_clause_name_list->LookupName(select_column_alias, &target)) {
    return absl::OkStatus();
  }

  ColumnReferenceCallback precompute_callback =
      [this, select_column_state, query_alias](
          ColumnReferenceContext context) -> absl::StatusOr<ResolvedColumn> {
    // Update the state tracking of pre- and post-grouping status when the
    // context kind is known. Cascade to any columns further referenced from
    // this one as well.
    if (context.grouping_context == ExprGroupingContext::kPreGrouping) {
      select_column_state->lateral_reference_state->PinToPreGroupingContext();
    }

    // Regardless of context, mark it as referenced laterally.
    select_column_state->lateral_reference_state->MarkAsReferencedLaterally();

    auto& resolved_expr = select_column_state->resolved_expr;

    if (resolved_expr == nullptr) {
      // This only happens with multi-level aggregation and GROUP ROWS, due to
      // the deferred resolution. Any column referencing this itself also has
      // to be deferred. We signal this by returning an uninitialized
      // ResolvedColumn.
      GOOGLESQL_RET_CHECK(
          select_column_state->contains_outer_group_rows_or_group_by_modifiers);
      return MakeSqlErrorAtPoint(context.location)
             << "Lateral references to SELECT columns with multi-level "
                "aggregation or GROUP ROWS are not supported";
    }

    if (resolved_expr->Is<ResolvedColumnRef>() &&
        !resolved_expr->GetAs<ResolvedColumnRef>()->is_correlated()) {
      // The expression already resolves to a column, so return that
      // column directly. No need to precompute anything.
      return resolved_expr->GetAs<ResolvedColumnRef>()->column();
    }

    ResolvedColumn precomputed_resolved_column = ResolvedColumn(
        AllocateColumnId(), query_alias, select_column_state->alias,
        resolved_expr->annotated_type());

    select_column_state->resolved_select_column = precomputed_resolved_column;
    return precomputed_resolved_column;
  };

  if (language().LanguageFeatureEnabled(FEATURE_LATERAL_COLUMN_REFERENCES)) {
    GOOGLESQL_RETURN_IF_ERROR(updated_name_list->AddDelayedColumn(
        select_column_alias,
        /*is_explicit=*/select_column_state->ast_select_column->alias() !=
            nullptr,
        DelayedColumnInfo{.precompute_callback = std::move(precompute_callback),
                          .select_column_state = select_column_state}));
  } else if (analyzer_options_.log_impact_of_lateral_column_references()) {
    LoggingCallback logging_callback =
        [this](const ASTNode* location, const NameTarget& original_target) {
          return AddDeprecationWarning(
              location, DeprecationWarning::LATERAL_COLUMN_REFERENCE,
              absl::StrCat("Lateral column alias will shadow original target: ",
                           original_target.DebugString()));
        };

    GOOGLESQL_RETURN_IF_ERROR(updated_name_list->AddLoggingTarget(
        select_column_alias,
        /*is_explicit=*/select_column_state->ast_select_column->alias() !=
            nullptr,
        LoggingTargetInfo{
            // Return the usual target which we would have normally return.
            .fallback_name_scope = from_scan_scope,
            .logging_callback = logging_callback}));
  }

  updated_name_scope = std::make_unique<NameScope>(
      from_scan_scope->previous_scope(), updated_name_list);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveSelectListExprsFirstPass(
    const ASTSelectList* select_list, const NameScope* from_scan_scope,
    const std::shared_ptr<const NameList>& from_clause_name_list,
    QueryResolutionInfo* query_resolution_info, IdString query_alias,
    std::unique_ptr<NameScope>& updated_name_scope,
    std::shared_ptr<NameList>& updated_name_list,
    const Type* inferred_type_for_query) {
  updated_name_list = from_clause_name_list->Copy();

  GOOGLESQL_RETURN_IF_ERROR(from_scan_scope->CopyNameScopeWithOverridingNameTargets(
      /*overriding_name_targets=*/{}, &updated_name_scope));

  for (int select_column_idx = 0;
       select_column_idx < select_list->columns().size(); ++select_column_idx) {
    size_t num_existing_select_column_states =
        query_resolution_info->select_column_state_list()->Size();

    const ASTSelectColumn* select_column =
        select_list->columns(select_column_idx);
    IdString alias = ComputeSelectColumnAlias(select_column, select_column_idx);
    GOOGLESQL_ASSIGN_OR_RETURN(DeferredResolutionSelectColumnInfo select_column_info,
                     GetDeferredResolutionSelectColumnInfo(select_column));
    // If the `ASTSelectColumn` contains a function that uses GROUP ROWS or
    // GROUP BY modifiers, then 1st-pass resolution of this `ASTSelectColumn`
    // must be deferred until after GROUP BY clause resolution finishes. This is
    // because GROUP ROWS subquery resolution needs to know that certain columns
    // are already grouped by and therefore valid to access in an aggregate
    // context without being wrapped by an aggregate expression.
    //
    // For example, given the following query:
    //
    //   SELECT
    //     A,
    //     B,
    //     SUM(intermediate) WITH GROUP ROWS (
    //       SELECT
    //         A + AVG(C) AS intermediate
    //       FROM GROUP_ROWS()
    //     ) AS aggregate_alias,
    //   FROM LettersTable
    //   GROUP BY {{ (A, B) | ALL }}
    //
    // Accessing column `A` within the GROUP ROWS subquery should be valid,
    // since `A` is grouped by in the outermost query and therefore constant
    // within the group of rows that the subquery runs over. However, it is
    // impossible to determine that `A` is valid to access without first
    // resolving the 'GROUP BY' clause. Furthermore, 'GROUP BY' clause
    // resolution requires doing a 1st-pass resolution of SELECT list
    // expressions since SELECT list expression columns can be used in the
    // 'GROUP BY' clause.
    //
    // We work around this by deferring resolution of SELECT list expressions
    // that contain a 'GROUP ROWS' expression or 'GROUP BY' modifier outside
    // of an expression subquery. These expressions are instead resolved after
    // 'GROUP BY' clause resolution. Since both 'GROUP ROWS' and 'GROUP BY'
    // modifiers are only permitted on aggregate functions, we claim that any
    // expression containing a 'GROUP ROWS' expression or a 'GROUP BY' modifier
    // outside of an expression subquery *must* be an aggregate expression and
    // therefore never valid to be used in the 'GROUP BY' clause. Thus, deferred
    // resolution of these expressions is considered safe.
    // TODO: b/430036320 - Cleanup if not needed by simplified GROUP ROWS.
    if (select_column_info.has_outer_group_rows_or_group_by_modifiers) {
      auto select_column_state = std::make_unique<SelectColumnState>(
          select_column, alias, /*is_explicit=*/true,
          ExprFindings{
              .has_aggregation = true,
              .has_analytic = select_column_info.has_outer_analytic_function,
              .has_volatile = false,
          },
          /*resolved_expr=*/nullptr,
          /*columns_referenced_laterally=*/std::vector<SelectColumnState*>(),
          /*dot_star_source_expr_info=*/nullptr);
      select_column_state->contains_outer_group_rows_or_group_by_modifiers =
          true;
      query_resolution_info->select_column_state_list()->AddSelectColumn(
          std::move(select_column_state));
      query_resolution_info->SetHasAggregation(true);
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ResolveSelectColumnFirstPass(
          select_column, from_scan_scope, from_clause_name_list,
          updated_name_scope, alias,
          static_cast<int32_t>(
              query_resolution_info->select_column_state_list()->Size()),
          query_resolution_info, inferred_type_for_query));
    }

    // Add the new aliases to the namescope, when no such names already exists
    // in the from_scan_scope. Multiple SelectColumnState may have been added
    // as is the case with dot-star, for example.
    for (size_t idx = num_existing_select_column_states;
         idx < query_resolution_info->select_column_state_list()->Size();
         ++idx) {
      SelectColumnState* select_column_state =
          query_resolution_info->select_column_state_list()
              ->GetSelectColumnState(static_cast<int32_t>(idx));
      GOOGLESQL_RETURN_IF_ERROR(AddSelectColumnAliasToScopeForLateralReferences(
          query_resolution_info->select_form(), from_clause_name_list,
          from_scan_scope, query_alias, select_column_state, updated_name_list,
          updated_name_scope));
    }
  }

  // Integrity checks
  for (const auto& select_column_state :
       query_resolution_info->select_column_state_list()
           ->select_column_state_list()) {
    if (select_column_state->lateral_reference_state
            ->is_referenced_laterally()) {
      GOOGLESQL_RET_CHECK(select_column_state->resolved_expr != nullptr);
      GOOGLESQL_RET_CHECK(!select_column_state->resolved_pre_group_by_select_column
                     .IsInitialized());
      if (select_column_state->resolved_select_column.IsInitialized()) {
        GOOGLESQL_RET_CHECK(
            !select_column_state->resolved_expr->Is<ResolvedColumnRef>() ||
            select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
                ->is_correlated())
            << select_column_state->DebugString();
      }
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDeferredFirstPassSelectListExprs(
    const NameScope* name_scope,
    const std::shared_ptr<const NameList>& name_list,
    QueryResolutionInfo* query_resolution_info, IdString query_alias,
    const Type* inferred_type_for_query) {
  // Resolve SELECT list expressions that were skipped during first pass
  // resolution. Resolving these expressions will modify existing
  // `SelectColumnState` values in the `SelectColumnStateList` in
  // `query_resolution_info`.
  // Note: If a SELECT list expression resolves to multiple SelectColumnStates
  // (e.g. from dot-star or star expansion), then any ordinals used previously
  // during GROUP BY clause evaluation may be incorrect. To avoid this, we throw
  // an error if expression resolution results in multiple SelectColumnStates.
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();

  std::unique_ptr<NameScope> current_name_scope;
  GOOGLESQL_RETURN_IF_ERROR(name_scope->CopyNameScopeWithOverridingNameTargets(
      /*overriding_name_targets=*/{}, &current_name_scope));
  std::shared_ptr<NameList> current_name_list = name_list->Copy();

  for (int idx = 0; idx < select_column_state_list->Size(); ++idx) {
    const std::unique_ptr<SelectColumnState>& select_column_state =
        select_column_state_list->select_column_state_list().at(idx);
    if (select_column_state->contains_outer_group_rows_or_group_by_modifiers) {
      // The call to `ResolveSelectColumnFirstPass` will modify the
      // `SelectColumnState` at this `idx`, meaning the `SelectColumnState`
      // wrapped by `select_column_state` will be different.
      GOOGLESQL_RETURN_IF_ERROR(ResolveSelectColumnFirstPass(
          select_column_state->ast_select_column, name_scope, name_list,
          current_name_scope, select_column_state->alias, idx,
          query_resolution_info, inferred_type_for_query));

      GOOGLESQL_RET_CHECK(!select_column_state->ast_expr->Is<ASTDotStar>());
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ValidateAndResolveOrderByCollate(
    const ASTCollate* ast_collate, const ASTNode* ast_order_by_item_location,
    const Type* order_by_item_column,
    std::unique_ptr<const ResolvedExpr>* resolved_collate) {
  if (!language().LanguageFeatureEnabled(FEATURE_ORDER_BY_COLLATE)) {
    return MakeSqlErrorAt(ast_collate) << "COLLATE is not supported";
  }
  if (!order_by_item_column->IsString()) {
    return MakeSqlErrorAt(ast_order_by_item_location)
           << "COLLATE can only be applied to expressions of type "
              "STRING, but was applied to "
           << order_by_item_column->ShortTypeName(product_mode());
  }
  return ResolveCollate(ast_collate, resolved_collate);
}

absl::StatusOr<ResolvedOrderByItemEnums::NullOrderMode>
Resolver::ResolveNullOrderMode(const ASTNullOrder* null_order) {
  if (null_order == nullptr) {
    return ResolvedOrderByItemEnums::ORDER_UNSPECIFIED;
  }
  if (!language().LanguageFeatureEnabled(
          FEATURE_NULLS_FIRST_LAST_IN_ORDER_BY)) {
    return MakeSqlErrorAt(null_order)
           << "NULLS FIRST and NULLS LAST are not supported";
  }
  return null_order->nulls_first() ? ResolvedOrderByItemEnums::NULLS_FIRST
                                   : ResolvedOrderByItemEnums::NULLS_LAST;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveOrderingExprWithGroupByExprEquivalenceFallback(
    const ASTExpression* ordering_expr,
    ExprResolutionInfo* post_group_by_expr_resolution_info) {
  // Try to resolve against post-group-by scope first. If it succeeds,
  // proceed. Otherwise, try to resolve against the select list and from scan
  // scope, if we can find a matching expression in GROUP BY clause, use the
  // post-group-by column.
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  absl::Status post_group_by_resolve_status = ResolveExpr(
      ordering_expr, post_group_by_expr_resolution_info, &resolved_expr);

  if (!post_group_by_resolve_status.ok()) {
    // ExprResolutionInfo::aggregate_name_scope always points to the
    // pre-group-by scope.
    auto pre_group_by_expr_resolution_info =
        std::make_unique<ExprResolutionInfo>(
            post_group_by_expr_resolution_info->aggregate_name_scope,
            post_group_by_expr_resolution_info->aggregate_name_scope,
            post_group_by_expr_resolution_info->aggregate_name_scope,
            /*allows_aggregation_in=*/false,
            /*allows_analytic_in=*/false,
            /*use_post_grouping_columns_in=*/false,
            /*clause_name_in=*/"ORDER BY",
            post_group_by_expr_resolution_info->query_resolution_info);
    std::unique_ptr<const ResolvedExpr> resolved_pre_group_by_expr;
    absl::Status pre_group_by_resolve_status =
        ResolveExpr(ordering_expr, pre_group_by_expr_resolution_info.get(),
                    &resolved_pre_group_by_expr);
    // If the expression fails to resolve to pre-group-by scope, return the
    // failing status of post-group-by scope resolution directly.
    if (!pre_group_by_resolve_status.ok()) {
      return post_group_by_resolve_status;
    }
    bool has_matching_group_by_expr = false;
    for (const GroupByColumnState& group_by_column_state :
         post_group_by_expr_resolution_info->query_resolution_info
             ->group_by_column_state_list()) {
      const ResolvedExpr* group_by_expr =
          group_by_column_state.GetPreGroupByResolvedExpr();

      GOOGLESQL_ASSIGN_OR_RETURN(bool is_same_expr, IsSameExpressionForGroupBy(
                                              resolved_pre_group_by_expr.get(),
                                              group_by_expr, language()));
      if (is_same_expr) {
        // We matched this ORDER BY expression to a GROUP BY expression.
        // Update the ORDER BY expression to point at the GROUP BY
        // computed column.
        resolved_expr = MakeResolvedColumnRef(
            group_by_expr->type(),
            group_by_column_state.computed_column->column(),
            /*is_correlated=*/false);
        has_matching_group_by_expr = true;
        break;
      }
    }
    // If the expression fails to match any GROUP BY expression, return the
    // failing status of post-group-by scope resolution directly.
    if (!has_matching_group_by_expr) {
      return post_group_by_resolve_status;
    }
  }
  return resolved_expr;
}

absl::Status Resolver::ResolveOrderingExprs(
    const absl::Span<const ASTOrderingExpression* const> ordering_expressions,
    ExprResolutionInfo* expr_resolution_info, bool allow_ordinals,
    absl::string_view clause_name,
    std::vector<OrderByItemInfo>* order_by_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (const ASTOrderingExpression* order_by_expression :
       ordering_expressions) {
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedOrderByItemEnums::NullOrderMode null_order,
                     ResolveNullOrderMode(order_by_expression->null_order()));

    // Resolves the ORDER BY expression against the post-GROUP BY scope and
    // falls back to an equivalent GROUP BY expression if it exists.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> resolved_order_expression,
        ResolveOrderingExprWithGroupByExprEquivalenceFallback(
            order_by_expression->expression(), expr_resolution_info));

    // If the expression was an integer literal, remember that mapping.
    if (resolved_order_expression->node_kind() == RESOLVED_LITERAL &&
        !resolved_order_expression->GetAs<ResolvedLiteral>()
             ->has_explicit_type()) {
      const Value& value =
          resolved_order_expression->GetAs<ResolvedLiteral>()->value();
      if (value.type_kind() == TYPE_INT64 && !value.is_null()) {
        if (!allow_ordinals) {
          return MakeSqlErrorAt(order_by_expression)
                 << "ORDER BY ordinal is not allowed in " << clause_name;
        }
        if (value.int64_value() < 1) {
          return MakeSqlErrorAt(order_by_expression)
                 << "ORDER BY column number item is out of range. "
                 << "Column numbers must be greater than or equal to one. "
                 << "Found : " << value.int64_value();
        }
        const int64_t int_value = value.int64_value() - 1;  // Make it 0-based.
        order_by_info->emplace_back(
            order_by_expression, order_by_expression->collate(), int_value,
            order_by_expression->descending(), null_order);
      } else {
        return MakeSqlErrorAt(order_by_expression)
               << "Cannot ORDER BY literal values";
      }
      resolved_order_expression.reset();  // No longer needed.
    } else {
      order_by_info->emplace_back(
          order_by_expression, order_by_expression->collate(),
          std::move(resolved_order_expression),
          order_by_expression->descending(), null_order);
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::HandleGroupBySelectColumn(
    const SelectColumnState* group_by_column_state,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr,
    const ResolvedExpr** pre_group_by_expr, ResolvedColumn* group_by_column) {
  GOOGLESQL_RET_CHECK(!query_resolution_info->IsPipeOp());

  // If this SELECT list column is already being grouped by then we should
  // not be calling this.
  GOOGLESQL_RET_CHECK(!group_by_column_state->is_group_by_column);

  // We are grouping by either a SELECT list ordinal or alias so we must
  // update the SelectColumnState to reflect it is being grouped by.
  // We need a mutable version of the SelectColumnState since we will
  // be updating its expression and other information.
  SelectColumnState* select_column_state =
      const_cast<SelectColumnState*>(group_by_column_state);

  // Move the expression from the SelectColumnState to the
  // group_by_columns list, and update the SelectColumnState to reference
  // the associated group by column.
  GOOGLESQL_RET_CHECK(select_column_state->resolved_expr != nullptr)
      << select_column_state->DebugString();

  const ResolvedComputedColumn* existing_computed_column =
      query_resolution_info->GetEquivalentGroupByComputedColumnOrNull(
          select_column_state->resolved_expr.get());
  if (existing_computed_column != nullptr) {
    // Make a reference to the existing column rather than recomputing the
    // expression.
    *group_by_column = existing_computed_column->column();
  } else {
    const IdString* query_alias = &kGroupById;
    if (query_resolution_info->IsGqlReturn() ||
        query_resolution_info->IsGqlWith()) {
      query_alias = &kGraphTableId;
    }
    *group_by_column = ResolvedColumn(
        AllocateColumnId(), *query_alias, select_column_state->alias,
        select_column_state->resolved_expr->annotated_type());
  }

  *pre_group_by_expr = GetPreGroupByResolvedExpr(select_column_state);

  if (select_column_state->lateral_reference_state
          ->IsInvolvedInLateralReferences()) {
    // We cannot directly add the column as it may be referenced by other
    // grouping columns. We need to precompute it. This happens in the second
    // pass, so we need to hold onto the expression until then, e.g. to match
    // against grouping exprs in subsequent HandleGroupByExpression() calls.
    // In the second pass, we will update the SELECT expression to be a colRef
    // to the grouping column like the other else {} branch right below.
    // Even if the column itself is not referenced by other grouping columns,
    // but only makes column references, it still needs to have a pre-grouping
    // column ID assigned, in case it's referenced from HAVING, etc.
    //
    // The actual grouping expression will be a simple column reference
    // to the precomputed column.
    GOOGLESQL_RET_CHECK(select_column_state->resolved_pre_group_by_select_column
                  .IsInitialized());
    *resolved_expr =
        MakeColumnRef(select_column_state->resolved_pre_group_by_select_column);
  } else {
    *resolved_expr = std::move(select_column_state->resolved_expr);
    select_column_state->resolved_expr = MakeColumnRef(*group_by_column);
  }
  select_column_state->lateral_reference_state->PinToPreGroupingContext();
  select_column_state->is_group_by_column = true;
  // Update the SelectColumnState to reflect the grouped by version of
  // the column.
  select_column_state->resolved_select_column = *group_by_column;

  // If the 'resolved_expr' is a path expression, we must collect
  // information in the 'query_resolution_info' about that path
  // expression and its relationship to the 'group_by_column'.
  // This information will get used later when constructing a
  // NameScope for what names are valid post-GROUP BY, where the
  // new NameScope is used when resolving subsequent expressions.
  // If the 'resolved_expr' is not a path expression then this
  // is a no-op.
  GOOGLESQL_RETURN_IF_ERROR(CollectResolvedPathExpressionInfoIfRelevant(
      query_resolution_info, resolved_expr->get(), *group_by_column));

  DotStarSourceExprInfo* dot_star_source_expr_info =
      select_column_state->dot_star_source_expr_info;
  if (dot_star_source_expr_info != nullptr &&
      !dot_star_source_expr_info->is_finalized) {
    auto& dot_star_source_resolved_expr =
        dot_star_source_expr_info->resolved_expr;
    GOOGLESQL_RET_CHECK(dot_star_source_resolved_expr != nullptr);
    if (!dot_star_source_resolved_expr->Is<ResolvedColumnRef>() ||
        dot_star_source_resolved_expr->GetAs<ResolvedColumnRef>()
            ->is_correlated()) {
      // For a dot-star column to be a grouping column, the dot-star source
      // expression must be precomputed before grouping.
      GOOGLESQL_RET_CHECK(!dot_star_source_expr_info->expr_findings.has_aggregation);
      GOOGLESQL_RET_CHECK(!dot_star_source_expr_info->expr_findings.has_analytic);
      query_resolution_info->select_list_columns_to_compute_before_aggregation()
          ->push_back(MakeResolvedComputedColumn(
              dot_star_source_expr_info->original_resolved_column,
              std::move(dot_star_source_expr_info->resolved_expr)));
    }

    // Mark the dot-star source expression info as finalized so that it is not
    // processed again.
    dot_star_source_expr_info->is_finalized = true;
    dot_star_source_expr_info->resolved_expr = nullptr;
  }

  return absl::OkStatus();
}

absl::Status Resolver::HandleGroupByExpression(
    const ASTExpression* ast_group_by_expr,
    QueryResolutionInfo* query_resolution_info, IdString alias,
    std::unique_ptr<const ResolvedExpr>* resolved_expr,
    const ResolvedExpr** pre_group_by_expr, ResolvedColumn* group_by_column) {
  // We're grouping by an expression that was not a SELECT list alias
  // or ordinal.
  GOOGLESQL_RET_CHECK(resolved_expr != nullptr && (*resolved_expr) != nullptr);

  if ((*resolved_expr)->node_kind() == RESOLVED_LITERAL &&
      !(*resolved_expr)->GetAs<ResolvedLiteral>()->has_explicit_type()) {
    return MakeSqlErrorAt(ast_group_by_expr)
           << "Cannot GROUP BY literal values";
  }

  // This expression might match one that is already going to be
  // precomputed before the GROUP BY.  If so, then set <group_by_column>
  // to it and update the expression to be a simple column reference.
  // For instance, consider:
  //   SELECT k.col1
  //   FROM valuetable k
  //   GROUP BY k.col1
  // This code is needed to detect that the GROUP BY k.col1 expression
  // is the same thing as the k.col1 that we are precomputing before the
  // GROUP BY, so we can re-use the same column and avoid an additional
  // expression evaluation.
  bool found_precomputed_expression = false;
  // Expressions in select_list_columns_to_compute_before_aggregation are
  // pre-group-by expressions, so we can set its value to pre_group_by_expr once
  // a precomputed expression is found.
  const ResolvedExpr* precomputed_expr = nullptr;

  SelectColumnState* precomputed_lateral_select_column_state = nullptr;

  // First, look in SELECT list columns for lateral column references.
  GOOGLESQL_RET_CHECK(query_resolution_info
                ->successive_select_list_columns_to_compute_before_aggregation()
                ->empty());

  for (const auto& select_column :
       query_resolution_info->select_column_state_list()
           ->select_column_state_list()) {
    // Only select columns involved in lateral references are relevant here.
    if (select_column->expr_findings.has_aggregation ||
        select_column->expr_findings.has_analytic ||
        select_column->contains_outer_group_rows_or_group_by_modifiers ||
        !select_column->lateral_reference_state
             ->IsInvolvedInLateralReferences()) {
      continue;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_same_expr,
                     IsSameExpressionForGroupBy(
                         resolved_expr->get(),
                         select_column->resolved_expr.get(), language()));
    if (is_same_expr) {
      *group_by_column = select_column->resolved_pre_group_by_select_column;
      precomputed_expr = select_column->resolved_expr.get();
      found_precomputed_expression = true;
      // This column itself counts as a grouping column so it should be
      // pre-computed before grouping.
      select_column->is_group_by_column = true;
      select_column->lateral_reference_state->PinToPreGroupingContext();
      precomputed_lateral_select_column_state = select_column.get();
      break;
    }
  }

  // If not found, look in the usual list of computed columns before
  // aggregation.
  if (!found_precomputed_expression) {
    for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
         *query_resolution_info
              ->select_list_columns_to_compute_before_aggregation()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          bool is_same_expr,
          IsSameExpressionForGroupBy(resolved_expr->get(),
                                     computed_column->expr(), language()));
      if (is_same_expr) {
        *group_by_column = computed_column->column();
        precomputed_expr = computed_column->expr();
        found_precomputed_expression = true;
        break;
      }
    }
  }
  if (found_precomputed_expression) {
    *resolved_expr = MakeColumnRef(*group_by_column);
    *pre_group_by_expr = precomputed_expr;
  }
  const ResolvedComputedColumn* existing_computed_column =
      query_resolution_info->GetEquivalentGroupByComputedColumnOrNull(
          (*resolved_expr).get());
  if (existing_computed_column != nullptr) {
    // Make a reference to the existing column rather than recomputing the
    // expression.
    *group_by_column = existing_computed_column->column();
  } else {
    const IdString* query_alias = &kGroupById;
    if (query_resolution_info->IsGqlReturn() ||
        query_resolution_info->IsGqlWith()) {
      query_alias = &kGraphTableId;
    }
    *group_by_column = ResolvedColumn(AllocateColumnId(), *query_alias, alias,
                                      (*resolved_expr)->annotated_type());
  }

  if (precomputed_lateral_select_column_state != nullptr) {
    precomputed_lateral_select_column_state->resolved_select_column =
        *group_by_column;
  }

  // If the 'resolved_expr' is a path expression, we must collect
  // information in the 'query_resolution_info' about that path
  // expression and its relationship to the 'group_by_column'.
  // This information will get used later when constructing a
  // NameScope for what names are valid post-GROUP BY, where the
  // new NameScope is used when resolving subsequent expressions.
  // If the 'resolved_expr' is not a path expression then this
  // is a no-op.
  GOOGLESQL_RETURN_IF_ERROR(CollectResolvedPathExpressionInfoIfRelevant(
      query_resolution_info, resolved_expr->get(), *group_by_column));

  return absl::OkStatus();
}

absl::Status Resolver::AddSelectColumnToGroupByAllComputedColumn(
    const SelectColumnState* select_column_state,
    QueryResolutionInfo* query_resolution_info) {
  auto column_desc = [select_column_state]() {
    if (IsInternalAlias(select_column_state->alias)) {
      return absl::StrCat("Column in position ",
                          select_column_state->select_list_position + 1);
    } else {
      return absl::StrCat("Column `", select_column_state->alias.ToStringView(),
                          "`");
    }
  };

  // Check if the type of the group by column contains volatile function.
  if (select_column_state->expr_findings.has_volatile) {
    return MakeSqlErrorAt(select_column_state->ast_expr)
           << column_desc() << ", which is included in the grouping list by "
           << "GROUP BY ALL, contains a volatile expression which must be "
           << "explicitly listed as a group by key. To include this expression "
           << "in GROUP BY, explicitly enumerate group by columns";
  }

  // Resolve group by expression to a post group by column.
  std::unique_ptr<const ResolvedExpr> group_by_expr;
  const ResolvedExpr* pre_group_by_expr = nullptr;
  ResolvedColumn group_by_column;
  GOOGLESQL_RETURN_IF_ERROR(HandleGroupBySelectColumn(
      select_column_state, query_resolution_info, &group_by_expr,
      &pre_group_by_expr, &group_by_column));
  GOOGLESQL_RET_CHECK(group_by_expr != nullptr);
  GOOGLESQL_RET_CHECK(pre_group_by_expr != nullptr);

  // Check if the type of the group by column is groupable.
  std::string no_grouping_type;
  if (!group_by_expr->type()->SupportsGrouping(language(), &no_grouping_type)) {
    return MakeSqlErrorAt(select_column_state->ast_expr)
           << column_desc() << ", which is included in the grouping list by "
           << "GROUP BY ALL, has type " << no_grouping_type
           << " which cannot be used in a grouping key";
  }

  query_resolution_info->AddGroupByComputedColumnIfNeeded(
      group_by_column, std::move(group_by_expr), pre_group_by_expr,
      /*override_existing_column=*/false);
  return absl::OkStatus();
}

struct PathExpressionSelectItemForGroupBy {
  PathExpressionSelectItemForGroupBy(const ResolvedColumn& source_column,
                                     const ValidNamePath& field_path, int index)
      : source_column(source_column),
        field_path(field_path),
        select_list_index(index) {}
  ResolvedColumn source_column;
  ValidNamePath field_path;
  int select_list_index;

  bool operator<(const PathExpressionSelectItemForGroupBy& another) const {
    return field_path.name_path().size() <
               another.field_path.name_path().size() ||
           (field_path.name_path().size() ==
                another.field_path.name_path().size() &&
            select_list_index < another.select_list_index);
  }
};

absl::Status Resolver::ResolveGroupByAll(
    const ASTGroupBy* group_by, const NameScope* from_clause_scope,
    QueryResolutionInfo* query_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (!language().LanguageFeatureEnabled(FEATURE_GROUP_BY_ALL) &&
      !ShouldApplyImplicitGroupByAll(language(), query_resolution_info)) {
    return MakeSqlErrorAt(group_by->all()) << "GROUP BY ALL is not supported";
  }

  for (const auto& select_column_state :
       query_resolution_info->select_column_state_list()
           ->select_column_state_list()) {
    if (select_column_state->lateral_reference_state
            ->is_referenced_laterally()) {
      return MakeSqlErrorAt(select_column_state->ast_expr)
             << "Column " << select_column_state->alias.ToStringView() << " is "
             << "referenced laterally; GROUP BY ALL is not supported when "
             << "lateral references are present";
    }
  }

  // No grouping items should show up in the syntax.
  if (group_by != nullptr) {
    GOOGLESQL_RET_CHECK(group_by->grouping_items().empty());
  }

  query_resolution_info->set_is_group_by_all(true);
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();

  // Record the minimum number of columns in the select list that are needed in
  // GROUP BY ALL.
  //
  // The GROUP BY list excludes any select list expression that is aggregate,
  // windowed, or does not reference any column in the current FROM clause. Such
  // expression could be constant, or only contains correlated references to
  // external scope, or only contains references to other column references that
  // are already considered grouping keys.
  //
  // In the first pass, sort all the select items that are path expressions
  // (with one or more names) based on the length of the path.
  //
  // In the second pass, build a ValidFieldInfoMap out of the columns that come
  // from the current FROM scope. The map records all of the unique
  // ResolvedColumns and paths of simple fields that start from those unique
  // columns.
  //
  // If multiple field paths share the same field path as common prefix, we only
  // record the shortest field path which is the common prefix. Only the select
  // items with those shortest field paths will be added to the group by list.
  // For example, given (a.b, a.b.c, a.b.d.e), the map will only record an entry
  // {a, [("b")]}. Given (a, a.b.c, x.y.z, x.y), the map will record two entries
  // {a, [()]} and {x, [("y")]}.
  //
  // In the third pass, check if any of the other select items have
  // non-correlated column references or field path that does not show up in the
  // map. If so, add them to the group by list.
  //
  // In the last pass, we build the final group by list by sorting the chosen
  // select list items based on their original indexes in the select list.
  absl::flat_hash_set<int> skip_column_positions;
  std::vector<PathExpressionSelectItemForGroupBy> path_expr_select_items;

  // First pass - Eliminate aggregate and window functions and identify select
  // list items that are simple non-correlated path expressions.
  for (int i = 0; i < select_column_state_list->Size(); i++) {
    const SelectColumnState* select_column_state =
        select_column_state_list->GetSelectColumnState(i);

    // A groupable column should not contain aggregate or analytic function.
    if (select_column_state->expr_findings.has_aggregation ||
        select_column_state->expr_findings.has_analytic) {
      skip_column_positions.insert(i);
      continue;
    }

    const ResolvedExpr* select_expr =
        GetPreGroupByResolvedExpr(select_column_state);
    ResolvedColumn source_column;
    bool is_correlated;
    ValidNamePath select_expr_name_path;
    if (select_expr != nullptr &&
        GetSourceColumnAndNamePath(
            select_expr, /*target_column=*/ResolvedColumn(), &source_column,
            &is_correlated, &select_expr_name_path, id_string_pool_)) {
      // If we identify a path expression, we do not need to look at it any more
      // when we go over the select list items again in the third pass.
      skip_column_positions.insert(i);
      // If it's a correlated expression that does not reference any FROM scope
      // column, ignore this select item.
      if (is_correlated) {
        continue;
      }
      path_expr_select_items.push_back(PathExpressionSelectItemForGroupBy(
          source_column, select_expr_name_path, i));
    }
  }
  std::sort(path_expr_select_items.begin(), path_expr_select_items.end());

  // Second pass - Identify which of the path expressions must be a group by key
  // because it references the FROM clause and no other group by key is a prefix
  // of it.
  ValidFieldInfoMap chosen_group_by_keys;
  std::vector<int> final_group_by_item_indexes;
  for (const PathExpressionSelectItemForGroupBy& path_expr_select_item :
       path_expr_select_items) {
    const ValidNamePathList* old_name_path_list;
    ResolvedColumn target_column;
    int num_names_consumed;

    // If the chosen group by keys already contain a prefix of the current path
    // expression, we ignore the current select item.
    if (chosen_group_by_keys.LookupNamePathList(
            path_expr_select_item.source_column, &old_name_path_list) &&
        ValidFieldInfoMap::FindLongestMatchingPathIfAny(
            *old_name_path_list, path_expr_select_item.field_path.name_path(),
            &target_column, &num_names_consumed)) {
      continue;
    }

    // Otherwise, update the map and the group by list.
    chosen_group_by_keys.InsertNamePath(path_expr_select_item.source_column,
                                        path_expr_select_item.field_path);
    final_group_by_item_indexes.push_back(
        path_expr_select_item.select_list_index);
  }

  // Third pass - For the rest of the select list items, identify those that
  // contain at least one path expression that references the FROM clause but no
  // other group by key is a prefix of it.
  for (int i = 0; i < select_column_state_list->Size(); i++) {
    if (skip_column_positions.contains(i)) {
      continue;
    }
    const SelectColumnState* select_column_state =
        select_column_state_list->GetSelectColumnState(i);
    const ResolvedExpr* select_expr =
        GetPreGroupByResolvedExpr(select_column_state);
    GOOGLESQL_RET_CHECK(select_expr != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(bool no_from_scope_reference,
                     AllPathsInExprHaveExpectedPrefixes(
                         *select_expr, chosen_group_by_keys, id_string_pool_));
    if (no_from_scope_reference) {
      continue;
    }
    final_group_by_item_indexes.push_back(i);
  }

  // Last pass
  std::sort(final_group_by_item_indexes.begin(),
            final_group_by_item_indexes.end());
  for (int select_list_index : final_group_by_item_indexes) {
    GOOGLESQL_RETURN_IF_ERROR(AddSelectColumnToGroupByAllComputedColumn(
        select_column_state_list->GetSelectColumnState(select_list_index),
        query_resolution_info));
  }

  // There are a couple of situations when neither the group by list nor the
  // aggregate list contains any columns. They act like group by 0 column which
  // produce one row of output.
  if (query_resolution_info->group_by_column_state_list().empty() &&
      query_resolution_info
              ->num_aggregate_columns_to_compute_across_all_scopes() == 0) {
    query_resolution_info->SetHasAggregation(true);
    query_resolution_info->set_has_group_by(false);
  }
  return absl::OkStatus();
}

// Resolves a list of expressions from the same grouping set, while the grouping
// set type is specified by the kind argument.
//
// For each expression
// 1. If it's a ASTStructConstructorWithParens, then extract all field
//    expressions from the struct constructor, and resolve each field expression
//    to grouping set column. This is for the use case of multi-columns, e.g.
//    In the example ROLLUP((x, y), z), (x, y) is parsed to an
//    ASTStructConstructorWithParens, so we have to extract sub-expressions in
//    the parentheses manually at first.
// 2. Otherwise, resolve the expression to a grouping set column.
//
// Resolved columns will be added to GroupingSetInfo and returned later.
// Each grouping item may have multiple columns, and they are added into
// ResolvedComputedColumnList, e.g. ROLLUP(x, y) has two grouping set item.
// For multi-columns grouping set item, the column list contains columns
// extracted from the ASTStructConstructor. For non-multi-columns, the column
// list contains only one column resolved from the expression. E.g. ROLLUP((x,
// y), z) has two grouping set item, the first item's column list is [x, y] and
// the second is [z].
//
// Example for a query with the following grouping sets specifications:
//   GROUPING SETS(x, (y, z), ROLLUP(x, y), CUBE((x, y), z))
// The 4 GroupingSetInfos look like accordingly:
//
// GroupingSetInfo {
//    kind: kGroupingSet
//    grouping_set_item_list: [[x]]
// }
// GroupingSetInfo {
//    kind: kGroupingSet
//    grouping_set_item_list: [[y], [z]]
// }
// GroupingSetInfo {
//    kind: kRollup
//    grouping_set_item_list: [[x], [y]]
// }
// GroupingSetInfo {
//    kind: kCube
//    grouping_set_item_list: [[x, y], [z]]
// }
absl::StatusOr<GroupingSetInfo> Resolver::ResolveGroupingSetExpressions(
    const absl::Span<const ASTExpression* const> expressions,
    const NameScope* from_clause_scope, GroupingSetKind kind,
    QueryResolutionInfo* query_resolution_info) {
  GroupingSetInfo grouping_set_info = {.kind = kind};
  for (const ASTExpression* expr : expressions) {
    ResolvedComputedColumnList column_list;
    // The expr is null when it's an empty grouping set. In this case, we simply
    // put the empty column list to grouping set item list and continue, without
    // going through the resolving process.
    if (expr == nullptr) {
      // A empty grouping_set_info will be added to the grouping set list.
      continue;
    }
    // Resolve the single-level annoymous struct construtor to a column list,
    // the expression has been validated without nested struct construtors.
    // Only resolve the annoymous struct construtor to a column list when:
    // 1. The current grouping set is a cube, OR
    // 2. The current groupint set is rollup, and FEATURE_GROUPING_SETS is
    //    enabled. We will not change the existing behavior of ROLLUP when
    //    FEATURE_GROUPING_SETS is not enabled.
    if (expr->Is<ASTStructConstructorWithParens>() &&
        (kind != GroupingSetKind::kRollup ||
         language().LanguageFeatureEnabled(FEATURE_GROUPING_SETS))) {
      const ASTStructConstructorWithParens* struct_constructor =
          expr->GetAsOrDie<ASTStructConstructorWithParens>();
      for (const ASTExpression* field_expression :
           struct_constructor->field_expressions()) {
        GOOGLESQL_RET_CHECK(field_expression != nullptr);
        GOOGLESQL_RETURN_IF_ERROR(ResolveGroupingItemExpression(
            field_expression,
            /*ast_alias=*/nullptr,
            /*ast_grouping_item_order=*/nullptr, from_clause_scope,
            /*from_grouping_set=*/true, query_resolution_info, &column_list));
      }
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ResolveGroupingItemExpression(
          expr,
          /*ast_alias=*/nullptr,
          /*ast_grouping_item_order=*/nullptr, from_clause_scope,
          /*from_grouping_set=*/true, query_resolution_info, &column_list));
    }
    grouping_set_info.grouping_set_item_list.push_back(column_list);
  }
  return grouping_set_info;
}

// Analyzes group by items. If the group by item is a GROUPING SETS, ROLLUP, or
// CUBE, validate them accordingly and resolve the list of expressions by
// ResolveGroupingSetExpressions. If the group by item is a normal expression or
// column, simply resolve it via ResolveGroupingItemExpression.
absl::Status Resolver::ResolveGroupByExprs(
    const ASTGroupBy* group_by, const NameScope* from_clause_scope,
    QueryResolutionInfo* query_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // The resolution of GROUP BY ALL syntax should not reach this code path.
  GOOGLESQL_RET_CHECK(group_by->all() == nullptr);

  bool is_multi_grouping_sets = IsMultiGroupingSets(group_by);
  bool has_rollup_or_cube = false;
  for (const ASTGroupingItem* grouping_item : group_by->grouping_items()) {
    if (grouping_item->alias() != nullptr) {
      // The parser only allows aliases on expressions.
      GOOGLESQL_RET_CHECK(grouping_item->expression() != nullptr);
    }
    if (grouping_item->grouping_item_order() != nullptr) {
      // The parser only allows ASC/DESC ordering suffixes on expressions.
      GOOGLESQL_RET_CHECK(grouping_item->expression() != nullptr);
    }

    if (grouping_item->rollup() != nullptr) {
      // GROUP BY ROLLUP
      if (query_resolution_info->group_by_has_and_order_by()) {
        return MakeSqlErrorAt(grouping_item)
               << "GROUP AND ORDER BY cannot be used with ROLLUP";
      }
      has_rollup_or_cube = true;
      const ASTRollup* rollup = grouping_item->rollup();
      GOOGLESQL_RETURN_IF_ERROR(ValidateRollup(rollup, language(),
                                     group_by->grouping_items().size()));
      GOOGLESQL_ASSIGN_OR_RETURN(GroupingSetInfo grouping_set_info,
                       ResolveGroupingSetExpressions(
                           rollup->expressions(), from_clause_scope,
                           GroupingSetKind::kRollup, query_resolution_info));
      query_resolution_info->AddGroupingSetList({grouping_set_info});
    } else if (grouping_item->cube() != nullptr) {
      // GROUP BY CUBE
      if (query_resolution_info->group_by_has_and_order_by()) {
        return MakeSqlErrorAt(grouping_item)
               << "GROUP AND ORDER BY cannot be used with CUBE";
      }
      has_rollup_or_cube = true;
      const ASTCube* cube = grouping_item->cube();
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateCube(cube, language(), group_by->grouping_items().size()));
      GOOGLESQL_ASSIGN_OR_RETURN(GroupingSetInfo grouping_set_info,
                       ResolveGroupingSetExpressions(
                           cube->expressions(), from_clause_scope,
                           GroupingSetKind::kCube, query_resolution_info));
      query_resolution_info->AddGroupingSetList({grouping_set_info});
    } else if (grouping_item->grouping_set_list() != nullptr) {
      // GROUP BY GROUPING SETS
      if (query_resolution_info->group_by_has_and_order_by()) {
        return MakeSqlErrorAt(grouping_item)
               << "GROUP AND ORDER BY cannot be used with GROUPING SETS";
      }
      const ASTGroupingSetList* grouping_set_list =
          grouping_item->grouping_set_list();
      GOOGLESQL_RETURN_IF_ERROR(ValidateGroupingSetList(
          grouping_set_list, language(), group_by->grouping_items().size()));
      GroupingSetInfoList grouping_set_info_list;
      grouping_set_info_list.reserve(grouping_set_list->grouping_sets().size());
      for (const ASTGroupingSet* grouping_set :
           grouping_set_list->grouping_sets()) {
        GOOGLESQL_RET_CHECK(grouping_set != nullptr);
        GroupingSetInfo grouping_set_info;
        if (grouping_set->rollup() != nullptr) {
          // ROLLUP in GROUP BY GROUPING SETS()
          has_rollup_or_cube = true;
          GOOGLESQL_ASSIGN_OR_RETURN(
              grouping_set_info,
              ResolveGroupingSetExpressions(
                  grouping_set->rollup()->expressions(), from_clause_scope,
                  GroupingSetKind::kRollup, query_resolution_info));
        } else if (grouping_set->cube() != nullptr) {
          // CUBE in GROUP BY GROUPING SETS()
          has_rollup_or_cube = true;
          GOOGLESQL_ASSIGN_OR_RETURN(
              grouping_set_info,
              ResolveGroupingSetExpressions(
                  grouping_set->cube()->expressions(), from_clause_scope,
                  GroupingSetKind::kCube, query_resolution_info));
        } else {
          // Column list in GROUP BY GROUPING SETS or empty grouping set ()
          const ASTExpression* expr = grouping_set->expression();
          std::vector<const ASTExpression*> grouping_set_item_expr_list;
          // This is an anonymous struct construtor, we treat it as a grouping
          // set item list. The node is already validated that there is no
          // nested struct constructor inside.
          if (expr != nullptr && expr->Is<ASTStructConstructorWithParens>()) {
            const ASTStructConstructorWithParens* struct_constructor =
                expr->GetAsOrDie<ASTStructConstructorWithParens>();
            GOOGLESQL_RET_CHECK(struct_constructor != nullptr);
            for (const ASTExpression* field_expression :
                 struct_constructor->field_expressions()) {
              GOOGLESQL_RET_CHECK(field_expression != nullptr);
              grouping_set_item_expr_list.push_back(field_expression);
            }
          } else {
            // This is a single expression or an empty grouping set
            grouping_set_item_expr_list.push_back(expr);
          }
          GOOGLESQL_ASSIGN_OR_RETURN(
              grouping_set_info,
              ResolveGroupingSetExpressions(
                  grouping_set_item_expr_list, from_clause_scope,
                  GroupingSetKind::kGroupingSet, query_resolution_info));
        }
        grouping_set_info_list.push_back(grouping_set_info);
      }
      query_resolution_info->AddGroupingSetList(
          std::move(grouping_set_info_list));
    } else if (grouping_item->expression() != nullptr) {
      // GROUP BY expressions
      if (grouping_item->grouping_item_order() != nullptr) {
        if (!query_resolution_info->IsPipeAggregate()) {
          return MakeSqlErrorAt(grouping_item->grouping_item_order())
                 << "GROUP BY does not support order specification outside "
                    "pipe AGGREGATE";
        }
      }

      // An expression is treated as a GROUPING SETS item when
      // `is_multi_grouping_sets` is true.
      // For example, GROUP BY ROLLUP(a, b), c, d is treated as
      // GROUP BY ROLLUP(a, b), GROUPING SET(c, d)
      ResolvedComputedColumnList column_list;
      GOOGLESQL_RETURN_IF_ERROR(ResolveGroupingItemExpression(
          grouping_item->expression(), grouping_item->alias(),
          grouping_item->grouping_item_order(), from_clause_scope,
          /*from_grouping_set=*/is_multi_grouping_sets, query_resolution_info,
          &column_list));
      if (is_multi_grouping_sets) {
        GroupingSetInfo grouping_set_info = {.kind =
                                                 GroupingSetKind::kGroupingSet};
        grouping_set_info.grouping_set_item_list.push_back(column_list);
        query_resolution_info->AddGroupingSetList({grouping_set_info});
      }
    } else {
      // This is GROUP BY ()
      // We only allow GROUP BY () being used without other grouping items.
      if (language().LanguageFeatureEnabled(FEATURE_GROUPING_SETS)) {
        if (group_by->grouping_items().size() > 1) {
          return MakeSqlErrorAt(grouping_item)
                 << "GROUP BY () is only allowed when there are no other "
                    "grouping items";
        }
      } else {
        return MakeSqlErrorAt(grouping_item) << "GROUP BY () is not supported";
      }
      if (query_resolution_info->group_by_has_and_order_by()) {
        return MakeSqlErrorAt(grouping_item)
               << "GROUP AND ORDER BY cannot be used with GROUP BY ()";
      }
    }

    // GROUP AND ORDER BY is only allowed on expression items.
    // Errors should have been produced in all other cases above.
    GOOGLESQL_RET_CHECK(grouping_item->expression() != nullptr ||
              !query_resolution_info->group_by_has_and_order_by());
  }
  return absl::OkStatus();
}

// Analyze the GROUP BY expression.  Map SELECT list ordinal and
// alias references to the appropriate SelectColumnState in
// <query_resolution_info>, and resolve other expressions against
// the <from_clause_scope>.  For GROUP BY expressions that
// are SELECT list items, update the related SelectColumnState
// information with the grouped version of the column.  Updates
// query_resolution_info with the GROUP BY ResolvedColumns and
// ResolvedComputedColumns.
//
// Note that the current logic will not allow the use of SELECT list
// aliases within GROUP BY expressions, because we only match aliases
// exactly.  For example, the following is not supported:
//
//   select a+b as foo
//   ...
//   group by foo + 1;
//
// However, in most cases these types of queries would be invalid even
// without this restriction anyway since the SELECT list expression would
// not be something that was grouped by.  But it is possible to have a
// valid example (though highly contrived), such as:
//
//   select a+b as foo
//   ...
//   group by foo, foo + 1
//
// The general logic implemented by this function for each GROUP BY column is:
// 1) Determine if the GROUP BY expression exactly matches a SELECT list alias
//    or is an integer literal (representing a SELECT list column ordinal)
// 2) If either, update the corresponding SelectColumnState
// 3) If neither, resolve the expression against the <from_clause_scope>
// 4) Provide an error if grouping by literals, constant expressions, structs,
//    protos, or arrays.
// 5) If grouping by a column, update the mapping from pre-GROUP BY column to
//    post-GROUP BY column.
// 6) If grouping by an expression, update the mapping from the pre-GROUP BY
//    expression to the post-GROUP BY column.
// 7) Add a ResolvedComputedColumn for the GROUP BY expression.
// 8) If the expression is from a grouping set, then add the
//    ResolvedComputedColumn to the passed-in GroupingSetItem.
// 9) For pipe aggregate, record the item in PipeExtraSelectItems so the column
//    can be added to the output.
// 10) If adding an implicit order by for this GROUP BY column, add an entry
//     to group_by_order_by_item_info.
absl::Status Resolver::ResolveGroupingItemExpression(
    const ASTExpression* ast_group_by_expr, const ASTAlias* ast_alias,
    const ASTGroupingItemOrder* ast_grouping_item_order,
    const NameScope* from_clause_scope, bool from_grouping_set,
    QueryResolutionInfo* query_resolution_info,
    ResolvedComputedColumnList* column_list) {
  GOOGLESQL_RET_CHECK(ast_group_by_expr != nullptr);
  if (from_grouping_set) {
    GOOGLESQL_RET_CHECK(column_list != nullptr);
  }

  auto no_aggregation =
      std::make_unique<ExprResolutionInfo>(from_clause_scope, "GROUP BY");

  ABSL_DCHECK_NE(ast_group_by_expr->node_kind(), AST_IDENTIFIER)
      << "We expect to get PathExpressions, not Identifiers here";

  const SelectColumnState* group_by_column_state = nullptr;
  // Determine if the GROUP BY expression exactly matches a SELECT list alias.
  if (ast_group_by_expr->node_kind() == AST_PATH_EXPRESSION
      // For pipe AGGREGATE, we bypass this case that matches GROUP BY
      // expressions against SELECT-list aliases.  We don't want aliases
      // of aggregate expressions to be referencable in the GROUP BY.
      && !query_resolution_info->IsPipeAggregate()) {
    const ASTPathExpression* path_expr =
        ast_group_by_expr->GetAsOrDie<ASTPathExpression>();
    const IdString alias = path_expr->first_name()->GetAsIdString();
    GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->select_column_state_list()
                        ->FindAndValidateSelectColumnStateByAlias(
                            /*clause_name=*/"GROUP BY clause",
                            ast_group_by_expr, alias, no_aggregation.get(),
                            &group_by_column_state));
    if (group_by_column_state != nullptr && path_expr->num_names() != 1) {
      // We resolved the first identifier in a path expression to a SELECT
      // list alias.  There is currently no way that accessing the column's
      // fields in the GROUP BY can possibly be valid.  Consider:
      //   SELECT foo as foo2
      //   FROM (select as struct 1 as a, 2 as b) foo
      //   GROUP BY foo2.a
      // This is invalid since 'foo' is in the SELECT list but it is not
      // in the GROUP BY.
      //
      // If we add foo2 to the GROUP BY, then the query is invalid since
      // we do not allow grouping by STRUCT.
      //
      // If we were to allow grouping by PROTO or STRUCT then the following
      // query could be valid, but at this time it is not valid:
      //   SELECT foo as foo2
      //   FROM (select as struct 1 as a, 2 as b) foo
      //   GROUP BY foo, foo.a, foo.b;
      return MakeSqlErrorAt(ast_group_by_expr)
             << "Cannot GROUP BY field references from SELECT list alias "
             << alias;
    }
  }

  std::unique_ptr<const ResolvedExpr> resolved_expr;
  const ResolvedExpr* pre_group_by_expr = nullptr;
  // This means the current group by column is not a select column alias.
  if (group_by_column_state == nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_group_by_expr, from_clause_scope,
                                      "GROUP BY", &resolved_expr));

    // Determine if the GROUP BY expression is an integer literal
    // representing a SELECT list column ordinal.  Look for GROUP BY 1,2,3.
    if (resolved_expr->node_kind() == RESOLVED_LITERAL &&
        !resolved_expr->GetAs<ResolvedLiteral>()->has_explicit_type()) {
      const Value& value = resolved_expr->GetAs<ResolvedLiteral>()->value();
      if (value.type_kind() == TYPE_INT64 && !value.is_null()) {
        if (query_resolution_info->IsPipeAggregate()) {
          return MakeSqlErrorAt(ast_group_by_expr)
                 << "GROUP BY ordinal not allowed in pipe AGGREGATE";
        }
        if (query_resolution_info->IsGqlReturn()) {
          return MakeSqlErrorAt(ast_group_by_expr)
                 << "GROUP BY ordinal is not allowed in RETURN";
        }
        if (query_resolution_info->IsGqlWith()) {
          return MakeSqlErrorAt(ast_group_by_expr)
                 << "GROUP BY ordinal is not allowed in WITH";
        }
        GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->select_column_state_list()
                            ->FindAndValidateSelectColumnStateByOrdinal(
                                /*expr_description=*/"GROUP BY",
                                ast_group_by_expr, value.int64_value(),
                                no_aggregation.get(), &group_by_column_state));
      }
    }
  }

  IdString alias = GetAliasForExpression(ast_group_by_expr);
  if (ast_alias != nullptr) {
    if (!query_resolution_info->IsPipeAggregate()) {
      return MakeSqlErrorAt(ast_alias) << "GROUP BY does not support aliases";
    }
    alias = ast_alias->GetAsIdString();
  } else {
    if (alias.empty()) {
      // Fail the query if ROLLUP/CUBE/GROUPING SETS contains anonymous
      // columns in pipe AGGREGATE, as anonymous columns are not querable in the
      // next pipe operator.
      if (from_grouping_set && query_resolution_info->IsPipeAggregate()) {
        return MakeSqlErrorAt(ast_group_by_expr)
               << "Anonymous columns are not allowed in grouping sets; "
               << "Consider using the EXTEND operator to pre-compute these "
                  "expressions and assign names, and then reference those "
                  "columns by name here";
      }
      alias = MakeIdString(absl::StrCat(
          "$groupbycol",
          query_resolution_info->group_by_column_state_list().size() + 1));
    }
  }

  ResolvedColumn group_by_column;
  // This means the current group by column is a select column alias.
  if (group_by_column_state != nullptr) {
    if (group_by_column_state->is_group_by_column) {
      // We are already grouping by this SELECT list column, so we do not need
      // to do more unless the query uses GROUPING SETS, in which case we need
      // to add another entry in the grouping set list for it.
      if (!from_grouping_set) {
        // This early exit seems unreachable, so hasn't been tested.
        // We don't want by bypass PipeExtraSelectItem tracking below.
        GOOGLESQL_RET_CHECK(!query_resolution_info->IsPipeAggregate());
        return absl::OkStatus();
      }

      const ResolvedComputedColumn* existing_computed_column = nullptr;
      for (const GroupByColumnState& group_by_column :
           query_resolution_info->group_by_column_state_list()) {
        if (group_by_column_state->resolved_select_column ==
            group_by_column.computed_column->column()) {
          existing_computed_column = group_by_column.computed_column.get();
          break;
        }
      }

      GOOGLESQL_RET_CHECK_NE(existing_computed_column, nullptr)
          << "Expected to find existing group by column matching "
          << group_by_column_state->resolved_select_column.DebugString();
      // Field paths may repeat inside the grouping set list. We have already
      // resolved this field path, so just add another entry for it.
      column_list->push_back(existing_computed_column);
      return absl::OkStatus();
    }
    GOOGLESQL_RETURN_IF_ERROR(HandleGroupBySelectColumn(
        group_by_column_state, query_resolution_info, &resolved_expr,
        &pre_group_by_expr, &group_by_column));
  } else {
    // If the same expression already has a computed column, then reuse the
    // existing computed column. This is to make sure the group_by_list only has
    // one expression when multiple duplicated expressions appear in the group
    // by clause.
    //
    // When FEATURE_GROUPING_SETS is enabled, expressions in GROUPING
    // SETS/ROLLUP/CUBE will be de-duplicated to avoid expression matching
    // ambigulity.
    //
    // When it's a pipe AGGREGATE operator, we throw errors if two expressions
    // from ROLLUP/CUBE/GROUPING SETS may actually be the same but
    // IsSameExpressionForGroupBy is unable to tell due to unsupported
    // expression types in this method. Duplicate expressions cause ambiguity in
    // the output columns of pipe AGGREGATE, which is a problem since the output
    // column list is inferred from GROUP BY.
    bool check_is_same_expressions =
        language().LanguageFeatureEnabled(FEATURE_GROUPING_SETS) ||
        query_resolution_info->IsPipeAggregate();

    if (from_grouping_set && check_is_same_expressions) {
      for (const GroupByColumnState& group_by_column :
           query_resolution_info->group_by_column_state_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            TestIsSameExpressionForGroupByResult same_expression_result,
            TestIsSameExpressionForGroupBy(
                resolved_expr.get(), group_by_column.computed_column->expr(),
                language()));

        switch (same_expression_result) {
          case TestIsSameExpressionForGroupByResult::kEqual:
            // Reuse the resolved expression if it's the same as an existing
            // expression.
            column_list->push_back(group_by_column.computed_column.get());
            return absl::OkStatus();
            // Continue if the expression is different.
          case TestIsSameExpressionForGroupByResult::kNotEqual:
            break;
            // Throw errors if we are unable to tell whether they are same in
            // pipe AGGREGATE. This happens when expressions of the same type
            // are not handled in TestIsSameExpressionForGroupBy.
          case TestIsSameExpressionForGroupByResult::kUnknown:
            if (query_resolution_info->IsPipeAggregate()) {
              return MakeSqlErrorAt(ast_group_by_expr)
                     << "Unable to check whether multiple grouping set "
                        "expressions are equivalent, so the output column list "
                        "cannot be determined; Consider using the EXTEND "
                        "operator to pre-compute these expressions and assign "
                        "names, and then reference those columns by name here";
            }
            break;
        }
      }
    }

    GOOGLESQL_RETURN_IF_ERROR(HandleGroupByExpression(
        ast_group_by_expr, query_resolution_info, alias, &resolved_expr,
        &pre_group_by_expr, &group_by_column));
  }
  GOOGLESQL_RET_CHECK(resolved_expr != nullptr);

  // Check if the type of the group by column is groupable.
  std::string no_grouping_type;
  if (!resolved_expr->type()->SupportsGrouping(language(), &no_grouping_type)) {
    return MakeSqlErrorAt(ast_group_by_expr)
           << "Grouping by expressions of type " << no_grouping_type
           << " is not allowed";
  }

  const ResolvedComputedColumn* computed_column =
      query_resolution_info->AddGroupByComputedColumnIfNeeded(
          group_by_column, std::move(resolved_expr), pre_group_by_expr,
          /*override_existing_column=*/false);
  if (from_grouping_set) {
    column_list->push_back(computed_column);
  }

  // For pipe AGGREGATE, record the GROUP BY item, which will become an
  // output column in the result table.
  if (query_resolution_info->IsPipeAggregate()) {
    query_resolution_info->pipe_extra_select_items()->push_back(
        PipeExtraSelectItem(alias, group_by_column));
  }

  // Set up the ordering column if we have GROUP AND ORDER BY or the
  // expression has ASC/DESC attached.
  if (ast_grouping_item_order != nullptr ||
      query_resolution_info->group_by_has_and_order_by()) {
    ResolvedOrderByItemEnums::NullOrderMode null_order =
        ResolvedOrderByItemEnums::ORDER_UNSPECIFIED;
    if (ast_grouping_item_order != nullptr) {
      if (!query_resolution_info->group_by_has_and_order_by() &&
          ast_grouping_item_order->null_order() != nullptr &&
          ast_grouping_item_order->ordering_spec() ==
              ASTOrderingExpression::UNSPECIFIED) {
        return MakeSqlErrorAt(ast_grouping_item_order->null_order())
               << "GROUP BY items cannot have NULLS FIRST/LAST without "
                  "ASC/DESC except when using GROUP AND ORDER BY";
      }
      GOOGLESQL_ASSIGN_OR_RETURN(null_order, ResolveNullOrderMode(
                                       ast_grouping_item_order->null_order()));
    }

    // We will order by <group_by_column> directly, rather than a
    // ResolvedExpression.  AddColumnsForOrderByExprs (which normally adds
    // these columns) will not be called on this vector.
    query_resolution_info->mutable_group_by_order_by_item_info()->emplace_back(
        ast_group_by_expr,
        /*ast_collate_in=*/nullptr, /*order_column=*/group_by_column,
        /*is_descending=*/
        (ast_grouping_item_order != nullptr
             ? ast_grouping_item_order->descending()
             : false),
        null_order);
  }

  return absl::OkStatus();
}

static std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
ChooseComputedColumnListForLaterallyReferencedColumn(
    QueryResolutionInfo* query_resolution_info, bool has_analytic,
    bool is_pinned_to_pre_grouping_context) {
  if (has_analytic) {
    return query_resolution_info
        ->successive_columns_to_compute_after_analytic();
  }

  if (query_resolution_info->HasGroupByOrAggregation() &&
      !is_pinned_to_pre_grouping_context) {
    // TODO: when we have a utility to check if this
    // expression is a grouping constant, we should add such columns to
    // the pre-grouping list instead. For now, we always make it a post-
    // grouping column.
    return query_resolution_info
        ->successive_columns_to_compute_after_aggregation();
  }

  return query_resolution_info
      ->successive_select_list_columns_to_compute_before_aggregation();
}

// Performs a second pass of analysis over a SELECT list column,
// (re)evaluating the expression if necessary.
//
// SELECT list expressions that already have a ResolvedColumn assigned
// are not re-resolved, unless they are referenced laterally (as those cases
// need some handling, like placing their computed columns in the right lists.)
// Those that do not are re-resolved against the post-GROUP BY NameScope
// <group_by_scope> (in this case the ResolvedExpr from the first pass is
// deleted).
//
// Aggregate expressions that were resolved in the first pass are not
// re-resolved, but use the ResolvedExpr from the first pass.
//
// All necessary computed columns are created and assigned to the relevant
// computed column list (dot-star computed columns, and computed columns that
// can be referenced by GROUP BY/etc.).
//
// After this pass, all SELECT list columns have initialized output
// ResolvedColumns.
absl::Status Resolver::ResolveSelectColumnSecondPass(
    IdString query_alias, const NameScope* group_by_scope,
    SelectColumnState* select_column_state,
    std::shared_ptr<NameList>* final_project_name_list,
    QueryResolutionInfo* query_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (select_column_state->is_group_by_column) {
    if (select_column_state->lateral_reference_state
            ->IsInvolvedInLateralReferences()) {
      // Intrgrity checks
      GOOGLESQL_RET_CHECK(select_column_state->resolved_select_column.IsInitialized())
          << select_column_state->DebugString();
      GOOGLESQL_RET_CHECK(select_column_state->resolved_pre_group_by_select_column
                    .IsInitialized())
          << select_column_state->DebugString();
      GOOGLESQL_RET_CHECK(select_column_state->lateral_reference_state
                    ->is_pinned_to_pre_grouping_context())
          << select_column_state->DebugString();
      GOOGLESQL_RET_CHECK(select_column_state->resolved_expr != nullptr)
          << select_column_state->DebugString();

      // No need to re-resolve. We only need to place the column in the
      // successive pre-aggregation list if it's more than a direct,
      // uncorrelated column reference, as it will be referenced by one of the
      // `group_by_column_list` items.
      if (!select_column_state->resolved_expr->Is<ResolvedColumnRef>() ||
          select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
              ->is_correlated()) {
        query_resolution_info
            ->successive_select_list_columns_to_compute_before_aggregation()
            ->push_back(MakeResolvedComputedColumn(
                select_column_state->resolved_pre_group_by_select_column,
                std::move(select_column_state->resolved_expr)));
      }
      // Now that we unloaded any computation and pushed it to the successive
      // list, we can finally place the actual SELECT expr, which should be a
      // column reference to the (post-grouping version) of the grouping column,
      // which is already stored in `resolved_select_column`.
      select_column_state->resolved_expr =
          MakeColumnRef(select_column_state->resolved_select_column);
    }
  } else if (!select_column_state->is_finalized) {
    // We already handled that case in the other branch.
    GOOGLESQL_RET_CHECK(!select_column_state->is_group_by_column);

    // If we have not already fully resolved this SELECT list expression
    // to a ResolvedColumn, then resolve the SELECT list expression.

    // First, look for SELECT list columns that resulted from star
    // expansion.  Star expansion was already performed in
    // ResolveSelectColumnFirstPass().   It is not done here.  The
    // star-expanded columns are often simple column references, but
    // could be GET_PROTO_FIELD or GET_STRUCT_FIELD if a FROM clause
    // subquery is SELECT AS PROTO or SELECT AS STRUCT.
    if (select_column_state->ast_expr->node_kind() == AST_DOT_STAR ||
        select_column_state->ast_expr->node_kind() ==
            AST_DOT_STAR_WITH_MODIFIERS ||
        select_column_state->ast_expr->node_kind() == AST_STAR ||
        select_column_state->ast_expr->node_kind() == AST_STAR_WITH_MODIFIERS) {
      // Re-resolve the dot-star's source if needed.
      if (select_column_state->dot_star_source_expr_info != nullptr) {
        if (!select_column_state->dot_star_source_expr_info->is_finalized) {
          GOOGLESQL_RET_CHECK(
              select_column_state->dot_star_source_expr_info->resolved_expr !=
              nullptr);
          GOOGLESQL_RET_CHECK(!select_column_state->is_group_by_column);
          GOOGLESQL_RETURN_IF_ERROR(ResolveDotStarSourceExpressionSecondPass(
              // This should be updated when we can compute whether the
              // dot-star source expression is a grouping constant (i.e., has a
              // functional dependency on the grouping keys), which means we can
              // precompute it before aggregation.
              select_column_state->dot_star_source_expr_info,
              query_resolution_info, group_by_scope));
          select_column_state->dot_star_source_expr_info->is_finalized = true;
          select_column_state->dot_star_source_expr_info->resolved_expr =
              nullptr;
        }

        // If the dot-star source column changed, update the current getter
        // expression to reflect this.
        ResolvedColumn original_dot_star_column =
            select_column_state->dot_star_source_expr_info
                ->original_resolved_column;
        const ResolvedColumn* new_src_column =
            query_resolution_info->GetDotStarColumnToRemapOrNull(
                original_dot_star_column);
        if (new_src_column != nullptr) {
          ColumnReplacementMap column_replacements;
          column_replacements.emplace(original_dot_star_column,
                                      *new_src_column);
          GOOGLESQL_ASSIGN_OR_RETURN(select_column_state->resolved_expr,
                           RemapSpecifiedColumns(
                               std::move(select_column_state->resolved_expr),
                               column_replacements));
        }
      }

      if (select_column_state->resolved_expr->node_kind() ==
              RESOLVED_COLUMN_REF &&
          !select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
               ->is_correlated()) {
        // We already have column references after star expansion.  Do not
        // re-resolve the expression.  Just update the SelectColumnState
        // to reflect the associated ResolvedColumn.
        if (query_resolution_info->HasGroupByOrAggregation()) {
          // If this column is part of the grouping key, mark the post-grouping
          // column ref as such.
          const ResolvedExpr* const original_resolved_expr =
              select_column_state->resolved_expr.get();
          GOOGLESQL_RETURN_IF_ERROR(ResolveColumnRefExprToPostGroupingColumn(
              select_column_state->ast_expr, /*clause_name=*/"Star expansion",
              query_resolution_info, &select_column_state->resolved_expr));

          if (original_resolved_expr !=
              select_column_state->resolved_expr.get()) {
            // <select_column_state->resolved_expr> has been replaced with the
            // post-grouping column.  This indicates that this column is part of
            // the grouping key.
            GOOGLESQL_RET_CHECK_EQ(RESOLVED_COLUMN_REF,
                         select_column_state->resolved_expr->node_kind());
            select_column_state->is_group_by_column = true;
          }
        }
        select_column_state->resolved_select_column =
            select_column_state->resolved_expr->GetAs<ResolvedColumnRef>()
                ->column();
      } else {
        ResolvedColumn select_column(
            AllocateColumnId(), query_alias, select_column_state->alias,
            select_column_state->resolved_expr->annotated_type());
        auto* list = query_resolution_info->select_list_columns_to_compute();
        if (select_column_state->lateral_reference_state
                ->is_referenced_laterally()) {
          // Place it in the right list.
          list = ChooseComputedColumnListForLaterallyReferencedColumn(
              query_resolution_info,
              select_column_state->expr_findings.has_analytic,
              select_column_state->lateral_reference_state
                  ->is_pinned_to_pre_grouping_context());
        }
        list->push_back(MakeResolvedComputedColumn(
            select_column, std::move(select_column_state->resolved_expr)));

        select_column_state->resolved_computed_column = list->back().get();
        // Update the SelectColumnState to reflect the associated
        // ResolvedColumn.
        select_column_state->resolved_select_column = select_column;
      }
      // Check if the source of the '*' is a correlated column reference,
      // i.e., 'outercol.*'.
      bool is_correlated_column_ref = false;
      if (select_column_state->resolved_computed_column != nullptr &&
          select_column_state->resolved_computed_column->expr()->node_kind() ==
              RESOLVED_COLUMN_REF &&
          select_column_state->resolved_computed_column->expr()
              ->GetAs<ResolvedColumnRef>()
              ->is_correlated()) {
        is_correlated_column_ref = true;
      }
      // If the query has grouping or aggregation, then check the
      // *-expansion columns to ensure that they are either outer correlation
      // references or they are being grouped by.  If not, then produce
      // an error.
      //
      // We do not do this check for columns with aggregation or analytic
      // functions because those column expressions will be re-resolved
      // against the post-GROUP BY NameScope and errors will be detected then.
      if (query_resolution_info->HasGroupByOrAggregation() &&
          !select_column_state->is_group_by_column &&
          !is_correlated_column_ref &&
          !select_column_state->expr_findings.has_aggregation &&
          !select_column_state->expr_findings.has_analytic) {
        return MakeSqlErrorAt(select_column_state->ast_expr)
               << "Star expansion expression references column "
               << select_column_state->alias
               << " which is neither grouped nor aggregated";
      }
    } else {
      auto resolved_expr_or_status = ResolveSelectExpressionSecondPass(
          select_column_state->ast_expr, select_column_state->alias,
          query_alias, group_by_scope, select_column_state->resolved_expr.get(),
          query_resolution_info);
      std::unique_ptr<const ResolvedExpr> resolved_expr;
      if (resolved_expr_or_status.ok()) {
        resolved_expr = std::move(*resolved_expr_or_status);
      } else {
        if (!select_column_state->lateral_reference_state
                 ->is_referenced_laterally() ||
            !absl::IsInvalidArgument(resolved_expr_or_status.status())) {
          // Propagate the error
          return resolved_expr_or_status.status();
        }

        // The expression did not match, but this expression was referenced
        // laterally. Check to see if the resolved column itself was already
        // marked as a grouping column directly. This is the case of a
        // subsequent column that is just aliasing this one, and then declared
        // as a grouping column, e.g.
        //   SELECT rand() AS a, a AS b
        //   FROM (SELECT 1)
        //   GROUP BY b;
        // The grouping expression is a mere column ref to `a`'s computed
        // column, so it won't match the actual expression, rather we should
        // just check with a ColRef to it.
        bool matched_group_by_column = false;
        for (const GroupByColumnState& group_by_column_state :
             query_resolution_info->group_by_column_state_list()) {
          auto col_ref = MakeColumnRef(
              select_column_state->resolved_pre_group_by_select_column);
          GOOGLESQL_ASSIGN_OR_RETURN(
              bool is_same_expr,
              IsSameExpressionForGroupBy(
                  col_ref.get(), group_by_column_state.computed_column->expr(),
                  language()));
          if (is_same_expr) {
            // This is now a grouping column. Mark it as such.
            matched_group_by_column = true;
            select_column_state->is_group_by_column = true;
            select_column_state->resolved_select_column =
                group_by_column_state.computed_column->column();
            // Replace the expression with a column ref, and put the computed
            // column in the successive pre-grouping list.
            query_resolution_info
                ->successive_select_list_columns_to_compute_before_aggregation()
                ->push_back(MakeResolvedComputedColumn(
                    select_column_state->resolved_pre_group_by_select_column,
                    std::move(select_column_state->resolved_expr)));
            resolved_expr =
                MakeColumnRef(group_by_column_state.computed_column->column());
            break;
          }
        }
        if (!matched_group_by_column) {
          // Propagate the error
          return resolved_expr_or_status.status();
        }
      }
      if (resolved_expr->Is<ResolvedColumnRef>() &&
          !resolved_expr->GetAs<ResolvedColumnRef>()->is_correlated()) {
        // The expression resolves to a simple uncorrelated column reference.
        // Just use it directly.
        select_column_state->resolved_select_column =
            resolved_expr->GetAs<ResolvedColumnRef>()->column();
      } else {
        // The expression is not a simple uncorrelated column reference, so we
        // cannot use it directly.
        GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
        if (!select_column_state->resolved_select_column.IsInitialized()) {
          select_column_state->resolved_select_column = ResolvedColumn(
              AllocateColumnId(), query_alias, select_column_state->alias,
              resolved_expr->annotated_type());
        }
        auto computed_column = MakeResolvedComputedColumn(
            select_column_state->resolved_select_column,
            std::move(resolved_expr));

        auto* list = query_resolution_info->select_list_columns_to_compute();
        if (select_column_state->lateral_reference_state
                ->is_referenced_laterally()) {
          // Place it in the right list.
          list = ChooseComputedColumnListForLaterallyReferencedColumn(
              query_resolution_info,
              select_column_state->expr_findings.has_analytic,
              select_column_state->lateral_reference_state
                  ->is_pinned_to_pre_grouping_context());
        }

        list->push_back(std::move(computed_column));

        if (query_resolution_info->HasGroupByOrAggregation()) {
          // This is a computation that resolves fine even though it is not a
          // grouping column and it doesn't match any grouping expression.
          // This is definitely a post-grouping column.
          //
          // TODO: Here, we need a utility to check if the
          // expression is a grouping constant (i.e., the expression is
          // functionally dependent on the grouping columns). When that is the
          // case, the column can be precomputed before grouping and be
          // referenced in pre-grouping contexts as well. We do not currently
          // have this capability so we we restrict these shapes.
          if (select_column_state->lateral_reference_state
                  ->is_pinned_to_pre_grouping_context()) {
            GOOGLESQL_RET_CHECK(!select_column_state->is_group_by_column);
            return MakeSqlErrorAt(select_column_state->ast_expr)
                   << "Column "
                   << ToIdentifierLiteral(select_column_state->alias)
                   << " is a post-grouping column but is also referenced "
                      "from a pre-grouping context";
          }
        }
      }
    }
  }

  GOOGLESQL_RETURN_IF_ERROR((*final_project_name_list)
                      ->AddColumn(select_column_state->alias,
                                  select_column_state->resolved_select_column,
                                  select_column_state->is_explicit));

  // If the select item has an ordering suffix (which can occur only in
  // pipe AGGREGATE), add the OrderByItemInfo for it, pointing at the
  // post-aggregate ResolvedColumn.
  const ASTGroupingItemOrder* ast_grouping_item_order =
      select_column_state->ast_grouping_item_order;
  if (ast_grouping_item_order != nullptr) {
    GOOGLESQL_RET_CHECK(query_resolution_info->IsPipeAggregate());

    GOOGLESQL_ASSIGN_OR_RETURN(
        const ResolvedOrderByItemEnums::NullOrderMode null_order,
        ResolveNullOrderMode(ast_grouping_item_order->null_order()));

    query_resolution_info->mutable_aggregate_order_by_item_info()->emplace_back(
        select_column_state->ast_expr,
        /*ast_collate_in=*/nullptr, select_column_state->resolved_select_column,
        ast_grouping_item_order->descending(), null_order);
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveDotStarSourceExpressionSecondPass(
    DotStarSourceExprInfo* dot_star_source_expr_info,
    QueryResolutionInfo* query_resolution_info,
    const NameScope* group_by_scope) {
  GOOGLESQL_RET_CHECK(!dot_star_source_expr_info->is_finalized);

  // For other scenarios, e.g. `(analytic_fn() OVER()).*`, we need to
  // re-resolve the expression.
  const ASTExpression* ast_src_expr = dot_star_source_expr_info->ast_expr;
  ResolvedColumn original_dot_star_column =
      dot_star_source_expr_info->original_resolved_column;
  GOOGLESQL_RET_CHECK(original_dot_star_column.IsInitialized());
  GOOGLESQL_RET_CHECK(ast_src_expr != nullptr);

  // Re-resolve the dot-star expression's source.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> dot_star_source_resolved_expr,
      ResolveSelectExpressionSecondPass(
          ast_src_expr, original_dot_star_column.name_id(),
          original_dot_star_column.table_name_id(), group_by_scope,
          dot_star_source_expr_info->resolved_expr.get(),
          query_resolution_info));

  if (dot_star_source_resolved_expr->Is<ResolvedColumnRef>() &&
      !dot_star_source_resolved_expr->GetAs<ResolvedColumnRef>()
           ->is_correlated()) {
    // If the column changed (e.g. due to the re-resolution of analytic
    // functions, or because we're now using a post-grouping column),
    // register it on the `query_resolution_info`.
    ResolvedColumn dot_star_resolved_column =
        dot_star_source_resolved_expr->GetAs<ResolvedColumnRef>()->column();
    if (dot_star_resolved_column != original_dot_star_column) {
      GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->AddDotStarColumnToRemap(
          original_dot_star_column, dot_star_resolved_column));
    }
    return absl::OkStatus();
  }

  // Instead of remapping columns, simply assign the original column
  // to the final required computation.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>* list;
  if (dot_star_source_expr_info->lateral_reference_state
          ->is_referenced_laterally()) {
    // Place in the appropriate "successive computation" list.
    list = ChooseComputedColumnListForLaterallyReferencedColumn(
        query_resolution_info,
        dot_star_source_expr_info->expr_findings.has_analytic,
        dot_star_source_expr_info->lateral_reference_state
            ->is_pinned_to_pre_grouping_context());
  } else {
    if (dot_star_source_expr_info->expr_findings.has_analytic) {
      list = query_resolution_info->columns_to_compute_after_analytic();
    } else if (query_resolution_info->HasGroupByOrAggregation()) {
      list = query_resolution_info->columns_to_compute_after_aggregation();
    } else {
      list = query_resolution_info
                 ->select_list_columns_to_compute_before_aggregation();
    }
  }
  list->push_back(MakeResolvedComputedColumn(
      dot_star_source_expr_info->original_resolved_column,
      std::move(dot_star_source_resolved_expr)));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveSelectExpressionSecondPass(
    const ASTExpression* ast_expr, IdString alias, IdString query_alias,
    const NameScope* group_by_scope,
    const ResolvedExpr* first_pass_resolved_expr,
    QueryResolutionInfo* query_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const char* clause_name = "SELECT list";
  if (query_resolution_info->IsPipeAggregate()) {
    clause_name = "AGGREGATE list";
  }
  if (query_resolution_info->IsGqlReturn()) {
    clause_name = "Graph RETURN operator";
  }
  if (query_resolution_info->IsGqlWith()) {
    clause_name = "Graph WITH operator";
  }
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info, group_by_scope, group_by_scope, group_by_scope,
      ExprResolutionInfoOptions{
          .allows_aggregation = true,
          .allows_analytic = true,
          .use_post_grouping_columns =
              query_resolution_info->HasGroupByOrAggregation(),
          .clause_name = clause_name,
          .top_level_ast_expr = ast_expr,
          .column_alias = alias,
      });
  expr_resolution_info->grouping_context =
      query_resolution_info->HasGroupByOrAggregation()
          ? ExprGroupingContext::kPostGrouping
          : ExprGroupingContext::kPreGrouping;

  std::unique_ptr<const ResolvedExpr> resolved_expr;
  const absl::Status resolve_expr_status =
      ResolveExpr(ast_expr, expr_resolution_info.get(), &resolved_expr);
  if (resolve_expr_status.ok()) {
    return resolved_expr;
  }

  GOOGLESQL_RET_CHECK_NE(first_pass_resolved_expr, nullptr);
  // Look at the QueryResolutionInfo to see if there is a GROUP BY
  // expression that exactly matches the ResolvedExpr from the
  // first pass resolution.
  // I didn't find any cases where the loop can find a match, but I don't
  // think we ever want matches here anyway, so I'm skipping the search
  // rather than asserting it finds nothing.
  if (!query_resolution_info->IsPipeAggregate()) {
    for (const GroupByColumnState& group_by_column_state :
         query_resolution_info->group_by_column_state_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          bool is_same_expr,
          IsSameExpressionForGroupBy(
              first_pass_resolved_expr,
              group_by_column_state.computed_column->expr(), language()));
      if (is_same_expr) {
        return MakeColumnRef(group_by_column_state.computed_column->column());
      }
    }
  }
  // TODO: Improve error message to say that expressions didn't
  // match.
  GOOGLESQL_RET_CHECK(!resolve_expr_status.ok());
  return resolve_expr_status;
}

absl::Status Resolver::ResolveSelectListExprsSecondPass(
    IdString query_alias, const NameScope* group_by_scope,
    std::shared_ptr<NameList>* final_project_name_list,
    QueryResolutionInfo* query_resolution_info) {
  SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();

  // Holds the names for "lateral" references.
  auto updated_group_by_namelist = std::make_shared<NameList>();
  std::unique_ptr<NameScope> updated_group_by_scope;
  GOOGLESQL_RETURN_IF_ERROR(group_by_scope->CopyNameScopeWithOverridingNames(
      updated_group_by_namelist, &updated_group_by_scope));

  for (const std::unique_ptr<SelectColumnState>& select_column_state :
       select_column_state_list->select_column_state_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveSelectColumnSecondPass(
        query_alias, updated_group_by_scope.get(), select_column_state.get(),
        final_project_name_list, query_resolution_info));

    // Some integrity checks.
    GOOGLESQL_RET_CHECK(select_column_state->GetType() != nullptr);
    GOOGLESQL_RET_CHECK(select_column_state->resolved_select_column.IsInitialized());

    if (SelectFormAllowsLateralColumnReferences(
            query_resolution_info->select_form()) &&
        select_column_state->lateral_reference_state
            ->is_referenced_laterally()) {
      GOOGLESQL_RET_CHECK(
          language().LanguageFeatureEnabled(FEATURE_LATERAL_COLUMN_REFERENCES));
      GOOGLESQL_RET_CHECK(!IsInternalAlias(select_column_state->alias));

      // Make this column available to the upcoming aggregate or post-grouping
      // expressions.
      std::unique_ptr<NameTarget> name_target;
      name_target = std::make_unique<NameTarget>(
          select_column_state->resolved_select_column,
          select_column_state->is_explicit);

      IdStringHashMapCase<NameTarget> additional_name_targets;
      additional_name_targets.emplace(select_column_state->alias,
                                      std::move(*name_target));
      std::unique_ptr<NameScope> tmp_scope;
      GOOGLESQL_RETURN_IF_ERROR(
          updated_group_by_scope->CopyNameScopeWithOverridingNameTargets(
              additional_name_targets, &tmp_scope));
      updated_group_by_scope = std::move(tmp_scope);
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveSelectAs(
    const ASTSelectAs* select_as,
    const SelectColumnStateList& select_column_state_list,
    std::unique_ptr<const ResolvedScan> input_scan,
    const NameList* input_name_list,
    std::unique_ptr<const ResolvedScan>* output_scan,
    std::shared_ptr<const NameList>* output_name_list) {
  if (select_as->is_select_as_struct()) {
    // Convert to an anonymous struct type.
    return ConvertScanToStruct(select_as, /*named_struct_type=*/nullptr,
                               std::move(input_scan), input_name_list,
                               output_scan, output_name_list);
  } else if (select_as->is_select_as_value()) {
    // For SELECT AS VALUE, we just check that the input has exactly one
    // column, and then build a new NameList with is_value_table true.
    if (input_name_list->num_columns() != 1) {
      return MakeSqlErrorAt(select_as)
             << "SELECT AS VALUE query must have exactly one column";
    }
    std::unique_ptr<NameList> name_list = std::make_unique<NameList>();
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddValueTableColumn(
        kValueColumnId, input_name_list->column(0).column(), select_as));
    GOOGLESQL_RETURN_IF_ERROR(name_list->SetIsValueTable());
    *output_name_list = std::move(name_list);
    *output_scan = std::move(input_scan);
    return absl::OkStatus();
  } else {
    GOOGLESQL_RET_CHECK(select_as->type_name() != nullptr);

    const Type* type;
    GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsType(
        select_as->type_name(), /*is_single_identifier=*/false, &type));

    if (type->IsStruct()) {
      // Convert to a named struct type.
      return ConvertScanToStruct(select_as->type_name(), type->AsStruct(),
                                 std::move(input_scan), input_name_list,
                                 output_scan, output_name_list);
    } else if (type->IsProto()) {
      return ConvertScanToProto(select_as->type_name(),
                                select_column_state_list, type->AsProto(),
                                std::move(input_scan), input_name_list,
                                output_scan, output_name_list);
    } else if (product_mode() == PRODUCT_EXTERNAL) {
      return MakeSqlErrorAt(select_as->type_name())
             << "SELECT AS TypeName can only be used for type STRUCT";
    } else {
      return MakeSqlErrorAt(select_as->type_name())
             << "SELECT AS TypeName can only be used for STRUCT or PROTO "
                "types, but "
             << select_as->type_name()->ToIdentifierPathString() << " has type "
             << type->ShortTypeName(product_mode());
    }
  }
}

absl::Status Resolver::ConvertScanToStruct(
    const ASTNode* ast_location,
    const StructType* named_struct_type,  // May be NULL.
    std::unique_ptr<const ResolvedScan> input_scan,
    const NameList* input_name_list,
    std::unique_ptr<const ResolvedScan>* output_scan,
    std::shared_ptr<const NameList>* output_name_list) {
  if (named_struct_type != nullptr) {
    // TODO Implement named struct construction - match fields
    // by name and verify types, with coercion.
    return MakeSqlErrorAt(ast_location)
           << "Constructing named STRUCT types in subqueries not implemented "
              "yet";
  }

  std::unique_ptr<ResolvedComputedColumn> computed_column;
  const CorrelatedColumnsSetList correlated_columns_set_list;
  GOOGLESQL_RETURN_IF_ERROR(CreateStructFromNameList(
      input_name_list, correlated_columns_set_list, &computed_column));

  const ResolvedColumn& struct_column = computed_column->column();
  NameList* mutable_name_list;
  output_name_list->reset((mutable_name_list = new NameList));
  // is_explicit=false because the created column is always anonymous.
  GOOGLESQL_RET_CHECK(IsInternalAlias(struct_column.name()));
  GOOGLESQL_RETURN_IF_ERROR(mutable_name_list->AddValueTableColumn(
      struct_column.name_id(), struct_column, ast_location));
  // Make the output table a value table.
  GOOGLESQL_RETURN_IF_ERROR(mutable_name_list->SetIsValueTable());

  GOOGLESQL_RET_CHECK(struct_column.type()->IsStruct());
  if (absl::c_any_of(struct_column.type()->AsStruct()->fields(),
                     [](const StructType::StructField& field) {
                       return field.type->IsMeasureType();
                     })) {
    return MakeSqlErrorAt(ast_location)
           << "SELECT AS STRUCT cannot contain MEASURE";
  }
  if (absl::c_any_of(struct_column.type()->AsStruct()->fields(),
                     [](const StructType::StructField& field) {
                       return field.type->IsRow();
                     })) {
    return MakeSqlErrorAt(ast_location)
           << "SELECT AS STRUCT cannot contain ROW-typed values";
  }

  *output_scan = MakeResolvedProjectScan(
      std::vector<ResolvedColumn>{struct_column},
      MakeNodeVector(std::move(computed_column)), std::move(input_scan));
  return absl::OkStatus();
}

absl::Status Resolver::CreateStructFromNameList(
    const NameList* name_list,
    const CorrelatedColumnsSetList& correlated_column_sets,
    std::unique_ptr<ResolvedComputedColumn>* computed_column) {
  GOOGLESQL_RET_CHECK(computed_column != nullptr);
  GOOGLESQL_RET_CHECK(*computed_column == nullptr);

  std::vector<std::unique_ptr<const ResolvedExpr>> field_exprs;
  std::vector<StructType::StructField> fields;

  for (const auto& named_column : name_list->columns()) {
    // Internal aliases mean the column has no visible name, so we make a
    // struct with an anonymous field.
    fields.emplace_back(IsInternalAlias(named_column.name())
                            ? ""
                            : named_column.name().ToString(),
                        named_column.column().type());
    field_exprs.emplace_back(MakeColumnRefWithCorrelation(
        named_column.column(), correlated_column_sets));
  }
  const StructType* struct_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeStructType(fields, &struct_type));

  auto make_struct =
      MakeResolvedMakeStruct(struct_type, std::move(field_exprs));

  GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
      /*error_node=*/nullptr, make_struct.get()));
  const ResolvedColumn struct_column(AllocateColumnId(), kMakeStructId,
                                     kStructId, make_struct->annotated_type());
  *computed_column =
      MakeResolvedComputedColumn(struct_column, std::move(make_struct));
  return absl::OkStatus();
}

absl::Status Resolver::ConvertScanToProto(
    const ASTNode* ast_type_location,
    const SelectColumnStateList& select_column_state_list,
    const ProtoType* proto_type, std::unique_ptr<const ResolvedScan> input_scan,
    const NameList* input_name_list,
    std::unique_ptr<const ResolvedScan>* output_scan,
    std::shared_ptr<const NameList>* output_name_list) {
  GOOGLESQL_RET_CHECK_EQ(select_column_state_list.Size(), input_name_list->num_columns());

  std::vector<ResolvedBuildProtoArg> arguments;
  for (int i = 0; i < input_name_list->num_columns(); ++i) {
    const ASTNode* ast_column_location =
        select_column_state_list.GetSelectColumnState(i)->ast_expr;
    const NamedColumn& named_column = input_name_list->column(i);
    if (IsInternalAlias(named_column.name())) {
      return MakeSqlErrorAt(ast_column_location)
             << "Cannot construct PROTO from query result because column "
             << (i + 1) << " has no name";
    }

    std::unique_ptr<ResolvedColumnRef> expr =
        MakeColumnRef(named_column.column());
    MaybeRecordParseLocation(ast_column_location, expr.get());
    GOOGLESQL_ASSIGN_OR_RETURN(
        const google::protobuf::FieldDescriptor* field_descriptor,
        FindFieldDescriptor(proto_type->descriptor(),
                            AliasOrASTPathExpression(named_column.name()),
                            ast_column_location, i, "Column"));
    GOOGLESQL_ASSIGN_OR_RETURN(const Type* type,
                     FindProtoFieldType(field_descriptor, ast_column_location,
                                        proto_type->CatalogNamePath()));
    arguments.emplace_back(
        ast_column_location, std::move(expr), type,
        std::vector<const google::protobuf::FieldDescriptor*>{field_descriptor});
  }

  std::unique_ptr<const ResolvedExpr> resolved_build_proto_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveBuildProto(ast_type_location, proto_type,
                                    input_scan.get(), "Column", "Query",
                                    &arguments, &resolved_build_proto_expr));

  // Wrap resolved_query with a projection that creates the proto.
  const ResolvedColumn proto_column(AllocateColumnId(), kMakeProtoId, kProtoId,
                                    proto_type);

  *output_scan = MakeResolvedProjectScan(
      std::vector<ResolvedColumn>{proto_column},
      MakeNodeVector(MakeResolvedComputedColumn(
          proto_column, std::move(resolved_build_proto_expr))),
      std::move(input_scan));

  NameList* mutable_name_list;
  output_name_list->reset((mutable_name_list = new NameList));
  // is_explicit=false because the created column is always anonymous.
  GOOGLESQL_RET_CHECK(IsInternalAlias(proto_column.name()));
  GOOGLESQL_RETURN_IF_ERROR(mutable_name_list->AddValueTableColumn(
      MakeIdString(proto_column.name()), proto_column, ast_type_location));
  // Make the output table a value table.
  GOOGLESQL_RETURN_IF_ERROR(mutable_name_list->SetIsValueTable());

  return absl::OkStatus();
}

static absl::Status GetRecursiveScanEnumType(
    const ASTSetOperationMetadata& effective_metadata,
    ResolvedRecursiveScan::RecursiveSetOperationType* op_type) {
  GOOGLESQL_RET_CHECK_EQ(effective_metadata.op_type()->value(), ASTSetOperation::UNION);
  if (effective_metadata.all_or_distinct()->value() ==
      ASTSetOperation::DISTINCT) {
    *op_type = ResolvedRecursiveScan::UNION_DISTINCT;
  } else {
    *op_type = ResolvedRecursiveScan::UNION_ALL;
  }
  return absl::OkStatus();
}

static std::string FormatColumnCount(const NameList& name_list) {
  return name_list.is_value_table()
             ? std::string(" is value table with 1 column")
             : absl::StrCat(" has ", name_list.num_columns(), " column",
                            (name_list.num_columns() == 1 ? "" : "s"));
}

static std::string ColumnPropagationModeToString(
    ASTSetOperation::ColumnPropagationMode mode, bool for_by_name = false) {
  switch (mode) {
    case ASTSetOperation::FULL:
      return "FULL";
    case ASTSetOperation::LEFT:
      return "LEFT";
    case ASTSetOperation::STRICT:
      if (for_by_name) {
        return "none";
      } else {
        return "STRICT";
      }
    case ASTSetOperation::INNER:
      return "INNER";
  }
}

static ASTSetOperation::ColumnMatchMode GetColumnMatchMode(
    const ASTSetOperationMetadata& metadata) {
  if (metadata.column_match_mode() == nullptr) {
    return ASTSetOperation::BY_POSITION;
  }
  return metadata.column_match_mode()->value();
}

// Return true if this set operation has BY NAME syntax.
static bool HasByName(const ASTSetOperationMetadata& metadata) {
  switch (GetColumnMatchMode(metadata)) {
    case ASTSetOperation::CORRESPONDING:
    case ASTSetOperation::CORRESPONDING_BY:
    case ASTSetOperation::BY_POSITION:
      return false;
    case ASTSetOperation::BY_NAME:
    case ASTSetOperation::BY_NAME_ON:
      return true;
  }
}

// Return true if this set operation has a column name list.
static bool HasColumnList(const ASTSetOperationMetadata& metadata) {
  switch (GetColumnMatchMode(metadata)) {
    case ASTSetOperation::CORRESPONDING_BY:
    case ASTSetOperation::BY_NAME_ON:
      return true;
    case ASTSetOperation::CORRESPONDING:
    case ASTSetOperation::BY_NAME:
    case ASTSetOperation::BY_POSITION:
      return false;
  }
}

// Get the effective ColumnPropagationMode, applying default modes.
static ASTSetOperation::ColumnPropagationMode GetColumnPropagationMode(
    const ASTSetOperationMetadata& metadata) {
  if (metadata.column_propagation_mode() != nullptr) {
    return metadata.column_propagation_mode()->value();
  }
  // For column match mode BY_POSITION, we return STRICT, which is closest to
  // the positional behavior, but STRICT name matching won't really be used.
  ASTSetOperation::ColumnMatchMode column_match_mode =
      GetColumnMatchMode(metadata);
  if (column_match_mode == ASTSetOperation::BY_POSITION) {
    return ASTSetOperation::STRICT;
  }
  // The default for BY NAME is STRICT, and for CORRESPONDING is INNER.
  if (HasByName(metadata)) {
    return ASTSetOperation::STRICT;
  } else {
    return ASTSetOperation::INNER;
  }
}

// Convert AST MatchMode to resolved MatchMode.
static ResolvedSetOperationScan::SetOperationColumnMatchMode
GetResolvedSetOperationColumnMatchMode(
    ASTSetOperation::ColumnMatchMode column_match_mode) {
  switch (column_match_mode) {
    case ASTSetOperation::CORRESPONDING:
      return ResolvedSetOperationScan::CORRESPONDING;
    case ASTSetOperation::CORRESPONDING_BY:
      return ResolvedSetOperationScan::CORRESPONDING_BY;
    case ASTSetOperation::BY_NAME:
      return ResolvedSetOperationScan::CORRESPONDING;
    case ASTSetOperation::BY_NAME_ON:
      return ResolvedSetOperationScan::CORRESPONDING_BY;
    case ASTSetOperation::BY_POSITION:
      return ResolvedSetOperationScan::BY_POSITION;
  }
}

// Convert AST PropagationMode to resolved PropagationMode.
static ResolvedSetOperationScan::SetOperationColumnPropagationMode
GetResolvedSetOperationColumnPropagationMode(
    ASTSetOperation::ColumnPropagationMode mode) {
  switch (mode) {
    case ASTSetOperation::LEFT:
      return ResolvedSetOperationScan::LEFT;
    case ASTSetOperation::FULL:
      return ResolvedSetOperationScan::FULL;
    case ASTSetOperation::STRICT:
      return ResolvedSetOperationScan::STRICT;
    case ASTSetOperation::INNER:
      return ResolvedSetOperationScan::INNER;
  }
}

Resolver::SetOperationResolver::SetOperationResolver(
    const ASTSetOperation* set_operation, Resolver* resolver)
    : SetOperationResolver(set_operation,
                           /*lhs_name_list=*/nullptr, /*lhs_scan=*/nullptr,
                           resolver) {}

Resolver::SetOperationResolver::SetOperationResolver(
    const ASTPipeSetOperation* pipe_set_operation,
    const std::shared_ptr<const NameList>& lhs_name_list,
    std::unique_ptr<const ResolvedScan>* lhs_scan, Resolver* resolver)
    : SetOperationResolver(InputSetOperation(pipe_set_operation), lhs_name_list,
                           lhs_scan, resolver) {}

Resolver::SetOperationResolver::SetOperationResolver(
    const ASTPipeRecursiveUnion* pipe_recursive_union,
    const std::shared_ptr<const NameList>& lhs_name_list,
    std::unique_ptr<const ResolvedScan>* lhs_scan, Resolver* resolver)
    : SetOperationResolver(InputSetOperation(pipe_recursive_union),
                           lhs_name_list, lhs_scan, resolver) {
  if (pipe_recursive_union->input_subpipeline() != nullptr) {
    pipe_recursive_rhs_input_nodes_ = {
        pipe_recursive_union->input_subpipeline()};
  } else {
    ABSL_DCHECK(pipe_recursive_union->input_subquery() != nullptr);
    pipe_recursive_rhs_input_nodes_ = {pipe_recursive_union->input_subquery()};
  }
}

std::optional<Resolver::SetOperationResolver::ResolvedInputResult>
Resolver::SetOperationResolver::GetResolvedPipeInput(
    const ASTNode* ast_pipe_operator,
    const std::shared_ptr<const NameList>& lhs_name_list,
    std::unique_ptr<const ResolvedScan>* lhs_scan) {
  if (lhs_name_list == nullptr) {
    return std::nullopt;
  }
  return ResolvedInputResult{
      .node = MakeResolvedSetOperationItem(std::move(*lhs_scan),
                                           lhs_name_list->GetResolvedColumns()),
      .name_list = lhs_name_list,
      // We don't have a better location for the lhs input, so we point
      // at the whole pipe operator.
      .ast_location = ast_pipe_operator,
      .query_idx = 0,
  };
}

Resolver::SetOperationResolver::SetOperationResolver(
    InputSetOperation set_operation,
    const std::shared_ptr<const NameList>& lhs_name_list,
    std::unique_ptr<const ResolvedScan>* lhs_scan, Resolver* resolver)
    : SetOperationResolverBase(resolver->analyzer_options(), resolver->coercer_,
                               *resolver->column_factory_),
      set_operation_(set_operation),
      input_kind_(static_cast<InputKind>(set_operation.index())),
      resolved_pipe_input_(
          GetResolvedPipeInput(ast_node(), lhs_name_list, lhs_scan)),
      resolver_(resolver),
      op_type_str_(
          resolver->MakeIdString(GetSetOpTypeStr(effective_metadata()))) {
  if (resolved_pipe_input_.has_value()) {
    // Columns in set operations are matched positionally, so don't let columns
    // get pruned.
    resolver_->RecordColumnAccess(
        resolved_pipe_input_->name_list->GetResolvedColumns());
  }
}

// Static versions of GetByNameString, etc, for when we don't have
// effective_metadata() yet.  They need to have a different name.
static std::string GetByNameStringImpl(
    const ASTSetOperationMetadata& metadata) {
  if (HasByName(metadata)) {
    return "BY NAME";
  } else {
    return "CORRESPONDING";
  }
}

static std::string GetByNameOnStringImpl(
    const ASTSetOperationMetadata& metadata) {
  if (HasByName(metadata)) {
    return "BY NAME ON";
  } else {
    return "CORRESPONDING BY";
  }
}

static std::string GetByNameStringFullImpl(
    const ASTSetOperationMetadata& metadata) {
  if (HasByName(metadata)) {
    switch (GetColumnPropagationMode(metadata)) {
      case ASTSetOperation::STRICT:
        return "BY NAME";
      case ASTSetOperation::INNER:
        return "INNER BY NAME";
      case ASTSetOperation::LEFT:
        return "LEFT BY NAME";
      case ASTSetOperation::FULL:
        return "FULL BY NAME";
    }
  } else {
    switch (GetColumnPropagationMode(metadata)) {
      case ASTSetOperation::STRICT:
        return "STRICT CORRESPONDING";
      case ASTSetOperation::INNER:
        return "CORRESPONDING";
      case ASTSetOperation::LEFT:
        return "LEFT CORRESPONDING";
      case ASTSetOperation::FULL:
        return "FULL CORRESPONDING";
    }
  }
}

std::string Resolver::SetOperationResolver::GetByNameString() const {
  return GetByNameStringImpl(effective_metadata());
}

std::string Resolver::SetOperationResolver::GetByNameOnString() const {
  return GetByNameOnStringImpl(effective_metadata());
}

std::string Resolver::SetOperationResolver::GetByNameStringFull() const {
  return GetByNameStringFullImpl(effective_metadata());
}

std::string Resolver::SetOperationResolver::GetByNameByOrOn() const {
  if (HasByName(effective_metadata())) {
    return "ON";
  } else {
    return "BY";
  }
}

ASTSetOperation::ColumnMatchMode
Resolver::SetOperationResolver::ASTColumnMatchMode() const {
  return GetColumnMatchMode(effective_metadata());
}

ASTSetOperation::ColumnPropagationMode
Resolver::SetOperationResolver::ASTColumnPropagationMode() const {
  return GetColumnPropagationMode(effective_metadata());
}

// Returns the identifiers provided in the CORRESPONDING BY list of the given
// `metadata`. If `metadata` does not have a corresponding by list (is nullptr),
// returns an empty vector.
static std::vector<IdString> GetCorrespondingByIdStrings(
    const ASTSetOperationMetadata& metadata) {
  if (metadata.corresponding_by_column_list() == nullptr) {
    return {};
  }
  std::vector<IdString> identifier_names;
  const absl::Span<const ASTIdentifier* const> identifiers =
      metadata.corresponding_by_column_list()->identifiers();
  identifier_names.reserve(identifiers.size());
  for (const ASTIdentifier* const identifier : identifiers) {
    identifier_names.push_back(identifier->GetAsIdString());
  }
  return identifier_names;
}

// Returns whether `by_list_1` and `by_list_2` are the same. IdStrings are
// compared case-insensitively.
static bool IsSameByList(const std::vector<IdString>& by_list_1,
                         const std::vector<IdString>& by_list_2) {
  return absl::c_equal(
      by_list_1, by_list_2,
      [](const IdString& a, const IdString& b) { return a.CaseEquals(b); });
}

// static
std::vector<SetOperationResolverBase::IndexedColumnNames>
Resolver::SetOperationResolver::ToIndexedColumnNamesList(
    absl::Span<const ResolvedInputResult> resolved_inputs) {
  std::vector<IndexedColumnNames> indexed_column_names_list;
  indexed_column_names_list.reserve(resolved_inputs.size());
  for (const auto& resolved_input : resolved_inputs) {
    indexed_column_names_list.push_back(
        {.query_idx = resolved_input.query_idx,
         .column_names = resolved_input.name_list->GetColumnNames()});
  }
  return indexed_column_names_list;
}

absl::StatusOr<Resolver::SetOperationResolver::FinalColumnList>
Resolver::SetOperationResolver::MatchInputsAndCalculateFinalColumns(
    std::vector<ResolvedInputResult>& resolved_inputs) {
  GOOGLESQL_RET_CHECK(!resolved_inputs.empty());
  ResolvedColumnList final_column_list;
  std::shared_ptr<const NameList> name_list_template;

  auto record_column_access = [&](const ResolvedColumn& col) -> void {
    resolver_->RecordColumnAccess(col);
  };

  // We report errors related to calculating the column supertypes at the 2nd
  // input, which is `ast_inputs(1)` for standard set operation syntax, and
  // `ast_recursive_term()` for pipe syntax.
  //
  // TODO: Because `GetSuperTypesOfSetOperation()` also verifies that column
  // types support grouping (except for UNION ALL), the error location is
  // wrong when, for example, it is the first input that contains a
  // non-groupable column.
  const ASTNode* super_type_error_location =
      IsPipeSyntax() ? ast_recursive_term() : ast_set_operation()->inputs()[1];

  GOOGLESQL_ASSIGN_OR_RETURN(ResolvedSetOperationScan::SetOperationType op_type,
                   GetSetOperationType(&effective_metadata()));

  if (ASTColumnMatchMode() == ASTSetOperation::BY_POSITION) {
    GOOGLESQL_RETURN_IF_ERROR(CheckSameColumnNumber(resolved_inputs));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::vector<InputArgumentType>> column_type_lists,
        BuildColumnTypeListsByPosition(absl::MakeSpan(resolved_inputs)));
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const Type*> super_types,
                     GetSuperTypesOfSetOperation(
                         column_type_lists, super_type_error_location,
                         op_type, /*column_identifier_in_error_string=*/
                         [&](int col_idx) -> std::string {
                           return absl::StrCat(col_idx + 1);
                         }));
    GOOGLESQL_ASSIGN_OR_RETURN(
        final_column_list,
        BuildFinalColumnList(resolved_inputs[0].name_list->GetColumnNames(),
                             super_types, op_type_str_, record_column_access));
    // Type coercion, if needed.
    for (ResolvedInputResult& resolved_input : resolved_inputs) {
      GOOGLESQL_RETURN_IF_ERROR(CreateWrapperScanWithCastsForSetOperationItem(
          final_column_list, resolved_input));
    }
    // The first subquery determines the name and explicit attribute of each
    // column, as well as whether the result is a value table.
    name_list_template = resolved_inputs.front().name_list;
  } else {
    GOOGLESQL_RETURN_IF_ERROR(CheckNoValueTable(resolved_inputs));
    std::vector<IdString> final_column_names;
    if (!HasColumnList(effective_metadata())) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          final_column_names,
          CalculateFinalColumnNamesForCorresponding(resolved_inputs));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          final_column_names,
          CalculateFinalColumnNamesForCorrespondingBy(resolved_inputs));
    }
    std::vector<IndexedColumnNames> input_queries_column_names =
        ToIndexedColumnNamesList(resolved_inputs);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<IndexMapper> index_mapper,
        BuildIndexMapping(input_queries_column_names, final_column_names));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::vector<InputArgumentType>> column_type_lists,
        BuildColumnTypeListsForCorresponding(
            /*final_column_num=*/static_cast<int>(final_column_names.size()),
            resolved_inputs, index_mapper.get()));
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const Type*> super_types,
                     GetSuperTypesOfSetOperation(
                         column_type_lists, super_type_error_location, op_type,
                         /*column_identifier_in_error_string=*/
                         [&](int col_idx) -> std::string {
                           return final_column_names[col_idx].ToString();
                         }));
    GOOGLESQL_ASSIGN_OR_RETURN(final_column_list,
                     BuildFinalColumnList(final_column_names, super_types,
                                          op_type_str_, record_column_access));
    GOOGLESQL_ASSIGN_OR_RETURN(name_list_template,
                     BuildNameListTemplateForCorresponding(
                         final_column_list, *resolved_inputs.front().name_list,
                         index_mapper.get()));
    GOOGLESQL_RETURN_IF_ERROR(AddTypeCastIfNeededForCorresponding(
        final_column_list, absl::MakeSpan(resolved_inputs),
        index_mapper.get()));
    // WARNING: After this function the output_column_list of each items in
    // `resolved_inputs` can be updated, so
    // - `output_column_list` and `name_list` do not necessarily match anymore.
    // - The mapping stored in `index_mapper` may not be valid for
    //   `output_column_list` anymore.
    GOOGLESQL_RETURN_IF_ERROR(AdjustAndReorderColumns(
        final_column_list, index_mapper.get(), resolved_inputs));
  }

  return FinalColumnList{
      .column_list = std::move(final_column_list),
      .name_list_template = std::move(name_list_template),
  };
}

absl::Status Resolver::SetOperationResolver::Resolve(
    const NameScope* scope, const Type* inferred_type_for_query,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  GOOGLESQL_RET_CHECK(input_kind_ != InputKind::kPipeRecursive)
      << "Pipe recursive union should be resolved by ResolveRecursive() rather "
         "than Resolve()";
  GOOGLESQL_RETURN_IF_ERROR(ValidateHint());
  GOOGLESQL_RETURN_IF_ERROR(ValidateCorresponding());
  GOOGLESQL_RETURN_IF_ERROR(ValidateIdenticalSetOperator());

  GOOGLESQL_ASSIGN_OR_RETURN(ResolvedSetOperationScan::SetOperationType op_type,
                   GetSetOperationType(&effective_metadata()));

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<ResolvedInputResult> resolved_inputs,
                   GetResolvedInputs(scope, inferred_type_for_query));
  GOOGLESQL_RET_CHECK_GE(resolved_inputs.size(), 2);

  GOOGLESQL_ASSIGN_OR_RETURN(FinalColumnList result_columns,
                   MatchInputsAndCalculateFinalColumns(resolved_inputs));
  ResolvedColumnList final_column_list = std::move(result_columns.column_list);
  std::shared_ptr<const NameList> name_list_template =
      std::move(result_columns.name_list_template);

  ResolvedSetOperationScanBuilder set_op_scan_builder =
      ResolvedSetOperationScanBuilder()
          .set_column_list(final_column_list)
          .set_op_type(op_type)
          .set_input_item_list(
              ExtractSetOperationItems(absl::MakeSpan(resolved_inputs)))
          .set_column_match_mode(
              GetResolvedSetOperationColumnMatchMode(ASTColumnMatchMode()))
          .set_column_propagation_mode(
              GetResolvedSetOperationColumnPropagationMode(
                  ASTColumnPropagationMode()));
  // Resolve the ResolvedOption (Query Hint), if present.
  if (effective_metadata().hint() != nullptr) {
    std::vector<std::unique_ptr<const ResolvedOption>> hint_list;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveHintAndAppend(effective_metadata().hint(),
                                                    &hint_list));
    set_op_scan_builder.set_hint_list(std::move(hint_list));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedSetOperationScan> set_op_scan,
                   std::move(set_op_scan_builder).Build());

  GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
      ast_node(), const_cast<ResolvedSetOperationScan*>(set_op_scan.get())));

  GOOGLESQL_ASSIGN_OR_RETURN(
      *output_name_list,
      BuildFinalNameList(*name_list_template, set_op_scan->column_list()));

  GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
      *output_name_list, ast_node(),
      ResolvedSetOperationScanEnums::SetOperationType_Name(op_type),
      resolver_->product_mode()));

  *output = std::move(set_op_scan);
  return absl::OkStatus();
}

absl::Status Resolver::SetOperationResolver::CheckSameColumnNumber(
    const std::vector<ResolvedInputResult>& resolved_inputs) const {
  const NameList& first_name_list = *resolved_inputs.front().name_list;
  for (int idx = 1; idx < resolved_inputs.size(); ++idx) {
    const NameList& curr_name_list = *resolved_inputs.at(idx).name_list;
    if (curr_name_list.num_columns() != first_name_list.num_columns()) {
      return MakeSqlErrorAt(resolved_inputs[idx].ast_location)
             << "Queries in " << effective_metadata().GetSQLForOperation()
             << " have mismatched column count; " << GetFirstQueryLabel()
             << FormatColumnCount(first_name_list) << ", " << GetQueryLabel(idx)
             << FormatColumnCount(curr_name_list);
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::SetOperationResolver::CheckNoValueTable(
    absl::Span<const ResolvedInputResult> resolved_inputs) const {
  const ASTNode* op_type = effective_metadata().op_type();
  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    if (resolved_input.name_list->is_value_table()) {
      return MakeSqlErrorAtLocalNode(op_type)
             << "Value table type not allowed in set operations when "
             << GetByNameString()
             << " is used: " << GetQueryLabel(resolved_input.query_idx);
    }
  }
  return absl::OkStatus();
}

// `named_columns` should not contain any duplicate names.
static absl::StatusOr<
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>>
ToColumnNameSet(absl::Span<const NamedColumn> named_columns) {
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      column_names;
  for (const NamedColumn& named_column : named_columns) {
    GOOGLESQL_RET_CHECK(column_names.insert(named_column.name()).second);
  }
  return column_names;
}

// Similar to `ToColumnNameSet` but allows the input `named_columns` to have
// duplicate names.
static absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
ToColumnNameSetAllowDuplicates(absl::Span<const NamedColumn> named_columns) {
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      column_names;
  for (const NamedColumn& named_column : named_columns) {
    column_names.insert(named_column.name());
  }
  return column_names;
}

static IdStringHashSetCase ToColumnNameSetAllowDuplicates(
    const absl::Span<const ASTIdentifier* const> identifiers) {
  IdStringHashSetCase column_names;
  for (const ASTIdentifier* const identifier : identifiers) {
    column_names.insert(identifier->GetAsIdString());
  }
  return column_names;
}

static std::string ColumnNamesToString(
    const std::vector<IdString>& column_names) {
  return absl::StrCat(
      "[",
      absl::StrJoin(column_names, ", ",
                    [](std::string* out, const IdString& column_name) {
                      absl::StrAppend(out, ToIdentifierLiteral(column_name));
                    }),
      "]");
}

absl::StatusOr<std::vector<IdString>>
Resolver::SetOperationResolver::CalculateFinalColumnNamesForCorresponding(
    absl::Span<const ResolvedInputResult> resolved_inputs) const {
  // Validation needed for CORRESPONDING regardless of column_propagation_mode.
  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
        column_names;
    const NameList* name_list = resolved_input.name_list.get();
    const std::vector<NamedColumn>& named_column_list = name_list->columns();
    for (int column_idx = 0; column_idx < named_column_list.size();
         ++column_idx) {
      const NamedColumn& column = named_column_list[column_idx];
      if (IsInternalAlias(column.name())) {
        return MakeSqlErrorAtLocalNode(resolved_input.ast_location)
               << "Anonymous columns are not allowed in set operations when "
               << GetByNameString()
               << " is used: " << GetQueryLabel(resolved_input.query_idx)
               << ", column " << (column_idx + 1);
      }
      if (!column_names.insert(column.name()).second) {
        return MakeSqlErrorAtLocalNode(resolved_input.ast_location)
               << "Duplicate columns found when using " << GetByNameString()
               << " in set operations: " << column.name().ToString() << " in "
               << GetQueryLabel(resolved_input.query_idx);
      }
    }
  }

  std::vector<IndexedColumnNames> input_queries_column_names =
      ToIndexedColumnNamesList(resolved_inputs);

  // Calculates the final column list based on column_propagation_mode.
  switch (ASTColumnPropagationMode()) {
    case ASTSetOperation::INNER: {
      return CalculateFinalColumnNamesForInnerCorresponding(
          absl::MakeSpan(input_queries_column_names),
          /*column_intersection_empty_error=*/[&]() {
            // Report error at the set operation.
            const ASTNode* op_type = effective_metadata().op_type();
            return MakeSqlErrorAtLocalNode(op_type)
                   << "Queries of the set operation using " << GetByNameString()
                   << " do not have any columns in common";
          });
    }
    case ASTSetOperation::LEFT: {
      return CalculateFinalColumnNamesForLeftCorresponding(
          absl::MakeSpan(input_queries_column_names),
          /*no_common_columns_error=*/[&](int query_idx) {
            const ASTNode* op_type = effective_metadata().op_type();
            return MakeSqlErrorAtLocalNode(op_type)
                   << GetQueryLabel(query_idx,
                                    /*capitalize_first_char=*/true)
                   << " of the set operation with LEFT mode does not share "
                   << "any common columns with " << GetFirstQueryLabel();
          });
    }
    case ASTSetOperation::FULL: {
      return CalculateFinalColumnNamesForFullCorresponding(
          absl::MakeSpan(input_queries_column_names));
    }
    case ASTSetOperation::STRICT: {
      return CalculateFinalColumnNamesForStrictCorresponding(
          absl::MakeSpan(input_queries_column_names),
          /*input_column_mismatch_error=*/
          [&](int query_idx,
              const std::vector<IdString>& expected_query_column_names,
              const std::vector<IdString>& actual_query_column_names) {
            const ASTNode* op_type = effective_metadata().op_type();
            return MakeSqlErrorAtLocalNode(op_type)
                   << GetByNameStringFull()
                   << " requires all input queries to have "
                      "identical column names, but "
                   << GetFirstQueryLabel() << " has "
                   << ColumnNamesToString(expected_query_column_names)
                   << " and " << GetQueryLabel(query_idx) << " has "
                   << ColumnNamesToString(actual_query_column_names);
          });
    }
  }
}

// Returns the first identifier in `identifiers` that does not appear in
// `scan_column_names`.
static const ASTIdentifier* GetFirstNonPresentIdentifier(
    const absl::Span<const ASTIdentifier* const> identifiers,
    const absl::flat_hash_set<IdString, IdStringCaseHash,
                              IdStringCaseEqualFunc>& scan_column_names) {
  for (const ASTIdentifier* identifier : identifiers) {
    if (!scan_column_names.contains(identifier->GetAsIdString())) {
      return identifier;
    }
  }
  return nullptr;
}

IdStringHashSetCase Resolver::SetOperationResolver::GetAllColumnNames(
    const std::vector<ResolvedInputResult>& resolved_inputs) const {
  IdStringHashSetCase all_column_names;
  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    std::vector<IdString> scan_column_names =
        resolved_input.name_list->GetColumnNames();
    all_column_names.insert(scan_column_names.begin(), scan_column_names.end());
  }
  return all_column_names;
}

absl::StatusOr<std::vector<IdString>>
Resolver::SetOperationResolver::CalculateFinalColumnNamesForCorrespondingBy(
    const std::vector<ResolvedInputResult>& resolved_inputs) const {
  const absl::Span<const ASTIdentifier* const> by_list =
      effective_metadata().corresponding_by_column_list()->identifiers();
  // Enforced by the grammar that the by list must not be empty.
  // TODO: Replace the GOOGLESQL_RET_CHECK error with a SQL error once the
  // grammar allows empty CORRESPONDING BY list.
  GOOGLESQL_RET_CHECK(!by_list.empty());
  // No duplicate identifiers in `by_list`.
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      by_list_identifier_set;
  for (const ASTIdentifier* identifier : by_list) {
    if (!by_list_identifier_set.insert(identifier->GetAsIdString()).second) {
      return MakeSqlErrorAtLocalNode(identifier)
             << "Duplicate identifiers are not allowed in the "
             << GetByNameOnString()
             << " list: " << identifier->GetAsIdString().ToStringView();
    }
  }
  // No scans have multiple columns with the name.
  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
        in_by_list_scan_columns;
    for (const NamedColumn& column : resolved_input.name_list->columns()) {
      if (!by_list_identifier_set.contains(column.name())) {
        // It is ok for the columns not in the by_list to have duplicates.
        continue;
      }
      if (!in_by_list_scan_columns.insert(column.name()).second) {
        return MakeSqlErrorAtLocalNode(resolved_input.ast_location)
               << "The column name " << column.name().ToStringView()
               << " from the " << GetByNameOnString()
               << " list appears multiple times in "
               << GetQueryLabel(resolved_input.query_idx);
      }
    }
  }
  // Outer-mode specific checks.
  switch (ASTColumnPropagationMode()) {
    case ASTSetOperation::INNER: {
      // All identifiers in by_list must appear in every input query.
      for (const ResolvedInputResult& resolved_input : resolved_inputs) {
        absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
            scan_column_names = ToColumnNameSetAllowDuplicates(
                resolved_input.name_list->columns());
        const ASTIdentifier* non_present_identifier =
            GetFirstNonPresentIdentifier(by_list, scan_column_names);
        if (non_present_identifier != nullptr) {
          return MakeSqlErrorAtLocalNode(non_present_identifier)
                 << "The column name "
                 << non_present_identifier->GetAsIdString() << " from the "
                 << GetByNameOnString() << " list does not appear in "
                 << GetQueryLabel(resolved_input.query_idx)
                 << ". All columns in the " << GetByNameByOrOn()
                 << " list must appear in each input "
                 << "query unless FULL " << GetByNameString() << " or LEFT "
                 << GetByNameString() << " is specified";
        }
      }
      break;
    }
    case ASTSetOperation::FULL: {
      // Each identifier in by_list must appear in at least one of the input
      // queries.
      IdStringHashSetCase all_column_names = GetAllColumnNames(resolved_inputs);
      for (const ASTIdentifier* identifier : by_list) {
        if (!all_column_names.contains(identifier->GetAsIdString())) {
          return MakeSqlErrorAtLocalNode(identifier)
                 << "The column name " << identifier->GetAsIdString()
                 << " from the " << GetByNameOnString()
                 << " list does not appear in any input queries.";
        }
      }
      break;
    }
    case ASTSetOperation::LEFT: {
      // All identifiers in by_list must appear in the first input query.
      IdStringHashSetCase first_scan_column_names =
          ToColumnNameSetAllowDuplicates(
              resolved_inputs.front().name_list->columns());
      const ASTIdentifier* non_present_identifier =
          GetFirstNonPresentIdentifier(by_list, first_scan_column_names);
      if (non_present_identifier != nullptr) {
        return MakeSqlErrorAtLocalNode(non_present_identifier)
               << "The column name '" << non_present_identifier->GetAsIdString()
               << "' from the LEFT " << GetByNameOnString()
               << " list must appear in " << GetFirstQueryLabel();
      }
      break;
    }
    case ASTSetOperation::STRICT: {
      // All queries must share the same set of column names as the by_list.
      IdStringHashSetCase by_list_identifier_set =
          ToColumnNameSetAllowDuplicates(by_list);
      for (const ResolvedInputResult& resolved_input : resolved_inputs) {
        const IdStringHashSetCase scan_column_names =
            ToColumnNameSetAllowDuplicates(resolved_input.name_list->columns());
        if (!googlesql_base::HashSetEquality(by_list_identifier_set, scan_column_names)) {
          return MakeSqlErrorAtLocalNode(resolved_input.ast_location)
                 << GetQueryLabel(resolved_input.query_idx,
                                  /*capitalize_first_char=*/true)
                 << " must share the same set of column names as the "
                 << GetByNameByOrOn() << " list when using "
                 << GetByNameStringFull() << " " << GetByNameByOrOn();
        }
      }
      break;
    }
  }
  // All validations have passed, return the IdStrings.
  std::vector<IdString> final_column_names;
  final_column_names.reserve(by_list.size());
  for (const ASTIdentifier* identifier : by_list) {
    final_column_names.push_back(identifier->GetAsIdString());
  }
  return final_column_names;
}

absl::StatusOr<std::vector<std::vector<InputArgumentType>>>
Resolver::SetOperationResolver::BuildColumnTypeListsForCorresponding(
    int final_column_num, absl::Span<const ResolvedInputResult> resolved_inputs,
    const IndexMapper* index_mapper) const {
  std::vector<std::vector<InputArgumentType>> column_type_lists;
  column_type_lists.resize(final_column_num);

  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    const ResolvedScan* resolved_scan = resolved_input.node->scan();
    const NameList& curr_name_list = *resolved_input.name_list;
    for (int i = 0; i < final_column_num; ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::optional<int> output_column_index,
          index_mapper->GetOutputColumnIndex(resolved_input.query_idx, i));
      if (!output_column_index.has_value()) {
        // This query does not have a column corresponding to the i-th final
        // column; a NULL column will be padded which can coerce to any types.
        column_type_lists[i].push_back(InputArgumentType::UntypedNull());
        continue;
      }
      const ResolvedColumn& column =
          curr_name_list.column(*output_column_index).column();
      GOOGLESQL_ASSIGN_OR_RETURN(InputArgumentType input_argument_type,
                       GetColumnInputArgumentType(column, resolved_scan));
      column_type_lists[i].push_back(input_argument_type);
    }
  }
  return column_type_lists;
}

absl::Status
Resolver::SetOperationResolver::AddTypeCastIfNeededForCorresponding(
    const ResolvedColumnList& final_column_list,
    absl::Span<ResolvedInputResult> resolved_inputs,
    const IndexMapper* index_mapper) const {
  for (ResolvedInputResult& resolved_input : resolved_inputs) {
    ResolvedSetOperationItem* item = resolved_input.node.get();
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumnList matched_final_columns,
                     GetCorrespondingFinalColumns(
                         final_column_list, item->output_column_list(),
                         resolved_input.query_idx, *index_mapper));
    GOOGLESQL_RETURN_IF_ERROR(CreateWrapperScanWithCastsForSetOperationItem(
        matched_final_columns, resolved_input));
  }
  return absl::OkStatus();
}

absl::Status Resolver::SetOperationResolver::AdjustAndReorderColumns(
    const ResolvedColumnList& final_column_list,
    const IndexMapper* index_mapper,
    std::vector<ResolvedInputResult>& resolved_inputs) const {
  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    ResolvedColumnList new_output_column_list;
    new_output_column_list.reserve(final_column_list.size());
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> null_columns;

    // Reorder output_column_list based on the column order in
    // final_column_list, and pad NULL columns if needed.
    for (int final_column_idx = 0; final_column_idx < final_column_list.size();
         ++final_column_idx) {
      const ResolvedColumn& column = final_column_list[final_column_idx];
      GOOGLESQL_ASSIGN_OR_RETURN(std::optional<int> output_column_index,
                       index_mapper->GetOutputColumnIndex(
                           resolved_input.query_idx, final_column_idx));

      if (output_column_index.has_value()) {
        const ResolvedColumnList output_column_list =
            resolved_input.node->output_column_list();
        GOOGLESQL_RET_CHECK_GE(*output_column_index, 0);
        GOOGLESQL_RET_CHECK_LT(*output_column_index, output_column_list.size());
        new_output_column_list.push_back(
            output_column_list[*output_column_index]);
        continue;
      }

      // INNER and STRICT do not allow padding NULL columns.
      GOOGLESQL_RET_CHECK_NE(ASTColumnPropagationMode(), ASTSetOperation::INNER);
      GOOGLESQL_RET_CHECK_NE(ASTColumnPropagationMode(), ASTSetOperation::STRICT);
      // This column does not appear in this query. Prepare a computed NULL
      // column.
      std::unique_ptr<const ResolvedComputedColumn> null_column =
          MakeResolvedComputedColumn(
              ResolvedColumn(
                  resolver_->AllocateColumnId(),
                  resolver_->MakeIdString("$null_column_for_outer_set_op"),
                  resolver_->MakeIdString(column.name_id().ToStringView()),
                  // No annotations are assigned to the created NULL columns.
                  // This is consistent with the case when an input to a set
                  // operation has a literal null, e.g.
                  //
                  // SELECT COLLATE('abc', 'en-us')
                  // UNION ALL
                  // SELECT NULL
                  column.type()),
              googlesql::MakeResolvedLiteral(Value::Null(column.type())));
      resolver_->RecordColumnAccess(null_column->column());
      new_output_column_list.push_back(null_column->column());
      null_columns.push_back(std::move(null_column));
    }

    ResolvedSetOperationItem* input = resolved_input.node.get();
    if (new_output_column_list != input->scan()->column_list()) {
      // Add a ProjectScan to adjust the output columns, including cases when:
      // - only some of the expected columns are selected
      // - original columns are reordered
      // - there are padded NULL columns
      //
      // Note: `input->output_column_list()` is the same as
      // `input->scan()->column_list()` except for the "SELECT DISTINCT" edge
      // case where a ProjectScan is missing: b/36095506. For example, in the
      // following resolved ast, `output_column_list` has more columns than
      // `scan.column_list()`:
      //
      // ```
      // scan=AggregateScan(column_list=$distinct.[int32#19, int64#20])
      // output_column_list=$distinct.[int32#19, int64#20, int32#19]
      // ```
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedProjectScan> project_scan,
          ResolvedProjectScanBuilder()
              .set_column_list(new_output_column_list)
              .set_expr_list(std::move(null_columns))
              .set_input_scan(input->release_scan())
              .set_node_source(kNodeSourceResolverSetOperationCorresponding)
              .Build());
      input->set_scan(std::move(project_scan));
    }
    *input->mutable_output_column_list() = std::move(new_output_column_list);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<const NameList>>
Resolver::SetOperationResolver::BuildNameListTemplateForCorresponding(
    const ResolvedColumnList& final_column_list,
    const NameList& first_item_name_list,
    const IndexMapper* index_mapper) const {
  GOOGLESQL_RET_CHECK(!first_item_name_list.is_value_table());
  std::shared_ptr<NameList> name_list(new NameList);
  for (int i = 0; i < final_column_list.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<int> output_column_index,
                     index_mapper->GetOutputColumnIndex(/*query_idx=*/0, i));
    bool is_explicit;
    if (output_column_index.has_value()) {
      // This final column is present in the first input. Use the explicit
      // attribute of its corresponding column.
      is_explicit =
          first_item_name_list.column(*output_column_index).is_explicit();
    } else {
      // Padded NULL columns are not explicit columns.
      is_explicit = false;
    }
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(final_column_list[i].name_id(),
                                         final_column_list[i], is_explicit));
  }
  return name_list;
}

std::vector<std::unique_ptr<const ResolvedSetOperationItem>>
Resolver::SetOperationResolver::ExtractSetOperationItems(
    absl::Span<ResolvedInputResult> resolved_inputs) const {
  std::vector<std::unique_ptr<const ResolvedSetOperationItem>>
      resolved_input_set_op_items;
  resolved_input_set_op_items.reserve(resolved_inputs.size());
  for (ResolvedInputResult& resolved_input : resolved_inputs) {
    std::unique_ptr<ResolvedSetOperationItem> item =
        std::move(resolved_input.node);
    resolved_input_set_op_items.push_back(std::move(item));
  }
  return resolved_input_set_op_items;
}

absl::StatusOr<std::vector<InputArgumentType>>
Resolver::SetOperationResolver::BuildColumnTypeList(
    const ResolvedInputResult& resolved_input) const {
  std::vector<InputArgumentType> column_types;
  column_types.reserve(resolved_input.name_list->num_columns());
  for (const ResolvedColumn& column :
       resolved_input.name_list->GetResolvedColumns()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        InputArgumentType input_argument_type,
        GetColumnInputArgumentType(column, resolved_input.node->scan()));
    column_types.push_back(input_argument_type);
  }
  return column_types;
}

Resolver::ValidateRecursiveTermVisitor::ValidateRecursiveTermVisitor(
    const Resolver* resolver, IdString recursive_query_name)
    : resolver_(resolver), recursive_query_name_(recursive_query_name) {}

absl::Status Resolver::ValidateRecursiveTermVisitor::DefaultVisit(
    const ResolvedNode* node) {
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  return absl::OkStatus();
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedAggregateScan(
    const ResolvedAggregateScan* node) {
  ++aggregate_scan_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --aggregate_scan_count_;
  return absl::OkStatus();
}

absl::Status
Resolver::ValidateRecursiveTermVisitor::VisitResolvedFunctionArgument(
    const ResolvedFunctionArgument* node) {
  ++tvf_argument_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --tvf_argument_count_;
  return absl::OkStatus();
}

absl::Status
Resolver::ValidateRecursiveTermVisitor::VisitResolvedLimitOffsetScan(
    const ResolvedLimitOffsetScan* node) {
  ++limit_offset_scan_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --limit_offset_scan_count_;
  return absl::OkStatus();
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedWithEntry(
    const ResolvedWithEntry* node) {
  ++nested_with_entry_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --nested_with_entry_count_;
  return absl::OkStatus();
}

absl::Status
Resolver::ValidateRecursiveTermVisitor::VisitResolvedSetOperationScan(
    const ResolvedSetOperationScan* node) {
  switch (node->op_type()) {
    case ResolvedSetOperationScan::EXCEPT_ALL:
      if (node->input_item_list_size() == 0) {
        return absl::OkStatus();
      }
      GOOGLESQL_RETURN_IF_ERROR(node->input_item_list(0)->Accept(this));
      ++except_clause_count_;
      for (int i = 1; i < node->input_item_list_size(); ++i) {
        GOOGLESQL_RETURN_IF_ERROR(node->input_item_list(i)->Accept(this));
      }
      --except_clause_count_;
      return absl::OkStatus();
    case ResolvedSetOperationScan::EXCEPT_DISTINCT:
    case ResolvedSetOperationScan::INTERSECT_DISTINCT:
    case ResolvedSetOperationScan::UNION_DISTINCT:
      ++setop_distinct_count_;
      GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
      --setop_distinct_count_;
      return absl::OkStatus();
    default:
      return node->ChildrenAccept(this);
  }
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedAnalyticScan(
    const ResolvedAnalyticScan* node) {
  ++analytic_scan_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --analytic_scan_count_;
  return absl::OkStatus();
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedSampleScan(
    const ResolvedSampleScan* node) {
  ++sample_scan_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --sample_scan_count_;
  return absl::OkStatus();
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedOrderByScan(
    const ResolvedOrderByScan* node) {
  ++order_by_scan_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --order_by_scan_count_;
  return absl::OkStatus();
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedSubqueryExpr(
    const ResolvedSubqueryExpr* node) {
  ++subquery_expr_count_;
  GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
  --subquery_expr_count_;
  return absl::OkStatus();
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedRecursiveScan(
    const ResolvedRecursiveScan* node) {
  // If we have an inner recursive query, process only the query's non-recursive
  // term. As recursive queries are validated in a bottom-up fashon, the inner
  // query's recursive term has already been validated, and part of the
  // inner query's validation prevents its recursive term from referencing the
  // current (outer) query.
  GOOGLESQL_RETURN_IF_ERROR(node->non_recursive_term()->Accept(this));
  return absl::OkStatus();
}

int* Resolver::ValidateRecursiveTermVisitor::GetJoinCountField(
    const ResolvedJoinScan::JoinType join_type, bool left_operand) {
  switch (join_type) {
    case ResolvedJoinScan::LEFT:
      return left_operand ? nullptr : &right_operand_of_left_join_count_;
    case ResolvedJoinScan::RIGHT:
      return left_operand ? &left_operand_of_right_join_count_ : nullptr;
    case ResolvedJoinScan::FULL:
      return &full_join_operand_count_;
    case ResolvedJoinScan::INNER:
      return nullptr;
  }
}

void Resolver::ValidateRecursiveTermVisitor::MaybeAdjustJoinCount(
    const ResolvedJoinScan::JoinType join_type, bool left_operand, int offset) {
  int* field = GetJoinCountField(join_type, left_operand);
  if (field != nullptr) {
    (*field) += offset;
  }
}

absl::Status Resolver::ValidateRecursiveTermVisitor::VisitResolvedJoinScan(
    const ResolvedJoinScan* node) {
  // Process left operand
  MaybeAdjustJoinCount(node->join_type(), /*left_operand=*/true, 1);
  GOOGLESQL_RETURN_IF_ERROR(node->left_scan()->Accept(this));
  MaybeAdjustJoinCount(node->join_type(), /*left_operand=*/true, -1);

  // Process right operand
  MaybeAdjustJoinCount(node->join_type(), /*left_operand=*/false, 1);
  GOOGLESQL_RETURN_IF_ERROR(node->right_scan()->Accept(this));
  MaybeAdjustJoinCount(node->join_type(), /*left_operand=*/false, -1);

  // Process ON expression
  if (node->join_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(node->join_expr()->Accept(this));
  }

  return absl::OkStatus();
}

absl::Status
Resolver::ValidateRecursiveTermVisitor::VisitResolvedRecursiveRefScan(
    const ResolvedRecursiveRefScan* node) {
  auto it = resolver_->recursive_ref_info_.find(node);
  GOOGLESQL_RET_CHECK(it != resolver_->recursive_ref_info_.end());
  const RecursiveRefScanInfo& info = it->second;
  std::string query_type =
      (recursive_query_name_.ToStringView() == "$view") ? "View" : "Table";
  bool relaxed = resolver_->language().LanguageFeatureEnabled(
      FEATURE_RELAXED_WITH_RECURSIVE);

  // Check the name matching first because `seen_recursive_reference_` does not
  // know whether it is the same recursive alias or a different one.
  //
  // Example:
  //
  // SELECT 1
  // |> RECURSIVE UNION ALL (
  //   SELECT 1
  //   |> RECURSIVE UNION ALL (
  //     SELECT 1
  //     |> UNION ALL (FROM inner_alias)
  //     |> UNION ALL (FROM outer_alias)
  //   ) AS inner_alias
  // ) AS outer_alias
  //
  // should error because it references `outer_alias`, not that `inner_alias` is
  // referenced twice.
  if (!info.recursive_query_unique_name.Equals(recursive_query_name_)) {
    return MakeSqlErrorAt(info.ast_location)
           << query_type << " '" << IdentifierPathToString(info.path)
           << "' must not be recursively referenced from inside an "
              "inner recursive query";
  }

  if (seen_recursive_reference_) {
    return MakeSqlErrorAt(info.ast_location)
           << "Multiple recursive references to " << FirstCharLower(query_type)
           << " '" << IdentifierPathToString(info.path) << "' are not allowed";
  }
  seen_recursive_reference_ = true;

  if (nested_with_entry_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << query_type << " '" << IdentifierPathToString(info.path)
           << "' must not be recursively referenced from inside an "
              "inner recursive query";
  }

  if (subquery_expr_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A recursive reference from inside an expression subquery is not "
              "allowed";
  }

  if (aggregate_scan_count_ > 0 && !relaxed) {
    return MakeSqlErrorAt(info.ast_location)
           << "A subquery containing a recursive reference must not use "
              "DISTINCT, GROUP BY, or any aggregate function";
  }

  if (setop_distinct_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A subquery containing a recursive reference must not use "
              "INTERSECT, UNION, or EXCEPT with the DISTINCT modifier";
  }

  if (analytic_scan_count_ > 0 && !relaxed) {
    return MakeSqlErrorAt(info.ast_location)
           << "A subquery containing a recursive reference must not use an "
              "analytic function";
  }

  if (sample_scan_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A subquery containing a recursive reference must not use the "
              "TABLESAMPLE operator";
  }

  if (order_by_scan_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A subquery containing a recursive reference must not contain an "
              "ORDER BY clause";
  }

  if (limit_offset_scan_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A query containing a recursive reference must not use a LIMIT "
              "clause";
  }

  if (right_operand_of_left_join_count_ > 0 && !relaxed) {
    return MakeSqlErrorAt(info.ast_location)
           << "A query containing a recursive reference must not be used as "
              "the right operand of a LEFT JOIN";
  }

  if (left_operand_of_right_join_count_ > 0 && !relaxed) {
    return MakeSqlErrorAt(info.ast_location)
           << "A query containing a recursive reference must not be used as "
              "the left operand of a RIGHT JOIN";
  }

  if (full_join_operand_count_ > 0 && !relaxed) {
    return MakeSqlErrorAt(info.ast_location)
           << "A query containing a recursive reference must not be used as an "
              "operand of a FULL OUTER JOIN";
  }

  if (tvf_argument_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A query containing a recursive reference must not be used as an "
              "argument to a table-valued function";
  }

  if (except_clause_count_ > 0) {
    return MakeSqlErrorAt(info.ast_location)
           << "A subquery containing a recursive reference must not be used as "
              "the right operand of EXCEPT";
  }

  return absl::OkStatus();
}

static bool IsBasicCorrespondingEnabled(
    const LanguageOptions& language_options) {
  return language_options.LanguageFeatureEnabled(FEATURE_CORRESPONDING) ||
         language_options.LanguageFeatureEnabled(FEATURE_CORRESPONDING_FULL);
}

static absl::Status ValidateMatchByNameMetadataForRecursive(
    const ASTSetOperationMetadata* metadata) {
  auto column_match_mode = GetColumnMatchMode(*metadata);
  auto column_propagation_mode = GetColumnPropagationMode(*metadata);
  switch (column_match_mode) {
    case ASTSetOperation::BY_POSITION:
      return absl::OkStatus();
    case ASTSetOperation::BY_NAME:
    case ASTSetOperation::CORRESPONDING:
      if (column_propagation_mode == ASTSetOperation::STRICT) {
        return absl::OkStatus();
      }
      break;
    case ASTSetOperation::CORRESPONDING_BY:
    case ASTSetOperation::BY_NAME_ON:
      break;
  }
  // Only suggest the form that the user uses in the query. For example, if the
  // user uses INNER BY NAME, suggest "BY NAME". If the user uses CORRESPONDING,
  // suggest "STRICT CORRESPONDING".
  if (HasByName(*metadata)) {
    return MakeSqlErrorAt(metadata)
           << "Only BY NAME is supported in recursive queries";
  }
  return MakeSqlErrorAt(metadata)
         << "Only STRICT CORRESPONDING is supported in recursive queries";
}

absl::Status Resolver::SetOperationResolver::ValidateMatchByNameForRecursive()
    const {
  if (IsPipeSyntax()) {
    return ValidateMatchByNameMetadataForRecursive(&effective_metadata());
  }
  for (const auto* metadata :
       ast_set_operation()->metadata()->set_operation_metadata_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMatchByNameMetadataForRecursive(metadata));
  }
  return absl::OkStatus();
}

absl::StatusOr<Resolver::SetOperationResolver::NonRecursiveTerm>
Resolver::SetOperationResolver::ResolveNonRecursiveTerm(
    const NameScope* scope,
    ResolvedSetOperationScan::SetOperationType op_type) {
  std::vector<ResolvedInputResult> nonrecursive_resolved_inputs;
  if (IsPipeSyntax()) {
    // The input is a WITH RECURSIVE using pipe UNION or is a pipe recursive
    // union. The non-recursive term is the pipe input table.
    nonrecursive_resolved_inputs.push_back(std::move(*resolved_pipe_input_));
  } else {
    // This WITH RECURSIVE uses standard UNION.

    // The standard requires that any recursion be confined to the rhs of the
    // UNION. However, the GoogleSQL parser collapses chains of UNION's like:
    //   SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 ...
    // into a single ASTSetOperation node, which is left-associative. When this
    // happens, we will treat the UNION of all terms except the last as the
    // overall non-recursive term, and the last term of the UNION as the overall
    // recursive term. To override and include an inner UNION within the
    // recursive term, the user will need to provide explicit parentheses.
    int num_nonrecursive_inputs =
        static_cast<int>(ast_set_operation()->inputs().size() - 1);
    for (int idx = 0; idx < num_nonrecursive_inputs; ++idx) {
      GOOGLESQL_ASSIGN_OR_RETURN(nonrecursive_resolved_inputs.emplace_back(),
                       ResolveInputQuery(scope, idx,
                                         /*inferred_type_for_query=*/nullptr));
    }

    GOOGLESQL_RET_CHECK_EQ(ast_set_operation()->inputs().size() - 1,
                 nonrecursive_resolved_inputs.size());
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      FinalColumnList result_columns,
      MatchInputsAndCalculateFinalColumns(nonrecursive_resolved_inputs));
  ResolvedColumnList column_list = std::move(result_columns.column_list);

  // Type coercion, if needed.
  for (ResolvedInputResult& resolved_input : nonrecursive_resolved_inputs) {
    GOOGLESQL_RETURN_IF_ERROR(CreateWrapperScanWithCastsForSetOperationItem(
        column_list, resolved_input));
  }
  return NonRecursiveTerm{
      .resolved_inputs = std::move(nonrecursive_resolved_inputs),
      .final_column_list = std::move(column_list),
      .name_list_template = std::move(result_columns.name_list_template),
  };
}

absl::Status Resolver::SetOperationResolver::ResolveRecursive(
    const NameScope* scope, const std::vector<IdString>& recursive_alias,
    const IdString& recursive_query_unique_name,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  switch (input_kind_) {
    case InputKind::kStandard:
      GOOGLESQL_RET_CHECK_GE(ast_set_operation()->inputs().size(), 2);
      break;
    case InputKind::kPipe:
      GOOGLESQL_RET_CHECK(!ast_inputs().empty());
      if (ast_inputs().size() != 1) {
        return MakeSqlErrorAt(ast_inputs().at(1))
               << "The final UNION in a recursive query is the recursive term "
                  "and must have only one query argument";
      }
      break;
    case InputKind::kPipeRecursive:
      GOOGLESQL_RET_CHECK_EQ(ast_inputs().size(), 1);
      break;
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateHint());
  GOOGLESQL_RETURN_IF_ERROR(ValidateCorresponding());
  GOOGLESQL_RETURN_IF_ERROR(ValidateMatchByNameForRecursive());
  GOOGLESQL_RETURN_IF_ERROR(ValidateIdenticalSetOperator());
  ResolvedRecursiveScan::RecursiveSetOperationType recursive_op_type;
  GOOGLESQL_RETURN_IF_ERROR(
      GetRecursiveScanEnumType(effective_metadata(), &recursive_op_type));

  GOOGLESQL_ASSIGN_OR_RETURN(ResolvedSetOperationScan::SetOperationType op_type,
                   GetSetOperationType(&effective_metadata()));

  GOOGLESQL_ASSIGN_OR_RETURN(NonRecursiveTerm non_recursive_term,
                   ResolveNonRecursiveTerm(scope, op_type));

  std::vector<ResolvedInputResult>& nonrecursive_resolved_inputs =
      non_recursive_term.resolved_inputs;
  const ResolvedColumnList& column_list = non_recursive_term.final_column_list;

  std::vector<std::unique_ptr<const ResolvedSetOperationItem>>
      resolved_nonrecursive_input_set_op_items = ExtractSetOperationItems(
          absl::MakeSpan(nonrecursive_resolved_inputs));

  // Create the recursive scan with the non-recursive term and propagate
  // annotations to output columns of the recursive scan before we register the
  // subquery, so that the recursive term can be resolved with column list of
  // proper annotations.
  std::unique_ptr<ResolvedRecursiveScan> recursive_scan;
  if (nonrecursive_resolved_inputs.size() == 1) {
    recursive_scan = MakeResolvedRecursiveScan(
        column_list, recursive_op_type,
        std::move(resolved_nonrecursive_input_set_op_items.at(0)),
        /*recursive_term=*/nullptr);
  } else {
    // If we have multiple non-recursive operands, wrap them in an inner UNION
    // so that the ResolvedRecursive scan can have just one non-recursive
    // operand.
    // Clone the column list so that the columns in the inner set operation used
    // for the non-recursive term have unique ids.
    ResolvedColumnList inner_set_op_column_list;
    for (const auto& column : column_list) {
      inner_set_op_column_list.push_back(
          ResolvedColumn(resolver_->AllocateColumnId(), op_type_str_,
                         column.name_id(), column.annotated_type()));
      resolver_->RecordColumnAccess(inner_set_op_column_list.back());
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedSetOperationScan> set_op_scan,
        ResolvedSetOperationScanBuilder()
            .set_column_list(inner_set_op_column_list)
            .set_op_type(op_type)
            .set_input_item_list(
                std::move(resolved_nonrecursive_input_set_op_items))
            .set_column_match_mode(
                GetResolvedSetOperationColumnMatchMode(ASTColumnMatchMode()))
            .set_column_propagation_mode(
                GetResolvedSetOperationColumnPropagationMode(
                    ASTColumnPropagationMode()))
            .Build());
    GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
        ast_node(), const_cast<ResolvedSetOperationScan*>(set_op_scan.get())));

    // Clone column list so that the output columns have correct annotations.
    GOOGLESQL_RET_CHECK_EQ(inner_set_op_column_list.size(),
                 set_op_scan->column_list_size());
    ResolvedColumnList non_recursive_term_output_list;
    non_recursive_term_output_list.reserve(inner_set_op_column_list.size());
    for (int i = 0; i < inner_set_op_column_list.size(); ++i) {
      const auto& column = inner_set_op_column_list.at(i);
      non_recursive_term_output_list.push_back(
          ResolvedColumn(column.column_id(), op_type_str_, column.name_id(),
                         set_op_scan->column_list(i).annotated_type()));
    }
    auto non_recursive_operand = MakeResolvedSetOperationItem(
        std::move(set_op_scan), non_recursive_term_output_list);
    recursive_scan = MakeResolvedRecursiveScan(column_list, recursive_op_type,
                                               std::move(non_recursive_operand),
                                               /*recursive_term=*/nullptr);
  }
  // Propagate annotations from non-recursive term to the recursive scan.
  GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
      ast_node(), recursive_scan.get()));

  std::shared_ptr<const NameList> final_name_list;
  GOOGLESQL_ASSIGN_OR_RETURN(final_name_list,
                   BuildFinalNameList(*non_recursive_term.name_list_template,
                                      recursive_scan->column_list()));

  if (input_kind_ == InputKind::kPipeRecursive &&
      ast_pipe_recursive_union()->alias() != nullptr) {
    const IdString alias = ast_pipe_recursive_union()->alias()->GetAsIdString();
    GOOGLESQL_RET_CHECK_EQ(recursive_alias.size(), 1);
    GOOGLESQL_RET_CHECK_EQ(recursive_alias.at(0), alias);

    // The alias of pipe RECURSIVE UNION is both an input and output alias.
    // If it is provided, we should register it in the final name list so that:
    // (1) The recursive term can reference the fields of the range variable.
    // (2) The subsequent pipe operators can reference the fields of the range
    //     variable.
    auto output_name_list = std::make_shared<const NameList>();
    GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(
        ast_pipe_recursive_union(), alias, final_name_list, &output_name_list));
    final_name_list = output_name_list;
  }

  // A nullptr must have been registered for this named subquery, which is to
  // guarantee that any references to it from within the non-recursive term
  // results in an error. Now the non-recursive term has been resolved, we can
  // replace it with a real named subquery so that self-references in the
  // recursive term can resolve correctly, with column list of proper
  // annotations.
  std::unique_ptr<NamedSubquery>& named_subquery =
      resolver_->named_subquery_map_.at(recursive_alias).back();
  GOOGLESQL_RET_CHECK(named_subquery == nullptr);
  named_subquery = std::make_unique<NamedSubquery>(
      recursive_query_unique_name,
      /*recursive_in=*/true, recursive_scan->column_list(), final_name_list);

  // Resolve the recursive term.
  ResolvedInputResult resolved_recursive_input;
  if (ast_recursive_term()->Is<ASTSubpipeline>()) {
    GOOGLESQL_RET_CHECK_EQ(nonrecursive_resolved_inputs.size(), 1);
    GOOGLESQL_RET_CHECK_EQ(recursive_alias.size(), 1);
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_recursive_input,
                     ResolveRecursiveTermSubpipeline(
                         // The subpipeline is not allowed to reference any
                         // correlated columns, so we use an empty name scope.
                         /*outer_scope=*/resolver_->empty_name_scope_.get(),
                         recursive_alias.back(), recursive_query_unique_name,
                         *named_subquery));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_recursive_input,
                     ResolveInputQuery(scope, ast_recursive_term_idx(),
                                       /*inferred_type_for_query=*/nullptr));
  }

  // The recursive query is now resolved; clear the 'recursive' flag in
  // named_subquery_map_ so that future references simply resolve like an
  // ordinary WITH entry.
  resolver_->named_subquery_map_[recursive_alias].back()->is_recursive = false;

  ValidateRecursiveTermVisitor validation_visitor(resolver_,
                                                  recursive_query_unique_name);
  GOOGLESQL_RETURN_IF_ERROR(resolved_recursive_input.node->Accept(&validation_visitor));

  if (ASTColumnMatchMode() == ASTSetOperation::BY_POSITION) {
    if (resolved_recursive_input.name_list->num_columns() !=
        column_list.size()) {
      return MakeSqlErrorAt(ast_recursive_term())
             << "Queries in " << effective_metadata().GetSQLForOperation()
             << " have mismatched column count; " << GetFirstQueryLabel()
             << FormatColumnCount(
                    *nonrecursive_resolved_inputs.front().name_list)
             << ", "
             << GetQueryLabel(/*query_idx=*/static_cast<int>(
                    nonrecursive_resolved_inputs.size()))
             << FormatColumnCount(*resolved_recursive_input.name_list);
    }

    // Obtain the type of each column in the recursive term and verify that it
    // is coercible to the corresponding column type based on the non-recursive
    // terms.
    GOOGLESQL_RETURN_IF_ERROR(ValidateRecursiveTermColumnTypes(*final_name_list,
                                                     resolved_recursive_input));

    // Add casts as needed to ensure that every column produced by the recursive
    // term is the correct type (we already verified that the casts are legal
    // through implicit coercion).
    GOOGLESQL_RETURN_IF_ERROR(CreateWrapperScanWithCastsForSetOperationItem(
        column_list, resolved_recursive_input));
    recursive_scan->set_recursive_term(
        std::move(resolved_recursive_input.node));
  } else {
    // Contains the non-recursive term and the recursive term.
    std::vector<ResolvedInputResult> resolved_inputs;
    resolved_inputs.push_back(ResolvedInputResult{
        .node = absl::WrapUnique(const_cast<ResolvedSetOperationItem*>(
            recursive_scan->release_non_recursive_term().release())),
        // When there are multiple non-recursive operands, we use the query_idx
        // and ast_location of the first one to report any errors.
        .name_list = final_name_list,
        .ast_location = nonrecursive_resolved_inputs.front().ast_location,
        .query_idx = 0,
    });
    resolved_inputs.push_back(std::move(resolved_recursive_input));

    GOOGLESQL_RETURN_IF_ERROR(CheckNoValueTable(resolved_inputs));
    GOOGLESQL_RETURN_IF_ERROR(
        CalculateFinalColumnNamesForCorresponding(resolved_inputs).status());
    std::vector<IndexedColumnNames> input_queries_column_names =
        ToIndexedColumnNamesList(resolved_inputs);

    const ResolvedColumnList& final_column_list =
        final_name_list->GetResolvedColumns();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<IndexMapper> index_mapper,
                     BuildIndexMapping(input_queries_column_names,
                                       final_name_list->GetColumnNames()));
    GOOGLESQL_RETURN_IF_ERROR(ValidateRecursiveTermColumnTypes(
        *final_name_list, resolved_inputs.back(), index_mapper.get()));
    GOOGLESQL_RETURN_IF_ERROR(AddTypeCastIfNeededForCorresponding(
        final_column_list, absl::MakeSpan(resolved_inputs),
        index_mapper.get()));
    GOOGLESQL_RETURN_IF_ERROR(AdjustAndReorderColumns(
        final_column_list, index_mapper.get(), resolved_inputs));

    recursive_scan->set_non_recursive_term(
        std::move(resolved_inputs.front().node));
    recursive_scan->set_recursive_term(std::move(resolved_inputs.back().node));
  }
  // Resolve the ResolvedOption (Query Hint), if present.
  if (effective_metadata().hint() != nullptr) {
    std::vector<std::unique_ptr<const ResolvedOption>> hint_list;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveHintAndAppend(effective_metadata().hint(),
                                                    &hint_list));
    recursive_scan->set_hint_list(std::move(hint_list));
  }
  // TODO: Update `final_name_list` again after this annotation
  // propagation. Context: We should update final_column_list whenever
  // annotations are propagated because it is possible that the merged
  // annotation is becomes different. In this specific case, however, it "ok" to
  // not update (for now) because WITH RECURSIVE has the check that the
  // recursive and non-recursive terms have the same annotations; if the merged
  // annotation changes, the annotations of the recursive and non-recursive term
  // must be different and a SQL error will be thrown.
  GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
      ast_node(), recursive_scan.get()));

  // Check output columns in non-recursive term and recursive term have the same
  // annotation.
  // TODO: Support queries where output columns in non-recursive
  // term and recursive term have different collations.
  GOOGLESQL_RET_CHECK_EQ(recursive_scan->non_recursive_term()->output_column_list_size(),
               recursive_scan->recursive_term()->output_column_list_size());
  for (int i = 0;
       i < recursive_scan->non_recursive_term()->output_column_list_size();
       i++) {
    const AnnotationMap* non_recursive_column_annotation =
        recursive_scan->non_recursive_term()
            ->output_column_list(i)
            .type_annotation_map();
    const AnnotationMap* recursive_column_annotation =
        recursive_scan->recursive_term()
            ->output_column_list(i)
            .type_annotation_map();
    // TODO: Consider change the error message to be type mismatch
    // with details, e.g., "a has type STRING while b has STRING COLLATE
    // 'und:ci'"
    if (!AnnotationMap::HasEqualAnnotations(
            non_recursive_column_annotation, recursive_column_annotation,
            googlesql::CollationAnnotation::GetId())) {
      return MakeSqlErrorAt(ast_recursive_term())
             << "Collation conflict: \""
             << (non_recursive_column_annotation == nullptr
                     ? "null"
                     : non_recursive_column_annotation->DebugString())
             << "\" vs. \""
             << (recursive_column_annotation == nullptr
                     ? "null"
                     : recursive_column_annotation->DebugString())
             << "\"; in column " << (i + 1) << " of recursive scan";
    }
  }

  // Update named subquery column list.
  resolver_->named_subquery_map_[recursive_alias].back()->column_list =
      recursive_scan->column_list();
  *output = std::move(recursive_scan);
  *output_name_list = final_name_list;

  return absl::OkStatus();
}

absl::Status Resolver::SetOperationResolver::ValidateRecursiveTermColumnTypes(
    const NameList& final_name_list,
    const ResolvedInputResult& resolved_recursive_input,
    const IndexMapper* index_mapper) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<InputArgumentType> recursive_term_type_list,
                   BuildColumnTypeList(resolved_recursive_input));
  for (int i = 0; i < recursive_term_type_list.size(); ++i) {
    SignatureMatchResult result;
    std::optional<int> final_column_index = i;
    if (index_mapper != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(final_column_index,
                       index_mapper->GetFinalColumnIndex(
                           resolved_recursive_input.query_idx, i));
    }
    const NamedColumn& recursive_column =
        resolved_recursive_input.name_list->column(i);
    GOOGLESQL_RET_CHECK(final_column_index.has_value());
    if (!resolver_->coercer_.CoercesTo(
            recursive_term_type_list[i],
            final_name_list.column(*final_column_index).column().type(),
            /*is_explicit=*/false, &result)) {
      // Provide a clearer name for the non-recursive term when pipe recursive
      // union is used.
      const std::string non_recursive_term_name =
          input_kind_ == InputKind::kPipeRecursive ? "pipe input table"
                                                   : "non-recursive term";

      std::string recursive_term_column_identifier =
          index_mapper == nullptr
              ? absl::StrCat(i + 1)
              : ToIdentifierLiteral(recursive_column.name());
      std::string non_recursive_term_column_identifier =
          index_mapper == nullptr
              ? absl::StrCat(i + 1)
              : ToIdentifierLiteral(
                    final_name_list.column(*final_column_index).name());

      return MakeSqlErrorAt(ast_recursive_term())
             << "Cannot coerce type ("
             << recursive_term_type_list[i].type()->ShortTypeName(
                    resolver_->analyzer_options_.language().product_mode())
             << ") of column " << recursive_term_column_identifier
             << " in the recursive term to type ("
             << final_name_list.column(*final_column_index)
                    .column()
                    .type()
                    ->ShortTypeName(
                        resolver_->analyzer_options_.language().product_mode())
             << ") of column " << non_recursive_term_column_identifier << " in "
             << non_recursive_term_name;
    }
  }
  return absl::OkStatus();
}

std::string Resolver::SetOperationResolver::GetQueryLabel(
    int query_idx, bool capitalize_first_char) const {
  std::string label;
  switch (input_kind_) {
    case InputKind::kStandard:
      label = absl::StrCat("query ", query_idx + 1);
      break;
    case InputKind::kPipe:
      label = query_idx == 0 ? "pipe input table"
                             : absl::StrCat("pipe argument query ", query_idx);
      break;
    case InputKind::kPipeRecursive:
      if (query_idx == 0) {
        label = "pipe input table";
      } else {
        ABSL_DCHECK_EQ(query_idx, 1)
            << "Pipe recursive union should only have one input argument query";
        label = "pipe recursive term";
      }
      break;
  }
  if (capitalize_first_char) {
    label[0] = absl::ascii_toupper(label[0]);
  }
  return label;
}

absl::Status Resolver::SetOperationResolver::AddDepthColumnToRecursiveScan(
    const ASTNode* ast_location, const std::vector<IdString>& recursive_alias,
    std::unique_ptr<const ResolvedRecursionDepthModifier> depth_modifier,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  GOOGLESQL_RET_CHECK((*output)->Is<ResolvedRecursiveScan>());

  // TODO: b/370786791 - Update the error message to remove the period, and
  // change "with one of the existing names" to "with an existing column name".
  const auto& name_list = *output_name_list;
  if (name_list->is_value_table()) {
    return MakeSqlErrorAt(ast_location)
           << "WITH DEPTH modifier is not allowed when the recursive query "
              "produces a value table.";
  }

  // Handles the case when the recursion depth column alias is ambiguous.
  const ResolvedColumn& depth_column =
      depth_modifier->recursion_depth_column()->column();
  NameTarget name_target;
  if (name_list->LookupName(depth_column.name_id(), &name_target)) {
    return MakeSqlErrorAt(ast_location)
           << "WITH DEPTH modifier depth column is named "
           << ToSingleQuotedStringLiteral(depth_column.name_id().ToStringView())
           << " which collides with one of the existing names.";
  }

  // Adds the recursion depth column to output name list.
  auto name_list_with_modifier = name_list->Copy();
  GOOGLESQL_RETURN_IF_ERROR(name_list_with_modifier->AddColumn(depth_column.name_id(),
                                                     depth_column,
                                                     /*is_explicit=*/true));

  // Updates the resolved ast to add depth column to the column list.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto recursive_scan,
      ToBuilder(
          absl::WrapUnique(output->release()->GetAs<ResolvedRecursiveScan>()))
          .set_recursion_depth_modifier(std::move(depth_modifier))
          .add_column_list(depth_column)
          .Build());

  // Updates the named subquery corresponding to the recursive subquery.
  auto& named_subquery = resolver_->named_subquery_map_[recursive_alias].back();
  auto modified_named_subquery = std::make_unique<NamedSubquery>(
      named_subquery->unique_alias, named_subquery->is_recursive,
      recursive_scan->column_list(), name_list_with_modifier->Copy());
  named_subquery = std::move(modified_named_subquery);

  *output_name_list = std::move(name_list_with_modifier);
  *output = std::move(recursive_scan);
  return absl::OkStatus();
}

absl::Status
Resolver::SetOperationResolver::CreateRecursiveRefScanForSubpipeline(
    IdString alias, const NamedSubquery& named_subquery,
    std::unique_ptr<const ResolvedScan>* resolved_recursive_ref_scan,
    std::shared_ptr<const NameList>* recursive_ref_scan_name_list) {
  // The resolution should not error because the alias is already registered in
  // `named_subquery_map_`, so we report an internal error if it happens.
  GOOGLESQL_RET_CHECK_OK(resolver_->ResolveNamedSubqueryRef(
      ast_recursive_term(), {alias}, &named_subquery,
      /*hint=*/nullptr, resolved_recursive_ref_scan,
      recursive_ref_scan_name_list));
  GOOGLESQL_RET_CHECK(*resolved_recursive_ref_scan != nullptr &&
            (*resolved_recursive_ref_scan)->Is<ResolvedRecursiveRefScan>());
  GOOGLESQL_RET_CHECK(*recursive_ref_scan_name_list != nullptr);
  // The ResolvedRecursiveRefScan is not never ordered.
  GOOGLESQL_RET_CHECK(!(*resolved_recursive_ref_scan)->is_ordered());

  // If the alias is user provided, we should add it to the output name list
  // so that the subpipeline can reference it.
  //
  // The presence of the alias affects:
  // - [1] Whether the recursive input table can be referenced.
  //    - [1.1] When there is no alias, the recursive input table cannot be
  //            referenced.
  //    - [1.2] When there is an alias and the recursive term is a subpipeline,
  //            it is an error to reference that alias.
  // - [2] The output range variable.
  //    - [2.1] When the output is a value table, if the input value table has
  //            an alias, there is always one output range variable. Its name is
  //            the alias if an alias is provided, otherwise the name is the
  //            same as the input range variable.
  //    - [2.2] When the output is a normal table, there is an output range
  //            variable only if an alias is provided, and its name is the same
  //            as the alias.
  //
  // When there is no alias, we do not need the processing in
  // UpdateNameListForTableAlias() because `recursive_ref_scan_name_list` (the
  // same as the final_name_list constructed by BuildFinalNameList()):
  //
  // (1) Does not contain any range variables, so no need to drop or rename any.
  // (2) If is not a value table, it does not contain any value table columns,
  //     so no need to flatten any.
  if (!IsInternalAlias(alias)) {
    auto output_name_list = std::make_shared<const NameList>();
    GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(
        ast_pipe_recursive_union(), alias, *recursive_ref_scan_name_list,
        &output_name_list));
    *recursive_ref_scan_name_list = output_name_list;
  }
  return absl::OkStatus();
}

absl::StatusOr<Resolver::SetOperationResolver::ResolvedInputResult>
Resolver::SetOperationResolver::ResolveRecursiveTermSubpipeline(
    const NameScope* outer_scope, IdString alias,
    IdString recursive_query_unique_name, const NamedSubquery& named_subquery) {
  const ASTSubpipeline* ast_subpipeline =
      ast_recursive_term()->GetAsOrNull<ASTSubpipeline>();
  GOOGLESQL_RET_CHECK(ast_subpipeline != nullptr);

  // The input scan to the subpipeline is the recursive reference scan.
  std::unique_ptr<const ResolvedScan> resolved_recursive_ref_scan;
  std::shared_ptr<const NameList> recursive_ref_scan_name_list;
  GOOGLESQL_RETURN_IF_ERROR(CreateRecursiveRefScanForSubpipeline(
      alias, named_subquery, &resolved_recursive_ref_scan,
      &recursive_ref_scan_name_list));

  // Remember information about the recursive scan, which will be needed for
  // downstream validation checks.
  resolver_->recursive_ref_info_[resolved_recursive_ref_scan
                                     ->GetAs<ResolvedRecursiveRefScan>()] =
      RecursiveRefScanInfo{
          ast_recursive_term(),
          {alias},
          recursive_query_unique_name,
      };

  // Set the `access_error_message` of the named subquery to an error to
  // disallow referencing the recursive table alias in the subpipeline. This
  // gives a more accurate error message for cases like:
  //
  // FROM KeyValue
  // |> RECURSIVE UNION ALL (
  //   |> JOIN t USING (key)
  // ) AS t
  //
  // which would otherwise give an error "Duplicate table alias t in the same
  // FROM clause".
  resolver_->named_subquery_map_.at({alias}).back()->access_error_message =
      MakeSqlError() << "Referencing the recursive table alias inside a "
                        "RECURSIVE UNION subpipeline is not allowed";

  // The subpipeline can reference the range variables of the recursive
  // reference scan.
  std::shared_ptr<const NameList> inside_name_list =
      recursive_ref_scan_name_list->CopyWithIsValueTable();
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedSubpipeline> resolved_subpipeline,
      resolver_->ResolveSubpipeline(
          ast_subpipeline, outer_scope,
          resolved_recursive_ref_scan->column_list(),
          // The ResolvedRecursiveRefScan is never ordered.
          /*input_is_ordered=*/false, &inside_name_list,
          /*allow_terminal=*/false));

  // Clear the `access_error_message` once the subpipeline is resolved. This
  // is not strictly necessary because the NamedSubquery will be removed from
  // the map when RemoveInnermostNamedSubqueryWithAlias() is called in
  // ResolvePipeRecursiveUnion().
  resolver_->named_subquery_map_.at({alias}).back()->access_error_message =
      absl::OkStatus();

  // We use the rewriter function `RewriteSubpipelineToScan` to rewrite the
  // subpipeline to a scan, so that pipe recursive unions using subpipelines
  // come out the same as those using subqueries, as if they started with
  // `FROM recursive_table_name`.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedScan> resolved_scan,
      RewriteSubpipelineToScan(std::move(resolved_subpipeline),
                               std::move(resolved_recursive_ref_scan)));

  // Columns in set operations are matched positionally, so don't let
  // columns get pruned.
  resolver_->RecordColumnAccess(resolved_scan->column_list());

  return ResolvedInputResult{
      .node = MakeResolvedSetOperationItem(
          std::move(resolved_scan), inside_name_list->GetResolvedColumns()),
      .name_list = inside_name_list,
      .ast_location = ast_subpipeline,
      .query_idx = ast_recursive_term_idx() + 1,
  };
}

absl::StatusOr<Resolver::SetOperationResolver::ResolvedInputResult>
Resolver::SetOperationResolver::ResolveInputQuery(
    const NameScope* scope, int ast_input_index,
    const Type* inferred_type_for_query) const {
  GOOGLESQL_RET_CHECK_GE(ast_input_index, 0);
  GOOGLESQL_RET_CHECK_LT(ast_input_index, ast_inputs().size());
  const IdString query_alias = resolver_->MakeIdString(
      absl::StrCat(op_type_str_.ToStringView(), ast_input_index + 1));

  ResolvedInputResult result = {
      .ast_location = ast_inputs(ast_input_index),
  };
  std::unique_ptr<const ResolvedScan> resolved_scan;
  const ASTQueryExpression* ast_input =
      ast_inputs(ast_input_index)->GetAsOrNull<ASTQueryExpression>();
  GOOGLESQL_RET_CHECK(ast_input != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveQueryExpression(
      ast_input, scope, query_alias,
      /*force_new_columns_for_projected_outputs=*/false, &resolved_scan,
      &result.name_list, inferred_type_for_query));

  // Columns in set operations are matched positionally, so don't let
  // columns get pruned.
  resolver_->RecordColumnAccess(result.name_list->GetResolvedColumns());

  result.node = MakeResolvedSetOperationItem(
      std::move(resolved_scan), result.name_list->GetResolvedColumns());
  // Pipe-syntax set operations have one more input, which is the pipe input
  // table.
  result.query_idx = IsPipeSyntax() ? ast_input_index + 1 : ast_input_index;
  // Record the location of the set operation keyword for standard set
  // operations. Note that we skip the first input (ast_input_index == 0) so
  // that each keyword is only recorded once.
  if (input_kind_ == InputKind::kStandard && ast_input_index > 0) {
    resolver_->MaybeRecordResolvedNodeOperatorKeywordLocation(
        ast_set_operation()
            ->metadata()
            ->set_operation_metadata_list(ast_input_index - 1)
            ->location(),
        result.node.get());
  }
  return result;
}

absl::StatusOr<std::vector<std::vector<InputArgumentType>>>
Resolver::SetOperationResolver::BuildColumnTypeListsByPosition(
    absl::Span<ResolvedInputResult> resolved_inputs) const {
  std::vector<std::vector<InputArgumentType>> column_type_lists;
  column_type_lists.resize(resolved_inputs.front().name_list->num_columns());

  for (const ResolvedInputResult& resolved_input : resolved_inputs) {
    const ResolvedScan* resolved_scan = resolved_input.node->scan();
    const NameList& curr_name_list = *resolved_input.name_list;
    for (int i = 0; i < curr_name_list.num_columns(); ++i) {
      const ResolvedColumn& column = curr_name_list.column(i).column();
      GOOGLESQL_ASSIGN_OR_RETURN(InputArgumentType input_argument_type,
                       GetColumnInputArgumentType(column, resolved_scan));
      column_type_lists[i].push_back(input_argument_type);
    }
  }
  return column_type_lists;
}

absl::Status
Resolver::SetOperationResolver::CreateWrapperScanWithCastsForSetOperationItem(
    const ResolvedColumnList& column_list,
    ResolvedInputResult& resolved_input) const {
  ResolvedSetOperationItem& set_operation_item = *resolved_input.node;
  std::unique_ptr<const ResolvedScan> resolved_scan =
      set_operation_item.release_scan();

  // The supertype annotations have not computed yet. This happens at the very
  // end through the AnnotationPropagator. So we annotate with the source
  // annotations instead to propagate them, so that they're available for
  // AnnotationPropagator to compute the final output annotations.
  ResolvedColumnList annotated_column_list;
  for (int i = 0; i < column_list.size(); ++i) {
    const AnnotationMap* src_annotation_map =
        resolver_->language().LanguageFeatureEnabled(
            FEATURE_PRESERVE_ANNOTATION_IN_IMPLICIT_CAST_IN_SCAN)
            ? set_operation_item.output_column_list(i).type_annotation_map()
            : nullptr;

    const ResolvedColumn& column = column_list[i];
    annotated_column_list.emplace_back(
        column.column_id(), column.table_name_id(), column.name_id(),
        AnnotatedType(column.type(), src_annotation_map));
  }

  GOOGLESQL_RETURN_IF_ERROR(resolver_->CreateWrapperScanWithCasts(
      resolved_input.ast_location, annotated_column_list,
      resolver_->MakeIdString(absl::StrCat(
          op_type_str_.ToStringView(), resolved_input.query_idx + 1, "_cast")),
      &resolved_scan, set_operation_item.mutable_output_column_list()));

  set_operation_item.set_scan(std::move(resolved_scan));
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<const NameList>>
Resolver::SetOperationResolver::BuildFinalNameList(
    const NameList& name_list_template,
    const ResolvedColumnList& final_column_list) const {
  GOOGLESQL_RET_CHECK_EQ(name_list_template.num_columns(), final_column_list.size());
  std::shared_ptr<NameList> name_list(new NameList);
  for (int i = 0; i < final_column_list.size(); ++i) {
    const IdString name = name_list_template.column(i).name();
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumnMaybeValueTable(
        name, final_column_list.at(i),
        name_list_template.column(i).is_explicit(), ast_node(),
        name_list_template.is_value_table()));
  }

  if (name_list_template.is_value_table()) {
    GOOGLESQL_RETURN_IF_ERROR(name_list->SetIsValueTable());
  }
  return name_list;
}

absl::Status Resolver::SetOperationResolver::ValidateHint() const {
  if (IsPipeSyntax()) {
    // Pipe set operation or pipe recursive union only has one metadata, so
    // we don't need this validation.
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(ast_set_operation()->metadata() != nullptr);
  const absl::Span<const ASTSetOperationMetadata* const> metadata_list =
      ast_set_operation()->metadata()->set_operation_metadata_list();
  for (int i = 0; i < metadata_list.size(); ++i) {
    const ASTSetOperationMetadata* metadata = metadata_list[i];
    if (i > 0 && metadata->hint() != nullptr) {
      return MakeSqlErrorAt(metadata->hint())
             << "Syntax error: Hints on set operations must appear on the "
                "first  operation.";
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::SetOperationResolver::ValidateIdenticalSetOperator()
    const {
  if (IsPipeSyntax()) {
    // Pipe set operation or pipe recursive union only has one metadata, so we
    // don't need this validation.
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(ast_set_operation()->metadata() != nullptr);
  const auto& metadata_list =
      ast_set_operation()->metadata()->set_operation_metadata_list();
  GOOGLESQL_RET_CHECK(!metadata_list.empty());

  const ASTSetOperation::OperationType op_type =
      metadata_list[0]->op_type()->value();
  const ASTSetOperation::AllOrDistinct all_or_distinct =
      metadata_list[0]->all_or_distinct()->value();
  const ASTSetOperation::ColumnMatchMode column_match_mode =
      GetColumnMatchMode(*metadata_list[0]);
  const ASTSetOperation::ColumnPropagationMode column_propagation_mode =
      GetColumnPropagationMode(*metadata_list[0]);
  const std::vector<IdString> by_list =
      GetCorrespondingByIdStrings(*metadata_list[0]);

  for (int i = 1; i < metadata_list.size(); ++i) {
    const ASTSetOperationMetadata* metadata = metadata_list[i];
    if (metadata->op_type()->value() != op_type ||
        metadata->all_or_distinct()->value() != all_or_distinct) {
      // We report the error at metadata->op_type(), even if it is the distinct
      // that is different, to align with the error message from the parser for
      // `query_set_operation_prefix`.
      // TODO: Remove the "Syntax error" prefix and update all the
      // affected tests.
      return MakeSqlErrorAt(metadata->op_type())
             << "Syntax error: Different set operations cannot be used in the "
                "same query without using parentheses for grouping";
    }

    if (column_match_mode != GetColumnMatchMode(*metadata)) {
      const ASTNode* err_location = metadata->column_match_mode() == nullptr
                                        ? metadata_list[0]->column_match_mode()
                                        : metadata->column_match_mode();
      return MakeSqlErrorAt(err_location)
             << "Different set operation modifiers cannot be used in the same "
                "query without using parentheses for grouping";
    }

    if (column_propagation_mode != GetColumnPropagationMode(*metadata)) {
      const ASTNode* err_location =
          metadata->column_propagation_mode() == nullptr
              ? metadata_list[0]->column_propagation_mode()
              : metadata->column_propagation_mode();
      return MakeSqlErrorAt(err_location)
             << "Different set operation modifiers ("
             << ColumnPropagationModeToString(column_propagation_mode,
                                              HasByName(*metadata_list[0]))
             << " and "
             << ColumnPropagationModeToString(
                    GetColumnPropagationMode(*metadata), HasByName(*metadata))
             << ") cannot be used in the same query without using parentheses "
                "for grouping";
    }

    if (!IsSameByList(by_list, GetCorrespondingByIdStrings(*metadata))) {
      const ASTNode* err_location = metadata->corresponding_by_column_list();
      return MakeSqlErrorAt(err_location)
             << "Different " << GetByNameOnString()
             << " lists cannot be used in the same "
                "query without using parentheses for grouping";
    }
  }
  return absl::OkStatus();
}

static absl::Status ValidateCorrespondingInMetadata(
    const ASTSetOperationMetadata* metadata,
    const LanguageOptions& language_options) {
  // Verify that the related language features are turned on if
  // column_match_mode and column_propagation_mode are specified.
  {
    const ASTSetOperationColumnMatchMode* column_match_mode =
        metadata->column_match_mode();
    if (column_match_mode != nullptr) {
      bool basic_is_enabled = IsBasicCorrespondingEnabled(language_options);
      bool full_is_enabled =
          language_options.LanguageFeatureEnabled(FEATURE_CORRESPONDING_FULL);
      bool by_name_is_enabled =
          language_options.LanguageFeatureEnabled(FEATURE_BY_NAME);

      if (!basic_is_enabled &&
          column_match_mode->value() == ASTSetOperation::CORRESPONDING) {
        return MakeSqlErrorAt(column_match_mode)
               << GetByNameStringImpl(*metadata)
               << " for set operations is not supported";
      }
      if (!full_is_enabled &&
          column_match_mode->value() == ASTSetOperation::CORRESPONDING_BY) {
        return MakeSqlErrorAt(column_match_mode)
               << GetByNameOnStringImpl(*metadata)
               << " for set operations is not supported";
      }
      if ((!full_is_enabled || !by_name_is_enabled) &&
          column_match_mode->value() == ASTSetOperation::BY_NAME) {
        return MakeSqlErrorAt(column_match_mode)
               << GetByNameStringImpl(*metadata)
               << " for set operations is not supported";
      }
      if ((!full_is_enabled || !by_name_is_enabled) &&
          column_match_mode->value() == ASTSetOperation::BY_NAME_ON) {
        return MakeSqlErrorAt(column_match_mode)
               << GetByNameOnStringImpl(*metadata)
               << " for set operations is not supported";
      }
    }

    const ASTSetOperationColumnPropagationMode* column_propagation_mode =
        metadata->column_propagation_mode();
    if (!language_options.LanguageFeatureEnabled(FEATURE_CORRESPONDING_FULL) &&
        column_propagation_mode != nullptr) {
      return MakeSqlErrorAt(metadata->column_propagation_mode())
             << "Column propagation mode ("
             << ColumnPropagationModeToString(
                    metadata->column_propagation_mode()->value())
             << ") for set "
             << "operations is not supported";
    }
  }

  ASTSetOperation::ColumnMatchMode match_mode = GetColumnMatchMode(*metadata);
  ASTSetOperation::ColumnPropagationMode propagation_mode =
      GetColumnPropagationMode(*metadata);
  switch (match_mode) {
    case ASTSetOperation::BY_POSITION:
      // No column_propagation_mode is allowed to be specified when column
      // match mode is BY_POSITION, although we default
      // `column_propagation_mode` to STRICT in analyzer for BY_POSITION.
      if (metadata->column_propagation_mode() != nullptr) {
        return MakeSqlErrorAt(metadata->column_propagation_mode())
               << ColumnPropagationModeToString(propagation_mode)
               << " in set operations cannot be used without "
               << GetByNameStringImpl(*metadata);
      }
      break;
    case ASTSetOperation::CORRESPONDING:
    case ASTSetOperation::CORRESPONDING_BY:
    case ASTSetOperation::BY_NAME:
    case ASTSetOperation::BY_NAME_ON:
      break;
  }
  return absl::OkStatus();
}

absl::Status Resolver::SetOperationResolver::ValidateCorresponding() const {
  if (IsPipeSyntax()) {
    // Pipe set operation and pipe recursive union only has one metadata, which
    // is the effective metadata.
    return ValidateCorrespondingInMetadata(&effective_metadata(),
                                           resolver_->language());
  }
  // Standard set operations have multiple metadata. Validate them all.
  GOOGLESQL_RET_CHECK(ast_set_operation()->metadata() != nullptr);
  for (const ASTSetOperationMetadata* metadata :
       ast_set_operation()->metadata()->set_operation_metadata_list()) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateCorrespondingInMetadata(metadata, resolver_->language()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<Resolver::SetOperationResolver::ResolvedInputResult>>
Resolver::SetOperationResolver::GetResolvedInputs(
    const NameScope* scope, const Type* inferred_type_for_query) {
  std::vector<ResolvedInputResult> resolved_inputs;

  if (IsPipeSyntax()) {
    GOOGLESQL_RET_CHECK(resolved_pipe_input_.has_value());
    resolved_inputs.push_back(std::move(*resolved_pipe_input_));
    resolved_pipe_input_.reset();
  }

  for (int idx = 0; idx < ast_inputs().size(); ++idx) {
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_inputs.emplace_back(),
                     ResolveInputQuery(scope, idx, inferred_type_for_query));
  }

  return resolved_inputs;
}

// Note that we allow set operations between value tables and regular
// tables with exactly one column.  The output will be a value table if
// the first subquery was a value table.
absl::Status Resolver::ResolveSetOperation(
    const ASTSetOperation* set_operation, const NameScope* scope,
    const Type* inferred_type_for_query,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  SetOperationResolver resolver(set_operation, this);
  return resolver.Resolve(scope, inferred_type_for_query, output,
                          output_name_list);
}

absl::Status Resolver::ValidateIntegerParameterOrLiteral(
    const char* clause_name, const ASTNode* ast_location,
    const ResolvedExpr& expr) const {
  if ((expr.node_kind() != RESOLVED_PARAMETER &&
       expr.node_kind() != RESOLVED_LITERAL) ||
      !expr.type()->IsInteger()) {
    return MakeSqlErrorAt(ast_location)
           << clause_name << " expects an integer literal or parameter";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
    const char* clause_name, const ASTNode* ast_location,
    std::unique_ptr<const ResolvedExpr>* expr) const {
  if ((*expr)->type()->IsInt64() && (*expr)->node_kind() == RESOLVED_CAST) {
    // We allow CAST(<expr> AS INT64), as long as <expr> is a parameter
    // or literal.
    GOOGLESQL_RETURN_IF_ERROR(ValidateIntegerParameterOrLiteral(
        clause_name, ast_location, *(*expr)->GetAs<ResolvedCast>()->expr()));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateIntegerParameterOrLiteral(clause_name, ast_location, **expr));
  }
  // kExplicitCoercion is safe to use here because the above lines validate the
  // type. If those restrictions are relaxed we might need to do something
  // differently.
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(ast_location, type_factory_->get_int64(),
                                   kExplicitCoercion, expr));

  if ((*expr)->node_kind() == RESOLVED_LITERAL) {
    // If a literal, we can also validate its value.
    const Value& value = (*expr)->GetAs<ResolvedLiteral>()->value();
    if (value.is_null()) {
      return MakeSqlErrorAt(ast_location) << clause_name << " must not be null";
    }
    if (value.int64_value() < 0) {
      return MakeSqlErrorAt(ast_location)
             << clause_name
             << " expects a non-negative integer literal or parameter";
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveLimitOrOffsetExpr(
    const ASTExpression* ast_expr, const char* clause_name,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) {
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_expr, expr_resolution_info, resolved_expr));
  ABSL_DCHECK(resolved_expr != nullptr);

  if (!language().LanguageFeatureEnabled(FEATURE_LIMIT_OFFSET_EXPRESSIONS)) {
    return ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
        clause_name, ast_expr, resolved_expr);
  }

  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
      ast_expr, type_factory_->get_int64(), kImplicitCoercion,
      [](absl::string_view target_type_name,
         absl::string_view actual_type_name) {
        return absl::StrCat("LIMIT ... OFFSET ... expects ", target_type_name,
                            ", got ", actual_type_name);
      },
      resolved_expr));
  GOOGLESQL_ASSIGN_OR_RETURN(bool is_constant_expr,
                   IsConstantExpression(resolved_expr->get()));
  if (!is_constant_expr) {
    return MakeSqlErrorAt(ast_expr)
           << clause_name << " expression must be constant";
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveHavingMaxMinModifier(
    const ASTFunctionCall* ast_function_call,
    const ResolvedFunctionCall* resolved_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedAggregateHavingModifier>* resolved_having) {
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  const ASTHavingModifier* ast_having_modifier =
      ast_function_call->having_modifier();
  if (ast_having_modifier == nullptr) {
    return absl::OkStatus();
  }
  if (!language().LanguageFeatureEnabled(FEATURE_HAVING_IN_AGGREGATE)) {
    return MakeSqlErrorAt(ast_having_modifier)
           << "HAVING modifier in aggregate function is not supported";
  }
  if (!resolved_function_call->function()->SupportsHavingModifier()) {
    return MakeSqlErrorAt(ast_having_modifier)
           << resolved_function_call->function()->QualifiedSQLName(
                  /*capitalize_qualifier=*/true)
           << " does not support HAVING in arguments";
  }
  if (!language().LanguageFeatureEnabled(FEATURE_HAVING_IN_AGGREGATE)) {
    return MakeSqlErrorAt(ast_having_modifier)
           << "HAVING modifier in aggregate function is not supported";
  }
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_having_modifier->expr(), expr_resolution_info,
                              &resolved_expr));
  // TODO: Introduce a feature to support ARRAY type in HAVING
  // The HAVING MIN/MAX expression type must support ordering, and it cannot
  // be an array since MIN/MAX is currently undefined for arrays (even if
  // the array element supports MIN/MAX).
  if (!resolved_expr->type()->SupportsOrdering(language(),
                                               /*type_description=*/nullptr) ||
      resolved_expr->type()->IsArray()) {
    return MakeSqlErrorAt(ast_having_modifier)
           << "HAVING modifier does not support expressions of type "
           << resolved_expr->type()->ShortTypeName(product_mode());
  }

  if (language().LanguageFeatureEnabled(FEATURE_DISALLOW_GROUP_BY_FLOAT) &&
      resolved_expr->type()->IsFloatingPoint()) {
    return MakeSqlErrorAt(ast_having_modifier)
           << "HAVING modifier does not support expressions of type "
           << resolved_expr->type()->ShortTypeName(product_mode());
  }

  GOOGLESQL_RET_CHECK(resolved_having != nullptr);
  ResolvedAggregateHavingModifier::HavingModifierKind kind;
  if (ast_having_modifier->modifier_kind() ==
      ASTHavingModifier::ModifierKind::MAX) {
    kind = ResolvedAggregateHavingModifier::MAX;
  } else {
    kind = ResolvedAggregateHavingModifier::MIN;
  }
  *resolved_having =
      MakeResolvedAggregateHavingModifier(kind, std::move(resolved_expr));
  return absl::OkStatus();
}

// Resolves a LimitOffsetScan.
// If an OFFSET is not supplied, then the default value, 0, is used.
absl::Status Resolver::ResolveLimitOffsetScan(
    const ASTLimitOffset* limit_offset, const NameScope* name_scope,
    std::unique_ptr<const ResolvedScan>* scan) {
  if (limit_offset->has_limit_all() &&
      !language().LanguageFeatureEnabled(FEATURE_LIMIT_ALL)) {
    return MakeSqlErrorAt(limit_offset->limit())
           << "LIMIT ALL is not supported";
  }

  return ResolveLimitOffsetScan(limit_offset->limit_expression(),
                                limit_offset->offset(), name_scope, scan);
}

// Resolves a LimitOffsetScan.
// If an OFFSET is not supplied, then a default value of 0 is used.
// If a LIMIT is not supplied and FEATURE_LIMIT_ALL is disabled,
// then a default value of kint64max/2 is used.
absl::Status Resolver::ResolveLimitOffsetScan(
    const ASTExpression* limit, const ASTExpression* offset,
    const NameScope* name_scope, std::unique_ptr<const ResolvedScan>* scan) {
  // LIMIT and OFFSET cannot reference the current name scope. Theoretically
  // they can reference the parent scope. Practically this makes no difference
  // than passing in an empty scope because LIMIT and OFFSET can currently only
  // take constant expressions and correlated expressions are not that. We pass
  // in the parent scope here to get a better error message, and allow easier
  // migration when correlated column references are supported in the future.
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      name_scope->previous_scope(), "LIMIT OFFSET");

  // Resolve and validate the LIMIT.
  std::unique_ptr<const ResolvedExpr> limit_expr;
  if (limit == nullptr &&
      !language().LanguageFeatureEnabled(FEATURE_LIMIT_ALL)) {
    // There is no specific location that we can associate with the virtual
    // literal so we set `ast_location` to nullptr.
    limit_expr = MakeResolvedLiteral(
        /*ast_location=*/nullptr,
        Value::Int64(std::numeric_limits<int64_t>::max() / 2));
  } else if (limit != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveLimitOrOffsetExpr(
        limit,
        /*clause_name=*/"LIMIT", expr_resolution_info.get(), &limit_expr));
  }

  // Resolve and validate the OFFSET.
  std::unique_ptr<const ResolvedExpr> offset_expr;
  if (offset != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveLimitOrOffsetExpr(offset, /*clause_name=*/"OFFSET",
                                             expr_resolution_info.get(),
                                             &offset_expr));
  }

  const std::vector<ResolvedColumn>& column_list = (*scan)->column_list();
  *scan = MakeResolvedLimitOffsetScan(column_list, std::move(*scan),
                                      std::move(limit_expr),
                                      std::move(offset_expr));
  return absl::OkStatus();
}

// Tries to return the AST node corresponding to <column_index> in the
// input <ast_location>.  <num_columns> is the expected total number of
// columns.
static const ASTNode* GetASTNodeForColumn(const ASTNode* ast_location,
                                          int column_index, int num_columns) {
  if (ast_location->node_kind() == AST_QUERY) {
    ast_location = ast_location->GetAsOrDie<ASTQuery>()->query_expr();
  }
  if (ast_location->node_kind() == AST_SELECT) {
    // We can only find a specific column if the column list matches 1:1 with
    // output columns.  If there is a SELECT *, the column indexes won't match.
    // Star can never expand to zero columns so if parse node has N children
    // and we have N columns, we know they must match 1:1.
    const ASTSelectList* select_list =
        ast_location->GetAsOrDie<ASTSelect>()->select_list();
    if (select_list->columns().size() == num_columns) {
      ast_location = select_list->columns(column_index);
    }
  }
  return ast_location;
}

// TODO: If all of the columns that need coercing are
// literals then we do not need to add a wrapper scan and the literals should
// be converted in place instead.
absl::Status Resolver::CreateWrapperScanWithCasts(
    const ASTNode* ast_location, const ResolvedColumnList& target_column_list,
    IdString scan_alias, std::unique_ptr<const ResolvedScan>* scan,
    ResolvedColumnList* scan_column_list) {
  GOOGLESQL_RET_CHECK(scan != nullptr && *scan != nullptr);
  GOOGLESQL_RET_CHECK_EQ(target_column_list.size(), scan_column_list->size());

  bool needs_casts = false;
  for (int i = 0; i < target_column_list.size(); ++i) {
    if (!target_column_list[i].type()->Equals((*scan_column_list)[i].type())) {
      needs_casts = true;
      break;
    }
  }
  if (needs_casts) {
    ResolvedColumnList casted_column_list;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> casted_exprs;

    for (int i = 0; i < target_column_list.size(); ++i) {
      AnnotatedType target_annotated_type =
          target_column_list[i].annotated_type();
      const ResolvedColumn& scan_column = (*scan_column_list)[i];

      if (target_annotated_type.type->Equals(scan_column.type()) &&
          AnnotationMap::Equals(target_annotated_type.annotation_map,
                                scan_column.type_annotation_map())) {
        casted_column_list.emplace_back(scan_column);
      } else {
        // Determine the AST expression corresponding to the column. If the
        // query is not a SELECT clause (e.g., a set operation),
        // AddCastOrConvertLiteral below will not convert literals but add
        // casts. Hence, <column_ast_location> will be used for error reporting
        // only and not for recording parse location of literals.
        const ASTNode* column_ast_location = GetASTNodeForColumn(
            ast_location, i, static_cast<int>(target_column_list.size()));
        std::unique_ptr<const ResolvedExpr> casted_expr =
            MakeColumnRef(scan_column);
        Collation collation_for_cast;
        TypeParameters type_parameters;
        bool set_has_explicit_type = false;
        if (target_annotated_type.annotation_map != nullptr) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              collation_for_cast,
              Collation::MakeCollation(*target_annotated_type.annotation_map));
          // Generate type parameters from target annotation map.
          GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_column_annotations,
                           MakeResolvedColumnAnnotationsFromAnnotationMap(
                               target_annotated_type.type,
                               target_annotated_type.annotation_map,
                               /*options_list=*/nullptr));
          GOOGLESQL_ASSIGN_OR_RETURN(type_parameters,
                           resolved_column_annotations->GetFullTypeParameters(
                               target_annotated_type.type));
        }
        set_has_explicit_type = !type_parameters.IsEmpty();
        GOOGLESQL_RETURN_IF_ERROR(function_resolver_->AddCastOrConvertLiteral(
            column_ast_location, target_annotated_type, /*format=*/nullptr,
            /*time_zone=*/nullptr,
            TypeModifiers::MakeTypeModifiers(type_parameters,
                                             std::move(collation_for_cast)),
            scan->get(), set_has_explicit_type, /*return_null_on_error=*/false,
            &casted_expr));
        const ResolvedColumn casted_column(AllocateColumnId(), scan_alias,
                                           scan_column.name_id(),
                                           target_annotated_type);

        // These casted columns should not get pruned.  We wouldn't create them
        // if they weren't required for the query.
        RecordColumnAccess(casted_column);

        casted_column_list.emplace_back(casted_column);
        casted_exprs.push_back(
            MakeResolvedComputedColumn(casted_column, std::move(casted_expr)));
      }
    }

    *scan = MakeResolvedProjectScan(casted_column_list, std::move(casted_exprs),
                                    std::move(*scan));
    casted_exprs.clear();  // Avoid deletion after ownership transfer.

    GOOGLESQL_RET_CHECK_EQ(scan_column_list->size(), casted_column_list.size());
    *scan_column_list = casted_column_list;
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveFromClauseAndCreateScan(
    const ASTSelect* select, const ASTOrderBy* order_by,
    const NameScope* external_scope,
    std::unique_ptr<const ResolvedScan>* output_scan,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (select->from_clause() != nullptr) {
    GOOGLESQL_RET_CHECK(select->from_clause()->table_expression() != nullptr);
    return ResolveTableExpression(select->from_clause()->table_expression(),
                                  external_scope, external_scope,
                                  /*is_leftmost=*/true,
                                  /*on_rhs_of_right_or_full_join=*/false,
                                  output_scan, output_name_list);
  } else {
    // No-from-clause query has special rules about what else can exist.
    if (select->where_clause() != nullptr) {
      return MakeSqlErrorAt(select->where_clause())
             << "Query without FROM clause cannot have a WHERE clause";
    }
    if (select->distinct()) {
      return MakeSqlErrorAt(select)
             << "Query without FROM clause cannot use SELECT DISTINCT";
    }
    if (select->group_by() != nullptr) {
      return MakeSqlErrorAt(select->group_by())
             << "Query without FROM clause cannot have a GROUP BY clause";
    }
    if (select->having() != nullptr) {
      return MakeSqlErrorAt(select->having())
             << "Query without FROM clause cannot have a HAVING clause";
    }
    if (select->window_clause() != nullptr) {
      return MakeSqlErrorAt(select->window_clause())
             << "Query without FROM clause cannot have a WINDOW clause";
    }
    if (order_by != nullptr) {
      return MakeSqlErrorAt(order_by)
             << "Query without FROM clause cannot have an ORDER BY clause";
    }
    if (select->qualify() != nullptr) {
      return MakeSqlErrorAt(select->qualify())
             << "Query without FROM clause cannot have a QUALIFY clause";
    }
    if (GetActiveLockModeOrNull() != nullptr) {
      return MakeSqlErrorAt(GetActiveLockModeOrNull())
             << "Query without FROM clause cannot have a lock mode clause";
    }

    // All children of the select node that are allowed on no-FROM-clause
    // queries should be listed here.  Ones that are not allowed should have
    // errors above.  This checks we didn't miss anything.
    for (int i = 0; i < select->num_children(); ++i) {
      const ASTNode* child = select->child(i);
      if (child != select->select_list() && child != select->select_as() &&
          child != select->hint()) {
        GOOGLESQL_RET_CHECK_FAIL() << "Select without FROM clause has child of type "
                         << child->GetNodeKindString()
                         << " that wasn't caught with an error";
      }
    }

    // Set up a SingleRowScan for this from clause, which produces one
    // row with zero columns. All output columns will come from
    // expressions in the select-list.
    *output_scan = MakeResolvedSingleRowScan();
    *output_name_list = empty_name_list_;
  }
  return absl::OkStatus();
}

// The current rules are arbitrary, and we should simply allow all operators
// to compose.
static absl::Status CheckPostfixTableOperators(
    const ASTTableExpression* table_expr) {
  if (table_expr->postfix_operators().empty() ||
      table_expr->postfix_operators().size() == 1) {
    return absl::OkStatus();
  }

  const ASTPostfixTableOperator* target_op = table_expr->postfix_operators(1);

  // Currently, the only allowed combination is PIVOT or UNPIVOT, followed by
  // TABLESAMPLE.
  if (table_expr->postfix_operators(1)->node_kind() == AST_SAMPLE_CLAUSE &&
      (table_expr->postfix_operators(0)->node_kind() == AST_PIVOT_CLAUSE ||
       table_expr->postfix_operators(0)->node_kind() == AST_UNPIVOT_CLAUSE)) {
    if (table_expr->postfix_operators().size() == 2) {
      return absl::OkStatus();
    }

    target_op = table_expr->postfix_operators(2);
  }

  return MakeSqlErrorAt(target_op)
         << "Unsupported combination of table operators. The only allowed "
            "combination is PIVOT/UNPIVOT followed by TABLESAMPLE. ";
}

// This is a self-contained table expression.  It can be an UNNEST, but
// only as a leaf - not one that has to wrap another scan and flatten it.
absl::Status Resolver::ResolveTableExpression(
    const ASTTableExpression* table_expr, const NameScope* external_scope,
    const NameScope* local_scope, bool is_leftmost,
    bool on_rhs_of_right_or_full_join,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (IsLateral(table_expr)) {
    if (!language().LanguageFeatureEnabled(FEATURE_LATERAL_JOIN)) {
      return MakeSqlErrorAt(table_expr) << "LATERAL join is not supported";
    }
    if (is_leftmost) {
      return MakeSqlErrorAt(table_expr)
             << "First item in the parenthesized join or at the top level of "
                "the FROM clause "
                "cannot be LATERAL";
    }
    if (on_rhs_of_right_or_full_join) {
      return MakeSqlErrorAt(table_expr)
             << "LATERAL items cannot be on the RHS of a RIGHT or FULL JOIN";
    }
    if (!table_expr->postfix_operators().empty()) {
      return MakeSqlErrorAt(table_expr->postfix_operators(0))
             << "LATERAL cannot be combined with suffix table operators";
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(CheckPostfixTableOperators(table_expr));

  switch (table_expr->node_kind()) {
    case AST_TABLE_PATH_EXPRESSION:
      return ResolveTablePathExpression(
          table_expr->GetAsOrDie<ASTTablePathExpression>(), external_scope,
          local_scope, output, output_name_list);

    case AST_TABLE_SUBQUERY: {
      const auto* table_subquery = table_expr->GetAsOrDie<ASTTableSubquery>();
      return ResolveTableSubquery(
          table_subquery,
          table_subquery->is_lateral() ? local_scope : external_scope, output,
          output_name_list);
    }

    case AST_JOIN:
      return ResolveJoin(table_expr->GetAsOrDie<ASTJoin>(), external_scope,
                         local_scope, is_leftmost, on_rhs_of_right_or_full_join,
                         output, output_name_list);

    case AST_PARENTHESIZED_JOIN:
      GOOGLESQL_RET_CHECK(!IsLateral(table_expr))
          << "Parenthesized join cannot be LATERAL";
      return ResolveParenthesizedJoin(
          table_expr->GetAsOrDie<ASTParenthesizedJoin>(), external_scope,
          local_scope, on_rhs_of_right_or_full_join, output, output_name_list);

    case AST_TVF: {
      const auto* tvf = table_expr->GetAsOrDie<ASTTVF>();
      return ResolveTVF(tvf, tvf->is_lateral() ? local_scope : external_scope,
                        /*pipe_input_arg=*/nullptr, output, output_name_list);
    }

    case AST_GRAPH_TABLE_QUERY: {
      // In the future, if we support LATERAL on GRAPH_TABLE(), we would pass
      // `local_scope` instead of `external_scope` for the LATERAL case.
      const auto* graph_query = table_expr->GetAsOrDie<ASTGraphTableQuery>();
      return ResolveGraphTableQuery(graph_query, external_scope, output,
                                    output_name_list);
    }
    default:
      return MakeSqlErrorAt(table_expr)
             << "Unhandled node type in from clause: "
             << table_expr->GetNodeKindString();
  }
}

bool Resolver::IsPathExpressionStartingFromScope(const ASTPathExpression& expr,
                                                 const NameScope& scope) {
  return scope.HasName(expr.first_name()->GetAsIdString());
}

bool Resolver::ShouldResolveAsArrayScan(const ASTTablePathExpression* table_ref,
                                        const NameScope* scope) {
  // Return true if it has UNNEST, or it is a path with at least two
  // identifiers where the first comes from <scope>.
  // Single-word identifiers are always resolved as table names.
  return table_ref->unnest_expr() != nullptr ||
         (table_ref->path_expr()->num_names() > 1 &&
          IsPathExpressionStartingFromScope(*(table_ref->path_expr()), *scope));
}

absl::Status Resolver::CheckValidValueTable(const ASTNode* ast_location,
                                            const Table& table) const {
  if (table.NumColumns() == 0 || table.GetColumn(0)->IsPseudoColumn()) {
    return MakeSqlErrorAt(ast_location)
           << "Table " << table.FullName()
           << " is a value table but does not have a value column";
  }
  for (int i = 1; i < table.NumColumns(); ++i) {
    if (!table.GetColumn(i)->IsPseudoColumn()) {
      return MakeSqlErrorAt(ast_location)
             << "Table " << table.FullName()
             << " is a value table but has multiple columns";
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::CheckValidValueTableFromTVF(
    const ASTTVF* path_expr, absl::string_view full_tvf_name,
    const TVFRelation& schema) const {
  int64_t num_pseudo_columns = std::count_if(
      schema.columns().begin(), schema.columns().end(),
      [](const TVFSchemaColumn& column) { return column.is_pseudo_column; });
  if (schema.num_columns() - num_pseudo_columns != 1) {
    return MakeSqlErrorAt(path_expr)
           << "Table-valued functions returning value tables should have "
           << "exactly one column, but value table TVF " << full_tvf_name
           << " returned has " << schema.num_columns() - num_pseudo_columns
           << " columns";
  }
  if (schema.column(0).is_pseudo_column) {
    return MakeSqlErrorAt(path_expr)
           << "Table-valued functions returning value tables should have "
           << "a value column at index 0, but value table TVF " << full_tvf_name
           << " returned has a pseudo column at index 0";
  }
  return absl::OkStatus();
}

absl::Status Resolver::AppendPivotColumnNameViaStringCast(
    const Value& pivot_value, std::string* column_name) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      Value string_value,
      CastValue(pivot_value, analyzer_options_.default_time_zone(),
                analyzer_options_.language(), type_factory_->get_string(),
                /*catalog=*/nullptr, /*canonicalize_zero=*/true));

  const std::string& raw_column_name = string_value.string_value();

  // Cast of any numeric type to string should never produce an empty string.
  GOOGLESQL_RET_CHECK(!raw_column_name.empty());

  // Transform the result so that it is a valid SQL identifier, which can be
  // accessed without backticks:
  // - If the entire column name (including prefixes from the pivot expression)
  //   would start with a digit, prepend an "_".
  // - For negative values, replace "-" as "minus_" for the minus sign.
  // - For decimal values, replace "." with "_point_" for the decimal point.
  if (column_name->empty() && isdigit(raw_column_name[0])) {
    absl::StrAppend(column_name, "_");
  }

  absl::StrAppend(column_name,
                  absl::StrReplaceAll(raw_column_name,
                                      {{"-", "minus_"}, {".", "_point_"}}));
  return absl::OkStatus();
}

absl::Status Resolver::AppendPivotColumnName(const Value& pivot_value,
                                             const ASTNode* ast_location,
                                             std::string* column_name) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const Type* type = pivot_value.type();
  if (pivot_value.is_null()) {
    absl::StrAppend(column_name, "NULL");
    return absl::OkStatus();
  }
  if (type->IsInt32() || type->IsInt64() || type->IsUint32() ||
      type->IsUint64() || type->IsNumericType() || type->IsBigNumericType() ||
      type->IsBool()) {
    GOOGLESQL_RETURN_IF_ERROR(
        AppendPivotColumnNameViaStringCast(pivot_value, column_name));
    return absl::OkStatus();
  }
  if (type->IsString()) {
    if (pivot_value.string_value().empty()) {
      if (!column_name->empty()) {
        absl::StrAppend(column_name, "_");
      }
      absl::StrAppend(column_name, "empty_string_value");
    } else {
      absl::StrAppend(column_name, pivot_value.string_value());
    }
    return absl::OkStatus();
  }
  if (type->IsDate()) {
    std::string formatted_date;
    GOOGLESQL_RETURN_IF_ERROR(functions::FormatDateToString(
        "%Y_%m_%d", pivot_value.date_value(),
        {.expand_Q = false, .expand_J = false}, &formatted_date));

    // If the column name is empty so far, prefix the date with an underscore
    // so that it's a valid SQL identifier.
    if (column_name->empty()) {
      absl::StrAppend(column_name, "_", formatted_date);
    } else {
      absl::StrAppend(column_name, formatted_date);
    }
    return absl::OkStatus();
  }
  if (type->IsEnum()) {
    absl::string_view enum_value_name;
    if (type->AsEnum()->FindName(pivot_value.enum_value(), &enum_value_name)) {
      absl::StrAppend(column_name, enum_value_name);
    } else {
      // As a fallback, treat the underlying value as an INT32.
      GOOGLESQL_RETURN_IF_ERROR(AppendPivotColumnNameViaStringCast(
          Value::Int32(pivot_value.enum_value()), column_name));
    }
    return absl::OkStatus();
  }
  if (type->IsStruct()) {
    for (int i = 0; i < type->AsStruct()->num_fields(); ++i) {
      if (i != 0) {
        absl::StrAppend(column_name, "_");
      }
      const StructField& field = type->AsStruct()->field(i);
      if (!field.name.empty()) {
        absl::StrAppend(column_name, field.name, "_");
      }
      GOOGLESQL_RETURN_IF_ERROR(AppendPivotColumnName(pivot_value.field(i), ast_location,
                                            column_name));
    }
    return absl::OkStatus();
  }
  return MakeSqlErrorAt(ast_location)
         << "PIVOT values of type " << type->ShortTypeName(product_mode())
         << " must specify an alias";
}

absl::StatusOr<Value> Resolver::GetStructPivotValue(
    const ResolvedMakeStruct* node, const ASTPivotValue* error_location) {
  const StructType* struct_type = node->type()->AsStruct();
  std::vector<Value> fields;
  fields.reserve(struct_type->num_fields());
  for (const auto& field : node->field_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value field_value,
                     GetPivotValue(field.get(), error_location));
    fields.push_back(std::move(field_value));
  }

  return Value::UnsafeStruct(struct_type, std::move(fields));
}

absl::StatusOr<Value> Resolver::GetPivotValue(
    const ResolvedExpr* node, const ASTPivotValue* error_location) {
  switch (node->node_kind()) {
    case RESOLVED_LITERAL: {
      // Mark this literal for preservation in the literal remover. It cannot
      // be replaced by a query parameter without causing the query to fail to
      // resolve (because we do not support implicit pivot-value aliases on
      // query parameters and, even if we ever did, the name wouldn't match the
      // one generated by the actual value).
      const ResolvedLiteral* literal = node->GetAs<ResolvedLiteral>();
      const_cast<ResolvedLiteral*>(literal)->set_preserve_in_literal_remover(
          true);
      return literal->value();
    }
    case RESOLVED_MAKE_STRUCT:
      return GetStructPivotValue(node->GetAs<ResolvedMakeStruct>(),
                                 error_location);
    case RESOLVED_CONSTANT: {
      if (language().LanguageFeatureEnabled(
              FEATURE_ANALYSIS_CONSTANT_PIVOT_COLUMN)) {
        absl::StatusOr<Value> constant_value = GetResolvedConstantValue(
            *node->GetAs<ResolvedConstant>(), analyzer_options());
        if (ShouldPropagateError(constant_value.status())) {
          return constant_value.status();
        }
        // Named constant body expression with runtime error should never be
        // lazily evaluated.
        GOOGLESQL_RET_CHECK(!absl::IsOutOfRange(constant_value.status()))
            << "Evaluation of constant expressions during semantic analysis "
            << "produced a runtime error: "
            << constant_value.status().message();
        // For all other errors, normalize the lazy evaluation error by
        // throwing SQL error with prefixed error message.
        if (!constant_value.ok()) {
          return MakeSqlErrorAt(error_location)
                 << "PIVOT column alias cannot be computed because the value "
                    "of constant "
                 << node->GetAs<ResolvedConstant>()->constant()->Name()
                 << " is not available: " << constant_value.status().message();
        }
        // If everything is ok, return the value
        return constant_value;
      }
      [[fallthrough]];
    }
    default:
      // TODO: b/277365877 - Support other ANALYSIS_CONST for PIVOT IN column.
      return MakeSqlErrorAt(error_location)
             << "Generating an implicit alias for this PIVOT value is not "
                "supported; please provide an explicit alias";
  }
}

absl::StatusOr<ResolvedColumn> Resolver::CreatePivotColumn(
    const ASTPivotExpression* ast_pivot_expr,
    const ResolvedExpr* resolved_pivot_expr, bool is_only_pivot_expr,
    const ASTPivotValue* ast_pivot_value,
    const ResolvedExpr* resolved_pivot_value) {
  std::string column_name;

  // Generate the name of the pivot column according to rules described in
  // (broken link).
  if (ast_pivot_expr->alias() != nullptr) {
    absl::StrAppend(&column_name, ast_pivot_expr->alias()->GetAsString(), "_");
  } else if (!is_only_pivot_expr) {
    return MakeSqlErrorAt(ast_pivot_expr)
           << "PIVOT expression must specify an alias unless it is the only "
              "pivot expression in the PIVOT clause";
  }

  if (ast_pivot_value->alias() != nullptr) {
    absl::StrAppend(&column_name, ast_pivot_value->alias()->GetAsString());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(Value pivot_value,
                     GetPivotValue(resolved_pivot_value, ast_pivot_value));
    GOOGLESQL_RETURN_IF_ERROR(
        AppendPivotColumnName(pivot_value, ast_pivot_value, &column_name));
  }

  return ResolvedColumn(AllocateColumnId(), kPivotId,
                        analyzer_options_.id_string_pool()->Make(column_name),
                        resolved_pivot_expr->annotated_type());
}

absl::Status Resolver::ResolvePivotExpressions(
    const ASTPivotExpressionList* ast_pivot_expr_list, const NameScope* scope,
    std::vector<std::unique_ptr<const ResolvedExpr>>* pivot_expr_columns,
    QueryResolutionInfo& query_resolution_info) {
  auto info = std::make_unique<ExprResolutionInfo>(
      scope, scope, scope,
      /*allows_aggregation_in=*/true,
      /*allows_analytic_in=*/false,
      /*use_post_grouping_columns_in=*/false, "PIVOT", &query_resolution_info);
  for (const ASTPivotExpression* pivot_expr :
       ast_pivot_expr_list->expressions()) {
    std::unique_ptr<const ResolvedExpr> resolved_pivot_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(pivot_expr->expression(), info.get(),
                                &resolved_pivot_expr));

    // Unlike scalar functions, aggregate function calls don't appear directly
    // in the ResolvedExpr. Instead, <resolved_pivot_expr> contains a
    // post-aggregation expression, which refers to the aggregation result by
    // accessing a special column, whose definition is stored off to the side
    // in the QueryResolutionInfo.
    //
    // Verify that
    //   1) We indeed have an aggregate function call saved in the
    //        QueryResolutionInfo --and--
    //   2) The ResolvedExpr is just a ColumnRef referring to the aggregate
    //        column from 1 (since post-aggregation logic in pivot expressions
    //        is not supported).
    GOOGLESQL_RETURN_IF_ERROR(query_resolution_info.PinToRowRange(std::nullopt));
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        pivot_expr_column_vector =
            query_resolution_info.release_aggregate_columns_to_compute();
    if (pivot_expr_column_vector.size() == 1) {
      auto& pivot_expr_column = pivot_expr_column_vector[0];
      GOOGLESQL_RET_CHECK(pivot_expr_column->Is<ResolvedComputedColumn>());
      if (resolved_pivot_expr->node_kind() == RESOLVED_COLUMN_REF &&
          (resolved_pivot_expr->GetAs<ResolvedColumnRef>()
               ->column()
               .column_id() == pivot_expr_column->column().column_id())) {
        if (pivot_expr_column->expr()->Is<ResolvedAggregateFunctionCall>()) {
          const ResolvedAggregateFunctionCall* agg_fn =
              pivot_expr_column->expr()->GetAs<ResolvedAggregateFunctionCall>();
          if (IsMeasureAggFunction(agg_fn->function())) {
            return MakeSqlErrorAt(pivot_expr->expression())
                   << "PIVOT expression cannot be a measure type "
                   << agg_fn->function()->Name() << " function call";
          }
          if (!agg_fn->group_by_list().empty()) {
            return MakeSqlErrorAt(pivot_expr->expression())
                   << "PIVOT expression cannot be a multi-level aggregate "
                      "function call";
          }
          if (agg_fn->where_expr() != nullptr) {
            return MakeSqlErrorAt(pivot_expr->expression())
                   << "PIVOT expression cannot use aggregate filtering WHERE "
                      "clause";
          }
          if (agg_fn->having_expr() != nullptr) {
            return MakeSqlErrorAt(pivot_expr->expression())
                   << "PIVOT expression cannot use aggregate filtering HAVING "
                      "clause";
          }
        }
        // The rewriter requires the root nodes of PIVOT expressions to have a
        // parse location, which gets used in error messages if the function
        // call is not supported. So, always include the parse location for this
        // node, whether explicitly asked for or not.
        auto* mutable_pivot_expr_column = const_cast<ResolvedComputedColumn*>(
            pivot_expr_column->GetAs<ResolvedComputedColumn>());
        std::unique_ptr<const ResolvedExpr> standalone_pivot_expr =
            mutable_pivot_expr_column->release_expr();
        const_cast<ResolvedExpr*>(standalone_pivot_expr.get())
            ->SetParseLocationRange(pivot_expr->location());

        pivot_expr_columns->push_back(std::move(standalone_pivot_expr));
        continue;
      }
    }
    return MakeSqlErrorAt(pivot_expr->expression())
           << "PIVOT expression must be an aggregate function call";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveForExprInPivotClause(
    const ASTExpression* for_expr, const NameScope* scope,
    std::unique_ptr<const ResolvedExpr>* resolved_for_expr) {
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
  auto info = std::make_unique<ExprResolutionInfo>(
      scope, scope, scope,
      /*allows_aggregation_in=*/false,
      /*allows_analytic_in=*/false,
      /*use_post_grouping_columns_in=*/false, "PIVOT",
      query_resolution_info.get());
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(for_expr, info.get(), resolved_for_expr));
  // TODO: figure out a way to remove the const_cast here.
  const_cast<ResolvedExpr*>(resolved_for_expr->get())
      ->SetParseLocationRange(for_expr->location());

  std::string no_grouping_type;
  if (!resolved_for_expr->get()->type()->SupportsGrouping(language(),
                                                          &no_grouping_type)) {
    return MakeSqlErrorAt(for_expr)
           << "Type " << no_grouping_type
           << " cannot be used as a FOR expression because it is not groupable";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveInClauseInPivotClause(
    const ASTPivotValueList* pivot_values, const NameScope* scope,
    const Type* for_expr_type,
    std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_in_exprs) {
  for (const ASTPivotValue* ast_pivot_value : pivot_values->values()) {
    // Even though pivot values must be constant, we still resolve them in a
    // scope which includes columns from the input table; this way, when such
    // columns are referenced, you get an error message about the pivot value
    // not being constant vs. the column not existing.
    auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
    auto info = std::make_unique<ExprResolutionInfo>(
        scope, scope, scope,
        /*allows_aggregation_in=*/false,
        /*allows_analytic_in=*/false,
        /*use_post_grouping_columns_in=*/false, "IN clause",
        query_resolution_info.get());

    std::unique_ptr<const ResolvedExpr> resolved_in_expr;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(ast_pivot_value->value(), info.get(), &resolved_in_expr));
    GOOGLESQL_ASSIGN_OR_RETURN(bool resolved_in_expr_is_constant_expr,
                     IsConstantExpression(resolved_in_expr.get()));
    if (!resolved_in_expr_is_constant_expr) {
      return MakeSqlErrorAt(ast_pivot_value->value())
             << "IN expression in PIVOT clause must be constant";
    }

    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(ast_pivot_value->value(), for_expr_type,
                                     kImplicitCoercion,
                                     "PIVOT IN list item must be type $0 to "
                                     "match the PIVOT FOR expression; found $1",
                                     &resolved_in_expr));

    resolved_in_exprs->push_back(std::move(resolved_in_expr));
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolvePivotClause(
    std::unique_ptr<const ResolvedScan> input_scan,
    std::shared_ptr<const NameList> input_name_list,
    const NameScope* previous_scope, bool input_is_subquery,
    const ASTPivotClause* ast_pivot_clause,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_PIVOT)) {
    return MakeSqlErrorAt(ast_pivot_clause) << "PIVOT is not supported";
  }

  if (in_strict_mode() && !input_is_subquery) {
    // PIVOT treats all unreferenced columns in the input table as GROUP BY
    // columns, allowing it to effectively function like "SELECT *". So, we
    // allow PIVOT in strict mode only when the input is a subquery. Since
    // strict mode require subqueries to explicitly state their columns in the
    // SELECT list, all of the grouping columns are explicitly stated in the
    // query and resiliant to new columns being added to underlying tables.
    return MakeSqlErrorAt(ast_pivot_clause)
           << "Input to PIVOT must be a subquery in strict name resolution "
              "mode";
  }

  if (input_name_list->HasValueTableColumns()) {
    return MakeSqlErrorAt(ast_pivot_clause)
           << "PIVOT is not allowed on value tables";
  }

  // Resolve pivot expressions
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
  auto pivot_scope =
      std::make_unique<NameScope>(previous_scope, input_name_list);
  std::vector<std::unique_ptr<const ResolvedExpr>> pivot_expr_columns;
  GOOGLESQL_RETURN_IF_ERROR(ResolvePivotExpressions(
      ast_pivot_clause->pivot_expressions(), pivot_scope.get(),
      &pivot_expr_columns, *query_resolution_info));

  // If one or more of the pivot expressions contains an ORDER BY clause,
  // wrap the input scan with a ProjectScan that computes the order-by columns.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
      query_resolution_info
          ->release_select_list_columns_to_compute_before_aggregation(),
      &input_scan));

  // Resolve FOR expression
  std::unique_ptr<const ResolvedExpr> resolved_for_expr;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveForExprInPivotClause(ast_pivot_clause->for_expression(),
                                  pivot_scope.get(), &resolved_for_expr));

  // Resolve IN expressions.
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_in_exprs;
  GOOGLESQL_RETURN_IF_ERROR(ResolveInClauseInPivotClause(
      ast_pivot_clause->pivot_values(), pivot_scope.get(),
      resolved_for_expr->type(), &resolved_in_exprs));

  // Determine final column list, starting with the grouping columns.
  auto final_name_list = std::make_shared<NameList>();
  absl::flat_hash_set<ResolvedColumn> referenced_columns;
  FindReferencedColumnsVisitor visitor(&referenced_columns);
  for (const auto& pivot_expr : pivot_expr_columns) {
    GOOGLESQL_RETURN_IF_ERROR(pivot_expr->Accept(&visitor));
  }
  GOOGLESQL_RETURN_IF_ERROR(resolved_for_expr->Accept(&visitor));

  std::vector<std::unique_ptr<const ResolvedComputedColumn>> group_by_list;
  std::vector<ResolvedColumn> output_column_list;
  std::vector<std::unique_ptr<const ResolvedPivotColumn>>
      output_column_detail_list;
  output_column_list.reserve(input_scan->column_list().size());
  for (int i = 0; i < input_name_list->num_columns(); ++i) {
    const NamedColumn& named_col = input_name_list->column(i);
    const ResolvedColumn& col = named_col.column();
    if (!referenced_columns.contains(col)) {
      std::string no_grouping_type;
      if (!col.type()->SupportsGrouping(language(), &no_grouping_type)) {
        return MakeSqlErrorAt(ast_pivot_clause)
               << "Column " << ColumnAliasOrPosition(named_col.name(), i)
               << " of type " << no_grouping_type
               << " cannot be used as an implicit grouping column of a PIVOT "
                  "clause";
      }

      // Columns from the input table not referenced in either a pivot
      // expression or FOR expression are considered implicit group-by columns.
      ResolvedColumn output_col(AllocateColumnId(), kGroupById,
                                named_col.name(), col.annotated_type());
      output_column_list.push_back(output_col);

      group_by_list.push_back(
          MakeResolvedComputedColumn(output_col, MakeColumnRef(col)));

      GOOGLESQL_RETURN_IF_ERROR(final_name_list->AddColumn(named_col.name(), output_col,
                                                 /*is_explicit=*/true));
    }
  }

  // Add a pivot column for each pivot-expr/pivot-value combination.
  output_column_list.reserve(resolved_in_exprs.size() *
                             pivot_expr_columns.size());
  for (int i = 0; i < resolved_in_exprs.size(); ++i) {
    for (int j = 0; j < pivot_expr_columns.size(); ++j) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          ResolvedColumn pivot_column,
          CreatePivotColumn(
              ast_pivot_clause->pivot_expressions()->expressions()[j],
              pivot_expr_columns[j].get(),
              /*is_only_pivot_expr=*/pivot_expr_columns.size() == 1,
              ast_pivot_clause->pivot_values()->values()[i],
              resolved_in_exprs[i].get()));
      output_column_detail_list.push_back(
          MakeResolvedPivotColumn(pivot_column,
                                  /*pivot_expr_index=*/j,
                                  /*pivot_value_index=*/i));
      output_column_list.push_back(pivot_column);
      GOOGLESQL_RETURN_IF_ERROR(final_name_list->AddColumn(
          pivot_column.name_id(), pivot_column, /*is_explicit=*/true));
    }
  }

  *output_name_list = final_name_list;
  if (ast_pivot_clause->output_alias() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(*output_name_list,
                     NameList::AddRangeVariableInWrappingNameList(
                         ast_pivot_clause->output_alias()->GetAsIdString(),
                         ast_pivot_clause->output_alias(), *output_name_list));
  }
  *output = MakeResolvedPivotScan(
      output_column_list, std::move(input_scan), std::move(group_by_list),
      std::move(pivot_expr_columns), std::move(resolved_for_expr),
      std::move(resolved_in_exprs), std::move(output_column_detail_list));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveUnpivotOutputValueColumns(
    const ASTPathExpressionList* ast_unpivot_expr_list,
    std::vector<ResolvedColumn>* unpivot_value_columns,
    const std::vector<const Type*>& value_column_types,
    const NameScope* scope) {
  int column_index = 0;
  if (value_column_types.size() !=
      ast_unpivot_expr_list->path_expression_list().size()) {
    return MakeSqlErrorAt(ast_unpivot_expr_list)
           << "The number of new columns introduced as value columns must be "
              "the same as the number of columns in the column groups of "
              "UNPIVOT IN clause";
  }
  for (const ASTPathExpression* unpivot_column :
       ast_unpivot_expr_list->path_expression_list()) {
    if (unpivot_column->num_names() > 1) {
      return MakeSqlErrorAt(unpivot_column)
             << "Only names of the new columns are accepted as value columns "
                "in UNPIVOT. Qualified names are not allowed";
    }
    IdString column_name = unpivot_column->first_name()->GetAsIdString();
    ResolvedColumn unpivot_resolved_column(
        AllocateColumnId(), /*table_name=*/kUnpivotColumnId, column_name,
        AnnotatedType(value_column_types.at(column_index),
                      /*annotation_map=*/nullptr));
    column_index++;
    unpivot_value_columns->push_back(unpivot_resolved_column);
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveUnpivotInClause(
    const ASTUnpivotInItemList* ast_unpivot_expr_list,
    std::vector<std::unique_ptr<const ResolvedUnpivotArg>>* resolved_in_items,
    const std::vector<ResolvedColumn>* input_scan_columns,
    absl::flat_hash_set<ResolvedColumn>* in_clause_input_columns,
    std::vector<const Type*>* value_column_types, const Type** label_type,
    std::vector<std::unique_ptr<const ResolvedLiteral>>* resolved_label_list,
    const ASTUnpivotClause* ast_unpivot_clause, const NameScope* scope) {
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);
  auto info = std::make_unique<ExprResolutionInfo>(
      scope, scope, scope,
      /*allows_aggregation_in=*/false,
      /*allows_analytic_in=*/false,
      /*use_post_grouping_columns_in=*/false, "IN clause",
      query_resolution_info.get());

  absl::flat_hash_set<ResolvedColumn> input_columns;
  input_columns.insert(input_scan_columns->begin(), input_scan_columns->end());
  absl::flat_hash_set<ResolvedColumn> input_columns_seen;
  TypeKind label_type_kind = TYPE_UNKNOWN;
  // The first column group is used to determine the size and order of datatypes
  // of the column groups in IN clause.
  bool is_first_column_group = true;
  for (const ASTUnpivotInItem* in_column_list :
       ast_unpivot_expr_list->in_items()) {
    // IN clause contains nested columns, i.e. groups of columns inside a list,
    // Example .. FOR .. IN( (a , b) , ( c , d ) ).
    // in_column_group will contain the columns lists resolved from inner groups
    // and the resulting list will be pushed onto the resolved_in_items vector,
    // in the same order as they appear in the IN clause.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> in_column_group;
    in_column_group.reserve(
        in_column_list->unpivot_columns()->path_expression_list().size());

    if (!is_first_column_group &&
        value_column_types->size() !=
            in_column_list->unpivot_columns()->path_expression_list().size()) {
      return MakeSqlErrorAt(in_column_list->unpivot_columns())
             << "All column groups in UNPIVOT IN clause must have the same "
                "number of columns";
    }
    int column_index = 0;
    for (const ASTPathExpression* in_column :
         in_column_list->unpivot_columns()->path_expression_list()) {
      // Get resolved column for column in the IN clause.
      std::unique_ptr<const ResolvedExpr> resolved_expr_out;
      const ASTExpression* in_column_expr = in_column->GetAs<ASTExpression>();
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveExpr(in_column_expr, info.get(), &resolved_expr_out));
      std::unique_ptr<const ResolvedColumnRef> in_column_ref;
      if (resolved_expr_out->node_kind() != RESOLVED_COLUMN_REF) {
        return MakeSqlErrorAt(in_column)
               << "UNPIVOT IN clause cannot have expressions, only column "
                  "names are allowed";
      }
      in_column_ref.reset(
          resolved_expr_out.release()->GetAs<ResolvedColumnRef>());
      AnnotatedType annotated_type = in_column_ref->annotated_type();
      if (CollationAnnotation::ExistsIn(annotated_type.annotation_map)) {
        if (!language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
          GOOGLESQL_RET_CHECK_FAIL() << "Collation annotation is unexpected";
        }
        return MakeSqlErrorAt(in_column)
               << "Collation "
               << annotated_type.annotation_map->DebugString(
                      CollationAnnotation::GetId())
               << " in UNPIVOT IN clause is not supported";
      }
      const Type* datatype = in_column_ref->type();
      // The first column group is used to determine the datatype order and
      // number of columns for all other column groups.
      if (is_first_column_group) {
        value_column_types->push_back(std::move(datatype));
      } else if (!datatype->Equals(value_column_types->at(column_index))) {
        GOOGLESQL_RET_CHECK_LT(column_index, value_column_types->size());
        return MakeSqlErrorAt(in_column)
               << "The datatype of column does not match with other datatypes "
                  "in the IN clause. Expected "
               << Type::TypeKindToString(
                      value_column_types->at(column_index)->kind(),
                      product_mode())
               << ", Found "
               << Type::TypeKindToString(datatype->kind(), product_mode());
      }
      column_index++;
      if (!input_columns.contains(in_column_ref->column())) {
        return MakeSqlErrorAt(in_column)
               << "Correlated column references in UNPIVOT IN clause is not "
                  "allowed";
      }
      if (input_columns_seen.contains(in_column_ref->column())) {
        // TODO: For queries where same input table column is
        // mapped to different columns in the unpivot input subquery, we might
        // want to allow those columns in the UNPIVOT IN clause.
        // Example, SELECT * FROM (SELECT x as a, x as b
        //            FROM t) UNPIVOT (y for z IN ( a , b ));
        return MakeSqlErrorAt(in_column)
               << "Column names in UNPIVOT IN clause cannot be repeated";
      }
      input_columns_seen.insert(in_column_ref->column());
      in_clause_input_columns->insert(in_column_ref->column());
      in_column_group.push_back(std::move(in_column_ref));
    }
    std::unique_ptr<const ResolvedUnpivotArg> col_set =
        MakeResolvedUnpivotArg(std::move(in_column_group));
    resolved_in_items->push_back(std::move(col_set));

    // Each column group can have an explicitly provided alias (after AS).
    // If the alias is not provided, a string label is auto-generated by
    // concatenating column names from the column group. The datatypes of all
    // labels must be the same and hence they must all be provided in the case
    // of non-string(integer) labels.
    GOOGLESQL_ASSIGN_OR_RETURN(Value label,
                     GetLabelForUnpivotInColumnList(in_column_list));
    if (label_type_kind == TYPE_UNKNOWN) {
      label_type_kind = label.type_kind();
    } else if (label_type_kind != label.type_kind()) {
      return MakeSqlErrorAt(in_column_list)
             << "All UNPIVOT labels must be the same type; when non-string "
                "labels are present then all labels must be explicitly "
                "provided";
    }
    resolved_label_list->push_back(MakeResolvedLiteralWithoutLocation(label));
    is_first_column_group = false;
  }
  *label_type = types::TypeFromSimpleTypeKind(label_type_kind);
  return absl::OkStatus();
}

absl::StatusOr<Value> Resolver::GetLabelForUnpivotInColumnList(
    const ASTUnpivotInItem* in_column_list) {
  if (in_column_list->alias() == nullptr ||
      in_column_list->alias()->label() == nullptr) {
    std::string column_group_label = "";
    for (const ASTPathExpression* in_col :
         in_column_list->unpivot_columns()->path_expression_list()) {
      if (column_group_label.empty()) {
        column_group_label = in_col->last_name()->GetAsString();
      } else {
        absl::StrAppend(&column_group_label, "_",
                        in_col->last_name()->GetAsString());
      }
    }
    return Value::String(column_group_label);
  }

  if (in_column_list->alias()->label()->node_kind() == AST_STRING_LITERAL) {
    return Value::String(in_column_list->alias()
                             ->label()
                             ->GetAsOrNull<ASTStringLiteral>()
                             ->string_value());
  } else if (in_column_list->alias()->label()->node_kind() == AST_INT_LITERAL) {
    Value label;
    if (!Value::ParseInteger(in_column_list->alias()
                                 ->label()
                                 ->GetAsOrNull<ASTIntLiteral>()
                                 ->image(),
                             &label)) {
      return MakeSqlErrorAt(in_column_list->alias()->label())
             << "Invalid integer label for UNPIVOT clause";
    }
    return label;
  }
  // The parser only accepts integer and string literal so we would
  // never get here, but added for sanity check.
  return MakeSqlErrorAt(in_column_list)
         << "UNPIVOT column labels must be string or integer";
}

absl::Status Resolver::ResolveUnpivotClause(
    std::unique_ptr<const ResolvedScan> input_scan,
    std::shared_ptr<const NameList> input_name_list,
    const NameScope* previous_scope, const ASTUnpivotClause* ast_unpivot_clause,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_UNPIVOT)) {
    return MakeSqlErrorAt(ast_unpivot_clause) << "UNPIVOT is not supported";
  }

  if (input_name_list->HasValueTableColumns()) {
    return MakeSqlErrorAt(ast_unpivot_clause)
           << "UNPIVOT is not allowed on value tables";
  }

  auto unpivot_scope =
      std::make_unique<NameScope>(previous_scope, input_name_list);

  // Resolve IN columns.
  std::vector<std::unique_ptr<const ResolvedUnpivotArg>> resolved_in_items;
  absl::flat_hash_set<ResolvedColumn> in_clause_input_columns;
  std::vector<const Type*> value_column_types;
  std::vector<std::unique_ptr<const ResolvedLiteral>> resolved_label_list;
  const Type* label_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveUnpivotInClause(
      ast_unpivot_clause->unpivot_in_items(), &resolved_in_items,
      &input_scan->column_list(), &in_clause_input_columns, &value_column_types,
      &label_type, &resolved_label_list, ast_unpivot_clause,
      unpivot_scope.get()));

  // Resolve unpivot output value columns.
  std::vector<ResolvedColumn> unpivot_value_columns;
  GOOGLESQL_RETURN_IF_ERROR(ResolveUnpivotOutputValueColumns(
      ast_unpivot_clause->unpivot_output_value_columns(),
      &unpivot_value_columns, value_column_types, unpivot_scope.get()));

  // Resolve unpivot output name column.
  if (ast_unpivot_clause->unpivot_output_name_column()->num_names() > 1) {
    return MakeSqlErrorAt(ast_unpivot_clause->unpivot_output_name_column())
           << "Only name of the new column is accepted as label column in "
              "UNPIVOT. Qualified names are not allowed";
  }
  IdString column_name = ast_unpivot_clause->unpivot_output_name_column()
                             ->first_name()
                             ->GetAsIdString();
  ResolvedColumn unpivot_label_column(
      AllocateColumnId(), /*table_name=*/kUnpivotColumnId, column_name,
      AnnotatedType(label_type, /*annotation_map=*/nullptr));

  // Create a list of output columns. It would contain:
  // Input columns that are not present in the IN clause, in the order as they
  // appear in the <input_scan> + unpivot_value_columns + unpivot_label_column
  std::vector<ResolvedColumn> output_column_list;
  auto final_name_list = std::make_shared<NameList>();

  // Columns from the input table that are not unpivoted in IN clause are
  // projected in the output as a new computed column.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      projected_input_column_list;
  for (int i = 0; i < input_name_list->num_columns(); ++i) {
    const NamedColumn& named_col = input_name_list->column(i);
    const ResolvedColumn& col = named_col.column();
    if (!in_clause_input_columns.contains(col)) {
      ResolvedColumn output_col(AllocateColumnId(),
                                /*table_name=*/kUnpivotColumnId,
                                named_col.name(), col.type());
      output_column_list.push_back(output_col);
      projected_input_column_list.push_back(
          MakeResolvedComputedColumn(output_col, MakeColumnRef(col)));
      GOOGLESQL_RETURN_IF_ERROR(final_name_list->AddColumn(named_col.name(), output_col,
                                                 /*is_explicit=*/true));
    }
  }
  for (int i = 0; i < unpivot_value_columns.size(); i++) {
    output_column_list.push_back(unpivot_value_columns[i]);
    GOOGLESQL_RETURN_IF_ERROR(final_name_list->AddColumn(
        unpivot_value_columns[i].name_id(), unpivot_value_columns[i], true));
  }
  output_column_list.push_back(unpivot_label_column);
  GOOGLESQL_RETURN_IF_ERROR(final_name_list->AddColumn(unpivot_label_column.name_id(),
                                             unpivot_label_column, true));

  *output_name_list = final_name_list;

  if (ast_unpivot_clause->output_alias() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        *output_name_list,
        NameList::AddRangeVariableInWrappingNameList(
            ast_unpivot_clause->output_alias()->GetAsIdString(),
            ast_unpivot_clause->output_alias(), *output_name_list));
  }

  GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
      *output_name_list, ast_unpivot_clause, "UNPIVOT", product_mode()));

  bool include_nulls = false;
  if (ast_unpivot_clause->null_filter() == ASTUnpivotClause::kInclude) {
    include_nulls = true;
  }

  *output = MakeResolvedUnpivotScan(
      output_column_list, std::move(input_scan),
      std::move(unpivot_value_columns), std::move(unpivot_label_column),
      std::move(resolved_label_list), std::move(resolved_in_items),
      std::move(projected_input_column_list), include_nulls);
  return absl::OkStatus();
}

static absl::StatusOr<
    ResolvedMatchRecognizePatternOperation::MatchRecognizePatternOperationType>
GetOperationType(const ASTRowPatternOperation* op) {
  switch (op->op_type()) {
    case ASTRowPatternOperation::ALTERNATE:
      return ResolvedMatchRecognizePatternOperation::ALTERNATE;
    case ASTRowPatternOperation::CONCAT:
      return ResolvedMatchRecognizePatternOperation::CONCAT;
    case ASTRowPatternOperation::PERMUTE:
    case ASTRowPatternOperation::OPERATION_TYPE_UNSPECIFIED:
      return ResolvedMatchRecognizePatternOperation::OPERATION_TYPE_UNSPECIFIED;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected operation type: " << op->op_type();
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveQuantifierBound(const ASTExpression* ast_quantifier_bound,
                                 ExprResolutionInfo* expr_resolution_info) {
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpr(ast_quantifier_bound, expr_resolution_info, &resolved_expr));

  // TODO: loosen const-ness? (e.g. catalog constant? grouping constants, etc?)
  // Either way, we cannot have a volatile function here because fixed-bound
  // quantifier should not evaluate the expression twice. In that case, the
  // ResolvedAST should change and have just 1 expression for that case.
  GOOGLESQL_RETURN_IF_ERROR(ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
      "MATCH_RECOGNIZE quantifier bound", ast_quantifier_bound,
      &resolved_expr));
  return resolved_expr;
}

absl::StatusOr<std::unique_ptr<const ResolvedMatchRecognizePatternExpr>>
Resolver::ResolveMatchRecognizePatternQuantifier(
    std::unique_ptr<const ResolvedMatchRecognizePatternExpr> operand,
    const ASTQuantifier* ast_quantifier,
    ExprResolutionInfo* expr_resolution_info) {
  std::unique_ptr<const ResolvedExpr> lower_bound;
  std::unique_ptr<const ResolvedExpr> upper_bound;

  switch (ast_quantifier->node_kind()) {
    case AST_SYMBOL_QUANTIFIER: {
      auto symbol = ast_quantifier->GetAsOrDie<ASTSymbolQuantifier>()->symbol();
      switch (symbol) {
        case ASTSymbolQuantifierEnums::QUESTION_MARK:
          lower_bound =
              MakeResolvedLiteral(/*ast_location=*/nullptr, Value::Int64(0));
          upper_bound =
              MakeResolvedLiteral(/*ast_location=*/nullptr, Value::Int64(1));
          break;
        case ASTSymbolQuantifierEnums::PLUS:
          lower_bound =
              MakeResolvedLiteral(/*ast_location=*/nullptr, Value::Int64(1));
          upper_bound = nullptr;
          break;
        case ASTSymbolQuantifierEnums::STAR:
          lower_bound =
              MakeResolvedLiteral(/*ast_location=*/nullptr, Value::Int64(0));
          upper_bound = nullptr;
          break;
        default:
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected symbol type: " << symbol;
      }
      break;
    }
    case AST_FIXED_QUANTIFIER: {
      auto bound = ast_quantifier->GetAsOrDie<ASTFixedQuantifier>()->bound();
      GOOGLESQL_ASSIGN_OR_RETURN(lower_bound,
                       ResolveQuantifierBound(bound, expr_resolution_info));
      // IMPORTANT: This won't work if the bound expression is volatile, because
      // then the results for upper & lower will differ.
      // We should just make these a single expression.
      GOOGLESQL_ASSIGN_OR_RETURN(upper_bound,
                       ResolvedASTDeepCopyVisitor::Copy(lower_bound.get()));
      break;
    }
    case AST_BOUNDED_QUANTIFIER: {
      auto bounded_quantifier =
          ast_quantifier->GetAsOrDie<ASTBoundedQuantifier>();
      // The wrapper ASTQuantifierBound node is never null, but the bound expr
      // inside may be.
      GOOGLESQL_RET_CHECK(bounded_quantifier->lower_bound() != nullptr);
      if (bounded_quantifier->lower_bound()->bound() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            lower_bound,
            ResolveQuantifierBound(bounded_quantifier->lower_bound()->bound(),
                                   expr_resolution_info));
      } else {
        // If the lower bound is not specified, this means a lower bound of 0,
        // i.e., ResolvedLiteral(0)
        lower_bound =
            MakeResolvedLiteral(/*ast_location=*/nullptr, Value::Int64(0));
      }

      // Unlike the lower bound, the upper bound is optional, to represent
      // "unlimited".
      GOOGLESQL_RET_CHECK(bounded_quantifier->upper_bound() != nullptr);
      if (bounded_quantifier->upper_bound()->bound() != nullptr) {
        GOOGLESQL_RET_CHECK(bounded_quantifier->upper_bound()->bound() != nullptr);
        GOOGLESQL_ASSIGN_OR_RETURN(
            upper_bound,
            ResolveQuantifierBound(bounded_quantifier->upper_bound()->bound(),
                                   expr_resolution_info));
        if (upper_bound->Is<ResolvedLiteral>() &&
            lower_bound->Is<ResolvedLiteral>() &&
            upper_bound->GetAs<ResolvedLiteral>()->value().int64_value() <
                lower_bound->GetAs<ResolvedLiteral>()->value().int64_value()) {
          return MakeSqlErrorAt(ast_quantifier)
                 << "Quantifier upper bound cannot be smaller than the lower "
                    "bound";
        }
      }
      break;
    }
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected quantifier type: "
                       << ast_quantifier->node_kind();
    }
  }

  return ResolvedMatchRecognizePatternQuantificationBuilder()
      .set_operand(std::move(operand))
      .set_lower_bound(std::move(lower_bound))
      .set_upper_bound(std::move(upper_bound))
      .set_is_reluctant(ast_quantifier->is_reluctant())
      .Build();
}

absl::Status Resolver::ResolveMatchRecognizePatternExpr(
    const ASTRowPatternExpression* node,
    ExprResolutionInfo* expr_resolution_info,
    const IdStringHashMapCase<const ASTIdentifier*>& pattern_variables_defined,
    IdStringHashMapCase<const ASTIdentifier*>& undeclared_pattern_variables,
    IdStringHashSetCase& referenced_pattern_variables,
    std::unique_ptr<const ResolvedMatchRecognizePatternExpr>& output) {
  switch (node->node_kind()) {
    case AST_EMPTY_ROW_PATTERN: {
      GOOGLESQL_ASSIGN_OR_RETURN(output,
                       ResolvedMatchRecognizePatternEmptyBuilder().Build());
      return absl::OkStatus();
    }
    case AST_ROW_PATTERN_ANCHOR: {
      ResolvedMatchRecognizePatternAnchorEnums::Mode anchor_mode;
      switch (node->GetAsOrDie<ASTRowPatternAnchor>()->anchor()) {
        case ASTRowPatternAnchorEnums::START:
          anchor_mode = ResolvedMatchRecognizePatternAnchorEnums::START;
          break;
        case ASTRowPatternAnchorEnums::END:
          anchor_mode = ResolvedMatchRecognizePatternAnchorEnums::END;
          break;
        case ASTRowPatternAnchorEnums::ANCHOR_UNSPECIFIED:
          GOOGLESQL_RET_CHECK_FAIL() << "Row pattern anchor is not set";
          break;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(output, ResolvedMatchRecognizePatternAnchorBuilder()
                                   .set_mode(anchor_mode)
                                   .Build());
      return absl::OkStatus();
    }
    case AST_ROW_PATTERN_VARIABLE: {
      const auto* var = node->GetAsOrDie<ASTRowPatternVariable>();
      IdString name = var->name()->GetAsIdString();
      referenced_pattern_variables.insert(name);
      absl::string_view standardized_name = name.ToStringView();
      auto it = pattern_variables_defined.find(name);
      if (it == pattern_variables_defined.end()) {
        return MakeSqlErrorAt(var)
               << "Pattern variable `" << name.ToStringView()
               << "` is not defined in the DEFINE clause";
      }
      // The variable has a DEFINE entry, use the name from there.
      standardized_name = it->second->GetAsStringView();
      GOOGLESQL_ASSIGN_OR_RETURN(output,
                       ResolvedMatchRecognizePatternVariableRefBuilder()
                           // Variable names are normalized in the ResolvedAST
                           .set_name(standardized_name)
                           .Build());
      return absl::OkStatus();
    }
    case AST_ROW_PATTERN_OPERATION: {
      const auto* op = node->GetAsOrDie<ASTRowPatternOperation>();
      GOOGLESQL_ASSIGN_OR_RETURN(auto op_type, GetOperationType(op));
      ResolvedMatchRecognizePatternOperationBuilder builder;
      builder.set_op_type(op_type);
      for (const ASTRowPatternExpression* expr : op->inputs()) {
        std::unique_ptr<const ResolvedMatchRecognizePatternExpr> arg;
        GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizePatternExpr(
            expr, expr_resolution_info, pattern_variables_defined,
            undeclared_pattern_variables, referenced_pattern_variables, arg));
        builder.add_operand_list(std::move(arg));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(output, std::move(builder).Build());
      return absl::OkStatus();
    }
    case AST_ROW_PATTERN_QUANTIFICATION: {
      const auto* quantification =
          node->GetAsOrDie<ASTRowPatternQuantification>();

      // Recursively resolve the operand.
      std::unique_ptr<const ResolvedMatchRecognizePatternExpr> operand;
      GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizePatternExpr(
          quantification->operand(), expr_resolution_info,
          pattern_variables_defined, undeclared_pattern_variables,
          referenced_pattern_variables, operand));

      GOOGLESQL_ASSIGN_OR_RETURN(output,
                       ResolveMatchRecognizePatternQuantifier(
                           std::move(operand), quantification->quantifier(),
                           expr_resolution_info));
      return absl::OkStatus();
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected node kind: " << node->GetNodeKindString();
  }
}

static absl::StatusOr<ResolvedMatchRecognizeScan::AfterMatchSkipMode>
ResolveAfterMatchSkipMode(
    const ASTAfterMatchSkipClause* ast_after_match_skip_clause) {
  if (ast_after_match_skip_clause == nullptr) {
    // When the AFTER MATCH SKIP clause is omitted, the default is
    // PAST_LAST_ROW.
    return ResolvedMatchRecognizeScanEnums::END_OF_MATCH;
  }
  switch (ast_after_match_skip_clause->target_type()) {
    case ASTAfterMatchSkipClauseEnums::PAST_LAST_ROW:
      return ResolvedMatchRecognizeScanEnums::END_OF_MATCH;
    case ASTAfterMatchSkipClauseEnums::TO_NEXT_ROW:
      return ResolvedMatchRecognizeScanEnums::NEXT_ROW;
    case ASTAfterMatchSkipClauseEnums::AFTER_MATCH_SKIP_TARGET_UNSPECIFIED:
      GOOGLESQL_RET_CHECK_FAIL() << "AFTER MATCH SKIP clause must specify a mode.";
  }
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedOption>>>
Resolver::ResolveMatchRecognizeOptionsList(
    const ASTOptionsList* ast_options_list) {
  // OPTIONS() list is resolved with an empty namescope, and the function
  // arguments need to be cleared, if we're inside a CREATE FUNCTION STATEMENT.
  const auto* fn_arg_info = function_argument_info_;
  function_argument_info_ = nullptr;
  absl::Cleanup cleanup = [this, fn_arg_info] {
    this->function_argument_info_ = fn_arg_info;
  };

  std::vector<std::unique_ptr<const ResolvedOption>> options_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveOptionsList(ast_options_list,
                                     /*allow_alter_array_operators=*/false,
                                     &options_list));

  bool seen_use_longest_match = false;
  for (const auto& option : options_list) {
    auto location = option->GetParseLocationOrNULL() != nullptr
                        ? option->GetParseLocationOrNULL()->start()
                        : ast_options_list->location().start();

    if (!googlesql_base::CaseEqual(option->name(), "use_longest_match")) {
      return MakeSqlErrorAtPoint(location)
             << "Unrecognized MATCH_RECOGNIZE option `" << option->name()
             << "`";
    }
    if (!option->qualifier().empty()) {
      return MakeSqlErrorAtPoint(location)
             << "MATCH_RECOGNIZE option `" << option->name()
             << "` cannot have a qualifier";
    }
    if (seen_use_longest_match) {
      return MakeSqlErrorAtPoint(location)
             << "MATCH_RECOGNIZE option `" << option->name()
             << "` cannot be specified multiple times";
    }
    seen_use_longest_match = true;
    if (option->value() == nullptr) {
      return MakeSqlErrorAtPoint(location)
             << "MATCH_RECOGNIZE option `" << option->name()
             << "` requires a value";
    }
    if (!option->value()->type()->IsBool()) {
      return MakeSqlErrorAtPoint(location)
             << "MATCH_RECOGNIZE option `" << option->name()
             << "` requires a boolean value, but found "
             << option->value()->type()->ShortTypeName(product_mode());
    }
    if (option->value()->node_kind() != RESOLVED_LITERAL &&
        option->value()->node_kind() != RESOLVED_PARAMETER) {
      return MakeSqlErrorAtPoint(location)
             << "MATCH_RECOGNIZE option `" << option->name()
             << "` requires a boolean literal or parameter";
    }
    if (option->value()->node_kind() == RESOLVED_LITERAL &&
        option->value()->GetAs<ResolvedLiteral>()->value().is_null()) {
      return MakeSqlErrorAtPoint(location)
             << "MATCH_RECOGNIZE option `" << option->name()
             << "` cannot be NULL";
    }
  }
  return std::move(options_list);
}

absl::StatusOr<std::vector<
    std::unique_ptr<const ResolvedMatchRecognizeVariableDefinition>>>
Resolver::ResolveMatchRecognizeVariableDefinitions(
    const ASTSelectList* definition_list,
    IdStringHashSetCase referenced_pattern_variables, NameScope* local_scope,
    QueryResolutionInfo* query_resolution_info,
    const IdStringHashMapCase<NameTarget>& disallowed_access_targets) {
  std::unique_ptr<NameScope> define_scope;
  GOOGLESQL_RETURN_IF_ERROR(local_scope->CopyNameScopeWithOverridingNameTargets(
      disallowed_access_targets, &define_scope));

  define_scope->DisallowCorrelatedAccess(
      "Correlated access to MATCH_RECOGNIZE columns in the DEFINE clause is "
      "not allowed");

  std::vector<std::unique_ptr<const ResolvedMatchRecognizeVariableDefinition>>
      resolved_pattern_var_defs;

  auto def_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info, define_scope.get(),
      ExprResolutionInfoOptions{
          .analytic_name_scope = define_scope.get(),
          .allows_aggregation = false,
          .allows_analytic = false,
          .clause_name = "MATCH_RECOGNIZE DEFINE predicate",
          .in_match_recognize_define = true,
      });

  resolved_pattern_var_defs.reserve(definition_list->columns().size());

  for (const auto* def : definition_list->columns()) {
    GOOGLESQL_RET_CHECK(def->alias() != nullptr);

    IdString name = def->alias()->GetAsIdString();
    if (!referenced_pattern_variables.contains(name)) {
      return MakeSqlErrorAt(def->alias())
             << "Pattern variable `" << name.ToStringView()
             << "` is defined but not used in the pattern";
    }

    std::unique_ptr<const ResolvedExpr> expr;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(def->expression(), def_expr_resolution_info.get(), &expr));

    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(
        def->expression(), def_expr_resolution_info->clause_name, &expr));

    GOOGLESQL_RET_CHECK(!def_expr_resolution_info->findings.has_aggregation);

    GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_def,
                     ResolvedMatchRecognizeVariableDefinitionBuilder()
                         // Variable names are normalized in the ResolvedAST
                         .set_name(def->alias()->GetAsStringView())
                         .set_predicate(std::move(expr))
                         .Build());
    resolved_pattern_var_defs.push_back(std::move(resolved_def));
  }

  return resolved_pattern_var_defs;
}

absl::Status Resolver::ResolveMatchRecognizeMeasures(
    const ASTSelectList* ast_measures, const NameList* input_name_list,
    const NameScope* input_scope, const NameScope* local_scope,
    absl::Span<const ResolvedColumn> partitioning_columns,
    const IdStringHashMapCase<const ASTIdentifier*>& pattern_variables_defined,
    const IdStringHashMapCase<NameTarget>& pattern_variable_targets,
    const IdStringHashMapCase<NameTarget>& disallowed_access_targets,
    std::unique_ptr<const ResolvedScan>& input_scan, NameList& out_name_list,
    std::vector<std::unique_ptr<const ResolvedMeasureGroup>>&
        out_measure_groups,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>&
        out_resolved_measures) {
  GOOGLESQL_RET_CHECK(ast_measures != nullptr);
  out_resolved_measures.reserve(ast_measures->columns().size());

  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(this);

  // Add the partitioning columns as grouping columns to the QRI.
  // Remember that ResolvedPartitionBy only holds ResolvedColumnRefs, while the
  // ResolvedComputedColumns themselves are on the input scan (which are on the
  // input project scan). This is important for multilevel aggregations.
  for (const auto& column : partitioning_columns) {
    GOOGLESQL_RET_CHECK(input_scan->Is<ResolvedProjectScan>());
    std::unique_ptr<const ResolvedExpr> partitioning_expr_copy;
    for (const auto& computed_column :
         input_scan->GetAs<ResolvedProjectScan>()->expr_list()) {
      if (computed_column->column() == column) {
        ResolvedColumn source_column;
        bool is_correlated = false;
        ValidNamePath valid_name_path;
        if (GetSourceColumnAndNamePath(computed_column->expr(), column,
                                       &source_column, &is_correlated,
                                       &valid_name_path, id_string_pool_) &&
            !is_correlated) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              partitioning_expr_copy,
              ResolvedASTDeepCopyVisitor::Copy(computed_column->expr()));
          query_resolution_info->AddGroupByComputedColumnIfNeeded(
              column, std::move(partitioning_expr_copy),
              /*pre_group_by_expr=*/nullptr,
              /*override_existing_column=*/false);
          query_resolution_info->mutable_group_by_valid_field_info_map()
              ->InsertNamePath(source_column, valid_name_path);
          break;
        }
      }
    }
  }

  std::unique_ptr<NameScope> measures_scope;
  GOOGLESQL_RETURN_IF_ERROR(local_scope->CopyNameScopeWithOverridingNameTargets(
      pattern_variable_targets, &measures_scope));

  // Outside of aggregations, we only see partitioning columns.
  // Pattern variable refs are disallowed.
  // In the future, we might consider allowing variables that are guaranteed
  // to occur at most once in the pattern.
  std::unique_ptr<NameScope> measures_scope_post_agg;
  {
    std::unique_ptr<const NameScope> post_agg_scope;
    GOOGLESQL_RETURN_IF_ERROR(CreatePostGroupByNameScope(
        input_scope, query_resolution_info.get(), &post_agg_scope));

    GOOGLESQL_RETURN_IF_ERROR(post_agg_scope->CopyNameScopeWithOverridingNameTargets(
        disallowed_access_targets, &measures_scope_post_agg));
  }

  // Outside of aggregations, only see MATCH_NUMBER() is available.
  measures_scope_post_agg->EnableMatchNumberFunction();

  // Within an aggregation, all match row functions are available.
  measures_scope->EnableAllMatchFunctions();
  measures_scope->DisallowCorrelatedAccess(
      "Correlated access to MATCH_RECOGNIZE columns in the MEASURES clause is "
      "not allowed");

  auto measure_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info.get(), /*name_scope=*/measures_scope_post_agg.get(),
      /*aggregate_name_scope=*/measures_scope.get(),
      /*analytic_name_scope=*/measures_scope_post_agg.get(),
      ExprResolutionInfoOptions{
          // These will be controlled by ONE ROW vs ALL ROWS PER MATCH.
          .allows_aggregation = true,
          .allows_analytic = false,
          .clause_name = "MATCH_RECOGNIZE MEASURES clause",
      });

  IdStringHashMapCase<const ASTIdentifier*> measure_names;
  for (const auto* measure : ast_measures->columns()) {
    // Clear the scoping state from the previous measure. Even if the previous
    // measure had no aggregations, it may still mark the QRI as pinned, e.g.
    //    MEASURES partition_key AS m1
    // The reference to `partition_key` causes the QRI to be pinned.
    // We do not want this leaking to this new, separate measure.
    *query_resolution_info->scoped_aggregation_state() = {};

    // By default, assume that each measure column applies to the entire match
    // unless we find a reference to a symbol qualifier that says otherwise.
    // Don't carry over state accumulated from the previous measure column.
    // Any prior measures should have already cleared the scoping state.

    std::unique_ptr<const ResolvedExpr> expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(measure->expression(),
                                measure_expr_resolution_info.get(), &expr));
    ResolvedColumn column(AllocateColumnId(), kMatchRecognizeId,
                          measure->alias()->GetAsIdString(),
                          expr->annotated_type());
    GOOGLESQL_RET_CHECK(side_effect_scope_depth_ == 0 ||
              !language().LanguageFeatureEnabled(
                  FEATURE_ENFORCE_CONDITIONAL_EVALUATION));
    // If we need to defer side effects, we should use DeferredComputedColumn.
    out_resolved_measures.push_back(
        MakeResolvedComputedColumn(column, std::move(expr)));

    // Error on duplicate names in the measures list
    if (measure_names.contains(measure->alias()->GetAsIdString())) {
      return MakeSqlErrorAt(measure->alias())
             << "Measure `" << measure->alias()->GetAsStringView()
             << "` is already defined";
    }
    GOOGLESQL_RETURN_IF_ERROR(out_name_list.AddColumn(column.name_id(), column,
                                            /*is_explicit=*/true));
  }

  // Wrap the input in another project scan if some computations are needed,
  // e.g. for expressions in ORDER BY and HAVING MAX/MIN in the aggregations.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
      query_resolution_info
          ->release_select_list_columns_to_compute_before_aggregation(),
      &input_scan));

  GOOGLESQL_RET_CHECK(
      query_resolution_info->unscoped_aggregate_columns_to_compute().empty());

  out_measure_groups.reserve(
      query_resolution_info->scoped_aggregate_columns_to_compute().size());

  for (auto& [var, aggs] :
       query_resolution_info->release_scoped_aggregate_columns_to_compute()) {
    for (const auto& measure_expr : aggs) {
      if (measure_expr->Is<ResolvedDeferredComputedColumn>()) {
        const ASTNode* location = ast_measures;
        // Try to find the location of this particular aggregation.
        for (const auto& [ast_fn_call, resolved_agg_call] :
             query_resolution_info->aggregate_expr_map()) {
          if (resolved_agg_call == measure_expr.get()) {
            location = ast_fn_call;
            break;
          }
        }
        return MakeSqlErrorAt(location) << "Conditional evaluation is not "
                                           "supported with MATCH_RECOGNIZE";
      }
    }

    auto group_builder =
        ResolvedMeasureGroupBuilder().set_aggregate_list(std::move(aggs));

    auto it = pattern_variables_defined.find(var);
    if (it == pattern_variables_defined.end()) {
      GOOGLESQL_RET_CHECK(var.empty());
      if (group_builder.aggregate_list().empty()) {
        continue;
      }
    } else {
      absl::string_view standardized_name = it->second->GetAsStringView();
      group_builder.set_pattern_variable_ref(
          ResolvedMatchRecognizePatternVariableRefBuilder().set_name(
              standardized_name));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(auto measure_group, std::move(group_builder).Build());
    GOOGLESQL_RET_CHECK(!measure_group->aggregate_list().empty());
    out_measure_groups.push_back(std::move(measure_group));

    // Make the tree deterministic in tests by sorting the groups based on the
    // variable name.
    std::sort(out_measure_groups.begin(), out_measure_groups.end(),
              [](const auto& a, const auto& b) {
                if (b->pattern_variable_ref() == nullptr) {
                  return false;
                }
                if (a->pattern_variable_ref() == nullptr) {
                  return true;
                }
                return a->pattern_variable_ref()->name() <
                       b->pattern_variable_ref()->name();
              });
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveMatchRecognizeClause(
    std::unique_ptr<const ResolvedScan> input_scan,
    std::shared_ptr<const NameList> input_name_list,
    const NameScope* previous_scope,
    const ASTMatchRecognizeClause* ast_match_recognize_clause,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_MATCH_RECOGNIZE)) {
    return MakeSqlErrorAt(ast_match_recognize_clause)
           << "MATCH_RECOGNIZE is not supported";
  }

  if (side_effect_scope_depth_ > 0 &&
      language().LanguageFeatureEnabled(
          FEATURE_ENFORCE_CONDITIONAL_EVALUATION)) {
    return MakeSqlErrorAt(ast_match_recognize_clause)
           << "Conditional evaluation is not supported with MATCH_RECOGNIZE";
  }

  if (match_recognize_state_.has_value()) {
    return MakeSqlErrorAt(ast_match_recognize_clause)
           << "MATCH_RECOGNIZE clause cannot be nested inside another "
              "MATCH_RECOGNIZE clause";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const ResolvedOption>> options_list,
      ResolveMatchRecognizeOptionsList(
          ast_match_recognize_clause->options_list()));

  // Resolve PARTITION BY and ORDER BY clause in the input relation's namescope.
  // PARTITION BY and ORDEER BY expressions are resolved in the NameScope of the
  // input relation.
  auto input_scope =
      std::make_unique<NameScope>(previous_scope, input_name_list);

  const ASTOrderBy* ast_order_by = ast_match_recognize_clause->order_by();
  GOOGLESQL_RET_CHECK(ast_order_by != nullptr);
  GOOGLESQL_RET_CHECK_GT(ast_order_by->ordering_expressions().size(), 0);

  auto input_query_resolution_info =
      std::make_unique<QueryResolutionInfo>(this);
  input_query_resolution_info->analytic_resolver()->DisableNamedWindowRefs(
      "MATCH_RECOGNIZE clause");

  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      input_query_resolution_info.get(), input_scope.get(),
      ExprResolutionInfoOptions{.aggregate_name_scope = nullptr,
                                .analytic_name_scope = nullptr,
                                .allows_aggregation = false,
                                .allows_analytic = false,
                                .clause_name = "MATCH_RECOGNIZE"});

  std::vector<ResolvedColumn> resolved_partitioning_columns;
  GOOGLESQL_RETURN_IF_ERROR(input_query_resolution_info->analytic_resolver()
                      ->SetMatchRecognizeWindowContext(
                          ast_match_recognize_clause->partition_by(),
                          ast_match_recognize_clause->order_by(),
                          expr_resolution_info.get(),
                          resolved_partitioning_columns));

  auto out_name_list = std::make_unique<NameList>();
  const ASTPartitionBy* ast_partition_by =
      ast_match_recognize_clause->partition_by();
  if (ast_partition_by != nullptr) {
    for (int i = 0; i < resolved_partitioning_columns.size(); ++i) {
      // TODO: the AS syntax would make this explicit, and we should use that
      // alias, and mark the column as explicit.
      IdString alias =
          GetAliasForExpression(ast_partition_by->partitioning_expressions(i));
      if (alias.empty()) {
        alias = MakeIdString(absl::StrCat("$partition_by_col", (i + 1)));
      }

      GOOGLESQL_RETURN_IF_ERROR(out_name_list->AddColumn(alias,
                                               resolved_partitioning_columns[i],
                                               /*is_explicit=*/false));
    }
  }

  // Collect all the pattern variable names from the DEFINE clause.
  // We do that first because the names in this clause specify the exact form
  // (i.e, case) of the variable name.
  const auto* definition_list =
      ast_match_recognize_clause->pattern_variable_definition_list();
  GOOGLESQL_RET_CHECK(definition_list != nullptr);

  IdStringHashMapCase<const ASTIdentifier*> pattern_variables_defined;
  for (const auto* def : definition_list->columns()) {
    GOOGLESQL_RET_CHECK(def->alias() != nullptr);
    IdString name = def->alias()->GetAsIdString();
    auto [_, success] =
        pattern_variables_defined.insert({name, def->alias()->identifier()});
    if (!success) {
      return MakeSqlErrorAt(def->alias())
             << "Pattern variable `" << def->alias()->GetAsStringView()
             << "` is defined multiple times";
    }
  }

  // Find all the undeclared names used in the pattern. Those are equivalent to
  // having an implicit entry with predicate as "TRUE". The standard form of the
  // name is taken from the first (leftmost) reference, so we keep track of the
  // order of appearance. This is also useful for error location.
  IdStringHashMapCase<const ASTIdentifier*> undeclared_pattern_variables;
  IdStringHashSetCase referenced_pattern_variables;

  // Used to resolve expressions in the pattern quantifiers. For example, lower
  // and upper bounds could be parameters.
  auto pattern_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      input_scope.get(), "MATCH_RECOGNIZE pattern");

  std::unique_ptr<const ResolvedMatchRecognizePatternExpr> resolved_pattern;
  GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizePatternExpr(
      ast_match_recognize_clause->pattern(), pattern_expr_resolution_info.get(),
      pattern_variables_defined, undeclared_pattern_variables,
      referenced_pattern_variables, resolved_pattern));

  // Both declared and undeclared.
  IdStringHashSetCase all_pattern_variables;
  for (const auto& [name, _] : pattern_variables_defined) {
    all_pattern_variables.insert(name);
  }
  for (const auto& [name, _] : undeclared_pattern_variables) {
    all_pattern_variables.insert(name);
  }

  // For the local scopes, update the name lists. For example,
  // `PARTITION BY x` should refer to the renamed column `x`, not the original
  // input column.
  if (ast_partition_by == nullptr) {
    GOOGLESQL_RET_CHECK(out_name_list->columns().empty());
  } else {
    GOOGLESQL_RET_CHECK_EQ(out_name_list->columns().size(),
                 resolved_partitioning_columns.size());
  }

  std::shared_ptr<NameList> local_name_list = out_name_list->Copy();
  IdStringSetCase partitioning_column_names;
  for (IdString name : out_name_list->GetColumnNames()) {
    partitioning_column_names.insert(name);
  }
  NameList::MergeOptions merge_options{.excluded_field_names =
                                           &partitioning_column_names};
  GOOGLESQL_RETURN_IF_ERROR(local_name_list->MergeFrom(
      *input_name_list, ast_match_recognize_clause, merge_options));
  std::unique_ptr<NameScope> local_scope;
  GOOGLESQL_RETURN_IF_ERROR(input_scope->CopyNameScopeWithOverridingNames(
      out_name_list->Copy(), &local_scope));

  // Disallow access to pattern variables. We do not want to allow access to
  // columns through them either, hence the empty column list.
  IdStringHashMapCase<NameTarget> disallowed_access_targets;
  IdStringHashMapCase<NameTarget> pattern_variable_targets;
  for (const auto name : all_pattern_variables) {
    NameTarget disallowed_target(std::make_shared<NameList>(),
                                 /*is_pattern_variable=*/true);
    disallowed_target.SetAccessError(
        disallowed_target.kind(),
        "Cannot access columns through pattern variables.");
    GOOGLESQL_RET_CHECK(
        disallowed_access_targets.insert({name, std::move(disallowed_target)})
            .second);
    GOOGLESQL_RET_CHECK(pattern_variable_targets
                  .insert({name, NameTarget(local_name_list,
                                            /*is_pattern_variable=*/true)})
                  .second);
  }

  // Resolve the predicates in the DEFINE clause. Pattern variable refs are
  // disallowed.
  // Use the same QueryResolutionInfo as we want the windows to be the same.
  // For example, `LEAD(2, x)` without an OVER() clause takes on the "main"
  // window, defined by the MATCH_RECOGNIZE's PARTITION BY and ORDER BY
  // clauses.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<
          std::unique_ptr<const ResolvedMatchRecognizeVariableDefinition>>
          resolved_pattern_var_defs,
      ResolveMatchRecognizeVariableDefinitions(
          ast_match_recognize_clause->pattern_variable_definition_list(),
          referenced_pattern_variables, local_scope.get(),
          input_query_resolution_info.get(), disallowed_access_targets));

  // Now that we collected all analytic windows, from the PARTITION BY,
  // ORDER BY, and (in the future) DEFINE clauses, we need to add any
  // computation required in a project scan, and collect the analytic
  // computations used in DEFINE.
  GOOGLESQL_RETURN_IF_ERROR(
      input_query_resolution_info->analytic_resolver()->CreateAnalyticScan(
          input_query_resolution_info.get(), &input_scan));

  // Grab the analytic groups from the analytic scan, and restore the input
  // scan from it (potentially wrapped in a project scan for any computations
  // needed for the PARTITION BY and ORDER BY clauses.)
  auto analytic_scan_builder =
      ToBuilder(std::unique_ptr<const ResolvedAnalyticScan>(
          input_scan.release()->GetAs<ResolvedAnalyticScan>()));

  input_scan = analytic_scan_builder.release_input_scan();

  GOOGLESQL_RET_CHECK_EQ(analytic_scan_builder.function_group_list().size(), 1);

  // AFTER MATCH SKIP clause
  GOOGLESQL_ASSIGN_OR_RETURN(
      ResolvedMatchRecognizeScanEnums::AfterMatchSkipMode after_match_skip_mode,
      ResolveAfterMatchSkipMode(
          ast_match_recognize_clause->after_match_skip_clause()));

  // MEASURES list
  ResolvedColumnList partitioning_columns;
  for (const NamedColumn& column : out_name_list->columns()) {
    partitioning_columns.push_back(column.column());
  }

  // Allocate the special columns representing the state, which could be
  // accessed in the MEASURES clause through the special functions such
  // MATCH_NUMBER(), MATCH_ROW_NUMBER() and CLASSIFIER().
  GOOGLESQL_RET_CHECK(!match_recognize_state_.has_value())
      << "MATCH_RECOGNIZE clause cannot be nested inside another "
         "MATCH_RECOGNIZE clause";

  googlesql_base::VarSetter<std::optional<MatchRecognizeState>> mr_state_setter(
      &match_recognize_state_,
      MatchRecognizeState{
          .match_number_column =
              ResolvedColumn(AllocateColumnId(), kMatchRecognizeId,
                             id_string_pool_->Make("$match_number"),
                             type_factory_->get_int64()),
          .match_row_number_column =
              ResolvedColumn(AllocateColumnId(), kMatchRecognizeId,
                             id_string_pool_->Make("$match_row_number"),
                             type_factory_->get_int64()),
          .classifier_column =
              ResolvedColumn(AllocateColumnId(), kMatchRecognizeId,
                             id_string_pool_->Make("$classifier"),
                             type_factory_->get_string()),
      });

  std::vector<std::unique_ptr<const ResolvedMeasureGroup>> resolved_agg_groups;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> resolved_measures;
  GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizeMeasures(
      ast_match_recognize_clause->measures(), input_name_list.get(),
      input_scope.get(), local_scope.get(), partitioning_columns,
      pattern_variables_defined, pattern_variable_targets,
      disallowed_access_targets, input_scan, *out_name_list,
      resolved_agg_groups, resolved_measures));

  const auto* alias = ast_match_recognize_clause->output_alias();
  if (alias != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(alias, alias->GetAsIdString(),
                                                std::move(out_name_list),
                                                output_name_list));
  } else {
    *output_name_list = std::move(out_name_list);
  }

  ResolvedColumnList inner_column_list = partitioning_columns;

  // Reserve an extra 3 columns for the special internal state columns.
  // Those columns may still be used in the projection on top of the MRScan,
  // for example, the minus operator in:
  //   MEASURES MATCH_NUMBER() - MATCH_ROW_NUMBER() AS x.
  inner_column_list.reserve(partitioning_columns.size() +
                            resolved_agg_groups.size() + 3);
  for (const auto& group : resolved_agg_groups) {
    for (const auto& agg : group->aggregate_list()) {
      GOOGLESQL_RET_CHECK(agg->Is<ResolvedComputedColumn>());
      inner_column_list.push_back(
          agg->GetAs<ResolvedComputedColumn>()->column());
    }
  }
  // Now add the special internal state columns. The final project scan will
  // hide them.
  inner_column_list.push_back(match_recognize_state_->match_number_column);
  inner_column_list.push_back(match_recognize_state_->match_row_number_column);
  inner_column_list.push_back(match_recognize_state_->classifier_column);

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto mr_scan,
      ResolvedMatchRecognizeScanBuilder()
          .set_option_list(std::move(options_list))
          .set_input_scan(std::move(input_scan))
          .set_column_list(std::move(inner_column_list))
          .set_analytic_function_group_list(
              analytic_scan_builder.release_function_group_list())
          .set_measure_group_list(std::move(resolved_agg_groups))
          .set_after_match_skip_mode(after_match_skip_mode)
          .set_pattern(std::move(resolved_pattern))
          .set_pattern_variable_definition_list(
              std::move(resolved_pattern_var_defs))
          .set_match_number_column(match_recognize_state_->match_number_column)
          .set_match_row_number_column(
              match_recognize_state_->match_row_number_column)
          .set_classifier_column(match_recognize_state_->classifier_column)
          .Build());

  // Add a project scan for post-aggregate computations and hide the internal
  // elementary aggregations. For example, `MEASURES MAX(a) - MIN(b) AS x` will
  // expose only 1 column, for the final `x`, and hide the internal columns for
  // the MAX and MIN aggregations.
  // Partitioning columns will appear here too (and in ALL ROWS PER MATCH,
  // ordering columns as well.)
  // TODO: we will need to reserve more columns for deferred evaluation
  ResolvedColumnList column_list = partitioning_columns;
  column_list.reserve(partitioning_columns.size() + resolved_measures.size());
  for (const auto& measure : resolved_measures) {
    column_list.push_back(measure->column());
  }

  // Partitioning columns are part of the output and affect SELECT *, etc,
  // so ensure they're never pruned to maintain a correct schema.
  // Similarly, the measures also make it to the output, just like a
  // SELECT list. Prevent pruning.
  RecordColumnAccess(column_list);

  GOOGLESQL_ASSIGN_OR_RETURN(*output, ResolvedProjectScanBuilder()
                                .set_input_scan(std::move(mr_scan))
                                .set_column_list(std::move(column_list))
                                .set_expr_list(std::move(resolved_measures))
                                .Build());

  return absl::OkStatus();
}

static bool ShouldResolveSuffixAsArrayScan(
    const LanguageOptions& options, std::unique_ptr<PathExpressionSpan>& suffix,
    const ASTPathExpression* path_expr) {
  return options.LanguageFeatureEnabled(FEATURE_SINGLE_TABLE_NAME_ARRAY_PATH) &&
         suffix != nullptr && suffix->num_names() > 1 &&
         suffix->num_names() <= path_expr->num_names();
}

// This is a self-contained table expression.  It can be an UNNEST, but
// only as a leaf - not one that has to wrap another scan and flatten it.
absl::Status Resolver::ResolveTablePathExpression(
    const ASTTablePathExpression* table_ref, const NameScope* external_scope,
    const NameScope* local_scope, std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ASTForSystemTime* for_system_time = table_ref->for_system_time();
  if (for_system_time != nullptr &&
      !language().LanguageFeatureEnabled(FEATURE_FOR_SYSTEM_TIME_AS_OF)) {
    return MakeSqlErrorAt(for_system_time)
           << "FOR SYSTEM_TIME AS OF is not supported";
  }

  if (ShouldResolveAsArrayScan(table_ref, local_scope)) {
    if (for_system_time != nullptr) {
      return MakeSqlErrorAt(for_system_time)
             << "FOR SYSTEM_TIME AS OF is not allowed with array scans";
    }
    if (!table_ref->postfix_operators().empty()) {
      const auto* first_op = table_ref->postfix_operators(0);
      return MakeSqlErrorAt(first_op)
             << first_op->Name() << " is not allowed with array scans";
    }

    // When <table_ref> contains explicit UNNEST, do not supply <path_expr>.
    // Otherwise pass in the full path expression.
    std::unique_ptr<const ResolvedScan> no_lhs_scan;
    std::optional<PathExpressionSpan> path_expr =
        table_ref->path_expr() != nullptr
            ? std::make_optional(PathExpressionSpan(*table_ref->path_expr()))
            : std::nullopt;
    return ResolveArrayScan(table_ref, path_expr,
                            /*on_clause=*/nullptr, /*using_clause=*/nullptr,
                            /*ast_join=*/nullptr, /*is_outer_scan=*/false,
                            /*include_lhs_name_list=*/false,
                            /*is_single_table_array_path=*/false,
                            /*resolved_input_scan=*/&no_lhs_scan,
                            /*name_list_input=*/nullptr, local_scope, output,
                            output_name_list);
  }

  if (table_ref->with_offset() != nullptr) {
    return MakeSqlErrorAt(table_ref)
           << "WITH OFFSET can only be used with array scans";
  }

  const ASTPathExpression* path_expr = table_ref->path_expr();
  GOOGLESQL_RET_CHECK(path_expr != nullptr);

  IdString alias;
  bool has_explicit_alias;
  const ASTNode* alias_location;
  if (table_ref->alias() != nullptr) {
    alias = table_ref->alias()->GetAsIdString();
    alias_location = table_ref->alias();
    has_explicit_alias = true;
  } else {
    alias = GetAliasForExpression(path_expr);
    alias_location = table_ref;
    has_explicit_alias = false;
  }
  GOOGLESQL_RET_CHECK(!alias.empty());

  std::shared_ptr<const NameList> name_list;
  std::unique_ptr<const ResolvedScan> this_scan;
  if (named_subquery_map_.contains(path_expr->ToIdStringVector())) {
    if (for_system_time != nullptr) {
      return MakeSqlErrorAt(for_system_time) << "FOR SYSTEM_TIME AS OF cannot "
                                                "be used with tables defined "
                                                "in WITH clause";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveNamedSubqueryRef(path_expr, table_ref->hint(),
                                            &this_scan, &name_list));

    if (name_list->is_value_table()) {
      auto new_name_list = std::make_shared<NameList>();
      GOOGLESQL_RETURN_IF_ERROR(ConvertValueTableNameListToNameListWithValueTable(
          table_ref, alias, name_list, &new_name_list));
      name_list = new_name_list;
    } else {
      // Add a range variable for the with_ref scan.
      GOOGLESQL_ASSIGN_OR_RETURN(name_list, NameList::AddRangeVariableInWrappingNameList(
                                      alias, alias_location, name_list));
    }
  } else if (path_expr->num_names() == 1 &&
             function_argument_info_ != nullptr &&
             function_argument_info_->FindTableArg(
                 path_expr->first_name()->GetAsIdString()) != nullptr) {
    if (for_system_time != nullptr) {
      return MakeSqlErrorAt(for_system_time)
             << "FOR SYSTEM_TIME AS OF cannot be used with TABLE parameter "
             << path_expr->first_name()->GetAsIdString() << " to FUNCTION";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsFunctionTableArgument(
        path_expr, table_ref->hint(), alias, alias_location, &this_scan,
        &name_list));
  } else {
    // The (possibly multi-part) table name did not match the WITH clause or a
    // table-valued argument (which only support single-part names), so try to
    // resolve this name as a Table from the Catalog.
    // If the prefix of the <path_expr> is resolved as the table name,
    // <suffix> is the suffix part of the names that start with the
    // last identifier in the table name.
    // For example, if we found table name "catalog.table" in <path_expr>
    // "catalog.table.array", then <suffix> will be set to "table.array".
    std::unique_ptr<const ResolvedTableScan> table_scan;
    std::unique_ptr<PathExpressionSpan> suffix;
    std::unique_ptr<PathExpressionSpan>* remaining_names =
        language().LanguageFeatureEnabled(FEATURE_SINGLE_TABLE_NAME_ARRAY_PATH)
            ? &suffix
            : nullptr;

    GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsTableScan(
        path_expr, alias, has_explicit_alias, alias_location, table_ref->hint(),
        for_system_time, local_scope,
        /*read_as_row_type_error_kind=*/"", remaining_names, &table_scan,
        &name_list,
        /*output_column_name_list=*/nullptr,
        resolved_columns_from_table_scans_));
    this_scan = std::move(table_scan);

    if (ShouldResolveSuffixAsArrayScan(language(), suffix, path_expr)) {
      std::unique_ptr<const NameScope> rhs_scope =
          std::make_unique<NameScope>(*name_list);
      const NameScope* rhs_array_scan_scope = rhs_scope.get();
      auto empty_name_list = std::make_shared<NameList>();
      return ResolveArrayScan(table_ref, std::make_optional(*suffix),
                              /*on_clause=*/nullptr, /*using_clause=*/nullptr,
                              /*ast_join=*/nullptr, /*is_outer_scan=*/false,
                              /*include_lhs_name_list=*/false,
                              /*is_single_table_array_path=*/true, &this_scan,
                              empty_name_list, rhs_array_scan_scope, output,
                              output_name_list);
    }
  }
  GOOGLESQL_RET_CHECK(this_scan != nullptr);
  GOOGLESQL_RET_CHECK(name_list != nullptr);

  for (const auto* op : table_ref->postfix_operators()) {
    switch (op->node_kind()) {
      case AST_PIVOT_CLAUSE:
        if (for_system_time != nullptr) {
          GOOGLESQL_RET_CHECK_EQ(for_system_time, nullptr)
              << "Parser should not allow PIVOT and FOR SYSTEM TIME AS OF to "
                 "coexist";
        }
        GOOGLESQL_RETURN_IF_ERROR(ResolvePivotClause(
            std::move(this_scan), name_list, external_scope,
            /*input_is_subquery=*/false, op->GetAsOrDie<ASTPivotClause>(),
            &this_scan, &name_list));
        break;
      case AST_UNPIVOT_CLAUSE:
        GOOGLESQL_RET_CHECK_EQ(for_system_time, nullptr)
            << "Parser should not allow UNPIVOT and FOR SYSTEM TIME AS OF to "
               "coexist";
        GOOGLESQL_RETURN_IF_ERROR(ResolveUnpivotClause(
            std::move(this_scan), name_list, external_scope,
            op->GetAsOrDie<ASTUnpivotClause>(), &this_scan, &name_list));
        break;
      case AST_SAMPLE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveTablesampleClause(
            op->GetAsOrDie<ASTSampleClause>(), &name_list, &this_scan));
        break;
      case AST_MATCH_RECOGNIZE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizeClause(
            std::move(this_scan), name_list, external_scope,
            op->GetAsOrDie<ASTMatchRecognizeClause>(), &this_scan, &name_list));
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported postfix operator: " << op->node_kind();
    }
  }

  *output_name_list = name_list;
  *output = std::move(this_scan);
  return absl::OkStatus();
}

absl::Status Resolver::ResolvePathExpressionAsFunctionTableArgument(
    const ASTPathExpression* path_expr, const ASTHint* hint, IdString alias,
    const ASTNode* ast_location, std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  std::shared_ptr<const NameList> name_list;
  std::unique_ptr<const ResolvedScan> this_scan;
  GOOGLESQL_RET_CHECK_NE(function_argument_info_, nullptr);
  // The path refers to a relation argument in a CREATE TABLE FUNCTION
  // statement. Create and return a resolved relation argument reference.
  const FunctionArgumentInfo::ArgumentDetails* details =
      function_argument_info_->FindTableArg(
          path_expr->first_name()->GetAsIdString());
  GOOGLESQL_RET_CHECK_NE(details, nullptr);
  // We do not expect to analyze a template function body until the function is
  // invoked and we have concrete argument types. If a template type is found
  // that could mean engine code is using an API incorrectly.
  GOOGLESQL_RET_CHECK(!details->arg_type.IsTemplated())
      << "Function bodies cannot be resolved with templated argument types";
  const TVFRelation& tvf_relation =
      details->arg_type.options().relation_input_schema();
  std::unique_ptr<NameList> new_name_list = std::make_unique<NameList>();
  std::vector<ResolvedColumn> resolved_columns;
  if (tvf_relation.is_value_table()) {
    resolved_columns.reserve(tvf_relation.num_columns());
    resolved_columns.push_back(ResolvedColumn(
        AllocateColumnId(), path_expr->first_name()->GetAsIdString(),
        kValueColumnId, tvf_relation.column(0).type));
    std::shared_ptr<NameList> pseudo_column_name_list(new NameList);
    for (int i = 1; i < tvf_relation.num_columns(); ++i) {
      const TVFRelation::Column& column = tvf_relation.column(i);
      GOOGLESQL_RET_CHECK(column.is_pseudo_column);
      resolved_columns.push_back(ResolvedColumn(
          AllocateColumnId(),
          id_string_pool_->Make(path_expr->first_name()->GetAsString()),
          id_string_pool_->Make(column.name), column.type));
      // Add to name list dedicated to pseudo-columns so they
      // can be accessed in a qualified manner: SELECT t.pseudo_column
      GOOGLESQL_RETURN_IF_ERROR(pseudo_column_name_list->AddPseudoColumn(
          resolved_columns.back().name_id(), resolved_columns.back(),
          path_expr));
      // Add pseudo-column to top level name list so they can be accessed in
      // an unqualified manner: SELECT pseudo_column
      GOOGLESQL_RETURN_IF_ERROR(
          new_name_list->AddPseudoColumn(resolved_columns.back().name_id(),
                                         resolved_columns.back(), path_expr));
    }
    GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddValueTableColumn(
        alias, resolved_columns[0], path_expr, /*excluded_field_names=*/{},
        pseudo_column_name_list));
    GOOGLESQL_RETURN_IF_ERROR(new_name_list->SetIsValueTable());
    name_list = std::move(new_name_list);
  } else {
    resolved_columns.reserve(tvf_relation.num_columns());
    for (const TVFRelation::Column& column : tvf_relation.columns()) {
      resolved_columns.push_back(ResolvedColumn(
          AllocateColumnId(),
          id_string_pool_->Make(path_expr->first_name()->GetAsString()),
          id_string_pool_->Make(column.name), column.type));
      if (column.is_pseudo_column) {
        GOOGLESQL_RETURN_IF_ERROR(
            new_name_list->AddPseudoColumn(resolved_columns.back().name_id(),
                                           resolved_columns.back(), path_expr));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddColumn(
            resolved_columns.back().name_id(), resolved_columns.back(),
            /*is_explicit=*/true));
      }
    }
    name_list = std::move(new_name_list);
    // Add a range variable for the TVF relation argument scan.
    GOOGLESQL_ASSIGN_OR_RETURN(name_list, NameList::AddRangeVariableInWrappingNameList(
                                    alias, ast_location, name_list));
  }
  auto relation_argument_scan = MakeResolvedRelationArgumentScan(
      resolved_columns, path_expr->first_name()->GetAsString(),
      tvf_relation.is_value_table());
  GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
      name_list, path_expr, "RELATION ARGUMENT", product_mode()));
  if (hint != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(hint, relation_argument_scan.get()));
  }
  MaybeRecordParseLocation(path_expr, relation_argument_scan.get());
  this_scan = std::move(relation_argument_scan);

  *output_name_list = name_list;
  *output = std::move(this_scan);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveTableSubquery(
    const ASTTableSubquery* table_ref, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  // Table subqueries can always see correlated names from outer queries, but
  // can have correlated references to tables earlier in the same FROM clause
  // only when LATERAL.

  IdString alias;
  if (table_ref->alias() != nullptr) {
    alias = table_ref->alias()->GetAsIdString();
  } else {
    alias = AllocateSubqueryName();
  }
  GOOGLESQL_RET_CHECK(!alias.empty());

  std::unique_ptr<const ResolvedScan> resolved_subquery;
  std::shared_ptr<const NameList> subquery_name_list;
  GOOGLESQL_RETURN_IF_ERROR(ResolveQuery(table_ref->subquery(), scope, alias,
                               &resolved_subquery, &subquery_name_list));
  GOOGLESQL_RET_CHECK(nullptr != subquery_name_list);

  // A table subquery never preserves order, so we clear is_ordered on the
  // final scan of the subquery, even if it was a ResolvedOrderByScan.
  const_cast<ResolvedScan*>(resolved_subquery.get())->set_is_ordered(false);

  if (table_ref->alias() == nullptr) {
    // For a table subquery without an alias, the NameList from the subquery
    // should propagate unchanged, including range variables, pseudo-columns
    // and is_value_table.
    if (subquery_name_list->is_value_table() &&
        !subquery_name_list->HasValueTableColumns()) {
      // This handles the case where the input is marked with is_value_table,
      // but it doesn't have a value table column.  This happens for UNION
      // of value tables, for one example.  Calling this function
      // builds a NameList containing a value table column.
      // TODO This should be fixed by setting up the value table
      // NameList properly in the first place, rather than fixing it when
      // handling a table subquery.  If we have a pipe operator after this
      // input table without a table subquery, the value table won't work.
      // e.g. The test with a parenthesized UNION ALL producing a value
      // table exposes this problem.
      auto new_name_list = std::make_shared<NameList>();
      GOOGLESQL_RETURN_IF_ERROR(ConvertValueTableNameListToNameListWithValueTable(
          table_ref, alias, subquery_name_list, &new_name_list));
      *output_name_list = new_name_list;
    } else {
      *output_name_list = subquery_name_list;
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(
        table_ref, alias, subquery_name_list, output_name_list));
  }

  for (const auto* op : table_ref->postfix_operators()) {
    switch (op->node_kind()) {
      case AST_PIVOT_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePivotClause(
            std::move(resolved_subquery), *output_name_list, scope,
            /*input_is_subquery=*/true, op->GetAsOrDie<ASTPivotClause>(),
            &resolved_subquery, output_name_list));
        break;
      case AST_UNPIVOT_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveUnpivotClause(
            std::move(resolved_subquery), *output_name_list, scope,
            op->GetAsOrDie<ASTUnpivotClause>(), &resolved_subquery,
            output_name_list));
        break;
      case AST_SAMPLE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(
            ResolveTablesampleClause(op->GetAsOrDie<ASTSampleClause>(),
                                     output_name_list, &resolved_subquery));
        break;
      case AST_MATCH_RECOGNIZE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizeClause(
            std::move(resolved_subquery), *output_name_list, scope,
            op->GetAsOrDie<ASTMatchRecognizeClause>(), &resolved_subquery,
            output_name_list));
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported postfix operator: " << op->node_kind();
    }
  }

  *output = std::move(resolved_subquery);
  return absl::OkStatus();
}

// Validates the parameter to PERCENT. It can either be a literal or a
// parameter. In either case, its type must be a double or an int64.
static absl::Status CheckPercentIsValid(
    const ASTNode* ast_location,
    const std::unique_ptr<const ResolvedExpr>& expr) {
  if ((expr->node_kind() != RESOLVED_PARAMETER &&
       expr->node_kind() != RESOLVED_LITERAL) ||
      (!expr->type()->IsInt64() && !expr->type()->IsDouble())) {
    return MakeSqlErrorAt(ast_location)
           << "PERCENT expects either a double or an integer literal or "
              "parameter";
  }

  if (expr->node_kind() == RESOLVED_LITERAL) {
    // If a literal, we can also validate its value.
    const Value value = expr->GetAs<ResolvedLiteral>()->value();
    bool is_valid = false;
    if (value.type()->IsInt64()) {
      is_valid = (!value.is_null() && value.int64_value() >= 0 &&
                  value.int64_value() <= 100);
    } else {
      GOOGLESQL_RET_CHECK(value.type()->IsDouble());
      is_valid = (!value.is_null() && value.double_value() >= 0.0 &&
                  value.double_value() <= 100.0);
    }
    if (!is_valid) {
      return MakeSqlErrorAt(ast_location)
             << "PERCENT value must be in the range [0, 100]";
    }
  }
  return absl::OkStatus();
}

// Note that this takes a NameList rather than a NameScope.  That means it
// won't be able to resolve correlated references.  The only places names
// are resolved is in PARTITION BY, which is somewhat obscure.
// This was probably an accidental omission, but seems unlikely to show up much.
absl::Status Resolver::ResolveTablesampleClause(
    const ASTSampleClause* sample_clause,
    std::shared_ptr<const NameList>* current_name_list,
    std::unique_ptr<const ResolvedScan>* current_scan) {
  if (!language().LanguageFeatureEnabled(FEATURE_TABLESAMPLE)) {
    return MakeSqlErrorAt(sample_clause) << "TABLESAMPLE not supported";
  }

  GOOGLESQL_RET_CHECK(sample_clause->sample_method() != nullptr);
  const ASTIdentifier* method = sample_clause->sample_method();

  GOOGLESQL_RET_CHECK(sample_clause->sample_size() != nullptr);
  std::unique_ptr<const ResolvedExpr> resolved_size;
  const ASTExpression* size = sample_clause->sample_size()->size();
  static constexpr char kTablesampleClause[] = "TABLESAMPLE clause";
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(size, empty_name_scope_.get(),
                                    kTablesampleClause, &resolved_size));

  const int unit = sample_clause->sample_size()->unit();
  GOOGLESQL_RET_CHECK(unit == ASTSampleSize::ROWS || unit == ASTSampleSize::PERCENT)
      << unit;
  const ResolvedSampleScan::SampleUnit resolved_unit =
      (unit == ASTSampleSize::ROWS) ? ResolvedSampleScan::ROWS
                                    : ResolvedSampleScan::PERCENT;
  if (resolved_unit == ResolvedSampleScan::ROWS) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
        /*clause_name=*/"ROWS", size, &resolved_size));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(CheckPercentIsValid(size, resolved_size));
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> partition_by_list;
  const NameScope name_scope(**current_name_list);
  auto query_info = std::make_unique<QueryResolutionInfo>(this);
  if (sample_clause->sample_size()->partition_by() != nullptr) {
    if (!language().LanguageFeatureEnabled(
            FEATURE_STRATIFIED_RESERVOIR_TABLESAMPLE)) {
      return MakeSqlErrorAt(sample_clause->sample_size()->partition_by())
             << "TABLESAMPLE does not support the PARTITION BY clause";
    }
    const std::string method = sample_clause->sample_method()->GetAsString();
    if (!googlesql_base::CaseEqual(method, "reservoir")) {
      return MakeSqlErrorAt(sample_clause->sample_size()->partition_by())
             << "The TABLESAMPLE " << method
             << " method does not support PARTITION BY. "
             << "Remove PARTITION BY, or use the TABLESAMPLE RESERVOIR method";
    }
    if (resolved_unit != ResolvedSampleScan::ROWS) {
      return MakeSqlErrorAt(sample_clause->sample_size()->partition_by())
             << "TABLESAMPLE with PERCENT does not support PARTITION BY. "
             << "Remove PARTITION BY, or use TABLESAMPLE RESERVOIR with ROWS";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveCreateTablePartitionByList(
        sample_clause->sample_size()
            ->partition_by()
            ->partitioning_expressions(),
        PartitioningKind::PARTITION_BY, name_scope, query_info.get(),
        &partition_by_list));
  }

  std::unique_ptr<const ResolvedExpr> resolved_repeatable_argument;
  if (sample_clause->sample_suffix() != nullptr &&
      sample_clause->sample_suffix()->repeat() != nullptr) {
    GOOGLESQL_RET_CHECK(sample_clause->sample_suffix()->repeat()->argument() != nullptr);
    const ASTExpression* repeatable_argument =
        sample_clause->sample_suffix()->repeat()->argument();
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveScalarExpr(repeatable_argument, empty_name_scope_.get(),
                          kTablesampleClause, &resolved_repeatable_argument));
    GOOGLESQL_RETURN_IF_ERROR(ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
        /*clause_name=*/"REPEATABLE", repeatable_argument,
        &resolved_repeatable_argument));
  }

  // Resolve WITH WEIGHT if present.
  std::unique_ptr<ResolvedColumnHolder> weight_column;
  ResolvedColumnList output_column_list = (*current_scan)->column_list();
  if (sample_clause->sample_suffix() != nullptr &&
      sample_clause->sample_suffix()->weight() != nullptr) {
    // If the alias is NULL, we get "weight" as an implicit alias.
    const ASTAlias* with_weight_alias =
        sample_clause->sample_suffix()->weight()->alias();
    const IdString weight_alias =
        (with_weight_alias == nullptr ? kWeightAlias
                                      : with_weight_alias->GetAsIdString());

    const ResolvedColumn column(AllocateColumnId(),
                                /*table_name=*/kWeightId, /*name=*/weight_alias,
                                type_factory_->get_double());
    weight_column = MakeResolvedColumnHolder(column);
    output_column_list.push_back(weight_column->column());
    std::shared_ptr<NameList> name_list = (*current_name_list)->Copy();
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddValueTableColumn(
        weight_alias, weight_column->column(),
        with_weight_alias != nullptr
            ? absl::implicit_cast<const ASTNode*>(with_weight_alias)
            : sample_clause->sample_suffix()->weight()));
    *current_name_list = name_list;
  }

  *current_scan = MakeResolvedSampleScan(
      output_column_list, std::move(*current_scan),
      absl::AsciiStrToLower(method->GetAsString()), std::move(resolved_size),
      resolved_unit, std::move(resolved_repeatable_argument),
      std::move(weight_column), std::move(partition_by_list));
  return absl::OkStatus();
}

// There's not much resolving left to do here.  We already know we have
// an identifier that matches a WITH subquery alias, so we build the
// ResolvedWithRefScan.
absl::Status Resolver::ResolveNamedSubqueryRef(
    const ASTPathExpression* table_path, const ASTHint* hint,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  GOOGLESQL_RET_CHECK(table_path != nullptr);
  auto it = named_subquery_map_.find(table_path->ToIdStringVector());
  GOOGLESQL_RET_CHECK(it != named_subquery_map_.end() && !it->second.empty());
  const NamedSubquery* named_subquery = it->second.back().get();
  return ResolveNamedSubqueryRef(table_path, table_path->ToIdStringVector(),
                                 named_subquery, hint, output,
                                 output_name_list);
}

absl::Status Resolver::ResolveNamedSubqueryRef(
    const ASTNode* ast_location, const std::vector<IdString>& table_path,
    const NamedSubquery* named_subquery, const ASTHint* hint,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  if (named_subquery == nullptr) {
    // This is possible only if we have a recursive reference from within the
    // non-recursive term of a recursive UNION. This is an error.
    return MakeSqlErrorAt(ast_location)
           << "Recursive reference is not allowed in non-recursive UNION term";
  }

  GOOGLESQL_RETURN_IF_ERROR(named_subquery->access_error_message)
      .With(LocationOverride(ast_location));

  // For each new column produced in the WithRefScan, we want to name it
  // using the WITH alias, not the original column name.  e.g. In
  //   WITH Q AS (SELECT Key K FROM KeyValue)
  //   SELECT * FROM Q;
  // we want to call the new column Q.K, not Q.Key.  Since the column_list
  // may not map 1:1 with select-list column names, we need to build a map.
  std::map<ResolvedColumn, IdString> with_column_to_alias;
  for (const NamedColumn& named_column : named_subquery->name_list->columns()) {
    googlesql_base::InsertIfNotPresent(&with_column_to_alias, named_column.column(),
                            named_column.name());
  }

  // Make a new ResolvedColumn for each column from the WITH scan.
  // This is necessary so that if the WITH subquery is referenced twice,
  // we get distinct column names for each scan.
  ResolvedColumnList column_list;
  std::map<ResolvedColumn, ResolvedColumn> old_column_to_new_column;
  for (int i = 0; i < named_subquery->column_list.size(); ++i) {
    const ResolvedColumn& column = named_subquery->column_list[i];

    // Get the alias for the column produced by the WITH reference,
    // using the first alias for that column in the WITH subquery.
    // Every column in the column_list should correspond to at least column
    // in the WITH subquery's NameList.
    IdString new_column_alias;
    const IdString* found = googlesql_base::FindOrNull(with_column_to_alias, column);
    GOOGLESQL_RET_CHECK(found != nullptr) << column.DebugString();
    new_column_alias = *found;

    column_list.emplace_back(
        ResolvedColumn(AllocateColumnId(), named_subquery->unique_alias,
                       new_column_alias, column.annotated_type()));
    // Build mapping from WITH subquery column to the newly created column
    // for the WITH reference.
    old_column_to_new_column[column] = column_list.back();
    // We can't prune any columns from the ResolvedWithRefScan because they
    // need to match 1:1 with the column_list on the with subquery.
    RecordColumnAccess(column_list.back());
  }

  // Make a new NameList pointing at the new ResolvedColumns.
  std::shared_ptr<NameList> name_list(new NameList);
  for (const NamedColumn& named_column : named_subquery->name_list->columns()) {
    const ResolvedColumn& old_column = named_column.column();
    auto found_column = old_column_to_new_column.find(old_column);
    GOOGLESQL_RET_CHECK(found_column != old_column_to_new_column.end());
    const ResolvedColumn& new_column = found_column->second;

    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumnMaybeValueTable(
        named_column.name(), new_column, named_column.is_explicit(),
        ast_location, named_subquery->name_list->is_value_table()));
  }
  if (named_subquery->name_list->is_value_table()) {
    GOOGLESQL_RETURN_IF_ERROR(name_list->SetIsValueTable());
  }

  std::unique_ptr<ResolvedScan> scan;
  if (named_subquery->is_recursive) {
    std::unique_ptr<ResolvedRecursiveRefScan> recursive_scan =
        MakeResolvedRecursiveRefScan(column_list);

    // Remember information about the recursive scan, which will be needed for
    // downstream validation checks.
    recursive_ref_info_[recursive_scan.get()] = RecursiveRefScanInfo{
        ast_location,
        table_path,
        named_subquery->unique_alias,
    };
    scan = std::move(recursive_scan);
  } else {
    scan = MakeResolvedWithRefScan(column_list,
                                   named_subquery->unique_alias.ToString());
    MaybeRecordParseLocation(ast_location, scan.get());
  }
  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(hint, scan.get()));

  *output = std::move(scan);
  *output_name_list = name_list;
  return absl::OkStatus();
}

absl::Status Resolver::ResolveColumnInUsing(
    const ASTIdentifier* ast_identifier, const NameList& name_list,
    absl::string_view side_name, IdString key_name,
    ResolvedColumn* found_column,
    std::unique_ptr<const ResolvedExpr>* compute_expr_for_found_column) {
  compute_expr_for_found_column->reset();
  // <ast_identifier> and <found_column> are redundant but we pass the
  // string in to avoid doing extra string copy.
  ABSL_DCHECK_EQ(ast_identifier->GetAsIdString(), key_name);

  NameTarget found_name;
  if (!name_list.LookupName(key_name, &found_name)) {
    return MakeSqlErrorAt(ast_identifier)
           << "Column " << key_name << " in USING clause not found on "
           << side_name << " side of join";
  }
  if (in_strict_mode() && found_name.IsImplicit()) {
    return MakeSqlErrorAt(ast_identifier)
           << "Column name " << ToIdentifierLiteral(key_name)
           << " cannot be used without a qualifier in strict name resolution"
           << " mode. Use JOIN ON with a qualified name instead";
  }
  switch (found_name.kind()) {
    case NameTarget::ACCESS_ERROR:
      // An ACCESS_ERROR should not be possible in this context, since
      // ACCESS_ERROR NameTargets only exist during post-GROUP BY processing,
      // and the USING clause is evaluated pre-GROUP BY.
      GOOGLESQL_RET_CHECK_FAIL() << "Accessing column " << key_name
                       << " in USING clause is invalid";
    case NameTarget::AMBIGUOUS:
      return MakeSqlErrorAt(ast_identifier)
             << "Column " << key_name << " in USING clause is ambiguous on "
             << side_name << " side of join";
    case NameTarget::RANGE_VARIABLE:
      GOOGLESQL_RET_CHECK(!found_name.is_pattern_variable())
          << "Pattern variable in USING clause";
      if (found_name.scan_columns()->is_value_table()) {
        GOOGLESQL_RET_CHECK_EQ(found_name.scan_columns()->num_columns(), 1);
        *found_column = found_name.scan_columns()->column(0).column();
        break;
      } else {
        return MakeSqlErrorAt(ast_identifier)
               << "Name " << key_name
               << " in USING clause is a table alias, not a column name, on "
               << side_name << " side of join";
      }
    case NameTarget::FIELD_OF: {
      // We have an implicit field access.  Make the ResolvedColumnRef for
      // the column and then resolve the field access on top.
      std::unique_ptr<const ResolvedExpr> resolved_get_field;
      // We don't auto-flatten for USING. It could make for matches that look
      // the same but come from different structure which would be weird.
      GOOGLESQL_RETURN_IF_ERROR(ResolveFieldAccess(
          MakeColumnRef(found_name.column_containing_field()),
          ast_identifier->location(), ast_identifier,
          /*flatten_state=*/nullptr, &resolved_get_field));

      // Then create a new ResolvedColumn to store this result.
      *found_column = ResolvedColumn(
          AllocateColumnId(), MakeIdString(absl::StrCat("$join_", side_name)),
          key_name, resolved_get_field->annotated_type());

      *compute_expr_for_found_column = std::move(resolved_get_field);
      break;
    }
    case NameTarget::IMPLICIT_COLUMN:
    case NameTarget::EXPLICIT_COLUMN:
      *found_column = found_name.column();
      break;
  }
  return absl::OkStatus();
}

// static
absl::Status Resolver::MaybeAddJoinHintKeyword(const ASTJoin* ast_join,
                                               ResolvedScan* resolved_scan) {
  if (ast_join->join_hint() != ASTJoin::NO_JOIN_HINT) {
    // Convert HASH and LOOKUP to HASH_JOIN and LOOKUP_JOIN, respectively.
    absl::string_view hint;
    switch (ast_join->join_hint()) {
      case ASTJoin::HASH:
        hint = "HASH_JOIN";
        break;
      case ASTJoin::LOOKUP:
        hint = "LOOKUP_JOIN";
        break;
      case ASTJoin::NO_JOIN_HINT:
        GOOGLESQL_RET_CHECK_FAIL() << "Can't get here";
    }
    // Join hint keywords don't directly correspond to query text so we don't
    // record the parse location.
    resolved_scan->add_hint_list(MakeResolvedOption(
        /*qualifier=*/"", "join_type",
        MakeResolvedLiteralWithoutLocation(Value::String(hint))));
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveUsing(
    const ASTUsingClause* using_clause, const NameList& name_list_lhs,
    const NameList& name_list_rhs, const ResolvedJoinScan::JoinType join_type,
    bool is_array_scan,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        lhs_computed_columns,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        rhs_computed_columns,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        computed_columns,
    NameList* output_name_list,
    std::unique_ptr<const ResolvedExpr>* join_condition) {
  GOOGLESQL_RET_CHECK(using_clause != nullptr);
  GOOGLESQL_RET_CHECK(computed_columns != nullptr);
  GOOGLESQL_RET_CHECK(output_name_list != nullptr);
  GOOGLESQL_RET_CHECK(join_condition != nullptr);

  IdStringSetCase column_names_emitted_by_using;

  std::vector<std::unique_ptr<const ResolvedExpr>> join_key_exprs;

  for (const ASTIdentifier* using_key : using_clause->keys()) {
    const IdString key_name = using_key->GetAsIdString();

    ResolvedColumn lhs_column, rhs_column;
    std::unique_ptr<const ResolvedExpr> lhs_compute_expr, rhs_compute_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveColumnInUsing(using_key, name_list_lhs, "left",
                                         key_name, &lhs_column,
                                         &lhs_compute_expr));
    // NOTE: For ArrayScan, there is no rhs_scan we can push a Project into, so
    // we'll never materialize rhs_column. Instead, we'll use rhs_compute_expr
    // directly.
    GOOGLESQL_RETURN_IF_ERROR(ResolveColumnInUsing(using_key, name_list_rhs, "right",
                                         key_name, &rhs_column,
                                         &rhs_compute_expr));

    std::unique_ptr<const ResolvedExpr> lhs_expr = MakeColumnRef(lhs_column);
    std::unique_ptr<const ResolvedExpr> rhs_expr =
        is_array_scan && rhs_compute_expr != nullptr
            ? std::move(rhs_compute_expr)
            : MakeColumnRef(rhs_column);

    std::unique_ptr<const ResolvedExpr> join_key_expr;
    absl::Status status = MakeEqualityComparison(
        using_key, std::move(lhs_expr), std::move(rhs_expr), &join_key_expr);
    if (absl::IsInvalidArgument(status)) {
      // We assume there are only two major reasons when INVALID_ARGUMENT is
      // returned by MakeEqualityComparison:
      // 1) The column in USING from the table on both sides of join has the
      // same type, which does not support equality comparison;
      // 2) The column in USING from the table on both sides of join has
      // different types and are incompatible.
      // In particular, looking up catalog for equality operator should never
      // return INVALID_ARGUMENT.
      if (lhs_column.type()->Equivalent(rhs_column.type())) {
        return MakeSqlErrorAt(using_key)
               << "Column '" << ToIdentifierLiteral(key_name)
               << "' in USING has types that do not support equality "
                  "comparison: "
               << lhs_column.type()->ShortTypeName(product_mode());
      }
      return MakeSqlErrorAt(using_key)
             << "Column '" << ToIdentifierLiteral(key_name)
             << "' in USING has incompatible types that cannot be directly "
                "compared using equality on either side of the join: "
             << lhs_column.type()->ShortTypeName(product_mode()) << " and "
             << rhs_column.type()->ShortTypeName(product_mode());
    }
    // Propagate all other errors.
    GOOGLESQL_RETURN_IF_ERROR(status);
    join_key_exprs.push_back(std::move(join_key_expr));

    // The column name from inside USING should be visible as a column
    // exactly once, from the non-NULL side of the join.
    // As per specification, the output for the using column is:
    // 1) The lhs for INNER or LEFT JOIN
    // 2) The rhs for RIGHT JOIN
    // 3) A COALESCE(lhs, rhs) expression for FULL JOIN (whose result
    //    type is the supertype of lhs/rhs)
    if (googlesql_base::InsertIfNotPresent(&column_names_emitted_by_using, key_name)) {
      const IdString key_name = using_key->GetAsIdString();
      switch (join_type) {
        case ResolvedJoinScan::LEFT:
        case ResolvedJoinScan::INNER:
          // is_explicit=true because we always have a provided alias in
          // JOIN USING.
          GOOGLESQL_RETURN_IF_ERROR(output_name_list->AddColumn(key_name, lhs_column,
                                                      /*is_explicit=*/true));
          break;
        case ResolvedJoinScan::RIGHT:
          GOOGLESQL_RETURN_IF_ERROR(output_name_list->AddColumn(key_name, rhs_column,
                                                      /*is_explicit=*/true));
          break;
        case ResolvedJoinScan::FULL: {
          std::unique_ptr<const ResolvedExpr> coalesce_expr;
          GOOGLESQL_RETURN_IF_ERROR(MakeCoalesceExpr(using_key, {lhs_column, rhs_column},
                                           &coalesce_expr));
          const ResolvedColumn coalesce_column(AllocateColumnId(), kFullJoinId,
                                               key_name,
                                               coalesce_expr->annotated_type());
          computed_columns->push_back(MakeResolvedComputedColumn(
              coalesce_column, std::move(coalesce_expr)));
          GOOGLESQL_RETURN_IF_ERROR(output_name_list->AddColumn(key_name, coalesce_column,
                                                      /*is_explicit=*/true));
          // Mark the <coalesce_column> as referenced so that it does not get
          // pruned if column pruning is enabled in the AnalyzerOptions.
          // Pruning this column causes problems for the SQLBuilder and other
          // visitors that do not visit expressions computed in a ResolvedNode
          // unless that node actually projects the expressions.
          // TODO: Consider enhancing column pruning, so that when
          // pruning a computed column we determine if we can also prune the
          // related expression.
          RecordColumnAccess(coalesce_column);
          break;
        }
      }
    }

    if (lhs_compute_expr != nullptr) {
      lhs_computed_columns->push_back(
          MakeResolvedComputedColumn(lhs_column, std::move(lhs_compute_expr)));
    }
    if (rhs_compute_expr != nullptr) {
      rhs_computed_columns->push_back(
          MakeResolvedComputedColumn(rhs_column, std::move(rhs_compute_expr)));
    }
  }

  NameList::MergeOptions merge_options{.excluded_field_names =
                                           &column_names_emitted_by_using};

  GOOGLESQL_RETURN_IF_ERROR(
      output_name_list->MergeFrom(name_list_lhs, using_clause, merge_options));
  GOOGLESQL_RETURN_IF_ERROR(
      output_name_list->MergeFrom(name_list_rhs, using_clause, merge_options));

  return MakeAndExpr(using_clause, std::move(join_key_exprs), join_condition);
}

// Inside this method, `external_scope` is the scope including names that
// are visible coming from outside the join.
// We build additional NameScopes inside the method where we need to resolve
// expressions using names that may come from the lhs or rhs of this join.
absl::Status Resolver::ResolveJoin(
    const ASTJoin* join, const NameScope* external_scope,
    const NameScope* local_scope, bool is_leftmost,
    bool on_rhs_of_right_or_full_join,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::unique_ptr<const ResolvedScan> resolved_lhs;
  std::shared_ptr<const NameList> name_list_lhs;

  GOOGLESQL_RETURN_IF_ERROR(ResolveTableExpression(
      join->lhs(), external_scope, local_scope, is_leftmost,
      on_rhs_of_right_or_full_join, &resolved_lhs, &name_list_lhs));

  auto scope_for_lhs_storage = std::make_unique<NameScope>(*name_list_lhs);
  const NameScope* scope_for_lhs = scope_for_lhs_storage.get();

  GOOGLESQL_RETURN_IF_ERROR(ResolveJoinRhs(join, external_scope, scope_for_lhs,
                                 name_list_lhs, std::move(resolved_lhs),
                                 on_rhs_of_right_or_full_join, output,
                                 output_name_list));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveJoinRhs(
    const ASTJoin* join, const NameScope* external_scope,
    const NameScope* scope_for_lhs,
    const std::shared_ptr<const NameList>& name_list_lhs,
    std::unique_ptr<const ResolvedScan> resolved_lhs,
    bool on_rhs_of_right_or_full_join,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  bool disable_outer_join_array =
      language().LanguageFeatureEnabled(FEATURE_DISABLE_OUTER_JOIN_ARRAY);
  bool rhs_is_table_path_expr =
      join->rhs()->node_kind() == AST_TABLE_PATH_EXPRESSION;
  bool is_right_or_full_join =
      join->join_type() == ASTJoin::RIGHT || join->join_type() == ASTJoin::FULL;

  on_rhs_of_right_or_full_join =
      on_rhs_of_right_or_full_join || is_right_or_full_join;

  bool is_lateral = IsLateral(join->rhs());

  bool rhs_is_unnest_expr = false;
  bool is_correlated_array_join = false;
  bool rhs_is_correlated_array_in_subquery = false;
  std::string error_label;

  if (rhs_is_table_path_expr) {
    const ASTTablePathExpression* table_ref =
        join->rhs()->GetAsOrDie<ASTTablePathExpression>();
    const ASTPathExpression* rhs_path_expr = table_ref->path_expr();
    rhs_is_unnest_expr = rhs_path_expr == nullptr;
    bool rhs_is_dotted_path_expr =
        rhs_path_expr != nullptr && rhs_path_expr->num_names() > 1;
    is_correlated_array_join =
        rhs_is_dotted_path_expr &&
        IsPathExpressionStartingFromScope(*rhs_path_expr, *scope_for_lhs);
    rhs_is_correlated_array_in_subquery =
        rhs_is_dotted_path_expr &&
        IsPathExpressionStartingFromScope(*rhs_path_expr, *external_scope);

    if (rhs_is_unnest_expr) {
      error_label = "UNNEST expression";
    } else {
      error_label = rhs_path_expr->ToIdentifierPathString();
    }

    // TODO: Update the error message after the rollout of
    // outer join independent array is done.
    // Showing legacy error message for backward compatibility.
    if (disable_outer_join_array &&
        (rhs_is_unnest_expr || is_correlated_array_join ||
         rhs_is_correlated_array_in_subquery)) {
      if (join->join_type() == ASTJoin::RIGHT) {
        return MakeSqlErrorAt(join->rhs())
               << "Array scan is not allowed with RIGHT JOIN: " << error_label;
      } else if (join->join_type() == ASTJoin::FULL) {
        return MakeSqlErrorAt(join->rhs())
               << "Array scan is not allowed with FULL JOIN: " << error_label;
      }
      if (table_ref->for_system_time() != nullptr) {
        return MakeSqlErrorAt(table_ref->for_system_time())
               << "FOR SYSTEM TIME is not allowed with array scan";
      }
    }
    // For backward compatibility, we decided not to update the error message of
    // NATURAL JOIN for the array reference case as of April 2023. But it is a
    // nice improvement to have in the future.
    if (join->natural() && (rhs_is_unnest_expr || is_correlated_array_join ||
                            rhs_is_correlated_array_in_subquery)) {
      return MakeSqlErrorAt(join->rhs())
             << "Array scan is not allowed with NATURAL JOIN: " << error_label;
    }
  }

  // Join trees are normally left deep and normally evaluated left to right.
  // There are two ways to introduce a right-deep shape in GoogleSQL:
  // 1) Use parenthesis to evaluate a join on the right first.
  // 2) Use consecutive ON or USING clauses.
  // In such cases, we need different scoping rules for the right deep part.
  //
  // From Date & Darwen, p143:
  //   ( ( T1 JOIN T2 ON cond1 )
  //     JOIN
  //     ( T3 JOIN T4 ON cond2 )
  //     ON cond3 )
  // * cond1 can see T1, T2
  // * cond2 can see T3, T4
  // * cond3 can see T1, T2, T3, T4
  // * The select-list can see T1, T2, T3, T4
  //
  // The same structure can be written without parens as:
  //   T1 JOIN T2 ON cond1 JOIN T3 JOIN T4 ON cond2 ON cond3
  //
  // There is a further refinement we don't currently support because we
  // don't allow aliases on parenthesized join.
  //   ( ( T1 JOIN T2 ON cond1 ) AS TA
  //     JOIN
  //     ( T3 JOIN T4 ON cond2 ) AS TB
  //     ON cond3 )
  // * Now, cond3 can see only TA, TB, and not T1, T2, T3, T4.
  // * The select-list can see only TA, TB, and not T1, T2, T3, T4.
  auto external_and_lhs_scope =
      std::make_unique<NameScope>(external_scope, name_list_lhs);

  CorrelatedColumnsSet correlated_columns_set;
  auto lateral_scope = std::make_unique<NameScope>(external_and_lhs_scope.get(),
                                                   &correlated_columns_set);

  const NameScope* rhs_from_scope;
  if (is_right_or_full_join ||
      // When the RHS is parenthsized
      join->rhs()->node_kind() == AST_PARENTHESIZED_JOIN ||
      // When the RHS is right-deep due to consecutive ON clauses.
      join->rhs()->node_kind() == AST_JOIN) {
    // A clean scope only includes external names (and none of the names
    // introduced locally in the same FROM clause). There are two cases where
    // we need a "clean" scope on the right hand side of the JOIN:
    //
    // 1. The rhs is not a parenthesized join (e.g. table names, path
    //    expressions, table subqueries) and the current JOIN type is RIGHT JOIN
    //    or FULL JOIN.
    //
    // 2. The right hand side is a join introduced by parenthesis or a tight
    //    binding ON or USING clause. See the example above the conditional.
    //
    // Otherwise, the lhs name scope is still needed because it is allowed to
    // have names correlated to the lhs of the JOIN.
    rhs_from_scope = external_scope;
  } else {
    rhs_from_scope =
        is_lateral ? lateral_scope.get() : external_and_lhs_scope.get();
  }

  // Peek at rhs_node to see if we should try to resolve it as an array scan.
  // If the first identifier can be resolved inside <rhs_from_scope>, then try
  // to resolve this join as an array scan.
  if (rhs_is_table_path_expr) {
    const ASTTablePathExpression* table_ref =
        join->rhs()->GetAsOrDie<ASTTablePathExpression>();
    const ASTPathExpression* rhs_path_expr = table_ref->path_expr();
    // We may have an unnest_expr instead of a path_expr here.
    // Single-word identifiers are always resolved as table names.
    if ((rhs_is_unnest_expr && !is_right_or_full_join) ||
        is_correlated_array_join ||
        (rhs_is_correlated_array_in_subquery && !is_right_or_full_join)) {
      // Make sure this join is valid for an array scan.
      bool is_left_outer = false;
      switch (join->join_type()) {
        case ASTJoin::DEFAULT_JOIN_TYPE:
        case ASTJoin::CROSS:
        case ASTJoin::INNER:
        case ASTJoin::COMMA:
          break;  // These are all inner joins.
        case ASTJoin::LEFT:
          is_left_outer = true;
          break;
        case ASTJoin::RIGHT:
          return MakeSqlErrorAt(join->rhs())
                 << "Correlated array references are not allowed with RIGHT "
                    "JOIN: "
                 << error_label;
        case ASTJoin::FULL:
          return MakeSqlErrorAt(join->rhs())
                 << "Correlated array references are not allowed with FULL "
                    "JOIN: "
                 << error_label;
      }
      if (table_ref->for_system_time() != nullptr) {
        return MakeSqlErrorAt(table_ref->for_system_time())
               << "FOR SYSTEM TIME is not allowed with correlated array "
                  "reference";
      }

      std::optional<PathExpressionSpan> path_expr =
          table_ref->path_expr() != nullptr
              ? std::make_optional(PathExpressionSpan(*rhs_path_expr))
              : std::nullopt;
      return ResolveArrayScan(table_ref, path_expr, join->on_clause(),
                              join->using_clause(), join, is_left_outer,
                              /*include_lhs_name_list=*/true,
                              /*is_single_table_array_path=*/false,
                              &resolved_lhs, name_list_lhs, rhs_from_scope,
                              output, output_name_list);
    }
  }

  // Now we're in the normal table-scan case.
  std::unique_ptr<const ResolvedScan> resolved_rhs;
  std::shared_ptr<const NameList> name_list_rhs;
  GOOGLESQL_RETURN_IF_ERROR(ResolveTableExpression(
      join->rhs(), external_scope, rhs_from_scope,
      /*is_leftmost=*/false, on_rhs_of_right_or_full_join, &resolved_rhs,
      &name_list_rhs));

  // True iff this join type requires an ON or USING clause.
  bool requires_join_condition;
  // True iff this join type requires an ON or USING clause (not necessarily
  // requires it). This must be true if `requires_join_condition` is true.
  bool accepts_join_condition;

  const char* join_type_name = "";  // For error messages.
  ResolvedJoinScan::JoinType resolved_join_type;
  bool has_using = false;
  switch (join->join_type()) {
    case ASTJoin::COMMA:
      // This is the only case without a "JOIN" keyword.
      // This join_type_name never gets used currently because none of the
      // error cases below apply for comma joins.
      join_type_name = "comma join";
      accepts_join_condition = false;
      requires_join_condition = false;
      resolved_join_type = ResolvedJoinScan::INNER;
      break;
    case ASTJoin::CROSS:
      join_type_name = "CROSS JOIN";
      accepts_join_condition = false;
      requires_join_condition = false;
      resolved_join_type = ResolvedJoinScan::INNER;
      break;
    case ASTJoin::DEFAULT_JOIN_TYPE:  // No join_type keyword - same as INNER.
    case ASTJoin::INNER:
      join_type_name = "INNER JOIN";
      accepts_join_condition = true;
      requires_join_condition = true;
      resolved_join_type = ResolvedJoinScan::INNER;
      break;
    case ASTJoin::LEFT:
      join_type_name = "LEFT JOIN";
      resolved_join_type = ResolvedJoinScan::LEFT;
      // LEFT JOIN always accepts a join condition, even if LATEARAL.
      accepts_join_condition = true;
      // LEFT JOIN requires a join condition unless it's not LATERAL.
      requires_join_condition = !is_lateral;
      break;
    case ASTJoin::RIGHT:
      join_type_name = "RIGHT JOIN";
      requires_join_condition = true;
      accepts_join_condition = true;
      resolved_join_type = ResolvedJoinScan::RIGHT;
      break;
    case ASTJoin::FULL:
      join_type_name = "FULL JOIN";
      requires_join_condition = true;
      accepts_join_condition = true;
      resolved_join_type = ResolvedJoinScan::FULL;
      break;
  }

  // Sanity checks: cannot require a join condition if we're not accepting it.
  GOOGLESQL_RET_CHECK(!requires_join_condition || accepts_join_condition);

  const char* natural_str = "";  // For error messages.
  if (join->natural()) {
    if (!accepts_join_condition) {
      return MakeSqlErrorAtLocalNode(join->join_location())
             << "NATURAL cannot be used with " << join_type_name;
    }
    return MakeSqlErrorAtLocalNode(join->join_location())
           << "Natural join not supported";
  }

  // This stores the extra casted (for LEFT, RIGHT, INNER JOIN) and
  // coalesced columns (for FULL JOIN) that may be required for USING.
  // These columns are computed after the join.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns;

  std::shared_ptr<NameList> name_list(new NameList);
  std::unique_ptr<const ResolvedExpr> join_condition;

  if (join->using_clause() != nullptr) {
    GOOGLESQL_RET_CHECK(join->on_clause() == nullptr);  // Can't have both.
    if (!accepts_join_condition) {
      return MakeSqlErrorAt(join->using_clause())
             << "USING clause cannot be used with " << natural_str
             << join_type_name;
    }
    has_using = true;

    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        lhs_computed_columns;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        rhs_computed_columns;

    GOOGLESQL_RETURN_IF_ERROR(ResolveUsing(join->using_clause(), *name_list_lhs,
                                 *name_list_rhs, resolved_join_type,
                                 /*is_array_scan=*/false, &lhs_computed_columns,
                                 &rhs_computed_columns, &computed_columns,
                                 name_list.get(), &join_condition));

    // Add a Project for any columns we need to computed before the join.
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
        std::move(lhs_computed_columns), &resolved_lhs));
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
        std::move(rhs_computed_columns), &resolved_rhs));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(
        name_list->MergeFrom(*name_list_lhs, join->lhs()->alias_location()));
    GOOGLESQL_RETURN_IF_ERROR(
        name_list->MergeFrom(*name_list_rhs, join->rhs()->alias_location()));

    static constexpr char kJoinOnClause[] = "JOIN ON clause";
    if (join->on_clause() != nullptr) {
      if (!accepts_join_condition) {
        return MakeSqlErrorAt(join->on_clause())
               << "ON clause cannot be used with " << natural_str
               << join_type_name;
      }

      const std::unique_ptr<const NameScope> on_scope(
          new NameScope(external_scope, name_list));

      GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(join->on_clause()->expression(),
                                        on_scope.get(), kJoinOnClause,
                                        &join_condition));
      GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(join->on_clause()->expression(),
                                       kJoinOnClause, &join_condition));
    } else {
      // No ON or USING clause.
      if (requires_join_condition) {
        return MakeSqlErrorAtLocalNode(join->join_location())
               << natural_str << join_type_name
               << " must have an immediately following ON or USING clause";
      }
    }
  }

  if (!language().LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING) &&
      resolved_join_type != ResolvedJoinScan::INNER) {
    GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
        NameListPtr(name_list_lhs), join->join_location(),
        absl::StrCat(ResolvedJoinScanEnums::JoinType_Name(resolved_join_type),
                     " JOIN"),
        product_mode()));
    GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
        NameListPtr(name_list_rhs), join->join_location(),
        absl::StrCat(ResolvedJoinScanEnums::JoinType_Name(resolved_join_type),
                     " JOIN"),
        product_mode()));
  }

  *output_name_list = name_list;

  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list;
  if (is_lateral) {
    FetchCorrelatedSubqueryParameters(correlated_columns_set, &parameter_list);
  }

  return AddScansForJoin(join, std::move(resolved_lhs), std::move(resolved_rhs),
                         resolved_join_type, has_using,
                         std::move(join_condition), std::move(computed_columns),
                         is_lateral, std::move(parameter_list),
                         *output_name_list, output);
}

absl::Status Resolver::AddScansForJoin(
    const ASTJoin* join, std::unique_ptr<const ResolvedScan> resolved_lhs,
    std::unique_ptr<const ResolvedScan> resolved_rhs,
    ResolvedJoinScan::JoinType resolved_join_type, bool has_using,
    std::unique_ptr<const ResolvedExpr> join_condition,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns,
    bool is_lateral,
    std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list,
    std::shared_ptr<const NameList> output_name_list,
    std::unique_ptr<const ResolvedScan>* output_scan) {
  if (!is_lateral) {
    GOOGLESQL_RET_CHECK(parameter_list.empty());
  }

  ResolvedColumnList concat_columns = ConcatColumnLists(
      resolved_lhs->column_list(), resolved_rhs->column_list());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedJoinScan> resolved_join,
                   ResolvedJoinScanBuilder()
                       .set_column_list(concat_columns)
                       .set_join_type(resolved_join_type)
                       .set_left_scan(std::move(resolved_lhs))
                       .set_right_scan(std::move(resolved_rhs))
                       .set_join_expr(std::move(join_condition))
                       .set_has_using(has_using)
                       .set_is_lateral(is_lateral)
                       .set_parameter_list(std::move(parameter_list))
                       .BuildMutable());

  // If we have a join_type keyword hint (e.g. HASH JOIN or LOOKUP JOIN),
  // add it on the front of hint_list, before any long-form hints.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddJoinHintKeyword(join, resolved_join.get()));

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(join->hint(), resolved_join.get()));

  if (join->join_location() != nullptr) {
    MaybeRecordParseLocation(join->join_location()->location(),
                             resolved_join.get());
  }

  *output_scan = std::move(resolved_join);

  // If we created a CAST expression for RIGHT/INNER/LEFT JOIN or a
  // COALESCE for FULL JOIN with USING, we create a wrapper ProjectScan node to
  // produce additional columns for those expressions.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(std::move(computed_columns),
                                                    output_scan));

  return absl::OkStatus();
}

absl::Status Resolver::ResolveParenthesizedJoin(
    const ASTParenthesizedJoin* parenthesized_join,
    const NameScope* external_scope, const NameScope* local_scope,
    bool on_rhs_of_right_or_full_join,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  std::unique_ptr<const ResolvedScan> resolved_join;
  // Set `is_leftmost` to true, to disallow LATERAL on the first item within
  // this parenthesized join.
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveJoin(parenthesized_join->join(), external_scope, local_scope,
                  /*is_leftmost=*/true, on_rhs_of_right_or_full_join,
                  &resolved_join, output_name_list));

  for (const auto* op : parenthesized_join->postfix_operators()) {
    switch (op->node_kind()) {
      case AST_SAMPLE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(
            ResolveTablesampleClause(op->GetAsOrDie<ASTSampleClause>(),
                                     output_name_list, &resolved_join));
        break;
      case AST_MATCH_RECOGNIZE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizeClause(
            std::move(resolved_join), *output_name_list, external_scope,
            op->GetAsOrDie<ASTMatchRecognizeClause>(), &resolved_join,
            output_name_list));
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported postfix operator: " << op->node_kind();
    }
  }

  *output = std::move(resolved_join);
  return absl::OkStatus();
}

namespace {
// Vertifies that argument of TVF call inside <resolved_tvf_args> has no
// collation, i.e. each argument is not a scalar argument of collated type or a
// table argument with collated columns. An error will be thrown if the
// verification fails. The input <ast_locations> is expected to match 1:1 to the
// arguments in the <resolved_tvf_args>, and it is mainly used for error
// message.
absl::Status CheckTVFArgumentHasNoAnnotations(
    std::vector<ResolvedTVFArg>& resolved_tvf_args,
    const std::vector<const ASTNode*> ast_locations,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK_EQ(ast_locations.size(), resolved_tvf_args.size());
  for (int i = 0; i < resolved_tvf_args.size(); ++i) {
    if (resolved_tvf_args[i].IsExpr()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* const expr,
                       resolved_tvf_args[i].GetExpr());
      if (CollationAnnotation::ExistsIn(expr->type_annotation_map())) {
        GOOGLESQL_RET_CHECK(ast_locations[i] != nullptr);
        return MakeSqlErrorAt(ast_locations[i])
               << "Collation "
               << expr->type_annotation_map()->DebugString(
                      CollationAnnotation::GetId())
               << " on argument of TVF call is not allowed";
      }
      if (expr->type_annotation_map() != nullptr &&
          !expr->type_annotation_map()->Empty()) {
        std::unique_ptr<AnnotationMap> annotations_to_block =
            expr->type_annotation_map()->Clone();
        // TODO: Block all other annotations
        if (!annotations_to_block->Empty()) {
          GOOGLESQL_RET_CHECK(ast_locations[i] != nullptr);
          return MakeSqlErrorAt(ast_locations[i])
                 << "Annotations on argument of TVF call is not allowed"
                 << expr->type_annotation_map()->DebugString();
        }
      }
    } else if (resolved_tvf_args[i].IsScan()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                       resolved_tvf_args[i].GetNameList());
      const std::vector<ResolvedColumn> column_list =
          name_list->GetResolvedColumns();
      for (const ResolvedColumn& col : column_list) {
        if (CollationAnnotation::ExistsIn(col.type_annotation_map())) {
          std::string column_str = name_list->is_value_table()
                                       ? "value-table column"
                                       : "column " + col.name();
          GOOGLESQL_RET_CHECK(ast_locations[i] != nullptr);
          return MakeSqlErrorAt(ast_locations[i])
                 << "Collation "
                 << col.type_annotation_map()->DebugString(
                        CollationAnnotation::GetId())
                 << " on " << column_str
                 << " of argument of TVF call is not allowed";
        }
        if (col.type_annotation_map() != nullptr) {
          std::unique_ptr<AnnotationMap> annotations_to_block =
              col.type_annotation_map()->Clone();
          // TODO: Block all other annotations
          annotations_to_block->UnsetAnnotationRecursively(100001);
          annotations_to_block->UnsetAnnotationRecursively(613944552);
          annotations_to_block->UnsetAnnotationRecursively(808504570);

          if (!annotations_to_block->Empty()) {
            std::string column_str = name_list->is_value_table()
                                         ? "value-table column"
                                         : "column " + col.name();
            GOOGLESQL_RET_CHECK(ast_locations[i] != nullptr);
            return MakeSqlErrorAt(ast_locations[i])
                   << "Annotations on " << column_str
                   << " of argument of TVF call is not allowed: "
                   << annotations_to_block->DebugString();
          }
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<const TableValuedFunction*> FindTVFOrMakeNiceError(
    absl::string_view tvf_name_string, const ASTTVF* ast_tvf,
    const AnalyzerOptions& analyzer_options, Catalog* catalog) {
  const TableValuedFunction* tvf_catalog_entry = nullptr;
  const absl::Status find_status = catalog->FindTableValuedFunction(
      ast_tvf->name()->ToIdentifierVector(), &tvf_catalog_entry,
      analyzer_options.find_options());
  if (find_status.code() == absl::StatusCode::kNotFound) {
    std::string error_message;
    absl::StrAppend(&error_message,
                    "Table-valued function not found: ", tvf_name_string);

    const std::string tvf_suggestion = catalog->SuggestTableValuedFunction(
        ast_tvf->name()->ToIdentifierVector());
    if (!tvf_suggestion.empty()) {
      absl::StrAppend(&error_message, "; Did you mean ", tvf_suggestion, "?");
    }

    return MakeSqlErrorAt(ast_tvf) << error_message;
  } else if (!find_status.ok()) {
    // The FindTableValuedFunction() call can return an invalid argument error,
    // for example, when looking up LazyResolutionTableFunctions (which are
    // resolved upon lookup).
    //
    // Rather than directly return the <find_status>, we update the location
    // of the error to indicate the function call in this statement.  We also
    // preserve the ErrorSource payload from <find_status>, since that
    // indicates source errors for this error.
    return WrapNestedErrorStatus(
        ast_tvf,
        absl::StrCat("Invalid table-valued function ", tvf_name_string),
        find_status, analyzer_options.error_message_mode());
  }
  return tvf_catalog_entry;
}

}  // namespace

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveTVF(
    const ASTTVF* ast_tvf, const NameScope* external_scope,
    ResolvedTVFArg* pipe_input_arg, std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Check the language options to make sure TVFs are supported on this server.
  if (!language().LanguageFeatureEnabled(FEATURE_TABLE_VALUED_FUNCTIONS)) {
    return MakeSqlErrorAt(ast_tvf)
           << "Table-valued functions are not supported";
  }

  // Lookup into the catalog to get the TVF definition.
  const std::string tvf_name_string = ast_tvf->name()->ToIdentifierPathString();
  const IdString tvf_name_idstring = MakeIdString(tvf_name_string);
  GOOGLESQL_ASSIGN_OR_RETURN(const TableValuedFunction* tvf_catalog_entry,
                   FindTVFOrMakeNiceError(tvf_name_string, ast_tvf,
                                          analyzer_options_, catalog_));

  std::vector<ResolvedTVFArg> resolved_tvf_args;
  std::unique_ptr<FunctionSignature> result_signature;
  std::vector<TVFInputArgumentType> tvf_input_arguments;
  GOOGLESQL_RETURN_IF_ERROR(
      PrepareTVFInputArguments(tvf_name_string, ast_tvf, tvf_catalog_entry,
                               external_scope, pipe_input_arg, result_signature,
                               resolved_tvf_args, tvf_input_arguments));

  GOOGLESQL_RET_CHECK_EQ(resolved_tvf_args.size(), tvf_input_arguments.size());

  GOOGLESQL_RETURN_IF_ERROR(StatusWithInternalErrorLocation(
      tvf_catalog_entry->CheckPostResolutionArgumentConstraints(
          *result_signature, tvf_input_arguments, analyzer_options_.language()),
      ast_tvf));

  // Call the TableValuedFunction::Resolve method to get the output schema.
  // Use a new empty cycle detector, or the cycle detector from an enclosing
  // Resolver if we are analyzing one or more templated function calls.
  std::shared_ptr<TVFSignature> tvf_signature;
  absl::Status resolve_status;
  if (analyzer_options_.find_options().cycle_detector() == nullptr) {
    // AnalyzerOptions is a very large object, and this stack frame is already
    // huge. Allocate it on the heap to save some stack space.
    struct AnalyzerAndCycleDetector {
      CycleDetector owned_cycle_detector;
      AnalyzerOptions analyzer_options;
    };
    auto cycle = std::make_unique<AnalyzerAndCycleDetector>(
        AnalyzerAndCycleDetector{.analyzer_options = analyzer_options_});
    cycle->analyzer_options.mutable_find_options()->set_cycle_detector(
        &cycle->owned_cycle_detector);
    resolve_status = tvf_catalog_entry->Resolve(
        &cycle->analyzer_options, tvf_input_arguments, *result_signature,
        catalog_, type_factory_, &tvf_signature);
  } else {
    resolve_status = tvf_catalog_entry->Resolve(
        &analyzer_options_, tvf_input_arguments, *result_signature, catalog_,
        type_factory_, &tvf_signature);
  }

  if (!resolve_status.ok()) {
    // The Resolve method returned an error status that is already updated
    // based on the <analyzer_options> ErrorMessageMode.  Make a new
    // ErrorSource based on the <resolve_status>, and return a new error
    // status that indicates that the TVF call is invalid, while indicating
    // the TVF call location for the error.
    return WrapNestedErrorStatus(
        ast_tvf,
        absl::StrCat("Invalid table-valued function ", tvf_name_string),
        resolve_status, analyzer_options_.error_message_mode());
  }

  bool has_measure = absl::c_any_of(tvf_signature->result_schema().columns(),
                                    [](const TVFSchemaColumn& column) {
                                      return column.type->IsMeasureType();
                                    });
  if (has_measure) {
    if (!language().LanguageFeatureEnabled(FEATURE_MEASURES_TVF)) {
      return MakeSqlErrorAt(ast_tvf)
             << "TVF returning MEASURE-typed columns is not supported";
    }
    // TVFs that emit measure columns must provide a `const Table*` to describe
    // their output schema to provide information like row identity columns.
    //
    // This is not a SQL error because it is usually because engines
    // misconfigured the catalog and users cannot change their query to fix it.
    GOOGLESQL_RET_CHECK(tvf_signature->result_table_schema() != nullptr);

    const std::string measure_source =
        absl::StrCat("the output of the table valued function ",
                     ast_tvf->name()->ToIdentifierPathString());
    GOOGLESQL_RETURN_IF_ERROR(ValidateMeasureSource(
        ast_tvf, tvf_signature->result_table_schema(), measure_source));
  }

  bool is_value_table = tvf_signature->result_schema().is_value_table();
  if (is_value_table) {
    GOOGLESQL_RETURN_IF_ERROR(CheckValidValueTableFromTVF(
        ast_tvf, tvf_catalog_entry->SQLName(), tvf_signature->result_schema()));
  } else if (tvf_signature->result_schema().num_columns() == 0) {
    return MakeSqlErrorAt(ast_tvf)
           << "Table-valued functions must return at least one column, but "
           << "TVF " << tvf_catalog_entry->SQLName() << " returned no columns";
  }

  // Fill the column and name list based on the output schema.
  // These columns match up 1:1 by position because we don't have a guarantee
  // of unique names. For value table, the output schema must have the value
  // column at index 0 and the rest columns if present must be pseudo columns.
  ResolvedColumnList column_list;
  NameListBuilder name_list_builder(ast_tvf);
  column_list.reserve(tvf_signature->result_schema().num_columns());
  for (int i = 0; i < tvf_signature->result_schema().num_columns(); ++i) {
    const TVFRelation::Column& column =
        tvf_signature->result_schema().column(i);
    const IdString column_name = MakeIdString(
        !column.name.empty() ? column.name : absl::StrCat("$col", i));
    column_list.push_back(ResolvedColumn(AllocateColumnId(), tvf_name_idstring,
                                         column_name, column.annotated_type()));
    name_list_builder.AddColumn(column_name, column_list.back(),
                                column.is_pseudo_column);
  }
  if (is_value_table) {
    name_list_builder.SetValueTable().SetAlias(MakeIdString("$col0"));
  }
  if (ast_tvf->alias() != nullptr) {
    name_list_builder.SetAlias(ast_tvf->alias()->GetAsIdString());
  }
  GOOGLESQL_ASSIGN_OR_RETURN(*output_name_list, name_list_builder.Build());

  // Resolve the query hint, if present.
  std::vector<std::unique_ptr<const ResolvedOption>> hints;
  if (ast_tvf->hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintAndAppend(ast_tvf->hint(), &hints));
  }

  // Create the resolved TVF scan.
  std::vector<const ResolvedTVFArgument*> final_resolved_tvf_args;
  for (ResolvedTVFArg& arg : resolved_tvf_args) {
    if (arg.IsExpr()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> expr,
                       arg.MoveExpr());
      final_resolved_tvf_args.push_back(
          MakeResolvedTVFArgument(std::move(expr), /*scan=*/nullptr,
                                  /*model=*/nullptr, /*connection=*/nullptr,
                                  /*descriptor_arg=*/nullptr,
                                  /*argument_column_list=*/{})
              .release());
    } else if (arg.IsScan()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> scan,
                       arg.MoveScan());
      GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                       arg.GetNameList());
      std::vector<ResolvedColumn> argument_column_list =
          name_list->GetResolvedColumns();
      // Add pseudo-columns to the argument column list.
      if (language().LanguageFeatureEnabled(FEATURE_TVF_PSEUDO_COLUMNS)) {
        std::vector<ResolvedColumn> pseudo_columns =
            name_list->GetResolvedPseudoColumns();
        argument_column_list.insert(argument_column_list.end(),
                                    pseudo_columns.begin(),
                                    pseudo_columns.end());
      }
      final_resolved_tvf_args.push_back(
          MakeResolvedTVFArgument(/*expr=*/nullptr, std::move(scan),
                                  /*model=*/nullptr, /*connection=*/nullptr,
                                  /*descriptor_arg=*/nullptr,
                                  argument_column_list)
              .release());
    } else if (arg.IsConnection()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedConnection> connection,
                       arg.MoveConnection());
      final_resolved_tvf_args.push_back(
          MakeResolvedTVFArgument(/*expr=*/nullptr, /*scan=*/nullptr,
                                  /*model=*/nullptr, std::move(connection),
                                  /*descriptor_arg=*/nullptr,
                                  /*argument_column_list=*/{})
              .release());
    } else if (arg.IsDescriptor()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedDescriptor> descriptor,
                       arg.MoveDescriptor());
      final_resolved_tvf_args.push_back(
          MakeResolvedTVFArgument(/*expr=*/nullptr, /*scan=*/nullptr,
                                  /*model=*/nullptr, /*connection=*/nullptr,
                                  /*descriptor_arg=*/std::move(descriptor),
                                  /*argument_column_list=*/{})
              .release());
    } else if (arg.IsModel()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedModel> model,
                       arg.MoveModel());
      final_resolved_tvf_args.push_back(
          MakeResolvedTVFArgument(/*expr=*/nullptr, /*scan=*/nullptr,
                                  std::move(model), /*connection=*/nullptr,
                                  /*descriptor_arg=*/nullptr,
                                  /*argument_column_list=*/{})
              .release());
    } else {
      GOOGLESQL_RET_CHECK(arg.IsGraph());
      GOOGLESQL_ASSIGN_OR_RETURN(const PropertyGraph* graph, arg.GetGraph());
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto tvf_arg,
          ResolvedFunctionArgumentBuilder().set_graph(graph).Build());
      final_resolved_tvf_args.push_back(tvf_arg.release());
    }
  }
  std::string alias;
  if (ast_tvf->alias() != nullptr) {
    alias = ast_tvf->alias()->GetAsString();
  }

  std::vector<int> column_index_list(
      tvf_signature->result_schema().columns().size());
  // Fill column_index_list with 0, 1, 2, ..., column_list.size()-1.
  std::iota(column_index_list.begin(), column_index_list.end(), 0);

  auto tvf_scan =
      MakeResolvedTVFScan(column_list, tvf_catalog_entry, tvf_signature,
                          std::move(final_resolved_tvf_args), column_index_list,
                          alias, std::move(result_signature));
  // WARNING: <result_signature> is destroyed at this point.

  tvf_scan->set_hint_list(std::move(hints));

  MaybeRecordTVFCallParseLocation(ast_tvf, tvf_scan.get());
  *output = std::move(tvf_scan);

  for (const auto* op : ast_tvf->postfix_operators()) {
    switch (op->node_kind()) {
      case AST_PIVOT_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolvePivotClause(
            std::move(*output), *output_name_list, external_scope,
            /*input_is_subquery=*/false, op->GetAsOrDie<ASTPivotClause>(),
            output, output_name_list));
        break;
      case AST_UNPIVOT_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveUnpivotClause(
            std::move(*output), *output_name_list, external_scope,
            op->GetAsOrDie<ASTUnpivotClause>(), output, output_name_list));
        break;
      case AST_SAMPLE_CLAUSE: {
        if (!language().LanguageFeatureEnabled(
                FEATURE_TABLESAMPLE_FROM_TABLE_VALUED_FUNCTIONS)) {
          return MakeSqlErrorAt(op)
                 << "TABLESAMPLE from table-valued function calls is not "
                    "supported";
        }
        GOOGLESQL_RETURN_IF_ERROR(ResolveTablesampleClause(
            op->GetAsOrDie<ASTSampleClause>(), output_name_list, output));
        break;
      }
      case AST_MATCH_RECOGNIZE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizeClause(
            std::move(*output), *output_name_list, external_scope,
            op->GetAsOrDie<ASTMatchRecognizeClause>(), output,
            output_name_list));
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported postfix operator: " << op->node_kind();
    }
  }
  return absl::OkStatus();
}

std::string Resolver::MaybeSuggestUsingInputTable() {
  return language().LanguageFeatureEnabled(FEATURE_PIPE_CALL_INPUT_TABLE)
             ? "; Consider using INPUT TABLE for the table argument"
             : "";
}

absl::Status Resolver::MatchTVFSignature(
    const ASTTVF* /*absl_nonnull*/ ast_tvf,
    const TableValuedFunction* /*absl_nonnull*/ tvf_catalog_entry,
    int signature_idx, const NameScope* /*absl_nonnull*/ external_scope,
    const FunctionResolver& function_resolver,
    ResolvedTVFArg* /*absl_nullable*/ pipe_input_arg,
    std::unique_ptr<FunctionSignature>& result_signature,
    std::vector<const ASTNode*>& arg_locations,
    std::vector<ResolvedTVFArg>& resolved_tvf_args,
    std::vector<NamedArgumentInfo>& named_arguments,
    SignatureMatchResult& signature_match_result) {
  const FunctionSignature& function_signature =
      *tvf_catalog_entry->GetSignature(signature_idx);

  // Look for an INPUT TABLE argument.  If present, it will get the pipe
  // input table.
  int pipe_input_arg_idx_in_sig = -1;
  bool has_input_table_arg = false;
  const ASTNode* input_table_arg_location = nullptr;
  for (int arg_idx = 0; arg_idx < ast_tvf->argument_entries().size();
       ++arg_idx) {
    const ASTTVFArgument* ast_arg = ast_tvf->argument_entries()[arg_idx];
    if (ast_arg->expr() == nullptr) continue;
    const ASTExpression* ast_expr = ast_arg->expr();

    const auto* ast_named_argument = ast_expr->GetAsOrNull<ASTNamedArgument>();
    if (ast_named_argument != nullptr) {
      if (!language().LanguageFeatureEnabled(FEATURE_NAMED_ARGUMENTS)) {
        return MakeSqlErrorAt(ast_expr) << "Named arguments are not supported";
      }
      ast_expr = ast_named_argument->expr();
    }
    if (ast_expr->Is<ASTInputTableArgument>()) {
      if (!language().LanguageFeatureEnabled(FEATURE_PIPES) ||
          !language().LanguageFeatureEnabled(FEATURE_PIPE_CALL_INPUT_TABLE)) {
        return MakeSqlErrorAt(ast_expr)
               << "INPUT TABLE arguments are not supported";
      }
      if (has_input_table_arg) {
        return MakeSqlErrorAt(ast_expr)
               << "TVF calls cannot have multiple INPUT TABLE arguments";
      }
      if (pipe_input_arg == nullptr) {
        return MakeSqlErrorAt(ast_expr)
               << "INPUT TABLE arguments are only allowed when using "
                  "pipe CALL to call TVFs";
      }
      has_input_table_arg = true;
      pipe_input_arg_idx_in_sig = arg_idx;
      input_table_arg_location = ast_expr;
      if (ast_named_argument != nullptr) {
        named_arguments.emplace_back(
            ast_named_argument->name()->GetAsIdString(), arg_idx,
            ast_named_argument);
      }
    }
  }

  const int num_ast_args = static_cast<int>(ast_tvf->argument_entries().size());
  int num_tvf_args =
      num_ast_args +
      (pipe_input_arg != nullptr && !has_input_table_arg ? 1 : 0);

  // If we have a pipe input table argument and didn't have INPUT TABLE to
  // indicate which argument it is, find the first table argument in the
  // signature to match it with.  It must not be a named-only argument or
  // follow any optional arguments.
  if (pipe_input_arg != nullptr && !has_input_table_arg) {
    if (pipe_input_arg->IsScan()) {
      // Make sure the signature has enough table arguments as positional
      // arguments at the front to support these initial table args.
      for (int sig_idx = 0; sig_idx < function_signature.arguments().size();
           ++sig_idx) {
        const FunctionArgumentType& arg_type =
            function_signature.argument(sig_idx);
        if (arg_type.IsRelation()) {
          if (arg_type.options().named_argument_kind() == kNamedOnly) {
            return MakeSqlErrorAt(ast_tvf)
                   << "Table-valued function cannot be called with pipe CALL "
                      "syntax because its first table-typed argument is a "
                      "named-only argument; Supported signature: "
                   << tvf_catalog_entry->GetSupportedSignaturesUserFacingText(
                          language(), /*print_template_and_name_details=*/true)
                   << MaybeSuggestUsingInputTable();
          }
          // Found the signature argument to use.
          pipe_input_arg_idx_in_sig = sig_idx;
          break;
        }
      }
      if (pipe_input_arg_idx_in_sig < 0) {
        return MakeSqlErrorAt(ast_tvf)
               << "Table-valued function cannot be called with pipe CALL "
                  "syntax because its signature does not include a table-typed "
                  "argument; Supported signature: "
               << tvf_catalog_entry->GetSupportedSignaturesUserFacingText(
                      language(), /*print_template_and_name_details=*/true);
      }
      // If any optional arguments occur before the table argument, that makes
      // signature matching more complicated because we can't just insert the
      // table argument in the right place in positional arguments ahead of
      // time. We also get more confusing error messages. This case seems
      // obscure so we disallow it.
      for (int sig_idx = 0; sig_idx < pipe_input_arg_idx_in_sig; ++sig_idx) {
        const FunctionArgumentType& arg_type =
            function_signature.argument(sig_idx);
        if (arg_type.optional()) {
          return MakeSqlErrorAt(ast_tvf)
                 << "Table-valued function cannot be called with pipe CALL "
                    "syntax because it has an optional argument before its "
                    "first table-typed argument; Supported signature: "
                 << tvf_catalog_entry->GetSupportedSignaturesUserFacingText(
                        language(), /*print_template_and_name_details=*/true)
                 << MaybeSuggestUsingInputTable();
        }
        // It shouldn't be possible that there's a named-only argument before
        // the table argument since we checked that the table argument is
        // not named-only above.
        GOOGLESQL_RET_CHECK_NE(arg_type.options().named_argument_kind(), kNamedOnly);
      }

      // The CALL doesn't work if it doesn't have enough arguments to
      // fill in all arguments that precede the pipe input argument.
      if (num_ast_args < pipe_input_arg_idx_in_sig) {
        return MakeSqlErrorAt(ast_tvf)
               << "Table-valued function call in pipe CALL requires at least "
               << pipe_input_arg_idx_in_sig << " positional argument"
               << (pipe_input_arg_idx_in_sig == 1 ? "" : "s")
               << " because its first table argument is in position "
               << (pipe_input_arg_idx_in_sig + 1) << ", but the call has "
               << (num_ast_args == 0
                       ? "no arguments"
                       : (num_ast_args == 1
                              ? "only 1 argument"
                              : absl::StrCat("only ", num_ast_args,
                                             " arguments")))
               << "; Supported signature: "
               << tvf_catalog_entry->GetSupportedSignaturesUserFacingText(
                      language(), /*print_template_and_name_details=*/true)
               << MaybeSuggestUsingInputTable();
      }

      // Require that all arguments passed in the CALL before the input table
      // argument are positional.  This could be relaxed but it complicates the
      // signature matching because we can't just insert the table argument
      // in the correct place.  Errors can also be confusing.
      int num_ast_positional_args = 0;
      const ASTNode* ast_node_for_error = ast_tvf;
      for (; num_ast_positional_args < num_ast_args;
           ++num_ast_positional_args) {
        const ASTTVFArgument* ast_arg =
            ast_tvf->argument_entries()[num_ast_positional_args];
        const ASTExpression* ast_expr = ast_arg->expr();
        if (ast_expr != nullptr &&
            ast_expr->node_kind() == AST_NAMED_ARGUMENT) {
          // If we stopped at a named argument, use that as the error location.
          // If we had too few total arguments, just point at the function name.
          ast_node_for_error = ast_expr;
          break;
        }
      }
      if (num_ast_positional_args < pipe_input_arg_idx_in_sig) {
        return MakeSqlErrorAt(ast_node_for_error)
               << "Table-valued function call in pipe CALL requires at least "
               << pipe_input_arg_idx_in_sig << " positional argument"
               << (pipe_input_arg_idx_in_sig == 1 ? "" : "s")
               << " before any named arguments because "
                  "its first table argument is in position "
               << (pipe_input_arg_idx_in_sig + 1) << "; Supported signature: "
               << tvf_catalog_entry->GetSupportedSignaturesUserFacingText(
                      language(), /*print_template_and_name_details=*/true);
      }

    } else {
      GOOGLESQL_RET_CHECK(pipe_input_arg->IsGraph());
      if (!function_signature.arguments().empty()) {
        const FunctionArgumentType& arg_type = function_signature.argument(0);
        if (arg_type.IsGraph()) {
          GOOGLESQL_RET_CHECK(arg_type.options().named_argument_kind() != kNamedOnly)
              << "No user-defined TVFs that take a graph work today";
          pipe_input_arg_idx_in_sig = 0;
        }
      }

      if (pipe_input_arg_idx_in_sig < 0) {
        // We're not using the input arg, so forget about it.
        pipe_input_arg = nullptr;
        --num_tvf_args;
      }
    }
  }

  // Check whether descriptors are present in this TVF call.
  bool descriptor_arg_present = false;
  for (const ASTTVFArgument* ast_tvf_arg : ast_tvf->argument_entries()) {
    if (ast_tvf_arg->descriptor() != nullptr) {
      descriptor_arg_present = true;
      break;
    }
  }

  // Resolve the TVF arguments. Each one becomes either an expression, a scan,
  // an ML model, a connection or a descriptor object. We allow correlation
  // references to the enclosing query if this TVF call is inside a scalar
  // subquery expression, but we do not allow references to columns in
  // previous tables in the same FROM clause as the TVF. For this reason, we
  // use 'external_scope' for the ExprResolutionInfo object here when
  // resolving expressions in the TVF call.
  absl::flat_hash_map<int, std::unique_ptr<const NameScope>>
      sig_idx_to_name_scope_map;
  arg_locations.reserve(num_tvf_args);
  resolved_tvf_args.reserve(num_tvf_args);
  for (int arg_idx = 0, current_ast_idx = 0; arg_idx < num_tvf_args;
       ++arg_idx) {
    // `arg_idx` is the implicit argument number in the call after the
    // pipe_input_arg has been inserted.

    if (arg_idx == pipe_input_arg_idx_in_sig) {
      GOOGLESQL_RET_CHECK(pipe_input_arg != nullptr);
      // The resolved input arg is just the `pipe_input_arg` table scan or a
      // graph arg, which has already been resolved.
      ResolvedTVFArg pipe_arg_copy;
      if (pipe_input_arg->IsScan()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                         pipe_input_arg->GetNameList());
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedScan* pipe_input_scan,
                         pipe_input_arg->GetScan());
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> scan,
                         ResolvedASTDeepCopyVisitor::Copy(pipe_input_scan));
        pipe_arg_copy.SetScan(std::move(scan), name_list,
                              pipe_input_arg->IsPipeInputTable());
      } else if (pipe_input_arg->IsGraph()) {
        GOOGLESQL_ASSIGN_OR_RETURN(const PropertyGraph* graph,
                         pipe_input_arg->GetGraph());
        pipe_arg_copy.SetGraph(graph);
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected pipe input argument";
      }
      resolved_tvf_args.push_back(std::move(pipe_arg_copy));

      if (input_table_arg_location != nullptr) {
        // Use the location of INPUT TABLE if we had it.
        arg_locations.push_back(input_table_arg_location);
        // INPUT TABLE has an explicit slot in the AST arguments.
        ++current_ast_idx;
      } else {
        // For the initial table argument, we don't have an argument location,
        // so just use the location of the TVF call.
        arg_locations.push_back(ast_tvf);
      }
    } else {
      // ast_idx is the arg position in the AST arguments.
      int ast_idx = current_ast_idx++;

      // `sig_idx` is the position of the arg in the signature.
      // Positional arguments come first, so this is initially `arg_idx`.
      // `sig_idx` will be adjusted below for named arguments.
      int sig_idx = arg_idx;

      // Hitting this would mean we went off the end of AST args before we
      // got to use the pipe_input_arg.
      GOOGLESQL_RET_CHECK_LT(ast_idx, num_ast_args);

      const ASTTVFArgument* ast_arg = ast_tvf->argument_entries()[ast_idx];
      const ASTExpression* ast_expr = ast_arg->expr();
      if (ast_expr == nullptr) {
        arg_locations.push_back(ast_arg);
      } else {
        arg_locations.push_back(ast_expr);
        if (ast_expr->node_kind() == AST_NAMED_ARGUMENT) {
          // Make sure the language feature is enabled.
          if (!language().LanguageFeatureEnabled(FEATURE_NAMED_ARGUMENTS)) {
            return MakeSqlErrorAt(ast_expr)
                   << "Named arguments are not supported";
          }
          // Add the named argument to the map.
          const ASTNamedArgument* named_arg =
              ast_expr->GetAs<ASTNamedArgument>();
          if (IsNamedLambda(named_arg)) {
            return MakeSqlErrorAt(named_arg->expr())
                   << "Lambda arguments are not implemented for TVF";
          }
          named_arguments.emplace_back(named_arg->name()->GetAsIdString(),
                                       arg_idx, named_arg);
          const absl::string_view arg_name =
              named_arg->name()->GetAsIdString().ToStringView();

          sig_idx = -1;
          for (int j = 0; j < function_signature.arguments().size(); ++j) {
            const FunctionArgumentType& arg_type =
                function_signature.argument(j);
            if (arg_type.has_argument_name() &&
                googlesql_base::CaseEqual(arg_type.argument_name(), arg_name)) {
              sig_idx = j;
              break;
            }
          }
          if (sig_idx == -1) {
            return MakeSqlErrorAt(ast_expr)
                   << "Named argument " << arg_name
                   << " not found in signature for call to function "
                   << tvf_catalog_entry->SQLName();
          }
        }
      }
      // TODO - Fix a bug in TVF argument resolution if a model /
      // connection / description argument occurs after a repeated argument.
      // FunctionSignature::IsValidForTableValuedFunction validates that
      // relation args don't follow repeated args but it doesn't have a similar
      // check for model / connection / descriptor args.
      // NOTE: This function is not called for the pipe CALL lhs args.
      auto tvf_arg_or_status = ResolveTVFArg(
          ast_arg, external_scope,
          // This arg can be NULL if these are extra args beyond what the
          // signature expects.
          sig_idx < function_signature.arguments().size()
              ? &function_signature.argument(sig_idx)
              : nullptr,
          tvf_catalog_entry, sig_idx,
          descriptor_arg_present ? &sig_idx_to_name_scope_map : nullptr);

      GOOGLESQL_RETURN_IF_ERROR(tvf_arg_or_status.status());
      resolved_tvf_args.push_back(std::move(tvf_arg_or_status).value());
    }
  }
  GOOGLESQL_RET_CHECK_EQ(arg_locations.size(), num_tvf_args);
  GOOGLESQL_RET_CHECK_EQ(resolved_tvf_args.size(), num_tvf_args);

  // We perform a second resolution pass for descriptors whose columns must be
  // resolved against a related table argument. The second pass is necessary
  // because column resolution is done with respect to the table's NameScope,
  // which requires that the related table argument is already resolved
  // (descriptor columns can reference table arguments that appear after them
  // in the function call).
  if (descriptor_arg_present) {
    // Store the NameList for the pipe_input_arg so descriptor matching
    // can see it. This happened in ResolveTVFArg for other table args.
    if (pipe_input_arg != nullptr) {
      GOOGLESQL_RET_CHECK_GE(pipe_input_arg_idx_in_sig, 0);
      GOOGLESQL_RET_CHECK(!sig_idx_to_name_scope_map.contains(pipe_input_arg_idx_in_sig));
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto name_list,
          resolved_tvf_args[pipe_input_arg_idx_in_sig].GetNameList());
      sig_idx_to_name_scope_map[pipe_input_arg_idx_in_sig] =
          std::make_unique<NameScope>(external_scope, name_list);
    }

    for (int sig_idx = 0, current_ast_idx = 0; sig_idx < num_tvf_args;
         ++sig_idx) {
      // `sig_idx` is the index in the FunctionSignature.
      // `ast_idx` is the arg position in the AST arguments, or -1 for
      // pipe_input_arg.
      int ast_idx = -1;
      if (sig_idx != pipe_input_arg_idx_in_sig || has_input_table_arg) {
        ast_idx = current_ast_idx++;
      }

      ResolvedTVFArg* resolved_tvf_arg = &resolved_tvf_args[sig_idx];
      if (resolved_tvf_arg->IsDescriptor()) {
        GOOGLESQL_RET_CHECK_NE(ast_idx, -1);  // pipe_input_arg is not a descriptor.
        const ASTTVFArgument* ast_tvf_arg =
            ast_tvf->argument_entries()[ast_idx];
        const FunctionArgumentType* function_argument =
            sig_idx < function_signature.arguments().size()
                ? &function_signature.argument(sig_idx)
                : nullptr;
        if (function_argument != nullptr) {
          std::optional<int> table_offset =
              function_argument->GetDescriptorResolutionTableOffset();
          if (table_offset.has_value()) {
            GOOGLESQL_RET_CHECK_GE(table_offset.value(), 0);
            if (!sig_idx_to_name_scope_map.contains(table_offset.value())) {
              return MakeSqlErrorAt(ast_tvf_arg)
                     << "DESCRIPTOR specifies resolving names from non-table "
                        "argument "
                     << table_offset.value();
            }

            std::unique_ptr<const ResolvedDescriptor> resolved_descriptor =
                resolved_tvf_arg->MoveDescriptor().value();
            GOOGLESQL_RETURN_IF_ERROR(FinishResolvingDescriptor(
                ast_tvf_arg, sig_idx_to_name_scope_map[table_offset.value()],
                table_offset.value(), &resolved_descriptor));
            resolved_tvf_arg->SetDescriptor(std::move(resolved_descriptor));
          }
        }
      }
    }
  }

  // Check if the function call contains any named arguments.
  const std::string tvf_name_string = ast_tvf->name()->ToIdentifierPathString();
  int repetitions = 0;
  int optionals = 0;
  GOOGLESQL_RET_CHECK_LE(arg_locations.size(), std::numeric_limits<int32_t>::max());

  std::vector<InputArgumentType> input_arg_types;
  input_arg_types.reserve(num_tvf_args);
  for (int i = 0; i < num_tvf_args; ++i) {
    auto input_arg_type_or_status = GetTVFArgType(resolved_tvf_args.at(i));
    GOOGLESQL_RETURN_IF_ERROR(input_arg_type_or_status.status());
    input_arg_types.push_back(std::move(input_arg_type_or_status).value());
  }

  signature_match_result.set_allow_mismatch_message(true);

  if (!SignatureArgumentCountMatches(function_signature, input_arg_types,
                                     &repetitions, &optionals,
                                     &signature_match_result)) {
    return GenerateTVFNotMatchError(
        ast_tvf, arg_locations, signature_match_result, *tvf_catalog_entry,
        tvf_name_string, input_arg_types, signature_idx);
  }

  std::vector<ArgIndexEntry> arg_index_mapping;
  // TVFs can only have one signature for now, so either this call
  // returns an error, or the arguments match the signature.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string mismatch_message,
      function_resolver.GetFunctionArgumentIndexMappingPerSignature(
          tvf_name_string, function_signature, ast_tvf, arg_locations,
          named_arguments, repetitions,
          /*always_include_omitted_named_arguments_in_index_mapping=*/false,
          &arg_index_mapping));
  if (!mismatch_message.empty()) {
    if (signature_match_result.allow_mismatch_message()) {
      signature_match_result.set_mismatch_message(mismatch_message);
    }
    return GenerateTVFNotMatchError(
        ast_tvf, arg_locations, signature_match_result, *tvf_catalog_entry,
        tvf_name_string, input_arg_types, signature_idx);
  }
  GOOGLESQL_RETURN_IF_ERROR(
      FunctionResolver::
          ReorderInputArgumentTypesPerIndexMappingAndInjectDefaultValues(
              function_signature, arg_index_mapping, &input_arg_types));

  // Check if the TVF arguments match its signature. If not, return an error.
  GOOGLESQL_ASSIGN_OR_RETURN(
      const bool matches,
      function_resolver.SignatureMatches(
          arg_locations, input_arg_types, function_signature,
          /*allow_argument_coercion=*/true, /*name_scope=*/nullptr,
          &result_signature, &signature_match_result, &arg_index_mapping,
          /*arg_overrides=*/nullptr, /*lambda_ast_nodes=*/nullptr));

  if (!matches) {
    return GenerateTVFNotMatchError(
        ast_tvf, arg_locations, signature_match_result, *tvf_catalog_entry,
        tvf_name_string, input_arg_types, signature_idx);
  }

  GOOGLESQL_RETURN_IF_ERROR(FunctionResolver::ReorderArgumentExpressionsPerIndexMapping(
      tvf_name_string, function_signature, arg_index_mapping, ast_tvf,
      input_arg_types, &arg_locations, /*resolved_args=*/nullptr,
      &resolved_tvf_args));

  return absl::OkStatus();
}

absl::StatusOr<int> Resolver::FindMatchingTVFSignature(
    const ASTTVF* /*absl_nonnull*/ ast_tvf,
    const TableValuedFunction* /*absl_nonnull*/ tvf_catalog_entry,
    const NameScope* /*absl_nonnull*/ external_scope,
    const FunctionResolver& function_resolver,
    ResolvedTVFArg* /*absl_nullable*/ pipe_input_arg,
    std::unique_ptr<FunctionSignature>& result_signature,
    std::vector<const ASTNode*>& arg_locations,
    std::vector<ResolvedTVFArg>& resolved_tvf_args,
    std::vector<NamedArgumentInfo>& named_arguments,
    SignatureMatchResult& signature_match_result) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  int matching_signature_idx = -1;

  for (int sig_idx = 0; sig_idx < tvf_catalog_entry->NumSignatures();
       ++sig_idx) {
    std::vector<const ASTNode*> current_arg_locations;
    // TODO - Avoid duplicate resolution of arguments during each
    // signature matching iteration for args that don't need the signature
    // argument type for resolution.
    std::vector<ResolvedTVFArg> current_resolved_tvf_args;
    std::vector<NamedArgumentInfo> current_named_arguments;
    SignatureMatchResult current_signature_match_result;
    std::unique_ptr<FunctionSignature> current_result_signature;

    absl::Status match_status = MatchTVFSignature(
        ast_tvf, tvf_catalog_entry, sig_idx, external_scope, function_resolver,
        pipe_input_arg, current_result_signature, current_arg_locations,
        current_resolved_tvf_args, current_named_arguments,
        current_signature_match_result);

    if (match_status.ok()) {
      // If this signature matches, check if we have previously found another
      // matching signature. Multiple matching signatures for a given set of
      // input arguments is currently not supported and an internal error is
      // returned. Signature scoring rules to find the closest matching
      // signature will be added later.
      if (matching_signature_idx != -1) {
        return googlesql_base::InternalErrorBuilder()
               << "Signatures of a Table Valued Function should be constructed "
                  "in such a way that it's impossible for a set of arguments "
                  "to match multiple signatures. But found multiple matching "
                  "signatures for table valued function : "
               << tvf_catalog_entry->DebugString();
      }
      matching_signature_idx = sig_idx;
      result_signature = std::move(current_result_signature);
      arg_locations = std::move(current_arg_locations);
      resolved_tvf_args = std::move(current_resolved_tvf_args);
      named_arguments = std::move(current_named_arguments);
      signature_match_result = std::move(current_signature_match_result);
    } else {
      // Return the non-OK match status if there is a single signature to
      // return the exact error message.
      if (tvf_catalog_entry->NumSignatures() == 1) {
        return match_status;
      }
    }
  }
  // TODO - Keep track of the mismatch errors per signature
  // which can be returned in case of no matching signatures.
  if (matching_signature_idx == -1) {
    return MakeSqlErrorAt(ast_tvf)
           << "No matching signature for table valued function : "
           << tvf_catalog_entry->SQLName() << ". Supported Signatures: "
           << tvf_catalog_entry->GetSupportedSignaturesUserFacingText(
                  language(), /*print_template_and_name_details=*/true);
  }
  return matching_signature_idx;
}

absl::Status Resolver::PrepareTVFInputArguments(
    absl::string_view tvf_name_string, const ASTTVF* ast_tvf,
    const TableValuedFunction* tvf_catalog_entry,
    const NameScope* external_scope, ResolvedTVFArg* pipe_input_arg,
    std::unique_ptr<FunctionSignature>& result_signature,
    std::vector<ResolvedTVFArg>& resolved_tvf_args,
    std::vector<TVFInputArgumentType>& tvf_input_arguments) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK(resolved_tvf_args.empty());
  GOOGLESQL_RET_CHECK(tvf_input_arguments.empty());

  // <arg_locations> and <resolved_tvf_args> reflect the concrete function
  // call arguments in <result_signature> and match 1:1 to them.
  std::vector<const ASTNode*> arg_locations;
  std::vector<NamedArgumentInfo> named_arguments;

  SignatureMatchResult signature_match_result;
  FunctionResolver function_resolver(catalog_, type_factory_, this);
  GOOGLESQL_ASSIGN_OR_RETURN(
      const int matching_signature_idx,
      FindMatchingTVFSignature(
          ast_tvf, tvf_catalog_entry, external_scope, function_resolver,
          pipe_input_arg, result_signature, arg_locations, resolved_tvf_args,
          named_arguments, signature_match_result));
  GOOGLESQL_RET_CHECK_EQ(arg_locations.size(), resolved_tvf_args.size());
  const FunctionSignature* function_signature =
      tvf_catalog_entry->GetSignature(matching_signature_idx);
  GOOGLESQL_RETURN_IF_ERROR(AddAdditionalDeprecationWarningsForCalledFunction(
      ast_tvf, *function_signature, tvf_catalog_entry->SQLName(),
      /*is_tvf=*/true));

  // TODO: b/166482992 - Implement annotations in SQL TVFs.
  GOOGLESQL_RETURN_IF_ERROR(CheckTVFArgumentHasNoAnnotations(
      resolved_tvf_args, arg_locations, analyzer_options_));

  // Add casts or coerce literals for TVF arguments.
  GOOGLESQL_RET_CHECK(result_signature->IsConcrete()) << ast_tvf->DebugString();

  const auto BadArgErrorPrefix = [&result_signature, &named_arguments,
                                  tvf_catalog_entry](int idx) {
    const FunctionArgumentType& argument =
        result_signature->ConcreteArgument(idx);
    if (argument.has_argument_name()) {
      // Check whether function call was using named argument or positional
      // argument, and if it was named - use the name in the error message.
      for (const auto& named_arg : named_arguments) {
        if (googlesql_base::CaseEqual(named_arg.name().ToString(),
                                   argument.argument_name())) {
          return absl::StrCat("Argument '", argument.argument_name(),
                              "' to table-valued function ",
                              tvf_catalog_entry->SQLName());
        }
      }
    }
    if (result_signature->NumConcreteArguments() == 1) {
      return absl::StrCat("The argument to table-valued function ",
                          tvf_catalog_entry->SQLName());
    } else {
      return absl::StrCat("Argument ", idx + 1, " to table-valued function ",
                          tvf_catalog_entry->SQLName());
    }
  };

  for (int arg_idx = 0; arg_idx < resolved_tvf_args.size(); ++arg_idx) {
    ResolvedTVFArg& resolved_tvf_arg = resolved_tvf_args[arg_idx];
    if (resolved_tvf_arg.IsExpr()) {
      GOOGLESQL_RET_CHECK_LT(arg_idx, result_signature->NumConcreteArguments());

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> expr,
                       resolved_tvf_arg.MoveExpr());
      GOOGLESQL_RETURN_IF_ERROR(function_resolver.CheckArgumentConstraints(
          ast_tvf, tvf_catalog_entry->SQLName(),
          /*is_tvf=*/true, arg_locations[arg_idx], arg_idx,
          result_signature->ConcreteArgument(arg_idx), expr.get(),
          BadArgErrorPrefix));

      const Type* target_type = result_signature->ConcreteArgumentType(arg_idx);
      const ASTNode* ast = arg_locations[arg_idx];
      if (ast->node_kind() == AST_NAMED_ARGUMENT) {
        ast = ast->GetAs<ASTNamedArgument>()->expr();
        GOOGLESQL_RET_CHECK(ast != nullptr);
        // Has validated in `MatchTVFSignature` that named arguments are not
        // named lambdas.
        GOOGLESQL_RET_CHECK(!ast->Is<ASTLambda>());
      }
      GOOGLESQL_RETURN_IF_ERROR(
          CoerceExprToType(ast, target_type, kExplicitCoercion, &expr));
      if (expr->node_kind() == RESOLVED_LITERAL) {
        const Value& value = expr->GetAs<ResolvedLiteral>()->value();
        GOOGLESQL_RETURN_IF_ERROR(function_resolver.CheckArgumentValueConstraints(
            arg_locations[arg_idx], arg_idx, value,
            result_signature->ConcreteArgument(arg_idx), BadArgErrorPrefix));
      }
      resolved_tvf_arg.SetExpr(std::move(expr));
    } else if (resolved_tvf_arg.IsScan()) {
      GOOGLESQL_ASSIGN_OR_RETURN(bool must_add_projection,
                       CheckIfMustCoerceOrRearrangeTVFRelationArgColumns(
                           function_signature->argument(arg_idx), arg_idx,
                           signature_match_result, resolved_tvf_arg));
      if (must_add_projection) {
        GOOGLESQL_RETURN_IF_ERROR(CoerceOrRearrangeTVFRelationArgColumns(
            function_signature->argument(arg_idx), arg_idx,
            signature_match_result, ast_tvf, resolved_tvf_arg));
      }
      // We've now ensured the input scan has just the required columns.
      // Mark those columns accessed so they don't get pruned.
      // Use the NameList rather than the column_list to avoid including
      // pseudo-columns.
      GOOGLESQL_ASSIGN_OR_RETURN(auto name_list, resolved_tvf_arg.GetNameList());
      RecordColumnAccess(name_list->GetResolvedColumns());
      // Mark all pseudo-columns as accessed so that they don't get pruned.
      if (language().LanguageFeatureEnabled(FEATURE_TVF_PSEUDO_COLUMNS)) {
        RecordColumnAccess(name_list->GetResolvedPseudoColumns());
      }
    }
  }

  // Prepare the list of TVF input arguments for calling the
  // TableValuedFunction::Resolve method.
  tvf_input_arguments.reserve(resolved_tvf_args.size());
  for (const ResolvedTVFArg& resolved_tvf_arg : resolved_tvf_args) {
    if (resolved_tvf_arg.IsExpr()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* const expr,
                       resolved_tvf_arg.GetExpr());
      if (expr->node_kind() == RESOLVED_LITERAL) {
        const Value& value = expr->GetAs<ResolvedLiteral>()->value();
        tvf_input_arguments.push_back(
            TVFInputArgumentType(InputArgumentType(value)));
      } else {
        tvf_input_arguments.push_back(
            TVFInputArgumentType(InputArgumentType(expr->type())));
      }
      tvf_input_arguments.back().set_scalar_expr(expr);
    } else if (resolved_tvf_arg.IsDescriptor()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedDescriptor* const descriptor,
                       resolved_tvf_arg.GetDescriptor());
      tvf_input_arguments.push_back(TVFInputArgumentType(
          TVFDescriptorArgument(descriptor->descriptor_column_name_list())));
    } else if (resolved_tvf_arg.IsConnection()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedConnection* const connection,
                       resolved_tvf_arg.GetConnection());
      tvf_input_arguments.push_back(TVFInputArgumentType(
          TVFConnectionArgument(connection->connection())));
    } else if (resolved_tvf_arg.IsModel()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedModel* const model,
                       resolved_tvf_arg.GetModel());
      tvf_input_arguments.push_back(
          TVFInputArgumentType(TVFModelArgument(model->model())));
    } else if (resolved_tvf_arg.IsGraph()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const PropertyGraph* graph, resolved_tvf_arg.GetGraph());
      tvf_input_arguments.push_back(
          TVFInputArgumentType(TVFGraphArgument(graph)));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                       resolved_tvf_arg.GetNameList());
      const std::vector<ResolvedColumn> column_list =
          name_list->GetResolvedColumns();
      if (name_list->is_value_table()) {
        GOOGLESQL_RET_CHECK_EQ(1, name_list->num_columns()) << ast_tvf->DebugString();
        GOOGLESQL_RET_CHECK_EQ(1, column_list.size()) << ast_tvf->DebugString();
        TVFRelation::ColumnList pseudo_columns;
        if (language().LanguageFeatureEnabled(FEATURE_TVF_PSEUDO_COLUMNS)) {
          for (const auto& pseudo_column :
               name_list->GetResolvedPseudoColumns()) {
            pseudo_columns.emplace_back(pseudo_column.name(),
                                        pseudo_column.annotated_type(),
                                        /*is_pseudo_column_in=*/true);
          }
        }
        GOOGLESQL_ASSIGN_OR_RETURN(TVFRelation value_table_relation,
                         TVFRelation::ValueTable(
                             name_list->column(0).column().annotated_type(),
                             pseudo_columns));
        tvf_input_arguments.push_back(
            TVFInputArgumentType(value_table_relation));
      } else {
        TVFRelation::ColumnList tvf_relation_columns;
        tvf_relation_columns.reserve(column_list.size());
        GOOGLESQL_RET_CHECK_GE(column_list.size(), name_list->num_columns())
            << ast_tvf->DebugString();
        for (int j = 0; j < name_list->num_columns(); ++j) {
          tvf_relation_columns.emplace_back(
              name_list->column(j).name().ToString(),
              column_list[j].annotated_type());
        }
        // Add pseudo columns to tvf_relation_columns.
        if (language().LanguageFeatureEnabled(FEATURE_TVF_PSEUDO_COLUMNS)) {
          for (const auto& pseudo_column :
               name_list->GetResolvedPseudoColumns()) {
            tvf_relation_columns.emplace_back(pseudo_column.name(),
                                              pseudo_column.annotated_type(),
                                              /*is_pseudo_column_in=*/true);
          }
        }
        tvf_input_arguments.push_back(
            TVFInputArgumentType(TVFRelation(tvf_relation_columns)));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::GenerateTVFNotMatchError(
    const ASTTVF* ast_tvf, const std::vector<const ASTNode*>& arg_locations,
    const SignatureMatchResult& signature_match_result,
    const TableValuedFunction& tvf_catalog_entry, absl::string_view tvf_name,
    absl::Span<const InputArgumentType> input_arg_types, int signature_idx) {
  const ASTNode* ast_location = ast_tvf;
  if (signature_match_result.bad_argument_index() != -1) {
    GOOGLESQL_RET_CHECK_LT(signature_match_result.bad_argument_index(),
                 arg_locations.size());
    ast_location = arg_locations[signature_match_result.bad_argument_index()];
  }
  return MakeSqlErrorAt(ast_location)
         << tvf_catalog_entry.GetTVFSignatureErrorMessage(
                tvf_name, input_arg_types, signature_idx,
                signature_match_result, language());
}

// NOTE: This will not be called for initial arguments like the pipe CALL lhs.
// This only does processing of the args on the rhs in the parentheses.
// So we can't do any processing or checking required on all args here.
absl::StatusOr<ResolvedTVFArg> Resolver::ResolveTVFArg(
    const ASTTVFArgument* ast_tvf_arg, const NameScope* external_scope,
    const FunctionArgumentType* function_argument,
    const TableValuedFunction* tvf_catalog_entry, int sig_idx,
    absl::flat_hash_map<int, std::unique_ptr<const NameScope>>*
        sig_idx_to_name_scope_map) {
  const ASTExpression* ast_expr = ast_tvf_arg->expr();
  const ASTTableClause* ast_table_clause = ast_tvf_arg->table_clause();
  const ASTModelClause* ast_model_clause = ast_tvf_arg->model_clause();
  const ASTConnectionClause* ast_connection_clause =
      ast_tvf_arg->connection_clause();
  const ASTDescriptor* ast_descriptor = ast_tvf_arg->descriptor();
  ResolvedTVFArg resolved_tvf_arg;
  if (ast_table_clause != nullptr) {
    GOOGLESQL_RET_CHECK(ast_expr == nullptr);
    std::unique_ptr<const ResolvedScan> scan;
    std::shared_ptr<const NameList> name_list;
    GOOGLESQL_RETURN_IF_ERROR(ResolveTableClause(
        ast_table_clause, external_scope,
        /*resolve_argument_first=*/
        (tvf_catalog_entry->statement_context() == CONTEXT_MODULE),
        /*read_as_row_type_error_kind=*/"TVF argument", &scan, &name_list));
    resolved_tvf_arg.SetScan(std::move(scan), name_list,
                             /*is_pipe_input_table=*/false);
    GOOGLESQL_ASSIGN_OR_RETURN(name_list, resolved_tvf_arg.GetNameList());
    RecordColumnAccess(name_list->GetResolvedColumns());
  } else if (ast_expr != nullptr) {
    if (ast_expr->node_kind() == AST_NAMED_ARGUMENT) {
      if (IsNamedLambda(ast_expr)) {
        return MakeSqlErrorAt(ast_expr)
               << "Lambda arguments are not implemented for TVF";
      }
      // Set 'ast_expr' to the named argument value for further resolving.
      ast_expr = ast_expr->GetAs<ASTNamedArgument>()->expr();
    }
    if (function_argument &&
        (function_argument->IsRelation() || function_argument->IsModel() ||
         function_argument->IsConnection() ||
         function_argument->IsDescriptor() || function_argument->IsGraph())) {
      auto gen_arg_id = [function_argument](int sig_idx) {
        std::string arg_id = absl::StrCat(sig_idx + 1);
        if (function_argument->has_argument_name()) {
          absl::StrAppend(&arg_id, " ('", function_argument->argument_name(),
                          "')");
        }
        return arg_id;
      };
      if (function_argument->IsRelation()) {
        // Resolve the TVF argument as a relation. The argument should be
        // written in the TVF call as a table subquery. We parsed all
        // arguments as expressions, so the parse node should initially have
        // scalar subquery type. We check that the expression subquery
        // modifier type is NONE to exclude ARRAY and EXISTS subqueries.
        if (ast_expr->node_kind() != AST_EXPRESSION_SUBQUERY ||
            ast_expr->GetAsOrDie<ASTExpressionSubquery>()->modifier() !=
                ASTExpressionSubquery::NONE) {
          std::string error = absl::StrCat(
              "Table-valued function ", tvf_catalog_entry->SQLName(),
              " argument ", gen_arg_id(sig_idx),
              " must be a relation (i.e. table subquery)");
          if (ast_expr->node_kind() == AST_PATH_EXPRESSION) {
            const std::string table_name =
                ast_expr->GetAsOrDie<ASTPathExpression>()
                    ->ToIdentifierPathString();
            // Return a specific error message helping the user figure out
            // what to change.
            absl::StrAppend(&error, "; if you meant to refer to table ",
                            table_name, " then add the TABLE keyword ",
                            "before the table name (i.e. TABLE ", table_name,
                            ")");
          }
          return MakeSqlErrorAt(ast_expr) << error;
        }
        std::unique_ptr<const ResolvedScan> scan;
        std::shared_ptr<const NameList> name_list;
        GOOGLESQL_RETURN_IF_ERROR(
            ResolveQuery(ast_expr->GetAsOrDie<ASTExpressionSubquery>()->query(),
                         external_scope, AllocateSubqueryName(), &scan,
                         &name_list, {.is_expr_subquery = true}));

        // The <sig_idx_to_name_scope_map> is not nullptr means descriptors
        // appear in TVF thus there is a need to build NameScopes for the table
        // arguments.
        if (sig_idx_to_name_scope_map != nullptr) {
          sig_idx_to_name_scope_map->emplace(
              sig_idx, std::make_unique<NameScope>(external_scope, name_list));
        }
        resolved_tvf_arg.SetScan(std::move(scan), std::move(name_list),
                                 /*is_pipe_input_table=*/false);
      } else if (function_argument->IsConnection()) {
        return MakeSqlErrorAt(ast_expr)
               << "Table-valued function " << tvf_catalog_entry->SQLName()
               << " argument " << gen_arg_id(sig_idx)
               << " must be a connection specified with the CONNECTION keyword";
      } else if (function_argument->IsDescriptor()) {
        return MakeSqlErrorAt(ast_expr)
               << "Table-valued function " << tvf_catalog_entry->SQLName()
               << " argument " << gen_arg_id(sig_idx)
               << " must be a DESCRIPTOR";
      } else if (function_argument->IsGraph()) {
        return MakeSqlErrorAt(ast_expr)
               << "Table-valued function " << tvf_catalog_entry->SQLName()
               << " argument " << gen_arg_id(sig_idx) << " must be a GRAPH";
      } else {
        GOOGLESQL_RET_CHECK(function_argument->IsModel());
        return MakeSqlErrorAt(ast_expr)
               << "Table-valued function " << tvf_catalog_entry->SQLName()
               << " argument " << gen_arg_id(sig_idx)
               << " must be a model specified with the MODEL keyword";
      }
    } else {
      // Resolve the TVF argument as a scalar expression.
      std::unique_ptr<const ResolvedExpr> expr;
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveScalarExpr(ast_expr, external_scope, "FROM clause", &expr));
      resolved_tvf_arg.SetExpr(std::move(expr));
    }
  } else if (ast_connection_clause != nullptr) {
    std::unique_ptr<const ResolvedConnection> resolved_connection;
    GOOGLESQL_RETURN_IF_ERROR(ResolveConnection(ast_connection_clause->connection_path(),
                                      &resolved_connection));
    resolved_tvf_arg.SetConnection(std::move(resolved_connection));
  } else if (ast_model_clause != nullptr) {
    std::unique_ptr<const ResolvedModel> resolved_model;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveModel(ast_model_clause->model_path(), &resolved_model));
    resolved_tvf_arg.SetModel(std::move(resolved_model));
  } else {
    GOOGLESQL_RET_CHECK(ast_descriptor != nullptr);
    std::unique_ptr<const ResolvedDescriptor> resolved_descriptor;
    const ASTDescriptorColumnList* column_list = ast_descriptor->columns();
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveDescriptorFirstPass(column_list, &resolved_descriptor));
    resolved_tvf_arg.SetDescriptor(std::move(resolved_descriptor));
  }

  return resolved_tvf_arg;
}

absl::StatusOr<InputArgumentType> Resolver::GetTVFArgType(
    const ResolvedTVFArg& resolved_tvf_arg) {
  InputArgumentType input_arg_type;
  if (resolved_tvf_arg.IsExpr()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* const expr,
                     resolved_tvf_arg.GetExpr());
    // We should not force the type for a pure NULL expr, as the output
    // InputArgumentType is used for function signature matching and the final
    // argument type is not determined yet.
    GOOGLESQL_ASSIGN_OR_RETURN(input_arg_type,
                     GetInputArgumentTypeForExpr(
                         expr, /*pick_default_type_for_untyped_expr=*/false,
                         analyzer_options()));
  } else if (resolved_tvf_arg.IsScan()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                     resolved_tvf_arg.GetNameList());
    if (name_list->is_value_table()) {
      input_arg_type = InputArgumentType::RelationInputArgumentType(
          TVFRelation::ValueTable(name_list->column(0).column().type()),
          resolved_tvf_arg.IsPipeInputTable());
    } else {
      TVFRelation::ColumnList provided_input_relation_columns;
      provided_input_relation_columns.reserve(name_list->num_columns());
      // Loop over each explicit column returned from the relation argument.
      // Use the number of names in the relation argument's name list instead
      // of the scan's column_list().size() here since the latter includes
      // pseudo-columns, which we do not want to consider here.
      for (int j = 0; j < name_list->num_columns(); ++j) {
        const NamedColumn& named_column = name_list->column(j);
        provided_input_relation_columns.emplace_back(
            named_column.name().ToString(),
            named_column.column().annotated_type());
      }
      input_arg_type = InputArgumentType::RelationInputArgumentType(
          TVFRelation(provided_input_relation_columns),
          resolved_tvf_arg.IsPipeInputTable());
    }
  } else if (resolved_tvf_arg.IsConnection()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedConnection* const connection,
                     resolved_tvf_arg.GetConnection());
    input_arg_type = InputArgumentType::ConnectionInputArgumentType(
        TVFConnectionArgument(connection->connection()));
  } else if (resolved_tvf_arg.IsModel()) {
    // We are processing a model argument.
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedModel* const model,
                     resolved_tvf_arg.GetModel());
    input_arg_type = InputArgumentType::ModelInputArgumentType(
        TVFModelArgument(model->model()));
  } else if (resolved_tvf_arg.IsGraph()) {
    input_arg_type = InputArgumentType::GraphInputArgumentType();
  } else {
    GOOGLESQL_RET_CHECK(resolved_tvf_arg.IsDescriptor());
    // We are processing a descriptor argument.
    input_arg_type = InputArgumentType::DescriptorInputArgumentType();
  }
  return input_arg_type;
}

absl::StatusOr<bool>
Resolver::CheckIfMustCoerceOrRearrangeTVFRelationArgColumns(
    const FunctionArgumentType& tvf_signature_arg, int arg_idx,
    const SignatureMatchResult& signature_match_result,
    const ResolvedTVFArg& resolved_tvf_arg) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                   resolved_tvf_arg.GetNameList());
  const int num_provided_columns = name_list->num_columns();

  // If we have an ROW-typed columns, we want to call the Coerce method to
  // trigger an error.
  for (int i = 0; i < num_provided_columns; ++i) {
    if (name_list->column(i).column().type()->IsRow()) {
      return true;
    }
  }

  // If the function signature did not include a required schema for this
  // particular relation argument, there is no need to add a projection.
  if (!tvf_signature_arg.options().has_relation_input_schema()) {
    return false;
  }

  // Add a projection to add or drop columns as needed if the number of provided
  // columns was not equal to the number of required columns.
  const TVFRelation& required_schema =
      tvf_signature_arg.options().relation_input_schema();
  if (required_schema.num_columns() != num_provided_columns) {
    return true;
  }

  // Add a projection if the function signature-matching process indicates a
  // need to coerce the type of one of the function arguments.
  for (const auto& [key, to_type] :
       signature_match_result.tvf_relation_coercion_map()) {
    if (arg_idx == key.argument_index) {
      return true;
    }
  }

  // If the required schema was a value table and no type coercion was
  // necessary, then there is no need to add a projection.
  if (required_schema.is_value_table()) {
    return false;
  }

  // If the order of provided columns was not equal to the order of required
  // columns, add a projection to rearrange provided columns as needed.
  GOOGLESQL_RET_CHECK_EQ(required_schema.num_columns(), name_list->columns().size());
  for (int i = 0; i < num_provided_columns; ++i) {
    if (!googlesql_base::CaseEqual(required_schema.column(i).name,
                                name_list->column(i).name().ToString())) {
      return true;
    }
  }
  return false;
}

absl::Status Resolver::CoerceOrRearrangeTVFRelationArgColumns(
    const FunctionArgumentType& tvf_signature_arg, int arg_idx,
    const SignatureMatchResult& signature_match_result,
    const ASTNode* ast_location, ResolvedTVFArg& resolved_tvf_arg) {
  // Generate a name for a new projection to perform the TVF relation argument
  // coercion and create vectors to hold new columns for the projection.
  const IdString new_project_alias = AllocateSubqueryName();
  std::vector<ResolvedColumn> new_column_list;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      new_project_columns;
  std::unique_ptr<NameList> new_project_name_list =
      std::make_unique<NameList>();

  // Reserve vectors.
  // Use the number of names in the relation argument's name list instead of the
  // scan's column_list().size() here since the latter includes pseudo-columns,
  // which we do not want to consider here.
  GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<const NameList> name_list,
                   resolved_tvf_arg.GetNameList());
  const int num_provided_columns = name_list->num_columns();
  new_column_list.reserve(num_provided_columns);
  new_project_columns.reserve(num_provided_columns);
  new_project_name_list->ReserveColumns(num_provided_columns);

  // Build a map from provided column name to the index of that column in the
  // list of provided columns.
  std::map<std::string, int, googlesql_base::CaseLess> col_name_to_idx;
  for (int col_idx = 0; col_idx < num_provided_columns; ++col_idx) {
    col_name_to_idx.emplace(name_list->column(col_idx).name().ToString(),
                            col_idx);
    const Type* column_type = name_list->column(col_idx).column().type();
    if (column_type->IsRow()) {
      return MakeSqlErrorAt(ast_location)
             << "TVF table argument cannot include a column of type "
             << column_type->ShortTypeName(product_mode());
    }
  }

  // Iterate through each required column of the relation from the function
  // signature and find the matching provided column with the same name. Also
  // create a cast expression if any provided column type is not equivalent to
  // the corresponding required input column type.
  const int num_required_columns = static_cast<int>(
      tvf_signature_arg.options().relation_input_schema().columns().size());
  for (int required_col_idx = 0; required_col_idx < num_required_columns;
       ++required_col_idx) {
    const std::string required_col_name = tvf_signature_arg.options()
                                              .relation_input_schema()
                                              .column(required_col_idx)
                                              .name;
    int provided_col_idx;
    if (tvf_signature_arg.options().relation_input_schema().is_value_table()) {
      provided_col_idx = 0;
    } else {
      const int* lookup = googlesql_base::FindOrNull(col_name_to_idx, required_col_name);
      GOOGLESQL_RET_CHECK(lookup != nullptr) << required_col_name;
      provided_col_idx = *lookup;
    }
    const Type* result_type =
        googlesql_base::FindPtrOrNull(signature_match_result.tvf_relation_coercion_map(),
                           {arg_idx, provided_col_idx});
    const ResolvedColumn& provided_input_column =
        name_list->column(provided_col_idx).column();
    if (result_type == nullptr) {
      new_column_list.push_back(provided_input_column);
    } else {
      new_column_list.emplace_back(AllocateColumnId(), new_project_alias,
                                   name_list->column(provided_col_idx).name(),
                                   result_type);
      std::unique_ptr<const ResolvedExpr> resolved_cast(
          MakeColumnRef(provided_input_column, /*is_correlated=*/false));
      GOOGLESQL_RETURN_IF_ERROR(ResolveCastWithResolvedArgument(
          ast_location, result_type, /*return_null_on_error=*/false,
          &resolved_cast));
      new_project_columns.push_back(MakeResolvedComputedColumn(
          new_column_list.back(), std::move(resolved_cast)));
    }
    // Add the new referenced column to the set of referenced columns so that it
    // is not pruned away later.
    RecordColumnAccess(new_column_list.back());
    GOOGLESQL_RETURN_IF_ERROR(new_project_name_list->AddColumnMaybeValueTable(
        name_list->column(provided_col_idx).name(), new_column_list.back(),
        /*is_explicit=*/true, ast_location,
        tvf_signature_arg.options().relation_input_schema().is_value_table()));
  }
  if (tvf_signature_arg.options().relation_input_schema().is_value_table()) {
    GOOGLESQL_RETURN_IF_ERROR(new_project_name_list->SetIsValueTable());
  }

  // Reset the scan and name list to the new projection.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> scan,
                   resolved_tvf_arg.MoveScan());
  resolved_tvf_arg.SetScan(
      MakeResolvedProjectScan(new_column_list, std::move(new_project_columns),
                              std::move(scan)),
      std::move(new_project_name_list), resolved_tvf_arg.IsPipeInputTable());
  return absl::OkStatus();
}

absl::Status Resolver::ValidateUnnestAliases(
    const ASTTablePathExpression* table_ref) {
  GOOGLESQL_RET_CHECK_NE(table_ref, nullptr);
  const ASTUnnestExpression* unnest_expr = table_ref->unnest_expr();
  GOOGLESQL_RET_CHECK_NE(unnest_expr, nullptr);
  GOOGLESQL_RET_CHECK_GE(unnest_expr->expressions().size(), 1);
  const ASTExpressionWithOptAlias* first_arg = unnest_expr->expressions(0);

  if (!language().LanguageFeatureEnabled(FEATURE_MULTIWAY_UNNEST)) {
    if (unnest_expr->expressions().size() > 1) {
      return MakeSqlErrorAt(unnest_expr->expressions(1))
             << "The UNNEST operator supports exactly one argument";
    }
    if (first_arg->optional_alias() != nullptr) {
      return MakeSqlErrorAt(first_arg->optional_alias())
             << "Argument alias is not supported in the UNNEST operator";
    }
  }

  if (table_ref->alias() != nullptr) {
    // The unnest expression has legacy alias. No argument aliases are allowed
    // and there should be exactly one expression in UNNEST.
    if (unnest_expr->expressions().size() != 1) {
      return MakeSqlErrorAt(table_ref->alias())
             << "When 2 or more array arguments are supplied to UNNEST, "
                "aliases for the element columns must be specified following "
                "the argument inside the parenthesis";
    }
    if (first_arg->optional_alias() != nullptr) {
      return MakeSqlErrorAt(table_ref->alias())
             << "Alias outside UNNEST is not allowed when the argument inside "
                "the parenthesis has alias";
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveArrayZipMode(const ASTUnnestExpression* unnest,
                              ExprResolutionInfo* info) {
  const EnumType* array_zip_mode_type = types::ArrayZipModeEnumType();
  const ASTNamedArgument* array_zip_mode = unnest->array_zip_mode();
  std::unique_ptr<const ResolvedExpr> resolved_mode;
  if (array_zip_mode == nullptr) {
    // Ok to not specify an array zip mode. Supply a default 'PAD' mode if it's
    // multiway UNNEST syntax.
    if (unnest->expressions().size() > 1) {
      // We should have already validated in `ValidateUnnestAliases` that when
      // unnest contains more than 1 argument, the language feature is on.
      GOOGLESQL_RET_CHECK(language().LanguageFeatureEnabled(FEATURE_MULTIWAY_UNNEST));
      return MakeResolvedLiteralWithoutLocation(
          Value::Enum(array_zip_mode_type, functions::ArrayZipEnums::PAD));
    }
    return resolved_mode;
  }
  if (!language().LanguageFeatureEnabled(FEATURE_MULTIWAY_UNNEST)) {
    return MakeSqlErrorAt(array_zip_mode) << "Argument `mode` is not supported";
  }

  static constexpr absl::string_view kArrayZipModeArgName = "mode";
  if (!googlesql_base::CaseEqual(array_zip_mode->name()->GetAsStringView(),
                              kArrayZipModeArgName)) {
    return MakeSqlErrorAt(array_zip_mode)
           << "Unsupported named argument `"
           << array_zip_mode->name()->GetAsStringView()
           << "` in UNNEST; use `mode` instead";
  }

  if (unnest->expressions().size() == 1) {
    return MakeSqlErrorAt(array_zip_mode)
           << "Argument `mode` is not allowed when UNNEST only has one array "
              "argument";
  }
  if (IsNamedLambda(array_zip_mode)) {
    return MakeSqlErrorAt(array_zip_mode) << "Argument `mode` cannot be lambda";
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(array_zip_mode->expr(), info, &resolved_mode));

  // See if we need to coerce the type.
  if (resolved_mode->type()->Equals(array_zip_mode_type)) {
    return resolved_mode;
  }
  auto make_error_msg = [&](absl::string_view target_type_name,
                            absl::string_view actual_type_name) {
    return absl::Substitute(
        "Named argument `mode` used in UNNEST should have type $0, but got "
        "type $1",
        target_type_name, actual_type_name);
  };
  AnnotatedType annotated_target_type = {array_zip_mode_type,
                                         /*annotation_map=*/nullptr};
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(array_zip_mode->expr(),
                                   annotated_target_type, kImplicitCoercion,
                                   make_error_msg, &resolved_mode))
      .With(LocationOverride(array_zip_mode->expr()));
  return resolved_mode;
}

// Returns a more precise parsed location for UNNEST argument alias when the
// node is a path expression.
static const ASTNode* GetInferredAliasLocation(
    const ASTExpressionWithOptAlias* argument) {
  if (!argument->expression()->Is<ASTPathExpression>()) {
    return argument;
  }
  return argument->expression()->GetAsOrDie<ASTPathExpression>()->last_name();
}

// The alias could be returned empty. It will be allocated post-traversal of
// the resolved expr.
Resolver::UnnestArrayColumnAlias Resolver::GetArrayElementColumnAlias(
    const ASTExpressionWithOptAlias* argument) {
  if (argument->optional_alias() != nullptr) {
    return {/*alias=*/argument->optional_alias()->GetAsIdString(),
            /*alias_location=*/argument->optional_alias()};
  }
  return {/*alias=*/GetAliasForExpression(argument->expression()),
          /*alias_location=*/GetInferredAliasLocation(argument)};
}

absl::Status Resolver::ResolveArrayArgumentForExplicitUnnest(
    const ASTExpressionWithOptAlias* argument,
    UnnestArrayColumnAlias& arg_alias, int num_unnest_arguments,
    ExprResolutionInfo* info,
    std::vector<UnnestArrayColumnAlias>& output_alias_list,
    ResolvedColumnList& output_column_list,
    std::shared_ptr<NameList>& output_name_list,
    std::vector<std::unique_ptr<const ResolvedExpr>>& resolved_array_expr_list,
    std::vector<ResolvedColumn>& resolved_element_column_list) {
  std::unique_ptr<const ResolvedExpr> resolved_value_expr;
  const absl::Status resolve_expr_status =
      ResolveExpr(argument->expression(), info, &resolved_value_expr);

  // If resolving the expression failed, and it looked like a valid table
  // name, then give a more helpful error message.
  // TODO: b/315169608 - Find a better way to detect the desired error
  if (resolve_expr_status.code() == absl::StatusCode::kInvalidArgument &&
      absl::StartsWith(resolve_expr_status.message(), "Unrecognized name: ") &&
      argument->expression()->node_kind() == AST_PATH_EXPRESSION) {
    const ASTPathExpression* path_expr =
        argument->expression()->GetAsOrDie<ASTPathExpression>();
    const Table* table = nullptr;
    int num_names_consumed = 0;
    const absl::Status find_status = catalog_->FindTableWithPathPrefix(
        path_expr->ToIdentifierVector(), analyzer_options_.find_options(),
        &num_names_consumed, &table);

    if (find_status.ok()) {
      if (table != nullptr && num_names_consumed < path_expr->num_names()) {
        return MakeSqlErrorAt(path_expr)
               << "UNNEST cannot be applied on path expression with "
                  "non-correlated table name prefix "
               << table->FullName();
      }
      return MakeSqlErrorAt(path_expr)
             << "UNNEST cannot be applied on a table: "
             << path_expr->ToIdentifierPathString();
    }
    if (find_status.code() != absl::StatusCode::kNotFound) {
      GOOGLESQL_RETURN_IF_ERROR(find_status);
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(resolve_expr_status);  // Return original error.
  GOOGLESQL_RET_CHECK(resolved_value_expr != nullptr);
  const Type* value_type = resolved_value_expr->type();
  GOOGLESQL_RET_CHECK(value_type != nullptr);
  const Type* element_type = nullptr;
  if (value_type->IsArray()) {
    element_type = value_type->AsArray()->element_type();
  } else if (value_type->IsRow()) {
    element_type = value_type->AsRow()->element_type();
    if (num_unnest_arguments > 1) {
      return MakeSqlErrorAt(argument)
             << "Multi-argument UNNEST not allowed for value with ROW type";
    }
    if (!value_type->AsRow()->IsJoin()) {
      return MakeSqlErrorAt(argument)
             << "UNNEST not allowed for value with type "
             << value_type->ShortTypeName(product_mode())
             << " when the value is not a join column";
    }
  } else {
    return MakeSqlErrorAt(argument->expression())
           << "Values referenced in UNNEST must be arrays. "
           << "UNNEST contains expression of type "
           << value_type->ShortTypeName(product_mode());
  }
  GOOGLESQL_RET_CHECK(element_type != nullptr);

  // Compute alias if it's not provided in the user query or not inferrable from
  // the original path expression.
  if (arg_alias.alias.empty()) {
    arg_alias.alias = AllocateUnnestName();
  }

  const AnnotationMap* element_annotation = nullptr;
  if (resolved_value_expr->type_annotation_map() != nullptr) {
    element_annotation =
        resolved_value_expr->type_annotation_map()->AsStructMap()->field(0);
  }
  const ResolvedColumn array_element_column(
      AllocateColumnId(), /*table_name=*/kArrayId, /*name=*/arg_alias.alias,
      AnnotatedType(element_type, element_annotation));
  output_alias_list.emplace_back(arg_alias);
  output_column_list.emplace_back(array_element_column);
  resolved_array_expr_list.emplace_back(std::move(resolved_value_expr));
  resolved_element_column_list.emplace_back(array_element_column);

  absl::Status name_list_update_status = output_name_list->AddValueTableColumn(
      arg_alias.alias, array_element_column, arg_alias.alias_location);
  if (name_list_update_status.code() == absl::StatusCode::kInvalidArgument &&
      absl::StartsWith(name_list_update_status.message(), "Duplicate alias")) {
    return MakeSqlErrorAt(arg_alias.alias_location)
           << "Duplicate value table name `" << arg_alias.alias
           << "` found in UNNEST is not allowed";
  }
  return name_list_update_status;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveUnnest(
    const ASTTablePathExpression* table_ref, ExprResolutionInfo* info,
    std::vector<UnnestArrayColumnAlias>& output_alias_list,
    ResolvedColumnList& output_column_list,
    std::shared_ptr<NameList>& output_name_list,
    std::vector<std::unique_ptr<const ResolvedExpr>>& resolved_array_expr_list,
    std::vector<ResolvedColumn>& resolved_element_column_list) {
  const ASTUnnestExpression* unnest = table_ref->unnest_expr();
  GOOGLESQL_RET_CHECK(unnest != nullptr);

  if (unnest->expressions().size() > 1) {
    for (const ASTExpressionWithOptAlias* argument : unnest->expressions()) {
      UnnestArrayColumnAlias arg_alias = GetArrayElementColumnAlias(argument);
      GOOGLESQL_RETURN_IF_ERROR(ResolveArrayArgumentForExplicitUnnest(
          argument, arg_alias, unnest->expressions().size(), info,
          output_alias_list, output_column_list, output_name_list,
          resolved_array_expr_list, resolved_element_column_list));
    }
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(unnest->expressions().size() == 1);
  UnnestArrayColumnAlias arg_alias;
  // For the singleton UNNEST case, we respect alias outside the explicit UNNEST
  // parenthesis for backward compatibility.
  if (table_ref->alias() != nullptr) {
    arg_alias.alias = table_ref->alias()->GetAsIdString();
    arg_alias.alias_location = table_ref->alias();
  } else {
    // Point alias location to UNNEST by default for backward compatibility.
    arg_alias.alias_location = table_ref;
    if (language().LanguageFeatureEnabled(
            FEATURE_SINGLETON_UNNEST_INFERS_ALIAS)) {
      arg_alias.alias =
          GetAliasForExpression(unnest->expressions(0)->expression());
      arg_alias.alias_location =
          GetInferredAliasLocation(unnest->expressions(0));
    }
    if (language().LanguageFeatureEnabled(FEATURE_MULTIWAY_UNNEST) &&
        unnest->expressions(0)->optional_alias() != nullptr) {
      arg_alias.alias =
          unnest->expressions(0)->optional_alias()->GetAsIdString();
      arg_alias.alias_location = unnest->expressions(0)->optional_alias();
    }
  }

  return ResolveArrayArgumentForExplicitUnnest(
      unnest->expressions(0), arg_alias, unnest->expressions().size(), info,
      output_alias_list, output_column_list, output_name_list,
      resolved_array_expr_list, resolved_element_column_list);
}

absl::Status Resolver::ResolveTableClause(
    const ASTTableClause* ast_table_clause, const NameScope* external_scope,
    bool resolve_argument_first, const std::string& read_as_row_type_error_kind,
    std::unique_ptr<const ResolvedScan>* output_scan,
    std::shared_ptr<const NameList>* output_name_list) {
  if (ast_table_clause->where_clause() != nullptr) {
    return MakeSqlErrorAt(ast_table_clause)
           << "TABLE clause with WHERE is not supported";
  }

  const Table* table = nullptr;
  if (ast_table_clause->tvf() != nullptr) {
    // The TABLE clause represents a TVF call with arguments. Resolve the
    // TVF inside. Then add an identity projection to match the plan shape
    // expected by engines.
    // When X is a value table, TABLE X produces a scan of it as a value
    // table. (The same is true when we have TABLE tvf(...), and the tvf
    // returns a value table.)
    GOOGLESQL_RETURN_IF_ERROR(ResolveTVF(ast_table_clause->tvf(), external_scope,
                               /*pipe_input_arg=*/nullptr, output_scan,
                               output_name_list));
  } else {
    // Table name can be a WITH clause entry, a table-valued argument if
    // resolving inside a function, or a table from the <catalog_>.
    const ASTPathExpression* table_path = ast_table_clause->table_path();
    if (named_subquery_map_.contains(table_path->ToIdStringVector())) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveNamedSubqueryRef(table_path, /*hint=*/nullptr,
                                              output_scan, output_name_list));
    } else if (table_path->num_names() == 1 &&
               function_argument_info_ != nullptr &&
               function_argument_info_->FindTableArg(
                   table_path->first_name()->GetAsIdString()) != nullptr &&
               (resolve_argument_first ||
                !language().LanguageFeatureEnabled(
                    FEATURE_TABLE_SYNTAX_RESOLVE_ARGUMENT_LAST) ||
                catalog_->FindTable(table_path->ToIdentifierVector(), &table,
                                    analyzer_options_.find_options())
                        .code() == absl::StatusCode::kNotFound)) {
      GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsFunctionTableArgument(
          table_path, /*hint=*/nullptr, GetAliasForExpression(table_path),
          /*ast_location=*/ast_table_clause, output_scan, output_name_list));
    } else {
      std::unique_ptr<const ResolvedTableScan> table_scan;
      GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsTableScan(
          table_path, GetAliasForExpression(table_path),
          /*has_explicit_alias=*/false, /*alias_location=*/ast_table_clause,
          /*hints=*/nullptr, /*for_system_time=*/nullptr, external_scope,
          read_as_row_type_error_kind,
          /*remaining_names=*/nullptr, &table_scan, output_name_list,
          /*output_column_name_list=*/nullptr,
          resolved_columns_from_table_scans_));
      *output_scan = std::move(table_scan);
      if (table_path->num_names() == 1 && function_argument_info_ != nullptr &&
          function_argument_info_->FindTableArg(
              table_path->first_name()->GetAsIdString()) != nullptr) {
        // TODO: b/372326459 - Clean up this warning once the new resolution
        // order is rolled out, and there are no remaining references.
        GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
            ast_table_clause,
            DeprecationWarning::TABLE_SYNTAX_ARGUMENT_RESOLUTION_ORDER,
            "TABLE syntax was resolved as reference to a catalog table, when "
            "a function argument of the same name exists. This will change "
            "in the future to resolve as a reference to the function "
            "argument. If you wish to preserve this as a reference to a "
            "catalog table, rename the function argument to avoid the name "
            "collision"));
      }
    }
  }
  // Record the node source as TABLE clause. This allows the SQLBuilder to
  // render this scan as a TABLE clause.
  const_cast<ResolvedScan*>(output_scan->get())
      ->set_node_source(kNodeSourceTableClause);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveArrayScan(
    const ASTTablePathExpression* table_ref,
    std::optional<PathExpressionSpan> path_expr, const ASTOnClause* on_clause,
    const ASTUsingClause* using_clause, const ASTJoin* ast_join,
    bool is_outer_scan, bool include_lhs_name_list,
    bool is_single_table_array_path,
    std::unique_ptr<const ResolvedScan>* resolved_input_scan,
    const std::shared_ptr<const NameList>& name_list_input,
    const NameScope* scope, std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // There might not be a lhs_scan, but the unique_ptr should be non-NULL.
  GOOGLESQL_RET_CHECK(resolved_input_scan != nullptr);
  GOOGLESQL_RET_CHECK_EQ(*resolved_input_scan == nullptr, name_list_input == nullptr);

  for (const auto* op : table_ref->postfix_operators()) {
    if (language().LanguageFeatureEnabled(
            FEATURE_DISALLOW_PIVOT_AND_UNPIVOT_ON_ARRAY_SCANS) ||
        (op->node_kind() != AST_PIVOT_CLAUSE &&
         op->node_kind() != AST_UNPIVOT_CLAUSE)) {
      return MakeSqlErrorAt(op)
             << op->Name() << " is not allowed with array scans";
    }
    GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
        op, DeprecationWarning::PIVOT_OR_UNPIVOT_ON_ARRAY_SCAN,
        absl::StrCat(op->Name(),
                     " is not allowed with array scans. This will become "
                     "an error")));
  }

  // We have either an array reference or UNNEST.
  // These variables get set in either branch below.
  std::unique_ptr<const ResolvedExpr> resolved_value_expr;
  const Type* value_type = nullptr;
  // `mode` argument will only be set and used in explicit UNNEST syntax.
  std::unique_ptr<const ResolvedExpr> resolved_zip_mode;
  ResolvedColumnList output_column_list;
  if (*resolved_input_scan != nullptr && include_lhs_name_list) {
    output_column_list = (*resolved_input_scan)->column_list();
  }

  // Build a name list for correlated names.
  // TODO: b/315045184 - clean up shared_ptr usage.
  std::shared_ptr<NameList> name_list_lhs(new NameList);
  if (name_list_input != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(name_list_lhs->MergeFrom(*name_list_input, table_ref));
  }

  // Array aliases are always treated as explicit range variables,
  // even if computed.
  // This allows
  //   SELECT t.key, array1, array2
  //   FROM Table t, t.array1, array1.array2;
  // `array1` and `array2` are also available implicitly as columns on the
  // preceding scan, but the array scan makes them implicit.
  std::shared_ptr<NameList> name_list(new NameList);
  // `name_list_rhs` is only used if USING clause is present.
  std::shared_ptr<NameList> name_list_rhs(new NameList);

  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_array_expr_list;
  std::vector<ResolvedColumn> resolved_element_column_list;
  std::vector<UnnestArrayColumnAlias> output_alias_list;

  if (table_ref->path_expr() != nullptr) {
    GOOGLESQL_RET_CHECK(path_expr.has_value());

    // Single-word identifiers are always resolved as table names
    // and shouldn't have made it into ResolveArrayScan.
    GOOGLESQL_RET_CHECK_GE(path_expr->num_names(), 2);

    // Path expression in the FROM clause only resolves against non-aggregate
    // and non-analytic scope, so no aggregate function or window function is
    // allowed here.
    auto no_aggregation =
        std::make_unique<ExprResolutionInfo>(scope, "FROM clause");
    FlattenState::Restorer restorer;
    if (language().LanguageFeatureEnabled(FEATURE_UNNEST_AND_FLATTEN_ARRAYS)) {
      no_aggregation->flatten_state.set_can_flatten(true, &restorer);
    }

    NameTarget first_target;
    GOOGLESQL_RET_CHECK(scope->LookupName(path_expr->GetFirstIdString(), &first_target));

    switch (first_target.kind()) {
      case NameTarget::EXPLICIT_COLUMN:
      case NameTarget::RANGE_VARIABLE: {
        // These are the allowed cases.
        GOOGLESQL_RET_CHECK(!first_target.IsRangeVariable() ||
                  !first_target.is_pattern_variable())
            << "Pattern variable in ArrayScan";
        break;
      }
      case NameTarget::IMPLICIT_COLUMN: {
        // We disallowed this because the results were very confusing.
        //   FROM TableName, ColumnName.array_value
        // is not allowed.
        // Explicit column names are allowed in order to support chained
        // references like
        //   FROM Table t, t.Column.arr1 a1, a1.arr2 a2
        //
        // If we decide to make this allowed in normal mode, we would still
        // need to give an error if in_strict_mode().
        return MakeSqlErrorAtPoint(path_expr->GetParseLocationRange().start())
               << "Aliases referenced in the from clause must refer to "
                  "preceding scans, and cannot refer to columns on those "
                  "scans. "
               << path_expr->GetFirstIdString()
               << " refers to a column and must be qualified with a table "
                  "name.";
      }
      case NameTarget::FIELD_OF: {
        return MakeSqlErrorAtPoint(path_expr->GetParseLocationRange().start())
               << "Aliases referenced in the from clause must refer to "
                  "preceding scans, and cannot refer to columns or fields on "
                  "those scans. "
               << path_expr->GetFirstIdString()
               << " refers to a field and must be qualified with a table name.";
      }
      case NameTarget::ACCESS_ERROR: {
        PathExpressionSpan path_expr_span(path_expr.value());
        GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsExpression(
            path_expr_span, no_aggregation.get(), ResolvedStatement::READ,
            &resolved_value_expr));
        GOOGLESQL_RET_CHECK(path_expr_span.num_names() > 1);
        // This is the allowed case when the whole path can be resolved and
        // matched to a post-group by column, despite that the first name is
        // ACCESS_ERROR. For example:
        //
        // select tt.KitchenSink.repeated_int32_val,
        //        (select sum(e)
        //         from tt.KitchenSink.repeated_int32_val),  -- <- this ref
        // from TestTable tt
        // group by 1
        //
        // In this query, the reference to tt.KitchenSink.Repeated_int32_val
        // in the subquery is okay because tt.KitchenSink.Repeated_int32_val
        // is a group by key and thus a post-group by column. Any other
        // repeated field in KitchenSink would trigger the error above
        // because KitchenSink is not generally visible post GROUP BY.
        break;
      }
      case NameTarget::AMBIGUOUS: {
        // This can happen if the array name is ambiguous (resolves to a name
        // in more than one table previously in the FROM clause).
        return MakeSqlErrorAtPoint(path_expr->GetParseLocationRange().start())
               << path_expr->GetFirstIdString()
               << " ambiguously references multiple columns in previous FROM"
               << " clause tables";
      }
    }

    // Now we know we have an identifier path starting with a scan.
    // Resolve the remaining path to expand proto field accesses.
    PathExpressionSpan path_expr_span(path_expr.value());
    GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsExpression(
        path_expr_span, no_aggregation.get(), ResolvedStatement::READ,
        &resolved_value_expr));

    GOOGLESQL_RET_CHECK(resolved_value_expr != nullptr);
    value_type = resolved_value_expr->type();
    GOOGLESQL_RET_CHECK(value_type != nullptr);
    const Type* element_type = nullptr;
    if (value_type->IsArray()) {
      element_type = value_type->AsArray()->element_type();
    } else if (value_type->IsRow() && value_type->IsArrayLike()) {
      element_type = value_type->AsRow()->element_type();
    } else {
      return MakeSqlErrorAtPoint(path_expr->GetParseLocationRange().start())
             << "Values referenced in FROM clause must be arrays. "
             << path_expr->ToIdentifierPathString() << " has type "
             << value_type->ShortTypeName(product_mode());
    }
    GOOGLESQL_RET_CHECK(element_type != nullptr);

    IdString alias;
    const ASTNode* alias_location;
    if (table_ref->alias() != nullptr) {
      alias = table_ref->alias()->GetAsIdString();
      alias_location = table_ref->alias();
    } else {
      alias = GetAliasForExpression(table_ref->path_expr());
      alias_location = table_ref;
    }
    GOOGLESQL_RET_CHECK(!alias.empty());
    output_alias_list.emplace_back(
        UnnestArrayColumnAlias{alias, alias_location});

    const AnnotationMap* element_annotation = nullptr;
    if (resolved_value_expr->type_annotation_map() != nullptr) {
      element_annotation =
          resolved_value_expr->type_annotation_map()->AsStructMap()->field(0);
    }
    const ResolvedColumn array_element_column(
        AllocateColumnId(), /*table_name=*/kArrayId, /*name=*/alias,
        AnnotatedType(element_type, element_annotation));

    GOOGLESQL_RETURN_IF_ERROR(name_list_rhs->AddValueTableColumn(
        alias, array_element_column, alias_location));
    output_column_list.emplace_back(array_element_column);
    resolved_array_expr_list.push_back(std::move(resolved_value_expr));
    resolved_element_column_list.push_back(array_element_column);
  } else {
    GOOGLESQL_RET_CHECK(table_ref->unnest_expr() != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateUnnestAliases(table_ref));

    auto info = std::make_unique<ExprResolutionInfo>(scope, "UNNEST");
    FlattenState::Restorer restorer;
    if (language().LanguageFeatureEnabled(FEATURE_UNNEST_AND_FLATTEN_ARRAYS)) {
      info->flatten_state.set_can_flatten(true, &restorer);
    }

    const ASTUnnestExpression* unnest = table_ref->unnest_expr();
    GOOGLESQL_RETURN_IF_ERROR(ResolveUnnest(
        table_ref, info.get(), output_alias_list, output_column_list,
        name_list_rhs, resolved_array_expr_list, resolved_element_column_list));
    GOOGLESQL_RET_CHECK_EQ(output_alias_list.size(), resolved_element_column_list.size());
    GOOGLESQL_RET_CHECK_EQ(resolved_array_expr_list.size(),
                 resolved_element_column_list.size());
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_zip_mode,
                     ResolveArrayZipMode(unnest, info.get()));
  }

  // Resolve WITH OFFSET if present.
  std::unique_ptr<ResolvedColumnHolder> array_position_column;
  if (table_ref->with_offset() != nullptr) {
    // WITH OFFSET is not allowed when scanning RowTypes since joins don't
    // have an order.  Having more than one input expr gave an error earlier if
    // we have RowTypes so it will always be first if it exists.
    GOOGLESQL_RET_CHECK(!resolved_array_expr_list.empty());
    if (resolved_array_expr_list.front()->type()->IsRow()) {
      return MakeSqlErrorAt(table_ref->with_offset())
             << "WITH OFFSET is not allowed when scanning values with ROW type";
    }

    // If the alias is NULL, we get "offset" as an implicit alias.
    const ASTAlias* with_offset_alias = table_ref->with_offset()->alias();
    const IdString offset_alias =
        (with_offset_alias == nullptr ? kOffsetAlias
                                      : with_offset_alias->GetAsIdString());

    const ResolvedColumn column(AllocateColumnId(),
                                /*table_name=*/kArrayOffsetId,
                                /*name=*/offset_alias,
                                AnnotatedType(type_factory_->get_int64(),
                                              /*annotation_map=*/nullptr));
    array_position_column = MakeResolvedColumnHolder(column);
    output_column_list.push_back(array_position_column->column());

    // We add the offset column as a value table column so its name acts
    // like a range variable and we get an error if it conflicts with
    // other range variables in the same FROM clause.
    GOOGLESQL_RETURN_IF_ERROR(name_list_rhs->AddValueTableColumn(
        offset_alias, array_position_column->column(),
        with_offset_alias != nullptr
            ? absl::implicit_cast<const ASTNode*>(with_offset_alias)
            : table_ref->with_offset()));
  }

  std::unique_ptr<const ResolvedExpr> resolved_condition;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns;

  if (using_clause != nullptr) {
    GOOGLESQL_RET_CHECK(on_clause == nullptr);  // Can't have both.

    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        lhs_computed_columns;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        rhs_computed_columns;

    GOOGLESQL_RETURN_IF_ERROR(ResolveUsing(
        using_clause, *name_list_lhs, *name_list_rhs,
        is_outer_scan ? ResolvedJoinScan::LEFT : ResolvedJoinScan::INNER,
        /*is_array_scan=*/true, &lhs_computed_columns, &rhs_computed_columns,
        &computed_columns, name_list.get(), &resolved_condition));

    // Add the USING columns to the <output_column_list>.  These will show
    // up in SELECT *, and can be referenced unqualified (without a table
    // alias qualifier).
    for (const std::unique_ptr<const ResolvedComputedColumn>& column :
         lhs_computed_columns) {
      output_column_list.push_back(column->column());
    }

    // Add a Project for any columns we need to computed before the join.
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForComputedColumns(
        std::move(lhs_computed_columns), resolved_input_scan));
    // The <rhs_computed_columns> and <computed_columns> should be empty as
    // ArrayScan has no notion of right or full outer join.
    GOOGLESQL_RET_CHECK(rhs_computed_columns.empty());
    GOOGLESQL_RET_CHECK(computed_columns.empty());
  } else {
    GOOGLESQL_RETURN_IF_ERROR(name_list->MergeFrom(*name_list_lhs, table_ref));
    // We explicitly add the array element and offset columns to the name_list
    // instead of merging name_list_rhs to get the exact error location.
    for (int i = 0; i < resolved_element_column_list.size(); ++i) {
      GOOGLESQL_RETURN_IF_ERROR(name_list->AddValueTableColumn(
          output_alias_list[i].alias, resolved_element_column_list[i],
          output_alias_list[i].alias_location));
    }

    if (array_position_column != nullptr) {
      const ASTAlias* with_offset_alias = table_ref->with_offset()->alias();
      GOOGLESQL_RETURN_IF_ERROR(name_list->AddValueTableColumn(
          with_offset_alias == nullptr ? kOffsetAlias
                                       : with_offset_alias->GetAsIdString(),
          array_position_column->column(),
          with_offset_alias != nullptr
              ? absl::implicit_cast<const ASTNode*>(with_offset_alias)
              : table_ref->with_offset()));
    }

    if (on_clause != nullptr) {
      const std::unique_ptr<const NameScope> on_clause_scope(
          new NameScope(scope, name_list));
      GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(on_clause->expression(),
                                        on_clause_scope.get(), "ON clause",
                                        &resolved_condition));
      GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(on_clause->expression(),
                                       "JOIN ON clause", &resolved_condition));
    }
  }

  std::unique_ptr<ResolvedArrayScan> resolved_array_scan =
      MakeResolvedArrayScan(output_column_list, std::move(*resolved_input_scan),
                            std::move(resolved_array_expr_list),
                            std::move(resolved_element_column_list),
                            std::move(array_position_column),
                            std::move(resolved_condition), is_outer_scan,
                            std::move(resolved_zip_mode));

  if (!language().LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING) &&
      is_outer_scan && ast_join->join_type() == ASTJoin::LEFT) {
    GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
        NameListPtr(name_list), table_ref, "LEFT JOIN", product_mode()));
  }

  // We can have hints attached to either or both the JOIN keyword for this
  // array scan and the table_path_expr for the array itself.
  // Add hints from both places onto the ResolvedArrayScan node.
  if (ast_join != nullptr) {
    // If we have HASH JOIN or LOOKUP JOIN on an array join, we still add those
    // hints on the ArrayScan node even though they may have no meaning.
    GOOGLESQL_RETURN_IF_ERROR(
        MaybeAddJoinHintKeyword(ast_join, resolved_array_scan.get()));

    GOOGLESQL_RETURN_IF_ERROR(
        ResolveHintsForNode(ast_join->hint(), resolved_array_scan.get()));
  }
  if (table_ref->hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveHintsForNode(table_ref->hint(), resolved_array_scan.get()));
  }

  // For the unique shape of FROM table.array_path, attach informational node
  // source which has no functionality impact.
  if (is_single_table_array_path) {
    resolved_array_scan->set_node_source(kNodeSourceSingleTableArrayNamePath);
  }

  *output = std::move(resolved_array_scan);
  // If we created a CAST expression for JOIN with USING, we create a wrapper
  // ProjectScan node to produce additional columns for those expressions.
  GOOGLESQL_RETURN_IF_ERROR(
      MaybeAddProjectForComputedColumns(std::move(computed_columns), output));
  *output_name_list = name_list;
  return absl::OkStatus();
}

void Resolver::MaybeRecordParseLocation(const ASTNode* ast_location,
                                        ResolvedNode* resolved_node) const {
  if (analyzer_options_.parse_location_record_type() !=
          PARSE_LOCATION_RECORD_NONE &&
      ast_location != nullptr) {
    resolved_node->SetParseLocationRange(ast_location->location());
  }
}

void Resolver::MaybeRecordParseLocation(
    const ParseLocationRange& parse_location,
    ResolvedNode* resolved_node) const {
  if (analyzer_options_.parse_location_record_type() !=
          PARSE_LOCATION_RECORD_NONE &&
      parse_location.IsValid()) {
    resolved_node->SetParseLocationRange(parse_location);
  }
}

void Resolver::MaybeRecordExpressionSubqueryParseLocation(
    const ASTExpressionSubquery* ast_expr_subquery,
    ResolvedNode* resolved_node) const {
  switch (analyzer_options_.parse_location_record_type()) {
    case PARSE_LOCATION_RECORD_FULL_NODE_SCOPE:
      MaybeRecordParseLocation(ast_expr_subquery, resolved_node);
      break;
    case PARSE_LOCATION_RECORD_CODE_SEARCH:
      MaybeRecordParseLocation(ast_expr_subquery->query(), resolved_node);
      break;
    case PARSE_LOCATION_RECORD_NONE:  // NO-OP
      break;
  }
}

void Resolver::MaybeRecordTVFCallParseLocation(
    const ASTTVF* ast_location, ResolvedNode* resolved_node) const {
  switch (analyzer_options_.parse_location_record_type()) {
    case PARSE_LOCATION_RECORD_FULL_NODE_SCOPE:
      MaybeRecordParseLocation(ast_location, resolved_node);
      break;
    case PARSE_LOCATION_RECORD_CODE_SEARCH:
      MaybeRecordParseLocation(ast_location->name(), resolved_node);
      break;
    case PARSE_LOCATION_RECORD_NONE:  // NO-OP
      break;
  }
}

void Resolver::MaybeRecordAnalyticFunctionCallParseLocation(
    const ASTAnalyticFunctionCall* ast_location,
    ResolvedNode* resolved_node) const {
  if (ast_location == nullptr) {
    return;
  }
  switch (analyzer_options_.parse_location_record_type()) {
    case PARSE_LOCATION_RECORD_FULL_NODE_SCOPE:
      MaybeRecordParseLocation(ast_location, resolved_node);
      break;
    case PARSE_LOCATION_RECORD_CODE_SEARCH:
      MaybeRecordParseLocation(ast_location->function()->function(),
                               resolved_node);
      break;
    case PARSE_LOCATION_RECORD_NONE:  // NO-OP
      break;
  }
}

void Resolver::MaybeRecordFunctionCallParseLocation(
    const ASTFunctionCall* ast_location, ResolvedNode* resolved_node) const {
  if (ast_location == nullptr) {
    return;
  }
  switch (analyzer_options_.parse_location_record_type()) {
    case PARSE_LOCATION_RECORD_FULL_NODE_SCOPE:
      MaybeRecordParseLocation(ast_location, resolved_node);
      break;
    case PARSE_LOCATION_RECORD_CODE_SEARCH:
      MaybeRecordParseLocation(ast_location->function(), resolved_node);
      break;
    case PARSE_LOCATION_RECORD_NONE:  // NO-OP
      break;
  }
}

void Resolver::MaybeRecordFieldAccessParseLocation(
    const ParseLocationRange& parse_location, const ASTIdentifier* ast_field,
    ResolvedExpr* resolved_field_access_expr) const {
  switch (analyzer_options_.parse_location_record_type()) {
    case PARSE_LOCATION_RECORD_CODE_SEARCH:
      MaybeRecordParseLocation(ast_field, resolved_field_access_expr);
      break;
    case PARSE_LOCATION_RECORD_FULL_NODE_SCOPE: {
      if (ast_field == nullptr) {
        MaybeRecordParseLocation(parse_location, resolved_field_access_expr);
      } else if (!parse_location.IsValid()) {
        MaybeRecordParseLocation(ast_field, resolved_field_access_expr);
      } else {
        ParseLocationRange range;
        range.set_start(parse_location.start());
        range.set_end(ast_field->location().end());
        resolved_field_access_expr->SetParseLocationRange(range);
      }
      break;
    }
    case PARSE_LOCATION_RECORD_NONE:  // NO-OP
      break;
  }
}

void Resolver::RecordArgumentParseLocationsIfPresent(
    const ASTFunctionParameter& function_argument,
    FunctionArgumentTypeOptions* options) const {
  if (analyzer_options_.parse_location_record_type() ==
      PARSE_LOCATION_RECORD_NONE) {
    return;
  }

  if (function_argument.name() != nullptr) {
    options->set_argument_name_parse_location(
        function_argument.name()->location());
  }
  if (function_argument.type() != nullptr) {
    options->set_argument_type_parse_location(
        function_argument.type()->location());
  } else if (function_argument.templated_parameter_type() != nullptr) {
    options->set_argument_type_parse_location(
        function_argument.templated_parameter_type()->location());
  } else if (function_argument.tvf_schema() != nullptr) {
    options->set_argument_type_parse_location(
        function_argument.tvf_schema()->location());
  }
}

void Resolver::RecordTVFRelationColumnParseLocationsIfPresent(
    const ASTTVFSchemaColumn& tvf_schema_column, TVFRelation::Column* column) {
  if (analyzer_options_.parse_location_record_type() ==
      PARSE_LOCATION_RECORD_NONE) {
    return;
  }
  // Column name is an optional field.
  if (tvf_schema_column.name() != nullptr) {
    column->name_parse_location_range = tvf_schema_column.name()->location();
  }
  column->type_parse_location_range = tvf_schema_column.type()->location();
}

absl::Status Resolver::ResolveModel(
    const ASTPathExpression* path_expr,
    std::unique_ptr<const ResolvedModel>* resolved_model) {
  const Model* model = nullptr;
  const absl::Status find_status =
      catalog_->FindModel(path_expr->ToIdentifierVector(), &model,
                          analyzer_options_.find_options());

  if (find_status.code() == absl::StatusCode::kNotFound) {
    return MakeSqlErrorAt(path_expr)
           << "Model not found: " << path_expr->ToIdentifierPathString();
  }
  GOOGLESQL_RETURN_IF_ERROR(find_status);

  *resolved_model = MakeResolvedModel(model);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDescriptorFirstPass(
    const ASTDescriptorColumnList* column_list,
    std::unique_ptr<const ResolvedDescriptor>* resolved_descriptor) {
  std::vector<std::string> descriptor_column_name_list;
  for (const ASTDescriptorColumn* const descriptor_column :
       column_list->descriptor_column_list()) {
    descriptor_column_name_list.push_back(
        descriptor_column->name()->GetAsString());
  }
  *resolved_descriptor = MakeResolvedDescriptor(std::vector<ResolvedColumn>(),
                                                descriptor_column_name_list);
  return ::absl::OkStatus();
}

absl::Status Resolver::FinishResolvingDescriptor(
    const ASTTVFArgument* ast_tvf_argument,
    const std::unique_ptr<const NameScope>& name_scope, int sig_idx,
    std::unique_ptr<const ResolvedDescriptor>* resolved_descriptor) {
  std::vector<ResolvedColumn> descriptor_column_list;
  std::vector<std::string> descriptor_column_name_list(
      resolved_descriptor->get()->descriptor_column_name_list());

  // resolve descriptor names from input table.
  for (int i = 0;
       i < resolved_descriptor->get()->descriptor_column_name_list().size();
       i++) {
    const std::string& name =
        resolved_descriptor->get()->descriptor_column_name_list()[i];
    NameTarget target;

    if (!name_scope->LookupName(id_string_pool_->Make(name), &target)) {
      return MakeSqlErrorAt(ast_tvf_argument->descriptor()
                                ->columns()
                                ->descriptor_column_list()
                                .at(i))
             << "DESCRIPTOR specifies " << name
             << ", which does not exist in the table passed as argument "
             << sig_idx + 1;
    } else if (target.IsAmbiguous()) {
      return MakeSqlErrorAt(ast_tvf_argument->descriptor()
                                ->columns()
                                ->descriptor_column_list()
                                .at(i))
             << "DESCRIPTOR specifies " << name
             << ", which is ambiguous in the table passed as argument "
             << sig_idx + 1;
    } else {
      descriptor_column_list.push_back(target.column());
    }
  }

  *resolved_descriptor = MakeResolvedDescriptor(descriptor_column_list,
                                                descriptor_column_name_list);
  return ::absl::OkStatus();
}

absl::Status Resolver::ResolveConnection(
    const ASTExpression* path_expr_or_default,
    std::unique_ptr<const ResolvedConnection>* resolved_connection,
    bool is_default_connection_allowed) {
  if (path_expr_or_default->Is<ASTPathExpression>()) {
    return ResolveConnectionPath(
        path_expr_or_default->GetAsOrDie<ASTPathExpression>(),
        resolved_connection);
  }
  GOOGLESQL_RET_CHECK(path_expr_or_default->Is<ASTDefaultLiteral>());
  if (!is_default_connection_allowed) {
    return MakeSqlErrorAt(path_expr_or_default)
           << "CONNECTION DEFAULT is not supported";
  }
  return ResolveDefaultConnection(
      path_expr_or_default->GetAsOrDie<ASTDefaultLiteral>(),
      resolved_connection);
}

absl::Status Resolver::ResolveConnectionPath(
    const ASTPathExpression* path_expr,
    std::unique_ptr<const ResolvedConnection>* resolved_connection) {
  const Connection* connection = nullptr;
  const absl::Status find_status =
      catalog_->FindConnection(path_expr->ToIdentifierVector(), &connection,
                               analyzer_options_.find_options());

  if (find_status.code() == absl::StatusCode::kNotFound) {
    return MakeSqlErrorAt(path_expr)
           << "Connection not found: " << path_expr->ToIdentifierPathString();
  }
  GOOGLESQL_RETURN_IF_ERROR(find_status);

  *resolved_connection = MakeResolvedConnection(connection);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDefaultConnection(
    const ASTDefaultLiteral* default_literal,
    std::unique_ptr<const ResolvedConnection>* resolved_connection) {
  const Connection* connection = nullptr;
  const absl::Status find_status = catalog_->FindConnection(
      {"$connection_default"}, &connection, analyzer_options_.find_options());
  if (find_status.code() == absl::StatusCode::kNotFound) {
    return MakeSqlErrorAt(default_literal) << "Default connection not found";
  }
  GOOGLESQL_RETURN_IF_ERROR(find_status);

  *resolved_connection = MakeResolvedConnection(connection);
  return absl::OkStatus();
}

bool Resolver::IsPathExpressionStartingFromNamedSubquery(
    const ASTPathExpression& path_expr) const {
  std::vector<IdString> path;
  path.reserve(path_expr.num_names() - 1);
  for (int i = 0; i < path_expr.num_names() - 1; ++i) {
    path.push_back(path_expr.names().at(i)->GetAsIdString());
    if (named_subquery_map_.contains(path)) {
      return true;
    }
  }
  return false;
}

static bool IsColumnOfTableArgument(const ASTPathExpression& path_expr,
                                    const FunctionArgumentInfo* arguments) {
  if (arguments == nullptr) {
    return false;  // No arguments are in scope.
  }
  if (path_expr.num_names() < 2) {
    return false;
  }
  const FunctionArgumentInfo::ArgumentDetails* details =
      arguments->FindTableArg(path_expr.first_name()->GetAsIdString());
  if (details == nullptr || details->arg_type.IsTemplated()) {
    return false;
  }
  const TVFRelation& table =
      details->arg_type.options().relation_input_schema();
  for (int i = 0; i < table.num_columns(); ++i) {
    const TVFSchemaColumn& column = table.column(i);
    if (googlesql_base::CaseEqual(
            path_expr.name(1)->GetAsIdString().ToStringView(), column.name)) {
      return true;
    }
  }
  return false;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::MakeScanForTable(
    const ASTNode* ast_location, const Table* table, IdString alias,
    bool has_explicit_alias, const ASTNode* alias_location,
    const ASTForSystemTime* for_system_time,
    absl::string_view read_as_row_type_error_kind,
    std::unique_ptr<ResolvedTableScan>* output_table_scan,
    NameListPtr* output_name_list,
    NameListPtr* /*absl_nullable*/ output_column_name_list,
    ResolvedColumnToCatalogColumnHashMap&
        out_resolved_columns_to_catalog_columns_for_scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(!alias.empty());
  GOOGLESQL_RET_CHECK(alias_location != nullptr);

  GOOGLESQL_RET_CHECK(table != nullptr);
  const IdString table_name = MakeIdString(table->Name());

  bool is_value_table = table->IsValueTable();
  if (is_value_table) {
    GOOGLESQL_RETURN_IF_ERROR(CheckValidValueTable(ast_location, *table));
  }

  ResolvedColumnList column_list;
  std::shared_ptr<NameList> name_list(new NameList);
  bool has_measure = false;

  bool read_as_row_type = false;
  if (!table->HasColumnList()) {
    if (!language().LanguageFeatureEnabled(FEATURE_ROW_TYPE)) {
      return MakeSqlErrorAt(ast_location)
             << "Table with non-default ColumnListMode not supported: "
             << table_name;
    }

    // TODO Check all callers that pass this and see if they should
    // allow passing ROW types.
    if (!read_as_row_type_error_kind.empty()) {
      return MakeSqlErrorAt(ast_location)
             << "Tables with ROW type don't support "
             << read_as_row_type_error_kind;
    }

    const RowType* row_type;
    GOOGLESQL_RETURN_IF_ERROR(
        type_factory_->MakeRowType(table, table->FullName(), &row_type));

    column_list.emplace_back(
        ResolvedColumn(AllocateColumnId(), table_name, kRowId, row_type));

    // TODO Handle actual values tables?
    GOOGLESQL_RET_CHECK(!is_value_table)
        << "TableScan with ROW type cannot be a value table yet";
    is_value_table = true;
    read_as_row_type = true;
  } else {
    for (int i = 0; i < table->NumColumns(); ++i) {
      const Column* column = table->GetColumn(i);
      IdString column_name = MakeIdString(column->Name());
      if (column_name.empty()) {
        column_name = MakeIdString(absl::StrCat("$col", i + 1));
      }
      if (column->GetType()->IsRow() &&
          !language().LanguageFeatureEnabled(FEATURE_ROW_TYPE)) {
        return MakeSqlErrorAt(ast_location)
               << "Table " << table->FullName() << " has column "
               << column->Name() << " with unsupported ROW type";
      }
      if (column->GetType()->IsMeasureType()) {
        has_measure = true;
      }

      const Type* resolved_column_type = column->GetType();
      if (resolved_column_type->IsMeasureType()) {
        // Each measure column definition has its own MeasureType. For example,
        // in
        //
        // ```sql
        // ...
        // FROM (
        //   SELECT m AS m1, m AS m2
        //   FROM T
        // )
        // ...
        // ```
        //
        // m1 and m2 will have different MeasureTypes. Measure-typed
        // ResolvedColumns have the same type iff they originate from the same
        // measure column definition. This allows us to track the measure
        // column propagation more easily.
        GOOGLESQL_ASSIGN_OR_RETURN(resolved_column_type,
                         type_factory_->MakeMeasureType(
                             resolved_column_type->AsMeasure()->result_type()));
      }
      column_list.emplace_back(ResolvedColumn(
          AllocateColumnId(), table_name, column_name,
          AnnotatedType(resolved_column_type, column->GetTypeAnnotationMap())));
      // Save the Catalog column for this ResolvedColumn so it can later be used
      // for checking column properties like Column::IsWritableColumn().
      out_resolved_columns_to_catalog_columns_for_scan[column_list.back()] =
          column;
      if (column->IsPseudoColumn()) {
        GOOGLESQL_RETURN_IF_ERROR(name_list->AddPseudoColumn(
            column_name, column_list.back(), ast_location));
      } else if (is_value_table) {
        GOOGLESQL_RET_CHECK_EQ(i, 0);  // Verified in CheckValidValueTable.
        // Defer AddValueTableColumn until after adding the pseudo-columns.
      } else {
        // is_explicit=false because we're adding all columns of a table.
        GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(column_name, column_list.back(),
                                             /*is_explicit=*/false));
      }
    }

    if (has_measure) {
      const std::string measure_source =
          absl::StrCat("table ", table_name.ToStringView());
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateMeasureSource(ast_location, table, measure_source));
    }
  }

  // `output_column_name_list` collects the names corresponds to the table
  // columns; this is different from `output_name_list` which contains range
  // variables (and may override the names collides with range variables).
  if (output_column_name_list != nullptr) {
    auto column_name_list = std::make_shared<NameList>();
    GOOGLESQL_RETURN_IF_ERROR(column_name_list->MergeFrom(*name_list, ast_location));
    *output_column_name_list = std::move(column_name_list);
  }

  if (is_value_table) {
    // So far, we've accumulated the pseudo-columns only.  Now add the
    // value table column, and pass in the list of pseudo-columns so they
    // can be attached to the range variable for the value table.
    GOOGLESQL_RET_CHECK_EQ(name_list->num_columns(), 0);
    GOOGLESQL_RETURN_IF_ERROR(
        name_list->AddValueTableColumn(alias, column_list[0], ast_location,
                                       /*excluded_field_names=*/{}, name_list));
    GOOGLESQL_RETURN_IF_ERROR(name_list->SetIsValueTable());
  }

  std::unique_ptr<const ResolvedExpr> for_system_time_expr;
  if (for_system_time != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveForSystemTimeExpr(for_system_time, &for_system_time_expr));
  }

  const ASTLockMode* lock_mode = GetActiveLockModeOrNull();
  std::unique_ptr<const ResolvedLockMode> resolved_lock_mode;
  if (lock_mode != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveLockMode(lock_mode, &resolved_lock_mode));
  }

  std::unique_ptr<ResolvedTableScan> table_scan =
      MakeResolvedTableScan(column_list, table, std::move(for_system_time_expr),
                            has_explicit_alias ? alias.ToString() : "");
  table_scan->set_lock_mode(std::move(resolved_lock_mode));
  if (read_as_row_type) {
    table_scan->set_read_as_row_type(true);
    // The column list always scans the ROW type value initially.
    // It might get pruned away later.
    GOOGLESQL_RET_CHECK_EQ(column_list.size(), 1);
  } else {
    std::vector<int> column_index_list(column_list.size());
    // Fill column_index_list with 0, 1, 2, ..., column_list.size()-1.
    std::iota(column_index_list.begin(), column_index_list.end(), 0);
    table_scan->set_column_index_list(column_index_list);
  }

  // The number of columns should equal the number of regular columns plus
  // the number of pseudo-columns in name_list, but we don't maintain the
  // count of pseudo-columns so we can't check that exactly.
  GOOGLESQL_RET_CHECK_GE(table_scan->column_list_size(), name_list->num_columns());

  // Add a range variable for the whole scan unless this is a value table. For
  // value tables, the column's name already serves that purpose.
  if (!is_value_table) {
    GOOGLESQL_ASSIGN_OR_RETURN(name_list, NameList::AddRangeVariableInWrappingNameList(
                                    alias, alias_location, name_list));
  }

  *output_table_scan = std::move(table_scan);
  *output_name_list = std::move(name_list);
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePathExpressionAsTableScan(
    const ASTPathExpression* path_expr, IdString alias, bool has_explicit_alias,
    const ASTNode* alias_location, const ASTHint* hints,
    const ASTForSystemTime* for_system_time, const NameScope* scope,
    const std::string& read_as_row_type_error_kind,
    std::unique_ptr<PathExpressionSpan>* remaining_names,
    std::unique_ptr<const ResolvedTableScan>* output,
    std::shared_ptr<const NameList>* output_name_list,
    NameListPtr* /*absl_nullable*/ output_column_name_list,
    ResolvedColumnToCatalogColumnHashMap&
        out_resolved_columns_to_catalog_columns_for_scan) {
  GOOGLESQL_RET_CHECK(output != nullptr);
  GOOGLESQL_RET_CHECK(output_name_list != nullptr);
  GOOGLESQL_RET_CHECK(path_expr != nullptr);
  GOOGLESQL_RET_CHECK(!alias.empty());
  GOOGLESQL_RET_CHECK(alias_location != nullptr);

  if (analyzing_partition_by_clause_name_ != nullptr) {
    return MakeSqlErrorAt(path_expr)
           << analyzing_partition_by_clause_name_
           << " expression cannot contain a table scan";
  }

  // Check if the table exists.
  const Table* table = nullptr;
  int num_names_consumed = 0;
  const absl::Status find_status =
      remaining_names != nullptr
          ? catalog_->FindTableWithPathPrefix(path_expr->ToIdentifierVector(),
                                              analyzer_options_.find_options(),
                                              &num_names_consumed, &table)
          : catalog_->FindTable(path_expr->ToIdentifierVector(), &table,
                                analyzer_options_.find_options());
  if (find_status.code() == absl::StatusCode::kNotFound) {
    if (const TableValuedFunction* tvf_catalog_entry = nullptr;
        analyzer_options()
            .replace_table_not_found_error_with_tvf_error_if_applicable() &&
        catalog_
            ->FindTableValuedFunction(path_expr->ToIdentifierVector(),
                                      &tvf_catalog_entry,
                                      analyzer_options().find_options())
            .ok()) {
      if (tvf_catalog_entry->NumSignatures() > 0 &&
          !tvf_catalog_entry->GetSignature(0)->arguments().empty()) {
        return MakeSqlErrorAt(path_expr)
               << "Table-valued function must be called with an argument list: "
                  "`"
               << tvf_catalog_entry->SQLName() << "(...)`";
      } else {
        return MakeSqlErrorAt(path_expr)
               << "Table-valued function with no parameters must be called "
                  "with an empty argument list: `"
               << tvf_catalog_entry->SQLName() << "()`";
      }
    }
    std::string error_message;
    absl::StrAppend(&error_message,
                    "Table not found: ", path_expr->ToIdentifierPathString());

    // We didn't find the name when trying to resolve it as a table.
    // If it looks like it might have been intended as a name from the scope,
    // give a more helpful error.
    if (IsPathExpressionStartingFromScope(*path_expr, *scope)) {
      absl::StrAppend(
          &error_message,
          " (Unqualified identifiers in a FROM clause are always resolved "
          "as tables. Identifier ",
          ToIdentifierLiteral(path_expr->first_name()->GetAsIdString()),
          " is in scope but unqualified names cannot be resolved here.)");
    } else if (IsPathExpressionStartingFromNamedSubquery(*path_expr)) {
      absl::StrAppend(
          &error_message, "; Table name ", path_expr->ToIdentifierPathString(),
          " starts with a WITH clause alias and references a column from that",
          " table, which is invalid in the FROM clause");
    } else if (IsColumnOfTableArgument(*path_expr, function_argument_info_)) {
      absl::StrAppend(
          &error_message, "; Table name ", path_expr->ToIdentifierPathString(),
          " starts with a TVF table-valued argument name and references a ",
          "column from that table, which is invalid in the FROM clause");
    } else {
      const std::string table_suggestion =
          catalog_->SuggestTable(path_expr->ToIdentifierVector());
      if (!table_suggestion.empty()) {
        absl::StrAppend(&error_message, "; Did you mean ", table_suggestion,
                        "?");
      }
    }
    return MakeSqlErrorAt(path_expr) << error_message;
  }
  GOOGLESQL_RETURN_IF_ERROR(find_status);

  bool is_partial_name_path = remaining_names != nullptr &&
                              path_expr->num_names() != num_names_consumed;
  if (is_partial_name_path) {
    GOOGLESQL_RET_CHECK_GT(num_names_consumed, 0);
    alias = path_expr->name(num_names_consumed - 1)->GetAsIdString();
    GOOGLESQL_ASSIGN_OR_RETURN(auto suffix, PathExpressionSpan(*path_expr)
                                      .subspan(num_names_consumed - 1,
                                               path_expr->num_names()));
    *remaining_names = std::make_unique<PathExpressionSpan>(suffix);
  }

  std::unique_ptr<ResolvedTableScan> table_scan;
  std::shared_ptr<const NameList> name_list;

  GOOGLESQL_RETURN_IF_ERROR(MakeScanForTable(
      path_expr, table, alias, has_explicit_alias, alias_location,
      for_system_time, read_as_row_type_error_kind, &table_scan, &name_list,
      output_column_name_list,
      out_resolved_columns_to_catalog_columns_for_scan));

  GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(hints, table_scan.get()));

  *output_name_list = name_list;
  MaybeRecordParseLocation(path_expr, table_scan.get());
  *output = std::move(table_scan);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveForSystemTimeExpr(
    const ASTForSystemTime* for_system_time,
    std::unique_ptr<const ResolvedExpr>* resolved) {
  // Resolve against an empty NameScope, because column references don't
  // make sense in this clause (it needs to be constant).
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(for_system_time->expression(),
                                    empty_name_scope_.get(),
                                    "FOR SYSTEM_TIME AS OF", resolved));

  // Try to coerce STRING literals to TIMESTAMP, but ignore error if it
  // didn't work - proper error will be raised below.
  if (((*resolved)->node_kind() == RESOLVED_LITERAL &&
       (*resolved)->type()->IsString())) {
    CoerceExprToType(for_system_time, type_factory_->get_timestamp(),
                     kExplicitCoercion, resolved)
        .IgnoreError();  // TODO
  }

  if (!(*resolved)->type()->IsTimestamp()) {
    return MakeSqlErrorAt(for_system_time->expression())
           << "FOR SYSTEM_TIME AS OF must be of type TIMESTAMP but was of "
              "type "
           << (*resolved)->type()->ShortTypeName(product_mode());
  }
  return absl::OkStatus();
}

absl::Status Resolver::CoerceQueryStatementResultToTypes(
    const ASTNode* ast_node, absl::Span<const Type* const> types,
    std::unique_ptr<const ResolvedScan>* scan,
    std::shared_ptr<const NameList>* output_name_list) {
  const std::vector<NamedColumn>& column_list = (*output_name_list)->columns();
  if (types.size() != column_list.size()) {
    return MakeSqlErrorAt(ast_node)
           << "Query has unexpected number of output columns, " << "expected "
           << types.size() << ", but had " << column_list.size();
  }
  GOOGLESQL_RET_CHECK((*scan)->node_kind() == RESOLVED_PROJECT_SCAN);
  ResolvedColumnList casted_column_list;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> casted_exprs;
  auto name_list = std::make_shared<NameList>();
  for (int i = 0; i < types.size(); ++i) {
    const Type* result_type = column_list[i].column().type();
    const Type* target_type = types[i];
    if (result_type->Equals(target_type)) {
      casted_column_list.emplace_back(column_list[i].column());
      GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumnMaybeValueTable(
          column_list[i].name(), column_list[i].column(),
          column_list[i].is_explicit(), ast_node,
          (*output_name_list)->is_value_table()));

    } else {
      // Extract and coerce an expression out of each column.
      //
      // When the result type of the query's output column does
      // not match the target type, then we try to coerce the output
      // column to the target type.  We use assignment coercion
      // rules for determining if coercion is allowed.  We also use
      // the projected expression when present (as opposed to the
      // projected column), since that allows us to do extra coercion
      // for literals such as:
      //
      // target_type: {DATE, TIMESTAMP}
      // query: SELECT '2011-01-01' as d, '2011-01-01 12:34:56' as t;
      //
      // target_type: {ENUM}
      // query: SELECT CAST(0 AS INT32) as e;

      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* column_expr,
                       GetColumnExpr((*scan)->GetAs<ResolvedProjectScan>(),
                                     column_list[i].column()));
      std::unique_ptr<const ResolvedExpr> column_ref;
      if (column_expr == nullptr) {
        column_ref = MakeColumnRef(column_list[i].column());
        column_expr = column_ref.get();
      }
      // Disallow untyped parameters for now, we can't mutably change them and
      // adding in a duplicate parameter into the tree with a different type
      // triggers an error.
      if (column_expr->node_kind() == RESOLVED_PARAMETER &&
          column_expr->GetAs<ResolvedParameter>()->is_untyped()) {
        return MakeSqlErrorAt(ast_node)
               << "Untyped parameter cannot be coerced to an output target "
               << "type for a query";
      }
      SignatureMatchResult unused;
      GOOGLESQL_ASSIGN_OR_RETURN(InputArgumentType column_input_argument_type,
                       GetInputArgumentTypeForExpr(
                           column_expr,
                           /*pick_default_type_for_untyped_expr=*/false,
                           analyzer_options()));
      if (!coercer_.AssignableTo(column_input_argument_type, target_type,
                                 /*is_explicit=*/false, &unused)) {
        return MakeSqlErrorAt(ast_node)
               << "Query column " << (i + 1) << " has type "
               << result_type->ShortTypeName(product_mode())
               << " which cannot be coerced to target type "
               << target_type->ShortTypeName(product_mode());
      }
      std::unique_ptr<const ResolvedExpr> casted_expr =
          MakeColumnRef(column_list[i].column());
      const ASTNode* ast_location =
          GetASTNodeForColumn(ast_node, i, static_cast<int>(types.size()));
      GOOGLESQL_RETURN_IF_ERROR(function_resolver_->AddCastOrConvertLiteral(
          ast_location, target_type, /*format=*/nullptr,
          /*time_zone=*/nullptr, TypeParameters(), &**scan,
          /* set_has_explicit_type =*/false,
          /* return_null_on_error =*/false, &casted_expr));
      const ResolvedColumn casted_column(AllocateColumnId(), kCastedColumnId,
                                         column_list[i].name(), target_type);
      RecordColumnAccess(casted_column);
      casted_column_list.emplace_back(casted_column);
      casted_exprs.push_back(
          MakeResolvedComputedColumn(casted_column, std::move(casted_expr)));
      GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumnMaybeValueTable(
          column_list[i].name(), casted_column,
          (*output_name_list)->column(i).is_explicit(), ast_node,
          (*output_name_list)->is_value_table()));
    }
  }
  if (!casted_exprs.empty()) {
    *scan = MakeResolvedProjectScan(casted_column_list, std::move(casted_exprs),
                                    std::move(*scan));
    GOOGLESQL_RET_CHECK_EQ((*output_name_list)->num_columns(), name_list->num_columns());
    if ((*output_name_list)->is_value_table()) {
      GOOGLESQL_RETURN_IF_ERROR(name_list->SetIsValueTable());
    }
    *output_name_list = name_list;
  }

  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolveGraphTableQuery(
    const ASTGraphTableQuery* ast_graph_table_query,
    const NameScope* external_scope,
    std::unique_ptr<const ResolvedScan>* output,
    std::shared_ptr<const NameList>* output_name_list) {
  GraphTableQueryResolver graph_query_resolver(catalog_, this);
  GOOGLESQL_RETURN_IF_ERROR(graph_query_resolver.ResolveGraphTableQuery(
      ast_graph_table_query, external_scope, output, output_name_list));

  for (const auto* op : ast_graph_table_query->postfix_operators()) {
    switch (op->node_kind()) {
      case AST_SAMPLE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveTablesampleClause(op->GetAs<ASTSampleClause>(),
                                                 output_name_list, output));
        break;
      case AST_MATCH_RECOGNIZE_CLAUSE:
        GOOGLESQL_RETURN_IF_ERROR(ResolveMatchRecognizeClause(
            std::move(*output), *output_name_list, external_scope,
            op->GetAsOrDie<ASTMatchRecognizeClause>(), output,
            output_name_list));
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported postfix operator: " << op->node_kind();
    }
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<std::unique_ptr<const ResolvedScan>> Resolver::ResolveGqlQuery(
    const ASTGqlQuery* ast_gql_query, const NameScope* scope,
    std::shared_ptr<const NameList>* output_name_list) {
  std::unique_ptr<const ResolvedScan> output;
  GOOGLESQL_RETURN_IF_ERROR(ResolveGraphTableQuery(ast_gql_query->graph_table(),
                                         /*external_scope=*/scope, &output,
                                         output_name_list));
  return output;
}

absl::Status Resolver::ResolveLockMode(
    const ASTLockMode* ast_lock_mode,
    std::unique_ptr<const ResolvedLockMode>* resolved) {
  ResolvedLockModeEnums::LockStrengthType strength;
  switch (ast_lock_mode->strength()) {
    case googlesql::ASTLockMode::UPDATE:
      strength = ResolvedLockModeEnums::UPDATE;
      break;
    default:
      // Shouldn't get here.
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported lock mode strength: "
                       << ast_lock_mode->strength();
  }
  *resolved = MakeResolvedLockMode(strength);
  return absl::OkStatus();
}

IdString Resolver::MakeUniqueWithAlias(IdString with_alias) {
  IdString unique_alias = with_alias;
  while (!googlesql_base::InsertIfNotPresent(&unique_with_alias_names_, unique_alias)) {
    unique_alias = MakeIdString(absl::StrCat(unique_alias.ToStringView(), "_",
                                             unique_with_alias_names_.size()));
  }
  return unique_alias;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<IdString> Resolver::ResolvePipeRecursiveUnionAlias(
    const ASTPipeRecursiveUnion* pipe_recursive_union) {
  if (pipe_recursive_union->input_subpipeline() != nullptr) {
    // It is ok to not provide an alias when the input is a subpipeline.
    return pipe_recursive_union->alias() == nullptr
               ? MakeIdString("$pipe_recursive_union")
               : pipe_recursive_union->alias()->GetAsIdString();
  }

  // Subquery input.
  if (pipe_recursive_union->alias() == nullptr) {
    // Point to the end of the subquery input because an alias should be added
    // after the subquery input.
    return MakeSqlErrorAtPoint(
               pipe_recursive_union->input_subquery()->location().end())
           << "The subquery input of RECURSIVE UNION must have an alias";
  }
  IdString alias = pipe_recursive_union->alias()->GetAsIdString();

  // The subquery must contain a self reference to be recursive.
  GOOGLESQL_ASSIGN_OR_RETURN(bool contains_self_reference,
                   ContainsReferenceToTable(
                       pipe_recursive_union->input_subquery(), {alias}));
  if (!contains_self_reference) {
    return MakeSqlErrorAt(pipe_recursive_union->input_subquery())
           << "Recursive union subquery must contain a reference to the "
              "recursive input table "
           << ToIdentifierLiteral(alias);
  }
  return alias;
}

absl::Status Resolver::RemoveInnermostNamedSubqueryWithAlias(IdString alias) {
  auto it = named_subquery_map_.find({alias});
  GOOGLESQL_RET_CHECK(it != named_subquery_map_.end());
  it->second.pop_back();
  if (it->second.empty()) {
    named_subquery_map_.erase(it);
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status Resolver::ResolvePipeRecursiveUnion(
    const ASTPipeRecursiveUnion* pipe_recursive_union,
    std::unique_ptr<const ResolvedScan>* current_scan,
    std::shared_ptr<const NameList>* current_name_list) {
  if (!language().LanguageFeatureEnabled(FEATURE_PIPE_RECURSIVE_UNION)) {
    return MakeSqlErrorAt(pipe_recursive_union)
           << "Pipe RECURSIVE UNION is not supported";
  }
  analyzer_output_properties_.AddFeatureLabel("PipeOperator:RECURSIVE_UNION");

  if (const ASTSetOperationType* set_op_type =
          pipe_recursive_union->metadata()->op_type();
      set_op_type->value() != ASTSetOperation::UNION) {
    return MakeSqlErrorAt(set_op_type)
           << "UNION is the only supported RECURSIVE set operator";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(IdString alias,
                   ResolvePipeRecursiveUnionAlias(pipe_recursive_union));
  IdString unique_alias = MakeUniqueWithAlias(alias);

  // Register a NULL entry for the named subquery. We'll change this to an
  // actual NamedSubquery object when the recursive term resolves in
  // SetOperationResolver::ResolveRecursive().
  // TODO: We can use the `NamedSubquery::access_error_message` to avoid using a
  // NULL entry. The same for resolving WITH RECURSIVE.
  named_subquery_map_[{alias}].push_back(nullptr);

  SetOperationResolver setop_resolver(pipe_recursive_union, *current_name_list,
                                      current_scan, this);

  // We use `empty_name_scope_` because the recursive term, either with subquery
  // input or subpipeline input, is not allowed to reference any correlated
  // columns. This is to stay consistent with WITH RECURSIVE.
  GOOGLESQL_RETURN_IF_ERROR(setop_resolver.ResolveRecursive(
      empty_name_scope_.get(), {alias}, unique_alias, current_scan,
      current_name_list));

  const ASTRecursionDepthModifier* ast_depth_modifier =
      pipe_recursive_union->recursion_depth_modifier();
  if (ast_depth_modifier != nullptr) {
    if (!language().LanguageFeatureEnabled(
            FEATURE_WITH_RECURSIVE_DEPTH_MODIFIER)) {
      return MakeSqlErrorAt(ast_depth_modifier)
             << "Recursion depth modifier is not supported";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedRecursionDepthModifier>
                         recursion_depth_modifier,
                     ResolveRecursionDepthModifier(ast_depth_modifier));

    // The depth column is added after the call to ResolveRecursive() so that
    // the depth column cannot be referenced inside the recursive term, e.g.
    //
    // SELECT 1
    // |> RECURSIVE UNION DISTINCT WITH DEPTH (
    //    FROM t
    //    -- Error: depth is not visible inside the recursive term
    //    WHERE depth = 1
    // ) AS t
    //
    // but is visible in the output:
    //
    // SELECT t.depth
    // |> RECURSIVE UNION DISTINCT WITH DEPTH (
    //    FROM t
    // ) AS t
    // -- OK: depth and t.depth are both is visible in the output.
    // |> SELECT depth, t.depth;
    GOOGLESQL_RETURN_IF_ERROR(setop_resolver.AddDepthColumnToRecursiveScan(
        ast_depth_modifier, {alias}, std::move(recursion_depth_modifier),
        current_scan, current_name_list));

    if (!IsInternalAlias(alias)) {
      // `current_name_list` contains the depth column after
      // FinishResolveRecursionWithModifier(), but the depth column hasn't been
      // added to the existing range variable. Call
      // UpdateNameListForTableAlias() to update it to support references like
      // `t.depth` in this example:
      //
      // SELECT 1
      // |> RECURSIVE UNION DISTINCT WITH DEPTH (
      //    FROM t
      // ) AS t
      // |> SELECT t.depth;
      std::shared_ptr<const NameList> output_name_list;
      GOOGLESQL_RETURN_IF_ERROR(UpdateNameListForTableAlias(
          ast_depth_modifier, alias, *current_name_list, &output_name_list));
      *current_name_list = output_name_list;
    }
  }

  // The resolution of this recursive query is complete, restore the original
  // `named_subquery_map_`.
  GOOGLESQL_RETURN_IF_ERROR(RemoveInnermostNamedSubqueryWithAlias(alias));

  return absl::OkStatus();
}

absl::Status Resolver::ValidateMeasureSource(const ASTNode* ast_location,
                                             const Table* table,
                                             absl::string_view measure_source) {
  bool needs_table_row_identity_columns = false;
  for (int i = 0; i < table->NumColumns(); ++i) {
    const Column* column = table->GetColumn(i);
    if (column->GetType()->IsMeasureType()) {
      IdString column_name = MakeIdString(column->Name());
      if (column_name.empty()) {
        // This is to stay consistent with the implementation in
        // `MakeScanForTable`.
        column_name = MakeIdString(absl::StrCat("$col", i + 1));
      }
      GOOGLESQL_RETURN_IF_ERROR(ValidateMeasureColumn(
          ast_location, column, table, column_name, language(), measure_source,
          &needs_table_row_identity_columns));
    }
  }
  if (needs_table_row_identity_columns) {
    std::vector<int> row_identity_columns =
        table->RowIdentityColumns().value_or(std::vector<int>{});
    GOOGLESQL_RETURN_IF_ERROR(CheckRowIdentityColumns(ast_location, row_identity_columns,
                                            table, language(), measure_source));
  }
  return absl::OkStatus();
}

}  // namespace googlesql
