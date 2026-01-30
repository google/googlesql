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

#ifndef GOOGLESQL_RESOLVED_AST_RESOLVED_AST_HELPER_H_
#define GOOGLESQL_RESOLVED_AST_RESOLVED_AST_HELPER_H_

#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

using ParentPointerMap = absl::flat_hash_map<const ResolvedNode* /*child*/,
                                             const ResolvedNode* /*parent*/>;

// A visitor which collects free ResolvedColumnRef that are referenced, but not
// local to this expression.
class ColumnRefVisitor : public ResolvedASTVisitor {
 public:
  ColumnRefVisitor() = default;

 protected:
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override;

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override;

  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override;

  absl::Status VisitResolvedWithExpr(const ResolvedWithExpr* node) override;

  absl::Status VisitResolvedArrayAggregate(
      const ResolvedArrayAggregate* node) override;

  bool IsLocalColumn(const ResolvedColumn& column) const {
    return local_columns_.contains(column);
  }

 private:
  // Columns that are local to an expression -- that is they are defined,
  // populated, and consumed fully within the expression -- should not be
  // collected by this code.
  absl::flat_hash_set<ResolvedColumn> local_columns_;
};

// Returns all ResolvedColumnRef's from the tree under `node` that are
// references to free columns. Note that, a "free" column means that it is not
// defined within the tree under `node`.
absl::StatusOr<absl::flat_hash_set<const ResolvedColumnRef*>>
CollectFreeColumnRefs(const ResolvedNode& node);

// Returns a map of parent-child relationships between ResolvedAST nodes that
// comprise field access path expressions out of the `expr`. In particular, this
// function is only collecting paths that begin at column references for columns
// defined outside `expr`.
//
// `expr`: All parent-child relationships are edges under its resolved AST.
// `free_columns`: The set of columns defined outside of `expr`.
// `matched_column_refs`: The set of column refs that contain any column in
// `free_columns`, which will also be returned by this function.
absl::StatusOr<ParentPointerMap>
CollectParentPointersOfUnboundedFieldAccessPaths(
    const ResolvedExpr& expr,
    const absl::flat_hash_set<ResolvedColumn>& free_columns,
    absl::flat_hash_set<const ResolvedColumnRef*>& matched_column_refs);

// Find the ResolvedComputedColumn in a ResolvedProjectScan producing <column>.
// Return NULL if <column> is not computed by <project>.
const ResolvedComputedColumn* FindProjectComputedColumn(
    const ResolvedProjectScan* project, const ResolvedColumn& column);

// Find the ResolvedExpr in a ResolvedProjectScan producing <column>.
// Return NULL if <column> is not computed by <project>.
const ResolvedExpr* FindProjectExpr(const ResolvedProjectScan* project,
                                    const ResolvedColumn& column);

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_RESOLVED_AST_HELPER_H_
