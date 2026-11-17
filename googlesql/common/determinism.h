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

#ifndef GOOGLESQL_COMMON_DETERMINISM_H_
#define GOOGLESQL_COMMON_DETERMINISM_H_

#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"

namespace googlesql {

// Returns true if `type` is a floating-point type or Geography, a composite
// type recursively containing one, or a proto type containing a
// floating-point field.
bool HasFloatingPoint(const Type* type);

// Returns true if casting from `input_type` to `output_type` can produce
// non-deterministic output based on the static types (e.g., converting
// floating-point or Geography values to a non-floating-point representation).
bool CastHasPotentialNondeterminism(const Type* input_type,
                                    const Type* output_type);

// Returns true if casting `from_value` to `to_type` converts any array with
// uncertain order into JSON (where the uncertain-order metadata is lost).
bool HasNonDeterministicArrayCastToJson(const Value& from_value,
                                        const Type* to_type);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_DETERMINISM_H_
