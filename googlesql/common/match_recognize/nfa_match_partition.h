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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_MATCH_PARTITION_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_MATCH_PARTITION_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/edge_tracker.h"
#include "googlesql/common/match_recognize/row_edge_list.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"

namespace googlesql::functions::match_recognize {

// Options for controlling match behavior.
struct NFAMatchPartitionOptions {
  // Enables AFTER MATCH SKIP TO NEXT ROW behavior.
  bool overlapping_mode = false;

  bool longest_match_mode = false;

  // TODO: Add more fields for additional knobs, for example,
  // OMIT EMPTY MATCH, etc.

  std::string DebugString() const;
};

class NFAMatchPartition : public MatchPartition {
 public:
  explicit NFAMatchPartition(std::shared_ptr<const CompiledNFA> nfa,
                             const NFAMatchPartitionOptions& options);

  // This class is not copyable, as copying data in edge_tracker_
  // sized O(number of rows) could be expensive.
  NFAMatchPartition(const NFAMatchPartition&) = delete;
  NFAMatchPartition& operator=(const NFAMatchPartition&) = delete;

  // Adds a row to row_edge_list_, marking edges that the NFA could potentially
  // take to consume the row (depending on what follows it).
  //
  // If at least one edge is marked, we need to wait until we see more rows to
  // know if we have any matches. Otherwise, we have a row which we know is not
  // part of any match, so compute and report matches for all prior rows.
  absl::StatusOr<MatchResult> AddRow(
      const std::vector<bool>& row_pattern_variables) override;

  // Computes and reports all matches unreported so far, as no additional rows
  // are possible.
  absl::StatusOr<MatchResult> Finalize() override;

  std::string DebugString() const override;

 private:
  // Computes and returns all matches whose rows are fully contained within
  // row_edge_list_. Clears row_edge_list_ so that the returned
  // matches are not reported again.
  //
  // If `in_finalize` is true, this indicates that the last row of
  // `row_edge_list_` is a sentinel "end of input" row, and we will disallow
  // empty matches at that row.
  absl::StatusOr<MatchResult> ExtractMatches(const RowEdgeList& row_edge_list,
                                             bool in_finalize);

  absl::StatusOr<Match> ComputeLongestMatchModeMatch(
      const RowEdgeList& primary_row_edge_list,
      int start_match_row_number) const;

  absl::StatusOr<Match> ComputeMatchImpl(const RowEdgeList& row_edge_list,
                                         int start_row_index,
                                         int start_partition_row_number) const;

  std::shared_ptr<const CompiledNFA> nfa_;

  // Keeps track of which edges can lead to a match for each row.
  EdgeTracker edge_tracker_;

  // Offset into the partition of the first row in row_edge_list_.
  int row_offset_ = 0;

  // Match id of the next match to emit.
  int next_match_id_ = 1;

  // True if Finalize() has been called.
  bool finalized_ = false;

  // True if the next row to be called is the start of the partition.
  bool at_partition_start_ = true;

  const NFAMatchPartitionOptions options_;
};

}  // namespace googlesql::functions::match_recognize

#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_MATCH_PARTITION_H_
