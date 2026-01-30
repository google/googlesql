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

#include "googlesql/analyzer/rewriters/measure_type_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/rewriters/measure_collector.h"
#include "googlesql/analyzer/rewriters/measure_reference_rewrite_util.h"
#include "googlesql/analyzer/rewriters/measure_source_scan_rewrite_util.h"
#include "googlesql/analyzer/rewriters/measure_type_rewriter_util.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// `UnusedCorrelatedColumnPruner` removes any ColumnRefs in the `parameter_list`
// of subqueries or lambdas if they are not referenced within the subquery or
// lambda body.
class UnusedCorrelatedColumnPruner : public ResolvedASTDeepCopyVisitor {
 public:
  static absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PruneUnusedCorrelatedColumns(std::unique_ptr<const ResolvedNode> input) {
    UnusedCorrelatedColumnPruner pruner;
    GOOGLESQL_RETURN_IF_ERROR(input->Accept(&pruner));
    return pruner.ConsumeRootNode<ResolvedNode>();
  }

 protected:
  absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
      const ResolvedColumn& column) override {
    for (auto& correlated_column_references :
         correlated_column_references_list_) {
      correlated_column_references.insert(column);
    }
    return column;
  }

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    // First, process the `in_expr` field. The `in_expr` does not see the
    // parameter list, so we must process it first, before we push a new
    // set of correlated column references onto the stack.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> rewritten_in_expr,
                     ProcessNode(node->in_expr()));

    // Track referenced columns.
    correlated_column_references_list_.push_back(
        absl::flat_hash_set<ResolvedColumn>());
    // Visit the subquery's body.
    GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten_subquery, ProcessNode(node->subquery()));

    // Make a copy of the subquery expr, since we need to modify the parameter
    // list.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedSubqueryExpr> copy,
                     ResolvedASTDeepCopyVisitor::Copy(node));
    auto builder = ToBuilder(std::move(copy));
    builder.set_subquery(std::move(rewritten_subquery));
    builder.set_in_expr(std::move(rewritten_in_expr));

    return PruneUnusedCorrelatedColumns(std::move(builder));
  }

  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override {
    // Track referenced columns.
    correlated_column_references_list_.push_back(
        absl::flat_hash_set<ResolvedColumn>());
    // Visit the lambda's body.
    GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten_body, ProcessNode(node->body()));

    // Make a copy of the subquery expr, since we need to modify the parameter
    // list.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedInlineLambda> copy,
                     ResolvedASTDeepCopyVisitor::Copy(node));
    auto builder = ToBuilder(std::move(copy));
    builder.set_body(std::move(rewritten_body));

    return PruneUnusedCorrelatedColumns(std::move(builder));
  }

 private:
  UnusedCorrelatedColumnPruner() = default;
  UnusedCorrelatedColumnPruner(const UnusedCorrelatedColumnPruner&) = delete;
  UnusedCorrelatedColumnPruner& operator=(const UnusedCorrelatedColumnPruner&) =
      delete;

  template <typename NodeBuilderType>
  absl::Status PruneUnusedCorrelatedColumns(NodeBuilderType&& builder) {
    GOOGLESQL_RET_CHECK(!correlated_column_references_list_.empty());
    absl::flat_hash_set<ResolvedColumn> current_correlated_column_references =
        correlated_column_references_list_.back();
    correlated_column_references_list_.pop_back();
    std::vector<std::unique_ptr<const ResolvedColumnRef>> new_parameter_list;
    for (std::unique_ptr<const ResolvedColumnRef>& parameter :
         builder.release_parameter_list()) {
      // Skip any parameters that are not used in the subquery. Else, preserve
      // it.
      if (!current_correlated_column_references.contains(parameter->column())) {
        continue;
      }
      new_parameter_list.push_back(std::move(parameter));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten_node,
                     std::forward<NodeBuilderType>(builder)
                         .set_parameter_list(std::move(new_parameter_list))
                         .BuildMutable());
    PushNodeToStack(std::move(rewritten_node));
    return absl::OkStatus();
  }

  // Track columns referenced within the body of a correlated expression (i.e.
  // a subquery or lambda)
  std::vector<absl::flat_hash_set<ResolvedColumn>>
      correlated_column_references_list_;
};

class MeasureTypeRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    if (!options.language().LanguageFeatureEnabled(
            FEATURE_MULTILEVEL_AGGREGATION)) {
      return absl::UnimplementedError(
          "Multi-level aggregation is needed to rewrite measures.");
    }
    if (!options.language().LanguageFeatureEnabled(FEATURE_GROUP_BY_STRUCT)) {
      return absl::UnimplementedError(
          "Grouping by STRUCT types is needed to rewrite measures.");
    }

    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(/*max_seen_col_id=*/0,
                                 *options.id_string_pool(),
                                 *options.column_id_sequence_number());

    // Step 0: Find unsupported query shapes, and return an error if any are
    // found.
    GOOGLESQL_RETURN_IF_ERROR(HasUnsupportedQueryShape(input.get()));

    const Function* any_value_fn = nullptr;
    GOOGLESQL_RET_CHECK_OK(catalog.FindFunction({"any_value"}, &any_value_fn,
                                      options.find_options()));
    FunctionCallBuilder function_call_builder(options, catalog, type_factory);

    // The high-level rewrite algorithm is as follows:
    //
    // Step 1: Identify the measure columns that are AGG'ed, i.e., there is an
    // AGG(m) function call. Only the AGG'ed measure columns need to be
    // rewritten.
    //
    // Step 2: Identify the measure source columns, and rewrite their source
    // scans to replace the measure source columns with their computed closures.
    // Note only AGG'ed measure columns are rewritten.
    //
    // Step 3: Rewrite the measure column references.
    //
    // For each AGG(m) function call, we replace it with its measure definition
    // expression rewritten with multi-level aggregation to do grain locking.
    // The referenced columns are replaced with the corresponding struct field
    // access to the closure struct. For example, consider a measure column
    // m := SUM(col) with closure struct column s. We rewrite AGG(m) to
    //
    // ```
    // SUM(
    //   ANY_VALUE(s.referenced_columns.col)
    //   WHERE s IS NOT NULL
    //   GROUP BY s.key_columns
    // )
    // ```
    //
    // For other measure column references, we replace them with the
    // corresponding struct closure columns to propagate the struct closure.

    MeasureCollector measure_collector(column_factory);

    // Step 1: Find AGG(measure) calls and mark measures as AGG'ed.
    GOOGLESQL_RETURN_IF_ERROR(MarkAggedMeasures(input.get(), measure_collector));

    // Step 2: Identify the measure source columns.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> node,
                     AddClosures(measure_collector, std::move(input),
                                 type_factory, column_factory));

    // All required measure information has been collected. Perform the final
    // correctness check.
    GOOGLESQL_RETURN_IF_ERROR(measure_collector.Validate());

    // Step 3: Rewrite the measure column references.
    GOOGLESQL_ASSIGN_OR_RETURN(node, RewriteMeasureColumns(
                               std::move(node), measure_collector, any_value_fn,
                               function_call_builder, options.language(),
                               column_factory, type_factory));

    // Step 4: Remove ColumnRefs on parameter_list that are not accessed.
    //
    // This is needed because, when the measure definition expression does not
    // use any columns, e.g., m := 1, the rewritten expression does not need
    // to reference any columns in the struct closure column. As a result, the
    // corresponding struct ColumnRef on `parameter_list` is unused, causing
    // validator failures.
    GOOGLESQL_ASSIGN_OR_RETURN(node,
                     UnusedCorrelatedColumnPruner::PruneUnusedCorrelatedColumns(
                         std::move(node)));

    return node;
  }

  std::string Name() const override { return "MeasureTypeRewriter"; }
};

const Rewriter* GetMeasureTypeRewriter() {
  static const Rewriter* rewriter = new MeasureTypeRewriter();
  return rewriter;
}

}  // namespace googlesql
