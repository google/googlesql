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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_INTERNAL_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_INTERNAL_H_

#include <cmath>
#include <limits>

#include "googlesql/base/logging.h"

namespace googlesql {
// Do not use any methods from the convert_internal namespace.
namespace convert_internal {

// Return true if the value is in the representable range of the result type:
// MIN <= value <= MAX.
template <typename FloatType, typename ResultType>
bool InRangeNoTruncate(FloatType value);

// Return true if the truncated form of value is smaller than or equal to the
// MAX value of IntType. When the MAX value of IntType can not be represented
// precisely in FloatType, the comparison is tricky, because the MAX value of
// IntType is promoted to a FloatType value that is actually greater than what
// IntType can handle. Also note that when value is nan, this function will
// return false.
template<typename FloatType, typename IntType>
bool SmallerThanOrEqualToIntMax(FloatType value) {
  if (value <= 0) {
    return true;
  }
  if (std::isnan(value) || std::isinf(value)) {
    return false;
  }
  // Set exp such that value == f * 2^exp for some f with |f| in [0.5, 1.0),
  // unless value is zero in which case exp == 0. Note that this implies that
  // the magnitude of value is strictly less than 2^exp.
  int exp = 0;
  std::frexp(value, &exp);

  // Let N be the number of non-sign bits in the representation of IntType.
  // If the magnitude of value is strictly less than 2^N, the truncated version
  // of value is representable as IntType.
  static_assert(std::numeric_limits<FloatType>::radix == 2,
                "return type size must be based on 2");
  return exp <= std::numeric_limits<IntType>::digits;
}

// Return true if max value of IntType can be represented precisely in
// FloatType.
template <typename FloatType, typename IntType>
constexpr bool CanRepresentMaxPrecisely() {
  int fraction_bits = 0;
  if (sizeof(FloatType) == 4) {
    fraction_bits = 24;
  } else if (sizeof(FloatType) == 8) {
    fraction_bits = 53;
  } else if (sizeof(FloatType) == 10) {
    fraction_bits = 64;
  } else if (sizeof(FloatType) == 16) {
    fraction_bits = 113;
  } else {
    ABSL_LOG(FATAL) << "FloatType is not supported";
  }
  return fraction_bits >= std::numeric_limits<IntType>::digits;
}

template <typename FloatType, typename ResultType>
bool InRangeNoTruncate(FloatType value) {
  static_assert(std::is_floating_point<FloatType>::value,
                "value must have floating point type");
  static_assert(std::is_integral<ResultType>::value,
                "return value must have integral type");
  static_assert(sizeof(ResultType) <= 16,
                "ResultType is no larger than 128 bits");
  static_assert(sizeof(FloatType) >= 4,
                "FloatType is no smaller than IEEE754 single precision");

  if (std::isnan(value)) {
    return false;
  }
  // Return false for unsigned type and negative value.
  if (!std::is_signed<ResultType>::value && value < 0) {
    return false;
  }

  FloatType lower_bound = std::numeric_limits<ResultType>::min();
  if (CanRepresentMaxPrecisely<FloatType, ResultType>()) {
    FloatType upper_bound = std::numeric_limits<ResultType>::max();
      return (value <= upper_bound) && (value >= lower_bound);
  } else {
    // If Max (2^N-1) value can't be represented precisely in FloatType,
    // -2^N-1 can't be represented precisely either.
    // Min (-2^N or 0) can anyway be represented precisely in FloatType.
    return value >= lower_bound &&
           SmallerThanOrEqualToIntMax<FloatType, ResultType>(value);
  }
}

}  // namespace convert_internal
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_INTERNAL_H_
