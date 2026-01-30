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

#include "googlesql/public/numeric_value_test_utils.h"

#include <cstddef>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_value.h"
#include "gtest/gtest.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace googlesql {

template <typename T>
void TestMakeRandomCoverage(T (*generator)(absl::BitGen*),
                            bool expect_zero_values,
                            bool expect_negative_values) {
  absl::BitGen random;
  // covered[sign][i][j] = true iff there is at least one random value with
  // sign (0=non-negative; 1=negative), exactly i integer digits and exactly j
  // fractional digits.
  bool covered[2][T::kMaxIntegerDigits + 1][T::kMaxFractionalDigits + 1];
  memset(covered, 0, sizeof(covered));
  bool has_zero_values = false;
  bool has_negative_values = false;
  for (int i = 0; i < 100000; ++i) {
    T v = generator(&random);
    if (v == T()) {
      has_zero_values = true;
    }
    if (v < T()) {
      has_negative_values = true;
    }
    std::string str = v.ToString();
    ASSERT_TRUE(!str.empty());
    absl::string_view str_view = str;
    bool negative = false;
    if (str[0] == '-') {
      negative = true;
      str_view.remove_prefix(1);
    }
    size_t num_integer_digits = 0;
    size_t num_fractional_digits = 0;
    auto decimal_point_pos = str_view.find('.');
    if (decimal_point_pos == absl::string_view::npos) {
      num_integer_digits = str_view.size();
    } else {
      num_integer_digits = decimal_point_pos;
      num_fractional_digits = str_view.size() - decimal_point_pos - 1;
    }
    if (num_integer_digits == 1 && str_view[0] == '0') {
      num_integer_digits = 0;
    }
    ASSERT_LE(num_integer_digits, T::kMaxIntegerDigits);
    ASSERT_LE(num_fractional_digits, T::kMaxFractionalDigits);
    covered[negative][num_integer_digits][num_fractional_digits] = true;
  }
  size_t num_covered = 0;
  for (const auto& table : covered) {
    for (const auto& row : table) {
      for (bool cell : row) {
        num_covered += cell;
      }
    }
  }
  EXPECT_EQ(expect_zero_values, has_zero_values);
  EXPECT_EQ(expect_negative_values, has_negative_values);
  // covered[true][0][0] is always false, and hence subtract 1.
  size_t kNumPossibleCombinations = (1 + expect_negative_values) *
                                        (T::kMaxIntegerDigits + 1) *
                                        (T::kMaxFractionalDigits + 1) -
                                    1;
  // Should cover at least 90% of the possible combinations.
  // In an experiment with 200 runs, the minimum coverage is 99.9% for
  // BigNumericValue and 100% for NumericValue.
  EXPECT_GE(num_covered, 9 * kNumPossibleCombinations / 10);
}

template <typename T>
T MakeRandomNumericValueWrapper(absl::BitGen* random) {
  return MakeRandomNumericValue<T>(random, nullptr);
}

TEST(NumericTest, MakeRandomNumericValue_Coverage) {
  TestMakeRandomCoverage(&MakeRandomNumericValueWrapper<NumericValue>,
                         /* expect_zero_values = */ true,
                         /* expect_negative_values = */ true);
}

TEST(BigNumericTest, MakeRandomNumericValue_Coverage) {
  TestMakeRandomCoverage(&MakeRandomNumericValueWrapper<BigNumericValue>,
                         /* expect_zero_values = */ true,
                         /* expect_negative_values = */ true);
}

TEST(NumericTest, MakeRandomNonZeroNumericValue_Coverage) {
  TestMakeRandomCoverage(
      &MakeRandomNonZeroNumericValue<NumericValue, absl::BitGen>,
      /* expect_zero_values = */ false,
      /* expect_negative_values = */ true);
}

TEST(BigNumericTest, MakeRandomNonZeroNumericValue_Coverage) {
  TestMakeRandomCoverage(
      &MakeRandomNonZeroNumericValue<BigNumericValue, absl::BitGen>,
      /* expect_zero_values = */ false,
      /* expect_negative_values = */ true);
}

TEST(NumericTest, MakeRandomPositiveNumericValue_Coverage) {
  TestMakeRandomCoverage(
      &MakeRandomPositiveNumericValue<NumericValue, absl::BitGen>,
      /* expect_zero_values = */ false,
      /* expect_negative_values = */ false);
}

TEST(BigNumericTest, MakeRandomPositiveNumericValue_Coverage) {
  TestMakeRandomCoverage(
      &MakeRandomPositiveNumericValue<BigNumericValue, absl::BitGen>,
      /* expect_zero_values = */ false,
      /* expect_negative_values = */ false);
}

template <typename T>
void TestMakeLosslessRandomDoubleValueRoundTrip() {
  absl::BitGen random;
  for (int i = 0; i < 100000; ++i) {
    uint max_integer_bits =
        absl::Uniform<uint>(random, 0, 55 - T::kMaxFractionalDigits);
    double value = MakeLosslessRandomDoubleValue<T>(max_integer_bits, &random);
    EXPECT_LT(std::abs(value), 1ULL << max_integer_bits);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T numeric_value, T::FromDouble(value));
    EXPECT_EQ(value, numeric_value.ToDouble());
    std::string str = absl::StrFormat("%.*f", T::kMaxFractionalDigits, value);
    // Cannot directly compare str to numeric_value.ToString() because str
    // might have trailing zeros after the decimal point.
    EXPECT_THAT(T::FromStringStrict(str),
                ::absl_testing::IsOkAndHolds(numeric_value));
  }
}

TEST(NumericTest, MakeLosslessRandomDoubleValue) {
  TestMakeLosslessRandomDoubleValueRoundTrip<NumericValue>();
}

TEST(BigNumericTest, MakeLosslessRandomDoubleValue) {
  TestMakeLosslessRandomDoubleValueRoundTrip<BigNumericValue>();
}

}  // namespace googlesql
