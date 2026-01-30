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

#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/unknown_field_set.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/interval_value_test_util.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/token_list_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/base/casts.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

static google::protobuf::DescriptorPoolDatabase* s_descriptor_pool_database_ = nullptr;
static google::protobuf::DescriptorPool* s_descriptor_pool_ = nullptr;

// Descriptor pool database and pool in which to allocate some protos and
// enums.  These allocated protos/enums will be used to create GoogleSQL
// Types, and those Types will be used to test equivalence with corresponding
// Types whose proto/enum descriptors are from their <name>_descriptor()
// function call (e.g., googlesql_test::TestEnum_descriptor()).
static google::protobuf::DescriptorPoolDatabase* local_descriptor_pool_database() {
  if (!s_descriptor_pool_database_) {
    s_descriptor_pool_database_ = new google::protobuf::DescriptorPoolDatabase(
        *google::protobuf::DescriptorPool::generated_pool());
  }
  return s_descriptor_pool_database_;
}
static google::protobuf::DescriptorPool* local_descriptor_pool() {
  if (!s_descriptor_pool_) {
    s_descriptor_pool_ =
        new google::protobuf::DescriptorPool(local_descriptor_pool_database());
  }
  return s_descriptor_pool_;
}

static const EnumType* TestEnumType_equivalent() {
  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(local_descriptor_pool()->
               FindEnumTypeByName("googlesql_test.TestEnum"), &enum_type));
  return enum_type;
}

static const EnumType* AnotherEnumType() {
  const EnumType* enum_type;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      googlesql_test::AnotherTestEnum_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &enum_type));
  return enum_type;
}

static const ProtoType* KitchenSinkProtoType_equivalent() {
  const ProtoType* proto_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(local_descriptor_pool()->
               FindMessageTypeByName("googlesql_test.KitchenSinkPB"),
                                     &proto_type));
  return proto_type;
}

static const StructType* AnotherStructType2() {
  const StructType* struct_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}, {"b", BoolType()}}, &struct_type));
  return struct_type;
}

static const StructType* AnotherStructType3() {
  const StructType* struct_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}, {"b", TestEnumType()}}, &struct_type));
  return struct_type;
}

static const ArrayType* TestEnumArrayType() {
  const ArrayType* array_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeArrayType(TestEnumType(), &array_type));
  return array_type;
}

static const ArrayType* TestEnumArrayType_equivalent() {
  const ArrayType* array_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeArrayType(TestEnumType_equivalent(),
                                         &array_type));
  return array_type;
}

static const ProtoType* StringInt32MapEntryType() {
  const ProtoType* ret;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      googlesql_test::MessageWithMapField::descriptor()
          ->FindFieldByName("string_int32_map")
          ->message_type(),
      &ret));
  return ret;
}

static const ProtoType* Uint64StringMapEntryType() {
  const ProtoType* ret;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      googlesql_test::MessageWithMapField::descriptor()
          ->FindFieldByName("uint64_string_map")
          ->message_type(),
      &ret));
  return ret;
}

template <typename Type>
static std::vector<Type> ConcatTests(
    absl::Span<const std::vector<Type>> test_vectors) {
  std::vector<Type> result;
  for (const auto& v : test_vectors) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

static Value KitchenSink_equivalent(absl::string_view proto_str) {
  googlesql_test::KitchenSinkPB kitchen_sink_message;
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(proto_str,
                                            &kitchen_sink_message));
  absl::Cord bytes;
  ABSL_CHECK(kitchen_sink_message.SerializeToCord(&bytes));
  return Value::Proto(KitchenSinkProtoType_equivalent(), bytes);
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastString() {
  return {
      // String -> String
      {{NullString()}, NullString()},
      {{String("")}, String("")},
      {{String("a")}, String("a")},
      // Bytes -> Bytes
      {{NullBytes()}, NullBytes()},
      {{Bytes("")}, Bytes("")},
      {{Bytes("a")}, Bytes("a")},
      {{Bytes("\xd7")}, Bytes("\xd7")},  // Invalid UTF8 is OK for Bytes.
      // String -> Bytes
      {{NullString()}, NullBytes()},
      {{String("")}, Bytes("")},
      {{String("a")}, Bytes("a")},
      {{String("\x00")}, Bytes("\x00")},
      // Bytes -> String
      {{NullBytes()}, NullString()},
      {{Bytes("")}, String("")},
      {{Bytes("a")}, String("a")},
      {{Bytes("\x00\x0a")}, String("\x00\x0a")},
      {{Bytes("\xd7")}, NullString(), OUT_OF_RANGE},  // Invalid UTF8
      // String -> Double
      {{String("infinity")}, double_pos_inf},
      {{String("+infinity")}, double_pos_inf},
      {{String("-infinity")}, double_neg_inf},
      {{String("inf")}, double_pos_inf},
      {{String("+inf")}, double_pos_inf},
      {{String("-inf")}, double_neg_inf},
      {{String("nan")}, double_nan},
      {{String("+nan")}, double_nan},
      {{String("-nan")}, double_nan},
      // String -> Float
      {{String("infinity")}, float_pos_inf},
      {{String("+infinity")}, float_pos_inf},
      {{String("-infinity")}, float_neg_inf},
      {{String("inf")}, float_pos_inf},
      {{String("+inf")}, float_pos_inf},
      {{String("-inf")}, float_neg_inf},
      {{String("nan")}, float_nan},
      {{String("+nan")}, float_nan},
      {{String("-nan")}, float_nan},
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastBytesStringWithFormat() {
  std::vector<FunctionTestCall> function_calls =
      GetFunctionTestsBytesStringConversion();
  std::vector<QueryParamsWithResult> tests;

  tests.reserve(function_calls.size());
  for (auto& function_call : function_calls) {
    tests.push_back(QueryParamsWithResult(function_call.params)
                        .WrapWithFeature(FEATURE_FORMAT_IN_CAST));
  }

  return tests;
}

std::vector<QueryParamsWithResult>
GetFunctionTestsCastDateTimestampStringWithFormat() {
  std::vector<FunctionTestCall> function_calls =
      GetFunctionTestsCastFormatDateTimestamp();
  std::vector<QueryParamsWithResult> tests;

  tests.reserve(function_calls.size());
  for (auto& function_call : function_calls) {
    tests.push_back(QueryParamsWithResult(function_call.params)
                        .AddRequiredFeature(FEATURE_FORMAT_IN_CAST));
  }

  return tests;
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastNumericString() {
  return {
      // bool->string
      {{NullBool()}, NullString()},
      {{true}, String("true")},
      {{false}, String("false")},

      // string->bool
      {{NullString()}, NullBool()},
      {{String("")}, NullBool(), OUT_OF_RANGE},
      {{String("0")}, NullBool(), OUT_OF_RANGE},
      {{String("1")}, NullBool(), OUT_OF_RANGE},
      {{String("x")}, NullBool(), OUT_OF_RANGE},
      {{String(" true")}, NullBool(), OUT_OF_RANGE},
      {{String("true ")}, NullBool(), OUT_OF_RANGE},
      {{String(" false")}, NullBool(), OUT_OF_RANGE},
      {{String("false ")}, NullBool(), OUT_OF_RANGE},
      {{String("true")}, true},
      {{String("false")}, false},
      {{String("True")}, true},
      {{String("False")}, false},
      {{String("TRUE")}, true},
      {{String("FALSE")}, false},

      // string->int64
      {{NullString()}, NullInt64()},
      {{String("")}, NullInt64(), OUT_OF_RANGE},
      {{String("a")}, NullInt64(), OUT_OF_RANGE},
      {{String("0")}, 0ll},
      {{String("9223372036854775808")}, NullInt64(), OUT_OF_RANGE},
      {{String("9223372036854775807")}, 9223372036854775807ll},
      {{String("-9223372036854775808")}, -9223372036854775807ll - 1},
      {{String("-9223372036854775809")}, NullInt64(), OUT_OF_RANGE},

      // int64->string
      {{NullInt64()}, NullString()},
      {{0ll}, String("0")},
      {{123ll}, String("123")},
      {{-456ll}, String("-456")},
      {{9223372036854775807ll}, String("9223372036854775807")},
      {{-9223372036854775807ll - 1}, String("-9223372036854775808")},

      // string->uint64
      {{NullString()}, NullUint64()},
      {{String("")}, NullUint64(), OUT_OF_RANGE},
      {{String("a")}, NullUint64(), OUT_OF_RANGE},
      {{String("0")}, 0ull},
      {{String("123")}, 123ull},
      {{String("18446744073709551615")}, 18446744073709551615ull},
      {{String("18446744073709551616")}, NullUint64(), OUT_OF_RANGE},
      {{String("-1")}, NullUint64(), OUT_OF_RANGE},
      {{String("-9223372036854775809")}, NullUint64(), OUT_OF_RANGE},

      // uint64->string
      {{NullUint64()}, NullString()},
      {{0ull}, String("0")},
      {{123ull}, String("123")},
      {{18446744073709551615ull}, String("18446744073709551615")},

      // string->int32
      {{NullString()}, NullInt32()},
      {{String("")}, NullInt32(), OUT_OF_RANGE},
      {{String("a")}, NullInt32(), OUT_OF_RANGE},
      {{String("0")}, 0},
      {{String("2147483648")}, NullInt32(), OUT_OF_RANGE},
      {{String("2147483647")}, 2147483647},
      {{String("-2147483648")}, -2147483647 - 1},
      {{String("-2147483649")}, NullInt32(), OUT_OF_RANGE},

      // int32->string
      {{NullInt32()}, NullString()},
      {{0}, String("0")},
      {{123}, String("123")},
      {{-456}, String("-456")},
      {{2147483647}, String("2147483647")},
      {{-2147483647 - 1}, String("-2147483648")},

      // string->uint32
      {{NullString()}, NullUint32()},
      {{String("")}, NullUint32(), OUT_OF_RANGE},
      {{String("a")}, NullUint32(), OUT_OF_RANGE},
      {{String("0")}, 0u},
      {{String("123")}, 123u},
      {{String("4294967295")}, 4294967295u},
      {{String("4294967296")}, NullUint32(), OUT_OF_RANGE},
      {{String("-1")}, NullUint32(), OUT_OF_RANGE},
      {{String("-4294967295")}, NullUint32(), OUT_OF_RANGE},

      // uint32->string
      {{NullUint32()}, NullString()},
      {{0u}, String("0")},
      {{123u}, String("123")},
      {{4294967295u}, String("4294967295")},

      // string->float
      {{NullString()}, NullFloat()},
      {{String("")}, NullFloat(), OUT_OF_RANGE},
      {{String(" ")}, NullFloat(), OUT_OF_RANGE},
      {{String("a")}, NullFloat(), OUT_OF_RANGE},
      {{String("0")}, 0.0f},
      {{String("0.123")}, 0.123f},
      {{String("123")}, 123.0f},
      {{String("-123")}, -123.0f},
      {{String("1.123e+25")}, 1.123e25f},
      {{String("1.234e-25")}, 1.234e-25f},
      {{String("1.123456789e+25")}, 1.123456789e25f},
      {{String("1.123456789123456e+25")}, 1.123456789e25f},
      {{String("3.4028236e+38")}, float_pos_inf},
      {{String("-3.4028236e+38")}, float_neg_inf},
      {{String("inf")}, float_pos_inf},
      {{String("+inf")}, float_pos_inf},
      {{String("-inf")}, float_neg_inf},
      {{String("nan")}, float_nan},
      {{String("NaN")}, float_nan},

      // float->string
      {{NullFloat()}, NullString()},
      {{0.0f}, String("0")},
      {{0.123f}, String("0.123")},
      {{123.0f}, String("123")},
      {{-123.0f}, String("-123")},
      {{1.123e25f}, String("1.123e+25")},
      {{1.234e-25f}, String("1.234e-25")},
      {{0.1}, String("0.1")},

      {{floatmax}, String("3.4028235e+38")},
      {{floatminpositive}, String("1.1754944e-38")},
      {{float_nan}, String("nan")},
      {{float_pos_inf}, String("inf")},
      {{float_neg_inf}, String("-inf")},
      {{-0.0f}, String("0")},
      {{float_nan}, String("nan")},
      {{float_neg_nan}, String("nan")},

      // string->double
      {{NullString()}, NullDouble()},
      {{String("")}, NullDouble(), OUT_OF_RANGE},
      {{String(" ")}, NullDouble(), OUT_OF_RANGE},
      {{String("a")}, NullDouble(), OUT_OF_RANGE},
      {{String("0")}, 0.0},
      {{String("0.123")}, 0.123},
      {{String("123")}, 123.0},
      {{String("-123")}, -123.0},
      {{String("1.123e+25")}, 1.123e25},
      {{String("1.234e-25")}, 1.234e-25},
      {{String("1.1234567891234e+25")}, 1.1234567891234e25},
      {{String("1.123456789123456789e+25")}, 1.1234567891234567e25},

      // TODO: decide if conversion of a value out of the range should
      // return an error instead of +/-inf.
      {{String("1.797693134862316e+308")}, double_pos_inf},
      {{String("-1.797693134862316e+308")}, double_neg_inf},

      {{String("1.0000000000000003")}, 1.0000000000000002},
      {{String("inf")}, double_pos_inf},
      {{String("+inf")}, double_pos_inf},
      {{String("-inf")}, double_neg_inf},

      // TODO: decide if Inf and NaN should really be case-insensetive.
      {{String("nan")}, double_nan},
      {{String("NaN")}, double_nan},

      // double->string
      {{NullDouble()}, NullString()},
      {{0.0}, String("0")},
      {{0.123}, String("0.123")},
      {{123.0}, String("123")},
      {{-123.0}, String("-123")},
      {{1.123e25}, String("1.123e+25")},
      {{1.234e-25}, String("1.234e-25")},
      {{1.1234567891234e25}, String("1.1234567891234e+25")},
      {{doublemax}, String("1.7976931348623157e+308")},
      {{doubleminpositive}, String("2.2250738585072014e-308")},
      {{double_nan}, String("nan")},
      {{double_pos_inf}, String("inf")},
      {{double_neg_inf}, String("-inf")},
      {{-0.0}, String("0")},
      {{double_nan}, String("nan")},
      {{double_neg_nan}, String("nan")},

      // string->numeric
      QueryParamsWithResult({NullString()}, NullNumeric())
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({String("9223372036854775807")},
                            Value::Numeric(NumericValue(9223372036854775807ll)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({String("0")}, Value::Numeric(NumericValue()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({String("0.0")}, Value::Numeric(NumericValue()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {String("-9223372036854775807")},
          Value::Numeric(NumericValue(-9223372036854775807ll)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {String("123456789.12345678")},
          Value::Numeric(
              NumericValue::FromStringStrict("123456789.12345678").value()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // string->bignumeric
      QueryParamsWithResult({NullString()}, NullBigNumeric())
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {String("9223372036854775807")},
          Value::BigNumeric(BigNumericValue(9223372036854775807LL)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({String("0")}, Value::BigNumeric(BigNumericValue()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({String("0.0")},
                            Value::BigNumeric(BigNumericValue()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {String("-9223372036854775807")},
          Value::BigNumeric(BigNumericValue(-9223372036854775807LL)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {String("1e38")},
          Value::BigNumeric(BigNumericValue::FromStringStrict("1e38").value()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {String("578960446186580977117854925043439539266."
                  "34992332820282019728792003956564819967")},
          Value::BigNumeric(BigNumericValue::FromStringStrict(
                                "578960446186580977117854925043439539266."
                                "34992332820282019728792003956564819967")
                                .value()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {String("-578960446186580977117854925043439539266."
                  "34992332820282019728792003956564819968")},
          Value::BigNumeric(BigNumericValue::FromStringStrict(
                                "-578960446186580977117854925043439539266."
                                "34992332820282019728792003956564819968")
                                .value()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      // numeric->string
      QueryParamsWithResult({NullNumeric()}, NullString())
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::Numeric(NumericValue(9223372036854775807ll))},
          String("9223372036854775807"))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue())}, String("0"))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::Numeric(NumericValue(-9223372036854775807ll))},
          String("-9223372036854775807"))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::Numeric(
              NumericValue::FromStringStrict("123456789.12345678").value())},
          String("123456789.12345678"))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // bignumeric->string
      QueryParamsWithResult({NullBigNumeric()}, NullString())
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue(9223372036854775807LL))},
          String("9223372036854775807"))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue())}, String("0"))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue(-9223372036854775807LL))},
          String("-9223372036854775807"))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromStringStrict(
                                 "578960446186580977117854925043439539266."
                                 "34992332820282019728792003956564819967")
                                 .value())},
          String("578960446186580977117854925043439539266."
                 "34992332820282019728792003956564819967"))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromStringStrict(
                                 "-578960446186580977117854925043439539266."
                                 "34992332820282019728792003956564819968")
                                 .value())},
          String("-578960446186580977117854925043439539266."
                 "34992332820282019728792003956564819968"))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

// FromType is in {float, double}. ToType is in {int32, uint32, int64, uint64}.
template <typename FromType, typename ToType>
static std::vector<QueryParamsWithResult> TestCastPositiveRounding() {
  return {
    {{Value::Make<FromType>(1.1)}, Value::Make<ToType>(1)},
    {{Value::Make<FromType>(1.5)}, Value::Make<ToType>(2)},
    {{Value::Make<FromType>(1.9)}, Value::Make<ToType>(2)},
    {{Value::Make<FromType>(2.5)}, Value::Make<ToType>(3)},
  };
}

// FromType = {float, double}. ToType = {int32, int64}.
template <typename FromType, typename ToType>
static std::vector<QueryParamsWithResult> TestCastNegativeRounding() {
  return {
    {{Value::Make<FromType>(-1.1)}, Value::Make<ToType>(-1)},
    {{Value::Make<FromType>(-1.5)}, Value::Make<ToType>(-2)},
    {{Value::Make<FromType>(-1.9)}, Value::Make<ToType>(-2)},
    {{Value::Make<FromType>(-2.5)}, Value::Make<ToType>(-3)},
  };
}

// FromType is in {float, double}. ToType is in {int32, uint32, int64, uint64,
// bool}.
template <typename FromType, typename ToType>
static std::vector<QueryParamsWithResult> TestCastInfinityError() {
  return {
      {{Value::Make<FromType>(std::numeric_limits<FromType>::quiet_NaN())},
       Value::MakeNull<ToType>(),
       OUT_OF_RANGE},
      {{Value::Make<FromType>(std::numeric_limits<FromType>::infinity())},
       Value::MakeNull<ToType>(),
       OUT_OF_RANGE},
      {{Value::Make<FromType>(-std::numeric_limits<FromType>::infinity())},
       Value::MakeNull<ToType>(),
       OUT_OF_RANGE},
  };
}

// FromType = {int32, int64, uint32, uint64, float, double}.
template <typename FromType>
static std::vector<QueryParamsWithResult> TestCastNumericNull() {
  const Value& from_null = Value::MakeNull<FromType>();
  return {
    {{from_null}, NullInt32()},
    {{from_null}, NullInt64()},
    {{from_null}, NullUint32()},
    {{from_null}, NullUint64()},
    {{from_null}, NullFloat()},
    {{from_null}, NullDouble()},
  };
}

// Represents a cast test case for TIME and DATETIME asserting
//   CAST(<input> AS <output_type>)
// either:
//   returns a value that matches the expected output, or
//   give an error with the same error code as the expected error status.
// The cast could be from STRING to TIME/DATETIME, or from TIME/DATETIME to
// STRING.
struct CivilTimeCastTestCase : CivilTimeTestCase {
  CivilTimeCastTestCase(const Value& input,
                        const absl::StatusOr<Value>& same_output,
                        const Type* output_type = nullptr)
      : CivilTimeTestCase({input}, same_output, output_type) {}
  // For test cases where <micros_output> is different with <nanos_output>.
  CivilTimeCastTestCase(const Value& input,
                        const absl::StatusOr<Value>& micros_output,
                        const absl::StatusOr<Value>& nanos_output,
                        const Type* output_type = nullptr)
      : CivilTimeTestCase({input}, micros_output, nanos_output, output_type) {}
};

static absl::Status CivilTimeCastEvalError() {
  return absl::Status(absl::StatusCode::kOutOfRange, "Civil time cast failed");
}

static void AddInvalidTimeAndDatetimeCastFromStringTestCases(
    std::vector<QueryParamsWithResult>* result) {
  static const std::vector<std::string> time_test_cases = {
      // malformed string
      "abc",
      // out-of-range hour
      "24:00:00",
      "-1:00:00",
      // out-of-range minute
      "12:60:00",
      "12:-1:00",
      // out-of-range second
      "12:30:61",  // :61 is out-of-range instead of a leap second
      "12:30:-1",
      // out-of-range sub-second
      "12:34:56.-1",
      // string with trailing unconsumed junks
      "12:34:56zzzz",
      "12:34:56.zzzz",
      "12:34:56.123zzz",
      "12:34:56.123456zzz",
      "12:34:56.123456789zzz",
      // leading and trailing blanks
      " 12:34:56.123456",
      "12:34:56.123456 ",
      "12:34:56 ",
      " 12:34:56.123456 ",
      // timezone is irrelevant for TIME, and considered junk
      "12:34:56.123456 UTC",
      "12:34:56.123456+08",
      "12:34:56.123456+08:00",
  };
  for (const auto& each : time_test_cases) {
    AddTestCaseWithWrappedResultForCivilTimeAndNanos(
        CivilTimeCastTestCase(String(each), CivilTimeCastEvalError(),
                              TimeType()),
        result);
  }

  static const std::vector<std::string> datetime_test_cases = {
      // malformed string
      "abc",
      // out-of-range year
      "10000-11-06 12:34:56",
      "0000-11-06 12:34:56",
      // out-of-range month
      "2015-13-06 12:34:56",
      "2015-00-06 12:34:56",
      // Invalid day-of-month
      "2015-11-32 12:34:56",
      "2015-11-00 12:34:56",
      "2015-11-31 12:34:56",
      "2015-02-29 12:34:56",
      // out-of-range hour
      "2015-11-06 24:00:00",
      "2015-11-06 -1:00:00",
      // out-of-range minute
      "2015-11-06 12:60:00",
      "2015-11-06 12:-1:00",
      // out-of-range second
      "2015-11-06 12:30:61",  // :61 is out-of-range instead of a leap second
      "2015-11-06 12:30:-1",
      // out-of-range sub-second
      "2015-11-06 12:34:56.-1",
      // Leap second normalization leads to out-of-range year field
      "9999-12-31 23:59:60",
      // 4 digits year part is required
      "125-01-02 01:02:03",
      // string with trailing unconsumed junks
      "2006-01-02 12:34:56zzzz",
      "2006-01-02 12:34:56.zzzz",
      "2006-01-02 12:34:56.123zzz",
      "2006-01-02 12:34:56.123456zzz",
      "2006-01-02 12:34:56.123456789zzz",
      // leading and trailing blanks
      " 2006-01-02 12:34:56.123456",
      "2006-01-02 12:34:56.123456 ",
      "2006-01-02 12:34:56 ",
      "2006-01-02 ",
      " 2006-01-02 12:34:56.123456 ",
      // timezone is irrelevant for DATETIME, and considered junk
      "2006-01-02 12:34:56.123456 UTC",
      "2006-01-02 12:34:56.123456+08",
      "2006-01-02 12:34:56.123456+08:00",
  };
  for (const auto& each : datetime_test_cases) {
    AddTestCaseWithWrappedResultForCivilTimeAndNanos(
        CivilTimeCastTestCase(String(each), CivilTimeCastEvalError(),
                              DatetimeType()),
        result);
  }
}

// There are additional roundtrip cast test cases in
//   compliance/testdata/civil_time.test
static void AddTimeAndDatetimeCastTestCases(
    std::vector<QueryParamsWithResult>* result) {
  static const std::vector<CivilTimeCastTestCase> test_cases = {
      // STRING -> TIME
      {NullString(), NullTime()},
      {String("00:00:00"), TimeMicros(0, 0, 0, 0)},
      {String("23:59:59.999999"), TimeMicros(23, 59, 59, 999999)},
      {String("01:02:03.123456"), TimeMicros(1, 2, 3, 123456)},
      {String("01:02:03.12345"), TimeMicros(1, 2, 3, 123450)},
      {String("01:02:03.120000"), TimeMicros(1, 2, 3, 120000)},
      {String("01:02:3"), TimeMicros(1, 2, 3, 0)},
      {String("01:2:03"), TimeMicros(1, 2, 3, 0)},
      {String("1:02:03"), TimeMicros(1, 2, 3, 0)},
      // Leap second cases
      {String("23:59:60"), TimeMicros(0, 0, 0, 0)},
      {String("12:59:60"), TimeMicros(13, 0, 0, 0)},
      {String("12:59:60.123456"), TimeMicros(13, 0, 0, 0)},
      {String("12:34:60"), TimeMicros(12, 35, 0, 0)},
      {String("12:34:60.123456"), TimeMicros(12, 35, 0, 0)},

      // These return an error in micros mode and a value in nanos mode
      {String("23:59:59.999999999"), CivilTimeCastEvalError(),
       TimeNanos(23, 59, 59, 999999999)},
      {String("01:02:03.123456789"), CivilTimeCastEvalError(),
       TimeNanos(1, 2, 3, 123456789)},
      {String("01:02:03.123456780"), CivilTimeCastEvalError(),
       TimeNanos(1, 2, 3, 123456780)},
      {String("01:02:03.123450000"), CivilTimeCastEvalError(),
       TimeNanos(1, 2, 3, 123450000)},
      {String("01:02:03.120000000"), CivilTimeCastEvalError(),
       TimeNanos(1, 2, 3, 120000000)},
      // Leap second case
      {String("12:59:60.123456789"), CivilTimeCastEvalError(),
       TimeNanos(13, 0, 0, 0)},
      {String("12:34:60.123456789"), CivilTimeCastEvalError(),
       TimeNanos(12, 35, 0, 0)},

      // too many subsecond digits
      {String("12:34:56.1000000"), CivilTimeCastEvalError(),
       TimeNanos(12, 34, 56, 100000000)},
      {String("12:34:56.0000000"), CivilTimeCastEvalError(),
       TimeNanos(12, 34, 56, 0)},
      {String("12:34:56.1000000000"), CivilTimeCastEvalError(), TimeType()},
      {String("12:34:56.0000000000"), CivilTimeCastEvalError(), TimeType()},

      // STRING -> DATETIME
      {NullString(), NullDatetime()},
      {String("0001-01-01 00:00:00"), DatetimeMicros(1, 1, 1, 0, 0, 0, 0)},
      {String("9999-12-31 23:59:59.999999"),
       DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999)},
      {String("2006-01-02"), DatetimeMicros(2006, 1, 2, 0, 0, 0, 0)},

      {String("2006-01-02 01:02:03.123456"),
       DatetimeMicros(2006, 1, 2, 1, 2, 3, 123456)},
      {String("2006-01-02 01:02:03.12345"),
       DatetimeMicros(2006, 1, 2, 1, 2, 3, 123450)},
      {String("2006-01-02 01:02:03.12"),
       DatetimeMicros(2006, 1, 2, 1, 2, 3, 120000)},
      {String("2006-01-02 01:02:3"), DatetimeMicros(2006, 1, 2, 1, 2, 3, 0)},
      {String("2006-01-02 01:2:03"), DatetimeMicros(2006, 1, 2, 1, 2, 3, 0)},
      {String("2006-01-02 1:02:03"), DatetimeMicros(2006, 1, 2, 1, 2, 3, 0)},
      {String("2006-01-2 01:02:03"), DatetimeMicros(2006, 1, 2, 1, 2, 3, 0)},
      {String("2006-1-02 01:02:03"), DatetimeMicros(2006, 1, 2, 1, 2, 3, 0)},
      // Leap second cases
      {String("2015-11-06 23:59:60"), DatetimeMicros(2015, 11, 7, 0, 0, 0, 0)},
      {String("2015-11-06 12:59:60"), DatetimeMicros(2015, 11, 6, 13, 0, 0, 0)},
      {String("2015-11-06 12:59:60.123456"),
       DatetimeMicros(2015, 11, 6, 13, 0, 0, 0)},
      {String("2015-11-06 12:34:60"),
       DatetimeMicros(2015, 11, 6, 12, 35, 0, 0)},
      {String("2015-11-06 12:34:60.123456"),
       DatetimeMicros(2015, 11, 6, 12, 35, 0, 0)},

      // These return an error in micros mode and a value in nanos mode
      {String("9999-12-31 23:59:59.999999999"), CivilTimeCastEvalError(),
       DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999999)},
      {String("2006-01-02 01:02:03.123456789"), CivilTimeCastEvalError(),
       DatetimeNanos(2006, 1, 2, 1, 2, 3, 123456789)},
      {String("2006-01-02 01:02:03.12345678"), CivilTimeCastEvalError(),
       DatetimeNanos(2006, 1, 2, 1, 2, 3, 123456780)},
      {String("2006-01-02 01:02:03.123450000"), CivilTimeCastEvalError(),
       DatetimeNanos(2006, 1, 2, 1, 2, 3, 123450000)},
      {String("2006-01-02 01:02:03.120000000"), CivilTimeCastEvalError(),
       DatetimeNanos(2006, 1, 2, 1, 2, 3, 120000000)},
      // Leap second case
      {String("2015-11-06 12:59:60.123456789"), CivilTimeCastEvalError(),
       DatetimeNanos(2015, 11, 6, 13, 0, 0, 0)},
      {String("2015-11-06 12:34:60.123456789"), CivilTimeCastEvalError(),
       DatetimeNanos(2015, 11, 6, 12, 35, 0, 0)},

      // too many subsecond digits
      {String("2015-11-06 12:34:56.1000000"), CivilTimeCastEvalError(),
       DatetimeNanos(2015, 11, 6, 12, 34, 56, 100000000)},
      {String("2015-11-06 12:34:56.0000000"), CivilTimeCastEvalError(),
       DatetimeNanos(2015, 11, 6, 12, 34, 56, 0)},
      {String("2015-11-06 12:34:56.1000000000"), CivilTimeCastEvalError(),
       DatetimeType()},
      {String("2015-11-06 12:34:56.0000000000"), CivilTimeCastEvalError(),
       DatetimeType()},

      // TIME -> STRING
      {NullTime(), NullString()},
      {TimeMicros(0, 0, 0, 0), String("00:00:00")},
      {TimeMicros(23, 59, 59, 999999), String("23:59:59.999999")},
      {TimeMicros(1, 2, 3, 123456), String("01:02:03.123456")},
      {TimeMicros(1, 2, 3, 123450), String("01:02:03.123450")},
      {TimeMicros(1, 2, 3, 120000), String("01:02:03.120")},
      {TimeMicros(1, 2, 3, 0), String("01:02:03")},
      {TimeNanos(23, 59, 59, 999999999), String("23:59:59.999999"),
       String("23:59:59.999999999")},
      {TimeNanos(1, 2, 3, 123456789), String("01:02:03.123456"),
       String("01:02:03.123456789")},
      {TimeNanos(1, 2, 3, 123456780), String("01:02:03.123456"),
       String("01:02:03.123456780")},
      {TimeNanos(1, 2, 3, 123450000), String("01:02:03.123450")},
      {TimeNanos(1, 2, 3, 120000000), String("01:02:03.120")},

      // DATETIME -> STRING
      {NullDatetime(), NullString()},
      {DatetimeMicros(1, 1, 1, 0, 0, 0, 0), String("0001-01-01 00:00:00")},
      {DatetimeMicros(9999, 12, 31, 23, 59, 59, 999999),
       String("9999-12-31 23:59:59.999999")},
      {DatetimeMicros(2006, 1, 2, 0, 0, 0, 0), String("2006-01-02 00:00:00")},

      {DatetimeMicros(2006, 1, 2, 1, 2, 3, 123456),
       String("2006-01-02 01:02:03.123456")},
      {DatetimeMicros(2006, 1, 2, 1, 2, 3, 123450),
       String("2006-01-02 01:02:03.123450")},
      {DatetimeMicros(2006, 1, 2, 1, 2, 3, 120000),
       String("2006-01-02 01:02:03.120")},
      {DatetimeMicros(2006, 1, 2, 1, 2, 3, 0), String("2006-01-02 01:02:03")},

      {DatetimeNanos(9999, 12, 31, 23, 59, 59, 999999999),
       String("9999-12-31 23:59:59.999999"),
       String("9999-12-31 23:59:59.999999999")},
      {DatetimeNanos(2006, 1, 2, 1, 2, 3, 123456789),
       String("2006-01-02 01:02:03.123456"),
       String("2006-01-02 01:02:03.123456789")},
      {DatetimeNanos(2006, 1, 2, 1, 2, 3, 123456780),
       String("2006-01-02 01:02:03.123456"),
       String("2006-01-02 01:02:03.123456780")},
      {DatetimeNanos(2006, 1, 2, 1, 2, 3, 123450000),
       String("2006-01-02 01:02:03.123450")},
      {DatetimeNanos(2006, 1, 2, 1, 2, 3, 120000000),
       String("2006-01-02 01:02:03.120")},
  };
  for (const auto& each : test_cases) {
    AddTestCaseWithWrappedResultForCivilTimeAndNanos(each, result);
  }
  AddInvalidTimeAndDatetimeCastFromStringTestCases(result);
}

std::vector<QueryParamsWithResult> TestCastDateTimeWithString() {
  std::vector<QueryParamsWithResult> v = {
    // Note that there are more complete tests for conversions between
    // DATE/TIMESTAMP and STRING in:
    //   public/functions/date_time_util_test.cc
    // TODO: Move the majority of those tests here so that
    // we have complete coverage in our compliance test suite.

    // DATE -> STRING
    {{Date(-1)}, String("1969-12-31")},
    {{Date(0)}, String("1970-01-01")},
    {{Date(1)}, String("1970-01-02")},

    {{Date(16102)}, String("2014-02-01")},
    {{Date(14276)}, String("2009-02-01")},
    {{Date(14288)}, String("2009-02-13")},
    {{Date(14579)}, String("2009-12-01")},
    {{Date(11016)}, String("2000-02-29")},

    {{Date(-106650)}, String("1678-01-01")},
    {{Date(106650)}, String("2261-12-31")},

    {{Date(date_min)}, String("0001-01-01")},
    {{Date(date_max)}, String("9999-12-31")},

    // TIMESTAMP -> STRING
    // The default time zone is America/Los_Angeles.
    {{Timestamp(0)},       String("1969-12-31 16:00:00-08")},
    {{Timestamp(1)},       String("1969-12-31 16:00:00.000001-08")},
    {{Timestamp(10)},      String("1969-12-31 16:00:00.000010-08")},
    {{Timestamp(100)},     String("1969-12-31 16:00:00.000100-08")},
    {{Timestamp(1000)},    String("1969-12-31 16:00:00.001-08")},
    {{Timestamp(10000)},   String("1969-12-31 16:00:00.010-08")},
    {{Timestamp(100000)},  String("1969-12-31 16:00:00.100-08")},
    {{Timestamp(1000000)}, String("1969-12-31 16:00:01-08")},

    {{Timestamp(timestamp_min)},    String("0000-12-31 16:08:00-07:52")},
    {{Timestamp(timestamp_max)},    String("9999-12-31 15:59:59.999999-08")},
    {{Timestamp(1391258096123456)}, String("2014-02-01 04:34:56.123456-08")},

    // STRING -> DATE
    {{String("1969-12-31")}, Date(-1)},
    {{String("1970-1-1")}, Date(0)},
    {{String("1970-01-02")}, Date(1)},

    {{String("2014-02-01")}, Date(16102)},
    {{String("2009-02-01")}, Date(14276)},
    {{String("2009-02-1")}, Date(14276)},
    {{String("2009-2-1")}, Date(14276)},
    {{String("2009-2-01")}, Date(14276)},
    {{String("2009-02-13")}, Date(14288)},
    {{String("2009-2-13")}, Date(14288)},
    {{String("2009-12-01")}, Date(14579)},
    {{String("2009-12-1")}, Date(14579)},
    {{String("2000-02-29")}, Date(11016)},
    {{String("2000-2-29")}, Date(11016)},

    {{String("1678-01-01")}, Date(-106650)},
    {{String("2261-12-31")}, Date(106650)},

    {{String("0001-01-01")}, Date(date_min)},
    {{String("9999-12-31")}, Date(date_max)},

    // STRING -> TIMESTAMP
    {{String("0001-01-01 00:00:00+00")}, Timestamp(timestamp_min)},
    {{String("1970-01-01 00:00:00+00")}, Timestamp(0)},
    {{String("9999-12-31 23:59:59.999999+00")}, Timestamp(timestamp_max)},
    {{String("2014-02-01 12:34:56.123456+00")}, Timestamp(1391258096123456)},

    // ERROR TESTS
    {{String("2009-02-29")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-01-32")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-13-10")}, NullDate(), OUT_OF_RANGE},
    {{String(" 2009-02-13")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02-13 ")}, NullDate(), OUT_OF_RANGE},
    {{String("20090213")}, NullDate(), OUT_OF_RANGE},
    {{String("2009/02/13")}, NullDate(), OUT_OF_RANGE},
    {{String("2009")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02-")}, NullDate(), OUT_OF_RANGE},
    {{String("2000--01")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02-0")}, NullDate(), OUT_OF_RANGE},
    {{String("0000-12-31")}, NullDate(), OUT_OF_RANGE},
    {{String("10000-01-01")}, NullDate(), OUT_OF_RANGE},
    {{String("2009--2-13")}, NullDate(), OUT_OF_RANGE},
    {{String("8-02-13")}, NullDate(), OUT_OF_RANGE},
    {{String("98-02-13")}, NullDate(), OUT_OF_RANGE},
    {{String("998-02-13")}, NullDate(), OUT_OF_RANGE},
    {{String("02-03-2009")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02-13-")}, NullDate(), OUT_OF_RANGE},
    {{String("-2009-02-13-")}, NullDate(), OUT_OF_RANGE},
    {{String("2009/02-13")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02/13")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-0213")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-01-29 00:00:00")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-02-28.100001")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-01-21Rubbish")}, NullDate(), OUT_OF_RANGE},
    {{String("2009-01-21 Trash")}, NullDate(), OUT_OF_RANGE},

    // Boundary tests.

    // One microsecond before supported range.
    {{String("0000-12-31 23:59:59.999999+00")},
       NullTimestamp(), OUT_OF_RANGE},
    {{String("0000-12-31 16:07:01.999999 America/Los_Angeles")},
       NullTimestamp(), OUT_OF_RANGE},

    // Minimum of supported range.
    {{String("0001-01-01 00:00:00.000000+00")}, Timestamp(timestamp_min)},
    {{String("0000-12-31 16:07:02.000000 America/Los_Angeles")},
       Timestamp(timestamp_min)},

    // Maximum of supported range.
    {{String("9999-12-31 23:59:59.999999+00")}, Timestamp(timestamp_max)},
    {{String("9999-12-31 15:59:59.999999 America/Los_Angeles")},
        Timestamp(timestamp_max)},

    // One microsecond after supported range.
    {{String("10000-01-01 00:00:00.000000+00")},
       NullTimestamp(), OUT_OF_RANGE},
    {{String("9999-12-31 16:00:00.000000 America/Los_Angeles")},
       NullTimestamp(), OUT_OF_RANGE},
  };
  AddTimeAndDatetimeCastTestCases(&v);
  return v;
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastDateTime() {
  const absl::CivilDay epoch_day = absl::CivilDay(1970, 1, 1);
  std::vector<QueryParamsWithResult> v = {
      // DATE -> DATE
      {{NullDate()}, NullDate()},
      {{Date(date_min)}, Date(date_min)},
      {{Date(0)}, Date(0)},
      {{Date(date_max)}, Date(date_max)},

      // TIMESTAMP ->  TIMESTAMP
      {{NullTimestamp()}, NullTimestamp()},
      {{Timestamp(timestamp_min)}, Timestamp(timestamp_min)},
      {{Timestamp(0)}, Timestamp(0)},
      {{Timestamp(timestamp_max)}, Timestamp(timestamp_max)},

      // DATETIME -> TIME
      QueryParamsWithResult({Datetime(DatetimeValue::FromYMDHMSAndMicros(
                                2018, 11, 21, 11, 13, 1, 2))},
                            Time(TimeValue::FromHMSAndMicros(11, 13, 1, 2)))
          .WrapWithFeature(FEATURE_CIVIL_TIME),

      // DATETIME -> DATE
      QueryParamsWithResult({Datetime(DatetimeValue::FromYMDHMSAndMicros(
                                2018, 11, 21, 11, 13, 1, 2))},
                            Date(absl::CivilDay(2018, 11, 21) - epoch_day))
          .WrapWithFeature(FEATURE_CIVIL_TIME),

      // TODO: Add positive tests between date and timestamp.
  };

  return ConcatTests<QueryParamsWithResult>({
      v,
      TestCastDateTimeWithString(),
  });
}

using interval_testing::Days;
using interval_testing::Hours;
using interval_testing::Micros;
using interval_testing::Minutes;
using interval_testing::Months;
using interval_testing::MonthsDaysMicros;
using interval_testing::Nanos;
using interval_testing::Seconds;
using interval_testing::Years;
using interval_testing::YMDHMS;

std::vector<QueryParamsWithResult> GetFunctionTestsCastInterval() {
  std::vector<QueryParamsWithResult> tests({
      // INTERVAL -> INTERVAL
      {{NullInterval()}, NullInterval()},
      {{Interval(Months(0))}, Interval(Months(0))},
      // INTERVAL -> STRING
      {{Interval(Months(0))}, String("0-0 0 0:0:0")},
      {{Interval(Months(-240))}, String("-20-0 0 0:0:0")},
      {{Interval(Days(30))}, String("0-0 30 0:0:0")},
      {{Interval(Micros(-1))}, String("0-0 0 -0:0:0.000001")},
      {{Interval(Nanos(-1))}, String("0-0 0 -0:0:0.000000001")},
      {{Interval(MonthsDaysMicros(1, 2, 3000000))}, String("0-1 2 0:0:3")},
      {{Interval(YMDHMS(1, 2, 3, 4, 5, 6))}, String("1-2 3 4:5:6")},
      // STRING -> INTERVAL
      {{String("0-0 0 0:0:0.0")}, Interval(Months(0))},
      {{String("1-2 3 4:5:6")}, Interval(YMDHMS(1, 2, 3, 4, 5, 6))},
      {{String("-1-2 -3 -4:5:6")}, Interval(YMDHMS(-1, -2, -3, -4, -5, -6))},
      {{String("1-2 3 +4:5")}, Interval(YMDHMS(1, 2, 3, 4, 5, 0))},
      {{String("1-2 +3 -4:5")}, Interval(YMDHMS(1, 2, 3, -4, -5, 0))},
      {{String("1-2 3 4")}, Interval(YMDHMS(1, 2, 3, 4, 0, 0))},
      {{String("1-2 3 -4")}, Interval(YMDHMS(1, 2, 3, -4, 0, 0))},
      {{String("+1-2 3")}, Interval(YMDHMS(1, 2, 3, 0, 0, 0))},
      {{String("-1-2 -3")}, Interval(YMDHMS(-1, -2, -3, 0, 0, 0))},
      {{String("1-2")}, Interval(YMDHMS(1, 2, 0, 0, 0, 0))},
      {{String("-1-2")}, Interval(YMDHMS(-1, -2, 0, 0, 0, 0))},
      {{String("0-1 2 -3:4:5")}, Interval(YMDHMS(0, 1, 2, -3, -4, -5))},
      {{String("0-1 2 3:4")}, Interval(YMDHMS(0, 1, 2, 3, 4, 0))},
      {{String("0-1 2 3")}, Interval(YMDHMS(0, 1, 2, 3, 0, 0))},
      {{String("0-1 -2 -3")}, Interval(YMDHMS(0, 1, -2, -3, 0, 0))},
      {{String("1 2:3:4")}, Interval(YMDHMS(0, 0, 1, 2, 3, 4))},
      {{String("1 2:3")}, Interval(YMDHMS(0, 0, 1, 2, 3, 0))},
      {{String("+1:2:3")}, Interval(YMDHMS(0, 0, 0, 1, 2, 3))},
      {{String("-1:2:3")}, Interval(YMDHMS(0, 0, 0, -1, -2, -3))},
      {{String("-0:0:0.0003")}, Interval(Micros(-300))},
      // STRING -> INTERVAL using ISO 8601 format
      {{String("PT")}, Interval(Years(0))},
      {{String("P0Y")}, Interval(Years(0))},
      {{String("P1Y")}, Interval(Years(1))},
      {{String("P-1Y")}, Interval(Years(-1))},
      {{String("P1Y-2Y1Y")}, Interval(Years(0))},
      {{String("P0M")}, Interval(Months(0))},
      {{String("P2M")}, Interval(Months(2))},
      {{String("P2M-4M")}, Interval(Months(-2))},
      {{String("P1Y-1M")}, Interval(Months(11))},
      {{String("P0W")}, Interval(Days(0))},
      {{String("P-1W4W")}, Interval(Days(21))},
      {{String("P3D-5D-2D")}, Interval(Days(-4))},
      {{String("PT25H")}, Interval(Hours(25))},
      {{String("PT-7M")}, Interval(Minutes(-7))},
      {{String("PT1S2S5S")}, Interval(Seconds(8))},
      {{String("PT-0.000009S")}, Interval(Micros(-9))},
      {{String("PT0.000000001S")}, Interval(Nanos(1))},
      {{String("P1Y-2M3DT-4H5M-6S")}, Interval(YMDHMS(1, -2, 3, -4, 5, -6))},

      // Bad formats
      {{String("0-0-0")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:0:0:0")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:0.0")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:0:0.")}, NullInterval(), OUT_OF_RANGE},
      {{String(" 0-0 0")}, NullInterval(), OUT_OF_RANGE},
      {{String("0-0 +-0")}, NullInterval(), OUT_OF_RANGE},
      {{String("1  2 3")}, NullInterval(), OUT_OF_RANGE},
      {{String("1\t2 3")}, NullInterval(), OUT_OF_RANGE},
      {{String("1:2:3 ")}, NullInterval(), OUT_OF_RANGE},
      {{String("++1:2:3")}, NullInterval(), OUT_OF_RANGE},
      {{String("+-1:2:3")}, NullInterval(), OUT_OF_RANGE},
      {{String("--1:2:3")}, NullInterval(), OUT_OF_RANGE},
      {{String("")}, NullInterval(), OUT_OF_RANGE},
      {{String(" ")}, NullInterval(), OUT_OF_RANGE},
      {{String("1")}, NullInterval(), OUT_OF_RANGE},
      {{String("+")}, NullInterval(), OUT_OF_RANGE},
      {{String(":")}, NullInterval(), OUT_OF_RANGE},
      // TODO: Re-enable once special chars are differentiated in
      //     Generated tests names.
      // {{String(".")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:0:1.234e-6")}, NullInterval(), OUT_OF_RANGE},
      // Ambiguous formats
      {{String("1 2")}, NullInterval(), OUT_OF_RANGE},
      {{String("1:2")}, NullInterval(), OUT_OF_RANGE},
      // Too many fractional digits
      {{String("0:0:0.0000000000")}, NullInterval(), OUT_OF_RANGE},
      // Integer parsing overflow
      {{String("9223372036854775808-0")}, NullInterval(), OUT_OF_RANGE},
      {{String("0-9223372036854775808")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:9223372036854775808:0")}, NullInterval(), OUT_OF_RANGE},
      // Exceeds maximum allowed values
      {{String("10001-0")}, NullInterval(), OUT_OF_RANGE},
      {{String("-10000-1")}, NullInterval(), OUT_OF_RANGE},
      {{String("-0-120001")}, NullInterval(), OUT_OF_RANGE},
      {{String("1-120000")}, NullInterval(), OUT_OF_RANGE},
      {{String("0-0 3660001")}, NullInterval(), OUT_OF_RANGE},
      {{String("-87840001:0:0")}, NullInterval(), OUT_OF_RANGE},
      {{String("87840000:0:0.001")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:5270400001:0")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:5270400000:0.000001")}, NullInterval(), OUT_OF_RANGE},
      {{String("0:0:316224000001")}, NullInterval(), OUT_OF_RANGE},
      {{String("-0:0:316224000000.0001")}, NullInterval(), OUT_OF_RANGE},

      {{String("")}, NullInterval(), OUT_OF_RANGE},
      {{String("1Y")}, NullInterval(), OUT_OF_RANGE},
      {{String("T")}, NullInterval(), OUT_OF_RANGE},
      {{String("P")}, NullInterval(), OUT_OF_RANGE},
      {{String("P--1")}, NullInterval(), OUT_OF_RANGE},
      {{String("P1")}, NullInterval(), OUT_OF_RANGE},
      {{String("PTT")}, NullInterval(), OUT_OF_RANGE},
      {{String("PT1HT1M")}, NullInterval(), OUT_OF_RANGE},
      {{String("P1YM")}, NullInterval(), OUT_OF_RANGE},
      {{String("P1YT2MS")}, NullInterval(), OUT_OF_RANGE},
      {{String("PT.1S")}, NullInterval(), OUT_OF_RANGE},
      {{String("PT1.S")}, NullInterval(), OUT_OF_RANGE},
      {{String("PT1.1M")}, NullInterval(), OUT_OF_RANGE},
      {{String("PT99999999999999999999999999999H")},
       NullInterval(),
       OUT_OF_RANGE},
      {{String("PT-99999999999H")}, NullInterval(), OUT_OF_RANGE},
      {{String("P-1S")}, NullInterval(), OUT_OF_RANGE},
      {{String("")}, NullInterval(), OUT_OF_RANGE},
  });

  std::vector<QueryParamsWithResult> result;
  result.reserve(tests.size());
  for (const auto& test : tests) {
    result.push_back(test.WrapWithFeature(FEATURE_INTERVAL_TYPE));
  }
  return result;
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastUuid() {
  std::vector<QueryParamsWithResult> tests({
      // UUID -> UUID
      {{values::NullUuid()}, values::NullUuid()},
      {{values::Uuid(
           UuidValue::FromString("00000000-0000-4000-8000-000000000000")
               .value())},
       values::Uuid(
           UuidValue::FromString("00000000000040008000000000000000").value())},

      // UUID -> STRING
      {{values::NullUuid()}, NullString()},
      {{values::Uuid(
           UuidValue::FromString("00000000000040008000000000000000").value())},
       String("00000000-0000-4000-8000-000000000000")},
      {{values::Uuid(
           UuidValue::FromString("ffffffffffff4fffbfffffffffffffff").value())},
       String("ffffffff-ffff-4fff-bfff-ffffffffffff")},

      // UUID -> Bytes
      {{values::NullUuid()}, NullBytes()},
      {{values::Uuid(
           UuidValue::FromString("00000000000040008000000000000000").value())},
       Bytes(
           "\x00\x00\x00\x00\x00\x00\x40\x00\x80\x00\x00\x00\x00\x00\x00\x00")},
      {{values::Uuid(
           UuidValue::FromString("0102030405060708090a0b0c0d0e0f11").value())},
       Bytes(
           "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x11")},

      // STRING -> UUID
      {{NullString()}, values::NullUuid()},
      {{String("00000000-0000-4000-8000-000000000000")},
       values::Uuid(
           UuidValue::FromString("00000000000040008000000000000000").value())},
      {{String("00000000000040008000000000000000")},
       values::Uuid(
           UuidValue::FromString("00000000000040008000000000000000").value())},
      {{String("9d3da323-4c20-360f-bd9b-ec54feec54f0")},
       values::Uuid(
           UuidValue::FromString("9d3da3234c20360fbd9bec54feec54f0").value())},
      {{String("ffffffff-ffff-4fff-bfff-ffffffffffff")},
       values::Uuid(
           UuidValue::FromString("ffffffffffff4fffbfffffffffffffff").value())},

      // Bytes -> UUID
      {{NullBytes()}, values::NullUuid()},
      {{Bytes(
           "\x00\x00\x00\x00\x00\x00\x40\x00\x80\x00\x00\x00\x00\x00\x00\x00")},
       values::Uuid(
           UuidValue::FromString("00000000-0000-4000-8000-000000000000")
               .value())},
      {{Bytes(
           "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x11")},
       values::Uuid(
           UuidValue::FromString("0102030405060708090a0b0c0d0e0f11").value())},

      // Bad formats
      {{String("9d")}, values::NullUuid(), OUT_OF_RANGE},
  });
  std::vector<QueryParamsWithResult> result;
  result.reserve(tests.size());
  for (auto& test : tests) {
    test.AddRequiredFeature(FEATURE_UUID_TYPE);
    result.push_back(test);
  }
  return result;
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastRange() {
  std::vector<QueryParamsWithResult> tests = {
      // RANGE<DATE> -> RANGE<DATE>
      {{Null(types::DateRangeType())}, {Null(types::DateRangeType())}},
      {{Range(Date(14276), Date(14288))}, {Range(Date(14276), Date(14288))}},
      // RANGE<DATETIME> -> RANGE<DATETIME>
      {{Null(types::DatetimeRangeType())}, {Null(types::DatetimeRangeType())}},
      {{Range(DatetimeMicros(2009, 02, 01, 13, 57, 29, 123456),
              DatetimeMicros(2010, 03, 03, 14, 58, 30, 654321))},
       {Range(DatetimeMicros(2009, 02, 01, 13, 57, 29, 123456),
              DatetimeMicros(2010, 03, 03, 14, 58, 30, 654321))}},
      // RANGE<TIMESTAMP> -> RANGE<TIMESTAMP>
      {{Null(types::TimestampRangeType())},
       {Null(types::TimestampRangeType())}},
      {{Range(Timestamp(1233496649123456), Timestamp(1267628310654321))},
       {Range(Timestamp(1233496649123456), Timestamp(1267628310654321))}},

      // RANGE<DATE> -> STRING
      {{Null(types::DateRangeType())}, NullString()},
      {{Range(Date(14276), Date(14288))}, String("[2009-02-01, 2009-02-13)")},
      {{Range(NullDate(), Date(14288))}, String("[UNBOUNDED, 2009-02-13)")},
      {{Range(Date(14276), NullDate())}, String("[2009-02-01, UNBOUNDED)")},
      {{Range(NullDate(), NullDate())}, String("[UNBOUNDED, UNBOUNDED)")},
      // RANGE<DATETIME> -> STRING
      {{Null(types::DatetimeRangeType())}, NullString()},
      {{Range(DatetimeMicros(2009, 02, 01, 13, 57, 29, 123456),
              DatetimeMicros(2010, 03, 03, 14, 58, 30, 654321))},
       String("[2009-02-01 13:57:29.123456, 2010-03-03 14:58:30.654321)")},
      {{Range(NullDatetime(),
              DatetimeMicros(2010, 03, 03, 14, 58, 30, 654321))},
       String("[UNBOUNDED, 2010-03-03 14:58:30.654321)")},
      {{Range(DatetimeMicros(2009, 02, 01, 13, 57, 29, 123456),
              NullDatetime())},
       String("[2009-02-01 13:57:29.123456, UNBOUNDED)")},
      {{Range(NullDatetime(), NullDatetime())},
       String("[UNBOUNDED, UNBOUNDED)")},
      // RANGE<TIMESTAMP> -> STRING
      // The default time zone is America/Los_Angeles.
      {{Null(types::TimestampRangeType())}, NullString()},
      {{Range(Timestamp(1233496649123456), Timestamp(1267628310654321))},
       String(
           "[2009-02-01 05:57:29.123456-08, 2010-03-03 06:58:30.654321-08)")},
      {{Range(NullTimestamp(), Timestamp(1267628310654321))},
       String("[UNBOUNDED, 2010-03-03 06:58:30.654321-08)")},
      {{Range(Timestamp(1233496649123456), NullTimestamp())},
       String("[2009-02-01 05:57:29.123456-08, UNBOUNDED)")},
      {{Range(NullTimestamp(), NullTimestamp())},
       String("[UNBOUNDED, UNBOUNDED)")},

      // STRING -> RANGE<DATE>
      {{NullString()}, Null(types::DateRangeType())},
      {{String("[2009-02-01, 2009-02-13)")}, Range(Date(14276), Date(14288))},
      {{String("[UNBOUNDED, 2009-02-13)")}, Range(NullDate(), Date(14288))},
      {{String("[2009-02-01, UNBOUNDED)")}, Range(Date(14276), NullDate())},
      {{String("[UNBOUNDED, UNBOUNDED)")}, Range(NullDate(), NullDate())},
      // STRING -> RANGE<DATETIME>
      {{NullString()}, Null(types::DatetimeRangeType())},
      {{String("[2009-02-01 13:57:29.123456, 2010-03-03 14:58:30.654321)")},
       Range(DatetimeMicros(2009, 02, 01, 13, 57, 29, 123456),
             DatetimeMicros(2010, 03, 03, 14, 58, 30, 654321))},
      {{String("[UNBOUNDED, 2010-03-03 14:58:30.654321)")},
       Range(NullDatetime(), DatetimeMicros(2010, 03, 03, 14, 58, 30, 654321))},
      {{String("[2009-02-01 13:57:29.123456, UNBOUNDED)")},
       Range(DatetimeMicros(2009, 02, 01, 13, 57, 29, 123456), NullDatetime())},
      {{String("[UNBOUNDED, UNBOUNDED)")},
       Range(NullDatetime(), NullDatetime())},
      // STRING -> RANGE<TIMESTAMP>
      {{NullString()}, Null(types::TimestampRangeType())},
      {{String(
           "[2009-02-01 05:57:29.123456-08, 2010-03-03 06:58:30.654321-08)")},
       Range(Timestamp(1233496649123456), Timestamp(1267628310654321))},
      {{String("[UNBOUNDED, 2010-03-03 06:58:30.654321-08)")},
       Range(NullTimestamp(), Timestamp(1267628310654321))},
      {{String("[2009-02-01 05:57:29.123456-08, UNBOUNDED)")},
       Range(Timestamp(1233496649123456), NullTimestamp())},
      {{String("[UNBOUNDED, UNBOUNDED)")},
       Range(NullTimestamp(), NullTimestamp())},

      // STRING -> RANGE<DATE> bad formats
      {{String("2009-02-01, 2009-02-13)")},
       Null(types::DateRangeType()),
       OUT_OF_RANGE},
      {{String("[2009-02-01, 2009-02-13")},
       Null(types::DateRangeType()),
       OUT_OF_RANGE},
      {{String("2009-02-01, 2009-02-13")},
       Null(types::DateRangeType()),
       OUT_OF_RANGE},
      {{String("[2009-02-01,2009-02-13)")},
       Null(types::DateRangeType()),
       OUT_OF_RANGE},
      // STRING -> RANGE<DATETIME> bad formats
      {{String("2009-02-01 13:57:29.123456, 2010-03-03 14:58:30.654321)")},
       Null(types::DatetimeRangeType()),
       OUT_OF_RANGE},
      {{String("[2009-02-01 13:57:29.123456, 2010-03-03 14:58:30.654321")},
       Null(types::DatetimeRangeType()),
       OUT_OF_RANGE},
      {{String("2009-02-01 13:57:29.123456, 2010-03-03 14:58:30.654321")},
       Null(types::DatetimeRangeType()),
       OUT_OF_RANGE},
      {{String("[2009-02-01 13:57:29.123456,2010-03-03 14:58:30.654321)")},
       Null(types::DatetimeRangeType()),
       OUT_OF_RANGE},
      // STRING -> RANGE<TIMESTAMP> bad formats
      {{String(
           "2009-02-01 13:57:29.123456-08, 2010-03-03 14:58:30.654321-08)")},
       Null(types::TimestampRangeType()),
       OUT_OF_RANGE},
      {{String(
           "[2009-02-01 13:57:29.123456-08, 2010-03-03 14:58:30.654321-08")},
       Null(types::TimestampRangeType()),
       OUT_OF_RANGE},
      {{String("2009-02-01 13:57:29.123456-08, 2010-03-03 14:58:30.654321-08")},
       Null(types::TimestampRangeType()),
       OUT_OF_RANGE},
      {{String(
           "[2009-02-01 13:57:29.123456-08,2010-03-03 14:58:30.654321-08)")},
       Null(types::TimestampRangeType()),
       OUT_OF_RANGE},
  };

  auto is_datetime_range_value = [](const Value& value) -> bool {
    return value.type()->IsRangeType() &&
           value.type()->AsRange()->element_type()->IsDatetime();
  };
  for (auto& test : tests) {
    test.AddRequiredFeature(FEATURE_RANGE_TYPE);
    // Add FEATURE_CIVIL_TIME if the source/result value is of DATETIME
    // type.
    if (is_datetime_range_value(test.param(0)) ||
        is_datetime_range_value(test.result())) {
      test.AddRequiredFeature(FEATURE_CIVIL_TIME);
    }
  }
  return tests;
}

static std::vector<QueryParamsWithResult> GetFunctionTestsCastIntPorted() {
  return {
    // INT32 -> INT32
    {{Int32(int32max)}, Int32(int32max)},

    // INT32 -> INT64
    {{Int32(int32max)}, Int64(int32max)},
    {{Int32(3)}, Int64(3)},
    {{Int32(-3)}, Int64(-3)},
    {{Int32(int32min)}, Int64(int32min)},

    // INT32 -> UINT32
    {{Int32(int32max)}, Uint32(int32max)},
    {{Int32(3)}, Uint32(3)},
    {{Int32(0)}, Uint32(0)},
    {{Int32(-3)},        NullUint32(), OUT_OF_RANGE},
    {{Int32(int32min)},  NullUint32(), OUT_OF_RANGE},

    // INT32 -> UINT64
    {{Int32(int32max)}, Uint64(int32max)},
    {{Int32(3)}, Uint64(3)},
    {{Int32(0)}, Uint64(0)},
    {{Int32(-3)},        NullUint64(), OUT_OF_RANGE},
    {{Int32(int32min)},  NullUint64(), OUT_OF_RANGE},

    // INT32 -> FLOAT
    {{Int32(int32max)}, Float(int32max)},
    {{Int32(3)}, Float(3)},
    {{Int32(-3)}, Float(-3)},
    {{Int32(int32min)}, Float(int32min)},

    // INT32 -> DOUBLE
    {{Int32(int32max)}, Double(int32max)},
    {{Int32(3)}, Double(3)},
    {{Int32(-3)}, Double(-3)},
    {{Int32(int32min)}, Double(int32min)},

    // INT64 -> INT32
    {{Int64(int64max)},           NullInt32(), OUT_OF_RANGE},
    {{Int64(int32max_plus_one)},  NullInt32(), OUT_OF_RANGE},
    {{Int64(int32max)}, Int32(int32max)},
    {{Int64(3)}, Int32(3)},
    {{Int64(-3)}, Int32(-3)},
    {{Int64(int32min)}, Int32(int32min)},
    {{Int64(int32min_minus_one)}, NullInt32(), OUT_OF_RANGE},
    {{Int64(int64min)},           NullInt32(), OUT_OF_RANGE},

    // INT64 -> INT64
    {{Int64(int64max)}, Int64(int64max)},

    // INT64 -> UINT32
    {{Int64(int64max)},            NullUint32(), OUT_OF_RANGE},
    {{Int64(uint32max_plus_one)},  NullUint32(), OUT_OF_RANGE},
    {{Int64(uint32max)}, Uint32(uint32max)},
    {{Int64(3)}, Uint32(3)},
    {{Int64(0)}, Uint32(0)},
    {{Int64(-1)},                  NullUint32(), OUT_OF_RANGE},
    {{Int64(int64min)},            NullUint32(), OUT_OF_RANGE},

    // INT64 -> UINT64
    {{Int64(int64max)}, Uint64(int64max)},
    {{Int64(3)}, Uint64(3)},
    {{Int64(0)}, Uint64(0)},
    {{Int64(-1)},                  NullUint64(), OUT_OF_RANGE},
    {{Int64(int64min)},            NullUint64(), OUT_OF_RANGE},

    // INT64 -> FLOAT
    {{Int64(int64max)}, Float(int64max)},
    {{Int64(3)}, Float(3)},
    {{Int64(-3)}, Float(-3)},
    {{Int64(int64min)}, Float(int64min)},

    // INT64 -> DOUBLE
    {{Int64(int64max)}, Double(int64max)},
    {{Int64(3)}, Double(3)},
    {{Int64(-3)}, Double(-3)},
    {{Int64(int64min)}, Double(int64min)},
  };
}

static std::vector<QueryParamsWithResult> GetFunctionTestsCastUintPorted() {
  return {
    // UINT32 -> INT32
    {{Uint32(uint32max)},         NullInt32(), OUT_OF_RANGE},
    {{Uint32(int32max_plus_one)}, NullInt32(), OUT_OF_RANGE},
    {{Uint32(int32max)}, Int32(int32max)},
    {{Uint32(3)}, Int32(3)},
    {{Uint32(0)}, Int32(0)},

    // UINT32 -> INT64
    {{Uint32(uint32max)}, Int64(uint32max)},
    {{Uint32(3)}, Int64(3)},
    {{Uint32(0)}, Int64(0)},

    // UINT32 -> UINT32
    {{Uint32(uint32max)}, Uint32(uint32max)},

    // UINT32 -> UINT64
    {{Uint32(uint32max)}, Uint64(uint32max)},
    {{Uint32(3)}, Uint64(3)},
    {{Uint32(0)}, Uint64(0)},

    // UINT32 -> FLOAT
    {{Uint32(uint32max)}, Float(uint32max)},
    {{Uint32(3)}, Float(3)},
    {{Uint32(0)}, Float(0)},

    // UINT32 -> DOUBLE
    {{Uint32(uint32max)}, Double(uint32max)},
    {{Uint32(3)}, Double(3)},
    {{Uint32(0)}, Double(0)},

    // UINT64 -> INT32
    {{Uint64(uint64max)},         NullInt32(), OUT_OF_RANGE},
    {{Uint64(int32max_plus_one)}, NullInt32(), OUT_OF_RANGE},
    {{Uint64(int32max)}, Int32(int32max)},
    {{Uint64(3)}, Int32(3)},
    {{Uint64(0)}, Int32(0)},

    // UINT64 -> INT64
    {{Uint64(uint64max)},         NullInt64(), OUT_OF_RANGE},
    {{Uint64(int64max_plus_one)}, NullInt64(), OUT_OF_RANGE},
    {{Uint64(int64max)}, Int64(int64max)},
    {{Uint64(3)}, Int64(3)},
    {{Uint64(0)}, Int64(0)},

    // UINT64 -> UINT32
    {{Uint64(uint64max)},          NullUint32(), OUT_OF_RANGE},
    {{Uint64(uint32max_plus_one)}, NullUint32(), OUT_OF_RANGE},
    {{Uint64(uint32max)}, Uint32(uint32max)},
    {{Uint64(3)}, Uint32(3)},
    {{Uint64(0)}, Uint32(0)},

    // UINT64 -> UINT64
    {{Uint64(uint64max)}, Uint64(uint64max)},

    // UINT64 -> FLOAT
    {{Uint64(uint64max)}, Float(uint64max)},
    {{Uint64(3)}, Float(3)},
    {{Uint64(0)}, Float(0)},

    // UINT64 -> DOUBLE
    {{Uint64(uint64max)}, Double(uint64max)},
    {{Uint64(3)}, Double(3)},
    {{Uint64(0)}, Double(0)},
  };
}

static std::vector<QueryParamsWithResult> GetFunctionTestsCastFloatPorted() {
  return {
    // FLOAT -> INT32
    {{Float(floatmax)},          NullInt32(), OUT_OF_RANGE},
    {{Float(int32max)},          NullInt32(), OUT_OF_RANGE},
    {{Float(3.5001)}, Int32(4)},
    {{Float(3.5)}, Int32(4)},
    {{Float(3)}, Int32(3)},
    {{Float(2.5001)}, Int32(3)},
    {{Float(2.5)}, Int32(3)},
    {{Float(2.4999)}, Int32(2)},
    {{Float(-2.4999)}, Int32(-2)},
    {{Float(-2.5)}, Int32(-3)},
    {{Float(-2.5001)}, Int32(-3)},
    {{Float(-3)}, Int32(-3)},
    {{Float(-3.5)}, Int32(-4)},
    {{Float(-3.5001)}, Int32(-4)},
    {{Float(int32min)}, Int32(int32min)},
    {{Float(int32min - 1.0)}, Int32(int32min)},
    {{Float(floatmin)},          NullInt32(), OUT_OF_RANGE},

    {{Float(float_pos_inf)}, NullInt32(), OUT_OF_RANGE},
    {{Float(float_neg_inf)}, NullInt32(), OUT_OF_RANGE},
    {{Float(float_nan)},     NullInt32(), OUT_OF_RANGE},

    // FLOAT -> INT64
    {{Float(floatmax)},       NullInt64(), OUT_OF_RANGE},
    {{Float(int64max)},       NullInt64(), OUT_OF_RANGE},
    {{Float(3.5001)}, Int64(4)},
    {{Float(3.5)}, Int64(4)},
    {{Float(3)}, Int64(3)},
    {{Float(2.5001)}, Int64(3)},
    {{Float(2.5)}, Int64(3)},
    {{Float(2.4999)}, Int64(2)},
    {{Float(-2.4999)}, Int64(-2)},
    {{Float(-2.5)}, Int64(-3)},
    {{Float(-2.5001)}, Int64(-3)},
    {{Float(-3)}, Int64(-3)},
    {{Float(-3.5)}, Int64(-4)},
    {{Float(-3.5001)}, Int64(-4)},
    {{Float(int64min)}, Int64(int64min)},
    {{Float(int64min - 1.0)}, Int64(int64min)},
    {{Float(floatmin)},       NullInt64(), OUT_OF_RANGE},

    {{Float(float_pos_inf)}, NullInt64(), OUT_OF_RANGE},
    {{Float(float_neg_inf)}, NullInt64(), OUT_OF_RANGE},
    {{Float(float_nan)},     NullInt64(), OUT_OF_RANGE},

    // FLOAT -> UINT32
    {{Float(floatmax)},        NullUint32(), OUT_OF_RANGE},
    {{Float(uint32max)},       NullUint32(), OUT_OF_RANGE},
    {{Float(3.5001)}, Uint32(4)},
    {{Float(3.5)}, Uint32(4)},
    {{Float(3)}, Uint32(3)},
    {{Float(2.5001)}, Uint32(3)},
    {{Float(2.5)}, Uint32(3)},
    {{Float(2.4999)}, Uint32(2)},
    {{Float(0)}, Uint32(0)},
    {{Float(-1)},              NullUint32(), OUT_OF_RANGE},
    {{Float(floatmin)},        NullUint32(), OUT_OF_RANGE},

    {{Float(float_pos_inf)}, NullUint32(), OUT_OF_RANGE},
    {{Float(float_neg_inf)}, NullUint32(), OUT_OF_RANGE},
    {{Float(float_nan)},     NullUint32(), OUT_OF_RANGE},

    // FLOAT -> UINT64
    {{Float(floatmax)},        NullUint64(), OUT_OF_RANGE},
    {{Float(uint64max)},       NullUint64(), OUT_OF_RANGE},
    {{Float(3.5001)}, Uint64(4)},
    {{Float(3.5)}, Uint64(4)},
    {{Float(3)}, Uint64(3)},
    {{Float(2.5001)}, Uint64(3)},
    {{Float(2.5)}, Uint64(3)},
    {{Float(2.4999)}, Uint64(2)},
    {{Float(0)}, Uint64(0)},
    {{Float(-1)},              NullUint64(), OUT_OF_RANGE},
    {{Float(floatmin)},        NullUint64(), OUT_OF_RANGE},

    {{Float(float_pos_inf)}, NullUint64(), OUT_OF_RANGE},
    {{Float(float_neg_inf)}, NullUint64(), OUT_OF_RANGE},
    {{Float(float_nan)},     NullUint64(), OUT_OF_RANGE},

    // FLOAT -> FLOAT
    {{Float(floatmax)}, Float(floatmax)},

    {{Float(float_pos_inf)}, Float(float_pos_inf)},
    {{Float(float_neg_inf)}, Float(float_neg_inf)},
    {{Float(float_nan)}, Float(float_nan)},

    // FLOAT -> DOUBLE
    {{Float(floatmax)}, Double(floatmax)},
    {{Float(3)}, Double(3)},
    {{Float(floatminpositive)}, Double(floatminpositive)},
    {{Float(-floatminpositive)}, Double(-floatminpositive)},
    {{Float(-3)}, Double(-3)},
    {{Float(floatmin)}, Double(floatmin)},

    {{Float(float_pos_inf)}, Double(double_pos_inf)},
    {{Float(float_neg_inf)}, Double(double_neg_inf)},
    {{Float(float_nan)}, Double(double_nan)},
  };
}

static std::vector<QueryParamsWithResult> GetFunctionTestsCastDoublePorted() {
  return {
    // DOUBLE -> INT32
    {{Double(doublemax)},          NullInt32(), OUT_OF_RANGE},
    {{Double(int32max + 0.0001)},  NullInt32(), OUT_OF_RANGE},
    {{Double(int32max)}, Int32(int32max)},
    {{Double(3.5001)}, Int32(4)},
    {{Double(3.5)}, Int32(4)},
    {{Double(3)}, Int32(3)},
    {{Double(2.5001)}, Int32(3)},
    {{Double(2.5)}, Int32(3)},
    {{Double(2.4999)}, Int32(2)},
    {{Double(-2.4999)}, Int32(-2)},
    {{Double(-2.5)}, Int32(-3)},
    {{Double(-2.5001)}, Int32(-3)},
    {{Double(-3)}, Int32(-3)},
    {{Double(-3.5)}, Int32(-4)},
    {{Double(-3.5001)}, Int32(-4)},
    {{Double(int32min)}, Int32(int32min)},
    {{Double(int32min - 0.0001)},  NullInt32(), OUT_OF_RANGE},
    {{Double(doublemin)},          NullInt32(), OUT_OF_RANGE},

    {{Double(double_pos_inf)}, NullInt32(), OUT_OF_RANGE},
    {{Double(double_neg_inf)}, NullInt32(), OUT_OF_RANGE},
    {{Double(double_nan)},     NullInt32(), OUT_OF_RANGE},

    // DOUBLE -> INT64
    {{Double(doublemax)},      NullInt64(), OUT_OF_RANGE},
    {{Double(int64max)},       NullInt64(), OUT_OF_RANGE},
    {{Double(3.5001)}, Int64(4)},
    {{Double(3.5)}, Int64(4)},
    {{Double(3)}, Int64(3)},
    {{Double(2.5001)}, Int64(3)},
    {{Double(2.5)}, Int64(3)},
    {{Double(2.4999)}, Int64(2)},
    {{Double(-2.4999)}, Int64(-2)},
    {{Double(-2.5)}, Int64(-3)},
    {{Double(-2.5001)}, Int64(-3)},
    {{Double(-3)}, Int64(-3)},
    {{Double(-3.5)}, Int64(-4)},
    {{Double(-3.5001)}, Int64(-4)},
    {{Double(int64min)}, Int64(int64min)},
    {{Double(int64min - 1.0)}, Int64(int64min)},
    {{Double(doublemin)},      NullInt64(), OUT_OF_RANGE},

    {{Double(double_pos_inf)}, NullInt64(), OUT_OF_RANGE},
    {{Double(double_neg_inf)}, NullInt64(), OUT_OF_RANGE},
    {{Double(double_nan)},     NullInt64(), OUT_OF_RANGE},

    // DOUBLE -> UINT32
    {{Double(doublemax)},           NullUint32(), OUT_OF_RANGE},
    {{Double(uint32max + 0.0001)},  NullUint32(), OUT_OF_RANGE},
    {{Double(uint32max)}, Uint32(uint32max)},
    {{Double(3.5001)}, Uint32(4)},
    {{Double(3.5)}, Uint32(4)},
    {{Double(3)}, Uint32(3)},
    {{Double(2.5001)}, Uint32(3)},
    {{Double(2.5)}, Uint32(3)},
    {{Double(2.4999)}, Uint32(2)},
    {{Double(0)}, Uint32(0)},
    {{Double(-0.0001)},             NullUint32(), OUT_OF_RANGE},
    {{Double(doublemin)},           NullUint32(), OUT_OF_RANGE},

    {{Double(double_pos_inf)}, NullUint32(), OUT_OF_RANGE},
    {{Double(double_neg_inf)}, NullUint32(), OUT_OF_RANGE},
    {{Double(double_nan)},     NullUint32(), OUT_OF_RANGE},

    // DOUBLE -> UINT64
    {{Double(doublemax)},           NullUint64(), OUT_OF_RANGE},
    {{Double(uint64max)},           NullUint64(), OUT_OF_RANGE},
    {{Double(3.5001)}, Uint64(4)},
    {{Double(3.5)}, Uint64(4)},
    {{Double(3)}, Uint64(3)},
    {{Double(2.5001)}, Uint64(3)},
    {{Double(2.5)}, Uint64(3)},
    {{Double(2.4999)}, Uint64(2)},
    {{Double(0)}, Uint64(0)},
    {{Double(-0.0001)},             NullUint64(), OUT_OF_RANGE},
    {{Double(doublemin)},           NullUint64(), OUT_OF_RANGE},

    {{Double(double_pos_inf)}, NullUint64(), OUT_OF_RANGE},
    {{Double(double_neg_inf)}, NullUint64(), OUT_OF_RANGE},
    {{Double(double_nan)},     NullUint64(), OUT_OF_RANGE},

    // DOUBLE -> FLOAT
    {{Double(doublemax)}, NullFloat(), OUT_OF_RANGE},
    {{Double(floatmax)}, Float(floatmax)},
    {{Double(3)}, Float(3)},
    {{Double(floatmin)}, Float(floatmin)},
    {{Double(doublemin)}, NullFloat(), OUT_OF_RANGE},

    {{Double(double_pos_inf)}, Float(float_pos_inf)},
    {{Double(double_neg_inf)}, Float(float_neg_inf)},
    {{Double(double_nan)}, Float(float_nan)},

    // DOUBLE -> DOUBLE
    {{Double(doublemax)}, Double(doublemax)},

    {{Double(double_pos_inf)}, Double(double_pos_inf)},
    {{Double(double_neg_inf)}, Double(double_neg_inf)},
    {{Double(double_nan)}, Double(double_nan)},
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastInteger() {
  return {
      {{Int32(0)}, Int32(0)},
      {{Int64(0)}, Int64(0)},
      {{Uint32(0)}, Uint32(0)},
      {{Uint64(0)}, Uint64(0)},
      {{Float(0)}, Float(0)},
      {{Double(0)}, Double(0)},
      QueryParamsWithResult(
          {{Value::Numeric(NumericValue())}, Value::Numeric(NumericValue())})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{Value::BigNumeric(BigNumericValue())},
                             Value::BigNumeric(BigNumericValue())})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      {{NullInt32()}, NullInt32()},
      {{NullInt64()}, NullInt64()},
      {{NullUint32()}, NullUint32()},
      {{NullUint64()}, NullUint64()},
      {{NullFloat()}, NullFloat()},
      {{NullDouble()}, NullDouble()},
      {{NullDate()}, NullDate()},
      QueryParamsWithResult({{NullNumeric()}, NullNumeric()})
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({{NullBigNumeric()}, NullBigNumeric()})
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      // INT32
      {{Int32(int32max)}, Int32(int32max)},
      {{Int32(int32max)}, Int64(int32max)},
      {{Int32(int32max)}, Uint32(int32max)},
      {{Int32(int32max)}, Uint64(int32max)},
      {{Int32(int32max)}, Float(int32max)},
      {{Int32(int32max)}, Double(int32max)},
      {{Int32(-1)}, NullUint32(), OUT_OF_RANGE},
      {{Int32(-1)}, NullUint64(), OUT_OF_RANGE},
      {{Int32(int32min)}, Int32(int32min)},
      {{Int32(int32min)}, Int64(int32min)},
      {{Int32(int32min)}, Float(int32min)},
      {{Int32(int32min)}, Double(int32min)},
      {{Int32(int32min)}, Int32(int32min)},
      {{Int32(int32min)}, Int64(int32min)},
      {{Int32(int32min)}, Float(int32min)},
      {{Int32(int32min)}, Double(int32min)},
      QueryParamsWithResult({Int32(int32max)},
                            Value::Numeric(NumericValue(int32max)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Int32(int32min)},
                            Value::Numeric(NumericValue(int32min)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Int32(int32max)},
                            Value::BigNumeric(BigNumericValue(int32max)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Int32(int32min)},
                            Value::BigNumeric(BigNumericValue(int32min)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      // INT64
      {{static_cast<int64_t>(int32max)}, Int32(int32max)},
      {{static_cast<int64_t>(int32max) + 1}, NullInt32(), OUT_OF_RANGE},
      {{Int64(int64max)}, Int64(int64max)},
      {{Int64(uint32max)}, Uint32(uint32max)},
      {{static_cast<int64_t>(uint32max) + 1}, NullUint32(), OUT_OF_RANGE},
      {{Int64(int64max)}, Uint64(int64max)},
      {{Int64(int64max)}, Float(int64max)},
      {{Int64(int64max)}, Double(int64max)},
      {{Int64(-1)}, NullUint32(), OUT_OF_RANGE},
      {{Int64(-1)}, NullUint64(), OUT_OF_RANGE},
      {{Int64(int32min)}, Int32(int32min)},
      {{static_cast<int64_t>(int32min) - 1}, NullInt32(), OUT_OF_RANGE},
      {{Int64(int64min)}, Int64(int64min)},
      {{Int64(int64min)}, NullUint32(), OUT_OF_RANGE},
      {{Int64(int64min)}, NullUint64(), OUT_OF_RANGE},
      {{Int64(int64min)}, Float(int64min)},
      {{Int64(int64min)}, Double(int64min)},
      QueryParamsWithResult({Int64(int64max)},
                            Value::Numeric(NumericValue(int64max)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Int64(int64min)},
                            Value::Numeric(NumericValue(int64min)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Int64(int64max)},
                            Value::BigNumeric(BigNumericValue(int64max)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Int64(int64min)},
                            Value::BigNumeric(BigNumericValue(int64min)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      // UINT32
      {{Uint32(uint32max)}, NullInt32(), OUT_OF_RANGE},
      {{Uint32(uint32max)}, Int64(uint32max)},
      {{Uint32(uint32max)}, Uint32(uint32max)},
      {{Uint32(uint32max)}, Uint64(uint32max)},
      {{Uint32(uint32max)}, Float(uint32max)},
      {{Uint32(uint32max)}, Double(uint32max)},
      {{Uint32(0)}, Int32(0)},
      {{Uint32(0)}, Int64(0)},
      {{Uint32(0)}, Uint32(0)},
      {{Uint32(0)}, Uint64(0)},
      {{Uint32(0)}, Float(0)},
      {{Uint32(0)}, Double(0)},
      QueryParamsWithResult({Uint32(uint32max)},
                            Value::Numeric(NumericValue(uint32max)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Uint32(0)}, Value::Numeric(NumericValue()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Uint32(uint32max)},
                            Value::BigNumeric(BigNumericValue(uint32max)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Uint32(0)}, Value::BigNumeric(BigNumericValue()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),

      // UINT64
      {{Uint64(int32max)}, Int32(int32max)},
      {{static_cast<uint64_t>(int32max) + 1}, NullInt32(), OUT_OF_RANGE},
      {{static_cast<uint64_t>(int64max)}, Int64(int64max)},
      {{static_cast<uint64_t>(int64max) + 1}, NullInt64(), OUT_OF_RANGE},
      {{Uint64(uint32max)}, Uint32(uint32max)},
      {{static_cast<uint64_t>(uint32max) + 1}, NullUint32(), OUT_OF_RANGE},
      {{Uint64(uint64max)}, Uint64(uint64max)},
      {{Uint64(uint64max)}, Float(uint64max)},
      {{Uint64(uint64max)}, Double(uint64max)},
      {{Uint64(0)}, Int32(0)},
      {{Uint64(0)}, Int64(0)},
      {{Uint64(0)}, Uint32(0)},
      {{Uint64(0)}, Uint64(0)},
      {{Uint64(0)}, Float(0)},
      {{Uint64(0)}, Double(0)},
      QueryParamsWithResult({Uint64(uint64max)},
                            Value::Numeric(NumericValue(uint64max)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Uint64(0)}, Value::Numeric(NumericValue()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Uint64(uint64max)},
                            Value::BigNumeric(BigNumericValue(uint64max)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Uint64(0)}, Value::BigNumeric(BigNumericValue()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastFloat() {
  // Maximal integers that roundtrip via integer-float-integer casts.
  int32_t float_int32_in = int32max - (1 << 6);
  int32_t float_int32_plus = float_int32_in + 1;
  int32_t float_int32_out = int32max - (1 << 7) + 1;
  uint32_t float_uint32_in = uint32max - (1 << 7);
  uint32_t float_uint32_plus = float_uint32_in + 1;
  uint32_t float_uint32_out = uint32max - (1 << 8) + 1;
  int64_t float_int64_in = int64max - (1ll << 38);
  int64_t float_int64_plus = float_int64_in + 1;
  int64_t float_int64_out = int64max - (1ll << 39) + 1;
  uint64_t float_uint64_in = uint64max - (1ull << 39);
  uint64_t float_uint64_plus = float_uint64_in + 1;
  uint64_t float_uint64_out = uint64max - (1ull << 40) + 1;
  return {
      {{static_cast<float>(float_int32_in)}, Int32(float_int32_out)},
      {{static_cast<float>(float_int64_in)}, Int64(float_int64_out)},
      {{static_cast<float>(float_uint32_in)}, Uint32(float_uint32_out)},
      {{static_cast<float>(float_uint64_in)}, Uint64(float_uint64_out)},
      {{static_cast<float>(float_int32_plus)}, NullInt32(), OUT_OF_RANGE},
      {{static_cast<float>(float_int64_plus)}, NullInt64(), OUT_OF_RANGE},
      {{static_cast<float>(float_uint32_plus)}, NullUint32(), OUT_OF_RANGE},
      {{static_cast<float>(float_uint64_plus)}, NullUint64(), OUT_OF_RANGE},
      {{static_cast<float>(int32min)}, Int32(int32min)},
      {{static_cast<float>(int64min)}, Int64(int64min)},
      {{static_cast<float>(-1)}, NullUint32(), OUT_OF_RANGE},
      {{static_cast<float>(-1)}, NullUint64(), OUT_OF_RANGE},
      QueryParamsWithResult({Float(float_uint64_in)},
                            Value::Numeric(NumericValue(float_uint64_out)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      // Smallest positive number.
      QueryParamsWithResult({Float(std::numeric_limits<float>::min())},
                            Value::Numeric(NumericValue(0)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Float(static_cast<float>(NumericValue::MaxValue().ToDouble()))},
          NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Float(std::numeric_limits<float>::max())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Float(std::numeric_limits<float>::quiet_NaN())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Float(std::numeric_limits<float>::infinity())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Float(-std::numeric_limits<float>::infinity())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Float(float_uint64_in)},
          Value::BigNumeric(BigNumericValue(float_uint64_out)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Float(std::numeric_limits<float>::min())},
          Value::BigNumeric(BigNumericValue::FromString("1e-38").value()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Float(std::numeric_limits<float>::max())},
          Value::BigNumeric(BigNumericValue::FromString(
                                "340282346638528859811704183484516925440")
                                .value()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Float(std::numeric_limits<float>::quiet_NaN())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Float(std::numeric_limits<float>::infinity())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Float(-std::numeric_limits<float>::infinity())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      {{std::numeric_limits<float>::quiet_NaN()},
       std::numeric_limits<float>::quiet_NaN()},
      {{std::numeric_limits<float>::quiet_NaN()},
       std::numeric_limits<double>::quiet_NaN()},
      {{std::numeric_limits<float>::infinity()},
       std::numeric_limits<float>::infinity()},
      {{std::numeric_limits<float>::infinity()},
       std::numeric_limits<double>::infinity()},
      {{-std::numeric_limits<float>::infinity()},
       -std::numeric_limits<float>::infinity()},
      {{-std::numeric_limits<float>::infinity()},
       -std::numeric_limits<double>::infinity()},
      {{std::numeric_limits<float>::max()}, NullInt32(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::max()}, NullInt64(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::max()}, NullUint32(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::max()}, NullUint64(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::max()}, std::numeric_limits<float>::max()},
      {{std::numeric_limits<float>::max()},
       static_cast<double>(std::numeric_limits<float>::max())},
      {{std::numeric_limits<float>::lowest()}, NullInt32(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::lowest()}, NullInt64(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::lowest()}, NullUint32(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::lowest()}, NullUint64(), OUT_OF_RANGE},
      {{std::numeric_limits<float>::lowest()},
       std::numeric_limits<float>::lowest()},
      {{std::numeric_limits<float>::lowest()},
       static_cast<double>(std::numeric_limits<float>::lowest())},
      {{Float(0)}, Double(0)},
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastDouble() {
  // Maximal integers that roundtrip via integer-double-integer casts.
  int64_t double_int64_in = int64max - (1ll << 9);
  int64_t double_int64_plus = double_int64_in + 1;
  int64_t double_int64_out = int64max - (1ll << 10) + 1;
  uint64_t double_uint64_in = uint64max - (1ull << 10);
  uint64_t double_uint64_plus = double_uint64_in + 1;
  uint64_t double_uint64_out = uint64max - (1ull << 11) + 1;
  return {
      {{static_cast<double>(int32max)}, Int32(int32max)},
      {{static_cast<double>(double_int64_in)}, Int64(double_int64_out)},
      {{static_cast<double>(double_uint64_in)}, Uint64(double_uint64_out)},
      {{static_cast<double>(double_int64_plus)}, NullInt64(), OUT_OF_RANGE},
      {{static_cast<double>(double_uint64_plus)}, NullUint64(), OUT_OF_RANGE},
      {{static_cast<double>(uint32max)}, Uint32(uint32max)},
      {{static_cast<double>(int32min)}, Int32(int32min)},
      {{static_cast<double>(int64min)}, Int64(int64min)},
      {{static_cast<double>(-1)}, NullUint32(), OUT_OF_RANGE},
      {{static_cast<double>(-1)}, NullUint64(), OUT_OF_RANGE},
      QueryParamsWithResult({Double(double_uint64_in)},
                            Value::Numeric(NumericValue(double_uint64_out)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::min())},
                            Value::Numeric(NumericValue(0)))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Double(NumericValue::MaxValue().ToDouble())},
                            Value::Numeric(NumericValue::FromDouble(
                                               99999999999999991433150857216.0)
                                               .value()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::max())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::quiet_NaN())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::infinity())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Double(-std::numeric_limits<double>::infinity())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      {{std::numeric_limits<double>::quiet_NaN()},
       std::numeric_limits<float>::quiet_NaN()},
      {{std::numeric_limits<double>::quiet_NaN()},
       std::numeric_limits<double>::quiet_NaN()},
      {{std::numeric_limits<double>::infinity()},
       std::numeric_limits<float>::infinity()},
      {{std::numeric_limits<double>::infinity()},
       std::numeric_limits<double>::infinity()},
      {{-std::numeric_limits<double>::infinity()},
       -std::numeric_limits<float>::infinity()},
      {{-std::numeric_limits<double>::infinity()},
       -std::numeric_limits<double>::infinity()},
      {{std::numeric_limits<double>::max()}, NullFloat(), OUT_OF_RANGE},
      {{static_cast<double>(std::numeric_limits<float>::max()) * 2},
       NullFloat(),
       OUT_OF_RANGE},
      {{std::numeric_limits<double>::max()},
       std::numeric_limits<double>::max()},
      {{std::numeric_limits<double>::lowest()}, NullInt32(), OUT_OF_RANGE},
      {{std::numeric_limits<double>::lowest()}, NullInt64(), OUT_OF_RANGE},
      {{std::numeric_limits<double>::lowest()}, NullUint32(), OUT_OF_RANGE},
      {{std::numeric_limits<double>::lowest()}, NullUint64(), OUT_OF_RANGE},
      {{std::numeric_limits<double>::lowest()}, NullFloat(), OUT_OF_RANGE},
      {{static_cast<double>(std::numeric_limits<float>::lowest()) * 2},
       NullFloat(),
       OUT_OF_RANGE},
      {{std::numeric_limits<double>::lowest()},
       std::numeric_limits<double>::lowest()},
      {{Double(0)}, Float(0)},
      QueryParamsWithResult(
          {Double(double_uint64_in)},
          Value::BigNumeric(BigNumericValue(double_uint64_out)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::min())},
                            Value::BigNumeric(BigNumericValue(0)))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Double(BigNumericValue::MaxValue().ToDouble())},
          Value::BigNumeric(BigNumericValue::FromString(
                                "578960446186580955070694765308237840384")
                                .value()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::max())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::quiet_NaN())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Double(std::numeric_limits<double>::infinity())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Double(-std::numeric_limits<double>::infinity())},
                            NullBigNumeric(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastNumericValue() {
  const std::set<LanguageFeature> kNumericFeatureSet = {
      FEATURE_NUMERIC_TYPE, FEATURE_BIGNUMERIC_TYPE};
  return {
      // NUMERIC -> Other numeric types
      QueryParamsWithResult({Value::Numeric(NumericValue(int32max))},
                            Int32(int32max))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(int32min))},
                            Int32(int32min))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(int64max))},
                            Int64(int64max))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(int64min))},
                            Int64(int64min))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(uint32max))},
                            Uint32(uint32max))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue())}, Uint32(0))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(uint64max))},
                            Uint64(uint64max))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue())}, Uint64(0))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(uint64max))},
                            NullInt32(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(uint64max))},
                            NullUint32(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(uint64max))},
                            NullInt64(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue::MaxValue())},
                            NullUint64(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(int64max))},
                            Value::BigNumeric(BigNumericValue(int64max)))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::Numeric(NumericValue(int64min))},
                            Value::BigNumeric(BigNumericValue(int64min)))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::Numeric(NumericValue(uint64max))},
                            Value::BigNumeric(BigNumericValue(uint64max)))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::Numeric(NumericValue())},
                            Value::BigNumeric(BigNumericValue()))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult(
          {Value::Numeric(NumericValue::MaxValue())},
          Value::BigNumeric(BigNumericValue(NumericValue::MaxValue())))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult(
          {Value::Numeric(NumericValue::MinValue())},
          Value::BigNumeric(BigNumericValue(NumericValue::MinValue())))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult(
          {Value::Numeric(NumericValue::FromDouble(3.14159).value())},
          Float(3.14159f))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(3))}, Float(3))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue())}, Float(0))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::Numeric(
              NumericValue::FromStringStrict("0.000000001").value())},
          Float(1e-09f))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue::MaxValue())},
                            Float(1e+29f))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue::MinValue())},
                            Float(-1e+29f))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Double(3.14159)},
          Value::Numeric(NumericValue::FromDouble(3.14159).value()))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue(3))}, Double(3))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue())}, Double(0))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::Numeric(
              NumericValue::FromStringStrict("0.000000001").value())},
          Double(1e-09))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue::MaxValue())},
                            Double(1e+29))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),
      QueryParamsWithResult({Value::Numeric(NumericValue::MinValue())},
                            Double(-1e+29))
          .WrapWithFeature(FEATURE_NUMERIC_TYPE),

      // BIGNUMERIC -> Other numeric types
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(int32max))},
                            Int32(int32max))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(int32min))},
                            Int32(int32min))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(int64max))},
                            Int64(int64max))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(int64min))},
                            Int64(int64min))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(uint32max))},
                            Uint32(uint32max))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue())}, Uint32(0))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(uint64max))},
                            Uint64(uint64max))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue())}, Uint64(0))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(uint64max))},
                            NullInt32(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(uint64max))},
                            NullUint32(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(uint64max))},
                            NullInt64(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MaxValue())},
                            NullUint64(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(int64max))},
                            Value::Numeric(NumericValue(int64max)))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(int64min))},
                            Value::Numeric(NumericValue(int64min)))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(uint64max))},
                            Value::Numeric(NumericValue(uint64max)))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue())},
                            Value::Numeric(NumericValue()))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue(NumericValue::MinValue()))},
          Value::Numeric(NumericValue::MinValue()))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue(NumericValue::MaxValue()))},
          Value::Numeric(NumericValue::MaxValue()))
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MinValue())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MaxValue())},
                            NullNumeric(), OUT_OF_RANGE)
          .WrapWithFeatureSet(kNumericFeatureSet),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue())}, Float(0))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(3))}, Float(3))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromDouble(3.14159).value())},
          Float(3.14159f))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromDouble(3.9999997).value())},
          Float(3.9999998f))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      // 2^127 * (2 - 2^(-24) - 2^(-53)) - 1
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromStringStrict(
                                 "-340282356779733642748073463979561713663")
                                 .value())},
          Float(std::numeric_limits<float>::lowest()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromStringStrict(
                                 "340282356779733642748073463979561713663")
                                 .value())},
          Float(std::numeric_limits<float>::max()))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MaxValue())},
                            NullFloat(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MinValue())},
                            NullFloat(), OUT_OF_RANGE)
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue())}, Double(0))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue(3))}, Double(3))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(BigNumericValue::FromDouble(3.14159).value())},
          Double(3.14159))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult(
          {Value::BigNumeric(
              BigNumericValue::FromDouble(3.9999999999999997).value())},
          Double(3.9999999999999995))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MaxValue())},
                            Double(5.7896044618658096e+38))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
      QueryParamsWithResult({Value::BigNumeric(BigNumericValue::MinValue())},
                            Double(-5.7896044618658096e+38))
          .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastBool() {
  const int32_t int32max = std::numeric_limits<int32_t>::max();
  const int32_t int32min = std::numeric_limits<int32_t>::min();
  const int64_t int64max = std::numeric_limits<int64_t>::max();
  const int64_t int64min = std::numeric_limits<int64_t>::min();
  const uint32_t uint32max = std::numeric_limits<uint32_t>::max();
  const uint64_t uint64max = std::numeric_limits<uint64_t>::max();
  return {
      {{True()}, True()},
      {{NullBool()}, NullBool()},
      {{Int32(0)}, False()},
      {{Int32(int32max)}, True()},
      {{Int32(int32min)}, True()},
      {{NullInt32()}, NullBool()},
      {{Int64(0)}, False()},
      {{Int64(int64max)}, True()},
      {{Int64(int64min)}, True()},
      {{NullInt64()}, NullBool()},
      {{Uint32(0)}, False()},
      {{Uint32(uint32max)}, True()},
      {{NullUint32()}, NullBool()},
      {{Int64(int32max)}, True()},
      {{Uint64(0)}, False()},
      {{Uint64(uint64max)}, True()},
      {{NullUint64()}, NullBool()},
      {{True()}, Int32(1)},
      {{True()}, Int64(1)},
      {{True()}, Uint32(1)},
      {{True()}, Uint64(1)},
      {{True()}, True()},
      {{False()}, Int32(0)},
      {{False()}, Int64(0)},
      {{False()}, Uint32(0)},
      {{False()}, Uint64(0)},
      {{False()}, False()},
      {{NullBool()}, NullInt32()},
      {{NullBool()}, NullInt64()},
      {{NullBool()}, NullUint32()},
      {{NullBool()}, NullUint64()},
      {{NullBool()}, NullBool()},
  };
}

// TODO: move the remaining monotonicity and boundary tests from
// functions/convert_test.cc. Dedup the tests in XXXPorted(), which were moved
// over from cast_test.cc.
std::vector<QueryParamsWithResult> GetFunctionTestsCastNumeric() {
  return ConcatTests<QueryParamsWithResult>({
      GetFunctionTestsCastInteger(),
      GetFunctionTestsCastFloat(),
      GetFunctionTestsCastDouble(),
      GetFunctionTestsCastNumericValue(),
      GetFunctionTestsCastIntPorted(),
      GetFunctionTestsCastUintPorted(),
      GetFunctionTestsCastFloatPorted(),
      GetFunctionTestsCastDoublePorted(),
      TestCastPositiveRounding<float, int32_t>(),
      TestCastPositiveRounding<float, int64_t>(),
      TestCastPositiveRounding<float, uint32_t>(),
      TestCastPositiveRounding<float, uint32_t>(),
      TestCastPositiveRounding<double, int32_t>(),
      TestCastPositiveRounding<double, int64_t>(),
      TestCastPositiveRounding<double, uint32_t>(),
      TestCastPositiveRounding<double, uint32_t>(),
      TestCastNegativeRounding<float, int32_t>(),
      TestCastNegativeRounding<float, int64_t>(),
      TestCastNegativeRounding<double, int32_t>(),
      TestCastNegativeRounding<double, int64_t>(),
      TestCastInfinityError<float, int32_t>(),
      TestCastInfinityError<float, int64_t>(),
      TestCastInfinityError<float, uint32_t>(),
      TestCastInfinityError<float, uint64_t>(),
      TestCastInfinityError<double, int32_t>(),
      TestCastInfinityError<double, int64_t>(),
      TestCastInfinityError<double, uint32_t>(),
      TestCastInfinityError<double, uint64_t>(),
      TestCastNumericNull<int32_t>(),
      TestCastNumericNull<int64_t>(),
      TestCastNumericNull<uint32_t>(),
      TestCastNumericNull<uint64_t>(),
      TestCastNumericNull<float>(),
      TestCastNumericNull<double>(),
  });
}

namespace {
#if defined(__cpp_lib_char8_t)
// Overload to use in C++20.
static std::string u8tostring(const std::u8string& s) {
  return std::string(s.begin(), s.end());
}
#endif
static std::string u8tostring(absl::string_view s) { return std::string(s); }
}  // namespace

std::vector<QueryParamsWithResult> GetFunctionTestsCastComplex() {
  const Value struct_value =
      Value::Struct(SimpleStructType(), {String("aaa"), Int32(777)});

  const Value enum_value = Value::Enum(TestEnumType(), 1);
  const Value array_value = Value::EmptyArray(Int32ArrayType());
  const Value array_value2 =
      Value::Array(Int64ArrayType(), {Int64(3), Int64(4)});
  const Value null_enum = Value::Null(TestEnumType());
  const Value null_proto = Value::Null(KitchenSinkProtoType());
  const Value null_struct = Value::Null(SimpleStructType());
  const Value null_array = Value::Null(Int32ArrayType());

  const Value another_null_enum = Value::Null(AnotherEnumType());
  const Value another_null_proto = Value::Null(NullableIntProtoType());
  const Value another_null_array = Value::Null(Int64ArrayType());

  LanguageOptions language_options_map_type;
  language_options_map_type.EnableLanguageFeature(FEATURE_MAP_TYPE);
  absl::StatusOr<const Type*> map_type_or_status = type_factory()->MakeMapType(
      StringType(), Int64Type(), language_options_map_type);
  GOOGLESQL_CHECK_OK(map_type_or_status);
  const Value null_map = Value::Null(map_type_or_status.value());

  const std::string kitchen_sink_string_1("int64_key_1: 1 int64_key_2: 2");
  const std::string kitchen_sink_string_2(
      "int64_key_1: 1 int64_key_2: 2 repeated_int32_val: 3 "
      "repeated_int32_val: 4");
  // String value with UTF characters.
  const std::string kitchen_sink_string_3(u8tostring(
      u8"int64_key_1: 2 int64_key_2: 3 string_val: \"\u2661\u2662\""));
  const std::string kitchen_sink_string_4(
      "int64_key_1: 2 int64_key_2: 3 string_val: \"\\u2661\\u2662\"");

  // Scratch message.
  googlesql_test::KitchenSinkPB kitchen_sink_message;

  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(kitchen_sink_string_1,
                                            &kitchen_sink_message));
  absl::Cord bytes_2186;
  ABSL_CHECK(kitchen_sink_message.SerializeToCord(&bytes_2186));
  absl::Cord kitchen_sink_cord_1 = bytes_2186;

  const google::protobuf::Reflection* reflection = kitchen_sink_message.GetReflection();
  google::protobuf::UnknownFieldSet* unknown_fields =
      reflection->MutableUnknownFields(&kitchen_sink_message);
  ABSL_CHECK(unknown_fields->empty());
  const int reserved_tag_number = 103;
  ABSL_CHECK(kitchen_sink_message.GetDescriptor()->IsReservedNumber(
      reserved_tag_number));
  unknown_fields->AddVarint(reserved_tag_number, /*value=*/1000);
  absl::Cord bytes_2196;
  ABSL_CHECK(kitchen_sink_message.SerializeToCord(&bytes_2196));
  absl::Cord kitchen_sink_cord_5 = bytes_2196;

  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(kitchen_sink_string_2,
                                            &kitchen_sink_message));
  absl::Cord bytes_2200;
  ABSL_CHECK(kitchen_sink_message.SerializeToCord(&bytes_2200));
  absl::Cord kitchen_sink_cord_2 = bytes_2200;
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(kitchen_sink_string_3,
                                            &kitchen_sink_message));
  absl::Cord bytes_2203;
  ABSL_CHECK(kitchen_sink_message.SerializeToCord(&bytes_2203));
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(kitchen_sink_string_4,
                                            &kitchen_sink_message));
  absl::Cord bytes_2206;
  ABSL_CHECK(kitchen_sink_message.SerializeToCord(&bytes_2206));

  googlesql_test::NullableInt nullable_int_message;
  const std::string nullable_int_string_1("value: 1");
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(nullable_int_string_1,
                                            &nullable_int_message));
  absl::Cord bytes_2212;
  ABSL_CHECK(nullable_int_message.SerializeToCord(&bytes_2212));
  absl::Cord nullable_int_cord_1 = bytes_2212;

  // Set up some equivalent but not equal enums and protos, both null and
  // non-null.
  ABSL_CHECK(!enum_value.type()->Equals(TestEnumType_equivalent()));
  ABSL_CHECK(enum_value.type()->Equivalent(TestEnumType_equivalent()));

  ABSL_CHECK(!null_proto.type()->Equals(KitchenSinkProtoType_equivalent()));
  ABSL_CHECK(null_proto.type()->Equivalent(KitchenSinkProtoType_equivalent()));

  ABSL_CHECK(!KitchenSink(kitchen_sink_string_1).type()->Equals(
          KitchenSink_equivalent(kitchen_sink_string_1).type()));
  ABSL_CHECK(KitchenSink(kitchen_sink_string_1).type()->Equivalent(
          KitchenSink_equivalent(kitchen_sink_string_1).type()));

  const Value enum_value_equivalent = Value::Enum(TestEnumType_equivalent(), 1);
  const Value null_enum_equivalent = Value::Null(TestEnumType_equivalent());

  const Value null_proto_equivalent =
      Value::Null(KitchenSinkProtoType_equivalent());

  // Set up a couple of arrays that have equivalent but not equal ENUM elements.
  const Value enum_array_value = Value::Array(TestEnumArrayType(),
                                              {enum_value, null_enum});
  const Value enum_array_value_equivalent =
      Value::Array(TestEnumArrayType_equivalent(),
                   {enum_value_equivalent, null_enum_equivalent});

  const Value null_enum_array_value = Value::Null(TestEnumArrayType());
  const Value null_enum_array_value_equivalent =
      Value::Null(TestEnumArrayType_equivalent());

  const google::protobuf::Descriptor* string_int32_descriptor =
      StringInt32MapEntryType()->descriptor();
  std::unique_ptr<google::protobuf::Message> string_int32_message(
      google::protobuf::MessageFactory::generated_factory()
          ->GetPrototype(string_int32_descriptor)
          ->New());
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString("key: 'aaa' value: 777",
                                            string_int32_message.get()));
  absl::Cord bytes_2253;
  ABSL_CHECK(string_int32_message->SerializeToCord(&bytes_2253));
  const Value string_int32_map_entry =
      Value::Proto(StringInt32MapEntryType(), bytes_2253);

  const StructType* string_string_struct;
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"", type_factory()->get_string()}, {"", type_factory()->get_string()}},
      &string_string_struct));
  // When casted, results in the same value as string_int32_map_entry.
  const Value string_string_struct_value = Value::Struct(
      string_string_struct, {Value::String("aaa"), Value::String("777")});

  TypeFactory factory;
  const Value nested_container_from_type = test_values::Map(
      {{"k", test_values::Array({test_values::Struct({"a"}, {Value::Float(1.5)},
                                                     type_factory())},
                                test_values::kPreservesOrder, type_factory())}},
      type_factory());
  const Value nested_container_to_type = test_values::Map(
      {{Value::Bytes("k"),
        test_values::Array(
            {test_values::Struct({"a"}, {Value::Int32(2)}, type_factory())},
            test_values::kPreservesOrder, type_factory())}},
      type_factory());

  std::set<LanguageFeature> with_proto_maps = {FEATURE_PROTO_MAPS};

  return {
      {{NullInt64()}, null_enum},
      {{NullInt32()}, null_enum},
      {{NullUint64()}, null_enum},
      {{NullUint32()}, null_enum},
      {{null_enum}, NullInt32()},
      {{null_enum}, NullInt64()},
      {{null_enum}, NullUint32()},
      {{null_enum}, NullUint64()},
      {{null_enum}, NullString()},
      {{null_enum}, null_enum_equivalent},
      {{null_enum_equivalent}, null_enum},

      {{struct_value}, struct_value},

      // Both num_fields and field types match.
      {{null_struct}, Null(SimpleStructType())},
      // num_fields() match and field types are compatible.
      {{null_struct}, Null(AnotherStructType3())},
      // Struct where one of the field is NULL and field type is compatible.
      {{Value::Struct(SimpleStructType(),
                      {Value::String("a"), Value::NullInt32()})},
       Value::Struct(AnotherStructType3(),
                     {Value::String("a"), Value::Null(TestEnumType())})},

      {{enum_value}, enum_value},
      {{enum_value}, enum_value_equivalent},
      {{enum_value_equivalent}, enum_value},
      {{enum_value}, Int32(1)},
      {{enum_value}, Int64(1)},
      {{enum_value}, Uint32(1)},
      {{enum_value}, Uint64(1)},
      {{Int32(1)}, enum_value},
      {{Int32(12345)}, null_enum, OUT_OF_RANGE},
      {{Int64(1)}, enum_value},
      {{Int64(12345)}, null_enum, OUT_OF_RANGE},
      {{Int64(1234567890123)}, null_enum, OUT_OF_RANGE},
      {{Uint32(1)}, enum_value},
      {{Uint32(12345)}, null_enum, OUT_OF_RANGE},
      {{Uint64(1)}, enum_value},
      {{Uint64(12345)}, null_enum, OUT_OF_RANGE},
      {{Uint64(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1)},
       null_enum,
       OUT_OF_RANGE},
      {{String("TESTENUM1")}, enum_value},
      {{String("INVALID_VALUE")}, null_enum, OUT_OF_RANGE},
      {{enum_value}, String("TESTENUM1")},

      {{array_value}, array_value},
      {{array_value2}, array_value2},

      {{null_array}, null_array},

      {{enum_array_value}, enum_array_value_equivalent},
      {{enum_array_value_equivalent}, enum_array_value},
      {{null_enum_array_value}, null_enum_array_value_equivalent},
      {{null_enum_array_value_equivalent}, null_enum_array_value},

      {{null_proto}, null_proto},
      {{null_proto}, null_proto_equivalent},
      {{null_proto_equivalent}, null_proto},
      {{null_proto}, NullString()},
      {{NullString()}, null_proto},

      QueryParamsWithResult({{null_map}, null_map})
          .AddRequiredFeature(FEATURE_MAP_TYPE),

      {{KitchenSink(kitchen_sink_string_1)}, String(kitchen_sink_string_1)},
      {{KitchenSink(kitchen_sink_string_2)}, String(kitchen_sink_string_2)},
      {{KitchenSink(kitchen_sink_string_3)}, String(kitchen_sink_string_3)},
      // Parsing the input string unescapes the UTF characters, so the
      // result will be the same as in the third message.
      {{KitchenSink(kitchen_sink_string_4)}, String(kitchen_sink_string_3)},
      // Casting a PROTO with unknown fields to STRING results in some extra
      // debug information, but the resulting string is not re-parseable.
      {{Proto(KitchenSinkProtoType(), kitchen_sink_cord_5)},
       String("int64_key_1: 1 int64_key_2: 2 103: 1000")},
      {{String("int64_key_1: 1 int64_key_2: 2 103: 1000")},
       null_proto,
       OUT_OF_RANGE},

      // Two unequal but equivalent protos allow casting.
      {{KitchenSink(kitchen_sink_string_1)},
       KitchenSink_equivalent(kitchen_sink_string_1)},
      {{KitchenSink_equivalent(kitchen_sink_string_1)},
       KitchenSink(kitchen_sink_string_1)},

      // Same basic string for conversion to proto, but differing
      // whitespaces, newlines, commas, field order, etc.
      {{String("int64_key_1: 1\nint64_key_2: 2\n")},
       KitchenSink(kitchen_sink_string_1)},
      {{String("\nint64_key_1: 1\n\n\nint64_key_2: 2\n\n\n")},
       KitchenSink(kitchen_sink_string_1)},
      {{String("int64_key_1: 1 int64_key_2: 2")},
       KitchenSink(kitchen_sink_string_1)},
      {{String("int64_key_1: 1, int64_key_2: 2")},
       KitchenSink(kitchen_sink_string_1)},
      {{String("  int64_key_2: 2,    int64_key_1: 1, ")},
       KitchenSink(kitchen_sink_string_1)},

      {{String("int64_key_1: 1 int64_key_2: 2 "
               "repeated_int32_val: 3 repeated_int32_val: 4")},
       KitchenSink(kitchen_sink_string_2)},

      {{String("int64_key_1: 2 int64_key_2: 3 "
               "string_val: '\\u2661\\u2662'")},
       KitchenSink(kitchen_sink_string_3)},

      {{String(nullable_int_string_1)}, NullableInt(nullable_int_string_1)},

      // This also works for NullableInt, as there are no required fields.
      {{String("")}, Proto(NullableIntProtoType(), absl::Cord(""))},

      // Invalid strings for conversion to proto - they do not match the
      // KitchenSinkProto descriptor.
      {{String("invalid string blahblahblah")}, null_proto, OUT_OF_RANGE},
      // Missing required field(s).
      {{String("")}, null_proto, OUT_OF_RANGE},
      {{String("int64_key_1: 1")}, null_proto, OUT_OF_RANGE},
      // Duplicate field.
      {{String("int64_key_1: 1 int64_key_1: 2 int64_key_2: 3")},
       null_proto,
       OUT_OF_RANGE},
      // Bad field name.
      {{String("int64_key_2: 1 int64_key_2: 2 bad_field_name: foo")},
       null_proto,
       OUT_OF_RANGE},
      // After the comma, expected a field name.
      {{String("int64_key_1: 1 int64_key_2: 2 repeated_int32_val: 3, 4")},
       null_proto,
       OUT_OF_RANGE},
      {{Proto(KitchenSinkProtoType(), absl::Cord("invalid cord"))},
       NullString(),
       OUT_OF_RANGE},

      {{Proto(KitchenSinkProtoType(),
              absl::Cord("really really really really really really really "
                         "really really really long invalid cord"))},
       NullString(),
       OUT_OF_RANGE},
      // The error message produced clips the string in the middle of the
      // hex-bytes part.  Note that the sequence of bytes is converted to an
      // escaped Bytes literal before clipping, so there aren't really any
      // odd conditions to consider.
      {{Proto(KitchenSinkProtoType(),
              absl::Cord("reallyXreallyXreallyXreallyXreallyXrellyX"
                         "Google'\xe8\xb0\xb7\xe6\xad\x8c'Google"))},
       NullString(),
       OUT_OF_RANGE},

      {{null_proto}, NullBytes()},
      {{NullBytes()}, null_proto},
      {{Bytes(kitchen_sink_cord_1)},
       Proto(KitchenSinkProtoType(), kitchen_sink_cord_1)},
      {{Proto(KitchenSinkProtoType(), kitchen_sink_cord_1)},
       Bytes(kitchen_sink_cord_1)},
      {{Bytes(kitchen_sink_cord_2)},
       Proto(KitchenSinkProtoType(), kitchen_sink_cord_2)},
      {{Proto(KitchenSinkProtoType(), kitchen_sink_cord_2)},
       Bytes(kitchen_sink_cord_2)},
      {{Bytes(kitchen_sink_cord_5)},
       Proto(KitchenSinkProtoType(), kitchen_sink_cord_5)},
      {{Proto(KitchenSinkProtoType(), kitchen_sink_cord_5)},
       Bytes(kitchen_sink_cord_5)},

      {{Bytes(nullable_int_cord_1)},
       Proto(NullableIntProtoType(), nullable_int_cord_1)},
      {{Proto(NullableIntProtoType(), nullable_int_cord_1)},
       Bytes(nullable_int_cord_1)},
      {{Proto(NullableIntProtoType(), nullable_int_cord_1)}, Bytes("\x08\x01")},

      // NullableInt has no required fields, so this should work.
      {{Bytes("")}, Proto(NullableIntProtoType(), absl::Cord(""))},
      {{Proto(NullableIntProtoType(), absl::Cord(""))}, Bytes("")},

      QueryParamsWithResult({struct_value}, string_int32_map_entry)
          .WrapWithFeatureSet(with_proto_maps),
      QueryParamsWithResult({string_string_struct_value},
                            string_int32_map_entry)
          .WrapWithFeatureSet(with_proto_maps),

      // Struct with the wrong field types won't cast.
      QueryParamsWithResult(
          {struct_value}, Value::Null(Uint64StringMapEntryType()), OUT_OF_RANGE)
          .WrapWithFeatureSet(with_proto_maps),

      QueryParamsWithResult({nested_container_from_type},
                            nested_container_to_type)
          .AddRequiredFeatures(
              {FEATURE_MAP_TYPE, FEATURE_CAST_DIFFERENT_ARRAY_TYPES}),
  };
}

std::vector<QueryParamsWithResult> GetFunctionTestsCast() {
  return ConcatTests<QueryParamsWithResult>({
      GetFunctionTestsCastBool(),
      GetFunctionTestsCastComplex(),
      GetFunctionTestsCastDateTime(),
      GetFunctionTestsCastInterval(),
      GetFunctionTestsCastNumeric(),
      GetFunctionTestsCastString(),
      GetFunctionTestsCastNumericString(),
      GetFunctionTestsCastBytesStringWithFormat(),
      GetFunctionTestsCastDateTimestampStringWithFormat(),
      GetFunctionTestsCastRange(),
      GetFunctionTestsCastTokenList(),
      GetFunctionTestsCastUuid(),
  });
}

std::vector<QueryParamsWithResult> GetFunctionTestsSafeCast() {
  std::vector<QueryParamsWithResult> tests;
  for (QueryParamsWithResult& test : GetFunctionTestsCast()) {
    ABSL_CHECK_GE(test.params().size(), 1) << test;
    ABSL_CHECK_LE(test.params().size(), 3) << test;
    test.MutateResult([](QueryParamsWithResult::Result& result_struct) {
      if (result_struct.status.code() == OUT_OF_RANGE) {
        result_struct.status = absl::OkStatus();
        result_struct.result = Value::Null(result_struct.result.type());
      }
    });
    tests.push_back(test);
  }
  return tests;
}

// Constructs tests casting between different array types from the scalar cast
// tests by wrapping the input/output to arrays.
// <arrays_with_nulls> indicates if tests are generated where the casted
// arrays includes nulls, or excludes nulls.  If true, then all test case arrays
// will include at least one null element.  If false, then no test case arrays
// will include a null element.
std::vector<QueryParamsWithResult>
GetFunctionTestsCastBetweenDifferentArrayTypes(bool arrays_with_nulls) {
  std::vector<QueryParamsWithResult> tests;
  for (const QueryParamsWithResult& test : GetFunctionTestsCast()) {
    // Currently FORMAT parameter is not allowed for array casting
    if (test.params().size() > 1) {
      continue;
    }
    ABSL_CHECK_EQ(1, test.params().size()) << test;

    const Value& cast_value = test.param(0);
    const Value& result_value = test.result();

    if (!cast_value.type()->IsArray() && !result_value.type()->IsArray() &&
        !cast_value.type()->Equivalent(result_value.type()) &&
        arrays_with_nulls == cast_value.is_null()) {
      const ArrayType* from_array_type;
      const ArrayType* to_array_type;
      GOOGLESQL_CHECK_OK(
          type_factory()->MakeArrayType(cast_value.type(), &from_array_type));
      GOOGLESQL_CHECK_OK(
          type_factory()->MakeArrayType(result_value.type(), &to_array_type));
      Value from_array = Value::Array(from_array_type, {cast_value});
      Value to_array = Value::Array(to_array_type, {result_value});

      tests.push_back(
          QueryParamsWithResult({from_array}, to_array, test.status())
              .AddRequiredFeatures(
                  googlesql_base::STLSetUnion(test.required_features(),
                                   {FEATURE_CAST_DIFFERENT_ARRAY_TYPES})));
    }
  }

  // More tests can be added here.
  if (!arrays_with_nulls) {
    const Value null_array = Value::Null(Int32ArrayType());
    const Value another_null_array = Value::Null(Int64ArrayType());

    tests.push_back(
        QueryParamsWithResult({null_array}, another_null_array)
            .AddRequiredFeature(FEATURE_CAST_DIFFERENT_ARRAY_TYPES));
  }

  return tests;
}

std::vector<QueryParamsWithResult> GetFunctionTestsCastTokenList() {
  const Value tokenlist = TokenListFromStringArray({"test", "tokenlist"});
  const std::string bytes = tokenlist.tokenlist_value().GetBytes();

  std::vector<QueryParamsWithResult> tests = {
      {{Value::NullTokenList()}, Value::NullTokenList()},
      {{NullBytes()}, Value::NullTokenList()},
      {{Value::NullTokenList()}, NullBytes()},
      {{Bytes(bytes)}, tokenlist},
      {{tokenlist}, Bytes(bytes)},
  };

  std::vector<QueryParamsWithResult> result;
  result.reserve(tests.size());
  for (const auto& test : tests) {
    result.push_back(test.WrapWithFeature(FEATURE_TOKENIZED_SEARCH));
  }
  return result;
}

// Helper functions for MAP cast tests.
namespace {

enum class MapKeyOrValueSelector {
  kMapKey,
  kMapValue,
};

absl::StatusOr<QueryParamsWithResult> WrapCastTestInputAndResultInMapCastTest(
    MapKeyOrValueSelector selector, const Value& cast_value,
    const Value& result_value, const absl::Status& expected_status,
    std::set<LanguageFeature> required_features) {
  Value map_entry_cast_key;
  Value map_entry_cast_value;
  Value map_entry_result_key;
  Value map_entry_result_value;
  switch (selector) {
    case MapKeyOrValueSelector::kMapKey: {
      map_entry_cast_key = cast_value;
      map_entry_cast_value = Value::Bool(true);
      map_entry_result_key = result_value;
      map_entry_result_value = Value::Bool(true);
      break;
    }
    case MapKeyOrValueSelector::kMapValue: {
      map_entry_cast_key = Value::Bool(true);
      map_entry_cast_value = cast_value;
      map_entry_result_key = Value::Bool(true);
      map_entry_result_value = result_value;
      break;
    }
    default:
      ABSL_LOG(FATAL) << "Unsupported MapKeyOrValueSelector";
  }

  required_features.emplace(FEATURE_MAP_TYPE);

  // Basic support for cases with containers as keys.
  if (map_entry_cast_key.type()->IsArray()) {
    required_features.emplace(FEATURE_GROUP_BY_ARRAY);
  } else if (map_entry_cast_key.type()->IsStruct()) {
    required_features.emplace(FEATURE_GROUP_BY_STRUCT);
  }

  LanguageOptions language_options;
  language_options.SetEnabledLanguageFeatures(required_features);

  GOOGLESQL_ASSIGN_OR_RETURN(const Type* cast_map_type,
                   type_factory()->MakeMapType(map_entry_cast_key.type(),
                                               map_entry_cast_value.type(),
                                               language_options));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* result_map_type,
                   type_factory()->MakeMapType(map_entry_result_key.type(),
                                               map_entry_result_value.type(),
                                               language_options));

  GOOGLESQL_ASSIGN_OR_RETURN(Value cast_map,
                   Value::MakeMap(cast_map_type, {{map_entry_cast_key,
                                                   map_entry_cast_value}}));
  GOOGLESQL_ASSIGN_OR_RETURN(Value result_map,
                   Value::MakeMap(result_map_type, {{map_entry_result_key,
                                                     map_entry_result_value}}));

  GOOGLESQL_RET_CHECK(cast_map.is_valid());
  GOOGLESQL_RET_CHECK(result_map.is_valid());
  GOOGLESQL_RET_CHECK(cast_map.map_entries().begin()->first.is_valid());
  GOOGLESQL_RET_CHECK(cast_map.map_entries().begin()->second.is_valid());

  QueryParamsWithResult map_cast_test_case = {
      {cast_map}, result_map, expected_status};

  map_cast_test_case.AddRequiredFeatures(required_features);
  return map_cast_test_case;
}

// Additional test cases for MAP to address gaps in the standard cast test
// cases.
static inline absl::Status AddAdditionalMapCastTestCases(
    std::vector<QueryParamsWithResult>& map_cast_test_cases) {
  // ARRAY element tests.
  GOOGLESQL_ASSIGN_OR_RETURN(
      QueryParamsWithResult map_array_key,
      WrapCastTestInputAndResultInMapCastTest(
          MapKeyOrValueSelector::kMapKey,
          test_values::Array({"1", "2", "3"}, type_factory()),
          test_values::Array({1, 2, 3}, type_factory()), absl::OkStatus(),
          {FEATURE_GROUP_BY_ARRAY, FEATURE_CAST_DIFFERENT_ARRAY_TYPES}));
  map_cast_test_cases.push_back(map_array_key);

  GOOGLESQL_ASSIGN_OR_RETURN(QueryParamsWithResult map_array_value,
                   WrapCastTestInputAndResultInMapCastTest(
                       MapKeyOrValueSelector::kMapValue,
                       test_values::Array({"1", "2", "3"}, type_factory()),
                       test_values::Array({1, 2, 3}, type_factory()),
                       absl::OkStatus(), {FEATURE_CAST_DIFFERENT_ARRAY_TYPES}));
  map_cast_test_cases.push_back(map_array_value);

  // MAP element test.
  GOOGLESQL_ASSIGN_OR_RETURN(
      QueryParamsWithResult map_map_value,
      WrapCastTestInputAndResultInMapCastTest(
          MapKeyOrValueSelector::kMapValue,
          test_values::Map({{"1", "1.5"}}, type_factory()),
          test_values::Map({{1, 1.5}}, type_factory()), absl::OkStatus(),
          /*required_features=*/{}));
  map_cast_test_cases.push_back(map_map_value);

  // STRUCT element tests.
  GOOGLESQL_ASSIGN_OR_RETURN(
      QueryParamsWithResult map_struct_key,
      WrapCastTestInputAndResultInMapCastTest(
          MapKeyOrValueSelector::kMapKey,
          test_values::Struct({"a", "b"}, {1, 2.5}, type_factory()),
          test_values::Struct({"a", "b"}, {"1", "2.5"}, type_factory()),
          absl::OkStatus(),
          /*required_features=*/{FEATURE_GROUP_BY_STRUCT}));
  map_cast_test_cases.push_back(map_struct_key);
  GOOGLESQL_ASSIGN_OR_RETURN(
      QueryParamsWithResult map_struct_value,
      WrapCastTestInputAndResultInMapCastTest(
          MapKeyOrValueSelector::kMapValue,
          test_values::Struct({"a", "b"}, {1, 2.5}, type_factory()),
          test_values::Struct({"a", "b"}, {"1", "2.5"}, type_factory()),
          absl::OkStatus(),
          /*required_features=*/{}));
  map_cast_test_cases.push_back(map_struct_value);

  return absl::OkStatus();
}

}  // namespace

// Builds test cases for MAP type based on the standard cast test cases plus
// some additional test cases. Each standard test case will be reproduced twice:
// once placing the case in the MAP key and once in the MAP value. Invalid
// cases, such as when the MAP key type is not groupable, will be filtered out.
std::vector<QueryParamsWithResult> GetFunctionTestsMapCast() {
  auto cast_test_cases = GetFunctionTestsCast();
  std::vector<QueryParamsWithResult> map_cast_test_cases;
  map_cast_test_cases.reserve(cast_test_cases.size() * 2);

  int cast_test_case_as_key_failure_count = 0;
  int cast_test_case_as_value_failures_count = 0;

  // Add standard cast test cases wrapped in a MAP type.
  for (const QueryParamsWithResult& test : cast_test_cases) {
    // Currently FORMAT parameter is not allowed for MAP casting.
    if (test.params().size() > 1) {
      continue;
    }
    ABSL_CHECK_EQ(1, test.params().size()) << test;

    const Value& cast_value = test.param(0);
    const Value& result_value = test.result();
    if (!cast_value.type()->Equivalent(result_value.type())) {
      const std::set<LanguageFeature>& required_features =
          test.required_features();

      absl::StatusOr<QueryParamsWithResult>
          cast_and_result_maps_varying_key_or_status =
              WrapCastTestInputAndResultInMapCastTest(
                  MapKeyOrValueSelector::kMapKey, cast_value, result_value,
                  test.status(), required_features);
      absl::StatusOr<QueryParamsWithResult>
          cast_and_result_maps_varying_value_or_status =
              WrapCastTestInputAndResultInMapCastTest(
                  MapKeyOrValueSelector::kMapValue, cast_value, result_value,
                  test.status(), required_features);

      if (cast_and_result_maps_varying_key_or_status.ok()) {
        map_cast_test_cases.push_back(
            cast_and_result_maps_varying_key_or_status.value());
      } else {
        ++cast_test_case_as_key_failure_count;
      }
      if (cast_and_result_maps_varying_value_or_status.ok()) {
        map_cast_test_cases.push_back(
            cast_and_result_maps_varying_value_or_status.value());
      } else {
        ++cast_test_case_as_value_failures_count;
      }
    }
  }

  ABSL_DCHECK_GE(map_cast_test_cases.size(), 1);

  ABSL_LOG(INFO) << "GetFunctionTestsMapCast(): " << map_cast_test_cases.size()
            << " cases generated from GetFunctionTestsCast(). Removed "
            << cast_test_case_as_key_failure_count
            << " cases invalid as MAP keys and "
            << cast_test_case_as_value_failures_count
            << " cases invalid as MAP values.";

  // Additional test cases here. The cast test cases being used above don't
  // contain array tests, so we add some add additional tests for ARRAYs and
  // other container types explicitly.
  GOOGLESQL_CHECK_OK(AddAdditionalMapCastTestCases(map_cast_test_cases));

  for (auto& test_case : map_cast_test_cases) {
    test_case.AddRequiredFeature(FEATURE_MAP_TYPE);
  }

  return map_cast_test_cases;
}

}  // namespace googlesql
