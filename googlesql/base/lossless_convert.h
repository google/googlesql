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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_LOSSLESS_CONVERT_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_LOSSLESS_CONVERT_H_

#include <limits>
#include <type_traits>

#include "googlesql/base/castops.h"

namespace googlesql_base {

// LosslessConvert casts a value from one numeric type to another, detecting if
// any information is lost due to overflow, rounding, or changes in signedness.
// If successful, returns true and writes converted value to *out. Otherwise,
// returns false, and the contents of *out is undefined.
//
// There may exist edge cases where the permissiveness of this function depends
// on compiler and CPU details.
//
// Note that converting a NaN value to another floating-point type will always
// succeed, even though the resulting values will not compare equal, because
// no information is lost (technically IEEE NaN values can store extra data in
// the mantissa bits, but we believe that data is virtually never used in
// Google code).
//
// Example usage:
//
//   double x = 5.0;
//   int32 y;
//
//   if (googlesql_base::LosslessConvert(x, &y)) {
//     ABSL_LOG(INFO) << "Converted to: " << y;
//   }
//
template <typename InType, typename OutType>
bool LosslessConvert(InType in_val, OutType *out);

//////////////////////////////////////////////////////////////////
//// Implementation details follow; clients should ignore.

namespace internal {

template <typename InType, typename OutType>
typename std::enable_if<std::is_integral<InType>::value &&
                            std::is_integral<OutType>::value,
                        bool>::type
LosslessConvert(InType in_val, OutType *out) {
  // static_cast between integral types always produces a valid value, so we
  // don't need a range check; we can just check after the fact if the
  // cast changed the value.
  *out = static_cast<OutType>(in_val);

  if (static_cast<InType>(*out) != in_val) {
    return false;
  }

  // Detect sign flips when converting between signed and unsigned types.
  // The numeric_limits check isn't strictly necessary, but could help
  // the compiler optimize the branch away.
  if (std::numeric_limits<InType>::is_signed !=
          std::numeric_limits<OutType>::is_signed &&
      (in_val < 0) != (*out < 0)) {
    return false;
  }

  // No loss detected.
  return true;
}

template <typename InType, typename OutType>
typename std::enable_if<std::is_floating_point<InType>::value &&
                            std::is_integral<OutType>::value,
                        bool>::type
LosslessConvert(InType in_val, OutType *out) {
  // Check for float-cast-overflow.
  if (!googlesql_base::castops::InRange<InType, OutType>(in_val)) {
    return false;
  }

  *out = static_cast<OutType>(in_val);

  return in_val == static_cast<InType>(*out);
}

template <typename InType, typename OutType>
typename std::enable_if<std::is_integral<InType>::value &&
                            std::is_floating_point<OutType>::value,
                        bool>::type
LosslessConvert(InType in_val, OutType *out) {
  // Assert that the range of OutType includes all possible values of InType.
  // This is true even when InType is 32 bit IEEE and OutType is uint128, so
  // it shouldn't fail in practice.
  static_assert(std::numeric_limits<InType>::digits <
                    std::numeric_limits<OutType>::max_exponent,
                "Integral->floating conversion with such a large size"
                " discrepancy is not yet supported");

  *out = static_cast<OutType>(in_val);

  // The initial conversion may have rounded the value out of the range of
  // InType, so we need a range check before the reverse conversion.
  return googlesql_base::castops::InRange<OutType, InType>(*out) &&
         in_val == static_cast<InType>(*out);
}

template <typename InType, typename OutType>
typename std::enable_if<std::is_floating_point<InType>::value &&
                            std::is_floating_point<OutType>::value,
                        bool>::type
LosslessConvert(InType in_val, OutType *out) {
  // C++ and IEEE both guarantee that each floating-point type is a subset of
  // the next larger one.
  if (sizeof(OutType) >= sizeof(InType)) {
    *out = static_cast<OutType>(in_val);
    return true;
  }

  static_assert((std::numeric_limits<OutType>::has_infinity ||
                 !std::numeric_limits<InType>::has_infinity) &&
                    (std::numeric_limits<OutType>::has_quiet_NaN ||
                     !std::numeric_limits<InType>::has_quiet_NaN),
                "Conversions that can lose infinity or NaN are not supported");
  if (std::isnan(in_val) || std::isinf(in_val)) {
    *out = static_cast<OutType>(in_val);
    return true;
  }

  // At this point we know InType is larger than OutType, so we can safely
  // cast to InType.
  if (in_val > static_cast<InType>(std::numeric_limits<OutType>::max()) ||
      in_val < static_cast<InType>(std::numeric_limits<OutType>::lowest())) {
    return false;
  }
  *out = static_cast<OutType>(in_val);
  return in_val == static_cast<InType>(*out);
}

}  // namespace internal

template <typename InType, typename OutType>
[[nodiscard]] bool LosslessConvert(InType in_val, OutType *out) {
  static_assert(std::is_arithmetic<InType>::value, "");
  static_assert(std::is_arithmetic<OutType>::value, "");

  // 128-bit integer support is possible, but annoying due to varying
  // implementation support (is_integral<__int128> doesn't even produce a
  // consistent result). Punt for now.
  static_assert(!std::is_integral<InType>::value ||
                sizeof(InType) <= 8, "128-bit integer types are unsupported");
  static_assert(!std::is_integral<OutType>::value ||
                sizeof(OutType) <= 8, "128-bit integer types are unsupported");

  return internal::LosslessConvert(in_val, out);
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_LOSSLESS_CONVERT_H_
