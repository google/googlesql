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

#include "googlesql/tools/execute_query/execute_query_prompt_testutils.h"

#include <string>

#include "gtest/gtest.h"

namespace googlesql {
namespace {

TEST(PrepareCompletionReqTest, Empty) {
  const std::string body;
  const ExecuteQueryCompletionRequest got = PrepareCompletionReq(body, 0);
  EXPECT_TRUE(got.body.empty());
  EXPECT_EQ(got.cursor_position, 0);
  EXPECT_EQ(got.word_start, 0);
  EXPECT_EQ(got.word_end, 0);
  EXPECT_TRUE(got.word.empty());
}

TEST(PrepareCompletionReqTest, SingleWord) {
  const std::string body = "SELECT";

  {
    const ExecuteQueryCompletionRequest got = PrepareCompletionReq(body, 0);
    EXPECT_EQ(got.body, "SELECT");
    EXPECT_EQ(got.cursor_position, 0);
    EXPECT_EQ(got.word_start, 0);
    EXPECT_EQ(got.word_end, 0);
    EXPECT_TRUE(got.word.empty());
  }

  {
    const ExecuteQueryCompletionRequest got = PrepareCompletionReq(body, 4);
    EXPECT_EQ(got.body, "SELECT");
    EXPECT_EQ(got.cursor_position, 4);
    EXPECT_EQ(got.word_start, 0);
    EXPECT_EQ(got.word_end, 4);
    EXPECT_EQ(got.word, "SELE");
  }

  {
    const ExecuteQueryCompletionRequest got = PrepareCompletionReq(body, 6);
    EXPECT_EQ(got.body, "SELECT");
    EXPECT_EQ(got.cursor_position, 6);
    EXPECT_EQ(got.word_start, 0);
    EXPECT_EQ(got.word_end, 6);
    EXPECT_EQ(got.word, "SELECT");
  }
}

TEST(PrepareCompletionReqTest, Middle) {
  const std::string body = "Hello world test";

  {
    const ExecuteQueryCompletionRequest got = PrepareCompletionReq(body, 9);
    EXPECT_EQ(got.body, "Hello world test");
    EXPECT_EQ(got.cursor_position, 9);
    EXPECT_EQ(got.word_start, 6);
    EXPECT_EQ(got.word_end, 9);
    EXPECT_EQ(got.word, "wor");
  }

  {
    const ExecuteQueryCompletionRequest got = PrepareCompletionReq(body, 11);
    EXPECT_EQ(got.body, "Hello world test");
    EXPECT_EQ(got.cursor_position, 11);
    EXPECT_EQ(got.word_start, 6);
    EXPECT_EQ(got.word_end, 11);
    EXPECT_EQ(got.word, "world");
  }
}

}  // namespace
}  // namespace googlesql
