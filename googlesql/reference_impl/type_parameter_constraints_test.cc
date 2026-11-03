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

#include "googlesql/reference_impl/type_parameter_constraints.h"

#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/declarative_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/types/vector_value_codec.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

using ::googlesql::BigNumericValue;
using ::googlesql::NumericValue;

using ::testing::HasSubstr;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

// A string with UTF-8 length of 7 and a byte length of 14. String says "Google"
// in Perso-Arabic script with 4 letters and 3 vowel diacritics (hence a UTF-8
// length of 7). We use this string so that its UTF-8 length differs from its
// byte length.
constexpr absl::string_view TEST_STRING = "گُوگِلْ";

TEST(TypeParametersTest, StringWithMaxLengthOk) {
  Value string_value = Value::String(TEST_STRING);
  StringTypeParametersProto proto;
  proto.set_max_length(7);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeStringTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, string_value),
              IsOk());
}

TEST(TypeParametersTest, StringWithMaxLengthFails) {
  Value string_value = Value::String(TEST_STRING);
  StringTypeParametersProto proto;
  proto.set_max_length(6);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeStringTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, string_value),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("maximum length 6 but got a value with length 7")));
}

TEST(TypeParametersTest, BytesWithMaxLengthOk) {
  Value bytes_value = Value::Bytes(TEST_STRING);
  StringTypeParametersProto proto;
  proto.set_max_length(14);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeStringTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, bytes_value),
              IsOk());
}

TEST(TypeParametersTest, BytesWithMaxLengthFails) {
  Value bytes_value = Value::Bytes(TEST_STRING);
  StringTypeParametersProto proto;
  proto.set_max_length(13);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeStringTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, bytes_value),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("maximum length 13 but got a value with length 14")));
}

TEST(TypeParametersTest, NumericEveryValidIntegerPrecisionAndScaleSucceeds) {
  Value numeric_value = Value::Numeric(NumericValue(1));
  for (int scale = 0; scale <= NumericValue::kMaxFractionalDigits; scale++) {
    int max_precision = NumericValue::kMaxIntegerDigits + scale;
    for (int precision = 1 + scale; precision <= max_precision; precision++) {
      NumericTypeParametersProto proto;
      proto.set_precision(precision);
      proto.set_scale(scale);
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                           TypeParameters::MakeNumericTypeParameters(proto));
      EXPECT_THAT(
          ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value),
          IsOk());
    }
  }
}

TEST(TypeParametersTest, NumericEveryValidFractionalScaleSucceeds) {
  Value numeric_value = Value::Numeric(NumericValue::FromString("0.1").value());
  for (int scale = 1; scale <= NumericValue::kMaxFractionalDigits; scale++) {
    NumericTypeParametersProto proto;
    proto.set_precision(scale);
    proto.set_scale(scale);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                         TypeParameters::MakeNumericTypeParameters(proto));
    EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value),
                IsOk());
  }
}

TEST(TypeParametersTest, NumericPrecisionScaleFails) {
  Value numeric_value = Value::Numeric(NumericValue(10000));
  NumericTypeParametersProto proto;
  proto.set_precision(4);
  proto.set_scale(0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "precision 4 and scale 0 but got a value that is not in range")));
}

TEST(TypeParametersTest, NumericPrecisionScaleRoundsOk) {
  Value numeric_value =
      Value::Numeric(NumericValue::FromString("999999.994999999").value());
  NumericTypeParametersProto proto;
  proto.set_precision(8);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  GOOGLESQL_ASSERT_OK(ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value));
  EXPECT_EQ(numeric_value.numeric_value(),
            NumericValue::FromString("999999.99").value());
}

TEST(TypeParametersTest, NumericPrecisionScaleRoundFails) {
  Value numeric_value =
      Value::Numeric(NumericValue::FromString("999999.995").value());
  NumericTypeParametersProto proto;
  proto.set_precision(8);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "precision 8 and scale 2 but got a value that is not in range")));
}

TEST(TypeParametersTest, NumericPrecisionScaleMaxOverflowFails) {
  Value numeric_value = Value::Numeric(
      NumericValue::FromString("99999999999999999999999999999.999999999")
          .value());
  NumericTypeParametersProto proto;
  proto.set_precision(31);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("numeric overflow")));
}

TEST(TypeParametersTest, NumericInvalidPrecisionScaleFails) {
  Value numeric_value = Value::Numeric(NumericValue(1));
  NumericTypeParametersProto proto;
  proto.set_precision(35);
  proto.set_scale(5);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, numeric_value),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("In NUMERIC(P, 5), P must be between 5 and "
                                 "34, actual precision: 35")));
}

TEST(TypeParametersTest, BigNumericEveryValidIntegerPrecisionAndScaleSucceeds) {
  Value bignumeric_value = Value::BigNumeric(BigNumericValue(1));
  for (int scale = 0; scale <= BigNumericValue::kMaxFractionalDigits; scale++) {
    int max_precision = BigNumericValue::kMaxIntegerDigits + scale;
    for (int precision = 1 + scale; precision < max_precision; precision++) {
      NumericTypeParametersProto proto;
      proto.set_precision(precision);
      proto.set_scale(scale);
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                           TypeParameters::MakeNumericTypeParameters(proto));
      EXPECT_THAT(
          ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value),
          IsOk());
    }
  }
}

TEST(TypeParametersTest, BigNumericEveryValidFractionalScaleSucceeds) {
  Value bignumeric_value =
      Value::BigNumeric(BigNumericValue::FromString("0.1").value());
  for (int scale = 1; scale <= BigNumericValue::kMaxFractionalDigits; scale++) {
    NumericTypeParametersProto proto;
    proto.set_precision(scale);
    proto.set_scale(scale);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                         TypeParameters::MakeNumericTypeParameters(proto));
    EXPECT_THAT(
        ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value),
        IsOk());
  }
}

TEST(TypeParametersTest, BigNumericPrecisionScaleFails) {
  Value bignumeric_value = Value::BigNumeric(BigNumericValue(10000));
  NumericTypeParametersProto proto;
  proto.set_precision(4);
  proto.set_scale(0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "precision 4 and scale 0 but got a value that is not in range")));
}

TEST(TypeParametersTest, BigNumericMaxPrecisionFails) {
  Value bignumeric_value =
      Value::BigNumeric(BigNumericValue::FromString(
                            "312345678901234567890123456789012345678.987654321")
                            .value());
  NumericTypeParametersProto proto;
  proto.set_precision(76);
  proto.set_scale(38);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("precision 76 and scale 38 but got a value "
                                 "that is not in range")));
}

TEST(TypeParametersTest, BigNumericMaxLiteralSucceeds) {
  Value bignumeric_value =
      Value::BigNumeric(BigNumericValue::FromString(
                            "312345678901234567890123456789012345678.987654321")
                            .value());
  NumericTypeParametersProto proto;
  proto.set_is_max_precision(true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  GOOGLESQL_ASSERT_OK(ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value));
  EXPECT_EQ(bignumeric_value.bignumeric_value(),
            BigNumericValue::FromString(
                "312345678901234567890123456789012345678.987654321")
                .value());
}

TEST(TypeParametersTest, BigNumericMaxLiteralRoundingSucceeds) {
  Value bignumeric_value =
      Value::BigNumeric(BigNumericValue::FromString(
                            "312345678901234567890123456789012345678.987654321")
                            .value());
  NumericTypeParametersProto proto;
  proto.set_is_max_precision(true);
  proto.set_scale(4);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  GOOGLESQL_ASSERT_OK(ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value));
  EXPECT_EQ(bignumeric_value.bignumeric_value(),
            BigNumericValue::FromString(
                "312345678901234567890123456789012345678.9877")
                .value());
}

TEST(TypeParametersTest, BigNumericMaxLiteralMaxValueSucceeds) {
  Value bignumeric_value = Value::BigNumeric(BigNumericValue::MaxValue());
  NumericTypeParametersProto proto;
  proto.set_is_max_precision(true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  GOOGLESQL_ASSERT_OK(ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value));
  EXPECT_EQ(bignumeric_value.bignumeric_value(), BigNumericValue::MaxValue());
}

TEST(TypeParametersTest, BigNumericPrecisionScaleRoundsOk) {
  Value bignumeric_value = Value::BigNumeric(
      BigNumericValue::FromString("999999.994999999").value());
  NumericTypeParametersProto proto;
  proto.set_precision(8);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  GOOGLESQL_ASSERT_OK(ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value));
  EXPECT_EQ(bignumeric_value.bignumeric_value(),
            BigNumericValue::FromString("999999.99").value());
}

TEST(TypeParametersTest, BigNumericPrecisionScaleRoundFails) {
  Value bignumeric_value =
      Value::BigNumeric(BigNumericValue::FromString("999999.995").value());
  NumericTypeParametersProto proto;
  proto.set_precision(8);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "precision 8 and scale 2 but got a value that is not in range")));
}

TEST(TypeParametersTest, BigNumericInvalidPrecisionScaleFails) {
  Value bignumeric_value = Value::BigNumeric(BigNumericValue(1));
  NumericTypeParametersProto proto;
  proto.set_precision(55);
  proto.set_scale(5);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeNumericTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, bignumeric_value),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("In BIGNUMERIC(P, 5), P must be between 5 and "
                                 "43, actual precision: 55")));
}

TEST(TypeParametersTest, ArrayWithTypeParametersOk) {
  // Create string array.
  TypeFactory type_factory;
  const Type* numeric_array = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeArrayType(type_factory.get_numeric(), &numeric_array));
  Value array_value =
      Value::Array(numeric_array->AsArray(),
                   {Value::Numeric(NumericValue::FromString("123.987").value()),
                    Value::Numeric(NumericValue(2))});

  // Create array type parameters.
  std::vector<TypeParameters> child_list;
  NumericTypeParametersProto proto;
  proto.set_precision(10);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters numeric_child,
                       TypeParameters::MakeNumericTypeParameters(proto));
  child_list.push_back(numeric_child);
  TypeParameters array_type_params =
      TypeParameters::MakeTypeParametersWithChildList(child_list);

  EXPECT_THAT(
      ApplyConstraints(array_type_params, PRODUCT_INTERNAL, array_value),
      IsOk());
  EXPECT_EQ(array_value.element(0).numeric_value(),
            NumericValue::FromString("123.99").value());
}

TEST(TypeParametersTest, ArrayWithTypeParametersFails) {
  // Create string array.
  TypeFactory type_factory;
  const Type* numeric_array = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeArrayType(type_factory.get_numeric(), &numeric_array));
  Value array_value =
      Value::Array(numeric_array->AsArray(),
                   {Value::Numeric(NumericValue::FromString("123.987").value()),
                    Value::Numeric(NumericValue(2))});

  // Create array type parameters.
  std::vector<TypeParameters> child_list;
  NumericTypeParametersProto proto;
  proto.set_precision(3);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters numeric_child,
                       TypeParameters::MakeNumericTypeParameters(proto));
  child_list.push_back(numeric_child);
  TypeParameters array_type_params =
      TypeParameters::MakeTypeParametersWithChildList(child_list);

  EXPECT_THAT(
      ApplyConstraints(array_type_params, PRODUCT_INTERNAL, array_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "precision 3 and scale 2 but got a value that is not in range")));
}

TEST(TypeParametersTest, StructWithTypeParametersOk) {
  // Create struct value.
  TypeFactory type_factory;
  const Type* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeStructType({{"string", type_factory.get_string()},
                                   {"numeric", type_factory.get_numeric()}},
                                  &struct_type));
  Value struct_value =
      Value::Struct(struct_type->AsStruct(),
                    {Value::String("hello"),
                     Value::Numeric(NumericValue::FromString("1.67").value())});

  // Create struct type parameters.
  std::vector<TypeParameters> child_list;
  NumericTypeParametersProto proto;
  proto.set_precision(2);
  proto.set_scale(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters numeric_child,
                       TypeParameters::MakeNumericTypeParameters(proto));
  child_list.push_back(TypeParameters());
  child_list.push_back(numeric_child);
  TypeParameters struct_type_params =
      TypeParameters::MakeTypeParametersWithChildList(child_list);

  EXPECT_THAT(
      ApplyConstraints(struct_type_params, PRODUCT_INTERNAL, struct_value),
      IsOk());
  EXPECT_EQ(struct_value.field(1).numeric_value(),
            NumericValue::FromString("1.7").value());
}

TEST(TypeParametersTest, StructWithTypeParametersFails) {
  // Create struct value.
  TypeFactory type_factory;
  const Type* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeStructType({{"string", type_factory.get_string()},
                                   {"numeric", type_factory.get_numeric()}},
                                  &struct_type));
  Value struct_value =
      Value::Struct(struct_type->AsStruct(),
                    {Value::String("hello"),
                     Value::Numeric(NumericValue::FromString("1.67").value())});

  // Create struct type parameters.
  std::vector<TypeParameters> child_list;
  NumericTypeParametersProto proto;
  proto.set_precision(2);
  proto.set_scale(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters numeric_child,
                       TypeParameters::MakeNumericTypeParameters(proto));
  child_list.push_back(TypeParameters());
  child_list.push_back(numeric_child);
  TypeParameters struct_type_params =
      TypeParameters::MakeTypeParametersWithChildList(child_list);

  EXPECT_THAT(
      ApplyConstraints(struct_type_params, PRODUCT_INTERNAL, struct_value),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "precision 2 and scale 2 but got a value that is not in range")));
}

absl::StatusOr<Value> MakeVectorValue(
    TypeFactory* type_factory, absl::Span<const float> elements,
    VectorEncodingId::Id encoding = VectorEncodingId::FLOAT32) {
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* vector_type, MakeVectorType(type_factory));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string vector_bytes,
      MakeVectorValueBytes(encoding, MakeFloat32VectorPayload(elements)));
  return Value::Declarative(vector_type->AsDeclarativeType(),
                            Value::Bytes(std::move(vector_bytes)));
}

TEST(TypeParametersTest, VectorWithDimsOk) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector_val,
                       MakeVectorValue(&type_factory, {1.0f, 2.0f, 3.0f}));
  VectorTypeParametersProto proto;
  proto.set_dims(3);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, vector_val),
              IsOk());
}

TEST(TypeParametersTest, VectorWithDimsFails) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector_val,
                       MakeVectorValue(&type_factory, {1.0f, 2.0f}));
  VectorTypeParametersProto proto;
  proto.set_dims(3);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, vector_val),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("VECTOR(3) has dims 3 but got a vector with dims 2")));
}

TEST(TypeParametersTest, VectorNullWithDimsOk) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));
  Value vector_null = Value::Null(vector_type);
  VectorTypeParametersProto proto;
  proto.set_dims(3);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, vector_null),
              IsOk());
}

TEST(TypeParametersTest, VectorWithoutTypeParametersOk) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector_val,
                       MakeVectorValue(&type_factory, {1.0f, 2.0f}));
  TypeParameters type_params;
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, vector_val),
              IsOk());
}

TEST(TypeParametersTest, ArrayOfVectorWithTypeParametersOk) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(vector_type, &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v1, MakeVectorValue(&type_factory, {1.0f, 2.0f}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v2, MakeVectorValue(&type_factory, {3.0f, 4.0f}));
  Value array_val = Value::Array(array_type, {v1, v2});

  VectorTypeParametersProto proto;
  proto.set_dims(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters vector_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  TypeParameters array_params =
      TypeParameters::MakeTypeParametersWithChildList({vector_params});
  EXPECT_THAT(ApplyConstraints(array_params, PRODUCT_INTERNAL, array_val),
              IsOk());
}

TEST(TypeParametersTest, ArrayOfVectorWithTypeParametersFails) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(vector_type, &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v1, MakeVectorValue(&type_factory, {1.0f, 2.0f}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value v2,
                       MakeVectorValue(&type_factory, {3.0f, 4.0f, 5.0f}));
  Value array_val = Value::Array(array_type, {v1, v2});

  VectorTypeParametersProto proto;
  proto.set_dims(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters vector_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  TypeParameters array_params =
      TypeParameters::MakeTypeParametersWithChildList({vector_params});
  EXPECT_THAT(
      ApplyConstraints(array_params, PRODUCT_INTERNAL, array_val),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("VECTOR(2) has dims 2 but got a vector with dims 3")));
}

TEST(TypeParametersTest, VectorWithFloat32EncodingOk) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value vector_val,
                       MakeVectorValue(&type_factory, {1.0f, 2.0f}));
  VectorTypeParametersProto proto;
  proto.set_dims(2);
  proto.set_encoding(googlesql::VectorEncodingId::FLOAT32);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeVectorTypeParameters(proto));
  EXPECT_THAT(ApplyConstraints(type_params, PRODUCT_INTERNAL, vector_val),
              IsOk());
}

TEST(TypeParametersTest, NonVectorDeclarativeTypeWithParametersFails) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers,
      TypeParameterHandlers::Create(
          [](const std::vector<TypeParameterValue>&, ProductMode)
              -> absl::StatusOr<TypeParameters> { return TypeParameters(); },
          [](const TypeParameters&, ProductMode) -> absl::Status {
            return absl::OkStatus();
          }));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* custom_type,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id(DeclarativeTypeId{"test_ns", "MyCustomType"})
              .set_display_name("MyCustomType")
              .set_backing_type(type_factory.get_string())
              .set_type_params_strategy(handlers)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value custom_val,
                       Value::Declarative(custom_type->AsDeclarativeType(),
                                          Value::String("foo")));
  StringTypeParametersProto proto;
  proto.set_max_length(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_params,
                       TypeParameters::MakeStringTypeParameters(proto));
  EXPECT_THAT(
      ApplyConstraints(type_params, PRODUCT_INTERNAL, custom_val),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Type parameters are not handled for type MyCustomType")));
}

}  // namespace
}  // namespace googlesql
