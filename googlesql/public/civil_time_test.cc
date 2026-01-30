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

#include "googlesql/public/civil_time.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "gtest/gtest.h"
#include "absl/hash/hash_testing.h"
#include "absl/time/civil_time.h"
#include "absl/types/compare.h"

using googlesql::TimeValue;
using googlesql::DatetimeValue;

namespace {

void CheckTimeEqual(int expected_hour, int expected_minute, int expected_second,
                    int expected_nanos, TimeValue time) {
  EXPECT_EQ(expected_hour, time.Hour());
  EXPECT_EQ(expected_minute , time.Minute());
  EXPECT_EQ(expected_second, time.Second());
  EXPECT_EQ(expected_nanos, time.Nanoseconds());
  EXPECT_EQ(
      googlesql::TimeValue::FromHMSAndNanos(expected_hour, expected_minute,
                                            expected_second, expected_nanos),
      time);
}

void CheckDatetimeEqual(int expected_year, int expected_month, int expected_day,
                        int expected_hour, int expected_minute,
                        int expected_second, int expected_nanos,
                        DatetimeValue datetime) {
  EXPECT_EQ(expected_year, datetime.Year());
  EXPECT_EQ(expected_month, datetime.Month());
  EXPECT_EQ(expected_day, datetime.Day());
  EXPECT_EQ(expected_hour, datetime.Hour());
  EXPECT_EQ(expected_minute , datetime.Minute());
  EXPECT_EQ(expected_second, datetime.Second());
  EXPECT_EQ(expected_nanos, datetime.Nanoseconds());
  EXPECT_EQ(googlesql::DatetimeValue::FromYMDHMSAndNanos(
                expected_year, expected_month, expected_day, expected_hour,
                expected_minute, expected_second, expected_nanos),
            datetime);
}

// This test verifies that Time can be built correctly.
TEST(CivilTimeValuesTest, CorrectlyConstructingTime) {
  // default time value
  TimeValue time;
  ASSERT_TRUE(time.IsValid());
  CheckTimeEqual(0, 0, 0, 0, time);

  time = TimeValue::FromHMSAndNanos(13, 14, 15, 123456);
  ASSERT_TRUE(time.IsValid());
  CheckTimeEqual(13, 14, 15, 123456, time);

  EXPECT_NE(time, TimeValue::FromHMSAndMicros(13, 14, 15, 123456));

  time = TimeValue::FromHMSAndMicros(13, 14, 15, 123456);

  ASSERT_TRUE(time.IsValid());
  EXPECT_EQ(time, TimeValue::FromHMSAndMicros(13, 14, 15, 123456));
  CheckTimeEqual(13, 14, 15, 123456000, time);

  time = TimeValue::FromPacked32SecondsAndNanos(0xD38F, 123456);
  ASSERT_TRUE(time.IsValid());
  CheckTimeEqual(13, 14, 15, 123456, time);

  time = TimeValue::FromPacked32SecondsAndMicros(0xD38F, 123456);
  ASSERT_TRUE(time.IsValid());
  CheckTimeEqual(13, 14, 15, 123456000, time);

  time = TimeValue::FromPacked64Nanos(int64_t{0x34E3C001E240});
  ASSERT_TRUE(time.IsValid());
  CheckTimeEqual(13, 14, 15, 123456, time);

  time = TimeValue::FromPacked64Micros(int64_t{0xD38F1E240});
  ASSERT_TRUE(time.IsValid());
  CheckTimeEqual(13, 14, 15, 123456000, time);

  // invalid values
  time = TimeValue::FromHMSAndNanos(0, 0, 0, 1000000000);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, 0, 0, -1);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, 0, 60, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, 0, -1, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, 60, 0, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, -1, 0, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(24, 0, 0, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(-1, 0, 0, 0);
  ASSERT_FALSE(time.IsValid());

  // overflow in any field also makes it invalid
  time = TimeValue::FromHMSAndNanos(256, 0, 0, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, 256, 0, 0);
  ASSERT_FALSE(time.IsValid());
  time = TimeValue::FromHMSAndNanos(0, 0, 256, 0);
  ASSERT_FALSE(time.IsValid());
}

TEST(CivilTimeValuesTest, CorrectlyBuildingDatetime) {
  // default datetime value
  DatetimeValue datetime = DatetimeValue();
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(1970, 1, 1, 0, 0, 0, 0, datetime);

  datetime = DatetimeValue::FromYMDHMSAndNanos(
      2015, 10, 19, 16, 19, 37, 98765);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765, datetime);

  EXPECT_NE(datetime, DatetimeValue::FromYMDHMSAndMicros(2015, 10, 19, 16, 19,
                                                         37, 98765));

  datetime = DatetimeValue::FromYMDHMSAndMicros(
      2015, 10, 19, 16, 19, 37, 98765);

  EXPECT_EQ(datetime, DatetimeValue::FromYMDHMSAndMicros(2015, 10, 19, 16, 19,
                                                         37, 98765));
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765000, datetime);

  absl::CivilSecond civil_second(2015, 10, 19, 16, 19, 37);
  datetime = DatetimeValue::FromCivilSecondAndNanos(civil_second, 98765);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765, datetime);

  datetime = DatetimeValue::FromCivilSecondAndMicros(civil_second, 98765);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765000, datetime);

  datetime = DatetimeValue::FromPacked64SecondsAndNanos(0x1F7EA704E5, 98765);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765, datetime);

  datetime = DatetimeValue::FromPacked64SecondsAndMicros(0x1F7EA704E5, 98765);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765000, datetime);

  datetime = DatetimeValue::FromPacked64Micros(0X1F7EA704E5181CD);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(2015, 10, 19, 16, 19, 37, 98765000, datetime);

  // min DATETIME encodings
  datetime = DatetimeValue::FromPacked64SecondsAndNanos(0x4420000, 0);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(1, 1, 1, 0, 0, 0, 0, datetime);
  datetime = DatetimeValue::FromPacked64SecondsAndMicros(0x4420000, 0);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(1, 1, 1, 0, 0, 0, 0, datetime);
  datetime = DatetimeValue::FromPacked64Micros(0x442000000000);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(1, 1, 1, 0, 0, 0, 0, datetime);

  // max DATETIME encodings
  datetime = DatetimeValue::FromPacked64SecondsAndNanos(
      0x9C3F3F7EFB, 999999999);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(9999, 12, 31, 23, 59, 59, 999999999, datetime);
  datetime = DatetimeValue::FromPacked64SecondsAndMicros(
      0x9C3F3F7EFB, 999999);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(9999, 12, 31, 23, 59, 59, 999999000, datetime);
  datetime = DatetimeValue::FromPacked64Micros(
      0x9C3F3F7EFBF423F);
  ASSERT_TRUE(datetime.IsValid());
  CheckDatetimeEqual(9999, 12, 31, 23, 59, 59, 999999000, datetime);

  // invalid values
  datetime =
      DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 0, 0, 1000000000);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 0, 0, -1);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 0, 60, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 0, -1, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 60, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, -1, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 24, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, -1, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 32, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 0, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 13, 1, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 0, 1, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(10000, 1, 1, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(0, 1, 1, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());

  // overflow in any field also makes it invalid
  datetime = DatetimeValue::FromYMDHMSAndNanos(67506, 1, 1, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 257, 1, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 257, 0, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 256, 0, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 256, 0, 0);
  ASSERT_FALSE(datetime.IsValid());
  datetime = DatetimeValue::FromYMDHMSAndNanos(1970, 1, 1, 0, 0, 256, 0);
  ASSERT_FALSE(datetime.IsValid());
}

TEST(CivilTimeValuesTest, CorrectlyProduceDebugStrings) {
  TimeValue time =
      TimeValue::FromHMSAndNanos(5, 28, 56, 7654321);
  EXPECT_EQ("05:28:56.007654321", time.DebugString());
  time = TimeValue::FromHMSAndMicros(5, 28, 56, 120000);
  EXPECT_EQ("05:28:56.120", time.DebugString());
  time = TimeValue::FromHMSAndMicros(5, 28, 56, 0);
  EXPECT_EQ("05:28:56", time.DebugString());

  // invalid value
  time = TimeValue::FromHMSAndMicros(23, 59, 60, 0);
  EXPECT_EQ("[INVALID]", time.DebugString());
  time = TimeValue::FromHMSAndMicros(23, 59, 59, 1234567);
  EXPECT_EQ("[INVALID]", time.DebugString());

  DatetimeValue datetime = DatetimeValue::FromYMDHMSAndNanos(
      2006, 5, 13, 17, 5, 20, 7654321);
  EXPECT_EQ("2006-05-13 17:05:20.007654321", datetime.DebugString());
  datetime = DatetimeValue::FromYMDHMSAndMicros(
      2006, 5, 13, 17, 5, 20, 120000);
  EXPECT_EQ("2006-05-13 17:05:20.120", datetime.DebugString());
  datetime = DatetimeValue::FromYMDHMSAndMicros(
      2006, 5, 13, 17, 5, 0, 0);
  EXPECT_EQ("2006-05-13 17:05:00", datetime.DebugString());

  // invalid value
  datetime = DatetimeValue::FromYMDHMSAndMicros(
      2006, 5, 13, 17, 5, 60, 0);
  EXPECT_EQ("[INVALID]", datetime.DebugString());
  datetime = DatetimeValue::FromYMDHMSAndMicros(
      2006, 5, 13, 17, 5, 20, 1234567);
  EXPECT_EQ("[INVALID]", datetime.DebugString());
}

TEST(CivilTimeValuesTest, TimeEncodingDecodingTest) {
  EXPECT_EQ(TimeValue::FromHMSAndMicros(0, 0, 0, 0).Packed64TimeMicros(), 0);
  EXPECT_EQ(TimeValue().DebugString(),
            TimeValue::FromPacked64Micros(0).DebugString());

  std::vector<TimeValue> valid_test_cases = {
      TimeValue(),                                      // 00:00:00
      TimeValue::FromHMSAndMicros(0, 0, 0, 0),          // 00:00:00
      TimeValue::FromHMSAndMicros(23, 59, 59, 999999),  // 23:59:59.999999
      TimeValue::FromHMSAndMicros(12, 34, 56, 654321),  // 12:34:56.654321
  };

  for (const auto& each : valid_test_cases) {
    ASSERT_TRUE(each.IsValid()) << each.DebugString();
    EXPECT_EQ(each.DebugString(),
              TimeValue::FromPacked64Micros(each.Packed64TimeMicros())
                  .DebugString());
    EXPECT_EQ(each.DebugString(),
              TimeValue::FromPacked64Nanos(each.Packed64TimeNanos())
                  .DebugString());
  }

  std::vector<int64_t> invalid_encoded_cases = {
      0x1800000000,  // 24:00:00
      0xF0000000,    // 00:60:00
      0x3C00000,     // 00:00:60
      0xF4240,       // 00:00:00 with 1000000 in micros
      // The lower 37 bits actually decode to 12:34:56.654321, but there
      // something on the higher, unused bits, making it invalid.
      0x100000c8b89fbf1,
      -1,  // Any negative encoded value is inherently invalid.
  };
  for (int64_t encoded : invalid_encoded_cases) {
    TimeValue time = TimeValue::FromPacked64Micros(encoded);
    ASSERT_FALSE(time.IsValid()) << "Decoding " << encoded;
  }
}

TEST(CivilTimeValuesTest, DatetimeEncodingDecodingTest) {
  std::vector<DatetimeValue> valid_test_cases = {
      DatetimeValue(),  // 1970-01-01 00:00:00
      // 0001-01-01 00:00:00
      DatetimeValue::FromYMDHMSAndMicros(1, 1, 1, 0, 0, 0, 0),
      // 9999-12-31 23:59:59.999999
      DatetimeValue::FromYMDHMSAndMicros(9999, 12, 31, 23, 59, 59, 999999),
      // 2006-01-02 03:04:05.654321
      DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654321),
  };

  for (const auto& each : valid_test_cases) {
    ASSERT_TRUE(each.IsValid());
    EXPECT_EQ(each.DebugString(),
              DatetimeValue::FromPacked64SecondsAndMicros(
                  each.Packed64DatetimeSeconds(), each.Microseconds())
                  .DebugString());
    EXPECT_EQ(each.DebugString(),
              DatetimeValue::FromPacked64Micros(each.Packed64DatetimeMicros())
                  .DebugString());
  }

  std::vector<int64_t> invalid_encoded_cases = {
      0,                  // 0000-00-00 00:00:00
      0x9C4042000000000,  // 10000-01-01 00:00:00
      0x1ECB42000000000,  // 1970-13-01 00:00:00
      // 1970-01-32 00:00:00.
      // Actually there is an overflow on the day part, making it effectively
      // the same encoding as 1970-01-00 00:00:00, which is also invalid.
      0x1EC840000000000,
      0x1EC843800000000,  // 1970-01-01 24:00:00
      0x1EC8420F0000000,  // 1970-01-01 00:60:00
      0x1EC842003C00000,  // 1970-01-01 00:00:60
      0x1EC8420000F4240,  // 1970-01-01 00:00:00 with 1000000 in micros
      // The lower 60 bits actually decode to 2006-01-02 03:04:05.654321, but
      // there are something on the higher, unused bits, make it invalid.
      0X11EC842000000000,
      -1,  // Any negative encoded value is inherently invalid.
  };
  for (int64_t encoded : invalid_encoded_cases) {
    DatetimeValue datetime = DatetimeValue::FromPacked64Micros(encoded);
    ASSERT_FALSE(datetime.IsValid());
  }
}

TEST(CivilTimeValuesTest, TimeValueNormalizationTest) {
  // The second member is the debug string for the normalized TimeValue.
  std::vector<std::pair<TimeValue, std::string>> test_cases = {
      // leap seconds
      {TimeValue::FromHMSAndMicrosNormalized(23, 59, 60, 123456),
       "00:00:00.123456"},
      {TimeValue::FromHMSAndMicrosNormalized(12, 34, 60, 123456),
       "12:35:00.123456"},

      // out-of-range-second
      {TimeValue::FromHMSAndMicrosNormalized(12, 34, 70, 123456),
       "12:35:10.123456"},
      {TimeValue::FromHMSAndMicrosNormalized(12, 34, -10, 123456),
       "12:33:50.123456"},

      // out-of-range-minute
      {TimeValue::FromHMSAndMicrosNormalized(12, 70, 56, 123456),
       "13:10:56.123456"},
      {TimeValue::FromHMSAndMicrosNormalized(12, -10, 56, 123456),
       "11:50:56.123456"},

      // out-of-range-hour
      {TimeValue::FromHMSAndMicrosNormalized(30, 34, 56, 123456),
       "06:34:56.123456"},
      {TimeValue::FromHMSAndMicrosNormalized(-5, 34, 56, 123456),
       "19:34:56.123456"},
  };

  TimeValue time;
  std::string expected_time_string;
  for (const auto& each : test_cases) {
    ABSL_LOG(INFO) << "Testing case: " << each.second;
    time = each.first;
    ASSERT_TRUE(time.IsValid());
    EXPECT_EQ(each.second, time.DebugString());
  }

  // Special cases
  // Normalized on a valid time does nothing.
  time = TimeValue::FromHMSAndMicrosNormalized(12, 34, 56, 123456);
  expected_time_string = "12:34:56.123456";
  ASSERT_TRUE(time.IsValid());
  EXPECT_EQ(expected_time_string, time.DebugString());

  // Normalizing a time with too large sub-second part
  time = TimeValue::FromHMSAndNanosNormalized(12, 34, 56, 1234567890);
  ASSERT_TRUE(time.IsValid());
  EXPECT_EQ("12:34:57.234567890", time.DebugString());

  // Normalizing a time with a negative sub-second part
  time = TimeValue::FromHMSAndNanosNormalized(12, 34, 56, -10);
  ASSERT_TRUE(time.IsValid());
  EXPECT_EQ("12:34:55.999999990", time.DebugString());

  time = TimeValue::FromHMSAndNanosNormalized(12, 34, 56, -1000000010);
  ASSERT_TRUE(time.IsValid());
  EXPECT_EQ("12:34:54.999999990", time.DebugString());
}

TEST(CivilTimeValuesTest, DatetimeValueNormalizationTest) {
  // The second member is the debug string for the normalized DatetimeValue.
  std::vector<std::pair<DatetimeValue, std::string>> test_cases = {
      // leap second
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2015, 11, 6, 12, 59, 60,
                                                    123456),
       "2015-11-06 13:00:00.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2015, 12, 31, 23, 59, 60,
                                                    123456),
       "2016-01-01 00:00:00.123456"},

      // out-of-range second
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 3, 4, 70,
                                                    123456),
       "2006-01-02 03:05:10.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 3, 4, -5,
                                                    123456),
       "2006-01-02 03:03:55.123456"},

      // out-of-range minute
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 3, 70, 5,
                                                    123456),
       "2006-01-02 04:10:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 3, -5, 5,
                                                    123456),
       "2006-01-02 02:55:05.123456"},

      // out-of-range hour
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 30, 4, 5,
                                                    123456),
       "2006-01-03 06:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, -5, 4, 5,
                                                    123456),
       "2006-01-01 19:04:05.123456"},

      // invalid day-of-month
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 0, 3, 4, 5,
                                                    123456),
       "2005-12-31 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, -1, 3, 4, 5,
                                                    123456),
       "2005-12-30 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 32, 3, 4, 5,
                                                    123456),
       "2006-02-01 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 12, 32, 3, 4, 5,
                                                    123456),
       "2007-01-01 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 2, 29, 3, 4, 5,
                                                    123456),
       "2006-03-01 03:04:05.123456"},

      // out-of-range month
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 0, 2, 3, 4, 5,
                                                    123456),
       "2005-12-02 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, -1, 2, 3, 4, 5,
                                                    123456),
       "2005-11-02 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 13, 2, 3, 4, 5,
                                                    123456),
       "2007-01-02 03:04:05.123456"},

      // invalid day-of-month after normalizing out-of-range month
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 14, 31, 3, 4, 5,
                                                    123456),
       "2007-03-03 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2007, 14, 31, 3, 4, 5,
                                                    123456),
       "2008-03-02 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, -1, 31, 3, 4, 5,
                                                    123456),
       "2005-12-01 03:04:05.123456"},

      // out-of-range year, but brought back by an out-of-range value on some
      // other field
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(0, 12, 31, 23, 59, 60,
                                                    123456),
       "0001-01-01 00:00:00.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(0, 12, 31, 23, 60, 5,
                                                    123456),
       "0001-01-01 00:00:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(0, 12, 31, 24, 4, 5,
                                                    123456),
       "0001-01-01 00:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(0, 12, 32, 3, 4, 5, 123456),
       "0001-01-01 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(0, 13, 2, 3, 4, 5, 123456),
       "0001-01-02 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(10000, 1, 1, 0, 0, -1,
                                                    123456),
       "9999-12-31 23:59:59.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(10000, 1, 1, 0, -1, 5,
                                                    123456),
       "9999-12-31 23:59:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(10000, 1, 1, -1, 4, 5,
                                                    123456),
       "9999-12-31 23:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(10000, 1, 0, 3, 4, 5,
                                                    123456),
       "9999-12-31 03:04:05.123456"},
      {DatetimeValue::FromYMDHMSAndMicrosNormalized(10000, 0, 2, 3, 4, 5,
                                                    123456),
       "9999-12-02 03:04:05.123456"},
  };

  DatetimeValue datetime;
  std::string expected_datetime_string;
  for (const auto& each : test_cases) {
    ABSL_LOG(INFO) << "Testing case: " << each.first.DebugString();

    // Does not clear sub-second
    datetime = each.first;
    EXPECT_TRUE(datetime.IsValid());
    EXPECT_EQ(each.second, datetime.DebugString());
  }

  // Special cases
  // Normalizing a valid datetime does nothing.
  datetime =
      DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 3, 4, 5, 123456);
  ASSERT_TRUE(datetime.IsValid());
  EXPECT_EQ("2006-01-02 03:04:05.123456", datetime.DebugString());

  // Normalizing a datetime with too large sub-second part
  datetime = DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 12, 34,
                                                          56, 1234567);
  ASSERT_TRUE(datetime.IsValid());
  EXPECT_EQ("2006-01-02 12:34:57.234567", datetime.DebugString());

  // Normalizing a datetime with a negative sub-second part
  datetime =
      DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 12, 34, 56, -50);
  ASSERT_TRUE(datetime.IsValid());
  EXPECT_EQ("2006-01-02 12:34:55.999950", datetime.DebugString());

  datetime = DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 12, 34,
                                                          56, -1234567);
  ASSERT_TRUE(datetime.IsValid());
  EXPECT_EQ("2006-01-02 12:34:54.765433", datetime.DebugString());

  // Normalizing a datetime could result in an out-of-range value
  datetime = DatetimeValue::FromYMDHMSAndMicrosNormalized(9999, 12, 31, 23, 59,
                                                          61, 123456);
  ASSERT_FALSE(datetime.IsValid());
}

TEST(CivilTimeValuesTest, HashConsistencyForDateTime) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      DatetimeValue(),  // 1970-01-01 00:00:00
      // 0001-01-01 00:00:00
      DatetimeValue::FromYMDHMSAndMicros(1, 1, 1, 0, 0, 0, 0),
      // 9999-12-31 23:59:59.999999
      DatetimeValue::FromYMDHMSAndMicros(9999, 12, 31, 23, 59, 59, 999999),
      // 2006-01-02 03:04:05.654321
      DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654321),
      DatetimeValue::FromYMDHMSAndMicrosNormalized(2006, 1, 2, 3, 4, 5, 123456),
      DatetimeValue::FromYMDHMSAndMicrosNormalized(10000, 0, 2, 3, 4, 5,
                                                   123456),
      // Invalid case
      DatetimeValue::FromPacked64Micros(0x1EC8420F0000000),
  }));
}
TEST(CivilTimeValuesTest, HashConsistencyForTime) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      TimeValue(),
      TimeValue::FromHMSAndMicros(0, 0, 0, 0),
      TimeValue::FromHMSAndMicros(23, 59, 59, 999999),
      TimeValue::FromHMSAndMicros(12, 34, 56, 654321),
      TimeValue::FromHMSAndMicrosNormalized(12, 34, 56, 123456),
      // Invalid case
      TimeValue::FromHMSAndMicros(23, 59, 59, -10),
  }));
}

TEST(CivilTimeValuesTest, ComparisonOperatorsForDatetime) {
#ifndef __cpp_impl_three_way_comparison
  GTEST_SKIP() << "<=> operator not supported";
#else
  DatetimeValue invalid = DatetimeValue::FromPacked64Micros(0x1EC8420F0000000);

  EXPECT_EQ(DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654321) <=>
                DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654321),
            std::partial_ordering::equivalent);
  EXPECT_EQ(DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 6, 654321) <=>
                DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654321),
            std::partial_ordering::greater);
  EXPECT_EQ(DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654320) <=>
                DatetimeValue::FromYMDHMSAndMicros(2006, 1, 2, 3, 4, 5, 654321),
            std::partial_ordering::less);
  EXPECT_EQ(invalid <=> DatetimeValue(), std::partial_ordering::unordered);
  EXPECT_EQ(DatetimeValue() <=> invalid, std::partial_ordering::unordered);
#endif
}

TEST(CivilTimeValuesTest, ComparisonOperatorsForTime) {
#ifndef __cpp_impl_three_way_comparison
  GTEST_SKIP() << "<=> operator not supported";
#else
  TimeValue invalid = TimeValue::FromHMSAndMicros(23, 59, 60, 0);

  EXPECT_EQ(TimeValue::FromHMSAndMicros(23, 59, 51, 7) <=>
                TimeValue::FromHMSAndMicros(23, 59, 51, 7),
            std::partial_ordering::equivalent);
  EXPECT_EQ(TimeValue::FromHMSAndMicros(23, 59, 52, 7) <=>
                TimeValue::FromHMSAndMicros(23, 59, 51, 7),
            std::partial_ordering::greater);
  EXPECT_EQ(TimeValue::FromHMSAndMicros(23, 59, 50, 7) <=>
                TimeValue::FromHMSAndMicros(23, 59, 51, 7),
            std::partial_ordering::less);
  EXPECT_EQ(invalid <=> TimeValue(), std::partial_ordering::unordered);
  EXPECT_EQ(TimeValue() <=> invalid, std::partial_ordering::unordered);
#endif
}

}  // namespace
