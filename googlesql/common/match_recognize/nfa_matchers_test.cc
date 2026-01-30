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
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/base/testing/status_matchers.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StartsWith;

class NFAMatchersTest : public testing::Test {
 public:
  NFAMatchersTest() = default;

  void SetUp() override {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(nfa_, CreateNFA(false));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(nfa_with_anchors_, CreateNFA(true));
  }

 protected:
  static absl::StatusOr<std::unique_ptr<NFA>> CreateNFA(bool with_anchors) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NFA> nfa, NFA::Create(2));
    NFAState s0 = nfa->NewState();
    NFAState s1 = nfa->NewState();
    NFAState s2 = nfa->NewState();
    NFAState s3 = nfa->NewState();
    NFAState s4 = nfa->NewState();
    NFAState s5 = nfa->NewState();
    nfa->SetAsStartState(s2);
    nfa->SetAsFinalState(s3);
    GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s2, s4));
    if (with_anchors) {
      GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s4, NFAEdge(s5).SetHeadAnchored()));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s4, s5));
    }
    if (with_anchors) {
      GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s4, NFAEdge(kA, s0).SetTailAnchored()));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s4, kA, s0));
    }
    GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s5, s0));
    GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s0, kA, s1));
    GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s1, s0));
    GOOGLESQL_RETURN_IF_ERROR(nfa->AddEdge(s1, s3));

    return nfa;
  }

  std::unique_ptr<NFA> nfa_;
  std::unique_ptr<NFA> nfa_with_anchors_;
  static constexpr PatternVariableId kA = PatternVariableId(0);
  static constexpr PatternVariableId kB = PatternVariableId(1);
};

TEST_F(NFAMatchersTest, TestGraphDebugString) {
  TestGraph graph = Graph({
      State("S2", {Edge("S4")}),
      State("S0", {Edge("S1").On(kA).WithHeadAnchor().WithTailAnchor()}),
      State("S1", {Edge("S0"), Edge("S3")}),
      State("S4", {Edge("S5"), Edge("S0").On(kA)}),
      State("S5", {Edge("S0")}),
      State("S3", {}),
  });
  EXPECT_THAT(graph.GetCodeSyntax(), Eq(R"({
  State("S2", {Edge("S4")}),
  State("S0", {Edge("S1").On(0).WithHeadAnchor().WithTailAnchor()}),
  State("S1", {Edge("S0"), Edge("S3")}),
  State("S4", {Edge("S5"), Edge("S0").On(0)}),
  State("S5", {Edge("S0")}),
  State("S3", {}),
})"));
}

TEST_F(NFAMatchersTest, GetTestOutputForNfa) {
  std::string output = GetTestOutputForNfa(*nfa_with_anchors_);
  EXPECT_THAT(
      output,
      StartsWith(Graph({
                           State("N2", {Edge("N4")}),
                           State("N4", {Edge("N5").WithHeadAnchor(),
                                        Edge("N0").On(kA).WithTailAnchor()}),
                           State("N5", {Edge("N0")}),
                           State("N0", {Edge("N1").On(kA)}),
                           State("N1", {Edge("N0"), Edge("N3")}),
                           State("N3", {}),
                       })
                     .GetCodeSyntax()));
}

TEST_F(NFAMatchersTest, EquivToGraph) {
  EXPECT_THAT(*nfa_, EquivToGraph(Graph({
                         State("N0", {Edge("N1")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     })));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithReorderedStates) {
  // For now, we require the graph to list the edges in the same order as the
  // graph (after state renumbering) in order to be considered equivalent,
  // although the labels may be different.
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1")}),
                         State("N2", {Edge("N3")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithDifferentStartState) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N0", {Edge("N1")}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithDifferentFinalState) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N5", {}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                     }))));
}

TEST_F(NFAMatchersTest, EquivToGraphWithDifferentLabels) {
  EXPECT_THAT(*nfa_, EquivToGraph(Graph({
                         State("N0_", {Edge("N1_")}),
                         State("N1_", {Edge("N2_"), Edge("N3_").On(kA)}),
                         State("N2_", {Edge("N3_")}),
                         State("N3_", {Edge("N4_").On(kA)}),
                         State("N4_", {Edge("N3_"), Edge("N5_")}),
                         State("N5_", {}),
                     })));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWhereEdgeOperatesOnDifferentPatternVar) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kB)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWhereEdgeBecomesEpsilon) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1")}),
                         State("N1", {Edge("N2"), Edge("N3")}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWhereEdgeBecomesNotEpsilon) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1").On(kA)}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithExtraUnreachableState) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("Unreached", {}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithExtraEdgeAddedToState) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N1"), Edge("N2")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithEdgeRemovedFromState) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWhereTargetOfEdgeHasChanged) {
  EXPECT_THAT(*nfa_, Not(EquivToGraph(Graph({
                         State("N0", {Edge("N2")}),
                         State("N1", {Edge("N2"), Edge("N3").On(kA)}),
                         State("N2", {Edge("N3")}),
                         State("N3", {Edge("N4").On(kA)}),
                         State("N4", {Edge("N3"), Edge("N5")}),
                         State("N5", {}),
                     }))));
}

TEST_F(NFAMatchersTest, EquivToGraphWithAnchors) {
  EXPECT_THAT(*nfa_with_anchors_,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1")}),
                  State("N1", {Edge("N2").WithHeadAnchor(),
                               Edge("N3").On(kA).WithTailAnchor()}),
                  State("N2", {Edge("N3")}),
                  State("N3", {Edge("N4").On(kA)}),
                  State("N4", {Edge("N3"), Edge("N5")}),
                  State("N5", {}),
              })));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithHeadAnchorAddedToEdge) {
  EXPECT_THAT(*nfa_,
              Not(EquivToGraph(Graph({
                  State("N0", {Edge("N1")}),
                  State("N1", {Edge("N2").WithHeadAnchor(), Edge("N3").On(kA)}),
                  State("N2", {Edge("N3")}),
                  State("N3", {Edge("N4").On(kA)}),
                  State("N4", {Edge("N3"), Edge("N5")}),
                  State("N5", {}),
              }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithTailAnchorAddedToEdge) {
  EXPECT_THAT(*nfa_,
              Not(EquivToGraph(Graph({
                  State("N0", {Edge("N1")}),
                  State("N1", {Edge("N2").WithTailAnchor(), Edge("N3").On(kA)}),
                  State("N2", {Edge("N3")}),
                  State("N3", {Edge("N4").On(kA)}),
                  State("N4", {Edge("N3"), Edge("N5")}),
                  State("N5", {}),
              }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithHeadAnchorRemovedFromEdge) {
  EXPECT_THAT(*nfa_with_anchors_,
              Not(EquivToGraph(Graph({
                  State("N0", {Edge("N1")}),
                  State("N1", {Edge("N2"), Edge("N3").On(kA).WithTailAnchor()}),
                  State("N2", {Edge("N3")}),
                  State("N3", {Edge("N4").On(kA)}),
                  State("N4", {Edge("N3"), Edge("N5")}),
                  State("N5", {}),
              }))));
}

TEST_F(NFAMatchersTest, NotEquivToGraphWithTailAnchorRemovedFromEdge) {
  EXPECT_THAT(*nfa_with_anchors_,
              Not(EquivToGraph(Graph({
                  State("N0", {Edge("N1")}),
                  State("N1", {Edge("N2").WithHeadAnchor(), Edge("N3").On(kA)}),
                  State("N2", {Edge("N3")}),
                  State("N3", {Edge("N4").On(kA)}),
                  State("N4", {Edge("N3"), Edge("N5")}),
                  State("N5", {}),
              }))));
}

}  // namespace googlesql::functions::match_recognize
