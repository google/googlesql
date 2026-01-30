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

#include "googlesql/public/types/extended_type.h"

#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "absl/status/statusor.h"

namespace googlesql {

bool ExtendedType::IsSupportedType(
    const LanguageOptions& language_options) const {
  return language_options.LanguageFeatureEnabled(FEATURE_EXTENDED_TYPES);
}

absl::StatusOr<std::string> ExtendedType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode) const {
  const TypeParameters& type_params = type_modifiers.type_parameters();
  const Collation& collation = type_modifiers.collation();
  GOOGLESQL_RET_CHECK(type_params.IsEmpty());
  GOOGLESQL_RET_CHECK(collation.Empty());
  return TypeName(mode);
}

}  // namespace googlesql
