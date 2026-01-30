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

#include "googlesql/public/functions/percentile.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "googlesql/base/logging.h"
#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/numeric_value.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/mathutil.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
template <typename T>
inline absl::Status CheckPercentileArgument(T percentile) {
  if (!(percentile >= T(0) && percentile <= T(1))) {  // handles NaN
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Percentile argument must be in [0, 1]; got " << percentile;
  }
  return absl::OkStatus();
}

template <typename Weight>
size_t PercentileHelper<double, Weight>::ComputePercentileIndex(
    size_t max_index, Weight* left_weight, Weight* right_weight) const {
  // Some fast paths for common use cases.
  if (percentile_ == 0.5) {
    *right_weight = 0.5 * (max_index & 1);
    *left_weight = 1 - *right_weight;
    return max_index >> 1;
  }
  if (percentile_ == 0) {
    *left_weight = 1;
    *right_weight = 0;
    return 0;
  }
  if (percentile_ == 1) {
    *left_weight = 1;
    *right_weight = 0;
    return max_index;
  }
  using uint128 = unsigned __int128;
  // Use integer multiplication to preserve all bits. The compiler is smart
  // enough to optimize the multiplication to only one mulq instruction.
  const uint128 product =
      static_cast<uint128>(static_cast<uint64_t>(percentile_mantissa_)) *
      max_index;
  if (num_fractional_bits_ < 128) {
    const uint128 scale = static_cast<uint128>(1) << num_fractional_bits_;
    const uint128 fractional_part = product & (scale - 1);
    // To minimize precision loss, the smaller weight should be computed from
    // the integer; the bigger weight can be computed as 1 - smaller weight
    // without further precision loss.
    if (fractional_part > (scale >> 1)) {
      *left_weight = std::ldexp(static_cast<Weight>(scale - fractional_part),
                                percentile_exponent_);
      *right_weight = 1 - *left_weight;
    } else {
      *right_weight = std::ldexp(static_cast<Weight>(fractional_part),
                                 percentile_exponent_);
      *left_weight = 1 - *right_weight;
    }
    const size_t index = static_cast<size_t>(product >> num_fractional_bits_);
    ABSL_DCHECK_LE(index, max_index);
    ABSL_DCHECK_GE(*left_weight, 0);
    ABSL_DCHECK_LE(*left_weight, 1);
    ABSL_DCHECK_GE(*right_weight, 0);
    ABSL_DCHECK_LE(*right_weight, 1);
    return index;
  }
  *right_weight =
      std::ldexp(static_cast<Weight>(product), percentile_exponent_);
  *left_weight = 1 - *right_weight;
  return 0;
}

// static
template <typename Weight>
absl::StatusOr<PercentileHelper<double, Weight>>
PercentileHelper<double, Weight>::Create(double percentile) {
  GOOGLESQL_RETURN_IF_ERROR(CheckPercentileArgument(percentile));
  const googlesql_base::MathUtil::DoubleParts parts = googlesql_base::MathUtil::Decompose(percentile);
  GOOGLESQL_RET_CHECK_GE(parts.mantissa, 0);
  return PercentileHelper(percentile, parts.mantissa, parts.exponent);
}

size_t PercentileHelper<NumericValue>::ComputePercentileIndex(
    size_t max_index, NumericValue* left_weight,
    NumericValue* right_weight) const {
  FixedUint<64, 2> scaled_index(static_cast<uint64_t>(max_index));
  scaled_index *= scaled_percentile_;
  uint64_t scaled_mod;
  scaled_index.DivMod(NumericValue::kScalingFactor, &scaled_index, &scaled_mod);
  *left_weight =
      NumericValue::FromScaledValue(NumericValue::kScalingFactor - scaled_mod);
  *right_weight = NumericValue::FromScaledValue(scaled_mod);
  ABSL_DCHECK_EQ(scaled_index.number()[1], 0);
  return scaled_index.number()[0];
}

template class PercentileHelper<double, double>;
template class PercentileHelper<double, long double>;

// static
NumericValue PercentileHelper<NumericValue>::ComputeLinearInterpolation(
    NumericValue left_value, NumericValue left_weight, NumericValue right_value,
    NumericValue right_weight) {
  ABSL_DCHECK_GE(left_weight, NumericValue(0));
  ABSL_DCHECK_LE(left_weight, NumericValue(1));
  ABSL_DCHECK_GE(right_weight, NumericValue(0));
  ABSL_DCHECK_LE(right_weight, NumericValue(1));
  ABSL_DCHECK_EQ(left_weight.low_bits() + right_weight.low_bits(),
            NumericValue::kScalingFactor)
      << "left_weight + right_weight must be 1";
  __int128 packed_left_value = left_value.as_packed_int();
  __int128 packed_right_value = right_value.as_packed_int();
  FixedInt<64, 3> scaled_left_value(packed_left_value);
  FixedInt<64, 3> scaled_right_value(packed_right_value);
  scaled_left_value *= left_weight.low_bits();
  scaled_right_value *= right_weight.low_bits();
  scaled_left_value += scaled_right_value;
  scaled_left_value.DivAndRoundAwayFromZero(NumericValue::kScalingFactor);
  __int128 packed_result = static_cast<__int128>(scaled_left_value);
  ABSL_DCHECK(scaled_left_value == (FixedInt<64, 3>(packed_result)));
  auto status_or_result = NumericValue::FromPackedInt(packed_result);
  if (ABSL_PREDICT_TRUE(status_or_result.ok())) {
    return status_or_result.value();
  }
  // This branch is reached only on error cases, i.e., when the weights are not
  // in [0, 1] or do not sum to 1. Adjust the value to avoid crash in .value().
  __int128 max_packed_value = std::max(packed_left_value, packed_right_value);
  if (packed_result > max_packed_value) {
    packed_result = max_packed_value;
  }
  __int128 min_packed_value = std::min(packed_left_value, packed_right_value);
  if (packed_result < min_packed_value) {
    packed_result = min_packed_value;
  }
  return NumericValue::FromPackedInt(packed_result).value();
}

// static
absl::StatusOr<PercentileHelper<NumericValue>>
PercentileHelper<NumericValue>::Create(NumericValue percentile) {
  GOOGLESQL_RETURN_IF_ERROR(CheckPercentileArgument(percentile));
  return PercentileHelper<NumericValue>(
      static_cast<uint32_t>(percentile.low_bits()));
}

size_t PercentileHelper<BigNumericValue>::ComputePercentileIndex(
    size_t max_index, BigNumericValue* left_weight,
    BigNumericValue* right_weight) const {
  FixedUint<64, 3> scaled_index = ExtendAndMultiply(
      scaled_percentile_, FixedUint<64, 1>(static_cast<uint64_t>(max_index)));
  FixedUint<64, 2> index =
      BigNumericValue::RemoveScalingFactor</* round = */ false>(scaled_index);
  ABSL_DCHECK_EQ(index.number()[1], 0);
  uint64_t result = index.number()[0];
  FixedUint<64, 2> scaled_left_weight(BigNumericValue::kScalingFactor);
  scaled_index -=
      ExtendAndMultiply(scaled_left_weight, FixedUint<64, 1>(result));
  ABSL_DCHECK(scaled_index < (FixedUint<64, 3>(BigNumericValue::kScalingFactor)));
  scaled_left_weight -= FixedUint<64, 2>(scaled_index);
  *left_weight = BigNumericValue::FromPackedLittleEndianArray(
      FixedUint<64, 4>(scaled_left_weight).number());
  *right_weight = BigNumericValue::FromPackedLittleEndianArray(
      FixedUint<64, 4>(scaled_index).number());
  return result;
}

inline FixedInt<64, 6> MultiplyValueByWeight(const BigNumericValue& value,
                                             const BigNumericValue& weight) {
  ABSL_DCHECK_GE(weight, BigNumericValue(0));
  ABSL_DCHECK_LE(weight, BigNumericValue(1));
  FixedInt<64, 2> scaled_weight(
      FixedInt<64, 4>(weight.ToPackedLittleEndianArray()));
  return ExtendAndMultiply(FixedInt<64, 4>(value.ToPackedLittleEndianArray()),
                           scaled_weight);
}

// static
BigNumericValue PercentileHelper<BigNumericValue>::ComputeLinearInterpolation(
    const BigNumericValue& left_value, const BigNumericValue& left_weight,
    const BigNumericValue& right_value, const BigNumericValue& right_weight) {
  ABSL_DCHECK_EQ(left_weight.Add(right_weight).value(), BigNumericValue(1))
      << "left_weight + right_weight must be 1";
  FixedInt<64, 6> scaled_sum = MultiplyValueByWeight(left_value, left_weight);
  bool overflow =
      scaled_sum.AddOverflow(MultiplyValueByWeight(right_value, right_weight));
  ABSL_DCHECK(!overflow) << "Unexpected overflow: " << left_value << " * "
                    << left_weight << " + " << right_value << " * "
                    << right_weight;
  FixedUint<64, 5> sum_abs =
      BigNumericValue::RemoveScalingFactor</* round = */ true>(
          scaled_sum.abs());
  ABSL_DCHECK_EQ(sum_abs.number()[4], 0);
  FixedInt<64, 4> result(sum_abs);
  if (scaled_sum.is_negative()) {
    result = -result;
  }
  return BigNumericValue::FromPackedLittleEndianArray(result.number());
}

// static
absl::StatusOr<PercentileHelper<BigNumericValue>>
PercentileHelper<BigNumericValue>::Create(BigNumericValue percentile) {
  GOOGLESQL_RETURN_IF_ERROR(CheckPercentileArgument(percentile));
  return PercentileHelper<BigNumericValue>(FixedUint<64, 2>(
      FixedUint<64, 4>(percentile.ToPackedLittleEndianArray())));
}

}  // namespace googlesql
