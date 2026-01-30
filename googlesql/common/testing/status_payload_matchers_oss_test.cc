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

#include "googlesql/common/testing/status_payload_matchers_oss.h"

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/testing/status_payload_matchers_test.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/testing/proto_matchers_oss.h"

using ::testing::AllOf;
using ::testing::Not;

namespace googlesql {
namespace testing {
namespace {

using googlesql::CustomPayload;
using googlesql::OtherPayload;
using googlesql::internal::AttachPayload;
using googlesql::testing::EqualsProto;
using googlesql::testing::StatusHasPayload;
using googlesql_base::testing::StatusIs;

absl::Status StatusWithPayload(absl::string_view text) {
  absl::Status status{absl::StatusCode::kUnknown, "message"};

  CustomPayload payload;
  payload.set_text(text);
  AttachPayload(&status, payload);

  return status;
}

absl::StatusOr<int> StatusOrWithPayload(absl::string_view text) {
  return StatusWithPayload(text);
}

TEST(StatusHasPayload, Status) {
  EXPECT_THAT(absl::OkStatus(), Not(StatusHasPayload<CustomPayload>()));
  EXPECT_THAT(absl::OkStatus(), Not(StatusHasPayload<OtherPayload>()));

  EXPECT_THAT(StatusWithPayload("foo"), StatusHasPayload<CustomPayload>());
  EXPECT_THAT(StatusWithPayload("foo"),
              StatusHasPayload<CustomPayload>(EqualsProto(R"pb(
                text: "foo"
              )pb")));

  EXPECT_THAT(StatusWithPayload("hello"),
              AllOf(StatusIs(absl::StatusCode::kUnknown, "message"),
                    Not(StatusHasPayload<OtherPayload>())));
}

TEST(StatusHasPayload, StatusOr) {
  EXPECT_THAT(StatusOrWithPayload("aaa"), StatusHasPayload<CustomPayload>());
  EXPECT_THAT(StatusOrWithPayload("bbb"),
              Not(StatusHasPayload<OtherPayload>()));
  EXPECT_THAT(StatusOrWithPayload("ccc"),
              StatusHasPayload<CustomPayload>(EqualsProto(R"pb(
                text: "ccc"
              )pb")));
  EXPECT_THAT(StatusOrWithPayload("ccc"),
              Not(StatusHasPayload<OtherPayload>(EqualsProto(R"pb(
                text: "ccc"
              )pb"))));
}

}  // namespace
}  // namespace testing
}  // namespace googlesql
