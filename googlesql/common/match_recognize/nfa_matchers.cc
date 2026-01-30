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

#include "googlesql/common/match_recognize/nfa_matchers.h"

#include <memory>
#include <ostream>
#include <stack>
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

// Use depth-first ordering, with the exception of the final state, which always
// goes last.
static std::vector<NFAState> GetStateOrdering(const NFA& nfa) {
  std::vector<NFAState> result;
  std::vector<bool> visited(nfa.num_states());
  std::stack<NFAState> stack;
  stack.push(nfa.start_state());
  while (!stack.empty()) {
    NFAState state = stack.top();
    stack.pop();

    if (visited[state.value()]) {
      continue;
    }

    result.push_back(state);
    visited[state.value()] = true;

    const std::vector<NFAEdge>& edges = nfa.GetEdgesFrom(state);
    for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
      const NFAEdge& edge = *it;
      if (visited[edge.target.value()]) {
        continue;
      }
      if (edge.target == nfa.final_state() && !stack.empty()) {
        // Defer the final state until we hit the last edge going into it
        // (e.g. when there's nothing else left in the stack).
        continue;
      }
      stack.push(edge.target);
    }
  }

  // If the final state was never seen (due to the special-case around the final
  // state above), add it end at the end, now. This case is possible only if the
  // final state is unreachable.
  if (!visited[nfa.final_state().value()]) {
    result.push_back(nfa.final_state());
  }
  return result;
}

static TestGraph GetTestGraphForNfa(const NFA& nfa) {
  std::vector<NFAState> ordered_states = GetStateOrdering(nfa);
  TestGraph graph;
  for (NFAState nfa_state : ordered_states) {
    TestState state;
    state.label = absl::StrCat(nfa_state);
    for (const NFAEdge& edge : nfa.GetEdgesFrom(nfa_state)) {
      TestEdge test_edge = {.target_label = absl::StrCat(edge.target),
                            .is_head_anchored = edge.is_head_anchored,
                            .is_tail_anchored = edge.is_tail_anchored};
      if (edge.pattern_var.has_value()) {
        test_edge.pattern_var_id = edge.pattern_var;
      }
      state.edges.push_back(test_edge);
    }
    graph.states.push_back(state);
  }
  return graph;
}

std::string GetTestOutputForNfa(const NFA& nfa) {
  std::string result = GetTestGraphForNfa(nfa).GetCodeSyntax();
  return result;
}

void PrintTo(const NFA& nfa, std::ostream* os) {
  std::unique_ptr<NFA> renumbered = RenumberStates(nfa);

  *os << GetTestOutputForNfa(*renumbered) << "\nBefore renumbering:\n"
      << GetTestOutputForNfa(nfa);
}

std::string TestGraph::GetCodeSyntax() const {
  std::string result;
  absl::StrAppend(&result, "{\n");
  for (int i = 0; i < states.size(); ++i) {
    const TestState& state = states[i];
    absl::StrAppend(&result, "  State(\"", state.label, "\", {");
    bool first_edge = true;
    for (const TestEdge& edge : state.edges) {
      if (first_edge) {
        first_edge = false;
      } else {
        absl::StrAppend(&result, ", ");
      }
      absl::StrAppend(&result, "Edge(\"", edge.target_label, "\")");
      if (edge.pattern_var_id.has_value()) {
        absl::StrAppend(&result, ".On(", edge.pattern_var_id->value(), ")");
      }
      if (edge.is_head_anchored) {
        absl::StrAppend(&result, ".WithHeadAnchor()");
      }
      if (edge.is_tail_anchored) {
        absl::StrAppend(&result, ".WithTailAnchor()");
      }
    }
    absl::StrAppend(&result, "}),\n");
  }
  absl::StrAppend(&result, "}");
  return result;
}

std::unique_ptr<NFA> RenumberStates(const NFA& nfa) {
  std::vector<NFAState> state_order = GetStateOrdering(nfa);

  // Create new NFA states and build a map to associate each original state
  // with its new state.
  std::unique_ptr<NFA> new_nfa = nfa.CreateEmptyNFA();

  // Index = state number in original NFA; value = state in new NFA.
  std::vector<NFAState> state_map(nfa.num_states());
  for (int i = 0; i < state_order.size(); ++i) {
    state_map[state_order[i].value()] = new_nfa->NewState();
  }

  for (NFAState old_nfa_state : state_order) {
    for (const NFAEdge& edge : nfa.GetEdgesFrom(old_nfa_state)) {
      NFAEdge new_edge(edge);
      new_edge.target = state_map[edge.target.value()];
      GOOGLESQL_CHECK_OK(new_nfa  // Crash OK
                   ->AddEdge(state_map[old_nfa_state.value()], new_edge));
    }
  }
  new_nfa->SetAsStartState(state_map[nfa.start_state().value()]);
  new_nfa->SetAsFinalState(state_map[nfa.final_state().value()]);

  return new_nfa;
}

bool EqualsGraphImpl(const NFA& nfa, const TestGraph& graph) {
  if (graph.states.size() != nfa.num_states()) {
    return false;
  }

  absl::flat_hash_map<std::string, int> label_to_index;
  for (int i = 0; i < graph.states.size(); ++i) {
    label_to_index[graph.states[i].label] = i;
  }

  for (int state_number = 0; state_number < nfa.num_states(); ++state_number) {
    NFAState state(state_number);
    const TestState& test_state = graph.states[state.value()];
    const std::vector<NFAEdge>& nfa_edges = nfa.GetEdgesFrom(state);
    if (test_state.edges.size() != nfa_edges.size()) {
      // Mismatched number of edges.
      return false;
    }
    for (int i = 0; i < nfa_edges.size(); ++i) {
      if (nfa_edges[i].pattern_var.has_value()) {
        if (!test_state.edges[i].pattern_var_id.has_value()) {
          // Edge expected to not have pattern variable, but does.
          return false;
        }
        PatternVariableId actual_pattern_var = *nfa_edges[i].pattern_var;
        if (actual_pattern_var != *test_state.edges[i].pattern_var_id) {
          // Pattern variable name does not match expected.
          return false;
        }
      } else if (test_state.edges[i].pattern_var_id.has_value()) {
        // Edge expected to have pattern variable, but doesn't.
        return false;
      }
      auto it = label_to_index.find(test_state.edges[i].target_label);
      ABSL_CHECK(it != label_to_index.end())  // Crash OK
          << "Malformed test case: state " << test_state.label
          << " has edge targeting " << test_state.edges[i].target_label
          << ", but no state with label " << test_state.edges[i].target_label
          << " exists";
      if (nfa_edges[i].target.value() != it->second) {
        // NFA edge goes to wrong target.
        return false;
      }

      if (nfa_edges[i].is_head_anchored !=
          test_state.edges[i].is_head_anchored) {
        // Whether the edge is head-anchored doesn't match expectations.
        return false;
      }
      if (nfa_edges[i].is_tail_anchored !=
          test_state.edges[i].is_tail_anchored) {
        // Whether the edge is tail-anchored doesn't match expectations.
        return false;
      }
    }
  }

  // Check if the start/final state are set correctly; we only support cases
  // where the start state is state 0 and the final state is the last one.
  if (nfa.start_state().value() != 0) {
    return false;
  }
  if (nfa.final_state().value() != nfa.num_states() - 1) {
    return false;
  }

  return true;
}

TestEdge Edge(absl::string_view target_label) {
  return TestEdge{.target_label = std::string(target_label)};
}

TestState State(absl::string_view label, const std::vector<TestEdge>& edges) {
  return TestState{.label = std::string(label), .edges = edges};
}

TestGraph Graph(const std::vector<TestState>& states) {
  return TestGraph{.states = states};
}

}  // namespace googlesql::functions::match_recognize
