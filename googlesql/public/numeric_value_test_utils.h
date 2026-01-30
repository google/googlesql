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

#ifndef GOOGLESQL_PUBLIC_NUMERIC_VALUE_TEST_UTILS_H_
#define GOOGLESQL_PUBLIC_NUMERIC_VALUE_TEST_UTILS_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/numeric_value.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Generates a random valid numeric value, intended to cover all precisions
// and scales when enough numbers are generated. Do not assume the result to
// follow any specific distribution.
template <typename T, typename URBG>
T MakeRandomNumericValue(URBG* random, int* num_truncated_digits = nullptr) {
  constexpr int kNumWords = sizeof(T) / sizeof(uint64_t);
  constexpr int kNumBits = sizeof(T) * 8;
  std::array<uint64_t, kNumWords> value;
  for (uint64_t& elem : value) {
    elem = absl::Uniform<uint64_t>(*random);
  }
  FixedInt<64, kNumWords> fixed_int(value);
  uint32_t num_non_trivial_bits = fixed_int.abs().FindMSBSetNonZero() + 1;
  uint32_t shift_bits =
      absl::Uniform<uint32_t>(*random, 0, num_non_trivial_bits + 1);
  fixed_int >>= shift_bits;
  num_non_trivial_bits -= shift_bits;
  T result;
  if constexpr (std::is_same_v<T, NumericValue>) {
    __int128 v = std::max(
        std::min(static_cast<__int128>(FixedInt<64, 2>(fixed_int.number())),
                 NumericValue::MaxValue().as_packed_int()),
        NumericValue::MinValue().as_packed_int());
    result = NumericValue::FromPackedInt(v).value();
  } else {
    static_assert(std::is_same_v<T, BigNumericValue>,
                  "T must be NumericValue or BigNumericValue");
    result = BigNumericValue::FromPackedLittleEndianArray(fixed_int.number());
  }
  constexpr int scale = T::kMaxFractionalDigits;
  constexpr int precision = T::kMaxIntegerDigits + scale;
  int num_approx_digits = num_non_trivial_bits * precision / kNumBits;
  int digits_to_trunc =
      num_approx_digits == 0
          ? 0
          : absl::Uniform<uint>(*random, 0, num_approx_digits);
  if (num_truncated_digits != nullptr) {
    *num_truncated_digits = digits_to_trunc;
  }
  return result.Trunc(scale - digits_to_trunc);
}

template <typename T, typename URBG>
T MakeRandomNonZeroNumericValue(URBG* random) {
  T result = MakeRandomNumericValue<T>(random);
  if (result == T()) {
    int64_t v = absl::Uniform<uint32_t>(*random);
    return T::FromScaledValue(v + 1);
  }
  return result;
}

template <typename T, typename URBG>
T MakeRandomPositiveNumericValue(URBG* random) {
  absl::StatusOr<T> abs = MakeRandomNonZeroNumericValue<T>(random).Abs();
  // Abs fails only when result = T::MinValue().
  return abs.ok() ? abs.value() : T::MaxValue();
}

// Generate a random double value that can be losslessly converted to T.
template <typename T, typename URBG>
double MakeLosslessRandomDoubleValue(uint max_integer_bits, URBG* random) {
  uint max_mantissa_bits =
      std::min<uint>(53, T::kMaxFractionalDigits + max_integer_bits);
  int64_t mantissa = absl::Uniform<int64_t>(
      *random, 1 - (1LL << max_mantissa_bits), (1LL << max_mantissa_bits));
  int exponent_bits = absl::Uniform<int>(absl::IntervalClosedClosed, *random,
                                         -T::kMaxFractionalDigits,
                                         max_integer_bits - max_mantissa_bits);
  return std::ldexp(mantissa, exponent_bits);
}
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_NUMERIC_VALUE_TEST_UTILS_H_
