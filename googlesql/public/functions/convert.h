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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_H_

// Implements pairwise conversion between the following types: int32, int64,
// uint32, uint64, bool, float, double, NumericValue, and BigNumericValue. The
// conversion function has the following signature:
//
//   template <typename FromType, typename ToType>
//   bool Convert(const FromType& in, ToType* out, absl::Status* error);
//
// Upon success the function sets 'out' to the converted value and ignores the
// 'error' parameter. If conversion fails, the function returns false, updates
// 'error' if 'error' != nullptr, and possibly sets the 'out' parameter to a
// bogus value.
//
// Floating point numbers are rounded when converted to integers.

#include <math.h>  // for round and roundf

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "googlesql/base/logging.h"
#include "googlesql/public/functions/convert_internal.h"
#include "googlesql/public/functions/util.h"
#include "googlesql/public/numeric_value.h"
#include "absl/base/optimization.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// Do not use any methods from the internal namespace.
namespace internal {

extern const char* const kConvertOverflowInt32;
extern const char* const kConvertOverflowInt64;
extern const char* const kConvertOverflowUint32;
extern const char* const kConvertOverflowUint64;
extern const char* const kConvertOverflowFloat;
extern const char* const kConvertNonFinite;

// Check to see if a pointer to POD points to a value within a valid range.
// This function assumes that the FromType is wider than the ToType;
// that is, any possible ToType instance i satisfies:
//   numeric_limits<FromType>::lowest() <= i <= numeric_limits<FromType>::max()
template <typename FromType, typename ToType>
static inline bool CheckRange(const FromType& value) {
  static_assert(sizeof(FromType) > sizeof(ToType),
                "FromType must be larger than ToType");
  // Not a static_assert since floating point PODs are not integral constants.
  ABSL_DCHECK_LE(std::numeric_limits<FromType>::lowest(),
            std::numeric_limits<ToType>::lowest());
  ABSL_DCHECK_GE(std::numeric_limits<FromType>::max(),
            std::numeric_limits<ToType>::max());
  FromType min = static_cast<FromType>(std::numeric_limits<ToType>::lowest());
  FromType max = static_cast<FromType>(std::numeric_limits<ToType>::max());
  // Not a static_assert since floating point PODs are not integral constants.
  ABSL_DCHECK_LE(min, 0);
  ABSL_DCHECK_LT(0, max);
  return value >= min && value <= max;
}

template <typename FromType, typename ToType>
static inline bool CheckFloatToIntRange(const FromType& value) {
  static_assert((std::is_same<FromType, float>::value ||
                 std::is_same<FromType, double>::value),
                "Invalid FromType");
  static_assert((std::is_same<ToType, int32_t>::value ||
                 std::is_same<ToType, int64_t>::value ||
                 std::is_same<ToType, uint32_t>::value ||
                 std::is_same<ToType, uint64_t>::value),
                "Invalid ToType");
  return convert_internal::InRangeNoTruncate<FromType, ToType>(value);
}

// We use an internal struct since function template partial specialization is
// not allowed. Baseline template simply does a static_cast(). It also covers
// the case when FromType is the same as ToType.
template <typename FromType, typename ToType> struct Converter {
  static inline bool Convert(
      const FromType& in, ToType* out, absl::Status* error) {
    *out = static_cast<ToType>(in);
    return true;
  }
};

// Partial specialization for all conversions to a Boolean value.
template <typename FromType> struct Converter<FromType, bool> {
  static inline bool Convert(
      const FromType& in, bool* out, absl::Status* error) {
    *out = (in != 0);
    return true;
  }
};

}  // namespace internal

template <typename FromType, typename ToType>
inline bool Convert(
    const FromType& in, ToType* out, absl::Status* error) {
  static_assert((std::is_same<FromType, int32_t>::value ||
                 std::is_same<FromType, int64_t>::value ||
                 std::is_same<FromType, uint32_t>::value ||
                 std::is_same<FromType, uint64_t>::value ||
                 std::is_same<FromType, bool>::value ||
                 std::is_same<FromType, float>::value ||
                 std::is_same<FromType, double>::value ||
                 std::is_same<FromType, NumericValue>::value ||
                 std::is_same<FromType, BigNumericValue>::value),
                "Invalid FromType");
  static_assert((std::is_same<ToType, int32_t>::value ||
                 std::is_same<ToType, int64_t>::value ||
                 std::is_same<ToType, uint32_t>::value ||
                 std::is_same<ToType, uint64_t>::value ||
                 std::is_same<ToType, bool>::value ||
                 std::is_same<ToType, float>::value ||
                 std::is_same<ToType, double>::value ||
                 std::is_same<ToType, NumericValue>::value ||
                 std::is_same<ToType, BigNumericValue>::value),
                "Invalid ToType");
  return internal::Converter<FromType, ToType>::Convert(in, out, error);
}

// The following specializations are done only for the cases where static_cast()
// is not sufficient and overflow checking is required.

// -------------- int32 --------------

template <>
inline bool Convert<int32_t, uint32_t>(const int32_t& in, uint32_t* out,
                                       absl::Status* error) {
  if (in < 0) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint32, in));
  }
  *out = static_cast<uint32_t>(in);
  return true;
}

template <>
inline bool Convert<int32_t, uint64_t>(const int32_t& in, uint64_t* out,
                                       absl::Status* error) {
  if (in < 0) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint64, in));
  }
  *out = static_cast<uint64_t>(in);
  return true;
}

template <>
inline bool Convert<int32_t, NumericValue>(const int32_t& in, NumericValue* out,
                                           absl::Status* error) {
  *out = NumericValue(in);
  return true;
}

template <>
inline bool Convert<int32_t, BigNumericValue>(const int32_t& in,
                                              BigNumericValue* out,
                                              absl::Status* error) {
  *out = BigNumericValue(in);
  return true;
}

// -------------- int64 --------------

template <>
inline bool Convert<int64_t, int32_t>(const int64_t& in, int32_t* out,
                                      absl::Status* error) {
  if (!internal::CheckRange<int64_t, int32_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt32, in));
  }
  *out = static_cast<int32_t>(in);
  return true;
}

template <>
inline bool Convert<int64_t, uint32_t>(const int64_t& in, uint32_t* out,
                                       absl::Status* error) {
  if (!internal::CheckRange<int64_t, uint32_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint32, in));
  }
  *out = static_cast<uint32_t>(in);
  return true;
}

template <>
inline bool Convert<int64_t, uint64_t>(const int64_t& in, uint64_t* out,
                                       absl::Status* error) {
  if (in < 0) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint64, in));
  }
  *out = static_cast<uint64_t>(in);
  return true;
}

template <>
inline bool Convert<int64_t, NumericValue>(const int64_t& in, NumericValue* out,
                                           absl::Status* error) {
  *out = NumericValue(in);
  return true;
}

template <>
inline bool Convert<int64_t, BigNumericValue>(const int64_t& in,
                                              BigNumericValue* out,
                                              absl::Status* error) {
  *out = BigNumericValue(in);
  return true;
}

// -------------- uint32 --------------

template <>
inline bool Convert<uint32_t, int32_t>(const uint32_t& in, int32_t* out,
                                       absl::Status* error) {
  if (!internal::CheckRange<int64_t, int32_t>(static_cast<int64_t>(in))) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt32, in));
  }
  *out = static_cast<int32_t>(in);
  return true;
}

template <>
inline bool Convert<uint32_t, NumericValue>(const uint32_t& in,
                                            NumericValue* out,
                                            absl::Status* error) {
  *out = NumericValue(in);
  return true;
}

template <>
inline bool Convert<uint32_t, BigNumericValue>(const uint32_t& in,
                                               BigNumericValue* out,
                                               absl::Status* error) {
  *out = BigNumericValue(in);
  return true;
}

// -------------- uint64 --------------

template <>
inline bool Convert<uint64_t, int32_t>(const uint64_t& in, int32_t* out,
                                       absl::Status* error) {
  if (in > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt32, in));
  }
  *out = static_cast<int32_t>(in);
  return true;
}

template <>
inline bool Convert<uint64_t, int64_t>(const uint64_t& in, int64_t* out,
                                       absl::Status* error) {
  if (in > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt64, in));
  }
  *out = static_cast<int64_t>(in);
  return true;
}

template <>
inline bool Convert<uint64_t, uint32_t>(const uint64_t& in, uint32_t* out,
                                        absl::Status* error) {
  if (in > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint32, in));
  }
  *out = static_cast<uint32_t>(in);
  return true;
}

template <>
inline bool Convert<uint64_t, NumericValue>(const uint64_t& in,
                                            NumericValue* out,
                                            absl::Status* error) {
  *out = NumericValue(in);
  return true;
}

template <>
inline bool Convert<uint64_t, BigNumericValue>(const uint64_t& in,
                                               BigNumericValue* out,
                                               absl::Status* error) {
  *out = BigNumericValue(in);
  return true;
}

// -------------- bool --------------

// We have to provide a specialization for this due to template instantations
// used in tests, even though GoogleSQL does not support casting between BOOL
// and NUMERIC/BIGNUMERIC.
template <>
inline bool Convert<bool, NumericValue>(const bool& in, NumericValue* out,
                                        absl::Status* error) {
  *out = NumericValue(static_cast<int64_t>(in));
  return true;
}

template <>
inline bool Convert<bool, BigNumericValue>(const bool& in, BigNumericValue* out,
                                           absl::Status* error) {
  *out = BigNumericValue(static_cast<int64_t>(in));
  return true;
}

// -------------- float --------------

template <>
inline bool Convert<float, int32_t>(const float& in, int32_t* out,
                                    absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<float, int32_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt32, in));
  }
  *out = static_cast<int32_t>(roundf(in));
  return true;
}

template <>
inline bool Convert<float, int64_t>(const float& in, int64_t* out,
                                    absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<float, int64_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt64, in));
  }
  *out = static_cast<int64_t>(roundf(in));
  return true;
}

template <>
inline bool Convert<float, uint32_t>(const float& in, uint32_t* out,
                                     absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<float, uint32_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint32, in));
  }
  *out = static_cast<uint32_t>(roundf(in));
  return true;
}

template <>
inline bool Convert<float, uint64_t>(const float& in, uint64_t* out,
                                     absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<float, uint64_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint64, in));
  }
  *out = static_cast<uint64_t>(roundf(in));
  return true;
}

template <> inline bool Convert<float, bool>(
    const float& in, bool* out, absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  *out = (in != 0);
  return true;
}

template <>
inline bool Convert<float, NumericValue>(const float& in, NumericValue* out,
                                         absl::Status* error) {
  const absl::StatusOr<NumericValue> numeric_value_status =
      NumericValue::FromDouble(in);
  if (ABSL_PREDICT_TRUE(numeric_value_status.ok())) {
    *out = numeric_value_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_value_status.status();
  }
  return false;
}

template <>
inline bool Convert<float, BigNumericValue>(const float& in,
                                            BigNumericValue* out,
                                            absl::Status* error) {
  const absl::StatusOr<BigNumericValue> bignumeric_value_status =
      BigNumericValue::FromDouble(in);
  if (ABSL_PREDICT_TRUE(bignumeric_value_status.ok())) {
    *out = *bignumeric_value_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_value_status.status();
  }
  return false;
}

// -------------- double --------------

template <>
inline bool Convert<double, int32_t>(const double& in, int32_t* out,
                                     absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<double, int32_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt32, in));
  }
  *out = static_cast<int32_t>(round(in));
  return true;
}

template <>
inline bool Convert<double, int64_t>(const double& in, int64_t* out,
                                     absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<double, int64_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowInt64, in));
  }
  *out = static_cast<int64_t>(round(in));
  return true;
}

template <>
inline bool Convert<double, uint32_t>(const double& in, uint32_t* out,
                                      absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<double, uint32_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint32, in));
  }
  *out = static_cast<uint32_t>(round(in));
  return true;
}

template <>
inline bool Convert<double, uint64_t>(const double& in, uint64_t* out,
                                      absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  if (!internal::CheckFloatToIntRange<double, uint64_t>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowUint64, in));
  }
  *out = static_cast<uint64_t>(round(in));
  return true;
}

template <> inline bool Convert<double, bool>(
    const double& in, bool* out, absl::Status* error) {
  if (!std::isfinite(in)) {
    return internal::UpdateError(error,
                                 absl::StrCat(internal::kConvertNonFinite, in));
  }
  *out = (in != 0);
  return true;
}

template <> inline bool Convert<double, float>(
    const double& in, float* out, absl::Status* error) {
  if (std::isfinite(in) && !internal::CheckRange<double, float>(in)) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowFloat, in));
  }
  *out = static_cast<float>(in);
  return true;
}

template <>
inline bool Convert<double, NumericValue>(const double& in, NumericValue* out,
                                          absl::Status* error) {
  const absl::StatusOr<NumericValue> numeric_value_status =
      NumericValue::FromDouble(in);
  if (ABSL_PREDICT_TRUE(numeric_value_status.ok())) {
    *out = numeric_value_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = numeric_value_status.status();
  }
  return false;
}

template <>
inline bool Convert<double, BigNumericValue>(const double& in,
                                             BigNumericValue* out,
                                             absl::Status* error) {
  const absl::StatusOr<BigNumericValue> bignumeric_value_status =
      BigNumericValue::FromDouble(in);
  if (ABSL_PREDICT_TRUE(bignumeric_value_status.ok())) {
    *out = *bignumeric_value_status;
    return true;
  }
  if (error != nullptr) {
    *error = bignumeric_value_status.status();
  }
  return false;
}

// -------------- numeric --------------

template <>
inline bool Convert<NumericValue, int32_t>(const NumericValue& in, int32_t* out,
                                           absl::Status* error) {
  const absl::StatusOr<int32_t> int32_status = in.To<int32_t>();
  if (ABSL_PREDICT_TRUE(int32_status.ok())) {
    *out = int32_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = int32_status.status();
  }
  return false;
}

template <>
inline bool Convert<NumericValue, int64_t>(const NumericValue& in, int64_t* out,
                                           absl::Status* error) {
  const absl::StatusOr<int64_t> int64_status = in.To<int64_t>();
  if (ABSL_PREDICT_TRUE(int64_status.ok())) {
    *out = int64_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = int64_status.status();
  }
  return false;
}

template <>
inline bool Convert<NumericValue, uint32_t>(const NumericValue& in,
                                            uint32_t* out,
                                            absl::Status* error) {
  const absl::StatusOr<uint32_t> uint32_status = in.To<uint32_t>();
  if (ABSL_PREDICT_TRUE(uint32_status.ok())) {
    *out = uint32_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = uint32_status.status();
  }
  return false;
}

template <>
inline bool Convert<NumericValue, uint64_t>(const NumericValue& in,
                                            uint64_t* out,
                                            absl::Status* error) {
  const absl::StatusOr<uint64_t> uint64_status = in.To<uint64_t>();
  if (ABSL_PREDICT_TRUE(uint64_status.ok())) {
    *out = uint64_status.value();
    return true;
  }
  if (error != nullptr) {
    *error = uint64_status.status();
  }
  return false;
}

template <> inline bool Convert<NumericValue, float>(
    const NumericValue& in, float* out, absl::Status* error) {
  *out = static_cast<float>(in.ToDouble());
  return true;
}

template <>
inline bool Convert<NumericValue, double>(const NumericValue& in, double* out,
                                          absl::Status* error) {
  *out = in.ToDouble();
  return true;
}

template <>
inline bool Convert<NumericValue, BigNumericValue>(const NumericValue& in,
                                                   BigNumericValue* out,
                                                   absl::Status* error) {
  *out = BigNumericValue(in);
  return true;
}

template <>
inline bool Convert<NumericValue, bool>(const NumericValue& in, bool* out,
                                        absl::Status* error) {
  *out = in != NumericValue();
  return true;
}

// -------------- bignumeric --------------

template <>
inline bool Convert<BigNumericValue, int32_t>(const BigNumericValue& in,
                                              int32_t* out,
                                              absl::Status* error) {
  const absl::StatusOr<int32_t> int32_status = in.To<int32_t>();
  if (ABSL_PREDICT_TRUE(int32_status.ok())) {
    *out = *int32_status;
    return true;
  }
  if (error != nullptr) {
    *error = int32_status.status();
  }
  return false;
}

template <>
inline bool Convert<BigNumericValue, int64_t>(const BigNumericValue& in,
                                              int64_t* out,
                                              absl::Status* error) {
  const absl::StatusOr<int64_t> int64_status = in.To<int64_t>();
  if (ABSL_PREDICT_TRUE(int64_status.ok())) {
    *out = *int64_status;
    return true;
  }
  if (error != nullptr) {
    *error = int64_status.status();
  }
  return false;
}

template <>
inline bool Convert<BigNumericValue, uint32_t>(const BigNumericValue& in,
                                               uint32_t* out,
                                               absl::Status* error) {
  const absl::StatusOr<uint32_t> uint32_status = in.To<uint32_t>();
  if (ABSL_PREDICT_TRUE(uint32_status.ok())) {
    *out = *uint32_status;
    return true;
  }
  if (error != nullptr) {
    *error = uint32_status.status();
  }
  return false;
}

template <>
inline bool Convert<BigNumericValue, uint64_t>(const BigNumericValue& in,
                                               uint64_t* out,
                                               absl::Status* error) {
  const absl::StatusOr<uint64_t> uint64_status = in.To<uint64_t>();
  if (ABSL_PREDICT_TRUE(uint64_status.ok())) {
    *out = *uint64_status;
    return true;
  }
  if (error != nullptr) {
    *error = uint64_status.status();
  }
  return false;
}

template <> inline bool Convert<BigNumericValue, float>(
    const BigNumericValue& in, float* out, absl::Status* error) {
  // There are some edge cases where conversion to double and then to float
  // yields slightly different results than conversion directly to float, but
  // the usage of float implies that the precision is not critical.
  *out = static_cast<float>(in.ToDouble());
  if (ABSL_PREDICT_FALSE(std::isinf(*out))) {
    return internal::UpdateError(
        error, absl::StrCat(internal::kConvertOverflowFloat, in.ToString()));
  }
  return true;
}

template <>
inline bool Convert<BigNumericValue, double>(const BigNumericValue& in,
                                             double* out, absl::Status* error) {
  *out = in.ToDouble();
  return true;
}

template <>
inline bool Convert<BigNumericValue, NumericValue>(const BigNumericValue& in,
                                                   NumericValue* out,
                                                   absl::Status* error) {
  const absl::StatusOr<NumericValue> numeric_value_status = in.ToNumericValue();
  if (ABSL_PREDICT_TRUE(numeric_value_status.ok())) {
    *out = *numeric_value_status;
    return true;
  }
  if (error != nullptr) {
    *error = numeric_value_status.status();
  }
  return false;
}

template <>
inline bool Convert<BigNumericValue, bool>(const BigNumericValue& in, bool* out,
                                           absl::Status* error) {
  *out = in != BigNumericValue();
  return true;
}

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_H_
