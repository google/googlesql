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

#include "googlesql/public/functions/convert.h"

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

template <typename Type>
std::string NumberToString(Type v) {
  return absl::StrCat(v);
}
template <>
std::string NumberToString<float>(float v) {
  return absl::StrCat(v, "f");
}
template <>
std::string NumberToString<double>(double v) {
  return absl::StrCat(v, "d");
}

// Tests conversion of values approaching zero and the 'max' value of the ToType
// domain. 'max' may be positive or negative. Tests that monotonically
// increasing (decreasing) values are converted into non-decreasing
// (non-increasing) values. Assumes that FromType can accommodate ToType values
// in the range [0..max] if max is positive and [max..0] if max is negative.
template <typename FromType, typename ToType>
inline void TestMonotonicity(ToType max) {
  ToType converted_x, converted_y;
  absl::Status error, last_error;
  bool success = true;
  int num_iterations = 0;
  // divisor = 2 for integer FromType, 2.3 otherwise
  const FromType divisor = static_cast<FromType>(23) / 10;
  ToType prev_converted_x = max / divisor;
  ToType prev_converted_y = max - prev_converted_x;
  FromType last_x = 0;
  FromType x = static_cast<FromType>(prev_converted_x);
  while (x != last_x) {
    last_x = x;
    x /= divisor;
    FromType y = static_cast<FromType>(max) - x;
    EXPECT_TRUE((Convert<FromType, ToType>(x, &converted_x, &error))) << error;
    // Upper bound may result in legitimate out-of-range errors.
    success = Convert<FromType, ToType>(y, &converted_y, &error);
    std::string log =
        absl::StrCat("Iteration: ", num_iterations, " x: ", NumberToString(x),
                     " converted_x: ", converted_x, " y: ", NumberToString(y));
    if (success) {
      absl::StrAppend(&log, " converted_y: ", converted_y);
    } else {
      absl::StrAppend(&log,
                      " OUT OF RANGE max:", std::numeric_limits<ToType>::max());
    }
    GOOGLESQL_VLOG(1) << log;
    if (max > 0) {
      EXPECT_LE(converted_x, prev_converted_x);
      EXPECT_GE(converted_y, prev_converted_y);
      if (!last_error.ok()) {
        // Once upper bound is out of range, it stays out of range.
        EXPECT_FALSE(error.ok());
      }
    } else {
      EXPECT_TRUE(success);
      EXPECT_GE(converted_x, prev_converted_x);
      EXPECT_LE(converted_y, prev_converted_y);
    }
    if (std::is_integral<FromType>::value ||
        std::is_same<FromType, ToType>::value) {
      EXPECT_EQ(static_cast<ToType>(x), converted_x);
      EXPECT_EQ(static_cast<ToType>(y), converted_y);
    }
    prev_converted_x = converted_x;
    if (success) {
      prev_converted_y = converted_y;
    }
    last_error = error;
    num_iterations++;
  }
  EXPECT_EQ(0, converted_x);
  if (success) {
    EXPECT_EQ(max, converted_y);
  }
}

template <typename FromType, typename ToType>
inline void TestOutOfRange(ToType max) {
  // Exceed the bound by at least 0.5 and check for error. The epsilon is
  // gradually increased in magnitude until the bound is exceeded. Epsilon is
  // positive for 'max' > 0, negative otherwise.
  FromType max_plus_epsilon = static_cast<FromType>(max);
  FromType epsilon = 0.5;
  while (max_plus_epsilon == max) {
    max_plus_epsilon = max + (max > 0 ? epsilon : -epsilon);
    epsilon *= 2;
  }
  GOOGLESQL_VLOG(1) << "max_plus_epsilon: " << max_plus_epsilon
          << " epsilon: " << epsilon;
  ToType max_out;
  absl::Status error;
  EXPECT_FALSE((Convert<FromType, ToType>(
      max_plus_epsilon, &max_out, &error)))
      << "Converted " << max_plus_epsilon << " to " << max_out;
  // Ensure that the value appears in the error message.
  EXPECT_THAT(error,
              ::absl_testing::StatusIs(
                  absl::StatusCode::kOutOfRange,
                  ::testing::HasSubstr(absl::StrCat(": ", max_plus_epsilon))));
}

template <typename FromType, typename ToType>
inline void TestBoundaries(ToType max) {
  TestMonotonicity<FromType, ToType>(max);
  TestOutOfRange<FromType, ToType>(max);
}

constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();
constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::lowest();
constexpr uint32_t kUInt32Max = std::numeric_limits<uint32_t>::max();

constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();
constexpr int64_t kInt64Min = std::numeric_limits<int64_t>::lowest();
constexpr uint64_t kUInt64Max = std::numeric_limits<uint64_t>::max();

TEST(Convert, Int32) {
  TestMonotonicity<int32_t, int32_t>(kInt32Max);
  TestMonotonicity<int32_t, int32_t>(kInt32Min);
  TestMonotonicity<int32_t, int64_t>(kInt32Max);
  TestMonotonicity<int32_t, int64_t>(kInt32Min);
  TestMonotonicity<int32_t, uint32_t>(kInt32Max);
  TestMonotonicity<int32_t, uint64_t>(kInt32Max);
  // Divide max by 2 to avoid overflow in static_cast().
  TestMonotonicity<int32_t, float>(kInt32Max / 2);
  TestMonotonicity<int32_t, float>(kInt32Min);
  TestMonotonicity<int32_t, double>(kInt32Max);
  TestMonotonicity<int32_t, double>(kInt32Min);
}

TEST(Convert, Int64) {
  TestMonotonicity<int64_t, int32_t>(kInt32Max);
  TestMonotonicity<int64_t, int32_t>(kInt32Min);
  TestMonotonicity<int64_t, int64_t>(kInt64Max);
  TestMonotonicity<int64_t, int64_t>(kInt64Min);
  TestMonotonicity<int64_t, uint32_t>(kUInt32Max);
  TestMonotonicity<int64_t, uint64_t>(kInt64Max);
  // Divide max by 2 to avoid overflow in static_cast().
  TestMonotonicity<int64_t, float>(kInt64Max / 2);
  TestMonotonicity<int64_t, float>(kInt64Min);
  // Divide max by 2 to avoid overflow in static_cast().
  TestMonotonicity<int64_t, double>(kInt64Max / 2);
  TestMonotonicity<int64_t, double>(kInt64Min);
}

TEST(Convert, Uint32) {
  TestMonotonicity<uint32_t, int32_t>(kInt32Max);
  TestMonotonicity<uint32_t, int64_t>(kUInt32Max);
  TestMonotonicity<uint32_t, uint32_t>(kUInt32Max);
  TestMonotonicity<uint32_t, uint64_t>(kUInt32Max);
  // Divide max by 2 to avoid overflow in static_cast().
  TestMonotonicity<uint32_t, float>(kUInt32Max / 2);
  TestMonotonicity<uint32_t, double>(kUInt32Max);
}

TEST(Convert, Uint64) {
  TestMonotonicity<uint64_t, int32_t>(kInt32Max);
  TestMonotonicity<uint64_t, int64_t>(kInt64Max);
  TestMonotonicity<uint64_t, uint32_t>(kUInt32Max);
  TestMonotonicity<uint64_t, uint64_t>(kUInt64Max);
  // Divide max by 2 to avoid overflow in static_cast().
  TestMonotonicity<uint64_t, float>(kUInt64Max / 2);
  // Divide max by 2 to avoid overflow in static_cast().
  TestMonotonicity<uint64_t, double>(kUInt64Max / 2);
}

TEST(Convert, Float) {
  TestBoundaries<float, int32_t>(kInt32Max);
  TestBoundaries<float, int32_t>(kInt32Min);
  TestBoundaries<float, int64_t>(kInt64Max);
  TestBoundaries<float, int64_t>(kInt64Min);
  TestBoundaries<float, uint32_t>(kUInt32Max);
  TestBoundaries<float, uint64_t>(kUInt64Max);
  TestMonotonicity<float, float>(std::numeric_limits<float>::max());
  TestMonotonicity<float, float>(std::numeric_limits<float>::lowest());
  TestMonotonicity<float, double>(std::numeric_limits<float>::max());
  TestMonotonicity<float, double>(std::numeric_limits<float>::lowest());
}

TEST(Convert, Double) {
  TestBoundaries<double, int32_t>(kInt32Max);
  TestBoundaries<double, int32_t>(kInt32Min);
  TestBoundaries<double, int64_t>(kInt64Max);
  TestBoundaries<double, int64_t>(kInt64Min);
  TestBoundaries<double, uint32_t>(kUInt32Max);

  TestBoundaries<double, uint64_t>(kUInt64Max);
  TestBoundaries<double, float>(std::numeric_limits<float>::max());
  TestBoundaries<double, float>(std::numeric_limits<float>::lowest());
  TestMonotonicity<double, double>(std::numeric_limits<double>::max());
  TestMonotonicity<double, double>(std::numeric_limits<double>::lowest());
}

// Binary search for the maximal integer value that can be roundtripped via
// the corresponding floating point type. This is a helper method for
// determining boundaries in MaximalFloatToIntegerRoundtrip test.
template <typename FloatType, typename IntType>
void FindMaximalRoundtripValue() {
  IntType max = std::numeric_limits<IntType>::max();
  IntType upper = max;
  IntType lower = 0;
  IntType result;
  while (upper > lower + 1) {
    IntType mid = (upper - lower) / 2 + lower;
    absl::Status s;
    if (Convert<FloatType, IntType>(static_cast<FloatType>(mid), &result, &s)) {
      lower = mid;
    } else {
      upper = mid;
    }
  }
  ABSL_LOG(INFO) << "Maximal integer: " << lower << " is "
            << (max - lower) << " away from max " << max
            << ", convers back as " << result
            << " which is " << (max - result) << " away from max";
}

// The 'false' branches below test that the next-larger integer value produces
// a float-cast-overflow runtime error under ASAN.
TEST(Convert, MaximalFloatToIntegerRoundtrip) {
  {
    FindMaximalRoundtripValue<float, int32_t>();
    int32_t num1 = kInt32Max - (1 << 6);
    int32_t num2 = kInt32Max - (1 << 7) + 1;
    EXPECT_EQ(static_cast<int32_t>(static_cast<float>(num1)), num2);
    if (false) {
      EXPECT_NE(static_cast<int32_t>(static_cast<float>(num1 + 1)), 0);
    }
  }
  {
    FindMaximalRoundtripValue<float, uint32_t>();
    uint32_t num1 = kUInt32Max - (1 << 7);
    uint32_t num2 = kUInt32Max - (1 << 8) + 1;
    EXPECT_EQ(static_cast<uint32_t>(static_cast<float>(num1)), num2);
    if (false) {
      EXPECT_NE(static_cast<uint32_t>(static_cast<float>(num1 + 1)), 0);
    }
  }
  {
    FindMaximalRoundtripValue<float, int64_t>();
    int64_t num1 = kInt64Max - (1ll << 38);
    int64_t num2 = kInt64Max - (1ll << 39) + 1;
    EXPECT_EQ(static_cast<int64_t>(static_cast<float>(num1)), num2);
    if (false) {
      EXPECT_NE(static_cast<int64_t>(static_cast<float>(num1 + 1)), 0);
    }
  }
  {
    FindMaximalRoundtripValue<float, uint64_t>();
    uint64_t num1 = kUInt64Max - (1ull << 39);
    uint64_t num2 = kUInt64Max - (1ull << 40) + 1;
    EXPECT_EQ(static_cast<uint64_t>(static_cast<float>(num1)), num2);
    if (false) {
      EXPECT_NE(static_cast<uint64_t>(static_cast<float>(num1 + 1)), 0);
    }
  }
  {
    FindMaximalRoundtripValue<double, int64_t>();
    int64_t num1 = kInt64Max - (1ll << 9);
    int64_t num2 = kInt64Max - (1ll << 10) + 1;
    EXPECT_EQ(static_cast<int64_t>(static_cast<double>(num1)), num2);
    if (false) {
      EXPECT_NE(static_cast<int64_t>(static_cast<double>(num1 + 1)), 0);
    }
  }
  {
    FindMaximalRoundtripValue<double, uint64_t>();
    uint64_t num1 = kUInt64Max - (1ull << 10);
    uint64_t num2 = kUInt64Max - (1ull << 11) + 1;
    EXPECT_EQ(static_cast<uint64_t>(static_cast<double>(num1)), num2);
    if (false) {
      EXPECT_NE(static_cast<uint64_t>(static_cast<double>(num1 + 1)), 0);
    }
  }
}

}  // namespace functions
}  // namespace googlesql
