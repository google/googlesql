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

#include "googlesql/analyzer/rewriters/delete_with_join_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/base/no_destructor.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

// Rewrites `ColumnRef` expressions inside the `WHERE` clause that reference
// target table columns to have `is_correlated = true`, and collects the set of
// target table columns referenced.
class CorrelatedTargetColumnRewriter : public ResolvedASTRewriteVisitor {
 public:
  explicit CorrelatedTargetColumnRewriter(
      const absl::flat_hash_set<ResolvedColumn>& target_columns)
      : target_columns_(target_columns) {}

  const absl::btree_set<ResolvedColumn>& referenced_target_columns() const {
    return referenced_target_columns_;
  }

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedColumnRef(
      std::unique_ptr<const ResolvedColumnRef> node) override {
    if (target_columns_.contains(node->column())) {
      referenced_target_columns_.insert(node->column());
      if (!node->is_correlated()) {
        return ToBuilder(std::move(node)).set_is_correlated(true).Build();
      }
    }
    return node;
  }

  // All columns produced by the target table scan (including the timestamp
  // version column, if present).
  const absl::flat_hash_set<ResolvedColumn>& target_columns_;
  // The subset of `target_columns_` that are actually referenced in
  // `where_expr` and need to be passed as correlated parameters to the `EXISTS`
  // subquery.
  absl::btree_set<ResolvedColumn> referenced_target_columns_;
};

// A visitor that rewrites `ResolvedDeleteStmt` with a `using_scan` into a
// `ResolvedDeleteStmt` with an `EXISTS` subquery in `where_expr`.
//
// Specifically, a query like:
//   DELETE FROM target_table
//   USING using_table
//   WHERE where_condition
//   [THEN RETURN ...]
//
// gets rewritten to:
//   DELETE FROM target_table
//   WHERE EXISTS (
//     SELECT NULL
//     FROM using_table
//     WHERE where_condition
//   )
//   [THEN RETURN ...]
//
// where any references to `target_table` columns inside `where_condition`
// become correlated column references in the `EXISTS` subquery.
class DeleteWithJoinRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  DeleteWithJoinRewriteVisitor() = default;

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedDeleteStmt(
      std::unique_ptr<const ResolvedDeleteStmt> node) override {
    // This rewrite only works for top-level `DELETE` statements with a `USING`
    // clause (`using_scan != nullptr`).
    // TODO: b/535677131 - `ResolvedDeleteStmt` under `ResolvedDeleteScan`
    // should not be rewritten by this rule; bail in the case of
    // `ResolvedDeleteScan` once GQL `DELETE` is supported.
    GOOGLESQL_RET_CHECK(node->using_scan() != nullptr)
        << "Expected non-null using_scan in ResolvedDeleteStmt";
    GOOGLESQL_RET_CHECK(node->table_scan() != nullptr)
        << "Expected non-null table_scan in ResolvedDeleteStmt";

    // 1. Identify target table columns.
    absl::flat_hash_set<ResolvedColumn> target_columns(
        node->table_scan()->column_list().begin(),
        node->table_scan()->column_list().end());
    if (node->timestamp_version_column() != nullptr) {
      target_columns.insert(node->timestamp_version_column()->column());
    }

    // 2. Rewrite `where_expr` to mark references to target table columns as
    // correlated and collect the referenced columns.
    ResolvedDeleteStmtBuilder delete_stmt_builder = ToBuilder(std::move(node));
    std::unique_ptr<const ResolvedExpr> where_expr =
        delete_stmt_builder.release_where_expr();
    GOOGLESQL_RET_CHECK(where_expr != nullptr)
        << "Expected non-null where_expr in ResolvedDeleteStmt";

    CorrelatedTargetColumnRewriter column_rewriter(target_columns);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> rewritten_where_expr,
        column_rewriter.VisitAll<ResolvedExpr>(std::move(where_expr)));

    // 3. Create `FilterScan` with `using_scan` as `input_scan` and rewritten
    // `where_expr` as `filter_expr`.
    std::unique_ptr<const ResolvedScan> using_scan =
        delete_stmt_builder.release_using_scan();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedFilterScan> filter_scan,
                     ResolvedFilterScanBuilder()
                         .set_column_list({})
                         .set_input_scan(std::move(using_scan))
                         .set_filter_expr(std::move(rewritten_where_expr))
                         .Build());

    // 4. Create `parameter_list` for the `EXISTS` subquery.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list;
    parameter_list.reserve(column_rewriter.referenced_target_columns().size());
    for (const ResolvedColumn& col :
         column_rewriter.referenced_target_columns()) {
      parameter_list.push_back(
          MakeResolvedColumnRef(col.type(), col, /*is_correlated=*/false));
    }

    // 5. Create `ResolvedSubqueryExpr` of type `EXISTS`.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedSubqueryExpr> exists_subquery,
        ResolvedSubqueryExprBuilder()
            .set_type(types::BoolType())
            .set_subquery_type(ResolvedSubqueryExpr::EXISTS)
            .set_parameter_list(std::move(parameter_list))
            .set_subquery(std::move(filter_scan))
            .Build());

    // 6. Recreate `DeleteStmt` with new `where_expr` and `using_scan =
    // nullptr`.
    return std::move(delete_stmt_builder)
        .set_where_expr(std::move(exists_subquery))
        .set_using_scan(nullptr)
        .Build();
  }
};

class DeleteWithJoinRewriter : public Rewriter {
 public:
  std::string Name() const override { return "DeleteWithJoinRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    // TODO: b/535677131 - Bail in the case of `ResolvedDeleteScan` once GQL
    // `DELETE` is supported. For now, `GOOGLESQL_RET_CHECK` that the root input is a
    // top-level `ResolvedDeleteStmt`.
    GOOGLESQL_RET_CHECK_EQ(input->node_kind(), RESOLVED_DELETE_STMT);
    DeleteWithJoinRewriteVisitor rewriter;
    return rewriter.VisitAll(std::move(input));
  }
};

}  // namespace

const Rewriter* GetDeleteWithJoinRewriter() {
  static const absl::NoDestructor<DeleteWithJoinRewriter> kRewriter;
  return kRewriter.get();
}

}  // namespace googlesql
