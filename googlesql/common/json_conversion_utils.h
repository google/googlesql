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

#ifndef GOOGLESQL_COMMON_JSON_CONVERSION_UTILS_H_
#define GOOGLESQL_COMMON_JSON_CONVERSION_UTILS_H_

#include "googlesql/public/types/type.h"
#include "absl/base/nullability.h"

namespace googlesql {

// Traverses `type` and its component types, returning the first type that is
// not supported in JSON conversion functions (e.g. TO_JSON, TO_JSON_STRING,
// JSON_ARRAY or JSON_OBJECT). Returns nullptr if `type` is fully supported.
//
// This is the single source of truth for which argument types JSON conversion
// functions accept. It is used by the resolver to reject unsupported arguments
// to JSON conversion functions and unsupported dynamic graph property values,
// and by other components (e.g. the random query generator) that need to agree
// with the resolver.
//
// This is currently a denylist: types that are not explicitly rejected are
// treated as supported, so new types must be added here if
// `functions::ToJson()` cannot encode them.
// TODO: b/483763571 - Expand this function to cover types more exhaustively.
// Desired behavior for extended types and existing unsupported types will need
// to be considered.
const Type* /*absl_nullable*/ GetUnsupportedTypeForJsonConversion(
    const Type* /*absl_nonnull*/ type);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_JSON_CONVERSION_UTILS_H_
