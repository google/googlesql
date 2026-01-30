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

#include "googlesql/public/functions/util.h"

#include "googlesql/base/testing/status_matchers.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {
namespace internal {

namespace {

using absl_testing::StatusIs;

TEST(UpdateError, UpdateError) {
  // Regular string.
  absl::Status status;
  UpdateError(&status, "xyz");
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange, "xyz"));

  // Valid UTF-8.
  status = absl::OkStatus();
  UpdateError(&status, "xyzñ");
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange, "xyzñ"));

  // Does nothing if status is already set.
  status = absl::Status(absl::StatusCode::kInvalidArgument, "msg");
  UpdateError(&status, "xyzñ");
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument, "msg"));

  // Invalid UTF-8 gets converted to valid UTF-8 with REPLACEMENT CHARACTER
  // used for invalid characters.
  status = absl::OkStatus();
  UpdateError(&status, "xyz\xc3(");
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange, "xyz\uFFFD("));
}

}  // anonymous namespace
}  // namespace internal
}  // namespace functions
}  // namespace googlesql
