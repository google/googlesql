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

#include "googlesql/public/types/vector_value_codec.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/common/string_util.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "absl/base/casts.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "google/protobuf/io/coded_stream.h"

namespace googlesql {

namespace {

using ::google::protobuf::io::CodedInputStream;
using ::google::protobuf::io::CodedOutputStream;

// The number of bytes one element occupies in a FLOAT32 payload.
constexpr size_t kFloat32ElementSize = sizeof(uint32_t);

}  // namespace

absl::StatusOr<std::string> MakeVectorValueBytes(VectorEncodingId::Id encoding,
                                                 absl::string_view payload) {
  // Hold the writer to exactly what ParseVectorValueBytes accepts, so that it
  // can never produce a value that cannot be read back.
  GOOGLESQL_RET_CHECK_NE(encoding, VectorEncodingId::UNKNOWN_VECTOR_ENCODING)
      << "A VECTOR value must have an encoding";
  GOOGLESQL_RET_CHECK(VectorEncodingId_Id_IsValid(encoding))
      << "Unrecognized VECTOR encoding: " << static_cast<int>(encoding);
  // The payload of any other encoding is opaque here, but a FLOAT32 payload
  // can be checked.
  if (encoding == VectorEncodingId::FLOAT32) {
    GOOGLESQL_RET_CHECK(payload.size() % kFloat32ElementSize == 0)
        << "Malformed FLOAT32 VECTOR payload of " << payload.size() << " bytes";
  }
  VectorValueProto vector_value;
  vector_value.set_encoding(encoding);
  vector_value.set_data(payload);
  return vector_value.SerializeAsString();
}

absl::StatusOr<VectorValueProto> ParseVectorValueBytes(
    absl::string_view vector_bytes) {
  VectorValueProto vector_value;
  GOOGLESQL_RET_CHECK(vector_value.ParseFromString(vector_bytes))
      << "Malformed VECTOR backing value";
  GOOGLESQL_RET_CHECK_NE(vector_value.encoding(),
               VectorEncodingId::UNKNOWN_VECTOR_ENCODING)
      << "VECTOR value has no encoding";
  GOOGLESQL_RET_CHECK(VectorEncodingId_Id_IsValid(vector_value.encoding()))
      << "Unrecognized VECTOR encoding: " << vector_value.encoding();
  return vector_value;
}

std::string MakeFloat32VectorPayload(absl::Span<const float> elements) {
  std::string payload(elements.size() * kFloat32ElementSize, '\0');
  uint8_t* cursor = reinterpret_cast<uint8_t*>(payload.data());
  for (float element : elements) {
    cursor = CodedOutputStream::WriteLittleEndian32ToArray(
        absl::bit_cast<uint32_t>(element), cursor);
  }
  return payload;
}

absl::StatusOr<std::vector<float>> DecodeFloat32VectorPayload(
    absl::string_view payload) {
  GOOGLESQL_RET_CHECK(payload.size() % kFloat32ElementSize == 0)
      << "Malformed FLOAT32 VECTOR payload of " << payload.size() << " bytes";
  std::vector<float> elements;
  elements.reserve(payload.size() / kFloat32ElementSize);
  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(payload.data());
  const uint8_t* const end = cursor + payload.size();
  while (cursor < end) {
    uint32_t bits = 0;
    cursor = CodedInputStream::ReadLittleEndian32FromArray(cursor, &bits);
    elements.push_back(absl::bit_cast<float>(bits));
  }
  return elements;
}

std::string FormatFloat32VectorElements(absl::Span<const float> elements) {
  return absl::StrJoin(elements, ", ", [](std::string* out, float element) {
    absl::StrAppend(out, RoundTripFloatToString(element));
  });
}

}  // namespace googlesql
