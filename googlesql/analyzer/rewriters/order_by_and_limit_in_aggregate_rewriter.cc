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

#include "googlesql/analyzer/rewriters/order_by_and_limit_in_aggregate_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

const absl::string_view kRewriter = "$agg_rewriter";

// OrderByAndLimitInAggregateRewriter rewrites ARRAY_AGG, STRING_AGG, and
// ARRAY_CONCAT_AGG function calls with an ORDER BY or LIMIT clause into an
// ARRAY subquery and an ARRAY_AGG without an ORDER BY or LIMIT clause.
//
// Consider the transformation for ARRAY_AGG:
//
// SELECT ARRAY_AGG(DISTINCT f IGNORE NULLS HAVING MAX g ORDER BY ord LIMIT c)
//          AS result
// FROM input_table
// GROUP BY gby
//
// is transformed into a ResolvedAST like:
//
// SELECT WITH($result AS ARRAY(
//   SELECT DISTINCT e.f
//   FROM (SELECT e
//     FROM UNNEST($arr) AS e
//     WHERE e.f IS NOT NULL
//   )
//   ORDER BY e.ord
//   LIMIT c
// ), IF(ARRAY_LENGTH($result) >= 1, $result, NULL)) AS result
// FROM (SELECT ARRAY_AGG(
//              STRUCT(<all non-correlated variables used in f, g, ord>)
//              HAVING MAX g) as $arr
//       FROM from
//       GROUP BY gby)
//
// If there are multiple aggregates that must be transformed, we keep the same
// high level structure but use multiple columns:
//
// SELECT <agg1>, <agg2> FROM ..
//
// is transformed into:
//
// SELECT <result1>, <result2>
// FROM (SELECT <array_of_struct1>, <array_of_struct2> FROM ..)
//
// where <result> looks like `WITH($result as ARRAY(..), ..)`
// and <array_of_struct> looks like `ARRAY_AGG(STRUCT(<vars>) HAVING MAX g)`
//
// In this transformation, e.<foo> means the expression <foo> where all the
// columns are replaced with the struct field of the same name.
//
// The mechanism is to stash all the variables involved in a STRUCT in an
// initial ARRAY_AGG and then use those stashed variables inside the ARRAY
// subquery.
//
// The same transformation is used with small modifications for STRING_AGG and
// ARRAY_CONCAT_AGG.
// - For STRING_AGG, we perform an ARRAY_AGG with IGNORE NULLS and then
//   call ARRAY_TO_STRING on the resulting array.
// - For ARRAY_CONCAT_AGG, we do the same ARRAY_AGG on a dummy struct (to avoid
//   creating an array of arrays) and then call FLATTEN.
class OrderByAndLimitInAggregateRewriterVisitor
    : public ResolvedASTRewriteVisitor {
 public:
  explicit OrderByAndLimitInAggregateRewriterVisitor(
      const AnalyzerOptions& analyzer_options, Catalog& catalog,
      ColumnFactory& column_factory, TypeFactory& type_factory)
      : column_factory_(column_factory),
        type_factory_(type_factory),
        function_builder_(analyzer_options, catalog, type_factory) {}

  OrderByAndLimitInAggregateRewriterVisitor(
      const OrderByAndLimitInAggregateRewriterVisitor&) = delete;
  OrderByAndLimitInAggregateRewriterVisitor& operator=(
      const OrderByAndLimitInAggregateRewriterVisitor&) = delete;

 private:
  // The result of handling a single aggregate function call.
  // `array_of_struct` and `result` are described in the description above.
  struct AggregateFunctionCallRewriteResult {
    // A newly minted column naming the built array (`$arr` in the example
    // above)
    ResolvedColumn array_column;
    // The ARRAY_AGG function call that builds `array_column`.
    // `ARRAY_AGG(STRUCT(<all non-correlated variables used in f, g, foo, bar>)
    // HAVING MAX g)` in the example above.
    std::unique_ptr<const ResolvedAggregateFunctionCall> array_of_struct;
    // The result. References the expression in `array_of_struct` as
    // `array_column`.
    std::unique_ptr<const ResolvedExpr> result;
    // A rewrite occurred.
    bool is_rewritten = false;
  };

  // Handles a single ARRAY_AGG function call and returns a struct that
  // PostVisitResolvedAggregateScan() will stitch together.
  //
  // We don't use PostVisitResolvedAggregateFunctionCall() because we only can
  // rewrite cases where we're doing an aggregate scan. Some cases we don't want
  // to handle: ArrayAggregates that have an aggregate function call but no
  // scan and aggregate function calls in CREATE AGGREGATE FUNCTION.
  absl::StatusOr<AggregateFunctionCallRewriteResult>
  HandleAggregateFunctionCall(
      std::unique_ptr<ResolvedAggregateFunctionCall> function_call);

  // Rewrites the ARRAY_AGG function calls in the aggregate scan.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateScan(
      std::unique_ptr<const ResolvedAggregateScan> aggregate_scan) override;

  // If the aggregate scan has any aggregate function calls with ORDER BY or
  // LIMIT, returns an unimplemented error.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> CheckRewriterDoesNotApply(
      std::unique_ptr<const ResolvedAggregateScanBase> scan);

  // TODO: Remove these PostVisit functions and the static_assert
  // once we have a DefaultVisit on ResolvedASTRewriteVisitor.

  // If there are any anonymized aggregate scans with ORDER BY or LIMIT, returns
  // an unimplemented error. Today this is impossible because the anonymization
  // rewriter don't allow ARRAY_AGG/STRING_AGG/ARRAY_CONCAT_AGG.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAnonymizedAggregateScan(
      std::unique_ptr<const ResolvedAnonymizedAggregateScan> scan) override {
    return CheckRewriterDoesNotApply(std::move(scan));
  }

  // If there are any differentially private aggregate scans with ORDER BY or
  // LIMIT, returns an unimplemented error. Today this is impossible because the
  // anonymization rewriter don't allow ARRAY_AGG/STRING_AGG/ARRAY_CONCAT_AGG.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedDifferentialPrivacyAggregateScan(
      std::unique_ptr<const ResolvedDifferentialPrivacyAggregateScan> scan)
      override {
    return CheckRewriterDoesNotApply(std::move(scan));
  }

  // If there are any threshold aggregate scans with ORDER BY or LIMIT, returns
  // an unimplemented error. Today this is impossible because the anonymization
  // rewriter don't allow ARRAY_AGG/STRING_AGG/ARRAY_CONCAT_AGG.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregationThresholdAggregateScan(
      std::unique_ptr<const ResolvedAggregationThresholdAggregateScan> scan)
      override {
    return CheckRewriterDoesNotApply(std::move(scan));
  }

  static_assert(ResolvedAggregateScanBase::NUM_DESCENDANT_LEAF_TYPES == 4,
                "Add support for new aggregate scans to "
                "OrderByAndLimitInAggregateRewriter");

  ColumnFactory& column_factory_;
  TypeFactory& type_factory_;
  FunctionCallBuilder function_builder_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
OrderByAndLimitInAggregateRewriterVisitor::CheckRewriterDoesNotApply(
    std::unique_ptr<const ResolvedAggregateScanBase> scan) {
  for (const auto& aggregate_computed_column : scan->aggregate_list()) {
    if (aggregate_computed_column->expr()
            ->Is<ResolvedAggregateFunctionCall>()) {
      auto* function_call = aggregate_computed_column->expr()
                                ->GetAs<ResolvedAggregateFunctionCall>();
      if (function_call->order_by_item_list_size() > 0 ||
          function_call->limit() != nullptr) {
        return absl::UnimplementedError(
            "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter does not support "
            "ORDER BY or LIMIT in differentially private aggregate "
            "functions");
      }
    }
  }
  return scan;
}

// Handle a ResolvedAggregateScan. Gather the AggregateFunctionCallRewriteResult
// from each aggregate function call and combine them.
absl::StatusOr<std::unique_ptr<const ResolvedNode>>
OrderByAndLimitInAggregateRewriterVisitor::PostVisitResolvedAggregateScan(
    std::unique_ptr<const ResolvedAggregateScan> aggregate_scan) {
  ResolvedAggregateScanBuilder builder = ToBuilder(std::move(aggregate_scan));
  std::vector<ResolvedColumn> column_list = builder.release_column_list();
  ResolvedProjectScanBuilder project_scan_builder;
  project_scan_builder.set_column_list(column_list);
  absl::flat_hash_set<ResolvedColumn> seen_columns;
  bool any_rewritten = false;
  for (auto& aggregate_computed_column : builder.release_aggregate_list()) {
    ResolvedColumn result_column = aggregate_computed_column->column();
    seen_columns.insert(result_column);
    GOOGLESQL_RET_CHECK(
        aggregate_computed_column->expr()->Is<ResolvedAggregateFunctionCall>());
    if (!aggregate_computed_column->Is<ResolvedComputedColumn>()) {
      // TODO: Today in the ResolvedAST the only place we can put
      // side effecting columns (ResolvedComputedColumnBase) is in
      // ResolvedAggregateScanBase (and child classes) but here we want to move
      // a side-effecting column from an aggregation into an ARRAY subquery.
      return absl::UnimplementedError(
          "ResolvedDeferredComputedColumn is not supported in "
          "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter");
    }
    ResolvedComputedColumnBuilder computed_column_builder =
        ToBuilder(GetAsResolvedNode<ResolvedComputedColumn>(
            std::move(aggregate_computed_column)));
    GOOGLESQL_ASSIGN_OR_RETURN(AggregateFunctionCallRewriteResult result,
                     HandleAggregateFunctionCall(absl::WrapUnique(
                         const_cast<ResolvedExpr*>(
                             computed_column_builder.release_expr().release())
                             ->GetAs<ResolvedAggregateFunctionCall>())));
    if (result.is_rewritten) {
      any_rewritten = true;
      builder.add_column_list(result.array_column);
      builder.add_aggregate_list(MakeResolvedComputedColumn(
          result.array_column, std::move(result.array_of_struct)));
      project_scan_builder.add_expr_list(
          MakeResolvedComputedColumn(result_column, std::move(result.result)));
    } else {
      builder.add_column_list(result_column);
      builder.add_aggregate_list(MakeResolvedComputedColumn(
          result_column, std::move(result.array_of_struct)));
    }
  }
  // The rewrite relevance checker may have told us to run on the query but
  // there may be multiple aggregate scans in the query and this one doesn't
  // have any ORDER BY or LIMIT to rewrite away.
  if (!any_rewritten) {
    // Take back the original column list to avoid reordering it.
    return std::move(builder).set_column_list(std::move(column_list)).Build();
  }

  // Columns that aren't involved in an aggregate need to be in our transformed
  // builder. For example in `SELECT a, ARRAY_AGG(b ORDER BY b)`, the column `a`
  // needs to be in the transformed builder.
  for (const ResolvedColumn& column : column_list) {
    if (!seen_columns.contains(column)) {
      builder.add_column_list(column);
    }
  }
  return std::move(project_scan_builder)
      .set_input_scan(std::move(builder))
      .Build();
}

// Aggregate functions we support in this rewriter.
enum class AggFunction {
  kArrayAgg,
  kStringAgg,
  kArrayConcatAgg,
};

// Intermediate state while processing an aggregate function call.
struct AggregateFunctionState {
  ColumnFactory& column_factory;
  TypeFactory& type_factory;
  FunctionCallBuilder& function_builder;

  // The non-correlated column references in the original aggregate function
  // call.
  const std::vector<std::unique_ptr<const ResolvedColumnRef>> refs;
  // The result type of the original aggregate function call.
  const Type* const result_type = nullptr;
  // The column that holds the aggregate's first argument. It's the original
  // value: most users will want to get the current column using
  // `column_map_.at(original_arg_column_)`.
  const ResolvedColumn original_arg_column;
};

absl::StatusOr<std::unique_ptr<const ResolvedScan>>
ProjectColumnsFromStructAndComputeArg(
    const AggregateFunctionState& state, const ResolvedColumn& array_column,
    std::unique_ptr<const ResolvedExpr> arg,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  ResolvedColumn struct_column = state.column_factory.MakeCol(
      kRewriter, "$struct", array_column.type()->AsArray()->element_type());
  ResolvedProjectScanBuilder project_scan_builder =
      ResolvedProjectScanBuilder().set_input_scan(
          ResolvedArrayScanBuilder()
              .add_column_list(struct_column)
              .add_array_expr_list(ResolvedColumnRefBuilder()
                                       .set_column(array_column)
                                       .set_type(array_column.type())
                                       .set_is_correlated(true))
              .add_element_column_list(struct_column));
  int idx = 0;
  for (const std::unique_ptr<const ResolvedColumnRef>& ref : state.refs) {
    ResolvedColumn new_col = column_map.at(ref->column());
    project_scan_builder.add_column_list(new_col);
    project_scan_builder.add_expr_list(
        ResolvedComputedColumnBuilder().set_column(new_col).set_expr(
            ResolvedGetStructFieldBuilder()
                .set_type(new_col.type())
                .set_expr(MakeResolvedColumnRef(struct_column.type(),
                                                struct_column,
                                                /*is_correlated=*/false))
                .set_field_idx(idx++)));
  }
  std::unique_ptr<const ResolvedScan> input_scan;
  if (project_scan_builder.column_list().empty()) {
    // An aggregate that doesn't reference any real columns, like
    // `ARRAY_AGG(1 LIMIT 100)`
    // We keep the array scan builder so that the containing subquery's
    // parameter list is constant.
    input_scan = project_scan_builder.release_input_scan();
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(input_scan, std::move(project_scan_builder).Build());
  }
  if (arg->Is<ResolvedColumnRef>() &&
      state.original_arg_column == arg->GetAs<ResolvedColumnRef>()->column()) {
    return std::move(input_scan);
  }
  // We will always access arg_column through column_map_. It's a unique
  // column so it won't interfere with any existing refs.
  column_map[state.original_arg_column] = state.original_arg_column;
  std::vector<ResolvedColumn> column_list = input_scan->column_list();
  return ResolvedProjectScanBuilder()
      .set_column_list(std::move(column_list))
      .add_column_list(column_map.at(state.original_arg_column))
      .add_expr_list(
          ResolvedComputedColumnBuilder()
              .set_column(column_map.at(state.original_arg_column))
              .set_expr(RemapSpecifiedColumns(std::move(arg), column_map)))
      .set_input_scan(std::move(input_scan))
      .Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedScan>> HandleNullHandlingModifier(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan,
    ResolvedNonScalarFunctionCallBase::NullHandlingModifier
        null_handling_modifier,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  switch (null_handling_modifier) {
    case ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING:
    case ResolvedNonScalarFunctionCallBase::RESPECT_NULLS:
      return input_scan;
    case ResolvedNonScalarFunctionCallBase::IGNORE_NULLS: {
      std::vector<ResolvedColumn> column_list = input_scan->column_list();
      return ResolvedFilterScanBuilder()
          .set_column_list(std::move(column_list))
          .set_input_scan(std::move(input_scan))
          .set_filter_expr(
              state.function_builder.IsNotNull(MakeResolvedColumnRef(
                  column_map.at(state.original_arg_column).type(),
                  column_map.at(state.original_arg_column),
                  /*is_correlated=*/false)))
          .Build();
    }
  }
}

// Implementing DISTINCT is accomplished by a ResolvedAggregateScan with no
// aggregate function that groups the argument column.
absl::StatusOr<std::unique_ptr<const ResolvedScan>> HandleDistinct(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan, bool distinct,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  if (!distinct) {
    return input_scan;
  }
  // If DISTINCT is present, we need to end up with the argument of the
  // aggregate as the only column.
  //
  // If there is no order by clause, no other column is needed: the argument to
  // limit must be a constant. If there is an order by clause, why is it okay to
  // project away the other columns? If there is DISTINCT and an ORDER BY clause
  // the order by list must only refer to the argument to the aggregate. The
  // resolver will create a ResolvedColumnRef for the shared expressions so
  // there should only be one column in the incoming column_list.
  if (input_scan->column_list().size() > 1) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        input_scan,
        ResolvedProjectScanBuilder()
            .add_column_list(column_map.at(state.original_arg_column))
            .set_input_scan(std::move(input_scan))
            .Build());
  }

  ResolvedAggregateScanBuilder aggregate_scan_builder;
  ResolvedColumn arg_column = column_map.at(state.original_arg_column);
  ResolvedColumn distinct_arg = state.column_factory.MakeCol(
      kRewriter, "distinct.arg", arg_column.type());
  aggregate_scan_builder.add_column_list(distinct_arg);
  aggregate_scan_builder.add_group_by_list(MakeResolvedComputedColumn(
      distinct_arg, MakeResolvedColumnRef(arg_column.type(), arg_column,
                                          /*is_correlated=*/false)));
  // There's only one column now.
  column_map = {{state.original_arg_column, distinct_arg}};
  return std::move(aggregate_scan_builder)
      .set_input_scan(std::move(input_scan))
      .Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedScan>> HandleOrderByIfPresent(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan,
    std::vector<std::unique_ptr<const ResolvedOrderByItem>>
        original_order_by_list,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  // Remap the order by items using `column_map`.
  std::vector<std::unique_ptr<const ResolvedOrderByItem>> order_by_item_list;
  for (std::unique_ptr<const ResolvedOrderByItem>& order_by_item :
       std::move(original_order_by_list)) {
    GOOGLESQL_ASSIGN_OR_RETURN(order_by_item, RemapSpecifiedColumns(
                                        std::move(order_by_item), column_map));
    order_by_item_list.push_back(std::move(order_by_item));
  }

  // Whether or not there is an ORDER BY clause, we need to project out the
  // argument since the ARRAY subquery expects just one column.
  if (order_by_item_list.empty()) {
    // This project scan isn't necessary for the ResolvedAST but it allows the
    // SQLBuilder to see that we're projecting away everything but arg_column.
    return ResolvedProjectScanBuilder()
        .add_column_list(column_map.at(state.original_arg_column))
        .set_input_scan(std::move(input_scan))
        .Build();
  } else {
    // Some ORDER BY items may be correlated which is not supported in some
    // engines. Create equivalent non-correlated columns for those items.
    ResolvedProjectScanBuilder project_scan_builder;
    bool any_correlated = false;
    for (std::unique_ptr<const ResolvedOrderByItem>& order_by_item :
         order_by_item_list) {
      if (order_by_item->column_ref()->is_correlated()) {
        any_correlated = true;
        ResolvedColumn new_col = state.column_factory.MakeCol(
            kRewriter,
            absl::StrCat("non_correlated.",
                         order_by_item->column_ref()->column().name()),
            order_by_item->column_ref()->type());
        project_scan_builder.add_column_list(new_col);
        project_scan_builder.add_expr_list(
            ResolvedComputedColumnBuilder().set_column(new_col).set_expr(
                MakeResolvedColumnRef(order_by_item->column_ref()->type(),
                                      order_by_item->column_ref()->column(),
                                      /*is_correlated=*/true)));
        GOOGLESQL_ASSIGN_OR_RETURN(
            order_by_item,
            ToBuilder(std::move(order_by_item))
                .set_column_ref(MakeResolvedColumnRef(new_col.type(), new_col,
                                                      /*is_correlated=*/false))
                .Build());
      } else {
        project_scan_builder.add_column_list(
            order_by_item->column_ref()->column());
      }
    }
    if (any_correlated) {
      GOOGLESQL_ASSIGN_OR_RETURN(input_scan, std::move(project_scan_builder)
                                       .set_input_scan(std::move(input_scan))
                                       .Build());
    }
    return ResolvedOrderByScanBuilder()
        .add_column_list(column_map.at(state.original_arg_column))
        .set_input_scan(std::move(input_scan))
        .set_is_ordered(true)
        .set_order_by_item_list(std::move(order_by_item_list))
        .Build();
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedScan>> HandleLimit(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan,
    std::unique_ptr<const ResolvedExpr> limit,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  if (limit == nullptr) {
    return input_scan;
  }
  return ResolvedLimitOffsetScanBuilder()
      .add_column_list(column_map.at(state.original_arg_column))
      .set_input_scan(std::move(input_scan))
      .set_limit(std::move(limit))
      .set_offset(MakeResolvedLiteral(Value::Int64(0)))
      .Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeArrayExpr(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan,
    const ResolvedColumn& array_column,
    std::vector<std::unique_ptr<const ResolvedColumnRef>> correlated_refs) {
  // If the input to ARRAY_AGG/STRING_AGG/ARRAY_CONCAT_AGG is empty, we need
  // to return NULL. If the input to an ARRAY subquery is empty, the return is
  // the empty list. In order to convert between them we produce NULL if the
  // ARRAY_LENGTH is 0.
  GOOGLESQL_RET_CHECK_EQ(input_scan->column_list().size(), 1);
  const Type* array_result_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(state.type_factory.MakeArrayType(
      input_scan->column_list()[0].type(), &array_result_type));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedSubqueryExpr> array_subquery,
                   ResolvedSubqueryExprBuilder()
                       .set_subquery_type(ResolvedSubqueryExpr::ARRAY)
                       .set_subquery(std::move(input_scan))
                       .set_parameter_list(std::move(correlated_refs))
                       .add_parameter_list(ResolvedColumnRefBuilder()
                                               .set_column(array_column)
                                               .set_type(array_column.type())
                                               .set_is_correlated(false))
                       .set_type(array_result_type)
                       .Build());
  return state.function_builder.MakeNullIfEmptyArray(state.column_factory,
                                                     std::move(array_subquery));
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeArrayConcatExpr(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan,
    const ResolvedColumn& array_column,
    std::vector<std::unique_ptr<const ResolvedColumnRef>> correlated_refs,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  // We cannot build an array of arrays, so we pack up our array into a struct
  const StructType* array_concat_struct_type = nullptr;
  ResolvedColumn arg_column = column_map.at(state.original_arg_column);
  GOOGLESQL_RETURN_IF_ERROR(state.type_factory.MakeStructType(
      {{"array", arg_column.type()}}, &array_concat_struct_type));
  ResolvedColumn struct_column = state.column_factory.MakeCol(
      kRewriter, "$struct", array_concat_struct_type);
  GOOGLESQL_ASSIGN_OR_RETURN(
      input_scan,
      ResolvedProjectScanBuilder()
          .add_column_list(struct_column)
          .add_expr_list(
              ResolvedComputedColumnBuilder()
                  .set_column(struct_column)
                  .set_expr(ResolvedMakeStructBuilder()
                                .set_type(array_concat_struct_type)
                                .add_field_list(MakeResolvedColumnRef(
                                    arg_column.type(), arg_column,
                                    /*is_correlated=*/false))))
          .set_input_scan(std::move(input_scan))
          .Build());

  // We wrapped every child array in a struct above; unwrap them.
  return ResolvedFlattenBuilder()
      .set_type(state.result_type)
      .set_expr(MakeArrayExpr(state, std::move(input_scan), array_column,
                              std::move(correlated_refs)))
      .add_get_field_list(ResolvedGetStructFieldBuilder()
                              .set_type(state.result_type)
                              .set_expr(ResolvedFlattenedArgBuilder().set_type(
                                  array_concat_struct_type))
                              .set_field_idx(0))
      .Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>> GenerateAggregate(
    AggFunction agg_function, const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedScan> input_scan,
    const ResolvedColumn& array_column,
    std::vector<std::unique_ptr<const ResolvedColumnRef>> correlated_refs,
    std::unique_ptr<const ResolvedExpr> arg2,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  switch (agg_function) {
    case AggFunction::kArrayAgg: {
      return MakeArrayExpr(state, std::move(input_scan), array_column,
                           std::move(correlated_refs));
    }
    case AggFunction::kStringAgg: {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> array_expr,
                       MakeArrayExpr(state, std::move(input_scan), array_column,
                                     std::move(correlated_refs)));
      // We don't have to worry about replacing the columns in arg2 like we did
      // for arg1 because arg2 (the delimiter) must be a literal.
      return state.function_builder.ArrayToString(std::move(array_expr),
                                                  std::move(arg2));
      break;
    }
    case AggFunction::kArrayConcatAgg: {
      return MakeArrayConcatExpr(state, std::move(input_scan), array_column,
                                 std::move(correlated_refs), column_map);
    }
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
GenerateArrayOfStruct(
    const AggregateFunctionState& state,
    std::unique_ptr<const ResolvedAggregateHavingModifier> having_modifier,
    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>& column_map) {
  // Create `ARRAY_AGG(STRUCT(<all non-correlated variables used in f, g, foo,
  // bar>) HAVING MAX g)`
  std::vector<StructType::StructField> struct_fields;
  std::vector<std::unique_ptr<const ResolvedExpr>> struct_fields_exprs;
  for (int i = 0; i < state.refs.size(); ++i) {
    const std::unique_ptr<const ResolvedColumnRef>& ref = state.refs[i];
    struct_fields.emplace_back(absl::StrCat(ref->column().name(), "_", i),
                               ref->column().type());
    struct_fields_exprs.push_back(MakeResolvedColumnRef(
        ref->column().type(), ref->column(), ref->is_correlated()));
    column_map[ref->column()] = state.column_factory.MakeCol(
        kRewriter, ref->column().name(), ref->column().type());
  }
  const StructType* struct_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(state.type_factory.MakeStructType(
      absl::MakeConstSpan(struct_fields), &struct_type));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedMakeStruct> struct_arg,
                   ResolvedMakeStructBuilder()
                       .set_type(struct_type)
                       .set_field_list(std::move(struct_fields_exprs))
                       .Build());
  std::unique_ptr<const ResolvedExpr> having_expr = nullptr;
  ResolvedAggregateHavingModifier::HavingModifierKind having_kind =
      ResolvedAggregateHavingModifier::INVALID;
  if (having_modifier != nullptr) {
    having_kind = having_modifier->kind();
    having_expr = ToBuilder(std::move(having_modifier)).release_having_expr();
  }
  return state.function_builder.ArrayAgg(std::move(struct_arg),
                                         std::move(having_expr), having_kind);
}

// Given an aggregate function call, return AggregateFunctionCallRewriteResult.
absl::StatusOr<OrderByAndLimitInAggregateRewriterVisitor::
                   AggregateFunctionCallRewriteResult>
OrderByAndLimitInAggregateRewriterVisitor::HandleAggregateFunctionCall(
    std::unique_ptr<ResolvedAggregateFunctionCall> aggregate_function_call) {
  if (aggregate_function_call->order_by_item_list_size() == 0 &&
      aggregate_function_call->limit() == nullptr) {
    return AggregateFunctionCallRewriteResult{
        .array_of_struct = std::move(aggregate_function_call),
        .is_rewritten = false};
  }
  AggFunction agg_function;
  if (aggregate_function_call->function()->IsGoogleSQLBuiltin(FN_ARRAY_AGG)) {
    agg_function = AggFunction::kArrayAgg;
  } else if (aggregate_function_call->function()->IsGoogleSQLBuiltin(
                 FN_STRING_AGG_STRING) ||
             aggregate_function_call->function()->IsGoogleSQLBuiltin(
                 FN_STRING_AGG_DELIM_STRING) ||
             aggregate_function_call->function()->IsGoogleSQLBuiltin(
                 FN_STRING_AGG_BYTES) ||
             aggregate_function_call->function()->IsGoogleSQLBuiltin(
                 FN_STRING_AGG_DELIM_BYTES)) {
    agg_function = AggFunction::kStringAgg;
  } else if (aggregate_function_call->function()->IsGoogleSQLBuiltin(
                 FN_ARRAY_CONCAT_AGG)) {
    agg_function = AggFunction::kArrayConcatAgg;
  } else {
    return absl::UnimplementedError(
        "Only ARRAY_AGG, STRING_AGG and ARRAY_CONCAT_AGG are supported in "
        "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter");
  }
  if (!aggregate_function_call->group_by_list().empty()) {
    return absl::UnimplementedError(
        "Aggregate functions with GROUP BY modifiers are not supported in "
        "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter");
  }
  if (aggregate_function_call->where_expr() != nullptr) {
    return absl::UnimplementedError(
        "Aggregate functions with a WHERE filter modifier are not supported in "
        "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter");
  }
  if (aggregate_function_call->having_expr() != nullptr) {
    return absl::UnimplementedError(
        "Aggregate functions with a HAVING filter modifier are not supported "
        "in ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter");
  }

  // Columns used in the HAVING clause are used in the same context as the
  // original aggregates so we don't need any special handling for the refs they
  // contain. Release the having modifier before CollectSortUniqueColumnRefs.
  std::unique_ptr<const ResolvedAggregateHavingModifier> having_modifier =
      aggregate_function_call->release_having_modifier();
  std::vector<std::unique_ptr<const ResolvedColumnRef>> all_refs;
  GOOGLESQL_RETURN_IF_ERROR(CollectSortUniqueColumnRefs(*aggregate_function_call,
                                              all_refs,
                                              /*correlate=*/false));
  std::vector<std::unique_ptr<const ResolvedColumnRef>> refs;
  std::vector<std::unique_ptr<const ResolvedColumnRef>> correlated_refs;
  for (std::unique_ptr<const ResolvedColumnRef>& ref : all_refs) {
    if (ref->is_correlated()) {
      correlated_refs.push_back(std::move(ref));
    } else {
      refs.push_back(std::move(ref));
    }
  }

  const Type* const result_type = aggregate_function_call->type();
  ResolvedNonScalarFunctionCallBase::NullHandlingModifier
      null_handling_modifier = ResolvedNonScalarFunctionCallBase::IGNORE_NULLS;
  switch (agg_function) {
    case AggFunction::kStringAgg:
    case AggFunction::kArrayConcatAgg:
      // STRING_AGG and ARRAY_CONCAT_AGG don't accept a null handling modifier
      // but implicitly ignore nulls.
      break;
    case AggFunction::kArrayAgg: {
      null_handling_modifier =
          aggregate_function_call->null_handling_modifier();
      break;
    }
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> args =
      aggregate_function_call->release_argument_list();
  std::unique_ptr<const ResolvedExpr> arg1 = std::move(args[0]);
  std::unique_ptr<const ResolvedExpr> arg2;
  switch (agg_function) {
    case AggFunction::kArrayAgg:
    case AggFunction::kArrayConcatAgg:
      GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
      break;
    case AggFunction::kStringAgg:
      if (args.size() == 1) {
        // We will call ARRAY_TO_STRING which doesn't have an optional delimiter
        // argument. STRING_AGG's default is ",".
        if (arg1->type()->IsString()) {
          arg2 = MakeResolvedLiteral(Value::String(","));
        } else {
          arg2 = MakeResolvedLiteral(Value::Bytes(","));
        }
      } else {
        GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
        arg2 = std::move(args[1]);
      }
      break;
  }

  if (aggregate_function_call->distinct() &&
      !aggregate_function_call->order_by_item_list().empty()) {
    GOOGLESQL_RET_CHECK(arg1->Is<ResolvedColumnRef>())
        << "The argument must be a ResolvedColumnRef if DISTINCT and ORDER BY "
           "are both present";
    for (const std::unique_ptr<const ResolvedOrderByItem>& order_by_item :
         aggregate_function_call->order_by_item_list()) {
      GOOGLESQL_RET_CHECK(arg1->GetAs<ResolvedColumnRef>()->column() ==
                order_by_item->column_ref()->column())
          << "Order by item is referring to a column other than the argument "
             "column. First arg: "
          << arg1->DebugString()
          << "; order by item: " << order_by_item->DebugString();
      GOOGLESQL_RET_CHECK(!arg1->GetAs<ResolvedColumnRef>()->is_correlated());
      GOOGLESQL_RET_CHECK(!order_by_item->column_ref()->is_correlated());
    }
  }
  AggregateFunctionState state{
      column_factory_, type_factory_, function_builder_, std::move(refs),
      result_type,
      /*original_arg_column=*/
      arg1->Is<ResolvedColumnRef>() &&
              !arg1->GetAs<ResolvedColumnRef>()->is_correlated()
          ? arg1->GetAs<ResolvedColumnRef>()->column()
          : column_factory_.MakeCol(kRewriter, "$arg", arg1->type())};
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> column_map;
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedAggregateFunctionCall> array_of_struct,
      GenerateArrayOfStruct(state, std::move(having_modifier), column_map));
  ResolvedColumn array_column =
      column_factory_.MakeCol(kRewriter, "$array", array_of_struct->type());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> input_scan,
                   ProjectColumnsFromStructAndComputeArg(
                       state, array_column, std::move(arg1), column_map));
  GOOGLESQL_ASSIGN_OR_RETURN(input_scan, HandleNullHandlingModifier(
                                   state, std::move(input_scan),
                                   null_handling_modifier, column_map));
  GOOGLESQL_ASSIGN_OR_RETURN(
      input_scan,
      HandleDistinct(state, std::move(input_scan),
                     aggregate_function_call->distinct(), column_map));
  GOOGLESQL_ASSIGN_OR_RETURN(
      input_scan,
      HandleOrderByIfPresent(
          state, std::move(input_scan),
          aggregate_function_call->release_order_by_item_list(), column_map));
  GOOGLESQL_ASSIGN_OR_RETURN(
      input_scan,
      HandleLimit(state, std::move(input_scan),
                  aggregate_function_call->release_limit(), column_map));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> result,
                   GenerateAggregate(agg_function, state, std::move(input_scan),
                                     array_column, std::move(correlated_refs),
                                     std::move(arg2), column_map));
  return AggregateFunctionCallRewriteResult{
      .array_column = std::move(array_column),
      .array_of_struct = std::move(array_of_struct),
      .result = std::move(result),
      .is_rewritten = true};
}

class OrderByAndLimitInAggregateRewriter : public Rewriter {
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(/*max_seen_col_id=*/0,
                                 *options.id_string_pool(),
                                 *options.column_id_sequence_number());
    OrderByAndLimitInAggregateRewriterVisitor visitor(
        options, catalog, column_factory, type_factory);
    return visitor.VisitAll(std::move(input));
  }

  std::string Name() const override {
    return "OrderByAndLimitInAggregateRewriter";
  }
};

}  // namespace

const Rewriter* GetOrderByAndLimitInAggregateRewriter() {
  static const auto* const kRewriter = new OrderByAndLimitInAggregateRewriter;
  return kRewriter;
}

}  // namespace googlesql
