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

// This file contains a library of test matchers for matching GoogleSQL error
// messages in unit tests. Using matchers controlled by the GoogleSQL library
// helps prevent brittle tests that depend on the exact wording of error
// messages.
//
// Additional matchers can be added to this file as needed, but please make
// sure that added matchers are written in a way so that they aren't brittle in
// the same way that matching the error test directly would be. In particular,
// avoid matchers that accept a error messages fragment as a string argument.
//
// Example usage:
//
// TEST(MyTest, MyTest) {
//   googlesql::SimpleCatalog catalog = InitializeCatalog();
//   // MyFunction only accepts a named argument.
//   EXPECT_THAT(
//       PrepareQuery("SELECT MyFunction(arg=>1)"), ::testing::IsOk());
//   EXPECT_THAT(
//       PrepareQuery("SELECT MyFunction(1)"),
//       googlesql::IsNamedArgumentSuppliedPositionallyError());
// }

#ifndef GOOGLESQL_PUBLIC_TESTING_ERROR_MATCHERS_H_
#define GOOGLESQL_PUBLIC_TESTING_ERROR_MATCHERS_H_

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "googlesql/base/testing/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

namespace internal {

inline absl::string_view GetMessage(const absl::Status& status) {
  return status.message();
}

template <typename T>
inline absl::string_view GetMessage(const absl::StatusOr<T>& status) {
  return status.status().message();
}

}  // namespace internal

// Matches an error message indicating that no function signature matches the
// arguments supplied to the function.
//
// The argument is the function name. If the error matched reports a function
// name then the matcher will assert that the reported name matches the argument
// in a case insensitive manner.
//
// Typically users should prefer to match the entire Status or StatusOr using
// IsFunctionSignatureMismatchError. The Message variation is useful for
// testing cases where the StatusCode has been changed by software layers above
// the GoogleSQL library.
MATCHER_P(
    IsFunctionSignatureMismatchErrorMessage, function_name,
    "is an error indicating no function signatures matches the arguments") {
  auto name_matcher =
      ::testing::HasSubstr(absl::AsciiStrToUpper(function_name));
  auto message_matcher = ::testing::ContainsRegex("No matching signature for");
  return ExplainMatchResult(name_matcher, absl::AsciiStrToUpper(arg),
                            result_listener) &&
         ExplainMatchResult(message_matcher, arg, result_listener);
}

// Matches an error Status indicating that no function signature matches the
// arguments supplied to the function.
//
// The argument is the function name. If the error matched reports a function
// name then the matcher will assert that the reported name matches the argument
// in a case insensitive manner.
MATCHER_P(
    IsFunctionSignatureMismatchError, function_name,
    "is an error indicating no function signatures matches the arguments") {
  return ExplainMatchResult(
      absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          IsFunctionSignatureMismatchErrorMessage(function_name)),
      arg, result_listener);
}

// Matches an error message indicating that no operator signature matches the
// arguments supplied to the operator.
//
// The argument is the operator name. If the error matched reports an operator
// name then the matcher will assert that the reported name matches the argument
// in a case insensitive manner.
//
// Typically users should prefer to match the entire Status or StatusOr using
// IsOperatorSignatureMismatchError. The Message variation is useful for
// testing cases where the StatusCode has been changed by software layers above
// the GoogleSQL library.
MATCHER_P(
    IsOperatorSignatureMismatchErrorMessage, operator_name,
    "is an error indicating no operator signatures matches the arguments") {
  auto name_matcher =
      ::testing::HasSubstr(absl::AsciiStrToUpper(operator_name));
  auto message_matcher =
      ::testing::HasSubstr("No matching signature for operator");
  return ExplainMatchResult(name_matcher, absl::AsciiStrToUpper(arg),
                            result_listener) &&
         ExplainMatchResult(message_matcher, arg, result_listener);
}

// Matches an error Status indicating that no operator signature matches the
// arguments supplied to the operator.
//
// The argument is the operator name. If the error matched reports an operator
// name then the matcher will assert that the reported name matches the argument
// in a case insensitive manner.
MATCHER_P(
    IsOperatorSignatureMismatchError, operator_name,
    "is an error indicating no operator signatures matches the arguments") {
  return ExplainMatchResult(
      absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          IsOperatorSignatureMismatchErrorMessage(operator_name)),
      arg, result_listener);
}

// Matches an error message indicating that no function signature has the right
// number of arguments.
//
// The argument is the function name. If the error matched reports a function
// name then the matcher will assert that the reported name matches the argument
// in a case insensitive manner.
MATCHER_P(IsWrongNumberOfArgsErrorMessage, function_name,
          "is an error message indicating the number of arguments does not "
          "match any signature for the function") {
  auto name_matcher =
      ::testing::HasSubstr(absl::AsciiStrToUpper(function_name));
  auto message_matcher = ::testing::AnyOf(
      ::testing::ContainsRegex(
          "Number of arguments does not match for (aggregate )?function"),
      ::testing::ContainsRegex("Signature accepts at most \\d+ "
                               "arguments?, found \\d+ argument"),
      ::testing::ContainsRegex("Signature requires at least \\d+ "
                               "arguments?, found \\d+ argument"));
  return ExplainMatchResult(name_matcher, absl::AsciiStrToUpper(arg),
                            result_listener) &&
         ExplainMatchResult(message_matcher, arg, result_listener);
}

// Matches an error Status indicating that no function signature has the right
// number of arguments.
//
// The argument is the function name. If the error matched reports a function
// name then the matcher will assert that the reported name matches the argument
// in a case insensitive manner.
MATCHER_P(IsWrongNumberOfArgsError, function_name,
          "is an error Status indicating the number of arguments does not "
          "match any signature for the function") {
  return ExplainMatchResult(
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                             IsWrongNumberOfArgsErrorMessage(function_name)),
      arg, result_listener);
}

MATCHER(
    IsNamedArgumentSuppliedPositionallyError,
    "is an error indicating a named-only argument is supplied positionally") {
  auto matcher = ::testing::AnyOf(
      absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr(
              "Positional argument is invalid because this function restricts "
              "that this argument is referred to by name")),
      absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::ContainsRegex(
              "Positional argument at .* is invalid because argument .* "
              "can only be referred to by name")));
  return ExplainMatchResult(matcher, arg, result_listener);
}

// Matches an error message indicating that the supplied named argument is not
// found.
//
// The argument is the name of the named argument. If the error matched reports
// an argument name then the matcher will assert that the reported name matches
// the argument in a case insensitive manner.
MATCHER_P(IsNamedArgumentNotFoundErrorMessage, arg_name,
          "is an error message indicating a named argument is not found") {
  auto name_matcher = ::testing::HasSubstr(absl::AsciiStrToUpper(arg_name));
  auto message_matcher = ::testing::AnyOf(
      ::testing::ContainsRegex(
          "Named argument .* not found in signature for call to.*"),
      ::testing::ContainsRegex(
          "Named argument .* does not exist in signature"));
  if (ExplainMatchResult(name_matcher, absl::AsciiStrToUpper(arg),
                         result_listener) &&
      ExplainMatchResult(message_matcher, arg, result_listener)) {
    return true;
  }
  // Before the improved function signature mismatch error messages, sometimes
  // we produced a "wrong number of arguments" error.
  return ExplainMatchResult(
      ::testing::ContainsRegex(
          "Number of arguments does not match for (aggregate )?function"),
      arg, result_listener);
}

// Matches an error Status indicating that the supplied named argument is not
// found.
//
// The argument is the name of the named argument. If the error matched reports
// an argument name then the matcher will assert that the reported name matches
// the argument in a case insensitive manner.
MATCHER_P(IsNamedArgumentNotFoundError, arg_name,
          "is an error status indicating a named-only argument is not found") {
  return ExplainMatchResult(
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                             IsNamedArgumentNotFoundErrorMessage(arg_name)),
      arg, result_listener);
}

// Matches an error indicating that a GoogleSQL statement kind is not supported.
//
// The argument is the statement kind name. If the error matched reports a
// statement kind name then the matcher will assert that the reported name
// matches the argument in a case insensitive manner.
MATCHER_P(IsStatementNotSupportedError, statement_kind_name,
          "is an error indicating the statement kind is not supported") {
  auto kind_matcher =
      ::testing::HasSubstr(absl::AsciiStrToUpper(statement_kind_name));
  auto status_matcher =
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                             ::testing::HasSubstr("Statement not supported:"));
  return ExplainMatchResult(kind_matcher,
                            absl::AsciiStrToUpper(internal::GetMessage(arg)),
                            result_listener) &&
         ExplainMatchResult(status_matcher, arg, result_listener);
}

// Matches an error message indicating that a table is not found.
//
// The argument is the name of the table. If the error matched reports a
// table name then the matcher will assert that the reported name matches
// the argument in a case insensitive manner.
MATCHER_P(IsTableNotFoundErrorMessage, table_name,
          "is an error message indicating the table is not found") {
  auto name_matcher = ::testing::HasSubstr(absl::AsciiStrToUpper(table_name));
  auto message_matcher = ::testing::HasSubstr("Table not found");
  return ExplainMatchResult(name_matcher, absl::AsciiStrToUpper(arg),
                            result_listener) &&
         ExplainMatchResult(message_matcher, arg, result_listener);
}

// Matches an error status indicating that a table is not found.
//
// The argument is the name of the table. If the error matched reports a
// table name then the matcher will assert that the reported name matches
// the argument in a case insensitive manner.
MATCHER_P(IsTableNotFoundError, table_name,
          "is an error indicating the table is not found") {
  return ExplainMatchResult(
      absl_testing::StatusIs(absl::StatusCode::kNotFound,
                             IsTableNotFoundErrorMessage(table_name)),
      arg, result_listener);
}

// Matches syntax errors from GoogleSQL's parser.
MATCHER(IsSyntaxErrorMessage,
        "is an error message indicating the table is not found") {
  return ExplainMatchResult(
      AnyOf(HasSubstr("Syntax error:"), HasSubstr("SYNTAX_ERROR")), arg,
      result_listener);
}

// Matches syntax errors from GoogleSQL's parser.
MATCHER(IsSyntaxError, "is an error indicating failure to parse the query") {
  return ExplainMatchResult(
      StatusIs(absl::StatusCode::kInvalidArgument, IsSyntaxErrorMessage()), arg,
      result_listener);
}

// Matches an error with a location encoded into the error message looking for
// the given line and column.
MATCHER_P2(IsAtLocation, line, column,
           "is an error located at the given line and column") {
  return ExplainMatchResult(
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(absl::StrCat("[at ", line, ":", column, "]"))),
      arg, result_listener);
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TESTING_ERROR_MATCHERS_H_
