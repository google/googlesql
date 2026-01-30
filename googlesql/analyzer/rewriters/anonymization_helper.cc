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

#include "googlesql/analyzer/rewriters/anonymization_helper.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/rewriters/privacy/approx_count_distinct_utility.h"
#include "googlesql/analyzer/rewriters/privacy/privacy_utility.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/proto/anon_output_with_report.pb.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/anon_function.h"
#include "googlesql/public/anonymization_utils.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

using googlesql::functions::DifferentialPrivacyEnums;

struct PrivacyOptionSpec;
struct WithEntryRewriteState;
struct RewritePerUserTransformResult;
struct RewriteInnerAggregateListResult;
struct UidColumnState;
class OuterAggregateListRewriterVisitor;

// Used for generating correct error messages for SELECT WITH ANONYMIZATION and
// WITH DIFFERENTIAL_PRIVACY.
struct WithModifierModeName {
  absl::string_view name;
  // Article used with name if true should use `a`.
  bool uses_a_article;
};

constexpr absl::string_view kPublicGroupsWithNamePrefix = "$public_groups";
constexpr absl::string_view kGroupSelectionOptionName =
    "group_selection_epsilon";

STATIC_IDSTRING(kContributionBoundingStrategy,
                "contribution_bounding_strategy");
STATIC_IDSTRING(kContributionBoundsPerGroup, "contribution_bounds_per_group");
STATIC_IDSTRING(kEpsilon, "epsilon");
STATIC_IDSTRING(kMaxContributionsPerGroup, "max_contributions_per_group");
STATIC_IDSTRING(kReportFormat, "report_format");

// Keeps global state for the public groups feature to provide unique query
// names for newly added `WithScan` entries across multiple anon / dp aggregate
// scans.
//
// The name of a `WithEntry` must be unique across the query.
class GlobalPublicGroupsQueryNameProvider {
 public:
  // Returns the next name of an otherwise unnamed WithEntry query name.
  std::string GetNextPublicGroupsWithQueryName() {
    const std::string query_name =
        absl::StrCat(kPublicGroupsWithNamePrefix, next_query_name_number_);
    ++next_query_name_number_;
    return query_name;
  }

 private:
  int64_t next_query_name_number_ = 0;
};

// Keeps state for using the public groups feature. This class will be invoked
// by the PerUserRewriterVisitor to check and add joins with the public groups
// table. It will be invoked from the AnonymizationRewriter to ensure all
// required joins for public groups are present.
//
// It will rewrite public group joins and, in case max_groups_contributed is
// set, will add an additional join after the SampleScan. In case it adds this
// additional join, it also provides functionality to add a CTE for the public
// groups subquery.
//
// See (broken link) and (broken link).
//
// Example:
//
// Given a resolved AST of the following shape:
//   AnonymizedAggregateScan
//   -> input_scan =
//      JoinScan
//       -> join_type = RIGHT
//       -> left_scan = TableScan with user id
//       -> right_scan =
//              (SELECT DISTINCT public_value from public_groups_table)
//   -> group_by_list = (public group columns)
//   -> anonymization_option_list =
//      -> group_selection_strategy = PUBLIC_GROUPS
//      -> max_groups_contributed = 3
//   -> aggregate_list = ANON_COUNT(*)
//
// We will:
// 1. .. identify the JoinScan as public groups join and the right_scan as the
//    public groups scan.
// 2. .. replace the right_scan of this JoinScan with a CTE and make this join
//    an inner join.
// 3. .. add another join with the same CTE after the SampleScan that will be
//    introduced during the rewrite. This join will be an outer join and the
//    columns of the join condition must be modified.
//
// The rewritten AST of the above AST will have the following shape:
//   WithScan
//   -> with_entry_list =
//        WithEntry
//        -> with_query_name = "$public_groups0"
//        -> with_subquery =
//           (SELECT DISTINCT public_value from public_groups_table)
//   -> query =
//        AnonymizedAggregateScan
//        -> input_scan =
//           JoinScan
//           -> join_type = RIGHT
//           -> left_scan =
//              SampleScan
//              -> input_scan =
//                 AggregateScan
//                 -> input_scan =
//                    JoinScan
//                    -> join_type = INNER
//                    -> left_scan = TableScan with user id
//                    -> right_scan = $public_groups0
//           -> right_scan = $public_groups0
//        -> aggregate_list = ANON_SUM(1)
//        -> anonymization_option_list =
//           -> group_selection_strategy = PUBLIC_GROUPS
//
class PublicGroupsState {
 public:
  // Creates a public groups state from the group by list of the anonymization
  // aggregate scan.
  //
  // When bound_contributions_across_groups is true, a second join has to be
  // introduced after the table scan (see CreateJoinScanAfterSampleScan). This
  // means that all public group joins need to be moved to a CTE, and we need to
  // track columns in the join condition, so that we can match them for this
  // join.
  static absl::StatusOr<std::unique_ptr<PublicGroupsState>>
  CreateFromGroupByList(
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
          group_by_list,
      bool bound_contributions_across_groups, absl::string_view error_prefix,
      GlobalPublicGroupsQueryNameProvider*
          global_public_groups_query_name_provider);

  // Rewrites an existing copy of the resolved join scan with the modifications
  // required for the public groups feature if the shape satisfies the public
  // groups constraints.
  //
  // The public group join constraints are:
  // 1. The join must be of type left or right outer join. We call the side that
  //    is preserved the public groups scan.
  // 2. The public groups scan must be a SELECT DISTINCT sub-query. The columns
  //    must match the columns of the group-by list of the anonymization query.
  // 3. The public groups scan must *not* have a privacy unit.
  // 4. The other side must have a privacy unit.
  //
  // Returns:
  // * The UidColumnState that should be used after this join. In this case,
  //   the join was a public groups join and the passed join is rewritten.
  // * std::nullopt, in case the join is not a public groups join, i.e., does
  //   not fulfill the public groups requirements. In this case, the join is
  //   unmodified.
  absl::StatusOr<std::optional<UidColumnState>>
  MaybeRewritePublicGroupsJoinAndReturnUid(
      const UidColumnState& left_scan_uid_state,
      const UidColumnState& right_scan_uid_state,
      absl::Span<const std::unique_ptr<WithEntryRewriteState>> with_entries,
      ResolvedJoinScan* join);

  // Tracks column replacements, so that the join conditions can be rewritten
  // properly. We will keep an internal data structure for replacing the
  // columns in the join condition of the additional outer join that is added
  // after the sample scan.
  void TrackColumnReplacement(const ResolvedColumn& old_column,
                              const ResolvedColumn& new_column);

  // Returns an error status in case some joins that are required for public
  // groups were not present in the query. To be called after all relevant
  // joins were passed to MaybeRewritePublicGroupsJoinAndReturnUid.
  absl::Status ValidateAllRequiredJoinsAdded(
      absl::string_view error_prefix) const;

  // Adds a join after the sample scan in case we bound the contribution across
  // groups.
  absl::StatusOr<std::unique_ptr<ResolvedScan>> CreateJoinScanAfterSampleScan(
      std::unique_ptr<ResolvedScan> sample_scan, ColumnFactory& column_factory);

  // Adds a resolved CTE for each of the public group scans, if there were any.
  absl::StatusOr<std::unique_ptr<const ResolvedScan>>
  WrapScanWithPublicGroupsWithEntries(
      ColumnFactory* column_factory,
      std::unique_ptr<const ResolvedScan> resolved_scan) const;

  // Returns columns from the user-written join to the added join after the
  // sample scan.
  //
  // REQUIRES: Can only be called after adding the additional join, i.e., after
  // CreateJoinScanAfterSampleScan has been called.
  std::optional<ResolvedColumn> FindPublicGroupColumnForAddedJoin(
      const ResolvedColumn& user_join_column) {
    if (original_join_column_to_added_join_column_.contains(user_join_column)) {
      return original_join_column_to_added_join_column_.at(user_join_column);
    }
    return std::nullopt;
  }

  absl::StatusOr<std::vector<std::unique_ptr<ResolvedWithEntry>>>
  GetPublicGroupScansAsWithEntries(ColumnFactory* column_factory) const;

 private:
  PublicGroupsState(
      const absl::flat_hash_map<int, const ResolvedColumn>& column_id_map,
      const absl::flat_hash_set<int>& public_group_column_ids,
      bool bound_contributions_across_groups,
      GlobalPublicGroupsQueryNameProvider*
          global_public_groups_query_name_provider)
      : column_id_map_(column_id_map),
        public_group_column_ids_(public_group_column_ids),
        unjoined_column_ids_(public_group_column_ids),
        bound_contributions_across_groups_(bound_contributions_across_groups),
        global_public_groups_query_name_provider_(
            global_public_groups_query_name_provider) {}

  // Copies relevant information about the scan and the join conditions, so that
  // they can be later added as CTE. This method also ensures that columns in
  // the join_expr are tracked, so that they can be replaced.
  //
  // Returns with_query_name of the WithScan that will be added just before
  // the ResolvedAnonScan.
  absl::StatusOr<std::string> RecordPublicGroupsWithScan(
      const ResolvedScan* public_groups_scan, const ResolvedExpr* join_expr);

  // Marks the passed columns as visited and joined properly for the required
  // joins.
  void MarkPublicGroupColumnsAsVisited(
      absl::Span<const ResolvedColumn> column_list) {
    for (const ResolvedColumn& column : column_list) {
      unjoined_column_ids_.erase(column.column_id());
    }
  }

  // Returns a pointer to the ResolvedScan in the provided ResolvedJoinScan,
  // which contains a public groups scan. Returns a nullptr in case the join is
  // not a public groups join.
  const ResolvedScan* GetPublicGroupsScanOrNull(
      const ResolvedJoinScan* node,
      absl::Span<const std::unique_ptr<WithEntryRewriteState>> with_entries)
      const;

  // Contains more information for the columns in the following sets. This map
  // is keyed by column id. Only populated when group selection strategy is
  // public groups and there is at least one element in the group-by list.
  absl::flat_hash_map<int, const ResolvedColumn> column_id_map_;
  // Immutable set containing the column ids of public group columns, i.e., the
  // columns in the group-by list. Only populated when group selection strategy
  // is public groups and there is at least one element in the group-by list.
  const absl::flat_hash_set<int> public_group_column_ids_;
  // Contains the column ids for columns, for which no public group join has
  // been identified yet. Initially is a copy of the public_group_column_ids_.
  // Only populated when group selection strategy is public groups and there are
  // still required public group joins that have not been identified.
  absl::flat_hash_set<int> unjoined_column_ids_;
  const bool bound_contributions_across_groups_;

  struct PublicGroupsWithScan {
    // The name of the CTE.
    //
    // * If with_scan_already_present = false, we will use a unique name from
    //   the global public groups state.
    // * Otherwise this will be the user-given query name of the CTE.
    std::string with_query_name;

    std::unique_ptr<ResolvedScan> public_groups_scan;
    std::unique_ptr<ResolvedExpr> join_expr;

    // Indicates that the public groups join has been defined by the user as a
    // CTE and does not need to be added as a CTE.
    bool with_scan_already_present;
  };
  std::vector<PublicGroupsWithScan> with_scans_;

  // Mapping from the original user-written join to the added outer join. Is
  // only populated after CreateJoinScanAfterSampleScan has been called.
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn>
      original_join_column_to_added_join_column_;

  // Mapping of columns to an equivalent column in the `join_expr` in any of the
  // `with_scans_`.
  absl::flat_hash_map<int, int> column_to_join_column_id_;

  GlobalPublicGroupsQueryNameProvider*
      global_public_groups_query_name_provider_;  // unowned
};

std::string CreateOptionNotAllowedWithPublicGroupsError(
    absl::string_view operation = "") {
  return absl::Substitute(
      "group_selection_strategy = PUBLIC_GROUPS does not allow $0$1operations "
      "between the public groups join and the aggregation, because they could "
      "suppress public groups from the result. Try moving the operation to an "
      "input subquery of the public groups join.",
      operation, operation.empty() ? "" : " ");
}

// The `AddGroupSelectionThreshold` function of the `RewriterVisitor` class
// adds the necessary aggregate columns to the anonymized/differentially private
// aggregate scan to enable thresholding per group.
//
// Logically, thresholding is based on the exact number of distinct privacy
// units per group (pre-thresholding) and the noisy count of distinct privacy
// units per group. For optimization reasons, these quantities are not
// necessarily computed as a separate column, but may be computed from the
// existing columns.
struct AddGroupSelectionThresholdResult {
  // The expression that is used to compute the quantity that is compared to
  // the threshold via: `group_selection_threshold_expr >= threshold`.
  // This will later be passed on as the `group_selection_threshold_expr`
  // argument of the `ResolvedDifferentiallyPrivateAggregateScan`. As part of
  // the expression, it references results of the aggregate functions.
  std::unique_ptr<ResolvedExpr> group_selection_threshold_expr;
  // Expression that evaluates to the noisy count of distinct privacy units per
  // group. This expression may be different from the
  // `group_selection_threshold_expr`. It references the results of the
  // aggregate functions of the `ResolvedDifferentiallyPrivateAggregateScan`.
  // This expression is always created, but only used if the aggregates contain
  // an `approx_count_distinct` aggregate function. Otherwise it is ignored by
  // the calling code.
  std::unique_ptr<ResolvedExpr> noisy_count_distinct_privacy_ids_expr;
};

// The result of the `BoundGroupsContributedToInputScan` function of the
// `RewriterVisitor` class.
// Contains the rewritten scan (i.e., the one that contains the rewrites needed
// to count the user contributions).
template <typename NodeType>
struct BoundGroupsContributedToInputScanResult {
  // A scan that contains the rewrites necessary for differential privacy. In
  // particular, it contains the inner per-user aggregate scan, and potentially
  // further aggregates the are needed to count distinct users per group (see
  // `BoundGroupsContributedToInput` for more details.)
  std::unique_ptr<NodeType> rewritten_scan;
  // An expression that can be used to compute the noisy count of distinct
  // privacy units per group. In general, the expression may be `nullptr` if
  // counting distinct privacy units is not necessary for the given
  // configuration of the query (for details about when this is the case, see
  // `BoundGroupsContributedToInput`). This expression is currently only used if
  // the aggregates contain an `approx_count_distinct` aggregate function. It is
  // otherwise ignored by the calling code.
  std::unique_ptr<ResolvedExpr> noisy_count_distinct_privacy_ids_expr;
};

// Rewrites a given AST that includes a ResolvedAnonymizedAggregateScan to use
// the semantics defined in https://arxiv.org/abs/1909.01917 and
// (broken link).
//
// Overview of the rewrite process:
// 1. This class is invoked on an AST node, blindly copying everything until a
//    ResolvedAnonymizedAggregateScan (anon node) is hit
// 2. Every column in the anon node's column list is recorded in a map entry
//    with a freshly allocated column of the same type in the entry's value
//    (the intermediate columns)
// 3. The per-user ResolvedAggregateScan is created using this map:
//   a. The original anon node's input scan is validated to partition by $uid,
//      and project the $uid column up to the top column list
//   b. The projected $uid column is added to the GROUP BY list if not already
//      included.
//   c. Each ANON_* function call in the anon node is re-resolved to the
//      appropriate per-user aggregate function, e.g. ANON_SUM(expr)->SUM(expr)
//   d. For each aggregate or group by column in the anon node, the column set
//      in the per-user scan's column list is the appropriate intermediate
//      column looked up in the column map
// 4. Bound user contributions. The method of contribution bounding depends on
//    which of the mutually exclusive parameters max_groups_contributed (aka
//    kappa) and max_rows_contributed were specified.
//   - If neither was specified, then the rewriter will use some default
//     contribution bounding. The default contribution bounding is determined by
//     the rewriter and might change in future.
//   - Setting either max_rows_contributed=NULL or max_groups_contributed=NULL
//     disables cross-group contribution bounding, while still limiting
//     contributions within a group. For most queries, if you disable
//     contribution bounding then you WILL NOT get a query that is
//     differentially private with the requested privacy unit; instead, the
//     query will effectively have a privacy unit of <$uid, GROUP BY key>.
//   - Setting max_groups_contributed to a positive value causes the rewriter to
//     insert a ResolvedSampleScan (partitioned by $uid and the GROUP BY keys)
//     to limit the number of groups that a user can contribute to.
//   - Setting max_rows_contributed to a positive value causes the rewriter to
//     insert a ResolvedSampleScan (partitioned by $uid) to limit the number of
//     rows that a user can contribute to the dataset.
// 5. The final cross-user ResolvedAnonymizedAggregateScan is created:
//   a. The input scan is set to the (possibly sampled) per-user scan
//   b. The first argument for each ANON_* function call in the anon node is
//      re-resolved to point to the appropriate intermediate column
//   c. If the `group_selection_strategy` option not set to PUBLIC_GROUPS, then
//      a group selection threshold expression is added. Otherwise, it's not.
//      The `group_selection_threshold_expr` is computed for every
//      group that may appear in the output. Whether or not a group actually
//      appears in the output is determined by the engine. In particular,
//      engines are supposed to keep a group if and only if
//      `group_selection_threshold_expr >= group_selection_threshold`, where
//      `group_selection_threshold` is implementation-defined and usually
//      computed from the ResolvedAnonymizedAggregateScan's options.
//      The `group_selection_threshold_expr` is determined as follows:
//      CASE 1) If the `min_privacy_units_per_group` option is unspecified or
//         null, `group_selection_threshold_expr` means "noisy count of distinct
//         privacy units per group". Since the input_scan to the step (5) is a
//         per-user aggregate scan, it can be computed as follows (or any of its
//         equivalent functions):
//             ANON_SUM(1 CLAMPED BETWEEN 0 AND 1)
//      CASE 2) If `min_privacy_units_per_group` is specified, the meaning of
//         `group_selection_threshold_expr` does not change but it needs to pass
//         two conditions to have a non-zero value:
//          - the exact count of distinct privacy units per group needs to be
//         greater than or equal to `min_privacy_units_per_group`
//          - the difference between
//          `noisy_count_distinct_privacy_units_per_group`
//         and the `min_privacy_units_per_group` threshold is at least as large
//         as the difference between `group_selection_threshold` and 1
//         After combining the two conditions, it can be computed as follows:
//             IF(exact_count_distinct_privacy_units_per_group >=
//                  min_privacy_units_per_group,
//                noisy_count_distinct_privacy_units_per_group -
//                  (min_privacy_units_per_group - 1),
//                NULL)
//
// If we consider the scans in the original AST as a linked list as:
//
// cross_user_transform
//  -> ResolvedAnonymizedAggregateScan
//    -> per_user_transform
//
// Then the above operations can be thought of as inserting a pair of new list
// nodes:
//
// cross_user_transform
//  -> ResolvedAnonymizedAggregateScan
//    -> ResolvedSampleScan (optional)
//      -> ResolvedAggregateScan
//        -> per_user_transform
//
// Where the new ResolvedAggregateScan is the per-user aggregate scan, and
// the optional ResolvedSampleScan uses max_groups_contributed to restrict the
// number of groups a user can contribute to (for more information on
// max_groups_contributed, see (broken link)).
class RewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  RewriterVisitor(ColumnFactory* allocator, TypeFactory* type_factory,
                  Resolver* resolver, Catalog* catalog,
                  AnalyzerOptions* options)
      : allocator_(allocator),
        type_factory_(type_factory),
        resolver_(resolver),
        catalog_(catalog),
        analyzer_options_(options) {}

 private:
  // Chooses one of the uid columns between per_user_visitor_uid_column and
  // options_uid. If both are present returns an error if none is present
  // returns an error.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ChooseUidColumn(
      const ResolvedAggregateScanBase* node,
      WithModifierModeName with_modifier_mode_name,
      const UidColumnState& per_user_visitor_uid_column_state,
      std::optional<const ResolvedExpr*> options_uid_column);

  // Rewrites node using the PerUserRewriterVisitor.
  //
  // Constructs a deep copy of the input scan, attempting to identify a UID
  // column while rewriting.
  absl::StatusOr<RewritePerUserTransformResult> RewritePerUserTransform(
      const ResolvedAggregateScanBase* node,
      WithModifierModeName with_modifier_mode_name,
      std::optional<const ResolvedExpr*> options_uid_column,
      PublicGroupsState* public_groups_state);

  // Rewrites node using InnerAggregateListRewriterVisitor.
  //
  // Rewrites aggregates and group by columns, constructing a map from the old
  // columns to the rewritten columns.
  absl::StatusOr<RewriteInnerAggregateListResult> RewriteInnerAggregateList(
      const ResolvedAggregateScanBase* node,
      WithModifierModeName with_modifier_mode_name,
      std::unique_ptr<const ResolvedExpr> inner_uid_column);

  // If order_by_column is initialized, then wraps node in a ProjectScan that
  // additionally projects a rand() for use in choosing a random subset of the
  // partition.
  //
  // This method is only used when bounding L0 sensitivity / max groups
  // contributed. We sample a limited number of elements when computing the
  // per-partition aggregation for
  // ANON_VAR_POP/ANON_STDDEV_POP/ANON_PERCENTILE_CONT, and we use this rand()
  // to choose a random subset of the partition's contributions.
  absl::StatusOr<std::unique_ptr<ResolvedScan>>
  ProjectOrderByForSampledAggregation(std::unique_ptr<ResolvedScan> node,
                                      const ResolvedColumn& order_by_column);

  // Rewrites the node contained in `rewritten_per_user_transform`,
  // pre-aggregating each user's contributions to each group, and potentially
  // adding further columns to the aggregate scan to count distinct users. The
  // result of this rewrite will be returned as the `rewriten_scan` field.
  //
  // If `privacy_option_spec` says to bound the number of groups to which a
  // privacy unit can contribute, then this method will also insert a
  // `SampleScan` to do so.
  //
  // The second return value `noisy_count_distinct_privacy_ids_expr` is the
  // expression that can be used to compute the noisy count of distinct privacy
  // ids per group. This expression may or may not be present. That latter case
  // is indicated by a `nullptr` value. The expression is present under the
  // following circumstances:
  // - If the `group_selection_strategy` is not `PUBLIC_GROUPS`, then the
  // expression is always guaranteed to be present. If a suitable aggregate
  // function to reference doesn't already exist, a dedicated column is added to
  // the aggregate list to compute the noisy count of distinct privacy ids per
  // group (see (broken link) for details).
  // - If the `group_selection_strategy` is `PUBLIC_GROUPS` then the expression
  // is only guaranteed to be present if the
  // `always_create_count_distinct_privacy_ids_column` parameter is set to true.
  // If that latter flag is set, and a suitable aggregate function to reference
  // doesn't already exist, then the present function will add a column that
  // counts distinct privacy ids. The returned expression is currently only used
  // if the aggregates contain an `approx_count_distinct` aggregate function.
  // Otherwise it is ignored by the calling code.
  template <class NodeType>
  absl::StatusOr<BoundGroupsContributedToInputScanResult<NodeType>>
  BoundGroupsContributedToInputScan(
      std::unique_ptr<NodeType> input_scan,
      RewritePerUserTransformResult rewritten_per_user_transform,
      const PrivacyOptionSpec& privacy_option_spec,
      PublicGroupsState* public_groups_state,
      bool always_create_count_distinct_privacy_ids_column);

  // Rewrites the node contained in rewritten_per_user_transform, inserting a
  // `SampleScan` to bound the number of rows that a user can contribute.
  template <class NodeType>
  absl::StatusOr<std::unique_ptr<NodeType>> BoundRowsContributedToInputScan(
      const NodeType* original_input_scan,
      RewritePerUserTransformResult rewritten_per_user_transform,
      int64_t max_rows_contributed, bool filter_values_with_null_uid);

  // Returns a reference to a column containing the count of unique users
  // (accounting for the different report types like JSON or proto). If this is
  // not possible (e.g. unique_users_count_column is not initialized or a
  // required feature is not enabled), then returns nullptr.
  template <class NodeType>
  absl::StatusOr<std::unique_ptr<ResolvedExpr>>
  ExtractGroupSelectionThresholdExpr(
      const ResolvedColumn& unique_users_count_column);

  // Create the cross-user group selection threshold function call. It is called
  // k_threshold for ResolvedAnonymizedAggregateScan but the name got updated to
  // group selection threshold see: (broken link).
  absl::StatusOr<std::unique_ptr<ResolvedComputedColumn>>
  MakeGroupSelectionThresholdFunctionColumn(
      const ResolvedAnonymizedAggregateScan* scan_node);
  absl::StatusOr<std::unique_ptr<ResolvedComputedColumn>>
  MakeGroupSelectionThresholdFunctionColumn(
      const ResolvedDifferentialPrivacyAggregateScan* scan_node);

  // Ensures that the number of distinct privacy ids per group is counted
  // somewhere in the query. It's checked if the noisy count of distinct privacy
  // ids is already (implicitly) part of the output. If so, the function returns
  // a column reference to that output.  If not, a new column containing the
  // number of distinct privacy ids per group is created. A reference to that
  // column is returned.
  template <class NodeType>
  absl::StatusOr<std::unique_ptr<ResolvedExpr>>
  IdentifyOrAddNoisyCountDistinctPrivacyIdsColumnToAggregateList(
      const NodeType* original_input_scan,
      const OuterAggregateListRewriterVisitor& outer_rewriter_visitor,
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>&
          outer_aggregate_list);

  // Adds a new column containing the exact number of distinct privacy ids per
  // group to the outer_aggregate_list. A reference to that column is returned.
  absl::StatusOr<std::unique_ptr<ResolvedExpr>>
  AddExactCountDistinctPrivacyIdsColumnToAggregateList(
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>&
          outer_aggregate_list);

  // Returns two expressions: the first is the group selection threshold
  // expression, and the second is the noisy count of distinct privacy ids.
  //
  // The group selection threshold expression is the expression that should be
  // added to the DP aggregate scan as the `group_selection_threshold_expr`.
  //
  // The noisy count of distinct privacy ids is the expression that can be used
  // to compute the noisy count of distinct privacy ids per group. This is
  // currently only used for `approx_count_distinct`.
  //
  // As a side effect, this function may add additional columns to the
  // `outer_aggregate_list` if they are required to compute the
  // `group_selection_threshold_expr` or `noisy_count_distinct_privacy_ids`.
  template <typename NodeType>
  absl::StatusOr<AddGroupSelectionThresholdResult>
  AddGroupSelectionThresholding(
      const NodeType* original_input_scan,
      const OuterAggregateListRewriterVisitor& outer_rewriter_visitor,
      std::optional<int64_t> min_privacy_units_per_group,
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>&
          outer_aggregate_list);

  // Creates a new `ResolvedComputedColumn` that counts distinct user IDs,
  // appends it to the output argument `aggregate_list`, and returns an
  // expression that refers to the newly created column.
  template <class NodeType>
  absl::StatusOr<std::unique_ptr<ResolvedExpr>>
  CreateCountDistinctPrivacyIdsColumn(
      const NodeType* original_input_scan,
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>& aggregate_list);

  std::unique_ptr<ResolvedAnonymizedAggregateScan> CreateAggregateScan(
      const ResolvedAnonymizedAggregateScan* node,
      std::unique_ptr<ResolvedScan> input_scan,
      std::vector<std::unique_ptr<ResolvedComputedColumn>> outer_group_by_list,
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
          outer_aggregate_list,
      std::unique_ptr<ResolvedExpr> group_selection_threshold_expr,
      std::vector<std::unique_ptr<ResolvedOption>> resolved_options);

  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> CreateAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* node,
      std::unique_ptr<ResolvedScan> input_scan,
      std::vector<std::unique_ptr<ResolvedComputedColumn>> outer_group_by_list,
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
          outer_aggregate_list,
      std::unique_ptr<ResolvedExpr> group_selection_threshold_expr,
      std::vector<std::unique_ptr<ResolvedOption>> resolved_options);

  // Wraps input_scan with a sample scan that bounds the number of partitions
  // that a user contributes to.
  //
  // This will only provide epsilon-delta dataset level differential privacy
  // when the query includes a GROUP BY clause.
  //
  // If max_groups_contributed is a nullopt, then we add a
  // SampleScan using default_anon_kappa_value.
  absl::StatusOr<std::unique_ptr<ResolvedScan>> AddCrossPartitionSampleScan(
      std::unique_ptr<ResolvedScan> input_scan,
      std::optional<int64_t> max_groups_contributed, ResolvedColumn uid_column,
      PublicGroupsState* public_groups_state);

  // Wraps input_scan with a sample scan that bounds the number of rows that a
  // user can contribute to the dataset.
  absl::StatusOr<std::unique_ptr<ResolvedScan>> AddCrossContributionSampleScan(
      std::unique_ptr<ResolvedScan> input_scan, int64_t max_rows_contributed,
      std::unique_ptr<const ResolvedExpr> uid_column);

  absl::Status VisitResolvedAnonymizedAggregateScan(
      const ResolvedAnonymizedAggregateScan* node) override {
    return VisitResolvedDifferentialPrivacyAggregateScanTemplate(node);
  }
  absl::Status VisitResolvedDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* node) override {
    return VisitResolvedDifferentialPrivacyAggregateScanTemplate(node);
  }

  template <class NodeType>
  absl::Status VisitResolvedDifferentialPrivacyAggregateScanTemplate(
      const NodeType* node);

  absl::Status VisitResolvedAggregationThresholdAggregateScan(
      const ResolvedAggregationThresholdAggregateScan* node) override;

  absl::Status VisitResolvedWithScan(const ResolvedWithScan* node) override;
  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override;

  absl::Status AttachExtraNodeFields(const ResolvedScan& original,
                                     ResolvedScan& copy);

  ColumnFactory* allocator_;           // unowned
  TypeFactory* type_factory_;          // unowned
  Resolver* resolver_;                 // unowned
  Catalog* catalog_;                   // unowned
  AnalyzerOptions* analyzer_options_;  // unowned

  // Public groups state that is kept over multiple anonymization / differential
  // privacy aggregate scans.
  GlobalPublicGroupsQueryNameProvider global_public_groups_query_name_provider_;

  std::vector<std::unique_ptr<WithEntryRewriteState>> with_entries_;
};

class ColumnReplacingDeepCopyVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  explicit ColumnReplacingDeepCopyVisitor(
      const absl::flat_hash_map<ResolvedColumn, ResolvedColumn>*
          unowned_column_replacament)
      : unowned_column_replacement_(unowned_column_replacament) {}

  absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
      const ResolvedColumn& column) override {
    if (unowned_column_replacement_->contains(column)) {
      return unowned_column_replacement_->at(column);
    }
    return column;
  }

 private:
  const absl::flat_hash_map<ResolvedColumn, ResolvedColumn>*
      unowned_column_replacement_;
};

absl::Status MaybeAttachParseLocation(absl::Status status,
                                      const ResolvedNode& node) {
  const auto* parse_location = node.GetParseLocationRangeOrNULL();
  if (!status.ok() &&
      !googlesql::internal::HasPayloadWithType<InternalErrorLocation>(status) &&
      parse_location != nullptr) {
    googlesql::internal::AttachPayload(
        &status, parse_location->start().ToInternalErrorLocation());
  }
  return status;
}

// Return true if the internal implementation of differential privacy function
// uses array type as an input.
bool HasInnerAggregateArray(int64_t signature_id) {
  switch (signature_id) {
    case FunctionSignatureId::FN_ANON_VAR_POP_DOUBLE:
    case FunctionSignatureId::FN_ANON_STDDEV_POP_DOUBLE:
    case FunctionSignatureId::FN_ANON_PERCENTILE_CONT_DOUBLE:
    case FunctionSignatureId::FN_ANON_QUANTILES_DOUBLE:
    case FunctionSignatureId::FN_ANON_QUANTILES_DOUBLE_WITH_REPORT_JSON:
    case FunctionSignatureId::FN_ANON_QUANTILES_DOUBLE_WITH_REPORT_PROTO:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_VAR_POP_DOUBLE:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_STDDEV_POP_DOUBLE:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_PERCENTILE_CONT_DOUBLE:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE:
    case FunctionSignatureId::
        FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_REPORT_JSON:
    case FunctionSignatureId::
        FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_REPORT_PROTO:
      return true;
    default:
      return false;
  }
}

// Returns true if the signature_id is the signature of an aggregate function
// that is a variant of a COUNT(*) aggregation.
constexpr bool IsCountStarFunction(int64_t signature_id) {
  switch (signature_id) {
    case FunctionSignatureId::FN_ANON_COUNT_STAR:
    case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_JSON:
    case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_PROTO:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_JSON:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_PROTO:
      return true;

    default:
      return false;
  }
}

// Given a call to an ANON_* function, resolve a concrete function signature for
// the matching per-user aggregate call. For example,
// ANON_COUNT(expr, 0, 1) -> COUNT(expr)
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
ResolveInnerAggregateFunctionCallForAnonFunction(
    const ResolvedAggregateFunctionCall* node,
    std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
    Resolver* resolver, ResolvedColumn* order_by_column,
    ColumnFactory* allocator, absl::string_view select_with_identifier) {
  if (!node->function()->Is<AnonFunction>()) {
    return MakeSqlErrorAtNode(*node)
           << "Unsupported function in " << select_with_identifier
           << " clause: " << node->function()->SQLName();
  }

  if (node->function()->GetGroup() == Function::kGoogleSQLFunctionGroupName &&
      IsCountStarFunction(node->signature().context_id())) {
    // COUNT(*) doesn't take any arguments.
    arguments.clear();
  } else {
    arguments.resize(1);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> result,
      ResolveFunctionCall(
          node->function()->GetAs<AnonFunction>()->GetPartialAggregateName(),
          std::move(arguments), /*named_arguments=*/{}, resolver));

  // If the anon function is an anon array function, we allocate a new column
  // "$orderbycol1" and set the limit as 5.
  if (node->function()->GetGroup() == Function::kGoogleSQLFunctionGroupName &&
      HasInnerAggregateArray(node->signature().context_id())) {
    if (!order_by_column->IsInitialized()) {
      *order_by_column =
          allocator->MakeCol("$orderby", "$orderbycol1", types::DoubleType());
    }
    std::unique_ptr<const ResolvedColumnRef> resolved_column_ref =
        BuildResolvedColumnRef(*order_by_column);
    std::unique_ptr<const ResolvedOrderByItem> resolved_order_by_item =
        MakeResolvedOrderByItem(std::move(resolved_column_ref), nullptr,
                                /*is_descending=*/false,
                                ResolvedOrderByItemEnums::ORDER_UNSPECIFIED);

    ResolvedAggregateFunctionCall* resolved_aggregate_function_call =
        result->GetAs<ResolvedAggregateFunctionCall>();
    resolved_aggregate_function_call->add_order_by_item_list(
        std::move(resolved_order_by_item));
    resolved_aggregate_function_call->set_null_handling_modifier(
        ResolvedNonScalarFunctionCallBaseEnums::IGNORE_NULLS);
    resolved_aggregate_function_call->set_limit(MakeResolvedLiteral(
        Value::Int64(anonymization::kPerUserArrayAggLimit)));
  }
  return result;
}

// Used to validate expression subqueries nested under an anonymizization /
// differential privacy node. Rejects nested anonymization operations and reads
// of user data based on (broken link).
class ExpressionSubqueryRewriterVisitor : public ResolvedASTDeepCopyVisitor {
  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    if (node->table()->SupportsAnonymization()) {
      return MakeSqlErrorAtNode(*node)
             << "Reading the table " << node->table()->Name()
             << " containing user data in expression subqueries is not allowed";
    }
    return CopyVisitResolvedTableScan(node);
  }

  absl::Status VisitResolvedTVFScan(const ResolvedTVFScan* node) override {
    if (node->signature()->SupportsAnonymization()) {
      return MakeSqlErrorAtNode(*node)
             << "Reading the TVF " << node->tvf()->FullName()
             << " containing user data in expression subqueries is not allowed";
    }
    return CopyVisitResolvedTVFScan(node);
  }

  absl::Status VisitResolvedAnonymizedAggregateScan(
      const ResolvedAnonymizedAggregateScan* node) override {
    return MakeSqlErrorAtNode(*node)
           << "Nested anonymization query is not implemented yet";
  }

  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override {
    // Necessary to correctly attach parse location to errors generated above.
    return MaybeAttachParseLocation(CopyVisitResolvedProjectScan(node), *node);
  }
};

// Rewrites the aggregate and group by list for the inner per-user aggregate
// scan. Replaces all function calls with their non-ANON_* versions, and sets
// the output column for each ComputedColumn to the corresponding intermediate
// column in the <injected_col_map>.
class InnerAggregateListRewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  InnerAggregateListRewriterVisitor(
      std::map<ResolvedColumn, ResolvedColumn>* injected_col_map,
      ColumnFactory* allocator, Resolver* resolver,
      absl::string_view select_with_identifier)
      : injected_col_map_(injected_col_map),
        allocator_(allocator),
        resolver_(resolver),
        select_with_identifier_(select_with_identifier) {}

  const ResolvedColumn& order_by_column() { return order_by_column_; }

  // Rewrite the aggregates in `node` to change ANON_* functions to their
  // per-user aggregate alternatives (e.g. ANON_SUM->SUM).
  //
  // This also changes the output column of each function to the appropriate
  // intermediate column, as dictated by the injected_col_map.
  absl::StatusOr<std::vector<std::unique_ptr<ResolvedComputedColumnBase>>>
  RewriteAggregateColumns(const ResolvedAggregateScanBase* node) {
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
        inner_aggregate_list;
    for (const auto& col : node->aggregate_list()) {
      GOOGLESQL_RETURN_IF_ERROR(col->Accept(this));
      GOOGLESQL_ASSIGN_OR_RETURN(auto unique_ptr_node,
                       this->ConsumeRootNode<ResolvedComputedColumnBase>());
      inner_aggregate_list.emplace_back(std::move(unique_ptr_node));
    }
    return inner_aggregate_list;
  }

  // Rewrite the GROUP BY list of `node` to change each output column to the
  // appropriate intermediate column, as dictated by the injected_col_map.
  //
  // Any complex GROUP BY transforms/computed columns will be included here
  // (e.g. GROUP BY col1 + col2).
  absl::StatusOr<std::vector<std::unique_ptr<ResolvedComputedColumn>>>
  RewriteGroupByColumns(const ResolvedAggregateScanBase* node) {
    std::vector<std::unique_ptr<ResolvedComputedColumn>> inner_group_by_list;
    for (const auto& col : node->group_by_list()) {
      GOOGLESQL_RETURN_IF_ERROR(col->Accept(this));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedComputedColumn> unique_ptr_node,
                       this->ConsumeRootNode<ResolvedComputedColumn>());
      inner_group_by_list.emplace_back(std::move(unique_ptr_node));
    }
    return inner_group_by_list;
  }

 private:
  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<ResolvedExpr>> argument_list,
        ResolvedASTDeepCopyVisitor::CopyNodeList(node->argument_list()));

    // Trim the arg list and resolve the per-user aggregate function.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedExpr> result,
        ResolveInnerAggregateFunctionCallForAnonFunction(
            node,
            // This is expecting unique_ptr to be const.
            // std::vector<std::unique_ptr<__const__ ResolvedExpr>>
            {std::make_move_iterator(argument_list.begin()),
             std::make_move_iterator(argument_list.end())},
            resolver_, &order_by_column_, allocator_, select_with_identifier_));
    GOOGLESQL_RET_CHECK_EQ(result->node_kind(), RESOLVED_AGGREGATE_FUNCTION_CALL)
        << result->DebugString();
    PushNodeToStack(std::move(result));
    return absl::OkStatus();
  }

  ResolvedColumn ReplaceColumn(const ResolvedColumn& old_column,
                               absl::string_view new_name,
                               const Type* new_type) {
    const ResolvedColumn injected_column =
        allocator_->MakeCol(old_column.table_name(), new_name, new_type);
    injected_col_map_->emplace(old_column, injected_column);
    return injected_column;
  }

  absl::Status ProcessResolvedComputedColumnImpl(
      const ResolvedComputedColumnImpl* node) {
    auto* col = GetUnownedTopOfStack<ResolvedComputedColumnImpl>();
    // Create a column to splice together the per-user and cross-user
    // aggregate/groupby lists, then update the copied computed column and place
    // our new column in the replacement map.
    if (node->Is<ResolvedComputedColumn>()) {
      const ResolvedColumn& old_column = node->column();
      static_cast<ResolvedComputedColumn*>(col)->set_column(ReplaceColumn(
          old_column, old_column.name() + "_partial", col->expr()->type()));
    } else {
      GOOGLESQL_RET_CHECK(node->Is<ResolvedDeferredComputedColumn>());
      const ResolvedColumn& old_column = node->column();
      static_cast<ResolvedDeferredComputedColumn*>(col)->set_column(ReplaceColumn(
          old_column, old_column.name() + "_partial", col->expr()->type()));

      const ResolvedColumn& old_side_effect_column =
          node->GetAs<ResolvedDeferredComputedColumn>()->side_effect_column();
      static_cast<ResolvedDeferredComputedColumn*>(col)->set_side_effect_column(
          ReplaceColumn(old_side_effect_column,
                        old_side_effect_column.name() + "_partial",
                        old_side_effect_column.type()));
    }

    return absl::OkStatus();
  }

  absl::Status VisitResolvedDeferredComputedColumn(
      const ResolvedDeferredComputedColumn* node) override {
    // Rewrite the output column to point to the mapped column.
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedDeferredComputedColumn(node));
    return ProcessResolvedComputedColumnImpl(node);
  }

  absl::Status VisitResolvedComputedColumn(
      const ResolvedComputedColumn* node) override {
    // Rewrite the output column to point to the mapped column.
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedComputedColumn(node));
    return ProcessResolvedComputedColumnImpl(node);
  }

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    // Expression subqueries may contain nested scans which might have their own
    // group by, aggregate, or computed columns. We don't want to rewrite those
    // columns, only the top level column names. Therefore take a simple copy.
    ExpressionSubqueryRewriterVisitor copy_visitor;
    GOOGLESQL_RETURN_IF_ERROR(node->Accept(&copy_visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(auto copy,
                     copy_visitor.ConsumeRootNode<ResolvedSubqueryExpr>());
    PushNodeToStack(std::move(copy));
    return absl::OkStatus();
  }

  std::map<ResolvedColumn, ResolvedColumn>* injected_col_map_;
  ColumnFactory* allocator_;
  Resolver* resolver_;
  ResolvedColumn order_by_column_;
  absl::string_view select_with_identifier_;
};

// Returns a vector with a reference to all named arguments in the node's
// signature.
std::vector<NamedArgumentInfo> FindAllNamedArguments(
    const ResolvedAggregateFunctionCall* node, Resolver* resolver) {
  auto id_string_pool = resolver->analyzer_options().id_string_pool();

  std::vector<NamedArgumentInfo> named_arguments;
  for (int i = 0; i < node->signature().arguments().size(); ++i) {
    const auto& arg = node->signature().argument(i);
    if (arg.options().named_argument_kind() == FunctionEnums::NAMED_ONLY) {
      named_arguments.emplace_back(id_string_pool->Make(arg.argument_name()), i,
                                   node);
    }
  }
  return named_arguments;
}

// The rewriter replaces some aggregations, e.g. rewriting ANON_COUNT_STAR to
// ANON_SUM. This struct stores information about the aggregate function call
// that should be used as a replacement.
struct ReplacementFunctionCall {
  // The name of the new function call, e.g. "$anon_sum_with_report_json".
  std::string name;

  // An expression that should be aggregated instead of the original input
  // expression. If null, it indicates that the original input expression
  // should be used.
  //
  // For example, when bounding groups contributed, this field should point to
  // the intermediate partially-aggregated column produced while rewriting the
  // per-user scan; or when bounding rows contributed and rewriting an
  // aggregation like COUNT(*), it could contain a literal value 1.
  std::unique_ptr<ResolvedExpr> input_expr;

  // The named arguments for the rewritten function call.
  //
  // Since the arguments for the new functional call are not necessarily the
  // same as the old, we also might need to rewrite the function arguments. An
  // empty optional indicates to copy all of the named arguments from the
  // original argument list. An empty vector indicates to remove all named
  // arguments.
  std::optional<std::vector<NamedArgumentInfo>> named_arguments;
};

absl::StatusOr<std::unique_ptr<ResolvedExpr>>
ResolveOuterAggregateFunctionCallForAnonFunction(
    const ResolvedAggregateFunctionCall* node,
    std::vector<std::unique_ptr<ResolvedExpr>> arguments,
    ReplacementFunctionCall replacement_function_call, Resolver* resolver) {
  if (node->function()->GetGroup() != Function::kGoogleSQLFunctionGroupName) {
    replacement_function_call.named_arguments =
        std::make_optional<std::vector<NamedArgumentInfo>>({});
  }

  if (node->function()->GetGroup() == Function::kGoogleSQLFunctionGroupName &&
      IsCountStarFunction(node->signature().context_id())) {
    // We rewrite COUNT(*) functions to an aggregation that needs a input
    // expression, so here we insert a dummy argument that will get replaced by
    // the input expression below.
    arguments.insert(arguments.begin(), nullptr);
  }
  // Replace the aggregated expression with the new input expression.
  if (replacement_function_call.input_expr != nullptr) {
    arguments[0] = std::move(replacement_function_call.input_expr);
  }

  std::vector<NamedArgumentInfo> named_arguments =
      replacement_function_call.named_arguments.has_value()
          ? replacement_function_call.named_arguments.value()
          : FindAllNamedArguments(node, resolver);
  return ResolveFunctionCall(
      replacement_function_call.name,
      // ResolvedFunctionCall expects a std::vector<std::unique_ptr<const
      // ResolvedExpr>>, but our ResolvedExpr are non-const, so we can't just
      // move arguments.
      {std::make_move_iterator(arguments.begin()),
       std::make_move_iterator(arguments.end())},
      named_arguments, resolver);
}

bool IsInteger(const Type& type) {
  return type.IsInteger64() || type.IsNumericType();
}

// Converts value from int64_t to Value object based on provided integer type -
// UNIT64, NUMERIC, INT64.
Value ToIntegerValue(const Type& type, int64_t value) {
  switch (type.kind()) {
    case TYPE_UINT64:
      return Value::Uint64(value);
    case TYPE_NUMERIC:
      return values::Numeric(value);
    default:
      return Value::Int64(value);
  }
}

// Returns true if a given expr is an integer literal and its value equals to
// expected_value.
bool IsLiteralWithValueEqualTo(const ResolvedExpr& expr,
                               int64_t expected_value) {
  if (expr.node_kind() != RESOLVED_LITERAL || !IsInteger(*expr.type())) {
    return false;
  }

  const Value expected = ToIntegerValue(*expr.type(), expected_value);
  const Value& literal = expr.GetAs<ResolvedLiteral>()->value();
  return !literal.is_null() && expected.Equals(literal);
}

// Returns true if a given expr is an integer literal and its value >=
// lower_bound.
bool IsLiteralWithValueGreaterThanOrEqualTo(const ResolvedExpr& expr,
                                            int64_t lower_bound) {
  if (expr.node_kind() != RESOLVED_LITERAL || !IsInteger(*expr.type())) {
    return false;
  }

  const Value lower = ToIntegerValue(*expr.type(), lower_bound);
  const Value& literal = expr.GetAs<ResolvedLiteral>()->value();
  return !literal.is_null() &&
         (lower.LessThan(literal) || lower.Equals(literal));
}

// Returns true if a given expr is a literal and its value is not NULL.
bool IsNonNullLiteral(const ResolvedExpr& expr) {
  if (expr.node_kind() != RESOLVED_LITERAL) {
    return false;
  }
  return !expr.GetAs<ResolvedLiteral>()->value().is_null();
}

std::optional<ResolvedColumn> GetResolvedColumn(const ResolvedExpr* exp) {
  if (exp != nullptr && exp->node_kind() == RESOLVED_COLUMN_REF) {
    return exp->GetAs<ResolvedColumnRef>()->column();
  }
  return std::nullopt;
}

// Returns true if a given expr is uid column.
bool IsUidColumn(const ResolvedExpr& expr, int64_t uid_column_id) {
  ResolvedColumn column = GetResolvedColumn(&expr).value_or(ResolvedColumn());
  return column.IsInitialized() && column.column_id() == uid_column_id;
}

// Returns true if the column (corresponding to a given column_id)
// is a function counting unique users:
// 1. ANON_COUNT(* CLAMPED BETWEEN 0 AND 1)
// 2. ANON_COUNT($X CLAMPED BETWEEN 0 AND 1) where X is non-null literal
// 3. ANON_COUNT(uid CLAMPED BETWEEN 0 AND 1)
// 4. ANON_SUM($X CLAMPED BETWEEN 0 AND 1) where X is non-null literal and X
//    >= 1
// 5. $differential_privacy_count(*, contribution_bounds_per_group => (0,1))
// 6. $differential_privacy_count($X, contribution_bounds_per_group => (0,1))
//    where X is non-null literal.
// 7. $differential_privacy_count(uid, contribution_bounds_per_group => (0,1))
// 8. $differential_privacy_count($X, contribution_bounds_per_group => (0,1))
//    where X is non-null literal and X >= 1.
bool IsCountUniqueUsers(const ResolvedAggregateFunctionCall* function_call,
                        int64_t uid_column_id) {
  const std::vector<std::unique_ptr<const ResolvedExpr>>& arguments =
      function_call->argument_list();
  auto check_dp_contribution_bounds = [](const ResolvedExpr& expr) {
    if (expr.node_kind() != RESOLVED_LITERAL) {
      return false;
    }
    if (!expr.type()->IsStruct() ||
        expr.type()->AsStruct()->num_fields() != 2) {
      return false;
    }

    if (!IsInteger(*expr.type()->AsStruct()->field(0).type) ||
        !IsInteger(*expr.type()->AsStruct()->field(1).type)) {
      return false;
    }

    const Value expected_lower_bound =
        ToIntegerValue(*expr.type()->AsStruct()->field(0).type, 0);
    const Value expected_upper_bound =
        ToIntegerValue(*expr.type()->AsStruct()->field(1).type, 1);

    const Value& literal = expr.GetAs<ResolvedLiteral>()->value();
    return !literal.is_null() && literal.num_fields() == 2 &&
           expected_lower_bound.Equals(literal.field(0)) &&
           expected_upper_bound.Equals(literal.field(1));
  };

  switch (function_call->signature().context_id()) {
    // ANON_COUNT(* CLAMPED BETWEEN 0 AND 1)
    case FunctionSignatureId::FN_ANON_COUNT_STAR:
    case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_PROTO:
    case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_JSON:
      return arguments.size() == 2 &&
             // CLAMPED BETWEEN 0 AND 1
             IsLiteralWithValueEqualTo(*arguments[0], /* expected_value=*/0) &&
             IsLiteralWithValueEqualTo(*arguments[1], /* expected_value=*/1);
    // ANON_COUNT($X CLAMPED BETWEEN 0 AND 1), X - non-null literal
    // ANON_COUNT(uid CLAMPED BETWEEN 0 AND 1)
    case FunctionSignatureId::FN_ANON_COUNT:
    case FunctionSignatureId::FN_ANON_COUNT_WITH_REPORT_PROTO:
    case FunctionSignatureId::FN_ANON_COUNT_WITH_REPORT_JSON:
      return arguments.size() == 3 &&
             (IsNonNullLiteral(*arguments[0]) ||
              IsUidColumn(*arguments[0], uid_column_id)) &&
             // CLAMPED BETWEEN 0 AND 1
             IsLiteralWithValueEqualTo(*arguments[1], /* expected_value=*/0) &&
             IsLiteralWithValueEqualTo(*arguments[2], /* expected_value=*/1);
    // ANON_SUM($X CLAMPED BETWEEN 0 AND 1), X  >= 1
    case FunctionSignatureId::FN_ANON_SUM_INT64:
    case FunctionSignatureId::FN_ANON_SUM_WITH_REPORT_PROTO_INT64:
    case FunctionSignatureId::FN_ANON_SUM_WITH_REPORT_JSON_INT64:
    case FunctionSignatureId::FN_ANON_SUM_UINT64:
    case FunctionSignatureId::FN_ANON_SUM_WITH_REPORT_PROTO_UINT64:
    case FunctionSignatureId::FN_ANON_SUM_WITH_REPORT_JSON_UINT64:
    case FunctionSignatureId::FN_ANON_SUM_NUMERIC:
      return arguments.size() == 3 &&
             IsLiteralWithValueGreaterThanOrEqualTo(*arguments[0],
                                                    /* lower_bound=*/1) &&
             // CLAMPED BETWEEN 0 AND 1
             IsLiteralWithValueEqualTo(*arguments[1], /* expected_value=*/0) &&
             IsLiteralWithValueEqualTo(*arguments[2], /* expected_value=*/1);
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT:
      return arguments.size() >= 2 &&
             (IsNonNullLiteral(*arguments[0]) ||
              IsUidColumn(*arguments[0], uid_column_id)) &&
             check_dp_contribution_bounds(*arguments[1]);
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_JSON:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_PROTO:
      return arguments.size() >= 3 &&
             (IsNonNullLiteral(*arguments[0]) ||
              IsUidColumn(*arguments[0], uid_column_id)) &&
             check_dp_contribution_bounds(*arguments[2]);
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR:
      return !arguments.empty() && check_dp_contribution_bounds(*arguments[0]);
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_JSON:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_PROTO:
      return arguments.size() >= 2 &&
             check_dp_contribution_bounds(*arguments[1]);
    // For new dp syntax we expect group threshold expression to be INT64.
    // Therefore other SUM types like UINT64, NUMERIC) aren't supported for
    // replacement.
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_SUM_INT64:
      return arguments.size() >= 2 &&
             IsLiteralWithValueGreaterThanOrEqualTo(*arguments[0],
                                                    /* lower_bound=*/1) &&
             check_dp_contribution_bounds(*arguments[1]);
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_JSON_INT64:
    case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_PROTO_INT64:
      return arguments.size() >= 2 &&
             IsLiteralWithValueGreaterThanOrEqualTo(*arguments[0],
                                                    /* lower_bound=*/1) &&
             check_dp_contribution_bounds(*arguments[2]);
    default:
      return false;
  }

  return false;
}

// Searches for a user-provided column that counts distinct privacy units.
//
// Usage:
//  - First call CheckAndSetCountDistinctPrivacyIdsColumn for each
//    ResolvedAggregateFunctionCall that might be counting distinct privacy
//    units.
//  - Once all such aggregates have been processed, call
//    GetCountDistinctPrivacyIdsColumn to retrieve the first column that counts
//    distinct privacy units, if any such column was found.
class DistinctPrivacyIdsCountColumnFinder {
 public:
  explicit DistinctPrivacyIdsCountColumnFinder(
      const ResolvedColumn& inner_uid_column, Resolver* resolver)
      : inner_uid_column_(inner_uid_column), resolver_(resolver) {}

  // Checks whether function_call_node is an aggregation that counts distinct
  // users.
  //
  // If so, and it is the first such aggregation found, then this class stores a
  // reference to the column that contained the function call, for later
  // retrieval using GetCountDistinctPrivacyIdsColumn().
  //
  // The parameter `containing_column` must be a reference to the column that
  // contains the aggregate function `function_call_node`.
  void CheckAndSetCountDistinctPrivacyIdsColumn(
      const ResolvedAggregateFunctionCall* function_call_node,
      const ResolvedColumn& containing_column) {
    const auto* function_call =
        function_call_node
            ->GetAs<const googlesql::ResolvedAggregateFunctionCall>();

    if (resolver_->language().LanguageFeatureEnabled(
            FEATURE_ANONYMIZATION_THRESHOLDING) ||
        resolver_->language().LanguageFeatureEnabled(
            FEATURE_DIFFERENTIAL_PRIVACY_THRESHOLDING)) {
      // Save the first found column which matches unique user count function.
      // We choose to select first to make the unit tests deterministic.
      // In general, we can safely select any matching function.
      // Since, ignoring the intrinsic randomness in these functions, we'll get
      // indistinguishable query results regardless of which function we
      // replace.
      if (!unique_users_count_column_.IsInitialized() &&
          IsCountUniqueUsers(function_call, inner_uid_column_.column_id())) {
        unique_users_count_column_ = containing_column;
      }
    }
  }

  // Returns the first column counting unique users that was found in
  // CheckAndSetCountDistinctPrivacyIdsColumn.
  //
  // If no such column was found, returns an uninitialized ResolvedColumn
  // instance.
  ResolvedColumn GetCountDistinctPrivacyIdsColumn() const {
    return unique_users_count_column_;
  }

 private:
  // The first found user aggregation function which counts the unique users, if
  // any such column has been found yet.
  ResolvedColumn unique_users_count_column_;

  // The UID column in the original, unrewritten user query.
  const ResolvedColumn inner_uid_column_;

  Resolver* resolver_;
};

// Interface for rewriting aggregate function calls in user queries.
//
// The rewriter rewrites certain aggregates in terms of other DP primitives,
// like replacing ANON_COUNT_STAR with ANON_SUM. The exact replacements made
// will depend on the contribution bounding strategy.
class AggregateFunctionCallResolver {
 public:
  virtual ~AggregateFunctionCallResolver() = default;

  virtual absl::StatusOr<std::unique_ptr<ResolvedExpr>> Resolve(
      const ResolvedAggregateFunctionCall* function_call_node,
      const ResolvedColumn& containing_column,
      const std::optional<const ResolvedColumn>& side_effect_column,
      std::vector<std::unique_ptr<ResolvedExpr>> argument_list) const = 0;
};

// Rewrites the aggregate list for the outer cross-user aggregate scan.
//
// Replaces some DP aggregations with calls to other DP primitives and allows
// for rewriting the target column that is being aggregated over (e.g. replacing
// with the intermediate column produced by the per-user aggregate scan).
class OuterAggregateListRewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  OuterAggregateListRewriterVisitor(
      Resolver* resolver, ResolvedColumn inner_uid_column,
      std::unique_ptr<AggregateFunctionCallResolver>
          aggregate_function_call_resolver,
      bool filter_values_with_null_uid)
      : resolver_(resolver),
        distinct_user_count_column_finder_(inner_uid_column, resolver),
        aggregate_function_call_resolver_(
            std::move(aggregate_function_call_resolver)) {}

  ResolvedColumn GetCountDistinctPrivacyIdsColumn() const {
    return distinct_user_count_column_finder_
        .GetCountDistinctPrivacyIdsColumn();
  }

  // Rewrite the outer aggregate list, changing each ANON_* function to refer to
  // the intermediate column with pre-aggregated values that was produced by the
  // per-user aggregate scan.
  absl::StatusOr<std::vector<std::unique_ptr<ResolvedComputedColumnBase>>>
  RewriteAggregateColumns(const ResolvedAggregateScanBase* node) {
    return ProcessNodeList(node->aggregate_list());
  }

 private:
  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ResolvedExpr>> argument_list,
                     ProcessNodeList(node->argument_list()));

    // Resolve the new cross-user ANON_* function call.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> result,
                     aggregate_function_call_resolver_->Resolve(
                         node, current_column_, current_side_effect_column_,
                         std::move(argument_list)));
    GOOGLESQL_RET_CHECK_EQ(result->node_kind(), RESOLVED_AGGREGATE_FUNCTION_CALL)
        << result->DebugString();

    distinct_user_count_column_finder_.CheckAndSetCountDistinctPrivacyIdsColumn(
        node, current_column_);

    PushNodeToStack(std::move(result));
    return absl::OkStatus();
  }

  // This function is in practice the class entry point. We need to record
  // what the current output column is so that we can look the appropriate
  // intermediate column up in the map.
  absl::Status VisitResolvedComputedColumn(
      const ResolvedComputedColumn* node) override {
    current_column_ = node->column();
    current_side_effect_column_ = std::nullopt;
    return CopyVisitResolvedComputedColumn(node);
  }

  // This function is in practice the class entry point. We need to record
  // what the current output column is so that we can look the appropriate
  // intermediate column up in the map.
  absl::Status VisitResolvedDeferredComputedColumn(
      const ResolvedDeferredComputedColumn* node) override {
    current_column_ = node->column();
    current_side_effect_column_ = node->side_effect_column();
    return CopyVisitResolvedDeferredComputedColumn(node);
  }

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    // Expression subqueries aren't allowed to read from tables or TVFs that
    // have $uid columns. See (broken link)
    ExpressionSubqueryRewriterVisitor subquery_visitor;
    GOOGLESQL_RETURN_IF_ERROR(node->Accept(&subquery_visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(auto copy,
                     subquery_visitor.ConsumeRootNode<ResolvedSubqueryExpr>());
    PushNodeToStack(std::move(copy));
    return absl::OkStatus();
  }

  // The most recent column visited.
  //
  // When visiting an aggregate function call, current_column_ will point to the
  // column that contains that function call.
  ResolvedColumn current_column_;
  // If the aggregate function in question defers side effects, the companion
  // `current_side_effect_column_` points to the companion column, std::nullopt
  // otherwise.
  std::optional<ResolvedColumn> current_side_effect_column_;

  Resolver* resolver_;
  DistinctPrivacyIdsCountColumnFinder distinct_user_count_column_finder_;
  std::unique_ptr<AggregateFunctionCallResolver>
      aggregate_function_call_resolver_;
};

// This class is used by VisitResolvedTVFScan to validate that none of the TVF
// argument trees contain nodes that are not supported yet as TVF arguments.
//
// The current implementation does not support subqueries that have
// anonymization, where we will need to recursively call the rewriter on
// these (sub)queries.
class TVFArgumentValidatorVisitor : public ResolvedASTVisitor {
 public:
  explicit TVFArgumentValidatorVisitor(absl::string_view tvf_name)
      : tvf_name_(tvf_name) {}

  absl::Status VisitResolvedAnonymizedAggregateScan(
      const ResolvedAnonymizedAggregateScan* node) override {
    return MakeSqlErrorAtNode(*node)
           << "TVF arguments do not support SELECT WITH ANONYMIZATION queries";
  }

  absl::Status VisitResolvedDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* node) override {
    return MakeSqlErrorAtNode(*node) << "TVF arguments do not support SELECT "
                                        "WITH DIFFERENTIAL_PRIVACY queries";
  }

  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override {
    return MaybeAttachParseLocation(
        ResolvedASTVisitor::VisitResolvedProjectScan(node), *node);
  }

 private:
  const std::string tvf_name_;
};

std::string FieldPathExpressionToString(const ResolvedExpr* expr) {
  std::vector<std::string> field_path;
  while (expr != nullptr) {
    switch (expr->node_kind()) {
      case RESOLVED_GET_PROTO_FIELD: {
        auto* node = expr->GetAs<ResolvedGetProtoField>();
        field_path.emplace_back(node->field_descriptor()->name());
        expr = node->expr();
        break;
      }
      case RESOLVED_GET_STRUCT_FIELD: {
        auto* node = expr->GetAs<ResolvedGetStructField>();
        field_path.emplace_back(
            node->expr()->type()->AsStruct()->field(node->field_idx()).name);
        expr = node->expr();
        break;
      }
      case RESOLVED_COLUMN_REF: {
        std::string name = expr->GetAs<ResolvedColumnRef>()->column().name();
        if (!IsInternalAlias(name)) {
          field_path.emplace_back(std::move(name));
        }
        expr = nullptr;
        break;
      }
      default:
        // Node types other than RESOLVED_GET_PROTO_FIELD /
        // RESOLVED_GET_STRUCT_FIELD / RESOLVED_COLUMN_REF should never show up
        // in a $uid column path expression.
        return "<INVALID>";
    }
  }
  return absl::StrJoin(field_path.rbegin(), field_path.rend(), ".");
}

// Wraps the ResolvedColumn for a given $uid column during AST rewrite. Also
// tracks an optional alias for the column, this improves error messages with
// aliased tables.
struct UidColumnState {
  void InitFromValueTable(const ResolvedComputedColumn* projected_userid_column,
                          std::string value_table_alias) {
    column = projected_userid_column->column();
    alias = std::move(value_table_alias);
    value_table_uid = projected_userid_column->expr();
  }

  void Clear() {
    column.Clear();
    alias.clear();
    value_table_uid = nullptr;
  }

  bool SetColumn(const googlesql::ResolvedColumn& col) {
    column = col;
    return true;
  }

  bool SetColumn(const googlesql::ResolvedColumn& col,
                 absl::string_view new_alias) {
    SetColumn(col);
    alias = new_alias;
    return true;
  }

  // Returns an alias qualified (if specified) user visible name for the $uid
  // column to be returned in validation error messages.
  std::string ToString() const {
    const std::string alias_prefix =
        absl::StrCat(alias.empty() ? "" : absl::StrCat(alias, "."));
    if (!IsInternalAlias(column.name())) {
      return absl::StrCat(alias_prefix, column.name());
    } else if (value_table_uid != nullptr) {
      return absl::StrCat(alias_prefix,
                          FieldPathExpressionToString(value_table_uid));
    } else {
      return "";
    }
  }

  // If the uid column is derived from a value table we insert a
  // ResolvedProjectScan that extracts the uid column from the table row object.
  // But existing references to the uid column in the query (like in a group by
  // list) will reference a semantically equivalent but distinct column. This
  // function replaces these semantically equivalent computed columns with
  // column references to the 'canonical' uid column.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
  SubstituteUidComputedColumn(
      std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list) {
    if (value_table_uid == nullptr) return expr_list;
    for (auto& col : expr_list) {
      if (MatchesPathExpression(*col->expr())) {
        col = MakeResolvedComputedColumn(col->column(),
                                         BuildResolvedColumnRef(column));
        column = col->column();
        value_table_uid = nullptr;
      }
    }

    return expr_list;
  }

  // Add the $uid column to the argument scan node column list if it isn't
  // already included.
  void ProjectIfMissing(ResolvedScan& node) {
    for (const ResolvedColumn& col : node.column_list()) {
      if (col == column) {
        return;
      }
    }
    node.add_column_list(column);
  }

  // Returns true IFF the argument expression points to the same (optionally
  // nested) value as this.
  bool MatchesPathExpression(const ResolvedExpr& other) const {
    if (value_table_uid == nullptr) {
      if (other.node_kind() == RESOLVED_COLUMN_REF) {
        return other.GetAs<ResolvedColumnRef>()->column() == column;
      }
      return false;
    }
    return IsSameFieldPath(&other, value_table_uid,
                           FieldPathMatchingOption::kExpression);
  }

  // A column declared as the $uid column in a table or TVF schema definition.
  // This gets passed up the AST during the rewriting process to validate the
  // query, and gets replaced with computed columns as needed for joins and
  // nested aggregations.
  ResolvedColumn column;

  // <alias> is only used for clarifying error messages, it's only set to a non
  // empty string for table scan clauses like '... FROM Table as t' so that we
  // can display error messages related to the $uid column as 't.userid' rather
  // than 'userid' or 'Table.userid'. It has no impact on the actual rewriting
  // logic.
  std::string alias;

 private:
  const ResolvedExpr* value_table_uid = nullptr;
};

// Tracks the lazily-rewritten state of a ResolvedWithEntry. The original AST
// must outlive instances of this struct.
struct WithEntryRewriteState {
  // References the WITH entry in the original AST, always set.
  const ResolvedWithEntry& original_entry;

  // Contains the rewritten AST for this WITH entry, but only if it's been
  // rewritten.
  const ResolvedWithEntry* rewritten_entry;
  std::unique_ptr<const ResolvedWithEntry> rewritten_entry_owned;

  // Contains the $uid column state for this WITH entry IFF it's been rewritten
  // AND it reads from a table, TVF, or another WITH entry that reads user data.
  std::optional<UidColumnState> rewritten_uid;
};

// A helper for JoinExprIncludesUid, returns true if at least one argument of
// the function call is a column ref referring to left_uid, and the same for
// right_uid.
bool FunctionReferencesUid(const ResolvedFunctionCall* call,
                           const UidColumnState& left_uid,
                           const UidColumnState& right_uid) {
  bool left_referenced = false;
  bool right_referenced = false;
  for (const std::unique_ptr<const ResolvedExpr>& argument :
       call->argument_list()) {
    left_referenced |= left_uid.MatchesPathExpression(*argument);
    right_referenced |= right_uid.MatchesPathExpression(*argument);
  }
  return left_referenced && right_referenced;
}

// A helper function for checking if a join expression between two tables
// containing user data meets our requirements for joining on the $uid column in
// each table.
//
// Returns true IFF join_expr contains a top level AND function, or an AND
// function nested inside another AND function (arbitrarily deep), that contains
// an EQUAL function that satisfies FunctionReferencesUid.
//
// This excludes a number of logically equivalent join expressions
// (e.g. !(left != right)), but that's fine, we want queries to be intentional.
bool JoinExprIncludesUid(const ResolvedExpr* join_expr,
                         const UidColumnState& left_uid,
                         const UidColumnState& right_uid) {
  if (join_expr->node_kind() != RESOLVED_FUNCTION_CALL) {
    return false;
  }
  const ResolvedFunctionCall* call = join_expr->GetAs<ResolvedFunctionCall>();
  const Function* function = call->function();
  if (!function->IsScalar() || !function->IsGoogleSQLBuiltin()) {
    return false;
  }
  switch (call->signature().context_id()) {
    case FN_AND:
      for (const std::unique_ptr<const ResolvedExpr>& argument :
           call->argument_list()) {
        if (JoinExprIncludesUid(argument.get(), left_uid, right_uid)) {
          return true;
        }
      }
      break;
    case FN_EQUAL:
      if (FunctionReferencesUid(call, left_uid, right_uid)) {
        return true;
      }
      break;
  }
  return false;
}

constexpr absl::string_view SetOperationTypeToString(
    const ResolvedSetOperationScanEnums::SetOperationType type) {
  switch (type) {
    case ResolvedSetOperationScanEnums::UNION_ALL:
      return "UNION ALL";
    case ResolvedSetOperationScanEnums::UNION_DISTINCT:
      return "UNION DISTINCT";
    case ResolvedSetOperationScanEnums::INTERSECT_ALL:
      return "INTERSECT ALL";
    case ResolvedSetOperationScanEnums::INTERSECT_DISTINCT:
      return "INTERSECT DISTINCT";
    case ResolvedSetOperationScanEnums::EXCEPT_ALL:
      return "EXCEPT ALL";
    case ResolvedSetOperationScanEnums::EXCEPT_DISTINCT:
      return "EXCEPT DISTINCT";
  }
}

absl::Status ValidateUidColumnTypeSupportsGrouping(
    const Type* uid_column_type, const ResolvedNode& node_with_parse_location,
    const LanguageOptions& language_options) {
  if (!uid_column_type->SupportsGrouping(language_options)) {
    return MakeSqlErrorAtNode(node_with_parse_location)
           << "User id columns must support grouping, instead got type "
           << Type::TypeKindToString(uid_column_type->kind(),
                                     language_options.product_mode());
  }
  return absl::OkStatus();
}

// Rewrites the rest of the per-user scan, propagating the AnonymizationInfo()
// userid (aka $uid column) from the base private table scan to the top node
// returned.
//
// This visitor may only be invoked on a scan that is a transitive child of a
// ResolvedAnonymizedAggregateScan. uid_column() will return an error if the
// subtree represented by that scan does not contain a table or TVF that
// contains user data (AnonymizationInfo).
class PerUserRewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  PerUserRewriterVisitor(
      ColumnFactory* allocator, TypeFactory* type_factory, Resolver* resolver,
      std::vector<std::unique_ptr<WithEntryRewriteState>>& with_entries,
      WithModifierModeName with_modifier_mode_name,
      PublicGroupsState* public_groups_state, RewriterVisitor* parent_visitor)
      : allocator_(allocator),
        type_factory_(type_factory),
        resolver_(resolver),
        with_entries_(with_entries),
        with_modifier_mode_name_(with_modifier_mode_name),
        public_groups_state_(public_groups_state),
        parent_visitor_(parent_visitor) {}

  std::optional<ResolvedColumn> uid_column() const {
    if (current_uid_.column.IsInitialized()) {
      return current_uid_.column;
    } else {
      return std::nullopt;
    }
  }

  const UidColumnState& uid_column_state() const { return current_uid_; }

  // In case a public groups join is found, the operations that are allowed in
  // the AST is very limited as they could suppress public groups that are not
  // in the user data. More details in (broken link).
  bool GetFoundPublicGroupsJoin() const { return found_public_groups_join_; }

 private:
  absl::Status DefaultVisit(const ResolvedNode* node) override {
    // There are only a limited number of node types allowed after the first
    // public groups join and the anon or dp aggregate scan. The default
    // behavior here is to throw an error, which also prevents against privacy
    // issues in case a new node type is introduced in the future. To explicitly
    // allow a node type, the VisitXXX() method must be overwritten.
    if (GetFoundPublicGroupsJoin()) {
      return MakeSqlErrorAtNode(*node)
             << CreateOptionNotAllowedWithPublicGroupsError();
    }
    return ResolvedASTDeepCopyVisitor::DefaultVisit(node);
  }

  absl::Status ProjectValueTableScanRowValueIfNeeded(
      ResolvedTableScan* copy, const Column* value_table_value_column,
      ResolvedColumn* value_table_value_resolved_column) {
    for (int i = 0; i < copy->column_list_size(); ++i) {
      int j = copy->column_index_list(i);
      if (value_table_value_column == copy->table()->GetColumn(j)) {
        // The current scan already produces the value table value column
        // that we want to extract from, so we can leave the scan node
        // as is.
        *value_table_value_resolved_column = copy->column_list(i);
        return absl::OkStatus();
      }
    }

    // Make a new ResolvedColumn for the value table value column and
    // add it to the table scan's column list.
    *value_table_value_resolved_column = allocator_->MakeCol(
        "$table_scan", "$value", value_table_value_column->GetType());
    copy->add_column_list(*value_table_value_resolved_column);
    int table_col_idx = -1;
    for (int idx = 0; idx < copy->table()->NumColumns(); ++idx) {
      if (value_table_value_column == copy->table()->GetColumn(idx)) {
        table_col_idx = idx;
        break;
      }
    }
    GOOGLESQL_RET_CHECK_GE(table_col_idx, 0);
    GOOGLESQL_RET_CHECK_LT(table_col_idx, copy->table()->NumColumns());
    copy->add_column_index_list(table_col_idx);

    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<ResolvedComputedColumn>>
  MakeGetFieldComputedColumn(
      const ResolvedScan* node,
      absl::Span<const std::string> userid_column_name_path,
      const ResolvedColumn& value_table_value_resolved_column) {
    const std::string& userid_column_name =
        IdentifierPathToString(userid_column_name_path);
    ResolvedColumn userid_column = value_table_value_resolved_column;
    std::unique_ptr<const ResolvedExpr> resolved_expr_to_ref =
        BuildResolvedColumnRef(value_table_value_resolved_column);

    if (value_table_value_resolved_column.type()->IsStruct()) {
      const StructType* struct_type =
          value_table_value_resolved_column.type()->AsStruct();

      for (const std::string& userid_column_field : userid_column_name_path) {
        GOOGLESQL_RET_CHECK_NE(struct_type, nullptr) << userid_column_name;
        int found_idx = -1;
        bool is_ambiguous = false;
        const StructField* struct_field = struct_type->FindField(
            userid_column_field, &is_ambiguous, &found_idx);
        GOOGLESQL_RET_CHECK_NE(struct_field, nullptr) << userid_column_name;
        GOOGLESQL_RET_CHECK(!is_ambiguous) << userid_column_name;
        struct_type = struct_field->type->AsStruct();

        std::unique_ptr<ResolvedExpr> get_userid_field_expr =
            MakeResolvedGetStructField(
                struct_field->type, std::move(resolved_expr_to_ref), found_idx);

        userid_column = allocator_->MakeCol(
            "$project", absl::StrCat("$", userid_column_field),
            get_userid_field_expr->type());
        resolved_expr_to_ref = std::move(get_userid_field_expr);
      }

    } else {
      const google::protobuf::Descriptor* descriptor =
          value_table_value_resolved_column.type()->AsProto()->descriptor();

      for (const std::string& userid_column_field : userid_column_name_path) {
        GOOGLESQL_RET_CHECK_NE(descriptor, nullptr) << userid_column_name;
        const google::protobuf::FieldDescriptor* field =
            ProtoType::FindFieldByNameIgnoreCase(descriptor,
                                                 userid_column_field);
        if (field == nullptr) {
          return MakeSqlErrorAtNode(*node)
                 << "Unable to find "
                 << absl::AsciiStrToLower(with_modifier_mode_name_.name)
                 << " user ID column " << userid_column_name
                 << " in value table fields";
        }
        descriptor = field->message_type();

        const Type* field_type;
        GOOGLESQL_RETURN_IF_ERROR(type_factory_->GetProtoFieldType(
            field,
            value_table_value_resolved_column.type()
                ->AsProto()
                ->CatalogNamePath(),
            &field_type));

        Value default_value;
        GOOGLESQL_RETURN_IF_ERROR(
            GetProtoFieldDefault(ProtoFieldDefaultOptions::FromFieldAndLanguage(
                                     field, resolver_->language()),
                                 field, field_type, &default_value));

        // Note that we use 'return_default_value_when_unset' as false here
        // because it indicates behavior for when the parent message is unset,
        // not when the extracted field is unset (whose behavior depends on the
        // field annotations, e.g., use_field_defaults).
        std::unique_ptr<ResolvedExpr> get_userid_field_expr =
            MakeResolvedGetProtoField(
                field_type, std::move(resolved_expr_to_ref), field,
                default_value,
                /*get_has_bit=*/false, ProtoType::GetFormatAnnotation(field),
                /*return_default_value_when_unset=*/false);
        userid_column = allocator_->MakeCol(
            "$project", absl::StrCat("$", userid_column_field),
            get_userid_field_expr->type());

        resolved_expr_to_ref = std::move(get_userid_field_expr);
      }
    }
    return MakeResolvedComputedColumn(userid_column,
                                      std::move(resolved_expr_to_ref));
  }

  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedTableScan(node));
    ResolvedTableScan* copy = GetUnownedTopOfStack<ResolvedTableScan>();

    if (!copy->table()->SupportsAnonymization()) {
      return absl::OkStatus();
    }

    // There exists an authoritative $uid column in the underlying table.
    //
    // For value tables, the Column itself doesn't exist in the table,
    // but its Column Name identifies the $uid field name of the value table
    // Value.
    GOOGLESQL_RET_CHECK(copy->table()->GetAnonymizationInfo().has_value());
    // Save the table alias with the $uid column. If the table doesn't have an
    // alias, copy->alias() returns an empty string and the $uid column alias
    // gets cleared.
    current_uid_.alias = copy->alias();
    const Column* table_col = copy->table()
                                  ->GetAnonymizationInfo()
                                  .value()
                                  .GetUserIdInfo()
                                  .get_column();
    if (table_col != nullptr) {
      // The userid column is an actual physical column from the table, so
      // find it and make sure it's part of the table's output column list.
      //
      // For each ResolvedColumn column_list[i], the matching table column is
      // table->GetColumn(column_index_list[i])
      for (int i = 0; i < copy->column_list_size(); ++i) {
        int j = copy->column_index_list(i);
        if (table_col == copy->table()->GetColumn(j)) {
          // If the original query selects the $uid column, reuse it.
          current_uid_.SetColumn(copy->column_list(i));
          GOOGLESQL_RETURN_IF_ERROR(ValidateUidColumnSupportsGrouping(*node));
          return absl::OkStatus();
        }
      }

      if (current_uid_.SetColumn(allocator_->MakeCol(copy->table()->Name(),
                                                     table_col->Name(),
                                                     table_col->GetType()))) {
        copy->add_column_list(current_uid_.column);

        int table_col_id = -1;
        for (int i = 0; i < copy->table()->NumColumns(); ++i) {
          if (table_col == copy->table()->GetColumn(i)) {
            table_col_id = i;
          }
        }
        GOOGLESQL_RET_CHECK_NE(table_col_id, -1);
        copy->add_column_index_list(table_col_id);
      }
    } else {
      // The userid column is identified by the column name.  This case
      // happens when the table is a value table, and the userid column is
      // derived from the value table's value.
      //
      // In this case, the $uid column is derived by fetching the
      // proper struct/proto field from the table value type.  We create
      // a new Project node on top of the input scan node that projects
      // all of the scan columns, along with one new column that is the
      // GetProto/StructField expression to extract the userid column.

      // First, ensure that the Table's row value is projected from the scan
      // (it may not be projected, for instance, if the full original query
      // is just ANON_COUNT(*)).
      //
      // As per the Table contract, value tables require their first column
      // (column 0) to be the value table value column.
      GOOGLESQL_RET_CHECK_GE(copy->table()->NumColumns(), 1);
      const Column* value_table_value_column = copy->table()->GetColumn(0);
      GOOGLESQL_RET_CHECK_NE(value_table_value_column, nullptr) << copy->table()->Name();
      GOOGLESQL_RET_CHECK(value_table_value_column->GetType()->IsStruct() ||
                value_table_value_column->GetType()->IsProto());

      ResolvedColumn value_table_value_resolved_column;
      GOOGLESQL_RETURN_IF_ERROR(ProjectValueTableScanRowValueIfNeeded(
          copy, value_table_value_column, &value_table_value_resolved_column));

      GOOGLESQL_RET_CHECK(value_table_value_resolved_column.IsInitialized())
          << value_table_value_resolved_column.DebugString();

      // Build an expression to extract the userid column from the
      // value table row value.
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ResolvedComputedColumn> projected_userid_column,
          MakeGetFieldComputedColumn(node,
                                     copy->table()
                                         ->GetAnonymizationInfo()
                                         .value()
                                         .UserIdColumnNamePath(),
                                     value_table_value_resolved_column));

      current_uid_.InitFromValueTable(projected_userid_column.get(),
                                      copy->alias());

      // Create a new Project node that projects the extracted userid
      // field from the table's row (proto or struct) value.
      std::vector<ResolvedColumn> project_column_list_with_userid =
          copy->column_list();
      project_column_list_with_userid.emplace_back(current_uid_.column);

      PushNodeToStack(MakeResolvedProjectScan(
          project_column_list_with_userid,
          MakeNodeVector(std::move(projected_userid_column)),
          ConsumeTopOfStack<ResolvedScan>()));
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateUidColumnSupportsGrouping(*node));
    return absl::OkStatus();
  }

  absl::Status VisitResolvedTVFScan(const ResolvedTVFScan* node) override {
    // We do not currently allow TVF arguments to contain anonymization,
    // because we are not invoking the rewriter on the TVF arguments yet.
    for (const std::unique_ptr<const ResolvedFunctionArgument>& arg :
         node->argument_list()) {
      TVFArgumentValidatorVisitor visitor(node->tvf()->FullName());
      GOOGLESQL_RETURN_IF_ERROR(arg->Accept(&visitor));
    }

    {
      ResolvedASTDeepCopyVisitor copy_visitor;
      GOOGLESQL_RETURN_IF_ERROR(node->Accept(&copy_visitor));
      GOOGLESQL_ASSIGN_OR_RETURN(auto copy,
                       copy_visitor.ConsumeRootNode<ResolvedTVFScan>());
      PushNodeToStack(std::move(copy));
    }
    ResolvedTVFScan* copy = GetUnownedTopOfStack<ResolvedTVFScan>();

    // The TVF doesn't produce user data or an anonymization userid column, so
    // we can return early.
    //
    // TODO: Figure out how we can take an early exit without the
    // copy. Does this method take ownership of <node>? Can we effectively
    // push it back onto the top of the stack (which requires a non-const
    // std::unique_ptr<ResolvedNode>)?  I tried creating a non-const unique_ptr
    // and that failed with what looked like a double free condition.  It's
    // unclear at present what the contracts are and how we can avoid the
    // needless copy.
    if (!copy->signature()->SupportsAnonymization()) {
      return absl::OkStatus();
    }

    if (copy->signature()->result_schema().is_value_table()) {
      GOOGLESQL_RET_CHECK_EQ(copy->signature()->result_schema().num_columns(), 1);
      const std::optional<const AnonymizationInfo> anonymization_info =
          copy->signature()->GetAnonymizationInfo();
      GOOGLESQL_RET_CHECK(anonymization_info.has_value());

      ResolvedColumn value_column;
      // Check if the value table column is already being projected.
      if (copy->column_list_size() > 0) {
        GOOGLESQL_RET_CHECK_EQ(copy->column_list_size(), 1);
        value_column = copy->column_list(0);
      } else {
        // Create and project the column of the entire proto.
        value_column = allocator_->MakeCol(
            copy->tvf()->Name(), "$value",
            copy->signature()->result_schema().column(0).type);
        copy->mutable_column_list()->push_back(value_column);
        copy->mutable_column_index_list()->push_back(0);
      }

      // Build an expression to extract the userid column from the
      // value table row value.
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ResolvedComputedColumn> projected_userid_column,
          MakeGetFieldComputedColumn(
              node, anonymization_info->UserIdColumnNamePath(), value_column));

      current_uid_.InitFromValueTable(projected_userid_column.get(),
                                      copy->alias());

      std::vector<ResolvedColumn> project_column_list_with_userid =
          copy->column_list();
      project_column_list_with_userid.emplace_back(current_uid_.column);

      PushNodeToStack(MakeResolvedProjectScan(
          project_column_list_with_userid,
          MakeNodeVector(std::move(projected_userid_column)),
          ConsumeTopOfStack<ResolvedScan>()));

      GOOGLESQL_RETURN_IF_ERROR(ValidateUidColumnSupportsGrouping(*node));
      return absl::OkStatus();
    }

    if (copy->signature()
            ->GetAnonymizationInfo()
            ->UserIdColumnNamePath()
            .size() > 1) {
      return MakeSqlErrorAtNode(*node)
             << "Nested user IDs are not currently supported for TVFs (in TVF "
             << copy->tvf()->FullName() << ")";
    }
    // Since we got to here, the TVF produces a userid column so we must ensure
    // that the column is projected for use in the anonymized aggregation.
    const std::string& userid_column_name = copy->signature()
                                                ->GetAnonymizationInfo()
                                                ->GetUserIdInfo()
                                                .get_column_name();

    // Check if the $uid column is already being projected.
    for (int i = 0; i < copy->column_list_size(); ++i) {
      // Look up the schema column name in the index list.
      const std::string& result_column_name =
          copy->signature()
              ->result_schema()
              .column(copy->column_index_list(i))
              .name;
      if (result_column_name == userid_column_name) {
        // Already projected, we're done.
        current_uid_.SetColumn(copy->column_list(i), copy->alias());
        return absl::OkStatus();
      }
    }

    // We need to project the $uid column. Look it up by name in the TVF schema
    // to get type information and record it in column_index_list.
    int tvf_userid_column_index = -1;
    for (int i = 0; i < copy->signature()->result_schema().num_columns(); ++i) {
      if (userid_column_name ==
          copy->signature()->result_schema().column(i).name) {
        tvf_userid_column_index = i;
        break;
      }
    }
    // Engines should normally validate the userid column when creating/adding
    // the TVF to the catalog whenever possible. However, this is not possible
    // in all cases - for example for templated TVFs where the output schema is
    // unknown until call time. So we produce a user-facing error message in
    // this case.
    if (tvf_userid_column_index == -1) {
      return MakeSqlErrorAtNode(*node)
             << "The " << absl::AsciiStrToLower(with_modifier_mode_name_.name)
             << " userid column " << userid_column_name << " defined for TVF "
             << copy->tvf()->FullName()
             << " was not found in the output schema of the TVF";
    }

    // Create and project the new $uid column.
    ResolvedColumn uid_column =
        allocator_->MakeCol(copy->tvf()->Name(), userid_column_name,
                            copy->signature()
                                ->result_schema()
                                .column(tvf_userid_column_index)
                                .type);

    // Per the ResolvedTVFScan contract:
    //   <column_list> is a set of ResolvedColumns created by this scan.
    //   These output columns match positionally with the columns in the output
    //   schema of <signature>
    // To satisfy this contract we must also insert the $uid column
    // positionally. The target location is at the first value in
    // column_index_list that is greater than tvf_userid_column_index (because
    // it is positional the indices must be ordered).
    int userid_column_insertion_index = 0;
    for (int i = 0; i < copy->column_index_list_size(); ++i) {
      if (copy->column_index_list(i) > tvf_userid_column_index) {
        userid_column_insertion_index = i;
        break;
      }
    }

    copy->mutable_column_list()->insert(
        copy->column_list().begin() + userid_column_insertion_index,
        uid_column);
    copy->mutable_column_index_list()->insert(
        copy->column_index_list().begin() + userid_column_insertion_index,
        tvf_userid_column_index);
    current_uid_.SetColumn(uid_column, copy->alias());

    return absl::OkStatus();
  }

  absl::Status VisitResolvedWithRefScan(
      const ResolvedWithRefScan* node) override {
    // No $uid column should have been encountered before now
    GOOGLESQL_RET_CHECK(!current_uid_.column.IsInitialized());

    // Lookup the referenced WITH entry
    auto it = absl::c_find_if(
        with_entries_,
        [&node](const std::unique_ptr<WithEntryRewriteState>& entry) {
          return node->with_query_name() ==
                 entry->original_entry.with_query_name();
        });
    GOOGLESQL_RET_CHECK(it != with_entries_.end())
        << "Failed to find WITH entry " << node->with_query_name();
    WithEntryRewriteState& entry = **it;

    if (entry.rewritten_entry == nullptr) {
      // This entry hasn't been rewritten yet, rewrite it as if it was just a
      // nested subquery.
      GOOGLESQL_ASSIGN_OR_RETURN(entry.rewritten_entry_owned,
                       ProcessNode(&entry.original_entry));
      // VisitResolvedWithEntry sets 'entry.rewritten_entry'
      GOOGLESQL_RET_CHECK_EQ(entry.rewritten_entry, entry.rewritten_entry_owned.get())
          << "Invalid rewrite state for " << node->with_query_name();
    }

    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedWithRefScan(node));
    if (entry.rewritten_uid && entry.rewritten_uid->column.IsInitialized()) {
      // The WITH entry contained a reference to user data, use its $uid column.
      auto* copy = GetUnownedTopOfStack<ResolvedWithRefScan>();
      // Update $uid column reference. The column_list in the
      // ResolvedWithRefScan matches positionally with the column_list in the
      // ResolvedWithEntry. But if the WithEntry explicitly selects columns and
      // does not include the $uid column, ResolvedWithRefScan will have one
      // less column.
      for (int i = 0;
           i < entry.rewritten_entry->with_subquery()->column_list().size() &&
           i < copy->column_list().size();
           ++i) {
        if (entry.rewritten_entry->with_subquery()
                ->column_list(i)
                .column_id() == entry.rewritten_uid->column.column_id()) {
          current_uid_.SetColumn(copy->column_list(i), "");
          return absl::OkStatus();
        }
      }
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedWithEntry(const ResolvedWithEntry* node) override {
    // No $uid column should have been encountered before now
    GOOGLESQL_RET_CHECK(!current_uid_.column.IsInitialized());
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedWithEntry(node));
    // Check if this entry is recorded in 'with_entries_', record the rewritten
    // result and $uid column if so.
    for (auto& entry : with_entries_) {
      if (node->with_query_name() == entry->original_entry.with_query_name()) {
        GOOGLESQL_RET_CHECK(entry->rewritten_entry == nullptr)
            << "WITH entry has already been rewritten: "
            << node->with_query_name();
        entry->rewritten_entry = GetUnownedTopOfStack<ResolvedWithEntry>();
        entry->rewritten_uid = std::move(current_uid_);
        current_uid_.Clear();
        return absl::OkStatus();
      }
    }
    // Record this entry and corresponding rewrite state for use by
    // VisitResolvedWithRefScan.
    with_entries_.emplace_back(new WithEntryRewriteState{
        .original_entry = *node,
        .rewritten_entry = GetUnownedTopOfStack<ResolvedWithEntry>(),
        .rewritten_uid = std::move(current_uid_)});
    current_uid_.Clear();
    return absl::OkStatus();
  }

  absl::Status VisitResolvedJoinScan(const ResolvedJoinScan* node) override {
    // No $uid column should have been encountered before now
    GOOGLESQL_RET_CHECK(!current_uid_.column.IsInitialized());

    // Make a simple copy of the join node that we can swap the left and right
    // scans out of later.
    ResolvedASTDeepCopyVisitor join_visitor;
    GOOGLESQL_RETURN_IF_ERROR(node->Accept(&join_visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedJoinScan> owned_copy,
                     join_visitor.ConsumeRootNode<ResolvedJoinScan>());
    PushNodeToStack(std::move(owned_copy));
    ResolvedJoinScan* copy = GetUnownedTopOfStack<ResolvedJoinScan>();

    // Rewrite and copy the left scan.
    PerUserRewriterVisitor left_visitor(allocator_, type_factory_, resolver_,
                                        with_entries_, with_modifier_mode_name_,
                                        public_groups_state_, parent_visitor_);
    GOOGLESQL_RETURN_IF_ERROR(node->left_scan()->Accept(&left_visitor));
    const ResolvedColumn& left_uid = left_visitor.current_uid_.column;
    found_public_groups_join_ |= left_visitor.GetFoundPublicGroupsJoin();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> left_scan,
                     left_visitor.ConsumeRootNode<ResolvedScan>());
    copy->set_left_scan(std::move(left_scan));

    // Rewrite and copy the right scan.
    PerUserRewriterVisitor right_visitor(
        allocator_, type_factory_, resolver_, with_entries_,
        with_modifier_mode_name_, public_groups_state_, parent_visitor_);
    GOOGLESQL_RETURN_IF_ERROR(node->right_scan()->Accept(&right_visitor));
    const ResolvedColumn& right_uid = right_visitor.current_uid_.column;
    found_public_groups_join_ |= right_visitor.GetFoundPublicGroupsJoin();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> right_scan,
                     right_visitor.ConsumeRootNode<ResolvedScan>());
    copy->set_right_scan(std::move(right_scan));

    // If the group selection strategy is public groups, check if this join is
    // suitable for use as a public group join and rewrite as such.
    if (public_groups_state_ != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          const std::optional<UidColumnState> public_groups_uid_column,
          public_groups_state_->MaybeRewritePublicGroupsJoinAndReturnUid(
              left_visitor.current_uid_, right_visitor.current_uid_,
              with_entries_, copy));
      if (public_groups_uid_column.has_value()) {
        // The public groups uid column might not be initialized in cases where
        // the user provided the uid column via an option.
        if (public_groups_uid_column->column.IsInitialized()) {
          current_uid_ = public_groups_uid_column.value();
          current_uid_.ProjectIfMissing(*copy);
        }
        found_public_groups_join_ = true;
        return absl::OkStatus();
      }
    }

    if (found_public_groups_join_) {
      return MakeSqlErrorAtNode(*copy)
             << CreateOptionNotAllowedWithPublicGroupsError("JOIN");
    }

    if (!left_uid.IsInitialized() && !right_uid.IsInitialized()) {
      // Two non-private tables
      // Nothing needs to be done
      return absl::OkStatus();
    } else if (left_uid.IsInitialized() && right_uid.IsInitialized()) {
      // Two private tables
      // Both tables have a $uid column, so we add AND Left.$uid = Right.$uid
      // to the join clause after checking that the types are equal and
      // comparable
      // TODO: Revisit if we want to allow $uid type coercion
      if (!left_uid.type()->Equals(right_uid.type())) {
        return MakeSqlErrorAtNode(*copy) << absl::StrCat(
                   "Joining two tables containing private data requires "
                   "matching user id column types, instead got ",
                   Type::TypeKindToString(left_uid.type()->kind(),
                                          resolver_->language().product_mode()),
                   " and ",
                   Type::TypeKindToString(
                       right_uid.type()->kind(),
                       resolver_->language().product_mode()));
      }
      if (!left_uid.type()->SupportsEquality(resolver_->language())) {
        return MakeSqlErrorAtNode(*copy) << absl::StrCat(
                   "Joining two tables containing private data requires "
                   "the user id column types to support equality comparison, "
                   "instead got ",
                   Type::TypeKindToString(
                       left_uid.type()->kind(),
                       resolver_->language().product_mode()));
      }

      // Reject joins with either missing join expressions, or join
      // expressions that don't join on $uid
      // TODO: also support uid constraints with a WHERE clause,
      // for example this query:
      //   select anon_count(*)
      //   from t1, t2
      //   where t1.uid = t2.uid;
      if (copy->join_expr() == nullptr) {
        return MakeSqlErrorAtNode(*copy) << absl::StrCat(
                   "Joins between tables containing private data must "
                   "explicitly join on the user id column in each table",
                   FormatJoinUidError(", add 'ON %s=%s'",
                                      left_visitor.current_uid_,
                                      right_visitor.current_uid_));
      }
      if (!JoinExprIncludesUid(copy->join_expr(), left_visitor.current_uid_,
                               right_visitor.current_uid_)) {
        return MakeSqlErrorAtNode(*copy->join_expr()) << absl::StrCat(
                   "Joins between tables containing private data must also "
                   "explicitly join on the user id column in each table",
                   FormatJoinUidError(
                       ", add 'AND %s=%s' to the join ON expression",
                       left_visitor.current_uid_, right_visitor.current_uid_));
      }
    }

    // At this point, we are either joining two private tables and Left.$uid
    // and Right.$uid are both valid, or joining a private table against a
    // non-private table and exactly one of {Left.$uid, Right.$uid} are valid.
    //
    // Now we want to check if a valid $uid column is being projected, and add
    // an appropriate one based on the join type if not.
    // INNER JOIN: project either Left.$uid or Right.$uid
    // LEFT JOIN:  project (and require) Left.$uid
    // RIGHT JOIN: project (and require) Right.$uid
    // FULL JOIN:  require Left.$uid and Right.$uid, project
    //             COALESCE(Left.$uid, Right.$uid)
    current_uid_.column.Clear();

    switch (node->join_type()) {
      case ResolvedJoinScan::INNER:
        // If both join inputs have a $uid column then project the $uid from
        // the left.  Otherwise project the $uid column from the join input
        // that contains it.
        current_uid_ = (left_uid.IsInitialized() ? left_visitor.current_uid_
                                                 : right_visitor.current_uid_);
        current_uid_.ProjectIfMissing(*copy);
        return absl::OkStatus();

      case ResolvedJoinScan::LEFT:
        // We must project the $uid from the Left table in a left outer join,
        // otherwise we end up with rows with NULL $uid.
        if (!left_uid.IsInitialized()) {
          return MakeSqlErrorAtNode(*copy->left_scan())
                 << "The left table in a LEFT OUTER join must contain user "
                    "data";
        }
        current_uid_ = left_visitor.current_uid_;
        current_uid_.ProjectIfMissing(*copy);
        return absl::OkStatus();

      case ResolvedJoinScan::RIGHT:
        // We must project the $uid from the Right table in a right outer
        // join, otherwise we end up with rows with NULL $uid.
        if (!right_uid.IsInitialized()) {
          return MakeSqlErrorAtNode(*copy->right_scan())
                 << "The right table in a RIGHT OUTER join must contain user "
                    "data";
        }
        current_uid_ = right_visitor.current_uid_;
        current_uid_.ProjectIfMissing(*copy);
        return absl::OkStatus();

      case ResolvedJoinScan::FULL:
        // Full outer joins require both tables to have an attached $uid. We
        // project COALESCE(Left.$uid, Right.$uid) because up to one of the
        // $uid columns may be null for each output row.
        if (!left_uid.IsInitialized() || !right_uid.IsInitialized()) {
          return MakeSqlErrorAtNode(left_uid.IsInitialized()
                                        ? *copy->right_scan()
                                        : *copy->left_scan())
                 << "Both tables in a FULL OUTER join must contain user "
                    "data";
        }

        // Full outer join, the result $uid column is
        // COALESCE(Left.$uid, Right.$uid).
        // TODO: This generated column is an internal name and
        // isn't selectable by the end user, this makes full outer joins
        // unusable in nested queries. Improve either error messages or change
        // query semantics around full outer joins to fix this usability gap.
        std::vector<ResolvedColumn> wrapped_column_list = copy->column_list();
        copy->add_column_list(left_uid);
        copy->add_column_list(right_uid);

        std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
        arguments.emplace_back(BuildResolvedColumnRef(left_uid));
        arguments.emplace_back(BuildResolvedColumnRef(right_uid));
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<ResolvedExpr> coalesced_uid_function,
            ResolveFunctionCall("coalesce", std::move(arguments),
                                /*named_arguments=*/{}, resolver_));

        ResolvedColumn uid_column = allocator_->MakeCol(
            "$join", "$uid", coalesced_uid_function->type());
        auto coalesced_uid_column = MakeResolvedComputedColumn(
            uid_column, std::move(coalesced_uid_function));
        if (current_uid_.SetColumn(coalesced_uid_column->column())) {
          wrapped_column_list.emplace_back(current_uid_.column);
        }

        PushNodeToStack(MakeResolvedProjectScan(
            wrapped_column_list,
            MakeNodeVector(std::move(coalesced_uid_column)),
            ConsumeTopOfStack<ResolvedScan>()));

        return absl::OkStatus();
    }
  }

  // Nested AggregateScans require special handling. The differential privacy
  // spec requires that each such scan GROUPs BY the $uid column. But GROUP BY
  // columns are implemented as computed columns in GoogleSQL, so we need to
  // inspect the group by list and update 'current_uid_column_' with the new
  // ResolvedColumn.
  absl::Status VisitResolvedAggregateScan(
      const ResolvedAggregateScan* node) override {
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedAggregateScan(node));
    if (!current_uid_.column.IsInitialized()) {
      // Table doesn't contain any private data, so do nothing.
      return absl::OkStatus();
    }
    ResolvedAggregateScan* copy = GetUnownedTopOfStack<ResolvedAggregateScan>();

    // If the source table is a value table the uid column refs will be
    // GetProtoField or GetStructField expressions, replace them with ColumnRef
    // expressions.
    copy->set_group_by_list(current_uid_.SubstituteUidComputedColumn(
        copy->release_group_by_list()));

    // AggregateScan nodes in the per-user transform must always group by
    // $uid. Check if we already do so, and add a group by element if not.
    ResolvedColumn group_by_uid_col;
    for (const auto& col : copy->group_by_list()) {
      if (col->expr()->node_kind() != googlesql::RESOLVED_COLUMN_REF) {
        // Even if 'group by $uid+0' is equivalent to 'group by $uid', these
        // kind of operations are hard to verify so let's ignore them.
        continue;
      }
      const ResolvedColumn& grouped_by_column =
          col->expr()->GetAs<ResolvedColumnRef>()->column();
      if (grouped_by_column.column_id() == current_uid_.column.column_id()) {
        group_by_uid_col = col->column();
        break;
      }
    }

    if (group_by_uid_col.IsInitialized()) {
      // Point current_uid_column_ to the updated group by column, and verify
      // that the original query projected it.
      if (current_uid_.SetColumn(group_by_uid_col)) {
        for (const ResolvedColumn& col : copy->column_list()) {
          if (col == current_uid_.column) {
            // Explicitly projecting a column removes the alias.
            current_uid_.alias = "";
            return absl::OkStatus();
          }
        }
      }
    }
    return absl::OkStatus();
  }

  // For nested projection operations, we require the query to explicitly
  // project $uid.
  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override {
    GOOGLESQL_RETURN_IF_ERROR(
        MaybeAttachParseLocation(CopyVisitResolvedProjectScan(node), *node));
    if (GetFoundPublicGroupsJoin() && node->expr_list_size() != 0) {
      // Project scans with computed columns are not allowed.
      return MakeSqlErrorAtNode(*node)
             << CreateOptionNotAllowedWithPublicGroupsError();
    }

    if (!current_uid_.column.IsInitialized()) {
      return absl::OkStatus();
    }
    auto* copy = GetUnownedTopOfStack<ResolvedProjectScan>();

    // If the source table is a value table the uid column refs will be
    // GetProtoField or GetStructField expressions, replace them with ColumnRef
    // expressions.
    copy->set_expr_list(
        current_uid_.SubstituteUidComputedColumn(copy->release_expr_list()));

    for (const ResolvedColumn& col : copy->column_list()) {
      if (col.column_id() == current_uid_.column.column_id()) {
        // Explicitly projecting a column removes the alias.
        current_uid_.alias = "";
        return absl::OkStatus();
      }
    }

    // TODO: Ensure that the $uid column name in the error message
    // is appropriately alias/qualified.
    return MakeSqlErrorAtNode(*copy) << absl::StrFormat(
               "Subqueries of %s queries must explicitly SELECT the userid "
               "column '%s'",
               absl::AsciiStrToLower(with_modifier_mode_name_.name),
               current_uid_.ToString());
  }

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    // Expression subqueries aren't allowed to read from tables or TVFs that
    // have $uid columns. See (broken link)
    ExpressionSubqueryRewriterVisitor subquery_visitor;
    GOOGLESQL_RETURN_IF_ERROR(node->Accept(&subquery_visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(auto copy,
                     subquery_visitor.ConsumeRootNode<ResolvedSubqueryExpr>());
    PushNodeToStack(std::move(copy));
    return absl::OkStatus();
  }

  absl::Status VisitResolvedSetOperationScan(
      const ResolvedSetOperationScan* node) override {
    std::vector<std::unique_ptr<const ResolvedSetOperationItem>>
        rewritten_input_items;
    std::vector<UidColumnState> uids;

    // Rewrite each input item.
    for (const auto& input_item : node->input_item_list()) {
      PerUserRewriterVisitor input_item_visitor(
          allocator_, type_factory_, resolver_, with_entries_,
          with_modifier_mode_name_, public_groups_state_, parent_visitor_);
      GOOGLESQL_RETURN_IF_ERROR(input_item->Accept(&input_item_visitor));
      UidColumnState uid = input_item_visitor.current_uid_;
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ResolvedSetOperationItem> rewritten_input_item,
          input_item_visitor.ConsumeRootNode<ResolvedSetOperationItem>());

      if (uid.column.IsInitialized()) {
        // The $uid column should be included in the output column list, set
        // operation columns aren't trimmed at this point.
        GOOGLESQL_RET_CHECK(std::find(rewritten_input_item->output_column_list().begin(),
                            rewritten_input_item->output_column_list().end(),
                            uid.column) !=
                  rewritten_input_item->output_column_list().end())
            << "Column " << uid.ToString()
            << " not included in set operation output";
      }

      rewritten_input_items.push_back(std::move(rewritten_input_item));
      uids.push_back(std::move(uid));
    }

    std::unique_ptr<ResolvedSetOperationScan> copy =
        MakeResolvedSetOperationScan(node->column_list(), node->op_type(),
                                     std::move(rewritten_input_items));

    const ResolvedSetOperationItem& reference_input_item =
        *copy->input_item_list(0);
    const UidColumnState& reference_uid = uids[0];

    // Validate that either all input items have a $uid column, or that none do.
    for (int i = 1; i < copy->input_item_list_size(); ++i) {
      if (reference_uid.column.IsInitialized() !=
          uids[i].column.IsInitialized()) {
        std::string select_with_identifier_lower =
            absl::AsciiStrToLower(with_modifier_mode_name_.name);
        absl::string_view a_or_an =
            with_modifier_mode_name_.uses_a_article ? "a" : "an";
        return MakeSqlErrorAtNode(*node) << absl::StrFormat(
                   "Not all queries in %s are %s-enabled table "
                   "expressions; query 1 %s %s %s-enabled table "
                   "expression, but query %d %s",
                   SetOperationTypeToString(copy->op_type()),
                   select_with_identifier_lower,
                   reference_uid.column.IsInitialized() ? "is" : "is not",
                   a_or_an, select_with_identifier_lower, i + 1,
                   uids[i].column.IsInitialized() ? "is" : "is not");
      }
    }

    // If input items set the $uid column, ensure that they all point to the
    // same column offset.
    if (reference_uid.column.IsInitialized()) {
      std::size_t reference_uid_index =
          std::find(reference_input_item.output_column_list().begin(),
                    reference_input_item.output_column_list().end(),
                    reference_uid.column) -
          reference_input_item.output_column_list().begin();
      GOOGLESQL_RET_CHECK_NE(reference_uid_index,
                   reference_input_item.output_column_list_size());
      for (int i = 1; i < copy->input_item_list_size(); ++i) {
        const auto& column_list =
            copy->input_item_list(i)->output_column_list();
        std::size_t uid_index =
            std::find(column_list.begin(), column_list.end(), uids[i].column) -
            column_list.begin();
        if (reference_uid_index != uid_index) {
          return MakeSqlErrorAtNode(*node) << absl::StrFormat(
                     "Queries in %s have mismatched userid columns; query 1 "
                     "has userid column '%s' in position %d, query %d has "
                     "userid column '%s' in position %d",
                     SetOperationTypeToString(copy->op_type()),
                     reference_uid.ToString(), reference_uid_index + 1, i + 1,
                     uids[i].ToString(), uid_index + 1);
        }
      }

      current_uid_.SetColumn(
          copy->column_list(static_cast<int>(reference_uid_index)));
    }
    PushNodeToStack(std::move(copy));

    return absl::OkStatus();
  }

  absl::Status VisitResolvedDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* node) override {
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_DIFFERENTIAL_PRIVACY_NESTED)) {
      return ResolvedASTDeepCopyVisitor::
          VisitResolvedDifferentialPrivacyAggregateScan(node);
    }
    // Rewrite nested queries.
    GOOGLESQL_RETURN_IF_ERROR(node->Accept(parent_visitor_));
    GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten_scan,
                     parent_visitor_->ConsumeRootNode<ResolvedScan>());
    PushNodeToStack(std::move(rewritten_scan));

    // The $uid column should not be set for nested queries.
    GOOGLESQL_RET_CHECK(!current_uid_.column.IsInitialized());

    return absl::OkStatus();
  }

/////////////////////////////////////////////////////////////////////////////
// For these scans, the $uid column can be implicitly projected
/////////////////////////////////////////////////////////////////////////////
#define PROJECT_UID(resolved_scan)                                        \
  absl::Status Visit##resolved_scan(const resolved_scan* node) override { \
    GOOGLESQL_RETURN_IF_ERROR(CopyVisit##resolved_scan(node));                      \
    if (GetFoundPublicGroupsJoin()) {                                     \
      return MakeSqlErrorAtNode(*node)                                    \
             << CreateOptionNotAllowedWithPublicGroupsError();            \
    }                                                                     \
    if (!current_uid_.column.IsInitialized()) {                           \
      return absl::OkStatus();                                            \
    }                                                                     \
    auto* scan = GetUnownedTopOfStack<resolved_scan>();                   \
    current_uid_.ProjectIfMissing(*scan);                                 \
    return absl::OkStatus();                                              \
  }
  PROJECT_UID(ResolvedArrayScan);
  PROJECT_UID(ResolvedSingleRowScan);
  PROJECT_UID(ResolvedFilterScan);
  PROJECT_UID(ResolvedOrderByScan);
  PROJECT_UID(ResolvedLimitOffsetScan);
  PROJECT_UID(ResolvedSampleScan);
#undef PROJECT_UID

/////////////////////////////////////////////////////////////////////////////
// As of now unsupported per-user scans
// TODO: Provide a user-friendly error message
/////////////////////////////////////////////////////////////////////////////
#define UNSUPPORTED(resolved_scan)                                         \
  absl::Status Visit##resolved_scan(const resolved_scan* node) override {  \
    return MakeSqlErrorAtNode(*node)                                       \
           << "Unsupported scan type inside of "                           \
           << with_modifier_mode_name_.name << " clause: " #resolved_scan; \
  }
  UNSUPPORTED(ResolvedAnalyticScan);
  UNSUPPORTED(ResolvedRelationArgumentScan);
  UNSUPPORTED(ResolvedRecursiveScan);
  UNSUPPORTED(ResolvedRecursiveRefScan);
#undef UNSUPPORTED

  // Join errors are special cased because:
  // 1) they reference uid columns from two different table subqueries
  // 2) we want to suggest table names as implicit aliases, when helpful
  static std::string FormatJoinUidError(
      const absl::FormatSpec<std::string, std::string>& format_str,
      UidColumnState column1, UidColumnState column2) {
    if (IsInternalAlias(column1.column.name()) ||
        IsInternalAlias(column2.column.name())) {
      return "";
    }
    // Use full table names as uid aliases where doing so reduces ambiguity:
    // 1) the tables must have different names
    // 2) the uid columns must have the same name
    // 3) the query doesn't specify a table alias
    if (column1.column.table_name() != column2.column.table_name() &&
        column1.column.name() == column2.column.name()) {
      if (column1.alias.empty()) column1.alias = column1.column.table_name();
      if (column2.alias.empty()) column2.alias = column2.column.table_name();
    }
    return absl::StrFormat(format_str, column1.ToString(), column2.ToString());
  }

  absl::Status ValidateUidColumnSupportsGrouping(const ResolvedNode& node) {
    return ValidateUidColumnTypeSupportsGrouping(current_uid_.column.type(),
                                                 node, resolver_->language());
  }

  // Replace existing columns with new columns for public groups, if they should
  // be replaced.
  absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
      const ResolvedColumn& column) override {
    if (column_replacements_.contains(column)) {
      return column_replacements_.at(column);
    }
    return column;
  }

  ColumnFactory* allocator_;   // unowned
  TypeFactory* type_factory_;  // unowned
  Resolver* resolver_;         // unowned
  std::vector<std::unique_ptr<WithEntryRewriteState>>&
      with_entries_;  // unowned

  WithModifierModeName with_modifier_mode_name_;
  UidColumnState current_uid_;
  PublicGroupsState* public_groups_state_;  // unowned
  bool found_public_groups_join_ = false;
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> column_replacements_;
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn>
      public_groups_join_replacements_;  // original -> replaced

  RewriterVisitor* parent_visitor_;  // unowned
};

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
RewriterVisitor::ChooseUidColumn(
    const ResolvedAggregateScanBase* node,
    WithModifierModeName with_modifier_mode_name,
    const UidColumnState& per_user_visitor_uid_column_state,
    std::optional<const ResolvedExpr*> options_uid_column) {
  if (options_uid_column.has_value()) {
    if (per_user_visitor_uid_column_state.column.IsInitialized()) {
      return MakeSqlErrorAtNode(*node)
             << "privacy_unit_column option cannot override the privacy unit "
                "column set in the table metadata: "
             << per_user_visitor_uid_column_state.ToString();
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateUidColumnTypeSupportsGrouping(
        options_uid_column.value()->type(), *options_uid_column.value(),
        resolver_->language()));
    return ResolvedASTDeepCopyVisitor::Copy(options_uid_column.value());
  }

  if (per_user_visitor_uid_column_state.column.IsInitialized()) {
    return BuildResolvedColumnRef(per_user_visitor_uid_column_state.column);
  }

  absl::string_view error_message_prefix =
      resolver_->language().LanguageFeatureEnabled(
          FEATURE_PIPE_AGGREGATE_WITH_DIFFERENTIAL_PRIVACY)
          ? (with_modifier_mode_name.uses_a_article ? "A " : "An ")
          : "A SELECT WITH ";

  return MakeSqlErrorAtNode(*node)
         << error_message_prefix << with_modifier_mode_name.name
         << " query must query data with a specified privacy unit column";
}

struct RewritePerUserTransformResult {
  // The rewritten per-user transform, possibly re-wrapped in another
  // ResolvedScan.
  std::unique_ptr<ResolvedScan> rewritten_scan;

  // The original UID column extracted from the per-user transform.
  //
  // If original UID is not a column (e.g. if it was specified via the
  // privacy_unit_column query option) then this value may be uninitialized.
  // Note that if the table does not have a UID column at all, then
  // RewritePerUserTransformResult will return a failed status. The returned
  // value is used when identifying aggregations that perform "count distinct
  // users" aggregations.
  std::unique_ptr<const ResolvedExpr> inner_uid_column;
};

absl::StatusOr<RewritePerUserTransformResult>
RewriterVisitor::RewritePerUserTransform(
    const ResolvedAggregateScanBase* node,
    WithModifierModeName with_modifier_mode_name,
    std::optional<const ResolvedExpr*> options_uid_column,
    PublicGroupsState* public_groups_state) {
  PerUserRewriterVisitor per_user_visitor(
      allocator_, type_factory_, resolver_, with_entries_,
      with_modifier_mode_name, public_groups_state, this);
  GOOGLESQL_RETURN_IF_ERROR(node->input_scan()->Accept(&per_user_visitor));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> rewritten_scan,
                   per_user_visitor.ConsumeRootNode<ResolvedScan>());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> inner_uid_column,
                   ChooseUidColumn(node, with_modifier_mode_name,
                                   per_user_visitor.uid_column_state(),
                                   std::move(options_uid_column)));
  return RewritePerUserTransformResult{
      .rewritten_scan = std::move(rewritten_scan),
      .inner_uid_column = std::move(inner_uid_column),
  };
}

struct RewriteInnerAggregateListResult {
  // A projected intermediate column that points to the inner_uid_column
  // identified while rewriting the per-user transform.
  ResolvedColumn uid_column;

  // This map is populated when the per-user aggregate list is resolved. It maps
  // the existing columns in the original DP aggregate scan `column_list` to the
  // new intermediate columns that splice together the per-user and cross-user
  // aggregate/groupby lists.
  std::map<ResolvedColumn, ResolvedColumn> injected_col_map;

  // The rewritten aggregate columns produced by
  // InnerAggregateListRewriterVisitor.
  std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
      rewritten_aggregate_list;

  // The rewritten group by columns produced by
  // InnerAggregateListRewriterVisitor.
  std::vector<std::unique_ptr<ResolvedComputedColumn>> rewritten_group_by_list;

  // The value of InnerAggregateListRewriterVisitor::order_by_column().
  ResolvedColumn order_by_column;
};

absl::StatusOr<RewriteInnerAggregateListResult>
RewriterVisitor::RewriteInnerAggregateList(
    const ResolvedAggregateScanBase* const node,
    WithModifierModeName with_modifier_mode_name,
    std::unique_ptr<const ResolvedExpr> inner_uid_column) {
  std::map<ResolvedColumn, ResolvedColumn> injected_col_map;
  InnerAggregateListRewriterVisitor inner_rewriter_visitor(
      &injected_col_map, allocator_, resolver_, with_modifier_mode_name.name);
  GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten_aggregate_list,
                   inner_rewriter_visitor.RewriteAggregateColumns(node));
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ResolvedComputedColumn>>
                       rewritten_group_by_list,
                   inner_rewriter_visitor.RewriteGroupByColumns(node));

  // This is validated by PerUserRewriterVisitor.
  GOOGLESQL_RET_CHECK(inner_uid_column->type()->SupportsGrouping(resolver_->language()));

  // Group by the $uid column.
  ResolvedColumn uid_column =
      allocator_->MakeCol("$group_by", "$uid", inner_uid_column->type());
  rewritten_group_by_list.emplace_back(
      MakeResolvedComputedColumn(uid_column, std::move(inner_uid_column)));

  return RewriteInnerAggregateListResult{
      .uid_column = uid_column,
      .injected_col_map = std::move(injected_col_map),
      .rewritten_aggregate_list = std::move(rewritten_aggregate_list),
      .rewritten_group_by_list = std::move(rewritten_group_by_list),
      .order_by_column = inner_rewriter_visitor.order_by_column(),
  };
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
RewriterVisitor::ProjectOrderByForSampledAggregation(
    std::unique_ptr<ResolvedScan> node, const ResolvedColumn& order_by_column) {
  if (!order_by_column.IsInitialized()) {
    return node;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> rand_function,
                   ResolveFunctionCall("rand", /*arguments=*/{},
                                       /*named_arguments=*/{}, resolver_));
  std::vector<std::unique_ptr<ResolvedComputedColumn>> order_by_expr_list;
  std::unique_ptr<ResolvedComputedColumn> rand_expr =
      MakeResolvedComputedColumn(order_by_column, std::move(rand_function));
  GOOGLESQL_RET_CHECK(rand_expr != nullptr);
  order_by_expr_list.emplace_back(std::move(rand_expr));

  ResolvedColumnList wrapper_column_list = node->column_list();
  for (const auto& computed_column : order_by_expr_list) {
    wrapper_column_list.push_back(computed_column->column());
  }
  return MakeResolvedProjectScan(
      wrapper_column_list, std::move(order_by_expr_list), std::move(node));
}

absl::StatusOr<std::unique_ptr<ResolvedComputedColumn>>
RewriterVisitor::MakeGroupSelectionThresholdFunctionColumn(
    const ResolvedAnonymizedAggregateScan* scan_node) {
  std::vector<std::unique_ptr<const ResolvedExpr>> argument_list;
  // Create function call argument list logically equivalent to:
  //   ANON_SUM(1 CLAMPED BETWEEN 0 AND 1)
  argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(1)));
  argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(0)));
  argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(1)));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> call,
                   ResolveFunctionCall("anon_sum", std::move(argument_list),
                                       /*named_arguments=*/{}, resolver_));
  GOOGLESQL_RET_CHECK_EQ(call->node_kind(), RESOLVED_AGGREGATE_FUNCTION_CALL)
      << call->DebugString();
  // k_threshold is old name used in ResolvedAnonymizedAggregateScan it got
  // updated to group selection threshold see: (broken link).
  ResolvedColumn uid_column =
      allocator_->MakeCol("$anon", "$k_threshold_col", call->type());
  return MakeResolvedComputedColumn(uid_column, std::move(call));
}

const ResolvedExpr* GetGroupSelectionEpsilonFromOptions(
    const ResolvedDifferentialPrivacyAggregateScan* node) {
  auto it =
      absl::c_find_if(node->option_list(),
                      [](const std::unique_ptr<const ResolvedOption>& option) {
                        return googlesql_base::CaseEqual(
                            option->name(), kGroupSelectionOptionName);
                      });
  if (it == node->option_list().end()) {
    // Not found.
    return nullptr;
  }
  return (*it)->value();
}

constexpr const ResolvedExpr* GetGroupSelectionEpsilonFromOptions(
    const ResolvedAnonymizedAggregateScan* node) {
  // Not supported for SELECT WITH ANONYMIZATION.
  return nullptr;
}

absl::StatusOr<std::unique_ptr<ResolvedComputedColumn>>
RewriterVisitor::MakeGroupSelectionThresholdFunctionColumn(
    const ResolvedDifferentialPrivacyAggregateScan* scan_node) {
  std::vector<std::unique_ptr<const ResolvedExpr>> argument_list;
  std::vector<NamedArgumentInfo> argument_info;
  // Create function call argument list logically equivalent to:
  //   SUM(1, contribution_bounds_per_group => (0, 1))
  argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(1)));

  const StructType* contribution_bounds_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeStructType(
      {{"", type_factory_->get_int64()}, {"", type_factory_->get_int64()}},
      &contribution_bounds_type));
  GOOGLESQL_ASSIGN_OR_RETURN(auto value,
                   Value::MakeStruct(contribution_bounds_type,
                                     {Value::Int64(0), Value::Int64(1)}));
  argument_list.emplace_back(MakeResolvedLiteral(value));
  argument_info.emplace_back(
      NamedArgumentInfo(kContributionBoundsPerGroup, 1, scan_node));

  if (resolver_->language().LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_PER_AGGREGATION_BUDGET)) {
    // Use the group_selection_epsilon from the query options, in case it has
    // been set.
    const ResolvedExpr* group_selection_epsilon_expr =
        GetGroupSelectionEpsilonFromOptions(scan_node);
    if (group_selection_epsilon_expr != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ResolvedExpr> copy,
          ResolvedASTDeepCopyVisitor::Copy(group_selection_epsilon_expr));
      argument_list.emplace_back(std::move(copy));
      argument_info.emplace_back(NamedArgumentInfo(kEpsilon, 2, scan_node));
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> call,
      ResolveFunctionCall("$differential_privacy_sum", std::move(argument_list),
                          argument_info, resolver_));
  GOOGLESQL_RET_CHECK_EQ(call->node_kind(), RESOLVED_AGGREGATE_FUNCTION_CALL)
      << call->DebugString();
  ResolvedColumn uid_column = allocator_->MakeCol(
      "$differential_privacy", "$group_selection_threshold_col", call->type());
  return MakeResolvedComputedColumn(uid_column, std::move(call));
}

template <class NodeType>
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
RewriterVisitor::CreateCountDistinctPrivacyIdsColumn(
    const NodeType* original_input_scan,
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>& aggregate_list) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedComputedColumn> group_selection_threshold_col,
      MakeGroupSelectionThresholdFunctionColumn(original_input_scan));
  std::unique_ptr<ResolvedExpr> group_selection_threshold_expr =
      BuildResolvedColumnRef(group_selection_threshold_col->column());
  aggregate_list.emplace_back(std::move(group_selection_threshold_col));
  return group_selection_threshold_expr;
}

std::unique_ptr<ResolvedAnonymizedAggregateScan>
RewriterVisitor::CreateAggregateScan(
    const ResolvedAnonymizedAggregateScan* node,
    std::unique_ptr<ResolvedScan> input_scan,
    std::vector<std::unique_ptr<ResolvedComputedColumn>> outer_group_by_list,
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
        outer_aggregate_list,
    std::unique_ptr<ResolvedExpr> group_selection_threshold_expr,
    std::vector<std::unique_ptr<ResolvedOption>> resolved_options) {
  auto result = MakeResolvedAnonymizedAggregateScan(
      node->column_list(), std::move(input_scan),
      std::move(outer_group_by_list), std::move(outer_aggregate_list),
      /*grouping_set_list=*/{}, /*rollup_column_list=*/{},
      /*grouping_call_list=*/{}, std::move(group_selection_threshold_expr),
      std::move(resolved_options));
  return result;
}

std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan>
RewriterVisitor::CreateAggregateScan(
    const ResolvedDifferentialPrivacyAggregateScan* node,
    std::unique_ptr<ResolvedScan> input_scan,
    std::vector<std::unique_ptr<ResolvedComputedColumn>> outer_group_by_list,
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
        outer_aggregate_list,
    std::unique_ptr<ResolvedExpr> group_selection_threshold_expr,
    std::vector<std::unique_ptr<ResolvedOption>> resolved_options) {
  auto result = MakeResolvedDifferentialPrivacyAggregateScan(
      node->column_list(), std::move(input_scan),
      std::move(outer_group_by_list), std::move(outer_aggregate_list),
      /*grouping_set_list=*/{}, /*rollup_column_list=*/{},
      /*grouping_call_list=*/{}, std::move(group_selection_threshold_expr),
      std::move(resolved_options));
  return result;
}

// Verifies that `option` is a resolved literal containing either a valid int64
// value that either is NULL or is strictly positive and fits into an int32.
// Returns the value of the resolved literal.
//
// In some places, the privacy libraries only support int32 values (e.g.
// max_groups_contributed), but those options are declared as int64 values in
// SQL.
absl::StatusOr<Value> ParseNullOrPositiveInt32Option(
    const ResolvedOption& option, absl::string_view dp_option_error_prefix) {
  googlesql_base::StatusBuilder invalid_value_message =
      MakeSqlErrorAtNode(option)
      << dp_option_error_prefix << " must be an INT64 literal between 1 and "
      << std::numeric_limits<int32_t>::max();
  const bool is_valid_int64_value =
      option.value()->node_kind() == RESOLVED_LITERAL &&
      option.value()->GetAs<ResolvedLiteral>()->type()->IsInt64() &&
      option.value()->GetAs<ResolvedLiteral>()->value().is_valid();
  if (!is_valid_int64_value) {
    return invalid_value_message;
  }
  Value option_value = option.value()->GetAs<ResolvedLiteral>()->value();

  if (option_value.is_null()) {
    return option_value;
  } else if (option_value.int64_value() < 1 ||
             option_value.int64_value() > std::numeric_limits<int32_t>::max()) {
    return invalid_value_message;
  }
  return option_value;
}

absl::StatusOr<DifferentialPrivacyEnums::GroupSelectionStrategy>
ValidateAndParseGroupSelectionStrategyEnum(
    const ResolvedOption& option, const LanguageOptions& language_options,
    const absl::string_view error_prefix) {
  if (!option.value()->Is<ResolvedLiteral>() ||
      !option.value()->GetAs<ResolvedLiteral>()->type()->IsEnum() ||
      option.value()->GetAs<ResolvedLiteral>()->value().is_null()) {
    return MakeSqlErrorAtNode(option)
           << error_prefix << " must be a non-NULL enum literal";
  }
  const int32_t enum_value =
      option.value()->GetAs<ResolvedLiteral>()->value().enum_value();
  GOOGLESQL_RET_CHECK(
      DifferentialPrivacyEnums::GroupSelectionStrategy_IsValid(enum_value))
      << error_prefix << " is invalid: " << enum_value;
  const auto strategy =
      static_cast<DifferentialPrivacyEnums::GroupSelectionStrategy>(enum_value);
  if (strategy == DifferentialPrivacyEnums::PUBLIC_GROUPS) {
    if (!language_options.LanguageFeatureEnabled(
            FEATURE_DIFFERENTIAL_PRIVACY_PUBLIC_GROUPS)) {
      return MakeSqlErrorAtNode(option)
             << error_prefix << " PUBLIC_GROUPS has not been enabled";
    }
    if (!language_options.LanguageFeatureEnabled(FEATURE_WITH_ON_SUBQUERY)) {
      return MakeSqlErrorAtNode(option)
             << error_prefix
             << " PUBLIC_GROUPS is not supported without support for WITH "
                "subqueries";
    }
  }
  return strategy;
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
RewriterVisitor::AddCrossPartitionSampleScan(
    std::unique_ptr<ResolvedScan> input_scan,
    std::optional<int64_t> max_groups_contributed, ResolvedColumn uid_column,
    PublicGroupsState* public_groups_state) {
  if (max_groups_contributed.has_value()) {
    std::vector<std::unique_ptr<const ResolvedExpr>> partition_by_list;
    partition_by_list.push_back(BuildResolvedColumnRef(uid_column));
    const std::vector<ResolvedColumn>& column_list = input_scan->column_list();
    input_scan = MakeResolvedSampleScan(
        column_list, std::move(input_scan),
        /*method=*/"RESERVOIR",
        MakeResolvedLiteral(Value::Int64(*max_groups_contributed)),
        ResolvedSampleScan::ROWS, /*repeatable_argument=*/nullptr,
        /*weight_column=*/nullptr, std::move(partition_by_list));
    if (public_groups_state != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(input_scan,
                       public_groups_state->CreateJoinScanAfterSampleScan(
                           std::move(input_scan), *allocator_));
    }
  }
  return input_scan;
}

// Returns ResolvedGetProtoFieldExpr for extracting a submessage field
// from a ResolvedColumn containing a proto.
absl::StatusOr<std::unique_ptr<ResolvedGetProtoField>>
ExtractSubmessageFromProtoColumn(absl::string_view field_name,
                                 const ResolvedColumn& proto_column,
                                 const google::protobuf::Descriptor& proto_descriptor,
                                 TypeFactory& type_factory) {
  const google::protobuf::FieldDescriptor* field =
      proto_descriptor.FindFieldByName(field_name);
  GOOGLESQL_RET_CHECK(field != nullptr);

  const Type* field_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory.GetProtoFieldType(
      field, proto_column.type()->AsProto()->CatalogNamePath(), &field_type));

  return MakeResolvedGetProtoField(
      field_type, BuildResolvedColumnRef(proto_column), field,
      Value::Null(field_type),
      /* get_has_bit=*/false, ProtoType::GetFormatAnnotation(field),
      /* return_default_value_when_unset=*/false);
}

// Returns ResolvedGetProtoFieldExpr for extracting an int64 field
// from the provided ResolvedGetProtoField.
absl::StatusOr<std::unique_ptr<ResolvedGetProtoField>> ExtractIntFromProtoExpr(
    absl::string_view field_name,
    std::unique_ptr<ResolvedGetProtoField> proto_expr,
    const google::protobuf::Descriptor& proto_descriptor, TypeFactory& type_factory) {
  const google::protobuf::FieldDescriptor* int_value =
      proto_descriptor.FindFieldByName(field_name);

  GOOGLESQL_RET_CHECK(int_value != nullptr);

  return MakeResolvedGetProtoField(
      type_factory.get_int64(), std::move(proto_expr), int_value,
      /* default_value=*/Value::NullInt64(),
      /* get_has_bit=*/false, ProtoType::GetFormatAnnotation(int_value),
      /* return_default_value_when_unset=*/false);
}

template <class NodeType>
struct DPNodeSpecificData;

template <>
struct DPNodeSpecificData<ResolvedAnonymizedAggregateScan> {
  static bool IsMaxGroupsContributedOption(absl::string_view argument_name) {
    return googlesql_base::CaseEqual(argument_name, "kappa") ||
           googlesql_base::CaseEqual(argument_name, "max_groups_contributed");
  }

  static bool IsThresholdingFeatureEnabled(Resolver& resolver) {
    return resolver.language().LanguageFeatureEnabled(
        FEATURE_ANONYMIZATION_THRESHOLDING);
  }

  static const google::protobuf::Descriptor* GetOutputValueProtoDescriptor() {
    return AnonOutputValue::GetDescriptor();
  }

  static constexpr absl::string_view kGroupSelectionErrorPrefix =
      "Anonymization option group_selection_strategy";
  static constexpr absl::string_view kDefaultMaxGroupsContributedOptionName =
      "max_groups_contributed";
  static constexpr absl::string_view kMaxGroupsContributedErrorPrefix =
      "Anonymization option MAX_GROUPS_CONTRIBUTED (aka KAPPA)";
  static constexpr WithModifierModeName kWithModifierModeName = {
      .name = WithModifierModeToString(WithModifierMode::ANONYMIZATION),
      .uses_a_article = false};
};

template <>
struct DPNodeSpecificData<ResolvedDifferentialPrivacyAggregateScan> {
  static bool IsMaxGroupsContributedOption(absl::string_view argument_name) {
    return googlesql_base::CaseEqual(argument_name, "max_groups_contributed");
  }

  static bool IsThresholdingFeatureEnabled(Resolver& resolver) {
    return resolver.language().LanguageFeatureEnabled(
        FEATURE_DIFFERENTIAL_PRIVACY_THRESHOLDING);
  }

  static const google::protobuf::Descriptor* GetOutputValueProtoDescriptor() {
    return functions::DifferentialPrivacyOutputValue::GetDescriptor();
  }

  static constexpr absl::string_view kGroupSelectionErrorPrefix =
      "Differential privacy option group_selection_strategy";
  static constexpr absl::string_view kDefaultMaxGroupsContributedOptionName =
      "max_groups_contributed";
  static constexpr absl::string_view kMaxGroupsContributedErrorPrefix =
      "Option MAX_GROUPS_CONTRIBUTED";
  static constexpr WithModifierModeName kWithModifierModeName = {
      .name = WithModifierModeToString(WithModifierMode::DIFFERENTIAL_PRIVACY),
      .uses_a_article = true};
};

// Provided unique_users_count_column is column with type Proto
// (AnonOutputWithReport|DifferentialPrivacyOutputWithReport). Since it's
// counting unique users we want to replace group selection threshold with the
// count from the result of this function which is in
// AnonOutputWithReport|DifferentialPrivacyOutputWithReport -> value ->
// int_value. This method returns an expression extracting the value from the
// proto via ResolvedGetProtoField.
template <class NodeType>
static absl::StatusOr<std::unique_ptr<ResolvedExpr>>
MakeExtractCountFromAnonOutputWithReportProto(
    const ResolvedColumn& unique_users_count_column,
    TypeFactory& type_factory) {
  // Check that provided column has proto type for
  // AnonOutputWithReport|DifferentialPrivacyOutputWithReport
  const Type* unique_users_count_column_type = unique_users_count_column.type();
  GOOGLESQL_RET_CHECK_EQ(unique_users_count_column_type->kind(), TYPE_PROTO);

  const google::protobuf::Descriptor* unique_users_count_column_descriptor =
      unique_users_count_column_type->AsProto()->descriptor();

  // Extracts "value" field
  std::unique_ptr<ResolvedGetProtoField> get_value_expr;
  GOOGLESQL_ASSIGN_OR_RETURN(get_value_expr,
                   ExtractSubmessageFromProtoColumn(
                       "value", unique_users_count_column,
                       *unique_users_count_column_descriptor, type_factory));

  // Extracts "int_value" from "value"
  return ExtractIntFromProtoExpr(
      "int_value", std::move(get_value_expr),
      *DPNodeSpecificData<NodeType>::GetOutputValueProtoDescriptor(),
      type_factory);
}

// Constructors for scans don't have arguments for some fields. They must be
// attached to the node after construction.
absl::Status RewriterVisitor::AttachExtraNodeFields(
    const ResolvedScan& original, ResolvedScan& copy) {
  GOOGLESQL_RETURN_IF_ERROR(CopyHintList(&original, &copy));
  copy.set_is_ordered(original.is_ordered());
  const auto* parse_location = original.GetParseLocationRangeOrNULL();
  if (parse_location != nullptr) {
    copy.SetParseLocationRange(*parse_location);
  }
  return absl::OkStatus();
}

// Returns an expression extracting a INT64 value for k_threshold from the JSON
// column unique_users_count_column.
//
// The value of unique_users_count_column has format
// {result: {value: $count ...} ...}.
// Since the count was pre-computed, we want to replace k_threshold_expr with
// that computed value.
static absl::StatusOr<std::unique_ptr<ResolvedExpr>>
MakeExtractCountFromAnonOutputWithReportJson(
    const ResolvedColumn& unique_users_count_column, TypeFactory& type_factory,
    Catalog& catalog, AnalyzerOptions& options) {
  // Construct ResolvedExpr for int64(json_query(unique_users_count_column,
  // "$.result.value"))
  const Function* json_query_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(catalog.FindFunction({std::string("json_query")},
                                       &json_query_fn, options.find_options()));
  FunctionSignature json_query_signature(
      {type_factory.get_json(), 1},
      {{type_factory.get_json(), 1}, {type_factory.get_string(), 1}},
      FN_JSON_QUERY_JSON);
  std::vector<std::unique_ptr<const ResolvedExpr>> json_query_fn_args(2);
  json_query_fn_args[0] = BuildResolvedColumnRef(unique_users_count_column);
  json_query_fn_args[1] =
      MakeResolvedLiteral(types::StringType(), Value::String("$.result.value"),
                          /*has_explicit_type=*/true);

  const Function* json_to_int64_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(catalog.FindFunction(
      {std::string("int64")}, &json_to_int64_fn, options.find_options()));
  FunctionSignature json_to_int64_signature({type_factory.get_int64(), 1},
                                            {{type_factory.get_json(), 1}},
                                            FN_JSON_TO_INT64);
  std::vector<std::unique_ptr<const ResolvedExpr>> json_to_int64_fn_args(1);
  json_to_int64_fn_args[0] = MakeResolvedFunctionCall(
      types::JsonType(), json_query_fn, json_query_signature,
      std::move(json_query_fn_args), ResolvedFunctionCall::DEFAULT_ERROR_MODE);

  return MakeResolvedFunctionCall(types::Int64Type(), json_to_int64_fn,
                                  json_to_int64_signature,
                                  std::move(json_to_int64_fn_args),
                                  ResolvedFunctionCall::DEFAULT_ERROR_MODE);
}

const std::vector<std::unique_ptr<const ResolvedOption>>& GetOptions(
    const ResolvedAnonymizedAggregateScan* node) {
  return node->anonymization_option_list();
}

const std::vector<std::unique_ptr<const ResolvedOption>>& GetOptions(
    const ResolvedDifferentialPrivacyAggregateScan* node) {
  return node->option_list();
}

// We don't support setting privacy unit column in WITH ANONYMIZATION OPTIONS.
absl::StatusOr<std::optional<const ResolvedExpr*>> ExtractUidColumnFromOptions(
    const ResolvedAnonymizedAggregateScan* node) {
  return std::nullopt;
}

class PrivacyUnitColumnValidator : public ResolvedASTVisitor {
 public:
  absl::Status DefaultVisit(const ResolvedNode* node) override {
    return MakeSqlErrorAtNode(*node)
           << "Unsupported privacy_unit_column definition";
  }

  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    return node->ChildrenAccept(this);
  }

  absl::Status VisitResolvedGetStructField(
      const ResolvedGetStructField* node) override {
    return node->ChildrenAccept(this);
  }

  absl::Status VisitResolvedGetProtoField(
      const ResolvedGetProtoField* node) override {
    return node->ChildrenAccept(this);
  }
};

// Extracts privacy unit column from WITH DIFFERENTIAL_PRIVACY OPTIONS
// privacy_unit_column option when it is present. see:
// (broken link) for details.
absl::StatusOr<std::optional<const ResolvedExpr*>> ExtractUidColumnFromOptions(
    const ResolvedDifferentialPrivacyAggregateScan* node) {
  std::optional<const ResolvedExpr*> result;
  for (const auto& option : node->option_list()) {
    if (!googlesql_base::CaseEqual(option->name(), "privacy_unit_column")) {
      continue;
    }
    if (result.has_value()) {
      return MakeSqlErrorAtNode(*option)
             << "Option privacy_unit_column must only be set once";
    }
    PrivacyUnitColumnValidator visitor;
    GOOGLESQL_RETURN_IF_ERROR(option->value()->Accept(&visitor));
    result = option->value();
  }
  return result;
}

enum ContributionBoundingStrategy {
  BOUNDING_NONE,
  BOUNDING_MAX_GROUPS,
  BOUNDING_MAX_ROWS,
};

struct PrivacyOptionSpec {
  ContributionBoundingStrategy strategy;

  // The maximum number of groups a privacy unit can contribute to.
  //
  // Only enforced if strategy is `BOUNDING_MAX_GROUPS`. If nullopt, then a
  // default value will be used.
  std::optional<int64_t> max_groups_contributed;

  // The maximum number of rows a privacy unit can contribute to the dataset.
  //
  // Only enforced if strategy is `BOUNDING_MAX_ROWS`. If nullopt, then a
  // default value will be used.
  std::optional<int64_t> max_rows_contributed;

  // The minimal number of privacy units that must contribute to a group for
  // that group to be potentially included in the query result.
  //
  // If nullopt, no minimal number will be enforced. However, since only
  // existing groups can potentially be included in the query result, this is
  // equivalent to setting min_privacy_units_per_group to 1.
  std::optional<int64_t> min_privacy_units_per_group;

  // The group selection strategy, which defaults to Laplace thresholding.
  DifferentialPrivacyEnums::GroupSelectionStrategy group_selection_strategy =
      DifferentialPrivacyEnums::LAPLACE_THRESHOLD;

  // Factory method that extracts (and validates) the privacy options from the
  // raw options of the differential private or anonymized aggregate scan.
  template <class NodeType>
  static absl::StatusOr<PrivacyOptionSpec> FromScanOptions(
      absl::Span<const std::unique_ptr<const ResolvedOption>> scan_options,
      const LanguageOptions& language_options);
};

template <class NodeType>
absl::StatusOr<PrivacyOptionSpec> PrivacyOptionSpec::FromScanOptions(
    absl::Span<const std::unique_ptr<const ResolvedOption>> scan_options,
    const LanguageOptions& language_options) {
  std::optional<Value> max_groups_contributed;
  std::optional<Value> max_rows_contributed;
  std::optional<Value> min_privacy_units_per_group;
  std::optional<DifferentialPrivacyEnums::GroupSelectionStrategy>
      group_selection_strategy;
  for (const auto& option : scan_options) {
    if (DPNodeSpecificData<NodeType>::IsMaxGroupsContributedOption(
            option->name())) {
      // This condition was already checked in the resolver.
      GOOGLESQL_RET_CHECK(!max_groups_contributed.has_value())
          << DPNodeSpecificData<NodeType>::kMaxGroupsContributedErrorPrefix
          << " can only be set once";
      GOOGLESQL_ASSIGN_OR_RETURN(
          max_groups_contributed,
          ParseNullOrPositiveInt32Option(
              *option,
              DPNodeSpecificData<NodeType>::kMaxGroupsContributedErrorPrefix));
    } else if (googlesql_base::CaseEqual(option->name(), "max_rows_contributed")) {
      GOOGLESQL_RET_CHECK(!max_rows_contributed.has_value())
          << "max_rows_contributed can only be set once";
      GOOGLESQL_ASSIGN_OR_RETURN(max_rows_contributed,
                       ParseNullOrPositiveInt32Option(
                           *option, "Option MAX_ROWS_CONTRIBUTED"));
    } else if (googlesql_base::CaseEqual(option->name(),
                                      "min_privacy_units_per_group")) {
      // The following two conditions were already checked in the resolver.
      GOOGLESQL_RET_CHECK(language_options.LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_MIN_PRIVACY_UNITS_PER_GROUP))
          << "FEATURE DIFFERENTIAL_PRIVACY_MIN_PRIVACY_UNITS_PER_GROUP is not "
             "enabled.";
      GOOGLESQL_RET_CHECK(!min_privacy_units_per_group.has_value())
          << "min_privacy_units_per_group can only be set once";
      GOOGLESQL_ASSIGN_OR_RETURN(min_privacy_units_per_group,
                       ParseNullOrPositiveInt32Option(
                           *option, "Option MIN_PRIVACY_UNITS_PER_GROUP"));
    } else if (googlesql_base::CaseEqual(option->name(),
                                      "group_selection_strategy")) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          group_selection_strategy,
          ValidateAndParseGroupSelectionStrategyEnum(
              *option, language_options,
              DPNodeSpecificData<NodeType>::kGroupSelectionErrorPrefix));
    }
  }

  PrivacyOptionSpec privacy_option_spec;
  if (!min_privacy_units_per_group.has_value()) {
    // Option not found, return no value.
    privacy_option_spec.min_privacy_units_per_group = std::nullopt;
  } else if (min_privacy_units_per_group->is_null()) {
    // Null values are interpreted as if min_privacy_units_per_group were
    // omitted.
    privacy_option_spec.min_privacy_units_per_group = std::nullopt;
  } else {
    privacy_option_spec.min_privacy_units_per_group =
        min_privacy_units_per_group->int64_value();
  }

  // This condition was already checked in the resolver.
  GOOGLESQL_RET_CHECK(
      !(max_groups_contributed.has_value() && max_rows_contributed.has_value()))
      << "MAX_GROUPS_CONTRIBUTED and MAX_ROWS_CONTRIBUTED are mutually "
         "exclusive";
  // should_disable_bounding is true iff one of the contribution bounds is
  // explicitly set to NULL.
  bool should_disable_bounding =
      (max_groups_contributed.has_value() ? max_groups_contributed->is_null()
                                          : false) ||
      (max_rows_contributed.has_value() ? max_rows_contributed->is_null()
                                        : false);

  if (should_disable_bounding) {
    privacy_option_spec.strategy = BOUNDING_NONE;
  } else if (max_rows_contributed) {
    privacy_option_spec.strategy = BOUNDING_MAX_ROWS;
    privacy_option_spec.max_rows_contributed =
        max_rows_contributed->int64_value();
  } else if (max_groups_contributed) {
    privacy_option_spec.strategy = BOUNDING_MAX_GROUPS;
    privacy_option_spec.max_groups_contributed =
        max_groups_contributed->int64_value();
  } else {
    privacy_option_spec.strategy = BOUNDING_MAX_GROUPS;
  }

  if (group_selection_strategy.has_value()) {
    privacy_option_spec.group_selection_strategy =
        group_selection_strategy.value();
  }

  return privacy_option_spec;
}

struct AggregationThresholdOptionSpec {
  // The threshold that a group must satisfy to be included in the results.
  std::optional<int64_t> threshold;
  // TODO: Implement max_groups_contributed, max_rows_contributed.
  // The maximum number of groups a privacy unit can contribute to.
  std::optional<int64_t> max_groups_contributed;
  // The maximum number of rows a privacy unit can contribute to the dataset.
  std::optional<int64_t> max_rows_contributed;
};

inline constexpr absl::string_view kAggThreshUnimplementedOptionErrorMsg =
    "Aggregation Threshold option $0 is unimplemented";
inline constexpr absl::string_view kAggThreshDuplicateOptionErrorMsg =
    "Aggregation Threshold option $0 can only be set once";
inline constexpr absl::string_view kThresholdOptionName = "threshold";
inline constexpr absl::string_view kMaxGroupsContributedOptionName =
    "max_groups_contributed";
inline constexpr absl::string_view kMaxRowsContributedOptionName =
    "max_rows_contributed";
inline constexpr absl::string_view kPrivacyUnitColumnOptionName =
    "privacy_unit_column";

// Factory method that extracts the aggregation threshold options from the raw
// options of the aggregation threshold aggregate scan.
inline absl::StatusOr<AggregationThresholdOptionSpec> FromScanOptions(
    absl::Span<const std::unique_ptr<const ResolvedOption>> scan_options,
    const LanguageOptions& language_options) {
  std::optional<Value> threshold;
  std::optional<Value> max_groups_contributed;
  std::optional<Value> max_rows_contributed;
  for (const auto& option : scan_options) {
    if (googlesql_base::CaseEqual(option->name(), kThresholdOptionName)) {
      // This condition was already checked in the resolver.
      GOOGLESQL_RET_CHECK(!threshold.has_value()) << absl::Substitute(
          kAggThreshDuplicateOptionErrorMsg, kThresholdOptionName);
      GOOGLESQL_ASSIGN_OR_RETURN(threshold, ParseNullOrPositiveInt32Option(
                                      *option, kThresholdOptionName));
    } else if (googlesql_base::CaseEqual(option->name(),
                                      kMaxRowsContributedOptionName)) {
      // This condition was already checked in the resolver.
      GOOGLESQL_RET_CHECK(!max_rows_contributed.has_value()) << absl::Substitute(
          kAggThreshDuplicateOptionErrorMsg, kMaxRowsContributedOptionName);
      GOOGLESQL_ASSIGN_OR_RETURN(max_rows_contributed,
                       ParseNullOrPositiveInt32Option(
                           *option, kMaxRowsContributedOptionName));
    } else if (googlesql_base::CaseEqual(option->name(),
                                      kMaxGroupsContributedOptionName)) {
      // This condition was already checked in the resolver.
      GOOGLESQL_RET_CHECK(!max_groups_contributed.has_value()) << absl::Substitute(
          kAggThreshDuplicateOptionErrorMsg, kMaxGroupsContributedOptionName);
      GOOGLESQL_ASSIGN_OR_RETURN(max_groups_contributed,
                       ParseNullOrPositiveInt32Option(
                           *option, kMaxGroupsContributedOptionName));
    }
  }
  AggregationThresholdOptionSpec aggregation_threshold_option_spec;
  aggregation_threshold_option_spec.threshold =
      threshold.has_value() ? threshold->int64_value() : 0;
  aggregation_threshold_option_spec.max_groups_contributed =
      max_groups_contributed && !max_groups_contributed->is_null()
          ? max_groups_contributed->int64_value()
          : Value::Int64(0).int64_value();
  aggregation_threshold_option_spec.max_rows_contributed =
      max_rows_contributed && !max_rows_contributed->is_null()
          ? max_rows_contributed->int64_value()
          : Value::Int64(0).int64_value();
  return aggregation_threshold_option_spec;
}

// Extracts privacy unit column from WITH AGGREGATION_THRESHOLD OPTIONS
// privacy_unit_column option when it is present. see:
// (broken link) for details.
// TODO: Exact behaviour of option vs. source table UID is TBD.
absl::StatusOr<std::optional<const ResolvedExpr*>> ExtractUidColumnFromOptions(
    const ResolvedAggregationThresholdAggregateScan* node) {
  std::optional<const ResolvedExpr*> result;
  for (const auto& option : node->option_list()) {
    if (!googlesql_base::CaseEqual(option->name(), "privacy_unit_column")) {
      continue;
    }
    // This condition was already checked in the resolver.
    GOOGLESQL_RET_CHECK(!result.has_value()) << absl::Substitute(
        kAggThreshDuplicateOptionErrorMsg, kPrivacyUnitColumnOptionName);
    PrivacyUnitColumnValidator visitor;
    GOOGLESQL_RETURN_IF_ERROR(option->value()->Accept(&visitor));
    result = option->value();
  }
  return result;
}

template <class NodeType>
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
RewriterVisitor::ExtractGroupSelectionThresholdExpr(
    const ResolvedColumn& unique_users_count_column) {
  std::unique_ptr<ResolvedExpr> group_selection_threshold_expr;
  if (DPNodeSpecificData<NodeType>::IsThresholdingFeatureEnabled(*resolver_)) {
    if (unique_users_count_column.IsInitialized()) {
      switch (unique_users_count_column.type()->kind()) {
        case TYPE_PROTO: {
          GOOGLESQL_ASSIGN_OR_RETURN(
              group_selection_threshold_expr,
              MakeExtractCountFromAnonOutputWithReportProto<NodeType>(
                  unique_users_count_column, *type_factory_));
          break;
        }
        case TYPE_JSON: {
          // The feature FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS should be
          // enabled in order to be able to use JSON to INT64 function.
          if (resolver_->language().LanguageFeatureEnabled(
                  FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS)) {
            GOOGLESQL_ASSIGN_OR_RETURN(group_selection_threshold_expr,
                             MakeExtractCountFromAnonOutputWithReportJson(
                                 unique_users_count_column, *type_factory_,
                                 *catalog_, *analyzer_options_));
          }
          // If FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS isn't enabled use
          // default logic when we add additional unique users count function
          // for k_threshold_expr instead of replacement.
          break;
        }
        default:
          group_selection_threshold_expr =
              BuildResolvedColumnRef(unique_users_count_column);
      }
    }
  }
  return group_selection_threshold_expr;
}

// Take target_expr and return a ResolvedExpr equivalent to:
// IF(uid_column_ IS NULL, NULL, target_expr)
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
CreateResolvedExprFilterValuesWithNullUid(
    std::unique_ptr<ResolvedExpr> target_expr, const ResolvedColumn& uid_column,
    Resolver* resolver) {
  std::vector<std::unique_ptr<const ResolvedExpr>> uid_is_null_args;
  uid_is_null_args.emplace_back(MakeResolvedColumnRef(
      uid_column.type(), uid_column, /*is_correlated=*/false));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> uid_is_null_fn,
                   ResolveFunctionCall("$is_null", std::move(uid_is_null_args),
                                       /*named_arguments=*/{}, resolver));

  std::vector<std::unique_ptr<const ResolvedExpr>> if_args;
  if_args.emplace_back(std::move(uid_is_null_fn));
  if_args.emplace_back(MakeResolvedLiteral(target_expr->type(),
                                           Value::Null(target_expr->type())));
  if_args.emplace_back(std::move(target_expr));
  return ResolveFunctionCall("if", std::move(if_args), /*named_arguments=*/{},
                             resolver);
}

// Rewrites aggregations when the bound max rows (i.e. bound L1)
// contribution bounding strategy is used.
//
// In particular, rewrites some aggregations in terms of other primitives (e.g.
// ANON_COUNT(*) -> ANON_SUM(1))
class BoundRowsAggregateFunctionCallResolver final
    : public AggregateFunctionCallResolver {
 public:
  explicit BoundRowsAggregateFunctionCallResolver(
      Resolver* resolver, bool filter_values_with_null_uid)
      : resolver_(resolver),
        filter_values_with_null_uid_(filter_values_with_null_uid) {}

  absl::StatusOr<std::unique_ptr<ResolvedExpr>> Resolve(
      const ResolvedAggregateFunctionCall* function_call_node,
      const ResolvedColumn& containing_column,
      const std::optional<const ResolvedColumn>& side_effect_column,
      std::vector<std::unique_ptr<ResolvedExpr>> argument_list) const override {
    GOOGLESQL_ASSIGN_OR_RETURN(ReplacementFunctionCall replacement_function_call,
                     GetReplacementAggregateFunctionCall(function_call_node));
    if (filter_values_with_null_uid_) {
      std::unique_ptr<ResolvedExpr> input_expr =
          std::move(replacement_function_call.input_expr);
      GOOGLESQL_ASSIGN_OR_RETURN(
          replacement_function_call.input_expr,
          CreateResolvedExprFilterValuesWithNullUid(
              std::move(input_expr), containing_column, resolver_));
    }
    return ResolveOuterAggregateFunctionCallForAnonFunction(
        function_call_node, std::move(argument_list),
        std::move(replacement_function_call), resolver_);
  }

 private:
  // Returns a ResolvedExpr representing the SQL function
  // `IF(expr IS NULL, 0, 1)`.
  absl::StatusOr<std::unique_ptr<ResolvedExpr>> BuildIfNullThen0Else1Expr(
      std::unique_ptr<ResolvedExpr> expr) const {
    std::vector<std::unique_ptr<const ResolvedExpr>>
        is_target_null_argument_list;
    is_target_null_argument_list.emplace_back(std::move(expr));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedExpr> is_target_null_expr,
        ResolveFunctionCall("$is_null", std::move(is_target_null_argument_list),
                            /*named_arguments=*/{}, resolver_));

    std::vector<std::unique_ptr<const ResolvedExpr>> result_argument_list;
    result_argument_list.emplace_back(std::move(is_target_null_expr));
    result_argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(0)));
    result_argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(1)));
    return ResolveFunctionCall("if", std::move(result_argument_list),
                               /*named_arguments=*/{}, resolver_);
  }

  absl::StatusOr<ReplacementFunctionCall> GetReplacementAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) const {
    switch (node->signature().context_id()) {
      case FunctionSignatureId::FN_ANON_COUNT: {
        // When rewriting COUNT -> SUM, we need to preserve the behaviour that
        // COUNT is counting non-null rows. We rewrite ANON_COUNT(x) to
        // ANON_SUM(IF(x IS NULL, 0, 1))
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<ResolvedExpr> original_target,
            ResolvedASTDeepCopyVisitor::Copy(node->argument_list()[0].get()));
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> new_target,
                         BuildIfNullThen0Else1Expr(std::move(original_target)));
        return ReplacementFunctionCall{
            .name = "anon_sum",
            .input_expr = std::move(new_target),
            .named_arguments =
                std::make_optional<std::vector<NamedArgumentInfo>>({})};
      }
      case FunctionSignatureId::FN_ANON_COUNT_WITH_REPORT_PROTO:
      case FunctionSignatureId::FN_ANON_COUNT_WITH_REPORT_JSON:
        /* TODOAdd support for ANON_COUNT WITH REPORT and
         * max_rows_contributed. The reasons we can't support this currently
         * are:
         *  - In the easiest case, there'd just be a
         *   $anon_count_with_report_json primitive that we could use, but this
         *   primitive doesn't exist. Therefore, we need to rewrite to some
         *   variant of ANON_SUM.
         *  - The next thing you might try is to rewrite ANON_COUNT(x) to
         *   ANON_SUM(1). This doesn't work, because ANON_COUNT only counts
         *   non-NULL rows while ANON_SUM(1) counts all rows.
         *  - We can fix this by rewriting ANON_COUNT(x) to ANON_SUM(IF(x is
         *   NULL, 0, 1)). This works correctly, and is what we do above for
         *   ANON_COUNT, but there is a bug preventing us from using this with
         *   reports: b/290310062.
         * Therefore, we don't support ANON_COUNT + WITH REPORT +
         * max_rows_contributed for now. */
        return MakeSqlErrorAtNode(*node)
               << "Unsupported aggregation: ANON_COUNT WITH REPORT while "
                  "using max_rows_contributed";
      case FunctionSignatureId::FN_ANON_COUNT_STAR:
        return ReplacementFunctionCall{
            .name = "anon_sum",
            .input_expr = MakeResolvedLiteral(Value::Int64(1)),
            .named_arguments =
                std::make_optional<std::vector<NamedArgumentInfo>>({})};
      case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_JSON:
        return ReplacementFunctionCall{
            .name = "$anon_sum_with_report_json",
            .input_expr = MakeResolvedLiteral(Value::Int64(1)),
            .named_arguments =
                std::make_optional<std::vector<NamedArgumentInfo>>({})};
      case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_PROTO:
        return ReplacementFunctionCall{
            .name = "$anon_sum_with_report_proto",
            .input_expr = MakeResolvedLiteral(Value::Int64(1)),
            .named_arguments =
                std::make_optional<std::vector<NamedArgumentInfo>>({})};
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_JSON:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_PROTO: {
        // TODO Implement COUNT(x) as
        // `SUM(IF(x IS NULL, 0, 1))` once the contribution_bounds_per_row
        // optional argument is added to the signature for SUM.
        return absl::UnimplementedError(
            "Unimplemented aggregation: COUNT while using "
            "max_rows_contributed");
      }
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_JSON:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_PROTO:
        // TODO Implement COUNT(*) as `SUM(1)` once the
        // contribution_bounds_per_row optional argument is added to the
        // signature for SUM.
        return absl::UnimplementedError(
            "Unimplemented aggregation: COUNT(*) while using "
            "max_rows_contributed");
      default:
        return ReplacementFunctionCall{.name = node->function()->Name()};
    }
  }

  Resolver* resolver_;

  // If true, add a IF(uid is NULL, NULL, raw_aggregation_value) expression.
  // This is used to filter out spurious contributions in the second aggregation
  // function when public groups are used, e.g., for an ANON_COUNT(*), where
  // there are two aggregation functions involved, one per uid and one for the
  // overall aggregation of the user-specified groups.
  const bool filter_values_with_null_uid_;
};

absl::StatusOr<std::unique_ptr<ResolvedScan>>
RewriterVisitor::AddCrossContributionSampleScan(
    std::unique_ptr<ResolvedScan> input_scan, int64_t max_rows_contributed,
    std::unique_ptr<const ResolvedExpr> uid_column) {
  std::vector<std::unique_ptr<const ResolvedExpr>> partition_by_list;
  partition_by_list.push_back(std::move(uid_column));
  std::vector<ResolvedColumn> column_list = input_scan->column_list();
  return MakeResolvedSampleScan(
      column_list, std::move(input_scan),
      /*method=*/"RESERVOIR",
      /*size=*/MakeResolvedLiteral(Value::Int64(max_rows_contributed)),
      ResolvedSampleScan::ROWS, /*repeatable_argument=*/nullptr,
      /*weight_column=*/nullptr, std::move(partition_by_list));
}

// Returns the column that is referenced in the ResolvedExpr. Returns an nullopt
// in case there are more than one columns or if there are none.
//
// We want to make sure that there is exactly one column involved in the expr.
absl::StatusOr<std::optional<ResolvedColumn>> FindSingleColumnInResolvedExpr(
    const ResolvedExpr* node) {
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(*node, &column_refs));
  if (column_refs.size() == 1) {
    return column_refs[0]->column();
  }
  return std::nullopt;
}

// Extracts the public group columns from the join expr. Currently, only
// equality expressions and functions are supported. Additionally, parameters to
// those functions must be singleton column refs. Despite those restrictions,
// this should already enable most of public group join use cases.
//
// TODO: Check if we can relax requirements for public groups.
absl::StatusOr<std::vector<std::pair<ResolvedColumn, ResolvedColumn>>>
ExtractPublicGroupKeyColumnsFromJoinExpr(const ResolvedExpr* node) {
  if (node->Is<ResolvedFunctionCall>()) {
    const ResolvedFunctionCall* call = node->GetAs<ResolvedFunctionCall>();
    switch (call->signature().context_id()) {
      case FN_EQUAL: {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::optional<ResolvedColumn> arg0,
            FindSingleColumnInResolvedExpr(call->argument_list(0)));
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::optional<ResolvedColumn> arg1,
            FindSingleColumnInResolvedExpr(call->argument_list(1)));
        if (arg0.has_value() && arg1.has_value()) {
          return {{{arg0.value(), arg1.value()}}};
        }
        break;
      }
      case FN_AND: {
        std::vector<std::pair<ResolvedColumn, ResolvedColumn>> result;
        for (const std::unique_ptr<const ResolvedExpr>& argument :
             call->argument_list()) {
          std::vector<std::pair<ResolvedColumn, ResolvedColumn>> equal_pairs;
          GOOGLESQL_ASSIGN_OR_RETURN(
              equal_pairs,
              ExtractPublicGroupKeyColumnsFromJoinExpr(argument.get()));
          result.insert(result.end(), equal_pairs.begin(), equal_pairs.end());
        }
        return result;
      }
    }
  }
  return std::vector<std::pair<ResolvedColumn, ResolvedColumn>>();
}

struct PublicGroupsUserDataAndPublicDataUidColumn {
  UidColumnState user_data_uid_column_state;
  UidColumnState public_data_uid_column_state;
};

// Returns the uid column state of the user data and the public data side of the
// join. Assumes that the join is actually a public groups join. Might return a
// nullopt in the case where the provided join is not a public groups join.
std::optional<PublicGroupsUserDataAndPublicDataUidColumn>
GetPublicGroupsUserDataAndPublicDataUidColumn(
    const ResolvedJoinScan* public_groups_join, const UidColumnState& left_uid,
    const UidColumnState& right_uid) {
  switch (public_groups_join->join_type()) {
    case ResolvedJoinScanEnums::LEFT:
      return PublicGroupsUserDataAndPublicDataUidColumn{
          .user_data_uid_column_state = right_uid,
          .public_data_uid_column_state = left_uid,
      };
    case ResolvedJoinScanEnums::RIGHT:
      return PublicGroupsUserDataAndPublicDataUidColumn{
          .user_data_uid_column_state = left_uid,
          .public_data_uid_column_state = right_uid,
      };
    default:
      return std::nullopt;
  }
}

absl::StatusOr<std::optional<UidColumnState>>
PublicGroupsState::MaybeRewritePublicGroupsJoinAndReturnUid(
    const UidColumnState& left_scan_uid_state,
    const UidColumnState& right_scan_uid_state,
    absl::Span<const std::unique_ptr<WithEntryRewriteState>> with_entries,
    ResolvedJoinScan* join) {
  std::optional<PublicGroupsUserDataAndPublicDataUidColumn> uid_columns =
      GetPublicGroupsUserDataAndPublicDataUidColumn(join, left_scan_uid_state,
                                                    right_scan_uid_state);
  if (!uid_columns.has_value()) {
    // Not a public groups join.
    return std::nullopt;
  }
  if (uid_columns->public_data_uid_column_state.column.IsInitialized()) {
    // Public data cannot have a uid set.
    return std::nullopt;
  }

  const ResolvedScan* public_groups_scan =
      GetPublicGroupsScanOrNull(join, with_entries);
  if (public_groups_scan == nullptr) {
    // Not a public groups join.
    return std::nullopt;
  }
  MarkPublicGroupColumnsAsVisited(public_groups_scan->column_list());
  if (!bound_contributions_across_groups_) {
    return uid_columns->user_data_uid_column_state;
  }
  // In case we bound the contribution across groups, we need to introduce a
  // second join later after the sample scan.
  GOOGLESQL_ASSIGN_OR_RETURN(
      const std::string with_query_name,
      RecordPublicGroupsWithScan(public_groups_scan, join->join_expr()));

  // Track equivalent columns from the join_expr, so that we can later match the
  // partial aggregate columns to the columns in the join_expr when adding the
  // second join.
  std::vector<std::pair<ResolvedColumn, ResolvedColumn>>
      equal_columns_from_join_expr;
  GOOGLESQL_ASSIGN_OR_RETURN(equal_columns_from_join_expr,
                   ExtractPublicGroupKeyColumnsFromJoinExpr(join->join_expr()));
  for (const auto& [col1, col2] : equal_columns_from_join_expr) {
    column_id_map_.try_emplace(col1.column_id(), col1);
    column_id_map_.try_emplace(col2.column_id(), col2);
    column_to_join_column_id_.insert_or_assign(col1.column_id(),
                                               col2.column_id());
    column_to_join_column_id_.insert_or_assign(col2.column_id(),
                                               col1.column_id());
  }

  // The user specified outer join will be replaced with an inner join with a
  // CTE ref to the public groups table. We will add the requested outer join
  // back after the sample scan.
  //
  // We need to use an inner join as this would otherwise create new rows, which
  // make contribution bounding harder.
  std::unique_ptr<ResolvedWithRefScan> ref_scan = MakeResolvedWithRefScan(
      public_groups_scan->column_list(), with_query_name);
  if (join->join_type() == ResolvedJoinScanEnums::RIGHT) {
    join->set_right_scan(std::move(ref_scan));
  } else {
    join->set_left_scan(std::move(ref_scan));
  }
  join->set_join_type(ResolvedJoinScan::INNER);
  return uid_columns->user_data_uid_column_state;
}

bool IsColumnListEqualToGroupByList(const ResolvedAggregateScanBase* node) {
  const absl::flat_hash_set<ResolvedColumn> set_from_column_list(
      node->column_list().begin(), node->column_list().end());
  absl::flat_hash_set<ResolvedColumn> set_from_group_by_list;
  set_from_group_by_list.reserve(node->group_by_list_size());
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       node->group_by_list()) {
    set_from_group_by_list.insert(computed_column->column());
  }
  return set_from_column_list == set_from_group_by_list;
}

// Returns whether the resolved scan is either a SELECT DISTINCT subquery or an
// equivalent subquery, e.g., SELECT x FROM y GROUP BY x.
bool IsSelectDistinctSubquery(const ResolvedScan* node) {
  switch (node->node_kind()) {
    case RESOLVED_PROJECT_SCAN: {
      // A (SELECT x ... GROUP BY x) subquery introduces a project scan that
      // does not do anything, i.e., the expr_list is empty.  This is okay and
      // we should support such subqueries that are equivalent to SELECT
      // DISTINCT subqueries.
      const ResolvedProjectScan* project_scan =
          node->GetAs<ResolvedProjectScan>();
      if (project_scan->expr_list_size() != 0) {
        return false;
      }
      return IsSelectDistinctSubquery(project_scan->input_scan());
    }
    case RESOLVED_LIMIT_OFFSET_SCAN:
      // Supporting (SELECT DISTINCT ... LIMIT x) queries. There is no privacy
      // impact, so we support this for completeness. Such queries would not
      // make much sense in production queries, but can be used for testing.
      return IsSelectDistinctSubquery(
          node->GetAs<ResolvedLimitOffsetScan>()->input_scan());
    case RESOLVED_SAMPLE_SCAN:
      // Supporting (SELECT DISTINCT ...) TABLESAMPLE .. (x percent) queries.
      // There is no privacy impact, so we support this for completeness. Such
      // queries would not make much sense in production queries, but can be
      // used for testing.
      return IsSelectDistinctSubquery(
          node->GetAs<ResolvedSampleScan>()->input_scan());
    case RESOLVED_AGGREGATE_SCAN:
    case RESOLVED_DIFFERENTIAL_PRIVACY_AGGREGATE_SCAN: {
      // This is the common path for a SELECT DISTINCT subquery.
      const auto* aggregate_scan = node->GetAs<ResolvedAggregateScanBase>();
      if (!aggregate_scan->rollup_column_list().empty()) {
        return false;
      }
      return IsColumnListEqualToGroupByList(aggregate_scan);
    }
    default:
      return false;
  }
}

const ResolvedScan* TryResolveCTESubqueryOrReturnSame(
    const ResolvedScan* node,
    absl::Span<const std::unique_ptr<WithEntryRewriteState>> with_entries) {
  if (!node->Is<ResolvedWithRefScan>()) {
    return node;
  }
  const std::string& with_query_name =
      node->GetAs<ResolvedWithRefScan>()->with_query_name();
  auto with_entry = absl::c_find_if(
      with_entries,
      [&with_query_name](
          const std::unique_ptr<WithEntryRewriteState>& rewrite_state) {
        return googlesql_base::CaseEqual(
            rewrite_state->original_entry.with_query_name(), with_query_name);
      });
  if (with_entry == with_entries.end()) {
    // No CTE with this name found.
    return node;
  }
  return (*with_entry)->original_entry.with_subquery();
}

const ResolvedScan* PublicGroupsState::GetPublicGroupsScanOrNull(
    const ResolvedJoinScan* node,
    absl::Span<const std::unique_ptr<WithEntryRewriteState>> with_entries)
    const {
  const ResolvedScan* public_group_scan_candidate;
  switch (node->join_type()) {
    case ResolvedJoinScanEnums::RIGHT:
      public_group_scan_candidate = node->right_scan();
      break;
    case ResolvedJoinScanEnums::LEFT:
      public_group_scan_candidate = node->left_scan();
      break;
    default:
      // Ignore other join types.
      return nullptr;
  }
  // In case the candidate is a CTE, we need to check if the CTE is a SELECT
  // DISTINCT subquery. In case the candidate is not a CTE, the candidate must
  // be a SELECT DISTINCT subquery itself.
  if (!IsSelectDistinctSubquery(TryResolveCTESubqueryOrReturnSame(
          public_group_scan_candidate, with_entries))) {
    return nullptr;
  }
  // Check if the column list is a subset of the public groups column list.
  for (const ResolvedColumn& column :
       public_group_scan_candidate->column_list()) {
    if (!public_group_column_ids_.contains(column.column_id())) {
      return nullptr;
    }
  }
  return public_group_scan_candidate;
}

absl::StatusOr<std::string> PublicGroupsState::RecordPublicGroupsWithScan(
    const ResolvedScan* public_groups_scan, const ResolvedExpr* join_expr) {
  ResolvedASTDeepCopyVisitor public_groups_scan_copier;
  GOOGLESQL_RETURN_IF_ERROR(public_groups_scan->Accept(&public_groups_scan_copier));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> owned_public_groups_scan,
                   public_groups_scan_copier.ConsumeRootNode<ResolvedScan>());

  class ColumnExtractingDeepCopyVisitor : public ResolvedASTDeepCopyVisitor {
   public:
    explicit ColumnExtractingDeepCopyVisitor(
        absl::flat_hash_set<ResolvedColumn>* columns)
        : columns_(columns) {}

   private:
    absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
        const ResolvedColumn& column) override {
      columns_->insert(column);
      return column;
    }

    absl::flat_hash_set<ResolvedColumn>* columns_;  // unowned
  };

  // Insert columns in the join_expr in the column_to_join_column_, so that they
  // will be tracked throughout the query.
  absl::flat_hash_set<ResolvedColumn> join_expr_columns;
  ColumnExtractingDeepCopyVisitor join_expr_copier(&join_expr_columns);
  GOOGLESQL_RETURN_IF_ERROR(join_expr->Accept(&join_expr_copier));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> owned_join_expr,
                   join_expr_copier.ConsumeRootNode<ResolvedExpr>());

  for (const ResolvedColumn& column : join_expr_columns) {
    const int column_id = column.column_id();
    column_id_map_.try_emplace(column_id, column);
    column_to_join_column_id_.insert_or_assign(column_id, column_id);
  }

  std::string with_query_name;
  bool with_scan_already_present;
  if (public_groups_scan->Is<ResolvedWithRefScan>()) {
    with_query_name =
        public_groups_scan->GetAs<ResolvedWithRefScan>()->with_query_name();
    with_scan_already_present = true;
  } else {
    with_query_name = global_public_groups_query_name_provider_
                          ->GetNextPublicGroupsWithQueryName();
    with_scan_already_present = false;
  }

  with_scans_.push_back({
      .with_query_name = with_query_name,
      .public_groups_scan = std::move(owned_public_groups_scan),
      .join_expr = std::move(owned_join_expr),
      .with_scan_already_present = with_scan_already_present,
  });
  return with_query_name;
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
PublicGroupsState::CreateJoinScanAfterSampleScan(
    std::unique_ptr<ResolvedScan> sample_scan, ColumnFactory& column_factory) {
  class ColumnRefTrackingVisitor : public ResolvedASTVisitor {
   public:
    explicit ColumnRefTrackingVisitor(
        absl::flat_hash_map<ResolvedColumn, ResolvedColumn>*
            column_to_resolved_column)
        : column_to_resolved_column_(column_to_resolved_column) {}

   private:
    absl::Status VisitResolvedComputedColumn(
        const ResolvedComputedColumn* node) override {
      if (!node->expr()->Is<ResolvedColumnRef>()) {
        return DefaultVisit(node);
      }
      column_to_resolved_column_->insert_or_assign(
          node->column(), node->expr()->GetAs<ResolvedColumnRef>()->column());
      return absl::OkStatus();
    }

    absl::flat_hash_map<ResolvedColumn, ResolvedColumn>*
        column_to_resolved_column_;  // unowned
  };

  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> column_to_resolved_column;
  ColumnRefTrackingVisitor column_tracking_visitor(&column_to_resolved_column);
  GOOGLESQL_RETURN_IF_ERROR(sample_scan->Accept(&column_tracking_visitor));
  for (const auto& [new_column, old_column] : column_to_resolved_column) {
    TrackColumnReplacement(old_column, new_column);
  }

  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> column_replacement;
  for (const ResolvedColumn& column : sample_scan->column_list()) {
    const int column_id = column.column_id();
    if (column_to_join_column_id_.contains(column_id)) {
      const int original_column_id = column_to_join_column_id_.at(column_id);
      column_replacement.insert_or_assign(column_id_map_.at(original_column_id),
                                          column_id_map_.at(column_id));
    }
  }
  for (const PublicGroupsWithScan& with_scan : with_scans_) {
    // Create new column list for this WithRefScan.
    std::vector<ResolvedColumn> new_column_list;
    for (const ResolvedColumn& column :
         with_scan.public_groups_scan->column_list()) {
      ResolvedColumn new_column = column_factory.MakeCol(
          with_scan.with_query_name, column.name(), column.type());
      original_join_column_to_added_join_column_.insert_or_assign(column,
                                                                  new_column);
      new_column_list.push_back(new_column);
      if (column_replacement.contains(column)) {
        const ResolvedColumn original_column = column_replacement.at(column);
        column_replacement.insert_or_assign(original_column, new_column);
      } else {
        column_replacement.insert_or_assign(column, new_column);
      }
    }

    std::unique_ptr<ResolvedWithRefScan> ref_scan =
        MakeResolvedWithRefScan(new_column_list, with_scan.with_query_name);

    ColumnReplacingDeepCopyVisitor join_expr_copier(&column_replacement);
    GOOGLESQL_RETURN_IF_ERROR(with_scan.join_expr->Accept(&join_expr_copier));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> owned_join_expr,
                     join_expr_copier.ConsumeRootNode<ResolvedExpr>());

    // Add existing columns of the sample scan.
    for (const ResolvedColumn& column : sample_scan->column_list()) {
      new_column_list.push_back(column);
    }

    GOOGLESQL_ASSIGN_OR_RETURN(sample_scan,
                     ResolvedJoinScanBuilder()
                         .set_column_list(new_column_list)
                         .set_join_type(ResolvedJoinScanEnums::RIGHT)
                         .set_left_scan(std::move(sample_scan))
                         .set_right_scan(std::move(ref_scan))
                         .set_join_expr(std::move(owned_join_expr))
                         .BuildMutable());
  }
  return sample_scan;
}

void PublicGroupsState::TrackColumnReplacement(
    const ResolvedColumn& old_column, const ResolvedColumn& new_column) {
  const int old_column_id = old_column.column_id();
  if (column_to_join_column_id_.contains(old_column_id)) {
    const int target_id = column_to_join_column_id_.at(old_column_id);
    column_id_map_.emplace(new_column.column_id(), new_column);
    column_to_join_column_id_.insert_or_assign(new_column.column_id(),
                                               target_id);
  }
}

absl::StatusOr<std::vector<std::unique_ptr<ResolvedWithEntry>>>
PublicGroupsState::GetPublicGroupScansAsWithEntries(
    ColumnFactory* column_factory) const {
  std::vector<std::unique_ptr<ResolvedWithEntry>> with_entries;
  for (const PublicGroupsWithScan& with_scan : with_scans_) {
    if (with_scan.with_scan_already_present) {
      continue;
    }
    ColumnReplacementMap unused_column_map;
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedScan> owned_public_groups_scan,
        CopyResolvedASTAndRemapColumns(*with_scan.public_groups_scan.get(),
                                       *column_factory, unused_column_map));
    std::unique_ptr<ResolvedWithEntry> with_entry = MakeResolvedWithEntry(
        with_scan.with_query_name, std::move(owned_public_groups_scan));
    with_entries.emplace_back(std::move(with_entry));
  }
  return with_entries;
}

absl::StatusOr<std::unique_ptr<const ResolvedScan>>
PublicGroupsState::WrapScanWithPublicGroupsWithEntries(
    ColumnFactory* column_factory,
    std::unique_ptr<const ResolvedScan> resolved_scan) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ResolvedWithEntry>> with_entries,
                   GetPublicGroupScansAsWithEntries(column_factory));
  if (with_entries.empty()) {
    return resolved_scan;
  }
  const std::vector<ResolvedColumn> new_column_list =
      resolved_scan->column_list();
  return MakeResolvedWithScan(new_column_list, std::move(with_entries),
                              std::move(resolved_scan), /*recursive=*/false);
}

std::vector<int> SortFlatHashSet(const absl::flat_hash_set<int>& set) {
  std::vector<int> result;
  result.reserve(set.size());
  for (int element : set) {
    result.push_back(element);
  }
  std::sort(result.begin(), result.end());
  return result;
}

absl::Status PublicGroupsState::ValidateAllRequiredJoinsAdded(
    absl::string_view error_prefix) const {
  if (!unjoined_column_ids_.empty()) {
    std::vector<std::string> error_names;
    error_names.reserve(unjoined_column_ids_.size());
    // Sort by column id, so that the output is deterministic.
    for (int column_id : SortFlatHashSet(unjoined_column_ids_)) {
      error_names.push_back(column_id_map_.at(column_id).name());
    }
    return absl::InvalidArgumentError(absl::StrCat(
        error_prefix,
        " PUBLIC_GROUPS expects an outer join with a SELECT "
        "DISTINCT subquery over a non-uid table for all columns in the GROUP "
        "BY, but did not find such a join for the following columns: ",
        absl::StrJoin(error_names, ", ")));
  }
  return absl::OkStatus();
}

// Add the column ref to the public_group_columns set. Expects a ColumnRef and
// fails if node is not a ColumnRef.
absl::Status ExtractPublicGroupColumns(
    const ResolvedExpr* node, absl::string_view error_prefix,
    absl::flat_hash_set<ResolvedColumn>& public_group_columns) {
  if (!node->Is<ResolvedColumnRef>()) {
    // TODO: Add more information to the following error message,
    // e.g., the public_group_table_subquery_alias and the column_alias.
    return MakeSqlErrorAtNode(*node)
           << error_prefix
           << " PUBLIC_GROUPS only allows column names out of the public group "
              "SELECT DISTINCT subquery in the GROUP BY list.";
  }
  public_group_columns.insert(node->GetAs<ResolvedColumnRef>()->column());
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<PublicGroupsState>>
PublicGroupsState::CreateFromGroupByList(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        group_by_list,
    bool bound_contributions_across_groups, absl::string_view error_prefix,
    GlobalPublicGroupsQueryNameProvider*
        global_public_groups_query_name_provider) {
  absl::flat_hash_set<ResolvedColumn> public_group_columns;
  for (const std::unique_ptr<const ResolvedComputedColumn>& group :
       group_by_list) {
    GOOGLESQL_RETURN_IF_ERROR(ExtractPublicGroupColumns(group->expr(), error_prefix,
                                              public_group_columns));
  }
  absl::flat_hash_set<int> public_group_column_ids;
  absl::flat_hash_map<int, const ResolvedColumn> column_id_map;
  for (const ResolvedColumn& column : public_group_columns) {
    public_group_column_ids.insert(column.column_id());
    column_id_map.try_emplace(column.column_id(), column);
  }
  return absl::WrapUnique(new PublicGroupsState(
      column_id_map, public_group_column_ids, bound_contributions_across_groups,
      global_public_groups_query_name_provider));
}

template <class NodeType>
absl::StatusOr<std::unique_ptr<NodeType>>
RewriterVisitor::BoundRowsContributedToInputScan(
    const NodeType* original_input_scan,
    RewritePerUserTransformResult rewritten_per_user_transform,
    int64_t max_rows_contributed, bool filter_values_with_null_uid) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ResolvedOption>> options_list,
                   ResolvedASTDeepCopyVisitor::CopyNodeList(
                       GetOptions(original_input_scan)));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ResolvedComputedColumn>> group_by_list,
      ResolvedASTDeepCopyVisitor::CopyNodeList(
          original_input_scan->group_by_list()));

  // Rewrite aggregate columns while looking for columns that count distinct
  // user IDs.
  OuterAggregateListRewriterVisitor aggregate_rewriter_visitor(
      resolver_,
      GetResolvedColumn(rewritten_per_user_transform.inner_uid_column.get())
          .value_or(ResolvedColumn()),
      std::make_unique<BoundRowsAggregateFunctionCallResolver>(
          resolver_, filter_values_with_null_uid),
      filter_values_with_null_uid);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>> aggregate_list,
      aggregate_rewriter_visitor.RewriteAggregateColumns(original_input_scan));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> group_selection_threshold_expr,
      ExtractGroupSelectionThresholdExpr<NodeType>(
          aggregate_rewriter_visitor.GetCountDistinctPrivacyIdsColumn()));
  if (group_selection_threshold_expr == nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(group_selection_threshold_expr,
                     CreateCountDistinctPrivacyIdsColumn(original_input_scan,
                                                         aggregate_list));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedScan> rewritten_scan,
      AddCrossContributionSampleScan(
          std::move(rewritten_per_user_transform.rewritten_scan),
          max_rows_contributed,
          std::move(rewritten_per_user_transform.inner_uid_column)));

  return CreateAggregateScan(
      original_input_scan, std::move(rewritten_scan), std::move(group_by_list),
      std::move(aggregate_list), std::move(group_selection_threshold_expr),
      std::move(options_list));
}

// Return the index of the `epsilon` argument in the function signature, if the
// function supports the `epsilon` parameter. If the function does not support a
// parameter named `epsilon`, this function returns a nullopt. The returned
// index is 0-based and can be used to retrieve the expression for epsilon via
// the argument list.
std::optional<int> GetEpsilonArgumentIndex(const FunctionSignature& signature) {
  int index = 0;
  for (const FunctionArgumentType& arg : signature.arguments()) {
    if (arg.has_argument_name() &&
        googlesql_base::CaseEqual(arg.argument_name(), "epsilon")) {
      return index;
    }
    ++index;
  }
  return std::nullopt;
}

// Returns true if a given expr is a literal and its value is NULL.
bool IsNullLiteral(const ResolvedExpr& expr) {
  if (expr.node_kind() != RESOLVED_LITERAL) {
    return false;
  }
  return expr.GetAs<ResolvedLiteral>()->value().is_null();
}

// Check if the user provided a count-unique-users-function in the query and
// assign the group selection epsilon, in case the user has not specified an
// epsilon argument for this function.
//
// A user-provided count unique users function will be used for group selection.
// We therefore need to disambiguate the use of the `epsilon` argument on this
// function vs the use of the `group_selection_epsilon` option in the DP
// aggregate scan.
//
// * In case the function has no `epsilon` argument, we will use the
//   `group_selection_epsilon` from the DP scan options.
// * In case both the `epsilon` argument on the function and the
//   `group_selection_epsilon` option on the DP scan is set, we return an error.
// * Otherwise we won't modify the `epsilon` and the `epsilon` becomes the
//   de-facto `group_selection_epsilon`.
//
// Args:
// * `count_unique_users_function`: function call that has been identified as
//    user-provided count-unique-users-function.
// * `option_group_selection_epsilon`: the expr from the options list of the
//    anon aggregate scan.
// * `argument_list`: copy of the original argument list of the function that
//    might be modified to set `epsilon` to `option_group_selection_epsilon`.
absl::Status CheckAndAssignGroupSelectionEpsilonToCountUniqueUsersFunction(
    const ResolvedAggregateFunctionCall* count_unique_users_function,
    const ResolvedExpr* option_group_selection_epsilon,
    std::vector<std::unique_ptr<ResolvedExpr>>& argument_list) {
  std::optional<int> epsilon_index =
      GetEpsilonArgumentIndex(count_unique_users_function->signature());
  if (!epsilon_index.has_value() || option_group_selection_epsilon == nullptr) {
    // The function does not support an `epsilon` argument or the query did not
    // specify a `group_selection_epsilon` option.
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(epsilon_index.value() <
            count_unique_users_function->argument_list_size());
  const ResolvedExpr* function_epsilon_expr =
      count_unique_users_function->argument_list(epsilon_index.value());
  GOOGLESQL_RET_CHECK(function_epsilon_expr != nullptr);
  if (!IsNullLiteral(*function_epsilon_expr) &&
      !IsNullLiteral(*option_group_selection_epsilon)) {
    return MakeSqlErrorAtNode(*count_unique_users_function)
           << "Cannot set both option group_selection_epsilon and epsilon "
              "argument on unique privacy unit count or equivalent "
              "aggregations";
  }
  if (IsNullLiteral(*function_epsilon_expr)) {
    // Use `group_selection_epsilon` as function argument `epsilon`.
    GOOGLESQL_RET_CHECK(epsilon_index.value() < argument_list.size());
    GOOGLESQL_ASSIGN_OR_RETURN(
        argument_list[epsilon_index.value()],
        ResolvedASTDeepCopyVisitor::Copy(option_group_selection_epsilon));
  }
  return absl::OkStatus();
}

// Rewrites aggregations when the bound max groups (i.e. bound L0/Linf)
// contribution bounding strategy is used.
//
// In particular, replaces the column that is being aggregated over with the
// intermediate column that pre-aggregates contributions to each partition.
class BoundGroupsAggregateFunctionCallResolver final
    : public AggregateFunctionCallResolver {
 public:
  BoundGroupsAggregateFunctionCallResolver(
      const std::map<ResolvedColumn, ResolvedColumn>& injected_col_map,
      Resolver* resolver, ResolvedColumn uid_column,
      bool filter_values_with_null_uid,
      const ResolvedExpr* option_group_selection_epsilon)
      : injected_col_map_(injected_col_map),
        resolver_(resolver),
        uid_column_(uid_column),
        filter_values_with_null_uid_(filter_values_with_null_uid),
        option_group_selection_epsilon_(option_group_selection_epsilon) {}

  absl::StatusOr<std::unique_ptr<ResolvedExpr>> Resolve(
      const ResolvedAggregateFunctionCall* function_call_node,
      const ResolvedColumn& containing_column,
      const std::optional<const ResolvedColumn>& side_effect_column,
      std::vector<std::unique_ptr<ResolvedExpr>> argument_list) const override {
    ReplacementFunctionCall call = GetReplacementAggregateFunctionCall(
        function_call_node, containing_column, side_effect_column);
    if (filter_values_with_null_uid_) {
      std::unique_ptr<ResolvedExpr> input_expr = std::move(call.input_expr);
      GOOGLESQL_ASSIGN_OR_RETURN(call.input_expr,
                       CreateResolvedExprFilterValuesWithNullUid(
                           std::move(input_expr), uid_column_, resolver_));
    }
    if (resolver_->language().LanguageFeatureEnabled(
            FEATURE_DIFFERENTIAL_PRIVACY_PER_AGGREGATION_BUDGET) &&
        IsCountUniqueUsers(function_call_node, uid_column_.column_id())) {
      GOOGLESQL_RETURN_IF_ERROR(
          CheckAndAssignGroupSelectionEpsilonToCountUniqueUsersFunction(
              function_call_node, option_group_selection_epsilon_,
              argument_list));
    }
    return ResolveOuterAggregateFunctionCallForAnonFunction(
        function_call_node, std::move(argument_list), std::move(call),
        resolver_);
  }

 private:
  ReplacementFunctionCall GetReplacementAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node,
      const ResolvedColumn& containing_column,
      const std::optional<const ResolvedColumn>& side_effect_column) const {
    ReplacementFunctionCall replacement = {
        // Always rewrite to point at the intermediate column that
        // pre-aggregates per partition.
        .input_expr =
            BuildResolvedColumnRef(injected_col_map_.at(containing_column))};

    if (side_effect_column.has_value()) {
      std::vector<std::unique_ptr<const ResolvedExpr>> args;
      args.push_back(std::move(replacement.input_expr));
      args.push_back(
          BuildResolvedColumnRef(injected_col_map_.at(*side_effect_column)));
      auto wrapper_call =
          ResolveFunctionCall("$with_side_effects", std::move(args),
                              /*named_arguments=*/{}, resolver_);
      GOOGLESQL_DCHECK_OK(wrapper_call);
      replacement.input_expr = std::move(*wrapper_call);
    }

    switch (node->signature().context_id()) {
      case FunctionSignatureId::FN_ANON_COUNT:
      case FunctionSignatureId::FN_ANON_COUNT_STAR:
        replacement.name = "anon_sum";
        replacement.named_arguments =
            std::make_optional<std::vector<NamedArgumentInfo>>({});
        return replacement;

      case FunctionSignatureId::FN_ANON_COUNT_WITH_REPORT_JSON:
      case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_JSON:
        replacement.name = "$anon_sum_with_report_json";
        replacement.named_arguments =
            std::make_optional<std::vector<NamedArgumentInfo>>({});
        return replacement;

      case FunctionSignatureId::FN_ANON_COUNT_WITH_REPORT_PROTO:
      case FunctionSignatureId::FN_ANON_COUNT_STAR_WITH_REPORT_PROTO:
        replacement.name = "$anon_sum_with_report_proto";
        replacement.named_arguments =
            std::make_optional<std::vector<NamedArgumentInfo>>({});
        return replacement;

      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR:
        replacement.name = "$differential_privacy_sum";
        if (resolver_->language().LanguageFeatureEnabled(
                FEATURE_DIFFERENTIAL_PRIVACY_PER_AGGREGATION_BUDGET)) {
          replacement.named_arguments =
              std::make_optional<std::vector<NamedArgumentInfo>>({
                  NamedArgumentInfo(kContributionBoundsPerGroup, 1, node),
                  NamedArgumentInfo(kEpsilon, 2, node),
              });
        } else {
          replacement.named_arguments =
              std::make_optional<std::vector<NamedArgumentInfo>>({
                  NamedArgumentInfo(kContributionBoundsPerGroup, 1, node),
              });
        }
        return replacement;

      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_JSON:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_PROTO:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_JSON:
      case FunctionSignatureId::FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_PROTO:
        replacement.name = "$differential_privacy_sum";
        if (resolver_->language().LanguageFeatureEnabled(
                FEATURE_DIFFERENTIAL_PRIVACY_PER_AGGREGATION_BUDGET)) {
          replacement.named_arguments =
              std::make_optional<std::vector<NamedArgumentInfo>>({
                  NamedArgumentInfo(kReportFormat, 1, node),
                  NamedArgumentInfo(kContributionBoundsPerGroup, 2, node),
                  NamedArgumentInfo(kEpsilon, 3, node),
              });
        } else {
          replacement.named_arguments =
              std::make_optional<std::vector<NamedArgumentInfo>>({
                  NamedArgumentInfo(kReportFormat, 1, node),
                  NamedArgumentInfo(kContributionBoundsPerGroup, 2, node),
              });
        }
        return replacement;

      default:
        replacement.name = node->function()->Name();
        return replacement;
    }
  }

  std::map<ResolvedColumn, ResolvedColumn> injected_col_map_;
  Resolver* resolver_;

  const ResolvedColumn uid_column_;

  // If true, add a IF(uid is NULL, NULL, raw_aggregation_value) expression.
  // This is used to filter out spurious contributions in the second aggregation
  // function when public groups are used, e.g., for an ANON_COUNT(*), where
  // there are two aggregation functions involved, one per uid and one for the
  // overall aggregation of the user-specified groups.
  const bool filter_values_with_null_uid_;

  // Epsilon to be used for group selection as provided by the client via
  // OPTIONS.
  const ResolvedExpr* option_group_selection_epsilon_;
};

void AppendResolvedComputedColumnsToList(
    absl::Span<const std::unique_ptr<ResolvedComputedColumn>>
        resolved_computed_columns,
    std::vector<ResolvedColumn>& out_column_list) {
  for (const std::unique_ptr<ResolvedComputedColumn>& computed_column :
       resolved_computed_columns) {
    out_column_list.push_back(computed_column->column());
  }
}

void AppendResolvedComputedColumnsToList(
    absl::Span<const std::unique_ptr<ResolvedComputedColumnBase>>
        resolved_computed_columns,
    std::vector<ResolvedColumn>& out_column_list) {
  for (const std::unique_ptr<ResolvedComputedColumnBase>& computed_column :
       resolved_computed_columns) {
    out_column_list.push_back(computed_column->column());
    if (computed_column->Is<ResolvedDeferredComputedColumn>()) {
      out_column_list.push_back(
          computed_column->GetAs<ResolvedDeferredComputedColumn>()
              ->side_effect_column());
    }
  }
}

template <class NodeType>
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
RewriterVisitor::IdentifyOrAddNoisyCountDistinctPrivacyIdsColumnToAggregateList(
    const NodeType* original_input_scan,
    const OuterAggregateListRewriterVisitor& outer_rewriter_visitor,
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>&
        outer_aggregate_list) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> noisy_count_distinct_privacy_ids_column,
      ExtractGroupSelectionThresholdExpr<NodeType>(
          outer_rewriter_visitor.GetCountDistinctPrivacyIdsColumn()));
  if (noisy_count_distinct_privacy_ids_column == nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(noisy_count_distinct_privacy_ids_column,
                     CreateCountDistinctPrivacyIdsColumn(original_input_scan,
                                                         outer_aggregate_list));
  }
  return noisy_count_distinct_privacy_ids_column;
}

absl::StatusOr<std::unique_ptr<ResolvedExpr>>
RewriterVisitor::AddExactCountDistinctPrivacyIdsColumnToAggregateList(
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>&
        outer_aggregate_list) {
  // Create aggregate function call SUM(1). Since the "outer aggregation"
  // aggregates over the per privacy unit aggregates, this counts the number
  // of distinct privacy ids in each group.
  std::vector<std::unique_ptr<const ResolvedExpr>> argument_list;
  argument_list.emplace_back(MakeResolvedLiteral(Value::Int64(1)));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> sum,
                   ResolveFunctionCall(FunctionSignatureIdToName(FN_SUM_INT64),
                                       std::move(argument_list),
                                       /*named_arguments=*/{}, resolver_));

  ResolvedColumn exact_count_distinct_privacy_units_col = allocator_->MakeCol(
      "$anon", "$exact_count_distinct_privacy_units_col", sum->type());
  outer_aggregate_list.emplace_back(MakeResolvedComputedColumn(
      exact_count_distinct_privacy_units_col, std::move(sum)));
  return BuildResolvedColumnRef(outer_aggregate_list.back()
                                    ->GetAs<ResolvedComputedColumnImpl>()
                                    ->column());
}

template <typename NodeType>
absl::StatusOr<AddGroupSelectionThresholdResult>
RewriterVisitor::AddGroupSelectionThresholding(
    const NodeType* original_input_scan,
    const OuterAggregateListRewriterVisitor& outer_rewriter_visitor,
    std::optional<int64_t> min_privacy_units_per_group,
    std::vector<std::unique_ptr<ResolvedComputedColumnBase>>&
        outer_aggregate_list) {
  AddGroupSelectionThresholdResult aggregation_threshold_data;
  GOOGLESQL_ASSIGN_OR_RETURN(
      aggregation_threshold_data.noisy_count_distinct_privacy_ids_expr,
      IdentifyOrAddNoisyCountDistinctPrivacyIdsColumnToAggregateList(
          original_input_scan, outer_rewriter_visitor, outer_aggregate_list));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> noisy_count_distinct_privacy_ids_expr_copy,
      ResolvedASTDeepCopyVisitor::Copy(
          aggregation_threshold_data.noisy_count_distinct_privacy_ids_expr
              .get()));

  // Use the "pre-thresholding strategy" if max_privacy_units_per_group has a
  // value.
  if (analyzer_options_->language().LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_MIN_PRIVACY_UNITS_PER_GROUP) &&
      min_privacy_units_per_group.has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedExpr> exact_count_distinct_privacy_ids_expr,
        AddExactCountDistinctPrivacyIdsColumnToAggregateList(
            outer_aggregate_list));
    FunctionCallBuilder builder(*analyzer_options_, *catalog_, *type_factory_);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> condition,
        builder.GreaterOrEqual(
            std::move(exact_count_distinct_privacy_ids_expr),
            MakeResolvedLiteral(Value::Int64(*min_privacy_units_per_group))));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> then,
                     builder.SafeSubtract(
                         std::move(noisy_count_distinct_privacy_ids_expr_copy),
                         MakeResolvedLiteral(
                             Value::Int64(*min_privacy_units_per_group - 1))));

    // Note that the else part yields NULL. The crucial underlying assumption
    // here is that the engine will drop pratitions/groups if the
    // `group_selection_thresholding_expr` (the return value of this function)
    // is NULL. This assumed behaviour is justified by the contract that the
    // engine is supposed to act as if the we added a `HAVING
    // group_selection_thresholding_expr >= <engine-defined-threshold>` and that
    // according to the GoogleSQL documentation a NULL value in a HAVING clause
    // is interpreted as false.
    GOOGLESQL_ASSIGN_OR_RETURN(
        aggregation_threshold_data.group_selection_threshold_expr,
        builder.If(std::move(condition), std::move(then),
                   /*else*/ MakeResolvedLiteral(Value::NullInt64())));
    return aggregation_threshold_data;
  }
  aggregation_threshold_data.group_selection_threshold_expr =
      std::move(noisy_count_distinct_privacy_ids_expr_copy);
  return aggregation_threshold_data;
}

absl::StatusOr<std::optional<int64_t>> GetMaxGroupsContributedOrDefault(
    std::optional<int64_t> max_groups_contributed,
    absl::string_view default_max_groups_contributed_option_name,
    int64_t default_max_groups_contributed,
    std::vector<std::unique_ptr<ResolvedOption>>&
        resolved_anonymization_options) {
  GOOGLESQL_RET_CHECK(0 <= default_max_groups_contributed &&
            default_max_groups_contributed <
                std::numeric_limits<int32_t>::max())
      << "Default max_groups_contributed value must be an int64 between 0 and "
      << std::numeric_limits<int32_t>::max() << ", but was "
      << default_max_groups_contributed;

  if (!max_groups_contributed.has_value() &&
      default_max_groups_contributed > 0) {
    max_groups_contributed = default_max_groups_contributed;
    std::unique_ptr<ResolvedOption> max_groups_contributed_option =
        MakeResolvedOption(
            /*qualifier=*/"", default_max_groups_contributed_option_name,
            MakeResolvedLiteral(Value::Int64(*max_groups_contributed)));
    resolved_anonymization_options.push_back(
        std::move(max_groups_contributed_option));
  }
  // Note that if default_max_groups_contributed is 0, then
  // max_groups_contributed might still not have a value by this point.
  return max_groups_contributed;
}

template <class NodeType>
absl::StatusOr<BoundGroupsContributedToInputScanResult<NodeType>>
RewriterVisitor::BoundGroupsContributedToInputScan(
    std::unique_ptr<NodeType> input_scan,
    RewritePerUserTransformResult rewritten_per_user_transform,
    const PrivacyOptionSpec& privacy_option_spec,
    PublicGroupsState* public_groups_state,
    bool always_create_count_distinct_privacy_ids_column) {
  auto [rewritten_scan, inner_uid_column] =
      std::move(rewritten_per_user_transform);
  const ResolvedColumn inner_uid_resolved_column =
      GetResolvedColumn(inner_uid_column.get()).value_or(ResolvedColumn());
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto rewrite_inner_aggregation_list_result,
      RewriteInnerAggregateList(
          input_scan.get(), DPNodeSpecificData<NodeType>::kWithModifierModeName,
          std::move(inner_uid_column)));
  auto [uid_column, injected_col_map, inner_aggregate_list, inner_group_by_list,
        order_by_column] = std::move(rewrite_inner_aggregation_list_result);

  GOOGLESQL_ASSIGN_OR_RETURN(rewritten_scan,
                   ProjectOrderByForSampledAggregation(
                       std::move(rewritten_scan), order_by_column));

  std::vector<ResolvedColumn> new_column_list;
  AppendResolvedComputedColumnsToList(inner_aggregate_list, new_column_list);
  AppendResolvedComputedColumnsToList(inner_group_by_list, new_column_list);
  rewritten_scan = MakeResolvedAggregateScan(
      new_column_list, std::move(rewritten_scan),
      std::move(inner_group_by_list), std::move(inner_aggregate_list),
      /*grouping_set_list=*/{}, /*rollup_column_list=*/{});

  OuterAggregateListRewriterVisitor outer_rewriter_visitor(
      resolver_, inner_uid_resolved_column,
      std::make_unique<BoundGroupsAggregateFunctionCallResolver>(
          injected_col_map, resolver_, uid_column,
          /*filter_values_with_null_uid=*/public_groups_state != nullptr,
          GetGroupSelectionEpsilonFromOptions(input_scan.get())),
      /*filter_values_with_null_uid=*/public_groups_state != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>>
          outer_aggregate_list,
      outer_rewriter_visitor.RewriteAggregateColumns(input_scan.get()));

  // We retrieve expressions necessary to perform group selection thresholding
  // and to determine to noisy count of distinct privacy IDs.
  //
  // The logic is as follows:
  // If we are in the public groups mode, we do not need to perform
  // thresholding. Thus, in that case, we do not need to compute the
  // `group_selection_thresholding_expr`. However, we may still need to compute
  // the noisy count of distinct privacy ids (e.g. for usage by the rewrite for
  // `approx_count_distinct`).
  // Whether we need to compute the noisy count of distinct privacy ids is
  // determined by the `always_create_count_distinct_privacy_ids_column`
  // parameter.
  // So, we need to call `AddGroupSelectionThresholding` in the following cases:
  // 1. We are not in the public groups mode.
  // 2. We are in the public groups mode, but we still need to compute the noisy
  //    count of distinct privacy ids.
  // In the second case, we need to make sure that no thresholding is performed.
  // We achieve this by setting the `group_selection_threshold_expr` to
  // `nullptr` in that case.
  AddGroupSelectionThresholdResult threshold_expressions;
  if (public_groups_state == nullptr ||
      always_create_count_distinct_privacy_ids_column) {
    GOOGLESQL_ASSIGN_OR_RETURN(threshold_expressions,
                     AddGroupSelectionThresholding(
                         input_scan.get(), outer_rewriter_visitor,
                         privacy_option_spec.min_privacy_units_per_group,
                         outer_aggregate_list));
    if (public_groups_state != nullptr) {
      threshold_expressions.group_selection_threshold_expr = nullptr;
    }
  }

  // GROUP BY columns in the cross-user scan are always simple column
  // references to the intermediate columns. Any computed columns are handled
  // in the per-user scan.
  std::vector<std::unique_ptr<ResolvedComputedColumn>> outer_group_by_list;
  for (const std::unique_ptr<const ResolvedComputedColumn>& group_by :
       input_scan->group_by_list()) {
    outer_group_by_list.emplace_back(MakeResolvedComputedColumn(
        group_by->column(),
        BuildResolvedColumnRef(injected_col_map.at(group_by->column()))));
  }

  // Copy the options for the new anonymized aggregate scan.
  std::vector<std::unique_ptr<ResolvedOption>> resolved_anonymization_options;
  bool group_selection_strategy_was_set = false;
  for (const std::unique_ptr<const ResolvedOption>& option :
       GetOptions(input_scan.get())) {
    // We don't forward privacy unit column option as it will refer to invalid
    // column at this point.
    if (googlesql_base::CaseEqual(option->name(), "privacy_unit_column")) {
      continue;
    }
    if (googlesql_base::CaseEqual(option->name(), "group_selection_strategy")) {
      group_selection_strategy_was_set = true;
    }
    ResolvedASTDeepCopyVisitor deep_copy_visitor;
    GOOGLESQL_RETURN_IF_ERROR(option->Accept(&deep_copy_visitor));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedOption> option_copy,
                     deep_copy_visitor.ConsumeRootNode<ResolvedOption>());
    resolved_anonymization_options.push_back(std::move(option_copy));
  }
  // Explicitly set the group_selection_strategy in case the default is used.
  if (!group_selection_strategy_was_set &&
      analyzer_options_->language().LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_PUBLIC_GROUPS)) {
    resolved_anonymization_options.push_back(MakeResolvedOption(
        /*qualifier=*/"", "group_selection_strategy",
        MakeResolvedLiteral(Value::Enum(
            types::DifferentialPrivacyGroupSelectionStrategyEnumType(),
            privacy_option_spec.group_selection_strategy,
            /*allow_unknown_enum_values=*/false))));
  }

  if (privacy_option_spec.strategy == BOUNDING_MAX_GROUPS) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::optional<int64_t> max_groups_contributed,
        GetMaxGroupsContributedOrDefault(
            privacy_option_spec.max_groups_contributed,
            DPNodeSpecificData<
                NodeType>::kDefaultMaxGroupsContributedOptionName,
            resolver_->analyzer_options().default_anon_kappa_value(),
            resolved_anonymization_options));
    GOOGLESQL_ASSIGN_OR_RETURN(rewritten_scan,
                     AddCrossPartitionSampleScan(
                         std::move(rewritten_scan), max_groups_contributed,
                         uid_column, public_groups_state));
    if (public_groups_state != nullptr && max_groups_contributed.has_value()) {
      // Replace columns in the group-by list. Since we modified the original
      // join to be an INNER JOIN and added an additional OUTER JOIN, we need to
      // make sure that the columns now matches with the OUTER JOIN.
      outer_group_by_list.clear();
      for (const std::unique_ptr<const ResolvedComputedColumn>& group_by :
           input_scan->group_by_list()) {
        ResolvedColumn column =
            group_by->expr()->GetAs<ResolvedColumnRef>()->column();
        ResolvedColumn added_column =
            public_groups_state->FindPublicGroupColumnForAddedJoin(column)
                .value_or(column);
        outer_group_by_list.emplace_back(MakeResolvedComputedColumn(
            group_by->column(), BuildResolvedColumnRef(added_column)));
      }
    }
  }

  return {{.rewritten_scan = CreateAggregateScan(
               input_scan.get(), std::move(rewritten_scan),
               std::move(outer_group_by_list), std::move(outer_aggregate_list),
               std::move(threshold_expressions.group_selection_threshold_expr),
               std::move(resolved_anonymization_options)),
           .noisy_count_distinct_privacy_ids_expr = std::move(
               threshold_expressions.noisy_count_distinct_privacy_ids_expr)}};
}

// Combines information from the user-provided privacy options in the query and
// the default value as set via the analyzer. The default value in the analyzer
// is only used when the user did not explicitly specify the
// `max_groups_contributed` option.
bool BoundContributionsAcrossGroups(const PrivacyOptionSpec& privacy_options,
                                    const AnalyzerOptions& analyzer_options) {
  if (privacy_options.strategy != BOUNDING_MAX_GROUPS) {
    return false;
  }
  if (privacy_options.max_groups_contributed.has_value()) {
    return true;
  }
  if (analyzer_options.default_anon_kappa_value() > 0) {
    return true;
  }
  // This might throw an error later in the code.
  return false;
}

template <class NodeType>
bool HasGroupSelectionEpsilonOptionSet(const NodeType* node) {
  return absl::c_any_of(
      GetOptions(node),
      [](const std::unique_ptr<const ResolvedOption>& option) {
        return googlesql_base::CaseEqual(option->name(),
                                      kGroupSelectionOptionName);
      });
}

template <class NodeType>
absl::Status
RewriterVisitor::VisitResolvedDifferentialPrivacyAggregateScanTemplate(
    const NodeType* original_node) {
  GOOGLESQL_ASSIGN_OR_RETURN(PrivacyOptionSpec privacy_options_spec,
                   PrivacyOptionSpec::FromScanOptions<NodeType>(
                       GetOptions(original_node), resolver_->language()));

  std::unique_ptr<PublicGroupsState> public_groups_state;
  if (privacy_options_spec.group_selection_strategy ==
      DifferentialPrivacyEnums::PUBLIC_GROUPS) {
    if (privacy_options_spec.min_privacy_units_per_group.has_value()) {
      return MakeSqlErrorAtNode(*original_node)
             << "The MIN_PRIVACY_UNITS_PER_GROUP option must not be specified "
                "if GROUP_SELECTION_STRATEGY=PUBLIC_GROUPS";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        public_groups_state,
        PublicGroupsState::CreateFromGroupByList(
            original_node->group_by_list(),
            BoundContributionsAcrossGroups(privacy_options_spec,
                                           *analyzer_options_),
            DPNodeSpecificData<NodeType>::kGroupSelectionErrorPrefix,
            &global_public_groups_query_name_provider_));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::optional<const ResolvedExpr*> options_uid_column,
                   ExtractUidColumnFromOptions(original_node));

  if (HasGroupSelectionEpsilonOptionSet(original_node)) {
    if constexpr (std::is_same_v<NodeType, ResolvedAnonymizedAggregateScan>) {
      // Not supported for SELECT WITH ANONYMIZATION.
      return MakeSqlErrorAtNode(*original_node)
             << "Anonymization option group_selection_epsilon is not supported";
    }
    if (!resolver_->language().LanguageFeatureEnabled(
            FEATURE_DIFFERENTIAL_PRIVACY_PER_AGGREGATION_BUDGET)) {
      return MakeSqlErrorAtNode(*original_node)
             << "Option group_selection_epsilon is not supported";
    }
    if (public_groups_state != nullptr) {
      return MakeSqlErrorAtNode(*original_node)
             << "Option group_selection_epsilon cannot be used with "
                "group_selection_strategy = PUBLIC_GROUPS";
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      RewritePerUserTransformResult rewrite_per_user_result,
      RewritePerUserTransform(
          original_node, DPNodeSpecificData<NodeType>::kWithModifierModeName,
          options_uid_column, public_groups_state.get()));

  bool has_approx_count_distinct = false;
  // We copy the original node here, because it will allow us to modify certain
  // parts of it directly. It is possible to avoid this copy and only modify the
  // things that need to be modified. However, making a copy once here makes
  // the subsequent implementation easier. In case this causes performance
  // issues, we can revisit this decision.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeType> rewritten_dp_scan,
                   ResolvedASTDeepCopyVisitor::Copy(original_node));
  // Only implement APPROX_COUNT_DISTINCT for the SELECT WITH
  // DIFFERENTIAL_PRIVACY, but not for the deprecated SELECT WITH ANONYMIZATION
  // syntax.
  if constexpr (std::is_same_v<NodeType,
                               ResolvedDifferentialPrivacyAggregateScan>) {
    // If there is an `approx_count_distinct` aggregation, where the distinct
    // elements to count are privacy ids, then the call can be replaced by a
    // simpler call.
    // Specifically, the call `approx_count_distinct(privacy_ids,
    // max_countributions_per_group => x)` can be replaced by `sum(1,
    // contribution_bounds_per_group => (0, x))`.
    // Note `x != 1` is not a smart choice of parameters, since the contribution
    // of a privacy id is at most `1`, and choosing a value greater `1`
    // therefore only increases the noise added without any benefit. Apart from
    // being an unwise choice, however, it would be a valid configuration.
    //
    // This optimization is meaningful:
    // a) For performance reasons, since the `sum` aggregation is more
    //    efficient than the `approx_count_distinct` aggregation.
    // b) For better differential privacy bugdet-split behaviour.
    //    The `sum(1, contribution_bounds_per_group =>(0, 1))` will be
    //    recognized as a "count distinct privacy ids" aggregation. It will
    //    therefore be reused for thresholding and as the input to the
    //    `EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT` function.
    GOOGLESQL_ASSIGN_OR_RETURN(
        rewritten_dp_scan,
        differential_privacy::approx_count_distinct_utility::
            ReplaceApproxCountDistinctOfPrivacyIdCallsBySimplerAggregations(
                std::move(rewritten_dp_scan),
                rewrite_per_user_result.inner_uid_column.get(), resolver_,
                type_factory_));
    // Determine if there are any `approx_count_distinct` aggregations left.
    // This is needed because we always need to have a column that
    // counts unique privacy ids. In particular, we need to make sure that such
    // a column is created even when we use `public_groups`.
    GOOGLESQL_ASSIGN_OR_RETURN(has_approx_count_distinct,
                     differential_privacy::approx_count_distinct_utility::
                         HasApproxCountDistinctAggregation(
                             rewritten_dp_scan->aggregate_list()));
  }

  std::unique_ptr<ResolvedScan> result = nullptr;
  if (privacy_options_spec.strategy == BOUNDING_MAX_ROWS) {
    GOOGLESQL_RET_CHECK(privacy_options_spec.max_rows_contributed.has_value())
        << "There is no way to bound max rows without explicitly specifying "
           "max_rows_contributed, since the default contribution bounding "
           "strategy is BOUNDING_MAX_GROUPS.";
    if (privacy_options_spec.min_privacy_units_per_group.has_value()) {
      // TODO: Implement this as part of the implementation
      // of l1 contribution bounding.
      return absl::UnimplementedError(
          "MIN_PRIVACY_UNITS_PER_GROUP is not yet implemented for the "
          "contribution bounding strategy BOUNDING_MAX_ROWS");
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        result,
        BoundRowsContributedToInputScan(
            rewritten_dp_scan.get(), std::move(rewrite_per_user_result),
            privacy_options_spec.max_rows_contributed.value(),
            /*filter_values_with_null_uid=*/public_groups_state != nullptr));
  } else {
    // Note that `BoundGroupsContributedToInputScan` also handles the case where
    // no bounding is applied.
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto bound_groups_result,
        BoundGroupsContributedToInputScan(
            std::move(rewritten_dp_scan), std::move(rewrite_per_user_result),
            privacy_options_spec, public_groups_state.get(),
            /*always_create_count_distinct_privacy_ids_column=*/
            has_approx_count_distinct));
    auto [rewritten_scan, noisy_count_distinct_privacy_ids_expr] =
        std::move(bound_groups_result);

    GOOGLESQL_ASSIGN_OR_RETURN(result,
                     differential_privacy::approx_count_distinct_utility::
                         PerformApproxCountDistinctRewrites(
                             std::move(rewritten_scan),
                             noisy_count_distinct_privacy_ids_expr.get(),
                             has_approx_count_distinct, resolver_, allocator_,
                             analyzer_options_, catalog_, type_factory_));
  }

  if (public_groups_state != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(MaybeAttachParseLocation(
        public_groups_state->ValidateAllRequiredJoinsAdded(
            DPNodeSpecificData<NodeType>::kGroupSelectionErrorPrefix),
        *original_node));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<ResolvedWithEntry>> with_entries,
        public_groups_state->GetPublicGroupScansAsWithEntries(allocator_));
    if (!with_entries.empty()) {
      // Wrap `result` in a `WithScan` with all required `WithEntry` objects for
      // public groups.
      std::vector<ResolvedColumn> column_list = result->column_list();
      result = MakeResolvedWithScan(std::move(column_list),
                                    std::move(with_entries), std::move(result),
                                    /*recursive=*/false);
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(AttachExtraNodeFields(*original_node, *result));
  PushNodeToStack(std::move(result));
  return absl::OkStatus();
}

absl::Status ValidateAggregationThresholdOptions(
    const AggregationThresholdOptionSpec& agg_options_spec) {
  // If threshold is not set, this is unimplemented behaviour.
  if (!agg_options_spec.threshold.has_value() ||
      agg_options_spec.threshold <= 0) {
    return absl::UnimplementedError(
        "Unimplemented aggregation threshold defaults: threshold must be "
        "specified");
  }
  // Max rows and max groups contributed are unimplemented.
  if (agg_options_spec.max_rows_contributed.has_value() &&
      agg_options_spec.max_rows_contributed > 0) {
    return absl::UnimplementedError(absl::Substitute(
        kAggThreshUnimplementedOptionErrorMsg, kMaxRowsContributedOptionName));
  }
  if (agg_options_spec.max_groups_contributed.has_value() &&
      agg_options_spec.max_groups_contributed > 0) {
    return absl::UnimplementedError(
        absl::Substitute(kAggThreshUnimplementedOptionErrorMsg,
                         kMaxGroupsContributedOptionName));
  }
  // All validations passed
  return absl::OkStatus();
}

// Rewrites an aggregation threshold node to an aggregate with
// the thresholding represented as a filter on the privacy unit column.
absl::Status RewriterVisitor::VisitResolvedAggregationThresholdAggregateScan(
    const ResolvedAggregationThresholdAggregateScan* node) {
  // Validate the aggregation threshold options.
  GOOGLESQL_ASSIGN_OR_RETURN(AggregationThresholdOptionSpec agg_options_spec,
                   FromScanOptions(node->option_list(), resolver_->language()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregationThresholdOptions(agg_options_spec));
  // Handle the privacy unit id.
  GOOGLESQL_ASSIGN_OR_RETURN(std::optional<const ResolvedExpr*> options_uid_column,
                   ExtractUidColumnFromOptions(node));
  // Retrieve the column from the transform result which is either passed
  // through the options or is inferred from the source table.
  // TODO: Note this does not work if both the table has a defined
  // privacy uid and a privacy unit is specified. This behaviour is yet to be
  // determined.
  GOOGLESQL_ASSIGN_OR_RETURN(RewritePerUserTransformResult rewrite_per_user_result,
                   RewritePerUserTransform(
                       node,
                       {.name = WithModifierModeToString(
                            WithModifierMode::AGGREGATION_THRESHOLD),
                        .uses_a_article = false},
                       options_uid_column, /*public_groups_state=*/nullptr));
  auto [rewritten_scan, inner_uid_column] = std::move(rewrite_per_user_result);
  // Construct a function call to count the number of distinct privacy units.
  FunctionCallBuilder builder(*analyzer_options_, *catalog_, *type_factory_);
  std::unique_ptr<const ResolvedExpr> count_distinct_uid;
  // Call the count function with the inner uid column.
  GOOGLESQL_ASSIGN_OR_RETURN(count_distinct_uid,
                   builder.Count(std::move(inner_uid_column),
                                 /*is_distinct=*/true));
  // Create a column to hold the results of the privacy unit distinct count.
  ResolvedColumn count_column = allocator_->MakeCol(
      "$aggregate", "$privacy_unit_count", types::Int64Type());
  // Add the count column to a copy of the original column list.
  ResolvedColumnList column_list_with_count = node->column_list();
  column_list_with_count.push_back(count_column);
  // Add the count distinct aggregate to the copy of the original list.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ResolvedComputedColumnBase>> aggregate_list,
      ProcessNodeList(node->aggregate_list()));
  aggregate_list.emplace_back(
      MakeResolvedComputedColumn(count_column, std::move(count_distinct_uid)));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ResolvedOption>> options_list,
      ResolvedASTDeepCopyVisitor::CopyNodeList(node->option_list()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ResolvedComputedColumn>> group_by_list,
      ResolvedASTDeepCopyVisitor::CopyNodeList(node->group_by_list()));
  // Make a new aggregation threshold scan to add the new column and aggregate.
  auto agg_thresh_scan = MakeResolvedAggregationThresholdAggregateScan(
      column_list_with_count, std::move(rewritten_scan),
      std::move(group_by_list), std::move(aggregate_list),
      /*grouping_set_list=*/{},
      /*rollup_column_list=*/{},
      /*grouping_call_list=*/{}, std::move(options_list));
  //  Construct a HAVING function to compare the count against the threshold.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> condition,
      builder.GreaterOrEqual(
          MakeResolvedColumnRef(types::Int64Type(), count_column, false),
          MakeResolvedLiteral(Value::Int64(*agg_options_spec.threshold))));
  // Construct a filter scan using the HAVING clause.
  PushNodeToStack(MakeResolvedFilterScan(
      node->column_list(), std::move(agg_thresh_scan), std::move(condition)));
  return absl::OkStatus();
}

// The default behavior of ResolvedASTDeepCopyVisitor copies the WITH entries
// before copying the subquery. This is backwards, we need to know if a WITH
// entry is referenced inside a SELECT WITH ANONYMIZATION node to know how it
// should be copied. Instead, WithScans are rewritten as follows:
//
// 1. Collect a list of all (at this point un-rewritten) WITH entries.
// 2. Traverse and copy the WithScan subquery, providing the WITH entries list
//    to the PerUserRewriterVisitor when a SELECT WITH ANONYMIZATION node is
//    encountered.
// 3. When a ResolvedWithRefScan is encountered during the per-user rewriting
//    stage, begin rewriting the referenced WITH entry subquery. This can
//    repeat recursively for nested WITH entries.
// 4. Nested ResolvedWithScans inside of a SELECT WITH ANONYMIZATION node are
//    rewritten immediately by PerUserRewriterVisitor and recorded into the
//    WITH entries list.
// 5. Copy non-rewritten-at-this-point WITH entries, they weren't referenced
//    during the per-user rewriting stage and don't need special handling.
absl::Status RewriterVisitor::VisitResolvedWithScan(
    const ResolvedWithScan* node) {
  // Remember the offset for the with_entry_list_size() number of nodes we add
  // to the list of all WITH entries, those are the ones we need to add back
  // to with_entry_list() after rewriting.
  int64_t local_with_entries_offset = with_entries_.size();
  for (const std::unique_ptr<const ResolvedWithEntry>& entry :
       node->with_entry_list()) {
    with_entries_.emplace_back(new WithEntryRewriteState(
        {.original_entry = *entry, .rewritten_entry = nullptr}));
  }
  // Copy the subquery. This will visit and copy referenced WITH entries.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> subquery,
                   ProcessNode(node->query()));

  // Extract (and rewrite if needed) the slice of WITH entries belonging to this
  // node out of `with_entries_`.
  // This is done in reverse order to ensure that entries are rewritten in the
  // order that they are referenced.
  std::vector<std::unique_ptr<const ResolvedWithEntry>> copied_entries;
  for (int64_t i = local_with_entries_offset + node->with_entry_list_size() - 1;
       i >= local_with_entries_offset; --i) {
    WithEntryRewriteState& entry = *with_entries_[i];
    if (entry.rewritten_entry == nullptr) {
      // Copy unreferenced WITH entries.
      GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedWithEntry(&entry.original_entry));
      entry.rewritten_entry_owned = ConsumeTopOfStack<ResolvedWithEntry>();
      entry.rewritten_entry = entry.rewritten_entry_owned.get();
    }
    copied_entries.emplace_back(std::move(entry.rewritten_entry_owned));
  }
  GOOGLESQL_RET_CHECK_EQ(copied_entries.size(), node->with_entry_list_size());
  // Reverse the list to match the order of the original WITH entries.
  std::reverse(copied_entries.begin(), copied_entries.end());

  // Copy the with scan now that we have the subquery and WITH entry list
  // copied.
  auto copy =
      MakeResolvedWithScan(node->column_list(), std::move(copied_entries),
                           std::move(subquery), node->recursive());

  // Copy node members that aren't constructor arguments.
  GOOGLESQL_RETURN_IF_ERROR(AttachExtraNodeFields(*node, *copy));

  // Add the non-abstract node to the stack.
  PushNodeToStack(std::move(copy));
  return absl::OkStatus();
}

absl::Status RewriterVisitor::VisitResolvedProjectScan(
    const ResolvedProjectScan* node) {
  return MaybeAttachParseLocation(CopyVisitResolvedProjectScan(node), *node);
}
}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteHelper(
    const ResolvedNode& tree, AnalyzerOptions options,
    ColumnFactory& column_factory, Catalog& catalog,
    TypeFactory& type_factory) {
  options.CreateDefaultArenasIfNotSet();

  Resolver resolver(&catalog, &type_factory, &options);
  // The fresh resolver needs to be reset to initialize internal state before
  // use. We can use an empty SQL string because we aren't resolving a query,
  // we are just using the resolver to help resolve function calls from the
  // catalog.
  // Normally if errors are encountered during the function resolving process
  // the resolver also returns error locations based on the query string. We
  // don't have this issue because the calling code ensures that the resolve
  // calls do not return errors during normal use. We construct bogus
  // locations when resolving functions so that the resolver doesn't segfault
  // if an error is encountered, the bogus location information is ok because
  // these errors should only be raised during development in this file.
  resolver.Reset("");

  RewriterVisitor rewriter(&column_factory, &type_factory, &resolver, &catalog,
                           &options);
  GOOGLESQL_RETURN_IF_ERROR(tree.Accept(&rewriter));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> node,
                   rewriter.ConsumeRootNode<ResolvedNode>());
  return node;
}

}  // namespace googlesql
