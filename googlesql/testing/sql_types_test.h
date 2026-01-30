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

#ifndef GOOGLESQL_TESTING_SQL_TYPES_TEST_H_
#define GOOGLESQL_TESTING_SQL_TYPES_TEST_H_

#include <memory>
#include <vector>

#include "googlesql/public/civil_time.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"

namespace googlesql {

using types::BoolType;
using types::BytesType;
using types::DateType;
using types::DoubleType;
using types::FloatType;
using types::Int32Type;
using types::Int64Type;
using types::NumericType;
using types::BigNumericType;
using types::StringType;
using types::DateType;
using types::TimeType;
using types::DatetimeType;
using types::TimestampType;
using types::Uint32Type;
using types::Uint64Type;

// Provides a base class for GoogleSQL unit tests, setting up convenience
// variables for types, literals, and nulls of different types.  It also
// populates lists of literal, non-literal, and NULL argument types.
class GoogleSQLTypesTest : public ::testing::Test {
 public:
  GoogleSQLTypesTest() = default;
  GoogleSQLTypesTest(const GoogleSQLTypesTest&) = delete;
  GoogleSQLTypesTest& operator=(const GoogleSQLTypesTest&) = delete;
  ~GoogleSQLTypesTest() override {}

#define BOOL (*bool_arg)
#define INT32 (*int32_arg)
#define INT64 (*int64_arg)
#define UINT32 (*uint32_arg)
#define UINT64 (*uint64_arg)
#define FLOAT (*float_arg)
#define DOUBLE (*double_arg)
#define NUMERIC (*numeric_arg)
#define BIGNUMERIC (*bignumeric_arg)
#define STRING (*string_arg)
#define BYTES (*bytes_arg)
#define DATE (*date_arg)
#define TIME (*time_arg)
#define DATETIME (*datetime_arg)
#define TIMESTAMP (*timestamp_arg)
#define GEOGRAPHY (*geography_arg)
#define ENUM (*enum_arg)
#define OPAQUE_ENUM (*opaque_enum_arg)
#define PROTO (*proto_arg)
#define ARRAY_INT32 (*array_int32_arg)
#define ARRAY_INT64 (*array_int64_arg)
#define ARRAY_STRUCT (*array_struct_arg)
#define STRUCT (*struct_arg)
#define UNTYPED_NULL (*untyped_null_arg_)

  // These are parameter non-literals.
#define BOOL_PARAMETER (*bool_parameter_arg)
#define INT32_PARAMETER (*int32_parameter_arg)
#define INT64_PARAMETER (*int64_parameter_arg)
#define UINT32_PARAMETER (*uint32_parameter_arg)
#define UINT64_PARAMETER (*uint64_parameter_arg)
#define FLOAT_PARAMETER (*float_parameter_arg)
#define DOUBLE_PARAMETER (*double_parameter_arg)
#define NUMERIC_PARAMETER (*numeric_parameter_arg)
#define BIGNUMERIC_PARAMETER (*bignumeric_parameter_arg)
#define STRING_PARAMETER (*string_parameter_arg)
#define BYTES_PARAMETER (*bytes_parameter_arg)
#define DATE_PARAMETER (*date_parameter_arg)
#define TIME_PARAMETER (*time_parameter_arg)
#define DATETIME_PARAMETER (*datetime_parameter_arg)
#define TIMESTAMP_PARAMETER (*timestamp_parameter_arg)
#define MILLIS_PARAMETER (*millis_parameter_arg)
#define MICROS_PARAMETER (*micros_parameter_arg)
#define ENUM_PARAMETER (*enum_parameter_arg)
#define OPAQUE_ENUM_PARAMETER (*opaque_enum_parameter_arg)
#define PROTO_PARAMETER (*proto_parameter_arg)
#define ARRAY_INT32_PARAMETER (*array_int32_parameter_arg)
#define ARRAY_INT64_PARAMETER (*array_int64_parameter_arg)
#define ARRAY_STRUCT_PARAMETER (*array_struct_parameter_arg)
#define STRUCT_PARAMETER (*struct_parameter_arg)

  // These are non-null literals.
#define BOOL_LITERAL (*bool_literal_arg)
#define INT32_LITERAL (*int32_literal_arg)
#define INT64_LITERAL (*int64_literal_arg)
#define UINT32_LITERAL (*uint32_literal_arg)
#define UINT64_LITERAL (*uint64_literal_arg)
#define FLOAT_LITERAL (*float_literal_arg)
#define DOUBLE_LITERAL (*double_literal_arg)
#define NUMERIC_LITERAL (*numeric_literal_arg)
#define BIGNUMERIC_LITERAL (*bignumeric_literal_arg)
#define STRING_LITERAL (*string_literal_arg)
#define BYTES_LITERAL (*bytes_literal_arg)
#define DATE_LITERAL (*date_literal_arg)
#define TIME_LITERAL (*time_literal_arg)
#define DATETIME_LITERAL (*datetime_literal_arg)
#define TIMESTAMP_LITERAL (*timestamp_literal_arg)
#define ENUM_LITERAL (*enum_literal_arg)
#define OPAQUE_ENUM_LITERAL (*opaque_enum_literal_arg)
#define PROTO_LITERAL (*proto_literal_arg)
#define ARRAY_INT32_LITERAL (*array_int32_literal_arg)
#define ARRAY_INT64_LITERAL (*array_int64_literal_arg)
#define ARRAY_STRUCT_LITERAL (*array_struct_literal_arg)
#define STRUCT_LITERAL (*struct_literal_arg)

#define BOOL_NULL (*bool_null_arg)
#define INT32_NULL (*int32_null_arg)
#define INT64_NULL (*int64_null_arg)
#define UINT32_NULL (*uint32_null_arg)
#define UINT64_NULL (*uint64_null_arg)
#define FLOAT_NULL (*float_null_arg)
#define DOUBLE_NULL (*double_null_arg)
#define NUMERIC_NULL (*numeric_null_arg)
#define BIGNUMERIC_NULL (*bignumeric_null_arg)
#define STRING_NULL (*string_null_arg)
#define BYTES_NULL (*bytes_null_arg)
#define DATE_NULL (*date_null_arg)
#define TIME_NULL (*time_null_arg)
#define DATETIME_NULL (*datetime_null_arg)
#define TIMESTAMP_NULL (*timestamp_null_arg)
#define MILLIS_NULL (*millis_null_arg)
#define MICROS_NULL (*micros_null_arg)
#define GEOGRAPHY_NULL (*geography_null_arg)
#define ENUM_NULL (*enum_null_arg)
#define OPAQUE_ENUM_NULL (*opaque_enum_null_arg)
#define PROTO_NULL (*proto_null_arg)
#define ARRAY_INT32_NULL (*array_int32_null_arg)
#define ARRAY_INT64_NULL (*array_int64_null_arg)
#define ARRAY_STRUCT_NULL (*array_struct_null_arg)
#define STRUCT_NULL (*struct_null_arg)

  void SetUp() override;

  // Get non-simple Types from test schema.
  const ArrayType* GetInt32ArrayType();
  const ArrayType* GetInt64ArrayType();
  const ArrayType* GetStructArrayType();
  const EnumType* GetTestEnumType();
  const EnumType* GetTestEnumAsOpaqueType();
  const EnumType* GetAnotherTestEnumType();
  const ProtoType* GetKitchenSinkNestedProtoType();
  const ProtoType* GetKitchenSinkNestedDatesProtoType();
  const StructType* GetSimpleStructType();

  // Returns the list of sample test types that includes all GoogleSQL simple
  // types and non-simple types from the test schema listed above.
  void GetSampleTestTypes(std::vector<const Type*>* sample_types);
  std::vector<const Type*> GetSampleTestTypes();

  // Gets all simple types.
  static void GetSimpleTypes(std::vector<const Type*>* simple_types);

  TypeFactory type_factory_;

  // Generates member variables, including const literal value, const null
  // value, and InputArgumentType of non-literal, literal, parameter, and
  // null argument types for the specified Type.
#define GEN_VARIABLES(ltype, utype, value) \
  const Value ltype##_value = value; \
  const Value ltype##_null = Value::Null##utype(); \
  std::unique_ptr<InputArgumentType> ltype##_arg; \
  std::unique_ptr<InputArgumentType> ltype##_parameter_arg; \
  std::unique_ptr<InputArgumentType> ltype##_literal_arg; \
  std::unique_ptr<InputArgumentType> ltype##_null_arg;

  GEN_VARIABLES(bool,       Bool,           Value::Bool(true));
  GEN_VARIABLES(int32, Int32, Value::Int32(3));
  GEN_VARIABLES(int64, Int64, Value::Int64(4));
  GEN_VARIABLES(uint32, Uint32, Value::Uint32(5));
  GEN_VARIABLES(uint64, Uint64, Value::Uint64(6));
  GEN_VARIABLES(float,      Float,          Value::Float(7.0));
  GEN_VARIABLES(double,     Double,         Value::Double(8.0));
  GEN_VARIABLES(numeric,    Numeric,        Value::Numeric(NumericValue(9LL)));
  GEN_VARIABLES(bignumeric, BigNumeric,
                Value::BigNumeric(BigNumericValue(10LL)));
  GEN_VARIABLES(string,     String,         Value::String("bbb"));
  GEN_VARIABLES(bytes,      Bytes,          Value::Bytes("aaa"));
  GEN_VARIABLES(date,       Date,           Value::Date(555));
  GEN_VARIABLES(time, Time,
                Value::Time(TimeValue::FromHMSAndMicros(12, 34, 56, 789)));
  GEN_VARIABLES(datetime, Datetime,
                Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(1987, 12, 31,
                                                                   23, 45, 21,
                                                                   987654)));
  GEN_VARIABLES(timestamp,  Timestamp,
                Value::TimestampFromUnixMicros(5678));
  GEN_VARIABLES(geography, Geography,        Value::EmptyGeography());

  // Defines variables but does not initialize them.
#define DEFINE_VARIABLES(ltype) \
  Value ltype##_value; \
  Value ltype##_null; \
  std::unique_ptr<InputArgumentType> ltype##_arg; \
  std::unique_ptr<InputArgumentType> ltype##_parameter_arg; \
  std::unique_ptr<InputArgumentType> ltype##_literal_arg; \
  std::unique_ptr<InputArgumentType> ltype##_null_arg;

  // Enum, Proto, Array, and Struct must be handled separately since they
  // require TypeFactory use.  They get initialized in SetUp().
  DEFINE_VARIABLES(enum);
  DEFINE_VARIABLES(opaque_enum);
  DEFINE_VARIABLES(proto);
  DEFINE_VARIABLES(array_int32);
  DEFINE_VARIABLES(array_int64);
  DEFINE_VARIABLES(array_struct);
  DEFINE_VARIABLES(struct);

  std::unique_ptr<InputArgumentType> untyped_null_arg_;

  absl::flat_hash_map<const Type*, const InputArgumentType*> literal_of_type_;
  absl::flat_hash_map<const Type*, const InputArgumentType*> null_of_type_;
  absl::flat_hash_map<const Type*, const InputArgumentType*> parameter_of_type_;

  std::vector<const InputArgumentType*> all_non_literal_args_;
  std::vector<const InputArgumentType*> all_parameter_args_;
  std::vector<const InputArgumentType*> all_literal_and_null_args_;
  std::vector<const InputArgumentType*> all_literal_args_;
  std::vector<const InputArgumentType*> all_null_args_;

  // All the types used in these InputArgumentTypes.
  std::vector<const Type*> all_types_;

  LanguageOptions language_options_;
  std::unique_ptr<Coercer> coercer_;

  const ArrayType*  int32_array_type_ = nullptr;
  const ArrayType*  int64_array_type_ = nullptr;
  const ArrayType* struct_array_type_ = nullptr;
  const EnumType*   enum_type_ = nullptr;
  const EnumType* enum_as_opaque_type_ = nullptr;
  const EnumType*   another_enum_type_ = nullptr;
  const ProtoType* kitchen_sink_nested_proto_type_ = nullptr;
  const ProtoType* kitchen_sink_nested_dates_proto_type_ = nullptr;
  const StructType* simple_struct_type_ = nullptr;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TESTING_SQL_TYPES_TEST_H_
