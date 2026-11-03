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

#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::absl_testing::StatusIs;

TEST(VectorValueCodecTest, Float32ValueRoundTrips) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::string bytes,
                       MakeVectorValueBytes(VectorEncodingId::FLOAT32,
                                            MakeFloat32VectorPayload(
                                                {1.0f, -2.5f, 0.0f, 1e10f})));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VectorValueProto vector_value,
                       ParseVectorValueBytes(bytes));
  EXPECT_EQ(vector_value.encoding(), VectorEncodingId::FLOAT32);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<float> decoded,
                       DecodeFloat32VectorPayload(vector_value.data()));
  EXPECT_THAT(decoded, ElementsAre(1.0f, -2.5f, 0.0f, 1e10f));
}

// The bytes are a serialized VectorValueProto, which is what every other
// reader and writer of a VECTOR value relies on. Pin that with a plain proto
// parse rather than with the codec, which could drift along with the writer.
TEST(VectorValueCodecTest, BytesAreASerializedVectorValueProto) {
  const std::string payload = MakeFloat32VectorPayload({1.0f, 2.0f});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::string bytes,
      MakeVectorValueBytes(VectorEncodingId::FLOAT32, payload));

  VectorValueProto vector_value;
  ASSERT_TRUE(vector_value.ParseFromString(bytes));
  EXPECT_EQ(vector_value.encoding(), VectorEncodingId::FLOAT32);
  EXPECT_EQ(vector_value.data(), payload);
}

// A FLOAT32 payload is a dense array of little-endian IEEE-754 binary32
// values, four bytes per element, as that encoding defines.
TEST(VectorValueCodecTest, Float32PayloadIsDenseLittleEndian) {
  EXPECT_EQ(MakeFloat32VectorPayload({1.0f, 2.0f}),
            absl::string_view("\x00\x00\x80\x3F\x00\x00\x00\x40", 8));
}

TEST(VectorValueCodecTest, EmptyFloat32VectorRoundTrips) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::string bytes,
      MakeVectorValueBytes(VectorEncodingId::FLOAT32,
                           MakeFloat32VectorPayload(std::vector<float>())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VectorValueProto vector_value,
                       ParseVectorValueBytes(bytes));
  EXPECT_THAT(vector_value.data(), IsEmpty());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<float> decoded,
                       DecodeFloat32VectorPayload(vector_value.data()));
  EXPECT_THAT(decoded, IsEmpty());
}

// A value written by a newer binary may carry fields this one does not know.
// It must still be readable, which is what lets VectorValueProto evolve.
TEST(VectorValueCodecTest, UnknownFieldsAreTolerated) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::string bytes,
                       MakeVectorValueBytes(VectorEncodingId::FLOAT32,
                                            MakeFloat32VectorPayload({1.0f})));
  // Field 99, varint, value 1: a field no version of VectorValueProto has.
  bytes.append("\x98\x06\x01");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VectorValueProto vector_value,
                       ParseVectorValueBytes(bytes));
  EXPECT_EQ(vector_value.encoding(), VectorEncodingId::FLOAT32);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<float> decoded,
                       DecodeFloat32VectorPayload(vector_value.data()));
  EXPECT_THAT(decoded, ElementsAre(1.0f));
}

// The writer refuses exactly what the reader would refuse, so it can never
// produce a value that cannot be read back.
TEST(VectorValueCodecTest, WritingWithoutAnEncodingIsRejected) {
  EXPECT_THAT(
      MakeVectorValueBytes(VectorEncodingId::UNKNOWN_VECTOR_ENCODING, ""),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("A VECTOR value must have an encoding")));
}

TEST(VectorValueCodecTest, WritingAnUnrecognizedEncodingIsRejected) {
  // 254 is not an id that vector_encoding_id.proto declares.
  EXPECT_THAT(MakeVectorValueBytes(static_cast<VectorEncodingId::Id>(254), ""),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unrecognized VECTOR encoding: 254")));
}

TEST(VectorValueCodecTest, WritingAMalformedFloat32PayloadIsRejected) {
  EXPECT_THAT(MakeVectorValueBytes(VectorEncodingId::FLOAT32, "abc"),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Malformed FLOAT32 VECTOR payload")));
}

// An absent encoding and UNKNOWN_VECTOR_ENCODING are the same thing on the
// wire, so neither names an encoding.
TEST(VectorValueCodecTest, MissingEncodingIsRejected) {
  EXPECT_THAT(ParseVectorValueBytes(""),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("VECTOR value has no encoding")));

  VectorValueProto vector_value;
  vector_value.set_data("payload");
  EXPECT_THAT(ParseVectorValueBytes(vector_value.SerializeAsString()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("VECTOR value has no encoding")));
}

TEST(VectorValueCodecTest, NonProtoBytesAreMalformed) {
  // A lone byte with the varint continuation bit set is a truncated tag.
  EXPECT_THAT(ParseVectorValueBytes("\xff"),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Malformed VECTOR backing value")));
}

TEST(VectorValueCodecTest, UnrecognizedEncodingIsRejected) {
  // 254 is not an id that vector_encoding_id.proto declares.
  VectorValueProto vector_value;
  vector_value.set_encoding(254);

  EXPECT_THAT(ParseVectorValueBytes(vector_value.SerializeAsString()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unrecognized VECTOR encoding: 254")));
}

TEST(VectorValueCodecTest, Float32PayloadMustBeAWholeNumberOfElements) {
  EXPECT_THAT(DecodeFloat32VectorPayload("abc"),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Malformed FLOAT32 VECTOR payload")));
}

}  // namespace
}  // namespace googlesql
