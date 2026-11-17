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

#ifndef GOOGLESQL_PUBLIC_TYPES_VECTOR_VALUE_CODEC_H_
#define GOOGLESQL_PUBLIC_TYPES_VECTOR_VALUE_CODEC_H_

#include <string>
#include <vector>

#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// Reads and writes the bytes backing a VECTOR value, which are a serialized
// VectorValueProto. See vector_value.proto for what that message means.

// Builds the bytes of a VECTOR value from its `encoding` and its
// already-encoded `payload`. Returns an error rather than bytes that
// ParseVectorValueBytes would reject: if `encoding` is
// UNKNOWN_VECTOR_ENCODING or not a recognized VectorEncodingId::Id, or if
// `encoding` is FLOAT32 and `payload` is not a whole number of float32s.
absl::StatusOr<std::string> MakeVectorValueBytes(VectorEncodingId::Id encoding,
                                                 absl::string_view payload);

// Parses the bytes of a VECTOR value. On success, `encoding()` of the result
// is a valid, non-zero VectorEncodingId::Id. Returns an error if the bytes are
// not a VectorValueProto, or carry an encoding this binary does not recognize.
absl::StatusOr<VectorValueProto> ParseVectorValueBytes(
    absl::string_view vector_bytes);

// Builds the FLOAT32 payload holding `elements`.
std::string MakeFloat32VectorPayload(absl::Span<const float> elements);

// Extracts the elements of a FLOAT32 `payload`. Returns an error if `payload`
// is not a whole number of float32s.
absl::StatusOr<std::vector<float>> DecodeFloat32VectorPayload(
    absl::string_view payload);

// Formats the elements of a FLOAT32 vector as a comma-separated list, e.g.
// "1.5, -2.25". Each element is printed so that it parses back to the same
// float.
std::string FormatFloat32VectorElements(absl::Span<const float> elements);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_VECTOR_VALUE_CODEC_H_
