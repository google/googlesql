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

#include "googlesql/analyzer/resolver_util.h"

#include <string>

#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/types/type.h"
#include "absl/algorithm/container.h"
#include "absl/types/span.h"

namespace googlesql {

std::string GetTypeCompatibilityExplanation(
    absl::Span<const InputArgumentType> types) {
  if (absl::c_any_of(types, [](const InputArgumentType& type) {
        return type.type() != nullptr && type.type()->IsMeasureType();
      })) {
    return "; each MEASURE type is unique and does not coerce to other MEASURE "
           "types";
  }
  return "";
}

}  // namespace googlesql
