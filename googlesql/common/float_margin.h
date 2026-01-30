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

#ifndef GOOGLESQL_COMMON_FLOAT_MARGIN_H_
#define GOOGLESQL_COMMON_FLOAT_MARGIN_H_

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>
#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/common/string_util.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/mathutil.h"

namespace googlesql {

class FloatMargin;
std::ostream& operator<<(std::ostream& out, const FloatMargin& float_margin);

// Represents a relative floating point error margin. Used in
// googlesql::Value::EqualsInternal() to compare test results. The error is
// specified in terms of the unit in the Nth-last place (ULP): see
// http://en.wikipedia.org/wiki/Unit_in_the_last_place.
//
// This file defines two instances of this class that should be sufficient for
// common use:
//   kExactFloatMargin: for exact comparison
//   kDefaultFloatMargin: for approximate comparison
//
// Most functions including IEEE 754 arithmetic operations are expected to
// return a value that is exact in all places of the mantissa. Maximum absolute
// error of such functions is 0.5 ULP and tests should specify the error as 0
// ULP.  If implementation is allowed to round the last bit in either direction
// then 1 ULP should be specified.
//
// A progressively exponentially larger error can be generated as
// FloatMargin::UlpMargin(n) for n=1, 2, etc. Progressive margins can be used to
// compare floating point results that were produced using computations
// potentially involving high precision loss such as trigonometry, running
// averages, etc.
class FloatMargin {
 public:
  static constexpr int kDefaultUlpBits = 4;

  // Maximal number N of bits N-th last ULPs. 2^100 is 1.3e+30, so it still
  // comfortably fits into a float.
  static constexpr int kMaxUlpBits = 100;

  // Creates an error corresponding to the unit in the Nth-last place (N=1 means
  // last, N=2 means last but one, etc.). Numbers within 2^bits*Ulp(1.0) of zero
  // are treated as equal. Valid inputs are [0..kMaxUlpBits].
  static constexpr FloatMargin UlpMargin(int bits) {
    return FloatMargin(bits, bits);
  }

  // Creates an error corresponding to the unit in the Nth-last place (N=1 means
  // last, N=2 means last but one, etc.). Zero is distinct from all non-zero
  // numbers. To be used only in cases where the nature of the computation is
  // known. Valid inputs are [0..kMaxUlpBits].
  static constexpr FloatMargin UlpMarginStrictZero(int bits) {
    return FloatMargin(bits, 0 /* zero_ulp_bits */);
  }

  // Creates a minimal error that is looser than both inputs.
  static FloatMargin MinimalLooseError(const FloatMargin& left,
                                       const FloatMargin& right) {
    return FloatMargin(std::max(left.ulp_bits_, right.ulp_bits_),
                       std::max(left.zero_ulp_bits_, right.zero_ulp_bits_));
  }

  bool IsExactEquality() const { return ulp_bits_ == 0; }

  // Compares two numbers for approximate or exact equality. NaNs are considered
  // equal to support comparison of composite values.
  template <typename T>
  bool Equal(T x, T y) const {
    if (x == y) {  // Covers +inf and -inf, and is a shortcut for finite values.
      return true;
    }
    if (std::numeric_limits<T>::is_integer) return false;
    if (std::isnan(x) && std::isnan(y)) return true;
    if (!std::isfinite(x) || !std::isfinite(y))
      return false;  // One of the values is finite, the other is not.
    T abs_error = MaxAbsDiff(x, y);
    return googlesql_base::MathUtil::WithinMargin(x, y, abs_error);
  }

  // For a given finite non-zero floating point number returns its unit in the
  // last place (ULP).
  template <typename T>
  static T Ulp(T result) {
    // Using a ABSL_CHECK instead of static_assert to make the code compile for
    // integer T.
    ABSL_CHECK(!std::numeric_limits<T>::is_integer)
        << "T must be a floating point type.";
    ABSL_CHECK_NE(0, result);
    ABSL_CHECK(std::isfinite(result));
    int exp;
    // Extract exponent. Because frexp() always returns a value less than one
    // this will be greater than the normalized exponent by 1.
    std::frexp(static_cast<double>(result), &exp);
    // Exponent may be less than min_exponent if the number is subnormal.
    // Adjust "exp" accordingly.
    exp = std::max(std::numeric_limits<T>::min_exponent, exp - 1);
    return std::ldexp(static_cast<double>(std::numeric_limits<T>::epsilon()),
                      exp);
  }

  template <typename T>
  std::string PrintError(T x, T y) const {
    if (Equal(x, y)) return "No error";
    if (std::isnan(x) != std::isnan(y) ||
        std::isfinite(x) != std::isfinite(y)) {
      return absl::StrCat("One of ", RoundTripDoubleToString(x), " and ",
                          RoundTripDoubleToString(y),
                          " is nan/inf while the other is not");
    }
    return absl::StrCat(
        "The difference ", RoundTripDoubleToString(googlesql_base::MathUtil::AbsDiff(x, y)),
        " between ", RoundTripDoubleToString(x), " and ",
        RoundTripDoubleToString(y), " exceeds maximal error ", DebugString(),
        " of ", RoundTripDoubleToString(MaxAbsDiff(x, y)));
  }

  std::string DebugString() const {
    if (IsExactEquality()) {
      return absl::StrCat("FloatMargin(exact)");
    }
    return absl::StrCat("FloatMargin(ulp_bits=", ulp_bits_,
                        ", zero_ulp_bits=", zero_ulp_bits_, ")");
  }

 private:
  constexpr FloatMargin(int ulp_bits, int zero_ulp_bits)
      : ulp_bits_(ulp_bits), zero_ulp_bits_(zero_ulp_bits) {}

  // Returns the maximal allowed error margin for x and y.
  template <typename T>
  T MaxAbsDiff(T x, T y) const {
    if (IsExactEquality()) return T(0);

    ABSL_CHECK(ulp_bits_ >= 0 && ulp_bits_ <= kMaxUlpBits &&
          zero_ulp_bits_ >= 0 && zero_ulp_bits_ <= kMaxUlpBits)
        << "Out of range float margin: " << *this;

    if (zero_ulp_bits_ > 0) {
      T zero_margin =
          googlesql_base::MathUtil::IPow(2.0, zero_ulp_bits_) * Ulp(static_cast<T>(1.0));
      ABSL_CHECK(std::isfinite(zero_margin)) << "Zero margin overflow: " << *this;
      if (googlesql_base::MathUtil::Abs(x) <= zero_margin && googlesql_base::MathUtil::Abs(y) <= zero_margin) {
        return zero_margin;
      }
    }

    T result = googlesql_base::MathUtil::IPow(2.0, ulp_bits_) *
               Ulp(std::max(googlesql_base::MathUtil::Abs(x), googlesql_base::MathUtil::Abs(y)));
    ABSL_CHECK(std::isfinite(result)) << "Float margin overflow: " << *this;
    return result;
  }

  // Number of ULP bits used for comparison. Zero means "exact comparison".
  // When comparing two numbers, these bit positions refer to the
  // larger-magnitude number.
  int ulp_bits_;

  // Number of ULP bits of 1.0 used for comparing numbers close to zero. Used
  // only if ulp_bits_ > 0.  Numbers close to zero cannot be compared using
  // relative error. For details, see http://randomascii.wordpress.com/
  // 2012/02/25/comparing-floating-point-numbers-2012-edition/.
  // If zero_ulp_bits_ = 0, zero is different from all non-zero numbers.  If
  // zero_ulp_bits_ = 5, zero margin becomes MathLimits<T>::kStdError.
  int zero_ulp_bits_;

  // Copyable by design.
};

// Compiler prevents us from declaring these constants inside the FloatMargin
// class itself.

// Use this float margin to require exact comparison.
static constexpr FloatMargin kExactFloatMargin = FloatMargin::UlpMargin(0);

// Use this for default comparison of floating point numbers. Unit in the
// 4th-last place, similar to GTest's EXPECT_FLOAT_EQ.
static constexpr FloatMargin kDefaultFloatMargin =
           FloatMargin::UlpMargin(FloatMargin::kDefaultUlpBits);

// Allow FloatMargin to be logged.
inline std::ostream& operator<<(std::ostream& out,
                                const FloatMargin& float_margin) {
  return out << float_margin.DebugString();
}

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_FLOAT_MARGIN_H_
