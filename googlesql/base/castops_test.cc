//
// Copyright 2024 Google LLC
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
#include "googlesql/base/castops.h"

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"
#include "absl/numeric/int128.h"

static const float FLOAT_INT32_MIN = static_cast<float>(INT32_MIN);
static const float FLOAT_INT32_MAX = static_cast<float>(INT32_MAX);
static const float FLOAT_INT64_MIN = static_cast<float>(INT64_MIN);
static const float FLOAT_INT64_MAX = static_cast<float>(INT64_MAX);
static const float FLOAT_UINT32_MAX = static_cast<float>(UINT32_MAX);
static const float FLOAT_UINT64_MAX = static_cast<float>(UINT64_MAX);
static const double DOUBLE_INT32_MIN = static_cast<double>(INT32_MIN);
static const double DOUBLE_INT32_MAX = static_cast<double>(INT32_MAX);
static const double DOUBLE_INT64_MIN = static_cast<double>(INT64_MIN);
static const double DOUBLE_INT64_MAX = static_cast<double>(INT64_MAX);
static const double DOUBLE_UINT32_MAX = static_cast<double>(UINT32_MAX);
static const double DOUBLE_UINT64_MAX = static_cast<double>(UINT64_MAX);
static const float FLOAT_INFI = std::numeric_limits<float>::infinity();
static const float FLOAT_NINFI = -std::numeric_limits<float>::infinity();
static const float FLOAT_NAN = std::numeric_limits<float>::quiet_NaN();
static const double DOUBLE_INFI = std::numeric_limits<double>::infinity();
static const double DOUBLE_NINFI = -std::numeric_limits<double>::infinity();
static const double DOUBLE_NAN = std::numeric_limits<double>::quiet_NaN();

typedef unsigned char Uchar;
typedef signed char Schar;

namespace googlesql_base {
namespace castops {

TEST(CastOpsTest, SaturatingFloatToInt) {
  // int32 in range cases.
  EXPECT_EQ(5744950, (SaturatingFloatToInt<double, int32_t>(5744950.5334)));
  EXPECT_EQ(-41834793,
            (SaturatingFloatToInt<double, int32_t>(-41834793.402368)));
  EXPECT_EQ(1470707200, (SaturatingFloatToInt<float, int32_t>(1470707200)));
  EXPECT_EQ(-14707, (SaturatingFloatToInt<float, int32_t>(-14707.997)));

  // int32 border or out of range cases.
  EXPECT_EQ(INT32_MAX, (SaturatingFloatToInt<float, int32_t>(FLOAT_INT32_MAX)));
  EXPECT_EQ(INT32_MAX,
            (SaturatingFloatToInt<float, int32_t>(FLOAT_INT32_MAX + 1)));
  EXPECT_EQ(INT32_MIN, (SaturatingFloatToInt<float, int32_t>(FLOAT_INT32_MIN)));
  EXPECT_EQ(INT32_MAX,
            (SaturatingFloatToInt<double, int32_t>(DOUBLE_INT32_MAX)));
  EXPECT_EQ(INT32_MAX,
            (SaturatingFloatToInt<double, int32_t>(DOUBLE_INT32_MAX + 1)));
  EXPECT_EQ(INT32_MAX,
            (SaturatingFloatToInt<double, int32_t>(DOUBLE_INT32_MAX + 0.5)));
  EXPECT_EQ(INT32_MIN,
            (SaturatingFloatToInt<double, int32_t>(DOUBLE_INT32_MIN)));
  EXPECT_EQ(INT32_MIN,
            (SaturatingFloatToInt<double, int32_t>(DOUBLE_INT32_MIN - 0.5)));

  // int32 infinite and nan cases.
  EXPECT_EQ(INT32_MAX, (SaturatingFloatToInt<float, int32_t>(FLOAT_INFI)));
  EXPECT_EQ(INT32_MIN, (SaturatingFloatToInt<float, int32_t>(FLOAT_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, int32_t>(FLOAT_NAN)));
  EXPECT_EQ(INT32_MAX, (SaturatingFloatToInt<double, int32_t>(DOUBLE_INFI)));
  EXPECT_EQ(INT32_MIN, (SaturatingFloatToInt<double, int32_t>(DOUBLE_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, int32_t>(DOUBLE_NAN)));

  // int64 in range cases.
  EXPECT_EQ(37483134, (SaturatingFloatToInt<double, int64_t>(37483134.653)));
  EXPECT_EQ(-37483134, (SaturatingFloatToInt<double, int64_t>(-37483134.653)));
  EXPECT_EQ(374831, (SaturatingFloatToInt<float, int64_t>(374831.653)));
  EXPECT_EQ(-374831, (SaturatingFloatToInt<float, int64_t>(-374831.653)));

  // int64 border or out of range cases.
  EXPECT_EQ(INT64_MAX,
            (SaturatingFloatToInt<double, int64_t>(DOUBLE_INT64_MAX)));
  EXPECT_EQ(INT64_MAX,
            (SaturatingFloatToInt<double, int64_t>(DOUBLE_INT64_MAX + 1)));
  EXPECT_EQ(INT64_MAX,
            (SaturatingFloatToInt<double, int64_t>(DOUBLE_INT64_MAX + 0.5)));
  EXPECT_EQ(INT64_MIN,
            (SaturatingFloatToInt<double, int64_t>(DOUBLE_INT64_MIN)));
  EXPECT_EQ(INT64_MIN,
            (SaturatingFloatToInt<double, int64_t>(DOUBLE_INT64_MIN - 1)));
  EXPECT_EQ(INT64_MIN,
            (SaturatingFloatToInt<double, int64_t>(DOUBLE_INT64_MIN - 0.5)));
  EXPECT_EQ(INT64_MAX, (SaturatingFloatToInt<float, int64_t>(FLOAT_INT64_MAX)));
  EXPECT_EQ(INT64_MIN, (SaturatingFloatToInt<float, int64_t>(FLOAT_INT64_MIN)));

  // int64 infinite and nan cases.
  EXPECT_EQ(INT64_MAX, (SaturatingFloatToInt<float, int64_t>(FLOAT_INFI)));
  EXPECT_EQ(INT64_MIN, (SaturatingFloatToInt<float, int64_t>(FLOAT_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, int64_t>(FLOAT_NAN)));
  EXPECT_EQ(INT64_MAX, (SaturatingFloatToInt<double, int64_t>(DOUBLE_INFI)));
  EXPECT_EQ(INT64_MIN, (SaturatingFloatToInt<double, int64_t>(DOUBLE_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, int64_t>(DOUBLE_NAN)));

  // uint32 in range cases.
  EXPECT_EQ(5744950, (SaturatingFloatToInt<double, uint32_t>(5744950.5334)));
  EXPECT_EQ(2634022912, (SaturatingFloatToInt<float, uint32_t>(2634022912.00)));

  // uint32 corner or out of range cases.
  EXPECT_EQ(UINT32_MAX,
            (SaturatingFloatToInt<double, uint32_t>(DOUBLE_UINT32_MAX)));
  EXPECT_EQ(UINT32_MAX,
            (SaturatingFloatToInt<double, uint32_t>(DOUBLE_UINT32_MAX + 0.5)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, uint32_t>(-1.23)));
  EXPECT_EQ(UINT32_MAX,
            (SaturatingFloatToInt<float, uint32_t>(FLOAT_UINT32_MAX)));
  EXPECT_EQ(UINT32_MAX,
            (SaturatingFloatToInt<float, uint32_t>(FLOAT_UINT32_MAX + 0.5)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, uint32_t>(-1.023)));

  // uint32 infinite and nan cases.
  EXPECT_EQ(UINT32_MAX, (SaturatingFloatToInt<float, uint32_t>(FLOAT_INFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, uint32_t>(FLOAT_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, uint32_t>(FLOAT_NAN)));
  EXPECT_EQ(UINT32_MAX, (SaturatingFloatToInt<double, uint32_t>(DOUBLE_INFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, uint32_t>(DOUBLE_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, uint32_t>(DOUBLE_NAN)));

  // uint64 in range cases.
  EXPECT_EQ(5744950, (SaturatingFloatToInt<double, uint64_t>(5744950.5334)));
  EXPECT_EQ(2634022912, (SaturatingFloatToInt<float, uint64_t>(2634022912.00)));

  // uint64 corner or out of range cases.
  EXPECT_EQ(UINT64_MAX,
            (SaturatingFloatToInt<double, uint64_t>(DOUBLE_UINT64_MAX)));
  EXPECT_EQ(UINT64_MAX,
            (SaturatingFloatToInt<double, uint64_t>(DOUBLE_UINT64_MAX + 0.5)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, uint64_t>(-1.23)));
  EXPECT_EQ(UINT64_MAX,
            (SaturatingFloatToInt<float, uint64_t>(FLOAT_UINT64_MAX)));
  EXPECT_EQ(UINT64_MAX,
            (SaturatingFloatToInt<float, uint64_t>(FLOAT_UINT64_MAX + 0.5)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, uint64_t>(-1.023)));

  // uint64 infinite and nan cases.
  EXPECT_EQ(UINT64_MAX, (SaturatingFloatToInt<float, uint64_t>(FLOAT_INFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, uint64_t>(FLOAT_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, uint64_t>(FLOAT_NAN)));
  EXPECT_EQ(UINT64_MAX, (SaturatingFloatToInt<double, uint64_t>(DOUBLE_INFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, uint64_t>(DOUBLE_NINFI)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, uint64_t>(DOUBLE_NAN)));

  // Schar in range cases.
  EXPECT_EQ(101, (SaturatingFloatToInt<float, Schar>(101.234)));
  EXPECT_EQ(-100, (SaturatingFloatToInt<float, Schar>(-100.234)));
  EXPECT_EQ(101, (SaturatingFloatToInt<double, Schar>(101.234)));
  EXPECT_EQ(-100, (SaturatingFloatToInt<double, Schar>(-100.234)));

  // Schar corner or out of range cases.
  EXPECT_EQ(127, (SaturatingFloatToInt<float, Schar>(127.23)));
  EXPECT_EQ(127, (SaturatingFloatToInt<float, Schar>(128.13)));
  EXPECT_EQ(-128, (SaturatingFloatToInt<float, Schar>(-128)));
  EXPECT_EQ(-128, (SaturatingFloatToInt<float, Schar>(-129)));
  EXPECT_EQ(127, (SaturatingFloatToInt<double, Schar>(127.23)));
  EXPECT_EQ(127, (SaturatingFloatToInt<double, Schar>(128.13)));
  EXPECT_EQ(-128, (SaturatingFloatToInt<double, Schar>(-128)));
  EXPECT_EQ(-128, (SaturatingFloatToInt<double, Schar>(-129)));

  // Schar infinite and nan cases.
  EXPECT_EQ(127,
            (SaturatingFloatToInt<float, Schar>(FLOAT_INFI)));
  EXPECT_EQ(-128,
            (SaturatingFloatToInt<float, Schar>(FLOAT_NINFI)));
  EXPECT_EQ(0,
            (SaturatingFloatToInt<float, Schar>(FLOAT_NAN)));
  EXPECT_EQ(127,
            (SaturatingFloatToInt<double, Schar>(DOUBLE_INFI)));
  EXPECT_EQ(-128,
            (SaturatingFloatToInt<double, Schar>(DOUBLE_NINFI)));
  EXPECT_EQ(0,
            (SaturatingFloatToInt<double, Schar>(DOUBLE_NAN)));

  // Uchar in range cases.
  EXPECT_EQ(201, (SaturatingFloatToInt<float, Uchar>(201.234)));
  EXPECT_EQ(200, (SaturatingFloatToInt<double, Uchar>(200.234)));

  // Uchar corner or out of range cases.
  EXPECT_EQ(255, (SaturatingFloatToInt<float, Uchar>(255.23)));
  EXPECT_EQ(255, (SaturatingFloatToInt<float, Uchar>(256.83)));
  EXPECT_EQ(0, (SaturatingFloatToInt<float, Uchar>(-1.13)));
  EXPECT_EQ(255, (SaturatingFloatToInt<double, Uchar>(255.23)));
  EXPECT_EQ(0, (SaturatingFloatToInt<double, Uchar>(-12)));

  // Uchar infinite and nan cases.
  EXPECT_EQ(255,
            (SaturatingFloatToInt<float, Uchar>(FLOAT_INFI)));
  EXPECT_EQ(0,
            (SaturatingFloatToInt<float, Uchar>(FLOAT_NINFI)));
  EXPECT_EQ(0,
            (SaturatingFloatToInt<float, Uchar>(FLOAT_NAN)));
  EXPECT_EQ(255,
            (SaturatingFloatToInt<double, Uchar>(DOUBLE_INFI)));
  EXPECT_EQ(0,
            (SaturatingFloatToInt<double, Uchar>(DOUBLE_NINFI)));
  EXPECT_EQ(0,
            (SaturatingFloatToInt<double, Uchar>(DOUBLE_NAN)));

  // absl::int128 cases.
  EXPECT_EQ(absl::MakeInt128(2000, 0),
            (SaturatingFloatToInt<long double, absl::int128>(
                36893488147419103232000.0L)));
  EXPECT_EQ(std::numeric_limits<absl::int128>::max(),
            (SaturatingFloatToInt<long double, absl::int128>(
                std::numeric_limits<long double>::max())));
  EXPECT_EQ(std::numeric_limits<absl::int128>::min(),
            (SaturatingFloatToInt<long double, absl::int128>(
                std::numeric_limits<long double>::lowest())));

  // absl::uint128 cases.
  EXPECT_EQ(absl::MakeInt128(2000, 0),
            (SaturatingFloatToInt<long double, absl::uint128>(
                36893488147419103232000.0L)));
  EXPECT_EQ(std::numeric_limits<absl::uint128>::max(),
            (SaturatingFloatToInt<long double, absl::uint128>(
                std::numeric_limits<long double>::max())));
  EXPECT_EQ(std::numeric_limits<absl::uint128>::min(),
            (SaturatingFloatToInt<long double, absl::uint128>(0.0)));
}

TEST(CastOpsTest, InRange) {
  // int32 in range cases.
  EXPECT_EQ(true, (InRange<double, int32_t>(5744950.5334)));
  EXPECT_EQ(true, (InRange<double, int32_t>(-41834793.402368)));
  EXPECT_EQ(true, (InRange<float, int32_t>(1470707200)));
  EXPECT_EQ(true, (InRange<float, int32_t>(-14707.997)));

  // int32 border or out of range cases.
  EXPECT_EQ(false, (InRange<float, int32_t>(FLOAT_INT32_MAX)));
  EXPECT_EQ(true, (InRange<float, int32_t>(FLOAT_INT32_MIN)));
  EXPECT_EQ(true, (InRange<double, int32_t>(DOUBLE_INT32_MAX)));
  EXPECT_EQ(false, (InRange<double, int32_t>(DOUBLE_INT32_MAX + 1)));
  EXPECT_EQ(true, (InRange<double, int32_t>(DOUBLE_INT32_MAX + 0.5)));
  EXPECT_EQ(true, (InRange<double, int32_t>(DOUBLE_INT32_MIN)));
  EXPECT_EQ(true, (InRange<double, int32_t>(DOUBLE_INT32_MIN - 0.5)));

  // int64 in range cases.
  EXPECT_EQ(true, (InRange<double, int64_t>(37483134.653)));
  EXPECT_EQ(true, (InRange<double, int64_t>(-37483134.653)));
  EXPECT_EQ(true, (InRange<float, int64_t>(374831.653)));
  EXPECT_EQ(true, (InRange<float, int64_t>(-374831.653)));

  // int64 border or out of range cases.
  EXPECT_EQ(false, (InRange<double, int64_t>(DOUBLE_INT64_MAX)));
  EXPECT_EQ(false, (InRange<double, int64_t>(DOUBLE_INT64_MAX + 1)));
  EXPECT_EQ(true, (InRange<double, int64_t>(DOUBLE_INT64_MIN)));
  EXPECT_EQ(false, (InRange<float, int64_t>(FLOAT_INT64_MAX)));
  EXPECT_EQ(true, (InRange<float, int64_t>(FLOAT_INT64_MIN)));

  // uint32 in range cases.
  EXPECT_EQ(true, (InRange<double, uint32_t>(5744950.5334)));
  EXPECT_EQ(true, (InRange<float, uint32_t>(2634022912.00)));

  // uint32 corner or out of range cases.
  EXPECT_EQ(true, (InRange<double, uint32_t>(DOUBLE_UINT32_MAX)));
  EXPECT_EQ(true, (InRange<double, uint32_t>(DOUBLE_UINT32_MAX + 0.5)));
  EXPECT_EQ(false, (InRange<double, uint32_t>(-1.23)));
  EXPECT_EQ(true, (InRange<double, uint32_t>(-0.23)));
  EXPECT_EQ(false, (InRange<float, uint32_t>(FLOAT_UINT32_MAX)));
  EXPECT_EQ(false, (InRange<float, uint32_t>(-1.023)));
  EXPECT_EQ(true, (InRange<float, uint32_t>(-0.023)));

  // uint64 in range cases.
  EXPECT_EQ(true, (InRange<double, uint64_t>(5744950.5334)));
  EXPECT_EQ(true, (InRange<float, uint64_t>(2634022912.00)));

  // uint64 corner or out of range cases.
  EXPECT_EQ(false, (InRange<double, uint64_t>(DOUBLE_UINT64_MAX)));
  EXPECT_EQ(false, (InRange<double, uint64_t>(-1.23)));
  EXPECT_EQ(false, (InRange<float, uint64_t>(FLOAT_UINT64_MAX)));
  EXPECT_EQ(false, (InRange<float, uint64_t>(-1.023)));

  // Schar in range cases.
  EXPECT_EQ(true, (InRange<float, Schar>(101.234)));
  EXPECT_EQ(true, (InRange<float, Schar>(-100.234)));
  EXPECT_EQ(true, (InRange<double, Schar>(101.234)));
  EXPECT_EQ(true, (InRange<double, Schar>(-100.234)));

  // Schar corner or out of range cases.
  EXPECT_EQ(true, (InRange<float, Schar>(127.23)));
  EXPECT_EQ(false, (InRange<float, Schar>(128.13)));
  EXPECT_EQ(true, (InRange<float, Schar>(-128)));
  EXPECT_EQ(false, (InRange<float, Schar>(-129)));
  EXPECT_EQ(true, (InRange<float, Schar>(-128.5)));
  EXPECT_EQ(true, (InRange<double, Schar>(127.23)));
  EXPECT_EQ(false, (InRange<double, Schar>(128.13)));
  EXPECT_EQ(true, (InRange<double, Schar>(-128)));
  EXPECT_EQ(true, (InRange<double, Schar>(-128.233)));
  EXPECT_EQ(false, (InRange<double, Schar>(-129)));

  // Uchar in range cases.
  EXPECT_EQ(true, (InRange<float, Uchar>(201.234)));
  EXPECT_EQ(true, (InRange<double, Uchar>(200.234)));

  // Uchar corner or out of range cases.
  EXPECT_EQ(true, (InRange<float, Uchar>(255.23)));
  EXPECT_EQ(false, (InRange<float, Uchar>(256.83)));
  EXPECT_EQ(false, (InRange<float, Uchar>(-1.13)));
  EXPECT_EQ(true, (InRange<double, Uchar>(255.23)));
  EXPECT_EQ(false, (InRange<double, Uchar>(-12)));
}

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

  // Schar in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, Schar>(101.234)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, Schar>(-100.234)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, Schar>(101.234)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, Schar>(-100.234)));

  // Schar corner or out of range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, Schar>(127)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, Schar>(127.023)));
  EXPECT_EQ(true, (InRangeNoTruncate<float, Schar>(-128)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, Schar>(-128.023)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, Schar>(127)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, Schar>(127.023)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, Schar>(-128)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, Schar>(-128.0233)));

  // Uchar in range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, Uchar>(201.234)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, Uchar>(200.234)));

  // Uchar corner or out of range cases.
  EXPECT_EQ(true, (InRangeNoTruncate<float, Uchar>(255)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, Uchar>(255.023)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, Uchar>(-1.13)));
  EXPECT_EQ(true, (InRangeNoTruncate<double, Uchar>(255)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, Uchar>(255.023)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, Uchar>(-12)));
}

TEST(CastOpsTest, DoubleToFloat) {
  // NaN
  EXPECT_TRUE(std::isnan(
      DoubleToFloat(std::numeric_limits<float>::quiet_NaN())));

  // Within float range
  EXPECT_EQ(static_cast<float>(1.23), DoubleToFloat(1.23));
  EXPECT_EQ(static_cast<float>(-0.034), DoubleToFloat(-0.034));
  EXPECT_EQ(0.0f, DoubleToFloat(0.0));

  // Limits of float range
  // Relies on the fact that every float is exactly representable as a double.
  EXPECT_EQ(std::numeric_limits<float>::max(),
            DoubleToFloat(std::numeric_limits<float>::max()));
  EXPECT_EQ(std::numeric_limits<float>::lowest(),
            DoubleToFloat(std::numeric_limits<float>::lowest()));

  // Clips to +infinity
  EXPECT_EQ(std::numeric_limits<float>::infinity(),
            DoubleToFloat(std::numeric_limits<double>::infinity()));
  EXPECT_EQ(std::numeric_limits<float>::infinity(),
            DoubleToFloat(
                2.0 * static_cast<double>(std::numeric_limits<float>::max())));

  // Clips to -infinity
  EXPECT_EQ(-std::numeric_limits<float>::infinity(),
            DoubleToFloat(-std::numeric_limits<double>::infinity()));
  EXPECT_EQ(-std::numeric_limits<float>::infinity(),
            DoubleToFloat(
                2.0 * static_cast<double>(
                    std::numeric_limits<float>::lowest())));
}

template <typename T>
class CastOpsInfNanTest : public ::testing::Test {
 public:
  T value_;
};

typedef ::testing::Types<int32_t, int64_t, uint32_t, uint64_t, Schar, Uchar>
    MyTypes;
TYPED_TEST_SUITE(CastOpsInfNanTest, MyTypes);

TYPED_TEST(CastOpsInfNanTest, InRangeInfNanTest) {
  EXPECT_EQ(false, (InRange<float, TypeParam>(FLOAT_INFI)));
  EXPECT_EQ(false, (InRange<float, TypeParam>(FLOAT_NINFI)));
  EXPECT_EQ(false, (InRange<float, TypeParam>(FLOAT_NAN)));
  EXPECT_EQ(false, (InRange<double, TypeParam>(DOUBLE_INFI)));
  EXPECT_EQ(false, (InRange<double, TypeParam>(DOUBLE_NINFI)));
  EXPECT_EQ(false, (InRange<double, TypeParam>(DOUBLE_NAN)));
}

TYPED_TEST(CastOpsInfNanTest, InRangeNoTruncateInfNanTest) {
  EXPECT_EQ(false, (InRangeNoTruncate<float, TypeParam>(FLOAT_INFI)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, TypeParam>(FLOAT_NINFI)));
  EXPECT_EQ(false, (InRangeNoTruncate<float, TypeParam>(FLOAT_NAN)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, TypeParam>(DOUBLE_INFI)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, TypeParam>(DOUBLE_NINFI)));
  EXPECT_EQ(false, (InRangeNoTruncate<double, TypeParam>(DOUBLE_NAN)));
}


TEST(CastOpsTest, DoubleToFiniteFloat) {
  // NaN
  EXPECT_TRUE(std::isnan(
      DoubleToFiniteFloat(std::numeric_limits<float>::quiet_NaN())));

  // Within float range
  EXPECT_EQ(static_cast<float>(1.23), DoubleToFiniteFloat(1.23));
  EXPECT_EQ(static_cast<float>(-0.034), DoubleToFiniteFloat(-0.034));
  EXPECT_EQ(0.0f, DoubleToFiniteFloat(0.0));

  // Limits of float range
  // Relies on the fact that every float is exactly representable as a double.
  EXPECT_EQ(std::numeric_limits<float>::max(),
            DoubleToFiniteFloat(std::numeric_limits<float>::max()));
  EXPECT_EQ(std::numeric_limits<float>::lowest(),
            DoubleToFiniteFloat(std::numeric_limits<float>::lowest()));

  // Clips to FLT_MAX
  EXPECT_EQ(std::numeric_limits<float>::max(),
            DoubleToFiniteFloat(std::numeric_limits<double>::infinity()));
  EXPECT_EQ(std::numeric_limits<float>::max(),
            DoubleToFiniteFloat(
                2.0 * static_cast<double>(std::numeric_limits<float>::max())));

  // Clips to FLT_LOWEST
  EXPECT_EQ(std::numeric_limits<float>::lowest(),
            DoubleToFiniteFloat(-std::numeric_limits<double>::infinity()));
  EXPECT_EQ(std::numeric_limits<float>::lowest(),
            DoubleToFiniteFloat(
                2.0 * static_cast<double>(
                    std::numeric_limits<float>::lowest())));
}

TEST(CastOpsTest, LongDoubleToDouble) {
  // NaN
  EXPECT_TRUE(
      std::isnan(LongDoubleToDouble(std::numeric_limits<double>::quiet_NaN())));

  // Within double range
  EXPECT_EQ(1.23, LongDoubleToDouble(1.23L));
  EXPECT_EQ(-0.034, LongDoubleToDouble(-0.034L));
  EXPECT_EQ(0.0, LongDoubleToDouble(0.0L));

  // Limits of double range
  // Relies on the fact that every double is exactly representable as a long
  // double.
  EXPECT_EQ(std::numeric_limits<double>::max(),
            LongDoubleToDouble(std::numeric_limits<double>::max()));
  EXPECT_EQ(std::numeric_limits<double>::lowest(),
            LongDoubleToDouble(std::numeric_limits<double>::lowest()));

  // Clips to +infinity
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            LongDoubleToDouble(std::numeric_limits<long double>::infinity()));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            LongDoubleToDouble(2.0 * static_cast<long double>(
                                         std::numeric_limits<double>::max())));

  // Clips to -infinity
  EXPECT_EQ(-std::numeric_limits<double>::infinity(),
            LongDoubleToDouble(-std::numeric_limits<long double>::infinity()));
  EXPECT_EQ(
      -std::numeric_limits<double>::infinity(),
      LongDoubleToDouble(2.0 * static_cast<long double>(
                                   std::numeric_limits<double>::lowest())));
}

TEST(CastOpsTest, LongDoubleToFiniteDouble) {
  // NaN
  EXPECT_TRUE(std::isnan(
      LongDoubleToFiniteDouble(std::numeric_limits<double>::quiet_NaN())));

  // Within double range
  EXPECT_EQ(1.23, LongDoubleToFiniteDouble(1.23L));
  EXPECT_EQ(-0.034, LongDoubleToFiniteDouble(-0.034L));
  EXPECT_EQ(0.0, LongDoubleToFiniteDouble(0.0L));

  // Limits of double range
  // Relies on the fact that every double is exactly representable as a long
  // double.
  EXPECT_EQ(std::numeric_limits<double>::max(),
            LongDoubleToFiniteDouble(std::numeric_limits<double>::max()));
  EXPECT_EQ(std::numeric_limits<double>::lowest(),
            LongDoubleToFiniteDouble(std::numeric_limits<double>::lowest()));

  // Clips to DBL_MAX
  EXPECT_EQ(
      std::numeric_limits<double>::max(),
      LongDoubleToFiniteDouble(std::numeric_limits<long double>::infinity()));
  EXPECT_EQ(
      std::numeric_limits<double>::max(),
      LongDoubleToFiniteDouble(
          2.0 * static_cast<long double>(std::numeric_limits<double>::max())));

  // Clips to DBL_LOWEST
  EXPECT_EQ(
      std::numeric_limits<double>::lowest(),
      LongDoubleToFiniteDouble(-std::numeric_limits<long double>::infinity()));
  EXPECT_EQ(std::numeric_limits<double>::lowest(),
            LongDoubleToFiniteDouble(
                2.0 * static_cast<long double>(
                          std::numeric_limits<double>::lowest())));
}

}  // namespace castops

namespace x86compatible {

TEST(CastOpsTest, ToInt32) {
  // In range common cases.
  EXPECT_EQ(5744950, (ToInt32<double>(5744950.5334)));
  EXPECT_EQ(-41834793, (ToInt32<double>(-41834793.402368)));
  EXPECT_EQ(14707, (ToInt32<double>(14707.00)));

  EXPECT_EQ(5744950, (ToInt32<float>(5744950.2334)));
  EXPECT_EQ(1470707200, (ToInt32<float>(1470707200.128)));
  EXPECT_EQ(-14707, (ToInt32<float>(-14707.997)));

  // Double border cases.
  EXPECT_EQ(INT32_MAX, (ToInt32<double>(DOUBLE_INT32_MAX)));
  EXPECT_EQ(INT32_MAX, (ToInt32<double>(DOUBLE_INT32_MAX+0.5)));
  EXPECT_EQ(INT32_MIN, (ToInt32<double>(DOUBLE_INT32_MIN)));
  EXPECT_EQ(INT32_MIN, (ToInt32<double>(DOUBLE_INT32_MIN-0.5)));
  EXPECT_EQ(INT32_MIN, (ToInt32<double>(DOUBLE_INT32_MIN-1)));
  // Double value DOUBLE_INT32_MIN+0.1 is close to INT_MIN and in-range.
  EXPECT_EQ(INT32_MIN+1, (ToInt32<double>(DOUBLE_INT32_MIN+0.1)));

  // Float border cases.
  // Because INT_MAX cannot be presented precisely in float, when it is casted
  // to float type, it is actually treated out of range of int32 representable
  // range.
  // Note that 2147483520.000f (2^31-128 = FLOAT_INT32_MAX-128) and
  // 2147483648.000f (2^31 = FLOAT_INT32_MAX) are
  // adjacent float type values.
  EXPECT_EQ(INT32_MIN, (ToInt32<float>(FLOAT_INT32_MAX)));
  EXPECT_EQ(INT32_MAX-127, (ToInt32<float>(FLOAT_INT32_MAX-128)));
  EXPECT_EQ(INT32_MIN, (ToInt32<float>(FLOAT_INT32_MIN)));

  // Infinite and NaN cases.
  EXPECT_EQ(INT32_MIN, ToInt32<float>(FLOAT_INFI));
  EXPECT_EQ(INT32_MIN, ToInt32<float>(FLOAT_NINFI));
  EXPECT_EQ(INT32_MIN, ToInt32<float>(FLOAT_NAN));
  EXPECT_EQ(INT32_MIN, ToInt32<double>(DOUBLE_INFI));
  EXPECT_EQ(INT32_MIN, ToInt32<double>(DOUBLE_NINFI));
  EXPECT_EQ(INT32_MIN, ToInt32<double>(DOUBLE_NAN));
}

TEST(CastOpsTest, ToInt64) {
  // In range common cases.
  EXPECT_EQ(37483134, (ToInt64<double>(37483134.653)));
  EXPECT_EQ(-37483134, (ToInt64<double>(-37483134.653)));
  EXPECT_EQ(374831, (ToInt64<float>(374831.653)));
  EXPECT_EQ(-374831, (ToInt64<float>(-374831.653)));

  // Border cases.
  // Note that DOUBLE_INT64_MAX is actually 2^63. Its adjacent double value
  // is 2^63-1024 in double.
  EXPECT_EQ(INT64_MIN, ToInt64<double>(DOUBLE_INT64_MAX));
  EXPECT_EQ(INT64_MAX-1023, ToInt64<double>(DOUBLE_INT64_MAX-1024));
  // DOUBLE_INT64_MIN-2048, DOUBLE_INT64_MIN and DOUBLE_INT64_MIN+1024 are
  // adjacent three double values.
  EXPECT_EQ(INT64_MIN, ToInt64<double>(DOUBLE_INT64_MIN));
  EXPECT_EQ(INT64_MIN+1024, ToInt64<double>(DOUBLE_INT64_MIN+1024));
  EXPECT_EQ(INT64_MIN, ToInt64<double>(DOUBLE_INT64_MIN-2048));

  EXPECT_EQ(INT64_MIN, ToInt64<float>(FLOAT_INT64_MAX));
  EXPECT_EQ(INT64_MIN, ToInt64<float>(FLOAT_INT64_MIN));

  // Infinite and NaN cases.
  EXPECT_EQ(INT64_MIN, ToInt64<float>(FLOAT_INFI));
  EXPECT_EQ(INT64_MIN, ToInt64<float>(FLOAT_NINFI));
  EXPECT_EQ(INT64_MIN, ToInt64<float>(FLOAT_NAN));
  EXPECT_EQ(INT64_MIN, ToInt64<double>(DOUBLE_INFI));
  EXPECT_EQ(INT64_MIN, ToInt64<double>(DOUBLE_NINFI));
  EXPECT_EQ(INT64_MIN, ToInt64<double>(DOUBLE_NAN));
}

TEST(CastOptsTest, ToUint32) {
  // In range common cases.
  EXPECT_EQ(5744950, (ToUint32<double>(5744950.5334)));
  EXPECT_EQ(2634022912, (ToUint32<float>(2634022912.00)));

  // Double border cases.
  EXPECT_EQ(UINT32_MAX, ToUint32<double>(DOUBLE_UINT32_MAX));
  EXPECT_EQ(0, ToUint32<double>(DOUBLE_UINT32_MAX+1.01));

  // Float border cases.
  // Note that FLOAT_UINT32_MAX is actually 2^32. 2^32-256 is its adjacent float
  // value that is in range.
  EXPECT_EQ(0, ToUint32<float>(FLOAT_UINT32_MAX));
  EXPECT_EQ(UINT32_MAX-255, ToUint32<float>(FLOAT_UINT32_MAX-256));

  // Negative cases. These are out-of-range of uint32.
  EXPECT_EQ(static_cast<uint32_t>(-678), ToUint32<float>(-678));
  EXPECT_EQ(static_cast<uint32_t>(INT32_MIN + 128),
            ToUint32<float>(FLOAT_INT32_MIN + 128));
  EXPECT_EQ(static_cast<uint32_t>(INT32_MAX) + 1,
            ToUint32<float>(FLOAT_INT32_MIN));

  // Infinite and NaN cases.
  EXPECT_EQ(0, ToUint32<float>(FLOAT_INFI));
  EXPECT_EQ(0, ToUint32<float>(FLOAT_NINFI));
  EXPECT_EQ(0, ToUint32<float>(FLOAT_NAN));
  EXPECT_EQ(0, ToUint32<double>(DOUBLE_INFI));
  EXPECT_EQ(0, ToUint32<double>(DOUBLE_NINFI));
  EXPECT_EQ(0, ToUint32<double>(DOUBLE_NAN));
}

TEST(CastOptsTest, ToUint64) {
  // In range common cases.
  EXPECT_EQ(37483134, (ToUint64<double>(37483134.653)));
  EXPECT_EQ(374831, (ToUint64<float>(374831.653)));

  // Double border cases.
  EXPECT_EQ(0, ToUint64<double>(DOUBLE_UINT64_MAX));
  EXPECT_EQ(UINT64_MAX-2047,
            ToUint64<double>(DOUBLE_UINT64_MAX-2048));

  // Float border cases.
  EXPECT_EQ(0, ToUint64<float>(FLOAT_UINT64_MAX));

  // Negative cases.
  EXPECT_EQ(static_cast<uint64_t>(-678), ToUint64<float>(-678));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1,
            ToUint64<float>(FLOAT_INT64_MIN));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1,
            ToUint64<double>(DOUBLE_INT64_MIN));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MIN + 1024),
            ToUint64<double>(DOUBLE_INT64_MIN + 1024));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1,
            ToUint64<double>(DOUBLE_INT64_MIN - 2048));

  // Infinite and NaN cases.
  EXPECT_EQ(0, ToUint64<float>(FLOAT_INFI));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1, ToUint64<float>(FLOAT_NINFI));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1, ToUint64<float>(FLOAT_NAN));
  EXPECT_EQ(0, ToUint64<double>(DOUBLE_INFI));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1,
            ToUint64<double>(DOUBLE_NINFI));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX) + 1, ToUint64<double>(DOUBLE_NAN));
}

// We group unit tests for some Toxxx<FloatType>() functions
// because they are very similar.
TEST(CastOptsTest, ToSmallIntegrals) {
  // In range common cases.
  EXPECT_EQ(8, (ToSchar<float>(8.987096)));
  EXPECT_EQ(-71, (ToSchar<float>(-71.793536)));
  EXPECT_EQ(8, (ToSchar<double>(8.987096)));
  EXPECT_EQ(-71, (ToSchar<double>(-71.793536)));
  EXPECT_EQ(8, (ToUchar<float>(8.987096)));
  EXPECT_EQ(8, (ToUchar<double>(8.987096)));
  EXPECT_EQ(187, (ToInt16<float>(187.58)));
  EXPECT_EQ(-71, (ToInt16<float>(-71.793536)));
  EXPECT_EQ(187, (ToInt16<double>(187.58)));
  EXPECT_EQ(-71, (ToInt16<double>(-71.793536)));
  EXPECT_EQ(28, (ToUint16<float>(28.987096)));
  EXPECT_EQ(28, (ToUint16<double>(28.987096)));

  // Out of range cases.
#define CHECK_SMALLINT_OUTRANGE(ValueType, value, result_int)               \
  EXPECT_EQ(static_cast<Schar>(result_int), (ToSchar<ValueType>(value)));   \
  EXPECT_EQ(static_cast<Uchar>(result_int), (ToUchar<ValueType>(value)));   \
  EXPECT_EQ(static_cast<int16_t>(result_int), (ToInt16<ValueType>(value))); \
  EXPECT_EQ(static_cast<uint16_t>(result_int), (ToUint16<ValueType>(value)));

  CHECK_SMALLINT_OUTRANGE(float, 128.34, 128)
  CHECK_SMALLINT_OUTRANGE(double, 128.34, 128)
  CHECK_SMALLINT_OUTRANGE(float, -129.34, -129)
  CHECK_SMALLINT_OUTRANGE(double, -129.34, -129)
  CHECK_SMALLINT_OUTRANGE(float, 32768.12, 32768)
  CHECK_SMALLINT_OUTRANGE(float, -32769.3, -32769)
  CHECK_SMALLINT_OUTRANGE(double, 32768.12, 32768)
  CHECK_SMALLINT_OUTRANGE(double, -32769.3, -32769)
  CHECK_SMALLINT_OUTRANGE(float, FLOAT_INT32_MAX, 0)
  CHECK_SMALLINT_OUTRANGE(float, FLOAT_INT32_MIN, 0)
  CHECK_SMALLINT_OUTRANGE(double, DOUBLE_INT32_MAX, INT32_MAX)
  CHECK_SMALLINT_OUTRANGE(double, DOUBLE_INT32_MIN, 0)

  // Infinite and NaN cases.
#define CHECK_SMALLINT_INFI(IntType)  \
    EXPECT_EQ(0,  \
        (To##IntType<float>(FLOAT_INFI)));    \
    EXPECT_EQ(0,  \
        (To##IntType<float>(FLOAT_NINFI)));   \
    EXPECT_EQ(0,  \
        (To##IntType<float>(FLOAT_NAN)));     \
    EXPECT_EQ(0,  \
        (To##IntType<double>(DOUBLE_INFI)));  \
    EXPECT_EQ(0,  \
        (To##IntType<double>(DOUBLE_NINFI))); \
    EXPECT_EQ(0,  \
        (To##IntType<double>(DOUBLE_NAN)));

  CHECK_SMALLINT_INFI(Schar)
  CHECK_SMALLINT_INFI(Uchar)
  CHECK_SMALLINT_INFI(Int16)
  CHECK_SMALLINT_INFI(Uint16)
}

}  // namespace x86compatible
}  // namespace googlesql_base
