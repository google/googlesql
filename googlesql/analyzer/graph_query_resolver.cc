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

#include "googlesql/analyzer/graph_query_resolver.h"

#include <stdbool.h>

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/analyzer/analytic_function_resolver.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/function_resolver.h"
#include "googlesql/analyzer/graph_expr_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/resolver_common_inl.h"
#include "googlesql/analyzer/set_operation_resolver_base.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/internal_analyzer_output_properties.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

STATIC_IDSTRING(kGraphTableName, "$graph_table");
STATIC_IDSTRING(kElementTableName, "$element_table");
STATIC_IDSTRING(kPathScanName, "$path_scan");
STATIC_IDSTRING(kHeadColumnName, "$head");
STATIC_IDSTRING(kTailColumnName, "$tail");
STATIC_IDSTRING(kGraphSetOperation, "$graph_set_op");
STATIC_IDSTRING(kOffsetAlias, "offset");
STATIC_IDSTRING(kDefaultPathColumnName, "$path");

namespace {

ResolvedGraphEdgeScan::EdgeOrientation GetResolvedEdgeOrientation(
    ASTGraphEdgePattern::EdgeOrientation ast_edge_orientation) {
  switch (ast_edge_orientation) {
    case ASTGraphEdgePattern::ANY:
      return ResolvedGraphEdgeScan::ANY;
    case ASTGraphEdgePattern::LEFT:
      return ResolvedGraphEdgeScan::LEFT;
    case ASTGraphEdgePattern::RIGHT:
      return ResolvedGraphEdgeScan::RIGHT;
    default:
      ABSL_DCHECK(false) << "Unexpected edge orientation: " << ast_edge_orientation;
      return ResolvedGraphEdgeScan::ANY;
  }
}

ResolvedGraphPathMode::PathMode GetResolvedPathMode(
    const ASTGraphPathMode* path_mode) {
  if (path_mode == nullptr) {
    return ResolvedGraphPathMode::PATH_MODE_UNSPECIFIED;
  }
  switch (path_mode->path_mode()) {
    case ASTGraphPathMode::PATH_MODE_UNSPECIFIED:
      return ResolvedGraphPathMode::PATH_MODE_UNSPECIFIED;
    case ASTGraphPathMode::WALK:
      return ResolvedGraphPathMode::WALK;
    case ASTGraphPathMode::TRAIL:
      return ResolvedGraphPathMode::TRAIL;
    case ASTGraphPathMode::SIMPLE:
      return ResolvedGraphPathMode::SIMPLE;
    case ASTGraphPathMode::ACYCLIC:
      return ResolvedGraphPathMode::ACYCLIC;
    default:
      ABSL_LOG(ERROR) << "Unexpected path mode: " << path_mode;
      return ResolvedGraphPathMode::PATH_MODE_UNSPECIFIED;
  }
}

// Returns error if <name_list> derived from <location> contains any ambiguous
// name target; otherwise return ok.
// TODO: Make this part of GraphTableNamedVariables.
absl::Status CheckNoAmbiguousNameTarget(const ASTNode& location,
                                        const NameList& name_list) {
  for (const NamedColumn& named_column : name_list.columns()) {
    if (NameTarget target; name_list.LookupName(named_column.name(), &target) &&
                           target.IsAmbiguous()) {
      return MakeSqlErrorAt(&location)
             << "Ambiguous name: " << ToIdentifierLiteral(named_column.name());
    }
  }
  return absl::OkStatus();
}

// Map from identifier to resolved label expr.
using LabelExprMap =
    IdStringHashMapCase<std::unique_ptr<const ResolvedGraphLabelExpr>>;

GraphElementTable::Kind GetElementTableKind(
    const ASTGraphElementPattern& ast_element_pattern) {
  return ast_element_pattern.Is<ASTGraphNodePattern>()
             ? GraphElementTable::Kind::kNode
             : GraphElementTable::Kind::kEdge;
}

// Utility class for resolving label expressions.
class GraphLabelExprResolver {
 public:
  static absl::StatusOr<LabelExprMap> ResolveNamedVariables(
      const ASTGraphPattern* graph_pattern,
      std::function<
          absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>(
              const ASTGraphLabelFilter*, GraphElementTable::Kind)>
          label_expr_resolver) {
    GraphLabelExprResolver collector(std::move(label_expr_resolver));
    return collector.Visit(graph_pattern);
  }

 private:
  explicit GraphLabelExprResolver(
      std::function<
          absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>(
              const ASTGraphLabelFilter*, GraphElementTable::Kind)>
          label_expr_resolver)
      : label_expr_resolver_(std::move(label_expr_resolver)) {}

  absl::StatusOr<LabelExprMap> Visit(
      const ASTGraphElementPattern* ast_element_pattern) const {
    LabelExprMap map;
    const ASTGraphElementPatternFiller* filler = ast_element_pattern->filler();
    // Ignore anonymous variables as they are mutually distinct, they don't
    // have explicit identifiers and will not be multiply-declared variables.
    // They will be resolved on demand.
    if (filler == nullptr || filler->variable_name() == nullptr) {
      return map;
    }
    const ASTIdentifier* element_var = filler->variable_name();
    const ASTGraphLabelFilter* ast_label_filter = filler->label_filter();
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphLabelExpr> label_expr,
        label_expr_resolver_(ast_label_filter,
                             GetElementTableKind(*ast_element_pattern)));
    map.emplace(element_var->GetAsIdString(), std::move(label_expr));
    return map;
  }

  absl::StatusOr<LabelExprMap> Visit(
      const ASTGraphPattern* graph_pattern) const {
    std::vector<LabelExprMap> results;
    results.reserve(graph_pattern->paths().size());
    for (const auto* path : graph_pattern->paths()) {
      GOOGLESQL_ASSIGN_OR_RETURN(LabelExprMap result, Visit(path));
      results.push_back(std::move(result));
    }
    return MergeLabelMaps(ResolvedGraphLabelNaryExpr::AND,
                          absl::MakeSpan(results));
  }

  absl::StatusOr<LabelExprMap> Visit(
      const ASTGraphPathPattern* path_pattern) const {
    std::vector<LabelExprMap> results;
    results.reserve(path_pattern->input_pattern_list().size());
    for (const auto* ast : path_pattern->input_pattern_list()) {
      switch (ast->node_kind()) {
        case AST_GRAPH_NODE_PATTERN:
        case AST_GRAPH_EDGE_PATTERN: {
          GOOGLESQL_ASSIGN_OR_RETURN(LabelExprMap result,
                           Visit(ast->GetAsOrDie<ASTGraphElementPattern>()));
          results.push_back(std::move(result));
          break;
        }
        case AST_GRAPH_PATH_PATTERN: {
          GOOGLESQL_ASSIGN_OR_RETURN(LabelExprMap result,
                           Visit(ast->GetAsOrDie<ASTGraphPathPattern>()));
          results.push_back(std::move(result));
          break;
        }
        default:
          return MakeSqlErrorAt(ast) << "Unsupported ast node";
      }
    }
    // Merge label maps because there can be multiply-declared variables.
    // Uses `AND` for pattern concatenation. Other composition could use a
    // different operation (for example, pattern alternation will use OR).
    return MergeLabelMaps(ResolvedGraphLabelNaryExpr::AND,
                          absl::MakeSpan(results));
  }

  absl::StatusOr<LabelExprMap> MergeLabelMaps(
      ResolvedGraphLabelNaryExpr::GraphLogicalOpType op,
      absl::Span<LabelExprMap> sub_maps) const {
    GOOGLESQL_RET_CHECK(!sub_maps.empty());
    IdStringHashMapCase<
        std::vector<std::unique_ptr<const ResolvedGraphLabelExpr>>>
        label_expr_by_names;
    for (LabelExprMap& sub_map : sub_maps) {
      for (auto& [name, label_expr] : sub_map) {
        label_expr_by_names[name].push_back(std::move(label_expr));
      }
    }

    LabelExprMap map;
    for (auto& [name, label_exprs] : label_expr_by_names) {
      GOOGLESQL_RET_CHECK(!label_exprs.empty());
      if (label_exprs.size() == 1) {
        map[name] = std::move(label_exprs.front());
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(map[name],
                         ResolvedGraphLabelNaryExprBuilder()
                             .set_op(op)
                             .set_operand_list(std::move(label_exprs))
                             .Build());
      }
    }
    return map;
  }

 private:
  const std::function<
      absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>(
          const ASTGraphLabelFilter*, GraphElementTable::Kind)>
      label_expr_resolver_;
};

// Add names from `name_list` in reverse order, merging multiply-declared
// names instead of adding them like other names.
void MergeReversedMultiplyDeclaredNames(
    const NameListPtr& name_list,
    std::vector<IdString>& multiply_declared_variables,
    IdStringHashMapCase<ResolvedColumnList>& variable_declarations,
    std::vector<const NamedColumn*>& reversed_name_list) {
  for (auto it = name_list->columns().rbegin();
       it != name_list->columns().rend(); ++it) {
    const NamedColumn& named_column = *it;
    const auto& [iter, inserted] = variable_declarations.emplace(
        named_column.name(), ResolvedColumnList{named_column.column()});
    if (inserted) {
      reversed_name_list.push_back(&named_column);
    } else {
      if (iter->second.size() == 1) {
        multiply_declared_variables.push_back(named_column.name());
      }
      iter->second.push_back(named_column.column());
    }
  }
}

// Ensure that the last GQL return operator does not return any columns
// whose type is or contains a graph element.
absl::Status CheckReturnNoGraphTypedCols(
    const ResolvedColumnList& graph_table_columns,
    const ASTGqlOperator* return_op) {
  GOOGLESQL_RET_CHECK(return_op->Is<ASTGqlReturn>());
  const ASTSelectList* return_item_list =
      return_op->GetAsOrDie<ASTGqlReturn>()->select()->select_list();
  GOOGLESQL_RET_CHECK(return_item_list != nullptr);
  absl::Span<const ASTSelectColumn* const> select_items =
      return_item_list->columns();
  GOOGLESQL_RET_CHECK_GE(select_items.size(), 1);

  int star_idx = -1;
  int num_stars = 0;
  for (int i = 0; i < select_items.size(); ++i) {
    const ASTSelectColumn* column = select_items[i];
    if (column->expression()->node_kind() == AST_STAR) {
      num_stars++;
      star_idx = i;
    }
  }
  GOOGLESQL_RET_CHECK_LE(num_stars, 1) << "Should have been caught during resolution";

  int offset =
      static_cast<int>(graph_table_columns.size() - select_items.size());
  auto select_idx_lambda = [star_idx, offset](int i) {
    if (i < star_idx) {
      return i;
    } else if (i > star_idx + offset) {
      return i - offset;
    } else {
      return star_idx;
    }
  };
  for (int i = 0; i < graph_table_columns.size(); ++i) {
    if (TypeIsOrContainsGraphElement(graph_table_columns[i].type())) {
      int select_idx = num_stars == 0 ? i : select_idx_lambda(i);
      GOOGLESQL_RET_CHECK_GE(select_idx, 0);
      GOOGLESQL_RET_CHECK_LT(select_idx, select_items.size());
      return MakeSqlErrorAt(select_items[select_idx])
             << "Returning graph-typed column is not supported";
    }
  }
  return absl::OkStatus();
}

absl::Status CheckReturnStarIsStandalone(const ASTSelectList* select_list) {
  if (select_list->columns().size() == 1 &&
      select_list->columns().front()->expression()->node_kind() == AST_STAR) {
    return absl::OkStatus();
  } else {
    return MakeSqlErrorAt(select_list)
           << "* cannot be combined with other return items";
  }
}

// Creates a name scope that returns error message built by `error_msg_fn`
// when names in `disallowed_name_lists` are referenced.
template <typename ErrorMessageFn>
absl::StatusOr<std::unique_ptr<NameScope>> CreateNameScopeWithDisallowList(
    const NameScope* external_scope,
    absl::Span<const NameList* const> disallowed_name_lists,
    const ErrorMessageFn& error_msg_fn) {
  IdStringHashMapCase<NameTarget> error_name_targets;
  for (const auto& disallowed_name_list : disallowed_name_lists) {
    for (const NamedColumn& column : disallowed_name_list->columns()) {
      NameTarget name_target(column.column(), column.is_explicit());
      name_target.SetAccessError(NameTarget::EXPLICIT_COLUMN,
                                 error_msg_fn(column.name()));
      googlesql_base::InsertIfNotPresent(&error_name_targets, column.name(), name_target);
    }
  }
  NameScope empty_scope(external_scope);
  std::unique_ptr<NameScope> scope;
  GOOGLESQL_RETURN_IF_ERROR(empty_scope.CopyNameScopeWithOverridingNameTargets(
      error_name_targets, &scope));
  return scope;
}

// Returns the set operation column propagation mode for the given `metadata`.
// Default to STRICT if column propagation mode is not specified.
static ResolvedSetOperationScan::SetOperationColumnPropagationMode
GetGraphSetColumnPropagationMode(const ASTSetOperationMetadata* metadata) {
  if (metadata->column_propagation_mode() == nullptr) {
    return ResolvedSetOperationScan::STRICT;
  }
  switch (metadata->column_propagation_mode()->value()) {
    case ASTSetOperation::STRICT:
      return ResolvedSetOperationScan::STRICT;
    case ASTSetOperation::INNER:
      return ResolvedSetOperationScan::INNER;
    case ASTSetOperation::LEFT:
      return ResolvedSetOperationScan::LEFT;
    case ASTSetOperation::FULL:
      return ResolvedSetOperationScan::FULL;
  }
}

}  // namespace

// Returns true if the 'ast_node' is a quantified pattern.
bool IsQuantified(const ASTNode* ast_node) {
  const auto* pattern = ast_node->GetAsOrNull<ASTGraphPathBase>();
  return pattern != nullptr && pattern->quantifier() != nullptr;
}

// Validates the AST node of a top level path pattern.
absl::Status ValidateTopLevelPathPattern(
    const ASTGraphPathPattern* ast_path_pattern) {
  // AST search prefix is usually wrapped around an AST parenthesized path node.
  // When occurring on the same AST path node, it means MATCH (ANY ... ) which
  // is not supported.
  if (ast_path_pattern->parenthesized() &&
      ast_path_pattern->search_prefix() != nullptr) {
    return MakeSqlErrorAt(ast_path_pattern)
           << "Search prefix is not allowed inside a parenthesized path "
              "pattern";
  }
  return absl::OkStatus();
}

static bool ContainsSelectiveSearchPrefix(
    const ASTGraphPathPattern& ast_path_pattern) {
  return ast_path_pattern.search_prefix() != nullptr &&
         ast_path_pattern.search_prefix()->type() !=
             ASTGraphPathSearchPrefix::PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED &&
         ast_path_pattern.search_prefix()->type() !=
             ASTGraphPathSearchPrefix::ALL;
}

static bool ContainsRestrictivePathMode(
    const ASTGraphPathPattern& ast_path_pattern) {
  return ast_path_pattern.path_mode() != nullptr &&
         ast_path_pattern.path_mode()->path_mode() != ASTGraphPathMode::WALK &&
         ast_path_pattern.path_mode()->path_mode() !=
             ASTGraphPathMode::PATH_MODE_UNSPECIFIED;
}

absl::Status GraphTableQueryResolver::ValidatePathPattern(
    const ASTGraphPattern* ast_graph_pattern, const NameScope* scope) {
  IdStringHashMapCase<bool> var_has_cost_map;
  for (const auto* ast_path_pattern : ast_graph_pattern->paths()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const ASTGraphPathBase*> ast_path_bases,
                     CanonicalizePathPattern(*ast_path_pattern));
    GOOGLESQL_RETURN_IF_ERROR(ValidateTopLevelPathPattern(ast_path_pattern));

    // Additional validation logic applies if the top level path pattern
    // has the CHEAPEST search prefix.
    bool in_cheapest = ast_path_pattern->search_prefix() != nullptr &&
                       ast_path_pattern->search_prefix()->type() ==
                           ASTGraphPathSearchPrefix::CHEAPEST;
    // Unbounded quantified path patterns must be used within a parent path
    // containing a selective search prefix or a restrictive path mode. So we
    // need to propagate this information into the child path pattern
    // validation.
    GOOGLESQL_ASSIGN_OR_RETURN(auto path_rule_result,
                     ValidatePathPatternInternal(
                         ast_path_pattern, scope, ast_path_bases,
                         var_has_cost_map, in_cheapest,
                         /*parent_has_selective_search_prefix=*/false,
                         /*parent_has_restrictive_path_mode=*/false));

    // Minimum node count must be checked on the entire path.
    if (path_rule_result.min_node_count == 0) {
      return MakeSqlErrorAt(ast_path_pattern)
             << "Minimum node count of path pattern cannot be 0";
    }
    if (in_cheapest && !path_rule_result.has_edge_cost) {
      return MakeSqlErrorAt(ast_path_pattern)
             << "CHEAPEST search prefix must include at least one edge cost "
                "expression";
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<GraphTableQueryResolver::PathInfo>
GraphTableQueryResolver::ValidatePathPatternInternal(
    const ASTGraphPathPattern* ast_path_pattern, const NameScope* scope,
    const std::vector<const ASTGraphPathBase*>& ast_path_bases,
    IdStringHashMapCase<bool>& var_has_cost_map, bool in_cheapest,
    bool parent_has_selective_search_prefix,
    bool parent_has_restrictive_path_mode) {
  PathInfo path_stats;

  for (const auto* ast_path_base : ast_path_bases) {
    switch (ast_path_base->node_kind()) {
      // Note: All quantified edges are syntactically transformed into a
      // quantified path before we begin resolution, hence, this case covers
      // both quantified-edges and quantified-paths.
      case AST_GRAPH_PATH_PATTERN: {
        GOOGLESQL_RET_CHECK(ast_path_base->Is<ASTGraphPathPattern>());
        const auto* subpath_pattern =
            ast_path_base->GetAsOrDie<ASTGraphPathPattern>();
        GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const ASTGraphPathBase*> ast_subpath_bases,
                         CanonicalizePathPattern(*subpath_pattern));
        bool has_selective_search_prefix =
            ContainsSelectiveSearchPrefix(*ast_path_pattern) ||
            parent_has_selective_search_prefix;
        bool has_restrictive_path_mode =
            ContainsRestrictivePathMode(*ast_path_pattern) ||
            parent_has_restrictive_path_mode;
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto subpath_rule_info,
            ValidatePathPatternInternal(
                subpath_pattern, scope, ast_subpath_bases, var_has_cost_map,
                in_cheapest, has_selective_search_prefix,
                has_restrictive_path_mode));

        path_stats.min_node_count += subpath_rule_info.min_node_count;
        path_stats.min_path_length += subpath_rule_info.min_path_length;
        path_stats.child_is_quantified |= subpath_rule_info.child_is_quantified;
        path_stats.has_edge_cost |= subpath_rule_info.has_edge_cost;
        break;
      }
      case AST_GRAPH_NODE_PATTERN:
        // Path length for a single node is 0.
        ++path_stats.min_node_count;
        break;
      case AST_GRAPH_EDGE_PATTERN: {
        // Path length for a single edge is 1.
        ++path_stats.min_path_length;
        // COST <expr> specific validations:
        const auto* edge_pattern = ast_path_base->GetAs<ASTGraphEdgePattern>();
        if (edge_pattern->filler() != nullptr &&
            edge_pattern->filler()->variable_name() != nullptr) {
          const IdString var_name =
              edge_pattern->filler()->variable_name()->GetAsIdString();
          const bool has_edge_cost =
              edge_pattern->filler()->edge_cost() != nullptr;
          if (var_has_cost_map.find(var_name) != var_has_cost_map.end() &&
              var_has_cost_map.at(var_name)) {
            return MakeSqlErrorAt(edge_pattern)
                   << "Cannot multiply declare variable that previously "
                      "defined a cost expression in the same path pattern: "
                   << var_name;
          }
          if (has_edge_cost) {
            path_stats.has_edge_cost = true;
            if (var_has_cost_map.find(var_name) != var_has_cost_map.end()) {
              return MakeSqlErrorAt(edge_pattern)
                     << "Edge cost expression cannot be redefined for "
                        "variable: "
                     << var_name;
            }
          }
          var_has_cost_map.emplace(var_name, has_edge_cost);
        }
        break;
      }
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported ASTNode within a path pattern: "
                         << ast_path_base->GetNodeKindString();
    }
  }

  // If a quantifier is provided, attempt to fetch its values and validate them.
  // Accordingly, adjust the parent path's node count and path length values.
  // If this is a nested quantifier, throw an error.
  if (path_stats.child_is_quantified && IsQuantified(ast_path_pattern)) {
    return MakeSqlErrorAt(ast_path_pattern)
           << "Nested quantifiers are not allowed";
  } else if (IsQuantified(ast_path_pattern)) {
    // Minimum path length needs to only be checked for path-primaries within a
    // quantified path pattern.
    if (path_stats.min_path_length == 0) {
      return MakeSqlErrorAt(ast_path_pattern)
             << "Minimum path length of a path-primary within a quantified "
                "path pattern cannot be 0";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_quantifier,
                     ResolveGraphPathPatternQuantifier(
                         scope, ast_path_pattern->quantifier()));
    bool is_unbounded_quantifier =
        resolved_quantifier->upper_bound() == nullptr;
    if (is_unbounded_quantifier) {
      if (!resolver_->language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_UNBOUNDED_PATH_QUANTIFICATION)) {
        return MakeSqlErrorAt(ast_path_pattern->quantifier())
               << "Unbounded quantifier is not supported";
      }
      if (!parent_has_selective_search_prefix &&
          !parent_has_restrictive_path_mode) {
        return MakeSqlErrorAt(ast_path_pattern->quantifier())
               << "Unbounded quantified path pattern is not allowed without a "
                  "selective search prefix or a restrictive path mode";
      }
    }
    const auto* lower_bound_expr = resolved_quantifier->lower_bound();
    int64_t lower_bound = 0;
    if (lower_bound_expr->Is<ResolvedLiteral>()) {
      lower_bound =
          lower_bound_expr->GetAs<ResolvedLiteral>()->value().int64_value();
    } else {
      // If the lower bound was given as a parameter, we cannot perform
      // validation on it, so we assume a lower_bound of 1 to make it pass
      // minimum node count validation.
      lower_bound = 1;
    }

    path_stats.min_node_count *= lower_bound;
    path_stats.min_path_length *= lower_bound;
    path_stats.child_is_quantified = true;
  }
  if (in_cheapest && IsQuantified(ast_path_pattern) &&
      !path_stats.has_edge_cost) {
    return MakeSqlErrorAt(ast_path_pattern)
           << "Quantified pattern in CHEAPEST must include at least one edge "
              "cost expression";
  }
  return path_stats;
}

absl::Status GraphTableQueryResolver::ResolveGraphTableQuery(
    const ASTGraphTableQuery* graph_table_query, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* output,
    NameListPtr* output_name_list) {
  if (!resolver_->language().LanguageFeatureEnabled(FEATURE_SQL_GRAPH)) {
    return MakeSqlErrorAt(graph_table_query) << "Graph query is not supported";
  }
  GOOGLESQL_RET_CHECK(output != nullptr);
  GOOGLESQL_RET_CHECK(output_name_list != nullptr);
  GOOGLESQL_RET_CHECK(graph_table_query != nullptr);
  GOOGLESQL_RET_CHECK(graph_table_query->graph_op() != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(auto callback, HandleGraphReference(graph_table_query));
  absl::Cleanup cleanup = [&callback]() { callback(); };

  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphElementTablesDynamicLabelAndProperties(
      resolver_->language(), graph_table_query->graph_reference(), *graph_));

  auto graph_named_variables = CreateEmptyGraphNameLists(graph_table_query);
  ResolvedColumnList graph_table_columns;
  std::unique_ptr<const ResolvedGraphScanBase> resolved_graph_scan;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      graph_table_column_expr_list;
  if (graph_table_query->graph_op()->Is<ASTGqlOperatorList>()) {
    // GQL syntax, which contains chained GQL operators and no
    // graph_table_shape. see (broken link):gql-graph-table for more details.
    const auto* gql_query =
        graph_table_query->graph_op()->GetAsOrDie<ASTGqlOperatorList>();
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_ADVANCED_QUERY)) {
      return MakeSqlErrorAt(gql_query)
             << "Graph query with GQL extension is not supported";
    }
    if (graph_table_query->graph_reference() == nullptr) {
      GOOGLESQL_RET_CHECK(!gql_query->operators().empty());
      const ASTGqlOperatorList* ops_list;
      if (gql_query->operators(0)->Is<ASTGqlOperatorList>()) {
        ops_list = gql_query->operators(0)->GetAsOrDie<ASTGqlOperatorList>();
      } else {
        GOOGLESQL_RET_CHECK(gql_query->operators(0)->Is<ASTGqlSetOperation>());
        const auto& inputs =
            gql_query->operators(0)->GetAsOrDie<ASTGqlSetOperation>()->inputs();
        GOOGLESQL_RET_CHECK(!inputs.empty());
        ops_list = inputs.at(0)->GetAsOrDie<ASTGqlOperatorList>();
      }
      GOOGLESQL_RET_CHECK(!ops_list->operators().empty());
      if (ops_list->operators(0)->node_kind() != AST_GQL_MATCH) {
        return MakeSqlErrorAt(gql_query)
               << "Match should be the first operator when the graph "
                  "reference is omitted";
      }
    }

    // 'graph_named_variables' contains an empty working name list to indicate
    // the start of the GQL query.
    GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                     ResolveGqlLinearQueryList(
                         *gql_query, scope, std::move(graph_named_variables),
                         /*is_first_statement_in_graph_query=*/true));
    resolved_graph_scan = std::move(result.resolved_node);
    graph_table_columns = resolved_graph_scan->column_list();
    *output_name_list = result.graph_name_lists.singleton_name_list;
  } else {
    GOOGLESQL_RET_CHECK(graph_table_query->graph_op()->Is<ASTGqlMatch>());
    auto graph_pattern_name_list = std::make_shared<NameList>();
    const ASTGqlMatch* gql_match =
        graph_table_query->graph_op()->GetAsOrDie<ASTGqlMatch>();
    const ASTGraphPattern* graph_pattern = gql_match->graph_pattern();

    GOOGLESQL_RET_CHECK(graph_pattern != nullptr);
    GOOGLESQL_RET_CHECK(!gql_match->optional()) << "Parser doesn't accept optional match";

    GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                     ResolveGraphPattern(*graph_pattern, scope,
                                         std::move(graph_named_variables)));

    // Capture the output nameslists of the match(es).
    graph_named_variables = std::move(result.graph_name_lists);
    // The result of the match(es) will be a ResolvedGraphScan.
    std::unique_ptr<const ResolvedGraphScan> graph_pattern_scan =
        std::move(result.resolved_node);
    // Resolve graph table shape using the combined namescope.
    const NameScope graph_pattern_scope(
        scope, graph_named_variables.singleton_name_list);
    auto graph_table_shape_name_list = std::make_shared<const NameList>();
    if (graph_table_query->graph_table_shape() == nullptr) {
      if (!resolver_->language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT)) {
        return MakeSqlErrorAt(graph_table_query)
               << "Graph query without COLUMNS clause is not supported";
      }
      // Resolve optional columns, all named graph elements represent the output
      // Columns with internal names are excluded from the output name list and
      // output column list
      auto named_graph_element_name_list = std::make_shared<NameList>();
      for (const NamedColumn& col :
           graph_named_variables.singleton_name_list->columns()) {
        // TODO: Fix IsInternalAlias behavior for quoted
        // identifiers
        if (IsInternalAlias(col.name())) {
          continue;
        }
        GOOGLESQL_RETURN_IF_ERROR(named_graph_element_name_list->AddColumn(
            col.name(), col.column(), col.is_explicit()));
        graph_table_columns.push_back(col.column());
      }
      *output_name_list = named_graph_element_name_list;
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ResolveGraphTableShape(
          graph_table_query->graph_table_shape(), &graph_pattern_scope,
          &graph_table_shape_name_list, &graph_table_columns,
          &graph_table_column_expr_list));
      *output_name_list = graph_table_shape_name_list;
    }

    resolved_graph_scan = std::move(graph_pattern_scan);
  }

  if (graph_table_query->alias() != nullptr) {
    // If alias is provided, add a range variable so that this alias can be
    // referred to.
    GOOGLESQL_ASSIGN_OR_RETURN(*output_name_list,
                     NameList::AddRangeVariableInWrappingNameList(
                         graph_table_query->alias()->GetAsIdString(),
                         graph_table_query, *output_name_list));
  }

  // Columns produced by GRAPH_TABLE is similar to a SELECT list. They count as
  // referenced and cannot be pruned.
  resolver_->RecordColumnAccess(graph_table_columns);
  GOOGLESQL_ASSIGN_OR_RETURN(
      *output, ResolvedGraphTableScanBuilder()
                   .set_column_list(graph_table_columns)
                   .set_property_graph(graph_)
                   .set_input_scan(std::move(resolved_graph_scan))
                   .set_shape_expr_list(std::move(graph_table_column_expr_list))
                   .Build());

  GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(*output_name_list,
                                             graph_table_query, "graph queries",
                                             resolver_->product_mode()));

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::MakeGraphPatternFilterExpr(
    const ASTGraphPattern* graph_pattern,
    const GraphTableNamedVariables& input_named_variables,
    std::unique_ptr<const ResolvedExpr> cross_path_multiply_decl_filter_expr,
    std::unique_ptr<const ResolvedExpr> cross_stmt_multiply_decl_filter_expr,
    const NameScope* scope) {
  std::vector<std::unique_ptr<const ResolvedExpr>> filter_expr_conjuncts;
  if (cross_path_multiply_decl_filter_expr != nullptr) {
    filter_expr_conjuncts.push_back(
        std::move(cross_path_multiply_decl_filter_expr));
  }
  if (cross_stmt_multiply_decl_filter_expr != nullptr) {
    filter_expr_conjuncts.push_back(
        std::move(cross_stmt_multiply_decl_filter_expr));
  }
  if (graph_pattern->where_clause() != nullptr) {
    const NameScope where_clause_scope(
        scope, input_named_variables.singleton_name_list);
    auto query_resolution_info =
        std::make_unique<QueryResolutionInfo>(resolver_);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> where_expr,
        ResolveWhereClause(graph_pattern->where_clause(), &where_clause_scope,
                           query_resolution_info.get(),
                           /*allow_analytic=*/false,
                           /*allow_horizontal_aggregate=*/true));
    filter_expr_conjuncts.push_back(std::move(where_expr));
  }
  std::unique_ptr<const ResolvedExpr> filter_expr;
  if (!filter_expr_conjuncts.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeAndExpr(
        graph_pattern, std::move(filter_expr_conjuncts), &filter_expr));
  }
  return std::move(filter_expr);
}

absl::Status GraphTableQueryResolver::ResolveGqlGraphPatternQuery(
    const ASTGqlGraphPatternQuery* query, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* output,
    NameListPtr* output_name_list) {
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_ADVANCED_QUERY)) {
    return MakeSqlErrorAt(query) << "GQL subquery is not supported";
  }
  GOOGLESQL_RET_CHECK_NE(output, nullptr);
  GOOGLESQL_RET_CHECK_NE(output_name_list, nullptr);
  GOOGLESQL_RET_CHECK_NE(query, nullptr);
  GOOGLESQL_RET_CHECK_NE(query->graph_pattern(), nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(auto callback, HandleGraphReference(query));
  absl::Cleanup cleanup = [&callback]() { callback(); };

  GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                   ResolveGraphPattern(*query->graph_pattern(), scope,
                                       CreateEmptyGraphNameLists(query)));

  ResolvedColumn column(resolver_->AllocateColumnId(), kGraphTableName,
                        resolver_->MakeIdString("literal_true"),
                        types::BoolType());

  ResolvedColumnList result_col_list{column};
  // RecordColumnAccess to avoid being pruned.
  resolver_->RecordColumnAccess(result_col_list);

  auto name_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(column.name_id(), column,
                                       /*is_explicit=*/false));
  *output_name_list = std::move(name_list);

  GOOGLESQL_ASSIGN_OR_RETURN(
      *output,
      ResolvedGraphTableScanBuilder()
          .set_column_list(result_col_list)
          .set_property_graph(graph_)
          .set_input_scan(
              ResolvedGraphLinearScanBuilder()
                  .set_column_list(result_col_list)
                  .add_scan_list(
                      ResolvedGraphLinearScanBuilder()
                          .set_column_list(result_col_list)
                          .add_scan_list(
                              ToBuilder(std::move(result.resolved_node))
                                  .set_input_scan(
                                      ResolvedSingleRowScanBuilder()))
                          .add_scan_list(
                              ResolvedProjectScanBuilder()
                                  .set_column_list(result_col_list)
                                  .add_expr_list(MakeResolvedComputedColumn(
                                      result_col_list.back(),
                                      MakeResolvedLiteral(Value::Bool(true))))
                                  .set_input_scan(BuildGraphRefScan(
                                      std::move(result.graph_name_lists)
                                          .singleton_name_list)))))
          .Build());
  return absl::OkStatus();
}

absl::Status GraphTableQueryResolver::ResolveGqlLinearOpsQuery(
    const ASTGqlLinearOpsQuery* query, const NameScope* scope,
    std::unique_ptr<const ResolvedScan>* output,
    NameListPtr* output_name_list) {
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_ADVANCED_QUERY)) {
    return MakeSqlErrorAt(query) << "GQL subquery is not supported";
  }
  GOOGLESQL_RET_CHECK_NE(output, nullptr);
  GOOGLESQL_RET_CHECK_NE(output_name_list, nullptr);
  GOOGLESQL_RET_CHECK_NE(query, nullptr);
  GOOGLESQL_RET_CHECK_NE(query->linear_ops(), nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(auto callback, HandleGraphReference(query));
  absl::Cleanup cleanup = [&callback]() { callback(); };

  GOOGLESQL_ASSIGN_OR_RETURN(auto input_scan, ResolvedSingleRowScanBuilder().Build());
  GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_input,
                   ResolveGqlOperatorList(
                       query->linear_ops()->operators(), scope,
                       {.resolved_node = std::move(input_scan),
                        .graph_name_lists = CreateEmptyGraphNameLists(query)},
                       /*is_first_statement_in_graph_query=*/true));

  ResolvedColumn column(resolver_->AllocateColumnId(), kGraphTableName,
                        resolver_->MakeIdString("literal_true"),
                        types::BoolType());

  ResolvedColumnList result_col_list{column};
  // RecordColumnAccess to avoid being pruned.
  resolver_->RecordColumnAccess(result_col_list);

  auto name_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(column.name_id(), column,
                                       /*is_explicit=*/false));
  *output_name_list = std::move(name_list);

  GOOGLESQL_ASSIGN_OR_RETURN(
      *output,
      ResolvedGraphTableScanBuilder()
          .set_column_list(result_col_list)
          .set_property_graph(graph_)
          .set_input_scan(
              ResolvedGraphLinearScanBuilder()
                  .set_column_list(result_col_list)
                  .add_scan_list(
                      ToBuilder(std::move(resolved_input.resolved_node))
                          .set_column_list(result_col_list)
                          .add_scan_list(
                              ResolvedProjectScanBuilder()
                                  .set_column_list(result_col_list)
                                  .add_expr_list(MakeResolvedComputedColumn(
                                      result_col_list.back(),
                                      MakeResolvedLiteral(Value::Bool(true))))
                                  .set_input_scan(BuildGraphRefScan(
                                      resolved_input.graph_name_lists
                                          .singleton_name_list)))))
          .Build());
  return absl::OkStatus();
}

absl::StatusOr<IdString> GraphTableQueryResolver::GetColumnName(
    const ASTSelectColumn* column) const {
  if (column->alias() != nullptr) {
    return column->alias()->GetAsIdString();
  }

  const auto* column_expr = column->expression();
  if (column_expr->node_kind() != AST_PATH_EXPRESSION) {
    return MakeSqlErrorAt(column)
           << "A name must be explicitly defined for this column";
  }
  return column_expr->GetAsOrDie<ASTPathExpression>()
      ->last_name()
      ->GetAsIdString();
}

IdString GraphTableQueryResolver::ComputeElementAlias(
    const ASTIdentifier* ast_id) {
  if (ast_id != nullptr) {
    return ast_id->GetAsIdString();
  }

  // Arbitrary locally unique name.
  return resolver_->MakeIdString(
      absl::StrCat("$element", ++element_var_count_));
}

template <typename T>
absl::StatusOr<ElementTableSet> GetTablesSatisfyingLabelExpr(
    const PropertyGraph& property_graph,
    const ResolvedGraphLabelExpr* label_expr,
    const absl::flat_hash_set<const T*>& tables) {
  static_assert(
      std::is_base_of<GraphElementTable, T>::value,
      "Type parameter must have base class of type GraphElementTable");
  ElementTableSet matching_element_tables;
  for (const GraphElementTable* table : tables) {
    GOOGLESQL_ASSIGN_OR_RETURN(const GraphDynamicLabel* dynamic_label,
                     GetDynamicLabelOfElementTable(*table));

    absl::flat_hash_set<const GraphElementLabel*> label_set;
    GOOGLESQL_RETURN_IF_ERROR(table->GetLabels(label_set));
    GOOGLESQL_ASSIGN_OR_RETURN(auto label_expr_satisfied,
                     ElementLabelsSatisfyResolvedGraphLabelExpr(
                         label_set, dynamic_label, label_expr));
    if (label_expr_satisfied != LabelSatisfyResult::kFalse) {
      matching_element_tables.insert(table);
    }
  }
  return matching_element_tables;
}

absl::StatusOr<ElementTableSet> GetMatchingElementTables(
    const PropertyGraph& property_graph,
    const ResolvedGraphLabelExpr* label_expr,
    const GraphElementTable::Kind element_kind) {
  GOOGLESQL_RET_CHECK_NE(label_expr, nullptr);

  // For each element table in the graph, determine whether its set of exposed
  // labels satisfies the given label expression.
  switch (element_kind) {
    case GraphElementTable::Kind::kNode: {
      absl::flat_hash_set<const GraphNodeTable*> node_tables;
      GOOGLESQL_RETURN_IF_ERROR(property_graph.GetNodeTables(node_tables));
      return GetTablesSatisfyingLabelExpr(property_graph, label_expr,
                                          node_tables);
    }
    case GraphElementTable::Kind::kEdge: {
      absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
      GOOGLESQL_RETURN_IF_ERROR(property_graph.GetEdgeTables(edge_tables));
      return GetTablesSatisfyingLabelExpr(property_graph, label_expr,
                                          edge_tables);
    }
  }
}

absl::Status GetPropertySet(const ElementTableSet& element_tables,
                            PropertySet& static_properties,
                            DynamicPropertyMap& dynamic_properties) {
  static_properties.clear();
  dynamic_properties.clear();
  for (const auto* element_table : element_tables) {
    absl::flat_hash_set<const GraphPropertyDefinition*> properties;
    GOOGLESQL_RETURN_IF_ERROR(element_table->GetPropertyDefinitions(properties));
    for (const auto* prop : properties) {
      static_properties.insert(&prop->GetDeclaration());
    }
    if (element_table->HasDynamicProperties()) {
      const GraphDynamicProperties* dynamic = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicProperties(dynamic));
      dynamic_properties.try_emplace(element_table, dynamic);
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<const GraphElementType*>
GraphTableQueryResolver::MakeGraphElementType(
    const GraphElementTable::Kind table_kind, const PropertySet& properties,
    TypeFactory* type_factory, bool is_dynamic) {
  std::vector<GraphElementType::PropertyType> property_types;
  property_types.reserve(properties.size());
  for (const auto* property : properties) {
    property_types.emplace_back(property->Name(), property->Type());
  }

  GraphElementType::ElementKind element_kind =
      table_kind == GraphElementTable::Kind::kNode
          ? GraphElementType::ElementKind::kNode
          : GraphElementType::ElementKind::kEdge;

  const GraphElementType* type = nullptr;
  // TODO: Only use NamePath when it's ready in Spanner, and the
  // SimpleCatalog is improved to support nested name path.
  if (is_dynamic) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeDynamicGraphElementType(
        graph_->NamePath().empty() ? std::vector<std::string>{graph_->Name()}
                                   : graph_->NamePath(),
        element_kind, property_types, &type));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeGraphElementType(
        graph_->NamePath().empty() ? std::vector<std::string>{graph_->Name()}
                                   : graph_->NamePath(),
        element_kind, property_types, &type));
  }
  return type;
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>
GraphTableQueryResolver::ResolveGraphLabelFilter(
    const ASTGraphLabelFilter* ast_graph_label_filter,
    const GraphElementTable::Kind element_kind) const {
  // If no label filter is specified, we return the label expression % | !%
  // which matches all element tables even those without labels.
  if (ast_graph_label_filter == nullptr) {
    return ResolvedGraphLabelNaryExprBuilder()
        .set_op(ResolvedGraphLabelNaryExprEnums::OR)
        .add_operand_list(ResolvedGraphWildCardLabelBuilder())
        .add_operand_list(
            ResolvedGraphLabelNaryExprBuilder()
                .set_op(ResolvedGraphLabelNaryExprEnums::NOT)
                .add_operand_list(ResolvedGraphWildCardLabelBuilder()))
        .Build();
  }

  absl::flat_hash_map<const GraphElementTable*, const GraphDynamicLabel*>
      dynamic_labels;
  // Construct a set of valid static labels. A valid static label contained
  // within a label expression belonging to a node or edge pattern must be
  // exposed by some node table or edge table, respectively.
  absl::flat_hash_set<const GraphElementLabel*> valid_static_labels;
  GOOGLESQL_RETURN_IF_ERROR(FindAllLabelsApplicableToElementKind(
      *graph_, element_kind, valid_static_labels, dynamic_labels));
  return ResolveGraphLabelExpr(
      ast_graph_label_filter->label_expression(), element_kind,
      valid_static_labels, graph_,
      /*supports_dynamic_labels=*/
      resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE),
      /*element_table_contains_dynamic_label=*/!dynamic_labels.empty());
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::ResolveWhereClause(
    const ASTWhereClause* ast_where_clause, const NameScope* input_scope,
    QueryResolutionInfo* query_resolution_info, bool allow_analytic,
    bool allow_horizontal_aggregate) const {
  static constexpr char kWhereClause[] = "WHERE clause";

  if (ast_where_clause == nullptr) {
    return nullptr;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> output,
                   ResolveExpr(ast_where_clause->expression(), input_scope,
                               query_resolution_info, allow_analytic,
                               allow_horizontal_aggregate, kWhereClause));
  GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToBool(ast_where_clause->expression(),
                                              kWhereClause, &output));
  return output;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::ResolveCostExpr(
    const ASTExpression& ast_edge_cost, const NameScope* input_scope,
    QueryResolutionInfo* query_resolution_info,
    const Type* cost_supertype) const {
  static constexpr char kCostExpr[] = "COST expression";

  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info, input_scope,
      ExprResolutionInfoOptions{.allows_aggregation = false,
                                .allows_analytic = false,
                                .allows_horizontal_aggregation = false,
                                .clause_name = kCostExpr});
  std::unique_ptr<const ResolvedExpr> output;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(&ast_edge_cost,
                                         expr_resolution_info.get(), &output));

  if (cost_supertype != nullptr) {
    auto make_error_msg = [](absl::string_view target_type_name,
                             absl::string_view actual_type_name) {
      return absl::Substitute(
          "COST expression should return type $0, but returns $1",
          target_type_name, actual_type_name);
    };
    GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
        &ast_edge_cost, {cost_supertype, output->type_annotation_map()},
        Resolver::CoercionMode::kImplicitCoercion, make_error_msg, &output));
  }
  if (output->Is<ResolvedLiteral>() &&
      output->GetAs<ResolvedLiteral>()->value().is_null()) {
    return MakeSqlErrorAt(ast_edge_cost)
           << "COST expression cannot be NULL literal";
  }
  return output;
}

void GraphTableQueryResolver::RecordArrayColumnsForPathMode(
    std::vector<std::unique_ptr<const ResolvedGraphPathScan>>& path_scans) {
  for (auto& top_level_scan : path_scans) {
    for (auto [path_mode, path_mode_scan] :
         CollectPathScansForPathMode(*top_level_scan)) {
      switch (path_mode) {
        case ResolvedGraphPathModeEnums::SIMPLE:
        case ResolvedGraphPathModeEnums::ACYCLIC: {
          PathModeScalarToArrayVarMap scalar_to_array_var_map;
          RecordArrayColumnsForPathModeForSinglePathScan(
              path_mode_scan, /*recursion_level=*/0,
              /*record_nodes=*/true, scalar_to_array_var_map);
          break;
        }
        case ResolvedGraphPathModeEnums::TRAIL: {
          PathModeScalarToArrayVarMap scalar_to_array_var_map;
          RecordArrayColumnsForPathModeForSinglePathScan(
              path_mode_scan, /*recursion_level=*/0,
              /*record_nodes=*/false, scalar_to_array_var_map);
          break;
        }
        default:
          // Nothing needs to be recorded for other path modes.
          break;
      }
    }
  }
}

GraphTableQueryResolver::PathModeScansVector
GraphTableQueryResolver::CollectPathScansForPathMode(
    const ResolvedGraphPathScan& path_scan) {
  PathModeScansVector result;
  auto path_mode = path_scan.path_mode() == nullptr
                       ? ResolvedGraphPathModeEnums::PATH_MODE_UNSPECIFIED
                       : path_scan.path_mode()->path_mode();
  if (path_mode != ResolvedGraphPathModeEnums::PATH_MODE_UNSPECIFIED &&
      path_mode != ResolvedGraphPathModeEnums::WALK) {
    result.push_back({path_mode, &path_scan});
  }
  for (const auto& input_scan : path_scan.input_scan_list()) {
    if (input_scan->Is<ResolvedGraphPathScan>()) {
      auto sub_result = CollectPathScansForPathMode(
          *input_scan->GetAs<ResolvedGraphPathScan>());
      result.insert(result.end(), sub_result.begin(), sub_result.end());
    }
  }
  return result;
}

void GraphTableQueryResolver::RecordArrayColumnsForPathModeForSinglePathScan(
    const ResolvedGraphPathScan* path_scan, const int recursion_level,
    bool record_nodes, PathModeScalarToArrayVarMap& scalar_to_array_var_map) {
  // If `scalar_to_group_var[i]` is set to `j`, then column #i in the current
  // GraphPathScan maps to the j-th group variable in `group_variable_list`.
  for (const auto& group_var : path_scan->group_variable_list()) {
    int column_id = group_var->element().column_id();
    scalar_to_array_var_map[column_id] = {recursion_level, &group_var->array()};
  }
  for (auto& scan : path_scan->input_scan_list()) {
    if (scan->Is<ResolvedGraphPathScan>()) {
      RecordArrayColumnsForPathModeForSinglePathScan(
          scan->GetAs<ResolvedGraphPathScan>(), recursion_level + 1,
          record_nodes, scalar_to_array_var_map);
      // Quantified path variables are scalar variables in the GraphPathScan
      // that defines their column map, therefore we don't check node or edge
      // scans when `recursion_level == 0`.
    } else if ((recursion_level > 0) &&
               ((record_nodes && scan->Is<ResolvedGraphNodeScan>()) ||
                (!record_nodes && scan->Is<ResolvedGraphEdgeScan>()))) {
      int scalar_column_id = scan->column_list(0).column_id();
      auto it = scalar_to_array_var_map.find(scalar_column_id);
      if (it != scalar_to_array_var_map.end()) {
        const auto [var_level, array_var] = it->second;
        if (var_level > 0) {
          resolver_->RecordColumnAccess(*array_var);
        }
      }
    }
  }
}

absl::StatusOr<GraphTableQueryResolver::ResolvedGraphWithNameList<
    const ResolvedGraphElementScan>>
GraphTableQueryResolver::ResolveElementPattern(
    const ASTGraphElementPattern& ast_element_pattern,
    const NameScope* input_scope,
    GraphTableNamedVariables input_graph_name_lists,
    const Type* cost_supertype) {
  // Note about quantifiers:
  // Syntactically, only paths or edges may have an attached quantifier.
  // All quantified edges are syntactically transformed into a quantified path
  // before we begin resolution, hence, we do not need to handle quantified
  // edges in ResolveElementPattern().
  GOOGLESQL_RET_CHECK(ast_element_pattern.quantifier() == nullptr);

  // Filler can be null for abbreviated edge pattern, semantically its
  // equivalent as an empty filler.
  const ASTGraphElementPatternFiller empty_filler;
  const ASTGraphElementPatternFiller* filler =
      ast_element_pattern.filler() == nullptr ? &empty_filler
                                              : ast_element_pattern.filler();

  // Graph element variable name must be distinct from the ones in a containing
  // GRAPH_TABLE.
  bool has_explicit_name = filler->variable_name() != nullptr;
  if (has_explicit_name) {
    NameTarget existing_name_target;
    if (input_scope->LookupName(filler->variable_name()->GetAsIdString(),
                                &existing_name_target) &&
        existing_name_target.IsColumn() &&
        existing_name_target.column().type()->IsGraphElement()) {
      return MakeSqlErrorAt(filler)
             << "The name "
             << ToSingleQuotedStringLiteral(
                    filler->variable_name()->GetAsStringView())
             << " is already defined; redefining graph element variables in a "
                "subquery is not allowed. To refer to the same "
                "graph element, use a different name and add an explicit "
                "filter that checks for equality";
    }
  }

  const IdString element_alias = ComputeElementAlias(filler->variable_name());
  const GraphElementTable::Kind table_kind =
      GetElementTableKind(ast_element_pattern);

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphLabelExpr> label_expr,
      ResolveGraphElementLabelExpr(filler->variable_name(), table_kind,
                                   filler->label_filter()));

  // Match element tables against the resolved label expression.
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ElementTableSet matching_element_tables,
      GetMatchingElementTables(*graph_, label_expr.get(), table_kind));
  PropertySet static_properties;
  DynamicPropertyMap dynamic_properties;
  GOOGLESQL_RETURN_IF_ERROR(GetPropertySet(matching_element_tables, static_properties,
                                 dynamic_properties));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* graph_element_type,
                   MakeGraphElementType(
                       table_kind, static_properties, resolver_->type_factory_,
                       /*is_dynamic=*/!dynamic_properties.empty()));

  ResolvedColumn element_column(resolver_->AllocateColumnId(),
                                /*table_name=*/kElementTableName, element_alias,
                                graph_element_type);
  // Mark element_column as accessed so analyzer does not consider it unused and
  // prune it from the scan. In some cases, element_column may not be used
  // in the resolved graph_table query e.g.:
  //   SELECT 1 FROM KeyValue kv WHERE EXISTS (
  //     SELECT Key FROM GRAPH_TABLE ( aml MATCH (n) COLUMNS (kv.Key))
  //   )
  // However, element_column carries Type information necessary to execute the
  // query.
  resolver_->RecordColumnAccess(element_column);

  auto element_name_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(element_name_list->AddColumn(element_alias, element_column,
                                               /*is_explicit=*/true));

  // resolve WHERE clause using input_scope and the names produced by this scan.
  const NameScope element_pattern_scope(input_scope, element_name_list);
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(resolver_);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> filter_expr,
      ResolveWhereClause(filler->where_clause(), &element_pattern_scope,
                         query_resolution_info.get(),
                         /*allow_analytic=*/false,
                         /*allow_horizontal_aggregate=*/false));

  // Resolve property specification and populate filter_expr if needed. Note
  // an element should have at most one where clause or property specification.
  // Where clause and property specification cannot co-exist for the same
  // element.
  if (filler->property_specification() != nullptr) {
    // Property specification and Where clause cannot co-exist.
    GOOGLESQL_RET_CHECK(filler->where_clause() == nullptr);
    std::vector<ResolvedColumn> resolved_columns =
        element_name_list->GetResolvedColumns();
    // <element pattern> should have one column in this name list.
    GOOGLESQL_RET_CHECK(resolved_columns.size() == 1);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> property_specification_expr,
        ResolveGraphElementPropertySpecification(
            filler->property_specification(), &element_pattern_scope,
            resolved_columns.at(0),
            /*supports_dynamic_properties=*/
            resolver_->language().LanguageFeatureEnabled(
                FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)));
    filter_expr = std::move(property_specification_expr);
  }

  // Resolve edge cost if present.
  std::unique_ptr<const ResolvedExpr> edge_cost_expr;
  if (filler->edge_cost() != nullptr) {
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_CHEAPEST_PATH)) {
      return MakeSqlErrorAt(filler->edge_cost())
             << "Cost definition is not supported";
    }
    if (ast_element_pattern.node_kind() != AST_GRAPH_EDGE_PATTERN) {
      return MakeSqlErrorAt(filler->edge_cost())
             << "Cost definition is only allowed on a graph edge pattern";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        edge_cost_expr,
        ResolveCostExpr(*filler->edge_cost(), &element_pattern_scope,
                        /*query_resolution_info=*/nullptr, cost_supertype));
    GOOGLESQL_RET_CHECK(edge_cost_expr != nullptr);
    if (!edge_cost_expr->type()->IsNumerical()) {
      return MakeSqlErrorAt(filler->edge_cost())
             << "Cost expression must be a numerical type";
    }
  }

  // Resolve element hints if they're present.
  std::vector<std::unique_ptr<const ResolvedOption>> element_hints;
  if (filler->hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        resolver_->ResolveHintAndAppend(filler->hint(), &element_hints));
  }

  std::vector<const GraphElementTable*> element_table_list = {
      matching_element_tables.begin(), matching_element_tables.end()};
  std::unique_ptr<ResolvedGraphElementScan> output_scan;
  switch (ast_element_pattern.node_kind()) {
    case AST_GRAPH_NODE_PATTERN: {
      GOOGLESQL_ASSIGN_OR_RETURN(output_scan, ResolvedGraphNodeScanBuilder()
                                        .set_column_list({element_column})
                                        .set_filter_expr(std::move(filter_expr))
                                        .set_label_expr(std::move(label_expr))
                                        .set_target_element_table_list(
                                            std::move(element_table_list))
                                        .set_hint_list(std::move(element_hints))
                                        .BuildMutable());
      break;
    }
    case AST_GRAPH_EDGE_PATTERN: {
      const auto* ast_edge_pattern =
          ast_element_pattern.GetAsOrDie<ASTGraphEdgePattern>();
      std::vector<std::unique_ptr<const ResolvedOption>> lhs_hints;
      std::vector<std::unique_ptr<const ResolvedOption>> rhs_hints;
      // Resolve a path hint on a traversal from a node to an edge if present.
      if (ast_edge_pattern->lhs_hint() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveHintAndAppend(
            ast_edge_pattern->lhs_hint()->hint(), &lhs_hints));
      }
      // Subsequently, resolve a path hint on a traversal from an edge to a node
      // if present.
      if (ast_edge_pattern->rhs_hint() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveHintAndAppend(
            ast_edge_pattern->rhs_hint()->hint(), &rhs_hints));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          output_scan,
          ResolvedGraphEdgeScanBuilder()
              .set_column_list({element_column})
              .set_orientation(
                  GetResolvedEdgeOrientation(ast_edge_pattern->orientation()))
              .set_filter_expr(std::move(filter_expr))
              .set_label_expr(std::move(label_expr))
              .set_cost_expr(std::move(edge_cost_expr))
              .set_target_element_table_list(std::move(element_table_list))
              .set_hint_list(std::move(element_hints))
              .set_lhs_hint_list(std::move(lhs_hints))
              .set_rhs_hint_list(std::move(rhs_hints))
              .BuildMutable());
      break;
    }
    default:
      return MakeSqlErrorAt(&ast_element_pattern)
             << "Unexpected node: " << ast_element_pattern.DebugString();
  }
  resolver_->MaybeRecordParseLocation(&ast_element_pattern, output_scan.get());
  // Merge the new element's name with the current working graph name lists.
  GOOGLESQL_ASSIGN_OR_RETURN(auto working_name_lists,
                   CreateGraphNameListsSingletonOnly(&ast_element_pattern,
                                                     element_name_list));
  GOOGLESQL_ASSIGN_OR_RETURN(auto output_graph_name_lists,
                   MergeGraphNameLists(std::move(input_graph_name_lists),
                                       std::move(working_name_lists)));
  return absl::StatusOr<
      ResolvedGraphWithNameList<const ResolvedGraphElementScan>>(
      {.resolved_node = std::move(output_scan),
       .graph_name_lists = std::move(output_graph_name_lists)});
}

class EdgeCostVisitor : public ResolvedASTVisitor {
 public:
  explicit EdgeCostVisitor(InputArgumentTypeSet* cost_types)
      : cost_types_(cost_types) {}

  static absl::Status GetCostTypes(const ResolvedGraphPathScan* path_scan,
                                   InputArgumentTypeSet* cost_types) {
    EdgeCostVisitor visitor = EdgeCostVisitor(cost_types);
    GOOGLESQL_RETURN_IF_ERROR(path_scan->Accept(&visitor));
    return absl::OkStatus();
  }
  // Traverse the path / element children only. Ignore the WHERE and COST
  // clauses since they may contain graph subqueries.
  absl::Status VisitResolvedGraphPathScan(
      const googlesql::ResolvedGraphPathScan* node) override {
    for (const auto& child : node->input_scan_list()) {
      GOOGLESQL_RETURN_IF_ERROR(child->Accept(this));
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedGraphEdgeScan(
      const googlesql::ResolvedGraphEdgeScan* node) override {
    if (node->cost_expr() != nullptr) {
      cost_types_->Insert(InputArgumentType(node->cost_expr()->type()));
    }
    return absl::OkStatus();
  }

 private:
  InputArgumentTypeSet* cost_types_;
};

absl::StatusOr<GraphTableQueryResolver::PathPatternListResult>
GraphTableQueryResolver::ResolvePathPatternList(
    const ASTGraphPattern& ast_graph_pattern, const NameScope* input_scope) {
  GOOGLESQL_RETURN_IF_ERROR(ValidatePathPattern(&ast_graph_pattern, input_scope));
  // Resolves the label expressions in the graph pattern scope.
  GOOGLESQL_RETURN_IF_ERROR(ResolveGraphLabelExpressions(&ast_graph_pattern));
  PathPatternListResult result;
  result.resolved_scan_vector.reserve(ast_graph_pattern.paths().size());

  auto child_name_lists = CreateEmptyGraphNameLists(&ast_graph_pattern);
  ResolvedColumnList output_col_list;
  // Singleton variable names local to each path that cannot be
  // multiply-declared, mapping to their first declaring path. Group vars are
  // rejected separately in `MakeEqualElementExpr` to get a better error
  // message.
  IdStringHashMapCase<const ASTGraphPathPattern* const>
      multiply_decl_forbidden_names_to_err_location;
  for (const auto& path : ast_graph_pattern.paths()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto path_result,
        ResolvePathPattern(*path, input_scope, child_name_lists,
                           /*create_path_column=*/false,
                           &multiply_decl_forbidden_names_to_err_location));

    InputArgumentTypeSet cost_types;
    GOOGLESQL_RETURN_IF_ERROR(EdgeCostVisitor::GetCostTypes(
        path_result.resolved_node.get(), &cost_types));
    const Type* cost_supertype = nullptr;
    if (!cost_types.empty()) {
      GOOGLESQL_RETURN_IF_ERROR(
          resolver_->coercer_.GetCommonSuperType(cost_types, &cost_supertype));
      if (cost_supertype == nullptr) {
        return MakeSqlErrorAt(path)
               << "Edge cost expressions have no valid supertype. Consider "
                  "adding explicit CASTs to the cost expressions. Found types: "
               << absl::StrJoin(
                      cost_types.arguments(), ", ",
                      [&](std::string* out, const InputArgumentType& arg) {
                        absl::StrAppend(out, arg.type()->TypeName(
                                                 resolver_->product_mode()));
                      });
      }
      GOOGLESQL_RET_CHECK(cost_supertype->IsNumerical())
          << "Expected a numerical type for the cost supertype, but got: "
          << cost_supertype->DebugString();
      // Canonicalize the cost supertype to higher precision type to match the
      // behavior of aggregate SUM.
      // The following type widening behavior occurs:
      // - INT32 -> INT64
      // - UINT32 -> UINT64
      // - FLOAT -> DOUBLE
      // Note that NUMERIC and BIGNUMERIC are unchanged.
      switch (cost_supertype->kind()) {
        case TypeKind::TYPE_INT32:
          cost_supertype = resolver_->type_factory_->get_int64();
          break;
        case TypeKind::TYPE_UINT32:
          cost_supertype = resolver_->type_factory_->get_uint64();
          break;
        case TypeKind::TYPE_FLOAT:
          cost_supertype = resolver_->type_factory_->get_double();
          break;
        default:
          break;
      }
      // Second pass resolution, this time passing down the computed cost
      // supertype to all descendants
      GOOGLESQL_ASSIGN_OR_RETURN(
          path_result,
          ResolvePathPattern(*path, input_scope, child_name_lists,
                             /*create_path_column=*/false,
                             &multiply_decl_forbidden_names_to_err_location,
                             cost_supertype));
    }

    output_col_list.insert(output_col_list.end(),
                           path_result.resolved_node->column_list().begin(),
                           path_result.resolved_node->column_list().end());
    result.resolved_scan_vector.push_back(std::move(path_result.resolved_node));
    child_name_lists = std::move(path_result.graph_name_lists);
  }

  // Record the column names of array variables of group variables that will be
  // used by the path mode implementation.
  RecordArrayColumnsForPathMode(result.resolved_scan_vector);

  GOOGLESQL_ASSIGN_OR_RETURN(
      ResolvedGraphWithNameList<const ResolvedExpr> multiply_decl_result,
      ResolveMultiplyDeclaredVariables(
          ast_graph_pattern, child_name_lists,
          &multiply_decl_forbidden_names_to_err_location));
  result.resolved_column_list = std::move(output_col_list);
  result.filter_expr = std::move(multiply_decl_result.resolved_node);
  result.graph_name_lists = std::move(multiply_decl_result.graph_name_lists);
  return result;
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedGraphScan>>
GraphTableQueryResolver::ResolveGraphPattern(
    const ASTGraphPattern& ast_graph_pattern, const NameScope* input_scope,
    GraphTableNamedVariables input_graph_name_lists) {
  // Names imported from the previous statement cannot be accessed
  // from within the pattern. Names can be accessed in the WHERE clause of
  // MATCH.
  auto disallowed_list = std::make_shared<NameList>();
  if (input_graph_name_lists.correlated_name_list != nullptr) {
    GOOGLESQL_RET_CHECK_OK(
        disallowed_list->MergeFrom(*input_graph_name_lists.correlated_name_list,
                                   input_graph_name_lists.ast_node));
  }
  GOOGLESQL_RET_CHECK_OK(
      disallowed_list->MergeFrom(*input_graph_name_lists.singleton_name_list,
                                 input_graph_name_lists.ast_node));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto restricted_scope,
      CreateNameScopeWithDisallowList(
          input_scope,
          {disallowed_list.get(), input_graph_name_lists.group_name_list.get()},
          [](const IdString& name) {
            return absl::StrCat(
                "Name ", ToSingleQuotedStringLiteral(name.ToStringView()),
                ", defined in the previous statement, can only "
                "be referenced in the outermost WHERE clause of MATCH");
          }));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto path_list_result,
      ResolvePathPatternList(ast_graph_pattern, restricted_scope.get()));
  ResolvedColumnList& graph_pattern_scan_column_list =
      path_list_result.resolved_column_list;

  // Resolve cross-statement multiply-declared variables and WHERE clause,
  // preferring the variable occurrence in the graph pattern.
  GOOGLESQL_ASSIGN_OR_RETURN(auto multiply_decl_result,
                   ResolveMultiplyDeclaredVariables(
                       ast_graph_pattern, input_graph_name_lists,
                       path_list_result.graph_name_lists));

  auto& [cross_stmt_multiply_decl_filter_expr, path_list_name_lists] =
      multiply_decl_result;
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto filter_expr,
      MakeGraphPatternFilterExpr(
          &ast_graph_pattern, path_list_name_lists,
          std::move(path_list_result.filter_expr),
          std::move(cross_stmt_multiply_decl_filter_expr), input_scope));

  std::vector<std::unique_ptr<const ResolvedGraphPathScan>> input_scan_list =
      std::move(path_list_result.resolved_scan_vector);
  GOOGLESQL_ASSIGN_OR_RETURN(auto output_scan,
                   ResolvedGraphScanBuilder()
                       .set_column_list(graph_pattern_scan_column_list)
                       .set_input_scan_list(std::move(input_scan_list))
                       .set_filter_expr(std::move(filter_expr))
                       .Build());

  return {{.resolved_node = std::move(output_scan),
           .graph_name_lists = std::move(path_list_name_lists)}};
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphPathPatternQuantifier>>
GraphTableQueryResolver::ResolveGraphPathPatternQuantifier(
    const NameScope* name_scope, const ASTQuantifier* ast_quantifier) {
  // Graph quantifiers are never reluctant. The syntax doesn't allow it.
  // It also does not allow symbol quantifiers such as `+` or `*`.
  GOOGLESQL_RET_CHECK(!ast_quantifier->is_reluctant())
      << "Graph quantifiers are never reluctant.";

  std::unique_ptr<const ResolvedExpr> lower_bound_expr;
  std::unique_ptr<const ResolvedExpr> upper_bound_expr;

  const ASTExpression* ast_lower_quantifier = nullptr;
  const ASTExpression* ast_upper_quantifier = nullptr;
  switch (ast_quantifier->node_kind()) {
    case AST_FIXED_QUANTIFIER: {
      ast_lower_quantifier =
          ast_quantifier->GetAsOrDie<ASTFixedQuantifier>()->bound();
      ast_upper_quantifier = ast_lower_quantifier;
      GOOGLESQL_RETURN_IF_ERROR(GetQuantifierBoundExpr(ast_lower_quantifier, name_scope,
                                             &lower_bound_expr));

      // The upper bound is identical to the lower one for fixed quantifiers.
      GOOGLESQL_ASSIGN_OR_RETURN(upper_bound_expr, ResolvedASTDeepCopyVisitor::Copy(
                                             lower_bound_expr.get()));
      break;
    }
    case AST_BOUNDED_QUANTIFIER: {
      // Fetch the mandatory upper bound.
      const auto* bounded_quantifier =
          ast_quantifier->GetAsOrDie<ASTBoundedQuantifier>();
      ast_lower_quantifier = bounded_quantifier->lower_bound()->bound();
      ast_upper_quantifier = bounded_quantifier->upper_bound()->bound();

      // The upper bound is optional. An omitted upper bound is unbounded.
      if (ast_upper_quantifier != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(GetQuantifierBoundExpr(ast_upper_quantifier, name_scope,
                                               &upper_bound_expr));
      }

      // The lower bound is optional. Fetch it if it's present.
      if (ast_lower_quantifier != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(GetQuantifierBoundExpr(ast_lower_quantifier, name_scope,
                                               &lower_bound_expr));
      } else {
        // If lower bound is omitted, consider it as an INT64 literal with the
        // value '0'.
        lower_bound_expr = googlesql::MakeResolvedLiteral(
            resolver_->type_factory_->get_int64(), Value::Int64(0));
      }
      break;
    }
    case AST_SYMBOL_QUANTIFIER: {
      const auto symbol =
          ast_quantifier->GetAsOrDie<ASTSymbolQuantifier>()->symbol();
      GOOGLESQL_RET_CHECK(symbol == ASTSymbolQuantifier::PLUS ||
                symbol == ASTSymbolQuantifier::STAR);
      if (symbol == ASTSymbolQuantifier::PLUS) {
        lower_bound_expr = googlesql::MakeResolvedLiteral(
            resolver_->type_factory_->get_int64(), Value::Int64(1));
      } else {
        lower_bound_expr = googlesql::MakeResolvedLiteral(
            resolver_->type_factory_->get_int64(), Value::Int64(0));
      }
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected quantifier type: "
                       << ast_quantifier->DebugString();
  }

  // Validate the lower and upper bounds if they are given as literals.
  // If the bounds are given as parameters, we cannot validate them at query
  // compile time. We simply confirm that they're a constant and expect
  // parameters to be validated at runtime.
  int64_t lower_bound, upper_bound;
  GOOGLESQL_RET_CHECK_NE(lower_bound_expr, nullptr);
  if (lower_bound_expr->Is<ResolvedLiteral>()) {
    lower_bound =
        lower_bound_expr->GetAs<ResolvedLiteral>()->value().int64_value();
    if (lower_bound < 0) {
      return MakeSqlErrorAt(ast_lower_quantifier)
             << "Value of lower bound must be an unsigned integer";
    }
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_lower_constant_expr,
                     IsConstantExpression(lower_bound_expr.get()));
    if (!is_lower_constant_expr) {
      return MakeSqlErrorAt(ast_lower_quantifier)
             << "lower bound expression must be constant";
    }
  }

  if (upper_bound_expr == nullptr) {
    return MakeResolvedGraphPathPatternQuantifier(std::move(lower_bound_expr),
                                                  std::move(upper_bound_expr));
  }
  if (upper_bound_expr->Is<ResolvedLiteral>()) {
    upper_bound =
        upper_bound_expr->GetAs<ResolvedLiteral>()->value().int64_value();
    if (upper_bound <= 0) {
      return MakeSqlErrorAt(ast_upper_quantifier)
             << "Value of upper bound must be greater than zero";
    }
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_upper_constant_expr,
                     IsConstantExpression(upper_bound_expr.get()));
    if (!is_upper_constant_expr) {
      return MakeSqlErrorAt(ast_upper_quantifier)
             << "upper bound expression must be constant";
    }
  }
  if (lower_bound_expr->Is<ResolvedLiteral>() &&
      upper_bound_expr->Is<ResolvedLiteral>()) {
    if (lower_bound > upper_bound) {
      return MakeSqlErrorAt(ast_lower_quantifier)
             << "Invalid ast_lower_quantifier: lower bound cannot be greater "
                "than upper bound: "
             << lower_bound << " > " << upper_bound;
    }
  }
  return MakeResolvedGraphPathPatternQuantifier(std::move(lower_bound_expr),
                                                std::move(upper_bound_expr));
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphPathMode>>
GraphTableQueryResolver::ResolvePathMode(
    const ASTGraphPathMode* ast_path_mode) {
  std::unique_ptr<const ResolvedGraphPathMode> resolved_path_mode;
  ResolvedGraphPathMode::PathMode path_mode =
      GetResolvedPathMode(ast_path_mode);
  if (path_mode != ResolvedGraphPathMode::PATH_MODE_UNSPECIFIED) {
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_PATH_MODE)) {
      return MakeSqlErrorAt(ast_path_mode) << "path mode is not supported";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_path_mode,
        ResolvedGraphPathModeBuilder().set_path_mode(path_mode).Build());
  }
  return resolved_path_mode;
}

absl::Status GraphTableQueryResolver::GetSearchPrefixPathCountExpr(
    const ASTExpression* ast_search_prefix_path_count,
    const NameScope* input_scope,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) const {
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_PATH_SEARCH_PREFIX_PATH_COUNT)) {
    return MakeSqlErrorAt(ast_search_prefix_path_count)
           << "Path search prefix with path count is not supported";
  }

  static constexpr char kSearchPrefix[] = "graph search prefix k";
  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(input_scope, kSearchPrefix);
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(ast_search_prefix_path_count,
                                         expr_resolution_info.get(),
                                         resolved_expr_out));
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
      kSearchPrefix, ast_search_prefix_path_count, resolved_expr_out));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphPathSearchPrefix>>
GraphTableQueryResolver::ResolvePathSearchPrefix(
    const ASTGraphPathSearchPrefix* ast_path_search_prefix,
    const NameScope* name_scope) {
  if (ast_path_search_prefix == nullptr) {
    return nullptr;
  }

  std::unique_ptr<const ResolvedExpr> path_count_expr = nullptr;
  if (ast_path_search_prefix->path_count() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(GetSearchPrefixPathCountExpr(
        ast_path_search_prefix->path_count()->path_count(), name_scope,
        &path_count_expr));

    if (path_count_expr->Is<ResolvedLiteral>()) {
      int64_t path_count_value =
          path_count_expr->GetAs<ResolvedLiteral>()->value().int64_value();
      if (path_count_value < 0) {
        return MakeSqlErrorAt(ast_path_search_prefix->path_count())
               << "Value of search prefix path count must be a non-negative "
                  "integer";
      }
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(bool is_constant_expr,
                       IsConstantExpression(path_count_expr.get()));
      if (!is_constant_expr) {
        return MakeSqlErrorAt(ast_path_search_prefix->path_count())
               << "Search prefix path count expression must be constant";
      }
    }
  }

  auto builder = ResolvedGraphPathSearchPrefixBuilder();
  switch (ast_path_search_prefix->type()) {
    case ASTGraphPathSearchPrefix::ANY:
      return ResolvedGraphPathSearchPrefixBuilder()
          .set_type(ResolvedGraphPathSearchPrefix::ANY)
          .set_path_count(std::move(path_count_expr))
          .Build();
    case ASTGraphPathSearchPrefix::SHORTEST:
      return ResolvedGraphPathSearchPrefixBuilder()
          .set_type(ResolvedGraphPathSearchPrefix::SHORTEST)
          .set_path_count(std::move(path_count_expr))
          .Build();
    case ASTGraphPathSearchPrefix::ALL:
      // Drop "ALL" because it's a no-op for now.
      return nullptr;
    case ASTGraphPathSearchPrefix::ALL_SHORTEST:
      // Unimplemented for now.
      return MakeSqlErrorAt(ast_path_search_prefix)
             << "ALL SHORTEST search is unimplemented";
    case ASTGraphPathSearchPrefix::CHEAPEST:
      if (!resolver_->language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_CHEAPEST_PATH)) {
        return MakeSqlErrorAt(ast_path_search_prefix)
               << "CHEAPEST search is unimplemented";
      }
      return ResolvedGraphPathSearchPrefixBuilder()
          .set_type(ResolvedGraphPathSearchPrefix::CHEAPEST)
          .set_path_count(std::move(path_count_expr))
          .Build();
    case ASTGraphPathSearchPrefix::ALL_CHEAPEST:
      if (!resolver_->language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_CHEAPEST_PATH)) {
        return MakeSqlErrorAt(ast_path_search_prefix)
               << "CHEAPEST search is unimplemented";
      }
      return MakeSqlErrorAt(ast_path_search_prefix)
             << "ALL CHEAPEST search is not supported";
    case ASTGraphPathSearchPrefix::PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED:
      GOOGLESQL_RET_CHECK_FAIL()
          << "The parser should never generate a non-null "
             "ASTGraphPathSearchPrefix node with the prefix UNSPECIFIED";
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected search prefix type: "
                       << ast_path_search_prefix->type();
  }
}

absl::StatusOr<ResolvedColumn> GraphTableQueryResolver::GeneratePathColumn(
    const ASTIdentifier* ast_path_name,
    const ResolvedColumnList& path_scan_column_list,
    GraphTableNamedVariables multiply_decl_namelists) {
  InputArgumentTypeSet node_types;
  InputArgumentTypeSet edge_types;
  InputArgumentTypeSet path_types;
  for (const ResolvedColumn& column : path_scan_column_list) {
    const Type* type = column.type();
    if (type->IsArray()) {
      type = type->AsArray()->element_type();
    }
    if (type->IsGraphPath()) {
      path_types.Insert(InputArgumentType(type));
    } else {
      GOOGLESQL_RET_CHECK(type->IsGraphElement());
      if (type->AsGraphElement()->IsNode()) {
        node_types.Insert(InputArgumentType(type));
      } else {
        edge_types.Insert(InputArgumentType(type));
      }
    }
  }
  const Type* node_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      resolver_->coercer_.GetCommonSuperType(node_types, &node_type));
  GOOGLESQL_RET_CHECK_NE(node_type, nullptr)
      << "Unable to find common node supertype from "
      << node_types.ToString(/*verbose=*/true);
  GOOGLESQL_RET_CHECK(node_type->IsGraphElement());
  const GraphElementType* edge_type = nullptr;
  if (edge_types.empty()) {
    // There are no edges as part of this path so we need to create an edge
    // type with no properties.
    GOOGLESQL_RETURN_IF_ERROR(resolver_->type_factory_->MakeGraphElementType(
        node_type->AsGraphElement()->graph_reference(),
        GraphElementType::ElementKind::kEdge,
        /*property_types=*/{}, &edge_type));
  } else {
    const Type* edge_super_type;
    GOOGLESQL_RETURN_IF_ERROR(
        resolver_->coercer_.GetCommonSuperType(edge_types, &edge_super_type));
    GOOGLESQL_RET_CHECK(edge_super_type != nullptr)
        << "Unable to find common edge supertype from "
        << edge_types.ToString(/*verbose=*/true);
    GOOGLESQL_RET_CHECK(edge_super_type->IsGraphElement());
    edge_type = edge_super_type->AsGraphElement();
  }
  const GraphPathType* path_type;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->type_factory_->MakeGraphPathType(
      node_type->AsGraphElement(), edge_type, &path_type));
  path_types.Insert(InputArgumentType(path_type));
  const Type* path_super_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      resolver_->coercer_.GetCommonSuperType(path_types, &path_super_type));
  GOOGLESQL_RET_CHECK_NE(path_super_type, nullptr)
      << "Unable to find common path supertype from "
      << path_types.ToString(/*verbose=*/true);
  GOOGLESQL_RET_CHECK(path_super_type->IsGraphPath());
  IdString path_name = kDefaultPathColumnName;
  if (ast_path_name != nullptr) {
    path_name = ast_path_name->GetAsIdString();
  }
  ResolvedColumn result(resolver_->AllocateColumnId(), kGraphTableName,
                        path_name, path_super_type);
  if (ast_path_name != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(multiply_decl_namelists.singleton_name_list->AddColumn(
        result.name_id(), result,
        /*is_explicit=*/true));
  }
  return result;
}

absl::StatusOr<GraphTableQueryResolver::ResolvedGraphWithNameList<
    const ResolvedGraphPathScan>>
GraphTableQueryResolver::ResolvePathPattern(
    const ASTGraphPathPattern& ast_path_pattern, const NameScope* input_scope,
    GraphTableNamedVariables input_graph_name_lists, bool create_path_column,
    IdStringHashMapCase<const ASTGraphPathPattern* const>*
        out_multiply_decl_forbidden_names_to_err_location,
    const Type* cost_supertype) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const ASTGraphPathBase*> ast_path_bases,
                   CanonicalizePathPattern(ast_path_pattern));

  std::vector<std::unique_ptr<const ResolvedGraphPathScanBase>> input_scans;
  input_scans.reserve(ast_path_bases.size());
  ResolvedColumnList path_scan_column_list;
  GOOGLESQL_RET_CHECK(!ast_path_bases.empty());
  if (ast_path_pattern.path_name() != nullptr) {
    create_path_column = true;
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_PATH_TYPE)) {
      return MakeSqlErrorAt(ast_path_pattern) << "Paths are not supported";
    }
    if (ast_path_pattern.parenthesized()) {
      return MakeSqlErrorAt(ast_path_pattern.path_name())
             << "Path variables cannot be assigned on subpaths";
    }
  }
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_BOUNDED_PATH_QUANTIFICATION) &&
      IsQuantified(&ast_path_pattern)) {
    return MakeSqlErrorAt(ast_path_pattern)
           << "Graph query with quantifiers is not supported";
  }
  if (ast_path_pattern.path_mode() != nullptr &&
      ast_path_pattern.search_prefix() != nullptr) {
    return MakeSqlErrorAt(ast_path_pattern)
           << "Path pattern with path mode and search prefix is not allowed";
  }

  auto path_name_lists = CreateEmptyGraphNameLists(&ast_path_pattern);
  for (const auto* ast_path_base : ast_path_bases) {
    GOOGLESQL_RET_CHECK_NE(ast_path_base, nullptr);
    std::unique_ptr<const ResolvedGraphPathScanBase> path_base_scan;
    if (const auto ast_element_pattern =
            ast_path_base->GetAsOrNull<ASTGraphElementPattern>();
        ast_element_pattern != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto element_result,
          ResolveElementPattern(*ast_element_pattern, input_scope,
                                std::move(path_name_lists), cost_supertype));
      path_base_scan = std::move(element_result.resolved_node);
      path_name_lists = std::move(element_result.graph_name_lists);
      GOOGLESQL_RET_CHECK_EQ(path_base_scan->column_list().size(), 1);
    } else {
      GOOGLESQL_RET_CHECK(ast_path_base->Is<ASTGraphPathPattern>())
          << "ASTGraphPathBase node is unsupported type "
          << ast_path_base->node_kind();
      const auto* ast_path_pattern =
          ast_path_base->GetAsOrDie<ASTGraphPathPattern>();
      if (ast_path_pattern->search_prefix() != nullptr) {
        return MakeSqlErrorAt(ast_path_pattern->search_prefix())
               << "Path search prefix is not supported in a subpath";
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto subpath_result,
          ResolvePathPattern(
              *ast_path_pattern, input_scope, std::move(path_name_lists),
              create_path_column,
              /*out_multiply_decl_forbidden_names_to_err_location=*/nullptr,
              cost_supertype));
      path_base_scan = std::move(subpath_result.resolved_node);
      path_name_lists = std::move(subpath_result.graph_name_lists);
    }
    for (const ResolvedColumn& column : path_base_scan->column_list()) {
      if (!column.type()->IsGraphPath()) {
        path_scan_column_list.push_back(column);
      }
    }
    input_scans.push_back(std::move(path_base_scan));
  }
  std::vector<std::unique_ptr<const ResolvedExpr>> filter_expr_conjuncts;
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto multiply_decl_result,
      ResolveMultiplyDeclaredVariables(ast_path_pattern, path_name_lists));

  auto& [filter_expr, multiply_decl_namelists] = multiply_decl_result;

  if (filter_expr != nullptr) {
    filter_expr_conjuncts.push_back(std::move(filter_expr));
  }

  std::optional<ResolvedColumn> path_column;
  if (create_path_column) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        path_column,
        GeneratePathColumn(ast_path_pattern.path_name(), path_scan_column_list,
                           multiply_decl_namelists));
  }

  const NameScope path_pattern_scope(
      input_scope, multiply_decl_namelists.singleton_name_list);
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(resolver_);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> where_expr,
      ResolveWhereClause(ast_path_pattern.where_clause(), &path_pattern_scope,
                         query_resolution_info.get(),
                         /*allow_analytic=*/false,
                         /*allow_horizontal_aggregate=*/true));
  if (where_expr != nullptr) {
    filter_expr_conjuncts.push_back(std::move(where_expr));
  }

  if (!filter_expr_conjuncts.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeAndExpr(
        &ast_path_pattern, std::move(filter_expr_conjuncts), &filter_expr));
  }

  // Resolve any hints between paths if present.
  std::vector<std::unique_ptr<const ResolvedOption>> path_hints;
  if (ast_path_pattern.hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        resolver_->ResolveHintAndAppend(ast_path_pattern.hint(), &path_hints));
  }

  // Resolve the search prefix locally before merging namelists.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphPathSearchPrefix>
          resolved_search_prefix,
      ResolvePathSearchPrefix(ast_path_pattern.search_prefix(), input_scope));
  // If there is a prefix, collect local singleton variable names in
  // `multiply_decl_namelists` that cannot be multiply-declared. Skip if the
  // path IsQuantified() since it has no singleton names. Another reason to skip
  // is their group var names is not finalized yet (shown as singleton,
  // non-array here) which causes confusion.
  if (resolved_search_prefix != nullptr && !IsQuantified(&ast_path_pattern)) {
    GOOGLESQL_RET_CHECK_NE(
        resolved_search_prefix->type(),
        ResolvedGraphPathSearchPrefix::PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED);
    GOOGLESQL_RET_CHECK(out_multiply_decl_forbidden_names_to_err_location != nullptr);
    const IdString head_name_id = path_scan_column_list.front().name_id();
    const IdString tail_name_id = path_scan_column_list.back().name_id();
    // `head_name_id` and `tail_name_id` are both singleton names (not array
    // names from group vars), either from a child's synthetic head / tail or
    // are originally singletons.
    GOOGLESQL_RET_CHECK(path_scan_column_list.front().type()->IsGraphElement() &&
              path_scan_column_list.front().type()->AsGraphElement()->IsNode());
    GOOGLESQL_RET_CHECK(path_scan_column_list.back().type()->IsGraphElement() &&
              path_scan_column_list.back().type()->AsGraphElement()->IsNode());
    for (const NamedColumn& named_column :
         multiply_decl_namelists.singleton_name_list->columns()) {
      if (!named_column.column().type()->IsGraphElement()) {
        // Group var arrays are verified separately in `MakeEqualElementExpr` to
        // get a better error message.
        continue;
      }
      const IdString name_id = named_column.name();
      if (name_id != head_name_id && name_id != tail_name_id) {
        out_multiply_decl_forbidden_names_to_err_location->insert(
            {name_id, &ast_path_pattern});
      }
    }
  }

  std::unique_ptr<const ResolvedGraphPathPatternQuantifier> resolved_quantifier;
  // Merge the name lists of the resolved path along with the current working
  // graph name lists.
  std::vector<std::unique_ptr<const ResolvedGraphMakeArrayVariable>>
      new_group_variables;
  GOOGLESQL_ASSIGN_OR_RETURN(GraphTableNamedVariables output_graph_name_lists,
                   MergeGraphNameLists(std::move(input_graph_name_lists),
                                       std::move(multiply_decl_namelists),
                                       new_group_variables));
  if (IsQuantified(&ast_path_pattern)) {
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_quantifier,
                     ResolveGraphPathPatternQuantifier(
                         input_scope, ast_path_pattern.quantifier()));

    // If this is a quantified path, output a synthetic 'head' and 'tail'
    // column along with the group variables accessible along the path.
    // For more context: (broken link):bounded-quantified-path-pattern
    const Type* head_type = input_scans.front()->column_list().front().type();
    const Type* tail_type = nullptr;
    for (auto it = input_scans.back()->column_list().rbegin();
         it != input_scans.back()->column_list().rend(); ++it) {
      if (!it->type()->IsGraphPath()) {
        tail_type = it->type();
        break;
      }
    }
    GOOGLESQL_RET_CHECK(head_type->IsGraphElement());
    GOOGLESQL_RET_CHECK(head_type->AsGraphElement()->IsNode());
    GOOGLESQL_RET_CHECK(tail_type != nullptr);
    GOOGLESQL_RET_CHECK(tail_type->IsGraphElement());
    GOOGLESQL_RET_CHECK(tail_type->AsGraphElement()->IsNode());
    ResolvedColumn head_column(resolver_->AllocateColumnId(),
                               /*table_name=*/kPathScanName, kHeadColumnName,
                               head_type);
    ResolvedColumn tail_column(resolver_->AllocateColumnId(),
                               /*table_name=*/kPathScanName, kTailColumnName,
                               tail_type);
    // Clear the original column list for this path.
    path_scan_column_list.clear();
    path_scan_column_list.push_back(head_column);
    // Add the group variables to the column list.
    for (const auto& new_group_variable : new_group_variables) {
      path_scan_column_list.push_back(new_group_variable->array());
    }
    path_scan_column_list.push_back(tail_column);

    // Since head/tail are internal columns, they will not be accessed anywhere
    // within the query. Record column accesses for them here so that the
    // resolver does not prune them out.
    resolver_->RecordColumnAccess({head_column, tail_column});
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_path_mode,
                   ResolvePathMode(ast_path_pattern.path_mode()));
  GOOGLESQL_RET_CHECK_EQ(create_path_column, path_column.has_value());
  if (path_column.has_value()) {
    resolver_->RecordColumnAccess({*path_column});
    path_scan_column_list.push_back(*path_column);
  }
  ResolvedColumn tail = path_scan_column_list.back();
  for (auto it = path_scan_column_list.rbegin();
       it != path_scan_column_list.rend(); ++it) {
    if (!it->type()->IsGraphPath()) {
      tail = *it;
      break;
    }
  }

  std::unique_ptr<const ResolvedGraphPathCost> path_cost = nullptr;
  if (cost_supertype != nullptr) {
    path_cost = MakeResolvedGraphPathCost(cost_supertype);
  }
  auto builder = ResolvedGraphPathScanBuilder()
                     .set_column_list(path_scan_column_list)
                     .set_input_scan_list(std::move(input_scans))
                     .set_filter_expr(std::move(filter_expr))
                     .set_head(path_scan_column_list.front())
                     .set_tail(std::move(tail))
                     .set_path_hint_list(std::move(path_hints))
                     .set_quantifier(std::move(resolved_quantifier))
                     .set_group_variable_list(std::move(new_group_variables))
                     .set_path_mode(std::move(resolved_path_mode))
                     .set_search_prefix(std::move(resolved_search_prefix))
                     .set_path_cost(std::move(path_cost));
  if (path_column.has_value()) {
    builder.set_path(ResolvedColumnHolderBuilder().set_column(*path_column));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto output_scan, std::move(builder).Build());
  return {{.resolved_node = std::move(output_scan),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

std::vector<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::BuildColumnRefs(
    const ResolvedColumnList& columns,
    const absl::flat_hash_set<ResolvedColumn>& correlated_columns) {
  std::vector<std::unique_ptr<const ResolvedExpr>> column_refs;
  column_refs.reserve(columns.size());
  for (const auto& column : columns) {
    column_refs.push_back(resolver_->MakeColumnRef(
        column, /*is_correlated=*/correlated_columns.contains(column)));
  }
  return column_refs;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::MakeEqualElementExpr(
    const ASTNode& ast_location, const IdString variable,
    std::vector<std::unique_ptr<const ResolvedExpr>> exprs) const {
  GOOGLESQL_RET_CHECK_GE(exprs.size(), 2);
  if (!absl::c_all_of(exprs,
                      [](const std::unique_ptr<const ResolvedExpr>& expr) {
                        return expr->type()->IsGraphElement();
                      })) {
    return MakeSqlErrorAt(ast_location) << "`" << variable.ToStringView()
                                        << "` is not a graph element column "
                                           "and cannot be redeclared";
  }
  std::vector<std::unique_ptr<const ResolvedExpr>> equalities;
  equalities.reserve(exprs.size() - 1);
  for (int i = 1; i < exprs.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> lhs,
                     ResolvedASTDeepCopyVisitor::Copy(exprs[0].get()));
    std::unique_ptr<const ResolvedExpr> equal_expr;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeEqualityComparison(
        &ast_location, std::move(lhs), std::move(exprs[i]), &equal_expr));
    equalities.push_back(std::move(equal_expr));
  }
  std::unique_ptr<const ResolvedExpr> result;
  GOOGLESQL_RETURN_IF_ERROR(
      resolver_->MakeAndExpr(&ast_location, std::move(equalities), &result));
  return result;
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedExpr>>
GraphTableQueryResolver::ResolveMultiplyDeclaredVariables(
    const ASTNode& ast_location,
    const GraphTableNamedVariables& input_graph_name_lists,
    const IdStringHashMapCase<const ASTGraphPathPattern* const>*
        forbidden_singleton_names_to_err_location) {
  // Stores multiply_declared_variables names in a vector as test cases are
  // sensitive to order, uniqueness is guaranteed by variable_declarations.
  std::vector<IdString> multiply_declared_variables;
  IdStringHashMapCase<ResolvedColumnList> variable_declarations;
  auto singleton_name_list = std::make_shared<NameList>();

  // Confirm that there are no multiply declared group variables.
  GOOGLESQL_RET_CHECK_OK(CheckNoAmbiguousNameTarget(
      ast_location, *input_graph_name_lists.group_name_list));

  // Finds multiply-declared singleton variables.
  for (const NamedColumn& named_column :
       input_graph_name_lists.singleton_name_list->columns()) {
    const auto& [iter, inserted] = variable_declarations.emplace(
        named_column.name(), ResolvedColumnList{named_column.column()});
    if (inserted) {
      GOOGLESQL_RETURN_IF_ERROR(singleton_name_list->AddColumn(named_column.name(),
                                                     named_column.column(),
                                                     /*is_explicit=*/true));
    } else {
      if (iter->second.size() == 1) {
        multiply_declared_variables.push_back(named_column.name());
      }
      iter->second.push_back(named_column.column());
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(&ast_location, singleton_name_list,
                           std::move(input_graph_name_lists.group_name_list),
                           input_graph_name_lists.correlated_name_list));
  if (multiply_declared_variables.empty()) {
    return {{.resolved_node = nullptr,
             .graph_name_lists = std::move(output_graph_name_lists)}};
  }

  GOOGLESQL_RET_CHECK(input_graph_name_lists.correlated_name_list == nullptr);
  absl::flat_hash_set<ResolvedColumn> correlated_columns;

  // Builds SAME for each group of multiply-declared variables.
  std::vector<std::unique_ptr<const ResolvedExpr>> same_exprs;
  same_exprs.reserve(multiply_declared_variables.size());
  for (const auto& variable : multiply_declared_variables) {
    const ResolvedColumnList& column_list = variable_declarations[variable];
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> same_expr,
        MakeEqualElementExpr(ast_location, variable,
                             BuildColumnRefs(column_list, correlated_columns)));
    if (forbidden_singleton_names_to_err_location != nullptr &&
        (forbidden_singleton_names_to_err_location->find(variable) !=
         forbidden_singleton_names_to_err_location->end())) {
      // Throw this error when redeclaring singletons. If one of them is
      // a group var, will throw a better msg "group vars cannot be redeclared"
      // in `MakeEqualElementExpr` above.
      const ASTGraphPathPattern* declaring_location =
          forbidden_singleton_names_to_err_location->at(variable);
      return MakeSqlErrorAt(declaring_location)
             << "Variable `" << variable.ToStringView()
             << "` is inside a path under a selection prefix (`"
             << ASTGraphPathSearchPrefixEnums::PathSearchPrefixType_Name(
                    static_cast<int>(
                        declaring_location->search_prefix()->type()))
             << "`). It cannot be reused in other "
                "paths within the same MATCH statement, unless it is the "
                "endpoint node of such paths. Maybe use "
                "separated MATCHes for each path?";
    }
    same_exprs.push_back(std::move(same_expr));
  }
  std::unique_ptr<const ResolvedExpr> output_expr;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeAndExpr(&ast_location, std::move(same_exprs),
                                         &output_expr));

  return {{.resolved_node = std::move(output_expr),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedExpr>>
GraphTableQueryResolver::ResolveMultiplyDeclaredVariables(
    const ASTNode& ast_location,
    const GraphTableNamedVariables& input_name_list,
    const GraphTableNamedVariables& local_name_list) {
  GOOGLESQL_RET_CHECK(local_name_list.correlated_name_list == nullptr);

  std::vector<IdString> multiply_declared_variables;
  IdStringHashMapCase<ResolvedColumnList> variable_declarations;
  // For each multiply declared singleton variable in both left and right name
  // lists, we output the one that appeared in the right name list due to label
  // inference rules for GQL multiply declared variables in
  // (broken link):gql-linear-comp. We also want to keep the original relative order
  // of names so we scan the 2 lists in reverse order, and reverse the final
  // results back. Doing this in 2 pass since we cannot do random modifications
  // to a NameList.
  std::vector<const NamedColumn*> reversed_name_list;

  // Confirm that there are no ambiguous group variables.
  GOOGLESQL_RET_CHECK_OK(CheckNoAmbiguousNameTarget(ast_location,
                                          *input_name_list.group_name_list));
  GOOGLESQL_RET_CHECK_OK(CheckNoAmbiguousNameTarget(ast_location,
                                          *local_name_list.group_name_list));

  if (input_name_list.correlated_name_list != nullptr) {
    // For correlated names, do not add any names to the output. We only want to
    // find multiply declared names across the scope to add SAME exprs, so reset
    // those output list.
    std::vector<const NamedColumn*> dummy_list;

    MergeReversedMultiplyDeclaredNames(input_name_list.correlated_name_list,
                                       multiply_declared_variables,
                                       variable_declarations, dummy_list);
  }

  MergeReversedMultiplyDeclaredNames(local_name_list.singleton_name_list,
                                     multiply_declared_variables,
                                     variable_declarations, reversed_name_list);

  MergeReversedMultiplyDeclaredNames(input_name_list.singleton_name_list,
                                     multiply_declared_variables,
                                     variable_declarations, reversed_name_list);

  auto name_list = std::make_shared<NameList>();
  // Reverse the namelist to get the normal order. The final namelist also
  // prefers the right input list for where the multiply declared variable
  // appeared.
  for (auto it = reversed_name_list.rbegin(); it != reversed_name_list.rend();
       ++it) {
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn((*it)->name(), (*it)->column(),
                                         /*is_explicit=*/true));
  }

  // Merge the group name lists into one list.
  auto final_group_list = std::make_shared<NameList>();
  for (const NamedColumn& group_variable :
       input_name_list.group_name_list->columns()) {
    if (NameTarget unused_name_target;
        local_name_list.group_name_list->LookupName(group_variable.name(),
                                                    &unused_name_target)) {
      return MakeSqlErrorAt(ast_location)
             << "Group variable " << group_variable.name()
             << " is multiply declared";
    }
    if (NameTarget unused_name_target;
        local_name_list.singleton_name_list->LookupName(group_variable.name(),
                                                        &unused_name_target)) {
      return MakeSqlErrorAt(ast_location)
             << "Variable name: " << group_variable.name()
             << " cannot be used in both quantified and unquantified patterns";
    }
  }
  for (const NamedColumn& group_variable :
       local_name_list.group_name_list->columns()) {
    if (NameTarget unused_name_target;
        input_name_list.singleton_name_list->LookupName(group_variable.name(),
                                                        &unused_name_target)) {
      return MakeSqlErrorAt(ast_location)
             << "Variable name: " << group_variable.name()
             << " cannot be used in both quantified and unquantified patterns";
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(final_group_list->MergeFrom(*input_name_list.group_name_list,
                                              &ast_location));
  GOOGLESQL_RETURN_IF_ERROR(final_group_list->MergeFrom(*local_name_list.group_name_list,
                                              &ast_location));
  GOOGLESQL_RET_CHECK_OK(CheckNoAmbiguousNameTarget(ast_location, *final_group_list));

  GOOGLESQL_ASSIGN_OR_RETURN(auto output_graph_name_lists,
                   CreateGraphNameLists(&ast_location, name_list,
                                        std::move(final_group_list),
                                        input_name_list.correlated_name_list));
  if (multiply_declared_variables.empty()) {
    return {{.resolved_node = nullptr,
             .graph_name_lists = std::move(output_graph_name_lists)}};
  }

  // Builds SAME for each group of multiply-declared variables.
  std::vector<std::unique_ptr<const ResolvedExpr>> same_exprs;
  same_exprs.reserve(multiply_declared_variables.size());

  absl::flat_hash_set<ResolvedColumn> correlated_columns;
  if (input_name_list.correlated_name_list != nullptr) {
    for (const ResolvedColumn& column :
         input_name_list.correlated_name_list->GetResolvedColumns()) {
      correlated_columns.insert(column);
    }
  }

  for (const IdString& variable : multiply_declared_variables) {
    const ResolvedColumnList& column_list = variable_declarations[variable];
    std::optional<GraphElementType::ElementKind> kind;
    for (const ResolvedColumn& col : column_list) {
      if (col.type()->IsGraphElement()) {
        if (kind.has_value() &&
            *kind != col.type()->AsGraphElement()->element_kind()) {
          return MakeSqlErrorAt(ast_location)
                 << "Cannot multiply declare an element as both a node and an "
                    "edge pattern: `"
                 << variable.ToStringView() << "`";
        }
        kind = col.type()->AsGraphElement()->element_kind();
      }
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> same_expr,
        MakeEqualElementExpr(ast_location, variable,
                             BuildColumnRefs(column_list, correlated_columns)));
    same_exprs.push_back(std::move(same_expr));
  }
  ResolvedGraphWithNameList<const ResolvedExpr> result;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeAndExpr(&ast_location, std::move(same_exprs),
                                         &result.resolved_node));
  result.graph_name_lists = std::move(output_graph_name_lists);
  return result;
}

absl::Status GraphTableQueryResolver::ResolveGraphTableShape(
    const ASTSelectList* ast_select_list, const NameScope* input_scope,
    NameListPtr* output_name_list, ResolvedColumnList* output_column_list,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>* expr_list)
    const {
  GOOGLESQL_RET_CHECK(ast_select_list != nullptr);
  GOOGLESQL_RET_CHECK(input_scope != nullptr);
  GOOGLESQL_RET_CHECK(output_name_list != nullptr);
  GOOGLESQL_RET_CHECK(output_column_list != nullptr);
  GOOGLESQL_RET_CHECK(expr_list != nullptr);

  if (ast_select_list->columns().empty()) {
    return MakeSqlErrorAt(ast_select_list) << "COLUMNS cannot be empty";
  }

  const auto output_size = ast_select_list->columns().size();
  expr_list->reserve(output_size);
  output_column_list->reserve(output_size);

  auto graph_table_shape_name_list = std::make_shared<NameList>();
  for (const ASTSelectColumn* col : ast_select_list->columns()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveSelectColumn(col, input_scope,
                                        graph_table_shape_name_list.get(),
                                        output_column_list, expr_list));
  }
  for (int i = 0; i < output_column_list->size(); ++i) {
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT) &&
        TypeIsOrContainsGraphElement(output_column_list->at(i).type())) {
      return MakeSqlErrorAt(ast_select_list->columns()[i])
             << "Returning graph-typed column is not supported";
    }
  }

  // Report error if the final shape contains ambiguous column names.
  GOOGLESQL_RETURN_IF_ERROR(CheckNoAmbiguousNameTarget(*ast_select_list,
                                             *graph_table_shape_name_list));
  *output_name_list = std::move(graph_table_shape_name_list);

  return absl::OkStatus();
}

static constexpr absl::string_view kDynamicPropertyEqualityFunctionName =
    "$dynamic_property_equals";

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::ResolveGraphDynamicPropertyEquality(
    const ASTNode* error_location, const PropertyGraph* graph,
    const GraphElementType* element_type, absl::string_view property_name,
    const NameScope* input_scope,
    std::unique_ptr<const ResolvedExpr> resolved_element_expr,
    std::unique_ptr<const ResolvedExpr> resolved_property_value_expr) const {
  static constexpr char kDynamicPropertyEqualityExpr[] =
      "Dynamic property equality expr";
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      input_scope, kDynamicPropertyEqualityExpr);
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  resolved_arguments.push_back(std::move(resolved_element_expr));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> resolved_property_name_expr,
      ResolvedLiteralBuilder()
          .set_type(types::StringType())
          .set_value(Value::String(property_name))
          .set_has_explicit_type(true)
          .Build());
  resolved_arguments.push_back(std::move(resolved_property_name_expr));
  resolved_arguments.push_back(std::move(resolved_property_value_expr));

  std::unique_ptr<const ResolvedExpr> dynamic_property_equality;
  // An internal caller to resolve an internal function call instead of a
  // user-facing function call.
  FakeASTNode dummy_ast_node;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveFunctionCallWithResolvedArguments(
      error_location, {&dummy_ast_node, &dummy_ast_node, &dummy_ast_node},
      /*match_internal_signatures=*/true, kDynamicPropertyEqualityFunctionName,
      std::move(resolved_arguments),
      /*named_arguments=*/{}, expr_resolution_info.get(),
      &dynamic_property_equality));
  return dynamic_property_equality;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::ResolveGraphElementPropertySpecification(
    const ASTGraphPropertySpecification* ast_graph_property_specification,
    const NameScope* input_scope, const ResolvedColumn& element_column,
    bool supports_dynamic_properties) const {
  if (ast_graph_property_specification == nullptr) {
    return nullptr;
  }

  GOOGLESQL_RET_CHECK(element_column.type() != nullptr &&
            element_column.type()->IsGraphElement());
  const GraphElementType* element_type =
      element_column.type()->AsGraphElement();

  std::vector<std::unique_ptr<const ResolvedExpr>> eq_exprs;
  eq_exprs.reserve(
      ast_graph_property_specification->property_name_and_value().size());
  for (const auto* property_name_and_value :
       ast_graph_property_specification->property_name_and_value()) {
    absl::string_view property_name =
        property_name_and_value->property_name()->GetAsStringView();

    // Graph element column reference.
    std::unique_ptr<const ResolvedColumnRef> resolved_column_ref =
        resolver_->MakeColumnRef(element_column);

    // Property value expression.
    static constexpr char kPropertyValueExp[] = "Property value expr";
    auto expr_resolution_info =
        std::make_unique<ExprResolutionInfo>(input_scope, kPropertyValueExp);
    std::unique_ptr<const ResolvedExpr> value_expr;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(property_name_and_value->value(),
                                           expr_resolution_info.get(),
                                           &value_expr));

    if (element_type->is_dynamic() &&
        element_type->FindPropertyType(property_name) == nullptr) {
      GOOGLESQL_RET_CHECK(supports_dynamic_properties);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> resolved_eq_expr,
                       ResolveGraphDynamicPropertyEquality(
                           property_name_and_value->property_name(), graph_,
                           element_type, property_name, input_scope,
                           resolver_->CopyColumnRef(resolved_column_ref.get()),
                           std::move(value_expr)));
      eq_exprs.push_back(std::move(resolved_eq_expr));
      continue;
    }

    std::unique_ptr<const ResolvedGraphGetElementProperty> get_property_expr;
    GOOGLESQL_ASSIGN_OR_RETURN(
        get_property_expr,
        ResolveGraphGetElementProperty(
            property_name_and_value->property_name(), graph_, element_type,
            property_name, supports_dynamic_properties,
            resolver_->CopyColumnRef(resolved_column_ref.get())));

    std::unique_ptr<const ResolvedExpr> resolved_eq_expr;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeEqualityComparison(
        property_name_and_value, std::move(get_property_expr),
        std::move(value_expr), &resolved_eq_expr));
    eq_exprs.push_back(std::move(resolved_eq_expr));
  }

  std::unique_ptr<const ResolvedExpr> filter_expr;
  if (!eq_exprs.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_->MakeAndExpr(ast_graph_property_specification,
                                           std::move(eq_exprs), &filter_expr));
  }
  return filter_expr;
}

absl::Status GraphTableQueryResolver::ResolveSelectColumn(
    const ASTSelectColumn* ast_select_column, const NameScope* input_scope,
    NameList* output_name_list, ResolvedColumnList* output_column_list,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>* expr_list)
    const {
  static constexpr char kGraphTableShape[] = "graph table shape";

  if (ast_select_column->expression()->node_kind() == AST_DOT_STAR) {
    auto expr_resolution_info =
        std::make_unique<ExprResolutionInfo>(input_scope, kGraphTableShape);
    std::unique_ptr<const ResolvedExpr> resolved_column_expr;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(
        ast_select_column->expression()->GetAsOrDie<ASTDotStar>()->expr(),
        expr_resolution_info.get(), &resolved_column_expr));
    return AddPropertiesFromElement(
        ast_select_column, std::move(resolved_column_expr), output_name_list,
        output_column_list, expr_list);
  }

  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(resolver_);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> resolved_column_expr,
      ResolveExpr(ast_select_column->expression(), input_scope,
                  query_resolution_info.get(), /*allow_analytic=*/true,
                  /*allow_horizontal_aggregate=*/true, kGraphTableShape));
  GOOGLESQL_ASSIGN_OR_RETURN(const auto column_name, GetColumnName(ast_select_column));
  ResolvedColumn output_column(resolver_->AllocateColumnId(), kGraphTableName,
                               column_name, resolved_column_expr->type());
  GOOGLESQL_RETURN_IF_ERROR(output_name_list->AddColumn(column_name, output_column,
                                              /*is_explicit=*/true));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedComputedColumn> computed_column,
      ResolvedComputedColumnBuilder()
          .set_column(output_column)
          .set_expr(std::move(resolved_column_expr))
          .Build());
  expr_list->push_back(std::move(computed_column));
  output_column_list->push_back(std::move(output_column));

  return absl::OkStatus();
}

absl::Status GraphTableQueryResolver::AddPropertiesFromElement(
    const ASTNode* ast_location,
    std::unique_ptr<const ResolvedExpr> resolved_element, NameList* name_list,
    ResolvedColumnList* column_list,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>* expr_list)
    const {
  if (!resolved_element->type()->IsGraphElement()) {
    return MakeSqlErrorAt(ast_location)
           << "Expects GraphElement type. Found "
           << resolved_element->type()->DebugString();
  }

  if (resolved_element->type()->AsGraphElement()->is_dynamic()) {
    GOOGLESQL_RET_CHECK(resolver_->analyzer_options().language().LanguageFeatureEnabled(
        FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE));
    return MakeSqlErrorAt(ast_location)
           << "Dot-star is not supported for dynamic graph elements.";
  }

  // Add all properties exposed by graph element to output.
  const absl::Span<const PropertyType> property_types =
      resolved_element->type()->AsGraphElement()->property_types();
  for (const PropertyType& property_type : property_types) {
    const auto column_name_id = resolver_->MakeIdString(property_type.name);
    const Type* column_type = property_type.value_type;
    const ResolvedColumn output_column(resolver_->AllocateColumnId(),
                                       kGraphTableName, column_name_id,
                                       column_type);

    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(column_name_id, output_column,
                                         /*is_explicit=*/false));
    const GraphPropertyDeclaration* prop_dcl;
    GOOGLESQL_RETURN_IF_ERROR(
        graph_->FindPropertyDeclarationByName(property_type.name, prop_dcl));
    ResolvedASTDeepCopyVisitor deep_copy_visitor;
    GOOGLESQL_RETURN_IF_ERROR(resolved_element->Accept(&deep_copy_visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> resolved_element_copy,
                     deep_copy_visitor.ConsumeRootNode<ResolvedExpr>());
    auto builder = ResolvedGraphGetElementPropertyBuilder()
                       .set_type(column_type)
                       .set_expr(std::move(resolved_element_copy))
                       .set_property(prop_dcl);
    if (resolver_->analyzer_options().language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
      builder.set_property_name(ResolvedLiteralBuilder()
                                    .set_value(Value::String(prop_dcl->Name()))
                                    .set_type(types::StringType())
                                    .set_has_explicit_type(true));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphGetElementProperty>
                         get_element_property,
                     std::move(builder).Build());
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedComputedColumn> computed_column,
        ResolvedComputedColumnBuilder()
            .set_column(output_column)
            .set_expr(std::move(get_element_property))
            .Build());
    expr_list->push_back(std::move(computed_column));
    column_list->push_back(std::move(output_column));
  }

  return absl::OkStatus();
}

absl::Status GraphTableQueryResolver::ResolveGraphReference(
    const ASTPathExpression* graph_ref) {
  const absl::Status find_status =
      catalog_->FindPropertyGraph(graph_ref->ToIdentifierVector(), graph_,
                                  resolver_->analyzer_options().find_options());
  if (find_status.code() == absl::StatusCode::kNotFound) {
    return MakeGraphNotFoundSqlError(graph_ref);
  }
  GOOGLESQL_RETURN_IF_ERROR(find_status);
  GOOGLESQL_RET_CHECK(graph_ != nullptr);
  resolver_->RecordPropertyGraphRef(graph_);
  return absl::OkStatus();
}

absl::Status GraphTableQueryResolver::ResolveGraphLabelExpressions(
    const ASTGraphPattern* graph_pattern) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      label_expr_map_,
      GraphLabelExprResolver::ResolveNamedVariables(
          graph_pattern, [this](const ASTGraphLabelFilter* filter,
                                GraphElementTable::Kind element_kind) {
            return ResolveGraphLabelFilter(filter, element_kind);
          }));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>
GraphTableQueryResolver::ResolveGraphElementLabelExpr(
    const ASTIdentifier* ast_element_var, GraphElementTable::Kind element_kind,
    const ASTGraphLabelFilter* ast_label_filter) {
  // Resolves anonymous variables when needed as they are mutually distinct.
  if (ast_element_var == nullptr) {
    return ResolveGraphLabelFilter(ast_label_filter, element_kind);
  }
  const auto itr = label_expr_map_.find(ast_element_var->GetAsIdString());
  GOOGLESQL_RET_CHECK(itr != label_expr_map_.end());

  // Copies the resolved label expr as the label expr might be used multiple
  // times.
  return ResolvedASTDeepCopyVisitor::Copy<ResolvedGraphLabelExpr>(
      itr->second.get());
}

absl::Status GraphTableQueryResolver::MakeGraphNotFoundSqlError(
    const ASTPathExpression* graph_ref) const {
  std::string error_message;
  absl::StrAppend(&error_message, "Property graph not found: ",
                  graph_ref->ToIdentifierPathString());
  const std::string graph_suggestion =
      catalog_->SuggestPropertyGraph(graph_ref->ToIdentifierVector());
  if (!graph_suggestion.empty()) {
    absl::StrAppend(&error_message, "; Did you mean ", graph_suggestion, "?");
  }
  return MakeSqlErrorAt(graph_ref) << error_message;
}

absl::Status GraphTableQueryResolver::MaybeAddMissingNodePattern(
    const ASTGraphPathBase* left, const ASTGraphPathBase* right,
    std::vector<const ASTGraphPathBase*>& output_patterns) const {
  GOOGLESQL_RET_CHECK(left != nullptr);
  GOOGLESQL_RET_CHECK(right != nullptr);
  // Consecutive edges.
  if (left->Is<ASTGraphEdgePattern>() && right->Is<ASTGraphEdgePattern>()) {
    output_patterns.push_back(&empty_node_pattern_);
    return absl::OkStatus();
  }
  // Do nothing if either left or right is a subpath pattern.
  return absl::OkStatus();
}

absl::StatusOr<std::vector<const ASTGraphPathBase*>>
GraphTableQueryResolver::CanonicalizePathPattern(
    const ASTGraphPathPattern& ast_path_pattern) const {
  absl::Span<const ASTGraphPathBase* const> input_patterns =
      ast_path_pattern.input_pattern_list();
  GOOGLESQL_RET_CHECK(!input_patterns.empty());
  std::vector<const ASTGraphPathBase*> output;

  // Path head.
  if (input_patterns.front()->Is<ASTGraphEdgePattern>()) {
    output.push_back(&empty_node_pattern_);
  }

  output.push_back(input_patterns.front());
  for (const auto* pattern : input_patterns.subspan(1)) {
    GOOGLESQL_RETURN_IF_ERROR(MaybeAddMissingNodePattern(output.back(), pattern, output));
    output.push_back(pattern);
  }

  // Path tail.
  if (input_patterns.back()->Is<ASTGraphEdgePattern>()) {
    output.push_back(&empty_node_pattern_);
  }
  return output;
}

absl::Status GraphTableQueryResolver::GetQuantifierBoundExpr(
    const ASTExpression* ast_quantifier_bound, const NameScope* input_scope,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) const {
  static constexpr char kGraphQuantifier[] = "graph quantifier";
  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(input_scope, kGraphQuantifier);
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(
      ast_quantifier_bound, expr_resolution_info.get(), resolved_expr_out));
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
      kGraphQuantifier, ast_quantifier_bound, resolved_expr_out));
  return absl::OkStatus();
}

// Build GraphRefScan from input namelist
absl::StatusOr<std::unique_ptr<const ResolvedScan>>
GraphTableQueryResolver::BuildGraphRefScan(const NameListPtr& input_name_list) {
  GOOGLESQL_RET_CHECK(input_name_list != nullptr);
  ResolvedGraphRefScanBuilder builder;
  for (auto& named_column : input_name_list->columns()) {
    builder.add_column_list(named_column.column());
  }
  return std::move(builder).Build();
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlOperator(
    const ASTGqlOperator* gql_op, const NameScope* external_scope,
    std::vector<std::unique_ptr<const ResolvedScan>>& current_scan_list,
    ResolvedGraphWithNameList<const ResolvedScan> inputs,
    bool is_first_statement_in_graph_query) {
  auto& input_scan = inputs.resolved_node;
  GOOGLESQL_RET_CHECK(input_scan != nullptr);

  auto local_scope = std::make_unique<NameScope>(
      external_scope, inputs.graph_name_lists.singleton_name_list);

  switch (gql_op->node_kind()) {
    case AST_GQL_OPERATOR_LIST: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto op_list_result,
          ResolveGqlLinearQuery(*gql_op->GetAsOrDie<ASTGqlOperatorList>(),
                                external_scope, std::move(inputs),
                                is_first_statement_in_graph_query));
      // ResolveGqlLinearQuery() returns a result with ResolvedGraphLinearScan.
      // Upcast it to a ResolvedScan before returning it.
      return {{.resolved_node = std::move(op_list_result.resolved_node),
               .graph_name_lists = std::move(op_list_result.graph_name_lists)}};
    }
    case AST_GQL_MATCH: {
      return ResolveGqlMatch(*gql_op->GetAsOrDie<ASTGqlMatch>(), external_scope,
                             std::move(inputs));
    }
    case AST_GQL_LET: {
      return ResolveGqlLet(*gql_op->GetAsOrDie<ASTGqlLet>(), local_scope.get(),
                           std::move(inputs));
    }
    case AST_GQL_FILTER: {
      return ResolveGqlFilter(*gql_op->GetAsOrDie<ASTGqlFilter>(),
                              local_scope.get(), current_scan_list,
                              std::move(inputs));
    }
    case AST_GQL_ORDER_BY_AND_PAGE: {
      return ResolveGqlOrderByAndPage(
          *gql_op->GetAsOrDie<ASTGqlOrderByAndPage>(), external_scope,
          std::move(inputs));
    }
    case AST_GQL_WITH: {
      return ResolveGqlWith(*gql_op->GetAsOrDie<ASTGqlWith>(),
                            local_scope.get(), std::move(inputs));
    }
    case AST_GQL_FOR: {
      return ResolveGqlFor(*gql_op->GetAsOrDie<ASTGqlFor>(), local_scope.get(),
                           std::move(inputs));
    }
    case AST_GQL_INLINE_SUBQUERY_CALL: {
      return ResolveGqlInlineSubqueryCall(
          *gql_op->GetAsOrDie<ASTGqlInlineSubqueryCall>(), local_scope.get(),
          std::move(inputs), is_first_statement_in_graph_query);
    }
    case AST_GQL_NAMED_CALL: {
      return ResolveGqlNamedCall(*gql_op->GetAsOrDie<ASTGqlNamedCall>(),
                                 local_scope.get(), std::move(inputs),
                                 is_first_statement_in_graph_query);
    }
    case AST_GQL_RETURN: {
      return ResolveGqlReturn(*gql_op->GetAsOrDie<ASTGqlReturn>(),
                              local_scope.get(), std::move(inputs));
    }
    case AST_GQL_SET_OPERATION: {
      return ResolveGqlSetOperation(*gql_op->GetAsOrDie<ASTGqlSetOperation>(),
                                    external_scope, std::move(inputs),
                                    is_first_statement_in_graph_query);
    }
    case AST_GQL_SAMPLE: {
      return ResolveGqlSample(*gql_op->GetAsOrDie<ASTGqlSample>(),
                              external_scope, std::move(inputs));
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected query statement " << gql_op->node_kind();
  }
}

// Returns a set of column names produced by the singleton name list.
static absl::StatusOr<ColumnNameSet> ToColumnNameSet(
    GraphTableNamedVariables graph_name_lists) {
  ColumnNameSet column_names;
  for (const NamedColumn& named_column :
       graph_name_lists.singleton_name_list->columns()) {
    GOOGLESQL_RET_CHECK(column_names.insert(named_column.name()).second);
  }
  return column_names;
}

// Returns a string of column names produced by graph singleton and group name
// lists for user-visible errors.
static std::string GraphNameListsToString(GraphTableNamedVariables name_lists) {
  return absl::StrCat(
      "[",
      absl::StrJoin(name_lists.singleton_name_list->columns(), ", ",
                    [](std::string* out, const NamedColumn& named_column) {
                      absl::StrAppend(out,
                                      ToIdentifierLiteral(named_column.name()));
                    }),
      name_lists.singleton_name_list->num_columns() > 0 &&
              name_lists.group_name_list->num_columns() > 0
          ? ","
          : "",
      absl::StrJoin(name_lists.group_name_list->columns(), ", ",
                    [](std::string* out, const NamedColumn& named_column) {
                      absl::StrAppend(out,
                                      ToIdentifierLiteral(named_column.name()));
                    }),
      "]");
}

static absl::StatusOr<GraphTableNamedVariables> BuildFinalNameLists(
    const ResolvedColumnList& final_column_list, const ASTNode* ast_location) {
  auto name_list = std::make_shared<NameList>();
  for (int i = 0; i < final_column_list.size(); ++i) {
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumnMaybeValueTable(
        final_column_list[i].name_id(), final_column_list[i],
        /*is_explicit=*/true, ast_location, /*is_value_table_column=*/false));
  }
  return {{.ast_node = ast_location,
           .singleton_name_list = std::move(name_list),
           .group_name_list = std::make_shared<NameList>()}};
}

absl::Status
GraphTableQueryResolver::GraphSetOperationResolver::ValidateGqlSetOperation(
    const ASTGqlSetOperation& set_op) {
  GOOGLESQL_RET_CHECK_GE(set_op.inputs().size(), 2)
      << "Gql set operation must have at least 2 inputs";
  GOOGLESQL_RET_CHECK_NE(set_op.metadata(), nullptr);
  GOOGLESQL_RET_CHECK(!set_op.metadata()->set_operation_metadata_list().empty())
      << "Gql set operation must have metadata";

  GOOGLESQL_ASSIGN_OR_RETURN(
      ResolvedSetOperationScan::SetOperationType first_op_type,
      GetSetOperationType(set_op.metadata()->set_operation_metadata_list(0)));
  for (const auto* metadata :
       set_op.metadata()->set_operation_metadata_list()) {
    GOOGLESQL_RET_CHECK_EQ(metadata->hint(), nullptr)
        << "Gql set operation does not support hint";
    GOOGLESQL_RET_CHECK_EQ(metadata->column_match_mode(), nullptr)
        << "Gql set operation does not support column match mode";
    if (!graph_resolver_->resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_SET_OPERATION_PROPAGATION_MODE)) {
      if (metadata->column_propagation_mode() != nullptr) {
        return MakeSqlErrorAtLocalNode(metadata)
               << "Gql set operation does not support column propagation mode";
      }
    }
    GOOGLESQL_RET_CHECK_EQ(metadata->corresponding_by_column_list(), nullptr)
        << "Gql set operation does not support corresponding by";
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedSetOperationScan::SetOperationType op_type,
                     GetSetOperationType(metadata));
    if (op_type != first_op_type) {
      return MakeSqlErrorAtLocalNode(metadata)
             << "Gql set operation must have the same set operation type";
    }
  }
  if (graph_resolver_->resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_SET_OPERATION_PROPAGATION_MODE)) {
    const auto first_column_propagation_mode = GetGraphSetColumnPropagationMode(
        set_op.metadata()->set_operation_metadata_list(0));
    for (const auto* metadata :
         set_op.metadata()->set_operation_metadata_list()) {
      if (GetGraphSetColumnPropagationMode(metadata) !=
          first_column_propagation_mode) {
        return MakeSqlErrorAtLocalNode(metadata)
               << "Gql set operation must have the same column propagation "
                  "mode";
      }
    }
  }

  for (const auto* set_op_input : set_op.inputs()) {
    GOOGLESQL_RET_CHECK(set_op_input->Is<ASTGqlOperatorList>())
        << "Gql set operation must have ASTGqlOperatorList as input";
    for (const auto* op :
         set_op_input->GetAsOrDie<ASTGqlOperatorList>()->operators()) {
      GOOGLESQL_RET_CHECK(!op->Is<ASTGqlOperatorList>());
    }
  }
  return absl::OkStatus();
}

// Computes the grouping name list for projections like WITH/RETURN. It uses the
// ResolvedColumn identity to track which of the original output columns in
// `new_singleton_name_list` originated from the old names tracked in
// `old_group_name_list`.
// Note that the same input group name may appear multiple times, e.g.
//   RETURN group_name AS g1, group_name AS g2, .. etc.
// It may also disappear and be dropped completely.
static absl::StatusOr<std::shared_ptr<NameList>> ComputeNewGroupNameList(
    const NameListPtr& old_group_name_list,
    const NameListPtr& new_singleton_name_list) {
  auto new_group_name_list = std::make_shared<NameList>();
  absl::flat_hash_set<ResolvedColumn> resolved_columns_for_group_names;
  for (const NamedColumn& column : old_group_name_list->columns()) {
    resolved_columns_for_group_names.insert(column.column());
  }

  for (const NamedColumn& column : new_singleton_name_list->columns()) {
    if (resolved_columns_for_group_names.contains(column.column())) {
      GOOGLESQL_RET_CHECK_OK(new_group_name_list->AddColumn(
          column.name(), column.column(), column.is_explicit()));
    }
  }
  return new_group_name_list;
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::BuildRefForInputScan(
    const ResolvedGraphWithNameList<const ResolvedScan>& input) {
  std::unique_ptr<const ResolvedScan> copied_scan;
  if (input.resolved_node->Is<ResolvedSingleRowScan>()) {
    GOOGLESQL_ASSIGN_OR_RETURN(copied_scan,
                     googlesql::ResolvedSingleRowScanBuilder().Build());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        copied_scan,
        BuildGraphRefScan(input.graph_name_lists.singleton_name_list));
  }
  return ResolvedGraphWithNameList<const ResolvedScan>{
      .resolved_node = std::move(copied_scan),
      .graph_name_lists = input.graph_name_lists};
}

absl::StatusOr<std::vector<std::vector<InputArgumentType>>>
GraphTableQueryResolver::GraphSetOperationResolver::BuildColumnTypeLists(
    absl::Span<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
        resolved_inputs,
    int final_column_num, const IndexMapper& index_mapper) const {
  std::vector<std::vector<InputArgumentType>> column_type_lists;
  column_type_lists.resize(final_column_num);

  for (int query_idx = 0; query_idx < resolved_inputs.size(); ++query_idx) {
    const ResolvedScan* resolved_scan =
        resolved_inputs[query_idx].resolved_node->scan_list().back().get();
    const NameList& curr_name_list =
        *resolved_inputs[query_idx].graph_name_lists.singleton_name_list;
    for (int i = 0; i < final_column_num; ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::optional<int> output_column_index,
                       index_mapper.GetOutputColumnIndex(query_idx, i));
      if (!graph_resolver_->resolver_->language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_SET_OPERATION_PROPAGATION_MODE)) {
        GOOGLESQL_RET_CHECK(output_column_index.has_value());
      }
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

absl::StatusOr<std::unique_ptr<const ResolvedGraphLinearScan>>
GraphTableQueryResolver::GraphSetOperationResolver::
    MaybeTypeCastAndWrapWithNullPaddedProjectScan(
        const ASTSelect* ast_location,
        ResolvedGraphWithNameList<const ResolvedGraphLinearScan> resolved_input,
        int query_idx, const ResolvedColumnList& final_column_list,
        const IndexMapper& index_mapper) {
  ResolvedGraphLinearScanBuilder linear_scan_builder =
      ToBuilder(std::move(resolved_input.resolved_node));
  std::vector<std::unique_ptr<const ResolvedScan>> scan_list =
      linear_scan_builder.release_scan_list();
  ResolvedColumnList matched_final_column_list;
  ResolvedColumnList scan_column_list;
  ResolvedColumnList full_scan_column_list_with_null_columns(
      final_column_list.size());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> null_columns;
  std::vector<int> non_null_column_indices;
  for (int i = 0; i < final_column_list.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<int> output_column_index,
                     index_mapper.GetOutputColumnIndex(query_idx, i));
    if (output_column_index.has_value()) {
      matched_final_column_list.push_back(final_column_list[i]);
      scan_column_list.push_back(
          resolved_input.graph_name_lists.singleton_name_list
              ->column(*output_column_index)
              .column());
      non_null_column_indices.push_back(i);
      continue;
    }

    // NULL columns are padded for those columns in the final column list but
    // not present in the current query.
    const ResolvedColumn& column = final_column_list[i];
    std::unique_ptr<const ResolvedComputedColumn> null_column =
        MakeResolvedComputedColumn(
            ResolvedColumn(
                graph_resolver_->resolver_->AllocateColumnId(),
                graph_resolver_->resolver_->MakeIdString(absl::StrCat(
                    kGraphSetOperation.ToStringView(), query_idx + 1, "_null")),
                graph_resolver_->resolver_->MakeIdString(
                    column.name_id().ToStringView()),
                column.type()),
            googlesql::MakeResolvedLiteral(Value::Null(column.type())));
    full_scan_column_list_with_null_columns[i] = null_column->column();
    graph_resolver_->resolver_->RecordColumnAccess(null_column->column());
    null_columns.push_back(std::move(null_column));
  }

  std::unique_ptr<const ResolvedScan>* return_scan = &scan_list.back();
  GOOGLESQL_RETURN_IF_ERROR(graph_resolver_->resolver_->CreateWrapperScanWithCasts(
      ast_location, matched_final_column_list,
      graph_resolver_->resolver_->MakeIdString(absl::StrCat(
          kGraphSetOperation.ToStringView(), query_idx + 1, "_cast")),
      return_scan, &scan_column_list));

  // Because the column names in `scan_column_list` might be changed after the
  // cast, we need to update the `full_scan_column_list_with_null_columns` to
  // use the new column names.
  for (int i = 0; i < non_null_column_indices.size(); ++i) {
    full_scan_column_list_with_null_columns[non_null_column_indices[i]] =
        scan_column_list[i];
  }

  // Wrap the last scan (ProjectScan) in GraphLinearScan with another
  // ProjectScan to pad NULL columns if needed.
  if (!null_columns.empty()) {
    std::unique_ptr<const ResolvedScan> last_scan = std::move(scan_list.back());
    scan_list.pop_back();
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedProjectScan> project_scan,
        ResolvedProjectScanBuilder()
            .set_column_list(full_scan_column_list_with_null_columns)
            .set_expr_list(std::move(null_columns))
            .set_input_scan(std::move(last_scan))
            .Build());
    scan_list.push_back(std::move(project_scan));
  }

  return std::move(linear_scan_builder)
      .set_column_list(full_scan_column_list_with_null_columns)
      .set_scan_list(std::move(scan_list))
      .Build();
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedSetOperationItem>>>
GraphTableQueryResolver::GraphSetOperationResolver::BuildSetOperationItems(
    absl::Span<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
        resolved_inputs,
    const ResolvedColumnList& final_column_list,
    const IndexMapper& index_mapper) {
  std::vector<std::unique_ptr<const ResolvedSetOperationItem>>
      resolved_set_op_items;
  resolved_set_op_items.reserve(resolved_inputs.size());
  for (int query_idx = 0; query_idx < resolved_inputs.size(); ++query_idx) {
    // Wrap the last scan (ProjectScan) in GraphLinearScan with another
    // ProjectScan, if casting to supertype is needed (and the new ProjectScan
    // will become the last scan of the GraphLinearScan).
    //
    // Then wrap the last scan (ProjectScan) in GraphLinearScan with another,
    // if padding NULL columns is needed (and the new ProjectScan will become
    // the last scan of the GraphLinearScan).
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphLinearScan> linear_scan,
                     MaybeTypeCastAndWrapWithNullPaddedProjectScan(
                         node_.inputs(query_idx)
                             ->GetAsOrDie<ASTGqlOperatorList>()
                             ->operators()
                             .back()
                             ->GetAsOrDie<ASTGqlReturn>()
                             ->select(),
                         std::move(resolved_inputs[query_idx]), query_idx,
                         final_column_list, index_mapper));

    ResolvedColumnList column_list = linear_scan->column_list();
    std::unique_ptr<ResolvedSetOperationItem> item =
        MakeResolvedSetOperationItem(std::move(linear_scan), column_list);
    resolved_set_op_items.push_back(std::move(item));
  }
  return resolved_set_op_items;
}

absl::StatusOr<std::vector<IdString>>
GraphTableQueryResolver::GraphSetOperationResolver::GetFinalColumnNames(
    absl::Span<const ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
        resolved_inputs,
    absl::Span<IndexedColumnNames> indexed_column_names_list,
    ResolvedSetOperationScan::SetOperationColumnPropagationMode
        column_propagation_mode,
    const ASTNode* error_location) {
  // Please notice that the column_propagation_mode is always STRICT when
  // FEATURE_SQL_GRAPH_SET_OPERATION_PROPAGATION_MODE is disabled.
  switch (column_propagation_mode) {
    case ResolvedSetOperationScan::LEFT: {
      return CalculateFinalColumnNamesForLeftCorresponding(
          indexed_column_names_list,
          /*no_common_columns_error=*/[&](int query_idx) {
            return MakeSqlErrorAtLocalNode(error_location)
                   << absl::StrCat("Query ", query_idx + 1)
                   << " of the GQL set operation with LEFT mode does not"
                      " share any common columns with the first input query";
          });
      break;
    }
    case ResolvedSetOperationScan::STRICT: {
      return CalculateFinalColumnNamesForStrictCorresponding(
          indexed_column_names_list,
          /*input_column_mismatch_error=*/[&, resolved_inputs](
                                              int query_idx,
                                              absl::Span<const IdString>,
                                              absl::Span<const IdString>) {
            // When the feature is disabled, users are not exposed with the
            // internal details of column propagation mode.
            absl::string_view specify_propagation_mode_err_msg =
                graph_resolver_->resolver_->language().LanguageFeatureEnabled(
                    FEATURE_SQL_GRAPH_SET_OPERATION_PROPAGATION_MODE)
                    ? " when column propagation mode is default (STRICT)"
                    : "";
            return MakeSqlErrorAtLocalNode(error_location)
                   << "GQL set operation requires all input queries to have "
                      "identical column names"
                   << specify_propagation_mode_err_msg
                   << ", but the first query has "
                   << GraphNameListsToString(
                          resolved_inputs.front().graph_name_lists)
                   << " and query " << (query_idx + 1) << " has "
                   << GraphNameListsToString(
                          resolved_inputs[query_idx].graph_name_lists);
          });
    }
    case ResolvedSetOperationScan::INNER: {
      return CalculateFinalColumnNamesForInnerCorresponding(
          indexed_column_names_list,
          /*column_intersection_empty_error=*/[&]() {
            return MakeSqlErrorAtLocalNode(error_location)
                   << "GQL set operation using INNER requires that the "
                      "intersection of columns from input queries is non-empty";
          });
    }
    case ResolvedSetOperationScan::FULL: {
      return CalculateFinalColumnNamesForFullCorresponding(
          indexed_column_names_list);
    }
  }
}

GraphTableQueryResolver::GraphSetOperationResolver::GraphSetOperationResolver(
    const ASTGqlSetOperation& node, GraphTableQueryResolver* resolver)
    : SetOperationResolverBase(resolver->resolver_->analyzer_options(),
                               resolver->resolver_->coercer_,
                               *resolver->resolver_->column_factory_),
      node_(node),
      graph_resolver_(resolver) {}

// static
std::vector<SetOperationResolverBase::IndexedColumnNames>
GraphTableQueryResolver::GraphSetOperationResolver::ToIndexedColumnNamesList(
    absl::Span<const ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
        resolved_inputs) {
  std::vector<IndexedColumnNames> indexed_column_names_list;
  for (int query_idx = 0; query_idx < resolved_inputs.size(); ++query_idx) {
    IndexedColumnNames indexed_column_names;
    indexed_column_names.query_idx = query_idx;
    for (const auto& col :
         resolved_inputs[query_idx]
             .graph_name_lists.singleton_name_list->columns()) {
      indexed_column_names.column_names.push_back(col.name());
    }
    indexed_column_names_list.push_back(std::move(indexed_column_names));
  }
  return indexed_column_names_list;
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::GraphSetOperationResolver::Resolve(
    const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs,
    bool is_first_statement_in_graph_query) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateGqlSetOperation(node_));
  const auto* set_op_metadata =
      node_.metadata()->set_operation_metadata_list(0);
  GOOGLESQL_ASSIGN_OR_RETURN(ResolvedSetOperationScan::SetOperationType op_type,
                   GetSetOperationType(set_op_metadata));
  const auto column_propagation_mode =
      GetGraphSetColumnPropagationMode(set_op_metadata);

  std::vector<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
      resolved_set_op_inputs;
  for (const auto* set_op_input_node : node_.inputs()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        ResolvedGraphWithNameList<const ResolvedScan> set_op_input_scan,
        graph_resolver_->BuildRefForInputScan(inputs));

    // Return one linear scan for each set operation input.
    GOOGLESQL_ASSIGN_OR_RETURN(
        ResolvedGraphWithNameList<const ResolvedGraphLinearScan> resolved_input,
        graph_resolver_->ResolveGqlLinearQuery(
            *set_op_input_node->GetAsOrDie<ASTGqlOperatorList>(),
            external_scope, std::move(set_op_input_scan),
            is_first_statement_in_graph_query));
    resolved_set_op_inputs.push_back(std::move(resolved_input));
  }
  std::vector<IndexedColumnNames> input_query_column_names =
      ToIndexedColumnNamesList(absl::MakeSpan(resolved_set_op_inputs));

  const auto* error_location =
      set_op_metadata->column_propagation_mode() == nullptr
          ? static_cast<const ASTNode*>(set_op_metadata->op_type())
          : static_cast<const ASTNode*>(
                set_op_metadata->column_propagation_mode());
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<IdString> final_column_names,
      GetFinalColumnNames(resolved_set_op_inputs,
                          absl::MakeSpan(input_query_column_names),
                          column_propagation_mode, error_location));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<IndexMapper> index_mapper,
      BuildIndexMapping(input_query_column_names, final_column_names));

  // Decide the final column list. The names and order of the columns will
  // respect those of the first query. The column type will be supertype of the
  // columns with the same name.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::vector<InputArgumentType>> column_type_lists,
      BuildColumnTypeLists(absl::MakeSpan(resolved_set_op_inputs),
                           static_cast<int>(final_column_names.size()),
                           *index_mapper));

  // Verify column types satisfy the following requirements:
  //   * Columns with the same name have the same supertype
  //   * Set operation other than UNION ALL must have all column types groupable
  //   * Set operation INTERSECT or EXCEPT must have all column types comparable
  // Note that, GraphLinearScan guarantees that output columns must have name.
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const Type*> super_types,
                   GetSuperTypesOfSetOperation(
                       column_type_lists, set_op_metadata,
                       op_type, /*column_identifier_in_error_string=*/
                       [&final_column_names](int col_idx) -> std::string {
                         return final_column_names[col_idx].ToString();
                       }));
  GOOGLESQL_ASSIGN_OR_RETURN(
      ResolvedColumnList final_column_list,
      BuildFinalColumnList(
          final_column_names, super_types, kGraphSetOperation,
          /*record_column_access=*/[&](const ResolvedColumn& col) -> void {
            graph_resolver_->resolver_->RecordColumnAccess(col);
          }));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_set_op_items,
      BuildSetOperationItems(absl::MakeSpan(resolved_set_op_inputs),
                             final_column_list, *index_mapper));

  // We set CORRESPONDING here to indicate that columns are matched by name.
  // However, this would have the side effect of requiring components that
  // rely on that field to not associate it with user input, e.g. SQLBuilder.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedSetOperationScan> set_op_scan,
      ResolvedSetOperationScanBuilder()
          .set_column_list(final_column_list)
          .set_op_type(op_type)
          .set_column_match_mode(ResolvedSetOperationScan::CORRESPONDING)
          .set_column_propagation_mode(column_propagation_mode)
          .set_input_item_list(std::move(resolved_set_op_items))
          .Build());

  GOOGLESQL_ASSIGN_OR_RETURN(auto final_name_lists,
                   BuildFinalNameLists(final_column_list, &node_));
  final_name_lists.correlated_name_list =
      inputs.graph_name_lists.correlated_name_list;

  return ResolvedGraphWithNameList<const ResolvedScan>(
      {.resolved_node = std::move(set_op_scan),
       .graph_name_lists = std::move(final_name_lists)});
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlSetOperation(
    const ASTGqlSetOperation& set_op, const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs,
    bool is_first_statement_in_graph_query) {
  GraphSetOperationResolver set_op_resolver(set_op, this);
  return set_op_resolver.Resolve(external_scope, std::move(inputs),
                                 is_first_statement_in_graph_query);
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlSample(
    const ASTGqlSample& sample_op, const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input) {
  GOOGLESQL_RET_CHECK(sample_op.sample() != nullptr);

  std::shared_ptr<const NameList> output_name_list =
      input.graph_name_lists.singleton_name_list->Copy();
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveTablesampleClause(
      sample_op.sample(), &output_name_list, &input.resolved_node));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(&sample_op, output_name_list->Copy(),
                           std::move(input.graph_name_lists.group_name_list),
                           input.graph_name_lists.correlated_name_list));
  return {{.resolved_node = std::move(input.resolved_node),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

static bool IsLinearQuery(const ASTGqlOperator* node) {
  return node->Is<ASTGqlOperatorList>();
}

static bool IsCompositeQuery(const ASTGqlOperator* node) {
  return IsLinearQuery(node) || node->Is<ASTGqlSetOperation>();
}

absl::StatusOr<GraphTableQueryResolver::ResolvedGraphWithNameList<
    const ResolvedGraphLinearScan>>
GraphTableQueryResolver::ResolveGqlLinearQueryList(
    const ASTGqlOperatorList& gql_ops_list, const NameScope* external_scope,
    GraphTableNamedVariables input_graph_name_lists,
    bool is_first_statement_in_graph_query) {
  // Check that the first 2 levels are linear scans.
  GOOGLESQL_RET_CHECK(!gql_ops_list.operators().empty());
  for (const auto* composite_op : gql_ops_list.operators()) {
    GOOGLESQL_RET_CHECK(IsCompositeQuery(composite_op))
        << "GQL top level linear scan must contain ASTGqlOperatorList or "
           "ASTGqlSetOperation as children";
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto input_scan,
                   googlesql::ResolvedSingleRowScanBuilder().Build());

  GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                   ResolveGqlOperatorList(
                       gql_ops_list.operators(), external_scope,
                       {.resolved_node = std::move(input_scan),
                        .graph_name_lists = std::move(input_graph_name_lists)},
                       is_first_statement_in_graph_query));

  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT)) {
    const ASTGqlOperator* last_op = gql_ops_list.operators().back();
    if (IsLinearQuery(last_op)) {
      GOOGLESQL_RETURN_IF_ERROR(CheckReturnNoGraphTypedCols(
          result.resolved_node->column_list(),
          last_op->GetAsOrDie<ASTGqlOperatorList>()->operators().back()));
    } else {
      // If the last scan is a composite query, validate that the column list of
      // every resolved linear scan contained satisfies the "no graph element
      // column" constraint.
      for (const auto* linear_op :
           last_op->GetAsOrDie<ASTGqlSetOperation>()->inputs()) {
        GOOGLESQL_RETURN_IF_ERROR(CheckReturnNoGraphTypedCols(
            result.resolved_node->column_list(),
            linear_op->GetAsOrDie<ASTGqlOperatorList>()->operators().back()));
      }
    }
  }
  return result;
}

absl::Status GraphTableQueryResolver::CheckGqlLinearQuery(
    absl::Span<const ASTGqlOperator* const> primitive_ops) const {
  const auto size = primitive_ops.size();
  GOOGLESQL_RET_CHECK_GT(size, 0);
  // All ops other than the last one should be one of MATCH,
  // LET, FILTER, WITH, FOR, or ORDER BY and OFFSET/LIMIT.
  for (int i = 0; i < size - 1; ++i) {
    GOOGLESQL_RET_CHECK(primitive_ops[i]->Is<ASTGqlMatch>() ||
              primitive_ops[i]->Is<ASTGqlLet>() ||
              primitive_ops[i]->Is<ASTGqlOrderByAndPage>() ||
              primitive_ops[i]->Is<ASTGqlFilter>() ||
              primitive_ops[i]->Is<ASTGqlWith>() ||
              primitive_ops[i]->Is<ASTGqlSample>() ||
              primitive_ops[i]->Is<ASTGqlFor>() ||
              primitive_ops[i]->Is<ASTGqlNamedCall>() ||
              primitive_ops[i]->Is<ASTGqlInlineSubqueryCall>())
        << "Unexpected op: " << primitive_ops[i]->DebugString();
  }
  // The last op should be a RETURN.
  GOOGLESQL_RET_CHECK(primitive_ops[size - 1]->Is<ASTGqlReturn>());

  auto is_only_order_by = [](const ASTGqlOrderByAndPage* op) {
    return op != nullptr && op->order_by() != nullptr && op->page() == nullptr;
  };
  auto is_only_limit = [](const ASTGqlOrderByAndPage* op) {
    return op != nullptr && op->order_by() == nullptr &&
           op->page() != nullptr && op->page()->limit() != nullptr &&
           op->page()->offset() == nullptr;
  };
  auto is_only_offset = [](const ASTGqlOrderByAndPage* op) {
    return op != nullptr && op->order_by() == nullptr &&
           op->page() != nullptr && op->page()->offset() != nullptr &&
           op->page()->limit() == nullptr;
  };

  // We consider "ORDER BY LIMIT OFFSET" op sequences to be an error.
  for (int i = 2; i < primitive_ops.size() - 1; ++i) {
    auto* op0 = primitive_ops[i - 2]->GetAsOrNull<ASTGqlOrderByAndPage>();
    auto* op1 = primitive_ops[i - 1]->GetAsOrNull<ASTGqlOrderByAndPage>();
    auto* op2 = primitive_ops[i]->GetAsOrNull<ASTGqlOrderByAndPage>();

    // If op0 is "ORDER BY" and op1 is "LIMIT" and op2 is "OFFSET" then mark
    // the sequence as error.
    if (is_only_order_by(op0) && is_only_limit(op1) && is_only_offset(op2)) {
      return MakeSqlErrorAt(op2) << "ORDER BY LIMIT OFFSET is not allowed; "
                                    "please use ORDER BY OFFSET LIMIT";
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<GraphTableQueryResolver::ResolvedGraphWithNameList<
    const ResolvedGraphLinearScan>>
GraphTableQueryResolver::ResolveGqlLinearQuery(
    const ASTGqlOperatorList& gql_ops_list, const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs,
    bool is_first_statement_in_graph_query) {
  const absl::Span<const ASTGqlOperator* const> primitive_ops =
      gql_ops_list.operators();
  GOOGLESQL_RETURN_IF_ERROR(CheckGqlLinearQuery(primitive_ops));

  return ResolveGqlOperatorList(primitive_ops, external_scope,
                                std::move(inputs),
                                is_first_statement_in_graph_query);
}

absl::StatusOr<GraphTableQueryResolver::ResolvedGraphWithNameList<
    const ResolvedGraphLinearScan>>
GraphTableQueryResolver::ResolveGqlOperatorList(
    absl::Span<const ASTGqlOperator* const> gql_ops,
    const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs,
    bool is_first_statement_in_graph_query) {
  GOOGLESQL_RET_CHECK(!gql_ops.empty())
      << "GQL linear scan must contain at least one child ASTGqlOperator";

  // Invariant: Incoming correlated name list must be the same across all
  // operators.
  std::shared_ptr<const NameList> correlated_name_list =
      inputs.graph_name_lists.correlated_name_list;

  // Output scan list
  std::vector<std::unique_ptr<const ResolvedScan>> scan_list;
  std::unique_ptr<const ResolvedScan> ref_scan;
  auto op_inputs = std::move(inputs);

  for (int i = 0; i < gql_ops.size(); ++i) {
    const ASTGqlOperator* gql_op = gql_ops[i];
    GOOGLESQL_ASSIGN_OR_RETURN(
        op_inputs, ResolveGqlOperator(
                       gql_op, external_scope, scan_list, std::move(op_inputs),
                       is_first_statement_in_graph_query && i == 0));
    // Capture the resulting output scan in our scan list.
    scan_list.push_back(std::move(op_inputs.resolved_node));
    // Build a ref scan to the tabular result as the next input scan to GQL ops.
    GOOGLESQL_ASSIGN_OR_RETURN(
        ref_scan,
        BuildGraphRefScan(op_inputs.graph_name_lists.singleton_name_list));
    op_inputs.resolved_node = std::move(ref_scan);
    // Measures cannot be propagated through GQL operators for now.
    GOOGLESQL_RETURN_IF_ERROR(EnsureNoMeasuresInNameList(
        op_inputs.graph_name_lists.singleton_name_list, gql_op, "graph queries",
        resolver_->product_mode()));
    // Ensure we didn't mistakenly drop the correlated list somewhere.
    GOOGLESQL_RET_CHECK(op_inputs.graph_name_lists.correlated_name_list ==
              correlated_name_list);
  }
  auto linear_builder = ResolvedGraphLinearScanBuilder().set_column_list(
      scan_list.back()->column_list());
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto linear_scan,
      std::move(linear_builder).set_scan_list(std::move(scan_list)).Build());
  return {{.resolved_node = std::move(linear_scan),
           .graph_name_lists = std::move(op_inputs.graph_name_lists)}};
}

// If we have an OPTIONAL MATCH the new working set is the existing
// 'original_input_name_list' and any new variables in 'graph_name_lists',
// keeping the old declaration of multiply-declared names.
static absl::Status RestoreMultiplyDeclaredNames(
    GraphTableNamedVariables& graph_name_lists,
    const NameList& original_input_name_list) {
  auto optional_name_list = std::make_shared<NameList>();
  IdStringHashSetCase working_name_columns;
  for (const auto& named_column : original_input_name_list.columns()) {
    GOOGLESQL_RET_CHECK(working_name_columns.insert(named_column.name()).second);
    GOOGLESQL_RETURN_IF_ERROR(optional_name_list->AddColumn(named_column.name(),
                                                  named_column.column(),
                                                  /*is_explicit=*/true));
  }

  for (const auto& named_column :
       graph_name_lists.singleton_name_list->columns()) {
    if (!working_name_columns.contains(named_column.name())) {
      GOOGLESQL_RETURN_IF_ERROR(optional_name_list->AddColumn(named_column.name(),
                                                    named_column.column(),
                                                    /*is_explicit=*/true));
    }
  }
  graph_name_lists.singleton_name_list = std::move(optional_name_list);
  return absl::OkStatus();
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlMatch(
    const ASTGqlMatch& match_op, const NameScope* input_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  auto& input_scan = inputs.resolved_node;
  auto& input_graph_name_lists = inputs.graph_name_lists;
  GOOGLESQL_RET_CHECK(input_scan != nullptr);
  GOOGLESQL_RET_CHECK(match_op.graph_pattern() != nullptr);
  const ASTGraphPattern& ast_graph_pattern = *match_op.graph_pattern();

  GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                   ResolveGraphPattern(ast_graph_pattern, input_scope,
                                       input_graph_name_lists));

  if (match_op.optional()) {
    GOOGLESQL_RETURN_IF_ERROR(RestoreMultiplyDeclaredNames(
        result.graph_name_lists, *input_graph_name_lists.singleton_name_list));
  }

  ResolvedColumnList graph_pattern_scan_column_list =
      result.resolved_node->column_list();

  ResolvedColumnList joined_columns = input_scan->column_list();
  absl::c_move(graph_pattern_scan_column_list,
               std::back_inserter(joined_columns));

  // Resolve match hints if they're present.
  std::vector<std::unique_ptr<const ResolvedOption>> match_hints;
  if (match_op.hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        resolver_->ResolveHintAndAppend(match_op.hint(), &match_hints));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto graph_scan, ToBuilder(std::move(result.resolved_node))
                                        .set_column_list(joined_columns)
                                        .set_hint_list(std::move(match_hints))
                                        .set_input_scan(std::move(input_scan))
                                        .set_optional(match_op.optional())
                                        .Build());
  return {{.resolved_node = std::move(graph_scan),
           .graph_name_lists = std::move(result.graph_name_lists)}};
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
GraphTableQueryResolver::ResolveExpr(const ASTExpression* expr,
                                     const NameScope* local_scope,
                                     QueryResolutionInfo* query_resolution_info,
                                     bool allow_analytic,
                                     bool allow_horizontal_aggregate,
                                     const char* const clause_name) const {
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info, local_scope,
      ExprResolutionInfoOptions{
          .allows_aggregation = false,
          .allows_analytic = allow_analytic,
          .allows_horizontal_aggregation =
              allow_horizontal_aggregate &&
              resolver_->language().LanguageFeatureEnabled(
                  FEATURE_SQL_GRAPH_ADVANCED_QUERY),
          .clause_name = clause_name});
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(
      resolver_->ResolveExpr(expr, expr_resolution_info.get(), &resolved_expr));
  return resolved_expr;
}

static absl::Status CheckNameNotInDisallowedList(
    IdString name, std::shared_ptr<NameList> disallowed_name_list,
    const ASTNode* location) {
  if (disallowed_name_list == nullptr) {
    return absl::OkStatus();
  }
  if (NameTarget name_target;
      disallowed_name_list->LookupName(name, &name_target)) {
    return MakeSqlErrorAt(location)
           << "Variable name: " << ToIdentifierLiteral(name)
           << " already exists";
  }
  return absl::OkStatus();
}

// Takes the input scan and extends it with one column each for the
// variable definitions in `let_op`s `variable_definition_list()`.
// Produces a ResolvedProjectScan with these additional columns, and
// amends the working name list with names from the variable definitions.
// Names added to the name list do not conflict with names already in the
// list. If any such names are encountered, returns an error.
absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlLet(
    const ASTGqlLet& let_op, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  auto& input_scan = inputs.resolved_node;
  auto& input_graph_name_lists = inputs.graph_name_lists;

  const auto definitions =
      let_op.variable_definition_list()->variable_definitions();

  // Initialize a ResolvedColumnList with the input scan's column list.
  // New ResolvedColumns will be added to this list as the variable
  // definition list is processed. This is necessary because the generated
  // ResolvedProjectScan extends the input scan with new columns.
  // As such, it is different from a ResolvedProjectScan that selects
  // from the input scan.
  ResolvedColumnList let_definition_column_list(input_scan->column_list());
  let_definition_column_list.reserve(input_scan->column_list_size() +
                                     definitions.size());

  // Like the column list, the output name list starts of being the
  // same as the initial working name list.
  auto post_let_name_list = input_graph_name_lists.singleton_name_list->Copy();

  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.reserve(definitions.size());

  // Reuse the node-source annotation string for LET as the clause identifier
  // string since it very conveniently already available here.
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(resolver_);
  for (const auto& definition : definitions) {
    const auto column_name = definition->identifier()->GetAsIdString();

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> resolved_column_expr,
                     ResolveExpr(definition->expression(), local_scope,
                                 query_resolution_info.get(),
                                 /*allow_analytic=*/true,
                                 /*allow_horizontal_aggregate=*/true, "LET"));

    ResolvedColumn new_column(resolver_->AllocateColumnId(), kGraphTableName,
                              column_name, resolved_column_expr->type());
    let_definition_column_list.push_back(new_column);

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedComputedColumn> computed_column,
        ResolvedComputedColumnBuilder()
            .set_column(new_column)
            .set_expr(std::move(resolved_column_expr))
            .Build());

    GOOGLESQL_RETURN_IF_ERROR(CheckNameNotInDisallowedList(
        column_name, input_graph_name_lists.correlated_name_list,
        definition->identifier()));

    GOOGLESQL_RETURN_IF_ERROR(post_let_name_list->AddColumn(column_name, new_column,
                                                  /*is_explicit=*/true));

    expr_list.push_back(std::move(computed_column));
  }

  GOOGLESQL_RET_CHECK(!query_resolution_info->HasAggregation());
  if (query_resolution_info->HasAnalytic()) {
    GOOGLESQL_RETURN_IF_ERROR(
        query_resolution_info->analytic_resolver()->CreateAnalyticScan(
            query_resolution_info.get(), &input_scan));
  }

  // As of now only singleton variables may be assigned via LET.
  // Create an output graph namelist by preserving the input group variables.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(&let_op, std::move(post_let_name_list),
                           std::move(input_graph_name_lists.group_name_list),
                           input_graph_name_lists.correlated_name_list));

  // Report error if the final shape contains ambiguous column names.
  GOOGLESQL_RETURN_IF_ERROR(CheckNoAmbiguousNameTarget(
      /*location=*/*let_op.variable_definition_list(),
      *output_graph_name_lists.singleton_name_list));

  GOOGLESQL_ASSIGN_OR_RETURN(auto project_scan,
                   ResolvedProjectScanBuilder()
                       .set_column_list(let_definition_column_list)
                       .set_expr_list(std::move(expr_list))
                       .set_input_scan(std::move(input_scan))
                       .Build());
  return {{.resolved_node = std::move(project_scan),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlFilter(
    const ASTGqlFilter& filter_op, const NameScope* local_scope,
    std::vector<std::unique_ptr<const ResolvedScan>>& current_scan_list,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  // ResolveWhereClauseAndCreateScan except horizontal aggregations are allowed.
  GOOGLESQL_RET_CHECK(filter_op.condition() != nullptr);
  auto query_resolution_info = std::make_unique<QueryResolutionInfo>(resolver_);

  // Window functions are allowed in FILTER.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> resolved_where,
                   ResolveWhereClause(filter_op.condition(), local_scope,
                                      query_resolution_info.get(),
                                      /*allow_analytic=*/true,
                                      /*allow_horizontal_aggregate=*/true));

  GOOGLESQL_RET_CHECK(!query_resolution_info->HasAggregation());

  ResolvedColumnList tmp_column_list = inputs.resolved_node->column_list();
  if (query_resolution_info->HasAnalytic()) {
    GOOGLESQL_RETURN_IF_ERROR(
        query_resolution_info->analytic_resolver()->CreateAnalyticScan(
            query_resolution_info.get(), &inputs.resolved_node));
    // `inputs.resolved_node` now contains the new AnalyticScan, which added
    // the analytic columns referenced in the current filter expression.
    tmp_column_list = inputs.resolved_node->column_list();
    current_scan_list.push_back(std::move(inputs.resolved_node));
    GOOGLESQL_ASSIGN_OR_RETURN(
        inputs.resolved_node,
        ResolvedGraphRefScanBuilder().set_column_list(tmp_column_list).Build());
  }

  return {{.resolved_node = MakeResolvedFilterScan(
               std::move(tmp_column_list), std::move(inputs.resolved_node),
               std::move(resolved_where)),
           .graph_name_lists = std::move(inputs.graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlOrderByAndPage(
    const ASTGqlOrderByAndPage& order_by_page_op,
    const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto order_by_result,
                   ResolveGqlOrderByClause(order_by_page_op.order_by(),
                                           external_scope, std::move(inputs)));

  // Pass the result of the OrderBy to the Page clause
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto page_result,
      ResolveGqlPageClauses(order_by_page_op.page(), external_scope,
                            std::move(order_by_result)));

  return {{.resolved_node = std::move(page_result.resolved_node),
           .graph_name_lists = std::move(page_result.graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlFor(
    const ASTGqlFor& for_op, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  GOOGLESQL_RET_CHECK(for_op.identifier() != nullptr);
  GOOGLESQL_RET_CHECK(for_op.expression() != nullptr);

  // Initialize the output column list and NameList with the input
  // column list and NameList.
  ResolvedColumnList output_column_list(inputs.resolved_node->column_list());
  auto post_for_name_list = inputs.graph_name_lists.singleton_name_list->Copy();

  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_array_expr_list;
  std::unique_ptr<const ResolvedExpr> array_expr;
  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(local_scope, "FOR");
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(
      for_op.expression(), expr_resolution_info.get(), &array_expr));
  GOOGLESQL_RET_CHECK(array_expr != nullptr);
  const Type* array_expr_type = array_expr->type();
  GOOGLESQL_RET_CHECK(array_expr_type != nullptr);
  if (!array_expr_type->IsArray()) {
    return MakeSqlErrorAt(for_op.expression())
           << "Expression in graph FOR operator must be of type ARRAY ";
  }

  const AnnotationMap* element_annotation = nullptr;
  if (array_expr->type_annotation_map() != nullptr) {
    element_annotation =
        array_expr->type_annotation_map()->AsStructMap()->field(0);
  }
  const ResolvedColumn array_element_column(
      resolver_->AllocateColumnId(), kGraphTableName,
      /*name=*/for_op.identifier()->GetAsIdString(),
      AnnotatedType(array_expr_type->AsArray()->element_type(),
                    element_annotation));
  std::vector<ResolvedColumn> resolved_element_column_list;
  resolved_element_column_list.push_back(array_element_column);
  output_column_list.push_back(array_element_column);
  GOOGLESQL_RETURN_IF_ERROR(post_for_name_list->AddColumn(
      for_op.identifier()->GetAsIdString(), array_element_column,
      /*is_explicit=*/true));
  GOOGLESQL_RETURN_IF_ERROR(CheckNameNotInDisallowedList(
      for_op.identifier()->GetAsIdString(),
      inputs.graph_name_lists.correlated_name_list, for_op.identifier()));
  resolved_array_expr_list.push_back(std::move(array_expr));

  // Resolve WITH OFFSET if present
  std::unique_ptr<ResolvedColumnHolder> array_position_column;
  if (for_op.with_offset() != nullptr) {
    const ASTAlias* with_offset_alias = for_op.with_offset()->alias();
    const IdString offset_alias =
        (with_offset_alias == nullptr ? kOffsetAlias
                                      : with_offset_alias->GetAsIdString());
    const ResolvedColumn column(
        resolver_->AllocateColumnId(), kGraphTableName,
        /*name=*/offset_alias,
        AnnotatedType(resolver_->type_factory_->get_int64(),
                      /*annotation_map=*/nullptr));
    array_position_column = MakeResolvedColumnHolder(column);
    output_column_list.push_back(array_position_column->column());
    GOOGLESQL_RETURN_IF_ERROR(post_for_name_list->AddColumn(
        offset_alias, array_position_column->column(),
        /*is_explicit=*/true));

    GOOGLESQL_RETURN_IF_ERROR(CheckNameNotInDisallowedList(
        offset_alias, inputs.graph_name_lists.correlated_name_list,
        with_offset_alias != nullptr
            ? with_offset_alias->identifier()->GetAsOrDie<ASTNode>()
            : for_op.with_offset()));
  }

  // Create an output graph namelist by preserving the input group variables.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(&for_op, std::move(post_for_name_list),
                           std::move(inputs.graph_name_lists.group_name_list),
                           inputs.graph_name_lists.correlated_name_list));

  // Report error if the final shape contains ambiguous column names.
  GOOGLESQL_RETURN_IF_ERROR(CheckNoAmbiguousNameTarget(
      /*location=*/for_op, *output_graph_name_lists.singleton_name_list));

  return {{.resolved_node = MakeResolvedArrayScan(
               output_column_list, std::move(inputs.resolved_node),
               std::move(resolved_array_expr_list),
               std::move(resolved_element_column_list),
               std::move(array_position_column),
               /*join_expr=*/nullptr, /*is_outer=*/false,
               /*array_zip_mode=*/nullptr),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlInlineSubqueryCall(
    const ASTGqlInlineSubqueryCall& call_op, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input,
    bool is_first_statement_in_graph_query) {
  if (!resolver_->language().LanguageFeatureEnabled(FEATURE_SQL_GRAPH_CALL)) {
    return MakeSqlErrorAt(call_op) << "CALL is not supported";
  }

  GOOGLESQL_RET_CHECK(call_op.subquery() != nullptr);
  GOOGLESQL_RET_CHECK(call_op.subquery()->query_expr() != nullptr);
  GOOGLESQL_RET_CHECK(call_op.subquery()->query_expr()->Is<ASTGqlQuery>());
  const ASTGqlQuery* gql_query =
      call_op.subquery()->query_expr()->GetAsOrDie<ASTGqlQuery>();
  GOOGLESQL_RET_CHECK(gql_query->graph_table() != nullptr);

  const ASTGraphTableQuery* query = gql_query->graph_table();

  if (query->graph_reference() != nullptr) {
    return MakeSqlErrorAt(query->graph_reference())
           << "CALL subquery cannot specify a graph reference";
  }

  if (call_op.is_partitioning()) {
    return MakeSqlErrorAt(query) << "CALL PER on a subquery is not supported";
  }

  GOOGLESQL_RET_CHECK(query->graph_op() != nullptr);
  GOOGLESQL_RET_CHECK(query->graph_op()->Is<ASTGqlOperatorList>());

  const ASTGqlOperatorList* op_list =
      query->graph_op()->GetAsOrDie<ASTGqlOperatorList>();

  auto [input_scan, input_name_list] = std::move(input);
  if (call_op.optional() && input_scan->column_list().empty()) {
    GOOGLESQL_RET_CHECK_EQ(input_name_list.singleton_name_list->num_columns(), 0);
    GOOGLESQL_RET_CHECK_EQ(input_name_list.group_name_list->num_columns(), 0);
    GOOGLESQL_RET_CHECK(input_name_list.correlated_name_list == nullptr ||
              input_name_list.correlated_name_list->num_columns() == 0);
    return MakeSqlErrorAt(call_op) << "OPTIONAL CALL is not supported on an "
                                      "input with no columns defined";
  }

  // Initialize the output column list and NameList with the input
  // column list and NameList, since the LHS won't change.
  ResolvedColumnList output_column_list =
      input_name_list.singleton_name_list->GetResolvedColumns();

  // Isolate the scope into another layer in order to capture correlated
  // references, which we will need to populate the lateral columns.
  CorrelatedColumnsSet correlated_columns;

  // If the name capture list is present, restrict the visible names to only
  // those listed in it.
  if (call_op.name_capture_list() == nullptr) {
    return MakeSqlErrorAt(call_op.subquery())
           << "CALL with a subquery requires a name capture list";
  }

  // Used mostly for error tracking and placing disallowed targets.
  auto flattened_name_list = std::make_shared<NameList>();

  for (const ASTIdentifier* name :
       call_op.name_capture_list()->identifier_list()) {
    bool is_already_correlated = false;
    NameTarget target;
    if (!input_name_list.singleton_name_list->LookupName(name->GetAsIdString(),
                                                         &target)) {
      if (input_name_list.correlated_name_list == nullptr) {
        // This is a top-level CALL query. Its capture list can make outer
        // column references.
        CorrelatedColumnsSetList correlated_column_sets;
        if (!local_scope->LookupName(name->GetAsIdString(), &target,
                                     &correlated_column_sets)) {
          return MakeSqlErrorAt(name)
                 << "Unrecognized name " << name->GetAsStringView();
        }
        GOOGLESQL_RET_CHECK(!correlated_column_sets.empty())
            << "The name must be correlated, since it's not in the input "
               "name list.";
      } else if (!input_name_list.correlated_name_list->LookupName(
                     name->GetAsIdString(), &target)) {
        return MakeSqlErrorAt(name)
               << "Unrecognized name " << name->GetAsStringView();
      }
      is_already_correlated = true;
    }

    if (target.IsAmbiguous()) {
      return MakeSqlErrorAt(name)
             << "Ambiguous name: "
             << ToIdentifierLiteral(name->GetAsStringView());
    }
    if (!target.IsColumn()) {
      return MakeSqlErrorAt(name)
             << "Not a column: "
             << ToIdentifierLiteral(name->GetAsStringView());
    }

    GOOGLESQL_RETURN_IF_ERROR(flattened_name_list->AddColumn(
        name->GetAsIdString(), target.column(), target.IsExplicit()));

    // Columns for the multiply-declared variable SAME exprs are not resolved
    // from some parse AST. Rather, they are constructed directly, so we need
    // to explicitly call RecordColumnAccess() to ensure they don't get pruned.
    correlated_columns.insert({target.column(), is_already_correlated});
    resolver_->RecordColumnAccess(target.column());
  }

  // Do not pass the previous scope. We captured all the correlated names from
  // the list, and that's all that should be visible to the subquery.
  auto filtered_local_scope = std::make_unique<NameScope>(
      /*previous_scope=*/nullptr, flattened_name_list);
  CorrelatedColumnsSet dummy_correlated_columns;
  auto lateral_scope = std::make_unique<NameScope>(filtered_local_scope.get(),
                                                   &dummy_correlated_columns);

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_subquery_with_names,
      ResolveGqlOperatorList(
          op_list->operators(), lateral_scope.get(),
          {.resolved_node = MakeResolvedSingleRowScan(),
           .graph_name_lists =
               {.ast_node = query,
                .singleton_name_list = std::make_shared<NameList>(),
                .group_name_list = std::make_shared<NameList>(),
                // Mark the names as correlated for this subquery.
                .correlated_name_list = flattened_name_list}},
          is_first_statement_in_graph_query));

  // Ensure we didn't mistakenly drop the correlated list somewhere.
  GOOGLESQL_RET_CHECK(
      resolved_subquery_with_names.graph_name_lists.correlated_name_list ==
      flattened_name_list);

  for (const ResolvedColumn& column :
       resolved_subquery_with_names.resolved_node->column_list()) {
    output_column_list.push_back(column);
  }

  // Collect the `parameter_list`.
  std::vector<std::unique_ptr<const ResolvedColumnRef>> lateral_columns;
  resolver_->FetchCorrelatedSubqueryParameters(correlated_columns,
                                               &lateral_columns);

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_name_lists,
      MergeGraphNameLists(
          std::move(input_name_list),
          std::move(resolved_subquery_with_names.graph_name_lists)));
  // Report error if the final shape contains ambiguous column names.
  GOOGLESQL_RETURN_IF_ERROR(CheckNoAmbiguousNameTarget(
      *call_op.subquery(), *output_name_lists.singleton_name_list));

  ResolvedColumnList subquery_column_list =
      resolved_subquery_with_names.resolved_node->column_list();

  const PropertyGraph* current_graph =
      resolver_->GetActivePropertyGraphOrNull();
  GOOGLESQL_RET_CHECK(current_graph != nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto lateral_join,
      ResolvedGraphCallScanBuilder()
          .set_column_list(std::move(output_column_list))
          .set_optional(call_op.optional())
          .set_parameter_list(std::move(lateral_columns))
          .set_input_scan(std::move(input_scan))
          .set_subquery(ResolvedGraphTableScanBuilder()
                            .set_column_list(std::move(subquery_column_list))
                            .set_property_graph(current_graph)
                            .set_input_scan(std::move(
                                resolved_subquery_with_names.resolved_node)))
          .Build());
  return {{.resolved_node = std::move(lateral_join),
           .graph_name_lists = std::move(output_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlNamedCall(
    const ASTGqlNamedCall& call_op, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input,
    bool is_first_statement_in_graph_query) {
  if (!resolver_->language().LanguageFeatureEnabled(FEATURE_SQL_GRAPH_CALL)) {
    return MakeSqlErrorAt(call_op) << "CALL is not supported";
  }

  auto& [input_scan, input_graph_name_lists] = input;
  GOOGLESQL_RET_CHECK(call_op.tvf_call() != nullptr);

  if (call_op.optional() && input_scan->column_list().empty()) {
    GOOGLESQL_RET_CHECK_EQ(input_graph_name_lists.singleton_name_list->num_columns(), 0);
    GOOGLESQL_RET_CHECK_EQ(input_graph_name_lists.group_name_list->num_columns(), 0);
    GOOGLESQL_RET_CHECK(input_graph_name_lists.correlated_name_list == nullptr ||
              input_graph_name_lists.correlated_name_list->num_columns() == 0);
    return MakeSqlErrorAt(call_op) << "OPTIONAL CALL is not supported on an "
                                      "input with no columns defined";
  }

  std::unique_ptr<const ResolvedScan> resolved_scan;
  std::shared_ptr<const NameList> tvf_output_name_list;

  // This is used for the CALL tvf() case, without PER(), in the same way we
  // handle CALL () {subquery}.
  std::vector<std::unique_ptr<const ResolvedColumnRef>> lateral_columns;

  if (call_op.is_partitioning()) {
    GOOGLESQL_RET_CHECK(call_op.name_capture_list() != nullptr)
        << "The name list must be available if PER is specified";
    if (!call_op.name_capture_list()->identifier_list().empty()) {
      // We only support PER() with an empty list at the moment, and without
      // OPTIONAL. In the future, this will be a FOR EACH PARTITION BY.
      return MakeSqlErrorAt(call_op.name_capture_list())
             << "CALL PER() TVF must have an empty list of arguments";
    }
    if (call_op.optional()) {
      // We cannot currently support this case.
      return MakeSqlErrorAt(&call_op)
             << "OPTIONAL on CALL PER() with an empty list is not supported";
    }

    // The implicit input table must have at least one column.
    if (input_scan->column_list().empty()) {
      return MakeSqlErrorAt(call_op)
             << "The input table to a CALL PER() TVF must have at least one "
                "column";
    }

    GOOGLESQL_RET_CHECK(local_scope->previous_scope() != nullptr)
        << "The external scope cannot be null";
    // Disallow names in the current scope, to ensure the TVF does not silently
    // take an outer name, and instead give a good error message, for example,
    // within a correlated subquery:
    //  FROM (.. AS x) ..
    //  |> EXTEND (SELECT ... FROM GRAPH_TABLE(...
    //     LET x = 1;        # Shadows an outer column `x`
    //     CALL PER() tvf(x) # Cannot reference the local `x`, but can reference
    //                       # the outer `x`.
    //     ...
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto restricted_scope,
        CreateNameScopeWithDisallowList(
            /*external_scope=*/nullptr,  // Hide all previous scopes.
            {input_graph_name_lists.singleton_name_list.get(),
             input_graph_name_lists.group_name_list.get()},
            [](const IdString& name) {
              return absl::StrCat(
                  "Name ", ToSingleQuotedStringLiteral(name.ToStringView()),
                  ", defined in the current scope, cannot be used as a TVF "
                  "argument in a CALL PER() with an empty list");
            }));

    // The input scan becomes the first table-typed TVF argument.
    ResolvedTVFArg table_arg;
    table_arg.SetScan(std::move(input_scan),
                      input_graph_name_lists.singleton_name_list,
                      /*is_pipe_input_table=*/true);

    // CALL PER() with an empty list means the whole table is a single
    // partition. This special degenerate case of partitioned CALL is simply a
    // TVF call with the input table as the implicit table argument.
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveTVF(
        call_op.tvf_call(), restricted_scope.get(), &table_arg, &resolved_scan,
        &tvf_output_name_list));

    for (const ResolvedColumn& column : resolved_scan->column_list()) {
      // Prevent the TVF columns from being pruned, to avoid the case where they
      // all get pruned.
      resolver_->RecordColumnAccess(column);
    }
  } else {
    CorrelatedColumnsSet correlated_columns;
    auto lateral_scope =
        std::make_unique<NameScope>(local_scope, &correlated_columns);
    // Unlike CALL PER(), there is no implicit table argument here but there
    // could be an implicit graph argument if this is the first statement. See
    // http://shortn/_0Uc6eBN0Lu for more details.
    auto* graph = resolver_->GetActivePropertyGraphOrNull();
    GOOGLESQL_RET_CHECK(graph != nullptr);
    ResolvedTVFArg graph_arg;
    graph_arg.SetGraph(graph);
    const absl::Status tvf_resolve_status = resolver_->ResolveTVF(
        call_op.tvf_call(), lateral_scope.get(),
        /*pipe_input_arg=*/
        is_first_statement_in_graph_query ? &graph_arg : nullptr,
        &resolved_scan, &tvf_output_name_list);
    if (!tvf_resolve_status.ok()) {
      if (absl::IsInvalidArgument(tvf_resolve_status) &&
          !is_first_statement_in_graph_query &&
          resolver_
              ->ResolveTVF(call_op.tvf_call(), lateral_scope.get(), &graph_arg,
                           &resolved_scan, &tvf_output_name_list)
              .ok()) {
        // We would have succeeded if the graph was provided so we give a better
        // error message.
        return googlesql_base::StatusBuilder(tvf_resolve_status)
               << "CALL is not the first statement in the graph query so "
               << call_op.tvf_call()->name()->ToIdentifierPathString()
               << " cannot access the implicit graph argument";
      }
      return tvf_resolve_status;
    }
    ResolvedColumnList output_column_list = input.resolved_node->column_list();
    for (const ResolvedColumn& column : resolved_scan->column_list()) {
      // Prevent the RHS columns from being pruned, to avoid the case where they
      // all get pruned.
      resolver_->RecordColumnAccess(column);
      output_column_list.push_back(column);
    }

    // Collect the `parameter_list`.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> lateral_columns;
    resolver_->FetchCorrelatedSubqueryParameters(correlated_columns,
                                                 &lateral_columns);

    GOOGLESQL_ASSIGN_OR_RETURN(resolved_scan,
                     ResolvedGraphCallScanBuilder()
                         .set_column_list(std::move(output_column_list))
                         .set_input_scan(std::move(input_scan))
                         .set_optional(call_op.optional())
                         .set_parameter_list(std::move(lateral_columns))
                         .set_subquery(std::move(resolved_scan))
                         .Build());
  }

  // Apply the YIELD clause.
  if (call_op.yield_clause() != nullptr) {
    GOOGLESQL_RET_CHECK(!call_op.yield_clause()->yield_items().empty());
    auto updated_name_list = std::make_shared<NameList>();
    for (const ASTExpressionWithOptAlias* yield_item :
         call_op.yield_clause()->yield_items()) {
      GOOGLESQL_RET_CHECK(yield_item->expression() != nullptr);
      GOOGLESQL_RET_CHECK(yield_item->expression()->Is<ASTIdentifier>());

      const auto* identifier =
          yield_item->expression()->GetAsOrNull<ASTIdentifier>();
      IdString name = identifier->GetAsIdString();
      NameTarget target;
      if (!tvf_output_name_list->LookupName(name, &target)) {
        return MakeSqlErrorAt(identifier)
               << "Name " << ToIdentifierLiteral(name)
               << " not found in the TVF output";
      }

      GOOGLESQL_RET_CHECK(target.IsColumn());

      const ASTNode* location = identifier;
      if (yield_item->optional_alias() != nullptr) {
        name = yield_item->optional_alias()->GetAsIdString();
        location = yield_item->optional_alias();
      }

      NameTarget dummy_target;
      if (updated_name_list->LookupName(name, &dummy_target)) {
        return MakeSqlErrorAt(location)
               << "Name " << ToIdentifierLiteral(name)
               << " is already specified in the TVF output";
      }

      GOOGLESQL_RET_CHECK_OK(updated_name_list->AddColumn(name, target.column(),
                                                target.IsExplicit()));
    }

    tvf_output_name_list = std::move(updated_name_list);
  }

  // Ensure the YIELD clause itself does not contain duplicates.
  IdStringHashSetCase distinct_column_names;
  for (IdString name : tvf_output_name_list->GetColumnNames()) {
    if (IsInternalAlias(name)) {
      return MakeSqlErrorAt(call_op.tvf_call())
             << "Anonymous column name in CALL PER() TVF output";
    }
    if (!distinct_column_names.insert(name).second) {
      return MakeSqlErrorAt(call_op.tvf_call())
             << "Duplicate column name in CALL PER() TVF output: "
             << name.ToStringView();
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(&call_op, std::move(tvf_output_name_list)->Copy(),
                           /*group_name_list=*/std::make_shared<NameList>(),
                           input_graph_name_lists.correlated_name_list));

  if (call_op.is_partitioning()) {
    GOOGLESQL_RET_CHECK(call_op.name_capture_list() != nullptr);
    GOOGLESQL_RET_CHECK(call_op.name_capture_list()->identifier_list().empty());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(output_graph_name_lists,
                     MergeGraphNameLists(input_graph_name_lists,
                                         std::move(output_graph_name_lists)));
    // Report error if the final shape contains ambiguous column names.
    GOOGLESQL_RETURN_IF_ERROR(CheckNoAmbiguousNameTarget(
        *call_op.tvf_call(), *output_graph_name_lists.singleton_name_list));
  }

  return {{.resolved_node = std::move(resolved_scan),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlOrderByClause(
    const ASTOrderBy* order_by, const NameScope* external_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  auto& input_scan = inputs.resolved_node;
  auto& input_graph_name_lists = inputs.graph_name_lists;

  if (order_by == nullptr) {
    return inputs;
  }

  const std::unique_ptr<const NameScope> order_by_name_scope(new NameScope(
      external_scope, input_graph_name_lists.singleton_name_list));

  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveOrderBySimple(
      order_by, *input_graph_name_lists.singleton_name_list,
      order_by_name_scope.get(), "GQL standalone ORDER BY",
      Resolver::OrderBySimpleMode::kGql, &input_scan));

  return {{.resolved_node = std::move(input_scan),
           .graph_name_lists = std::move(input_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlPageClauses(
    const ASTGqlPage* page, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  if ((page == nullptr) ||
      (page->limit() == nullptr && page->offset() == nullptr)) {
    return inputs;
  }

  auto& input_scan = inputs.resolved_node;
  auto& input_graph_name_lists = inputs.graph_name_lists;

  auto* limit = page->limit() != nullptr ? page->limit()->limit() : nullptr;
  auto* offset = page->offset() != nullptr ? page->offset()->offset() : nullptr;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveLimitOffsetScan(limit, offset, local_scope,
                                                    &input_scan));

  return {{.resolved_node = std::move(input_scan),
           .graph_name_lists = std::move(input_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlWith(
    const ASTGqlWith& with_op, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_RETURN_EXTENSIONS)) {
    return MakeSqlErrorAt(with_op) << "WITH is not supported";
  }
  const ASTSelectList* return_item_list = with_op.select()->select_list();
  GOOGLESQL_RET_CHECK(return_item_list != nullptr &&
            !return_item_list->columns().empty());
  auto& input_scan = inputs.resolved_node;
  auto& input_graph_name_lists = inputs.graph_name_lists;
  GOOGLESQL_RET_CHECK(input_scan != nullptr);

  const ASTSelect* select = with_op.select();
  // distinct is allowed but not an AST node
  GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckForUnwantedSelectClauseChildNodes(
      select, {select->hint(), select->select_list(), select->group_by()},
      "WITH"));

  const ASTGroupBy* group_by = select->group_by();
  if (group_by != nullptr) {
    for (const ASTGroupingItem* grouping_item : group_by->grouping_items()) {
      if (grouping_item->rollup() || grouping_item->cube() ||
          grouping_item->grouping_set_list()) {
        return MakeSqlErrorAt(group_by)
               << "WITH does not support ROLLUP, CUBE, or GROUPING SETS";
      }
    }
  }

  bool has_star = absl::c_any_of(
      select->select_list()->columns(), [](const ASTSelectColumn* column) {
        return column->expression()->node_kind() == AST_STAR;
      });
  if (has_star && !resolver_->language().LanguageFeatureEnabled(
                      FEATURE_SQL_GRAPH_RETURN_EXTENSIONS)) {
    GOOGLESQL_RETURN_IF_ERROR(CheckReturnStarIsStandalone(select->select_list()));
  }

  NameListPtr select_after_from_name_list;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveSelectAfterFrom(
      select, /*order_by=*/nullptr,
      /*limit_offset=*/nullptr, local_scope, kGraphTableName,
      SelectForm::kGqlWith, WithModifierMode::NONE,
      /*force_new_columns_for_projected_outputs=*/true,
      /*inferred_type_for_query=*/nullptr, &input_scan,
      input_graph_name_lists.singleton_name_list,
      &select_after_from_name_list));

  auto singleton_name_list = select_after_from_name_list->Copy();

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto new_group_name_list,
      ComputeNewGroupNameList(input_graph_name_lists.group_name_list,
                              singleton_name_list));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(&with_op, std::move(singleton_name_list),
                           std::move(new_group_name_list),
                           input_graph_name_lists.correlated_name_list));

  InternalAnalyzerOutputProperties::MarkTargetSyntax(
      resolver_->analyzer_output_properties_,
      const_cast<ResolvedScan*>(input_scan.get()),
      SQLBuildTargetSyntax::kGqlWith);
  return {{.resolved_node = std::move(input_scan),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

absl::StatusOr<
    GraphTableQueryResolver::ResolvedGraphWithNameList<const ResolvedScan>>
GraphTableQueryResolver::ResolveGqlReturn(
    const ASTGqlReturn& return_op, const NameScope* local_scope,
    ResolvedGraphWithNameList<const ResolvedScan> inputs) {
  const ASTSelectList* return_item_list = return_op.select()->select_list();
  GOOGLESQL_RET_CHECK(return_item_list != nullptr &&
            !return_item_list->columns().empty());
  auto& input_scan = inputs.resolved_node;
  auto& input_graph_name_lists = inputs.graph_name_lists;
  GOOGLESQL_RET_CHECK(input_scan != nullptr);

  const ASTSelect* select = return_op.select();
  // distinct is allowed but not an AST node
  GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckForUnwantedSelectClauseChildNodes(
      select, {select->hint(), select->select_list(), select->group_by()},
      "RETURN"));

  const ASTGroupBy* group_by = select->group_by();
  if (group_by != nullptr) {
    for (const ASTGroupingItem* grouping_item : group_by->grouping_items()) {
      if (grouping_item->rollup() || grouping_item->cube() ||
          grouping_item->grouping_set_list()) {
        return MakeSqlErrorAt(group_by)
               << "RETURN does not support ROLLUP, CUBE, or GROUPING SETS";
      }
    }
  }

  bool has_star = absl::c_any_of(
      select->select_list()->columns(), [](const ASTSelectColumn* column) {
        return column->expression()->node_kind() == AST_STAR;
      });
  if (has_star && !resolver_->language().LanguageFeatureEnabled(
                      FEATURE_SQL_GRAPH_RETURN_EXTENSIONS)) {
    GOOGLESQL_RETURN_IF_ERROR(CheckReturnStarIsStandalone(select->select_list()));
  }

  auto* order_by = return_op.order_by_page() != nullptr
                       ? return_op.order_by_page()->order_by()
                       : nullptr;
  auto select_after_from_name_list = std::make_shared<const NameList>();
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveSelectAfterFrom(
      select, order_by,
      /*limit_offset=*/nullptr, local_scope, kGraphTableName,
      SelectForm::kGqlReturn, WithModifierMode::NONE,
      /*force_new_columns_for_projected_outputs=*/true,
      /*inferred_type_for_query=*/nullptr, &input_scan,
      input_graph_name_lists.singleton_name_list,
      &select_after_from_name_list));

  auto* page = return_op.order_by_page() != nullptr
                   ? return_op.order_by_page()->page()
                   : nullptr;
  // Note: We MergeFrom() here since we cannot assign/move 'const NameList' to
  // 'NameList'
  auto singleton_name_list = select_after_from_name_list->Copy();
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto new_group_name_list,
      ComputeNewGroupNameList(input_graph_name_lists.group_name_list,
                              singleton_name_list));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto working_graph_name_lists,
      CreateGraphNameLists(&return_op, std::move(singleton_name_list),
                           std::move(new_group_name_list),
                           input_graph_name_lists.correlated_name_list));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto page_result,
      ResolveGqlPageClauses(
          page, local_scope,
          {.resolved_node = std::move(input_scan),
           .graph_name_lists = std::move(working_graph_name_lists)}));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto output_graph_name_lists,
      CreateGraphNameLists(
          &return_op,
          std::move(page_result.graph_name_lists.singleton_name_list),
          std::move(page_result.graph_name_lists.group_name_list),
          input_graph_name_lists.correlated_name_list));

  // Report error if the final shape contains ambiguous column names.
  GOOGLESQL_RETURN_IF_ERROR(CheckNoAmbiguousNameTarget(
      *return_item_list, *output_graph_name_lists.singleton_name_list));

  return {{.resolved_node = std::move(page_result.resolved_node),
           .graph_name_lists = std::move(output_graph_name_lists)}};
}

absl::StatusOr<GraphTableQueryResolver::CleanupGraphRefCb>
GraphTableQueryResolver::HandleGraphReference(const ASTNode* query) {
  const ASTPathExpression* graph_reference = nullptr;
  if (query->Is<ASTGraphTableQuery>()) {
    graph_reference = query->GetAs<ASTGraphTableQuery>()->graph_reference();
  } else if (query->Is<ASTGqlGraphPatternQuery>()) {
    graph_reference =
        query->GetAs<ASTGqlGraphPatternQuery>()->graph_reference();
  } else if (query->Is<ASTGqlLinearOpsQuery>()) {
    graph_reference = query->GetAs<ASTGqlLinearOpsQuery>()->graph_reference();
  } else {
    return MakeSqlErrorAt(query)
           << "Unexpected node kind for query " << query->GetNodeKindString();
  }
  const bool has_graph_reference = graph_reference != nullptr;
  if (has_graph_reference) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveGraphReference(graph_reference));
    GOOGLESQL_RETURN_IF_ERROR(resolver_->PushPropertyGraphContext(graph_));
  } else {
    graph_ = resolver_->GetActivePropertyGraphOrNull();
    if (graph_ == nullptr) {
      return MakeSqlErrorAt(query)
             << "No graph reference found in the current context of graph "
                "subquery. Try starting the graph subquery with the GRAPH "
                "clause";
    }
  }
  return std::function<void()>([has_graph_reference, this]() {
    if (has_graph_reference) {
      resolver_->PopPropertyGraphContext();
    }
  });
}

GraphTableNamedVariables GraphTableQueryResolver::CreateEmptyGraphNameLists(
    const ASTNode* node) {
  return {.ast_node = node};
}

absl::StatusOr<GraphTableNamedVariables>
GraphTableQueryResolver::CreateGraphNameListsSingletonOnly(
    const ASTNode* node, std::shared_ptr<NameList> singleton_name_list) {
  GraphTableNamedVariables result = {
      .ast_node = node, .singleton_name_list = std::move(singleton_name_list)};
  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphNameLists(result));
  return result;
}

absl::StatusOr<GraphTableNamedVariables>
GraphTableQueryResolver::CreateGraphNameLists(
    const ASTNode* node, std::shared_ptr<NameList> singleton_name_list,
    std::shared_ptr<NameList> group_name_list,
    std::shared_ptr<NameList> correlated_name_list) {
  GraphTableNamedVariables result = {
      .ast_node = node,
      .singleton_name_list = std::move(singleton_name_list),
      .group_name_list = std::move(group_name_list),
      .correlated_name_list = std::move(correlated_name_list)};
  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphNameLists(result));
  return result;
}

NameListPtr GraphTableQueryResolver::GetOutputSingletonNameList(
    const GraphTableNamedVariables& graph_namelists) {
  // Returns an empty singleton name list when the pattern is quantified.
  return (!IsQuantified(graph_namelists.ast_node))
             ? graph_namelists.singleton_name_list
             : std::make_shared<const NameList>();
}

absl::StatusOr<NameListPtr> GraphTableQueryResolver::GetOutputGroupNameList(
    const GraphTableNamedVariables& graph_namelists,
    std::vector<std::unique_ptr<const ResolvedGraphMakeArrayVariable>>&
        new_group_variables) {
  if (!IsQuantified(graph_namelists.ast_node)) {
    return graph_namelists.group_name_list;
  }

  // Returns both singleton and group variables as group variables if the
  // pattern is quantified.
  auto output_name_list = std::make_shared<NameList>();
  if (resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_BOUNDED_PATH_QUANTIFICATION)) {
    for (const NamedColumn& singleton :
         graph_namelists.singleton_name_list->columns()) {
      const ArrayType* array_type;
      GOOGLESQL_RETURN_IF_ERROR(resolver_->type_factory_->MakeArrayType(
          singleton.column().type(), &array_type));
      ResolvedColumn output_column(resolver_->AllocateColumnId(),
                                   singleton.column().table_name_id(),
                                   singleton.column().name_id(), array_type);
      new_group_variables.push_back(MakeResolvedGraphMakeArrayVariable(
          singleton.column(), output_column));
      GOOGLESQL_RETURN_IF_ERROR(output_name_list->AddColumn(output_column.name_id(),
                                                  output_column,
                                                  /*is_explicit=*/false));
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(output_name_list->MergeFrom(
        *graph_namelists.singleton_name_list, graph_namelists.ast_node));
  }
  GOOGLESQL_RETURN_IF_ERROR(output_name_list->MergeFrom(*graph_namelists.group_name_list,
                                              graph_namelists.ast_node));
  return output_name_list;
}

absl::StatusOr<GraphTableNamedVariables>
GraphTableQueryResolver::MergeGraphNameLists(
    GraphTableNamedVariables parent_namelist,
    GraphTableNamedVariables child_namelist,
    std::vector<std::unique_ptr<const ResolvedGraphMakeArrayVariable>>&
        new_group_variables) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto child_group_name_list,
                   GetOutputGroupNameList(child_namelist, new_group_variables));

  // Patterns with nested quantifiers should have been caught earlier.
  GOOGLESQL_RET_CHECK(!IsQuantified(parent_namelist.ast_node) ||
            child_group_name_list->columns().empty());

  // Merge the output-singleton and output-group names of the 'child_namelist'
  // with the singletons and groups in the 'parent_namelist' respectively.
  auto final_singleton_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(final_singleton_list->MergeFrom(
      *parent_namelist.singleton_name_list, parent_namelist.ast_node));
  GOOGLESQL_RETURN_IF_ERROR(final_singleton_list->MergeFrom(
      *GetOutputSingletonNameList(child_namelist), child_namelist.ast_node));

  // group variables can be thought of as singletons of type array
  for (const std::unique_ptr<const ResolvedGraphMakeArrayVariable>& make_array :
       new_group_variables) {
    GOOGLESQL_RETURN_IF_ERROR(final_singleton_list->AddColumn(
        make_array->array().name_id(), make_array->array(),
        /*is_explicit=*/true));
  }

  auto final_group_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(final_group_list->MergeFrom(*parent_namelist.group_name_list,
                                              parent_namelist.ast_node));
  GOOGLESQL_RETURN_IF_ERROR(final_group_list->MergeFrom(*child_group_name_list,
                                              child_namelist.ast_node));

  return CreateGraphNameLists(
      parent_namelist.ast_node, std::move(final_singleton_list),
      std::move(final_group_list), parent_namelist.correlated_name_list);
}

absl::StatusOr<GraphTableNamedVariables>
GraphTableQueryResolver::MergeGraphNameLists(
    GraphTableNamedVariables parent_namelist,
    GraphTableNamedVariables child_namelist) {
  std::vector<std::unique_ptr<const ResolvedGraphMakeArrayVariable>>
      new_group_variables;
  GOOGLESQL_ASSIGN_OR_RETURN(auto merged, MergeGraphNameLists(std::move(parent_namelist),
                                                    std::move(child_namelist),
                                                    new_group_variables));
  GOOGLESQL_RET_CHECK(new_group_variables.empty())
      << "This function must not be used in a quantified context";
  return merged;
}

absl::Status GraphTableQueryResolver::ValidateGraphNameLists(
    const GraphTableNamedVariables& graph_namelists) {
  auto ast_node = graph_namelists.ast_node;
  const auto& singleton_name_list = graph_namelists.singleton_name_list;
  const auto& group_name_list = graph_namelists.group_name_list;

  for (const auto& singleton_variable : singleton_name_list->columns()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckNameNotInDisallowedList(
        singleton_variable.name(), graph_namelists.correlated_name_list,
        ast_node));
  }

  for (const auto& group_variable : group_name_list->columns()) {
    // Validate that no duplicate variable in group name list.
    if (NameTarget name_target;
        group_name_list->LookupName(group_variable.name(), &name_target) &&
        name_target.IsAmbiguous()) {
      return MakeSqlErrorAt(ast_node)
             << "Variable name: " << ToIdentifierLiteral(group_variable.name())
             << " is ambiguous group variable in the "
             << ast_node->GetNodeKindString();
    }

    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_BOUNDED_PATH_QUANTIFICATION)) {
      // Validate that no variable is in both group and singleton name list when
      // direct access to group variables is not allowed. Otherwise this
      // check will be done while resolving multiply declared variables.
      if (NameTarget name_target; singleton_name_list->LookupName(
              group_variable.name(), &name_target)) {
        return MakeSqlErrorAt(ast_node)
               << "Variable name: "
               << ToIdentifierLiteral(group_variable.name())
               << " cannot be used in both quantified pattern and unquantified "
                  "pattern in the same "
               << ast_node->GetNodeKindString();
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace googlesql
