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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_COMPILED_NFA_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_COMPILED_NFA_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
namespace googlesql::functions::match_recognize {

// Our representation of an edge; similar to NFAEdge, but stores the 'from'
// state along with the 'to' state.
struct Edge {
  int edge_number = -1;
  NFAState from;
  NFAState to;
  std::optional<PatternVariableId> consumed;
  bool is_head_anchored = false;
  bool is_tail_anchored = false;
};

// Data structure to manage numbering of edges in an NFA.
// Edges are numbered from 0-n, sorted first by source state, then by
// precedence order. Thus, the highest-precedence edge from a given source state
// is simply the edge from that state with the lowest edge number.
class CompiledNFA {
 public:
  static absl::StatusOr<std::unique_ptr<CompiledNFA>> Create(const NFA& nfa);
  CompiledNFA(const CompiledNFA&) = delete;
  CompiledNFA& operator=(const CompiledNFA&) = delete;

  // Returns the number of edges in the NFA.
  int num_edges() const { return static_cast<int>(edges_.size()); }

  int num_states() const {
    return static_cast<int>(first_edge_number_by_state_.size());
  }

  // Returns details about the edge associated with the given edge number.
  const Edge& GetEdge(int edge_number) const { return edges_[edge_number]; }

  // Returns details about all edges in the NFA, indexed by edge number.
  absl::Span<const Edge> GetAllEdges() const { return edges_; }

  // Returns details about all edges in the NFA from a particular source state,
  // in order of increasing edge number (highest->lowest precedence).
  absl::Span<const Edge> GetEdgesFrom(NFAState state) const;

  // Returns a string describing each edge, by edge number.
  std::string DebugString() const;

  NFAState start_state() const { return start_state_; }
  NFAState final_state() const { return final_state_; }

  // The number of pattern variables in the underlying
  // ResolvedMatchRecongnizeScan used to generate the pattern. This may exceed
  // the number of pattern variables actually referred to in edges. For example,
  // in pattern "A{0}", variable 'A' technically exists, and the engine will
  // pass it in, however, the NFA won't have any edges conditional on it.
  int num_pattern_variables() const { return num_pattern_variables_; }

  StateMachineProto::CompiledNFAProto Serialize() const;
  static absl::StatusOr<std::unique_ptr<const CompiledNFA>> Deserialize(
      const StateMachineProto::CompiledNFAProto& proto);

 private:
  explicit CompiledNFA(const NFA& nfa);
  explicit CompiledNFA(const StateMachineProto::CompiledNFAProto& proto);

  absl::Status Validate() const;

  // All edges, indexed by edge number.
  std::vector<Edge> edges_;

  // Associates each state number with the first edge number whose source state
  // >= this state.
  //
  // For example, the edge numbers associated with state i are in the range:
  //   [first_edge_number_by_state_[i], first_edge_number_by_state_[i+1])
  // (or [first_edge_number_by_state_[i], num_edges()) for the highest state
  //   number).
  std::vector<int> first_edge_number_by_state_;

  NFAState start_state_;
  NFAState final_state_;

  int num_pattern_variables_ = 0;
};
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_COMPILED_NFA_H_
