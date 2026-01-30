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
#include <ostream>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace googlesql::functions::match_recognize {

void PrintTo(const PatternVariableId& var, std::ostream* os) {
  *os << "V" << var.value();
}

void PrintTo(const NFAEdge& edge, std::ostream* os) {
  if (edge.pattern_var.has_value()) {
    *os << "Edge to " << absl::StrCat(edge.target) << " on "
        << testing::PrintToString(*edge.pattern_var);
  } else {
    *os << "Edge to " << absl::StrCat(edge.target);
  }
}

namespace {
using ::testing::ContainsRegex;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

TEST(NFATest, BuildAndUse) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state3);

  EXPECT_EQ(nfa->start_state(), state1);
  EXPECT_EQ(nfa->final_state(), state3);
  EXPECT_EQ(nfa->num_states(), 3);

  EXPECT_THAT(nfa->GetEdgesFrom(state1),
              ElementsAre(NFAEdge(state2), NFAEdge(a_var, state3)));
  EXPECT_THAT(nfa->GetEdgesFrom(state2), ElementsAre(NFAEdge(b_var, state3)));
  EXPECT_THAT(nfa->GetEdgesFrom(state3), IsEmpty());
  EXPECT_EQ(nfa->num_pattern_variables(), 2);
}

TEST(NFATest, UnreferencedPatternVars) {
  // Make sure num_pattern_variables() is set correctly if we have some extra
  // pattern variables not referenced in the NFA.
  //
  // This can arise with patterns like A{0}, which is equivalent to simply ().
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(8));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state3);

  EXPECT_EQ(nfa->start_state(), state1);
  EXPECT_EQ(nfa->final_state(), state3);
  EXPECT_EQ(nfa->num_states(), 3);

  EXPECT_THAT(nfa->GetEdgesFrom(state1),
              ElementsAre(NFAEdge(state2), NFAEdge(a_var, state3)));
  EXPECT_THAT(nfa->GetEdgesFrom(state2), ElementsAre(NFAEdge(b_var, state3)));
  EXPECT_THAT(nfa->GetEdgesFrom(state3), IsEmpty());
  EXPECT_EQ(nfa->num_pattern_variables(), 8);
}

TEST(NFATest, DotGeneration) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state3);

  std::string dot = nfa->AsDot({"a", "b"});

  // Verify that the graph content looks correct in the dot.
  EXPECT_THAT(dot, HasSubstr("digraph {"));
  EXPECT_THAT(dot, HasSubstr("N0 (start)"));
  EXPECT_THAT(dot, HasSubstr("N2 (end)"));
  EXPECT_THAT(dot, ContainsRegex("N0->N1.*\\(0\\)"));
  EXPECT_THAT(dot, ContainsRegex("N0->N2.*a\\(1\\)"));
  EXPECT_THAT(dot, ContainsRegex("N1->N2.*b\\(0\\)"));
}

TEST(NFATest, AddInvalidStateToEdge) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));
  NFAState state1 = nfa->NewState();
  NFAState invalid_state;
  EXPECT_THAT(nfa->AddEdge(state1, invalid_state),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, AddStateNotInGraphToEdge) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));
  NFAState state1 = nfa->NewState();
  NFAState missing_state(2);
  EXPECT_THAT(nfa->AddEdge(state1, missing_state),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(nfa->AddEdge(missing_state, state1),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, AddEdgeWithInvalidPatternVariable) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));
  PatternVariableId invalid_var;
  NFAState state1 = nfa->NewState();
  EXPECT_THAT(nfa->AddEdge(state1, invalid_var, state1),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, NFAStatesAreHashable) {
  absl::flat_hash_set<NFAState> state_set;
  state_set.insert(NFAState(1));
  state_set.insert(NFAState(2));

  EXPECT_THAT(state_set, UnorderedElementsAre(NFAState(1), NFAState(2)));
}

TEST(NFATest, ValidateSuccess) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();
  NFAState state4 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state3, state4));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state4);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover), IsOk());
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching), IsOk());
}

TEST(NFATest, ValidateSuccessNoEpsilon) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));

  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();
  NFAState state4 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state3, state4));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state4);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover), IsOk());
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching), IsOk());
}

TEST(NFATest, ValidateFailureEpsilonEdgeToNonfinalState) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();
  NFAState state4 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state3, state4));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state4);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover), IsOk());
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, ValidateFailureFinalStateNotSet) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();
  NFAState state4 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state3, state4));

  nfa->SetAsStartState(state1);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, ValidateFailureStartStateNotSet) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();
  NFAState state4 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state3, state4));

  nfa->SetAsFinalState(state4);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, ValidateFailureNonEpsilonEdgeToFinalState) {
  PatternVariableId a_var(0);
  PatternVariableId b_var(1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, b_var, state3));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state3);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, ValidateFailureEdgeToStartState) {
  PatternVariableId a_var(0);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, state3));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state3);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, ValidateFailureEdgeOutOfFinalState) {
  PatternVariableId a_var(0);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));
  NFAState state1 = nfa->NewState();
  NFAState state2 = nfa->NewState();
  NFAState state3 = nfa->NewState();

  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state1, a_var, state2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state2, state3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(state3, state3));

  nfa->SetAsStartState(state1);
  nfa->SetAsFinalState(state3);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForMatching),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(NFATest, DanglingFinalState) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, PatternVariableId(0), n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, PatternVariableId(0), n2));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n3);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unreachable states detected: N3")));
  GOOGLESQL_EXPECT_OK(nfa->Validate(NFA::ValidationMode::kForMatching));
}

TEST(NFATest, DanglingNonFinalStates) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  NFAState n4 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, PatternVariableId(0), n3));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, PatternVariableId(0), n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n3, n4));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n4);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unreachable states detected: N1, N2")));
  GOOGLESQL_EXPECT_OK(nfa->Validate(NFA::ValidationMode::kForMatching));
}

TEST(NFATest, CycleWithNoExit) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, PatternVariableId(0), n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, PatternVariableId(0), n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, PatternVariableId(0), n1));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n3);

  EXPECT_THAT(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unreachable states detected: N3")));
  GOOGLESQL_EXPECT_OK(nfa->Validate(NFA::ValidationMode::kForMatching));
}

TEST(NFATest, CycleWithExit) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));

  NFAState n0 = nfa->NewState();
  NFAState n1 = nfa->NewState();
  NFAState n2 = nfa->NewState();
  NFAState n3 = nfa->NewState();
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n0, PatternVariableId(0), n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n1, PatternVariableId(0), n2));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, PatternVariableId(0), n1));
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(n2, n3));
  nfa->SetAsStartState(n0);
  nfa->SetAsFinalState(n3);

  GOOGLESQL_EXPECT_OK(nfa->Validate(NFA::ValidationMode::kForEpsilonRemover));
  GOOGLESQL_EXPECT_OK(nfa->Validate(NFA::ValidationMode::kForMatching));
}

}  // namespace
}  // namespace googlesql::functions::match_recognize
