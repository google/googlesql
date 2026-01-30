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

#include "googlesql/common/match_recognize/edge_tracker.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/common/match_recognize/row_edge_list.h"
#include "absl/functional/any_invocable.h"

namespace googlesql::functions::match_recognize {
EdgeTracker::EdgeTracker(const CompiledNFA* nfa, bool longest_match_mode)
    : nfa_(*nfa), longest_match_mode_(longest_match_mode) {
  Reset();
}

std::unique_ptr<const RowEdgeList> EdgeTracker::ProcessRow(
    absl::AnyInvocable<bool(const Edge&)> edge_predicate,
    bool disallow_match_start) {
  // Add a new row to the RowEdgeList, marking edges which are reachable and
  // where the predicate returns true. The set of target states for edges that
  // are marked specifies the set of edges which will be reachable on the next
  // ProcessRow() call.
  int row_number = row_edge_list_->AddRow();
  if (longest_match_mode_ && row_number != 0) {
    disallow_match_start = true;
  }
  bool possible_match_in_progress = false;
  std::vector<bool> new_states(nfa_.num_states());
  for (int state_number = 0; state_number < nfa_.num_states(); ++state_number) {
    if (next_row_input_states_[state_number] ||
        (!disallow_match_start &&
         (state_number == nfa_.start_state().value()))) {
      NFAState state(state_number);
      for (const Edge& edge : nfa_.GetEdgesFrom(state)) {
        if (!edge_predicate(edge)) {
          continue;  // Edge is not valid for this row.
        }
        row_edge_list_->MarkEdge(row_number, edge.edge_number);
        new_states[edge.to.value()] = true;

        if (edge.to != nfa_.final_state()) {
          possible_match_in_progress = true;
        }
      }
    }
  }
  next_row_input_states_ = new_states;

  if (possible_match_in_progress) {
    // Can't evaluate matches now, as an in-progress match exists that might
    // continue on through the next row.
    return nullptr;
  }
  FinalizeMarkedEdges();
  std::unique_ptr<const RowEdgeList> result = std::move(row_edge_list_);
  Reset();
  return result;
}

bool EdgeTracker::CanContinueMatch(int row_number) const {
  for (int edge_number = 0; edge_number < row_edge_list_->num_edges();
       ++edge_number) {
    if (row_edge_list_->IsMarked(row_number, edge_number)) {
      if (nfa_.GetEdge(edge_number).to != nfa_.final_state()) {
        return true;
      }
    }
  }
  return false;
}

void EdgeTracker::FinalizeMarkedEdges() {
  // We walk through each row in backwards order, keeping edges marked only
  // if the target is in `valid_target_states`. For the last row, this is a
  // singleton set containing only the final state; for prior rows, this is the
  // final state, plus the input states for all edges that remain marked for the
  // following row.
  std::vector<bool> valid_target_states(nfa_.num_states());
  valid_target_states[nfa_.final_state().value()] = true;

  for (int local_row_number = row_edge_list_->num_rows() - 1;
       local_row_number >= 0; --local_row_number) {
    bool exclude_edges_to_final_state =
        longest_match_mode_ && CanContinueMatch(local_row_number);

    std::vector<bool> prev_states(nfa_.num_states());
    prev_states[nfa_.final_state().value()] = true;
    for (int edge_number = 0; edge_number < row_edge_list_->num_edges();
         ++edge_number) {
      if (row_edge_list_->IsMarked(local_row_number, edge_number)) {
        const Edge& edge = nfa_.GetEdge(edge_number);
        bool keep_marked = valid_target_states[edge.to.value()];
        if (exclude_edges_to_final_state && (edge.to == nfa_.final_state())) {
          keep_marked = false;
        }
        if (keep_marked) {
          prev_states[edge.from.value()] = true;
        } else {
          row_edge_list_->UnmarkEdge(local_row_number, edge_number);
        }
      }
    }
    valid_target_states = std::move(prev_states);
  }
}

void EdgeTracker::Reset() {
  row_edge_list_ = std::make_unique<RowEdgeList>(&nfa_);
  next_row_input_states_.clear();
  next_row_input_states_.resize(nfa_.num_states());
}

std::string EdgeTracker::DebugString() const {
  return row_edge_list_->DebugString();
}
}  // namespace googlesql::functions::match_recognize
