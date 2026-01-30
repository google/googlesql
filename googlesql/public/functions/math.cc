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

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/functions/rounding_mode.pb.h"
#include "googlesql/public/numeric_value.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace googlesql {
namespace functions {
namespace {

// Minimum and maximum decimal exponent of numbers representable as double.
// "numeric_limits<double>::digits10 - 1"  is to take into account subnormal
// numbers.
const int kDoubleMinExponent = std::numeric_limits<double>::min_exponent10
                             - std::numeric_limits<double>::digits10 - 1;
const int kDoubleMaxExponent = std::numeric_limits<double>::max_exponent10;

// Minimum and maximum decimal exponent of numbers representable as float.
// "numeric_limits<float>::digits10 - 1"  is to take into account subnormal
// numbers.
const int kFloatMinExponent = std::numeric_limits<float>::min_exponent10
                            - std::numeric_limits<float>::digits10 - 1;
const int kFloatMaxExponent = std::numeric_limits<float>::max_exponent10;

// Precomputed powers of 10 in the entire ranges
// [kDoubleMinExponent, kDoubleMaxExponent] and
// [kFloatMinExponent, kFloatMaxExponent].
// We use "long double" instead of double and "double" for float exponents
// to avoid getting into subnormal numbers, and thus keeping relative error
// better than epsilon.
// Note that on some platforms "long double" may be the same type as double.
// On such platforms results of RoundDecimal() and TruncDecimal() will
// have error greater than 1 ULP if 10^(-digits) is a subnormal double number.
static long double kDecimalExponentDouble[kDoubleMaxExponent -
                                          kDoubleMinExponent + 1];
static double kDecimalExponentFloat[kFloatMaxExponent -
                                    kFloatMinExponent + 1];

void InitExponents() {
  // For doubles the results from pow/exp10 cause issues for rounding
  // whole numbers which is a pretty obvious wart. Instead compute with repeated
  // multiplying/dividing by 10. For floats below 1 we also use nextafter()
  // to avoid those issues.
  //
  // No solution will be fully correct with floating points here, but this
  // prevents the obvious whole number warts.
  long double exponent = 1;
  kDecimalExponentDouble[-kDoubleMinExponent] = 1;
  // Compute exponents above 1.
  for (int i = 0; i < kDoubleMaxExponent; ++i) {
    exponent *= 10;
    kDecimalExponentDouble[1 + i - kDoubleMinExponent] = exponent;
  }
  exponent = 1;
  // Compute exponents below 1.
  for (int i = 0; i < -kDoubleMinExponent; ++i) {
    exponent /= 10;
    kDecimalExponentDouble[-kDoubleMinExponent - i - 1] = exponent;
  }

  // Copy the values to kFloatMinExponent, and fix the values that are too high.
  for (int i = kFloatMinExponent; i < kFloatMaxExponent + 1; ++i) {
    double& dest = kDecimalExponentFloat[i - kFloatMinExponent];
    const long double& src = kDecimalExponentDouble[i - kDoubleMinExponent];
    dest = static_cast<double>(src);
    if (dest > src) {
      // If the exponent is even ULP higher than its exact value, the result
      // of `x / exp` is lower than exact. This prevents roundtrip of expression
      // `trunc(x / exp) * x` for values that we expect to roundtrip, because
      // `x / exp` is just a bit lower than its exact integer value, and `trunc`
      // rounds down to the next integer value.
      // To avoid this, we make the exponent the nearest value less than the
      // exact value, and thus `x / exp` is ULP higher than its exact value, and
      // this makes sure `trunc(x / exp) * x` roundtrips when we need it to.
      dest = std::nextafter(dest, 0);
    }
  }
}

namespace {
static bool module_initialization_complete = []() {
  InitExponents();
  return true;
} ();
}  // namespace

// This function assumes that the FromType is same or wider than the ToType.
template <typename FromType, typename ToType>
static inline bool CastRounded(FromType in, ToType* out) {
  static_assert(std::is_floating_point<FromType>::value,
                "FromType must be floating point type");
  static_assert(std::is_floating_point<ToType>::value,
                "ToType must be floating point type");
  static_assert(std::numeric_limits<FromType>::lowest() <=
                        std::numeric_limits<ToType>::lowest() &&
                    std::numeric_limits<FromType>::max() >=
                        std::numeric_limits<ToType>::max(),
                "FromType must be wider than or equal to ToType");
  if ((in >= std::numeric_limits<ToType>::lowest() &&
       in <= std::numeric_limits<ToType>::max()) ||
      ABSL_PREDICT_FALSE(!std::isfinite(in))) {
    *out = static_cast<ToType>(in);
    return true;
  } else {
    return false;
  }
}

}  // anonymous namespace

template <>
bool RoundDecimal(double in, int64_t digits, double* out, absl::Status* error) {
  if (digits > -kDoubleMinExponent || !std::isfinite(in)) {
    *out = in;
    return true;
  }
  if (digits < -kDoubleMaxExponent) {
    *out = 0.0;
    return true;
  }
  digits = -digits;
  const long double exp = kDecimalExponentDouble[digits - kDoubleMinExponent];

  long double x = in / exp;
  if (std::numeric_limits<long double>::max_exponent <
          std::numeric_limits<double>::max_exponent * 2 &&
      ABSL_PREDICT_FALSE(!std::isfinite(x))) {
    // "in / exp" overflows when "in" is a big number and we are rounding
    // digits to the right of the decimal point (digits > 0). In that case the
    // rounded value would be the same as "in" and we just return
    // the value of "in".
    *out = in;
    return true;
  }
  long double result = roundl(x) * exp;
  // 'result' may overflow if the exponent size of long double is insufficient
  // for the rounded result.
  if (std::numeric_limits<long double>::max_exponent <
          std::numeric_limits<double>::max_exponent * 2 &&
      ABSL_PREDICT_FALSE(!std::isfinite(result) && std::isfinite(in))) {
    return internal::SetFloatingPointOverflow(
        absl::StrCat("ROUND(", in, ", ", digits, ")"), error);
  }
  // Converting the result from long double to double may overflow.
  if (sizeof(long double) > sizeof(double)) {
    if (!CastRounded<long double, double>(result, out)) {
      return internal::SetFloatingPointOverflow(
          absl::StrCat("ROUND(", in, ", ", digits, ")"), error);
    } else {
      return true;
    }
  } else {
    *out = static_cast<double>(result);
    return true;
  }
}

template <>
bool RoundDecimal(float in, int64_t digits, float* out, absl::Status* error) {
  static_assert(std::numeric_limits<double>::max_exponent >=
                std::numeric_limits<float>::max_exponent * 2 ,
                "double's exponent must be wider than float's");
  if (digits > -kFloatMinExponent || !std::isfinite(in)) {
    *out = in;
    return true;
  }
  if (digits < -kFloatMaxExponent) {
    *out = 0.0;
    return true;
  }
  digits = -digits;
  double exp = kDecimalExponentFloat[digits - kFloatMinExponent];
  // round(in / exp) * exp will never overflow due to the static_assert above.
  // Converting the result from double to float may overflow.
  if (!CastRounded<double, float>(round(in / exp) * exp, out)) {
    return internal::SetFloatingPointOverflow(
        absl::StrCat("ROUND(", in, ", ", digits, ")"), error);
  } else {
    return true;
  }
}
template <>
bool TruncDecimal(double in, int64_t digits, double* out, absl::Status* error) {
  if (digits > -kDoubleMinExponent || !std::isfinite(in)) {
    *out = in;
    return true;
  }
  if (digits < -kDoubleMaxExponent) {
    *out = 0.0;
    return true;
  }
  digits = -digits;
  const long double exp = kDecimalExponentDouble[digits - kDoubleMinExponent];

  long double x = in / exp;
  if (std::numeric_limits<long double>::max_exponent <
          std::numeric_limits<double>::max_exponent * 2 &&
      ABSL_PREDICT_FALSE(!std::isfinite(x))) {
    // "in / exp" overflows when "in" is a big number and we are rounding
    // digits to the right of the decimal point (digits > 0). In that case the
    // truncated value would be the same as "in" and we just return
    // the value of "in".
    *out = in;
    return true;
  }
  *out = truncl(x) * exp;
  // Because truncl always rounds towards zero, the output value is less than
  // the input, so we do not expect an overflow to occur here.
  return true;
}

template <>
bool TruncDecimal(float in, int64_t digits, float* out, absl::Status* error) {
  static_assert(std::numeric_limits<double>::max_exponent >=
                std::numeric_limits<float>::max_exponent * 2 ,
                "double's exponent must be wider than float's");
  if (digits > -kFloatMinExponent || !std::isfinite(in)) {
    *out = in;
    return true;
  }
  if (digits < -kFloatMaxExponent) {
    *out = 0.0;
    return true;
  }
  digits = -digits;
  double exp = kDecimalExponentFloat[digits - kFloatMinExponent];
  *out = trunc(in / exp) * exp;
  // trunc(in / exp) * exp will never overflow due to the static_assert above.
  // Because truncl always rounds towards zero, the output value is less than
  // the input, so we do not expect an overflow to occur here.
  return true;
}

double Pi() { return M_PI; }

template <>
bool Radians(double in, double* out, absl::Status* error) {
  static const double value_pi_over_180 = M_PI / 180.0;
  *out = in * value_pi_over_180;
  return internal::CheckFloatingPointError("RADIANS", in, *out, error);
}

template <>
bool Degrees(double in, double* out, absl::Status* error) {
  static const double value_180_over_pi = 180.0 / M_PI;
  *out = in * value_180_over_pi;
  return internal::CheckFloatingPointError("DEGREES", in, *out, error);
}

namespace {
template <typename T>
inline bool SetNumericResultOrError(const absl::StatusOr<T>& status_or_numeric,
                                    T* out, absl::Status* error) {
  if (ABSL_PREDICT_TRUE(status_or_numeric.ok())) {
    *out = status_or_numeric.value();
    return true;
  }
  error->Update(status_or_numeric.status());
  return false;
}
}  // namespace

template <>
bool Round(NumericValue in, NumericValue *out, absl::Status* error) {
  return SetNumericResultOrError(in.Round(0), out, error);
}

template <>
bool RoundDecimal(NumericValue in, int64_t digits, NumericValue* out,
                  absl::Status* error) {
  return SetNumericResultOrError(in.Round(digits), out, error);
}

template <>
bool RoundDecimalWithRoundingMode(NumericValue in, int64_t digits,
                                  RoundingMode rounding_mode, NumericValue* out,
                                  absl::Status* error) {
  if (rounding_mode == RoundingMode::ROUND_HALF_EVEN) {
    return SetNumericResultOrError(in.Round(digits, true), out, error);
  } else {
    return SetNumericResultOrError(in.Round(digits, false), out, error);
  }
}

template <>
bool Trunc(NumericValue in, NumericValue* out, absl::Status* error) {
  *out = in.Trunc(0);
  return true;
}

template <>
bool TruncDecimal(NumericValue in, int64_t digits, NumericValue* out,
                  absl::Status* error) {
  *out = in.Trunc(digits);
  return true;
}

template <>
bool Ceil(NumericValue in, NumericValue *out, absl::Status* error) {
  return SetNumericResultOrError(in.Ceiling(), out, error);
}

template <>
bool Floor(NumericValue in, NumericValue *out, absl::Status* error) {
  return SetNumericResultOrError(in.Floor(), out, error);
}

template <>
bool Sqrt(NumericValue in, NumericValue *out, absl::Status* error) {
  return SetNumericResultOrError(in.Sqrt(), out, error);
}

template <>
bool Cbrt(NumericValue in, NumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Cbrt(), out, error);
}

template <>
bool Pow(NumericValue in1, NumericValue in2, NumericValue* out,
         absl::Status* error) {
  return SetNumericResultOrError(in1.Power(in2), out, error);
}

template <>
bool Exp(NumericValue in, NumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Exp(), out, error);
}

template <>
bool NaturalLogarithm(NumericValue in, NumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Ln(), out, error);
}

template <>
bool DecimalLogarithm(NumericValue in, NumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Log10(), out, error);
}

template <>
bool Logarithm(NumericValue in1, NumericValue in2, NumericValue* out,
               absl::Status* error) {
  return SetNumericResultOrError(in1.Log(in2), out, error);
}

NumericValue Pi_Numeric() {
  return *NumericValue::FromPackedInt(3141592654ULL);
}

template <>
bool Radians(NumericValue in, NumericValue* out, absl::Status* error) {
  // Represents the 128-bit numerator 95024763027997044254193810947721847404
  // which is approximately 2^132 * (pi / 180)
  constexpr FixedInt<64, 2> scaled_pi_over_180 = FixedInt<64, 2>(
      std::array<uint64_t, 2>{0x762FB374A42E26DULL, 0x477D1A894A74E457ULL});
  constexpr uint N = 132;
  const auto status_or_numeric =
      in.MultiplyAndDivideByPowerOfTwo(scaled_pi_over_180, N);
  if (!status_or_numeric.ok()) {
    return internal::SetFloatingPointOverflow(
        absl::StrCat("RADIANS(", in.ToString(), ")"), error);
  }
  *out = status_or_numeric.value();
  return true;
}

template <>
bool Degrees(NumericValue in, NumericValue* out, absl::Status* error) {
  // Represents the number (180 / pi) * 2^121
  constexpr FixedInt<64, 2> scaled_180_over_pi = FixedInt<64, 2>(
      std::array<uint64_t, 2>{0x854BA9BFA0692BECULL, 0x729770698F07DEE1ULL});
  constexpr uint N = 121;
  const auto status_or_numeric =
      in.MultiplyAndDivideByPowerOfTwo(scaled_180_over_pi, N);
  if (!status_or_numeric.ok()) {
    return internal::SetFloatingPointOverflow(
        absl::StrCat("DEGREES(", in.ToString(), ")"), error);
  }
  *out = status_or_numeric.value();
  return true;
}

template <>
bool Ceil(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Ceiling(), out, error);
}

template <>
bool Floor(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Floor(), out, error);
}

template <>
bool Round(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Round(0), out, error);
}

template <>
bool RoundDecimal(BigNumericValue in, int64_t digits, BigNumericValue* out,
                  absl::Status* error) {
  return SetNumericResultOrError(in.Round(digits), out, error);
}
template <>
bool RoundDecimalWithRoundingMode(BigNumericValue in, int64_t digits,
                                  RoundingMode rounding_mode,
                                  BigNumericValue* out, absl::Status* error) {
  if (rounding_mode == RoundingMode::ROUND_HALF_EVEN) {
    return SetNumericResultOrError(in.Round(digits, true), out, error);
  } else {
    return SetNumericResultOrError(in.Round(digits, false), out, error);
  }
}

template <>
bool Trunc(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  *out = in.Trunc(0);
  return true;
}

template <>
bool TruncDecimal(BigNumericValue in, int64_t digits, BigNumericValue* out,
                  absl::Status* error) {
  *out = in.Trunc(digits);
  return true;
}

template <>
bool Sqrt(BigNumericValue in, BigNumericValue *out, absl::Status* error) {
  return SetNumericResultOrError(in.Sqrt(), out, error);
}

template <>
bool Cbrt(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Cbrt(), out, error);
}

template <>
bool Pow(BigNumericValue in1, BigNumericValue in2, BigNumericValue* out,
         absl::Status* error) {
  return SetNumericResultOrError(in1.Power(in2), out, error);
}

template <>
bool Exp(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  return SetNumericResultOrError(in.Exp(), out, error);
}

template <>
bool NaturalLogarithm(BigNumericValue in, BigNumericValue* out,
                      absl::Status* error) {
  return SetNumericResultOrError(in.Ln(), out, error);
}

template <>
bool DecimalLogarithm(BigNumericValue in, BigNumericValue* out,
                      absl::Status* error) {
  return SetNumericResultOrError(in.Log10(), out, error);
}

template <>
bool Logarithm(BigNumericValue in1, BigNumericValue in2, BigNumericValue* out,
               absl::Status* error) {
  return SetNumericResultOrError(in1.Log(in2), out, error);
}

BigNumericValue Pi_BigNumeric() {
  constexpr uint64_t lo = 0xAD0D16E77D576624ULL;
  constexpr uint64_t hi = 0xEC58DFA74641AF52ULL;
  return BigNumericValue::FromPackedLittleEndianArray({lo, hi, 0ULL, 0ULL});
}

template <>
bool Radians(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  // A 256-bit integer representing (pi / 180) * 2^260
  constexpr FixedInt<64, 4> scaled_pi_over_180 =
      FixedInt<64, 4>(std::array<uint64_t, 4>({
          0x728154DA64A64289ULL,
          0x805BD77A80DAF35CULL,
          0x0762FB374A42E26CULL,
          0x477D1A894A74E457ULL,
      }));
  constexpr int N = 260;

  const auto status_or_numeric =
      in.MultiplyAndDivideByPowerOfTwo(scaled_pi_over_180, N);
  if (!status_or_numeric.ok()) {
    return internal::SetFloatingPointOverflow(
        absl::StrCat("RADIANS(", in.ToString(), ")"), error);
  }
  *out = status_or_numeric.value();
  return true;
}

template <>
bool Degrees(BigNumericValue in, BigNumericValue* out, absl::Status* error) {
  constexpr FixedInt<64, 4> scaled_180_over_pi =
      FixedInt<64, 4>(std::array<uint64_t, 4>({
          0x66D13A14D89C06C9ULL,
          0x9A41512FBE5F816EULL,
          0x854BA9BFA0692BEBULL,
          0x729770698F07DEE1ULL,
      }));
  constexpr int N = 249;

  const auto status_or_numeric =
      in.MultiplyAndDivideByPowerOfTwo(scaled_180_over_pi, N);
  if (!status_or_numeric.ok()) {
    return internal::SetFloatingPointOverflow(
        absl::StrCat("DEGREES(", in.ToString(), ")"), error);
  }
  *out = status_or_numeric.value();
  return true;
}

}  // namespace functions
}  // namespace googlesql
