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

#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/types/vector_value_codec.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

class VectorValueUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(vector_type_, MakeVectorType(&type_factory_));
  }

  absl::StatusOr<Value> MakeVector(VectorEncodingId::Id encoding,
                                   absl::string_view payload) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string bytes,
                     MakeVectorValueBytes(encoding, payload));
    return MakeVectorFromBytes(bytes);
  }

  // Wraps `bytes` as the backing value of a VECTOR without validating them, so
  // that tests can build VECTOR values that MakeVectorValueBytes would refuse.
  absl::StatusOr<Value> MakeVectorFromBytes(absl::string_view bytes) {
    return Value::Declarative(vector_type_->AsDeclarativeType(),
                              Value::Bytes(bytes));
  }

  TypeFactory type_factory_;
  const Type* vector_type_ = nullptr;
};

TEST_F(VectorValueUtilTest, ParseVectorValue) {
  const std::string payload = MakeFloat32VectorPayload({1.5f, -2.25f});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector,
                       MakeVector(VectorEncodingId::FLOAT32, payload));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VectorValueProto vector_value, ParseVectorValue(vector));
  EXPECT_EQ(vector_value.encoding(), VectorEncodingId::FLOAT32);
  EXPECT_EQ(vector_value.data(), payload);
}

TEST_F(VectorValueUtilTest, DecodeFloat32VectorValue) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector,
                       MakeVector(VectorEncodingId::FLOAT32,
                                  MakeFloat32VectorPayload({1.5f, -2.25f})));

  EXPECT_THAT(DecodeFloat32VectorValue(vector),
              IsOkAndHolds(ElementsAre(1.5f, -2.25f)));
}

TEST_F(VectorValueUtilTest, NullVectorIsRejected) {
  EXPECT_THAT(ParseVectorValue(Value::Null(vector_type_)),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Expected a non-NULL VECTOR value")));
  EXPECT_THAT(DecodeFloat32VectorValue(Value::Null(vector_type_)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(VectorValueUtilTest, NonVectorIsRejected) {
  EXPECT_THAT(ParseVectorValue(Value::Bytes("abc")),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Expected a VECTOR value, got BYTES")));
}

TEST_F(VectorValueUtilTest, MalformedBackingBytesAreRejected) {
  // A lone byte with the varint continuation bit set is a truncated tag.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector, MakeVectorFromBytes("\xff"));

  EXPECT_THAT(ParseVectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Malformed VECTOR backing value")));
  EXPECT_THAT(DecodeFloat32VectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Malformed VECTOR backing value")));
}

TEST_F(VectorValueUtilTest, MissingEncodingIsRejected) {
  VectorValueProto vector_value;
  vector_value.set_data(MakeFloat32VectorPayload({1.5f}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector,
                       MakeVectorFromBytes(vector_value.SerializeAsString()));

  EXPECT_THAT(ParseVectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("VECTOR value has no encoding")));
  EXPECT_THAT(DecodeFloat32VectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("VECTOR value has no encoding")));
}

TEST_F(VectorValueUtilTest, UnrecognizedEncodingIsRejected) {
  // 254 is not an id that vector_encoding_id.proto declares.
  VectorValueProto vector_value;
  vector_value.set_encoding(254);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector,
                       MakeVectorFromBytes(vector_value.SerializeAsString()));

  EXPECT_THAT(ParseVectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unrecognized VECTOR encoding: 254")));
  EXPECT_THAT(DecodeFloat32VectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unrecognized VECTOR encoding: 254")));
}

// The payload is opaque to ParseVectorValue, so only decoding it notices that
// a FLOAT32 payload is not a whole number of elements.
TEST_F(VectorValueUtilTest, MalformedFloat32PayloadIsRejectedWhenDecoding) {
  VectorValueProto vector_value;
  vector_value.set_encoding(VectorEncodingId::FLOAT32);
  vector_value.set_data("abc");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector,
                       MakeVectorFromBytes(vector_value.SerializeAsString()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VectorValueProto parsed, ParseVectorValue(vector));
  EXPECT_EQ(parsed.data(), "abc");
  EXPECT_THAT(DecodeFloat32VectorValue(vector),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Malformed FLOAT32 VECTOR payload")));
}

}  // namespace
}  // namespace googlesql
