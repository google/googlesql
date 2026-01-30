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
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/common/match_recognize/nfa_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql::functions::match_recognize {
namespace {

TEST(EpsilonRemover, EmptyPathToFinalState) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n3);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1")}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, ChainOfSymbols) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);
  PatternVariableId c_var(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(3));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, a_var, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, b_var, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, c_var, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, n7));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n7);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1").On(a_var)}),
                                    State("N1", {Edge("N2").On(b_var)}),
                                    State("N2", {Edge("N3").On(c_var)}),
                                    State("N3", {Edge("N4")}),
                                    State("N4", {}),
                                })));
}

TEST(EpsilonRemover, DiamondGraph) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));

  // From the start state (n0), the "a" path is preferred over the "b" path.
  // Make sure this same preference is preserved in the epsilon-removed graph.
  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, a_var, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, b_var, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n7));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, n7));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n7);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(a_var), Edge("N2").On(b_var)}),
                  State("N1", {Edge("N3")}),
                  State("N2", {Edge("N3")}),
                  State("N3", {}),
              })));
}

TEST(EpsilonRemover, EpsilonCycle) {
  // Make sure loops in the graph that consume no input are ignored.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState start = nfa->NewState();
  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(start, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n0));
  nfa->SetAsStartState(start);
  nfa->SetAsFinalState(n2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1")}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, LargerEpsilonCycle) {
  // Similar to the above, but has a larger epsilon cycle spanning more states.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState start = nfa->NewState();
  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(start, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n3));
  nfa->SetAsStartState(start);
  nfa->SetAsFinalState(n3);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1")}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, NestedEpsilonCycle) {
  // Two epsilon loops nested inside one another.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState start = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(start, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n4));

  nfa->SetAsStartState(start);
  nfa->SetAsFinalState(n4);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1")}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, GreedyConsumingCycle) {
  // Cycle that consumes input rows, which prefers to repeat as many times as
  // possible (e.g. an "a*" pattern).
  PatternVariableId a_var(0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState start = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(start, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, a_var, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n2));
  nfa->SetAsStartState(start);
  nfa->SetAsFinalState(n2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(a_var), Edge("N2")}),
                  State("N1", {Edge("N1").On(a_var), Edge("N2")}),
                  State("N2", {}),
              })));
}

TEST(EpsilonRemover, ReluctantConsumingCycle) {
  // Cycle that consumes input rows, which prefers to repeat as few times as
  // possible (e.g. an "a*?" pattern).
  PatternVariableId a_var(0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState start = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(start, n0));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, a_var, n0));
  nfa->SetAsStartState(start);
  nfa->SetAsFinalState(n2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N2"), Edge("N1").On(a_var)}),
                  State("N1", {Edge("N2"), Edge("N1").On(a_var)}),
                  State("N2", {}),
              })));
}

TEST(EpsilonRemover, Complex) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  // NFA representation of the following pattern:
  //   a ()* (b|)*().
  // After epsilon removal, the graph should look like simply "a b*".
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  NFAState n8 = nfa->NewState();
  NFAState n9 = nfa->NewState();
  NFAState n10 = nfa->NewState();

  // a
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, a_var, n2));

  // ()*
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n4));

  // (b|)*
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, b_var, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n7));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, n8));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n7, n8));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n8, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n9));

  // ()
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n9, n10));

  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n10);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  // Note: While states N1 and N2 could be combined, the epsilon remover code
  // is not smart enough to simplify the graph, so we just expect what it
  // actually produces, since it's functionally correct.
  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(a_var)}),
                  State("N1", {Edge("N2").On(b_var), Edge("N3")}),
                  State("N2", {Edge("N2").On(b_var), Edge("N3")}),
                  State("N3", {}),
              })));
}
TEST(EpsilonRemover, HeadAnchorBasic) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1).SetHeadAnchored()));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1").WithHeadAnchor()}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, HeadAnchors) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, NFAEdge(PatternVariableId(0), n4)));
  GOOGLESQL_ASSERT_OK(
      nfa->AddEdge(n4, NFAEdge(PatternVariableId(0), n5).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, n6));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n6);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                  State("N1", {}),
                  State("N2", {}),
              })));
}

TEST(EpsilonRemover, ReachableTailAnchors) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, PatternVariableId(0), n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, PatternVariableId(0), n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, NFAEdge(n6).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, NFAEdge(n7)));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n7);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1").On(0)}),
                                    State("N1", {Edge("N2").On(0)}),
                                    State("N2", {Edge("N3").WithTailAnchor()}),
                                    State("N3", {}),
                                })));
}

TEST(EpsilonRemover, UnreachableTailAnchors) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, NFAEdge(PatternVariableId(0), n4)));
  GOOGLESQL_ASSERT_OK(
      nfa->AddEdge(n4, NFAEdge(PatternVariableId(0), n5).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, n6));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n6);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, EpsilonPathWithHeadAndTailAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, NFAEdge(n2).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, NFAEdge(PatternVariableId(0), n4)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n5));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n5);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  // Note: The final state is unreachable, as it is not possible for the same
  // position to satisfy both a head anchor and tail anchor at the same time
  // (because an empty input is not allowed to produce a match, not even an
  //  empty match).
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {}),
                                    State("N1", {}),
                                })));
}

TEST(EpsilonRemover, DiamondEpsilonPathWithOptionalHeadAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n2)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, NFAEdge(PatternVariableId(0), n4)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n5));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n5);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  // Note: The expected graph has no anchor here, as an optional head anchor is
  // semantically equivalent to no anchor at all.
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1").On(0)}),
                                    State("N1", {Edge("N2")}),
                                    State("N2", {}),
                                })));
}

TEST(EpsilonRemover, EpsilonCycleWithHeadAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, NFAEdge(n2).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, NFAEdge(n1)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, NFAEdge(PatternVariableId(0), n5)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, n6));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n6);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                  State("N1", {Edge("N2")}),
                  State("N2", {}),
              })));
}

TEST(EpsilonRemover, EpsilonCycleWithHeadAnchorTailAnchorOrNoAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  NFAState n8 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, NFAEdge(n2).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, NFAEdge(n3).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, NFAEdge(PatternVariableId(0), n7)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n7, n8));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n8);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));

  // Note: The expected graph has no anchors in it, as the ability to go from
  // n0->n5 in the original graph without an anchor makes the anchored edges
  // redundant.
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1").On(0)}),
                                    State("N1", {Edge("N2")}),
                                    State("N2", {}),
                                })));
}

TEST(EpsilonRemover, EpsilonCycleWithHeadAnchorTailAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, NFAEdge(n1)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, NFAEdge(n2).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, NFAEdge(n3).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n4));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, NFAEdge(PatternVariableId(0), n6)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, n7));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n7);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  EXPECT_THAT(*epsilon_removed,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                  State("N1", {Edge("N2")}),
                  State("N2", {}),
              })));
}

TEST(EpsilonRemover, NestedEpsilonCycleWithHeadAnchorTailAnchorOrNoAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  NFAState n5 = nfa->NewState();
  NFAState n6 = nfa->NewState();
  NFAState n7 = nfa->NewState();
  NFAState n8 = nfa->NewState();
  NFAState n9 = nfa->NewState();
  NFAState n10 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, NFAEdge(n3).SetHeadAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, NFAEdge(n4).SetTailAnchored()));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n5));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n4, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n5, n6));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n6, n7));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n7, n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n7, n8));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n8, NFAEdge(PatternVariableId(0), n9)));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n9, n10));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n10);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> epsilon_removed,
                       RemoveEpsilons(*nfa));
  // Note: The expected graph has no anchors in it, as the ability to go from
  // n0->n5 in the original graph without an anchor makes the anchored edges
  // redundant.
  EXPECT_THAT(*epsilon_removed, EquivToGraph(Graph({
                                    State("N0", {Edge("N1").On(0)}),
                                    State("N1", {Edge("N2")}),
                                    State("N2", {}),
                                })));
}
}  // namespace
}  // namespace googlesql::functions::match_recognize
