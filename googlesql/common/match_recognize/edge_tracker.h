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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_EDGE_TRACKER_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_EDGE_TRACKER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/row_edge_list.h"
#include "absl/functional/any_invocable.h"

namespace googlesql::functions::match_recognize {
// Builds up a RowEdgeList, marking edges for each row which lead to a match.
class EdgeTracker {
 public:
  // If longest_match_mode is true, we will only mark edges that lead to the
  // longest possible match, starting at the first row. Otherwise, we will
  // mark all edges that can lead to any match, starting at any row (except rows
  // processed with `disallow_match_start` true).
  //
  // Note: Even in longest match mode, we still run the edge tracker with
  // longest-match-mode off in order to locate the starts of matches, then run
  // it again with longest-match-mode on to determine the full matching
  // information, given the start point.
  explicit EdgeTracker(const CompiledNFA* nfa, bool longest_match_mode = false);

  // Processes a row of input for matching, invoking `edge_predicate` for each
  // edge reachable at the current row, based on rows we've seen prior.
  //
  // A return value of true from edge_predicate indicates that it is ok to take
  // this edge when the row is seen (e.g. that the row satisfies a pattern
  // variable consumed by the edge).
  //
  // By default, a new match is allowed to begin at the first row only, if
  // longest_match_mode is specified, or at any row, otherwise.
  // `disallow_match_start` overrides this behavior and disallows matches
  // starting at this row, regardless of longest-match-mode.
  //
  // In the event that the rows processed so far are sufficient to identify a
  // complete set of matches, returns a RowEdgeList for all rows processed
  // since the last time a RowEdgeList was returned, otherwise, returns nullptr.
  // The returned RowEdgeList is set up so that an edge is marked if and only if
  // taking that edge while consuming the row leads to a match.
  std::unique_ptr<const RowEdgeList> ProcessRow(
      absl::AnyInvocable<bool(const Edge&)> edge_predicate,
      bool disallow_match_start = false);

  std::string DebugString() const;

 private:
  // For each row, unmarks edges that cannot lead to a match, given knowledge of
  // rows that follow it. For example, if the pattern is "A B", all rows
  // satisfying the "A" predicate will be initially have the "A" edge marked; it
  // is here that we unmark the "A" edge for "A" rows which aren't followed by a
  // "B" row.
  //
  // This function assumes that no matches can continue onward past the last
  // row, thus, in the final row, any edge leading to a non-final state is
  // unmarked.
  void FinalizeMarkedEdges();

  // Returns true if a marked edge exists at `row_number` that allows a match
  // to continue onto the next row (e.g. an edge not to the final state).
  bool CanContinueMatch(int row_number) const;

  // Clears accumulated state from prior ProcessRow() calls.
  void Reset();

  const CompiledNFA& nfa_;

  // For each row seen, marks edges which can potentially lead to a match,
  // depending on what input follows it.
  std::unique_ptr<RowEdgeList> row_edge_list_;

  // Indexed by state number. Elements are true if the state is a possible
  // source state for the next edge to be taken, excluding the start state.
  //
  // Represented as a vector, rather than a hash set, for performance reasons.
  std::vector<bool> next_row_input_states_;

  const bool longest_match_mode_;
};
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_EDGE_TRACKER_H_
