//
// Copyright 2021 Google LLC
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

#include "googlesql/base/testing/proto_matchers_oss.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/test_payload.pb.h"

namespace {

using googlesql::testing::EqualsProto;
using googlesql_base::TestPayload;
using testing::Not;

TEST(EqualsProtoTest, PayloadType) {
  EXPECT_THAT(TestPayload{}, EqualsProto(absl::string_view{}));

  {
    TestPayload msg;
    msg.set_message("foobar");

    EXPECT_THAT(msg, EqualsProto(msg));
    EXPECT_THAT(msg, EqualsProto(R"pb(
                  message: "foobar"
                )pb"));
    EXPECT_THAT(msg, Not(EqualsProto(absl::string_view{})));
  }
}

}  // namespace
