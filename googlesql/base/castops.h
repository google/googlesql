//
// Copyright 2024 Google LLC
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
// X86 compatible cast library is provided to help programmers to clean up
// float-cast-overflow failures. Before you use one of these calls, please
// make sure the overflowed float value is expected, and not due to a bug
// somewhere else.

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_CASTOPS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_CASTOPS_H_

#include <cmath>
#include <cstdint>
#include <limits>

namespace googlesql_base {
namespace castops {

// Generic saturating cast function, for casting floating point type to integral
// type. If the truncated form of value is larger than the max value of the
// result type (including positive infinity), return the max value. If the
// truncated form of value is smaller than the min value of the result type
// (including negative infinity), return the min value. If the value is NaN,
// return 0. If the truncated form of value is in the
// representable range of the result type, return the rounded result.
// The purpose of this function is to provide a defined and dependable cast
// method no matter the input value is in or out of the representable
// range of the result type.
template <typename FloatType, typename ResultType>
ResultType SaturatingFloatToInt(FloatType value);

// Return true if the truncated form of value is in the representable range of
// the result type: MIN-1 < value < MAX+1.
template <typename FloatType, typename ResultType>
bool InRange(FloatType value);

// Return true if the value is in the representable range of the result type:
// MIN <= value <= MAX.
template <typename FloatType, typename ResultType>
bool InRangeNoTruncate(FloatType value);

// Casts a double to a float, clipping values that are outside the legal range
// of float to +/- infinity.  NaN is passed through.
float DoubleToFloat(double value);

// Same as above, but clips to a finite value, e.g. FLT_MAX rather than
// +infinity.  Note that even infinite input values are clipped to finite.
float DoubleToFiniteFloat(double value);

// Casts a long double to a double, clipping values that are outside the legal
// range of double to +/- infinity.
double LongDoubleToDouble(long double value);

// Casts a long double to a double, but clips to a finite value for both +/-
// infinity.eg DBL_MAX rather than +infinity
double LongDoubleToFiniteDouble(long double value);

}  // namespace castops

namespace x86compatible {

// Emulating X86-64's behavior of casting long double, double or float to
// int32. Comparing to SaturatingFloatToInt<FloatType, int32>, when the
// truncated form of value is out of the representable range of int32 or NaN,
// X86-64 always returns INT32_MIN.
template <typename FloatType>
int32_t ToInt32(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float to
// int64. Comparing to SaturatingFloatToInt<FloatType, int64>, when the
// truncated form of value is out of the representable range of int64 or NaN,
// X86-64 always returns INT64_MIN.
template <typename FloatType>
int64_t ToInt64(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float to
// uint32. Comparing to SaturatingFloatToInt<FloatType, uint32>, X86-64 does
// things differently when the truncated form of value is out of the
// representable range of uint32. Basically, X86-64 outputs
// (uint32)(int64)value.
template <typename FloatType>
uint32_t ToUint32(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float to
// uint64. Comparing to SaturatingFloatToInt<FloatType, uint64>, X86-64 does
// things differently when the truncated form of value is out of the
// representable range of uint64.
// Basically, X86-64 does:
//   if (value > UINT64_MAX) {
//     return 0;
//   } else if (value >=0 && value <= UINT64_MAX) {
//     return (uint64)value;
//   } else if (value >= INT64_MIN) {
//     return (uint64)(int64)value;
//   } else { // value is NaN or value < INT64_MIN
//     return (uint64)INT64_MIN;
//   }
// Interestingly, when value is NaN, LLVM and GCC outputs differently. We
// emulate what GCC does.
template <typename FloatType>
uint64_t ToUint64(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float into
// int16. Comparing to SaturatingFloatToInt<FloatType, int16>, X86-64 does
// things differently when the truncated form of value is out of the
// representable range of int16.
// Basically, X86-64 does:
// (int16)(int32)value.
template <typename FloatType>
int16_t ToInt16(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float into
// uint16. Comparing to SaturatingFloatToInt<FloatType, uint16>, X86-64 does
// things differently when the truncated form of value is out of the
// representable range of uint16.
// Basically, X86-64 does:
// (uint16)(int32)value.
template <typename FloatType>
uint16_t ToUint16(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float into
// signed char. Comparing to SaturatingFloatToInt<FloatType, signed char>,
// X86-64 does things differently when the truncated form of value is out of
// the representable range of signed char. Basically, X86-64 does:
// (signed char)(int32)value.
template <typename FloatType>
signed char ToSchar(FloatType value);

// Emulating X86-64's behavior of casting long double, double or float into
// unsigned char. Comparing to SaturatingFloatToInt<FloatType, unsigned char>,
// X86-64 does things differently when the truncated form of value is out of
// the representable range of unsigned char. Basically, X86-64 does:
// (unsigned char)(int32)value.
template <typename FloatType>
unsigned char ToUchar(FloatType value);

}  // namespace x86compatible

//////////////////////////////////////////////////////////////////
// Implementation details follow; clients should ignore.

namespace castops {
namespace internal {

template <typename T>
constexpr bool kTypeIsIntegral = std::numeric_limits<T>::is_specialized &&
                                 std::numeric_limits<T>::is_integer;

template <typename T>
constexpr bool kTypeIsFloating = std::numeric_limits<T>::is_specialized &&
                                 !std::numeric_limits<T>::is_integer;

// Return true if `numeric_limits<IntType>::max() + 1` can be represented
// precisely in FloatType.
template <typename FloatType, typename IntType>
constexpr bool CanRepresentMaxPlusOnePrecisely() {
  static_assert(kTypeIsFloating<FloatType>);
  static_assert(kTypeIsIntegral<IntType>);
  static_assert(std::numeric_limits<FloatType>::radix == 2);
  // An N-bit integer has a max of 2^N - 1 so max() + 1 is 2^N.
  constexpr bool sufficient_range_for_max =
      (std::numeric_limits<FloatType>::max_exponent - 1) >=
      std::numeric_limits<IntType>::digits;
  return sufficient_range_for_max;
}

// Return true if the truncated form of value is smaller than or equal to the
// MAX value of IntType. When the MAX value of IntType can not be represented
// precisely in FloatType, the comparison is tricky, because the MAX value of
// IntType is promoted to a FloatType value that is actually greater than what
// IntType can handle. Also note that when value is nan, this function will
// return false.
template <typename FloatType, typename IntType, bool Truncate>
bool SmallerThanOrEqualToIntMax(FloatType value) {
  static_assert(kTypeIsFloating<FloatType>);
  static_assert(kTypeIsIntegral<IntType>);
  static_assert(std::numeric_limits<FloatType>::radix == 2);

  if constexpr (!Truncate) {
    // We are checking if the untruncated result is <= 2^N-1. This is equivalent
    // to asking if the rounded up value is < 2^N.
    value = std::ceil(value);
  }
  if constexpr (CanRepresentMaxPlusOnePrecisely<FloatType, IntType>()) {
    // We construct our exclusive upper bound carefully as we cannot construct
    // the integer `1 << N` in the obvious way as it would result in undefined
    // behavior (shifting past the width of the type).
    // N.B. We don't use std::ldexp because it does not constant fold.
    auto int_max_plus_one =
        static_cast<FloatType>(static_cast<IntType>(1)
                               << (std::numeric_limits<IntType>::digits - 1)) *
        static_cast<FloatType>(2);
    return value < int_max_plus_one;
  } else {
    if (value <= 0) {
      return true;
    }
    if (!std::isfinite(value)) {
      return false;
    }
    // Set exp such that value == f * 2^exp for some f in [0.5, 1.0).
    // Note that this implies that the magnitude of value is strictly less than
    // 2^exp.
    int exp = 0;
    std::frexp(value, &exp);

    // Let N be the number of non-sign bits in the representation of IntType.
    // If the magnitude of value is strictly less than 2^N, the truncated
    // version of value is representable as IntType.
    return exp <= std::numeric_limits<IntType>::digits;
  }
}

// Return true if the truncated form of value is greater than or equal to the
// MIN value of IntType. When the MIN value of IntType can not be represented
// precisely in FloatType, the comparison is tricky, because the MIN value of
// IntType is promoted to a FloatType value that is actually greater than what
// IntType can handle. Also note that when value is nan, this function will
// return false.
template <typename FloatType, typename IntType, bool Truncate>
bool GreaterThanOrEqualToIntMin(FloatType value) {
  static_assert(kTypeIsFloating<FloatType>);
  static_assert(kTypeIsIntegral<IntType>);
  static_assert(std::numeric_limits<FloatType>::radix == 2);
  if constexpr (!std::numeric_limits<IntType>::is_signed) {
    if constexpr (Truncate) {
      // We are checking if the truncated result is >= 0. This is equivalent to
      // asking if value is larger than -1 as negative numbers in (-1, 0) will
      // be truncated to 0.
      return value > static_cast<FloatType>(-1.0);
    } else {
      // We are checking if the untruncated result is >= 0. Our value must be
      // non-negative.
      return value >= static_cast<FloatType>(0.0);
    }
  }
  if constexpr (Truncate) {
    // We are checking if the truncated result is >= -(2^N). Truncate `value`
    // before performing the comparison.
    value = std::trunc(value);
  }
  if constexpr (CanRepresentMaxPlusOnePrecisely<FloatType, IntType>()) {
    auto int_min = static_cast<FloatType>(std::numeric_limits<IntType>::min());
    return value >= int_min;
  } else {
    if (!std::isfinite(value)) {
      return false;
    }
    if (value >= static_cast<FloatType>(0.0)) {
      return true;
    }
    // Set exp such that value == f * 2^exp for some f in (-1.0, -0.5].
    // Note that this implies that the magnitude of value is strictly less than
    // 2^exp.
    int exp = 0;
    FloatType f = std::frexp(value, &exp);

    // Let N be the number of non-sign bits in the representation of IntType.
    // If the magnitude of value is less than or equal to 2^N, the value is
    // representable as IntType.
    return exp < std::numeric_limits<IntType>::digits + 1 ||
           (exp == std::numeric_limits<IntType>::digits + 1 && f == -0.5);
  }
}

template <typename FloatType, typename ResultType, bool Truncate>
bool InRange(FloatType value) {
  static_assert(kTypeIsFloating<FloatType>);
  static_assert(kTypeIsIntegral<ResultType>);
  static_assert(sizeof(ResultType) <= 16);
  static_assert(sizeof(FloatType) >= 4);
  return SmallerThanOrEqualToIntMax<FloatType, ResultType, Truncate>(value) &&
         GreaterThanOrEqualToIntMin<FloatType, ResultType, Truncate>(value);
}

}  // namespace internal

// Return true if the truncated form of value is in the representable range of
// the result type, eg. MIN-1 < value < MAX+1.
template <typename FloatType, typename ResultType>
bool InRange(FloatType value) {
  return castops::internal::InRange<FloatType, ResultType, true>(value);
}

// Return true if the value is in the representable range of the result type:
// MIN <= value <= MAX.
template <typename FloatType, typename ResultType>
bool InRangeNoTruncate(FloatType value) {
  return castops::internal::InRange<FloatType, ResultType, false>(value);
}

template <typename FloatType, typename ResultType>
ResultType SaturatingFloatToInt(FloatType value) {
  static_assert(internal::kTypeIsFloating<FloatType>);
  static_assert(internal::kTypeIsIntegral<ResultType>);
  static_assert(sizeof(ResultType) <= 16);
  static_assert(sizeof(FloatType) >= 4);
  if (std::isnan(value)) {
    // If value is NaN.
    return 0;
  } else if (!castops::internal::SmallerThanOrEqualToIntMax<
                 FloatType, ResultType, /*Truncate=*/true>(value)) {
    // If value > MAX
    return std::numeric_limits<ResultType>::max();
  } else if (!castops::internal::GreaterThanOrEqualToIntMin<
                 FloatType, ResultType, /*Truncate=*/true>(value)) {
    // If value < MIN
    return std::numeric_limits<ResultType>::min();
  } else {
    // Value is in the representable range of the result type.
    return static_cast<ResultType>(value);
  }
}

inline float DoubleToFloat(double value) {
  // If value is NaN, both clauses will evaluate to false.
  if (value < std::numeric_limits<float>::lowest())
    return -std::numeric_limits<float>::infinity();
  if (value > std::numeric_limits<float>::max())
    return std::numeric_limits<float>::infinity();

  return static_cast<float>(value);
}

inline float DoubleToFiniteFloat(double value) {
  // If value is NaN, both clauses will evaluate to false.
  if (value < std::numeric_limits<float>::lowest())
    return std::numeric_limits<float>::lowest();
  if (value > std::numeric_limits<float>::max())
    return std::numeric_limits<float>::max();

  return static_cast<float>(value);
}

inline double LongDoubleToDouble(long double value) {
  // If value is NaN, both clauses will evaluate to false.
  if (value < std::numeric_limits<double>::lowest())
    return -std::numeric_limits<double>::infinity();
  if (value > std::numeric_limits<double>::max())
    return std::numeric_limits<double>::infinity();

  return static_cast<double>(value);
}

inline double LongDoubleToFiniteDouble(long double value) {
  // If value is NaN, both clauses will evaluate to false.
  if (value < std::numeric_limits<double>::lowest())
    return std::numeric_limits<double>::lowest();
  if (value > std::numeric_limits<double>::max())
    return std::numeric_limits<double>::max();

  return static_cast<double>(value);
}
}  // namespace castops

namespace x86compatible {

template <typename FloatType>
int32_t ToInt32(FloatType value) {
  static_assert(castops::internal::kTypeIsFloating<FloatType>);
  if (castops::internal::GreaterThanOrEqualToIntMin<FloatType, int32_t,
                                                    /*Truncate=*/true>(value) &&
      castops::internal::SmallerThanOrEqualToIntMax<FloatType, int32_t,
                                                    /*Truncate=*/true>(value)) {
    return static_cast<int32_t>(value);
  } else {
    // For out-of-bound value, including NaN, x86_64 returns INT32_MIN.
    return std::numeric_limits<int32_t>::min();
  }
}

template <typename FloatType>
int64_t ToInt64(FloatType value) {
  static_assert(castops::internal::kTypeIsFloating<FloatType>);
  if (castops::internal::GreaterThanOrEqualToIntMin<FloatType, int64_t,
                                                    /*Truncate=*/true>(value) &&
      castops::internal::SmallerThanOrEqualToIntMax<FloatType, int64_t,
                                                    /*Truncate=*/true>(value)) {
    return static_cast<int64_t>(value);
  } else {
    return std::numeric_limits<int64_t>::min();
  }
}

template <typename FloatType>
uint32_t ToUint32(FloatType value) {
  static_assert(castops::internal::kTypeIsFloating<FloatType>);
  return static_cast<uint32_t>(ToInt64<FloatType>(value));
}

template <typename FloatType>
uint64_t ToUint64(FloatType value) {
  static_assert(castops::internal::kTypeIsFloating<FloatType>);
  if (value >= static_cast<FloatType>(0.0) &&
      castops::internal::SmallerThanOrEqualToIntMax<FloatType, uint64_t,
                                                    /*Truncate=*/true>(value)) {
    return static_cast<uint64_t>(value);
  } else if (value < static_cast<FloatType>(0.0) &&
             castops::internal::GreaterThanOrEqualToIntMin<FloatType, int64_t,
                                                           /*Truncate=*/true>(
                 value)) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
  } else if (value > static_cast<FloatType>(0.0)) {
    return 0;
  } else {
    // If value < INT64_MIN or value is NaN. This is tricky when value is NaN,
    // because LLVM and GCC output differently. We mimic what GCC does.
    return static_cast<uint64_t>(std::numeric_limits<int64_t>::min());
  }
}

namespace internal {

// Emulating X86-64's behavior of casting long double, double or float into
// small integral types whose size is smaller than 4 bytes. Comparing to
// SaturatingFloatToInt<FloatType, SmallInt>, X86-64 does things differently
// when value is out of the representable range of SmallInt.
// Basically, X86-64 does:
// (SmallInt)(int32)value.
template <typename FloatType, typename SmallInt>
SmallInt ToSmallIntegral(FloatType value) {
  static_assert(castops::internal::kTypeIsFloating<FloatType>);
  static_assert(castops::internal::kTypeIsIntegral<SmallInt>);
  static_assert(sizeof(SmallInt) < 4);
  return static_cast<SmallInt>(ToInt32<FloatType>(value));
}

}  // namespace internal

template <typename FloatType>
int16_t ToInt16(FloatType value) {
  return internal::ToSmallIntegral<FloatType, int16_t>(value);
}

template <typename FloatType>
uint16_t ToUint16(FloatType value) {
  return internal::ToSmallIntegral<FloatType, uint16_t>(value);
}

template <typename FloatType>
signed char ToSchar(FloatType value) {
  return internal::ToSmallIntegral<FloatType, signed char>
      (value);
}

template <typename FloatType>
unsigned char ToUchar(FloatType value) {
  return internal::ToSmallIntegral<FloatType, unsigned char>
      (value);
}

}  // namespace x86compatible
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_CASTOPS_H_
