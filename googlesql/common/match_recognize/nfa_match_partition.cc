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

#include "googlesql/common/match_recognize/nfa_match_partition.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/edge_tracker.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/common/match_recognize/row_edge_list.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

std::string NFAMatchPartitionOptions::DebugString() const {
  std::string result;
  absl::StrAppend(&result,
                  "Overlapping mode: ", overlapping_mode ? "true" : "false");
  absl::StrAppend(
      &result, ", longest match mode: ", longest_match_mode ? "true" : "false");
  return result;
}

NFAMatchPartition::NFAMatchPartition(std::shared_ptr<const CompiledNFA> nfa,
                                     const NFAMatchPartitionOptions& options)
    : nfa_(std::move(nfa)), edge_tracker_(nfa_.get()), options_(options) {}

absl::StatusOr<MatchResult> NFAMatchPartition::AddRow(
    const std::vector<bool>& row_pattern_variables) {
  if (ABSL_PREDICT_FALSE(finalized_)) {
    return absl::FailedPreconditionError("AddRow() called after Finalize()");
  }
  if (ABSL_PREDICT_FALSE(row_pattern_variables.size() !=
                         nfa_->num_pattern_variables())) {
    return absl::InternalError(absl::StrCat(
        "Incorrect number of pattern variables passed to "
        "NFAMatchPartition::AddRow(): expected ",
        nfa_->num_pattern_variables(), ", got ", row_pattern_variables.size()));
  }

  std::unique_ptr<const RowEdgeList> row_edge_list =
      edge_tracker_.ProcessRow([&](const Edge& edge) {
        if (edge.is_head_anchored && !at_partition_start_) {
          return false;
        }
        if (edge.is_tail_anchored) {
          return false;
        }
        return edge.consumed == std::nullopt ||
               row_pattern_variables[edge.consumed->value()];
      });
  at_partition_start_ = false;
  if (row_edge_list == nullptr) {
    // We can't return any new matches if it is possible that a match spanning
    // the current row is not finished yet.
    return MatchResult{};
  } else {
    return ExtractMatches(*row_edge_list, /*in_finalize=*/false);
  }
}
absl::StatusOr<MatchResult> NFAMatchPartition::Finalize() {
  finalized_ = true;

  // Add a sentinel "end of input" row, whose possible edges are limited to
  // epsilon edges from one of our current states. This is needed to allow
  // ExtractMatch() to recognize matches that end at the final row of input.
  std::unique_ptr<const RowEdgeList> row_edge_list = edge_tracker_.ProcessRow(
      [](const Edge& edge) {
        // The end of the partition does not satisfy head-anchor constraints,
        // as, even on an empty partition, you can't have an empty match without
        // a row. (No need to check for a tail-anchor, as tail-anchor
        // constraints are automatically satisfied here).
        if (edge.is_head_anchored) {
          return false;
        }

        // At the end of the partition, we can't go down any edges that consume
        // pattern variables; only non-consuming edges (e.g. edges going to the
        // final state) are allowed.
        return edge.consumed == std::nullopt;
      },
      /*disallow_match_start=*/true);

  // We should always have an edge list returned, as it shouldn't be possible
  // for any match to continue past this sentinel row.
  GOOGLESQL_RET_CHECK(row_edge_list != nullptr);
  return ExtractMatches(*row_edge_list, /*in_finalize=*/true);
}

absl::StatusOr<Match> NFAMatchPartition::ComputeLongestMatchModeMatch(
    const RowEdgeList& primary_row_edge_list,
    int start_match_row_number) const {
  GOOGLESQL_RET_CHECK(options_.longest_match_mode);

  // Compute a new RowEdgeList object that only marks edges conducive with a
  // match beginning at our specific location, as opposed to
  // primary_row_edge_list, which marks any edge that could be part of any
  // match, starting anywhere.
  EdgeTracker match_specific_edge_tracker(nfa_.get(),
                                          /*longest_match_mode=*/true);
  for (int curr_row_number = start_match_row_number;
       curr_row_number < primary_row_edge_list.num_rows(); ++curr_row_number) {
    std::unique_ptr<const RowEdgeList> match_specific_row_edge_list =
        match_specific_edge_tracker.ProcessRow(
            [&primary_row_edge_list, curr_row_number](const Edge& edge) {
              return primary_row_edge_list.IsMarked(curr_row_number,
                                                    edge.edge_number);
            });
    if (match_specific_row_edge_list != nullptr) {
      return ComputeMatchImpl(*match_specific_row_edge_list, 0,
                              start_match_row_number + row_offset_);
    }
  }

  // The final row in primary_row_edge_list is a sentinel row that matches no
  // pattern variables, so it shouldn't be possible to have matches straddling
  // the end, so we should have had a RowEdgeList returned before reaching here.
  return absl::InternalError("Shouldn't get here");
}

absl::StatusOr<Match> NFAMatchPartition::ComputeMatchImpl(
    const RowEdgeList& row_edge_list, int start_row_index,
    int start_partition_row_number) const {
  Match match;
  match.match_id = next_match_id_;
  match.start_row_index = start_partition_row_number;

  NFAState state = nfa_->start_state();
  for (int local_row_number = start_row_index;
       local_row_number < row_edge_list.num_rows(); ++local_row_number) {
    const Edge* edge =
        row_edge_list.GetHighestPrecedenceMarkedEdge(local_row_number, state);
    GOOGLESQL_RET_CHECK(edge != nullptr)
        << "No edges possible from " << absl::StrCat(state)
        << " at local_row_number " << local_row_number;
    state = edge->to;
    if (state == nfa_->final_state()) {
      break;
    }
    GOOGLESQL_RET_CHECK(edge->consumed.has_value())
        << "Edges not to the final state should always consume a value";
    match.pattern_vars_by_row.push_back(edge->consumed->value());
  }
  return match;
}

absl::StatusOr<MatchResult> NFAMatchPartition::ExtractMatches(
    const RowEdgeList& row_edge_list, bool in_finalize) {
  int num_rows_to_check = row_edge_list.num_rows();
  if (in_finalize) {
    // Disallow matches at the sentinel "end of input" row (which are possible
    // only if the match is empty).
    --num_rows_to_check;
  }

  MatchResult result;
  for (int start_match_row_number = 0;
       start_match_row_number < num_rows_to_check;) {
    if (const Edge* edge = row_edge_list.GetHighestPrecedenceMarkedEdge(
            start_match_row_number, nfa_->start_state());
        edge != nullptr) {
      // We have the start of a match.
      Match& match = result.new_matches.emplace_back();
      if (options_.longest_match_mode) {
        GOOGLESQL_ASSIGN_OR_RETURN(match, ComputeLongestMatchModeMatch(
                                    row_edge_list, start_match_row_number));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(
            match, ComputeMatchImpl(row_edge_list, start_match_row_number,
                                    start_match_row_number + row_offset_));
      }
      ++next_match_id_;

      // Advance to one row before the earliest point where the next match can
      // start. Under overlapping mode, or if the current match is empty, the
      // next match can begin as soon as one row after the current match.
      // Otherwise, the earliest next match can begin is one row after the last
      // row in the current match.
      if (!match.pattern_vars_by_row.empty() && !options_.overlapping_mode) {
        start_match_row_number += match.pattern_vars_by_row.size();
      } else {
        ++start_match_row_number;
      }
    } else {
      // Row is not the start of a match; advance to the next row.
      ++start_match_row_number;
    }
  }
  row_offset_ += row_edge_list.num_rows();
  return result;
}

std::string NFAMatchPartition::DebugString() const {
  std::string result;
  absl::StrAppend(&result, nfa_->DebugString(), "\n");
  absl::StrAppend(&result, "Row offset: ", row_offset_, "\n");
  absl::StrAppend(&result, "Next match id: ", next_match_id_, "\n");
  absl::StrAppend(&result, edge_tracker_.DebugString());
  return result;
}

}  // namespace googlesql::functions::match_recognize
