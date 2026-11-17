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

#include "googlesql/common/determinism.h"

#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

using ::googlesql::test_values::MakeArrayType;
using ::googlesql::test_values::MakeMapType;
using ::googlesql::test_values::MakeProtoType;
using ::googlesql::test_values::MakeRangeType;
using ::googlesql::test_values::MakeStructType;
using ::googlesql::values::Int64;
using ::googlesql::values::Int64Array;
using ::googlesql::values::Null;

TEST(DeterminismTest, HasFloatingPointScalarTypes) {
  EXPECT_TRUE(HasFloatingPoint(types::FloatType()));
  EXPECT_TRUE(HasFloatingPoint(types::DoubleType()));
  EXPECT_TRUE(HasFloatingPoint(types::GeographyType()));

  EXPECT_FALSE(HasFloatingPoint(types::Int64Type()));
  EXPECT_FALSE(HasFloatingPoint(types::StringType()));
  EXPECT_FALSE(HasFloatingPoint(types::BoolType()));
  EXPECT_FALSE(HasFloatingPoint(types::JsonType()));
  EXPECT_FALSE(HasFloatingPoint(types::VariantType()));
}

TEST(DeterminismTest, HasFloatingPointCompositeTypes) {
  EXPECT_FALSE(HasFloatingPoint(types::Int64ArrayType()));
  EXPECT_TRUE(HasFloatingPoint(types::DoubleArrayType()));

  const StructType* int_struct = MakeStructType({{"a", types::Int64Type()}});
  const StructType* float_struct =
      MakeStructType({{"a", types::Int64Type()}, {"b", types::FloatType()}});
  EXPECT_FALSE(HasFloatingPoint(int_struct));
  EXPECT_TRUE(HasFloatingPoint(float_struct));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* int_map,
                       MakeMapType(types::Int64Type(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* double_map,
                       MakeMapType(types::Int64Type(), types::DoubleType()));
  EXPECT_FALSE(HasFloatingPoint(int_map));
  EXPECT_TRUE(HasFloatingPoint(double_map));
}

TEST(DeterminismTest, HasFloatingPointProtoTypes) {
  EXPECT_TRUE(HasFloatingPoint(
      MakeProtoType(googlesql_test::KitchenSinkPB::descriptor())));
  EXPECT_FALSE(HasFloatingPoint(
      MakeProtoType(googlesql_test::EmptyMessage::descriptor())));
}

TEST(DeterminismTest, CastHasPotentialNondeterminismScalars) {
  EXPECT_TRUE(
      CastHasPotentialNondeterminism(types::DoubleType(), types::StringType()));
  EXPECT_TRUE(
      CastHasPotentialNondeterminism(types::FloatType(), types::JsonType()));
  EXPECT_TRUE(CastHasPotentialNondeterminism(types::GeographyType(),
                                             types::StringType()));

  EXPECT_FALSE(
      CastHasPotentialNondeterminism(types::DoubleType(), types::DoubleType()));
  EXPECT_FALSE(
      CastHasPotentialNondeterminism(types::DoubleType(), types::FloatType()));
  EXPECT_FALSE(
      CastHasPotentialNondeterminism(types::Int64Type(), types::StringType()));
  EXPECT_FALSE(CastHasPotentialNondeterminism(types::DoubleType(),
                                              types::VariantType()));
}

TEST(DeterminismTest, CastHasPotentialNondeterminismComposites) {
  EXPECT_TRUE(CastHasPotentialNondeterminism(types::DoubleArrayType(),
                                             types::StringArrayType()));
  EXPECT_FALSE(CastHasPotentialNondeterminism(types::DoubleArrayType(),
                                              types::DoubleArrayType()));

  // Pairwise component checking detects DOUBLE -> STRING even when the output
  // struct also contains a DOUBLE in another field.
  const StructType* double_string_struct =
      MakeStructType({{"a", types::DoubleType()}, {"b", types::StringType()}});
  const StructType* string_double_struct =
      MakeStructType({{"a", types::StringType()}, {"b", types::DoubleType()}});
  EXPECT_TRUE(CastHasPotentialNondeterminism(double_string_struct,
                                             string_double_struct));

  EXPECT_FALSE(CastHasPotentialNondeterminism(MakeRangeType(types::DateType()),
                                              types::StringType()));
}

TEST(DeterminismTest, CastHasPotentialNondeterminismComponentMismatchToJson) {
  EXPECT_FALSE(CastHasPotentialNondeterminism(types::DoubleArrayType(),
                                              types::JsonType()));
  const StructType* double_struct =
      MakeStructType({{"a", types::DoubleType()}});
  EXPECT_FALSE(
      CastHasPotentialNondeterminism(double_struct, types::JsonType()));
  const StructType* int_struct = MakeStructType({{"a", types::Int64Type()}});
  EXPECT_FALSE(CastHasPotentialNondeterminism(int_struct, types::JsonType()));
}

TEST(DeterminismTest, HasNonDeterministicArrayCastToJsonNullAndSameType) {
  Value unordered_array = InternalValue::ArrayNotChecked(
      types::Int64ArrayType(), InternalValue::kIgnoresOrder,
      {Int64(1), Int64(2)});
  EXPECT_FALSE(HasNonDeterministicArrayCastToJson(Null(types::Int64ArrayType()),
                                                  types::JsonType()));
  EXPECT_FALSE(HasNonDeterministicArrayCastToJson(unordered_array,
                                                  types::Int64ArrayType()));
}

TEST(DeterminismTest, HasNonDeterministicArrayCastToJsonArraysAndContainers) {
  Value ordered_array = Int64Array({1, 2});
  Value unordered_array = InternalValue::ArrayNotChecked(
      types::Int64ArrayType(), InternalValue::kIgnoresOrder,
      {Int64(1), Int64(2)});
  EXPECT_FALSE(
      HasNonDeterministicArrayCastToJson(ordered_array, types::JsonType()));
  EXPECT_TRUE(
      HasNonDeterministicArrayCastToJson(unordered_array, types::JsonType()));

  const StructType* struct_array_type =
      MakeStructType({{"a", types::Int64ArrayType()}});
  const StructType* struct_json_type =
      MakeStructType({{"a", types::JsonType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value ordered_struct,
                       Value::MakeStruct(struct_array_type, {ordered_array}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value unordered_struct,
                       Value::MakeStruct(struct_array_type, {unordered_array}));
  EXPECT_FALSE(
      HasNonDeterministicArrayCastToJson(ordered_struct, struct_json_type));
  EXPECT_TRUE(
      HasNonDeterministicArrayCastToJson(unordered_struct, struct_json_type));

  const ArrayType* array_struct_array_type = MakeArrayType(struct_array_type);
  const ArrayType* array_struct_json_type = MakeArrayType(struct_json_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_of_ordered_struct,
      Value::MakeArray(array_struct_array_type, {ordered_struct}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_of_unordered_struct,
      Value::MakeArray(array_struct_array_type, {unordered_struct}));
  EXPECT_FALSE(HasNonDeterministicArrayCastToJson(array_of_ordered_struct,
                                                  array_struct_json_type));
  EXPECT_TRUE(HasNonDeterministicArrayCastToJson(array_of_unordered_struct,
                                                 array_struct_json_type));
}

TEST(DeterminismTest, HasNonDeterministicArrayCastToJsonHandlesVariant) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_int_variant_type,
                       MakeMapType(types::Int64Type(), types::VariantType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_int_json_type,
                       MakeMapType(types::Int64Type(), types::JsonType()));
  Value ordered_array = Int64Array({1, 2});
  Value unordered_array = InternalValue::ArrayNotChecked(
      types::Int64ArrayType(), InternalValue::kIgnoresOrder,
      {Int64(1), Int64(2)});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value ordered_map,
      Value::MakeMap(map_int_variant_type,
                     {{Int64(1), test_values::Variant(ordered_array)}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value unordered_map,
      Value::MakeMap(map_int_variant_type,
                     {{Int64(1), test_values::Variant(unordered_array)}}));

  EXPECT_FALSE(HasNonDeterministicArrayCastToJson(
      test_values::Variant(ordered_array), types::JsonType()));
  EXPECT_TRUE(HasNonDeterministicArrayCastToJson(
      test_values::Variant(unordered_array), types::JsonType()));
  EXPECT_FALSE(
      HasNonDeterministicArrayCastToJson(ordered_map, map_int_json_type));
  EXPECT_TRUE(
      HasNonDeterministicArrayCastToJson(unordered_map, map_int_json_type));
}

}  // namespace
}  // namespace googlesql
