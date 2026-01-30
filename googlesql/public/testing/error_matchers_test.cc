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

#include "googlesql/public/testing/error_matchers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using ::testing::Not;

TEST(ErrorMatchersTest, NamedArgumentSuppliedPositionallyMatches) {
  EXPECT_THAT(absl::InvalidArgumentError(
                  "Positional argument is invalid because this function "
                  "restricts that this argument is referred to by name"),
              IsNamedArgumentSuppliedPositionallyError());
  EXPECT_THAT(absl::InvalidArgumentError(
                  R"(No matching signature for function SPANNER:SNIPPET
  Argument types: STRING, STRING, BOOL, STRING, INT64
  Signature: SNIPPET(STRING, STRING, [enhance_query => BOOL], [language_tag => STRING], [max_snippet_width => INT64], [max_snippets => INT64], [content_type => STRING])
    Positional argument at 3 is invalid because argument `enhance_query` can only be referred to by name [at 2:14]
      SELECT SNIPPET(a.Summary, 'foo', true, "", 10)
             ^)"),
              IsNamedArgumentSuppliedPositionallyError());
}

TEST(ErrorMatchersTest, WrongNumberOfArgs) {
  EXPECT_THAT(absl::InvalidArgumentError(
                  "Number of arguments does not match for function foo"),
              IsWrongNumberOfArgsError("foo"));
  EXPECT_THAT(
      absl::InvalidArgumentError(
          "Number of arguments does not match for aggregate function foo"),
      IsWrongNumberOfArgsError("foo"));
}

TEST(ErrorMatchersTest, FiunctionSignatureMismatch) {
  absl::string_view error_message = "No matching signature for function foo";
  EXPECT_THAT(error_message, IsFunctionSignatureMismatchErrorMessage("foo"));
  EXPECT_THAT(absl::InvalidArgumentError(error_message),
              IsFunctionSignatureMismatchError("foo"));
}

TEST(ErrorMatchersTest, FiunctionSignatureMismatchAggregate) {
  absl::string_view error_message =
      "No matching signature for aggregate function foo";
  EXPECT_THAT(error_message, IsFunctionSignatureMismatchErrorMessage("foo"));
  EXPECT_THAT(absl::InvalidArgumentError(error_message),
              IsFunctionSignatureMismatchError("foo"));
}

TEST(ErrorMatchersTest, OperatorSignatureMismatch) {
  absl::string_view error_message = "No matching signature for operator +";
  EXPECT_THAT(error_message, IsOperatorSignatureMismatchErrorMessage("+"));
  EXPECT_THAT(absl::InvalidArgumentError(error_message),
              IsOperatorSignatureMismatchError("+"));
}

TEST(ErrorMatchersTest, IsNamedArgumentNotFoundError) {
  absl::Status error = absl::InvalidArgumentError(
      "Named argument `foo` not found in signature for call to function bar");
  EXPECT_THAT(error, IsNamedArgumentNotFoundError("foo"));
  EXPECT_THAT(error, IsNamedArgumentNotFoundError("bar"));
}

TEST(ErrorMatchersTest, DoesNotMatch) {
  absl::Status error =
      absl::InvalidArgumentError("Statement not supported: ExplainStatement");
  EXPECT_THAT(error, IsStatementNotSupportedError("EXPLAIN"));
  EXPECT_THAT(error, IsStatementNotSupportedError("Explain"));
  EXPECT_THAT(error, Not(IsStatementNotSupportedError("Select")));
}

TEST(ErrorMatchersTest, IsNamedArgumentNotFoundErrorMessage) {
  constexpr absl::string_view kErrorMessage =
      "No matching signature for aggregate function COUNT(INT64, STRING, "
      "STRING); \n  Signature: COUNT(T2, [contribution_bounds_per_group => "
      "STRUCT<INT64, INT64>]) -> INT64\n    Named argument "
      "`bounding_algorithm_type` does not exist in signature [at 2:12]";
  EXPECT_THAT(kErrorMessage,
              IsNamedArgumentNotFoundErrorMessage("bounding_algorithm_type"));
}

TEST(ErrorMatchersTest, IsNamedArgumentNotFoundErrorMessageOld) {
  // Before the improved function signature mismatch error messages, sometimes
  // we produced an error reporting wrong number of arguments in this case.
  constexpr absl::string_view kErrorMessage =
      "No matching signature for aggregate function COUNT(INT64, STRING, "
      "STRING); \n  Signature: COUNT(T2, [contribution_bounds_per_group => "
      "STRUCT<INT64, INT64>]) -> INT64\n    Named argument "
      "`bounding_algorithm_type` does not exist in signature [at 2:12]";
  EXPECT_THAT(kErrorMessage,
              IsNamedArgumentNotFoundErrorMessage("bounding_algorithm_type"));
}

TEST(ErrorMatchersTest, IsTableNotFoundError) {
  absl::Status error = absl::NotFoundError("Table not found: foo");
  EXPECT_THAT(error.message(), IsTableNotFoundErrorMessage("Foo"));
  EXPECT_THAT(error, IsTableNotFoundError("fOo"));
}

TEST(ErrorMatchersTest, IsSyntaxError) {
  EXPECT_THAT(absl::InvalidArgumentError(
                  "Syntax error: Expected end of input but got identifier"),
              IsSyntaxError());
}

TEST(ErrorMatchersTest, IsAtLocation) {
  absl::Status error = absl::InvalidArgumentError("Blah blah [at 27:3]");
  EXPECT_THAT(error, IsAtLocation(27, 3));
  EXPECT_THAT(error, Not(IsAtLocation(27, 4)));
  EXPECT_THAT(error, Not(IsAtLocation(28, 3)));
}

}  // namespace googlesql
