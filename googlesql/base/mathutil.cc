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

#include "googlesql/base/mathutil.h"

#include <stdlib.h>

#include <cstdint>
#include <limits>

#include "absl/base/casts.h"

namespace googlesql_base {
namespace {

template <typename R, typename F, typename Rep>
R DecomposeImpl(const F value) {
  using Limits = std::numeric_limits<F>;
  using Mantissa = decltype(R{}.mantissa);
  using Exponent = decltype(R{}.exponent);
  // The leading one-bit in 1.xxxxxx is not stored.
  constexpr int kMantissaBits = Limits::digits - 1;
  constexpr Rep kSignMask = ~(std::numeric_limits<Rep>::max() >> 1);
  constexpr Rep kMantissaMask = (Rep{1} << kMantissaBits) - 1;
  static_assert(Limits::is_iec559,
                "Floating point must follow IEC 559 (IEEE 754) standard.");
  static_assert(Limits::has_denorm == std::denorm_present,
                "Floating point must support denormal values.");
  static_assert(sizeof(F) * 8 - kMantissaBits <= sizeof(Exponent) * 8,
                "Exponent bits do not fit in Exponent's type");

  if (!std::isfinite(value)) {
    const int exponent = std::numeric_limits<Exponent>::max();
    if (value == Limits::infinity()) {
      return {std::numeric_limits<Mantissa>::max(), exponent};
    }
    if (value == -Limits::infinity()) {
      return {-std::numeric_limits<Mantissa>::max(), exponent};
    }
    // value is NaN.
    return {0, exponent};
  }

  const Rep bits = absl::bit_cast<Mantissa>(value);

  R parts;
  parts.mantissa = static_cast<Mantissa>(bits & kMantissaMask);
  parts.exponent = static_cast<Exponent>((bits & ~kSignMask) >> kMantissaBits);
  if (parts.exponent > 0) {
    // The value is normal, and represented as
    //   1.(mantissa bits) * pow(2, exponent - exponent_bias)
    // We need to add the implicit leading one-bit to mantissa.
    // The exponent offset is different from the denormal case by one,
    // so we account for it here as well.
    --parts.exponent;
    parts.mantissa |= (kMantissaMask + 1);
  }
  // Else, value is 0 or denormal.
  // The value is 0.(mantissa bits) * pow(2, 0 - (exponent_bias - 1)).

  if ((bits & kSignMask) != 0) {
    parts.mantissa = -parts.mantissa;
  }
  parts.exponent -= Limits::digits - Limits::min_exponent;
  return parts;
}

}  // namespace

MathUtil::FloatParts MathUtil::Decompose(float value) {
  return DecomposeImpl<FloatParts, float, uint32_t>(value);
}

MathUtil::DoubleParts MathUtil::Decompose(double value) {
  return DecomposeImpl<DoubleParts, double, uint64_t>(value);
}

}  // namespace googlesql_base
