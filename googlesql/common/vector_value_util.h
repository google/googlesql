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

#ifndef GOOGLESQL_COMMON_VECTOR_VALUE_UTIL_H_
#define GOOGLESQL_COMMON_VECTOR_VALUE_UTIL_H_

#include <vector>

#include "googlesql/public/proto/vector_value.pb.h"
#include "googlesql/public/value.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Helpers that read the VectorValueProto backing a VECTOR Value.
//
// These sit on top of vector_value_codec, which works on the backing bytes.
// The codec cannot take a Value itself, because the VECTOR type depends on the
// codec and Value depends on the types.

// Returns the VectorValueProto backing `vector`, which must be a non-NULL
// VECTOR value. Returns an internal error if its bytes are malformed.
//
// That the backing bytes are a serialized VectorValueProto is a choice of the
// reference implementation, not part of the language: see "Amendment 2:
// self-described value wire format" in (broken link).
absl::StatusOr<VectorValueProto> ParseVectorValue(const Value& vector);

// Returns the elements of `vector`, which must be a non-NULL VECTOR value.
// Returns an Unimplemented error if `vector` is not FLOAT32, since other
// encodings are quantized and have no element list to decode.
absl::StatusOr<std::vector<float>> DecodeFloat32VectorValue(
    const Value& vector);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_VECTOR_VALUE_UTIL_H_
