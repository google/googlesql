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

#include "googlesql/base/lossless_convert.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "gtest/gtest.h"

namespace {

template <typename T>
bool IsNaN(T val) {
  return false;
}
bool IsNaN(float val) { return std::isnan(val); }
bool IsNaN(double val) { return std::isnan(val); }
bool IsNaN(long double val) { return std::isnan(val); }

template<typename InType, typename OutType>
bool Case(InType in_val, bool expect_success) {
  OutType out_val;
  return (googlesql_base::LosslessConvert(in_val, &out_val) == expect_success &&
          (!expect_success || in_val == out_val ||
           (IsNaN(in_val) && IsNaN(out_val))));
}

TEST(LosslessConvertTest, Identity) {
  EXPECT_TRUE((Case<int32_t, int32_t>(1234, true)));
  EXPECT_TRUE((Case<bool, bool>(true, true)));
  EXPECT_TRUE((Case<float, float>(1.0, true)));
  EXPECT_TRUE((Case<double, double>(1.0, true)));
}

TEST(LosslessConvertTest, IntInt) {
  // Watch out for sign flips.
  EXPECT_TRUE((Case<int32_t, uint16_t>(-0x80000000, false)));
  EXPECT_TRUE((Case<int32_t, uint32_t>(-0x80000000, false)));
  EXPECT_TRUE((Case<int32_t, uint64_t>(-0x80000000, false)));
  EXPECT_TRUE((Case<int32_t, uint16_t>(-1, false)));
  EXPECT_TRUE((Case<int32_t, uint32_t>(-1, false)));
  EXPECT_TRUE((Case<int32_t, uint64_t>(-1, false)));
  EXPECT_TRUE((Case<int32_t, uint16_t>(0, true)));
  EXPECT_TRUE((Case<int32_t, uint32_t>(0, true)));
  EXPECT_TRUE((Case<int32_t, uint64_t>(0, true)));
  EXPECT_TRUE((Case<uint32_t, int16_t>(0, true)));
  EXPECT_TRUE((Case<uint32_t, int32_t>(0, true)));
  EXPECT_TRUE((Case<uint32_t, int64_t>(0, true)));
  EXPECT_TRUE((Case<uint32_t, int16_t>(0x80000000U, false)));
  EXPECT_TRUE((Case<uint32_t, int32_t>(0x80000000U, false)));
  EXPECT_TRUE((Case<uint32_t, int64_t>(0x80000000U, true)));
  EXPECT_TRUE((Case<uint32_t, int16_t>(0xFFFFFFFFU, false)));
  EXPECT_TRUE((Case<uint32_t, int32_t>(0xFFFFFFFFU, false)));
  EXPECT_TRUE((Case<uint32_t, int64_t>(0xFFFFFFFFU, true)));
}

TEST(LosslessConvertTest, BoolEtc) {
  EXPECT_TRUE((Case<int32_t, bool>(-1, false)));
  EXPECT_TRUE((Case<int32_t, bool>(0, true)));
  EXPECT_TRUE((Case<int32_t, bool>(1, true)));
  EXPECT_TRUE((Case<int32_t, bool>(2, false)));

  EXPECT_TRUE((Case<bool, int32_t>(false, true)));
  EXPECT_TRUE((Case<bool, int32_t>(true, true)));

  EXPECT_TRUE((Case<float, bool>(0.0, true)));
  EXPECT_TRUE((Case<float, bool>(0.5, false)));
  EXPECT_TRUE((Case<float, bool>(1.0, true)));
  EXPECT_TRUE((Case<float, bool>(2.0, false)));

  EXPECT_TRUE((Case<bool, float>(false, true)));
  EXPECT_TRUE((Case<bool, float>(true, true)));
}

TEST(LosslessConvertTest, IntDouble) {
  // Large integers lose precision when cast to double.
  EXPECT_TRUE((Case<int64_t, double>(int64_t{-10000000000000001}, false)));
  EXPECT_TRUE((Case<int64_t, double>(int64_t{-1000000000000001}, true)));
  EXPECT_TRUE((Case<int64_t, double>(int64_t{1000000000000001}, true)));
  EXPECT_TRUE((Case<int64_t, double>(int64_t{10000000000000001}, false)));
  EXPECT_TRUE(
      (Case<int64_t, double>(std::numeric_limits<int64_t>::min(), true)));
  EXPECT_TRUE(
      (Case<int64_t, double>(std::numeric_limits<int64_t>::max(), false)));

  // Small integers are fine, aside from sign issues.
  EXPECT_TRUE((Case<double, int64_t>(-1.0, true)));
  EXPECT_TRUE((Case<double, int64_t>(0.0, true)));
  EXPECT_TRUE((Case<double, int64_t>(1.0, true)));
  EXPECT_TRUE((Case<double, uint64_t>(-1.0, false)));

  // Non-integers lose precision when cast to an integer.
  EXPECT_TRUE((Case<double, int64_t>(-0.5, false)));
  EXPECT_TRUE((Case<double, int64_t>(0.5, false)));
  EXPECT_TRUE(
      (Case<double, int64_t>(-std::numeric_limits<double>::infinity(), false)));
  EXPECT_TRUE(
      (Case<double, int64_t>(std::numeric_limits<double>::infinity(), false)));
  EXPECT_TRUE(
      (Case<double, int64_t>(std::numeric_limits<double>::quiet_NaN(), false)));

  // Make sure nothing funny happens near the int64 minimum.
  EXPECT_TRUE((Case<double, int64_t>(-9223372036854775808.0, true)));
  EXPECT_TRUE((Case<double, int64_t>(-9223372036854775808.0 - 2048.0, false)));
}

TEST(LosslessConvertTest, IntLongDouble) {
  if (sizeof(long double) == sizeof(double)) {
    GTEST_SKIP()
        << "skip this test because IntLongDouble test assumes the"
           "`long double` type has more bytes than `double`, but `long double` "
           "is the same size as `double` in this platform";
  }
  // We don't have any integer types large enough to lose precision when
  // converting to long double.
  EXPECT_TRUE(
      (Case<int64_t, long double>(std::numeric_limits<int64_t>::min(), true)));
  EXPECT_TRUE(
      (Case<int64_t, long double>(std::numeric_limits<int64_t>::max(), true)));

  // Small integers are fine, aside from sign issues.
  EXPECT_TRUE((Case<long double, int64_t>(-1.0, true)));
  EXPECT_TRUE((Case<long double, int64_t>(0.0, true)));
  EXPECT_TRUE((Case<long double, int64_t>(1.0, true)));
  EXPECT_TRUE((Case<long double, uint64_t>(-1.0, false)));

  // Non-integers lose precision when cast to an integer.
  EXPECT_TRUE((Case<long double, int64_t>(-0.5, false)));
  EXPECT_TRUE((Case<long double, int64_t>(0.5, false)));
  EXPECT_TRUE((Case<long double, int64_t>(
      -std::numeric_limits<long double>::infinity(), false)));
  EXPECT_TRUE((Case<long double, int64_t>(
      std::numeric_limits<long double>::infinity(), false)));
  EXPECT_TRUE((Case<long double, int64_t>(
      std::numeric_limits<long double>::quiet_NaN(), false)));

  // Make sure nothing funny happens near the int64 minimum.
  EXPECT_TRUE((Case<long double, int64_t>(-9223372036854775808.0, true)));
  EXPECT_TRUE(
      (Case<long double, int64_t>(-9223372036854775808.0 - 2048.0, false)));
}

TEST(LosslessConvertTest, DoubleFloat) {
  // Detect precision loss.
  EXPECT_TRUE((Case<double, float>(10000001, true)));
  EXPECT_TRUE((Case<double, float>(100000001, false)));

  // Detect overflow
  EXPECT_TRUE((Case<double, float>(std::numeric_limits<float>::max(), true)));
  EXPECT_TRUE(
      (Case<double, float>(std::numeric_limits<float>::lowest(), true)));
  EXPECT_TRUE((Case<double, float>(3.4e+39, false)));
  EXPECT_TRUE((Case<double, float>(-3.4e+39, false)));

  // Binary fractions convert cleanly.
  EXPECT_TRUE((Case<double, float>(1.0, true)));
  EXPECT_TRUE((Case<double, float>(1.25, true)));
  EXPECT_TRUE((Case<double, float>(1.5, true)));
  EXPECT_TRUE((Case<float, double>(1.0, true)));
  EXPECT_TRUE((Case<float, double>(1.25, true)));
  EXPECT_TRUE((Case<float, double>(1.5, true)));

  // Non-binary fractions don't narrow well, but widening is okay.
  EXPECT_TRUE((Case<double, float>(1.1, false)));
  EXPECT_TRUE((Case<double, float>(1.3, false)));
  EXPECT_TRUE((Case<double, float>(1.7, false)));
  EXPECT_TRUE((Case<float, double>(1.1, true)));
  EXPECT_TRUE((Case<float, double>(1.3, true)));
  EXPECT_TRUE((Case<float, double>(1.7, true)));

  // NaN and infinity convert cleanly
  EXPECT_TRUE(
      (Case<float, double>(std::numeric_limits<float>::quiet_NaN(), true)));
  EXPECT_TRUE(
      (Case<float, double>(std::numeric_limits<float>::infinity(), true)));
  EXPECT_TRUE(
      (Case<double, float>(std::numeric_limits<double>::quiet_NaN(), true)));
  EXPECT_TRUE(
      (Case<double, float>(std::numeric_limits<double>::infinity(), true)));
}

TEST(LosslessConvertTest, LongDouble) {
  if (sizeof(long double) == sizeof(double)) {
    GTEST_SKIP() << "skip this test because LongDouble test assumes the `long "
                    "double` type has more bytes than `double`, but `long "
                    "double` is the same size as `double` in this platform";
  }
  // Detect precision loss.
  EXPECT_TRUE((Case<long double, float>(10000001, true)));
  EXPECT_TRUE((Case<long double, float>(100000001, false)));
  EXPECT_TRUE((Case<long double, double>(1000000000000001, true)));
  EXPECT_TRUE((Case<long double, double>(10000000000000001, false)));

  // Detect overflow
  EXPECT_TRUE(
      (Case<long double, float>(std::numeric_limits<float>::max(), true)));
  EXPECT_TRUE(
      (Case<long double, float>(std::numeric_limits<float>::lowest(), true)));
  EXPECT_TRUE((Case<long double, float>(3.4e+39, false)));
  EXPECT_TRUE((Case<long double, float>(-3.4e+39, false)));
  EXPECT_TRUE(
      (Case<long double, double>(std::numeric_limits<double>::max(), true)));
  EXPECT_TRUE(
      (Case<long double, double>(std::numeric_limits<double>::lowest(), true)));

  // Apparently `double` and `long double` are both 64 bits on k8
  if (std::numeric_limits<long double>::max() ==
      std::numeric_limits<double>::max()) {
    EXPECT_TRUE((Case<long double, double>(
        std::numeric_limits<long double>::max(), true)));
    EXPECT_TRUE((Case<long double, double>(
        std::numeric_limits<long double>::lowest(), true)));
  } else {
    EXPECT_TRUE((Case<long double, double>(
        std::numeric_limits<long double>::max(), false)));
    EXPECT_TRUE((Case<long double, double>(
        std::numeric_limits<long double>::lowest(), false)));
  }

  // Binary fractions convert cleanly.
  EXPECT_TRUE((Case<long double, float>(1.0, true)));
  EXPECT_TRUE((Case<long double, float>(1.25, true)));
  EXPECT_TRUE((Case<long double, float>(1.5, true)));
  EXPECT_TRUE((Case<float, long double>(1.0, true)));
  EXPECT_TRUE((Case<float, long double>(1.25, true)));
  EXPECT_TRUE((Case<float, long double>(1.5, true)));

  // Non-binary fractions don't narrow well, but widening is okay.
  EXPECT_TRUE((Case<long double, float>(1.1, false)));
  EXPECT_TRUE((Case<long double, float>(1.3, false)));
  EXPECT_TRUE((Case<long double, float>(1.7, false)));
  EXPECT_TRUE((Case<float, long double>(1.1, true)));
  EXPECT_TRUE((Case<float, long double>(1.3, true)));
  EXPECT_TRUE((Case<float, long double>(1.7, true)));

  // NaN and infinity convert cleanly
  EXPECT_TRUE((
      Case<float, long double>(std::numeric_limits<float>::quiet_NaN(), true)));
  EXPECT_TRUE(
      (Case<float, long double>(std::numeric_limits<float>::infinity(), true)));
  EXPECT_TRUE((Case<long double, float>(
      std::numeric_limits<long double>::quiet_NaN(), true)));
  EXPECT_TRUE((Case<long double, float>(
      std::numeric_limits<long double>::infinity(), true)));
}

}  // namespace
