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

#include "googlesql/reference_impl/functions/variant.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/public/variant_value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

absl::StatusOr<Value> MakeVariant(const Value& inner) {
  return Value::Variant(inner);
}

absl::StatusOr<Value> MakeJsonVariant(absl::string_view json_str) {
  GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json, JSONValue::ParseJSONString(json_str));
  return MakeVariant(Value::Json(std::move(json)));
}

struct IsVariantNullTestCase {
  std::string test_name;
  absl::StatusOr<Value> variant_arg;
  bool expected_result;
};

class IsVariantNullTest
    : public ::testing::TestWithParam<IsVariantNullTestCase> {
 protected:
  static void SetUpTestSuite() { RegisterBuiltinVariantFunctions(); }
};

TEST_P(IsVariantNullTest, EvaluatesIsVariantNull) {
  const IsVariantNullTestCase& test_case = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value& variant_arg, test_case.variant_arg);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BuiltinScalarFunction> fn,
                       BuiltinScalarFunction::CreateValidated(
                           FunctionKind::kIsVariantNull, language_options,
                           types::BoolType(), {}));

  EvaluationContext context((EvaluationOptions()));
  Value result;
  absl::Status status;
  ASSERT_TRUE(
      fn->Eval(/*params=*/{}, {variant_arg}, &context, &result, &status))
      << status;
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_TRUE(result.type()->IsBool());
  EXPECT_EQ(result, Value::Bool(test_case.expected_result));
}

std::vector<IsVariantNullTestCase> GetIsVariantNullTestCases() {
  auto make_struct_variant_with_null_field = []() -> absl::StatusOr<Value> {
    static auto* type_factory = new TypeFactory();
    const StructType* struct_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType({{"a", types::Int64Type()}},
                                                 &struct_type));
    return MakeVariant(Value::Struct(struct_type, {Value::NullInt64()}));
  };
  auto make_array_variant_with_null_element = []() -> absl::StatusOr<Value> {
    GOOGLESQL_ASSIGN_OR_RETURN(
        Value array_with_null_element,
        Value::MakeArray(types::Int64ArrayType(), {Value::NullInt64()}));
    return MakeVariant(array_with_null_element);
  };
  auto extract_key = [](const absl::StatusOr<Value>& variant,
                        absl::string_view key) -> absl::StatusOr<Value> {
    GOOGLESQL_RETURN_IF_ERROR(variant.status());
    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView view, variant->variant_value());
    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView field, view.GetKeyValueIfExists(key));
    return field.ToVariantValue();
  };
  auto extract_element = [](const absl::StatusOr<Value>& variant,
                            int64_t index) -> absl::StatusOr<Value> {
    GOOGLESQL_RETURN_IF_ERROR(variant.status());
    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView view, variant->variant_value());
    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView element, view.GetElementIfExists(index));
    return element.ToVariantValue();
  };

  absl::StatusOr<Value> struct_variant_with_null_field =
      make_struct_variant_with_null_field();
  absl::StatusOr<Value> array_variant_with_null_element =
      make_array_variant_with_null_element();
  absl::StatusOr<Value> json_object_variant_with_null_field =
      MakeJsonVariant(R"({"a": null})");
  absl::StatusOr<Value> json_array_variant_with_null_element =
      MakeJsonVariant("[null]");

  return {
      {
          .test_name = "VariantNullReturnsTrue",
          .variant_arg = Value::VariantNull(),
          .expected_result = true,
      },
      {
          .test_name = "JsonNullVariantReturnsTrue",
          .variant_arg = MakeJsonVariant("null"),
          .expected_result = true,
      },
      {
          .test_name = "ExtractedStructNullFieldReturnsTrue",
          .variant_arg = extract_key(struct_variant_with_null_field, "a"),
          .expected_result = true,
      },
      {
          .test_name = "ExtractedArrayNullElementReturnsTrue",
          .variant_arg = extract_element(array_variant_with_null_element, 0),
          .expected_result = true,
      },
      {
          .test_name = "ExtractedJsonObjectNullFieldReturnsTrue",
          .variant_arg = extract_key(json_object_variant_with_null_field, "a"),
          .expected_result = true,
      },
      {
          .test_name = "ExtractedJsonArrayNullElementReturnsTrue",
          .variant_arg =
              extract_element(json_array_variant_with_null_element, 0),
          .expected_result = true,
      },
      {
          .test_name = "SqlNullVariantReturnsFalse",
          .variant_arg = Value::NullVariant(),
          .expected_result = false,
      },
      {
          .test_name = "TypedSqlNullWrappedInVariantReturnsFalse",
          .variant_arg = MakeVariant(Value::NullBool()),
          .expected_result = false,
      },
      {
          .test_name = "JsonStringNullReturnsFalse",
          .variant_arg = MakeJsonVariant(R"("null")"),
          .expected_result = false,
      },
      {
          .test_name = "ScalarInt64ReturnsFalse",
          .variant_arg = MakeVariant(Value::Int64(1)),
          .expected_result = false,
      },
      {
          .test_name = "ScalarStringReturnsFalse",
          .variant_arg = MakeVariant(Value::String("abc")),
          .expected_result = false,
      },
      {
          .test_name = "ScalarBoolReturnsFalse",
          .variant_arg = MakeVariant(Value::Bool(false)),
          .expected_result = false,
      },
      {
          .test_name = "StructWithNullFieldReturnsFalse",
          .variant_arg = struct_variant_with_null_field,
          .expected_result = false,
      },
      {
          .test_name = "ArrayWithNullElementReturnsFalse",
          .variant_arg = array_variant_with_null_element,
          .expected_result = false,
      },
      {
          .test_name = "JsonObjectWithNullFieldReturnsFalse",
          .variant_arg = json_object_variant_with_null_field,
          .expected_result = false,
      },
      {
          .test_name = "JsonArrayWithNullElementReturnsFalse",
          .variant_arg = json_array_variant_with_null_element,
          .expected_result = false,
      },
  };
}

INSTANTIATE_TEST_SUITE_P(
    IsVariantNullTests, IsVariantNullTest,
    ::testing::ValuesIn(GetIsVariantNullTestCases()),
    [](const ::testing::TestParamInfo<IsVariantNullTestCase>& info) {
      return info.param.test_name;
    });

struct VariantSubscriptTestCase {
  std::string test_name;
  absl::StatusOr<Value> variant_arg;
  Value subscript_arg;
  absl::StatusOr<Value> expected;
};

class VariantSubscriptTest
    : public ::testing::TestWithParam<VariantSubscriptTestCase> {
 protected:
  static void SetUpTestSuite() { RegisterBuiltinVariantFunctions(); }
};

TEST_P(VariantSubscriptTest, EvaluatesSubscript) {
  const VariantSubscriptTestCase& test_case = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value& variant_arg, test_case.variant_arg);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value& expected, test_case.expected);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BuiltinScalarFunction> fn,
                       BuiltinScalarFunction::CreateValidated(
                           FunctionKind::kVariantSubscript, language_options,
                           types::VariantType(), {}));

  EvaluationContext context((EvaluationOptions()));
  Value result;
  absl::Status status;
  ASSERT_TRUE(fn->Eval(/*params=*/{}, {variant_arg, test_case.subscript_arg},
                       &context, &result, &status))
      << status;
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_TRUE(result.type()->IsVariant());
  EXPECT_TRUE(result.Equals(expected))
      << "Actual: " << result.DebugString()
      << ", Expected: " << expected.DebugString();
}

std::vector<VariantSubscriptTestCase> GetVariantSubscriptTestCases() {
  const Value nested_array_val =
      Value::Array(types::Int64ArrayType(), {Value::Int64(1), Value::Int64(2)});
  const Value struct_variant =
      test_values::Variant(test_values::Struct({{"a", Value::Int64(10)},
                                                {"b", Value::String("hello")},
                                                {"n", Value::NullInt64()},
                                                {"arr", nested_array_val},
                                                {"dup", Value::Int64(1)},
                                                {"dup", Value::Int64(2)},
                                                {"CaseKey", Value::Int64(100)},
                                                {"casekey", Value::Int64(200)},
                                                {"", Value::Int64(77)}}));
  const Value string_map_variant = test_values::Variant(
      test_values::Map({{Value::String("a"), Value::Int64(10)},
                        {Value::String("n"), Value::NullInt64()}}));
  googlesql_test::KitchenSinkPB proto_msg;
  proto_msg.set_int64_key_1(123);
  proto_msg.set_int64_key_2(456);
  proto_msg.add_repeated_int64_val(10);
  proto_msg.add_repeated_int64_val(20);
  const Value proto_variant = test_values::Variant(Value::Proto(
      test_values::MakeProtoType(googlesql_test::KitchenSinkPB::descriptor()),
      proto_msg.SerializeAsCord()));
  const Value int_map_variant = test_values::Variant(
      test_values::Map({{Value::Int64(1), Value::Int64(2)}}));
  const Value range_variant =
      test_values::Variant(test_values::Range(Value::Date(1), Value::Date(2)));
  const Value enum_variant = test_values::Variant(Value::Enum(
      test_values::MakeEnumType(googlesql_test::TestEnum_descriptor()), 1));
  const Value array_variant = test_values::Variant(
      Value::Array(types::Int64ArrayType(),
                   {Value::Int64(100), Value::NullInt64(), Value::Int64(300)}));
  const absl::StatusOr<Value> json_obj_variant =
      MakeJsonVariant(R"({"x": 42, "y": null, "z": "world"})");
  const absl::StatusOr<Value> json_arr_variant =
      MakeJsonVariant(R"([7, null, "item"])");
  const Value variant_null = Value::VariantNull();
  const Value int_variant = test_values::Variant(Value::Int64(99));
  const Value inner_struct_val = test_values::Struct({{"x", Value::Int64(42)}});
  const Value nested_struct_variant = test_values::Variant(
      test_values::Struct({{"nested_struct", inner_struct_val},
                           {"nested_array", nested_array_val}}));
  const Value struct_array_variant =
      test_values::Variant(test_values::Array({inner_struct_val}));
  const absl::StatusOr<Value> nested_json_variant =
      MakeJsonVariant(R"({"nested_obj": {"k": 123}, "nested_arr": [1, 2, 3]})");
  const Value unparsed_json_obj_variant =
      test_values::Variant(Value::UnvalidatedJsonString(R"({"x": 42})"));
  const Value unparsed_json_arr_variant = test_values::Variant(
      Value::UnvalidatedJsonString(R"([7, null, "item"])"));

  return {
      {
          .test_name = "StructExistingIntField",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("a"),
          .expected = MakeVariant(Value::Int64(10)),
      },
      {
          .test_name = "StructExistingStringField",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("b"),
          .expected = MakeVariant(Value::String("hello")),
      },
      {
          .test_name = "StructNullFieldReturnsVariantNull",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("n"),
          .expected = Value::VariantNull(),
      },
      {
          .test_name = "StructArrayField",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("arr"),
          .expected = MakeVariant(nested_array_val),
      },
      {
          .test_name = "StructDuplicateKeyReturnsFirstMatch",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("dup"),
          .expected = MakeVariant(Value::Int64(1)),
      },
      {
          .test_name = "StructKeysDifferingOnlyByCaseMatchCaseSensitively",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("casekey"),
          .expected = MakeVariant(Value::Int64(200)),
      },
      {
          .test_name = "StructUnnamedFieldMatchesEmptyStringKey",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String(""),
          .expected = MakeVariant(Value::Int64(77)),
      },
      {
          .test_name = "StructMissingFieldReturnsSqlNull",
          .variant_arg = struct_variant,
          .subscript_arg = Value::String("missing"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "StructWithIntIndexReturnsSqlNull",
          .variant_arg = struct_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "StringMapExistingKey",
          .variant_arg = string_map_variant,
          .subscript_arg = Value::String("a"),
          .expected = MakeVariant(Value::Int64(10)),
      },
      {
          .test_name = "StringMapNullValueReturnsVariantNull",
          .variant_arg = string_map_variant,
          .subscript_arg = Value::String("n"),
          .expected = Value::VariantNull(),
      },
      {
          .test_name = "StringMapMissingKeyReturnsSqlNull",
          .variant_arg = string_map_variant,
          .subscript_arg = Value::String("missing"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "StringMapWithIntIndexReturnsSqlNull",
          .variant_arg = string_map_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "ProtoExistingField",
          .variant_arg = proto_variant,
          .subscript_arg = Value::String("int64_key_1"),
          .expected = MakeVariant(Value::Int64(123)),
      },
      {
          .test_name = "ProtoRepeatedFieldReturnsArrayVariant",
          .variant_arg = proto_variant,
          .subscript_arg = Value::String("repeated_int64_val"),
          .expected = MakeVariant(Value::Array(
              types::Int64ArrayType(), {Value::Int64(10), Value::Int64(20)})),
      },
      {
          .test_name = "ProtoUnsetFieldWithDefault",
          .variant_arg = proto_variant,
          .subscript_arg = Value::String("int64_val"),
          .expected = MakeVariant(Value::Int64(0)),
      },
      {
          .test_name = "ProtoUnsetNullableFieldReturnsVariantNull",
          .variant_arg = proto_variant,
          .subscript_arg = Value::String("int_with_no_default_nullable"),
          .expected = Value::VariantNull(),
      },
      {
          .test_name = "ProtoMissingFieldReturnsSqlNull",
          .variant_arg = proto_variant,
          .subscript_arg = Value::String("missing"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "ProtoWithIntIndexReturnsSqlNull",
          .variant_arg = proto_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "IntMapWithKeyReturnsSqlNull",
          .variant_arg = int_map_variant,
          .subscript_arg = Value::String("1"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "IntMapWithIntIndexReturnsSqlNull",
          .variant_arg = int_map_variant,
          .subscript_arg = Value::Int64(1),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "RangeWithKeyReturnsSqlNull",
          .variant_arg = range_variant,
          .subscript_arg = Value::String("start"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "RangeWithIntIndexReturnsSqlNull",
          .variant_arg = range_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "EnumWithKeyReturnsSqlNull",
          .variant_arg = enum_variant,
          .subscript_arg = Value::String("a"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "EnumWithIntIndexReturnsSqlNull",
          .variant_arg = enum_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "ArrayValidIndex",
          .variant_arg = array_variant,
          .subscript_arg = Value::Int64(0),
          .expected = MakeVariant(Value::Int64(100)),
      },
      {
          .test_name = "ArrayNullElementReturnsVariantNull",
          .variant_arg = array_variant,
          .subscript_arg = Value::Int64(1),
          .expected = Value::VariantNull(),
      },
      {
          .test_name = "ArrayOutOfBoundsIndexReturnsSqlNull",
          .variant_arg = array_variant,
          .subscript_arg = Value::Int64(5),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "ArrayNegativeIndexReturnsSqlNull",
          .variant_arg = array_variant,
          .subscript_arg = Value::Int64(-1),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "ArrayIndexExceedingIntMaxReturnsSqlNull",
          .variant_arg = array_variant,
          .subscript_arg = Value::Int64(1LL << 32),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "ArrayWithStringKeyReturnsSqlNull",
          .variant_arg = array_variant,
          .subscript_arg = Value::String("a"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "JsonObjectExistingKey",
          .variant_arg = json_obj_variant,
          .subscript_arg = Value::String("x"),
          .expected = MakeJsonVariant("42"),
      },
      {
          .test_name = "JsonObjectNullFieldReturnsVariantNull",
          .variant_arg = json_obj_variant,
          .subscript_arg = Value::String("y"),
          .expected = Value::VariantNull(),
      },
      {
          .test_name = "JsonObjectMissingKeyReturnsSqlNull",
          .variant_arg = json_obj_variant,
          .subscript_arg = Value::String("missing"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "JsonArrayValidIndex",
          .variant_arg = json_arr_variant,
          .subscript_arg = Value::Int64(2),
          .expected = MakeJsonVariant(R"("item")"),
      },
      {
          .test_name = "JsonArrayNullElementReturnsVariantNull",
          .variant_arg = json_arr_variant,
          .subscript_arg = Value::Int64(1),
          .expected = Value::VariantNull(),
      },
      {
          .test_name = "VariantNullWithKeyReturnsSqlNull",
          .variant_arg = variant_null,
          .subscript_arg = Value::String("a"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "VariantNullWithIndexReturnsSqlNull",
          .variant_arg = variant_null,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "SqlNullVariantReturnsSqlNull",
          .variant_arg = Value::NullVariant(),
          .subscript_arg = Value::String("a"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "SqlNullKeyReturnsSqlNull",
          .variant_arg = struct_variant,
          .subscript_arg = Value::NullString(),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "SqlNullIndexReturnsSqlNull",
          .variant_arg = array_variant,
          .subscript_arg = Value::NullInt64(),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "PrimitiveVariantWithKeyReturnsSqlNull",
          .variant_arg = int_variant,
          .subscript_arg = Value::String("a"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "PrimitiveVariantWithIndexReturnsSqlNull",
          .variant_arg = int_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "StructNestedStructField",
          .variant_arg = nested_struct_variant,
          .subscript_arg = Value::String("nested_struct"),
          .expected = MakeVariant(inner_struct_val),
      },
      {
          .test_name = "StructNestedArrayField",
          .variant_arg = nested_struct_variant,
          .subscript_arg = Value::String("nested_array"),
          .expected = MakeVariant(nested_array_val),
      },
      {
          .test_name = "ArrayNestedStructElement",
          .variant_arg = struct_array_variant,
          .subscript_arg = Value::Int64(0),
          .expected = MakeVariant(inner_struct_val),
      },
      {
          .test_name = "JsonArrayNestedArray",
          .variant_arg = MakeJsonVariant(R"([[10, 20], [30, 40]])"),
          .subscript_arg = Value::Int64(0),
          .expected = MakeJsonVariant(R"([10, 20])"),
      },
      {
          .test_name = "JsonObjectNestedObject",
          .variant_arg = nested_json_variant,
          .subscript_arg = Value::String("nested_obj"),
          .expected = MakeJsonVariant(R"({"k": 123})"),
      },
      {
          .test_name = "JsonObjectNestedArray",
          .variant_arg = nested_json_variant,
          .subscript_arg = Value::String("nested_arr"),
          .expected = MakeJsonVariant(R"([1, 2, 3])"),
      },
      {
          .test_name = "JsonObjectWithIntIndexReturnsSqlNull",
          .variant_arg = json_obj_variant,
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "JsonArrayWithStringKeyReturnsSqlNull",
          .variant_arg = json_arr_variant,
          .subscript_arg = Value::String("key"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "JsonArrayNegativeIndexReturnsSqlNull",
          .variant_arg = json_arr_variant,
          .subscript_arg = Value::Int64(-1),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "JsonArrayOutOfBoundsIndexReturnsSqlNull",
          .variant_arg = json_arr_variant,
          .subscript_arg = Value::Int64(10),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "JsonArrayExceedingIntMaxIndexReturnsSqlNull",
          .variant_arg = json_arr_variant,
          .subscript_arg = Value::Int64(1LL << 32),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "UnparsedValidJsonObjectExistingKey",
          .variant_arg = unparsed_json_obj_variant,
          .subscript_arg = Value::String("x"),
          .expected = MakeJsonVariant("42"),
      },
      {
          .test_name = "UnparsedValidJsonArrayValidIndex",
          .variant_arg = unparsed_json_arr_variant,
          .subscript_arg = Value::Int64(2),
          .expected = MakeJsonVariant(R"("item")"),
      },
      {
          .test_name = "UnparsedMalformedJsonWithKeyReturnsSqlNull",
          .variant_arg =
              MakeVariant(Value::UnvalidatedJsonString(R"({"a": 100, "b": 2)")),
          .subscript_arg = Value::String("a"),
          .expected = Value::NullVariant(),
      },
      {
          .test_name = "UnparsedMalformedJsonWithIndexReturnsSqlNull",
          .variant_arg = MakeVariant(Value::UnvalidatedJsonString("[100, 2")),
          .subscript_arg = Value::Int64(0),
          .expected = Value::NullVariant(),
      },
  };
}

INSTANTIATE_TEST_SUITE_P(
    VariantSubscriptTests, VariantSubscriptTest,
    ::testing::ValuesIn(GetVariantSubscriptTestCases()),
    [](const ::testing::TestParamInfo<VariantSubscriptTestCase>& info) {
      return info.param.test_name;
    });

TEST(VariantSubscriptFunctionTest,
     UnsupportedSubscriptTypeReturnsInternalError) {
  RegisterBuiltinVariantFunctions();
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BuiltinScalarFunction> fn,
                       BuiltinScalarFunction::CreateValidated(
                           FunctionKind::kVariantSubscript, language_options,
                           types::VariantType(), {}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value int_variant, MakeVariant(Value::Int64(1)));

  EvaluationContext context((EvaluationOptions()));
  Value result;
  absl::Status status;
  bool success = fn->Eval(/*params=*/{}, {int_variant, Value::Bool(true)},
                          &context, &result, &status);
  EXPECT_FALSE(success);
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_THAT(status.message(),
              testing::HasSubstr("Unsupported subscript type: BOOL"));
}

}  // namespace
}  // namespace googlesql
