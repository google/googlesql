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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/substitute.h"
#include "googlesql/common/aggregate_null_handling.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

constexpr char kPivot[] = "$pivot";
constexpr char kPivotExprArg[] = "$pivot_expr_arg";
constexpr char kPivotAggResult[] = "$pivot_agg_result";

class PivotRewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  explicit PivotRewriterVisitor(Catalog* catalog, TypeFactory* type_factory,
                                ColumnFactory* column_factory,
                                const AnalyzerOptions* analyzer_options)
      : analyzer_options_(*analyzer_options),
        catalog_(catalog),
        type_factory_(type_factory),
        column_factory_(column_factory),
        fn_builder_(*analyzer_options, *catalog, *type_factory) {}

  PivotRewriterVisitor(const PivotRewriterVisitor&) = delete;
  PivotRewriterVisitor& operator=(const PivotRewriterVisitor&) = delete;

 private:
  // Generates SQL to check if pivot column is not distinct from the pivot value
  // working around engines that don't natively support the syntax.
  //
  // TODO: Remove this and always use IS NOT DISTINCT FROM.
  std::string PivotColumnNotDistinctSql() {
    if (analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_IS_DISTINCT)) {
      return "pivot_column IS NOT DISTINCT FROM pivot_value";
    }
    return "EXISTS(SELECT pivot_column INTERSECT ALL SELECT pivot_value)";
  }

  absl::Status VisitResolvedPivotScan(const ResolvedPivotScan* node) override;

  // Returns an aggregate function call representing a single pivot expression
  // over a subset of input where <pivot_value_expr> matches <pivot_column>.
  // If <agg_fn_arg_column> is present, the argument to the aggregate function
  // will be a column ref to this column, rather than using the subtree inside
  // of <pivot_expr> itself; this ensures that non-deterministic expressions
  // evaluate the same for each row, even across different pivot columns.
  //
  // For example, in this query:
  //   SELECT * FROM t PIVOT(SUM(x + 1) s, COUNT(*) c FOR y + 1 IN (0, 1));
  //
  // MakeAggregateExpr() will be called four times. The first two times:
  // - <pivot_expr> will be SUM(x)
  // - <pivot_value_expr> will be 0 (1st time) or 1 (2nd time)
  // - <pivot_column> will hold the result of (y + 1)
  // - <agg_fn_arg_column> will hold the result of x + 1
  //
  // And, the next two times:
  // - <pivot_expr> will be COUNT(*)
  // - <pivot_value_expr> will be 0 (3rd time) or 1 (4th time)
  // - <pivot_column> will hold the result of (y + 1)
  // - <agg_fn_arg_column> will be absl::nullopt (since there's no argument).
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeAggregateExpr(
      const ResolvedExpr* pivot_expr, const ResolvedExpr* pivot_value_expr,
      const ResolvedColumn& pivot_column,
      absl::Span<const ResolvedColumn> agg_fn_arg_columns);

  // Wraps the input scan of a pivot with a project scan, adding computed
  // columns holding the result of the FOR expression, plus each argument to the
  // aggregate function in each PIVOT expression. The resultant scan will be
  // used as the input to the AggregateScan used to represent the rewritten
  // PivotScan.
  //
  // <pivot_expr_arg_columns> is an output parameter, which is modified to hold
  // the argument columns to each pivot expression.
  // On output, the i'th element is the single argument to the i'th pivot
  // expression. If a given pivot expression does not take an argument (e.g. it
  // is "COUNT(*)"), the ResolvedColumn is blank.
  absl::StatusOr<std::unique_ptr<ResolvedScan>> AddExprColumnsToPivotInput(
      const ResolvedPivotScan* pivot_scan,
      const ResolvedColumn& for_expr_column,
      std::vector<std::vector<ResolvedColumn>>& pivot_expr_arg_columns);

  // Verifies that <call> is supported by this rewriter implementation.
  //
  // A aggregate function call is supported as a pivot expression if all of
  // the following conditions apply:
  //  - Function call has exactly one argument
  //  - Function is known to ignore all input rows where the argument is NULL
  //  - The HAVING MIN and HAVING MAX clauses are not present.
  //
  // There is one exception to the above; COUNT(*) is supported, in spite of
  // having zero arguments instead of one.
  absl::Status VerifyAggregateFunctionIsSupported(
      const ResolvedAggregateFunctionCall* call);

  // Implements MakeAggregateExpr() for the case where the pivot expression is
  // a call to the COUNT(*) function. COUNT(*) uses a different implementation
  // strategy from other functions, for which the COUNT(*) call is translated to
  // COUNTIF().
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> RewriteCountStarPivotExpr(
      ResolvedAggregateFunctionCall* call,
      std::unique_ptr<ResolvedExpr> pivot_value_expr,
      const ResolvedColumn& pivot_column);

  // Implements MakeAggregateExpr() for the case where the pivot expression is
  // a call to the ANY_VALUE() function. ANY_VALUE() uses a different
  // implementation strategy from other functions.
  //
  // When <x> is not an array type, ANY_VALUE(x) is translated to:
  //    ARRAY_AGG(x IGNORE NULLS LIMIT 1)[OFFSET(0)]
  //
  // When <x> is an array type, ANY_VALUE(x) is translated to the following,
  // instead, to avoid arrays of arrays:
  //   ARRAY_AGG(IF(x IS NULL, NULL, STRUCT(x AS _x)) IGNORE NULLS LIMIT 1)
  //     [OFFSET(0)]._x
  //
  // As the expr returned by RewriteAnyValuePivotExpr() is used as the aggregate
  // in a ResolvedAggregateScan, this expr contains just the call to
  // ARRAY_AGG(). Elsewhere, the entire ResolvedAggregateScan is then wrapped
  // in a ResolvedProjectScan, with the project scan (see
  // AddPostAggregationLogic()) containing computed columns for the [OFFSET(0)]
  // part.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> RewriteAnyValuePivotExpr(
      ResolvedAggregateFunctionCall* call,
      std::vector<std::unique_ptr<const ResolvedExpr>> agg_fn_args,
      const ResolvedColumn& pivot_column);

  // Wraps the AggregateScan with a ProjectScan that represents post-aggregation
  // logic as computed columns. This is needed when ANY_VALUE() is present,
  // so that the array value produced by the ARRAY_AGG() function gets
  // unwrapped.
  absl::StatusOr<std::unique_ptr<ResolvedScan>>
  AddPostAggregationLogicForAnyValue(
      const ResolvedColumnList& final_column_list,
      std::unique_ptr<ResolvedAggregateScan> aggregate_scan,
      const absl::flat_hash_map<ResolvedColumn, ResolvedColumn>&
          output_computed_columns_to_aggregate_column);

  const AnalyzerOptions& analyzer_options_;
  Catalog* const catalog_;
  TypeFactory* type_factory_;
  ColumnFactory* const column_factory_;
  FunctionCallBuilder fn_builder_;
};

absl::StatusOr<std::unique_ptr<ResolvedScan>>
PivotRewriterVisitor::AddExprColumnsToPivotInput(
    const ResolvedPivotScan* pivot_scan, const ResolvedColumn& for_expr_column,
    std::vector<std::vector<ResolvedColumn>>& pivot_expr_arg_columns) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> input_scan_copy,
                   ProcessNode(pivot_scan->input_scan()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> for_expr_copy,
                   ProcessNode(pivot_scan->for_expr()));
  if (CollationAnnotation::ExistsIn(
          pivot_scan->for_expr()->type_annotation_map())) {
    // TODO: support collation on FOR expression.
    return MakeUnimplementedErrorAtNode(pivot_scan->for_expr())
           << "Collation is not supported in a PIVOT clause yet";
  }

  std::vector<ResolvedColumn> column_list(
      pivot_scan->input_scan()->column_list().begin(),
      pivot_scan->input_scan()->column_list().end());
  column_list.push_back(for_expr_column);

  std::vector<std::unique_ptr<ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(for_expr_column, std::move(for_expr_copy)));

  for (const auto& pivot_expr : pivot_scan->pivot_expr_list()) {
    const ResolvedAggregateFunctionCall* call =
        pivot_expr->GetAs<ResolvedAggregateFunctionCall>();
    if (call->collation_list_size() > 0) {
      // TODO: support collation on aggregation functions
      return MakeUnimplementedErrorAtNode(call)
             << "Collation is not supported in a PIVOT clause yet";
    }
    GOOGLESQL_RETURN_IF_ERROR(VerifyAggregateFunctionIsSupported(call));
    pivot_expr_arg_columns.emplace_back();

    for (int i = 0; i < call->argument_list().size(); i++) {
      const auto& arg = call->argument_list()[i];
      if (CollationAnnotation::ExistsIn(arg->type_annotation_map())) {
        // TODO: support collation for arguments of aggregate
        // functions inside PIVOT clause.
        return MakeUnimplementedErrorAtNode(call) << absl::Substitute(
                   "Collation $0 is not supported on argument $1 of "
                   "aggregate function in a PIVOT clause",
                   arg->type_annotation_map()->DebugString(
                       CollationAnnotation::GetId()),
                   (i + 1));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(bool arg_is_constant_expr,
                       IsConstantExpression(arg.get()));
      if (arg_is_constant_expr) {
        // Constant expressions are ok to clone, rather than project. In most
        // cases, this doesn't matter, but some functions take constant
        // arguments that can't be read from a projected column (the delimiter
        // argument of STRING_AGG() is one such example). For simplicity, we
        // take the clone approach for all constant expressions, whether needed
        // or not.
        //
        // In this case, just push a dummy column into the projected column list
        // for the current pivot expr. We'll copy the argument later, where it
        // is used.
        pivot_expr_arg_columns.back().emplace_back();
        continue;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> argument_copy,
                       ProcessNode(arg.get()));

      ResolvedColumn projected_arg_col = column_factory_->MakeCol(
          kPivot, kPivotExprArg, argument_copy->type());
      expr_list.push_back(MakeResolvedComputedColumn(projected_arg_col,
                                                     std::move(argument_copy)));
      pivot_expr_arg_columns.back().push_back(projected_arg_col);
      column_list.push_back(projected_arg_col);
    }
  }

  return MakeResolvedProjectScan(column_list, std::move(expr_list),
                                 std::move(input_scan_copy));
}

absl::Status PivotRewriterVisitor::VisitResolvedPivotScan(
    const ResolvedPivotScan* node) {
  ResolvedColumn pivot_col = column_factory_->MakeCol("$pivot", "$pivot_value",
                                                      node->for_expr()->type());

  std::vector<std::vector<ResolvedColumn>> agg_fn_argument_columns;

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedScan> input_with_pivot_column,
      AddExprColumnsToPivotInput(node, pivot_col, agg_fn_argument_columns));

  std::vector<std::unique_ptr<ResolvedComputedColumn>> aggregate_list;
  std::vector<ResolvedColumn> aggregate_scan_column_list;

  // Map associating each output column which requires post-aggregation
  // computation with the result of its corresponding aggregation expression.
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn>
      output_computed_columns_to_aggregate_column;

  // Insert GROUP BY columns into the aggregate scan's column list, before
  // the pivot columns, to match the column order of the PIVOT clause.
  std::vector<std::unique_ptr<ResolvedComputedColumn>> group_by_list;
  group_by_list.reserve(node->group_by_list_size());
  for (const auto& group_by : node->group_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedComputedColumn> group_by_item,
                     ProcessNode(group_by.get()));
    group_by_list.push_back(std::move(group_by_item));
    aggregate_scan_column_list.push_back(group_by->column());
  }

  // Insert pivot columns into the aggregate scan's column list.
  for (const auto& pivot_column : node->pivot_column_list()) {
    const ResolvedExpr* pivot_expr =
        node->pivot_expr_list()[pivot_column->pivot_expr_index()].get();
    const ResolvedExpr* pivot_value_expr =
        node->pivot_value_list()[pivot_column->pivot_value_index()].get();

    ABSL_CHECK_LE(pivot_column->pivot_expr_index(), agg_fn_argument_columns.size());
    const std::vector<ResolvedColumn>& agg_fn_arg_columns =
        agg_fn_argument_columns[pivot_column->pivot_expr_index()];

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> agg_expr,
                     MakeAggregateExpr(pivot_expr, pivot_value_expr, pivot_col,
                                       agg_fn_arg_columns));

    ResolvedColumn aggregate_column;
    if (pivot_expr->GetAs<ResolvedAggregateFunctionCall>()
            ->signature()
            .context_id() == FN_ANY_VALUE) {
      // The result of the aggregation wraps the value in a single-element
      // array, of which the element needs to be extracted post-aggregation.
      //
      // Create a new column to hold this array.
      aggregate_column =
          column_factory_->MakeCol(kPivot, kPivotAggResult, agg_expr->type());
      GOOGLESQL_RET_CHECK(agg_expr->type()->IsArray()) << agg_expr->type()->DebugString();

      output_computed_columns_to_aggregate_column[pivot_column->column()] =
          aggregate_column;
    } else {
      // This column does not require any post-aggregation computation, so we
      // can simply use the final column as the aggregate column.
      GOOGLESQL_RET_CHECK(agg_expr->type()->Equals(pivot_column->column().type()));
      aggregate_column = pivot_column->column();
    }

    std::unique_ptr<ResolvedComputedColumn> agg_computed_column =
        MakeResolvedComputedColumn(aggregate_column, std::move(agg_expr));
    aggregate_list.push_back(std::move(agg_computed_column));
    aggregate_scan_column_list.push_back(aggregate_column);
  }

  std::unique_ptr<ResolvedAggregateScan> aggregate_result =
      MakeResolvedAggregateScan(
          aggregate_scan_column_list, std::move(input_with_pivot_column),
          std::move(group_by_list), std::move(aggregate_list),
          /*grouping_set_list=*/{},
          /*rollup_column_list=*/{},
          /*grouping_call_list=*/{});

  std::unique_ptr<ResolvedScan> result;
  if (!output_computed_columns_to_aggregate_column.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(result,
                     AddPostAggregationLogicForAnyValue(
                         node->column_list(), std::move(aggregate_result),
                         output_computed_columns_to_aggregate_column));
  } else {
    result = std::move(aggregate_result);
  }

  PushNodeToStack(std::move(result));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
PivotRewriterVisitor::AddPostAggregationLogicForAnyValue(
    const ResolvedColumnList& final_column_list,
    std::unique_ptr<ResolvedAggregateScan> aggregate_scan,
    const absl::flat_hash_map<ResolvedColumn, ResolvedColumn>&
        output_computed_columns_to_aggregate_column) {
  // Wrap the AggregateScan in a ProjectScan which unwraps the arrays
  // generated by the ARRAY_AGG() that replaced ANY_VALUE() to produce the
  // final result.
  std::vector<std::unique_ptr<ResolvedComputedColumn>> project_scan_expr_list;

  for (const ResolvedColumn& column : final_column_list) {
    auto it = output_computed_columns_to_aggregate_column.find(column);
    if (it == output_computed_columns_to_aggregate_column.end()) {
      // No post-aggregate computation for this column required.
      continue;
    }
    const ResolvedColumn& aggregate_column = it->second;

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> offset_zero_expr,
        AnalyzeSubstitute(
            analyzer_options_, *catalog_, *type_factory_,
            "agg_result[OFFSET(0)]",
            {{"agg_result",
              MakeResolvedColumnRef(aggregate_column.type(), aggregate_column,
                                    /*is_correlated=*/false)
                  .get()}}));
    if (column.type()->IsArray()) {
      // To avoid arrays of arrays, the input to ANY_VALUE() is wrapped in
      // a struct, pre-aggregation, so we need to extract out the field, here,
      // post-aggregation, so that the final result does not contain the
      // wrapper struct.
      GOOGLESQL_RET_CHECK(offset_zero_expr->type()->IsStruct());
      GOOGLESQL_RET_CHECK(offset_zero_expr->type()->AsStruct()->field(0).type->Equals(
          column.type()));
      project_scan_expr_list.push_back(MakeResolvedComputedColumn(
          column,
          MakeResolvedGetStructField(column.type(), std::move(offset_zero_expr),
                                     /*field_idx=*/0)));
    } else {
      project_scan_expr_list.push_back(
          MakeResolvedComputedColumn(column, std::move(offset_zero_expr)));
    }
  }

  return MakeResolvedProjectScan(final_column_list,
                                 std::move(project_scan_expr_list),
                                 std::move(aggregate_scan));
}

absl::Status PivotRewriterVisitor::VerifyAggregateFunctionIsSupported(
    const ResolvedAggregateFunctionCall* call) {
  if (call->having_modifier() != nullptr) {
    // Support for HAVING MIN/MAX through the rewriter requires additional work.
    // We would need to value we're taking the MIN/MAX of to exclude rows where
    // the pivot column does not match the pivot value.
    //
    // As HAVING MIN/MAX is not a commonly-used feature, this is low priority.
    return MakeUnimplementedErrorAtNode(call)
           << "Use of HAVING inside an aggregate function used as a PIVOT "
              "expression is not supported";
  }

  if (!call->group_by_list().empty()) {
    // TODO: Add support for multi-level aggregate functions in
    // PIVOT.
    return MakeUnimplementedErrorAtNode(call)
           << "Multi-level aggregate functions currently cannot be used as "
              "PIVOT expressions.";
  }
  // TODO: b/382275859 - Add support for WHERE and HAVING filters in aggregate
  // functions used as PIVOT expressions.
  if (call->where_expr() != nullptr) {
    return MakeUnimplementedErrorAtNode(call)
           << "Use of WHERE filter inside an aggregate function used as a "
              "PIVOT expression is not supported";
  }
  if (call->having_expr() != nullptr) {
    return MakeUnimplementedErrorAtNode(call)
           << "Use of HAVING filter inside an aggregate function used as a "
              "PIVOT expression is not supported";
  }

  if (call->signature().context_id() == FN_COUNT_STAR ||
      (call->signature().context_id() == FN_ANY_VALUE &&
       analyzer_options_.language().LanguageFeatureEnabled(
           FEATURE_NULL_HANDLING_MODIFIER_IN_AGGREGATE))) {
    // COUNT(*) and ANY_VALUE() have special implementation and is supported.
    // Note: The rewriter converts ANY_VALUE() into ARRAY_AGG(... IGNORE NULLS),
    // so support for rewriting ANY_VALUE() is conditioned upon IGNORE NULLS
    // support.
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(call->signature().IsConcrete());
  if (call->signature().NumConcreteArguments() == 0) {
    // Zero-argument signatures other than COUNT(*) are not supported.
    return MakeUnimplementedErrorAtNode(call)
           << "Use of aggregate function " << call->function()->SQLName()
           << " as PIVOT expression is not supported";
  }

  switch (call->null_handling_modifier()) {
    case ResolvedAggregateFunctionCall::IGNORE_NULLS:
      // Function call is explicitly annotated as ignoring nulls.
      return absl::OkStatus();
    case ResolvedAggregateFunctionCall::RESPECT_NULLS:
      return MakeUnimplementedErrorAtNode(call)
             << "Use of RESPECT NULLS in aggregate function used as a PIVOT "
                "expression is not supported";
    case ResolvedAggregateFunctionCall::DEFAULT_NULL_HANDLING:
      if (IgnoresNullArguments(call)) {
        return absl::OkStatus();
      }
      // The function call cannot be supported because it does/might respect
      // NULL inputs, which would break our rewrite strategy of replacing the
      // input argument with NULL when the pivot value does not match. Provide a
      // suitable error message indicating whether the function could be
      // supported if IGNORE NULLS were added.
      if (call->function()
              ->function_options()
              .supports_null_handling_modifier &&
          analyzer_options_.language().LanguageFeatureEnabled(
              FEATURE_NULL_HANDLING_MODIFIER_IN_AGGREGATE)) {
        return MakeUnimplementedErrorAtNode(call)
               << "Use of aggregate function " << call->function()->SQLName()
               << " as PIVOT expression is not supported unless IGNORE "
                  "NULLS is specified";
      }
      return MakeUnimplementedErrorAtNode(call)
             << "Use of aggregate function " << call->function()->SQLName()
             << " as PIVOT expression is not supported";
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
PivotRewriterVisitor::RewriteAnyValuePivotExpr(
    ResolvedAggregateFunctionCall* call,
    std::vector<std::unique_ptr<const ResolvedExpr>> agg_fn_args,
    const ResolvedColumn& pivot_column) {
  GOOGLESQL_RET_CHECK_EQ(agg_fn_args.size(), 1);

  const ResolvedExpr* arg = agg_fn_args[0].get();
  // Convert ANY_VALUE(x) to ARRAY_AGG(x IGNORE NULLS LIMIT 1).
  // This will "prefer" an element which is not null, but still satisfies the
  // contract of ANY_VALUE().
  //
  // The extraction of the underlying value from the array will happen
  // post-aggregation, by wrapping the AggregateScan with a ProjectScan.
  const Function* array_agg_fn;
  GOOGLESQL_RET_CHECK_OK(catalog_->FindFunction({"array_agg"}, &array_agg_fn,
                                      analyzer_options_.find_options()));
  GOOGLESQL_RET_CHECK(array_agg_fn->IsGoogleSQLBuiltin());
  const ArrayType* array_type;
  std::vector<std::unique_ptr<const ResolvedExpr>> array_agg_args;
  if (arg->type()->IsArray()) {
    // Need to wrap the ANY_VALUE() argument in a struct before passing it to
    // ARRAY_AGG(), to work around no GoogleSQL support for arrays of arrays.
    // The struct will be unpacked post-aggregation.
    const StructType* struct_type;
    GOOGLESQL_RETURN_IF_ERROR(
        type_factory_->MakeStructType({{"arg", arg->type()}}, &struct_type));
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(struct_type, &array_type));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> array_agg_arg,
        AnalyzeSubstitute(analyzer_options_, *catalog_, *type_factory_,
                          "IF(arg IS NULL, NULL, STRUCT(arg))", {{"arg", arg}}),
        _.With(ExpectAnalyzeSubstituteSuccess));
    array_agg_args.push_back(std::move(array_agg_arg));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(arg->type(), &array_type));
    array_agg_args = std::move(agg_fn_args);
  }
  FunctionSignature array_agg_sig(
      {array_type, 1}, {{array_type->element_type(), 1}}, FN_ARRAY_AGG);

  return MakeResolvedAggregateFunctionCall(
      array_type, array_agg_fn, array_agg_sig, std::move(array_agg_args), {},
      call->error_mode(), call->distinct(),
      ResolvedNonScalarFunctionCallBaseEnums::IGNORE_NULLS,
      /*where_expr=*/nullptr, call->release_having_modifier(),
      call->release_order_by_item_list(),
      /*limit=*/MakeResolvedLiteral(Value::Int64(1)),
      call->function_call_info(),
      /*group_by_list=*/{},
      /*group_by_aggregate_list=*/{},
      /*having_expr=*/nullptr);
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
PivotRewriterVisitor::RewriteCountStarPivotExpr(
    ResolvedAggregateFunctionCall* call,
    std::unique_ptr<ResolvedExpr> pivot_value_expr,
    const ResolvedColumn& pivot_column) {
  // Replace
  //  COUNT(*)
  // with
  //  COUNTIF(<pivot_column> IS NOT DISTINCT FROM <pivot value expr>)
  //
  std::unique_ptr<ResolvedColumnRef> pivot_column_ref = MakeResolvedColumnRef(
      pivot_column.type(), pivot_column, /*is_correlated=*/false);
  std::unique_ptr<const ResolvedExpr> countif_arg;
  if (analyzer_options_.language().LanguageFeatureEnabled(
          FEATURE_IS_DISTINCT) &&
      analyzer_options_.language().LanguageFeatureEnabled(
          FEATURE_SIMPLIFY_PIVOT_REWRITE)) {
    GOOGLESQL_ASSIGN_OR_RETURN(countif_arg, fn_builder_.IsNotDistinctFrom(
                                      std::move(pivot_column_ref),
                                      std::move(pivot_value_expr)));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        countif_arg,
        AnalyzeSubstitute(analyzer_options_, *catalog_, *type_factory_,
                          PivotColumnNotDistinctSql(),
                          {{"pivot_column", pivot_column_ref.get()},
                           {"pivot_value", pivot_value_expr.get()}}),
        _.With(ExpectAnalyzeSubstituteSuccess));
  }
  GOOGLESQL_RET_CHECK(countif_arg != nullptr);

  std::vector<std::unique_ptr<const ResolvedExpr>> countif_args;
  countif_args.push_back(std::move(countif_arg));

  const Function* countif_fn;
  GOOGLESQL_RET_CHECK_OK(catalog_->FindFunction({"countif"}, &countif_fn,
                                      analyzer_options_.find_options()));
  GOOGLESQL_RET_CHECK(countif_fn->IsGoogleSQLBuiltin());
  FunctionArgumentType int64_arg = FunctionArgumentType(types::Int64Type(), 1);
  FunctionArgumentType bool_arg = FunctionArgumentType(types::BoolType(), 1);
  FunctionSignature countif_sig(int64_arg, {bool_arg}, FN_COUNTIF);

  return MakeResolvedAggregateFunctionCall(
      types::Int64Type(), countif_fn, countif_sig, std::move(countif_args), {},
      call->error_mode(), call->distinct(), call->null_handling_modifier(),
      /*where_expr=*/nullptr, call->release_having_modifier(),
      call->release_order_by_item_list(), call->release_limit(),
      call->function_call_info(),
      /*group_by_list=*/{},
      /*group_by_aggregate_list=*/{},
      /*having_expr=*/nullptr);
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
PivotRewriterVisitor::MakeAggregateExpr(
    const ResolvedExpr* pivot_expr, const ResolvedExpr* pivot_value_expr,
    const ResolvedColumn& pivot_column,
    absl::Span<const ResolvedColumn> agg_fn_arg_columns) {
  // This condition guaranteed by the resolver and this check
  // really belongs in the validator; however, the validator currently has no
  // way to call IsConstantExpression() without creating a circular build
  // dependency, so adding a check here, just to make sure.
  // TODO: Refactor the code so that this check and be moved to the
  // validator, where it belongs.
  GOOGLESQL_ASSIGN_OR_RETURN(bool pivot_value_expr_is_constant_expr,
                   IsConstantExpression(pivot_value_expr));
  GOOGLESQL_RET_CHECK(pivot_value_expr_is_constant_expr);

  GOOGLESQL_RET_CHECK_EQ(pivot_expr->node_kind(), RESOLVED_AGGREGATE_FUNCTION_CALL)
      << "Should have function call; this should have been checked upstream";
  const ResolvedAggregateFunctionCall* call =
      pivot_expr->GetAs<ResolvedAggregateFunctionCall>();
  GOOGLESQL_RETURN_IF_ERROR(VerifyAggregateFunctionIsSupported(call));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> pivot_expr_copy,
                   ProcessNode(pivot_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> pivot_value_expr_copy,
                   ProcessNode(pivot_value_expr));

  ResolvedAggregateFunctionCall* call_copy =
      pivot_expr_copy->GetAs<ResolvedAggregateFunctionCall>();

  std::vector<std::string> fn_name_path = call->function()->FunctionNamePath();
  if (call->signature().context_id() == FN_COUNT_STAR) {
    return RewriteCountStarPivotExpr(
        call_copy, std::move(pivot_value_expr_copy), pivot_column);
  }

  // General case for remaining aggregate functions.
  std::unique_ptr<ResolvedColumnRef> pivot_column_ref = MakeResolvedColumnRef(
      pivot_column.type(), pivot_column, /*is_correlated=*/false);
  std::vector<std::unique_ptr<const ResolvedExpr>> agg_fn_args;

  // Because "ignores nulls" behavior means skipping rows when *any* input
  // argument is NULL, we only need to add the IF clause to check the pivot
  // value on the first argument. The rest can just be used directly.
  //
  // (For the one null-respecting function that gets here, ANY_VALUE(), this is
  // also not a problem because ANY_VALUE() accepts only one argument).
  if (!agg_fn_arg_columns.empty()) {
    std::unique_ptr<ResolvedExpr> orig_arg;
    if (agg_fn_arg_columns[0].IsInitialized()) {
      orig_arg = MakeResolvedColumnRef(agg_fn_arg_columns[0].type(),
                                       agg_fn_arg_columns[0],
                                       /*is_correlated=*/false);
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(orig_arg, ProcessNode(call->argument_list(0)));
    }

    std::unique_ptr<ResolvedExpr> agg_fn_arg;
    if (analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_IS_DISTINCT) &&
        analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_SIMPLIFY_PIVOT_REWRITE)) {
      // Build the expression
      //   IF(pivot_column IS NOT DISTINCT FROM pivot_value, orig_arg, NULL)
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedExpr> pivot_column_not_distinct_expr,
          fn_builder_.IsNotDistinctFrom(std::move(pivot_column_ref),
                                        std::move(pivot_value_expr_copy)));
      std::unique_ptr<ResolvedLiteral> null_literal =
          MakeResolvedLiteral(Value::Null(orig_arg->type()));
      GOOGLESQL_ASSIGN_OR_RETURN(
          agg_fn_arg,
          fn_builder_.If(std::move(pivot_column_not_distinct_expr),
                         std::move(orig_arg), std::move(null_literal)));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          agg_fn_arg,
          AnalyzeSubstitute(analyzer_options_, *catalog_, *type_factory_,
                            absl::StrCat("IF(", PivotColumnNotDistinctSql(),
                                         ", orig_arg, NULL)"),
                            {{"pivot_column", pivot_column_ref.get()},
                             {"pivot_value", pivot_value_expr_copy.get()},
                             {"orig_arg", orig_arg.get()}}),
          _.With(ExpectAnalyzeSubstituteSuccess));
    }
    GOOGLESQL_RET_CHECK(agg_fn_arg != nullptr);

    agg_fn_args.push_back(std::move(agg_fn_arg));

    for (int i = 1; i < agg_fn_arg_columns.size(); ++i) {
      if (agg_fn_arg_columns[i].IsInitialized()) {
        agg_fn_args.push_back(MakeResolvedColumnRef(
            agg_fn_arg_columns[i].type(), agg_fn_arg_columns[i],
            /*is_correlated=*/false));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> arg_copy,
                         ProcessNode(call->argument_list(i)));
        agg_fn_args.push_back(std::move(arg_copy));
      }
    }
  }

  if (call->signature().context_id() == FN_ANY_VALUE) {
    return RewriteAnyValuePivotExpr(call_copy, std::move(agg_fn_args),
                                    pivot_column);
  }

  return MakeResolvedAggregateFunctionCall(
      call->type(), call->function(), call->signature(), std::move(agg_fn_args),
      {}, call->error_mode(), call->distinct(), call->null_handling_modifier(),
      /*where_expr=*/nullptr, call_copy->release_having_modifier(),
      call_copy->release_order_by_item_list(), call_copy->release_limit(),
      call->function_call_info(),
      /*group_by_list=*/{},
      /*group_by_aggregate_list=*/{},
      /*having_expr=*/nullptr);
}
}  // namespace

class PivotRewriter : public Rewriter {
 public:
  std::string Name() const override { return "PivotRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());

    PivotRewriterVisitor visitor(&catalog, &type_factory, &column_factory,
                                 &options);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> result,
                     visitor.ConsumeRootNode<ResolvedNode>());
    GOOGLESQL_RET_CHECK(result != nullptr);
    return result;
  }
};

const Rewriter* GetPivotRewriter() {
  static const auto* const kRewriter = new PivotRewriter;
  return kRewriter;
}

}  // namespace googlesql
