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

// This rewriter rewrites a ResolvedWithExpr into one or more project scans.
// The output is converted as follows (represented as SQL):
//
// WITH(a AS 1, b AS 2, c AS a + b, a + b + c)
//
// SELECT a + b + c FROM (
//     SELECT a, b, a + b AS c FROM (
//         SELECT a, 2 AS b FROM (
//             SELECT 1 AS a
//         )
//     )
// )
//
// As shown, each computed column in the WITH/Let expression can refer to prior
// columns in the expression.

#include "googlesql/analyzer/rewriters/with_expr_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// A visitor that rewrites ResolvedWithExpr into a subquery expression.
class WithExprRewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  explicit WithExprRewriterVisitor(ColumnFactory* column_factory)
      : column_factory_(column_factory) {}

 private:
  absl::Status VisitResolvedWithExpr(const ResolvedWithExpr* node) override;

  // Creates a projection that projects:
  // 1. column
  // 2. all columns from input_scan, if input_scan is not null.
  absl::StatusOr<std::unique_ptr<ResolvedScan>> MakeProjectionForComputedColumn(
      const ResolvedComputedColumn& column,
      std::unique_ptr<ResolvedScan> input_scan);

  ColumnFactory* column_factory_;
};

absl::StatusOr<std::unique_ptr<ResolvedScan>>
WithExprRewriterVisitor::MakeProjectionForComputedColumn(
    const ResolvedComputedColumn& column,
    std::unique_ptr<ResolvedScan> input_scan) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto processed, ProcessNode(&column));
  std::vector<ResolvedColumn> projection_columns = input_scan->column_list();
  projection_columns.push_back(processed->column());
  std::vector<std::unique_ptr<ResolvedComputedColumn>> assignment;
  assignment.push_back(std::move(processed));
  return MakeResolvedProjectScan(projection_columns, std::move(assignment),
                                 std::move(input_scan));
}

absl::Status WithExprRewriterVisitor::VisitResolvedWithExpr(
    const ResolvedWithExpr* node) {
  // TODO: In case assignments are 'cheap' and non-volatile, inline them
  //     instead of using a subquery. We need to define 'cheap'.

  // Copy the let expr with references to free variables correlated.
  GOOGLESQL_ASSIGN_OR_RETURN(auto with_expr, CorrelateColumnRefs(*node));

  std::unique_ptr<ResolvedScan> assignments_projection =
      MakeResolvedSingleRowScan();
  for (const auto& col : with_expr->assignment_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(assignments_projection,
                     MakeProjectionForComputedColumn(
                         *col, std::move(assignments_projection)));
  }

  // Build result expression projection.
  ResolvedColumn result_column =
      column_factory_->MakeCol("$with_expr", "injected", with_expr->type());
  GOOGLESQL_ASSIGN_OR_RETURN(auto result_expr, ProcessNode(with_expr->expr()));
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> result_exprs;
  result_exprs.emplace_back(
      MakeResolvedComputedColumn(result_column, std::move(result_expr)));
  auto result_projection =
      MakeResolvedProjectScan({result_column}, std::move(result_exprs),
                              std::move(assignments_projection));

  // Build subquery.
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  GOOGLESQL_RETURN_IF_ERROR(CollectSortUniqueColumnRefs(*node, column_refs));
  PushNodeToStack(MakeResolvedSubqueryExpr(
      with_expr->type(), ResolvedSubqueryExpr::SCALAR, std::move(column_refs),
      /*in_expr=*/nullptr, std::move(result_projection)));
  return absl::OkStatus();
}

}  // namespace

class WithExprRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    WithExprRewriterVisitor rewriter(&column_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> result,
                     rewriter.ConsumeRootNode<ResolvedNode>());
    return result;
  }

  std::string Name() const override { return "WithExprRewriter"; }
};

const Rewriter* GetWithExprRewriter() {
  static const auto* const kRewriter = new WithExprRewriter;
  return kRewriter;
}

}  // namespace googlesql
