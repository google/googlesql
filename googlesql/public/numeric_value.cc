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

#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/numeric_constants.h"
#include "googlesql/public/numeric_parser.h"
#include "absl/base/optimization.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/mathutil.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
using FormatFlag = NumericValue::FormatSpec::Flag;

constexpr uint8_t kGroupSize = 3;
constexpr char kGroupChar = ',';
enum RoundingMode { kTrunc, kRoundHalfAwayFromZero, kRoundHalfEven };

constexpr FixedUint<64, 1> NumericScalingFactorSquared() {
  return FixedUint<64, 1>(NumericValue::kScalingFactor *
                          NumericValue::kScalingFactor);
}

constexpr FixedUint<64, 4> BigNumericScalingFactorSquared() {
  return FixedUint<64, 4>(std::array<uint64_t, 4>{0ULL, 0x7775a5f171951000ULL,
                                                  0x0764b4abe8652979ULL,
                                                  0x161bcca7119915b5ULL});
}

template <int n>
FixedInt<64, n * 2> GetScaledCovarianceNumerator(
    const FixedInt<64, n>& sum_x, const FixedInt<64, n>& sum_y,
    const FixedInt<64, n * 2 - 1>& sum_product, uint64_t count) {
  FixedInt<64, n * 2> numerator(sum_product);
  numerator *= count;
  numerator -= ExtendAndMultiply(sum_x, sum_y);
  return numerator;
}

template <int n, int m>
double Covariance(const FixedInt<64, n>& sum_x, const FixedInt<64, n>& sum_y,
                  const FixedInt<64, n * 2 - 1>& sum_product,
                  const FixedUint<64, m>& scaling_factor_square, uint64_t count,
                  uint64_t count_offset) {
  FixedInt<64, n* 2> numerator =
      GetScaledCovarianceNumerator(sum_x, sum_y, sum_product, count);
  FixedUint<64, m + 2> denominator(scaling_factor_square);
  denominator *= count;
  denominator *= (count - count_offset);
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

template <int n>
inline void SerializeFixedInt(std::string* dest, const FixedInt<64, n>& num) {
  num.SerializeToBytes(dest);
}

template <int n1, int... n>
inline void SerializeFixedInt(std::string* dest, const FixedInt<64, n1>& num1,
                              const FixedInt<64, n>&... num) {
  static_assert(sizeof(num1) < 128);
  size_t old_size = dest->size();
  dest->push_back('\0');  // add a place holder for size
  num1.SerializeToBytes(dest);
  ABSL_DCHECK_LE(dest->size() - old_size, 128);
  (*dest)[old_size] = static_cast<char>(dest->size() - old_size - 1);
  SerializeFixedInt(dest, num...);
}

template <int n>
bool DeserializeFixedInt(absl::string_view bytes, FixedInt<64, n>* num) {
  return num->DeserializeFromBytes(bytes);
}

template <int n1, int... n>
bool DeserializeFixedInt(absl::string_view bytes, FixedInt<64, n1>* num1,
                         FixedInt<64, n>*... num) {
  if (!bytes.empty()) {
    int len = bytes[0];
    return len < bytes.size() - 1 &&
           num1->DeserializeFromBytes(bytes.substr(1, len)) &&
           DeserializeFixedInt(bytes.substr(len + 1), num...);
  }
  return false;
}

// Helper method for appending a decimal value to a string. This function
// assumes the value is not zero, and the FixedInt string has already been
// appended to output. This function adds the decimal point and adjusts the
// leading and trailing zeros.
// Examples:
// (1, 9, 0, false, "-123") -> "-0.000000123"
// (1, 9, 0, false, "-123456789") -> "-0.123456789"
// (1, 9, 0, false, "-1234567890") -> "-1.23456789"
// (1, 9, 10, false, "-1234567890") -> "-1.2345678900"
// (1, 9, 0, false, "-1000000000") -> "-1"
// (1, 9, 0, true, "-1000000000") -> "-1."
// Returns the index of the decimal point (if decimal point was added) or
// output->size() otherwise.
size_t AddDecimalPointAndAdjustZeros(size_t first_digit_index, size_t scale,
                                     size_t min_num_fractional_digits,
                                     bool always_add_decimal_point,
                                     std::string* output) {
  size_t string_length = output->size();
  // Make a string_view that includes only the digits, so that find_last_not_of
  // does not search the substring before first_digit_index. This is for
  // performance instead of correctness. Note, std::string::find_last_not_of
  // doesn't have a signature that specifies the starting position.
  absl::string_view fixed_uint_str(*output);
  fixed_uint_str.remove_prefix(first_digit_index);
  size_t fixed_uint_length = fixed_uint_str.size();
  size_t zeros_to_truncate = 0;
  size_t num_fractional_digits = min_num_fractional_digits;
  if (min_num_fractional_digits < scale) {
    size_t num_trailing_zeros =
        fixed_uint_length - fixed_uint_str.find_last_not_of('0') - 1;
    zeros_to_truncate =
        std::min(num_trailing_zeros, scale - min_num_fractional_digits);
    output->resize(string_length - zeros_to_truncate);
    num_fractional_digits = scale - zeros_to_truncate;
  } else {
    output->append(min_num_fractional_digits - scale, '0');
  }

  size_t decimal_point_index = output->size();
  if (fixed_uint_length < scale + 1) {
    // Add zeros and decimal point if smaller than 1.
    output->insert(first_digit_index, scale + 2 - fixed_uint_length, '0');
    decimal_point_index = first_digit_index + 1;
    (*output)[decimal_point_index] = '.';
  } else if ((num_fractional_digits != 0) || always_add_decimal_point) {
    decimal_point_index -= num_fractional_digits;
    output->insert(output->begin() + decimal_point_index, '.');
  }
  return decimal_point_index;
}

// PowersAsc<Word, first_value, base, size>() returns a std::array<Word, size>
// {first_value, first_value * base, ..., first_value * pow(base, size - 1)}.
template <typename Word, Word first_value, Word base, int size, typename... T>
constexpr std::array<Word, size> PowersAsc(T... v) {
  if constexpr (sizeof...(T) < size) {
    return PowersAsc<Word, first_value, base, size>(first_value, v * base...);
  } else {
    return std::array<Word, size>{v...};
  }
}

// PowersDesc<Word, last_value, base, size>() returns a std::array<Word, size>
// {last_value * pow(base, size - 1), last_value * pow(base, size - 2), ...,
//  last_value}.
template <typename Word, Word last_value, Word base, int size, typename... T>
constexpr std::array<Word, size> PowersDesc(T... v) {
  if constexpr (sizeof...(T) < size) {
    return PowersDesc<Word, last_value, base, size>(v * base..., last_value);
  } else {
    return std::array<Word, size>{v...};
  }
}

FixedUint<64, 4> UnsignedFloor(FixedUint<64, 4> value) {
  // Remove the decimal portion of the value by dividing by the
  // ScalingFactor(10^38) then multiplying correspondingly.
  // For efficiency, the division is split into two rounds of 10^19 and the
  // multiplcation into two rounds of 10^19.
  value /= std::integral_constant<uint64_t, internal::k1e19>();
  value /= std::integral_constant<uint64_t, internal::k1e19>();
  value *= internal::k1e19;
  value *= internal::k1e19;
  return value;
}

FixedUint<64, 4> UnsignedCeiling(FixedUint<64, 4> value) {
  value += FixedUint<64, 4>(BigNumericValue::kScalingFactor - 1);
  return UnsignedFloor(value);
}

template <int n>
void ShiftRightAndRound(uint num_bits, FixedUint<64, n>* value) {
  ABSL_DCHECK_GT(num_bits, 0);
  constexpr uint kNumBits = n * 64;
  ABSL_DCHECK_LT(num_bits, kNumBits);
  uint bit_idx = num_bits - 1;
  uint64_t round_up = (value->number()[bit_idx / 64] >> (bit_idx % 64)) & 1;
  *value >>= num_bits;
  *value += round_up;
}

// SignedBinaryFraction and UnsignedBinaryFraction represent a fraction
// (x / pow(2, kFractionalBits)), where x is a FixedInt<64, kNumWords>
// or FixedUint<64, kNumWords>. These 2 classes are designed for advanced
// functions such as EXP and LN where there is 100% precise algorithm.
// Binary scale is used to make the operators much faster than decimal scale.
template <int kNumWords, int kFractionalBits>
class SignedBinaryFraction;

template <int kNumWords, int kFractionalBits>
class UnsignedBinaryFraction {
 public:
  using SignedType = SignedBinaryFraction<kNumWords, kFractionalBits>;
  static_assert(kNumWords * 64 > kFractionalBits);
  UnsignedBinaryFraction() = default;
  explicit UnsignedBinaryFraction(uint64_t value) : value_(value) {
    value_ <<= kFractionalBits;
  }
  // Constructs an instance representing value * 2 ^ scale_bits.
  UnsignedBinaryFraction(uint64_t value, int scale_bits) : value_(value) {
    ABSL_DCHECK_GE(scale_bits, -kFractionalBits);
    int shift_bits = kFractionalBits + scale_bits;
    ABSL_DCHECK_LE(shift_bits, kNumWords * 64);
    ABSL_DCHECK(shift_bits + 64 <= kNumWords * 64 ||
           (value >> (shift_bits + 64 - kNumWords * 64)) == 0);
    value_ <<= shift_bits;
  }
  static UnsignedBinaryFraction FromScaledValue(
      const FixedUint<64, kNumWords>& src) {
    UnsignedBinaryFraction result;
    result.value_ = src;
    return result;
  }
  bool To(bool is_negative, NumericValue* output) const {
    FixedUint<64, 2> result_abs;
    if (ABSL_PREDICT_TRUE(MulDivByScale(
            value_, FixedUint<64, 1>(NumericValue::kScalingFactor),
            &result_abs))) {
      unsigned __int128 v = static_cast<unsigned __int128>(result_abs);
      if (ABSL_PREDICT_TRUE(v <= internal::kNumericMax)) {
        __int128 packed = static_cast<__int128>(is_negative ? -v : v);
        *output = NumericValue::FromPackedInt(packed).value();
        return true;
      }
    }
    return false;
  }
  bool To(bool is_negative, BigNumericValue* output) const {
    FixedUint<64, 4> result_abs;
    FixedInt<64, 4> result;
    if (ABSL_PREDICT_TRUE(MulDivByScale(
            value_, FixedUint<64, 2>(BigNumericValue::kScalingFactor),
            &result_abs)) &&
        ABSL_PREDICT_TRUE(result.SetSignAndAbs(is_negative, result_abs))) {
      *output = BigNumericValue::FromPackedLittleEndianArray(result.number());
      return true;
    }
    return false;
  }

  UnsignedBinaryFraction& operator*=(const UnsignedBinaryFraction& rhs) {
    FixedUint<64, kNumWords* 2> product = ExtendAndMultiply(value_, rhs.value_);
    ShiftRightAndRound(kFractionalBits, &product);
    value_ = FixedUint<64, kNumWords>(product);
    return *this;
  }
  // Similar to operator *=, but returns true iff no overflow.
  bool Multiply(const UnsignedBinaryFraction& rhs) {
    return this->MulDivByScale(value_, rhs.value_, &value_);
  }
  bool Inverse();
  template <int n>
  bool IntegerPower(FixedUint<64, n> exponent,
                    UnsignedBinaryFraction* output) const;
  bool FractionalPower(const SignedType& exponent,
                       UnsignedBinaryFraction* output) const;
  bool Ln(const UnsignedBinaryFraction& unit_of_last_precision,
          SignedType* output) const;
  bool Log10(const UnsignedBinaryFraction& unit_of_last_precision,
             SignedType* output) const;
  bool Log(const UnsignedBinaryFraction& base,
           const UnsignedBinaryFraction& unit_of_last_precision,
           SignedType* output) const;
  bool Sqrt(UnsignedBinaryFraction* output) const;
  bool Cbrt(const UnsignedBinaryFraction& unit_of_last_precision,
            UnsignedBinaryFraction* output) const;
  bool ApproximateCbrt(UnsignedBinaryFraction* output) const;

 private:
  friend SignedType;
  friend UnsignedBinaryFraction<kNumWords + 1, kFractionalBits>;
  friend UnsignedBinaryFraction<kNumWords + 2, kFractionalBits>;

  // Sets *output to lhs * rhs / pow(2, kFractionalBits), and returns
  // true iff there is no overflow.
  template <int n, int m>
  static bool MulDivByScale(const FixedUint<64, kNumWords>& lhs,
                            const FixedUint<64, n>& rhs,
                            FixedUint<64, m>* output);

  FixedUint<64, kNumWords> value_;
};

template <int kNumWords, int kFractionalBits>
template <int n, int m>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::MulDivByScale(
    const FixedUint<64, kNumWords>& lhs, const FixedUint<64, n>& rhs,
    FixedUint<64, m>* output) {
  FixedUint<64, kNumWords + n> product = ExtendAndMultiply(lhs, rhs);
  ShiftRightAndRound(kFractionalBits, &product);
  for (int i = m; i < kNumWords + n - kFractionalBits / 64; ++i) {
    if (ABSL_PREDICT_FALSE(product.number()[i] != 0)) {
      return false;
    }
  }
  *output = FixedUint<64, m>(product);
  return true;
}

template <int kNumWords, int kFractionalBits>
template <int n>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::IntegerPower(
    FixedUint<64, n> exponent, UnsignedBinaryFraction* output) const {
  UnsignedBinaryFraction power(*this);
  *output = UnsignedBinaryFraction(1);
  while (true) {
    if ((exponent.number()[0] & 1) != 0 &&
        ABSL_PREDICT_FALSE(!output->Multiply(power))) {
      return false;
    }
    exponent >>= 1;
    if (exponent.is_zero()) {
      return true;
    }
    if (ABSL_PREDICT_FALSE(!power.Multiply(power))) {
      return false;
    }
  }
}

// Algorithm:
// https://en.wikipedia.org/wiki/Methods_of_computing_square_roots#A_two-variable_iterative_method
template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::Sqrt(
    UnsignedBinaryFraction* output) const {
  if (value_.is_zero()) {
    *output = UnsignedBinaryFraction();
    return true;
  }
  if (value_ == UnsignedBinaryFraction(1).value_) {
    *output = UnsignedBinaryFraction(1);
    return true;
  }

  // The algorithm requires the input must be in (0, 3) and is optimal when the
  // input is around 1. Decompose the input as s * 2^2t where 0.5 <= s < 2.
  FixedUint<64, kNumWords> s(value_);
  uint msb_index = s.FindMSBSetNonZero();
  int p = msb_index - kFractionalBits;
  p = p % 2 == 0 ? p : p + 1;
  int t = p / 2;
  if (p < 0) {
    s <<= (-p);
  } else if (p > 0) {
    ShiftRightAndRound(p, &s);
  }

  // Compute a = SQRT(s).
  //   a(0) = s
  //   c(0) = s - 1
  //   a(n+1) = a(n) * (1 - c(n) / 2)
  //   c(n+1) = c(n) ^ 2 * (c(n) - 3) / 4.
  // To save some computations and make the values mostly non-negative,
  // instead of computing c(n), we compute d(n) = -c(n) / 2:
  //   d(0) = (1 - s) / 2
  //   a(n+1) = a(n) * (1 + d(n))
  //   d(n+1) = d(n) ^ 2 * (d(n) + 1.5).

  // Since a(0) = s < 2, its integer part takes at most 1 bit. It can be proven
  // that s * (1 + c(n)) = a(n) ^ 2, and for n >= 1, c(n) < 0, which means
  // a(n) < sqrt(s) for n >= 1. Therefore, the integer part of a(n) takes at
  // most 1 bit for all n. Giving another bit just for safety, we can reduce
  // the total number of words from 3 to 2 for NumericValue and from 6 to 4 for
  // BigNumericValue.
  constexpr int kReducedWords = (kFractionalBits + 2 + 63) / 64;
  // a(n) is always positive. d(n) is positive except for n = 0. Use
  // unsigned type, and handle d(0) specially.
  using T = UnsignedBinaryFraction<kReducedWords, kFractionalBits>;
  T a = T::FromScaledValue(FixedUint<64, kReducedWords>(s));
  const T kOne(1);
  const T kHalf(1, -1);
  FixedUint<64, kReducedWords> half_s(s);
  ShiftRightAndRound(1, &half_s);
  T tmp = kHalf;
  // If d is negative, it will have the bits in 2's complement. When computing
  // tmp = d + 1, it will get the correct result because d >= -0.5.
  bool d_negative = tmp.value_.SubtractOverflow(half_s);  // tmp = d = (1 - s)/2
  // d^2 cannot be done in 2's complement due to the use ExtendAndMultiply.
  T abs_d;
  if (d_negative) {
    abs_d.value_ -= tmp.value_;
  } else {
    abs_d = tmp;
  }
  do {
    // tmp = d + 1. If d < 0, it must be >= -0.5, and tmp will be > 0.
    tmp.value_ += kOne.value_;
    a *= tmp;                    // a *= (1 + d)
    tmp.value_ += kHalf.value_;  // tmp = d + 1.5
    // Compute Next value of d = tmp = (d + 1.5) * d ^ 2.
    // Since d will be close to zero, (d + 1.5) * d ^ 2 is more
    // precise than d ^ 2 * (d + 1.5).
    tmp *= abs_d;
    tmp *= abs_d;
    abs_d = tmp;  // d is always non-negative now, so abs_d = d.
  } while (!abs_d.value_.is_zero());

  // Final output = SQRT(s) * 2^t.
  output->value_ = FixedUint<64, kNumWords>(a.value_);
  if (t < 0) {
    ShiftRightAndRound(-t, &output->value_);
  } else if (t > 0) {
    output->value_ <<= t;
  }
  return true;
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::ApproximateCbrt(
    UnsignedBinaryFraction* output) const {
  // If value_ = r * 2^(3t) where r is in [0.25, 2) and t is an integer,
  // then (0.371 r + 0.58) * 2^t is a good estimate of CBRT(value_).

  FixedUint<64, kNumWords> r = value_;
  uint msb_index = r.FindMSBSetNonZero();
  int p = msb_index - kFractionalBits;
  int t = p > 0 ? (p + 2) / 3 : p / 3;  // so r in [0.25, 2)
  if (p < 0) {
    r <<= (-3 * t);
  } else if (p > 0) {
    ShiftRightAndRound(3 * t, &r);
  }

  static UnsignedBinaryFraction linear_factor(95, -8);  // approximately 0.371
  static UnsignedBinaryFraction constant_factor(148, -8);  // approximately 0.58
  FixedUint<64, kNumWords>& estimated_cbrt = output->value_;
  if (!ABSL_PREDICT_TRUE(
          this->MulDivByScale(linear_factor.value_, r, &estimated_cbrt))) {
    return false;
  }
  estimated_cbrt += constant_factor.value_;
  if (t < 0) {
    ShiftRightAndRound(-t, &estimated_cbrt);
  } else if (t > 0) {
    estimated_cbrt <<= t;
  }
  return true;
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::Cbrt(
    const UnsignedBinaryFraction& unit_of_last_precision,
    UnsignedBinaryFraction* output) const {
  // 0. Handle simple cases
  if (value_.is_zero()) {
    *output = UnsignedBinaryFraction();
    return true;
  }
  if (value_ == UnsignedBinaryFraction(1).value_) {
    *output = UnsignedBinaryFraction(1);
    return true;
  }

  // Newton's method: start with a good estimate y_1 and iterate
  // y_{n+1} = 2/3 * y_n + 1/3 * (a / y_n^2)

  // 1. Compute a good estimate
  if (ABSL_PREDICT_FALSE(!this->ApproximateCbrt(output))) {
    return false;
  }

  // 2. Compute the scaled value we will be taking the cube root of
  //
  // In order for (output.value_ * 2^-F)^3 = (value_ * 2^-F),
  //   we need output.value_^3 == value_ * 2^(2F) =: scaled_value
  constexpr int fraction_words = (2 * kFractionalBits + 63) / 64;
  // We want to be able to store the original value, plus twice the number of
  // fractional bits, since we are shifting up by 2 * kFractionalBits
  //
  // In particular, we can always store the scaled_value with n bits, and all
  // intermediate values are strictly smaller than scaled_value
  constexpr int n = kNumWords + fraction_words;
  FixedUint<64, n> scaled_value(value_);
  scaled_value <<= (2 * kFractionalBits);

  FixedUint<64, kNumWords>& y = output->value_;
  FixedInt<64, kNumWords> delta;
  do {
    // Remember the old value of y
    delta = FixedInt<64, kNumWords>(y);

    // The initial guess for y is within 3 (binary) orders of magnitude of the
    // true cube root, which uses around n/3 bits. We expect that all
    // intermediate values, like y^2 take around 2n/3 < n bits, so this
    // shouldn't overflow
    FixedUint<64, n> ratio(scaled_value);
    FixedUint<64, n> y_squared(y);
    y_squared *= FixedUint<64, n>(y);
    ratio.DivAndRoundAwayFromZero(y_squared);
    //  -> ratio == scaled_value / y^2

    y <<= 1;
    y += FixedUint<64, kNumWords>(ratio);
    y.DivAndRoundAwayFromZero(std::integral_constant<uint64_t, 3>());
    //   -> y == 1/3 ( 2y + ratio )

    // Compare the new value of y to the old one to compute delta: we exit when
    //   this is small enough
    delta -= FixedInt<64, kNumWords>(y);
  } while (delta.abs() >= unit_of_last_precision.value_);
  return true;
}

template <int kNumWords, int kFractionalBits>
class SignedBinaryFraction {
 public:
  using UnsignedType = UnsignedBinaryFraction<kNumWords, kFractionalBits>;
  SignedBinaryFraction() = default;
  explicit SignedBinaryFraction(const NumericValue& src) {
    FixedInt<64, 2> src_number(src.as_packed_int());
    constexpr int n = 2 + (kFractionalBits + 63) / 64;
    FixedUint<64, n> src_abs(src_number.abs());
    src_abs <<= kFractionalBits;
    src_abs.DivAndRoundAwayFromZero(NumericValue::kScalingFactor);
    static_assert(kNumWords * 64 - kFractionalBits >= 98);
    // max(src_abs) < 10^29 * 2^kFractionalBits
    // < 2^(97 + kFractionalBits)
    // <= 2^(kNumWords * 64 - 1)
    value_ = FixedInt<64, kNumWords>(src_abs);
    if (src_number.is_negative()) {
      value_ = -value_;
    }
  }
  explicit SignedBinaryFraction(const BigNumericValue& src) {
    FixedInt<64, 4> src_number(src.ToPackedLittleEndianArray());
    constexpr int n = 4 + (kFractionalBits + 63) / 64;
    FixedUint<64, n> src_abs(src_number.abs());
    src_abs <<= kFractionalBits;
    FixedInt<64, n - 1> result_abs(
        BigNumericValue::RemoveScalingFactor</* round = */ true>(src_abs));
    static_assert(kNumWords * 64 - kFractionalBits >= 130);
    // max(result_abs) = 2^(255 + kFractionalBits) / 10^38
    // < 2^(255 + kFractionalBits) / 2^126 = 2^(129 + kFractionalBits)
    // <= 2^(kNumWords * 64 - 1)
    value_ = FixedInt<64, kNumWords>(result_abs);
    if (src_number.is_negative()) {
      value_ = -value_;
    }
  }
  UnsignedType Abs() const {
    return UnsignedType::FromScaledValue(value_.abs());
  }
  bool Exp(UnsignedType* output) const;
  template <typename T>
  bool To(T* output) const {
    return Abs().To(value_.is_negative(), output);
  }
  bool Multiply(const SignedBinaryFraction& rhs) {
    FixedUint<64, kNumWords> result_abs;
    bool result_is_negative = value_.is_negative() != rhs.value_.is_negative();
    return UnsignedType::MulDivByScale(value_.abs(), rhs.value_.abs(),
                                       &result_abs) &&
           value_.SetSignAndAbs(result_is_negative, result_abs);
  }

 private:
  friend UnsignedType;

  FixedInt<64, kNumWords> value_;
};

template <int kNumWords, int kFractionalBits>
bool SignedBinaryFraction<kNumWords, kFractionalBits>::Exp(
    UnsignedType* output) const {
  *output = UnsignedType(1);
  if (value_.is_zero()) {
    return true;
  }
  // For faster convergence, here we are calculating:
  // e^x = e^(r*2^t) = (e^r)^(2^t), where r < 1/8 and t >= 0
  const bool r_is_negative = value_.is_negative();
  UnsignedType r_abs = Abs();
  uint msb_index = r_abs.value_.FindMSBSetNonZero();
  uint t = 0;
  if (msb_index > kFractionalBits - 4) {
    t = msb_index - (kFractionalBits - 4);
    ShiftRightAndRound(t, &r_abs.value_);
  }
  // e^r is calculating with Taylor Series:
  // e^r = 1 + r + (r^2)/2! + (r^3)/3! + (r^4)/4! + ...
  uint64_t n = 0;
  UnsignedType term(1);
  bool term_is_negative = false;
  while (true) {
    if (ABSL_PREDICT_FALSE(!term.Multiply(r_abs))) {
      return false;
    }
    n++;
    term.value_.DivAndRoundAwayFromZero(n);
    if (term.value_.is_zero()) {
      break;
    }
    term_is_negative ^= r_is_negative;
    if (term_is_negative) {
      output->value_ -= term.value_;
    } else {
      output->value_ += term.value_;
    }
  }

  for (uint i = 0; i < t; ++i) {
    if (ABSL_PREDICT_FALSE(!output->Multiply(*output))) {
      return false;
    }
  }
  return true;
}

// Returns x * ln(2) * pow(2, scale_bits). scale_bits cannot exceed 320.
FixedUint<64, 6> MultiplyByScaledLn2(uint64_t x, uint scale_bits) {
  // kScaledLn2 = ROUND(ln(2) * pow(2, 320)).
  static constexpr FixedUint<64, 5> kScaledLn2(std::array<uint64_t, 5>{
      0xe7b876206debac98, 0x8a0d175b8baafa2b, 0x40f343267298b62d,
      0xc9e3b39803f2f6af, 0xb17217f7d1cf79ab});
  ABSL_DCHECK_LE(scale_bits, 320);
  FixedUint<64, 6> result = ExtendAndMultiply(kScaledLn2, FixedUint<64, 1>(x));
  ShiftRightAndRound(320 - scale_bits, &result);
  return result;
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::Ln(
    const UnsignedBinaryFraction& unit_of_last_precision,
    SignedType* output) const {
  if (value_ == UnsignedBinaryFraction(1).value_) {
    *output = SignedType();
    return true;
  }
  // For the algorithm to converge faster, here we calculate
  // ln(a) = ln(r * 2^t) = ln(r) + t*ln(2), where 1 <= r < 2.
  FixedUint<64, kNumWords> r = value_;
  uint msb_index = r.FindMSBSetNonZero();
  int t = msb_index - kFractionalBits;
  if (t < 0) {
    r <<= (-t);
  } else if (t > 0) {
    ShiftRightAndRound(t, &r);
  }

  // The approximate value of Ln is calculated by Halley's method:
  // y(n+1) = yn + 2 * ( r - exp(yn) )/( r + exp(yn) )
  // When r ~= 1, ln(r) ~= r - 1, so initialize y0 = r - 1;
  output->value_ = FixedInt<64, kNumWords>(r);
  output->value_ -= (FixedInt<64, kNumWords>(1) <<= kFractionalBits);
  // The Hally's method has cubic convergence, it is expected to converge within
  // 6 iterations. Thus, to be safe we allow at most 12 iterations here.
  // Because r < 2, exp(r) < 8, and the scaled value is far lower
  // than the max value of UnsignedBinaryFraction. No need to check overflow in
  // the operators in the loop.
  for (int i = 0; i < 12; i++) {
    UnsignedBinaryFraction exp_y;
    if (ABSL_PREDICT_FALSE(!output->Exp(&exp_y))) {
      return false;
    }
    if (r == exp_y.value_) {
      break;
    }
    FixedInt<64, kNumWords> r_minus_exp_y(r);
    r_minus_exp_y -= FixedInt<64, kNumWords>(exp_y.value_);
    bool r_less_than_exp_y = r_minus_exp_y.is_negative();
    FixedUint<64, kNumWords> r_minus_exp_y_abs_times_2 = r_minus_exp_y.abs();
    r_minus_exp_y_abs_times_2 <<= 1;
    FixedUint<64, kNumWords> r_plus_exp_y(r);
    r_plus_exp_y += exp_y.value_;
    constexpr int n = kNumWords + (kFractionalBits + 63) / 64;
    FixedUint<64, n> ratio(r_minus_exp_y_abs_times_2);
    ratio <<= kFractionalBits;
    ratio.DivAndRoundAwayFromZero(FixedUint<64, n>(r_plus_exp_y));
    FixedUint<64, kNumWords> delta_abs(ratio);
    if (r_less_than_exp_y) {
      output->value_ -= FixedInt<64, kNumWords>(delta_abs);
    } else {
      output->value_ += FixedInt<64, kNumWords>(delta_abs);
    }
    if (delta_abs < unit_of_last_precision.value_) {
      break;
    }
  }

  if (t != 0) {
    FixedInt<64, kNumWords> offset_abs(
        MultiplyByScaledLn2(std::abs(t), kFractionalBits));
    if (t > 0) {
      output->value_ += offset_abs;
    } else {
      output->value_ -= offset_abs;
    }
  }
  return true;
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::Log10(
    const UnsignedBinaryFraction& unit_of_last_precision,
    SignedType* output) const {
  // Calculate log10(a) = ln(a)*(1/ln(10)) by changing base.
  SignedType ln;
  if (!ABSL_PREDICT_TRUE(Ln(unit_of_last_precision, &ln))) {
    return false;
  }
  bool result_is_negative = ln.value_.is_negative();
  // kInversedScaledLn2 = ROUND(1/ln(10) * pow(2, 320)).
  static constexpr FixedUint<64, 5> kScaledLn2(std::array<uint64_t, 5>{
      4224701343442500089ULL, 2098561575983469214ULL, 2265771312819785985ULL,
      11145799226051128857ULL, 8011319160293570762ULL});
  FixedUint<64, kNumWords + 5> result_abs =
      ExtendAndMultiply(kScaledLn2, ln.value_.abs());
  ShiftRightAndRound(320, &result_abs);
  // There is no need to check cast overflow as 1/ln10 ~= 0.4343
  return output->value_.SetSignAndAbs(result_is_negative,
                                      FixedUint<64, kNumWords>(result_abs));
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::Log(
    const UnsignedBinaryFraction& base,
    const UnsignedBinaryFraction& unit_of_last_precision,
    SignedType* output) const {
  // Calculate log_b(a) = ln(a)/ln(b) by changing base.
  SignedType ln_a;
  SignedType ln_b;
  if (!ABSL_PREDICT_TRUE(Ln(unit_of_last_precision, &ln_a)) ||
      !ABSL_PREDICT_TRUE(base.Ln(unit_of_last_precision, &ln_b)) ||
      ABSL_PREDICT_FALSE(ln_b.value_.is_zero())) {
    return false;
  }
  bool result_is_negative =
      ln_a.value_.is_negative() != ln_b.value_.is_negative();
  constexpr int n = kNumWords + (kFractionalBits + 63) / 64;
  FixedUint<64, n> result_abs(ln_a.value_.abs());
  result_abs <<= kFractionalBits;
  result_abs.DivAndRoundAwayFromZero(FixedUint<64, n>(ln_b.value_.abs()));
  for (int i = kNumWords; i < n; ++i) {
    if (ABSL_PREDICT_FALSE(result_abs.number()[i] != 0)) {
      return false;
    }
  }
  return output->value_.SetSignAndAbs(result_is_negative,
                                      FixedUint<64, kNumWords>(result_abs));
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::FractionalPower(
    const SignedType& exponent, UnsignedBinaryFraction* output) const {
  // max_error_bits will be used in LN iteration ending condition.
  // pow(2, 4 - kFractionalBits) is chosen here to provide enough
  // precision, and also provide a loose range to avoid precision lost in
  // exp(yn) affecting the convergence.
  UnsignedBinaryFraction unit_of_last_precision(1, 4 - kFractionalBits);
  SignedType ln;
  // Here pow(x,y) is calculated as x^y=exp(y*ln(x))
  return (ABSL_PREDICT_TRUE(Ln(unit_of_last_precision, &ln)) &&
          ABSL_PREDICT_TRUE(ln.Multiply(exponent)) &&
          ABSL_PREDICT_TRUE(ln.Exp(output)));
}

template <int kNumWords, int kFractionalBits>
bool UnsignedBinaryFraction<kNumWords, kFractionalBits>::Inverse() {
  if (value_.is_zero()) {
    return false;
  }
  constexpr int n = std::max(kNumWords, kFractionalBits / 32 + 1);
  FixedUint<64, n> result(uint64_t{1});
  result <<= (kFractionalBits * 2);
  result.DivAndRoundAwayFromZero(FixedUint<64, n>(value_));
  for (int i = kNumWords; i < n; ++i) {
    if (ABSL_PREDICT_FALSE(result.number()[i] != 0)) {
      return false;
    }
  }
  value_ = FixedUint<64, kNumWords>(result);
  return true;
}

template <int kNumWords, int kMaxIntegerWords, int kBinaryFractionWords,
          int kBinaryFractionalBits, typename T>
absl::StatusOr<T> PowerInternal(const T& base, const T& exp) {
  constexpr absl::string_view type_name =
      std::is_same_v<T, BigNumericValue> ? "BIGNUMERIC" : "numeric";
  // For the cases where POW(base, exy) is equivalent as multiplication or
  // division of at most 2 T values, avoid conversion to
  // SignedBinaryFraction.
  if (exp == T(2)) {  // most common use case
    auto status_or_result = base.Multiply(base);
    if (ABSL_PREDICT_TRUE(status_or_result.ok())) {
      return status_or_result;
    }
    return MakeEvalError() << type_name << " overflow";
  }
  if (exp == T()) {
    return T(1);
  }
  if (exp == T(1)) {
    return base;
  }

  if (base == T()) {
    // An attempt to raise zero to a negative power results in division by zero.
    if (exp.Sign() < 0) {
      return MakeEvalError() << "division by zero";
    }
    // Otherwise zero raised to any power is still zero.
    return T();
  }
  if (exp == T(-1)) {
    return T(1).Divide(base);
  }

  FixedUint<64, kNumWords> extended_abs_integer_exp;
  FixedUint<64, kNumWords> extended_abs_fract_exp;
  FixedInt<64, kNumWords>(exp.ToPackedLittleEndianArray())
      .abs()
      .DivMod(FixedUint<64, kNumWords>(T::kScalingFactor),
              &extended_abs_integer_exp, &extended_abs_fract_exp);
  FixedUint<64, kMaxIntegerWords> abs_integer_exp(extended_abs_integer_exp);
  __int128 scaled_fract_exp =
      static_cast<unsigned __int128>(extended_abs_fract_exp);
  if (exp.Sign() < 0) {
    scaled_fract_exp = -scaled_fract_exp;
  }
  bool result_is_negative = false;
  if (base.Sign() < 0) {
    if (scaled_fract_exp != 0) {
      return MakeEvalError() << "Negative " << absl::AsciiStrToUpper(type_name)
                             << " value cannot be raised to a fractional power";
    }
    result_is_negative = (abs_integer_exp.number()[0] & 1) != 0;
  }
  using UnsignedFraction =
      UnsignedBinaryFraction<kBinaryFractionWords, kBinaryFractionalBits>;
  using SignedFraction =
      SignedBinaryFraction<kBinaryFractionWords, kBinaryFractionalBits>;
  UnsignedFraction base_binary_frac = SignedFraction(base).Abs();
  UnsignedFraction result;
  if (!abs_integer_exp.is_zero()) {
    if (exp.Sign() >= 0) {
      if (!base_binary_frac.IntegerPower(abs_integer_exp, &result)) {
        return MakeEvalError() << type_name << " overflow";
      }
    } else {
      FixedUint<64, kNumWords> value_abs =
          FixedInt<64, kNumWords>(base.ToPackedLittleEndianArray()).abs();
      if (value_abs > FixedUint<64, kNumWords>(T::kScalingFactor)) {
        // If the exponent is negative and value_abs is > 1, then we compute
        // 1 / (value_abs ^ (-integer_exp)) for integer part.
        if (!base_binary_frac.IntegerPower(abs_integer_exp, &result)) {
          return T();
        }
        if (!result.Inverse()) {
          return googlesql_base::InternalErrorBuilder()
                 << "Inverse of a value greater than 1 should not fail.";
        }
      } else {
        // If the exponent is negative and value_abs is < 1, then we compute
        // (1 / value_abs) ^ (-integer_exp).
        // Instead of calling base.Inverse(), we combine the conversion to
        // binary scale and the inversion for better precision.
        FixedUint<64, kBinaryFractionWords> scaled_inverse(T::kScalingFactor);
        scaled_inverse <<= kBinaryFractionalBits;
        scaled_inverse.DivAndRoundAwayFromZero(
            FixedUint<64, kBinaryFractionWords>(value_abs));
        base_binary_frac = UnsignedFraction::FromScaledValue(scaled_inverse);
        if (!base_binary_frac.IntegerPower(abs_integer_exp, &result)) {
          return MakeEvalError() << type_name << " overflow";
        }
        scaled_fract_exp = -scaled_fract_exp;
      }
    }
  }
  if (scaled_fract_exp != 0) {
    UnsignedFraction frac_result;
    if (ABSL_PREDICT_FALSE(!base_binary_frac.FractionalPower(
            SignedFraction(T::FromScaledValue(scaled_fract_exp)),
            &frac_result))) {
      return googlesql_base::InternalErrorBuilder()
             << "Fractional Power should never "
                "overflow with exponent less than 1";
    }
    if (abs_integer_exp.is_zero()) {
      result = frac_result;
    } else if (ABSL_PREDICT_FALSE(!result.Multiply(frac_result))) {
      return MakeEvalError() << type_name << " overflow";
    }
  }

  T output;
  if (ABSL_PREDICT_TRUE(result.To(result_is_negative, &output))) {
    return output;
  }
  return MakeEvalError() << type_name << " overflow";
}

}  // namespace

// Defined only for testing UnsignedBinaryFraction::ApproximateCbrt.
// The input should always be positive, matching the production code path.
absl::StatusOr<NumericValue> TestOnlyNumericApproximateCbrt(NumericValue in) {
  UnsignedBinaryFraction<3, 94> value = SignedBinaryFraction<3, 94>(in).Abs();
  UnsignedBinaryFraction<3, 94> cbrt;
  NumericValue result;
  GOOGLESQL_RET_CHECK(value.ApproximateCbrt(&cbrt) &&
            cbrt.To(/*is_negative=*/false, &result))
      << "ApproximateCbrt should never overflow";
  return result;
}

// Defined only for testing UnsignedBinaryFraction::ApproximateCbrt.
// The input should always be positive, matching the production code path.
absl::StatusOr<BigNumericValue> TestOnlyBigNumericApproximateCbrt(
    BigNumericValue in) {
  UnsignedBinaryFraction<6, 254> value = SignedBinaryFraction<6, 254>(in).Abs();
  UnsignedBinaryFraction<6, 254> cbrt;
  BigNumericValue result;
  GOOGLESQL_RET_CHECK(value.ApproximateCbrt(&cbrt) &&
            cbrt.To(/*is_negative=*/false, &result))
      << "ApproximateCbrt should never overflow";
  return result;
}

absl::StatusOr<NumericValue> NumericValue::FromStringStrict(
    absl::string_view str) {
  return FromStringInternal<internal::DigitTrimMode::kError>(str, 9);
}

absl::StatusOr<NumericValue> NumericValue::FromString(absl::string_view str) {
  return FromStringInternal<
      internal::DigitTrimMode::kTrimRoundHalfAwayFromZero>(str, 9);
}

absl::StatusOr<NumericValue> NumericValue::FromStringWithRounding(
    absl::string_view str, int64_t decimal_places, bool round_half_even) {
  if (round_half_even) {
    return FromStringInternal<internal::DigitTrimMode::kTrimRoundHalfEven>(
        str, decimal_places);
  } else {
    return FromStringInternal<
        internal::DigitTrimMode::kTrimRoundHalfAwayFromZero>(str,
                                                             decimal_places);
  }
}

size_t NumericValue::HashCode() const {
  return absl::Hash<NumericValue>()(*this);
}

void NumericValue::AppendToString(std::string* output) const {
  if (value_.is_zero()) {
    output->push_back('0');
    return;
  }
  size_t old_size = output->size();
  value_.AppendToString(output);
  size_t first_digit_index = old_size + value_.is_negative();
  AddDecimalPointAndAdjustZeros(first_digit_index, kMaxFractionalDigits, 0,
                                false, output);
}

// Parses a textual representation of a NUMERIC value. Returns an error if the
// given string cannot be parsed as a number or if the textual numeric value
// exceeds NUMERIC precision. If 'is_strict' is true then the function will
// return an error if there are more that 9 digits in the fractional part,
// otherwise the number will be rounded to contain no more than 9 fractional
// digits.
template <internal::DigitTrimMode trim_mode>
absl::StatusOr<NumericValue> NumericValue::FromStringInternal(
    absl::string_view str, int64_t decimal_places) {
  constexpr uint8_t word_count = 2;
  FixedPointRepresentation<word_count> parsed;
  absl::Status parse_status =
      ParseNumericWithRounding<trim_mode>(str, decimal_places, parsed);
  if (ABSL_PREDICT_TRUE(parse_status.ok())) {
    auto number_or_status = FromFixedUint(parsed.output, parsed.is_negative);
    if (number_or_status.ok()) {
      return number_or_status;
    }
  }
  return MakeEvalError() << "Invalid NUMERIC value: " << str;
}

double NumericValue::ToDouble() const {
  __int128 value = as_packed_int();
  if (value == 0) {
    return 0;
  }
  using uint128 = unsigned __int128;
  uint128 abs_value = static_cast<uint128>(value_.abs());
  // binary_scaling_factor must be a power of 2, so that the division by it
  // never loses any precision.
  double binary_scaling_factor = 1;
  // Make sure abs_value has at least 96 significant bits, so that after
  // dividing by kScalingFactor, it has at least 64 significant bits
  // before conversion to double.
  if (abs_value < (uint128{1} << 96)) {
    if (abs_value >= (uint128{1} << 64)) {
      abs_value <<= 32;
      binary_scaling_factor = static_cast<double>(uint128{1} << 32);
    } else if (abs_value >= (uint128{1} << 32)) {
      abs_value <<= 64;
      binary_scaling_factor = static_cast<double>(uint128{1} << 64);
    } else {
      abs_value <<= 96;
      binary_scaling_factor = static_cast<double>(uint128{1} << 96);
    }
  }
  // FixedUint<64, 2> / std::integral_constant<uint64, *> is much faster than
  // uint128 / uint64.
  FixedUint<64, 2> tmp(abs_value);
  uint64_t remainder;
  tmp.DivMod(NumericValue::kScalingFactor, &tmp, &remainder);
  std::array<uint64_t, 2> n = tmp.number();
  // If the remainder is not 0, set the least significant bit to 1 so that the
  // round-to-even in static_cast<double>() will not treat the value as a tie
  // between 2 nearest double values.
  n[0] |= (remainder != 0);
  double result =
      static_cast<double>(FixedUint<64, 2>(n)) / binary_scaling_factor;
  return value >= 0 ? result : -result;
}

inline unsigned __int128 ScaleMantissa(uint64_t mantissa, uint64_t scale) {
  return static_cast<unsigned __int128>(mantissa) * scale;
}

inline FixedUint<64, 4> ScaleMantissa(uint64_t mantissa,
                                      unsigned __int128 scale) {
  return ExtendAndMultiply(FixedUint<64, 2>(mantissa), FixedUint<64, 2>(scale));
}

template <typename S, typename T>
bool ScaleAndRoundAwayFromZero(S scale, double value, T* result) {
  if (value == 0) {
    *result = T();
    return true;
  }
  constexpr int kNumOutputBits = sizeof(T) * 8;
  googlesql_base::MathUtil::DoubleParts parts = googlesql_base::MathUtil::Decompose(value);
  ABSL_DCHECK_NE(parts.mantissa, 0) << value;
  if (parts.exponent <= -kNumOutputBits) {
    *result = T();
    return true;
  }
  // Because mantissa != 0, parts.exponent >= kNumOutputBits - 1 would mean that
  // (abs_mantissa * scale) << parts.exponent will exceed kNumOutputBits - 1
  // bits. Note, the most significant bit in abs_result cannot be set, or the
  // sign of *result will be wrong. We do not need to exempt the special case
  // of 1 << (kNumOutputBits - 1) which might keep the sign correct, because
  // <scale> is not a power of 2 and thus abs_result is never equal to
  // 1 << (kNumOutputBits - 1).
  if (ABSL_PREDICT_FALSE(parts.exponent >= kNumOutputBits - 1)) {
    return false;
  }
  bool negative = parts.mantissa < 0;
  uint64_t abs_mantissa =
      negative ? -static_cast<uint64_t>(parts.mantissa) : parts.mantissa;
  auto abs_result = ScaleMantissa(abs_mantissa, scale);
  static_assert(sizeof(abs_result) == sizeof(T));
  if (parts.exponent < 0) {
    abs_result >>= -1 - parts.exponent;
    abs_result += uint64_t{1};  // round away from zero
    abs_result >>= 1;
  } else if (parts.exponent > 0) {
    int msb_idx =
        FixedUint<64, kNumOutputBits / 64>(abs_result).FindMSBSetNonZero();
    if (ABSL_PREDICT_FALSE(msb_idx >= kNumOutputBits - 1 - parts.exponent)) {
      return false;
    }
    abs_result <<= parts.exponent;
  }
  static_assert(sizeof(T) > sizeof(S) + sizeof(uint64_t));
  // Because sizeof(T) is bigger than sizeof(S) + sizeof(uint64), the sign bit
  // of abs_result cannot be 1 when parts.exponent = 0. Same for the cases
  // where parts.exponent != 0. Therefore, we do not need to check overflow in
  // negation.
  T rv(abs_result);
  ABSL_DCHECK(rv >= T()) << value;
  *result = negative ? -rv : rv;
  return true;
}

absl::StatusOr<NumericValue> NumericValue::FromDouble(double value) {
  if (ABSL_PREDICT_FALSE(!std::isfinite(value))) {
    // This error message should be kept consistent with the error message found
    // in .../public/functions/convert.h.
    if (std::isnan(value)) {
      // Don't show the negative sign for -nan values.
      value = std::numeric_limits<double>::quiet_NaN();
    }
    return MakeEvalError() << "Illegal conversion of non-finite floating point "
                              "number to numeric: "
                           << value;
  }
  __int128 result = 0;
  if (ScaleAndRoundAwayFromZero(kScalingFactor, value, &result)) {
    absl::StatusOr<NumericValue> value_status = FromPackedInt(result);
    if (ABSL_PREDICT_TRUE(value_status.ok())) {
      return value_status;
    }
  }
  return MakeEvalError() << "numeric out of range: " << value;
}

absl::StatusOr<NumericValue> NumericValue::Multiply(NumericValue rh) const {
  bool negative = value_.is_negative();
  bool rh_negative = rh.value_.is_negative();
  FixedUint<64, 4> product = ExtendAndMultiply(value_.abs(), rh.value_.abs());

  // This value represents kNumericMax * kScalingFactor + kScalingFactor / 2.
  // At this value, <res> would be internal::kNumericMax + 1 and overflow.
  static constexpr FixedUint<64, 4> kOverflowThreshold(std::array<uint64_t, 4>{
      6450984253243169536ULL, 13015503840481697412ULL, 293873587ULL, 0ULL});
  if (ABSL_PREDICT_TRUE(product < kOverflowThreshold)) {
    FixedUint<64, 3> res(product);
    res += kScalingFactor / 2;
    res /= kScalingFactor;
    unsigned __int128 v = static_cast<unsigned __int128>(res);
    // We already checked the value range, so no need to call FromPackedInt.
    return NumericValue(
        static_cast<__int128>(negative == rh_negative ? v : -v));
  }
  return MakeEvalError() << "numeric overflow: " << ToString() << " * "
                         << rh.ToString();
}

absl::StatusOr<NumericValue> NumericValue::MultiplyAndDivideByPowerOfTwo(
    const FixedInt<64, 2>& multiplier, uint scale_bits) const {
  const bool result_is_negative =
      value_.is_negative() != multiplier.is_negative();
  FixedUint<64, 4> product = ExtendAndMultiply(value_.abs(), multiplier.abs());

  if (scale_bits > 0) {
    ShiftRightAndRound(scale_bits, &product);
  }

  std::array<uint64_t, 4> result_words = product.number();
  if (ABSL_PREDICT_FALSE(result_words[2] != 0 || result_words[3] != 0)) {
    return MakeEvalError() << "numeric overflow: " << ToString() << " * "
                           << multiplier.ToString() << " / pow(2, "
                           << scale_bits << ")";
  }

  __int128 result_128 =
      static_cast<__int128>(result_words[1]) << 64 | result_words[0];
  if (result_is_negative) result_128 *= -1;

  absl::StatusOr<NumericValue> result = NumericValue::FromPackedInt(result_128);
  if (ABSL_PREDICT_TRUE(result.ok())) {
    return result;
  }
  return MakeEvalError() << "numeric overflow: " << ToString() << " * "
                         << multiplier.ToString() << " / pow(2, " << scale_bits
                         << ")";
}

NumericValue NumericValue::Abs() const {
  if (value_.is_negative()) {
    NumericValue result = *this;
    // The result is expected to be within the valid range, ignore the
    // overflow signal.
    result.value_.NegateOverflow();
    return result;
  }
  return *this;
}

int NumericValue::Sign() const {
  return value_.is_negative() ? -1 : (value_.is_zero() ? 0 : 1);
}

absl::StatusOr<NumericValue> NumericValue::Power(NumericValue exp) const {
  auto res_or_status = PowerInternal<2, 2, 3, 94>(*this, exp);
  if (res_or_status.ok()) {
    return res_or_status;
  }
  return googlesql_base::StatusBuilder(res_or_status.status()).SetAppend()
         << ": POW(" << ToString() << ", " << exp.ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Exp() const {
  SignedBinaryFraction<3, 94> base(*this);
  UnsignedBinaryFraction<3, 94> exp;
  NumericValue result;
  if (ABSL_PREDICT_TRUE(base.Exp(&exp)) &&
      ABSL_PREDICT_TRUE(exp.To(false, &result))) {
    return result;
  }
  return MakeEvalError() << "numeric overflow: EXP(" << ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Ln() const {
  if (as_packed_int() <= 0) {
    return MakeEvalError() << "LN is undefined for zero or negative value: LN("
                           << ToString() << ")";
  }
  UnsignedBinaryFraction<3, 94> exp = SignedBinaryFraction<3, 94>(*this).Abs();
  SignedBinaryFraction<3, 94> ln;
  // unit_of_last_precision is set to pow(2, -34) ~= 5.8e-11 here. In the
  // implementation of Ln with Halley's method, computation will stop when the
  // delta of the iteration is less than unit_of_last_precision. Thus, 5.8e-11
  // is set up here to provide enough precision for NumericValue and avoid
  // unnecessary computation.
  UnsignedBinaryFraction<3, 94> unit_of_last_precision(1, -34);
  NumericValue result;
  if (ABSL_PREDICT_TRUE(exp.Ln(unit_of_last_precision, &ln)) &&
      ABSL_PREDICT_TRUE(ln.To(&result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "LN should never overflow: LN(" << ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Log10() const {
  if (as_packed_int() <= 0) {
    return MakeEvalError()
           << "LOG10 is undefined for zero or negative value: LOG10("
           << ToString() << ")";
  }
  UnsignedBinaryFraction<3, 94> exp = SignedBinaryFraction<3, 94>(*this).Abs();
  SignedBinaryFraction<3, 94> log10;
  // unit_of_last_precision will be used in LN iteration ending condition.
  // pow(2, -34) ~= 5.8e-11 is chosen here to provide enough precision and avoid
  // unnecessary computation.
  UnsignedBinaryFraction<3, 94> unit_of_last_precision(1, -34);
  NumericValue result;
  if (ABSL_PREDICT_TRUE(exp.Log10(unit_of_last_precision, &log10)) &&
      ABSL_PREDICT_TRUE(log10.To(&result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "LOG10 should never overflow: LOG10(" << ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Log(NumericValue base) const {
  if (as_packed_int() <= 0 || base.as_packed_int() <= 0 ||
      base == NumericValue(1)) {
    return MakeEvalError() << "LOG is undefined for zero or negative value, or "
                              "when base equals 1: "

                              "LOG("
                           << ToString() << ", " << base.ToString() << ")";
  }
  UnsignedBinaryFraction<3, 94> abs_value =
      SignedBinaryFraction<3, 94>(*this).Abs();
  UnsignedBinaryFraction<3, 94> abs_base =
      SignedBinaryFraction<3, 94>(base).Abs();
  SignedBinaryFraction<3, 94> log;
  // unit_of_last_precision will be used in LN iteration ending condition.
  // pow(2, -90) is chosen here to provide enough precision, and also provide a
  // loose range to avoid precision lost in division.
  UnsignedBinaryFraction<3, 94> unit_of_last_precision(1, -90);
  NumericValue result;
  if (ABSL_PREDICT_TRUE(
          abs_value.Log(abs_base, unit_of_last_precision, &log)) &&
      ABSL_PREDICT_TRUE(log.To(&result))) {
    return result;
  }
  // A theoretical max is
  // ABSL_LOG(99999999999999999999999999999.999999999, 1.000000001) ~=
  // 66774967730.214808679 and theoretical min is
  // ABSL_LOG(99999999999999999999999999999.999999999, 0.999999999) ~=
  // -66774967663.439840983
  return googlesql_base::InternalErrorBuilder()
         << "LOG(NumericValue, NumericValue) should never overflow: "

            "LOG("
         << ToString() << ", " << base.ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Sqrt() const {
  if (as_packed_int() < 0) {
    return MakeEvalError() << "SQRT is undefined for negative value: SQRT("
                           << ToString() << ")";
  }
  UnsignedBinaryFraction<3, 94> value =
      SignedBinaryFraction<3, 94>(*this).Abs();
  UnsignedBinaryFraction<3, 94> sqrt;
  NumericValue result;
  if (ABSL_PREDICT_TRUE(value.Sqrt(&sqrt)) &&
      ABSL_PREDICT_TRUE(sqrt.To(false, &result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "SQRT should never overflow: SQRT(" << ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Cbrt() const {
  const bool is_negative = value_.is_negative();
  UnsignedBinaryFraction<3, 94> value =
      SignedBinaryFraction<3, 94>(*this).Abs();
  UnsignedBinaryFraction<3, 94> cbrt;
  UnsignedBinaryFraction<3, 94> unit_of_last_precision(1, -34);
  NumericValue result;
  if (ABSL_PREDICT_TRUE(value.Cbrt(unit_of_last_precision, &cbrt)) &&
      ABSL_PREDICT_TRUE(cbrt.To(is_negative, &result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "CBRT should never overflow: CBRT(" << ToString() << ")";
}

namespace {

template <uint64_t divisor, RoundingMode rounding_mode>
inline unsigned __int128 RoundOrTruncConst64(unsigned __int128 dividend) {
  uint64_t remainder;
  googlesql::FixedUint<64, 2> quotient;
  FixedUint<64, 2>(dividend).DivMod(std::integral_constant<uint64_t, divisor>(),
                                    &quotient, &remainder);
  if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
    // If bankers rounding (round half even) is enabled, first step is to see
    // if we are halfway to the next value. We do that by seeing if the
    // remainder is exactly equal to the divisor / 2.
    // Example: divisor is 1000. remainder is 500. that would mean we were
    // exactly halfway between 0 and 1000.
    if ((divisor % 2 == 0) && (divisor >> 1) == remainder) {
      // Next we check whether the quotient is odd or even, since we will be
      // rounding to the nearest even.
      // If the quotient is odd then we will round up (to the nearest even).
      // If the quotient is even then we will round down (to the nearest even)
      // We find out if it's odd or even by bitwise & the least significant
      // piece of the quotient with 1
      if (quotient.number()[0] & 1) {
        // If the last bit is 1, the quotient is odd and we should round up
        return dividend + remainder;
      } else {
        // else if the last bit is 0, the quotient is even and we should round
        // down
        return dividend - remainder;
      }
    }
  }

  // If we're above half, and either rounding method is used, round up
  if ((remainder >= (divisor >> 1)) &&
      (rounding_mode != RoundingMode::kTrunc)) {
    return dividend + (divisor - remainder);
  }

  return dividend - remainder;
}

// Rounds or truncates this NUMERIC value to the given number of decimal
// places after the decimal point (or before the decimal point if
// 'decimal_places' is negative), and returns the packed integer. If
// 'round_away_from_zero' is used, then rounds the result away from zero, and
// the result might be out of the range of valid NumericValue. If
// RoundingMode::kTrunc, then the extra decimal places are discarded and the
// result is always in the valid range.
template <RoundingMode rounding_mode>
unsigned __int128 RoundOrTrunc(unsigned __int128 value,
                               int64_t decimal_places) {
  switch (decimal_places) {
    // Fast paths for some common values of the second argument.
    case 0:
      return RoundOrTruncConst64<internal::k1e9, rounding_mode>(value);
    case 1:
      return RoundOrTruncConst64<100000000, rounding_mode>(value);
    case 2:
      return RoundOrTruncConst64<10000000, rounding_mode>(value);
    case 3:
      return RoundOrTruncConst64<1000000, rounding_mode>(value);
    case 4:  // Format("%e", x) for ABS(x) in [100.0, 1000.0)
      return RoundOrTruncConst64<100000, rounding_mode>(value);
    case 5:  // Format("%e", x) for ABS(x) in [10.0, 100.0)
      return RoundOrTruncConst64<10000, rounding_mode>(value);
    case 6:  // Format("%f", *) and Format("%e", x) for ABS(x) in [1.0, 10.0)
      return RoundOrTruncConst64<1000, rounding_mode>(value);
    default: {
      if (decimal_places >= NumericValue::kMaxFractionalDigits) {
        // Rounding beyond the max number of supported fractional digits has no
        // effect.
        return value;
      }
      if (decimal_places < -NumericValue::kMaxIntegerDigits) {
        // Rounding (kMaxIntegerDigits + 1) digits away results in zero.
        // Rounding kMaxIntegerDigits digits away might result in overflow
        // instead of zero.
        return 0;
      }
      constexpr uint kMaxDigits =
          NumericValue::kMaxFractionalDigits + NumericValue::kMaxIntegerDigits;
      static constexpr std::array<__int128, kMaxDigits> kTruncFactors =
          PowersDesc<__int128, 10, 10, kMaxDigits>();
      unsigned __int128 trunc_factor =
          kTruncFactors[decimal_places + NumericValue::kMaxIntegerDigits];
      // First check if round to even is enabled and we're mid way to the next
      // value. Even if rounding to even is enabled, if we aren't half way
      // to the next value then continue to the round_away_from_zero default.
      if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
        FixedUint<64, 2> quotient;
        FixedUint<64, 2> remainder;
        FixedUint<64, 2>(value).DivMod(FixedUint<64, 2>(trunc_factor),
                                       &quotient, &remainder);
        if (remainder == FixedUint<64, 2>(trunc_factor >> 1)) {
          if (quotient.number()[0] & 1) {
            // previous digit is odd, so round up
            return value + static_cast<unsigned __int128>(remainder);
          } else {
            // previous digit is even, so round down
            return value - static_cast<unsigned __int128>(remainder);
          }
        }
      }
      if constexpr (rounding_mode != RoundingMode::kTrunc) {
        // The max result is < 1.5e38 < pow(2, 127); no need to check overflow.
        value += (trunc_factor >> 1);
      }
      value -= value % trunc_factor;
      return value;
    }
  }
}

inline void RoundInternal(
    FixedUint<64, 2>* input, int64_t decimal_places,
    RoundingMode rounding_mode = RoundingMode::kRoundHalfAwayFromZero) {
  if (rounding_mode == RoundingMode::kRoundHalfEven) {
    *input = FixedUint<64, 2>(RoundOrTrunc<RoundingMode::kRoundHalfEven>(
        static_cast<unsigned __int128>(*input), decimal_places));
  } else {
    *input =
        FixedUint<64, 2>(RoundOrTrunc<RoundingMode::kRoundHalfAwayFromZero>(
            static_cast<unsigned __int128>(*input), decimal_places));
  }
}
}  // namespace

absl::StatusOr<NumericValue> NumericValue::Round(int64_t decimal_places,
                                                 bool round_half_even) const {
  __int128 value = as_packed_int();
  if (value >= 0) {
    if (round_half_even) {
      value = RoundOrTrunc<RoundingMode::kRoundHalfEven>(value, decimal_places);
    } else {
      value = RoundOrTrunc<RoundingMode::kRoundHalfAwayFromZero>(
          value, decimal_places);
    }
    if (ABSL_PREDICT_TRUE(value <= internal::kNumericMax)) {
      return NumericValue(value);
    }
  } else {
    if (round_half_even) {
      value =
          RoundOrTrunc<RoundingMode::kRoundHalfEven>(-value, decimal_places);
    } else {
      value = RoundOrTrunc<RoundingMode::kRoundHalfAwayFromZero>(
          -value, decimal_places);
    }

    if (ABSL_PREDICT_TRUE(value <= internal::kNumericMax)) {
      return NumericValue(-value);
    }
  }
  return MakeEvalError() << "numeric overflow: ROUND(" << ToString() << ", "
                         << decimal_places << ")";
}

NumericValue NumericValue::Trunc(int64_t decimal_places) const {
  __int128 value = as_packed_int();
  if (value >= 0) {
    // TRUNC never overflows.
    return NumericValue(static_cast<__int128>(
        RoundOrTrunc<RoundingMode::kTrunc>(value, decimal_places)));
  }
  return NumericValue(static_cast<__int128>(
      -RoundOrTrunc<RoundingMode::kTrunc>(-value, decimal_places)));
}

absl::StatusOr<NumericValue> NumericValue::Ceiling() const {
  __int128 value = as_packed_int();
  int64_t fract_part = GetFractionalPart();
  value -= fract_part > 0 ? fract_part - static_cast<int64_t>(kScalingFactor)
                          : fract_part;
  auto res_status = NumericValue::FromPackedInt(value);
  if (res_status.ok()) {
    return res_status;
  }
  return MakeEvalError() << "numeric overflow: CEIL(" << ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Floor() const {
  __int128 value = as_packed_int();
  int64_t fract_part = GetFractionalPart();
  value -= fract_part < 0 ? fract_part + static_cast<int64_t>(kScalingFactor)
                          : fract_part;
  auto res_status = NumericValue::FromPackedInt(value);
  if (res_status.ok()) {
    return res_status;
  }
  return MakeEvalError() << "numeric overflow: FLOOR(" << ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Divide(NumericValue rh) const {
  const bool is_negative = value_.is_negative();
  const bool rh_is_negative = rh.value_.is_negative();

  if (ABSL_PREDICT_TRUE(!rh.value_.is_zero())) {
    FixedUint<64, 3> dividend(value_.abs());
    unsigned __int128 divisor = static_cast<unsigned __int128>(rh.value_.abs());

    // To preserve the scale of the result we need to multiply the dividend by
    // the scaling factor first.
    dividend *= kScalingFactor;
    // Not using DivAndRoundAwayFromZero because the addition never overflows
    // and shifting unsigned __int128 is more efficient.
    dividend += FixedUint<64, 3>(divisor >> 1);
    dividend /= FixedUint<64, 3>(divisor);

    auto res_or_status =
        NumericValue::FromFixedUint(dividend, is_negative != rh_is_negative);
    if (ABSL_PREDICT_TRUE(res_or_status.ok())) {
      return res_or_status;
    }
    return googlesql_base::StatusBuilder(res_or_status.status()).SetAppend()
           << ": " << ToString() << " / " << rh.ToString();
  }
  return MakeEvalError() << "division by zero: " << ToString() << " / "
                         << rh.ToString();
}

absl::StatusOr<NumericValue> NumericValue::DivideToIntegralValue(
    NumericValue rh) const {
  __int128 rh_value = rh.as_packed_int();
  if (ABSL_PREDICT_TRUE(rh_value != 0)) {
    __int128 value = as_packed_int() / rh_value;
    if (ABSL_PREDICT_TRUE(value <= internal::kNumericMax / kScalingFactor) &&
        ABSL_PREDICT_TRUE(value >= internal::kNumericMin / kScalingFactor)) {
      return NumericValue(value * kScalingFactor);
    }
    return MakeEvalError() << "numeric overflow: DIV(" << ToString() << ", "
                           << rh.ToString() << ")";
  }
  return MakeEvalError() << "division by zero: DIV(" << ToString() << ", "
                         << rh.ToString() << ")";
}

absl::StatusOr<NumericValue> NumericValue::Mod(NumericValue rh) const {
  __int128 rh_value = rh.as_packed_int();
  if (ABSL_PREDICT_TRUE(rh_value != 0)) {
    return NumericValue(as_packed_int() % rh_value);
  }
  return MakeEvalError() << "division by zero: MOD(" << ToString() << ", "
                         << rh.ToString() << ")";
}

void NumericValue::SerializeAndAppendToProtoBytes(std::string* bytes) const {
  FixedInt<64, 2>(as_packed_int()).SerializeToBytes(bytes);
}

absl::StatusOr<NumericValue> NumericValue::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  NumericValue out;
  if (ABSL_PREDICT_TRUE(out.value_.DeserializeFromBytes(bytes))) {
    if (ABSL_PREDICT_TRUE(out >= MinValue() && out <= MaxValue())) {
      return out;
    }
    return MakeEvalError() << "numeric overflow: result out of range";
  }
  return MakeEvalError() << "Invalid numeric encoding";
}

namespace {
void AppendZero(size_t fractional_size, bool always_print_decimal_point,
                std::string* output) {
  output->push_back('0');
  if (fractional_size > 0) {
    size_t decimal_point_pos = output->size();
    output->append(fractional_size + 1, '0');
    (*output)[decimal_point_pos] = '.';
  } else if (always_print_decimal_point) {
    output->push_back('.');
  }
}

void AppendExponent(int exponent, char e, std::string* output) {
  size_t size = output->size();
  googlesql_base::STLStringResizeUninitialized(output, size + 4);
  char exponent_sign = '+';
  if (exponent < 0) {
    exponent_sign = '-';
    exponent = -exponent;
  }
  ABSL_DCHECK_LE(exponent, 99);
  char* p = &(*output)[size];
  p[0] = e;
  p[1] = exponent_sign;
  p[2] = exponent / 10 + '0';
  p[3] = exponent % 10 + '0';
}

// Helper function called by two paths that decides whether to round a
// "round half even" value up, or to leave it as is.

// @should_round_half_up is a computed boolean on whether to round up or down
// @original_value is the original value that is instructed to be rounded
// @input is the current value, after being divided by pow(5,n) in the caller.
// @pow is the "digit" we are rounding the value to.

// This function may modify the *value argument.
ABSL_ATTRIBUTE_ALWAYS_INLINE
inline void MaybeRoundFinalBigNumericToEven(
    bool should_round_half_up, const FixedUint<64, 4>& original_value,
    FixedUint<64, 4>* input, uint64_t pow) {
  FixedUint<64, 4> difference = original_value;
  difference -= *input;
  difference <<= 1;
  FixedUint<64, 4> power_of_ten =
      FixedUint<64, 4>::PowerOf10(static_cast<uint>(pow));
  bool exactly_half_way = difference == power_of_ten;
  // If it's exactly halfway, and we were determined to need to round up
  // then add the power of ten.
  // Or, if the difference is actually MORE than half way, we should
  // round up like normal.
  // If not, continue to returning the
  // truncated (rounded down) value.
  if ((exactly_half_way && should_round_half_up) ||
      (difference > power_of_ten)) {
    *input += power_of_ten;
  }
}

// Rounds to the Pow-th digit using 2 divisions of Factors, whose product must
// equal 10^Pow, where Pow <= 38. Rounds depending on the rounding_mode, if the
// rounding mode is kTrunc, the value is always rounded down.
template <int Pow, uint64_t Factor1, uint64_t Factor2,
          RoundingMode rounding_mode>
    inline void
    RoundInternalFixedFactors(FixedUint<64, 4>* value) {
  FixedUint<64, 4> original_input = *value;
  *value /= std::integral_constant<uint64_t, Factor1>();
  uint64_t remainder;
  value->DivMod(std::integral_constant<uint64_t, Factor2>(), value, &remainder);
  // After dividing by the pow(10,n), determine whether the previous digit is
  // even or odd.
  bool should_round_half_up = false;
  if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
    // if we & the is_odd_mask and it's a 1, then the newest least
    // significant digit will be odd and we need to round up
    should_round_half_up = value->number()[0] & 1;
  }

  if (rounding_mode == RoundingMode::kRoundHalfAwayFromZero &&
      remainder >= (Factor2 >> 1)) {
    *value += uint64_t{1};
  }
  *value *= Factor1;
  *value *= Factor2;

  // If round to even is enabled, we need to see if the difference between
  // our original value and the truncated value is exactly halfway.
  if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
    MaybeRoundFinalBigNumericToEven(should_round_half_up, original_input, value,
                                    Pow);
  }
}

// Rounds or truncates this BIGNUMERIC value to the given number of decimal
// places after the decimal point (or before the decimal point if
// 'decimal_places' is negative), setting result if overflow does not occur. If
// 'round_away_from_zero' is true, then the result rounds away from zero, and
// might be out of the range of valid BigNumericValues. If
// 'round_away_from_zero' is false, then the extra decimal places are discarded
// and the result is always in the valid range.
template <RoundingMode rounding_mode>
bool RoundOrTrunc(FixedUint<64, 4>* abs_value, int64_t decimal_places) {
  switch (decimal_places) {
    // Fast paths for some common values of the second argument.
    case 0:
      RoundInternalFixedFactors<38, internal::k1e19, internal::k1e19,
                                rounding_mode>(abs_value);
      break;
    case 1:
      RoundInternalFixedFactors<37, internal::k1e19, internal::k1e18,
                                rounding_mode>(abs_value);
      break;
    case 2:
      RoundInternalFixedFactors<36, internal::k1e18, internal::k1e18,
                                rounding_mode>(abs_value);
      break;
    case 3:
      RoundInternalFixedFactors<35, internal::k1e18, internal::k1e17,
                                rounding_mode>(abs_value);
      break;
    case 4:
      RoundInternalFixedFactors<34, internal::k1e17, internal::k1e17,
                                rounding_mode>(abs_value);
      break;
    case 5:
      RoundInternalFixedFactors<33, internal::k1e17, internal::k1e16,
                                rounding_mode>(abs_value);
      break;
    case 6:
      RoundInternalFixedFactors<32, internal::k1e16, internal::k1e16,
                                rounding_mode>(abs_value);
      break;
    default: {
      if (ABSL_PREDICT_FALSE(decimal_places >=
                             BigNumericValue::kMaxFractionalDigits)) {
        // Rounding beyond the max number of supported fractional digits has no
        // effect.
        return true;
      }

      if (ABSL_PREDICT_FALSE(decimal_places <
                             -BigNumericValue::kMaxIntegerDigits)) {
        // Rounding (kBigNumericMaxIntegerDigits + 1) decimal_places away
        // results in zero. Rounding kBigNumericMaxIntegerDigits decimal_places
        // away might result in overflow instead of zero.
        *abs_value = FixedUint<64, 4>();
        return true;
      }

      // Overall logic is to perform value /= pow(10, n); value *= pow(10, n);
      // This is broken up into performing value /= pow(5, n); then
      // performing value /= pow(2, n); value *= pow(2, n); with some rounding
      // away from zero logic thrown in the middle, then multiply by
      // pow(5,n); again. This effectively truncates the values after the digit
      // to be rounded to, and in the process, decides whether to increment to
      // next value (round up) or not.

      static constexpr std::array<unsigned __int128, 39> kPowers =
          PowersAsc<unsigned __int128, 5, 5, 39>();
      // Power of 10 to divide the abs_value by, this should correspond to
      // 38 - decimal_places when decimal_places is positive and
      // abs(decimal_places) when negative since we do an initial division of
      // 10^38.
      uint64_t pow;
      if (decimal_places < 0) {
        pow = -decimal_places;
        *abs_value = FixedUint<64, 4>(
            BigNumericValue::RemoveScalingFactor</* round = */ false>(
                *abs_value));
      } else {
        pow = 38 - decimal_places;
      }

      FixedUint<64, 4> original_input = *abs_value;
      *abs_value /= FixedUint<64, 4>(kPowers[pow - 1]);
      bool should_round_half_up = false;

      if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
        const uint64_t is_odd_mask = (uint64_t{1} << pow);
        // if we & the is_odd_mask and it's a 1, then the newest least
        // significant digit will be odd and we need to round up
        if (abs_value->number()[0] & is_odd_mask) {
          should_round_half_up = true;
        }
      }

      // Determine if the value is half way OR MORE to the next value given the
      // pow we are rounding to, and then double check we should be rounding
      // and not truncating. If rounding mode is round_half_even, don't do this.
      if (rounding_mode == RoundingMode::kRoundHalfAwayFromZero &&
          abs_value->number()[0] & (1ULL << (pow - 1))) {
        *abs_value += (uint64_t{1} << pow);
      }

      const uint64_t mask = ~((uint64_t{1} << pow) - 1);
      std::array<uint64_t, 4> array = abs_value->number();

      // Chop off the bits after the "decimal_places" we are rounding to
      // specified given the mask.
      array[0] &= mask;
      *abs_value = FixedUint<64, 4>(array);
      // Multiply back by the power of 5 again.
      *abs_value *= FixedUint<64, 4>(kPowers[pow - 1]);

      // If rounding mode is round_half_even, check if the difference between
      // our original value and the truncated value is exactly halfway. If it
      // is, we'll round up depending on if the previous digit is odd or even.
      if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
        MaybeRoundFinalBigNumericToEven(should_round_half_up, original_input,
                                        abs_value, pow);
      }

      if (decimal_places < 0) {
        *abs_value *= internal::k1e19;
        *abs_value *= internal::k1e19;
      }
    }
  }
  return (rounding_mode == RoundingMode::kTrunc) ||
         !FixedInt<64, 4>(*abs_value).is_negative();
}

inline bool RoundInternal(FixedUint<64, 4>* input, int64_t decimal_places,
                          bool round_half_even = false) {
  if (round_half_even) {
    return RoundOrTrunc<RoundingMode::kRoundHalfEven>(input, decimal_places);
  }
  return RoundOrTrunc<RoundingMode::kRoundHalfAwayFromZero>(input,
                                                            decimal_places);
}

// Helper function to add grouping chars to the integer portion of the numeric
// string.
static void AddGroupingChar(const size_t first_digit_index,
                            const size_t end_of_integer_index,
                            std::string* output) {
  size_t grouping_char_count =
      (end_of_integer_index - first_digit_index - 1) / kGroupSize;
  // Resize output to account for 'soon-to-be-added' grouping chars
  googlesql_base::STLStringResizeUninitialized(output,
                                    output->size() + grouping_char_count);
  // Step 1: Copy all the fractional digits and decimal point to the end of
  // string.
  size_t copy_to_index = output->size() - 1;
  size_t copy_from_index = copy_to_index - grouping_char_count;
  while (copy_from_index >= end_of_integer_index) {
    output->at(copy_to_index--) = output->at(copy_from_index--);
  }

  // Step 2: Copy integer digits, adding grouping character as needed)
  while (copy_from_index < copy_to_index) {
    for (uint8_t curr_group_size = 0; curr_group_size < kGroupSize;
         curr_group_size++) {
      output->at(copy_to_index--) = output->at(copy_from_index--);
    }
    output->at(copy_to_index--) = kGroupChar;
  }
}

template <int scale, int n>
void Format(NumericValue::FormatSpec spec, const FixedInt<64, n>& input,
            std::string* output) {
  const size_t old_size = output->size();
  FixedUint<64, n> abs = input.abs();
  // Determine the sign.
  if (input.is_negative()) {
    output->push_back('-');
  } else if (spec.format_flags & FormatFlag::ALWAYS_PRINT_SIGN) {
    output->push_back('+');
  } else if (spec.format_flags & FormatFlag::SIGN_SPACE) {
    output->push_back(' ');
  }
  const size_t first_digit_index = output->size();

  switch (spec.mode) {
    case NumericValue::FormatSpec::DEFAULT: {
      size_t fractional_size =
          (spec.format_flags &
           FormatFlag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT)
              ? 0
              : spec.precision;
      // Round to the expected fractional size. If precision is 0, the
      // string produced by ToString() will not have a decimal point.
      RoundInternal(&abs, spec.precision);
      if (abs.is_zero()) {
        AppendZero(fractional_size,
                   spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT,
                   output);
      } else {
        abs.AppendToString(output);
        size_t decimal_point_index = AddDecimalPointAndAdjustZeros(
            first_digit_index, scale, fractional_size,
            spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT, output);
        if (spec.format_flags & FormatFlag::USE_GROUPING_CHAR) {
          AddGroupingChar(first_digit_index, decimal_point_index, output);
        }
      }
    } break;
    case NumericValue::FormatSpec::E_NOTATION_LOWER_CASE:
    case NumericValue::FormatSpec::E_NOTATION_UPPER_CASE: {
      int exponent = 0;
      size_t fractional_size =
          (spec.format_flags &
           FormatFlag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT)
              ? 0
              : spec.precision;
      if (abs.is_zero()) {
        AppendZero(fractional_size,
                   spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT,
                   output);
      } else {
        int num_digits = abs.CountDecimalDigits();
        exponent = num_digits - 1 - scale;
        RoundInternal(&abs, static_cast<int64_t>(spec.precision) - exponent);
        const FixedUint<64, n>& next_power_of_10 =
            FixedUint<64, n>::PowerOf10(num_digits);
        // If abs is rounded up with one more digit, adjust exponent.
        // Example: to compute FORMAT("%.0e", 9.5e-8), 95 is rounded to 100;
        // "1e-7" should be returned instead of "10e-8".
        exponent += (abs >= next_power_of_10);
        abs.AppendToString(output);
        AddDecimalPointAndAdjustZeros(
            first_digit_index, scale + exponent, fractional_size,
            spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT, output);
      }
      AppendExponent(exponent, spec.mode, output);
    } break;
    case NumericValue::FormatSpec::GENERAL_FORMAT_LOWER_CASE:
    case NumericValue::FormatSpec::GENERAL_FORMAT_UPPER_CASE: {
      int64_t adjusted_precision = std::max<uint>(spec.precision, 1);
      int64_t fractional_size = adjusted_precision - 1;
      if (abs.is_zero()) {
        fractional_size =
            spec.format_flags &
                    FormatFlag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT
                ? 0
                : fractional_size;
        AppendZero(fractional_size,
                   spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT,
                   output);
      } else {
        int num_digits = abs.CountDecimalDigits();
        int exponent = num_digits - 1 - scale;
        RoundInternal(&abs, fractional_size - exponent);
        const FixedUint<64, n>& next_power_of_10 =
            FixedUint<64, n>::PowerOf10(num_digits);
        exponent += (abs >= next_power_of_10);
        abs.AppendToString(output);
        if (exponent >= -4 && exponent <= fractional_size) {
          // Use f-style.
          size_t end_integer_index = AddDecimalPointAndAdjustZeros(
              first_digit_index, scale,
              spec.format_flags &
                      FormatFlag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT
                  ? 0
                  : fractional_size - exponent,
              spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT,
              output);
          if (spec.format_flags & FormatFlag::USE_GROUPING_CHAR) {
            AddGroupingChar(first_digit_index, end_integer_index, output);
          }
        } else {
          // Use e-style.
          AddDecimalPointAndAdjustZeros(
              first_digit_index, scale + exponent,
              spec.format_flags &
                      FormatFlag::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT
                  ? 0
                  : fractional_size,
              spec.format_flags & FormatFlag::ALWAYS_PRINT_DECIMAL_POINT,
              output);
          char mode = spec.mode - ('G' - 'E');
          AppendExponent(exponent, mode, output);
        }
      }
    }
  }

  const size_t inserted_size = output->size() - old_size;
  if (inserted_size < spec.minimum_size) {
    size_t padding_size = spec.minimum_size - inserted_size;
    // With space padding, the spaces come first. With zero-padding, the sign
    // comes first. Left justification takes precedence over the zero padding
    // flag.
    if (spec.format_flags & FormatFlag::LEFT_JUSTIFY) {
      output->append(padding_size, ' ');
    } else if (spec.format_flags & FormatFlag::ZERO_PAD) {
      output->insert(first_digit_index, padding_size, '0');
    } else {
      output->insert(old_size, padding_size, ' ');
    }
  }
}

absl::Status FromScaledValueOutOfRangeError(absl::string_view type_name,
                                            size_t input_len,
                                            int source_scale) {
  return MakeEvalError() << "Value is out of range after scaling to "
                         << type_name << " type; input length: " << input_len
                         << "; scale: " << source_scale;
}

// This function takes in a large number of scale and precision arguments that
// all signify different important pieces of parsing a little endian value.
// source_scale: the scale of the endian value being inserted
// fractional_digits_to_keep: the scale of the column being loaded into
// destination_scale: fractional digits (9 or 38) for the destination (input may
// need to be scaled up to have this number of fractional digits)
// destination_max_integer_digits: max integer digits (29 or 38)
// This function is called for both NUMERIC and BIGNUMERIC parsing
template <RoundingMode rounding_mode>
absl::StatusOr<FixedInt<64, 4>> FixedIntFromScaledValue(
    absl::string_view little_endian_value, int source_scale,
    int fractional_digits_to_keep, int destination_scale,
    int destination_max_integer_digits, absl::string_view type_name) {
  const size_t original_input_len = little_endian_value.size();
  if (original_input_len == 0) {
    return FixedInt<64, 4>();
  }

  // Ignore trailing '\xff' or '\x00' bytes if they don't affect the value.
  // For example, '\xab\xff' is equivalent to '\xab'.
  const char* most_significant_byte = &little_endian_value.back();
  bool is_negative = (*most_significant_byte & '\x80') != 0;
  char extension_byte = is_negative ? '\xff' : '\x00';
  while (most_significant_byte > little_endian_value.data() &&
         *most_significant_byte == extension_byte &&
         ((most_significant_byte[-1] ^ extension_byte) & '\x80') == 0) {
    --most_significant_byte;
  }
  if (most_significant_byte == little_endian_value.data() &&
      *most_significant_byte == '\x0') {
    return FixedInt<64, 4>();
  }
  little_endian_value =
      absl::string_view(little_endian_value.data(),
                        most_significant_byte - little_endian_value.data() + 1);

  if (source_scale <= fractional_digits_to_keep) {
    // Scale up the value.
    FixedInt<64, 4> value;
    if (source_scale <= -destination_max_integer_digits ||
        !value.DeserializeFromBytes(little_endian_value)) {
      return FromScaledValueOutOfRangeError(type_name, original_input_len,
                                            source_scale);
    }
    int scale_up_digits = destination_scale - source_scale;
    ABSL_DCHECK_GE(scale_up_digits, 0);
    ABSL_DCHECK_LT(scale_up_digits,
              destination_scale + destination_max_integer_digits);
    if (scale_up_digits > 0 &&
        value.MultiplyOverflow(FixedInt<64, 4>::PowerOf10(scale_up_digits))) {
      return FromScaledValueOutOfRangeError(type_name, original_input_len,
                                            source_scale);
    }
    return value;
  }

  // Scale down the value.
  std::vector<uint64_t> dividend(
      (little_endian_value.size() + sizeof(uint64_t) - 1) / sizeof(uint64_t));
  VarIntRef<64> var_int_ref(dividend);
  bool success = var_int_ref.DeserializeFromBytes(little_endian_value);
  ABSL_DCHECK(success);
  if (is_negative) {
    var_int_ref.Negate();
    if (dividend.back() == 0) {
      dividend.pop_back();
    }
  }

  int scale_down_digits = source_scale - fractional_digits_to_keep;
  uint64_t remainder;
  uint64_t divisor;
  // Cache whether during the scale down while loop, we have a remainder we
  // throw out. this helps us determine if we are in a "half way value"
  // when rounding.
  bool has_scale_down_remainder_discarded = false;
  // Compute dividend /= pow(10, scale_down_digits) by repeating
  // dividend /= uint64 divisor. When scale_down_digits > 19, this loop is not
  // as efficient as fixed_int_internal::LongDiv, but this method is not
  // expected to be called in a performance-critical path.
  while (scale_down_digits > 0) {
    if (dividend.empty()) {
      return FixedInt<64, 4>();
    }
    VarUintRef<64> var_int_ref(dividend);
    int to_scale_down = std::min(scale_down_digits, 19);
    divisor = var_int_ref.ScaleDown(to_scale_down, remainder);
    scale_down_digits -= to_scale_down;
    // If we're going to execute this while loop again, we're wiping the
    // remainder, so want to cache whether we had a remainder or not for
    // future rounding.
    if constexpr (rounding_mode != RoundingMode::kTrunc) {
      has_scale_down_remainder_discarded |=
          (remainder != 0) & (scale_down_digits > 0);
    } else if (remainder != 0) {
      return MakeEvalError()
             << "Value will lose precision after "
                "scaling down to "
             << type_name << " type; input length: " << original_input_len
             << "; scale: " << source_scale;
    }
    if (dividend.back() == 0) {
      dividend.pop_back();
    }
  }
  if (dividend.size() > 4) {
    return FromScaledValueOutOfRangeError(type_name, original_input_len,
                                          source_scale);
  }
  std::array<uint64_t, 4> src;
  auto itr = std::copy(dividend.begin(), dividend.end(), src.begin());
  std::fill(itr, src.end(), 0);
  FixedUint<64, 4> abs_value(src);
  // Round based on the rounding mode. Divisor is always an even number.
  bool should_round_up = false;
  if constexpr (rounding_mode == RoundingMode::kRoundHalfEven) {
    // Check whether the current remainder is exactly half of the divisor AND
    // we had no remainder during the scaling down of the value.
    const bool half_way_value =
        remainder == (divisor >> 1) && !has_scale_down_remainder_discarded;
    // if we're at a halfway value, check previous digit to being even or odd
    // to decide if we round up or not.
    if (half_way_value) {
      // For ROUND_HALF_EVEN, if the last digit is even, round down, if odd
      // round up
      should_round_up = abs_value.number()[0] & 1;
    } else {
      should_round_up = remainder >= (divisor >> 1);
    }
  } else if constexpr (rounding_mode == RoundingMode::kRoundHalfAwayFromZero) {
    should_round_up = remainder >= (divisor >> 1);
  }
  if (should_round_up && abs_value.AddOverflow(uint64_t{1})) {
    return FromScaledValueOutOfRangeError(type_name, original_input_len,
                                          source_scale);
  }
  FixedInt<64, 4> value;
  if (!value.SetSignAndAbs(is_negative, abs_value)) {
    return FromScaledValueOutOfRangeError(type_name, original_input_len,
                                          source_scale);
  }
  // If our destination scale is less than max fractional digits, scale the
  // remaining of the fractional digits we haven't already scaled to
  if (fractional_digits_to_keep < destination_scale) {
    int extra_scaling_factor = destination_scale - fractional_digits_to_keep;
    if (value.MultiplyOverflow(
            FixedInt<64, 4>::PowerOf10(extra_scaling_factor))) {
      return FromScaledValueOutOfRangeError(type_name, original_input_len,
                                            source_scale);
    }
  }
  return value;
}
}  // namespace

void NumericValue::FormatAndAppend(FormatSpec spec, std::string* output) const {
  Format<kMaxFractionalDigits>(spec, FixedInt<64, 2>(as_packed_int()), output);
}

std::ostream& operator<<(std::ostream& out, NumericValue value) {
  return out << value.ToString();
}

absl::StatusOr<NumericValue> NumericValue::Rescale(int scale,
                                                   bool allow_rounding) const {
  if (scale < 0 || scale > kMaxFractionalDigits) {
    return MakeEvalError() << absl::Substitute(
               "NUMERIC scale must be between 0 and $0 but got $1",
               kMaxFractionalDigits, scale);
  }

  const FixedInt<64, 2>& divisor =
      FixedInt<64, 2>::PowerOf10(kMaxFractionalDigits - scale);
  FixedInt<64, 2> scaled_value(as_packed_int());
  if (allow_rounding) {
    scaled_value.DivAndRoundAwayFromZero(divisor);
  } else {
    FixedInt<64, 2> remainder;
    scaled_value.DivMod(divisor, &scaled_value, &remainder);
    if (!remainder.is_zero()) {
      return MakeEvalError() << absl::Substitute(
                 "Value will lose precision after scaling down to a scale of "
                 "$0",
                 scale);
    }
  }
  return NumericValue(static_cast<__int128>(scaled_value));
}

absl::StatusOr<NumericValue> NumericValue::FromScaledLittleEndianValue(
    absl::string_view little_endian_value, int source_scale,
    int fractional_digits_to_keep, bool allow_rounding, bool round_half_even) {
  FixedInt<64, 4> value;
  int destination_scale = NumericValue::kMaxFractionalDigits;
  int destination_max_integer_digits = NumericValue::kMaxIntegerDigits;
  if (!allow_rounding) {
    GOOGLESQL_ASSIGN_OR_RETURN(value, FixedIntFromScaledValue<RoundingMode::kTrunc>(
                                little_endian_value, source_scale,
                                fractional_digits_to_keep, destination_scale,
                                destination_max_integer_digits, "NUMERIC"));
  } else if (round_half_even) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        value,
        FixedIntFromScaledValue<RoundingMode::kRoundHalfEven>(
            little_endian_value, source_scale, fractional_digits_to_keep,
            destination_scale, destination_max_integer_digits, "NUMERIC"));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        value,
        FixedIntFromScaledValue<RoundingMode::kRoundHalfAwayFromZero>(
            little_endian_value, source_scale, fractional_digits_to_keep,
            destination_scale, destination_max_integer_digits, "NUMERIC"));
  }
  auto res_status = NumericValue::FromFixedInt(value);
  if (res_status.ok()) {
    return res_status;
  }
  return FromScaledValueOutOfRangeError("NUMERIC", little_endian_value.size(),
                                        source_scale);
}

absl::StatusOr<NumericValue> NumericValue::SumAggregator::GetSum() const {
  auto res_status = NumericValue::FromFixedInt(sum_);
  if (res_status.ok()) {
    return res_status;
  }
  return MakeEvalError() << "numeric overflow: SUM";
}

absl::StatusOr<NumericValue> NumericValue::SumAggregator::GetAverage(
    uint64_t count) const {
  if (count == 0) {
    return MakeEvalError() << "division by zero: AVG";
  }

  FixedInt<64, 3> dividend = sum_;
  dividend.DivAndRoundAwayFromZero(count);

  auto res_status = NumericValue::FromFixedInt(dividend);
  if (res_status.ok()) {
    return res_status;
  }
  return MakeEvalError() << "numeric overflow: AVG";
}

void NumericValue::SumAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  sum_.SerializeToBytes(bytes);
}

absl::StatusOr<NumericValue::SumAggregator>
NumericValue::SumAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  NumericValue::SumAggregator out;
  if (out.sum_.DeserializeFromBytes(bytes)) {
    return out;
  }
  return MakeEvalError() << "Invalid NumericValue::SumAggregator encoding";
}

void NumericValue::VarianceAggregator::Add(NumericValue value) {
  sum_ += FixedInt<64, 3>(value.as_packed_int());
  FixedInt<64, 2> v(value.as_packed_int());
  sum_square_ += FixedInt<64, 5>(ExtendAndMultiply(v, v));
}

void NumericValue::VarianceAggregator::Subtract(NumericValue value) {
  sum_ -= FixedInt<64, 3>(value.as_packed_int());
  FixedInt<64, 2> v(value.as_packed_int());
  sum_square_ -= FixedInt<64, 5>(ExtendAndMultiply(v, v));
}

std::optional<double> NumericValue::VarianceAggregator::GetVariance(
    uint64_t count, bool is_sampling) const {
  uint64_t count_offset = is_sampling;
  if (count > count_offset) {
    return Covariance(sum_, sum_, sum_square_, NumericScalingFactorSquared(),
                      count, count_offset);
  }
  return std::nullopt;
}

std::optional<double> NumericValue::VarianceAggregator::GetStdDev(
    uint64_t count, bool is_sampling) const {
  uint64_t count_offset = is_sampling;
  if (count > count_offset) {
    return std::sqrt(Covariance(sum_, sum_, sum_square_,
                                NumericScalingFactorSquared(), count,
                                count_offset));
  }
  return std::nullopt;
}

void NumericValue::VarianceAggregator::MergeWith(
    const VarianceAggregator& other) {
  sum_ += other.sum_;
  sum_square_ += other.sum_square_;
}

void NumericValue::VarianceAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  SerializeFixedInt(bytes, sum_, sum_square_);
}

absl::StatusOr<NumericValue::VarianceAggregator>
NumericValue::VarianceAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  VarianceAggregator out;
  if (DeserializeFixedInt(bytes, &out.sum_, &out.sum_square_)) {
    return out;
  }
  return MakeEvalError() << "Invalid NumericValue::VarianceAggregator encoding";
}

void NumericValue::CovarianceAggregator::Add(NumericValue x, NumericValue y) {
  sum_x_ += FixedInt<64, 3>(x.as_packed_int());
  sum_y_ += FixedInt<64, 3>(y.as_packed_int());
  FixedInt<64, 2> x_num(x.as_packed_int());
  FixedInt<64, 2> y_num(y.as_packed_int());
  sum_product_ += FixedInt<64, 5>(ExtendAndMultiply(x_num, y_num));
}

void NumericValue::CovarianceAggregator::Subtract(NumericValue x,
                                                  NumericValue y) {
  sum_x_ -= FixedInt<64, 3>(x.as_packed_int());
  sum_y_ -= FixedInt<64, 3>(y.as_packed_int());
  FixedInt<64, 2> x_num(x.as_packed_int());
  FixedInt<64, 2> y_num(y.as_packed_int());
  sum_product_ -= FixedInt<64, 5>(ExtendAndMultiply(x_num, y_num));
}

std::optional<double> NumericValue::CovarianceAggregator::GetCovariance(
    uint64_t count, bool is_sampling) const {
  uint64_t count_offset = is_sampling;
  if (count > count_offset) {
    return Covariance(sum_x_, sum_y_, sum_product_,
                      NumericScalingFactorSquared(), count, count_offset);
  }
  return std::nullopt;
}

void NumericValue::CovarianceAggregator::MergeWith(
    const CovarianceAggregator& other) {
  sum_x_ += other.sum_x_;
  sum_y_ += other.sum_y_;
  sum_product_ += other.sum_product_;
}

void NumericValue::CovarianceAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  SerializeFixedInt(bytes, sum_product_, sum_x_, sum_y_);
}

absl::StatusOr<NumericValue::CovarianceAggregator>
NumericValue::CovarianceAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  CovarianceAggregator out;
  if (DeserializeFixedInt(bytes, &out.sum_product_, &out.sum_x_, &out.sum_y_)) {
    return out;
  }
  return MakeEvalError()
         << "Invalid NumericValue::CovarianceAggregator encoding";
}

void NumericValue::CorrelationAggregator::Add(NumericValue x, NumericValue y) {
  cov_agg_.Add(x, y);
  FixedInt<64, 2> x_num(x.as_packed_int());
  FixedInt<64, 2> y_num(y.as_packed_int());
  sum_square_x_ += FixedInt<64, 5>(ExtendAndMultiply(x_num, x_num));
  sum_square_y_ += FixedInt<64, 5>(ExtendAndMultiply(y_num, y_num));
}

void NumericValue::CorrelationAggregator::Subtract(NumericValue x,
                                                   NumericValue y) {
  cov_agg_.Subtract(x, y);
  FixedInt<64, 2> x_num(x.as_packed_int());
  FixedInt<64, 2> y_num(y.as_packed_int());
  sum_square_x_ -= FixedInt<64, 5>(ExtendAndMultiply(x_num, x_num));
  sum_square_y_ -= FixedInt<64, 5>(ExtendAndMultiply(y_num, y_num));
}

std::optional<double> NumericValue::CorrelationAggregator::GetCorrelation(
    uint64_t count) const {
  if (count > 1) {
    FixedInt<64, 6> numerator = GetScaledCovarianceNumerator(
        cov_agg_.sum_x_, cov_agg_.sum_y_, cov_agg_.sum_product_, count);
    FixedInt<64, 6> variance_numerator_x = GetScaledCovarianceNumerator(
        cov_agg_.sum_x_, cov_agg_.sum_x_, sum_square_x_, count);
    FixedInt<64, 6> variance_numerator_y = GetScaledCovarianceNumerator(
        cov_agg_.sum_y_, cov_agg_.sum_y_, sum_square_y_, count);
    FixedInt<64, 12> denominator_square =
        ExtendAndMultiply(variance_numerator_x, variance_numerator_y);
    if (denominator_square.is_zero()) {
      return std::numeric_limits<double>::quiet_NaN();
    }

    return static_cast<double>(numerator) /
           std::sqrt(static_cast<double>(denominator_square));
  }
  return std::nullopt;
}

void NumericValue::CorrelationAggregator::MergeWith(
    const CorrelationAggregator& other) {
  cov_agg_.MergeWith(other.cov_agg_);
  sum_square_x_ += other.sum_square_x_;
  sum_square_y_ += other.sum_square_y_;
}

void NumericValue::CorrelationAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  SerializeFixedInt(bytes, cov_agg_.sum_product_, cov_agg_.sum_x_,
                    cov_agg_.sum_y_, sum_square_x_, sum_square_y_);
}

absl::StatusOr<NumericValue::CorrelationAggregator>
NumericValue::CorrelationAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  CorrelationAggregator out;
  if (DeserializeFixedInt(bytes, &out.cov_agg_.sum_product_,
                          &out.cov_agg_.sum_x_, &out.cov_agg_.sum_y_,
                          &out.sum_square_x_, &out.sum_square_y_)) {
    return out;
  }
  return MakeEvalError()
         << "Invalid NumericValue::CorrelationAggregator encoding";
}

inline absl::Status MakeInvalidBigNumericError(absl::string_view str) {
  return MakeEvalError() << "Invalid BIGNUMERIC value: " << str;
}

absl::StatusOr<BigNumericValue> BigNumericValue::Multiply(
    const BigNumericValue& rh) const {
  bool lh_negative = value_.is_negative();
  bool rh_negative = rh.value_.is_negative();
  FixedUint<64, 8> abs_result_64x8 =
      ExtendAndMultiply(value_.abs(), rh.value_.abs());
  if (ABSL_PREDICT_TRUE(abs_result_64x8.number()[6] == 0) &&
      ABSL_PREDICT_TRUE(abs_result_64x8.number()[7] == 0)) {
    FixedUint<64, 5> abs_result_64x5 = RemoveScalingFactor</* round = */ true>(
        FixedUint<64, 6>(abs_result_64x8));
    if (ABSL_PREDICT_TRUE(abs_result_64x5.number()[4] == 0)) {
      FixedInt<64, 4> result;
      FixedUint<64, 4> abs_result_64x4(abs_result_64x5);
      if (ABSL_PREDICT_TRUE(result.SetSignAndAbs(lh_negative != rh_negative,
                                                 abs_result_64x4))) {
        return BigNumericValue(result);
      }
    }
  }
  return MakeEvalError() << "BIGNUMERIC overflow: " << ToString() << " * "
                         << rh.ToString();
}

absl::StatusOr<BigNumericValue> BigNumericValue::MultiplyAndDivideByPowerOfTwo(
    const FixedInt<64, 4>& multiplier, uint scale_bits) const {
  bool negative = value_.is_negative();
  bool mult_negative = multiplier.is_negative();
  bool result_is_negative = negative != mult_negative;
  FixedUint<64, 8> product = ExtendAndMultiply(
      FixedUint<64, 4>(value_.abs()), FixedUint<64, 4>(multiplier.abs()));
  if (scale_bits > 0) {
    ShiftRightAndRound(scale_bits, &product);
  }

  std::array<uint64_t, 8> result_words = product.number();
  for (int i = 4; i < 8; ++i) {
    if (ABSL_PREDICT_FALSE(result_words[i] != 0)) {
      return MakeEvalError()
             << "numeric overflow: " << ToString() << " * "
             << multiplier.ToString() << " / pow(2, " << scale_bits << ")";
    }
  }

  FixedUint<64, 4> magnitude(std::array<uint64_t, 4>{
      result_words[0], result_words[1], result_words[2], result_words[3]});
  FixedInt<64, 4> result;
  if (ABSL_PREDICT_FALSE(
          !result.SetSignAndAbs(result_is_negative, magnitude))) {
    return MakeEvalError() << "numeric overflow: " << ToString() << " * "
                           << multiplier.ToString() << " / pow(2, "
                           << scale_bits << ")";
  }
  return BigNumericValue(result);
}

absl::StatusOr<BigNumericValue> BigNumericValue::Divide(
    const BigNumericValue& rh) const {
  bool lh_negative = value_.is_negative();
  bool rh_negative = rh.value_.is_negative();
  if (ABSL_PREDICT_TRUE(!rh.value_.is_zero())) {
    FixedUint<64, 4> abs_value = value_.abs();
    FixedUint<64, 6> rh_abs_value(rh.value_.abs());
    FixedUint<64, 6> scaled_abs_value =
        ExtendAndMultiply(abs_value, FixedUint<64, 2>(kScalingFactor));
    scaled_abs_value.DivAndRoundAwayFromZero(rh_abs_value);
    if (ABSL_PREDICT_TRUE(scaled_abs_value.number()[4] == 0 &&
                          scaled_abs_value.number()[5] == 0)) {
      FixedUint<64, 4> abs_result(scaled_abs_value);
      FixedInt<64, 4> result;
      if (ABSL_PREDICT_TRUE(
              result.SetSignAndAbs(lh_negative != rh_negative, abs_result))) {
        return BigNumericValue(result);
      }
    }
    return MakeEvalError() << "BIGNUMERIC overflow: " << ToString() << " / "
                           << rh.ToString();
  }
  return MakeEvalError() << "division by zero: " << ToString() << " / "
                         << rh.ToString();
}

absl::StatusOr<BigNumericValue> BigNumericValue::DivideToIntegralValue(
    const BigNumericValue& rh) const {
  if (ABSL_PREDICT_TRUE(!rh.value_.is_zero())) {
    bool lh_negative = value_.is_negative();
    bool rh_negative = rh.value_.is_negative();
    FixedUint<64, 4> abs_result = value_.abs();
    abs_result /= rh.value_.abs();
    bool overflow =
        abs_result.MultiplyOverflow(FixedUint<64, 4>(kScalingFactor));

    if (ABSL_PREDICT_TRUE(!overflow)) {
      FixedInt<64, 4> result;
      if (ABSL_PREDICT_TRUE(
              result.SetSignAndAbs(lh_negative != rh_negative, abs_result))) {
        return BigNumericValue(result);
      }
    }
    return MakeEvalError() << "BIGNUMERIC overflow: DIV(" << ToString() << ", "
                           << rh.ToString() << ")";
  }
  return MakeEvalError() << "division by zero: DIV(" << ToString() << ", "
                         << rh.ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Mod(
    const BigNumericValue& rh) const {
  if (ABSL_PREDICT_TRUE(!rh.value_.is_zero())) {
    FixedInt<64, 4> remainder = value_;
    remainder %= rh.value_;
    return BigNumericValue(remainder);
  }
  return MakeEvalError() << "division by zero: MOD(" << ToString() << ", "
                         << rh.ToString() << ")";
}

bool BigNumericValue::HasFractionalPart() const {
  FixedUint<64, 4> abs_value = value_.abs();
  // Check whether abs_value is a multiple of pow(2, 38).
  if ((abs_value.number()[0] & ((1ULL << 38) - 1)) != 0) return true;
  // Check whether abs_value is a multiple of pow(10, 38).
  uint64_t mod = 0;
  abs_value.DivMod(std::integral_constant<uint64_t, internal::k1e19>(),
                   &abs_value, &mod);
  if (mod != 0) return true;
  abs_value.DivMod(std::integral_constant<uint64_t, internal::k1e19>(),
                   &abs_value, &mod);
  return (mod != 0);
}

double BigNumericValue::ToDouble() const {
  bool is_negative = value_.is_negative();
  FixedUint<64, 4> abs_value = value_.abs();
  double binary_scaling_factor = 1;
  constexpr double k1e64 = static_cast<double>(__int128{1} << 64);
  // To ensure precision, the number should be shifted first, so that it has
  // more than 54 bits after being scaled down by 10^38. The shifting is
  // undone by dividing by the binary_scaling_factor after coverting to double.
  // The shift amount is a multiple of 64, and will be optimized to simple
  // 64-bit assignments for performances.
  if (abs_value.number()[3] == 0) {
    if (abs_value.number()[2] != 0) {
      abs_value <<= 64;
      binary_scaling_factor = k1e64;
    } else if (abs_value.number()[1] != 0) {
      abs_value <<= 64 * 2;
      binary_scaling_factor = k1e64 * k1e64;
    } else if (abs_value.number()[0] != 0) {
      abs_value <<= 64 * 3;
      binary_scaling_factor = k1e64 * k1e64 * k1e64;
    } else {
      return 0;
    }
  }
  uint64_t remainder_bits;
  abs_value.DivMod(std::integral_constant<uint64_t, internal::k1e19>(),
                   &abs_value, &remainder_bits);
  uint64_t remainder;
  abs_value.DivMod(std::integral_constant<uint64_t, internal::k1e19>(),
                   &abs_value, &remainder);
  remainder_bits |= remainder;
  std::array<uint64_t, 4> n = abs_value.number();
  n[0] |= (remainder_bits != 0);
  double result =
      static_cast<double>(FixedUint<64, 4>(n)) / binary_scaling_factor;
  return is_negative ? -result : result;
}

absl::StatusOr<BigNumericValue> BigNumericValue::Round(
    int64_t decimal_places, bool round_half_even) const {
  FixedUint<64, 4> abs_value = value_.abs();
  if (ABSL_PREDICT_TRUE(
          RoundInternal(&abs_value, decimal_places, round_half_even))) {
    FixedInt<64, 4> result(abs_value);
    return BigNumericValue(!value_.is_negative() ? result : -result);
  }
  return MakeEvalError() << "BIGNUMERIC overflow: ROUND(" << ToString() << ", "
                         << decimal_places << ")";
}

BigNumericValue BigNumericValue::Trunc(int64_t decimal_places) const {
  FixedUint<64, 4> abs_value = value_.abs();
  RoundOrTrunc<RoundingMode::kTrunc>(&abs_value, decimal_places);
  FixedInt<64, 4> result(abs_value);
  return BigNumericValue(!value_.is_negative() ? result : -result);
}

absl::StatusOr<BigNumericValue> BigNumericValue::Floor() const {
  if (!value_.is_negative()) {
    FixedInt<64, 4> floor_value(UnsignedFloor(value_.abs()));
    return BigNumericValue(floor_value);
  }
  // UnsignedCeiling cannot overflow, however it can return a FixedUint which
  // is out of range as the same FixedInt. Because the constructor simply
  // copies the underlying bits we can check the high bit, i.e. is_negative
  FixedInt<64, 4> ceiling_value(UnsignedCeiling(value_.abs()));
  if (ABSL_PREDICT_TRUE(!ceiling_value.is_negative())) {
    return BigNumericValue(-ceiling_value);
  }
  return MakeEvalError() << "BIGNUMERIC overflow: FLOOR(" << ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Ceiling() const {
  if (!value_.is_negative()) {
    FixedInt<64, 4> ceiling_value(UnsignedCeiling(value_.abs()));
    // UnsignedCeiling cannot overflow, however it can return a FixedUint which
    // is out of range as the same FixedInt. Because the constructor simply
    // copies the underlying bits we can check the high bit, i.e. is_negative
    if (ABSL_PREDICT_TRUE(!ceiling_value.is_negative())) {
      return BigNumericValue(ceiling_value);
    }
    return MakeEvalError() << "BIGNUMERIC overflow: CEIL(" << ToString() << ")";
  }
  FixedInt<64, 4> floor_value(UnsignedFloor(value_.abs()));
  return BigNumericValue(-floor_value);
}

absl::StatusOr<BigNumericValue> BigNumericValue::Power(
    const BigNumericValue& exp) const {
  auto res_or_status = PowerInternal<4, 3, 6, 254>(*this, exp);
  if (res_or_status.ok()) {
    return res_or_status;
  }
  return googlesql_base::StatusBuilder(res_or_status.status()).SetAppend()
         << ": POW(" << ToString() << ", " << exp.ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Exp() const {
  SignedBinaryFraction<6, 254> base(*this);
  UnsignedBinaryFraction<6, 254> exp;
  BigNumericValue result;
  if (ABSL_PREDICT_TRUE(base.Exp(&exp)) &&
      ABSL_PREDICT_TRUE(exp.To(false, &result))) {
    return result;
  }
  return MakeEvalError() << "BIGNUMERIC overflow: EXP(" << ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Ln() const {
  if (value_.is_negative() || value_.is_zero()) {
    return MakeEvalError() << "LN is undefined for zero or negative value: LN("
                           << ToString() << ")";
  }
  UnsignedBinaryFraction<6, 254> exp =
      SignedBinaryFraction<6, 254>(*this).Abs();
  SignedBinaryFraction<6, 254> ln;
  // unit_of_last_precision is set to pow(2, -144) ~= 4.5e-44 here. In the
  // implementation of Ln with Halley's mothod, computation will stop when the
  // delta of the iteration is less than unit_of_last_precision. Thus, 4.5e-44
  // is set up here to provide enough precision for BigNumericValue and avoid
  // unnecessary computation.
  UnsignedBinaryFraction<6, 254> unit_of_last_precision(1, -144);
  BigNumericValue result;
  if (ABSL_PREDICT_TRUE(exp.Ln(unit_of_last_precision, &ln)) &&
      ABSL_PREDICT_TRUE(ln.To(&result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "LN should never overflow: LN(" << ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Log10() const {
  if (value_.is_negative() || value_.is_zero()) {
    return MakeEvalError()
           << "LOG10 is undefined for zero or negative value: LOG10("
           << ToString() << ")";
  }
  UnsignedBinaryFraction<6, 254> exp =
      SignedBinaryFraction<6, 254>(*this).Abs();
  SignedBinaryFraction<6, 254> log10;
  // unit_of_last_precision will be used in LN iteration ending condition.
  // pow(2, -144) ~= 4.5e-44 is chosen here to provide enough precision and
  // avoid unnecessary computation.
  UnsignedBinaryFraction<6, 254> unit_of_last_precision(1, -144);
  BigNumericValue result;
  if (ABSL_PREDICT_TRUE(exp.Log10(unit_of_last_precision, &log10)) &&
      ABSL_PREDICT_TRUE(log10.To(&result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "LOG10 should never overflow: LOG10(" << ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Log(
    const BigNumericValue& base) const {
  if (value_.is_negative() || value_.is_zero() || base.value_.is_negative() ||
      base.value_.is_zero() || base == BigNumericValue(1)) {
    return MakeEvalError() << "LOG is undefined for zero or negative value, or "
                              "when base equals 1: "

                              "LOG("
                           << ToString() << ", " << base.ToString() << ")";
  }
  UnsignedBinaryFraction<6, 254> abs_value =
      SignedBinaryFraction<6, 254>(*this).Abs();
  UnsignedBinaryFraction<6, 254> abs_base =
      SignedBinaryFraction<6, 254>(base).Abs();
  SignedBinaryFraction<6, 254> log;
  // unit_of_last_precision will be used in LN iteration ending condition.
  // pow(2, -250) is chosen here to provide enough precision, and also provide a
  // loose range to avoid precision lost in division.
  UnsignedBinaryFraction<6, 254> unit_of_last_precision(1, -250);
  BigNumericValue result;
  if (ABSL_PREDICT_TRUE(
          abs_value.Log(abs_base, unit_of_last_precision, &log)) &&
      ABSL_PREDICT_TRUE(log.To(&result))) {
    return result;
  }
  return MakeEvalError() << "BIGNUMERIC overflow: "

                            "LOG("
                         << ToString() << ", " << base.ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Sqrt() const {
  if (value_.is_negative()) {
    return MakeEvalError() << "SQRT is undefined for negative value: SQRT("
                           << ToString() << ")";
  }

  UnsignedBinaryFraction<6, 254> value =
      SignedBinaryFraction<6, 254>(*this).Abs();
  UnsignedBinaryFraction<6, 254> sqrt;
  BigNumericValue result;
  if (ABSL_PREDICT_TRUE(value.Sqrt(&sqrt)) &&
      ABSL_PREDICT_TRUE(sqrt.To(false, &result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "SQRT should never overflow: SQRT(" << ToString() << ")";
}

absl::StatusOr<BigNumericValue> BigNumericValue::Cbrt() const {
  bool is_negative = value_.is_negative();
  UnsignedBinaryFraction<6, 254> value =
      SignedBinaryFraction<6, 254>(*this).Abs();
  UnsignedBinaryFraction<6, 254> cbrt;
  UnsignedBinaryFraction<6, 254> unit_of_last_precision(1, -144);
  BigNumericValue result;
  if (ABSL_PREDICT_TRUE(value.Cbrt(unit_of_last_precision, &cbrt)) &&
      ABSL_PREDICT_TRUE(cbrt.To(is_negative, &result))) {
    return result;
  }
  return googlesql_base::InternalErrorBuilder()
         << "CBRT should never overflow: CBRT(" << ToString() << ")";
}

// Parses a textual representation of a BIGNUMERIC value. Returns an error if
// the given string cannot be parsed as a number or if the textual numeric value
// exceeds BIGNUMERIC range. If 'is_strict' is true then the function will
// return an error if there are more that 38 digits in the fractional part,
// otherwise the number will be rounded to contain no more than 38 fractional
// digits.
template <internal::DigitTrimMode trim_mode>
absl::StatusOr<BigNumericValue> BigNumericValue::FromStringInternal(
    absl::string_view str, int64_t decimal_places) {
  constexpr uint8_t word_count = 4;
  BigNumericValue result;
  FixedPointRepresentation<word_count> parsed;
  absl::Status parse_status =
      ParseBigNumericWithRounding<trim_mode>(str, decimal_places, parsed);
  if (ABSL_PREDICT_TRUE(parse_status.ok()) &&
      ABSL_PREDICT_TRUE(
          result.value_.SetSignAndAbs(parsed.is_negative, parsed.output))) {
    return result;
  }
  return MakeInvalidBigNumericError(str);
}

absl::StatusOr<BigNumericValue> BigNumericValue::FromStringStrict(
    absl::string_view str) {
  return FromStringInternal<internal::DigitTrimMode::kError>(str, 38);
}

absl::StatusOr<BigNumericValue> BigNumericValue::FromString(
    absl::string_view str) {
  return FromStringInternal<
      internal::DigitTrimMode::kTrimRoundHalfAwayFromZero>(str, 38);
}

absl::StatusOr<BigNumericValue> BigNumericValue::FromStringWithRounding(
    absl::string_view str, int64_t decimal_places, bool round_half_even) {
  if (round_half_even) {
    return FromStringInternal<internal::DigitTrimMode::kTrimRoundHalfEven>(
        str, decimal_places);
  } else {
    return FromStringInternal<
        internal::DigitTrimMode::kTrimRoundHalfAwayFromZero>(str,
                                                             decimal_places);
  }
}

size_t BigNumericValue::HashCode() const {
  return absl::Hash<BigNumericValue>()(*this);
}

absl::StatusOr<BigNumericValue> BigNumericValue::FromDouble(double value) {
  if (ABSL_PREDICT_FALSE(!std::isfinite(value))) {
    // This error message should be kept consistent with the error message found
    // in .../public/functions/convert.h.
    if (std::isnan(value)) {
      // Don't show the negative sign for -nan values.
      value = std::numeric_limits<double>::quiet_NaN();
    }
    return MakeEvalError() << "Illegal conversion of non-finite floating point "
                              "number to BIGNUMERIC: "
                           << value;
  }
  FixedInt<64, 4> result;
  if (ScaleAndRoundAwayFromZero(kScalingFactor, value, &result)) {
    return BigNumericValue(result);
  }
  return MakeEvalError() << "BIGNUMERIC out of range: " << value;
}

void BigNumericValue::AppendToString(std::string* output) const {
  if (value_.is_zero()) {
    output->push_back('0');
    return;
  }
  size_t old_size = output->size();
  value_.AppendToString(output);
  size_t first_digit_index = old_size + value_.is_negative();
  AddDecimalPointAndAdjustZeros(first_digit_index, kMaxFractionalDigits, 0,
                                false, output);
}

void BigNumericValue::SerializeAndAppendToProtoBytes(std::string* bytes) const {
  value_.SerializeToBytes(bytes);
}

absl::StatusOr<BigNumericValue> BigNumericValue::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  BigNumericValue out;
  if (out.value_.DeserializeFromBytes(bytes)) {
    return out;
  }
  return MakeEvalError() << "Invalid BIGNUMERIC encoding";
}

void BigNumericValue::FormatAndAppend(FormatSpec spec,
                                      std::string* output) const {
  Format<kMaxFractionalDigits>(spec, value_, output);
}

std::ostream& operator<<(std::ostream& out, const BigNumericValue& value) {
  return out << value.ToString();
}

absl::StatusOr<BigNumericValue> BigNumericValue::FromScaledLittleEndianValue(
    absl::string_view little_endian_value, int source_scale,
    int fractional_digits_to_keep, bool allow_rounding, bool round_half_even) {
  FixedInt<64, 4> value;
  int destination_scale = BigNumericValue::kMaxFractionalDigits;
  int destination_max_integer_digits = BigNumericValue::kMaxIntegerDigits;
  if (!allow_rounding) {
    GOOGLESQL_ASSIGN_OR_RETURN(value, FixedIntFromScaledValue<RoundingMode::kTrunc>(
                                little_endian_value, source_scale,
                                fractional_digits_to_keep, destination_scale,
                                destination_max_integer_digits, "BIGNUMERIC"));
  } else if (round_half_even) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        value,
        FixedIntFromScaledValue<RoundingMode::kRoundHalfEven>(
            little_endian_value, source_scale, fractional_digits_to_keep,
            destination_scale, destination_max_integer_digits, "BIGNUMERIC"));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        value,
        FixedIntFromScaledValue<RoundingMode::kRoundHalfAwayFromZero>(
            little_endian_value, source_scale, fractional_digits_to_keep,
            destination_scale, destination_max_integer_digits, "BIGNUMERIC"));
  }
  return BigNumericValue(value);
}

absl::StatusOr<BigNumericValue> BigNumericValue::Rescale(
    int scale, bool allow_rounding) const {
  if (scale < 0 || scale > kMaxFractionalDigits) {
    return MakeEvalError() << absl::Substitute(
               "BIGNUMERIC scale must be between 0 and $0 but got $1",
               kMaxFractionalDigits, scale);
  }

  const FixedInt<64, 4>& divisor =
      FixedInt<64, 4>::PowerOf10(kMaxFractionalDigits - scale);
  FixedInt<64, 4> scaled_value(value_);
  if (allow_rounding) {
    scaled_value.DivAndRoundAwayFromZero(divisor);
  } else {
    FixedInt<64, 4> remainder;
    scaled_value.DivMod(divisor, &scaled_value, &remainder);
    if (!remainder.is_zero()) {
      return MakeEvalError() << absl::Substitute(
                 "Value will lose precision after scaling down to a scale of "
                 "$0",
                 scale);
    }
  }
  return BigNumericValue(scaled_value);
}

absl::StatusOr<BigNumericValue> BigNumericValue::SumAggregator::GetSum() const {
  if (sum_.number()[4] ==
      static_cast<uint64_t>(static_cast<int64_t>(sum_.number()[3]) >> 63)) {
    FixedInt<64, 4> sum_trunc(sum_);
    return BigNumericValue(sum_trunc);
  }
  return MakeEvalError() << "BIGNUMERIC overflow: SUM";
}

absl::StatusOr<BigNumericValue> BigNumericValue::SumAggregator::GetAverage(
    uint64_t count) const {
  if (count == 0) {
    return MakeEvalError() << "division by zero: AVG";
  }

  FixedInt<64, 5> dividend = sum_;
  dividend.DivAndRoundAwayFromZero(count);
  if (ABSL_PREDICT_TRUE(
          dividend.number()[4] ==
          static_cast<uint64_t>(static_cast<int64_t>(dividend.number()[3]) >>
                                63))) {
    FixedInt<64, 4> dividend_trunc(dividend);
    return BigNumericValue(dividend_trunc);
  }

  return MakeEvalError() << "BIGNUMERIC overflow: AVG";
}

void BigNumericValue::SumAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  sum_.SerializeToBytes(bytes);
}

absl::StatusOr<BigNumericValue::SumAggregator>
BigNumericValue::SumAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  BigNumericValue::SumAggregator out;
  if (out.sum_.DeserializeFromBytes(bytes)) {
    return out;
  }
  return MakeEvalError() << "Invalid BigNumericValue::SumAggregator encoding";
}

void BigNumericValue::VarianceAggregator::Add(BigNumericValue value) {
  const FixedInt<64, 4>& v = value.value_;
  sum_ += FixedInt<64, 5>(v);
  sum_square_ += FixedInt<64, 9>(ExtendAndMultiply(v, v));
}

void BigNumericValue::VarianceAggregator::Subtract(BigNumericValue value) {
  const FixedInt<64, 4>& v = value.value_;
  sum_ -= FixedInt<64, 5>(v);
  sum_square_ -= FixedInt<64, 9>(ExtendAndMultiply(v, v));
}

std::optional<double> BigNumericValue::VarianceAggregator::GetVariance(
    uint64_t count, bool is_sampling) const {
  uint64_t count_offset = is_sampling;
  if (count > count_offset) {
    return Covariance(sum_, sum_, sum_square_, BigNumericScalingFactorSquared(),
                      count, count_offset);
  }
  return std::nullopt;
}

std::optional<double> BigNumericValue::VarianceAggregator::GetStdDev(
    uint64_t count, bool is_sampling) const {
  uint64_t count_offset = is_sampling;
  if (count > count_offset) {
    return std::sqrt(Covariance(sum_, sum_, sum_square_,
                                BigNumericScalingFactorSquared(), count,
                                count_offset));
  }
  return std::nullopt;
}

void BigNumericValue::VarianceAggregator::MergeWith(
    const VarianceAggregator& other) {
  sum_ += other.sum_;
  sum_square_ += other.sum_square_;
}

void BigNumericValue::VarianceAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  SerializeFixedInt(bytes, sum_, sum_square_);
}

absl::StatusOr<BigNumericValue::VarianceAggregator>
BigNumericValue::VarianceAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  VarianceAggregator out;
  if (DeserializeFixedInt(bytes, &out.sum_, &out.sum_square_)) {
    return out;
  }
  return MakeEvalError()
         << "Invalid BigNumericValue::VarianceAggregator encoding";
}

void BigNumericValue::CovarianceAggregator::Add(BigNumericValue x,
                                                BigNumericValue y) {
  sum_x_ += FixedInt<64, 5>(x.value_);
  sum_y_ += FixedInt<64, 5>(y.value_);
  sum_product_ += FixedInt<64, 9>(ExtendAndMultiply(x.value_, y.value_));
}

void BigNumericValue::CovarianceAggregator::Subtract(BigNumericValue x,
                                                     BigNumericValue y) {
  sum_x_ -= FixedInt<64, 5>(x.value_);
  sum_y_ -= FixedInt<64, 5>(y.value_);
  sum_product_ -= FixedInt<64, 9>(ExtendAndMultiply(x.value_, y.value_));
}

std::optional<double> BigNumericValue::CovarianceAggregator::GetCovariance(
    uint64_t count, bool is_sampling) const {
  uint64_t count_offset = is_sampling;
  if (count > count_offset) {
    return Covariance(sum_x_, sum_y_, sum_product_,
                      BigNumericScalingFactorSquared(), count, count_offset);
  }
  return std::nullopt;
}

void BigNumericValue::CovarianceAggregator::MergeWith(
    const CovarianceAggregator& other) {
  sum_x_ += other.sum_x_;
  sum_y_ += other.sum_y_;
  sum_product_ += other.sum_product_;
}

void BigNumericValue::CovarianceAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  SerializeFixedInt(bytes, sum_product_, sum_x_, sum_y_);
}

absl::StatusOr<BigNumericValue::CovarianceAggregator>
BigNumericValue::CovarianceAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  CovarianceAggregator out;
  if (DeserializeFixedInt(bytes, &out.sum_product_, &out.sum_x_, &out.sum_y_)) {
    return out;
  }
  return MakeEvalError()
         << "Invalid BigNumericValue::CovarianceAggregator encoding";
}

void BigNumericValue::CorrelationAggregator::Add(BigNumericValue x,
                                                 BigNumericValue y) {
  cov_agg_.Add(x, y);
  sum_square_x_ += FixedInt<64, 9>(ExtendAndMultiply(x.value_, x.value_));
  sum_square_y_ += FixedInt<64, 9>(ExtendAndMultiply(y.value_, y.value_));
}

void BigNumericValue::CorrelationAggregator::Subtract(BigNumericValue x,
                                                      BigNumericValue y) {
  cov_agg_.Subtract(x, y);
  sum_square_x_ -= FixedInt<64, 9>(ExtendAndMultiply(x.value_, x.value_));
  sum_square_y_ -= FixedInt<64, 9>(ExtendAndMultiply(y.value_, y.value_));
}

std::optional<double> BigNumericValue::CorrelationAggregator::GetCorrelation(
    uint64_t count) const {
  if (count > 1) {
    FixedInt<64, 10> numerator = GetScaledCovarianceNumerator(
        cov_agg_.sum_x_, cov_agg_.sum_y_, cov_agg_.sum_product_, count);
    FixedInt<64, 10> variance_numerator_x = GetScaledCovarianceNumerator(
        cov_agg_.sum_x_, cov_agg_.sum_x_, sum_square_x_, count);
    FixedInt<64, 10> variance_numerator_y = GetScaledCovarianceNumerator(
        cov_agg_.sum_y_, cov_agg_.sum_y_, sum_square_y_, count);
    FixedInt<64, 20> denominator_square =
        ExtendAndMultiply(variance_numerator_x, variance_numerator_y);
    // If the denominator is outside the range of valid double
    // conversion we'll remove 5 words from the denominator and 2.5 from the
    // numerator.
    // To avoid precision loss in the numerator we treat it as a double
    // and divide by 2^160.
    bool negate = denominator_square.is_negative() != numerator.is_negative();
    FixedUint<64, 20> denominator_square_abs = denominator_square.abs();
    FixedUint<64, 10> numerator_abs = numerator.abs();
    double converted_numerator = static_cast<double>(numerator_abs);
    if (denominator_square_abs.NonZeroLength() > 15) {
      denominator_square_abs >>= 320;
      converted_numerator = std::ldexp(converted_numerator, -160);
    }
    return (negate ? -1 : 1) * converted_numerator /
           std::sqrt(
               static_cast<double>(FixedInt<64, 15>(denominator_square_abs)));
  }
  return std::nullopt;
}

void BigNumericValue::CorrelationAggregator::MergeWith(
    const CorrelationAggregator& other) {
  cov_agg_.MergeWith(other.cov_agg_);
  sum_square_x_ += other.sum_square_x_;
  sum_square_y_ += other.sum_square_y_;
}

void BigNumericValue::CorrelationAggregator::SerializeAndAppendToProtoBytes(
    std::string* bytes) const {
  SerializeFixedInt(bytes, cov_agg_.sum_product_, cov_agg_.sum_x_,
                    cov_agg_.sum_y_, sum_square_x_, sum_square_y_);
}

absl::StatusOr<BigNumericValue::CorrelationAggregator>
BigNumericValue::CorrelationAggregator::DeserializeFromProtoBytes(
    absl::string_view bytes) {
  CorrelationAggregator out;
  if (DeserializeFixedInt(bytes, &out.cov_agg_.sum_product_,
                          &out.cov_agg_.sum_x_, &out.cov_agg_.sum_y_,
                          &out.sum_square_x_, &out.sum_square_y_)) {
    return out;
  }
  return MakeEvalError()
         << "Invalid BigNumericValue::CorrelationAggregator encoding";
}

VarNumericValue VarNumericValue::FromScaledLittleEndianValue(
    absl::string_view little_endian_value, uint scale) {
  VarNumericValue result;
  result.scale_ = scale;
  if (!little_endian_value.empty()) {
    result.value_.resize((little_endian_value.size() + sizeof(uint64_t) - 1) /
                         sizeof(uint64_t));
    VarIntRef<64> var_int_ref(result.value_);
    bool success = var_int_ref.DeserializeFromBytes(little_endian_value);
    ABSL_DCHECK(success);
  }
  return result;
}

void VarNumericValue::AppendToString(std::string* output) const {
  ABSL_DCHECK(output != nullptr);
  size_t first_digit_index = output->size();
  ConstVarIntRef<64>(value_).AppendToString(output);
  if (output->size() == first_digit_index + 1 &&
      output->at(first_digit_index) == '0') {
    return;
  }
  first_digit_index += output->at(first_digit_index) == '-';
  AddDecimalPointAndAdjustZeros(first_digit_index, scale_, 0, false, output);
}

}  // namespace googlesql
