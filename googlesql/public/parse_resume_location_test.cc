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

#include "googlesql/public/parse_resume_location.h"

#include <cstdint>
#include <string>
#include <utility>

#include "google/protobuf/descriptor.h"
#include "googlesql/public/parse_resume_location.pb.h"
#include "gtest/gtest.h"

namespace googlesql {

class ParseResumeLocationTest : public ::testing::Test {
 protected:
  // Provide access to private members to tests.
  const std::string& GetInputStorage(const ParseResumeLocation& input) {
    return input.input_storage_;
  }
  const std::string& GetFilenameStorage(const ParseResumeLocation& input) {
    return input.filename_storage_;
  }
};

TEST_F(ParseResumeLocationTest, FromProto) {
  std::string input = "12345678900*0000";
  int32_t byte_position = 12;
  ParseResumeLocationProto proto;
  proto.set_input(input);
  proto.set_byte_position(byte_position);
  proto.set_allow_resume(true);
  ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromProto(proto);

  EXPECT_EQ(input, parse_resume_location.input());
  EXPECT_EQ(byte_position, parse_resume_location.byte_position());
  EXPECT_EQ(true, parse_resume_location.allow_resume());
}

TEST_F(ParseResumeLocationTest, Serialize) {
  std::string filename = "filename_Serialize";
  std::string input = "12345678900*0000";
  int32_t byte_position = 12;
  ParseResumeLocationProto proto;
  ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromString(filename, input);
  parse_resume_location.set_byte_position(byte_position);
  parse_resume_location.DisallowResume();
  parse_resume_location.Serialize(&proto);

  EXPECT_EQ(filename, proto.filename())
      << "Serialize input error.";
  EXPECT_EQ(input, proto.input())
      << "Serialize input error.";
  EXPECT_EQ(byte_position, proto.byte_position())
      << "Serialize byte position error.";
  EXPECT_EQ(false, proto.allow_resume())
      << "Serialize allow_resume error.";
}

TEST_F(ParseResumeLocationTest, Copy) {
  // Test with long and short inputs, with FromString-constructed objects (which
  // point input() at their input_storage_) and FromStringView-constructed
  // objects (which don't).
  for (const char* input : {"shortinput",
                            "longinputtoolongtocontaininlineinastring0123456789"
                            "01234567890123456789"}) {
    constexpr int32_t kBytePosition = 5;
    const char* filename = "filename_Copy";
    ParseResumeLocation original =
        ParseResumeLocation::FromString(filename, input);
    original.set_byte_position(kBytePosition);
    original.DisallowResume();

    const ParseResumeLocation copy(original);
    EXPECT_EQ(copy.byte_position(), kBytePosition);
    EXPECT_EQ(copy.allow_resume(), false);
    EXPECT_EQ(copy.input().data(), GetInputStorage(copy).data());
    EXPECT_EQ(copy.filename().data(), GetFilenameStorage(copy).data());

    ParseResumeLocation assigned_copy =
        ParseResumeLocation::FromString("bogus_filename", "bogus");
    assigned_copy = original;
    EXPECT_EQ(assigned_copy.byte_position(), kBytePosition);
    EXPECT_EQ(assigned_copy.allow_resume(), false);
    EXPECT_EQ(assigned_copy.input().data(),
              GetInputStorage(assigned_copy).data());
    EXPECT_EQ(assigned_copy.filename().data(),
              GetFilenameStorage(assigned_copy).data());
  }
  for (const char* input : {"shortinput",
                            "longinputtoolongtocontaininlineinastring0123456789"
                            "01234567890123456789"}) {
    constexpr int32_t kBytePosition = 7;
    const char* filename = "filename_Copy_2";
    ParseResumeLocation original =
        ParseResumeLocation::FromStringView(filename, input);
    original.set_byte_position(kBytePosition);
    original.DisallowResume();

    const ParseResumeLocation copy(original);
    EXPECT_EQ(copy.byte_position(), kBytePosition);
    EXPECT_EQ(copy.allow_resume(), false);
    EXPECT_EQ(copy.input().data(), input);
    EXPECT_EQ(copy.filename().data(), filename);

    ParseResumeLocation assigned_copy =
        ParseResumeLocation::FromString("bogus_filename", "bogus");
    assigned_copy = original;
    EXPECT_EQ(assigned_copy.byte_position(), kBytePosition);
    EXPECT_EQ(assigned_copy.allow_resume(), false);
    EXPECT_EQ(assigned_copy.input().data(), input);
    EXPECT_EQ(assigned_copy.filename().data(), filename);
  }
}

TEST_F(ParseResumeLocationTest, Move) {
  // Test with long and short inputs, with FromString-constructed objects (which
  // point input() at their input_storage_) and FromStringView-constructed
  // objects (which don't).

  // FromString
  for (const char* input : {"shortinput",
                            "longinputtoolongtocontaininlineinastring0123456789"
                            "01234567890123456789"}) {
    constexpr int32_t kBytePosition = 5;
    const char* filename = "filename_Move";
    ParseResumeLocation original1 =
        ParseResumeLocation::FromString(filename, input);
    original1.set_byte_position(kBytePosition);
    original1.DisallowResume();

    const ParseResumeLocation copy(std::move(original1));
    EXPECT_EQ(copy.byte_position(), kBytePosition);
    EXPECT_EQ(copy.allow_resume(), false);
    EXPECT_EQ(copy.input().data(), GetInputStorage(copy).data());
    EXPECT_EQ(copy.filename().data(), GetFilenameStorage(copy).data());

    ParseResumeLocation original2 =
        ParseResumeLocation::FromString(filename, input);
    original2.set_byte_position(kBytePosition);
    original2.DisallowResume();
    ParseResumeLocation assigned_copy =
        ParseResumeLocation::FromString("bogus_filename", "bogus");
    assigned_copy = std::move(original2);
    EXPECT_EQ(assigned_copy.byte_position(), kBytePosition);
    EXPECT_EQ(assigned_copy.allow_resume(), false);
    EXPECT_EQ(assigned_copy.input().data(),
              GetInputStorage(assigned_copy).data());
    EXPECT_EQ(assigned_copy.filename().data(),
              GetFilenameStorage(assigned_copy).data());
  }

  // FromStringView
  for (const char* input : {"shortinput",
                            "longinputtoolongtocontaininlineinastring0123456789"
                            "01234567890123456789"}) {
    constexpr int32_t kBytePosition = 7;
    const char* filename = "filename_Move_2";
    ParseResumeLocation original1 =
        ParseResumeLocation::FromStringView(filename, input);
    original1.set_byte_position(kBytePosition);
    original1.DisallowResume();

    const ParseResumeLocation copy(std::move(original1));
    EXPECT_EQ(copy.byte_position(), kBytePosition);
    EXPECT_EQ(copy.allow_resume(), false);
    EXPECT_EQ(copy.input().data(), input);
    EXPECT_EQ(copy.filename().data(), filename);

    ParseResumeLocation original2 =
        ParseResumeLocation::FromStringView(filename, input);
    original2.set_byte_position(kBytePosition);
    original2.DisallowResume();
    ParseResumeLocation assigned_copy =
        ParseResumeLocation::FromString("bogus_filename", "bogus");
    assigned_copy = std::move(original2);
    EXPECT_EQ(assigned_copy.byte_position(), kBytePosition);
    EXPECT_EQ(assigned_copy.allow_resume(), false);
    EXPECT_EQ(assigned_copy.input().data(), input);
    EXPECT_EQ(assigned_copy.filename().data(), filename);
  }
}

TEST_F(ParseResumeLocationTest, ClassAndProtoSize) {

  EXPECT_EQ(4, ParseResumeLocationProto::descriptor()->field_count())
      << "The number of fields in ParseResumeLocationProto has changed, please "
      << "also update the serialization code accordingly.";
}

}  // namespace googlesql
