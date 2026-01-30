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

#include "googlesql/public/anonymization_utils.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace googlesql {
namespace anonymization {
namespace {

using ::testing::DoubleEq;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

constexpr int64_t kint64min = std::numeric_limits<int64_t>::lowest();
constexpr int64_t kint64max = std::numeric_limits<int64_t>::max();

constexpr double kPosInf = std::numeric_limits<double>::infinity();
constexpr double kNegInf = -std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kDoubleMax = std::numeric_limits<double>::max();
constexpr double kDoubleMin = std::numeric_limits<double>::lowest();
constexpr double kDoubleMinPos = std::numeric_limits<double>::min();

constexpr double kCalcDeltaTestDefaultTolerance = 1e-03;

struct ComputeDeltaTest {
  ComputeDeltaTest(double epsilon_in, int64_t k_threshold_in,
                   std::optional<int64_t> kappa_in,
                   std::optional<double> expected_delta_in,
                   double tolerance_in = kCalcDeltaTestDefaultTolerance)
      : epsilon(epsilon_in),
        k_threshold(k_threshold_in),
        kappa(kappa_in),
        expected_delta(expected_delta_in),
        tolerance(tolerance_in) {}

  const double epsilon;
  const int64_t k_threshold;
  const std::optional<int64_t> kappa;
  // Missing implies an error is returned.
  const std::optional<double> expected_delta;
  const double tolerance;
};

static ComputeDeltaTest DeltaPartitionTest(
    double epsilon, int64_t k_threshold, double expected_delta,
    double tolerance = kCalcDeltaTestDefaultTolerance) {
  return ComputeDeltaTest(epsilon, k_threshold, std::optional<int64_t>(),
                          expected_delta, tolerance);
}

static ComputeDeltaTest DeltaDatasetTest(
    double epsilon, int64_t k_threshold, int64_t kappa, double expected_delta,
    double tolerance = kCalcDeltaTestDefaultTolerance) {
  return ComputeDeltaTest(epsilon, k_threshold, kappa, expected_delta,
                          tolerance);
}

static ComputeDeltaTest DeltaErrorTest(
    double epsilon, int64_t k_threshold,
    std::optional<int64_t> kappa = std::optional<int64_t>()) {
  return ComputeDeltaTest(epsilon, k_threshold, kappa, std::optional<double>());
}

static void RunDeltaTest(ComputeDeltaTest test_case) {
  const Value epsilon_value = Value::Double(test_case.epsilon);
  const Value k_threshold_value = Value::Int64(test_case.k_threshold);
  const Value kappa_value =
      (test_case.kappa.has_value() ? Value::Int64(test_case.kappa.value())
                                   : Value());
  const std::string test_case_string =
      absl::StrCat("epsilon: ", epsilon_value.DebugString(),
                   ", k_threshold: ", k_threshold_value.DebugString(),
                   ", kappa: ", kappa_value.DebugString());

  absl::StatusOr<Value> status_or_delta = ComputeDeltaFromLaplaceThreshold(
      epsilon_value, k_threshold_value, kappa_value);

  if (test_case.expected_delta.has_value()) {
    // We expect the test to succeed, and a delta to be computed.
    ASSERT_TRUE(status_or_delta.ok()) << status_or_delta.status() << "\n"
                                      << test_case_string;
    const Value& delta_value = status_or_delta.value();
    EXPECT_THAT(delta_value.double_value(),
                testing::DoubleNear(test_case.expected_delta.value(),
                                    test_case.tolerance))
        << test_case_string;
  } else {
    EXPECT_FALSE(status_or_delta.ok())
        << test_case_string
        << "\nunexpected successfully computed delta value: "
        << status_or_delta.value();
  }
}

struct ComputeKThresholdTest {
  ComputeKThresholdTest(double epsilon_in, double delta_in,
                        std::optional<int64_t> kappa_in,
                        std::optional<int64_t> expected_k_threshold_in)
      : epsilon(epsilon_in),
        delta(delta_in),
        kappa(kappa_in),
        expected_k_threshold(expected_k_threshold_in) {}

  const double epsilon;
  const double delta;
  const std::optional<int64_t> kappa;
  // Missing implies an error is returned.
  const std::optional<double> expected_k_threshold;
};

static ComputeKThresholdTest KThresholdPartitionTest(
    double epsilon, double delta, int64_t expected_k_threshold) {
  return ComputeKThresholdTest(epsilon, delta, std::optional<int64_t>(),
                               expected_k_threshold);
}

static ComputeKThresholdTest KThresholdDatasetTest(
    double epsilon, double delta, int64_t kappa, int64_t expected_k_threshold) {
  return ComputeKThresholdTest(epsilon, delta, kappa, expected_k_threshold);
}

static ComputeKThresholdTest KThresholdErrorTest(
    double epsilon, double delta,
    std::optional<int64_t> kappa = std::optional<int64_t>()) {
  return ComputeKThresholdTest(epsilon, delta, kappa, std::optional<double>());
}

static void RunKThresholdTest(ComputeKThresholdTest test_case) {
  const Value epsilon_value = Value::Double(test_case.epsilon);
  const Value delta_value = Value::Double(test_case.delta);
  const Value kappa_value =
      (test_case.kappa.has_value() ? Value::Int64(test_case.kappa.value())
                                   : Value());
  const std::string test_case_string =
      absl::StrCat("epsilon: ", epsilon_value.DebugString(),
                   ", delta: ", delta_value.DebugString(),
                   ", kappa: ", kappa_value.DebugString());

  absl::StatusOr<Value> status_or_k_threshold =
      ComputeLaplaceThresholdFromDelta(epsilon_value, delta_value, kappa_value);

  if (test_case.expected_k_threshold.has_value()) {
    // We expect the test to succeed, and a k_threshold to be computed.
    ASSERT_TRUE(status_or_k_threshold.ok())
        << status_or_k_threshold.status() << "\n"
        << test_case_string;
    const Value& k_threshold_value = status_or_k_threshold.value();
    EXPECT_EQ(k_threshold_value.int64_value(),
              test_case.expected_k_threshold.value())
        << test_case_string;
  } else {
    EXPECT_FALSE(status_or_k_threshold.ok())
        << test_case_string
        << "\nunexpected successfully computed k_threshold value: "
        << status_or_k_threshold.value();
  }
}

TEST(ComputeAnonymizationUtilsTest, ComputeDeltaTests) {
  std::vector<ComputeDeltaTest> delta_test_cases = {
      // clang-format off
    // Tests with kappa unset.
    //
    // First set of tests, fix epsilon = ln(3) and vary k_threshold.
    //
    //                                            expected       test
    //                      epsilon  k_threshold  delta          tolerance
    //                 ------------  -----------  -------------  ---------
    DeltaPartitionTest(std::log(3.0),  kint64min, 1.0),
    DeltaPartitionTest(std::log(3.0),   -1000000, 1.0),
    DeltaPartitionTest(std::log(3.0),    -100000, 1.0),
    DeltaPartitionTest(std::log(3.0),     -10000, 1.0),
    DeltaPartitionTest(std::log(3.0),      -1000, 1.0),
    DeltaPartitionTest(std::log(3.0),       -100, 1.0),
    DeltaPartitionTest(std::log(3.0),        -10, 0.9999972),
    DeltaPartitionTest(std::log(3.0),          0, 0.83333331),
    DeltaPartitionTest(std::log(3.0),          1, 0.5),
    DeltaPartitionTest(std::log(3.0),          2, 0.16666667),
    DeltaPartitionTest(std::log(3.0),          3, 0.05555555556, 1e-05),
    DeltaPartitionTest(std::log(3.0),          4, 0.01851851852, 1e-05),
    DeltaPartitionTest(std::log(3.0),          5, 0.00617283960, 1e-06),
    DeltaPartitionTest(std::log(3.0),         10, 2.5402631e-05, 1e-08),
    DeltaPartitionTest(std::log(3.0),         20, 4.3019580e-10, 1e-13),
    DeltaPartitionTest(std::log(3.0),         50, 2.0894334e-24, 1e-27),
    DeltaPartitionTest(std::log(3.0),         75, 2.4660232e-36, 1e-39),
    DeltaPartitionTest(std::log(3.0),         87, 4.6402600e-42, 6e-46),
    DeltaPartitionTest(std::log(3.0),         93, 6.3652400e-45, 1e-48),
    DeltaPartitionTest(std::log(3.0),         94, 2.1217500e-45, 1e-48),
    DeltaPartitionTest(std::log(3.0),         95, 7.0724900e-46, 1e-49),
    DeltaPartitionTest(std::log(3.0),         96, 2.3575000e-46, 1e-49),
    DeltaPartitionTest(std::log(3.0),        100, 2.9104900e-48, 1e-51),
    DeltaPartitionTest(std::log(3.0),       1000, 0.0,           1e-100),
    DeltaPartitionTest(std::log(3.0),      10000, 0.0,           1e-100),
    DeltaPartitionTest(std::log(3.0),     100000, 0.0,           1e-100),
    DeltaPartitionTest(std::log(3.0),    1000000, 0.0,           1e-100),
    DeltaPartitionTest(std::log(3.0),  kint64max, 0.0,           1e-100),

    // Second set of tests, fix k_threshold = 50 and vary epsilon.
    //
    //                                             expected       test
    //                       epsilon  k_threshold  delta          tolerance
    //                 -------------  -----------  -------------  ---------
    DeltaPartitionTest(kDoubleMinPos,          50, 0.5),
    DeltaPartitionTest(       1e-308,          50, 0.5),
    DeltaPartitionTest(       1e-100,          50, 0.5),
    DeltaPartitionTest(        1e-50,          50, 0.5),
    DeltaPartitionTest(        1e-20,          50, 0.5),
    DeltaPartitionTest(        1e-10,          50, 0.5),
    DeltaPartitionTest(         1e-5,          50, 0.49975505),
    DeltaPartitionTest(         1e-2,          50, 0.30631319),
    DeltaPartitionTest(         1e-1,          50, 0.0037232914,  1e-06),
    DeltaPartitionTest(          0.5,          50, 1.1448674e-11, 1e-14),
    DeltaPartitionTest(          1.0,          50, 2.6214428e-22, 1e-25),
    DeltaPartitionTest(std::log(3.0),          50, 2.0894334e-24, 1e-27),
    DeltaPartitionTest(          1.5,          50, 6.0024092e-33, 1e-36),
    DeltaPartitionTest(          2.0,          50, 1.3732725e-43, 1.2e-46),
    DeltaPartitionTest(          5.0,          50, 0.0,           1e-100),
    DeltaPartitionTest(          1e1,          50, 0.0,           1e-100),
    DeltaPartitionTest(          1e2,          50, 0.0,           1e-100),
    DeltaPartitionTest(          1e5,          50, 0.0,           1e-100),
    DeltaPartitionTest(         1e10,          50, 0.0,           1e-100),
    DeltaPartitionTest(         1e20,          50, 0.0,           1e-100),
    DeltaPartitionTest(         1e50,          50, 0.0,           1e-100),
    DeltaPartitionTest(        1e100,          50, 0.0,           1e-100),
    DeltaPartitionTest(        1e308,          50, 0.0,           1e-100),
    DeltaPartitionTest(   kDoubleMax,          50, 0.0,           1e-100),

    // Tests with kappa explicitly set.
    //
    // Fix epsilon and k_threshold (k) and vary kappa.
    //                                             expected       test
    //                     epsilon   k      kappa  delta          tolerance
    //               -------------  --  ---------  -------------  ---------
    DeltaDatasetTest(std::log(3.0), 50,         1, 2.0894334e-24, 1e-27),
    DeltaDatasetTest(std::log(3.0), 50,         2, 2.0442300e-12, 1e-15),
    DeltaDatasetTest(std::log(3.0), 50,         3, 2.4160800e-08, 1e-11),
    DeltaDatasetTest(std::log(3.0), 50,         4, 2.8595300e-06, 1e-09),
    DeltaDatasetTest(std::log(3.0), 50,         5, 5.2740300e-05, 1e-08),
    DeltaDatasetTest(std::log(3.0), 50,        10, 0.0227296,     1e-05),
    DeltaDatasetTest(std::log(3.0), 50,       100, 1.0),
    DeltaDatasetTest(std::log(3.0), 50,      1000, 1.0),
    DeltaDatasetTest(std::log(3.0), 50,     10000, 1.0),
    DeltaDatasetTest(std::log(3.0), 50,    100000, 1.0),
    DeltaDatasetTest(std::log(3.0), 50,   1000000, 1.0),
    DeltaDatasetTest(std::log(3.0), 50, kint64max, 1.0),

    // Error cases.
    //
    // Epsilon must be finite and greater than 0.
    //
    //                    epsilon  k_threshold
    //                 ----------  -----------
    DeltaErrorTest(kDoubleMin,          50),
    DeltaErrorTest(      -1.0,          50),
    DeltaErrorTest(       0.0,          50),
    DeltaErrorTest(   kPosInf,          50),
    DeltaErrorTest(   kNegInf,          50),
    DeltaErrorTest(      kNaN,          50),

    // Kappa must be greater than 0.
    //
    //                       epsilon  k_threshold      kappa
    //                 -------------  -----------  ---------
    DeltaErrorTest(std::log(3.0),          50, kint64min),
    DeltaErrorTest(std::log(3.0),          50,        -1),
    DeltaErrorTest(std::log(3.0),          50,         0),
      // clang-format on
  };

  for (const ComputeDeltaTest& test_case : delta_test_cases) {
    RunDeltaTest(test_case);
  }
}

TEST(ComputeAnonymizationUtilsTest, ComputeKThresholdTests) {
  std::vector<ComputeKThresholdTest> k_threshold_test_cases = {
      // clang-format off
    // Tests with kappa unset.
    //
    // First set of tests, fix epsilon = ln(3) and vary delta.
    //
    //                                                    expected
    //                            epsilon          delta  k_threshold
    //                       ------------  -------------  -----------
    KThresholdPartitionTest(std::log(3.0),           0.0,   kint64max),
    KThresholdPartitionTest(std::log(3.0), kDoubleMinPos,         646),
    KThresholdPartitionTest(std::log(3.0),        1e-308,         646),
    KThresholdPartitionTest(std::log(3.0),        1e-256,         537),
    KThresholdPartitionTest(std::log(3.0),        1e-128,         269),
    KThresholdPartitionTest(std::log(3.0),         1e-64,         135),
    KThresholdPartitionTest(std::log(3.0),         1e-32,          68),
    KThresholdPartitionTest(std::log(3.0), 2.0894334e-24,          50),
    KThresholdPartitionTest(std::log(3.0),         1e-16,          34),
    KThresholdPartitionTest(std::log(3.0),          1e-8,          18),
    KThresholdPartitionTest(std::log(3.0),          1e-4,           9),
    KThresholdPartitionTest(std::log(3.0),          1e-2,           5),
    KThresholdPartitionTest(std::log(3.0),           0.1,           3),
    KThresholdPartitionTest(std::log(3.0),           0.3,           2),
    KThresholdPartitionTest(std::log(3.0),           0.5,           1),
    KThresholdPartitionTest(std::log(3.0),           0.7,           1),
    KThresholdPartitionTest(std::log(3.0),           0.9,           0),
    KThresholdPartitionTest(std::log(3.0),           1.0,   kint64min),

    // These tests cover settings used in some of the query tests.
    KThresholdPartitionTest(   1000000000,           0.0,   kint64max),
    KThresholdPartitionTest(   1000000000, kDoubleMinPos,           2),
    KThresholdPartitionTest(   1000000000,        1e-308,           2),
    KThresholdPartitionTest(   1000000000,        1e-200,           2),
    KThresholdPartitionTest(   1000000000,        1e-100,           2),
    KThresholdPartitionTest(   1000000000,         1e-50,           2),
    KThresholdPartitionTest(   1000000000,          1e-8,           2),
    KThresholdPartitionTest(   1000000000,           0.1,           2),
    // Note that the formula for computing k_threshold when delta > 0.5
    // is not very tight, and will likely be improved to produce much
    // smaller (negative) k_threshold values.  At that point, we will
    // need to update these tests.
    KThresholdPartitionTest(   1000000000,           0.5,           1),
    KThresholdPartitionTest(   1000000000,           0.8,           1),
    KThresholdPartitionTest(   1000000000,           1.0,   kint64min),

    // Second set of tests with kappa unset, fix delta = 2.0894334e-24 and
    // vary epsilon.
    //
    //                                                     expected
    //                            epsilon          delta   k_threshold
    //                 ------------------  -------------  ------------
    KThresholdPartitionTest(kDoubleMinPos, 2.0894334e-24,    kint64max),
    KThresholdPartitionTest(       1e-308, 2.0894334e-24,    kint64max),
    KThresholdPartitionTest(       1e-100, 2.0894334e-24,    kint64max),
    KThresholdPartitionTest(        1e-50, 2.0894334e-24,    kint64max),
    KThresholdPartitionTest(        1e-20, 2.0894334e-24,    kint64max),
    KThresholdPartitionTest(        1e-10, 2.0894334e-24, 538320021227),
    KThresholdPartitionTest(         1e-5, 2.0894334e-24,      5383202),
    KThresholdPartitionTest(         1e-2, 2.0894334e-24,         5385),
    KThresholdPartitionTest(         1e-1, 2.0894334e-24,          540),
    KThresholdPartitionTest(          0.5, 2.0894334e-24,          109),
    KThresholdPartitionTest(          1.0, 2.0894334e-24,           55),
    KThresholdPartitionTest(std::log(3.0), 2.0894334e-24,           50),
    KThresholdPartitionTest(          1.5, 2.0894334e-24,           37),
    KThresholdPartitionTest(          2.0, 2.0894334e-24,           28),
    KThresholdPartitionTest(          5.0, 2.0894334e-24,           12),
    KThresholdPartitionTest(          1e1, 2.0894334e-24,            7),
    KThresholdPartitionTest(          1e2, 2.0894334e-24,            2),
    KThresholdPartitionTest(          1e5, 2.0894334e-24,            2),
    KThresholdPartitionTest(         1e10, 2.0894334e-24,            2),
    KThresholdPartitionTest(         1e20, 2.0894334e-24,            1),
    KThresholdPartitionTest(         1e50, 2.0894334e-24,            1),
    KThresholdPartitionTest(        1e100, 2.0894334e-24,            1),
    KThresholdPartitionTest(        1e308, 2.0894334e-24,            1),
    KThresholdPartitionTest(   kDoubleMax, 2.0894334e-24,            1),

    // Tests with kappa explicitly set.
    //
    // Fix epsilon and delta and vary kappa.
    //                                                             expected
    //                          epsilon          delta      kappa  k_threshold
    //                    -------------  -------------  ---------  -----------
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,         1,         50),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,         2,        101),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,         3,        151),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,         4,        203),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,         5,        254),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,        10,        512),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,       100,       5321),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,      1000,      55289),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,     10000,     573838),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,    100000,    5947953),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24,   1000000,   61575421),
    KThresholdDatasetTest(std::log(3.0), 2.0894334e-24, kint64max,  kint64max),

    // A few more random tests, illustrating that we can legitimately compute a
    // negative k_threshold.
    KThresholdDatasetTest(   0.001, 0.99999999999, 1, -24634),
    KThresholdPartitionTest( 0.001, 0.99999999999,    -24634),

    // Error cases.
    //
    // Epsilon must be finite and greater than 0.
    //
    //                     epsilon          delta
    //                  ----------  -------------
    KThresholdErrorTest(kDoubleMin, 2.0894334e-24),
    KThresholdErrorTest(      -1.0, 2.0894334e-24),
    KThresholdErrorTest(       0.0, 2.0894334e-24),
    KThresholdErrorTest(   kPosInf, 2.0894334e-24),
    KThresholdErrorTest(   kNegInf, 2.0894334e-24),
    KThresholdErrorTest(      kNaN, 2.0894334e-24),

    // Kappa must be greater than 0.
    //
    //                        epsilon          delta      kappa
    //                  -------------  -------------  ---------
    KThresholdErrorTest(std::log(3.0), 2.0894334e-24, kint64min),
    KThresholdErrorTest(std::log(3.0), 2.0894334e-24,        -1),
    KThresholdErrorTest(std::log(3.0), 2.0894334e-24,         0),

    // Delta must be in range [0, 1].
    //
    //                        epsilon             delta      kappa
    //                  -------------  ----------------  ---------
    KThresholdErrorTest(std::log(3.0),         kNegInf),
    KThresholdErrorTest(std::log(3.0),         kNegInf,          1),
    KThresholdErrorTest(std::log(3.0),              -1),
    KThresholdErrorTest(std::log(3.0),              -1,          1),
    KThresholdErrorTest(std::log(3.0), 0-kDoubleMinPos),
    KThresholdErrorTest(std::log(3.0), 0-kDoubleMinPos,          1),
    KThresholdErrorTest(std::log(3.0), 1.0000000000001),
    KThresholdErrorTest(std::log(3.0), 1.0000000000001,          1),
    KThresholdErrorTest(std::log(3.0),               2),
    KThresholdErrorTest(std::log(3.0),               2,          1),
    KThresholdErrorTest(std::log(3.0),         kPosInf),
    KThresholdErrorTest(std::log(3.0),         kPosInf,          1),
      // clang-format on
  };

  for (const ComputeKThresholdTest& test_case : k_threshold_test_cases) {
    RunKThresholdTest(test_case);
  }
}

TEST(ComputeAnonymizationUtilsTest, RoundTripKThresholdTests) {
  // Vary k_threshold from 1 to 100, and kappa from 1 to 5.  Use
  // the often-recommended epsilon value of ln(3).  Compute delta
  // for each triple (epsilon, k_threshold, kappa), and then re-compute
  // k_threshold to ensure it is the same.  Note that once the k_threshold
  // gets high enough so that the computed delta is 0, round tripping back
  // to the original k_threshold longer works so we stop early in that
  // case.
  const Value epsilon_value = Value::Double(std::log(3));
  for (int kappa = 1; kappa < 5; ++kappa) {
    const Value kappa_value = Value::Int64(kappa);
    bool computed_delta_of_zero = false;
    for (int64_t k_threshold = 10; k_threshold < 1000; k_threshold += 10) {
      const Value k_threshold_value = Value::Int64(k_threshold);
      std::string test_case_string =
          absl::StrCat("k_threshold: ", k_threshold, ", kappa: ", kappa);
      absl::StatusOr<Value> status_or_delta = ComputeDeltaFromLaplaceThreshold(
          epsilon_value, k_threshold_value, kappa_value);
      ASSERT_TRUE(status_or_delta.ok()) << status_or_delta.status() << "\n"
                                        << test_case_string;
      const Value& delta_value = status_or_delta.value();
      absl::StrAppend(&test_case_string,
                      ", computed delta: ", delta_value.double_value());

      // If the computed delta is 0, then computing k_threshold from this will
      // result in the maximum int64 value (9223372036854775807).  Therefore
      // round tripping will not work.  Note that all remaining input
      // k_threshold values are higher that this one within this loop, which
      // implies that all remaining computed deltas will be 0 as well.
      if (computed_delta_of_zero) {
        EXPECT_EQ(delta_value.double_value(), 0);
        continue;
      }
      if (delta_value.double_value() == 0) {
        computed_delta_of_zero = true;
        continue;
      }

      absl::StatusOr<Value> status_or_computed_k_threshold =
          ComputeLaplaceThresholdFromDelta(epsilon_value, delta_value,
                                           kappa_value);
      ASSERT_TRUE(status_or_computed_k_threshold.ok())
          << status_or_computed_k_threshold.status() << "\n"
          << test_case_string;

      // We normally expect that the original k_threshold and the round tripped
      // k_threshold are the same.  However, in rare cases the loss of precision
      // due to floating point calculations means that we may be off by one.
      const int64_t original_k_threshold = k_threshold_value.int64_value();
      const int64_t computed_k_threshold =
          status_or_computed_k_threshold.value().int64_value();
      EXPECT_GE(computed_k_threshold, original_k_threshold - 1)
          << test_case_string;
      EXPECT_LE(computed_k_threshold, original_k_threshold + 1)
          << test_case_string;
    }
  }
}

// Helper function for computing delta from (epsilon, k_threshold, kappa).
static absl::StatusOr<double> ComputeDelta(Value epsilon_value,
                                           Value k_threshold_value,
                                           Value kappa_value) {
  absl::StatusOr<Value> status_or_computed_delta =
      ComputeDeltaFromLaplaceThreshold(epsilon_value, k_threshold_value,
                                       kappa_value);
  if (!status_or_computed_delta.ok()) {
    return status_or_computed_delta.status();
  }
  return status_or_computed_delta.value().double_value();
}

TEST(ComputeAnonymizationUtilsTest, RoundTripDeltaTests) {
  // Ideally, vary kappa from 1 to 5, and delta from 0 to 1 (exclusive).  Use
  // the often-recommended epsilon value of ln(3).  Compute k_threshold
  // for each triple (epsilon, delta, kappa), and then re-compute the
  // delta.
  //
  // We can't check the recomputed delta for equality against the original
  // delta because there are many deltas that map to the same k_threshold.
  // We also cannot check that the recomputed delta is closer to the
  // original delta than to computed deltas for k_threshold +/-1, and we
  // cannot check that the original delta is closer to the recomputed delta
  // than to the computed deltas for k_threshold +/1.  This is because the
  // curve is not linear, and for example the original delta can be closer
  // to the computed delta for k_threshold-1 than to the recomputed delta.
  //
  // So we just test here that the original delta is between the recomputed
  // delta and either the computed delta for k+1 or k-1.
  const Value epsilon_value = Value::Double(std::log(3));
  for (int kappa = 1; kappa < 5; ++kappa) {
    const Value kappa_value = Value::Int64(kappa);
    for (double delta = 1e-308; delta < 1.0; delta *= 10) {
      const Value delta_value = Value::Double(delta);
      std::string test_case_string =
          absl::StrCat("delta: ", delta, ", kappa: ", kappa);

      absl::StatusOr<Value> status_or_k_threshold =
          ComputeLaplaceThresholdFromDelta(epsilon_value, delta_value,
                                           kappa_value);
      ASSERT_TRUE(status_or_k_threshold.ok())
          << status_or_k_threshold.status() << "\n"
          << test_case_string;
      const Value& k_threshold_value = status_or_k_threshold.value();

      absl::StrAppend(&test_case_string, ", computed k_threshold: ",
                      k_threshold_value.int64_value());

      const absl::StatusOr<double> computed_delta =
          ComputeDelta(epsilon_value, k_threshold_value, kappa_value);

      const Value k_threshold_plus_one_value =
          values::Int64(k_threshold_value.int64_value() + 1);
      const absl::StatusOr<double> computed_delta_plus_one =
          ComputeDelta(epsilon_value, k_threshold_plus_one_value, kappa_value);

      const Value k_threshold_minus_one_value =
          values::Int64(k_threshold_value.int64_value() - 1);
      const absl::StatusOr<double> computed_delta_minus_one =
          ComputeDelta(epsilon_value, k_threshold_minus_one_value, kappa_value);

      // Expect that the original delta is closer to the computed delta than
      // to the computed delta for k+1, k-1.
      ASSERT_TRUE(computed_delta.ok());
      ASSERT_TRUE(computed_delta_plus_one.ok());
      ASSERT_TRUE(computed_delta_minus_one.ok());

      absl::StrAppend(&test_case_string,
                      ", computed delta: ", computed_delta.value());
      absl::StrAppend(&test_case_string, ", computed delta (k plus_one): ",
                      computed_delta_plus_one.value());
      absl::StrAppend(&test_case_string, ", computed delta (k minus one): ",
                      computed_delta_minus_one.value());

      // The original delta and recomputed delta for k_threshold is greater
      // than the computed delta for k_threshold+1.
      // The original delta and recomputed delta for k_threshold is less than
      // the computed delta for k_threshold-1.
      EXPECT_GT(delta, computed_delta_plus_one.value());
      EXPECT_LT(delta, computed_delta_minus_one.value());

      EXPECT_GT(computed_delta.value(), computed_delta_plus_one.value());
      EXPECT_LT(computed_delta.value(), computed_delta_minus_one.value());

      EXPECT_TRUE((delta >= computed_delta.value() &&
                   delta <= computed_delta_minus_one.value()) ||
                  (delta <= computed_delta.value() &&
                   delta >= computed_delta_plus_one.value()));
    }
  }
}

class FunctionEpsilonAssignerTest : public ::testing::Test {
 public:
  std::unique_ptr<const ResolvedAggregateFunctionCall>
  MakeFakeAggregateFunctionWithEpsilon(Value epsilon_value) {
    FunctionArgumentTypeOptions epsilon_argument_options;
    epsilon_argument_options.set_argument_name(
        "epsilon", FunctionEnums::NAMED_ARGUMENT_KIND_UNSPECIFIED);

    std::vector<std::unique_ptr<const ResolvedExpr>> argument_list;
    argument_list.emplace_back(MakeResolvedLiteral(epsilon_value));

    return MakeResolvedAggregateFunctionCall(
        /*type=*/nullptr,
        /*function=*/nullptr,
        /*signature=*/
        FunctionSignature(
            /*result_type=*/FunctionArgumentType(types::DoubleType()),
            /*arguments=*/
            {FunctionArgumentType(epsilon_value.type(),
                                  epsilon_argument_options)},
            /*context_ptr=*/nullptr),
        /*argument_list=*/std::move(argument_list),
        /*error_mode=*/ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
        /*distinct=*/false,
        /*null_handling_modifier=*/
        ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING,
        /*having_modifier=*/nullptr,
        /*order_by_item_list=*/{},
        /*limit=*/nullptr);
  }

  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan>
  MakeFakeDpAggregateScan(
      std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>&
          function_calls,
      Value option_epsilon,
      std::optional<Value> option_group_selection_epsilon = std::nullopt,
      std::unique_ptr<const ResolvedAggregateFunctionCall>
          group_selection_call = nullptr) {
    std::vector<std::unique_ptr<const ResolvedOption>> option_list;
    option_list.emplace_back(MakeResolvedOption(
        /*qualifier=*/"",
        /*name=*/"epsilon", MakeResolvedLiteral(option_epsilon)));
    option_list.emplace_back(MakeResolvedOption(
        /*qualifier=*/"",
        /*name=*/"delta", MakeResolvedLiteral(Value::Double(1e-3))));
    if (option_group_selection_epsilon.has_value()) {
      option_list.emplace_back(MakeResolvedOption(
          /*qualifier=*/"",
          /*name=*/"group_selection_epsilon",
          MakeResolvedLiteral(option_group_selection_epsilon.value())));
    }
    if (group_selection_call == nullptr) {
      option_list.emplace_back(MakeResolvedOption(
          /*qualifier=*/"",
          /*name=*/"group_selection_strategy",
          MakeResolvedLiteral(Value::Enum(
              types::DifferentialPrivacyGroupSelectionStrategyEnumType(),
              functions::DifferentialPrivacyEnums::PUBLIC_GROUPS,
              /*allow_unknown_enum_values=*/false))));
    }

    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        aggregate_list;
    for (auto& function_call : function_calls) {
      aggregate_list.emplace_back(MakeResolvedComputedColumn(
          /*column=*/ResolvedColumn(
              /*column_id=*/next_column_id_++,
              /*table_name=*/id_string_pool_.Make("testtable"),
              /*name=*/id_string_pool_.Make("testname"),
              /*type=*/types::DoubleType()),
          /*expr=*/std::move(function_call)));
    }

    std::unique_ptr<const ResolvedExpr> group_selection_threshold_expr;
    if (group_selection_call != nullptr) {
      const int group_selection_column_id = next_column_id_++;
      aggregate_list.emplace_back(MakeResolvedComputedColumn(
          /*column=*/ResolvedColumn(
              /*column_id=*/group_selection_column_id,
              /*table_name=*/id_string_pool_.Make("testtable"),
              /*name=*/id_string_pool_.Make("testname"),
              /*type=*/types::DoubleType()),
          /*expr=*/std::move(group_selection_call)));
      group_selection_threshold_expr = MakeResolvedColumnRef(
          /*type=*/types::DoubleType(),
          /*column=*/
          ResolvedColumn(
              /*column_id=*/group_selection_column_id,
              /*table_name=*/id_string_pool_.Make("testtable"),
              /*name=*/id_string_pool_.Make("testname"),
              /*type=*/types::DoubleType()),
          /*is_correlated=*/false);
    }

    return MakeResolvedDifferentialPrivacyAggregateScan(
        /*column_list=*/{},
        /*input_scan=*/nullptr,
        /*group_by_list=*/{},
        /*aggregate_list=*/std::move(aggregate_list),
        /*grouping_set_list=*/{},
        /*rollup_column_list=*/{},
        /*group_selection_threshold_expr=*/
        std::move(group_selection_threshold_expr),
        /*option_list=*/std::move(option_list));
  }

 private:
  IdStringPool id_string_pool_;
  int next_column_id_ = 1;
};

TEST_F(FunctionEpsilonAssignerTest,
       CreateFromScanWhenSumFunctionEpsilonLargerOptionReturnsOverconsumption) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));

  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(function_calls, Value::Double(3.0));

  EXPECT_THAT(
      FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("overconsumption")));
}

TEST_F(FunctionEpsilonAssignerTest,
       CreateFromScanWhenFullySpecifiedWithoutEpsilonOptionsReturnsOk) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));

  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(function_calls, Value::NullDouble());

  EXPECT_THAT(FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()), IsOk());
}

TEST_F(FunctionEpsilonAssignerTest,
       CreateFromScanWhenPartiallySpecifiedWithoutEpsilonOptionReturnsError) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::NullDouble()));

  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(function_calls, Value::NullDouble());

  EXPECT_THAT(FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("EPSILON must be set")));
}

TEST_F(FunctionEpsilonAssignerTest,
       CreateFromScanWhenPartiallySpecifiedWith0RemainingEpsilonReturnsError) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::NullDouble()));

  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(function_calls, Value::Double(2.2));

  EXPECT_THAT(
      FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("overconsumption")));
}

// This test is reproducing a query that is not rewritten as in b/445224883. We
// want to make sure that the internal error makes it clear that the query was
// not rewritten. This is an issue on the engine side.
TEST_F(FunctionEpsilonAssignerTest,
       CreateFromScanWhenUsingMinPrivacyUnitsWithUnwrittenQueryReturnsOk) {
  std::vector<std::unique_ptr<const ResolvedOption>> option_list;
  option_list.emplace_back(MakeResolvedOption(
      /*qualifier=*/"",
      /*name=*/"epsilon", MakeResolvedLiteral(Value::Double(1.1))));
  option_list.emplace_back(MakeResolvedOption(
      /*qualifier=*/"",
      /*name=*/"min_privacy_units_per_group",
      MakeResolvedLiteral(Value::Int64(1))));
  auto dp_scan_without_rewrite = MakeResolvedDifferentialPrivacyAggregateScan(
      /*column_list=*/{},
      /*input_scan=*/nullptr,
      /*group_by_list=*/{},
      /*aggregate_list=*/{},
      /*grouping_set_list=*/{},
      /*rollup_column_list=*/{},
      /*group_selection_threshold_expr=*/nullptr,
      /*option_list=*/std::move(option_list));

  EXPECT_THAT(
      FunctionEpsilonAssigner::CreateFromScan(dp_scan_without_rewrite.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("not rewritten by the anonymization rewriter")));
}

TEST_F(FunctionEpsilonAssignerTest,
       GetGroupSelectionEpsilonReturnsValueFromOptions) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::NullDouble()));
  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(
          function_calls, /*option_epsilon=*/Value::Double(2.2),
          /*option_group_selection_epsilon=*/Value::Double(0.3));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<FunctionEpsilonAssigner> assigner,
                       FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()));

  EXPECT_THAT(assigner->GetGroupSelectionEpsilon(), Optional(DoubleEq(0.3)));
}

TEST_F(FunctionEpsilonAssignerTest,
       GetGroupSelectionEpsilonReturnsValueFromGroupSelectionAggregation) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.3)));
  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(
          function_calls, /*option_epsilon=*/Value::Double(5.2),
          /*option_group_selection_epsilon=*/std::nullopt,
          /*group_selection_call=*/
          MakeFakeAggregateFunctionWithEpsilon(Value::Double(0.9)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<FunctionEpsilonAssigner> assigner,
                       FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()));

  EXPECT_THAT(assigner->GetGroupSelectionEpsilon(), Optional(DoubleEq(0.9)));
}

TEST_F(FunctionEpsilonAssignerTest,
       GetGroupSelectionEpsilonReturnsShareForEvenBudgetSplitting) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::NullDouble()));
  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(
          function_calls, /*option_epsilon=*/Value::Double(5.1),
          /*option_group_selection_epsilon=*/std::nullopt,
          /*group_selection_call=*/
          MakeFakeAggregateFunctionWithEpsilon(Value::NullDouble()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<FunctionEpsilonAssigner> assigner,
                       FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()));

  EXPECT_THAT(assigner->GetGroupSelectionEpsilon(), Optional(DoubleEq(2.0)));
}

TEST_F(FunctionEpsilonAssignerTest,
       GetGroupSelectionEpsilonReturnsNulloptWhenNoGroupSelectionSet) {
  std::vector<std::unique_ptr<const ResolvedAggregateFunctionCall>>
      function_calls;
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::Double(1.1)));
  function_calls.emplace_back(
      MakeFakeAggregateFunctionWithEpsilon(Value::NullDouble()));
  std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan =
      MakeFakeDpAggregateScan(function_calls,
                              /*option_epsilon=*/Value::Double(5.1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<FunctionEpsilonAssigner> assigner,
                       FunctionEpsilonAssigner::CreateFromScan(dp_scan.get()));

  EXPECT_THAT(assigner->GetGroupSelectionEpsilon(), Eq(std::nullopt));
}

}  // namespace
}  // namespace anonymization
}  // namespace googlesql
