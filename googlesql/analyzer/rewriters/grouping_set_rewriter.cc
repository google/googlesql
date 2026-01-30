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

#include "googlesql/analyzer/rewriters/grouping_set_rewriter.h"

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

using GroupingSetColumnRefs =
    std::vector<std::unique_ptr<const ResolvedColumnRef>>;

// Counts the number of expanded grouping set items in a single grouping set.
// `max_grouping_sets` is the maximum number of expanded grouping set items
// allowed. If the `grouping_set_base` is a ResolvedGroupingSetProduct and the
// number of expanded grouping set items exceeds `max_grouping_sets`, returns an
// InvalidArgumentError.
absl::StatusOr<int64_t> CountGroupingSetItem(
    const ResolvedGroupingSetBase& grouping_set_base,
    const int64_t max_grouping_sets) {
  if (grouping_set_base.Is<ResolvedGroupingSet>()) {
    return 1;
  }
  if (grouping_set_base.Is<ResolvedRollup>()) {
    const ResolvedRollup* rollup = grouping_set_base.GetAs<ResolvedRollup>();
    return rollup->rollup_column_list_size() + 1;
  }
  if (grouping_set_base.Is<ResolvedCube>()) {
    const ResolvedCube* cube = grouping_set_base.GetAs<ResolvedCube>();
    // This is a hard limit of the number of columns in cube, to avoid the
    // following computation overflow. The same check will be applied in the
    // CUBE expansion method too.
    int cube_size = cube->cube_column_list_size();
    if (cube_size > 31) {
      return absl::InvalidArgumentError(
          "Cube can not have more than 31 elements");
    }
    return 1ull << cube_size;
  }
  if (grouping_set_base.Is<ResolvedGroupingSetList>()) {
    int64_t grouping_set_count = 0;
    for (const std::unique_ptr<const ResolvedGroupingSetBase>&
             grouping_set_base :
         grouping_set_base.GetAs<ResolvedGroupingSetList>()->elem_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          int64_t single_item_grouping_set_count,
          CountGroupingSetItem(*grouping_set_base, max_grouping_sets));
      grouping_set_count += single_item_grouping_set_count;
    }
    return grouping_set_count;
  }
  // This is a ResolvedGroupingSetProduct node.
  GOOGLESQL_RET_CHECK(grouping_set_base.Is<ResolvedGroupingSetProduct>())
      << "Unsupported grouping set base type: "
      << grouping_set_base.DebugString();
  int64_t grouping_set_count_product = 1;
  for (const std::unique_ptr<const ResolvedGroupingSetBase>& grouping_set_base :
       grouping_set_base.GetAs<ResolvedGroupingSetProduct>()->input_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        int64_t single_item_grouping_set_count,
        CountGroupingSetItem(*grouping_set_base, max_grouping_sets));
    if (grouping_set_count_product >
        max_grouping_sets / single_item_grouping_set_count) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "At most %d grouping sets are allowed", max_grouping_sets));
    }
    grouping_set_count_product *= single_item_grouping_set_count;
  }
  return grouping_set_count_product;
}

absl::StatusOr<bool> ShouldRewriteGroupingSetListItems(
    absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
        grouping_set_list,
    const int64_t max_grouping_sets) {
  bool should_rewrite = grouping_set_list.size() > 1;
  int64_t grouping_set_total_count = 0;
  for (const std::unique_ptr<const ResolvedGroupingSetBase>& grouping_set_base :
       grouping_set_list) {
    should_rewrite |= !grouping_set_base->Is<ResolvedGroupingSet>();
    GOOGLESQL_ASSIGN_OR_RETURN(
        int64_t grouping_set_count,
        CountGroupingSetItem(*grouping_set_base, max_grouping_sets));
    grouping_set_total_count += grouping_set_count;
    if (grouping_set_total_count > max_grouping_sets) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "At most %d grouping sets are allowed, but %d were provided",
          max_grouping_sets, grouping_set_total_count));
    }
  }
  return should_rewrite;
}

// Returns whether it should rewrite the resolved ast.
// This function also checks whether the number of grouping sets and number of
// distinct columns in grouping sets exceed the limit specified in the rewriter
// options. The rewriter won't expand the rollup/cube before checking the above
// two numbers are in the range, this is to make sure the expansion won't cause
// OOO issues.
absl::StatusOr<bool> ShouldRewrite(const ResolvedAggregateScanBase* node,
                                   const GroupingSetRewriteOptions& options) {
  // If the aggregate scan has an empty grouping_set_list, it's a regular group
  // by query and won't be rewritten, skip all following checks.
  if (node->grouping_set_list().empty()) {
    return false;
  }

  // This is a grouping sets/rollup/cube query.
  if (node->grouping_set_list_size() > options.max_grouping_sets()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "At most %d grouping sets are allowed, but %d were provided",
        options.max_grouping_sets(), node->grouping_set_list_size()));
  }
  if (node->group_by_list_size() > options.max_columns_in_grouping_set()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "At most %d distinct columns are allowed in grouping "
        "sets, but %d were provided",
        options.max_columns_in_grouping_set(), node->group_by_list_size()));
  }

  return ShouldRewriteGroupingSetListItems(node->grouping_set_list(),
                                           options.max_grouping_sets());
}

// Expands ResolvedRollup to a list of ResolvedGroupingSets.
// For example, ROLLUP(a, b, c) will be expanded to grouping sets (a, b, c),
// (a, b), (a) and then (). ROLLUP((a, b), c) will be expanded to grouping sets
// (a, b, c), (a, b) and ().
absl::StatusOr<std::vector<std::unique_ptr<ResolvedGroupingSet>>> ExpandRollup(
    const ResolvedRollup* node) {
  GOOGLESQL_RET_CHECK(node != nullptr);
  std::vector<std::unique_ptr<ResolvedGroupingSet>> grouping_set_list;
  GroupingSetColumnRefs current_grouping_set;
  absl::flat_hash_set<ResolvedColumn> distinct_grouping_set_columns;

  grouping_set_list.reserve(node->rollup_column_list().size() + 1);
  // Add the empty grouping set.
  grouping_set_list.push_back(MakeResolvedGroupingSet());
  for (const std::unique_ptr<const ResolvedGroupingSetMultiColumn>&
           multi_column : node->rollup_column_list()) {
    for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
         multi_column->column_list()) {
      // Duplicated columns in grouping sets are equivalent to deduplicated
      // columns, so for simplicity we keep the deduplicated columns in the
      // grouping set. E.g. (a, a, b) is equivalent to (a, b).
      if (googlesql_base::InsertIfNotPresent(&distinct_grouping_set_columns,
                                  column_ref->column())) {
        current_grouping_set.push_back(
            MakeResolvedColumnRef(column_ref->type(), column_ref->column(),
                                  /*is_correlated=*/false));
      }
    }
    GroupingSetColumnRefs grouping_set_columns;
    grouping_set_columns.reserve(current_grouping_set.size());
    for (std::unique_ptr<const ResolvedColumnRef>& column_ref :
         current_grouping_set) {
      grouping_set_columns.push_back(
          MakeResolvedColumnRef(column_ref->type(), column_ref->column(),
                                /*is_correlated=*/false));
    }
    grouping_set_list.push_back(
        MakeResolvedGroupingSet(std::move(grouping_set_columns)));
  }
  // Order of the rows resulting from ROLLUP are not guaranteed, but engines
  // will generally want to compute aggregates from more to less granular
  // levels of subtotals, e.g. (a, b, c), (a, b), (a), and then ().
  std::reverse(grouping_set_list.begin(), grouping_set_list.end());
  return grouping_set_list;
}

// Expands ResolvedCube to ResolvedGroupingSet.
// For example, CUBE(a, b) will be expanded to grouping sets (a, b), (a), (b),
// and ().
// CUBE((a, b), c) will be expanded to grouping sets (a, b, c), (a, b), c, ()
absl::StatusOr<std::vector<std::unique_ptr<ResolvedGroupingSet>>> ExpandCube(
    const ResolvedCube* node) {
  GOOGLESQL_RET_CHECK(node != nullptr);

  int cube_size = node->cube_column_list_size();
  // This is the hard limit to avoid bitset overflow, it doesn't mean we will
  // allow this many columns in cube. There will be a numeric limit of the
  // maximum number of grouping sets each engine specifies.
  if (cube_size > 31) {
    return absl::InvalidArgumentError(
        "Cube can not have more than 31 elements");
  }
  uint32_t expanded_grouping_set_size = (1u << cube_size);

  std::vector<std::unique_ptr<ResolvedGroupingSet>> grouping_set_list;
  grouping_set_list.reserve(expanded_grouping_set_size);

  // Though expanded_grouping_set_size is an uint32_t, but the check above makes
  // sure uint32_t is smaller than or equal to 2^31, in which case,
  // expanded_grouping_set_size - 1 is still in the range of int.
  for (int i = expanded_grouping_set_size - 1; i >= 0; --i) {
    GroupingSetColumnRefs current_grouping_set;
    absl::flat_hash_set<ResolvedColumn> distinct_grouping_set_columns;
    std::bitset<32> grouping_set_bit(i);
    for (int column_index = 0; column_index < cube_size; ++column_index) {
      if (!grouping_set_bit.test(column_index)) {
        continue;
      }
      for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
           node->cube_column_list(column_index)->column_list()) {
        if (googlesql_base::InsertIfNotPresent(&distinct_grouping_set_columns,
                                    column_ref->column())) {
          current_grouping_set.push_back(
              MakeResolvedColumnRef(column_ref->type(), column_ref->column(),
                                    /*is_correlated=*/false));
        }
      }
    }
    grouping_set_list.push_back(
        MakeResolvedGroupingSet(std::move(current_grouping_set)));
  }
  return grouping_set_list;
}

// Create a grouping set from a list of column ref lists. Duplicated column refs
// will be deduplicated.
std::unique_ptr<ResolvedGroupingSet> CreateGroupingSetFromColumnRefs(
    const std::vector<const GroupingSetColumnRefs*>& column_ref_lists) {
  absl::flat_hash_set<ResolvedColumn> grouping_set_columns_set;
  GroupingSetColumnRefs grouping_set_columns;
  int64_t total_column_count = 0;
  for (const GroupingSetColumnRefs* column_ref_list : column_ref_lists) {
    total_column_count += column_ref_list->size();
  }
  grouping_set_columns.reserve(total_column_count);
  for (const GroupingSetColumnRefs* column_ref_list : column_ref_lists) {
    for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
         *column_ref_list) {
      if (!googlesql_base::InsertIfNotPresent(&grouping_set_columns_set,
                                   column_ref->column())) {
        continue;
      }
      grouping_set_columns.push_back(
          MakeResolvedColumnRef(column_ref->type(), column_ref->column(),
                                /*is_correlated=*/false));
    }
  }
  return MakeResolvedGroupingSet(std::move(grouping_set_columns));
}

// Given a list of grouping set lists, calculate the cartesian product of the
// grouping sets and create a list of grouping sets.
absl::Status CalculateGroupingSetsCatesianProduct(
    const std::vector<
        std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>>&
        grouping_set_list_multipliers,
    int64_t current_grouping_set_index,
    std::vector<const GroupingSetColumnRefs*>& current_columns,
    std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>&
        result_grouping_set_list) {
  if (current_grouping_set_index >= grouping_set_list_multipliers.size()) {
    result_grouping_set_list.push_back(
        CreateGroupingSetFromColumnRefs(current_columns));
    return absl::OkStatus();
  }

  const std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>&
      current_grouping_set_list =
          grouping_set_list_multipliers[current_grouping_set_index];
  for (const std::unique_ptr<const ResolvedGroupingSetBase>&
           current_grouping_set : current_grouping_set_list) {
    GOOGLESQL_RET_CHECK(current_grouping_set->Is<ResolvedGroupingSet>());
    current_columns.push_back(
        &current_grouping_set->GetAs<ResolvedGroupingSet>()
             ->group_by_column_list());
    GOOGLESQL_RETURN_IF_ERROR(CalculateGroupingSetsCatesianProduct(
        grouping_set_list_multipliers, current_grouping_set_index + 1,
        current_columns, result_grouping_set_list));
    current_columns.pop_back();
  }
  return absl::OkStatus();
}

// Expands single grouping set item to a list of grouping sets.
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>>
ExpandGroupingSet(
    std::unique_ptr<const ResolvedGroupingSetBase>& grouping_set) {
  std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>
      new_grouping_set_list;
  if (grouping_set->Is<ResolvedGroupingSet>()) {
    // Insert a copy of the ResolvedGroupingSet to the end of the
    // new_grouping_set_lists.
    new_grouping_set_list.push_back(std::move(grouping_set));
  } else if (grouping_set->Is<ResolvedRollup>()) {
    const ResolvedRollup* rollup = grouping_set->GetAs<ResolvedRollup>();
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ResolvedGroupingSet>>
                         expanded_grouping_set_list,
                     ExpandRollup(rollup));
    absl::c_move(expanded_grouping_set_list,
                 std::back_inserter(new_grouping_set_list));
  } else if (grouping_set->Is<ResolvedCube>()) {
    // This is a ResolvedCube node.
    const ResolvedCube* cube = grouping_set->GetAs<ResolvedCube>();
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ResolvedGroupingSet>>
                         expanded_grouping_set_list,
                     ExpandCube(cube));
    absl::c_move(expanded_grouping_set_list,
                 std::back_inserter(new_grouping_set_list));
  } else if (grouping_set->Is<ResolvedGroupingSetList>()) {
    // This is a ResolvedGroupingSetList node.
    auto builder = ToBuilder(absl::WrapUnique(
        grouping_set.release()->GetAs<ResolvedGroupingSetList>()));
    auto grouping_set_list = builder.release_elem_list();
    for (int i = 0; i < grouping_set_list.size(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>
              expanded_grouping_set_list,
          ExpandGroupingSet(grouping_set_list[i]));
      // Insert expanded grouping sets to the end of the new_grouping_set_lists.
      absl::c_move(expanded_grouping_set_list,
                   std::back_inserter(new_grouping_set_list));
    }
  } else {
    // This is a ResolvedGroupingSetProduct node.
    //
    // ResolvedGroupingSetProduct will be expanded to a list of grouping sets.
    //
    // For example,
    // ROLLUP(a, b), CUBE(c, d)
    // will be expanded to:
    // [(a, b), (a), ()] x [(c, d), (c), (d), ()]
    // = GROUPING SETS((a, b, c, d), (a, b, c), (a, b, d), (a, b), (a, c, d),
    // (a, c), (a, d), (a), (c, d), (c), (d), ())
    //
    // See (broken link) for more details.
    auto builder = ToBuilder(absl::WrapUnique(
        grouping_set.release()->GetAs<ResolvedGroupingSetProduct>()));

    std::vector<std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>>
        grouping_set_list_multipliers;
    for (std::unique_ptr<const ResolvedGroupingSetBase>& grouping_set :
         builder.release_input_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>
              expanded_grouping_set_list,
          ExpandGroupingSet(grouping_set));
      grouping_set_list_multipliers.push_back(
          std::move(expanded_grouping_set_list));
    }

    // Do Cartesian Product of the expanded grouping set lists.
    std::vector<const GroupingSetColumnRefs*> tmp_columns;
    GOOGLESQL_RETURN_IF_ERROR(CalculateGroupingSetsCatesianProduct(
        grouping_set_list_multipliers, /*current_grouping_set_index=*/0,
        tmp_columns, new_grouping_set_list));
  }
  return new_grouping_set_list;
}
}  // namespace

class GroupingSetRewriterVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit GroupingSetRewriterVisitor(
      const GroupingSetRewriteOptions& rewriter_options)
      : rewriter_options_(rewriter_options) {}

 private:
  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>>
  RewriteGroupingSetList(
      std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>
          grouping_set_list) {
    std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>
        new_grouping_set_list;
    for (int i = 0; i < grouping_set_list.size(); ++i) {
      GOOGLESQL_RET_CHECK(grouping_set_list[i]->Is<ResolvedGroupingSet>() ||
                grouping_set_list[i]->Is<ResolvedRollup>() ||
                grouping_set_list[i]->Is<ResolvedCube>() ||
                grouping_set_list[i]->Is<ResolvedGroupingSetProduct>());
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<std::unique_ptr<const ResolvedGroupingSetBase>>
              expanded_grouping_set_list,
          ExpandGroupingSet(grouping_set_list[i]));
      absl::c_move(expanded_grouping_set_list,
                   std::back_inserter(new_grouping_set_list));
    }
    return new_grouping_set_list;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateScan(
      std::unique_ptr<const ResolvedAggregateScan> node) override {
    // Only rewrite the resolved ast when rollup or cube exist.
    GOOGLESQL_ASSIGN_OR_RETURN(bool should_rewrite,
                     ShouldRewrite(node.get(), rewriter_options_));
    if (!should_rewrite) {
      return std::move(node);
    }

    ResolvedAggregateScanBuilder builder = ToBuilder(std::move(node));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto new_grouping_set_list,
        RewriteGroupingSetList(builder.release_grouping_set_list()));
    builder.set_grouping_set_list(std::move(new_grouping_set_list));
    return std::move(builder).Build();
  };

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregationThresholdAggregateScan(
      std::unique_ptr<const ResolvedAggregationThresholdAggregateScan> node)
      override {
    // Only rewrite the resolved ast when rollup or cube exist.
    GOOGLESQL_ASSIGN_OR_RETURN(bool should_rewrite,
                     ShouldRewrite(node.get(), rewriter_options_));
    if (!should_rewrite) {
      return std::move(node);
    }

    ResolvedAggregationThresholdAggregateScanBuilder builder =
        ToBuilder(std::move(node));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto new_grouping_set_list,
        RewriteGroupingSetList(builder.release_grouping_set_list()));
    builder.set_grouping_set_list(std::move(new_grouping_set_list));
    return std::move(builder).Build();
  };

  // The rewriter options.
  GroupingSetRewriteOptions rewriter_options_;
};

/**
 * The rewritter to expand ResolvedRollup and ResolvedCube to a list of
 * ResolvedGroupingSet.
 */
class GroupingSetRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    const GroupingSetRewriteOptions& rewrite_options =
        options.get_rewrite_options().grouping_set_rewrite_options();
    GroupingSetRewriterVisitor visitor(rewrite_options);
    return visitor.VisitAll(std::move(input));
  }

  std::string Name() const override { return "GroupingSetRewriter"; }
};

const Rewriter* GetGroupingSetRewriter() {
  static const auto* kRewriter = new GroupingSetRewriter();
  return kRewriter;
}

}  // namespace googlesql
