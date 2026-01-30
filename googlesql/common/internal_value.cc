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

#include "googlesql/common/internal_value.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/public/language_options.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// static
bool InternalValue::ContainsArrayWithUncertainOrder(const Value& val) {
  if (val.is_null()) {
    return false;
  }
  if (val.type()->IsArray()) {
    for (int i = 0; i < val.num_elements(); ++i) {
      if (ContainsArrayWithUncertainOrder(val.element(i))) {
        return true;
      }
    }
    if (InternalValue::GetOrderKind(val) == Value::kIgnoresOrder) {
      return val.num_elements() >= 2;
    }
  }
  if (val.type()->IsStruct()) {
    for (int i = 0; i < val.num_fields(); ++i) {
      if (ContainsArrayWithUncertainOrder(val.field(i))) {
        return true;
      }
    }
  }
  return false;
}

absl::StatusOr<Value> InternalValue::MakeMeasure(
    const MeasureType* measure_type, Value captured_values_as_struct,
    std::vector<int> key_indices, const LanguageOptions& language_options) {
  if (measure_type == nullptr) {
    return absl::InvalidArgumentError("Measure type cannot be nullptr");
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<Value::TypedMeasure> typed_measure,
      Value::TypedMeasure::Create(std::move(captured_values_as_struct),
                                  std::move(key_indices), language_options));
  Value measure_value(measure_type, /*is_null=*/false, kPreservesOrder);
  measure_value.measure_ptr_ =
      new internal::ValueContentMeasureRef(std::move(typed_measure));
  return measure_value;
}

absl::StatusOr<Value> InternalValue::GetMeasureAsStructValue(
    const Value& measure_value) {
  if (measure_value.metadata_.type_kind() != TYPE_MEASURE) {
    return absl::InvalidArgumentError("Not a measure type");
  }
  if (measure_value.metadata_.is_null()) {
    return absl::InvalidArgumentError("Null measure");
  }
  GOOGLESQL_RET_CHECK(measure_value.measure_ptr_->value()->Is<Value::TypedMeasure>());
  return measure_value.measure_ptr_->value()
      ->GetAs<Value::TypedMeasure>()
      ->GetCapturedValuesAsStructValue();
}

absl::StatusOr<std::vector<int>> InternalValue::GetMeasureGrainLockingIndices(
    const Value& measure_value) {
  if (measure_value.metadata_.type_kind() != TYPE_MEASURE) {
    return absl::InvalidArgumentError("Not a measure type");
  }
  if (measure_value.metadata_.is_null()) {
    return absl::InvalidArgumentError("Null measure");
  }
  GOOGLESQL_RET_CHECK(measure_value.measure_ptr_->value()->Is<Value::TypedMeasure>());
  return measure_value.measure_ptr_->value()
      ->GetAs<Value::TypedMeasure>()
      ->KeyIndices();
}

}  // namespace googlesql
