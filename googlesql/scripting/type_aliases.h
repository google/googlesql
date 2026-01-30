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

#ifndef GOOGLESQL_SCRIPTING_TYPE_ALIASES_H_
#define GOOGLESQL_SCRIPTING_TYPE_ALIASES_H_

#include "googlesql/public/id_string.h"
#include "googlesql/public/value.h"

namespace googlesql {
// Mapping which associates a script variable with its current value.
using VariableMap = absl::flat_hash_map<IdString, Value, IdStringCaseHash,
                                        IdStringCaseEqualFunc>;

// Mapping which associates a script variable with its type parameters.
using VariableTypeParametersMap =
    absl::flat_hash_map<IdString, TypeParameters, IdStringCaseHash,
                        IdStringCaseEqualFunc>;

// Struct used to hold a variable's type and its type parameters.
struct TypeWithParameters {
  const Type* type = nullptr;
  TypeParameters type_params;
};

// Set of script variable names.
using VariableSet =
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>;

// Mapping of script variable names to its value and type parameter . Using
// case-insensitive comparator since script variable names are
// case-insensitive.
struct ValueWithTypeParameter {
  Value value;
  TypeParameters type_params;
};

using VariableWithTypeParameterMap =
    absl::flat_hash_map<IdString, ValueWithTypeParameter, IdStringCaseHash,
                        IdStringCaseEqualFunc>;

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_TYPE_ALIASES_H_
