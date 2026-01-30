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

#include "googlesql/base/flat_internal.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql_base {
namespace internal_flat {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

TEST(FlatInternalTest, VerifyHintForEmptyArray) {
  const std::vector<int> rep;
  const std::less<int> cmp;
  // Any hint in an empty array is perfect.
  EXPECT_THAT(verify_hint(rep, rep.end(), 1, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.end()));
}

TEST(FlatInternalTest, VerifyPerfectHint) {
  const std::vector<int> rep = {2, 4, 6};
  const std::less<int> cmp;
  // Trying perfect begin/end hints.
  EXPECT_THAT(verify_hint(rep, rep.begin(), 1, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.begin()));
  EXPECT_THAT(verify_hint(rep, rep.end(), 7, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.end()));
  // Trying perfect non-begin/end hints.
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 3, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.begin() + 1));
  EXPECT_THAT(verify_hint(rep, rep.begin() + 2, 5, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.begin() + 2));
  // Not testing duplicates here, as there could be no perfect hint for them.
}

TEST(FlatInternalTest, VerifyBadHint) {
  const std::vector<int> rep = {2, 4, 6};
  const std::less<int> cmp;
  // Trying bad begin/end hints for non-duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin(), 3, cmp).first,
              VerifyHintResult::kBadHint);
  EXPECT_THAT(verify_hint(rep, rep.end(), 1, cmp).first,
              VerifyHintResult::kBadHint);
  // Trying bad begin/end hints for duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin(), 2, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.begin()));
  EXPECT_THAT(verify_hint(rep, rep.end(), 6, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.end() - 1));
  // Trying bad non-begin/end hints for non-duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 5, cmp).first,
              VerifyHintResult::kBadHint);
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 1, cmp).first,
              VerifyHintResult::kBadHint);
  // Trying bad non-begin/end hints for duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 6, cmp).first,
              VerifyHintResult::kBadHint);
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 4, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.begin() + 1));
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 2, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.begin()));
}

TEST(FlatInternalTest, MultiInsertUniqueWithPerfectHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep;
  // Any hint in an empty array is perfect.
  multi_insert_hint(&rep, rep.end(), std::make_pair(1, 1), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1)));
  // Insert unique item with hint.
  multi_insert_hint(&rep, rep.end(), std::make_pair(2, 2), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1), Pair(2, 2)));
  // Insert unique item with hint.
  multi_insert_hint(&rep, rep.begin(), std::make_pair(0, 0), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 1), Pair(2, 2)));
}

TEST(FlatInternalTest, MultiInsertDuplicatesWithPerfectHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep = {{1, 1}};
  // Insert a duplicate before.
  multi_insert_hint(&rep, rep.begin(), std::make_pair(1, 2), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 2), Pair(1, 1)));
  // Insert a duplicate after.
  multi_insert_hint(&rep, rep.end(), std::make_pair(1, 3), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 2), Pair(1, 1), Pair(1, 3)));
  // Insert a duplicate in the middle.
  multi_insert_hint(&rep, rep.begin() + 1, std::make_pair(1, 4), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 2), Pair(1, 4), Pair(1, 1), Pair(1, 3)));
}

TEST(FlatInternalTest, MultiInsertUniqueWithBadHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep = {{1, 1}, {2, 2}};
  multi_insert_hint(&rep, rep.begin(), std::make_pair(3, 3), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3)));
  multi_insert_hint(&rep, rep.end(), std::make_pair(0, 0), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 1), Pair(2, 2), Pair(3, 3)));
}

TEST(FlatInternalTest, MultiInsertDuplicatesWithBadHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep = {{0, 0}, {1, 1}, {2, 2}};
  // Insert a duplicate with a hint before a correct one. Insert at lower bound.
  multi_insert_hint(&rep, rep.begin(), std::make_pair(1, 2), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 2), Pair(1, 1), Pair(2, 2)));
  // Insert a duplicate with a hint after a correct one. Insert at upper bound.
  multi_insert_hint(&rep, rep.end(), std::make_pair(1, 0), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 2), Pair(1, 1), Pair(1, 0),
                               Pair(2, 2)));
}

}  // namespace
}  // namespace internal_flat
}  // namespace googlesql_base
