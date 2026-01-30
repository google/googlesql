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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_RET_CHECK_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_RET_CHECK_H_

// Macros for non-fatal assertions.  The `GOOGLESQL_RET_CHECK` family of macros
// mirrors the `ABSL_CHECK` family from "base/logging.h", but instead of aborting the
// process on failure, these return a absl::Status with code
// `googlesql::StatusCode::kInternal` from the current method.
//
//   GOOGLESQL_RET_CHECK(ptr != nullptr);
//   GOOGLESQL_RET_CHECK_GT(value, 0) << "Optional additional message";
//   GOOGLESQL_RET_CHECK_FAIL() << "Always fails";
//   GOOGLESQL_RET_CHECK_OK(status) << "If status is not OK, return an internal
//   error";
//
// The GOOGLESQL_RET_CHECK* macros can only be used in functions that return
// absl::Status or absl::StatusOr.  The generated
// `absl::Status` will contain the string "GOOGLESQL_RET_CHECK failure".
//
// On failure these routines will log a stack trace to `ERROR`.  The
// `GOOGLESQL_RET_CHECK` macros end with a `googlesql_base::StatusBuilder` in
// their tail position and can be customized like calls to
// `GOOGLESQL_RETURN_IF_ERROR` from `status_macros.h`.
//

#include <string>

#include "absl/status/status.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql_base {
namespace internal_ret_check {

// Returns a StatusBuilder that corresponds to a `GOOGLESQL_RET_CHECK` failure.
StatusBuilder RetCheckFailSlowPath(SourceLocation location);
StatusBuilder RetCheckFailSlowPath(SourceLocation location,
                                   const char* condition);
StatusBuilder RetCheckFailSlowPath(SourceLocation location,
                                   const char* condition,
                                   const absl::Status& s);

// Takes ownership of `condition`.  This API is a little quirky because it is
// designed to make use of the `::Check_*Impl` methods that implement `CHECK_*`
// and `DCHECK_*`.
StatusBuilder RetCheckFailSlowPath(SourceLocation location,
                                   std::string* condition);

inline StatusBuilder RetCheckImpl(const absl::Status& status,
                                  const char* condition,
                                  SourceLocation location) {
  if (ABSL_PREDICT_TRUE(status.ok()))
    return StatusBuilder(absl::OkStatus(), location);
  return RetCheckFailSlowPath(location, condition, status);
}

}  // namespace internal_ret_check
}  // namespace googlesql_base

#define GOOGLESQL_RET_CHECK(cond)                                    \
  while (ABSL_PREDICT_FALSE(!(cond)))                                \
  return ::googlesql_base::internal_ret_check::RetCheckFailSlowPath( \
      GOOGLESQL_LOC, #cond)

#define GOOGLESQL_RET_CHECK_FAIL()                                   \
  return ::googlesql_base::internal_ret_check::RetCheckFailSlowPath( \
      GOOGLESQL_LOC)

// Takes an expression returning absl::Status and asserts that the
// status is `ok()`.  If not, it returns an internal error.
//
// This is similar to `GOOGLESQL_RETURN_IF_ERROR` in that it propagates errors.
// The difference is that it follows the behavior of `GOOGLESQL_RET_CHECK`,
// returning an internal error (wrapping the original error text), including the
// filename and line number, and logging a stack trace.
//
// This is appropriate to use to write an assertion that a function that returns
// `absl::Status` cannot fail, particularly when the error code itself
// should not be surfaced.
#define GOOGLESQL_RET_CHECK_OK(status)                                      \
  GOOGLESQL_RETURN_IF_ERROR(                                                \
      ::googlesql_base::internal_ret_check::RetCheckImpl((status), #status, \
                                                         GOOGLESQL_LOC))

#if defined(STATIC_ANALYSIS)
#define GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(name, op, lhs, rhs) \
  GOOGLESQL_RET_CHECK((lhs)op(rhs))
#else
#define GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(name, op, lhs, rhs) \
  while (std::string* _result = googlesql_base::Check_##name##Impl(       \
             ::googlesql_base::GetReferenceableValue(lhs),                \
             ::googlesql_base::GetReferenceableValue(rhs),                \
             #lhs " " #op " " #rhs))                                      \
  return ::googlesql_base::internal_ret_check::RetCheckFailSlowPath(      \
      GOOGLESQL_LOC, _result)
#endif

#define GOOGLESQL_RET_CHECK_EQ(lhs, rhs) \
  GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(EQ, ==, lhs, rhs)
#define GOOGLESQL_RET_CHECK_NE(lhs, rhs) \
  GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(NE, !=, lhs, rhs)
#define GOOGLESQL_RET_CHECK_LE(lhs, rhs) \
  GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(LE, <=, lhs, rhs)
#define GOOGLESQL_RET_CHECK_LT(lhs, rhs) \
  GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(LT, <, lhs, rhs)
#define GOOGLESQL_RET_CHECK_GE(lhs, rhs) \
  GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(GE, >=, lhs, rhs)
#define GOOGLESQL_RET_CHECK_GT(lhs, rhs) \
  GOOGLESQL_STATUS_MACROS_INTERNAL_RET_CHECK_OP(GT, >, lhs, rhs)

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_RET_CHECK_H_
