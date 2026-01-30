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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_UUID_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_UUID_H_

#include <string>

#include "googlesql/public/uuid_value.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// GENERATE_UUID() -> STRING
//
// Generates a V4-compliant UUID using the given random number generator.
//
// If the generator is initialized using a seed, the seed must contain at least
// 128 bits of data to ensure proper UUID uniqueness.
//
std::string GenerateUuid(absl::BitGenRef gen);

// NEW_UUID() -> UUID
//
// Generates a V4-compliant UUID using the given random number generator.
//
// If the generator is initialized using a seed, the seed must contain at least
// 128 bits of data to ensure proper UUID uniqueness.
UuidValue NewUuid(absl::BitGenRef gen);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_UUID_H_
