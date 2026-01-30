//
// Copyright 2018 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_TESTING_STATUS_MATCHERS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_TESTING_STATUS_MATCHERS_H_

// Testing utilities for working with absl::Status and absl::StatusOr.

#include <ostream>
#include <string>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql_base {
namespace testing {
namespace internal_status {

void AddFatalFailure(absl::string_view expression,
                     const ::googlesql_base::StatusBuilder& builder);

}  // namespace internal_status

// Macros for testing the results of functions that return absl::Status or
// absl::StatusOr<T> (for any type T).
#define GOOGLESQL_EXPECT_OK(expression) \
  EXPECT_THAT(expression, ::absl_testing::IsOk())
#define GOOGLESQL_ASSERT_OK(expression) \
  ASSERT_THAT(expression, ::absl_testing::IsOk())

// Executes an expression that returns a absl::StatusOr, and
// assigns the contained variable to lhs if the error code is OK. If the Status
// is non-OK, generates a test failure and returns from the current function,
// which must have a void return type.
//
// Example: Declaring and initializing a new value
//   GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ValueType& value, MaybeGetValue(arg));
//
// Example: Assigning to an existing value
//   ValueType value;
//   GOOGLESQL_ASSERT_OK_AND_ASSIGN(value, MaybeGetValue(arg));
//
// The value assignment example would expand into:
//   StatusOr<ValueType> status_or_value = MaybeGetValue(arg);
//   GOOGLESQL_ASSERT_OK(statusor.status());
//   value = status_or_value.value();
//
// WARNING: Like GOOGLESQL_ASSIGN_OR_RETURN, GOOGLESQL_ASSERT_OK_AND_ASSIGN expands into
//   multiple statements; it cannot be used in a single statement (e.g. as the
//   body of an if statement without {})!
#define GOOGLESQL_ASSERT_OK_AND_ASSIGN(lhs, rexpr) \
  GOOGLESQL_ASSIGN_OR_RETURN(                      \
      lhs, rexpr,                                \
      ::googlesql_base::testing::internal_status::AddFatalFailure(#rexpr, _))

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

}  // namespace testing
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_TESTING_STATUS_MATCHERS_H_
