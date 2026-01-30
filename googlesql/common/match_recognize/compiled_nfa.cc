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

#include "googlesql/common/match_recognize/compiled_nfa.h"

#include <memory>
#include <optional>
#include <string>

#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.pb.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {
using CompiledNFAProto = ::googlesql::functions::match_recognize::
    StateMachineProto::CompiledNFAProto;
using EdgeProto = ::googlesql::functions::match_recognize::StateMachineProto::
    CompiledNFAProto::EdgeProto;
using StateProto = ::googlesql::functions::match_recognize::StateMachineProto::
    CompiledNFAProto::StateProto;

absl::StatusOr<std::unique_ptr<CompiledNFA>> CompiledNFA::Create(
    const NFA& nfa) {
  auto compiled = absl::WrapUnique(new CompiledNFA(nfa));
  compiled->first_edge_number_by_state_.resize(nfa.num_states());
  for (int i = 0; i < nfa.num_states(); ++i) {
    NFAState state(i);
    compiled->first_edge_number_by_state_[i] = compiled->num_edges();
    for (const NFAEdge& nfa_edge : nfa.GetEdgesFrom(state)) {
      compiled->edges_.push_back(
          {.edge_number = compiled->num_edges(),
           .from = state,
           .to = nfa_edge.target,
           .consumed = nfa_edge.pattern_var,
           .is_head_anchored = nfa_edge.is_head_anchored,
           .is_tail_anchored = nfa_edge.is_tail_anchored});
    }
  }
  return compiled;
}

CompiledNFA::CompiledNFA(const NFA& nfa)
    : start_state_(nfa.start_state()),
      final_state_(nfa.final_state()),
      num_pattern_variables_(nfa.num_pattern_variables()) {}

CompiledNFA::CompiledNFA(const CompiledNFAProto& proto)
    : start_state_(proto.start_state()),
      final_state_(proto.final_state()),
      num_pattern_variables_(proto.num_pattern_variables()) {
  int state_number = 0;
  int edge_number = 0;
  for (const StateProto& state_proto : proto.states()) {
    first_edge_number_by_state_.push_back(edge_number);
    for (const EdgeProto& edge_proto : state_proto.edges()) {
      Edge& edge = edges_.emplace_back();
      edge.edge_number = edge_number;
      edge.from = NFAState(state_number);
      edge.to = NFAState(edge_proto.to_state());
      if (edge_proto.has_pattern_variable()) {
        edge.consumed = PatternVariableId(edge_proto.pattern_variable());
      }
      edge.is_head_anchored = edge_proto.is_head_anchored();
      edge.is_tail_anchored = edge_proto.is_tail_anchored();
      ++edge_number;
    }
    ++state_number;
  }
}

absl::StatusOr<std::unique_ptr<const CompiledNFA>> CompiledNFA::Deserialize(
    const CompiledNFAProto& proto) {
  auto nfa = absl::WrapUnique(new CompiledNFA(proto));
  GOOGLESQL_RETURN_IF_ERROR(nfa->Validate());
  return nfa;
}

absl::Status CompiledNFA::Validate() const {
  GOOGLESQL_RET_CHECK(start_state_.IsValid());
  GOOGLESQL_RET_CHECK_LT(start_state_.value(), num_states());

  GOOGLESQL_RET_CHECK(final_state_.IsValid());
  GOOGLESQL_RET_CHECK_LT(final_state_.value(), num_states());

  NFAState prior_edge_from;
  for (const Edge& edge : edges_) {
    GOOGLESQL_RET_CHECK(edge.from.IsValid());
    GOOGLESQL_RET_CHECK_LT(edge.from.value(), num_states());
    GOOGLESQL_RET_CHECK_GE(edge.from.value(), prior_edge_from.value());

    GOOGLESQL_RET_CHECK(edge.to.IsValid());
    GOOGLESQL_RET_CHECK_LT(edge.to.value(), num_states());

    if (edge.consumed != std::nullopt) {
      GOOGLESQL_RET_CHECK_LT(edge.consumed->value(), num_pattern_variables_);
    }

    prior_edge_from = edge.from;
  }
  GOOGLESQL_RET_CHECK_GE(num_pattern_variables_, 0);

  return absl::OkStatus();
}

CompiledNFAProto CompiledNFA::Serialize() const {
  CompiledNFAProto proto;
  proto.set_start_state(start_state_.value());
  proto.set_final_state(final_state_.value());
  proto.set_num_pattern_variables(num_pattern_variables_);
  for (int i = 0; i < num_states(); ++i) {
    proto.add_states();
  }
  for (const Edge& edge : edges_) {
    EdgeProto& edge_proto =
        *proto.mutable_states(edge.from.value())->add_edges();
    edge_proto.set_to_state(edge.to.value());
    if (edge.consumed != std::nullopt) {
      edge_proto.set_pattern_variable(edge.consumed->value());
    }
    if (edge.is_head_anchored) {
      edge_proto.set_is_head_anchored(true);
    }
    if (edge.is_tail_anchored) {
      edge_proto.set_is_tail_anchored(true);
    }
  }
  return proto;
}

absl::Span<const Edge> CompiledNFA::GetEdgesFrom(NFAState state) const {
  int start_edge_number = first_edge_number_by_state_[state.value()];
  int next_state_number = state.value() + 1;
  int end_edge_number =
      (next_state_number == first_edge_number_by_state_.size())
          ? num_edges()
          : first_edge_number_by_state_[next_state_number];

  return GetAllEdges().subspan(start_edge_number,
                               end_edge_number - start_edge_number);
}

// NOMUTANTS -- Debug utility only.
std::string CompiledNFA::DebugString() const {
  std::string result;

  // Display details of all of the edges.
  for (int i = 0; i < num_edges(); ++i) {
    absl::StrAppend(&result, i, ": ", edges_[i].from, " => ", edges_[i].to,
                    (edges_[i].consumed.has_value()
                         ? absl::StrCat(" (consuming pattern_var#",
                                        edges_[i].consumed->value(), ")")
                         : ""),
                    (edges_[i].is_head_anchored ? " (head-anchored)" : ""),
                    (edges_[i].is_tail_anchored ? " (tail-anchored)" : ""),
                    "\n");
  }
  absl::StrAppend(&result, "Start state: ", start_state_.value(),
                  "\nFinal state: ", final_state_.value(),
                  "\nPattern variable count: ", num_pattern_variables_, "\n");
  return result;
}
}  // namespace googlesql::functions::match_recognize
