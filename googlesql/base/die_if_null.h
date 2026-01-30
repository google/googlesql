//
// Copyright 2022 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_DIE_IF_NULL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_DIE_IF_NULL_H_

#include "googlesql/base/check.h"

#define GOOGLESQL_DIE_IF_NULL(expr) ::googlesql_internal::DieIfNull(expr)

namespace googlesql_internal {
template <typename T>
inline T DieIfNull(T&& t) {
  ABSL_CHECK(t != nullptr);
  return std::forward<T>(t);
}
}  // namespace googlesql_internal

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_DIE_IF_NULL_H_
