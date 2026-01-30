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

#include "googlesql/common/internal_value.h"

#include <cstdint>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {

using ::testing::ElementsAre;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using types::Int64Type;

static Value Int64(int64_t v) { return Value::Int64(v); }

TEST(InternalValueTest, SimpleArraysUncertainOrders) {
  const ArrayType* type = test_values::MakeArrayType(Int64Type());
  Value array_null = Value::Null(type);
  Value array_empty =
      InternalValue::ArrayChecked(type, InternalValue::kIgnoresOrder,
                                  /*values=*/{});
  Value array_len_one_ordered = InternalValue::ArrayChecked(
      type, InternalValue::kPreservesOrder, {Int64(1)});
  Value array_len_one_unordered = InternalValue::ArrayChecked(
      type, InternalValue::kIgnoresOrder, {Int64(1)});
  Value array_len_two_ordered = InternalValue::ArrayChecked(
      type, InternalValue::kPreservesOrder, {Int64(1), Int64(2)});
  Value array_len_two_unordered = InternalValue::ArrayChecked(
      type, InternalValue::kIgnoresOrder, {Int64(1), Int64(2)});
  Value array_len_three_ordered = InternalValue::ArrayChecked(
      type, InternalValue::kPreservesOrder,
      {Int64(1), Int64(2), Value::Null(Int64Type())});
  Value array_len_three_unordered = InternalValue::ArrayChecked(
      type, InternalValue::kIgnoresOrder,
      {Int64(1), Int64(2), Value::Null(Int64Type())});

  EXPECT_FALSE(InternalValue::ContainsArrayWithUncertainOrder(array_null));
  EXPECT_FALSE(InternalValue::ContainsArrayWithUncertainOrder(array_empty));
  EXPECT_FALSE(
      InternalValue::ContainsArrayWithUncertainOrder(array_len_one_ordered));
  EXPECT_FALSE(
      InternalValue::ContainsArrayWithUncertainOrder(array_len_one_unordered));
  EXPECT_FALSE(
      InternalValue::ContainsArrayWithUncertainOrder(array_len_two_ordered));
  EXPECT_TRUE(
      InternalValue::ContainsArrayWithUncertainOrder(array_len_two_unordered));
  EXPECT_FALSE(
      InternalValue::ContainsArrayWithUncertainOrder(array_len_three_ordered));
  EXPECT_TRUE(InternalValue::ContainsArrayWithUncertainOrder(
      array_len_three_unordered));
}

TEST(InternalValueTest, NestedValues) {
  const ArrayType* array_int = test_values::MakeArrayType(Int64Type());
  const StructType* struct_type =
      test_values::MakeStructType({{"a", array_int}, {"b", array_int}});
  const ArrayType* struct_arr = test_values::MakeArrayType(struct_type);

  Value array_null = Value::Null(array_int);
  Value array_empty =
      InternalValue::ArrayChecked(array_int, InternalValue::kIgnoresOrder,
                                  /*values=*/{});
  Value int_array_len_three_ordered = InternalValue::ArrayChecked(
      array_int, InternalValue::kPreservesOrder,
      {Int64(1), Int64(2), Value::Null(Int64Type())});
  Value int_array_len_three_unordered = InternalValue::ArrayChecked(
      array_int, InternalValue::kIgnoresOrder,
      {Int64(1), Int64(2), Value::Null(Int64Type())});

  Value struct_null_empty =
      *Value::MakeStruct(struct_type, {array_null, array_empty});
  Value struct_ordered_unordered = *Value::MakeStruct(
      struct_type,
      {int_array_len_three_ordered, int_array_len_three_unordered});

  Value array_of_struct_ordered = InternalValue::ArrayChecked(
      struct_arr, InternalValue::kPreservesOrder,
      {struct_null_empty, struct_ordered_unordered});

  EXPECT_FALSE(
      InternalValue::ContainsArrayWithUncertainOrder(struct_null_empty));
  EXPECT_TRUE(
      InternalValue::ContainsArrayWithUncertainOrder(struct_ordered_unordered));
  EXPECT_TRUE(
      InternalValue::ContainsArrayWithUncertainOrder(array_of_struct_ordered));
}

TEST(InternalValueTest, MeasureFormatting) {
  LanguageOptions language_options;
  TypeFactory type_factory;
  const StructType* captured_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"key", types::Int64Type()}, {"value", types::Int64Type()}},
      &captured_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value captured_values_as_struct,
                       Value::MakeStruct(captured_struct_type,
                                         {Value::Int64(1), Value::Int64(100)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       type_factory.MakeMeasureType(types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value measure, InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                                captured_values_as_struct, {0},
                                                language_options));
  EXPECT_EQ(measure.DebugString(/*verbose=*/true),
            "Measure<Int64>{Struct{key:Int64(1), value:Int64(100)}}");
  EXPECT_EQ(measure.DebugString(), "Measure<Int64>{{key:1, value:100}}");
  EXPECT_EQ(measure.Format(/*print_top_level_type=*/false),
            "Measure<Int64>{{key:1, value:100}}");
  EXPECT_EQ(measure.Format(),
            "Measure<Int64>{Struct{key:Int64(1), value:Int64(100)}}");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value measure_as_struct,
                       InternalValue::GetMeasureAsStructValue(measure));
  EXPECT_EQ(measure_as_struct, captured_values_as_struct);
  EXPECT_THAT(InternalValue::GetMeasureGrainLockingIndices(measure),
              IsOkAndHolds(ElementsAre(0)));
}

TEST(InternalValueTest, NullMeasure) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       type_factory.MakeMeasureType(types::Int64Type()));
  Value null_measure = Value::Null(measure_type);
  EXPECT_TRUE(null_measure.is_null());
  EXPECT_EQ(null_measure.DebugString(/*verbose=*/true), "Measure<Int64>(NULL)");
  EXPECT_EQ(null_measure.DebugString(), "NULL");
  EXPECT_EQ(null_measure.Format(/*print_top_level_type=*/false), "NULL");
  EXPECT_EQ(null_measure.Format(), "Measure<Int64>(NULL)");
  // Measures do not have SQL literals, and do not support CASTs. The printed
  // SQL is thus not meaningful.
  EXPECT_EQ(null_measure.GetSQLLiteral(PRODUCT_INTERNAL), "NULL");
  EXPECT_EQ(null_measure.GetSQL(PRODUCT_INTERNAL),
            "CAST(NULL AS MEASURE<INT64>)");
  EXPECT_THAT(InternalValue::GetMeasureAsStructValue(null_measure),
              StatusIs(absl::StatusCode::kInvalidArgument, "Null measure"));
  EXPECT_THAT(InternalValue::GetMeasureGrainLockingIndices(null_measure),
              StatusIs(absl::StatusCode::kInvalidArgument, "Null measure"));
}

TEST(InternalValueTest, TestMeasureCreation) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_JSON_TYPE);
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       type_factory.MakeMeasureType(types::Int64Type()));

  // Error when measure type is nullptr.
  EXPECT_THAT(
      InternalValue::MakeMeasure(nullptr, Value(), {0}, language_options),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Measure type cannot be nullptr"));

  // Error when invalid Value supplied to MakeMeasure.
  EXPECT_THAT(
      InternalValue::MakeMeasure(measure_type->AsMeasure(), Value(), {0},
                                 language_options),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Measure must capture a non-null STRUCT-typed value with at "
               "least one field"));

  // Error when NULL supplied to MakeMeasure.
  const StructType* captured_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"key", types::Int64Type()}, {"value", types::Int64Type()}},
      &captured_struct_type));
  Value null_struct = Value::Null(captured_struct_type);
  EXPECT_THAT(InternalValue::MakeMeasure(measure_type->AsMeasure(), null_struct,
                                         {0}, language_options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Measure must capture a non-null STRUCT-typed value "
                       "with at least one field"));

  // Error when non-STRUCT Value supplied to MakeMeasure.
  EXPECT_THAT(
      InternalValue::MakeMeasure(measure_type->AsMeasure(), Value::Int64(1),
                                 {0}, language_options),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Measure must capture a non-null STRUCT-typed value "
               "with at least one field"));

  // Error when a struct with an empty field name supplied to MakeMeasure.
  const StructType* unnamed_field_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"key", types::Int64Type()}, {"", types::Int64Type()}},
      &unnamed_field_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value unnamed_field_captured_values_as_struct,
                       Value::MakeStruct(unnamed_field_struct_type,
                                         {Value::Int64(1), Value::Int64(100)}));
  EXPECT_THAT(
      InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                 unnamed_field_captured_values_as_struct, {0},
                                 language_options),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Captured measure value must contain non-empty field names"));

  // Error when a struct with duplicate field names supplied to MakeMeasure.
  const StructType* duplicate_field_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"key", types::Int64Type()}, {"key", types::Int64Type()}},
      &duplicate_field_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value duplicate_field_captured_values_as_struct,
                       Value::MakeStruct(duplicate_field_struct_type,
                                         {Value::Int64(1), Value::Int64(100)}));
  EXPECT_THAT(
      InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                 duplicate_field_captured_values_as_struct, {0},
                                 language_options),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Captured measure value must contain unique field names"));

  // Error when empty struct supplied to MakeMeasure.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_struct,
                       Value::MakeStruct(types::EmptyStructType(), {}));
  EXPECT_THAT(InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                         empty_struct, {0}, language_options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Measure must capture a non-null STRUCT-typed value "
                       "with at least one field"));

  // Error when no `key_indices` supplied to MakeMeasure.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value captured_values_as_struct,
                       Value::MakeStruct(captured_struct_type,
                                         {Value::Int64(1), Value::Int64(100)}));
  EXPECT_THAT(
      InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                 captured_values_as_struct, {},
                                 language_options),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          "Measure value creation requires a non-empty list of key indices"));

  // Error when invalid `key_indices` supplied to MakeMeasure.
  EXPECT_THAT(InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                         captured_values_as_struct, {-1},
                                         language_options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Key index for measure value creation is invalid"));
  EXPECT_THAT(InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                         captured_values_as_struct, {100},
                                         language_options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Key index for measure value creation is invalid"));

  // Error when a key type does not support grouping.
  const StructType* key_type_does_not_support_grouping_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"key", types::JsonType()}, {"value", types::Int64Type()}},
      &key_type_does_not_support_grouping_struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value captured_values_with_invalid_key_type,
      Value::MakeStruct(key_type_does_not_support_grouping_struct_type,
                        {Value::NullJson(), Value::Int64(100)}));
  EXPECT_THAT(InternalValue::MakeMeasure(measure_type->AsMeasure(),
                                         captured_values_with_invalid_key_type,
                                         {0}, language_options),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Key type does not support grouping: JSON"));
}

TEST(InternalValueTest, MeasureMethodsFailOnNonMeasureValue) {
  Value int64_value = Value::Int64(1);
  EXPECT_THAT(
      InternalValue::GetMeasureAsStructValue(int64_value),
      StatusIs(absl::StatusCode::kInvalidArgument, "Not a measure type"));
  EXPECT_THAT(
      InternalValue::GetMeasureGrainLockingIndices(int64_value),
      StatusIs(absl::StatusCode::kInvalidArgument, "Not a measure type"));
}

}  // namespace googlesql
