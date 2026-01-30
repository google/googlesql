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

#ifndef GOOGLESQL_COMMON_INTERNAL_VALUE_H_
#define GOOGLESQL_COMMON_INTERNAL_VALUE_H_

#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/value.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace googlesql {

// This class is a friend of Value. It is used to access internal methods that
// are intended only for use by the reference implementation.
class InternalValue {
 public:
  using OrderPreservationKind = Value::OrderPreservationKind;
  using ProtoRep = internal::ProtoRep;

  static constexpr OrderPreservationKind kPreservesOrder =
      Value::kPreservesOrder;
  static constexpr OrderPreservationKind kIgnoresOrder = Value::kIgnoresOrder;

  // Access the private accessor for order_kind.
  static OrderPreservationKind order_kind(const Value& v) {
    return v.order_kind();
  }

  // Creates an array of the given 'array_type' initialized by moving from
  // 'values'. The type of each value must be the same as
  // array_type->element_type(). If we are in debug mode, this is ABSL_CHECK'd.
  static Value ArrayNotChecked(const ArrayType* array_type,
                               OrderPreservationKind order_kind,
                               std::vector<Value>&& values) {
    absl::StatusOr<Value> value = Value::MakeArrayInternal(
        /*already_validated=*/true, array_type, order_kind, std::move(values));
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }

  // Same as ArrayNotChecked except we ABSL_CHECK the type of each element, even when
  // not in debug mode.
  static Value ArrayChecked(const ArrayType* array_type,
                            OrderPreservationKind order_kind,
                            std::vector<Value>&& values) {
    absl::StatusOr<Value> value = Value::MakeArrayInternal(
        /*already_validated=*/false, array_type, order_kind, std::move(values));
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }

  // A version of Value::MakeArray that allows setting the
  // OrderPreservationKind. This method is identical to
  // InternalValue::ArrayChecked except it returns an absl::StatusOr.
  static absl::StatusOr<Value> MakeArray(const ArrayType* array_type,
                                         OrderPreservationKind order_kind,
                                         std::vector<Value>&& values) {
    return Value::MakeArrayInternal(
        /*already_validated=*/false, array_type, order_kind, std::move(values));
  }

  // DEPRECATED: use ArrayNotChecked/ArrayChecked() instead. (For some reason,
  // there are forks of the reference implementation outside googlesql code that
  // are allowed to call this class.)
  static Value Array(const ArrayType* array_type,
                     absl::Span<const Value> values,
                     OrderPreservationKind order_kind) {
    std::vector<Value> value_copies(values.begin(), values.end());
    absl::StatusOr<Value> value =
        Value::MakeArrayInternal(/*already_validated=*/false, array_type,
                                 order_kind, std::move(value_copies));
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }

  // Checks equality of values. Arrays inside 'x' value with
  // order_kind()=kIgnoresOrder are compared as multisets to respective arrays
  // in 'y' value.
  static bool Equals(const Value& x, const Value& y,
                     const ValueEqualityCheckOptions& options = {}) {
    if (options.reason) {
      options.reason->clear();
    }
    return Value::EqualsInternal(x, y, /*allow_bags=*/true, options);
  }

  static OrderPreservationKind GetOrderKind(const Value& x) {
    return x.order_kind();
  }

  static const ProtoRep* GetProtoRep(const Value& x) {
    if (x.type_kind() != TYPE_PROTO) return nullptr;
    return x.proto_ptr_;
  }

  static std::string FormatInternal(const Value& x,
                                    bool include_array_ordereness
  ) {
    return x.FormatInternal({
        .force_type_at_top_level = true,
        .include_array_ordereness = include_array_ordereness,
        .indent = 0,
    });
  }

  // Creates a measure value of the specified `measure_type`.
  // `captured_values_as_struct` is a STRUCT-typed value, with uniquely named
  // fields. Each field represents either a column used for grain-locking or a
  // column referenced by the measure expression.
  // Each index in `key_indices` is the index of the field in
  // `captured_values_as_struct` that forms the grain-locking key for the
  // measure.
  // Preconditions:
  // - `measure_type` must be a valid measure type.
  // - `captured_values_as_struct` must be a valid, non-null struct with at
  //   least one field and unique field names.
  // - `key_indices` must be non-empty, with valid indices.
  static absl::StatusOr<Value> MakeMeasure(
      const MeasureType* measure_type, Value captured_values_as_struct,
      std::vector<int> key_indices, const LanguageOptions& language_options);

  // REQUIRES: measure type
  static absl::StatusOr<Value> GetMeasureAsStructValue(
      const Value& measure_value);
  static absl::StatusOr<std::vector<int> > GetMeasureGrainLockingIndices(
      const Value& measure_value);

  // Reexport FormatValueContentOptions for code that has visibility to this
  // class.
  using FormatValueContentOptions = Type::FormatValueContentOptions;

  static std::string FormatInternal(const Value& x,
                                    const FormatValueContentOptions& options) {
    return x.FormatInternal(options);
  }

  // Returns true if `val` contains in its nested structure an array with two
  // or more elements where the order between those elements is unknown because
  // the array reports kIgnoresOrder.
  static bool ContainsArrayWithUncertainOrder(const Value& val);
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_INTERNAL_VALUE_H_
