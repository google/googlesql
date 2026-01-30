//
// Copyright 2018 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MATHUTIL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MATHUTIL_H_

// This class is intended to contain a collection of useful (static)
// mathematical functions, properly coded (by consulting numerical
// recipes or another authoritative source first).

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "googlesql/base/check.h"
#include "googlesql/base/bits.h"

namespace googlesql_base {

class MathUtil {
 public:
  template<typename IntegralType>
  static IntegralType FloorOfRatio(IntegralType numerator,
                                   IntegralType denominator) {
    return CeilOrFloorOfRatio<IntegralType, false>(numerator, denominator);
  }

  template<typename IntegralType, bool ceil>
  static IntegralType CeilOrFloorOfRatio(IntegralType numerator,
                                         IntegralType denominator);

  // Returns the nonnegative remainder when one integer is divided by another.
  // The modulus must be positive.  Use integral types only (no float or
  // double).
  template <class T>
  static T NonnegativeMod(T a, T b) {
    static_assert(std::is_integral<T>::value, "Integral types only.");
    ABSL_DCHECK_GT(b, 0);
    // As of C++11 (per [expr.mul]/4), a%b is in (-b,0] for a<0, b>0.
    T c = a % b;
    return c + (c < 0) * b;
  }

  // --------------------------------------------------------------------
  // Round
  //   This function rounds a floating-point number to an integer. It
  //   works for positive or negative numbers.
  //
  //   Values that are halfway between two integers may be rounded up or
  //   down, for example Round<int>(0.5) == 0 and Round<int>(1.5) == 2.
  //   This allows the function to be implemented efficiently on multiple
  //   hardware platforms (see the template specializations at the bottom
  //   of this file). You should not use this function if you care about which
  //   way such half-integers are rounded.
  //
  //   Example usage:
  //     double y, z;
  //     int x = Round<int>(y + 3.7);
  //     int64_t b = Round<int64_t>(0.3 * z);
  //
  //   Note that the floating-point template parameter is typically inferred
  //   from the argument type, i.e. there is no need to specify it explicitly.
  // --------------------------------------------------------------------
  template <class IntOut, class FloatIn>
  static IntOut Round(FloatIn x) {
    static_assert(!std::numeric_limits<FloatIn>::is_integer,
                  "FloatIn is integer");
    static_assert(std::numeric_limits<IntOut>::is_integer,
                  "IntOut is not integer");

    // Note that there are specialized faster versions of this function for
    // Intel, ARM and PPC processors at the bottom of this file.
    if (x > -0.5 && x < 0.5) {
      // This case is special, because for largest floating point number
      // below 0.5, the addition of 0.5 yields 1 and this would lead
      // to incorrect result.
      return static_cast<IntOut>(0);
    }
    return static_cast<IntOut>(x < 0 ? (x - 0.5) : (x + 0.5));
  }

  // Returns the minimum integer value which is a multiple of rounding_value,
  // and greater than or equal to input_value.
  // The input_value must be greater than or equal to zero, and the
  // rounding_value must be greater than zero.
  template <typename IntType>
  static IntType RoundUpTo(IntType input_value, IntType rounding_value) {
    static_assert(std::numeric_limits<IntType>::is_integer,
                  "RoundUpTo() operation type is not integer");
    ABSL_DCHECK_GE(input_value, 0);
    ABSL_DCHECK_GT(rounding_value, 0);
    const IntType remainder = input_value % rounding_value;
    return (remainder == 0) ? input_value
                            : (input_value - remainder + rounding_value);
  }

  // --------------------------------------------------------------------
  // FastIntRound, FastInt64Round
  //   Fast routines for converting floating-point numbers to integers.
  //
  //   These routines are approximately 6 times faster than the default
  //   implementation of Round<int> on Intel processors (12 times faster on
  //   the Pentium 3).  They are also more than 5 times faster than simply
  //   casting a "double" to an "int" using static_cast<int>.  This is
  //   because casts are defined to truncate towards zero, which on Intel
  //   processors requires changing the rounding mode and flushing the
  //   floating-point pipeline (unless programs are compiled specifically
  //   for the Pentium 4, which has a new instruction to avoid this).
  //
  //   Numbers that are halfway between two integers may be rounded up or
  //   down.  This is because the conversion is done using the default
  //   rounding mode, which rounds towards the closest even number in case
  //   of ties.  So for example, FastIntRound(0.5) == 0, but
  //   FastIntRound(1.5) == 2.  These functions should only be used with
  //   applications that don't care about which way such half-integers are
  //   rounded.
  //
  //   There are template specializations of Round() which call these
  //   functions (for "int" and "int64_t" only), but it's safer to call them
  //   directly.
  //   --------------------------------------------------------------------

  static int32_t FastIntRound(double x) {
#if defined __GNUC__ && (defined __i386__ || defined __SSE2__ || \
                         defined __aarch64__ || defined __powerpc64__)
#if defined __AVX__
    // AVX.
    int32_t result;
    __asm__ __volatile__(
        "vcvtsd2si %1, %0"
        : "=r"(result)  // Output operand is a register
        : "xm"(x));     // Input operand is an xmm register or memory
    return result;
#elif defined __SSE2__
    // SSE2.
    int32_t result;
    __asm__ __volatile__(
        "cvtsd2si %1, %0"
        : "=r"(result)  // Output operand is a register
        : "xm"(x));     // Input operand is an xmm register or memory
    return result;
#elif defined __i386__
    // FPU stack.  Adapted from /usr/include/bits/mathinline.h.
    int32_t result;
    __asm__ __volatile__
        ("fistpl %0"
         : "=m" (result)    // Output operand is a memory location
         : "t" (x)          // Input operand is top of FP stack
         : "st");           // Clobbers (pops) top of FP stack
    return result;
#elif defined __aarch64__
    int64_t result;
    __asm__ __volatile__
        ("fcvtns %d0, %d1"
         : "=w"(result)     // Vector floating point register
         : "w"(x)           // Vector floating point register
         : /* No clobbers */);
    return static_cast<int32_t>(result);
#elif defined __powerpc64__
    int64_t result;
    __asm__ __volatile__
        ("fctid %0, %1"
         : "=d"(result)
         : "d"(x)
         : /* No clobbers */);
    return result;
#endif  // defined __powerpc64__
#else
    return Round<int32_t>(x);
#endif  // if defined __GNUC__ && ...
  }

  static int32_t FastIntRound(float x) {
#if defined __GNUC__ && (defined __i386__ || defined __SSE2__ || \
                         defined __aarch64__ || defined __powerpc64__)
#if defined __AVX__
    // AVX.
    int32_t result;
    __asm__ __volatile__(
        "vcvtss2si %1, %0"
        : "=r"(result)  // Output operand is a register
        : "xm"(x));     // Input operand is an xmm register or memory
    return result;
#elif defined __SSE2__
    // SSE2.
    int32_t result;
    __asm__ __volatile__(
        "cvtss2si %1, %0"
        : "=r"(result)  // Output operand is a register
        : "xm"(x));     // Input operand is an xmm register or memory
    return result;
#elif defined __i386__
    // FPU stack.  Adapted from /usr/include/bits/mathinline.h.
    int32_t result;
    __asm__ __volatile__
        ("fistpl %0"
         : "=m" (result)    // Output operand is a memory location
         : "t" (x)          // Input operand is top of FP stack
         : "st");           // Clobbers (pops) top of FP stack
    return result;
#elif defined __aarch64__
    int64_t result;
    __asm__ __volatile__
        ("fcvtns %s0, %s1"
         : "=w"(result)     // Vector floating point register
         : "w"(x)           // Vector floating point register
         : /* No clobbers */);
    return static_cast<int32_t>(result);
#elif defined __powerpc64__
    uint64_t output;
    __asm__ __volatile__
        ("fctiw %0, %1"
         : "=d"(output)
         : "f"(x)
         : /* No clobbers */);
    return absl::bit_cast<int32_t>(static_cast<uint32_t>(output >> 32));
#endif  // defined __powerpc64__
#else
    return Round<int32_t>(x);
#endif  // if defined __GNUC__ && ...
  }

  static int64_t FastInt64Round(double x) {
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__ || \
                         defined __aarch64__ || defined __powerpc64__)
#if defined __AVX__
    // AVX.
    int64_t result;
    __asm__ __volatile__(
        "vcvtsd2si %1, %0"
        : "=r"(result)  // Output operand is a register
        : "xm"(x));     // Input operand is an xmm register or memory
    return result;
#elif defined __x86_64__
    // SSE2.
    int64_t result;
    __asm__ __volatile__(
        "cvtsd2si %1, %0"
        : "=r"(result)  // Output operand is a register
        : "xm"(x));     // Input operand is an xmm register or memory
    return result;
#elif defined __i386__
    // There is no CVTSD2SI in i386 to produce a 64 bit int, even with SSE2.
    // FPU stack.  Adapted from /usr/include/bits/mathinline.h.
    int64_t result;
    __asm__ __volatile__
        ("fistpll %0"
         : "=m" (result)    // Output operand is a memory location
         : "t" (x)          // Input operand is top of FP stack
         : "st");           // Clobbers (pops) top of FP stack
    return result;
#elif defined __aarch64__
    // Floating-point convert to signed integer,
    // rounding to nearest with ties to even.
    int64_t result;
    __asm__ __volatile__
        ("fcvtns %d0, %d1"
         : "=w"(result)
         : "w"(x)
         : /* No clobbers */);
    return result;
#elif defined __powerpc64__
    int64_t result;
    __asm__ __volatile__
        ("fctid %0, %1"
         : "=d"(result)
         : "d"(x)
         : /* No clobbers */);
    return result;
#endif  // if defined __powerpc64__
#else
    return Round<int64_t>(x);
#endif  // if defined __GNUC__ && ...
  }

  static int64_t FastInt64Round(float x) {
    return FastInt64Round(static_cast<double>(x));
  }

  static int32_t FastIntRound(long double x) { return Round<int32_t>(x); }

  static int64_t FastInt64Round(long double x) { return Round<int64_t>(x); }

  // Smallest of two values
  // Works correctly for special floating point values.
  // Note: 0.0 and -0.0 are not differentiated by Min (Min(-0.0, 0.0) is 0.0),
  // which should be OK: see the comment for Max above.
  template<typename T>
  static T Min(const T x, const T y) {
    return std::isnan(x) || x < y ? x : y;
  }

  // Absolute value of x
  // Works correctly for unsigned types and
  // for special floating point values.
  // Note: 0.0 and -0.0 are not differentiated by Abs (Abs(0.0) is -0.0),
  // which should be OK: see the comment for Max above.
  template<typename T>
  static T Abs(const T x) {
    return x > T(0) ? x : -x;
  }

  // Absolute value of the difference between two floating point numbers.
  // Works correctly for special floating point values.
  template <typename T,
            std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
  static T AbsDiff(const T x, const T y) {
    return std::abs(x - y);
  }

  // Absolute value of the difference between two signed or unsigned integers.
  // The return type is always unsigned.
  template <typename T,
            std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                             bool> = true>
  static std::make_unsigned_t<T> AbsDiff(const T x, const T y) {
    // Carries out arithmetic as unsigned to avoid overflow.
    using R = std::make_unsigned_t<T>;
    return x > y ? R(x) - R(y) : R(y) - R(x);
  }

  // CAVEAT: Floating point computation instability for x86 CPUs
  // can frequently stem from the difference of when floating point values
  // are transferred from registers to memory and back,
  // which can depend simply on the level of optimization.
  // The reason is that the registers use a higher-precision representation.
  // Hence, instead of relying on approximate floating point equality below
  // it might be better to reorganize the code with volatile modifiers
  // for the floating point variables so as to control when
  // the loss of precision occurs.

  // If two (usually floating point) numbers are within a certain
  // absolute margin of error.
  template<typename T>
  static bool WithinMargin(const T x, const T y, const T margin) {
    ABSL_DCHECK_GE(margin, 0);
    // this is a little faster than x <= y + margin  &&  x >= y - margin
    return AbsDiff(x, y) <= margin;
  }

  // Decomposes `value` to the form `mantissa * pow(2, exponent)`.  Similar to
  // `std::frexp`, but returns `mantissa` as an integer without normalization.
  //
  // The returned `mantissa` might be a power of 2; this method does not shift
  // the trailing 0 bits out.
  //
  // If `value` is inf, then `mantissa = kint64max` and `exponent = intmax`.
  // If `value` is -inf, then `mantissa = -kint64max` and `exponent = intmax`.
  // If `value` is NaN, then `mantissa = 0` and `exponent = intmax`.
  // If `value` is 0, then `mantissa = 0` and `exponent < 0`.
  //
  // For all cases, `value` is equivalent to
  // `static_cast<double>(mantissa) * std::ldexp(1.0, exponent)`, though the
  // bits might differ (e.g., `-0.0` vs `0.0`, signaling NaN vs quiet NaN).
  //
  // For all cases except NaN,
  // `value = std::ldexp(static_cast<double>(mantissa), exponent)`.
  struct FloatParts {
    int32_t mantissa;
    int exponent;
  };
  static FloatParts Decompose(float value);
  struct DoubleParts {
    int64_t mantissa;
    int exponent;
  };
  static DoubleParts Decompose(double value);

  // Computes v^i, where i is a non-negative integer.
  // When T is a floating point type, this has the same semantics as pow(), but
  // is much faster.
  // T can also be any integral type, in which case computations will be
  // performed in the value domain of this integral type, and overflow semantics
  // will be those of T.
  // You can also use any type for which operator*= is defined.
  template <typename T>
  static T IPow(T base, int exp) {
    ABSL_DCHECK_GE(exp, 0);
    uint32_t uexp = exp;

    if (uexp < 16) {
      T result = (uexp & 1) ? base : static_cast<T>(1);
      if (uexp >= 2) {
        base *= base;
        if (uexp & 2) {
          result *= base;
        }
        if (uexp >= 4) {
          base *= base;
          if (uexp & 4) {
            result *= base;
          }
          if (uexp >= 8) {
            base *= base;
            result *= base;
          }
        }
      }
      return result;
    }

    T result = base;
    int count = 31 ^ Bits::Log2FloorNonZero(uexp);

    uexp <<= count;
    count ^= 31;

    while (count--) {
      uexp <<= 1;
      result *= result;
      if (uexp >= 0x80000000) {
        result *= base;
      }
    }

    return result;
  }

 private:
  // Wraps `x` to the periodic range `[low, high)`
  static double Wrap(double x, double low, double high);
};

// ========================================================================= //

#if defined __GNUC__ && (defined __i386__ || defined __x86_64__ || \
                         defined __aarch64__ || defined __powerpc64__)

// We define template specializations of Round() to get the more efficient
// Intel versions when possible.  Note that gcc does not currently support
// partial specialization of templatized functions.

template <>
inline int32_t MathUtil::Round<int32_t, float>(float x) {
  return FastIntRound(x);
}

template <>
inline int32_t MathUtil::Round<int32_t, double>(double x) {
  return FastIntRound(x);
}

template <>
inline int64_t MathUtil::Round<int64_t, float>(float x) {
  return FastInt64Round(x);
}

template <>
inline int64_t MathUtil::Round<int64_t, double>(double x) {
  return FastInt64Round(x);
}

#endif

// ---- CeilOrFloorOfRatio ----
// This is a branching-free, cast-to-double-free implementation.
//
// Casting to double is in general incorrect because of loss of precision
// when casting an int64_t into a double.
//
// There's a bunch of 'recipes' to compute a integer ceil (or floor) on the web,
// and most of them are incorrect.
template<typename IntegralType, bool ceil>
IntegralType MathUtil::CeilOrFloorOfRatio(IntegralType numerator,
                                          IntegralType denominator) {
  static_assert(std::numeric_limits<IntegralType>::is_integer,
                "CeilOfRatio is only defined for integral types");
  ABSL_DCHECK_NE(0, denominator) << "Division by zero is not supported.";
  ABSL_DCHECK(!std::numeric_limits<IntegralType>::is_signed ||
         numerator != std::numeric_limits<IntegralType>::lowest() ||
         denominator != -1)
      << "Dividing " << numerator << "by -1 is not supported: it would SIGFPE";

  const IntegralType rounded_toward_zero = numerator / denominator;
  const bool needs_round = (numerator % denominator) != 0;
  // It is important to use >= here, even for the denominator, to ensure that
  // this value is a compile-time constant for unsigned types.
  const bool same_sign = (numerator >= 0) == (denominator >= 0);

  if (ceil) {  // Compile-time condition: not an actual branching
    return rounded_toward_zero +
           static_cast<IntegralType>(same_sign && needs_round);
  } else {
    return rounded_toward_zero -
           static_cast<IntegralType>(!same_sign && needs_round);
  }
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MATHUTIL_H_
