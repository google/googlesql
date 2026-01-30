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

#include "googlesql/compliance/matchers.h"

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/compliance/runtime_expected_errors.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {

TEST(ErrorMatcherTest, StatusErrorCodeMatcher) {
  StatusErrorCodeMatcher matcher(absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_FALSE(matcher.Matches(
      absl::Status(absl::StatusCode::kInternal, "answer: 42:life")));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(1, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, RegexMatcher) {
  RegexMatcher matcher("(\\d+):(\\w+)");
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches("answer: 42:life"));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_TRUE(matcher.Matches("answer: 43:happiness"));
  EXPECT_FALSE(matcher.Matches("answer: nothing:noone"));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(2, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, RegexGroupMatcher) {
  std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers;
  group_matchers.emplace(0, std::make_unique<RegexMatcher>("42"));
  group_matchers.emplace(1, std::make_unique<SubstringMatcher>("life"));
  RegexMatcher matcher("(\\d+):(\\w+)", std::move(group_matchers));
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches("answer: 42:life"));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_FALSE(matcher.Matches("answer: 43:happiness"));
  EXPECT_FALSE(matcher.Matches("answer: nothing:noone"));
  EXPECT_FALSE(matcher.Matches("answer: 42:happiness"));
  EXPECT_FALSE(matcher.Matches("answer: nothing:life"));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(1, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, RegexGroupMatcherNotWrapper) {
  std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers;
  group_matchers.emplace(0, std::make_unique<NotMatcher<std::string>>(
                                std::make_unique<RegexMatcher>("^(43|44)$")));
  RegexMatcher matcher("(\\d+):(\\w+)", std::move(group_matchers));
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches("answer: 42:life"));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_TRUE(matcher.Matches("answer: 4:life"));
  EXPECT_FALSE(matcher.Matches("answer: 43:happiness"));
  EXPECT_FALSE(matcher.Matches("answer: 44:noone"));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(2, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, StatusRegexMatcher) {
  StatusRegexMatcher matcher(absl::StatusCode::kInvalidArgument,
                             "(\\d+):(\\w+)");
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_TRUE(matcher.Matches(absl::Status(absl::StatusCode::kInvalidArgument,
                                           "answer: 43:happiness")));
  EXPECT_TRUE(matcher.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_FALSE(matcher.Matches(absl::Status(absl::StatusCode::kInvalidArgument,
                                            "answer: nothing:noone")));
  EXPECT_FALSE(matcher.Matches(
      absl::Status(absl::StatusCode::kInternal, "answer: 42:life")));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(3, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, SubstringMatcher) {
  SubstringMatcher matcher("42");
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches("answer: 42:life"));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_FALSE(matcher.Matches("answer: 43:happiness"));
  EXPECT_TRUE(matcher.Matches("answer: 42:life"));
  EXPECT_FALSE(matcher.Matches("answer: nothing:noone"));
  EXPECT_TRUE(matcher.Matches("answer: 42:life"));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(3, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, StatusSubstringMatcher) {
  StatusSubstringMatcher matcher(absl::StatusCode::kInvalidArgument, "42");
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());

  EXPECT_TRUE(matcher.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_FALSE(matcher.Matches(absl::Status(absl::StatusCode::kInvalidArgument,
                                            "answer: 43:happiness")));
  EXPECT_TRUE(matcher.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_FALSE(matcher.Matches(absl::Status(absl::StatusCode::kInvalidArgument,
                                            "answer: nothing:noone")));
  EXPECT_FALSE(matcher.Matches(
      absl::Status(absl::StatusCode::kInternal, "answer: 42:life")));

  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(2, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

TEST(ErrorMatcherTest, CollectionMatcher) {
  std::vector<std::unique_ptr<MatcherBase<absl::Status>>> error_matchers;
  error_matchers.emplace_back(
      new StatusErrorCodeMatcher(absl::StatusCode::kInvalidArgument));
  error_matchers.emplace_back(new StatusRegexMatcher(
      absl::StatusCode::kInvalidArgument, "(\\d+):(\\w+)"));
  error_matchers.emplace_back(
      new StatusSubstringMatcher(absl::StatusCode::kInvalidArgument, "42"));
  MatcherCollection<absl::Status> matchers("TestCollection",
                                           std::move(error_matchers));
  EXPECT_FALSE(matchers.HasMatches());
  EXPECT_EQ(0, matchers.MatchCount());

  EXPECT_TRUE(matchers.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_TRUE(matchers.HasMatches());
  EXPECT_TRUE(matchers.Matches(absl::Status(absl::StatusCode::kInvalidArgument,
                                            "answer: 43:happiness")));
  EXPECT_TRUE(matchers.Matches(
      absl::Status(absl::StatusCode::kInvalidArgument, "answer: 42:life")));
  EXPECT_TRUE(matchers.Matches(absl::Status(absl::StatusCode::kInvalidArgument,
                                            "answer: nothing:noone")));
  EXPECT_FALSE(matchers.Matches(
      absl::Status(absl::StatusCode::kInternal, "answer: 42:life")));

  EXPECT_TRUE(matchers.HasMatches());
  EXPECT_EQ(4, matchers.MatchCount());
  ABSL_LOG(INFO) << matchers.MatcherSummary();
}

TEST(ErrorMatcherTest, CollectionMatcher_AddMatcher) {
  MatcherCollection<absl::Status> matchers("TestCollection", {});
  const absl::Status s = absl::InvalidArgumentError("test");
  EXPECT_FALSE(matchers.Matches(s));
  matchers.AddMatcher(std::make_unique<StatusErrorCodeMatcher>(
      absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(matchers.Matches(s));
}

TEST(ErrorMatcherTest, ProtoFieldIsDefaultMatcher) {
  ProtoFieldIsDefaultMatcher<googlesql_test::KitchenSinkPB, std::string>
      matcher("bool_val", std::make_unique<SubstringMatcher>("match"));
  googlesql_test::KitchenSinkPB kitchen_sink;
  matcher.Matches(std::make_pair(kitchen_sink, "match"));
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());
  matcher.Matches(std::make_pair(kitchen_sink, "nope"));
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());
  kitchen_sink.set_bool_val(!kitchen_sink.bool_val());
  matcher.Matches(std::make_pair(kitchen_sink, "nope"));
  EXPECT_FALSE(matcher.HasMatches());
  EXPECT_EQ(0, matcher.MatchCount());
  matcher.Matches(std::make_pair(kitchen_sink, "match"));
  EXPECT_TRUE(matcher.HasMatches());
  EXPECT_EQ(1, matcher.MatchCount());
  ABSL_LOG(INFO) << matcher.MatcherSummary();
}

// This test can be used to validate new matchers added to
// `RuntimeExpectedErrorMatcher` for failing queries that are found
// during randomized testing.
TEST(ErrorMatcherTest, RuntimeExpectedErrorMatcher) {
  std::unique_ptr<MatcherBase<absl::Status>> matcher =
      RuntimeExpectedErrorMatcher("RuntimeErrors");
  EXPECT_FALSE(matcher->HasMatches());
  EXPECT_EQ(0, matcher->MatchCount());

  EXPECT_FALSE(matcher->Matches(
      absl::Status(absl::StatusCode::kOutOfRange,
                   "This is a fake error message and should not match")));
  EXPECT_FALSE(matcher->HasMatches());
  EXPECT_EQ(0, matcher->MatchCount());

  EXPECT_TRUE(
      matcher->Matches(absl::Status(absl::StatusCode::kOutOfRange,
                                    "Bitwise binary operator for BYTES "
                                    "requires equal length of the inputs")));
  EXPECT_TRUE(matcher->HasMatches());
  EXPECT_EQ(1, matcher->MatchCount());

  EXPECT_TRUE(matcher->Matches(
      absl::Status(absl::StatusCode::kOutOfRange,
                   "LOG10 is undefined for zero or negative value")));
  EXPECT_TRUE(matcher->HasMatches());
  EXPECT_EQ(2, matcher->MatchCount());

  // Error matcher for COSINE_DISTANCE
  const std::string cosine_distance_error_message = R"(Duplicate index 17
  18
  19
  found in the input array.)";
  EXPECT_TRUE(matcher->Matches(absl::Status(absl::StatusCode::kOutOfRange,
                                            cosine_distance_error_message)));
  EXPECT_TRUE(matcher->HasMatches());
  EXPECT_EQ(3, matcher->MatchCount());

  ABSL_LOG(INFO) << matcher->MatcherSummary();
}

// This test can be used to validate new matchers added to
// `RuntimeDMLExpectedErrorMatcher` for failing queries that are found
// during randomized testing.
TEST(ErrorMatcherTest, RuntimeDMLExpectedErrorMatcher) {
  std::unique_ptr<MatcherBase<absl::Status>> matcher =
      RuntimeDMLExpectedErrorMatcher("DMLRuntimeErrors");
  EXPECT_FALSE(matcher->HasMatches());
  EXPECT_EQ(0, matcher->MatchCount());

  EXPECT_FALSE(matcher->Matches(
      absl::Status(absl::StatusCode::kOutOfRange,
                   "This is a fake error message and should not match")));
  EXPECT_FALSE(matcher->HasMatches());
  EXPECT_EQ(0, matcher->MatchCount());

  EXPECT_TRUE(
      matcher->Matches(absl::Status(absl::StatusCode::kOutOfRange,
                                    "INSERT OR REPLACE is not allowed because "
                                    "the table does not have a primary key")));
  EXPECT_TRUE(matcher->HasMatches());
  EXPECT_EQ(1, matcher->MatchCount());

  ABSL_LOG(INFO) << matcher->MatcherSummary();
}

}  // namespace googlesql
