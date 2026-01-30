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

#include "googlesql/common/match_recognize/nfa_builder.h"

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/common/match_recognize/nfa_matchers.h"
#include "googlesql/common/match_recognize/test_pattern_resolver.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace googlesql::functions::match_recognize {
namespace {

using ::absl_testing::StatusIs;

class NFABuilderTest : public testing::Test {
 protected:
  TestPatternResolver resolver_;
};

TEST_F(NFABuilderTest, BasicSymbol) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, ConcatTwoSymbols) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A B"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(1)}),
                        State("N2", {Edge("N3")}),
                        State("N3", {}),
                    })));
}

TEST_F(NFABuilderTest, ConcatSymbolWithItselfInDifferentCase) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A a"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3")}),
                        State("N3", {}),
                    })));
}

TEST_F(NFABuilderTest, ConcatThreeSymbols) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A B A"));
  std::vector<std::string> vars = {"A", "B"};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(1)}),
                        State("N2", {Edge("N3").On(0)}),
                        State("N3", {Edge("N4")}),
                        State("N4", {}),
                    })));
}

TEST_F(NFABuilderTest, NestedConcat) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A (B C)"));
  std::vector<std::string> vars = {"A", "B", "C"};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(1)}),
                        State("N2", {Edge("N3").On(2)}),
                        State("N3", {Edge("N4")}),
                        State("N4", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternativeOfTwoSymbols) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A|B"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0), Edge("N2").On(1)}),
                        State("N1", {Edge("N3")}),
                        State("N2", {Edge("N3")}),
                        State("N3", {}),
                    })));
}
TEST_F(NFABuilderTest, AlternativeOfThreeSymbols) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A|B|C"));
  std::vector<std::string> vars = {"A", "B", "C"};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(
      *nfa,
      EquivToGraph(Graph({{
          State("N0", {Edge("N1").On(0), Edge("N2").On(1), Edge("N3").On(2)}),
          State("N1", {Edge("N4")}),
          State("N2", {Edge("N4")}),
          State("N3", {Edge("N4")}),
          State("N4", {}),
      }})));
}
TEST_F(NFABuilderTest, AlternativeSymbolAndConcatCombo) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(A|(B C)) (A|C)"));
  std::vector<std::string> vars = {"A", "B", "C"};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0), Edge("N4").On(1)}),
                        State("N1", {Edge("N2").On(0), Edge("N3").On(2)}),
                        State("N2", {Edge("N6")}),
                        State("N3", {Edge("N6")}),
                        State("N4", {Edge("N5").On(2)}),
                        State("N5", {Edge("N2").On(0), Edge("N3").On(2)}),
                        State("N6", {}),
                    })));
}

TEST_F(NFABuilderTest, EmptyConcatSymbol) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("() A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, EmptyConcatEmptyConcatSymbol) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("() () A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, SymbolConcatEmpty) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A ()"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, SymbolOrEmpty) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A | "));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0), Edge("N2")}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, EmptyOrSymbol) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("| A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N2"), Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, ComplexWithEmpty) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A | () | (B C ())|"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(0), Edge("N4"), Edge("N2").On(1)}),
                  State("N1", {Edge("N4")}),
                  State("N2", {Edge("N3").On(2)}),
                  State("N3", {Edge("N4")}),
                  State("N4", {}),
              })));
}

TEST_F(NFABuilderTest, GreedyQuestionMark) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0), Edge("N2")}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}
TEST_F(NFABuilderTest, ReluctantQuestionMark) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A??"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N2"), Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, RepZeroToFourGreedy) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{0,4}"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0), Edge("N5")}),
                        State("N1", {Edge("N2").On(0), Edge("N5")}),
                        State("N2", {Edge("N3").On(0), Edge("N5")}),
                        State("N3", {Edge("N4").On(0), Edge("N5")}),
                        State("N4", {Edge("N5")}),
                        State("N5", {}),
                    })));
}

TEST_F(NFABuilderTest, RepZeroToFourReluctant) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{0,4}?"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N5"), Edge("N1").On(0)}),
                        State("N1", {Edge("N5"), Edge("N2").On(0)}),
                        State("N2", {Edge("N5"), Edge("N3").On(0)}),
                        State("N3", {Edge("N5"), Edge("N4").On(0)}),
                        State("N4", {Edge("N5")}),
                        State("N5", {}),
                    })));
}

TEST_F(NFABuilderTest, RepZeroGreedy) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{0,0}"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1")}),
                        State("N1", {}),
                    })));
}

TEST_F(NFABuilderTest, RepZeroReluctant) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{0,0}?"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1")}),
                        State("N1", {}),
                    })));
}

TEST_F(NFABuilderTest, QuantifierBoundExceedsInt32Max) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{4294967296}"));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, PatternTooComplex) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{1000000}"));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, PatternTooComplex2) {
  // Equivalent to A{,1000000}, but written in a way so that no quantifier bound
  // individually exceeds 10.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("(((((A{,10}){,10}){,10}){,10}){,10}){,10}"));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, RepTwoToFiveGreedy) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{2,5}"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3").On(0), Edge("N6")}),
                        State("N3", {Edge("N4").On(0), Edge("N6")}),
                        State("N4", {Edge("N5").On(0), Edge("N6")}),
                        State("N5", {Edge("N6")}),
                        State("N6", {}),
                    })));
}

TEST_F(NFABuilderTest, RepTwoToFiveReluctant) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{2,5}?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N6"), Edge("N3").On(0)}),
                        State("N3", {Edge("N6"), Edge("N4").On(0)}),
                        State("N4", {Edge("N6"), Edge("N5").On(0)}),
                        State("N5", {Edge("N6")}),
                        State("N6", {}),
                    })));
}

TEST_F(NFABuilderTest, RepThreeExact) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{3}"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3").On(0)}),
                        State("N3", {Edge("N4")}),
                        State("N4", {}),
                    })));
}

TEST_F(NFABuilderTest, GreedyStar) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A*"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0), Edge("N2")}),
                        State("N1", {Edge("N1").On(0), Edge("N2")}),
                        State("N2", {}),
                    })));
}
TEST_F(NFABuilderTest, ReluctantStar) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A*?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N2"), Edge("N1").On(0)}),
                        State("N1", {Edge("N2"), Edge("N1").On(0)}),
                        State("N2", {}),
                    })));
}
TEST_F(NFABuilderTest, GreedyPlus) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A+"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N1").On(0), Edge("N2")}),
                        State("N2", {}),
                    })));
}
TEST_F(NFABuilderTest, ReluctantPlus) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A+?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2"), Edge("N1").On(0)}),
                        State("N2", {}),
                    })));
}
TEST_F(NFABuilderTest, GreedyTwoOrMore) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{2,}"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N2").On(0), Edge("N3")}),
                        State("N3", {}),
                    })));
}
TEST_F(NFABuilderTest, ReluctantTwoOrMore) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A{2,}?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3"), Edge("N2").On(0)}),
                        State("N3", {}),
                    })));
}

TEST_F(NFABuilderTest, NamedQueryParameterInQuantifierBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{@lower_bound, @upper_bound}",
                               {.parameters = QueryParametersMap{
                                    {"lower_bound", types::Int64Type()},
                                    {"upper_bound", types::Int64Type()}}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const NFA> nfa,
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            absl::string_view param_name = std::get<absl::string_view>(param);
            if (param_name == "lower_bound") {
              return values::Int64(2);
            } else if (param_name == "upper_bound") {
              return values::Int64(4);
            } else {
              return absl::InvalidArgumentError("Unexpected param name");
            }
          }));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3").On(0), Edge("N5")}),
                        State("N3", {Edge("N4").On(0), Edge("N5")}),
                        State("N4", {Edge("N5")}),
                        State("N5", {}),
                    })));
}

TEST_F(NFABuilderTest, NamedQueryParameterInQuantifierBound_OutOfRange) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{@lower_bound, @upper_bound}",
                               {.parameters = QueryParametersMap{
                                    {"lower_bound", types::Int64Type()},
                                    {"upper_bound", types::Int64Type()}}}));

  EXPECT_THAT(
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            absl::string_view param_name = std::get<absl::string_view>(param);
            if (param_name == "lower_bound") {
              return values::Int64(2);
            } else if (param_name == "upper_bound") {
              return values::Int64(kMaxSupportedQuantifierBound + 1);
            } else {
              return absl::InvalidArgumentError("Unexpected param name");
            }
          }),
      StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, PositionalQueryParameterInQuantifierBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?, ?}",
                               {.parameters = std::vector<const Type*>{
                                    types::Int64Type(), types::Int64Type()}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const NFA> nfa,
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            int param_position = std::get<int>(param);
            if (param_position == 1) {
              return values::Int64(2);
            } else if (param_position == 2) {
              return values::Int64(4);
            } else {
              return absl::InvalidArgumentError("Unexpected param position");
            }
          }));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3").On(0), Edge("N5")}),
                        State("N3", {Edge("N4").On(0), Edge("N5")}),
                        State("N4", {Edge("N5")}),
                        State("N5", {}),
                    })));
}

TEST_F(NFABuilderTest, PositionalQueryParameterInQuantifierBound_OutOfRange) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?, ?}",
                               {.parameters = std::vector<const Type*>{
                                    types::Int64Type(), types::Int64Type()}}));

  EXPECT_THAT(
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            int param_position = std::get<int>(param);
            if (param_position == 1) {
              return values::Int64(2);
            } else if (param_position == 2) {
              return values::Int64(kMaxSupportedQuantifierBound + 1);
            } else {
              return absl::InvalidArgumentError("Unexpected param position");
            }
          }),
      StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, QueryParametersUnavailable) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{@foo}", {.parameters = QueryParametersMap{
                                               {"foo", types::Int64Type()}}}));

  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kInternal));
}
TEST_F(NFABuilderTest, NullAsQuantifierBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?}", {.parameters = std::vector<const Type*>{
                                            types::Int64Type()}}));

  EXPECT_THAT(
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            if (std::get<int>(param) == 1) {
              return values::NullInt64();
            }
            return absl::InvalidArgumentError("Unexpected param position");
          }),
      StatusIs(absl::StatusCode::kOutOfRange));
}
TEST_F(NFABuilderTest, NegativeQuantifierBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?}", {.parameters = std::vector<const Type*>{
                                            types::Int64Type()}}));

  EXPECT_THAT(
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            if (std::get<int>(param) == 1) {
              return values::Int64(-1);
            }
            return absl::InvalidArgumentError("Unexpected param position");
          }),
      StatusIs(absl::StatusCode::kOutOfRange));
}
TEST_F(NFABuilderTest, QuantifierLowerBoundBoundGreaterThanUpperBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?, ?}",
                               {.parameters = std::vector<const Type*>{
                                    types::Int64Type(), types::Int64Type()}}));

  EXPECT_THAT(
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            if (std::get<int>(param) == 1) {
              return values::Int64(2);
            } else if (std::get<int>(param) == 2) {
              return values::Int64(1);
            }
            return absl::InvalidArgumentError("Unexpected param position");
          }),
      StatusIs(absl::StatusCode::kOutOfRange));
}
TEST_F(NFABuilderTest, Int32QueryParameterAsQuantifierBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("A{?}", {.parameters = std::vector<const Type*>{
                                            types::Int32Type()}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const NFA> nfa,
      NFABuilder::BuildNFAForPattern(
          *scan,
          [](std::variant<int, absl::string_view> param)
              -> absl::StatusOr<Value> {
            if (std::get<int>(param) == 1) {
              return values::Int64(2);
            } else {
              return absl::InvalidArgumentError("Unexpected param position");
            }
          }));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").On(0)}),
                        State("N2", {Edge("N3")}),
                        State("N3", {}),
                    })));
}

TEST_F(NFABuilderTest, HeadAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("^A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, TailAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("A$"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").WithTailAnchor()}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, HeadAndTailAnchorSameSpot) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(^)($)A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {}),
                        State("N1", {}),
                    })));
}

TEST_F(NFABuilderTest, OptionalHeadAnchor) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(^)?A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, ComplexWithAnchors) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(^|$)A (^|$|) B ($|^)"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                        State("N1", {Edge("N2").On(1)}),
                        State("N2", {Edge("N3").WithTailAnchor()}),
                        State("N3", {}),
                    })));
}

TEST_F(NFABuilderTest, AnchorInQuantifier) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(^A B)*"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));

  // Note: Even though the quantifier is "*", the "^" makes it impossible to
  // repeat more than once, so "*" behaves like "?".
  EXPECT_THAT(*nfa,
              EquivToGraph(Graph({
                  State("N0", {Edge("N1").On(0).WithHeadAnchor(), Edge("N3")}),
                  State("N1", {Edge("N2").On(1)}),
                  State("N2", {Edge("N3")}),
                  State("N3", {}),
              })));
}

TEST_F(NFABuilderTest, AlternationOfAnchorsInQuantifier) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(^|^|$|$)+ A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationOfAnchorsInQuantifier2) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("( () (^|^|$|$))+ A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationOfAnchorsInQuantifier3) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("( () A (^|^|$|$))+"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").WithTailAnchor()}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationOfAnchorsInNestedPlusQuantifier) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("((^|^|$|$)+)+ A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0).WithHeadAnchor()}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationOfAnchorsInNestedStarQuantifier) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("((^|^|$|$)+)* A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, SimpleAnchorInNestedStarQuantifier) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("((^)+)* A"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AnchorInNestedStarQuantifier2) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("((^ (A?))+)* B"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(
      *nfa,
      EquivToGraph(Graph({
          State("N0", {Edge("N1").On(0).WithHeadAnchor(), Edge("N2").On(1)}),
          State("N1", {Edge("N2").On(1)}),
          State("N2", {Edge("N3")}),
          State("N3", {}),
      })));
}

TEST_F(NFABuilderTest, AnchorInNestedStarQuantifier3) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("((^ (A??))+?)*? B"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(
      *nfa,
      EquivToGraph(Graph({
          State("N0", {Edge("N1").On(1), Edge("N2").On(0).WithHeadAnchor()}),
          State("N1", {Edge("N3")}),
          State("N2", {Edge("N1").On(1)}),
          State("N3", {}),
      })));
}

TEST_F(NFABuilderTest, AnchorInBoundedQuantifier_SmallBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("B (($){,3})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AnchorInBoundedQuantifier_LargeBound) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("B (($){,2000})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AnchorInBoundedQuantifier_LargeBound2) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("B (($){2,2000})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").WithTailAnchor()}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AnchorInBoundedQuantifier_LargeBound3) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("B (($){1500,2000})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N1").On(0)}),
                        State("N1", {Edge("N2").WithTailAnchor()}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationHeadAnchorAndQuestion) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("(^)|A?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N2").WithHeadAnchor(),
                                     Edge("N1").On(0), Edge("N2")}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationTailAnchorAndQuestion) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("($)|A?"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa, EquivToGraph(Graph({
                        State("N0", {Edge("N2").WithTailAnchor(),
                                     Edge("N1").On(0), Edge("N2")}),
                        State("N1", {Edge("N2")}),
                        State("N2", {}),
                    })));
}

TEST_F(NFABuilderTest, AlternationHeadAndTailAnchorAndQuestion) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern("^|B$|(A?)"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const NFA> nfa,
                       NFABuilder::BuildNFAForPattern(*scan));
  EXPECT_THAT(*nfa,
              EquivToGraph(Graph({
                  State("N0", {Edge("N3").WithHeadAnchor(), Edge("N1").On(1),
                               Edge("N2").On(0), Edge("N3")}),
                  State("N1", {Edge("N3").WithTailAnchor()}),
                  State("N2", {Edge("N3")}),
                  State("N3", {}),
              })));
}

TEST_F(NFABuilderTest, PatternTooComplexInEpsilonRemover) {
  // This pattern has a reasonable-sized graph pre-epsilon-removal, but the
  // number of edges gets too large during epsilon-removal. Make sure it gets
  // rejected.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedMatchRecognizeScan* scan,
      resolver_.ResolvePattern("(((A|B|C|D|E|F|){,50}){,10})"));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, MaxSupportedQuantifierBound_Exact_Works) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{$0}", kMaxSupportedQuantifierBound)));
  GOOGLESQL_EXPECT_OK(NFABuilder::BuildNFAForPattern(*scan));
}

TEST_F(NFABuilderTest, MaxSupportedQuantifierBoundPlusOne_Exact_Fails) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{$0}", kMaxSupportedQuantifierBound + 1)));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, MaxSupportedQuantifierBound_Lower_Works) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{$0,}", kMaxSupportedQuantifierBound)));
  GOOGLESQL_EXPECT_OK(NFABuilder::BuildNFAForPattern(*scan));
}

TEST_F(NFABuilderTest, MaxSupportedQuantifierBoundPlusOne_Lower_Fails) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{$0,}", kMaxSupportedQuantifierBound + 1)));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest, MaxSupportedQuantifierBound_Upper_Works) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{,$0}", kMaxSupportedQuantifierBound)));
  GOOGLESQL_EXPECT_OK(NFABuilder::BuildNFAForPattern(*scan));
}

TEST_F(NFABuilderTest, MaxSupportedQuantifierBoundPlusOne_Upper_Fails) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{,$0}", kMaxSupportedQuantifierBound + 1)));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(NFABuilderTest,
       MaxSupportedQuantifierBound_UpperWithLowerBoundPresent_Works) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{$0,$1}", kMaxSupportedQuantifierBound - 1,
                           kMaxSupportedQuantifierBound)));
  GOOGLESQL_EXPECT_OK(NFABuilder::BuildNFAForPattern(*scan));
}

TEST_F(NFABuilderTest,
       MaxSupportedQuantifierBoundPlusOne_UpperWithLowerBoundPresent_Fails) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedMatchRecognizeScan* scan,
                       resolver_.ResolvePattern(absl::Substitute(
                           "A{$0, $1}", kMaxSupportedQuantifierBound,
                           kMaxSupportedQuantifierBound + 1)));
  EXPECT_THAT(NFABuilder::BuildNFAForPattern(*scan),
              StatusIs(absl::StatusCode::kOutOfRange));
}

}  // namespace
}  // namespace googlesql::functions::match_recognize
