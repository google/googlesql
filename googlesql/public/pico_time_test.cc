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

#include "googlesql/public/pico_time.h"

#include <cstdint>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/types/timestamp_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace googlesql {
namespace {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

class PicoTimeTest : public testing::Test {
 public:
  static absl::Time ParseTime(absl::string_view str) {
    absl::Time timestamp;
    std::string err;
    bool success =
        absl::ParseTime("%Y-%m-%d %H:%M:%E*S%Ez", str, &timestamp, &err);
    if (success) {
      return timestamp;
    }

    ABSL_LOG(FATAL) << "Cannot parse string as time: " << str << ", error: " << err;
  }
};

TEST_F(PicoTimeTest, MinAndMax) {
  EXPECT_EQ(PicoTime::MinValue().DebugString(), "0001-01-01 00:00:00+00");
  EXPECT_EQ(PicoTime::MaxValue().DebugString(),
            "9999-12-31 23:59:59.999999999999+00");
}

TEST_F(PicoTimeTest, Create) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto time,
      PicoTime::Create(ParseTime("1234-01-02 03:04:05+00"), 123456789123));
  EXPECT_EQ(time.DebugString(), "1234-01-02 03:04:05.123456789123+00");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      time,
      PicoTime::Create(ParseTime("2234-01-02 03:04:05+00"), 123456789321));
  EXPECT_EQ(time.DebugString(), "2234-01-02 03:04:05.123456789321+00");

  EXPECT_THAT(
      PicoTime::Create(ParseTime("1234-01-02 03:04:05.12+00"), 123456789123),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Time argument must not contain subseconds value")));
  EXPECT_THAT(
      PicoTime::Create(ParseTime("1234-01-02 03:04:05+00"), 999999999999 + 10),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Picoseconds argument must be less than "
                         "1000000000000. Found 1000000000009")));

  EXPECT_THAT(PicoTime::Create(ParseTime("0000-12-31 0:0:0+00"), 0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("PicoTime is out of range between 0001-01-01 "
                                 "00:00:00+00 "
                                 "and 9999-12-31 23:59:59.999999999999+00")));
  EXPECT_THAT(PicoTime::Create(ParseTime("10000-01-01 0:0:0+00"), 0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("PicoTime is out of range between 0001-01-01 "
                                 "00:00:00+00 "
                                 "and 9999-12-31 23:59:59.999999999999+00")));
}

TEST_F(PicoTimeTest, Comparison) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto time1,
      PicoTime::Create(ParseTime("1234-01-02 03:04:05+00"), 123456789123));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto time2,
      PicoTime::Create(ParseTime("1234-01-02 03:04:05+00"), 123456789124));
  ASSERT_LT(time1, time2);
  ASSERT_LE(time1, time2);
  ASSERT_GT(time2, time1);
  ASSERT_GE(time2, time1);
  ASSERT_NE(time1, time2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      time1,
      PicoTime::Create(ParseTime("2234-01-02 03:04:05+00"), 123456789123));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      time2,
      PicoTime::Create(ParseTime("2234-01-02 03:04:05+00"), 123456789124));
  ASSERT_LT(time1, time2);
  ASSERT_LE(time1, time2);
  ASSERT_GT(time2, time1);
  ASSERT_GE(time2, time1);
  ASSERT_NE(time1, time2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      time1,
      PicoTime::Create(ParseTime("2234-01-02 03:04:05+00"), 123456789123));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      time2,
      PicoTime::Create(ParseTime("2234-01-02 03:04:05+00"), 123456789123));
  ASSERT_EQ(time1, time2);
}

TEST_F(PicoTimeTest, ToUnixPicos) {
  EXPECT_EQ(PicoTime::Create(absl::FromUnixMicros(types::kTimestampMin))
                ->ToUnixPicos(),
            absl::int128(types::kTimestampMin) * 1000000);
  EXPECT_EQ(PicoTime::Create(absl::FromUnixMicros(types::kTimestampMax))
                ->ToUnixPicos(),
            absl::int128(types::kTimestampMax) * 1000000);
  EXPECT_EQ(PicoTime::Create(absl::FromUnixMicros(1234567890))->ToUnixPicos(),
            absl::int128(1234567890) * 1000000);
  EXPECT_EQ(PicoTime::Create(absl::FromUnixMicros(-1234567890))->ToUnixPicos(),
            absl::int128(-1234567890) * 1000000);
}

// Test methods ToAbslTime() and SubNanoseconds() together.
TEST_F(PicoTimeTest, ToAbslTimeAndSubNanoseconds) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  EXPECT_EQ(PicoTime::MinValue().ToAbslTime(),
            absl::FromCivil(absl::CivilSecond(1, 1, 1), utc));
  EXPECT_EQ(PicoTime::MinValue().SubNanoseconds(), 0);

  EXPECT_EQ(PicoTime::MaxValue().ToAbslTime(),
            absl::FromCivil(absl::CivilSecond(10000, 01, 01, 00, 00, 00), utc) -
                absl::Nanoseconds(1));
  EXPECT_EQ(PicoTime::MaxValue().SubNanoseconds(), 999);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto time,
      PicoTime::Create(ParseTime("1234-01-02 03:04:05+00"), 123456789123));
  EXPECT_EQ(time.ToAbslTime(),
            absl::FromCivil(absl::CivilSecond(1234, 1, 2, 3, 4, 5), utc) +
                absl::Nanoseconds(123456789));
  EXPECT_EQ(time.SubNanoseconds(), 123);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      time,
      PicoTime::Create(ParseTime("2234-01-02 03:04:05+00"), 123456789321));
  EXPECT_EQ(time.ToAbslTime(),
            absl::FromCivil(absl::CivilSecond(2234, 1, 2, 3, 4, 5), utc) +
                absl::Nanoseconds(123456789));
  EXPECT_EQ(time.SubNanoseconds(), 321);
}

struct ToStringTestCase {
  std::string absl_time;
  uint64_t picoseconds;
  std::string expected_string;
};

class ToStringTest : public ::testing::TestWithParam<ToStringTestCase> {};

TEST_P(ToStringTest, ToStringTest) {
  const ToStringTestCase test = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto time,
                       PicoTime::Create(PicoTimeTest::ParseTime(test.absl_time),
                                        test.picoseconds));
  EXPECT_EQ(time.ToString(), test.expected_string);
}

INSTANTIATE_TEST_SUITE_P(ToStringTests, ToStringTest,
                         ::testing::ValuesIn<ToStringTestCase>({
                             {"1234-01-02 03:04:05+00", 123'456'789'001,
                              "1234-01-02 03:04:05.123456789001+00"},
                             {"1234-01-02 03:04:05+00", 123'456'789'010,
                              "1234-01-02 03:04:05.12345678901+00"},
                             {"1234-01-02 03:04:05+00", 123'456'781'000,
                              "1234-01-02 03:04:05.123456781+00"},
                             {"1234-01-02 03:04:05+00", 123'456'710'000,
                              "1234-01-02 03:04:05.12345671+00"},
                             {"1234-01-02 03:04:05+00", 123'456'789'012,
                              "1234-01-02 03:04:05.123456789012+00"},
                         }));

struct PrecisionTestCase {
  std::string absl_time;
  uint64_t picoseconds;
  int expected_precision;
};

class PrecisionTest : public ::testing::TestWithParam<PrecisionTestCase> {};

TEST_P(PrecisionTest, PrecisionTest) {
  const PrecisionTestCase test = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto time,
                       PicoTime::Create(PicoTimeTest::ParseTime(test.absl_time),
                                        test.picoseconds));
  EXPECT_EQ(time.Precision(), test.expected_precision);
}

INSTANTIATE_TEST_SUITE_P(PrecisionTests, PrecisionTest,
                         ::testing::ValuesIn<PrecisionTestCase>({
                             {"1234-01-02 03:04:05+00", 123'456'789'001, 12},
                             {"1234-01-02 03:04:05+00", 123'456'789'010, 12},
                             {"1234-01-02 03:04:05+00", 123'456'781'000, 9},
                             {"1234-01-02 03:04:05+00", 123'456'710'000, 9},
                             {"1234-01-02 03:04:05+00", 123'456'000'000, 6},
                             {"1234-01-02 03:04:05+00", 123'000'000'000, 3},
                             {"1234-01-02 03:04:05+00", 0, 0},
                         }));
}  // namespace
}  // namespace googlesql
