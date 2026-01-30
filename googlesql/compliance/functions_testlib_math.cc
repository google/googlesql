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

#include <math.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<QueryParamsWithResult> GetFunctionTestsUnaryMinus() {
  return {
      // float
      {{Float(-floatmax)}, Float(floatmax)},
      {{Float(-1)}, Float(1)},
      {{Float(-floatmin)}, Float(floatmin)},
      {{Float(0)}, Float(0)},
      {{Float(floatmin)}, Float(-floatmin)},
      {{Float(1)}, Float(-1)},
      {{Float(floatmax)}, Float(-floatmax)},
      {{Float(float_neg_inf)}, Float(float_pos_inf)},
      {{Float(float_pos_inf)}, Float(float_neg_inf)},
      {{Float(float_nan)}, Float(float_nan)},
      {{NullFloat()}, NullFloat()},

      // double
      {{Double(-doublemax)}, Double(doublemax)},
      {{Double(-1)}, Double(1)},
      {{Double(-doublemin)}, Double(doublemin)},
      {{Double(0)}, Double(0)},
      {{Double(doublemin)}, Double(-doublemin)},
      {{Double(1)}, Double(-1)},
      {{Double(doublemax)}, Double(-doublemax)},
      {{Double(double_neg_inf)}, Double(double_pos_inf)},
      {{Double(double_pos_inf)}, Double(double_neg_inf)},
      {{Double(double_nan)}, Double(double_nan)},
      {{NullDouble()}, NullDouble()},

      // int32
      {{Int32(-1)}, Int32(1)},
      {{Int32(int32min)}, NullInt32(), OUT_OF_RANGE},
      {{Int32(0)}, Int32(0)},
      {{Int32(1)}, Int32(-1)},
      {{Int32(int32max)}, Int32(-int32max)},
      {{NullInt32()}, NullInt32()},

      // int64
      {{Int64(-1)}, Int64(1)},
      {{Int64(int64min)}, NullInt64(), OUT_OF_RANGE},
      {{Int64(0)}, Int64(0)},
      {{Int64(1)}, Int64(-1)},
      {{Int64(int64max)}, Int64(-int64max)},
      {{NullInt64()}, NullInt64()},

      // numeric
      QueryParamsWithResult({{Numeric(-1)}, Numeric(1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::FromDouble(3.14).value())},
                             Numeric(NumericValue::FromDouble(-3.14).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue())},
                             Numeric(NumericValue::MinValue())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(0)}, Numeric(0)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(1)}, Numeric(-1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue())},
                             Numeric(NumericValue::MaxValue())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult({{BigNumeric(-1)}, BigNumeric(1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("3.14").value())},
           BigNumeric(BigNumericValue::FromStringStrict("-3.14").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(0)}, BigNumeric(0)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(1)}, BigNumeric(-1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue())},
           BigNumeric(BigNumericValue::FromStringStrict(
                          "-578960446186580977117854925043439539266."
                          "34992332820282019728792003956564819967")
                          .value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MinValue())},
                             NullBigNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCoercedAdd() {
  return {
      // int32
      {{2, 3}, 5ll},
      {{int32min / 2, int32min / 2}, Int64(int32min)},
      {{1, int32max}, int32max + 1ll},
      {{int32max, 1}, int32max + 1ll},
      {{int32max, int32max}, int32max * 2ll},
      {{-1, int32min}, int32min - 1ll},
      {{int32min, -1}, int32min - 1ll},
      {{int32max, int32min}, Int64(-1)},

      // uint32
      {{2u, 3u}, 5ull},
      {{1u, uint32max}, uint32max + 1ull},
      {{uint32max, 1u}, uint32max + 1ull},
      {{uint32max, uint32max}, uint32max * 2ull},
      {{uint32max, 0u}, Uint64(uint32max)},

      // int32 and uint32
      {{1, 2u}, 3ll},
      {{2, 1u}, 3ll},

      // float
      {{1.0f, 2.0f}, 3.0},
      {{floatmax, floatmax}, floatmax * 2.0},
      {{floatmax, 0.0f}, Double(floatmax)},
      {{0.0f, float_pos_inf}, double_pos_inf},
      {{float_neg_inf, float_pos_inf}, double_nan},
      {{0.0f, float_nan}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullInt64()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Int64(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Int64(5)}, Numeric(2)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(1.25).value()), Int64(3)},
           Numeric(NumericValue::FromDouble(4.25).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MaxValue()), Int64(-3)},
           Numeric(NumericValue::FromStringStrict(
                       "99999999999999999999999999996.999999999")
                       .value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MaxValue()), Double(-1.01)}, Double(1e+29)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()), Int64(-3)},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Double(5)}, Double(2)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Double(5), Numeric(-3)}, Double(2)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MinValue()), Double(0.00001)},
           Double(-1e+29)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult({{BigNumeric(-3), NullInt64()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{NullBigNumeric(), Int64(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), Int64(5)}, BigNumeric(2)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("1.25").value()),
            Int64(3)},
           BigNumeric(BigNumericValue::FromStringStrict("4.25").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("2.02").value()),
            Double(-1.01)},
           Double(1.01)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{Double(5), BigNumeric(-3)}, Double(2)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Int64(-3)},
           BigNumeric(BigNumericValue::FromStringStrict(
                          "578960446186580977117854925043439539263."
                          "34992332820282019728792003956564819967")
                          .value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Double(3.1)},
           Double(5.7896044618658096e+38)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MinValue()), Int64(-3)},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsAdd() {
  return {
      {{1ll, NullInt64()}, NullInt64()},
      {{NullInt64(), 2ll}, NullInt64()},
      {{1ull, NullUint64()}, NullUint64()},
      {{NullUint64(), 2ull}, NullUint64()},
      {{1.0, NullDouble()}, NullDouble()},
      {{NullDouble(), 2.0}, NullDouble()},

      // int64
      {{2ll, 3ll}, 5ll},
      {{int64max / 2, int64max / 2}, (int64max / 2) * 2},
      {{int64min / 2, int64min / 2}, int64min},
      {{int64max / 2 + 1, int64max / 2 + 1}, NullInt64(), OUT_OF_RANGE},
      {{1ll, int64max}, NullInt64(), OUT_OF_RANGE},
      {{int64max, 1ll}, NullInt64(), OUT_OF_RANGE},
      {{int64max, int64max}, NullInt64(), OUT_OF_RANGE},
      {{-1ll, int64min}, NullInt64(), OUT_OF_RANGE},
      {{int64min, -1ll}, NullInt64(), OUT_OF_RANGE},
      {{int64max, int64min}, -1ll},

      // uint64
      {{2ull, 3ull}, 5ull},
      {{uint64max / 2u, uint64max / 2u}, (uint64max / 2u) * 2},
      {{uint64max / 2u + 1u, uint64max / 2u + 1u}, NullUint64(), OUT_OF_RANGE},
      {{1ull, uint64max}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, 1ull}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, uint64max}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, 0ull}, uint64max},

      // double
      {{1.0, 2.0}, 3.0},
      {{doublemax, doublemax}, NullDouble(), OUT_OF_RANGE},
      {{doublemax, 0.0}, doublemax},
      {{0.0, double_pos_inf}, double_pos_inf},
      {{double_neg_inf, double_pos_inf}, double_nan},
      {{0.0, double_nan}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullNumeric()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Numeric(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-1), Numeric(100)}, Numeric(99)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MinValue())},
                             Numeric(0)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MaxValue())},
                             Numeric(0)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MinValue())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MaxValue())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult(
          {{BigNumeric(-3), NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{NullBigNumeric(), BigNumeric(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-1), BigNumeric(100)}, BigNumeric(99)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()),
            BigNumeric(BigNumericValue::MinValue())},
           BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MinValue()),
            BigNumeric(BigNumericValue::MaxValue())},
           BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MinValue()),
            BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()),
            BigNumeric(BigNumericValue::FromStringStrict("1e-38").value())},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCoercedSubtract() {
  // These initial tests only cover subtraction between two same-type
  // operands.  TODO: Add tests for subtraction between
  // different types.
  return {
      // int32
      {{5, 2}, 3ll},
      {{int32max / 2, -(int32max / 2)}, Int64((int32max / 2) * 2)},
      {{int32min / 2, -(int32min / 2)}, Int64(int32min)},

      {{int32max, 1}, Int64(int32max - 1)},
      {{int32max, -1}, Int64(static_cast<int64_t>(int32max) + 1)},
      {{int32min, -1}, Int64(int32min + 1)},
      {{int32min, 1}, Int64(static_cast<int64_t>(int32min) - 1)},
      {{int32min, int32min}, Int64(int32min - int32min)},

      {{int32min, -int32max}, -1ll},
      {{int32max, int32max}, 0ll},
      {{int32min, int32min}, 0ll},
      {{int32max, int32min}, Int64(static_cast<int64_t>(int32max) - int32min)},

      // uint32
      {{5u, 3u}, 2ll},
      {{uint32max, uint32max}, 0ll},
      {{0u, 1u}, -1ll},
      {{uint32max, 0u}, Int64(uint32max)},
      {{0u, uint32max}, Int64(-static_cast<int64_t>(uint32max))},
      {{uint32max - 1u, uint32max}, -1ll},
      {{uint32max, uint32max - 1u}, 1ll},

      // float
      {{3.0f, 2.0f}, 1.0},
      {{floatmax, floatmax}, 0.0},
      {{floatmax, 0.0f}, Double(floatmax)},
      {{0.0f, float_pos_inf}, double_neg_inf},
      {{float_pos_inf, float_pos_inf}, double_nan},
      {{0.0f, float_nan}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullInt64()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Int64(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Int64(5)}, Numeric(-8)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(1.25).value()), Int64(3)},
           Numeric(NumericValue::FromDouble(-1.75).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MaxValue()), Int64(3)},
           Numeric(NumericValue::FromStringStrict(
                       "99999999999999999999999999996.999999999")
                       .value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MaxValue()), Double(1.01)}, Double(1e+29)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()), Int64(3)},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Double(5)}, Double(-8)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Double(5), Numeric(-3)}, Double(8)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MinValue()), Double(0.00001)},
           Double(-1e+29)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult({{BigNumeric(-3), NullInt64()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{NullBigNumeric(), Int64(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), Int64(5)}, BigNumeric(-8)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("1.25").value()),
            Int64(3)},
           BigNumeric(BigNumericValue::FromStringStrict("-1.75").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("2.02").value()),
            Double(-1)},
           Double(3.02)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{Double(5), BigNumeric(-3)}, Double(8)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Int64(-3)},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Double(3.1)},
           Double(5.7896044618658096e+38)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MinValue()), Int64(-3)},
           BigNumeric(BigNumericValue::FromStringStrict(
                          "-578960446186580977117854925043439539263."
                          "34992332820282019728792003956564819968")
                          .value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsSubtract() {
  return {
      {{1ll, NullInt64()}, NullInt64()},
      {{NullInt64(), 2ll}, NullInt64()},
      {{1ull, NullUint64()}, NullInt64()},
      {{NullUint64(), 2ull}, NullInt64()},
      {{1.0, NullDouble()}, NullDouble()},
      {{NullDouble(), 2.0}, NullDouble()},

      // int64
      {{5ll, 2ll}, 3ll},
      {{int64max / 2, -(int64max / 2)}, (int64max / 2) * 2},
      {{int64min / 2, -(int64min / 2)}, int64min / 2 * 2},

      {{int64max, 1ll}, int64max - 1},
      {{int64max, -1ll}, NullInt64(), OUT_OF_RANGE},
      {{int64min, -1ll}, int64min + 1},
      {{int64min, 1ll}, NullInt64(), OUT_OF_RANGE},

      {{int64min, -int64max}, -1ll},
      {{int64max, int64max}, 0ll},
      {{int64min, int64min}, 0ll},

      // uint64
      {{5ull, 3ull}, 2ll},
      {{uint64max, uint64max}, 0ll},
      {{uint64max, 0ull}, NullInt64(), OUT_OF_RANGE},
      {{uint64max, uint64max - 1u}, 1ll},
      {{static_cast<uint64_t>(int64max), 0ull}, int64max},
      {{static_cast<uint64_t>(int64max) + 1ull, 0ull},
       NullInt64(),
       OUT_OF_RANGE},
      {{0ull, 1ull}, -1ll},
      {{0ull, uint64max}, NullInt64(), OUT_OF_RANGE},
      {{0ull, static_cast<uint64_t>(int64max) + 1ull}, int64min},
      {{0ull, static_cast<uint64_t>(int64max) + 2ull},
       NullInt64(),
       OUT_OF_RANGE},
      {{uint64max - 1u, uint64max}, -1ll},

      // double
      {{3.0, 2.0}, 1.0},
      {{doublemax, doublemax}, 0.0},
      {{doublemax, -doublemax}, NullDouble(), OUT_OF_RANGE},
      {{doublemax, 0.0}, doublemax},
      {{0.0, double_pos_inf}, double_neg_inf},
      {{double_pos_inf, double_pos_inf}, double_nan},
      {{0.0, double_nan}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullNumeric()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Numeric(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-1), Numeric(100)}, Numeric(-101)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MaxValue())},
                             Numeric(0)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MinValue())},
                             Numeric(0)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MinValue())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MaxValue())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult(
          {{BigNumeric(-3), NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{NullBigNumeric(), BigNumeric(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(-1), BigNumeric(100)}, BigNumeric(-101)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue::MaxValue())},
                             BigNumeric(0)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MinValue()),
                              BigNumeric(BigNumericValue::MinValue())},
                             BigNumeric(0)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()),
            BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MinValue()),
            BigNumeric(BigNumericValue::FromStringStrict("1e-38").value())},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCoercedMultiply() {
  // These initial tests only cover multiplication between two same-type
  // operands.  TODO: Add tests for multiplication between
  // different types.
  return {
      // int32
      {{2, 2}, 4ll},
      {{3, -2}, -6ll},
      {{int32min / 2, 2}, Int64(int32min / 2 * 2)},
      {{int32max / 2, 2}, Int64(int32max / 2 * 2)},
      {{int32min, 0}, 0ll},
      {{int32max, 0}, 0ll},
      {{int32max, -1}, Int64(-int32max)},
      {{1 << 15, 1 << 15}, 1ll << 30},
      {{1 << 15, 1 << 16}, 1ll << 31},
      {{int32min, -1}, Int64(static_cast<int64_t>(int32min) * -1)},
      {{int32max, 2}, Int64(static_cast<int64_t>(int32max) * 2)},
      {{int32min, 2}, Int64(static_cast<int64_t>(int32min) * 2)},
      {{int32max, int32max}, Int64(static_cast<int64_t>(int32max) * int32max)},
      {{int32min, int32min}, Int64(static_cast<int64_t>(int32min) * int32min)},
      {{int32min, int32max}, Int64(static_cast<int64_t>(int32min) * int32max)},
      {{int32min / 2, -2}, Int64(int32max + int64_t{1})},
      {{int32min / 2, 2}, Int64(int32min)},

      // uint32
      {{2u, 2u}, 4ull},
      {{uint32max / 2u, 2u}, Uint64(uint32max / 2u * 2u)},
      {{uint32max, 0u}, 0ull},
      {{uint32max, 1u}, Uint64(uint32max)},
      {{1u << 16, 1u << 15}, Uint64(1u << 31)},
      {{1u << 16, 1u << 16},
       Uint64(static_cast<uint64_t>(1u << 16) * (1u << 16))},
      {{uint32max, 2u}, Uint64(uint32max * uint64_t{2})},
      {{uint32max, uint32max},
       Uint64(static_cast<uint64_t>(uint32max) * uint32max)},

      // float
      {{3.0f, 2.0f}, 6.0},
      {{floatmax, 0.0f}, 0.0},
      {{0.0f, float_pos_inf}, double_nan},
      {{0.0f, float_nan}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullInt64()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Int64(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Int64(5)}, Numeric(-15)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(1.25).value()), Int64(3)},
           Numeric(NumericValue::FromDouble(3.75).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()), Int64(3)},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()), Double(1.01)},
                             Double(1.0099999999999999e+29)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()), Int64(3)},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Double(5)}, Double(-15)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Double(5), Numeric(-3)}, Double(-15)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MaxValue()), Double(0.00001)}, Double(1e+24)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult({{BigNumeric(-3), NullInt64()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{NullBigNumeric(), Int64(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), Int64(5)}, BigNumeric(-15)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("1.25").value()),
            Int64(3)},
           BigNumeric(BigNumericValue::FromStringStrict("3.75").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("2").value()),
            Double(-1.01)},
           Double(-2.02)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{Double(5), BigNumeric(-3)}, Double(-15)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), Double(5)}, Double(-15)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Int64(-3)},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Double(2)},
           Double(1.1579208923731619e+39)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsMultiply() {
  return {
      // int64
      {{2ll, 2ll}, 4ll},
      {{3ll, -2ll}, -6ll},
      {{int64min / 2, 2ll}, int64min},
      {{int64max / 2, 2ll}, int64max / 2 * 2},
      {{int64min, 0ll}, 0ll},
      {{int64max, 0ll}, 0ll},
      {{int64max, -1ll}, -int64max},
      {{1ll << 31, 1ll << 31}, 1ll << 62},
      {{1ll << 32, 1ll << 31}, NullInt64(), OUT_OF_RANGE},
      {{int64min, -1ll}, NullInt64(), OUT_OF_RANGE},
      {{int64max, 2ll}, NullInt64(), OUT_OF_RANGE},
      {{int64min, 2ll}, NullInt64(), OUT_OF_RANGE},
      {{int64max, int64max}, NullInt64(), OUT_OF_RANGE},
      {{int64min, int64min}, NullInt64(), OUT_OF_RANGE},
      {{int64min, int64max}, NullInt64(), OUT_OF_RANGE},
      {{int64min, 2ll}, NullInt64(), OUT_OF_RANGE},
      {{int64min / 2, -2ll}, NullInt64(), OUT_OF_RANGE},
      {{int64min / 2, 2ll}, int64min},

      // uint64
      {{2ull, 2ull}, 4ull},
      {{uint64max / 2ull, 2ull}, uint64max / 2ull * 2ull},
      {{uint64max, 0ull}, 0ull},
      {{uint64max, 1ull}, uint64max},
      {{1ull << 32, 1ull << 31}, 1ull << 63},
      {{1ull << 32, 1ull << 32}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, 2ull}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, uint64max}, NullUint64(), OUT_OF_RANGE},

      // double
      {{3.0, 2.0}, 6.0},
      {{doublemax, doublemax}, NullDouble(), OUT_OF_RANGE},
      {{doublemax, 0.0}, 0.0},
      {{0.0, double_pos_inf}, double_nan},
      {{0.0, double_nan}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullNumeric()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Numeric(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Numeric(3)}, Numeric(-9)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(3.33).value()), Numeric(-3)},
           Numeric(NumericValue::FromDouble(-9.99).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(0.001).value()), Numeric(5)},
           Numeric(NumericValue::FromDouble(0.005).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MinValue())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MaxValue())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult(
          {{BigNumeric(-3), NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{NullBigNumeric(), BigNumeric(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), BigNumeric(3)}, BigNumeric(-9)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("3.33").value()),
            BigNumeric(-3)},
           BigNumeric(BigNumericValue::FromStringStrict("-9.99").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value()),
            BigNumeric(5)},
           BigNumeric(BigNumericValue::FromStringStrict("-5e-38").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("-1e-20").value()),
            BigNumeric(BigNumericValue::FromStringStrict("-1e-20").value())},
           BigNumeric(0)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue::MaxValue())},
                             NullBigNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MinValue()),
                              BigNumeric(BigNumericValue::MaxValue())},
                             NullBigNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCoercedModulo() {
  // These initial tests only cover MOD between two same-type
  // operands.  TODO: Add tests for MOD between
  // different types.
  return {
      // int32
      {{5, 3}, 2ll},
      {{5, 5}, 0ll},
      {{-5, 3}, -2ll},
      {{5, -3}, 2ll},
      {{-5, -3}, -2ll},
      {{int32max, 1}, 0ll},
      {{1, int32max}, 1ll},
      {{int32max - 1, int32max}, Int64(int32max - 1)},

      {{1, 0}, NullInt64(), OUT_OF_RANGE},
      {{0, 0}, NullInt64(), OUT_OF_RANGE},
      {{int32max, 0}, NullInt64(), OUT_OF_RANGE},
      {{int32min, 0}, NullInt64(), OUT_OF_RANGE},

      // uint32
      {{5u, 3u}, 2ull},
      {{5u, 5u}, 0ull},
      {{uint32max, 1u}, Uint64(uint64_t{0})},
      {{1u, uint32max}, Uint64(uint64_t{1})},
      {{uint32max - 1u, uint32max}, Uint64(uint32max - uint64_t{1})},

      {{1u, 0u}, NullUint64(), OUT_OF_RANGE},
      {{0u, 0u}, NullUint64(), OUT_OF_RANGE},
      {{uint32max, 0u}, NullUint64(), OUT_OF_RANGE},

      // numeric
      QueryParamsWithResult({{Numeric(5), Int64(3)}, Numeric(2)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Int64(5), Numeric(-3)}, Numeric(2)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(-3), Int64(0)}, NullNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsModulo() {
  return {
      // int64
      {{5ll, 3ll}, 2ll},
      {{5ll, 5ll}, 0ll},
      {{-5ll, 3ll}, -2ll},
      {{5ll, -3ll}, 2ll},
      {{-5ll, -3ll}, -2ll},
      {{int64max, 1ll}, 0ll},
      {{1ll, int64max}, 1ll},
      {{int64max - 1, int64max}, int64max - 1},
      {{int64min, -1ll}, 0ll},

      {{1ll, 0ll}, NullInt64(), OUT_OF_RANGE},
      {{0ll, 0ll}, NullInt64(), OUT_OF_RANGE},
      {{int64min, 0ll}, NullInt64(), OUT_OF_RANGE},
      {{int64max, 0ll}, NullInt64(), OUT_OF_RANGE},

      // uint64
      {{5ull, 3ull}, 2ull},
      {{5ull, 5ull}, 0ull},
      {{uint64max, 1ull}, 0ull},
      {{1ull, uint64max}, 1ull},
      {{uint64max - 1u, uint64max}, uint64max - 1ull},

      {{1ull, 0ull}, NullUint64(), OUT_OF_RANGE},
      {{0ull, 0ull}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, 0ull}, NullUint64(), OUT_OF_RANGE},

      // numeric
      QueryParamsWithResult({{Numeric(5), Numeric(2)}, Numeric(1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-5), Numeric(2)}, Numeric(-1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromString("3.33").value()), Numeric(-3)},
           Numeric(NumericValue::FromString("0.33").value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(5), Numeric(NumericValue::FromString("0.001").value())},
           Numeric(NumericValue())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MinValue())},
                             NumericValue()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MinValue())},
                             NumericValue()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::MaxValue()), Numeric(NumericValue())},
           NullNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      // bignumeric
      QueryParamsWithResult(
          {{BigNumeric(-3), NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{NullBigNumeric(), BigNumeric(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(-3), BigNumeric(3)}, BigNumeric(BigNumericValue())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(-3), BigNumeric(0)}, NullBigNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(5),
            BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())},
           BigNumeric(BigNumericValue())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      QueryParamsWithResult({{BigNumeric(5), BigNumeric(2)}, BigNumeric(1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-5), BigNumeric(2)}, BigNumeric(-1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("3.33").value()),
            BigNumeric(-3)},
           BigNumeric(BigNumericValue::FromString("0.33").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(5),
            BigNumeric(BigNumericValue::FromStringStrict("0.001").value())},
           BigNumeric(BigNumericValue())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MinValue()),
                              BigNumeric(BigNumericValue::MinValue())},
                             BigNumericValue()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue())},
                             NullBigNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCoercedDivide() {
  // These initial tests only cover division between two same-type
  // operands.  TODO: Add tests for division between
  // different types.
  return {
      // int32
      {{6, 2}, 3.0},

      // int64
      {{6ll, 2ll}, 3.0},

      // uint32
      {{6u, 2u}, 3.0},

      // uint64
      {{6ull, 2ull}, 3.0},

      // float
      {{4.0f, 2.0f}, 2.0},
      {{floatmax, 1.0f}, static_cast<double>(floatmax)},
      {{floatmin, 1.0f}, static_cast<double>(floatmin)},
      {{floatminpositive, 1.0f}, static_cast<double>(floatminpositive)},
      {{1.0f, 0.0f}, NullDouble(), OUT_OF_RANGE},
      {{1.0f, 0.0f}, NullDouble(), OUT_OF_RANGE},
      {{0.0f, 0.0f}, NullDouble(), OUT_OF_RANGE},

      {{1.0f, float_pos_inf}, 0.0},
      {{0.0f, float_pos_inf}, 0.0},
      {{float_pos_inf, 1.0f}, double_pos_inf},
      {{0.0f, float_nan}, double_nan},
      {{float_nan, 1.0f}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullInt64()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Int64(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Int64(5)},
                             Numeric(NumericValue::FromDouble(-0.6).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Int64(int64max),
            NumericValue::FromStringStrict("0.000000001").value()},
           Numeric(
               NumericValue::FromStringStrict("9223372036854775807000000000")
                   .value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(-3), Int64(0)}, NullNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Int64(5), Numeric(-3)},
           Numeric(NumericValue::FromStringStrict("-1.666666667").value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Double(5)}, Double(-0.6)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Double(5), Numeric(-3)}, Double(-1.6666666666666667)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult({{BigNumeric(-3), NullInt64()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{NullBigNumeric(), Int64(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(-3), Int64(5)},
           BigNumeric(BigNumericValue::FromStringStrict("-0.6").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("2.25").value()),
            Int64(3)},
           BigNumeric(BigNumericValue::FromStringStrict("0.75").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("2.02").value()),
            Double(-1.01)},
           Double(-2)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{Double(3), BigNumeric(-5)}, Double(-0.6)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), Double(5)}, Double(-0.6)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Int64(-2)},
           BigNumeric(BigNumericValue::FromStringStrict(
                          "-289480223093290488558927462521719769633."
                          "17496166410141009864396001978282409984")
                          .value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()), Double(2)},
           Double(2.8948022309329048e+38)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsDivide() {
  double doublemin_denorm = std::numeric_limits<double>::denorm_min();

  return {
      // double
      {{4.0, 2.0}, 2.0},
      {{doublemax, 1.0}, doublemax},
      {{doublemin, 1.0}, doublemin},
      {{doubleminpositive, 1.0}, doubleminpositive},

      // divide by (almost or exactly) 0
      {{1.0, 0.0}, NullDouble(), OUT_OF_RANGE},
      {{0.0, 0.0}, NullDouble(), OUT_OF_RANGE},
      {{1.0, doublemin_denorm}, NullDouble(), OUT_OF_RANGE},
      {{double_pos_inf, 0.0}, NullDouble(), OUT_OF_RANGE},
      {{double_neg_inf, 0.0}, NullDouble(), OUT_OF_RANGE},
      {{double_nan, 0.0}, NullDouble(), OUT_OF_RANGE},

      {{0.0, double_pos_inf}, 0.0},
      {{1.0, double_pos_inf}, 0.0},
      {{0.0, double_neg_inf}, 0.0},
      {{1.0, double_neg_inf}, 0.0},

      {{double_pos_inf, 1.0}, double_pos_inf},
      {{double_pos_inf, -1.0}, double_neg_inf},
      {{double_neg_inf, 1.0}, double_neg_inf},
      {{double_neg_inf, -1.0}, double_pos_inf},

      {{0.0, double_nan}, double_nan},
      {{double_nan, 1.0}, double_nan},

      // numeric
      QueryParamsWithResult({{Numeric(-3), NullNumeric()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullNumeric(), Numeric(5)}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(-3), Numeric(3)}, Numeric(-1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(-3), Numeric(0)}, NullNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(3.33).value()), Numeric(-3)},
           Numeric(NumericValue::FromDouble(-1.11).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(5), Numeric(NumericValue::FromDouble(0.001).value())},
           Numeric(NumericValue::FromDouble(5000).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MinValue())},
                             Numeric(-1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MinValue())},
                             Numeric(1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::FromDouble(0.001).value())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult(
          {{BigNumeric(-3), NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{NullBigNumeric(), BigNumeric(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), BigNumeric(3)}, BigNumeric(-1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(-3), BigNumeric(0)}, NullBigNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("3.33").value()),
            BigNumeric(-3)},
           BigNumeric(BigNumericValue::FromStringStrict("-1.11").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(5),
            BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())},
           BigNumeric(BigNumericValue::FromStringStrict("-5e38").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue::MaxValue())},
                             BigNumeric(1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue::MinValue())},
                             BigNumeric(-1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()),
            BigNumeric(BigNumericValue::FromStringStrict("-0.1").value())},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

namespace {

// Gets a corresponding safe version function test set by converting the regular
// test set (obtained from `regular_test_sets`) with errors to return NULL.
std::vector<QueryParamsWithResult> GetFunctionTestsSafeVersion(
    absl::Span<const std::function<std::vector<QueryParamsWithResult>()>>
        regular_test_set) {
  std::vector<QueryParamsWithResult> regular_tests;
  for (const auto& get_regular_test : regular_test_set) {
    std::vector<QueryParamsWithResult> regular_test = get_regular_test();
    regular_tests.insert(regular_tests.end(), regular_test.begin(),
                         regular_test.end());
  }
  std::vector<QueryParamsWithResult> safe_tests;
  safe_tests.reserve(regular_tests.size());
  for (const QueryParamsWithResult& test : regular_tests) {
    if (test.results().begin()->second.status.code() == OUT_OF_RANGE) {
      std::vector<ValueConstructor> params;
      params.reserve(test.num_params());
      for (const googlesql::Value& param : test.params()) {
        params.emplace_back(param);
      }
      if (test.required_features().empty()) {
        safe_tests.emplace_back(
            QueryParamsWithResult(params, Value::Null(test.result().type())));
      } else {
        safe_tests.emplace_back(
            QueryParamsWithResult(
                params,
                Value::Null(test.results().begin()->second.result.type()))
                .WrapWithFeatureSet(test.results().begin()->first));
      }
    } else {
      safe_tests.push_back(test);
    }
  }
  return safe_tests;
}

}  // namespace

std::vector<QueryParamsWithResult> GetFunctionTestsSafeAdd() {
  return GetFunctionTestsSafeVersion(
      {&GetFunctionTestsAdd, &GetFunctionTestsCoercedAdd});
}

std::vector<QueryParamsWithResult> GetFunctionTestsSafeSubtract() {
  return GetFunctionTestsSafeVersion(
      {&GetFunctionTestsSubtract, &GetFunctionTestsCoercedSubtract});
}

std::vector<QueryParamsWithResult> GetFunctionTestsSafeMultiply() {
  return GetFunctionTestsSafeVersion(
      {&GetFunctionTestsMultiply, &GetFunctionTestsCoercedMultiply});
}

std::vector<QueryParamsWithResult> GetFunctionTestsSafeDivide() {
  return GetFunctionTestsSafeVersion(
      {&GetFunctionTestsDivide, &GetFunctionTestsCoercedDivide});
}

std::vector<QueryParamsWithResult> GetFunctionTestsSafeNegate() {
  return GetFunctionTestsSafeVersion({&GetFunctionTestsUnaryMinus});
}

std::vector<QueryParamsWithResult> GetFunctionTestsCoercedDiv() {
  // These initial tests only cover DIV between two same-type
  // operands.  TODO: Add tests for DIV between
  // different types.
  return {
      // int32
      {{2, 3}, 0ll},
      {{5, 3}, 1ll},
      {{6, 3}, 2ll},
      {{5, 5}, 1ll},
      {{-5, 3}, -1ll},
      {{-2, 3}, 0ll},
      {{4, -3}, -1ll},
      {{2, -3}, 0ll},
      {{-2, -3}, 0ll},
      {{-3, -3}, 1ll},
      {{-5, -3}, 1ll},
      {{int32max, 1}, Int64(int32max)},
      {{1, int32max}, 0ll},
      {{int32max - 1, int32max}, 0ll},
      {{int32max, int32max - 1}, 1ll},

      {{int32min, -1}, Int64(-static_cast<int64_t>(int32min))},

      {{1, 0}, NullInt64(), OUT_OF_RANGE},
      {{0, 0}, NullInt64(), OUT_OF_RANGE},
      {{int32max, 0}, NullInt64(), OUT_OF_RANGE},
      {{int32min, 0}, NullInt64(), OUT_OF_RANGE},

      // uint32
      {{2u, 3u}, 0ull},
      {{5u, 3u}, 1ull},
      {{6u, 3u}, 2ull},
      {{5u, 5u}, 1ull},
      {{uint32max, 1u}, Uint64(uint32max)},
      {{1u, uint32max}, Uint64(uint64_t{0})},
      {{uint32max - 1u, uint32max}, Uint64(uint64_t{0})},
      {{uint32max, uint32max - 1u}, Uint64(uint64_t{1})},

      {{1u, 0u}, NullUint64(), OUT_OF_RANGE},
      {{0u, 0u}, NullUint64(), OUT_OF_RANGE},
      {{uint32max, 0u}, NullUint64(), OUT_OF_RANGE},

      // numeric
      QueryParamsWithResult({{Numeric(-3), Int64(5)}, Numeric(NumericValue())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Int64(5), Numeric(-3)}, Numeric(NumericValue(-1LL))})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(-3), Int64(0)}, NullNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsDiv() {
  return {
      // int64
      {{2ll, 3ll}, 0ll},
      {{5ll, 3ll}, 1ll},
      {{6ll, 3ll}, 2ll},
      {{5ll, 5ll}, 1ll},
      {{-5ll, 3ll}, -1ll},
      {{-2ll, 3ll}, 0ll},
      {{4ll, -3ll}, -1ll},
      {{2ll, -3ll}, 0ll},
      {{-2ll, -3ll}, 0ll},
      {{-3ll, -3ll}, 1ll},
      {{-5ll, -3ll}, 1ll},
      {{int64max, 1ll}, int64max},
      {{1ll, int64max}, 0ll},
      {{int64max - 1ll, int64max}, 0ll},
      {{int64max, int64max - 1ll}, 1ll},

      {{int64min, -1ll}, NullInt64(), OUT_OF_RANGE},  // Overflow.

      {{1ll, 0ll}, NullInt64(), OUT_OF_RANGE},
      {{0ll, 0ll}, NullInt64(), OUT_OF_RANGE},
      {{int64max, 0ll}, NullInt64(), OUT_OF_RANGE},
      {{int64min, 0ll}, NullInt64(), OUT_OF_RANGE},

      // uint64
      {{2ull, 3ull}, 0ull},
      {{5ull, 3ull}, 1ull},
      {{7ull, 3ull}, 2ull},
      {{5ull, 5ull}, 1ull},
      {{uint64max, 1ull}, uint64max},
      {{1ull, uint64max}, 0ull},
      {{uint64max - 1ull, uint64max}, 0ull},
      {{uint64max, uint64max - 1ull}, 1ull},

      {{1ull, 0ull}, NullUint64(), OUT_OF_RANGE},
      {{0ull, 0ull}, NullUint64(), OUT_OF_RANGE},
      {{uint64max, 0ull}, NullUint64(), OUT_OF_RANGE},

      // numeric
      QueryParamsWithResult({{Numeric(-3), Numeric(3)}, Numeric(-1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(NumericValue::FromDouble(3.33).value()), Numeric(-3)},
           Numeric(NumericValue::FromDouble(-1).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {{Numeric(5), Numeric(NumericValue::FromDouble(0.001).value())},
           Numeric(NumericValue::FromDouble(5000).value())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::MinValue())},
                             Numeric(-1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MinValue()),
                              Numeric(NumericValue::MinValue())},
                             Numeric(1)})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Numeric(NumericValue::MaxValue()),
                              Numeric(NumericValue::FromDouble(0.001).value())},
                             NullNumeric(),
                             OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric
      QueryParamsWithResult(
          {{BigNumeric(-3), NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{NullBigNumeric(), BigNumeric(5)}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(-3), BigNumeric(3)}, BigNumeric(-1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(-3), BigNumeric(0)}, NullBigNumeric(), OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::FromStringStrict("3.33").value()),
            BigNumeric(-3)},
           BigNumeric(BigNumericValue::FromStringStrict("-1").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(5),
            BigNumeric(BigNumericValue::FromStringStrict("-1e-38").value())},
           BigNumeric(BigNumericValue::FromStringStrict("-5e38").value())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue::MaxValue())},
                             BigNumeric(1)})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MaxValue()),
                              BigNumeric(BigNumericValue::MinValue())},
                             BigNumeric(BigNumericValue())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({{BigNumeric(BigNumericValue::MinValue()),
                              BigNumeric(BigNumericValue::MaxValue())},
                             BigNumeric(BigNumericValue(-1))})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {{BigNumeric(BigNumericValue::MaxValue()),
            BigNumeric(BigNumericValue::FromStringStrict("-0.1").value())},
           NullBigNumeric(),
           OUT_OF_RANGE})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<FunctionTestCall> GetFunctionTestsMath() {
  float floatmin_denorm = std::numeric_limits<float>::denorm_min();
  double doublemin_denorm = std::numeric_limits<double>::denorm_min();

  std::vector<FunctionTestCall> all_tests = {
      {"abs", {NullInt64()}, NullInt64()},
      {"abs", {0}, 0},
      {"abs", {1}, 1},
      {"abs", {-1}, 1},
      {"abs", {int32max}, int32max},
      {"abs", {int32min}, NullInt32(), OUT_OF_RANGE},
      {"abs", {int32min + 1}, int32max},

      {"abs", {0u}, 0u},
      {"abs", {1u}, 1u},
      {"abs", {uint32max}, uint32max},

      {"abs", {0ll}, 0ll},
      {"abs", {1ll}, 1ll},
      {"abs", {-1ll}, 1ll},
      {"abs", {int64max}, int64max},
      {"abs", {int64min}, NullInt64(), OUT_OF_RANGE},
      {"abs", {int64min + 1}, int64max},

      {"abs", {0ull}, 0ull},
      {"abs", {1ull}, 1ull},
      {"abs", {uint64max}, uint64max},

      {"abs", {0.0f}, 0.0f},
      {"abs", {1.0f}, 1.0f},
      {"abs", {-1.0f}, 1.0f},
      {"abs", {floatmax}, floatmax},
      {"abs", {floatmin}, floatmax},

      {"abs", {0.0}, 0.0},
      {"abs", {1.0}, 1.0},
      {"abs", {-1.0}, 1.0},
      {"abs", {doublemax}, doublemax},
      {"abs", {doublemin}, doublemax},

      {"sign", {NullInt32()}, NullInt32()},
      {"sign", {0}, 0},
      {"sign", {1}, 1},
      {"sign", {-1}, -1},
      {"sign", {int32max}, 1},
      {"sign", {int32min}, -1},

      {"sign", {NullUint32()}, NullUint32()},
      {"sign", {0u}, 0u},
      {"sign", {1u}, 1u},
      {"sign", {uint32max}, 1u},

      {"sign", {NullInt64()}, NullInt64()},
      {"sign", {0ll}, 0ll},
      {"sign", {1ll}, 1ll},
      {"sign", {-1ll}, -1ll},
      {"sign", {int64max}, 1ll},
      {"sign", {int64min}, -1ll},

      {"sign", {NullUint64()}, NullUint64()},
      {"sign", {0ull}, 0ull},
      {"sign", {1ull}, 1ull},
      {"sign", {uint64max}, 1ull},

      {"sign", {0.0f}, 0.0f},
      {"sign", {1.0f}, 1.0f},
      {"sign", {-1.0f}, -1.0f},
      {"sign", {floatmax}, 1.0f},
      {"sign", {floatmin}, -1.0f},
      {"sign", {float_pos_inf}, 1.0f},
      {"sign", {float_neg_inf}, -1.0f},
      {"sign", {float_nan}, float_nan},

      {"sign", {0.0}, 0.0},
      {"sign", {1.0}, 1.0},
      {"sign", {-1.0}, -1.0},
      {"sign", {doublemax}, 1.0},
      {"sign", {doublemin}, -1.0},
      {"sign", {double_pos_inf}, 1.0},
      {"sign", {double_neg_inf}, -1.0},
      {"sign", {double_nan}, double_nan},

      {"is_inf", {NullDouble()}, NullBool()},
      {"is_inf", {0.0}, false},
      {"is_inf", {1.0}, false},
      {"is_inf", {doublemax}, false},
      {"is_inf", {doublemin}, false},
      {"is_inf", {double_pos_inf}, true},
      {"is_inf", {double_neg_inf}, true},
      {"is_inf", {double_nan}, false},

      {"is_nan", {NullDouble()}, NullBool()},
      {"is_nan", {0.0}, false},
      {"is_nan", {double_pos_inf}, false},
      {"is_nan", {double_neg_inf}, false},
      {"is_nan", {double_nan}, true},

      {"ieee_divide", {NullDouble(), NullDouble()}, NullDouble()},
      {"ieee_divide", {NullDouble(), 0.0}, NullDouble()},
      {"ieee_divide", {0.0, NullDouble()}, NullDouble()},

      {"ieee_divide", {4.0f, 2.0f}, 2.0f},
      {"ieee_divide", {floatminpositive, 1.0f}, floatminpositive},
      {"ieee_divide", {1.0f, 0.0f}, float_pos_inf},
      {"ieee_divide", {-1.0f, 0.0f}, float_neg_inf},
      {"ieee_divide", {0.0f, 0.0f}, float_nan},
      {"ieee_divide", {1.0f, floatmin_denorm}, float_pos_inf},

      {"ieee_divide", {1.0f, float_pos_inf}, 0.0f},
      {"ieee_divide", {0.0f, float_pos_inf}, 0.0f},
      {"ieee_divide", {float_pos_inf, 1.0f}, float_pos_inf},
      {"ieee_divide", {0.0f, float_nan}, float_nan},
      {"ieee_divide", {float_nan, 1.0f}, float_nan},

      {"ieee_divide", {4.0, 2.0}, 2.0},
      {"ieee_divide", {doubleminpositive, 1.0}, doubleminpositive},
      {"ieee_divide", {1.0, 0.0}, double_pos_inf},
      {"ieee_divide", {-1.0, 0.0}, double_neg_inf},
      {"ieee_divide", {0.0, 0.0}, double_nan},
      {"ieee_divide", {1.0, doublemin_denorm}, double_pos_inf},

      {"ieee_divide", {0.0, double_pos_inf}, 0.0},
      {"ieee_divide", {1.0, double_pos_inf}, 0.0},
      {"ieee_divide", {double_pos_inf, 1.0}, double_pos_inf},
      {"ieee_divide", {0.0, double_nan}, double_nan},
      {"ieee_divide", {double_nan, 1.0}, double_nan},

      {"sqrt", {NullDouble()}, NullDouble()},
      {"sqrt", {0.0}, 0.0},
      {"sqrt", {1.0}, 1.0},
      {"sqrt", {0.25}, 0.5},
      {"sqrt", {256.0}, 16.0},
      {"sqrt", {double_pos_inf}, double_pos_inf},
      {"sqrt", {double_nan}, double_nan},
      {"sqrt", {-doublemin_denorm}, NullDouble(), OUT_OF_RANGE},
      {"sqrt", {-1.0}, NullDouble(), OUT_OF_RANGE},
      {"sqrt", {double_neg_inf}, NullDouble(), OUT_OF_RANGE},

      // POW and POWER are synonymous.
      {"power", {NullDouble(), NullDouble()}, NullDouble()},
      {"pow", {NullDouble(), 0.0}, NullDouble()},
      {"power", {0.0, NullDouble()}, NullDouble()},
      {"pow", {2.0, 2.0}, 4.0},
      {"power", {64.0, 0.5}, 8.0},

      {"pow", {-1.0, 2.0}, 1.0},
      {"power", {-1.0, 1.0}, -1.0},
      {"pow", {-1.0, -2.0}, 1.0},
      {"power", {-1.0, -1.0}, -1.0},
      {"pow", {-1.0, 0.5}, NullDouble(), OUT_OF_RANGE},

      {"power", {0.0, 0.0}, 1.0},
      {"pow", {0.0, -0.1}, NullDouble(), OUT_OF_RANGE},
      {"power", {0.0, double_neg_inf}, double_pos_inf},
      {"pow", {0.0, 0.1}, 0.0},

      {"power", {2.0, -1075.0}, 0.0},
      {"pow", {2.0, 1024.0}, NullDouble(), OUT_OF_RANGE},
      {"power", {2.0, 1023.0}, ldexp(1.0, 1023)},

      {"pow", {1.0, double_nan}, 1.0},
      {"power", {1.0, double_pos_inf}, 1.0},
      {"pow", {1.0, 0.0}, 1.0},
      {"power", {double_nan, 0.0}, 1.0},
      {"pow", {double_pos_inf, 0.0}, 1.0},
      {"power", {-1.0, double_pos_inf}, 1.0},
      {"pow", {-1.0, double_neg_inf}, 1.0},
      {"power", {1 - (1e-10), double_neg_inf}, double_pos_inf},
      {"pow", {1 + (1e-10), double_neg_inf}, 0.0},
      {"power", {1 - (1e-10), double_pos_inf}, 0.0},
      {"pow", {1 + (1e-10), double_pos_inf}, double_pos_inf},
      {"power", {double_neg_inf, -0.1}, 0.0},
      {"pow", {double_neg_inf, 0.1}, double_pos_inf},
      {"power", {double_neg_inf, 1.0}, double_neg_inf},
      {"pow", {double_neg_inf, 2.0}, double_pos_inf},
      {"power", {double_pos_inf, -0.1}, 0.0},
      {"pow", {double_pos_inf, 0.1}, double_pos_inf},
      {"power", {double_pos_inf, 1.0}, double_pos_inf},
      {"pow", {double_pos_inf, 2.0}, double_pos_inf},

      {"exp", {NullDouble()}, NullDouble()},
      {"exp", {0.0}, 1.0},
      {"exp", {1.0}, M_E},
      {"exp", {-1.0}, 1 / M_E},
      {"exp", {1000.0}, NullDouble(), OUT_OF_RANGE},
      {"exp", {double_neg_inf}, 0.0},
      {"exp", {double_pos_inf}, double_pos_inf},
      {"exp", {double_nan}, double_nan},

      {"ln", {NullDouble()}, NullDouble()},
      {"ln", {1.0}, 0.0},
      {"ln", {1 / M_E}, -1.0},
      {"ln", {M_E}, 1.0},
      {"ln", {M_E * M_E}, 2.0},
      {"ln", {10.0}, M_LN10},
      {"ln", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"ln", {-1.0}, NullDouble(), OUT_OF_RANGE},
      {"ln", {double_neg_inf}, double_nan},
      {"ln", {double_pos_inf}, double_pos_inf},

      {"log", {NullDouble()}, NullDouble()},
      {"log", {1.0}, 0.0},
      {"log", {1 / M_E}, -1.0},
      {"log", {M_E}, 1.0},
      {"log", {M_E * M_E}, 2.0},
      {"log", {10.0}, M_LN10},
      {"log", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"log", {-1.0}, NullDouble(), OUT_OF_RANGE},
      {"log", {double_neg_inf}, double_nan},
      {"log", {double_pos_inf}, double_pos_inf},

      // ABSL_LOG(X, Y)
      {"log", {NullDouble(), NullDouble()}, NullDouble()},
      {"log", {NullDouble(), 0.0}, NullDouble()},
      {"log", {0.0, NullDouble()}, NullDouble()},
      {"log", {1.0, 2.0}, 0.0},
      {"log", {0.5, 2.0}, -1.0},
      {"log", {2.0, 2.0}, 1.0},
      {"log", {1024.0, 2.0}, 10.0},

      {"log", {1.0, 1.0}, NullDouble(), OUT_OF_RANGE},
      {"log", {2.0, 1.0}, NullDouble(), OUT_OF_RANGE},
      {"log", {2.0, 0.0}, NullDouble(), OUT_OF_RANGE},
      {"log", {2.0, -0.5}, NullDouble(), OUT_OF_RANGE},
      {"log", {2.0, double_neg_inf}, double_nan},
      {"log", {0.0, 2.0}, NullDouble(), OUT_OF_RANGE},
      {"log", {-2.0, 2.0}, NullDouble(), OUT_OF_RANGE},

      {"log", {0.5, double_pos_inf}, double_nan},
      {"log", {2.0, double_pos_inf}, double_nan},
      {"log", {double_pos_inf, double_pos_inf}, double_nan},
      {"log", {double_neg_inf, 0.5}, double_nan},
      {"log", {double_neg_inf, 2.0}, double_nan},
      {"log", {double_pos_inf, 0.5}, double_neg_inf},
      {"log", {double_pos_inf, 2.0}, double_pos_inf},

      // LOG10(X)
      {"log10", {NullDouble()}, NullDouble()},
      {"log10", {1.0}, 0.0},
      {"log10", {0.1}, -1.0},
      {"log10", {10.0}, 1.0},
      {"log10", {100.0}, 2.0},
      {"log10", {1e100}, 100.0},
      {"log10", {1e-100}, -100.0},
      {"log10", {M_E}, M_LOG10E},
      {"log10", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"log10", {-1.0}, NullDouble(), OUT_OF_RANGE},
      {"log10", {double_neg_inf}, double_nan},
      {"log10", {double_pos_inf}, double_pos_inf},
  };

  std::vector<FunctionTestCall> numeric_test_cases = {
      {"abs", {NumericValue(0)}, NumericValue(0)},
      {"abs", {NumericValue(1)}, NumericValue(1)},
      {"abs", {NumericValue(-1)}, NumericValue(1)},
      {"abs", {NumericValue::MaxValue()}, NumericValue::MaxValue()},
      {"abs", {NumericValue::MinValue()}, NumericValue::MaxValue()},

      {"sign", {NullNumeric()}, NullNumeric()},
      {"sign", {NumericValue(0)}, NumericValue(0)},
      {"sign", {NumericValue(1)}, NumericValue(1)},
      {"sign", {NumericValue(-1)}, NumericValue(-1)},
      {"sign", {NumericValue::MaxValue()}, NumericValue(1)},
      {"sign", {NumericValue::MinValue()}, NumericValue(-1)},

      {"sqrt", {NumericValue()}, NumericValue()},
      {"sqrt", {NumericValue(0)}, NumericValue(0)},
      {"sqrt", {NumericValue(1)}, NumericValue(1)},
      {"sqrt",
       {NumericValue::FromString("0.25").value()},
       NumericValue::FromString("0.5").value()},
      {"sqrt", {NumericValue(256)}, NumericValue(16)},
      {"sqrt",
       {NumericValue::FromString("1e-8").value()},
       NumericValue::FromString("1e-4").value()},
      {"sqrt",
       {NumericValue::FromString("9e28").value()},
       NumericValue::FromString("3e14").value()},
      {"sqrt",
       {NumericValue::MaxValue()},
       NumericValue::FromString("316227766016837.933199889").value()},
      {"sqrt",
       {NumericValue::FromString("-1e-9").value()},
       NullNumeric(),
       OUT_OF_RANGE},
      {"sqrt", {NumericValue::MinValue()}, NullNumeric(), OUT_OF_RANGE},
      {"sqrt", {NumericValue(-1)}, NullNumeric(), OUT_OF_RANGE},

      {"pow", {NullNumeric(), NullNumeric()}, NullNumeric()},
      {"pow", {NullNumeric(), NumericValue(2)}, NullNumeric()},
      {"pow", {NumericValue(10), NullNumeric()}, NullNumeric()},
      {"power", {NumericValue(), NumericValue(2)}, NumericValue()},
      {"power", {NumericValue(10LL), NumericValue()}, NumericValue(1)},
      {"pow", {NumericValue(2), NumericValue(2)}, NumericValue(4)},
      {"power",
       {NumericValue(64), NumericValue::FromString("0.5").value()},
       NumericValue(8)},
      {"power", {NumericValue(), NumericValue()}, NumericValue(1)},
      {"pow",
       {NumericValue(81), NumericValue::FromString("3.75").value()},
       NumericValue(14348907)},
      {"power",
       {NumericValue::MaxValue(), NumericValue::FromString("0.5").value()},
       NumericValue::FromString("316227766016837.933199889").value()},

      {"pow", {NumericValue(-1), NumericValue(2)}, NumericValue(1)},
      {"power", {NumericValue(-1), NumericValue(1)}, NumericValue(-1)},
      {"pow", {NumericValue(-1), NumericValue(-2)}, NumericValue(1)},
      {"power", {NumericValue(-1), NumericValue(-1)}, NumericValue(-1)},
      {"pow",
       {NumericValue(-1), NumericValue::FromString("0.5").value()},
       NullNumeric(),
       OUT_OF_RANGE},
      {"pow",
       {NumericValue(), NumericValue::FromString("-0.1").value()},
       NullNumeric(),
       OUT_OF_RANGE},
      {"pow",
       {NumericValue::MaxValue(), NumericValue(2)},
       NullNumeric(),
       OUT_OF_RANGE},

      {"exp", {NullNumeric()}, NullNumeric()},
      {"exp", {NumericValue(0)}, NumericValue(1)},
      {"exp",
       {NumericValue(1)},
       NumericValue::FromString("2.718281828").value()},
      {"exp",
       {NumericValue(-1)},
       NumericValue::FromString("0.367879441").value()},
      {"exp",
       {NumericValue::FromString("0.5").value()},
       NumericValue::FromString("1.648721271").value()},
      {"exp",
       {NumericValue::FromString("-0.5").value()},
       NumericValue::FromString("0.60653066").value()},
      {"exp",
       {NumericValue(10)},
       NumericValue::FromString("22026.465794807").value()},
      {"exp", {NumericValue(1000)}, NullNumeric(), OUT_OF_RANGE},
      {"exp", {NumericValue::MaxValue()}, NullNumeric(), OUT_OF_RANGE},
      {"exp", {NumericValue::MinValue()}, NumericValue(0)},

      {"ln", {NullNumeric()}, NullNumeric()},
      {"ln", {NumericValue(1)}, NumericValue(0)},
      {"ln",
       {NumericValue::FromString("0.367879441").value()},
       NumericValue(-1)},
      {"ln",
       {NumericValue::FromString("2.718281828").value()},
       NumericValue(1)},
      {"ln",
       {NumericValue::FromString("0.5").value()},
       NumericValue::FromString("-0.693147181").value()},
      {"ln",
       {NumericValue::FromString("7.389056099").value()},
       NumericValue(2)},
      {"ln",
       {NumericValue(10)},
       NumericValue::FromString("2.302585093").value()},
      {"ln", {NumericValue(0)}, NullNumeric(), OUT_OF_RANGE},
      {"ln", {NumericValue(-1)}, NullNumeric(), OUT_OF_RANGE},
      {"ln", {NumericValue::MinValue()}, NullNumeric(), OUT_OF_RANGE},
      {"ln",
       {NumericValue::MaxValue()},
       NumericValue::FromString("66.774967697").value()},

      {"log10", {NullNumeric()}, NullNumeric()},
      {"log10", {NumericValue(1)}, NumericValue(0)},
      {"log10", {NumericValue::FromString("0.1").value()}, NumericValue(-1)},
      {"log10", {NumericValue::FromString("10").value()}, NumericValue(1)},
      {"log10", {NumericValue::FromString("1e-9").value()}, NumericValue(-9)},
      {"log10", {NumericValue::FromString("1e28").value()}, NumericValue(28)},
      {"log10",
       {NumericValue::FromString("0.5").value()},
       NumericValue::FromString("-0.301029996").value()},
      {"log10",
       {NumericValue(20)},
       NumericValue::FromString("1.301029996").value()},
      {"log10", {NumericValue(0)}, NullNumeric(), OUT_OF_RANGE},
      {"log10", {NumericValue(-1)}, NullNumeric(), OUT_OF_RANGE},
      {"log10", {NumericValue::MinValue()}, NullNumeric(), OUT_OF_RANGE},
      {"log10", {NumericValue::MaxValue()}, NumericValue(29)},

      {"log", {NumericValue(9), NumericValue(3)}, NumericValue(2)},
      {"log",
       {NumericValue::FromString("0.01").value(),
        NumericValue::FromString("0.1").value()},
       NumericValue(2)},
      {"log",
       {NumericValue::FromString("0.25").value(), NumericValue(2)},
       NumericValue(-2)},
      {"log",
       {NumericValue(2), NumericValue(4)},
       NumericValue::FromString("0.5").value()},
      {"log",
       {NumericValue::FromString("0.5").value(), NumericValue(4)},
       NumericValue::FromString("-0.5").value()},
      {"log",
       {NumericValue::FromString("1.000100005").value(),
        NumericValue::FromString("1.00001").value()},
       NumericValue::FromString("10.000049983").value()},
      {"log",
       {NumericValue::MaxValue(), NumericValue::MaxValue()},
       NumericValue(1)},
      {"log", {NumericValue(10), NumericValue(1)}, NullNumeric(), OUT_OF_RANGE},
      {"log", {NumericValue(10), NumericValue(0)}, NullNumeric(), OUT_OF_RANGE},
      {"log",
       {NumericValue(10), NumericValue(-1)},
       NullNumeric(),
       OUT_OF_RANGE},
      {"log", {NumericValue(0), NumericValue(10)}, NullNumeric(), OUT_OF_RANGE},
      {"log",
       {NumericValue(-1), NumericValue(10)},
       NullNumeric(),
       OUT_OF_RANGE},
  };

  for (const auto& test_case : numeric_test_cases) {
    all_tests.emplace_back(FunctionTestCall(
        test_case.function_name,
        test_case.params.WrapWithFeature(FEATURE_NUMERIC_TYPE)));
  }

  std::vector<FunctionTestCall> bignumeric_test_cases = {
      {"abs", {BigNumericValue(0)}, BigNumericValue(0)},
      {"abs", {BigNumericValue(1)}, BigNumericValue(1)},
      {"abs", {BigNumericValue(-1)}, BigNumericValue(1)},
      {"abs", {BigNumericValue::MaxValue()}, BigNumericValue::MaxValue()},
      {"abs", {BigNumericValue::MinValue()}, NullBigNumeric(), OUT_OF_RANGE},

      {"sign", {BigNumericValue()}, BigNumericValue()},
      {"sign", {BigNumericValue(0)}, BigNumericValue(0)},
      {"sign", {BigNumericValue(1)}, BigNumericValue(1)},
      {"sign", {BigNumericValue(-1)}, BigNumericValue(-1)},
      {"sign", {BigNumericValue::MaxValue()}, BigNumericValue(1)},
      {"sign", {BigNumericValue::MinValue()}, BigNumericValue(-1)},

      {"pow", {NullBigNumeric(), NullBigNumeric()}, NullBigNumeric()},
      {"pow", {NullBigNumeric(), BigNumericValue(2)}, NullBigNumeric()},
      {"pow", {BigNumericValue(10), NullBigNumeric()}, NullBigNumeric()},
      {"power", {BigNumericValue(), BigNumericValue(2)}, BigNumericValue()},
      {"power", {BigNumericValue(10LL), BigNumericValue()}, BigNumericValue(1)},
      {"pow", {BigNumericValue(2), BigNumericValue(2)}, BigNumericValue(4)},
      {"power",
       {BigNumericValue(64), BigNumericValue::FromString("0.5").value()},
       BigNumericValue(8)},
      {"power", {BigNumericValue(), BigNumericValue()}, BigNumericValue(1)},
      {"pow",
       {BigNumericValue(43046721),
        BigNumericValue::FromString("4.1875").value()},
       BigNumericValue::FromString("92709463147897837085761925410587").value()},
      {"power",
       {BigNumericValue::MaxValue(),
        BigNumericValue::FromString("0.5").value()},
       BigNumericValue::FromString(
           "24061596916800451154.5033772477625056927114980741063148377")
           .value()},

      {"sqrt", {BigNumericValue()}, BigNumericValue()},
      {"sqrt", {BigNumericValue(0)}, BigNumericValue(0)},
      {"sqrt", {BigNumericValue(1)}, BigNumericValue(1)},
      {"sqrt",
       {BigNumericValue::FromString("0.25").value()},
       BigNumericValue::FromString("0.5").value()},
      {"sqrt", {BigNumericValue(256)}, BigNumericValue(16)},
      {"sqrt",
       {BigNumericValue::FromString("1e-38").value()},
       BigNumericValue::FromString("1e-19").value()},
      {"sqrt",
       {BigNumericValue::FromString("4e38").value()},
       BigNumericValue::FromString("2e19").value()},
      {"sqrt",
       {BigNumericValue::MaxValue()},
       BigNumericValue::FromString(
           "24061596916800451154.5033772477625056927114980741063148377")
           .value()},
      {"sqrt",
       {BigNumericValue::FromString("-1e-38").value()},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"sqrt", {BigNumericValue::MinValue()}, NullBigNumeric(), OUT_OF_RANGE},
      {"sqrt", {BigNumericValue(-1)}, NullBigNumeric(), OUT_OF_RANGE},

      {"pow", {BigNumericValue(-1), BigNumericValue(2)}, BigNumericValue(1)},
      {"power", {BigNumericValue(-1), BigNumericValue(1)}, BigNumericValue(-1)},
      {"pow", {BigNumericValue(-1), BigNumericValue(-2)}, BigNumericValue(1)},
      {"power",
       {BigNumericValue(-1), BigNumericValue(-1)},
       BigNumericValue(-1)},
      {"pow",
       {BigNumericValue(-1), BigNumericValue::FromString("0.5").value()},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"pow",
       {BigNumericValue(), BigNumericValue::FromString("-0.1").value()},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"pow",
       {BigNumericValue::MaxValue(), BigNumericValue(2)},
       NullBigNumeric(),
       OUT_OF_RANGE},

      {"exp", {NullBigNumeric()}, NullBigNumeric()},
      {"exp", {BigNumericValue(0)}, BigNumericValue(1)},
      {"exp",
       {BigNumericValue(1)},
       BigNumericValue::FromString("2.71828182845904523536028747135266249776")
           .value()},
      {"exp",
       {BigNumericValue(-1)},
       BigNumericValue::FromString("0.36787944117144232159552377016146086745")
           .value()},
      {"exp",
       {BigNumericValue::FromString("0.5").value()},
       BigNumericValue::FromString("1.64872127070012814684865078781416357165")
           .value()},
      {"exp",
       {BigNumericValue::FromString("-0.5").value()},
       BigNumericValue::FromString("0.60653065971263342360379953499118045344")
           .value()},
      {"exp",
       {BigNumericValue(10)},
       BigNumericValue::FromString(
           "22026.46579480671651695790064528424436635351")
           .value()},
      {"exp", {BigNumericValue(1000)}, NullBigNumeric(), OUT_OF_RANGE},
      {"exp", {BigNumericValue::MaxValue()}, NullBigNumeric(), OUT_OF_RANGE},
      {"exp", {BigNumericValue::MinValue()}, BigNumericValue(0)},

      {"ln", {NullBigNumeric()}, NullBigNumeric()},
      {"ln", {BigNumericValue(1)}, BigNumericValue(0)},
      {"ln",
       {BigNumericValue::FromString("0.36787944117144232159552377016146086745")
            .value()},
       BigNumericValue::FromString("-0.99999999999999999999999999999999999999")
           .value()},
      {"ln",
       {BigNumericValue::FromString("2.71828182845904523536028747135266249776")
            .value()},
       BigNumericValue(1)},
      {"ln",
       {BigNumericValue::FromString("0.5").value()},
       BigNumericValue::FromString("-0.69314718055994530941723212145817656808")
           .value()},
      {"ln",
       {BigNumericValue::FromString("7.38905609893065022723042746057500781318")
            .value()},
       BigNumericValue(2)},
      {"ln",
       {BigNumericValue(10)},
       BigNumericValue::FromString("2.3025850929940456840179914546843642076")
           .value()},
      {"ln", {BigNumericValue(0)}, NullBigNumeric(), OUT_OF_RANGE},
      {"ln", {BigNumericValue(-1)}, NullBigNumeric(), OUT_OF_RANGE},
      {"ln", {BigNumericValue::MinValue()}, NullBigNumeric(), OUT_OF_RANGE},
      {"ln",
       {BigNumericValue::MaxValue()},
       BigNumericValue::FromString("89.25429750901231790871051569382918497041")
           .value()},

      {"log10", {NullBigNumeric()}, NullBigNumeric()},
      {"log10", {BigNumericValue(1)}, BigNumericValue(0)},
      {"log10",
       {BigNumericValue::FromString("0.1").value()},
       BigNumericValue(-1)},
      {"log10",
       {BigNumericValue::FromString("10").value()},
       BigNumericValue(1)},
      {"log10",
       {BigNumericValue::FromString("1e-38").value()},
       BigNumericValue(-38)},
      {"log10",
       {BigNumericValue::FromString("1e38").value()},
       BigNumericValue(38)},
      {"log10",
       {BigNumericValue::FromString("0.5").value()},
       BigNumericValue::FromString("-0.30102999566398119521373889472449302677")
           .value()},
      {"log10",
       {BigNumericValue(20)},
       BigNumericValue::FromString("1.30102999566398119521373889472449302677")
           .value()},
      {"log10", {BigNumericValue(0)}, NullBigNumeric(), OUT_OF_RANGE},
      {"log10", {BigNumericValue(-1)}, NullBigNumeric(), OUT_OF_RANGE},
      {"log10", {BigNumericValue::MinValue()}, NullBigNumeric(), OUT_OF_RANGE},
      {"log10",
       {BigNumericValue::MaxValue()},
       BigNumericValue::FromString("38.76264889431520477950341815474572182589")
           .value()},

      {"log", {BigNumericValue(9), BigNumericValue(3)}, BigNumericValue(2)},
      {"log",
       {BigNumericValue::FromString("0.01").value(),
        BigNumericValue::FromString("0.1").value()},
       BigNumericValue(2)},
      {"log",
       {BigNumericValue::FromString("0.25").value(), BigNumericValue(2)},
       BigNumericValue(-2)},
      {"log",
       {BigNumericValue(2), BigNumericValue(4)},
       BigNumericValue::FromString("0.5").value()},
      {"log",
       {BigNumericValue::FromString("0.5").value(), BigNumericValue(4)},
       BigNumericValue::FromString("-0.5").value()},
      {"log",
       {BigNumericValue::FromString("1.0001000045001200021000252002100012")
            .value(),
        BigNumericValue::FromString("1.00001").value()},
       BigNumericValue::FromString("9.99999999999999999999999999999999955004")
           .value()},
      {"log",
       {BigNumericValue::MaxValue(), BigNumericValue::MaxValue()},
       BigNumericValue(1)},
      {"log",
       {BigNumericValue(10), BigNumericValue(1)},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"log",
       {BigNumericValue(10), BigNumericValue(0)},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"log",
       {BigNumericValue(10), BigNumericValue(-1)},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"log",
       {BigNumericValue(0), BigNumericValue(10)},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"log",
       {BigNumericValue(-1), BigNumericValue(10)},
       NullBigNumeric(),
       OUT_OF_RANGE},
  };

  for (const auto& test_case : bignumeric_test_cases) {
    all_tests.emplace_back(FunctionTestCall(
        test_case.function_name,
        test_case.params.WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)));
  }

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsCbrt() {
  std::vector<FunctionTestCall> all_tests = {
      {"cbrt", {NullDouble()}, NullDouble()},
      {"cbrt", {0.0}, 0.0},
      {"cbrt", {1.0}, 1.0},
      {"cbrt", {0.008}, 0.2},
      {"cbrt", {64.0}, 4.0},
      {"cbrt", {-1.0}, -1.0},
      {"cbrt", {-1000.0}, -10.0},
      {"cbrt", {double_nan}, double_nan},
      {"cbrt", {double_pos_inf}, double_pos_inf},
      {"cbrt", {double_neg_inf}, double_neg_inf},
  };

  std::vector<FunctionTestCall> numeric_tests = {
      {"cbrt",
       {NumericValue::FromString("0.125").value()},
       NumericValue::FromString("0.5").value()},
      {"cbrt",
       {NumericValue::FromString("-0.125").value()},
       NumericValue::FromString("-0.5").value()},
      {"cbrt", {NumericValue(216)}, NumericValue(6)},
      {"cbrt", {NumericValue(-216)}, NumericValue(-6)},
      {"cbrt",
       {NumericValue::FromString("1e-9").value()},
       NumericValue::FromString("1e-3").value()},
      {"cbrt",
       {NumericValue::FromString("64e27").value()},
       NumericValue::FromString("4e9").value()},
      {"cbrt",
       {NumericValue::MaxValue()},
       NumericValue::FromString("4641588833.612778892").value()},
      {"cbrt",
       {NumericValue::MinValue()},
       NumericValue::FromString("-4641588833.612778892").value()},
  };

  for (auto& test_case : numeric_tests) {
    test_case.params.AddRequiredFeature(FEATURE_NUMERIC_TYPE);
    all_tests.emplace_back(test_case);
  }

  std::vector<FunctionTestCall> bignumeric_tests = {
      {"cbrt", {BigNumericValue()}, BigNumericValue()},
      {"cbrt", {BigNumericValue(0)}, BigNumericValue(0)},
      {"cbrt", {BigNumericValue(1)}, BigNumericValue(1)},
      {"cbrt", {BigNumericValue(-1)}, BigNumericValue(-1)},
      {"cbrt",
       {BigNumericValue::FromString("0.125").value()},
       BigNumericValue::FromString("0.5").value()},
      {"cbrt",
       {BigNumericValue::FromString("-0.125").value()},
       BigNumericValue::FromString("-0.5").value()},
      {"cbrt", {BigNumericValue(216)}, BigNumericValue(6)},
      {"cbrt", {BigNumericValue(-216)}, BigNumericValue(-6)},
      {"cbrt",
       {BigNumericValue::FromString("1e-36").value()},
       BigNumericValue::FromString("1e-12").value()},
      {"cbrt",
       {BigNumericValue::FromString("64e36").value()},
       BigNumericValue::FromString("4e12").value()},
      {"cbrt",
       {BigNumericValue::MaxValue()},
       BigNumericValue::FromString(
           "8334565515049.55065578647965760880872812752814461188")
           .value()},
      {"cbrt",
       {BigNumericValue::MinValue()},
       BigNumericValue::FromString(
           "-8334565515049.55065578647965760880872812752814461188")
           .value()},
  };

  for (auto& test_case : bignumeric_tests) {
    test_case.params.AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE);
    all_tests.emplace_back(test_case);
  }

  for (auto& test_case : all_tests) {
    test_case.params.AddRequiredFeature(FEATURE_CBRT_FUNCTIONS);
  }

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsPi(bool include_safe_calls) {
  std::vector<FunctionTestCall> all_tests;

  FunctionTestCall double_test = {"pi", /*arguments=*/{}, M_PI};
  double_test.params.AddRequiredFeature(FEATURE_PI_FUNCTIONS);
  all_tests.emplace_back(double_test);

  FunctionTestCall numeric_test = {
      "pi_numeric", /*arguments=*/{},
      NumericValue::FromString("3.141592654").value()};
  numeric_test.params.AddRequiredFeatures(
      {FEATURE_NUMERIC_TYPE, FEATURE_PI_FUNCTIONS});
  all_tests.emplace_back(numeric_test);

  FunctionTestCall bignumeric_test{
      "pi_bignumeric",
      /*arguments=*/{},
      BigNumericValue::FromString("3.14159265358979323846264338327950288420")
          .value()};
  bignumeric_test.params.AddRequiredFeatures(
      {FEATURE_BIGNUMERIC_TYPE, FEATURE_PI_FUNCTIONS});
  all_tests.emplace_back(bignumeric_test);

  // Safe versions of the above tests
  FunctionTestCall safe_double_test = {"safe.pi", /*arguments=*/{}, M_PI};
  safe_double_test.params.AddRequiredFeatures(
      {FEATURE_PI_FUNCTIONS, FEATURE_SAFE_FUNCTION_CALL});

  FunctionTestCall safe_numeric_test = {
      "safe.pi_numeric", /*arguments=*/{},
      NumericValue::FromString("3.141592654").value()};
  safe_numeric_test.params.AddRequiredFeatures(
      {FEATURE_NUMERIC_TYPE, FEATURE_PI_FUNCTIONS, FEATURE_SAFE_FUNCTION_CALL});

  FunctionTestCall safe_bignumeric_test{
      "safe.pi_bignumeric",
      /*arguments=*/{},
      BigNumericValue::FromString("3.14159265358979323846264338327950288420")
          .value()};
  safe_bignumeric_test.params.AddRequiredFeatures({FEATURE_BIGNUMERIC_TYPE,
                                                   FEATURE_PI_FUNCTIONS,
                                                   FEATURE_SAFE_FUNCTION_CALL});

  if (include_safe_calls) {
    all_tests.emplace_back(safe_double_test);
    all_tests.emplace_back(safe_numeric_test);
    all_tests.emplace_back(safe_bignumeric_test);
  }

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsDegreesRadiansPi() {
  // The tests for RADIANS, and DEGREES (and later CBRT)
  // NOTE: Some of the Numeric and BigNumeric tests' expected values are
  // multiples of PI, but shifted by a small amount to account for the numerical
  // error introduced along the way
  std::vector<FunctionTestCall> all_tests = {
      // RADIANS
      // Exceptional cases
      {"radians", {NullDouble()}, NullDouble()},
      {"radians", {double_nan}, double_nan},
      {"radians", {double_pos_inf}, double_pos_inf},
      {"radians", {double_neg_inf}, double_neg_inf},
      // Common cases
      {"radians", {0.0}, 0.0},
      {"radians", {45.0}, M_PI_4, kApproximate},
      {"radians", {90.0}, M_PI_2, kApproximate},
      {"radians", {135.0}, 3 * M_PI_4, kApproximate},
      {"radians", {180.0}, M_PI, kApproximate},
      {"radians", {225.0}, 5 * M_PI_4, kApproximate},
      {"radians", {270.0}, 3 * M_PI_2, kApproximate},
      {"radians", {315.0}, 7 * M_PI_4, kApproximate},
      {"radians", {360.0}, 2 * M_PI, kApproximate},
      {"radians", {-45.0}, -M_PI_4, kApproximate},
      {"radians", {-90.0}, -M_PI_2, kApproximate},
      {"radians", {-135.0}, -3 * M_PI_4, kApproximate},
      {"radians", {-180.0}, -M_PI, kApproximate},
      {"radians", {-225.0}, -5 * M_PI_4, kApproximate},
      {"radians", {-270.0}, -3 * M_PI_2, kApproximate},
      {"radians", {-315.0}, -7 * M_PI_4, kApproximate},
      {"radians", {-360.0}, -2 * M_PI, kApproximate},

      // DEGREES
      // Exceptional cases
      {"degrees", {NullDouble()}, NullDouble()},
      {"degrees", {double_nan}, double_nan},
      {"degrees", {double_pos_inf}, double_pos_inf},
      {"degrees", {double_neg_inf}, double_neg_inf},
      // Common cases
      {"degrees", {0.0}, 0.0},
      {"degrees", {M_PI_4}, 45.0, kApproximate},
      {"degrees", {M_PI_2}, 90.0, kApproximate},
      {"degrees", {3 * M_PI_4}, 135.0, kApproximate},
      {"degrees", {M_PI}, 180.0, kApproximate},
      {"degrees", {5 * M_PI_4}, 225.0, kApproximate},
      {"degrees", {3 * M_PI_2}, 270.0, kApproximate},
      {"degrees", {7 * M_PI_4}, 315.0, kApproximate},
      {"degrees", {2 * M_PI}, 360.0, kApproximate},
      {"degrees", {-M_PI_4}, -45.0, kApproximate},
      {"degrees", {-M_PI_2}, -90.0, kApproximate},
      {"degrees", {-3 * M_PI_4}, -135.0, kApproximate},
      {"degrees", {-M_PI}, -180.0, kApproximate},
      {"degrees", {-5 * M_PI_4}, -225.0, kApproximate},
      {"degrees", {-3 * M_PI_2}, -270.0, kApproximate},
      {"degrees", {-7 * M_PI_4}, -315.0, kApproximate},
      {"degrees", {-2 * M_PI}, -360.0, kApproximate},
  };

  std::vector<FunctionTestCall> numeric_tests = {
      // RADIANS
      // Exceptional cases
      {"radians", {NullNumeric()}, NullNumeric()},
      {"radians",
       {NumericValue::MinValue()},
       NumericValue::FromString("-1745329251994329576923690768.488612713")
           .value()},
      {"radians",
       {NumericValue::MaxValue()},
       NumericValue::FromString("1745329251994329576923690768.488612713")
           .value()},
      {"radians",
       {NumericValue::FromScaledValue(1)},
       NumericValue::FromString("0.0").value()},
      {"radians",
       {NumericValue::FromString("18000000000000000000000000000").value()},
       NumericValue::FromString("314159265358979323846264338.327950288")
           .value()},

      // Common cases
      {"radians",
       {NumericValue::FromString("0.0").value()},
       NumericValue::FromString("0.0").value()},
      {"radians",
       {NumericValue::FromString("90.0").value()},
       NumericValue::FromString("1.570796327").value()},
      {"radians",
       {NumericValue::FromString("180.0").value()},
       NumericValue::FromString("3.141592654").value()},
      {"radians",
       {NumericValue::FromString("270.0").value()},
       NumericValue::FromString("4.71238898").value()},
      {"radians",
       {NumericValue::FromString("360.0").value()},
       NumericValue::FromString("6.283185307").value()},
      {"radians",
       {NumericValue::FromString("-90.0").value()},
       NumericValue::FromString("-1.570796327").value()},
      {"radians",
       {NumericValue::FromString("-180.0").value()},
       NumericValue::FromString("-3.141592654").value()},
      {"radians",
       {NumericValue::FromString("-270.0").value()},
       NumericValue::FromString("-4.71238898").value()},
      {"radians",
       {NumericValue::FromString("-360.0").value()},
       NumericValue::FromString("-6.283185307").value()},
      {"radians",
       {NumericValue::MaxValue()},
       NumericValue::FromString("1745329251994329576923690768.488612713")
           .value()},
      {"radians",
       {NumericValue::MinValue()},
       NumericValue::FromString("-1745329251994329576923690768.488612713")
           .value()},

      // DEGREES
      // Exceptional cases
      {"degrees", {NullNumeric()}, NullNumeric()},
      {"degrees",
       {NumericValue::FromString("1745329251994329576923690768.488612713")
            .value()},
       NumericValue::FromString("99999999999999999999999999999.999999975")
           .value()},
      {"degrees",
       {NumericValue::FromString("1745329251994329576923690768.488612714")
            .value()},
       NullNumeric(),
       OUT_OF_RANGE},
      {"degrees",
       {NumericValue::FromString("-1745329251994329576923690768.488612713")
            .value()},
       NumericValue::FromString("-99999999999999999999999999999.999999975")
           .value()},
      {"degrees",
       {NumericValue::FromString("-1745329251994329576923690768.488612714")
            .value()},
       NullNumeric(),
       OUT_OF_RANGE},
      {"degrees",
       {NumericValue::FromScaledValue(1)},
       NumericValue::FromString("0.000000057").value()},

      {"degrees",
       {NumericValue::FromString("314159265358979323846264338.327950288")
            .value()},
       NumericValue::FromString("17999999999999999999999999999.999999976")
           .value()},

      // Common cases
      {"degrees",
       {NumericValue::FromString("0.0").value()},
       NumericValue::FromString("0.0").value()},
      {"degrees",
       {NumericValue::FromDouble(M_PI_2).value()},
       NumericValue::FromString("90.000000012").value()},
      {"degrees",
       {NumericValue::FromDouble(M_PI).value()},
       NumericValue::FromString("180.000000024").value()},
      {"degrees",
       {NumericValue::FromDouble(3 * M_PI_2).value()},
       NumericValue::FromString("269.999999978").value()},
      {"degrees",
       {NumericValue::FromDouble(2 * M_PI).value()},
       NumericValue::FromString("359.99999999").value()},

      {"degrees",
       {NumericValue::FromDouble(-M_PI_2).value()},
       NumericValue::FromString("-90.000000012").value()},
      {"degrees",
       {NumericValue::FromDouble(-M_PI).value()},
       NumericValue::FromString("-180.000000024").value()},
      {"degrees",
       {NumericValue::FromDouble(-3 * M_PI_2).value()},
       NumericValue::FromString("-269.999999978").value()},
      {"degrees",
       {NumericValue::FromDouble(-2 * M_PI).value()},
       NumericValue::FromString("-359.99999999").value()},
  };

  for (const auto& test_case : numeric_tests) {
    all_tests.emplace_back(FunctionTestCall(
        test_case.function_name,
        test_case.params.WrapWithFeature(FEATURE_NUMERIC_TYPE)));
  }

  std::vector<FunctionTestCall> bignumeric_tests = {
      // RADIANS
      // Exceptional cases
      {"radians", {NullBigNumeric()}, NullBigNumeric()},
      {"radians",
       {BigNumericValue::MinValue()},
       BigNumericValue::FromString("-10104766024771286785562081743825829518."
                                   "14981603689978972107700419772052484628")
           .value()},
      {"radians",
       {BigNumericValue::MaxValue()},
       BigNumericValue::FromString("10104766024771286785562081743825829518."
                                   "14981603689978972107700419772052484628")
           .value()},
      {"radians",
       {BigNumericValue::FromScaledValue(1)},
       BigNumericValue::FromString("0.0").value()},
      {"radians",
       {BigNumericValue::FromString("180000000000000000000000000000000000000")
            .value()},
       BigNumericValue::FromString("3141592653589793238462643383279502884."
                                   "19716939937510582097494459230781640629")
           .value()},

      // Common cases
      {"radians",
       {BigNumericValue::FromString("0.0").value()},
       BigNumericValue::FromString("0.0").value()},
      {"radians",
       {BigNumericValue::FromString("90.0").value()},
       BigNumericValue::FromString("1.5707963267948966192313216916397514421")
           .value()},
      {"radians",
       {BigNumericValue::FromString("180.0").value()},
       BigNumericValue::FromString("3.1415926535897932384626433832795028842")
           .value()},
      {"radians",
       {BigNumericValue::FromString("270.0").value()},
       BigNumericValue::FromString("4.7123889803846898576939650749192543263")
           .value()},
      {"radians",
       {BigNumericValue::FromString("360.0").value()},
       BigNumericValue::FromString("6.28318530717958647692528676655900576839")
           .value()},
      {"radians",
       {BigNumericValue::FromString("-90.0").value()},
       BigNumericValue::FromString("-1.5707963267948966192313216916397514421")
           .value()},
      {"radians",
       {BigNumericValue::FromString("-180.0").value()},
       BigNumericValue::FromString("-3.1415926535897932384626433832795028842")
           .value()},
      {"radians",
       {BigNumericValue::FromString("-270.0").value()},
       BigNumericValue::FromString("-4.7123889803846898576939650749192543263")
           .value()},
      {"radians",
       {BigNumericValue::FromString("-360.0").value()},
       BigNumericValue::FromString("-6.28318530717958647692528676655900576839")
           .value()},

      // DEGREES
      // Exceptional cases
      {"degrees", {NullBigNumeric()}, NullBigNumeric()},
      {"degrees",
       {BigNumericValue::FromString("10104766024771286785562081743825829518."
                                    "14981603689978972107700419772052484628")
            .value()},
       BigNumericValue::FromString("578960446186580977117854925043439539266."
                                   "34992332820282019728792003956564819952")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("10104766024771286785562081743825829518."
                                    "14981603689978972107700419772052484629")
            .value()},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"degrees",
       {BigNumericValue::FromString("-10104766024771286785562081743825829518."
                                    "14981603689978972107700419772052484628")
            .value()},
       BigNumericValue::FromString("-578960446186580977117854925043439539266."
                                   "34992332820282019728792003956564819952")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("-10104766024771286785562081743825829518."
                                    "14981603689978972107700419772052484629")
            .value()},
       NullBigNumeric(),
       OUT_OF_RANGE},
      {"degrees",
       {BigNumericValue::FromScaledValue(1)},
       BigNumericValue::FromString("0.00000000000000000000000000000000000057")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("3141592653589793238462643383279502884."
                                    "19716939937510582097494459230781640629")
            .value()},
       BigNumericValue::FromString("180000000000000000000000000000000000000."
                                   "00000000000000000000000000000000000022")
           .value()},

      // Common cases
      {"degrees",
       {BigNumericValue::FromString("0.0").value()},
       BigNumericValue::FromString("0.0").value()},
      {"degrees",
       {BigNumericValue::FromString("1.5707963267948966192313216916397514417")
            .value()},
       BigNumericValue::FromString("89.99999999999999999999999999999999997716")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("3.1415926535897932384626433832795028834")
            .value()},
       BigNumericValue::FromString("179.99999999999999999999999999999999995433")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("4.7123889803846898576939650749192543251")
            .value()},
       BigNumericValue::FromString("269.99999999999999999999999999999999993149")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("6.2831853071795864769252867665590057668")
            .value()},
       BigNumericValue::FromString("359.99999999999999999999999999999999990865")
           .value()},

      {"degrees",
       {BigNumericValue::FromString("-1.5707963267948966192313216916397514417")
            .value()},
       BigNumericValue::FromString("-89.99999999999999999999999999999999997716")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("-3.1415926535897932384626433832795028834")
            .value()},
       BigNumericValue::FromString(
           "-179.99999999999999999999999999999999995433")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("-4.7123889803846898576939650749192543251")
            .value()},
       BigNumericValue::FromString(
           "-269.99999999999999999999999999999999993149")
           .value()},
      {"degrees",
       {BigNumericValue::FromString("-6.2831853071795864769252867665590057668")
            .value()},
       BigNumericValue::FromString(
           "-359.99999999999999999999999999999999990865")
           .value()},
  };

  for (const auto& test_case : bignumeric_tests) {
    all_tests.emplace_back(FunctionTestCall(
        test_case.function_name,
        test_case.params.WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)));
  }

  return all_tests;
}

}  // namespace googlesql
