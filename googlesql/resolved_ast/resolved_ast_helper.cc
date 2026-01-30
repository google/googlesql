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

#include "googlesql/resolved_ast/resolved_ast_helper.h"

#include <memory>
#include <vector>

#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::Status ColumnRefVisitor::VisitResolvedColumnRef(
    const ResolvedColumnRef* node) {
  if (!IsLocalColumn(node->column())) {
    // Subclass should overwrite this function to read or collect ColumnRef
    // information.
  }
  return absl::OkStatus();
}

absl::Status ColumnRefVisitor::VisitResolvedSubqueryExpr(
    const ResolvedSubqueryExpr* node) {
  for (const auto& column : node->parameter_list()) {
    GOOGLESQL_RETURN_IF_ERROR(VisitResolvedColumnRef(column.get()));
  }
  if (node->in_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(node->in_expr()->Accept(this));
  }
  // Cut off traversal once we hit a subquery. Column refs inside subquery are
  // either internal or already collected in parameter_list.
  return absl::OkStatus();
}

absl::Status ColumnRefVisitor::VisitResolvedInlineLambda(
    const ResolvedInlineLambda* node) {
  for (const auto& column_ref : node->parameter_list()) {
    GOOGLESQL_RETURN_IF_ERROR(VisitResolvedColumnRef(column_ref.get()));
  }
  // Cut off traversal once we hit a lambda. Column refs inside lambda body
  // are either internal or already collected in parameter_list.
  return absl::OkStatus();
}

absl::Status ColumnRefVisitor::VisitResolvedWithExpr(
    const ResolvedWithExpr* node) {
  // Exclude the assignment columns because they are internal.
  for (int i = 0; i < node->assignment_list_size(); ++i) {
    local_columns_.insert(node->assignment_list(i)->column());
  }
  return ResolvedASTVisitor::VisitResolvedWithExpr(node);
}

absl::Status ColumnRefVisitor::VisitResolvedArrayAggregate(
    const ResolvedArrayAggregate* node) {
  // Exclude the element column because it is internal.
  local_columns_.insert(node->element_column());
  // And exclude the computed columns.
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       node->pre_aggregate_computed_column_list()) {
    local_columns_.insert(computed_column->column());
  }
  return ResolvedASTVisitor::VisitResolvedArrayAggregate(node);
}

class ColumnRefCollectorUnowned : public ColumnRefVisitor {
 public:
  explicit ColumnRefCollectorUnowned(
      absl::flat_hash_set<const ResolvedColumnRef*>& column_refs)
      : column_refs_(column_refs) {}

 private:
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    if (!IsLocalColumn(node->column())) {
      column_refs_.insert(node);
    }
    return absl::OkStatus();
  }

  absl::flat_hash_set<const ResolvedColumnRef*>& column_refs_;
};

absl::StatusOr<absl::flat_hash_set<const ResolvedColumnRef*>>
CollectFreeColumnRefs(const ResolvedNode& node) {
  absl::flat_hash_set<const ResolvedColumnRef*> column_refs;
  ColumnRefCollectorUnowned column_ref_collector(column_refs);
  GOOGLESQL_RETURN_IF_ERROR(node.Accept(&column_ref_collector));
  return column_refs;
}

// Record node to parent mappings if they are potentially on a subtree that
// represents path expression.
class ColumnRefParentPointerCollector : public ResolvedASTVisitor {
 public:
  explicit ColumnRefParentPointerCollector(
      const absl::flat_hash_set<ResolvedColumn>& column_ids,
      absl::flat_hash_set<const ResolvedColumnRef*>& matched_column_refs)
      : target_column_ids_(column_ids),
        matched_column_refs_(matched_column_refs) {}

  const ParentPointerMap& GetParentPointerMap() const {
    return node_to_parent_;
  }

 protected:
  const ResolvedNode* parent() const {
    if (parent_stack_.empty()) {
      return nullptr;
    }
    return parent_stack_.back();
  }

  absl::Status DefaultVisit(const ResolvedNode* node) override {
    parent_stack_.push_back(node);
    absl::Status status = node->ChildrenAccept(this);
    parent_stack_.pop_back();
    return status;
  }

  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    if (target_column_ids_.contains(node->column())) {
      node_to_parent_.emplace(node, parent());
      matched_column_refs_.insert(node);
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedGetStructField(
      const ResolvedGetStructField* node) override {
    node_to_parent_.emplace(node, parent());
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedGetProtoField(
      const ResolvedGetProtoField* node) override {
    node_to_parent_.emplace(node, parent());
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedGetJsonField(
      const ResolvedGetJsonField* node) override {
    node_to_parent_.emplace(node, parent());
    return DefaultVisit(node);
  }

 private:
  std::vector<const ResolvedNode*> parent_stack_;
  ParentPointerMap node_to_parent_;
  const absl::flat_hash_set<ResolvedColumn>& target_column_ids_;
  absl::flat_hash_set<const ResolvedColumnRef*>& matched_column_refs_;
};

absl::StatusOr<ParentPointerMap>
CollectParentPointersOfUnboundedFieldAccessPaths(
    const ResolvedExpr& expr,
    const absl::flat_hash_set<ResolvedColumn>& free_columns,
    absl::flat_hash_set<const ResolvedColumnRef*>& matched_column_refs) {
  ColumnRefParentPointerCollector parent_pointer_collector(free_columns,
                                                           matched_column_refs);
  GOOGLESQL_RETURN_IF_ERROR(expr.Accept(&parent_pointer_collector));
  return parent_pointer_collector.GetParentPointerMap();
}

const ResolvedComputedColumn* FindProjectComputedColumn(
    const ResolvedProjectScan* project, const ResolvedColumn& column) {
  for (const auto& computed_column : project->expr_list()) {
    if (computed_column->column() == column) {
      return computed_column.get();
    }
  }
  return nullptr;
}

const ResolvedExpr* FindProjectExpr(const ResolvedProjectScan* project,
                                    const ResolvedColumn& column) {
  const ResolvedComputedColumn* computed_column =
      FindProjectComputedColumn(project, column);
  if (computed_column != nullptr) {
    return computed_column->expr();
  }
  return nullptr;
}

}  // namespace googlesql
