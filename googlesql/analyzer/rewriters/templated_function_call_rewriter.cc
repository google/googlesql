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

#include "googlesql/analyzer/rewriters/templated_function_call_rewriter.h"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/analyzer/rewriters/rewriter_relevance_checker.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/btree_set.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

template <typename T>
static absl::StatusOr<std::unique_ptr<const T>> DownCastUniquePtr(
    std::unique_ptr<const ResolvedNode> node) {
  return std::unique_ptr<const T>(node.release()->GetAs<T>());
}

using ApplyRewritesFn =
    std::function<absl::StatusOr<std::unique_ptr<const ResolvedNode>>(
        const AnalyzerOptions& analyzer_options,
        std::unique_ptr<const ResolvedNode>)>;

class TemplatedFunctionCallVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit TemplatedFunctionCallVisitor(const AnalyzerOptions& analyzer_options,
                                        ApplyRewritesFn apply_rewriters_fn)
      : analyzer_options_(analyzer_options),
        apply_rewriters_fn_(apply_rewriters_fn) {}

  template <typename T>
  absl::StatusOr<std::unique_ptr<const T>> CopyAndRewriteNode(
      const ResolvedNode& node, googlesql_base::SequenceNumber& sequence_number,
      ColumnReplacementMap& column_map) {
    // If the SequenceNumber is nullptr in the AnalyzerOptions that are supplied
    // when analyzing a SQL statement, then a SequenceNumber is created by the
    // resolver to create unique column ids for the resolved statement.
    //
    // When a templated function call is encountered, a new resolver is created
    // to resolve the body of the templated function. In this case, the new
    // resolver will also create a new SequenceNumber if one is not provided in
    // the AnalyzerOptions.
    //
    // This causes a problem when executing this rewriter because the
    // `analyzer_options_` use a SequenceNumber that reflects the max column id
    // of the resolved statement, which is inconsistent with the SequenceNumber
    // used to resolve the body of the templated function.
    //
    // To avoid column id conflicts, we create a new SequenceNumber, remap the
    // columns ids in the function body to this sequence, and use this sequence
    // to perform the rewrites so that any injected columns have unique ids
    // within the body of the templated function call.
    AnalyzerOptions options_for_rewrite(analyzer_options_);
    options_for_rewrite.set_column_id_sequence_number(&sequence_number);
    GOOGLESQL_RET_CHECK(options_for_rewrite.id_string_pool() != nullptr);

    ColumnFactory column_factory(/*max_seen_col_id=*/0,
                                 *options_for_rewrite.id_string_pool(),
                                 sequence_number);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedNode> node_copy,
        CopyResolvedASTAndRemapColumns(node, column_factory, column_map));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedNode> rewritten_node,
        apply_rewriters_fn_(options_for_rewrite, std::move(node_copy)));
    // Apply the visitor to the rewritten node to rewrite nested templated
    // function calls.
    GOOGLESQL_ASSIGN_OR_RETURN(rewritten_node, this->VisitAll(std::move(rewritten_node)));
    return DownCastUniquePtr<T>(std::move(rewritten_node));
  }

  absl::StatusOr<std::shared_ptr<TemplatedSQLFunctionCall>>
  CopyAndRewriteTemplatedSQLFunctionCall(
      const absl::btree_set<ResolvedASTRewrite>& applicable_rewrites,
      const TemplatedSQLFunctionCall& function_call) {
    // Share a sequence number and column map between related expressions
    // (the scalar and aggregate expressions) on aggregate function calls.
    googlesql_base::SequenceNumber sequence_number;
    ColumnReplacementMap column_map;
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> rewritten_function_expr,
        CopyAndRewriteNode<ResolvedExpr>(*function_call.expr(), sequence_number,
                                         column_map));

    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        rewritten_aggregate_expression_list;
    if (!function_call.aggregate_expression_list().empty()) {
      rewritten_aggregate_expression_list.reserve(
          function_call.aggregate_expression_list().size());
      for (const std::unique_ptr<const ResolvedComputedColumn>&
               aggregate_expression :
           function_call.aggregate_expression_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<const ResolvedComputedColumn>
                rewritten_aggregate_expression,
            CopyAndRewriteNode<ResolvedComputedColumn>(
                *aggregate_expression, sequence_number, column_map));
        rewritten_aggregate_expression_list.push_back(
            std::move(rewritten_aggregate_expression));
      }
    }
    return std::make_shared<TemplatedSQLFunctionCall>(
        std::move(rewritten_function_expr),
        std::move(rewritten_aggregate_expression_list));
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionCall(
      std::unique_ptr<const ResolvedFunctionCall> function_call) override {
    if (function_call->function_call_info() == nullptr ||
        !function_call->function_call_info()->Is<TemplatedSQLFunctionCall>()) {
      return std::move(function_call);
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        absl::btree_set<ResolvedASTRewrite> applicable_rewrites,
        FindRelevantRewriters(function_call.get(),
                              /*check_templated_function_calls=*/true));

    if (applicable_rewrites.empty()) {
      return std::move(function_call);
    }

    TemplatedSQLFunctionCall* templated_function_call =
        function_call->function_call_info()->GetAs<TemplatedSQLFunctionCall>();

    GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<TemplatedSQLFunctionCall> rewritten_call,
                     CopyAndRewriteTemplatedSQLFunctionCall(
                         applicable_rewrites, *templated_function_call));

    return ToBuilder(std::move(function_call))
        .set_function_call_info(rewritten_call)
        .Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateFunctionCall(
      std::unique_ptr<const ResolvedAggregateFunctionCall> function_call)
      override {
    if (function_call->function_call_info() == nullptr ||
        !function_call->function_call_info()->Is<TemplatedSQLFunctionCall>()) {
      return std::move(function_call);
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        absl::btree_set<ResolvedASTRewrite> applicable_rewrites,
        FindRelevantRewriters(function_call.get(),
                              /*check_templated_function_calls=*/true));

    if (applicable_rewrites.empty()) {
      return std::move(function_call);
    }

    TemplatedSQLFunctionCall* templated_function_call =
        function_call->function_call_info()->GetAs<TemplatedSQLFunctionCall>();

    GOOGLESQL_ASSIGN_OR_RETURN(std::shared_ptr<TemplatedSQLFunctionCall> rewritten_call,
                     CopyAndRewriteTemplatedSQLFunctionCall(
                         applicable_rewrites, *templated_function_call));

    return ToBuilder(std::move(function_call))
        .set_function_call_info(rewritten_call)
        .Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedTVFScan(
      std::unique_ptr<const ResolvedTVFScan> tvf_scan) override {
    if (tvf_scan->signature() == nullptr ||
        !tvf_scan->signature()->Is<TemplatedSQLTVFSignature>()) {
      return std::move(tvf_scan);
    }
    auto* templated_tvf_signature =
        tvf_scan->signature()->GetAs<TemplatedSQLTVFSignature>();

    GOOGLESQL_ASSIGN_OR_RETURN(absl::btree_set<ResolvedASTRewrite> applicable_rewrites,
                     FindRelevantRewriters(
                         templated_tvf_signature->resolved_templated_query(),
                         /*check_templated_function_calls=*/true));

    if (applicable_rewrites.empty()) {
      return std::move(tvf_scan);
    }
    googlesql_base::SequenceNumber sequence_number;
    ColumnReplacementMap column_map;
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedQueryStmt> rewritten_query,
                     CopyAndRewriteNode<ResolvedQueryStmt>(
                         *templated_tvf_signature->resolved_templated_query(),
                         sequence_number, column_map));

    std::shared_ptr<TemplatedSQLTVFSignature> rewritten_signature =
        templated_tvf_signature->CopyWithoutResolvedTemplatedQuery();
    rewritten_signature->set_resolved_templated_query(
        std::move(rewritten_query));

    return ToBuilder(std::move(tvf_scan))
        .set_signature(rewritten_signature)
        .Build();
  }

 private:
  const AnalyzerOptions& analyzer_options_;
  ApplyRewritesFn apply_rewriters_fn_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RewriteTemplatedFunctionCalls(
    const AnalyzerOptions& analyzer_options,
    std::function<absl::StatusOr<std::unique_ptr<const ResolvedNode>>(
        const AnalyzerOptions& analyzer_options,
        std::unique_ptr<const ResolvedNode>)>
        rewriters_func,
    std::unique_ptr<const ResolvedNode> input) {
  TemplatedFunctionCallVisitor visitor(analyzer_options, rewriters_func);
  return visitor.VisitAll(std::move(input));
}

}  // namespace googlesql
