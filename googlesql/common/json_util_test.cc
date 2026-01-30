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

#include "googlesql/common/json_util.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql {

void TestJsonEscapeString(absl::string_view input, absl::string_view expected) {
  std::string actual;
  JsonEscapeString(input, &actual);
  EXPECT_EQ(actual, expected) << "input=>>>" << input << "<<<";

  std::string input_with_quotes("\"");
  input_with_quotes.append(input);
  input_with_quotes.push_back('"');
  EXPECT_EQ(JsonStringNeedsEscaping(input), input_with_quotes != actual);
}

// Test that the input is simply wrapped in quotes: input->"input"
TEST(JsonEscapeString, NoEscapingNormalCharacters) {
  TestJsonEscapeString("", R"("")");
  TestJsonEscapeString("a", R"("a")");
  TestJsonEscapeString("Σ", R"("Σ")");
  TestJsonEscapeString("abcd123Σ  ", R"("abcd123Σ  ")");
}

TEST(JsonEscapeString, NonPrintables) {
  TestJsonEscapeString("\b\f\n\r\t", R"("\b\f\n\r\t")");
  absl::string_view view_with_null("\x0\x1\x2\x3\x1a", 5);
  TestJsonEscapeString(view_with_null, R"("\u0000\u0001\u0002\u0003\u001a")");
}

TEST(JsonEscapeString, BackSlashAndQuote) {
  TestJsonEscapeString("\"\\", R"("\"\\")");
}

TEST(JsonEscapeString, Separators) {
  TestJsonEscapeString("\u2028\u2029", R"("\u2028\u2029")");
  TestJsonEscapeString("\u2030\u2027", "\"\u2030\u2027\"");
}

// Test a combination of special and non-special cases.
TEST(JsonEscapeString, BigTest) {
  TestJsonEscapeString("Σ\u2028Σ\u2029Σ\n\f\x10\\\t",
                       R"("Σ\u2028Σ\u2029Σ\n\f\u0010\\\t")");
}

TEST(JsonEscapeString, Append) {
  std::string buffer = "field_name:";

  JsonEscapeAndAppendString("\b\f\n\r\t", &buffer);

  // Appends escaped string to the buffer.
  EXPECT_EQ(buffer, R"(field_name:"\b\f\n\r\t")");
}

}  // namespace googlesql
