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

#include "googlesql/testing/sql_types_test.h"

#include <memory>
#include <vector>

#include "googlesql/public/coercer.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/base/check.h"
#include "absl/strings/cord.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

void GoogleSQLTypesTest::SetUp() {
  language_options_.EnableLanguageFeature(FEATURE_CAST_DIFFERENT_ARRAY_TYPES);
  language_options_.EnableLanguageFeature(
      FEATURE_IMPLICIT_COERCION_STRING_LITERAL_TO_BYTES);
  coercer_ = std::make_unique<Coercer>(&type_factory_, &language_options_);

  enum_value = Value::Enum(GetTestEnumType(), 1);
  enum_null = Value::Null(GetTestEnumType());
  opaque_enum_value = Value::Enum(GetTestEnumAsOpaqueType(), 1);
  opaque_enum_null = Value::Null(GetTestEnumAsOpaqueType());
  proto_value = Value::Proto(GetKitchenSinkNestedProtoType(), absl::Cord("1"));
  proto_null = Value::Null(GetKitchenSinkNestedProtoType());
  array_int32_value = Value::EmptyArray(GetInt32ArrayType());
  array_int64_value = Value::EmptyArray(GetInt64ArrayType());
  array_struct_value = Value::EmptyArray(GetStructArrayType());
  array_int32_null = Value::Null(GetInt32ArrayType());
  array_int64_null = Value::Null(GetInt64ArrayType());
  array_struct_null = Value::Null(GetStructArrayType());
  struct_value = Value::Struct(
      GetSimpleStructType(), {Value::String("aaa"), Value::Bytes("bbb")});
  struct_null = Value::Null(GetSimpleStructType());

#define RESET_ARGS(ltype, utype)                                           \
  ltype##_arg.reset(new InputArgumentType(utype##Type(), false));          \
  ltype##_literal_arg.reset(new InputArgumentType(ltype##_value));         \
  ltype##_parameter_arg.reset(new InputArgumentType(utype##Type(), true)); \
  ltype##_null_arg.reset(new InputArgumentType(ltype##_null));

  // Instantiates non-literal, literal, and null arguments of specified
  // type.
  RESET_ARGS(bool, Bool);
  RESET_ARGS(int32, Int32);
  RESET_ARGS(int64, Int64);
  RESET_ARGS(uint32, Uint32);
  RESET_ARGS(uint64, Uint64);
  RESET_ARGS(float, Float);
  RESET_ARGS(double, Double);
  RESET_ARGS(numeric, Numeric);
  RESET_ARGS(bignumeric, BigNumeric);
  RESET_ARGS(string, String);
  RESET_ARGS(bytes, Bytes);
  RESET_ARGS(date, Date);
  RESET_ARGS(time, Time);
  RESET_ARGS(datetime, Datetime);
  RESET_ARGS(timestamp, Timestamp);
  RESET_ARGS(geography, types::Geography);

  enum_arg = std::make_unique<InputArgumentType>(GetTestEnumType(),
                                                 false /* is_parameter */);
  enum_parameter_arg = std::make_unique<InputArgumentType>(
      GetTestEnumType(), true /* is_parameter */);
  enum_literal_arg = std::make_unique<InputArgumentType>(enum_value);
  enum_null_arg = std::make_unique<InputArgumentType>(enum_null);

  opaque_enum_arg = std::make_unique<InputArgumentType>(
      GetTestEnumAsOpaqueType(), false /* is_parameter */);
  opaque_enum_parameter_arg = std::make_unique<InputArgumentType>(
      GetTestEnumAsOpaqueType(), true /* is_parameter */);
  opaque_enum_literal_arg =
      std::make_unique<InputArgumentType>(opaque_enum_value);
  opaque_enum_null_arg = std::make_unique<InputArgumentType>(opaque_enum_null);
  opaque_enum_arg = std::make_unique<InputArgumentType>(
      GetTestEnumAsOpaqueType(), false /* is_parameter */);

  proto_arg = std::make_unique<InputArgumentType>(
      GetKitchenSinkNestedProtoType(), false /* is_parameter */);
  proto_parameter_arg = std::make_unique<InputArgumentType>(
      GetKitchenSinkNestedProtoType(), true /* is_parameter */);
  proto_literal_arg = std::make_unique<InputArgumentType>(proto_value);
  proto_null_arg = std::make_unique<InputArgumentType>(proto_null);

  array_int32_arg = std::make_unique<InputArgumentType>(
      GetInt32ArrayType(), false /* is_parameter */);
  array_int32_parameter_arg = std::make_unique<InputArgumentType>(
      GetInt32ArrayType(), true /* is_parameter */);
  array_int32_literal_arg =
      std::make_unique<InputArgumentType>(array_int32_value);
  array_int32_null_arg = std::make_unique<InputArgumentType>(array_int32_null);

  array_int64_arg = std::make_unique<InputArgumentType>(GetInt64ArrayType(),
                                                        /*is_parameter=*/false);
  array_int64_parameter_arg =
      std::make_unique<InputArgumentType>(GetInt64ArrayType(),
                                          /*is_parameter=*/true);
  array_int64_literal_arg =
      std::make_unique<InputArgumentType>(array_int64_value);
  array_int64_null_arg = std::make_unique<InputArgumentType>(array_int64_null);

  array_struct_arg =
      std::make_unique<InputArgumentType>(GetStructArrayType(),
                                          /*is_parameter=*/false);
  array_struct_parameter_arg =
      std::make_unique<InputArgumentType>(GetStructArrayType(),
                                          /*is_parameter=*/true);
  array_struct_literal_arg =
      std::make_unique<InputArgumentType>(array_struct_value);
  array_struct_null_arg =
      std::make_unique<InputArgumentType>(array_struct_null);

  std::vector<InputArgumentType> field_types;
  const StructType* struct_type = GetSimpleStructType();
  for (int i = 0; i < struct_type->num_fields(); ++i) {
    field_types.push_back(InputArgumentType(struct_type->field(i).type));
  }
  struct_arg = std::make_unique<InputArgumentType>(struct_type, field_types);
  struct_parameter_arg =
      std::make_unique<InputArgumentType>(struct_type, true /* is_parameter */);

  struct_literal_arg = std::make_unique<InputArgumentType>(struct_value);
  struct_null_arg = std::make_unique<InputArgumentType>(struct_null);

  untyped_null_arg_ = std::make_unique<InputArgumentType>();

  googlesql_base::InsertOrDie(&null_of_type_, BOOL.type(), &BOOL_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, INT32.type(), &INT32_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, INT64.type(), &INT64_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, UINT32.type(), &UINT32_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, UINT64.type(), &UINT64_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, FLOAT.type(), &FLOAT_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, DOUBLE.type(), &DOUBLE_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, NUMERIC.type(), &NUMERIC_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, BIGNUMERIC.type(), &BIGNUMERIC_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, STRING.type(), &STRING_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, BYTES.type(), &BYTES_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, DATE.type(), &DATE_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, TIME.type(), &TIME_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, DATETIME.type(), &DATETIME_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, TIMESTAMP.type(), &TIMESTAMP_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, GEOGRAPHY.type(), &GEOGRAPHY_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, ENUM.type(), &ENUM_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, OPAQUE_ENUM.type(), &OPAQUE_ENUM_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, PROTO.type(), &PROTO_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, ARRAY_INT32.type(), &ARRAY_INT32_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, ARRAY_INT64.type(), &ARRAY_INT64_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, ARRAY_STRUCT.type(), &ARRAY_STRUCT_NULL);
  googlesql_base::InsertOrDie(&null_of_type_, STRUCT.type(), &STRUCT_NULL);

  googlesql_base::InsertOrDie(&literal_of_type_, BOOL.type(), &BOOL_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, INT32.type(), &INT32_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, INT64.type(), &INT64_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, UINT32.type(), &UINT32_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, UINT64.type(), &UINT64_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, FLOAT.type(), &FLOAT_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, DOUBLE.type(), &DOUBLE_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, NUMERIC.type(), &NUMERIC_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, BIGNUMERIC.type(), &BIGNUMERIC_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, STRING.type(), &STRING_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, BYTES.type(), &BYTES_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, DATE.type(), &DATE_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, TIME.type(), &TIME_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, DATETIME.type(), &DATETIME_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, TIMESTAMP.type(), &TIMESTAMP_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, ENUM.type(), &ENUM_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, OPAQUE_ENUM.type(), &OPAQUE_ENUM_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, PROTO.type(), &PROTO_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, ARRAY_INT32.type(), &ARRAY_INT32_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, ARRAY_INT64.type(), &ARRAY_INT64_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, ARRAY_STRUCT.type(),
                   &ARRAY_STRUCT_LITERAL);
  googlesql_base::InsertOrDie(&literal_of_type_, STRUCT.type(), &STRUCT_LITERAL);

  googlesql_base::InsertOrDie(&parameter_of_type_, BOOL.type(), &BOOL_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, INT32.type(), &INT32_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, INT64.type(), &INT64_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, UINT32.type(), &UINT32_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, UINT64.type(), &UINT64_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, FLOAT.type(), &FLOAT_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, DOUBLE.type(), &DOUBLE_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, NUMERIC.type(), &NUMERIC_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, BIGNUMERIC.type(),
                   &BIGNUMERIC_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, STRING.type(), &STRING_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, BYTES.type(), &BYTES_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, DATE.type(), &DATE_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, TIME.type(), &TIME_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, DATETIME.type(), &DATETIME_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, TIMESTAMP.type(), &TIMESTAMP_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, ENUM.type(), &ENUM_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, OPAQUE_ENUM.type(),
                   &OPAQUE_ENUM_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, PROTO.type(), &PROTO_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, ARRAY_INT32.type(),
                   &ARRAY_INT32_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, ARRAY_INT64.type(),
                   &ARRAY_INT64_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, ARRAY_STRUCT.type(),
                   &ARRAY_STRUCT_PARAMETER);
  googlesql_base::InsertOrDie(&parameter_of_type_, STRUCT.type(), &STRUCT_PARAMETER);

  all_non_literal_args_ = {
      &BOOL,        &INT32,       &INT64,        &UINT32, &UINT64,      &FLOAT,
      &DOUBLE,      &NUMERIC,     &BIGNUMERIC,   &STRING, &BYTES,       &DATE,
      &TIME,        &DATETIME,    &TIMESTAMP,    &ENUM,   &OPAQUE_ENUM, &PROTO,
      &ARRAY_INT32, &ARRAY_INT64, &ARRAY_STRUCT, &STRUCT};

  all_literal_args_ = {
      &BOOL_LITERAL,        &INT32_LITERAL,       &INT64_LITERAL,
      &UINT32_LITERAL,      &UINT64_LITERAL,      &FLOAT_LITERAL,
      &DOUBLE_LITERAL,      &NUMERIC_LITERAL,     &BIGNUMERIC_LITERAL,
      &STRING_LITERAL,      &BYTES_LITERAL,       &DATE_LITERAL,
      &TIME_LITERAL,        &DATETIME_LITERAL,    &TIMESTAMP_LITERAL,
      &ENUM_LITERAL,        &OPAQUE_ENUM_LITERAL, &PROTO_LITERAL,
      &ARRAY_INT32_LITERAL, &ARRAY_INT64_LITERAL, &ARRAY_STRUCT_LITERAL,
      &STRUCT_LITERAL};

  all_parameter_args_ = {
      &BOOL_PARAMETER,        &INT32_PARAMETER,       &INT64_PARAMETER,
      &UINT32_PARAMETER,      &UINT64_PARAMETER,      &FLOAT_PARAMETER,
      &DOUBLE_PARAMETER,      &NUMERIC_PARAMETER,     &BIGNUMERIC_PARAMETER,
      &STRING_PARAMETER,      &BYTES_PARAMETER,       &DATE_PARAMETER,
      &TIME_PARAMETER,        &DATETIME_PARAMETER,    &TIMESTAMP_PARAMETER,
      &ENUM_PARAMETER,        &OPAQUE_ENUM_PARAMETER, &PROTO_PARAMETER,
      &ARRAY_INT32_PARAMETER, &ARRAY_INT64_PARAMETER, &ARRAY_STRUCT_PARAMETER,
      &STRUCT_PARAMETER};

  all_null_args_ = {
      &BOOL_NULL,        &INT32_NULL,        &INT64_NULL,     &UINT32_NULL,
      &UINT64_NULL,      &FLOAT_NULL,        &DOUBLE_NULL,    &NUMERIC_NULL,
      &BIGNUMERIC_NULL,  &STRING_NULL,       &BYTES_NULL,     &DATE_NULL,
      &TIME_NULL,        &DATETIME_NULL,     &TIMESTAMP_NULL, &GEOGRAPHY_NULL,
      &ENUM_NULL,        &OPAQUE_ENUM_NULL,  &PROTO_NULL,     &ARRAY_INT32_NULL,
      &ARRAY_INT64_NULL, &ARRAY_STRUCT_NULL, &STRUCT_NULL,    &UNTYPED_NULL};

  all_literal_and_null_args_ = all_literal_args_;
  all_literal_and_null_args_.insert(all_literal_and_null_args_.end(),
                                    all_null_args_.begin(),
                                    all_null_args_.end());

  all_types_ = {BOOL.type(),
                INT32.type(),
                INT64.type(),
                UINT32.type(),
                UINT64.type(),
                FLOAT.type(),
                DOUBLE.type(),
                NUMERIC.type(),
                BIGNUMERIC.type(),
                STRING.type(),
                BYTES.type(),
                DATE.type(),
                TIME.type(),
                DATETIME.type(),
                TIMESTAMP.type(),
                GEOGRAPHY.type(),
                ARRAY_INT32.type(),
                ARRAY_INT64.type(),
                ARRAY_STRUCT.type(),
                GetTestEnumType(),
                GetTestEnumAsOpaqueType(),
                GetAnotherTestEnumType(),
                GetKitchenSinkNestedProtoType(),
                GetKitchenSinkNestedDatesProtoType(),
                GetSimpleStructType()};
}

const ArrayType* GoogleSQLTypesTest::GetInt32ArrayType() {
  if (int32_array_type_ == nullptr) {
    GOOGLESQL_CHECK_OK(type_factory_.MakeArrayType(type_factory_.get_int32(),
                                         &int32_array_type_));
  }
  return int32_array_type_;
}

const ArrayType* GoogleSQLTypesTest::GetInt64ArrayType() {
  if (int64_array_type_ == nullptr) {
    GOOGLESQL_CHECK_OK(type_factory_.MakeArrayType(type_factory_.get_int64(),
                                         &int64_array_type_));
  }
  return int64_array_type_;
}

const ArrayType* GoogleSQLTypesTest::GetStructArrayType() {
  if (struct_array_type_ == nullptr) {
    GOOGLESQL_CHECK_OK(type_factory_.MakeArrayType(GetSimpleStructType(),
                                         &struct_array_type_));
  }
  return struct_array_type_;
}

const EnumType* GoogleSQLTypesTest::GetTestEnumType() {
  if (enum_type_ == nullptr) {
    const google::protobuf::EnumDescriptor* enum_descriptor =
        googlesql_test::TestEnum_descriptor();
    GOOGLESQL_CHECK_OK(type_factory_.MakeEnumType(enum_descriptor, &enum_type_));
  }
  return enum_type_;
}

const EnumType* GoogleSQLTypesTest::GetTestEnumAsOpaqueType() {
  if (enum_as_opaque_type_ == nullptr) {
    const google::protobuf::EnumDescriptor* enum_descriptor =
        googlesql_test::TestEnum_descriptor();
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(
        &type_factory_, enum_descriptor, &enum_as_opaque_type_, {}));
  }
  return enum_as_opaque_type_;
}

const EnumType* GoogleSQLTypesTest::GetAnotherTestEnumType() {
  if (another_enum_type_ == nullptr) {
    const google::protobuf::EnumDescriptor* enum_descriptor =
        googlesql_test::AnotherTestEnum_descriptor();
    GOOGLESQL_CHECK_OK(type_factory_.MakeEnumType(enum_descriptor, &another_enum_type_));
  }
  return another_enum_type_;
}

const ProtoType* GoogleSQLTypesTest::GetKitchenSinkNestedProtoType() {
  if (kitchen_sink_nested_proto_type_ == nullptr) {
    GOOGLESQL_CHECK_OK(type_factory_.MakeProtoType(
        googlesql_test::KitchenSinkPB::Nested::descriptor(),
        &kitchen_sink_nested_proto_type_));
  }
  return kitchen_sink_nested_proto_type_;
}

const ProtoType* GoogleSQLTypesTest::GetKitchenSinkNestedDatesProtoType() {
  if (kitchen_sink_nested_dates_proto_type_ == nullptr) {
    GOOGLESQL_CHECK_OK(type_factory_.MakeProtoType(
        googlesql_test::KitchenSinkPB::NestedDates::descriptor(),
        &kitchen_sink_nested_dates_proto_type_));
  }
  return kitchen_sink_nested_dates_proto_type_;
}

const StructType* GoogleSQLTypesTest::GetSimpleStructType() {
  if (simple_struct_type_ == nullptr) {
    const Type* string_type = type_factory_.get_string();
    const Type* bytes_type = type_factory_.get_bytes();
    GOOGLESQL_CHECK_OK(type_factory_.MakeStructType(
        {{"a", string_type}, {"b", bytes_type}}, &simple_struct_type_));
  }
  return simple_struct_type_;
}

void GoogleSQLTypesTest::GetSampleTestTypes(
    std::vector<const Type*>* sample_types) {
  GetSimpleTypes(sample_types);

  sample_types->push_back(GetInt32ArrayType());
  sample_types->push_back(GetStructArrayType());
  sample_types->push_back(GetTestEnumType());
  sample_types->push_back(GetTestEnumAsOpaqueType());
  sample_types->push_back(GetAnotherTestEnumType());
  sample_types->push_back(GetKitchenSinkNestedProtoType());
  sample_types->push_back(GetKitchenSinkNestedDatesProtoType());
  sample_types->push_back(GetSimpleStructType());
}

std::vector<const Type*> GoogleSQLTypesTest::GetSampleTestTypes() {
  std::vector<const Type*> result;
  GetSampleTestTypes(&result);
  return result;
}

void GoogleSQLTypesTest::GetSimpleTypes(
    std::vector<const Type*>* simple_types) {
  for (int i = TypeKind::TYPE_UNKNOWN; i < TypeKind_ARRAYSIZE; i++) {
    TypeKind kind = static_cast<TypeKind>(i);
    const Type* type = types::TypeFromSimpleTypeKind(kind);
    if (type == nullptr) {
      continue;
    }

    simple_types->push_back(type);
  }
}

}  // namespace googlesql
