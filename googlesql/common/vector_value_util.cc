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

#include "googlesql/common/vector_value_util.h"

#include <vector>

#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/types/vector_value_codec.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

absl::StatusOr<VectorValueProto> ParseVectorValue(const Value& vector) {
  GOOGLESQL_RET_CHECK(IsVectorType(vector.type()))
      << "Expected a VECTOR value, got " << vector.type()->DebugString();
  GOOGLESQL_RET_CHECK(!vector.is_null()) << "Expected a non-NULL VECTOR value";
  GOOGLESQL_ASSIGN_OR_RETURN(Value vector_bytes, vector.backing_value());
  return ParseVectorValueBytes(vector_bytes.bytes_value());
}

absl::StatusOr<std::vector<float>> DecodeFloat32VectorValue(
    const Value& vector) {
  GOOGLESQL_ASSIGN_OR_RETURN(VectorValueProto vector_value, ParseVectorValue(vector));
  const auto encoding =
      static_cast<VectorEncodingId::Id>(vector_value.encoding());
  if (encoding != VectorEncodingId::FLOAT32) {
    return absl::UnimplementedError(
        absl::StrCat("Decoding a VECTOR is not implemented for encoding: ",
                     VectorEncodingId_Id_Name(encoding)));
  }
  return DecodeFloat32VectorPayload(vector_value.data());
}

}  // namespace googlesql
