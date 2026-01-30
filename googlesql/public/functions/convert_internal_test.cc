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

#include "googlesql/public/functions/convert_internal.h"

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"

static const float FLOAT_INT32_MIN =
    static_cast<float>(std::numeric_limits<int32_t>::lowest());
static const float FLOAT_INT32_MAX =
    static_cast<float>(std::numeric_limits<int32_t>::max());
static const float FLOAT_INT64_MIN =
    static_cast<float>(std::numeric_limits<int64_t>::lowest());
static const float FLOAT_INT64_MAX =
    static_cast<float>(std::numeric_limits<int64_t>::max());
static const float FLOAT_UINT32_MAX =
    static_cast<float>(std::numeric_limits<uint32_t>::max());
static const float FLOAT_UINT64_MAX =
    static_cast<float>(std::numeric_limits<uint64_t>::max());
static const double DOUBLE_INT32_MIN =
    static_cast<double>(std::numeric_limits<int32_t>::lowest());
static const double DOUBLE_INT32_MAX =
    static_cast<double>(std::numeric_limits<int32_t>::max());
static const double DOUBLE_INT64_MIN =
    static_cast<double>(std::numeric_limits<int64_t>::lowest());
static const double DOUBLE_INT64_MAX =
    static_cast<double>(std::numeric_limits<int64_t>::max());
static const double DOUBLE_UINT32_MAX =
    static_cast<double>(std::numeric_limits<uint32_t>::max());
static const double DOUBLE_UINT64_MAX =
    static_cast<double>(std::numeric_limits<uint64_t>::max());
static const float FLOAT_INFI = std::numeric_limits<float>::infinity();
static const float FLOAT_NINFI = -std::numeric_limits<float>::infinity();
static const float FLOAT_NAN = std::numeric_limits<float>::quiet_NaN();
static const double DOUBLE_INFI = std::numeric_limits<double>::infinity();
static const double DOUBLE_NINFI = -std::numeric_limits<double>::infinity();
static const double DOUBLE_NAN = std::numeric_limits<double>::quiet_NaN();

namespace googlesql {
namespace convert_internal {

TEST(CastOpsTest, InRangeNoTruncate) {
  // int32 in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<double, int32_t>(5744950.5334)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int32_t>(-41834793.402368)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int32_t>(1470707200)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int32_t>(-14707.997)));

  // int32 border or out of range cases.
  EXPECT_EQ(false, (InRangeNoTruncate<float, int32_t>(FLOAT_INT32_MAX)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int32_t>(FLOAT_INT32_MIN)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int32_t>(DOUBLE_INT32_MAX)));
  EXPECT_EQ(false,
            (InRangeNoTruncate<double, int32_t>(DOUBLE_INT32_MAX + 0.5)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int32_t>(DOUBLE_INT32_MIN)));
  EXPECT_EQ(false,
            (InRangeNoTruncate<double, int32_t>(DOUBLE_INT32_MIN - 0.5)));

  // int64 in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<double, int64_t>(37483134.653)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int64_t>(-37483134.653)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int64_t>(374831.653)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int64_t>(-374831.653)));

  // int64 border or out of range cases.
  EXPECT_EQ(false, (InRangeNoTruncate<double, int64_t>(DOUBLE_INT64_MAX)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, int64_t>(DOUBLE_INT64_MAX + 1)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int64_t>(DOUBLE_INT64_MIN)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, int64_t>(FLOAT_INT64_MAX)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int64_t>(FLOAT_INT64_MIN)));

  // uint32 in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<double, uint32_t>(5744950.5334)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, uint32_t>(2634022912.00)));

  // uint32 corner or out of range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<double, uint32_t>(DOUBLE_UINT32_MAX)));
  EXPECT_EQ(false,
            (InRangeNoTruncate<double, uint32_t>(DOUBLE_UINT32_MAX + 0.5)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, uint32_t>(-1.23)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, uint32_t>(-0.23)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint32_t>(FLOAT_UINT32_MAX)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint32_t>(-1.023)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint32_t>(-0.023)));

  // uint64 in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<double, uint64_t>(5744950.5334)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, uint64_t>(2634022912.00)));

  // uint64 corner or out of range cases.
  EXPECT_EQ(false, (InRangeNoTruncate<double, uint64_t>(DOUBLE_UINT64_MAX)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, uint64_t>(-1.23)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint64_t>(FLOAT_UINT64_MAX)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint64_t>(-1.023)));

  // int8 in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, int8_t>(101.234f)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int8_t>(-100.234f)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int8_t>(101.234)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int8_t>(-100.234)));

  // int8 corner or out of range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, int8_t>(127)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, int8_t>(127.023f)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, int8_t>(-128)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, int8_t>(-128.023f)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int8_t>(127)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, int8_t>(127.023)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, int8_t>(-128)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, int8_t>(-128.0233)));

  // uint8 in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, uint8_t>(201.234f)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, uint8_t>(200.234)));

  // uint8 corner or out of range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, uint8_t>(255)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint8_t>(255.023f)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, uint8_t>(-1.13f)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, uint8_t>(255)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, uint8_t>(255.023)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, uint8_t>(-12)));
}

}  // namespace convert_internal
}  // namespace googlesql
