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

#include "googlesql/public/json_value.h"

#include <stddef.h>
#include <string.h>

#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/depth_limit_detector_test_cases.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "absl/types/compare.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace {

using ::googlesql::IsValidJSON;
using ::googlesql::JSONParsingOptions;
using ::googlesql::JSONValue;
using ::googlesql::JSONValueConstRef;
using ::googlesql::JSONValueRef;
using ::googlesql::kJSONMaxArraySize;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using RemoveEmptyOptions = ::JSONValueRef::RemoveEmptyOptions;
using WideNumberMode = ::googlesql::JSONParsingOptions::WideNumberMode;

constexpr char kJSONStr[] = R"(
  {
    "pi": 3.141,
    "happy": true,
    "name": "Niels",
    "nothing": null,
    "answer": {
      "everything": 42
    },
    "list": [1, 0, 2],
    "object": {
      "currency": "USD",
      "value": 42.99
    }
  }
)";

TEST(JSONValueTest, NullValue) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  EXPECT_TRUE(ref.IsNull());
  EXPECT_FALSE(ref.IsObject());
  EXPECT_FALSE(ref.IsBoolean());
  EXPECT_FALSE(ref.IsArray());
  EXPECT_FALSE(ref.IsString());
  EXPECT_FALSE(ref.IsNumber());

  JSONValueConstRef const_ref = value.GetConstRef();

  EXPECT_TRUE(const_ref.IsNull());
  EXPECT_FALSE(const_ref.IsObject());
  EXPECT_FALSE(const_ref.IsBoolean());
  EXPECT_FALSE(const_ref.IsArray());
  EXPECT_FALSE(const_ref.IsString());
  EXPECT_FALSE(const_ref.IsNumber());

  EXPECT_DEATH(ref.GetInt64(), "type must be number, but is null");
}

TEST(JSONValueTest, Int64Value) {
  constexpr int64_t kInt64Value = 1;
  {
    JSONValue value(kInt64Value);
    ASSERT_TRUE(value.GetConstRef().IsInt64());
    EXPECT_EQ(kInt64Value, value.GetConstRef().GetInt64());
  }

  {
    JSONValue value;
    JSONValueRef ref = value.GetRef();

    ref.SetInt64(kInt64Value);

    EXPECT_FALSE(ref.IsNull());
    EXPECT_FALSE(ref.IsObject());
    EXPECT_FALSE(ref.IsBoolean());
    EXPECT_FALSE(ref.IsArray());
    EXPECT_FALSE(ref.IsString());
    ASSERT_TRUE(ref.IsNumber());
    ASSERT_TRUE(ref.IsInt64());

    EXPECT_EQ(kInt64Value, ref.GetInt64());
    EXPECT_DEATH(ref.GetString(), "type must be string, but is number");
  }
}

TEST(JSONValueTest, UInt64Value) {
  constexpr uint64_t kUInt64Value = 1;

  {
    JSONValue value(kUInt64Value);
    ASSERT_TRUE(value.GetConstRef().IsUInt64());
    EXPECT_EQ(kUInt64Value, value.GetConstRef().GetUInt64());
  }

  {
    JSONValue value;
    JSONValueRef ref = value.GetRef();

    ref.SetUInt64(kUInt64Value);

    EXPECT_FALSE(ref.IsNull());
    EXPECT_FALSE(ref.IsObject());
    EXPECT_FALSE(ref.IsBoolean());
    EXPECT_FALSE(ref.IsArray());
    EXPECT_FALSE(ref.IsString());
    ASSERT_TRUE(ref.IsNumber());
    ASSERT_TRUE(ref.IsUInt64());

    EXPECT_EQ(kUInt64Value, ref.GetUInt64());
    EXPECT_DEATH(ref.GetString(), "type must be string, but is number");
  }
}

TEST(JSONValueTest, DoubleValue) {
  constexpr double kDoubleValue = 1;

  {
    JSONValue value(kDoubleValue);
    ASSERT_TRUE(value.GetConstRef().IsDouble());
    EXPECT_EQ(kDoubleValue, value.GetConstRef().GetDouble());
  }

  {
    JSONValue value;
    JSONValueRef ref = value.GetRef();

    ref.SetDouble(kDoubleValue);

    EXPECT_FALSE(ref.IsNull());
    EXPECT_FALSE(ref.IsObject());
    EXPECT_FALSE(ref.IsBoolean());
    EXPECT_FALSE(ref.IsArray());
    EXPECT_FALSE(ref.IsString());
    ASSERT_TRUE(ref.IsNumber());
    ASSERT_TRUE(ref.IsDouble());

    EXPECT_EQ(kDoubleValue, ref.GetDouble());
    EXPECT_DEATH(ref.GetString(), "type must be string, but is number");
  }
}

TEST(JSONValueTest, StringValue) {
  const std::string kStringValue = "test";

  {
    JSONValue value(kStringValue);
    ASSERT_TRUE(value.GetConstRef().IsString());
    EXPECT_EQ(kStringValue, value.GetConstRef().GetString());
    EXPECT_EQ(kStringValue, value.GetConstRef().GetStringRef());
  }

  {
    JSONValue value;
    JSONValueRef ref = value.GetRef();

    ref.SetString(kStringValue);

    EXPECT_FALSE(ref.IsNull());
    EXPECT_FALSE(ref.IsObject());
    EXPECT_FALSE(ref.IsBoolean());
    EXPECT_FALSE(ref.IsArray());
    ASSERT_TRUE(ref.IsString());
    EXPECT_FALSE(ref.IsNumber());

    EXPECT_EQ(kStringValue, ref.GetString());
    EXPECT_EQ(kStringValue, ref.GetStringRef());
    EXPECT_DEATH(ref.GetBoolean(), "type must be boolean, but is string");
  }
}

TEST(JSONValueTest, BooleanValue) {
  constexpr bool kBooleanValue = true;

  {
    JSONValue value(kBooleanValue);
    ASSERT_TRUE(value.GetConstRef().IsBoolean());
    EXPECT_EQ(kBooleanValue, value.GetConstRef().GetBoolean());
  }

  {
    JSONValue value;
    JSONValueRef ref = value.GetRef();

    ref.SetBoolean(kBooleanValue);

    EXPECT_FALSE(ref.IsNull());
    EXPECT_FALSE(ref.IsObject());
    ASSERT_TRUE(ref.IsBoolean());
    EXPECT_FALSE(ref.IsArray());
    EXPECT_FALSE(ref.IsString());
    EXPECT_FALSE(ref.IsNumber());

    EXPECT_EQ(kBooleanValue, ref.GetBoolean());
    EXPECT_DEATH(ref.GetString(), "type must be string, but is boolean");
  }
}

TEST(JSONValueTest, UpdateToDifferentPrimitiveTypes) {
  constexpr bool kBooleanValue = true;
  const std::string kStringValue = "test";
  constexpr int64_t kInt64Value = 1;

  JSONValue value;
  JSONValueRef ref = value.GetRef();

  ref.SetBoolean(kBooleanValue);
  ASSERT_TRUE(ref.IsBoolean());
  EXPECT_EQ(kBooleanValue, ref.GetBoolean());

  ref.SetString(kStringValue);
  ASSERT_TRUE(ref.IsString());
  EXPECT_EQ(kStringValue, ref.GetString());

  ref.SetInt64(kInt64Value);
  ASSERT_TRUE(ref.IsInt64());
  EXPECT_EQ(kInt64Value, ref.GetInt64());
}

TEST(JSONValueTest, UpdatePrimitiveTypeToObjectType) {
  constexpr bool kBooleanValue = true;

  JSONValue value;
  JSONValueRef ref = value.GetRef();

  ref.SetBoolean(kBooleanValue);
  EXPECT_DEATH(ref.GetMember("key"), "with a string argument with boolean");
}

TEST(JSONValueTest, UpdatePrimitiveTypeToArrayType) {
  constexpr bool kBooleanValue = true;

  JSONValue value;
  JSONValueRef ref = value.GetRef();

  ref.SetBoolean(kBooleanValue);
  EXPECT_DEATH(ref.GetArrayElement(0), "with a numeric argument with boolean");
}

TEST(JSONValueTest, ObjectValue) {
  constexpr int64_t kIntegerValue = 1;
  constexpr char kKey[] = "key";
  constexpr char kWrongKey[] = "wrong key";
  JSONValue value;

  JSONValueRef ref = value.GetRef();
  JSONValueRef member_ref = ref.GetMember(kKey);

  EXPECT_FALSE(ref.IsNull());
  ASSERT_TRUE(ref.IsObject());
  EXPECT_FALSE(ref.IsBoolean());
  EXPECT_FALSE(ref.IsArray());
  EXPECT_FALSE(ref.IsString());
  EXPECT_FALSE(ref.IsNumber());

  EXPECT_EQ(ref.GetObjectSize(), 1);
  EXPECT_TRUE(ref.HasMember(kKey));
  EXPECT_FALSE(ref.HasMember(kWrongKey));

  EXPECT_TRUE(member_ref.IsNull());

  member_ref.SetInt64(kIntegerValue);

  EXPECT_FALSE(ref.GetMember(kKey).IsNull());
  EXPECT_EQ(kIntegerValue, ref.GetMember(kKey).GetInt64());

  std::vector<std::pair<absl::string_view, JSONValueRef>> members =
      ref.GetMembers();

  EXPECT_EQ(1, members.size());
  EXPECT_EQ(kKey, members[0].first);
  EXPECT_EQ(kIntegerValue, members[0].second.GetInt64());

  std::optional<JSONValueConstRef> optional_member_const_ref =
      ref.GetMemberIfExists(kKey);
  ASSERT_TRUE(optional_member_const_ref.has_value());
  EXPECT_TRUE(optional_member_const_ref->IsInt64());
  EXPECT_EQ(1, optional_member_const_ref->GetInt64());

  EXPECT_FALSE(ref.GetMemberIfExists(kWrongKey).has_value());
}

TEST(JSONValueTest, EmptyObjectValue) {
  constexpr int64_t kIntegerValue = 1;
  constexpr char kKey[] = "key";
  constexpr char kWrongKey[] = "wrong key";
  JSONValue value;

  JSONValueRef ref = value.GetRef();
  ref.SetToEmptyObject();

  EXPECT_FALSE(ref.IsNull());
  ASSERT_TRUE(ref.IsObject());
  EXPECT_FALSE(ref.IsBoolean());
  EXPECT_FALSE(ref.IsArray());
  EXPECT_FALSE(ref.IsString());
  EXPECT_FALSE(ref.IsNumber());

  EXPECT_EQ(ref.GetObjectSize(), 0);
  EXPECT_THAT(ref.GetMembers(), IsEmpty());

  JSONValueRef member_ref = ref.GetMember(kKey);

  EXPECT_EQ(ref.GetObjectSize(), 1);
  EXPECT_TRUE(ref.HasMember(kKey));
  EXPECT_FALSE(ref.HasMember(kWrongKey));
  EXPECT_TRUE(member_ref.IsNull());

  member_ref.SetInt64(kIntegerValue);

  EXPECT_FALSE(ref.GetMember(kKey).IsNull());
  EXPECT_EQ(ref.GetMember(kKey).GetInt64(), kIntegerValue);

  std::vector<std::pair<absl::string_view, JSONValueRef>> members =
      ref.GetMembers();

  ASSERT_EQ(members.size(), 1);
  EXPECT_EQ(members[0].first, kKey);
  EXPECT_EQ(members[0].second.GetInt64(), kIntegerValue);

  std::optional<JSONValueConstRef> optional_member_const_ref =
      ref.GetMemberIfExists(kKey);
  ASSERT_TRUE(optional_member_const_ref.has_value());
  ASSERT_TRUE(optional_member_const_ref->IsInt64());
  EXPECT_EQ(optional_member_const_ref->GetInt64(), 1);

  EXPECT_FALSE(ref.GetMemberIfExists(kWrongKey).has_value());
}

TEST(JSONValueTest, ArrayValue) {
  constexpr int64_t kIntegerValue = 1;
  constexpr size_t kIndex = 5;
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  JSONValueRef element_ref = ref.GetArrayElement(kIndex);

  EXPECT_FALSE(ref.IsNull());
  EXPECT_FALSE(ref.IsObject());
  EXPECT_FALSE(ref.IsBoolean());
  ASSERT_TRUE(ref.IsArray());
  EXPECT_FALSE(ref.IsString());
  EXPECT_FALSE(ref.IsNumber());

  EXPECT_EQ(kIndex + 1, ref.GetArraySize());

  EXPECT_TRUE(element_ref.IsNull());

  element_ref.SetInt64(kIntegerValue);

  EXPECT_FALSE(ref.GetArrayElement(kIndex).IsNull());
  EXPECT_EQ(kIntegerValue, ref.GetArrayElement(kIndex).GetInt64());

  std::vector<JSONValueRef> elements = ref.GetArrayElements();
  EXPECT_EQ(kIndex + 1, elements.size());
  EXPECT_TRUE(elements[0].IsNull());
  EXPECT_EQ(kIntegerValue, elements[kIndex].GetInt64());
}

TEST(JSONValueTest, EmptyArrayValue) {
  constexpr int64_t kIntegerValue = 1;
  constexpr size_t kIndex = 5;
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetToEmptyArray();

  EXPECT_FALSE(ref.IsNull());
  EXPECT_FALSE(ref.IsObject());
  EXPECT_FALSE(ref.IsBoolean());
  ASSERT_TRUE(ref.IsArray());
  EXPECT_FALSE(ref.IsString());
  EXPECT_FALSE(ref.IsNumber());

  EXPECT_EQ(ref.GetArraySize(), 0);

  JSONValueRef element_ref = ref.GetArrayElement(kIndex);

  EXPECT_EQ(ref.GetArraySize(), kIndex + 1);
  EXPECT_TRUE(element_ref.IsNull());
  for (JSONValueConstRef element_ref : ref.GetArrayElements()) {
    EXPECT_TRUE(element_ref.IsNull());
  }

  element_ref.SetInt64(kIntegerValue);
  EXPECT_FALSE(ref.GetArrayElement(kIndex).IsNull());
  EXPECT_EQ(ref.GetArrayElement(kIndex).GetInt64(), kIntegerValue);

  std::vector<JSONValueRef> elements = ref.GetArrayElements();
  EXPECT_EQ(elements.size(), kIndex + 1);
  EXPECT_TRUE(elements[0].IsNull());
  EXPECT_EQ(elements[kIndex].GetInt64(), kIntegerValue);
}

TEST(JSONValueTest, CopyFrom) {
  constexpr int64_t kInt64Value1 = 1;
  constexpr int64_t kInt64Value2 = 5;

  JSONValue value(kInt64Value1);
  JSONValueRef ref = value.GetRef();

  JSONValue copy = JSONValue::CopyFrom(ref);
  JSONValueConstRef copy_ref = copy.GetConstRef();

  ASSERT_TRUE(copy_ref.IsInt64());
  EXPECT_EQ(kInt64Value1, copy_ref.GetInt64());
  EXPECT_TRUE(ref.NormalizedEquals(copy_ref));

  ref.SetInt64(kInt64Value2);

  ASSERT_TRUE(copy_ref.IsInt64());
  EXPECT_EQ(kInt64Value1, copy_ref.GetInt64());
  EXPECT_FALSE(ref.NormalizedEquals(copy_ref));
}

TEST(JSONValueTest, DISABLED_MoveFrom) {
  constexpr absl::string_view kInitialValue =
      R"({"a":{"b":{"c":1}}, "d":2, "e":[3, 4, [5, 6]]})";
  using TokenValue = std::variant<std::string, int64_t>;
  auto verify_func = [&](absl::Span<const TokenValue> path_tokens,
                         absl::string_view member_json_string,
                         absl::string_view modified_original_json_string) {
    JSONValue original_value =
        JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef member_ref_to_move = original_value.GetRef();
    // Fetch JSONValueRef we want to move.
    for (auto path_token : path_tokens) {
      if (std::holds_alternative<std::string>(path_token)) {
        member_ref_to_move =
            member_ref_to_move.GetMember(std::get<std::string>(path_token));
      } else {
        member_ref_to_move =
            member_ref_to_move.GetArrayElement(std::get<int64_t>(path_token));
      }
    }

    JSONValue member_value =
        JSONValue::ParseJSONString(member_json_string).value();
    EXPECT_TRUE(
        member_ref_to_move.NormalizedEquals(member_value.GetConstRef()));
    JSONValue moved_value = JSONValue::MoveFrom(member_ref_to_move);
    EXPECT_TRUE(
        moved_value.GetConstRef().NormalizedEquals(member_value.GetConstRef()));
    EXPECT_TRUE(member_ref_to_move.IsNull());
    EXPECT_TRUE(original_value.GetConstRef().NormalizedEquals(
        JSONValue::ParseJSONString(modified_original_json_string)
            ->GetConstRef()));
  };

  verify_func({}, kInitialValue, "null");
  verify_func({"a"}, R"({"b":{"c":1}})",
              R"({"a":null, "d":2, "e":[3, 4, [5, 6]]})");
  verify_func({"a", "b"}, R"({"c":1})",
              R"({"a":{"b":null}, "d":2, "e":[3, 4, [5, 6]]})");
  verify_func({"a", "b", "c"}, "1",
              R"({"a":{"b":{"c":null}}, "d":2, "e":[3, 4, [5, 6]]})");
  verify_func({"d"}, "2",
              R"({"a":{"b":{"c":1}}, "d":null, "e":[3, 4, [5, 6]]})");
  verify_func({"e"}, R"([3, 4, [5, 6]])",
              R"({"a":{"b":{"c":1}}, "d":2, "e":null})");
  verify_func({"e", 0}, "3",
              R"({"a":{"b":{"c":1}}, "d":2, "e":[null, 4, [5, 6]]})");
  verify_func({"e", 2}, "[5, 6]",
              R"({"a":{"b":{"c":1}}, "d":2, "e":[3, 4, null]})");
  verify_func({"e", 2, 1}, "6",
              R"({"a":{"b":{"c":1}}, "d":2, "e":[3, 4, [5, null]]})");
}

class JSONParserTest : public ::testing::TestWithParam<JSONParsingOptions> {};

TEST_P(JSONParserTest, ParseString) {
  JSONValue value = JSONValue::ParseJSONString("\"str\"", GetParam()).value();
  ASSERT_TRUE(value.GetConstRef().IsString());
  EXPECT_EQ("str", value.GetConstRef().GetString());
}

TEST_P(JSONParserTest, ParseInteger) {
  JSONValue value = JSONValue::ParseJSONString("1", GetParam()).value();
  EXPECT_TRUE(value.GetConstRef().IsInt64());
  ASSERT_TRUE(value.GetConstRef().IsUInt64());
  EXPECT_EQ(1, value.GetConstRef().GetInt64());
  EXPECT_EQ(1, value.GetConstRef().GetUInt64());

  value = JSONValue::ParseJSONString("-1", GetParam()).value();
  ASSERT_TRUE(value.GetConstRef().IsInt64());
  ASSERT_FALSE(value.GetConstRef().IsUInt64());
  EXPECT_EQ(-1, value.GetConstRef().GetInt64());

  uint64_t uint64_value =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
  value = JSONValue::ParseJSONString(std::to_string(uint64_value), GetParam())
              .value();
  EXPECT_FALSE(value.GetConstRef().IsInt64());
  ASSERT_TRUE(value.GetConstRef().IsUInt64());
  EXPECT_EQ(uint64_value, value.GetConstRef().GetUInt64());
}

TEST_P(JSONParserTest, ParseDouble) {
  JSONValue value = JSONValue::ParseJSONString("1.5", GetParam()).value();
  ASSERT_TRUE(value.GetConstRef().IsDouble());
  EXPECT_EQ(1.5, value.GetConstRef().GetDouble());
}

TEST_P(JSONParserTest, ParseLargeNumbers) {
  JSONValue value =
      JSONValue::ParseJSONString("11111111111111111111", GetParam()).value();
  EXPECT_FALSE(value.GetConstRef().IsInt64());
  ASSERT_TRUE(value.GetConstRef().IsUInt64());
  EXPECT_EQ(11111111111111111111ULL, value.GetConstRef().GetUInt64());

  switch (GetParam().wide_number_mode) {
    case WideNumberMode::kRound:
      value = JSONValue::ParseJSONString("123456789012345678901234567890",
                                         GetParam())
                  .value();
      EXPECT_FALSE(value.GetConstRef().IsInt64());
      ASSERT_TRUE(value.GetConstRef().IsDouble());
      EXPECT_THAT(value.GetConstRef().GetDouble(),
                  testing::DoubleEq(1.23456789012345678901234567890e+29));
      break;
    case WideNumberMode::kExact:
      EXPECT_THAT(JSONValue::ParseJSONString("123456789012345678901234567890",
                                             GetParam())
                      .status()
                      .message(),
                  ::testing::HasSubstr("cannot round-trip"));
      break;
  }

  auto result = JSONValue::ParseJSONString("3.14e314", GetParam());
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              ::testing::HasSubstr("number overflow parsing '3.14e314'"));
}

TEST_P(JSONParserTest, ParseBoolean) {
  JSONValue value = JSONValue::ParseJSONString("true", GetParam()).value();
  ASSERT_TRUE(value.GetConstRef().IsBoolean());
  EXPECT_TRUE(value.GetConstRef().GetBoolean());
}

TEST_P(JSONParserTest, ParseNull) {
  JSONValue value = JSONValue::ParseJSONString("null", GetParam()).value();
  ASSERT_TRUE(value.GetConstRef().IsNull());
}

TEST_P(JSONParserTest, ParseArray) {
  JSONValue value =
      JSONValue::ParseJSONString("[1, \n2, \t3]", GetParam()).value();
  ASSERT_TRUE(value.GetConstRef().IsArray());
  EXPECT_EQ(3, value.GetConstRef().GetArraySize());
}

TEST_P(JSONParserTest, ParseObject) {
  JSONValue value = JSONValue::ParseJSONString(kJSONStr, GetParam()).value();
  EXPECT_EQ(value.GetConstRef().GetObjectSize(), 7);
  ASSERT_TRUE(value.GetConstRef().IsObject());
  ASSERT_TRUE(value.GetConstRef().GetMember("pi").IsDouble());
  EXPECT_EQ(3.141, value.GetConstRef().GetMember("pi").GetDouble());
  ASSERT_TRUE(value.GetConstRef().GetMember("happy").IsBoolean());
  EXPECT_TRUE(value.GetConstRef().GetMember("happy").GetBoolean());
  ASSERT_TRUE(value.GetConstRef().GetMember("name").IsString());
  EXPECT_EQ("Niels", value.GetConstRef().GetMember("name").GetString());
  ASSERT_TRUE(value.GetConstRef().GetMember("nothing").IsNull());
  ASSERT_TRUE(value.GetConstRef().GetMember("list").IsArray());
  EXPECT_EQ(3, value.GetConstRef().GetMember("list").GetArraySize());
  ASSERT_TRUE(value.GetConstRef().GetMember("object").IsObject());
}

TEST_P(JSONParserTest, ParseDuplicateKeys) {
  absl::flat_hash_map<std::string, std::string> input_to_expected_output;
  input_to_expected_output.emplace(R"(
    {
      "a":{"a":1},
      "a":{"b":2}
    }
  )",
                                   R"({"a":{"a":1}})");
  input_to_expected_output.emplace(R"(
    {
      "f":1,
      "f":{"a":1, "b":[1, 2, 3]},
      "g":1,
      "f":[{"d":{"a":1}}]
    }
  )",
                                   R"({"f":1,"g":1})");
  input_to_expected_output.emplace(
      R"(
    {
      "a":[{"a":1,"b":3,"a":4,"b":1}],
      "a":{"a":1, "b":[1, 2, 3]},
      "b":1,
      "f":[{"d":{"a":1}}],
      "f":[1,2,3]
    }
  )",
      R"({"a":[{"a":1,"b":3}],"b":1,"f":[{"d":{"a":1}}]})");
  input_to_expected_output.emplace(R"(
    {
      "f":1,
      "f":{"a":1, "b":[1, {"a":1, "b":[2], "a":{"a":1}}]},
      "g":1,
      "f":[{"a":{"a":1, "a":[{"a":1}]}}, 2]
    }
  )",
                                   R"({"f":1,"g":1})");
  input_to_expected_output.emplace(
      R"(
    {
      "a":
      [{
        "a":1,
        "a":2,
        "c":
        {
          "a":1,
          "b":1,
          "a":3,
          "c":["b","d","e"],
          "b":{}
        }
      }],
      "b":
      {
        "a":1,
        "b":[1, 2, 3],
        "c":[],
        "c":[{"a":1,"b":2}],
        "a":{}
      },
      "c":
      [
        {"a":1},
        {"a":1,"b":2,"a":2,"b":[{"c":1,"c":[2,3,4]}]},
        {"c":{}}
      ],
      "b":
      [
        {"d":{"a":1}}
      ],
      "a":1
    }
  )",
      absl::StrCat(
          R"({"a":[{"a":1,"c":{"a":1,"b":1,"c":["b","d","e"]}}],)",
          R"("b":{"a":1,"b":[1,2,3],"c":[]},"c":[{"a":1},{"a":1,"b":2},)",
          R"({"c":{}}]})"));
  input_to_expected_output.emplace(
      R"(
    {
      "a":1,
      "b":
      [
        {"d":{"a":1}}
      ],
      "b":
      {
        "a":1,
        "b":[1, 2, 3],
        "c":[],
        "c":[{"a":1,"b":2}],
        "a":{}
      },
      "a":[
        {},
        {"a":1,"b":5},
        {"a":[1,2,3,4,5],"b":{"a":2}},
        {"a":1}
      ],
      "b":
      {
        "b":{"b":2,"b":["b","b","b"],"c":{"b":2}},
        "a":{"b":2,"b":"d","a":["b","b","b"]},
        "b":{"b":[{},{"b":["b"],"b":{"b":{"b":2,"b":[1,2]},"b":{"b":5}}}]}
      },
      "c":
      [
        {"a":1},
        {"a":1,"b":2,"a":2,"b":[{"c":1,"c":[2,3,4]}]},
        {"c":{}}
      ],
      "a":
      [{
        "a":1,
        "a":2,
        "c":
        {
          "a":1,
          "b":1,
          "a":3,
          "c":["b","d","e"],
          "b":{}
        }
      }]
    }
  )",
      absl::StrCat(R"({"a":1,"b":[{"d":{"a":1}}],)",
                   R"("c":[{"a":1},{"a":1,"b":2},)", R"({"c":{}}]})"));
  input_to_expected_output.emplace(
      R"(
    {
      "a":0,
      "a":1,
      "b":2,
      "c":3,
      "d":
      {
        "a":
        {
          "a":[{"a":4,"a":5}],
          "a":6
        },
        "b":7,
        "a":8,
        "b":9,
        "c":10,
        "b":11
      },
      "a":12,
      "b":13,
      "a":14,
      "c":15,
      "e":16,
      "f":[{"a":17},{"b":18,"a":19,"b":20},{"c":21,"d":22,"b":23,"c":24}],
      "f":25,
      "g":[{"a":17},{"b":18,"a":19,"b":20},{"c":21,"d":22,"b":23,"c":24}]
    }
  )",
      absl::StrCat(R"({"a":0,"b":2,"c":3,"d":{"a":{"a":[{"a":4}]},"b":7,)",
                   R"("c":10},"e":16,"f":[{"a":17},{"a":19,"b":18},{"b":)",
                   R"(23,"c":21,"d":22}],"g":[{"a":17},{"a":19,"b":18},)",
                   R"({"b":23,"c":21,"d":22}]})"));

  for (const auto& pair : input_to_expected_output) {
    JSONValue json = JSONValue::ParseJSONString(pair.first, GetParam()).value();
    EXPECT_EQ(json.GetConstRef().ToString(), pair.second);
  }
}

TEST_P(JSONParserTest, ParseComplexStrings) {
  googlesql::DepthLimitDetectorRuntimeControl control;
  control.max_probing_duration = absl::Seconds(2);

  for (auto const& test_case : googlesql::JSONDepthLimitDetectorTestCases()) {
    googlesql::DepthLimitDetectorTestResult result =
        googlesql::RunDepthLimitDetectorTestCase(
            test_case,
            [](absl::string_view json) -> absl::Status {
              GOOGLESQL_ASSIGN_OR_RETURN(JSONValue val,
                               JSONValue::ParseJSONString(json, GetParam()));

              // If the first deserialization had enough stack, we assert we can
              // format and serialize the value repeatedly.
              std::string serialized = val.GetConstRef().ToString();
              JSONValue from_serialized =
                  JSONValue::ParseJSONString(serialized, GetParam()).value();
              EXPECT_EQ(from_serialized.GetConstRef().ToString(), serialized);

              std::string formatted = val.GetConstRef().Format();
              JSONValue from_formatted =
                  JSONValue::ParseJSONString(formatted, GetParam()).value();
              EXPECT_EQ(from_formatted.GetConstRef().Format(), formatted);

              return absl::OkStatus();
            },
            control);
    EXPECT_EQ(result.depth_limit_detector_return_conditions[0].return_status,
              absl::OkStatus())
        << result;
  }
}

INSTANTIATE_TEST_SUITE_P(
    CommonJSONParserTests, JSONParserTest,
    ::testing::Values(
        JSONParsingOptions{.wide_number_mode = WideNumberMode::kRound},
        JSONParsingOptions{.wide_number_mode = WideNumberMode::kExact}));

TEST(JSONStrictNumberParsingTest, NumberParsingSuccess) {
  JSONParsingOptions options{.wide_number_mode = WideNumberMode::kExact};
  absl::flat_hash_map<absl::string_view, absl::string_view> test_cases;
  test_cases.try_emplace("1", "1");
  test_cases.try_emplace("1e0", "1.0");
  test_cases.try_emplace("1.35235e2", "135.235");
  test_cases.try_emplace("1.35235e-2", "0.0135235");
  test_cases.try_emplace("1.0035023500e2", "100.350235");
  test_cases.try_emplace("-1.003502000000000000000000000", "-1.003502");
  test_cases.try_emplace("0.0000000000000001", "1e-16");
  test_cases.try_emplace("-0.0000000000000000000000000000000000000000000001",
                         "-1e-46");
  test_cases.try_emplace("1e-300", "1e-300");
  test_cases.try_emplace("10000000000000000000000000000", "1e+28");
  test_cases.try_emplace("234567.12345678", "234567.12345678");
  test_cases.try_emplace("-234567.12345678", "-234567.12345678");
  test_cases.try_emplace("234567.12345678e15", "2.3456712345678e+20");
  test_cases.try_emplace("-234567.12345678e-25", "-2.3456712345678e-20");
  test_cases.try_emplace("17.9769313486231e307", "1.79769313486231e+308");
  test_cases.try_emplace("-17.9769313486231e307", "-1.79769313486231e+308");
  test_cases.try_emplace("17.9769313486231e-309", "1.79769313486231e-308");
  test_cases.try_emplace("-17.9769313486231e-309", "-1.79769313486231e-308");

  for (const auto& pair : test_cases) {
    JSONValue value = JSONValue::ParseJSONString(pair.first, options).value();
    EXPECT_EQ(value.GetConstRef().ToString(), pair.second);
  }
}

TEST(JSONStrictNumberParsingTest, NumberParsingFailure) {
  constexpr char overflow_err[] = "number overflow parsing";
  constexpr char failed_to_parse_err[] = "Failed to parse";
  constexpr char roundtrip_err[] = "cannot round-trip through string";
  JSONParsingOptions options{.wide_number_mode = WideNumberMode::kExact};
  absl::flat_hash_map<std::string, std::string> test_cases;
  // Number overflow failure test cases
  test_cases.try_emplace("1e1000", overflow_err);
  test_cases.try_emplace("1.79769313486232e308", overflow_err);
  test_cases.try_emplace("-1.79769313486232e308", overflow_err);
  // Parsing failure - input string too long
  test_cases.try_emplace(absl::StrCat("1.0", std::string(1500, '0'), "1"),
                         "is too long");
  // Parsing failure - too many significant fractional digits
  test_cases.try_emplace(absl::StrCat("1.0", std::string(1074, '0'), "1"),
                         failed_to_parse_err);
  // Round-tripping test failures
  test_cases.try_emplace("1523.3523546364323253e2", roundtrip_err);
  test_cases.try_emplace("-1523.3523546364323253e-2", roundtrip_err);
  test_cases.try_emplace("-1.003502000000000000000000001", roundtrip_err);
  test_cases.try_emplace("1e-500", roundtrip_err);
  for (const auto& pair : test_cases) {
    EXPECT_THAT(
        JSONValue::ParseJSONString(pair.first, options).status().message(),
        ::testing::HasSubstr(pair.second))
        << "Input: " << pair.first;
  }
}

TEST(JSONStandardParserTest, ParseErrorStandard) {
  auto result = JSONValue::ParseJSONString("[[[");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            "syntax error while parsing value - unexpected end of input; "
            "expected '[', '{', or a literal");

  result = JSONValue::ParseJSONString("t");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(
      result.status().message(),
      "syntax error while parsing value - invalid literal; last read: 't'");

  result = JSONValue::ParseJSONString("[1, a]");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(
      result.status().message(),
      "syntax error while parsing value - invalid literal; last read: '1, a'");

  result = JSONValue::ParseJSONString("{a: b}");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            "syntax error while parsing object key - invalid literal; last "
            "read: '{a'; expected string literal");

  result = JSONValue::ParseJSONString("+");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(
      result.status().message(),
      "syntax error while parsing value - invalid literal; last read: '+'");

  result = JSONValue::ParseJSONString("1e99999");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), "number overflow parsing '1e99999'");
}

TEST(JSONValueTest, SerializePrimitiveValueToString) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  constexpr char kStringValue[] = "value";
  ref.SetString(kStringValue);
  EXPECT_EQ(absl::Substitute("\"$0\"", kStringValue), ref.ToString());

  constexpr int64_t kIntValue = 1;
  ref.SetInt64(kIntValue);
  EXPECT_EQ(absl::Substitute("$0", kIntValue), ref.ToString());
}

TEST(JSONValueTest, SerializeObjectValueToString) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  constexpr char kIntValKey[] = "key_int";
  constexpr int64_t kIntValue = 1;
  constexpr char kStringValKey[] = "key_str";
  constexpr char kStringValue[] = "value";

  ref.GetMember(kIntValKey).SetInt64(kIntValue);
  ref.GetMember(kStringValKey).SetString(kStringValue);
  EXPECT_EQ(absl::Substitute("{\"$0\":$1,\"$2\":\"$3\"}", kIntValKey, kIntValue,
                             kStringValKey, kStringValue),
            ref.ToString());
}

TEST(JSONValueTest, SerializeArrayValueToString) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  constexpr bool kBooleanValue = true;
  constexpr uint64_t kIntValue = 1;
  ref.GetArrayElement(0).SetBoolean(kBooleanValue);
  ref.GetArrayElement(2).SetUInt64(kIntValue);

  EXPECT_EQ(absl::Substitute("[$0,null,$1]", kBooleanValue, kIntValue),
            ref.ToString());
}

TEST(JSONValueTest, Format) {
  // String value.
  EXPECT_EQ(
      JSONValue::ParseJSONString(R"("value")").value().GetConstRef().Format(),
      R"("value")");

  // Integer value.
  EXPECT_EQ(JSONValue::ParseJSONString("123").value().GetConstRef().Format(),
            "123");

  // Object value.
  EXPECT_EQ(
      JSONValue::ParseJSONString(R"({"key_int": 123, "key_str": "value"})")
          .value()
          .GetConstRef()
          .Format(),
      R"({
  "key_int": 123,
  "key_str": "value"
})");

  // Array value.
  EXPECT_EQ(JSONValue::ParseJSONString(R"([true, null, 123])")
                .value()
                .GetConstRef()
                .Format(),
            R"([
  true,
  null,
  123
])");

  // Nested value.
  EXPECT_EQ(JSONValue::ParseJSONString(R"({"key_arr": [true, null, 123], )"
                                       R"("key_int": 321, "key_str": "value"})")
                .value()
                .GetConstRef()
                .Format(),
            R"({
  "key_arr": [
    true,
    null,
    123
  ],
  "key_int": 321,
  "key_str": "value"
})");
}

TEST(JSONValueTest, ProtoBytesSerialization) {
  std::string test_json_strs[] = {"1", "null", "\"str\"", "[true, null, 5.0]",
                                  kJSONStr};

  for (const std::string& test_json_str : test_json_strs) {
    JSONValue value = JSONValue::ParseJSONString(test_json_str).value();
    std::string encoded_bytes;
    value.GetConstRef().SerializeAndAppendToProtoBytes(&encoded_bytes);
    JSONValue decoded_value =
        JSONValue::DeserializeFromProtoBytes(encoded_bytes).value();
    EXPECT_TRUE(
        decoded_value.GetConstRef().NormalizedEquals(value.GetConstRef()))
        << "Encoding test failed for '" << test_json_str << "'";
  }
}

TEST(JSONValueTest, DeserializeFromProtoBytesError) {
  auto result = JSONValue::DeserializeFromProtoBytes("invalid bytes");
  ASSERT_FALSE(result.status().ok());
  EXPECT_THAT(result.status().ToString(),
              testing::HasSubstr("syntax error while parsing UBJSON value"));
}

TEST(JSONValueTest, DeserializeFromProtoBytesMaxNestingLevel) {
  // Nesting level: 3
  JSONValue value =
      JSONValue::ParseJSONString(R"({"foo": [{"bar": 10}, 20]})").value();

  std::string encoded_bytes;
  value.GetConstRef().SerializeAndAppendToProtoBytes(&encoded_bytes);

  EXPECT_THAT(JSONValue::DeserializeFromProtoBytes(encoded_bytes,
                                                   /*max_nesting_level=*/2)
                  .status(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Max nesting of 2 has been exceeded while "
                                 "parsing JSON document")));

  EXPECT_THAT(JSONValue::DeserializeFromProtoBytes(encoded_bytes,
                                                   /*max_nesting_level=*/0)
                  .status(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Max nesting of 0 has been exceeded while "
                                 "parsing JSON document")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue decoded_value,
                       JSONValue::DeserializeFromProtoBytes(
                           encoded_bytes, /*max_nesting_level=*/3));
  EXPECT_TRUE(
      decoded_value.GetConstRef().NormalizedEquals(value.GetConstRef()));

  // Default value for 'max_nesting_level' is std::nullopt which means no limit
  // is enforced.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue decoded_value2,
                       JSONValue::DeserializeFromProtoBytes(encoded_bytes));
  EXPECT_TRUE(
      decoded_value2.GetConstRef().NormalizedEquals(value.GetConstRef()));
}

TEST(JSONValueTest, NestingLevelExceedsMaxTest) {
  std::vector<std::tuple<std::string, int, bool>> test_cases = {
      {"1", -1, false},
      {"1", 0, false},
      {"1", 1, false},
      {R"("abc")", 0, false},
      {R"("abc")", 1, false},
      {R"({})", -1, true},
      {R"({})", 0, true},
      {R"({})", 1, false},
      {R"([])", 0, true},
      {R"([])", 1, false},
      {R"([[]])", 0, true},
      {R"([[]])", 1, true},
      {R"([[]])", 2, false},
      {R"({"a":1})", 0, true},
      {R"({"a":1})", 1, false},
      {R"({"a":[1,2]})", 1, true},
      {R"({"a":[1,2]})", 2, false},
      {R"({"a":{"b":{"c":null}}})", 2, true},
      {R"({"a":{"b":{"c":null}}})", 3, false},
      {R"({"a":[{"1":1},{"2":2}],"b":[{"3":3},{"4":4}]})", 2, true},
      {R"({"a":[{"1":1},{"2":2}],"b":[{"3":3},{"4":4}]})", 3, false},
      {R"([{"a":1},{"a":2}])", 1, true},
      {R"([{"a":1},{"a":2}])", 2, false},
      {R"([{"a":1},{"a":2}])", 3, false},
  };
  for (const auto& [json, max_nesting, is_excess] : test_cases) {
    JSONValue value = JSONValue::ParseJSONString(json).value();
    EXPECT_EQ(value.GetConstRef().NestingLevelExceedsMax(max_nesting),
              is_excess);
  }
}

TEST(JSONValueTest, NormalizedEqualsNull) {
  constexpr int64_t kInt64Value = 1;
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  ref.SetInt64(kInt64Value);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsInt64) {
  constexpr int64_t kInt64Value = 1;
  constexpr int64_t kInt64Value2 = 5;
  constexpr uint64_t kUInt64Value = 1;
  constexpr double kDoubleValue = 1;

  JSONValue value(kInt64Value);
  JSONValueRef ref = value.GetRef();

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetInt64(kInt64Value2);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetUInt64(kUInt64Value);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.SetDouble(kDoubleValue);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.SetInt64(kInt64Value);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsUInt64) {
  constexpr uint64_t kUInt64Value = 1;
  constexpr uint64_t kUInt64Value2 = 5;
  constexpr int64_t kInt64Value = 1;
  constexpr double kDoubleValue = 1;

  JSONValue value(kUInt64Value);
  JSONValueRef ref = value.GetRef();

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetUInt64(kUInt64Value2);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetInt64(kInt64Value);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.SetDouble(kDoubleValue);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.SetUInt64(kUInt64Value);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsDouble) {
  constexpr double kDoubleValue = 1;
  constexpr double kDoubleValue2 = 5.1;
  constexpr int64_t kInt64Value = 1;
  constexpr uint64_t kUInt64Value = 1;

  JSONValue value(kDoubleValue);
  JSONValueRef ref = value.GetRef();

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetDouble(kDoubleValue2);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetInt64(kInt64Value);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.SetUInt64(kUInt64Value);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.SetDouble(kDoubleValue);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsString) {
  const std::string kStringValue = "test";
  const std::string kStringValue2 = "Test";

  JSONValue value(kStringValue);
  JSONValueRef ref = value.GetRef();

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetString(kStringValue2);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetString(kStringValue);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsBoolean) {
  constexpr bool kBooleanValue = true;
  const std::string kStringValue = "true";
  constexpr int64_t kInt64Value = 1;

  JSONValue value(kBooleanValue);
  JSONValueRef ref = value.GetRef();

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetString(kStringValue);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetInt64(kInt64Value);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.SetBoolean(kBooleanValue);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsObject) {
  constexpr int64_t kInt64Value = 1;
  constexpr int64_t kInt64Value2 = 3;
  const std::string kKey = "key";
  const std::string kKey2 = "key2";

  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.GetMember(kKey).SetInt64(kInt64Value);

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.GetMember(kKey).SetInt64(kInt64Value2);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  ref.GetMember(kKey2).SetInt64(kInt64Value2);
  other_ref.GetMember(kKey).SetInt64(kInt64Value);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.GetMember(kKey2).SetInt64(kInt64Value2);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, NormalizedEqualsArray) {
  const std::string kStringValue = "test";
  constexpr int64_t kInt64Value = 1;

  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.GetArrayElement(0).SetInt64(kInt64Value);

  JSONValue other;
  JSONValueRef other_ref = other.GetRef();

  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.GetArrayElement(1).SetInt64(kInt64Value);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  ref.GetArrayElement(1).SetString(kStringValue);
  other_ref.GetArrayElement(0).SetString(kStringValue);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));

  other_ref.GetArrayElement(0).SetInt64(kInt64Value);
  other_ref.GetArrayElement(1).SetString(kStringValue);
  EXPECT_TRUE(ref.NormalizedEquals(other_ref));

  other_ref.GetArrayElement(2);
  EXPECT_FALSE(ref.NormalizedEquals(other_ref));
}

TEST(JSONValueTest, GetMemberIfExistsOnNonObject) {
  {
    // JSON null
    JSONValue json;
    EXPECT_FALSE(json.GetRef().GetMemberIfExists("key").has_value());
  }

  {
    // Int64
    JSONValue json(int64_t{10});
    EXPECT_FALSE(json.GetRef().GetMemberIfExists("key").has_value());
  }
  {
    // Uint64
    JSONValue json(uint64_t{10});
    EXPECT_FALSE(json.GetRef().GetMemberIfExists("key").has_value());
  }
  {
    // Boolean
    JSONValue json(true);
    EXPECT_FALSE(json.GetRef().GetMemberIfExists("key").has_value());
  }
  {
    // String
    JSONValue json(std::string{"foo"});
    EXPECT_FALSE(json.GetRef().GetMemberIfExists("key").has_value());
  }
  {
    // Array
    JSONValue json;
    json.GetRef().SetToEmptyArray();
    EXPECT_FALSE(json.GetRef().GetMemberIfExists("key").has_value());
  }
}

TEST(JSONValueTest, GetMemberIfExistsOnObject) {
  JSONValue json =
      JSONValue::ParseJSONString(R"({"key": 10, "key2": [true]})").value();
  JSONValueRef ref = json.GetRef();

  {
    auto member = ref.GetMemberIfExists("key");
    ASSERT_TRUE(member.has_value());
    member->SetString("foo");
  }

  {
    auto member = ref.GetMemberIfExists("inexistent key");
    EXPECT_FALSE(member.has_value());
  }

  JSONValue expected =
      JSONValue::ParseJSONString(R"({"key": "foo", "key2": [true]})").value();
  EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
}

TEST(JSONValueTest, ParseWithNestingLimit) {
  JSONParsingOptions options{.wide_number_mode = WideNumberMode::kRound,
                             .max_nesting = std::nullopt};
  auto result = JSONValue::ParseJSONString("[10, 20]", options);
  ASSERT_TRUE(result.ok());
  EXPECT_THAT(result->GetConstRef().ToString(), "[10,20]");

  result = JSONValue::ParseJSONString(R"({"a": [10, 20]})", options);
  ASSERT_TRUE(result.ok());
  EXPECT_THAT(result->GetConstRef().ToString(), R"({"a":[10,20]})");

  options.max_nesting = -1;
  result = JSONValue::ParseJSONString("10", options);
  ASSERT_TRUE(result.ok());
  EXPECT_THAT(result->GetConstRef().ToString(), "10");

  result = JSONValue::ParseJSONString("[10, 20]", options);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(
          "Max nesting of 0 has been exceeded while parsing JSON document"));

  options.max_nesting = 0;
  result = JSONValue::ParseJSONString(R"({"a": 10})", options);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(
          "Max nesting of 0 has been exceeded while parsing JSON document"));

  options.max_nesting = 1;
  result = JSONValue::ParseJSONString("[10, 20]", options);
  ASSERT_TRUE(result.ok());
  EXPECT_THAT(result->GetConstRef().ToString(), "[10,20]");

  result = JSONValue::ParseJSONString(R"({"a": 10})", options);
  ASSERT_TRUE(result.ok());
  EXPECT_THAT(result->GetConstRef().ToString(), R"({"a":10})");

  result = JSONValue::ParseJSONString(R"({"a": [10, 20]})", options);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(
          "Max nesting of 1 has been exceeded while parsing JSON document"));

  options.max_nesting = 3;
  result = JSONValue::ParseJSONString(
      R"({"a": [{"b": "foo"}, {"b": "bar"}], "c": 10})", options);
  ASSERT_TRUE(result.ok());
  EXPECT_THAT(result->GetConstRef().ToString(),
              R"({"a":[{"b":"foo"},{"b":"bar"}],"c":10})");

  result = JSONValue::ParseJSONString(
      R"({"a": [{"b": "foo"}, {"b": {"c": 10}}], "d": 10})", options);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(
          "Max nesting of 3 has been exceeded while parsing JSON document"));
}

TEST(JSONValueTest, InsertArrayElementIntoNull) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  EXPECT_TRUE(ref.IsNull());

  EXPECT_THAT(ref.InsertArrayElement(JSONValue(int64_t{10}), 0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, InsertArrayElementIntoObject) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetToEmptyObject();
  ref.GetMember("key").SetInt64(10);

  EXPECT_TRUE(ref.IsObject());

  EXPECT_THAT(ref.InsertArrayElement(JSONValue(int64_t{10}), 0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, InsertArrayElementIntoPrimitiveTypes) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetString("hello");

  EXPECT_TRUE(ref.IsString());

  EXPECT_THAT(ref.InsertArrayElement(JSONValue(int64_t{10}), 0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, InsertArrayElement) {
  {
    // Empty array.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElement(JSONValue(int64_t{10}), 0));
    ASSERT_EQ(ref.GetArraySize(), 1);
    ASSERT_TRUE(ref.GetArrayElement(0).IsInt64());
    EXPECT_EQ(ref.GetArrayElement(0).GetInt64(), 10);
  }

  constexpr absl::string_view kInitialValue = "[1, \"foo\", null, {}]";
  {
    // Insert at the beginning of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElement(JSONValue(int64_t{10}), 0));
    EXPECT_EQ(ref.GetArraySize(), 5);
    JSONValue expected =
        JSONValue::ParseJSONString("[10, 1, \"foo\", null, {}]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert in the middle of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElement(JSONValue(int64_t{10}), 2));
    EXPECT_EQ(ref.GetArraySize(), 5);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", 10, null, {}]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert at the end of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElement(JSONValue(int64_t{10}), 4));
    EXPECT_EQ(ref.GetArraySize(), 5);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, 10]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert past the end of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElement(JSONValue(int64_t{10}), 6));
    EXPECT_EQ(ref.GetArraySize(), 7);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, null, null, 10]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }
}

TEST(JSONValueTest, InsertArrayElements) {
  {
    // Empty array.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 0));
    ASSERT_EQ(ref.GetArraySize(), 2);
    ASSERT_TRUE(ref.GetArrayElement(0).IsInt64());
    EXPECT_EQ(ref.GetArrayElement(0).GetInt64(), 10);
    ASSERT_TRUE(ref.GetArrayElement(1).IsString());
    EXPECT_EQ(ref.GetArrayElement(1).GetString(), "bar");
  }

  constexpr absl::string_view kInitialValue = "[1, \"foo\", null, {}]";
  {
    // Insert 0 element at existing index.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements({}, 0));
    EXPECT_EQ(ref.GetArraySize(), 4);
    EXPECT_TRUE(ref.NormalizedEquals(
        JSONValue::ParseJSONString(kInitialValue).value().GetConstRef()));
  }

  {
    // Insert 0 element past the end of the array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements({}, 4));
    EXPECT_EQ(ref.GetArraySize(), 5);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, null]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert 0 element past the end of the array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements({}, 6));
    EXPECT_EQ(ref.GetArraySize(), 7);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, null, null, null]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert at the beginning of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 0));
    EXPECT_EQ(ref.GetArraySize(), 6);
    JSONValue expected =
        JSONValue::ParseJSONString("[10, \"bar\", 1, \"foo\", null, {}]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert in the middle of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 2));
    EXPECT_EQ(ref.GetArraySize(), 6);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", 10, \"bar\", null, {}]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert at the end of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 4));
    EXPECT_EQ(ref.GetArraySize(), 6);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, 10, \"bar\"]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Insert past the end of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 5));
    EXPECT_EQ(ref.GetArraySize(), 7);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, null, 10, \"bar\"]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }
}

TEST(JSONValueTest, InsertArrayElementLargeIndex) {
  {
    // Auto-create and fills array up to kJSONMaxArraySize elements.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    GOOGLESQL_ASSERT_OK(
        ref.InsertArrayElement(JSONValue(int64_t{100}), kJSONMaxArraySize - 1));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize);
    ASSERT_TRUE(ref.GetArrayElement(kJSONMaxArraySize - 1).IsInt64());
    EXPECT_EQ(ref.GetArrayElement(kJSONMaxArraySize - 1).GetInt64(), 100);
  }

  {
    // Auto-create and fills array up to kJSONMaxArraySize + 1 elements. Fails.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    EXPECT_THAT(
        ref.InsertArrayElement(JSONValue(int64_t{100}), kJSONMaxArraySize),
        StatusIs(absl::StatusCode::kOutOfRange,
                 HasSubstr("Exceeded maximum array size")));
    EXPECT_EQ(ref.GetArraySize(), 0);
  }

  {
    // Array size will exceed kJSONMaxArraySize after insertion. Fails.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize - 1);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize);

    EXPECT_THAT(ref.InsertArrayElement(JSONValue(int64_t{100}), 5),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize);
  }
}

TEST(JSONValueTest, InsertArrayElementsLargeIndex) {
  {
    // Auto-create and fills array up to kJSONMaxArraySize elements.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert),
                                      kJSONMaxArraySize - 2));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize);
  }

  {
    // Auto-create and fills array up to kJSONMaxArraySize + 1 elements. Fails.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    EXPECT_THAT(ref.InsertArrayElements(std::move(values_to_insert),
                                        kJSONMaxArraySize - 1),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_EQ(ref.GetArraySize(), 0);
  }

  {
    // Auto-create and fills array up to kJSONMaxArraySize + 1 elements. Fails.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    std::vector<JSONValue> values_to_insert;

    EXPECT_THAT(
        ref.InsertArrayElements(std::move(values_to_insert), kJSONMaxArraySize),
        StatusIs(absl::StatusCode::kOutOfRange,
                 HasSubstr("Exceeded maximum array size")));
    EXPECT_EQ(ref.GetArraySize(), 0);
  }

  {
    // Array already at max size. Inserts 0 elements is fine.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize - 1);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize);

    std::vector<JSONValue> values_to_insert;

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 1));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize);
  }

  {
    // Array already > max size. Inserts 0 elements is fine.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize + 1);

    std::vector<JSONValue> values_to_insert;

    GOOGLESQL_ASSERT_OK(ref.InsertArrayElements(std::move(values_to_insert), 1));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize + 1);
  }

  {
    // Insertion will exceed max array size. Fails.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize - 2);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize - 1);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    EXPECT_THAT(ref.InsertArrayElements(std::move(values_to_insert), 1),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize - 1);
  }
}

TEST(JSONValueTest, AppendArrayElementIntoNull) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  EXPECT_TRUE(ref.IsNull());

  EXPECT_THAT(ref.AppendArrayElement(JSONValue(int64_t{10})),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, AppendArrayElementIntoObject) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetToEmptyObject();
  ref.GetMember("key").SetInt64(10);

  EXPECT_TRUE(ref.IsObject());

  EXPECT_THAT(ref.AppendArrayElement(JSONValue(int64_t{10})),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, AppendArrayElementIntoPrimitiveTypes) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetString("hello");

  EXPECT_TRUE(ref.IsString());

  EXPECT_THAT(ref.AppendArrayElement(JSONValue(int64_t{10})),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, AppendArrayElement) {
  {
    // Empty array.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    GOOGLESQL_ASSERT_OK(ref.AppendArrayElement(JSONValue(int64_t{10})));
    ASSERT_EQ(ref.GetArraySize(), 1);
    ASSERT_TRUE(ref.GetArrayElement(0).IsInt64());
    EXPECT_EQ(ref.GetArrayElement(0).GetInt64(), 10);
  }

  {
    // Non-empty array.
    constexpr absl::string_view kInitialValue = "[1, \"foo\", null, {}, true]";
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    ASSERT_EQ(ref.GetArraySize(), 5);

    GOOGLESQL_ASSERT_OK(ref.AppendArrayElement(JSONValue(int64_t{10})));
    EXPECT_EQ(ref.GetArraySize(), 6);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, true, 10]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }
}

TEST(JSONValueTest, AppendArrayElements) {
  {
    // Empty array.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.AppendArrayElements(std::move(values_to_insert)));
    ASSERT_EQ(ref.GetArraySize(), 2);
    ASSERT_TRUE(ref.GetArrayElement(0).IsInt64());
    EXPECT_EQ(ref.GetArrayElement(0).GetInt64(), 10);
    ASSERT_TRUE(ref.GetArrayElement(1).IsString());
    EXPECT_EQ(ref.GetArrayElement(1).GetString(), "bar");
  }

  constexpr absl::string_view kInitialValue = "[1, \"foo\", null, {}, true]";
  {
    // Append 0 element.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 5);

    GOOGLESQL_ASSERT_OK(ref.AppendArrayElements({}));
    EXPECT_EQ(ref.GetArraySize(), 5);
    EXPECT_TRUE(ref.NormalizedEquals(
        JSONValue::ParseJSONString(kInitialValue).value().GetConstRef()));
  }

  {
    // Non-empty array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    ASSERT_EQ(ref.GetArraySize(), 5);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    GOOGLESQL_ASSERT_OK(ref.AppendArrayElements(std::move(values_to_insert)));
    EXPECT_EQ(ref.GetArraySize(), 7);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null, {}, true, 10, \"bar\"]")
            .value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }
}

TEST(JSONValueTest, AppendArrayElementLargeIndex) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.GetArrayElement(kJSONMaxArraySize - 1);
  ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize);

  EXPECT_THAT(ref.AppendArrayElement(JSONValue(int64_t{100})),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Exceeded maximum array size")));
  EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize);
}

TEST(JSONValueTest, AppendArrayElementsLargeIndex) {
  {
    // Array at max size. Appending 0 element is fine.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize - 1);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize);

    std::vector<JSONValue> values_to_insert;

    GOOGLESQL_ASSERT_OK(ref.AppendArrayElements(std::move(values_to_insert)));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize);
  }

  {
    // Array will exceed max size after appending. Fails.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize - 2);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize - 1);

    std::vector<JSONValue> values_to_insert;
    values_to_insert.emplace_back(int64_t{10});
    values_to_insert.emplace_back(std::string{"bar"});

    EXPECT_THAT(ref.AppendArrayElements(std::move(values_to_insert)),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize - 1);
  }

  {
    // Array size already > max size. Appending 0 element is fine.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.GetArrayElement(kJSONMaxArraySize);
    ASSERT_EQ(ref.GetArraySize(), kJSONMaxArraySize + 1);

    std::vector<JSONValue> values_to_insert;

    GOOGLESQL_EXPECT_OK(ref.AppendArrayElements(std::move(values_to_insert)));
    EXPECT_EQ(ref.GetArraySize(), kJSONMaxArraySize + 1);
  }
}

TEST(JSONValueTest, RemoveArrayElementFromNull) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  EXPECT_TRUE(ref.IsNull());

  EXPECT_THAT(ref.RemoveArrayElement(0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, RemoveArrayElementFromObject) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetToEmptyObject();
  ref.GetMember("key").SetInt64(10);

  EXPECT_TRUE(ref.IsObject());

  EXPECT_THAT(ref.RemoveArrayElement(0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, RemoveArrayElementFromPrimitiveTypes) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetString("hello");

  EXPECT_TRUE(ref.IsString());

  EXPECT_THAT(ref.RemoveArrayElement(0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an array")));
}

TEST(JSONValueTest, RemoveArrayElement) {
  {
    // Empty array.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyArray();

    EXPECT_THAT(ref.RemoveArrayElement(0), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetArraySize(), 0);
  }

  constexpr absl::string_view kInitialValue = "[1, \"foo\", null, {}]";
  {
    // Remove at the beginning of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    EXPECT_THAT(ref.RemoveArrayElement(0), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 3);
    JSONValue expected =
        JSONValue::ParseJSONString("[\"foo\", null, {}]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Remove in the middle of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    EXPECT_THAT(ref.RemoveArrayElement(2), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 3);
    JSONValue expected = JSONValue::ParseJSONString("[1, \"foo\", {}]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Remove at the end of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    EXPECT_THAT(ref.RemoveArrayElement(3), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 3);
    JSONValue expected =
        JSONValue::ParseJSONString("[1, \"foo\", null]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Remove past the end of an array.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    EXPECT_THAT(ref.RemoveArrayElement(10), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetArraySize(), 4);
    JSONValue expected = JSONValue::ParseJSONString(kInitialValue).value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Negative index.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetArraySize(), 4);

    EXPECT_THAT(ref.RemoveArrayElement(-1), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetArraySize(), 4);
    JSONValue expected = JSONValue::ParseJSONString(kInitialValue).value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Remove all elements.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    ASSERT_EQ(ref.GetArraySize(), 4);

    EXPECT_THAT(ref.RemoveArrayElement(1), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 3);
    EXPECT_THAT(ref.RemoveArrayElement(0), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 2);
    EXPECT_THAT(ref.RemoveArrayElement(1), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 1);
    EXPECT_THAT(ref.RemoveArrayElement(0), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetArraySize(), 0);
    JSONValue expected = JSONValue::ParseJSONString("[]").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));

    EXPECT_THAT(ref.RemoveArrayElement(1), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetArraySize(), 0);
    EXPECT_THAT(ref.RemoveArrayElement(0), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetArraySize(), 0);
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }
}

TEST(JSONValueTest, RemoveMemberFromNull) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();

  EXPECT_TRUE(ref.IsNull());

  EXPECT_THAT(ref.RemoveMember("key"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an object")));
}

TEST(JSONValueTest, RemoveMemberFromArray) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetToEmptyArray();
  ref.GetArrayElement(0).SetInt64(10);

  EXPECT_TRUE(ref.IsArray());

  EXPECT_THAT(ref.RemoveMember("key"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an object")));
}

TEST(JSONValueTest, RemoveMemberFromPrimitiveTypes) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  ref.SetString("hello");

  EXPECT_TRUE(ref.IsString());

  EXPECT_THAT(ref.RemoveMember("key"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("JSON value is not an object")));
}

TEST(JSONValueTest, RemoveMember) {
  {
    // Empty object.
    JSONValue value;
    JSONValueRef ref = value.GetRef();
    ref.SetToEmptyObject();

    EXPECT_THAT(ref.RemoveMember("key"), IsOkAndHolds(false));
  }

  constexpr absl::string_view kInitialValue =
      R"({"key": 10, "foo": [1, true]})";
  {
    // Remove inexistent key.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetMembers().size(), 2);

    EXPECT_THAT(ref.RemoveMember("inexistent key"), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetMembers().size(), 2);
    JSONValue expected = JSONValue::ParseJSONString(kInitialValue).value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetMembers().size(), 2);

    EXPECT_THAT(ref.RemoveMember("key"), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetMembers().size(), 1);
    JSONValue expected =
        JSONValue::ParseJSONString("{\"foo\": [1, true]}").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }

  {
    // Remove all keys.
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    EXPECT_EQ(ref.GetMembers().size(), 2);

    EXPECT_THAT(ref.RemoveMember("key"), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetMembers().size(), 1);
    EXPECT_THAT(ref.RemoveMember("foo"), IsOkAndHolds(true));
    EXPECT_EQ(ref.GetMembers().size(), 0);
    JSONValue expected = JSONValue::ParseJSONString("{}").value();
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));

    EXPECT_THAT(ref.RemoveMember("key"), IsOkAndHolds(false));
    EXPECT_EQ(ref.GetMembers().size(), 0);
    EXPECT_TRUE(ref.NormalizedEquals(expected.GetConstRef()));
  }
}

TEST(JsonValueTest, ErrorCleanupJsonObject) {
  // Verify non-OBJECT types throw an error as input.
  std::vector<absl::string_view> json_strings = {R"("foo")", "1.1", "1", "true",
                                                 "[]"};
  for (absl::string_view json : json_strings) {
    JSONValue value = JSONValue::ParseJSONString(json).value();
    EXPECT_THAT(
        value.GetRef().CleanupJsonObject(RemoveEmptyOptions::kObjectAndArray),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("JSON value is not an object.")));
  }
}

TEST(JsonValueTest, ErrorCleanupJsonArray) {
  // Verify non-ARRAY types throw an error as input.
  std::vector<absl::string_view> json_strings = {R"("foo")", "1.1", "1", "true",
                                                 "{}"};
  for (absl::string_view json : json_strings) {
    JSONValue value = JSONValue::ParseJSONString(json).value();
    EXPECT_THAT(
        value.GetRef().CleanupJsonArray(RemoveEmptyOptions::kObjectAndArray),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("JSON value is not an array.")));
  }
}

TEST(JsonValueTest, CleanupObject) {
  constexpr absl::string_view kInitialValue =
      R"({"a":null, "b":[null], "c":[], "d":1, "e":{"f":null}, "g":{}})";

  auto test_fn = [&kInitialValue](JSONValueRef::RemoveEmptyOptions option,
                                  absl::string_view expected_result) {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    GOOGLESQL_ASSERT_OK(value.GetRef().CleanupJsonObject(option));
    EXPECT_TRUE(value.GetRef().NormalizedEquals(
        JSONValue::ParseJSONString(expected_result)->GetConstRef()));
  };

  test_fn(JSONValueRef::RemoveEmptyOptions::kNone,
          R"({"b":[null], "c":[], "d":1, "e":{"f":null}, "g":{}})");
  test_fn(JSONValueRef::RemoveEmptyOptions::kObject,
          R"({"b":[null], "c":[], "d":1, "e":{"f":null}})");
  test_fn(JSONValueRef::RemoveEmptyOptions::kArray,
          R"({"b":[null], "d":1, "e":{"f":null}, "g":{}})");
  test_fn(JSONValueRef::RemoveEmptyOptions::kObjectAndArray,
          R"({"b":[null], "d":1, "e":{"f":null}})");
}

TEST(JsonValueTest, CleanupArray) {
  constexpr absl::string_view kInitialValue =
      R"([1, null, [], [1], {}, [null], {"a":[null]}])";

  auto test_fn = [&kInitialValue](JSONValueRef::RemoveEmptyOptions option,
                                  absl::string_view expected_result) {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    GOOGLESQL_ASSERT_OK(value.GetRef().CleanupJsonArray(option));
    EXPECT_TRUE(value.GetRef().NormalizedEquals(
        JSONValue::ParseJSONString(expected_result)->GetConstRef()));
  };

  test_fn(RemoveEmptyOptions::kNone,
          R"([1, [], [1], {}, [null], {"a":[null]}])");
  test_fn(RemoveEmptyOptions::kObject, R"([1, [], [1], [null], {"a":[null]}])");
  test_fn(RemoveEmptyOptions::kArray, R"([1, [1], {}, [null], {"a":[null]}])");
  test_fn(RemoveEmptyOptions::kObjectAndArray,
          R"([1, [1], [null], {"a":[null]}])");
}

namespace {
struct ComparisonTestParam {
  std::string json_x;
  std::string json_y;
  absl::partial_ordering result;
};

void check_json_compare(JSONValueConstRef x, JSONValueConstRef y,
                        absl::partial_ordering result) {
  bool x_eq_y = x == y;
  bool x_less_y = x < y;
  bool x_greater_y = x > y;

  bool x_ne_y = x != y;
  bool x_less_eq_y = x <= y;
  bool x_greater_eq_y = x >= y;
  ABSL_LOG(INFO) << "Comparing JSON values: " << x.ToString() << " and "
            << y.ToString();
  if (result == absl::partial_ordering::equivalent) {
    EXPECT_TRUE(x_eq_y && x_less_eq_y && x_greater_eq_y);
    EXPECT_FALSE(x_ne_y || x_less_y || x_greater_y);
  } else if (result == absl::partial_ordering::less) {
    EXPECT_TRUE(x_less_y && x_less_eq_y && x_ne_y);
    EXPECT_FALSE(x_eq_y || x_greater_y || x_greater_eq_y);
  } else if (result == absl::partial_ordering::greater) {
    EXPECT_TRUE(x_greater_y && x_greater_eq_y && x_ne_y);
    EXPECT_FALSE(x_eq_y || x_less_y || x_less_eq_y);
  } else {
    FAIL() << "Unsupported comparison result";
  }
}

void RunJsonComparisonTestCases(
    absl::Span<const ComparisonTestParam> test_cases) {
  for (const auto& t : test_cases) {
    JSONValue x_value = JSONValue::ParseJSONString(t.json_x).value();
    JSONValueConstRef x = x_value.GetConstRef();
    JSONValue y_value = JSONValue::ParseJSONString(t.json_y).value();
    JSONValueConstRef y = y_value.GetConstRef();

    // Validate the comparison results.
    check_json_compare(x, y, t.result);
    // Check that the comparison is symmetric.
    auto result_reversed = t.result;
    if (t.result == absl::partial_ordering::less) {
      result_reversed = absl::partial_ordering::greater;
    } else if (t.result == absl::partial_ordering::greater) {
      result_reversed = absl::partial_ordering::less;
    }
    check_json_compare(y, x, result_reversed);
  }
}

constexpr auto kEqual = absl::partial_ordering::equivalent;
constexpr auto kLess = absl::partial_ordering::less;
constexpr auto kGreater = absl::partial_ordering::greater;

}  // namespace

TEST(JsonValueTest, ComparisonBasic) {
  std::vector<ComparisonTestParam> test_cases = {
      {"null", "null", kEqual},
      {"null", R"("abc")", kLess},
      {R"("abc")", R"("abc")", kEqual},
      {R"("abc")", "1", kLess},
      {"1", "1", kEqual},
      {"1", "1.0", kEqual},
      {"1", "1.1", kLess},
      {"1.1", "false", kLess},
      {"false", "false", kEqual},
      {"false", "true", kLess},
      {"true", "true", kEqual},
      {"true", "[1, 2]", kLess},
      {"[1, 2]", "[1, 2]", kEqual},
      {"[1, 2]", R"({"a": 1})", kLess},
      {R"({"a": 1})", R"({"a": 1})", kEqual},
      {R"({"a": 1})", R"({"b": 1})", kLess},
      {R"({"a": 1})", R"({"a": 2})", kLess},
  };

  RunJsonComparisonTestCases(test_cases);
}

TEST(JsonValueTest, ComparisonScalar) {
  std::vector<ComparisonTestParam> test_cases = {
      // Strings.
      {R"("a")", R"("a")", kEqual},
      {R"("a")", R"("b")", kLess},
      {R"("\n")", R"("\n")", kEqual},
      {R"("\r")", R"("\n")", kGreater},

      // Numbers.
      {"-1", "0", kLess},
      {"0", "10", kLess},
      {"3", "3", kEqual},
      {"1.1", "1.2", kLess},

      // Integer like floats.
      {"0", "-0", kEqual},
      {"0", "0.0", kEqual},
      {"0", "0E0", kEqual},
      {"0.0", "0.0000", kEqual},
      {"-0", "-0.00", kEqual},
      {"-0.0", "-0E0", kEqual},

      {"1", "1.0", kEqual},
      {"1", "1E0", kEqual},
      {"1.0", "1.00", kEqual},
      {"1.00", "1E0", kEqual},

      {"-1", "-1.0", kEqual},
      {"-1", "-1E0", kEqual},
      {"-1.0", "-1.00", kEqual},
      {"-1.00", "-1E0", kEqual},

      // Booleans, covered by ComparisonBasic.
      {"false", "true", kLess},

      //
      // Mixed types sections.
      //
      // JSON strings.
      {R"("a")", "0", kLess},
      {R"("a")", "0.0", kLess},
      {R"("a")", "true", kLess},
      {R"("a")", "[]", kLess},
      {R"("a")", "[null]", kLess},
      {R"("a")", R"({"a": 1})", kLess},
      {R"("a")", R"(null)", kGreater},
      // JSON numbers.
      {"0.1", "1E-1", kEqual},
      {"0.1", "true", kLess},
      {"0.1", "[]", kLess},
      {"0.1", "[null]", kLess},
      {"0.1", R"({"a": 1})", kLess},
      {"0.1", R"(null)", kGreater},

      {"0.1", "10", kLess},
      {"10", "false", kLess},
      {"10", "true", kLess},
      {"10", "[]", kLess},
      {"10", "[null]", kLess},
      {"10", R"({"a": 1})", kLess},
      {"10", R"(null)", kGreater},

      // JSON booleans.
      {"false", "[]", kLess},
      {"false", "[null]", kLess},
      {"false", R"({"a": 1})", kLess},
      {"false", R"(null)", kGreater},

      {"true", "[]", kLess},
      {"true", "[null]", kLess},
      {"true", R"({"a": 1})", kLess},
      {"true", R"(null)", kGreater},
  };

  RunJsonComparisonTestCases(test_cases);
}

TEST(JsonValueTest, ComparisonNumbersBasic) {
  std::vector<ComparisonTestParam> test_cases = {
      // Simple Numbers.
      {"-1", "0", kLess},
      {"0", "10", kLess},
      {"3", "3", kEqual},
      {"1.1", "1.2", kLess},
      {"1.21", "1.20", kGreater},

      // Signed zeros.
      {"0", "-0", kEqual},
      {"0", "0.0", kEqual},
      {"0", "0E0", kEqual},
      {"0.0", "0.0000", kEqual},
      {"-0", "-0.00", kEqual},
      {"-0.0", "-0E0", kEqual},
      {"-0", "0.25", kLess},
      {"-0.0", "1.0", kLess},
      {"0.1", "-0.0", kGreater},
      {"0", "-0.1", kGreater},
      {"-0", "-0.1", kGreater},

      // Integer like doubles.
      {"1", "1.0", kEqual},
      {"1", "1E0", kEqual},
      {"1.0", "1.00", kEqual},
      {"1.00", "1E0", kEqual},

      {"-1", "-1.0", kEqual},
      {"-1", "-1E0", kEqual},
      {"-1.0", "-1.00", kEqual},
      {"-1.00", "-1E0", kEqual},
  };

  RunJsonComparisonTestCases(test_cases);
}

TEST(JsonValueTest, ComparisonNumbersComplex) {
  auto NextDouble = [](double v) { return std::nexttoward(v, INFINITY); };
  auto PrevDouble = [](double v) { return std::nexttoward(v, -INFINITY); };
  auto NumbersToJson =
      [](std::vector<std::vector<std::variant<int64_t, uint64_t, double>>>
             numbers) {
        std::vector<std::vector<std::string>> out;
        for (const auto& nums : numbers) {
          out.push_back({});
          for (const auto& num : nums) {
            JSONValue json;
            if (std::holds_alternative<int64_t>(num)) {
              json.GetRef().SetInt64(std::get<int64_t>(num));
            } else if (std::holds_alternative<uint64_t>(num)) {
              json.GetRef().SetUInt64(std::get<uint64_t>(num));
            } else {
              json.GetRef().SetDouble(std::get<double>(num));
            }
            out.back().push_back(json.GetConstRef().ToString());
          }
        }
        return out;
      };

  // This table is ordered. Values in the same nested vector are equal.
  // That is, given {{a}, {b, c}, {d}} we have a < b == c < d. This allows us
  // to generate test cases which exhaustively compare combinations of values,
  // inferring their expected order based on the indexes of the outer vector.
  std::vector<std::vector<std::string>> ordered_json_numbers = NumbersToJson({
      // lowest double
      {std::numeric_limits<double>::lowest()},
      // Exact value integers beyond int64 range. Anything with fewer than 53
      // significant digits is exact.
      {-0x87654321.54321p70},  // 52 sig figs
      {-0x87654321.4321p70},   // 48 sig figs
      {-0x87654321p70},        // 32 sig figs
      {-0x1.p70},
      // -2^63 (int64 min)
      {PrevDouble(-0x1p63)},
      {-0x1p63, std::numeric_limits<int64_t>::min()},
      {NextDouble(-0x1p63)},
      // -2^53. Bottom of double's integer range.
      {PrevDouble(-0x1p53), -9007199254740994},
      {-9007199254740993},  // no exact double
      {-0x1p53, -9007199254740992},
      {NextDouble(-0x1p53), -9007199254740991},
      {-6},
      // Values around zero
      {-1.0, -1},
      {-std::numeric_limits<double>::min()},
      {-std::numeric_limits<double>::denorm_min()},
      {-0.0, 0.0, 0},
      {std::numeric_limits<double>::denorm_min()},
      {std::numeric_limits<double>::min()},
      {1.0, 1},
      // 2^53. Top of double's integer range.
      {PrevDouble(0x1p53), 9007199254740991},
      {0x1p53, 9007199254740992},
      {9007199254740993},  // no exact double
      {NextDouble(0x1p53), 9007199254740994},
      // ~2^63 (int64 max)
      {PrevDouble(0x1p63)},
      {std::numeric_limits<int64_t>::max()},  // no exact double value
      {0x1p63, uint64_t{1} << 63},
      {NextDouble(0x1p63)},
      // ~2^64 (uint64 max)
      {PrevDouble(0x1p64)},
      // 2^64 - 6, overflow as -6 when cast to int64_t.
      {18446744073709551610ull},
      {std::numeric_limits<uint64_t>::max()},  // no exact double value
      {0x1p64},
      {NextDouble(0x1p64)},
      // Exact value integers beyond int64 range.
      {0x1.p70},              // 1 sig fig
      {0x87654321p70},        // 32 sig figs
      {0x87654321.4321p70},   // 48 sig figs
      {0x87654321.54321p70},  // 52 sig figs
      // Largest double
      {std::numeric_limits<double>::max()},
  });

  // Generate test cases for all pairs of values.
  std::vector<ComparisonTestParam> test_cases;
  for (int i = 0; i < ordered_json_numbers.size(); ++i) {
    for (const std::string& lhs : ordered_json_numbers[i]) {
      for (int j = 0; j < ordered_json_numbers.size(); ++j) {
        for (const std::string& rhs : ordered_json_numbers[j]) {
          test_cases.push_back(
              {lhs, rhs, i < j ? kLess : (i == j ? kEqual : kGreater)});
        }
      }
    }
  }
  RunJsonComparisonTestCases(test_cases);
}

TEST(JsonValueTest, ComparisonArray) {
  std::vector<ComparisonTestParam> test_cases = {
      // Empty arrays.
      {"[]", "[]", kEqual},
      {"[]", "[null]", kLess},
      {"[]", "[1]", kLess},

      // Top level Arrays.
      {"[null]", "[null]", kEqual},
      {"[null]", "[1]", kLess},
      {"[null]", "[null, null]", kLess},
      {"[1]", "[1]", kEqual},
      {"[1]", "[1, 1]", kLess},
      {"[1]", "[1, 2]", kLess},
      {"[0, 1]", "[1]", kLess},
      {"[1, 2]", "[1, 2]", kEqual},
      {"[1, 2]", "[1, 3]", kLess},
      {"[1, 2]", "[1, 2, 3]", kLess},
      {"[1, 2]", "[2]", kLess},
      {"[1, 2]", "[2, 1]", kLess},
      {"[1, 1]", "[2, 1]", kLess},

      // Nested arrays.
      {"[[null]]", "[[null]]", kEqual},
      {"[[null]]", "[[null, null]]", kLess},
      {"[[1]]", "[[1]]", kEqual},
      {"[[1]]", "[[1, 1]]", kLess},
      {"[[1]]", "[[1, 2]]", kLess},

      {"[[1, 2]]", "[[1.0, 2]]", kEqual},
      {"[[1, 2]]", "[[1, 3]]", kLess},
      {"[[1, 2]]", "[[1, 2, 3]]", kLess},
      {"[[1, 2]]", "[[2]]", kLess},
      {"[[1, 2]]", "[[2, 1]]", kLess},
      {"[[1, 1]]", "[[2, 1]]", kLess},

      {"[[1, 2], 3]", "[[1, 2], 4]", kLess},
      {"[[1, 2], 3]", "[[1, 2.00], 3]", kEqual},
      {"[[1, 2], 3]", "[[1, 2], 4, 5]", kLess},
      {"[[1, 2], 3]", "[[2], 3]", kLess},
      {"[[1, 2], 3]", "[[2, 1], 3]", kLess},
      {"[[1, 1], 3]", "[[2, 1], 3]", kLess},

      {"[1]", "[[null]]", kLess},
      {"[1]", "[[1]]", kLess},
      {"[1]", "[[1], 1]", kLess},
      {"[1]", "[1, [1]]", kLess},

      {"[1, [2]]", "[1, [2]]", kEqual},
      {"[1, [2]]", "[1, [2, 3]]", kLess},
      {"[1, [2]]", "[1, [2], 3]", kLess},

      // Array with Objects.
      {"[2]", R"([2, {"a": 1}])", kLess},
      {R"([{"a": 1}, 2])", R"([{"a": 1}, 2])", kEqual},
      {R"([{"a": 1}, 2])", R"([{"a": 2}, 3])", kLess},
      {R"([{"a": 1}, 2])", R"([{"a": 1}, 3])", kLess},
  };

  RunJsonComparisonTestCases(test_cases);
}

TEST(JsonValueTest, ComparisonObject) {
  std::vector<ComparisonTestParam> test_cases = {
      // JSON empty objects.
      {"{}", "{}", kEqual},
      {"{}", R"({"": 1})", kLess},  // Empty string key.
      {"{}", R"({"a": 1})", kLess},
      {"{}", R"({"a": 1, "b": 2})", kLess},

      // JSON objects with empty keys.
      {R"({"": 1})", R"({"": 1})", kEqual},
      {R"({"": 1})", R"({"a": 1})", kLess},
      {R"({"": 1})", R"({"": 2})", kLess},

      // JSON with backslash.
      {R"({"\n": 1})", R"({"\n": 1.0})", kEqual},
      {R"({"\n": 1})", R"({"A": 1})", kLess},
      {R"({"\n": "\r"})", R"({"A": "\r"})", kLess},
      {R"({"\n": "\r"})", R"({"\n": "\r"})", kEqual},
      {R"({"\r": "\r"})", R"({"\n": "\r"})", kGreater},
      {R"({"\r": "\r"})", R"({"\r": "\n"})", kGreater},

      // JSON objects with single key.
      {R"({"a": 1})", R"({"a": 1.0})", kEqual},
      {R"({"a": 1})", R"({"a": 2})", kLess},
      {R"({"a": 1})", R"({"b": 1})", kLess},
      {R"({"a": 2})", R"({"b": 1})", kLess},
      {R"({"aa": 1})", R"({"b": 1})", kLess},

      // JSON objects with multiple keys.
      {R"({"a": 1})", R"({"a": 1, "b": 2})", kLess},
      {R"({"a": 1})", R"({"b": 2, "a": 1})", kLess},
      {R"({"a": 1, "b": 2})", R"({"b": 2})", kLess},
      {R"({"b": 2, "a": 1})", R"({"b": 2})", kLess},

      // JSON nested objects.
      {R"({"a": {"b": 1}})", R"({"a": {"b": 1}})", kEqual},
      {R"({"a": {"b": 1}})", R"({"a": {"b": 2}})", kLess},
      {R"({"a": {"b": 1}})", R"({"a": {"c": 1}})", kLess},
      {R"({"a": {"b": 1}})", R"({"a": {"b": 1, "c": 1}})", kLess},

      {R"({"a": {"b": 1, "c": 1}})", R"({"a": {"c": 1}})", kLess},
      {R"({"a": {"b": 1, "c": 1}})", R"({"a": {"c": 1}})", kLess},

      // JSON objects with null values.
      {R"({"a": null})", R"({"a": null})", kEqual},
      {R"({"a": null})", R"({"a": 1})", kLess},
      {R"({"a": null})", R"({"b": null})", kLess},
      {R"({"a": null})", R"({"a": null, "b": 1})", kLess},
  };

  RunJsonComparisonTestCases(test_cases);
}

// TODO: Add more tests.
TEST(JSONValueValidator, ValidJSON) {
  std::vector<std::string> jsons = {
      "10",
      "-1.3123",
      "null",
      "true",
      "false",
      R"("foo")",
      R"("10")",
      R"([10, "bar", null])",
      R"({"foo": "bar", "abc": null, "bar": 15})",
      R"({"a": {"b": {"c": [10, null, [null, "a", 1e10]]}}})",
      R"([[[[[[[[10], null], "foo"]]]]]])",
      R"([10, {"foo": 20}, [null, "abc", {"bar": "baz"}]])",
  };

  for (const std::string& json : jsons) {
    GOOGLESQL_EXPECT_OK(IsValidJSON(json));
  }
}

// TODO: Add more tests.
TEST(JSONValueValidator, InvalidJSON) {
  // Nesting level
  std::string json =
      R"({"foo": [{"bar": 10}, 20, [30], {"bar": 30}]})";  // nesting level: 3
  GOOGLESQL_EXPECT_OK(IsValidJSON(json));
  GOOGLESQL_EXPECT_OK(IsValidJSON(json, {.max_nesting = 3}));

  EXPECT_THAT(IsValidJSON(json, {.max_nesting = 2}),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Max nesting of 2 has been exceeded while "
                                 "parsing JSON document")));

  // Strict number parsing
  std::string large_double = R"({"foo": 123456789012345678901234567890})";
  GOOGLESQL_EXPECT_OK(IsValidJSON(large_double));
  EXPECT_THAT(
      IsValidJSON(large_double, {.wide_number_mode = WideNumberMode::kExact}),
      StatusIs(absl::StatusCode::kOutOfRange,
               ::testing::HasSubstr("cannot round-trip")));

  // Invalid values
  std::vector<std::pair<std::string, std::string>> jsons_and_errors = {
      {R"({"foo": [{"bar"= 10}, 20]})", "invalid literal"},
      {R"({"foo": [{"bar": 10}, 20])", "unexpected end of input"},
      {R"([[10, 20])", "unexpected end of input"},
      {R"([10, 20]])", "expected end of input"},
      {R"([10})", "unexpected '}'"},
      {R"({"foo": 10])", "unexpected ']'"},
      {R"({"foo": [10})", "unexpected '}'"},
  };

  for (const auto& [json, error] : jsons_and_errors) {
    EXPECT_THAT(IsValidJSON(json),
                StatusIs(absl::StatusCode::kOutOfRange,
                         AllOf(HasSubstr("syntax error while parsing"),
                               HasSubstr(error))));
  }
}

}  // namespace
