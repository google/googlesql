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

// The file functions_testlib.cc has been split into multiple files prefixed
// with "functions_testlib_" because an optimized compile with ASAN of the
// original single file timed out at 900 seconds.
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsBitCast() {
  return {
      // Null -> Null
      {"bit_cast_to_int32", {NullInt32()}, NullInt32()},
      {"bit_cast_to_int32", {NullUint32()}, NullInt32()},
      {"bit_cast_to_int64", {NullInt64()}, NullInt64()},
      {"bit_cast_to_int64", {NullUint64()}, NullInt64()},
      {"bit_cast_to_uint32", {NullUint32()}, NullUint32()},
      {"bit_cast_to_uint32", {NullInt32()}, NullUint32()},
      {"bit_cast_to_uint64", {NullUint64()}, NullUint64()},
      {"bit_cast_to_uint64", {NullInt64()}, NullUint64()},

      // INT32 -> INT32
      {"bit_cast_to_int32", {Int32(0)}, Int32(0)},
      {"bit_cast_to_int32", {Int32(int32max)}, Int32(int32max)},
      {"bit_cast_to_int32", {Int32(int32min)}, Int32(int32min)},
      {"bit_cast_to_int32", {Int32(3)}, Int32(3)},
      {"bit_cast_to_int32", {Int32(-3)}, Int32(-3)},

      // UINT32 -> INT32
      {"bit_cast_to_int32", {Uint32(0)}, Int32(0)},
      {"bit_cast_to_int32", {Uint32(uint32max)}, Int32(-1)},
      {"bit_cast_to_int32", {Uint32(3)}, Int32(3)},
      {"bit_cast_to_int32", {Uint32(uint32max - 3)}, Int32(-4)},
      {"bit_cast_to_int32", {Uint32(uint32max >> 1)}, Int32(int32max)},

      // INT64 -> INT64
      {"bit_cast_to_int64", {Int64(0)}, Int64(0)},
      {"bit_cast_to_int64", {Int64(int64max)}, Int64(int64max)},
      {"bit_cast_to_int64", {Int64(int64min)}, Int64(int64min)},
      {"bit_cast_to_int64", {Int64(3)}, Int64(3)},
      {"bit_cast_to_int64", {Int64(-3)}, Int64(-3)},

      // UINT64 -> INT64
      {"bit_cast_to_int64", {Uint64(0)}, Int64(0)},
      {"bit_cast_to_int64", {Uint64(uint64max)}, Int64(-1)},
      {"bit_cast_to_int64", {Uint64(3)}, Int64(3)},
      {"bit_cast_to_int64", {Uint64(uint64max - 3)}, Int64(-4)},
      {"bit_cast_to_int64", {Uint64(uint64max >> 1)}, Int64(int64max)},

      // UINT32 -> UINT32
      {"bit_cast_to_uint32", {Uint32(0)}, Uint32(0)},
      {"bit_cast_to_uint32", {Uint32(uint32max)}, Uint32(uint32max)},
      {"bit_cast_to_uint32", {Uint32(3)}, Uint32(3)},

      // INT32 -> UINT32
      {"bit_cast_to_uint32", {Int32(0)}, Uint32(0)},
      {"bit_cast_to_uint32", {Int32(int32max)}, Uint32(int32max)},
      {"bit_cast_to_uint32", {Int32(3)}, Uint32(3)},
      {"bit_cast_to_uint32", {Int32(-3)}, Uint32(-3)},
      {"bit_cast_to_uint32", {Int32(int32min)}, Uint32(int32min)},
      {"bit_cast_to_uint32", {Int32(int32min + 3)}, Uint32(2147483651)},

      // UINT64 -> UINT64
      {"bit_cast_to_uint64", {Uint64(0)}, Uint64(0)},
      {"bit_cast_to_uint64", {Uint64(uint64max)}, Uint64(uint64max)},
      {"bit_cast_to_uint64", {Uint64(3)}, Uint64(3)},

      // INT64 -> UINT64
      {"bit_cast_to_uint64", {Int64(0)}, Uint64(0)},
      {"bit_cast_to_uint64", {Int64(int64max)}, Uint64(int64max)},
      {"bit_cast_to_uint64", {Int64(3)}, Uint64(3)},
      {"bit_cast_to_uint64", {Int64(-3)}, Uint64(-3)},
      {"bit_cast_to_uint64", {Int64(int64min)}, Uint64(int64min)},
      {"bit_cast_to_uint64",
       {Int64(int64min + 3)},
       Uint64(uint64_t{9223372036854775811u})},
  };
}

namespace {

Value DateArray(absl::Span<const int32_t> values) {
  std::vector<Value> date_values;
  date_values.reserve(values.size());
  for (int32_t value : values) {
    date_values.push_back(Date(value));
  }
  return Value::Array(DateArrayType(), date_values);
}

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsRangeBucket() {
  const Value numeric_zero = Value::Numeric(NumericValue());
  const Value numeric_one =
      Value::Numeric(NumericValue(static_cast<int64_t>(1)));
  const Value numeric_max = Value::Numeric(NumericValue::MaxValue());
  const Value numeric_min = Value::Numeric(NumericValue::MinValue());

  const Value bignumeric_zero = Value::BigNumeric(BigNumericValue());
  const Value bignumeric_one = Value::BigNumeric(BigNumericValue(1));
  const Value bignumeric_just_over_one = Value::BigNumeric(
      bignumeric_one.bignumeric_value()
          .Add(BigNumericValue::FromStringStrict("1e-38").value())
          .value());
  const Value bignumeric_max = Value::BigNumeric(BigNumericValue::MaxValue());
  const Value bignumeric_min = Value::BigNumeric(BigNumericValue::MinValue());

  std::vector<FunctionTestCall> all_tests = {
      // Null inputs.
      {"range_bucket", {NullInt64(), Int64Array({1})}, NullInt64()},
      {"range_bucket", {Int64(1), Null(Int64ArrayType())}, NullInt64()},
      {"range_bucket", {NullInt64(), Null(Int64ArrayType())}, NullInt64()},
      {"range_bucket",
       {Int64(5), values::Array(Int64ArrayType(), {NullInt64()})},
       NullInt64(),
       OUT_OF_RANGE},
      // Empty array.
      {"range_bucket", {Int64(1), Int64Array({})}, Int64(0)},
      // Non-empty array.
      {"range_bucket", {Int64(-1), Int64Array({0})}, Int64(0)},
      {"range_bucket", {Int64(1), Int64Array({1})}, Int64(1)},
      {"range_bucket", {Int64(1), Int64Array({1, 1, 2})}, Int64(2)},
      {"range_bucket", {Int64(1), Int64Array({1, 1, 1})}, Int64(3)},
      {"range_bucket", {Int64(25), Int64Array({10, 20, 30})}, Int64(2)},
      {"range_bucket", {String("b"), StringArray({"a", "c"})}, Int64(1)},
      // Bool.
      {"range_bucket", {Bool(false), BoolArray({false, true})}, Int64(1)},
      {"range_bucket", {Bool(true), BoolArray({false, true})}, Int64(2)},
      // Bytes.
      {"range_bucket", {Bytes("b"), BytesArray({"a", "c"})}, Int64(1)},
      // Date.
      {"range_bucket",
       {Date(date_min), DateArray({date_min, 1, date_max})},
       Int64(1)},
      {"range_bucket", {Date(2), DateArray({date_min, 1, date_max})}, Int64(2)},
      {"range_bucket",
       {Date(date_max), DateArray({date_min, 1, date_max})},
       Int64(3)},
      // Double.
      {"range_bucket",
       {Double(double_neg_inf), DoubleArray({double_neg_inf, doublemin, 10,
                                             doublemax, double_pos_inf})},
       Int64(1)},
      {"range_bucket",
       {Double(doublemin), DoubleArray({double_neg_inf, doublemin, 10,
                                        doublemax, double_pos_inf})},
       Int64(2)},
      {"range_bucket",
       {Double(20), DoubleArray({double_neg_inf, doublemin, 10, doublemax,
                                 double_pos_inf})},
       Int64(3)},
      {"range_bucket",
       {Double(doublemax), DoubleArray({double_neg_inf, doublemin, 10,
                                        doublemax, double_pos_inf})},
       Int64(4)},
      {"range_bucket",
       {Double(double_pos_inf), DoubleArray({double_neg_inf, doublemin, 10,
                                             doublemax, double_pos_inf})},
       Int64(5)},
      {"range_bucket",
       {Double(double_nan), DoubleArray({10, 20, 30})},
       NullInt64()},
      {"range_bucket",
       {Double(1), DoubleArray({double_nan, 20, 30})},
       NullInt64(),
       OUT_OF_RANGE},
      // Float.
      {"range_bucket",
       {Float(float_neg_inf),
        FloatArray({float_neg_inf, floatmin, 10, floatmax, float_pos_inf})},
       Int64(1)},
      {"range_bucket",
       {Float(floatmin),
        FloatArray({float_neg_inf, floatmin, 10, floatmax, float_pos_inf})},
       Int64(2)},
      {"range_bucket",
       {Float(20),
        FloatArray({float_neg_inf, floatmin, 10, floatmax, float_pos_inf})},
       Int64(3)},
      {"range_bucket",
       {Float(floatmax),
        FloatArray({float_neg_inf, floatmin, 10, floatmax, float_pos_inf})},
       Int64(4)},
      {"range_bucket",
       {Float(float_pos_inf),
        FloatArray({float_neg_inf, floatmin, 10, floatmax, float_pos_inf})},
       Int64(5)},
      {"range_bucket",
       {Float(float_nan), FloatArray({10, 20, 30})},
       NullInt64()},
      {"range_bucket",
       {Float(1), FloatArray({float_nan, 20, 30})},
       NullInt64(),
       OUT_OF_RANGE},
      // Int32.
      {"range_bucket",
       {Int32(int32min), Int32Array({int32min, 10, int32max})},
       Int64(1)},
      {"range_bucket",
       {Int32(20), Int32Array({int32min, 10, int32max})},
       Int64(2)},
      {"range_bucket",
       {Int32(int32max), Int32Array({int32min, 10, int32max})},
       Int64(3)},
      // Int64.
      {"range_bucket",
       {Int64(int64min), Int64Array({int64min, 10, int64max})},
       Int64(1)},
      {"range_bucket",
       {Int64(20), Int64Array({int64min, 10, int64max})},
       Int64(2)},
      {"range_bucket",
       {Int64(int64max), Int64Array({int64min, 10, int64max})},
       Int64(3)},
      // Uint32.
      {"range_bucket", {Uint32(0), Uint32Array({0, 10, uint32max})}, Int64(1)},
      {"range_bucket", {Uint32(20), Uint32Array({0, 10, uint32max})}, Int64(2)},
      {"range_bucket",
       {Uint32(uint32max), Uint32Array({0, 10, uint32max})},
       Int64(3)},
      // Uint64.
      {"range_bucket", {Uint64(0), Uint64Array({0, 10, uint64max})}, Int64(1)},
      {"range_bucket", {Uint64(20), Uint64Array({0, 10, uint64max})}, Int64(2)},
      {"range_bucket",
       {Uint64(uint64max), Uint64Array({0, 10, uint64max})},
       Int64(3)},
      // Numeric.
      {"range_bucket",
       QueryParamsWithResult(
           {numeric_min,
            values::Array(NumericArrayType(),
                          {numeric_min, numeric_zero, numeric_max})},
           Int64(1))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"range_bucket",
       QueryParamsWithResult(
           {numeric_one,
            values::Array(NumericArrayType(),
                          {numeric_min, numeric_zero, numeric_max})},
           Int64(2))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      {"range_bucket",
       QueryParamsWithResult(
           {numeric_max,
            values::Array(NumericArrayType(),
                          {numeric_min, numeric_zero, numeric_max})},
           Int64(3))
           .WrapWithFeature(FEATURE_NUMERIC_TYPE)},
      // BigNumeric.
      {"range_bucket",
       QueryParamsWithResult(
           {bignumeric_min,
            values::Array(BigNumericArrayType(),
                          {bignumeric_min, bignumeric_zero, bignumeric_max})},
           Int64(1))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"range_bucket",
       QueryParamsWithResult(
           {bignumeric_one,
            values::Array(BigNumericArrayType(),
                          {bignumeric_min, bignumeric_zero, bignumeric_max})},
           Int64(2))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"range_bucket",
       QueryParamsWithResult(
           {bignumeric_max,
            values::Array(BigNumericArrayType(),
                          {bignumeric_min, bignumeric_zero, bignumeric_max})},
           Int64(3))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      {"range_bucket",
       QueryParamsWithResult(
           {bignumeric_one,
            values::Array(
                BigNumericArrayType(),
                {bignumeric_zero, bignumeric_just_over_one, bignumeric_max})},
           Int64(1))
           .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)},
      // Timestamp.
      {"range_bucket",
       {Timestamp(timestamp_min),
        values::Array(types::TimestampArrayType(),
                      {Timestamp(timestamp_min), Timestamp(10),
                       Timestamp(timestamp_max)})},
       Int64(1)},
      {"range_bucket",
       {Timestamp(20), values::Array(types::TimestampArrayType(),
                                     {Timestamp(timestamp_min), Timestamp(10),
                                      Timestamp(timestamp_max)})},
       Int64(2)},
      {"range_bucket",
       {Timestamp(timestamp_max),
        values::Array(types::TimestampArrayType(),
                      {Timestamp(timestamp_min), Timestamp(10),
                       Timestamp(timestamp_max)})},
       Int64(3)},
      // Error cases.
      {"range_bucket",
       {Double(15), DoubleArray({10, 20, 30, 25})},
       NullInt64(),
       OUT_OF_RANGE},  // Non-monotically increasing array.
  };

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsGenerateDateArray() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  const std::vector<FunctionTestCall> all_tests = {
      {"generate_date_array",
       {NullDate(), NullDate(), NullInt64(), Enum(part_enum, "DAY")},
       Null(DateArrayType())},
      // Empty generate_date_array.
      {"generate_date_array",
       {Date(10), Date(5), Int64(1), Enum(part_enum, "DAY")},
       DateArray({})},
      {"generate_date_array",
       {Date(90), Date(10), Int64(1), Enum(part_enum, "MONTH")},
       DateArray({})},
      // Non-empty generate_date_array.
      {"generate_date_array",
       {Date(10), Date(15), Int64(1), Enum(part_enum, "DAY")},
       DateArray({10, 11, 12, 13, 14, 15})},
      {"generate_date_array",
       {Date(10), Date(1000), Int64(100), Enum(part_enum, "DAY")},
       DateArray({10, 110, 210, 310, 410, 510, 610, 710, 810, 910})},
      {"generate_date_array",
       {Date(date_min), Date(date_max), Int64(date_max - date_min),
        Enum(part_enum, "DAY")},
       DateArray({date_min, date_max})},
      {"generate_date_array",
       {Date(10), Date(60), Int64(2), Enum(part_enum, "WEEK")},
       DateArray({10, 24, 38, 52})},
      {"generate_date_array",
       {Date(75), Date(30), Int64(-1), Enum(part_enum, "MONTH")},
       DateArray({75, 47})},
      // Guarding against overflows.
      {"generate_date_array",
       {Date(date_max - 3), Date(date_max), Int64(2), Enum(part_enum, "DAY")},
       DateArray({date_max - 3, date_max - 1})},
      {"generate_date_array",
       {Date(date_min), Date(date_min), Int64(-1), Enum(part_enum, "DAY")},
       DateArray({date_min})},
      {"generate_date_array",
       {Date(date_min), Date(date_max), int64max, Enum(part_enum, "DAY")},
       DateArray({date_min})},
      {"generate_date_array",
       {Date(date_max), Date(date_min), int64min, Enum(part_enum, "DAY")},
       DateArray({date_max})},
  };

  return all_tests;
}

namespace {

Value TimestampArray(
    absl::Span<const std::string> timestamp_strings,
    functions::TimestampScale scale = functions::kMicroseconds) {
  std::vector<Value> values;
  values.reserve(timestamp_strings.size());
  for (const std::string& timestamp_string : timestamp_strings) {
    values.push_back(TimestampFromStr(timestamp_string, scale));
  }
  return Value::Array(TimestampArrayType(), values);
}

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsGenerateTimestampArray() {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  const std::vector<FunctionTestCall> all_tests = {
      {"generate_timestamp_array",
       {NullTimestamp(), NullTimestamp(), NullInt64(), Enum(part_enum, "DAY")},
       Null(TimestampArrayType())},
      {"generate_timestamp_array",
       {NullTimestamp(), TimestampFromStr("2017-01-02"), Int64(1),
        Enum(part_enum, "DAY")},
       Null(TimestampArrayType())},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-02"), NullTimestamp(), Int64(1),
        Enum(part_enum, "DAY")},
       Null(TimestampArrayType())},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-02"), TimestampFromStr("2017-01-02"),
        NullInt64(), Enum(part_enum, "DAY")},
       Null(TimestampArrayType())},
      // Empty generate_timestamp_array.
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-02"), TimestampFromStr("2017-01-01"),
        Int64(1), Enum(part_enum, "DAY")},
       TimestampArray({})},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-02"), TimestampFromStr("2016-12-31"),
        Int64(1), Enum(part_enum, "MICROSECOND")},
       TimestampArray({})},
      // Non-empty generate_timestamp_array.
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01"), TimestampFromStr("2017-01-04"),
        Int64(1), Enum(part_enum, "DAY")},
       TimestampArray(
           {"2017-01-01", "2017-01-02", "2017-01-03", "2017-01-04"})},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01"), TimestampFromStr("2020-01-01"),
        Int64(100), Enum(part_enum, "DAY")},
       TimestampArray({"2017-01-01", "2017-04-11", "2017-07-20", "2017-10-28",
                       "2018-02-05", "2018-05-16", "2018-08-24", "2018-12-02",
                       "2019-03-12", "2019-06-20", "2019-09-28"})},
      {"generate_timestamp_array",
       {Timestamp(timestamp_min), Timestamp(timestamp_max),
        Int64(timestamp_max - timestamp_min), Enum(part_enum, "MICROSECOND")},
       Value::Array(TimestampArrayType(),
                    {Timestamp(timestamp_min), Timestamp(timestamp_max)})},
      {"generate_timestamp_array",
       QueryParamsWithResult({TimestampFromStr("2017-01-01 01:02:03.456789"),
                              TimestampFromStr("2017-01-01 01:02:03.456790"),
                              Int64(333), Enum(part_enum, "NANOSECOND")},
                             TimestampArray({"2017-01-01 01:02:03.456789",
                                             "2017-01-01 01:02:03.456789333",
                                             "2017-01-01 01:02:03.456789666",
                                             "2017-01-01 01:02:03.456789999"},
                                            functions::kNanoseconds))
           .WrapWithFeature(FEATURE_TIMESTAMP_NANOS)},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01 01:02:03.456789"),
        TimestampFromStr("2017-01-01 01:02:03.456801"), Int64(3),
        Enum(part_enum, "MICROSECOND")},
       TimestampArray(
           {"2017-01-01 01:02:03.456789", "2017-01-01 01:02:03.456792",
            "2017-01-01 01:02:03.456795", "2017-01-01 01:02:03.456798",
            "2017-01-01 01:02:03.456801"})},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01 01:02:03.456789"),
        TimestampFromStr("2017-01-01 01:02:03.490000"), Int64(11),
        Enum(part_enum, "MILLISECOND")},
       TimestampArray(
           {"2017-01-01 01:02:03.456789", "2017-01-01 01:02:03.467789",
            "2017-01-01 01:02:03.478789", "2017-01-01 01:02:03.489789"})},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01 01:02:03.456789"),
        TimestampFromStr("2016-12-30 12:00:00"), Int64(-76543),
        Enum(part_enum, "SECOND")},
       TimestampArray(
           {"2017-01-01 01:02:03.456789", "2016-12-31 03:46:20.456789"})},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01 01:02:03.456789"),
        TimestampFromStr("2017-01-01 01:10:00"), Int64(2),
        Enum(part_enum, "MINUTE")},
       TimestampArray(
           {"2017-01-01 01:02:03.456789", "2017-01-01 01:04:03.456789",
            "2017-01-01 01:06:03.456789", "2017-01-01 01:08:03.456789"})},
      {"generate_timestamp_array",
       {TimestampFromStr("2017-01-01"), TimestampFromStr("2017-01-05 12:00:00"),
        Int64(25), Enum(part_enum, "HOUR")},
       TimestampArray({"2017-01-01", "2017-01-02 01:00:00",
                       "2017-01-03 02:00:00", "2017-01-04 03:00:00",
                       "2017-01-05 04:00:00"})},
      // Guarding against overflows.
      {"generate_timestamp_array",
       {Timestamp(timestamp_max - 3), Timestamp(timestamp_max), Int64(2),
        Enum(part_enum, "MICROSECOND")},
       Value::Array(TimestampArrayType(), {Timestamp(timestamp_max - 3),
                                           Timestamp(timestamp_max - 1)})},
      {"generate_timestamp_array",
       {Timestamp(timestamp_min), Timestamp(timestamp_min), Int64(-1),
        Enum(part_enum, "MICROSECOND")},
       Value::Array(TimestampArrayType(), {Timestamp(timestamp_min)})},
      {"generate_timestamp_array",
       {Timestamp(timestamp_min), Timestamp(timestamp_max), int64max,
        Enum(part_enum, "MICROSECOND")},
       Value::Array(TimestampArrayType(), {Timestamp(timestamp_min)})},
      {"generate_timestamp_array",
       {Timestamp(timestamp_max), Timestamp(timestamp_min), int64min,
        Enum(part_enum, "MICROSECOND")},
       Value::Array(TimestampArrayType(), {Timestamp(timestamp_max)})},
  };

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsHash() {
  std::vector<FunctionTestCall> all_tests;

  // More interesting tests are in hash.test.
  const std::vector<std::string> function_names = {"md5", "sha1", "sha256",
                                                   "sha512"};
  for (const std::string& function_name : function_names) {
    all_tests.push_back({function_name, {NullBytes()}, NullBytes()});
    all_tests.push_back({function_name, {NullString()}, NullBytes()});
  }
  struct HashTest {
    std::string function;
    std::string input;
    std::string output;
  };
  // absl::HexStringToBytes
  std::vector<HashTest> hash_tests = {
      {"md5", "", "d41d8cd98f00b204e9800998ecf8427e"},
      {"md5", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
       "d174ab98d277d9f5a5611c2c9f419d9f"},

      {"sha1", "", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
      {"sha1",
       absl::HexStringToBytes("487351c8a5f440e4d03386483d5fe7bb669d41adcbfdb7"),
       "dbc1cb575ce6aeb9dc4ebf0f843ba8aeb1451e89"},

      {"sha256", "",
       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"sha256",
       absl::HexStringToBytes(
           "33fd9bc17e2b271fa04c6b93c0bdeae98654a7682d31d9b4dab7e6f32cd58f2f"
           "148a68fbe7a88c5ab1d88edccddeb30ab21e5e"),
       "cefdae1a3d75e792e8698d5e71f177cc761314e9ad5df9602c6e60ae65c4c267"},

      {"sha512", "",
       "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
       "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
      {"sha512",
       absl::HexStringToBytes(
           "8e8ef8aa336b3b98894c3126c71878910618838c00ac8590173c91749972ff3d"
           "42a61137029ad74501684f75e1b8d1d74336aa908c44082ae9eb162e901867f5"
           "4905"),
       "41672931558a93762522b1d55389ecf1b8c0feb8b88f4587fbd417ca809055b0"
       "cb630d8bea133ab7f6cf1f21c6b35e2e25c0d19583258808e6c23e1a75336103"},
  };
  for (const HashTest& hash_test : hash_tests) {
    all_tests.push_back({hash_test.function,
                         {Bytes(hash_test.input)},
                         {Bytes(absl::HexStringToBytes(hash_test.output))}});
  }

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsFarmFingerprint() {
  return std::vector<FunctionTestCall>{
      {"farm_fingerprint", {NullBytes()}, NullInt64()},
      {"farm_fingerprint", {NullString()}, NullInt64()},
      {"farm_fingerprint", {String("")}, Int64(-7286425919675154353)},
      {"farm_fingerprint", {String("\0")}, Int64(-4728684028706075820)},
      {"farm_fingerprint", {String("a b c d")}, Int64(-3676552216144541872)},
      {"farm_fingerprint",
       {String("abcABCжщфЖЩФ")},
       Int64(3736580821998982030)},
      {"farm_fingerprint",
       {String("Моша_öá5ホバークラフト鰻鰻")},
       Int64(4918483674106117036)},
      {"farm_fingerprint", {Bytes("")}, Int64(-7286425919675154353)},
      {"farm_fingerprint", {Bytes("\0")}, Int64(-4728684028706075820)},
      {"farm_fingerprint", {Bytes("a b c d")}, Int64(-3676552216144541872)},
      {"farm_fingerprint", {Bytes("a b\0c d")}, Int64(-4659737306420982693)},
      {"farm_fingerprint", {Bytes("abcABCжщфЖЩФ")}, Int64(3736580821998982030)},
      {"farm_fingerprint",
       {Bytes("Моша_öá5ホバークラフト鰻鰻")},
       Int64(4918483674106117036)},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsError() {
  return std::vector<FunctionTestCall>{
      {"error", {NullString()}, NullInt64(), OUT_OF_RANGE},
      {"error", {String("message")}, NullInt64(), OUT_OF_RANGE},
  };
}

}  // namespace googlesql
