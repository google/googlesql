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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_MATCHERS_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_MATCHERS_H_

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "gmock/gmock.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

// An edge in a graph that an NFA is expected to match.
struct TestEdge {
  std::string target_label;
  std::optional<PatternVariableId> pattern_var_id;
  bool is_head_anchored = false;
  bool is_tail_anchored = false;

  TestEdge On(PatternVariableId pattern_var_id) const {
    return TestEdge{.target_label = this->target_label,
                    .pattern_var_id = pattern_var_id};
  }

  // Convenience overload that takes the pattern variable as an int, rather than
  // a PatternVariableId to reduce verbosity in test construction.
  TestEdge On(int pattern_var_id) const {
    return TestEdge{.target_label = this->target_label,
                    .pattern_var_id = PatternVariableId(pattern_var_id)};
  }

  TestEdge WithHeadAnchor() const {
    TestEdge edge(*this);
    edge.is_head_anchored = true;
    return edge;
  }

  TestEdge WithTailAnchor() const {
    TestEdge edge(*this);
    edge.is_tail_anchored = true;
    return edge;
  }
};

// A state in a graph that an NFA is expected to match.
struct TestState {
  // Label that identifies the state; must be unique within the graph.
  std::string label;

  // Edges that this state is expected to have.
  std::vector<TestEdge> edges;
};

// An entire graph that an NFA is expected to match.
struct TestGraph {
  std::vector<TestState> states;

  // Convenience wrapper to allow TestGraph objects to be absl-stringified
  // into the result of GetCodeSyntax().
  //
  // The returned string is for debugging only, and should not be relied on in
  // tests. Exact format of the returned string is subject to change.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TestGraph& graph) {
    absl::Format(&sink, "%s", graph.GetCodeSyntax());
  }

  // Returns a string specifying the C++ code necessary to produce this graph.
  // When EquivToGraph() fails, this allows test authors to copy-paste the
  // 'actual' output into the test code as the expected output.
  //
  // The returned string is for debugging only, and should not be relied on in
  // tests. Exact format of the returned string is subject to change.
  std::string GetCodeSyntax() const;
};

// Returns test output for an NFA. This is C++ code to produce an equivalent
// TestGraph object.
std::string GetTestOutputForNfa(const NFA& nfa);

// Returns true if the given NFA is completely identical to the given graph.
// That is, same exact states in the same exact order, with each state
// containing identical edges in the same order.
bool EqualsGraphImpl(const NFA& nfa, const TestGraph& graph);

// Returns a new NFA that is functionally equivalent to the provided NFA, but
// with the states renumbered topologically so that the start state is always
// N0, the first edge out of N0 goes to N1, etc., with the largest state number
// the final state.
//
// Renumbered NFAs are more useful for comparison in test assertion because it
// prevents implementation details around state number assignment from leaking
// into the test results.
std::unique_ptr<NFA> RenumberStates(const NFA& nfa);

// Overrides formatting of the NFA for testing so that the renumbered NFA is
// shown alongside the NFA, itself. This makes for a better diff in the test
// output when EquivToGraph() fails.
void PrintTo(const NFA& nfa, std::ostream* os);

// Matches when the argument (an NFA) becomes exactly equal to 'graph' after
// state renumbering. For example, if 'graph' is (N0->N1, N1->N2) and 'arg' is
// (N2->N3, N3->N1), the NFA will be transformed to (N0->N1, N1->N2) via state
// renumbering, allowing the match to succeed.
//
// To avoid test brittleness, it is recommended that EquivToGraph be used over
// EqualsGraph() in most cases.
MATCHER_P(EquivToGraph, graph,
          absl::StrCat(negation ? "Not equal" : "Equal",
                       " to the follow graph, after renumbering:\n", graph)) {
  return EqualsGraphImpl(*RenumberStates(arg), graph);
}

// Convenience functions to concisely construct a graph in a test case to use
// as an argument to EqualsGraph/EquivToGraph.
//
// Expected usage:
// EXPECT_THAT(*nfa,
//             EquivToGraph(Graph(vars, {
//                                       State("N0", {EdgeTo("N1").On("A")}),
//                                       State("N1", {}),
//                                     })));
TestEdge Edge(absl::string_view target_label);
TestState State(absl::string_view label, const std::vector<TestEdge>& edges);
TestGraph Graph(const std::vector<TestState>& states);

}  // namespace googlesql::functions::match_recognize

#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_MATCHERS_H_
