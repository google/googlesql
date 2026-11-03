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
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/public/variant_value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
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

}  // namespace
}  // namespace googlesql
