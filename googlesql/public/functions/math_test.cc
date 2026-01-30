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

#include "googlesql/public/functions/math.h"

#include <cstdint>
#include <limits>
#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/functions/rounding_mode.pb.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

template <typename T>
inline T GetDummyValue() {
  return 0xDEADBEEF;
}

template <>
inline NumericValue GetDummyValue<NumericValue>() {
  return NumericValue(0xDEADBEEFll);
}

template <>
inline BigNumericValue GetDummyValue<BigNumericValue>() {
  return BigNumericValue(0xDEADBEEFll);
}

template <typename T>
void CompareResult(const QueryParamsWithResult& param,
                   const absl::Status& actual_status, T actual_value) {
  const Value& expected = param.result();
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), actual_status);
    ASSERT_EQ(expected.type_kind(), Value::MakeNull<T>().type_kind());
    if (isnan(expected.Get<T>())) {
      EXPECT_TRUE(isnan(actual_value)) << actual_value;
    } else if (isinf(expected.Get<T>())) {
      EXPECT_EQ(expected.Get<T>(), actual_value);
    } else if (std::numeric_limits<T>::is_integer ||
               param.float_margin().IsExactEquality()) {
      EXPECT_EQ(expected.Get<T>(), actual_value);
    } else {
      EXPECT_TRUE(param.float_margin().Equal(expected.Get<T>(), actual_value))
          << param.float_margin().PrintError(expected.Get<T>(), actual_value);
    }
  } else {
    // Check for the first parameter in the error message.
    EXPECT_THAT(actual_status, ::absl_testing::StatusIs(
                                   absl::StatusCode::kOutOfRange,
                                   ::testing::HasSubstr(absl::StrFormat(
                                       "%v", param.param(0).Get<T>()))));
  }
}

template <>
void CompareResult<NumericValue>(
    const QueryParamsWithResult& param,
    const absl::Status& actual_status, NumericValue actual_value) {
  // This assumes that the value is stored under NUMERIC feature set but
  // this should work with the default feature set too.
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), actual_status);
    ASSERT_EQ(param.result().type_kind(),
              Value::MakeNull<NumericValue>().type_kind());
    EXPECT_EQ(param.result().Get<NumericValue>(), actual_value);
  } else {
    // Check for the first parameter in the error message.
    EXPECT_THAT(actual_status,
                ::absl_testing::StatusIs(
                    absl::StatusCode::kOutOfRange,
                    ::testing::HasSubstr(
                        param.param(0).Get<NumericValue>().ToString())));
  }
}

template <>
void CompareResult<BigNumericValue>(const QueryParamsWithResult& param,
                                    const absl::Status& actual_status,
                                    BigNumericValue actual_value) {
  // This assumes that the value is stored under BIGNUMERIC feature set but
  // this should work with the default feature set too.
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), actual_status);
    ASSERT_EQ(param.result().type_kind(),
              Value::MakeNull<BigNumericValue>().type_kind());
    EXPECT_EQ(param.result().Get<BigNumericValue>(), actual_value);
  } else {
    // Check for the first parameter in the error message.
    EXPECT_THAT(actual_status,
                ::absl_testing::StatusIs(
                    absl::StatusCode::kOutOfRange,
                    ::testing::HasSubstr(
                        param.param(0).Get<BigNumericValue>().ToString())));
  }
}

template <>
void CompareResult<bool>(const QueryParamsWithResult& param,
                         const absl::Status& actual_status, bool actual_value) {
  const Value& expected = param.result();
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), actual_status);
    ASSERT_EQ(expected.type_kind(), Value::MakeNull<bool>().type_kind());
    EXPECT_EQ(expected.Get<bool>(), actual_value);
  } else {
    // Check for the first parameter in the error message.
    EXPECT_THAT(
        actual_status,
        ::absl_testing::StatusIs(
            absl::StatusCode::kOutOfRange,
            ::testing::HasSubstr(absl::StrCat(param.param(0).Get<bool>()))));
  }
}

template <typename OutType>
void TestNullaryFunction(const QueryParamsWithResult& param,
                         OutType (*function)()) {
  ABSL_CHECK_EQ(0, param.num_params());
  return CompareResult(param, absl::OkStatus(), function());
}

template <typename InType, typename OutType>
void TestUnaryFunction(const QueryParamsWithResult& param,
                       bool (*function)(InType, OutType*,
                           absl::Status* error)) {
  ABSL_CHECK_EQ(1, param.num_params());
  const Value& input1 = param.param(0);
  if (input1.is_null()) {
    return;
  }

  OutType out = GetDummyValue<OutType>();
  absl::Status status;  // actual status
  function(input1.Get<InType>(), &out, &status);
  return CompareResult(param, status, out);
}

template <typename InType1, typename InType2, typename OutType>
void TestBinaryFunction(const QueryParamsWithResult& param,
                        bool (*function)(InType1, InType2, OutType*,
                            absl::Status* error)) {
  ABSL_CHECK_EQ(2, param.num_params());
  const Value& input1 = param.param(0);
  const Value& input2 = param.param(1);
  if (input1.is_null() || input2.is_null()) {
    return;
  }

  OutType out = GetDummyValue<OutType>();
  absl::Status status;  // actual status
  function(input1.Get<InType1>(), input2.Get<InType2>(), &out, &status);
  return CompareResult(param, status, out);
}

template <typename InType1, typename InType2, typename InType3,
          typename OutType>
void TestTernaryRoundFunction(const QueryParamsWithResult& param,
                              bool (*function)(InType1, InType2, InType3,
                                               OutType*, absl::Status* error)) {
  ABSL_CHECK_EQ(3, param.num_params());
  const Value& input1 = param.param(0);
  const Value& input2 = param.param(1);
  const Value& input3 = param.param(2);
  if (input1.is_null() || input2.is_null() || input3.is_null()) {
    return;
  }

  OutType out = GetDummyValue<OutType>();
  absl::Status status;  // actual status
  const RoundingMode rounding_mode =
      static_cast<RoundingMode>(input3.enum_value());
  function(input1.Get<InType1>(), input2.Get<InType2>(), rounding_mode, &out,
           &status);
  return CompareResult(param, status, out);
}

typedef testing::TestWithParam<FunctionTestCall> MathTemplateTest;
TEST_P(MathTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  if (function == "abs") {
    switch (param.params.result().type()->kind()) {
      case TYPE_INT32:
        return TestUnaryFunction(param.params, &Abs<int32_t>);
      case TYPE_INT64:
        return TestUnaryFunction(param.params, &Abs<int64_t>);
      case TYPE_UINT32:
        return TestUnaryFunction(param.params, &Abs<uint32_t>);
      case TYPE_UINT64:
        return TestUnaryFunction(param.params, &Abs<uint64_t>);
      case TYPE_FLOAT:
        return TestUnaryFunction(param.params, &Abs<float>);
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Abs<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Abs<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Abs<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "sign") {
    switch (param.params.result().type()->kind()) {
      case TYPE_INT32:
        return TestUnaryFunction(param.params, &Sign<int32_t>);
      case TYPE_INT64:
        return TestUnaryFunction(param.params, &Sign<int64_t>);
      case TYPE_UINT32:
        return TestUnaryFunction(param.params, &Sign<uint32_t>);
      case TYPE_UINT64:
        return TestUnaryFunction(param.params, &Sign<uint64_t>);
      case TYPE_FLOAT:
        return TestUnaryFunction(param.params, &Sign<float>);
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Sign<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Sign<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Sign<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "is_inf") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        return TestUnaryFunction(param.params, &IsInf<float>);
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &IsInf<double>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "is_nan") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        return TestUnaryFunction(param.params, &IsNan<float>);
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &IsNan<double>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "ieee_divide") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        return TestBinaryFunction(param.params, &IeeeDivide<float>);
      case TYPE_DOUBLE:
        return TestBinaryFunction(param.params, &IeeeDivide<double>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "sqrt") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Sqrt<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Sqrt<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Sqrt<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "cbrt") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Cbrt<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Cbrt<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Cbrt<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "pow" || function == "power") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestBinaryFunction(param.params, &Pow<double>);
      case TYPE_NUMERIC:
        return TestBinaryFunction(param.params, &Pow<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestBinaryFunction(param.params, &Pow<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "exp") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Exp<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Exp<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Exp<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "ln") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &NaturalLogarithm<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &NaturalLogarithm<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params,
                                 &NaturalLogarithm<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "log") {
    if (param.params.num_params() == 1) {
      switch (param.params.param(0).type_kind()) {
        case TYPE_DOUBLE:
          return TestUnaryFunction(param.params, &NaturalLogarithm<double>);
        case TYPE_NUMERIC:
          return TestUnaryFunction(param.params,
                                   &NaturalLogarithm<NumericValue>);
        case TYPE_BIGNUMERIC:
          return TestUnaryFunction(param.params,
                                   &NaturalLogarithm<BigNumericValue>);
        default:
          FAIL() << "unrecognized type for " << function;
      }
    } else {
      switch (param.params.param(0).type_kind()) {
        case TYPE_DOUBLE:
          return TestBinaryFunction(param.params, &Logarithm<double>);
        case TYPE_NUMERIC:
          return TestBinaryFunction(param.params, &Logarithm<NumericValue>);
        case TYPE_BIGNUMERIC:
          return TestBinaryFunction(param.params, &Logarithm<BigNumericValue>);
        default:
          FAIL() << "unrecognized type for " << function;
      }
    }
  } else if (function == "log10") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &DecimalLogarithm<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &DecimalLogarithm<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params,
                                 &DecimalLogarithm<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "radians") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Radians<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Radians<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Radians<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "degrees") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Degrees<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Degrees<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Degrees<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "pi") {
    return TestNullaryFunction(param.params, &Pi);
  } else if (function == "pi_numeric") {
    return TestNullaryFunction(param.params, &Pi_Numeric);
  } else if (function == "pi_bignumeric") {
    return TestNullaryFunction(param.params, &Pi_BigNumeric);
  } else if (function == "cos") {
    return TestUnaryFunction(param.params, &Cos<double>);
  } else if (function == "acos") {
    return TestUnaryFunction(param.params, &Acos<double>);
  } else if (function == "cosh") {
    return TestUnaryFunction(param.params, &Cosh<double>);
  } else if (function == "acosh") {
    return TestUnaryFunction(param.params, &Acosh<double>);
  } else if (function == "sin") {
    return TestUnaryFunction(param.params, &Sin<double>);
  } else if (function == "asin") {
    return TestUnaryFunction(param.params, &Asin<double>);
  } else if (function == "sinh") {
    return TestUnaryFunction(param.params, &Sinh<double>);
  } else if (function == "asinh") {
    return TestUnaryFunction(param.params, &Asinh<double>);
  } else if (function == "tan") {
    return TestUnaryFunction(param.params, &Tan<double>);
  } else if (function == "atan") {
    return TestUnaryFunction(param.params, &Atan<double>);
  } else if (function == "tanh") {
    return TestUnaryFunction(param.params, &Tanh<double>);
  } else if (function == "atanh") {
    return TestUnaryFunction(param.params, &Atanh<double>);
  } else if (function == "atan2") {
    return TestBinaryFunction(param.params, &Atan2<double>);
  } else if (function == "csc") {
    return TestUnaryFunction(param.params, &Csc<double>);
  } else if (function == "sec") {
    return TestUnaryFunction(param.params, &Sec<double>);
  } else if (function == "cot") {
    return TestUnaryFunction(param.params, &Cot<double>);
  } else if (function == "csch") {
    return TestUnaryFunction(param.params, &Csch<double>);
  } else if (function == "sech") {
    return TestUnaryFunction(param.params, &Sech<double>);
  } else if (function == "coth") {
    return TestUnaryFunction(param.params, &Coth<double>);
  } else if (function == "round") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Round<float>);
        } else if (param.params.num_params() == 2) {
          return TestBinaryFunction(param.params, &RoundDecimal<float>);
        } else {
          FAIL() << "Round FLOAT cannot be called with 3 arguments "
                 << function;
        }
      case TYPE_DOUBLE:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Round<double>);
        } else if (param.params.num_params() == 2) {
          return TestBinaryFunction(param.params, &RoundDecimal<double>);
        } else {
          FAIL() << "Round DOUBLE cannot be called with 3 arguments "
                 << function;
        }
      case TYPE_NUMERIC:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Round<NumericValue>);
        } else if (param.params.num_params() == 2) {
          return TestBinaryFunction(param.params, &RoundDecimal<NumericValue>);
        } else {
          return TestTernaryRoundFunction(
              param.params, &RoundDecimalWithRoundingMode<NumericValue>);
        }
      case TYPE_BIGNUMERIC:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Round<BigNumericValue>);
        } else if (param.params.num_params() == 2) {
          return TestBinaryFunction(param.params,
                                    &RoundDecimal<BigNumericValue>);
        } else {
          return TestTernaryRoundFunction(
              param.params, &RoundDecimalWithRoundingMode<BigNumericValue>);
        }
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "trunc") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Trunc<float>);
        } else {
          return TestBinaryFunction(param.params, &TruncDecimal<float>);
        }
      case TYPE_DOUBLE:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Trunc<double>);
        } else {
          return TestBinaryFunction(param.params, &TruncDecimal<double>);
        }
      case TYPE_NUMERIC:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Trunc<NumericValue>);
        } else {
          return TestBinaryFunction(param.params, &TruncDecimal<NumericValue>);
        }
      case TYPE_BIGNUMERIC:
        if (param.params.num_params() == 1) {
          return TestUnaryFunction(param.params, &Trunc<BigNumericValue>);
        } else {
          return TestBinaryFunction(param.params,
                                    &TruncDecimal<BigNumericValue>);
        }
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "ceil" || function == "ceiling") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        return TestUnaryFunction(param.params, &Ceil<float>);
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Ceil<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Ceil<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Ceil<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else if (function == "floor") {
    switch (param.params.param(0).type_kind()) {
      case TYPE_FLOAT:
        return TestUnaryFunction(param.params, &Floor<float>);
      case TYPE_DOUBLE:
        return TestUnaryFunction(param.params, &Floor<double>);
      case TYPE_NUMERIC:
        return TestUnaryFunction(param.params, &Floor<NumericValue>);
      case TYPE_BIGNUMERIC:
        return TestUnaryFunction(param.params, &Floor<BigNumericValue>);
      default:
        FAIL() << "unrecognized type for " << function;
    }
  } else {
    FAIL() << "Unrecognized function: " << function;
  }
}

INSTANTIATE_TEST_SUITE_P(Math, MathTemplateTest,
                         testing::ValuesIn(GetFunctionTestsMath()));
INSTANTIATE_TEST_SUITE_P(Trigonometry, MathTemplateTest,
                         testing::ValuesIn(GetFunctionTestsTrigonometric()));
INSTANTIATE_TEST_SUITE_P(
    InverseTrig, MathTemplateTest,
    testing::ValuesIn(GetFunctionTestsInverseTrigonometric()));
INSTANTIATE_TEST_SUITE_P(Cbrt, MathTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCbrt()));
INSTANTIATE_TEST_SUITE_P(DegreesRadiansPi, MathTemplateTest,
                         testing::ValuesIn(GetFunctionTestsDegreesRadiansPi()));
INSTANTIATE_TEST_SUITE_P(Pi, MathTemplateTest,
                         testing::ValuesIn(GetFunctionTestsPi()));
INSTANTIATE_TEST_SUITE_P(Rounding, MathTemplateTest,
                         testing::ValuesIn(GetFunctionTestsRounding()));

namespace {

TEST(TruncAndRoundTest, PrecisionIssues) {
  auto test_same_in_as_out = [](double number, int digits) {
    absl::Status status;
    double out;
    ASSERT_TRUE(TruncDecimal(number, digits, &out, &status)) << status;
    EXPECT_EQ(number, out) << "Digits=" << digits;
    ASSERT_TRUE(RoundDecimal(number, digits, &out, &status)) << status;
    EXPECT_EQ(number, out) << "Digits=" << digits;

    float fnumber = number;
    float fout;
    ASSERT_TRUE(TruncDecimal(fnumber, digits, &fout, &status)) << status;
    EXPECT_EQ(fnumber, fout) << "Digits=" << digits;
    ASSERT_TRUE(RoundDecimal(fnumber, digits, &fout, &status)) << status;
    EXPECT_EQ(fnumber, fout) << "Digits=" << digits;
  };

  // Make sure that whole numbers don't change values through trunc / round.
  for (int digits = 0; digits < 50; ++digits) {
    for (double number = -10000; number <= 10000; ++number) {
      test_same_in_as_out(number, digits);
    }
  }

  // Check some fractional powers of 2.
  for (int digits = 4; digits <= 10; ++digits) {
    for (double number : { 0.5, 0.25, 0.125, 0.0625 }) {
      test_same_in_as_out(number, digits);
    }
  }
}

}  // namespace

}  // namespace functions
}  // namespace googlesql
