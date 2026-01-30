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

#include "googlesql/analyzer/rewriters/privacy/approx_count_distinct_utility.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/rewriters/privacy/privacy_utility.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/proto/anon_output_with_report.pb.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_comparator.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace differential_privacy {
namespace approx_count_distinct_utility {

STATIC_IDSTRING(kContributionBoundsPerGroup, "contribution_bounds_per_group");
STATIC_IDSTRING(kReportFormat, "report_format");
STATIC_IDSTRING(kMaxContributionsPerGroup, "max_contributions_per_group");
STATIC_IDSTRING(kContributionBoundingStrategy,
                "contribution_bounding_strategy");

namespace {
// Returns true if the given `function_call` is an approx count distinct
// function.
bool IsApproxCountDistinct(const ResolvedAggregateFunctionCall* function_call) {
  // Only consider built-in functions.
  if (function_call->function()->GetGroup() !=
      Function::kGoogleSQLFunctionGroupName) {
    return false;
  }
  switch (function_call->signature().context_id()) {
    case googlesql::FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT:
    case googlesql::FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_JSON:
    case googlesql::FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_PROTO:
      return true;
    default:
      return false;
  }
}

// Returns a copy of the argument list of the given `approx_count_disinct`
// function call with the `report_format` argument omitted.
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
CopyArgumentListOmittingReportFormat(
    const ResolvedAggregateFunctionCall* function_call) {
  GOOGLESQL_RET_CHECK(function_call != nullptr);
  GOOGLESQL_RET_CHECK(IsApproxCountDistinct(function_call));

  std::vector<std::unique_ptr<const ResolvedExpr>> new_argument_list;
  for (int i = 0; i < function_call->argument_list_size(); ++i) {
    if (function_call->signature().argument(i).has_argument_name() &&
        function_call->signature().argument(i).argument_name() ==
            "report_format") {
      continue;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> arg,
        ResolvedASTDeepCopyVisitor::Copy(function_call->argument_list(i)));
    new_argument_list.push_back(std::move(arg));
  }
  return new_argument_list;
}

// Creates a struct of type STRUCT<INT64, INT64> with values (0, 1).
// This is used to rewrite the `approx_count_distinct` function as a `sum`,
// when possible (see below).
absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> MakeZeroOneBounds(
    TypeFactory* type_factory) {
  GOOGLESQL_RET_CHECK(type_factory != nullptr);
  const StructType* contribution_bounds_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory->MakeStructType({{"lower_bound", type_factory->get_int64()},
                                    {"upper_bound", type_factory->get_int64()}},
                                   &contribution_bounds_type));
  GOOGLESQL_ASSIGN_OR_RETURN(Value contribution_bounds_value,
                   Value::MakeStruct(contribution_bounds_type,
                                     {Value::Int64(0), Value::Int64(1)}));
  return MakeResolvedLiteral(contribution_bounds_value);
}

bool IsWithReport(const ResolvedAggregateFunctionCall* function_call) {
  // Only consider built-in functions.
  if (function_call->function()->GetGroup() !=
      Function::kGoogleSQLFunctionGroupName) {
    return false;
  }
  return function_call->signature().context_id() ==
             googlesql::
                 FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_JSON ||
         function_call->signature().context_id() ==
             googlesql::
                 FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_PROTO;
}

// Takes an `approx_count_distinct(privacy_ids)` function call and returns a
// simpler expression computing the same thing (i.e., the noisy count of
// distinct privacy ids). Particularly, given a call of the following form:
// `approx_count_distinct(expr, report_format => RF,
//           contribution_bounding_strategy => CBS,
//            max_contributions_per_group => MC)`
// we return
// `differential_privacy_sum(1, report_format => RF,
//                           contribution_bounds_per_group => (0, 1))`
// Note that the `contribution_bounds_per_group` is a struct with two fields.
// This is to match the signature of `differential_privacy_sum`.
// Note that we are dropping the `max_contributions_per_group` argument. This
// is because in the case of counting distinct privacy ids, the optimal choice
// is always 1. Thus we ignore the actual value and set it to 1.
// If `report_format` is omitted, it will be omitted in the resulting function
// as well.
//
// Note: This is a pure optimization. This optimization is meaningful:
// a) For performance reasons, since the `sum` aggregation is more
//    efficient than the `approx_count_distinct` aggregation.
// b) For better differential privacy budget-split behaviour.
//    The `sum(1, contribution_bounds_per_group =>(0, 1))` will be
//    recognized as a "count distinct privacy ids" aggregation. It will
//    therefore be reused for thresholding and as the input to the
//    `EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT` function.
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
MakeCountDistinctPrivacyIdsFunctionCall(
    const ResolvedAggregateFunctionCall* function_call, Resolver* resolver,
    TypeFactory* type_factory) {
  GOOGLESQL_RET_CHECK(function_call != nullptr);
  GOOGLESQL_RET_CHECK(IsApproxCountDistinct(function_call));
  GOOGLESQL_RET_CHECK(resolver != nullptr);
  GOOGLESQL_RET_CHECK(type_factory != nullptr);

  FakeASTNode dummy_ast_location;
  std::vector<NamedArgumentInfo> named_arguments;

  bool is_with_report = IsWithReport(function_call);
  if (is_with_report) {
    GOOGLESQL_RET_CHECK(function_call->argument_list_size() == 4);
  } else {
    GOOGLESQL_RET_CHECK(function_call->argument_list_size() == 3);
  }
  std::vector<std::unique_ptr<const ResolvedExpr>> new_arguments(
      function_call->argument_list_size() - 1);
  // expr = Literal(1).
  new_arguments[0] = MakeResolvedLiteral(Value::Int64(1));
  if (is_with_report) {
    GOOGLESQL_ASSIGN_OR_RETURN(new_arguments[1],
                     ResolvedASTDeepCopyVisitor::Copy(
                         function_call->argument_list()[1].get()));
    named_arguments.emplace_back(/*name=*/kReportFormat,
                                 /*index=*/1, &dummy_ast_location);
  }

  // Sets the `contribution_bounds_per_group` to `(0, 1)` to ensure that
  // we are counting unique users.
  GOOGLESQL_ASSIGN_OR_RETURN(new_arguments.back(), MakeZeroOneBounds(type_factory));
  named_arguments.emplace_back(
      /*name=*/kContributionBoundsPerGroup,
      /*index=*/new_arguments.size() - 1, &dummy_ast_location);

  return ResolveFunctionCall("$differential_privacy_sum",
                             std::move(new_arguments), named_arguments,
                             resolver);
}

absl::StatusOr<std::optional<functions::DifferentialPrivacyEnums::ReportFormat>>
GetReportFormat(const ResolvedAggregateFunctionCall* function_call) {
  GOOGLESQL_RET_CHECK(function_call != nullptr);
  GOOGLESQL_RET_CHECK(function_call->function()->GetGroup() ==
            Function::kGoogleSQLFunctionGroupName);
  switch (function_call->signature().context_id()) {
    case FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT:
      return std::nullopt;
    case FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_PROTO:
      return functions::DifferentialPrivacyEnums::PROTO;
    case FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_JSON:
      return functions::DifferentialPrivacyEnums::JSON;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected function call: "
                       << function_call->DebugString();
  }
}

// Takes an `approx_count_distinct` call and rewrites it as a
// `merge_partial_for_dp_approx_count_distinct` call. The new call will have the
// same arguments as the original call, except for the `report_format`, which
// will be transferred to `extract_for_dp_approx_count_distinct`.
absl::StatusOr<std::unique_ptr<ResolvedComputedColumnBase>>
RewriteAsMergePartialForDpApproxCountDistinct(
    const ResolvedAggregateFunctionCall* function_call,
    const ResolvedExpr* noisy_count_distinct_privacy_ids_expr,
    Resolver* resolver, ColumnFactory* allocator) {
  GOOGLESQL_RET_CHECK(function_call != nullptr);
  GOOGLESQL_RET_CHECK(IsApproxCountDistinct(function_call));
  GOOGLESQL_RET_CHECK(noisy_count_distinct_privacy_ids_expr != nullptr);
  GOOGLESQL_RET_CHECK(resolver != nullptr);
  GOOGLESQL_RET_CHECK(allocator != nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
                   CopyArgumentListOmittingReportFormat(function_call));
  // If `report_format` is removed, we expect three arguments in this order:
  // `expr`, `contribution_bounding_strategy`, and
  // `max_contributions_per_group`. Moreover, the last two are named arguments.
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 3);

  FakeASTNode dummy_ast_location;
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> partial_merge_call,
      ResolveFunctionCall(
          "$differential_privacy_merge_partial_for_dp_approx_count_distinct",
          std::move(arguments),
          /*named_arguments=*/
          {{/*name=*/kContributionBoundingStrategy,
            /*index=*/1, &dummy_ast_location},
           {/*name=*/kMaxContributionsPerGroup,
            /*index=*/2, &dummy_ast_location}},
          resolver));

  return MakeResolvedComputedColumn(
      allocator->MakeCol("$differential_privacy", "$merge_partial_result",
                         types::BytesType()),
      std::move(partial_merge_call));
}

// Creates the list of columns that will be visible outside the differential
// privacy aggregate scan by simply adding all aggregate columns and all group
// by columns to the list. Through this, we usually export more information
// than is needed. This is not a problem, since this function is only used
// when we create a projection scan on top of the differential privacy aggregate
// scan to support `approx_count_distinct`.
std::vector<ResolvedColumn> CompileColumnListFromAggregatesAndGroupBys(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        aggregate_list,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        group_by_list) {
  std::vector<ResolvedColumn> column_list;
  column_list.reserve(aggregate_list.size() + group_by_list.size());
  for (const auto& aggregate : aggregate_list) {
    column_list.push_back(aggregate->column());
  }
  for (const auto& group_by : group_by_list) {
    column_list.push_back(group_by->column());
  }
  return column_list;
}

// Rewrites the differential privacy aggregate scan to perform cross-user
// aggregation into a byte-blob (sketch). Then adds a projection scan that
// extracts the desired value from the byte blob.
//
// Remark: The reason we split the partial merge and extract functions is that
// the extract function needs to reference the
// `noisy_count_distinct_privacy_ids_expr` in order to decide which
// contribution-bounding strategy to choose, and this cannot be computed as part
// of the partial aggregate, because that would consume additional dp-budget.
//
// Note that this function is called, after the inner per-user function calls
// have been rewritten to `init_for_dp_approx_count_distinct`.
// Suppose the original query was (adding types for clarity):
//
// SELECT WITH DIFFERENTIAL_PRIVACY (...)
// APPROX_COUNT_DISTINCT(some_col) --> INT64
// FROM some_table;
//
// Then the inner rewrite will have turned this into:
//
// SELECT WITH DIFFERENTIAL_PRIVACY (...)
// APPROX_COUNT_DISTINCT(agg_col BYTES) --> INT64
// FROM (
//   SELECT user_id, INIT_FOR_DP_APPROX_COUNT_DISTINCT(some_col) AS agg_col
//   FROM some_table
//   GROUP BY user_id
// );
//
// Taking this rewritten dp scan as input, the function below will start by
// rewriting the aggregate function call to a
// `merge_partial_for_dp_approx_count_distinct` call. Since the output type of
// `merge_partial_for_dp_approx_count_distinct` is BYTES, we need to create a
// new column with type BYTES to hold the result.
//
// SELECT WITH DIFFERENTIAL_PRIVACY (...)
// MERGE_PARTIAL_FOR_DP_APPROX_COUNT_DISTINCT(agg_col BYTES) -->
// partial_merge_col BYTES
// FROM (
//   SELECT user_id, INIT_FOR_DP_APPROX_COUNT_DISTINCT(some_col) AS agg_col
//   FROM some_table
//   GROUP BY user_id
// );
//
// Following this, we need to add a projection scan that extracts the desired
// value from the byte blob. This can be done as follows:
//
// SELECT EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT(partial_merge_col,
// noisy_count_distinct_privacy_ids_expr) --> INT64
// FROM (
//  SELECT WITH DIFFERENTIAL_PRIVACY (...)
//  MERGE_PARTIAL_FOR_DP_APPROX_COUNT_DISTINCT(agg_col BYTES) -->
//  partial_merge_col BYTES
//  FROM (
//    SELECT user_id, INIT_FOR_DP_APPROX_COUNT_DISTINCT(some_col) AS agg_col
//    FROM some_table
//    GROUP BY user_id
//  )
// );
absl::StatusOr<std::unique_ptr<ResolvedScan>>
AddProjectionScanForApproxCountDistinctAggregations(
    std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan,
    const ResolvedExpr* noisy_count_distinct_privacy_ids_expr,
    Resolver* resolver, ColumnFactory* allocator,
    AnalyzerOptions* analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  GOOGLESQL_RET_CHECK(dp_scan != nullptr);
  GOOGLESQL_RET_CHECK(noisy_count_distinct_privacy_ids_expr != nullptr);
  GOOGLESQL_RET_CHECK(resolver != nullptr);
  GOOGLESQL_RET_CHECK(allocator != nullptr);
  GOOGLESQL_RET_CHECK(analyzer_options != nullptr);
  GOOGLESQL_RET_CHECK(catalog != nullptr);
  GOOGLESQL_RET_CHECK(type_factory != nullptr);

  std::vector<std::unique_ptr<ResolvedComputedColumn>>
      projection_scan_expression_list;
  FunctionCallBuilder builder(*analyzer_options, *catalog, *type_factory);
  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
      modified_aggregate_list = dp_scan->release_aggregate_list();
  // Note: The following loop modifies the `new_aggregate_list` in place.
  for (std::unique_ptr<const ResolvedComputedColumnBase>& aggregate :
       modified_aggregate_list) {
    GOOGLESQL_RET_CHECK(aggregate->expr() != nullptr);
    GOOGLESQL_RET_CHECK(aggregate->expr()->Is<ResolvedAggregateFunctionCall>());
    const ResolvedAggregateFunctionCall* function_call =
        aggregate->expr()->GetAs<ResolvedAggregateFunctionCall>();
    // No action needed for non-approx-count-distinct aggregations.
    if (!IsApproxCountDistinct(function_call)) {
      continue;
    }

    // The following lines are where the actual rewrite happens.
    // We approach this in the following steps:
    //
    // First, we create a new `ResolvedComputedColumn`. Its expression field
    // contains the `merge_partial_for_dp_approx_count_distinct` call. The newly
    // created column is of type BYTES. We then replace the `aggregate` by that
    // new construct. At this point, we basically arrived at the third SQL
    // statement described in the function comment above.
    //
    // Second, we create an expression that extracts the desired value from the
    // byte blob. This expression will then be used after this for-loop to
    // create a projection scan. That expression is created from the
    // `noisy_count_distinct_privacy_ids_expr` and the new column of type BYTES.

    // The `public_column` is how the current `aggregate` is referenced outside
    // the original differential privacy aggregate scan. We need to copy this
    // column to the projection scan.
    ResolvedColumn public_column = aggregate->column();
    GOOGLESQL_ASSIGN_OR_RETURN(auto report_format, GetReportFormat(function_call));

    // Replace the `aggregate` given by the pair (column,
    // approx_count_distinct(...))` with
    // `(intermediate_column,
    // merge_partial_for_dp_approx_count_distinct(...)))`
    GOOGLESQL_ASSIGN_OR_RETURN(aggregate,
                     RewriteAsMergePartialForDpApproxCountDistinct(
                         function_call, noisy_count_distinct_privacy_ids_expr,
                         resolver, allocator));

    GOOGLESQL_ASSIGN_OR_RETURN(auto noisy_count_distinct_privacy_ids_expr_copy,
                     ResolvedASTDeepCopyVisitor::Copy(
                         noisy_count_distinct_privacy_ids_expr));
    GOOGLESQL_ASSIGN_OR_RETURN(auto extract_expression,
                     builder.ExtractForDpApproxCountDistinct(
                         MakeResolvedColumnRef(aggregate->column().type(),
                                               aggregate->column(), false),
                         std::move(noisy_count_distinct_privacy_ids_expr_copy),
                         report_format));
    projection_scan_expression_list.push_back(MakeResolvedComputedColumn(
        public_column, std::move(extract_expression)));
  }

  // For the rewritten DP scan, all aggregates and group by expressions are
  // propagated to the projection scan. This can be done without problems, since
  // superfluous columns will be projected away by the projection scan.
  std::vector<ResolvedColumn> dp_scan_new_column_list =
      CompileColumnListFromAggregatesAndGroupBys(modified_aggregate_list,
                                                 dp_scan->group_by_list());

  std::vector<ResolvedColumn> original_column_list = dp_scan->column_list();

  dp_scan->set_column_list(dp_scan_new_column_list);
  dp_scan->set_aggregate_list(std::move(modified_aggregate_list));
  // The projection scan has the same columns as the original DP scan.
  // This is fine as long as subsequent operations only reference these columns.
  // We expect this to remain true because referencing something else would have
  // been a syntax error in the original query.
  return MakeResolvedProjectScan(original_column_list,
                                 std::move(projection_scan_expression_list),
                                 std::move(dp_scan));
}
}  // namespace

absl::StatusOr<std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan>>
ReplaceApproxCountDistinctOfPrivacyIdCallsBySimplerAggregations(
    std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> node,
    const ResolvedExpr* uid_expr, Resolver* resolver,
    TypeFactory* type_factory) {
  GOOGLESQL_RET_CHECK(node != nullptr);
  GOOGLESQL_RET_CHECK(uid_expr != nullptr);
  GOOGLESQL_RET_CHECK(resolver != nullptr);
  GOOGLESQL_RET_CHECK(type_factory != nullptr);

  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
      modified_aggregate_list = node->release_aggregate_list();
  // Note: The following loop modifies the `new_aggregate_list` in place.
  for (std::unique_ptr<const ResolvedComputedColumnBase>& aggregate :
       modified_aggregate_list) {
    GOOGLESQL_RET_CHECK(aggregate->expr() != nullptr);
    GOOGLESQL_RET_CHECK(aggregate->expr()->Is<ResolvedAggregateFunctionCall>());

    const ResolvedAggregateFunctionCall* function_call =
        aggregate->expr()->template GetAs<ResolvedAggregateFunctionCall>();

    // Keep non-approx-count-distinct aggregations as is.
    if (!IsApproxCountDistinct(function_call)) {
      continue;
    }

    GOOGLESQL_RET_CHECK_GE(function_call->argument_list_size(), 1);
    const std::unique_ptr<const ResolvedExpr>& aggregation_expr =
        function_call->argument_list()[0];

    // Determine if the aggregation is counting privacy ids, by comparing the
    // corresponding expressions for equality.
    // Note: The general problem of checking if two expressions/functions are
    // equal (in the sense of computing the same thing) is undecidable. So we
    // must resort to a reasonable heuristic here.
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_counting_privacy_ids,
                     ResolvedASTComparator::CompareResolvedAST(
                         uid_expr, aggregation_expr.get()));
    // Keep count-distinct-non-privacy-ids as is.
    if (!is_counting_privacy_ids) {
      continue;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> replacement_call,
                     MakeCountDistinctPrivacyIdsFunctionCall(
                         function_call, resolver, type_factory));

    aggregate = MakeResolvedComputedColumn(aggregate->column(),
                                           std::move(replacement_call));
  }
  node->set_aggregate_list(std::move(modified_aggregate_list));
  return node;
}

absl::StatusOr<bool> HasApproxCountDistinctAggregation(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        aggregate_list) {
  for (const auto& aggregate : aggregate_list) {
    GOOGLESQL_RET_CHECK(aggregate->expr() != nullptr);
    GOOGLESQL_RET_CHECK(aggregate->expr()->Is<ResolvedAggregateFunctionCall>());
    if (IsApproxCountDistinct(
            aggregate->expr()->GetAs<ResolvedAggregateFunctionCall>())) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
PerformApproxCountDistinctRewrites(
    std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan,
    const ResolvedExpr* noisy_count_distinct_privacy_ids_expr,
    bool has_approx_count_distinct, Resolver* resolver,
    ColumnFactory* allocator, AnalyzerOptions* analyzer_options,
    Catalog* catalog, TypeFactory* type_factory) {
  if (!has_approx_count_distinct) {
    return dp_scan;
  }
  return AddProjectionScanForApproxCountDistinctAggregations(
      std::move(dp_scan), noisy_count_distinct_privacy_ids_expr, resolver,
      allocator, analyzer_options, catalog, type_factory);
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
PerformApproxCountDistinctRewrites(
    std::unique_ptr<ResolvedAnonymizedAggregateScan> dp_scan,
    const ResolvedExpr* noisy_count_distinct_privacy_ids_expr,
    bool has_approx_count_distinct, Resolver* resolver,
    ColumnFactory* allocator, AnalyzerOptions* analyzer_options,
    Catalog* catalog, TypeFactory* type_factory) {
  // APPROX_COUNT_DISTINCT is not supported by the deprecated
  // SELECT WITH ANONYMIZATION syntax, and thus should not be resolved.
  GOOGLESQL_RET_CHECK(!has_approx_count_distinct);
  return dp_scan;
}

}  // namespace approx_count_distinct_utility
}  // namespace differential_privacy
}  // namespace googlesql
