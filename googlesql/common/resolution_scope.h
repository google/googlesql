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

#ifndef GOOGLESQL_COMMON_RESOLUTION_SCOPE_H_
#define GOOGLESQL_COMMON_RESOLUTION_SCOPE_H_

#include "googlesql/parser/parse_tree.h"
#include "absl/status/statusor.h"

namespace googlesql {

// The scope of catalog to use when resolving module objects.
// Reference: (broken link).
enum class ResolutionScope { kBuiltin, kGlobal };

// Determines the resolution scope option for the given statement, or the given
// default if the statement does not have a resolution scope option. Can only be
// called with supported ASTs for statements which can be in modules and have a
// resolution scope option.
absl::StatusOr<ResolutionScope> GetResolutionScopeOption(
    const ASTStatement* stmt, ResolutionScope default_resolution_scope);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_RESOLUTION_SCOPE_H_
