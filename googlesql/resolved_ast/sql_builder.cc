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

// SQLBuilder principles.
//
// - The ResolvedTree is traversed depth first and processed bottom up.
//
// - As we traverse the tree, for each ResolvedNode, we return a QueryFragment.
//   A QueryFragment is either a string (of SQL text) or a QueryExpression. A
//   QueryExpression is a data structure representing a partial query, with
//   fields for various clauses (select, from, where, ...) filled in as strings.
//
// - Each call to Visit will return a new QueryFragment. As the Accept/Visit
//   methods cannot return a value, we maintain a QueryFragment stack that
//   parallels the call stack. Each call to Visit will push its return value
//   onto that stack, and each caller of Accept will pop that return value of
//   that stack. This is encapsulated inside PushQueryFragment and ProcessNode.
//
// - When visiting a Scan or Statement node we build its corresponding
//   QueryExpression and push that as part of the returned QueryFragment. While
//   building a QueryExpression we can either fill something onto an existing
//   QueryExpression (e.g. filling a where clause) or wrap the QueryExpression
//   produced by the input scan (if any). Refer to QueryExpression::Wrap().

#include "googlesql/resolved_ast/sql_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/varsetter.h"
#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/set_operation_resolver_base.h"
#include "googlesql/common/function_utils.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/functions/normalize_mode.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/node_sources.h"
#include "googlesql/resolved_ast/query_expression.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "absl/algorithm/container.h"
#include "absl/base/log_severity.h"
#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/case.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE() \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(      \
      "Out of stack space due to deeply nested query expression")

// Commonly used SQL keywords.
static const char kFrom[] = " FROM ";

// Used as a "magic alias" in some places.
static const char kEmptyAlias[] = "``";

std::string SQLBuilder::GetColumnPath(const ResolvedColumn& column) {
  if (googlesql_base::ContainsKey(column_paths(), column.column_id())) {
    return googlesql_base::FindOrDie(column_paths(), column.column_id());
  }
  return ToIdentifierLiteral(GetColumnAlias(column));
}

std::string SQLBuilder::GetColumnAlias(const ResolvedColumn& column) {
  if (googlesql_base::ContainsKey(computed_column_alias(), column.column_id())) {
    return googlesql_base::FindOrDie(computed_column_alias(), column.column_id());
  }

  const std::string alias = GenerateUniqueAliasName();
  googlesql_base::InsertOrDie(&mutable_computed_column_alias(), column.column_id(), alias);
  return alias;
}

std::string SQLBuilder::UpdateColumnAlias(const ResolvedColumn& column) {
  auto it = computed_column_alias().find(column.column_id());
  ABSL_CHECK(it != computed_column_alias().end())  // Crash OK
      << "Alias does not exist for " << column.DebugString();
  mutable_computed_column_alias().erase(it);
  return GetColumnAlias(column);
}

SQLBuilder::SQLBuilder(const SQLBuilderOptions& options) : options_(options) {}

namespace {
constexpr absl::string_view khalf_of_int64max_str = "4611686018427387903";

// In some cases GoogleSQL name resolution rules cause table names to be
// resolved as column names. See the test in sql_builder.test that is tagged
// "resolution_conflict" for an example. Currently this only happens in
// flattend FilterScans.
//
// To avoid such conflicts, process the AST in two passes. First, collect all
// column names into a set. Then, while generating SQL, create aliases for all
// tables whose names are in the collected set.  The columns are obtained from
// two places:
//   1. all columns of tables from ResolvedTableScan
//   2. all resolved columns elsewhere in the query.
// The latter is probably not necessary, but can't hurt. One might ask why we
// bother with this instead of generating aliases for all tables. We need to
// collect the set of column names to do that too, otherwise generated aliases
// might conflict with column names. And since we have to collect the set, we
// might as well use it to only generate aliases when needed.
class ColumnNameCollector : public ResolvedASTVisitor {
 public:
  explicit ColumnNameCollector(absl::flat_hash_set<std::string>* col_ref_names)
      : col_ref_names_(col_ref_names) {}

 private:
  void Register(absl::string_view col_name) {
    std::string name = absl::AsciiStrToLower(col_name);
    // Value tables do not currently participate in FilterScan flattening, so
    // avoid complexities and don't worry about refs to them.
    if (!value_table_names_.contains(name)) {
      col_ref_names_->insert(name);
    }
  }
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    Register(node->column().name());
    return absl::OkStatus();
  }

  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    const Table* t = node->table();
    if (t->GetColumnListMode() != Table::ColumnListMode::DEFAULT) {
      value_table_names_.insert(absl::AsciiStrToLower(node->table()->Name()));
    } else {
      if (t->IsValueTable()) {
        value_table_names_.insert(absl::AsciiStrToLower(node->table()->Name()));
      }
      for (int i = 0; i < t->NumColumns(); i++) {
        Register(t->GetColumn(i)->Name());
      }
    }
    return absl::OkStatus();
  }

  // Default implementation for ProjectScan visits expr list before input
  // scan (al others look at input_scan first) which prevents us from seeing
  // Value tables before ColumnRefs. Our version visits input_scan first.
  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override {
    if (node->input_scan() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(node->input_scan()->Accept(this));
    }
    for (const auto& elem : node->expr_list()) {
      GOOGLESQL_RETURN_IF_ERROR(elem->Accept(this));
    }
    return absl::OkStatus();
  }

  absl::flat_hash_set<std::string>* const col_ref_names_;
  absl::flat_hash_set<std::string> value_table_names_;
};

// Collects ResolvedScans that contain a side effects scope source,
// and links them to all their targets. The targets will need to be placed
// inside them in the generated SQL.
class SideEffectsScopeCollector : public ResolvedASTVisitor {
 public:
  SideEffectsScopeCollector() = default;
  SideEffectsScopeCollector(const SideEffectsScopeCollector& other) = delete;
  SideEffectsScopeCollector operator=(const SideEffectsScopeCollector& other) =
      delete;

  absl::Status VisitResolvedDeferredComputedColumn(
      const ResolvedDeferredComputedColumn* node) override {
    GOOGLESQL_RET_CHECK(!current_scan_stack_.empty());
    scans_to_collapse_.insert(current_scan_stack_.top());
    return absl::OkStatus();
  }

  absl::Status DefaultVisit(const ResolvedNode* node) override {
    if (node->IsScan()) {
      current_scan_stack_.push(node->GetAs<ResolvedScan>());
      GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
      current_scan_stack_.pop();
    } else {
      GOOGLESQL_RETURN_IF_ERROR(node->ChildrenAccept(this));
    }
    return absl::OkStatus();
  }

  absl::flat_hash_set<const ResolvedScan*> ReleaseScansToCollapse() {
    return std::move(scans_to_collapse_);
  }

 private:
  std::stack<const ResolvedScan*> current_scan_stack_;
  absl::flat_hash_set<const ResolvedScan*> scans_to_collapse_;
};

template <typename Iterator, typename Formatter>
absl::Status StrJoin(std::string* s, Iterator start, Iterator end,
                     absl::string_view separator, Formatter&& fmt) {
  absl::string_view sep("");
  for (Iterator it = start; it != end; ++it) {
    s->append(sep.data(), sep.size());
    GOOGLESQL_RETURN_IF_ERROR(fmt(s, *it));
    sep = separator;
  }
  return absl::OkStatus();
}

template <typename Range, typename Formatter>
absl::Status StrJoin(std::string* s, Range& range, absl::string_view separator,
                     Formatter&& fmt) {
  return StrJoin(s, std::begin(range), std::end(range), separator, fmt);
}

absl::Status AppendColumn(std::string* out, const ResolvedExpr* col) {
  if (col->Is<ResolvedColumnRef>()) {
    absl::StrAppend(out, ToIdentifierLiteral(
                             col->GetAs<ResolvedColumnRef>()->column().name()));
    return absl::OkStatus();
  }
  if (col->Is<ResolvedCatalogColumnRef>()) {
    absl::StrAppend(
        out, ToIdentifierLiteral(
                 col->GetAs<ResolvedCatalogColumnRef>()->column()->Name()));
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Column type must be either ResolvedColumnRef "
                   << "or ResolvedCatalogColumnRef";
}

// Dummy access all the collation_name fields on a ResolvedColumnAnnotations.
void MarkColumnAnnotationsAccessed(
    const ResolvedColumnAnnotations* annotations) {
  if (annotations != nullptr) {
    if (annotations->collation_name() != nullptr) {
      annotations->collation_name()->MarkFieldsAccessed();
    }
    // Dummy access type parameters
    annotations->type_parameters();
    for (int i = 0; i < annotations->child_list_size(); ++i) {
      const ResolvedColumnAnnotations* child_annotations =
          annotations->child_list(i);
      MarkColumnAnnotationsAccessed(child_annotations);
    }
  }
}

bool IsScanUnsupportedInPipeSyntax(const ResolvedScan* node) {
  switch (node->node_kind()) {
    case RESOLVED_ANONYMIZED_AGGREGATE_SCAN:
    case RESOLVED_AGGREGATION_THRESHOLD_AGGREGATE_SCAN:
      return true;
    default:
      return false;
  }
}

absl::Status VisitResolvedGraphNodeTableReferenceInternal(
    const ResolvedGraphNodeTableReference* node, absl::string_view prefix,
    std::string& edge_table_sql) {
  std::string sql;
  absl::StrAppend(&sql, " KEY(");
  GOOGLESQL_RETURN_IF_ERROR(StrJoin(
      &sql, node->edge_table_column_list(), ", ",
      [](std::string* out, const std::unique_ptr<const ResolvedExpr>& col) {
        return AppendColumn(out, col.get());
      }));
  absl::StrAppend(&sql, ") REFERENCES ",
                  ToIdentifierLiteral(node->node_table_identifier()));

  if (!node->node_table_column_list().empty()) {
    absl::StrAppend(&sql, "(");
    GOOGLESQL_RETURN_IF_ERROR(StrJoin(
        &sql, node->node_table_column_list(), ", ",
        [](std::string* out, const std::unique_ptr<const ResolvedExpr>& col) {
          return AppendColumn(out, col.get());
        }));
    absl::StrAppend(&sql, ")");
  }
  absl::StrAppend(&edge_table_sql, prefix, sql);
  return absl::OkStatus();
}

absl::Status AppendLabelAndPropertiesClause(
    const ResolvedGraphElementLabel* label,
    const absl::flat_hash_map<std::string,
                              const ResolvedGraphPropertyDefinition*>&
        name_to_property_def,
    std::string& sql) {
  if (label->property_declaration_name_list().empty()) {
    absl::StrAppend(&sql, "LABEL ", ToIdentifierLiteral(label->name()),
                    " NO PROPERTIES");
    return absl::OkStatus();
  }

  absl::StrAppend(&sql, "LABEL ", ToIdentifierLiteral(label->name()),
                  " PROPERTIES(");

  for (int i = 0; i < label->property_declaration_name_list_size(); ++i) {
    if (i > 0) {
      absl::StrAppend(&sql, ", ");
    }
    auto found_property =
        name_to_property_def.find(label->property_declaration_name_list(i));
    GOOGLESQL_RET_CHECK(found_property != name_to_property_def.end());
    const ResolvedGraphPropertyDefinition* property_def =
        found_property->second;
    if (property_def->is_measure()) {
      absl::StrAppend(&sql, "MEASURE(");
    }
    absl::StrAppend(&sql, property_def->sql());
    if (property_def->is_measure()) {
      absl::StrAppend(&sql, ")");
    }

    if (property_def->sql() != property_def->property_declaration_name()) {
      absl::StrAppend(
          &sql, " AS ",
          ToIdentifierLiteral(property_def->property_declaration_name()));
    }
    property_def->expr()->MarkFieldsAccessed();
  }
  absl::StrAppend(&sql, ")");
  return absl::OkStatus();
}

absl::Status ProcessResolvedGraphDynamicLabelSpecification(
    const ResolvedGraphDynamicLabelSpecification* node, std::string& sql) {
  absl::StrAppend(&sql, " DYNAMIC LABEL (");
  GOOGLESQL_RETURN_IF_ERROR(AppendColumn(&sql, node->label_expr()));
  absl::StrAppend(&sql, ")");
  return absl::OkStatus();
}

absl::Status ProcessResolvedGraphDynamicPropertiesSpecification(
    const ResolvedGraphDynamicPropertiesSpecification* node, std::string& sql) {
  absl::StrAppend(&sql, " DYNAMIC PROPERTIES (");
  GOOGLESQL_RETURN_IF_ERROR(AppendColumn(&sql, node->property_expr()));
  absl::StrAppend(&sql, ")");
  return absl::OkStatus();
}

// Convert a ResolvedGroupingSetBase to GroupingSetIds. `grouping_set_base` must
// be a ResolvedGroupingSet, ResolvedRollup, or ResolvedCube.
absl::StatusOr<GroupingSetIds> ConvertGroupSetBaseToGroupingSetIds(
    const ResolvedGroupingSetBase* grouping_set_base) {
  GroupingSetIds grouping_set_ids;
  GOOGLESQL_RET_CHECK(grouping_set_base->Is<ResolvedGroupingSet>() ||
            grouping_set_base->Is<ResolvedRollup>() ||
            grouping_set_base->Is<ResolvedCube>());
  if (grouping_set_base->Is<ResolvedGroupingSet>()) {
    grouping_set_ids.kind = GroupingSetKind::kGroupingSet;
    const ResolvedGroupingSet* grouping_set =
        grouping_set_base->GetAs<ResolvedGroupingSet>();
    // When it's an empty grouping set, then default GroupingSetIds will be
    // put into the grouping_set_ids_list.
    for (const auto& group_by_column : grouping_set->group_by_column_list()) {
      grouping_set_ids.ids.push_back({group_by_column->column().column_id()});
    }
  } else {
    // This is a ResolvedRollup or ResolvedCube.
    grouping_set_ids.kind = grouping_set_base->Is<ResolvedRollup>()
                                ? GroupingSetKind::kRollup
                                : GroupingSetKind::kCube;
    const auto& multi_column_list =
        grouping_set_base->Is<ResolvedRollup>()
            ? grouping_set_base->GetAs<ResolvedRollup>()->rollup_column_list()
            : grouping_set_base->GetAs<ResolvedCube>()->cube_column_list();
    for (const auto& multi_column : multi_column_list) {
      std::vector<int> ids;
      for (const auto& column_ref : multi_column->column_list()) {
        ids.push_back(column_ref->column().column_id());
      }
      // There must be at least one element in the rollup or cube
      GOOGLESQL_RET_CHECK_GT(ids.size(), 0);
      grouping_set_ids.ids.push_back(ids);
    }
  }
  return grouping_set_ids;
}

// Convert a list of ResolvedGroupingSetBase to a list of GroupingSetIds.
absl::StatusOr<std::vector<GroupingSetIds>> ConvertGroupSetBaseIdList(
    absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
        grouping_set_list) {
  std::vector<GroupingSetIds> grouping_set_ids_list;
  for (const auto& grouping_set_base : grouping_set_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto grouping_set_ids, ConvertGroupSetBaseToGroupingSetIds(
                                                grouping_set_base.get()));
    grouping_set_ids_list.push_back(grouping_set_ids);
  }
  return grouping_set_ids_list;
}

// Convert a ResolvedGroupingSetProduct to GroupingSetIdsInfo.
absl::Status ConvertProductGroupingSetBaseIdList(
    const ResolvedGroupingSetProduct* grouping_set_product,
    GroupingSetIdsInfo& grouping_set_ids_info) {
  for (const auto& grouping_set_base : grouping_set_product->input_list()) {
    GOOGLESQL_RET_CHECK(!grouping_set_base->Is<ResolvedGroupingSetProduct>());
    if (grouping_set_base->Is<ResolvedGroupingSetList>()) {
      GOOGLESQL_ASSIGN_OR_RETURN(auto grouping_set_ids_list,
                       ConvertGroupSetBaseIdList(
                           grouping_set_base->GetAs<ResolvedGroupingSetList>()
                               ->elem_list()));
      grouping_set_ids_info.product_grouping_set_ids_list.push_back(
          grouping_set_ids_list);
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto grouping_set_ids,
          ConvertGroupSetBaseToGroupingSetIds(grouping_set_base.get()));
      grouping_set_ids_info.product_grouping_set_ids_list.push_back(
          {grouping_set_ids});
    }
  }
  return absl::OkStatus();
}

// Helper function to convert list of ResolvedGroupingSet (AST Nodes) to
// GroupingSetIdsInfo.
//
// This function has a presumption about the structure of `grouping_set_list`.
// `grouping_set_list` can have two possible structures: one that represents a
// set of grouping sets without cross-product operations, in which case it only
// contains ResolvedGroupingSet, ResolvedRollup, and ResolvedCube:
//
// +-grouping_set_list=
//   +-ResolvedGroupingSet
//   +-ResolvedRollup
//   +-ResolvedCube
//
// The other represents a grouping set product
// ((broken link)). In this case, only one
// ResolvedGroupingSetProduct is present in the list. The input list of the
// ResolvedGroupingSetProduct could contain ResolvedGroupingSet, ResolvedRollup,
// ResolvedCube and ResolvedGroupingSetList, and the ResolvedGroupingSetList
// contains a list of ResolvedGroupingSet, ResolvedRollup, and ResolvedCube.
//
// +-grouping_set_list=
//   +-ResolvedGroupingSetProduct
//     +-input_list=
//       +-ResolvedGroupingSet
//       +-ResolvedRollup
//       +-ResolvedCube
//       +-ResolvedGroupingSetList
//         +-elem_list=
//           +-ResolvedGroupingSet
//           +-ResolvedRollup
//           +-ResolvedCube
absl::Status ConvertGroupingSetIdsInfo(
    absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
        grouping_set_list,
    absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
        rollup_column_list,
    std::vector<int>& rollup_column_id_list,
    GroupingSetIdsInfo& grouping_set_ids_info) {
  if (grouping_set_list.empty() && rollup_column_list.empty()) {
    return absl::OkStatus();
  }

  grouping_set_ids_info.is_product =
      grouping_set_list.size() == 1 &&
      grouping_set_list[0]->Is<ResolvedGroupingSetProduct>();
  // The check only works when only ROLLUP exists in the query
  if (!rollup_column_list.empty()) {
    GOOGLESQL_RET_CHECK(!grouping_set_ids_info.is_product);
    GOOGLESQL_RET_CHECK_EQ(grouping_set_list.size(), rollup_column_list.size() + 1);
    for (const auto& column_ref : rollup_column_list) {
      rollup_column_id_list.push_back(column_ref->column().column_id());
    }
  }
  if (grouping_set_ids_info.is_product) {
    return ConvertProductGroupingSetBaseIdList(
        grouping_set_list[0]->GetAs<ResolvedGroupingSetProduct>(),
        grouping_set_ids_info);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(grouping_set_ids_info.non_product_grouping_set_ids_list,
                   ConvertGroupSetBaseIdList(grouping_set_list));
  return absl::OkStatus();
}

// Helper function to create a map of the grouping_column id, to the group_by
// column ref it points to. In the ProcessAggregateScanBase, this will map the
// grouping_column_id to the correct ResolvedColumnRef that the group_by
// column references. This is necessary to rebuild the SQL for GROUPING
// function calls, which have their arguments stored in the ResolvedAST as
// a ColumnRef of a group_by computed column.
absl::flat_hash_map<int, int> CreateGroupingColumnIdMap(
    absl::Span<const std::unique_ptr<const ResolvedGroupingCall>>
        grouping_call_list) {
  absl::flat_hash_map<int, int> grouping_column_id_map;
  if (!grouping_call_list.empty()) {
    // Mark grouping_call_list fields as accessed.
    for (const auto& grouping_call : grouping_call_list) {
      grouping_call->group_by_column();
    }
    for (const auto& computed_col : grouping_call_list) {
      std::vector<const ResolvedNode*> column_ref_nodes;
      computed_col->GetChildNodes(&column_ref_nodes);
      for (const auto& column_ref : column_ref_nodes) {
        int column_ref_id =
            column_ref->GetAs<ResolvedColumnRef>()->column().column_id();
        googlesql_base::InsertOrDie(&grouping_column_id_map,
                         computed_col->output_column().column_id(),
                         column_ref_id);
      }
    }
  }
  return grouping_column_id_map;
}

ResolvedColumnList GetRecursiveScanColumnsExcludingDepth(
    const ResolvedRecursiveScan* scan) {
  if (scan->recursion_depth_modifier() == nullptr) {
    return scan->column_list();
  }
  ResolvedColumn depth_column =
      scan->recursion_depth_modifier()->recursion_depth_column()->column();
  ResolvedColumnList columns_excluding_depth;
  for (const auto& col : scan->column_list()) {
    if (col != depth_column) {
      columns_excluding_depth.push_back(col);
    }
  }
  return columns_excluding_depth;
}
}  // namespace

absl::Status SQLBuilder::Process(const ResolvedNode& ast) {
  ast.ClearFieldsAccessed();
  ColumnNameCollector name_collector(&mutable_col_ref_names());
  GOOGLESQL_RETURN_IF_ERROR(ast.Accept(&name_collector));

  SideEffectsScopeCollector side_effects_scope_collector;
  GOOGLESQL_RETURN_IF_ERROR(ast.Accept(&side_effects_scope_collector));
  scans_to_collapse_ = side_effects_scope_collector.ReleaseScansToCollapse();

  ast.ClearFieldsAccessed();
  GOOGLESQL_RETURN_IF_ERROR(ast.Accept(this));
  GOOGLESQL_RET_CHECK(state_.recursive_query_info.empty())
      << "Recursive query info stack has: "
      << state_.recursive_query_info.size() << " elements";
  return absl::OkStatus();
}

std::string SQLBuilder::QueryFragment::GetSQL() const {
  if (query_expression != nullptr) {
    // At this stage all QueryExpression parts (SELECT list, FROM clause,
    // etc.) have already had string fragments generated for them, so this
    // step of concatenating all the string pieces is not sensitive to
    // ProductMode.
    return query_expression->GetSQLQuery();
  }
  return text;
}

SQLBuilder::~SQLBuilder() = default;

void SQLBuilder::DumpQueryFragmentStack() {
}

void SQLBuilder::PushQueryFragment(
    std::unique_ptr<QueryFragment> query_fragment) {
  ABSL_DCHECK(query_fragment != nullptr);
  // We generate sql_ at once in the end, i.e. after the complete traversal of
  // the given ResolvedAST. Whenever a new QueryFragment is pushed, that means
  // we would need to incorporate it in the final sql which renders any
  // previously generated sql irrelevant.
  sql_.clear();
  query_fragments_.push_back(std::move(query_fragment));
}

void SQLBuilder::PushQueryFragment(const ResolvedNode* node,
                                   const std::string& text) {
  PushQueryFragment(std::make_unique<QueryFragment>(node, text));
}

void SQLBuilder::PushQueryFragment(const ResolvedNode* node,
                                   QueryExpression* query_expression) {
  PushQueryFragment(std::make_unique<QueryFragment>(node, query_expression));
}

std::unique_ptr<SQLBuilder::QueryFragment> SQLBuilder::PopQueryFragment() {
  ABSL_DCHECK(!query_fragments_.empty());
  std::unique_ptr<SQLBuilder::QueryFragment> f =
      std::move(query_fragments_.back());
  query_fragments_.pop_back();
  return f;
}

absl::StatusOr<std::unique_ptr<SQLBuilder::QueryFragment>>
SQLBuilder::ProcessNode(const ResolvedNode* node) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(node != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(node->Accept(this));
  GOOGLESQL_RET_CHECK_EQ(query_fragments_.size(), 1) << CurrentStackTrace();
  return PopQueryFragment();
}

absl::StatusOr<std::string> AddExplicitCast(absl::string_view sql,
                                            const Type* type,
                                            const TypeModifiers& type_modifiers,
                                            ProductMode mode,
                                            bool use_external_float32) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string type_name,
      type->TypeNameWithModifiers(type_modifiers, mode, use_external_float32));
  return absl::StrFormat("CAST(%s AS %s)", sql, type_name);
}

// Usually we add explicit casts to ensure that typed literals match their
// resolved type.  But <is_constant_value> is a hack to work around a
// limitation in the run_analyzer_test infrastructure.  This hack is necessary
// because the run_analyzer_test infrastructure doesn't know how to properly
// compare the 'shapes' of query OPTIONS/HINTS when the original query
// option is a constant while the unparsed query options is a CAST expression.
// TODO: The run_analyzer_test infrastructure no longer compares the
// option/hint expression shapes, and only compares their types, so we should
// remove the <is_constant_value> hack from this method.
absl::StatusOr<std::string> SQLBuilder::GetSQL(
    const Value& value, const AnnotationMap* annotation_map, ProductMode mode,
    bool use_external_float32, bool is_constant_value) {
  const Type* type = value.type();

  if (annotation_map != nullptr) {
    GOOGLESQL_RET_CHECK(annotation_map->HasCompatibleStructure(type));
    if (annotation_map->Has<CollationAnnotation>()) {
      GOOGLESQL_RET_CHECK(value.is_null() && !is_constant_value);
    }
  }

  // If the opaque enum type is not fully supported in the catalog, we cannot
  // use an explicit CAST when building the SQL, and instead:
  //   -If non-null, render a string literal representation of its value
  //   -If null, render NULL
  bool is_opaque_enum_not_in_catalog =
      type->IsEnum() && type->AsEnum()->IsOpaque();
  if (options_.catalog != nullptr) {
    const Type* ignored = nullptr;
    absl::Status find_type_status =
        options_.catalog->FindType({type->TypeName(mode)}, &ignored);
    if (find_type_status.code() == absl::StatusCode::kInternal) {
      return find_type_status;
    }
    if (find_type_status.ok()) {
      is_opaque_enum_not_in_catalog = false;
    }
  }

  if (value.is_null()) {
    if (is_constant_value || is_opaque_enum_not_in_catalog ||
        type->IsGraphElement() || type->IsGraphPath()) {
      // To handle NULL literals in ResolvedOption, where we would not want to
      // print them as a casted literal.
      return std::string("NULL");
    } else if (CollationAnnotation::ExistsIn(annotation_map)) {
      // TODO: Put this logic into value.GetSQL(mode) function to
      // avoid logic duplication. Would need to change the function signature or
      // use a new function name.
      GOOGLESQL_ASSIGN_OR_RETURN(Collation collation,
                       Collation::MakeCollation(*annotation_map));
      return AddExplicitCast(
          "NULL", type,
          TypeModifiers::MakeTypeModifiers(TypeParameters(), collation), mode,
          use_external_float32);
    } else {
      return value.GetSQL(mode, use_external_float32);
    }
  }

  if (type->IsTimestamp() || type->IsCivilDateOrTimeType()) {
    std::string timestamp_literal_sql;
    if (is_constant_value) {
      timestamp_literal_sql = ToStringLiteral(value.DebugString());
    } else {
      timestamp_literal_sql = value.GetSQL(mode);
    }

    return timestamp_literal_sql;
  }

  if (type->IsUuid()) {
    return value.GetSQL(mode);
  }

  if (type->IsSimpleType()) {
    // To handle simple types, where we would not want to print them as cast
    // expressions.
    if (is_constant_value) {
      return value.DebugString();
    }
    return value.GetSQL(mode, use_external_float32);
  }

  if (type->IsEnum()) {
    // Special cases to handle DateTimePart and NormalizeMode enums which are
    // treated as identifiers or expressions in the parser (not literals).
    absl::string_view enum_full_name =
        type->AsEnum()->enum_descriptor()->full_name();
    if (enum_full_name ==
        functions::DateTimestampPart_descriptor()->full_name()) {
      return std::string(functions::DateTimestampPartToSQL(value.enum_value()));
    }
    if (enum_full_name == functions::NormalizeMode_descriptor()->full_name()) {
      return ToIdentifierLiteral(value.DebugString());
    }

    if (enum_full_name ==
        functions::DifferentialPrivacyEnums::ReportFormat_descriptor()
            ->full_name()) {
      return ToStringLiteral(value.DebugString());
    }

    // For typed hints, we can't print a CAST for enums because the parser
    // won't accept it back in.
    if (is_constant_value || is_opaque_enum_not_in_catalog) {
      // For opaque enums (such as ROUNDING_MODE) should render fine with
      // GetSQL which will include a CAST(). Otherwise, the opaque type is
      // not fully supported in the catalog, in which case we can't use
      // CAST, but we also assume the only valid reference is a literal
      // to a function call, which we can just render as a string literal.
      return ToStringLiteral(value.DebugString());
    }
    return value.GetSQL(mode);
  }
  if (type->IsProto()) {
    // Cannot use Value::GetSQL() for proto types as it returns bytes (casted as
    // proto) instead of strings.
    // TODO: May have an issue here with EXTERNAL mode.
    const std::string proto_str = value.DebugString();
    // Strip off the curly braces encapsulating the proto value, so that it
    // could be recognized as a string literal in the unparsed sql, which could
    // be coerced to a proto literal later.
    GOOGLESQL_RET_CHECK_GT(proto_str.size(), 1);
    GOOGLESQL_RET_CHECK_EQ(proto_str[0], '{');
    GOOGLESQL_RET_CHECK_EQ(proto_str[proto_str.size() - 1], '}');
    const std::string literal_str =
        ToStringLiteral(proto_str.substr(1, proto_str.size() - 2));
    // For typed hints, we can't print a CAST for protos because the parser
    // won't accept it back in.
    if (is_constant_value) {
      return literal_str;
    }
    return AddExplicitCast(literal_str, type, TypeModifiers(), mode,
                           use_external_float32);
  }
  if (type->IsStruct()) {
    // Once STRUCT<...>(...) syntax is supported in hints, and CASTs work in
    // hints, we could just use GetSQL always.
    const StructType* struct_type = type->AsStruct();
    std::vector<std::string> fields_sql;

    // Get SQL string for each field providing it's annotation map (if any).
    GOOGLESQL_RET_CHECK(annotation_map == nullptr ||
              annotation_map->AsStructMap()->num_fields() ==
                  value.fields().size());
    for (int i = 0; i < value.fields().size(); ++i) {
      const auto& field_value = value.fields()[i];
      const AnnotationMap* field_annotation_map =
          annotation_map == nullptr ? nullptr
                                    : annotation_map->AsStructMap()->field(i);
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                       GetSQL(field_value, field_annotation_map, mode,
                              use_external_float32, is_constant_value));
      fields_sql.push_back(result);
    }

    // If any of the fields have names (are not anonymous) then we need to add
    // them in the returned SQL.
    std::vector<std::string> field_types;
    bool has_explicit_field_name = false;
    for (const StructField& field_type : type->AsStruct()->fields()) {
      std::string field_name;
      if (!field_type.name.empty()) {
        has_explicit_field_name = true;
        field_name = absl::StrCat(ToIdentifierLiteral(field_type.name), " ");
      }
      field_types.push_back(absl::StrCat(
          field_name, field_type.type->TypeName(mode, use_external_float32)));
    }
    ABSL_DCHECK_EQ(type->AsStruct()->num_fields(), fields_sql.size());

    if (has_explicit_field_name) {
      return absl::StrCat("STRUCT<", absl::StrJoin(field_types, ", "), ">(",
                          absl::StrJoin(fields_sql, ", "), ")");
    } else {
      return absl::StrCat(struct_type->TypeName(mode, use_external_float32),
                          "(", absl::StrJoin(fields_sql, ", "), ")");
    }
  }
  if (type->IsArray()) {
    std::vector<std::string> elements_sql;
    const AnnotationMap* element_annotation_map =
        annotation_map == nullptr ? nullptr
                                  : annotation_map->AsStructMap()->field(0);
    for (const auto& elem : value.elements()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                       GetSQL(elem, element_annotation_map, mode,
                              use_external_float32, is_constant_value));
      elements_sql.push_back(result);
    }

    Collation collation;
    if (CollationAnnotation::ExistsIn(annotation_map)) {
      GOOGLESQL_ASSIGN_OR_RETURN(collation, Collation::MakeCollation(*annotation_map));
    }

    return absl::StrCat(type->TypeName(mode, use_external_float32), "[",
                        absl::StrJoin(elements_sql, ", "), "]");
  }
  if (type->IsRange()) {
    return value.GetSQL(mode);
  }
  if (type->IsMap()) {
    return value.GetSQL(mode, use_external_float32);
  }
  if (type->IsExtendedType()) {
    return value.GetSQL(mode);
  }

  return ::googlesql_base::InvalidArgumentErrorBuilder()
         << "Value has unknown type: " << type->DebugString();
}

absl::StatusOr<std::string> SQLBuilder::GetSQL(const Value& value,
                                               ProductMode mode,
                                               bool is_constant_value) {
  return GetSQL(value, /*annotation_map=*/nullptr, mode,
                /*use_external_float32=*/false, is_constant_value);
}

absl::StatusOr<std::string> SQLBuilder::GetSQL(
    const Value& value, const AnnotationMap* annotation_map, ProductMode mode,
    bool is_constant_value) {
  return GetSQL(value, /*annotation_map=*/nullptr, mode,
                /*use_external_float32=*/false, is_constant_value);
}

absl::StatusOr<std::string> SQLBuilder::GetSQL(const Value& value,
                                               ProductMode mode,
                                               bool use_external_float32,
                                               bool is_constant_value) {
  return GetSQL(value, /*annotation_map=*/nullptr, mode, use_external_float32,
                is_constant_value);
}

absl::Status SQLBuilder::VisitResolvedCloneDataStmt(
    const ResolvedCloneDataStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "CLONE DATA INTO ");
  absl::StrAppend(&sql,
                  ToIdentifierLiteral(node->target_table()->table()->Name()),
                  " FROM ");
  if (node->clone_from()->node_kind() == RESOLVED_SET_OPERATION_SCAN) {
    const ResolvedSetOperationScan* set =
        node->clone_from()->GetAs<ResolvedSetOperationScan>();
    set->op_type();
    set->column_list();
    set->input_item_list(0)->output_column_list();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> source,
                     ProcessNode(set->input_item_list(0)->scan()));
    GOOGLESQL_RETURN_IF_ERROR(
        AppendCloneDataSource(set->input_item_list(0)->scan(), &sql));
    for (int i = 1; i < set->input_item_list_size(); i++) {
      set->input_item_list(i)->output_column_list();
      absl::StrAppend(&sql, " UNION ALL ");
      GOOGLESQL_RET_CHECK_EQ(set->op_type(), ResolvedSetOperationScan::UNION_ALL);
      GOOGLESQL_RETURN_IF_ERROR(
          AppendCloneDataSource(set->input_item_list(i)->scan(), &sql));
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(AppendCloneDataSource(node->clone_from(), &sql));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedExpressionColumn(
    const ResolvedExpressionColumn* node) {
  PushQueryFragment(node, ToIdentifierLiteral(node->name()));
  return absl::OkStatus();
}
absl::Status SQLBuilder::VisitResolvedCatalogColumnRef(
    const ResolvedCatalogColumnRef* node) {
  PushQueryFragment(node, ToIdentifierLiteral(node->column()->Name()));
  return absl::OkStatus();
}
absl::Status SQLBuilder::VisitResolvedLiteral(const ResolvedLiteral* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      const std::string result,
      GetSQL(node->value(), node->type_annotation_map(),
             options_.language_options.product_mode(),
             options_.use_external_float32, /*is_constant_value=*/false));
  PushQueryFragment(node, result);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedConstant(const ResolvedConstant* node) {
  PushQueryFragment(
      node, absl::StrJoin(node->constant()->name_path(), ".",
                          [](std::string* out, absl::string_view part) {
                            absl::StrAppend(out, ToIdentifierLiteral(part));
                          }));
  return absl::OkStatus();
}

// Call Function::GetSQL(), and also add the "SAFE." prefix if
// <function_call> is in SAFE_ERROR_MODE.
absl::StatusOr<std::string> SQLBuilder::GetFunctionCallSQL(
    const ResolvedFunctionCallBase* function_call,
    std::vector<std::string> inputs, absl::string_view arguments_suffix,
    bool allow_chained_call) {
  // Mark accessed. There is no SQL clause for the <collation_list> field.
  function_call->collation_list();

  const bool is_safe_call =
      (function_call->error_mode() ==
       ResolvedNonScalarFunctionCallBase::SAFE_ERROR_MODE);

  // Try using a chained function call if the hint is set.
  // The call must have at least one argument (the first), with expression type,
  // Named-only arguments are also excluded but that is detected inside GetSQL.
  // Aliases are excluded but that's handled in the caller, unsetting
  // `allow_chained_call`.
  bool is_chained_call =
      allow_chained_call &&
      googlesql_base::ContainsKeyValuePair(options_.target_syntax_map, function_call,
                                SQLBuildTargetSyntax::kChainedFunctionCall) &&
      (function_call->argument_list_size() > 0 ||
       (function_call->generic_argument_list_size() > 0 &&
        function_call->generic_argument_list().front()->expr() != nullptr));

  // DISTINCT modifiers on aggregate or analytic calls are passed
  // in as `arguments_prefix`.
  absl::string_view arguments_prefix;
  if (function_call->Is<ResolvedNonScalarFunctionCallBase>() &&
      function_call->GetAs<ResolvedNonScalarFunctionCallBase>()->distinct()) {
    arguments_prefix = "distinct";
  }

  std::string sql = function_call->function()->GetSQL(
      std::move(inputs), &function_call->signature(), arguments_prefix,
      arguments_suffix, is_safe_call, is_chained_call);

  if (!function_call->hint_list().empty()) {
    absl::StrAppend(&sql, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(function_call->hint_list(), &sql));
  }
  return sql;
}

absl::StatusOr<bool> ShouldArrayCtorPrintExplicitType(
    const ResolvedFunctionCall* node) {
  if (!TypeIsOrContainsGraphElement(node->type()) &&
      !IsOrContainsMeasure(node->type()) &&
      !CollationAnnotation::ExistsIn(node->type_annotation_map())
  ) {
    return true;
  }

  if (node->argument_list_size() == 0) {
    return absl::InvalidArgumentError(
        "Array constructor must have at least one argument when MEASURE "
        "or graph types are present");
  }
  return false;
}

absl::Status SQLBuilder::VisitResolvedFunctionCall(
    const ResolvedFunctionCall* node) {
  std::vector<std::string> inputs;
  if (node->function()->IsGoogleSQLBuiltin()) {
    if (node->function()->GetSignature(0)->context_id() == FN_MAKE_ARRAY) {
      // For MakeArray function we explicitly prepend the array type to the
      // function sql, and is passed as a part of the inputs.
      GOOGLESQL_ASSIGN_OR_RETURN(bool should_print_explicit_type,
                       ShouldArrayCtorPrintExplicitType(node));
      if (should_print_explicit_type) {
        inputs.push_back(
            node->type()->TypeName(options_.language_options.product_mode(),
                                   options_.use_external_float32));
      } else {
        inputs.push_back("ARRAY");
      }
    } else if (node->function()->GetSignature(0)->context_id() ==
               FN_WITH_SIDE_EFFECTS) {
      // The $with_side_effects() function is just symbolic to represent side
      // effect scopes.
      GOOGLESQL_RET_CHECK(!node->argument_list().empty());

      // Dummy access all other args.
      for (const auto& argument : node->argument_list()) {
        argument->MarkFieldsAccessed();
      }

      GOOGLESQL_RET_CHECK(node->argument_list(0)->Is<ResolvedColumnRef>());
      GOOGLESQL_RETURN_IF_ERROR(VisitResolvedColumnRef(
          node->argument_list(0)->GetAs<ResolvedColumnRef>()));
      return absl::OkStatus();
    }
  }

  for (const auto& argument : node->argument_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(argument.get()));
    inputs.push_back(result->GetSQL());
  }
  bool allow_chained_call = true;
  bool first_arg = true;
  for (const auto& argument : node->generic_argument_list()) {
    if (argument->expr() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(argument->expr()));
      inputs.push_back(result->GetSQL());
    } else {
      if (first_arg) {
        allow_chained_call = false;
      }
      if (argument->inline_lambda() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(argument->inline_lambda()));
        inputs.push_back(result->GetSQL());
      } else if (argument->sequence() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(argument->sequence()));
        inputs.push_back(result->GetSQL());
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected function call argument: "
                         << argument->DebugString();
      }
    }
    if (!argument->argument_alias().empty()) {
      absl::StrAppend(&inputs.back(), " AS ",
                      ToIdentifierLiteral(argument->argument_alias()));
      if (first_arg) {
        allow_chained_call = false;
      }
    }
    first_arg = false;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto sql, GetFunctionCallSQL(node, std::move(inputs),
                                                /*arguments_suffix=*/"",
                                                allow_chained_call));
  // Getting the SQL for a function given string arguments is not itself
  // sensitive to the ProductMode.
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedInlineLambda(
    const ResolvedInlineLambda* node) {
  ABSL_DCHECK(node->body());

  std::string args_list =
      absl::StrJoin(node->argument_list(), ",",
                    [this](std::string* out, const ResolvedColumn& col) {
                      *out += GetColumnAlias(col);
                    });

  // Skip the parentheses for single argument case.
  if (node->argument_list_size() != 1) {
    args_list = absl::StrCat("(", args_list, ")");
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr_fragment,
                   ProcessNode(node->body()));
  std::string lambda_sql =
      absl::StrCat(args_list, " -> ", expr_fragment->GetSQL());
  PushQueryFragment(node, lambda_sql);

  // Dummy access on the parameter list so as to pass the final
  // CheckFieldsAccessed() on a statement level before building the sql.
  for (const auto& parameter : node->parameter_list()) {
    parameter->column();
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedSequence(const ResolvedSequence* node) {
  ABSL_DCHECK(node->sequence());
  const std::string sequence_alias =
      ToIdentifierLiteral(node->sequence()->Name());
  PushQueryFragment(node, absl::StrCat("SEQUENCE ", sequence_alias));
  return absl::OkStatus();
}

static bool IsFirstOrLast(const Function* function) {
  if (!function->IsGoogleSQLBuiltin() || function->NumSignatures() != 1) {
    return false;
  }
  return function->GetSignature(0)->context_id() == FN_FIRST_AGG ||
         function->GetSignature(0)->context_id() == FN_LAST_AGG;
}

absl::Status SQLBuilder::VisitResolvedAggregateFunctionCall(
    const ResolvedAggregateFunctionCall* node) {
  bool allow_chained_call = true;
  std::string arguments_suffix;
  std::string with_group_rows;

  // Handle multi-level aggregation.
  std::string group_by_modifiers;
  if (!node->group_by_list().empty()) {
    std::vector<std::string> group_by_arguments;
    for (const auto& grouping_column : node->group_by_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(grouping_column.get()));
      group_by_arguments.push_back(result->GetSQL());
      googlesql_base::InsertOrDie(&mutable_pending_columns(),
                       grouping_column->column().column_id(), result->GetSQL());
    }
    GOOGLESQL_RET_CHECK(!group_by_arguments.empty());
    std::string group_by_modifier_hints;
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->group_by_hint_list(),
                                         &group_by_modifier_hints));
    if (!group_by_modifier_hints.empty()) {
      absl::StrAppend(&group_by_modifier_hints, " ");
    }
    group_by_modifiers = absl::StrCat(" GROUP ", group_by_modifier_hints, "BY ",
                                      absl::StrJoin(group_by_arguments, ", "));
    // Now process the `group_by_aggregate_list`
    for (const auto& aggregate_function : node->group_by_aggregate_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(aggregate_function.get()));
      googlesql_base::InsertOrDie(&mutable_pending_columns(),
                       aggregate_function->column().column_id(),
                       result->GetSQL());
    }
  }

  std::vector<std::string> inputs;
  if (node->argument_list_size() > 0) {
    for (const auto& argument : node->argument_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(argument.get()));
      inputs.push_back(result->GetSQL());
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(AddNullHandlingModifier(node->null_handling_modifier(),
                                          &arguments_suffix));

  if (node->where_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->where_expr()));
    absl::StrAppend(&arguments_suffix, " WHERE ", result->GetSQL());
  }

  if (node->having_modifier() != nullptr) {
    GOOGLESQL_RET_CHECK(group_by_modifiers.empty());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->having_modifier()));
    absl::StrAppend(&arguments_suffix, " ", result->GetSQL());
  }

  if (!group_by_modifiers.empty()) {
    allow_chained_call = false;
    absl::StrAppend(&arguments_suffix, " ", group_by_modifiers);
  }

  if (node->having_expr() != nullptr) {
    GOOGLESQL_RET_CHECK(!group_by_modifiers.empty());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->having_expr()));
    // Guaranteed to have a group by modifier so we can just append.
    absl::StrAppend(&arguments_suffix, " HAVING ", result->GetSQL());
  }

  if (!node->order_by_item_list().empty()) {
    if (IsFirstOrLast(node->function())) {
      GOOGLESQL_RET_CHECK(state_.match_recognize_state.has_value());
      GOOGLESQL_RET_CHECK_EQ(node->order_by_item_list_size(), 1);
      GOOGLESQL_RET_CHECK_EQ(
          node->order_by_item_list(0)->column_ref()->column().column_id(),
          state_.match_recognize_state->match_row_number_column.column_id());
      // Do not print the ORDER BY. In SQL, the ORDER BY is always implicit.
    } else {
      std::vector<std::string> order_by_arguments;
      for (const auto& order_by_item : node->order_by_item_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(order_by_item.get()));
        order_by_arguments.push_back(result->GetSQL());
      }
      absl::StrAppend(&arguments_suffix, " ORDER BY ",
                      absl::StrJoin(order_by_arguments, ", "));
    }
  }

  if (node->limit() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->limit()));
    absl::StrAppend(&arguments_suffix, " LIMIT ", result->GetSQL());
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::string text,
                   GetFunctionCallSQL(node, std::move(inputs), arguments_suffix,
                                      allow_chained_call));

  absl::StrAppend(&text, with_group_rows);

  PushQueryFragment(node, text);

  return absl::OkStatus();
}

class SQLBuilder::AnalyticFunctionInfo {
 public:
  explicit AnalyticFunctionInfo(const std::string& function)
      : function_(function) {}
  AnalyticFunctionInfo(const AnalyticFunctionInfo&) = delete;
  AnalyticFunctionInfo operator=(const AnalyticFunctionInfo&) = delete;

  std::string GetSQL() const;

  void set_partition_by(absl::string_view partition_by) {
    partition_by_ = partition_by;
  }

  void set_order_by(absl::string_view order_by) { order_by_ = order_by; }

  void set_window(absl::string_view window) { window_ = window; }

 private:
  const std::string function_;
  std::string partition_by_;
  std::string order_by_;
  std::string window_;
};

std::string SQLBuilder::AnalyticFunctionInfo::GetSQL() const {
  std::vector<std::string> over_clause;
  if (!partition_by_.empty()) {
    over_clause.push_back(partition_by_);
  }
  if (!order_by_.empty()) {
    over_clause.push_back(order_by_);
  }
  if (!window_.empty()) {
    over_clause.push_back(window_);
  }
  return absl::StrCat(function_, " OVER (", absl::StrJoin(over_clause, " "),
                      ")");
}

SQLBuilder::SQLBuilder(int* max_seen_alias_id_root_ptr,
                       const SQLBuilderOptions& options,
                       const CopyableState& state)
    : max_seen_alias_id_root_ptr_(max_seen_alias_id_root_ptr),
      options_(options),
      state_(state) {}

absl::Status SQLBuilder::VisitResolvedAnalyticFunctionCall(
    const ResolvedAnalyticFunctionCall* node) {
  std::vector<std::string> inputs;
  for (const auto& argument : node->argument_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(argument.get()));
    inputs.push_back(result->GetSQL());
  }

  std::string arguments_suffix;

  if (node->where_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->where_expr()));
    absl::StrAppend(&arguments_suffix, " where ", result->GetSQL());
  }

  GOOGLESQL_RETURN_IF_ERROR(AddNullHandlingModifier(node->null_handling_modifier(),
                                          &arguments_suffix));

  GOOGLESQL_ASSIGN_OR_RETURN(std::string sql, GetFunctionCallSQL(node, std::move(inputs),
                                                       arguments_suffix));
  std::unique_ptr<AnalyticFunctionInfo> analytic_function_info =
      std::make_unique<AnalyticFunctionInfo>(sql);
  if (node->window_frame() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->window_frame()));
    analytic_function_info->set_window(node->distinct() ? ""
                                                        : result->GetSQL());
  }

  GOOGLESQL_RET_CHECK(pending_analytic_function_ == nullptr);
  pending_analytic_function_ = std::move(analytic_function_info);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAnalyticFunctionGroup(
    const ResolvedAnalyticFunctionGroup* node) {
  std::string partition_by;
  if (node->partition_by() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> partition_result,
                     ProcessNode(node->partition_by()));
    partition_by = partition_result->GetSQL();
  }

  std::string order_by;
  if (node->order_by() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> order_by_result,
                     ProcessNode(node->order_by()));
    order_by = order_by_result->GetSQL();
  }

  for (const auto& analytic_function : node->analytic_function_list()) {
    GOOGLESQL_RET_CHECK_EQ(analytic_function->expr()->node_kind(),
                 RESOLVED_ANALYTIC_FUNCTION_CALL);
    GOOGLESQL_RETURN_IF_ERROR(analytic_function->Accept(this));
    // We expect analytic_function->Accept(...) will store its corresponding
    // AnalyticFunctionInfo in pending_analytic_function_.
    GOOGLESQL_RET_CHECK(pending_analytic_function_ != nullptr);

    pending_analytic_function_->set_partition_by(partition_by);
    pending_analytic_function_->set_order_by(order_by);
    googlesql_base::InsertOrDie(&mutable_pending_columns(),
                     analytic_function->column().column_id(),
                     pending_analytic_function_->GetSQL());

    pending_analytic_function_.reset();
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedWindowPartitioning(
    const ResolvedWindowPartitioning* node) {
  std::vector<std::string> partition_by_list_sql;
  for (const auto& column_ref : node->partition_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(column_ref.get()));
    partition_by_list_sql.push_back(result->GetSQL());
  }

  std::string sql = "PARTITION";
  if (!node->hint_list().empty()) {
    absl::StrAppend(&sql, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
  }
  absl::StrAppend(&sql, " BY ", absl::StrJoin(partition_by_list_sql, ", "));

  // Mark field accessed. <collation_list> doesn't have its own SQL clause.
  node->collation_list();

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedWindowOrdering(
    const ResolvedWindowOrdering* node) {
  std::vector<std::string> order_by_list_sql;
  for (const auto& order_by_item : node->order_by_item_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(order_by_item.get()));
    order_by_list_sql.push_back(result->GetSQL());
  }

  std::string sql = "ORDER";
  if (!node->hint_list().empty()) {
    absl::StrAppend(&sql, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
  }
  absl::StrAppend(&sql, " BY ", absl::StrJoin(order_by_list_sql, ", "));

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedWindowFrame(
    const ResolvedWindowFrame* node) {
  const std::string frame_unit_sql =
      ResolvedWindowFrame::FrameUnitToString(node->frame_unit());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> start_expr_result,
                   ProcessNode(node->start_expr()));
  const std::string start_expr_sql = start_expr_result->GetSQL();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> end_expr_result,
                   ProcessNode(node->end_expr()));
  const std::string end_expr_sql = end_expr_result->GetSQL();

  PushQueryFragment(node, absl::StrCat(frame_unit_sql, " BETWEEN ",
                                       start_expr_sql, " AND ", end_expr_sql));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedWindowFrameExpr(
    const ResolvedWindowFrameExpr* node) {
  std::string sql;
  switch (node->boundary_type()) {
    case ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING: {
      GOOGLESQL_RET_CHECK(node->expression() == nullptr);
      absl::StrAppend(&sql, "UNBOUNDED PRECEDING");
      break;
    }
    case ResolvedWindowFrameExpr::OFFSET_PRECEDING: {
      GOOGLESQL_RET_CHECK(node->expression() != nullptr);
      // In case the offset is a casted expression as a result of parameter or
      // literal coercion, we remove the cast as only literals and parameters
      // are allowed in window frame expressions.
      const ResolvedExpr* expr = node->expression();
      if (expr->node_kind() == RESOLVED_CAST) {
        expr = expr->GetAs<ResolvedCast>()->expr();
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(expr));
      absl::StrAppend(&sql, result->GetSQL(), " PRECEDING");
      break;
    }
    case ResolvedWindowFrameExpr::CURRENT_ROW: {
      GOOGLESQL_RET_CHECK(node->expression() == nullptr);
      absl::StrAppend(&sql, "CURRENT ROW");
      break;
    }
    case ResolvedWindowFrameExpr::OFFSET_FOLLOWING: {
      GOOGLESQL_RET_CHECK(node->expression() != nullptr);
      const ResolvedExpr* expr = node->expression();
      if (expr->node_kind() == RESOLVED_CAST) {
        expr = expr->GetAs<ResolvedCast>()->expr();
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(expr));
      absl::StrAppend(&sql, result->GetSQL(), " FOLLOWING");
      break;
    }
    case ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING: {
      GOOGLESQL_RET_CHECK(node->expression() == nullptr);
      absl::StrAppend(&sql, "UNBOUNDED FOLLOWING");
    }
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

namespace {
// If 'field_descriptor' represents an extension field, appends its
// parenthesized full name to 'text'. Otherwise it is just a regular proto
// field, so we just append its name.
void AppendFieldOrParenthesizedExtensionName(
    const google::protobuf::FieldDescriptor* field_descriptor, std::string* text) {
  const std::string field_name = ToIdentifierLiteral(field_descriptor->name());
  if (!field_descriptor->is_extension()) {
    absl::StrAppend(text, field_name);
  } else {
    // If the extension is scoped within a message we generate a name of the
    // form (`package.ProtoName`.ExtensionField).
    // If the extension is a top level one we generate a name of the form
    // (package.ExtensionField), where package can be multiple possibly
    // quoted identifiers separated by dots if the package has multiple
    // levels.
    if (field_descriptor->extension_scope() != nullptr) {
      absl::StrAppend(
          text, "(",
          ToIdentifierLiteral(field_descriptor->extension_scope()->full_name()),
          ".", field_name, ")");
    } else {
      std::string package_prefix;
      absl::string_view package = field_descriptor->file()->package();
      if (!package.empty()) {
        for (const auto& package_name : absl::StrSplit(package, '.')) {
          absl::StrAppend(&package_prefix, ToIdentifierLiteral(package_name),
                          ".");
        }
      }
      absl::StrAppend(text, "(", package_prefix, field_name, ")");
    }
  }
}
}  // namespace

static bool IsAmbiguousFieldExtraction(
    const google::protobuf::FieldDescriptor& field_descriptor,
    const ResolvedExpr* resolved_parent, bool get_has_bit) {
  const google::protobuf::Descriptor* message_descriptor =
      resolved_parent->type()->AsProto()->descriptor();
  if (get_has_bit) {
    return ProtoType::FindFieldByNameIgnoreCase(
               message_descriptor,
               absl::StrCat("has_", field_descriptor.name())) != nullptr;
  } else {
    return absl::StartsWithIgnoreCase(field_descriptor.name(), "has_") &&
           ProtoType::FindFieldByNameIgnoreCase(
               message_descriptor, field_descriptor.name().substr(4)) !=
               nullptr;
  }
}

static absl::StatusOr<bool> IsRawFieldExtraction(
    const ResolvedGetProtoField* node) {
  GOOGLESQL_RET_CHECK(!node->get_has_bit());
  GOOGLESQL_RET_CHECK(node->expr()->type()->IsProto());
  const Type* type_with_annotations;
  TypeFactory type_factory;
  GOOGLESQL_RET_CHECK_OK(type_factory.GetProtoFieldType(
      node->field_descriptor(),
      node->expr()->type()->AsProto()->CatalogNamePath(),
      &type_with_annotations));
  // We know this is a RAW extraction if the field type, respecting annotations,
  // is different than the return type of this node or if the field is primitive
  // and is annotated with (googlesql.use_defaults = false), yet the default
  // value of this node is not null.
  return !type_with_annotations->Equals(node->type()) ||
         (node->type()->IsSimpleType() &&
          node->field_descriptor()->has_presence() &&
          !ProtoType::GetUseDefaultsExtension(node->field_descriptor()) &&
          node->default_value().is_valid() && !node->default_value().is_null());
}

absl::Status SQLBuilder::VisitResolvedGetProtoField(
    const ResolvedGetProtoField* node) {
  // Dummy access on the default_value and format and fields so as to pass the
  // final CheckFieldsAccessed() on a statement level before building the sql.
  node->default_value();
  node->format();

  std::string text;
  if (node->return_default_value_when_unset()) {
    absl::StrAppend(&text, "PROTO_DEFAULT_IF_NULL(");
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));
  const std::string result_sql = result->GetSQL();
  // When proto_expr is an empty identifier, we directly use the field_name to
  // access the proto field (in the generated sql). This shows up for in-scope
  // expression column fields where the expression column is anonymous.
  if (result_sql != kEmptyAlias) {
    absl::StrAppend(&text, result_sql, ".");
  }

  if (node->get_has_bit()) {
    if (result_sql == kEmptyAlias ||
        (!IsAmbiguousFieldExtraction(*node->field_descriptor(), node->expr(),
                                     node->get_has_bit()) &&
         !node->field_descriptor()->is_extension())) {
      absl::StrAppend(&text, ToIdentifierLiteral(absl::StrCat(
                                 "has_", node->field_descriptor()->name())));
    } else {
      std::string field_name;
      if (node->field_descriptor()->is_extension()) {
        field_name =
            absl::StrCat("(", node->field_descriptor()->full_name(), ")");
      } else {
        field_name = ToIdentifierLiteral(node->field_descriptor()->name());
      }
      text =
          absl::StrCat("EXTRACT(HAS(", field_name, ") FROM ", result_sql, ")");
    }
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_raw_extraction, IsRawFieldExtraction(node));
    std::string field_name;
    if (node->field_descriptor()->is_extension()) {
      field_name =
          absl::StrCat("(", node->field_descriptor()->full_name(), ")");
    } else {
      field_name = ToIdentifierLiteral(node->field_descriptor()->name());
    }
    if (is_raw_extraction) {
      text =
          absl::StrCat("EXTRACT(RAW(", field_name, ") FROM ", result_sql, ")");
    } else if (node->field_descriptor()->is_extension() ||
               result_sql == kEmptyAlias ||
               !IsAmbiguousFieldExtraction(*node->field_descriptor(),
                                           node->expr(), node->get_has_bit())) {
      AppendFieldOrParenthesizedExtensionName(node->field_descriptor(), &text);
    } else {
      text = absl::StrCat("EXTRACT(FIELD(", field_name, ") FROM ", result_sql,
                          ")");
    }
  }
  if (node->return_default_value_when_unset()) {
    absl::StrAppend(&text, ")");
  }

  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedFlatten(const ResolvedFlatten* node) {
  std::string text = "FLATTEN((";
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));
  absl::StrAppend(&text, result->GetSQL(), ")");
  for (const std::unique_ptr<const ResolvedExpr>& get_field :
       node->get_field_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> get_field_fragment,
                     ProcessNode(get_field.get()));
    absl::StrAppend(&text, get_field_fragment->GetSQL());
  }
  absl::StrAppend(&text, ")");
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedFlattenedArg(
    const ResolvedFlattenedArg* node) {
  // Does not add to generated SQL. Artifact to point at argument.
  PushQueryFragment(node, "");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedFilterField(
    const ResolvedFilterField* node) {
  std::string text;
  absl::StrAppend(&text, "FILTER_FIELDS(");
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> proto_expr,
                   ProcessNode(node->expr()));
  absl::StrAppend(&text, proto_expr->GetSQL(), ",");
  absl::StrAppend(
      &text, absl::StrJoin(
                 node->filter_field_arg_list(), ",",
                 [](std::string* out, const auto& arg) {
                   out->append(arg->include() ? "+" : "-");
                   out->append(absl::StrJoin(
                       arg->field_descriptor_path(), ".",
                       [](std::string* out, const google::protobuf::FieldDescriptor* fd) {
                         if (fd->is_extension()) {
                           out->append(absl::StrCat("(", fd->full_name(), ")"));
                         } else {
                           out->append(ToIdentifierLiteral(fd->name()));
                         }
                       }));
                 }));
  if (node->reset_cleared_required_fields()) {
    absl::StrAppend(&text, ", RESET_CLEARED_REQUIRED_FIELDS => True");
  }
  absl::StrAppend(&text, ")");
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedReplaceField(
    const ResolvedReplaceField* node) {
  std::string text;
  absl::StrAppend(&text, "REPLACE_FIELDS(");
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> proto_expr,
                   ProcessNode(node->expr()));
  absl::StrAppend(&text, proto_expr->GetSQL(), ",");
  std::string replace_field_item_sql;
  for (const std::unique_ptr<const ResolvedReplaceFieldItem>& replace_item :
       node->replace_field_item_list()) {
    if (!replace_field_item_sql.empty()) {
      absl::StrAppend(&replace_field_item_sql, ",");
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> modified_value,
                     ProcessNode(replace_item->expr()));
    absl::StrAppend(&replace_field_item_sql, modified_value->GetSQL(), " AS ");
    std::string field_path_sql;
    const StructType* current_struct_type = node->expr()->type()->AsStruct();
    for (const int field_index : replace_item->struct_index_path()) {
      if (!field_path_sql.empty()) {
        absl::StrAppend(&field_path_sql, ".");
      }
      GOOGLESQL_RET_CHECK_LT(field_index, current_struct_type->num_fields());
      absl::StrAppend(
          &field_path_sql,
          ToIdentifierLiteral(current_struct_type->field(field_index).name));
      current_struct_type =
          current_struct_type->field(field_index).type->AsStruct();
    }
    for (const google::protobuf::FieldDescriptor* field :
         replace_item->proto_field_path()) {
      if (!field_path_sql.empty()) {
        absl::StrAppend(&field_path_sql, ".");
      }
      if (field->is_extension()) {
        absl::StrAppend(&field_path_sql, "(");
      }
      absl::StrAppend(&field_path_sql,
                      field->is_extension()
                          ? field->full_name()
                          : ToIdentifierLiteral(field->name()));
      if (field->is_extension()) {
        absl::StrAppend(&field_path_sql, ")");
      }
    }
    absl::StrAppend(&replace_field_item_sql, field_path_sql);
  }
  absl::StrAppend(&text, replace_field_item_sql, ")");

  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedColumnRef(const ResolvedColumnRef* node) {
  const ResolvedColumn& column = node->column();
  if (googlesql_base::ContainsKey(pending_columns(), column.column_id())) {
    PushQueryFragment(node,
                      googlesql_base::FindOrDie(pending_columns(), column.column_id()));
  } else if (googlesql_base::ContainsKey(correlated_pending_columns(),
                              column.column_id())) {
    PushQueryFragment(
        node, googlesql_base::FindOrDie(correlated_pending_columns(), column.column_id()));
  } else if (state_.match_recognize_state.has_value() &&
             column == state_.match_recognize_state->match_number_column) {
    PushQueryFragment(node, "MATCH_NUMBER()");
  } else if (state_.match_recognize_state.has_value() &&
             column == state_.match_recognize_state->match_row_number_column) {
    PushQueryFragment(node, "MATCH_ROW_NUMBER()");
  } else if (state_.match_recognize_state.has_value() &&
             column == state_.match_recognize_state->classifier_column) {
    PushQueryFragment(node, "CLASSIFIER()");
  } else {
    PushQueryFragment(node, GetColumnPath(node->column()));
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCast(const ResolvedCast* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string type_name,
      node->type()->TypeNameWithModifiers(
          node->type_modifiers(), options_.language_options.product_mode(),
          options_.use_external_float32));
  if (TypeIsOrContainsGraphElement(node->type())) {
    // Cast to graph element or container types with graph elements
    // is not supported in SQL so drop the cast. This cast
    // will be automatically re-generated during SQL resolution.
    PushQueryFragment(node, result->GetSQL());
    return absl::OkStatus();
  }
  std::string format_clause;
  if (node->format() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> format,
                     ProcessNode(node->format()));
    format_clause = absl::StrCat(" FORMAT ", format->GetSQL());

    if (node->time_zone() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> time_zone,
                       ProcessNode(node->time_zone()));
      absl::StrAppend(&format_clause, " AT TIME ZONE ", time_zone->GetSQL());
    }
  }

  PushQueryFragment(
      node,
      absl::StrCat(node->return_null_on_error() ? "SAFE_CAST(" : "CAST(",
                   result->GetSQL(), " AS ", type_name, format_clause, ")"));
  return absl::OkStatus();
}

SQLBuilder::PendingColumnsAutoRestorer::PendingColumnsAutoRestorer(
    SQLBuilder& sql_builder,
    const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
        columns_to_expose)
    : sql_builder_(sql_builder) {
  previous_pending_columns_.swap(sql_builder.mutable_pending_columns());
  previous_correlated_columns_.swap(
      sql_builder.mutable_correlated_pending_columns());

  for (const auto& parameter : columns_to_expose) {
    auto pending_column =
        previous_pending_columns_.find(parameter->column().column_id());
    if (pending_column != previous_pending_columns_.end()) {
      sql_builder.mutable_correlated_pending_columns().insert(*pending_column);
      continue;
    }

    auto correlated_column =
        previous_correlated_columns_.find(parameter->column().column_id());
    if (correlated_column != previous_correlated_columns_.end()) {
      sql_builder.mutable_correlated_pending_columns().insert(
          *correlated_column);
      continue;
    }
  }
}

SQLBuilder::PendingColumnsAutoRestorer::~PendingColumnsAutoRestorer() {
  previous_pending_columns_.swap(sql_builder_.mutable_pending_columns());
  previous_correlated_columns_.swap(
      sql_builder_.mutable_correlated_pending_columns());
}

absl::Status SQLBuilder::VisitResolvedSubqueryExpr(
    const ResolvedSubqueryExpr* node) {
  std::string text;
  switch (node->subquery_type()) {
    case ResolvedSubqueryExpr::SCALAR:
      break;
    case ResolvedSubqueryExpr::ARRAY:
      absl::StrAppend(&text, "ARRAY");
      break;
    case ResolvedSubqueryExpr::EXISTS:
      absl::StrAppend(&text, "EXISTS");
      break;
    case ResolvedSubqueryExpr::IN: {
      GOOGLESQL_RET_CHECK(node->in_expr() != nullptr)
          << "ResolvedSubqueryExpr of IN type does not have an associated "
             "in_expr:\n"
          << node->DebugString();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(node->in_expr()));
      absl::StrAppend(&text, "((", result->GetSQL(), ") IN");
      break;
    }
    case ResolvedSubqueryExpr::LIKE_ANY: {
      GOOGLESQL_RET_CHECK(node->in_expr() != nullptr)
          << "ResolvedSubqueryExpr of LIKE ANY|SOME type does not have an "
             "associated left hand side expression (in_expr):\n"
          << node->DebugString();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> like_result,
                       ProcessNode(node->in_expr()));
      absl::StrAppend(&text, "((", like_result->GetSQL(), ") LIKE ANY");
      break;
    }
    case ResolvedSubqueryExpr::LIKE_ALL: {
      GOOGLESQL_RET_CHECK(node->in_expr() != nullptr)
          << "ResolvedSubqueryExpr of LIKE ALL type does not have an "
             "associated left hand side expression (in_expr):\n"
          << node->DebugString();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> like_result,
                       ProcessNode(node->in_expr()));
      absl::StrAppend(&text, "((", like_result->GetSQL(), ") LIKE ALL");
      break;
    }
    case ResolvedSubqueryExpr::NOT_LIKE_ANY: {
      GOOGLESQL_RET_CHECK(node->in_expr() != nullptr)
          << "ResolvedSubqueryExpr of NOT LIKE ANY type does not have an "
             "associated left hand side expression (in_expr):\n"
          << node->DebugString();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> like_result,
                       ProcessNode(node->in_expr()));
      absl::StrAppend(&text, "((", like_result->GetSQL(), ") NOT LIKE ANY");
      break;
    }
    case ResolvedSubqueryExpr::NOT_LIKE_ALL: {
      GOOGLESQL_RET_CHECK(node->in_expr() != nullptr)
          << "ResolvedSubqueryExpr of NOT LIKE ALL type does not have an "
             "associated left hand side expression (in_expr):\n"
          << node->DebugString();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> like_result,
                       ProcessNode(node->in_expr()));
      absl::StrAppend(&text, "((", like_result->GetSQL(), ") NOT LIKE ALL");
      break;
    }
  }

  // NOTE: Swapping pending_columns_ must happen after resolution of in_expr
  // which may reference current pending columns as uncorrelated.
  PendingColumnsAutoRestorer column_restorer(*this, node->parameter_list());

  // Mark field accessed. <in_collation> doesn't have its own SQL clause.
  node->in_collation();

  {
    std::string gql_subquery;
    GOOGLESQL_RETURN_IF_ERROR(ProcessGqlSubquery(node, gql_subquery));
    if (!gql_subquery.empty()) {
      absl::StrAppend(&text, gql_subquery,
                      node->in_expr() == nullptr ? "" : ")");
      PushQueryFragment(node, text);
      return absl::OkStatus();
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->subquery()));
  std::unique_ptr<QueryExpression> subquery_result(
      result->query_expression.release());
  GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(node->subquery()->column_list(),
                                        subquery_result.get()));
  absl::StrAppend(&text, "(", subquery_result->GetSQLQuery(), ")",
                  node->in_expr() == nullptr ? "" : ")");

  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedWithExpr(const ResolvedWithExpr* node) {
  if (options_.language_options.LanguageFeatureEnabled(
          FEATURE_WITH_EXPRESSION)) {
    // Let expressions when WITH expression is enabled need to be unparsed
    // differently. WITH expressions have a select list that can re-reference
    // elements earlier in the select list. That means we cannot rely on the
    // standard computed column alias behavior. The standard behavior is to
    // introduce new aliases when columns appear multiple times in the select
    // list, which will lead to incorrect unparsings. For example, the following
    // unparsing would occur with standard alias handling rules:
    //
    // WITH(a AS 2, b AS a + 2, a + b) =>
    // WITH(a_1 AS 2, a_2 AS a_3 + 2, a_1 + a_2)
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                     ProcessNode(node->expr()));
    std::vector<std::string> assignments;
    for (int i = 0; i < node->assignment_list_size(); ++i) {
      const ResolvedColumn& col = node->assignment_list(i)->column();
      std::string alias = GetColumnAlias(col);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> assignment,
                       ProcessNode(node->assignment_list(i)));
      assignments.push_back(absl::StrCat(alias, " AS ", assignment->GetSQL()));
    }
    std::string sql;
    sql = absl::Substitute("WITH($0, $1)", absl::StrJoin(assignments, ", "),
                           expr->GetSQL());
    PushQueryFragment(node, sql);
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                   ProcessNode(node->expr()));
  std::vector<std::string> column_aliases;
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  for (int i = 0; i < node->assignment_list_size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> assignment,
                     ProcessNode(node->assignment_list(i)));
    const ResolvedColumn& col = node->assignment_list(i)->column();
    std::string column_alias = googlesql_base::FindWithDefault(
        computed_column_alias(), col.column_id(), col.name());
    if (i == 0) {
      GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(
          {{assignment->GetSQL(), column_alias}}, ""));

    } else {
      query_expression->Wrap(MakeNonconflictingAlias(column_aliases.back()));
      GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(
          {{assignment->GetSQL(), column_alias}}, ""));
      for (const auto& prev_column_alias : column_aliases) {
        query_expression->AppendSelectColumn(prev_column_alias, "");
      }
    }
    column_aliases.push_back(std::move(column_alias));
  }
  query_expression->Wrap(MakeNonconflictingAlias(column_aliases.back()));
  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause({{expr->GetSQL(), ""}}, ""));

  PushQueryFragment(node,
                    absl::StrCat("(", query_expression->GetSQLQuery(), ")"));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedTableAndColumnInfo(
    const ResolvedTableAndColumnInfo* node) {
  std::string sql;
  absl::StrAppend(&sql, TableToIdentifierLiteral(node->table()));
  std::vector<std::string> column_name_list;
  column_name_list.reserve(node->column_index_list().size());
  for (const int column_index : node->column_index_list()) {
    column_name_list.push_back(node->table()->GetColumn(column_index)->Name());
  }
  if (!column_name_list.empty()) {
    absl::StrAppend(&sql, " (", absl::StrJoin(column_name_list, ","), ") ");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendGeneratedColumnInfo(
    const ResolvedGeneratedColumnInfo* generated_column_info,
    std::string* text) {
  switch (generated_column_info->generated_mode()) {
    case ASTGeneratedColumnInfo::ALWAYS:
      absl::StrAppend(text, " ALWAYS");
      break;
    case ASTGeneratedColumnInfo::BY_DEFAULT:
      absl::StrAppend(text, " BY DEFAULT");
      break;
  }

  // If the generated column is an identity column.
  if (const ResolvedIdentityColumnInfo* identity_col =
          generated_column_info->identity_column_info();
      identity_col != nullptr) {
    GOOGLESQL_RET_CHECK(generated_column_info->expression() == nullptr);
    absl::StrAppend(text, " AS IDENTITY(");

    if (!identity_col->start_with_value().is_null()) {
      absl::StrAppend(text, "START WITH ",
                      identity_col->start_with_value().DebugString());
    }
    if (!identity_col->increment_by_value().is_null()) {
      absl::StrAppend(text, " INCREMENT BY ",
                      identity_col->increment_by_value().DebugString());
    }
    if (!identity_col->max_value().is_null()) {
      absl::StrAppend(text, " MAXVALUE ",
                      identity_col->max_value().DebugString());
    }
    if (!identity_col->min_value().is_null()) {
      absl::StrAppend(text, " MINVALUE ",
                      identity_col->min_value().DebugString());
    }
    identity_col->cycling_enabled() ? absl::StrAppend(text, " CYCLE)")
                                    : absl::StrAppend(text, " NO CYCLE)");
  } else {
    // If the generated column is an expression.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(generated_column_info->expression()));

    absl::StrAppend(text, " AS (", result->GetSQL(), ")");

    switch (generated_column_info->stored_mode()) {
      case ASTGeneratedColumnInfo::NON_STORED:
        break;
      case ASTGeneratedColumnInfo::STORED:
        absl::StrAppend(text, " STORED");
        break;
      case ASTGeneratedColumnInfo::STORED_VOLATILE:
        absl::StrAppend(text, " STORED VOLATILE");
        break;
    }
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendColumnSchema(
    const Type* type, bool is_hidden,
    const ResolvedColumnAnnotations* annotations,
    const ResolvedGeneratedColumnInfo* generated_column_info,
    const ResolvedColumnDefaultValue* default_value, std::string* text) {
  GOOGLESQL_RET_CHECK(text != nullptr);
  if (type != nullptr) {
    if (type->IsStruct()) {
      const StructType* struct_type = type->AsStruct();
      absl::StrAppend(text, "STRUCT<");
      for (int i = 0; i < struct_type->num_fields(); ++i) {
        const StructField& field = struct_type->field(i);
        if (i != 0) absl::StrAppend(text, ", ");
        if (!field.name.empty()) {
          absl::StrAppend(text, ToIdentifierLiteral(field.name), " ");
        }
        const ResolvedColumnAnnotations* child_annotations =
            annotations != nullptr && i < annotations->child_list_size()
                ? annotations->child_list(i)
                : nullptr;
        GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(field.type, /*is_hidden=*/false,
                                           child_annotations,
                                           /*generated_column_info=*/nullptr,
                                           /*default_value=*/nullptr, text));
      }
      absl::StrAppend(text, ">");
    } else if (type->IsArray() || type->IsRange()) {
      absl::StrAppend(text, type->IsRange() ? "RANGE" : "ARRAY");
      absl::StrAppend(text, "<");
      const ResolvedColumnAnnotations* child_annotations =
          annotations != nullptr && !annotations->child_list().empty()
              ? annotations->child_list(0)
              : nullptr;
      auto component_types = type->ComponentTypes();
      GOOGLESQL_RET_CHECK_EQ(component_types.size(), 1);
      GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(component_types[0],
                                         /*is_hidden=*/false, child_annotations,
                                         /*generated_column_info=*/nullptr,
                                         /*default_value=*/nullptr, text));
      absl::StrAppend(text, ">");
    } else if (type->IsMap()) {
      GOOGLESQL_RET_CHECK(annotations == nullptr || annotations->child_list_size() == 0 ||
                annotations->child_list_size() == 2)
          << "Annotations for MAP is an invalid shape. Expected annotations to "
             "be either: nullptr (no annotations set), a node with zero "
             "children (annotations set only on the MAP itself), or a node "
             "with two children (annotations set on the MAP key and value "
             "types). Got: "
          << annotations->DebugString();

      bool has_child_annotations =
          annotations != nullptr && annotations->child_list_size() == 2;
      const ResolvedColumnAnnotations* key_annotations =
          has_child_annotations ? annotations->child_list(0) : nullptr;
      const ResolvedColumnAnnotations* value_annotations =
          has_child_annotations ? annotations->child_list(1) : nullptr;

      const Type* key_type = type->AsMap()->key_type();
      const Type* value_type = type->AsMap()->value_type();

      absl::StrAppend(text, "MAP<");
      GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(key_type, /*is_hidden=*/false,
                                         key_annotations,
                                         /*generated_column_info=*/nullptr,
                                         /*default_value=*/nullptr, text));
      absl::StrAppend(text, ", ");
      GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(value_type, /*is_hidden=*/false,
                                         value_annotations,
                                         /*generated_column_info=*/nullptr,
                                         /*default_value=*/nullptr, text));
      absl::StrAppend(text, ">");
    } else {
      if (annotations != nullptr && !annotations->type_parameters().IsEmpty()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::string typename_with_parameters,
            type->TypeNameWithModifiers(
                TypeModifiers::MakeTypeModifiers(annotations->type_parameters(),
                                                 googlesql::Collation()),
                options_.language_options.product_mode(),
                options_.use_external_float32));
        absl::StrAppend(text, typename_with_parameters);
      } else {
        absl::StrAppend(text,
                        type->TypeName(options_.language_options.product_mode(),
                                       options_.use_external_float32));
      }
      if (annotations != nullptr && annotations->collation_name() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> collation,
                         ProcessNode(annotations->collation_name()));
        absl::StrAppend(text, " COLLATE ", collation->GetSQL());
      }
    }
  }
  if (generated_column_info != nullptr) {
    absl::StrAppend(text, " GENERATED");
    GOOGLESQL_RETURN_IF_ERROR(AppendGeneratedColumnInfo(generated_column_info, text));
  }
  if (default_value != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(default_value->expression()));
    absl::StrAppend(text, " DEFAULT ", result->GetSQL());
    GOOGLESQL_RET_CHECK(!default_value->sql().empty());  // Mark sql as accessed.
  }
  if (is_hidden) {
    absl::StrAppend(text, " HIDDEN");
  }
  if (annotations != nullptr) {
    if (annotations->not_null()) {
      absl::StrAppend(text, " NOT NULL");
    }
    if (!annotations->option_list().empty()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string col_options_string,
                       GetHintListString(annotations->option_list()));
      absl::StrAppend(text, " OPTIONS(", col_options_string, ")");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::GetHintListString(
    absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list) {
  if (hint_list.empty()) {
    return std::string() /* no hints */;
  }

  std::vector<std::string> hint_list_sql;
  for (const auto& hint : hint_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(hint.get()));
    hint_list_sql.push_back(result->GetSQL());
  }

  return absl::StrJoin(hint_list_sql, ", ");
}

absl::Status SQLBuilder::AppendOptions(
    absl::Span<const std::unique_ptr<const ResolvedOption>> option_list,
    std::string* sql) {
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                   GetHintListString(option_list));
  absl::StrAppend(sql, " OPTIONS(", options_string, ")");
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendOptionsIfPresent(
    absl::Span<const std::unique_ptr<const ResolvedOption>> option_list,
    std::string* sql) {
  if (!option_list.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AppendOptions(option_list, sql));
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendHintsIfPresent(
    absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list,
    std::string* text) {
  GOOGLESQL_RET_CHECK(text != nullptr);
  if (!hint_list.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result, GetHintListString(hint_list));
    absl::StrAppend(text, "@{ ", result, " }");
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedOption(const ResolvedOption* node) {
  std::string text;
  if (!node->qualifier().empty()) {
    absl::StrAppend(&text, ToIdentifierLiteral(node->qualifier()), ".");
  }
  std::string assignment_operator_string;
  switch (node->assignment_op()) {
    case ResolvedOptionEnums::ADD_ASSIGN:
      assignment_operator_string = "+=";
      break;
    case ResolvedOptionEnums::SUB_ASSIGN:
      assignment_operator_string = "-=";
      break;
    default:
      assignment_operator_string = "=";
      break;
  }
  absl::StrAppend(&text, ToIdentifierLiteral(node->name()),
                  assignment_operator_string);

  // If we have a CAST, strip it off and just print the value.  CASTs are
  // not allowed as part of the hint syntax so they must have been added
  // implicitly.
  const ResolvedExpr* value_expr = node->value();
  if (value_expr->node_kind() == RESOLVED_CAST) {
    value_expr = value_expr->GetAs<ResolvedCast>()->expr();
  }

  if (value_expr->node_kind() == RESOLVED_LITERAL) {
    const ResolvedLiteral* literal = value_expr->GetAs<ResolvedLiteral>();
    GOOGLESQL_ASSIGN_OR_RETURN(
        const std::string result,
        GetSQL(literal->value(), options_.language_options.product_mode(),
               options_.use_external_float32, true /* is_constant_value */));
    absl::StrAppend(&text, result);
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(value_expr));

    // Wrap the result sql in parentheses so that, when the generated text is
    // parsed, identifier expressions get treated as identifiers, rather than
    // string literals.
    absl::StrAppend(&text, "(", result->GetSQL(), ")");
  }
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedSystemVariable(
    const ResolvedSystemVariable* node) {
  std::string full_name = absl::StrJoin(
      node->name_path(), ".", [](std::string* out, absl::string_view input) {
        absl::StrAppend(out, ToIdentifierLiteral(input));
      });
  PushQueryFragment(node, absl::StrCat("@@", full_name));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedParameter(const ResolvedParameter* node) {
  if (node->name().empty()) {
    std::string param_str;
    switch (options_.positional_parameter_mode) {
      case SQLBuilderOptions::kQuestionMark:
        param_str = "?";
        break;
      case SQLBuilderOptions::kNamed:
        param_str = absl::StrCat("@param", node->position());
        break;
    }
    if (node->position() - 1 <
        options_.undeclared_positional_parameters.size()) {
      GOOGLESQL_ASSIGN_OR_RETURN(param_str,
                       AddExplicitCast(param_str, node->type(), TypeModifiers(),
                                       options_.language_options.product_mode(),
                                       options_.use_external_float32));
    }
    PushQueryFragment(node, param_str);
  } else {
    std::string param_str =
        absl::StrCat("@", ToIdentifierLiteral(node->name()));
    if (googlesql_base::ContainsKey(options_.undeclared_parameters, node->name())) {
      GOOGLESQL_ASSIGN_OR_RETURN(param_str,
                       AddExplicitCast(param_str, node->type(), TypeModifiers(),
                                       options_.language_options.product_mode(),
                                       options_.use_external_float32));
    }
    PushQueryFragment(node, param_str);
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedMakeProto(const ResolvedMakeProto* node) {
  const std::string& proto_name = node->type()->AsProto()->TypeName();

  std::string text;
  absl::StrAppend(&text, "NEW ", proto_name, "(");
  bool first = true;
  for (const auto& field : node->field_list()) {
    if (!first) absl::StrAppend(&text, ", ");
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(field.get()));
    absl::StrAppend(&text, result->GetSQL());
    first = false;
  }
  absl::StrAppend(&text, ")");

  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedMakeProtoField(
    const ResolvedMakeProtoField* node) {
  // Dummy access on the format field so as to pass the final
  // CheckFieldsAccessed() on a statement level, before building the sql.
  node->format();

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));

  std::string text = result->GetSQL();
  absl::StrAppend(&text, " AS ");
  AppendFieldOrParenthesizedExtensionName(node->field_descriptor(), &text);

  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedMakeStruct(
    const ResolvedMakeStruct* node) {
  const StructType* struct_type = node->type()->AsStruct();
  std::string text;
  absl::StrAppend(
      &text,
      struct_type->TypeName(options_.language_options.product_mode(),
                            options_.use_external_float32),
      "(");
  if (struct_type->num_fields() != node->field_list_size()) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Number of fields of ResolvedMakeStruct and its corresponding "
              "StructType, do not match\n:"
           << node->DebugString() << "\nStructType:\n"
           << struct_type->DebugString();
  }
  for (int i = 0; i < struct_type->num_fields(); ++i) {
    if (i > 0) absl::StrAppend(&text, ", ");
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->field_list(i)));
    absl::StrAppend(&text, result->GetSQL());
  }
  absl::StrAppend(&text, ")");
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGetStructField(
    const ResolvedGetStructField* node) {
  GOOGLESQL_RET_CHECK(node->expr()->type()->IsStruct());
  std::string text;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));
  std::string result_sql = result->GetSQL();

  if (node->field_expr_is_positional()) {
    absl::StrAppend(&text, result_sql, "[OFFSET(", node->field_idx(), ")]");
    PushQueryFragment(node, text);
    return absl::OkStatus();
  }

  // When struct_expr is an empty identifier, we directly use the field_name to
  // access the struct field (in the generated sql). This shows up for in-scope
  // expression column fields where the expression column is anonymous.
  //
  // Parentheses are similarly necessary for system variables so that (@@a).b
  // gets reparsed as field b of system variable "@@a", not system variable
  // "@@a.b".
  //
  // There is one exception - if a system variable is the LHS of a SET
  // statement, we do NOT insert the parentheses.  Since the grammar does not
  // allow parentheses around expressions in the LHS of a SET statement, this
  // is not only necessary to allow the generated SQL to parse, but also safe -
  // if system variables "@@a" and "@@a.b" both exist, it is not possible to
  // assign to field b of system variable "@@a".
  //
  const StructType* struct_type = node->expr()->type()->AsStruct();
  std::string field_alias = struct_type->field(node->field_idx()).name;
  if (result_sql != kEmptyAlias) {
    if (node->expr()->node_kind() == RESOLVED_CONSTANT ||
        (node->expr()->node_kind() == RESOLVED_SYSTEM_VARIABLE &&
         !state_.in_set_lhs)) {
      // Enclose <result_sql> in parentheses to ensure that "(foo).bar"
      // (accessing field bar of constant foo) does not get unparsed as
      // "foo.bar" (accessing named constant bar in namespace foo).
      result_sql = absl::StrCat("(", result_sql, ")");
    }

    bool is_ambiguous;
    // FindField returns nullptr for unnamed fields.
    if (struct_type->FindField(field_alias, &is_ambiguous) == nullptr ||
        is_ambiguous) {
      // Today there is no way to directly refer to a field by position in SQL.
      // Instead we cast the original STRUCT to the STRUCT of same SCHEMA type,
      // giving the nameless referenced field a name. Then this name used right
      // here.
      result_sql = absl::StrCat("CAST(", result_sql, " AS STRUCT<");
      for (int field_idx = 0; field_idx < struct_type->num_fields();
           ++field_idx) {
        if (field_idx > 0) {
          absl::StrAppend(&result_sql, ", ");
        }
        if (field_idx == node->field_idx()) {
          field_alias = "field_ref";
          absl::StrAppend(&result_sql, field_alias, " ");
        }
        GOOGLESQL_RETURN_IF_ERROR(
            AppendColumnSchema(struct_type->field(field_idx).type,
                               /*is_hidden=*/false, /*annotations=*/nullptr,
                               /*generated_column_info=*/nullptr,
                               /*default_value=*/nullptr, &result_sql));
      }
      GOOGLESQL_RET_CHECK_NE(field_alias, "");
      absl::StrAppend(&result_sql, ">)");
    }
    absl::StrAppend(&text, result_sql, ".");
  }
  absl::StrAppend(&text, ToIdentifierLiteral(field_alias));
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGetJsonField(
    const ResolvedGetJsonField* node) {
  GOOGLESQL_RET_CHECK(node->type()->IsJson());

  std::string text;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));
  std::string result_sql = result->GetSQL();

  // When expr is an empty identifier, we directly use the field_name to
  // access the json field (in the generated sql). This shows up when
  // we access a json field after an array subscript such as:
  // UPDATE T SET json_value["a"].b = 1 WHERE true;
  const std::string& field_name = node->field_name();
  if (result_sql != kEmptyAlias) {
    absl::StrAppend(&text, result_sql, ".");
  }
  absl::StrAppend(&text, ToIdentifierLiteral(field_name));
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGetRowField(
    const ResolvedGetRowField* node) {
  GOOGLESQL_RET_CHECK(node->expr()->type()->IsRow()) << node->type()->DebugString();

  std::string text;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));

  std::string sql = absl::StrCat(result->GetSQL(), ".",
                                 ToIdentifierLiteral(node->column()->Name()));

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::WrapQueryExpression(
    const ResolvedScan* node, QueryExpression* query_expression) {
  const std::string alias = GetScanAlias(node);
  GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(node->column_list(), query_expression));
  query_expression->Wrap(alias);
  SetPathForColumnList(node->column_list(), alias);
  return absl::OkStatus();
}

absl::Status SQLBuilder::GetSelectList(
    const ResolvedColumnList& column_list,
    const std::map<int64_t, const ResolvedExpr*>& col_to_expr_map,
    const ResolvedScan* parent_scan, QueryExpression* query_expression,
    SQLAliasPairList* select_list) {
  ABSL_DCHECK(select_list != nullptr);

  std::set<int /* column_id */> has_alias;
  std::set<std::string> seen_aliases;
  for (int i = 0; i < column_list.size(); ++i) {
    const ResolvedColumn& col = column_list[i];
    std::string alias;
    // When assigning select_alias to column which occurs in column_list more
    // than once:
    //  - We assign unique alias to each of those occurrences.
    //  - Only one of those aliases is registered to refer to that column later
    //    in the query. See GetColumnAlias().
    // This avoids ambiguity in selecting that column outside the subquery
    if (googlesql_base::ContainsKey(has_alias, col.column_id())) {
      alias = GenerateUniqueAliasName();
    } else {
      alias = GetColumnAlias(col);
      // This alias has already been used in the SELECT, so create a new one.
      if (googlesql_base::ContainsKey(seen_aliases, alias)) {
        alias = UpdateColumnAlias(col);
      }
      has_alias.insert(col.column_id());
    }
    seen_aliases.insert(alias);

    if (googlesql_base::ContainsKey(col_to_expr_map, col.column_id())) {
      const ResolvedExpr* expr =
          googlesql_base::FindOrDie(col_to_expr_map, col.column_id());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(expr));
      select_list->push_back(std::make_pair(result->GetSQL(), alias));
    } else {
      if (query_expression->HasGroupByColumn(col.column_id())) {
        select_list->push_back(std::make_pair(
            query_expression->GetGroupByColumnOrDie(col.column_id()), alias));
      } else if (googlesql_base::ContainsKey(pending_columns(), col.column_id())) {
        select_list->push_back(std::make_pair(
            googlesql_base::FindOrDie(pending_columns(), col.column_id()), alias));
      } else if (googlesql_base::ContainsKey(correlated_pending_columns(),
                                  col.column_id())) {
        select_list->push_back(std::make_pair(
            googlesql_base::FindOrDie(correlated_pending_columns(), col.column_id()),
            alias));
      } else {
        select_list->push_back(std::make_pair(GetColumnPath(col), alias));
      }
    }
  }
  mutable_pending_columns().clear();

  // If any group_by column is computed in the select list, then update the sql
  // text for those columns in group_by_list inside the QueryExpression,
  // reflecting the ordinal position of select clause.
  for (int pos = 0; pos < column_list.size(); ++pos) {
    const ResolvedColumn& col = column_list[pos];
    if (query_expression->HasGroupByColumn(col.column_id())) {
      query_expression->SetGroupByColumn(
          col.column_id(),
          absl::StrCat(pos + 1) /* select list ordinal position */);
    }
  }

  if (column_list.empty()) {
    GOOGLESQL_RET_CHECK_EQ(select_list->size(), 0);
    // Add a dummy value "NULL" to the select list if the column list is empty.
    // This ensures that we form a valid query for scans with empty columns.
    const std::string alias =
        IsPipeSyntaxTargetMode() && query_expression->HasAnyGroupByColumn()
            // In presence of group by columns, an alias in needed in Pipe
            // syntax mode to form a syntactically correct query.
            ? GenerateUniqueAliasName()
            : "" /* no select alias */;
    select_list->push_back(std::make_pair("NULL", alias));
  }

  return absl::OkStatus();
}

absl::Status SQLBuilder::GetSelectList(const ResolvedColumnList& column_list,
                                       QueryExpression* query_expression,
                                       SQLAliasPairList* select_list) {
  return GetSelectList(column_list, {} /* empty col_to_expr_map */,
                       nullptr /* parent_scan */, query_expression,
                       select_list);
}

absl::Status SQLBuilder::AddSelectListIfNeeded(
    const ResolvedColumnList& column_list, QueryExpression* query_expression) {
  if (!query_expression->CanFormSQLQuery()) {
    SQLAliasPairList select_list;
    GOOGLESQL_RETURN_IF_ERROR(GetSelectList(column_list, query_expression, &select_list));
    GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(select_list,
                                                   "" /* no select hints */));
  }
  return absl::OkStatus();
}

bool SQLBuilder::CanTableBeUsedWithImplicitAlias(const Table* table) {
  std::string table_identifier = TableToIdentifierLiteral(table);
  auto it = tables_with_implicit_alias().find(table->Name());
  if (it == tables_with_implicit_alias().end()) {
    mutable_tables_with_implicit_alias()[table->Name()] = table_identifier;
    return true;
  } else {
    return it->second == table_identifier;
  }
}

absl::Status SQLBuilder::VisitResolvedTableScan(const ResolvedTableScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  std::string from;
  absl::StrAppend(&from, TableToIdentifierLiteral(node->table()));
  if (node->hint_list_size() > 0) {
    absl::StrAppend(&from, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &from));
  }
  // To avoid name resolution conflicts between a table and a column that have
  // the same name.
  std::string table_alias =
      GetTableAliasForVisitResolvedTableScan(*node, &from);
  if (node->for_system_time_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->for_system_time_expr()));
    absl::StrAppend(&from, " FOR SYSTEM_TIME AS OF ", result->GetSQL());
  }
  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));

  SQLAliasPairList select_list;
  if (node->read_as_row_type()) {
    GOOGLESQL_RET_CHECK_LE(node->column_list().size(), 1);
    if (node->column_list().size() == 1) {
      const ResolvedColumn& column = node->column_list(0);
      GOOGLESQL_RETURN_IF_ERROR(AddValueTableAliasForVisitResolvedTableScan(
          table_alias, column, &select_list));
    }
  } else {
    // column_index_list should be preferred, but is not always available
    // (if not set after a ResolvedTableScan is created).
    // TODO: Remove the case use_column_index_list=false
    const bool use_column_index_list =
        node->column_index_list_size() == node->column_list_size();
    const bool use_value_table =
        node->table()->IsValueTable() && use_column_index_list;
    for (int i = 0; i < node->column_list_size(); ++i) {
      const ResolvedColumn& column = node->column_list(i);
      if (use_value_table && node->column_index_list(i) == 0) {
        GOOGLESQL_RETURN_IF_ERROR(AddValueTableAliasForVisitResolvedTableScan(
            table_alias, column, &select_list));
      } else {
        std::string column_name;
        if (use_column_index_list) {
          const Column* table_column =
              node->table()->GetColumn(node->column_index_list(i));
          GOOGLESQL_RET_CHECK(table_column != nullptr);
          column_name = table_column->Name();
        } else {
          column_name = column.name();
        }
        select_list.push_back(std::make_pair(
            absl::StrCat(table_alias, ".", ToIdentifierLiteral(column_name)),
            GetColumnAlias(column)));
      }
    }
  }
  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(select_list, ""));

  if (node->lock_mode() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->lock_mode()));
    GOOGLESQL_RET_CHECK(query_expression->TrySetLockModeClause(result->GetSQL()));
  }

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedSingleRowScan(
    const ResolvedSingleRowScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  // We generate a dummy subquery "select 1" for SingleRowScan, so that we
  // can build valid sql for scans having SinleRowScan as input.
  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(
      {std::make_pair("1", "" /* no select_alias */)}, "" /* no hints */));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUnsetArgumentScan(
    const ResolvedUnsetArgumentScan* node) {
  GOOGLESQL_RET_CHECK_FAIL() << "UnsetArgumentScan represents an unset argument and "
                      "is never printed. The parent node should handle the "
                      "omission of this node properly.";
  return absl::OkStatus();
}

// Returns true if:
// The 2 expressions are both ResolvedParameterRefs to the same *positional*
// parameter.
// It returns false for any other expressions, including CAST(ref1).
static bool AreRefsToSamePositionalParameter(const ResolvedExpr* expr1,
                                             const ResolvedExpr* expr2) {
  if (!expr1->Is<ResolvedParameter>() || !expr2->Is<ResolvedParameter>()) {
    return false;
  }

  const ResolvedParameter* param1 = expr1->GetAs<ResolvedParameter>();
  const ResolvedParameter* param2 = expr2->GetAs<ResolvedParameter>();
  if (param1->position() == 0 || param2->position() == 0) {
    // One of the two isn't a positional parameter.
    return false;
  }

  return param1->position() == param2->position();
}

absl::StatusOr<std::string> SQLBuilder::GeneratePatternExpression(
    const ResolvedMatchRecognizePatternExpr* node) {
  switch (node->node_kind()) {
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_EMPTY: {
      return "()";
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_ANCHOR: {
      auto mode = node->GetAs<ResolvedMatchRecognizePatternAnchor>()->mode();
      switch (mode) {
        case ResolvedMatchRecognizePatternAnchorEnums::START:
          return "(^)";
        case ResolvedMatchRecognizePatternAnchorEnums::END:
          return "($)";
        default:
          GOOGLESQL_RET_CHECK_FAIL()
              << "Unexpected special mode: "
              << ResolvedMatchRecognizePatternAnchorEnums::Mode_Name(mode);
      }
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_VARIABLE_REF: {
      return ToIdentifierLiteral(
          node->GetAs<ResolvedMatchRecognizePatternVariableRef>()->name());
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_OPERATION: {
      const auto* op = node->GetAs<ResolvedMatchRecognizePatternOperation>();
      GOOGLESQL_RET_CHECK_GE(op->operand_list_size(), 2);
      absl::string_view separator = " ";
      switch (op->op_type()) {
        case ResolvedMatchRecognizePatternOperation::CONCAT:
          separator = " ";
          break;
        case ResolvedMatchRecognizePatternOperation::ALTERNATE:
          separator = " | ";
          break;
        default:
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected op type: " << op->op_type();
      }

      // Always parenthesize the expression
      std::string pattern_expr = "(";
      std::vector<std::string> operands;
      operands.reserve(op->operand_list_size());
      for (const auto& arg : op->operand_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::string operand,
                         GeneratePatternExpression(arg.get()));
        operands.push_back(std::move(operand));
      }
      absl::StrAppend(&pattern_expr,
                      absl::StrJoin(std::move(operands), separator));
      absl::StrAppend(&pattern_expr, ")");
      return pattern_expr;
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_QUANTIFICATION: {
      const auto* quantification =
          node->GetAs<ResolvedMatchRecognizePatternQuantification>();
      GOOGLESQL_ASSIGN_OR_RETURN(std::string operand,
                       GeneratePatternExpression(quantification->operand()));
      std::string quantifier = "{";
      GOOGLESQL_RET_CHECK(quantification->lower_bound() != nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> lower_bound,
                       ProcessNode(quantification->lower_bound()));
      absl::StrAppend(&quantifier, lower_bound->GetSQL());

      if (quantification->upper_bound() == nullptr) {
        absl::StrAppend(&quantifier, ", ");
      } else {
        // NOTE: This will break for general expressions, e.g. `?+f()`
        // The complete fix for this is to have only 1 expression in the
        // ResolvedAST, marking the quantifier explicitly as a fixed-bound.
        // For now, the current fix works because the syntax requires a literal
        // or a param, not an expression.
        //
        // Special case for equal bounds because of positional parameters,
        // because A{?} is not the same as A{?, ?}  (which references different
        // parameters).
        // The ResolvedAST correctly lists references to the same parameter,
        // but printing it as A{?, ?} will change its semantics, as it should be
        // A{?}. This is a general problem for any expression that involves
        // positional parameters, like A{?+1}
        if (AreRefsToSamePositionalParameter(quantification->lower_bound(),
                                             quantification->upper_bound())) {
          // Do nothing, we already printed the lower bound. There's no comma
          // nor upper bound.
        } else {
          absl::StrAppend(&quantifier, ", ");
          GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> upper_bound,
                           ProcessNode(quantification->upper_bound()));
          absl::StrAppend(&quantifier, upper_bound->GetSQL());
        }
      }
      absl::StrAppend(&quantifier, "}");
      if (quantification->is_reluctant()) {
        absl::StrAppend(&quantifier, "?");
      }
      return absl::StrCat("(", std::move(operand), std::move(quantifier), ")");
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected node kind: " << node->node_kind_string();
  }
}

absl::Status SQLBuilder::VisitResolvedMatchRecognizeScan(
    const ResolvedMatchRecognizeScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(node->input_scan(), query_expr.get()));

  // Add a space to be extra safe it's separate from whatever text was generated
  // right before.
  std::string match_recognize = " MATCH_RECOGNIZE(";

  if (node->partition_by() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> partition_result,
                     ProcessNode(node->partition_by()));
    absl::StrAppend(&match_recognize, "\n  ", partition_result->GetSQL());
  }

  GOOGLESQL_RET_CHECK_EQ(node->analytic_function_group_list_size(), 1);
  const auto* group = node->analytic_function_group_list(0);
  for (const auto& analytic_call : group->analytic_function_list()) {
    // Handle navigation functions (NEXT() and PREV()).
    GOOGLESQL_RET_CHECK(analytic_call->expr()->Is<ResolvedAnalyticFunctionCall>());
    const auto* call =
        analytic_call->expr()->GetAs<ResolvedAnalyticFunctionCall>();
    std::vector<std::string> inputs;
    for (const auto& argument : call->argument_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(argument.get()));
      inputs.push_back(result->GetSQL());
    }

    // Dummy access to mark accessed.
    call->function();
    GOOGLESQL_RET_CHECK_EQ(call->error_mode(),
                 ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);
    call->window_frame();  // dummy access
    GOOGLESQL_RET_CHECK(!call->distinct());

    std::string call_sql;
    switch (call->signature().context_id()) {
      case FN_LEAD:
        absl::StrAppend(&call_sql, "NEXT");
        break;
      case FN_LAG:
        absl::StrAppend(&call_sql, "PREV");
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected function: " << call->DebugString();
    }
    absl::StrAppend(&call_sql, "(", absl::StrJoin(inputs, ", "), ")");

    googlesql_base::InsertOrDie(&mutable_pending_columns(),
                     analytic_call->column().column_id(), call_sql);
  }

  GOOGLESQL_RET_CHECK(node->order_by() != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> order_by_result,
                   ProcessNode(node->order_by()));
  absl::StrAppend(&match_recognize, "\n  ", order_by_result->GetSQL());

  // We need to compute the SQL for DEFINE predicates before calling
  // GetSelectList() for the MEASURES clause, as GetSelectList() clears
  // `pending_columns_`.
  std::string definitions = "\n  DEFINE ";
  bool is_first = true;
  for (const auto& def : node->pattern_variable_definition_list()) {
    if (is_first) {
      is_first = false;
    } else {
      absl::StrAppend(&definitions, ",\n");
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> predicate_result,
                     ProcessNode(def->predicate()));
    absl::StrAppend(&definitions, "\n    ", ToIdentifierLiteral(def->name()),
                    " AS ", predicate_result->GetSQL());
  }

  // The MEASURES clause is required in the syntax, but we always export the
  // match_number column anyway so it's never empty.
  absl::StrAppend(&match_recognize, "\n  MEASURES ");
  // Set up the current MATCH_RECOGNIZE state
  googlesql_base::VarSetter<std::optional<CopyableState::MatchRecognizeState>> mr_state_setter(
      &state_.match_recognize_state,
      CopyableState::MatchRecognizeState{
          .match_number_column = node->match_number_column(),
          .match_row_number_column = node->match_row_number_column(),
          .classifier_column = node->classifier_column(),
      });

  std::vector<std::pair<std::string, std::string>> flattened_measure_list;

  // We need a copy, because moving it would lose all paths, not just those
  // we're updating.
  std::map<int, std::string> original_column_paths = column_paths();

  for (const auto& measure_group : node->measure_group_list()) {
    if (measure_group->pattern_variable_ref() == nullptr) {
      mutable_column_paths() = original_column_paths;
    } else {
      // Update any input columns to have the scoping variable's path instead
      SetPathForColumnList(node->input_scan()->column_list(),
                           measure_group->pattern_variable_ref()->name());
    }

    std::map<int64_t /* column_id */, const ResolvedExpr*> col_to_expr_map;
    std::vector<ResolvedColumn> measure_columns;
    measure_columns.reserve(measure_group->aggregate_list_size());
    for (const auto& expr : measure_group->aggregate_list()) {
      GOOGLESQL_RET_CHECK(expr->Is<ResolvedComputedColumn>())
          << "Deferred computed columns are not yet supported.";

      googlesql_base::InsertIfNotPresent(&col_to_expr_map, expr->column().column_id(),
                              expr->expr());
      measure_columns.push_back(expr->column());
    }

    std::vector<std::pair<std::string, std::string>> measure_list;
    GOOGLESQL_RETURN_IF_ERROR(GetSelectList(std::move(measure_columns), col_to_expr_map,
                                  node, query_expr.get(), &measure_list));
    absl::c_move(measure_list, std::back_inserter(flattened_measure_list));
  }

  // Restore the original column paths.
  mutable_column_paths().swap(original_column_paths);

  // Expose the MATCH_NUMBER column for any future references.
  flattened_measure_list.emplace_back(
      "MATCH_NUMBER()", GetColumnAlias(node->match_number_column()));

  absl::StrAppend(&match_recognize,
                  absl::StrJoin(flattened_measure_list, ", ",
                                [](std::string* out, const auto& m) {
                                  absl::StrAppend(out, "\n    ", m.first,
                                                  " AS ", m.second);
                                }));

  absl::StrAppend(&match_recognize, "\n  AFTER MATCH SKIP ");
  switch (node->after_match_skip_mode()) {
    case ResolvedMatchRecognizeScan::END_OF_MATCH:
      absl::StrAppend(&match_recognize, "PAST LAST ROW");
      break;
    case ResolvedMatchRecognizeScan::NEXT_ROW:
      absl::StrAppend(&match_recognize, "TO NEXT ROW");
      break;
    case ResolvedMatchRecognizeScan::AFTER_MATCH_SKIP_MODE_UNSPECIFIED:
      GOOGLESQL_RET_CHECK_FAIL() << "AFTER MATCH SKIP mode must be specified";
  }

  absl::StrAppend(&match_recognize, "\n  PATTERN (");
  GOOGLESQL_ASSIGN_OR_RETURN(std::string pattern_expr,
                   GeneratePatternExpression(node->pattern()));
  absl::StrAppend(&match_recognize, std::move(pattern_expr));
  absl::StrAppend(&match_recognize, ")");

  absl::StrAppend(&match_recognize, std::move(definitions));
  if (!node->option_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AppendOptions(node->option_list(), &match_recognize));
  }

  std::string scan_alias = GetScanAlias(node);
  absl::StrAppend(&match_recognize, "\n) AS ", scan_alias);

  GOOGLESQL_RET_CHECK(query_expr->TrySetMatchRecognizeClause(match_recognize));
  PushSQLForQueryExpression(node, query_expr.release());

  // Partitionining columns are visible downstream, but have already been named
  // in the input. However, do not touch columns from outside, such as with
  // DML.
  // TODO: When we have PARTITION BY.. AS, simply use it and introduce new
  // aliases since the column will original here, just like GROUP BY.
  absl::flat_hash_set<int> direct_input_columns;
  for (ResolvedColumn col : node->input_scan()->column_list()) {
    direct_input_columns.insert(col.column_id());
  }
  if (node->partition_by() != nullptr) {
    for (const auto& partitioning_col :
         node->partition_by()->partition_by_list()) {
      ResolvedColumn col = partitioning_col->column();
      if (direct_input_columns.contains(col.column_id())) {
        SetPathForColumn(col,
                         absl::StrCat(scan_alias, ".", GetColumnAlias(col)));
      }
    }
  }

  // Measure columns are always aliased in this scan, and will use the scan
  // alias directly for their path.
  for (const auto& measure_group : node->measure_group_list()) {
    for (const auto& measure_col : measure_group->aggregate_list()) {
      SetPathForColumn(
          measure_col->column(),
          absl::StrCat(scan_alias, ".", GetColumnAlias(measure_col->column())));
    }
  }
  SetPathForColumn(node->match_number_column(),
                   absl::StrCat(scan_alias, ".",
                                GetColumnAlias(node->match_number_column())));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedPivotScan(const ResolvedPivotScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(node->input_scan(), query_expr.get()));

  // Generate SQL for each pivot expression and create a unique alias for it.
  std::vector<std::string> pivot_expr_aliases;
  std::vector<std::string> pivot_expr_sql;
  for (const auto& resolved_pivot_expr : node->pivot_expr_list()) {
    const std::string alias = GenerateUniqueAliasName();
    pivot_expr_aliases.push_back(alias);

    std::unique_ptr<QueryFragment> pivot_expr_fragment;
    GOOGLESQL_ASSIGN_OR_RETURN(pivot_expr_fragment,
                     ProcessNode(resolved_pivot_expr.get()));
    pivot_expr_sql.push_back(absl::StrCat(pivot_expr_fragment->GetSQL(), " AS ",
                                          ToIdentifierLiteral(alias)));
  }

  // Generate SQL for the FOR expression
  std::unique_ptr<QueryFragment> for_expr_fragment;
  GOOGLESQL_ASSIGN_OR_RETURN(for_expr_fragment, ProcessNode(node->for_expr()));

  // Generate SQL for each IN expression and create a unique alias for it.
  std::vector<std::string> in_expr_sql;
  std::vector<std::string> in_expr_aliases;
  for (int i = 0; i < node->pivot_value_list_size(); ++i) {
    const ResolvedExpr* resolved_in_expr = node->pivot_value_list(i);

    const std::string alias = GenerateUniqueAliasName();
    in_expr_aliases.push_back(alias);

    std::unique_ptr<QueryFragment> in_expr_fragment;
    GOOGLESQL_ASSIGN_OR_RETURN(in_expr_fragment, ProcessNode(resolved_in_expr));
    in_expr_sql.push_back(absl::StrCat(in_expr_fragment->GetSQL(), " AS ",
                                       ToIdentifierLiteral(alias)));
  }

  // Put everything together to generate the PIVOT clause text.
  query_expr->TrySetPivotClause(absl::Substitute(
      " PIVOT($0 FOR ($1) IN ($2))", absl::StrJoin(pivot_expr_sql, ", "),
      for_expr_fragment->GetSQL(), absl::StrJoin(in_expr_sql, ", ")));
  PushSQLForQueryExpression(node, query_expr.release());

  // Remember the column aliases of the pivot scan's output columns for when
  // they are referenced later.
  for (const auto& col : node->pivot_column_list()) {
    const std::string& pivot_expr_alias =
        pivot_expr_aliases[col->pivot_expr_index()];
    const std::string& pivot_value_alias =
        in_expr_aliases[col->pivot_value_index()];
    mutable_computed_column_alias()[col->column().column_id()] =
        absl::StrCat(pivot_expr_alias, "_", pivot_value_alias);
  }

  for (const auto& groupby_expr : node->group_by_list()) {
    const ResolvedColumn& output_column = groupby_expr->column();
    const ResolvedColumn& input_column =
        groupby_expr->expr()->GetAs<ResolvedColumnRef>()->column();
    mutable_computed_column_alias()[output_column.column_id()] =
        GetColumnAlias(input_column);
  }

  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUnpivotScan(
    const ResolvedUnpivotScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(node->input_scan(), query_expr.get()));

  std::vector<std::string> unpivot_value_columns;
  for (const ResolvedColumn& val_col : node->value_column_list()) {
    std::string alias = GetColumnAlias(val_col);
    unpivot_value_columns.push_back(alias);
    mutable_computed_column_alias()[val_col.column_id()] = alias;
  }
  mutable_computed_column_alias()[node->label_column().column_id()] =
      GetColumnAlias(node->label_column());

  // Remember the aliases of the column refs from the input source that
  // appear in the output (not present in UNPIVOT IN clause).
  for (const std::unique_ptr<const googlesql::ResolvedComputedColumn>&
           input_column_expr : node->projected_input_column_list()) {
    const ResolvedColumn& output_column = input_column_expr->column();
    const ResolvedColumn& input_column =
        input_column_expr->expr()->GetAs<ResolvedColumnRef>()->column();
    mutable_computed_column_alias()[output_column.column_id()] =
        GetColumnAlias(input_column);
  }

  std::vector<std::string> unpivot_in_column_groups;
  int label_index = 0;
  for (auto& in_col_group : node->unpivot_arg_list()) {
    std::vector<std::string> column_group;
    for (auto& in_col : in_col_group->column_list()) {
      column_group.push_back(GetColumnAlias(in_col->column()));
    }
    GOOGLESQL_RET_CHECK(!node->label_list(label_index)->value().is_null());
    unpivot_in_column_groups.push_back(
        absl::StrCat("( ", absl::StrJoin(column_group, ","), " )", " AS ",
                     node->label_list(label_index)->value().DebugString()));
    ++label_index;
  }

  query_expr->TrySetUnpivotClause(absl::Substitute(
      " UNPIVOT $0 ( $1 FOR $2 IN ( $3 ) )",
      node->include_nulls() ? "INCLUDE NULLS" : "EXCLUDE NULLS",
      absl::StrCat("( ", absl::StrJoin(unpivot_value_columns, ","), " )"),
      GetColumnAlias(node->label_column()),
      absl::StrJoin(unpivot_in_column_groups, ",")));
  PushSQLForQueryExpression(node, query_expr.release());

  return absl::OkStatus();
}

bool SQLBuilder::IsDegenerateProjectScan(
    const ResolvedProjectScan* node, const QueryExpression* query_expression) {
  // In Pipe syntax mode, we cannot mark the project scan as degenerate
  // when it wraps over an aggregate scan with group-by columns.
  // Otherwise, the select list is overwritten with the output columns, and that
  // causes a bad query to be output if there are duplicate column names.
  if (IsPipeSyntaxTargetMode() &&
      node->input_scan()->Is<ResolvedAggregateScanBase>() &&
      query_expression->HasAnyGroupByColumn()) {
    return false;
  }

  return node->expr_list_size() == 0     // no computed columns
         && node->hint_list_size() == 0  // no hints
         // same column list as the input scan
         && node->input_scan()->column_list() == node->column_list()
         // same number of columns as in the select list generated from the
         // input scan
         && node->column_list().size() == query_expression->SelectList().size();
}

absl::Status SQLBuilder::VisitResolvedProjectScan(
    const ResolvedProjectScan* node) {
  std::unique_ptr<QueryExpression> query_expression;
  // No sql to add if the input scan is ResolvedSingleRowScan.
  if (node->input_scan()->node_kind() == RESOLVED_SINGLE_ROW_SCAN) {
    query_expression =
        std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->input_scan()));
    query_expression = std::move(result->query_expression);
  }

  // When in Pipe syntax mode, if the input scan is not supported, wrap
  // the query expression in Standard syntax mode.
  if (IsPipeSyntaxTargetMode() &&
      IsScanUnsupportedInPipeSyntax(node->input_scan())) {
    const std::string alias = GetScanAlias(node);
    query_expression->WrapForPipeSyntaxMode(alias);
    SetPathForColumnList(node->column_list(), alias);
  }

  if (IsDegenerateProjectScan(node, query_expression.get()) &&
      query_expression->CanFormSQLQuery()) {
    // NOTE: any future fields added to ProjectScan that are not IGNORABLE will
    // fail at CheckFieldsAccessed() when this condition is met.
    PushSQLForQueryExpression(node, query_expression.release());
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(!scans_to_collapse_.contains(node)) << node->DebugString();

  // Capture has_group_by_clause before possibly wrapping the query.
  bool has_group_by_clause = query_expression->HasGroupByClause();
  if (query_expression->CanFormSQLQuery()) {
    GOOGLESQL_RETURN_IF_ERROR(
        WrapQueryExpression(node->input_scan(), query_expression.get()));
  }

  std::map<int64_t /* column_id */, const ResolvedExpr*> col_to_expr_map;
  for (const auto& expr : node->expr_list()) {
    googlesql_base::InsertIfNotPresent(&col_to_expr_map, expr->column().column_id(),
                            expr->expr());
  }
  SQLAliasPairList select_list;
  GOOGLESQL_RETURN_IF_ERROR(GetSelectList(node->column_list(), col_to_expr_map, node,
                                query_expression.get(), &select_list));

  std::string select_hints;
  GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &select_hints));

  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(select_list, select_hints));

  // For queries that have only aggregate columns but no group-by columns, eg.
  // `select count(*) from table`, the following condition become true, and we
  // capture that information in the query_expression so that it can be used
  // to output correct SQL in Pipe syntax mode.
  if (node->input_scan()->Is<ResolvedAggregateScanBase>() &&
      scans_to_collapse_.contains(node->input_scan()) && !has_group_by_clause &&
      query_expression->HasSelectClause()) {
    GOOGLESQL_RETURN_IF_ERROR(query_expression->SetGroupByOnlyAggregateColumns(true));
  }

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

std::string SQLBuilder::ComputedColumnAliasDebugString() const {
  std::vector<std::string> pairs;
  pairs.reserve(computed_column_alias().size());
  for (const auto& kv : computed_column_alias()) {
    pairs.push_back(absl::StrCat("(", kv.first, ", ", kv.second, ")"));
  }
  return absl::StrCat("[", absl::StrJoin(pairs, ", "), "]");
}

absl::Status SQLBuilder::VisitResolvedExecuteAsRoleScan(
    const ResolvedExecuteAsRoleScan* node) {
  // There is currently no way to express this node in SQL (which is the
  // boundary between rights zones).
  return ::googlesql_base::InvalidArgumentErrorBuilder()
         << "SQLBuilder cannot generate SQL for " << node->node_kind_string();
}

absl::Status SQLBuilder::VisitResolvedTVFScan(const ResolvedTVFScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);

  GOOGLESQL_ASSIGN_OR_RETURN(std::string from,
                   ProcessResolvedTVFScan(node, TVFBuildMode::kSql));

  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::ProcessResolvedTVFScan(
    const ResolvedTVFScan* node, TVFBuildMode build_mode) {
  // Visit the TVF arguments and build SQL from them.
  std::vector<std::string> argument_list;
  argument_list.reserve(node->argument_list_size());
  std::vector<std::string> output_aliases;

  // We use this flag to track if the first argument of the TVF
  // call is a query, so that we can transform `TVF((query), ...)` into
  // `query |> CALL TVF(...)` when in Pipe SQL syntax mode.
  //
  // This is more restrictive than strictly necessary.  We could use pipe
  // CALL in cases where the query is the first table-typed argument.
  // When FEATURE_PIPE_CALL_INPUT_TABLE is enabled, we could use pipe syntax
  // for any case with a query input, but we might want a heuristic for when
  // to do this.  `INPUT TABLE` syntax is currently never generated.
  bool is_first_arg_query = false;

  // Used to detect the first relation argument in the TVF call, to avoid
  // printing it especially in GQL.
  std::optional<int> first_relation_arg_idx;
  // Used to make sure there is only one implicit graph argument.
  bool seen_first_graph_arg = false;

  absl::flat_hash_set<int> unset_arg_indices;
  for (int arg_idx = 0; arg_idx < node->argument_list_size(); ++arg_idx) {
    const ResolvedTVFArgument* argument = node->argument_list(arg_idx);
    // If this is a scalar expression, generate SQL for it and continue.
    if (argument->expr() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(argument->expr()));
      argument_list.push_back(result->GetSQL());
      continue;
    }

    if (argument->model() != nullptr) {
      const std::string model_alias =
          ToIdentifierLiteral(argument->model()->model()->Name());
      argument_list.push_back(absl::StrCat("MODEL ", model_alias));
      continue;
    }

    if (argument->connection() != nullptr) {
      const std::string connection_alias =
          ToIdentifierLiteral(argument->connection()->connection()->Name());
      argument_list.push_back(absl::StrCat("CONNECTION ", connection_alias));
      continue;
    }

    if (argument->descriptor_arg() != nullptr) {
      std::string ret("DESCRIPTOR(");
      // sql_builder tests require accessing descriptor_column_list but only
      // descriptor_column_name_list is needed to rebuild sql query.
      GOOGLESQL_RET_CHECK_GE(argument->descriptor_arg()->descriptor_column_list().size(),
                   0);
      bool need_comma = false;
      for (const auto& column_name :
           argument->descriptor_arg()->descriptor_column_name_list()) {
        if (need_comma) {
          absl::StrAppend(&ret, ",");
        }
        absl::StrAppend(&ret, column_name);
        need_comma = true;
      }

      absl::StrAppend(&ret, ")");
      argument_list.push_back(ret);
      continue;
    }

    if (argument->graph() != nullptr) {
      GOOGLESQL_RET_CHECK(build_mode == TVFBuildMode::kGqlExplicit);
      GOOGLESQL_RET_CHECK(!seen_first_graph_arg)
          << "There can only be one implicit graph argument.";
      seen_first_graph_arg = true;
      // This is unused but we need to have an argument for later argument
      // handling.
      const std::string graph_alias =
          ToIdentifierLiteral(argument->graph()->Name());
      argument_list.push_back(absl::StrCat("GRAPH ", graph_alias));
      continue;
    }

    // If this is a relation argument, generate SQL for each of its attributes.
    const ResolvedScan* scan = argument->scan();
    GOOGLESQL_RET_CHECK(scan != nullptr);
    if (scan->Is<ResolvedUnsetArgumentScan>()) {
      unset_arg_indices.insert(arg_idx);
      continue;
    }
    GOOGLESQL_RET_CHECK_LT(arg_idx, node->signature()->input_arguments().size());
    GOOGLESQL_RET_CHECK(node->signature()->argument(arg_idx).is_relation());

    if (!first_relation_arg_idx.has_value()) {
      first_relation_arg_idx = arg_idx;
      if (build_mode == TVFBuildMode::kGqlImplicit) {
        // We add a bogus argument to align the argument names with the given
        // arguments. It will be removed later. We're using something that does
        // not parse as SQL in order to make it obvious if we accidentally
        // produce it.
        argument_list.push_back("<implicit table>");
        continue;
      }
    }

    const TVFRelation& relation =
        node->signature()->argument(arg_idx).relation();

    // Validate table arg with pseudo-columns aren't being passed into TVF.
    // There is no way to produce a pseudo-column through SQL and converting
    // the pseudo-column into a regular column wouldn't preserve the original
    // semantics of the query.
    for (int i = 0; i < relation.num_columns(); ++i) {
      const TVFSchemaColumn& col = relation.column(i);
      if (col.is_pseudo_column) {
        // TODO: b/469804970 - Fix this once GoogleSQL supports producing
        //       pseudo-columns in the language.
        return absl::UnimplementedError(
            absl::StrCat("SQL Builder doesn't support passing pseudo-columns "
                         "on table args into TVFs: ",
                         col.name));
      }
    }

    if (relation.is_value_table()) {
      // If the input table is a value table, add SELECT AS VALUE to make sure
      // that the generated SQL for the scan is also a value table.
      // There can be pseudo columns added in the input value table.
      GOOGLESQL_RET_CHECK_GE(relation.columns().size(), 1);
      GOOGLESQL_RET_CHECK(!relation.columns().begin()->is_pseudo_column);
      GOOGLESQL_RET_CHECK(std::all_of(relation.columns().begin() + 1,
                            relation.columns().end(),
                            [](const googlesql::TVFSchemaColumn& col) {
                              return col.is_pseudo_column;
                            }));
      GOOGLESQL_RET_CHECK_GE(argument->argument_column_list_size(), 1);
      GOOGLESQL_RET_CHECK_EQ(scan->column_list(0).column_id(),
                   argument->argument_column_list(0).column_id());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(scan));
      GOOGLESQL_RET_CHECK(result->query_expression != nullptr);
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(scan->column_list(),
                                            result->query_expression.get()));
      const std::string column_alias = googlesql_base::FindWithDefault(
          computed_column_alias(), scan->column_list(0).column_id());
      GOOGLESQL_RET_CHECK(!column_alias.empty())
          << "\nfor column: " << scan->column_list(0).DebugString()
          << "\nscan: " << scan->DebugString()
          << "\ncomputed_column_alias_: " << ComputedColumnAliasDebugString();

      GOOGLESQL_RETURN_IF_ERROR(
          WrapQueryExpression(scan, result->query_expression.get()));
      GOOGLESQL_RET_CHECK(result->query_expression->TrySetSelectClause(
          {std::make_pair(column_alias, column_alias)}, ""));
      result->query_expression->SetSelectAsModifier("AS VALUE");

      if (arg_idx == 0) {
        is_first_arg_query = true;
      }
      argument_list.push_back("(" + result->GetSQL() + ")");
    } else {
      // If the input table is not a value table, assign each required column
      // name as an alias for each element in the SELECT list.
      GOOGLESQL_RET_CHECK_EQ(node->argument_list_size(),
                   node->signature()->input_arguments().size())
          << node->DebugString();

      // Generate SQL for the input relation argument. Before doing so, check if
      // the argument_column_list differs from the scan's column_list. If so,
      // add a wrapping query that explicitly lists the former columns in the
      // correct order.
      //
      // First build a map from each column ID of the input scan to the alias
      // used when we generated the input scan SELECT list.
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(scan));
      GOOGLESQL_RET_CHECK(result->query_expression != nullptr);
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(scan->column_list(),
                                            result->query_expression.get()));
      std::map<int, std::string> column_id_to_alias;
      const SQLAliasPairList& select_list =
          result->query_expression->SelectList();
      GOOGLESQL_RET_CHECK_EQ(select_list.size(), scan->column_list_size())
          << scan->DebugString();
      for (int i = 0; i < select_list.size(); ++i) {
        googlesql_base::InsertIfNotPresent(&column_id_to_alias,
                                scan->column_list(i).column_id(),
                                select_list[i].second);
        if (arg_idx == 0 &&
            node->tvf()->Is<ForwardInputSchemaToOutputSchemaTVF>()) {
          output_aliases.push_back(select_list[i].second);
        }
      }
      // Then build a new SELECT list containing only the columns explicitly
      // referenced in the 'argument_column_list', using aliases from the
      // provided input table if necessary.
      SQLAliasPairList arg_col_list_select_items;
      GOOGLESQL_RET_CHECK_EQ(argument->argument_column_list_size(),
                   relation.num_columns())
          << relation.DebugString();
      for (int i = 0; i < argument->argument_column_list_size(); ++i) {
        const ResolvedColumn& col = argument->argument_column_list(i);
        const std::string* alias =
            googlesql_base::FindOrNull(column_id_to_alias, col.column_id());
        GOOGLESQL_RET_CHECK(alias != nullptr) << col.DebugString();

        const std::string required_col = relation.column(i).name;
        if (!IsInternalAlias(required_col)) {
          // If the TVF call included explicit column names for the input table
          // argument, specify them again here in case the TVF requires these
          // exact column names to work properly.
          arg_col_list_select_items.push_back(
              {*alias, ToIdentifierLiteral(required_col)});
          googlesql_base::InsertOrUpdate(&mutable_computed_column_alias(), col.column_id(),
                              ToIdentifierLiteral(required_col));
        } else {
          // If there is no explicit column/alias name, then we use the
          // original column name/alias.
          arg_col_list_select_items.push_back({*alias, *alias});
          googlesql_base::InsertOrUpdate(&mutable_computed_column_alias(), col.column_id(),
                              *alias);
        }
      }

      GOOGLESQL_RETURN_IF_ERROR(
          WrapQueryExpression(scan, result->query_expression.get()));
      GOOGLESQL_RET_CHECK(result->query_expression->TrySetSelectClause(
          arg_col_list_select_items, ""));

      if (arg_idx == 0) {
        is_first_arg_query = true;
      }
      argument_list.push_back("(" + result->GetSQL() + ")");
    }
  }
  GOOGLESQL_RET_CHECK_EQ(argument_list.size() + unset_arg_indices.size(),
               node->argument_list_size())
      << "All arguments in the function must make it to argument_list or "
         "unset_arg_indices. argument_list.size() = "
      << argument_list.size() << "; node: " << node->DebugString();

  std::string from;
  UpdateArgsForGetSQL(node->function_call_signature().get(), &argument_list,
                      unset_arg_indices);
  std::string name = node->tvf()->FullName(/*include_group=*/false);

  if (node->tvf()->tvf_options().uses_upper_case_sql_name) {
    absl::AsciiStrToUpper(&name);
  }

  if (!argument_list.empty()) {
    switch (build_mode) {
      case TVFBuildMode::kGqlExplicit:
        if (node->argument_list(0)->graph() != nullptr) {
          // For GQL CALL, the first argument is the implicit graph argument,
          // which we don't print.
          argument_list.erase(argument_list.begin());
        }
        break;
      case TVFBuildMode::kGqlImplicit:
        if (first_relation_arg_idx.has_value()) {
          // For GQL CALL PER, the first argument is the implicit scan argument,
          // which we don't print.
          GOOGLESQL_RET_CHECK_LT(*first_relation_arg_idx, argument_list.size());
          argument_list.erase(argument_list.begin() + *first_relation_arg_idx);
        }
        break;
      case TVFBuildMode::kSql:
        break;
    }
  }
  if (IsPipeSyntaxTargetMode() && !argument_list.empty() &&
      is_first_arg_query && build_mode == TVFBuildMode::kSql) {
    std::string first_arg = std::move(argument_list[0]);
    argument_list.erase(argument_list.begin());
    from = absl::StrCat(first_arg.substr(1, first_arg.length() - 2), " ",
                        QueryExpression::kPipe, " CALL ", name, "(",
                        absl::StrJoin(argument_list, ", "), ")");
  } else {
    from = absl::StrCat(name, "(", absl::StrJoin(argument_list, ", "), ")");
  }

  // Append the query hint, if present.
  if (node->hint_list_size() > 0) {
    absl::StrAppend(&from, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &from));
    absl::StrAppend(&from, " ");
  }

  // That's all for GQL. We will follow up with YIELD to assign new aliases to
  // output columns, without assigning an alias to the scan.
  if (build_mode != TVFBuildMode::kSql) {
    const TVFRelation& schema = node->signature()->result_schema();
    GOOGLESQL_RET_CHECK_GT(node->column_index_list_size(), 0);
    GOOGLESQL_RET_CHECK_EQ(node->column_index_list_size(), node->column_list_size());
    absl::StrAppend(&from, " YIELD ");
    for (int i = 0; i < node->column_index_list_size(); ++i) {
      if (i > 0) {
        absl::StrAppend(&from, ", ");
      }
      absl::StrAppend(
          &from,
          ToIdentifierLiteral(schema.column(node->column_index_list(i)).name),
          " AS ", GetColumnAlias(node->column_list(i)));
    }
    // That is all for a GQL CALL or CALL PER().
    return from;
  }

  // For SQL, we need to assign the correct aliases to output names, as well as
  // the scan alias.
  const std::string tvf_scan_alias = GetScanAlias(node);
  absl::StrAppend(&from, " AS ", tvf_scan_alias);

  // Copy the TVF's column names to the pending_columns_ list so that the
  // outer SELECT that includes this TVF in the FROM clause uses the TVF
  // names from its output schema.
  bool is_value_table = node->signature()->result_schema().is_value_table();
  for (int i = 0; i < node->column_list_size(); ++i) {
    const ResolvedColumn& column = node->column_list(i);
    if (i == 0 && is_value_table) {
      googlesql_base::InsertOrDie(&mutable_pending_columns(), column.column_id(),
                       tvf_scan_alias);
    } else {
      std::string column_name;
      // If this TVF has this particular implementation, then we pull the
      // column names from the input schema because the signature result
      // schema does not contain the expected column names.
      //
      // TODO: Figure out what's going on here, the signature's
      // result schema should always have appropriate column names and it's
      // unclear why this one does not.
      // TODO: Treating IsInternalAlias() as special isn't right:
      // there are other quotable identifiers (like `where`) that must be
      // handled as well.
      if (node->tvf()->Is<ForwardInputSchemaToOutputSchemaTVF>()) {
        GOOGLESQL_RET_CHECK(node->signature()->argument(0).is_relation());
        column_name = node->signature()->argument(0).relation().column(i).name;
        if (IsInternalAlias(column_name)) {
          GOOGLESQL_RET_CHECK_LT(i, output_aliases.size());
          column_name = output_aliases[i];
        }
      } else {
        // Otherwise, get the TVF column names from its output result schema.
        GOOGLESQL_RET_CHECK_LT(i, node->column_index_list().size());
        const TVFRelation& signature_result_schema =
            node->signature()->result_schema();
        column_name =
            signature_result_schema.column(node->column_index_list(i)).name;
      }
      // Prefix column name with tvf alias to support strict resolution mode.
      googlesql_base::InsertOrDie(&mutable_pending_columns(), column.column_id(),
                       absl::StrCat(tvf_scan_alias, ".", column_name));
    }
  }
  return from;
}

absl::Status SQLBuilder::VisitResolvedRelationArgumentScan(
    const ResolvedRelationArgumentScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(node->name()));
  SQLAliasPairList select_list;
  if (node->is_value_table()) {
    GOOGLESQL_RET_CHECK_EQ(1, node->column_list().size());
    select_list.push_back(
        std::make_pair(ToIdentifierLiteral(node->name()),
                       GetColumnAlias(node->column_list()[0])));
  } else {
    for (const ResolvedColumn& column : node->column_list()) {
      select_list.push_back(
          std::make_pair(absl::StrCat(ToIdentifierLiteral(node->name()), ".",
                                      ToIdentifierLiteral(column.name())),
                         GetColumnAlias(column)));
    }
  }
  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(select_list, ""));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedFilterScan(
    const ResolvedFilterScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  // When in Pipe syntax mode, if the input scan is not supported, wrap
  // the query expression in Standard syntax mode.
  if (IsPipeSyntaxTargetMode() &&
      IsScanUnsupportedInPipeSyntax(node->input_scan())) {
    const std::string alias = GetScanAlias(node);
    query_expression->WrapForPipeSyntaxMode(alias);
    SetPathForColumnList(node->column_list(), alias);
  }

  // Filter scans over non-value table scans can always be flattened.  Other
  // combinations can probably be flattened more aggressively too, but I only
  // needed simple table scans at this time. Can work harder on this when it
  // becomes needed.
  const ResolvedTableScan* simple_table_scan = nullptr;
  if (node->input_scan()->node_kind() == googlesql::RESOLVED_TABLE_SCAN) {
    simple_table_scan = node->input_scan()->GetAs<ResolvedTableScan>();
    if (simple_table_scan->table()->IsValueTable()) {
      simple_table_scan = nullptr;
    }
  }
  if (simple_table_scan != nullptr) {
    // Make columns from the input_scan available to filter_expr node processing
    // below.
    GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(
        simple_table_scan,
        googlesql_base::FindWithDefault(table_alias_map(), simple_table_scan->table())));
    // Remove the underlying TableScan's select list, to let the ProjectScan
    // just above this FilterScan to install its select list without wrapping
    // this fragment.
    query_expression->ResetSelectClause();
  } else {
    if (!query_expression->CanSetWhereClause()) {
      GOOGLESQL_RETURN_IF_ERROR(
          WrapQueryExpression(node->input_scan(), query_expression.get()));
    }
  }
  std::string where;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->filter_expr()));
  absl::StrAppend(&where, result->GetSQL());
  GOOGLESQL_RET_CHECK(query_expression->TrySetWhereClause(where));

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAnalyticScan(
    const ResolvedAnalyticScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());
  if (query_expression->CanFormSQLQuery()) {
    GOOGLESQL_RETURN_IF_ERROR(
        WrapQueryExpression(node->input_scan(), query_expression.get()));
  }

  for (const auto& function_group : node->function_group_list()) {
    GOOGLESQL_RETURN_IF_ERROR(function_group->Accept(this));
  }

  if (!scans_to_collapse_.contains(node)) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddSelectListIfNeeded(node->column_list(), query_expression.get()));
  } else {
    // We do not yet support conditional evaluation on analytic functions, so
    // for now we detect this case. When they are implemented, this branch
    // should simply be removed: there are no SelectLists() to add.
    GOOGLESQL_RET_CHECK_FAIL() << "ResolvedASTs with deferred columns on analytic scans "
                        "are not yet supported";
  }

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGroupRowsScan(
    const ResolvedGroupRowsScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);

  std::string from = "GROUP_ROWS()";

  if (node->hint_list_size() > 0) {
    absl::StrAppend(&from, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &from));
  }

  const std::string group_rows_alias = GetScanAlias(node);
  absl::StrAppend(&from, " AS ", group_rows_alias);

  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));

  std::map<int64_t, const ResolvedExpr*> col_to_expr_map;
  for (const auto& col : node->input_column_list()) {
    googlesql_base::InsertIfNotPresent(&col_to_expr_map, col->column().column_id(),
                            col->expr());
    if (col->expr()->Is<ResolvedMakeStruct>()) {
      const ResolvedMakeStruct* make_struct =
          col->expr()->GetAs<ResolvedMakeStruct>();

      const StructType* struct_type = make_struct->type()->AsStruct();
      if (struct_type->num_fields() != make_struct->field_list_size()) {
        return ::googlesql_base::InvalidArgumentErrorBuilder()
               << "In GROUP_ROWS(): the number of fields of ResolvedMakeStruct "
                  "and its corresponding StructType, do not match\n:"
               << node->DebugString() << "\nStructType:\n"
               << struct_type->DebugString();
      }
      for (int i = 0; i < struct_type->num_fields(); ++i) {
        // Dummy access the column to satisfy the final
        // CheckFieldsAccessed() on a statement level before building the sql.
        make_struct->field_list(i)->GetAs<ResolvedColumnRef>()->column();
      }
      continue;
    }
    // Define new aliases for all columns to repoint them to group_rows_alias.
    const ResolvedColumnRef* ref = col->expr()->GetAs<ResolvedColumnRef>();
    std::string alias;

    // Filter operator might decide to reset the column path to the original
    // column name even when the column has already an associated internal
    // alias. We take this case into account to keep generated query valid.
    std::string current_path = GetColumnPath(ref->column());
    bool aliased_to_self = absl::EndsWith(current_path, ref->column().name());
    if (!aliased_to_self &&
        googlesql_base::ContainsKey(computed_column_alias(), ref->column().column_id())) {
      alias = GetColumnAlias(ref->column());
    } else {
      alias = ref->column().name();
    }
    SetPathForColumn(ref->column(), absl::StrCat(group_rows_alias, ".",
                                                 ToIdentifierLiteral(alias)));
  }
  SQLAliasPairList select_list;
  GOOGLESQL_RETURN_IF_ERROR(GetSelectList(node->column_list(), col_to_expr_map, node,
                                query_expression.get(), &select_list));

  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(select_list,
                                                 /*select_hints=*/""));

  PushSQLForQueryExpression(node, query_expression.release());

  return absl::OkStatus();
}

static std::string GetJoinTypeString(ResolvedJoinScan::JoinType join_type,
                                     bool expect_on_condition) {
  switch (join_type) {
    case ResolvedJoinScan::INNER:
      return expect_on_condition ? "INNER JOIN" : "CROSS JOIN";
    case ResolvedJoinScan::LEFT:
      return "LEFT JOIN";
    case ResolvedJoinScan::RIGHT:
      return "RIGHT JOIN";
    case ResolvedJoinScan::FULL:
      return "FULL JOIN";
  }
}

absl::StatusOr<std::string> SQLBuilder::GetJoinOperand(
    const ResolvedScan* scan) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> scan_f, ProcessNode(scan));
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(scan, scan_f->query_expression.get()));
  std::string sql(scan_f->query_expression->FromClause());
  return sql;
}

absl::StatusOr<std::string> SQLBuilder::GetJoinOperand(
    const ResolvedScan* scan, absl::flat_hash_map<int, std::vector<int>>&
                                  right_to_left_column_id_mapping) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> scan_f, ProcessNode(scan));
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(scan, scan_f->query_expression.get()));
  GOOGLESQL_ASSIGN_OR_RETURN(SQLAliasPairList using_argument,
                   GetUsingWrapper(GetScanAlias(scan), scan->column_list(),
                                   right_to_left_column_id_mapping));
  GOOGLESQL_RET_CHECK(scan_f->query_expression->TrySetSelectClause(using_argument, ""));
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(scan, scan_f->query_expression.get()));
  std::string sql(scan_f->query_expression->FromClause());
  return sql;
}

absl::StatusOr<QueryExpression::SQLAliasPairList> SQLBuilder::GetUsingWrapper(
    absl::string_view scan_alias, const ResolvedColumnList& column_list,
    absl::flat_hash_map<int, std::vector<int>>&
        right_to_left_column_id_mapping) {
  SQLAliasPairList result;
  absl::flat_hash_map<ResolvedColumn, const std::string*> updated_alias;
  for (const auto& col : column_list) {
    int right_scan_col_id = col.column_id();
    const std::string& column_alias = GetColumnAlias(col);
    std::string column_name = absl::StrCat(scan_alias, ".", column_alias);
    // The `right_to_left_column_id_mapping` will contain the `column_ids` of
    // each item in the right scan column list which needs to be given a new
    // alias corresponding to one in the left side of the join scan.
    std::vector<int>* right_using_ids =
        googlesql_base::FindOrNull(right_to_left_column_id_mapping, right_scan_col_id);
    if (right_to_left_column_id_mapping.contains(right_scan_col_id) &&
        right_using_ids != nullptr && !right_using_ids->empty()) {
      int left_scan_col_id = right_using_ids->back();
      // Pop the left column id in case there are duplicate right columns in the
      // USING(...) expression.
      right_using_ids->pop_back();
      // The alias should have already been computed at this point.

      const std::string* const new_alias =
          googlesql_base::FindOrNull(computed_column_alias(), left_scan_col_id);
      GOOGLESQL_RET_CHECK(new_alias != nullptr);
      result.push_back({column_name, *new_alias});

      // Update the computed column alias so that outer queries which need to
      // reference this column use the updated alias. We'll update at the end
      // in case there are duplicate right columns not in the USING clause.
      if (right_using_ids->empty()) {
        updated_alias[col] = new_alias;
      }
    } else {
      // Write a copy for other columns which did not need a new alias.
      result.push_back({column_name, ""});
    }
  }
  for (const auto& elem : updated_alias) {
    mutable_computed_column_alias()[elem.first.column_id()] = *elem.second;
    SetPathForColumn(elem.first, absl::StrCat(scan_alias, ".", *elem.second));
  }
  return result;
}

std::string SQLBuilder::MakeNonconflictingAlias(absl::string_view name) {
  const std::string alias_prefix_lower = absl::AsciiStrToLower(name);
  std::string alias;
  do {
    alias = absl::StrCat(alias_prefix_lower, "_", GetUniqueId());
  } while (col_ref_names().contains(alias));
  return ToIdentifierLiteral(alias);
}

std::string SQLBuilder::GetTableAlias(const Table* table) {
  if (!table_alias_map().contains(table)) {
    googlesql_base::InsertIfNotPresent(&mutable_table_alias_map(), table,
                            MakeNonconflictingAlias(table->Name()));
  }

  return googlesql_base::FindOrDie(table_alias_map(), table);
}

std::string SQLBuilder::GetScanAlias(const ResolvedScan* scan) {
  if (!scan_alias_map().contains(scan)) {
    const std::string scan_alias_prefix_lower = absl::AsciiStrToLower(
        (scan->node_kind() == RESOLVED_TABLE_SCAN)
            // We prefer to use table names as alias prefix when possible.
            ? scan->GetAs<ResolvedTableScan>()->table()->Name()
            : scan->node_kind_string());
    googlesql_base::InsertIfNotPresent(&mutable_scan_alias_map(), scan,
                            MakeNonconflictingAlias(scan_alias_prefix_lower));
  }

  return googlesql_base::FindOrDie(scan_alias_map(), scan);
}

int64_t SQLBuilder::GetUniqueId() {
  while (true) {
    // Allocate from the sequence, but make sure it's higher than the max we
    // should start from.
    int next_alias_id = static_cast<int>(alias_id_sequence_.GetNext());
    if (next_alias_id > max_seen_alias_id()) {
      ABSL_DCHECK_NE(next_alias_id, 0);
      set_max_seen_alias_id(next_alias_id);
      break;
    }
  }
  ABSL_DCHECK_LE(max_seen_alias_id(), std::numeric_limits<int32_t>::max());
  return max_seen_alias_id();
}

void SQLBuilder::set_max_seen_alias_id(int max_seen_alias_id) {
  *mutable_max_seen_alias_id() = max_seen_alias_id;
}

int* SQLBuilder::mutable_max_seen_alias_id() {
  return max_seen_alias_id_root_ptr_ == nullptr ? &max_seen_alias_id_
                                                : max_seen_alias_id_root_ptr_;
}

int SQLBuilder::max_seen_alias_id() const {
  if (max_seen_alias_id_root_ptr_ == nullptr) {
    return max_seen_alias_id_;
  }
  return *max_seen_alias_id_root_ptr_;
}

void SQLBuilder::SetPathForColumn(const ResolvedColumn& column,
                                  const std::string& path) {
  googlesql_base::InsertOrUpdate(&mutable_column_paths(), column.column_id(), path);
}

void SQLBuilder::SetPathForColumnList(const ResolvedColumnList& column_list,
                                      absl::string_view scan_alias) {
  for (const ResolvedColumn& col : column_list) {
    SetPathForColumn(col, absl::StrCat(scan_alias, ".", GetColumnAlias(col)));
  }
}

absl::Status SQLBuilder::SetPathForColumnsInScan(const ResolvedScan* scan,
                                                 const std::string& alias) {
  if (scan->node_kind() == RESOLVED_TABLE_SCAN) {
    const auto* table_scan = scan->GetAs<ResolvedTableScan>();
    if (table_scan->table()->IsValueTable()) {
      if (scan->column_list_size() > 0) {
        // This code is wrong.  See http://b/37291554.
        const Table* table = table_scan->table();
        std::string table_name =
            alias.empty() ? ToIdentifierLiteral(table->Name()) : alias;
        SetPathForColumn(scan->column_list(0), table_name);
      }
      return absl::OkStatus();
    }
    // While column_index_list should always match to column_list, it's not
    // always the case. When it does, use column_index_list to retrieve column
    // names, otherwise fallback to using ResolvedColumn outside this
    // if-block.
    // See the class comment on `ResolvedTableScan` and ResolvedTableScan
    // generation code in gen_resolved_ast.py
    // TODO: Remove this if() and always use column_index_list
    // to handle ResolvedTableScan.
    if (table_scan->column_index_list_size() > 0) {
      GOOGLESQL_RET_CHECK_EQ(table_scan->column_index_list_size(),
                   table_scan->column_list_size());
      const Table* table = table_scan->table();
      std::string table_name =
          alias.empty() ? ToIdentifierLiteral(table->Name()) : alias;
      for (int i = 0; i < table_scan->column_index_list_size(); ++i) {
        const Column* column =
            table->GetColumn(table_scan->column_index_list(i));
        GOOGLESQL_RET_CHECK_NE(column, nullptr);
        SetPathForColumn(
            scan->column_list(i),
            absl::StrCat(table_name, ".", ToIdentifierLiteral(column->Name())));
      }
      return absl::OkStatus();
    }
  }
  // Use ResolvedColumn::name() for non-table scans and table scans where
  // column_index_list does not match in length column_list.
  for (const ResolvedColumn& column : scan->column_list()) {
    const std::vector<std::string> column_table_name =
        absl::StrSplit(column.table_name(), '.');
    // Retrieve the implicit table alias from the table path. This handles the
    // case where the table name has multiple components, e.g. the case of
    // nested catalogs.
    const std::string table_name =
        alias.empty() ? ToIdentifierLiteral(column_table_name.back()) : alias;
    SetPathForColumn(column, absl::StrCat(table_name, ".",
                                          ToIdentifierLiteral(column.name())));
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::SetPathForColumnsInReturningExpr(
    const ResolvedExpr* expr) {
  std::vector<std::unique_ptr<const ResolvedColumnRef>> refs;
  GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(*expr, &refs));
  for (std::unique_ptr<const ResolvedColumnRef>& ref : refs) {
    // Setup table alias for DML target table columns in returning clause.
    SetPathForColumn(ref->column(),
                     absl::StrCat(returning_table_alias_, ".",
                                  ToIdentifierLiteral(ref->column().name())));
  }

  return absl::OkStatus();
}

absl::StatusOr<ResolvedColumn> ExtractColumnFromArgumentForUsing(
    const ResolvedExpr* arg) {
  const ResolvedColumnRef* c_ref;
  if (arg->Is<ResolvedColumnRef>()) {
    c_ref = arg->GetAs<ResolvedColumnRef>();
  } else if (arg->Is<ResolvedCast>()) {
    GOOGLESQL_RET_CHECK(arg->GetAs<ResolvedCast>()->expr()->Is<ResolvedColumnRef>());
    c_ref = arg->GetAs<ResolvedCast>()->expr()->GetAs<ResolvedColumnRef>();
  } else {
    return absl::InvalidArgumentError("Bad argument in USING(...) expression");
  }
  return c_ref->column();
}

bool ValidateArgumentForUsing(const ResolvedExpr* expr) {
  // If there is only one column in the using clause the join expr is just
  // an "$equal" function call between two columns. Otherwise, the join_expr
  // is an "$and" function call joining multiple "$equal" function calls. This
  // function validates each "$equal" function call and thus we check that the
  // argument_list_size is 2.
  auto func = expr->GetAs<ResolvedFunctionCallBase>();
  if (func->argument_list_size() != 2) {
    return false;
  }
  auto arg_one = func->argument_list(0);
  auto arg_two = func->argument_list(1);

  // May be incorrect in the case that the two types can be coerced into the
  // same supertype which supports equality.
  if (!(arg_one->type()->Equals(arg_two->type()) ||
        (arg_one->type()->IsNumerical() && arg_two->type()->IsNumerical()))) {
    return false;
  }

  if (!(arg_one->Is<ResolvedColumnRef>() ||
        (arg_one->Is<ResolvedCast>() &&
         arg_one->GetAs<ResolvedCast>()->expr()->Is<ResolvedColumnRef>()))) {
    return false;
  }
  if (!(arg_two->Is<ResolvedColumnRef>() ||
        (arg_two->Is<ResolvedCast>() &&
         arg_two->GetAs<ResolvedCast>()->expr()->Is<ResolvedColumnRef>()))) {
    return false;
  }
  return true;
}

bool ValidateUsingScan(const ResolvedJoinScan* node) {
  if (node->join_expr() == nullptr) {
    return false;
  }
  if (!node->join_expr()->Is<ResolvedFunctionCallBase>()) {
    return false;
  }
  auto func = node->join_expr()->GetAs<ResolvedFunctionCallBase>();
  for (const auto& arg : func->argument_list()) {
    // There are only two cases.
    // 1. If there is only one argument in the using clause then the outer
    // function is an "$equal" function call with two column refs as
    // arguments.
    //
    // 2. If there are multiple arguments in the using clause then the
    // outer function is "and" function call with each argument being
    // an "$equal" function call.
    if (!(arg->Is<ResolvedFunctionCallBase>() || arg->Is<ResolvedColumnRef>() ||
          arg->Is<ResolvedCast>())) {
      return false;
    }
    if (arg->Is<ResolvedColumnRef>() || arg->Is<ResolvedCast>()) {
      if (!ValidateArgumentForUsing(node->join_expr())) {
        return false;
      }
      break;
    } else if (arg->Is<ResolvedFunctionCallBase>()) {
      if (!ValidateArgumentForUsing(arg.get())) {
        return false;
      }
    }
  }
  return true;
}

absl::Status SQLBuilder::VisitResolvedJoinScan(const ResolvedJoinScan* node) {
  absl::flat_hash_map<int, std::vector<int>> right_to_left_column_id_mapping;
  // Only build using if the scan has using and the join scan is of the correct
  // form.
  bool build_using = node->has_using() && ValidateUsingScan(node);

  if (build_using) {
    auto func = node->join_expr()->GetAs<ResolvedFunctionCallBase>();
    // Need this to pass CheckFieldsAccessed. Validator should already confirm
    // the shape of these things.
    GOOGLESQL_RET_CHECK(func->function()->Name() == "$equal" ||
              func->function()->Name() == "$and");
    std::set<int> left_column_ids;
    for (const auto& arg : func->argument_list()) {
      if (arg->Is<ResolvedFunctionCallBase>()) {
        auto sub_func = arg->GetAs<ResolvedFunctionCallBase>();
        GOOGLESQL_RET_CHECK(sub_func->function()->Name() == "$equal");
        // Mark accessed. This is safe because here we mark collation_list as
        // accessed only for JOIN USING clause which accepts only column
        // references, and doesn't explicitly list or change the collation.
        sub_func->collation_list();

        const ResolvedExpr* left_arg = sub_func->argument_list(0);
        const ResolvedExpr* right_arg = sub_func->argument_list(1);
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn left_col,
                         ExtractColumnFromArgumentForUsing(left_arg));
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn right_col,
                         ExtractColumnFromArgumentForUsing(right_arg));
        int left_id = left_col.column_id();
        int right_id = right_col.column_id();

        // Deparsing a statement with USING(...) that has duplicate left columns
        // is unsupported.
        if (left_column_ids.find(left_id) != left_column_ids.end()) {
          build_using = false;
          break;
        }
        left_column_ids.insert(left_id);
        if (right_to_left_column_id_mapping.contains(right_id)) {
          right_to_left_column_id_mapping[right_id].push_back(left_id);
        } else {
          right_to_left_column_id_mapping[right_id] = {left_id};
        }
      } else {
        const ResolvedExpr* right_arg = func->argument_list(1);
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn left_col,
                         ExtractColumnFromArgumentForUsing(arg.get()));
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn right_col,
                         ExtractColumnFromArgumentForUsing(right_arg));
        int left_id = left_col.column_id();
        int right_id = right_col.column_id();
        right_to_left_column_id_mapping[right_id] = {left_id};
        break;
      }
    }
    // Need to check collation list in case there is a non default value. The
    // collation list does not affect how the using clause is unparsed.
    GOOGLESQL_RET_CHECK(func->collation_list().empty() ||
              func->collation_list_size() > 0);
  }

  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  GOOGLESQL_ASSIGN_OR_RETURN(std::string left_join_operand,
                   GetJoinOperand(node->left_scan()));
  std::string right_join_operand;
  if (build_using) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        right_join_operand,
        GetJoinOperand(node->right_scan(), right_to_left_column_id_mapping));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(right_join_operand, GetJoinOperand(node->right_scan()));
  }

  if (IsPipeSyntaxTargetMode()) {
    right_join_operand = absl::StrCat(
        "(", StartsWithSelectOrFromOrWith(right_join_operand) ? "" : kFrom,
        std::move(right_join_operand), ")");
  }

  std::string hints = "";
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &hints));
  }

  std::string join_expr_sql;
  // A non-null `join_expr` in a non-USING join condition is necessary and
  // sufficient for a JOIN with ON.
  if (!build_using && node->join_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->join_expr()));
    absl::StrAppend(&join_expr_sql, " ON ", result->GetSQL());
  }

  if (build_using) {
    std::string using_names;
    auto func = node->join_expr()->GetAs<ResolvedFunctionCallBase>();
    for (const auto& arg : func->argument_list()) {
      if (arg->Is<ResolvedFunctionCallBase>()) {
        auto sub_func = arg->GetAs<ResolvedFunctionCallBase>();
        GOOGLESQL_RET_CHECK(sub_func->function()->Name() == "$equal");

        const ResolvedExpr* left_arg = sub_func->argument_list(0);
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn column,
                         ExtractColumnFromArgumentForUsing(left_arg));
        const std::string& alias_name = GetColumnAlias(column);
        if (!using_names.empty()) {
          absl::StrAppend(&using_names, ",");
        }
        absl::StrAppend(&using_names, alias_name);
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumn column,
                         ExtractColumnFromArgumentForUsing(arg.get()));
        const std::string& alias_name = GetColumnAlias(column);
        absl::StrAppend(&using_names, alias_name);
        break;
      }
    }
    absl::StrAppend(&join_expr_sql, " USING (", using_names, ")");
  }

  if (IsPipeSyntaxTargetMode() && absl::StartsWith(left_join_operand, "WITH")) {
    left_join_operand = absl::StrCat("(", std::move(left_join_operand), ")");
  }
  for (const auto& col_ref : node->parameter_list()) {
    col_ref->MarkFieldsAccessed();
  }

  std::string from;
  absl::StrAppend(
      &from, left_join_operand,
      IsPipeSyntaxTargetMode() ? QueryExpression::kPipe : " ",
      GetJoinTypeString(node->join_type(), node->join_expr() != nullptr), hints,
      " ");
  if (node->is_lateral()) {
    absl::StrAppend(&from, "LATERAL ");
  }
  absl::StrAppend(&from, right_join_operand, join_expr_sql);

  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedColumnHolder(
    const ResolvedColumnHolder* node) {
  PushQueryFragment(node, GetColumnPath(node->column()));
  return absl::OkStatus();
}

// TODO: b/236300834 - update sql builder to support multiple array elements.
absl::Status SQLBuilder::VisitResolvedArrayScan(const ResolvedArrayScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  std::string from;
  bool is_table_name_array_path =
      node->node_source() == kNodeSourceSingleTableArrayNamePath;
  if (is_table_name_array_path) {
    int table_column_id;
    GOOGLESQL_RET_CHECK(ContainsTableArrayNamePathWithFreeColumnRef(
        node->array_expr_list(0), &table_column_id));

    // Adding "table_name.column_name" to the table name array path expression.
    std::string table_column_name;
    const ResolvedTableScan* table_scan =
        node->input_scan()->GetAs<ResolvedTableScan>();
    const Table* table = table_scan->table();
    const std::vector<ResolvedColumn>& column_list = table_scan->column_list();
    for (int i = 0; i < column_list.size(); ++i) {
      if (table_column_id == column_list[i].column_id()) {
        const int column_index = table_scan->column_index_list(i);
        table_column_name = table->GetColumn(column_index)->FullName();
        break;
      }
    }
    absl::StrAppend(&from, table_column_name);

    // We can ignore building sql for input_scan.
    SQLBuilder throw_away_builder(options_);
    GOOGLESQL_RETURN_IF_ERROR(node->input_scan()->Accept(&throw_away_builder));

    if (node->array_expr_list(0)->node_kind() == RESOLVED_FLATTEN) {
      const auto* flatten_node =
          node->array_expr_list(0)->GetAs<ResolvedFlatten>();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> flatten_expr,
                       ProcessNode(flatten_node->expr()));

      // In reality, SQL text of `flatten_expr` could only be "column_alias" or
      // "column_alias.field_name[.field_name][...]". So we need to remove the
      // first identifier from the path of `flatten_expr` text.
      std::pair<std::string, std::string> flatten_splits =
          absl::StrSplit(flatten_expr->GetSQL(), absl::MaxSplits('.', 1));
      if (!flatten_splits.second.empty()) {
        absl::StrAppend(&from, ".", flatten_splits.second);
      }

      for (const std::unique_ptr<const ResolvedExpr>& get_field :
           flatten_node->get_field_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> get_field_fragment,
                         ProcessNode(get_field.get()));
        absl::StrAppend(&from, get_field_fragment->GetSQL());
      }
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> array_expr,
                       ProcessNode(node->array_expr_list(0)));

      std::pair<std::string, std::string> expr_splits =
          absl::StrSplit(array_expr->GetSQL(), absl::MaxSplits('.', 1));
      if (!expr_splits.second.empty()) {
        absl::StrAppend(&from, ".", expr_splits.second);
      }
    }
    absl::StrAppend(&from, " AS ", GetColumnAlias(node->element_column()));
  } else {
    if (node->input_scan() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string join_operand,
                       GetJoinOperand(node->input_scan()));
      absl::StrAppend(&from, join_operand,
                      IsPipeSyntaxTargetMode() ? QueryExpression::kPipe : " ",
                      node->is_outer() ? "LEFT " : "", "JOIN ");
    }

    bool is_multiway_enabled = options_.language_options.LanguageFeatureEnabled(
        FEATURE_MULTIWAY_UNNEST);
    absl::StrAppend(&from, "UNNEST(");
    for (int i = 0; i < node->array_expr_list_size(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> array_expr,
                       ProcessNode(node->array_expr_list(i)));
      absl::StrAppend(&from, i > 0 ? ", " : "", array_expr->GetSQL());
      if (is_multiway_enabled) {
        absl::StrAppend(&from, " AS ",
                        GetColumnAlias(node->element_column_list(i)));
      }
    }

    if (node->array_zip_mode() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> mode,
                       ProcessNode(node->array_zip_mode()));
      absl::StrAppend(&from, ", mode =>", mode->GetSQL());
    }
    absl::StrAppend(&from, ") ");

    // When multiway UNNEST language feature is disabled and we see a singleton
    // UNNEST, we omit "AS" and write `UNNEST(<expr>) <alias>` directly for
    // backward compatibility reason.
    if (!is_multiway_enabled) {
      GOOGLESQL_RET_CHECK(node->element_column_list_size() == 1);
      absl::StrAppend(&from, GetColumnAlias(node->element_column_list(0)));
    }

    if (node->array_offset_column() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(node->array_offset_column()));
      absl::StrAppend(&from, " WITH OFFSET ", result->GetSQL());
    }
    if (node->join_expr() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(node->join_expr()));
      absl::StrAppend(&from, " ON ", result->GetSQL());
    }
  }

  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedLimitOffsetScan(
    const ResolvedLimitOffsetScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  if (!query_expression->CanSetLimitClause()) {
    GOOGLESQL_RETURN_IF_ERROR(
        WrapQueryExpression(node->input_scan(), query_expression.get()));
  }
  if (node->limit() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->limit()));

    GOOGLESQL_RET_CHECK(query_expression->TrySetLimitClause(result->GetSQL()));
  } else {
    GOOGLESQL_RET_CHECK(query_expression->TrySetLimitClause("ALL"));
  }

  if (node->offset() != nullptr) {
    if (!query_expression->CanSetOffsetClause()) {
      GOOGLESQL_RETURN_IF_ERROR(
          WrapQueryExpression(node->input_scan(), query_expression.get()));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->offset()));
    GOOGLESQL_RET_CHECK(query_expression->TrySetOffsetClause(result->GetSQL()));
  }
  GOOGLESQL_RETURN_IF_ERROR(
      AddSelectListIfNeeded(node->column_list(), query_expression.get()));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

std::pair<std::string, std::string> GetOpTypePair(
    ResolvedRecursiveScan::RecursiveSetOperationType op_type) {
  switch (op_type) {
    case ResolvedRecursiveScan::UNION_ALL:
      return std::make_pair("UNION", "ALL");
    case ResolvedRecursiveScan::UNION_DISTINCT:
      return std::make_pair("UNION", "DISTINCT");
  }
}

std::pair<std::string, std::string> GetOpTypePair(
    ResolvedSetOperationScan::SetOperationType op_type) {
  switch (op_type) {
    case ResolvedSetOperationScan::UNION_ALL:
      return std::make_pair("UNION", "ALL");
    case ResolvedSetOperationScan::UNION_DISTINCT:
      return std::make_pair("UNION", "DISTINCT");
    case ResolvedSetOperationScan::INTERSECT_ALL:
      return std::make_pair("INTERSECT", "ALL");
    case ResolvedSetOperationScan::INTERSECT_DISTINCT:
      return std::make_pair("INTERSECT", "DISTINCT");
    case ResolvedSetOperationScan::EXCEPT_ALL:
      return std::make_pair("EXCEPT", "ALL");
    case ResolvedSetOperationScan::EXCEPT_DISTINCT:
      return std::make_pair("EXCEPT", "DISTINCT");
  }
}

static std::string GetSetOperationColumnMatchMode(
    ResolvedSetOperationScan::SetOperationColumnMatchMode column_match_mode) {
  switch (column_match_mode) {
    case ResolvedSetOperationScan::BY_POSITION:
      return "";
    case ResolvedSetOperationScan::CORRESPONDING:
      return "CORRESPONDING";
    case ResolvedSetOperationScan::CORRESPONDING_BY:
      return "CORRESPONDING BY";
  }
}

static bool HasDuplicateAliases(
    const QueryExpression::SQLAliasPairList& select_list) {
  absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      aliases;
  for (const auto& [unused, alias] : select_list) {
    if (!aliases.insert(alias).second) {
      return true;
    }
  }
  return false;
}

static bool HasDuplicateAliases(absl::Span<const absl::string_view> aliases) {
  absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      seen_aliases;
  for (const absl::string_view alias : aliases) {
    if (!seen_aliases.insert(alias).second) {
      return true;
    }
  }
  return false;
}

// Maps from column ids to their aliases. A column_id can be assigned with
// different column aliases for different occurrences, so the value type is
// a container.
using ColumnIdToAliasesMap =
    absl::flat_hash_map</*column_id*/ int,
                        /*alias*/ std::deque<absl::string_view>>;

// An ordered list of <column_id, alias>.
using ColumnAliasList = std::vector<std::pair</*column_id*/ int,
                                              /*alias*/ absl::string_view>>;

static absl::flat_hash_map<int, std::deque<absl::string_view>>
GetColumnIdToAliasMap(const ColumnAliasList& column_alias_list) {
  ColumnIdToAliasesMap id_to_aliases;
  for (auto [column_id, alias] : column_alias_list) {
    id_to_aliases[column_id].push_back(alias);
  }
  return id_to_aliases;
}

// Assigns the aliases in `id_to_aliases` to the columns in `scan_columns` by
// their column ids, and returns
// - a mapping from column_index to the assigned alias,
// - or std::nullopt if not all aliases in in `column_aliases` are assigned
//
// If a column_id corresponds to multiple column aliases in `id_to_aliases`,
// i.e. `id_to_aliases[column_id].size() > 1`, each alias is used once.
//
// `excluded_scan_column_indexes`: contains the column indexes of `scan_columns`
// that we will not assign aliases to.
static std::optional<absl::flat_hash_map<int, absl::string_view>>
AssignColumnAliasesByColumnId(
    const ResolvedColumnList& scan_columns, ColumnIdToAliasesMap id_to_aliases,
    const absl::flat_hash_set<int>& excluded_scan_column_indexes = {}) {
  absl::flat_hash_map<int, absl::string_view> new_aliases;
  for (int i = 0; i < scan_columns.size(); ++i) {
    if (excluded_scan_column_indexes.contains(i)) {
      continue;
    }
    auto provided_alias = id_to_aliases.find(scan_columns[i].column_id());
    if (provided_alias == id_to_aliases.end() ||
        provided_alias->second.empty()) {
      continue;
    }
    new_aliases[i] = provided_alias->second.front();
    provided_alias->second.pop_front();
  }

  for (const auto& [unused, remaining_aliases] : id_to_aliases) {
    if (!remaining_aliases.empty()) {
      return std::nullopt;
    }
  }
  return new_aliases;
}

// Similar to AssignColumnAliasByColumnId, assigns the aliases in
// `column_aliases` to the columns with the same column_id in `scan_columns`.
// REQUIRES: `column_aliases` are ordered by the order of the columns that
// showed up in the set operation item's output_column_list.
//
// For example, `column_aliases` = [(1, "A"), (2, "B")]. Then we must assign
// "A" to a column with column_id = 1 before assigning "B" to a column with
// column_id = 2.
//
// Returns
// - a mapping from column_index to the assigned alias,
// - or std::nullopt if not all aliases in in `column_aliases` are assigned.
static std::optional<absl::flat_hash_map<int, absl::string_view>>
AssignColumnAliasByColumnIdAndOrder(
    const ResolvedColumnList& scan_columns,
    absl::Span<const std::pair</*column_id*/ int,
                               /*alias*/ absl::string_view>>
        column_aliases) {
  absl::flat_hash_map</*scan column index*/ int, absl::string_view> new_aliases;
  // Index w.r.t. `column_aliases`. `column_aliases[next_alias_index]` is the
  // next alias to assign.
  int next_alias_index = 0;
  for (int i = 0;
       i < scan_columns.size() && next_alias_index < column_aliases.size();
       ++i) {
    const int output_column_id = column_aliases[next_alias_index].first;
    if (scan_columns[i].column_id() == output_column_id) {
      new_aliases[i] = column_aliases[next_alias_index].second;
      next_alias_index++;
    }
  }
  if (next_alias_index < column_aliases.size()) {
    return std::nullopt;
  }
  return new_aliases;
}

struct ColumnPathAndAlias {
  absl::string_view column_path;
  absl::string_view column_alias;
};

using ColumnPathAndAliasList = std::vector<ColumnPathAndAlias>;

// TODO: Replace the std::vector<std::pair</*column_path*/
// std::string, /*column_alias*/ std::string>> with `ColumnPathAndAlias`
// directly.
//
// Helper method to provide a copy of the `inputs` in the form of
// ColumnPathAndAliasList.
ColumnPathAndAliasList ToColumnPathAndAliasList(
    absl::Span<const std::pair</*column_path*/ std::string,
                               /*column_alias*/ std::string>>
        inputs) {
  ColumnPathAndAliasList column_path_and_alias_list;
  column_path_and_alias_list.reserve(inputs.size());
  for (const auto& [column_path, column_alias] : inputs) {
    column_path_and_alias_list.push_back(
        {.column_path = column_path, .column_alias = column_alias});
  }
  return column_path_and_alias_list;
}

// Returns an list of (column_id, alias), where column_id is from
// `output_column_list` and alias is from `final_column_list`.
static absl::StatusOr<std::vector<std::pair<int, absl::string_view>>>
GetColumnAliases(const ResolvedColumnList& output_column_list,
                 const ColumnPathAndAliasList& final_column_list) {
  GOOGLESQL_RET_CHECK_EQ(output_column_list.size(), final_column_list.size());
  std::vector<std::pair</*column_id*/ int, /*alias*/ absl::string_view>>
      column_aliases;
  column_aliases.reserve(final_column_list.size());
  for (int i = 0; i < output_column_list.size(); i++) {
    column_aliases.emplace_back(output_column_list[i].column_id(),
                                final_column_list[i].column_alias);
  }
  return column_aliases;
}

// Assigns aliases stored in `final_column_list` to columns in
// `scan_column_list`. Returns
// - a mapping from column index w.r.t `scan_column_list` to the assigned alias.
// - or std::nullopt if not all aliases are assigned. This usually indicates
//   `scan_column_list` misses columns in the original input scan.
// - or errors if any.
//
// `preserve_order`: if true, the column alias assignment order must be the
// same as the column order in the `output_column_list`. For example, assume
// `output_column_list` = [1, 2], where 1, 2 are column_ids. Then we must first
// assign an alias to a column with column_id = 1 before assigning an alias to
// a column with column_id = 2.
static absl::StatusOr<
    std::optional<absl::flat_hash_map<int, absl::string_view>>>
TryAssignAliasesNonFullCorresponding(
    const ResolvedColumnList& scan_column_list,
    const ResolvedColumnList& output_column_list,
    const ColumnPathAndAliasList& final_column_list, bool preserve_order) {
  std::vector<std::pair</*column_id*/ int, /*alias*/ absl::string_view>>
      column_aliases;
  GOOGLESQL_ASSIGN_OR_RETURN(column_aliases,
                   GetColumnAliases(output_column_list, final_column_list));
  if (preserve_order) {
    return AssignColumnAliasByColumnIdAndOrder(scan_column_list,
                                               column_aliases);
  } else {
    return AssignColumnAliasesByColumnId(scan_column_list,
                                         GetColumnIdToAliasMap(column_aliases));
  }
}

// This class stores the columns of a scan and provides a method to check
// whether a column belongs to the stored scan.
class ScanColumnChecker {
 public:
  explicit ScanColumnChecker(const ResolvedColumnList& scan_columns) {
    for (const ResolvedColumn& column : scan_columns) {
      scan_column_ids_.insert(column.column_id());
    }
  }

  bool ColumnInScan(int column_id) const {
    return scan_column_ids_.contains(column_id);
  }

  // Returns the column_aliases in `output_column_aliases[0, end)` that are of
  // the columns from the input scan.
  ColumnIdToAliasesMap GetScanColumnAliases(
      const ColumnAliasList& output_column_aliases, int end) const {
    ColumnIdToAliasesMap scan_column_aliases;
    for (int i = 0; i < end; ++i) {
      auto [column_id, alias] = output_column_aliases[i];
      if (!ColumnInScan(column_id)) {
        // This is a padded NULL column not from the scan, skip it.
        continue;
      }
      scan_column_aliases[column_id].push_back(alias);
    }
    return scan_column_aliases;
  }

 private:
  absl::flat_hash_set<int> scan_column_ids_;
};

// Tries assigning aliases stored in `output_column_aliases` to all columns in
// `scan_column_list`. Returns
// - a mapping from the column index w.r.t. `scan_column_list` to its assigned
//   alias.
// - or std::nullopt if not all aliases are assigned. This indicates the input
//   `scan_column_list` misses some columns of the original input scan (see more
//   explanation in the next paragraph).
// - or errors if any, for example when `first_seen_col_(start|end)` is invalid.
//
//  Due to b/36095506, not all aliases are assigned. For example, the resolved
//  AST for `SELECT DISTINCT key AS a, key AS b, value FROM KeyValue` is
//  ```
//  output_column_list = [a, b, value]
//  scan
//    column_list = [key, value]
//  ```
//  meaning `scan.column_list` misses one `key` column. As a result, not all
//  aliases in `output_column_list` can be assigned.
//
// `scan_column_list`: the columns to assign alias to.
// `output_column_aliases`: the aliases of all the columns this set operation
//  item outputs. Some columns are from the input scan, others are padded NULL
//  columns, as determined by `scan_column_checker`.
// `first_seen_col_start` and `first_seen_col_end`: [first_seen_col_start,
//   first_seen_col_end) of `output_column_aliases` represents the columns whose
//   name does not appear in previous queries.
static absl::StatusOr<
    std::optional<absl::flat_hash_map<int, absl::string_view>>>
TryAssignAliasesFullCorresponding(
    const ResolvedColumnList& scan_column_list,
    const ColumnAliasList& output_column_aliases, int first_seen_col_start,
    int first_seen_col_end, const ScanColumnChecker& scan_column_checker) {
  GOOGLESQL_RET_CHECK_LE(first_seen_col_start, first_seen_col_end);

  // The columns in `output_column_aliases` can be categorized into two parts:
  // - columns whose name has appeared in previous scans.
  // - columns whose name first shows up in the current scan.

  // First assign aliases to the columns in [first_seen_col_start,
  // first_seen_col_end). These columns have the aliases that do not show up in
  // previous queries, and their alias order will affect the final column order
  // of the resolved set operation scan, so we need to preserve the column order
  // in `output_column_aliases`.
  std::optional<absl::flat_hash_map<int, absl::string_view>>
      first_seen_column_index_to_alias;
  {
    // `output_column_aliases` in the half open range [first_seen_col_start,
    // first_seen_col_end) by definition should only contain columns from the
    // original query, so we use subvector directly.
    absl::Span<const std::pair</*column_id*/ int, /*alias*/ absl::string_view>>
        first_seen_column_aliases =
            absl::MakeSpan(output_column_aliases)
                .subspan(first_seen_col_start,
                         /*len=*/first_seen_col_end - first_seen_col_start);
    first_seen_column_index_to_alias = AssignColumnAliasByColumnIdAndOrder(
        scan_column_list, first_seen_column_aliases);
  }
  if (!first_seen_column_index_to_alias.has_value()) {
    return std::nullopt;
  }

  // The names of the columns in output_column_aliases[0, first_seen_col_start)
  // have appeared in previous queries, so they will not affect the column order
  // of the final column list. Assign aliases without worrying about the column
  // order in `output_column_aliases`.
  std::optional<absl::flat_hash_map<int, absl::string_view>>
      seen_column_index_to_alias;
  {
    ColumnIdToAliasesMap seen_column_aliases =
        scan_column_checker.GetScanColumnAliases(output_column_aliases,
                                                 /*end=*/first_seen_col_start);
    absl::flat_hash_set<int> indexes_already_assigned_alias;
    for (const auto [index, unused] : *first_seen_column_index_to_alias) {
      indexes_already_assigned_alias.insert(index);
    }
    seen_column_index_to_alias = AssignColumnAliasesByColumnId(
        scan_column_list, seen_column_aliases,
        /*excluded_scan_column_indexes=*/indexes_already_assigned_alias);
  }
  if (!seen_column_index_to_alias.has_value()) {
    return std::nullopt;
  }
  absl::flat_hash_map<int, absl::string_view> column_index_to_alias;
  column_index_to_alias.insert(first_seen_column_index_to_alias->begin(),
                               first_seen_column_index_to_alias->end());
  column_index_to_alias.insert(seen_column_index_to_alias->begin(),
                               seen_column_index_to_alias->end());
  // For FULL mode every column must be assigned with an alias because every
  // column shows up in the final column list of the set operation.
  GOOGLESQL_RET_CHECK_EQ(column_index_to_alias.size(), scan_column_list.size());
  return column_index_to_alias;
}

absl::StatusOr<const ResolvedScan*>
SQLBuilder::GetOriginalInputScanForCorresponding(const ResolvedScan* scan) {
  if (!scan->Is<ResolvedProjectScan>()) {
    return scan;
  }
  const ResolvedProjectScan* project_scan = scan->GetAs<ResolvedProjectScan>();
  if (project_scan->node_source() ==
      kNodeSourceResolverSetOperationCorresponding) {
    // This ProjectScan is added by the resolver to match columns. Its
    // `input_scan` is the original scan for the set operation.
    for (const std::unique_ptr<const ResolvedComputedColumn>& expr :
         project_scan->expr_list()) {
      // Visit `expr_list` otherwise `CheckFieldsAccessed` will fail.
      GOOGLESQL_RETURN_IF_ERROR(ProcessNode(expr.get()).status());
    }
    return project_scan->input_scan();
  }
  return scan;
}

// Returns the columns of `output_column_list` that belong to the input scan,
// as determined by `checker`, and populates the `column_index_to_alias`, which
// maps from the column indexes in the returned list to their column aliases.
static absl::StatusOr<ResolvedColumnList> GetScanColumnsFromOutputColumnList(
    const ResolvedColumnList& output_column_list,
    const ColumnAliasList& output_column_aliases,
    const ScanColumnChecker& checker,
    absl::flat_hash_map</*column index*/ int, /*alias*/ absl::string_view>&
        column_index_to_alias) {
  GOOGLESQL_RET_CHECK_EQ(output_column_list.size(), output_column_aliases.size());

  std::vector<ResolvedColumn> scan_columns;
  scan_columns.reserve(output_column_list.size());
  std::vector<absl::string_view> aliases;
  aliases.reserve(output_column_list.size());

  for (int i = 0; i < output_column_list.size(); ++i) {
    const ResolvedColumn& column = output_column_list[i];
    if (!checker.ColumnInScan(column.column_id())) {
      continue;
    }
    scan_columns.push_back(column);
    aliases.push_back(output_column_aliases[i].second);
  }
  for (int i = 0; i < aliases.size(); ++i) {
    column_index_to_alias[i] = aliases[i];
  }
  return scan_columns;
}

absl::Status SQLBuilder::RenameSetOperationItemsFullCorresponding(
    const ResolvedSetOperationScan* node,
    absl::Span<const std::pair<std::string, std::string>> final_column_list,
    std::vector<std::unique_ptr<QueryExpression>>& set_op_scan_list) {
  // `final_column_list` positionally matches and is equivalent to the
  // `output_column_list` of each set operation item. Their structures can be
  // viewed as having n partitions:
  // [<first_seen_columns_1>, <first_seen_columns_2>, ...,
  // <first_seen_columns_n>], where n is the number of scans.
  // <first_seen_columns_i> represents the columns whose names first appear
  // in the i-th scan of the set operation. <first_seen_columns_1> must be
  // non-empty, whereas other <first_seen_columns_i> can be empty.

  // Represents the start of the current <first_seen_columns>.
  int first_seen_column_start = 0;
  for (int query_idx = 0; query_idx < node->input_item_list_size();
       ++query_idx) {
    const ResolvedSetOperationItem* input_item =
        node->input_item_list(query_idx);
    GOOGLESQL_RET_CHECK_EQ(input_item->output_column_list_size(),
                 node->column_list_size());
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedScan* scan,
                     GetOriginalInputScanForCorresponding(input_item->scan()));

    // First unparse the scan of the current set operation item.
    std::unique_ptr<QueryExpression> scan_query_expression = nullptr;
    {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> scan_query_fragment,
                       ProcessNode(scan));
      scan_query_expression = std::move(scan_query_fragment->query_expression);
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(scan->column_list(),
                                            scan_query_expression.get()));
    }

    // Now we need to figure out which columns of the scan are selected, and
    // assign to them the corresponding aliases in `final_column_list`. The
    // selected columns can be categorized into two parts:
    // - <first_seen_columns>: Columns whose name first shows up in the current
    // scan.
    // - <non_first_seen_columns>: Columns whose name has appeared in previous
    // scans.
    const ResolvedColumnList& output_column_list =
        input_item->output_column_list();
    ScanColumnChecker checker(scan->column_list());

    // Calculate the end index (exclusive) of the <first_seen_columns> of this
    // scan by including all the following columns from this scan.
    // <non_first_seen_columns> are the non-placeholder
    // columns in output_column_list[0, first_seen_column_start).
    //
    // It is guaranteed that size_of(input_item::output_column_list) >=
    // size_of(scan::column_list).
    //
    // If a column in output_column_list[first_seen_column_start, max_size) does
    // not exist in scan::column_list, it's a NULL padded column in current
    // query item, and it has not shown up in any previous item or current item.
    int first_seen_column_end = first_seen_column_start;
    while (first_seen_column_end < output_column_list.size() &&
           checker.ColumnInScan(
               output_column_list[first_seen_column_end].column_id())) {
      ++first_seen_column_end;
    }

    // Now we know which columns of this scan are first seen, and which ones are
    // not, we can assign aliases to them based on this attribute.
    std::optional<
        absl::flat_hash_map</*column index*/ int, /*alias*/ absl::string_view>>
        column_index_to_alias;
    GOOGLESQL_ASSIGN_OR_RETURN(
        ColumnAliasList output_column_aliases,
        GetColumnAliases(output_column_list,
                         ToColumnPathAndAliasList(final_column_list)));
    GOOGLESQL_ASSIGN_OR_RETURN(
        column_index_to_alias,
        TryAssignAliasesFullCorresponding(
            scan->column_list(), output_column_aliases, first_seen_column_start,
            first_seen_column_end, checker));
    if (!column_index_to_alias.has_value()) {
      // Assignment failed; some aliases cannot be assigned to the columns
      // of the scan due to b/36095506. Fall back to using the scan columns in
      // `output_column_list` directly.
      column_index_to_alias.emplace();
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumnList columns_from_scan,
                       GetScanColumnsFromOutputColumnList(
                           output_column_list, output_column_aliases, checker,
                           *column_index_to_alias));
      GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(scan, scan_query_expression.get()));
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(columns_from_scan,
                                            scan_query_expression.get()));
    }
    GOOGLESQL_RET_CHECK_OK(
        scan_query_expression->SetAliasesForSelectList(*column_index_to_alias));
    set_op_scan_list.push_back(std::move(scan_query_expression));
    first_seen_column_start = first_seen_column_end;
  }
  return absl::OkStatus();
}

// Returns sublists containing only the columns from the input scan, as
// determined by `checker`, from `output_column_list` and `final_column_list`.
static std::pair<ResolvedColumnList, ColumnPathAndAliasList>
ExtractColumnsFromScan(
    const ResolvedColumnList& output_column_list,
    const ColumnPathAndAliasList& final_column_list,
    const ScanColumnChecker& checker,
    ResolvedSetOperationScan::SetOperationColumnPropagationMode
        column_propagation_mode) {
  ABSL_DCHECK_EQ(output_column_list.size(), final_column_list.size());
  if (column_propagation_mode == ResolvedSetOperationScan::INNER ||
      column_propagation_mode == ResolvedSetOperationScan::STRICT) {
    // INNER and STRICT mode do not have padded NULL columns.
    return std::make_pair(output_column_list, final_column_list);
  }
  ResolvedColumnList output_columns_from_scan;
  output_columns_from_scan.reserve(output_column_list.size());
  ColumnPathAndAliasList final_column_list_from_scan;
  final_column_list_from_scan.reserve(final_column_list.size());
  for (int i = 0; i < output_column_list.size(); ++i) {
    if (!checker.ColumnInScan(output_column_list[i].column_id())) {
      continue;
    }
    output_columns_from_scan.push_back(output_column_list[i]);
    final_column_list_from_scan.push_back(final_column_list[i]);
  }
  return std::make_pair(output_columns_from_scan, final_column_list_from_scan);
}

// Returns whether to assign aliases to all the columns of the `scan_index`-th
// input scan of the `node`.
static bool AllColumnsShouldBeAssignedAliases(
    const ResolvedSetOperationScan& node, int scan_index) {
  if (node.column_match_mode() != ResolvedSetOperationScan::BY_POSITION &&
      node.column_propagation_mode() == ResolvedSetOperationScan::STRICT) {
    // All columns should be assigned with an alias under STRICT mode.
    return true;
  }
  if (node.column_match_mode() == ResolvedSetOperationScan::CORRESPONDING &&
      node.column_propagation_mode() == ResolvedSetOperationScan::LEFT &&
      scan_index == 0) {
    // The first scan of LEFT CORRESPONDING should have aliases assigned to all
    // the columns.
    return true;
  }
  return false;
}

// Returns if the column alias assignment order should be the same as the column
// order in the output_column_list of the `scan_index`-th input item of the
// given `node`.
static bool AliasAssignmentShouldRespectTheOrderInOutputColumnList(
    const ResolvedSetOperationScan& node, int scan_index) {
  // For CORRESPONDING BY, the final column order is determined by the by list,
  // so even for the first item we don't need to preserve the order.
  return node.column_match_mode() == ResolvedSetOperationScan::CORRESPONDING &&
         scan_index == 0;
}

absl::Status SQLBuilder::RenameSetOperationItemsNonFullCorresponding(
    const ResolvedSetOperationScan* node,
    absl::Span<const std::pair<std::string, std::string>> final_column_list,
    std::vector<std::unique_ptr<QueryExpression>>& set_op_scan_list) {
  GOOGLESQL_RET_CHECK_NE(node, nullptr);
  // Unparse and rename each set operation item.
  for (int i = 0; i < node->input_item_list_size(); i++) {
    const ResolvedSetOperationItem* input_item = node->input_item_list(i);
    GOOGLESQL_RET_CHECK_EQ(input_item->output_column_list_size(),
                 node->column_list_size());
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedScan* scan,
                     GetOriginalInputScanForCorresponding(input_item->scan()));
    std::unique_ptr<QueryExpression> scan_query_expression = nullptr;
    {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> scan_query_fragment,
                       ProcessNode(scan));
      scan_query_expression = std::move(scan_query_fragment->query_expression);
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(scan->column_list(),
                                            scan_query_expression.get()));
    }

    // Exclude the padded NULL columns, if any, for LEFT mode. For INNER and
    // STRICT no columns are excluded.
    ScanColumnChecker checker(scan->column_list());
    const auto [output_columns_from_scan, final_column_list_from_scan] =
        ExtractColumnsFromScan(input_item->output_column_list(),
                               ToColumnPathAndAliasList(final_column_list),
                               checker, node->column_propagation_mode());
    // Normally `scan->column_list()` contains all the columns of the input
    // scan, so try using it as the scan columns first.
    std::optional<absl::flat_hash_map<int, absl::string_view>>
        assigned_new_aliases;
    GOOGLESQL_ASSIGN_OR_RETURN(assigned_new_aliases,
                     TryAssignAliasesNonFullCorresponding(
                         scan->column_list(), output_columns_from_scan,
                         final_column_list_from_scan,
                         AliasAssignmentShouldRespectTheOrderInOutputColumnList(
                             *node, /*scan_index=*/i)));
    absl::flat_hash_map</*column_index*/ int, absl::string_view> new_aliases;
    const ResolvedColumnList* scan_column_list;
    if (assigned_new_aliases.has_value()) {
      new_aliases = std::move(*assigned_new_aliases);
      scan_column_list = &scan->column_list();
    } else {
      // Some aliases are not assigned because `scan->column_list()` misses some
      // columns in `output_column_list`. This happens when SELECT DISTINCT is
      // used with duplicate column ids (see b/36095506). In this case we fall
      // back to using `output_column_list`.
      GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(scan, scan_query_expression.get()));
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(output_columns_from_scan,
                                            scan_query_expression.get()));
      for (int i = 0; i < output_columns_from_scan.size(); ++i) {
        new_aliases[i] = final_column_list_from_scan[i].column_alias;
      }
      scan_column_list = &output_columns_from_scan;
    }
    if (AllColumnsShouldBeAssignedAliases(*node, i)) {
      GOOGLESQL_RET_CHECK_EQ(new_aliases.size(), scan_column_list->size());
    }
    GOOGLESQL_RET_CHECK_OK(scan_query_expression->SetAliasesForSelectList(new_aliases));
    set_op_scan_list.push_back(std::move(scan_query_expression));
  }
  GOOGLESQL_RET_CHECK_GT(set_op_scan_list.size(), 0);
  return absl::OkStatus();
}

static std::string GetSetOperationColumnPropagationMode(
    ResolvedSetOperationScan::SetOperationColumnMatchMode column_match_mode,
    ResolvedSetOperationScan::SetOperationColumnPropagationMode
        column_propagation_mode) {
  if (column_match_mode == ResolvedSetOperationScan::BY_POSITION) {
    // Only STRICT is allowed for BY_POSITION, and we do not print it because
    // grammar does not allow it.
    return "";
  }
  switch (column_propagation_mode) {
    case ResolvedSetOperationScan::INNER:
      return "";
    case ResolvedSetOperationScan::FULL:
      return "FULL";
    case ResolvedSetOperationScan::LEFT:
      return "LEFT";
    case ResolvedSetOperationScan::STRICT:
      return "STRICT";
  }
}

absl::Status SQLBuilder::VisitResolvedSetOperationScan(
    const ResolvedSetOperationScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  std::vector<std::unique_ptr<QueryExpression>> set_op_scan_list;

  switch (node->column_match_mode()) {
    case ResolvedSetOperationScan::BY_POSITION: {
      // STRICT is the default and only-allowed `column_propagation_mode` for
      // BY_POSITION, and we do not print the keyword STRICT out in SQL
      // statement because the grammar does not allow it; we will ignore the
      // `column_propagation_mode()` field after the check.
      GOOGLESQL_RET_CHECK(node->column_propagation_mode() ==
                ResolvedSetOperationScan::STRICT);
      for (const auto& input_item : node->input_item_list()) {
        ABSL_DCHECK_EQ(input_item->output_column_list_size(),
                  node->column_list_size());
        const ResolvedScan* scan = input_item->scan();
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(scan));
        set_op_scan_list.push_back(std::move(result->query_expression));
        GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(input_item->output_column_list(),
                                              set_op_scan_list.back().get()));

        // If the query's column list doesn't exactly match the input to the
        // union, then add a wrapper query to make sure we have the right
        // number of columns and they have unique aliases.
        if (input_item->output_column_list() != scan->column_list()) {
          GOOGLESQL_RETURN_IF_ERROR(
              WrapQueryExpression(scan, set_op_scan_list.back().get()));
          GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(
              input_item->output_column_list(), set_op_scan_list.back().get()));
        }
      }
      // In a SetOperation scan, output columns use the column names from the
      // first subquery.
      ABSL_DCHECK_GT(set_op_scan_list.size(), 0);
      const SQLAliasPairList& first_select_list =
          set_op_scan_list[0]->SelectList();
      // If node->column_list() was empty, first_select_list will have a NULL
      // added.
      ABSL_DCHECK_EQ(first_select_list.size(),
                std::max<std::size_t>(node->column_list_size(), 1));
      for (int i = 0; i < node->column_list_size(); i++) {
        googlesql_base::InsertOrDie(&mutable_computed_column_alias(),
                         node->column_list(i).column_id(),
                         first_select_list[i].second);
      }
      break;
    }
    case ResolvedSetOperationScan::CORRESPONDING:
    case ResolvedSetOperationScan::CORRESPONDING_BY: {
      // Assign aliases to the final output columns.
      std::vector<
          std::pair</*column_path*/ std::string, /*column_alias*/ std::string>>
          final_column_list;
      final_column_list.reserve(node->column_list_size());
      for (const ResolvedColumn& column : node->column_list()) {
        std::string alias = ToIdentifierLiteral(GenerateUniqueAliasName());
        googlesql_base::InsertOrDie(&mutable_computed_column_alias(), column.column_id(),
                         alias);
        final_column_list.push_back(
            std::make_pair(GetColumnPath(column), alias));
      }
      query_expression->SetCorrespondingSetOpOutputColumnList(
          final_column_list);
      if (node->column_match_mode() ==
              ResolvedSetOperationScan::CORRESPONDING &&
          node->column_propagation_mode() == ResolvedSetOperationScan::FULL) {
        GOOGLESQL_RETURN_IF_ERROR(RenameSetOperationItemsFullCorresponding(
            node, final_column_list, set_op_scan_list));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(RenameSetOperationItemsNonFullCorresponding(
            node, final_column_list, set_op_scan_list));
      }
      break;
    }
  }

  std::string query_hints;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &query_hints));
  }

  const auto& pair = GetOpTypePair(node->op_type());
  GOOGLESQL_RET_CHECK(query_expression->TrySetSetOpScanList(
      &set_op_scan_list, pair.first, pair.second,
      GetSetOperationColumnMatchMode(node->column_match_mode()),
      GetSetOperationColumnPropagationMode(node->column_match_mode(),
                                           node->column_propagation_mode()),
      query_hints, /*with_depth_modifier=*/""));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedOrderByItem(
    const ResolvedOrderByItem* node) {
  std::string text;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->column_ref()));
  absl::StrAppend(&text, result->GetSQL());

  if (node->collation_name() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> collation,
                     ProcessNode(node->collation_name()));
    absl::StrAppend(&text, " COLLATE ", collation->GetSQL());
  }

  // Mark field accessed. Collation doesn't have its own SQL clause.
  node->collation();

  absl::StrAppend(&text, node->is_descending() ? " DESC" : "");
  if (node->null_order() != ResolvedOrderByItemEnums::ORDER_UNSPECIFIED) {
    absl::StrAppend(&text,
                    node->null_order() == ResolvedOrderByItemEnums::NULLS_FIRST
                        ? " NULLS FIRST"
                        : " NULLS LAST");
  }
  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedComputedColumn(
    const ResolvedComputedColumn* node) {
  return node->expr()->Accept(this);
}

absl::Status SQLBuilder::VisitResolvedDeferredComputedColumn(
    const ResolvedDeferredComputedColumn* node) {
  // The side effect column does not shows up in the generated SQL.
  // The ResolvedAST is presumed valid, so no need to validate it here.
  // Only a dummy access is needed.
  node->side_effect_column();
  return node->expr()->Accept(this);
}

absl::Status SQLBuilder::VisitResolvedOrderByScan(
    const ResolvedOrderByScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  // Wrap the input scan to avoid losing columns only used by order-by items
  // but not the select list.
  GOOGLESQL_RETURN_IF_ERROR(
      WrapQueryExpression(node->input_scan(), query_expression.get()));

  std::string order_by_hint_list;
  GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &order_by_hint_list));

  GOOGLESQL_RETURN_IF_ERROR(
      AddSelectListIfNeeded(node->column_list(), query_expression.get()));
  std::vector<std::string> order_by_list;
  for (const auto& order_by_item : node->order_by_item_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> item_result,
                     ProcessNode(order_by_item.get()));
    order_by_list.push_back(item_result->GetSQL());
  }
  GOOGLESQL_RET_CHECK(
      query_expression->TrySetOrderByClause(order_by_list, order_by_hint_list));

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

class SideEffectColRefCollector : public ResolvedASTVisitor {
 public:
  static bool ExpressionReferencesSideEffectColumns(const ResolvedExpr* expr) {
    SideEffectColRefCollector collector;
    absl::Status status = expr->Accept(&collector);
    GOOGLESQL_DCHECK_OK(status);
    return collector.references_side_effect_columns_;
  }

  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override {
    if (node->function()->IsGoogleSQLBuiltin(FN_WITH_SIDE_EFFECTS)) {
      references_side_effect_columns_ = true;
      // No need to continue recursing.
      return absl::OkStatus();
    }
    return node->ChildrenAccept(this);
  }

 private:
  explicit SideEffectColRefCollector() = default;

  bool references_side_effect_columns_ = false;
};

// The anonymization rewrite with conditional evaluation may introduce an extra
// aggregation, which could result in an unbuildable ResolvedAST (i.e., one that
// is inexpressible in SQL)
// To differentiate those rewritten ASTs from the authentic ones (which by
// definition are buildable since they were expressed in SQL by the user), we
// look at a deferred aggregations on the current AnonymizedScan that themselves
// are deferring computations from a deferred inner aggregate.
//
// Note: we may need to expand this condition if other scans could come in-
// between due to the rewrite.
static absl::Status CheckNoSuccessiveAggregateScansWithDeferredColumns(
    const ResolvedAggregateScanBase* node) {
  if (!node->input_scan()->Is<ResolvedAggregateScanBase>() &&
      !node->input_scan()->Is<ResolvedSampleScan>() &&
      !node->input_scan()->Is<ResolvedJoinScan>()) {
    return absl::OkStatus();
  }
  for (const auto& outer_agg : node->aggregate_list()) {
    if (outer_agg->Is<ResolvedDeferredComputedColumn>() &&
        SideEffectColRefCollector::ExpressionReferencesSideEffectColumns(
            outer_agg->GetAs<ResolvedDeferredComputedColumn>()->expr())) {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "SqlBuilder cannot generate SQL for ResolvedASTs rewritten "
                "by the anonymization rewriter when enforcing conditional "
                "evaluation at node: "
             << node->DebugString();
    }
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessAggregateScanBase(
    const ResolvedAggregateScanBase* node, bool is_differencial_privacy_scan,
    absl::flat_hash_map<int, int> grouping_column_id_map,
    QueryExpression* query_expression) {
  std::vector<int> rollup_column_id_list;
  GroupingSetIdsInfo grouping_set_ids_info;
  if (!is_differencial_privacy_scan) {
    GOOGLESQL_RETURN_IF_ERROR(ConvertGroupingSetIdsInfo(
        node->grouping_set_list(), node->rollup_column_list(),
        rollup_column_id_list, grouping_set_ids_info));
  }
  if (!query_expression->CanSetGroupByClause()) {
    GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(node->input_scan(), query_expression));
  }

  GOOGLESQL_RETURN_IF_ERROR(CheckNoSuccessiveAggregateScansWithDeferredColumns(node));

  std::map<int, std::string> group_by_list;
  int64_t pending_columns_before_grouping_columns = pending_columns().size();
  for (const auto& computed_col : node->group_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    googlesql_base::InsertOrDie(&group_by_list, computed_col->column().column_id(),
                     result->GetSQL());
    int group_by_col_id = computed_col->column().column_id();
    for (const auto& [grouping_col_id, grouping_argument_group_by_col_id] :
         grouping_column_id_map) {
      if (grouping_argument_group_by_col_id == group_by_col_id) {
        googlesql_base::InsertOrDie(&mutable_pending_columns(), grouping_col_id,
                         absl::StrCat("GROUPING(", result->GetSQL(), ")"));
      }
    }
  }
  // If we haven't added the same number of pending columns as there are
  // GROUPING function calls, throw error.
  GOOGLESQL_RET_CHECK(pending_columns().size() -
                pending_columns_before_grouping_columns ==
            grouping_column_id_map.size());

  for (const auto& collation : node->collation_list()) {
    // Mark collation_list field as accessed, because collation doesn't have its
    // own SQL clause.
    collation.CollationName();
  }

  for (const auto& computed_column : node->aggregate_list()) {
    const auto* computed_col =
        computed_column->GetAs<ResolvedComputedColumnImpl>();

    // The side effect column does not shows up in the generated SQL.
    // The ResolvedAST is presumed valid, so no need to validate it here.
    // Only a dummy access is needed.
    if (computed_col->Is<ResolvedDeferredComputedColumn>()) {
      computed_col->GetAs<ResolvedDeferredComputedColumn>()
          ->side_effect_column();
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    googlesql_base::InsertOrDie(&mutable_pending_columns(),
                     computed_col->column().column_id(), result->GetSQL());
  }

  std::string group_by_hints;
  GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &group_by_hints));

  GOOGLESQL_ASSIGN_OR_RETURN(bool has_grouping_func, HasGroupingCallNode(node));
  bool has_group_by_all = googlesql_base::ContainsKeyValuePair(
      options_.target_syntax_map, node, SQLBuildTargetSyntax::kGroupByAll);
  if (has_group_by_all && !has_grouping_func) {
    GOOGLESQL_RETURN_IF_ERROR(
        query_expression->SetGroupByAllClause(group_by_list, group_by_hints));
  } else {
    GOOGLESQL_RET_CHECK(query_expression->TrySetGroupByClause(
        group_by_list, group_by_hints, grouping_set_ids_info,
        rollup_column_id_list));
  }
  if (!scans_to_collapse_.contains(node)) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddSelectListIfNeeded(node->column_list(), query_expression));
    // For queries that have only aggregate columns but no group-by columns, eg.
    // `select count(*) from table`, the following condition become true, and we
    // capture that information in the query_expression so that it can be used
    // to output correct SQL in Pipe syntax mode.
    if (group_by_list.empty() && query_expression->HasSelectClause()) {
      GOOGLESQL_RETURN_IF_ERROR(query_expression->SetGroupByOnlyAggregateColumns(true));
    }
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAggregateScan(
    const ResolvedAggregateScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  absl::flat_hash_map<int, int> grouping_column_id_map =
      CreateGroupingColumnIdMap(node->grouping_call_list());

  GOOGLESQL_RETURN_IF_ERROR(
      ProcessAggregateScanBase(node, /*is_differencial_privacy_scan=*/false,
                               grouping_column_id_map, query_expression.get()));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAnonymizedAggregateScan(
    const ResolvedAnonymizedAggregateScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  GOOGLESQL_RETURN_IF_ERROR(ProcessAggregateScanBase(
      node, /*is_differencial_privacy_scan=*/true,
      /*grouping_column_id_map=*/{}, query_expression.get()));

  // We handle the WITH ANONYMIZATION clause *after* processing the
  // AggregateScan, because the AggregateScan might introduce a new
  // QueryExpression and we need to ensure that this clause is added
  // to the QueryExpression related to the AggregateScan.
  std::string anonymization_options_sql = "WITH ANONYMIZATION ";
  GOOGLESQL_RETURN_IF_ERROR(AppendOptions(node->anonymization_option_list(),
                                &anonymization_options_sql));
  GOOGLESQL_RET_CHECK(query_expression->TrySetWithAnonymizationClause(
      anonymization_options_sql));

  PushSQLForQueryExpression(node, query_expression.release());

  // The k_threshold is not mapped back to sql, so we can safely ignore it.
  if (node->k_threshold_expr() != nullptr) {
    node->k_threshold_expr()->MarkFieldsAccessed();
  }

  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDifferentialPrivacyAggregateScan(
    const ResolvedDifferentialPrivacyAggregateScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  GOOGLESQL_RETURN_IF_ERROR(ProcessAggregateScanBase(
      node, /*is_differencial_privacy_scan=*/true,
      /*grouping_column_id_map=*/{}, query_expression.get()));

  // We handle the WITH DIFFERENTIAL_PRIVACY clause *after* processing the
  // AggregateScan, because the AggregateScan might introduce a new
  // QueryExpression and we need to ensure that this clause is added
  // to the QueryExpression related to the AggregateScan.
  std::string options_sql = "WITH DIFFERENTIAL_PRIVACY";
  GOOGLESQL_RETURN_IF_ERROR(AppendOptions(node->option_list(), &options_sql));
  GOOGLESQL_RET_CHECK(query_expression->TrySetWithAnonymizationClause(options_sql));

  PushSQLForQueryExpression(node, query_expression.release());

  // The group_selection_threshold_expr is not mapped back to sql, so we can
  // safely ignore it.
  if (node->group_selection_threshold_expr() != nullptr) {
    SQLBuilder temp_builder(options_);
    GOOGLESQL_RETURN_IF_ERROR(
        node->group_selection_threshold_expr()->Accept(&temp_builder));
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAggregationThresholdAggregateScan(
    const ResolvedAggregationThresholdAggregateScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  absl::flat_hash_map<int, int> grouping_column_id_map =
      CreateGroupingColumnIdMap(node->grouping_call_list());

  GOOGLESQL_RETURN_IF_ERROR(
      ProcessAggregateScanBase(node, /*is_differencial_privacy_scan=*/false,
                               grouping_column_id_map, query_expression.get()));

  // We handle the WITH AGGREGATION_THRESHOLD clause *after* processing the
  // AggregateScan, because the AggregateScan might introduce a new
  // QueryExpression and we need to ensure that this clause is added
  // to the QueryExpression related to the AggregateScan.
  std::string options_sql = "WITH AGGREGATION_THRESHOLD";
  GOOGLESQL_RETURN_IF_ERROR(AppendOptions(node->option_list(), &options_sql));
  GOOGLESQL_RET_CHECK(query_expression->TrySetWithAnonymizationClause(options_sql));

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

static std::optional<const ResolvedRecursiveScan*> MaybeGetRecursiveScan(
    const ResolvedWithEntry* entry) {
  const ResolvedScan* query = entry->with_subquery();
  for (;;) {
    switch (query->node_kind()) {
      case RESOLVED_RECURSIVE_SCAN:
        return query->GetAs<ResolvedRecursiveScan>();
      case RESOLVED_WITH_SCAN:
        query = query->GetAs<ResolvedWithScan>()->query();
        break;
      default:
        return std::nullopt;
    }
  }
}

void SQLBuilder::AddPlaceholderRecursiveScan(absl::string_view query_name) {
  state_.recursive_query_info.push({std::string(query_name), nullptr});
}

absl::Status SQLBuilder::VisitResolvedWithScan(const ResolvedWithScan* node) {
  // Save state of the WITH alias map from the outer scope so we can restore it
  // after processing the local query.
  const std::map<std::string, const ResolvedScan*> old_with_query_name_to_scan =
      state_.with_query_name_to_scan;

  SQLAliasPairList with_list;
  bool has_recursive_entries = false;
  for (const auto& with_entry : node->with_entry_list()) {
    const std::string name = with_entry->with_query_name();
    const ResolvedScan* scan = with_entry->with_subquery();
    googlesql_base::InsertOrDie(&state_.with_query_name_to_scan, name, scan);
    std::optional<const ResolvedRecursiveScan*> recursive_scan =
        MaybeGetRecursiveScan(with_entry.get());
    std::string modifier_sql;
    if (recursive_scan.has_value()) {
      has_recursive_entries = true;
      AddPlaceholderRecursiveScan(ToIdentifierLiteral(name));
      const ResolvedRecursiveScan* scan = recursive_scan.value();

      if (scan->recursion_depth_modifier() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(auto modifier,
                         ProcessNode(scan->recursion_depth_modifier()));
        modifier_sql = modifier->GetSQL();
      }
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result, ProcessNode(scan));
    std::unique_ptr<QueryExpression> query_expression =
        std::move(result->query_expression);
    GOOGLESQL_RET_CHECK(query_expression != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(
        AddSelectListIfNeeded(scan->column_list(), query_expression.get()));

    with_list.push_back(
        std::make_pair(ToIdentifierLiteral(name),
                       absl::StrCat("(", query_expression->GetSQLQuery(), ")",
                                    std::move(modifier_sql))));

    SetPathForColumnList(scan->column_list(), ToIdentifierLiteral(name));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->query()));
  std::unique_ptr<QueryExpression> query_expression(
      result->query_expression.release());
  GOOGLESQL_RET_CHECK(query_expression != nullptr);

  // If the body of the WITH query is another WITH query, the WITH clause will
  // already be set, so we need to wrap it to avoid setting it twice.
  // TODO: Investigate this code more closely to determine if
  // there's a better way to fix this.
  if (query_expression->HasWithClause()) {
    GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(node, query_expression.get()));
  }
  GOOGLESQL_RET_CHECK(
      query_expression->TrySetWithClause(with_list, has_recursive_entries));
  PushSQLForQueryExpression(node, query_expression.release());

  state_.with_query_name_to_scan = old_with_query_name_to_scan;
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedWithRefScan(
    const ResolvedWithRefScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  const std::string alias = GetScanAlias(node);
  std::string from;
  absl::StrAppend(&from, ToIdentifierLiteral(node->with_query_name()), " AS ",
                  alias);
  const ResolvedScan* with_scan =
      googlesql_base::FindOrDie(state_.with_query_name_to_scan, node->with_query_name());
  GOOGLESQL_RET_CHECK_EQ(node->column_list_size(), with_scan->column_list_size());
  for (int i = 0; i < node->column_list_size(); ++i) {
    googlesql_base::InsertIfNotPresent(&mutable_computed_column_alias(),
                            node->column_list(i).column_id(),
                            GetColumnAlias(with_scan->column_list(i)));
  }
  SetPathForColumnList(node->column_list(), alias);
  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::GetSqlForSample(
    const ResolvedSampleScan* node, bool is_gql) {
  std::string sample = " TABLESAMPLE ";
  GOOGLESQL_RET_CHECK(!node->method().empty());
  absl::StrAppend(&sample, node->method(), " (");

  GOOGLESQL_RET_CHECK(node->size() != nullptr);
  if (node->size()->node_kind() == RESOLVED_LITERAL) {
    const Value value = node->size()->GetAs<ResolvedLiteral>()->value();
    GOOGLESQL_RET_CHECK(!value.is_null());
    GOOGLESQL_ASSIGN_OR_RETURN(
        const std::string value_sql,
        GetSQL(value, options_.language_options.product_mode(),
               options_.use_external_float32, true /* is_constant_value */));
    absl::StrAppend(&sample, value_sql);
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> size,
                     ProcessNode(node->size()));
    absl::StrAppend(&sample, size->GetSQL());
  }

  if (node->unit() == ResolvedSampleScan::ROWS) {
    absl::StrAppend(&sample, " ROWS");
    if (!node->partition_by_list().empty()) {
      absl::StrAppend(&sample, " PARTITION BY ");
      GOOGLESQL_RETURN_IF_ERROR(
          GetPartitionByListString(node->partition_by_list(), &sample));
    }
    absl::StrAppend(&sample, ")");
  } else {
    GOOGLESQL_RET_CHECK_EQ(node->unit(), ResolvedSampleScan::PERCENT);
    absl::StrAppend(&sample, " PERCENT)");
  }

  if (node->weight_column() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->weight_column()));
    absl::StrAppend(&sample, " WITH WEIGHT ", is_gql ? "AS " : "",
                    result->GetSQL());
  }

  if (node->repeatable_argument() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> repeatable_argument,
                     ProcessNode(node->repeatable_argument()));
    absl::StrAppend(&sample, " REPEATABLE(", repeatable_argument->GetSQL(),
                    ")");
  }
  return sample;
}

absl::Status SQLBuilder::VisitResolvedSampleScan(
    const ResolvedSampleScan* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input_result,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      input_result->query_expression.release());

  std::string from_clause;
  switch (node->input_scan()->node_kind()) {
    case RESOLVED_TABLE_SCAN: {
      // As we know the from clause here is just the table name, we will just
      // append the TABLESAMPLE clause at the end.
      from_clause = query_expression->FromClause();

      // If the PARTITION BY is present, then the column references in the
      // PARTITION BY must reference the table scan columns directly by name
      // (the PARTITION BY expressions cannot reference aliases defined in the
      // SELECT list).  So we must make columns from the input_scan available
      // to partition by node processing below.  This call will associate the
      // original table/column names with the associated column ids (so that
      // they can be referenced in the PARTITION BY).
      const ResolvedTableScan* resolved_table_scan =
          node->input_scan()->GetAs<ResolvedTableScan>();
      GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(
          node->input_scan(),
          googlesql_base::FindWithDefault(table_alias_map(),
                               resolved_table_scan->table())));
      break;
    }
    case RESOLVED_JOIN_SCAN: {
      // We explicitly parenthesize joins here so that later we can append
      // sample clause to it.
      // No need to parenthesize in Pipe syntax mode.
      from_clause =
          IsPipeSyntaxTargetMode()
              ? query_expression->FromClause()
              : absl::StrCat("(", query_expression->FromClause(), ")");
      break;
    }
    case RESOLVED_WITH_REF_SCAN: {
      // Ideally the ResolvedWithRefScan would be treated like the
      // ResolvedTableScan, so the FROM clause could just be the WITH table
      // name (and alias) with the TABLESAMPLE clause appended to the end.
      // However, the ProcessNode() call for ResolvedWithRefScan creates
      // a table alias for the WITH table and column aliases for its
      // columns which are subsequently referenced.  So we need to turn the
      // ResolvedWithRefScan into a subquery that produces all the WITH table
      // columns and also contains the sample clause.
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(node->input_scan()->column_list(),
                                            query_expression.get()));
      from_clause = query_expression->FromClause();
      break;
    }
    default: {
      GOOGLESQL_RETURN_IF_ERROR(
          WrapQueryExpression(node->input_scan(), query_expression.get()));
      from_clause = query_expression->FromClause();
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::string sample, GetSqlForSample(node, /*is_gql=*/false));

  if (node->weight_column() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(node->input_scan()->column_list(),
                                          query_expression.get()));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->weight_column()));
    query_expression->AppendSelectColumn(result->GetSQL() /* select column */,
                                         result->GetSQL() /* select alias */);
  }

  query_expression->SetFromClause(absl::StrCat(
      from_clause, IsPipeSyntaxTargetMode() ? QueryExpression::kPipe : " ",
      sample));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::MatchOutputColumns(
    absl::Span<const std::unique_ptr<const ResolvedOutputColumn>>
        output_column_list,
    const ResolvedScan* query, QueryExpression* query_expression) {
  ResolvedColumnList column_list;
  for (const auto& output_col : output_column_list) {
    column_list.push_back(output_col->column());
  }

  // Check whether columns in column_list and columns in query_expression's
  // select-list matches 1:1 in order.
  bool matches = false;
  if (column_list.size() == query_expression->SelectList().size()) {
    matches = true;
    // Stores the earliest occurring alias for the column-id while traversing
    // through the select-list.
    std::map<int /* column_id */, std::string /* alias */> select_aliases;
    for (int i = 0; i < column_list.size() && matches; ++i) {
      const ResolvedColumn& output_col = column_list[i];
      // Alias assigned for the column in select-list. If the column occurs more
      // than once in the select-list, we fetch the alias from the column's
      // earliest occurrence, which is registered to refer that column later in
      // the query.
      std::string select_alias =
          googlesql_base::LookupOrInsert(&select_aliases, output_col.column_id(),
                              query_expression->SelectList()[i].second);

      // As all the aliases (assigned through GetColumnAlias()) are unique
      // across column-id, to see whether the column in select-list and the
      // output_col are the same, we compare select_alias to the alias unique to
      // the output_col.
      matches = (select_alias == GetColumnAlias(output_col));
    }
  }

  if (!matches) {
    // When the select-list does not match the output-columns, we wrap the
    // earlier query_expression as a subquery to the from clause, selecting
    // columns matching the output_column_list.
    GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(query, query_expression));
    GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(column_list, query_expression));
  }

  GOOGLESQL_RET_CHECK_EQ(output_column_list.size(),
               query_expression->SelectList().size());
  absl::flat_hash_map<int, std::string> final_aliases;
  absl::flat_hash_set<std::string> output_column_aliases;
  for (int i = 0; i < output_column_list.size(); ++i) {
    const absl::string_view output_col_alias = output_column_list[i]->name();
    if (!IsInternalAlias(output_col_alias)) {
      final_aliases[i] = ToIdentifierLiteral(output_col_alias);
      output_column_aliases.insert(final_aliases[i]);
    }
  }
  // Assign an new alias for internal alias columns whose generated aliases
  // were used in the output list names.
  for (int i = 0; i < output_column_list.size(); ++i) {
    const absl::string_view output_col_alias = output_column_list[i]->name();
    std::string select_list_alias = query_expression->SelectList()[i].second;
    if (IsInternalAlias(output_col_alias) &&
        output_column_aliases.contains(select_list_alias)) {
      std::string new_alias = GenerateUniqueAliasName();
      final_aliases[i] = new_alias;
      output_column_aliases.insert(new_alias);
    }
  }
  absl::flat_hash_map<int, absl::string_view> final_aliases_view;
  for (const auto& [index, alias] : final_aliases) {
    final_aliases_view[index] = alias;
  }

  // For aggregation queries in Pipe syntax mode, if there are duplicate
  // aliases, we must wrap the query so that it does not throw errors due to
  // ambiguous aliases.
  if (IsPipeSyntaxTargetMode() &&
      query->node_kind() == RESOLVED_AGGREGATE_SCAN &&
      HasDuplicateAliases(final_aliases_view)) {
    GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(query, query_expression));
    GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(column_list, query_expression));
  }

  return query_expression->SetAliasesForSelectList(final_aliases_view);
}

absl::StatusOr<std::unique_ptr<QueryExpression>> SQLBuilder::ProcessQuery(
    const ResolvedScan* query,
    absl::Span<const std::unique_ptr<const ResolvedOutputColumn>>
        output_column_list) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result, ProcessNode(query));
  std::unique_ptr<QueryExpression> query_expression(
      result->query_expression.release());
  GOOGLESQL_RET_CHECK(query_expression != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      AddSelectListIfNeeded(query->column_list(), query_expression.get()));
  GOOGLESQL_RETURN_IF_ERROR(
      MatchOutputColumns(output_column_list, query, query_expression.get()));
  return query_expression;
}

absl::Status SQLBuilder::VisitResolvedQueryStmt(const ResolvedQueryStmt* node) {
  // Dummy access on the output_column_list so as to pass the final
  // CheckFieldsAccessed() on a statement level before building the sql.
  for (const auto& output_col : node->output_column_list()) {
    output_col->name();
    output_col->column();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> query_expression,
                   ProcessQuery(node->query(), node->output_column_list()));

  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }
  // We should get a struct or a proto if is_value_table=true.
  if (node->is_value_table()) {
    GOOGLESQL_RET_CHECK_EQ(query_expression->SelectList().size(), 1);
    query_expression->SetSelectAsModifier("AS VALUE");
  }
  absl::StrAppend(&sql, query_expression->GetSQLQuery());

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGeneralizedQueryStmt(
    const ResolvedGeneralizedQueryStmt* node) {
  // TODO Implement SQLBuilder for these.  We can't
  // currently generate them without using subqueries around pipe operators
  // like FORK that don't work in subqueries.
  GOOGLESQL_RET_CHECK_FAIL() << "SQLBuilder for generalized statements not supported yet";
}

absl::Status SQLBuilder::VisitResolvedMultiStmt(const ResolvedMultiStmt* node) {
  std::string sql;
  for (const auto& stmt : node->statement_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(stmt.get()));
    absl::StrAppend(&sql, result->GetSQL(), ";\n");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateWithEntryStmt(
    const ResolvedCreateWithEntryStmt* node) {
  // TODO Implement SQLBuilder for these.  We can't
  // currently generate them without using subqueries around pipe operators
  // like FORK that don't work in subqueries.
  GOOGLESQL_RET_CHECK_FAIL() << "SQLBuilder for generalized statements not supported yet";
}

absl::Status SQLBuilder::VisitResolvedExplainStmt(
    const ResolvedExplainStmt* node) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }

  ABSL_DCHECK(node->statement() != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->statement()));
  absl::StrAppend(&sql, "EXPLAIN ", result->GetSQL());

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

static std::string GetSqlSecuritySql(
    ResolvedCreateStatementEnums::SqlSecurity sql_security) {
  switch (sql_security) {
    case ResolvedCreateStatementEnums::SQL_SECURITY_UNSPECIFIED:
      return "";
    case ResolvedCreateStatementEnums::SQL_SECURITY_INVOKER:
      return " SQL SECURITY INVOKER";
    case ResolvedCreateStatementEnums::SQL_SECURITY_DEFINER:
      return " SQL SECURITY DEFINER";
  }
}

static std::string GetExternalSecuritySql(
    ResolvedCreateStatementEnums::SqlSecurity external_security) {
  switch (external_security) {
    case ResolvedCreateStatementEnums::SQL_SECURITY_UNSPECIFIED:
      return "";
    case ResolvedCreateStatementEnums::SQL_SECURITY_INVOKER:
      return " EXTERNAL SECURITY INVOKER ";
    case ResolvedCreateStatementEnums::SQL_SECURITY_DEFINER:
      return " EXTERNAL SECURITY DEFINER ";
  }
}

static std::string GetDeterminismLevelSql(
    ResolvedCreateStatementEnums::DeterminismLevel level) {
  switch (level) {
    case ResolvedCreateStatementEnums::DETERMINISM_UNSPECIFIED:
      return "";
    case ResolvedCreateStatementEnums::DETERMINISM_DETERMINISTIC:
      return " DETERMINISTIC";
    case ResolvedCreateStatementEnums::DETERMINISM_NOT_DETERMINISTIC:
      return " NOT DETERMINISTIC";
    case ResolvedCreateStatementEnums::DETERMINISM_IMMUTABLE:
      return " IMMUTABLE";
    case ResolvedCreateStatementEnums::DETERMINISM_STABLE:
      return " STABLE";
    case ResolvedCreateStatementEnums::DETERMINISM_VOLATILE:
      return " VOLATILE";
  }
}

absl::Status SQLBuilder::GetOptionalColumnNameWithOptionsList(
    const ResolvedCreateViewBase* node, std::string* sql) {
  if (node->has_explicit_columns()) {
    absl::StrAppend(sql, "(");
    for (auto iter = node->column_definition_list().begin();
         iter != node->column_definition_list().end(); ++iter) {
      absl::StrAppend(sql, ToIdentifierLiteral(iter->get()->name()));
      if (iter->get()->annotations() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(AppendOptionsIfPresent(
            iter->get()->annotations()->option_list(), sql));
      }
      if (ABSL_PREDICT_TRUE(iter != node->column_definition_list().end() - 1)) {
        absl::StrAppend(sql, ",");
      }
    }
    absl::StrAppend(sql, ")");
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::MaybeSetupRecursiveView(
    const ResolvedCreateViewBase* node) {
  if (node->query()->node_kind() != RESOLVED_RECURSIVE_SCAN) {
    return absl::OkStatus();
  }
  // Generate text to refer to the recursive view reference.
  std::string query_name = absl::StrJoin(
      node->name_path(), ".", [](std::string* out, absl::string_view name) {
        absl::StrAppend(out, ToIdentifierLiteral(name));
      });
  const auto* recursive_scan = node->query()->GetAs<ResolvedRecursiveScan>();
  GOOGLESQL_RET_CHECK(recursive_scan->recursion_depth_modifier() == nullptr)
      << "Recursive view should NOT have recursion depth modifier.";
  AddPlaceholderRecursiveScan(query_name);

  // Force the actual column names to be used against the recursive table;
  // we cannot use generated column names with an outer SELECT wrapper, as
  // that wrapper would violate the form that recursive queries must follow,
  // preventing the unparsed string from resolving.
  GOOGLESQL_RET_CHECK_EQ(recursive_scan->column_list_size(),
               recursive_scan->non_recursive_term()->output_column_list_size());
  for (int i = 0; i < recursive_scan->column_list_size(); ++i) {
    const ResolvedColumn recursive_query_column =
        recursive_scan->column_list(i);
    const ResolvedColumn nonrecursive_term_column =
        recursive_scan->non_recursive_term()->output_column_list(i);
    GOOGLESQL_RET_CHECK_EQ(recursive_query_column.name(),
                 nonrecursive_term_column.name());
    googlesql_base::InsertOrDie(&mutable_computed_column_alias(),
                     recursive_query_column.column_id(),
                     recursive_query_column.name());
    if (nonrecursive_term_column.column_id() !=
        recursive_query_column.column_id()) {
      googlesql_base::InsertOrDie(&mutable_computed_column_alias(),
                       nonrecursive_term_column.column_id(),
                       recursive_query_column.name());
    }
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::GetCreateViewStatement(
    const ResolvedCreateViewBase* node, bool is_value_table,
    const std::string& view_type) {
  // Dummy access on the fields so as to pass the final CheckFieldsAccessed() on
  // a statement level before building the sql.
  for (const auto& output_col : node->output_column_list()) {
    output_col->name();
    output_col->column();
  }
  for (const auto& column_def : node->column_definition_list()) {
    column_def->name();
    column_def->type();
    MarkColumnAnnotationsAccessed(column_def->annotations());
  }

  GOOGLESQL_RETURN_IF_ERROR(MaybeSetupRecursiveView(node));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> query_expression,
                   ProcessQuery(node->query(), node->output_column_list()));

  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, view_type, &sql));
  GOOGLESQL_RETURN_IF_ERROR(GetOptionalColumnNameWithOptionsList(node, &sql));

  absl::StrAppend(&sql, GetSqlSecuritySql(node->sql_security()));

  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, "OPTIONS(", result, ") ");
  }
  // We should get a struct or a proto if is_value_table=true.
  if (is_value_table) {
    GOOGLESQL_RET_CHECK_EQ(query_expression->SelectList().size(), 1);
    query_expression->SetSelectAsModifier("AS VALUE");
  }
  absl::StrAppend(&sql, " AS ", query_expression->GetSQLQuery());

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::GetCreateStatementPrefix(
    const ResolvedCreateStatement* node, absl::string_view object_type,
    std::string* sql) {
  bool is_index = object_type == "INDEX";
  sql->clear();
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), sql));
    absl::StrAppend(sql, " ");
  }
  absl::StrAppend(sql, "CREATE ");
  if (node->create_mode() == node->CREATE_OR_REPLACE) {
    absl::StrAppend(sql, "OR REPLACE ");
  }
  if (is_index) {
    const auto* create_index = node->GetAs<ResolvedCreateIndexStmt>();
    GOOGLESQL_RET_CHECK(create_index);
    if (create_index->is_unique()) {
      absl::StrAppend(sql, "UNIQUE ");
    }
    if (create_index->is_search()) {
      absl::StrAppend(sql, "SEARCH ");
    }
    if (create_index->is_vector()) {
      absl::StrAppend(sql, "VECTOR ");
    }
  } else {
    switch (node->create_scope()) {
      case ResolvedCreateStatement::CREATE_PRIVATE:
        absl::StrAppend(sql, "PRIVATE ");
        break;
      case ResolvedCreateStatement::CREATE_PUBLIC:
        absl::StrAppend(sql, "PUBLIC ");
        break;
      case ResolvedCreateStatement::CREATE_TEMP:
        absl::StrAppend(sql, "TEMP ");
        break;
      case ResolvedCreateStatement::CREATE_DEFAULT_SCOPE:
        break;
    }
  }
  if (node->node_kind() == RESOLVED_CREATE_MATERIALIZED_VIEW_STMT) {
    absl::StrAppend(sql, "MATERIALIZED ");
  }
  if (object_type == "VIEW" &&
      node->GetAs<ResolvedCreateViewBase>()->recursive()) {
    absl::StrAppend(sql, "RECURSIVE ");
  }
  absl::StrAppend(sql, object_type, " ");
  if (node->create_mode() == node->CREATE_IF_NOT_EXISTS) {
    absl::StrAppend(sql, "IF NOT EXISTS ");
  }
  absl::StrAppend(sql, IdentifierPathToString(node->name_path()), " ");
  return absl::OkStatus();
}

absl::Status SQLBuilder::GetPartitionByListString(
    absl::Span<const std::unique_ptr<const ResolvedExpr>> partition_by_list,
    std::string* sql) {
  ABSL_DCHECK(!partition_by_list.empty());

  std::vector<std::string> expressions;
  expressions.reserve(partition_by_list.size());
  for (const auto& partition_by_expr : partition_by_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                     ProcessNode(partition_by_expr.get()));
    expressions.push_back(expr->GetSQL());
  }
  absl::StrAppend(sql, absl::StrJoin(expressions, ","));

  return absl::OkStatus();
}

absl::Status SQLBuilder::GetTableAndColumnInfoList(
    absl::Span<const std::unique_ptr<const ResolvedTableAndColumnInfo>>
        table_and_column_info_list,
    std::string* sql) {
  std::vector<std::string> expressions;
  expressions.reserve(table_and_column_info_list.size());
  for (const auto& table_and_column_info : table_and_column_info_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                     ProcessNode(table_and_column_info.get()));
    expressions.push_back(expr->GetSQL());
  }
  absl::StrAppend(sql, absl::StrJoin(expressions, ","));

  return absl::OkStatus();
}

std::string SQLBuilder::GetOptionalObjectType(absl::string_view object_type) {
  if (object_type.empty()) {
    return "";
  } else {
    return absl::StrCat(ToIdentifierLiteral(object_type), " ");
  }
}

absl::Status SQLBuilder::GetPrivilegesString(
    const ResolvedGrantOrRevokeStmt* node, std::string* sql) {
  std::vector<std::string> privilege_list_sql;
  for (const auto& privilege : node->privilege_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(privilege.get()));
    privilege_list_sql.push_back(result->GetSQL());
  }

  *sql = absl::StrCat(
      (privilege_list_sql.empty() ? "ALL PRIVILEGES"
                                  : absl::StrJoin(privilege_list_sql, ", ")),
      " ON ", GetOptionalObjectType(node->object_type_list()[0]),
      IdentifierPathToString(node->name_path()));

  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateDatabaseStmt(
    const ResolvedCreateDatabaseStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "CREATE DATABASE ");
  absl::StrAppend(&sql, IdentifierPathToString(node->name_path()));
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

static std::string GetColumnListSql(
    const std::vector<std::string>& column_names) {
  return absl::StrCat("(", absl::StrJoin(column_names, ","), ")");
}

static std::string GetColumnListSql(
    absl::Span<const int> column_index_list,
    const std::function<std::string(int)>& get_name) {
  std::vector<std::string> column_names;
  column_names.reserve(column_index_list.size());
  for (auto index : column_index_list) {
    column_names.push_back(get_name(index));
  }
  return GetColumnListSql(column_names);
}

static std::string GetForeignKeyMatchSql(ResolvedForeignKey::MatchMode mode) {
  switch (mode) {
    case ResolvedForeignKey::SIMPLE:
      return "SIMPLE";
    case ResolvedForeignKey::FULL:
      return "FULL";
    case ResolvedForeignKey::NOT_DISTINCT:
      return "NOT DISTINCT";
  }
}

static std::string GetForeignKeyActionSql(
    ResolvedForeignKey::ActionOperation action) {
  switch (action) {
    case ResolvedForeignKey::NO_ACTION:
      return "NO ACTION";
    case ResolvedForeignKey::RESTRICT:
      return "RESTRICT";
    case ResolvedForeignKey::CASCADE:
      return "CASCADE";
    case ResolvedForeignKey::SET_NULL:
      return "SET NULL";
  }
}

absl::StatusOr<std::string> SQLBuilder::ProcessForeignKey(
    const ResolvedForeignKey* foreign_key, bool is_if_not_exists) {
  // We don't need the referencing column offsets here.
  foreign_key->MarkFieldsAccessed();
  std::string sql;
  if (!foreign_key->constraint_name().empty()) {
    absl::StrAppend(&sql, "CONSTRAINT ");
    if (is_if_not_exists) {
      absl::StrAppend(&sql, " IF NOT EXISTS ");
    }
    absl::StrAppend(&sql, foreign_key->constraint_name(), " ");
  }
  std::vector<std::string> referencing_columns;
  for (const std::string& referencing_column :
       foreign_key->referencing_column_list()) {
    referencing_columns.push_back(referencing_column);
  }
  absl::StrAppend(&sql, "FOREIGN KEY", GetColumnListSql(referencing_columns),
                  " ");
  absl::StrAppend(
      &sql, "REFERENCES ", foreign_key->referenced_table()->Name(),
      GetColumnListSql(
          foreign_key->referenced_column_offset_list(),
          [&foreign_key](int i) {
            return foreign_key->referenced_table()->GetColumn(i)->Name();
          }),
      " ");
  absl::StrAppend(&sql, "MATCH ",
                  GetForeignKeyMatchSql(foreign_key->match_mode()), " ");
  absl::StrAppend(&sql, "ON UPDATE ",
                  GetForeignKeyActionSql(foreign_key->update_action()), " ");
  absl::StrAppend(&sql, "ON DELETE ",
                  GetForeignKeyActionSql(foreign_key->delete_action()), " ");
  if (!foreign_key->enforced()) {
    absl::StrAppend(&sql, "NOT ");
  }
  absl::StrAppend(&sql, "ENFORCED");
  GOOGLESQL_RETURN_IF_ERROR(AppendOptionsIfPresent(foreign_key->option_list(), &sql));

  return sql;
}

absl::StatusOr<std::string> SQLBuilder::ProcessPrimaryKey(
    const ResolvedPrimaryKey* resolved_primary_key) {
  GOOGLESQL_RET_CHECK(resolved_primary_key != nullptr);
  // We don't access column_offset_list here.
  resolved_primary_key->MarkFieldsAccessed();

  std::string primary_key = "PRIMARY KEY";
  absl::StrAppend(&primary_key,
                  GetColumnListSql(resolved_primary_key->column_name_list()));
  if (resolved_primary_key->unenforced()) {
    absl::StrAppend(&primary_key, " NOT ENFORCED");
  }
  GOOGLESQL_RETURN_IF_ERROR(AppendOptionsIfPresent(resolved_primary_key->option_list(),
                                         &primary_key));

  return primary_key;
}

absl::Status SQLBuilder::ProcessTableElementsBase(
    std::string* sql,
    absl::Span<const std::unique_ptr<const ResolvedColumnDefinition>>
        column_definition_list,
    const ResolvedPrimaryKey* resolved_primary_key,
    absl::Span<const std::unique_ptr<const ResolvedForeignKey>>
        foreign_key_list,
    absl::Span<const std::unique_ptr<const ResolvedCheckConstraint>>
        check_constraint_list) {
  if (!column_definition_list.empty()) {
    absl::StrAppend(sql, "(");
  }
  std::vector<std::string> table_elements;
  // Go through each column and generate the SQL corresponding to it.
  for (const auto& c : column_definition_list) {
    std::string table_element =
        absl::StrCat(ToIdentifierLiteral(c->name()), " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(
        c->type(), c->is_hidden(), c->annotations(), c->generated_column_info(),
        c->default_value(), &table_element));
    table_elements.push_back(std::move(table_element));
  }
  if (resolved_primary_key != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string primary_key,
                     ProcessPrimaryKey(resolved_primary_key));
    table_elements.push_back(primary_key);
  }
  for (const auto& fk : foreign_key_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string foreign_key,
                     ProcessForeignKey(fk.get(), /*is_if_not_exists=*/false));
    table_elements.push_back(foreign_key);
  }
  for (const auto& check_constraint : check_constraint_list) {
    std::string check_constraint_sql;
    if (!check_constraint->constraint_name().empty()) {
      absl::StrAppend(&check_constraint_sql, "CONSTRAINT ",
                      check_constraint->constraint_name(), " ");
    }
    absl::StrAppend(&check_constraint_sql, "CHECK (");
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(check_constraint->expression()));
    absl::StrAppend(&check_constraint_sql, result->GetSQL(), ") ");
    if (!check_constraint->enforced()) {
      absl::StrAppend(&check_constraint_sql, "NOT ");
    }
    absl::StrAppend(&check_constraint_sql, "ENFORCED");
    GOOGLESQL_RETURN_IF_ERROR(AppendOptionsIfPresent(check_constraint->option_list(),
                                           &check_constraint_sql));
    table_elements.push_back(check_constraint_sql);
  }
  absl::StrAppend(sql, absl::StrJoin(table_elements, ", "));
  if (!column_definition_list.empty()) {
    absl::StrAppend(sql, ")");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::ProcessCreateTableStmtBase(
    const ResolvedCreateTableStmtBase* node, bool process_column_definitions,
    absl::string_view table_type) {
  std::string sql;

  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, table_type, &sql));

  const bool like_table_name_empty = node->like_table() == nullptr;
  if (!like_table_name_empty) {
    // Dummy access the column_definition_list
    for (const auto& column_definition : node->column_definition_list()) {
      column_definition->type();
      MarkColumnAnnotationsAccessed(column_definition->annotations());
    }

    absl::StrAppend(&sql, "LIKE ");
    absl::StrAppend(&sql, node->like_table()->Name());
  }
  // Make column aliases available for PARTITION BY, CLUSTER BY and table
  // constraints.
  for (const auto& column_definition : node->column_definition_list()) {
    mutable_computed_column_alias()[column_definition->column().column_id()] =
        column_definition->name();
  }
  for (const ResolvedColumn& column : node->pseudo_column_list()) {
    mutable_computed_column_alias()[column.column_id()] = column.name();
  }
  if (process_column_definitions && like_table_name_empty) {
    GOOGLESQL_RETURN_IF_ERROR(ProcessTableElementsBase(
        &sql, node->column_definition_list(), node->primary_key(),
        node->foreign_key_list(), node->check_constraint_list()));
  }

  if (node->collation_name() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> collation,
                     ProcessNode(node->collation_name()));
    absl::StrAppend(&sql, " DEFAULT COLLATE ", collation->GetSQL());
  }

  return sql;
}

absl::Status SQLBuilder::VisitResolvedCreateConnectionStmt(
    const ResolvedCreateConnectionStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "CONNECTION", &sql));

  GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                   GetHintListString(node->option_list()));
  absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateSequenceStmt(
    const ResolvedCreateSequenceStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "SEQUENCE", &sql));

  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateSchemaStmt(
    const ResolvedCreateSchemaStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "SCHEMA", &sql));
  if (node->collation_name() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> collation,
                     ProcessNode(node->collation_name()));
    absl::StrAppend(&sql, " DEFAULT COLLATE ", collation->GetSQL());
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateExternalSchemaStmt(
    const ResolvedCreateExternalSchemaStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "EXTERNAL SCHEMA", &sql));

  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                   GetHintListString(node->option_list()));
  absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendCloneDataSource(const ResolvedScan* source,
                                               std::string* sql) {
  // since CLONE DATA does not support Pipe syntax, we set the target syntax
  // mode to Standard, and restore it at the end of this function.
  TargetSyntaxMode target_syntax_mode = options_.target_syntax_mode;
  this->options_.target_syntax_mode = TargetSyntaxMode::kStandard;
  absl::Cleanup restore_target_syntax_mode = [this, target_syntax_mode] {
    this->options_.target_syntax_mode = target_syntax_mode;
  };

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result, ProcessNode(source));
  std::string from = result->GetSQL();
  // Strip away the " FROM " generated by QueryExpression::GetSQLQuery()
  absl::StrAppend(sql, absl::StripPrefix(from, kFrom));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateTableStmt(
    const ResolvedCreateTableStmt* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string sql,
      ProcessCreateTableStmtBase(node, /*process_column_definitions=*/true,
                                 /*table_type=*/"TABLE"));

  if (node->clone_from() != nullptr) {
    absl::StrAppend(&sql, " CLONE ");
    GOOGLESQL_RETURN_IF_ERROR(AppendCloneDataSource(node->clone_from(), &sql));
  }
  if (node->copy_from() != nullptr) {
    absl::StrAppend(&sql, " COPY ");
    GOOGLESQL_RETURN_IF_ERROR(AppendCloneDataSource(node->copy_from(), &sql));
  }

  if (!node->partition_by_list().empty()) {
    absl::StrAppend(&sql, " PARTITION BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->partition_by_list(), &sql));
  }

  if (!node->cluster_by_list().empty()) {
    absl::StrAppend(&sql, " CLUSTER BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->cluster_by_list(), &sql));
  }

  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }

  if (!node->option_list().empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateSnapshotTableStmt(
    const ResolvedCreateSnapshotTableStmt* node) {
  std::string sql;

  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "SNAPSHOT TABLE", &sql));

  absl::StrAppend(&sql, " CLONE ");
  GOOGLESQL_RETURN_IF_ERROR(AppendCloneDataSource(node->clone_from(), &sql));

  if (!node->option_list().empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateTableAsSelectStmt(
    const ResolvedCreateTableAsSelectStmt* node) {
  return VisitResolvedCreateTableAsSelectStmtImpl(node,
                                                  /*generate_as_pipe=*/false);
}

absl::Status SQLBuilder::VisitResolvedCreateTableAsSelectStmtImpl(
    const ResolvedCreateTableAsSelectStmt* node, bool generate_as_pipe) {
  // Dummy access on the fields so as to pass the final CheckFieldsAccessed() on
  // a statement level before building the sql.
  for (const auto& output_col : node->output_column_list()) {
    output_col->name();
    output_col->column();
  }

  // Print the column definition list only if it contains more information
  // than the output column list.
  bool process_column_definitions =
      (node->primary_key() != nullptr || !node->foreign_key_list().empty() ||
       !node->check_constraint_list().empty());
  if (!process_column_definitions) {
    for (const auto& column_def : node->column_definition_list()) {
      column_def->name();  // Mark accessed.
      column_def->type();  // Mark accessed.
      if (column_def->annotations() != nullptr || column_def->is_hidden() ||
          column_def->generated_column_info() != nullptr ||
          column_def->default_value() != nullptr) {
        process_column_definitions = true;
        break;
      }
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::string sql,
                   ProcessCreateTableStmtBase(node, process_column_definitions,
                                              /* table_type = */ "TABLE"));

  if (!node->partition_by_list().empty()) {
    absl::StrAppend(&sql, " PARTITION BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->partition_by_list(), &sql));
  }

  if (!node->cluster_by_list().empty()) {
    absl::StrAppend(&sql, " CLUSTER BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->cluster_by_list(), &sql));
  }

  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }

  if (!node->option_list().empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }

  // The query isn't present when this occurs in PipeCreateTableScan.
  if (node->query() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> query_expression,
                     ProcessQuery(node->query(), node->output_column_list()));

    // We should get a struct or a proto if is_value_table=true.
    if (node->is_value_table()) {
      GOOGLESQL_RET_CHECK_EQ(query_expression->SelectList().size(), 1);
      query_expression->SetSelectAsModifier(" AS VALUE");
    }
    std::string query_sql = query_expression->GetSQLQuery();
    if (generate_as_pipe) {
      sql = absl::StrCat(query_sql, QueryExpression::kPipe, sql);
    } else {
      absl::StrAppend(&sql, " AS ", query_sql);
    }
  } else {
    GOOGLESQL_RET_CHECK(!generate_as_pipe);
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateModelStmt(
    const ResolvedCreateModelStmt* node) {
  // Dummy access on the fields so as to pass the final CheckFieldsAccessed() on
  // a statement level before building the sql.
  for (const auto& output_col : node->output_column_list()) {
    output_col->name();
    output_col->column();
  }
  for (const auto& column_definition : node->transform_input_column_list()) {
    column_definition->name();
    column_definition->column();
    column_definition->type();
    if (column_definition->annotations() != nullptr) {
      column_definition->annotations()->MarkFieldsAccessed();
    }
  }

  std::string sql;
  // Restore CREATE MODEL sql prefix.
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "MODEL", &sql));

  // Restore INPUT and OUTPUT clause.
  if (!node->input_column_definition_list().empty()) {
    absl::StrAppend(&sql, " INPUT");
    GOOGLESQL_RETURN_IF_ERROR(ProcessTableElementsBase(
        &sql, node->input_column_definition_list(), {}, {}, {}));
  }
  if (!node->output_column_definition_list().empty()) {
    absl::StrAppend(&sql, " OUTPUT");
    GOOGLESQL_RETURN_IF_ERROR(ProcessTableElementsBase(
        &sql, node->output_column_definition_list(), {}, {}, {}));
  }

  // Restore SELECT statement.
  // Note this step must run before Restore TRANSFORM clause to fill
  // computed_column_alias_.
  std::unique_ptr<QueryExpression> query_expression;
  if (node->query() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(query_expression,
                     ProcessQuery(node->query(), node->output_column_list()));
  }

  // Restore TRANSFORM clause.
  if (!node->transform_list().empty()) {
    ABSL_DCHECK_EQ(node->transform_list_size(),
              node->transform_output_column_list_size());
    ABSL_DCHECK_EQ(node->output_column_list_size(),
              node->transform_input_column_list_size());
    absl::flat_hash_map<std::string, /*column_id=*/int>
        query_column_name_id_map;
    for (const auto& query_column_definition :
         node->transform_input_column_list()) {
      query_column_name_id_map.insert(
          {query_column_definition->name(),
           query_column_definition->column().column_id()});
    }
    // Rename columns in TRANSFORM with the aliases from SELECT statement.
    std::map</*column_id=*/int, std::string> renamed_computed_column_alias;
    for (const auto& output_col : node->output_column_list()) {
      const int output_col_id = output_col->column().column_id();
      const std::string alias = output_col->name();
      if (!googlesql_base::ContainsKey(computed_column_alias(), output_col_id)) {
        return ::googlesql_base::InternalErrorBuilder() << absl::Substitute(
                   "Column id $0 with name '$1' is not found in "
                   "computed_column_alias_",
                   output_col_id, alias);
      }
      if (!query_column_name_id_map.contains(alias)) {
        return ::googlesql_base::InternalErrorBuilder() << absl::Substitute(
                   "Column id $0 with name '$1' is not found in "
                   "query_column_name_id_map",
                   output_col_id, alias);
      }
      renamed_computed_column_alias.insert(
          {query_column_name_id_map[alias], alias});
    }
    mutable_computed_column_alias().swap(renamed_computed_column_alias);
    for (const auto& analytic_function_group :
         node->transform_analytic_function_group_list()) {
      GOOGLESQL_RETURN_IF_ERROR(
          VisitResolvedAnalyticFunctionGroup(analytic_function_group.get()));
    }

    // Assemble TRANSFORM clause sql string.
    std::vector<std::string> transform_list_strs;
    for (int i = 0; i < node->transform_list_size(); ++i) {
      const ResolvedComputedColumn* transform_element = node->transform_list(i);
      const ResolvedOutputColumn* transform_output_col =
          node->transform_output_column_list(i);

      // Dummy access.
      transform_output_col->column();
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<QueryFragment> transform_element_fragment,
          ProcessNode(transform_element));
      std::string expr_sql = transform_element_fragment->GetSQL();
      // The column name of ResolvedComputedColumn is always set as alias. Even
      // when the original TRANSFORM is 'a', the unparsed sql will be 'a AS a'.
      absl::StrAppend(&expr_sql, " AS ", transform_output_col->name());
      transform_list_strs.push_back(expr_sql);
    }
    absl::StrAppend(&sql, " TRANSFORM(",
                    absl::StrJoin(transform_list_strs, ", "), ")");
  }

  // Restore REMOTE.
  if (node->is_remote()) {
    absl::StrAppend(&sql, " REMOTE");
  }

  // Restore WITH CONNECTION.
  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, " WITH CONNECTION ", connection_alias, " ");
  }

  // Restore OPTIONS list.
  if (!node->option_list().empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }

  // Append AS aliased query list.
  if (!node->aliased_query_list().empty()) {
    std::vector<std::string> aliased_query_list_strs;
    for (const auto& resolved_aliased_query : node->aliased_query_list()) {
      std::string aliased_query_sql;
      absl::StrAppend(&aliased_query_sql, resolved_aliased_query->alias());
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<QueryExpression> subquery_expression,
          ProcessQuery(resolved_aliased_query->query(),
                       resolved_aliased_query->output_column_list()));
      absl::StrAppend(&aliased_query_sql, " AS (",
                      subquery_expression->GetSQLQuery(), ")");
      aliased_query_list_strs.push_back(aliased_query_sql);
    }
    absl::StrAppend(&sql, " AS (", absl::StrJoin(aliased_query_list_strs, ","),
                    ")");
  } else if (query_expression) {
    // Append SELECT statement.
    absl::StrAppend(&sql, " AS ", query_expression->GetSQLQuery());
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateIndexStmt(
    const ResolvedCreateIndexStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "INDEX", &sql));
  absl::StrAppend(&sql, "ON ");
  absl::StrAppend(&sql, IdentifierPathToString(node->table_name_path()));

  const ResolvedTableScan* table_scan = node->table_scan();
  GOOGLESQL_RET_CHECK(table_scan != nullptr);
  // Dummy access so that we can pass CheckFieldsAccessed().
  GOOGLESQL_RET_CHECK(table_scan->table() != nullptr);

  if (table_scan->table()->IsValueTable()) {
    if (table_scan->column_list_size() > 0) {
      // Set the path of the value column as the table name. This is consistent
      // with the spec in (broken link).
      SetPathForColumn(table_scan->column_list(0), table_scan->table()->Name());
      for (int i = 1; i < table_scan->column_list_size(); i++) {
        SetPathForColumn(
            table_scan->column_list(i),
            ToIdentifierLiteral(table_scan->column_list(i).name()));
      }
    }
  } else {
    for (const ResolvedColumn& column : table_scan->column_list()) {
      SetPathForColumn(column, ToIdentifierLiteral(column.name()));
    }
  }

  if (!node->unnest_expressions_list().empty()) {
    absl::StrAppend(&sql, "\n");
    for (const auto& index_unnest_expression :
         node->unnest_expressions_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(index_unnest_expression->array_expr()));
      absl::StrAppend(&sql, "UNNEST(", result->GetSQL(), ") ",
                      ToIdentifierLiteral(
                          index_unnest_expression->element_column().name()));
      SetPathForColumn(index_unnest_expression->element_column(),
                       ToIdentifierLiteral(
                           index_unnest_expression->element_column().name()));
      if (index_unnest_expression->array_offset_column() != nullptr) {
        absl::StrAppend(
            &sql, " WITH OFFSET ",
            ToIdentifierLiteral(index_unnest_expression->array_offset_column()
                                    ->column()
                                    .name()));
        SetPathForColumn(
            index_unnest_expression->array_offset_column()->column(),
            ToIdentifierLiteral(index_unnest_expression->array_offset_column()
                                    ->column()
                                    .name()));
      }
      absl::StrAppend(&sql, "\n");
    }
  }

  absl::StrAppend(&sql, "(");
  std::vector<std::string> cols;
  absl::flat_hash_map</*column_id=*/int, const ResolvedExpr*>
      computed_column_expressions;
  for (const auto& computed_column : node->computed_columns_list()) {
    computed_column_expressions.insert(
        {computed_column->column().column_id(), computed_column->expr()});
  }
  for (const auto& item : node->index_item_list()) {
    const int column_id = item->column_ref()->column().column_id();
    const ResolvedExpr* resolved_expr =
        googlesql_base::FindPtrOrNull(computed_column_expressions, column_id);
    if (resolved_expr == nullptr) {
      // The index key is on the table column, array element column, or offset
      // column.
      std::string col_string =
          absl::StrCat(GetColumnPath(item->column_ref()->column()),
                       item->descending() ? " DESC" : "");
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                       GetHintListString(item->option_list()));
      if (!options_string.empty()) {
        absl::StrAppend(&col_string, " OPTIONS(", options_string, ") ");
      }
      cols.push_back(col_string);
      continue;
    }
    // The index key is an extracted expression.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(resolved_expr));
    std::string col_string =
        absl::StrCat(result->GetSQL(), item->descending() ? " DESC" : "");

    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(item->option_list()));
    if (!options_string.empty()) {
      absl::StrAppend(&col_string, " OPTIONS(", options_string, ") ");
    }
    cols.push_back(col_string);
  }
  if (node->index_all_columns()) {
    std::string cols_with_opts = absl::StrJoin(cols, ",");
    bool has_col_opts = !cols.empty();
    cols.clear();
    if (has_col_opts) {
      cols.push_back("ALL COLUMNS WITH COLUMN OPTIONS (" + cols_with_opts +
                     " )");
    } else {
      cols.push_back("ALL COLUMNS");
    }
  }
  absl::StrAppend(&sql, absl::StrJoin(cols, ","), ") ");

  if (!node->storing_expression_list().empty()) {
    std::vector<std::string> argument_list;
    argument_list.reserve(node->storing_expression_list_size());
    for (int i = 0; i < node->storing_expression_list_size(); ++i) {
      const ResolvedExpr* argument = node->storing_expression_list(i);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(argument));
      argument_list.push_back(result->GetSQL());
    }
    absl::StrAppend(
        &sql, "STORING(",
        absl::StrJoin(argument_list.begin(), argument_list.end(), ","), ")");
  }

  if (!node->partition_by_list().empty()) {
    absl::StrAppend(&sql, " PARTITION BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->partition_by_list(), &sql));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                   GetHintListString(node->option_list()));
  if (!options_string.empty()) {
    absl::StrAppend(&sql, "OPTIONS(", options_string, ") ");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateViewStmt(
    const ResolvedCreateViewStmt* node) {
  return GetCreateViewStatement(node, node->is_value_table(), "VIEW");
}

absl::Status SQLBuilder::VisitResolvedCreateMaterializedViewStmt(
    const ResolvedCreateMaterializedViewStmt* node) {
  // Dummy access on the fields so as to pass the final CheckFieldsAccessed()
  // on a statement level before building the sql.
  for (const auto& output_col : node->output_column_list()) {
    output_col->name();
    output_col->column();
  }
  for (const auto& column_def : node->column_definition_list()) {
    column_def->name();
    column_def->type();
    MarkColumnAnnotationsAccessed(column_def->annotations());
  }

  std::unique_ptr<QueryExpression> query_expression;
  if (node->query() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(MaybeSetupRecursiveView(node));
    GOOGLESQL_ASSIGN_OR_RETURN(query_expression,
                     ProcessQuery(node->query(), node->output_column_list()));
  }

  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "VIEW", &sql));
  GOOGLESQL_RETURN_IF_ERROR(GetOptionalColumnNameWithOptionsList(node, &sql));

  // Make column aliases available for PARTITION BY, CLUSTER BY.
  for (const auto& column_definition : node->column_definition_list()) {
    mutable_computed_column_alias()[column_definition->column().column_id()] =
        column_definition->name();
  }
  absl::StrAppend(&sql, GetSqlSecuritySql(node->sql_security()));

  if (!node->partition_by_list().empty()) {
    absl::StrAppend(&sql, " PARTITION BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->partition_by_list(), &sql));
  }
  if (!node->cluster_by_list().empty()) {
    absl::StrAppend(&sql, " CLUSTER BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->cluster_by_list(), &sql));
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", result, ")");
  }
  // We should get a struct or a proto if is_value_table=true.
  if (query_expression) {
    if (node->is_value_table()) {
      GOOGLESQL_RET_CHECK_EQ(query_expression->SelectList().size(), 1);
      query_expression->SetSelectAsModifier(" AS VALUE");
    }
    absl::StrAppend(&sql, " AS ", query_expression->GetSQLQuery());
  } else if (node->replica_source() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->replica_source()));
    absl::StrAppend(&sql, " AS REPLICA OF ",
                    result->query_expression->FromClause());
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateApproxViewStmt(
    const ResolvedCreateApproxViewStmt* node) {
  return GetCreateViewStatement(node, node->is_value_table(), "APPROX VIEW");
}

absl::Status SQLBuilder::ProcessWithPartitionColumns(
    std::string* sql, const ResolvedWithPartitionColumns* node) {
  absl::StrAppend(sql, "WITH PARTITION COLUMNS");

  if (node->column_definition_list_size() > 0) {
    absl::StrAppend(sql, "(");
    std::vector<std::string> table_elements;
    // Go through each column and generate the SQL corresponding to it.
    for (const auto& c : node->column_definition_list()) {
      std::string table_element =
          absl::StrCat(ToIdentifierLiteral(c->name()), " ");
      GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(
          c->type(), c->is_hidden(), c->annotations(),
          c->generated_column_info(), c->default_value(), &table_element));
      table_elements.push_back(std::move(table_element));
    }
    absl::StrAppend(sql, absl::StrJoin(table_elements, ", "));
    absl::StrAppend(sql, ")");
  }
  absl::StrAppend(sql, " ");

  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateExternalTableStmt(
    const ResolvedCreateExternalTableStmt* node) {
  const bool process_column_definitions =
      node->column_definition_list_size() > 0;
  // PARTITION COLUMNS are not supported in constraints so it is safe to
  // process constraints without processing WITH PARTITION COLUMN clause first.
  GOOGLESQL_ASSIGN_OR_RETURN(std::string sql,
                   ProcessCreateTableStmtBase(node, process_column_definitions,
                                              /*table_type=*/"EXTERNAL TABLE"));

  if (node->with_partition_columns() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ProcessWithPartitionColumns(&sql, node->with_partition_columns()));
  }

  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                   GetHintListString(node->option_list()));
  absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::GetFunctionArgListString(
    absl::Span<const std::string> arg_name_list,
    const FunctionSignature& signature) {
  if (signature.arguments().empty()) {
    return std::string();  // no args
  }
  std::vector<std::string> arg_list_sql;
  arg_list_sql.reserve(arg_name_list.size());
  for (int i = 0; i < arg_name_list.size(); ++i) {
    std::string procedure_argument_mode;
    FunctionEnums::ProcedureArgumentMode mode =
        signature.argument(i).options().procedure_argument_mode();
    if (mode != FunctionEnums::NOT_SET) {
      procedure_argument_mode =
          absl::StrCat(FunctionEnums::ProcedureArgumentMode_Name(mode), " ");
    }
    arg_list_sql.push_back(absl::StrCat(
        procedure_argument_mode, arg_name_list[i], " ",
        signature.argument(i).GetSQLDeclaration(
            options_.language_options.product_mode()),
        signature.argument(i).options().is_not_aggregate() ? " NOT AGGREGATE"
                                                           : ""));
  }
  return absl::StrJoin(arg_list_sql, ", ");
}

absl::Status SQLBuilder::VisitResolvedCreateConstantStmt(
    const ResolvedCreateConstantStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "CONSTANT", &sql));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr_fragment,
                   ProcessNode(node->expr()));
  absl::StrAppend(&sql, " = ", expr_fragment->GetSQL());

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateFunctionStmt(
    const ResolvedCreateFunctionStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(
      node, node->is_aggregate() ? "AGGREGATE FUNCTION" : "FUNCTION", &sql));
  absl::StrAppend(&sql, node->signature().GetSQLDeclaration(
                            node->argument_name_list(),
                            options_.language_options.product_mode(),
                            options_.use_external_float32));
  absl::StrAppend(&sql, GetSqlSecuritySql(node->sql_security()));
  absl::StrAppend(&sql, GetDeterminismLevelSql(node->determinism_level()));
  bool is_sql_defined = absl::AsciiStrToUpper(node->language()) == "SQL";
  bool is_remote = node->is_remote();
  if (!is_sql_defined) {
    if (is_remote && options_.language_options.LanguageFeatureEnabled(
                         FEATURE_REMOTE_FUNCTION)) {
      absl::StrAppend(&sql, " REMOTE");
      if (node->connection() != nullptr) {
        const std::string connection_alias =
            ToIdentifierLiteral(node->connection()->connection()->Name());
        absl::StrAppend(&sql, " WITH CONNECTION ", connection_alias, " ");
      }
    } else {
      absl::StrAppend(&sql, " LANGUAGE ",
                      ToIdentifierLiteral(node->language()));
      if (options_.language_options.LanguageFeatureEnabled(
              FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION) &&
          node->connection() != nullptr) {
        const std::string connection_alias =
            ToIdentifierLiteral(node->connection()->connection()->Name());
        absl::StrAppend(&sql, " WITH CONNECTION ", connection_alias, " ");
      }
    }
  }

  // If we have aggregates, extract the strings for the aggregate expressions
  // and store them in `CopyableState::pending_columns` so they will be
  // substituted into the main expression body.
  for (const auto& computed_col : node->aggregate_expression_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    googlesql_base::InsertOrDie(&mutable_pending_columns(),
                     computed_col->column().column_id(), result->GetSQL());
  }
  node->is_aggregate();  // Mark as accessed.

  if (node->function_expression() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                     ProcessNode(node->function_expression()));
    absl::StrAppend(&sql, " AS (", expr->GetSQL(), ")");
  } else if (is_sql_defined) {
    // This case covers SQL defined function templates that do not have a
    // resolved function_expression().
    absl::StrAppend(&sql, " AS (", node->code(), ")");
  } else if (!is_remote) {
    absl::StrAppend(&sql, " AS ", ToStringLiteral(node->code()));
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateTableFunctionStmt(
    const ResolvedCreateTableFunctionStmt* node) {
  std::string function_type = "TABLE FUNCTION";
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, function_type, &sql));

  absl::StrAppend(&sql, node->signature().GetSQLDeclaration(
                            node->argument_name_list(),
                            options_.language_options.product_mode()));

  absl::StrAppend(&sql, GetSqlSecuritySql(node->sql_security()));

  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }
  bool is_sql_language = googlesql_base::CaseEqual(node->language(), "SQL");
  bool is_undeclared_language =
      googlesql_base::CaseEqual(node->language(), "UNDECLARED");
  bool is_external_language = !is_sql_language && !is_undeclared_language;
  if (is_external_language) {
    absl::StrAppend(&sql, " LANGUAGE ", ToIdentifierLiteral(node->language()));
    GOOGLESQL_RET_CHECK(node->output_column_list().empty());
  }

  if (node->query() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> query_expression,
                     ProcessQuery(node->query(), node->output_column_list()));
    absl::StrAppend(&sql, " AS ", query_expression->GetSQLQuery());
  } else if (!node->code().empty()) {
    if (is_external_language) {
      absl::StrAppend(&sql, " AS ", ToStringLiteral(node->code()));
    } else {
      GOOGLESQL_RET_CHECK(is_sql_language);
      absl::StrAppend(&sql, " AS (", node->code(), ")");
    }
  }

  if (node->query() != nullptr) {
    GOOGLESQL_RET_CHECK(!node->output_column_list().empty());
  } else {
    GOOGLESQL_RET_CHECK(node->output_column_list().empty());
  }
  // Dummy access on is_value_table field so as to pass the final
  // CheckFieldsAccessed() on a statement level before building the sql.
  node->is_value_table();
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateProcedureStmt(
    const ResolvedCreateProcedureStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "PROCEDURE", &sql));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const std::string args,
      GetFunctionArgListString(node->argument_name_list(), node->signature()));
  absl::StrAppend(&sql, node->signature().GetSQLDeclaration(
                            node->argument_name_list(),
                            options_.language_options.product_mode()));
  absl::StrAppend(&sql, GetExternalSecuritySql(node->external_security()));
  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }
  if (!node->language().empty()) {
    absl::StrAppend(&sql, " LANGUAGE ", ToIdentifierLiteral(node->language()));
    if (!node->code().empty()) {
      absl::StrAppend(&sql, " AS ", ToStringLiteral(node->code()));
    }
  } else {
    absl::StrAppend(&sql, node->procedure_body());
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedArgumentDef(
    const ResolvedArgumentDef* node) {
  PushQueryFragment(
      node, absl::StrCat(
                ToIdentifierLiteral(node->name()), " ",
                node->type()->TypeName(options_.language_options.product_mode(),
                                       options_.use_external_float32),
                node->argument_kind() == ResolvedArgumentDef::NOT_AGGREGATE
                    ? " NOT AGGREGATE"
                    : ""));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedArgumentRef(
    const ResolvedArgumentRef* node) {
  PushQueryFragment(node, ToIdentifierLiteral(node->name()));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedExportDataStmt(
    const ResolvedExportDataStmt* node) {
  return VisitResolvedExportDataStmtImpl(node, /*generate_as_pipe=*/false);
}

absl::Status SQLBuilder::VisitResolvedExportDataStmtImpl(
    const ResolvedExportDataStmt* node, bool generate_as_pipe) {
  // Dummy access on the fields so as to pass the final CheckFieldsAccessed() on
  // a statement level before building the sql.
  for (const auto& output_col : node->output_column_list()) {
    output_col->name();
    output_col->column();
  }

  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }
  absl::StrAppend(&sql, "EXPORT DATA ");
  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, "OPTIONS(", result, ") ");
  }
  if (node->query() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> query_expression,
                     ProcessQuery(node->query(), node->output_column_list()));

    // We should get a struct or a proto if is_value_table=true.
    if (node->is_value_table()) {
      GOOGLESQL_RET_CHECK_EQ(query_expression->SelectList().size(), 1);
      query_expression->SetSelectAsModifier("AS VALUE");
    }
    std::string query_sql = query_expression->GetSQLQuery();

    if (generate_as_pipe) {
      sql = absl::StrCat(query_sql, QueryExpression::kPipe, sql);
    } else {
      absl::StrAppend(&sql, "AS ", query_sql);
    }
  } else {
    GOOGLESQL_RET_CHECK(!generate_as_pipe);
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedExportModelStmt(
    const ResolvedExportModelStmt* node) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }
  absl::StrAppend(&sql, "EXPORT MODEL ");
  absl::StrAppend(&sql, IdentifierPathToString(node->model_name_path()));
  absl::StrAppend(&sql, " ");
  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "WITH CONNECTION ", connection_alias, " ");
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, "OPTIONS(", result, ") ");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedExportMetadataStmt(
    const ResolvedExportMetadataStmt* node) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }
  absl::StrAppend(&sql, "EXPORT ");
  absl::StrAppend(&sql, ToIdentifierLiteral(node->schema_object_kind()));
  absl::StrAppend(&sql, " METADATA FROM ");
  absl::StrAppend(&sql, IdentifierPathToString(node->name_path()));
  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, " WITH CONNECTION ", connection_alias);
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", result, ")");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCallStmt(const ResolvedCallStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "CALL ");
  absl::StrAppend(&sql, IdentifierPathToString(node->procedure()->name_path()));

  std::vector<std::string> argument_list;
  argument_list.reserve(node->argument_list_size());
  for (int i = 0; i < node->argument_list_size(); ++i) {
    const ResolvedExpr* argument = node->argument_list(i);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(argument));
    argument_list.push_back(result->GetSQL());
  }
  absl::StrAppend(&sql, "(", absl::StrJoin(argument_list, ", "), ")");
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDefineTableStmt(
    const ResolvedDefineTableStmt* node) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }
  absl::StrAppend(&sql, "DEFINE TABLE ",
                  IdentifierPathToString(node->name_path()));
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                   GetHintListString(node->option_list()));
  absl::StrAppend(&sql, "(", result, ")");

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDescribeStmt(
    const ResolvedDescribeStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DESCRIBE ", GetOptionalObjectType(node->object_type()),
                  IdentifierPathToString(node->name_path()));
  if (!node->from_name_path().empty()) {
    absl::StrAppend(&sql, " FROM ",
                    IdentifierPathToString(node->from_name_path()));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedShowStmt(const ResolvedShowStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "SHOW ", ToIdentifierLiteral(node->identifier()));
  if (!node->name_path().empty()) {
    absl::StrAppend(&sql, " FROM ", IdentifierPathToString(node->name_path()));
  }
  if (node->like_expr() != nullptr) {
    const Value value = node->like_expr()->value();
    GOOGLESQL_RET_CHECK(!value.is_null());
    GOOGLESQL_ASSIGN_OR_RETURN(
        const std::string result,
        GetSQL(value, options_.language_options.product_mode(),
               options_.use_external_float32, /*is_constant_value=*/false));
    absl::StrAppend(&sql, " LIKE ", result);
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedBeginStmt(const ResolvedBeginStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "BEGIN TRANSACTION");
  std::vector<std::string> modes;
  switch (node->read_write_mode()) {
    case ResolvedBeginStmtEnums::MODE_UNSPECIFIED:
      break;
    case ResolvedBeginStmtEnums::MODE_READ_ONLY:
      modes.push_back("READ ONLY");
      break;
    case ResolvedBeginStmtEnums::MODE_READ_WRITE:
      modes.push_back("READ WRITE");
      break;
  }

  if (!node->isolation_level_list().empty()) {
    modes.push_back("ISOLATION LEVEL");
    for (const std::string& part : node->isolation_level_list()) {
      absl::StrAppend(&modes.back(), " ", ToIdentifierLiteral(part));
    }
  }
  if (!modes.empty()) {
    absl::StrAppend(&sql, " ", absl::StrJoin(modes, ", "));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedSetTransactionStmt(
    const ResolvedSetTransactionStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "SET TRANSACTION");
  std::vector<std::string> modes;
  switch (node->read_write_mode()) {
    case ResolvedBeginStmtEnums::MODE_UNSPECIFIED:
      break;
    case ResolvedBeginStmtEnums::MODE_READ_ONLY:
      modes.push_back("READ ONLY");
      break;
    case ResolvedBeginStmtEnums::MODE_READ_WRITE:
      modes.push_back("READ WRITE");
      break;
  }
  if (!node->isolation_level_list().empty()) {
    modes.push_back("ISOLATION LEVEL");
    for (const std::string& part : node->isolation_level_list()) {
      absl::StrAppend(&modes.back(), " ", ToIdentifierLiteral(part));
    }
  }
  if (!modes.empty()) {
    absl::StrAppend(&sql, " ", absl::StrJoin(modes, ", "));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCommitStmt(
    const ResolvedCommitStmt* node) {
  PushQueryFragment(node, "COMMIT");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedRollbackStmt(
    const ResolvedRollbackStmt* node) {
  PushQueryFragment(node, "ROLLBACK");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedStartBatchStmt(
    const ResolvedStartBatchStmt* node) {
  std::string sql = "START BATCH";
  if (!node->batch_type().empty()) {
    absl::StrAppend(&sql, " ", node->batch_type());
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedRunBatchStmt(
    const ResolvedRunBatchStmt* node) {
  PushQueryFragment(node, "RUN BATCH");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAbortBatchStmt(
    const ResolvedAbortBatchStmt* node) {
  PushQueryFragment(node, "ABORT BATCH");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAssignmentStmt(
    const ResolvedAssignmentStmt* node) {
  ABSL_CHECK_EQ(state_.in_set_lhs, false);  // Crash OK
  state_.in_set_lhs = true;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> target_sql,
                   ProcessNode(node->target()));
  state_.in_set_lhs = false;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr_sql,
                   ProcessNode(node->expr()));

  std::string sql;
  absl::StrAppend(&sql, "SET ", target_sql->GetSQL(), " = ",
                  expr_sql->GetSQL());
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAnalyzeStmt(
    const ResolvedAnalyzeStmt* node) {
  std::string sql = "ANALYZE ";
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  }
  std::string table_and_column_index_list;
  GOOGLESQL_RETURN_IF_ERROR(GetTableAndColumnInfoList(node->table_and_column_index_list(),
                                            &table_and_column_index_list));
  absl::StrAppend(&sql, table_and_column_index_list);
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAssertStmt(
    const ResolvedAssertStmt* node) {
  std::string sql;
  const ResolvedExpr* expr = node->expression();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result, ProcessNode(expr));
  absl::StrAppend(&sql, "ASSERT ", result->GetSQL());
  if (!node->description().empty()) {
    absl::StrAppend(&sql, " AS ", ToStringLiteral(node->description()));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAssertRowsModified(
    const ResolvedAssertRowsModified* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->rows()));
  PushQueryFragment(node,
                    absl::StrCat("ASSERT_ROWS_MODIFIED ", result->GetSQL()));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDeleteStmt(
    const ResolvedDeleteStmt* node) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }

  std::string target_sql;
  if (node->table_scan() != nullptr) {
    const Table* table = node->table_scan()->table();
    const std::string alias = GetScanAlias(node->table_scan());
    if (table->IsValueTable()) {
      // Use table alias instead for referring to the value table column.
      // This code is wrong.  See http://b/37291554.
      SetPathForColumn(node->table_scan()->column_list(0),
                       ToIdentifierLiteral(alias));
    } else {
      for (const ResolvedColumn& column : node->table_scan()->column_list()) {
        SetPathForColumn(
            column,
            absl::StrCat(alias, ".", ToIdentifierLiteral(column.name())));
      }
    }
    target_sql = absl::StrCat(TableToIdentifierLiteral(table), " AS ", alias);
    returning_table_alias_ = alias;
  } else {
    GOOGLESQL_RET_CHECK(!nested_dml_targets().empty());
    target_sql = nested_dml_targets().back().first;
    if (nested_dml_targets().back().second != kEmptyAlias) {
      absl::StrAppend(&target_sql, " ", nested_dml_targets().back().second);
    }
  }
  GOOGLESQL_RET_CHECK(!target_sql.empty());
  absl::StrAppend(&sql, "DELETE ", target_sql);

  if (node->array_offset_column() != nullptr) {
    const ResolvedColumn& offset_column = node->array_offset_column()->column();
    const std::string offset_alias = ToIdentifierLiteral(offset_column.name());
    SetPathForColumn(offset_column, offset_alias);
    absl::StrAppend(&sql, " WITH OFFSET AS ", offset_alias);
  }
  if (node->where_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> where,
                     ProcessNode(node->where_expr()));
    absl::StrAppend(&sql, " WHERE ", where->GetSQL());
  }
  if (node->assert_rows_modified() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> assert_rows_modified,
                     ProcessNode(node->assert_rows_modified()));
    absl::StrAppend(&sql, " ", assert_rows_modified->GetSQL());
  }
  if (node->returning() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> returning,
                     ProcessNode(node->returning()));
    absl::StrAppend(&sql, " ", returning->GetSQL());
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedOnConflictClause(
    const ResolvedOnConflictClause* node) {
  std::string sql;
  absl::StrAppend(&sql, "ON CONFLICT ");
  if (!node->unique_constraint_name().empty()) {
    absl::StrAppend(&sql, "ON UNIQUE CONSTRAINT ",
                    node->unique_constraint_name());
    absl::StrAppend(&sql, " ");
  } else if (!node->conflict_target_column_list().empty()) {
    absl::StrAppend(&sql, "(");
    for (int i = 0; i < node->conflict_target_column_list().size(); i++) {
      absl::StrAppend(&sql, ToIdentifierLiteral(
                                node->conflict_target_column_list(i).name()));
      if (i != node->conflict_target_column_list().size() - 1) {
        absl::StrAppend(&sql, ", ");
      }
    }
    absl::StrAppend(&sql, ") ");
  }

  switch (node->conflict_action()) {
    case ResolvedOnConflictClause::NOTHING:
      absl::StrAppend(&sql, "DO NOTHING");
      break;
    case ResolvedOnConflictClause::UPDATE:
      absl::StrAppend(&sql, "DO UPDATE");
      break;
  }

  if (node->conflict_action() == ResolvedOnConflictClause::NOTHING) {
    PushQueryFragment(node, sql);
    return absl::OkStatus();
  }

  if (node->insert_row_scan() != nullptr) {
    // Make columns in this insert row scan available with "excluded" alias.
    GOOGLESQL_RETURN_IF_ERROR(
        SetPathForColumnsInScan(node->insert_row_scan(), "excluded"));
    node->insert_row_scan()->MarkFieldsAccessed();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::string update_item_list_sql,
                   GetUpdateItemListSQL(node->update_item_list()));
  absl::StrAppend(&sql, " SET ", update_item_list_sql);

  if (node->update_where_expression() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> where,
                     ProcessNode(node->update_where_expression()));
    absl::StrAppend(&sql, " WHERE ", where->GetSQL());
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedReturningClause(
    const ResolvedReturningClause* node) {
  for (int i = 0; i < node->output_column_list_size(); i++) {
    const ResolvedOutputColumn* output_col = node->output_column_list(i);
    // Dummy access on the output_column_list so as to pass the final
    // CheckFieldsAccessed() on a statement level before building the sql.
    output_col->column();
  }

  std::string sql = "THEN RETURN";
  bool has_action_column = node->action_column() != nullptr;

  if (has_action_column) {
    std::string action_alias = node->action_column()->column().name();
    absl::StrAppend(&sql, " WITH ACTION AS ", action_alias);
  }

  size_t output_size =
      node->output_column_list_size() - (has_action_column ? 1 : 0);

  absl::flat_hash_map</*column_id=*/int64_t, const ResolvedExpr*>
      col_to_expr_map;
  for (const auto& expr : node->expr_list()) {
    googlesql_base::InsertIfNotPresent(&col_to_expr_map, expr->column().column_id(),
                            expr->expr());
  }

  ABSL_DCHECK_NE(returning_table_alias_, "");
  for (int i = 0; i < output_size; i++) {
    const ResolvedOutputColumn* col = node->output_column_list(i);
    if (col_to_expr_map.contains(col->column().column_id())) {
      const ResolvedExpr* expr =
          googlesql_base::FindOrDie(col_to_expr_map, col->column().column_id());
      // Update the identifier for target table columns in expressions.
      GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInReturningExpr(expr));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(expr));
      absl::StrAppend(&sql, " ", result->GetSQL(), " AS ",
                      ToIdentifierLiteral(col->name()));
    } else {
      absl::StrAppend(&sql, " `", returning_table_alias_, "`.`",
                      col->column().name(), "` AS ",
                      ToIdentifierLiteral(col->name()));
    }

    if (i != output_size - 1) {
      absl::StrAppend(&sql, ",");
    }
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUndropStmt(
    const ResolvedUndropStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "UNDROP ",
                  ToIdentifierLiteral(node->schema_object_kind()),
                  node->is_if_not_exists() ? " IF NOT EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  if (node->for_system_time_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->for_system_time_expr()));
    absl::StrAppend(&sql, " FOR SYSTEM_TIME AS OF ", result->GetSQL());
  }
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ")");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

static std::string GetDropModeSQL(ResolvedDropStmtEnums::DropMode mode) {
  switch (mode) {
    case ResolvedDropStmtEnums::DROP_MODE_UNSPECIFIED:
      return "";
    case ResolvedDropStmtEnums::RESTRICT:
      return "RESTRICT";
    case ResolvedDropStmtEnums::CASCADE:
      return "CASCADE";
  }
}

absl::Status SQLBuilder::VisitResolvedDropStmt(const ResolvedDropStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DROP ", node->object_type(),
                  node->is_if_exists() ? " IF EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  absl::StrAppend(&sql, GetDropModeSQL(node->drop_mode()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropFunctionStmt(
    const ResolvedDropFunctionStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DROP FUNCTION ",
                  node->is_if_exists() ? " IF EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  if (node->signature() != nullptr) {
    absl::StrAppend(&sql, node->signature()->signature().GetSQLDeclaration(
                              {} /* arg_name_list */,
                              options_.language_options.product_mode()));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropTableFunctionStmt(
    const ResolvedDropTableFunctionStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DROP TABLE FUNCTION ",
                  node->is_if_exists() ? " IF EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropMaterializedViewStmt(
    const ResolvedDropMaterializedViewStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DROP MATERIALIZED VIEW",
                  node->is_if_exists() ? " IF EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropSnapshotTableStmt(
    const ResolvedDropSnapshotTableStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DROP SNAPSHOT TABLE",
                  node->is_if_exists() ? " IF EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropPrivilegeRestrictionStmt(
    const ResolvedDropPrivilegeRestrictionStmt* node) {
  std::string sql = "DROP PRIVILEGE RESTRICTION ";
  if (node->is_if_exists()) {
    absl::StrAppend(&sql, "IF EXISTS ");
  }
  absl::StrAppend(&sql, "ON ");
  std::vector<std::string> privilege_list_sql;
  for (const auto& privilege : node->column_privilege_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(privilege.get()));
    privilege_list_sql.push_back(result->GetSQL());
  }
  absl::StrAppend(&sql, absl::StrJoin(privilege_list_sql, ", "));
  absl::StrAppend(&sql, " ON ", node->object_type(), " ",
                  IdentifierPathToString(node->name_path()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropRowAccessPolicyStmt(
    const ResolvedDropRowAccessPolicyStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "DROP ");
  if (node->is_drop_all()) {
    absl::StrAppend(&sql, "ALL ROW ACCESS POLICIES");
  } else {
    absl::StrAppend(&sql, "ROW ACCESS POLICY",
                    node->is_if_exists() ? " IF EXISTS " : " ",
                    ToIdentifierLiteral(node->name()));
  }
  absl::StrAppend(&sql, " ON ",
                  IdentifierPathToString(node->target_name_path()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDropIndexStmt(
    const ResolvedDropIndexStmt* node) {
  std::string sql;
  switch (node->index_type()) {
    case ResolvedDropIndexStmt::INDEX_SEARCH:
      absl::StrAppend(&sql, "DROP SEARCH INDEX");
      break;
    case ResolvedDropIndexStmt::INDEX_VECTOR:
      absl::StrAppend(&sql, "DROP VECTOR INDEX");
      break;
    case ResolvedDropIndexStmt::INDEX_DEFAULT:
      GOOGLESQL_RET_CHECK_FAIL() << "unsupported index type";
  }
  absl::StrAppend(&sql, node->is_if_exists() ? " IF EXISTS " : " ",
                  ToIdentifierLiteral(node->name()));
  if (!node->table_name_path().empty()) {
    absl::StrAppend(&sql, " ON ",
                    IdentifierPathToString(node->table_name_path()));
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedTruncateStmt(
    const ResolvedTruncateStmt* node) {
  std::string sql = "TRUNCATE TABLE ";
  GOOGLESQL_RET_CHECK(node->table_scan() != nullptr) << "Missing target table.";
  std::string name_path = TableToIdentifierLiteral(node->table_scan()->table());
  GOOGLESQL_RET_CHECK(!name_path.empty());
  absl::StrAppend(&sql, name_path, " ");

  // Make column aliases available for WHERE expression
  for (const auto& column_definition : node->table_scan()->column_list()) {
    mutable_computed_column_alias()[column_definition.column_id()] =
        column_definition.name();
  }

  if (node->where_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> where,
                     ProcessNode(node->where_expr()));
    absl::StrAppend(&sql, " WHERE ", where->GetSQL());
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDMLDefault(
    const ResolvedDMLDefault* node) {
  PushQueryFragment(node, "DEFAULT");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedDMLValue(const ResolvedDMLValue* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->value()));
  PushQueryFragment(node, result->GetSQL());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUpdateItem(
    const ResolvedUpdateItem* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> target,
                   ProcessNode(node->target()));
  GOOGLESQL_RET_CHECK(!update_item_targets_and_offsets().empty());
  mutable_update_item_targets_and_offsets().back().emplace_back(
      target->GetSQL(),
      // UpdateItemElementState.subscript_sql will be empty for now.
      // VisitResolvedUpdateItemElement will fill it in later if needed.
      CopyableState::UpdateItemElementState(
          {.target_type = node->target()->type()}));
  if (node->update_item_element_list_size() > 0) {
    // Use kEmptyAlias as the path so that VisitResolvedGet{Proto,Struct}Field
    // will print "foo" instead of <column>.foo.
    SetPathForColumn(node->element_column()->column(), kEmptyAlias);

    std::vector<std::string> sql_fragments;
    for (const auto& update_item_element : node->update_item_element_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> fragment,
                       ProcessNode(update_item_element.get()));
      sql_fragments.push_back(fragment->GetSQL());
    }

    PushQueryFragment(node, absl::StrJoin(sql_fragments, ", "));
  } else {
    std::string target_sql;
    for (int i = 0; i < update_item_targets_and_offsets().back().size(); ++i) {
      const auto& target_and_subscript =
          update_item_targets_and_offsets().back()[i];
      const std::string& target = target_and_subscript.first;
      const std::string& subscript_sql =
          target_and_subscript.second.subscript_sql;

      const bool last =
          (i == update_item_targets_and_offsets().back().size() - 1);
      GOOGLESQL_RET_CHECK_EQ(last, subscript_sql.empty());

      // The ResolvedColumn representing an array element has path
      // kEmptyAlias. It is suppressed by VisitResolvedGet{Proto,Struct}Field,
      // but if we are modifying the element and not a field of a proto or
      // struct, then we need to suppress the string here (or else we would get
      // something like a[OFFSET(1)]``).
      if (target != kEmptyAlias) {
        if (i == 0) {
          target_sql = target;
        } else {
          absl::StrAppend(&target_sql, ".", target);
        }
      } else {
        // It is possible to access a nested subscript with JSON such as
        // JSON["a"]["b"] so the empty alias may correspond to an offset and not
        // necessarily the last target.
        GOOGLESQL_RET_CHECK(last || !subscript_sql.empty());
      }

      if (!subscript_sql.empty()) {
        if (target_and_subscript.second.subscript_modifier ==
            CopyableState::kOffset) {
          absl::StrAppend(&target_sql, "[OFFSET(", subscript_sql, ")]");
        } else if (target_and_subscript.second.subscript_modifier ==
                   CopyableState::kKey) {
          absl::StrAppend(&target_sql, "[KEY(", subscript_sql, ")]");
        } else {
          absl::StrAppend(&target_sql, "[", subscript_sql, "]");
        }
      }
    }

    std::string sql;
    if (node->set_value() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> value,
                       ProcessNode(node->set_value()));
      absl::StrAppend(&sql, target_sql, " = ", value->GetSQL());
    }

    std::string target_alias;
    if (node->element_column() != nullptr) {
      const ResolvedColumn& column = node->element_column()->column();
      if (IsInternalAlias(column.name())) {
        // We use an internal alias for the target of a nested DML statement
        // when it does not end in an identifier, and a ResolvedUpdateItem can
        // refer to that. E.g., the 'target' field of the ResolvedUpdateItem for
        // "b = 4" in "UPDATE T SET (UPDATE T.(a) SET b = 4 WHERE ..." contains
        // a ResolvedColumnRef for T.(a) using an internal alias.
        target_alias = kEmptyAlias;
      } else {
        target_alias = ToIdentifierLiteral(column.name());
      }
      SetPathForColumn(column, target_alias);
    }
    mutable_nested_dml_targets().push_back(
        std::make_pair(target_sql, target_alias));

    std::vector<std::string> nested_statements_sql;
    if (node->delete_list_size() > 0) {
      for (const auto& delete_stmt : node->delete_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(delete_stmt.get()));
        nested_statements_sql.push_back(
            absl::StrCat("(", result->GetSQL(), ")"));
      }
    }
    if (node->update_list_size() > 0) {
      for (const auto& update_stmt : node->update_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(update_stmt.get()));
        nested_statements_sql.push_back(
            absl::StrCat("(", result->GetSQL(), ")"));
      }
    }
    if (node->insert_list_size() > 0) {
      for (const auto& insert_stmt : node->insert_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                         ProcessNode(insert_stmt.get()));
        nested_statements_sql.push_back(
            absl::StrCat("(", result->GetSQL(), ")"));
      }
    }
    absl::StrAppend(&sql, absl::StrJoin(nested_statements_sql, ", "));

    mutable_nested_dml_targets().pop_back();
    PushQueryFragment(node, sql);
  }

  mutable_update_item_targets_and_offsets().back().pop_back();
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUpdateItemElement(
    const ResolvedUpdateItemElement* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> subscript,
                   ProcessNode(node->subscript()));
  GOOGLESQL_RET_CHECK(!update_item_targets_and_offsets().empty());
  GOOGLESQL_RET_CHECK(!update_item_targets_and_offsets().back().empty());
  GOOGLESQL_RET_CHECK_EQ(
      "", update_item_targets_and_offsets().back().back().second.subscript_sql);
  const std::string subscript_sql = subscript->GetSQL();
  GOOGLESQL_RET_CHECK(!subscript_sql.empty());
  mutable_update_item_targets_and_offsets().back().back().second.subscript_sql =
      subscript_sql;

  const Type* subscripted_target_type =
      update_item_targets_and_offsets().back().back().second.target_type;

  CopyableState::SubscriptModifierKind subscript_modifier_kind;
  if (node->update_item_mode() ==
          ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_REQUIRED &&
      subscripted_target_type->IsMap()) {
    subscript_modifier_kind = CopyableState::kKey;
  } else if (node->subscript()->type()->IsInteger() &&
             subscripted_target_type->IsArray()) {
    subscript_modifier_kind = CopyableState::kOffset;
  } else {
    subscript_modifier_kind = CopyableState::kNone;
  }

  mutable_update_item_targets_and_offsets().back().back().second = {
      // target_type is carried forward for the merged ResolvedUpdateItemElement
      // case.
      .target_type = subscripted_target_type,
      .subscript_sql = subscript_sql,
      .subscript_modifier = subscript_modifier_kind};

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> update,
                   ProcessNode(node->update_item()));

  // Clear the offset_sql.
  mutable_update_item_targets_and_offsets()
      .back()
      .back()
      .second.subscript_sql.clear();

  PushQueryFragment(node, update->GetSQL());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUpdateStmt(
    const ResolvedUpdateStmt* node) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }

  std::string target_sql;
  if (node->table_scan() != nullptr) {
    // Always use a table alias. If we have the FROM clause, the alias might
    // appear in the FROM scan.
    const std::string alias = GetScanAlias(node->table_scan());
    GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(node->table_scan(), alias));
    absl::StrAppend(&target_sql,
                    TableToIdentifierLiteral(node->table_scan()->table()),
                    " AS ", alias);
    returning_table_alias_ = alias;
  } else {
    GOOGLESQL_RET_CHECK(!nested_dml_targets().empty());
    target_sql = nested_dml_targets().back().first;
    if (nested_dml_targets().back().second != kEmptyAlias) {
      absl::StrAppend(&target_sql, " ", nested_dml_targets().back().second);
    }
  }
  GOOGLESQL_RET_CHECK(!target_sql.empty());
  absl::StrAppend(&sql, "UPDATE ", target_sql);

  if (node->array_offset_column() != nullptr) {
    const ResolvedColumn& offset_column = node->array_offset_column()->column();
    const std::string offset_alias = ToIdentifierLiteral(offset_column.name());
    SetPathForColumn(offset_column, offset_alias);
    absl::StrAppend(&sql, " WITH OFFSET AS ", offset_alias);
  }

  std::string from_sql;
  if (node->from_scan() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(node->from_scan(), ""));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> from,
                     ProcessNode(node->from_scan()));
    std::unique_ptr<QueryExpression> query_expression(
        from->query_expression.release());
    if (IsPipeSyntaxTargetMode()) {
      GOOGLESQL_RETURN_IF_ERROR(
          WrapQueryExpression(node->from_scan(), query_expression.get()));
      absl::StrAppend(&from_sql, " FROM (",
                      query_expression->GetSQLQuery(TargetSyntaxMode::kPipe),
                      ")");
    } else {
      absl::StrAppend(&from_sql, " FROM ", query_expression->FromClause());
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::string update_item_list_sql,
                   GetUpdateItemListSQL(node->update_item_list()));
  absl::StrAppend(&sql, " SET ", update_item_list_sql);

  if (!from_sql.empty()) {
    absl::StrAppend(&sql, from_sql);
  }

  if (node->where_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> where,
                     ProcessNode(node->where_expr()));
    absl::StrAppend(&sql, " WHERE ", where->GetSQL());
  }
  if (node->assert_rows_modified() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> assert_rows_modified,
                     ProcessNode(node->assert_rows_modified()));
    absl::StrAppend(&sql, " ", assert_rows_modified->GetSQL());
  }
  if (node->returning() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> returning,
                     ProcessNode(node->returning()));
    absl::StrAppend(&sql, " ", returning->GetSQL());
  }
  // Dummy access for topologically_sorted_generated_column_id_list &
  // generated_column_expr_list.
  node->topologically_sorted_generated_column_id_list();
  for (const auto& generated_expr : node->generated_column_expr_list()) {
    generated_expr->MarkFieldsAccessed();
  }

  ABSL_LOG(INFO) << "INSERT SQL: " << sql;
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedInsertRow(const ResolvedInsertRow* node) {
  std::vector<std::string> values_sql;
  for (const auto& value : node->value_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(value.get()));
    values_sql.push_back(result->GetSQL());
  }
  PushQueryFragment(node,
                    absl::StrCat("(", absl::StrJoin(values_sql, ", "), ")"));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedInsertStmt(
    const ResolvedInsertStmt* node) {
  return VisitResolvedInsertStmtImpl(node, /*generate_as_pipe=*/false);
}

absl::Status SQLBuilder::VisitResolvedInsertStmtImpl(
    const ResolvedInsertStmt* node, bool generate_as_pipe) {
  std::string sql;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &sql));
    absl::StrAppend(&sql, " ");
  }

  absl::StrAppend(&sql, "INSERT ");
  switch (node->insert_mode()) {
    case ResolvedInsertStmt::OR_IGNORE:
      absl::StrAppend(&sql, "OR IGNORE ");
      break;
    case ResolvedInsertStmt::OR_REPLACE:
      absl::StrAppend(&sql, "OR REPLACE ");
      break;
    case ResolvedInsertStmt::OR_UPDATE:
      absl::StrAppend(&sql, "OR UPDATE ");
      break;
    case ResolvedInsertStmt::OR_ERROR:
      break;
  }

  std::string target_sql;
  if (node->table_scan() != nullptr) {
    target_sql = TableToIdentifierLiteral(node->table_scan()->table());
    // INSERT doesn't support explicit aliasing, and this is the implicit alias
    // of the table's full name.
    returning_table_alias_ = node->table_scan()->table()->Name();

    // ON CONFLICT DO UPDATE SET...WHERE clause expressions can access the
    // table row values. This table scan's columns are used to reference the
    // table row columns. Set path for columns in the scan to be used in
    // ON CONFLICT's SQL builder.
    if (node->on_conflict_clause() != nullptr &&
        node->on_conflict_clause()->conflict_action() ==
            ResolvedOnConflictClause::UPDATE) {
      GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(node->table_scan(), ""));
    }
  } else {
    GOOGLESQL_RET_CHECK(!nested_dml_targets().empty());
    target_sql = nested_dml_targets().back().first;
  }
  GOOGLESQL_RET_CHECK(!target_sql.empty());
  absl::StrAppend(&sql, target_sql, " ");

  if (node->insert_column_list_size() > 0) {
    absl::StrAppend(&sql, "(",
                    GetInsertColumnListSQL(node->insert_column_list()), ") ");
  }

  if (node->row_list_size() > 0) {
    GOOGLESQL_RET_CHECK(!generate_as_pipe);
    std::vector<std::string> rows_sql;
    for (const auto& row : node->row_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(row.get()));
      rows_sql.push_back(result->GetSQL());
    }
    absl::StrAppend(&sql, "VALUES ", absl::StrJoin(rows_sql, ", "));
  } else {
    GOOGLESQL_RET_CHECK(node->query() != nullptr);

    std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;
    output_column_list.reserve(node->query_output_column_list().size());
    for (auto column : node->query_output_column_list()) {
      output_column_list.push_back(
          MakeResolvedOutputColumn(column.name(), column));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> query_expression,
                     ProcessQuery(node->query(), output_column_list));

    // Dummy access to the query parameter list for
    // ResolvedInsertStmt::CheckFieldsAccessed().
    for (const std::unique_ptr<const ResolvedColumnRef>& parameter :
         node->query_parameter_list()) {
      parameter->column();
    }

    std::string query_sql = query_expression->GetSQLQuery();
    // Subquery must be wrapped in parentheses in presence of ON CONFLICT
    // clause to avoid parser error.
    if (node->on_conflict_clause() != nullptr) {
      query_sql = absl::StrCat("(", query_sql, ")");
    }
    if (generate_as_pipe) {
      sql = absl::StrCat(query_sql, QueryExpression::kPipe, sql);
    } else {
      absl::StrAppend(&sql, query_sql);
    }
  }

  // Dummy access for topologically_sorted_generated_column_id_list &
  // generated_column_expr_list.
  node->topologically_sorted_generated_column_id_list();
  for (const auto& generated_expr : node->generated_column_expr_list()) {
    generated_expr->MarkFieldsAccessed();
  }

  if (node->on_conflict_clause() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> on_conflict_clause,
                     ProcessNode(node->on_conflict_clause()));
    absl::StrAppend(&sql, " ", on_conflict_clause->GetSQL());
  }

  if (node->assert_rows_modified() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> assert_rows_modified,
                     ProcessNode(node->assert_rows_modified()));
    absl::StrAppend(&sql, " ", assert_rows_modified->GetSQL());
  }
  if (node->returning() != nullptr) {
    // Prepares the visible columns for the returning clause
    GOOGLESQL_RET_CHECK_NE(node->table_scan(), nullptr);
    GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(node->table_scan(), target_sql));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> returning,
                     ProcessNode(node->returning()));
    absl::StrAppend(&sql, " ", returning->GetSQL());
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedMergeStmt(const ResolvedMergeStmt* node) {
  std::string sql = "MERGE INTO ";
  GOOGLESQL_RET_CHECK(node->table_scan() != nullptr) << "Missing target table.";
  // Creates alias for the target table, because its column names may appear in
  // the source scan.
  std::string alias = GetScanAlias(node->table_scan());
  GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(node->table_scan(), alias));
  absl::StrAppend(&sql, TableToIdentifierLiteral(node->table_scan()->table()),
                  " AS ", alias);

  GOOGLESQL_RET_CHECK(node->from_scan() != nullptr) << "Missing data source.";
  absl::StrAppend(&sql, " USING ");
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> source,
                   ProcessNode(node->from_scan()));
  std::unique_ptr<QueryExpression> query_expression(
      source->query_expression.release());
  GOOGLESQL_RETURN_IF_ERROR(
      WrapQueryExpression(node->from_scan(), query_expression.get()));
  if (IsPipeSyntaxTargetMode()) {
    absl::StrAppend(
        &sql, "(", query_expression->GetSQLQuery(TargetSyntaxMode::kPipe), ")");
  } else {
    absl::StrAppend(&sql, query_expression->FromClause());
  }

  GOOGLESQL_RET_CHECK(node->merge_expr() != nullptr) << "Missing merge condition.";
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> merge_condition,
                   ProcessNode(node->merge_expr()));
  absl::StrAppend(&sql, " ON ", merge_condition->GetSQL());

  for (const auto& when_clause : node->when_clause_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> when_clause_sql,
                     ProcessNode(when_clause.get()));
    absl::StrAppend(&sql, " ", when_clause_sql->GetSQL());
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedMergeWhen(const ResolvedMergeWhen* node) {
  std::string sql = "WHEN ";
  switch (node->match_type()) {
    case ResolvedMergeWhen::MATCHED:
      absl::StrAppend(&sql, "MATCHED");
      break;
    case ResolvedMergeWhen::NOT_MATCHED_BY_SOURCE:
      absl::StrAppend(&sql, "NOT MATCHED BY SOURCE");
      break;
    case ResolvedMergeWhen::NOT_MATCHED_BY_TARGET:
      absl::StrAppend(&sql, "NOT MATCHED BY TARGET");
      break;
  }

  if (node->match_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> match_condition,
                     ProcessNode(node->match_expr()));
    absl::StrAppend(&sql, " AND ", match_condition->GetSQL());
  }

  absl::StrAppend(&sql, " THEN ");

  switch (node->action_type()) {
    case ResolvedMergeWhen::INSERT: {
      GOOGLESQL_RET_CHECK(!node->insert_column_list().empty());
      GOOGLESQL_RET_CHECK_NE(nullptr, node->insert_row());
      GOOGLESQL_RET_CHECK_EQ(node->insert_column_list_size(),
                   node->insert_row()->value_list_size());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> row_value,
                       ProcessNode(node->insert_row()));
      absl::StrAppend(&sql, "INSERT (",
                      GetInsertColumnListSQL(node->insert_column_list()), ") ",
                      "VALUES ", row_value->GetSQL());
      break;
    }
    case ResolvedMergeWhen::UPDATE: {
      GOOGLESQL_RET_CHECK(!node->update_item_list().empty());
      GOOGLESQL_ASSIGN_OR_RETURN(std::string update_item_list_sql,
                       GetUpdateItemListSQL(node->update_item_list()));
      absl::StrAppend(&sql, "UPDATE SET ", update_item_list_sql);
      break;
    }
    case ResolvedMergeWhen::DELETE:
      absl::StrAppend(&sql, "DELETE");
      break;
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAlterConnectionStmt(
    const ResolvedAlterConnectionStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "CONNECTION");
}

absl::Status SQLBuilder::VisitResolvedAlterDatabaseStmt(
    const ResolvedAlterDatabaseStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "DATABASE");
}

absl::Status SQLBuilder::VisitResolvedAlterIndexStmt(
    const ResolvedAlterIndexStmt* node) {
  std::string sql;
  switch (node->index_type()) {
    case ResolvedAlterIndexStmt::INDEX_SEARCH:
      absl::StrAppend(&sql, "ALTER SEARCH INDEX");
      break;
    case ResolvedAlterIndexStmt::INDEX_VECTOR:
      absl::StrAppend(&sql, "ALTER VECTOR INDEX");
      break;
    case ResolvedAlterIndexStmt::INDEX_DEFAULT:
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported index type: " << node->index_type();
  }
  absl::StrAppend(&sql, node->is_if_exists() ? " IF EXISTS " : " ",
                  IdentifierPathToString(node->name_path()));
  if (!node->table_name_path().empty()) {
    absl::StrAppend(&sql, " ON ",
                    IdentifierPathToString(node->table_name_path()));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string actions_string,
                   GetAlterActionListSQL(node->alter_action_list()));
  absl::StrAppend(&sql, " ", actions_string);
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAlterSchemaStmt(
    const ResolvedAlterSchemaStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "SCHEMA");
}

absl::Status SQLBuilder::VisitResolvedAlterSequenceStmt(
    const ResolvedAlterSequenceStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "SEQUENCE");
}

absl::Status SQLBuilder::VisitResolvedAlterExternalSchemaStmt(
    const ResolvedAlterExternalSchemaStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "EXTERNAL SCHEMA");
}

absl::Status SQLBuilder::VisitResolvedAlterTableSetOptionsStmt(
    const ResolvedAlterTableSetOptionsStmt* node) {
  std::string sql = "ALTER TABLE ";
  absl::StrAppend(&sql, node->is_if_exists() ? "IF EXISTS " : "",
                  IdentifierPathToString(node->name_path()), " ");
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                   GetHintListString(node->option_list()));
  absl::StrAppend(&sql, "SET OPTIONS(", options_string, ") ");
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::GetResolvedAlterObjectStmtSQL(
    const ResolvedAlterObjectStmt* node, absl::string_view object_kind) {
  std::string sql = "ALTER ";
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string actions_string,
                   GetAlterActionListSQL(node->alter_action_list()));
  absl::StrAppend(
      &sql, object_kind, " ", node->is_if_exists() ? "IF EXISTS " : "",
      IdentifierPathToString(node->name_path()), " ", actions_string);
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAlterTableStmt(
    const ResolvedAlterTableStmt* node) {
  if (options_.language_options.LanguageFeatureEnabled(
          FEATURE_ENABLE_ALTER_TABLE_ADD_COLUMN_GENERATED_EXPR)) {
    if (node->table_scan() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(SetPathForColumnsInScan(node->table_scan(), ""));
    }
  }
  return GetResolvedAlterObjectStmtSQL(node, "TABLE");
}

absl::Status SQLBuilder::VisitResolvedAlterViewStmt(
    const ResolvedAlterViewStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "VIEW");
}

absl::Status SQLBuilder::VisitResolvedAlterMaterializedViewStmt(
    const ResolvedAlterMaterializedViewStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "MATERIALIZED VIEW");
}

absl::Status SQLBuilder::VisitResolvedAlterApproxViewStmt(
    const ResolvedAlterApproxViewStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "APPROX VIEW");
}

absl::Status SQLBuilder::VisitResolvedAlterModelStmt(
    const ResolvedAlterModelStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, "MODEL");
}

absl::StatusOr<std::string> SQLBuilder::GetAlterActionListSQL(
    absl::Span<const std::unique_ptr<const ResolvedAlterAction>>
        alter_action_list) {
  std::vector<std::string> alter_action_sql;
  for (const auto& alter_action : alter_action_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string action_string,
                     GetAlterActionSQL(alter_action.get()));
    alter_action_sql.push_back(action_string);
  }
  return absl::StrJoin(alter_action_sql, ", ");
}

absl::StatusOr<std::string> SQLBuilder::GetAlterActionSQL(
    const ResolvedAlterAction* alter_action) {
  std::string alter_action_sql;

  switch (alter_action->node_kind()) {
    case RESOLVED_SET_OPTIONS_ACTION: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          const std::string options_string,
          GetHintListString(
              alter_action->GetAs<ResolvedSetOptionsAction>()->option_list()));
      alter_action_sql = absl::StrCat("SET OPTIONS(", options_string, ") ");
    } break;
    case RESOLVED_ADD_COLUMN_ACTION: {
      const auto* add_action = alter_action->GetAs<ResolvedAddColumnAction>();
      const auto* column_definition = add_action->column_definition();
      if (options_.language_options.LanguageFeatureEnabled(
              FEATURE_ENABLE_ALTER_TABLE_ADD_COLUMN_GENERATED_EXPR)) {
        SetPathForColumn(
            column_definition->column(),
            ToIdentifierLiteral(column_definition->column().name()));
      }
      std::string add_column = absl::StrCat(
          "ADD COLUMN ", add_action->is_if_not_exists() ? "IF NOT EXISTS " : "",
          column_definition->name(), " ");
      GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(
          column_definition->type(), column_definition->is_hidden(),
          column_definition->annotations(),
          column_definition->generated_column_info(),
          column_definition->default_value(), &add_column));
      alter_action_sql = add_column;
    } break;
    case RESOLVED_ADD_COLUMN_IDENTIFIER_ACTION: {
      const auto* add_column_identifier_action =
          alter_action->GetAs<ResolvedAddColumnIdentifierAction>();
      std::string add_column_identifier_sql = absl::StrCat(
          "ADD COLUMN ",
          add_column_identifier_action->is_if_not_exists() ? "IF NOT EXISTS "
                                                           : "",
          add_column_identifier_action->name());
      if (add_column_identifier_action->options_list_size() > 0) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            const std::string result,
            GetHintListString(add_column_identifier_action->options_list()));
        absl::StrAppend(&add_column_identifier_sql, " OPTIONS(", result, ") ");
      }
      alter_action_sql = add_column_identifier_sql;
    } break;
    case RESOLVED_ADD_TO_RESTRICTEE_LIST_ACTION: {
      auto* add_to_restrictee_list_action =
          alter_action->GetAs<ResolvedAddToRestricteeListAction>();
      GOOGLESQL_ASSIGN_OR_RETURN(std::string restrictee_sql,
                       GetGranteeListSQL(
                           /*prefix=*/"", /*grantee_list=*/{},
                           add_to_restrictee_list_action->restrictee_list()));
      alter_action_sql = absl::StrCat(
          "ADD ",
          add_to_restrictee_list_action->is_if_not_exists() ? "IF NOT EXISTS "
                                                            : "",
          "(", restrictee_sql, ")");
    } break;
    case RESOLVED_DROP_COLUMN_ACTION: {
      auto* drop_action = alter_action->GetAs<ResolvedDropColumnAction>();
      alter_action_sql = absl::StrCat(
          "DROP COLUMN ", drop_action->is_if_exists() ? "IF EXISTS " : "",
          drop_action->name());
    } break;
    case RESOLVED_RENAME_COLUMN_ACTION: {
      auto* rename_action = alter_action->GetAs<ResolvedRenameColumnAction>();
      alter_action_sql = absl::StrCat(
          "RENAME COLUMN ", rename_action->is_if_exists() ? "IF EXISTS " : "",
          rename_action->name(), " TO ", rename_action->new_name());
    } break;
    case RESOLVED_GRANT_TO_ACTION: {
      auto* grant_to_action = alter_action->GetAs<ResolvedGrantToAction>();
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::string grantee_sql,
          GetGranteeListSQL("", {}, grant_to_action->grantee_expr_list()));
      alter_action_sql = absl::StrCat("GRANT TO (", grantee_sql, ")");
    } break;
    case RESOLVED_RESTRICT_TO_ACTION: {
      auto* restrict_to_action =
          alter_action->GetAs<ResolvedRestrictToAction>();
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::string restrictee_sql,
          GetGranteeListSQL(/*prefix=*/"", /*grantee_list=*/{},
                            restrict_to_action->restrictee_list()));
      alter_action_sql = absl::StrCat("RESTRICT TO (", restrictee_sql, ")");
    } break;
    case RESOLVED_REMOVE_FROM_RESTRICTEE_LIST_ACTION: {
      auto* remove_from_restrictee_list_action =
          alter_action->GetAs<ResolvedRemoveFromRestricteeListAction>();
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::string restrictee_sql,
          GetGranteeListSQL(
              /*prefix=*/"", /*grantee_list=*/{},
              remove_from_restrictee_list_action->restrictee_list()));
      alter_action_sql = absl::StrCat(
          "REMOVE ",
          remove_from_restrictee_list_action->is_if_exists() ? "IF EXISTS "
                                                             : "",
          "(", restrictee_sql, ")");
    } break;
    case RESOLVED_FILTER_USING_ACTION: {
      auto* filter_using_action =
          alter_action->GetAs<ResolvedFilterUsingAction>();
      alter_action_sql = absl::StrCat(
          "FILTER USING (", filter_using_action->predicate_str(), ")");
    } break;
    case RESOLVED_REVOKE_FROM_ACTION: {
      auto* revoke_from_action =
          alter_action->GetAs<ResolvedRevokeFromAction>();
      std::string revokees;
      if (revoke_from_action->is_revoke_from_all()) {
        revokees = "ALL";
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::string revokee_list,
            GetGranteeListSQL("", {}, revoke_from_action->revokee_expr_list()));
        revokees = absl::StrCat("(", revokee_list, ")");
      }
      alter_action_sql = absl::StrCat("REVOKE FROM ", revokees);
    } break;
    case RESOLVED_RENAME_TO_ACTION: {
      auto* rename_to_action = alter_action->GetAs<ResolvedRenameToAction>();
      alter_action_sql = absl::StrCat(
          "RENAME TO ", IdentifierPathToString(rename_to_action->new_path()));
    } break;
    case RESOLVED_SET_AS_ACTION: {
      auto* set_as_action = alter_action->GetAs<ResolvedSetAsAction>();
      if (!set_as_action->entity_body_json().empty()) {
        alter_action_sql = absl::StrCat(
            "SET AS JSON ", ToStringLiteral(set_as_action->entity_body_json()));
      }
      if (!set_as_action->entity_body_text().empty()) {
        alter_action_sql = absl::StrCat(
            "SET AS ", ToStringLiteral(set_as_action->entity_body_text()));
      }
    } break;
    case RESOLVED_ADD_CONSTRAINT_ACTION: {
      auto* action = alter_action->GetAs<ResolvedAddConstraintAction>();
      action->MarkFieldsAccessed();
      switch (action->constraint()->node_kind()) {
        case RESOLVED_FOREIGN_KEY: {
          auto* foreign_key = action->constraint()->GetAs<ResolvedForeignKey>();
          GOOGLESQL_ASSIGN_OR_RETURN(
              std::string action_sql,
              ProcessForeignKey(foreign_key, action->is_if_not_exists()));
          alter_action_sql = absl::StrCat("ADD ", action_sql);
        } break;
        case RESOLVED_PRIMARY_KEY: {
          auto* primary_key = action->constraint()->GetAs<ResolvedPrimaryKey>();

          std::string action_sql = "ADD ";
          if (!primary_key->constraint_name().empty()) {
            absl::StrAppend(&action_sql, "CONSTRAINT ");
            if (action->is_if_not_exists()) {
              absl::StrAppend(&action_sql, "IF NOT EXISTS ");
            }
            absl::StrAppend(&action_sql, primary_key->constraint_name(), " ");
          }

          GOOGLESQL_ASSIGN_OR_RETURN(std::string primary_key_sql,
                           ProcessPrimaryKey(primary_key));
          absl::StrAppend(&action_sql, primary_key_sql);
          alter_action_sql = action_sql;
        } break;
        default:
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected constraint: "
                           << action->constraint()->node_kind();
      }
    } break;
    case RESOLVED_DROP_CONSTRAINT_ACTION: {
      auto* action = alter_action->GetAs<ResolvedDropConstraintAction>();
      std::string action_sql = "DROP CONSTRAINT ";
      if (action->is_if_exists()) {
        absl::StrAppend(&action_sql, "IF EXISTS ");
      }
      absl::StrAppend(&action_sql, action->name());
      alter_action_sql = action_sql;
    } break;
    case RESOLVED_DROP_PRIMARY_KEY_ACTION: {
      auto* action = alter_action->GetAs<ResolvedDropPrimaryKeyAction>();
      std::string action_sql = "DROP PRIMARY KEY";
      if (action->is_if_exists()) {
        absl::StrAppend(&action_sql, " IF EXISTS");
      }
      alter_action_sql = action_sql;
    } break;
    case RESOLVED_ALTER_COLUMN_OPTIONS_ACTION: {
      auto* action = alter_action->GetAs<ResolvedAlterColumnOptionsAction>();
      std::string action_sql = absl::StrCat(
          "ALTER COLUMN ", action->is_if_exists() ? "IF EXISTS " : "");
      absl::StrAppend(&action_sql, action->column());
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                       GetHintListString(action->option_list()));
      absl::StrAppend(&action_sql, " SET OPTIONS(", options_string, ") ");
      alter_action_sql = action_sql;
    } break;
    case RESOLVED_ALTER_COLUMN_SET_DATA_TYPE_ACTION: {
      const auto* action =
          alter_action->GetAs<ResolvedAlterColumnSetDataTypeAction>();
      std::string action_sql = "ALTER COLUMN ";
      if (action->is_if_exists()) {
        absl::StrAppend(&action_sql, "IF EXISTS ");
      }
      absl::StrAppend(&action_sql, action->column());
      absl::StrAppend(&action_sql, " SET DATA TYPE ");
      GOOGLESQL_RETURN_IF_ERROR(AppendColumnSchema(
          action->updated_type(), /*is_hidden=*/false,
          action->updated_annotations(), /*generated_column_info=*/nullptr,
          /*default_value=*/nullptr, &action_sql));

      // Dummy access on the fields so as to pass the final
      // CheckFieldsAccessed() on a statement level before building the sql.
      action->updated_type_parameters();

      alter_action_sql = action_sql;
    } break;
    case RESOLVED_ALTER_COLUMN_DROP_NOT_NULL_ACTION: {
      auto* action =
          alter_action->GetAs<ResolvedAlterColumnDropNotNullAction>();
      alter_action_sql = absl::StrCat(
          "ALTER COLUMN ", action->is_if_exists() ? "IF EXISTS " : "",
          action->column(), " DROP NOT NULL");
    } break;
    case RESOLVED_ALTER_COLUMN_SET_DEFAULT_ACTION: {
      auto* action = alter_action->GetAs<ResolvedAlterColumnSetDefaultAction>();
      // Mark the field accessed to avoid test failures, even when it is not
      // used in building SQL.
      action->default_value()->expression()->MarkFieldsAccessed();
      alter_action_sql = absl::StrCat(
          "ALTER COLUMN ", action->is_if_exists() ? "IF EXISTS " : "",
          action->column(), " SET DEFAULT ", action->default_value()->sql());
    } break;
    case RESOLVED_ALTER_COLUMN_DROP_DEFAULT_ACTION: {
      auto* action =
          alter_action->GetAs<ResolvedAlterColumnDropDefaultAction>();
      alter_action_sql = absl::StrCat(
          "ALTER COLUMN ", action->is_if_exists() ? "IF EXISTS " : "",
          action->column(), " DROP DEFAULT");
    } break;
    case RESOLVED_ALTER_COLUMN_DROP_GENERATED_ACTION: {
      auto* action =
          alter_action->GetAs<ResolvedAlterColumnDropGeneratedAction>();
      alter_action_sql = absl::StrCat(
          "ALTER COLUMN ", action->is_if_exists() ? "IF EXISTS " : "",
          action->column(), " DROP GENERATED");
    } break;
    case RESOLVED_ALTER_COLUMN_SET_GENERATED_ACTION: {
      auto* action =
          alter_action->GetAs<ResolvedAlterColumnSetGeneratedAction>();
      alter_action_sql = absl::StrCat(
          "ALTER COLUMN ", action->is_if_exists() ? "IF EXISTS " : "",
          action->column(), " SET GENERATED");
      GOOGLESQL_RETURN_IF_ERROR(AppendGeneratedColumnInfo(action->generated_column_info(),
                                                &alter_action_sql));
    } break;
    case RESOLVED_SET_COLLATE_CLAUSE: {
      auto* action = alter_action->GetAs<ResolvedSetCollateClause>();
      std::string action_sql = "SET DEFAULT COLLATE ";
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> collation,
                       ProcessNode(action->collation_name()));
      absl::StrAppend(&action_sql, collation->GetSQL());
      alter_action_sql = action_sql;
    } break;
    case RESOLVED_ALTER_SUB_ENTITY_ACTION: {
      auto* action = alter_action->GetAs<ResolvedAlterSubEntityAction>();
      std::string action_sql = "ALTER ";
      absl::StrAppend(&action_sql, action->entity_type(), " ",
                      action->is_if_exists() ? "IF EXISTS " : "",
                      action->name(), " ");

      const ResolvedAlterAction* original = action->alter_action();
      GOOGLESQL_ASSIGN_OR_RETURN(const std::string actions_string,
                       GetAlterActionSQL(original));
      absl::StrAppend(&action_sql, actions_string);

      alter_action_sql = action_sql;
    } break;
    case RESOLVED_ADD_SUB_ENTITY_ACTION: {
      auto* action = alter_action->GetAs<ResolvedAddSubEntityAction>();
      std::string action_sql = "ADD ";
      absl::StrAppend(&action_sql, action->entity_type(), " ",
                      action->is_if_not_exists() ? "IF NOT EXISTS " : "",
                      action->name(), " ");
      if (!action->options_list().empty()) {
        GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                         GetHintListString(action->options_list()));
        absl::StrAppend(&action_sql, "OPTIONS(", options_string, ") ");
      }
      alter_action_sql = action_sql;
    } break;
    case RESOLVED_DROP_SUB_ENTITY_ACTION: {
      auto* action = alter_action->GetAs<ResolvedDropSubEntityAction>();
      std::string action_sql = "DROP ";
      absl::StrAppend(&action_sql, action->entity_type(), " ",
                      action->is_if_exists() ? "IF EXISTS " : "",
                      action->name(), " ");

      alter_action_sql = action_sql;
    } break;
    case RESOLVED_REBUILD_ACTION: {
      alter_action_sql = "REBUILD";
    } break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected AlterAction: "
                       << alter_action->DebugString();
  }
  return alter_action_sql;
}

namespace {

typedef std::string (*Escaper)(absl::string_view);

// Formatter which escapes a string with given escaper function, such as
// ToStringLiteral or ToIdentifierLiteral.
class EscapeFormatter {
 public:
  explicit EscapeFormatter(Escaper escaper) : escaper_(escaper) {}
  void operator()(std::string* out, absl::string_view in) const {
    absl::StrAppend(out, escaper_(in));
  }

 private:
  Escaper escaper_;
};

}  // namespace

absl::StatusOr<std::string> SQLBuilder::GetGranteeListSQL(
    absl::string_view prefix, const std::vector<std::string>& grantee_list,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> grantee_expr_list) {
  std::string sql;
  // We ABSL_CHECK the expected invariant that only one of grantee_list or
  // grantee_expr_list is empty.
  if (!grantee_list.empty()) {
    GOOGLESQL_RET_CHECK(grantee_expr_list.empty());
    absl::StrAppend(
        &sql, prefix,
        absl::StrJoin(grantee_list, ", ", EscapeFormatter(ToStringLiteral)));
  }
  if (!grantee_expr_list.empty()) {
    GOOGLESQL_RET_CHECK(grantee_list.empty());
    std::vector<std::string> grantee_string_list;
    for (const std::unique_ptr<const ResolvedExpr>& grantee :
         grantee_expr_list) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(grantee.get()));
      grantee_string_list.push_back(result->GetSQL());
    }
    absl::StrAppend(&sql, prefix, absl::StrJoin(grantee_string_list, ", "));
  }
  return sql;
}

absl::Status SQLBuilder::VisitResolvedAlterPrivilegeRestrictionStmt(
    const ResolvedAlterPrivilegeRestrictionStmt* node) {
  std::string sql = "ALTER PRIVILEGE RESTRICTION ";
  if (node->is_if_exists()) {
    absl::StrAppend(&sql, "IF EXISTS ");
  }
  absl::StrAppend(&sql, "ON ");

  std::vector<std::string> privilege_list_sql;
  for (const auto& privilege : node->column_privilege_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(privilege.get()));
    privilege_list_sql.push_back(result->GetSQL());
  }
  absl::StrAppend(&sql, absl::StrJoin(privilege_list_sql, ", "));

  absl::StrAppend(&sql, " ON ", node->object_type(), " ",
                  IdentifierPathToString(node->name_path()), " ");
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string actions_string,
                   GetAlterActionListSQL(node->alter_action_list()));
  absl::StrAppend(&sql, " ", actions_string);
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAlterRowAccessPolicyStmt(
    const ResolvedAlterRowAccessPolicyStmt* node) {
  std::string sql = "ALTER ROW ACCESS POLICY ";
  absl::StrAppend(&sql, node->is_if_exists() ? "IF EXISTS " : "");
  absl::StrAppend(&sql, ToIdentifierLiteral(node->name()));
  absl::StrAppend(&sql, " ON ", IdentifierPathToString(node->name_path()));
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string actions_string,
                   GetAlterActionListSQL(node->alter_action_list()));
  absl::StrAppend(&sql, " ", actions_string);
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAlterAllRowAccessPoliciesStmt(
    const ResolvedAlterAllRowAccessPoliciesStmt* node) {
  std::string sql = "ALTER ALL ROW ACCESS POLICIES ON ";
  absl::StrAppend(&sql, IdentifierPathToString(node->name_path()));
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string actions_string,
                   GetAlterActionListSQL(node->alter_action_list()));
  absl::StrAppend(&sql, " ", actions_string);
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedPrivilege(const ResolvedPrivilege* node) {
  std::string sql;

  absl::StrAppend(&sql, ToIdentifierLiteral(node->action_type()));

  if (!node->unit_list().empty()) {
    std::vector<std::string> unit_string_list;
    for (const std::unique_ptr<const ResolvedObjectUnit>& unit :
         node->unit_list()) {
      std::vector<std::string> formatted_identifiers;
      for (const std::string& name : unit->name_path()) {
        formatted_identifiers.push_back(ToIdentifierLiteral(name));
      }
      unit_string_list.push_back(absl::StrJoin(formatted_identifiers, "."));
    }
    absl::StrAppend(&sql, "(", absl::StrJoin(unit_string_list, ", "), ")");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::AddNullHandlingModifier(
    ResolvedNonScalarFunctionCallBase::NullHandlingModifier kind,
    std::string* arguments_suffix) {
  switch (kind) {
    case ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING:
      return absl::OkStatus();
    case ResolvedNonScalarFunctionCallBase::IGNORE_NULLS:
      absl::StrAppend(arguments_suffix, " IGNORE NULLS");
      return absl::OkStatus();
    case ResolvedNonScalarFunctionCallBase::RESPECT_NULLS:
      absl::StrAppend(arguments_suffix, " RESPECT NULLS");
      return absl::OkStatus();
      // No "default:". Let the compilation fail in case an entry is added to
      // the enum without being handled here.
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Encountered invalid NullHandlingModifier " << kind;
}

absl::Status SQLBuilder::VisitResolvedAggregateHavingModifier(
    const ResolvedAggregateHavingModifier* node) {
  std::string sql(" HAVING ");
  if (node->kind() == ResolvedAggregateHavingModifier::MAX) {
    absl::StrAppend(&sql, "MAX ");
  } else {
    absl::StrAppend(&sql, "MIN ");
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->having_expr()));
  absl::StrAppend(&sql, result->GetSQL());
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGetProtoOneof(
    const ResolvedGetProtoOneof* node) {
  std::string sql("EXTRACT(ONEOF_CASE(");
  absl::StrAppend(&sql, node->oneof_descriptor()->name(), ") FROM ");

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));
  absl::StrAppend(&sql, result->GetSQL(), ")");

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGrantStmt(const ResolvedGrantStmt* node) {
  std::string sql, privileges_string;

  GOOGLESQL_RETURN_IF_ERROR(GetPrivilegesString(node, &privileges_string));

  absl::StrAppend(&sql, "GRANT ", privileges_string);

  GOOGLESQL_ASSIGN_OR_RETURN(std::string grantee_sql,
                   GetGranteeListSQL(" TO ", node->grantee_list(),
                                     node->grantee_expr_list()));
  GOOGLESQL_RET_CHECK(!grantee_sql.empty());
  absl::StrAppend(&sql, grantee_sql);

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedRevokeStmt(
    const ResolvedRevokeStmt* node) {
  std::string sql, privileges_string;

  GOOGLESQL_RETURN_IF_ERROR(GetPrivilegesString(node, &privileges_string));

  absl::StrAppend(&sql, "REVOKE ", privileges_string);

  GOOGLESQL_ASSIGN_OR_RETURN(std::string grantee_sql,
                   GetGranteeListSQL(" FROM ", node->grantee_list(),
                                     node->grantee_expr_list()));
  GOOGLESQL_RET_CHECK(!grantee_sql.empty());
  absl::StrAppend(&sql, grantee_sql);

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedRenameStmt(
    const ResolvedRenameStmt* node) {
  std::string sql;
  absl::StrAppend(&sql, "RENAME ", ToIdentifierLiteral(node->object_type()),
                  " ", IdentifierPathToString(node->old_name_path()), " TO ",
                  IdentifierPathToString(node->new_name_path()));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

std::string SQLBuilder::sql() {
  if (sql_.empty()) {
    if (query_fragments_.empty()) {
      // No sql to build here since no ResolvedAST was visited before calling
      // sql().
      return "";
    }

    std::unique_ptr<QueryFragment> query_fragment = PopQueryFragment();
    ABSL_DCHECK(query_fragments_.empty());
    sql_ = query_fragment->GetSQL();
    GOOGLESQL_DCHECK_OK(query_fragment->node->CheckFieldsAccessed()) << "sql is\n"
                                                           << sql_;
  }
  return sql_;
}

absl::StatusOr<std::string> SQLBuilder::GetSql() {
  if (sql_.empty()) {
    GOOGLESQL_RET_CHECK(!query_fragments_.empty())
        << "No nodes visited before calling GetSql()";

    std::unique_ptr<QueryFragment> query_fragment = PopQueryFragment();
    GOOGLESQL_RET_CHECK(query_fragments_.empty());
    sql_ = query_fragment->GetSQL();
    if (GOOGLESQL_DEBUG_MODE) {
      GOOGLESQL_RETURN_IF_ERROR(query_fragment->node->CheckFieldsAccessed()) << "sql is\n"
                                                                   << sql_;
    }
  }
  return sql_;
}

absl::Status SQLBuilder::VisitResolvedCreatePrivilegeRestrictionStmt(
    const ResolvedCreatePrivilegeRestrictionStmt* node) {
  std::string sql = "CREATE ";
  if (node->create_mode() == ResolvedCreateStatement::CREATE_OR_REPLACE) {
    absl::StrAppend(&sql, "OR REPLACE ");
  }
  absl::StrAppend(&sql, "PRIVILEGE RESTRICTION ");
  if (node->create_mode() == ResolvedCreateStatement::CREATE_IF_NOT_EXISTS) {
    absl::StrAppend(&sql, "IF NOT EXISTS ");
  }
  absl::StrAppend(&sql, "ON ");

  std::vector<std::string> privilege_list_sql;
  for (const auto& privilege : node->column_privilege_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(privilege.get()));
    privilege_list_sql.push_back(result->GetSQL());
  }
  absl::StrAppend(&sql, absl::StrJoin(privilege_list_sql, ", "));
  absl::StrAppend(&sql, " ON ", node->object_type(), " ",
                  IdentifierPathToString(node->name_path()));

  std::vector<std::string> empty_list;
  GOOGLESQL_ASSIGN_OR_RETURN(std::string restrictee_sql,
                   GetGranteeListSQL("", empty_list, node->restrictee_list()));

  if (!restrictee_sql.empty()) {
    absl::StrAppend(&sql, " RESTRICT TO (");
    absl::StrAppend(&sql, restrictee_sql);
    absl::StrAppend(&sql, ")");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateRowAccessPolicyStmt(
    const ResolvedCreateRowAccessPolicyStmt* node) {
  std::string sql = "CREATE ";
  if (node->create_mode() == ResolvedCreateStatement::CREATE_OR_REPLACE) {
    absl::StrAppend(&sql, "OR REPLACE ");
  }
  absl::StrAppend(&sql, "ROW ACCESS POLICY ");
  if (node->create_mode() == ResolvedCreateStatement::CREATE_IF_NOT_EXISTS) {
    absl::StrAppend(&sql, "IF NOT EXISTS ");
  }

  if (!node->name().empty()) {
    absl::StrAppend(&sql, ToIdentifierLiteral(node->name()), " ");
  }

  absl::StrAppend(&sql, "ON ",
                  IdentifierPathToString(node->target_name_path()));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string grantee_sql,
      GetGranteeListSQL("", node->grantee_list(), node->grantee_expr_list()));
  if (!grantee_sql.empty()) {
    absl::StrAppend(&sql, " GRANT TO (");
    absl::StrAppend(&sql, grantee_sql);
    absl::StrAppend(&sql, ")");
  }

  // ProcessNode(node->predicate()) should produce an equivalent QueryFragment,
  // but we use the string form directly because it's simpler.
  absl::StrAppend(&sql, " FILTER USING (", node->predicate_str(), ")");

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedImportStmt(
    const ResolvedImportStmt* node) {
  std::string sql = "IMPORT ";
  switch (node->import_kind()) {
    case ResolvedImportStmt::MODULE:
      GOOGLESQL_RET_CHECK(node->file_path().empty());
      absl::StrAppend(&sql, "MODULE ");
      absl::StrAppend(&sql, IdentifierPathToString(node->name_path()), " ");
      break;
    case ResolvedImportStmt::PROTO:
      GOOGLESQL_RET_CHECK(node->name_path().empty());
      absl::StrAppend(&sql, "PROTO ");
      absl::StrAppend(&sql, ToStringLiteral(node->file_path()));
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected ImportKind " << node->import_kind()
                       << " for node: " << node->DebugString();
      break;
  }

  std::string alias_prefix;
  std::string alias;
  if (!node->alias_path().empty()) {
    alias_prefix = "AS ";
    alias = IdentifierPathToString(node->alias_path());
  } else if (!node->into_alias_path().empty()) {
    alias_prefix = "INTO ";
    alias = IdentifierPathToString(node->into_alias_path());
  }
  if (!alias.empty()) {
    absl::StrAppend(&sql, alias_prefix, alias, " ");
  }

  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, "OPTIONS(", result, ") ");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedModuleStmt(
    const ResolvedModuleStmt* node) {
  std::string sql = "MODULE ";
  absl::StrAppend(&sql, IdentifierPathToString(node->name_path()), " ");
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string result,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, "OPTIONS(", result, ") ");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::ProcessExecuteImmediateArgument(
    const ResolvedExecuteImmediateArgument* node) {
  std::string sql;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                   ProcessNode(node->expression()));
  absl::StrAppend(&sql, expr->GetSQL());
  if (!node->name().empty()) {
    absl::StrAppend(&sql, " AS ");
    absl::StrAppend(&sql, node->name());
  }
  return sql;
}

absl::Status SQLBuilder::VisitResolvedExecuteImmediateStmt(
    const ResolvedExecuteImmediateStmt* node) {
  std::string sql = "EXECUTE IMMEDIATE ";
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> dynamic_sql,
                   ProcessNode(node->sql()));
  absl::StrAppend(&sql, dynamic_sql->GetSQL());

  if (!node->into_identifier_list().empty()) {
    std::string identifiers = absl::StrJoin(node->into_identifier_list(), ", ");
    absl::StrAppend(&sql, " INTO ", identifiers);
  }

  if (!node->using_argument_list().empty()) {
    absl::StrAppend(&sql, " USING ");
    // After parsing, this list is guaranteed to have at least one argument.
    for (int i = 0; i < node->using_argument_list_size(); i++) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::string arg_sql, ProcessExecuteImmediateArgument(
                                                node->using_argument_list(i)));
      if (i > 0) {
        absl::StrAppend(&sql, ", ");
      }
      absl::StrAppend(&sql, arg_sql);
    }
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

void SQLBuilder::PushSQLForQueryExpression(const ResolvedNode* node,
                                           QueryExpression* query_expression) {
  PushQueryFragment(node, query_expression);
  DumpQueryFragmentStack();
}

absl::Status SQLBuilder::DefaultVisit(const ResolvedNode* node) {
  GOOGLESQL_RET_CHECK_FAIL() << "SQLBuilder visitor not implemented for "
                   << node->node_kind_string();
}

absl::StatusOr<std::string> SQLBuilder::GetUpdateItemListSQL(
    absl::Span<const std::unique_ptr<const ResolvedUpdateItem>>
        update_item_list) {
  std::vector<std::string> update_item_list_sql;
  update_item_list_sql.reserve(update_item_list.size());
  for (const auto& update_item : update_item_list) {
    mutable_update_item_targets_and_offsets().emplace_back();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(update_item.get()));
    mutable_update_item_targets_and_offsets().pop_back();

    update_item_list_sql.push_back(result->GetSQL());
  }
  return absl::StrJoin(update_item_list_sql, ", ");
}

std::string SQLBuilder::GetInsertColumnListSQL(
    absl::Span<const ResolvedColumn> insert_column_list) const {
  std::vector<std::string> columns_sql;
  columns_sql.reserve(insert_column_list.size());
  for (const auto& col : insert_column_list) {
    columns_sql.push_back(ToIdentifierLiteral(col.name()));
  }
  return absl::StrJoin(columns_sql, ", ");
}

absl::Status SQLBuilder::AddValueTableAliasForVisitResolvedTableScan(
    absl::string_view table_alias, const ResolvedColumn& column,
    SQLAliasPairList* select_list) {
  // Use the table name instead for selecting value table column.
  select_list->push_back({std::string(table_alias), GetColumnAlias(column)});
  return absl::OkStatus();
}

std::string SQLBuilder::TableToIdentifierLiteral(const Table* table) {
  return TableNameToIdentifierLiteral(table->Name());
}

std::string SQLBuilder::TableNameToIdentifierLiteral(
    absl::string_view table_name) {
  return ToIdentifierLiteral(table_name);
}

std::string SQLBuilder::GetTableAliasForVisitResolvedTableScan(
    const ResolvedTableScan& node, std::string* from) {
  std::string table_alias;
  // Check collision against columns.
  if (col_ref_names().contains(absl::AsciiStrToLower(node.table()->Name()))) {
    table_alias = GetTableAlias(node.table());
    absl::StrAppend(from, " AS ", table_alias);
  } else {
    // Check collision against other tables. If we select from T and S.T we have
    // to alias one of them to avoid collision for example.
    if (CanTableBeUsedWithImplicitAlias(node.table())) {
      table_alias = ToIdentifierLiteral(node.table()->Name());
    } else {
      table_alias = GetTableAlias(node.table());
      absl::StrAppend(from, " AS ", table_alias);
    }
  }
  return table_alias;
}

std::string SQLBuilder::GenerateUniqueAliasName() {
  return absl::StrCat("a_", GetUniqueId());
}

absl::Status SQLBuilder::VisitResolvedRecursiveScan(
    const ResolvedRecursiveScan* node) {
  bool is_pipe = false;
  CopyableState::RecursiveQueryInfo recursive_query_info;
  if (!state_.recursive_query_info.empty() &&
      state_.recursive_query_info.top().scan == nullptr) {
    // Recursive scan under WITH. Update the placeholder scan with the actual
    // scan.
    recursive_query_info = state_.recursive_query_info.top();
    recursive_query_info.scan = node;
    // The updated recursive query info will be pushed back onto the stack
    // before visiting the recursive term.
    state_.recursive_query_info.pop();
  } else {
    // Standalone recursive scans can only happen for pipe recursive unions.
    // We intentionally allow generating pipe recursive unions even if the
    // sqlbuilder mode is standard.
    is_pipe = true;
    // TODO: b/357999315 - Provide better SqlBuilder support for pipe recursive
    // union by fixing the following limitations:
    // - A subquery is always used instead of a subpipeline.
    // - BY NAME and CORRESPONDING are not preserved.
    // - (Optional) An output alias is always generated.
    std::string query_name =
        absl::StrCat("pipe_recursive_", GenerateUniqueAliasName());
    recursive_query_info = {
        .query_name = query_name,
        .scan = node,
    };
  }

  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  std::vector<std::unique_ptr<QueryExpression>> set_op_scan_list;

  const ResolvedColumnList columns_excluding_depth =
      GetRecursiveScanColumnsExcludingDepth(node);
  for (const auto& input_item : std::vector<const ResolvedSetOperationItem*>{
           node->non_recursive_term(), node->recursive_term()}) {
    GOOGLESQL_RET_CHECK_EQ(input_item->output_column_list_size(),
                 columns_excluding_depth.size());
    const ResolvedScan* scan = input_item->scan();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result, ProcessNode(scan));
    set_op_scan_list.push_back(std::move(result->query_expression));
    GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(input_item->output_column_list(),
                                          set_op_scan_list.back().get()));

    if (input_item == node->non_recursive_term()) {
      // Set up column aliases before processing the recursive term to ensure
      // that they match the non-recursive term.
      const SQLAliasPairList& first_select_list =
          set_op_scan_list[0]->SelectList();
      // If node->column_list() was empty, first_select_list will have a NULL
      // added.
      GOOGLESQL_RET_CHECK_EQ(first_select_list.size(),
                   std::max<std::size_t>(columns_excluding_depth.size(), 1));
      for (int i = 0; i < columns_excluding_depth.size(); i++) {
        if (googlesql_base::ContainsKey(computed_column_alias(),
                             node->column_list(i).column_id())) {
          GOOGLESQL_RET_CHECK_EQ(
              computed_column_alias().at(node->column_list(i).column_id()),
              first_select_list[i].second);
        } else {
          googlesql_base::InsertOrDie(&mutable_computed_column_alias(),
                           node->column_list(i).column_id(),
                           first_select_list[i].second);
        }
      }
      // The new `recursive_query_info` corresponds to the
      // ResolvedRecursiveRefScan in the recursive-term, not the non-recursive
      // term, so we push it onto the stack before visiting the recursive term.
      //
      // The recursive ref scan in the non-recursive term, if it exists,
      // corresponds to `stack.recursive_query_info.top()`, not the new
      // `recursive_query_info`. Example query:
      //
      // ```SQL
      // WITH RECURSIVE
      //   t AS (
      //     SELECT 1 AS col
      //     UNION ALL
      //     (
      //        -- `t` will appear in the non-recursive term for the pipe
      //        -- recursive union.
      //        FROM t
      //        |> RECURSIVE UNION ALL (FROM inner_alias) AS inner_alias
      //     )
      //   )
      // FROM t
      // ```
      state_.recursive_query_info.push(recursive_query_info);
    }

    // If the query's column list doesn't exactly match the input to the
    // union, then add a wrapper query to make sure we have the right number
    // of columns and they have unique aliases.
    if (input_item->output_column_list() != scan->column_list()) {
      GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(scan, set_op_scan_list.back().get()));
      GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(input_item->output_column_list(),
                                            set_op_scan_list.back().get()));
    }
  }

  std::string query_hints;
  if (node->hint_list_size() > 0) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &query_hints));
  }

  auto [op, all_or_distinct] = GetOpTypePair(node->op_type());
  std::string with_depth_modifier = "";
  if (is_pipe) {
    op = absl::StrCat("|> RECURSIVE ", op);
    // Unlike WITH RECURSIVE where the depth modifier is unparsed in
    // VisitResolvedWithScan(), the depth modifier for a pipe recursive union
    // is after the set operation, e.g. "|> RECURSIVE UNION ALL WITH DEPTH",
    // so we unparse it here.
    if (node->recursion_depth_modifier() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(auto modifier,
                       ProcessNode(node->recursion_depth_modifier()));
      with_depth_modifier = modifier->GetSQL();
    }
  }

  // TODO: Get `set_op_column_match_mode` directly from the
  // ResolvedRecurisveScan node once we add that field to ResolvedRecursiveScan.
  GOOGLESQL_RET_CHECK(query_expression->TrySetSetOpScanList(
      &set_op_scan_list, op, all_or_distinct,
      /*set_op_column_match_mode=*/"", /*set_op_column_propagation_mode=*/"",
      query_hints, with_depth_modifier));

  // Add the output alias if it is a pipe recursive union.
  if (is_pipe) {
    std::string sql = query_expression->GetSQLQuery();
    absl::StrAppend(&sql, " AS ", recursive_query_info.query_name);
    GOOGLESQL_ASSIGN_OR_RETURN(query_expression,
                     MaybeWrapPipeQueryAsStandardQuery(*node, sql));
  }
  PushSQLForQueryExpression(node, query_expression.release());
  GOOGLESQL_RET_CHECK(!state_.recursive_query_info.empty());
  state_.recursive_query_info.pop();
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedRecursionDepthModifier(
    const ResolvedRecursionDepthModifier* node) {
  std::string lower_bound = "UNBOUNDED", upper_bound = "UNBOUNDED";
  if (node->lower_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> lower,
                     ProcessNode(node->lower_bound()));
    lower_bound = lower->GetSQL();
  }
  if (node->upper_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> upper,
                     ProcessNode(node->upper_bound()));
    upper_bound = upper->GetSQL();
  }
  std::string sql = "WITH DEPTH";
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> column_alias,
                   ProcessNode(node->recursion_depth_column()));
  absl::StrAppend(&sql, " AS ", column_alias->GetSQL(), " BETWEEN ",
                  std::move(lower_bound), " AND ", std::move(upper_bound));
  PushQueryFragment(node, std::move(sql));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedRecursiveRefScan(
    const ResolvedRecursiveRefScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  const std::string alias = GetScanAlias(node);
  GOOGLESQL_RET_CHECK(!state_.recursive_query_info.empty())
      << "Found ResolvedRecursiveRefScan node without a corresponding "
      << "ResolvedRecursiveScan";

  const ResolvedScan* with_scan = state_.recursive_query_info.top().scan;
  std::string query_name = state_.recursive_query_info.top().query_name;
  GOOGLESQL_RET_CHECK(with_scan->Is<ResolvedRecursiveScan>());
  const ResolvedColumnList columns_excluding_depth =
      GetRecursiveScanColumnsExcludingDepth(
          with_scan->GetAs<ResolvedRecursiveScan>());

  std::string from;
  absl::StrAppend(&from, query_name, " AS ", alias);
  GOOGLESQL_RET_CHECK_EQ(node->column_list_size(), columns_excluding_depth.size());
  for (int i = 0; i < node->column_list_size(); ++i) {
    // Entry was added to computed_column_alias_ back in
    // VisitResolvedRecursiveScan() while processing the non-recursive term.
    GOOGLESQL_RET_CHECK(googlesql_base::ContainsKey(computed_column_alias(),
                               with_scan->column_list(i).column_id()))
        << "column id: " << node->column_list(i).column_id()
        << "\nComputed column aliases:\n"
        << ComputedColumnAliasDebugString();
    googlesql_base::InsertOrDie(
        &mutable_computed_column_alias(), node->column_list(i).column_id(),
        computed_column_alias().at(with_scan->column_list(i).column_id()));
  }
  SetPathForColumnList(node->column_list(), alias);
  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));
  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreateEntityStmt(
    const ResolvedCreateEntityStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, node->entity_type(), &sql));
  if (node->option_list_size() > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, " OPTIONS(", options_string, ") ");
  }

  if (!node->entity_body_json().empty()) {
    absl::StrAppend(&sql, "AS JSON ",
                    ToStringLiteral(node->entity_body_json()));
  }

  if (!node->entity_body_text().empty()) {
    absl::StrAppend(&sql, "AS ", ToStringLiteral(node->entity_body_text()));
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAlterEntityStmt(
    const ResolvedAlterEntityStmt* node) {
  return GetResolvedAlterObjectStmtSQL(node, node->entity_type());
}

absl::Status SQLBuilder::GetLoadDataPartitionFilterString(
    const ResolvedAuxLoadDataPartitionFilter* partition_filter,
    std::string* sql) {
  GOOGLESQL_RET_CHECK(partition_filter != nullptr);
  if (partition_filter->is_overwrite()) {
    absl::StrAppend(sql, "OVERWRITE ");
  }
  absl::StrAppend(sql, "PARTITIONS(");

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> expr,
                   ProcessNode(partition_filter->filter()));
  absl::StrAppend(sql, expr->GetSQL());
  absl::StrAppend(sql, ")");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAuxLoadDataStmt(
    const ResolvedAuxLoadDataStmt* node) {
  std::string sql = "LOAD DATA ";
  switch (node->insertion_mode()) {
    case ResolvedAuxLoadDataStmtEnums::OVERWRITE:
      absl::StrAppend(&sql, "OVERWRITE ");
      break;
    default:
      absl::StrAppend(&sql, "INTO ");
      break;
  }
  if (node->is_temp_table()) {
    absl::StrAppend(&sql, "TEMP TABLE ");
  }
  absl::StrAppend(&sql, IdentifierPathToString(node->name_path()));
  GOOGLESQL_RETURN_IF_ERROR(ProcessTableElementsBase(
      &sql, node->column_definition_list(), node->primary_key(),
      node->foreign_key_list(), node->check_constraint_list()));

  // Make column aliases available for PARTITION BY, CLUSTER BY and table
  // constraints.
  for (const ResolvedColumn& column : node->pseudo_column_list()) {
    mutable_computed_column_alias()[column.column_id()] = column.name();
  }
  for (const auto& col : node->output_column_list()) {
    mutable_computed_column_alias()[col->column().column_id()] = col->name();
  }

  if (node->partition_filter() != nullptr) {
    absl::StrAppend(&sql, "\n");
    GOOGLESQL_RETURN_IF_ERROR(
        GetLoadDataPartitionFilterString(node->partition_filter(), &sql));
  }
  if (!node->partition_by_list().empty()) {
    absl::StrAppend(&sql, "\nPARTITION BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->partition_by_list(), &sql));
  }

  if (!node->cluster_by_list().empty()) {
    absl::StrAppend(&sql, "\nCLUSTER BY ");
    GOOGLESQL_RETURN_IF_ERROR(GetPartitionByListString(node->cluster_by_list(), &sql));
  }

  if (!node->option_list().empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string options_string,
                     GetHintListString(node->option_list()));
    absl::StrAppend(&sql, "\nOPTIONS(", options_string, ") ");
  }
  GOOGLESQL_ASSIGN_OR_RETURN(const std::string source_files_options,
                   GetHintListString(node->from_files_option_list()));
  absl::StrAppend(&sql, "\nFROM FILES(", source_files_options, ")");
  if (node->with_partition_columns() != nullptr) {
    absl::StrAppend(&sql, "\n");
    GOOGLESQL_RETURN_IF_ERROR(
        ProcessWithPartitionColumns(&sql, node->with_partition_columns()));
  }
  if (node->connection() != nullptr) {
    const std::string connection_alias =
        ToIdentifierLiteral(node->connection()->connection()->Name());
    absl::StrAppend(&sql, "\nWITH CONNECTION ", connection_alias, " ");
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedUpdateConstructor(
    const ResolvedUpdateConstructor* node) {
  std::string text;
  absl::StrAppend(&text, "UPDATE (");
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> proto_expr,
                   ProcessNode(node->expr()));
  absl::StrAppend(&text, proto_expr->GetSQL(), ")");
  if (!node->alias().empty()) {
    absl::StrAppend(&text, " AS ", node->alias());
  }
  std::string update_field_item_sql;
  for (const std::unique_ptr<const ResolvedUpdateFieldItem>& update_item :
       node->update_field_item_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> modified_value,
                     ProcessNode(update_item->expr()));
    std::string field_path_sql;
    bool is_first = true;
    bool is_first_extension = false;
    for (const google::protobuf::FieldDescriptor* field :
         update_item->proto_field_path()) {
      if (!field_path_sql.empty()) {
        absl::StrAppend(&field_path_sql, ".");
      }
      if (field->is_extension()) {
        if (is_first) is_first_extension = true;
        absl::StrAppend(&field_path_sql, "(");
      }
      if (is_first) {
        is_first = false;
      }
      absl::StrAppend(&field_path_sql, field->is_extension()
                                           ? field->full_name()
                                           : field->name());
      if (field->is_extension()) {
        absl::StrAppend(&field_path_sql, ")");
      }
    }
    if (is_first_extension && !update_field_item_sql.empty()) {
      absl::StrAppend(&update_field_item_sql, ",\n");
    } else {
      absl::StrAppend(&update_field_item_sql, "\n");
    }
    std::string op_string;
    switch (update_item->operation()) {
      case ResolvedUpdateFieldItem::UPDATE_SINGLE:
        op_string = ":";
        break;
      case ResolvedUpdateFieldItem::UPDATE_MANY:
        op_string = ":*";
        break;
      case ResolvedUpdateFieldItem::UPDATE_SINGLE_NO_CREATION:
        op_string = ":?";
        break;
    }
    absl::StrAppend(&update_field_item_sql, field_path_sql, op_string,
                    modified_value->GetSQL());
  }
  absl::StrAppend(&text, "{", update_field_item_sql, "\n}");

  PushQueryFragment(node, text);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedCreatePropertyGraphStmt(
    const ResolvedCreatePropertyGraphStmt* node) {
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(GetCreateStatementPrefix(node, "PROPERTY GRAPH", &sql));
  absl::StrAppend(&sql, "NODE TABLES(");
  absl::flat_hash_map<std::string, const ResolvedGraphElementLabel*>
      name_to_label;
  for (const std::unique_ptr<const ResolvedGraphElementLabel>& label :
       node->label_list()) {
    GOOGLESQL_RET_CHECK(name_to_label.try_emplace(label->name(), label.get()).second);
  }

  GOOGLESQL_RETURN_IF_ERROR(
      AppendElementTables(node->node_table_list(), name_to_label, sql));
  absl::StrAppend(&sql, ")");
  if (!node->edge_table_list().empty()) {
    absl::StrAppend(&sql, " EDGE TABLES(");
    GOOGLESQL_RETURN_IF_ERROR(
        AppendElementTables(node->edge_table_list(), name_to_label, sql));
    absl::StrAppend(&sql, ")");
  }
  GOOGLESQL_RETURN_IF_ERROR(AppendOptionsIfPresent(node->option_list(), &sql));
  node->MarkFieldsAccessed();
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphElementTableInternal(
    const std::unique_ptr<const ResolvedGraphElementTable>& node,
    const absl::flat_hash_map<std::string, const ResolvedGraphElementLabel*>&
        name_to_label,
    std::string& sql) {
  const ResolvedTableScan* table_scan =
      node->input_scan()->GetAs<ResolvedTableScan>();
  GOOGLESQL_RET_CHECK(table_scan != nullptr);

  // TODO: There is no catalog API returns the right identifier for
  // a table. `Name()` returns a string instead of identifier path.
  //
  // A return check is added to ensure we don't accidentally get away with the
  // bug.
  const std::string input_table_name = table_scan->table()->Name();
  GOOGLESQL_RET_CHECK_EQ(table_scan->table()->FullName(), table_scan->table()->Name());
  if (node->alias() != input_table_name) {
    absl::StrAppend(&sql, ToIdentifierLiteral(input_table_name), " AS ",
                    ToIdentifierLiteral(node->alias()));
  } else {
    absl::StrAppend(&sql, ToIdentifierLiteral(node->alias()));
  }
  if (!node->key_list().empty()) {
    absl::StrAppend(&sql, " KEY(");
    GOOGLESQL_RETURN_IF_ERROR(StrJoin(
        &sql, node->key_list(), ", ",
        [](std::string* out, const std::unique_ptr<const ResolvedExpr>& col) {
          return AppendColumn(out, col.get());
        }));
    absl::StrAppend(&sql, ")");
  }

  if (node->source_node_reference() != nullptr) {
    absl::StrAppend(&sql, " ");
    GOOGLESQL_RETURN_IF_ERROR(VisitResolvedGraphNodeTableReferenceInternal(
        node->source_node_reference(), "SOURCE", sql));
  }
  if (node->dest_node_reference() != nullptr) {
    absl::StrAppend(&sql, " ");
    GOOGLESQL_RETURN_IF_ERROR(VisitResolvedGraphNodeTableReferenceInternal(
        node->dest_node_reference(), "DESTINATION", sql));
  }

  if (!node->label_name_list().empty()) {
    absl::flat_hash_map<std::string, const ResolvedGraphPropertyDefinition*>
        name_to_property_def;
    for (const std::unique_ptr<const ResolvedGraphPropertyDefinition>&
             property_definition : node->property_definition_list()) {
      GOOGLESQL_RET_CHECK(
          name_to_property_def
              .try_emplace(property_definition->property_declaration_name(),
                           property_definition.get())
              .second);
    }
    for (absl::string_view label_name : node->label_name_list()) {
      auto found_label = name_to_label.find(label_name);
      GOOGLESQL_RET_CHECK(found_label != name_to_label.end());
      absl::StrAppend(&sql, " ");
      GOOGLESQL_RETURN_IF_ERROR(AppendLabelAndPropertiesClause(
          found_label->second, name_to_property_def, sql));
    }
  }
  if (options_.language_options.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_LABEL_PROPERTIES_IN_DDL) &&
      node->dynamic_label() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGraphDynamicLabelSpecification(
        node->dynamic_label(), sql));
  }
  if (options_.language_options.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_LABEL_PROPERTIES_IN_DDL) &&
      node->dynamic_properties() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGraphDynamicPropertiesSpecification(
        node->dynamic_properties(), sql));
  }

  table_scan->MarkFieldsAccessed();
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendElementTables(
    absl::Span<const std::unique_ptr<const ResolvedGraphElementTable>> tables,
    const absl::flat_hash_map<std::string, const ResolvedGraphElementLabel*>&
        name_to_label,
    std::string& sql) {
  for (int i = 0; i < tables.size(); i++) {
    if (i > 0) {
      absl::StrAppend(&sql, ", ");
    }
    GOOGLESQL_RETURN_IF_ERROR(
        VisitResolvedGraphElementTableInternal(tables[i], name_to_label, sql));
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlMatchOp(
    const ResolvedGraphScan* node, std::string& output_sql) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> query_fragment,
                   ProcessNode(node));
  absl::StrAppend(&output_sql, query_fragment->GetSQL());
  return absl::OkStatus();
}

static absl::Status CheckScanIsGraphRefOrSingleRowScan(
    const ResolvedScan* scan) {
  GOOGLESQL_RET_CHECK(scan->node_kind() == RESOLVED_GRAPH_REF_SCAN ||
            scan->node_kind() == RESOLVED_SINGLE_ROW_SCAN)
      << "Unexpected scan kind: " << scan->node_kind_string() << ", "
      << scan->DebugString();
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessAnalyticInputsWithNoVerticalAggregation(
    const ResolvedScan* input_scan) {
  // Prepare all the pending computed columns from the window functions,
  // including any precomputed expressions for partition/order by.
  if (!input_scan->Is<ResolvedAnalyticScan>()) {
    return CheckScanIsGraphRefOrSingleRowScan(input_scan);
  }

  const ResolvedAnalyticScan* analytic_scan =
      input_scan->GetAs<ResolvedAnalyticScan>();
  if (analytic_scan->input_scan()->Is<ResolvedProjectScan>()) {
    const ResolvedProjectScan* project_scan =
        analytic_scan->input_scan()->GetAs<ResolvedProjectScan>();
    GOOGLESQL_RETURN_IF_ERROR(
        CheckScanIsGraphRefOrSingleRowScan(project_scan->input_scan()));
    for (const auto& computed_column : project_scan->expr_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> column_fragment,
                       ProcessNode(computed_column->expr()));
      googlesql_base::InsertOrDie(&mutable_pending_columns(),
                       computed_column->column().column_id(),
                       column_fragment->GetSQL());
    }
  } else {
    // 'access' the input scan; there is no SQL to be generated here. This
    // represents the 'graphrefscan' (which represents the working table)
    // generated from the previous statement, if there was one, or the
    // single row scan (unit table) otherwise.
    GOOGLESQL_RETURN_IF_ERROR(
        CheckScanIsGraphRefOrSingleRowScan(analytic_scan->input_scan()));
  }
  for (const auto& fn_group : analytic_scan->function_group_list()) {
    GOOGLESQL_RETURN_IF_ERROR(fn_group->Accept(this));
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlFilterOp(
    const ResolvedFilterScan* node, std::string& output_sql) {
  GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));

  if (node->filter_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->filter_expr()));
    absl::StrAppend(&output_sql, " FILTER WHERE ", result->GetSQL());
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlSampleOp(
    const ResolvedSampleScan* node, std::string& output_sql) {
  GOOGLESQL_RETURN_IF_ERROR(
      ProcessAnalyticInputsWithNoVerticalAggregation(node->input_scan()));

  GOOGLESQL_ASSIGN_OR_RETURN(std::string sample, GetSqlForSample(node, /*is_gql=*/true));
  absl::StrAppend(&output_sql, std::move(sample));

  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlNamedCallOp(
    const ResolvedTVFScan* node, std::string& output_sql) {
  // Grab the implicit table argument. This is the first relation argument to
  // the TVF call.
  int first_relation_arg_index = -1;
  const ResolvedTVFArgument* implicit_input_table = nullptr;
  for (int i = 0; i < node->argument_list_size(); ++i) {
    if (node->argument_list(i)->scan() != nullptr) {
      implicit_input_table = node->argument_list(i);
      first_relation_arg_index = i;
      break;
    }
  }

  GOOGLESQL_RET_CHECK(implicit_input_table != nullptr);
  const ResolvedScan* implicit_input_scan = implicit_input_table->scan();
  GOOGLESQL_RET_CHECK(implicit_input_scan != nullptr);
  GOOGLESQL_RET_CHECK_GE(first_relation_arg_index, 0);

  // Today we produce a query with a WITH statement which requires the
  // SQL_GRAPH_RETURN_EXTENSIONS feature. When we support CALL PER (variables)
  // we could consider stringifying this query as:
  //   CALL PER (...existing_vars...) {
  //     RETURN 1 as x, ..etc
  //     NEXT
  //     CALL PER (x) tvf()
  //   }
  if (implicit_input_scan->Is<ResolvedProjectScan>()) {
    // This projection may be casting to the correct types, reordering columns,
    // etc.
    const auto* input_project_scan =
        implicit_input_scan->GetAs<ResolvedProjectScan>();
    GOOGLESQL_RETURN_IF_ERROR(
        CheckScanIsGraphRefOrSingleRowScan(input_project_scan->input_scan()));

    std::vector<std::string> dummy_columns;
    GOOGLESQL_RETURN_IF_ERROR(CollapseResolvedGqlReturnScans(
        input_project_scan,
        /*output_column_to_alias=*/std::nullopt, output_sql, dummy_columns,
        /*generate_with=*/true));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(implicit_input_scan));
  }

  // Assign column aliases according to the TVF's input.
  const TVFInputArgumentType& signature_arg =
      node->signature()->argument(first_relation_arg_index);
  GOOGLESQL_RET_CHECK(signature_arg.is_relation());
  const TVFRelation& schema = signature_arg.relation();
  GOOGLESQL_RET_CHECK_EQ(schema.num_columns(), implicit_input_scan->column_list_size());
  GOOGLESQL_RET_CHECK_EQ(schema.num_columns(),
               implicit_input_table->argument_column_list_size());

  for (int i = 0; i < schema.num_columns(); ++i) {
    if (i == 0) {
      absl::StrAppend(&output_sql, " WITH ");
    } else {
      absl::StrAppend(&output_sql, ", ");
    }
    GOOGLESQL_RET_CHECK_EQ(implicit_input_scan->column_list(i).column_id(),
                 implicit_input_table->argument_column_list(i).column_id());
    absl::StrAppend(&output_sql,
                    GetColumnAlias(implicit_input_scan->column_list(i)), " AS ",
                    ToIdentifierLiteral(schema.column(i).name));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::string tvf_sql,
                   ProcessResolvedTVFScan(node, TVFBuildMode::kGqlImplicit));
  absl::StrAppend(&output_sql, " CALL PER() ", std::move(tvf_sql));
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlLetOp(
    const ResolvedAnalyticScan* node, std::string& output_sql) {
  GOOGLESQL_RETURN_IF_ERROR(ProcessAnalyticInputsWithNoVerticalAggregation(node));

  std::vector<std::string> let_definition_list;

  for (const auto& group : node->function_group_list()) {
    for (const auto& column : group->analytic_function_list()) {
      const std::string column_alias = GetColumnAlias(column->column());
      const std::string definition = absl::StrCat(
          column_alias, " = ",
          googlesql_base::FindOrDie(pending_columns(), column->column().column_id()));
      let_definition_list.push_back(std::move(definition));
    }
  }

  absl::StrAppend(&output_sql, " LET ",
                  absl::StrJoin(let_definition_list, ", "));
  mutable_pending_columns().clear();
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlLetOp(
    const ResolvedProjectScan* node, std::string& output_sql) {
  GOOGLESQL_RETURN_IF_ERROR(
      ProcessAnalyticInputsWithNoVerticalAggregation(node->input_scan()));
  std::vector<std::string> let_definition_list;

  for (const std::unique_ptr<const ResolvedComputedColumn>& expr :
       node->expr_list()) {
    const std::string column_alias = GetColumnAlias(expr->column());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(expr->expr()));
    const std::string definition =
        absl::StrCat(column_alias, " = ", result->GetSQL());
    let_definition_list.push_back(std::move(definition));
  }

  absl::StrAppend(&output_sql, " LET ",
                  absl::StrJoin(let_definition_list, ", "));
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlWithOp(const ResolvedScan* node,
                                                  std::string& output_sql) {
  std::vector<const ResolvedScan*> flattened_scans;
  const ResolvedLimitOffsetScan* limit_offset_scan;
  GOOGLESQL_RETURN_IF_ERROR(
      LinearizeWithOpResolvedScans(node, flattened_scans, limit_offset_scan));
  GOOGLESQL_RET_CHECK(limit_offset_scan == nullptr)
      << "LimitOffset scan is not supported in WITH";

  for (const ResolvedScan* scan : flattened_scans) {
    if (scan->Is<ResolvedAggregateScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlWithOp(
          scan->GetAs<ResolvedAggregateScan>(), output_sql));
    } else if (scan->Is<ResolvedProjectScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlWithOp(
          scan->GetAs<ResolvedProjectScan>(), output_sql));
    } else if (scan->Is<ResolvedAnalyticScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlWithOp(
          scan->GetAs<ResolvedAnalyticScan>(), output_sql));
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported scan type in WITH" << scan->node_kind();
    }
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlForOp(const ResolvedArrayScan* node,
                                                 std::string& output_sql) {
  GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));

  GOOGLESQL_RET_CHECK_EQ(node->array_expr_list().size(), 1);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->array_expr_list()[0].get()));
  GOOGLESQL_RET_CHECK_EQ(node->element_column_list().size(), 1);
  const std::string column_alias =
      GetColumnAlias(node->element_column_list()[0]);

  absl::StrAppend(&output_sql, " FOR ", column_alias, " IN ", result->GetSQL());

  if (node->array_offset_column() == nullptr) {
    // If the next statement is a WITH statement, there is an ambiguity that
    // gets resolved in favor of FOR .. IN .. WITH OFFSET. To avoid that, we
    // pick some alias.
    absl::StrAppend(&output_sql, " WITH OFFSET AS ", GenerateUniqueAliasName());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->array_offset_column()));
    absl::StrAppend(&output_sql, " WITH OFFSET AS ", result->GetSQL());
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlProjectScanFromOrderBy(
    const ResolvedProjectScan* scan) {
  GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(scan->input_scan()));
  for (const auto& computed_column : scan->expr_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> column_fragment,
                     ProcessNode(computed_column->expr()));
    googlesql_base::InsertOrDie(&mutable_pending_columns(),
                     computed_column->column().column_id(),
                     column_fragment->GetSQL());
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> SQLBuilder::ProcessResolvedGqlOrderByItem(
    const ResolvedOrderByItem* item, const std::string* return_alias) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(item->column_ref()));
  std::string fragment =
      return_alias != nullptr ? *return_alias : result->GetSQL();

  if (item->collation_name() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> collation,
                     ProcessNode(item->collation_name()));
    absl::StrAppend(&fragment, " COLLATE ", collation->GetSQL());
  }
  // Access field. Collation doesn't have its own GQL clause.
  item->collation();
  absl::StrAppend(&fragment, item->is_descending() ? " DESC" : "");
  if (item->null_order() != ResolvedOrderByItemEnums::ORDER_UNSPECIFIED) {
    absl::StrAppend(&fragment,
                    item->null_order() == ResolvedOrderByItemEnums::NULLS_FIRST
                        ? " NULLS FIRST"
                        : " NULLS LAST");
  }
  return fragment;
}

absl::Status SQLBuilder::ProcessResolvedGqlOrderByOp(
    const ResolvedOrderByScan* node, std::string& output_sql) {
  node->is_ordered();
  node->column_list();

  if (node->input_scan()->Is<ResolvedProjectScan>()) {
    auto* project_scan = node->input_scan()->GetAs<ResolvedProjectScan>();
    GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlProjectScanFromOrderBy(project_scan));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));
  }

  std::vector<std::string> fragments;
  for (const auto& item : node->order_by_item_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::string fragment,
        ProcessResolvedGqlOrderByItem(item.get(), /*return_alias=*/nullptr));
    fragments.push_back(fragment);
  }

  absl::StrAppend(&output_sql, " ORDER BY ", absl::StrJoin(fragments, ", "));

  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessGqlOffset(const ResolvedLimitOffsetScan* node,
                                          std::string& output_sql) {
  if (node->offset() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<QueryFragment> offset_fragment,
        ProcessNode(node->offset()->node_kind() != RESOLVED_CAST
                        ? node->offset()
                        : node->offset()->GetAs<ResolvedCast>()->expr()));
    absl::StrAppend(&output_sql, " OFFSET ", offset_fragment->GetSQL());
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessGqlLimit(const ResolvedLimitOffsetScan* node,
                                         std::string& output_sql) {
  bool limit_all_enabled =
      options_.language_options.LanguageFeatureEnabled(FEATURE_LIMIT_ALL);
  if (node->limit() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<QueryFragment> limit_fragment,
        // If limit is a ResolvedCast, it means that the original limit in the
        // query is a literal or parameter with a type other than int64 and
        // hence, analyzer has added a cast on top of it. We should skip this
        // cast here to avoid returning CAST(CAST ...)).
        ProcessNode(node->limit()->node_kind() != RESOLVED_CAST
                        ? node->limit()
                        : node->limit()->GetAs<ResolvedCast>()->expr()));
    // We resolve OFFSET / LIMIT clauses to LimitOffset scans.
    // In GQL, "LIMIT" clauses are optional.
    // When FEATURE_LIMIT_ALL is disabled, a limit must be present in the scan,
    // so we use kint64max/2 (4611686018427387903). Here, we
    // choose to remove "LIMIT 4611686018427387903" since it was likely not
    // present in the original query.
    // Note that this is a compromise: The original query could have been:
    //   "OFFSET 100 LIMIT 4611686018427387903 LIMIT 10"
    // which would be resolved to two LimitOffset scans, while by removing
    // "LIMIT 4611686018427387903", the query's AST unparses to "OFFSET 100
    // LIMIT 10" which will resolve to one scan (with the same semantics).
    // When FEATURE_LIMIT_ALL is enabled, a limit does not have to be present
    // in the scan. When a limit is present, it should always be present in
    // the original query and should not be removed.
    if (limit_fragment->GetSQL() != khalf_of_int64max_str ||
        limit_all_enabled) {
      absl::StrAppend(&output_sql, " LIMIT ", limit_fragment->GetSQL());
    }
  }

  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlPageOp(
    const ResolvedLimitOffsetScan* node, std::string& output_sql) {
  GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));

  GOOGLESQL_RETURN_IF_ERROR(ProcessGqlOffset(node, output_sql));
  GOOGLESQL_RETURN_IF_ERROR(ProcessGqlLimit(node, output_sql));

  return absl::OkStatus();
}

absl::Status SQLBuilder::PopulateItemsForGqlWithOp(
    absl::Span<const ResolvedColumn> columns,
    StringToStringHashMap& column_alias_to_sql,
    std::vector<std::string>& with_item_list) {
  for (const ResolvedColumn& col : columns) {
    const std::string column_alias = GetColumnAlias(col);
    if (column_alias_to_sql.contains(column_alias)) {
      with_item_list.push_back(column_alias_to_sql[column_alias]);
    } else {
      with_item_list.push_back(std::move(column_alias));
    }
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlWithOp(
    const ResolvedProjectScan* node, std::string& output_sql) {
  // Mark as visited
  node->input_scan()->MarkFieldsAccessed();
  StringToStringHashMap column_alias_to_sql;
  for (const auto& computed_col : node->expr_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    const std::string column_alias = GetColumnAlias(computed_col->column());
    const std::string projected_expr =
        absl::StrCat(result->GetSQL(), " AS ", column_alias);
    googlesql_base::InsertOrDie(&column_alias_to_sql, column_alias, projected_expr);
  }
  std::vector<std::string> return_item_list;
  GOOGLESQL_RETURN_IF_ERROR(PopulateItemsForGqlWithOp(
      node->column_list(), column_alias_to_sql, return_item_list));
  absl::StrAppend(&output_sql, " WITH ", absl::StrJoin(return_item_list, ", "));
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlWithOp(
    const ResolvedAnalyticScan* node, std::string& output_sql) {
  if (!node->input_scan()->Is<ResolvedProjectScan>() &&
      !node->input_scan()->Is<ResolvedAggregateScan>()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));
  }
  StringToStringHashMap column_alias_to_sql;
  for (const auto& group : node->function_group_list()) {
    GOOGLESQL_RETURN_IF_ERROR(group->Accept(this));
    for (const auto& function : group->analytic_function_list()) {
      const std::string column_alias = GetColumnAlias(function->column());
      const std::string projected_expr = absl::StrCat(
          googlesql_base::FindOrDie(pending_columns(), function->column().column_id()),
          " AS ", column_alias);
      googlesql_base::InsertOrDie(&column_alias_to_sql, column_alias, projected_expr);
      mutable_pending_columns().erase(function->column().column_id());
    }
  }

  std::vector<std::string> return_item_list;
  GOOGLESQL_RETURN_IF_ERROR(PopulateItemsForGqlWithOp(
      node->column_list(), column_alias_to_sql, return_item_list));
  absl::StrAppend(&output_sql, " WITH ", absl::StrJoin(return_item_list, ", "));
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlWithOp(
    const ResolvedAggregateScan* node, std::string& output_sql) {
  if (!node->input_scan()->Is<ResolvedProjectScan>()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));
  }
  std::vector<std::string> return_item_list;
  std::vector<std::string> group_by_list;
  StringToStringHashMap column_alias_to_sql;

  for (const auto& computed_col : node->aggregate_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    const std::string column_alias = GetColumnAlias(computed_col->column());
    const std::string aggregate_expr =
        absl::StrCat(result->GetSQL(), " AS ", column_alias);
    googlesql_base::InsertOrDie(&column_alias_to_sql, column_alias, aggregate_expr);
  }
  for (const auto& computed_col : node->group_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    const std::string column_alias = GetColumnAlias(computed_col->column());
    const std::string group_by_item = result->GetSQL();
    googlesql_base::InsertOrDie(&column_alias_to_sql, column_alias,
                     absl::StrCat(result->GetSQL(), " AS ", column_alias));
    group_by_list.push_back(std::move(column_alias));
  }
  GOOGLESQL_RETURN_IF_ERROR(PopulateItemsForGqlWithOp(
      node->column_list(), column_alias_to_sql, return_item_list));
  absl::StrAppend(&output_sql, " WITH ", absl::StrJoin(return_item_list, ", "));
  if (!group_by_list.empty()) {
    absl::StrAppend(&output_sql, " GROUP BY ",
                    absl::StrJoin(group_by_list, ", "));
  }
  return absl::OkStatus();
}

namespace {

// Returns the `input_scan` of the given `scan`, if the `scan`'s type is one of
// the provided derived types, and nullptr otherwise.
template <class Scan>
const Scan* GetInputScan(const Scan* scan) {
  return nullptr;
}
template <class Derived, class... DerivedTypes>
const ResolvedScan* GetInputScan(const ResolvedScan* scan) {
  const Derived* derived = dynamic_cast<const Derived*>(scan);
  return derived ? derived->input_scan() : GetInputScan<DerivedTypes...>(scan);
}

}  // namespace

absl::Status SQLBuilder::LinearizeWithOpResolvedScans(
    const ResolvedScan* scan, std::vector<const ResolvedScan*>& return_scans,
    const ResolvedLimitOffsetScan*& limit_offset_scan) const {
  // The last scan should represent the GQL RETURN operator, which consists
  // of nested AggregateScans, ProjectScans, OrderByScans and LimitOffsetScans.
  // We can unparse these into valid RETURN .. NEXT .. statements to address the
  // nesting since GQL RETURN doesn't support SELECT .. FROM (subquery).
  while (auto* input_scan =
             GetInputScan<ResolvedAggregateScan, ResolvedProjectScan,
                          ResolvedAnalyticScan, ResolvedOrderByScan,
                          ResolvedLimitOffsetScan>(scan)) {
    return_scans.push_back(scan);
    scan = input_scan;
  }
  GOOGLESQL_RET_CHECK(scan->Is<ResolvedGraphRefScan>() ||
            scan->Is<ResolvedSingleRowScan>());
  std::reverse(return_scans.begin(), return_scans.end());
  // If the scan sequence has a LimitOffsetScan, it will always be last after
  // linearization. We avoid creating a "RETURN .. NEXT" statement for such
  // scans since we know they don't produce any new columns.
  limit_offset_scan = nullptr;
  if (!return_scans.empty() &&
      return_scans.back()->Is<ResolvedLimitOffsetScan>()) {
    limit_offset_scan = return_scans.back()->GetAs<ResolvedLimitOffsetScan>();
    return_scans.pop_back();
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessGqlSubquery(const ResolvedSubqueryExpr* node,
                                            std::string& output_sql) {
  const bool is_gql_subquery = googlesql_base::ContainsKeyValuePair(
      options_.target_syntax_map, node, SQLBuildTargetSyntax::kGqlSubquery);
  const bool is_exists_graph_pattern = googlesql_base::ContainsKeyValuePair(
      options_.target_syntax_map, node,
      SQLBuildTargetSyntax::kGqlExistsSubqueryGraphPattern);
  const bool is_exists_linear_ops = googlesql_base::ContainsKeyValuePair(
      options_.target_syntax_map, node,
      SQLBuildTargetSyntax::kGqlExistsSubqueryLinearOps);
  if (!is_gql_subquery && !is_exists_graph_pattern && !is_exists_linear_ops) {
    // Not a GQL subquery, no need to proceed.
    return absl::OkStatus();
  }

  std::function<bool(const ResolvedScan*)> has_graph_linear_scan =
      [](const ResolvedScan* scan) {
        return scan->Is<ResolvedGraphTableScan>() &&
               scan->GetAs<ResolvedGraphTableScan>()
                   ->input_scan()
                   ->Is<ResolvedGraphLinearScan>();
      };
  if (node->subquery_type() == ResolvedSubqueryExpr::SCALAR &&
      node->subquery()->Is<ResolvedLimitOffsetScan>() &&
      has_graph_linear_scan(
          node->subquery()->GetAs<ResolvedLimitOffsetScan>()->input_scan())) {
    // This is a `VALUE { GQL subquery }`.
    std::string graph_subquery;
    GOOGLESQL_RETURN_IF_ERROR(
        ProcessResolvedGraphSubquery(node->subquery()
                                         ->GetAs<ResolvedLimitOffsetScan>()
                                         ->input_scan()
                                         ->GetAs<ResolvedGraphTableScan>(),
                                     graph_subquery));
    absl::StrAppend(&output_sql, "VALUE");
    const ResolvedLimitOffsetScan* limit_offset =
        node->subquery()->GetAs<ResolvedLimitOffsetScan>();
    if (limit_offset->limit() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(limit_offset->limit()));
      absl::StrAppend(&graph_subquery, " LIMIT ", result->GetSQL());
    }
    if (limit_offset->offset() != nullptr) {
      // Ignore OFFSET because GQL doesn't support LIMIT followed by OFFSET.
      limit_offset->offset()->MarkFieldsAccessed();
    }
    absl::StrAppend(&output_sql, "{", graph_subquery, "}");
    return absl::OkStatus();
  }
  if (!has_graph_linear_scan(node->subquery())) {
    // Tree structure is not a GQL subquery, return.
    return absl::OkStatus();
  }

  // Attempt to construct an EXISTS partial form subquery, which
  // requires only the first linear scan.
  auto& first_op = node->subquery()
                       ->GetAs<ResolvedGraphTableScan>()
                       ->input_scan()
                       ->GetAs<ResolvedGraphLinearScan>()
                       ->scan_list()
                       .front();
  if (!first_op->Is<ResolvedGraphLinearScan>()) {
    // This is a composite subquery, and we can't generate a partial form.
    // Return empty string to let caller to render a full subquery instead.
    GOOGLESQL_RET_CHECK(first_op->Is<ResolvedSetOperationScan>());
    return absl::OkStatus();
  }
  const auto* linear_scan = first_op->GetAs<ResolvedGraphLinearScan>();
  std::string exists_partial_form_sql;
  if (is_exists_graph_pattern) {
    for (const auto& scan : linear_scan->scan_list()) {
      if (scan->Is<ResolvedGraphScan>()) {
        for (const auto& graph_path_scan :
             scan->GetAs<ResolvedGraphScan>()->input_scan_list()) {
          if (!exists_partial_form_sql.empty()) {
            // append separating commas between graph_path(s)
            absl::StrAppend(&exists_partial_form_sql, ", ");
          }
          std::string graph_path_sql;
          GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGraphPathScan(graph_path_scan.get(),
                                                       graph_path_sql));
          absl::StrAppend(&exists_partial_form_sql, "(", graph_path_sql, ")");
        }
        if (!exists_partial_form_sql.empty()) {
          break;
        }
      }
    }
  } else if (is_exists_linear_ops) {
    if (linear_scan->scan_list().size() > 1) {
      // In order to render this as an EXISTS linear_ops subquery, it
      // needs to contain at least one op other than the last RETURN.
      std::vector<std::string> unused;
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlLinearScanIgnoringLastReturn(
          linear_scan, exists_partial_form_sql, unused));
    }
  }
  if (!exists_partial_form_sql.empty()) {
    node->subquery()->MarkFieldsAccessed();
    std::string graph_reference;
    AppendGraphReference(node->subquery()->GetAs<ResolvedGraphTableScan>(),
                         graph_reference);
    absl::StrAppend(&output_sql, "{", graph_reference, exists_partial_form_sql,
                    "}");
    return absl::OkStatus();
  }

  // Not an `EXISTS partial form`, fallback to the regular GQL subquery.
  std::string graph_subquery;
  GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGraphSubquery(
      node->subquery()->GetAs<ResolvedGraphTableScan>(), graph_subquery));
  absl::StrAppend(&output_sql, "{", graph_subquery, "}");
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGraphPathScan(
    const ResolvedGraphPathScan* node, std::string& output_sql) {
  if (node->path() != nullptr) {
    // Mark accessed.
    node->path()->column();
  }
  GOOGLESQL_RETURN_IF_ERROR(AppendPathMode(node->path_mode(), output_sql));
  if (!node->hint_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &output_sql));
    absl::StrAppend(&output_sql, " ");
  }
  for (const auto& input_scan : node->input_scan_list()) {
    const ResolvedGraphPathScanBase* child = input_scan.get();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result, ProcessNode(child));
    if (child->Is<ResolvedGraphPathScan>()) {
      // For a child subpath, print parentheses.
      absl::StrAppend(&output_sql, "(", result->GetSQL(), ")");
      // If this path contains subpaths that contain quantifiers, print it here.
      if (child->GetAs<ResolvedGraphPathScan>()->quantifier() != nullptr) {
        std::string quantifier_sql;
        GOOGLESQL_RETURN_IF_ERROR(ProcessGraphPathPatternQuantifier(
            child->GetAs<ResolvedGraphPathScan>()->quantifier(),
            quantifier_sql));
        absl::StrAppend(&output_sql, std::move(quantifier_sql));
      }
    } else {
      absl::StrAppend(&output_sql, result->GetSQL());
    }
  }
  for (const std::unique_ptr<const ResolvedGraphMakeArrayVariable>&
           group_variable : node->group_variable_list()) {
    // The alias of the group variable should match that of the element
    // There should never be a conflict because they're accessed at different
    // scopes (element is referenced inside the quantified path, array on the
    // outside).
    googlesql_base::InsertOrDie(&mutable_computed_column_alias(),
                     group_variable->array().column_id(),
                     GetColumnAlias(group_variable->element()));
    SetPathForColumn(group_variable->array(),
                     GetColumnPath(group_variable->element()));
  }

  if (node->filter_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->filter_expr()));
    absl::StrAppend(&output_sql, " WHERE ", result->GetSQL());
  }
  return absl::OkStatus();
}

// TODO: consider merge this with ProcessResolvedGqlLinearOp.
absl::Status SQLBuilder::ProcessResolvedGqlLinearScanIgnoringLastReturn(
    const ResolvedGraphLinearScan* node, std::string& sql,
    std::vector<std::string>& columns) {
  for (int i = 0; i < node->scan_list_size() - 1; ++i) {
    const ResolvedScan* scan = node->scan_list(i);

    if (scan->Is<ResolvedGraphScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlMatchOp(scan->GetAs<ResolvedGraphScan>(), sql));
    } else if (scan->Is<ResolvedFilterScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlFilterOp(scan->GetAs<ResolvedFilterScan>(), sql));
    } else if (scan->Is<ResolvedArrayScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlForOp(scan->GetAs<ResolvedArrayScan>(), sql));
    } else if (scan->Is<ResolvedProjectScan>()) {
      if (googlesql_base::ContainsKeyValuePair(options_.target_syntax_map, scan,
                                    SQLBuildTargetSyntax::kGqlWith)) {
        GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlWithOp(scan, sql));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(
            ProcessResolvedGqlLetOp(scan->GetAs<ResolvedProjectScan>(), sql));
      }
    } else if (scan->Is<ResolvedAnalyticScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlLetOp(scan->GetAs<ResolvedAnalyticScan>(), sql));
    } else if (scan->Is<ResolvedAggregateScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlWithOp(scan, sql));
    } else if (scan->Is<ResolvedOrderByScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlOrderByOp(scan->GetAs<ResolvedOrderByScan>(), sql));
    } else if (scan->Is<ResolvedSampleScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlSampleOp(scan->GetAs<ResolvedSampleScan>(), sql));
    } else if (scan->Is<ResolvedGraphCallScan>()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(scan));
      absl::StrAppend(&sql, result->GetSQL());
    } else if (scan->Is<ResolvedTVFScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ProcessResolvedGqlNamedCallOp(scan->GetAs<ResolvedTVFScan>(), sql));
    } else {
      GOOGLESQL_RET_CHECK(scan->Is<ResolvedLimitOffsetScan>());
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlPageOp(
          scan->GetAs<ResolvedLimitOffsetScan>(), sql));
    }
  }
  return absl::OkStatus();
}

static bool IsGqlCompositeScan(const ResolvedScan* scan) {
  return scan->Is<ResolvedGraphLinearScan>() ||
         scan->Is<ResolvedSetOperationScan>();
}

absl::Status SQLBuilder::ProcessGqlCompositeScans(
    const ResolvedGraphLinearScan* node, std::string& output_sql,
    std::vector<std::string>& columns) {
  GOOGLESQL_RET_CHECK(!node->scan_list().empty());

  // Iterate over composite scans, each of which is a linear scan or a set
  // operation scan whose children are linear scans.
  std::vector<std::string> composite_op_strs;
  composite_op_strs.reserve(node->scan_list().size());
  for (int i = 0; i < node->scan_list().size(); ++i) {
    auto child_scan = node->scan_list(i);
    GOOGLESQL_RET_CHECK(IsGqlCompositeScan(child_scan));

    std::string op_str;
    if (child_scan->Is<ResolvedGraphLinearScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlLinearOp(
          child_scan->GetAs<ResolvedGraphLinearScan>(),
          /*output_column_to_alias=*/std::nullopt, op_str, columns));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ProcessGqlSetOp(
          child_scan->GetAs<ResolvedSetOperationScan>(), op_str, columns));
    }

    composite_op_strs.push_back(std::move(op_str));
  }

  absl::StrAppend(&output_sql, absl::StrJoin(composite_op_strs, " NEXT "));
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessGqlSetOp(const ResolvedSetOperationScan* node,
                                         std::string& output_sql,
                                         std::vector<std::string>& columns) {
  // Mark fields as accessed. The validator should already check the shape.
  GOOGLESQL_RET_CHECK_EQ(node->column_match_mode(),
               ResolvedSetOperationScan::CORRESPONDING);

  // Populate the output column aliases for the set operation first.
  std::vector<std::string> aliases(node->column_list_size());
  for (int i = 0; i < node->column_list_size(); ++i) {
    aliases[i] = GetColumnAlias(node->column_list(i));
  }

  // When building set operation items (single linear query), each linear query
  // is required to produce the same set of column aliases in the RETURN clause.
  for (int query_idx = 0; query_idx < node->input_item_list_size();
       ++query_idx) {
    const ResolvedSetOperationItem* item = node->input_item_list(query_idx);

    // Populate a mapping of output column to the final aliases for GQL linear
    // scan RETURN clause.
    absl::btree_map<ResolvedColumn, std::string> output_column_to_alias;
    for (int i = 0; i < item->output_column_list_size(); ++i) {
      output_column_to_alias[item->output_column_list(i)] = aliases[i];
    }

    std::string linear_scan_sql;
    GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGqlLinearOp(
        item->scan()->GetAs<ResolvedGraphLinearScan>(), output_column_to_alias,
        linear_scan_sql, columns));

    // Generate GQL text.
    if (query_idx > 0) {
      absl::StrAppend(
          &output_sql, "\n",
          SetOperationResolverBase::GetSQLForOperation(node->op_type()), "\n");
    }
    absl::StrAppend(&output_sql, linear_scan_sql);
  }

  // Update column alias for other parts of the query.
  for (int i = 0; i < node->column_list_size(); ++i) {
    SetPathForColumn(node->column_list(i), aliases[i]);
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGqlLinearOp(
    const ResolvedGraphLinearScan* node,
    std::optional<absl::btree_map<ResolvedColumn, std::string>>
        output_column_to_alias,
    std::string& output_sql, std::vector<std::string>& columns) {
  std::string sql;
  GOOGLESQL_RET_CHECK(!node->scan_list().empty());

  // Process children primitive gql operations.
  std::vector<std::string> unused;
  GOOGLESQL_RETURN_IF_ERROR(
      ProcessResolvedGqlLinearScanIgnoringLastReturn(node, sql, unused));

  GOOGLESQL_RETURN_IF_ERROR(CollapseResolvedGqlReturnScans(
      node->scan_list().back().get(), output_column_to_alias, sql, columns));

  absl::StrAppend(&output_sql, sql);
  return absl::OkStatus();
}

GqlReturnOpSQLBuilder::GqlReturnOpSQLBuilder(
    const ResolvedColumnList& output_column_list,
    std::optional<absl::btree_map<ResolvedColumn, std::string>>
        output_column_to_alias,
    int* max_seen_alias_id, const SQLBuilderOptions& options,
    const CopyableState& state)
    : SQLBuilder(max_seen_alias_id, options, state),
      output_column_list_(output_column_list) {
  if (output_column_to_alias.has_value()) {
    output_column_to_alias_ = std::move(*output_column_to_alias);
  } else {
    // If the mapping of output column to aliases is not provided, generate
    // new aliases for all output columns.
    for (int i = 0; i < output_column_list_.size(); ++i) {
      const ResolvedColumn& col = output_column_list_[i];
      output_column_to_alias_[col] = GetColumnAlias(col);
    }
  }
}

std::vector<std::string> GqlReturnOpSQLBuilder::GetOutputColumnAliases() {
  std::vector<std::string> aliases;
  for (auto iter = output_column_to_alias_.begin();
       iter != output_column_to_alias_.end(); ++iter) {
    aliases.push_back(iter->second);
  }
  return aliases;
}

std::string GqlReturnOpSQLBuilder::GetReturnColumnExpr(
    const ResolvedColumn& column) {
  if (column_id_to_sql_.contains(column.column_id())) {
    return column_id_to_sql_[column.column_id()];
  }
  if (googlesql_base::ContainsKey(pending_columns(), column.column_id())) {
    return googlesql_base::FindOrDie(pending_columns(), column.column_id());
  }
  if (googlesql_base::ContainsKey(correlated_pending_columns(), column.column_id())) {
    return googlesql_base::FindOrDie(correlated_pending_columns(), column.column_id());
  }
  return GetColumnPath(column);
}

std::string GqlReturnOpSQLBuilder::gql() {
  std::string sql;
  if (is_distinct_) {
    absl::StrAppend(&sql, "DISTINCT ");
  }
  for (int i = 0; i < output_column_list_.size(); ++i) {
    if (i > 0) {
      absl::StrAppend(&sql, ", ");
    }
    const ResolvedColumn& col = output_column_list_[i];
    absl::StrAppend(&sql, GetReturnColumnExpr(col), " AS ",
                    output_column_to_alias_[col]);
  }
  if (!group_by_list_.empty()) {
    absl::StrAppend(&sql, "\n GROUP BY ");
    for (int i = 0; i < group_by_list_.size(); ++i) {
      if (i > 0) {
        absl::StrAppend(&sql, ", ");
      }

      const ResolvedColumn& col = group_by_list_[i];
      std::string group_by_item = output_column_to_alias_.contains(col)
                                      ? output_column_to_alias_[col]
                                      : column_id_to_sql_[col.column_id()];

      absl::StrAppend(&sql, group_by_item);
    }
  }
  if (!order_by_column_to_sql_.empty()) {
    absl::StrAppend(&sql, "\n ORDER BY ");
    int i = 0;
    for (const auto& [order_by_col, order_by_sql] : order_by_column_to_sql_) {
      if (i++ > 0) {
        absl::StrAppend(&sql, ", ");
      }
      absl::StrAppend(&sql, order_by_sql);
    }
  }
  if (!offset_sql_.empty()) {
    absl::StrAppend(&sql, "\n", offset_sql_);
  }
  if (!limit_sql_.empty()) {
    absl::StrAppend(&sql, "\n", limit_sql_);
  }
  return sql;
}

absl::Status GqlReturnOpSQLBuilder::VisitResolvedGraphRefScan(
    const ResolvedGraphRefScan* node) {
  // Register the input columns as they may be referenced in RETURN's
  // subclauses such as ORDER BY. Note that AnalyticScan and ProjectScan only
  // add the new columns they're introducing.
  // See b/382069429.
  for (const auto& column : node->column_list()) {
    column_id_to_sql_[column.column_id()] = GetColumnPath(column);
  }
  return absl::OkStatus();
}

bool GqlReturnOpSQLBuilder::IsDistinctAggregateScan(
    const ResolvedAggregateScan* node) {
  absl::flat_hash_set<ResolvedColumn> output_column_set(
      node->column_list().begin(), node->column_list().end());
  absl::flat_hash_set<ResolvedColumn> group_by_column_set;
  std::transform(
      node->group_by_list().begin(), node->group_by_list().end(),
      std::inserter(group_by_column_set, group_by_column_set.begin()),
      [](const auto& computed_col) -> ResolvedColumn {
        return computed_col->column();
      });

  return node->aggregate_list_size() == 0 &&
         googlesql_base::HashSetEquality(output_column_set, group_by_column_set);
}

absl::Status GqlReturnOpSQLBuilder::VisitResolvedAggregateScan(
    const ResolvedAggregateScan* node) {
  if (is_subquery_context_) {
    return SQLBuilder::VisitResolvedAggregateScan(node);
  }

  GOOGLESQL_RETURN_IF_ERROR(ProcessInputScan(node->input_scan()));

  if (IsDistinctAggregateScan(node)) {
    GOOGLESQL_RET_CHECK(!is_distinct_);
    is_distinct_ = true;
  }
  for (const auto& computed_col : node->aggregate_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    int column_id = computed_col->column().column_id();
    GOOGLESQL_RET_CHECK(!column_id_to_sql_.contains(column_id));
    column_id_to_sql_[column_id] = result->GetSQL();
    googlesql_base::InsertOrDie(&mutable_pending_columns(), column_id, result->GetSQL());
  }

  // Nested AggregateScan that does not represent DISTINCT is not supported
  // in GQL RETURN clause yet.
  if (!is_distinct_) {
    GOOGLESQL_RET_CHECK(group_by_list_.empty());
  }
  for (const auto& computed_col : node->group_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    const std::string group_by_item = result->GetSQL();

    // Skip processing the GROUP BY list if the current scan only represents
    // DISTINCT.
    if (!is_distinct_) {
      group_by_list_.push_back(computed_col->column());
    }
    int column_id = computed_col->column().column_id();
    if (!column_id_to_sql_.contains(column_id)) {
      column_id_to_sql_[column_id] = group_by_item;
      googlesql_base::InsertOrDie(&mutable_pending_columns(), column_id, group_by_item);
    }
  }

  return absl::OkStatus();
}

// Unlike aggregations, window functions may appear under ReturnOp.
absl::Status GqlReturnOpSQLBuilder::VisitResolvedAnalyticScan(
    const ResolvedAnalyticScan* node) {
  if (is_subquery_context_) {
    return SQLBuilder::VisitResolvedAnalyticScan(node);
  }

  GOOGLESQL_RETURN_IF_ERROR(ProcessInputScan(node->input_scan()));
  for (const auto& fn_group : node->function_group_list()) {
    GOOGLESQL_RETURN_IF_ERROR(fn_group->Accept(this));

    for (const auto& window_function : fn_group->analytic_function_list()) {
      int column_id = window_function->column().column_id();
      GOOGLESQL_RET_CHECK(!column_id_to_sql_.contains(column_id));
      column_id_to_sql_[column_id] =
          googlesql_base::FindOrDie(pending_columns(), column_id);
    }
  }
  return absl::OkStatus();
}

absl::Status GqlReturnOpSQLBuilder::VisitResolvedProjectScan(
    const ResolvedProjectScan* node) {
  if (is_subquery_context_) {
    return SQLBuilder::VisitResolvedProjectScan(node);
  }

  GOOGLESQL_RETURN_IF_ERROR(ProcessInputScan(node->input_scan()));

  for (const auto& computed_col : node->expr_list()) {
    int column_id = computed_col->column().column_id();
    if (column_id_to_sql_.contains(column_id)) {
      continue;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_col->expr()));
    column_id_to_sql_[column_id] = result->GetSQL();
    googlesql_base::InsertOrDie(&mutable_pending_columns(), column_id, result->GetSQL());
  }
  return absl::OkStatus();
}

absl::Status GqlReturnOpSQLBuilder::VisitResolvedOrderByScan(
    const ResolvedOrderByScan* node) {
  if (is_subquery_context_) {
    GOOGLESQL_RETURN_IF_ERROR(SQLBuilder::VisitResolvedOrderByScan(node));
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(order_by_column_to_sql_.empty());
  GOOGLESQL_RETURN_IF_ERROR(ProcessInputScan(node->input_scan()));

  for (const auto& item : node->order_by_item_list()) {
    std::string* return_alias = nullptr;
    const ResolvedColumn& column = item->column_ref()->column();
    if (output_column_to_alias_.contains(column)) {
      return_alias = &output_column_to_alias_[column];
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::string fragment,
                     ProcessResolvedGqlOrderByItem(item.get(), return_alias));
    GOOGLESQL_RET_CHECK(column_id_to_sql_.contains(column.column_id()))
        << column.DebugString() << " not found in column_id_to_sql_";
    order_by_column_to_sql_[column] = fragment;
  }

  return absl::OkStatus();
}

absl::Status GqlReturnOpSQLBuilder::VisitResolvedLimitOffsetScan(
    const ResolvedLimitOffsetScan* node) {
  if (is_subquery_context_) {
    GOOGLESQL_RETURN_IF_ERROR(SQLBuilder::VisitResolvedLimitOffsetScan(node));
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(offset_sql_.empty());
  GOOGLESQL_RET_CHECK(limit_sql_.empty());

  GOOGLESQL_RETURN_IF_ERROR(ProcessInputScan(node->input_scan()));
  GOOGLESQL_RETURN_IF_ERROR(ProcessGqlOffset(node, offset_sql_));
  GOOGLESQL_RETURN_IF_ERROR(ProcessGqlLimit(node, limit_sql_));

  return absl::OkStatus();
}

absl::Status GqlReturnOpSQLBuilder::VisitResolvedSubqueryExpr(
    const ResolvedSubqueryExpr* node) {
  absl::Cleanup subquery_context_cleanup = [&, previous_subquery_context =
                                                   is_subquery_context_] {
    is_subquery_context_ = previous_subquery_context;
  };
  is_subquery_context_ = true;
  GOOGLESQL_RETURN_IF_ERROR(SQLBuilder::VisitResolvedSubqueryExpr(node));
  return absl::OkStatus();
}

absl::Status GqlReturnOpSQLBuilder::ProcessInputScan(const ResolvedScan* node) {
  if (node == nullptr || node->Is<ResolvedSingleRowScan>()) {
    return absl::OkStatus();
  }
  return node->Accept(this);
}

std::unique_ptr<GqlReturnOpSQLBuilder> SQLBuilder::CreateGqlReturnOpSQLBuilder(
    const ResolvedColumnList& output_column_list,
    std::optional<absl::btree_map<ResolvedColumn, std::string>>
        output_column_to_alias,
    int* max_seen_alias_id, const SQLBuilderOptions& options,
    const CopyableState& state) {
  return std::make_unique<GqlReturnOpSQLBuilder>(
      output_column_list, output_column_to_alias, max_seen_alias_id, options,
      state);
}

absl::Status SQLBuilder::CollapseResolvedGqlReturnScans(
    const ResolvedScan* ret_scan,
    std::optional<absl::btree_map<ResolvedColumn, std::string>>
        output_column_to_alias,
    std::string& output_sql, std::vector<std::string>& columns,
    bool generate_with) {
  std::unique_ptr<GqlReturnOpSQLBuilder> return_builder =
      CreateGqlReturnOpSQLBuilder(
          ret_scan->column_list(), output_column_to_alias,
          mutable_max_seen_alias_id(), options_, state_);
  GOOGLESQL_RETURN_IF_ERROR(ret_scan->Accept(return_builder.get()));
  absl::StrAppend(&output_sql, generate_with ? " WITH " : " RETURN ",
                  return_builder->gql());

  // Update internal states back to the SQLBuilder.
  columns = return_builder->GetOutputColumnAliases();
  mutable_column_paths() = std::move(return_builder->mutable_column_paths());
  mutable_computed_column_alias() =
      std::move(return_builder->mutable_computed_column_alias());
  for (int i = 0; i < columns.size(); ++i) {
    SetPathForColumn(ret_scan->column_list(i), columns[i]);
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphTableScan(
    const ResolvedGraphTableScan* node) {
  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  std::string from;

  absl::StrAppend(&from, " GRAPH_TABLE ( ");
  if (node->property_graph()->NamePath().empty()) {
    absl::StrAppend(&from, ToIdentifierLiteral(node->property_graph()->Name()));
  } else {
    std::string graph_name =
        IdentifierPathToString(node->property_graph()->NamePath());
    absl::StrAppend(&from, graph_name);
  }

  std::string table_alias =
      MakeNonconflictingAlias(node->property_graph()->Name());
  SQLAliasPairList select_list;

  if (node->input_scan()->Is<ResolvedGraphLinearScan>()) {
    GOOGLESQL_RET_CHECK(node->shape_expr_list().empty());
    std::string gql;
    std::vector<std::string> columns;
    GOOGLESQL_RETURN_IF_ERROR(ProcessGqlCompositeScans(
        node->input_scan()->GetAs<ResolvedGraphLinearScan>(), gql, columns));
    for (const std::string& column_alias : columns) {
      select_list.emplace_back(absl::StrCat(table_alias, ".", column_alias),
                               column_alias);
    }
    absl::StrAppend(&from, " ", gql);
  } else {
    GOOGLESQL_RET_CHECK(node->input_scan()->Is<ResolvedGraphScan>());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> element_fragment,
                     ProcessNode(node->input_scan()));
    absl::StrAppend(&from, element_fragment->GetSQL());
    if (!node->shape_expr_list().empty()) {
      GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGraphShape(
          node->column_list(), node->shape_expr_list(), table_alias,
          &select_list, &from));
    }
  }

  absl::StrAppend(&from, " ) AS ", table_alias);

  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause(from));
  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(select_list, ""));

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::ProcessResolvedGraphShape(
    absl::Span<const ResolvedColumn> column_list,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        shape_expr_list,
    absl::string_view table_alias, SQLAliasPairList* select_list,
    std::string* sql) {
  std::vector<std::string> columns;
  for (size_t i = 0; i < shape_expr_list.size(); i++) {
    // For cases like "SELECT 1 FROM GRAPH_TABLE( MATCH () COLUMNS(1 AS num))",
    // unused columns could be pruned and we don't have a corresponding entry in
    // column_list. Create a unique alias in that case.
    const std::string column_alias = column_list.size() > i
                                         ? GetColumnAlias(column_list[i])
                                         : GenerateUniqueAliasName();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(shape_expr_list[i]->expr()));

    columns.push_back(absl::StrCat(result->GetSQL(), " AS ", column_alias));
    select_list->emplace_back(absl::StrCat(table_alias, ".", column_alias),
                              column_alias);
  }
  absl::StrAppend(sql, " COLUMNS ( ", absl::StrJoin(columns, ", "), " )");
  return absl::OkStatus();
}

absl::Status SQLBuilder::MakeGraphLabelNaryExpressionString(
    absl::string_view op, const ResolvedGraphLabelNaryExpr* node,
    std::string& output) {
  std::vector<std::string> sql_list;
  const std::vector<std::unique_ptr<const ResolvedGraphLabelExpr>>& op_list =
      node->operand_list();
  sql_list.reserve(op_list.size());
  for (auto const& operand : op_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> operand_fragment,
                     ProcessNode(operand.get()));
    sql_list.push_back(operand_fragment->GetSQL());
  }
  absl::StrAppend(&output, "(", absl::StrJoin(sql_list, op), ")");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphElementScanInternal(
    const ResolvedGraphElementScan* node, absl::string_view prefix,
    absl::string_view suffix) {
  std::string hint_sql;
  if (!node->hint_list().empty()) {
    absl::StrAppend(&hint_sql, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &hint_sql));
  }

  GOOGLESQL_RET_CHECK_EQ(node->column_list_size(), 1);
  const auto& column = node->column_list()[0];
  std::string alias = GetColumnAlias(column);
  SetPathForColumn(column, alias);
  std::string graph_element_filter_clause;
  const ResolvedExpr* filter_expr = node->filter_expr();
  if (filter_expr != nullptr) {
    std::vector<const ResolvedExpr*> property_specifications;
    std::vector<const ResolvedExpr*> remaining_conjuncts;
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool found_dynamic_property_specifications,
        ContainsDynamicPropertySpecification(
            filter_expr, property_specifications, remaining_conjuncts));
    if (found_dynamic_property_specifications) {
      // $dynamic_property_equals function call must be built as a property
      // specifications as the function is internal and not exposed to users.
      // Also, property specification cannot coexist with where clause per GQL
      // spec, so in this case there must be no remaining conjuncts.
      GOOGLESQL_RET_CHECK(remaining_conjuncts.empty() || property_specifications.empty())
          << "Found both dynamic property specifications and remaining "
             "conjuncts that cannot be turned into property specifications."
          << filter_expr->DebugString();
      // Add property specifications
      std::vector<std::pair<std::string, const ResolvedExpr*>> name_value_pairs;
      GOOGLESQL_ASSIGN_OR_RETURN(name_value_pairs,
                       ToPropertySpecifications(property_specifications));
      if (!name_value_pairs.empty()) {
        absl::StrAppend(&graph_element_filter_clause, " {");
        GOOGLESQL_RETURN_IF_ERROR(
            StrJoin(&graph_element_filter_clause, name_value_pairs, ", ",
                    [this](std::string* out,
                           std::pair<std::string, const ResolvedExpr*> part)
                        -> absl::Status {
                      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                                       ProcessNode(part.second));
                      absl::StrAppend(out, part.first, ":", result->GetSQL());
                      return absl::OkStatus();
                    }));
        absl::StrAppend(&graph_element_filter_clause, "}");
      }
    } else {
      // Otherwise, process the filter expression as a normal where clause.
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(filter_expr));
      absl::StrAppend(&graph_element_filter_clause, " WHERE ",
                      result->GetSQL());
    }
  }
  std::string label_clause;
  const ResolvedGraphLabelExpr* label_expr = node->label_expr();
  if (label_expr != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> label_result,
                     ProcessNode(label_expr));
    absl::StrAppend(&label_clause, " IS ", label_result->GetSQL());
  }
  std::string cost_clause;
  if (node->Is<ResolvedGraphEdgeScan>()) {
    const ResolvedExpr* cost_expr =
        node->GetAs<ResolvedGraphEdgeScan>()->cost_expr();
    if (cost_expr != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> cost_result,
                       ProcessNode(cost_expr));
      absl::StrAppend(&cost_clause, " COST ", cost_result->GetSQL());
    }
  }

  PushQueryFragment(
      node, absl::StrCat(prefix, hint_sql, alias, label_clause,
                         graph_element_filter_clause, cost_clause, suffix));
  node->MarkFieldsAccessed();
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphNodeScan(
    const ResolvedGraphNodeScan* node) {
  return VisitResolvedGraphElementScanInternal(node, "(", ")");
}

absl::Status SQLBuilder::VisitResolvedGraphEdgeScan(
    const ResolvedGraphEdgeScan* node) {
  std::string left, right;

  if (!node->lhs_hint_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->lhs_hint_list(), &left));
    absl::StrAppend(&left, " ");
  }

  switch (node->orientation()) {
    case ResolvedGraphEdgeScan::ANY:
      left = "-[";
      right = "]-";
      break;
    case ResolvedGraphEdgeScan::LEFT:
      left = "<-[";
      right = "]-";
      break;
    case ResolvedGraphEdgeScan::RIGHT:
      left = "-[";
      right = "]->";
      break;
  }
  if (!node->rhs_hint_list().empty()) {
    absl::StrAppend(&right, " ");
    GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->rhs_hint_list(), &right));
  }
  return VisitResolvedGraphElementScanInternal(node, left, right);
}

absl::Status SQLBuilder::ProcessGraphPathPatternQuantifier(
    const ResolvedGraphPathPatternQuantifier* node, std::string& output_sql) {
  // If either or both bounds are a ResolvedCast, it means that the
  // original bounds in the query are a literal or parameter with a type
  // other than int64 and hence, analyzer has added a cast on top of it.
  // We should skip this cast here to avoid returning CAST(CAST ...)).
  std::unique_ptr<QueryFragment> upper_result;
  std::unique_ptr<QueryFragment> lower_result;
  absl::StrAppend(&output_sql, "{ ");
  if (node->lower_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        lower_result,
        ProcessNode(node->lower_bound()->node_kind() != RESOLVED_CAST
                        ? node->lower_bound()
                        : node->lower_bound()->GetAs<ResolvedCast>()->expr()));
    absl::StrAppend(&output_sql, lower_result->GetSQL(), ", ");
  }
  if (node->upper_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        upper_result,
        ProcessNode(node->upper_bound()->node_kind() != RESOLVED_CAST
                        ? node->upper_bound()
                        : node->upper_bound()->GetAs<ResolvedCast>()->expr()));
    absl::StrAppend(&output_sql, upper_result->GetSQL());
  }
  absl::StrAppend(&output_sql, " }");

  return absl::OkStatus();
}

void SQLBuilder::AppendGraphReference(const ResolvedGraphTableScan* node,
                                      std::string& output_sql) {
  // TODO: allow graph reference to be omitted.
  absl::StrAppend(&output_sql, " GRAPH ",
                  ToIdentifierLiteral(node->property_graph()->Name()), " ");
}

absl::Status SQLBuilder::ProcessResolvedGraphSubquery(
    const ResolvedGraphTableScan* node, std::string& output_sql) {
  AppendGraphReference(node, output_sql);
  std::string gql;
  GOOGLESQL_RET_CHECK(node->input_scan()->Is<ResolvedGraphLinearScan>());
  GOOGLESQL_RET_CHECK(node->shape_expr_list().empty());
  std::vector<std::string> unused;
  GOOGLESQL_RETURN_IF_ERROR(ProcessGqlCompositeScans(
      node->input_scan()->GetAs<ResolvedGraphLinearScan>(), gql, unused));
  absl::StrAppend(&output_sql, gql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendPathMode(const ResolvedGraphPathMode* path_mode,
                                        std::string& sql) {
  if (path_mode == nullptr) {
    return absl::OkStatus();
  }
  switch (path_mode->path_mode()) {
    case ResolvedGraphPathMode::PATH_MODE_UNSPECIFIED:
      break;
    case ResolvedGraphPathMode::WALK:
      absl::StrAppend(&sql, "WALK ");
      break;
    case ResolvedGraphPathMode::TRAIL:
      absl::StrAppend(&sql, "TRAIL ");
      break;
    case ResolvedGraphPathMode::SIMPLE:
      absl::StrAppend(&sql, "SIMPLE ");
      break;
    case ResolvedGraphPathMode::ACYCLIC:
      absl::StrAppend(&sql, "ACYCLIC ");
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unrecognized path mode: " << path_mode;
  }
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphPathScan(
    const ResolvedGraphPathScan* node) {
  // <head> and <tail> are not used in the sql, mark as accessed.
  node->head();
  node->tail();
  // Marking prefix as visited because it will be printed on GraphScan to avoid
  // being put into parentheses.
  node->search_prefix();
  std::string sql;
  GOOGLESQL_RETURN_IF_ERROR(ProcessResolvedGraphPathScan(node, sql));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::AppendSearchPrefix(
    const ResolvedGraphPathSearchPrefix* prefix, std::string& text) {
  if (prefix == nullptr) {
    return absl::OkStatus();
  }
  switch (prefix->type()) {
    case ResolvedGraphPathSearchPrefix::ANY:
      absl::StrAppend(&text, " ANY ");
      if (prefix->path_count() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> path_count,
                         ProcessNode(prefix->path_count()));
        absl::StrAppend(&text, path_count->GetSQL(), " ");
      }
      return absl::OkStatus();
    case ResolvedGraphPathSearchPrefix::SHORTEST:
      if (prefix->path_count() == nullptr) {
        absl::StrAppend(&text, " ANY SHORTEST ");
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> path_count,
                         ProcessNode(prefix->path_count()));
        absl::StrAppend(&text, " SHORTEST ", path_count->GetSQL(), " ");
      }
      return absl::OkStatus();
    case ResolvedGraphPathSearchPrefix::CHEAPEST:
      if (prefix->path_count() == nullptr) {
        absl::StrAppend(&text, " ANY CHEAPEST ");
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> path_count,
                         ProcessNode(prefix->path_count()));
        absl::StrAppend(&text, " CHEAPEST ", path_count->GetSQL(), " ");
      }
      return absl::OkStatus();
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unrecognized prefix type: " << prefix->type();
      return absl::OkStatus();
  }
}

absl::Status SQLBuilder::VisitResolvedGraphScan(const ResolvedGraphScan* node) {
  if (node->input_scan() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));
  }
  std::string sql;
  if (node->optional()) {
    absl::StrAppend(&sql, " OPTIONAL");
  }
  absl::StrAppend(&sql, " MATCH ");
  for (auto it = node->input_scan_list().begin();
       it != node->input_scan_list().end(); ++it) {
    if (it != node->input_scan_list().begin()) {
      absl::StrAppend(&sql, ",");
    }
    auto& input_scan = *it;
    // If this scan defines a path variable, print the path variable definition
    // only for the top level graph path.
    if (input_scan->path() != nullptr) {
      absl::StrAppend(&sql, GetColumnAlias(input_scan->path()->column()), "=");
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(input_scan.get()));
    GOOGLESQL_RETURN_IF_ERROR(AppendSearchPrefix(input_scan->search_prefix(), sql));
    if (input_scan->filter_expr() != nullptr ||
        input_scan->quantifier() != nullptr) {
      // Add a parentheses here for:
      // 1. WHERE clause as it only applies to a parenthesized path pattern.
      //    This may also come from multiply declared vars introducing SAME.
      // 2. If a path quantifier exists, it must always be preceded by a closing
      //    parentheses.
      absl::StrAppend(&sql, "(", result->GetSQL(), ")");
      // Now if the path contains a quantifier, print it here.
      if (input_scan->quantifier() != nullptr) {
        std::string quantifier_sql;
        GOOGLESQL_RETURN_IF_ERROR(ProcessGraphPathPatternQuantifier(
            input_scan->quantifier(), quantifier_sql));
        absl::StrAppend(&sql, std::move(quantifier_sql));
      }
    } else {
      absl::StrAppend(&sql, result->GetSQL());
    }
  }
  if (node->filter_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->filter_expr()));
    absl::StrAppend(&sql, " WHERE ", result->GetSQL());
  }
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphCallScan(
    const ResolvedGraphCallScan* node) {
  GOOGLESQL_RETURN_IF_ERROR(CheckScanIsGraphRefOrSingleRowScan(node->input_scan()));
  std::string sql;
  if (node->optional()) {
    absl::StrAppend(&sql, " OPTIONAL");
  }
  absl::StrAppend(&sql, " CALL ");

  if (node->subquery()->Is<ResolvedTVFScan>()) {
    PendingColumnsAutoRestorer column_restorer(*this, node->parameter_list());

    const auto* tvf = node->subquery()->GetAs<ResolvedTVFScan>();

    GOOGLESQL_ASSIGN_OR_RETURN(std::string tvf_sql,
                     ProcessResolvedTVFScan(tvf, TVFBuildMode::kGqlExplicit));
    absl::StrAppend(&sql, std::move(tvf_sql));
  } else {
    GOOGLESQL_RET_CHECK(node->subquery()->Is<ResolvedGraphTableScan>());
    const auto* subquery = node->subquery()->GetAs<ResolvedGraphTableScan>();
    GOOGLESQL_RET_CHECK(subquery->shape_expr_list().empty());
    // Dummy access: since we do not print the graph for CALL subquery.
    subquery->property_graph();

    absl::StrAppend(&sql, "(");
    bool first = true;
    for (auto& col_ref : node->parameter_list()) {
      // TODO: SqlBuilder needs to ensure all outer columns are
      // properly aliased.
      if (col_ref->is_correlated()) {
        GOOGLESQL_RET_CHECK_FAIL() << "SQL Builder does not support correlated columns "
                            "in the capture list";
      }
      SetPathForColumn(col_ref->column(),
                       ToIdentifierLiteral(GetColumnAlias(col_ref->column())));

      if (first) {
        first = false;
      } else {
        absl::StrAppend(&sql, ", ");
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                       ProcessNode(col_ref.get()));
      absl::StrAppend(&sql, result->GetSQL());
    }
    absl::StrAppend(&sql, ") {\n");

    PendingColumnsAutoRestorer column_restorer(*this, node->parameter_list());

    GOOGLESQL_RET_CHECK(subquery->input_scan()->Is<ResolvedGraphLinearScan>());
    std::vector<std::string> columns;
    GOOGLESQL_RETURN_IF_ERROR(ProcessGqlCompositeScans(
        subquery->input_scan()->GetAs<ResolvedGraphLinearScan>(), sql,
        columns));

    absl::StrAppend(&sql, "}\n");
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphGetElementProperty(
    const ResolvedGraphGetElementProperty* node) {
  std::string sql;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> graph_element,
                   ProcessNode(node->expr()));
  std::string property_name;
  if (node->property() != nullptr) {
    property_name = node->property()->Name();
    if (node->property_name() != nullptr) {
      SQLBuilder unused_builder(options_);
      GOOGLESQL_RETURN_IF_ERROR(node->property_name()->Accept(&unused_builder));
    }
  } else {
    GOOGLESQL_RET_CHECK(node->property_name() != nullptr);
    GOOGLESQL_RET_CHECK(node->property_name()->Is<ResolvedLiteral>());
    GOOGLESQL_RET_CHECK(node->property_name()->type()->IsString());
    property_name =
        node->property_name()->GetAs<ResolvedLiteral>()->value().string_value();
  }
  GOOGLESQL_RET_CHECK(!property_name.empty());
  absl::StrAppend(&sql, graph_element->GetSQL(), ".",
                  ToIdentifierLiteral(property_name));
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphIsLabeledPredicate(
    const ResolvedGraphIsLabeledPredicate* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->expr()));
  std::string sql = result->GetSQL();
  if (node->is_not()) {
    absl::StrAppend(&sql, " IS NOT LABELED ");
  } else {
    absl::StrAppend(&sql, " IS LABELED ");
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> rhs,
                   ProcessNode(node->label_expr()));
  absl::StrAppend(&sql, rhs->GetSQL());
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphLabel(
    const ResolvedGraphLabel* node) {
  std::string label_name;
  if (node->label() != nullptr) {
    label_name = node->label()->Name();
    if (node->label_name() != nullptr) {
      SQLBuilder unused_builder(options_);
      GOOGLESQL_RETURN_IF_ERROR(node->label_name()->Accept(&unused_builder));
    }
  } else {
    GOOGLESQL_RET_CHECK(node->label_name() != nullptr);
    GOOGLESQL_RET_CHECK(node->label_name()->Is<ResolvedLiteral>());
    GOOGLESQL_RET_CHECK(node->label_name()->type()->IsString());
    label_name =
        node->label_name()->GetAs<ResolvedLiteral>()->value().string_value();
  }
  GOOGLESQL_RET_CHECK(!label_name.empty());
  PushQueryFragment(node, ToIdentifierLiteral(label_name));
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphLabelNaryExpr(
    const ResolvedGraphLabelNaryExpr* node) {
  std::string sql;
  if (node->op() == ResolvedGraphLabelNaryExpr::NOT) {
    if (node->operand_list().size() == 1) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> operand_fragment,
                       ProcessNode(node->operand_list()[0].get()));
      sql = absl::StrCat("!", operand_fragment->GetSQL());
    }
  } else if (node->op() == ResolvedGraphLabelNaryExpr::AND) {
    GOOGLESQL_RETURN_IF_ERROR(MakeGraphLabelNaryExpressionString(" & ", node, sql));
  } else if (node->op() == ResolvedGraphLabelNaryExpr::OR) {
    GOOGLESQL_RETURN_IF_ERROR(MakeGraphLabelNaryExpressionString(" | ", node, sql));
  } else {
    return ::googlesql_base::InternalErrorBuilder()
           << "Unrecognized graph label operation type: "
           << node->GetGraphLogicalOpTypeString();
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedGraphWildCardLabel(
    const ResolvedGraphWildCardLabel* node) {
  PushQueryFragment(node, "%");
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedArrayAggregate(
    const ResolvedArrayAggregate* node) {
  GOOGLESQL_RET_CHECK(node->array()->Is<ResolvedColumnRef>())
      << "Currently always a column ref";
  SetPathForColumn(
      node->element_column(),
      GetColumnPath(node->array()->GetAs<ResolvedColumnRef>()->column()));
  for (const auto& computed_column :
       node->pre_aggregate_computed_column_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(computed_column->expr()));
    googlesql_base::InsertOrDie(&mutable_pending_columns(),
                     computed_column->column().column_id(), result->GetSQL());
  }
  return VisitResolvedAggregateFunctionCall(node->aggregate());
}

absl::Status SQLBuilder::MaybeWrapStandardQueryAsPipeQuery(
    const ResolvedScan* node, QueryExpression* query_expression) {
  const std::string alias = GetScanAlias(node);
  GOOGLESQL_RETURN_IF_ERROR(AddSelectListIfNeeded(node->column_list(), query_expression));
  if (IsPipeSyntaxTargetMode()) {
    query_expression->Wrap(alias);
  } else {
    query_expression->WrapForPipeSyntaxMode(alias);
  }
  SetPathForColumnList(node->column_list(), alias);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<QueryExpression>>
SQLBuilder::MaybeWrapPipeQueryAsStandardQuery(
    const ResolvedScan& pipe_query_node, absl::string_view pipe_sql) {
  auto pipe_query_expr =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  if (IsPipeSyntaxTargetMode()) {
    GOOGLESQL_RET_CHECK(pipe_query_expr->TrySetFromClause(pipe_sql));
  } else {
    // In standard mode, the pipe query has to be wrapped in parentheses to act
    // as a subquery.
    std::string wrapped_assert_sql = absl::StrCat("(", pipe_sql, ")");
    GOOGLESQL_RET_CHECK(pipe_query_expr->TrySetFromClause(wrapped_assert_sql));
    GOOGLESQL_RETURN_IF_ERROR(
        WrapQueryExpression(&pipe_query_node, pipe_query_expr.get()));
  }
  return pipe_query_expr;
}

absl::Status SQLBuilder::VisitResolvedDescribeScan(
    const ResolvedDescribeScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);

  GOOGLESQL_RETURN_IF_ERROR(
      MaybeWrapStandardQueryAsPipeQuery(node->input_scan(), query_expr.get()));

  // Mark the expression as visited.  We don't actually use the output value
  // from the existing resolved AST.  The new `|> DESCRIBE` will compute a
  // new DESCRIBE output when it runs.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> ignored_expr,
                   ProcessNode(node->describe_expr()->expr()));

  SetPathForColumn(node->describe_expr()->column(),
                   ToIdentifierLiteral(node->describe_expr()->column().name()));

  // Construct the SQL for the pipe operator as follows:
  // <input_scan> |> DESCRIBE
  std::string sql =
      absl::StrCat(query_expr->GetSQLQuery(TargetSyntaxMode::kPipe),
                   QueryExpression::kPipe, "DESCRIBE");

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> new_query_expr,
                   MaybeWrapPipeQueryAsStandardQuery(*node, sql));

  PushSQLForQueryExpression(node, new_query_expr.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedStaticDescribeScan(
    const ResolvedStaticDescribeScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);
  GOOGLESQL_RETURN_IF_ERROR(WrapQueryExpression(node->input_scan(), query_expr.get()));

  // TODO We just lose |> STATIC_DESCRIBE for now.
  // When the SQLBuilder supports adding a pipe operator, we should add it.
  PushSQLForQueryExpression(node, query_expr.release());

  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedAssertScan(
    const ResolvedAssertScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);

  GOOGLESQL_RETURN_IF_ERROR(
      MaybeWrapStandardQueryAsPipeQuery(node->input_scan(), query_expr.get()));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> condition,
                   ProcessNode(node->condition()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> message,
                   ProcessNode(node->message()));

  GOOGLESQL_RET_CHECK(input != nullptr);
  GOOGLESQL_RET_CHECK(condition != nullptr);
  GOOGLESQL_RET_CHECK(message != nullptr);

  // Construct the SQL for the pipe operator as follows:
  // <input_scan> |> ASSERT <condition>, <message>
  std::string assert_sql = absl::StrCat(
      query_expr->GetSQLQuery(TargetSyntaxMode::kPipe), QueryExpression::kPipe,
      "ASSERT ", condition->GetSQL(), ", ", message->GetSQL());

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> assert_query_expr,
                   MaybeWrapPipeQueryAsStandardQuery(*node, assert_sql));

  PushSQLForQueryExpression(node, assert_query_expr.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedLogScan(const ResolvedLogScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);

  GOOGLESQL_RETURN_IF_ERROR(
      MaybeWrapStandardQueryAsPipeQuery(node->input_scan(), query_expr.get()));

  // The subpipeline under ABSL_LOG doesn't affect the main execution pipeline so we
  // don't want any of its added aliases to affect later operators.
  // Save the state so we can undo any changes it makes.
  // Depending how this gets used for different operators, this may belong
  // as part of VisitResolvedSubpipeline rather than getting copied,
  // Save state on the heap because it might be large.
  auto saved_state = std::make_unique<CopyableState>(state_);

  GOOGLESQL_RET_CHECK(node->subpipeline() != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> subpipeline_fragment,
                   ProcessNode(node->subpipeline()));

  // Avoid errors about unreferenced fields.  The output columns don't show up
  // as part of the syntax for ABSL_LOG.
  GOOGLESQL_RET_CHECK(node->output_schema() != nullptr);
  for (const auto& output_column :
       node->output_schema()->output_column_list()) {
    output_column->MarkFieldsAccessed();
  }
  node->output_schema()->is_value_table();

  // Construct the SQL for the pipe operator as follows:
  //   <input_scan> |> ABSL_LOG <hint> <subpipeline>
  std::string log_sql =
      absl::StrCat(query_expr->GetSQLQuery(TargetSyntaxMode::kPipe),
                   QueryExpression::kPipe, "LOG ");
  GOOGLESQL_RETURN_IF_ERROR(AppendHintsIfPresent(node->hint_list(), &log_sql));
  absl::StrAppend(&log_sql, subpipeline_fragment->GetSQL());

  // Restore the state from before the ABSL_LOG subpipeline was analyzed.
  state_ = std::move(*saved_state);

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> output_query_expr,
                   MaybeWrapPipeQueryAsStandardQuery(*node, log_sql));

  PushSQLForQueryExpression(node, output_query_expr.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedPipeIfScan(
    const ResolvedPipeIfScan* node) {
  // Generate SQL for the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->input_scan()));
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);

  GOOGLESQL_RETURN_IF_ERROR(
      MaybeWrapStandardQueryAsPipeQuery(node->input_scan(), query_expr.get()));

  std::string if_sql = absl::StrCat(
      query_expr->GetSQLQuery(TargetSyntaxMode::kPipe), QueryExpression::kPipe);

  for (int i = 0; i < node->if_case_list().size(); ++i) {
    const ResolvedPipeIfCase* if_case = node->if_case_list(i);
    bool is_selected_case = (i == node->selected_case());
    if (i == 0) {
      absl::StrAppend(&if_sql, "IF ");
    } else if (if_case->IsElse()) {
      GOOGLESQL_RET_CHECK_EQ(i, node->if_case_list().size() - 1);
      absl::StrAppend(&if_sql, "ELSE ");
    } else {
      absl::StrAppend(&if_sql, "ELSEIF ");
    }

    if (!if_case->IsElse()) {
      GOOGLESQL_RET_CHECK(if_case->condition() != nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> condition,
                       ProcessNode(if_case->condition()));
      absl::StrAppend(&if_sql, condition->GetSQL(), " THEN ");
    }

    // For the selected case, build the subpipeline from its resolved AST.
    // For others, just emit the `subpipeline_sql` string.
    if (is_selected_case) {
      GOOGLESQL_RET_CHECK(if_case->subpipeline() != nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> fragment,
                       ProcessNode(if_case->subpipeline()));
      absl::StrAppend(&if_sql, fragment->GetSQL(), "\n");
    } else {
      GOOGLESQL_RET_CHECK(!if_case->subpipeline_sql().empty());
      absl::StrAppend(&if_sql, if_case->subpipeline_sql(), "\n");
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> output_query_expr,
                   MaybeWrapPipeQueryAsStandardQuery(*node, if_sql));

  PushSQLForQueryExpression(node, output_query_expr.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedPipeForkScan(
    const ResolvedPipeForkScan* node) {
  // TODO Implement SQLBuilder for FORK.  We can't
  // currently generate it without using subqueries, and FORK doesn't work
  // in subqueries.
  GOOGLESQL_RET_CHECK_FAIL() << "SQLBuilder for FORK not supported yet";
}

absl::Status SQLBuilder::VisitResolvedPipeTeeScan(
    const ResolvedPipeTeeScan* node) {
  // TODO Implement SQLBuilder for TEE.  We can't
  // currently generate it without using subqueries, and TEE doesn't work
  // in subqueries.
  GOOGLESQL_RET_CHECK_FAIL() << "SQLBuilder for TEE not supported yet";
}

absl::Status SQLBuilder::VisitResolvedPipeExportDataScan(
    const ResolvedPipeExportDataScan* node) {
  return VisitResolvedExportDataStmtImpl(node->export_data_stmt(),
                                         /*generate_as_pipe=*/true);
}

absl::Status SQLBuilder::VisitResolvedPipeCreateTableScan(
    const ResolvedPipeCreateTableScan* node) {
  return VisitResolvedCreateTableAsSelectStmtImpl(
      node->create_table_as_select_stmt(), /*generate_as_pipe=*/true);
}

absl::Status SQLBuilder::VisitResolvedPipeInsertScan(
    const ResolvedPipeInsertScan* node) {
  return VisitResolvedInsertStmtImpl(node->insert_stmt(),
                                     /*generate_as_pipe=*/true);
}

absl::Status SQLBuilder::VisitResolvedSubpipeline(
    const ResolvedSubpipeline* node) {
  // This is a hack to generate subpipeline SQL before we have SQLBuilder pipe
  // syntax support.  We want it to start with a pipe operator, maybe using an
  // empty string for the ResolvedSubpipelineInputScan, and then adding pipe
  // operators onto it.
  //
  // Instead, using the standard-syntax builder, we'll get an inside-out query
  // that has the ResolvedSubpipelineInputScan in the middle.  To make that work
  // as a subpipeline, we'll generate a complex subquery something like this.
  // See the code below for the full pattern.
  //
  //   |> AGRREGATE ARRAY_AGG(STRUCT(<column list>)) AS __SubpipelineInput__
  //   |> JOIN UNNEST(ARRAY(
  //        # This subquery is the one generated by the SQLBuilder.
  //        SELECT ...
  //        FROM (
  //          # This subquery is the ResolvedSubpipelineInputScan.
  //          SELECT * FROM UNNEST(__SubpipelineInput__)
  //        )
  //
  // Explanation:
  // It first bundles the subpipeline input into one row as an array of structs.
  // Then it does a lateral join (using the array hack) to execute a subquery
  // (generated by SQLBuilder for the subpipeline) over that one row of input.
  // The innermost subquery comes from the ResolvedSubpipelineInputScan, and
  // it will UNNEST the array value to produce the subpipeline input rows.
  //
  // When we have real pipe syntax support in the SQLBuilder, we can avoid this.
  // TODO Replace with pipe SQLBuilder support.

  // Generate SQL for the scan.  This will generate a query that starts by
  // reading a ResolvedSubpipelineInputScan placeholder table.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> input,
                   ProcessNode(node->scan()));

  // Convert the query to a subquery. This also adds the needed parentheses.
  std::unique_ptr<QueryExpression> query_expr =
      std::move(input->query_expression);
  GOOGLESQL_RET_CHECK(query_expr != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      MaybeWrapStandardQueryAsPipeQuery(node->scan(), query_expr.get()));
  std::string subquery_sql = query_expr->GetSQLQuery(TargetSyntaxMode::kPipe);

  // Generate the subpipeline SQL.
  // The `SELECT *` pieces seem to work but may have some edge case issues.
  // If this was permanent, it should probably list columns explicitly.
  std::string sql = absl::StrCat(
      R"(( |> SELECT AS STRUCT *
  |> AS __StructValue__
  |> AGGREGATE ARRAY_AGG(__StructValue__) AS __SubpipelineInput__
  |> JOIN UNNEST(ARRAY(
          )",
      subquery_sql, QueryExpression::kPipe, R"(
     SELECT AS STRUCT *))
  |> SELECT *
  |> AS )",
      GetScanAlias(node->scan()), ")\n");

  // Return the subpipeline as a fragment, since it isn't a query.
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedSubpipelineInputScan(
    const ResolvedSubpipelineInputScan* node) {
  // This node is a placeholder scan at the start of a subpipeline.  It
  // shouldn't actually show up in the SQL, since the subpipeline starts
  // directly with pipe operators, so we'd prefer to just return an empty
  // string here and then let the following scans add pipe operators onto it.
  //
  // We can't do that yet, since the SQLBuilder will generate a standard syntax
  // query rather than pipe operators.  So we need to generate a query that
  // works as the innermost scan for that query.  The pattern is described
  // above in VisitResolvedSubpipeline.

  auto query_expression =
      std::make_unique<QueryExpression>(this->options_.target_syntax_mode);
  GOOGLESQL_RET_CHECK(query_expression->TrySetFromClause("UNNEST(__SubpipelineInput__)"));
  GOOGLESQL_RET_CHECK(query_expression->TrySetSelectClause(
      {std::make_pair("*", "" /* no select_alias */)}, "" /* no hints */));

  PushSQLForQueryExpression(node, query_expression.release());
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedSubpipelineStmt(
    const ResolvedSubpipelineStmt* node) {
  // Visit the `table_scan` but discard its output since the table scan is
  // implicit for a standalone subpipeline.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> table_scan,
                   ProcessNode(node->table_scan()));

  // TODO: b/307313878 - These don't really work yet.  We need to
  // generate just a subpipeline, without an input scan before it.
  std::string sql;
  if (node->output_schema() == nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                     ProcessNode(node->subpipeline()));
    sql = result->GetSQL();
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryExpression> result,
                     ProcessQuery(node->subpipeline()->scan(),
                                  node->output_schema()->output_column_list()));
    sql = result->GetSQLQuery();
  }

  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedStatementWithPipeOperatorsStmt(
    const ResolvedStatementWithPipeOperatorsStmt* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<QueryFragment> result,
                   ProcessNode(node->statement()));
  std::string sql = absl::StrCat(result->GetSQL(), "\n",
                                 node->suffix_subpipeline_sql()->value());
  PushQueryFragment(node, sql);
  return absl::OkStatus();
}

absl::Status SQLBuilder::VisitResolvedBarrierScan(
    const ResolvedBarrierScan* node) {
  // No SQL syntax corresponding to BarrierScan, so we simply generate SQL for
  // the input scan. Note that this will lose the BarrierScan semantics.
  return node->input_scan()->Accept(this);
}

absl::Status SQLBuilder::VisitResolvedLockMode(const ResolvedLockMode* node) {
  switch (node->strength()) {
    case ResolvedLockModeEnums::UPDATE:
      PushQueryFragment(node, "UPDATE");
      break;
    default:
      break;
  }
  return absl::OkStatus();
}

}  // namespace googlesql
