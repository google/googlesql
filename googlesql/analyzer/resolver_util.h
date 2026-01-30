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

#ifndef GOOGLESQL_ANALYZER_RESOLVER_UTIL_H_
#define GOOGLESQL_ANALYZER_RESOLVER_UTIL_H_

#include <string>

#include "googlesql/public/input_argument_type.h"
#include "absl/types/span.h"

namespace googlesql {

// Returns an explanation to append to a type incompatibility error message.
//
// If we just print out type names, some error messages may be confusing. For
// example, two different MEASURE<INT64> types are incompatible, but they
// both print as `MEASURE<INT64>`. This can be confusing for users, so we
// append an additional explanation in such cases.
std::string GetTypeCompatibilityExplanation(
    absl::Span<const InputArgumentType> types);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_RESOLVER_UTIL_H_
