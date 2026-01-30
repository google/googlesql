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

#ifndef GOOGLESQL_COMMON_AGGREGATE_NULL_HANDLING_H_
#define GOOGLESQL_COMMON_AGGREGATE_NULL_HANDLING_H_

#include "googlesql/resolved_ast/resolved_ast.h"

namespace googlesql {

// Returns true if the given aggregate function is known to ignore all rows
// where at least one input argument is NULL. A false value doesn't necessarily
// mean that the function respects NULLs, and could also mean that we don't
// know whether it ignores NULLs.
bool IgnoresNullArguments(
    const ResolvedNonScalarFunctionCallBase* aggregate_function);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_AGGREGATE_NULL_HANDLING_H_
