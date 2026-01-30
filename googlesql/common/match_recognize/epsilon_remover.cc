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

#include "googlesql/common/match_recognize/epsilon_remover.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

// Helper class to implement epsilon-removal logic.
class EpsilonRemover {
 public:
  explicit EpsilonRemover(const NFA& nfa) : nfa_(nfa) {
    // Initialize the epsilon-removed NFA to have the correct states, including
    // the start state and final state, but with no edges. (We'll fill in the
    // edges inside GetResult()).
    //
    // Also, populate state_map_ to map states between the original NFA and the
    // NFA we are creating.
    epsilon_removed_ = nfa.CreateEmptyNFA();
    state_map_[nfa.start_state()] = epsilon_removed_->NewState();
    epsilon_removed_->SetAsStartState(state_map_[nfa.start_state()]);

    for (int i = 0; i < nfa.num_states(); ++i) {
      NFAState from(i);
      for (const NFAEdge edge : nfa.GetEdgesFrom(from)) {
        if (edge.pattern_var.has_value()) {
          if (!state_map_.contains(edge.target)) {
            state_map_[edge.target] = epsilon_removed_->NewState();
          }
        }
      }
    }
    if (!state_map_.contains(nfa.final_state())) {
      state_map_[nfa.final_state()] = epsilon_removed_->NewState();
    }
    epsilon_removed_->SetAsFinalState(state_map_[nfa.final_state()]);
  }

  absl::StatusOr<std::unique_ptr<NFA>> GetResult() {
    for (auto [old_state, unused] : state_map_) {
      GOOGLESQL_ASSIGN_OR_RETURN(NFAState mapped_source, LookupState(old_state));

      // Compute edges arising out of `mapped_source` in the epsilon-removed
      // NFA, assuming we are in the middle of the partition (e.g. no head
      // anchored or tail anchored edges may be taken).
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<NFAEdge> mid_part_edges,
          GetEpsilonRemovedEdgesFrom(old_state, /*at_partition_start=*/false,
                                     /*at_partition_end=*/false));

      // If we are at the start state, compute edges arising out of
      // `mapped_source` in the epsilon-removed NFA, assuming that we are at
      // the start of the partition (e.g. head-anchored edges allowed,
      // tail-anchored edges not allowed).
      //
      // If we are not at the start state of the NFA, then being at the start of
      // the entire partition is not possible in the first place.
      std::optional<std::vector<NFAEdge>> start_part_edges;
      if (old_state == nfa_.start_state()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            start_part_edges,
            GetEpsilonRemovedEdgesFrom(old_state, /*at_partition_start=*/true,
                                       /*at_partition_end=*/false));

        // Every edge that is possible mid-partition should also be possible at
        // the start of the partition; allowing head anchors can change order or
        // add additional edges, but should never remove any edges.
        GOOGLESQL_RET_CHECK_GE(start_part_edges->size(), mid_part_edges.size());
      }

      // Compute edges arising out of `mapped_source` in the epsilon-removed
      // NFA, assuming we are at the end of the partition (e.g. tail anchors
      // allowed, head anchors not allowed).
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<NFAEdge> end_part_edges,
          GetEpsilonRemovedEdgesFrom(old_state, /*at_partition_start=*/false,
                                     /*at_partition_end=*/true));

      // Every edge that is possible mid-partition should also be possible at
      // the end of the partition; allowing tail anchors can only add additional
      // edges.
      GOOGLESQL_RET_CHECK_GE(end_part_edges.size(), mid_part_edges.size());

      int start_part_idx = 0;
      int mid_part_idx = 0;
      int end_part_idx = 0;
      while (mid_part_idx < mid_part_edges.size()) {
        if (start_part_edges != std::nullopt) {
          while (start_part_idx < start_part_edges->size() &&
                 mid_part_edges[mid_part_idx] !=
                     (*start_part_edges)[start_part_idx]) {
            // We have an edge that is reachable if at the start of the
            // partition, but not at the middle of the partition. Add it as
            // head-anchored.
            GOOGLESQL_RETURN_IF_ERROR(epsilon_removed_->AddEdge(
                mapped_source,
                (*start_part_edges)[start_part_idx].SetHeadAnchored()));
            ++start_part_idx;
          }
        }
        while (end_part_idx < end_part_edges.size() &&
               mid_part_edges[mid_part_idx] != end_part_edges[end_part_idx]) {
          // Tail-anchored edges that consume a row are unreachable, so we avoid
          // add them as an optimization to reduce the number of edges in the
          // graph.
          if (end_part_edges[end_part_idx].pattern_var == std::nullopt) {
            GOOGLESQL_RETURN_IF_ERROR(epsilon_removed_->AddEdge(
                mapped_source, end_part_edges[end_part_idx].SetTailAnchored()));
          }
          ++end_part_idx;
        }
        // The same edge is present in the graphs for the start, middle, and
        // end of the partition, so add it with no anchor.
        GOOGLESQL_RETURN_IF_ERROR(epsilon_removed_->AddEdge(
            mapped_source, mid_part_edges[mid_part_idx]));
        ++start_part_idx;
        ++mid_part_idx;
        ++end_part_idx;
      }

      // Any remaining edges in the start/end partition edge lists must be
      // anchored, as there is no corresponding edge in the mid-partition list.
      if (start_part_edges != std::nullopt) {
        while (start_part_idx < start_part_edges->size()) {
          GOOGLESQL_RETURN_IF_ERROR(epsilon_removed_->AddEdge(
              mapped_source,
              (*start_part_edges)[start_part_idx].SetHeadAnchored()));
          ++start_part_idx;
        }
      }
      while (end_part_idx < end_part_edges.size()) {
        // Tail-anchored edges that consume a row are unreachable, so we avoid
        // add them as an optimization to reduce the number of edges in the
        // graph.
        if (end_part_edges[end_part_idx].pattern_var == std::nullopt) {
          GOOGLESQL_RETURN_IF_ERROR(epsilon_removed_->AddEdge(
              mapped_source, end_part_edges[end_part_idx].SetTailAnchored()));
        }
        ++end_part_idx;
      }
    }

    // Sanity check that the NFA is valid and that epsilons have actually been
    // removed.
    GOOGLESQL_RETURN_IF_ERROR(
        epsilon_removed_->Validate(NFA::ValidationMode::kForMatching));
    return std::move(epsilon_removed_);
  }

 private:
  struct StackEntry {
    // State in the original NFA.
    NFAState source_state;

    // The edge in the old NFA to be processed
    const NFAEdge& edge;
  };

  // Consumes a state in `nfa_`. Returns the corresponding state in
  // `epsilon_removed_`. As it is expected that this function only be called
  // with states that map, InternalError is returned if the state does not map.
  absl::StatusOr<NFAState> LookupState(NFAState state) const {
    auto it = state_map_.find(state);
    if (it == state_map_.end()) {
      return absl::InternalError(
          absl::StrCat("State ", state, " missing from internal state_map"));
    }
    return it->second;
  }

  // Pushes all edges from `source_state` onto `stack`. Pushes the edges in
  // reverse order so that they get popped in forwards order, allowing
  // precedence ordering to be preserved across epsilon-removal.
  absl::Status PushEdges(NFAState source_state, bool at_partition_start,
                         bool at_partition_end,
                         std::vector<StackEntry>& stack) const {
    const std::vector<NFAEdge>& edges = nfa_.GetEdgesFrom(source_state);
    for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
      const NFAEdge& edge = *it;
      if ((edge.is_head_anchored && !at_partition_start) ||
          (edge.is_tail_anchored && !at_partition_end)) {
        continue;  // Edge is unreachable due to anchor constraints.
      }
      stack.push_back({.source_state = source_state, .edge = edge});
    }
    return absl::OkStatus();
  }

  // Adds all edges from `state`, in the original NFA, to the
  // epsilon-removed NFA. (It is assumed that `state` maps to the
  // epsilon-removed NFA).
  absl::StatusOr<std::vector<NFAEdge>> GetEpsilonRemovedEdgesFrom(
      NFAState source, bool at_partition_start, bool at_partition_end) const {
    std::vector<NFAEdge> epsilon_removed_edges;
    std::vector<StackEntry> stack;
    absl::flat_hash_set<NFAState> visited_states;

    GOOGLESQL_RETURN_IF_ERROR(
        PushEdges(source, at_partition_start, at_partition_end, stack));
    while (!stack.empty()) {
      StackEntry curr = stack.back();
      stack.pop_back();

      visited_states.insert(curr.source_state);

      if (curr.edge.pattern_var.has_value() ||
          curr.edge.target == nfa_.final_state()) {
        // End of the path. Create the corresponding edge in the
        // epsilon-removed NFA.
        GOOGLESQL_ASSIGN_OR_RETURN(NFAState mapped_target, LookupState(curr.edge.target));
        epsilon_removed_edges.push_back(
            NFAEdge(curr.edge.pattern_var, mapped_target));
      } else if (!visited_states.contains(curr.edge.target)) {
        // Path continues.
        GOOGLESQL_RETURN_IF_ERROR(PushEdges(curr.edge.target, at_partition_start,
                                  at_partition_end, stack));
      }
    }
    return epsilon_removed_edges;
  }

  // The epsilon-removed NFA being created.
  std::unique_ptr<NFA> epsilon_removed_;

  // The original NFA being transformed.
  const NFA& nfa_;

  // Mapping from states in `nfa_` to corresponding state in `epsilon_removed_'.
  // Only the start state, the final state, plus states which are the target
  // of at least one row-consuming edge are mapped.
  absl::flat_hash_map<NFAState, NFAState> state_map_;
};

absl::StatusOr<std::unique_ptr<NFA>> RemoveEpsilons(const NFA& nfa) {
  GOOGLESQL_RETURN_IF_ERROR(nfa.Validate(NFA::ValidationMode::kForEpsilonRemover));
  EpsilonRemover remover(nfa);
  return remover.GetResult();
}

}  // namespace googlesql::functions::match_recognize
