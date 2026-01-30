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

#include "googlesql/public/functions/percentile.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "googlesql/common/string_util.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {
template <typename T> using limits = std::numeric_limits<T>;

constexpr size_t kTwoTo62 = 1ULL << 62;
constexpr size_t kTwoTo63 = 1ULL << 63;
constexpr size_t kUint64Max = std::numeric_limits<uint64_t>::max();
constexpr double kNaN = limits<double>::quiet_NaN();
constexpr double kInf = limits<double>::infinity();
constexpr double kDoubleMax = limits<double>::max();
constexpr double kDoubleMin = limits<double>::min();
constexpr double kDoubleDenormalMin = limits<double>::denorm_min();
constexpr double kDoubleDenormalMax = kDoubleMin - kDoubleDenormalMin;
constexpr absl::string_view kNumericMin =
    "-99999999999999999999999999999.999999999";
constexpr absl::string_view kNumericMax =
    "99999999999999999999999999999.999999999";
constexpr absl::string_view kBigNumericMin =
    "-578960446186580977117854925043439539266"
    ".34992332820282019728792003956564819968";
constexpr absl::string_view kBigNumericMax =
    "578960446186580977117854925043439539266"
    ".34992332820282019728792003956564819967";

template <typename PercentileType, typename Weight = PercentileType>
struct PercentileIndexTestItem {
  PercentileType percentile;
  size_t max_index;
  size_t expected_index;
  Weight expected_left_weight;
  Weight expected_right_weight;
};

inline std::string ToString(double value) {
  return RoundTripDoubleToString(value);
}

template <typename NumericType>
inline std::string ToString(NumericType value) {
  return value.ToString();
}

template <typename NumericType>
inline NumericType operator+(NumericType lhs, NumericType rhs) {
  return lhs.Add(rhs).value();
}

template <typename NumericType>
inline NumericType operator-(NumericType lhs, NumericType rhs) {
  return lhs.Subtract(rhs).value();
}

template <typename PercentileType, typename Weight = PercentileType>
void TestComputePercentileIndex(
    const PercentileIndexTestItem<PercentileType, Weight>& item) {
  SCOPED_TRACE(absl::StrCat(" percentile=", ToString(item.percentile),
                            " max_index=", item.max_index));
  Weight left_weight = Weight(-1);
  Weight right_weight = Weight(-1);
  using Evaluator = PercentileEvaluator<PercentileType, Weight>;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Evaluator percentile_evalutor,
                       Evaluator::Create(item.percentile));
  EXPECT_EQ(item.expected_index,
            percentile_evalutor.ComputePercentileIndex(
                item.max_index, &left_weight, &right_weight));
  // Intentionally use EXPECT_EQ rather than EXPECT_DOUBLE_EQ or EXPECT_NEAR.
  // The results are expected to be precisely equal.
  EXPECT_EQ(item.expected_left_weight, left_weight);
  EXPECT_EQ(item.expected_right_weight, right_weight);
  EXPECT_EQ(Weight(1), left_weight + right_weight);

  const PercentileType complement_percentile =
      PercentileType(1) - item.percentile;
  // When item.percentile is too small, then 1 - item.percentile = 1,
  // and we can't test the complement.
  if (PercentileType(1) - complement_percentile == item.percentile) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Evaluator complement_percentile_evalutor,
                         Evaluator::Create(complement_percentile));
    const size_t index = complement_percentile_evalutor.ComputePercentileIndex(
        item.max_index, &left_weight, &right_weight);
    if (item.expected_right_weight > Weight(0)) {
      EXPECT_EQ(item.max_index - item.expected_index - 1, index);
      EXPECT_EQ(item.expected_right_weight, left_weight);
      EXPECT_EQ(item.expected_left_weight, right_weight);
    } else {
      EXPECT_EQ(item.max_index - item.expected_index, index);
      EXPECT_EQ(Weight(1), left_weight);
      EXPECT_EQ(Weight(0), right_weight);
    }
  }
}

template <typename Weight>
void TestComputeDoublePercentileIndex() {
  constexpr Weight kTwoToMinus65 = 0.25 / kTwoTo63;
  constexpr Weight kTwoToMinus64 = 0.5 / kTwoTo63;
  constexpr Weight kTwoToMinus63 = 1.0 / kTwoTo63;
  constexpr Weight kTwoToMinus52 = 1.0 / (1ULL << 52);  // double epsilon
  static constexpr PercentileIndexTestItem<double, Weight> kTestItems[] = {
      // {percentile, max_index, expected_index, expected_left_weight,
      // expected_right_weight}
      {1, 0, 0, 1, 0},
      {1, 1, 1, 1, 0},
      {1, 99, 99, 1, 0},
      {1, 100, 100, 1, 0},
      {1, kTwoTo63 - 1, kTwoTo63 - 1, 1, 0},
      {1, kTwoTo63, kTwoTo63, 1, 0},
      {1, kUint64Max - 1, kUint64Max - 1, 1, 0},
      {1, kUint64Max, kUint64Max, 1, 0},

      {0.5, 0, 0, 1, 0},
      {0.5, 1, 0, 0.5, 0.5},
      {0.5, 99, 49, 0.5, 0.5},
      {0.5, 100, 50, 1, 0},
      {0.5, kTwoTo63 - 1, kTwoTo63 / 2 - 1, 0.5, 0.5},
      {0.5, kTwoTo63, kTwoTo63 / 2, 1, 0},
      {0.5, kUint64Max - 1, kTwoTo63 - 1, 1, 0},
      {0.5, kUint64Max, kTwoTo63 - 1, 0.5, 0.5},

      {0.25, 0, 0, 1, 0},
      {0.25, 1, 0, 0.75, 0.25},
      {0.25, 99, 24, 0.25, 0.75},
      {0.25, 100, 25, 1, 0},
      {0.25, kTwoTo63 - 1, kTwoTo63 / 4 - 1, 0.25, 0.75},
      {0.25, kTwoTo63, kTwoTo63 / 4, 1, 0},
      {0.25, kUint64Max - 1, kTwoTo62 - 1, 0.5, 0.5},
      {0.25, kUint64Max, kTwoTo62 - 1, 0.25, 0.75},

      {kTwoToMinus63, 0, 0, 1, 0},
      {kTwoToMinus63, 1, 0, 1.0L - kTwoToMinus63, kTwoToMinus63},
      {kTwoToMinus63, 99, 0, 1.0L - 99 * kTwoToMinus63, 99 * kTwoToMinus63},
      {kTwoToMinus63, 100, 0, 1.0L - 100 * kTwoToMinus63, 100 * kTwoToMinus63},
      {kTwoToMinus63, kTwoTo63 - 1, 0, kTwoToMinus63, 1.0L - kTwoToMinus63},
      {kTwoToMinus63, kTwoTo63, 1, 1, 0},
      {kTwoToMinus63, kUint64Max - 1, 1, 2 * kTwoToMinus63,
       1.0L - 2 * kTwoToMinus63},
      {kTwoToMinus63, kUint64Max, 1, kTwoToMinus63, 1.0L - kTwoToMinus63},

      {7 * kTwoToMinus65, 0, 0, 1, 0},
      {7 * kTwoToMinus65, 1, 0, 1.0L - 7 * kTwoToMinus65, 7 * kTwoToMinus65},
      {7 * kTwoToMinus65, 99, 0, 1.0L - 99 * 7 * kTwoToMinus65,
       99 * 7 * kTwoToMinus65},
      {7 * kTwoToMinus65, 100, 0, 1.0L - 100 * 7 * kTwoToMinus65,
       100 * 7 * kTwoToMinus65},
      {7 * kTwoToMinus65, kTwoTo63 - 1, 1, 0.25 + 7 * kTwoToMinus65,
       0.75 - 7 * kTwoToMinus65},
      {7 * kTwoToMinus65, kTwoTo63, 1, 0.25, 0.75},
      {7 * kTwoToMinus65, kUint64Max - 1, 3, 0.5 + 14 * kTwoToMinus65,
       0.5 - 14 * kTwoToMinus65},
      {7 * kTwoToMinus65, kUint64Max, 3, 0.5 + 7 * kTwoToMinus65,
       0.5 - 7 * kTwoToMinus65},

      {1 - kTwoToMinus52, 0, 0, 1, 0},
      {1 - kTwoToMinus52, 1, 0, kTwoToMinus52, 1.0L - kTwoToMinus52},
      {1 - kTwoToMinus52, 99, 98, 99 * kTwoToMinus52, 1 - 99 * kTwoToMinus52},
      {1 - kTwoToMinus52, 100, 99, 100 * kTwoToMinus52,
       1 - 100 * kTwoToMinus52},
      {1 - kTwoToMinus52, kTwoTo63 - 1, kTwoTo63 - (1ULL << 11) - 1,
       1 - kTwoToMinus52, kTwoToMinus52},
      {1 - kTwoToMinus52, kTwoTo63, kTwoTo63 - (1ULL << 11), 1, 0},
      {1 - kTwoToMinus52, kUint64Max - 1, kUint64Max - (1ULL << 12) - 1,
       1 - 2 * kTwoToMinus52, 2 * kTwoToMinus52},
      {1 - kTwoToMinus52, kUint64Max, kUint64Max - (1ULL << 12),
       1 - kTwoToMinus52, kTwoToMinus52},
  };

  for (const PercentileIndexTestItem<double, Weight>& item : kTestItems) {
    TestComputePercentileIndex<double, Weight>(item);
  }

  // Test percentile values so small that the returned index is always 0.
  ASSERT_TRUE(std::isnormal(kDoubleMin));
  ASSERT_FALSE(std::isnormal(kDoubleDenormalMin));
  ASSERT_FALSE(std::isnormal(kDoubleDenormalMax));
  static const double kSmallPercnetiles[] =
      {-0.0, 0.0, kDoubleDenormalMin, kDoubleDenormalMax, kDoubleMin,
       kTwoToMinus64};
  static const size_t kUint64Maxes[] = {0,        1,         100, kTwoTo63 - 1,
                                        kTwoTo63, kUint64Max};
  for (double percentile : kSmallPercnetiles) {
    using Evaluator = PercentileEvaluator<double, Weight>;
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Evaluator percentile_evalutor,
                         Evaluator::Create(percentile));
    for (size_t max_index : kUint64Maxes) {
      SCOPED_TRACE(
          absl::StrCat("percentile=", RoundTripDoubleToString(percentile),
                       " max_index=", max_index));
      Weight left_weight = -1;
      Weight right_weight = -1;
      EXPECT_EQ(0, percentile_evalutor.ComputePercentileIndex(
          max_index, &left_weight, &right_weight));
      Weight expected_right_weight =
          percentile * static_cast<Weight>(max_index);
      EXPECT_EQ(expected_right_weight, right_weight);
      EXPECT_EQ(1, left_weight + right_weight);
    }
  }
}

TEST(DoublePercentileTest, ComputePercentileIndex) {
  TestComputeDoublePercentileIndex<double>();
  TestComputeDoublePercentileIndex<long double>();
}

TEST(DoublePercentileTest, InvalidPercentiles) {
  static constexpr double kInvalidPercentiles[] = {
      kNaN,
      -kInf,
      -kDoubleMax,
      -kDoubleMin,
      1 + limits<double>::epsilon(),
      kDoubleMax,
      kInf};
  for (double percentile : kInvalidPercentiles) {
    EXPECT_THAT(PercentileEvaluator<double>::Create(percentile),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

struct NumericPercentileIndexTestItem {
  absl::string_view percentile_str;
  size_t max_index;
  size_t expected_index;
  // expected_left_weight is always precisely 1 - expected_right_weight
  absl::string_view expected_right_weight_str;
};

template <typename PercentileType>
void TestComputeNumericPercentileIndex(
    absl::Span<const NumericPercentileIndexTestItem> items) {
  for (const NumericPercentileIndexTestItem& item : items) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(PercentileType percentile,
                         PercentileType::FromStringStrict(item.percentile_str));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        PercentileType expected_right_weight,
        PercentileType::FromStringStrict(item.expected_right_weight_str));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(PercentileType expected_left_weight,
                         PercentileType(1).Subtract(expected_right_weight));
    TestComputePercentileIndex<PercentileType>(
        {percentile, item.max_index, item.expected_index, expected_left_weight,
         expected_right_weight});
  }
}

static constexpr NumericPercentileIndexTestItem
    kNumericPercentileIndexCommonTestItems[] = {
        // {percentile, max_index, expected_index, expected_right_weight}
        {"1", 0, 0, "0"},
        {"1", 1, 1, "0"},
        {"1", 99, 99, "0"},
        {"1", 100, 100, "0"},
        {"1", kTwoTo63 - 1, kTwoTo63 - 1, "0"},
        {"1", kTwoTo63, kTwoTo63, "0"},
        {"1", kUint64Max - 1, kUint64Max - 1, "0"},
        {"1", kUint64Max, kUint64Max, "0"},

        {"1e-9", 0, 0, "0"},
        {"1e-9", 1, 0, "1e-9"},
        {"1e-9", 99, 0, "9.9e-8"},
        {"1e-9", 100, 0, "1e-7"},
        {"1e-9", 9223372036854775807ULL, 9223372036, "0.854775807"},
        {"1e-9", 9223372036854775808ULL, 9223372036, "0.854775808"},
        {"1e-9", 18446744073709551614ULL, 18446744073, "0.709551614"},
        {"1e-9", 18446744073709551615ULL, 18446744073, "0.709551615"},

        {"0.5", 0, 0, "0"},
        {"0.5", 1, 0, "0.5"},
        {"0.5", 99, 49, "0.5"},
        {"0.5", 100, 50, "0"},
        {"0.5", kTwoTo63 - 1, kTwoTo63 / 2 - 1, "0.5"},
        {"0.5", kTwoTo63, kTwoTo63 / 2, "0"},
        {"0.5", kUint64Max - 1, kTwoTo63 - 1, "0"},
        {"0.5", kUint64Max, kTwoTo63 - 1, "0.5"},

        {"0.25", 0, 0, "0"},
        {"0.25", 1, 0, "0.25"},
        {"0.25", 99, 24, "0.75"},
        {"0.25", 100, 25, "0"},
        {"0.25", kTwoTo63 - 1, kTwoTo63 / 4 - 1, "0.75"},
        {"0.25", kTwoTo63, kTwoTo63 / 4, "0"},
        {"0.25", kUint64Max - 1, kTwoTo62 - 1, "0.5"},
        {"0.25", kUint64Max, kTwoTo62 - 1, "0.75"},
};

TEST(NumericPercentileTest, ComputePercentileIndex) {
  TestComputeNumericPercentileIndex<NumericValue>(
      kNumericPercentileIndexCommonTestItems);
}

TEST(BigNumericPercentileTest, ComputePercentileIndex) {
  TestComputeNumericPercentileIndex<BigNumericValue>(
      kNumericPercentileIndexCommonTestItems);

  // Not repeating the values covered in kNumericPercentileIndexCommonTestItems.
  static constexpr NumericPercentileIndexTestItem kBigNumericTestItems[] = {
      // {percentile, max_index, expected_index, expected_right_weight}
      {"1e-38", 0, 0, "0"},
      {"1e-38", 1, 0, "1e-38"},
      {"1e-38", 99, 0, "9.9e-37"},
      {"1e-38", 100, 0, "1e-36"},
      {"1e-38", 9223372036854775807ULL, 0, "9.223372036854775807e-20"},
      {"1e-38", 9223372036854775808ULL, 0, "9.223372036854775808e-20"},
      {"1e-38", 18446744073709551614ULL, 0, "1.8446744073709551614e-19"},
      {"1e-38", 18446744073709551615ULL, 0, "1.8446744073709551615e-19"},
  };
  TestComputeNumericPercentileIndex<BigNumericValue>(kBigNumericTestItems);
}

template <typename PercentileType>
void TestInvalidPercentile() {
  static constexpr PercentileType kInvalidPercentiles[] = {
      PercentileType::MinValue(),
      PercentileType::FromScaledValue(-PercentileType::kScalingFactor),  // -1
      PercentileType::FromScaledValue(-1),  // -1 / kScalingFactor
      // 1 + 1 / kScalingFactor
      PercentileType::FromScaledValue(PercentileType::kScalingFactor + 1),
      PercentileType::FromScaledValue(PercentileType::kScalingFactor * 2),  // 2
      PercentileType::MaxValue(),
  };
  for (PercentileType percentile : kInvalidPercentiles) {
    EXPECT_THAT(PercentileEvaluator<PercentileType>::Create(percentile),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST(NumericPercentileTest, InvalidPercentiles) {
  TestInvalidPercentile<NumericValue>();
}

TEST(BigNumericPercentileTest, InvalidPercentiles) {
  TestInvalidPercentile<BigNumericValue>();
}

struct NumericLinearInterpolationTestItem {
  absl::string_view left_value;
  absl::string_view right_value;
  // left_weight is always precisely 1 - expected_right_weight
  absl::string_view right_weight;
  absl::string_view expected_result;
};

static constexpr NumericLinearInterpolationTestItem
    kNumericLinearInterpolationCommonTestItems[] = {
        {"0", "0", "0", "0"},
        {"0", "0", "1e-9", "0"},
        {"0", "0", "0.499999999", "0"},
        {"0", "0", "0.5", "0"},
        {"0", "0", "0.500000001", "0"},
        {"0", "0", "0.999999999", "0"},
        {"0", "0", "1", "0"},

        {"0", "1", "0", "0"},
        {"0", "1", "1e-9", "1e-9"},
        {"0", "1", "0.499999999", "0.499999999"},
        {"0", "1", "0.5", "0.5"},
        {"0", "1", "0.500000001", "0.500000001"},
        {"0", "1", "0.999999999", "0.999999999"},
        {"0", "1", "1", "1"},

        {"0", "1e28", "0", "0"},
        {"0", "1e28", "1e-9", "1e19"},
        {"0", "1e28", "0.499999999", "4.99999999e27"},
        {"0", "1e28", "0.5", "5e27"},
        {"0", "1e28", "0.500000001", "5.00000001e27"},
        {"0", "1e28", "0.999999999", "9.99999999e27"},
        {"0", "1e28", "1", "1e28"},

        {"1", "1", "0", "1"},
        {"1", "1", "1e-9", "1"},
        {"1", "1", "0.499999999", "1"},
        {"1", "1", "0.5", "1"},
        {"1", "1", "0.500000001", "1"},
        {"1", "1", "0.999999999", "1"},
        {"1", "1", "1", "1"},

        {"1", "1e28", "0", "1"},
        {"1", "1e28", "1e-9", "10000000000000000000.999999999"},
        {"1", "1e28", "0.499999999", "4999999990000000000000000000.500000001"},
        {"1", "1e28", "0.5", "5000000000000000000000000000.5"},
        {"1", "1e28", "0.500000001", "5000000010000000000000000000.499999999"},
        {"1", "1e28", "0.999999999", "9999999990000000000000000000.000000001"},
        {"1", "1e28", "1", "1e28"},

        {"1e28", "1e28", "0", "1e28"},
        {"1e28", "1e28", "1e-9", "1e28"},
        {"1e28", "1e28", "0.499999999", "1e28"},
        {"1e28", "1e28", "0.5", "1e28"},
        {"1e28", "1e28", "0.500000001", "1e28"},
        {"1e28", "1e28", "0.999999999", "1e28"},
        {"1e28", "1e28", "1", "1e28"},

        // Opposite signs

        {"-1", "1", "0", "-1"},
        {"-1", "1", "1e-9", "-0.999999998"},
        {"-1", "1", "0.499999999", "-2e-9"},
        {"-1", "1", "0.5", "0"},
        {"-1", "1", "0.500000001", "2e-9"},
        {"-1", "1", "0.999999999", "0.999999998"},
        {"-1", "1", "1", "1"},

        {"-1", "1e28", "0", "-1"},
        {"-1", "1e28", "1e-9", "9999999999999999999.000000001"},
        {"-1", "1e28", "0.499999999", "4999999989999999999999999999.499999999"},
        {"-1", "1e28", "0.5", "4999999999999999999999999999.5"},
        {"-1", "1e28", "0.500000001", "5000000009999999999999999999.500000001"},
        {"-1", "1e28", "0.999999999", "9999999989999999999999999999.999999999"},
        {"-1", "1e28", "1", "1e28"},

        {"-1e28", "1e28", "0", "-1e28"},
        {"-1e28", "1e28", "1e-9", "-9.99999998e27"},
        {"-1e28", "1e28", "0.499999999", "-2e19"},
        {"-1e28", "1e28", "0.5", "0"},
        {"-1e28", "1e28", "0.500000001", "2e19"},
        {"-1e28", "1e28", "0.999999999", "9.99999998e27"},
        {"-1e28", "1e28", "1", "1e28"},
};

template <typename NumericType>
void TestNumericLinearInterpolation(
    absl::Span<const NumericLinearInterpolationTestItem> items) {
  for (const NumericLinearInterpolationTestItem& item : items) {
    SCOPED_TRACE(absl::StrCat("left_value=", item.left_value,
                              " right_value=", item.right_value,
                              " right_weight=", item.right_weight));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType left_value,
                         NumericType::FromStringStrict(item.left_value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType right_value,
                         NumericType::FromStringStrict(item.right_value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType right_weight,
                         NumericType::FromStringStrict(item.right_weight));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType left_weight,
                         NumericType(1).Subtract(right_weight));
    NumericType result =
        PercentileEvaluator<NumericType>::ComputeLinearInterpolation(
            left_value, left_weight, right_value, right_weight);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType expected_result,
                         NumericType::FromStringStrict(item.expected_result));
    EXPECT_EQ(expected_result, result);

    // Swap left and right with weights. Result should not change.
    result = PercentileEvaluator<NumericType>::ComputeLinearInterpolation(
        right_value, right_weight, left_value, left_weight);
    EXPECT_EQ(expected_result, result);

    // Negate values. Result should be negated.
    // Note: NumericValue::Negate() returns NumericValue, while
    // BigNumericValue::Negate() returns StatusOr<BigNumericValue>.
    auto negated_left_value = left_value.Negate();
    auto negated_right_value = right_value.Negate();
    if constexpr (std::is_same_v<decltype(negated_left_value), NumericType>) {
      result = PercentileEvaluator<NumericType>::ComputeLinearInterpolation(
          negated_left_value, left_weight, negated_right_value, right_weight);
      EXPECT_EQ(expected_result.Negate(), result);
    } else if (negated_left_value.ok() && negated_right_value.ok()) {
      result = PercentileEvaluator<NumericType>::ComputeLinearInterpolation(
          negated_left_value.value(), left_weight, negated_right_value.value(),
          right_weight);
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(expected_result, expected_result.Negate());
      EXPECT_EQ(expected_result, result);
    }
  }
}

TEST(NumericPercentileTest, ComputeLinearInterpolation) {
  TestNumericLinearInterpolation<NumericValue>(
      kNumericLinearInterpolationCommonTestItems);

  static constexpr NumericLinearInterpolationTestItem kMoreTestItems[] = {
      {"0", "1e-9", "0", "0"},
      {"0", "1e-9", "1e-9", "0"},
      {"0", "1e-9", "0.499999999", "0"},
      {"0", "1e-9", "0.5", "1e-9"},
      {"0", "1e-9", "0.500000001", "1e-9"},
      {"0", "1e-9", "0.999999999", "1e-9"},
      {"0", "1e-9", "1", "1e-9"},

      {"1e-9", "1e-9", "0", "1e-9"},
      {"1e-9", "1e-9", "1e-9", "1e-9"},
      {"1e-9", "1e-9", "0.499999999", "1e-9"},
      {"1e-9", "1e-9", "0.5", "1e-9"},
      {"1e-9", "1e-9", "0.999999999", "1e-9"},
      {"1e-9", "1e-9", "1", "1e-9"},

      {"1e-9", "1", "0", "1e-9"},
      {"1e-9", "1", "1e-9", "2e-9"},
      {"1e-9", "1", "0.499999999", "0.5"},
      {"1e-9", "1", "0.5", "0.500000001"},
      {"1e-9", "1", "0.500000001", "0.500000001"},
      {"1e-9", "1", "0.999999999", "0.999999999"},
      {"1e-9", "1", "1", "1"},

      {"1e-9", "1e28", "0", "1e-9"},
      {"1e-9", "1e28", "1e-9", "10000000000000000000.000000001"},
      {"1e-9", "1e28", "0.499999999", "4999999990000000000000000000.000000001"},
      {"1e-9", "1e28", "0.5", "5000000000000000000000000000.000000001"},
      {"1e-9", "1e28", "0.500000001", "5.00000001e27"},
      {"1e-9", "1e28", "0.999999999", "9.99999999e27"},
      {"1e-9", "1e28", "1", "1e28"},

      {"0", kNumericMax, "0", "0"},
      {"0", kNumericMax, "1e-9", "1e20"},
      {"0", kNumericMax, "0.499999999", "4.99999999e28"},
      {"0", kNumericMax, "0.5", "5e28"},
      {"0", kNumericMax, "0.500000001",
       "50000000099999999999999999999.999999999"},
      {"0", kNumericMax, "0.999999999",
       "99999999899999999999999999999.999999999"},
      {"0", kNumericMax, "1", kNumericMax},

      {"1e-9", kNumericMax, "0", "1e-9"},
      {"1e-9", kNumericMax, "1e-9", "100000000000000000000.000000001"},
      {"1e-9", kNumericMax, "0.499999999", "4.99999999e28"},
      {"1e-9", kNumericMax, "0.5", "5e28"},
      {"1e-9", kNumericMax, "0.500000001", "5.00000001e28"},
      {"1e-9", kNumericMax, "0.999999999",
       "99999999899999999999999999999.999999999"},
      {"1e-9", kNumericMax, "1", kNumericMax},

      {"1", kNumericMax, "0", "1"},
      {"1", kNumericMax, "1e-9", "100000000000000000000.999999999"},
      {"1", kNumericMax, "0.499999999",
       "49999999900000000000000000000.500000001"},
      {"1", kNumericMax, "0.5", "50000000000000000000000000000.5"},
      {"1", kNumericMax, "0.500000001",
       "50000000100000000000000000000.499999998"},
      {"1", kNumericMax, "0.999999999", "9.99999999e28"},
      {"1", kNumericMax, "1", kNumericMax},
      {"1e28", kNumericMax, "0", "1e28"},
      {"1e28", kNumericMax, "1e-9", "1.000000009e28"},
      {"1e28", kNumericMax, "0.499999999", "5.499999991e28"},
      {"1e28", kNumericMax, "0.5", "5.5e28"},
      {"1e28", kNumericMax, "0.500000001",
       "55000000089999999999999999999.999999999"},
      {"1e28", kNumericMax, "0.999999999",
       "99999999909999999999999999999.999999999"},
      {"1e28", kNumericMax, "1", kNumericMax},

      {kNumericMax, kNumericMax, "0", kNumericMax},
      {kNumericMax, kNumericMax, "1e-9", kNumericMax},
      {kNumericMax, kNumericMax, "0.499999999", kNumericMax},
      {kNumericMax, kNumericMax, "0.5", kNumericMax},
      {kNumericMax, kNumericMax, "0.500000001", kNumericMax},
      {kNumericMax, kNumericMax, "0.999999999", kNumericMax},
      {kNumericMax, kNumericMax, "1", kNumericMax},

      // Opposite signs

      {"-1e-9", "1e-9", "0", "-1e-9"},
      {"-1e-9", "1e-9", "1e-9", "-1e-9"},
      {"-1e-9", "1e-9", "0.499999999", "0"},
      {"-1e-9", "1e-9", "0.5", "0"},
      {"-1e-9", "1e-9", "0.999999999", "1e-9"},
      {"-1e-9", "1e-9", "1", "1e-9"},

      {"-1e-9", "1", "0", "-1e-9"},
      {"-1e-9", "1", "1e-9", "0"},
      {"-1e-9", "1", "0.499999999", "0.499999998"},
      {"-1e-9", "1", "0.5", "0.5"},
      {"-1e-9", "1", "0.500000001", "0.500000001"},
      {"-1e-9", "1", "0.999999999", "0.999999999"},
      {"-1e-9", "1", "1", "1"},

      {"-1e-9", "1e28", "0", "-1e-9"},
      {"-1e-9", "1e28", "1e-9", "9999999999999999999.999999999"},
      {"-1e-9", "1e28", "0.499999999",
       "4999999989999999999999999999.999999999"},
      {"-1e-9", "1e28", "0.5", "5e27"},
      {"-1e-9", "1e28", "0.500000001", "5.00000001e27"},
      {"-1e-9", "1e28", "0.999999999", "9.99999999e27"},
      {"-1e-9", "1e28", "1", "1e28"},

      {"-1e-9", kNumericMax, "0", "-1e-9"},
      {"-1e-9", kNumericMax, "1e-9", "99999999999999999999.999999999"},
      {"-1e-9", kNumericMax, "0.499999999",
       "49999999899999999999999999999.999999999"},
      {"-1e-9", kNumericMax, "0.5", "49999999999999999999999999999.999999999"},
      {"-1e-9", kNumericMax, "0.500000001",
       "50000000099999999999999999999.999999999"},
      {"-1e-9", kNumericMax, "0.999999999",
       "99999999899999999999999999999.999999999"},
      {"-1e-9", kNumericMax, "1", kNumericMax},

      {"-1", kNumericMax, "0", "-1"},
      {"-1", kNumericMax, "1e-9", "99999999999999999999.000000001"},
      {"-1", kNumericMax, "0.499999999",
       "49999999899999999999999999999.499999999"},
      {"-1", kNumericMax, "0.5", "49999999999999999999999999999.5"},
      {"-1", kNumericMax, "0.500000001", "50000000099999999999999999999.5"},
      {"-1", kNumericMax, "0.999999999",
       "99999999899999999999999999999.999999998"},
      {"-1", kNumericMax, "1", kNumericMax},

      {"-1e28", kNumericMax, "0", "-1e28"},
      {"-1e28", kNumericMax, "1e-9", "-9.99999989e27"},
      {"-1e28", kNumericMax, "0.499999999", "4.499999989e28"},
      {"-1e28", kNumericMax, "0.5", "4.5e28"},
      {"-1e28", kNumericMax, "0.500000001",
       "45000000109999999999999999999.999999999"},
      {"-1e28", kNumericMax, "0.999999999",
       "99999999889999999999999999999.999999999"},
      {"-1e28", kNumericMax, "1", kNumericMax},

      {kNumericMin, kNumericMax, "0", kNumericMin},
      {kNumericMin, kNumericMax, "1e-9",
       "-99999999799999999999999999999.999999999"},
      {kNumericMin, kNumericMax, "0.499999999", "-2e20"},
      {kNumericMin, kNumericMax, "0.5", "0"},
      {kNumericMin, kNumericMax, "0.500000001", "2e20"},
      {kNumericMin, kNumericMax, "0.999999999",
       "99999999799999999999999999999.999999999"},
      {kNumericMin, kNumericMax, "1", kNumericMax},
  };
  TestNumericLinearInterpolation<NumericValue>(kMoreTestItems);
}

TEST(BigNumericPercentileTest, ComputeLinearInterpolation) {
  TestNumericLinearInterpolation<BigNumericValue>(
      kNumericLinearInterpolationCommonTestItems);

  static constexpr NumericLinearInterpolationTestItem kMoreTestItems[] = {
      {"0", "1e-38", "0", "0"},
      {"0", "1e-38", "1e-38", "0"},
      {"0", "1e-38", "0.49999999999999999999999999999999999999", "0"},
      {"0", "1e-38", "0.5", "1e-38"},
      {"0", "1e-38", "0.50000000000000000000000000000000000001", "1e-38"},
      {"0", "1e-38", "0.99999999999999999999999999999999999999", "1e-38"},
      {"0", "1e-38", "1", "1e-38"},

      {"1e-38", "1e-38", "0", "1e-38"},
      {"1e-38", "1e-38", "1e-38", "1e-38"},
      {"1e-38", "1e-38", "0.49999999999999999999999999999999999999", "1e-38"},
      {"1e-38", "1e-38", "0.5", "1e-38"},
      {"1e-38", "1e-38", "0.99999999999999999999999999999999999999", "1e-38"},
      {"1e-38", "1e-38", "1", "1e-38"},

      {"1e-38", "1", "0", "1e-38"},
      {"1e-38", "1", "1e-38", "2e-38"},
      {"1e-38", "1", "0.49999999999999999999999999999999999999", "0.5"},
      {"1e-38", "1", "0.5", "0.50000000000000000000000000000000000001"},
      {"1e-38", "1", "0.50000000000000000000000000000000000001",
       "0.50000000000000000000000000000000000001"},
      {"1e-38", "1", "0.99999999999999999999999999999999999999",
       "0.99999999999999999999999999999999999999"},
      {"1e-38", "1", "1", "1"},

      {"1e-38", "1e38", "0", "1e-38"},
      {"1e-38", "1e38", "1e-38", "1.00000000000000000000000000000000000001"},
      {"1e-38", "1e38", "0.49999999999999999999999999999999999999",
       "49999999999999999999999999999999999999."
       "00000000000000000000000000000000000001"},
      {"1e-38", "1e38", "0.5",
       "50000000000000000000000000000000000000."
       "00000000000000000000000000000000000001"},
      {"1e-38", "1e38", "0.50000000000000000000000000000000000001",
       "50000000000000000000000000000000000001"},
      {"1e-38", "1e38", "0.99999999999999999999999999999999999999",
       "99999999999999999999999999999999999999"},
      {"1e-38", "1e38", "1", "1e38"},

      {"0", kBigNumericMax, "0", "0"},
      {"0", kBigNumericMax, "1", kBigNumericMax},
      {"1e-38", kBigNumericMax, "0", "1e-38"},
      {"1e-38", kBigNumericMax, "1", kBigNumericMax},
      {"1", kBigNumericMax, "0", "1"},
      {"1", kBigNumericMax, "1", kBigNumericMax},
      {"1e38", kBigNumericMax, "0", "1e38"},
      {"1e38", kBigNumericMax, "1", kBigNumericMax},

      {"0", kBigNumericMin, "0", "0"},
      {"0", kBigNumericMin, "1", kBigNumericMin},
      {"-1e-38", kBigNumericMin, "0", "-1e-38"},
      {"-1e-38", kBigNumericMin, "1", kBigNumericMin},
      {"-1", kBigNumericMin, "0", "-1"},
      {"-1", kBigNumericMin, "1", kBigNumericMin},
      {"-1e38", kBigNumericMin, "0", "-1e38"},
      {"-1e38", kBigNumericMin, "1", kBigNumericMin},

      {kBigNumericMax, kBigNumericMax, "0", kBigNumericMax},
      {kBigNumericMax, kBigNumericMax, "1e-38", kBigNumericMax},
      {kBigNumericMax, kBigNumericMax,
       "0.49999999999999999999999999999999999999", kBigNumericMax},
      {kBigNumericMax, kBigNumericMax, "0.5", kBigNumericMax},
      {kBigNumericMax, kBigNumericMax,
       "0.50000000000000000000000000000000000001", kBigNumericMax},
      {kBigNumericMax, kBigNumericMax,
       "0.99999999999999999999999999999999999999", kBigNumericMax},
      {kBigNumericMax, kBigNumericMax, "1", kBigNumericMax},

      {kBigNumericMin, kBigNumericMin, "0", kBigNumericMin},
      {kBigNumericMin, kBigNumericMin, "1e-38", kBigNumericMin},
      {kBigNumericMin, kBigNumericMin,
       "0.49999999999999999999999999999999999999", kBigNumericMin},
      {kBigNumericMin, kBigNumericMin, "0.5", kBigNumericMin},
      {kBigNumericMin, kBigNumericMin,
       "0.50000000000000000000000000000000000001", kBigNumericMin},
      {kBigNumericMin, kBigNumericMin,
       "0.99999999999999999999999999999999999999", kBigNumericMin},
      {kBigNumericMin, kBigNumericMin, "1", kBigNumericMin},

      // Opposite signs

      {"-1e-38", "1e-38", "0", "-1e-38"},
      {"-1e-38", "1e-38", "1e-38", "-1e-38"},
      {"-1e-38", "1e-38", "0.49999999999999999999999999999999999999", "0"},
      {"-1e-38", "1e-38", "0.5", "0"},
      {"-1e-38", "1e-38", "0.99999999999999999999999999999999999999", "1e-38"},
      {"-1e-38", "1e-38", "1", "1e-38"},

      {"-1e-38", "1", "0", "-1e-38"},
      {"-1e-38", "1", "1e-38", "0"},
      {"-1e-38", "1", "0.49999999999999999999999999999999999999",
       "0.49999999999999999999999999999999999998"},
      {"-1e-38", "1", "0.5", "0.5"},
      {"-1e-38", "1", "0.50000000000000000000000000000000000001",
       "0.50000000000000000000000000000000000001"},
      {"-1e-38", "1", "0.99999999999999999999999999999999999999",
       "0.99999999999999999999999999999999999999"},
      {"-1e-38", "1", "1", "1"},

      {"-1e-38", "1e38", "0", "-1e-38"},
      {"-1e-38", "1e38", "1e-38", "0.99999999999999999999999999999999999999"},
      {"-1e-38", "1e38", "0.49999999999999999999999999999999999999",
       "49999999999999999999999999999999999998."
       "99999999999999999999999999999999999999"},
      {"-1e-38", "1e38", "0.5", "5e37"},
      {"-1e-38", "1e38", "0.50000000000000000000000000000000000001",
       "50000000000000000000000000000000000001"},
      {"-1e-38", "1e38", "0.99999999999999999999999999999999999999",
       "99999999999999999999999999999999999999"},
      {"-1e-38", "1e38", "1", "1e38"},

      {"-1e-38", kBigNumericMax, "0", "-1e-38"},
      {"-1e-38", kBigNumericMax, "1", kBigNumericMax},
      {"-1", kBigNumericMax, "0", "-1"},
      {"-1", kBigNumericMax, "1", kBigNumericMax},
      {"-1e28", kBigNumericMax, "0", "-1e28"},
      {"-1e28", kBigNumericMax, "1", kBigNumericMax},

      {"1e-38", kBigNumericMin, "0", "1e-38"},
      {"1e-38", kBigNumericMin, "1", kBigNumericMin},
      {"1", kBigNumericMin, "0", "1"},
      {"1", kBigNumericMin, "1", kBigNumericMin},
      {"1e38", kBigNumericMin, "0", "1e38"},
      {"1e38", kBigNumericMin, "1", kBigNumericMin},

      {kBigNumericMin, kBigNumericMax, "0", kBigNumericMin},
      {kBigNumericMin, kBigNumericMax, "0.5", "-1e-38"},
      {kBigNumericMin, kBigNumericMax, "1", kBigNumericMax},
  };
  TestNumericLinearInterpolation<BigNumericValue>(kMoreTestItems);
}

template <typename PercentileType>
struct PercentileContTestItem {
  absl::string_view expected_result;
  std::initializer_list<PercentileType> values;
  size_t num_nulls;
  PercentileType percentile;
};

template <typename PercentileType>
void VerifyPercentileCont(absl::string_view expected_result,
                          std::vector<PercentileType> values, size_t num_nulls,
                          PercentileType percentile) {
  SCOPED_TRACE(absl::StrCat("percentile=", ToString(percentile),
                            " num_nulls=", num_nulls));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(PercentileEvaluator<PercentileType> percentile_evalutor,
                       PercentileEvaluator<PercentileType>::Create(percentile));
  {
    PercentileType result = PercentileType(-1234);
    std::string actual_value = "NULL";
    if (percentile_evalutor.template ComputePercentileCont<false>(
            values.begin(), values.end(), num_nulls, &result)) {
      actual_value = ToString(result);
    }
    EXPECT_EQ(expected_result, actual_value);
  }

  auto itr = values.begin();
  if constexpr (std::is_floating_point_v<PercentileType>) {
    struct IsNaN {
      bool operator()(double value) const { return std::isnan(value); }
    };
    itr = std::partition(values.begin(), values.end(), IsNaN());
  }
  std::sort(itr, values.end());
  {
    PercentileType result = PercentileType(-1234);
    std::string actual_value = "NULL";
    if (percentile_evalutor.template ComputePercentileCont<true>(
            values.cbegin(), values.cend(), num_nulls, &result)) {
      actual_value = ToString(result);
    }
    EXPECT_EQ(expected_result, actual_value);
  }
}

TEST(DoublePercentileTest, ComputePercentileCont) {
  static const std::initializer_list<double> inf_values = {kInf, -kInf, -kInf,
                                                           kInf};
  static const std::initializer_list<double> values = {
      1,    101, kDoubleDenormalMin, kNaN, kDoubleMax,
      kInf, 0,   -kDoubleMax,        -kInf};

  // Args: expected_result, values, num_nulls, percentile
  static const PercentileContTestItem<double> kTestItems[] = {
      {"NULL", {}, 0, 0},
      {"NULL", {}, 0, 0.5},
      {"NULL", {}, 0, 1},
      // Empty inputs.
      {"NULL", {}, 0, 0},
      {"NULL", {}, 0, 0.5},
      {"NULL", {}, 0, 1},

      // Only nulls.
      // Percentile at null.
      {"NULL", {}, 1, 0},
      // Percentile between 2 nulls.
      {"NULL", {}, 10, 0.5},
      // Percentile at null.
      {"NULL", {}, kUint64Max, 1},

      // Only nans.
      // Percentile at nan.
      {"nan", {kNaN}, 0, 0},
      // Percentile between 2 nans.
      {"nan", {kNaN, kNaN, kNaN, kNaN}, 0, 0.5},
      // Percentile at nan.
      {"nan", {kNaN, kNaN, kNaN}, 0, 1},

      // One normal input. Percentile at the input.
      {"1", {1}, 0, 0},
      {"-1", {-1}, 0, 0.5},
      {"0", {0}, 0, 1},

      // [null, null, normal]
      // Percentile at null.
      {"NULL", {0}, 2, 0},
      // Percentile between 2 nulls.
      {"NULL", {kInf}, 2, 0.25},
      // Percentile at null.
      {"NULL", {kInf}, 2, 0.5},
      // Percentile between null and 100.
      {"100", {100}, 2, 0.5000001},
      // Percentile at 100.
      {"100", {100}, 2, 1},

      // [null, nan, normal]
      // Percentile at null.
      {"NULL", {kInf, kNaN}, 1, 0},
      // Percentile between null and nan.
      {"nan", {kInf, kNaN}, 1, 0.25},
      // Percentile at nan.
      {"nan", {kInf, kNaN}, 1, 0.5},
      // Percentile between nan and inf.
      {"nan", {kInf, kNaN}, 1, 0.75},
      // Percentile at inf.
      {"inf", {kInf, kNaN}, 1, 1},

      // [null, -inf, -inf, inf, inf]
      // Percentile at null.
      {"NULL", inf_values, 1, 0},
      // Percentile between null and -inf.
      {"-inf", inf_values, 1, 0.1},
      // Percentile at -inf.
      {"-inf", inf_values, 1, 0.25},
      // Percentile between 2 -infs.
      {"-inf", inf_values, 1, 0.4},
      // Percentile between -inf and inf.
      {"nan", inf_values, 1, 0.6},
      // Percentile at inf.
      {"inf", inf_values, 1, 0.75},
      // Percentile between 2 infs.
      {"inf", inf_values, 1, 0.8},

      // 1 nan and 8 normal inputs.
      // Percentile at nan.
      {"nan", values, 0, 0},
      // Percentile between nan and -inf.
      {"nan", values, 0, 0.06},
      // Percentile at -inf.
      {"-inf", values, 0, 0.125},
      // Percentile between -inf and -kDoubleMax.
      {"-inf", values, 0, 0.15},
      // Percentile at -kDoubleMax.
      {"-1.7976931348623157e+308", values, 0, 0.25},
      // Percentile between -kDoubleMax and 0.
      {"-1.0786158809173895e+308", values, 0, 0.3},
      // Percentile at 0.
      {"0", values, 0, 0.375},
      // Percentile between 0 and kDoubleDenormalMin.
      {"0", values, 0, 0.4375},
      // Percentile at kDoubleDenormalMin.
      {"4.94065645841247e-324", values, 0, 0.5},
      // Percentile at 1.
      {"1", values, 0, 0.625},
      // Percentile between 1 and 101.
      {"26", values, 0, 0.65625},
      // Percentile between 1 and 101.
      {"51", values, 0, 0.6875},
      // Percentile at 101.
      {"101", values, 0, 0.75},
      // Percentile at kDoubleMax.
      {"1.7976931348623157e+308", values, 0, 0.875},
      // Percentile between kDoubleMax and inf.
      {"inf", values, 0, 0.875001},
      // Percentile at inf.
      {"inf", values, 0, 1},
  };

  for (const PercentileContTestItem<double>& item : kTestItems) {
    VerifyPercentileCont(item.expected_result, {item.values}, item.num_nulls,
                         item.percentile);
  }
}

struct NumericPercentileContTestItem {
  absl::string_view expected_result;
  std::initializer_list<absl::string_view> values;
  size_t num_nulls;
  absl::string_view percentile;
};

template <typename PercentileType>
void TestNumericPercentileCont(
    absl::Span<const NumericPercentileContTestItem> items) {
  for (const NumericPercentileContTestItem& item : items) {
    std::vector<PercentileType> values;
    for (absl::string_view value_str : item.values) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(PercentileType value,
                           PercentileType::FromStringStrict(value_str));
      values.push_back(value);
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(PercentileType percentile,
                         PercentileType::FromStringStrict(item.percentile));
    VerifyPercentileCont(item.expected_result, values, item.num_nulls,
                         percentile);
  }
}

static const NumericPercentileContTestItem
    kNumericPercentileContCommonTestItems[] = {
        // Empty inputs.
        {"NULL", {}, 0, "0"},
        {"NULL", {}, 0, "0.5"},
        {"NULL", {}, 0, "1"},

        // Only nulls.
        // Percentile at null.
        {"NULL", {}, 1, "0"},
        // Percentile between 2 nulls.
        {"NULL", {}, 10, "0.5"},
        // Percentile at null.
        {"NULL", {}, kUint64Max, "1"},

        // One normal input. Percentile at the input.
        {"1", {"1"}, 0, "0"},
        {"-1", {"-1"}, 0, "0.5"},
        {"0", {"0"}, 0, "1"},

        // [null, null, normal]
        // Percentile at null.
        {"NULL", {"0"}, 2, "0"},
        // Percentile between 2 nulls.
        {"NULL", {"100"}, 2, "0.25"},
        // Percentile at null.
        {"NULL", {"100"}, 2, "0.5"},
        // Percentile between null and 100.
        {"100", {"100"}, 2, "0.5000001"},
        // Percentile at 100.
        {"100", {"100"}, 2, "1"},
};

TEST(NumericPercentileTest, ComputePercentileCont) {
  TestNumericPercentileCont<NumericValue>(
      kNumericPercentileContCommonTestItems);

  static const std::initializer_list<absl::string_view> kMinAndMax = {
      kNumericMin, kNumericMax};

  static const std::initializer_list<absl::string_view> values = {
      "0.999999999",
      "101.999999999",
      "1e-9",
      "-1",
      kNumericMax,
      "1.999999999",
      "0",
      kNumericMin,
      {kNumericMax.data() + 1, kNumericMax.size() - 1}  // removes one digit.
  };

  // Args: expected_value, values, num_nulls, percentile
  static const NumericPercentileContTestItem kTestItems[] = {
      {kNumericMin, kMinAndMax, 0, "0"},
      {"-99999999799999999999999999999.999999999", kMinAndMax, 0, "1e-9"},
      {"-59999999999999999999999999999.999999999", kMinAndMax, 0, "0.2"},
      {"-200000000000000000000", kMinAndMax, 0, "0.499999999"},
      {"0", kMinAndMax, 0, "0.5"},
      {"200000000000000000000", kMinAndMax, 0, "0.500000001"},
      {"40000000000000000000000000000", kMinAndMax, 0, "0.7"},
      {"99999999799999999999999999999.999999999", kMinAndMax, 0, "0.999999999"},
      {kNumericMax, kMinAndMax, 0, "1"},

      // Percentile at kNumericMin.
      {kNumericMin, values, 0, "0"},
      // Percentile between kNumericMin and -1.
      {"-99999999200000000000000000000.000000007", values, 0, "1e-9"},
      // Percentile at -1.
      {"-1", values, 0, "0.125"},
      // Percentile between -1 and 0.
      {"-0.000000008", values, 0, "0.249999999"},
      // Percentile at 0.
      {"0", values, 0, "0.25"},
      // Percentile between 0 and 1e-9.
      {"0", values, 0, "0.3"},
      // Percentile at 1e-9.
      {"0.000000001", values, 0, "0.375"},
      // Percentile between 1e-9 and 0.999999999.
      {"0.5", values, 0, "0.4375"},
      // Percentile at 0.999999999.
      {"0.999999999", values, 0, "0.5"},
      // Percentile between 0.999999999 and 1.999999999
      {"1.799999999", values, 0, "0.6"},
      // Percentile at 1.999999999.
      {"1.999999999", values, 0, "0.625"},
      // Percentile between 1.999999999 and 101.999999999
      {"61.999999999", values, 0, "0.7"},
      // Percentile at 101.999999999.
      {"101.999999999", values, 0, "0.75"},
      // Percentile between 101.999999999 and
      // 9999999999999999999999999999.999999999
      {"4000000000000000000000000061.199999999", values, 0, "0.8"},
      // Percentile at 9999999999999999999999999999.999999999
      {"9999999999999999999999999999.999999999", values, 0, "0.875"},
      // Percentile between 9999999999999999999999999999.999999999 and
      // kNumericMax.
      {"27999999999999999999999999999.999999999", values, 0, "0.9"},
      // Percentile at kNumericMax.
      {kNumericMax, values, 0, "1"},
  };

  TestNumericPercentileCont<NumericValue>(kTestItems);
}

TEST(BigNumericPercentileTest, ComputePercentileCont) {
  TestNumericPercentileCont<BigNumericValue>(
      kNumericPercentileContCommonTestItems);

  static const std::initializer_list<absl::string_view> kMinAndMax = {
      kBigNumericMin, kBigNumericMax};

  constexpr absl::string_view kNegativeMax =
      "-578960446186580977117854925043439539266"
      ".34992332820282019728792003956564819967";

  static const std::initializer_list<absl::string_view> values = {
      "1.00000000000000000000000000000000000001",
      "101.00000000000000000000000000000000000001",
      "1e-38",
      "-1",
      kBigNumericMax,
      "1.00000000000000000000000000000000000001",
      "0",
      kBigNumericMin,
      kNegativeMax,
  };

  // Args: expected_value, values, num_nulls, percentile
  static const NumericPercentileContTestItem kTestItems[] = {
      {kBigNumericMin, kMinAndMax, 0, "0"},
      {"-578960446186580977117854925043439539254"
       ".77071440447120065493082153869685741435",
       kMinAndMax, 0, "1e-38"},
      {"-11.57920892373161954235709850086879078533", kMinAndMax, 0,
       "0.49999999999999999999999999999999999999"},
      {"-0.00000000000000000000000000000000000001", kMinAndMax, 0, "0.5"},
      {"11.57920892373161954235709850086879078532", kMinAndMax, 0,
       "0.50000000000000000000000000000000000001"},
      {"578960446186580977117854925043439539254"
       ".77071440447120065493082153869685741434",
       kMinAndMax, 0, "0.99999999999999999999999999999999999999"},
      {kBigNumericMax, kMinAndMax, 0, "1"},

      // Percentile at kNumericMin.
      {kBigNumericMin, values, 0, "0"},
      // Percentile between kNumericMin and kNegativeMax.
      {kBigNumericMin, values, 0, "1e-38"},
      // Percentile at kNegativeMax.
      {kNegativeMax, values, 0, "0.125"},
      {"-1", values, 0, "0.25"},
      // Percentile between -1 and 0.
      {"-0.99999999999999999999999999999999999992", values, 0,
       "0.25000000000000000000000000000000000001"},
      // Percentile between -1 and 0.
      {"-0.6", values, 0, "0.3"},
      // Percentile between -1 and 0.
      {"-0.00000000000000000000000000000000000008", values, 0,
       "0.37499999999999999999999999999999999999"},
      // Percentile at 0.
      {"0", values, 0, "0.375"},
      // Percentile between 0 and 1e-38.
      {"0.00000000000000000000000000000000000001", values, 0, "0.4375"},
      // Percentile at 1e-38.
      {"0.00000000000000000000000000000000000001", values, 0, "0.5"},
      // Percentile between 1e-38 and 1 + 1e-38
      {"0.80000000000000000000000000000000000001", values, 0, "0.6"},
      // Percentile at 1.00000000000000000000000000000000000001.
      {"1.00000000000000000000000000000000000001", values, 0, "0.625"},
      // Percentile between 1 + 1e-38 and 1 + 1e-38
      {"1.00000000000000000000000000000000000001", values, 0, "0.7"},
      // Percentile between 1 + 1e-38 and 101 + 1e-38
      {"41.00000000000000000000000000000000000001", values, 0, "0.8"},
      // Percentile between 101 + 1e-38 and
      {"101.00000000000000000000000000000000000001", values, 0, "0.875"},
      // Percentile between 101 + 1e-38 and
      // kNumericMax.
      {"115792089237316195423570985008687907934"
       ".06998466564056403945758400791312963994",
       values, 0, "0.9"},
      // Percentile at kNumericMax.
      {kBigNumericMax, values, 0, "1"},
  };

  TestNumericPercentileCont<BigNumericValue>(kTestItems);
}

constexpr size_t kNumValues = 8;

static const int kShuffledIndexes[kNumValues] = {3, 4, 6, 2, 1, 7, 0, 5};

template <typename T>
struct PercentileDiscTest : public ::testing::Test {
 protected:
  template <typename PercentileType>
  void VerifyPercentileDisc(size_t expected_value_index, const T* sorted_values,
                            size_t num_values, const int* shuffled_indexes,
                            size_t num_nulls, double percentile);
  void VerifyNonNulls(size_t expected_value_index, size_t num_nulls,
                          double percentile) {
    VerifyPercentileDisc<double>(expected_value_index, kSortedValues,
                                 kNumValues, kShuffledIndexes, num_nulls,
                                 percentile);
    VerifyPercentileDisc<NumericValue>(expected_value_index, kSortedValues,
                                       kNumValues, kShuffledIndexes, num_nulls,
                                       percentile);
    VerifyPercentileDisc<BigNumericValue>(expected_value_index, kSortedValues,
                                          kNumValues, kShuffledIndexes,
                                          num_nulls, percentile);
  }
  void VerifyAllNulls(size_t num_nulls, double percentile) {
    VerifyPercentileDisc<double>(0, nullptr, 0, nullptr, num_nulls, percentile);
    VerifyPercentileDisc<NumericValue>(0, nullptr, 0, nullptr, num_nulls,
                                       percentile);
    VerifyPercentileDisc<BigNumericValue>(0, nullptr, 0, nullptr, num_nulls,
                                          percentile);
  }

 private:
  static const T kSortedValues[kNumValues];
};

using PercentileDiscTypes =
    ::testing::Types<int64_t, int32_t, uint64_t, uint32_t, double, float, bool,
                     absl::string_view>;
TYPED_TEST_SUITE(PercentileDiscTest, PercentileDiscTypes);

template <>
const int64_t PercentileDiscTest<int64_t>::kSortedValues[] = {
    limits<int64_t>::min(),     limits<int64_t>::min() + 1, -10, 0, 1, 10,
    limits<int64_t>::max() - 1, limits<int64_t>::max()};

template <>
const int32_t PercentileDiscTest<int32_t>::kSortedValues[] = {
    limits<int32_t>::min(),     limits<int32_t>::min() + 1, -10, 0, 1, 10,
    limits<int32_t>::max() - 1, limits<int32_t>::max()};

template <>
const uint64_t PercentileDiscTest<uint64_t>::kSortedValues[] = {
    0,
    1,
    2,
    100,
    128,
    1000,
    limits<uint64_t>::max() - 1,
    limits<uint64_t>::max()};

template <>
const uint32_t PercentileDiscTest<uint32_t>::kSortedValues[] = {
    0,
    1,
    2,
    100,
    128,
    1000,
    limits<uint32_t>::max() - 1,
    limits<uint32_t>::max()};

template <> const double PercentileDiscTest<double>::kSortedValues[] = {
    kNaN, -kInf, -kDoubleMax, 0, kDoubleDenormalMin, kDoubleMin, kDoubleMax,
    kInf
};

template <> const float PercentileDiscTest<float>::kSortedValues[] = {
    limits<float>::quiet_NaN(), -limits<float>::infinity(),
    limits<float>::lowest(), 0, limits<float>::min(), limits<float>::epsilon(),
    limits<float>::max(), limits<float>::infinity()
};

template <> const bool PercentileDiscTest<bool>::kSortedValues[] = {
    false, true, true, true, true, true, true, true
};

template <> const absl::string_view
PercentileDiscTest<absl::string_view>::kSortedValues[] = {
    "", absl::string_view("\0", 1), "!@#$%", "()", "A",
    "Abcdefghijklmnopqrstuvwxyz", "a", "\xFF"
};

template <typename T>
template <typename PercentileType>
void PercentileDiscTest<T>::VerifyPercentileDisc(
    size_t expected_value_index, const T* sorted_values, size_t num_values,
    const int* shuffled_indexes, size_t num_nulls, double percentile) {
  SCOPED_TRACE(absl::StrCat("percentile=", RoundTripDoubleToString(percentile),
                            " num_nulls=", num_nulls));
  PercentileType p;
  if constexpr (std::is_same_v<PercentileType, double>) {
    p = percentile;
  } else {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(p, PercentileType::FromDouble(percentile));
  }
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(PercentileEvaluator<PercentileType> percentile_evalutor,
                       PercentileEvaluator<PercentileType>::Create(p));
  {
    const T* result =
        percentile_evalutor.template ComputePercentileDisc<T, true>(
            sorted_values, sorted_values + num_values, num_nulls);
    EXPECT_EQ(expected_value_index, result - sorted_values);
  }

  {
    // Use deque instead of vector, to avoid the specialization vector<bool>,
    // whose iterator doesn't dereference to bool.
    std::deque<T> unsorted_values(num_values);
    for (int i = 0; i < num_values; ++i) {
      unsorted_values[shuffled_indexes[i]] = sorted_values[i];
    }
    auto result_itr =
        percentile_evalutor.template ComputePercentileDisc<T, false>(
            unsorted_values.begin(), unsorted_values.end(), num_nulls);
    if (expected_value_index != num_values) {
      EXPECT_EQ(testing::PrintToString(sorted_values[expected_value_index]),
                testing::PrintToString(*result_itr));
    }
    EXPECT_EQ(expected_value_index, result_itr - unsorted_values.begin());
  }
}

TYPED_TEST(PercentileDiscTest, AllNulls) {
  this->VerifyAllNulls(0, 0);
  this->VerifyAllNulls(0, 0.5);
  this->VerifyAllNulls(0, 1);

  this->VerifyAllNulls(1, 0);
  this->VerifyAllNulls(1, 0.5);
  this->VerifyAllNulls(1, 1);

  this->VerifyAllNulls(kUint64Max, 0);
  this->VerifyAllNulls(kUint64Max, 0.5);
  this->VerifyAllNulls(kUint64Max, 1);
}

TYPED_TEST(PercentileDiscTest, NonEmpty) {
  // 8 non-nulls
  this->VerifyNonNulls(0, 0, 0);
  this->VerifyNonNulls(0, 0, 1e-9);
  this->VerifyNonNulls(0, 0, 0.125);
  this->VerifyNonNulls(1, 0, 0.125000001);
  this->VerifyNonNulls(1, 0, 0.25);
  this->VerifyNonNulls(2, 0, 0.250000001);
  this->VerifyNonNulls(2, 0, 0.375);
  this->VerifyNonNulls(3, 0, 0.375000001);
  this->VerifyNonNulls(3, 0, 0.5);
  this->VerifyNonNulls(4, 0, 0.500000001);
  this->VerifyNonNulls(4, 0, 0.625);
  this->VerifyNonNulls(5, 0, 0.625000001);
  this->VerifyNonNulls(5, 0, 0.75);
  this->VerifyNonNulls(6, 0, 0.750000001);
  this->VerifyNonNulls(6, 0, 0.875);
  this->VerifyNonNulls(7, 0, 0.875000001);
  this->VerifyNonNulls(7, 0, 1);

  // 24 nulls and 8 non-nulls
  this->VerifyNonNulls(8, 24, 0);
  this->VerifyNonNulls(8, 24, 0.1);
  this->VerifyNonNulls(8, 24, 0.5);
  this->VerifyNonNulls(8, 24, 0.75);
  this->VerifyNonNulls(0, 24, 0.750000001);
  this->VerifyNonNulls(3, 24, 0.875);
  this->VerifyNonNulls(4, 24, 0.875000001);
  this->VerifyNonNulls(7, 24, 1);
}

}  // namespace
}  // namespace googlesql
