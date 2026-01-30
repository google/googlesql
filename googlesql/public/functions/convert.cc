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

#include "googlesql/public/functions/convert.h"

#include "absl/base/attributes.h"

namespace googlesql {
namespace functions {
namespace internal {

ABSL_CONST_INIT const char* const kConvertOverflowInt32 =
    "int32 out of range: ";
ABSL_CONST_INIT const char* const kConvertOverflowInt64 =
    "int64 out of range: ";
ABSL_CONST_INIT const char* const kConvertOverflowUint32 =
    "uint32 out of range: ";
ABSL_CONST_INIT const char* const kConvertOverflowUint64 =
    "uint64 out of range: ";
ABSL_CONST_INIT const char* const kConvertOverflowFloat =
    "float out of range: ";
ABSL_CONST_INIT const char* const kConvertNonFinite =
    "Illegal conversion of non-finite floating point number to an integer: ";

}  // namespace internal
}  // namespace functions
}  // namespace googlesql
