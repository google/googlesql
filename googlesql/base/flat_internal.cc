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
#include "googlesql/base/flat_internal.h"

namespace googlesql_base {
// This class has no state, and the only reason it's not a POD is to disallow
// brace-initialization.
sorted_unique_container_t sorted_unique_container{};  // NOLINT
}  // namespace googlesql_base
