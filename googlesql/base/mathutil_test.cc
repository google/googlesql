//
// Copyright 2018 Google LLC
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

// Test functions in MathUtil.

#include "googlesql/base/mathutil.h"

#include <stdio.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"

namespace googlesql_base {
namespace {

using ::testing::Eq;

TEST(MathUtil, Round) {
  // test float rounding
  EXPECT_EQ(MathUtil::FastIntRound(0.7f), 1);
  EXPECT_EQ(MathUtil::FastIntRound(5.7f), 6);
  EXPECT_EQ(MathUtil::FastIntRound(6.3f), 6);
  EXPECT_EQ(MathUtil::FastIntRound(1000000.7f), 1000001);

  // test that largest representable number below 0.5 rounds to zero.
  // this is important because naive implementation of round:
  // static_cast<int>(r + 0.5f) is 1 due to implicit rounding in operator+
  float rf = std::nextafter(0.5f, .0f);
  EXPECT_LT(rf, 0.5f);
  EXPECT_EQ(MathUtil::Round<int>(rf), 0);

  // same test for double
  double rd = std::nextafter(0.5, 0.0);
  EXPECT_LT(rd, 0.5);
  EXPECT_EQ(MathUtil::Round<int>(rd), 0);

  // same test for long double
  long double rl = std::nextafter(0.5l, 0.0l);
  EXPECT_LT(rl, 0.5l);
  EXPECT_EQ(MathUtil::Round<int>(rl), 0);
}

TEST(MathUtil, IntRound) {
  EXPECT_EQ(MathUtil::Round<int>(0.0), 0);
  EXPECT_EQ(MathUtil::Round<int>(0.49), 0);
  EXPECT_EQ(MathUtil::Round<int>(1.49), 1);
  EXPECT_EQ(MathUtil::Round<int>(-0.49), 0);
  EXPECT_EQ(MathUtil::Round<int>(-1.49), -1);

  // Either adjacent integer is an acceptable result.
  EXPECT_EQ(fabs(MathUtil::Round<int>(0.5) - 0.5), 0.5);
  EXPECT_EQ(fabs(MathUtil::Round<int>(1.5) - 1.5), 0.5);
  EXPECT_EQ(fabs(MathUtil::Round<int>(-0.5) + 0.5), 0.5);
  EXPECT_EQ(fabs(MathUtil::Round<int>(-1.5) + 1.5), 0.5);

  EXPECT_EQ(MathUtil::Round<int>(static_cast<double>(0x76543210)), 0x76543210);

  // A double-precision number has a 53-bit mantissa (52 fraction bits),
  // so the following value can be represented exactly.
  int64_t value64 = 0x1234567890abcd00;
  EXPECT_EQ(MathUtil::Round<int64_t>(static_cast<double>(value64)), value64);
}

template<typename Type>
void TestSimpleUtils() {
  ABSL_LOG(INFO) << "testing Max Min Abs Sign AbsDiff for " << sizeof(Type);

  const Type kZero = 0.0;
  const Type kNegZero = -0.0;
  const Type kFinite = 2.67;
  const Type kNaN = std::numeric_limits<Type>::quiet_NaN();
  const Type kPosInf = std::numeric_limits<Type>::infinity();
  const Type kNegInf = -std::numeric_limits<Type>::infinity();

  EXPECT_TRUE(MathUtil::Min(kZero, kNegZero) == kNegZero);
  EXPECT_TRUE(MathUtil::Min(kNegZero, kZero) == kNegZero);
  // these four tests above work because kNegZero and kZero
  // are indistinguishable w.r.t. == :
  EXPECT_TRUE(kNegZero == kZero);

  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kFinite)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kFinite, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, -kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kPosInf)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kNegInf)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kPosInf, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNegInf, kNaN)));

  EXPECT_TRUE(MathUtil::Min(kPosInf, kFinite) == kFinite);
  EXPECT_TRUE(MathUtil::Min(kFinite, kPosInf) == kFinite);
  EXPECT_EQ(MathUtil::Min(kPosInf, kPosInf), kPosInf);
  EXPECT_EQ(MathUtil::Min(kPosInf, kNegInf), kNegInf);
  EXPECT_EQ(MathUtil::Min(kNegInf, kPosInf), kNegInf);

  EXPECT_EQ(MathUtil::Min(kNegInf, kFinite), kNegInf);
  EXPECT_EQ(MathUtil::Min(kFinite, kNegInf), kNegInf);
  EXPECT_EQ(MathUtil::Min(kNegInf, kNegInf), kNegInf);

  EXPECT_TRUE(MathUtil::Abs(kZero) == kZero);
  EXPECT_TRUE(MathUtil::Abs(kNegZero) == kZero);
  EXPECT_TRUE(MathUtil::Abs(kFinite) == kFinite);
  EXPECT_TRUE(MathUtil::Abs(-kFinite) == kFinite);
  EXPECT_TRUE(std::isnan(MathUtil::Abs(kNaN)));
  EXPECT_EQ(MathUtil::Abs(kPosInf), kPosInf);
  EXPECT_EQ(MathUtil::Abs(kNegInf), kPosInf);

  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kFinite)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kFinite, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, -kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kPosInf)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kNegInf)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kPosInf, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNegInf, kNaN)));

  EXPECT_EQ(MathUtil::AbsDiff(kPosInf, kFinite), kPosInf);
  EXPECT_EQ(MathUtil::AbsDiff(kFinite, kPosInf), kPosInf);
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kPosInf, kPosInf)));
  EXPECT_EQ(MathUtil::AbsDiff(kPosInf, kNegInf), kPosInf);
  EXPECT_EQ(MathUtil::AbsDiff(kNegInf, kPosInf), kPosInf);

  EXPECT_EQ(MathUtil::AbsDiff(kNegInf, kFinite), kPosInf);
  EXPECT_EQ(MathUtil::AbsDiff(kFinite, kNegInf), kPosInf);
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNegInf, kNegInf)));
}

TEST(MathUtil, SimpleUtils) {
  TestSimpleUtils<float>();
  TestSimpleUtils<double>();
  TestSimpleUtils<long double>();
}

// Number of arguments for each test of the CeilOrRatio method
const int kNumTestArguments = 4;

template<typename IntegralType>
void TestCeilOfRatio(const IntegralType test_data[][kNumTestArguments],
                     int num_tests) {
  for (int i = 0; i < num_tests; ++i) {
    const IntegralType numerator = test_data[i][0];
    const IntegralType denominator = test_data[i][1];
    const IntegralType expected_floor = test_data[i][2];
    const IntegralType expected_ceil = test_data[i][3];
    // Make sure the two ways to compute the floor return the same thing.
    IntegralType floor_1 = MathUtil::FloorOfRatio(numerator, denominator);
    IntegralType floor_2 = MathUtil::CeilOrFloorOfRatio<IntegralType, false>(
        numerator, denominator);
    EXPECT_EQ(floor_1, floor_2);
    EXPECT_EQ(expected_floor, floor_1)
        << "FloorOfRatio fails with numerator = " << numerator
        << ", denominator = " << denominator
        << (std::numeric_limits<IntegralType>::is_signed ? "signed "
                                                         : "unsigned ")
        << (8 * sizeof(IntegralType)) << " bits";
    IntegralType ceil_1 = MathUtil::CeilOrFloorOfRatio<IntegralType, true>(
        numerator, denominator);
    EXPECT_EQ(expected_ceil, ceil_1)
        << "CeilOfRatio fails with numerator = " << numerator
        << ", denominator = " << denominator
        << (std::numeric_limits<IntegralType>::is_signed ? "signed "
                                                         : "unsigned ")
        << (8 * sizeof(IntegralType)) << " bits";
  }
}

template<typename UnsignedIntegralType>
void TestCeilOfRatioUnsigned() {
  static_assert(std::numeric_limits<UnsignedIntegralType>::is_integer);
  static_assert(!std::numeric_limits<UnsignedIntegralType>::is_signed);
  constexpr UnsignedIntegralType kMax =
      std::numeric_limits<UnsignedIntegralType>::max();
  const UnsignedIntegralType kTestData[][kNumTestArguments] = {
// Numerator  | Denominator | Expected floor of ratio | Expected ceil of ratio |
      // When numerator = 0, the result is always zero
      {      0,            1,                        0,                     0 },
      {      0,            2,                        0,                     0 },
      {      0,         kMax,                        0,                     0 },
      // Try some non-extreme cases
      {      1,            1,                        1,                     1 },
      {      5,            2,                        2,                     3 },
      // Try with huge positive numerator
      {   kMax,            1,                     kMax,                  kMax },
      {   kMax,            2, kMax / 2,  kMax / 2 + ((kMax % 2 != 0) ? 1 : 0) },
      {   kMax,            3, kMax / 3,  kMax / 3 + ((kMax % 3 != 0) ? 1 : 0) },
      // Try with a huge positive denominator
      {      1,         kMax,                        0,                     1 },
      {      2,         kMax,                        0,                     1 },
      {      3,         kMax,                        0,                     1 },
      // Try with a huge numerator and a huge denominator
      {   kMax,         kMax,                        1,                     1 },
  };
  const int kNumTests = ABSL_ARRAYSIZE(kTestData);
  TestCeilOfRatio<UnsignedIntegralType>(kTestData, kNumTests);
}

template<typename SignedInteger>
void TestCeilOfRatioSigned() {
  static_assert(std::numeric_limits<SignedInteger>::is_integer);
  static_assert(std::numeric_limits<SignedInteger>::is_signed);
  constexpr SignedInteger kMin = std::numeric_limits<SignedInteger>::lowest();
  constexpr SignedInteger kMax = std::numeric_limits<SignedInteger>::max();
  const SignedInteger kTestData[][kNumTestArguments] = {
// Numerator  | Denominator | Expected floor of ratio | Expected ceil of ratio |
      // When numerator = 0, the result is always zero
      {      0,            1,                        0,                     0 },
      {      0,           -1,                        0,                     0 },
      {      0,            2,                        0,                     0 },
      {      0,         kMin,                        0,                     0 },
      {      0,         kMax,                        0,                     0 },
      // Try all four combinations of 1 and -1
      {      1,            1,                        1,                     1 },
      {     -1,            1,                       -1,                    -1 },
      {      1,           -1,                       -1,                    -1 },
      {     -1,           -1,                        1,                     1 },
      // Try all four combinations of +/-5 divided by +/- 2
      {      5,            2,                        2,                     3 },
      {     -5,            2,                       -3,                    -2 },
      {      5,           -2,                       -3,                    -2 },
      {     -5,           -2,                        2,                     3 },
      // Try with huge positive numerator
      {   kMax,            1,                     kMax,                  kMax },
      {   kMax,           -1,                    -kMax,                 -kMax },
      {   kMax,            2, kMax / 2,  kMax / 2 + ((kMax % 2 != 0) ? 1 : 0) },
      {   kMax,            3, kMax / 3,  kMax / 3 + ((kMax % 3 != 0) ? 1 : 0) },
      // Try with huge negative numerator
      {   kMin,            1,                     kMin,                  kMin },
      {   kMin,            2, kMin / 2 - ((kMin % 2 != 0) ? 1 : 0),  kMin / 2 },
      {   kMin,            3, kMin / 3 - ((kMin % 3 != 0) ? 1 : 0),  kMin / 3 },
      // Try with a huge positive denominator
      {      1,         kMax,                        0,                     1 },
      {      2,         kMax,                        0,                     1 },
      {      3,         kMax,                        0,                     1 },
      // Try with a huge negative denominator
      {      1,         kMin,                       -1,                     0 },
      {      2,         kMin,                       -1,                     0 },
      {      3,         kMin,                       -1,                     0 },
      // Try with a huge numerator and a huge denominator
      {   kMin,         kMin,                        1,                     1 },
      {   kMin,         kMax,                       -2,                    -1 },
      {   kMax,         kMin,                       -1,                     0 },
      {   kMax,         kMax,                        1,                     1 },
  };
  const int kNumTests = ABSL_ARRAYSIZE(kTestData);
  TestCeilOfRatio<SignedInteger>(kTestData, kNumTests);
}

// ------------------------------------------------------------------------ //
// Benchmarking CeilOrFloorOfRatio
//
// We compare with other implementations that are unsafe in general.
// ------------------------------------------------------------------------ //

// An implementation of CeilOfRatio that is correct for small enough values,
// and provided that the numerator and denominator are both positive
template <typename IntegralType>
IntegralType CeilOfRatioDenomMinusOne(IntegralType numerator,
                                      IntegralType denominator) {
  const IntegralType kOne(1);
  return (numerator + denominator - kOne) / denominator;
}

// An implementation of FloorOfRatio that is correct when the denominator is
// positive and the numerator non-negative
template <typename IntegralType>
IntegralType FloorOfRatioByDivision(IntegralType numerator,
                                    IntegralType denominator) {
  return numerator / denominator;
}

template <typename Integer, bool ComputeCeil>
Integer CeilOrFloorOfRatioArithmetic(Integer numerator, Integer denominator) {
  if (ComputeCeil) {
    return CeilOfRatioDenomMinusOne(numerator, denominator);
  } else {
    return FloorOfRatioByDivision(numerator, denominator);
  }
}

// Implementations of the CeilOrRatio function.
enum Implementation {
  PROVIDED,   // The method provided by MathUtil in the .h file
  ARITHMETIC  // Using arithmetic tricks
};

template <typename Integer, bool ComputeCeil, Implementation Implementation>
Integer CeilOrFloorOfRatio(Integer numerator, Integer denominator) {
  switch (Implementation) {  // Compile time switch
    case PROVIDED:
      return MathUtil::CeilOrFloorOfRatio<Integer, ComputeCeil>(
          numerator, denominator);
    case ARITHMETIC:
      return CeilOrFloorOfRatioArithmetic<Integer, ComputeCeil>(
          numerator, denominator);
    default:
      ABSL_LOG(FATAL) << "This statement was supposed to be unreachable";
  }
}

void TestThatCeilOfRatioDenomMinusOneIsIncorrect(int64_t numerator,
                                                 int64_t denominator,
                                                 int64_t expected_error) {
  const int64_t correct_result =
      MathUtil::CeilOrFloorOfRatio<int64_t, true>(numerator, denominator);
  // MathUtil::CeilOfRatio(numerator, denominator);
  const int64_t result_by_denom_minus_one =
      CeilOfRatioDenomMinusOne(numerator, denominator);
  EXPECT_EQ(result_by_denom_minus_one + expected_error, correct_result)
      << "numerator = " << numerator << " denominator = " << denominator
      << " expected error = " << expected_error
      << " Actual difference: " << (correct_result - result_by_denom_minus_one);
}

// Here we demonstrate why not to use CeilOfRatioDenomMinusOne
void TestThatCeilOfRatioDenomMinusOneIsIncorrect() {
  // It does not work with negative values
  TestThatCeilOfRatioDenomMinusOneIsIncorrect(int64_t{-1}, int64_t{-2},
                                              int64_t{-1});

  // This would also fail if given kint64max because of signed integer overflow.
}

TEST(MathUtil, CeilOfRatio) {
  TestCeilOfRatioUnsigned<uint8_t>();
  TestCeilOfRatioUnsigned<uint16_t>();
  TestCeilOfRatioUnsigned<uint32_t>();
  TestCeilOfRatioUnsigned<uint64_t>();
  TestCeilOfRatioSigned<int8_t>();
  TestCeilOfRatioSigned<int16_t>();
  TestCeilOfRatioSigned<int32_t>();
  TestCeilOfRatioSigned<int64_t>();
  TestThatCeilOfRatioDenomMinusOneIsIncorrect();
}

TEST(MathUtil, NonnegativeMod) {
  // Test compliance with the standard -- integer division is supposed to
  // truncate towards zero, and integer mod is defined accordingly.
  ASSERT_THAT(-8 / 5, Eq(-1));
  ASSERT_THAT(-8 % 5, Eq(-3));
  // Now test the function itself.
  EXPECT_THAT(MathUtil::NonnegativeMod(4, 3), Eq(1));
  EXPECT_THAT(MathUtil::NonnegativeMod(-5, 3), Eq(1));
  EXPECT_THAT(MathUtil::NonnegativeMod(9, 3), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(-9, 3), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(0, 3), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(12U, 5U), Eq(2U));
  int64_t a_int64 = 10000000002;
  int64_t b_int64 = 10000000000;
  int64_t two_int64 = 2;
  EXPECT_THAT(MathUtil::NonnegativeMod(a_int64, b_int64), Eq(two_int64));
  EXPECT_THAT(MathUtil::NonnegativeMod(-a_int64, b_int64),
              Eq(b_int64 - two_int64));
  EXPECT_THAT(MathUtil::NonnegativeMod(b_int64, b_int64), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(-b_int64, b_int64), Eq(0));
}

TEST(MathUtil, DecomposeDouble) {
  using Limits = std::numeric_limits<double>;

  struct TestItem {
    double value;
    int64_t expected_mantissa;
    int expected_exponent;
  };

  static const TestItem kItems[] = {
      {0.0, 0, -1074},
      {Limits::denorm_min(), 1, -1074},
      {Limits::min() - Limits::denorm_min(), (1LL << 52) - 1, -1074},
      {Limits::min(), (1LL << 52), -1074},
      {1.0 / (1LL << 52), (1LL << 52), -104},
      {1.0, (1LL << 52), -52},
      {100, (100LL << 46), -46},
      {static_cast<double>(std::numeric_limits<uint64_t>::max()),
       (std::numeric_limits<uint64_t>::max() >> 12) + 1, 12},
      {Limits::max(), (1LL << 53) - 1, 971},
      {Limits::infinity(), std::numeric_limits<int64_t>::max(),
       std::numeric_limits<int>::max()},
  };
  for (const TestItem& item : kItems) {
    ABSL_LOG(INFO) << "Testing ExtractMantissaAndExponent(" << item.value << ")...";

    MathUtil::DoubleParts parts = MathUtil::Decompose(item.value);
    EXPECT_EQ(item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(item.value,
              std::ldexp(static_cast<double>(parts.mantissa), parts.exponent));
    EXPECT_EQ(item.value, static_cast<double>(parts.mantissa) *
                              std::ldexp(1.0, parts.exponent));

    parts = MathUtil::Decompose(-item.value);
    EXPECT_EQ(-item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(-item.value,
              std::ldexp(static_cast<double>(parts.mantissa), parts.exponent));
    EXPECT_EQ(-item.value, static_cast<double>(parts.mantissa) *
                               std::ldexp(1.0, parts.exponent));
  }

  MathUtil::DoubleParts parts = MathUtil::Decompose(Limits::quiet_NaN());
  EXPECT_EQ(0, parts.mantissa);
  EXPECT_EQ(std::numeric_limits<int>::max(), parts.exponent);
  EXPECT_TRUE(std::isnan(static_cast<double>(parts.mantissa) *
                         std::ldexp(1.0, parts.exponent)));
}

TEST(MathUtil, DecomposeFloat) {
  using Limits = std::numeric_limits<float>;

  struct TestItem {
    float value;
    int32_t expected_mantissa;
    int expected_exponent;
  };

  static const TestItem kItems[] = {
      {0.0, 0, -149},
      {1.2, 10066330, -23},
      {-1.2, -10066330, -23},
      {100, 100 << 17, -17},
      {Limits::denorm_min(), 1, -149},
      {Limits::min() - Limits::denorm_min(), (1 << 23) - 1, -149},
      {Limits::min(), 1 << 23, -149},
      {Limits::max(), 16777215, 104},
      {Limits::infinity(), std::numeric_limits<int32_t>::max(),
       std::numeric_limits<int>::max()},
  };
  for (const TestItem& item : kItems) {
    ABSL_LOG(INFO) << "Testing ExtractMantissaAndExponent(" << item.value << ")...";

    MathUtil::FloatParts parts = MathUtil::Decompose(item.value);
    EXPECT_EQ(item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(item.value,
              std::ldexp(static_cast<float>(parts.mantissa), parts.exponent));
    EXPECT_EQ(item.value, static_cast<float>(parts.mantissa) *
                              std::ldexp(1.0, parts.exponent));

    parts = MathUtil::Decompose(-item.value);
    EXPECT_EQ(-item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(-item.value,
              std::ldexp(static_cast<float>(parts.mantissa), parts.exponent));
    EXPECT_EQ(-item.value, static_cast<float>(parts.mantissa) *
                               std::ldexp(1.0, parts.exponent));
  }

  MathUtil::FloatParts parts = MathUtil::Decompose(Limits::quiet_NaN());
  EXPECT_EQ(0, parts.mantissa);
  EXPECT_EQ(std::numeric_limits<int>::max(), parts.exponent);
  EXPECT_TRUE(std::isnan(static_cast<float>(parts.mantissa) *
                         std::ldexp(1.0, parts.exponent)));
}

template <typename T>
void TestOneIPowN() {
  const T one{1};
  for (int i = 0; i < 1024; ++i) {
    // Computations are exact.
    EXPECT_EQ(MathUtil::IPow(one, i), one);
  }
}

template <typename T>
void TestTwoIPowN() {
  int limit = std::is_integral<T>::value ? std::numeric_limits<T>::digits : 63;
  for (int i = 0; i < limit; ++i) {
    // Computations are exact.
    EXPECT_EQ(MathUtil::IPow(T{2}, i), static_cast<T>(1ull << i));
  }
}

template <typename T>
void TestFloatIPow(const int max_exponent, const T start, const T end,
                   const T step) {
  for (T f = start; f < end; f += step) {
    for (int i = 0; i < max_exponent; ++i) {
      EXPECT_FLOAT_EQ(MathUtil::IPow(f, i), std::pow(f, i));
    }
  }
}

TEST(MathUtil, IPow) {
  TestOneIPowN<double>();
  TestOneIPowN<float>();
  TestOneIPowN<int>();
  TestOneIPowN<int64_t>();
  TestTwoIPowN<double>();
  TestTwoIPowN<float>();
  TestTwoIPowN<int>();
  TestTwoIPowN<int64_t>();

  EXPECT_EQ(MathUtil::IPow(3, 0), 1);
  EXPECT_EQ(MathUtil::IPow(3, 1), 3);
  EXPECT_EQ(MathUtil::IPow(3, 2), 9);
  EXPECT_EQ(MathUtil::IPow(3, 3), 27);
  EXPECT_EQ(MathUtil::IPow(3, 4), 81);
  EXPECT_EQ(MathUtil::IPow(3, 5), 243);

  TestFloatIPow<float>(13, -16.0f, 16.0f, 1.0f / 8);
  TestFloatIPow<double>(13, -16.0, 16.0, 1.0 / 8);

  TestFloatIPow<float>(13, -1.0f / (1 << 12), -1.0f / (1 << 12),
                       1.0f / (1 << 16));
  TestFloatIPow<double>(13, -1.0 / (1 << 12), -1.0 / (1 << 12),
                        1.0 / (1 << 16));
}

TEST(MathUtil, IPowEdgeCases) {
  constexpr const double kInf = std::numeric_limits<double>::infinity();

  EXPECT_EQ(MathUtil::IPow(-12345.0, 79), -kInf);
  EXPECT_EQ(MathUtil::IPow(-12345.0, 80), +kInf);

  // The semantics of the edge cases that follow  are defined in the standard:
  // http://en.cppreference.com/w/cpp/numeric/math/pow for a summary.


  // 1 - These edge cases apply.
  // pow(+0, exp), where exp is a positive odd integer, returns +0
  EXPECT_EQ(MathUtil::IPow(+0.0, 3), +0.0);
  // pow(-0, exp), where exp is a positive odd integer, returns -0
  EXPECT_EQ(MathUtil::IPow(-0.0, 3), -0.0);
  // pow(±0, exp), where exp is positive non-integer or a positive even integer,
  // returns +0
  EXPECT_EQ(MathUtil::IPow(+0.0, 42), +0.0);
  EXPECT_EQ(MathUtil::IPow(-0.0, 42), +0.0);
  // pow(base, ±0) returns 1 for any base, even when base is NaN
  EXPECT_EQ(MathUtil::IPow(-kInf, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(-2.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(-1.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(-0.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+0.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+1.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+2.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+kInf, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(std::numeric_limits<double>::quiet_NaN(), 0.0), 1.0);
  // pow(-∞, exp) returns -∞ if exp is a positive odd integer
  EXPECT_EQ(MathUtil::IPow(-kInf, 43), -kInf);
  // pow(-∞, exp) returns +∞ if exp is a positive non-integer or even integer
  EXPECT_EQ(MathUtil::IPow(-kInf, 42), +kInf);
  // pow(+∞, exp) returns +∞ for any positive exp
  EXPECT_EQ(MathUtil::IPow(+kInf, 42), +kInf);
  EXPECT_EQ(MathUtil::IPow(+kInf, 43), +kInf);

  EXPECT_EQ(pow(std::numeric_limits<int>::max() >> 0, 1),
            MathUtil::IPow(std::numeric_limits<int>::max() >> 0, 1));
  EXPECT_EQ(pow(std::numeric_limits<int>::max() >> 16, 2),
            MathUtil::IPow(std::numeric_limits<int>::max() >> 16, 2));

  // 2 - These do not apply due to the restricted exp range.
  // pow(+0, exp), where exp is a negative odd integer, returns +∞ and raises
  // FE_DIVBYZERO pow(-0, exp), where exp is a negative odd integer, returns -∞
  // and raises FE_DIVBYZERO pow(±0, exp), where exp is negative, finite, and is
  // an even integer or a non-integer, returns +∞ and raises FE_DIVBYZERO
  // pow(-1, ±∞) returns 1
  // pow(+1, exp) returns 1 for any exp, even when exp is NaN
  // pow(±0, -∞) returns +∞ and may raise FE_DIVBYZERO
  // pow(base, exp) returns NaN and raises FE_INVALID if base is finite and
  // negative and exp is finite and non-integer. pow(base, -∞) returns +∞ for
  // any |base|<1 pow(base, -∞) returns +0 for any |base|>1 pow(base, +∞)
  // returns +0 for any |base|<1 pow(base, +∞) returns +∞ for any |base|>1
  // pow(-∞, exp) returns -0 if exp is a negative odd integer
  // pow(-∞, exp) returns +0 if exp is a negative non-integer or even integer
  // pow(+∞, exp) returns +0 for any negative exp
}

}  // namespace
}  // namespace googlesql_base
