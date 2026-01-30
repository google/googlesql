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

// This file implements basic arithmetic operations with overflow checking.
// The following functions are defined:
//
//   bool Add(T in1, T in2, T *out, absl::Status* error);
//   bool Subtract(InType in1, InType in2, OutType *out, absl::Status* error);
//   bool Multiply(T in1, T in2, T *out, absl::Status* error);
//   bool Divide(T in1, T in2, T *out, absl::Status* error);
//   bool Modulo(T in1, T in2, T *out, absl::Status* error);
//   bool UnaryMinus(InType in, OutType *out, absl::Status* error);
//
// If overflow or any other error occurs, all functions return false and update
// *error.
// T must be a numeric type: either floating point type or integer type with
// size not greater than 64.
// Divide() is only defined for double.
// In case of Subtract(), OutType must be a signed type, InType may be signed
// or unsigned, but must not be wider than OutType.
// TODO: UnaryMinus() does not need to support different
// InType and OutType.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_ARITHMETICS_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_ARITHMETICS_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "googlesql/public/functions/arithmetics_internal.h"
#include "googlesql/public/functions/convert.h"
#include "googlesql/public/functions/util.h"
#include "googlesql/public/numeric_value.h"
#include "absl/base/optimization.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

namespace googlesql {
namespace functions {

template <typename T>
inline bool Add(T in1, T in2, T* out, absl::Status* error);
template <typename InType, typename OutType = InType>
inline bool Subtract(InType in1, InType in2, OutType* out, absl::Status* error);
template <typename T>
inline bool Multiply(T in1, T in2, T* out, absl::Status* error);
template <typename T>
inline bool Divide(T in1, T in2, T* out, absl::Status* error);
template <typename T>
inline bool Modulo(T in1, T in2, T* out, absl::Status* error);
template <typename InType, typename OutType = InType>
inline bool UnaryMinus(InType in, OutType* out, absl::Status* error);

// Division function for NUMERIC/BIGNUMERICs with integer semantics.
template <typename T>
inline bool DivideToIntegralValue(T in1, T in2, T* out, absl::Status* error);

// ----------------------- Internal parts -----------------------
// These are implementation details. Do not use outside of this file.

namespace internal {

template <typename T>
inline bool CheckFloatOverflow(T in1, T in2, absl::string_view operator_symbol,
                               T out, absl::Status* error) {
  if (ABSL_PREDICT_TRUE(std::isfinite(out))) {
    return true;
  } else if (!std::isfinite(in1) || !std::isfinite(in2)) {
    return true;
  } else {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, operator_symbol));
  }
}

// Checks if range of representable values of type From lies entirely within
// the range of representable values of type To. If this is true then From can
// be casted to To without overflows.
template <typename To, typename From>
struct is_safe_to_cast {
  static_assert(std::numeric_limits<To>::is_integer &&
                    std::numeric_limits<From>::is_integer,
                "is_safe_to_cast can only be used with integer types");
  static constexpr bool value =
      (std::numeric_limits<To>::digits >= std::numeric_limits<From>::digits) &&
      (std::numeric_limits<To>::is_signed >=
       std::numeric_limits<From>::is_signed);
};

template <typename To, typename From>
inline To safe_cast(From in) {
  static_assert(is_safe_to_cast<To, From>::value, "This cast is not safe.");
  return static_cast<To>(in);
}

}  // namespace internal

// ----------------------- Floating point -----------------------

template <>
inline bool Add(double in1, double in2, double* out, absl::Status* error) {
  *out = in1 + in2;
  return internal::CheckFloatOverflow(in1, in2, " + ", *out, error);
}

template <>
inline bool Subtract(double in1, double in2, double* out, absl::Status* error) {
  *out = in1 - in2;
  return internal::CheckFloatOverflow(in1, in2, " - ", *out, error);
}

template <>
inline bool Multiply(double in1, double in2, double* out, absl::Status* error) {
  *out = in1 * in2;
  return internal::CheckFloatOverflow(in1, in2, " * ", *out, error);
}
template <>
inline bool Divide(double in1, double in2, double* out, absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in2 == 0)) {
    return internal::UpdateError(error,
                                 internal::DivisionByZeroMessage(in1, in2));
  }
  *out = in1 / in2;
  if (ABSL_PREDICT_TRUE(std::isfinite(*out))) {
    return true;
  } else if (!std::isfinite(in1) || !std::isfinite(in2)) {
    return true;
  } else {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " / "));
  }
}
template <>
inline bool UnaryMinus(double in, double* out, absl::Status* error) {
  *out = -in;
  return true;
}

template <>
inline bool Add(long double in1, long double in2, long double* out,
                absl::Status* error) {
  *out = in1 + in2;
  return internal::CheckFloatOverflow(in1, in2, " + ", *out, error);
}

template <>
inline bool Subtract(long double in1, long double in2, long double* out,
                     absl::Status* error) {
  *out = in1 - in2;
  return internal::CheckFloatOverflow(in1, in2, " - ", *out, error);
}

template <>
inline bool Multiply(long double in1, long double in2, long double* out,
                     absl::Status* error) {
  *out = in1 * in2;
  return internal::CheckFloatOverflow(in1, in2, " * ", *out, error);
}
template <>
inline bool Divide(long double in1, long double in2, long double* out,
                   absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in2 == 0)) {
    return internal::UpdateError(error,
                                 internal::DivisionByZeroMessage(in1, in2));
  }
  *out = in1 / in2;
  if (ABSL_PREDICT_TRUE(std::isfinite(*out))) {
    return true;
  } else if (!std::isfinite(in1) || !std::isfinite(in2)) {
    return true;
  } else {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " / "));
  }
}
template <>
inline bool UnaryMinus(long double in, long double* out, absl::Status* error) {
  *out = -in;
  return true;
}

template <>
inline bool Add(float in1, float in2, float* out, absl::Status* error) {
  *out = in1 + in2;
  return internal::CheckFloatOverflow(in1, in2, " + ", *out, error);
}

template <>
inline bool Subtract(float in1, float in2, float* out, absl::Status* error) {
  *out = in1 - in2;
  return internal::CheckFloatOverflow(in1, in2, " - ", *out, error);
}

template <>
inline bool Multiply(float in1, float in2, float* out, absl::Status* error) {
  *out = in1 * in2;
  return internal::CheckFloatOverflow(in1, in2, " * ", *out, error);
}

template <>
inline bool UnaryMinus(float in, float* out, absl::Status* error) {
  *out = -in;
  return true;
}

// ----------------------- Integer -----------------------

// LLVM and Clang have builtin functions that implement overflow checking most
// efficiently (using overflow flag). We use them when they are available.
//   http://clang.llvm.org/docs/LanguageExtensions.html#builtin-functions
// Even when this builtins are available we may still need more generic code
// below when input types are not the same.
#if __has_builtin(__builtin_uadd_overflow)

static_assert(std::is_same<uint32_t, unsigned>::value,  // NOLINT(runtime/int)
              "unsigned != uint32?");
static_assert(std::is_same<int32_t, int>::value, "int != int32?");

// 64-bit integers may be either 'long' or 'long long' depending on the system,
// but we have to explicitly specify the underlying type in the name of the
// built-in function. We store the result in an intermediate value to work
// around that.
template <>
inline bool Add<uint64_t>(uint64_t in1, uint64_t in2, uint64_t* out,
                          absl::Status* error) {
  unsigned long long result;  // NOLINT(runtime/int)
  bool has_overflow = __builtin_uaddll_overflow(in1, in2, &result);
  *out = static_cast<uint64_t>(result);
  if (ABSL_PREDICT_FALSE(has_overflow)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " + "));
  } else {
    return true;
  }
}

template <>
inline bool Multiply<uint64_t>(uint64_t in1, uint64_t in2, uint64_t* out,
                               absl::Status* error) {
  unsigned long long result;  // NOLINT(runtime/int)
  bool has_overflow = __builtin_umulll_overflow(in1, in2, &result);
  *out = static_cast<uint64_t>(result);
  if (ABSL_PREDICT_FALSE(has_overflow)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " * "));
  } else {
    return true;
  }
}

template <>
inline bool Add<int32_t>(int32_t in1, int32_t in2, int32_t* out,
                         absl::Status* error) {
  if (ABSL_PREDICT_FALSE(__builtin_sadd_overflow(in1, in2, out))) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " + "));
  } else {
    return true;
  }
}

template <>
inline bool Add<int64_t>(int64_t in1, int64_t in2, int64_t* out,
                         absl::Status* error) {
  long long result;  // NOLINT(runtime/int)
  bool has_overflow = __builtin_saddll_overflow(in1, in2, &result);
  *out = static_cast<int64_t>(result);
  if (ABSL_PREDICT_FALSE(has_overflow)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " + "));
  } else {
    return true;
  }
}

template <>
inline bool Subtract<int64_t>(int64_t in1, int64_t in2, int64_t* out,
                              absl::Status* error) {
  long long result;  // NOLINT(runtime/int)
  bool has_overflow = __builtin_ssubll_overflow(in1, in2, &result);
  *out = static_cast<int64_t>(result);
  if (ABSL_PREDICT_FALSE(has_overflow)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " - "));
  } else {
    return true;
  }
}

template <>
inline bool Multiply<int32_t>(int32_t in1, int32_t in2, int32_t* out,
                              absl::Status* error) {
  if (ABSL_PREDICT_FALSE(__builtin_smul_overflow(in1, in2, out))) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " * "));
  } else {
    return true;
  }
}

template <>
inline bool Multiply<int64_t>(int64_t in1, int64_t in2, int64_t* out,
                              absl::Status* error) {
  long long result;  // NOLINT(runtime/int)
  bool has_overflow = __builtin_smulll_overflow(in1, in2, &result);
  *out = static_cast<int64_t>(result);
  if (ABSL_PREDICT_FALSE(has_overflow)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " * "));
  } else {
    return true;
  }
}

template <>
inline bool Multiply<absl::int128>(absl::int128 in1, absl::int128 in2,
                                   absl::int128* out, absl::Status* error) {
  __int128_t result;
  bool has_overflow = __builtin_mul_overflow(
      static_cast<__int128_t>(in1), static_cast<__int128_t>(in2), &result);

  *out = static_cast<absl::int128>(result);
  if (ABSL_PREDICT_FALSE(has_overflow)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " * "));
  }
  return true;
}

#else

namespace arithmetics_internal {

template <typename T>
inline bool CheckSaturatedOverflow(T in1, T in2,
                                   absl::string_view operator_symbol,
                                   Saturated<T> result, T* out,
                                   absl::Status* error) {
  *out = result.Value();
  if (ABSL_PREDICT_TRUE(result.IsValid())) {
    return true;
  } else {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, operator_symbol));
  }
}

}  // namespace arithmetics_internal

template <typename T>
bool Add(T in1, T in2, T* out, absl::Status* error) {
  arithmetics_internal::Saturated<T> result(internal::safe_cast<T>(in1));
  result.Add(internal::safe_cast<T>(in2));
  *out = result.Value();
  return arithmetics_internal::CheckSaturatedOverflow(in1, in2, " + ", result,
                                                      out, error);
}

template <>
inline bool Subtract<int64_t>(int64_t in1, int64_t in2, int64_t* out,
                              absl::Status* error) {
  arithmetics_internal::Saturated<int64_t> result(
      internal::safe_cast<int64_t>(in1));
  result.Sub(internal::safe_cast<int64_t>(in2));
  *out = result.Value();
  return arithmetics_internal::CheckSaturatedOverflow(in1, in2, " - ", result,
                                                      out, error);
}

template <typename T>
inline bool Multiply(T in1, T in2, T* out, absl::Status* error) {
  arithmetics_internal::Saturated<T> result(internal::safe_cast<T>(in1));
  result.Mul(internal::safe_cast<T>(in2));
  *out = result.Value();
  return arithmetics_internal::CheckSaturatedOverflow(in1, in2, " * ", result,
                                                      out, error);
}

#endif

template <typename T>
inline bool Modulo(T in1, T in2, T* out, absl::Status* error) {
  static_assert(
      std::is_same<uint64_t, T>::value || std::is_same<int64_t, T>::value,
      "Modulo only supports 64 bit integer");
  if (ABSL_PREDICT_FALSE(in2 == 0)) {
    return internal::UpdateError(
        error, absl::StrCat("division by zero: MOD(", in1, ", ", in2, ")"));
  }
  if constexpr (std::is_same_v<int64_t, T>) {
    if (ABSL_PREDICT_FALSE(in2 == -1)) {
      // Workaround for -9223372035808 % -1 triggering floating point exception.
      *out = 0;
      return true;
    }
  }
  *out = in1 % in2;
  return true;
}

static_assert(std::numeric_limits<int32_t>::min() +
                      std::numeric_limits<int32_t>::max() ==
                  -1,
              "int32 is not a two's complement type?");
static_assert(std::numeric_limits<int64_t>::min() +
                      std::numeric_limits<int64_t>::max() ==
                  -1,
              "int64 is not a two's complement type?");

template <>
inline bool UnaryMinus<int32_t, int32_t>(int32_t in, int32_t* out,
                                         absl::Status* error) {
  if (in == std::numeric_limits<int32_t>::min()) {
    return internal::UpdateError(error,
                                 internal::UnaryOverflowMessage(in, "-"));
  }
  *out = -in;
  return true;
}

template <>
inline bool UnaryMinus<int64_t, int64_t>(int64_t in, int64_t* out,
                                         absl::Status* error) {
  if (in == std::numeric_limits<int64_t>::min()) {
    return internal::UpdateError(error,
                                 internal::UnaryOverflowMessage(in, "-"));
  }
  *out = -in;
  return true;
}

template <>
inline bool Subtract<uint64_t, int64_t>(uint64_t in1, uint64_t in2,
                                        int64_t* out, absl::Status* error) {
  if (in1 >= in2) {
    uint64_t tmp = in1 - in2;
    if (!Convert<uint64_t, int64_t>(tmp, out, nullptr)) {
      return internal::UpdateError(
          error, internal::BinaryOverflowMessage(in1, in2, " - "));
    }
    return true;
  }
  uint64_t tmp = in2 - in1;
  if (ABSL_PREDICT_FALSE(
          tmp ==
          1ull + static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))) {
    *out = std::numeric_limits<int64_t>::min();
    return true;
  }
  if (!Convert<uint64_t, int64_t>(tmp, out, nullptr) ||
      !UnaryMinus<int64_t, int64_t>(*out, out, error)) {
    return internal::UpdateError(
        error, internal::BinaryOverflowMessage(in1, in2, " - "));
  }
  return true;
}

// TODO: Non-GoogleSQL-compliant temporary signature for
// UINT64 subtraction, to allow Spandex to continue building until it
// uses the correct new signature above.  Remove this once migrated.
template <>
inline bool Subtract<uint64_t, uint64_t>(uint64_t in1, uint64_t in2,
                                         uint64_t* out, absl::Status* error) {
  return internal::UpdateError(error, "invalid UINT64 subtraction signature");
}

template <>
inline bool Divide(uint64_t in1, uint64_t in2, uint64_t* out,
                   absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in2 == 0)) {
    return internal::UpdateError(error,
                                 internal::DivisionByZeroMessage(in1, in2));
  }
  *out = in1 / in2;
  return true;
}

template <>
inline bool Divide(int64_t in1, int64_t in2, int64_t* out,
                   absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in2 == -1)) {
    return UnaryMinus(in1, out, error);
  }
  if (ABSL_PREDICT_FALSE(in2 == 0)) {
    return internal::UpdateError(error,
                                 internal::DivisionByZeroMessage(in1, in2));
  }
  *out = in1 / in2;
  return true;
}

// ----------------------- Numeric -----------------------

template <>
inline bool Add(NumericValue in1, NumericValue in2, NumericValue* out,
                absl::Status* error) {
  absl::StatusOr<NumericValue> numeric_status = in1.Add(in2);
  if (ABSL_PREDICT_TRUE(numeric_status.ok())) {
    *out = numeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_status.status();
  }
  return false;
}

template <>
inline bool Subtract(NumericValue in1, NumericValue in2, NumericValue* out,
                     absl::Status* error) {
  absl::StatusOr<NumericValue> numeric_status = in1.Subtract(in2);
  if (ABSL_PREDICT_TRUE(numeric_status.ok())) {
    *out = numeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_status.status();
  }
  return false;
}

template <>
inline bool Multiply(NumericValue in1, NumericValue in2, NumericValue* out,
                     absl::Status* error) {
  absl::StatusOr<NumericValue> numeric_status = in1.Multiply(in2);
  if (ABSL_PREDICT_TRUE(numeric_status.ok())) {
    *out = numeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_status.status();
  }
  return false;
}

template <>
inline bool Divide(NumericValue in1, NumericValue in2, NumericValue* out,
                   absl::Status* error) {
  absl::StatusOr<NumericValue> numeric_status = in1.Divide(in2);
  if (ABSL_PREDICT_TRUE(numeric_status.ok())) {
    *out = numeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_status.status();
  }
  return false;
}

template <>
inline bool UnaryMinus(NumericValue in, NumericValue* out,
                       absl::Status* error) {
  *out = in.Negate();
  return true;
}

template <>
inline bool Modulo(NumericValue in1, NumericValue in2, NumericValue* out,
                   absl::Status* error) {
  absl::StatusOr<NumericValue> numeric_status = in1.Mod(in2);
  if (ABSL_PREDICT_TRUE(numeric_status.ok())) {
    *out = numeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_status.status();
  }
  return false;
}

// ----------------------- BIGNUMERIC -----------------------

template <>
inline bool Add(BigNumericValue in1, BigNumericValue in2, BigNumericValue* out,
                absl::Status* error) {
  absl::StatusOr<BigNumericValue> bignumeric_status = in1.Add(in2);
  if (ABSL_PREDICT_TRUE(bignumeric_status.ok())) {
    *out = *bignumeric_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_status.status();
  }
  return false;
}

template <>
inline bool Subtract(BigNumericValue in1, BigNumericValue in2,
                     BigNumericValue* out, absl::Status* error) {
  absl::StatusOr<BigNumericValue> bignumeric_status = in1.Subtract(in2);
  if (ABSL_PREDICT_TRUE(bignumeric_status.ok())) {
    *out = *bignumeric_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_status.status();
  }
  return false;
}

template <>
inline bool Multiply(BigNumericValue in1, BigNumericValue in2,
                     BigNumericValue* out, absl::Status* error) {
  absl::StatusOr<BigNumericValue> bignumeric_status = in1.Multiply(in2);
  if (ABSL_PREDICT_TRUE(bignumeric_status.ok())) {
    *out = *bignumeric_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_status.status();
  }
  return false;
}

template <>
inline bool Divide(BigNumericValue in1, BigNumericValue in2,
                   BigNumericValue* out, absl::Status* error) {
  absl::StatusOr<BigNumericValue> bignumeric_status = in1.Divide(in2);
  if (ABSL_PREDICT_TRUE(bignumeric_status.ok())) {
    *out = *bignumeric_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_status.status();
  }
  return false;
}

template <>
inline bool UnaryMinus(BigNumericValue in, BigNumericValue* out,
                       absl::Status* error) {
  absl::StatusOr<BigNumericValue> bignumeric_status = in.Negate();
  if (ABSL_PREDICT_TRUE(bignumeric_status.ok())) {
    *out = *bignumeric_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_status.status();
  }
  return false;
}

template <>
inline bool Modulo(BigNumericValue in1, BigNumericValue in2,
                   BigNumericValue* out, absl::Status* error) {
  absl::StatusOr<BigNumericValue> bignumeric_status = in1.Mod(in2);
  if (ABSL_PREDICT_TRUE(bignumeric_status.ok())) {
    *out = bignumeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_status.status();
  }
  return false;
}

// ----------------------- NUMERIC/BIGNUMERIC -----------------------
template <typename T>
inline bool DivideToIntegralValue(T in1, T in2, T* out, absl::Status* error) {
  absl::StatusOr<T> numeric_status = in1.DivideToIntegralValue(in2);
  if (ABSL_PREDICT_TRUE(numeric_status.ok())) {
    *out = numeric_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_status.status();
  }
  return false;
}

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_ARITHMETICS_H_
