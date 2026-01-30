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

#ifndef GOOGLESQL_COMMON_FUNCTION_SIGNATURE_TESTUTIL_H_
#define GOOGLESQL_COMMON_FUNCTION_SIGNATURE_TESTUTIL_H_

#include <vector>

#include "googlesql/public/function.pb.h"

namespace googlesql {

// A group of related SignatureArgumentKinds.
struct SignatureArgumentKindGroup {
  // <T>
  SignatureArgumentKind kind;
  // ARRAY<T>
  SignatureArgumentKind array_kind;
};

// Returns a list of groups of the related SignatureArgumentKinds.
std::vector<SignatureArgumentKindGroup> GetRelatedSignatureArgumentGroup();

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_FUNCTION_SIGNATURE_TESTUTIL_H_
