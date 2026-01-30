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

#include "googlesql/public/numeric_value.h"

#include <stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/multiprecision_int.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_constants.h"
#include "googlesql/public/numeric_value_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/base/macros.h"
#include "absl/hash/hash_testing.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "googlesql/base/bits.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/mathutil.h"
#include "googlesql/base/status_macros.h"

namespace std {
std::ostream& operator<<(std::ostream& o, __int128 x) {
  return x < 0 ? (o << '-' << -absl::uint128(x)) : (o << absl::uint128(x));
}
}  // namespace std

namespace googlesql {

extern absl::StatusOr<NumericValue> TestOnlyNumericApproximateCbrt(
    NumericValue in);
extern absl::StatusOr<BigNumericValue> TestOnlyBigNumericApproximateCbrt(
    BigNumericValue in);

namespace {
constexpr int kintmax = std::numeric_limits<int>::max();
constexpr int kintmin = std::numeric_limits<int>::min();
constexpr uint32_t kuint32max = std::numeric_limits<uint32_t>::max();

using ::testing::Matcher;
using ::testing::MatchesRegex;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

constexpr absl::string_view kMinNumericValueStr =
    "-99999999999999999999999999999.999999999";
constexpr absl::string_view kMaxNumericValueStr =
    "99999999999999999999999999999.999999999";
constexpr __int128 kMinNumericValuePacked =
    NumericValue::MinValue().as_packed_int();
constexpr __int128 kMaxNumericValuePacked =
    NumericValue::MaxValue().as_packed_int();
constexpr __int128 k1e9 = 1000000000;
constexpr __int128 k1e10 = k1e9 * 10;
constexpr uint64_t kuint64max = std::numeric_limits<uint64_t>::max();
constexpr int64_t kint64min = std::numeric_limits<int64_t>::min();
constexpr int64_t kint64max = std::numeric_limits<int64_t>::max();
constexpr int32_t kint32min = std::numeric_limits<int32_t>::min();
constexpr int32_t kint32max = std::numeric_limits<int32_t>::max();
using uint128 = unsigned __int128;
constexpr uint128 kuint128max = ~static_cast<uint128>(0);
constexpr __int128 kint128max = kuint128max >> 1;
constexpr __int128 kint128min = ~kint128max;

void VerifyVariance(std::optional<double> expect_var,
                    std::optional<double> actual_var) {
  ASSERT_EQ(expect_var.has_value(), actual_var.has_value());
  if (expect_var.has_value()) {
    EXPECT_DOUBLE_EQ(expect_var.value(), actual_var.value());
  }
}

void VerifyStandardDeviation(std::optional<double> expect_var,
                             std::optional<double> actual_stddev) {
  ASSERT_EQ(expect_var.has_value(), actual_stddev.has_value());
  if (expect_var.has_value()) {
    EXPECT_DOUBLE_EQ(std::sqrt(expect_var.value()), actual_stddev.value());
  }
}

template <typename T>
void VerifyVarianceAggregator(const T& agg,
                              std::optional<double> expect_var_pop,
                              std::optional<double> expect_var_samp,
                              uint64_t count) {
  VerifyVariance(expect_var_pop, agg.GetPopulationVariance(count));
  VerifyStandardDeviation(expect_var_pop, agg.GetPopulationStdDev(count));
  VerifyVariance(expect_var_samp, agg.GetSamplingVariance(count));
  VerifyStandardDeviation(expect_var_samp, agg.GetSamplingStdDev(count));
}

template <typename T>
void VerifyCovariance(const T& agg, std::optional<double> expect_covar_pop,
                      std::optional<double> expect_covar_samp, uint64_t count) {
  VerifyVariance(expect_covar_pop, agg.GetPopulationCovariance(count));
  VerifyVariance(expect_covar_samp, agg.GetSamplingCovariance(count));
}

template <typename T>
void VerifyCorrelation(const T& agg, std::optional<double> expect,
                       uint64_t count) {
  std::optional<double> actual(agg.GetCorrelation(count));
  ASSERT_EQ(expect.has_value(), actual.has_value());
  if (expect.has_value()) {
    EXPECT_EQ(std::isnan(expect.value()), std::isnan(actual.value()));
    if (!std::isnan(expect.value())) {
      EXPECT_DOUBLE_EQ(expect.value(), actual.value());
    }
  }
}

class NumericValueTest : public testing::Test {
 protected:
  NumericValue MakeRandomNumeric() {
    return MakeRandomNumericValue<NumericValue>(&random_);
  }

  absl::BitGen random_;
};

static constexpr __int128 kNumericValidPackedValues[] = {
    0,
    -1,
    1,
    -10000,
    10000,
    kint64min,
    kint64max,
    kMinNumericValuePacked,
    kMaxNumericValuePacked};

TEST_F(NumericValueTest, FromPackedInt) {
  for (__int128 packed : kNumericValidPackedValues) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromPackedInt(packed));
    EXPECT_EQ(packed, value.as_packed_int());
    if (packed >= kint64min && packed <= kint64max) {
      value = NumericValue::FromScaledValue(static_cast<int64_t>(packed));
      EXPECT_EQ(packed, value.as_packed_int());
    }
  }

  static constexpr __int128 kInvalidValues[] = {
      kMinNumericValuePacked - 1,
      kMaxNumericValuePacked + 1,
      kint128min,
      kint128max,
  };
  for (__int128 packed : kInvalidValues) {
    EXPECT_THAT(NumericValue::FromPackedInt(packed),
                StatusIs(absl::StatusCode::kOutOfRange,
                         "numeric overflow: result out of range"));
  }
}

TEST_F(NumericValueTest, FromHighAndLowBits) {
  constexpr uint64_t max_high = NumericValue::MaxValue().high_bits();
  constexpr uint64_t max_low = NumericValue::MaxValue().low_bits();
  constexpr uint64_t min_high = NumericValue::MinValue().high_bits();
  constexpr uint64_t min_low = NumericValue::MinValue().low_bits();
  constexpr uint64_t kuint64max = std::numeric_limits<uint64_t>::max();
  static constexpr std::pair<uint64_t, uint64_t> kValidValues[] = {
      {0, 0},
      {0, 1},
      {0, kuint64max},
      {1, kuint64max},
      {max_high - 1, kuint64max},
      {max_high, 0},
      {max_high, max_low - 1},
      {max_high, max_low},
      {min_high, min_low},
      {min_high, min_low + 1},
      {min_high, kuint64max},
      {min_high + 1, 0},
      {kuint64max, 0},
      {kuint64max, kuint64max},
  };
  for (std::pair<uint64_t, uint64_t> bits : kValidValues) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, NumericValue::FromHighAndLowBits(
                                                 bits.first, bits.second));
    EXPECT_EQ(bits.first, value.high_bits());
    EXPECT_EQ(bits.second, value.low_bits());
  }

  static constexpr std::pair<uint64_t, uint64_t> kInvalidValues[] = {
      {max_high, max_low + 1}, {max_high, kuint64max},
      {max_high + 1, 0},       {max_high + 1, kuint64max},
      {min_high - 1, 0},       {min_high - 1, kuint64max},
      {min_high, 0},           {min_high, min_low - 1},
  };
  for (std::pair<uint64_t, uint64_t> bits : kInvalidValues) {
    EXPECT_THAT(NumericValue::FromHighAndLowBits(bits.first, bits.second),
                StatusIs(absl::StatusCode::kOutOfRange,
                         "numeric overflow: result out of range"));
  }
}

using NumericStringTestData = std::pair<absl::string_view, __int128>;
constexpr NumericStringTestData kSortedNumericValueStringPairs[] = {
    {kMinNumericValueStr, kMinNumericValuePacked},
    {"-9223372036854775808", std::numeric_limits<int64_t>::min() * k1e9},
    {"-123", -123 * k1e9},
    {"-12.345678901", -12345678901LL},
    {"-10", -10 * k1e9},
    {"-1.23456789", -1234567890LL},
    {"-1", -k1e9},
    {"-0.123456789", -123456789},
    {"-0.123", -123000000},
    {"-0.00000001", -10},
    {"-0.000000001", -1},
    {"0", 0},
    {"0.000000001", 1},
    {"0.00000001", 10},
    {"0.123", 123000000},
    {"0.123456789", 123456789},
    {"1", k1e9},
    {"1.23456789", 1234567890LL},
    {"10", 10 * k1e9},
    {"12.345678901", 12345678901LL},
    {"123", 123 * k1e9},
    {{"9997.", 4}, 9997 * k1e9},  // a regression test for b/77498186
    {"9223372036854775807", std::numeric_limits<int64_t>::max() * k1e9},
    {"18446744073709551615", std::numeric_limits<uint64_t>::max() * k1e9},
    {kMaxNumericValueStr, kMaxNumericValuePacked},
};

constexpr NumericStringTestData kNumericFromStringTestData[] = {
    {"00", 0},
    {"0.0", 0},
    {"00.000", 0},
    {"+123", 123 * k1e9},
    {"123.0", 123 * k1e9},
    {"123.", 123 * k1e9},
    {"+123.0", 123 * k1e9},
    {"-123.0", -123 * k1e9},
    {"+0.000000001", 1},
    {".123", 123000000},
    {"+.123", 123000000},
    {"-.123", -123000000},

    // The white space is ignored.
    {" 0", 0},
    {"0 ", 0},
    {" 0 ", 0},

    // Non-essential zeroes are ignored.
    {"00000000000000000000000000000000000000000000000000", 0},
    {"-00000000000000000000000000000000000000000000000000", 0},
    {"+00000000000000000000000000000000000000000000000000", 0},
    {".00000000000000000000000000000000000000000000000000", 0},
    {"-.00000000000000000000000000000000000000000000000000", 0},
    {"+.00000000000000000000000000000000000000000000000000", 0},
    {"00000000000000000000000000000000.0000000000000000000", 0},
    {"-00000000000000000000000000000000.0000000000000000000", 0},
    {"+00000000000000000000000000000000.0000000000000000000", 0},
    {"0000000000000000000000000000000123.340000000000000000000000",
     12334 * k1e9 / 100},
    {"+0000000000000000000000000000000123.340000000000000000000000",
     12334 * k1e9 / 100},
    {"-0000000000000000000000000000000123.340000000000000000000000",
     -12334 * k1e9 / 100},

    // Exponent form.
    {"123e3", 123000 * k1e9},
    {"123E3", 123000 * k1e9},
    {"123.e3", 123000 * k1e9},
    {"123e+3", 123000 * k1e9},
    {"123000e0", 123000 * k1e9},
    {"123000e00", 123000 * k1e9},
    {"123000E+00", 123000 * k1e9},
    {"1230000e-001", 123000 * k1e9},
    {"123e-3", 123000000},
    {"123E-3", 123000000},
    {"0000000000000000000000000123e3", 123000 * k1e9},
    {"0000000000000000000000000123.000000000000000e3", 123000 * k1e9},
    {"-0000000000000000000000000123e3", -123000 * k1e9},
    {"-0000000000000000000000000123.000000000000000e3", -123000 * k1e9},
    {".123e10", 1230000000 * k1e9},
    {"1.1234567890E10", 11234567890 * k1e9},
    {"0.12345678901234567890123456789012345678e+29",
     ((1234567890 * k1e10 + 1234567890) * k1e9 + 123456789) * k1e9 + 12345678},
    {"-0.12345678901234567890123456789012345678e+29",
     ((-1234567890 * k1e10 - 1234567890) * k1e9 - 123456789) * k1e9 - 12345678},
    {"12345678901234567890123456789012345678e-9",
     ((1234567890 * k1e10 + 1234567890) * k1e9 + 123456789) * k1e9 + 12345678},
    {"-12345678901234567890123456789012345678e-9",
     ((-1234567890 * k1e10 - 1234567890) * k1e9 - 123456789) * k1e9 - 12345678},
    {"0.999999999999999999999999999999999999990000000000000000000000000"
     "e0000000000000000000000000000000000000000000000000000000000000029",
     kMaxNumericValuePacked},
    {"-0.999999999999999999999999999999999999990000000000000000000000000"
     "E0000000000000000000000000000000000000000000000000000000000000029",
     kMinNumericValuePacked},
    {"0.99999999999999999999999999999999999999e+29", kMaxNumericValuePacked},
    {"-0.99999999999999999999999999999999999999e+29", kMinNumericValuePacked},
    {"9999999999999999999999999999999999999900000000000000000000e-29",
     kMaxNumericValuePacked},
    {"-9999999999999999999999999999999999999900000000000000000000e-29",
     kMinNumericValuePacked},
    // exponent is small negative integer while coefficient is 0
    {"0.0E-122337203655840000", 0},
    {"-0.0E-122337203655840000", 0},
    {"0.00000000000000000000000000000000000000000000E-122337203655840000", 0},
    {"-0.00000000000000000000000000000000000000000000E-122337203655840000", 0},
    {"0.0E-99999999999999999999999999999999999999999999999999999999999", 0},
    {"-0.0E-99999999999999999999999999999999999999999999999999999999999", 0},
    // exponent below int64min
    {"0E-99999999999999999999999999999999999999999999999999999999999", 0},
    {"-0.00000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999",
     0},
    {"+000000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999",
     0},
    {"   -.00000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999   ",
     0},
    // exponent is large positive integer while coefficient is 0
    {"0.0E122337203655840000", 0},
    {"-0.0E122337203655840000", 0},
    {"0.00000000000000000000000000000000000000000000E122337203655840000", 0},
    {"-0.00000000000000000000000000000000000000000000E122337203655840000", 0},
    {"0.0E99999999999999999999999999999999999999999999999999999999999", 0},
    {"-0.0E99999999999999999999999999999999999999999999999999999999999", 0},
};

constexpr NumericStringTestData kNumericFromStringRoundingTestData[] = {
    {"0.1234567891", 123456789},
    {"0.123456789123456789", 123456789},
    {"0.1234567894", 123456789},
    {"0.1234567885", 123456789},
    {"0.123456788555", 123456789},
    {"0.1234567889", 123456789},
    {"-0.1234567891", -123456789},
    {"-0.123456789123456789", -123456789},
    {"-0.1234567894", -123456789},
    {"-0.1234567885", -123456789},
    {"-0.123456788555", -123456789},
    {"-0.1234567889", -123456789},

    {"99999999999999999999999999999.9999999991", kMaxNumericValuePacked},
    {"-99999999999999999999999999999.9999999991", kMinNumericValuePacked},

    // More tests for the exponential form.
    {"1111111111111111111111111111111111111111111111111111111111111111111e-66",
     1111111111},
    {".123e-10", 0},
    {".123e-9999", 0},
    {"123456789012345678901234567890123456789e-10",
     ((1234567890 * k1e10 + 1234567890) * k1e9 + 123456789) * k1e9 + 12345679},
    {"-123456789012345678901234567890123456789e-10",
     ((-1234567890 * k1e10 - 1234567890) * k1e9 - 123456789) * k1e9 - 12345679},
    // exponent below int64min
    {"1E-99999999999999999999999999999999999999999999999999999999999", 0},
    {"-1E-99999999999999999999999999999999999999999999999999999999999", 0},
    {"99999999999999999999999999999999999999999999999999999999999"
     "E-99999999999999999999999999999999999999999999999999999999999",
     0},
    {"-99999999999999999999999999999999999999999999999999999999999"
     "E-99999999999999999999999999999999999999999999999999999999999",
     0},
};

constexpr absl::string_view kInvalidNumericStrings[] = {
    "999999999999999999999999999999",
    "-999999999999999999999999999999",
    "100000000000000000000000000000",
    "-100000000000000000000000000000",
    "266666666666666666666666666666",
    "-266666666666666666666666666666",
    "26666666666666666666666666666600",
    "-26666666666666666666666666666600",
    "",
    "abcd",
    "- 123",
    "123abc",
    "123..456",
    "123.4.56",
    ".",
    "  ",
    "+",
    "-",
    "++1",
    "--1",
    "+-1",
    "-+1",
    "123e5.6",
    "345e+ 4",
    "123e",
    "e",
    "123e-",
    "123e+",
    "123e-+1",
    "123e+-1",
    "123ee+1",
    "123ee-1",
    "123e +1",
    "123e -1",
    ".e",
    "123e-9999e",
    "123e9999e",
    "123e99999999",
    "170141183460469231731687303715884105727",     // kint128max
    "170141183460469231731687303715884105727.0",   // kint128max
    "-170141183460469231731687303715884105728",    // kint128min
    "-170141183460469231731687303715884105728.0",  // kint128min
    // Exponent overflows.
    "1e9223372036854775808",
    "1e9223372036854775800",  // overflows when adding 9 to the exponent
    "0e-9999999999999999999999999999999999999999999999999ABC",
};

TEST_F(NumericValueTest, ToString) {
  for (const NumericStringTestData& pair : kSortedNumericValueStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromPackedInt(pair.second));
    EXPECT_EQ(pair.first, value.ToString());
    static constexpr char kExistingValue[] = "existing_value_1234567890";
    std::string str = kExistingValue;
    value.AppendToString(&str);
    EXPECT_EQ(absl::StrCat(kExistingValue, pair.first), str);
  }
}

TEST_F(NumericValueTest, FromString) {
  using FromStringFunc =
      std::function<absl::StatusOr<NumericValue>(absl::string_view)>;
  const FromStringFunc functions[] = {&NumericValue::FromStringStrict,
                                      &NumericValue::FromString};
  for (const auto& from_string : functions) {
    // Common successful cases under strict and non-strict parsing.
    for (const NumericStringTestData& pair : kSortedNumericValueStringPairs) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, from_string(pair.first));
      EXPECT_EQ(pair.second, value.as_packed_int()) << pair.first;
    }
    for (const NumericStringTestData& pair : kNumericFromStringTestData) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, from_string(pair.first));
      EXPECT_EQ(pair.second, value.as_packed_int()) << pair.first;
    }

    // Test common failures for the strict and non-strict parsing.
    for (const absl::string_view input : kInvalidNumericStrings) {
      EXPECT_THAT(from_string(input),
                  StatusIs(absl::StatusCode::kOutOfRange,
                           absl::StrCat("Invalid NUMERIC value: ", input)));
    }
  }

  for (const NumericStringTestData& pair : kNumericFromStringRoundingTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromString(pair.first));
    EXPECT_EQ(pair.second, value.as_packed_int()) << pair.first;

    EXPECT_THAT(NumericValue::FromStringStrict(pair.first),
                StatusIs(absl::StatusCode::kOutOfRange,
                         absl::StrCat("Invalid NUMERIC value: ", pair.first)));
  }
}

// A lite version of Status that allows instantiation with constexpr.
struct Error : absl::string_view {
  constexpr explicit Error(absl::string_view message_prefix)
      : absl::string_view(message_prefix) {}
};

constexpr Error kNumericInvalid("Invalid NUMERIC value: ");
constexpr Error kNumericOverflow("numeric overflow: ");
constexpr Error kNumericOutOfRange("numeric out of range: ");
constexpr Error kDivisionByZero("division by zero: ");
constexpr Error kNumericIllegalNonFinite(
    "Illegal conversion of non-finite floating point number to numeric: ");
constexpr Error kNumericFromScaledValueOutOfRange(
    "Value is out of range after scaling to NUMERIC type");
constexpr Error kNumericFromScaledValueRoundingNotAllowed(
    "Value will lose precision after scaling down to NUMERIC type");
constexpr Error kRescaleValueRoundingNotAllowed(
    "Value will lose precision after scaling down to a scale of ");
constexpr Error kNumericIllegalScale(
    "NUMERIC scale must be between 0 and 9 but got ");

// A lite version of StatusOr that allows instantiation with constexpr.
template <typename T>
using ErrorOr = std::variant<Error, T>;

struct NumericValueWrapper : std::variant<Error, absl::string_view, int64_t> {
  using Base = std::variant<Error, absl::string_view, int64_t>;
  using Base::Base;
  NumericValueWrapper(const NumericValueWrapper& src, bool negate)
      : Base(src), negate(negate ^ src.negate) {}

  bool negate = false;
};

template <typename T>
absl::StatusOr<T> GetNumericValue(const NumericValueWrapper& src) {
  T value;
  if (std::holds_alternative<absl::string_view>(src)) {
    GOOGLESQL_ASSIGN_OR_RETURN(value, T::FromString(std::get<absl::string_view>(src)));
  } else if (std::holds_alternative<int64_t>(src)) {
    value = T(std::get<int64_t>(src));
  } else {
    return MakeEvalError() << std::get<Error>(src);
  }
  if (src.negate) {
    return value.Negate();
  }
  return value;
}

// Defines a different type so that GetValue(BigNumericValueWrapper) returns
// BigNumericValue.
struct BigNumericValueWrapper : NumericValueWrapper {
  using NumericValueWrapper::NumericValueWrapper;
};

NumericValueWrapper operator-(const NumericValueWrapper& src) {
  NumericValueWrapper result = src;
  result.negate = !result.negate;
  return result;
}
BigNumericValueWrapper operator-(const BigNumericValueWrapper& src) {
  BigNumericValueWrapper result = src;
  result.negate = !result.negate;
  return result;
}

template <typename T>
absl::StatusOr<T> GetValue(const T& src) {
  return src;
}

template <typename T>
absl::StatusOr<T> GetValue(const ErrorOr<T>& src) {
  if (std::holds_alternative<T>(src)) {
    return std::get<T>(src);
  }
  return MakeEvalError() << std::get<Error>(src);
}

template <typename T>
absl::StatusOr<T> GetValue(const absl::StatusOr<T>& src) {
  return src;
}

absl::StatusOr<NumericValue> GetValue(const NumericValueWrapper& src) {
  return GetNumericValue<NumericValue>(src);
}

absl::StatusOr<std::pair<NumericValue, NumericValue>> GetValue(
    const std::pair<NumericValueWrapper, NumericValueWrapper>& src) {
  GOOGLESQL_ASSIGN_OR_RETURN(NumericValue first, GetValue(src.first));
  GOOGLESQL_ASSIGN_OR_RETURN(NumericValue second, GetValue(src.second));
  return std::make_pair(first, second);
}

absl::StatusOr<BigNumericValue> GetValue(const BigNumericValueWrapper& src) {
  return GetNumericValue<BigNumericValue>(src);
}

absl::StatusOr<std::pair<BigNumericValue, BigNumericValue>> GetValue(
    const std::pair<BigNumericValueWrapper, BigNumericValueWrapper>& src) {
  GOOGLESQL_ASSIGN_OR_RETURN(BigNumericValue first, GetValue(src.first));
  GOOGLESQL_ASSIGN_OR_RETURN(BigNumericValue second, GetValue(src.second));
  return std::make_pair(first, second);
}

constexpr Error kBigNumericInvalid("Invalid BIGNUMERIC value: ");
constexpr Error kBigNumericOverflow("BIGNUMERIC overflow: ");
constexpr Error kBigNumericOutOfRange("BIGNUMERIC out of range: ");
constexpr Error kBigNumericIllegalNonFinite(
    "Illegal conversion of non-finite floating point number to BIGNUMERIC: ");
constexpr Error kBigNumericFromScaledValueOutOfRange(
    "Value is out of range after scaling to BIGNUMERIC type");
constexpr Error kBigNumericFromScaledValueRoundingNotAllowed(
    "Value will lose precision after scaling down to BIGNUMERIC type");
constexpr Error kBigNumericIllegalScale(
    "BIGNUMERIC scale must be between 0 and 38 but got ");

template <typename Input = BigNumericValueWrapper,
          typename Output = BigNumericValueWrapper>
struct BigNumericUnaryOpTestData {
  Input input;
  Output expected_output;
};

template <typename Input2 = BigNumericValueWrapper,
          typename Output = BigNumericValueWrapper>
struct BigNumericBinaryOpTestData {
  BigNumericValueWrapper input1;
  Input2 input2;
  Output expected_output;
};

struct BigNumericFromStringWithRoundingOpTestData {
  absl::string_view input1;
  int64_t input2;
  BigNumericValueWrapper expected_output;
};

// Returns a value that can be used in absl::StrCat.
template <typename T>
const T& AlphaNum(const T& src) {
  return src;
}

std::string AlphaNum(const NumericValue& src) { return src.ToString(); }
std::string AlphaNum(const BigNumericValue& src) { return src.ToString(); }

template <typename T, typename Int>
absl::StatusOr<T> ScaleBy(Int mantissa, int exp) {
  std::ostringstream oss;
  if constexpr (std::is_same_v<Int, FixedInt<64, 4>>) {
    oss << mantissa.ToString() << "e" << exp;
  } else {
    oss << mantissa << "e" << exp;
  }
  return T::FromString(oss.str());
}

template <typename Int>
Int Uniform(absl::BitGen* random) {
  if constexpr (sizeof(Int) == sizeof(__int128)) {
    unsigned __int128 hi = absl::Uniform<uint64_t>(*random);
    unsigned __int128 lo = absl::Uniform<uint64_t>(*random);
    return (hi << 64) | lo;
  } else {
    return absl::Uniform<std::make_unsigned_t<Int>>(*random);
  }
}

template <typename NumericType, typename SmallInt, typename BigInt>
void TestMultiplication(absl::BitGen* random) {
  for (int i = 0; i < 1000; ++i) {
    SmallInt mantissa_x = Uniform<SmallInt>(random);
    SmallInt mantissa_y = Uniform<SmallInt>(random);
    BigInt x = static_cast<BigInt>(mantissa_x);
    BigInt y = static_cast<BigInt>(mantissa_y);
    EXPECT_THAT(NumericType(x).Multiply(NumericType(y)),
                IsOkAndHolds(NumericType(x * y)));

    int exp_x = absl::Uniform<int32_t>(*random, -38, 1);
    int exp_y = absl::Uniform<int32_t>(*random, -38, -37 - exp_x);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType numeric_x,
                         ScaleBy<NumericType>(mantissa_x, exp_x));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType numeric_y,
                         ScaleBy<NumericType>(mantissa_y, exp_y));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType expected_product,
                         ScaleBy<NumericType>(x * y, exp_x + exp_y));
    EXPECT_THAT(numeric_x.Multiply(numeric_y), IsOkAndHolds(expected_product));
  }
}

// Rounding not supported.
template <typename NumericType>
void VerifyExactProduct(NumericType x, NumericType y,
                        NumericType expected_product) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType product, x.Multiply(y));
  EXPECT_EQ(expected_product, product);
  EXPECT_THAT(product.Divide(y), IsOkAndHolds(x));
  EXPECT_THAT(product.Divide(x), IsOkAndHolds(y));
}

template <typename NumericType, typename BigInt, typename SmallInt>
void TestMultiplyAndDivide(SmallInt mantissa_x, int exp_x, SmallInt mantissa_y,
                           int exp_y) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType numeric_x,
                       ScaleBy<NumericType>(mantissa_x, exp_x));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType numeric_y,
                       ScaleBy<NumericType>(mantissa_y, exp_y));
  BigInt mantissa_x_times_y(mantissa_x);
  mantissa_x_times_y *= BigInt(mantissa_y);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericType expected_product,
                       ScaleBy<NumericType>(mantissa_x_times_y, exp_x + exp_y));
  VerifyExactProduct(numeric_x, numeric_y, expected_product);
}

// This does not test rounding or overflow.
template <typename NumericType, typename SmallInt, typename BigInt,
          typename HugeInt>
void TestMultiplicationDivisionRoundTrip(absl::BitGen* random) {
  for (int i = 0; i < 1000; ++i) {
    SmallInt mantissa_x = Uniform<SmallInt>(random);
    SmallInt mantissa_y = Uniform<SmallInt>(random);
    BigInt x = static_cast<BigInt>(mantissa_x);
    BigInt y = static_cast<BigInt>(mantissa_y);
    VerifyExactProduct(NumericType(x), NumericType(y), NumericType(x * y));

    // Test small fractional numbers.
    constexpr int kScale = NumericType::kMaxFractionalDigits;
    int exp_x = absl::Uniform<int32_t>(*random, -kScale, 1);
    int exp_y = absl::Uniform<int32_t>(*random, -kScale - exp_x, 1);
    TestMultiplyAndDivide<NumericType, BigInt>(mantissa_x, exp_x, mantissa_y,
                                               exp_y);

    // Test big fractional numbers whose product can be close to the max
    // supported value. exp_x + exp_y must be -kScale to avoid overflow.
    // The max possible value of the product is 0x40... / pow(10, kScale).
    BigInt big_mantissa_x = Uniform<BigInt>(random);
    BigInt big_mantissa_y = Uniform<BigInt>(random);
    exp_y = -kScale - exp_x;
    TestMultiplyAndDivide<NumericType, HugeInt>(big_mantissa_x, exp_x,
                                                big_mantissa_y, exp_y);
  }
}

template <class T>
struct FromScaledLittleEndianValueOp {
  absl::StatusOr<T> operator()(absl::string_view input, int logical_types_scale,
                               bool allow_rounding,
                               bool round_half_even) const {
    return T::FromScaledLittleEndianValue(input, logical_types_scale,
                                          T::kMaxFractionalDigits,
                                          allow_rounding, round_half_even);
  }
};

template <typename Input = NumericValueWrapper,
          typename Output = NumericValueWrapper>
struct NumericUnaryOpTestData {
  Input input;
  Output expected_output;
};

template <typename Input2 = NumericValueWrapper,
          typename Output = NumericValueWrapper>
struct NumericBinaryOpTestData {
  NumericValueWrapper input1;
  Input2 input2;
  Output expected_output;
};

struct NumericFromStringWithRoundingOpTestData {
  absl::string_view input1;
  int64_t input2;
  NumericValueWrapper expected_output;
};

struct NumericAddOp {
  template <class T>
  absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Add(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 + $1";
};

struct NumericSubtractOp {
  template <class T>
  absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Subtract(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 - $1";
};

struct NumericMultiplyOp {
  template <class T>
  absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Multiply(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 * $1";
};

struct NumericDivideOp {
  template <class T>
  absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Divide(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 / $1";
};

struct NumericModOp {
  template <class T>
  absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Mod(y);
  }
  static constexpr absl::string_view kExpressionFormat = "MOD($0, $1)";
};

struct NumericDivideToIntegralValueOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.DivideToIntegralValue(y);
  }
  static constexpr absl::string_view kExpressionFormat = "DIV($0, $1)";
};

struct NumericPowerOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Power(y);
  }
  static constexpr absl::string_view kExpressionFormat = "POW($0, $1)";
};

struct NumericExpOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Exp();
  }
  static constexpr absl::string_view kExpressionFormat = "EXP($0)";
};

struct NumericLnOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Ln();
  }
  static constexpr absl::string_view kExpressionFormat = "LN($0)";
};

struct NumericLog10Op {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Log10();
  }
  static constexpr absl::string_view kExpressionFormat = "LOG10($0)";
};

struct NumericLogOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Log(y);
  }
  static constexpr absl::string_view kExpressionFormat = "LOG($0, $1)";
};

struct NumericSqrtOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Sqrt();
  }
  static constexpr absl::string_view kExpressionFormat = "SQRT($0)";
};

struct NumericCbrtOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Cbrt();
  }
  static constexpr absl::string_view kExpressionFormat = "CBRT($0)";
};

struct NumericTruncOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x, int64_t y) const {
    return x.Trunc(y);
  }
  static constexpr absl::string_view kExpressionFormat = "TRUNC($0, $1)";
};

struct NumericRoundHalfEvenOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x, int64_t y) const {
    return x.Round(y, true);
  }
  static constexpr absl::string_view kExpressionFormat = "ROUND($0, $1)";
};

struct NumericRoundHalfAwayFromZeroOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x, int64_t y) const {
    return x.Round(y, false);
  }
  static constexpr absl::string_view kExpressionFormat = "ROUND($0, $1)";
};

struct NumericFloorOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Floor();
  }
  static constexpr absl::string_view kExpressionFormat = "FLOOR($0)";
};

struct NumericCeilingOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& x) const {
    return x.Ceiling();
  }
  static constexpr absl::string_view kExpressionFormat = "CEIL($0)";
};

struct NumericSignOp {
  template <class T>
  inline int operator()(const T& x) const {
    return x.Sign();
  }
  static constexpr absl::string_view kExpressionFormat = "SIGN($0)";
};

struct NumericAbsOp {
  template <class T>
  inline auto operator()(const T& x) const {
    return x.Abs();
  }
  static constexpr absl::string_view kExpressionFormat = "ABS($0)";
};

template <typename T>
struct NumericFromDoubleOp {
  absl::StatusOr<T> operator()(double operand) const {
    return T::FromDouble(operand);
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

struct NumericToDoubleOp {
  template <typename T>
  double operator()(const T& operand) const {
    return operand.ToDouble();
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

template <typename T>
struct CumulativeSumOp {
  absl::StatusOr<T> operator()(const T& x) {
    aggregator.Add(x);
    return aggregator.GetSum();
  }
  static constexpr absl::string_view kExpressionFormat = "SUM";

  typename T::SumAggregator aggregator;
};

template <typename T>
struct CumulativeSubtractOp {
  absl::StatusOr<T> operator()(const T& x) {
    aggregator.Subtract(x);
    return aggregator.GetSum();
  }
  static constexpr absl::string_view kExpressionFormat = "SUM";

  typename T::SumAggregator aggregator;
};

template <typename T>
struct CumulativeAverageOp {
  absl::StatusOr<T> operator()(const T& x) {
    aggregator.Add(x);
    ++count;
    return aggregator.GetAverage(count);
  }
  static constexpr absl::string_view kExpressionFormat = "AVG";

  typename T::SumAggregator aggregator;
  uint64_t count = 0;
};

struct BigNumericToNumericOp {
  absl::StatusOr<NumericValue> operator()(
      const BigNumericValue& operand) const {
    return operand.ToNumericValue();
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

struct NumericToBigNumericOp {
  BigNumericValue operator()(const NumericValue& operand) const {
    return BigNumericValue(operand);
  }
};

template <typename Output>
struct NumericToIntegerOp {
  template <typename T>
  absl::StatusOr<Output> operator()(const T& operand) const {
    return operand.template To<Output>();
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

struct NumericFormatOp {
  template <typename T>
  std::string operator()(const T& operand,
                         NumericValue::FormatSpec spec) const {
    std::string result;
    operand.FormatAndAppend(spec, &result);
    return result;
  }
};

template <bool allow_rounding>
struct NumericRescaleOp {
  template <class T>
  inline absl::StatusOr<T> operator()(const T& input_val, int scale) const {
    return input_val.Rescale(scale, allow_rounding);
  }
  static constexpr absl::string_view kExpressionFormat = "$1";
};

template <typename Op, typename Input, typename Output>
void TestUnaryOp(Op& op, const Input& input_wrapper,
                 const Output& expected_output) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input, GetValue(input_wrapper));
  auto status_or_result = GetValue(op(input));
  auto status_or_expected_output = GetValue(expected_output);
  if (status_or_expected_output.ok()) {
    EXPECT_THAT(status_or_result,
                IsOkAndHolds(status_or_expected_output.value()))
        << absl::Substitute(Op::kExpressionFormat, AlphaNum(input));
  } else {
    std::string expression =
        absl::Substitute(Op::kExpressionFormat, AlphaNum(input));
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(absl::StatusCode::kOutOfRange,
                 absl::StrCat(status_or_expected_output.status().message(),
                              expression)));
  }
}

template <typename Op, typename Input1, typename Input2, typename Output>
void TestBinaryOp(Op& op, const Input1& input_wrapper1,
                  const Input2& input_wrapper2, const Output& expected_output) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input1, GetValue(input_wrapper1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input2, GetValue(input_wrapper2));
  auto status_or_result = GetValue(op(input1, input2));
  auto status_or_expected_output = GetValue(expected_output);
  if (status_or_expected_output.ok()) {
    EXPECT_THAT(status_or_result,
                IsOkAndHolds(status_or_expected_output.value()))
        << absl::Substitute(Op::kExpressionFormat, AlphaNum(input1),
                            AlphaNum(input2));
  } else {
    std::string expression = absl::Substitute(
        Op::kExpressionFormat, AlphaNum(input1), AlphaNum(input2));
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(absl::StatusCode::kOutOfRange,
                 absl::StrCat(status_or_expected_output.status().message(),
                              expression)));
  }
}

template <typename Output>
void TestNumericFromStringWithRoundingOp(absl::string_view input1,
                                         const int64_t decimal_places,
                                         const bool round_half_even,
                                         const Output& expected_output) {
  auto status_or_result = NumericValue::FromStringWithRounding(
      input1, decimal_places, round_half_even);
  auto status_or_expected_output = GetValue(expected_output);
  if (status_or_expected_output.ok()) {
    EXPECT_EQ(status_or_expected_output.value(), status_or_result.value());
  } else {
    EXPECT_THAT(
        status_or_result.status().message(),
        absl::StrCat(status_or_expected_output.status().message(), input1));
  }
}

template <typename Output>
void TestBigNumericFromStringWithRoundingOp(absl::string_view input1,
                                            const int64_t decimal_places,
                                            const bool round_half_even,
                                            const Output& expected_output) {
  auto status_or_result = BigNumericValue::FromStringWithRounding(
      input1, decimal_places, round_half_even);
  auto status_or_expected_output = GetValue(expected_output);
  if (status_or_expected_output.ok()) {
    EXPECT_EQ(status_or_expected_output.value(), status_or_result.value());
  } else {
    EXPECT_THAT(
        status_or_result.status().message(),
        absl::StrCat(status_or_expected_output.status().message(), input1));
  }
}

void TestNumericMultiplyAndDivideByPowerOfTwoOp(
    const NumericValueWrapper& num_wrapper, const FixedInt<64, 2>& mult,
    uint scale_bits, const NumericValueWrapper& expected_output_wrapper) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto num, GetValue(num_wrapper));
  auto status_or_expected_output = GetValue(expected_output_wrapper);
  auto status_or_result =
      GetValue(num.MultiplyAndDivideByPowerOfTwo(mult, scale_bits));
  if (status_or_expected_output.ok()) {
    EXPECT_THAT(status_or_result,
                IsOkAndHolds(status_or_expected_output.value()))
        << absl::Substitute("$0 * $1 / pow(2, $2)", AlphaNum(num),
                            mult.ToString(), scale_bits);
  } else {
    std::string expression = absl::Substitute(
        "$0 * $1 / pow(2, $2)", AlphaNum(num), mult.ToString(), scale_bits);
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(absl::StatusCode::kOutOfRange,
                 absl::StrCat(status_or_expected_output.status().message(),
                              expression)));
  }
}

void TestBigNumericMultiplyAndDivideByPowerOfTwoOp(
    const BigNumericValueWrapper& num_wrapper, const FixedInt<64, 4>& mult,
    uint scale_bits, const BigNumericValueWrapper& expected_output_wrapper) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto num, GetValue(num_wrapper));
  auto status_or_expected_output = GetValue(expected_output_wrapper);
  auto status_or_result =
      GetValue(num.MultiplyAndDivideByPowerOfTwo(mult, scale_bits));
  if (status_or_expected_output.ok()) {
    EXPECT_THAT(status_or_result,
                IsOkAndHolds(status_or_expected_output.value()))
        << absl::Substitute("$0 * $1 / pow(2, $2)", AlphaNum(num),
                            mult.ToString(), scale_bits);
  } else {
    std::string expression = absl::Substitute(
        "$0 * $1 / pow(2, $2)", AlphaNum(num), mult.ToString(), scale_bits);
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(absl::StatusCode::kOutOfRange,
                 absl::StrCat(status_or_expected_output.status().message(),
                              expression)));
  }
}

template <typename NumericType, typename IntegerType>
void TestRoundTripFromInteger(absl::BitGen* random) {
  for (int i = 0; i < 1000; ++i) {
    IntegerType from_integer = Uniform<IntegerType>(random);
    NumericType numeric_value(from_integer);
    EXPECT_THAT(numeric_value.template To<IntegerType>(),
                IsOkAndHolds(from_integer))
        << numeric_value.ToString();
  }
}

template <typename T, typename V, size_t n>
void TestComparisonOperators(
    const std::pair<absl::string_view, V> (&sorted_str_test_data)[n]) {
  std::vector<T> values(n);
  for (size_t i = 0; i < n; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(values[i],
                         T::FromString(sorted_str_test_data[i].first));
  }
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      EXPECT_EQ(values[i] == values[j], i == j);
      EXPECT_EQ(values[i] != values[j], i != j);
      EXPECT_EQ(values[i] < values[j], i < j);
      EXPECT_EQ(values[i] > values[j], i > j);
      EXPECT_EQ(values[i] <= values[j], i <= j);
      EXPECT_EQ(values[i] >= values[j], i >= j);
#ifdef __cpp_impl_three_way_comparison
      EXPECT_EQ(values[i] <=> values[j], i <=> j);
#endif
    }
  }
}

template <typename T, typename V, size_t n>
void TestHashCode(const std::pair<absl::string_view, V> (&str_test_data)[n]) {
  std::vector<T> values(n);
  for (size_t i = 0; i < n; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(values[i], T::FromString(str_test_data[i].first));
  }
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(values));
}

template <typename T>
void TestSerialize(const T& value) {
  std::string bytes = value.SerializeAsProtoBytes();
  EXPECT_THAT(T::DeserializeFromProtoBytes(bytes), IsOkAndHolds(value));

  absl::string_view kExistingValue = "existing_value";
  bytes = kExistingValue;
  value.SerializeAndAppendToProtoBytes(&bytes);
  absl::string_view bytes_view = bytes;
  ASSERT_TRUE(absl::StartsWith(bytes_view, kExistingValue)) << bytes_view;
  bytes_view.remove_prefix(kExistingValue.size());
  EXPECT_THAT(T::DeserializeFromProtoBytes(bytes_view), IsOkAndHolds(value));
}

template <typename Aggregator, typename Input>
void AddToAggregator(const Input input, Aggregator* aggregator) {
  aggregator->Add(input);
}

template <typename Aggregator, typename Input>
void AddToAggregator(const std::pair<Input, Input> input,
                     Aggregator* aggregator) {
  aggregator->Add(input.first, input.second);
}

template <typename Aggregator, typename ValueWrapper, int kNumInputs>
void TestAggregatorSerialization(const ValueWrapper (&test_data)[kNumInputs]) {
  // aggregators[j][k] is the sum of the inputs with indexes in [j, k).
  Aggregator aggregators[kNumInputs + 1][kNumInputs + 1];
  for (int i = 0; i < kNumInputs; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input, GetValue(test_data[i]));
    // Emit the i-th input to aggregators[j][k] iff j <= i < k.
    for (int j = 0; j <= i; ++j) {
      for (int k = i + 1; k <= kNumInputs; ++k) {
        AddToAggregator(input, &aggregators[j][k]);
      }
    }
  }
  for (int j = 0; j < kNumInputs; ++j) {
    for (int k = j; k <= kNumInputs; ++k) {
      TestSerialize(aggregators[j][k]);
    }
  }

  constexpr absl::string_view kInvalidSerializedAggregators[] = {
      "",
      "A VERY VERY VERY VERY VERY VERY VERY VERY VERYLONG INVALID ENCODING",
  };
  for (absl::string_view string_view : kInvalidSerializedAggregators) {
    EXPECT_THAT(Aggregator::DeserializeFromProtoBytes(string_view),
                StatusIs(absl::StatusCode::kOutOfRange,
                         testing::MatchesRegex(
                             "Invalid "
                             "(Big)?NumericValue::(Sum|Variance|Covariance|"
                             "Correlation)?Aggregator encoding")));
  }
}

template <typename Aggregator, typename ValueWrapper, int kNumInputs>
void TestAggregatorMergeWith(const ValueWrapper (&test_data)[kNumInputs]) {
  // aggregators[j][k] is the sum of the inputs with indexes in [j, k).
  Aggregator aggregators[kNumInputs + 1][kNumInputs + 1];
  for (int i = 0; i < kNumInputs; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input, GetValue(test_data[i]));
    // Emit the i-th input to aggregators[j][k] iff j <= i < k.
    for (int j = 0; j <= i; ++j) {
      for (int k = i + 1; k <= kNumInputs; ++k) {
        AddToAggregator(input, &aggregators[j][k]);
      }
    }
  }
  for (int j = 0; j < kNumInputs; ++j) {
    for (int k = j; k <= kNumInputs; ++k) {
      for (int m = k; m <= kNumInputs; ++m) {
        // Verify aggregators[j][k] + aggregators[k][m] = aggregators[j][m].
        Aggregator aggregator = aggregators[j][k];
        aggregator.MergeWith(aggregators[k][m]);
        EXPECT_EQ(aggregators[j][m], aggregator) << j << ", " << k << ", " << m;
        aggregator = aggregators[k][m];
        aggregator.MergeWith(aggregators[j][k]);
        EXPECT_EQ(aggregators[j][m], aggregator) << j << ", " << k << ", " << m;
      }
    }
  }
}

template <typename T>
void TestFromScaledValue(absl::string_view bytes, int source_scale,
                         int fractional_digits_to_keep, bool allow_rounding,
                         bool round_half_even,
                         const absl::StatusOr<T>& expected_output) {
  auto status_or_result = T::FromScaledLittleEndianValue(
      bytes, source_scale, fractional_digits_to_keep, allow_rounding,
      round_half_even);
  if (expected_output.ok()) {
    EXPECT_THAT(status_or_result, IsOkAndHolds(expected_output.value()))
        << absl::Substitute(
               "input value: \"$0\", scale: $1, "
               "fractional_digits_to_keep: $2, "
               "allow_rounding: $3, "
               "round_half_even: $4",
               absl::BytesToHexString(bytes), source_scale,
               fractional_digits_to_keep, allow_rounding, round_half_even);
  } else {
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(absl::StatusCode::kOutOfRange,
                 absl::StrCat(expected_output.status().message(),
                              absl::Substitute("; input length: $0; scale: $1",
                                               bytes.size(), source_scale))));
  }
}

template <typename ValueWrapper>
void TestFromScaledValue(absl::string_view input, int source_scale,
                         int fractional_digits_to_keep, bool allow_rounding,
                         bool round_half_even,
                         const ValueWrapper& expected_output,
                         bool test_with_added_scale = true) {
  auto status_or_expected_output = GetValue(expected_output);
  if (input.empty()) {
    TestFromScaledValue(input, source_scale, fractional_digits_to_keep,
                        allow_rounding, round_half_even,
                        status_or_expected_output);
    return;
  }
  FixedInt<64, 8> input_value;
  ASSERT_TRUE(input_value.ParseFromStringStrict(input)) << input;
  bool negated_expected_value_overflows = false;
  if constexpr (std::is_same_v<ValueWrapper, BigNumericValueWrapper>) {
    negated_expected_value_overflows =
        status_or_expected_output.ok() &&
        status_or_expected_output.value() == BigNumericValue::MinValue();
  }
  for (bool negated : {false, true}) {
    SCOPED_TRACE(absl::StrCat("input: ", input, " negated: ", negated));
    if (negated) {
      input_value = -input_value;
      if (negated_expected_value_overflows) {
        status_or_expected_output =
            GetValue(ValueWrapper(kBigNumericFromScaledValueOutOfRange));
      } else {
        status_or_expected_output = GetValue(-expected_output);
      }
    }
    std::string bytes;
    input_value.SerializeToBytes(&bytes);
    TestFromScaledValue(bytes, source_scale, fractional_digits_to_keep,
                        allow_rounding, round_half_even,
                        status_or_expected_output);

    if (!test_with_added_scale) {
      continue;
    }
    for (int extra_scale : {1, 2, 5, 9, 38, 76, 100}) {
      if (static_cast<int64_t>(extra_scale) + source_scale > kintmax) break;
      FixedInt<64, 8> extra_scaled_value = input_value;
      if (extra_scaled_value.MultiplyOverflow(
              FixedInt<64, 8>::PowerOf10(extra_scale))) {
        break;
      }
      SCOPED_TRACE(absl::StrCat("extra_scale: ", extra_scale));
      bytes.clear();
      extra_scaled_value.SerializeToBytes(&bytes);
      // Append some redundant bytes that do not affect the result.
      bytes.append(bytes.size(), input_value.is_negative() ? '\xff' : '\x00');
      TestFromScaledValue(bytes, source_scale + extra_scale,
                          fractional_digits_to_keep, allow_rounding,
                          round_half_even, status_or_expected_output);
    }
  }
}

template <typename T, int kNumWords>
void TestFromScaledValueRoundTrip(absl::BitGen* random) {
  // Only valid cases are being tested here. Since we want to do round trip,
  // rounding should not happen when scaling down.
  for (int i = 0; i < 100000; ++i) {
    int num_truncated_digits = 0;
    T original = MakeRandomNumericValue<T>(random, &num_truncated_digits);
    FixedInt<64, kNumWords> scaled_int(original.ToPackedLittleEndianArray());

    int extra_scale = absl::Uniform<int>(
        *random, 0, T::kMaxIntegerDigits + T::kMaxFractionalDigits);
    FixedInt<64, kNumWords* 2> extended_int = ExtendAndMultiply(
        scaled_int, FixedInt<64, kNumWords>::PowerOf10(extra_scale));
    std::string bytes;
    extended_int.SerializeToBytes(&bytes);
    EXPECT_THAT(T::FromScaledLittleEndianValue(
                    bytes, T::kMaxFractionalDigits + extra_scale,
                    T::kMaxFractionalDigits, false, false),
                IsOkAndHolds(original));
    EXPECT_THAT(T::FromScaledLittleEndianValue(
                    bytes, T::kMaxFractionalDigits + extra_scale,
                    T::kMaxFractionalDigits, true, false),
                IsOkAndHolds(original));
    EXPECT_THAT(T::FromScaledLittleEndianValue(
                    bytes, T::kMaxFractionalDigits + extra_scale,
                    T::kMaxFractionalDigits, true, true),
                IsOkAndHolds(original));

    // scaled_int must be a multiple of pow(10, num_truncated_digits).
    int scale_reduction =
        absl::Uniform<int>(*random, 0, num_truncated_digits + 1);
    FixedInt<64, kNumWords> quotient;
    FixedInt<64, kNumWords> remainder;
    scaled_int.DivMod(FixedInt<64, kNumWords>::PowerOf10(scale_reduction),
                      &quotient, &remainder);
    ASSERT_EQ((FixedInt<64, kNumWords>()), remainder);
    bytes.clear();
    quotient.SerializeToBytes(&bytes);
    EXPECT_THAT(T::FromScaledLittleEndianValue(
                    bytes, T::kMaxFractionalDigits - scale_reduction,
                    T::kMaxFractionalDigits, false, false),
                IsOkAndHolds(original));
    EXPECT_THAT(T::FromScaledLittleEndianValue(
                    bytes, T::kMaxFractionalDigits - scale_reduction,
                    T::kMaxFractionalDigits, true, false),
                IsOkAndHolds(original));
    EXPECT_THAT(T::FromScaledLittleEndianValue(
                    bytes, T::kMaxFractionalDigits - scale_reduction,
                    T::kMaxFractionalDigits, true, true),
                IsOkAndHolds(original));
  }
}

TEST_F(NumericValueTest, RescaleValueAllowRounding) {
  static constexpr NumericBinaryOpTestData<int64_t> kTestData[] = {
      {"1234567890123456789012345678.123456", 9,
       "1234567890123456789012345678.123456"},
      {"1234567890123456789012345678.123456", 6,
       "1234567890123456789012345.678123456"},
      {"1234567890123456789012345678.123456", 4,
       "12345678901234567890123.456781235"},
      {"1234567890123456789012345678.123456", 3,
       "1234567890123456789012.345678123"},
      {"1234567890123456789012345678.123456", 0,
       "1234567890123456789.012345678"},
      {"99.99999", 3, "0.0001"},
      {"99.99999", 1, "0.000001"},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 5, "10000000000000000000000000"},
      {kMaxNumericValueStr, 0, "100000000000000000000"},
      {"0", 9, "0"},
      {"0", 4, "0"},
      {"123456789.123", 10, kNumericIllegalScale},
      {"123456789.123", -1, kNumericIllegalScale},
  };

  NumericRescaleOp<true> op;
  for (const NumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, RescaleValueNoRounding) {
  static constexpr NumericBinaryOpTestData<int64_t> kTestData[] = {
      {"1234567890123456789012345678.123456", 9,
       "1234567890123456789012345678.123456"},
      {"1234567890123456789012345678.123456", 6,
       "1234567890123456789012345.678123456"},
      {"1234567890123456789012345678.123456", 4,
       kRescaleValueRoundingNotAllowed},
      {"1234567890123456789012345678.123456", 3,
       kRescaleValueRoundingNotAllowed},
      {"99.99999", 3, kRescaleValueRoundingNotAllowed},
      {"99.99999", 1, kRescaleValueRoundingNotAllowed},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 5, kRescaleValueRoundingNotAllowed},
      {kMaxNumericValueStr, 0, kRescaleValueRoundingNotAllowed},
      {"0", 9, "0"},
      {"0", 4, "0"},
      {"123456789.123", 10, kNumericIllegalScale},
      {"123456789.123", -1, kNumericIllegalScale},
  };

  NumericRescaleOp<false> op;
  for (const NumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Add) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      // The result is too large to fit into int128.
      {kMaxNumericValueStr, kMaxNumericValueStr, kNumericOverflow},
      // The result fits into int128 but exceeds the NUMERIC range.
      {kMaxNumericValueStr, 1, kNumericOverflow},

      {kMaxNumericValueStr, 0, kMaxNumericValueStr},
      {kMinNumericValueStr, kMaxNumericValueStr, 0},
      {0, 0, 0},
      {"0.000000001", "0.000000002", "0.000000003"},
      {"0.000000003", "-0.000000002", "0.000000001"},
      {1, 2, 3},
      {1, "0.000000001", "1.000000001"},
  };

  NumericAddOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Subtract) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      // The result is too large to fit into int128.
      {kMaxNumericValueStr, kMinNumericValueStr, kNumericOverflow},

      // The result fits into int128 but exceeds the NUMERIC range.
      {kMinNumericValueStr, 1, kNumericOverflow},

      {kMaxNumericValueStr, kMaxNumericValueStr, 0},
      {kMaxNumericValueStr, 0, kMaxNumericValueStr},
      {1, 1, 0},
      {3, 1, 2},
      {3, 5, -2},
      {3, -5, 8},
      {-1, 2, -3},
      {-1, -2, 1},
      {"0.000000001", "0.000000001", 0},
      {"0.000000003", "0.000000001", "0.000000002"},
      {"0.000000003", "0.000000005", "-0.000000002"},
      {"0.000000003", "-0.000000005", "0.000000008"},
      {"-0.000000001", "0.000000002", "-0.000000003"},
      {"-0.000000001", "-0.000000002", "0.000000001"},
  };

  NumericSubtractOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, -data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
  }
}

template <typename NumericType, typename MultType>
struct NumericMultiplyAndDivideByPowerOfTwoTestData {
  NumericType num;
  MultType mult;
  uint scale_bits;
  NumericType expected_output;
};

TEST_F(NumericValueTest, NumericMultiplyAndDivideByPowerOfTwo) {
  static constexpr NumericMultiplyAndDivideByPowerOfTwoTestData<
      NumericValueWrapper, FixedInt<64, 2>>
      kTestData[] = {
          // Overflow cases
          {kMaxNumericValueStr, FixedInt<64, 2>(2), 0, kNumericOverflow},
          {kMaxNumericValueStr,
           FixedInt<64, 2>((static_cast<__int128>(1) << 64) | 1), 64,
           kNumericOverflow},

          // Rounding up: 2.4999999976 -> 2.499999998
          {"2.5", FixedInt<64, 2>(0x3FFFFFFF), 30, "2.499999998"},
          // Rounding down: 2.69999999748 -> 2.699999997
          {"2.7", FixedInt<64, 2>(0x3FFFFFFF), 30, "2.699999997"},

          {0, FixedInt<64, 2>(1), 0, 0},
          {1, FixedInt<64, 2>(1), 0, 1},
          {0x80000000, FixedInt<64, 2>(1), 31, 1},
          {0xC0000000, FixedInt<64, 2>(1), 31, "1.5"},
          {0x20000000, FixedInt<64, 2>(3), 31, "0.75"},
          {kMaxNumericValueStr, FixedInt<64, 2>(1), 0, kMaxNumericValueStr},
      };

  const FixedInt<64, 2> max_multiplier =
      FixedInt<64, 2>(static_cast<__int128>(1LL << 60) << 64);  // 2^124
  for (const auto& data : kTestData) {
    const auto num = data.num;
    FixedInt<64, 2> mult = data.mult;
    uint scale_bits = data.scale_bits;
    const auto expected_output = data.expected_output;
    while (mult < max_multiplier) {
      TestNumericMultiplyAndDivideByPowerOfTwoOp(num, mult, scale_bits,
                                                 expected_output);
      TestNumericMultiplyAndDivideByPowerOfTwoOp(-num, mult, scale_bits,
                                                 -expected_output);
      TestNumericMultiplyAndDivideByPowerOfTwoOp(num, -mult, scale_bits,
                                                 -expected_output);
      TestNumericMultiplyAndDivideByPowerOfTwoOp(-num, -mult, scale_bits,
                                                 expected_output);
      mult <<= 1;
      scale_bits++;
    }
  }
}

TEST_F(NumericValueTest, Multiply) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {0, 0, 0},
      {0, 2, 0},
      {0, kMaxNumericValueStr, 0},
      {kMaxNumericValueStr, 1, kMaxNumericValueStr},
      {6, "0.5", 3},
      {4294967296, 4294967296, "18446744073709551616"},
      {"2.5", "0.999999999",
       "2.499999998"},  // round 2.4999999975 > 2.499999998
      {"2.7", "0.999999999",
       "2.699999997"},  // round 2.6999999973 -> 2.699999997
      {"99999999.98", "1000000000100000000010.000000001",
       "99999999989999999998999999999.9"},
      {"33333333333333333333333333333.333333333", 3, kMaxNumericValueStr},
      {kMaxNumericValueStr, kMaxNumericValueStr, kNumericOverflow},
      {kMaxNumericValueStr, "1.000000001", kNumericOverflow},
      // Overflow after rounding.
      {"99999999.99", "1000000000100000000010.000000001", kNumericOverflow},
      {"5e14", "2e14", kNumericOverflow},
  };

  NumericMultiplyOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, data.input2, -data.input1, -data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input2, data.input1, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Multiply_Random) {
  TestMultiplication<NumericValue, int32_t, int64_t>(&random_);
}

TEST_F(NumericValueTest, Negate) {
  EXPECT_EQ(NumericValue(0), NumericValue(0).Negate());
  EXPECT_EQ(NumericValue(-1), NumericValue(1).Negate());
  EXPECT_EQ(NumericValue(2), NumericValue(-2).Negate());
  EXPECT_EQ(NumericValue::MinValue(), NumericValue::MaxValue().Negate());
  EXPECT_EQ(NumericValue::MaxValue(), NumericValue::MinValue().Negate());
}

TEST_F(NumericValueTest, Abs) {
  static constexpr NumericUnaryOpTestData<NumericValueWrapper,
                                          NumericValueWrapper>
      kTestData[] = {
          {0, 0},           {"1e-9", "1e-9"},
          {"0.1", "0.1"},   {1, 1},
          {"1.5", "1.5"},   {123, 123},
          {"1e20", "1e20"}, {kMaxNumericValueStr, kMaxNumericValueStr},
      };
  NumericAbsOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, Sign) {
  static constexpr NumericUnaryOpTestData<NumericValueWrapper, int>
      kTestData[] = {
          {0, 0},     {"1e-9", 1}, {"0.1", 1},  {1, 1},
          {"1.5", 1}, {123, 1},    {"1e20", 1}, {kMaxNumericValueStr, 1},
      };
  NumericSignOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(NumericValueTest, IsTriviallyDestructible) {
  // Verify that NumericValue has a trivial destructor and hence can have static
  // storage duration per (broken link).
  constexpr NumericValue kOne(static_cast<int64_t>(1));
  constexpr NumericValue kTwo(static_cast<uint64_t>(2));
  EXPECT_EQ(kOne, kOne);
  EXPECT_LT(kOne, kTwo);
}

TEST_F(NumericValueTest, SerializeSize) {
  struct ExpectedSizes {
    int size;
    int negated_value_size;
  };
  static constexpr NumericUnaryOpTestData<NumericValueWrapper, ExpectedSizes>
      kTestData[] = {
          {0, {1, 1}},
          {"0.000000001", {1, 1}},
          {"0.00000001", {1, 1}},
          {"0.0000001", {1, 1}},
          {"0.000000127", {1, 1}},
          {"0.000000128", {2, 1}},
          {"0.000000129", {2, 2}},
          {"0.000001", {2, 2}},
          {"0.00001", {2, 2}},
          {"0.000032767", {2, 2}},
          {"0.000032768", {3, 2}},
          {"0.000032769", {3, 3}},
          {"0.0001", {3, 3}},
          {"0.001", {3, 3}},
          {"0.008388607", {3, 3}},
          {"0.008388608", {4, 3}},
          {"0.008388609", {4, 4}},
          {"0.01", {4, 4}},
          {"0.1", {4, 4}},
          {1, {4, 4}},
          {"2.147483647", {4, 4}},
          {"2.147483648", {5, 4}},
          {"2.147483649", {5, 5}},
          {10, {5, 5}},
          {100, {5, 5}},
          {"549.755813887", {5, 5}},
          {"549.755813888", {6, 5}},
          {"549.755813889", {6, 6}},
          {1000, {6, 6}},
          {10000, {6, 6}},
          {100000, {6, 6}},
          {"140737.488355327", {6, 6}},
          {"140737.488355328", {7, 6}},
          {"140737.488355329", {7, 7}},
          {1000000, {7, 7}},
          {10000000, {7, 7}},
          {"36028797.018963967", {7, 7}},
          {"36028797.018963968", {8, 7}},
          {"36028797.018963969", {8, 8}},
          {100000000, {8, 8}},
          {1000000000, {8, 8}},
          {"9223372036.854775807", {8, 8}},
          {"9223372036.854775808", {9, 8}},
          {"9223372036.854775809", {9, 9}},
          {"1e10", {9, 9}},
          {"1e11", {9, 9}},
          {"1e12", {9, 9}},
          {"2361183241434.822606847", {9, 9}},
          {"2361183241434.822606848", {10, 9}},
          {"2361183241434.822606849", {10, 10}},
          {"1e13", {10, 10}},
          {"1e14", {10, 10}},
          {"604462909807314.587353087", {10, 10}},
          {"604462909807314.587353088", {11, 10}},
          {"604462909807314.587353089", {11, 11}},
          {"1e15", {11, 11}},
          {"1e16", {11, 11}},
          {"1e17", {11, 11}},
          {"154742504910672534.362390527", {11, 11}},
          {"154742504910672534.362390528", {12, 11}},
          {"154742504910672534.362390529", {12, 12}},
          {"1e18", {12, 12}},
          {"1e19", {12, 12}},
          {"39614081257132168796.771975167", {12, 12}},
          {"39614081257132168796.771975168", {13, 12}},
          {"39614081257132168796.771975169", {13, 13}},
          {"1e20", {13, 13}},
          {"1e21", {13, 13}},
          {"1e22", {13, 13}},
          {"10141204801825835211973.625643007", {13, 13}},
          {"10141204801825835211973.625643008", {14, 13}},
          {"10141204801825835211973.625643009", {14, 14}},
          {"1e23", {14, 14}},
          {"1e24", {14, 14}},
          {"2596148429267413814265248.164610047", {14, 14}},
          {"2596148429267413814265248.164610048", {15, 14}},
          {"2596148429267413814265248.164610049", {15, 15}},
          {"1e25", {15, 15}},
          {"1e26", {15, 15}},
          {"664613997892457936451903530.140172287", {15, 15}},
          {"664613997892457936451903530.140172288", {16, 15}},
          {"664613997892457936451903530.140172289", {16, 16}},
          {"1e27", {16, 16}},
          {"1e28", {16, 16}},
          {kMaxNumericValueStr, {16, 16}},
      };
  for (const auto& data : kTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(data.input));
    EXPECT_EQ(data.expected_output.size, value.SerializeAsProtoBytes().size());

    NumericValue negated_value = value.Negate();
    EXPECT_EQ(data.expected_output.negated_value_size,
              negated_value.SerializeAsProtoBytes().size());

    std::string output;
    value.SerializeAndAppendToProtoBytes(&output);
    EXPECT_EQ(data.expected_output.size, output.size());
    output.clear();
    negated_value.SerializeAndAppendToProtoBytes(&output);
    EXPECT_EQ(data.expected_output.negated_value_size, output.size());
  }
}

TEST_F(NumericValueTest, SerializeDeserializeProtoBytes) {
  static constexpr NumericValueWrapper kTestValues[] = {
      0, 1, "123.01", "0.000000001", "0.999999999", kMaxNumericValueStr};
  for (const NumericValueWrapper& value_wrapper : kTestValues) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(value_wrapper));
    TestSerialize(value);
    TestSerialize(value.Negate());
  }

  const int kTestIterations = 500;
  for (int i = 0; i < kTestIterations; ++i) {
    NumericValue value = MakeRandomNumeric();
    TestSerialize(value);
    TestSerialize(value.Negate());
  }
}

TEST_F(NumericValueTest, DeserializeProtoBytesFailures) {
  std::string bytes;

  EXPECT_THAT(
      NumericValue::DeserializeFromProtoBytes(bytes),
      StatusIs(absl::StatusCode::kOutOfRange, "Invalid numeric encoding"));
  bytes.resize(17);
  EXPECT_THAT(
      NumericValue::DeserializeFromProtoBytes(bytes),
      StatusIs(absl::StatusCode::kOutOfRange, "Invalid numeric encoding"));

  bytes.resize(16);
  bytes[15] = 0x7f;
  for (int i = 0; i < 15; ++i) {
    bytes[i] = 0xff;
  }
  EXPECT_THAT(NumericValue::DeserializeFromProtoBytes(bytes),
              StatusIs(absl::StatusCode::kOutOfRange,
                       ::testing::HasSubstr("numeric overflow: ")));
}

TEST_F(NumericValueTest, ToDouble) {
  static constexpr NumericUnaryOpTestData<NumericValueWrapper, double>
      kTestData[] = {
          {0, 0},
          {"0.000000001", 0.000000001},
          {"0.999999999", 0.999999999},
          {"1.5", 1.5},
          {123, 123},
          {"123.5", 123.5},
          {"4503599627370496", 4503599627370496.0},
          {"8.12407667", 8.12407667},  // test round to even
          {"1974613819685343985664.0249533", 1974613819685343985664.0249533},
          {kMaxNumericValueStr, 1e29},
      };

  NumericToDoubleOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(NumericValueTest, ToDouble_RandomInputs) {
  for (int i = 0; i < 10000; ++i) {
    NumericValue v = MakeRandomNumeric();
    double expected;
    ASSERT_TRUE(absl::SimpleAtod(v.ToString(), &expected));
    // Use EXPECT_EQ instead of EXPECT_DOUBLE_EQ to ensure exactness.
    EXPECT_EQ(expected, v.ToDouble()) << v.ToString();
  }
}
static constexpr Error kInt32OutOfRange("int32 out of range: ");
template <typename T>
    const NumericUnaryOpTestData<T, ErrorOr<int32_t>>
    kToInt32ValueTestData[] = {
        {0, 0},
        {123, 123},
        {-123, -123},
        {"123.56", 124},
        {"-123.56", -124},
        {"123.5", 124},
        {"-123.5", -124},
        {"123.46", 123},
        {"-123.46", -123},
        {2147483647, 2147483647},
        {-2147483648, int32_t{-2147483648}},
        {2147483648, kInt32OutOfRange},
        {-2147483649, kInt32OutOfRange},
        {"2147483647.499999999", 2147483647},
        {"-2147483648.499999999", int32_t{-2147483648}},
        {"2147483647.5", kInt32OutOfRange},
        {"-2147483648.5", kInt32OutOfRange},
        {kMaxNumericValueStr, kInt32OutOfRange},
        {kMinNumericValueStr, kInt32OutOfRange},
};

TEST_F(NumericValueTest, ToInt32) {
  NumericToIntegerOp<int32_t> op;
  for (const auto& data : kToInt32ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromInt32) {
  TestRoundTripFromInteger<NumericValue, int32_t>(&random_);
}

static constexpr Error kInt64OutOfRange("int64 out of range: ");
template <typename T>
    const NumericUnaryOpTestData<T, ErrorOr<int64_t>>
    kToInt64ValueTestData[] = {
        {0, 0},
        {123, 123},
        {-123, -123},
        {"123.56", 124},
        {"-123.56", -124},
        {"123.5", 124},
        {"-123.5", -124},
        {"123.46", 123},
        {"-123.46", -123},
        {"9223372036854775807", 9223372036854775807LL},
        {"-9223372036854775808", -9223372036854775807LL - 1},
        {"9223372036854775807.499999999", 9223372036854775807LL},
        {"-9223372036854775808.499999999", -9223372036854775807LL - 1},
        {"9223372036854775807.5", kInt64OutOfRange},
        {"-9223372036854775808.5", kInt64OutOfRange},
        {"9223372036854775808", kInt64OutOfRange},
        {"-9223372036854775809", kInt64OutOfRange},
        {kMaxNumericValueStr, kInt64OutOfRange},
        {kMinNumericValueStr, kInt64OutOfRange},
};

TEST_F(NumericValueTest, ToInt64) {
  NumericToIntegerOp<int64_t> op;
  for (const auto& data : kToInt64ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromInt64) {
  TestRoundTripFromInteger<NumericValue, int64_t>(&random_);
}

static constexpr Error kUint32OutOfRange("uint32 out of range: ");
template <typename T>

    const NumericUnaryOpTestData<T, ErrorOr<uint32_t>>
    kToUint32ValueTestData[] = {
        {uint32_t{0}, uint32_t{0}},
        {123, uint32_t{123}},
        {"123.56", uint32_t{124}},
        {"123.5", uint32_t{124}},
        {"123.46", uint32_t{123}},
        {4294967295, uint32_t{4294967295U}},
        {"4294967295.499999999", uint32_t{4294967295U}},
        {"4294967295.5", kUint32OutOfRange},
        {4294967296, kUint32OutOfRange},
        {"-0.499999999", uint32_t{0}},
        {"-0.5", kUint32OutOfRange},
        {-1, kUint32OutOfRange},
        {kMaxNumericValueStr, kUint32OutOfRange},
        {kMinNumericValueStr, kUint32OutOfRange},
};

TEST_F(NumericValueTest, ToUint32) {
  NumericToIntegerOp<uint32_t> op;
  for (const auto& data : kToUint32ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromUint32) {
  TestRoundTripFromInteger<NumericValue, uint32_t>(&random_);
}

static constexpr Error kUint64OutOfRange("uint64 out of range: ");
template <typename T>
    const NumericUnaryOpTestData<T, ErrorOr<uint64_t>>
    kToUint64ValueTestData[] = {
        {0, uint64_t{0}},
        {123, uint64_t{123}},
        {"123.56", uint64_t{124}},
        {"123.5", uint64_t{124}},
        {"123.46", uint64_t{123}},
        {"18446744073709551615", uint64_t{18446744073709551615ull}},
        {"18446744073709551615.499999999", uint64_t{18446744073709551615ull}},
        {"18446744073709551615.5", kUint64OutOfRange},
        {"18446744073709551616", kUint64OutOfRange},
        {"-0.499999999", uint64_t{0}},
        {"-0.5", kUint64OutOfRange},
        {-1, kUint64OutOfRange},
        {kMaxNumericValueStr, kUint64OutOfRange},
        {kMinNumericValueStr, kUint64OutOfRange},
};

TEST_F(NumericValueTest, ToUint64) {
  NumericToIntegerOp<uint64_t> op;
  for (const auto& data : kToUint64ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromUint64) {
  TestRoundTripFromInteger<NumericValue, uint64_t>(&random_);
}

TEST_F(NumericValueTest, FromScaledValueToFullPrecision) {
  struct FromScaledValueTestData {
    absl::string_view input_value;
    int scale;
    NumericValueWrapper expected_output;
  };
  constexpr absl::string_view kScaledMaxNumericValueStr =
      "99999999999999999999999999999999999999";
  static constexpr FromScaledValueTestData kTestDataNoRoundingRequired[] = {
      {"", kintmin, 0},
      {"", -29, 0},
      {"", 0, 0},
      {"", 29, 0},
      {"", kintmax, 0},
      {"0", kintmin, 0},
      {"0", -29, 0},
      {"0", 0, 0},
      {"0", 29, 0},
      {"0", kintmax, 0},
      {"1", kintmin, kNumericFromScaledValueOutOfRange},
      {"1", kintmin / 2, kNumericFromScaledValueOutOfRange},
      {"1", kintmin + 20, kNumericFromScaledValueOutOfRange},
      {"1", -29, kNumericFromScaledValueOutOfRange},
      {"1", -28, "1e28"},
      {"1", -1, 10},
      {"1", 0, 1},
      {"9", -28, "9e28"},
      {"10", -28, kNumericFromScaledValueOutOfRange},
      {"72", 0, 72},
      {"9999999999999999999999999999", -1, "99999999999999999999999999990"},
      {"10000000000000000000000000000", -1, kNumericFromScaledValueOutOfRange},
      // 1e28
      {"10000000000000000000000000000", 27, 10},
      {"100000000000000000000000000000", 29, 1},
      {"100000000000000000000000000000000000000"
       "00000000000000000000000000000000000000",
       76, 1},
      {"100000000000000000000000000000000000000"
       "00000000000000000000000000000000000000",
       85, "1e-9"},
      {"9999999999999999999999999999999999999", 8,
       "99999999999999999999999999999.99999999"},
      {"10000000000000000000000000000000000000", 8,
       kNumericFromScaledValueOutOfRange},
      {kScaledMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kScaledMaxNumericValueStr, 8, kNumericFromScaledValueOutOfRange},
      {kScaledMaxNumericValueStr, kintmin, kNumericFromScaledValueOutOfRange},
  };
  for (const auto& data : kTestDataNoRoundingRequired) {
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/false,
                        /*round_half_even=*/false, data.expected_output);
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/false, data.expected_output);
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/true, data.expected_output);
  }

  static constexpr FromScaledValueTestData kTestDataWithGenericRounding[] = {
      {"449999999999999999999999999999999999999", 47, "4e-9"},
      {"450000000000000000000000000000000000001", 47, "5e-9"},
      {kScaledMaxNumericValueStr, 38, 1},
      {kScaledMaxNumericValueStr, 40, "0.01"},
      {kScaledMaxNumericValueStr, 47, "1e-9"},
      {kScaledMaxNumericValueStr, 48, 0},
      {kScaledMaxNumericValueStr, kintmax, 0},

      {"999999999999999999999999999999999999994", 10, kMaxNumericValueStr},
      {"999999999999999999999999999999999999995", 10,
       kNumericFromScaledValueOutOfRange},  // overflow after rounding
      {"999999999999999999999999999999999999994"
       "999999999999999999999999999999999999999",
       49, kMaxNumericValueStr},
      {"999999999999999999999999999999999999995"
       "000000000000000000000000000000000000000",
       49, kNumericFromScaledValueOutOfRange},
      {"1", 10, 0},
      {"4444444444444444444444444444444444444444", 28,
       "444444444444.444444444"},
  };

  static constexpr FromScaledValueTestData kTestDataRoundHalfEven[] = {
      {"42023125", 10, "0.004202312"},
      {"44444444444444444444445", 10, "4444444444444.444444444"},
      {"450000000000000000000000000000000000000", 47, "4e-9"},
  };

  static constexpr FromScaledValueTestData kTestDataRoundHalfAwayFromZero[] = {
      {"42023125", 10, "0.004202313"},
      {"44444444444444444444445", 10, "4444444444444.444444445"},
      {"450000000000000000000000000000000000000", 47, "5e-9"},
  };

  for (const auto& data : kTestDataWithGenericRounding) {
    TestFromScaledValue(
        data.input_value, data.scale, NumericValue::kMaxFractionalDigits,
        /*allow_rounding=*/false,
        /*round_half_even=*/false,
        NumericValueWrapper(kNumericFromScaledValueRoundingNotAllowed));
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true, /*round_half_even=*/true,
                        data.expected_output);
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true, /*round_half_even=*/false,
                        data.expected_output);
  }

  for (const auto& data : kTestDataRoundHalfEven) {
    TestFromScaledValue(
        data.input_value, data.scale, NumericValue::kMaxFractionalDigits,
        /*allow_rounding=*/false,
        /*round_half_even=*/false,
        NumericValueWrapper(kNumericFromScaledValueRoundingNotAllowed));
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true, /*round_half_even=*/true,
                        data.expected_output);
  }

  for (const auto& data : kTestDataRoundHalfAwayFromZero) {
    TestFromScaledValue(
        data.input_value, data.scale, NumericValue::kMaxFractionalDigits,
        /*allow_rounding=*/false,
        /*round_half_even=*/false,
        NumericValueWrapper(kNumericFromScaledValueRoundingNotAllowed));
    TestFromScaledValue(data.input_value, data.scale,
                        NumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true, /*round_half_even=*/false,
                        data.expected_output);
  }
}

TEST_F(NumericValueTest, FromScaledValueToDestinationScale) {
  struct FromScaledValueToParameterizedTypeTestData {
    absl::string_view input_value;
    int scale;
    int destination_scale;
    NumericValueWrapper expected_output;
  };
  // No rounding required when scaling up. We scale up when the destination
  // type scale is larger than the scale of the little endian input.
  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataScaledUp[] = {
          {"123456789", 5, 8, "1234.56789"},
          {"123456789", 5, 9, "1234.56789"},
          {"123456789", 2, 6, "1234567.89"},
          {"123456789", 5, 7, "1234.56789"},
          {"9999999999999999999999999999999999999", 8, 9,
           "99999999999999999999999999999.99999999"},
          {"99999999999999999999999999999999999995", 9, 9,
           "99999999999999999999999999999.999999995"},
      };

  for (const auto& data : kTestDataScaledUp) {
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/false, /*round_half_even*/ false,
                        data.expected_output, /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even*/ false,
                        data.expected_output, /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even*/ true,
                        data.expected_output, /*test_with_added_scale=*/false);
  }

  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataWithGenericRounding[] = {
          {"123456789", 5, 4, "1234.5679"},
          {"123456789", 5, 2, "1234.57"},
          {"99999999999999999999999999999999999992", 10, 9,
           "9999999999999999999999999999.999999999"},
          {"550000000000000000000000000000000000000", 47, 9, "6e-9"},
      };

  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataWithRoundHalfEven[] = {
          {"123456785", 5, 4, "1234.5678"},
          {"450000000000000000000000000000000000000", 47, 9, "4e-9"},
          {"12345678912345678912345678945", 2, 1,
           "123456789123456789123456789.4"},
      };

  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataWithRoundHalfAwayFromZero[] = {
          {"123456785", 5, 4, "1234.5679"},
          {"450000000000000000000000000000000000000", 47, 9, "5e-9"},
          {"12345678912345678912345678945", 2, 1,
           "123456789123456789123456789.5"},
      };

  for (const auto& data : kTestDataWithGenericRounding) {
    TestFromScaledValue(
        data.input_value, data.scale, data.destination_scale,
        /*allow_rounding=*/false, /*round_half_even=*/false,
        NumericValueWrapper(kNumericFromScaledValueRoundingNotAllowed),
        /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even=*/false,
                        data.expected_output, /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/true, data.expected_output,
                        /*test_with_added_scale=*/false);
  }

  for (const auto& data : kTestDataWithRoundHalfAwayFromZero) {
    TestFromScaledValue(
        data.input_value, data.scale, data.destination_scale,
        /*allow_rounding=*/false, /*round_half_even=*/false,
        NumericValueWrapper(kNumericFromScaledValueRoundingNotAllowed),
        /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even=*/false,
                        data.expected_output,
                        /*test_with_added_scale=*/false);
  }

  for (const auto& data : kTestDataWithRoundHalfEven) {
    TestFromScaledValue(
        data.input_value, data.scale, data.destination_scale,
        /*allow_rounding=*/false, /*round_half_even=*/false,
        NumericValueWrapper(kNumericFromScaledValueRoundingNotAllowed),
        /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even=*/true,
                        data.expected_output,
                        /*test_with_added_scale=*/false);
  }
}

TEST_F(NumericValueTest, FromScaledValueRoundTrip) {
  TestFromScaledValueRoundTrip<NumericValue, 2>(&random_);
}

TEST_F(NumericValueTest, FromDouble) {
  static constexpr NumericUnaryOpTestData<double, NumericValueWrapper>
      kTestData[] = {
          {0, 0},
          {0.000000001, "0.000000001"},
          {0.999999999, "0.999999999"},
          {1.5, "1.5"},
          {123, 123},
          {123.5, "123.5"},
          {4503599627370496.0, "4503599627370496"},
          {1.5e18, "1500000000000000000"},
          {99999999999999999999999999999.999999999,
           "99999999999999991433150857216"},

          // Check rounding
          {3.141592653, "3.141592653"},
          {3.141592653589, "3.141592654"},
          {3.1415926532, "3.141592653"},
          {3.1415926539, "3.141592654"},
          {0.5555555555, "0.555555556"},
          {0.0000000001, 0},

          {1.0000000001e29, kNumericOutOfRange},
          // 3e29 * kScalingFactor is between int128max and uint128max.
          {3e29, kNumericOutOfRange},
          {1e30, kNumericOutOfRange},
          {std::numeric_limits<double>::max(), kNumericOutOfRange},

          {std::numeric_limits<double>::quiet_NaN(), kNumericIllegalNonFinite},
          {std::numeric_limits<double>::signaling_NaN(),
           kNumericIllegalNonFinite},
          {std::numeric_limits<double>::infinity(), kNumericIllegalNonFinite},
      };

  NumericFromDoubleOp<NumericValue> op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(NumericValueTest, FromDouble_RandomInputs) {
  NumericFromDoubleOp<NumericValue> op;
  for (int i = 0; i < 10000; ++i) {
    uint64_t bits = absl::Uniform<uint64_t>(random_);
    double double_val = absl::bit_cast<double>(bits);
    if (!std::isfinite(double_val)) {
      TestUnaryOp(op, double_val,
                  NumericValueWrapper(kNumericIllegalNonFinite));
    } else {
      std::string str = absl::StrFormat("%.11f", double_val);
      if (absl::EndsWith(str, "50")) {
        // Strings with suffix 50 could be from doubles with suffix 49 and
        // get rounded up. In this case the test would fail while the result is
        // correct. Skipping those cases for now.
        continue;
      }
      auto expected = NumericValue::FromString(str);
      if (expected.ok()) {
        TestUnaryOp(op, double_val, expected);
      } else {
        TestUnaryOp(op, double_val, NumericValueWrapper(kNumericOutOfRange));
      }
    }
  }
}

TEST_F(NumericValueTest, Divide) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {1, 1, 1},
      {0, kMaxNumericValueStr, 0},
      {6, 2, 3},
      {kMaxNumericValueStr, 1, kMaxNumericValueStr},
      {kMaxNumericValueStr, kMaxNumericValueStr, 1},
      {kMaxNumericValueStr, 3, "33333333333333333333333333333.333333333"},
      {kMaxNumericValueStr, "33333333333333333333333333333.333333333", 3},
      {5, 2, "2.5"},
      {5, "0.5", 10},
      {"18446744073709551616", 4294967296, 4294967296},

      // Rounding.
      {1, 3, "0.333333333"},
      {2, 3, "0.666666667"},
      {"1e20", "33333333333333333333333333333", "0.000000003"},

      // Specific test cases that exercise rarely executed branches in the
      // division algorithm.
      {"72532070012368178591038012.422501607", "30633582.37",
       "2367730588486448005.037486330"},
      {"75968009597863048104202226663", "56017.999",
       "1356135723410310462967487.051135118"},

      {kMaxNumericValueStr, "0.3", kNumericOverflow},
      {kMaxNumericValueStr, "0.999999999", kNumericOverflow},
      {"1e20", "1e-9", kNumericOverflow},

      {0, 0, kDivisionByZero},
      {"0.1", 0, kDivisionByZero},
      {1, 0, kDivisionByZero},
      {kMaxNumericValueStr, 0, kDivisionByZero},
  };

  NumericDivideOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, MultiplicationDivisionRoundTrip) {
  TestMultiplicationDivisionRoundTrip<NumericValue, int32_t, int64_t, __int128>(
      &random_);
}

template <typename T>
double GetMaxErrorFromDoubleReference(double expected_value) {
  static double kMinPositiveValue = T::FromScaledValue(1).ToDouble();
  return std::max(kMinPositiveValue, std::scalbn(expected_value, -52));
}

TEST_F(NumericValueTest, Exp) {
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {1, "2.718281828"},
      {4, "54.598150033"},
      {"-20.723265837", "1e-9"},
      {"-1.2345", "0.290980216"},
      {"-0.2", "0.818730753"},
      {"1e-9", "1.000000001"},
      {"0.000001234", "1.000001234"},
      {"1.123456789", "3.07546709"},
      {"12.123456789", "184140.952403667"},
      {"30", "10686474581524.46214699"},
      {"-23", "0"},
      {"-21.416413018", "0"},
      {"66.774967697", kNumericOverflow},
      {"67", kNumericOverflow},
      {kMinNumericValueStr, "0"},
      {kMaxNumericValueStr, kNumericOverflow},
  };

  NumericExpOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

template <typename T>
void TestExpWithRandomLosslessDoubleValue(uint max_integer_bits,
                                          double max_valid_value_abs,
                                          absl::BitGen* random) {
  int trivial_case_count = 0;
  for (int i = 0; i < 100000; ++i) {
    double x;
    do {
      x = MakeLosslessRandomDoubleValue<T>(max_integer_bits, random);
    } while (x > max_valid_value_abs || x < -max_valid_value_abs);
    double approx_expected = std::exp(x);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue x_value,
                         BigNumericValue::FromDouble(x));
    auto result = x_value.Exp();
    if (result.ok()) {
      double expected_error =
          GetMaxErrorFromDoubleReference<T>(approx_expected);
      EXPECT_NEAR(result.value().ToDouble(), approx_expected, expected_error)
          << "EXP(" << x_value.ToString() << ")";
      if (result.value() == BigNumericValue()) {
        trivial_case_count++;
      }
    } else {
      EXPECT_GT(approx_expected, 5.78960446186580977e38)
          << "EXP(" << x_value << "): expected " << approx_expected << "\ngot "
          << result.status();
      trivial_case_count++;
    }
  }
  EXPECT_LT(trivial_case_count, 2000);
}

TEST_F(NumericValueTest, Exp_WithRandomLosslessDoubleValue) {
  // LN(MaxNumericValue) is approximately equals to 66.775, so here we
  // test value with maximum integer bit 7 for a value upper bound 128 to
  // cover overflow cases.
  TestExpWithRandomLosslessDoubleValue<NumericValue>(7, 66.8, &random_);
}

TEST_F(NumericValueTest, Ln) {
  static constexpr Error kNegativeUndefinedError(
      "LN is undefined for zero or negative value: ");
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {"2.718281828", 1},
      {"54.598150033", 4},
      {"1e-9", "-20.723265837"},
      {"0.000001234", "-13.605249632"},
      {"0.123456789", "-2.091864071"},
      {"1.123456789", "0.116410351"},
      {"12345678901234567890123456789.123456789", "64.683103626"},
      {kMaxNumericValueStr, "66.774967697"},
      {-1, kNegativeUndefinedError},
      {kMinNumericValueStr, kNegativeUndefinedError},
      {0, kNegativeUndefinedError},
  };

  NumericLnOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

template <typename T>
void TestLnWithRandomLosslessDoubleValue(uint max_integer_bits,
                                         absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    double x;
    do {
      x = std::abs(MakeLosslessRandomDoubleValue<T>(max_integer_bits, random));
    } while (x == 0);
    double approx_expected = std::log(x);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue x_value,
                         BigNumericValue::FromDouble(x));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue result, x_value.Ln());
    double expected_error = GetMaxErrorFromDoubleReference<T>(approx_expected);
    EXPECT_NEAR(result.ToDouble(), approx_expected, expected_error)
        << "LN(" << x_value.ToString() << ") = " << result.ToString()
        << " double x: " << x;
  }
}

TEST_F(NumericValueTest, Ln_WithRandomLosslessDoubleValue) {
  TestLnWithRandomLosslessDoubleValue<NumericValue>(96, &random_);
}

template <typename T>
void TestLnExpRoundTrip(const T& max_valid_exp_value, absl::BitGen* random) {
  // Testing EXP(LN(x)) should be close to x.
  for (int i = 0; i < 10000; ++i) {
    T value = MakeRandomPositiveNumericValue<T>(random);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, value.Ln());
    if (result > max_valid_exp_value) {
      // Assign result to be max_valid_exp_value to avoid the out of range issue
      // caused by round up in Ln.
      result = max_valid_exp_value;
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, result.Exp());
    // Due to precision limit, Ln will cause a precision loss at most
    // MinPossibleValue of T, so that could cause precision loss of final result
    // for at most x * EXP(MinPossibleValue), which approximately equals to x *
    // MinPossibleValue.
    T expect_error = T::FromScaledValue(1);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(expect_error, value.Multiply(expect_error));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error, result.Subtract(value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error_abs, GetValue(error.Abs()));
    EXPECT_LE(error_abs, expect_error);
  }
}

TEST_F(NumericValueTest, LnExpRoundTrip) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue max_valid_exp_value,
                       NumericValue::FromString("66.774967696"));
  TestLnExpRoundTrip(max_valid_exp_value, &random_);
}

TEST_F(NumericValueTest, Log10) {
  static constexpr Error kNonPositiveUndefinedError(
      "LOG10 is undefined for zero or negative value: ");
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {10, 1},
      {10000, 4},
      {"0.1", -1},
      {"1e-9", -9},
      {"1e28", 28},
      {"0.000001234", "-5.90868484"},
      {"0.123456789", "-0.908485023"},
      {"1.123456789", "0.050556373"},
      {"12345678901234567890123456789.123456789", "28.091514977"},
      {kMaxNumericValueStr, 29},
      {-1, kNonPositiveUndefinedError},
      {"-1e-9", kNonPositiveUndefinedError},
      {kMinNumericValueStr, kNonPositiveUndefinedError},
      {0, kNonPositiveUndefinedError},
  };

  NumericLog10Op op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

template <typename T>
void TestLog10PowRoundTripIntegerResult(int integer_digits,
                                        int fractional_digits,
                                        absl::BitGen* random) {
  for (int i = -fractional_digits; i <= integer_digits; ++i) {
    T expected_result(i);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, T(10).Power(expected_result));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, x_value.Log10());
    EXPECT_EQ(expected_result, result) << "LOG10(" << x_value.ToString() << ")";
  }
}

TEST_F(NumericValueTest, Log10_PowRoundTrip_IntegerResult) {
  TestLog10PowRoundTripIntegerResult<NumericValue>(28, -9, &random_);
}

template <typename T>
void TestLog10PowRoundTripRandomResult(absl::BitGen* random) {
  // Testing POW(10,LOG10(x)) should be close to x.
  int trivial_case_count = 0;
  // With y=LOG10(x), expected error of POW(10,LOG10(x)) is approximatly
  // 10^(y+e)-10^y = x*(10^e), e is expected error of LOG10(x), which is less
  // that the smallest fractional unit of T.
  T expected_relative_error_ratio = T::FromScaledValue(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(expected_relative_error_ratio,
                       T(10).Power(expected_relative_error_ratio));
  for (int i = 0; i < 10000; ++i) {
    T x_value = MakeRandomPositiveNumericValue<T>(random);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T log10, x_value.Log10());
    auto result_or_status = T(10).Power(log10);
    if (result_or_status.status().code() == absl::StatusCode::kOutOfRange) {
      // Round up in previous steps could cause out of range error.
      ++trivial_case_count;
      continue;
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, result_or_status);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T expect_error,
                         x_value.Multiply(expected_relative_error_ratio));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error, result.Subtract(x_value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error_abs, GetValue(error.Abs()));
    EXPECT_LE(error_abs, expect_error);
  }
  EXPECT_LT(trivial_case_count, 50);
}

TEST_F(NumericValueTest, Log10_PowRoundTrip_RandomResult) {
  TestLog10PowRoundTripRandomResult<NumericValue>(&random_);
}

TEST_F(NumericValueTest, Log) {
  static constexpr Error kNonPositiveUndefinedError(
      "LOG is undefined for zero or negative value, or when base equals 1: ");
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {9, 3, 2},
      {"0.25", 2, -2},
      {32768, 2, 15},
      {"0.01", "0.1", 2},
      {"0.001", "0.1", 3},
      {"0.0001", "0.1", 4},
      {"86.497558594", "1.5", 11},
      {"4495482048799565826089401.980417643", "1.5", 140},
      {"21916.681339078", "1.001", 10000},
      {2, 4, "0.5"},
      {"0.5", 4, "-0.5"},
      {8, 4, "1.5"},
      {"0.125", 4, "-1.5"},
      {32, 4, "2.5"},
      {"0.03125", 4, "-2.5"},
      {"3e10", "9e20", "0.5"},
      {"12345678912345", "152415787806720022193399025", "0.5"},
      {"1e-5", "1e10", "-0.5"},
      {"1.000100005", "1.00001", "10.000049983"},
      {100, "1.000000001", "4605170188.290676461"},
      {100, "0.999999999", "-4605170183.685506275"},
      {"0.000000238", "2", "-22.002535091"},
      {kMaxNumericValueStr, kMaxNumericValueStr, 1},
      // Actual value approx. 66774967730.214808679
      {kMaxNumericValueStr, "1.000000001", "66774967730.214808682"},
      // Actual value approx. -66774967663.439840983
      {kMaxNumericValueStr, "0.999999999", "-66774967663.439840994"},
      {"1.000000001", 8, 0 /* Actual value approx. 4.8e-10*/},
      {"1.000000001", 7, "1e-9" /* Actual value approx. 5.14e-10*/},
      {10, "-1e-9", kNonPositiveUndefinedError},
      {10, 0, kNonPositiveUndefinedError},
      {10, 1, kNonPositiveUndefinedError},
      {10, kMinNumericValueStr, kNonPositiveUndefinedError},
      {"-1e-9", 10, kNonPositiveUndefinedError},
      {0, 10, kNonPositiveUndefinedError},
      {kMinNumericValueStr, 10, kNonPositiveUndefinedError},
  };

  NumericLogOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

template <typename T>
void TestLogWithRandomLosslessDoubleValue(uint max_integer_bits,
                                          double expected_relative_error_ratio,
                                          absl::BitGen* random) {
  int trivial_case_count = 0;
  const double max_value = T::MaxValue().ToDouble();
  for (int i = 0; i < 1000; ++i) {
    double x = 0;
    do {
      x = std::abs(MakeLosslessRandomDoubleValue<T>(max_integer_bits, random));
    } while (x == 0);
    double y = 0;
    do {
      y = std::abs(MakeLosslessRandomDoubleValue<T>(max_integer_bits, random));
    } while (y == 0);
    double approx_expected = std::log(x) / std::log(y);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, T::FromDouble(x));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T y_value, T::FromDouble(y));
    auto result = x_value.Log(y_value);
    if (result.ok()) {
      double expected_error =
          std::max(T::FromScaledValue(1).ToDouble(),
                   approx_expected * expected_relative_error_ratio);
      EXPECT_NEAR(result.value().ToDouble(), approx_expected, expected_error)
          << "LOG(" << x_value.ToString() << ", " << y_value.ToString() << ")";
    } else {
      EXPECT_TRUE(std::abs(approx_expected) > max_value)
          << "LOG(" << x_value << ", " << y_value << "): expected "
          << approx_expected << "\ngot " << result.status();
      trivial_case_count++;
    }
  }
  EXPECT_LT(trivial_case_count, 50);
}

TEST_F(NumericValueTest, Log_WithRandomLosslessDoubleValue) {
  TestLogWithRandomLosslessDoubleValue<NumericValue>(96, 0, &random_);
}

template <typename T>
void TestLogPowRoundTrip(T expected_relative_error_ratio,
                         absl::BitGen* random) {
  // Testing POW(y,ABSL_LOG(x,y)) should be close to x.
  int trivial_case_count = 0;
  for (int i = 0; i < 10000; ++i) {
    T x_value = MakeRandomPositiveNumericValue<T>(random);
    T y_value = MakeRandomPositiveNumericValue<T>(random);
    auto log_or_status = x_value.Log(y_value);
    if (log_or_status.status().code() == absl::StatusCode::kOutOfRange) {
      ++trivial_case_count;
      continue;
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T log, log_or_status);
    auto result_or_status = y_value.Power(log);
    if (result_or_status.status().code() == absl::StatusCode::kOutOfRange) {
      ++trivial_case_count;
      continue;
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, result_or_status);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T expect_error,
                         x_value.Multiply(expected_relative_error_ratio));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error, result.Subtract(x_value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error_abs, GetValue(error.Abs()));
    EXPECT_LE(error_abs, expect_error)
        << "LOG(" << x_value.ToString() << ", " << y_value.ToString() << ")";
  }
  EXPECT_LT(trivial_case_count, 50);
}

TEST_F(NumericValueTest, LogPowRoundTrip) {
  // 1e-7
  NumericValue expected_relative_error_ratio =
      NumericValue::FromScaledValue(100);
  TestLogPowRoundTrip<NumericValue>(expected_relative_error_ratio, &random_);
}

TEST_F(NumericValueTest, Power) {
  static constexpr Error kNegativeToFractionalError(
      "Negative NUMERIC value cannot be raised to a fractional power: ");
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {0, 0, 1},
      {kMaxNumericValueStr, 0, 1},
      {kMinNumericValueStr, 0, 1},
      {0, 10, 0},
      {3, 1, 3},
      {-3, 1, -3},
      {3, 2, 9},
      {-3, 2, 9},
      {2, 15, 32768},
      {-2, 15, -32768},
      {kMaxNumericValueStr, 1, kMaxNumericValueStr},
      {kMinNumericValueStr, 1, kMinNumericValueStr},
      {"0.1", 2, "0.01"},
      {"0.1", 3, "0.001"},
      {"0.1", 4, "0.0001"},
      {"-0.1", 2, "0.01"},
      {"-0.1", 3, "-0.001"},
      {"-0.1", 4, "0.0001"},
      {"1.00001", 10, "1.000100005"},
      {"-1.00001", 10, "1.000100005"},
      {"1.5", 11, "86.497558594"},
      {"-1.5", 11, "-86.497558594"},
      {"1.5", 140, "4495482048799565826089401.980417643"},
      {"1.001", 10000, "21916.681339078"},
      {"-1.001", 10000, "21916.681339078"},

      // Negative exponent.
      {5, -1, "0.2"},
      {-5, -1, "-0.2"},
      {1, -10, 1},
      {-1, -10, 1},
      {1, -11, 1},
      {-1, -11, -1},
      {"0.1", -1, 10},
      {"-0.1", -1, -10},
      {"0.1", -10, "1e10"},
      {"-0.1", -10, "1e10"},
      {"0.1", -11, "1e11"},
      {"-0.1", -11, "-1e11"},
      {"0.1", -28, "1e28"},

      // Fractional exponent.
      {0, "0.5", 0},
      {4, "0.5", 2},
      {4, "-0.5", "0.5"},
      {4, "1.5", 8},
      {4, "-1.5", "0.125"},
      {4, "2.5", 32},
      {4, "-2.5", "0.03125"},
      {"9e20", "0.5", "3e10"},
      {kMaxNumericValueStr, "0.5", "316227766016837.933199889"},
      {"152415787806720022193399025", "0.5", "12345678912345"},
      {"1e10", "-0.5", "1e-5"},
      {"2e9", "-0.5", "0.000022361"},
      {"2e9", "-1.01", 0},
      {"1024", "-2.2", "0.000000238"},
      {"2", -22, "0.000000238"},
      {"-2", -22, "0.000000238"},

      // Underflow.
      {kMaxNumericValueStr, -1, 0},
      {kMinNumericValueStr, -1, 0},
      {kMaxNumericValueStr, kMinNumericValueStr, 0},
      {"0.1", 10, 0},
      {"-0.1", 10, 0},
      {"1e5", -2, 0},
      {"-1e5", -2, 0},
      {"1e2", -5, 0},
      {"-1e2", -5, 0},
      {"5.123", -40, 0},
      {"-5.123", -40, 0},

      // Overflow.
      {kMaxNumericValueStr, 2, kNumericOverflow},
      {kMinNumericValueStr, 2, kNumericOverflow},
      {"5.123", 50, kNumericOverflow},
      {"-5.123", 50, kNumericOverflow},
      {"19106210.01032759", "4.23", kNumericOverflow},
      {"0.479371", "-96.45037198", kNumericOverflow},

      {0, -1, kDivisionByZero},
      {0, "-1.5", kDivisionByZero},
      {0, -2, kDivisionByZero},
      {0, kMinNumericValueStr, kDivisionByZero},

      {"-0.000000001", "0.000000001", kNegativeToFractionalError},
      {"-0.000000001", "-0.000000001", kNegativeToFractionalError},
      {-123, "2.1", kNegativeToFractionalError},
      {-123, "-2.1", kNegativeToFractionalError},
      {kMinNumericValueStr, kMaxNumericValueStr, kNegativeToFractionalError},
      {kMinNumericValueStr, kMinNumericValueStr, kNegativeToFractionalError},
  };

  NumericPowerOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

template <typename T>
void TestPowWithRandomLosslessDoubleValue(uint max_integer_bits,
                                          absl::BitGen* random) {
  int trivial_case_count = 0;
  const double max_value = T::MaxValue().ToDouble();
  for (int i = 0; i < 1000; ++i) {
    double approx_expected =
        MakeRandomPositiveNumericValue<T>(random).ToDouble();
    double x = 0;
    do {
      x = std::abs(MakeLosslessRandomDoubleValue<T>(max_integer_bits, random));
    } while (x == 0);
    double approx_y = std::log(approx_expected) / std::log(x);
    googlesql_base::MathUtil::DoubleParts y_parts = googlesql_base::MathUtil::Decompose(approx_y);
    // Enforce exponent >= -T::kMaxFractionalDigits without increasing the bits
    // in mantissa, so that mantissa * 2 ^ exponent can be losslessly
    // represented by both double and T.
    if (y_parts.exponent < -T::kMaxFractionalDigits) {
      uint shift_bits = -T::kMaxFractionalDigits - y_parts.exponent;
      // Use FixedInt to handle the cases with mantissa < 0 and those with
      // shift_bits > 64.
      FixedInt<64, 1> mantissa(y_parts.mantissa);
      mantissa >>= shift_bits;
      y_parts.mantissa = static_cast<int64_t>(mantissa.number()[0]);
      y_parts.exponent = -T::kMaxFractionalDigits;
    }
    double y = std::ldexp(y_parts.mantissa, y_parts.exponent);
    approx_expected = std::pow(x, y);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, T::FromDouble(x));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T y_value, T::FromDouble(y));
    auto result = x_value.Power(y_value);
    if (result.ok()) {
      double expected_error =
          GetMaxErrorFromDoubleReference<T>(approx_expected);
      EXPECT_NEAR(result.value().ToDouble(), approx_expected, expected_error)
          << "POW(" << x_value.ToString() << ", " << y_value.ToString() << ")";
      if (result.value() == T()) {
        trivial_case_count++;
      }
    } else {
      EXPECT_TRUE(std::abs(approx_expected) > max_value)
          << "POW(" << x_value << ", " << y_value << "): expected "
          << approx_expected << "\ngot " << result.status();
      trivial_case_count++;
    }
  }
  EXPECT_LT(trivial_case_count, 50);
}

TEST_F(NumericValueTest, Pow_WithRandomLosslessDoubleValue) {
  TestPowWithRandomLosslessDoubleValue<NumericValue>(96, &random_);
}

TEST_F(NumericValueTest, Sqrt) {
  static constexpr Error kNegativeUndefinedError(
      "SQRT is undefined for negative value: ");
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {1, 1},
      {4, 2},
      {"0.04", "0.2"},
      {"1e-8", "1e-4"},
      {"0.000001234", "0.001110856"},
      {"0.123456789", "0.351364183"},
      {"1.123456789", "1.059932445"},
      {"12345678901234567890123456789.123456789", "111111110611111.109936111"},
      {"9e28", "3e14"},
      {kMaxNumericValueStr, "316227766016837.933199889"},
      {"-1e-9", kNegativeUndefinedError},
      {-1, kNegativeUndefinedError},
      {kMinNumericValueStr, kNegativeUndefinedError},
  };

  NumericSqrtOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

template <typename T>
void TestSqrtWithRandomIntegerValue(uint64_t max_integer_value,
                                    absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    uint64_t x_sqrt = absl::Uniform<uint64_t>(absl::IntervalClosedClosed,
                                              *random, 0, max_integer_value);
    T expected_result(x_sqrt);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, expected_result.Multiply(expected_result));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, x_value.Sqrt());
    EXPECT_EQ(expected_result, result)
        << "SQRT(" << x_value.ToString() << ")";
  }
}

TEST_F(NumericValueTest, Sqrt_WithRandomIntegerValue) {
  TestSqrtWithRandomIntegerValue<NumericValue>(316227766016837ULL, &random_);
}

template <typename T>
void TestSqrtWithRandomLosslessDoubleValue(uint max_integer_bits,
                                           absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    double x;
    do {
      x = std::abs(MakeLosslessRandomDoubleValue<T>(max_integer_bits, random));
    } while (x == 0);
    double approx_expected = std::sqrt(x);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, T::FromDouble(x));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, x_value.Sqrt());
    double expected_error = GetMaxErrorFromDoubleReference<T>(approx_expected);
    EXPECT_NEAR(result.ToDouble(), approx_expected, expected_error)
        << "SQRT(" << x_value.ToString() << ") = " << result.ToString()
        << " double x: " << x;
  }
}

TEST_F(NumericValueTest, Sqrt_WithRandomLosslessDoubleValue) {
  TestSqrtWithRandomLosslessDoubleValue<NumericValue>(96, &random_);
}

template <typename T>
void TestSqrtPowRoundTrip(absl::BitGen* random) {
  // Testing POW(SQRT(x), 2) should be close to x.
  for (int i = 0; i < 10000; ++i) {
    T x_value = MakeRandomPositiveNumericValue<T>(random);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_sqrt, x_value.Sqrt());
    auto result_or_status = x_sqrt.Power(T(2));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, result_or_status);
    T expected_relative_error_ratio = T::FromScaledValue(2);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T expect_error,
                         x_sqrt.Multiply(expected_relative_error_ratio));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error, result.Subtract(x_value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error_abs, GetValue(error.Abs()));
    EXPECT_LE(error_abs, expect_error)
        << "POW(SQRT(" << x_value.ToString() << "), 2) = " << result.ToString()
        << " expected x: " << x_value.ToString();
  }
}

TEST_F(NumericValueTest, SqrtPowRoundTrip) {
  TestSqrtPowRoundTrip<NumericValue>(&random_);
}

TEST_F(NumericValueTest, Cbrt) {
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {1, 1},
      {8, 2},
      {"0.008", "0.2"},
      {"1e-9", "1e-3"},
      {"0.000001234", "0.010726015"},
      {"0.123456789", "0.497933859"},
      {"1.123456789", "1.039566137"},
      {"12345678901234567890123456789.123456789", "2311204240.901836251"},
      {"8e27", "2e9"},
      {kMaxNumericValueStr, "4641588833.612778892"},
  };

  NumericCbrtOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

template <typename T>
void TestCbrtWithRandomIntegerValue(int64_t max_integer_value,
                                    absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    int64_t x_cbrt =
        absl::Uniform<int64_t>(absl::IntervalClosedClosed, *random,
                               -max_integer_value, max_integer_value);
    T expected_result(x_cbrt);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, expected_result.Power(T(3)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, x_value.Cbrt());
    EXPECT_EQ(expected_result, result) << "CBRT(" << x_value.ToString() << ")";
  }
}

TEST_F(NumericValueTest, Cbrt_WithRandomIntegerValue) {
  TestCbrtWithRandomIntegerValue<NumericValue>(4641588833ULL, &random_);
}

template <typename T>
double GetMaxErrorForCbrt(double expected_value) {
  expected_value = fabs(expected_value);
  static double kMinPositiveValue = T::FromScaledValue(1).ToDouble();
  // From third_party/googletest/googletest/src/gtest.cc:DoubleNearPredFormat
  static double epsilon =
      nextafter(expected_value, std::numeric_limits<double>::infinity()) -
      expected_value;
  return std::max(
      epsilon, std::max(kMinPositiveValue, std::scalbn(expected_value, -50)));
}

void TestNumericApproximateCbrt(absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    NumericValue value = MakeRandomPositiveNumericValue<NumericValue>(random);
    double true_cbrt = std::cbrt(value.ToDouble());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue approx_cbrt,
                         TestOnlyNumericApproximateCbrt(value));
    double max_allowable_error = 0.08 * true_cbrt;
    EXPECT_NEAR(approx_cbrt.ToDouble(), true_cbrt, max_allowable_error);
  }
}

void TestBigNumericApproximateCbrt(absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    BigNumericValue value =
        MakeRandomPositiveNumericValue<BigNumericValue>(random);
    double true_cbrt = std::cbrt(value.ToDouble());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue approx_cbrt,
                         TestOnlyBigNumericApproximateCbrt(value));
    double max_allowable_error = 0.08 * true_cbrt;
    EXPECT_NEAR(approx_cbrt.ToDouble(), true_cbrt, max_allowable_error);
  }
}

TEST_F(NumericValueTest, ApproximateCbrt) {
  TestNumericApproximateCbrt(&random_);
}

template <typename T>
void TestCbrtWithRandomLosslessDoubleValue(uint max_integer_bits,
                                           absl::BitGen* random) {
  for (int i = 0; i < 10000; ++i) {
    double x;
    do {
      x = MakeLosslessRandomDoubleValue<T>(max_integer_bits, random);
    } while (x == 0);
    double approx_expected = std::cbrt(x);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_value, T::FromDouble(x));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, x_value.Cbrt());
    double expected_error = GetMaxErrorForCbrt<T>(approx_expected);
    EXPECT_NEAR(result.ToDouble(), approx_expected, expected_error)
        << "CBRT(" << x_value.ToString() << ") = " << result.ToString()
        << " double x: " << x;
  }
}

TEST_F(NumericValueTest, Cbrt_WithRandomLosslessDoubleValue) {
  TestCbrtWithRandomLosslessDoubleValue<NumericValue>(96, &random_);
}

template <typename T>
void TestCbrtPowRoundTrip(absl::BitGen* random) {
  // Testing POW(CBRT(x), 3) should be close to x.
  for (int i = 0; i < 10000; ++i) {
    T x_value = MakeRandomNumericValue<T>(random);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_cbrt, x_value.Cbrt());
    auto result_or_status = x_cbrt.Power(T(3));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T result, result_or_status);

    // Given e = T::FromScaledValue(1), the smallest representable value, we
    // expect the error for cbrt to be at most e, so

    // (x + e)^3 - x^3 = e^3 + 3e^2 x + 3e x^2 ~ 3ex (x + e)
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T cbrt_abs, GetValue(x_cbrt.Abs()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T three_e_x, cbrt_abs.Multiply(T::FromScaledValue(3)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T x_plus_e, cbrt_abs.Add(T::FromScaledValue(1)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T expect_error, three_e_x.Multiply(x_plus_e));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error, result.Subtract(x_value));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T error_abs, GetValue(error.Abs()));
    EXPECT_LE(error_abs, expect_error)
        << "POW(CBRT(" << x_value.ToString() << "), 3) = " << result.ToString()
        << " expected x: " << x_value.ToString();
  }
}

TEST_F(NumericValueTest, CbrtPowRoundTrip) {
  TestCbrtPowRoundTrip<NumericValue>(&random_);
}

using FormatSpec = NumericValue::FormatSpec;
using FormatTestData = NumericBinaryOpTestData<FormatSpec, absl::string_view>;
using Flag = NumericValue::FormatSpec::Flag;
constexpr FormatSpec kDefaultSpec;
constexpr FormatSpec::Mode f = FormatSpec::DEFAULT;
constexpr FormatSpec::Mode e = FormatSpec::E_NOTATION_LOWER_CASE;
constexpr FormatSpec::Mode E = FormatSpec::E_NOTATION_UPPER_CASE;
constexpr FormatSpec::Mode g = FormatSpec::GENERAL_FORMAT_LOWER_CASE;
constexpr FormatSpec::Mode G = FormatSpec::GENERAL_FORMAT_UPPER_CASE;
static constexpr FormatTestData kFormatTestData[] = {
    {0, {0, 0, f}, "0"},
    {0, kDefaultSpec, "0.000000"},
    {"1.5", kDefaultSpec, "1.500000"},
    {"1.5", {3, 7}, "1.5000000"},
    {"1.5", {10}, "  1.500000"},
    {"1.5", {10, 3}, "     1.500"},
    // 1.5 rounds away from 0 to 2.
    {"1.5", {10, 0}, "         2"},
    // -1.5 rounds away from 0 to -2.
    {"-1.5", {10, 0}, "        -2"},
    {"0.04", {0, 1}, "0.0"},
    // Sign is preserved after rounding.
    {"-0.04", {0, 1}, "-0.0"},
    {"1.999999999", kDefaultSpec, "2.000000"},
    {"1.999999999", {10}, "  2.000000"},
    {"1.999999999", {10, 0}, "         2"},
    {"1.999999999", {10, 9}, "1.999999999"},
    {"1.999999999", {10, 7}, " 2.0000000"},
    {"-1.999999999", {10, 7}, "-2.0000000"},
    {"1.999999999", {10, 10}, "1.9999999990"},
    {"1.999999999", {14, 10}, "  1.9999999990"},
    {"-1.999999999", {14, 10}, " -1.9999999990"},
    {kMaxNumericValueStr, {0, 9}, "99999999999999999999999999999.999999999"},
    {kMaxNumericValueStr, {0, 11}, "99999999999999999999999999999.99999999900"},
    {kMaxNumericValueStr,
     {45, 11},
     "    99999999999999999999999999999.99999999900"},
    {kMaxNumericValueStr,
     {40, 11},
     "99999999999999999999999999999.99999999900"},

    // Zero-padding.
    {"1.5", {10, 3, f, Flag::ZERO_PAD}, "000001.500"},
    {"-1.5", {10, 3, f, Flag::ZERO_PAD}, "-00001.500"},
    // Sign flag.
    {"1.5", {0, 3, f, Flag::ALWAYS_PRINT_SIGN}, "+1.500"},
    {"-1.5", {0, 3, f, Flag::ALWAYS_PRINT_SIGN}, "-1.500"},
    // Blank space if no sign.
    {"1.5", {0, 3, f, Flag::SIGN_SPACE}, " 1.500"},
    {"-1.5", {0, 3, f, Flag::SIGN_SPACE}, "-1.500"},
    {"1.5", {6, 3, f, Flag::SIGN_SPACE}, " 1.500"},
    {"1.5", {7, 3, f, Flag::SIGN_SPACE}, "  1.500"},
    // Decimal point.
    {1, {0, 0, f, Flag::ALWAYS_PRINT_DECIMAL_POINT}, "1."},
    // Remove trailing zeros after decimal point.
    {0, {0, 3, f, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "0"},
    {1, {0, 3, f, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "1"},
    {"1.2", {0, 3, f, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "1.2"},
    {10, {0, 3, f, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "10"},
    // Left justify padding
    {"0.001433", {12, 4, f, Flag::LEFT_JUSTIFY}, "0.0014      "},
    {"12", {6, 0, f, Flag::LEFT_JUSTIFY}, "12    "},
    {"9.999", {6, 2, f, Flag::LEFT_JUSTIFY}, "10.00 "},
    {"-431876.532876872", {15, 3, f, Flag::LEFT_JUSTIFY}, "-431876.533    "},
    // Use Grouping character
    {"0.432089234", {0, 4, f, Flag::USE_GROUPING_CHAR}, "0.4321"},
    {"4", {0, 4, f, Flag::USE_GROUPING_CHAR}, "4.0000"},
    {"123", {0, 0, f, Flag::USE_GROUPING_CHAR}, "123"},
    {"1234", {0, 2, f, Flag::USE_GROUPING_CHAR}, "1,234.00"},
    {"12345", {0, 2, f, Flag::USE_GROUPING_CHAR}, "12,345.00"},
    {"-431876523.5320197",
     {0, 5, f, Flag::USE_GROUPING_CHAR},
     "-431,876,523.53202"},
    // Combination of special flags.
    {1,
     {4, 0, f,
      Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::ALWAYS_PRINT_SIGN |
          Flag::SIGN_SPACE | Flag::ZERO_PAD},
     "+01."},
    {1,
     {4, 0, f,
      Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::SIGN_SPACE | Flag::ZERO_PAD},
     " 01."},
    {1,
     {4, 2, f,
      Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT |
          Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::ALWAYS_PRINT_SIGN |
          Flag::SIGN_SPACE | Flag::ZERO_PAD},
     "+01."},
    {1,
     {4, 2, f,
      Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT |
          Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::SIGN_SPACE | Flag::ZERO_PAD},
     " 01."},
    {"-431876523.532",
     {15, 0, f, Flag::ZERO_PAD | Flag::USE_GROUPING_CHAR},
     "-000431,876,524"},

    // Overflow due to rounding; this is a special case for NUMERIC
    // formatting.
    {kMaxNumericValueStr, kDefaultSpec,
     "100000000000000000000000000000.000000"},
    {kMaxNumericValueStr, {0, 1}, "100000000000000000000000000000.0"},
    {kMaxNumericValueStr, {0, 0}, "100000000000000000000000000000"},
    {kMinNumericValueStr, kDefaultSpec,
     "-100000000000000000000000000000.000000"},
    {kMinNumericValueStr, {0, 1}, "-100000000000000000000000000000.0"},
    {kMinNumericValueStr, {0, 0}, "-100000000000000000000000000000"},
    {kMinNumericValueStr,
     {45, 6},
     "       -100000000000000000000000000000.000000"},

    // %e and %E produce mantissa/exponent notation.
    {0, {0, 0, e}, "0e+00"},
    {0, {0, 0, E}, "0E+00"},
    {0, {0, 6, e}, "0.000000e+00"},
    {0, {0, 6, E}, "0.000000E+00"},
    {"1e-9", {0, 0, e}, "1e-09"},
    {"-1e-9", {0, 0, E}, "-1E-09"},
    {"1.5", {0, 6, e}, "1.500000e+00"},
    {"1.5", {0, 6, E}, "1.500000E+00"},
    {1500, {0, 6, e}, "1.500000e+03"},
    {1500, {0, 6, E}, "1.500000E+03"},
    {"0.00015", {0, 6, e}, "1.500000e-04"},
    {"0.00015", {14, 6, e}, "  1.500000e-04"},
    {"0.000000001", {0, 6, e}, "1.000000e-09"},
    {"0.000000009", {0, 6, e}, "9.000000e-09"},
    {kMaxNumericValueStr,
     {0, 37, e},
     "9.9999999999999999999999999999999999999e+28"},
    {kMinNumericValueStr,
     {0, 37, e},
     "-9.9999999999999999999999999999999999999e+28"},
    {kMaxNumericValueStr,
     {0, 40, e},
     "9.9999999999999999999999999999999999999000e+28"},
    {kMinNumericValueStr,
     {0, 40, e},
     "-9.9999999999999999999999999999999999999000e+28"},

    // Rounding cases.
    {"0.00015", {0, 0, e}, "2e-04"},
    {"0.00099995", {0, 3, e}, "1.000e-03"},
    {"0.00059995", {0, 3, e}, "6.000e-04"},
    {"0.00059995", {0, 2, e}, "6.00e-04"},
    {"-0.00015555", {0, 0, e}, "-2e-04"},
    {"0.00015555", {0, 0, e}, "2e-04"},
    {"-0.00015555", {0, 1, e}, "-1.6e-04"},
    {"0.00015555", {0, 1, e}, "1.6e-04"},
    {"-0.00015", {0, 0, e}, "-2e-04"},
    {"0.00015", {0, 0, e}, "2e-04"},
    {"0.00095555", {0, 0, e}, "1e-03"},
    {"-0.00095555", {0, 0, e}, "-1e-03"},
    {"0.00095555", {0, 3, e}, "9.556e-04"},
    {"-0.00095555", {0, 3, e}, "-9.556e-04"},
    {"-0.000955549", {0, 3, e}, "-9.555e-04"},
    {kMaxNumericValueStr, {0, 6, e}, "1.000000e+29"},
    {kMinNumericValueStr, {0, 6, e}, "-1.000000e+29"},
    {"0.99999999", {0, 3, e}, "1.000e+00"},
    {"-0.99999999", {0, 3, e}, "-1.000e+00"},

    // Zero-padding.
    {"1.5", {11, 3, e, Flag::ZERO_PAD}, "001.500e+00"},
    {"-1.5", {11, 3, e, Flag::ZERO_PAD}, "-01.500e+00"},
    // Sign flag.
    {"1.5", {0, 3, e, Flag::ALWAYS_PRINT_SIGN}, "+1.500e+00"},
    {"-1.5", {0, 3, e, Flag::ALWAYS_PRINT_SIGN}, "-1.500e+00"},
    // Blank space if no sign.
    {"1.5", {0, 3, e, Flag::SIGN_SPACE}, " 1.500e+00"},
    {"-1.5", {0, 3, e, Flag::SIGN_SPACE}, "-1.500e+00"},
    {"1.5", {10, 3, e, Flag::SIGN_SPACE}, " 1.500e+00"},
    {"1.5", {11, 3, e, Flag::SIGN_SPACE}, "  1.500e+00"},
    // Decimal point.
    {1, {0, 0, e, Flag::ALWAYS_PRINT_DECIMAL_POINT}, "1.e+00"},
    // Remove trailing zeros after decimal point.
    {0, {0, 3, e, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "0e+00"},
    {1, {0, 3, e, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "1e+00"},
    {"1.2",
     {0, 3, e, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT},
     "1.2e+00"},
    {10, {0, 3, e, Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT}, "1e+01"},
    // Left justify padding
    {"0.001433", {12, 2, e, Flag::LEFT_JUSTIFY}, "1.43e-03    "},
    {"12", {6, 0, E, Flag::LEFT_JUSTIFY}, "1E+01 "},
    {"9.999", {12, 2, e, Flag::LEFT_JUSTIFY}, "1.00e+01    "},
    {"-431876.532876872", {15, 3, e, Flag::LEFT_JUSTIFY}, "-4.319e+05     "},
    // Combination of special flags.
    {1,
     {9, 0, e,
      Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::ALWAYS_PRINT_SIGN |
          Flag::SIGN_SPACE | Flag::ZERO_PAD},
     "+001.e+00"},
    {1,
     {9, 0, e,
      Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::SIGN_SPACE | Flag::ZERO_PAD},
     " 001.e+00"},
    {10,
     {9, 2, e,
      Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT |
          Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::ALWAYS_PRINT_SIGN |
          Flag::SIGN_SPACE | Flag::ZERO_PAD},
     "+001.e+01"},
    {10,
     {9, 2, e,
      Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT |
          Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::SIGN_SPACE | Flag::ZERO_PAD},
     " 001.e+01"},

    // %g and %G. remove_trailing_zeros_after_decimal_point is most commonly
    // used, and thus most of the test cases have this flag.
    {0, {0, 0, g, true}, "0"},
    {0, {0, 0, G, true}, "0"},
    {0, {0, 6, g, true}, "0"},
    {0, {3, 6, G, true}, "  0"},
    {0, {3, kuint32max, G, true}, "  0"},
    {"1e-9", {0, 0, g, true}, "1e-09"},
    {"-1e-9", {0, 0, G, true}, "-1E-09"},
    {"1e-9", {7, 0, g, true}, "  1e-09"},
    {"-1e-9", {7, 0, G, true}, " -1E-09"},
    {"-1.5e-5", {0, 6, g, true}, "-1.5e-05"},
    {"1.5e-5", {0, 6, G, true}, "1.5E-05"},
    {"-0.000099999", {0, 6, g, true}, "-9.9999e-05"},
    {"0.000099999", {0, 6, G, true}, "9.9999E-05"},
    {"-0.000099999", {0, 7, g, true}, "-9.9999e-05"},
    {"0.000099999", {0, 7, G, true}, "9.9999E-05"},
    {"-0.000099999", {0, kuint32max, g, true}, "-9.9999e-05"},
    {"0.000099999", {0, kuint32max, G, true}, "9.9999E-05"},
    {"-1e-4", {0, 6, g, true}, "-0.0001"},
    {"1e-4", {0, 6, G, true}, "0.0001"},
    {"-1e-4", {0, 7, g, true}, "-0.0001"},
    {"1e-4", {0, 7, G, true}, "0.0001"},
    {"-1e-4", {0, kuint32max, g, true}, "-0.0001"},
    {"1e-4", {0, kuint32max, G, true}, "0.0001"},
    {"-1.5e-4", {0, 6, g, true}, "-0.00015"},
    {"1.5e-4", {0, 6, G, true}, "0.00015"},
    {"-1.5e-4", {8, 6, g, true}, "-0.00015"},
    {"1.5e-4", {8, 6, G, true}, " 0.00015"},
    {-1, {0, 6, g, true}, "-1"},
    {1, {0, 6, G, true}, "1"},
    {-1, {0, kuint32max, g, true}, "-1"},
    {1, {0, kuint32max, G, true}, "1"},
    {"1.5", {0, 6, g, true}, "1.5"},
    {"-1.5", {0, 6, G, true}, "-1.5"},
    {1500, {0, 6, g, true}, "1500"},
    {-1500, {0, 6, G, true}, "-1500"},
    {999999, {0, 6, g, true}, "999999"},
    {-999999, {0, 6, G, true}, "-999999"},
    {1000000, {0, 6, g, true}, "1e+06"},
    {-1000000, {0, 6, G, true}, "-1E+06"},
    {1000000, {0, 7, g, true}, "1000000"},
    {-1000000, {0, 7, G, true}, "-1000000"},
    {1000000, {0, kuint32max, g, true}, "1000000"},
    {-1000000, {0, kuint32max, G, true}, "-1000000"},
    {1234560, {0, 6, g, true}, "1.23456e+06"},
    {-1234560, {0, 6, G, true}, "-1.23456E+06"},
    {1234560, {0, 7, g, true}, "1234560"},
    {-1234560, {0, 7, G, true}, "-1234560"},
    {1234560, {0, kuint32max, g, true}, "1234560"},
    {-1234560, {0, kuint32max, G, true}, "-1234560"},
    {1234560, {12, 6, g, true}, " 1.23456e+06"},
    {-1234560, {12, 6, G, true}, "-1.23456E+06"},
    {1234560, {12, 7, g, true}, "     1234560"},
    {-1234560, {12, 7, G, true}, "    -1234560"},
    {1234560, {12, kuint32max, g, true}, "     1234560"},
    {-1234560, {12, kuint32max, G, true}, "    -1234560"},
    {-1230000, {0, 6, g, true}, "-1.23e+06"},
    {1230000, {0, 6, G, true}, "1.23E+06"},
    {-1230000, {10, 6, g, true}, " -1.23e+06"},
    {1230000, {10, 6, G, true}, "  1.23E+06"},
    {kMaxNumericValueStr, {0, 38, g, true}, kMaxNumericValueStr},
    {kMinNumericValueStr, {0, 38, G, true}, kMinNumericValueStr},
    {kMaxNumericValueStr, {0, 40, G, true}, kMaxNumericValueStr},
    {kMinNumericValueStr, {0, 40, g, true}, kMinNumericValueStr},
    {kMaxNumericValueStr, {0, kuint32max, g, true}, kMaxNumericValueStr},
    {kMinNumericValueStr, {0, kuint32max, G, true}, kMinNumericValueStr},

    // Rounding cases.
    {"-0.000014999", {0, 0, g, true}, "-1e-05"},
    {"0.000014999", {0, 1, G, true}, "1E-05"},
    {"0.000014999", {0, 2, g, true}, "1.5e-05"},
    {"-0.000014999", {0, 3, G, true}, "-1.5E-05"},
    {"0.000015", {0, 0, g, true}, "2e-05"},
    {"-0.000015", {0, 1, G, true}, "-2E-05"},
    {"-0.000015", {0, 2, g, true}, "-1.5e-05"},
    {"0.000015", {0, 3, G, true}, "1.5E-05"},
    {"-0.000094999", {0, 0, g, true}, "-9e-05"},
    {"0.000094999", {0, 1, G, true}, "9E-05"},
    {"-0.000094999", {0, 2, g, true}, "-9.5e-05"},
    {"0.000094999", {0, 3, G, true}, "9.5E-05"},
    {"0.000095", {0, 0, g, true}, "0.0001"},
    {"-0.000095", {0, 1, G, true}, "-0.0001"},
    {"0.000095", {0, 2, g, true}, "9.5e-05"},
    {"-0.000095", {0, 3, G, true}, "-9.5E-05"},
    {"-0.000149999", {0, 0, g, true}, "-0.0001"},
    {"0.000149999", {0, 1, G, true}, "0.0001"},
    {"-0.000149999", {0, 2, g, true}, "-0.00015"},
    {"0.000149999", {0, 3, G, true}, "0.00015"},
    {"-0.00015", {0, 0, g, true}, "-0.0002"},
    {"0.00015", {0, 1, G, true}, "0.0002"},
    {"0.00015", {0, 2, g, true}, "0.00015"},
    {"-0.00015", {0, 3, G, true}, "-0.00015"},
    {"1.5", {0, 0, g, true}, "2"},
    {"-1.5", {0, 1, G, true}, "-2"},
    {"-1.5", {0, 2, g, true}, "-1.5"},
    {"1.5", {0, 3, G, true}, "1.5"},
    {15, {0, 0, g, true}, "2e+01"},
    {-15, {0, 1, G, true}, "-2E+01"},
    {15, {0, 2, g, true}, "15"},
    {-15, {0, 3, G, true}, "-15"},
    {kMaxNumericValueStr, {0, 6, g, true}, "1e+29"},
    {kMinNumericValueStr, {0, 6, G, true}, "-1E+29"},
    {kMaxNumericValueStr, {0, 37, g, true}, "100000000000000000000000000000"},
    {kMinNumericValueStr, {0, 37, G, true}, "-100000000000000000000000000000"},

    // Zero-padding.
    {"1.5", {4, 1, g, Flag::ZERO_PAD}, "0002"},
    {"-1.5", {4, 1, g, Flag::ZERO_PAD}, "-002"},
    {"1.5", {4, 2, g, Flag::ZERO_PAD}, "01.5"},
    {"-1.5", {4, 2, g, Flag::ZERO_PAD}, "-1.5"},
    {"0.000095", {9, 0, g, Flag::ZERO_PAD}, "0000.0001"},
    {"-0.000095", {9, 1, G, Flag::ZERO_PAD}, "-000.0001"},
    {"-0.000095", {9, 2, g, Flag::ZERO_PAD}, "-09.5e-05"},
    {"0.000095", {9, 3, G, Flag::ZERO_PAD}, "09.50E-05"},
    // Sign flag.
    {"0.000095", {9, 0, g, Flag::ALWAYS_PRINT_SIGN}, "  +0.0001"},
    {"-0.000095", {9, 1, G, Flag::ALWAYS_PRINT_SIGN}, "  -0.0001"},
    {"-0.000095", {9, 2, g, Flag::ALWAYS_PRINT_SIGN}, " -9.5e-05"},
    {"0.000095", {9, 3, G, Flag::ALWAYS_PRINT_SIGN}, "+9.50E-05"},
    // Blank space if no sign.
    {"0.000095", {9, 0, g, Flag::SIGN_SPACE}, "   0.0001"},
    {"-0.000095", {9, 1, G, Flag::SIGN_SPACE}, "  -0.0001"},
    {"-0.000095", {9, 2, g, Flag::SIGN_SPACE}, " -9.5e-05"},
    {"0.000095", {9, 3, G, Flag::SIGN_SPACE}, " 9.50E-05"},
    // Decimal point.
    {10, {0, 1, g, Flag::ALWAYS_PRINT_DECIMAL_POINT}, "1.e+01"},
    {-10, {0, 1, G, Flag::ALWAYS_PRINT_DECIMAL_POINT}, "-1.E+01"},
    {10, {0, 2, g, Flag::ALWAYS_PRINT_DECIMAL_POINT}, "10."},
    {-10, {0, 2, G, Flag::ALWAYS_PRINT_DECIMAL_POINT}, "-10."},
    // Keep trailing zeros after decimal point.
    {1, {0, 3, g}, "1.00"},
    {-1, {0, 3, G}, "-1.00"},
    {"1e-4", {0, 3, g}, "0.000100"},
    {"-1e-4", {0, 3, G}, "-0.000100"},
    {"1e-5", {0, 3, g}, "1.00e-05"},
    {"-1e-5", {0, 3, G}, "-1.00E-05"},
    // Combination of special flags.
    {10,
     {9, 1, g,
      Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::ALWAYS_PRINT_SIGN |
          Flag::SIGN_SPACE | Flag::ZERO_PAD},
     "+001.e+01"},
    {10,
     {9, 2, G,
      Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::SIGN_SPACE | Flag::ZERO_PAD},
     " 0000010."},
    {10,
     {9, 3, g,
      Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT |
          Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::ALWAYS_PRINT_SIGN |
          Flag::SIGN_SPACE | Flag::ZERO_PAD},
     "+0000010."},
    {10,
     {9, 4, G,
      Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT |
          Flag::ALWAYS_PRINT_DECIMAL_POINT | Flag::SIGN_SPACE | Flag::ZERO_PAD},
     " 0000010."},
};

TEST_F(NumericValueTest, FormatAndAppend) {
  for (const FormatTestData& data : kFormatTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(data.input1));
    std::string output = "existing_value;";
    value.FormatAndAppend(data.input2, &output);
    EXPECT_EQ(output, absl::StrCat("existing_value;", data.expected_output))
        << "Input: " << value.ToString();
  }
}

template <typename T>
void TestFormatAndAppend(T value, NumericValue::FormatSpec spec,
                         const Matcher<absl::string_view>& matcher) {
  constexpr int kNumericScale = T::kMaxFractionalDigits;
  constexpr int kNumericPrecision = T::kMaxIntegerDigits + kNumericScale;
  constexpr absl::string_view existing_value("existing_value;");
  std::string output(existing_value);
  value.FormatAndAppend(spec, &output);
  absl::string_view str = output;
  str.remove_prefix(existing_value.size());

  EXPECT_GE(str.size(), spec.minimum_size);
  EXPECT_THAT(str, matcher);
  size_t decimal_point_pos = str.find('.');
  if (spec.format_flags & Flag::ALWAYS_PRINT_DECIMAL_POINT) {
    EXPECT_NE(decimal_point_pos, absl::string_view::npos) << str;
  }
  if (decimal_point_pos == absl::string_view::npos) {
    decimal_point_pos = str.size();
  }

  int num_fractional_digits = 0;
  size_t pos = decimal_point_pos;
  char last_fractional_digit = '\0';
  while (++pos < str.size() && str[pos] >= '0' && str[pos] <= '9') {
    ++num_fractional_digits;
    last_fractional_digit = str[pos];
  }
  if (spec.format_flags & Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT) {
    EXPECT_NE(last_fractional_digit, '0') << str;
  }
  if (spec.mode == NumericValue::FormatSpec::GENERAL_FORMAT_LOWER_CASE ||
      spec.mode == NumericValue::FormatSpec::GENERAL_FORMAT_UPPER_CASE) {
    int num_significant_digits = 0;
    for (size_t pos = 0; pos < str.size(); ++pos) {
      if (str[pos] >= '1' && str[pos] <= '9') {
        ++num_significant_digits;
        while (++pos < str.size()) {
          if (str[pos] != '.') continue;
          if (str[pos] < '0' || str[pos] > '9') break;
          ++num_significant_digits;
        }
        break;
      }
    }
    EXPECT_LE(num_significant_digits, std::max<uint>(spec.precision, 1)) << str;
  } else {
    if (spec.format_flags & Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT) {
      EXPECT_LE(num_fractional_digits, spec.precision) << str;
    } else {
      EXPECT_EQ(num_fractional_digits, spec.precision) << str;
    }
  }

  bool is_precise = false;
  switch (spec.mode) {
    case NumericValue::FormatSpec::DEFAULT:
      is_precise = (spec.precision >= kNumericScale);
      break;
    case NumericValue::FormatSpec::E_NOTATION_LOWER_CASE:
    case NumericValue::FormatSpec::E_NOTATION_UPPER_CASE:
      is_precise = (spec.precision >= kNumericPrecision - 1);
      break;
    case NumericValue::FormatSpec::GENERAL_FORMAT_LOWER_CASE:
    case NumericValue::FormatSpec::GENERAL_FORMAT_UPPER_CASE:
      is_precise = (spec.precision >= kNumericPrecision);
      break;
  }

  if (is_precise) {
    // The formatted result should not be rounded.
    EXPECT_THAT(T::FromStringStrict(absl::StripLeadingAsciiWhitespace(str)),
                IsOkAndHolds(value));
  } else {
    if (str.size() >= 4 &&
        (str[str.size() - 4] == 'e' || str[str.size() - 4] == 'E')) {
      const char* p = &str.back();
      int exponent = (p[-1] - '0') * 10 + (p[0] - '0');
      if (p[-2] == '-') {
        exponent = -exponent;
      }
      num_fractional_digits -= exponent;
    }
    BigNumericValue expected_value(value);
    auto status_or_expected_value = expected_value.Round(num_fractional_digits);
    if (std::is_same_v<T, NumericValue>) {
      GOOGLESQL_ASSERT_OK(status_or_expected_value.status());
    }
    str = absl::StripLeadingAsciiWhitespace(str);
    if (status_or_expected_value.ok()) {
      EXPECT_THAT(BigNumericValue::FromStringStrict(str),
                  IsOkAndHolds(status_or_expected_value.value()))
          << "original input: " << value;
    } else {
      EXPECT_FALSE(BigNumericValue::FromStringStrict(str).ok())
          << "original input: " << value;
    }
  }
}

void SetFormatFlag(bool condition, const Flag flag, FormatSpec* fmt_spec) {
  if (condition) {
    fmt_spec->format_flags = fmt_spec->format_flags | flag;
    return;
  }
  uint32_t tmp = static_cast<uint32_t>(flag);
  fmt_spec->format_flags = fmt_spec->format_flags & ~tmp;
}

template <typename T>
void TestFormatWithRandomValues(absl::BitGen* random) {
  // The index of kRegExps is defined as the following:
  // 1st (least significant) bit: zero_pad;
  // 2nd bit: sign_space;
  // 3rd bit: always_print_sign;
  // 4-6th bit: mode (0=DEFAULT, 1=E_NOTATION_LOWER_CASE,
  // 2=E_NOTATION_UPPER_CASE, 3=GENERAL_FORMAT_LOWER_CASE,
  // 4=GENERAL_FORMAT_UPPER_CASE);
  static constexpr absl::string_view kRegExps[] = {
      R"( *\-?[0-9]+\.?[0-9]*)",
      R"(\-?0*[0-9]+\.?[0-9]*)",
      R"( *\-?[0-9]+\.?[0-9]*)",
      R"([ |\-]0*[0-9]+\.?[0-9]*)",
      R"( *[\+|\-][0-9]+\.?[0-9]*)",
      R"([\+|\-]0*[0-9]+\.?[0-9]*)",
      R"( *[\+|\-][0-9]+\.?[0-9]*)",
      R"([\+|\-]0*[0-9]+\.?[0-9]*)",

      R"( *\-?[0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"(\-?0*[0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"( *\-?[0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"([ |\-]0*[0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"( *[\+|\-][0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"([\+|\-]0*[0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"( *[\+|\-][0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",
      R"([\+|\-]0*[0-9]\.?[0-9]*e[\+|\-][0-9][0-9])",

      R"( *\-?[0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"(\-?0*[0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"( *\-?[0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"([ |\-]0*[0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"( *[\+|\-][0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"([\+|\-]0*[0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"( *[\+|\-][0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",
      R"([\+|\-]0*[0-9]\.?[0-9]*E[\+|\-][0-9][0-9])",

      R"( *\-?[0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"(\-?0*[0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"( *\-?[0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"([ |\-]0*[0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"( *[\+|\-][0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"([\+|\-]0*[0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"( *[\+|\-][0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",
      R"([\+|\-]0*[0-9]+\.?[0-9]*(e[\+|\-][0-9][0-9])?)",

      R"( *\-?[0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"(\-?0*[0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"( *\-?[0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"([ |\-]0*[0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"( *[\+|\-][0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"([\+|\-]0*[0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"( *[\+|\-][0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
      R"([\+|\-]0*[0-9]+\.?[0-9]*(E[\+|\-][0-9][0-9])?)",
  };

  constexpr uint kNumMatchers = ABSL_ARRAYSIZE(kRegExps);
  Matcher<absl::string_view> matchers[kNumMatchers];
  for (int i = 0; i < kNumMatchers; ++i) {
    matchers[i] = MatchesRegex(std::string(kRegExps[i]));
  }
  constexpr int kNumericScale = T::kMaxFractionalDigits;
  constexpr int kNumericPrecision = T::kMaxIntegerDigits + kNumericScale;
  constexpr FormatSpec full_spec = {0, kNumericScale};
  constexpr FormatSpec exp_full_spec = {0, kNumericPrecision - 1, e};
  constexpr FormatSpec general_full_spec = {0, kNumericPrecision, g, true};
  Matcher<absl::string_view> full_spec_matcher =
      MatchesRegex(absl::Substitute(R"(\-?[0-9]+\.[0-9]{$0})", kNumericScale));
  Matcher<absl::string_view> exp_full_spec_matcher =
      MatchesRegex(absl::Substitute(R"(\-?[0-9]\.[0-9]{$0}e[\+|\-][0-9][0-9])",
                                    kNumericPrecision - 1));
  Matcher<absl::string_view> general_full_spec_matcher =
      MatchesRegex(R"(\-?[0-9]+(\.[0-9]*[1-9])?(e[\+|\-][0-9][0-9])?)");
  for (int i = 0; i < 100000; ++i) {
    T value = MakeRandomNumericValue<T>(random);
    TestFormatAndAppend(value, full_spec, full_spec_matcher);
    TestFormatAndAppend(value, exp_full_spec, exp_full_spec_matcher);
    TestFormatAndAppend(value, general_full_spec, general_full_spec_matcher);

    FormatSpec spec;
    spec.minimum_size = absl::Uniform<uint>(*random, 0, 50);
    spec.precision = absl::Uniform<uint>(*random, 0, 50);
    uint bits = absl::Uniform<uint>(*random, 0, kNumMatchers << 2);
    SetFormatFlag((bits & 1) != 0,
                  Flag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT, &spec);
    SetFormatFlag((bits & 2) != 0, Flag::ALWAYS_PRINT_DECIMAL_POINT, &spec);
    uint matcher_index = bits >> 2;
    static constexpr FormatSpec::Mode kModes[] = {FormatSpec::DEFAULT, e, E, g,
                                                  G};
    uint mode_index = matcher_index >> 3;
    ASSERT_LT(mode_index, ABSL_ARRAYSIZE(kModes));
    spec.mode = kModes[mode_index];
    SetFormatFlag((matcher_index & 0x4) != 0, Flag::ALWAYS_PRINT_SIGN, &spec);
    SetFormatFlag((matcher_index & 0x2) != 0, Flag::SIGN_SPACE, &spec);
    SetFormatFlag((matcher_index & 0x1) != 0, Flag::ZERO_PAD, &spec);
    TestFormatAndAppend(value, spec, matchers[matcher_index]);
  }
}

TEST_F(NumericValueTest, Format_Random) {
  TestFormatWithRandomValues<NumericValue>(&random_);
}

struct SumAggregatorTestData {
  NumericValueWrapper input;
  NumericValueWrapper expected_cumulative_sum;
  NumericValueWrapper expected_cumulative_avg;
};

static constexpr SumAggregatorTestData kSumAggregatorTestData[] = {
    {1, 1, 1},
    {0, 1, "0.5"},
    {-2, -1, "-0.333333333"},
    {"1e-9", "-0.999999999", "-0.25"},
    {kMaxNumericValueStr, "99999999999999999999999999999",
     "19999999999999999999999999999.8"},
    {1, kNumericOverflow /* actual sum = 1e29 */,
     "16666666666666666666666666666.666666667"},
    {0, kNumericOverflow /* actual sum = 1e29 */,
     "14285714285714285714285714285.714285714"},
    {"-1e-9", kMaxNumericValueStr, "1.25e28"},
    {kMaxNumericValueStr, kNumericOverflow /* actual sum = max * 2 */,
     "22222222222222222222222222222.222222222"},
    {kMaxNumericValueStr, kNumericOverflow /* actual sum = max * 3 */, "3e28"},
    {kMinNumericValueStr, kNumericOverflow /* actual sum = max * 2 */,
     "18181818181818181818181818181.818181818"},
    {kMinNumericValueStr, kMaxNumericValueStr,
     "8333333333333333333333333333.333333333"},
    {kMinNumericValueStr, 0, 0},
    {"7e-9", "7e-9", "1e-9" /* rounded up from 5e-10 */},
    {0, "7e-9", 0 /* rounded down from 4.6666666...e-10 */},
};

TEST(NumericSumAggregatorTest, Sum) {
  // Test exactly one input value.
  for (__int128 packed : kNumericValidPackedValues) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue input,
                         NumericValue::FromPackedInt(packed));
    NumericValue::SumAggregator aggregator;
    aggregator.Add(input);
    EXPECT_THAT(aggregator.GetSum(), IsOkAndHolds(input));
  }

  // Test cumulative sum with different inputs.
  CumulativeSumOp<NumericValue> sum_op;
  EXPECT_THAT(sum_op.aggregator.GetSum(), IsOkAndHolds(NumericValue(0)));
  CumulativeSumOp<NumericValue> negated_sum_op;
  for (const SumAggregatorTestData& data : kSumAggregatorTestData) {
    TestUnaryOp(sum_op, data.input, data.expected_cumulative_sum);
    TestUnaryOp(negated_sum_op, -data.input, -data.expected_cumulative_sum);
  }
}

TEST(NumericSumAggregatorTest, Subtract) {
  // Test exactly one input value.
  for (__int128 packed : kNumericValidPackedValues) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue input,
                         NumericValue::FromPackedInt(packed));
    NumericValue::SumAggregator aggregator;
    aggregator.Subtract(input);
    EXPECT_THAT(aggregator.GetSum(), IsOkAndHolds(input.Negate()));
  }

  // Test cumulative sum with different inputs.
  CumulativeSubtractOp<NumericValue> sum_op;
  EXPECT_THAT(sum_op.aggregator.GetSum(), IsOkAndHolds(NumericValue(0)));
  CumulativeSubtractOp<NumericValue> negated_sum_op;
  for (const SumAggregatorTestData& data : kSumAggregatorTestData) {
    TestUnaryOp(sum_op, data.input, -data.expected_cumulative_sum);
    TestUnaryOp(negated_sum_op, -data.input, data.expected_cumulative_sum);
  }
}

TEST(NumericSumAggregatorTest, Avg) {
  // Test repeated inputs with same value.
  for (__int128 packed : kNumericValidPackedValues) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue input,
                         NumericValue::FromPackedInt(packed));
    NumericValue::SumAggregator aggregator;
    for (uint64_t count = 1; count <= 1000; ++count) {
      aggregator.Add(input);
      EXPECT_THAT(aggregator.GetAverage(count), IsOkAndHolds(input));
    }
  }

  // Test cumulative average with different inputs.
  CumulativeAverageOp<NumericValue> avg_op;
  EXPECT_THAT(avg_op.aggregator.GetAverage(0),
              StatusIs(absl::StatusCode::kOutOfRange, "division by zero: AVG"));
  CumulativeAverageOp<NumericValue> negated_avg_op;
  for (const SumAggregatorTestData& data : kSumAggregatorTestData) {
    TestUnaryOp(avg_op, data.input, data.expected_cumulative_avg);
    TestUnaryOp(negated_avg_op, -data.input, -data.expected_cumulative_avg);
  }
}

static constexpr NumericValueWrapper kNumericUnaryAggregatorTestInputs[] = {
    1,
    0,
    -2,
    "1e-9",
    kMaxNumericValueStr,
    1,
    0,
    "-1e-9",
    kMaxNumericValueStr,
    kMaxNumericValueStr,
    kMinNumericValueStr,
    kMinNumericValueStr,
    kMinNumericValueStr,
    "7e-9",
    0};

TEST(NumericSumAggregatorTest, MergeWith) {
  TestAggregatorMergeWith<NumericValue::SumAggregator>(
      kNumericUnaryAggregatorTestInputs);
}

TEST(NumericSumAggregatorTest, Serialization) {
  TestAggregatorSerialization<NumericValue::SumAggregator>(
      kNumericUnaryAggregatorTestInputs);
}

TEST_F(NumericValueTest, VarianceAggregator) {
  using W = NumericValueWrapper;
  struct VarianceTestData {
    std::initializer_list<W> inputs;
    std::optional<double> expect_var_pop;
    std::optional<double> expect_var_samp;
  };

  constexpr W kMax(kMaxNumericValueStr);
  const VarianceTestData kTestData[] = {
      {{}, std::nullopt, std::nullopt},
      {{0}, 0.0, std::nullopt},
      {{kMax}, 0.0, std::nullopt},
      {{0, "1e-9"}, 2.5e-19, 5e-19},
      {{kMax, kMax}, 0.0, 0.0},
      {{kMax, kMinNumericValueStr}, 1e58, 2e58},
      {{2, 2, -3, 2, 2}, 4, 5},
      {{1, -1, 1, 1, 0}, 0.64, 0.8},
      {{2, 4, 2, 5, -3, 2, 2, -W(5), -W(4)}, 4, 5},  // subtract 5 and 4
      {{1, -100, -1, 200, 1, -W(-100), 1, -W(200), 0}, 0.64, 0.8},
      {{kMax, -W(kMax)}, std::nullopt, std::nullopt},
  };

  for (const VarianceTestData& test_data : kTestData) {
    NumericValue::VarianceAggregator agg;
    NumericValue::VarianceAggregator neg_agg;
    uint64_t count = 0;
    for (const W& input : test_data.inputs) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(input));
      // Interpret input.negate as subtraction.
      if (input.negate) {
        // Negate value to get the original value before negation.
        agg.Subtract(value.Negate());
        neg_agg.Subtract(value);
        --count;
      } else {
        agg.Add(value);
        neg_agg.Add(value.Negate());
        ++count;
      }
    }
    VerifyVarianceAggregator(agg, test_data.expect_var_pop,
                             test_data.expect_var_samp, count);
    VerifyVarianceAggregator(neg_agg, test_data.expect_var_pop,
                             test_data.expect_var_samp, count);
  }
}

template <typename NumericType>
void TestVarianceAggregatorManyValues() {
  const int64_t kInputCount = 10000;

  typename NumericType::VarianceAggregator agg1;
  typename NumericType::VarianceAggregator agg2;
  typename NumericType::VarianceAggregator agg3;
  typename NumericType::VarianceAggregator agg4;

  for (int64_t i = 1; i <= kInputCount; i++) {
    agg1.Add(NumericType(i));
    agg2.Add(NumericType(-i));
    agg3.Add(NumericType(i % 2 ? i : -i));
    agg4.Add(NumericType(kInputCount));
  }

  double expect_pvar1 = 8333333.25;
  double expect_svar1 = 8334166.666666667;
  VerifyVarianceAggregator(agg1, expect_pvar1, expect_svar1, kInputCount);
  double expect_pvar2 = 8333333.25;
  double expect_svar2 = 8334166.666666667;
  VerifyVarianceAggregator(agg2, expect_pvar2, expect_svar2, kInputCount);
  double expect_pvar3 = 33338333.25;
  double expect_svar3 = 33341667.41674167;
  VerifyVarianceAggregator(agg3, expect_pvar3, expect_svar3, kInputCount);
  double expect_pvar4 = 0;
  double expect_svar4 = 0;
  VerifyVarianceAggregator(agg4, expect_pvar4, expect_svar4, kInputCount);
}

TEST_F(NumericValueTest, VarianceAggregatorManyValues) {
  TestVarianceAggregatorManyValues<NumericValue>();
}

TEST_F(NumericValueTest, VarianceAggregatorMergeWith) {
  TestAggregatorMergeWith<NumericValue::VarianceAggregator>(
      kNumericUnaryAggregatorTestInputs);
}

TEST_F(NumericValueTest, VarianceAggregatorSerialization) {
  TestAggregatorSerialization<NumericValue::VarianceAggregator>(
      kNumericUnaryAggregatorTestInputs);
}

std::optional<double> operator-(std::optional<double> x) {
  return x.has_value() ? -x.value() : x;
}

template <typename Aggregator, typename Wrapper>
struct BinaryAggregators {
  void Add(const std::pair<Wrapper, Wrapper>& input) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto first_value, GetValue(input.first));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto second_value, GetValue(input.second));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto negated_first_value, GetValue(-input.first));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto negated_second_value, GetValue(-input.second));
    // Interpret input.negate as subtraction.
    if (input.first.negate) {
      // Negate value to get the original value before subtraction.
      agg.Subtract(negated_first_value, negated_second_value);
      neg_agg.Subtract(first_value, second_value);
      partial_neg_agg.Subtract(negated_first_value, second_value);
      --count;
    } else {
      agg.Add(first_value, second_value);
      neg_agg.Add(negated_first_value, negated_second_value);
      partial_neg_agg.Add(first_value, negated_second_value);
      ++count;
    }
  }
  Aggregator agg;
  Aggregator neg_agg;
  Aggregator partial_neg_agg;
  uint64_t count = 0;
};

TEST_F(NumericValueTest, CovarianceAggregator) {
  using W = NumericValueWrapper;
  struct CovarianceTestData {
    std::initializer_list<std::pair<W, W>> inputs;
    std::optional<double> expect_var_pop;
    std::optional<double> expect_var_samp;
  };

  constexpr W kMax(kMaxNumericValueStr);
  // clang-format off
  const CovarianceTestData kTestData[] = {
      {{}, std::nullopt, std::nullopt},
      {{{1, 1}}, 0, std::nullopt},
      {{{kMax, kMax}}, 0, std::nullopt},
      {{{0, "1e-9"}, {"1e-9", 0}}, -2.5e-19, -5e-19},
      {{{kMax, kMax}, {kMax, kMax}}, 0, 0},
      {{{kMax, kMax}, {kMax, kMax}}, 0, 0},
      {{{"1.2", 5}, {"-2.4", 15}, {"3.6", -20}, {"4.8", 30}, {6, 35}},
       16.08,
       20.1},
      {{{100, 3}, {200, 7}, {300, 11}, {400, 13}, {600, 17}}, 816, 1020},
      {{{"1.2", 5},
        {"-2.4", 15},
        {17, "-3.5"},
        {"3.6", -20},
        {"2.4", -8},
        {"4.8", 30},
        {-W("2.4"), -W(-8)},
        {6, 35},
        {-W(17), -W("-3.5")}},
       16.08,
       20.1},
  };
  // clang-format on

  for (const CovarianceTestData& test_data : kTestData) {
    BinaryAggregators<NumericValue::CovarianceAggregator, NumericValueWrapper>
        aggregators;
    for (const std::pair<W, W>& input : test_data.inputs) {
      aggregators.Add(input);
    }
    VerifyCovariance(aggregators.agg, test_data.expect_var_pop,
                     test_data.expect_var_samp, aggregators.count);
    VerifyCovariance(aggregators.neg_agg, test_data.expect_var_pop,
                     test_data.expect_var_samp, aggregators.count);
    VerifyCovariance(aggregators.partial_neg_agg, -test_data.expect_var_pop,
                     -test_data.expect_var_samp, aggregators.count);
  }
}

static constexpr std::pair<NumericValueWrapper, NumericValueWrapper>
    kNumericBinaryAggregatorTestInputs[] = {
        {0, 8},
        {1, 7},
        {-1, 2},
        {kMaxNumericValueStr, "-53.8"},
        {"-123.01", kMinNumericValueStr},
        {kMinNumericValueStr, kMinNumericValueStr},
        {kMinNumericValueStr, kMinNumericValueStr},
        {kMinNumericValueStr, kMaxNumericValueStr},
        {kMaxNumericValueStr, "32.999999999"},
        {kMaxNumericValueStr, kMaxNumericValueStr},
        {kMaxNumericValueStr, kMaxNumericValueStr},
        {"56.999999999", kMaxNumericValueStr}};

TEST_F(NumericValueTest, CovarianceAggregatorMergeWith) {
  TestAggregatorMergeWith<NumericValue::CovarianceAggregator>(
      kNumericBinaryAggregatorTestInputs);
}

TEST_F(NumericValueTest, CovarianceAggregatorSerialization) {
  TestAggregatorSerialization<NumericValue::CovarianceAggregator>(
      kNumericBinaryAggregatorTestInputs);
}

TEST_F(NumericValueTest, CorrelationAggregator) {
  using W = NumericValueWrapper;
  struct CorrelationTestData {
    std::initializer_list<std::pair<W, W>> inputs;
    std::optional<double> expect_corr;
  };

  constexpr W kMax(kMaxNumericValueStr);
  constexpr W kMin(kMinNumericValueStr);
  // clang-format off
  const CorrelationTestData kTestData[] = {
      {{}, std::nullopt},
      {{{1, 1}}, std::nullopt},
      {{{kMax, kMax}}, std::nullopt},
      {{{1, 1}, {1, 1}}, std::numeric_limits<double>::quiet_NaN()},
      {{{1, 1}, {-1, -1}}, 1},
      {{{1, 1}, {kMax, kMax}, {kMin, kMin}}, 1},
      {{{1, 5}, {"1.5", 15}, {2, 20}, {"2.5", 25}, {3, 35}},
       0.98994949366116658},  // sqrt(0.98)
      {{{1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}},
       std::numeric_limits<double>::quiet_NaN()},
      {{{8, -2},
        {1, 5},
        {3, 35},
        {"1.5", 15},
        {2, 20},
        {"2.5", 25},
        {-W(8), -W(-2)},
        {-W(3), -W(35)},
        {3, 35}},
       0.98994949366116658},  // sqrt(0.98)
  };
  // clang-format on
  for (const CorrelationTestData& test_data : kTestData) {
    BinaryAggregators<NumericValue::CorrelationAggregator, NumericValueWrapper>
        aggregators;
    for (const std::pair<W, W>& input : test_data.inputs) {
      aggregators.Add(input);
    }
    VerifyCorrelation(aggregators.agg, test_data.expect_corr,
                      aggregators.count);
    VerifyCorrelation(aggregators.neg_agg, test_data.expect_corr,
                      aggregators.count);
    VerifyCorrelation(aggregators.partial_neg_agg, -test_data.expect_corr,
                      aggregators.count);
  }
}

TEST_F(NumericValueTest, CorrelationAggregatorMergeWith) {
  TestAggregatorMergeWith<NumericValue::CorrelationAggregator>(
      kNumericBinaryAggregatorTestInputs);
}

TEST_F(NumericValueTest, CorrelationAggregatorSerialization) {
  TestAggregatorSerialization<NumericValue::CorrelationAggregator>(
      kNumericBinaryAggregatorTestInputs);
}

TEST_F(NumericValueTest, HasFractionalPart) {
  static constexpr NumericUnaryOpTestData<NumericValueWrapper, bool>
      kTestData[] = {
          {"0.5", true},
          {"0.3", true},
          {"0.2", true},
          {"0.1", true},
          {"0.01", true},
          {"0.001", true},
          {"0.000000001", true},
          {"0.987654321", true},
          {"0.999999999", true},
          {"1.000000001", true},
          {1, false},
          {2, false},
          {3, false},
          {5, false},
          {10, false},
          {9999, false},
          {"99999999999999999999999999999", false},
          {"99999999999999999999999999999.000000001", true},
          {kMaxNumericValueStr, true},
      };

  for (const auto& data : kTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(data.input));
    EXPECT_EQ(data.expected_output, value.HasFractionalPart());
    EXPECT_EQ(data.expected_output, value.Negate().HasFractionalPart());
  }
}

TEST_F(NumericValueTest, Trunc) {
  static constexpr NumericBinaryOpTestData<int64_t> kTestData[] = {
      {0, kint64min, 0},
      {0, -30, 0},
      {0, -29, 0},
      {0, -1, 0},
      {0, 0, 0},
      {0, 1, 0},
      {0, 9, 0},
      {0, 10, 0},
      {0, kint64max, 0},

      {"0.987654321", kint64min, 0},
      {"0.987654321", -30, 0},
      {"0.987654321", -1, 0},
      {"0.987654321", 0, 0},
      {"0.987654321", 1, "0.9"},
      {"0.987654321", 2, "0.98"},
      {"0.987654321", 4, "0.9876"},
      {"0.987654321", 5, "0.98765"},
      {"0.987654321", 8, "0.98765432"},
      {"0.987654321", 9, "0.987654321"},
      {"0.987654321", 10, "0.987654321"},
      {"0.987654321", kint64max, "0.987654321"},

      {"1234567899876543210.123456789", kint64min, 0},
      {"1234567899876543210.123456789", -30, 0},
      {"1234567899876543210.123456789", -19, 0},
      {"1234567899876543210.123456789", -18, 1000000000000000000LL},
      {"1234567899876543210.123456789", -17, 1200000000000000000LL},
      {"1234567899876543210.123456789", -16, 1230000000000000000LL},
      {"1234567899876543210.123456789", -15, 1234000000000000000LL},
      {"1234567899876543210.123456789", -14, 1234500000000000000LL},
      {"1234567899876543210.123456789", -9, 1234567899000000000LL},
      {"1234567899876543210.123456789", -8, 1234567899800000000LL},
      {"1234567899876543210.123456789", -2, 1234567899876543200LL},
      {"1234567899876543210.123456789", 0, 1234567899876543210LL},
      {"1234567899876543210.123456789", 1, "1234567899876543210.1"},
      {"1234567899876543210.123456789", 3, "1234567899876543210.123"},
      {"1234567899876543210.123456789", 4, "1234567899876543210.1234"},
      {"1234567899876543210.123456789", 8, "1234567899876543210.12345678"},
      {"1234567899876543210.123456789", 9, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", 10, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", kint64max,
       "1234567899876543210.123456789"},

      {kMaxNumericValueStr, kint64min, 0},
      {kMaxNumericValueStr, -30, 0},
      {kMaxNumericValueStr, -29, 0},
      {kMaxNumericValueStr, -28, "90000000000000000000000000000"},
      {kMaxNumericValueStr, -27, "99000000000000000000000000000"},
      {kMaxNumericValueStr, -1, "99999999999999999999999999990"},
      {kMaxNumericValueStr, 0, "99999999999999999999999999999"},
      {kMaxNumericValueStr, 1, "99999999999999999999999999999.9"},
      {kMaxNumericValueStr, 8, "99999999999999999999999999999.99999999"},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 10, kMaxNumericValueStr},
      {kMaxNumericValueStr, kint64max, kMaxNumericValueStr},
  };

  NumericTruncOp op;
  for (const NumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Round) {
  static constexpr NumericBinaryOpTestData<int64_t> kCommonTestData[] = {
      {0, kint64min, 0},
      {0, -30, 0},
      {0, -29, 0},
      {0, -1, 0},
      {0, 0, 0},
      {0, 1, 0},
      {0, 9, 0},
      {0, 10, 0},
      {0, kint64max, 0},

      {"0.987654321", kint64min, 0},
      {"0.987654321", -30, 0},
      {"0.987654321", -1, 0},
      {"0.987654321", 0, 1},
      {"0.987654321", 1, 1},
      {"0.987654321", 2, "0.99"},
      {"0.987654321", 4, "0.9877"},
      {"0.987654321", 5, "0.98765"},
      {"0.987654321", 8, "0.98765432"},
      {"0.987654321", 9, "0.987654321"},
      {"0.987654321", 10, "0.987654321"},
      {"0.987654321", kint64max, "0.987654321"},

      {"1234567899876543210.123456789", kint64min, 0},
      {"1234567899876543210.123456789", -30, 0},
      {"1234567899876543210.123456789", -19, 0},
      {"1234567899876543210.123456789", -18, 1000000000000000000LL},
      {"1234567899876543210.123456789", -17, 1200000000000000000LL},
      {"1234567899876543210.123456789", -16, 1230000000000000000LL},
      {"1234567899876543210.123456789", -15, 1235000000000000000LL},
      {"1234567899876543210.123456789", -14, 1234600000000000000LL},
      {"1234567899876543210.123456789", -9, 1234567900000000000LL},
      {"1234567899876543210.123456789", -8, 1234567899900000000LL},
      {"1234567899876543210.123456789", -2, 1234567899876543200LL},
      {"1234567899876543210.123456789", 0, 1234567899876543210LL},
      {"1234567899876543210.123456789", 1, "1234567899876543210.1"},
      {"1234567899876543210.123456789", 3, "1234567899876543210.123"},
      {"1234567899876543210.123456789", 4, "1234567899876543210.1235"},
      {"1234567899876543210.123456789", 8, "1234567899876543210.12345679"},
      {"1234567899876543210.123456789", 9, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", 10, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", kint64max,
       "1234567899876543210.123456789"},

      {"12341234123412341234123412341.234123412", kint64min, 0},
      {"12341234123412341234123412341.234123412", -30, 0},
      {"12341234123412341234123412341.234123412", -29, 0},
      {"12341234123412341234123412341.234123412", -28,
       "10000000000000000000000000000"},
      {"12341234123412341234123412341.234123412", -27,
       "12000000000000000000000000000"},
      {"12341234123412341234123412341.234123412", -1,
       "12341234123412341234123412340"},
      {"12341234123412341234123412341.234123412", 0,
       "12341234123412341234123412341"},
      {"12341234123412341234123412341.234123412", 1,
       "12341234123412341234123412341.2"},
      {"12341234123412341234123412341.234123412", 8,
       "12341234123412341234123412341.23412341"},
      {"12341234123412341234123412341.234123412", 9,
       "12341234123412341234123412341.234123412"},
      {"12341234123412341234123412341.234123412", 10,
       "12341234123412341234123412341.234123412"},
      {"12341234123412341234123412341.234123412", kint64max,
       "12341234123412341234123412341.234123412"},

      {"56785678567856785678567856785.567856785", kint64min, 0},
      {"56785678567856785678567856785.567856785", -30, 0},
      {"56785678567856785678567856785.567856785", -29, kNumericOverflow},
      {"56785678567856785678567856785.567856785", -28,
       "60000000000000000000000000000"},
      {"56785678567856785678567856785.567856785", -27,
       "57000000000000000000000000000"},
      {"56785678567856785678567856785.567856785", -1,
       "56785678567856785678567856790"},
      {"56785678567856785678567856785.567856785", 0,
       "56785678567856785678567856786"},
      {"56785678567856785678567856785.567856785", 1,
       "56785678567856785678567856785.6"},
      {"56785678567856785678567856785.567856785", 7,
       "56785678567856785678567856785.5678568"},
      {"56785678567856785678567856785.567856785", 9,
       "56785678567856785678567856785.567856785"},
      {"56785678567856785678567856785.567856785", 10,
       "56785678567856785678567856785.567856785"},
      {"56785678567856785678567856785.567856785", kint64max,
       "56785678567856785678567856785.567856785"},
      {"99999999999999999999999999999.999999995", 8, kNumericOverflow},
      {"99999999999999999999999999999.995", 2, kNumericOverflow},

      {kMaxNumericValueStr, kint64min, 0},
      {kMaxNumericValueStr, -30, 0},
      {kMaxNumericValueStr, -29, kNumericOverflow},
      {kMaxNumericValueStr, -28, kNumericOverflow},
      {kMaxNumericValueStr, -1, kNumericOverflow},
      {kMaxNumericValueStr, 0, kNumericOverflow},
      {kMaxNumericValueStr, 1, kNumericOverflow},
      {kMaxNumericValueStr, 8, kNumericOverflow},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 10, kMaxNumericValueStr},
      {kMaxNumericValueStr, kint64max, kMaxNumericValueStr},
  };

  static constexpr NumericBinaryOpTestData<int64_t>
      kRoundAwayFromZeroTestData[] = {
          {"0.05", 1, "0.1"},
          {"0.25", 1, "0.3"},
          {"56785678567856785678567856785.567856785", 8,
           "56785678567856785678567856785.56785679"},
          {"56785678567856750000000000", -11, "56785678567856800000000000"},
          {"56785678567856850000000000", -11, "56785678567856900000000000"},
          {"0.987655000", 5, "0.98766"},
          {"0.987665000", 5, "0.98767"},
      };

  static constexpr NumericBinaryOpTestData<int64_t> kRoundToEvenTestData[] = {
      {"0.05", 1, 0},
      {"0.25", 1, "0.2"},
      {"56785678567856750000000000", -11, "56785678567856800000000000"},
      {"56785678567856850000000000", -11, "56785678567856800000000000"},
      {"56785678500000000000000000", -18, "56785678000000000000000000"},
      {"56785678567856850000000001", -11, "56785678567856900000000000"},
      {"56785678500000000000000001", -18, "56785679000000000000000000"},
      {"56785678567856785675000000000", -10, "56785678567856785680000000000"},
      {"56785678567856785678500000000", -9, "56785678567856785678000000000"},
      {"56785678567856785678550000000", -8, "56785678567856785678600000000"},
      {"56785678567856785678565000000", -7, "56785678567856785678560000000"},
      {"56785678567856785678567500000", -6, "56785678567856785678568000000"},
      {"56785678567856785678567850000", -5, "56785678567856785678567800000"},
      {"56785678567856785678567850001", -5, "56785678567856785678567900000"},
      {"56785678567856785678567855000", -4, "56785678567856785678567860000"},
      {"56785678567856785678567856500", -3, "56785678567856785678567856000"},
      {"56785678567856785678567856750", -2, "56785678567856785678567856800"},
      {"56785678567856785678567856785", -1, "56785678567856785678567856780"},
      {"56785678567856785678567856785.5", 0, "56785678567856785678567856786"},
      {"56785678567856785678567856785.65", 1,
       "56785678567856785678567856785.6"},
      {"56785678567856785678567856785.565", 2,
       "56785678567856785678567856785.56"},
      {"56785678567856785678567856785.5675", 3,
       "56785678567856785678567856785.568"},
      {"56785678567856785678567856785.56785", 4,
       "56785678567856785678567856785.5678"},
      {"56785678567856785678567856785.567855", 5,
       "56785678567856785678567856785.56786"},
      {"56785678567856785678567856785.56786501", 5,
       "56785678567856785678567856785.56787"},
      {"56785678567856785678567856785.5678565", 6,
       "56785678567856785678567856785.567856"},
      {"56785678567856785678567856785.56785685", 7,
       "56785678567856785678567856785.5678568"},
      {"56785678567856785678567856785.567856785", 8,
       "56785678567856785678567856785.56785678"},
      {"56785678567856785678567856785.5678567885", 9,
       "56785678567856785678567856785.5678567885"},
      {"56785678567856785678567856785.56785678885", 10,
       "56785678567856785678567856785.56785678885"},
      {"56785678567856785678567856785.56785678885", -30, 0},
      {"0.987655000", 5, "0.98766"},
      {"0.987665000", 5, "0.98766"},
  };

  NumericRoundHalfAwayFromZeroOp away_from_zero_op;
  NumericRoundHalfEvenOp half_even_op;
  for (const NumericBinaryOpTestData<int64_t>& data :
       kRoundAwayFromZeroTestData) {
    TestBinaryOp(away_from_zero_op, data.input1, data.input2,
                 data.expected_output);
    TestBinaryOp(away_from_zero_op, -data.input1, data.input2,
                 -data.expected_output);
  }
  for (const NumericBinaryOpTestData<int64_t>& data : kRoundToEvenTestData) {
    TestBinaryOp(half_even_op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(half_even_op, -data.input1, data.input2,
                 -data.expected_output);
  }
  for (const NumericBinaryOpTestData<int64_t>& data : kCommonTestData) {
    TestBinaryOp(half_even_op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(half_even_op, -data.input1, data.input2,
                 -data.expected_output);
    TestBinaryOp(away_from_zero_op, data.input1, data.input2,
                 data.expected_output);
    TestBinaryOp(away_from_zero_op, -data.input1, data.input2,
                 -data.expected_output);
  }
}

TEST_F(NumericValueTest, FromStringWithRounding) {
  static constexpr NumericFromStringWithRoundingOpTestData kCommonTestData[] = {
      {"0", kint32min, 0},
      {"0", -30, 0},
      {"0", -29, 0},
      {"0", -1, 0},
      {"0", 0, 0},
      {"0", 1, 0},
      {"0", 9, 0},
      {"0", 10, 0},
      {"0", kint32max, 0},
      {"1.26520e3", 2, "1265.2"},
      {"15.15", 9, "15.15"},
      {"1500.00e-3", 0, "2"},
      {"155000.00e-5", 1, "1.6"},
      {"12345678912.00e-10", 10, "1.234567891"},
      {"12.00e-10", 10, "0.000000001"},
      {"1.00e-10", 10, 0},
      {"12.00e-20", 10, 0},
      {"12e3", 100, 12000},
      {"12e10", -10, 120000000000},
      {"12e9", -10, 10000000000},
      {"1", -50, 0},
      {"1", -3, 0},
      {"1234567891234567891234567891", -27, "1000000000000000000000000000"},
      {"1E4294967296", -4294967296, kNumericInvalid},
      {"1e9223372036854775807", -9223372036854775807, kNumericInvalid},
      {"1e9223372036854775807", std::numeric_limits<int64_t>::min(), 0},
      {"1e9223372036854775808", std::numeric_limits<int64_t>::min(),
       kNumericInvalid},
      {"1e-9223372036854775808", 9223372036854775807, 0},

      {"0.987654321", kint32min, 0},
      {"0.987654321", -30, 0},
      {"0.987654321", -1, 0},
      {"0.987654321", 0, 1},
      {"0.987654321", 1, 1},
      {"0.987654321", 2, "0.99"},
      {"0.987654321", 4, "0.9877"},
      {"0.987654321", 5, "0.98765"},
      {"0.987654321", 8, "0.98765432"},
      {"0.987654321", 9, "0.987654321"},
      {"0.987654321", 10, "0.987654321"},
      {"0.987654321", kint32max, "0.987654321"},

      {"1234567899876543210.123456789", kint32min, 0},
      {"1234567899876543210.123456789", -30, 0},
      {"1234567899876543210.123456789", -19, 0},
      {"1234567899876543210.123456789", -18, 1000000000000000000LL},
      {"1234567899876543210.123456789", -17, 1200000000000000000LL},
      {"1234567899876543210.123456789", -16, 1230000000000000000LL},
      {"1234567899876543210.123456789", -15, 1235000000000000000LL},
      {"1234567899876543210.123456789", -14, 1234600000000000000LL},
      {"1234567899876543210.123456789", -9, 1234567900000000000LL},
      {"1234567899876543210.123456789", -8, 1234567899900000000LL},
      {"1234567899876543210.123456789", -2, 1234567899876543200LL},
      {"1234567899876543210.123456789", 0, 1234567899876543210LL},
      {"1234567899876543210.123456789", 1, "1234567899876543210.1"},
      {"1234567899876543210.123456789", 3, "1234567899876543210.123"},
      {"1234567899876543210.123456789", 4, "1234567899876543210.1235"},
      {"1234567899876543210.123456789", 8, "1234567899876543210.12345679"},
      {"1234567899876543210.123456789", 9, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", 10, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", kint32max,
       "1234567899876543210.123456789"},
      {"56785678567856785678567856785.56785678885", 10,
       "56785678567856785678567856785.567856789"},

      {"12341234123412341234123412341.234123412", kint32min, 0},
      {"12341234123412341234123412341.234123412", -30, 0},
      {"12341234123412341234123412341.234123412", -29, 0},
      {"12341234123412341234123412341.234123412", -28,
       "10000000000000000000000000000"},
      {"12341234123412341234123412341.234123412", -27,
       "12000000000000000000000000000"},
      {"12341234123412341234123412341.234123412", -1,
       "12341234123412341234123412340"},
      {"12341234123412341234123412341.234123412", 0,
       "12341234123412341234123412341"},
      {"12341234123412341234123412341.234123412", 1,
       "12341234123412341234123412341.2"},
      {"12341234123412341234123412341.234123412", 8,
       "12341234123412341234123412341.23412341"},
      {"12341234123412341234123412341.234123412", 9,
       "12341234123412341234123412341.234123412"},
      {"12341234123412341234123412341.234123412", 10,
       "12341234123412341234123412341.234123412"},
      {"12341234123412341234123412341.234123412", kint32max,
       "12341234123412341234123412341.234123412"},

      {"56785678567856785678567856785.567856785", kint32min, 0},
      {"56785678567856785678567856785.567856785", -30, 0},
      {"56785678567856785678567856785.567856785", -29, kNumericInvalid},
      {"56785678567856785678567856785.567856785", -28,
       "60000000000000000000000000000"},
      {"56785678567856785678567856785.567856785", -27,
       "57000000000000000000000000000"},
      {"56785678567856785678567856785.567856785", -1,
       "56785678567856785678567856790"},
      {"56785678567856785678567856785.567856785", 0,
       "56785678567856785678567856786"},
      {"56785678567856785678567856785.567856785", 1,
       "56785678567856785678567856785.6"},
      {"56785678567856785678567856785.567856785", 7,
       "56785678567856785678567856785.5678568"},
      {"56785678567856785678567856785.567856785", 9,
       "56785678567856785678567856785.567856785"},
      {"56785678567856785678567856785.567856785", 10,
       "56785678567856785678567856785.567856785"},
      {"5678499991", -6, "5678000000"},
      {"5678500001", -6, "5679000000"},
      {"5678513123123123.1235001", 3, "5678513123123123.124"},
      {"56785678567856785678567856785.567856785", kint32max,
       "56785678567856785678567856785.567856785"},
      {"99999999999999999999999999999.999999995", 8, kNumericInvalid},
      {"99999999999999999999999999999.995", 2, kNumericInvalid},

      {kMaxNumericValueStr, kint32min, 0},
      {kMaxNumericValueStr, -30, 0},
      {kMaxNumericValueStr, -29, kNumericInvalid},
      {kMaxNumericValueStr, -28, kNumericInvalid},
      {kMaxNumericValueStr, -1, kNumericInvalid},
      {kMaxNumericValueStr, 0, kNumericInvalid},
      {kMaxNumericValueStr, 1, kNumericInvalid},
      {kMaxNumericValueStr, 8, kNumericInvalid},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 10, kMaxNumericValueStr},
      {kMaxNumericValueStr, kint32max, kMaxNumericValueStr},
  };

  static constexpr NumericFromStringWithRoundingOpTestData
      kRoundAwayFromZeroTestData[] = {
          {"0.05", 1, "0.1"},
          {"0.25", 1, "0.3"},
          {"56785678567856785678567856785.567856785", 8,
           "56785678567856785678567856785.56785679"},
          {"5678499991", -6, "5678000000"},
          {"5678500000", -6, "5679000000"},
          {"5678513123123123.1225000", 3, "5678513123123123.123"},
          {"56785678567856750000000000", -11, "56785678567856800000000000"},
          {"56785678567856850000000000", -11, "56785678567856900000000000"},
          {"0.987655000", 5, "0.98766"},
          {"0.987665000", 5, "0.98767"},
      };

  static constexpr NumericFromStringWithRoundingOpTestData
      kRoundToEvenTestData[] = {
          {"0.05", 1, 0},
          {"0.25", 1, "0.2"},
          {"56785678567856750000000000", -11, "56785678567856800000000000"},
          {"56785678567856850000000000", -11, "56785678567856800000000000"},
          {"56785678500000000000000000", -18, "56785678000000000000000000"},
          {"56785678567856850000000001", -11, "56785678567856900000000000"},
          {"56785678500000000000000001", -18, "56785679000000000000000000"},
          {"56785678567856785675000000000", -10,
           "56785678567856785680000000000"},
          {"5678499991", -6, "5678000000"},
          {"5678500000", -6, "5678000000"},
          {"5678513123123123.1225000", 3, "5678513123123123.122"},
          {"56785678567856785678500000000", -9,
           "56785678567856785678000000000"},
          {"56785678567856785678550000000", -8,
           "56785678567856785678600000000"},
          {"56785678567856785678565000000", -7,
           "56785678567856785678560000000"},
          {"56785678567856785678567500000", -6,
           "56785678567856785678568000000"},
          {"56785678567856785678567850000", -5,
           "56785678567856785678567800000"},
          {"56785678567856785678567850001", -5,
           "56785678567856785678567900000"},
          {"56785678567856785678567855000", -4,
           "56785678567856785678567860000"},
          {"56785678567856785678567856500", -3,
           "56785678567856785678567856000"},
          {"56785678567856785678567856750", -2,
           "56785678567856785678567856800"},
          {"56785678567856785678567856785", -1,
           "56785678567856785678567856780"},
          {"56785678567856785678567856785.5", 0,
           "56785678567856785678567856786"},
          {"56785678567856785678567856785.65", 1,
           "56785678567856785678567856785.6"},
          {"56785678567856785678567856785.565", 2,
           "56785678567856785678567856785.56"},
          {"56785678567856785678567856785.5675", 3,
           "56785678567856785678567856785.568"},
          {"56785678567856785678567856785.56785", 4,
           "56785678567856785678567856785.5678"},
          {"56785678567856785678567856785.567855", 5,
           "56785678567856785678567856785.56786"},
          {"56785678567856785678567856785.56786501", 5,
           "56785678567856785678567856785.56787"},
          {"56785678567856785678567856785.5678565", 6,
           "56785678567856785678567856785.567856"},
          {"56785678567856785678567856785.56785685", 7,
           "56785678567856785678567856785.5678568"},
          {"56785678567856785678567856785.567856785", 8,
           "56785678567856785678567856785.56785678"},
          {"56785678567856785678567856785.5678567885", 9,
           "56785678567856785678567856785.567856788"},
          {"56785678567856785678567856785.56785678885", -30, 0},
          {"0.987655000", 5, "0.98766"},
          {"0.987665000", 5, "0.98766"},
          {"5000015.000001", -1, "5000020"},
          {"5000024.9999", -1, "5000020"},
          {"5000025.0000", -1, "5000020"},
      };

  for (const NumericFromStringWithRoundingOpTestData data : kCommonTestData) {
    TestNumericFromStringWithRoundingOp<NumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ true, data.expected_output);
    TestNumericFromStringWithRoundingOp<NumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ false, data.expected_output);
  }

  for (const NumericFromStringWithRoundingOpTestData data :
       kRoundAwayFromZeroTestData) {
    TestNumericFromStringWithRoundingOp<NumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ false, data.expected_output);
  }

  for (const NumericFromStringWithRoundingOpTestData data :
       kRoundToEvenTestData) {
    TestNumericFromStringWithRoundingOp<NumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ true, data.expected_output);
  }
}

TEST_F(NumericValueTest, DivideToIntegralValue) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {6, 2, 3},
      {1, 3, 0},
      {kMaxNumericValueStr, 3, "33333333333333333333333333333"},
      {kMaxNumericValueStr, 2, "49999999999999999999999999999"},
      {kMaxNumericValueStr, 1, "99999999999999999999999999999"},
      {5, "2.3", 2},
      {"5.2", 2, 2},
      {kMaxNumericValueStr, "0.3", kNumericOverflow},
      {"1e20", "1e-9", kNumericOverflow},

      {0, 0, kDivisionByZero},
      {"0.1", 0, kDivisionByZero},
      {1, 0, kDivisionByZero},
      {kMaxNumericValueStr, 0, kDivisionByZero},
  };

  NumericDivideToIntegralValueOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
  }
}

TEST_F(NumericValueTest, Mod) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {5, 2, 1},
      {5, "0.001", 0},
      {kMaxNumericValueStr, 3, "0.999999999"},
      {kMaxNumericValueStr, 2, "1.999999999"},
      {kMaxNumericValueStr, 1, "0.999999999"},
      {5, "2.3", "0.4"},
      {5, "0.3", "0.2"},
      {"5.2", 2, "1.2"},
      {1, 0, kDivisionByZero},
      {"1e-9", 0, kDivisionByZero},
  };

  NumericModOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, OperatorsTest) {
  TestComparisonOperators<NumericValue>(kSortedNumericValueStringPairs);
}

TEST_F(NumericValueTest, HashCode) {
  TestHashCode<NumericValue>(kSortedNumericValueStringPairs);
}

TEST_F(NumericValueTest, Ceiling) {
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {999, 999},
      {"999.000000001", 1000},
      {"999.999999999", 1000},
      {-999, -999},
      {"-999.000000001", -999},
      {"-999.999999999", -999},
      {"0.999999999", 1},
      {"-0.999999999", 0},
      {kMinNumericValueStr, "-99999999999999999999999999999"},
      {kMaxNumericValueStr, kNumericOverflow},
      {"99999999999999999999999999999.000000001", kNumericOverflow},
  };

  NumericCeilingOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, Floor) {
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {999, 999},
      {"999.000000001", 999},
      {"999.999999999", 999},
      {-999, -999},
      {"-999.000000001", -1000},
      {"-999.999999999", -1000},
      {"0.999999999", 0},
      {"-0.999999999", -1},
      {kMaxNumericValueStr, "99999999999999999999999999999"},
      {kMinNumericValueStr, kNumericOverflow},
      {"-99999999999999999999999999999.000000001", kNumericOverflow},
  };

  NumericFloorOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

class BigNumericValueTest : public NumericValueTest {
 protected:
  BigNumericValue MakeRandomBigNumeric() {
    return MakeRandomNumericValue<BigNumericValue>(&random_);
  }
};

constexpr absl::string_view kMinBigNumericValueStr =
    "-578960446186580977117854925043439539266."
    "34992332820282019728792003956564819968";
constexpr absl::string_view kMaxBigNumericValueStr =
    "578960446186580977117854925043439539266."
    "34992332820282019728792003956564819967";
constexpr std::array<uint64_t, 4> kMinBigNumericValuePacked =
    BigNumericValue::MinValue().ToPackedLittleEndianArray();
constexpr std::array<uint64_t, 4> kMaxBigNumericValuePacked =
    BigNumericValue::MaxValue().ToPackedLittleEndianArray();

using BigNumericStringTestData =
    std::pair<absl::string_view, std::array<uint64_t, 4>>;
constexpr BigNumericStringTestData kSortedBigNumericValueStringPairs[] = {
    {kMinBigNumericValueStr, kMinBigNumericValuePacked},
    {"-10000000000000000000",
     {0xb600000000000000, 0x140234ab79b5257c, 0xd737834a3765da8e, kuint64max}},
    {"-3.40282366920938463463374607431768211456",
     {0, 0, kuint64max, kuint64max}},
    {"-0.0000000000000000000000000000000000001",
     {kuint64max - 9, kuint64max, kuint64max, kuint64max}},
    {"-0.00000000000000000000000000000000000001",
     {kuint64max, kuint64max, kuint64max, kuint64max}},
    {"0", {0, 0, 0, 0}},
    {"0.00000000000000000000000000000000000001", {1, 0, 0, 0}},
    {"0.0000000000000000000000000000000000001", {10, 0, 0, 0}},
    {"1", {687399551400673280ULL, 5421010862427522170ULL, 0, 0}},
    {"10", {6873995514006732800ULL, 17316620476856118468ULL, 2, 0}},
    {"10.01", {1346846287407874048ULL, 17370830585480393690ULL, 2, 0}},
    {"1000000000", {0x5986800000000000, 0xb4a05bc8a8a4de84, 0x118427b3, 0}},
    {kMaxBigNumericValueStr, kMaxBigNumericValuePacked},
};
constexpr BigNumericStringTestData kBigNumericValueValidFromStringPairs[] = {
    {"-578960446186580977117854925043439539266349923328202820197287920039565648"
     "19968e-38",
     kMinBigNumericValuePacked},
    {"-0."
     "5789604461865809771178549250434395392663499233282028201972879200395656481"
     "9968e39",
     kMinBigNumericValuePacked},
    {"-1e19",
     {0xb600000000000000, 0x140234ab79b5257c, 0xd737834a3765da8e, kuint64max}},
    {"-0.0000000001e29",
     {0xb600000000000000, 0x140234ab79b5257c, 0xd737834a3765da8e, kuint64max}},
    {"-34028236692.0938463463374607431768211456e-10",
     {0, 0, kuint64max, kuint64max}},
    {"-0.000000000340282366920938463463374607431768211456e10",
     {0, 0, kuint64max, kuint64max}},
    {"-0000003.40282366920938463463374607431768211456",
     {0, 0, kuint64max, kuint64max}},
    {"0000000000", {0, 0, 0, 0}},
    {"   0000   ", {0, 0, 0, 0}},
    {" 0000e000 ", {0, 0, 0, 0}},
    {" 00.00e01 ", {0, 0, 0, 0}},
    {" .00e0002 ", {0, 0, 0, 0}},
    {" 00.e0003 ", {0, 0, 0, 0}},
    {" +00e0004 ", {0, 0, 0, 0}},
    {" -00e0005 ", {0, 0, 0, 0}},
    {" 000e+006 ", {0, 0, 0, 0}},
    {" 000e-007 ", {0, 0, 0, 0}},
    {"  1e-38   ", {1, 0, 0, 0}},
    {"10000000000e-48", {1, 0, 0, 0}},
    {"1.0000000e-38", {1, 0, 0, 0}},
    {"  1e-37   ", {10, 0, 0, 0}},
    {"  1e+9    ", {0x5986800000000000, 0xb4a05bc8a8a4de84, 0x118427b3, 0}},
    {"+0.00001e14", {0x5986800000000000, 0xb4a05bc8a8a4de84, 0x118427b3, 0}},
    {"00001000000000", {0x5986800000000000, 0xb4a05bc8a8a4de84, 0x118427b3, 0}},
    {"100000e+4", {0x5986800000000000, 0xb4a05bc8a8a4de84, 0x118427b3, 0}},
    {"5789604461865809771178549250434395392663499233282028201972879200395656481"
     "9967e-38",
     kMaxBigNumericValuePacked},
    {"0."
     "5789604461865809771178549250434395392663499233282028201972879200395656481"
     "9967e39",
     kMaxBigNumericValuePacked},
    // exponent is small negative number while coefficient is 0
    {"0.0E-122337203655840000", {0, 0, 0, 0}},
    {"-0.0E-122337203655840000", {0, 0, 0, 0}},
    {"0.00000000000000000000000000000000000000000000E-122337203655840000",
     {0, 0, 0, 0}},
    {"-0.00000000000000000000000000000000000000000000E-122337203655840000",
     {0, 0, 0, 0}},
    {"0.0E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"-0.0E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    // exponent below int64min
    {"0E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"-0.00000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"+000000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"   -.00000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999   ",
     {0, 0, 0, 0}},
    // exponent is large positive number while coefficient is 0
    {"0.0E122337203655840000", {0, 0, 0, 0}},
    {"-0.0E122337203655840000", {0, 0, 0, 0}},
    {"0.00000000000000000000000000000000000000000000E122337203655840000",
     {0, 0, 0, 0}},
    {"-0.00000000000000000000000000000000000000000000E122337203655840000",
     {0, 0, 0, 0}},
    {"0.0E99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"-0.0E99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
};
constexpr BigNumericStringTestData kBigNumericValueNonStrictStringPairs[] = {
    {"1e-9223372036854775808", {0, 0, 0, 0}},
    {"-578960446186580977117854925043439539266349923328202820197287920039565648"
     "199684e-39",
     kMinBigNumericValuePacked},
    {"-578960446186580977117854925043439539266."
     "349923328202820197287920039565648199684",
     kMinBigNumericValuePacked},
    {"-0."
     "5789604461865809771178549250434395392663499233282028201972879200395656481"
     "99684e39",
     kMinBigNumericValuePacked},
    {"0.000000000000000000000000000000000000001", {0, 0, 0, 0}},
    {"0.000000000000000000000000000000000000001000", {0, 0, 0, 0}},
    {"1e-39", {0, 0, 0, 0}},
    {"4.999e-39", {0, 0, 0, 0}},
    {"5.000e-39", {1, 0, 0, 0}},
    {"-3.402823669209384634633746074317682114564",
     {0, 0, kuint64max, kuint64max}},
    {"-3.402823669209384634633746074317682114565",
     {kuint64max, kuint64max, kuint64max - 1, kuint64max}},
    {"5789604461865809771178549250434395392663499233282028201972879200395656481"
     "99674e-39",
     kMaxBigNumericValuePacked},
    {"578960446186580977117854925043439539266."
     "349923328202820197287920039565648199674",
     kMaxBigNumericValuePacked},
    {"0."
     "5789604461865809771178549250434395392663499233282028201972879200395656481"
     "99674e39",
     kMaxBigNumericValuePacked},
    // exponent below int64min
    {"1E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"-1E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"99999999999999999999999999999999999999999999999999999999999"
     "99999999999999999999999999999999999999999999999999999999999"
     "E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
    {"-99999999999999999999999999999999999999999999999999999999999"
     "99999999999999999999999999999999999999999999999999999999999"
     "E-99999999999999999999999999999999999999999999999999999999999",
     {0, 0, 0, 0}},
};
constexpr absl::string_view kBigNumericValueInvalidStrings[] = {
    // Invalid format
    "",
    "              ",
    "e",
    "1.0e",
    "1.0f",
    "1..0",
    "1.2.3",
    "e.",
    ".e",
    "e10",
    "1.0e+",
    "1.0e-",
    "1.0e-+1",
    "1.0e+-1",
    "1.0ee+1",
    "1.0ee-1",
    "1.0e10e10",
    "1.0e1.0",
    "1.0e1.0",
    "1.0fe1.0",
    "1.0e1.0f",
    "1.0e +10",
    "1.0e1 0",
    "nan",
    "inf",
    "+inf",
    "-inf",
    "1 2 3",
    "++1",
    "--1",
    "+-1",
    "-+1",
    // Overflow
    "578960446186580977117854925043439539266."
    "34992332820282019728792003956564819968",
    "-578960446186580977117854925043439539266."
    "34992332820282019728792003956564819969",
    "578960446186580977117854925043439539266."
    "349923328202820197287920039565648199675",
    "-578960446186580977117854925043439539266."
    "349923328202820197287920039565648199685",
    "0.578960446186580977117854925043439539266"
    "34992332820282019728792003956564819968e39",
    "-0.578960446186580977117854925043439539266"
    "34992332820282019728792003956564819969e39",
    "0.578960446186580977117854925043439539266"
    "349923328202820197287920039565648199675e39",
    "-0.578960446186580977117854925043439539266"
    "349923328202820197287920039565648199685e39",
    "578960446186580977117854925043439539266"
    "34992332820282019728792003956564819968e-38",
    "-578960446186580977117854925043439539266"
    "34992332820282019728792003956564819969e-38",
    "578960446186580977117854925043439539266"
    "349923328202820197287920039565648199675e-39",
    "-578960446186580977117854925043439539266"
    "349923328202820197287920039565648199685e-39",
    "1000000000000000000000000000000000000000",
    "-1000000000000000000000000000000000000000",
    "1.0e40",
    "-1.0e40",
    "0.0000000001e50",
    "-0.0000000001e50",
    // The integer part fits in 256-bit integer, but overflows in scaling.
    "9999999999999999999999999999999999999999999999999999999999999999999999999",
    "9999999999999999999999999999999999999999999999999999999999999999999999999"
    ".999999999",
    "-999999999999999999999999999999999999999999999999999999999999999999999999",
    "-999999999999999999999999999999999999999999999999999999999999999999999999"
    ".999999999",
    // Exponent overflows.
    "1e9223372036854775808",
    "1e9223372036854775770",  // overflows when adding 38 to the exponent
    "0e-9999999999999999999999999999999999999999999999999ABC",
};

struct BigNumericAddTestData {
  BigNumericValueWrapper addend1;
  BigNumericValueWrapper addend2;
  BigNumericValueWrapper sum;
};

// Cases that do not involve the min value or overflow. Can apply common math.
static constexpr BigNumericAddTestData kNormalBigNumericAddTestData[] = {
    {kMaxBigNumericValueStr, 0, kMaxBigNumericValueStr},
    {0, 0, 0},
    {"1e-38", 0, "1e-38"},
    {"1e-38", "2e-38", "3e-38"},
    {"1e-38", "-2e-38", "-1e-38"},
    {1, 2, 3},
    {1, -1, 0},
    {1, "1e-38", "1.00000000000000000000000000000000000001"},
    {1, "-1e-38", "0.99999999999999999999999999999999999999"},
};

// Cases without overflow, rounding, and zero
static constexpr BigNumericBinaryOpTestData<>
    kNormalBigNumericMultiplyTestData[] = {
        {1, "1e-38", "1e-38"},
        {1, 1, 1},
        {"0.5", 6, 3},
        {2, 3, 6},
        // 10^38 * 5^-38 = 2^38
        {"1e38", "2.74877906944e-27", "274877906944"},
        {4294967296, 4294967296, "18446744073709551616"},
        // 2^38 * (5^19 * 2^-19) = 10^19
        {"274877906944", "3.63797880709171295166015625e7", "1e19"},
        // 5^38 * 2^38 = 10^38
        {"363797880709171295166015625", "274877906944", "1e38"},
        {1, kMaxBigNumericValueStr, kMaxBigNumericValueStr},
        // Two factors of 2^255 - 1 x and y. (x / 1e19) * (y / 1e19) =
        // (2^255 - 1) / 1e38
        {"145040486610750631737778315940.6158317435996246631",
         "3991716104.3478428820870502057", kMaxBigNumericValueStr},
};

constexpr std::pair<int, absl::string_view> kIntStringPairs[] = {
    {0, "0"},
    {1, "1"},
    {7, "7"},
    {10, "10"},
    {33, "33"},
    {100, "100"},
    {511, "511"},
    {1000, "1000"},
};

template <typename T>
void NumericLimitConstructorCheck() {
  EXPECT_EQ(BigNumericValue(std::numeric_limits<T>::min()).ToString(),
            absl::StrFormat("%d", std::numeric_limits<T>::min()));
  EXPECT_EQ(BigNumericValue(std::numeric_limits<T>::max()).ToString(),
            absl::StrFormat("%d", std::numeric_limits<T>::max()));
}

TEST_F(BigNumericValueTest, IntegerConstructor) {
  for (const auto& p : kIntStringPairs) {
    EXPECT_EQ(BigNumericValue(p.first).ToString(), p.second);
    EXPECT_EQ(BigNumericValue(static_cast<long>(p.first)).ToString(), p.second);
    EXPECT_EQ(BigNumericValue(static_cast<long long>(p.first)).ToString(),
              p.second);
    EXPECT_EQ(BigNumericValue(static_cast<__int128>(p.first)).ToString(),
              p.second);

    EXPECT_EQ(BigNumericValue(static_cast<unsigned int>(p.first)).ToString(),
              p.second);
    EXPECT_EQ(BigNumericValue(static_cast<unsigned long>(p.first)).ToString(),
              p.second);
    EXPECT_EQ(BigNumericValue(
        static_cast<unsigned long long>(p.first)).ToString(),
        p.second);
    EXPECT_EQ(BigNumericValue(
        static_cast<unsigned __int128>(p.first)).ToString(),
        p.second);

    if (p.first != 0) {
      std::string negated_str = absl::StrCat("-", p.second);
      EXPECT_EQ(BigNumericValue(-p.first).ToString(), negated_str);
      EXPECT_EQ(BigNumericValue(static_cast<long>(-p.first)).ToString(),
                negated_str);
      EXPECT_EQ(BigNumericValue(static_cast<long long>(-p.first)).ToString(),
                negated_str);
      EXPECT_EQ(BigNumericValue(static_cast<__int128>(-p.first)).ToString(),
                negated_str);
    }
  }
  NumericLimitConstructorCheck<int64_t>();
  NumericLimitConstructorCheck<uint64_t>();
  NumericLimitConstructorCheck<int>();
  NumericLimitConstructorCheck<unsigned int>();
  NumericLimitConstructorCheck<long>();                // NOLINT
  NumericLimitConstructorCheck<unsigned long>();       // NOLINT
  NumericLimitConstructorCheck<long long>();           // NOLINT
  NumericLimitConstructorCheck<unsigned long long>();  // NOLINT
  EXPECT_EQ(BigNumericValue(kint128min).ToString(),
            "-170141183460469231731687303715884105728");
  EXPECT_EQ(BigNumericValue(kint128max).ToString(),
            "170141183460469231731687303715884105727");
  EXPECT_EQ(BigNumericValue(kuint128max).ToString(),
            "340282366920938463463374607431768211455");
}

TEST_F(BigNumericValueTest, NumericValueConstructor) {
  for (const NumericStringTestData& pair : kSortedNumericValueStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromPackedInt(pair.second));
    EXPECT_EQ(BigNumericValue(value).ToString(), value.ToString());
  }
}

TEST_F(BigNumericValueTest, Add) {
  static constexpr BigNumericBinaryOpTestData<> kSpecialTestData[] = {
      {kMaxBigNumericValueStr, kMaxBigNumericValueStr, kBigNumericOverflow},
      {kMaxBigNumericValueStr, "1e-38", kBigNumericOverflow},
      {kMinBigNumericValueStr, kMinBigNumericValueStr, kBigNumericOverflow},
      {kMinBigNumericValueStr, "-1e-38", kBigNumericOverflow},

      {kMinBigNumericValueStr, 0, kMinBigNumericValueStr},
      {kMinBigNumericValueStr, kMaxBigNumericValueStr, "-1e-38"},
  };
  NumericAddOp op;
  for (const BigNumericAddTestData& data : kNormalBigNumericAddTestData) {
    TestBinaryOp(op, data.addend1, data.addend2, data.sum);
    TestBinaryOp(op, -data.addend1, -data.addend2, -data.sum);
    TestBinaryOp(op, data.addend2, data.addend1, data.sum);
    TestBinaryOp(op, -data.addend2, -data.addend1, -data.sum);
    TestBinaryOp(op, data.sum, -data.addend1, data.addend2);
    TestBinaryOp(op, -data.sum, data.addend1, -data.addend2);
    TestBinaryOp(op, data.sum, -data.addend2, data.addend1);
    TestBinaryOp(op, -data.sum, data.addend2, -data.addend1);
  }
  for (const BigNumericBinaryOpTestData<>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Subtract) {
  static constexpr BigNumericBinaryOpTestData<> kSpecialTestData[] = {
      {kMaxBigNumericValueStr, kMinBigNumericValueStr, kBigNumericOverflow},
      {kMaxBigNumericValueStr, "-1e-38", kBigNumericOverflow},
      {kMinBigNumericValueStr, kMaxBigNumericValueStr, kBigNumericOverflow},
      {kMinBigNumericValueStr, "1e-38", kBigNumericOverflow},
      {0, kMinBigNumericValueStr, kBigNumericOverflow},
      {"1e-38", kMinBigNumericValueStr, kBigNumericOverflow},

      {kMinBigNumericValueStr, 0, kMinBigNumericValueStr},
      {kMinBigNumericValueStr, kMinBigNumericValueStr, 0},
      {"-1e-38", kMaxBigNumericValueStr, kMinBigNumericValueStr},
      {"-1e-38", kMinBigNumericValueStr, kMaxBigNumericValueStr},
  };
  NumericSubtractOp op;
  for (const BigNumericAddTestData& data : kNormalBigNumericAddTestData) {
    TestBinaryOp(op, data.sum, data.addend1, data.addend2);
    TestBinaryOp(op, -data.sum, -data.addend1, -data.addend2);
    TestBinaryOp(op, data.sum, data.addend2, data.addend1);
    TestBinaryOp(op, -data.sum, -data.addend2, -data.addend1);
    TestBinaryOp(op, data.addend1, data.sum, -data.addend2);
    TestBinaryOp(op, -data.addend1, -data.sum, data.addend2);
    TestBinaryOp(op, data.addend2, data.sum, -data.addend1);
    TestBinaryOp(op, -data.addend2, -data.sum, data.addend1);
  }
  for (const BigNumericBinaryOpTestData<>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, MultiplyAndDivideByPowerOfTwo) {
  // Multiplying by this, then shifting right by 96
  constexpr FixedInt<64, 4> one_minus_epsilon = FixedInt<64, 4>(
      std::array<uint64_t, 4>{0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFULL, 0, 0});

  static constexpr NumericMultiplyAndDivideByPowerOfTwoTestData<
      BigNumericValueWrapper, FixedInt<64, 4>>
      kTestData[] = {
          // Overflow cases
          {kMaxBigNumericValueStr, FixedInt<64, 4>(2), 0, kNumericOverflow},
          {kMaxBigNumericValueStr,
           FixedInt<64, 4>((static_cast<__int128>(1) << 64) | 1), 64,
           kNumericOverflow},

          // Rounding down from 2.4999999999999999999999999999684455637911...
          {"2.5", one_minus_epsilon, 96,
           "2.49999999999999999999999999996844556379"},
          // Rounding up from 2.79999999999999999999999999996465903144609...
          {"2.8", one_minus_epsilon, 96,
           "2.79999999999999999999999999996465903145"},

          {0, FixedInt<64, 4>(1), 0, 0},
          {1, FixedInt<64, 4>(1), 0, 1},
          {0x80000000, FixedInt<64, 4>(1), 31, 1},
          {0xC0000000, FixedInt<64, 4>(1), 31, "1.5"},
          {0x20000000, FixedInt<64, 4>(3), 31, "0.75"},
          {kMaxBigNumericValueStr, FixedInt<64, 4>(1), 0,
           kMaxBigNumericValueStr},
      };

  const FixedInt<64, 4> max_multiplier =
      FixedInt<64, 4>(std::array<uint64_t, 4>{0, 0, 0, 1});
  for (const auto& data : kTestData) {
    const auto num = data.num;
    FixedInt<64, 4> mult = data.mult;
    uint scale_bits = data.scale_bits;
    const auto expected_output = data.expected_output;
    while (mult < max_multiplier) {
      TestBigNumericMultiplyAndDivideByPowerOfTwoOp(num, mult, scale_bits,
                                                    expected_output);
      TestBigNumericMultiplyAndDivideByPowerOfTwoOp(-num, mult, scale_bits,
                                                    -expected_output);
      TestBigNumericMultiplyAndDivideByPowerOfTwoOp(num, -mult, scale_bits,
                                                    -expected_output);
      TestBigNumericMultiplyAndDivideByPowerOfTwoOp(-num, -mult, scale_bits,
                                                    expected_output);
      mult <<= 1;
      scale_bits++;
    }
  }
}

TEST_F(BigNumericValueTest, Multiply) {
  static constexpr BigNumericBinaryOpTestData<> kSpecialTestData[] = {
      {kMinBigNumericValueStr, kMinBigNumericValueStr, kBigNumericOverflow},
      {kMinBigNumericValueStr, kMaxBigNumericValueStr, kBigNumericOverflow},
      {-1, kMinBigNumericValueStr, kBigNumericOverflow},
      // Two factors of -(2^255 + 1) x and y. (x / 1e19) * (y / 1e19) =
      // -(2^255 + 1) / 1e38 -> Overflow
      {"-112712448241465033605533872070906047752.1979626733333956899",
       "5.1366149455494753931", kBigNumericOverflow},
      // Two factors of -(2^256 + 1) x and y. (x / 2) * (y / 1e38) =
      // -(2^255 + 0.5) / 1e38 round down to -(2^255 + 1) / 1e38 -> Overflow
      {"-619463180776448.5",
       "934616397153579777691635.58199606896584051237541638188580280321",
       kBigNumericOverflow},
      // Two factors of -(2^258 + 1) x and y. (x / 8 / 1e19) * (y / 1e19) =
      // -(2^255 + 0.125) / 1e38 round up to -(2^255) / 1e38
      {"-972775953663059703615317563714681.0319961263583002625625",
       "595163.1966296685834686149", kMinBigNumericValueStr},
      // -(2^127 / 1e19) * (2^128 / 1e19) = -2^255 / 1e38
      {"-17014118346046923173.1687303715884105728",
       "34028236692093846346.3374607431768211456", kMinBigNumericValueStr},
      {1, kMinBigNumericValueStr, kMinBigNumericValueStr},
      // -((2^127 + 0.5) / 1e19) * ((2^128 - 1) / 1e19) =
      // -(2^255 + 0.5) / 1e38 round down to -(2^255) / 1e38
      {"-17014118346046923173.16873037158841057285",
       "34028236692093846346.3374607431768211455", kMinBigNumericValueStr},
      // 1.5 * (-(2^128 - 1) / 1e38) round down to -((3 * 2^127 - 1) / 1e38)
      {"1.5", "-3.40282366920938463463374607431768211455",
       "-5.10423550381407695195061911147652317183"},
      // 0.5 * (-(2^128 - 1) / 1e38) round down to -(2^127 / 1e38)
      {"0.5", "-3.40282366920938463463374607431768211455",
       "-1.70141183460469231731687303715884105728"},
      {0, 0, 0},
      {0, "1e-38", 0},
      {0, 1, 0},
      {"1e-38", "0.1", 0},
      {"2e-38", "3e-38", 0},
      {"0.49999999999999999999999999999999999999", "1e-38", 0},
      {"0.5", "1e-38", "1e-38"},
      // 0.5 * ((2^128 - 1) / 1e38) round up to (2^127 / 1e38)
      {"0.5", "3.40282366920938463463374607431768211455",
       "1.70141183460469231731687303715884105728"},
      // 1.5 * ((2^128 - 1) / 1e38) round up to (3 * 2^127 - 1) / 1e38
      {"1.5", "3.40282366920938463463374607431768211455",
       "5.10423550381407695195061911147652317183"},
      {"1e-38", kMaxBigNumericValueStr,
       "5.78960446186580977117854925043439539266"},
      // Two factors of 2^256 - 3 x and y. (x / 2) * (y / 1e38) =
      // (2^255 - 1.5) / 1e38 round up to (2^255 - 1) / 1e38
      {"5375.5",
       "107703552448438466583174574466270958."
       "84407960623722496887680921217367047683",
       kMaxBigNumericValueStr},
      // Two factors of 2^257 - 3 x and y. (x / 4) * (y / 1e38) =
      // (2^255 - 0.75) / 1e38 round down to (2^255 - 1) / 1e38
      {"89670385.25",
       "6456540189633912352549582974423.98396817805489832221748146540855906009",
       kMaxBigNumericValueStr},
      // ((2^127 + 0.5) / 1e19) * ((2^128 - 1) / 1e19) =
      // (2^255 - 0.5) / 1e38 round up to 2^255 / 1e38 -> Overflow
      {"17014118346046923173.16873037158841057285",
       "34028236692093846346.3374607431768211455", kBigNumericOverflow},
      // (2^127 / 1e19) * (2^128 / 1e19) = 2^255 / 1e38 -> Overflow
      {"17014118346046923173.1687303715884105728",
       "34028236692093846346.3374607431768211456", kBigNumericOverflow},
      // ((2^192 - 1) / 1e38) * ((2^192 + 1) / 1e38) -> Overflow when truncating
      // to 4 words
      {"62771017353866807638.35789423207666416102355444464034512895",
       "62771017353866807638.35789423207666416102355444464034512897",
       kBigNumericOverflow},
      {kMaxBigNumericValueStr, kMaxBigNumericValueStr, kBigNumericOverflow},
  };
  NumericMultiplyOp op;
  for (const BigNumericBinaryOpTestData<>& data :
       kNormalBigNumericMultiplyTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, data.expected_output);
    TestBinaryOp(op, data.input2, -data.input1, -data.expected_output);
    TestBinaryOp(op, -data.input2, data.input1, -data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Multiply_Random) {
  TestMultiplication<BigNumericValue, int64_t, __int128>(&random_);
}

TEST_F(BigNumericValueTest, Divide) {
  static constexpr BigNumericBinaryOpTestData<> kTestData[] = {
      // Rounding
      {"100000000000000000000000000000000000000", 3,
       "33333333333333333333333333333333333333."
       "33333333333333333333333333333333333333"},
      {"100000000000000000000000000000000000000",
       "33333333333333333333333333333333333333."
       "33333333333333333333333333333333333333",
       3},
      {1, 3, "0.33333333333333333333333333333333333333"},
      {2, 3, "0.66666666666666666666666666666666666667"},
      {"1", "33333333333333333333333333333333333333",
       "0.00000000000000000000000000000000000003"},
      {"5.78960446186580977117854925043439539266", "1e-38",
       "578960446186580977117854925043439539266"},
      // Ties
      // 1e28 / 2^67 The result ends at 39th digit which is a 5. Round up.
      {"1e28", "147573952589676412928",
       "67762635.78034402712546580005437135696411132813"},
      // In the result, after the decimal dot, the digits following the 39th
      // four is 20 of 9s and repeating 8s. Round down.
      {"4000000000000000000000.00000000000000000049999999999999999999", "9e20",
       "4.44444444444444444444444444444444444444"},
      {"107703552448438466583174574466270958."
       "84407960623722496887680921217367047683",
       "0.00018602920658543391312436052460236258",
       "578960446186580977117854925043439511018."
       "86975388491694661714612717015190558901"},
      {"6456540189633912352549582974423.98396817805489832221748146540855906009",
       "0.00000001115195387208398326804333652621",
       "578960446186580977117854925043064707303."
       "32733510873638103607008167080036872013"},

      // Overflow
      {kMaxBigNumericValueStr, "0.3", kBigNumericOverflow},
      {kMaxBigNumericValueStr, "0.6", kBigNumericOverflow},
      {kMaxBigNumericValueStr, "0.99999999999999999999999999999999999999",
       kBigNumericOverflow},
      {kMaxBigNumericValueStr, "-0.99999999999999999999999999999999999999",
       kBigNumericOverflow},
      {"107703552448438466583174574466270958."
       "84407960623722496887680921217367047683",
       "0.00018602920658543391312436052460236257", kBigNumericOverflow},
      {"6456540189633912352549582974423.98396817805489832221748146540855906009",
       "0.00000001115195387208398326804333652620", kBigNumericOverflow},
      {"1e20", "1e-19", kBigNumericOverflow},

      // Divide by zero
      {0, 0, kDivisionByZero},
      {"0.1", 0, kDivisionByZero},
      {1, 0, kDivisionByZero},
      {kMaxNumericValueStr, 0, kDivisionByZero},
  };
  static constexpr BigNumericBinaryOpTestData<> kSpecialTestData[] = {
      {kMinBigNumericValueStr, 1, kMinBigNumericValueStr},
      // 2^255 / 1e38
      {"134799733335753198973335075435.09815336818572211270286240551805124608",
       "0.00000000023283064365386962890625", kBigNumericOverflow},
      {kMinBigNumericValueStr, -1, kBigNumericOverflow},
      {kMinBigNumericValueStr, "0.99999999999999999999999999999999999999",
       kBigNumericOverflow},
  };

  NumericDivideOp op;
  for (const BigNumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<>& data :
       kNormalBigNumericMultiplyTestData) {
    TestBinaryOp(op, data.expected_output, data.input1, data.input2);
    TestBinaryOp(op, -data.expected_output, data.input1, -data.input2);
    TestBinaryOp(op, data.expected_output, -data.input1, -data.input2);
    TestBinaryOp(op, -data.expected_output, -data.input1, data.input2);
    TestBinaryOp(op, data.expected_output, data.input2, data.input1);
    TestBinaryOp(op, -data.expected_output, data.input2, -data.input1);
    TestBinaryOp(op, data.expected_output, -data.input2, -data.input1);
    TestBinaryOp(op, -data.expected_output, -data.input2, data.input1);
  }
}

TEST_F(BigNumericValueTest, Exp) {
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {1, "2.71828182845904523536028747135266249776"},
      {4, "54.59815003314423907811026120286087840279073704"},
      {"-87.49823353377373599268367527800583988884185657", "1e-38"},
      {"-1.2345", "0.29098021610944063022937592691470534366"},
      {"-0.2", "0.81873075307798185866993550861903942436"},
      {"1e-38", "1.00000000000000000000000000000000000001"},
      {"0.00000000000000000000123456789012345678",
       "1.00000000000000000000123456789012345678"},
      {"1.12345678901234567890123456789012345678",
       "3.07546709031488470896944216141611590685"},
      {"1.2345", "3.43665976117046318315183309106868322791"},
      {"12.12345678901234567890123456789012345678",
       "184140.95240594049518793644384330007811722404"},
      {"70.45678",
       "3971831050584175601252032799800."
       "70334481656820226928188021413354803618"},
      {"77.9",
       "6784848274913494220240250206827303."
       "45301438616356825349293562980103619621"},
      {"-123", "0"},
      {"89.26", kBigNumericOverflow},
      {"91.17133256052693468518555164337158203125", kBigNumericOverflow},
      {kMinBigNumericValueStr, "0"},
      {kMaxBigNumericValueStr, kBigNumericOverflow},
  };

  NumericExpOp op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Exp_WithRandomLosslessDoubleValue) {
  // LN(MaxBigNumericValue) is approximately equals to 89.25, so here we
  // test value with maximum integer bit 7 for a value upper bound 128 to
  // cover overflow cases.
  TestExpWithRandomLosslessDoubleValue<BigNumericValue>(7, 89.5, &random_);
}

TEST_F(BigNumericValueTest, Ln) {
  static constexpr Error kNegativeUndefinedError(
      "LN is undefined for zero or negative value: ");
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {"2.718281828459045235360287471352662497757247094", 1},
      {"54.59815003314423907811026120286087840279073704", 4},
      {"1e-38", "-87.49823353377373599268367527800583988884"},
      {"0.00000000000000000000123456789012345678",
       "-48.14356593055930681062281890349302510108"},
      {"0.12345678901234567890123456789012345678",
       "-2.09186407067839312296298974419574032544"},
      {"1.12345678901234567890123456789012345678",
       "0.11641035085540028597889775880586929036"},
      {"1.23456789", "0.2107210222156525610500017104882905489"},
      {"12345678901234567890123456789012345678."
       "12345678901234567890123456789012345678",
       "85.40636946309534286972068553381009956342"},
      {kMaxBigNumericValueStr, "89.25429750901231790871051569382918497041"},
      {-1, kNegativeUndefinedError},
      {kMinBigNumericValueStr, kNegativeUndefinedError},
      {0, kNegativeUndefinedError},
  };

  NumericLnOp op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Ln_WithRandomLosslessDoubleValue) {
  TestLnWithRandomLosslessDoubleValue<BigNumericValue>(128, &random_);
}

TEST_F(BigNumericValueTest, LnExpRoundTrip) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      BigNumericValue max_valid_exp_value,
      BigNumericValue::FromString("89.25429750901231790871051569382918497041"));
  TestLnExpRoundTrip(max_valid_exp_value, &random_);
}

TEST_F(BigNumericValueTest, Log10) {
  static constexpr Error kNonPositiveUndefinedError(
      "LOG10 is undefined for zero or negative value: ");
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {10, 1},
      {10000, 4},
      {"0.1", -1},
      {"1e38", 38},
      {"1e-38", -38},
      {"0.00000000000000000000123456789012345678",
       "-20.90848502278730010745951946815347839602"},
      {"0.12345678901234567890123456789012345678",
       "-0.90848502278730010428916972176567882566"},
      {"1.12345678901234567890123456789012345678",
       "0.05055637301292183541410644614259954919"},
      {"12345678901234567890123456789012345678."
       "12345678901234567890123456789012345678",
       "37.09151497721269989571083027823432117434"},
      {kMaxBigNumericValueStr, "38.76264889431520477950341815474572182589"},
      {-1, kNonPositiveUndefinedError},
      {"-1e-38", kNonPositiveUndefinedError},
      {kMinBigNumericValueStr, kNonPositiveUndefinedError},
      {0, kNonPositiveUndefinedError},
  };

  NumericLog10Op op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Log10_PowRoundTrip_IntegerResult) {
  TestLog10PowRoundTripIntegerResult<BigNumericValue>(38, -38, &random_);
}

TEST_F(BigNumericValueTest, Log10_PowRoundTrip_RandomResult) {
  TestLog10PowRoundTripRandomResult<BigNumericValue>(&random_);
}

TEST_F(BigNumericValueTest, Log) {
  static constexpr Error kNonPositiveUndefinedError(
      "LOG is undefined for zero or negative value, or when base equals 1: ");
  static constexpr BigNumericBinaryOpTestData<> kTestData[] = {
      {9, 3, 2},
      {"0.25", 2, -2},
      {32768, 2, 15},
      {"0.01", "0.1", 2},
      {"0.001", "0.1", 3},
      {"86.49755859375", "1.5", 11},
      {"4495482048799565826089401.98041764319429608512799023715885650255",
       "1.5", 140},
      {"21916.68133907842704378473867917442516044264", "1.001", 10000},
      {2, 4, "0.5"},
      {"0.5", 4, "-0.5"},
      {8, 4, "1.5"},
      {"0.125", 4, "-1.5"},
      {32, 4, "2.5"},
      {"0.03125", 4, "-2.5"},
      {"2e19", "4e38", "0.5"},
      {"12345678912345", "152415787806720022193399025", "0.5"},
      {"1e-19", "1e38", "-0.5"},
      {"1.0001000045001200021000252002100012", "1.00001",
       "9.99999999999999999999999999999999955004"},
      {"1.00000000000000000000000000000000000001",
       "1.00000000000000000000000000000000000001", 1},
      {"0.99999999999999999999999999999999999999",
       "0.99999999999999999999999999999999999999", 1},
      {kMaxBigNumericValueStr, kMaxBigNumericValueStr, 1},
      {"1e38", "1.00000000000000000000000000000000000015", kBigNumericOverflow},
      {"1e38", "0.99999999999999999999999999999999999985", kBigNumericOverflow},
      {kMaxBigNumericValueStr, "1.00000000000000000000000000000000000001",
       kBigNumericOverflow},
      {kMaxBigNumericValueStr, "0.99999999999999999999999999999999999999",
       kBigNumericOverflow},
      {"1.00000000000000000000000000000000000001", 8,
       0 /* Actual value approx. 4.8e-39*/},
      {"1.00000000000000000000000000000000000001", 7,
       "1e-38" /* Actual value approx. 5.14e-39*/},
      {10, "-1e-38", kNonPositiveUndefinedError},
      {10, 0, kNonPositiveUndefinedError},
      {10, 1, kNonPositiveUndefinedError},
      {10, kMinBigNumericValueStr, kNonPositiveUndefinedError},
      {"-1e-38", 10, kNonPositiveUndefinedError},
      {0, 10, kNonPositiveUndefinedError},
      {kMinBigNumericValueStr, 10, kNonPositiveUndefinedError},
  };

  NumericLogOp op;
  for (const BigNumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Log_WithRandomLosslessDoubleValue) {
  TestLogWithRandomLosslessDoubleValue<BigNumericValue>(128, 1e-15, &random_);
}

TEST_F(BigNumericValueTest, LogPowRoundTrip) {
  // 1e-36
  BigNumericValue expected_relative_error_ratio =
      BigNumericValue::FromScaledValue(100);
  TestLogPowRoundTrip<BigNumericValue>(expected_relative_error_ratio, &random_);
}

TEST_F(BigNumericValueTest, Power) {
  static constexpr Error kNegativeToFractionalError(
      "Negative BIGNUMERIC value cannot be raised to a fractional power: ");
  static constexpr BigNumericBinaryOpTestData<> kTestData[] = {
      {0, 0, 1},
      {kMaxBigNumericValueStr, 0, 1},
      {kMinBigNumericValueStr, 0, 1},
      {0, 10, 0},
      {3, 1, 3},
      {-3, 1, -3},
      {3, 2, 9},
      {-3, 2, 9},
      {2, 15, 32768},
      {-2, 15, -32768},
      {kMaxBigNumericValueStr, 1, kMaxBigNumericValueStr},
      {kMinBigNumericValueStr, 1, kMinBigNumericValueStr},
      {"1e-38", 1, "1e-38"},
      {"-1e-38", 1, "-1e-38"},
      {"1e-19", 2, "1e-38"},
      {"-1e-19", 2, "1e-38"},
      {"1e-9", 4, "1e-36"},
      {"-1e-9", 4, "1e-36"},
      {"1e-7", 5, "1e-35"},
      {"-1e-7", 5, "-1e-35"},
      {"1e-5", 7, "1e-35"},
      {"-1e-5", 7, "-1e-35"},
      {"1e-4", 9, "1e-36"},
      {"-1e-4", 9, "-1e-36"},
      {"0.01", 18, "1e-36"},
      {"-0.01", 18, "1e-36"},
      {"0.01", 19, "1e-38"},
      {"-0.01", 19, "-1e-38"},
      {"0.1", 2, "0.01"},
      {"0.1", 3, "0.001"},
      {"0.1", 4, "0.0001"},
      {"-0.1", 2, "0.01"},
      {"-0.1", 3, "-0.001"},
      {"-0.1", 4, "0.0001"},
      {"-0.1", 37, "-1e-37"},
      {"-0.1", 38, "1e-38"},
      {"1.00001", 10, "1.0001000045001200021000252002100012"},
      {"-1.00001", 10, "1.0001000045001200021000252002100012"},
      {"1.5", 11, "86.49755859375"},
      {"-1.5", 11, "-86.49755859375"},
      {"1.5", 220,
       "549638305763114488057221168491896929031"
       ".59709044913294271498812453957094425637"},
      {"1.001", 10000, "21916.68133907842704378473867917442516044264"},
      {"-1.001", 10000, "21916.68133907842704378473867917442516044264"},
      {"3.2", 38,
       "15692754338466701909.58947355801916604025588861116008628224"},
      {"10.24", 19,
       "15692754338466701909.58947355801916604025588861116008628224"},
      {"1.12345678901234567890123456789012345678", 500,
       "18975206307973218234625208"
       ".2822098051751045422557479717116721704157929799382952"},

      // Negative exponent.
      {5, -1, "0.2"},
      {-5, -1, "-0.2"},
      {1, -10, 1},
      {-1, -10, 1},
      {1, -11, 1},
      {-1, -11, -1},
      {2, -22, "2.384185791015625e-7"},
      {-2, -22, "2.384185791015625e-7"},
      {"1e-38", -1, "1e38"},
      {"-1e-38", -1, "-1e38"},
      {"1e-19", -2, "1e38"},
      {"-1e-19", -2, "1e38"},
      {"1e-9", -4, "1e36"},
      {"-1e-9", -4, "1e36"},
      {"1e-7", -5, "1e35"},
      {"-1e-7", -5, "-1e35"},
      {"1e-5", -7, "1e35"},
      {"-1e-5", -7, "-1e35"},
      {"1e-4", -9, "1e36"},
      {"-1e-4", -9, "-1e36"},
      {"0.01", -18, "1e36"},
      {"-0.01", -18, "1e36"},
      {"0.01", -19, "1e38"},
      {"-0.01", -19, "-1e38"},
      {"0.1", -1, 10},
      {"-0.1", -1, -10},
      {"0.1", -10, "1e10"},
      {"-0.1", -10, "1e10"},
      {"0.1", -11, "1e11"},
      {"-0.1", -11, "-1e11"},
      {"0.1", -28, "1e28"},
      {"0.3125", -38,
       "15692754338466701909.58947355801916604025588861116008628224"},
      {"0.09765625", -19,
       "15692754338466701909.58947355801916604025588861116008628224"},

      // Fractional exponent with exact result.
      {0, "0.5", 0},
      {4, "0.5", 2},
      {"1.21", "0.5", "1.1"},
      {1, "1.1", 1},
      {4, "-0.5", "0.5"},
      {"0.25", "-0.5", 2},
      {4, "1.5", 8},
      {"1.21", "1.5", "1.331"},
      {4, "-1.5", "0.125"},
      {"0.25", "-1.5", 8},
      {4, "2.5", 32},
      {4, "-2.5", "0.03125"},
      {"4e38", "0.5", "2e19"},
      {"1524157875323883675019051998750190521", "0.5", "1234567890123456789"},
      {"1e38", "-0.5", "1e-19"},
      {"2e38", "-0.5", "7.071067811865475244e-20"},
      {"2e38", "-1.01", 0},
      {"1024", "-2.2", "0.0000002384185791015625"},
      {"550209415302576.2935239572687716", "0.5", "23456543.12345654"},
      {"550209415302576.2935239572687716", "1.5",
       "12906010876976689528504.447489595764089191786264"},
      {"21.11377674535255285545615254209921", "0.65625",
       "7.400249944258160101211"},

      // Fractional exponent with rounding.
      {"1.234567890123456789e-20", "0.123456789",
       "0.00348467568688847211894214598995113923"},
      {"1.234567890123456789e-20", "1.234567890123456789e-20",
       "0.9999999999999999994340619700617724256"},
      {"1.23456789e-30", "1.23456789e-30",
       "0.99999999999999999999999999991497922081"},
      {"123.456789", "0.987654321",
       "116.33055622387920175489315687610291463841"},
      {"1234567890123456789.12345678901234567890123456789012345678",
       "0.78901234567890123456789012345678901234",
       "188117270656095.63248203330649254223279353247222372431"},
      {"1234567890123456789.12345678901234567890123456789012345678",
       "0.987654321",
       "738181132508049073.72324736261436725672282057372762417723"},
      {"12345678901234567890123456789012345678."
       "12345678901234567890123456789012345678",
       "0.78901234567890123456789012345678901234",
       "184358530159660897631690015178.04270299911752871575012132323387923014"},
      {kMaxBigNumericValueStr, "0.5",
       "24061596916800451154.5033772477625056927114980741063148377"},
      {"123.456789", "12.3456789",
       "66247931514706151179162891"
       ".219374273323081627744740027865335822713222813928912"},

      // Underflow.
      {kMaxBigNumericValueStr, -1, 0},
      {kMinBigNumericValueStr, -1, 0},
      {kMaxBigNumericValueStr, kMinBigNumericValueStr, 0},
      {"0.1", 39, 0},
      {"-0.1", 39, 0},
      {"1e5", -8, 0},
      {"-1e5", -8, 0},
      {"1e2", -20, 0},
      {"-1e2", -20, 0},
      {"5.123", -60, 0},
      {"-5.123", -60, 0},

      // Overflow.
      {kMaxBigNumericValueStr, 2, kBigNumericOverflow},
      {kMinBigNumericValueStr, 2, kBigNumericOverflow},
      {"5.123", 60, kBigNumericOverflow},
      {"-5.123", 60, kBigNumericOverflow},
      {"0.09436702927996520884335041046142578125",
       "-48.78799563072971068322658538818359375", kBigNumericOverflow},
      {"5.1", "54.782762478883762494688174407440714184", kBigNumericOverflow
       /* Actual value is approximately equal kMaxBigNumericValueStr+0.08*/},
      // TODO: improve pow precision to pass the following test.
      // {"1.0000000000000000000000000000000000006",
      //  "148757162515020529847850859489715308328"
      //  ".6449449111256868068668281536606165383",
      //  kBigNumericOverflow
      /* Actual value is approximately equal kMaxBigNumericValueStr+9.8e-37*/
      // },
      {"-5.06728621", "55", kBigNumericOverflow
       /* Actual value is approximately equal kMinBigNumericValueStr-2.37e31*/},
      {"0.567", "-157.30512975351341786893836610256977551362",
       kBigNumericOverflow
       /* Actual value is approximately equal kMaxBigNumericValueStr+2.59*/},
      // Overflow after multiplication with fractional power.
      {"3.4e19", "2.9", kBigNumericOverflow},
      {"1.0000000000000000000000000000000000002", kMaxBigNumericValueStr,
       kBigNumericOverflow},

      // DivisionByZero
      {0, -1, kDivisionByZero},
      {0, "-1.5", kDivisionByZero},
      {0, -2, kDivisionByZero},
      {0, kMinBigNumericValueStr, kDivisionByZero},

      // NegativeToFractional
      {"-0.000000001", "0.000000001", kNegativeToFractionalError},
      {"-0.000000001", "-0.000000001", kNegativeToFractionalError},
      {-123, "2.1", kNegativeToFractionalError},
      {-123, "-2.1", kNegativeToFractionalError},
      {kMinBigNumericValueStr, kMaxBigNumericValueStr,
       kNegativeToFractionalError},
      {kMinBigNumericValueStr, kMinBigNumericValueStr,
       kNegativeToFractionalError},
  };

  NumericPowerOp op;
  for (const BigNumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Pow_WithRandomLosslessDoubleValue) {
  TestPowWithRandomLosslessDoubleValue<BigNumericValue>(128, &random_);
}

TEST_F(BigNumericValueTest, Sqrt) {
  static constexpr Error kNegativeUndefinedError(
      "SQRT is undefined for negative value: ");
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {1, 1},
      {4, 2},
      {"0.04", "0.2"},
      {"1e-38", "1e-19"},
      {"0.00000000000000000000123456789012345678",
       "0.00000000003513641828820144240286429591"},
      {"0.12345678901234567890123456789012345678",
       "0.3513641828820144253111222381699882939"},
      {"1.12345678901234567890123456789012345678",
       "1.05993244549468608051549938620585216415"},
      {"12345678901234567890123456789012345678."
       "12345678901234567890123456789012345678",
       "3513641828820144253.11122238169988293906416115847891558041"},
      {"4e38", "2e19"},
      {"5.76e38", "2.4e19"},
      {kMaxBigNumericValueStr,
       "24061596916800451154.5033772477625056927114980741063148377"},
      {"-1e-38", kNegativeUndefinedError},
      {-1, kNegativeUndefinedError},
      {kMinBigNumericValueStr, kNegativeUndefinedError},
  };

  NumericSqrtOp op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Sqrt_WithRandomIntegerValue) {
  TestSqrtWithRandomIntegerValue<BigNumericValue>(18446744073709551615ULL,
                                               &random_);
}

TEST_F(BigNumericValueTest, Sqrt_WithRandomLosslessDoubleValue) {
  TestSqrtWithRandomLosslessDoubleValue<BigNumericValue>(128, &random_);
}

TEST_F(BigNumericValueTest, SqrtPowRoundTrip) {
  TestSqrtPowRoundTrip<BigNumericValue>(&random_);
}

TEST_F(BigNumericValueTest, Cbrt) {
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {1, 1},
      {8, 2},
      {"0.008", "0.2"},
      {"1e-36", "1e-12"},
      {"0.00000000000000000000123456789012345678",
       "0.00000010727659796768462141273718580017"},
      {"0.12345678901234567890123456789012345678",
       "0.49793385923477226971099150388336845385"},
      {"1.12345678901234567890123456789012345678",
       "1.03956613714085164031820407189785652284"},
      {"12345678901234567890123456789012345678."
       "12345678901234567890123456789012345678",
       "2311204240901.83625114396092108297847144293179424758"},
      {"8e36", "2e12"},
      {"1.25e38", "5e12"},
      {kMaxBigNumericValueStr,
       "8334565515049.55065578647965760880872812752814461188"},
  };

  NumericCbrtOp op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
  TestUnaryOp(op, BigNumericValueWrapper(kMinBigNumericValueStr),
              BigNumericValueWrapper(
                  "-8334565515049.55065578647965760880872812752814461188"));
}

TEST_F(BigNumericValueTest, ApproximateCbrt) {
  TestBigNumericApproximateCbrt(&random_);
}

TEST_F(BigNumericValueTest, Cbrt_WithRandomIntegerValue) {
  TestCbrtWithRandomIntegerValue<BigNumericValue>(8334565515049ULL, &random_);
}

TEST_F(BigNumericValueTest, Cbrt_WithRandomLosslessDoubleValue) {
  TestCbrtWithRandomLosslessDoubleValue<BigNumericValue>(128, &random_);
}

TEST_F(BigNumericValueTest, CbrtPowRoundTrip) {
  TestCbrtPowRoundTrip<BigNumericValue>(&random_);
}

TEST_F(BigNumericValueTest, MultiplicationDivisionRoundTrip) {
  TestMultiplicationDivisionRoundTrip<BigNumericValue, int64_t, __int128,
                                      FixedInt<64, 4>>(&random_);
}

TEST_F(BigNumericValueTest, DivideToIntegralValue) {
  static constexpr BigNumericBinaryOpTestData<> kTestData[] = {
      {6, 2, 3},
      {1, 3, 0},
      {"5.78960446186580977117854925043439539266", "1e-38",
       "578960446186580977117854925043439539266"},
      {"5.78960446186580977117854925043439539266", "1", "5"},
      {kMaxBigNumericValueStr, 3, "192986815395526992372618308347813179755"},
      {kMaxBigNumericValueStr, 2, "289480223093290488558927462521719769633"},
      {kMaxBigNumericValueStr, 1, "578960446186580977117854925043439539266"},
      {kMaxBigNumericValueStr, kMaxBigNumericValueStr, 1},
      {5, "2.3", 2},
      {"5.2", 2, 2},
      {kMaxBigNumericValueStr, "0.99999999999999999999999999999999999999",
       kBigNumericOverflow},
      {"1e20", "1e-19", kBigNumericOverflow},
      {"57896044618658097711785492504343953926.7", "0.1", kBigNumericOverflow},
      {"5.78960446186580977117854925043439539267", "1e-38",
       kBigNumericOverflow},
      {"57896044618658097711785492504343953926."
       "69999999999999999999999999999999999999",
       "0.1", "578960446186580977117854925043439539266"},
      {0, 0, kDivisionByZero},
      {"0.1", 0, kDivisionByZero},
      {1, 0, kDivisionByZero},
      {kMaxBigNumericValueStr, 0, kDivisionByZero},
  };

  static constexpr BigNumericBinaryOpTestData<> kSpecialTestData[] = {
      {kMinBigNumericValueStr, 1, "-578960446186580977117854925043439539266"},
      {kMinBigNumericValueStr, -1, "578960446186580977117854925043439539266"},
      {kMinBigNumericValueStr, kMaxBigNumericValueStr, -1},
      {kMaxBigNumericValueStr, kMinBigNumericValueStr, 0},
      {1, kMinBigNumericValueStr, 0},
      {"1e-38", kMinBigNumericValueStr, 0},
      {0, kMinBigNumericValueStr, 0},
      {kMinBigNumericValueStr, kMinBigNumericValueStr, 1},
      {kMinBigNumericValueStr, "0.99999999999999999999999999999999999999",
       kBigNumericOverflow},
      {kMinBigNumericValueStr, 0, kDivisionByZero},
  };

  NumericDivideToIntegralValueOp op;
  for (const BigNumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Mod) {
  static constexpr BigNumericBinaryOpTestData<> kTestData[] = {
      {5, 2, 1},
      {5, "0.001", 0},
      {"5.78960446186580977117854925043439539266", "1e-38", 0},
      {"5.78960446186580977117854925043439539266", "1",
       "0.78960446186580977117854925043439539266"},
      {kMaxBigNumericValueStr, 3, "1.34992332820282019728792003956564819967"},
      {kMaxBigNumericValueStr, 2, "0.34992332820282019728792003956564819967"},
      {kMaxBigNumericValueStr, 1, "0.34992332820282019728792003956564819967"},
      {kMaxBigNumericValueStr, kMaxBigNumericValueStr, 0},
      {"1e-38", "1e-38", 0},
      {5, "2.3", "0.4"},
      {5, "0.3", "0.2"},
      {"5.2", 2, "1.2"},
      {1, 0, kDivisionByZero},
      {1, kMaxBigNumericValueStr, 1},
      {"1e-38", kMaxBigNumericValueStr, "1e-38"},
      {0, kMaxBigNumericValueStr, 0},
      {1, 0, kDivisionByZero},
      {"1e-38", 0, kDivisionByZero},
  };

  static constexpr BigNumericBinaryOpTestData<> kSpecialTestData[] = {
      {kMinBigNumericValueStr, -1, "-0.34992332820282019728792003956564819968"},
      {kMinBigNumericValueStr, 1, "-0.34992332820282019728792003956564819968"},
      {kMinBigNumericValueStr, "1e-38", 0},
      {kMinBigNumericValueStr, kMinBigNumericValueStr, 0},
      {kMinBigNumericValueStr, kMaxBigNumericValueStr, "-1e-38"},
      {kMinBigNumericValueStr, 0, kDivisionByZero},
      {kMaxBigNumericValueStr, kMinBigNumericValueStr, kMaxBigNumericValueStr},
      {1, kMinBigNumericValueStr, 1},
      {"1e-38", kMinBigNumericValueStr, "1e-38"},
      {0, kMinBigNumericValueStr, 0},
  };

  NumericModOp op;
  for (const BigNumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, HasFractionalPart) {
  static constexpr NumericUnaryOpTestData<BigNumericValueWrapper, bool>
      kTestData[] = {
          {"0.5", true},
          {"0.3", true},
          {"0.2", true},
          {"0.1", true},
          {"0.01", true},
          {"0.001", true},
          {"0.000000001", true},
          {"1e-38", true},
          {"1.00000000000000000000000000000000000001", true},
          {"10000000000000000000000000000000000000."
           "00000000000000000000000000000000000001",
           true},
          {"0.987654321", true},
          {"0.999999999", true},
          {"1.000000001", true},
          {1, false},
          {2, false},
          {3, false},
          {5, false},
          {10, false},
          {9999, false},
          {"99999999999999999999999999999", false},
          {"1e38", false},
          {"99999999999999999999999999999.000000001", true},
          {"10000000000000000000000000000000000000.1", true},
          {kMaxNumericValueStr, true},
          {kMaxBigNumericValueStr, true},
      };

  for (const auto& data : kTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue value, GetValue(data.input));
    EXPECT_EQ(data.expected_output, value.HasFractionalPart());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue negated, value.Negate());
    EXPECT_EQ(data.expected_output, negated.HasFractionalPart());
  }
  EXPECT_TRUE(BigNumericValue::MinValue().HasFractionalPart());
}

TEST_F(BigNumericValueTest, Floor) {
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {999, 999},
      {"999.000000001", 999},
      {"999.00000000000000000000000000000000000001", 999},
      {"999.999999999", 999},
      {"999.99999999999999999999999999999999999999", 999},
      {-999, -999},
      {"-999.000000001", -1000},
      {"-999.00000000000000000000000000000000000001", -1000},
      {"-999.999999999", -1000},
      {"-999.99999999999999999999999999999999999999", -1000},
      {"1e-38", 0},
      {"0.99999999999999999999999999999999999999", 0},
      {"-0.99999999999999999999999999999999999999", -1},
      {"-1e-38", -1},
      {kMaxNumericValueStr, "99999999999999999999999999999"},
      {kMinNumericValueStr, "-100000000000000000000000000000"},
      {"-99999999999999999999999999999.000000001",
       "-100000000000000000000000000000"},
      {kMaxBigNumericValueStr, "578960446186580977117854925043439539266"},
      {kMinBigNumericValueStr, kBigNumericOverflow},
      {"-578960446186580977117854925043439539266.0",
       "-578960446186580977117854925043439539266"},
      {"-578960446186580977117854925043439539266.1", kBigNumericOverflow},
      {"-578960446186580977117854925043439539266."
       "00000000000000000000000000000000000001",
       kBigNumericOverflow},
  };

  NumericFloorOp op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Trunc) {
  constexpr absl::string_view kMaxDigitStr =
      "123456789012345678901234567890123456789."
      "12345678901234567890123456789012345678";
  static constexpr BigNumericBinaryOpTestData<int64_t> kTestData[] = {
      {0, kint64min, 0},
      {0, -40, 0},
      {0, -39, 0},
      {0, -38, 0},
      {0, -32, 0},
      {0, -16, 0},
      {0, -8, 0},
      {0, -5, 0},
      {0, -4, 0},
      {0, -3, 0},
      {0, -1, 0},
      {0, 0, 0},
      {0, 1, 0},
      {0, 3, 0},
      {0, 4, 0},
      {0, 5, 0},
      {0, 8, 0},
      {0, 16, 0},
      {0, 32, 0},
      {0, 36, 0},
      {0, 37, 0},
      {0, 38, 0},
      {0, 39, 0},
      {0, kint64max, 0},

      {kMaxDigitStr, kint64min, 0},
      {kMaxDigitStr, -39, 0},
      {kMaxDigitStr, -38, "100000000000000000000000000000000000000"},
      {kMaxDigitStr, -37, "120000000000000000000000000000000000000"},
      {kMaxDigitStr, -36, "123000000000000000000000000000000000000"},
      {kMaxDigitStr, -32, "123456700000000000000000000000000000000"},
      {kMaxDigitStr, -16, "123456789012345678901230000000000000000"},
      {kMaxDigitStr, -8, "123456789012345678901234567890100000000"},
      {kMaxDigitStr, -5, "123456789012345678901234567890123400000"},
      {kMaxDigitStr, -4, "123456789012345678901234567890123450000"},
      {kMaxDigitStr, -3, "123456789012345678901234567890123456000"},
      {kMaxDigitStr, -1, "123456789012345678901234567890123456780"},
      {kMaxDigitStr, 0, "123456789012345678901234567890123456789"},
      {kMaxDigitStr, 1, "123456789012345678901234567890123456789.1"},
      {kMaxDigitStr, 3, "123456789012345678901234567890123456789.123"},
      {kMaxDigitStr, 4, "123456789012345678901234567890123456789.1234"},
      {kMaxDigitStr, 5, "123456789012345678901234567890123456789.12345"},
      {kMaxDigitStr, 8, "123456789012345678901234567890123456789.12345678"},
      {kMaxDigitStr, 16,
       "123456789012345678901234567890123456789.1234567890123456"},
      {kMaxDigitStr, 32,
       "123456789012345678901234567890123456789."
       "12345678901234567890123456789012"},
      {kMaxDigitStr, 36,
       "123456789012345678901234567890123456789."
       "123456789012345678901234567890123456"},
      {kMaxDigitStr, 37,
       "123456789012345678901234567890123456789."
       "1234567890123456789012345678901234567"},
      {kMaxDigitStr, 38, kMaxDigitStr},
      {kMaxDigitStr, 39, kMaxDigitStr},
      {kMaxDigitStr, kint64max, kMaxDigitStr},

      {kMaxBigNumericValueStr, kint64min, 0},
      {kMaxBigNumericValueStr, -39, 0},
      {kMaxBigNumericValueStr, -38, "500000000000000000000000000000000000000"},
      {kMaxBigNumericValueStr, -37, "570000000000000000000000000000000000000"},
      {kMaxBigNumericValueStr, -36, "578000000000000000000000000000000000000"},
      {kMaxBigNumericValueStr, -32, "578960400000000000000000000000000000000"},
      {kMaxBigNumericValueStr, -16, "578960446186580977117850000000000000000"},
      {kMaxBigNumericValueStr, -8, "578960446186580977117854925043400000000"},
      {kMaxBigNumericValueStr, -5, "578960446186580977117854925043439500000"},
      {kMaxBigNumericValueStr, -4, "578960446186580977117854925043439530000"},
      {kMaxBigNumericValueStr, -3, "578960446186580977117854925043439539000"},
      {kMaxBigNumericValueStr, -1, "578960446186580977117854925043439539260"},
      {kMaxBigNumericValueStr, 0, "578960446186580977117854925043439539266"},
      {kMaxBigNumericValueStr, 1, "578960446186580977117854925043439539266.3"},
      {kMaxBigNumericValueStr, 3,
       "578960446186580977117854925043439539266.349"},
      {kMaxBigNumericValueStr, 4,
       "578960446186580977117854925043439539266.3499"},
      {kMaxBigNumericValueStr, 5,
       "578960446186580977117854925043439539266.34992"},
      {kMaxBigNumericValueStr, 8,
       "578960446186580977117854925043439539266.34992332"},
      {kMaxBigNumericValueStr, 16,
       "578960446186580977117854925043439539266.3499233282028201"},
      {kMaxBigNumericValueStr, 32,
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564"},
      {kMaxBigNumericValueStr, 36,
       "578960446186580977117854925043439539266."
       "349923328202820197287920039565648199"},
      {kMaxBigNumericValueStr, 37,
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481996"},
      {kMaxBigNumericValueStr, 38, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr, 39, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr, kint64max, kMaxBigNumericValueStr},
  };

  static constexpr BigNumericBinaryOpTestData<int64_t> kSpecialTestData[] = {
      {kMinBigNumericValueStr, kint64min, 0},
      {kMinBigNumericValueStr, -39, 0},
      {kMinBigNumericValueStr, -38, "-500000000000000000000000000000000000000"},
      {kMinBigNumericValueStr, -37, "-570000000000000000000000000000000000000"},
      {kMinBigNumericValueStr, -30, "-578960446000000000000000000000000000000"},
      {kMinBigNumericValueStr, -29, "-578960446100000000000000000000000000000"},
      {kMinBigNumericValueStr, -28, "-578960446180000000000000000000000000000"},
      {kMinBigNumericValueStr, -27, "-578960446186000000000000000000000000000"},
      {kMinBigNumericValueStr, -1, "-578960446186580977117854925043439539260"},
      {kMinBigNumericValueStr, 0, kMinBigNumericValueStr.substr(0, 41 + 0)},
      {kMinBigNumericValueStr, 1, kMinBigNumericValueStr.substr(0, 41 + 1)},
      {kMinBigNumericValueStr, 8, kMinBigNumericValueStr.substr(0, 41 + 8)},
      {kMinBigNumericValueStr, 9, kMinBigNumericValueStr.substr(0, 41 + 9)},
      {kMinBigNumericValueStr, 10, kMinBigNumericValueStr.substr(0, 41 + 10)},
      {kMinBigNumericValueStr, 27, kMinBigNumericValueStr.substr(0, 41 + 27)},
      {kMinBigNumericValueStr, 28, kMinBigNumericValueStr.substr(0, 41 + 28)},
      {kMinBigNumericValueStr, 29, kMinBigNumericValueStr.substr(0, 41 + 29)},
      {kMinBigNumericValueStr, 30, kMinBigNumericValueStr.substr(0, 41 + 30)},
      {kMinBigNumericValueStr, 37, kMinBigNumericValueStr.substr(0, 41 + 37)},
      {kMinBigNumericValueStr, 38, kMinBigNumericValueStr},
      {kMinBigNumericValueStr, kint64max, kMinBigNumericValueStr},
  };

  NumericTruncOp op;

  for (const BigNumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }

  for (const BigNumericBinaryOpTestData<int64_t>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Round) {
  constexpr absl::string_view kMaxDigitStr =
      "123456789012345678901234567890123456789."
      "12345678901234567890123456789012345678";

  static constexpr BigNumericBinaryOpTestData<int64_t> kCommonTestData[] = {
      {0, kint64min, 0},
      {0, -40, 0},
      {0, -39, 0},
      {0, -38, 0},
      {0, -32, 0},
      {0, -16, 0},
      {0, -8, 0},
      {0, -5, 0},
      {0, -4, 0},
      {0, -3, 0},
      {0, -1, 0},
      {0, 0, 0},
      {0, 1, 0},
      {0, 3, 0},
      {0, 4, 0},
      {0, 5, 0},
      {0, 8, 0},
      {0, 16, 0},
      {0, 32, 0},
      {0, 36, 0},
      {0, 37, 0},
      {0, 38, 0},
      {0, 39, 0},
      {0, kint64max, 0},

      {kMaxDigitStr, kint64min, 0},
      {kMaxDigitStr, -39, 0},
      {kMaxDigitStr, -38, "100000000000000000000000000000000000000"},
      {kMaxDigitStr, -37, "120000000000000000000000000000000000000"},
      {kMaxDigitStr, -36, "123000000000000000000000000000000000000"},
      {kMaxDigitStr, -32, "123456800000000000000000000000000000000"},
      {kMaxDigitStr, -16, "123456789012345678901230000000000000000"},
      {kMaxDigitStr, -8, "123456789012345678901234567890100000000"},
      {kMaxDigitStr, -5, "123456789012345678901234567890123500000"},
      {kMaxDigitStr, -4, "123456789012345678901234567890123460000"},
      {kMaxDigitStr, -3, "123456789012345678901234567890123457000"},
      {kMaxDigitStr, -1, "123456789012345678901234567890123456790"},
      {kMaxDigitStr, 0, "123456789012345678901234567890123456789"},
      {kMaxDigitStr, 1, "123456789012345678901234567890123456789.1"},
      {kMaxDigitStr, 3, "123456789012345678901234567890123456789.123"},
      {kMaxDigitStr, 4, "123456789012345678901234567890123456789.1235"},
      {kMaxDigitStr, 5, "123456789012345678901234567890123456789.12346"},
      {kMaxDigitStr, 8, "123456789012345678901234567890123456789.12345679"},
      {kMaxDigitStr, 16,
       "123456789012345678901234567890123456789.1234567890123457"},
      {kMaxDigitStr, 32,
       "123456789012345678901234567890123456789."
       "12345678901234567890123456789012"},
      {kMaxDigitStr, 36,
       "123456789012345678901234567890123456789."
       "123456789012345678901234567890123457"},
      {kMaxDigitStr, 37,
       "123456789012345678901234567890123456789."
       "1234567890123456789012345678901234568"},
      {kMaxDigitStr, 38, kMaxDigitStr},
      {kMaxDigitStr, 39, kMaxDigitStr},
      {kMaxDigitStr, kint64max, kMaxDigitStr},

      {kMaxBigNumericValueStr, kint64min, 0},
      {kMaxBigNumericValueStr, -40, 0},
      {kMaxBigNumericValueStr, -39, kBigNumericOverflow},
      {kMaxBigNumericValueStr, -38, kBigNumericOverflow},
      {kMaxBigNumericValueStr, -37, kBigNumericOverflow},
      {kMaxBigNumericValueStr, -36, kBigNumericOverflow},
      {kMaxBigNumericValueStr, -32, "578960400000000000000000000000000000000"},
      {kMaxBigNumericValueStr, -16, "578960446186580977117850000000000000000"},
      {kMaxBigNumericValueStr, -8, "578960446186580977117854925043400000000"},
      {kMaxBigNumericValueStr, -5, "578960446186580977117854925043439500000"},
      {kMaxBigNumericValueStr, -4, kBigNumericOverflow},
      {kMaxBigNumericValueStr, -3, "578960446186580977117854925043439539000"},
      {kMaxBigNumericValueStr, -1, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 0, "578960446186580977117854925043439539266"},
      {kMaxBigNumericValueStr, 1, "578960446186580977117854925043439539266.3"},
      {kMaxBigNumericValueStr, 3, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 4,
       "578960446186580977117854925043439539266.3499"},
      {kMaxBigNumericValueStr, 5,
       "578960446186580977117854925043439539266.34992"},
      {kMaxBigNumericValueStr, 8, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 16, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 32, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 36, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 37, kBigNumericOverflow},
      {kMaxBigNumericValueStr, 38, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr, 39, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr, kint64max, kMaxBigNumericValueStr},

      {"578960446186580977117854925043439539266."
       "34949999999999999999999999999999999999",
       3, "578960446186580977117854925043439539266.349"},
      {"578960446186580977117854925043439539266.3495", 3, kBigNumericOverflow},
      {"-578960446186580977117854925043439539266."
       "34949999999999999999999999999999999999",
       3, "-578960446186580977117854925043439539266.349"},
      {"-578960446186580977117854925043439539266.3495", 3, kBigNumericOverflow},

      {"0.49999999999999999999999999999999999999", 0, 0},
      {"0.49999999999999999999999999999999999999", 1, "0.5"},
      {"0.49999999999999999999999999999999999999", 2, "0.5"},
      {"0.49999999999999999999999999999999999999", 10, "0.5"},
      {"0.49999999999999999999999999999999999999", 20, "0.5"},
      {"0.49999999999999999999999999999999999999", 37, "0.5"},
      {"0.49999999999999999999999999999999999999", 38,
       "0.49999999999999999999999999999999999999"},
      {"0.49999999999999999999999999999999999999", 39,
       "0.49999999999999999999999999999999999999"},
  };

  static constexpr BigNumericBinaryOpTestData<int64_t>
      kRoundAwayFromZeroTestData[] = {
          {"0.45000000000000000000000000000000000000", 1, "0.5"},
          {"0.45500000000000000000000000000000000000", 2, "0.46"},
          {"0.46500000000000000000000000000000000000", 2, "0.47"},
      };

  static constexpr BigNumericBinaryOpTestData<int64_t> kRoundToEvenTestData[] =
      {
          {"56785678567856785675000000000", -10,
           "56785678567856785680000000000"},
          {"56785678567856785678500000000", -9,
           "56785678567856785678000000000"},
          {"56785678567856785678550000000", -8,
           "56785678567856785678600000000"},
          {"56785678567856785678565000000", -7,
           "56785678567856785678560000000"},
          {"56785678567856785678567500000", -6,
           "56785678567856785678568000000"},
          {"56785678567856785678567850000", -5,
           "56785678567856785678567800000"},
          {"56785678567856785678567850001", -5,
           "56785678567856785678567900000"},
          {"56785678567856785678567855000", -4,
           "56785678567856785678567860000"},
          {"56785678567856785678567856500", -3,
           "56785678567856785678567856000"},
          {"56785678567856785678567856750", -2,
           "56785678567856785678567856800"},
          {"56785678567856785678567856785", -1,
           "56785678567856785678567856780"},
          {"56785678567856785678567856785.5", 0,
           "56785678567856785678567856786"},
          {"56785678567856785678567856785.65", 1,
           "56785678567856785678567856785.6"},
          {"56785678567856785678567856785.565", 2,
           "56785678567856785678567856785.56"},
          {"56785678567856785678567856785.5675", 3,
           "56785678567856785678567856785.568"},
          {"56785678567856785678567856785.56785", 4,
           "56785678567856785678567856785.5678"},
          {"56785678567856785678567856785.567855", 5,
           "56785678567856785678567856785.56786"},
          {"56785678567856785678567856785.56786501", 5,
           "56785678567856785678567856785.56787"},
          {"56785678567856785678567856785.5678565", 6,
           "56785678567856785678567856785.567856"},
          {"56785678567856785678567856785.56785685", 7,
           "56785678567856785678567856785.5678568"},
          {"56785678567856785678567856785.567856785", 8,
           "56785678567856785678567856785.56785678"},
          {"56785678567856785678567856785.5678567885", 9,
           "56785678567856785678567856785.567856788"},
          {"56785678567856785678567856785.56785678885", 10,
           "56785678567856785678567856785.5678567888"},
          {"56785678567856785678567856785.567856788865", 11,
           "56785678567856785678567856785.56785678886"},
          {"0.49999999999999999999999999999999999450", 36,
           "0.499999999999999999999999999999999994"},
          {"0.49999999999999999999999999999999995490", 35,
           "0.49999999999999999999999999999999995"},
          {"0.49999999999999999999999999999999995499", 35,
           "0.49999999999999999999999999999999995"},
          {"0.49999999999999999999999999999999995510", 35,
           "0.49999999999999999999999999999999996"},
          {"0.49999999999999999999999999999999996501", 35,
           "0.49999999999999999999999999999999997"},
          {"0.49999999999999999999999999999999996500", 35,
           "0.49999999999999999999999999999999996"},
          {"0.49999999999999999999999999999999994500", 35,
           "0.49999999999999999999999999999999994"},
          {"0.49999999999999999999999999999999945000", 34,
           "0.4999999999999999999999999999999994"},
          {"0.49999999999999999999999999999999975000", 34,
           "0.4999999999999999999999999999999998"},
          {"0.49999999999999999999999999999999994490", 35,
           "0.49999999999999999999999999999999994"},
          {"56785678567856750000000000", -11, "56785678567856800000000000"},
          {"56785678567856850000000000", -11, "56785678567856800000000000"},
          {"56785678500000000000000000", -18, "56785678000000000000000000"},
          {"56785678567856850000000001", -11, "56785678567856900000000000"},
          {"56785678500000000000000001", -18, "56785679000000000000000000"},
          {"0.445", 2, "0.44"},
          {"0.005", 2, "0"},
          {"0.49999999999999999999999999999999999999", 37, "0.5"},
          {"0.49999999999999999999999999999999999999", 38,
           "0.49999999999999999999999999999999999999"},
          {"0.49999999999999999999999999999999999999", 39,
           "0.49999999999999999999999999999999999999"},
          {"0.45000000000000000000000000000000000000", 1, "0.4"},
          {"0.45500000000000000000000000000000000000", 2, "0.46"},
          {"0.46500000000000000000000000000000000000", 2, "0.46"},
      };

  static constexpr BigNumericBinaryOpTestData<int64_t> kSpecialTestData[] = {
      {kMinBigNumericValueStr, kint64min, 0},
      {kMinBigNumericValueStr, -40, 0},
      {kMinBigNumericValueStr, -39, kBigNumericOverflow},
      {kMinBigNumericValueStr, -38, kBigNumericOverflow},
      {kMinBigNumericValueStr, -37, kBigNumericOverflow},
      {kMinBigNumericValueStr, -30, "-578960446000000000000000000000000000000"},
      {kMinBigNumericValueStr, -29, kBigNumericOverflow},
      {kMinBigNumericValueStr, -28, kBigNumericOverflow},
      {kMinBigNumericValueStr, -27, kBigNumericOverflow},
      {kMinBigNumericValueStr, -1, kBigNumericOverflow},
      {kMinBigNumericValueStr, 0, kMinBigNumericValueStr.substr(0, 41 + 0)},
      {kMinBigNumericValueStr, 1, kMinBigNumericValueStr.substr(0, 41 + 1)},
      {kMinBigNumericValueStr, 8, kBigNumericOverflow},
      {kMinBigNumericValueStr, 9, kMinBigNumericValueStr.substr(0, 41 + 9)},
      {kMinBigNumericValueStr, 10, kMinBigNumericValueStr.substr(0, 41 + 10)},
      {kMinBigNumericValueStr, 27, kBigNumericOverflow},
      {kMinBigNumericValueStr, 28, kBigNumericOverflow},
      {kMinBigNumericValueStr, 29, kBigNumericOverflow},
      {kMinBigNumericValueStr, 30, kBigNumericOverflow},
      {kMinBigNumericValueStr, 37, kBigNumericOverflow},
      {kMinBigNumericValueStr, 38, kMinBigNumericValueStr},
      {kMinBigNumericValueStr, kint64max, kMinBigNumericValueStr},
  };

  NumericRoundHalfAwayFromZeroOp away_from_zero_op;
  NumericRoundHalfEvenOp half_even_op;
  for (const BigNumericBinaryOpTestData<int64_t>& data :
       kRoundAwayFromZeroTestData) {
    TestBinaryOp(away_from_zero_op, data.input1, data.input2,
                 data.expected_output);
    TestBinaryOp(away_from_zero_op, -data.input1, data.input2,
                 -data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<int64_t>& data : kRoundToEvenTestData) {
    TestBinaryOp(half_even_op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(half_even_op, -data.input1, data.input2,
                 -data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<int64_t>& data : kSpecialTestData) {
    TestBinaryOp(away_from_zero_op, data.input1, data.input2,
                 data.expected_output);
    TestBinaryOp(half_even_op, data.input1, data.input2, data.expected_output);
  }
  for (const BigNumericBinaryOpTestData<int64_t>& data : kCommonTestData) {
    TestBinaryOp(away_from_zero_op, data.input1, data.input2,
                 data.expected_output);
    TestBinaryOp(away_from_zero_op, -data.input1, data.input2,
                 -data.expected_output);
    TestBinaryOp(half_even_op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(half_even_op, -data.input1, data.input2,
                 -data.expected_output);
  }
}

TEST_F(BigNumericValueTest, FromStringWithRounding) {
  constexpr absl::string_view kMaxDigitStr =
      "123456789012345678901234567890123456789."
      "12345678901234567890123456789012345678";

  static constexpr BigNumericFromStringWithRoundingOpTestData
      kCommonTestData[] = {
          {"0", kint64min, 0},
          {"0", -40, 0},
          {"0", -39, 0},
          {"0", -38, 0},
          {"0", -32, 0},
          {"0", -16, 0},
          {"0", -8, 0},
          {"0", -5, 0},
          {"0", -4, 0},
          {"0", -3, 0},
          {"0", -1, 0},
          {"0", 0, 0},
          {"0", 1, 0},
          {"0", 3, 0},
          {"0", 4, 0},
          {"0", 5, 0},
          {"0", 8, 0},
          {"0", 16, 0},
          {"0", 32, 0},
          {"0", 36, 0},
          {"0", 37, 0},
          {"0", 38, 0},
          {"0", 39, 0},
          {"0", kint64max, 0},

          {kMaxDigitStr, kint64min, 0},
          {kMaxDigitStr, -39, 0},
          {kMaxDigitStr, -38, "100000000000000000000000000000000000000"},
          {kMaxDigitStr, -37, "120000000000000000000000000000000000000"},
          {kMaxDigitStr, -36, "123000000000000000000000000000000000000"},
          {kMaxDigitStr, -32, "123456800000000000000000000000000000000"},
          {kMaxDigitStr, -16, "123456789012345678901230000000000000000"},
          {kMaxDigitStr, -8, "123456789012345678901234567890100000000"},
          {kMaxDigitStr, -5, "123456789012345678901234567890123500000"},
          {kMaxDigitStr, -4, "123456789012345678901234567890123460000"},
          {kMaxDigitStr, -3, "123456789012345678901234567890123457000"},
          {kMaxDigitStr, -1, "123456789012345678901234567890123456790"},
          {kMaxDigitStr, 0, "123456789012345678901234567890123456789"},
          {kMaxDigitStr, 1, "123456789012345678901234567890123456789.1"},
          {kMaxDigitStr, 3, "123456789012345678901234567890123456789.123"},
          {kMaxDigitStr, 4, "123456789012345678901234567890123456789.1235"},
          {kMaxDigitStr, 5, "123456789012345678901234567890123456789.12346"},
          {kMaxDigitStr, 8, "123456789012345678901234567890123456789.12345679"},
          {kMaxDigitStr, 16,
           "123456789012345678901234567890123456789.1234567890123457"},
          {kMaxDigitStr, 32,
           "123456789012345678901234567890123456789."
           "12345678901234567890123456789012"},
          {kMaxDigitStr, 36,
           "123456789012345678901234567890123456789."
           "123456789012345678901234567890123457"},
          {kMaxDigitStr, 37,
           "123456789012345678901234567890123456789."
           "1234567890123456789012345678901234568"},
          {kMaxDigitStr, 38, kMaxDigitStr},
          {kMaxDigitStr, 39, kMaxDigitStr},
          {kMaxDigitStr, kint64max, kMaxDigitStr},

          {kMaxBigNumericValueStr, kint64min, 0},
          {kMaxBigNumericValueStr, -40, 0},
          {kMaxBigNumericValueStr, -39, kBigNumericInvalid},
          {kMaxBigNumericValueStr, -38, kBigNumericInvalid},
          {kMaxBigNumericValueStr, -37, kBigNumericInvalid},
          {kMaxBigNumericValueStr, -36, kBigNumericInvalid},
          {kMaxBigNumericValueStr, -32,
           "578960400000000000000000000000000000000"},
          {kMaxBigNumericValueStr, -16,
           "578960446186580977117850000000000000000"},
          {kMaxBigNumericValueStr, -8,
           "578960446186580977117854925043400000000"},
          {kMaxBigNumericValueStr, -5,
           "578960446186580977117854925043439500000"},
          {kMaxBigNumericValueStr, -4, kBigNumericInvalid},
          {kMaxBigNumericValueStr, -3,
           "578960446186580977117854925043439539000"},
          {kMaxBigNumericValueStr, -1, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 0,
           "578960446186580977117854925043439539266"},
          {kMaxBigNumericValueStr, 1,
           "578960446186580977117854925043439539266.3"},
          {kMaxBigNumericValueStr, 3, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 4,
           "578960446186580977117854925043439539266.3499"},
          {kMaxBigNumericValueStr, 5,
           "578960446186580977117854925043439539266.34992"},
          {kMaxBigNumericValueStr, 8, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 16, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 32, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 36, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 37, kBigNumericInvalid},
          {kMaxBigNumericValueStr, 38, kMaxBigNumericValueStr},
          {kMaxBigNumericValueStr, 39, kMaxBigNumericValueStr},
          {kMaxBigNumericValueStr, kint64max, kMaxBigNumericValueStr},

          {"578960446186580977117854925043439539266."
           "34949999999999999999999999999999999999",
           3, "578960446186580977117854925043439539266.349"},
          {"578960446186580977117854925043439539266.3495", 3,
           kBigNumericInvalid},
          {"-578960446186580977117854925043439539266."
           "34949999999999999999999999999999999999",
           3, "-578960446186580977117854925043439539266.349"},
          {"-578960446186580977117854925043439539266.3495", 3,
           kBigNumericInvalid},

          {"0.49999999999999999999999999999999999999", 0, 0},
          {"0.49999999999999999999999999999999999999", 1, "0.5"},
          {"0.49999999999999999999999999999999999999", 2, "0.5"},
          {"0.49999999999999999999999999999999999999", 10, "0.5"},
          {"0.49999999999999999999999999999999999999", 20, "0.5"},
          {"0.49999999999999999999999999999999999999", 37, "0.5"},
          {"0.49999999999999999999999999999999999999", 38,
           "0.49999999999999999999999999999999999999"},
          {"0.49999999999999999999999999999999999999", 39,
           "0.49999999999999999999999999999999999999"},
      };

  static constexpr BigNumericFromStringWithRoundingOpTestData
      kRoundAwayFromZeroTestData[] = {
          {"0.45000000000000000000000000000000000000", 1, "0.5"},
          {"0.45500000000000000000000000000000000000", 2, "0.46"},
          {"0.46500000000000000000000000000000000000", 2, "0.47"},
      };

  static constexpr BigNumericFromStringWithRoundingOpTestData
      kRoundToEvenTestData[] = {
          {"56785678567856785675000000000", -10,
           "56785678567856785680000000000"},
          {"56785678567856785678500000000", -9,
           "56785678567856785678000000000"},
          {"56785678567856785678550000000", -8,
           "56785678567856785678600000000"},
          {"56785678567856785678565000000", -7,
           "56785678567856785678560000000"},
          {"56785678567856785678567500000", -6,
           "56785678567856785678568000000"},
          {"56785678567856785678567850000", -5,
           "56785678567856785678567800000"},
          {"56785678567856785678567850001", -5,
           "56785678567856785678567900000"},
          {"56785678567856785678567855000", -4,
           "56785678567856785678567860000"},
          {"56785678567856785678567856500", -3,
           "56785678567856785678567856000"},
          {"56785678567856785678567856750", -2,
           "56785678567856785678567856800"},
          {"56785678567856785678567856785", -1,
           "56785678567856785678567856780"},
          {"56785678567856785678567856785.5", 0,
           "56785678567856785678567856786"},
          {"56785678567856785678567856785.65", 1,
           "56785678567856785678567856785.6"},
          {"56785678567856785678567856785.565", 2,
           "56785678567856785678567856785.56"},
          {"56785678567856785678567856785.5675", 3,
           "56785678567856785678567856785.568"},
          {"56785678567856785678567856785.56785", 4,
           "56785678567856785678567856785.5678"},
          {"56785678567856785678567856785.567855", 5,
           "56785678567856785678567856785.56786"},
          {"56785678567856785678567856785.56786501", 5,
           "56785678567856785678567856785.56787"},
          {"56785678567856785678567856785.5678565", 6,
           "56785678567856785678567856785.567856"},
          {"56785678567856785678567856785.56785685", 7,
           "56785678567856785678567856785.5678568"},
          {"56785678567856785678567856785.567856785", 8,
           "56785678567856785678567856785.56785678"},
          {"56785678567856785678567856785.5678567885", 9,
           "56785678567856785678567856785.567856788"},
          {"56785678567856785678567856785.56785678885", 10,
           "56785678567856785678567856785.5678567888"},
          {"56785678567856785678567856785.567856788865", 11,
           "56785678567856785678567856785.56785678886"},
          {"0.49999999999999999999999999999999999450", 36,
           "0.499999999999999999999999999999999994"},
          {"0.49999999999999999999999999999999995490", 35,
           "0.49999999999999999999999999999999995"},
          {"0.49999999999999999999999999999999995499", 35,
           "0.49999999999999999999999999999999995"},
          {"0.49999999999999999999999999999999995510", 35,
           "0.49999999999999999999999999999999996"},
          {"0.49999999999999999999999999999999996501", 35,
           "0.49999999999999999999999999999999997"},
          {"0.49999999999999999999999999999999996500", 35,
           "0.49999999999999999999999999999999996"},
          {"0.49999999999999999999999999999999994500", 35,
           "0.49999999999999999999999999999999994"},
          {"0.49999999999999999999999999999999945000", 34,
           "0.4999999999999999999999999999999994"},
          {"0.49999999999999999999999999999999975000", 34,
           "0.4999999999999999999999999999999998"},
          {"0.49999999999999999999999999999999994490", 35,
           "0.49999999999999999999999999999999994"},
          {"56785678567856750000000000", -11, "56785678567856800000000000"},
          {"56785678567856850000000000", -11, "56785678567856800000000000"},
          {"56785678500000000000000000", -18, "56785678000000000000000000"},
          {"56785678567856850000000001", -11, "56785678567856900000000000"},
          {"56785678500000000000000001", -18, "56785679000000000000000000"},
          {"0.445", 2, "0.44"},
          {"0.005", 2, "0"},
          {"0.49999999999999999999999999999999999999", 37, "0.5"},
          {"0.49999999999999999999999999999999999999", 38,
           "0.49999999999999999999999999999999999999"},
          {"0.49999999999999999999999999999999999999", 39,
           "0.49999999999999999999999999999999999999"},
          {"0.45000000000000000000000000000000000000", 1, "0.4"},
          {"0.45500000000000000000000000000000000000", 2, "0.46"},
          {"0.46500000000000000000000000000000000000", 2, "0.46"},
      };

  static constexpr BigNumericFromStringWithRoundingOpTestData
      kSpecialTestData[] = {
          {kMinBigNumericValueStr, kint64min, 0},
          {kMinBigNumericValueStr, -40, 0},
          {kMinBigNumericValueStr, -39, kBigNumericInvalid},
          {kMinBigNumericValueStr, -38, kBigNumericInvalid},
          {kMinBigNumericValueStr, -37, kBigNumericInvalid},
          {kMinBigNumericValueStr, -30,
           "-578960446000000000000000000000000000000"},
          {kMinBigNumericValueStr, -29, kBigNumericInvalid},
          {kMinBigNumericValueStr, -28, kBigNumericInvalid},
          {kMinBigNumericValueStr, -27, kBigNumericInvalid},
          {kMinBigNumericValueStr, -1, kBigNumericInvalid},
          {kMinBigNumericValueStr, 0, kMinBigNumericValueStr.substr(0, 41 + 0)},
          {kMinBigNumericValueStr, 1, kMinBigNumericValueStr.substr(0, 41 + 1)},
          {kMinBigNumericValueStr, 8, kBigNumericInvalid},
          {kMinBigNumericValueStr, 9, kMinBigNumericValueStr.substr(0, 41 + 9)},
          {kMinBigNumericValueStr, 10,
           kMinBigNumericValueStr.substr(0, 41 + 10)},
          {kMinBigNumericValueStr, 27, kBigNumericInvalid},
          {kMinBigNumericValueStr, 28, kBigNumericInvalid},
          {kMinBigNumericValueStr, 29, kBigNumericInvalid},
          {kMinBigNumericValueStr, 30, kBigNumericInvalid},
          {kMinBigNumericValueStr, 37, kBigNumericInvalid},
          {kMinBigNumericValueStr, 38, kMinBigNumericValueStr},
          {kMinBigNumericValueStr, kint64max, kMinBigNumericValueStr},
      };

  for (const BigNumericFromStringWithRoundingOpTestData data :
       kCommonTestData) {
    TestBigNumericFromStringWithRoundingOp<BigNumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ true, data.expected_output);
    TestBigNumericFromStringWithRoundingOp<BigNumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ false, data.expected_output);
  }

  for (const BigNumericFromStringWithRoundingOpTestData data :
       kRoundAwayFromZeroTestData) {
    TestBigNumericFromStringWithRoundingOp<BigNumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ false, data.expected_output);
  }

  for (const BigNumericFromStringWithRoundingOpTestData data :
       kRoundToEvenTestData) {
    TestBigNumericFromStringWithRoundingOp<BigNumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ true, data.expected_output);
  }

  for (const BigNumericFromStringWithRoundingOpTestData data :
       kSpecialTestData) {
    TestBigNumericFromStringWithRoundingOp<BigNumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ true, data.expected_output);
    TestBigNumericFromStringWithRoundingOp<BigNumericValueWrapper>(
        data.input1, data.input2,
        /** round_half_even */ false, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, Ceiling) {
  static constexpr BigNumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {999, 999},
      {"999.000000001", 1000},
      {"999.00000000000000000000000000000000000001", 1000},
      {"999.999999999", 1000},
      {"999.99999999999999999999999999999999999999", 1000},
      {-999, -999},
      {"-999.000000001", -999},
      {"-999.00000000000000000000000000000000000001", -999},
      {"-999.999999999", -999},
      {"-999.99999999999999999999999999999999999999", -999},
      {"1e-38", 1},
      {"0.99999999999999999999999999999999999999", 1},
      {"-0.99999999999999999999999999999999999999", 0},
      {"-1e-38", 0},
      {kMaxNumericValueStr, "100000000000000000000000000000"},
      {kMinNumericValueStr, "-99999999999999999999999999999"},
      {"-99999999999999999999999999999.000000001",
       "-99999999999999999999999999999"},
      {kMaxBigNumericValueStr, kBigNumericOverflow},
      {kMinBigNumericValueStr, "-578960446186580977117854925043439539266"},
      {"578960446186580977117854925043439539266",
       "578960446186580977117854925043439539266"},
      {"578960446186580977117854925043439539266.1", kBigNumericOverflow},
      {"578960446186580977117854925043439539266."
       "00000000000000000000000000000000000001",
       kBigNumericOverflow},
  };

  NumericCeilingOp op;
  for (const BigNumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, HashCode) {
  TestHashCode<BigNumericValue>(kSortedBigNumericValueStringPairs);
}

TEST_F(BigNumericValueTest, FromString) {
  for (BigNumericStringTestData data : kSortedBigNumericValueStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue actual,
                         BigNumericValue().FromString(data.first));
    EXPECT_EQ(data.second, actual.ToPackedLittleEndianArray());
  }
  for (BigNumericStringTestData data : kBigNumericValueValidFromStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue actual,
                         BigNumericValue().FromString(data.first));
    EXPECT_EQ(data.second, actual.ToPackedLittleEndianArray());
  }
  for (BigNumericStringTestData data : kBigNumericValueNonStrictStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue actual,
                         BigNumericValue().FromString(data.first));
    EXPECT_EQ(data.second, actual.ToPackedLittleEndianArray());
  }
  for (absl::string_view str : kBigNumericValueInvalidStrings) {
    EXPECT_THAT(BigNumericValue().FromString(str),
                StatusIs(absl::StatusCode::kOutOfRange,
                         absl::StrCat("Invalid BIGNUMERIC value: ", str)));
  }
}

TEST_F(BigNumericValueTest, FromStringStrict) {
  for (BigNumericStringTestData data : kSortedBigNumericValueStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue actual,
                         BigNumericValue().FromStringStrict(data.first));
    EXPECT_EQ(data.second, actual.ToPackedLittleEndianArray());
  }
  for (BigNumericStringTestData data : kBigNumericValueValidFromStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue actual,
                         BigNumericValue().FromStringStrict(data.first));
    EXPECT_EQ(data.second, actual.ToPackedLittleEndianArray());
  }
  for (BigNumericStringTestData data : kBigNumericValueNonStrictStringPairs) {
    EXPECT_THAT(
        BigNumericValue().FromStringStrict(data.first),
        StatusIs(absl::StatusCode::kOutOfRange,
                 absl::StrCat("Invalid BIGNUMERIC value: ", data.first)));
  }
  for (absl::string_view str : kBigNumericValueInvalidStrings) {
    EXPECT_THAT(BigNumericValue().FromStringStrict(str),
                StatusIs(absl::StatusCode::kOutOfRange,
                         absl::StrCat("Invalid BIGNUMERIC value: ", str)));
  }
}

TEST_F(BigNumericValueTest, FromDouble) {
  static constexpr BigNumericUnaryOpTestData<double, BigNumericValueWrapper>
      kTestData[] = {
          {0, 0},
          {0.00000000000001, "0.00000000000000999999999999999998819309"},
          {0.99999999999999, "0.99999999999999000799277837359113618731"},
          {1.5, "1.5"},
          {123, 123},
          {123.5, "123.5"},
          {4503599627370496.0, "4503599627370496"},
          {4951760157141521099596496896.0, "4951760157141521099596496896"},
          {1.5e18, "1.5e18"},
          {99999999999999999999999999999999999999.99999999999999999999999999999,
           "99999999999999997748809823456034029568"},
          {578960446186580977117854925043439539266.3499233282028201972879200395,
           "578960446186580955070694765308237840384"},
          // Rounded down to the double value nearest to kMaxBigNumericValue.
          {578960446186580992849626628265399549952.0,
           "578960446186580955070694765308237840384"},
          // Rounded up and out of range.
          {578960446186580992849626628265399549952.0000000000000000000000000001,
           kBigNumericOutOfRange},

          // Check rounding
          {1048576.000000000116415321826934814453125, "1048576"},
          {1048576.000000000116415321826934814453125000000000000000000000000001,
           "1048576.00000000023283064365386962890625"},
          {0.555555555555555555555555555,
           "0.55555555555555558022717832500347867608"},
          {0.0000000001, "0.00000000010000000000000000364321973155"},
          // int256 < (value * 1e38) < uint256
          {1.0000000001e39, kBigNumericOutOfRange},
          {1e40, kBigNumericOutOfRange},
          {std::numeric_limits<double>::max(), kBigNumericOutOfRange},

          {std::numeric_limits<double>::quiet_NaN(),
           kBigNumericIllegalNonFinite},
          {std::numeric_limits<double>::signaling_NaN(),
           kBigNumericIllegalNonFinite},
          {std::numeric_limits<double>::infinity(),
           kBigNumericIllegalNonFinite},
      };

  NumericFromDoubleOp<BigNumericValue> op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(BigNumericValueTest, FromDouble_RandomInputs) {
  NumericFromDoubleOp<BigNumericValue> op;
  for (int i = 0; i < 10000; ++i) {
    uint64_t bits = absl::Uniform<uint64_t>(random_);
    double double_val = absl::bit_cast<double>(bits);
    if (!std::isfinite(double_val)) {
      TestUnaryOp(op, double_val,
                  BigNumericValueWrapper(kBigNumericIllegalNonFinite));
    } else {
      std::string str = absl::StrFormat("%.40f", double_val);
      if (absl::EndsWith(str, "50")) {
        // Strings with suffix 50 could be from doubles with suffix 49 and
        // get rounded up. In this case the test would fail while the result
        // is correct. Skipping those cases for now.
        continue;
      }
      auto expected = BigNumericValue::FromString(str);
      if (expected.ok()) {
        TestUnaryOp(op, double_val, expected);
      } else {
        TestUnaryOp(op, double_val,
                    BigNumericValueWrapper(kBigNumericOutOfRange));
      }
    }
  }
}

TEST_F(BigNumericValueTest, ToDouble) {
  static constexpr BigNumericUnaryOpTestData<BigNumericValueWrapper, double>
      kTestData[] = {
          {0, 0},
          {"1e-38", 1e-38},
          {"0.000000001", 0.000000001},
          {"0.999999999", 0.999999999},
          {"0.99999999999999999999999999999999999999",
           0.99999999999999999999999999999999999999},
          {"1.5", 1.5},
          {123, 123},
          {"123.5", 123.5},
          {"4503599627370496", 4503599627370496.0},
          {"1048576.000000000116415321826934814453125",  // test round to even
           1048576},
          {"1048576.00000000011641532182693481445312500001",
           1048576.00000000023283064365386962890625},
          {"1974613819685343985664.0249533", 1974613819685343985664.0249533},
          {kMaxBigNumericValueStr,
           // Not copying all digits from kMaxBigNumericValueStr, just to fit
           // the digits in one line. It already exceeds the max precision of
           // double type.
           578960446186580977117854925043439539266.349923328202820197287920039},
      };

  NumericToDoubleOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
  EXPECT_EQ(-578960446186580977117854925043439539266.34992332820282019728792003,
            BigNumericValue::MinValue().ToDouble());
}

TEST_F(BigNumericValueTest, ToDouble_RandomInputs) {
  for (int i = 0; i < 20000; ++i) {
    BigNumericValue v = MakeRandomBigNumeric();
    double expected;
    ASSERT_TRUE(absl::SimpleAtod(v.ToString(), &expected));
    EXPECT_EQ(expected, v.ToDouble()) << v.ToString();
  }
}

TEST_F(BigNumericValueTest, ToString) {
  for (const auto& pair : kSortedBigNumericValueStringPairs) {
    BigNumericValue value =
        BigNumericValue::FromPackedLittleEndianArray(pair.second);
    EXPECT_EQ(pair.first, value.ToString());
    static constexpr char kExistingValue[] = "existing_value_1234567890";
    std::string str = kExistingValue;
    value.AppendToString(&str);
    EXPECT_EQ(absl::StrCat(kExistingValue, pair.first), str);
  }
}

TEST_F(BigNumericValueTest, FormatAndAppend) {
  // Reuse the test data for NumericValue.
  for (const FormatTestData& data : kFormatTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(data.input1));
    std::string output = "existing_value;";
    BigNumericValue(value).FormatAndAppend(data.input2, &output);
    EXPECT_EQ(output, absl::StrCat("existing_value;", data.expected_output))
        << "Input: " << value.ToString();
  }

  // Inputs that fit input NumericValue are tested in the loop above, and
  // should not be repeated below.
  using TestData = BigNumericBinaryOpTestData<FormatSpec, absl::string_view>;
  static constexpr TestData kTestData[] = {
      {"1.99999999999999999999999999999999999999", kDefaultSpec, "2.000000"},
      {"1.99999999999999999999999999999999999999", {10}, "  2.000000"},
      {"1.99999999999999999999999999999999999999", {10, 0}, "         2"},
      {"1.99999999999999999999999999999999999999",
       {10, 38},
       "1.99999999999999999999999999999999999999"},
      {"1.99999999999999999999999999999999999999", {10, 7}, " 2.0000000"},
      {"-1.99999999999999999999999999999999999999", {10, 7}, "-2.0000000"},
      {"1.99999999999999999999999999999999999999",
       {10, 39},
       "1.999999999999999999999999999999999999990"},
      {"1.99999999999999999999999999999999999999",
       {43, 39},
       "  1.999999999999999999999999999999999999990"},
      {"-1.99999999999999999999999999999999999999",
       {43, 39},
       " -1.999999999999999999999999999999999999990"},

      {kMaxBigNumericValueStr, kDefaultSpec,
       "578960446186580977117854925043439539266.349923"},
      {kMaxBigNumericValueStr, {0, 38}, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr,
       {0, 39},
       "578960446186580977117854925043439539266."
       "349923328202820197287920039565648199670"},
      {kMaxBigNumericValueStr,
       {81, 39},
       "  578960446186580977117854925043439539266."
       "349923328202820197287920039565648199670"},
      {kMaxBigNumericValueStr,
       {40, 40},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481996700"},

      {kMinBigNumericValueStr, kDefaultSpec,
       "-578960446186580977117854925043439539266.349923"},
      {kMinBigNumericValueStr, {0, 38}, kMinBigNumericValueStr},
      {kMinBigNumericValueStr,
       {0, 39},
       "-578960446186580977117854925043439539266."
       "349923328202820197287920039565648199680"},
      {kMinBigNumericValueStr,
       {81, 39},
       " -578960446186580977117854925043439539266."
       "349923328202820197287920039565648199680"},
      {kMinBigNumericValueStr,
       {40, 40},
       "-578960446186580977117854925043439539266."
       "3499233282028201972879200395656481996800"},

      // Rounding away digits might result in value out of BIGNUMERIC range,
      // but FormatAndAppend should still output properly.
      {kMaxBigNumericValueStr,
       {0, 37},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481997"},
      {kMaxBigNumericValueStr,
       {0, 36},
       "578960446186580977117854925043439539266."
       "349923328202820197287920039565648200"},
      {kMaxBigNumericValueStr,
       {0, 35},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564820"},
      {kMaxBigNumericValueStr,
       {0, 34},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656482"},
      {kMaxBigNumericValueStr,
       {0, 33},
       "578960446186580977117854925043439539266."
       "349923328202820197287920039565648"},
      {kMaxBigNumericValueStr,
       {0, 32},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956565"},
      {kMaxBigNumericValueStr,
       {0, 31},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656"},
      {kMaxBigNumericValueStr,
       {0, 29},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003957"},
      {kMaxBigNumericValueStr,
       {0, 4},
       "578960446186580977117854925043439539266.3499"},
      {kMaxBigNumericValueStr,
       {0, 3},
       "578960446186580977117854925043439539266.350"},
      {kMaxBigNumericValueStr,
       {0, 1},
       "578960446186580977117854925043439539266.3"},
      {kMaxBigNumericValueStr,
       {0, 0},
       "578960446186580977117854925043439539266"},

      {kMinBigNumericValueStr,
       {0, 37},
       "-578960446186580977117854925043439539266."
       "3499233282028201972879200395656481997"},
      {kMinBigNumericValueStr,
       {0, 36},
       "-578960446186580977117854925043439539266."
       "349923328202820197287920039565648200"},
      {kMinBigNumericValueStr,
       {0, 35},
       "-578960446186580977117854925043439539266."
       "34992332820282019728792003956564820"},
      {kMinBigNumericValueStr,
       {0, 34},
       "-578960446186580977117854925043439539266."
       "3499233282028201972879200395656482"},
      {kMinBigNumericValueStr,
       {0, 33},
       "-578960446186580977117854925043439539266."
       "349923328202820197287920039565648"},
      {kMinBigNumericValueStr,
       {0, 32},
       "-578960446186580977117854925043439539266."
       "34992332820282019728792003956565"},
      {kMinBigNumericValueStr,
       {0, 31},
       "-578960446186580977117854925043439539266."
       "3499233282028201972879200395656"},
      {kMinBigNumericValueStr,
       {0, 29},
       "-578960446186580977117854925043439539266."
       "34992332820282019728792003957"},
      {kMinBigNumericValueStr,
       {0, 4},
       "-578960446186580977117854925043439539266.3499"},
      {kMinBigNumericValueStr,
       {0, 3},
       "-578960446186580977117854925043439539266.350"},
      {kMinBigNumericValueStr,
       {0, 1},
       "-578960446186580977117854925043439539266.3"},
      {kMinBigNumericValueStr,
       {0, 0},
       "-578960446186580977117854925043439539266"},

      // %e and %E produce mantissa/exponent notation.
      {kMaxBigNumericValueStr,
       {0, 76, e},
       "5.78960446186580977117854925043439539266"
       "34992332820282019728792003956564819967e+38"},
      {kMinBigNumericValueStr,
       {0, 76, e},
       "-5.78960446186580977117854925043439539266"
       "34992332820282019728792003956564819968e+38"},
      {kMaxBigNumericValueStr,
       {0, 80, e},
       "5.78960446186580977117854925043439539266"
       "349923328202820197287920039565648199670000e+38"},
      {kMinBigNumericValueStr,
       {0, 80, e},
       "-5.78960446186580977117854925043439539266"
       "349923328202820197287920039565648199680000e+38"},

      // Rounding cases.
      {kMaxBigNumericValueStr,
       {0, 75, e},
       "5.78960446186580977117854925043439539266"
       "3499233282028201972879200395656481997e+38"},
      {kMinBigNumericValueStr,
       {0, 75, e},
       "-5.78960446186580977117854925043439539266"
       "3499233282028201972879200395656481997e+38"},
      {kMaxBigNumericValueStr, {0, 25, E}, "5.7896044618658097711785493E+38"},
      {kMinBigNumericValueStr, {0, 25, E}, "-5.7896044618658097711785493E+38"},
      {kMaxBigNumericValueStr, {0, 22, e}, "5.7896044618658097711785e+38"},
      {kMinBigNumericValueStr, {0, 22, e}, "-5.7896044618658097711785e+38"},
      {kMaxBigNumericValueStr, {0, 5, e}, "5.78960e+38"},
      {kMinBigNumericValueStr, {0, 5, e}, "-5.78960e+38"},
      {kMaxBigNumericValueStr, {0, 0, e}, "6e+38"},
      {kMinBigNumericValueStr, {0, 0, e}, "-6e+38"},

      // %g and %G. remove_trailing_zeros_after_decimal_point is most commonly
      // used, and thus most of the test cases have this flag.
      {kMaxBigNumericValueStr, {0, 77, g, true}, kMaxBigNumericValueStr},
      {kMinBigNumericValueStr, {0, 77, G, true}, kMinBigNumericValueStr},
      {kMaxBigNumericValueStr, {0, 80, G, true}, kMaxBigNumericValueStr},
      {kMinBigNumericValueStr, {0, 80, g, true}, kMinBigNumericValueStr},
      {kMaxBigNumericValueStr,
       {0, kuint32max, g, true},
       kMaxBigNumericValueStr},
      {kMinBigNumericValueStr,
       {0, kuint32max, G, true},
       kMinBigNumericValueStr},

      // Rounding cases.
      {kMaxBigNumericValueStr, {0, 6, g, true}, "5.7896e+38"},
      {kMinBigNumericValueStr, {0, 6, G, true}, "-5.7896E+38"},
      {kMaxBigNumericValueStr,
       {0, 76, g},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481997"},
      {kMinBigNumericValueStr,
       {0, 76, G},
       "-578960446186580977117854925043439539266."
       "3499233282028201972879200395656481997"},
      {kMaxBigNumericValueStr, {0, 0, g}, "6e+38"},
      {kMinBigNumericValueStr, {0, 0, G}, "-6E+38"},
  };

  for (const TestData& data : kTestData) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue value, GetValue(data.input1));
    std::string output = "existing_value;";
    value.FormatAndAppend(data.input2, &output);
    EXPECT_EQ(output, absl::StrCat("existing_value;", data.expected_output));
  }
}

TEST_F(BigNumericValueTest, Format_Random) {
  TestFormatWithRandomValues<BigNumericValue>(&random_);
}

TEST_F(BigNumericValueTest, OperatorsTest) {
  TestComparisonOperators<BigNumericValue>(kSortedBigNumericValueStringPairs);
}

TEST_F(BigNumericValueTest, Negate) {
  static constexpr absl::string_view kValueStrings[] = {
      "0",          "1e-38", "0.5",
      "1",          "10",    "10.01",
      "1000000000", "1e38",  kMaxBigNumericValueStr,
  };
  for (absl::string_view str : kValueStrings) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue value,
                         BigNumericValue::FromString(str));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue expected_neg_value,
                         BigNumericValue::FromString(absl::StrCat("-", str)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue actual_neg_value, value.Negate());
    EXPECT_EQ(expected_neg_value, actual_neg_value);
    EXPECT_THAT(actual_neg_value.Negate(), IsOkAndHolds(value));
  }
  EXPECT_THAT(BigNumericValue::MinValue().Negate(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       absl::StrCat("BIGNUMERIC overflow: -(",
                                    kMinBigNumericValueStr, ")")));
}

TEST_F(BigNumericValueTest, SerializeDeserializeProtoBytes) {
  for (const auto& pair : kSortedBigNumericValueStringPairs) {
    BigNumericValue value =
        BigNumericValue::FromPackedLittleEndianArray(pair.second);
    TestSerialize(value);
  }
  const int kTestIterations = 500;
  for (int i = 0; i < kTestIterations; ++i) {
    TestSerialize(MakeRandomBigNumeric());
  }
}

TEST_F(BigNumericValueTest, Abs) {
  static constexpr BigNumericUnaryOpTestData<BigNumericValueWrapper,
                                             BigNumericValueWrapper>
      kTestData[] = {
          {0, 0},           {"1e-38", "1e-38"},
          {"0.1", "0.1"},   {1, 1},
          {"1.5", "1.5"},   {123, 123},
          {"1e38", "1e38"}, {kMaxBigNumericValueStr, kMaxBigNumericValueStr},
      };
  NumericAbsOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, data.expected_output);
  }
  TestUnaryOp(op, BigNumericValueWrapper(kMinBigNumericValueStr),
              BigNumericValueWrapper(kBigNumericOverflow));
}

TEST_F(BigNumericValueTest, Sign) {
  static constexpr BigNumericUnaryOpTestData<BigNumericValueWrapper, int>
      kTestData[] = {
          {0, 0},     {"1e-38", 1}, {"0.1", 1},  {1, 1},
          {"1.5", 1}, {123, 1},     {"1e38", 1}, {kMaxBigNumericValueStr, 1},
      };
  NumericSignOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
  TestUnaryOp(op, BigNumericValueWrapper(kMinBigNumericValueStr), -1);
}

TEST_F(BigNumericValueTest, SerializeDeserializeInvalidProtoBytes) {
  EXPECT_THAT(
      BigNumericValue::DeserializeFromProtoBytes(""),
      StatusIs(absl::StatusCode::kOutOfRange, "Invalid BIGNUMERIC encoding"));
  std::string bad_string(sizeof(BigNumericValue) + 1, '\x1');
  EXPECT_THAT(
      BigNumericValue::DeserializeFromProtoBytes(bad_string),
      StatusIs(absl::StatusCode::kOutOfRange, "Invalid BIGNUMERIC encoding"));
}

TEST_F(BigNumericValueTest, ToInt32) {
  NumericToIntegerOp<int32_t> op;
  for (const auto& data : kToInt32ValueTestData<BigNumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, RoundTripFromInt32) {
  TestRoundTripFromInteger<BigNumericValue, int32_t>(&random_);
}

TEST_F(BigNumericValueTest, ToUint32) {
  NumericToIntegerOp<uint32_t> op;
  for (const auto& data : kToUint32ValueTestData<BigNumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, RoundTripFromUint32) {
  TestRoundTripFromInteger<BigNumericValue, uint32_t>(&random_);
}

TEST_F(BigNumericValueTest, ToInt64) {
  NumericToIntegerOp<int64_t> op;
  for (const auto& data : kToInt64ValueTestData<BigNumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, RoundTripFromInt64) {
  TestRoundTripFromInteger<BigNumericValue, int64_t>(&random_);
}

TEST_F(BigNumericValueTest, ToUint64) {
  NumericToIntegerOp<uint64_t> op;
  for (const auto& data : kToUint64ValueTestData<BigNumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, RoundTripFromUint64) {
  TestRoundTripFromInteger<BigNumericValue, uint64_t>(&random_);
}

TEST_F(BigNumericValueTest, ToNumericValue) {
  static constexpr NumericUnaryOpTestData<BigNumericValueWrapper,
                                          NumericValueWrapper>
      kTestData[] = {
          {0, 0},
          {123, 123},
          {"123.56", "123.56"},
          {"123.5", "123.5"},
          {"123.46", "123.46"},
          {"18446744073709551615", "18446744073709551615"},
          {"18446744073709551615.499999999", "18446744073709551615.499999999"},
          {"18446744073709551615.12345678949999999999999999999999999999",
           "18446744073709551615.123456789"},
          {"18446744073709551615.5", "18446744073709551615.5"},
          {"18446744073709551615.1234567895", "18446744073709551615.12345679"},
          {"18446744073709551616", "18446744073709551616"},
          {"0.499999999", "0.499999999"},
          {"0.5", "0.5"},
          {1, 1},
          {"99999999999999999999999999999.999999999499999999",
           "99999999999999999999999999999.999999999"},
          {"99999999999999999999999999999.9999999995", kNumericOutOfRange},
          {kMaxNumericValueStr, kMaxNumericValueStr},
          {"100000000000000000000000000000", kNumericOutOfRange},
          {kMaxBigNumericValueStr, kNumericOutOfRange}};
  BigNumericToNumericOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(BigNumericValueTest, NumericValueRoundTrip) {
  for (int i = 0; i < 10000; ++i) {
    NumericValue value = MakeRandomNumeric();
    EXPECT_THAT(BigNumericValue(value).ToNumericValue(), IsOkAndHolds(value));
  }
}

TEST_F(BigNumericValueTest, FromScaledValueToFullPrecision) {
  struct FromScaledValueTestData {
    absl::string_view input_value;
    int scale;
    BigNumericValueWrapper expected_output;
  };
  constexpr absl::string_view kScaledMinBigNumericValueStr =
      "-578960446186580977117854925043439539266"
      "34992332820282019728792003956564819968";
  constexpr absl::string_view kScaledMaxBigNumericValueStr =
      "578960446186580977117854925043439539266"
      "34992332820282019728792003956564819967";
  static constexpr FromScaledValueTestData kTestDataNoRoundingRequired[] = {
      {"", kintmin, 0},
      {"", -39, 0},
      {"", 0, 0},
      {"", 39, 0},
      {"", kintmax, 0},
      {"0", kintmin, 0},
      {"0", -39, 0},
      {"0", 0, 0},
      {"0", 39, 0},
      {"0", kintmax, 0},
      {"1", kintmin, kBigNumericFromScaledValueOutOfRange},
      {"1", kintmin / 2, kBigNumericFromScaledValueOutOfRange},
      {"1", kintmin + 20, kBigNumericFromScaledValueOutOfRange},
      {"1", -39, kBigNumericFromScaledValueOutOfRange},
      {"1", -38, "1e38"},
      {"1", -1, 10},
      {"1", 0, 1},
      {"5", -38, "5e38"},
      {"6", -38, kBigNumericFromScaledValueOutOfRange},
      {"72", 0, 72},
      {"57896044618658097711785492504343953926", -1,
       "5.7896044618658097711785492504343953926e38"},
      {"57896044618658097711785492504343953927", -1,
       kBigNumericFromScaledValueOutOfRange},
      // 1e38
      {"100000000000000000000000000000000000000", 37, 10},
      {"1000000000000000000000000000000000000000", 39, 1},
      {"100000000000000000000000000000000000000"
       "00000000000000000000000000000000000000",
       76, 1},
      {"100000000000000000000000000000000000000"
       "00000000000000000000000000000000000000",
       114, "1e-38"},
      // The maximum positive/minimum negative value not causing overflow when
      // scale is 37. In the kMaxBigNumericValueStr, move the decimal digit back
      // by 1 digit, and truncate by the last digit.
      {"57896044618658097711785492504343953926"
       "63499233282028201972879200395656481996",
       37,
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564819960"},
      // The maximum positive/minimum negative value causing overflow when scale
      // is 37. In the kMaxBigNumericValueStr, move the decimal digit back by 1
      // digit, truncate by the last digit, and then increment the last digit by
      // one.
      {"57896044618658097711785492504343953926"
       "63499233282028201972879200395656481997",
       37, kBigNumericFromScaledValueOutOfRange},
      {kScaledMaxBigNumericValueStr, 38, kMaxBigNumericValueStr},
      {kScaledMaxBigNumericValueStr, 37, kBigNumericFromScaledValueOutOfRange},
      {kScaledMaxBigNumericValueStr, kintmin,
       kBigNumericFromScaledValueOutOfRange},
      {kScaledMinBigNumericValueStr, 38, kMinBigNumericValueStr},
      {kScaledMinBigNumericValueStr, 37, kBigNumericFromScaledValueOutOfRange},
      {kScaledMinBigNumericValueStr, kintmin,
       kBigNumericFromScaledValueOutOfRange},
  };
  for (const auto& data : kTestDataNoRoundingRequired) {
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/false,
                        /*round_half_even*/ false, data.expected_output);
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even*/ false, data.expected_output);
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even*/ true, data.expected_output);
  }

  static constexpr FromScaledValueTestData kTestDataWithGenericRounding[] = {
      {"1", 39, 0},
      {"449999999999999999999999999999999999999", 76, "4e-38"},
      {kScaledMaxBigNumericValueStr, 77,
       "0.57896044618658097711785492504343953927"},
      {kScaledMaxBigNumericValueStr, 110, "5.7896e-34"},
      {kScaledMaxBigNumericValueStr, 114, "6e-38"},
      {kScaledMaxBigNumericValueStr, 115, "1e-38"},
      {kScaledMaxBigNumericValueStr, 116, 0},
      {kScaledMaxBigNumericValueStr, kintmax, 0},
      {kScaledMinBigNumericValueStr, 77,
       "-0.57896044618658097711785492504343953927"},
      {kScaledMinBigNumericValueStr, 110, "-5.7896e-34"},
      {kScaledMinBigNumericValueStr, 114, "-6e-38"},
      {kScaledMinBigNumericValueStr, 115, "-1e-38"},
      {kScaledMinBigNumericValueStr, 116, 0},
      {kScaledMinBigNumericValueStr, kintmax, 0},
      {"578960446186580977117854925043439539266"
       "349923328202820197287920039565648199674",
       39, kMaxBigNumericValueStr},
      {"-578960446186580977117854925043439539266"
       "349923328202820197287920039565648199675",
       39, kMinBigNumericValueStr},
      {"-578960446186580977117854925043439539266"
       "349923328202820197287920039565648199684",
       39, kMinBigNumericValueStr},
      {"578960446186580977117854925043439539266"
       "349923328202820197287920039565648199674"
       "999999999999999999999999999999999999999",
       78, kMaxBigNumericValueStr},
      {"-578960446186580977117854925043439539266"
       "349923328202820197287920039565648199675"
       "000000000000000000000000000000000000000",
       78, kMinBigNumericValueStr},
      {"-578960446186580977117854925043439539266"
       "349923328202820197287920039565648199684"
       "999999999999999999999999999999999999999",
       78, kMinBigNumericValueStr},
      {"115792089237316195423570985008687907853"
       "269984665640564039457584007913129639935",
       39,
       "115792089237316195423570985008687907853"
       ".26998466564056403945758400791312963994"},
      {"115792089237316195423570985008687907853"
       "2699846656405640394575840079131296399355",
       39, kBigNumericFromScaledValueOutOfRange},
  };

  static constexpr FromScaledValueTestData kTestDataWithRoundHalfEven[] = {
      {"450000000000000000000000000000000000000", 76, "4e-38"},
      {"115792089237316195423570985008687907853"
       "269984665640564039457584007913129639945",
       39,
       "115792089237316195423570985008687907853"
       ".26998466564056403945758400791312963994"},
  };

  static constexpr FromScaledValueTestData
      kTestDataWithRoundHalfAwayFromZero[] = {
          {"450000000000000000000000000000000000000", 76, "5e-38"},
          {"578960446186580977117854925043439539266"
           "349923328202820197287920039565648199685",
           39,
           kBigNumericFromScaledValueOutOfRange},  // overflow after rounding
          {"578960446186580977117854925043439539266"
           "349923328202820197287920039565648199685"
           "000000000000000000000000000000000000000",
           78, kBigNumericFromScaledValueOutOfRange},
          {"115792089237316195423570985008687907853"
           "269984665640564039457584007913129639945",
           39,
           "115792089237316195423570985008687907853"
           ".26998466564056403945758400791312963995"},
      };

  for (const auto& data : kTestDataWithGenericRounding) {
    TestFromScaledValue(
        data.input_value, data.scale, BigNumericValue::kMaxFractionalDigits,
        /*allow_rounding=*/false,
        /*round_half_even=*/false,
        BigNumericValueWrapper(kBigNumericFromScaledValueRoundingNotAllowed));
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/false, data.expected_output);
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/true, data.expected_output);
  }

  for (const auto& data : kTestDataWithRoundHalfAwayFromZero) {
    TestFromScaledValue(
        data.input_value, data.scale, BigNumericValue::kMaxFractionalDigits,
        /*allow_rounding=*/false,
        /*round_half_even=*/false,
        BigNumericValueWrapper(kBigNumericFromScaledValueRoundingNotAllowed));
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/false, data.expected_output);
  }

  for (const auto& data : kTestDataWithRoundHalfEven) {
    TestFromScaledValue(
        data.input_value, data.scale, BigNumericValue::kMaxFractionalDigits,
        /*allow_rounding=*/false,
        /*round_half_even=*/false,
        BigNumericValueWrapper(kBigNumericFromScaledValueRoundingNotAllowed));
    TestFromScaledValue(data.input_value, data.scale,
                        BigNumericValue::kMaxFractionalDigits,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/true, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, FromScaledValueToDestinationScale) {
  struct FromScaledValueToParameterizedTypeTestData {
    absl::string_view input_value;
    int scale;
    int destination_scale;
    BigNumericValueWrapper expected_output;
  };
  // No rounding required when scaling up. We scale up when the destination
  // type scale is larger than the scale of the little endian input.
  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataScaledUp[] = {
          {"123456789", 5, 8, "1234.56789"},
          {"123456789", 5, 12, "1234.56789"},
          {"123456789", 2, 16, "1234567.89"},
          {"123456789", 5, 29, "1234.56789"},
          {"578960446186580977117854925043439539266"
           "3499233282028201972879200395656481995",
           37, 38,
           "578960446186580977117854925043439539266."
           "3499233282028201972879200395656481995"},
          {"578960446186580977117854925043439539266"
           "34992332820282019728792003956564819965",
           38, 38,
           "578960446186580977117854925043439539266."
           "34992332820282019728792003956564819965"},
      };

  for (const auto& data : kTestDataScaledUp) {
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/false, /*round_half_even*/ false,
                        data.expected_output, /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even*/ false,
                        data.expected_output, /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even*/ true,
                        data.expected_output, /*test_with_added_scale=*/false);
  }

  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataWithGenericRounding[] = {
          {"123456789", 5, 4, "1234.5679"},
          {"123456789", 5, 2, "1234.57"},
          {"578960446186580977117854925043439539266"
           "34992332820282019728792003956564819957",
           38, 37,
           "578960446186580977117854925043439539266."
           "3499233282028201972879200395656481996"},
          {"578960446186580977117854925043439539266"
           "34992332820282019728792003956564819967",
           38, 36, kBigNumericFromScaledValueOutOfRange},
          {"550000000000000000000000000000000000000", 76, 38, "6e-38"},
      };

  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataWithRoundHalfEven[] = {
          {"123456785", 5, 4, "1234.5678"},
          {"450000000000000000000000000000000000000", 76, 38, "4e-38"},
          {"115792089237316195423570985008687907853"
           "269984665640564039457584007913129639945",
           39, 38,
           "115792089237316195423570985008687907853."
           "26998466564056403945758400791312963994"},
      };

  static constexpr FromScaledValueToParameterizedTypeTestData
      kTestDataWithRoundHalfAwayFromZero[] = {
          {"123456785", 5, 4, "1234.5679"},
          {"450000000000000000000000000000000000000", 76, 38, "5e-38"},
          {"578960446186580977117854925043439539266"
           "269984665640564039457584007913129639945"
           "000000000000000000000000000000000000000",
           78, 38,
           "578960446186580977117854925043439539266."
           "26998466564056403945758400791312963995"},
          {"115792089237316195423570985008687907853"
           "269984665640564039457584007913129639945",
           39, 38,
           "115792089237316195423570985008687907853."
           "26998466564056403945758400791312963995"},
      };

  for (const auto& data : kTestDataWithGenericRounding) {
    TestFromScaledValue(
        data.input_value, data.scale, data.destination_scale,
        /*allow_rounding=*/false, /*round_half_even=*/false,
        BigNumericValueWrapper(kBigNumericFromScaledValueRoundingNotAllowed),
        /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even=*/false,
                        data.expected_output, /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true,
                        /*round_half_even=*/true, data.expected_output,
                        /*test_with_added_scale=*/false);
  }

  for (const auto& data : kTestDataWithRoundHalfAwayFromZero) {
    TestFromScaledValue(
        data.input_value, data.scale, data.destination_scale,
        /*allow_rounding=*/false, /*round_half_even=*/false,
        BigNumericValueWrapper(kBigNumericFromScaledValueRoundingNotAllowed),
        /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even=*/false,
                        data.expected_output, /*test_with_added_scale=*/false);
  }

  for (const auto& data : kTestDataWithRoundHalfEven) {
    TestFromScaledValue(
        data.input_value, data.scale, data.destination_scale,
        /*allow_rounding=*/false, /*round_half_even=*/false,
        BigNumericValueWrapper(kBigNumericFromScaledValueRoundingNotAllowed),
        /*test_with_added_scale=*/false);
    TestFromScaledValue(data.input_value, data.scale, data.destination_scale,
                        /*allow_rounding=*/true, /*round_half_even=*/true,
                        data.expected_output, /*test_with_added_scale=*/false);
  }
}

TEST_F(BigNumericValueTest, FromScaledValueRoundTrip) {
  TestFromScaledValueRoundTrip<BigNumericValue, 4>(&random_);
}

TEST_F(BigNumericValueTest, RescaleValueAllowRounding) {
  static constexpr BigNumericBinaryOpTestData<int64_t> kTestData[] = {
      {"1234567890123456789012345678.123456", 20,
       "1234567890.123456789012345678123456"},
      {"1234567890123456789012345678.123456", 6,
       "0.00001234567890123456789012345678123456"},
      {"1234567890123456789012345678.123456", 4,
       "0.00000012345678901234567890123456781235"},
      {"1234567890123456789012345678.123456", 3,
       "0.00000001234567890123456789012345678123"},
      {"1234567890123456789012345678.123456", 0,
       "0.00000000001234567890123456789012345678123"},
      {"99.99999", 3, "0.000000000000000000000000000000001"},
      {"99.99999", 1, "0.00000000000000000000000000000000001"},
      {kMaxBigNumericValueStr, 38, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr, 5,
       "578960.44618658097711785492504343953926634992"},
      {kMaxBigNumericValueStr, 0,
       "5.78960446186580977117854925043439539266349923"},
      {"0", 38, "0"},
      {"0", 0, "0"},
      {"123456789.123", 40, kBigNumericIllegalScale},
      {"123456789.123", -1, kBigNumericIllegalScale},
  };

  static constexpr BigNumericBinaryOpTestData<int64_t> kSpecialTestData[] = {
      {kMinBigNumericValueStr, 38, kMinBigNumericValueStr},
      {kMinBigNumericValueStr, 5,
       "-578960.44618658097711785492504343953926634992"},
      {kMinBigNumericValueStr, 0,
       "-5.78960446186580977117854925043439539266349923"},
  };

  NumericRescaleOp<true> op;
  for (const BigNumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }

  for (const BigNumericBinaryOpTestData<int64_t>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

TEST_F(BigNumericValueTest, RescaleValueNoRounding) {
  static constexpr BigNumericBinaryOpTestData<int64_t> kTestData[] = {
      {"1234567890123456789012345678.123456", 20,
       "1234567890.123456789012345678123456"},
      {"1234567890123456789012345678.123456", 6,
       "0.00001234567890123456789012345678123456"},
      {"1234567890123456789012345678.123456", 4,
       kRescaleValueRoundingNotAllowed},
      {"1234567890123456789012345678.123456", 3,
       kRescaleValueRoundingNotAllowed},
      {"1234567890123456789012345678.123456", 0,
       kRescaleValueRoundingNotAllowed},
      {"99.99999", 3, kRescaleValueRoundingNotAllowed},
      {"99.99999", 1, kRescaleValueRoundingNotAllowed},
      {kMaxBigNumericValueStr, 38, kMaxBigNumericValueStr},
      {kMaxBigNumericValueStr, 5, kRescaleValueRoundingNotAllowed},
      {kMaxBigNumericValueStr, 0, kRescaleValueRoundingNotAllowed},
      {"0", 38, "0"},
      {"0", 0, "0"},
      {"123456789.123", 40, kBigNumericIllegalScale},
      {"123456789.123", -1, kBigNumericIllegalScale},
  };

  static constexpr BigNumericBinaryOpTestData<int64_t> kSpecialTestData[] = {
    {kMinBigNumericValueStr, 38, kMinBigNumericValueStr},
    {kMinBigNumericValueStr, 5, kRescaleValueRoundingNotAllowed},
    {kMinBigNumericValueStr, 0, kRescaleValueRoundingNotAllowed},
  };

  NumericRescaleOp<false> op;
  for (const BigNumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }

  for (const BigNumericBinaryOpTestData<int64_t>& data : kSpecialTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }
}

struct BigNumericSumAggregatorTestData {
  BigNumericValueWrapper input;
  BigNumericValueWrapper expected_cumulative_sum;
  BigNumericValueWrapper expected_cumulative_avg;
};

// Cases without the min value and overflow.
static constexpr BigNumericSumAggregatorTestData
    kBigNumericSumAggregatorTestData[] = {
        {1, 1, 1},
        {0, 1, "0.5"},
        {-2, -1, "-0.33333333333333333333333333333333333333"},
        {"1e-38", "-0.99999999999999999999999999999999999999", "-0.25"},
        {"1.00000000000000000000000000000000000001", "2e-38",
         0 /* rounded down from 4e-39 */},
        {"1e-38", "3e-38", "1e-38" /* rounded up from 5e-39 */},
        {"-4e-38", "-1e-38", 0 /* rounded down from -1.4285714285...e-39 */},
        {"1e-38", 0, 0},
        {"-1e-38", "-1e-38", 0 /* rounded down from -1.11111...e-39 */},
        {-1, "-1.00000000000000000000000000000000000001", "-0.1"},
        {"1e-38", "-1", "-0.09090909090909090909090909090909090909"
         /* rounded down from -0.090909... with repeating digit 09 */},
        {"-999999999999999998.999999999999999999",
         "-999999999999999999.999999999999999999",
         "-83333333333333333.33333333333333333325"},
        {"-999999999999999999.999999999999999999",
         "-1999999999999999999.999999999999999998",
         "-153846153846153846.153846153846153846"},
        {"1999999999999999998.999999999999999998", "-1",
         "-0.07142857142857142857142857142857142857"
         /* rounded down from actual value with repeating digits 714285 */},
        {"10000000000000000000000000000000000000",
         "9999999999999999999999999999999999999",
         "666666666666666666666666666666666666.6"},
};

// Cases with the min value and overflow.
static constexpr BigNumericSumAggregatorTestData
    kBigNumericSumAggregatorSpecialTestData[] = {
        {kMaxBigNumericValueStr, kMaxBigNumericValueStr,
         kMaxBigNumericValueStr},
        // Avg rounded up from 289480223093290488558927462521719769633.
        // 174961664101410098643960019782824099835
        {0, kMaxBigNumericValueStr,
         "289480223093290488558927462521719769633"
         ".17496166410141009864396001978282409984"},
        {"1e-38", kBigNumericOverflow,
         "192986815395526992372618308347813179755."
         "44997444273427339909597334652188273323"},
        // Actual sum = max * 2 + 1e-38
        // Avg rounded up from 289480223093290488558927462521719769633.
        // 1749616641014100986439600197828240998375
        {kMaxBigNumericValueStr, kBigNumericOverflow,
         "289480223093290488558927462521719769633."
         "174961664101410098643960019782824099838"},
        // Actual sum = max * 3 + 1e-38
        {kMaxBigNumericValueStr, kBigNumericOverflow,
         "347376267711948586270712955026063723559."
         "809953996921692118372752023739388919804"},
        // Actual sum = max * 2
        // Avg rounded down from 192986815395526992372618308347813179755.
        // 449974442734273399095973346521882733223333... with repeating digit 3
        {kMinBigNumericValueStr, kBigNumericOverflow,
         "192986815395526992372618308347813179755."
         "449974442734273399095973346521882733223"},
        // Avg rounded up from 82708635169511568159693560720491362752.
        // 33570333260040288532684571993794974280857142857142... with
        // repeating digits 857142
        {kMinBigNumericValueStr,
         "578960446186580977117854925043439539266."
         "34992332820282019728792003956564819966",
         "82708635169511568159693560720491362752."
         "33570333260040288532684571993794974281"},
        // Avg rounded down from 2.5e-39
        {kMinBigNumericValueStr, "-2e-38", 0},
        // Avg rounded down from 1.1111...e-39 with repeating digits 1
        {"1e-38", "-1e-38", 0},
        // Avg rounded up from -578960446186580977117854925043439539266.
        // 34992332820282019728792003956564819969
        {kMinBigNumericValueStr,
         kBigNumericOverflow /* actual sum = min - 1e-38*/,
         "-57896044618658097711785492504343953926."
         "63499233282028201972879200395656481997"},
        // Actual sum = min * 2 - 1e-38
        // Avg rounded up from -105265535670287450385064531826079916230.
        // 24544060512778549041598546173920876357909090909090... with
        // repeating digits 90
        {kMinBigNumericValueStr, kBigNumericOverflow,
         "-105265535670287450385064531826079916230."
         "2454406051277854904159854617392087635791"},
        // Actual sum = min - 2e-38
        // Avg rounded up from -48246703848881748093154577086953294938.
        // 8624936106835683497739933366304706833083333333... with repeating
        // digit 3
        {kMaxBigNumericValueStr, kBigNumericOverflow,
         "-48246703848881748093154577086953294938."
         "86249361068356834977399333663047068331"},
        // Avg rounded up from 44535418937429305932142686541803041482.
        // 02691717909252463056060923381274216920615384615384... with
        // repeating digits 615384
        {"2e-38", kMinBigNumericValueStr,
         "-44535418937429305932142686541803041482."
         "02691717909252463056060923381274216921"},
};

TEST(BigNumericSumAggregatorTest, Sum) {
  // Test exactly one input value.
  for (BigNumericStringTestData data : kBigNumericValueValidFromStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue input,
                         BigNumericValue().FromString(data.first));
    BigNumericValue::SumAggregator aggregator;
    aggregator.Add(input);
    EXPECT_THAT(aggregator.GetSum(), IsOkAndHolds(input));
  }

  // Test cumulative sum with different inputs not involving min and overflow.
  CumulativeSumOp<BigNumericValue> sum_op;
  EXPECT_THAT(sum_op.aggregator.GetSum(), IsOkAndHolds(BigNumericValue(0)));
  CumulativeSumOp<BigNumericValue> negated_sum_op;
  for (const BigNumericSumAggregatorTestData& data :
       kBigNumericSumAggregatorTestData) {
    TestUnaryOp(sum_op, data.input, data.expected_cumulative_sum);
    TestUnaryOp(negated_sum_op, -data.input, -data.expected_cumulative_sum);
  }

  // Test cumulative sum with different inputs with min and overflow.
  CumulativeSumOp<BigNumericValue> overflow_sum_op;
  for (const BigNumericSumAggregatorTestData& data :
       kBigNumericSumAggregatorSpecialTestData) {
    TestUnaryOp(overflow_sum_op, data.input, data.expected_cumulative_sum);
  }
}

TEST(BigNumericSumAggregatorTest, Avg) {
  // Test repeated inputs with same value.
  for (BigNumericStringTestData data : kBigNumericValueValidFromStringPairs) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue input,
                         BigNumericValue().FromString(data.first));
    BigNumericValue::SumAggregator aggregator;
    for (uint64_t count = 1; count <= 1000; ++count) {
      aggregator.Add(input);
      EXPECT_THAT(aggregator.GetAverage(count), IsOkAndHolds(input));
    }
  }

  // Test cumulative average with different inputs not involving min.
  CumulativeAverageOp<BigNumericValue> avg_op;
  EXPECT_THAT(avg_op.aggregator.GetAverage(0),
              StatusIs(absl::StatusCode::kOutOfRange, "division by zero: AVG"));
  CumulativeAverageOp<BigNumericValue> negated_avg_op;
  for (const BigNumericSumAggregatorTestData& data :
       kBigNumericSumAggregatorTestData) {
    TestUnaryOp(avg_op, data.input, data.expected_cumulative_avg);
    TestUnaryOp(negated_avg_op, -data.input, -data.expected_cumulative_avg);
  }

  // Test cumulative average with different inputs with min and sum overflow.
  CumulativeAverageOp<BigNumericValue> special_avg_op;
  for (const BigNumericSumAggregatorTestData& data :
       kBigNumericSumAggregatorSpecialTestData) {
    TestUnaryOp(special_avg_op, data.input, data.expected_cumulative_avg);
  }
}

template <int kNumInputs>
void TestBigNumericSumAggregatorSubtract(
    const BigNumericSumAggregatorTestData (&test_data)[kNumInputs]) {
  BigNumericValue::SumAggregator aggregator;
  for (const BigNumericSumAggregatorTestData& data : test_data) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue input, GetValue(data.input));
    aggregator.Add(input);
  }
  CumulativeSubtractOp<BigNumericValue> op;
  op.aggregator = aggregator;
  for (int i = kNumInputs - 1; i > 0; --i) {
    TestUnaryOp(op, test_data[i].input,
                test_data[i - 1].expected_cumulative_sum);
  }
  TestUnaryOp(op, test_data[0].input, BigNumericValueWrapper(0));
}

TEST(BigNumericSumAggregatorTest, Subtract) {
  constexpr int kNumInputs = ABSL_ARRAYSIZE(kBigNumericSumAggregatorTestData);
  // Test with sliding window size equals one
  BigNumericValue::SumAggregator aggregator;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue previous_input,
                       GetValue(kBigNumericSumAggregatorTestData[0].input));
  aggregator.Add(previous_input);
  for (int i = 1; i < kNumInputs; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue input,
                         GetValue(kBigNumericSumAggregatorTestData[i].input));
    aggregator.Add(input);
    aggregator.Subtract(previous_input);
    previous_input = input;
    EXPECT_THAT(aggregator.GetSum(), IsOkAndHolds(input));
  }
  // Test add and subtract multiple inputs
  TestBigNumericSumAggregatorSubtract(kBigNumericSumAggregatorTestData);
  TestBigNumericSumAggregatorSubtract(kBigNumericSumAggregatorSpecialTestData);
}

static constexpr BigNumericValueWrapper kBigNumericUnaryAggregatorTestInputs[] =
    {kMaxBigNumericValueStr,
     0,
     "1e-38",
     kMaxBigNumericValueStr,
     kMaxBigNumericValueStr,
     kMinBigNumericValueStr,
     kMinBigNumericValueStr,
     kMinBigNumericValueStr,
     "1e-38",
     kMinBigNumericValueStr,
     kMinBigNumericValueStr,
     kMaxBigNumericValueStr,
     "2e-38"};

TEST(BigNumericSumAggregatorTest, MergeWith) {
  TestAggregatorMergeWith<BigNumericValue::SumAggregator>(
      kBigNumericUnaryAggregatorTestInputs);
}

TEST(BigNumericSumAggregatorTest, Serialization) {
  TestAggregatorSerialization<BigNumericValue::SumAggregator>(
      kBigNumericUnaryAggregatorTestInputs);
}

TEST_F(BigNumericValueTest, VarianceAggregator) {
  using W = BigNumericValueWrapper;
  struct VarianceTestData {
    std::initializer_list<W> inputs;
    std::optional<double> expect_var_pop;
    std::optional<double> expect_var_samp;
  };

  constexpr W kMax(kMaxBigNumericValueStr);
  const VarianceTestData kTestData[] = {
      {{}, std::nullopt, std::nullopt},
      {{0}, 0.0, std::nullopt},
      {{kMax}, 0.0, std::nullopt},
      {{0, "1e-9"}, 2.5e-19, 5e-19},
      {{0, "1e-19"}, 2.5e-39, 5e-39},
      {{0, "1e-38"}, 2.5e-77, 5e-77},
      {{kMax, kMax}, 0.0, 0.0},
      {{2, 2, -3, 2, 2}, 4, 5},
      {{1, -1, 1, 1, 0}, 0.64, 0.8},
      {{2, 4, 2, 5, -3, 2, 2, -W(5), -W(4)}, 4, 5},  // subtract 5 and 4
      {{1, -100, -1, 200, 1, -W(-100), 1, -W(200), 0}, 0.64, 0.8},
      {{kMax, -W(kMax)}, std::nullopt, std::nullopt},
  };

  for (const VarianceTestData& test_data : kTestData) {
    BigNumericValue::VarianceAggregator agg;
    BigNumericValue::VarianceAggregator neg_agg;
    uint64_t count = 0;
    for (const W& input : test_data.inputs) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(BigNumericValue value, GetValue(input));
      // Interpret input.negate as subtraction.
      if (input.negate) {
        // Negate value to get the original value before negation.
        agg.Subtract(value.Negate().value());
        neg_agg.Subtract(value);
        --count;
      } else {
        agg.Add(value);
        neg_agg.Add(value.Negate().value());
        ++count;
      }
    }
    VerifyVarianceAggregator(agg, test_data.expect_var_pop,
                             test_data.expect_var_samp, count);
    VerifyVarianceAggregator(neg_agg, test_data.expect_var_pop,
                             test_data.expect_var_samp, count);
  }
}

TEST_F(BigNumericValueTest, VarianceAggregatorManyValues) {
  TestVarianceAggregatorManyValues<BigNumericValue>();
}

TEST_F(BigNumericValueTest, VarianceAggregatorMergeWith) {
  TestAggregatorMergeWith<BigNumericValue::VarianceAggregator>(
      kBigNumericUnaryAggregatorTestInputs);
}

TEST_F(BigNumericValueTest, VarianceAggregatorSerialization) {
  TestAggregatorSerialization<BigNumericValue::VarianceAggregator>(
      kBigNumericUnaryAggregatorTestInputs);
}

TEST_F(BigNumericValueTest, CovarianceAggregator) {
  using W = BigNumericValueWrapper;
  struct CovarianceTestData {
    std::initializer_list<std::pair<W, W>> inputs;
    std::optional<double> expect_var_pop;
    std::optional<double> expect_var_samp;
  };

  constexpr W kMax(kMaxBigNumericValueStr);
  const CovarianceTestData kTestData[] = {
      {{}, std::nullopt, std::nullopt},
      {{{1, 1}}, 0, std::nullopt},
      {{{kMax, kMax}}, 0, std::nullopt},
      {{{0, "1e-9"}, {"1e-9", 0}}, -2.5e-19, -5e-19},
      {{{0, "1e-19"}, {"1e-19", 0}}, -2.5e-39, -5e-39},
      {{{0, "1e-38"}, {"1e-38", 0}}, -2.5e-77, -5e-77},
      {{{kMax, kMax}, {kMax, kMax}}, 0, 0},
      {{{kMax, kMax}, {kMax, kMax}}, 0, 0},
      {{{"1.2", 5}, {"-2.4", 15}, {"3.6", -20}, {"4.8", 30}, {6, 35}},
       16.08,
       20.1},
      {{{100, 3}, {200, 7}, {300, 11}, {400, 13}, {600, 17}}, 816, 1020},
      {{{"1.2", 5},
        {"-2.4", 15},
        {17, "-3.5"},
        {"3.6", -20},
        {"2.4", -8},
        {"4.8", 30},
        {-W("2.4"), -W(-8)},
        {6, 35},
        {-W(17), -W("-3.5")}},
       16.08,
       20.1},
  };

  for (const CovarianceTestData& test_data : kTestData) {
    BinaryAggregators<BigNumericValue::CovarianceAggregator,
                      BigNumericValueWrapper>
        aggregators;
    for (const std::pair<W, W>& input : test_data.inputs) {
      aggregators.Add(input);
    }
    VerifyCovariance(aggregators.agg, test_data.expect_var_pop,
                     test_data.expect_var_samp, aggregators.count);
    VerifyCovariance(aggregators.neg_agg, test_data.expect_var_pop,
                     test_data.expect_var_samp, aggregators.count);
    VerifyCovariance(aggregators.partial_neg_agg, -test_data.expect_var_pop,
                     -test_data.expect_var_samp, aggregators.count);
  }
}

static constexpr std::pair<BigNumericValueWrapper, BigNumericValueWrapper>
    kBigNumericBinaryAggregatorTestInputs[] = {
        {0, 8},
        {1, 7},
        {-1, 2},
        {kMaxBigNumericValueStr, "-53.8"},
        {"-123.01", kMaxBigNumericValueStr},
        {kMinBigNumericValueStr, kMinBigNumericValueStr},
        {kMinBigNumericValueStr, kMinBigNumericValueStr},
        {kMinBigNumericValueStr, kMaxBigNumericValueStr},
        {kMaxBigNumericValueStr, "32.999999999"},
        {kMaxBigNumericValueStr, kMaxBigNumericValueStr},
        {kMaxBigNumericValueStr, kMaxBigNumericValueStr},
        {"56.999999999", kMaxBigNumericValueStr}};

TEST_F(BigNumericValueTest, CovarianceAggregatorMergeWith) {
  TestAggregatorMergeWith<BigNumericValue::CovarianceAggregator>(
      kBigNumericBinaryAggregatorTestInputs);
}

TEST_F(BigNumericValueTest, CovarianceAggregatorSerialization) {
  TestAggregatorSerialization<BigNumericValue::CovarianceAggregator>(
      kBigNumericBinaryAggregatorTestInputs);
}

TEST_F(BigNumericValueTest, CorrelationAggregator) {
  using W = BigNumericValueWrapper;
  struct CorrelationTestData {
    std::initializer_list<std::pair<W, W>> inputs;
    std::optional<double> expect_corr;
  };

  constexpr W kMax(kMaxBigNumericValueStr);
  // clang-format off
  const CorrelationTestData kTestData[] = {
      {{}, std::nullopt},
      {{{1, 1}}, std::nullopt},
      {{{kMax, kMax}}, std::nullopt},
      {{{1, 1}, {1, 1}}, std::numeric_limits<double>::quiet_NaN()},
      {{{1, 1}, {-1, -1}}, 1},
      {{{"0", "1e9"}, {"1e9", "0"}}, -1},
      {{{"0", "1e19"}, {"1e19", "0"}}, -1},
      {{{"0", "1e38"}, {"1e38", "0"}}, -1},
      {{{"1", "2"}, {"1e19", "2e19"}}, 1},
      {{{"1", "2"}, {"1e38", "2e38"}}, 1},
      {{{"1", "1e19"}, {"2", "2e19"}}, 1},
      {{{"1", "1e38"}, {"2", "2e38"}}, 1},
      {{{1, 5}, {"1.5", 15}, {2, 20}, {"2.5", 25}, {3, 35}},
       0.98994949366116658},  // sqrt(0.98)
      {{{1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}},
       std::numeric_limits<double>::quiet_NaN()},
      {{{8, -2},
        {1, 5},
        {3, 35},
        {"1.5", 15},
        {2, 20},
        {"2.5", 25},
        {-W(8), -W(-2)},
        {-W(3), -W(35)},
        {3, 35}},
       0.98994949366116658},  // sqrt(0.98)
  };
  // clang-format on
  for (const CorrelationTestData& test_data : kTestData) {
    BinaryAggregators<BigNumericValue::CorrelationAggregator,
                      BigNumericValueWrapper>
        aggregators;
    for (const std::pair<W, W>& input : test_data.inputs) {
      aggregators.Add(input);
    }
    VerifyCorrelation(aggregators.agg, test_data.expect_corr,
                      aggregators.count);
    VerifyCorrelation(aggregators.neg_agg, test_data.expect_corr,
                      aggregators.count);
    VerifyCorrelation(aggregators.partial_neg_agg, -test_data.expect_corr,
                      aggregators.count);
  }
}

TEST_F(BigNumericValueTest, CorrelationAggregatorMergeWith) {
  TestAggregatorMergeWith<BigNumericValue::CorrelationAggregator>(
      kBigNumericBinaryAggregatorTestInputs);
}

TEST_F(BigNumericValueTest, CorrelationAggregatorSerialization) {
  TestAggregatorSerialization<BigNumericValue::CorrelationAggregator>(
      kBigNumericBinaryAggregatorTestInputs);
}

TEST(VarNumericValueTest, ToString) {
  struct TestData {
    absl::string_view input;
    uint scale;
    absl::string_view expected_output;
  };
  static constexpr TestData kTestData[] = {
      {"", 0, "0"},
      {"", 1, "0"},
      {"", kuint32max, "0"},
      {"0", 0, "0"},
      {"0", 1, "0"},
      {"0", kuint32max, "0"},
      {"1", 0, "1"},
      {"1", 1, "0.1"},
      {"1", 58, "0.0000000000000000000000000000000000000000000000000000000001"},
      {"10", 0, "10"},
      {"10", 1, "1"},
      {"10", 2, "0.1"},
      {"10", 58, "0.000000000000000000000000000000000000000000000000000000001"},
      {"12", 0, "12"},
      {"12", 1, "1.2"},
      {"12", 2, "0.12"},
      {"12", 57, "0.000000000000000000000000000000000000000000000000000000012"},
      {"9999999999999999999999999999", 0, "9999999999999999999999999999"},
      {"9999999999999999999999999999", 1, "999999999999999999999999999.9"},
      {"9999999999999999999999999999", 27, "9.999999999999999999999999999"},
      {"9999999999999999999999999999", 28, "0.9999999999999999999999999999"},
      {"9999999999999999999999999999", 29, "0.09999999999999999999999999999"},
      {"9999999999999999999999999999", 54,
       "0.000000000000000000000000009999999999999999999999999999"},
      {"100000000000000000000000000000", 0, "100000000000000000000000000000"},
      {"100000000000000000000000000000", 28, "10"},
      {"100000000000000000000000000000", 29, "1"},
      {"100000000000000000000000000000", 30, "0.1"},
      {"100000000000000000000000000000", 58, "0.00000000000000000000000000001"},
  };
  for (const TestData& data : kTestData) {
    std::string output = "ExistingValue";
    if (data.input.empty()) {
      VarNumericValue value =
          VarNumericValue::FromScaledLittleEndianValue(data.input, data.scale);
      EXPECT_EQ(data.expected_output, value.ToString());
      value.AppendToString(&output);
      EXPECT_EQ(absl::StrCat("ExistingValue", data.expected_output), output);
      continue;
    }
    FixedInt<64, 8> input_value;
    ASSERT_TRUE(input_value.ParseFromStringStrict(data.input)) << data.input;
    std::string expected_output = std::string(data.expected_output);
    for (bool negated : {false, true}) {
      SCOPED_TRACE(absl::StrCat("input: ", data.input, " negated: ", negated));
      if (negated) {
        input_value = -input_value;
        if (expected_output != "0") {
          expected_output.insert(expected_output.begin(), 1, '-');
        }
      }
      std::string bytes;
      input_value.SerializeToBytes(&bytes);
      VarNumericValue value =
          VarNumericValue::FromScaledLittleEndianValue(bytes, data.scale);
      EXPECT_EQ(expected_output, value.ToString());
      output = "ExistingValue";
      value.AppendToString(&output);
      EXPECT_EQ(absl::StrCat("ExistingValue", expected_output), output);

      for (uint32_t extra_scale : {1, 2, 5, 9, 38, 76, 100}) {
        if (static_cast<int64_t>(extra_scale) + data.scale > kuint32max) break;
        FixedInt<64, 8> extra_scaled_value = input_value;
        if (extra_scaled_value.MultiplyOverflow(
                FixedInt<64, 8>::PowerOf10(extra_scale))) {
          break;
        }
        SCOPED_TRACE(absl::StrCat("extra_scale: ", extra_scale));
        bytes.clear();
        extra_scaled_value.SerializeToBytes(&bytes);
        // Append some redundant bytes that do not affect the result.
        bytes.append(bytes.size(), input_value.is_negative() ? '\xff' : '\x00');
        value = VarNumericValue::FromScaledLittleEndianValue(
            bytes, data.scale + extra_scale);
        EXPECT_EQ(expected_output, value.ToString());
        output = "ExistingValue";
        value.AppendToString(&output);
        EXPECT_EQ(absl::StrCat("ExistingValue", expected_output), output);
      }
    }
  }
}

template <typename T>
void TestVarNumericValueTestToStringWithRandomNumericValues() {
  absl::BitGen random;
  for (int i = 0; i < 10000; ++i) {
    T value = MakeRandomNumericValue<T>(&random);
    std::string bytes = value.SerializeAsProtoBytes();
    VarNumericValue var_value = VarNumericValue::FromScaledLittleEndianValue(
        bytes, T::kMaxFractionalDigits);
    EXPECT_EQ(value.ToString(), var_value.ToString());
  }
}

TEST(VarNumericValueTest, ToString_RandomData) {
  TestVarNumericValueTestToStringWithRandomNumericValues<NumericValue>();
  TestVarNumericValueTestToStringWithRandomNumericValues<BigNumericValue>();
}
}  // namespace
}  // namespace googlesql
