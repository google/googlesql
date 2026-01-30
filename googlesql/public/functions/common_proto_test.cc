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

#include "googlesql/public/functions/common_proto.h"

#include <string>

#include "google/protobuf/wrappers.pb.h"
#include "google/type/timeofday.pb.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {
namespace {

static google::type::TimeOfDay MakeTimeOfDay(int hours, int minutes,
                                             int seconds, int nanos) {
  google::type::TimeOfDay result;
  result.set_hours(hours);
  result.set_minutes(minutes);
  result.set_seconds(seconds);
  result.set_nanos(nanos);
  return result;
}

TEST(CommonProtoTest, ConvertProto3TimeOfDay) {
  struct ConvertProto3TimeOfDay {
    google::type::TimeOfDay time;
    TimestampScale scale;
    TimeValue expected;
    std::string expected_error;
  };
  ConvertProto3TimeOfDay kTestInput[] = {
      {MakeTimeOfDay(11, 30, 25, 123456), kNanoseconds,
       TimeValue::FromHMSAndNanos(11, 30, 25, 123456), ""},
      {MakeTimeOfDay(12, 00, 00, 999999999), kMicroseconds,
       TimeValue::FromHMSAndNanos(12, 00, 00, 999999000), ""},
      {MakeTimeOfDay(19, 25, 45, 0), kNanoseconds,
       TimeValue::FromHMSAndNanos(19, 25, 45, 0), ""},
      {MakeTimeOfDay(22, 01, 01, 100), kMicroseconds,
       TimeValue::FromHMSAndNanos(22, 01, 01, 0), ""},
      {MakeTimeOfDay(01, 59, 59, 1), kNanoseconds,
       TimeValue::FromHMSAndNanos(01, 59, 59, 1), ""},
      {MakeTimeOfDay(00, 00, 00, 00), kNanoseconds,
       TimeValue::FromHMSAndNanos(00, 00, 00, 00), ""},
      {MakeTimeOfDay(23, 59, 59, 999999999), kNanoseconds,
       TimeValue::FromHMSAndNanos(23, 59, 59, 999999999), ""},
      {MakeTimeOfDay(00, 00, 00, -1), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(24, 00, 00, 00), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(-1, 30, 25, 123456), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(24, 00, 05, 123456), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(2, -1, 30, 123456), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(2, 60, 13, 123), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(3, 30, -1, 123456), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(3, 30, 60, 123), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(3, 30, 30, -20), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"},
      {MakeTimeOfDay(3, 30, 29, 1390065408), kNanoseconds,
       TimeValue::FromHMSAndNanos(-1, -1, -1, -1), "Invalid Proto3"}};

  for (const ConvertProto3TimeOfDay& test : kTestInput) {
    const std::string test_input_string = absl::StrCat(
        "proto3_time_of_day_to_time(", test.time.DebugString(), ")");
    TimeValue actual;
    const absl::Status status =
        ConvertProto3TimeOfDayToTime(test.time, test.scale, &actual);
    if (test.expected_error.empty()) {
      GOOGLESQL_ASSERT_OK(status) << test_input_string;
      ASSERT_EQ(test.expected.Packed64TimeNanos(), actual.Packed64TimeNanos())
          << test_input_string;
    } else {
      EXPECT_THAT(status, absl_testing::StatusIs(
                              absl::StatusCode::kOutOfRange,
                              testing::HasSubstr(test.expected_error)))
          << test_input_string;
    }
  }
}

TEST(CommonProtoTest, ConvertTimeOfDayToProto3) {
  struct ConvertTimeOfDayToProto3 {
    TimeValue time;
    std::string expected_error;
  };
  ConvertTimeOfDayToProto3 kTestInput[] = {
      {TimeValue::FromHMSAndNanos(11, 30, 25, 123456), ""},
      {TimeValue::FromHMSAndNanos(12, 00, 00, 99999999), ""},
      {TimeValue::FromHMSAndNanos(19, 25, 45, 0), ""},
      {TimeValue::FromHMSAndNanos(22, 01, 01, 100), ""},
      {TimeValue::FromHMSAndNanos(01, 59, 59, 1), ""},
      {TimeValue::FromHMSAndNanos(-1, 30, 25, 123456),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(24, 00, 05, 123456),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(2, -1, 30, 123456),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(2, 60, 13, 123),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(3, 30, -1, 123456),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(3, 30, 61, 123),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(3, 30, 30, -20),
       "Input is outside of Proto3 TimeOfDay range"},
      {TimeValue::FromHMSAndNanos(3, 30, 29, 1390065408),
       "Input is outside of Proto3 TimeOfDay range"}};

  for (const ConvertTimeOfDayToProto3& test : kTestInput) {
    const std::string test_input_string =
        absl::StrCat("time_to_time_of_day(", test.time.DebugString(), ")");
    google::type::TimeOfDay result;
    const absl::Status status =
        ConvertTimeToProto3TimeOfDay(test.time, &result);
    if (test.expected_error.empty()) {
      TimeValue expected;
      GOOGLESQL_ASSERT_OK(status) << test_input_string;
      GOOGLESQL_ASSERT_OK(ConvertProto3TimeOfDayToTime(result, kNanoseconds, &expected))
          << test_input_string;
      ASSERT_EQ(test.time.Packed64TimeNanos(), expected.Packed64TimeNanos())
          << test_input_string;
    } else {
      EXPECT_THAT(status, absl_testing::StatusIs(
                              absl::StatusCode::kOutOfRange,
                              testing::HasSubstr(test.expected_error)))
          << test_input_string;
    }
  }
}

TEST(CommonProtoTest, ConvertProto3Wrappers) {
  absl::Status status;
  google::protobuf::BoolValue bool_proto;
  bool_proto.set_value(true);
  bool bool_output;
  status = ConvertProto3WrapperToType<google::protobuf::BoolValue>(
      bool_proto, &bool_output);
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(true, bool_output);

  // Specifically testing the conversion from
  // string->google::protobuf::StringValue as it is the only case where an error
  // can occur.
  google::protobuf::StringValue string_proto;
  string_proto.set_value("Valid UTF8 string");
  std::string string_output;
  status = ConvertProto3WrapperToType<google::protobuf::StringValue>(
      string_proto, &string_output);
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ("Valid UTF8 string", string_output);

  string_proto.set_value("\xA4");
  status = ConvertProto3WrapperToType<google::protobuf::StringValue>(
      string_proto, &string_output);
  EXPECT_EQ(absl::StatusCode::kOutOfRange, status.code());
}

TEST(CommonProtoTest, ConvertTypeToProto3Wrapper) {
  // A couple simple tests, as the implementation is trivial.
  const bool bool_input = true;
  google::protobuf::BoolValue bool_proto;
  ConvertTypeToProto3Wrapper<google::protobuf::BoolValue>(bool_input,
                                                          &bool_proto);
  EXPECT_EQ(true, bool_proto.value());

  const absl::Cord bytes_input("\xA4");
  google::protobuf::BytesValue bytes_proto;
  ConvertTypeToProto3Wrapper<google::protobuf::BytesValue>(bytes_input,
                                                           &bytes_proto);
  EXPECT_EQ("\xA4", bytes_proto.value());
}

}  // namespace
}  // namespace functions
}  // namespace googlesql
