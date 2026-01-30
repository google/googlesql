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

#ifndef GOOGLESQL_COMMON_UNICODE_UTILS_H_
#define GOOGLESQL_COMMON_UNICODE_UTILS_H_

#include <string>

#include "absl/flags/declare.h"
#include "absl/strings/string_view.h"

ABSL_DECLARE_FLAG(bool, googlesql_idstring_allow_unicode_characters);

namespace googlesql {
// Casefold (lowercase) the input string to result. The function relies on
// ASCII lowercase in case of failure or if the input string is completely
// made of ASCII characters.
std::string GetNormalizedAndCasefoldedString(absl::string_view str);
}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_UNICODE_UTILS_H_
