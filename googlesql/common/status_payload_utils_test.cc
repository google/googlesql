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

#include "googlesql/common/status_payload_utils.h"

#include <string>

#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace internal {
namespace {

using ::googlesql::testing::EqualsProto;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

std::string StatusPayloadTypeUrl() {
  return GetTypeUrl<googlesql_test::TestStatusPayload>();
}

std::string StatusPayload2TypeUrl() {
  return GetTypeUrl<googlesql_test::TestStatusPayload2>();
}

absl::Cord ToStatusCord(const googlesql_test::TestStatusPayload& p) {
  return absl::Cord(p.SerializeAsString());
}

absl::Cord ToStatusCord(const googlesql_test::TestStatusPayload2& p) {
  return absl::Cord(p.SerializeAsString());
}

}  // namespace

TEST(StatusPayloadUtils, HasPayload) {
  EXPECT_FALSE(HasPayload(absl::OkStatus()));

  EXPECT_FALSE(HasPayload(absl::Status(absl::StatusCode::kCancelled, "")));

  absl::Status ok_status = absl::OkStatus();
  ok_status.SetPayload(StatusPayloadTypeUrl(),
                       ToStatusCord(googlesql_test::TestStatusPayload()));
  // Ok Status should never have a payload.
  EXPECT_FALSE(HasPayload(ok_status));

  absl::Status status1 = absl::Status(absl::StatusCode::kCancelled, "");
  status1.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  EXPECT_TRUE(HasPayload(status1));
}

TEST(StatusPayloadUtils, GetPayloadCount) {
  EXPECT_EQ(GetPayloadCount(absl::OkStatus()), 0);

  EXPECT_EQ(GetPayloadCount(absl::Status(absl::StatusCode::kCancelled, "")), 0);

  absl::Status status1 = absl::Status(absl::StatusCode::kCancelled, "");
  status1.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  EXPECT_EQ(GetPayloadCount(status1), 1);

  // Attaching the same message twice overwrites, count should be the same.
  absl::Status status2 = absl::Status(absl::StatusCode::kCancelled, "");
  status2.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  status2.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  EXPECT_EQ(GetPayloadCount(status2), 1);

  // Attaching the same message twice overwrites, count should be the same
  absl::Status status3 = absl::Status(absl::StatusCode::kCancelled, "");
  status3.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  status3.SetPayload(StatusPayload2TypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload2()));
  EXPECT_EQ(GetPayloadCount(status3), 2);
}

TEST(StatusPayloadUtils, HasProto) {
  EXPECT_FALSE(
      HasPayloadWithType<googlesql_test::TestStatusPayload>(absl::OkStatus()));

  absl::Status status1 = absl::Status(absl::StatusCode::kCancelled, "");
  status1.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  EXPECT_TRUE(HasPayloadWithType<googlesql_test::TestStatusPayload>(status1));
  EXPECT_FALSE(HasPayloadWithType<googlesql_test::TestStatusPayload2>(status1));

  absl::Status status2 = absl::Status(absl::StatusCode::kCancelled, "");
  status2.SetPayload(StatusPayloadTypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload()));
  status2.SetPayload(StatusPayload2TypeUrl(),
                     ToStatusCord(googlesql_test::TestStatusPayload2()));
  EXPECT_TRUE(HasPayloadWithType<googlesql_test::TestStatusPayload>(status2));
  EXPECT_TRUE(HasPayloadWithType<googlesql_test::TestStatusPayload2>(status2));
}

TEST(StatusPayloadUtils, GetPayload) {
  // Results undefined, but don't crash.
  GetPayload<googlesql_test::TestStatusPayload>(absl::OkStatus());

  absl::Status status1 = absl::Status(absl::StatusCode::kCancelled, "");
  // Results undefined, but don't crash.
  GetPayload<googlesql_test::TestStatusPayload>(status1);

  googlesql_test::TestStatusPayload status_payload1;
  status_payload1.set_value("msg1");

  googlesql_test::TestStatusPayload2 status_payload2;
  status_payload2.set_f1(15);
  status_payload2.set_f2(12);

  absl::Status status2 = absl::Status(absl::StatusCode::kCancelled, "");
  status2.SetPayload(StatusPayloadTypeUrl(), ToStatusCord(status_payload1));
  EXPECT_THAT(GetPayload<googlesql_test::TestStatusPayload>(status2),
              EqualsProto(status_payload1));
  // Results undefined, but don't crash.
  GetPayload<googlesql_test::TestStatusPayload2>(status2);

  absl::Status status3 = absl::Status(absl::StatusCode::kCancelled, "");
  status3.SetPayload(StatusPayloadTypeUrl(), ToStatusCord(status_payload1));
  status3.SetPayload(StatusPayload2TypeUrl(), ToStatusCord(status_payload2));
  EXPECT_THAT(GetPayload<googlesql_test::TestStatusPayload>(status3),
              EqualsProto(status_payload1));
  EXPECT_THAT(GetPayload<googlesql_test::TestStatusPayload2>(status3),
              EqualsProto(status_payload2));
}

TEST(StatusPayloadUtils, SetPayload_NoOpForOkStatus) {
  absl::Status status = absl::OkStatus();
  internal::AttachPayload(&status, googlesql_test::TestStatusPayload());

  EXPECT_FALSE(HasPayload(status));
}

TEST(StatusPayloadUtils, ErasePayloadTyped_NoOpForOkStatus) {
  absl::Status status = absl::OkStatus();
  ErasePayloadTyped<googlesql_test::TestStatusPayload>(&status);
  EXPECT_EQ(status, absl::OkStatus());
}

TEST(StatusPayloadUtils, ErasePayloadTyped_ClearEverything) {
  absl::Status status = absl::Status(absl::StatusCode::kCancelled, "");
  googlesql_test::TestStatusPayload payload;
  payload.set_value("my payload");

  status.SetPayload(StatusPayloadTypeUrl(), ToStatusCord(payload));
  ErasePayloadTyped<googlesql_test::TestStatusPayload>(&status);
  EXPECT_EQ(status, absl::Status(absl::StatusCode::kCancelled, ""));
  EXPECT_FALSE(HasPayload(status));
}

TEST(StatusPayloadUtils, ErasePayloadTyped_ClearOnlyOneThing) {
  googlesql_test::TestStatusPayload payload1;
  payload1.set_value("my payload");
  googlesql_test::TestStatusPayload2 payload2;
  payload2.set_f1(15);

  absl::Status status = absl::Status(absl::StatusCode::kCancelled, "msg");
  status.SetPayload(StatusPayloadTypeUrl(), ToStatusCord(payload1));
  status.SetPayload(StatusPayload2TypeUrl(), ToStatusCord(payload2));

  ErasePayloadTyped<googlesql_test::TestStatusPayload>(&status);

  absl::Status expected = absl::Status(absl::StatusCode::kCancelled, "msg");
  expected.SetPayload(StatusPayload2TypeUrl(), ToStatusCord(payload2));
  EXPECT_EQ(status, expected);
}

// Note: we only care about the output of ToString because other tests care
// about it. If we can relax the requirements of other tests in
// googlesql we can relax them here as well.
TEST(StatusPayloadUtils, ToString) {
  EXPECT_EQ(StatusToString(absl::OkStatus()), "OK");

  std::string cancelled_no_payload_str =
      StatusToString(absl::Status(absl::StatusCode::kCancelled, "msg"));
  EXPECT_THAT(cancelled_no_payload_str, HasSubstr("cancelled"));
  EXPECT_THAT(cancelled_no_payload_str, HasSubstr("msg"));

  absl::Status cancelled_with_payload =
      absl::Status(absl::StatusCode::kCancelled, "msg");
  googlesql_test::TestStatusPayload payload1;
  payload1.set_value("my payload");
  googlesql_test::TestStatusPayload2 payload2;
  payload2.set_f1(15);

  internal::AttachPayload(&cancelled_with_payload, payload1);
  internal::AttachPayload(&cancelled_with_payload, payload2);

  std::string cancelled_with_payload_str =
      StatusToString(cancelled_with_payload);

  EXPECT_THAT(cancelled_with_payload_str, HasSubstr("cancelled"));
  EXPECT_THAT(cancelled_with_payload_str, HasSubstr("msg"));
  EXPECT_THAT(cancelled_with_payload_str, HasSubstr(".TestStatusPayload"));
  EXPECT_THAT(cancelled_with_payload_str, HasSubstr("my payload"));
  EXPECT_THAT(cancelled_with_payload_str, HasSubstr("15"));
  EXPECT_THAT(cancelled_with_payload_str, HasSubstr(".TestStatusPayload2"));
}

TEST(StatusPayloadUtils, MergingOkIntoOkIsNoOp) {
  absl::Status ok_status1 = absl::OkStatus();
  absl::Status ok_status2 = absl::OkStatus();

  UpdateStatus(&ok_status1, ok_status2);
  GOOGLESQL_EXPECT_OK(ok_status1);
  GOOGLESQL_EXPECT_OK(ok_status2);
  EXPECT_NE(&ok_status1, &ok_status2);  // ownership is preserved
}

TEST(StatusPayloadUtils, MergingOkIntoErrorIsNoOp) {
  absl::Status ok_status = absl::OkStatus();
  absl::Status error(absl::StatusCode::kInternal, "Message 1");
  UpdateStatus(&error, ok_status);
  EXPECT_THAT(error,
              StatusIs(absl::StatusCode::kInternal, HasSubstr("Message 1")));
}

TEST(StatusPayloadUtils, MergingErrorIntoOkCopiesTheErrorIntoTheOkStatus) {
  absl::Status ok_status = absl::OkStatus();
  absl::Status error(absl::StatusCode::kInternal, "Message 1");
  UpdateStatus(&ok_status, error);
  EXPECT_THAT(ok_status,
              StatusIs(absl::StatusCode::kInternal, HasSubstr("Message 1")));
}

TEST(StatusPayloadUtils, MergingErrorIntoErrorAppendsTheMessageOnly) {
  // (&err1, err2) -> err1 + err2.message()
  absl::Status error1(absl::StatusCode::kInternal, "Message 1");
  absl::Status error2(absl::StatusCode::kInternal, "Message 2");

  UpdateStatus(&error1, error2);

  EXPECT_THAT(error1,
              StatusIs(absl::StatusCode::kInternal,
                       AllOf(HasSubstr("Message 1"), HasSubstr("Message 2"))));
  EXPECT_THAT(error2, StatusIs(absl::StatusCode::kInternal,
                               AllOf(Not(HasSubstr("Message 1")),
                                     HasSubstr("Message 2"))));
}

}  // namespace internal
}  // namespace googlesql
