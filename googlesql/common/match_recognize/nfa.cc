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

#include "googlesql/common/match_recognize/nfa.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

absl::StatusOr<std::unique_ptr<NFA>> NFA::Create(int num_pattern_variables) {
  GOOGLESQL_RET_CHECK(num_pattern_variables > 0);
  return absl::WrapUnique(new NFA(num_pattern_variables));
}

NFA::NFA(int num_pattern_variables)
    : num_pattern_variables_(num_pattern_variables) {
  ABSL_DCHECK_GT(num_pattern_variables, 0);
}

std::unique_ptr<NFA> NFA::CreateEmptyNFA() const {
  return absl::WrapUnique(new NFA(num_pattern_variables_));
}

NFAState NFA::NewState() {
  edges_.emplace_back();
  return NFAState(static_cast<int>(edges_.size()) - 1);
}

absl::Status NFA::AddEdge(NFAState state, const NFAEdge& edge) {
  GOOGLESQL_RET_CHECK(state.IsValid());
  GOOGLESQL_RET_CHECK_LT(state.value(), num_states());
  GOOGLESQL_RET_CHECK(edge.target.IsValid());
  GOOGLESQL_RET_CHECK_LT(edge.target.value(), num_states());

  if (edge.pattern_var.has_value()) {
    GOOGLESQL_RET_CHECK_LT(edge.pattern_var->value(), num_pattern_variables_);
  }

  if (edge_count_ >= kMaxSupportedEdges) {
    return absl::OutOfRangeError(
        "MATCH_RECOGNIZE pattern is too complex. This can happen if the "
        "pattern is too long, quantifier bounds are too large, or if bounded "
        "quantifiers are too deeply nested");
  }
  ++edge_count_;
  edges_[state.value()].push_back(edge);
  return absl::OkStatus();
}

absl::Status NFA::Validate(ValidationMode mode) const {
  GOOGLESQL_RETURN_IF_ERROR(ValidateInternal(mode)) << "\nNFA:\n" << AsDot();
  return absl::OkStatus();
}

absl::Status ValidateAllStatesReachable(const NFA& nfa) {
  absl::flat_hash_set<NFAState> reachable_states;
  std::vector<NFAState> stack;
  reachable_states.insert(nfa.start_state());
  stack.push_back(nfa.start_state());
  while (!stack.empty()) {
    NFAState state = stack.back();
    stack.pop_back();

    for (const NFAEdge& edge : nfa.GetEdgesFrom(state)) {
      if (reachable_states.contains(edge.target)) {
        continue;
      }
      reachable_states.insert(edge.target);
      stack.push_back(edge.target);
    }
  }
  if (reachable_states.size() == nfa.num_states()) {
    return absl::OkStatus();
  }

  // We have at least one unreachable state. Generate a error message that says
  // which ones are unreachable.
  std::vector<NFAState> unreachable_states;
  for (int i = 0; i < nfa.num_states(); ++i) {
    NFAState state(i);
    if (!reachable_states.contains(state)) {
      unreachable_states.push_back(state);
    }
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Unreachable states detected: "
                   << absl::StrJoin(unreachable_states, ", ");
}

absl::Status NFA::ValidateInternal(ValidationMode mode) const {
  GOOGLESQL_RET_CHECK_GT(num_pattern_variables_, 0);
  GOOGLESQL_RET_CHECK(start_state().IsValid());
  GOOGLESQL_RET_CHECK(final_state().IsValid());
  GOOGLESQL_RET_CHECK_LT(start_state().value(), num_states());
  GOOGLESQL_RET_CHECK_LT(final_state().value(), num_states());
  GOOGLESQL_RET_CHECK(GetEdgesFrom(final_state()).empty())
      << "Edge detected out of final state";

  for (int state_num = 0; state_num < num_states(); ++state_num) {
    NFAState state(state_num);
    const std::vector<NFAEdge>& edge_list = edges_[state_num];
    for (int i = 0; i < edge_list.size(); ++i) {
      const NFAEdge& edge = edge_list[i];

      GOOGLESQL_RET_CHECK(edge.target.IsValid());
      GOOGLESQL_RET_CHECK_LT(edge.target.value(), num_states())
          << "Edge not present in NFA";

      GOOGLESQL_RET_CHECK(edge.target != start_state())
          << "Detected edge from " << absl::StrCat(state)
          << " into the start state (" << absl::StrCat(start_state()) << ")";

      if (edge.pattern_var.has_value()) {
        GOOGLESQL_RET_CHECK(edge.pattern_var->IsValid());
        GOOGLESQL_RET_CHECK_LT(edge.pattern_var->value(), num_pattern_variables_);

        GOOGLESQL_RET_CHECK(edge.target != final_state())
            << "Non-Epsilon edge from " << absl::StrCat(state)
            << " detected into final state (" << absl::StrCat(final_state())
            << ")";
      } else if (mode == ValidationMode::kForMatching) {
        GOOGLESQL_RET_CHECK(edge.target == final_state())
            << "Detected epsilon edge from " << absl::StrCat(state)
            << " into non-final state (" << absl::StrCat(edge.target) << ")";
      }
    }
  }

  if (mode == ValidationMode::kForEpsilonRemover) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateAllStatesReachable(*this));
  }
  return absl::OkStatus();
}

std::string NFA::AsDot(absl::Span<const std::string> pattern_var_names) const {
  std::string dot;
  absl::StrAppend(&dot, "digraph {\n");
  absl::StrAppend(
      &dot,
      "  stylesheet = \"/frameworks/g3doc/includes/graphviz-style.css\"\n\n");
  absl::StrAppend(&dot, "  ", start_state_, " [shape=\"rectangle\" label=\"",
                  start_state_, " (start)\"]\n");
  absl::StrAppend(&dot, "  ", final_state_, " [shape=\"rectangle\" label=\"",
                  final_state_, " (end)\"]\n");
  for (int state_num = 0; state_num < num_states(); ++state_num) {
    NFAState state(state_num);
    const std::vector<NFAEdge>& edge_list = edges_[state_num];
    for (int i = 0; i < edge_list.size(); ++i) {
      const NFAEdge& edge = edge_list[i];
      absl::StrAppend(&dot, "  ", state, "->", edge.target, " [label=\" ");
      if (edge.is_head_anchored) {
        absl::StrAppend(&dot, "^");
      }
      if (edge.is_tail_anchored) {
        absl::StrAppend(&dot, "$");
      }
      if (edge.pattern_var.has_value()) {
        std::string pattern_var_name;
        if (edge.pattern_var->value() < pattern_var_names.size()) {
          pattern_var_name = pattern_var_names[edge.pattern_var->value()];
        } else {
          pattern_var_name = absl::StrCat("V", edge.pattern_var->value());
        }
        absl::StrAppend(&dot, pattern_var_name);
      }
      absl::StrAppend(&dot, "(", i, ")\"];\n");
    }
  }
  absl::StrAppend(&dot, "}\n");
  return dot;
}
}  // namespace googlesql::functions::match_recognize
