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

#include "googlesql/public/timestamp_picos_value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/types/timestamp_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/hash/hash_testing.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace googlesql {
namespace {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

class TimestampPicosTest : public testing::Test {};

TEST_F(TimestampPicosTest, FromAndToTime) {
  std::vector<int64_t> picos = {
      1704164645123456789,
      -2208891354876543211,
  };

  for (int64_t pico : picos) {
    absl::Time time = absl::FromUnixNanos(pico);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(TimestampPicosValue value,
                         TimestampPicosValue::Create(time));
    EXPECT_EQ(value.ToAbslTime(), time);
  }

  EXPECT_THAT(TimestampPicosValue::Create(
                  absl::FromUnixMicros(types::kTimestampMin - 1)),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("out of allowed range")));
  EXPECT_THAT(TimestampPicosValue::Create(
                  absl::FromUnixMicros(types::kTimestampMax + 1)),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("out of allowed range")));
}

TEST_F(TimestampPicosTest, MinAndMax) {
  EXPECT_EQ(TimestampPicosValue::MinValue().ToString(),
            "0001-01-01 00:00:00+00");
  EXPECT_EQ(absl::StrCat(TimestampPicosValue::MinValue().ToUnixPicos()),
            "-62135596800000000000000");

  EXPECT_EQ(TimestampPicosValue::MaxValue().ToString(),
            "9999-12-31 23:59:59.999999999999+00");
  EXPECT_EQ(absl::StrCat(TimestampPicosValue::MaxValue().ToUnixPicos()),
            "253402300799999999999999");
}

TEST_F(TimestampPicosTest, SerializeAndDeserialize) {
  std::vector<TimestampPicosValue> picos = {
      TimestampPicosValue(),
      *TimestampPicosValue::Create(absl::FromUnixSeconds(1234567890)),
      TimestampPicosValue::MinValue(),
      TimestampPicosValue::MaxValue(),
  };

  for (const TimestampPicosValue& t1 : picos) {
    std::string serialized = t1.SerializeAsProtoBytes();

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        TimestampPicosValue t2,
        TimestampPicosValue::DeserializeFromProtoBytes(serialized));
    EXPECT_EQ(t1.ToPicoTime(), t2.ToPicoTime());
    EXPECT_EQ(t1, t2);
  }

  // Directly check the serialized results.
  EXPECT_EQ(absl::BytesToHexString(
                TimestampPicosValue::MaxValue().SerializeAsProtoBytes()),
            "ffff977bab4a5cf7a835000000000000");
  EXPECT_EQ(absl::BytesToHexString(
                TimestampPicosValue::MinValue().SerializeAsProtoBytes()),
            "000090ade64e5f9fd7f2ffffffffffff");

  // Invalid input.
  EXPECT_THAT(TimestampPicosValue::DeserializeFromProtoBytes("invalid"),
              StatusIs(absl::StatusCode::kOutOfRange));
}

constexpr int64_t kAscendingNanoseconds[] = {
    -1000000001, -1000000000, 0, 1000000000, 1000000001, 1234567890,
};

TEST_F(TimestampPicosTest, ComparisonOps) {
  size_t n = sizeof(kAscendingNanoseconds) / sizeof(kAscendingNanoseconds[0]);
  std::vector<TimestampPicosValue> values;
  values.resize(n);
  for (size_t i = 0; i < n; ++i) {
    values[i] = *TimestampPicosValue::Create(
        absl::FromUnixNanos(kAscendingNanoseconds[i]));
  }
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      EXPECT_EQ(values[i] == values[j], i == j);
      EXPECT_EQ(values[i] != values[j], i != j);
      EXPECT_EQ(values[i] < values[j], i < j);
      EXPECT_EQ(values[i] > values[j], i > j);
      EXPECT_EQ(values[i] <= values[j], i <= j);
      EXPECT_EQ(values[i] >= values[j], i >= j);
    }
  }
}

TEST_F(TimestampPicosTest, HashCode) {
  EXPECT_NE(TimestampPicosValue::Create(absl::FromUnixSeconds(1))->HashCode(),
            TimestampPicosValue::Create(absl::FromUnixSeconds(2))->HashCode());
  EXPECT_NE(TimestampPicosValue::Create(absl::FromUnixNanos(1))->HashCode(),
            TimestampPicosValue::Create(absl::FromUnixNanos(2))->HashCode());
  EXPECT_EQ(
      TimestampPicosValue::Create(absl::FromUnixSeconds(1))->HashCode(),
      TimestampPicosValue::Create(absl::FromUnixNanos(1000000000))->HashCode());
}

// TODO: Add random value tests with round trip between FromString
// and ToString.
TEST_F(TimestampPicosTest, FromString) {
  absl::TimeZone pst;
  ASSERT_TRUE(absl::LoadTimeZone("America/Los_Angeles", &pst));

  EXPECT_EQ(TimestampPicosValue::FromString("2024-08-19 12:34:56.123456789",
                                            absl::UTCTimeZone(),
                                            /*allow_tz_in_str=*/true)
                .value()
                .ToString(),
            "2024-08-19 12:34:56.123456789+00");
  EXPECT_EQ(TimestampPicosValue::FromString("2024-08-19 12:34:56.123456789",
                                            pst, /*allow_tz_in_str=*/true)
                .value()
                .ToString(),
            "2024-08-19 19:34:56.123456789+00");
  EXPECT_EQ(TimestampPicosValue::FromString("2024-08-19 12:34:56.123456789+00",
                                            pst, /*allow_tz_in_str=*/true)
                .value()
                .ToString(),
            "2024-08-19 12:34:56.123456789+00");

  // Test cases with Picosecond precisions.
  EXPECT_EQ(TimestampPicosValue::FromString("2024-08-19 12:34:56.123456789321",
                                            absl::UTCTimeZone(),
                                            /*allow_tz_in_str=*/true)
                .value()
                .ToString(),
            "2024-08-19 12:34:56.123456789321+00");
  EXPECT_EQ(TimestampPicosValue::FromString("2024-08-19 12:34:56.123456789321",
                                            pst, /*allow_tz_in_str=*/true)
                .value()
                .ToString(),
            "2024-08-19 19:34:56.123456789321+00");
  EXPECT_EQ(
      TimestampPicosValue::FromString("2024-08-19 12:34:56.123456789321+00",
                                      pst, /*allow_tz_in_str=*/true)
          .value()
          .ToString(),
      "2024-08-19 12:34:56.123456789321+00");
}

TEST_F(TimestampPicosTest, ToUnixPicos) {
  EXPECT_EQ(TimestampPicosValue::Create(absl::UnixEpoch())->ToUnixPicos(), 0);
  EXPECT_EQ(
      TimestampPicosValue::Create(absl::FromUnixMicros(types::kTimestampMin))
          ->ToUnixPicos(),
      absl::int128(types::kTimestampMin) * 1000000);
  EXPECT_EQ(
      TimestampPicosValue::Create(absl::FromUnixMicros(types::kTimestampMax))
          ->ToUnixPicos(),
      absl::int128(types::kTimestampMax) * 1000000);
  EXPECT_EQ(TimestampPicosValue::Create(absl::FromUnixMicros(1234567890))
                ->ToUnixPicos(),
            absl::int128(1234567890) * 1000000);
}

TEST_F(TimestampPicosTest, FromUnixPicos) {
  std::vector<absl::int128> picos = {
      0,
      absl::int128(types::kTimestampMin) * 1000000,
      absl::int128(types::kTimestampMin) * 1000000 + 1,
      absl::int128(types::kTimestampMax) * 1000000,
      absl::int128(types::kTimestampMax + 1) * 1000000 - 1,
  };

  for (absl::int128 p : picos) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(TimestampPicosValue value,
                         TimestampPicosValue::FromUnixPicos(p));
    EXPECT_EQ(value.ToUnixPicos(), p);
  }
}

TEST_F(TimestampPicosTest, FromUnixPicosOutOfRange) {
  EXPECT_THAT(
      TimestampPicosValue::FromUnixPicos(
          absl::int128(types::kTimestampMin) * 1000000 - 1),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("allowed range")));
  EXPECT_THAT(
      TimestampPicosValue::FromUnixPicos(
          absl::int128(types::kTimestampMax + 1) * 1000000),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("allowed range")));
}

}  // namespace
}  // namespace googlesql
