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

#include "googlesql/compliance/test_util.h"

#include "gtest/gtest.h"

namespace googlesql {

TEST(Escaping, ShellEscape) {
  struct TestValue {
    const char* input;
    const char* output;
  };
  TestValue tests[] = {
      // no escape
      {"Aa0-_.=/:,@", "Aa0-_.=/:,@"},
      // empty
      {"", "''"},
      // no single quotes
      {"foo bar", "'foo bar'"},
      // single quotes
      {"'", "\"'\""},
      // needs double quotes
      {"a\\b$c\"d`e'f g", "\"a\\\\b\\$c\\\"d\\`e'f g\""},
      // needs double quotes
      {"'a$", "\"'a\\$\""},        // quote at end
      {"a$'a", "\"a\\$'a\""},      // quote in middle
      {"a$$'a", "\"a\\$\\$'a\""},  // two in a row
      {"$'a", "\"\\$'a\""},        // quote at beginning
  };

  const int NUM_TESTS = sizeof(tests) / sizeof(TestValue);

  for (int i = 0; i < NUM_TESTS; ++i) {
    EXPECT_EQ(ShellEscape(tests[i].input), tests[i].output);
  }
}

}  // namespace googlesql
