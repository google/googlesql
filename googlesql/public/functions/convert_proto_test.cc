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

#include "googlesql/public/functions/convert_proto.h"

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::HasSubstr;
using ::testing::_;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace functions {
namespace {

TEST(ConvertProtoTest, BasicPrintingCord) {
  googlesql_test::KitchenSinkPB proto;
  absl::Status error;
  absl::Cord out;

  EXPECT_TRUE(ProtoToString(&proto, &out, &error));
  EXPECT_EQ(std::string(out), "");
  GOOGLESQL_EXPECT_OK(error);

  proto.set_int64_val(1984);

  EXPECT_TRUE(ProtoToString(&proto, &out, &error));
  EXPECT_EQ(std::string(out), "int64_val: 1984");
  GOOGLESQL_EXPECT_OK(error);

  out.Clear();
  proto.set_string_val("spam");

  EXPECT_TRUE(ProtoToString(&proto, &out, &error));
  EXPECT_EQ(std::string(out), "int64_val: 1984 string_val: \"spam\"");
  GOOGLESQL_EXPECT_OK(error);
}

TEST(ConvertProtoTest, BasicParsing) {
  googlesql_test::KitchenSinkPB proto;
  absl::Status error;

  EXPECT_TRUE(StringToProto("int64_key_1: 1 int64_key_2: 2", &proto, &error));
  EXPECT_EQ(proto.int64_key_1(), 1);
  EXPECT_EQ(proto.int64_key_2(), 2);
  GOOGLESQL_EXPECT_OK(error);

  EXPECT_TRUE(StringToProto(
      "int64_key_1: 1 int64_key_2: 2 string_val: \"spam\"", &proto, &error));
  EXPECT_EQ(proto.int64_key_1(), 1);
  EXPECT_EQ(proto.int64_key_2(), 2);
  EXPECT_EQ(proto.string_val(), "spam");
  GOOGLESQL_EXPECT_OK(error);
}

TEST(ConvertProtoTest, ParsingWithoutRequiredField) {
  googlesql_test::KitchenSinkPB proto;
  absl::Status error;

  EXPECT_FALSE(StringToProto("", &proto, &error));
  EXPECT_THAT(
      error,
      StatusIs(
          _, HasSubstr("Error parsing proto: Message missing required fields: "
                       "int64_key_1, int64_key_2 [0:1]")));
}

TEST(ConvertProtoTest, ParsingWithUnknownField) {
  googlesql_test::KitchenSinkPB proto;
  absl::Status error;

  EXPECT_FALSE(
      StringToProto("int64_key_1: 1 int64_key_2: 2 123: 4", &proto, &error));
  EXPECT_THAT(
      error,
      StatusIs(
          _, HasSubstr(
                 "Error parsing proto: Expected identifier, got: 123 [1:31]")));
}

TEST(ConvertProtoTest, ParsingWithExtensionsCord) {
  googlesql_test::KitchenSinkPB proto;
  absl::Status error;
  absl::Cord out;

  EXPECT_TRUE(
      StringToProto("int64_key_1: 1 int64_key_2: 2 "
                    "[googlesql_test.KitchenSinkExtension.int_extension]: 1234",
                    &proto, &error));
  EXPECT_EQ(
      proto.GetExtension(googlesql_test::KitchenSinkExtension::int_extension),
      1234);
  GOOGLESQL_EXPECT_OK(error);
}

TEST(ConvertProtoTest, ParsingWithUnknownExtensionCord) {
  googlesql_test::KitchenSinkPB proto;
  absl::Status error;
  absl::Cord out;

  EXPECT_FALSE(
      StringToProto("int64_key_1: 1 int64_key_2: 2 "
                    "[googlesql_test.UnknownExtension.int_extension]: 1234",
                    &proto, &error));
  EXPECT_THAT(
      error,
      StatusIs(_, HasSubstr("Error parsing proto: Extension \"googlesql_test."
                            "UnknownExtension.int_extension\" is not defined or"
                            " is not an extension of \"googlesql_test."
                            "KitchenSinkPB\".")));
}

}  // namespace
}  // namespace functions
}  // namespace googlesql
