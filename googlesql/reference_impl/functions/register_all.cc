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

#include "googlesql/reference_impl/functions/register_all.h"

#include "googlesql/reference_impl/functions/compression.h"
#include "googlesql/reference_impl/functions/hash.h"
#include "googlesql/reference_impl/functions/json.h"
#include "googlesql/reference_impl/functions/map.h"
#include "googlesql/reference_impl/functions/range.h"
#include "googlesql/reference_impl/functions/string_with_collation.h"
#include "googlesql/reference_impl/functions/uuid.h"

#include "googlesql/reference_impl/functions/graph.h"

namespace googlesql {

void RegisterAllOptionalBuiltinFunctions() {
  RegisterBuiltinJsonFunctions();
  RegisterBuiltinGraphFunctions();
  RegisterBuiltinUuidFunctions();
  RegisterBuiltinHashFunctions();
  RegisterBuiltinStringWithCollationFunctions();
  RegisterBuiltinRangeFunctions();
  RegisterBuiltinMapFunctions();
  RegisterBuiltinCompressionFunctions();
}

}  // namespace googlesql
