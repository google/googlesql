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

#include "googlesql/common/json_conversion_utils.h"

#include "googlesql/public/types/type.h"
#include "googlesql/public/types/vector_type_util.h"
#include "absl/base/nullability.h"

namespace googlesql {

const Type* /*absl_nullable*/ GetUnsupportedTypeForJsonConversion(
    const Type* /*absl_nonnull*/ type) {
  if (type->IsMap()) {
    return type;
  }

  if (type->IsTokenList()) {
    return type;
  }

  // TODO: b/483763571 - Support JSON conversion for VECTOR.
  if (IsVectorType(type)) {
    return type;
  }

  for (const Type* child : type->ComponentTypes()) {
    const Type* unsupported_type = GetUnsupportedTypeForJsonConversion(child);
    if (unsupported_type != nullptr) {
      return unsupported_type;
    }
  }

  // NOTE: This is a denylist; any type not rejected above is treated as
  // supported, including types added in the future. When adding a new type
  // that `functions::ToJson()` cannot encode, reject it here so that the error
  // is reported at analysis time rather than at runtime.
  // TODO: b/483763571 - Replace this denylist with an allowlist shared with
  // `IsTypeCastableToJson()`, so that new types are unsupported by default.
  return nullptr;
}

}  // namespace googlesql
