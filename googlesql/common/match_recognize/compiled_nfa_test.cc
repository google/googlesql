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

#include "googlesql/common/match_recognize/edge_matchers.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.pb.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql::functions::match_recognize {
namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Test;
using ::absl_testing::StatusIs;

class CompiledNFATest : public Test {
 public:
  void SetUp() override {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(nfa_, NFA::Create(2));
    start_ = nfa_->NewState();
    s0_ = nfa_->NewState();
    s1_ = nfa_->NewState();
    s2_ = nfa_->NewState();
    s3_ = nfa_->NewState();
    end_ = nfa_->NewState();
    a_var_ = PatternVariableId(0);
    b_var_ = PatternVariableId(1);

    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(start_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(s0_, NFAEdge(a_var_, s1_).SetHeadAnchored()));
    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(s0_, b_var_, s2_));
    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(s1_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(s2_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(s2_, a_var_, s3_));
    GOOGLESQL_ASSERT_OK(nfa_->AddEdge(s3_, NFAEdge(end_).SetTailAnchored()));
    nfa_->SetAsStartState(start_);
    nfa_->SetAsFinalState(end_);
    GOOGLESQL_ASSERT_OK(nfa_->Validate(NFA::ValidationMode::kForMatching));
  }

 protected:
  std::unique_ptr<NFA> nfa_;
  NFAState start_;
  NFAState s0_;
  NFAState s1_;
  NFAState s2_;
  NFAState s3_;
  NFAState end_;
  PatternVariableId a_var_;
  PatternVariableId b_var_;
};

// Returns a sample NFA that satisfies all required properties of an NFA
// post epsilon-removal.

TEST_F(CompiledNFATest, GetEdge) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));

  EXPECT_EQ(compiled_nfa->num_edges(), 7);
  EXPECT_THAT(compiled_nfa->GetEdge(0), IsEdge(0, start_, s0_, a_var_));
  EXPECT_THAT(compiled_nfa->GetEdge(1),
              IsHeadAnchoredEdge(1, s0_, s1_, a_var_));
  EXPECT_THAT(compiled_nfa->GetEdge(2), IsEdge(2, s0_, s2_, b_var_));
  EXPECT_THAT(compiled_nfa->GetEdge(3), IsEdge(3, s1_, s0_, a_var_));
  EXPECT_THAT(compiled_nfa->GetEdge(4), IsEdge(4, s2_, s0_, a_var_));
  EXPECT_THAT(compiled_nfa->GetEdge(5), IsEdge(5, s2_, s3_, a_var_));
  EXPECT_THAT(compiled_nfa->GetEdge(6), IsTailAnchoredEdge(6, s3_, end_));
}

TEST_F(CompiledNFATest, GetAllEdges) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));
  EXPECT_THAT(
      compiled_nfa->GetAllEdges(),
      ElementsAre(IsEdge(0, start_, s0_, a_var_),
                  IsHeadAnchoredEdge(1, s0_, s1_, a_var_),
                  IsEdge(2, s0_, s2_, b_var_), IsEdge(3, s1_, s0_, a_var_),
                  IsEdge(4, s2_, s0_, a_var_), IsEdge(5, s2_, s3_, a_var_),
                  IsTailAnchoredEdge(6, s3_, end_)));
}

TEST_F(CompiledNFATest, StartState) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));
  EXPECT_EQ(compiled_nfa->start_state(), start_);
}

TEST_F(CompiledNFATest, FinalState) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));
  EXPECT_EQ(compiled_nfa->final_state(), end_);
}

TEST_F(CompiledNFATest, NumStates) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));
  EXPECT_EQ(compiled_nfa->num_states(), 6);  // start, s0, s1, s2, s3, end.
}

TEST_F(CompiledNFATest, GetEdgesFrom) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));

  EXPECT_THAT(compiled_nfa->GetEdgesFrom(start_),
              ElementsAre(IsEdge(0, start_, s0_, a_var_)));
  EXPECT_THAT(compiled_nfa->GetEdgesFrom(s0_),
              ElementsAre(IsHeadAnchoredEdge(1, s0_, s1_, a_var_),
                          IsEdge(2, s0_, s2_, b_var_)));
  EXPECT_THAT(compiled_nfa->GetEdgesFrom(s1_),
              ElementsAre(IsEdge(3, s1_, s0_, a_var_)));
  EXPECT_THAT(
      compiled_nfa->GetEdgesFrom(s2_),
      ElementsAre(IsEdge(4, s2_, s0_, a_var_), IsEdge(5, s2_, s3_, a_var_)));
  EXPECT_THAT(compiled_nfa->GetEdgesFrom(s3_),
              ElementsAre(IsTailAnchoredEdge(6, s3_, end_)));
  EXPECT_THAT(compiled_nfa->GetEdgesFrom(end_), IsEmpty());
}

TEST_F(CompiledNFATest, SerializeRoundTrip) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));
  StateMachineProto::CompiledNFAProto proto = compiled_nfa->Serialize();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const CompiledNFA> deserialized,
                       CompiledNFA::Deserialize(proto));

  EXPECT_THAT(
      deserialized->GetAllEdges(),
      ElementsAre(IsEdge(0, start_, s0_, a_var_),
                  IsHeadAnchoredEdge(1, s0_, s1_, a_var_),
                  IsEdge(2, s0_, s2_, b_var_), IsEdge(3, s1_, s0_, a_var_),
                  IsEdge(4, s2_, s0_, a_var_), IsEdge(5, s2_, s3_, a_var_),
                  IsTailAnchoredEdge(6, s3_, end_)));
  EXPECT_EQ(deserialized->start_state(), start_);
  EXPECT_EQ(deserialized->final_state(), end_);
  EXPECT_EQ(deserialized->num_states(), 6);  // start, s0, s1, s2, s3, end.
}

TEST_F(CompiledNFATest, DeserializeInvalidProtoStartState) {
  StateMachineProto::CompiledNFAProto proto;
  proto.add_states();
  proto.set_start_state(1);  // start state out of bounds
  EXPECT_THAT(CompiledNFA::Deserialize(proto),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(CompiledNFATest, DeserializeInvalidProtoTargetStateOfEdge) {
  StateMachineProto::CompiledNFAProto proto;
  proto.add_states()->add_edges()->set_to_state(2);
  proto.add_states();
  proto.set_start_state(0);
  proto.set_final_state(1);
  EXPECT_THAT(CompiledNFA::Deserialize(proto),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(CompiledNFATest, NumPatternVariablesNonZero) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa_));
  EXPECT_EQ(compiled_nfa->num_pattern_variables(), 2);
}

TEST_F(CompiledNFATest, NumPatternVariablesAfterDeserializae) {
  StateMachineProto::CompiledNFAProto proto;

  auto* edge = proto.add_states()->add_edges();
  edge->set_to_state(1);
  edge->set_pattern_variable(0);

  edge = proto.add_states()->add_edges();
  edge->set_to_state(2);
  edge->set_pattern_variable(1);

  edge = proto.add_states()->add_edges();
  edge->set_to_state(3);

  proto.add_states();

  proto.set_start_state(0);
  proto.set_final_state(3);
  proto.set_num_pattern_variables(4);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const CompiledNFA> deserialized,
                       CompiledNFA::Deserialize(proto));

  EXPECT_EQ(deserialized->num_pattern_variables(), 4);
}

}  // namespace
}  // namespace googlesql::functions::match_recognize
