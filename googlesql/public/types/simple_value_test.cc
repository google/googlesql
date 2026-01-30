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

#include "googlesql/public/types/simple_value.h"

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {

class SimpleValueTest : public ::testing::Test {
 public:
  SimpleValueTest() = default;
  SimpleValueTest(const SimpleValueTest&) = delete;
  SimpleValueTest& operator=(const SimpleValueTest&) = delete;
  ~SimpleValueTest() override = default;

  bool StringPtrRefCountIsOne(const SimpleValue& value) {
    return value.string_ptr_->RefCountIsOne();
  }
};

TEST_F(SimpleValueTest, SimpleValueTypeInvalid) {
  SimpleValue invalid1;
  EXPECT_FALSE(invalid1.IsValid());
  SimpleValue invalid2;
  EXPECT_FALSE(invalid2.IsValid());

  EXPECT_TRUE(invalid1 == invalid2);
  EXPECT_TRUE(invalid1.Equals(invalid2));
  EXPECT_FALSE(invalid1 != invalid2);
}

TEST_F(SimpleValueTest, SimpleValueTypeInt64) {
  SimpleValue int64_value = SimpleValue::Int64(0x12345);
  EXPECT_TRUE(int64_value.IsValid());
  EXPECT_TRUE(int64_value.has_int64_value());
  EXPECT_FALSE(int64_value.has_string_value());
  EXPECT_EQ(int64_value.int64_value(), 0x12345);

  {
    SimpleValue another_int64_value = SimpleValue::Int64(0x12345);
    EXPECT_TRUE(int64_value.Equals(another_int64_value));
    EXPECT_TRUE(int64_value == another_int64_value);
    EXPECT_FALSE(int64_value != another_int64_value);
  }

  {
    SimpleValue another_int64_value = SimpleValue::Int64(0x54321);
    EXPECT_FALSE(int64_value.Equals(another_int64_value));
    EXPECT_FALSE(int64_value == another_int64_value);
    EXPECT_TRUE(int64_value != another_int64_value);
  }

  SimpleValue invalid_value;
  EXPECT_FALSE(int64_value.Equals(invalid_value));

  SimpleValueProto proto;
  GOOGLESQL_ASSERT_OK(int64_value.Serialize(&proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(SimpleValue deserialized_int64_value,
                       SimpleValue::Deserialize(proto));
  EXPECT_TRUE(int64_value.Equals(deserialized_int64_value));
}

TEST_F(SimpleValueTest, SimpleValueTypeString) {
  SimpleValue string_value = SimpleValue::String("abcdefg");
  EXPECT_TRUE(string_value.IsValid());
  EXPECT_TRUE(string_value.has_string_value());
  EXPECT_FALSE(string_value.has_int64_value());
  EXPECT_EQ(string_value.string_value(), "abcdefg");

  {
    SimpleValue another_string_value = SimpleValue::String("abcdefg");
    EXPECT_TRUE(string_value.Equals(another_string_value));
    EXPECT_TRUE(string_value == another_string_value);
    EXPECT_FALSE(string_value != another_string_value);
  }

  {
    SimpleValue another_string_value = SimpleValue::String("1234567");
    EXPECT_FALSE(string_value.Equals(another_string_value));
    EXPECT_FALSE(string_value == another_string_value);
    EXPECT_TRUE(string_value != another_string_value);
  }

  SimpleValue invalid_value;
  EXPECT_FALSE(string_value.Equals(invalid_value));

  SimpleValue int64_value = SimpleValue::Int64(0x12345);
  EXPECT_FALSE(string_value.Equals(int64_value));

  SimpleValueProto proto;
  GOOGLESQL_ASSERT_OK(string_value.Serialize(&proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(SimpleValue deserialized_string_value,
                       SimpleValue::Deserialize(proto));
  EXPECT_TRUE(string_value.Equals(deserialized_string_value));
}

TEST_F(SimpleValueTest, SimpleValueTypeBool) {
  SimpleValue bool_value = SimpleValue::Bool(true);
  EXPECT_TRUE(bool_value.IsValid());
  EXPECT_TRUE(bool_value.has_bool_value());
  EXPECT_FALSE(bool_value.has_int64_value());
  EXPECT_EQ(bool_value.bool_value(), true);

  {
    SimpleValue another_bool_value = SimpleValue::Bool(true);
    EXPECT_TRUE(bool_value.Equals(another_bool_value));
    EXPECT_TRUE(bool_value == another_bool_value);
    EXPECT_FALSE(bool_value != another_bool_value);
  }

  {
    SimpleValue another_bool_value = SimpleValue::Bool(false);
    EXPECT_FALSE(bool_value.Equals(another_bool_value));
    EXPECT_FALSE(bool_value == another_bool_value);
    EXPECT_TRUE(bool_value != another_bool_value);
  }

  SimpleValue invalid_value;
  EXPECT_FALSE(bool_value.Equals(invalid_value));

  SimpleValue string_value = SimpleValue::String("abcdefg");
  EXPECT_FALSE(bool_value.Equals(string_value));

  SimpleValueProto proto;
  GOOGLESQL_ASSERT_OK(bool_value.Serialize(&proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(SimpleValue deserialized_bool_value,
                       SimpleValue::Deserialize(proto));
  EXPECT_TRUE(bool_value.Equals(deserialized_bool_value));
}

TEST_F(SimpleValueTest, SimpleValueTypeDouble) {
  SimpleValue double_value = SimpleValue::Double(1.23);
  EXPECT_TRUE(double_value.IsValid());
  EXPECT_TRUE(double_value.has_double_value());
  EXPECT_FALSE(double_value.has_int64_value());
  EXPECT_EQ(double_value.double_value(), 1.23);

  {
    SimpleValue another_double_value = SimpleValue::Double(1.23);
    EXPECT_TRUE(double_value.Equals(another_double_value));
    EXPECT_TRUE(double_value == another_double_value);
    EXPECT_FALSE(double_value != another_double_value);
  }

  {
    SimpleValue another_double_value = SimpleValue::Double(1.234);
    EXPECT_FALSE(double_value.Equals(another_double_value));
    EXPECT_FALSE(double_value == another_double_value);
    EXPECT_TRUE(double_value != another_double_value);
  }

  SimpleValue invalid_value;
  EXPECT_FALSE(double_value.Equals(invalid_value));

  SimpleValue int64_value = SimpleValue::Int64(1);
  EXPECT_FALSE(double_value.Equals(int64_value));

  SimpleValueProto proto;
  GOOGLESQL_ASSERT_OK(double_value.Serialize(&proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(SimpleValue deserialized_double_value,
                       SimpleValue::Deserialize(proto));
  EXPECT_TRUE(double_value.Equals(deserialized_double_value));
}

TEST_F(SimpleValueTest, SimpleValueTypeBytes) {
  SimpleValue bytes_value = SimpleValue::Bytes("abcdefg");
  EXPECT_TRUE(bytes_value.IsValid());
  EXPECT_TRUE(bytes_value.has_bytes_value());
  EXPECT_FALSE(bytes_value.has_string_value());
  EXPECT_EQ(bytes_value.bytes_value(), "abcdefg");

  {
    SimpleValue another_bytes_value = SimpleValue::Bytes("abcdefg");
    EXPECT_TRUE(bytes_value.Equals(another_bytes_value));
    EXPECT_TRUE(bytes_value == another_bytes_value);
    EXPECT_FALSE(bytes_value != another_bytes_value);
  }

  {
    SimpleValue another_bytes_value = SimpleValue::Bytes("1234567");
    EXPECT_FALSE(bytes_value.Equals(another_bytes_value));
    EXPECT_FALSE(bytes_value == another_bytes_value);
    EXPECT_TRUE(bytes_value != another_bytes_value);
  }

  SimpleValue invalid_value;
  EXPECT_FALSE(bytes_value.Equals(invalid_value));

  SimpleValue string_value = SimpleValue::String("abcdefg");
  EXPECT_FALSE(bytes_value.Equals(string_value));

  SimpleValueProto proto;
  GOOGLESQL_ASSERT_OK(bytes_value.Serialize(&proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(SimpleValue deserialized_bytes_value,
                       SimpleValue::Deserialize(proto));
  EXPECT_TRUE(bytes_value.Equals(deserialized_bytes_value));
}

TEST_F(SimpleValueTest, SimpleValueTypeStringRefCount) {
  // Test copy assignment operator.
  {
    SimpleValue string_value = SimpleValue::String("abcdefg");
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
    {
      SimpleValue another_string_value = SimpleValue::String("1234567");
      string_value = another_string_value;
      // After the copy, the ref count is no longer one.
      EXPECT_FALSE(StringPtrRefCountIsOne(string_value));
      EXPECT_FALSE(StringPtrRefCountIsOne(another_string_value));
    }
    // another_string_value is out of scope, ref count is back to one.
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
  }

  // Test move assignment operator.
  {
    SimpleValue string_value = SimpleValue::String("abcdefg");
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
    {
      SimpleValue another_string_value = SimpleValue::String("1234567");
      string_value = std::move(another_string_value);
      // After std::move, another_string_value becomes invalid and string_value
      // has ref count 1.
      EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
      // NOLINTNEXTLINE - suppress clang-tidy warning on use-after-move.
      EXPECT_FALSE(another_string_value.IsValid());
    }
    // another_string_value is out of scope, ref count is back to one.
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
  }

  // Test copy-constructor.
  {
    SimpleValue string_value = SimpleValue::String("abcdefg");
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
    {
      SimpleValue another_string_value(string_value);
      // After the copy, the ref count is no longer one.
      EXPECT_FALSE(StringPtrRefCountIsOne(string_value));
      EXPECT_FALSE(StringPtrRefCountIsOne(another_string_value));
    }
    // another_string_value is out of scope, ref count is back to one.
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
  }

  // Test move-construtor.
  {
    SimpleValue string_value = SimpleValue::String("abcdefg");
    EXPECT_TRUE(StringPtrRefCountIsOne(string_value));
    SimpleValue another_string_value(std::move(string_value));
    // After std::move, string_value becomes invalid and another_string_value
    // has ref count 1.
    EXPECT_TRUE(StringPtrRefCountIsOne(another_string_value));
    // NOLINTNEXTLINE - suppress clang-tidy warning on use-after-move.
    EXPECT_FALSE(string_value.IsValid());
  }
}

}  // namespace googlesql
