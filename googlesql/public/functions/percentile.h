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

// Utilities for percentiles.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_PERCENTILE_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_PERCENTILE_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
// TODO: Remove the second template parameter.
template <typename T, typename W = T>
class PercentileHelper;

// A comparator to determine if the <lhs> is less than the <rhs> given the
// collation from the input <collator>.
class LessComparatorWithCollation {
 public:
  explicit LessComparatorWithCollation(const GoogleSqlCollator* collator)
      : collator_(collator) {
    ABSL_DCHECK(collator_ != nullptr);
  }

  bool operator()(absl::string_view lhs, absl::string_view rhs) {
    // CompareUtf8 function returns -1 if <lhs> is less than <rhs>, returns 1 if
    // <lhs> is greater than <rhs>, and returns 0 is <lhs> is equal to <rhs>.
    // Therefore we compare this value with 0 to get final comparison result.
    absl::Status status;
    // TODO: Consider using status->Update() inside CompareUtf8
    // function call.
    int64_t collator_res = collator_->CompareUtf8(lhs, rhs, &status);
    if (!status.ok()) {
      status_.Update(status);
    }
    return collator_res < 0;
  }
  absl::Status status() const { return status_; }

 private:
  const GoogleSqlCollator* collator_;
  absl::Status status_;
};

// PercentileType = double, NumericValue or BigNumericValue
// TODO: Remove the second template parameter.
template <typename PercentileType, typename W = PercentileType>
class PercentileEvaluator {
 public:
  // For PercentileType = double, Weight = double by default; can be
  // overridden by the template parameter W to long double (deprecated).
  // For PercentileType = NumericValue, Weight = NumericValue.
  // For PercentileType = BigNumericValue, Weight = BigNumericValue.
  using Weight = W;
  // Returns an error if percentile is not in [0, 1].
  static absl::StatusOr<PercentileEvaluator> Create(PercentileType percentile) {
    GOOGLESQL_ASSIGN_OR_RETURN(PctHelper helper, PctHelper::Create(percentile));
    return PercentileEvaluator(helper);
  }

  // Computes the integral part and the fractional part of
  // <max_index> * <percentile_>.
  // The returned value (returned_index) = floor(<max_index> * <percentile_>).
  // <right_weight> = <max_index> * <percentile_> - (returned_index).
  // <left_weight> = 1 - <right_weight>.
  // For a sorted array X with size = <max_index> + 1, the percentile value
  // of X = X[(returned_index)] * <left_weight> +
  //        X[(returned_index) + 1] * <right_weight>.
  // (note: it is possible that (returned_index) = <max_index> and
  // <right_weight> = 0)
  //
  // This function handles <max_index> > std::numeric_limits<int32>::max().
  // (returned_index) is precise; <left_weight> and <right_weight> are as
  // precise as the <Weight> type supports.
  //
  // The reason why not return only <right_weight> is that this function
  // computes both weights in a way to minimize the floating point error for
  // both. Always calculating <left_weight> as 1 - <right_weight> or vice versa
  // could result in high relative error when one of them is close to 0.
  size_t ComputePercentileIndex(size_t max_index, Weight* left_weight,
                                Weight* right_weight) const {
    return helper_.ComputePercentileIndex(max_index, left_weight, right_weight);
  }

  static PercentileType ComputeLinearInterpolation(PercentileType left_value,
                                                   Weight left_weight,
                                                   PercentileType right_value,
                                                   Weight right_weight) {
    return PctHelper::ComputeLinearInterpolation(left_value, left_weight,
                                                 right_value, right_weight);
  }

  // Computes PERCENTILE_CONT(x, percentile) from values in
  // [nonnull_values_begin, nonnull_values_end) PLUS <num_nulls> nulls,
  // according to docs/document/d/1XECBsOd5pzMtnJBIMAS8jwiWa7RMPoPSayUBuC9Mm4g.
  //
  // Itr must be random accessible, and it must be dereferenced to
  // PercentileType or a type that can be implicitly cast to PercentileType.
  //
  // [nonnull_values_begin, nonnull_values_end) may contain NaNs but not NULLs.
  //
  // If <sorted> is false:
  //   It means [nonnull_values_begin, nonnull_values_end) is not sorted. Itr
  //   must be mutable, and this method will reorder some of the elements in
  //   this range. This method has a time complexity of O(n) where
  //   n = nonnull_values_end - nonnull_values_begin.
  // If <sorted> is true:
  //   It means [nonnull_values_begin, nonnull_values_end) is sorted in
  //   ascending order. If this range contains NaNs, NaNs must be placed in the
  //   beginning. This method has a constant time complexity.
  //
  // If the percentile value is null, returns false and leaves *result
  // unchanged. Otherwise returns true and populates *result with the computed
  // percentile value.
  //
  // This method is not ideal for handling sliding windows; an alternative is to
  // use PercentileHeap, which does not support n >
  // std::numeric_limits<int32>::max(), though.
  template <bool sorted, typename Itr>
  bool ComputePercentileCont(Itr nonnull_values_begin, Itr nonnull_values_end,
                             size_t num_nulls, PercentileType* result) const {
    size_t num_nonnull_values = nonnull_values_end - nonnull_values_begin;
    if (num_nonnull_values == 0) {
      return false;
    }
    Weight left_weight = Weight();
    Weight right_weight = Weight();
    size_t index = ComputePercentileIndex(
        num_nonnull_values - 1 + num_nulls,
        &left_weight, &right_weight);
    std::less<PercentileType> comp;
    if (index >= num_nulls) {
      // The percentile is within normal values.
      index -= num_nulls;
      const size_t num_nans = PartitionNaNs<sorted, Itr, PercentileType>(
          nonnull_values_begin, nonnull_values_end);
      *result = *GetNthElement<sorted, Itr>(
          nonnull_values_begin, nonnull_values_end, index, num_nans, comp);
      if (right_weight > Weight()) {
        const PercentileType right_value = *GetNthElement<sorted, Itr>(
            nonnull_values_begin, nonnull_values_end, index + 1, num_nans,
            comp);
        *result = ComputeLinearInterpolation((*result), left_weight,
                                             right_value, right_weight);
      }
    } else if (index == num_nulls - 1 && right_weight != Weight()) {
      // The percentile is between a null value and the minimum normal value.
      const size_t num_nans = PartitionNaNs<sorted, Itr, PercentileType>(
          nonnull_values_begin, nonnull_values_end);
      *result = *GetNthElement<sorted, Itr>(
          nonnull_values_begin, nonnull_values_end, 0, num_nans, comp);
    } else {
      // The percentile is within null values.
      return false;
    }
    return true;
  }

  // Computes PERCENTILE_DISC(x, percentile) from values in
  // [nonnull_values_begin, nonnull_values_end) PLUS <num_nulls> nulls,
  // according to docs/document/d/1XECBsOd5pzMtnJBIMAS8jwiWa7RMPoPSayUBuC9Mm4g.
  //
  // T must be comparable. Itr must be random accessible, and it must be
  // dereferenced to T or a type that can be implicitly cast to T.
  //
  // [nonnull_values_begin, nonnull_values_end) may contain NaNs but not NULLs.
  //
  // If <sorted> is false:
  //   It means [nonnull_values_begin, nonnull_values_end) is not sorted. Itr
  //   must be mutable, and this method will reorder some of the elements in
  //   this range. This method has a time complexity of O(n) where
  //   n = nonnull_values_end - nonnull_values_begin.
  // If <sorted> is true:
  //   It means [nonnull_values_begin, nonnull_values_end) is sorted in
  //   ascending order. If this range contains NaNs, NaNs must be placed in the
  //   beginning. This method has a constant time complexity.
  //
  // If the percentile value is null, returns nonnull_values_end. Otherwise
  // returns the iterator pointing to the percentile value in
  // [nonnull_values_begin, nonnull_values_end). If the percentile lands between
  // two values, the iterator to the second ordered value is returned.
  //
  // This method is not ideal for handling sliding windows; an alternative is to
  // use PercentileHeap, which does not support n >
  // std::numeric_limits<int32>::max(), though.
  template <typename T, bool sorted, typename Itr>
  Itr ComputePercentileDisc(
      Itr nonnull_values_begin, Itr nonnull_values_end,
      size_t num_nulls) const {
    std::less<T> comp;
    return ComputePercentileDiscImpl<T, sorted, Itr>(
        nonnull_values_begin, nonnull_values_end, num_nulls, comp);
  }

  // Similar to the function above but accepts an <collator> argument to
  // compare the values with collation.
  template <bool sorted, typename Itr>
  absl::StatusOr<Itr> ComputePercentileDiscWithCollation(
      Itr nonnull_values_begin, Itr nonnull_values_end, size_t num_nulls,
      const GoogleSqlCollator* collator) const {
    LessComparatorWithCollation comp(collator);
    Itr itr = ComputePercentileDiscImpl<absl::string_view, sorted, Itr>(
        nonnull_values_begin, nonnull_values_end, num_nulls, comp);
    if (!comp.status().ok()) {
      return comp.status();
    }
    return itr;
  }

 private:
  using PctHelper = PercentileHelper<PercentileType, W>;
  struct IsNaN {
    template <typename T>
    bool operator()(T value) const {
      return std::isnan(value);
    }
  };

  template <bool sorted, typename Itr, typename Value>
  static size_t PartitionNaNs(Itr begin, Itr end) {
    if constexpr (!sorted && std::is_floating_point_v<Value>) {
      return std::partition(begin, end, IsNaN()) - begin;
    } else {
      return 0;
    }
  }

  template <bool sorted, typename Itr, typename Comparator>
  static Itr GetNthElement(Itr begin, Itr end, size_t index, size_t num_nans,
                           Comparator& comp) {
    if constexpr (sorted) {
      return begin + index;
    } else {
      Itr itr = begin + index;
      if (index >= num_nans) {
        std::nth_element(begin + num_nans, itr, end, comp);
      }
      return itr;
    }
  }

  template <typename T, bool sorted, typename Itr, typename Compare>
  Itr ComputePercentileDiscImpl(Itr nonnull_values_begin,
                                Itr nonnull_values_end, size_t num_nulls,
                                Compare& comp) const {
    size_t num_nonnull_values = nonnull_values_end - nonnull_values_begin;
    if (num_nonnull_values == 0) {
      return nonnull_values_end;
    }
    Weight left_weight = Weight();
    Weight right_weight = Weight();
    size_t index = ComputePercentileIndex(num_nonnull_values + num_nulls,
                                          &left_weight, &right_weight);
    if (index > 0 && right_weight == Weight()) {
      --index;
    }
    if (index >= num_nulls) {
      // The percentile is within normal values.
      index -= num_nulls;
      const size_t num_nans = PartitionNaNs<sorted, Itr, T>(
          nonnull_values_begin, nonnull_values_end);
      return GetNthElement<sorted, Itr>(
          nonnull_values_begin, nonnull_values_end, index, num_nans, comp);
    }
    return nonnull_values_end;
  }

  explicit PercentileEvaluator(const PctHelper& helper) : helper_(helper) {}

  PctHelper helper_;
};

template <typename Weight>
class PercentileHelper<double, Weight> {
 public:
  static absl::StatusOr<PercentileHelper> Create(double percentile);
  PercentileHelper(const PercentileHelper& src) = default;
  size_t ComputePercentileIndex(size_t max_index, Weight* left_weight,
                                Weight* right_weight) const;
  static double ComputeLinearInterpolation(double left_value,
                                           Weight left_weight,
                                           double right_value,
                                           Weight right_weight) {
    return left_value * left_weight + right_value * right_weight;
  }

 private:
  PercentileHelper(double percentile, int64_t percentile_mantissa,
                   int percentile_exponent)
      : percentile_(percentile),
        percentile_mantissa_(percentile_mantissa),
        percentile_exponent_(percentile_exponent),
        num_fractional_bits_(-percentile_exponent) {}
  const double percentile_;
  const int64_t percentile_mantissa_;
  const int percentile_exponent_;
  const int num_fractional_bits_;
};

extern template class PercentileHelper<double, double>;

// DEPRECATED.
// TODO: Remove this extern template declaration.
extern template class PercentileHelper<double, long double>;

template <>
class PercentileHelper<NumericValue, NumericValue> {
 public:
  using Weight = NumericValue;
  static absl::StatusOr<PercentileHelper> Create(NumericValue percentile);
  PercentileHelper(const PercentileHelper& src) = default;
  size_t ComputePercentileIndex(size_t max_index, NumericValue* left_weight,
                                NumericValue* right_weight) const;
  static NumericValue ComputeLinearInterpolation(NumericValue left_value,
                                                 NumericValue left_weight,
                                                 NumericValue right_value,
                                                 NumericValue right_weight);

 private:
  explicit PercentileHelper(uint32_t scaled_percentile)
      : scaled_percentile_(scaled_percentile) {}
  const uint32_t scaled_percentile_;
};

template <>
class PercentileHelper<BigNumericValue, BigNumericValue> {
 public:
  using Weight = BigNumericValue;
  static absl::StatusOr<PercentileHelper> Create(BigNumericValue percentile);
  PercentileHelper(const PercentileHelper& src) = default;
  size_t ComputePercentileIndex(size_t max_index, BigNumericValue* left_weight,
                                BigNumericValue* right_weight) const;
  static BigNumericValue ComputeLinearInterpolation(
      const BigNumericValue& left_value, const BigNumericValue& left_weight,
      const BigNumericValue& right_value, const BigNumericValue& right_weight);

 private:
  explicit PercentileHelper(FixedUint<64, 2> scaled_percentile)
      : scaled_percentile_(scaled_percentile) {}
  const FixedUint<64, 2> scaled_percentile_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_PERCENTILE_H_
