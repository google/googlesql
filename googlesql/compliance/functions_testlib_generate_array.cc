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

#include <cstdint>
#include <limits>
#include <vector>

#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsGenerateArray() {
  const Value empty_int64_array = Int64Array({});
  // Numeric values
  const Value numeric_zero = Value::Numeric(NumericValue());
  const Value numeric_one =
      Value::Numeric(NumericValue(static_cast<int64_t>(1)));
  const Value numeric_two =
      Value::Numeric(NumericValue(static_cast<int64_t>(2)));
  const Value numeric_three =
      Value::Numeric(NumericValue(static_cast<int64_t>(3)));
  const Value numeric_pi =
      Value::Numeric(NumericValue::FromStringStrict("3.141592654").value());
  const Value numeric_pos_min =
      Value::Numeric(NumericValue::FromStringStrict("0.000000001").value());
  const Value numeric_negative_golden_ratio =
      Value::Numeric(NumericValue::FromStringStrict("-1.618033988").value());
  const Value numeric_eleven =
      Value::Numeric(NumericValue(static_cast<int64_t>(11)));
  const Value numeric_max = Value::Numeric(NumericValue::MaxValue());
  const Value numeric_min = Value::Numeric(NumericValue::MinValue());

  // BigNumeric values
  const Value bignumeric_zero = Value::BigNumeric(BigNumericValue());
  const Value bignumeric_one = Value::BigNumeric(BigNumericValue(1));
  const Value bignumeric_two = Value::BigNumeric(BigNumericValue(2));
  const Value bignumeric_three = Value::BigNumeric(BigNumericValue(3));
  const Value bignumeric_pi =
      Value::BigNumeric(BigNumericValue::FromStringStrict(
                            "3.14159265358979323846264338327950288419")
                            .value());
  const Value bignumeric_pos_min =
      Value::BigNumeric(BigNumericValue::FromStringStrict("1e-38").value());
  const Value bignumeric_negative_golden_ratio =
      Value::BigNumeric(BigNumericValue::FromStringStrict(
                            "-1.61803398874989484820458683436563811772")
                            .value());
  const Value bignumeric_eleven = Value::BigNumeric(BigNumericValue(11));
  const Value bignumeric_almost_thirteen =
      Value::BigNumeric(BigNumericValue(13)
                            .Subtract(bignumeric_pos_min.bignumeric_value())
                            .value());
  const Value bignumeric_max = Value::BigNumeric(BigNumericValue::MaxValue());
  const Value bignumeric_min = Value::BigNumeric(BigNumericValue::MinValue());

  std::vector<FunctionTestCall> all_tests = {
      // Null inputs.
      {"generate_array", {NullInt64(), NullInt64()}, Null(Int64ArrayType())},
      {"generate_array",
       {NullInt64(), NullInt64(), NullInt64()},
       Null(Int64ArrayType())},
      {"generate_array", {Int64(1), NullInt64()}, Null(Int64ArrayType())},
      {"generate_array", {NullInt64(), Int64(1)}, Null(Int64ArrayType())},
      {"generate_array",
       {Int64(1), Int64(2), NullInt64()},
       Null(Int64ArrayType())},
      {"generate_array", {NullUint64(), NullUint64()}, Null(Uint64ArrayType())},
      {"generate_array", {NullDouble(), NullDouble()}, Null(DoubleArrayType())},
      {"generate_array",
       {NullDouble(), Double(2), Double(double_pos_inf)},
       Null(DoubleArrayType())},
      {"generate_array",
       {Double(0), NullDouble(), Double(double_neg_inf)},
       Null(DoubleArrayType())},
      {"generate_array",
       {NullDouble(), NullDouble(), Double(double_nan)},
       Null(DoubleArrayType())},
      {"generate_array",
       QueryParamsWithResult({numeric_zero, NullNumeric(), numeric_three},
                             Null(NumericArrayType()))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_zero, NullBigNumeric(), bignumeric_three},
           Null(BigNumericArrayType()))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      // Empty generate_array.
      {"generate_array", {Int64(1), Int64(0)}, empty_int64_array},
      {"generate_array", {Int64(1), Int64(5), Int64(-1)}, empty_int64_array},
      {"generate_array", {Int64(5), Int64(1)}, empty_int64_array},
      {"generate_array", {Int64(1), Int64(0)}, empty_int64_array},
      {"generate_array", {Int64(5), Int64(0), Int64(2)}, empty_int64_array},
      {"generate_array",
       QueryParamsWithResult({numeric_three, numeric_zero, numeric_three},
                             NumericArray({}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_three, bignumeric_zero, bignumeric_three},
           BigNumericArray({}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      // Non-empty generate_array.
      {"generate_array", {Int64(2), Int64(2)}, Int64Array({2})},
      {"generate_array", {Uint64(2), Uint64(2)}, Uint64Array({2})},
      {"generate_array", {Double(2), Double(2)}, DoubleArray({2})},
      {"generate_array",
       {Int64(1), Int64(10)},
       Int64Array({1, 2, 3, 4, 5, 6, 7, 8, 9, 10})},
      {"generate_array",
       {Uint64(1), Uint64(10)},
       Uint64Array({1, 2, 3, 4, 5, 6, 7, 8, 9, 10})},
      {"generate_array",
       {Double(1), Double(10)},
       DoubleArray({1, 2, 3, 4, 5, 6, 7, 8, 9, 10})},
      {"generate_array", {Int64(-5), Int64(-5)}, Int64Array({-5})},
      {"generate_array",
       {Int64(0), Int64(-10), Int64(-3)},
       Int64Array({0, -3, -6, -9})},
      {"generate_array",
       {Double(0), Double(-10), Double(-3)},
       DoubleArray({0, -3, -6, -9}),
       FloatMargin::UlpMargin(FloatMargin::kDefaultUlpBits)},
      {"generate_array",
       {Uint64(10), Uint64(20), Uint64(3)},
       Uint64Array({10, 13, 16, 19})},
      {"generate_array",
       {Double(10), Double(20), Double(3.33)},
       DoubleArray({10, 13.33, 16.66, 19.99}),
       FloatMargin::UlpMargin(FloatMargin::kDefaultUlpBits)},
      {"generate_array",
       {Double(0.05), Double(0.1), Double(0.01)},
       DoubleArray({0.05, 0.06, 0.07, 0.08, 0.09, 0.1}),
       FloatMargin::UlpMargin(FloatMargin::kDefaultUlpBits)},
      {"generate_array",
       {Double(5), Double(10), Double(1)},
       DoubleArray({5, 6, 7, 8, 9, 10}),
       FloatMargin::UlpMargin(FloatMargin::kDefaultUlpBits)},
      {"generate_array", {Int64(1), Int64(3)}, Int64Array({1, 2, 3})},
      {"generate_array", {Int64(4), Int64(8), Int64(2)}, Int64Array({4, 6, 8})},
      {"generate_array", {Int64(4), Int64(9), Int64(2)}, Int64Array({4, 6, 8})},
      {"generate_array",
       {Int64(8), Int64(4), Int64(-2)},
       Int64Array({8, 6, 4})},
      {"generate_array",
       {Int64(10), Int64(19), Int64(5)},
       Int64Array({10, 15})},
      {"generate_array",
       {Int64(-15), Int64(-10), Int64(5)},
       Int64Array({-15, -10})},
      {"generate_array",
       {Int64(-15), Int64(-10), Int64(5)},
       Int64Array({-15, -10})},
      {"generate_array",
       {Int64(int64max), Int64(int64max - 2), Int64(-1)},
       Int64Array({int64max, int64max - 1, int64max - 2})},
      {"generate_array",
       QueryParamsWithResult(
           {numeric_zero, numeric_three, numeric_one},
           Array({numeric_zero, numeric_one, numeric_two, numeric_three}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {numeric_one, numeric_eleven, numeric_three},
           NumericArray({NumericValue(static_cast<int64_t>(1)),
                         NumericValue(static_cast<int64_t>(4)),
                         NumericValue(static_cast<int64_t>(7)),
                         NumericValue(static_cast<int64_t>(10))}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {numeric_eleven, numeric_one,
            Value::Numeric(NumericValue(static_cast<int64_t>(-4)))},
           NumericArray({NumericValue(static_cast<int64_t>(11)),
                         NumericValue(static_cast<int64_t>(7)),
                         NumericValue(static_cast<int64_t>(3))}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {numeric_eleven, numeric_pi, numeric_negative_golden_ratio},
           NumericArray(
               {NumericValue(static_cast<int64_t>(11)),
                NumericValue::FromStringStrict("9.381966012").value(),
                NumericValue::FromStringStrict("7.763932024").value(),
                NumericValue::FromStringStrict("6.145898036").value(),
                NumericValue::FromStringStrict("4.527864048").value()}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {numeric_negative_golden_ratio, numeric_three, numeric_one},
           NumericArray(
               {numeric_negative_golden_ratio.numeric_value(),
                NumericValue::FromStringStrict("-0.618033988").value(),
                NumericValue::FromStringStrict("0.381966012").value(),
                NumericValue::FromStringStrict("1.381966012").value(),
                NumericValue::FromStringStrict("2.381966012").value()}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult({numeric_zero, numeric_pos_min, numeric_pos_min},
                             Array({numeric_zero, numeric_pos_min}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult({numeric_max, numeric_max, numeric_one},
                             Array({numeric_max}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult({numeric_max, numeric_max, numeric_pos_min},
                             Array({numeric_max}))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array", QueryParamsWithResult({numeric_min, numeric_min,
                                                numeric_negative_golden_ratio},
                                               Array({numeric_min}))
                             .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_zero, bignumeric_three, bignumeric_one},
           Array({bignumeric_zero, bignumeric_one, bignumeric_two,
                  bignumeric_three}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_one, bignumeric_almost_thirteen, bignumeric_three},
           BigNumericArray({BigNumericValue(1), BigNumericValue(4),
                            BigNumericValue(7), BigNumericValue(10)}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_eleven, bignumeric_one,
            Value::BigNumeric(BigNumericValue(-4))},
           BigNumericArray(
               {BigNumericValue(11), BigNumericValue(7), BigNumericValue(3)}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_eleven, bignumeric_pi, bignumeric_negative_golden_ratio},
           BigNumericArray({BigNumericValue(11),
                            BigNumericValue::FromStringStrict(
                                "9.38196601125010515179541316563436188228")
                                .value(),
                            BigNumericValue::FromStringStrict(
                                "7.76393202250021030359082633126872376456")
                                .value(),
                            BigNumericValue::FromStringStrict(
                                "6.14589803375031545538623949690308564684")
                                .value(),
                            BigNumericValue::FromStringStrict(
                                "4.52786404500042060718165266253744752912")
                                .value()}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_negative_golden_ratio, bignumeric_three, bignumeric_one},
           BigNumericArray({bignumeric_negative_golden_ratio.bignumeric_value(),
                            BigNumericValue::FromStringStrict(
                                "-0.61803398874989484820458683436563811772")
                                .value(),
                            BigNumericValue::FromStringStrict(
                                "0.38196601125010515179541316563436188228")
                                .value(),
                            BigNumericValue::FromStringStrict(
                                "1.38196601125010515179541316563436188228")
                                .value(),
                            BigNumericValue::FromStringStrict(
                                "2.38196601125010515179541316563436188228")
                                .value()}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_zero, bignumeric_pos_min, bignumeric_pos_min},
           Array({bignumeric_zero, bignumeric_pos_min}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult({bignumeric_max, bignumeric_max, bignumeric_one},
                             Array({bignumeric_max}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array", QueryParamsWithResult({bignumeric_max, bignumeric_max,
                                                bignumeric_pos_min},
                                               Array({bignumeric_max}))
                             .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_min, bignumeric_min, bignumeric_negative_golden_ratio},
           Array({bignumeric_min}))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      // Guarding against overflows.
      {"generate_array",
       {Int64(int64max - 2), Int64(int64max), Int64(5)},
       Int64Array({int64max - 2})},
      {"generate_array",
       {Int64(int64min + 2), Int64(int64min), Int64(-5)},
       Int64Array({int64min + 2})},
      {"generate_array",
       {Uint64(uint64max - 2), Uint64(uint64max), Uint64(1)},
       Uint64Array({uint64max - 2, uint64max - 1, uint64max})},
      {"generate_array",
       {Double(doublemax), Double(doublemax), Double(doublemax)},
       DoubleArray({doublemax})},
      {"generate_array",
       {Double(0), Double(doubleminpositive), Double(doubleminpositive)},
       DoubleArray({0, doubleminpositive})},
      {"generate_array",
       {Double(0), Double(doubleminpositive * 3), Double(doubleminpositive)},
       DoubleArray({0, doubleminpositive, doubleminpositive * 2,
                    doubleminpositive * 3}),
       FloatMargin::UlpMargin(1)},
      {"generate_array",
       {Int64(int64min), Int64(int64min + 5), Int64(2)},
       Int64Array({int64min, int64min + 2, int64min + 4})},
      {"generate_array",
       {Int64(int64min), Int64(int64max), Int64(int64max / 2)},
       Int64Array({int64min, -4611686018427387905, -2, 4611686018427387901,
                   9223372036854775804})},
      // Zero step size.
      {"generate_array",
       {Int64(1), Int64(2), Int64(0)},
       Null(Int64ArrayType()),
       OUT_OF_RANGE},
      {"generate_array",
       {Uint64(1), Uint64(2), Uint64(0)},
       Null(Uint64ArrayType()),
       OUT_OF_RANGE},
      {"generate_array",
       {Double(1), Double(2), Double(0.0)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},
      {"generate_array",
       {Double(1), Double(2), Double(-0.0)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},
      {"generate_array",
       QueryParamsWithResult({numeric_one, numeric_two, numeric_zero},
                             Null(NumericArrayType()), OUT_OF_RANGE)
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"generate_array",
       {Double(double_pos_inf), Double(double_pos_inf), Double(1)},
       DoubleArray({double_pos_inf})},  // Adding to +inf.
      {"generate_array",
       {Double(double_neg_inf), Double(double_neg_inf), Double(1)},
       DoubleArray({double_neg_inf})},  // Adding to -inf.
      {"generate_array",
       {Double(0), Double(10), Double(double_pos_inf)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // +inf as a step.
      {"generate_array",
       {Double(10), Double(0), Double(double_neg_inf)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // -inf as a step.
      {"generate_array",
       {Double(0), Double(10),
        Double(std::numeric_limits<double>::denorm_min())},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // Adding the minimum positive subnormal value.
      {"generate_array",
       {Double(0), Double(10), Double(std::numeric_limits<double>::epsilon())},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // Adding epsilon.
      {"generate_array",
       {Double(double_nan), Double(1), Double(1)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // Adding to nan.
      {"generate_array",
       {Double(1), Double(double_nan), Double(1)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // Adding to nan.
      {"generate_array",
       {Double(1), Double(2), Double(double_nan)},
       Null(DoubleArrayType()),
       OUT_OF_RANGE},  // nan as a step.
      {"generate_array",
       QueryParamsWithResult({numeric_zero, numeric_one, numeric_pos_min},
                             Null(NumericArrayType()), OUT_OF_RANGE)
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},  // Large NUMERIC range.
      {"generate_array",
       QueryParamsWithResult({numeric_min, numeric_max, numeric_one},
                             Null(NumericArrayType()), OUT_OF_RANGE)
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},  // Large NUMERIC range.
      {"generate_array",
       QueryParamsWithResult(
           {bignumeric_zero, bignumeric_one, bignumeric_pos_min},
           Null(BigNumericArrayType()), OUT_OF_RANGE)
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},  // BIGNUMERIC range.
      {"generate_array",
       QueryParamsWithResult({bignumeric_min, bignumeric_max, bignumeric_one},
                             Null(BigNumericArrayType()), OUT_OF_RANGE)
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},  // BIGNUMERIC range.
  };

  return all_tests;
}

}  // namespace googlesql
