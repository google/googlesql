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

#include "googlesql/common/match_recognize/row_edge_list.h"

#include <memory>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/edge_matchers.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/base/testing/status_matchers.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace googlesql::functions::match_recognize {
namespace {
using ::testing::IsNull;
using ::testing::Pointee;
using ::testing::Test;

class RowEdgeListTest : public Test {
 public:
  void SetUp() override {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
    start_ = nfa->NewState();
    s0_ = nfa->NewState();
    s1_ = nfa->NewState();
    s2_ = nfa->NewState();
    s3_ = nfa->NewState();
    end_ = nfa->NewState();
    a_var_ = PatternVariableId(0);
    b_var_ = PatternVariableId(1);

    GOOGLESQL_ASSERT_OK(nfa->AddEdge(start_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s0_, a_var_, s1_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s0_, b_var_, s2_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s1_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s2_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s2_, a_var_, s3_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s3_, end_));
    nfa->SetAsStartState(start_);
    nfa->SetAsFinalState(end_);
    GOOGLESQL_ASSERT_OK(nfa->Validate(NFA::ValidationMode::kForMatching));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(nfa_, CompiledNFA::Create(*nfa));
  }

 protected:
  std::unique_ptr<CompiledNFA> nfa_;
  NFAState start_;
  NFAState s0_;
  NFAState s1_;
  NFAState s2_;
  NFAState s3_;
  NFAState end_;
  PatternVariableId a_var_;
  PatternVariableId b_var_;
};

TEST_F(RowEdgeListTest, Empty) {
  RowEdgeList edge_list(nfa_.get());

  EXPECT_EQ(edge_list.num_rows(), 0);

  // Make sure that DebugString() doesn't crash.
  ABSL_LOG(INFO) << "Debug string:\n" << edge_list.DebugString();
}

TEST_F(RowEdgeListTest, OneRowAllUnmarked) {
  RowEdgeList edge_list(nfa_.get());

  ASSERT_EQ(edge_list.AddRow(), 0);

  EXPECT_EQ(edge_list.num_rows(), 1);
  for (int i = 0; i < edge_list.num_edges(); ++i) {
    EXPECT_FALSE(edge_list.IsMarked(0, i));
  }
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, start_), IsNull());

  // Make sure that DebugString() doesn't crash.
  ABSL_LOG(INFO) << "Debug string:\n" << edge_list.DebugString();
}

TEST_F(RowEdgeListTest, OneRowWithMarking) {
  RowEdgeList edge_list(nfa_.get());

  ASSERT_EQ(edge_list.AddRow(), 0);

  // Mark two edges in the row.
  edge_list.MarkEdge(0, 1);
  edge_list.MarkEdge(0, 2);

  EXPECT_EQ(edge_list.num_rows(), 1);

  EXPECT_FALSE(edge_list.IsMarked(0, 0));
  EXPECT_TRUE(edge_list.IsMarked(0, 1));
  EXPECT_TRUE(edge_list.IsMarked(0, 2));
  EXPECT_FALSE(edge_list.IsMarked(0, 3));
  EXPECT_FALSE(edge_list.IsMarked(0, 4));
  EXPECT_FALSE(edge_list.IsMarked(0, 5));
  EXPECT_FALSE(edge_list.IsMarked(0, 6));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s0_),
              Pointee(IsEdge(1, s0_, s1_, a_var_)));

  // Unmark one of the edges.
  edge_list.UnmarkEdge(0, 1);
  EXPECT_FALSE(edge_list.IsMarked(0, 0));
  EXPECT_FALSE(edge_list.IsMarked(0, 1));
  EXPECT_TRUE(edge_list.IsMarked(0, 2));
  EXPECT_FALSE(edge_list.IsMarked(0, 3));
  EXPECT_FALSE(edge_list.IsMarked(0, 4));
  EXPECT_FALSE(edge_list.IsMarked(0, 5));
  EXPECT_FALSE(edge_list.IsMarked(0, 6));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s0_),
              Pointee(IsEdge(2, s0_, s2_, b_var_)));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s1_), IsNull());

  // Mark an edge that's already marked; shouldn't change anything.
  edge_list.MarkEdge(0, 2);
  EXPECT_FALSE(edge_list.IsMarked(0, 0));
  EXPECT_FALSE(edge_list.IsMarked(0, 1));
  EXPECT_TRUE(edge_list.IsMarked(0, 2));
  EXPECT_FALSE(edge_list.IsMarked(0, 3));
  EXPECT_FALSE(edge_list.IsMarked(0, 4));
  EXPECT_FALSE(edge_list.IsMarked(0, 5));
  EXPECT_FALSE(edge_list.IsMarked(0, 6));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s0_),
              Pointee(IsEdge(2, s0_, s2_, b_var_)));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s1_), IsNull());

  // Unmark an edge that's already unmarked; shouldn't change anything.
  edge_list.UnmarkEdge(0, 1);
  EXPECT_FALSE(edge_list.IsMarked(0, 0));
  EXPECT_FALSE(edge_list.IsMarked(0, 1));
  EXPECT_TRUE(edge_list.IsMarked(0, 2));
  EXPECT_FALSE(edge_list.IsMarked(0, 3));
  EXPECT_FALSE(edge_list.IsMarked(0, 4));
  EXPECT_FALSE(edge_list.IsMarked(0, 5));
  EXPECT_FALSE(edge_list.IsMarked(0, 6));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s0_),
              Pointee(IsEdge(2, s0_, s2_, b_var_)));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s1_), IsNull());
}

TEST_F(RowEdgeListTest, TwoRows) {
  RowEdgeList edge_list(nfa_.get());

  ASSERT_EQ(edge_list.AddRow(), 0);
  ASSERT_EQ(edge_list.AddRow(), 1);

  edge_list.MarkEdge(0, 1);
  edge_list.MarkEdge(1, 2);

  EXPECT_EQ(edge_list.num_rows(), 2);

  EXPECT_FALSE(edge_list.IsMarked(0, 0));
  EXPECT_TRUE(edge_list.IsMarked(0, 1));
  EXPECT_FALSE(edge_list.IsMarked(0, 2));
  EXPECT_FALSE(edge_list.IsMarked(0, 3));
  EXPECT_FALSE(edge_list.IsMarked(0, 4));
  EXPECT_FALSE(edge_list.IsMarked(0, 5));
  EXPECT_FALSE(edge_list.IsMarked(0, 6));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(0, s0_),
              Pointee(IsEdge(1, s0_, s1_, a_var_)));

  EXPECT_FALSE(edge_list.IsMarked(1, 0));
  EXPECT_FALSE(edge_list.IsMarked(1, 1));
  EXPECT_TRUE(edge_list.IsMarked(1, 2));
  EXPECT_FALSE(edge_list.IsMarked(1, 3));
  EXPECT_FALSE(edge_list.IsMarked(1, 4));
  EXPECT_FALSE(edge_list.IsMarked(1, 5));
  EXPECT_FALSE(edge_list.IsMarked(1, 6));
  EXPECT_THAT(edge_list.GetHighestPrecedenceMarkedEdge(1, s0_),
              Pointee(IsEdge(2, s0_, s2_, b_var_)));

  // Make sure that DebugString() doesn't crash.
  ABSL_LOG(INFO) << "Debug string:\n" << edge_list.DebugString();
}

TEST_F(RowEdgeListTest, ClearRows) {
  RowEdgeList edge_list(nfa_.get());

  ASSERT_EQ(edge_list.AddRow(), 0);
  ASSERT_EQ(edge_list.AddRow(), 1);
  edge_list.MarkEdge(0, 1);
  edge_list.MarkEdge(1, 2);
  edge_list.ClearRows();

  EXPECT_EQ(edge_list.num_rows(), 0);
}

TEST_F(RowEdgeListTest, MarkAndUnmarkAllEdgesAllRows) {
  RowEdgeList edge_list(nfa_.get());

  int num_rows = 2;
  for (int i = 0; i < num_rows; ++i) {
    edge_list.AddRow();
    for (int j = 0; j < edge_list.num_edges(); ++j) {
      edge_list.MarkEdge(i, j);
    }
    for (int j = 0; j < edge_list.num_edges(); ++j) {
      EXPECT_TRUE(edge_list.IsMarked(i, j));
    }
    for (int j = 0; j < edge_list.num_edges(); ++j) {
      edge_list.UnmarkEdge(i, j);
    }
    for (int j = 0; j < edge_list.num_edges(); ++j) {
      EXPECT_FALSE(edge_list.IsMarked(i, j));
    }
  }
}
}  // namespace
}  // namespace googlesql::functions::match_recognize
