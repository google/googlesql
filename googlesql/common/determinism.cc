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

#include "googlesql/common/determinism.h"

#include "googlesql/common/internal_value.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"

namespace googlesql {

class ProtoTypeHasFloatingPointChecker {
 public:
  static bool HasFloatingPoint(const ProtoType* proto_type) {
    return proto_type->HasFloatingPointFields();
  }
};

bool HasFloatingPoint(const Type* type) {
  if (type->IsProto()) {
    return ProtoTypeHasFloatingPointChecker::HasFloatingPoint(type->AsProto());
  }
  for (const Type* component_type : type->ComponentTypes()) {
    if (HasFloatingPoint(component_type)) {
      return true;
    }
  }
  return type->IsFloatingPoint() || type->IsGeography();
}

bool CastHasPotentialNondeterminism(const Type* input_type,
                                    const Type* output_type) {
  if (output_type->IsVariant()) {
    return false;
  }
  TypeListView input_type_components = input_type->ComponentTypes();
  TypeListView output_type_components = output_type->ComponentTypes();
  if (input_type_components.size() != output_type_components.size()) {
    // Component counts differ (e.g., RANGE<T> <-> STRING, ARRAY/STRUCT ->
    // JSON); treat these casts as deterministic.
    return false;
  }
  if (input_type_components.empty()) {
    return HasFloatingPoint(input_type) && !HasFloatingPoint(output_type);
  }
  for (int i = 0; i < input_type_components.size(); ++i) {
    if (CastHasPotentialNondeterminism(input_type_components[i],
                                       output_type_components[i])) {
      return true;
    }
  }
  return false;
}

bool HasNonDeterministicArrayCastToJson(const Value& from_value,
                                        const Type* to_type) {
  if (from_value.is_null() || from_value.type()->Equals(to_type)) {
    return false;
  }
  if (to_type->IsJson()) {
    return InternalValue::ContainsArrayWithUncertainOrder(from_value);
  }
  if (from_value.type()->IsArray() && to_type->IsArray()) {
    const Type* to_element_type = to_type->AsArray()->element_type();
    for (int i = 0; i < from_value.num_elements(); ++i) {
      if (HasNonDeterministicArrayCastToJson(from_value.element(i),
                                             to_element_type)) {
        return true;
      }
    }
  } else if (from_value.type()->IsStruct() && to_type->IsStruct()) {
    const StructType* to_struct_type = to_type->AsStruct();
    for (int i = 0;
         i < from_value.num_fields() && i < to_struct_type->num_fields(); ++i) {
      if (HasNonDeterministicArrayCastToJson(from_value.field(i),
                                             to_struct_type->field(i).type)) {
        return true;
      }
    }
  } else if (from_value.type()->IsMap() && to_type->IsMap()) {
    const Type* to_key_type = to_type->AsMap()->key_type();
    const Type* to_value_type = to_type->AsMap()->value_type();
    for (const auto& [key, val] : from_value.map_entries()) {
      if (HasNonDeterministicArrayCastToJson(key, to_key_type) ||
          HasNonDeterministicArrayCastToJson(val, to_value_type)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace googlesql
