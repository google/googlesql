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

#include "googlesql/public/variant_value.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/simple_token_list.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

// --- Primitives ---

class VariantValuePrimitiveTest : public ::testing::TestWithParam<Value> {};

TEST_P(VariantValuePrimitiveTest, PrimitiveStates) {
  const Value& val = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(val));
  EXPECT_THAT(ref.IsPrimitive(), true);
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.is_valid(), true);
  EXPECT_THAT(ref.is_null(), false);
  EXPECT_THAT(ref.IsVariantNull(), false);
  EXPECT_THAT(ref.GetPrimitiveValue(), IsOkAndHolds(val));
  EXPECT_THAT(ref.GetKeys(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetKeyValue("a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetArraySize(), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_P(VariantValuePrimitiveTest, ToVariantValue) {
  const Value& val = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView view, VariantValueView::Create(val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_val, view.ToVariantValue());
  EXPECT_TRUE(variant_val.type()->IsVariant());
  EXPECT_FALSE(variant_val.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_variant, Value::Variant(val));
  EXPECT_EQ(variant_val, expected_variant);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_view,
                       variant_val.variant_value());
  EXPECT_TRUE(roundtrip_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_view.IsObject());
  EXPECT_FALSE(roundtrip_view.IsArray());
  EXPECT_FALSE(roundtrip_view.IsVariantNull());
  EXPECT_THAT(roundtrip_view.GetPrimitiveValue(), IsOkAndHolds(val));
  EXPECT_EQ(roundtrip_view.DebugString(), view.DebugString());
}

INSTANTIATE_TEST_SUITE_P(
    AllPrimitives, VariantValuePrimitiveTest,
    ::testing::Values(Value::Int32(123), Value::Uint32(123), Value::Int64(123),
                      Value::Uint64(123), Value::Bool(true),
                      Value::Float(3.14f), Value::Double(3.14),
                      Value::String("hello"), Value::Bytes("bytes"),
                      Value::Date(123), Value::TimestampFromUnixMicros(123),
                      Value::Enum(types::UnsupportedFieldsEnumType(), 1),
                      Value::Interval(IntervalValue()),
                      Value::TokenList(tokens::TokenList())));

TEST(VariantValueTest, RangeTypeInVariant) {
  // Range type is not considered an Object/Array/Primitive.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_val,
                       Value::MakeRange(Value::Date(1), Value::Date(2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(range_val));
  EXPECT_THAT(ref.IsPrimitive(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.GetKeys(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetKeyValue("a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetPrimitiveValue(),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetArraySize(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetElement(0), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetElementIfExists(0),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetKeyValueIfExists("a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(ref.HasKey("a"));
}

// --- Arrays ---

TEST(VariantValueTest, ArrayStates) {
  Value array_val =
      Value::Array(types::Int64ArrayType(), {Value::Int64(1), Value::Int64(2)});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(array_val));
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), true);

  EXPECT_THAT(ref.GetArraySize(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> elem0,
                       ref.GetElement(0));
  ASSERT_TRUE(elem0.has_value());
  EXPECT_THAT(elem0->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));

  EXPECT_THAT(ref.GetElement(2), IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(ref.GetElementIfExists(2),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(VariantValueTest, ArrayWithSqlNullElementExposedAsVariantNull) {
  Value array_val =
      Value::Array(types::Int64ArrayType(),
                   {Value::Int64(10), Value::NullInt64(), Value::Int64(30)});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(array_val));
  EXPECT_THAT(ref.IsArray(), true);
  EXPECT_THAT(ref.GetArraySize(), IsOkAndHolds(3));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem0, ref.GetElementIfExists(0));
  EXPECT_THAT(elem0.IsPrimitive(), true);
  EXPECT_THAT(elem0.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(10)));
  EXPECT_THAT(elem0.IsVariantNull(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem1, ref.GetElementIfExists(1));
  EXPECT_THAT(elem1.IsVariantNull(), true);
  EXPECT_FALSE(elem1.is_null());
  EXPECT_THAT(elem1.IsPrimitive(), true);
  EXPECT_THAT(elem1.IsObject(), false);
  EXPECT_THAT(elem1.IsArray(), false);
  EXPECT_THAT(elem1.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem2, ref.GetElementIfExists(2));
  EXPECT_THAT(elem2.IsPrimitive(), true);
  EXPECT_THAT(elem2.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(30)));
  EXPECT_THAT(elem2.IsVariantNull(), false);
}

// --- Structs ---

TEST(VariantValueTest, StructObjectStates) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type,
                        {Value::Int64(1), Value::String("hello")}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(struct_val));
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.IsPrimitive(), false);
  EXPECT_THAT(ref.IsArray(), false);

  EXPECT_THAT(ref.GetKeys(), IsOkAndHolds(ElementsAre("a", "b")));
  EXPECT_TRUE(ref.HasKey("a"));
  EXPECT_TRUE(ref.HasKey("b"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> a_ref,
                       ref.GetKeyValue("a"));
  ASSERT_TRUE(a_ref.has_value());
  EXPECT_THAT(a_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView a_ref_strict,
                       ref.GetKeyValueIfExists("a"));
  EXPECT_THAT(a_ref_strict.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));

  EXPECT_THAT(ref.GetKeyValue("c"), IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(ref.GetKeyValueIfExists("c"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(VariantValueTest, StructDeduplication) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()},
                                    {"a", types::StringType()},
                                    {"b", types::BoolType()},
                                    {"A", types::DateType()}},
                                   &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type, {Value::Int64(1), Value::String("hello"),
                                      Value::Bool(true), Value::Date(2024)}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(struct_val));
  // GetKeys should return deduped keys
  EXPECT_THAT(ref.GetKeys(), IsOkAndHolds(ElementsAre("a", "b", "A")));

  // GetKeyValue("a") should return the first one (Int64(1))
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> a_ref,
                       ref.GetKeyValue("a"));
  ASSERT_TRUE(a_ref.has_value());
  EXPECT_THAT(a_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));

  // HasKey should be consistent
  EXPECT_TRUE(ref.HasKey("a"));
  EXPECT_TRUE(ref.HasKey("b"));
  EXPECT_TRUE(ref.HasKey("A"));
}

TEST(VariantValueTest, StructWithEmptyFieldNames) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"", types::Int64Type()}, {"a", types::StringType()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type,
                        {Value::Int64(1), Value::String("hello")}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(struct_val));
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.GetKeys(), IsOkAndHolds(ElementsAre("", "a")));
  EXPECT_TRUE(ref.HasKey(""));
  EXPECT_TRUE(ref.HasKey("a"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> empty_ref,
                       ref.GetKeyValue(""));
  ASSERT_TRUE(empty_ref.has_value());
  EXPECT_THAT(empty_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
}

TEST(VariantValueTest, StructWithSqlNullFieldExposedAsVariantNull) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type,
                        {Value::NullInt64(), Value::String("hello")}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(struct_val));
  EXPECT_THAT(ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView a_ref, ref.GetKeyValueIfExists("a"));
  EXPECT_THAT(a_ref.IsVariantNull(), true);
  EXPECT_FALSE(a_ref.is_null());
  EXPECT_THAT(a_ref.IsPrimitive(), true);
  EXPECT_THAT(a_ref.IsObject(), false);
  EXPECT_THAT(a_ref.IsArray(), false);
  EXPECT_THAT(a_ref.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView b_ref, ref.GetKeyValueIfExists("b"));
  EXPECT_THAT(b_ref.IsVariantNull(), false);
  EXPECT_THAT(b_ref.IsPrimitive(), true);
  EXPECT_THAT(b_ref.GetPrimitiveValue(), IsOkAndHolds(Value::String("hello")));
}

// --- Maps ---

TEST(VariantValueTest, MapObjectStates) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::String("a"), Value::Int64(1)},
                                {Value::String("b"), Value::Int64(2)}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.IsArray(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> a_ref,
                       ref.GetKeyValue("a"));
  ASSERT_TRUE(a_ref.has_value());
  EXPECT_THAT(a_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));

  EXPECT_THAT(ref.GetKeyValue("c"), IsOkAndHolds(testing::Eq(std::nullopt)));
}

TEST(VariantValueTest, MapObjectStatesWithNullKey) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::NullString(), Value::Int64(1)},
                                {Value::String("a"), Value::Int64(2)}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  EXPECT_THAT(ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> keys, ref.GetKeys());
  EXPECT_THAT(keys, ElementsAre("a"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> a_ref,
                       ref.GetKeyValue("a"));
  ASSERT_TRUE(a_ref.has_value());
  EXPECT_THAT(a_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(2)));
}

TEST(VariantValueTest, MapWithEmptyKey) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::String(""), Value::Int64(1)},
                                {Value::String("a"), Value::Int64(2)}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.GetKeys(), IsOkAndHolds(ElementsAre("", "a")));
  EXPECT_TRUE(ref.HasKey(""));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> empty_ref,
                       ref.GetKeyValue(""));
  ASSERT_TRUE(empty_ref.has_value());
  EXPECT_THAT(empty_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
}

TEST(VariantValueTest, MapWithSqlNullValueExposedAsVariantNull) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::String("a"), Value::NullInt64()},
                                {Value::String("b"), Value::Int64(42)}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  EXPECT_THAT(ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView a_ref, ref.GetKeyValueIfExists("a"));
  EXPECT_THAT(a_ref.IsVariantNull(), true);
  EXPECT_FALSE(a_ref.is_null());
  EXPECT_THAT(a_ref.IsPrimitive(), true);
  EXPECT_THAT(a_ref.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView b_ref, ref.GetKeyValueIfExists("b"));
  EXPECT_THAT(b_ref.IsVariantNull(), false);
  EXPECT_THAT(b_ref.IsPrimitive(), true);
  EXPECT_THAT(b_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(42)));
}

TEST(VariantValueTest, MapWithNonStringKeyIsNotObject) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::Int64Type(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::Int64(1), Value::Int64(2)}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  // Map with non-string key is not considered an Object/Array/Primitive.
  EXPECT_THAT(ref.IsPrimitive(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.GetKeys(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetKeyValue("a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// --- JSON ---

TEST(VariantValueTest, JsonStates) {
  // JSON Number
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_num, JSONValue::ParseJSONString("123"));
  Value json_num = Value::Json(std::move(parsed_json_num));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_num,
                       VariantValueView::Create(json_num));
  EXPECT_THAT(ref_num.IsPrimitive(), true);
  EXPECT_THAT(ref_num.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(123)));
  EXPECT_THAT(ref_num.IsObject(), false);
  EXPECT_THAT(ref_num.IsArray(), false);
  EXPECT_THAT(ref_num.IsVariantNull(), false);

  // JSON UInt64
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_uint,
                       JSONValue::ParseJSONString("18446744073709551615"));
  Value json_uint = Value::Json(std::move(parsed_json_uint));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_uint,
                       VariantValueView::Create(json_uint));
  EXPECT_THAT(ref_uint.IsPrimitive(), true);
  EXPECT_THAT(ref_uint.GetPrimitiveValue(),
              IsOkAndHolds(Value::Uint64(18446744073709551615ULL)));
  EXPECT_THAT(ref_uint.IsObject(), false);
  EXPECT_THAT(ref_uint.IsArray(), false);
  EXPECT_THAT(ref_uint.IsVariantNull(), false);

  // JSON Double
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_double,
                       JSONValue::ParseJSONString("3.14"));
  Value json_double = Value::Json(std::move(parsed_json_double));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_double,
                       VariantValueView::Create(json_double));
  EXPECT_THAT(ref_double.IsPrimitive(), true);
  EXPECT_THAT(ref_double.GetPrimitiveValue(),
              IsOkAndHolds(Value::Double(3.14)));
  EXPECT_THAT(ref_double.IsObject(), false);
  EXPECT_THAT(ref_double.IsArray(), false);
  EXPECT_THAT(ref_double.IsVariantNull(), false);

  // JSON String
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_str,
                       JSONValue::ParseJSONString("\"hello\""));
  Value json_str = Value::Json(std::move(parsed_json_str));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_str,
                       VariantValueView::Create(json_str));
  EXPECT_THAT(ref_str.IsPrimitive(), true);
  EXPECT_THAT(ref_str.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("hello")));

  // JSON Boolean
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_bool,
                       JSONValue::ParseJSONString("true"));
  Value json_bool = Value::Json(std::move(parsed_json_bool));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_bool,
                       VariantValueView::Create(json_bool));
  EXPECT_THAT(ref_bool.IsPrimitive(), true);
  EXPECT_THAT(ref_bool.GetPrimitiveValue(), IsOkAndHolds(Value::Bool(true)));

  // JSON Null
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_null,
                       JSONValue::ParseJSONString("null"));
  Value json_null = Value::Json(std::move(parsed_json_null));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_null,
                       VariantValueView::Create(json_null));
  EXPECT_THAT(ref_null.IsPrimitive(), true);
  EXPECT_THAT(ref_null.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));
  EXPECT_THAT(ref_null.IsVariantNull(), true);

  // JSON Object
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_obj,
                       JSONValue::ParseJSONString("{\"a\": 1}"));
  Value json_obj = Value::Json(std::move(parsed_json_obj));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_obj,
                       VariantValueView::Create(json_obj));
  EXPECT_THAT(ref_obj.IsPrimitive(), false);
  EXPECT_THAT(ref_obj.IsObject(), true);

  // JSON Array
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_arr,
                       JSONValue::ParseJSONString("[1, 2]"));
  Value json_arr = Value::Json(std::move(parsed_json_arr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_arr,
                       VariantValueView::Create(json_arr));
  EXPECT_THAT(ref_arr.IsPrimitive(), false);
  EXPECT_THAT(ref_arr.IsArray(), true);
}

TEST(VariantValueTest, UnparsedJsonPrimitiveStates) {
  // Unparsed JSON Int64
  Value json_int = Value::UnvalidatedJsonString("123");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_int,
                       VariantValueView::Create(json_int));
  EXPECT_THAT(ref_int.IsPrimitive(), true);
  EXPECT_THAT(ref_int.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(123)));
  EXPECT_THAT(ref_int.IsObject(), false);
  EXPECT_THAT(ref_int.IsArray(), false);
  EXPECT_THAT(ref_int.IsVariantNull(), false);

  // Unparsed JSON UInt64
  Value json_uint = Value::UnvalidatedJsonString("18446744073709551615");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_uint,
                       VariantValueView::Create(json_uint));
  EXPECT_THAT(ref_uint.IsPrimitive(), true);
  EXPECT_THAT(ref_uint.GetPrimitiveValue(),
              IsOkAndHolds(Value::Uint64(18446744073709551615ULL)));

  // Unparsed JSON Double
  Value json_double = Value::UnvalidatedJsonString("3.14");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_double,
                       VariantValueView::Create(json_double));
  EXPECT_THAT(ref_double.IsPrimitive(), true);
  EXPECT_THAT(ref_double.GetPrimitiveValue(),
              IsOkAndHolds(Value::Double(3.14)));

  // Unparsed JSON String
  Value json_str = Value::UnvalidatedJsonString("\"hello world\"");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_str,
                       VariantValueView::Create(json_str));
  EXPECT_THAT(ref_str.IsPrimitive(), true);
  EXPECT_THAT(ref_str.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("hello world")));

  // Unparsed JSON Boolean
  Value json_bool = Value::UnvalidatedJsonString("true");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_bool,
                       VariantValueView::Create(json_bool));
  EXPECT_THAT(ref_bool.IsPrimitive(), true);
  EXPECT_THAT(ref_bool.GetPrimitiveValue(), IsOkAndHolds(Value::Bool(true)));

  // Unparsed JSON Null
  Value json_null = Value::UnvalidatedJsonString("null");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_null,
                       VariantValueView::Create(json_null));
  EXPECT_THAT(ref_null.IsPrimitive(), true);
  EXPECT_THAT(ref_null.IsObject(), false);
  EXPECT_THAT(ref_null.IsArray(), false);
  EXPECT_THAT(ref_null.IsVariantNull(), true);
  EXPECT_FALSE(ref_null.is_null());
  EXPECT_THAT(ref_null.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));
}

TEST(VariantValueTest, JsonWithEmptyKey) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json,
                       JSONValue::ParseJSONString("{\"\": 1, \"a\": 2}"));
  Value json_obj = Value::Json(std::move(parsed_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(json_obj));
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.GetKeys(), IsOkAndHolds(ElementsAre("", "a")));
  EXPECT_TRUE(ref.HasKey(""));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> empty_ref,
                       ref.GetKeyValue(""));
  ASSERT_TRUE(empty_ref.has_value());
  EXPECT_THAT(empty_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
}

TEST(VariantValueTest, UnparsedJsonSafety) {
  Value unparsed_json = Value::UnvalidatedJsonString("{\"a\": 1}");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(unparsed_json));

  EXPECT_THAT(ref.IsPrimitive(), false);
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsVariantNull(), false);

  EXPECT_THAT(ref.GetPrimitiveValue(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(ref.GetKeys(), IsOkAndHolds(ElementsAre("a")));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> a_ref,
                       ref.GetKeyValue("a"));
  ASSERT_TRUE(a_ref.has_value());
  EXPECT_THAT(a_ref->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
}

TEST(VariantValueTest, UnparsedJsonArraySafety) {
  Value unparsed_json = Value::UnvalidatedJsonString("[1, 2]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(unparsed_json));

  EXPECT_THAT(ref.IsPrimitive(), false);
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), true);
  EXPECT_THAT(ref.IsVariantNull(), false);

  EXPECT_THAT(ref.GetArraySize(), IsOkAndHolds(2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> elem0,
                       ref.GetElement(0));
  ASSERT_TRUE(elem0.has_value());
  EXPECT_THAT(elem0->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
}

TEST(VariantValueTest, JsonArrayWithExplicitNullElement) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json,
                       JSONValue::ParseJSONString("[1, null, \"foo\"]"));
  Value json_arr = Value::Json(std::move(parsed_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(json_arr));
  EXPECT_THAT(ref.IsArray(), true);
  EXPECT_THAT(ref.GetArraySize(), IsOkAndHolds(3));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem1, ref.GetElementIfExists(1));
  EXPECT_THAT(elem1.IsVariantNull(), true);
  EXPECT_FALSE(elem1.is_null());
  EXPECT_THAT(elem1.IsPrimitive(), true);
}

TEST(VariantValueTest, JsonObjectWithExplicitNullMember) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json,
                       JSONValue::ParseJSONString("{\"a\": null, \"b\": 123}"));
  Value json_obj = Value::Json(std::move(parsed_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(json_obj));
  EXPECT_THAT(ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView a_ref, ref.GetKeyValueIfExists("a"));
  EXPECT_THAT(a_ref.IsVariantNull(), true);
  EXPECT_FALSE(a_ref.is_null());
  EXPECT_THAT(a_ref.IsPrimitive(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView b_ref, ref.GetKeyValueIfExists("b"));
  EXPECT_THAT(b_ref.IsVariantNull(), false);
  EXPECT_THAT(b_ref.IsPrimitive(), true);
  EXPECT_THAT(b_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(123)));
}

TEST(VariantValueTest, InvalidUnparsedJsonSafety) {
  Value corrupted_json =
      Value::UnvalidatedJsonString("{\"a\": 1");  // missing closing brace
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(corrupted_json));
  EXPECT_TRUE(ref.is_valid());
  EXPECT_FALSE(ref.is_null());

  // Accessors fail on invalid JSON during on-demand parsing.
  EXPECT_THAT(ref.IsPrimitive(), false);
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsVariantNull(), false);
  EXPECT_THAT(ref.GetPrimitiveValue(),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ref.GetKeys(), StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(ref.GetKeyValue("a"), StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(ref.GetArraySize(), StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(ref.GetElement(0), StatusIs(absl::StatusCode::kOutOfRange));

  EXPECT_FALSE(ref.HasKey("a"));
}

// --- Proto ---

TEST(VariantValueTest, ProtoExposedAsObjectAndArray) {
  TypeFactory factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &proto_type));

  googlesql_test::KitchenSinkPB proto_msg;
  proto_msg.set_int64_key_1(123);
  proto_msg.set_int64_key_2(456);
  proto_msg.add_repeated_int32_val(10);
  proto_msg.add_repeated_int32_val(20);
  proto_msg.add_repeated_string_val("hello");
  proto_msg.add_repeated_string_val("world");

  Value proto_val = Value::Proto(proto_type, proto_msg.SerializeAsCord());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(proto_val));
  EXPECT_THAT(ref.IsObject(), true);
  EXPECT_THAT(ref.IsArray(), false);

  // Object behavior: GetKeys
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> keys, ref.GetKeys());
  EXPECT_THAT(keys, testing::Contains("repeated_int32_val"));
  EXPECT_THAT(keys, testing::Contains("repeated_string_val"));
  EXPECT_THAT(keys, testing::Contains("int64_key_1"));

  // Object behavior: GetKeyValue for repeated primitive
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView repeated_int_ref,
                       ref.GetKeyValueIfExists("repeated_int32_val"));

  // The exposed behavior for repeated field itself
  EXPECT_THAT(repeated_int_ref.IsArray(), true);
  EXPECT_THAT(repeated_int_ref.IsObject(), false);
  EXPECT_THAT(repeated_int_ref.IsPrimitive(), false);

  // Array behavior on the repeated field
  EXPECT_THAT(repeated_int_ref.GetArraySize(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem0,
                       repeated_int_ref.GetElementIfExists(0));
  EXPECT_THAT(elem0.IsPrimitive(), true);
  EXPECT_THAT(elem0.GetPrimitiveValue(), IsOkAndHolds(Value::Int32(10)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem1,
                       repeated_int_ref.GetElementIfExists(1));
  EXPECT_THAT(elem1.IsPrimitive(), true);
  EXPECT_THAT(elem1.GetPrimitiveValue(), IsOkAndHolds(Value::Int32(20)));

  // Repeat for string
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView repeated_str_ref,
                       ref.GetKeyValueIfExists("repeated_string_val"));
  EXPECT_THAT(repeated_str_ref.IsArray(), true);
  EXPECT_THAT(repeated_str_ref.GetArraySize(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView str_elem0,
                       repeated_str_ref.GetElementIfExists(0));
  EXPECT_THAT(str_elem0.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("hello")));

  // Empty repeated field
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView empty_repeated_ref,
                       ref.GetKeyValueIfExists("repeated_float_val"));
  EXPECT_THAT(empty_repeated_ref.IsArray(), true);
  EXPECT_THAT(empty_repeated_ref.GetArraySize(), IsOkAndHolds(0));

  // Verify GetKeys fails on array reference
  EXPECT_THAT(repeated_int_ref.GetKeys(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Not an object")));
}

TEST(VariantValueTest, ProtoUnsetFieldExposedAsDefaultOrMissing) {
  TypeFactory factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &proto_type));

  googlesql_test::KitchenSinkPB proto_msg;
  proto_msg.set_int64_key_1(123);
  proto_msg.set_int64_key_2(456);
  // int64_val is unset
  // int_with_no_default_nullable is unset and has (googlesql.use_defaults) =
  // false
  Value proto_val = Value::Proto(proto_type, proto_msg.SerializeAsCord());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(proto_val));
  EXPECT_THAT(ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView set_field,
                       ref.GetKeyValueIfExists("int64_key_1"));
  EXPECT_THAT(set_field.IsPrimitive(), true);
  EXPECT_THAT(set_field.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(123)));
  EXPECT_THAT(set_field.IsVariantNull(), false);

  // Unset field with defaults enabled returns default value (0)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView unset_field_with_default,
                       ref.GetKeyValueIfExists("int64_val"));
  EXPECT_THAT(unset_field_with_default.IsVariantNull(), false);
  EXPECT_FALSE(unset_field_with_default.is_null());
  EXPECT_THAT(unset_field_with_default.IsPrimitive(), true);
  EXPECT_THAT(unset_field_with_default.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(0)));

  // Unset field with defaults disabled returns VariantNull
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView unset_field_without_default,
                       ref.GetKeyValueIfExists("int_with_no_default_nullable"));
  EXPECT_THAT(unset_field_without_default.IsVariantNull(), true);
}

// --- Null Behavior ---

TEST(VariantValueTest, JsonNullState) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_null,
                       JSONValue::ParseJSONString("null"));
  Value json_null = Value::Json(std::move(parsed_json_null));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(json_null));
  EXPECT_THAT(ref.IsPrimitive(), true);
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsVariantNull(), true);
  EXPECT_FALSE(ref.is_null());
}

TEST(VariantValueTest, VariantNull) {
  // Verify that the JSONValue::ParseJSONString("null") is the same as the
  // default JSONValue
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_null, JSONValue::ParseJSONString("null"));
  EXPECT_TRUE(parsed_null.GetConstRef().IsNull());

  JSONValue default_json = JSONValue();
  EXPECT_TRUE(default_json.GetConstRef().IsNull());
  EXPECT_TRUE(
      parsed_null.GetConstRef().NormalizedEquals(default_json.GetConstRef()));

  // Create a Variant null value.
  Value variant_null = Value::Json(std::move(default_json));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(variant_null));
  EXPECT_TRUE(ref.IsVariantNull());
  EXPECT_FALSE(ref.is_null());
}

TEST(VariantValueTest, UntypedSqlNullState) {
  Value null_int = Value::NullInt64();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(null_int));
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsVariantNull(),
              false);  // SQL NULL is not Variant null.
  EXPECT_TRUE(ref.is_null());
}

TEST(VariantValueTest, TypedSqlNullState) {
  Value null_string = Value::NullString();
  Value null_json = Value::NullJson();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(null_string));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref2,
                       VariantValueView::Create(null_json));
  EXPECT_THAT(ref.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsVariantNull(),
              false);  // SQL NULL is not Variant null.
  EXPECT_TRUE(ref.is_null());

  EXPECT_THAT(ref2.IsObject(), false);
  EXPECT_THAT(ref.IsArray(), false);
  EXPECT_THAT(ref.IsVariantNull(),
              false);  // SQL NULL is not Variant null.
  EXPECT_TRUE(ref.is_null());
}

// --- Complex / Nested Structures ---

TEST(VariantLifetimeTest, JsonSubtreeNested) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json,
                       JSONValue::ParseJSONString(
                           "{\"metadata\": {\"info\": {\"id\": \"xyz_123\", "
                           "\"active\": true, \"score\": 99.5}}}"));
  Value json_val = Value::Json(std::move(parsed_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView json_val_ref,
                       VariantValueView::Create(json_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView metadata_ref,
                       json_val_ref.GetKeyValueIfExists("metadata"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView info_ref,
                       metadata_ref.GetKeyValueIfExists("info"));

  EXPECT_THAT(info_ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView id_ref,
                       info_ref.GetKeyValueIfExists("id"));
  EXPECT_THAT(id_ref.IsPrimitive(), true);
  EXPECT_THAT(id_ref.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("xyz_123")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView active_ref,
                       info_ref.GetKeyValueIfExists("active"));
  EXPECT_THAT(active_ref.IsPrimitive(), true);
  EXPECT_THAT(active_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Bool(true)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView score_ref,
                       info_ref.GetKeyValueIfExists("score"));
  EXPECT_THAT(score_ref.IsPrimitive(), true);
  EXPECT_THAT(score_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Double(99.5)));
}

TEST(VariantValueTest, NestedComplexTypesNavigation) {
  TypeFactory factory;

  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &proto_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(types::StringType(), proto_type));

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"nested_map", map_type}}, &struct_type));

  googlesql_test::KitchenSinkPB proto_msg;
  proto_msg.set_int64_key_1(123);
  proto_msg.set_int64_key_2(456);
  proto_msg.set_string_val("proto_val");
  proto_msg.set_date(12345);
  Value proto_val = Value::Proto(proto_type, proto_msg.SerializeAsCord());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::String("key1"), proto_val}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {map_val}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_ref,
                       VariantValueView::Create(struct_val));
  ASSERT_THAT(root_ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView map_ref,
                       root_ref.GetKeyValueIfExists("nested_map"));
  ASSERT_THAT(map_ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView proto_member_ref,
                       map_ref.GetKeyValueIfExists("key1"));
  ASSERT_THAT(proto_member_ref.IsObject(), true);

  // Verify GetKeys for Proto
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> keys,
                       proto_member_ref.GetKeys());
  EXPECT_THAT(keys, testing::Contains("string_val"));
  EXPECT_THAT(keys, testing::Contains("int64_key_1"));

  // Verify GetKeyValue for Proto
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView string_val_ref,
                       proto_member_ref.GetKeyValueIfExists("string_val"));
  EXPECT_THAT(string_val_ref.IsPrimitive(), true);
  EXPECT_THAT(string_val_ref.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("proto_val")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView date_ref,
                       proto_member_ref.GetKeyValueIfExists("date"));
  EXPECT_THAT(date_ref.IsPrimitive(), true);
  EXPECT_THAT(date_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Date(12345)));

  // Verify UnimplementedError for Enum
  EXPECT_THAT(
      proto_member_ref.GetKeyValueIfExists("test_enum"),
      StatusIs(absl::StatusCode::kUnimplemented,
               testing::HasSubstr("Cannot extract nested field 'test_enum' of "
                                  "type 'enum' from Object")));

  // Verify UnimplementedError for Nested Proto
  EXPECT_THAT(
      proto_member_ref.GetKeyValueIfExists("nested_value"),
      StatusIs(absl::StatusCode::kUnimplemented,
               testing::HasSubstr("Cannot extract nested field 'nested_value' "
                                  "of type 'message' from Object")));
}

TEST(VariantValueTest, NestedMixedKeyNavigation) {
  TypeFactory factory;

  // Inner Map: Map<Int64, String>
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* inner_map_type,
      factory.MakeMapType(types::Int64Type(), types::StringType()));

  // Struct: Struct<"inner_map" : Map<Int64, String>>
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"inner_map", inner_map_type}}, &struct_type));

  // Outer Map: Map<String, Struct<...>>
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* outer_map_type,
                       factory.MakeMapType(types::StringType(), struct_type));

  // Inner Map Value
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value inner_map_val,
      Value::MakeMap(inner_map_type,
                     {{Value::Int64(1), Value::String("one")},
                      {Value::Int64(2), Value::String("two")}}));

  // Struct Value
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {inner_map_val}));

  // Outer Map Value
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value outer_map_val,
      Value::MakeMap(outer_map_type, {{Value::String("key1"), struct_val}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(outer_map_val));

  // Outer map is object (String key)
  EXPECT_THAT(ref.IsObject(), true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto struct_member, ref.GetKeyValueIfExists("key1"));

  // Struct is object
  EXPECT_THAT(struct_member.IsObject(), true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto inner_map_member,
                       struct_member.GetKeyValueIfExists("inner_map"));

  // Inner map is NOT object (Int64 key)
  EXPECT_THAT(inner_map_member.IsObject(), false);
  EXPECT_THAT(inner_map_member.IsArray(), false);
  EXPECT_THAT(inner_map_member.IsPrimitive(), false);
  EXPECT_THAT(inner_map_member.IsVariantNull(), false);
  EXPECT_FALSE(inner_map_member.is_null());

  // Operations should fail loudly
  EXPECT_THAT(inner_map_member.GetKeys(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Not an object")));
  EXPECT_THAT(inner_map_member.GetArraySize(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Not an array")));
  EXPECT_THAT(inner_map_member.GetPrimitiveValue(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Value is not a primitive")));
}

TEST(VariantValueTest, UnparsedJsonNestedAccess) {
  Value unparsed_json = Value::UnvalidatedJsonString(
      "{\"nested\": {\"key\": \"value\"}, \"arr\": [42, 43]}");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_ref,
                       VariantValueView::Create(unparsed_json));

  // Extract nested object
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView nested_ref,
                       root_ref.GetKeyValueIfExists("nested"));
  EXPECT_THAT(nested_ref.IsObject(), true);
  EXPECT_THAT(nested_ref.GetKeys(), IsOkAndHolds(ElementsAre("key")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView key_ref,
                       nested_ref.GetKeyValueIfExists("key"));
  EXPECT_THAT(key_ref.IsPrimitive(), true);
  EXPECT_THAT(key_ref.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("value")));

  // Extract array and element
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView arr_ref,
                       root_ref.GetKeyValueIfExists("arr"));
  EXPECT_THAT(arr_ref.IsArray(), true);
  EXPECT_THAT(arr_ref.GetArraySize(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem0, arr_ref.GetElementIfExists(0));
  EXPECT_THAT(elem0.IsPrimitive(), true);
  EXPECT_THAT(elem0.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(42)));
}

TEST(VariantValueTest, UnparsedJsonNestedInStructAndArray) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"json_field", types::JsonType()}},
                                   &struct_type));

  Value unparsed_json = Value::UnvalidatedJsonString("{\"x\": 99}");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {unparsed_json}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_ref,
                       VariantValueView::Create(struct_val));
  EXPECT_THAT(struct_ref.IsObject(), true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_ref,
                       struct_ref.GetKeyValueIfExists("json_field"));
  EXPECT_THAT(field_ref.IsObject(), true);
  EXPECT_THAT(field_ref.GetKeys(), IsOkAndHolds(ElementsAre("x")));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView x_ref,
                       field_ref.GetKeyValueIfExists("x"));
  EXPECT_THAT(x_ref.IsPrimitive(), true);
  EXPECT_THAT(x_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(99)));

  // Array of unparsed JSON
  Value array_val = Value::Array(
      types::JsonArrayType(), {Value::UnvalidatedJsonString("100"),
                               Value::UnvalidatedJsonString("{\"y\": 200}")});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_ref,
                       VariantValueView::Create(array_val));
  EXPECT_THAT(array_ref.IsArray(), true);
  EXPECT_THAT(array_ref.GetArraySize(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem0, array_ref.GetElementIfExists(0));
  EXPECT_THAT(elem0.IsPrimitive(), true);
  EXPECT_THAT(elem0.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(100)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem1, array_ref.GetElementIfExists(1));
  EXPECT_THAT(elem1.IsObject(), true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView y_ref, elem1.GetKeyValueIfExists("y"));
  EXPECT_THAT(y_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(200)));
}

TEST(VariantValueTest, NestedStructWithSqlNullFieldExposedAsVariantNull) {
  TypeFactory factory;
  const StructType* inner_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"val", types::Int64Type()}}, &inner_type));
  const StructType* outer_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"nested_struct", inner_type}}, &outer_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value outer_val,
      Value::MakeStruct(outer_type, {Value::Null(inner_type)}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(outer_val));
  EXPECT_THAT(ref.IsObject(), true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView nested_ref,
                       ref.GetKeyValueIfExists("nested_struct"));
  EXPECT_THAT(nested_ref.IsVariantNull(), true);
  EXPECT_FALSE(nested_ref.is_null());
  EXPECT_THAT(nested_ref.IsObject(), false);
}

TEST(VariantValueTest, ArrayOfSqlNullStructFieldExposedAsVariantNull) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(struct_type, &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {Value::Int64(10)}));

  Value array_val =
      Value::Array(array_type, {Value::Null(struct_type), struct_val});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(array_val));
  EXPECT_THAT(ref.IsArray(), true);
  EXPECT_THAT(ref.GetArraySize(), IsOkAndHolds(2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem0, ref.GetElementIfExists(0));
  EXPECT_THAT(elem0.IsVariantNull(), true);
  EXPECT_FALSE(elem0.is_null());
  EXPECT_THAT(elem0.IsObject(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem1, ref.GetElementIfExists(1));
  EXPECT_THAT(elem1.IsVariantNull(), false);
  EXPECT_THAT(elem1.IsObject(), true);
}

// --- Lifetime and Safety ---

TEST(VariantLifetimeTest, NestedGetKeyValueLifetimeSafety) {
  TypeFactory factory;
  const StructType* inner_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"inner", types::Int64Type()}}, &inner_type));
  const StructType* outer_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"outer", inner_type}}, &outer_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value inner_val,
                       Value::MakeStruct(inner_type, {Value::Int64(42)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value outer_val,
                       Value::MakeStruct(outer_type, {inner_val}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView outer_val_ref,
                       VariantValueView::Create(outer_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView outer_ref,
                       outer_val_ref.GetKeyValueIfExists("outer"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView leaf_ref,
                       outer_ref.GetKeyValueIfExists("inner"));

  EXPECT_THAT(leaf_ref.IsPrimitive(), true);
  EXPECT_THAT(leaf_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(42)));
}

TEST(VariantLifetimeTest, ValueCopyLifetimeIndependence) {
  TypeFactory factory;
  const StructType* type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()}}, &type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(type, {Value::Int64(1)}));
  auto root_val = std::make_unique<Value>(struct_val);

  // VariantValueView holds by value, so it doesn't depend on root_val
  // lifetime in the same way, but we should verify it works.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_val,
                       VariantValueView::Create(*root_val));
  auto ref = std::make_unique<VariantValueView>(std::move(ref_val));

  EXPECT_THAT(ref->IsObject(), true);
  EXPECT_THAT(ref->GetKeys(), IsOkAndHolds(ElementsAre("a")));

  root_val.reset();  // Should still be valid because ref holds by value.

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView member_ref,
                       ref->GetKeyValueIfExists("a"));
  EXPECT_THAT(member_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
}

TEST(VariantLifetimeTest, NestedJsonLifetimeIndependence) {
  std::optional<VariantValueView> child_obj_ref;
  std::optional<VariantValueView> child_arr_elem_ref;

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        JSONValue parsed_json,
        JSONValue::ParseJSONString(
            R"({"nested": {"k": "hello"}, "arr": [{"elem": 42}, 99]})"));
    auto root_val =
        std::make_unique<Value>(Value::Json(std::move(parsed_json)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_view_val,
                         VariantValueView::Create(*root_val));
    auto root_view =
        std::make_unique<VariantValueView>(std::move(root_view_val));

    // Destroy the source Value first; root_view retains ownership.
    root_val.reset();

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView nested_obj,
                         root_view->GetKeyValueIfExists("nested"));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView arr_view,
                         root_view->GetKeyValueIfExists("arr"));

    // Destroy the root VariantValueView; intermediate views retain the root
    // JSON document via reference counting.
    root_view.reset();

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem0,
                         arr_view.GetElementIfExists(0));
    child_obj_ref = std::move(nested_obj);
    child_arr_elem_ref = std::move(elem0);
  }

  // Both root_val, root_view, and intermediate views are now out of scope.
  ASSERT_TRUE(child_obj_ref.has_value());
  EXPECT_TRUE(child_obj_ref->IsObject());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView k_ref,
                       child_obj_ref->GetKeyValueIfExists("k"));
  EXPECT_THAT(k_ref.GetPrimitiveValue(), IsOkAndHolds(Value::String("hello")));

  ASSERT_TRUE(child_arr_elem_ref.has_value());
  EXPECT_TRUE(child_arr_elem_ref->IsObject());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem_val_ref,
                       child_arr_elem_ref->GetKeyValueIfExists("elem"));
  EXPECT_THAT(elem_val_ref.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(42)));
}

// Boundary cases like invalid JSON, empty objects/arrays, etc.

TEST(VariantValueTest, InvalidUnparsedJsonNestedInStruct) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"bad_json", types::JsonType()}}, &struct_type));

  Value invalid_json = Value::UnvalidatedJsonString("{bad json");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {invalid_json}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_ref,
                       VariantValueView::Create(struct_val));
  EXPECT_THAT(struct_ref.IsObject(), true);

  // GetKeyValueIfExists succeeds returning VariantValueView wrapping
  // unparsed JSON
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView bad_ref,
                       struct_ref.GetKeyValueIfExists("bad_json"));

  // Accessors on the bad unparsed JSON fail with parse error
  EXPECT_THAT(bad_ref.IsObject(), false);
  EXPECT_THAT(bad_ref.GetKeys(), StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(VariantValueTest, GetKeyValueMissingVsNull) {
  TypeFactory factory;

  // --- Struct Scenarios ---
  {
    // Scenario A (Missing Key)
    const StructType* struct_type;
    GOOGLESQL_ASSERT_OK(
        factory.MakeStructType({{"a", types::Int64Type()}}, &struct_type));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                         Value::MakeStruct(struct_type, {Value::Int64(1)}));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                         VariantValueView::Create(struct_val));

    EXPECT_THAT(ref.GetKeyValue("missing"),
                IsOkAndHolds(testing::Eq(std::nullopt)));
    EXPECT_THAT(ref.GetKeyValueIfExists("missing"),
                StatusIs(absl::StatusCode::kNotFound));

    // Scenario B (Explicit Null Value)
    const StructType* struct_type_null;
    GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"null_key", types::Int64Type()}},
                                     &struct_type_null));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Value struct_val_null,
        Value::MakeStruct(struct_type_null, {Value::NullInt64()}));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_null,
                         VariantValueView::Create(struct_val_null));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> null_entry,
                         ref_null.GetKeyValue("null_key"));
    ASSERT_TRUE(null_entry.has_value());
    // For Struct, SQL Null is exposed as Variant Null.
    EXPECT_TRUE(null_entry->IsVariantNull());
    EXPECT_FALSE(null_entry->is_null());
  }

  // --- JSON Scenarios ---
  {
    // Scenario A (Missing Key)
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json,
                         JSONValue::ParseJSONString("{\"a\": 1}"));
    Value json_obj = Value::Json(std::move(parsed_json));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                         VariantValueView::Create(json_obj));

    EXPECT_THAT(ref.GetKeyValue("missing"),
                IsOkAndHolds(testing::Eq(std::nullopt)));
    EXPECT_THAT(ref.GetKeyValueIfExists("missing"),
                StatusIs(absl::StatusCode::kNotFound));

    // Scenario B (Explicit Null Value)
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_json_null,
                         JSONValue::ParseJSONString("{\"null_key\": null}"));
    Value json_obj_null = Value::Json(std::move(parsed_json_null));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_null,
                         VariantValueView::Create(json_obj_null));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> null_entry,
                         ref_null.GetKeyValue("null_key"));
    ASSERT_TRUE(null_entry.has_value());
    // For JSON, explicit null is exposed as Variant Null.
    EXPECT_TRUE(null_entry->IsVariantNull());
    EXPECT_FALSE(null_entry->is_null());
  }

  // --- Map Scenarios ---
  {
    // Scenario A (Missing Key)
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const Type* map_type,
        factory.MakeMapType(types::StringType(), types::Int64Type()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Value map_val,
        Value::MakeMap(map_type, {{Value::String("a"), Value::Int64(1)}}));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                         VariantValueView::Create(map_val));

    EXPECT_THAT(ref.GetKeyValue("missing"),
                IsOkAndHolds(testing::Eq(std::nullopt)));
    EXPECT_THAT(ref.GetKeyValueIfExists("missing"),
                StatusIs(absl::StatusCode::kNotFound));

    // Scenario B (Explicit Null Value)
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const Type* map_type_null,
        factory.MakeMapType(types::StringType(), types::Int64Type()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Value map_val_null,
        Value::MakeMap(map_type_null,
                       {{Value::String("null_key"), Value::NullInt64()}}));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref_null,
                         VariantValueView::Create(map_val_null));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> null_entry,
                         ref_null.GetKeyValue("null_key"));
    ASSERT_TRUE(null_entry.has_value());
    // For Map, SQL Null is exposed as Variant Null.
    EXPECT_TRUE(null_entry->IsVariantNull());
    EXPECT_FALSE(null_entry->is_null());
  }

  // --- Proto Scenarios ---
  {
    // Scenario A (Missing Key - Not in descriptor)
    const ProtoType* proto_type;
    GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                    &proto_type));
    googlesql_test::KitchenSinkPB proto_msg;
    proto_msg.set_int64_key_1(0);  // Set required fields
    proto_msg.set_int64_key_2(0);  // Set required fields
    Value proto_val = Value::Proto(proto_type, proto_msg.SerializeAsCord());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                         VariantValueView::Create(proto_val));

    EXPECT_THAT(ref.GetKeyValue("non_existent_field"),
                IsOkAndHolds(testing::Eq(std::nullopt)));
    EXPECT_THAT(ref.GetKeyValueIfExists("non_existent_field"),
                StatusIs(absl::StatusCode::kNotFound));

    // Scenario B (Unset field with default)
    // For Proto, a field in the descriptor that is not set in the message
    // but has defaults enabled (use_defaults=true) returns its default value.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> null_entry,
                         ref.GetKeyValue("int64_val"));
    ASSERT_TRUE(null_entry.has_value());
    EXPECT_FALSE(null_entry->IsVariantNull());
    EXPECT_THAT(null_entry->GetPrimitiveValue(), IsOkAndHolds(Value::Int64(0)));

    // Scenario C (Unset field with defaults disabled)
    // For Proto, a field in the descriptor that is not set in the message
    // and has defaults disabled (use_defaults=false) returns VariantNull.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::optional<VariantValueView> unset_entry,
                         ref.GetKeyValue("int_with_no_default_nullable"));
    ASSERT_TRUE(unset_entry.has_value());
    EXPECT_TRUE(unset_entry->IsVariantNull());
  }
}

TEST(VariantValueTest, EmptyObjectsAndArrays) {
  TypeFactory factory;

  // 1. Parsed empty JSON object
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_empty_obj, JSONValue::ParseJSONString("{}"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView json_obj_ref,
      VariantValueView::Create(Value::Json(std::move(parsed_empty_obj))));
  EXPECT_THAT(json_obj_ref.IsObject(), true);
  EXPECT_THAT(json_obj_ref.IsArray(), false);
  EXPECT_THAT(json_obj_ref.IsPrimitive(), false);
  EXPECT_THAT(json_obj_ref.GetKeys(), IsOkAndHolds(ElementsAre()));
  EXPECT_FALSE(json_obj_ref.HasKey("key"));
  EXPECT_THAT(json_obj_ref.GetKeyValue("key"),
              IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(json_obj_ref.GetKeyValueIfExists("key"),
              StatusIs(absl::StatusCode::kNotFound));

  // 2. Unparsed empty JSON object
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView unparsed_obj_ref,
      VariantValueView::Create(Value::UnvalidatedJsonString("{}")));
  EXPECT_THAT(unparsed_obj_ref.IsObject(), true);
  EXPECT_THAT(unparsed_obj_ref.GetKeys(), IsOkAndHolds(ElementsAre()));
  EXPECT_FALSE(unparsed_obj_ref.HasKey("key"));
  EXPECT_THAT(unparsed_obj_ref.GetKeyValue("key"),
              IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(unparsed_obj_ref.GetKeyValueIfExists("key"),
              StatusIs(absl::StatusCode::kNotFound));

  // 3. Parsed empty JSON array
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_empty_arr, JSONValue::ParseJSONString("[]"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView json_arr_ref,
      VariantValueView::Create(Value::Json(std::move(parsed_empty_arr))));
  EXPECT_THAT(json_arr_ref.IsArray(), true);
  EXPECT_THAT(json_arr_ref.IsObject(), false);
  EXPECT_THAT(json_arr_ref.IsPrimitive(), false);
  EXPECT_THAT(json_arr_ref.GetArraySize(), IsOkAndHolds(0));
  EXPECT_THAT(json_arr_ref.GetElement(0),
              IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(json_arr_ref.GetElementIfExists(0),
              StatusIs(absl::StatusCode::kOutOfRange));

  // 4. Unparsed empty JSON array
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView unparsed_arr_ref,
      VariantValueView::Create(Value::UnvalidatedJsonString("[]")));
  EXPECT_THAT(unparsed_arr_ref.IsArray(), true);
  EXPECT_THAT(unparsed_arr_ref.GetArraySize(), IsOkAndHolds(0));
  EXPECT_THAT(unparsed_arr_ref.GetElement(0),
              IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(unparsed_arr_ref.GetElementIfExists(0),
              StatusIs(absl::StatusCode::kOutOfRange));

  // 5. Empty Map
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_map_val, Value::MakeMap(map_type, {}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView map_ref,
                       VariantValueView::Create(empty_map_val));
  EXPECT_THAT(map_ref.IsObject(), true);
  EXPECT_THAT(map_ref.GetKeys(), IsOkAndHolds(ElementsAre()));
  EXPECT_FALSE(map_ref.HasKey("key"));
  EXPECT_THAT(map_ref.GetKeyValue("key"),
              IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(map_ref.GetKeyValueIfExists("key"),
              StatusIs(absl::StatusCode::kNotFound));

  // 6. Empty Struct
  const StructType* empty_struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({}, &empty_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_struct_val,
                       Value::MakeStruct(empty_struct_type, {}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_ref,
                       VariantValueView::Create(empty_struct_val));
  EXPECT_THAT(struct_ref.IsObject(), true);
  EXPECT_THAT(struct_ref.GetKeys(), IsOkAndHolds(ElementsAre()));
  EXPECT_FALSE(struct_ref.HasKey("key"));
  EXPECT_THAT(struct_ref.GetKeyValue("key"),
              IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(struct_ref.GetKeyValueIfExists("key"),
              StatusIs(absl::StatusCode::kNotFound));

  // 7. Empty GoogleSQL Array
  Value array_val = Value::Array(types::Int64ArrayType(), {});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_ref,
                       VariantValueView::Create(array_val));
  EXPECT_THAT(array_ref.IsArray(), true);
  EXPECT_THAT(array_ref.IsObject(), false);
  EXPECT_THAT(array_ref.IsPrimitive(), false);
  EXPECT_THAT(array_ref.GetArraySize(), IsOkAndHolds(0));
  EXPECT_THAT(array_ref.GetElement(0), IsOkAndHolds(testing::Eq(std::nullopt)));
  EXPECT_THAT(array_ref.GetElementIfExists(0),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(VariantValueTest, GetMembersStruct) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()},
                                    {"b", types::StringType()},
                                    {"a", types::Int64Type()},
                                    {"", types::BoolType()}},
                                   &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type, {Value::Int64(10), Value::NullString(),
                                      Value::Int64(20), Value::Bool(true)}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(struct_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto members, ref.GetMembers());
  ASSERT_EQ(members.size(), 3);

  EXPECT_EQ(members[0].first, "a");
  EXPECT_THAT(members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));

  EXPECT_EQ(members[1].first, "b");
  EXPECT_TRUE(members[1].second.IsVariantNull());

  EXPECT_EQ(members[2].first, "");
  EXPECT_THAT(members[2].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Bool(true)));
}

TEST(VariantValueTest, GetMembersMap) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::NullString(), Value::Int64(99)},
                                {Value::String("k1"), Value::Int64(1)},
                                {Value::String("k2"), Value::NullInt64()}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto members, ref.GetMembers());
  ASSERT_EQ(members.size(), 2);

  EXPECT_EQ(members[0].first, "k1");
  EXPECT_THAT(members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(1)));

  EXPECT_EQ(members[1].first, "k2");
  EXPECT_TRUE(members[1].second.IsVariantNull());
}

TEST(VariantValueTest, GetMembersJson) {
  // Parsed JSON object
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue parsed_json,
      JSONValue::ParseJSONString(R"({"x": 42, "y": null, "z": [1, 2]})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView parsed_ref,
      VariantValueView::Create(Value::Json(std::move(parsed_json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parsed_members, parsed_ref.GetMembers());
  ASSERT_EQ(parsed_members.size(), 3);
  EXPECT_EQ(parsed_members[0].first, "x");
  EXPECT_THAT(parsed_members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(42)));
  EXPECT_EQ(parsed_members[1].first, "y");
  EXPECT_TRUE(parsed_members[1].second.IsVariantNull());
  EXPECT_EQ(parsed_members[2].first, "z");
  EXPECT_TRUE(parsed_members[2].second.IsArray());

  // Unparsed JSON object
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView unparsed_ref,
                       VariantValueView::Create(
                           Value::UnvalidatedJsonString(R"({"k": "val"})")));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto unparsed_members, unparsed_ref.GetMembers());
  ASSERT_EQ(unparsed_members.size(), 1);
  EXPECT_EQ(unparsed_members[0].first, "k");
  EXPECT_THAT(unparsed_members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("val")));
}

TEST(VariantValueTest, GetMembersProto) {
  TypeFactory factory;

  // Proto with primitive and repeated primitive fields
  const ProtoType* inner_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::TestNullabilityInnerPB::descriptor(), &inner_proto_type));
  googlesql_test::TestNullabilityInnerPB inner_msg;
  inner_msg.set_required_field(10);
  inner_msg.add_repeated_field(20);
  inner_msg.add_repeated_field(30);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView inner_ref,
                       VariantValueView::Create(Value::Proto(
                           inner_proto_type, inner_msg.SerializeAsCord())));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto inner_members, inner_ref.GetMembers());
  ASSERT_EQ(inner_members.size(), 3);
  EXPECT_EQ(inner_members[0].first, "required_field");
  EXPECT_THAT(inner_members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));
  EXPECT_EQ(inner_members[1].first, "optional_field");
  EXPECT_THAT(inner_members[1].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(0)));
  EXPECT_EQ(inner_members[2].first, "repeated_field");
  EXPECT_TRUE(inner_members[2].second.IsArray());
  EXPECT_THAT(inner_members[2].second.GetArraySize(), IsOkAndHolds(2));

  // Proto with use_field_defaults = false
  const ProtoType* nulls_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::MessageWithNulls::descriptor(), &nulls_proto_type));
  googlesql_test::MessageWithNulls nulls_msg;
  nulls_msg.set_i1(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView nulls_ref,
                       VariantValueView::Create(Value::Proto(
                           nulls_proto_type, nulls_msg.SerializeAsCord())));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto nulls_members, nulls_ref.GetMembers());
  ASSERT_EQ(nulls_members.size(), 4);
  EXPECT_EQ(nulls_members[0].first, "i1");
  EXPECT_THAT(nulls_members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));
  EXPECT_EQ(nulls_members[1].first, "i2");
  EXPECT_TRUE(nulls_members[1].second.IsVariantNull());
  EXPECT_EQ(nulls_members[2].first, "i3");
  EXPECT_THAT(nulls_members[2].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(0)));
  EXPECT_EQ(nulls_members[3].first, "i4");
  EXPECT_THAT(nulls_members[3].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(6)));

  // Proto with unsupported nested message / enum fields fails GetMembers()
  const ProtoType* ks_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &ks_proto_type));
  googlesql_test::KitchenSinkPB ks_msg;
  ks_msg.set_int64_key_1(1);
  ks_msg.set_int64_key_2(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ks_ref,
                       VariantValueView::Create(Value::Proto(
                           ks_proto_type, ks_msg.SerializeAsCord())));
  EXPECT_THAT(ks_ref.GetMembers(), StatusIs(absl::StatusCode::kUnimplemented));

  // Empty proto with zero fields returns empty members vector.
  const ProtoType* empty_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::EmptyMessage::descriptor(),
                                  &empty_proto_type));
  googlesql_test::EmptyMessage empty_msg;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView empty_ref,
                       VariantValueView::Create(Value::Proto(
                           empty_proto_type, empty_msg.SerializeAsCord())));
  EXPECT_THAT(empty_ref.GetMembers(), IsOkAndHolds(testing::IsEmpty()));
}

TEST(VariantValueTest, GetMembersNestedComplexTypes) {
  TypeFactory factory;

  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::TestNullabilityInnerPB::descriptor(), &proto_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(types::StringType(), proto_type));

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"nested_map", map_type},
                                    {"nested_json", types::JsonType()},
                                    {"nested_array", types::Int64ArrayType()}},
                                   &struct_type));

  googlesql_test::TestNullabilityInnerPB proto_msg;
  proto_msg.set_required_field(123);
  proto_msg.add_repeated_field(456);
  Value proto_val = Value::Proto(proto_type, proto_msg.SerializeAsCord());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::String("key1"), proto_val}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue parsed_json,
      JSONValue::ParseJSONString(
          R"({"inner_obj": {"k": "v"}, "inner_arr": [1, 2]})"));
  Value json_val = Value::Json(std::move(parsed_json));

  Value array_val = Value::Array(types::Int64ArrayType(),
                                 {Value::Int64(10), Value::Int64(20)});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type, {map_val, json_val, array_val}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_ref,
                       VariantValueView::Create(struct_val));
  ASSERT_TRUE(root_ref.IsObject());

  // Top-level GetMembers() only unpacks the immediate Struct fields.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto root_members, root_ref.GetMembers());
  ASSERT_EQ(root_members.size(), 3);
  EXPECT_EQ(root_members[0].first, "nested_map");
  EXPECT_TRUE(root_members[0].second.IsObject());
  EXPECT_EQ(root_members[1].first, "nested_json");
  EXPECT_TRUE(root_members[1].second.IsObject());
  EXPECT_EQ(root_members[2].first, "nested_array");
  EXPECT_TRUE(root_members[2].second.IsArray());
  EXPECT_FALSE(root_members[2].second.IsObject());
  EXPECT_THAT(root_members[2].second.GetMembers(),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(root_members[2].second.GetArraySize(), IsOkAndHolds(2));

  // Unpack nested Map<String, Proto> member.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto map_members, root_members[0].second.GetMembers());
  ASSERT_EQ(map_members.size(), 1);
  EXPECT_EQ(map_members[0].first, "key1");
  EXPECT_TRUE(map_members[0].second.IsObject());

  // Unpack nested Proto member inside the Map.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto proto_members, map_members[0].second.GetMembers());
  ASSERT_EQ(proto_members.size(), 3);
  EXPECT_EQ(proto_members[0].first, "required_field");
  EXPECT_THAT(proto_members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(123)));
  EXPECT_EQ(proto_members[1].first, "optional_field");
  EXPECT_THAT(proto_members[1].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(0)));
  EXPECT_EQ(proto_members[2].first, "repeated_field");
  EXPECT_TRUE(proto_members[2].second.IsArray());

  // Unpack nested JSON object member.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto json_members, root_members[1].second.GetMembers());
  ASSERT_EQ(json_members.size(), 2);
  EXPECT_EQ(json_members[0].first, "inner_arr");
  EXPECT_TRUE(json_members[0].second.IsArray());
  EXPECT_EQ(json_members[1].first, "inner_obj");
  EXPECT_TRUE(json_members[1].second.IsObject());

  // Unpack inner JSON object inside the JSON object.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto inner_json_members,
                       json_members[1].second.GetMembers());
  ASSERT_EQ(inner_json_members.size(), 1);
  EXPECT_EQ(inner_json_members[0].first, "k");
  EXPECT_THAT(inner_json_members[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("v")));
}

TEST(VariantValueTest, GetMembersNonObjectErrors) {
  TypeFactory factory;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_ref,
                       VariantValueView::Create(Value::Int64(42)));
  EXPECT_THAT(int_ref.GetMembers(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView arr_ref,
                       VariantValueView::Create(Value::Array(
                           types::Int64ArrayType(), {Value::Int64(1)})));
  EXPECT_THAT(arr_ref.GetMembers(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* int_map_type,
      factory.MakeMapType(types::Int64Type(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value int_map_val,
      Value::MakeMap(int_map_type, {{Value::Int64(1), Value::String("a")}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_map_ref,
                       VariantValueView::Create(int_map_val));
  EXPECT_THAT(int_map_ref.GetMembers(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_ref,
                       VariantValueView::Create(Value::NullInt64()));
  EXPECT_THAT(null_ref.GetMembers(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VariantValueTest, ForMembersStruct) {
  TypeFactory factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()},
                                    {"b", types::StringType()},
                                    {"a", types::Int64Type()},
                                    {"", types::BoolType()}},
                                   &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type, {Value::Int64(10), Value::NullString(),
                                      Value::Int64(20), Value::Bool(true)}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(struct_val));
  std::vector<std::pair<std::string, VariantValueView>> visited;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view key, VariantValueView val) {
    visited.emplace_back(std::string(key), std::move(val));
    return absl::OkStatus();
  }));

  ASSERT_EQ(visited.size(), 3);
  EXPECT_EQ(visited[0].first, "a");
  EXPECT_THAT(visited[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));
  EXPECT_EQ(visited[1].first, "b");
  EXPECT_TRUE(visited[1].second.IsVariantNull());
  EXPECT_EQ(visited[2].first, "");
  EXPECT_THAT(visited[2].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Bool(true)));
}

TEST(VariantValueTest, ForMembersMap) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_type, {{Value::NullString(), Value::Int64(99)},
                                {Value::String("k1"), Value::Int64(1)},
                                {Value::String("k2"), Value::NullInt64()}}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref, VariantValueView::Create(map_val));
  std::vector<std::pair<std::string, VariantValueView>> visited;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view key, VariantValueView val) {
    visited.emplace_back(std::string(key), std::move(val));
    return absl::OkStatus();
  }));

  ASSERT_EQ(visited.size(), 2);
  EXPECT_EQ(visited[0].first, "k1");
  EXPECT_THAT(visited[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(1)));
  EXPECT_EQ(visited[1].first, "k2");
  EXPECT_TRUE(visited[1].second.IsVariantNull());
}

TEST(VariantValueTest, ForMembersParsedJson) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue parsed_json,
      JSONValue::ParseJSONString(R"({"x": 42, "y": null, "z": [1, 2]})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView ref,
      VariantValueView::Create(Value::Json(std::move(parsed_json))));

  std::vector<std::pair<std::string, VariantValueView>> visited;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view key, VariantValueView val) {
    visited.emplace_back(std::string(key), std::move(val));
    return absl::OkStatus();
  }));

  ASSERT_EQ(visited.size(), 3);
  EXPECT_EQ(visited[0].first, "x");
  EXPECT_THAT(visited[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(42)));
  EXPECT_EQ(visited[1].first, "y");
  EXPECT_TRUE(visited[1].second.IsVariantNull());
  EXPECT_EQ(visited[2].first, "z");
  EXPECT_TRUE(visited[2].second.IsArray());
}

TEST(VariantValueTest, ForMembersNestedCallback) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue parsed_json,
      JSONValue::ParseJSONString(
          R"({"x": 42, "y": null, "z": [1, 2], "child": {"a": 1, "b": "val"}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView ref,
      VariantValueView::Create(Value::Json(std::move(parsed_json))));

  std::vector<std::string> visited_keys;
  std::vector<std::pair<std::string, VariantValueView>> visited_child;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&visited_keys, &visited_child](
                               absl::string_view key,
                               VariantValueView value) -> absl::Status {
    visited_keys.emplace_back(key);
    if (key == "child") {
      return value.ForMembers([&visited_child](absl::string_view child_key,
                                               VariantValueView child_value) {
        visited_child.emplace_back(std::string(child_key),
                                   std::move(child_value));
        return absl::OkStatus();
      });
    }
    return absl::OkStatus();
  }));

  EXPECT_THAT(visited_keys, ElementsAre("child", "x", "y", "z"));
  ASSERT_EQ(visited_child.size(), 2);
  EXPECT_EQ(visited_child[0].first, "a");
  EXPECT_THAT(visited_child[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(1)));
  EXPECT_EQ(visited_child[1].first, "b");
  EXPECT_THAT(visited_child[1].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("val")));
}

TEST(VariantValueTest, ForMembersUnparsedJson) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(
                           Value::UnvalidatedJsonString(R"({"k": "val"})")));

  std::vector<std::pair<std::string, VariantValueView>> visited;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view key, VariantValueView val) {
    visited.emplace_back(std::string(key), std::move(val));
    return absl::OkStatus();
  }));

  ASSERT_EQ(visited.size(), 1);
  EXPECT_EQ(visited[0].first, "k");
  EXPECT_THAT(visited[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("val")));
}

TEST(VariantValueTest, ForMembersProto) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::TestNullabilityInnerPB::descriptor(), &proto_type));
  googlesql_test::TestNullabilityInnerPB msg;
  msg.set_required_field(10);
  msg.add_repeated_field(20);
  msg.add_repeated_field(30);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(
                           Value::Proto(proto_type, msg.SerializeAsCord())));

  std::vector<std::pair<std::string, VariantValueView>> visited;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view key, VariantValueView val) {
    visited.emplace_back(std::string(key), std::move(val));
    return absl::OkStatus();
  }));

  ASSERT_EQ(visited.size(), 3);
  EXPECT_EQ(visited[0].first, "required_field");
  EXPECT_THAT(visited[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));
  EXPECT_EQ(visited[1].first, "optional_field");
  EXPECT_THAT(visited[1].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(0)));
  EXPECT_EQ(visited[2].first, "repeated_field");
  EXPECT_TRUE(visited[2].second.IsArray());
  EXPECT_THAT(visited[2].second.GetArraySize(), IsOkAndHolds(2));
}

TEST(VariantValueTest, ForMembersProtoWithNulls) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::MessageWithNulls::descriptor(), &proto_type));
  googlesql_test::MessageWithNulls msg;
  msg.set_i1(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(
                           Value::Proto(proto_type, msg.SerializeAsCord())));

  std::vector<std::pair<std::string, VariantValueView>> visited;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view key, VariantValueView val) {
    visited.emplace_back(std::string(key), std::move(val));
    return absl::OkStatus();
  }));

  ASSERT_EQ(visited.size(), 4);
  EXPECT_EQ(visited[0].first, "i1");
  EXPECT_THAT(visited[0].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));
  EXPECT_EQ(visited[1].first, "i2");
  EXPECT_TRUE(visited[1].second.IsVariantNull());
  EXPECT_EQ(visited[2].first, "i3");
  EXPECT_THAT(visited[2].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(0)));
  EXPECT_EQ(visited[3].first, "i4");
  EXPECT_THAT(visited[3].second.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(6)));
}

TEST(VariantValueTest, ForMembersEmptyProto) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::EmptyMessage::descriptor(),
                                  &proto_type));
  googlesql_test::EmptyMessage msg;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(
                           Value::Proto(proto_type, msg.SerializeAsCord())));

  int call_count = 0;
  GOOGLESQL_ASSERT_OK(ref.ForMembers([&](absl::string_view, VariantValueView) {
    ++call_count;
    return absl::OkStatus();
  }));
  EXPECT_EQ(call_count, 0);
}

TEST(VariantValueTest, ForMembersUnsupportedProtoFields) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &proto_type));
  googlesql_test::KitchenSinkPB msg;
  msg.set_int64_key_1(1);
  msg.set_int64_key_2(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(
                           Value::Proto(proto_type, msg.SerializeAsCord())));

  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(VariantValueTest, ForMembersStopsOnCallbackError) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue parsed_json,
      JSONValue::ParseJSONString(R"({"a": 1, "b": 2, "c": 3})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView ref,
      VariantValueView::Create(Value::Json(std::move(parsed_json))));

  std::vector<std::string> visited_keys;
  absl::Status status =
      ref.ForMembers([&](absl::string_view key, VariantValueView) {
        visited_keys.emplace_back(key);
        if (key == "b") {
          return absl::AbortedError("Stop at b");
        }
        return absl::OkStatus();
      });

  EXPECT_THAT(status, StatusIs(absl::StatusCode::kAborted, "Stop at b"));
  EXPECT_THAT(visited_keys, ElementsAre("a", "b"));
}

TEST(VariantValueTest, ForMembersRejectsNonStringKeyedMap) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* int_map_type,
      factory.MakeMapType(types::Int64Type(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value int_map_val,
      Value::MakeMap(int_map_type, {{Value::Int64(1), Value::String("a")}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(int_map_val));

  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VariantValueTest, ForMembersRejectsMalformedUnparsedJson) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView ref,
      VariantValueView::Create(Value::UnvalidatedJsonString("{\"a\": 1")));

  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kOutOfRange));
}

class VariantValueForMembersNonObjectTest
    : public ::testing::TestWithParam<Value> {};

TEST_P(VariantValueForMembersNonObjectTest, RejectsNonObject) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(GetParam()));
  EXPECT_THAT(ref.ForMembers([](absl::string_view, VariantValueView) {
    return absl::OkStatus();
  }),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

INSTANTIATE_TEST_SUITE_P(
    NonObjects, VariantValueForMembersNonObjectTest,
    ::testing::Values(Value::Int64(42),
                      Value::Array(types::Int64ArrayType(), {Value::Int64(1)}),
                      Value::UnvalidatedJsonString("[1, 2]"),
                      Value::NullInt64(), Value::NullVariant(),
                      Value::Json(JSONValue())));

TEST(VariantValueTest, GetElementsSqlArray) {
  Value array_val =
      Value::Array(types::Int64ArrayType(),
                   {Value::Int64(10), Value::NullInt64(), Value::Int64(30)});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView ref,
                       VariantValueView::Create(array_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<VariantValueView> elements,
                       ref.GetElements());
  ASSERT_EQ(elements.size(), 3);
  EXPECT_THAT(elements[0].GetPrimitiveValue(), IsOkAndHolds(Value::Int64(10)));
  EXPECT_TRUE(elements[1].IsVariantNull());
  EXPECT_THAT(elements[2].GetPrimitiveValue(), IsOkAndHolds(Value::Int64(30)));
}

TEST(VariantValueTest, GetElementsJsonArray) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue parsed_json,
                       JSONValue::ParseJSONString(R"([1, null, {"a": 2}])"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView parsed_ref,
      VariantValueView::Create(Value::Json(std::move(parsed_json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<VariantValueView> parsed_elements,
                       parsed_ref.GetElements());
  ASSERT_EQ(parsed_elements.size(), 3);
  EXPECT_THAT(parsed_elements[0].GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(1)));
  EXPECT_TRUE(parsed_elements[1].IsVariantNull());
  EXPECT_TRUE(parsed_elements[2].IsObject());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView unparsed_ref,
      VariantValueView::Create(Value::UnvalidatedJsonString(R"(["x", "y"])")));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<VariantValueView> unparsed_elements,
                       unparsed_ref.GetElements());
  ASSERT_EQ(unparsed_elements.size(), 2);
  EXPECT_THAT(unparsed_elements[0].GetPrimitiveValue(),
              IsOkAndHolds(Value::String("x")));
  EXPECT_THAT(unparsed_elements[1].GetPrimitiveValue(),
              IsOkAndHolds(Value::String("y")));
}

TEST(VariantValueTest, GetElementsNonArrayErrors) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_ref,
                       VariantValueView::Create(Value::Int64(42)));
  EXPECT_THAT(int_ref.GetElements(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView obj_ref,
      VariantValueView::Create(Value::UnvalidatedJsonString(R"({"a": 1})")));
  EXPECT_THAT(obj_ref.GetElements(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_ref,
                       VariantValueView::Create(Value::NullInt64()));
  EXPECT_THAT(null_ref.GetElements(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

absl::StatusOr<std::string> GetViewDebugString(Value value) {
  absl::StatusOr<VariantValueView> view =
      VariantValueView::Create(std::move(value));
  if (!view.ok()) {
    return view.status();
  }
  return view->DebugString();
}

TEST(VariantValueTest, DebugStringPrimitivesAndNulls) {
  // Invalid value
  EXPECT_THAT(GetViewDebugString(Value()), IsOkAndHolds("Uninitialized value"));

  // SQL NULL
  EXPECT_THAT(GetViewDebugString(Value::NullInt64()), IsOkAndHolds("NULL"));
  EXPECT_EQ(Value::Null(types::VariantType()).DebugString(), "NULL");

  // Variant Null (JSON null)
  Value variant_null = Value::Json(JSONValue());
  EXPECT_THAT(GetViewDebugString(variant_null), IsOkAndHolds("null"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value wrapped_variant_null,
                       Value::Variant(variant_null));
  EXPECT_EQ(wrapped_variant_null.DebugString(), "null");

  // Primitives
  EXPECT_THAT(GetViewDebugString(Value::Int64(42)), IsOkAndHolds("42"));
  EXPECT_THAT(GetViewDebugString(Value::String("hello")),
              IsOkAndHolds("\"hello\""));
  EXPECT_THAT(GetViewDebugString(Value::Bool(true)), IsOkAndHolds("true"));

  // JSON primitives (parsed and unparsed) are formatted as SQL primitives
  EXPECT_THAT(GetViewDebugString(Value::UnvalidatedJsonString("\"hello\"")),
              IsOkAndHolds("\"hello\""));
  EXPECT_THAT(GetViewDebugString(Value::UnvalidatedJsonString("123")),
              IsOkAndHolds("123"));
  EXPECT_THAT(GetViewDebugString(Value::UnvalidatedJsonString("null")),
              IsOkAndHolds("null"));
}

TEST(VariantValueTest, DebugStringObjectsAndArrays) {
  TypeFactory factory;

  // Struct formatted as Simple View Object: {"a": 1, "b": "two", "c": null}
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"b", types::StringType()},
                                    {"a", types::Int64Type()},
                                    {"c", types::Int64Type()}},
                                   &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type, {Value::String("two"), Value::Int64(1),
                                      Value::NullInt64()}));
  EXPECT_THAT(GetViewDebugString(struct_val),
              IsOkAndHolds(R"({"a": 1, "b": "two", "c": null})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_struct, Value::Variant(struct_val));
  EXPECT_EQ(variant_struct.DebugString(), R"({"a": 1, "b": "two", "c": null})");

  // Struct with duplicate field names and anonymous field
  const StructType* dup_struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"x", types::Int64Type()},
                                    {"x", types::Int64Type()},
                                    {"", types::StringType()}},
                                   &dup_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value dup_struct_val,
      Value::MakeStruct(dup_struct_type, {Value::Int64(10), Value::Int64(20),
                                          Value::String("anon")}));
  EXPECT_THAT(GetViewDebugString(dup_struct_val),
              IsOkAndHolds(R"({"": "anon", "x": 10})"));

  // Empty Struct
  const StructType* empty_struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({}, &empty_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_struct_val,
                       Value::MakeStruct(empty_struct_type, {}));
  EXPECT_THAT(GetViewDebugString(empty_struct_val), IsOkAndHolds("{}"));

  // Map<String, V> with SQL NULL key (skipped) and SQL NULL value (null)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* str_map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value str_map_val,
      Value::MakeMap(str_map_type,
                     {{Value::NullString(), Value::Int64(99)},
                      {Value::String("k1"), Value::Int64(1)},
                      {Value::String("k2"), Value::NullInt64()}}));
  EXPECT_THAT(GetViewDebugString(str_map_val),
              IsOkAndHolds(R"({"k1": 1, "k2": null})"));

  // JSON object and array
  Value json_val = Value::UnvalidatedJsonString(
      R"({"arr": [1, null, {"nested": "ok"}], "flag": true})");
  EXPECT_THAT(
      GetViewDebugString(json_val),
      IsOkAndHolds(R"({"arr": [1, null, {"nested": "ok"}], "flag": true})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_json, Value::Variant(json_val));
  EXPECT_EQ(variant_json.DebugString(),
            R"({"arr": [1, null, {"nested": "ok"}], "flag": true})");

  // Array of Structs with SQL NULL element
  const ArrayType* struct_array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(struct_type, &struct_array_type));
  Value struct_array_val =
      Value::Array(struct_array_type, {struct_val, Value::Null(struct_type)});
  EXPECT_THAT(GetViewDebugString(struct_array_val),
              IsOkAndHolds(R"([{"a": 1, "b": "two", "c": null}, null])"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_array, Value::Variant(struct_array_val));
  EXPECT_EQ(variant_array.DebugString(),
            R"([{"a": 1, "b": "two", "c": null}, null])");

  // Proto with primitive and repeated primitive fields
  const ProtoType* inner_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::TestNullabilityInnerPB::descriptor(), &inner_proto_type));
  googlesql_test::TestNullabilityInnerPB inner_msg;
  inner_msg.set_required_field(10);
  inner_msg.add_repeated_field(20);
  inner_msg.add_repeated_field(30);
  Value inner_proto_val =
      Value::Proto(inner_proto_type, inner_msg.SerializeAsCord());
  EXPECT_THAT(
      GetViewDebugString(inner_proto_val),
      IsOkAndHolds(R"({"optional_field": 0, "repeated_field": [20, 30], )"
                   R"("required_field": 10})"));

  // Proto with use_field_defaults = false
  const ProtoType* nulls_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::MessageWithNulls::descriptor(), &nulls_proto_type));
  googlesql_test::MessageWithNulls nulls_msg;
  nulls_msg.set_i1(10);
  Value nulls_proto_val =
      Value::Proto(nulls_proto_type, nulls_msg.SerializeAsCord());
  EXPECT_THAT(GetViewDebugString(nulls_proto_val),
              IsOkAndHolds(R"({"i1": 10, "i2": null, "i3": 0, "i4": 6})"));
}

TEST(VariantValueTest, DebugStringNonSimpleViewFallback) {
  TypeFactory factory;

  // Map with non-string keys is not a Simple View Object/Array/Primitive and
  // delegates directly to Value::DebugString().
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* int_map_type,
      factory.MakeMapType(types::Int64Type(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value int_map_val,
      Value::MakeMap(int_map_type, {{Value::Int64(1), Value::String("one")},
                                    {Value::Int64(2), Value::NullString()}}));
  EXPECT_THAT(GetViewDebugString(int_map_val),
              IsOkAndHolds(int_map_val.DebugString()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_int_map, Value::Variant(int_map_val));
  EXPECT_EQ(variant_int_map.DebugString(), int_map_val.DebugString());

  // Range is not a Simple View Object/Array/Primitive and delegates directly
  // to Value::DebugString().
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_val,
                       Value::MakeRange(Value::Date(1), Value::Date(2)));
  EXPECT_THAT(GetViewDebugString(range_val),
              IsOkAndHolds(range_val.DebugString()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_range, Value::Variant(range_val));
  EXPECT_EQ(variant_range.DebugString(), range_val.DebugString());

  // Corrupted unparsed JSON object and array fail view extraction and fall
  // back to Value::DebugString().
  Value bad_json_obj = Value::UnvalidatedJsonString("{\"a\": 1");
  EXPECT_THAT(GetViewDebugString(bad_json_obj),
              IsOkAndHolds(bad_json_obj.DebugString()));
  Value bad_json_arr = Value::UnvalidatedJsonString("[1, 2");
  EXPECT_THAT(GetViewDebugString(bad_json_arr),
              IsOkAndHolds(bad_json_arr.DebugString()));

  // Proto with unsupported nested message/enum fields fails GetMembers() and
  // falls back to Value::DebugString().
  const ProtoType* ks_proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &ks_proto_type));
  googlesql_test::KitchenSinkPB ks_msg;
  ks_msg.set_int64_key_1(1);
  ks_msg.set_int64_key_2(2);
  Value ks_proto_val = Value::Proto(ks_proto_type, ks_msg.SerializeAsCord());
  EXPECT_THAT(GetViewDebugString(ks_proto_val),
              IsOkAndHolds(ks_proto_val.DebugString()));
}

TEST(VariantValueTest, ToVariantValueFromStructVariantValueView) {
  TypeFactory factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {Value::Int64(42)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_variant, Value::Variant(struct_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_view,
                       root_variant.variant_value());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value roundtrip_root, root_view.ToVariantValue());
  EXPECT_TRUE(roundtrip_root.type()->IsVariant());
  EXPECT_FALSE(roundtrip_root.is_null());
  EXPECT_EQ(roundtrip_root, root_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_root_view,
                       roundtrip_root.variant_value());
  EXPECT_TRUE(roundtrip_root_view.IsObject());
  EXPECT_FALSE(roundtrip_root_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_root_view.IsArray());
  EXPECT_FALSE(roundtrip_root_view.IsVariantNull());
  EXPECT_EQ(roundtrip_root_view.DebugString(), R"({"a": 42})");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_view,
                       root_view.GetKeyValueIfExists("a"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value field_variant, field_view.ToVariantValue());
  EXPECT_TRUE(field_variant.type()->IsVariant());
  EXPECT_FALSE(field_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_field_variant,
                       Value::Variant(Value::Int64(42)));
  EXPECT_EQ(field_variant, expected_field_variant);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_field_view,
                       field_variant.variant_value());
  EXPECT_TRUE(roundtrip_field_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_field_view.IsObject());
  EXPECT_FALSE(roundtrip_field_view.IsArray());
  EXPECT_FALSE(roundtrip_field_view.IsVariantNull());
  EXPECT_THAT(roundtrip_field_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(42)));
  EXPECT_EQ(roundtrip_field_view.DebugString(), "42");
}

TEST(VariantValueTest, ToVariantValueFromStructWithComplexFields) {
  TypeFactory factory;
  const StructType* nested_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"x", types::Int64Type()}}, &nested_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value nested_struct_val,
      Value::MakeStruct(nested_struct_type, {Value::Int64(10)}));

  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(types::Int64Type(), &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_val,
      Value::MakeArray(array_type, {Value::Int64(1), Value::Int64(2)}));

  const StructType* outer_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"nested_struct", nested_struct_type}, {"nested_array", array_type}},
      &outer_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value outer_struct_val,
      Value::MakeStruct(outer_struct_type, {nested_struct_val, array_val}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_variant, Value::Variant(outer_struct_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_view,
                       root_variant.variant_value());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_field_view,
                       root_view.GetKeyValueIfExists("nested_struct"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_field_variant,
                       struct_field_view.ToVariantValue());
  EXPECT_TRUE(struct_field_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_nested_struct_variant,
                       Value::Variant(nested_struct_val));
  EXPECT_EQ(struct_field_variant, expected_nested_struct_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_struct_view,
                       struct_field_variant.variant_value());
  EXPECT_TRUE(roundtrip_struct_view.IsObject());
  EXPECT_FALSE(roundtrip_struct_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_struct_view.IsArray());
  EXPECT_EQ(roundtrip_struct_view.DebugString(), R"({"x": 10})");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_field_view,
                       root_view.GetKeyValueIfExists("nested_array"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value array_field_variant,
                       array_field_view.ToVariantValue());
  EXPECT_TRUE(array_field_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_array_variant, Value::Variant(array_val));
  EXPECT_EQ(array_field_variant, expected_array_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_array_view,
                       array_field_variant.variant_value());
  EXPECT_TRUE(roundtrip_array_view.IsArray());
  EXPECT_FALSE(roundtrip_array_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_array_view.IsObject());
  EXPECT_EQ(roundtrip_array_view.DebugString(), "[1, 2]");
}

TEST(VariantValueTest, ToVariantValueFromProto) {
  TypeFactory factory;
  const ProtoType* nulls_proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(
      googlesql_test::MessageWithNulls::descriptor(), &nulls_proto_type));
  googlesql_test::MessageWithNulls nulls_msg;
  nulls_msg.set_i1(10);
  Value proto_val = Value::Proto(nulls_proto_type, nulls_msg.SerializeAsCord());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView proto_view,
                       VariantValueView::Create(proto_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value proto_variant, proto_view.ToVariantValue());
  EXPECT_TRUE(proto_variant.type()->IsVariant());
  EXPECT_FALSE(proto_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_proto_variant, Value::Variant(proto_val));
  EXPECT_EQ(proto_variant, expected_proto_variant);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_proto_view,
                       proto_variant.variant_value());
  EXPECT_TRUE(roundtrip_proto_view.IsObject());
  EXPECT_FALSE(roundtrip_proto_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_proto_view.IsArray());
  EXPECT_FALSE(roundtrip_proto_view.IsVariantNull());
  EXPECT_EQ(roundtrip_proto_view.DebugString(), proto_view.DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView set_field_view,
                       proto_view.GetKeyValueIfExists("i1"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value set_field_variant,
                       set_field_view.ToVariantValue());
  EXPECT_TRUE(set_field_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_i1_variant,
                       Value::Variant(Value::Int64(10)));
  EXPECT_EQ(set_field_variant, expected_i1_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_i1_view,
                       set_field_variant.variant_value());
  EXPECT_TRUE(roundtrip_i1_view.IsPrimitive());
  EXPECT_THAT(roundtrip_i1_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(10)));
  EXPECT_EQ(roundtrip_i1_view.DebugString(), "10");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView unset_null_field_view,
                       proto_view.GetKeyValueIfExists("i2"));
  EXPECT_TRUE(unset_null_field_view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value unset_null_field_variant,
                       unset_null_field_view.ToVariantValue());
  EXPECT_TRUE(unset_null_field_variant.type()->IsVariant());
  EXPECT_FALSE(unset_null_field_variant.is_null());
  EXPECT_EQ(unset_null_field_variant, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_i2_view,
                       unset_null_field_variant.variant_value());
  EXPECT_TRUE(roundtrip_i2_view.IsVariantNull());
  EXPECT_EQ(roundtrip_i2_view.DebugString(), "null");
}

TEST(VariantValueTest, ToVariantValueFromMapAndRange) {
  TypeFactory factory;

  // String-keyed MAP (Object in Simple View)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* str_map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value str_map_val,
      Value::MakeMap(str_map_type, {{Value::String("a"), Value::Int64(1)},
                                    {Value::String("b"), Value::NullInt64()}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView str_map_view,
                       VariantValueView::Create(str_map_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value str_map_variant, str_map_view.ToVariantValue());
  EXPECT_TRUE(str_map_variant.type()->IsVariant());
  EXPECT_FALSE(str_map_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_str_map_variant,
                       Value::Variant(str_map_val));
  EXPECT_EQ(str_map_variant, expected_str_map_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_str_map_view,
                       str_map_variant.variant_value());
  EXPECT_TRUE(roundtrip_str_map_view.IsObject());
  EXPECT_EQ(roundtrip_str_map_view.DebugString(), R"({"a": 1, "b": null})");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView map_null_val_view,
                       str_map_view.GetKeyValueIfExists("b"));
  EXPECT_TRUE(map_null_val_view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value map_null_val_variant,
                       map_null_val_view.ToVariantValue());
  EXPECT_TRUE(map_null_val_variant.type()->IsVariant());
  EXPECT_FALSE(map_null_val_variant.is_null());
  EXPECT_EQ(map_null_val_variant, Value::VariantNull());

  // Non-string-keyed MAP (outside Simple View categories)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* int_map_type,
      factory.MakeMapType(types::Int64Type(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value int_map_val,
      Value::MakeMap(int_map_type, {{Value::Int64(1), Value::String("one")}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_map_view,
                       VariantValueView::Create(int_map_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value int_map_variant, int_map_view.ToVariantValue());
  EXPECT_TRUE(int_map_variant.type()->IsVariant());
  EXPECT_FALSE(int_map_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_int_map_variant,
                       Value::Variant(int_map_val));
  EXPECT_EQ(int_map_variant, expected_int_map_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_int_map_view,
                       int_map_variant.variant_value());
  EXPECT_FALSE(roundtrip_int_map_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_int_map_view.IsObject());
  EXPECT_FALSE(roundtrip_int_map_view.IsArray());
  EXPECT_FALSE(roundtrip_int_map_view.IsVariantNull());
  EXPECT_EQ(roundtrip_int_map_view.DebugString(), int_map_view.DebugString());

  // RANGE (outside Simple View categories)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_val,
                       Value::MakeRange(Value::Date(1), Value::Date(2)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView range_view,
                       VariantValueView::Create(range_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_variant, range_view.ToVariantValue());
  EXPECT_TRUE(range_variant.type()->IsVariant());
  EXPECT_FALSE(range_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_range_variant, Value::Variant(range_val));
  EXPECT_EQ(range_variant, expected_range_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_range_view,
                       range_variant.variant_value());
  EXPECT_FALSE(roundtrip_range_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_range_view.IsObject());
  EXPECT_FALSE(roundtrip_range_view.IsArray());
  EXPECT_FALSE(roundtrip_range_view.IsVariantNull());
  EXPECT_EQ(roundtrip_range_view.DebugString(), range_view.DebugString());
}

TEST(VariantValueTest, ToVariantValueFromJsonVariantValueView) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json,
                       JSONValue::ParseJSONString(R"({"a": 42})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_variant,
                       Value::Variant(Value::Json(std::move(json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_view,
                       root_variant.variant_value());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_view,
                       root_view.GetKeyValueIfExists("a"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value field_variant, field_view.ToVariantValue());
  EXPECT_TRUE(field_variant.type()->IsVariant());
  EXPECT_FALSE(field_variant.is_null());

  // JSON sub-nodes materialize as TYPE_JSON inside
  // VARIANT (distinct from INT64 inside VARIANT).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expected_json,
                       JSONValue::ParseJSONString("42"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_json_variant,
                       Value::Variant(Value::Json(std::move(expected_json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value int_variant, Value::Variant(Value::Int64(42)));
  EXPECT_EQ(field_variant, expected_json_variant);
  EXPECT_NE(field_variant, int_variant);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_field_view,
                       field_variant.variant_value());
  EXPECT_TRUE(roundtrip_field_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_field_view.IsObject());
  EXPECT_FALSE(roundtrip_field_view.IsArray());
  EXPECT_FALSE(roundtrip_field_view.IsVariantNull());
  EXPECT_THAT(roundtrip_field_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(42)));
  EXPECT_EQ(roundtrip_field_view.DebugString(), "42");
}

TEST(VariantValueTest, ToVariantValueFromJsonSubObject) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json, JSONValue::ParseJSONString(
                                           R"({"obj": {"nested": "value"}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_variant,
                       Value::Variant(Value::Json(std::move(json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_view,
                       root_variant.variant_value());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_view,
                       root_view.GetKeyValueIfExists("obj"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value field_variant, field_view.ToVariantValue());
  EXPECT_TRUE(field_variant.type()->IsVariant());
  EXPECT_FALSE(field_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expected_sub_obj,
                       JSONValue::ParseJSONString(R"({"nested": "value"})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_variant,
      Value::Variant(Value::Json(std::move(expected_sub_obj))));
  EXPECT_EQ(field_variant, expected_variant);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_view,
                       field_variant.variant_value());
  EXPECT_TRUE(roundtrip_view.IsObject());
  EXPECT_FALSE(roundtrip_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_view.IsArray());
  EXPECT_FALSE(roundtrip_view.IsVariantNull());
  EXPECT_EQ(roundtrip_view.DebugString(), R"({"nested": "value"})");
}

TEST(VariantValueTest, ToVariantValueFromVariantNullField) {
  // 1. JSON object with null field
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json,
                       JSONValue::ParseJSONString(R"({"a": null})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_variant,
                       Value::Variant(Value::Json(std::move(json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView root_view,
                       root_variant.variant_value());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_view,
                       root_view.GetKeyValueIfExists("a"));
  EXPECT_TRUE(field_view.IsVariantNull());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value field_variant, field_view.ToVariantValue());
  EXPECT_TRUE(field_variant.is_valid());
  EXPECT_TRUE(field_variant.type()->IsVariant());
  EXPECT_FALSE(field_variant.is_null());
  EXPECT_EQ(field_variant, Value::VariantNull());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_variant_view,
                       field_variant.variant_value());
  EXPECT_TRUE(field_variant_view.IsVariantNull());
  EXPECT_TRUE(field_variant_view.IsPrimitive());
  EXPECT_FALSE(field_variant_view.IsObject());
  EXPECT_FALSE(field_variant_view.IsArray());
  EXPECT_THAT(field_variant_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));
  EXPECT_EQ(field_variant_view.DebugString(), "null");

  // 2. Struct with SQL NULL field exposed as VariantNull
  TypeFactory factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {Value::NullInt64()}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_variant, Value::Variant(struct_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_view,
                       struct_variant.variant_value());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_null_field_view,
                       struct_view.GetKeyValueIfExists("a"));
  EXPECT_TRUE(struct_null_field_view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_null_variant,
                       struct_null_field_view.ToVariantValue());
  EXPECT_TRUE(struct_null_variant.is_valid());
  EXPECT_TRUE(struct_null_variant.type()->IsVariant());
  EXPECT_FALSE(struct_null_variant.is_null());
  EXPECT_EQ(struct_null_variant, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_struct_null_view,
                       struct_null_variant.variant_value());
  EXPECT_TRUE(roundtrip_struct_null_view.IsVariantNull());
  EXPECT_EQ(roundtrip_struct_null_view.DebugString(), "null");

  // 3. Array with SQL NULL element exposed as VariantNull
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(types::Int64Type(), &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value array_val,
                       Value::MakeArray(array_type, {Value::NullInt64()}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_view,
                       VariantValueView::Create(array_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem_view,
                       array_view.GetElementIfExists(0));
  EXPECT_TRUE(elem_view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value elem_variant, elem_view.ToVariantValue());
  EXPECT_TRUE(elem_variant.is_valid());
  EXPECT_TRUE(elem_variant.type()->IsVariant());
  EXPECT_FALSE(elem_variant.is_null());
  EXPECT_EQ(elem_variant, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_elem_view,
                       elem_variant.variant_value());
  EXPECT_TRUE(roundtrip_elem_view.IsVariantNull());
  EXPECT_EQ(roundtrip_elem_view.DebugString(), "null");
}

TEST(VariantValueTest, ToVariantValueFromJsonNullAndSqlNull) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json, JSONValue::ParseJSONString("null"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_null,
                       Value::Variant(Value::Json(std::move(json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView var_null_view,
                       variant_null.variant_value());
  EXPECT_TRUE(var_null_view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value var_null_to_variant,
                       var_null_view.ToVariantValue());
  EXPECT_TRUE(var_null_to_variant.is_valid());
  EXPECT_TRUE(var_null_to_variant.type()->IsVariant());
  EXPECT_FALSE(var_null_to_variant.is_null());
  EXPECT_EQ(var_null_to_variant, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_var_null_view,
                       var_null_to_variant.variant_value());
  EXPECT_TRUE(roundtrip_var_null_view.IsVariantNull());
  EXPECT_TRUE(roundtrip_var_null_view.IsPrimitive());
  EXPECT_THAT(roundtrip_var_null_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));
  EXPECT_EQ(roundtrip_var_null_view.DebugString(), "null");

  for (const Value& sql_null :
       {Value::NullVariant(), Value::NullInt64(), Value::NullJson()}) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView sql_null_view,
                         VariantValueView::Create(sql_null));
    EXPECT_TRUE(sql_null_view.is_null());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value sql_null_to_variant,
                         sql_null_view.ToVariantValue());
    EXPECT_TRUE(sql_null_to_variant.is_valid());
    EXPECT_TRUE(sql_null_to_variant.type()->IsVariant());
    EXPECT_TRUE(sql_null_to_variant.is_null());
    EXPECT_EQ(sql_null_to_variant, Value::NullVariant());
    EXPECT_EQ(sql_null_to_variant.DebugString(), "NULL");
  }
}

TEST(VariantValueTest, ToVariantValueFromVariantNull) {
  Value variant_null = Value::VariantNull();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView var_null_view,
                       variant_null.variant_value());
  EXPECT_TRUE(var_null_view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value var_null_to_variant,
                       var_null_view.ToVariantValue());
  EXPECT_TRUE(var_null_to_variant.is_valid());
  EXPECT_TRUE(var_null_to_variant.type()->IsVariant());
  EXPECT_FALSE(var_null_to_variant.is_null());
  EXPECT_EQ(var_null_to_variant, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_view,
                       var_null_to_variant.variant_value());
  EXPECT_TRUE(roundtrip_view.IsVariantNull());
  EXPECT_TRUE(roundtrip_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_view.IsObject());
  EXPECT_FALSE(roundtrip_view.IsArray());
  EXPECT_THAT(roundtrip_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::Json(JSONValue())));
  EXPECT_EQ(roundtrip_view.DebugString(), "null");
}

TEST(VariantValueTest, ToVariantValueFromSqlArray) {
  TypeFactory factory;
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(types::Int64Type(), &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_val,
      Value::MakeArray(array_type, {Value::Int64(1), Value::Int64(2)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_view,
                       VariantValueView::Create(array_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value array_variant, array_view.ToVariantValue());
  EXPECT_TRUE(array_variant.type()->IsVariant());
  EXPECT_FALSE(array_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_array_variant, Value::Variant(array_val));
  EXPECT_EQ(array_variant, expected_array_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_array_view,
                       array_variant.variant_value());
  EXPECT_TRUE(roundtrip_array_view.IsArray());
  EXPECT_FALSE(roundtrip_array_view.IsPrimitive());
  EXPECT_FALSE(roundtrip_array_view.IsObject());
  EXPECT_EQ(roundtrip_array_view.DebugString(), "[1, 2]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<VariantValueView> elements,
                       array_view.GetElements());
  ASSERT_EQ(elements.size(), 2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value elem0_variant, elements[0].ToVariantValue());
  EXPECT_TRUE(elem0_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem0, Value::Variant(Value::Int64(1)));
  EXPECT_EQ(elem0_variant, expected_elem0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_elem0,
                       elem0_variant.variant_value());
  EXPECT_TRUE(roundtrip_elem0.IsPrimitive());
  EXPECT_THAT(roundtrip_elem0.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(1)));
  EXPECT_EQ(roundtrip_elem0.DebugString(), "1");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value elem1_variant, elements[1].ToVariantValue());
  EXPECT_TRUE(elem1_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem1, Value::Variant(Value::Int64(2)));
  EXPECT_EQ(elem1_variant, expected_elem1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_elem1,
                       elem1_variant.variant_value());
  EXPECT_TRUE(roundtrip_elem1.IsPrimitive());
  EXPECT_THAT(roundtrip_elem1.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(2)));
  EXPECT_EQ(roundtrip_elem1.DebugString(), "2");
}

TEST(VariantValueTest, ToVariantValueFromJsonArray) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_arr,
                       JSONValue::ParseJSONString("[1, [2, 3]]"));
  Value root_json_val = Value::Json(std::move(json_arr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_view,
                       VariantValueView::Create(root_json_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value array_variant, array_view.ToVariantValue());
  EXPECT_TRUE(array_variant.type()->IsVariant());
  EXPECT_FALSE(array_variant.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_root_variant,
                       Value::Variant(root_json_val));
  EXPECT_EQ(array_variant, expected_root_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_array_view,
                       array_variant.variant_value());
  EXPECT_TRUE(roundtrip_array_view.IsArray());
  EXPECT_EQ(roundtrip_array_view.DebugString(), "[1, [2, 3]]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<VariantValueView> elements,
                       array_view.GetElements());
  ASSERT_EQ(elements.size(), 2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value elem0_variant, elements[0].ToVariantValue());
  EXPECT_TRUE(elem0_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expected_elem0,
                       JSONValue::ParseJSONString("1"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem0_variant,
                       Value::Variant(Value::Json(std::move(expected_elem0))));
  EXPECT_EQ(elem0_variant, expected_elem0_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_elem0,
                       elem0_variant.variant_value());
  EXPECT_TRUE(roundtrip_elem0.IsPrimitive());
  EXPECT_THAT(roundtrip_elem0.GetPrimitiveValue(),
              IsOkAndHolds(Value::Int64(1)));
  EXPECT_EQ(roundtrip_elem0.DebugString(), "1");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value elem1_variant, elements[1].ToVariantValue());
  EXPECT_TRUE(elem1_variant.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expected_elem1,
                       JSONValue::ParseJSONString("[2, 3]"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem1_variant,
                       Value::Variant(Value::Json(std::move(expected_elem1))));
  EXPECT_EQ(elem1_variant, expected_elem1_variant);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView roundtrip_elem1,
                       elem1_variant.variant_value());
  EXPECT_TRUE(roundtrip_elem1.IsArray());
  EXPECT_FALSE(roundtrip_elem1.IsPrimitive());
  EXPECT_FALSE(roundtrip_elem1.IsObject());
  EXPECT_EQ(roundtrip_elem1.DebugString(), "[2, 3]");
}

TEST(VariantValueTest, ToVariantValueChainedNavigation) {
  // Simulates step-by-step SQL dot/subscript navigation (e.g.,
  // v.items[0].payload.tags[1]), where each step extracts a child view and
  // materializes an intermediate TYPE_VARIANT Value via ToVariantValue().
  auto eval_field = [](const Value& variant,
                       absl::string_view key) -> absl::StatusOr<Value> {
    if (variant.is_null()) return Value::NullVariant();
    absl::StatusOr<VariantValueView> view = variant.variant_value();
    if (!view.ok()) return view.status();
    if (!view->IsObject()) return Value::NullVariant();
    absl::StatusOr<std::optional<VariantValueView>> child =
        view->GetKeyValue(key);
    if (!child.ok()) return child.status();
    if (!child->has_value()) return Value::NullVariant();
    return (*child)->ToVariantValue();
  };
  auto eval_index = [](const Value& variant,
                       int64_t index) -> absl::StatusOr<Value> {
    if (variant.is_null()) return Value::NullVariant();
    absl::StatusOr<VariantValueView> view = variant.variant_value();
    if (!view.ok()) return view.status();
    if (!view->IsArray()) return Value::NullVariant();
    absl::StatusOr<std::optional<VariantValueView>> child =
        view->GetElement(index);
    if (!child.ok()) return child.status();
    if (!child->has_value()) return Value::NullVariant();
    return (*child)->ToVariantValue();
  };

  TypeFactory factory;
  const StructType* item_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"id", types::Int64Type()}, {"payload", types::JsonType()}},
      &item_struct_type));
  const ArrayType* items_array_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(item_struct_type, &items_array_type));
  const StructType* root_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"items", items_array_type}}, &root_struct_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue payload_json,
      JSONValue::ParseJSONString(R"({"tags": ["alpha", "beta"], "n": null})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value item0, Value::MakeStruct(item_struct_type,
                                     {Value::Int64(7),
                                      Value::Json(std::move(payload_json))}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value items_arr,
      Value::MakeArray(items_array_type,
                       {item0, Value::Null(item_struct_type)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_struct,
                       Value::MakeStruct(root_struct_type, {items_arr}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value root_variant, Value::Variant(root_struct));

  // v.items[0].id -> INT64 7 wrapped in VARIANT
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_items, eval_field(root_variant, "items"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_item0, eval_index(v_items, 0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_id, eval_field(v_item0, "id"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_id, Value::Variant(Value::Int64(7)));
  EXPECT_EQ(v_id, expected_id);

  // v.items[0].payload.tags[1] -> JSON '"beta"' wrapped in VARIANT
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_payload, eval_field(v_item0, "payload"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_tags, eval_field(v_payload, "tags"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_tag1, eval_index(v_tags, 1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expected_beta,
                       JSONValue::ParseJSONString(R"("beta")"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_tag1,
                       Value::Variant(Value::Json(std::move(expected_beta))));
  EXPECT_EQ(v_tag1, expected_tag1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView v_tag1_view, v_tag1.variant_value());
  EXPECT_THAT(v_tag1_view.GetPrimitiveValue(),
              IsOkAndHolds(Value::String("beta")));

  // v.items[0].payload.n -> Variant null; navigating .anything on Variant null
  // yields SQL NULL Variant.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_json_null, eval_field(v_payload, "n"));
  EXPECT_EQ(v_json_null, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_past_null, eval_field(v_json_null, "x"));
  EXPECT_TRUE(v_past_null.is_null());
  EXPECT_EQ(v_past_null, Value::NullVariant());

  // v.items[1] (SQL NULL struct element) -> Variant null; v.items[1].id -> SQL
  // NULL Variant.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_item1, eval_index(v_items, 1));
  EXPECT_EQ(v_item1, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_item1_id, eval_field(v_item1, "id"));
  EXPECT_TRUE(v_item1_id.is_null());

  // Missing key and out-of-bounds index -> SQL NULL Variant.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_missing, eval_field(v_item0, "missing"));
  EXPECT_TRUE(v_missing.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_oob, eval_index(v_items, 5));
  EXPECT_TRUE(v_oob.is_null());

  // PROTO repeated field navigation: proto.repeated_int32_val[1]
  const ProtoType* ks_proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &ks_proto_type));
  googlesql_test::KitchenSinkPB ks_msg;
  ks_msg.set_int64_key_1(1);
  ks_msg.set_int64_key_2(2);
  ks_msg.add_repeated_int32_val(10);
  ks_msg.add_repeated_int32_val(20);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value proto_variant,
      Value::Variant(Value::Proto(ks_proto_type, ks_msg.SerializeAsCord())));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_rep,
                       eval_field(proto_variant, "repeated_int32_val"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v_rep1, eval_index(v_rep, 1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_rep1, Value::Variant(Value::Int32(20)));
  EXPECT_EQ(v_rep1, expected_rep1);

  // ARRAY<VARIANT> element + nested field navigation
  const ArrayType* variant_array_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(types::VariantType(), &variant_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value heterog_array,
      Value::MakeArray(variant_array_type,
                       {v_item0, Value::VariantNull(), Value::NullVariant()}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value heterog_variant, Value::Variant(heterog_array));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value h0, eval_index(heterog_variant, 0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value h0_id, eval_field(h0, "id"));
  EXPECT_EQ(h0_id, expected_id);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value h1, eval_index(heterog_variant, 1));
  EXPECT_EQ(h1, Value::VariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value h2, eval_index(heterog_variant, 2));
  EXPECT_EQ(h2, Value::VariantNull());
}

TEST(VariantValueTest, ToValueFromInvalidValue) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView invalid_view,
                       VariantValueView::Create(Value{}));
  EXPECT_FALSE(invalid_view.is_valid());
  EXPECT_THAT(invalid_view.ToVariantValue(),
              StatusIs(absl::StatusCode::kInternal));

  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       factory.MakeMeasureType(types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_measure_view,
                       VariantValueView::Create(Value::Null(measure_type)));
  EXPECT_FALSE(null_measure_view.ToVariantValue().ok());

  const StructType* struct_measure_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"m", measure_type}}, &struct_measure_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_measure_val,
      Value::MakeStruct(struct_measure_type, {Value::Null(measure_type)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_measure_view,
                       VariantValueView::Create(struct_measure_val));
  EXPECT_FALSE(struct_measure_view.ToVariantValue().ok());
}

TEST(VariantValueTest, CreateFailsOnNonNullVariant) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_value, Value::Variant(Value::Int64(42)));
  EXPECT_THAT(
      VariantValueView::Create(variant_value),
      StatusIs(absl::StatusCode::kInternal,
               testing::HasSubstr("Cannot construct VariantValueAdapter from a "
                                  "valid, non-null TYPE_VARIANT Value")));
}

TEST(VariantValueTest, CreatePreservesSqlNullVariant) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView view,
                       VariantValueView::Create(Value::NullVariant()));
  EXPECT_TRUE(view.is_valid());
  EXPECT_TRUE(view.is_null());
  EXPECT_FALSE(view.IsPrimitive());
  EXPECT_FALSE(view.IsObject());
  EXPECT_FALSE(view.IsArray());
  EXPECT_FALSE(view.IsVariantNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_val, view.ToVariantValue());
  EXPECT_TRUE(variant_val.type()->IsVariant());
  EXPECT_TRUE(variant_val.is_null());
  EXPECT_EQ(variant_val, Value::NullVariant());
}

TEST(VariantValueTest, UnprojectedTypeAndValueForAtomicTypes) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_val,
                       Value::MakeRange(Value::Date(1), Value::Date(2)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView range_view,
                       VariantValueView::Create(range_val));
  ASSERT_NE(range_view.GetUnprojectedType(), nullptr);
  EXPECT_TRUE(range_view.GetUnprojectedType()->Equals(range_val.type()));
  EXPECT_THAT(range_view.GetUnprojectedValue(), IsOkAndHolds(range_val));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_view,
                       VariantValueView::Create(Value::Int64(42)));
  EXPECT_EQ(int_view.GetUnprojectedType(), types::Int64Type());
  EXPECT_THAT(int_view.GetUnprojectedValue(), IsOkAndHolds(Value::Int64(42)));
}

TEST(VariantValueTest, UnprojectedTypeAndValueForContainersAndJson) {
  TypeFactory factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value struct_val,
                       Value::MakeStruct(struct_type, {Value::Int64(1)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_view,
                       VariantValueView::Create(struct_val));
  EXPECT_TRUE(struct_view.IsObject());
  EXPECT_EQ(struct_view.GetUnprojectedType(), struct_type);
  EXPECT_THAT(struct_view.GetUnprojectedValue(), IsOkAndHolds(struct_val));

  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &proto_type));
  Value proto_val = Value::Proto(proto_type, absl::Cord());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView proto_view,
                       VariantValueView::Create(proto_val));
  EXPECT_TRUE(proto_view.IsObject());
  EXPECT_EQ(proto_view.GetUnprojectedType(), proto_type);
  EXPECT_THAT(proto_view.GetUnprojectedValue(), IsOkAndHolds(proto_val));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value arr_val, Value::MakeArray(types::Int64ArrayType(),
                                                       {Value::Int64(1)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView arr_view,
                       VariantValueView::Create(arr_val));
  EXPECT_TRUE(arr_view.IsArray());
  EXPECT_EQ(arr_view.GetUnprojectedType(), types::Int64ArrayType());
  EXPECT_THAT(arr_view.GetUnprojectedValue(), IsOkAndHolds(arr_val));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* str_map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value str_map_val,
      Value::MakeMap(str_map_type, {{Value::String("k"), Value::Int64(1)}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView str_map_view,
                       VariantValueView::Create(str_map_val));
  EXPECT_TRUE(str_map_view.IsObject());
  EXPECT_EQ(str_map_view.GetUnprojectedType(), str_map_type);
  EXPECT_THAT(str_map_view.GetUnprojectedValue(), IsOkAndHolds(str_map_val));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* int_map_type,
      factory.MakeMapType(types::Int64Type(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value int_map_val,
      Value::MakeMap(int_map_type, {{Value::Int64(1), Value::Int64(2)}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_map_view,
                       VariantValueView::Create(int_map_val));
  EXPECT_FALSE(int_map_view.IsObject());
  EXPECT_EQ(int_map_view.GetUnprojectedType(), int_map_type);
  EXPECT_THAT(int_map_view.GetUnprojectedValue(), IsOkAndHolds(int_map_val));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue parsed_json,
                       JSONValue::ParseJSONString(R"({"a": [1, null]})"));
  Value root_json_val = Value::Json(std::move(parsed_json));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView json_view,
                       VariantValueView::Create(root_json_val));
  EXPECT_EQ(json_view.GetUnprojectedType(), types::JsonType());
  EXPECT_THAT(json_view.GetUnprojectedValue(), IsOkAndHolds(root_json_val));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView json_arr_view,
                       json_view.GetKeyValueIfExists("a"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView int_elem_view,
                       json_arr_view.GetElementIfExists(0));
  EXPECT_TRUE(int_elem_view.IsPrimitive());
  EXPECT_EQ(int_elem_view.GetUnprojectedType(), types::JsonType());
  EXPECT_THAT(int_elem_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::Json(JSONValue(int64_t{1}))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_elem_view,
                       json_arr_view.GetElementIfExists(1));
  EXPECT_TRUE(null_elem_view.IsVariantNull());
  EXPECT_EQ(null_elem_view.GetUnprojectedType(), types::JsonType());
  EXPECT_THAT(null_elem_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::Json(JSONValue())));
}

TEST(VariantValueTest, UnprojectedTypeAndValueForNullInvalidAndUnparsedJson) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_view,
                       VariantValueView::Create(Value::NullInt64()));
  EXPECT_EQ(null_view.GetUnprojectedType(), types::Int64Type());
  EXPECT_THAT(null_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::NullInt64()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_json_view,
                       VariantValueView::Create(Value::NullJson()));
  EXPECT_EQ(null_json_view.GetUnprojectedType(), types::JsonType());
  EXPECT_THAT(null_json_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::NullJson()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView null_variant_view,
                       VariantValueView::Create(Value::NullVariant()));
  EXPECT_EQ(null_variant_view.GetUnprojectedType(), types::VariantType());
  EXPECT_THAT(null_variant_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::NullVariant()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView variant_null_view,
                       VariantValueView::Create(Value::Json(JSONValue())));
  EXPECT_TRUE(variant_null_view.IsVariantNull());
  EXPECT_EQ(variant_null_view.GetUnprojectedType(), types::JsonType());
  EXPECT_THAT(variant_null_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::Json(JSONValue())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView valid_unparsed_json_view,
      VariantValueView::Create(Value::UnvalidatedJsonString(R"({"x": 7})")));
  EXPECT_EQ(valid_unparsed_json_view.GetUnprojectedType(), types::JsonType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expected_json,
                       JSONValue::ParseJSONString(R"({"x": 7})"));
  EXPECT_THAT(valid_unparsed_json_view.GetUnprojectedValue(),
              IsOkAndHolds(Value::Json(std::move(expected_json))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView invalid_view,
                       VariantValueView::Create(Value{}));
  EXPECT_EQ(invalid_view.GetUnprojectedType(), nullptr);
  EXPECT_THAT(invalid_view.GetUnprojectedValue(),
              StatusIs(absl::StatusCode::kInternal));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      VariantValueView malformed_json_view,
      VariantValueView::Create(Value::UnvalidatedJsonString("{malformed")));
  EXPECT_EQ(malformed_json_view.GetUnprojectedType(), types::JsonType());
  EXPECT_THAT(malformed_json_view.GetUnprojectedValue(),
              StatusIs(absl::StatusCode::kOutOfRange));
}

}  // namespace
}  // namespace googlesql
