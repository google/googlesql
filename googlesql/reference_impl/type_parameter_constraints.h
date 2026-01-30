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

#ifndef GOOGLESQL_REFERENCE_IMPL_TYPE_PARAMETER_CONSTRAINTS_H_
#define GOOGLESQL_REFERENCE_IMPL_TYPE_PARAMETER_CONSTRAINTS_H_

#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"

namespace googlesql {

// Applies the type parameter constraints within <type_params> to the input
// <value>. Returns an internal error if <type_params> is invalid or does not
// match the type of <value>, and returns a user-facing error if the
// <value> does not adhere to the type parameter constraints. Depending on the
// type of <value>, <value> may be mutated to fit the provided type parameters.
absl::Status ApplyConstraints(const TypeParameters& type_params,
                              ProductMode mode, Value& value);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_TYPE_PARAMETER_CONSTRAINTS_H_
