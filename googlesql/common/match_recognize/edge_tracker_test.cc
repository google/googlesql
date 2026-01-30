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

#include "googlesql/common/match_recognize/edge_tracker.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/edge_matchers.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/common/match_recognize/row_edge_list.h"
#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql::functions::match_recognize {
namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;

class EdgeTrackerTest : public testing::Test {
 public:
  void SetUp() override {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(2));
    start_ = nfa->NewState();
    s0_ = nfa->NewState();
    s1_ = nfa->NewState();
    s2_ = nfa->NewState();
    end_ = nfa->NewState();
    a_var_ = PatternVariableId(0);
    b_var_ = PatternVariableId(1);

    GOOGLESQL_ASSERT_OK(nfa->AddEdge(start_, a_var_, s0_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s0_, a_var_, s1_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s1_, b_var_, s2_));
    GOOGLESQL_ASSERT_OK(nfa->AddEdge(s2_, end_));
    nfa->SetAsStartState(start_);
    nfa->SetAsFinalState(end_);
    GOOGLESQL_ASSERT_OK(nfa->Validate(NFA::ValidationMode::kForMatching));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(nfa_, CompiledNFA::Create(*nfa));
    tracker_ = std::make_unique<EdgeTracker>(nfa_.get());
  }

 protected:
  std::unique_ptr<const RowEdgeList> ProcessRow(
      std::initializer_list<PatternVariableId> vars) {
    return ProcessRow(*tracker_, vars);
  }

  static std::unique_ptr<const RowEdgeList> ProcessRow(
      EdgeTracker& tracker, std::initializer_list<PatternVariableId> vars,
      bool disallow_new_match = false) {
    return tracker.ProcessRow(
        [vars](const Edge& edge) -> bool {
          if (edge.consumed == std::nullopt) {
            return true;
          }
          for (PatternVariableId v : vars) {
            if (*edge.consumed == v) {
              return true;
            }
          }
          return false;
        },
        disallow_new_match);
  }

  static std::vector<Edge> GetMarkedEdges(const CompiledNFA& nfa,
                                          const RowEdgeList& row_edge_list,
                                          int row_number) {
    std::vector<Edge> result;
    for (int i = 0; i < row_edge_list.num_edges(); ++i) {
      if (row_edge_list.IsMarked(row_number, i)) {
        result.push_back(nfa.GetEdge(i));
      }
    }
    return result;
  }

  std::vector<Edge> GetMarkedEdges(const RowEdgeList& row_edge_list,
                                   int row_number) {
    return GetMarkedEdges(*nfa_, row_edge_list, row_number);
  }

  std::unique_ptr<CompiledNFA> nfa_;
  NFAState start_;
  NFAState s0_;
  NFAState s1_;
  NFAState s2_;
  NFAState end_;
  PatternVariableId a_var_;
  PatternVariableId b_var_;

  std::unique_ptr<EdgeTracker> tracker_;
};

TEST_F(EdgeTrackerTest, SingleMatch) {
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({b_var_}), IsNull());
  std::unique_ptr<const RowEdgeList> result = ProcessRow({});

  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 5);
  EXPECT_THAT(GetMarkedEdges(*result, 0), IsEmpty());
  EXPECT_THAT(GetMarkedEdges(*result, 1),
              ElementsAre(IsEdge(0, start_, s0_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 2),
              ElementsAre(IsEdge(1, s0_, s1_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 3),
              ElementsAre(IsEdge(2, s1_, s2_, b_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 4),
              ElementsAre(IsEdge(3, s2_, end_, std::nullopt)));
}

TEST_F(EdgeTrackerTest, OverlappingMatches) {
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_, b_var_}), IsNull());
  EXPECT_THAT(ProcessRow({b_var_}), IsNull());
  std::unique_ptr<const RowEdgeList> result = ProcessRow({});

  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 5);
  EXPECT_THAT(GetMarkedEdges(*result, 0),
              ElementsAre(IsEdge(0, start_, s0_, a_var_)));
  EXPECT_THAT(
      GetMarkedEdges(*result, 1),
      ElementsAre(IsEdge(0, start_, s0_, a_var_), IsEdge(1, s0_, s1_, a_var_)));
  EXPECT_THAT(
      GetMarkedEdges(*result, 2),
      ElementsAre(IsEdge(1, s0_, s1_, a_var_), IsEdge(2, s1_, s2_, b_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 3),
              ElementsAre(IsEdge(2, s1_, s2_, b_var_),
                          IsEdge(3, s2_, end_, std::nullopt)));
  EXPECT_THAT(GetMarkedEdges(*result, 4),
              ElementsAre(IsEdge(3, s2_, end_, std::nullopt)));
}

TEST_F(EdgeTrackerTest, BackToBackMatches) {
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({b_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({b_var_}), IsNull());
  std::unique_ptr<const RowEdgeList> result = ProcessRow({});

  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 7);
  // First match
  EXPECT_THAT(GetMarkedEdges(*result, 0),
              ElementsAre(IsEdge(0, start_, s0_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 1),
              ElementsAre(IsEdge(1, s0_, s1_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 2),
              ElementsAre(IsEdge(2, s1_, s2_, b_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 3),
              ElementsAre(IsEdge(0, start_, s0_, a_var_),
                          IsEdge(3, s2_, end_, std::nullopt)));
  EXPECT_THAT(GetMarkedEdges(*result, 4),
              ElementsAre(IsEdge(1, s0_, s1_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 5),
              ElementsAre(IsEdge(2, s1_, s2_, b_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 6),
              ElementsAre(IsEdge(3, s2_, end_, std::nullopt)));
}

TEST_F(EdgeTrackerTest, MatchAfterPreviouslyReportedMatch) {
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({b_var_}), IsNull());
  EXPECT_THAT(ProcessRow({}), NotNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({a_var_}), IsNull());
  EXPECT_THAT(ProcessRow({b_var_}), IsNull());
  std::unique_ptr<const RowEdgeList> result = ProcessRow({});

  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 4);
  EXPECT_THAT(GetMarkedEdges(*result, 0),
              ElementsAre(IsEdge(0, start_, s0_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 1),
              ElementsAre(IsEdge(1, s0_, s1_, a_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 2),
              ElementsAre(IsEdge(2, s1_, s2_, b_var_)));
  EXPECT_THAT(GetMarkedEdges(*result, 3),
              ElementsAre(IsEdge(3, s2_, end_, std::nullopt)));
}

TEST_F(EdgeTrackerTest, EmptyMatch) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<NFA> nfa, NFA::Create(1));
  NFAState start = nfa->NewState();
  NFAState end = nfa->NewState();
  nfa->SetAsStartState(start);
  nfa->SetAsFinalState(end);
  GOOGLESQL_ASSERT_OK(nfa->AddEdge(start, end));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<CompiledNFA> compiled_nfa,
                       CompiledNFA::Create(*nfa));
  EdgeTracker tracker(compiled_nfa.get());

  // First empty match
  std::unique_ptr<const RowEdgeList> result = ProcessRow(tracker, {});
  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 1);
  EXPECT_THAT(GetMarkedEdges(*compiled_nfa, *result, 0),
              ElementsAre(IsEdge(0, start, end, std::nullopt)));

  // Second empty match
  result = ProcessRow(tracker, {});
  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 1);
  EXPECT_THAT(GetMarkedEdges(*compiled_nfa, *result, 0),
              ElementsAre(IsEdge(0, start, end, std::nullopt)));

  // Second empty match
  result = ProcessRow(tracker, {});
  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 1);
  EXPECT_THAT(GetMarkedEdges(*compiled_nfa, *result, 0),
              ElementsAre(IsEdge(0, start, end, std::nullopt)));

  // When disallowing a new match, we should not get the empty match at the end.
  result = ProcessRow(tracker, {}, /*disallow_new_match=*/true);
  ASSERT_THAT(result, NotNull());
  EXPECT_EQ(result->num_rows(), 1);
  EXPECT_THAT(GetMarkedEdges(*compiled_nfa, *result, 0), IsEmpty());
}

}  // namespace
}  // namespace googlesql::functions::match_recognize
