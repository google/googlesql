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

#include "googlesql/analyzer/rewriters/measure_reference_rewrite_util.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/analyzer/rewriters/measure_collector.h"
#include "googlesql/analyzer/rewriters/measure_type_rewriter_util.h"
#include "googlesql/public/function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class AggedMeasureMarker : public ResolvedASTVisitor {
 public:
  explicit AggedMeasureMarker(MeasureCollector& measure_collector)
      : measure_collector_(measure_collector) {}

  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override {
    if (IsMeasureAggFunction(node)) {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn measure_col,
                       GetInvokedMeasureColumn(node));
      measure_collector_.MarkAgged(measure_col.type()->AsMeasure());
    }
    return DefaultVisit(node);
  }

 private:
  MeasureCollector& measure_collector_;
};

absl::Status MarkAggedMeasures(const ResolvedNode* resolved_ast,
                               MeasureCollector& measure_collector) {
  AggedMeasureMarker visitor(measure_collector);
  return resolved_ast->Accept(&visitor);
}

// Assumes `aggregate_fn` is the builtin function `AGG(MEASURE<T>) => T`.
static absl::StatusOr<const ResolvedColumnRef*> GetMeasureColumnRef(
    const ResolvedAggregateFunctionCall* aggregate_fn) {
  GOOGLESQL_RET_CHECK(aggregate_fn != nullptr);
  GOOGLESQL_RET_CHECK(aggregate_fn->argument_list().size() == 1);
  const ResolvedExpr* arg = aggregate_fn->argument_list()[0].get();
  GOOGLESQL_RET_CHECK(arg->Is<ResolvedColumnRef>());
  return arg->GetAs<ResolvedColumnRef>();
}

class MeasureColumnRewriter : public ResolvedASTRewriteVisitor {
 public:
  MeasureColumnRewriter(MeasureCollector& measure_collector,
                        const Function* any_value_fn,
                        FunctionCallBuilder& function_call_builder,
                        const LanguageOptions& language_options,
                        ColumnFactory& column_factory,
                        TypeFactory& type_factory)
      : measure_collector_(measure_collector),
        any_value_fn_(any_value_fn),
        function_call_builder_(function_call_builder),
        language_options_(language_options),
        column_factory_(column_factory),
        type_factory_(type_factory) {}

 protected:
  // Creates a new stack frame to hold constituent aggregates produced by
  // rewriting AGG(m) calls under this AggregateScan. Also collects
  // columns that are outputs of AGG(m) calls.
  absl::Status PreVisitResolvedAggregateScan(
      const ResolvedAggregateScan& node) override {
    constituent_aggregates_stack_.emplace_back();
    for (const auto& agg_col : node.aggregate_list()) {
      if (IsMeasureAggFunction(agg_col->expr())) {
        constituent_aggregates_stack_.back().agg_m_columns.insert(
            agg_col->column());
      }
    }
    return absl::OkStatus();
  }

  absl::Status PreVisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall& node) override {
    if (IsMeasureAggFunction(&node)) {
      // We do not support nested AGG calls currently.
      GOOGLESQL_RET_CHECK(!is_under_measure_agg_function_);
      is_under_measure_agg_function_ = true;
    }
    return absl::OkStatus();
  }

  // If node is an AGG(m) call, rewrites it into an expression over
  // constituent aggregates, collects constituent aggregates in
  // `constituent_aggregates_stack_`, and returns the rewritten expression.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateFunctionCall(
      std::unique_ptr<const ResolvedAggregateFunctionCall> node) override {
    if (!IsMeasureAggFunction(node.get())) {
      return node;
    }
    GOOGLESQL_RET_CHECK(is_under_measure_agg_function_);
    is_under_measure_agg_function_ = false;
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedColumnRef* measure_col_ref,
                     GetMeasureColumnRef(node.get()));
    const ResolvedColumn& measure_column = measure_col_ref->column();
    GOOGLESQL_RET_CHECK(measure_collector_.IsAgged(measure_column.type()->AsMeasure()));

    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn closure_column,
                     measure_collector_.GetClosureColumn(measure_column));
    GOOGLESQL_RET_CHECK(measure_column.type()->IsMeasureType());

    GOOGLESQL_ASSIGN_OR_RETURN(
        MeasureInfo measure_info,
        measure_collector_.GetMeasureInfo(measure_column.type()->AsMeasure()));
    GOOGLESQL_ASSIGN_OR_RETURN(
        RewriteMeasureExprResult result,
        RewriteMeasureExpr(measure_info.measure_expr, closure_column,
                           measure_info.row_identity_column_names,
                           measure_col_ref->is_correlated(), any_value_fn_,
                           function_call_builder_, language_options_,
                           column_factory_, type_factory_));
    for (auto& agg : result.constituent_aggregate_list) {
      constituent_aggregates_stack_.back().constituent_aggregate_list.push_back(
          std::move(agg));
    }
    return std::move(result.rewritten_measure_expr);
  }

  absl::StatusOr<ResolvedColumn> PostVisitResolvedColumn(
      const ResolvedColumn& column) override {
    if (!column.type()->IsMeasureType()) {
      return column;
    }
    if (!measure_collector_.IsAgged(column.type()->AsMeasure())) {
      return column;
    }
    if (is_under_measure_agg_function_) {
      // This is for AGG(m). No need to rewrite the column separately; the whole
      // AGG(m) will be rewritten directly. The logic here assumes that the
      // argument to the AGG function is always a column ref. This assumption is
      // currently enforced by UnsupportedQueryShapeFinder, but relaxing it in
      // the future will require changing the logic here.
      return column;
    }
    return measure_collector_.GetClosureColumn(column);
  }

  // We have to have this function instead of doing a PostVisitType() because
  // we don't replace non-AGG'ed measure columns with the struct closure
  // columns.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedColumnRef(
      std::unique_ptr<const ResolvedColumnRef> node) override {
    const Type* column_type = node->column().type();
    const Type* ref_type = node->type();
    if (is_under_measure_agg_function_ || column_type == ref_type) {
      return node;
    }
    // The measure column the ColumnRef points to is rewritten to the closure
    // column, so update the ColumnRef's type to match.
    GOOGLESQL_RET_CHECK(ref_type->IsMeasureType());
    GOOGLESQL_RET_CHECK(column_type->IsStruct());
    return ToBuilder(std::move(node)).set_type(column_type).Build();
  }

  // If AGG(m) calls are present under `node`, this function rewrites `node`
  // into a ProjectScan over an AggregateScan to capture the rewritten result
  // of AGG(m) by `PostVisitResolvedAggregateFunctionCall`.
  //
  // The overall algorithm is:
  //
  // If an AggregateScan contains AGG(m) in its aggregate list,
  // `PostVisitResolvedAggregateFunctionCall` rewrites AGG(m) into an
  // expression that computes AGG(m) from constituent aggregate functions
  // (e.g. SUM, COUNT), where the constituent aggregate function calls are
  // stored in `constituent_aggregates_stack_`.
  //
  // Then this function takes those constituent aggregate function calls and
  // adds them to `node`'s aggregate list. The rewritten AGG(m) expression is
  // added to the expr_list of the new ProjectScan layered on top of `node`.
  //
  // For example:
  //
  // AggregateScan
  // +-column_list=[agg1, agg2, key]
  // +-grouping_key_list=[key]
  // +-aggregate_list=
  // | +-agg1 := AGG(m),
  // | +-agg2 := COUNT(*)
  //
  // is rewritten to:
  //
  // ProjectScan
  // +-column_list=[agg1, agg2, key]
  // +-expr_list=
  // | +-agg1 := rewritten AGG(m) expression
  // +-input_scan=
  //   +-AggregateScan
  //     +-column_list=[constituent_agg1, constituent_agg2, agg2, key]
  //     +-grouping_key_list=[key]
  //     +-aggregate_list=
  //       +-constituent_agg1 := SUM(...)
  //       +-constituent_agg2 := COUNT(...)
  //       +-agg2 := COUNT(*)
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateScan(
      std::unique_ptr<const ResolvedAggregateScan> node) override {
    ConstituentAggregates agg_info =
        std::move(constituent_aggregates_stack_.back());
    constituent_aggregates_stack_.pop_back();

    // If `agg_info.agg_m_columns` is empty, it means no AGG(m) function calls
    // were found under this AggregateScan. No rewrite is needed.
    if (agg_info.agg_m_columns.empty()) {
      return node;
    }

    ResolvedColumnList original_column_list = node->column_list();
    // This will store AGG(m) expressions which are rewritten by
    // `PostVisitResolvedAggregateFunctionCall`. These expressions will become
    // computed columns in ProjectScan.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        computed_columns_for_project_scan;

    ResolvedAggregateScanBuilder agg_scan_builder = ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        original_aggregate_list = agg_scan_builder.release_aggregate_list();

    // `rewritten_agg_scan_aggregate_list` will store aggregate expressions for
    // the rewritten AggregateScan. It includes non-AGG(m) aggregates and
    // constituent aggregates for AGG(m).
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        rewritten_agg_scan_aggregate_list;

    // Go through `node`'s aggregate list. If an aggregate expression is
    // AGG(m), it has been rewritten to an expression over constituent
    // aggregates by `PostVisitResolvedAggregateFunctionCall`. Move this
    // expression to `computed_columns_for_project_scan` which will be
    // computed by the ProjectScan. Otherwise, it's a non-AGG(m) aggregate
    // expression, simply move it to `rewritten_agg_scan_aggregate_list`.
    for (auto& agg_col : original_aggregate_list) {
      if (agg_info.agg_m_columns.contains(agg_col->column())) {
        // This column corresponds to an AGG(m) function call. The column
        // expression holds the rewritten expression for AGG(m), which should
        // be computed in ProjectScan.
        GOOGLESQL_RET_CHECK(agg_col->Is<ResolvedComputedColumn>());
        computed_columns_for_project_scan.push_back(absl::WrapUnique(
            agg_col.release()->GetAs<ResolvedComputedColumn>()));
      } else {
        rewritten_agg_scan_aggregate_list.push_back(std::move(agg_col));
      }
    }

    // Build `rewritten_agg_scan_column_list` which is output column list of
    // the rewritten AggregateScan. It should contain all columns from
    // `original_column_list` except for AGG(m) columns, plus columns for
    // constituent aggregates.
    std::vector<ResolvedColumn> rewritten_agg_scan_column_list;
    for (const auto& col : original_column_list) {
      if (!agg_info.agg_m_columns.contains(col)) {
        rewritten_agg_scan_column_list.push_back(col);
      }
    }
    for (auto& constituent_agg : agg_info.constituent_aggregate_list) {
      rewritten_agg_scan_column_list.push_back(constituent_agg->column());
      rewritten_agg_scan_aggregate_list.push_back(std::move(constituent_agg));
    }

    // Build the rewritten AggregateScan with the new
    // `rewritten_agg_scan_column_list` and `rewritten_agg_scan_aggregate_list`.
    agg_scan_builder.set_aggregate_list(
        std::move(rewritten_agg_scan_aggregate_list));
    agg_scan_builder.set_column_list(std::move(rewritten_agg_scan_column_list));
    GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten_agg_scan,
                     std::move(agg_scan_builder).Build());

    // Build and return a ProjectScan on top of rewritten AggregateScan.
    return MakeResolvedProjectScan(original_column_list,
                                   std::move(computed_columns_for_project_scan),
                                   std::move(rewritten_agg_scan));
  }

 private:
  MeasureCollector& measure_collector_;
  const Function* any_value_fn_;
  FunctionCallBuilder& function_call_builder_;
  const LanguageOptions& language_options_;
  ColumnFactory& column_factory_;
  TypeFactory& type_factory_;

  // If true, we are currently visiting nodes under an AGG(MEASURE)
  // function.
  bool is_under_measure_agg_function_ = false;

  // Holds information about rewritten AGG(m) calls under an AggregateScan.
  struct ConstituentAggregates {
    // Constituent aggregates from rewritten AGG(m) calls.
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        constituent_aggregate_list;
    // Columns that are outputs of AGG(m) calls.
    absl::flat_hash_set<ResolvedColumn> agg_m_columns;
  };
  // A stack to hold ConstituentAggregates for nested AggregateScans.
  std::vector<ConstituentAggregates> constituent_aggregates_stack_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteMeasureColumns(
    std::unique_ptr<const ResolvedNode> resolved_ast,
    MeasureCollector& measure_collector, const Function* any_value_fn,
    FunctionCallBuilder& function_call_builder,
    const LanguageOptions& language_options, ColumnFactory& column_factory,
    TypeFactory& type_factory) {
  MeasureColumnRewriter rewriter(measure_collector, any_value_fn,
                                 function_call_builder, language_options,
                                 column_factory, type_factory);
  return rewriter.VisitAll(std::move(resolved_ast));
}

}  // namespace googlesql
