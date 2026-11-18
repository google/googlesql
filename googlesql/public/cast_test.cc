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

#include "googlesql/public/cast.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/unicode_utils.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/function.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/token_list_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/declarative_type.h"
#include "googlesql/public/types/extended_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.h"
#include "googlesql/public/variant_value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

ABSL_DECLARE_FLAG(size_t, googlesql_enough_stack_bytes);

namespace googlesql {

MATCHER_P(StringValueMatches, matcher, "") {
  return ExplainMatchResult(matcher, arg.string_value(), result_listener);
}

using ::testing::HasSubstr;
using ::testing::StrCaseEq;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

static TypeFactory* type_factory = new TypeFactory();

static const StructType* SimpleStructType() {
  const StructType* struct_type;
  GOOGLESQL_EXPECT_OK(type_factory->MakeStructType(
      {{"", type_factory->get_string()}, {"", type_factory->get_string()}},
      &struct_type));
  return struct_type;
}

static const StructType* TimestampStructType() {
  const StructType* struct_type;
  GOOGLESQL_EXPECT_OK(type_factory->MakeStructType({{"a", type_factory->get_timestamp()},
                                          {"b", type_factory->get_timestamp()}},
                                         &struct_type));
  return struct_type;
}

TEST(CastValueWithTimezoneArgumentTests, TimestampCastTest) {
  // These are done here instead of in compliance tests for now, since the
  // test framework for the compliance tests does not support setting the
  // time zone.  TODO: Allow compliance testing to set the
  // time zone for requests if possible, then move these tests to the
  // compliance tests.
  const Value string_without_timezone = String("1970-01-01 01:01:06");
  const Value string_with_timezone =
      String("1970-01-01 01:01:06 America/Los_Angeles");
  const Value canonical_seconds_string = String("1970-01-01 01:01:06-08");
  const Value canonical_millis_string = String("1970-01-01 01:01:06.000-08");
  const Value canonical_micros_string = String("1970-01-01 01:01:06.000000-08");
  const Value timestamp = TimestampFromUnixMicros(32466000000);

  // TIMESTAMP to string, with zero truncation.
  const Type* string_type = String("").type();
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(0), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(1), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00.000001+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(10), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00.000010+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(100), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00.000100+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(1000), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00.001+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(10000), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00.010+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(100000), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:00.100+00")));
  EXPECT_THAT(CastValue(TimestampFromUnixMicros(1000000), absl::UTCTimeZone(),
                        LanguageOptions(), string_type),
              IsOkAndHolds(String("1970-01-01 00:00:01+00")));

  // Cast to STRUCT<TIMESTAMP, TIMESTAMP> with los_angeles timezone.
  absl::TimeZone los_angeles;
  absl::LoadTimeZone("America/Los_Angeles", &los_angeles);
  const Value struct_value = Value::Struct(
      SimpleStructType(), {string_with_timezone, string_without_timezone});
  const absl::StatusOr<Value> status_or_value = CastValue(
      struct_value, los_angeles, LanguageOptions(), TimestampStructType());
  GOOGLESQL_EXPECT_OK(status_or_value);

  const Value casted_struct_value = status_or_value.value();
  EXPECT_TRUE(casted_struct_value.Equals(
      Value::Struct(TimestampStructType(), {timestamp, timestamp})));
}

TEST(ConversionTest, ValueCastTest) {
  const Type* int_type = type_factory->get_int32();
  const Type* string_type = type_factory->get_string();

  Function conversion_function(
      "MyIntToMyString", "engine_defined_conversion", Function::SCALAR,
      /*function_signatures=*/{},
      FunctionOptions().set_evaluator([](const absl::Span<const Value> args) {
        ABSL_CHECK_EQ(args.size(), 1);
        return Value::StringValue(std::to_string(args[0].int32_value()));
      }));

  // Check evaluation of valid conversion.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Conversion conversion,
      Conversion::Create(int_type, string_type, &conversion_function,
                         CastFunctionProperty(CastFunctionType::IMPLICIT,
                                              /*coercion_cost=*/50)));
  ASSERT_TRUE(conversion.is_valid());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value casted_value,
                       conversion.evaluator().Eval(Value::Int32(12)));
  EXPECT_EQ(casted_value, Value::String("12"));

  // Check invalid conversion error generation.
  conversion = Conversion::Invalid();
  constexpr const char* const invalid_conversion_message =
      "Attempt to access properties of invalid Conversion";
  EXPECT_FALSE(conversion.is_valid());
  EXPECT_DEATH(conversion.from_type(), invalid_conversion_message);
  EXPECT_DEATH(conversion.to_type(), invalid_conversion_message);
  EXPECT_DEATH(conversion.property(), invalid_conversion_message);
  EXPECT_DEATH(conversion.evaluator().Eval(Value::Int32(12)).value(),
               invalid_conversion_message);
}

TEST(ConversionTest, CanonicalizedNanAndZeroTest) {
  const Type* string_type = type_factory->get_string();
  EXPECT_THAT(
      CastValue(Value::Float(-0.0), absl::UTCTimeZone(), LanguageOptions(),
                string_type, /*catalog=*/nullptr, /*canonicalize_zero=*/true),
      IsOkAndHolds(String("0")));
  EXPECT_THAT(
      CastValue(Value::Double(-0.0), absl::UTCTimeZone(), LanguageOptions(),
                string_type, /*catalog=*/nullptr, /*canonicalize_zero=*/true),
      IsOkAndHolds(String("0")));
  EXPECT_THAT(
      CastValue(Value::Float(-0.0), absl::UTCTimeZone(), LanguageOptions(),
                string_type, /*catalog=*/nullptr, /*canonicalize_zero=*/false),
      IsOkAndHolds(String("-0")));
  EXPECT_THAT(
      CastValue(Value::Double(-0.0), absl::UTCTimeZone(), LanguageOptions(),
                string_type, /*catalog=*/nullptr, /*canonicalize_zero=*/false),
      IsOkAndHolds(String("-0")));
  EXPECT_THAT(CastValue(Value::Float(std::numeric_limits<float>::quiet_NaN()),
                        absl::UTCTimeZone(), LanguageOptions(), string_type),
              IsOkAndHolds(StringValueMatches(StrCaseEq("nan"))));
  EXPECT_THAT(CastValue(Value::Float(std::numeric_limits<double>::quiet_NaN()),
                        absl::UTCTimeZone(), LanguageOptions(), string_type),
              IsOkAndHolds(StringValueMatches(StrCaseEq("nan"))));
  // Negative float NaN.
  EXPECT_THAT(CastValue(Value::Float(absl::bit_cast<float>(0xffc00000u)),
                        absl::UTCTimeZone(), LanguageOptions(), string_type),
              IsOkAndHolds(StringValueMatches(StrCaseEq("nan"))));
  // Negative double NaN.
  EXPECT_THAT(
      CastValue(Value::Float(absl::bit_cast<double>(0xfff8000000000000ul)),
                absl::UTCTimeZone(), LanguageOptions(), string_type),
      IsOkAndHolds(StringValueMatches(StrCaseEq("nan"))));
}

TEST(ConversionTest, ConversionMatchTest) {
  Function conversion_function("Name", "Group", Function::SCALAR);

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Conversion conversion,
        Conversion::Create(
            types::Int32Type(), types::StringType(), &conversion_function,
            CastFunctionProperty(CastFunctionType::EXPLICIT_OR_LITERAL,
                                 /*coercion_cost=*/50)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/true,
        Catalog::ConversionSourceExpressionKind::kOther)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kLiteral)));
    EXPECT_FALSE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kParameter)));
    EXPECT_FALSE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kOther)));
  }

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Conversion conversion,
        Conversion::Create(
            types::Int32Type(), types::StringType(), &conversion_function,
            CastFunctionProperty(
                CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER,
                /*coercion_cost=*/50)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/true,
        Catalog::ConversionSourceExpressionKind::kOther)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kLiteral)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kParameter)));
    EXPECT_FALSE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kOther)));
  }

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Conversion conversion,
        Conversion::Create(types::Int32Type(), types::StringType(),
                           &conversion_function,
                           CastFunctionProperty(CastFunctionType::EXPLICIT,
                                                /*coercion_cost=*/50)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/true,
        Catalog::ConversionSourceExpressionKind::kOther)));
    EXPECT_FALSE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kLiteral)));
    EXPECT_FALSE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kParameter)));
    EXPECT_FALSE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kOther)));
  }

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        Conversion conversion,
        Conversion::Create(types::Int32Type(), types::StringType(),
                           &conversion_function,
                           CastFunctionProperty(CastFunctionType::IMPLICIT,
                                                /*coercion_cost=*/50)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/true,
        Catalog::ConversionSourceExpressionKind::kOther)));
    EXPECT_TRUE(conversion.IsMatch(Catalog::FindConversionOptions(
        /*is_explicit=*/false,
        Catalog::ConversionSourceExpressionKind::kOther)));
  }
}

TEST(StructCastErrorTest, FieldSpecificError) {
  const StructType* from_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType(
      {{"a", type_factory->get_string()}, {"b", type_factory->get_string()}},
      &from_struct_type));
  const Value from_value = Value::Struct(
      from_struct_type, {Value::String("foo"), Value::String("bar")});

  const StructType* to_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType(
      {{"a", type_factory->get_string()}, {"b", type_factory->get_int64()}},
      &to_struct_type));

  EXPECT_THAT(
      CastValue(from_value, absl::UTCTimeZone(), LanguageOptions(),
                to_struct_type),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr(
                   "Error casting field 'b' in STRUCT: Bad int64 value: bar")));

  const StructType* from_anon_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType(
      {StructType::StructField("", type_factory->get_string()),
       StructType::StructField("", type_factory->get_string())},
      &from_anon_struct_type));
  const Value from_anon_value = Value::Struct(
      from_anon_struct_type, {Value::String("foo"), Value::String("bar")});

  const StructType* to_anon_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType(
      {StructType::StructField("", type_factory->get_string()),
       StructType::StructField("", type_factory->get_int64())},
      &to_anon_struct_type));

  EXPECT_THAT(CastValue(from_anon_value, absl::UTCTimeZone(), LanguageOptions(),
                        to_anon_struct_type),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Error casting field at index 1 in STRUCT: "
                                 "Bad int64 value: bar")));
}

TEST(ArrayCastTest, StringToInt64) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_CAST_DIFFERENT_ARRAY_TYPES);

  const ArrayType* from_type;
  GOOGLESQL_ASSERT_OK(
      type_factory->MakeArrayType(type_factory->get_string(), &from_type));
  const Value from_value = Value::Array(
      from_type, {Value::String("2"), Value::String("3"), Value::String("4")});

  const ArrayType* to_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeArrayType(type_factory->get_int64(), &to_type));

  const Value expected_value = Value::Array(
      to_type, {Value::Int64(2), Value::Int64(3), Value::Int64(4)});

  EXPECT_THAT(
      CastValue(from_value, absl::UTCTimeZone(), language_options, to_type),
      IsOkAndHolds(expected_value));

  const Value from_value_invalid = Value::Array(
      from_type,
      {Value::String("2"), Value::String("bad_val"), Value::String("4")});

  EXPECT_THAT(CastValue(from_value_invalid, absl::UTCTimeZone(),
                        language_options, to_type),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Error casting element at index 1 in ARRAY: "
                                 "Bad int64 value: bad_val")));
}

absl::StatusOr<Value> CastToJsonHelper(const Value& from_value) {
  LanguageOptions language_options;
  // Enable the cast to JSON feature.
  language_options.EnableLanguageFeature(FEATURE_CAST_TO_JSON_TYPE);
  return CastValue(from_value, absl::UTCTimeZone(), language_options,
                   types::JsonType());
}

TEST(JsonCastTest, SqlTypesToJson) {
  // Cast BOOL.
  EXPECT_THAT(CastToJsonHelper(Bool(true)),
              IsOkAndHolds(Json(JSONValue(true))));
  EXPECT_THAT(CastToJsonHelper(Bool(false)),
              IsOkAndHolds(Json(JSONValue(false))));

  // Cast INT32/UINT32.
  EXPECT_THAT(CastToJsonHelper(Int32(1)),
              IsOkAndHolds(Json(JSONValue(int64_t{1}))));
  EXPECT_THAT(CastToJsonHelper(Int32(-1)),
              IsOkAndHolds(Json(JSONValue(int64_t{-1}))));
  EXPECT_THAT(CastToJsonHelper(Int32(0)),
              IsOkAndHolds(Json(JSONValue(int64_t{0}))));
  EXPECT_THAT(CastToJsonHelper(Int32(std::numeric_limits<int32_t>::max())),
              IsOkAndHolds(Json(
                  JSONValue(int64_t{std::numeric_limits<int32_t>::max()}))));
  EXPECT_THAT(CastToJsonHelper(Int32(std::numeric_limits<int32_t>::min())),
              IsOkAndHolds(Json(
                  JSONValue(int64_t{std::numeric_limits<int32_t>::min()}))));

  EXPECT_THAT(CastToJsonHelper(Uint32(1)),
              IsOkAndHolds(Json(JSONValue(uint64_t{1}))));
  EXPECT_THAT(CastToJsonHelper(Uint32(0)),
              IsOkAndHolds(Json(JSONValue(uint64_t{0}))));
  EXPECT_THAT(CastToJsonHelper(Uint32(std::numeric_limits<uint32_t>::max())),
              IsOkAndHolds(Json(
                  JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}))));

  // Cast INT64/UINT64.
  EXPECT_THAT(CastToJsonHelper(Int64(1)),
              IsOkAndHolds(Json(JSONValue(int64_t{1}))));
  EXPECT_THAT(CastToJsonHelper(Int64(-1)),
              IsOkAndHolds(Json(JSONValue(int64_t{-1}))));
  EXPECT_THAT(CastToJsonHelper(Int64(0)),
              IsOkAndHolds(Json(JSONValue(int64_t{0}))));
  EXPECT_THAT(
      CastToJsonHelper(Int64(std::numeric_limits<int64_t>::max())),
      IsOkAndHolds(Json(JSONValue(std::numeric_limits<int64_t>::max()))));
  EXPECT_THAT(
      CastToJsonHelper(Int64(std::numeric_limits<int64_t>::min())),
      IsOkAndHolds(Json(JSONValue(std::numeric_limits<int64_t>::min()))));

  EXPECT_THAT(CastToJsonHelper(Uint64(1)),
              IsOkAndHolds(Json(JSONValue(uint64_t{1}))));
  EXPECT_THAT(CastToJsonHelper(Uint64(0)),
              IsOkAndHolds(Json(JSONValue(uint64_t{0}))));
  EXPECT_THAT(
      CastToJsonHelper(Uint64(std::numeric_limits<uint64_t>::max())),
      IsOkAndHolds(Json(JSONValue(std::numeric_limits<uint64_t>::max()))));

  // Cast FLOAT/DOUBLE.
  EXPECT_THAT(CastToJsonHelper(Float(1.5)),
              IsOkAndHolds(Json(JSONValue(double{1.5}))));
  EXPECT_THAT(CastToJsonHelper(Float(-1.5)),
              IsOkAndHolds(Json(JSONValue(double{-1.5}))));
  EXPECT_THAT(CastToJsonHelper(Float(0.0)),
              IsOkAndHolds(Json(JSONValue(double{0.0}))));
  EXPECT_THAT(CastToJsonHelper(Float(-0.0)),
              IsOkAndHolds(Json(JSONValue(double{0.0}))));

  EXPECT_THAT(CastToJsonHelper(Double(1.5)),
              IsOkAndHolds(Json(JSONValue(double{1.5}))));
  EXPECT_THAT(CastToJsonHelper(Double(-1.5)),
              IsOkAndHolds(Json(JSONValue(double{-1.5}))));
  EXPECT_THAT(CastToJsonHelper(Double(0.0)),
              IsOkAndHolds(Json(JSONValue(double{0.0}))));
  EXPECT_THAT(CastToJsonHelper(Double(-0.0)),
              IsOkAndHolds(Json(JSONValue(double{0.0}))));

  EXPECT_THAT(CastToJsonHelper(Double(std::numeric_limits<double>::infinity())),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Infinity is not a valid JSON number")));
  EXPECT_THAT(
      CastToJsonHelper(Double(-std::numeric_limits<double>::infinity())),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Infinity is not a valid JSON number")));
  EXPECT_THAT(
      CastToJsonHelper(Double(std::numeric_limits<double>::quiet_NaN())),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("NaN is not a valid JSON number")));
  EXPECT_THAT(
      CastToJsonHelper(Double(std::numeric_limits<double>::signaling_NaN())),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("NaN is not a valid JSON number")));
  EXPECT_THAT(
      CastToJsonHelper(Double(std::numeric_limits<double>::quiet_NaN())),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("NaN is not a valid JSON number")));

  // Cast NMERIC/BIGNUMERIC.
  EXPECT_THAT(CastToJsonHelper(Numeric(NumericValue(1))),
              IsOkAndHolds(Json(JSONValue(int64_t{1}))));
  EXPECT_THAT(CastToJsonHelper(Numeric(NumericValue::MaxValue())),
              IsOkAndHolds(Json(JSONValue(double{1e+29}))));

  EXPECT_THAT(CastToJsonHelper(BigNumeric(BigNumericValue(1))),
              IsOkAndHolds(Json(JSONValue(int64_t{1}))));
  EXPECT_THAT(CastToJsonHelper(BigNumeric(BigNumericValue::MaxValue())),
              IsOkAndHolds(Json(JSONValue(double{5.7896044618658096e+38}))));

  // Cast STRING.
  EXPECT_THAT(CastToJsonHelper(String("abc")),
              IsOkAndHolds(Json(JSONValue(absl::string_view("abc")))));
  EXPECT_THAT(CastToJsonHelper(String("")),
              IsOkAndHolds(Json(JSONValue(absl::string_view("")))));
  EXPECT_THAT(CastToJsonHelper(String("null")),
              IsOkAndHolds(Json(JSONValue(absl::string_view("null")))));
  EXPECT_THAT(CastToJsonHelper(String("1")),
              IsOkAndHolds(Json(JSONValue(absl::string_view("1")))));

  // Cast DATE/DATETIME
  EXPECT_THAT(CastToJsonHelper(values::Date(1)),
              IsOkAndHolds(Json(JSONValue(absl::string_view("1970-01-02")))));
  EXPECT_THAT(
      CastToJsonHelper(values::Datetime(
          DatetimeValue::FromYMDHMSAndMicros(2024, 1, 2, 3, 4, 5, 0))),
      IsOkAndHolds(Json(JSONValue(absl::string_view("2024-01-02T03:04:05")))));

  // Cast TIME/TIMESTAMP
  EXPECT_THAT(
      CastToJsonHelper(values::Time(TimeValue::FromHMSAndMicros(3, 4, 5, 0))),
      IsOkAndHolds(Json(JSONValue(absl::string_view("03:04:05")))));
  EXPECT_THAT(CastToJsonHelper(values::TimestampFromUnixMicros(1)),
              IsOkAndHolds(Json(JSONValue(
                  absl::string_view("1970-01-01T00:00:00.000001Z")))));

  // Cast UUID
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto uuid, UuidValue::FromString("00000000-0000-0000-0000-000000000000"));
  EXPECT_THAT(CastToJsonHelper(values::Uuid(uuid)),
              IsOkAndHolds(Json(JSONValue(
                  absl::string_view("00000000-0000-0000-0000-000000000000")))));

  // Cast BYTES
  EXPECT_THAT(CastToJsonHelper(values::Bytes("abc")),
              IsOkAndHolds(Json(JSONValue(absl::string_view("YWJj")))));

  // Cast ENUM
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));
  EXPECT_THAT(CastToJsonHelper(values::Enum(enum_type, 1)),
              IsOkAndHolds(Json(JSONValue(absl::string_view("TESTENUM1")))));

  // Cast INTERVAL
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto interval, IntervalValue::FromMonths(1));
  EXPECT_THAT(CastToJsonHelper(values::Interval(interval)),
              IsOkAndHolds(Json(JSONValue(absl::string_view("P1M")))));

  // Cast RANGE
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_val,
                       Value::MakeRange(values::Date(1), values::Date(10)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto expected_json,
                       JSONValue::ParseJSONString(
                           R"({"start":"1970-01-02","end":"1970-01-11"})"));
  EXPECT_THAT(CastToJsonHelper(range_val),
              IsOkAndHolds(Json(std::move(expected_json))));

  // Cast PROTO

  // Cast GRAPH_ELEMENT
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto node, GraphNode({"g"}, "n1", {{"a", Value::Int64(1)}}, {"L1"}, "T"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto expected_node_json,
      JSONValue::ParseJSONString(
          R"({"identifier":"bjE=","kind":"node","labels":["L1"],"properties":{"a":1}})"));
  EXPECT_THAT(CastToJsonHelper(node),
              IsOkAndHolds(Json(std::move(expected_node_json))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto edge, GraphEdge({"g"}, "e1", {{"a", Value::Int64(1)}}, {"L1"}, "T",
                           "n1", "n2"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto expected_edge_json,
      JSONValue::ParseJSONString(
          R"({"destination_node_identifier":"bjI=","identifier":"ZTE=","kind":"edge","labels":["L1"],"properties":{"a":1},"source_node_identifier":"bjE="})"));
  EXPECT_THAT(CastToJsonHelper(edge),
              IsOkAndHolds(Json(std::move(expected_edge_json))));

  // Cast GRAPH_PATH
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto node2,
      GraphNode({"g"}, "n2", {{"a", Value::Int64(1)}}, {"L1"}, "T"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value path,
                       Value::MakeGraphPath(test_values::MakeGraphPathType(
                                                node.type()->AsGraphElement(),
                                                edge.type()->AsGraphElement()),
                                            {node, edge, node2}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto expected_path_json,
      JSONValue::ParseJSONString(
          R"([{"identifier":"bjE=","kind":"node","labels":["L1"],"properties":{"a":1}},{"destination_node_identifier":"bjI=","identifier":"ZTE=","kind":"edge","labels":["L1"],"properties":{"a":1},"source_node_identifier":"bjE="},{"identifier":"bjI=","kind":"node","labels":["L1"],"properties":{"a":1}}])"));
  EXPECT_THAT(CastToJsonHelper(path),
              IsOkAndHolds(Json(std::move(expected_path_json))));

  // Cast GRAPH_ELEMENT with NaN float property fails with OUT_OF_RANGE.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nan_node,
      GraphNode(
          {"g"}, "n1",
          {{"f", Value::Double(std::numeric_limits<double>::quiet_NaN())}},
          {"L1"}, "T"));
  EXPECT_THAT(CastToJsonHelper(nan_node),
              StatusIs(absl::StatusCode::kOutOfRange));

  // Cast ARRAY
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(
      type_factory->MakeArrayType(type_factory->get_int64(), &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto expected_array_json,
                       JSONValue::ParseJSONString(R"([1,2])"));
  EXPECT_THAT(CastToJsonHelper(values::Array(
                  array_type, {values::Int64(1), values::Int64(2)})),
              IsOkAndHolds(Json(std::move(expected_array_json))));

  // Cast STRUCT
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", type_factory->get_int64()}},
                                         &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto expected_struct_json,
                       JSONValue::ParseJSONString(R"({"a":1})"));
  EXPECT_THAT(CastToJsonHelper(values::Struct(struct_type, {values::Int64(1)})),
              IsOkAndHolds(Json(std::move(expected_struct_json))));
}

TEST(MapCastErrorTest, KeyValueSpecificError) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);

  const Type* from_map_type;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      from_map_type,
      type_factory->MakeMapType(type_factory->get_string(),
                                type_factory->get_string(), language_options));

  const Type* to_map_type;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      to_map_type,
      type_factory->MakeMapType(type_factory->get_int64(),
                                type_factory->get_int64(), language_options));

  // Error in key cast
  const Value from_value_bad_key =
      Value::MakeMap(from_map_type,
                     {{Value::String("1"), Value::String("10")},
                      {Value::String("bad_key"), Value::String("20")},
                      {Value::String("3"), Value::String("30")}})
          .value();

  EXPECT_THAT(CastValue(from_value_bad_key, absl::UTCTimeZone(),
                        language_options, to_map_type),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Error casting key \"bad_key\" in MAP: Bad "
                                 "int64 value: bad_key")));

  // Error in value cast
  const Value from_value_bad_value =
      Value::MakeMap(from_map_type,
                     {{Value::String("1"), Value::String("10")},
                      {Value::String("2"), Value::String("bad_value")},
                      {Value::String("3"), Value::String("30")}})
          .value();

  EXPECT_THAT(CastValue(from_value_bad_value, absl::UTCTimeZone(),
                        language_options, to_map_type),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Error casting value for key \"2\" in MAP: "
                                 "Bad int64 value: bad_value")));
}

TEST(GraphCastTests, GraphElementTypeTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_node_no_properties,
                       GraphNode({"graph_name"}, "id1", {}, {"label1"},
                                 "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_node_no_properties_different_label,
                       GraphNode({"graph_name"}, "id2", {}, {"label2"},
                                 "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_node_no_properties_different_name,
                       GraphNode({"graph_name"}, "id1", {}, {"label1"},
                                 "NewElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label1"}, "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_null_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::NullString()}, {"b", Value::Int32(1)}},
                {"label1"}, "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_null_b_null,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::NullString()}, {"b", Value::NullInt32()}},
                {"label1"}, "ElementTable", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_null_b_null_id2,
      GraphNode({"graph_name"}, "id2",
                {{"a", Value::NullString()}, {"b", Value::NullInt32()}},
                {"label1"}, "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_int_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::Int32(10)}, {"b", Value::Int32(1)}}, {"label1"},
                "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_b_c,
      GraphNode({"graph_name"}, "id1",
                {{"b", Value::Int32(1)}, {"c", Value::Int32(1)}}, {"label1"},
                "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_edge_a_b,
      GraphEdge({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label2"}, "ElementTable", "src_node_id", "dst_node_id",
                type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value different_graph_node_a_b,
      GraphNode({"new_graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label3"}, "ElementTable", type_factory));

  EXPECT_THAT(CastValue(graph_node_no_properties, absl::UTCTimeZone(),
                        LanguageOptions(), graph_node_a_b.type()),
              IsOkAndHolds(graph_node_a_null_b_null));

  // CastValue has no effect on labels.
  EXPECT_THAT(
      CastValue(graph_node_no_properties_different_label, absl::UTCTimeZone(),
                LanguageOptions(), graph_node_a_b.type()),
      IsOkAndHolds(graph_node_a_null_b_null_id2));

  // CastValue has no effect on definition name.
  EXPECT_THAT(
      CastValue(graph_node_no_properties_different_name, absl::UTCTimeZone(),
                LanguageOptions(), graph_node_a_b.type()),
      IsOkAndHolds(graph_node_a_null_b_null));

  EXPECT_THAT(CastValue(graph_node_a_b, absl::UTCTimeZone(), LanguageOptions(),
                        graph_node_a_int_b.type()),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("property of the same name must have the "
                                 "same value type")));
  EXPECT_THAT(CastValue(graph_edge_a_b, absl::UTCTimeZone(), LanguageOptions(),
                        graph_node_a_b.type()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("between node and edge type")));

  EXPECT_THAT(CastValue(different_graph_node_a_b, absl::UTCTimeZone(),
                        LanguageOptions(), graph_node_a_b.type()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("with different graph references")));
}

// Parameterized common test that applies to both node and edge.
class GraphElementValueCastTest
    : public TestWithParam<GraphElementType::ElementKind> {
 protected:
  void SetUp() override {
    language_options_.EnableLanguageFeature(
        FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  }

  bool IsNode() const { return GetParam() == GraphElementType::kNode; }
  bool IsEdge() const { return GetParam() == GraphElementType::kEdge; }

  // A graph element value might contain static property values that are only a
  // subset of the declared static properties of its graph element type.
  absl::StatusOr<Value> MakeDynamicGraphElementByType(
      const GraphElementType* type, std::string identifier,
      std::vector<Value::Property> static_properties,
      std::vector<Value::Property> dynamic_properties,
      std::vector<std::string> static_labels,
      std::vector<std::string> dynamic_labels, std::string definition_name) {
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                     MakePropertiesJsonValue(absl::MakeSpan(dynamic_properties),
                                             language_options_));
    return IsNode()
               ? Value::MakeGraphNode(
                     type, std::move(identifier),
                     {.static_labels = std::move(static_labels),
                      .static_properties = std::move(static_properties),
                      .dynamic_labels = std::move(dynamic_labels),
                      .dynamic_properties = json_value.GetConstRef()},
                     std::move(definition_name))
               : Value::MakeGraphEdge(
                     type, std::move(identifier),
                     {.static_labels = std::move(static_labels),
                      .static_properties = std::move(static_properties),
                      .dynamic_labels = std::move(dynamic_labels),
                      .dynamic_properties = json_value.GetConstRef()},
                     std::move(definition_name), "src_node_id", "dst_node_id");
  }

  Value MakeElement(absl::Span<const std::string> graph_reference,
                    std::string identifier,
                    std::vector<Value::Property> properties,
                    absl::Span<const std::string> labels,
                    std::string definition_name) {
    auto value =
        IsNode() ? GraphNode(graph_reference, identifier, properties, labels,
                             definition_name)
                 : GraphEdge(graph_reference, identifier, properties, labels,
                             definition_name, "src_node_id", "dst_node_id");
    GOOGLESQL_CHECK_OK(value);
    return *value;
  }

  absl::StatusOr<Value> MakeDynamicElement(
      absl::Span<const std::string> graph_reference, std::string identifier,
      std::vector<Value::Property> static_properties,
      std::vector<Value::Property> dynamic_properties,
      std::vector<std::string> static_labels,
      std::vector<std::string> dynamic_labels, std::string definition_name) {
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                     MakePropertiesJsonValue(absl::MakeSpan(dynamic_properties),
                                             language_options_));
    return IsNode() ? DynamicGraphNode(
                          graph_reference, identifier, static_properties,
                          /*dynamic_properties=*/json_value.GetConstRef(),
                          static_labels, dynamic_labels, definition_name)
                    : DynamicGraphEdge(
                          graph_reference, identifier, static_properties,
                          /*dynamic_properties=*/json_value.GetConstRef(),
                          static_labels, dynamic_labels, definition_name,
                          "src_node_id", "dst_node_id");
  }

  LanguageOptions language_options_ = LanguageOptions::MaximumFeatures();
};

INSTANTIATE_TEST_SUITE_P(Common, GraphElementValueCastTest,
                         ValuesIn({GraphElementType::kNode,
                                   GraphElementType::kEdge}));

TEST_P(GraphElementValueCastTest, DynamicGraphElementTypeTest) {
  const Value static_no_properties =
      MakeElement({"graph_name"}, "id1", {}, {"label1"}, "ElementTable");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value dynamic_no_properties,
                       MakeDynamicElement({"graph_name"}, "id1", {}, {},
                                          {"label1"}, {}, "ElementTable"));

  const Value static_no_properties_different_label =
      MakeElement({"graph_name"}, "id1", {}, {"label2"}, "ElementTable");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value dynamic_no_properties_different_label,
                       MakeDynamicElement({"graph_name"}, "id1", {}, {},
                                          {"label2"}, {}, "ElementTable"));

  const Value static_no_properties_different_name =
      MakeElement({"graph_name"}, "id1", {}, {"label2"}, "NewElementTable");

  const Value static_sp_a_b =
      MakeElement({"graph_name"}, "id1",
                  {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                  {"label1"}, "ElementTable");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b,
      MakeDynamicElement({"graph_name"}, "id1",
                         {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                         {}, {"label1"}, {}, "ElementTable"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_b_c,
      MakeDynamicElement({"graph_name"}, "id1",
                         {{"b", Value::Int32(1)}, {"c", Value::Float(3.14)}},
                         {}, {"label1"}, {}, "ElementTable"));

  const Value static_sp_c =
      MakeElement({"graph_name"}, "id1", {{"c", Value::String("v0")}},
                  {"label1"}, "ElementTable");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_c_d,
      MakeDynamicElement({"graph_name"}, "id1",
                         {{"c", Value::String("v0")}, {"d", Value::Int32(1)}},
                         {}, {"label1"}, {}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_dp_c_d,
      MakeDynamicElement({"graph_name"}, "id1", {},
                         {{"c", Value::String("v0")}, {"d", Value::Int32(1)}},
                         {"label1"}, {}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value dynamic_sp_c_d_nulls,
                       MakeDynamicElement({"graph_name"}, "id1",
                                          {{"c", Value::NullString()},
                                           {"d", Value::NullInt32()}},
                                          {}, {"label1"}, {}, "ElementTable"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b_dp_c_d,
      MakeDynamicElement({"graph_name"}, "id1",
                         {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                         {{"c", Value::String("v0")}, {"d", Value::Int32(1)}},
                         {"label1"}, {}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b_c_dp_c_d,
      MakeDynamicElement({"graph_name"}, "id1",
                         {{"a", Value::String("v0")},
                          {"b", Value::Int32(1)},
                          {"c", Value::Float(3.14)}},
                         {{"c", Value::String("v0")}, {"d", Value::Int32(1)}},
                         {"label1"}, {}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value dynamic_sp_a_b_c_null_dp_d,
                       MakeDynamicElement({"graph_name"}, "id1",
                                          {{"a", Value::String("v0")},
                                           {"b", Value::Int32(1)},
                                           {"c", Value::NullFloat()}},
                                          {{"d", Value::Int32(1)}}, {"label1"},
                                          {}, "ElementTable"));

  // Invalid coercion.
  EXPECT_THAT(CastValue(static_sp_a_b, absl::UTCTimeZone(), language_options_,
                        dynamic_sp_b_c.type()),
              StatusIs(absl::StatusCode::kInternal));

  // Conversion 1: static -> dynamic
  EXPECT_THAT(CastValue(static_no_properties, absl::UTCTimeZone(),
                        language_options_, dynamic_no_properties.type()),
              IsOkAndHolds(dynamic_no_properties));

  // CastValue has no effect on labels.
  auto static_to_dynamic_no_properties_different_labels =
      CastValue(static_no_properties_different_label, absl::UTCTimeZone(),
                language_options_, dynamic_no_properties.type());
  EXPECT_THAT(static_to_dynamic_no_properties_different_labels,
              IsOkAndHolds(dynamic_no_properties));
  EXPECT_THAT(static_to_dynamic_no_properties_different_labels,
              IsOkAndHolds(dynamic_no_properties_different_label));

  // CastValue has no effect on definition name.
  EXPECT_THAT(
      CastValue(static_no_properties_different_name, absl::UTCTimeZone(),
                language_options_, dynamic_no_properties.type()),
      IsOkAndHolds(dynamic_no_properties));

  // The return type's static properties are defined by the "to type".
  EXPECT_THAT(CastValue(static_sp_a_b, absl::UTCTimeZone(), language_options_,
                        dynamic_sp_a_b.type()),
              IsOkAndHolds(dynamic_sp_a_b));
  EXPECT_THAT(CastValue(static_sp_c, absl::UTCTimeZone(), language_options_,
                        dynamic_sp_c_d.type()),
              IsOkAndHolds(dynamic_sp_c_d_nulls));

  // Conversion 2: dynamic -> dynamic
  EXPECT_THAT(CastValue(dynamic_dp_c_d, absl::UTCTimeZone(), language_options_,
                        dynamic_sp_c_d.type()),
              IsOkAndHolds(dynamic_sp_c_d_nulls));
  EXPECT_THAT(CastValue(dynamic_sp_a_b_dp_c_d, absl::UTCTimeZone(),
                        language_options_, dynamic_sp_a_b_c_dp_c_d.type()),
              IsOkAndHolds(dynamic_sp_a_b_c_null_dp_d));
}

TEST_P(GraphElementValueCastTest,
       DynamicGraphElementValueWithStaticPropertiesSubsetOfThatInType) {
  const GraphElementType* type_a_b_c_d =
      MakeDynamicGraphElementType({"graph_name"}, GetParam(),
                                  {{"a", StringType()},
                                   {"b", Int32Type()},
                                   {"c", BoolType()},
                                   {"d", FloatType()}});

  const GraphElementType* type_a_b_c_d_e =
      MakeDynamicGraphElementType({"graph_name"}, GetParam(),
                                  {{"a", StringType()},
                                   {"b", Int32Type()},
                                   {"c", BoolType()},
                                   {"d", FloatType()},
                                   {"e", Int64Type()}});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b,
      MakeDynamicGraphElementByType(
          type_a_b_c_d, "id", /*static_properties=*/
          {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
          /*dynamic_properties=*/{}, /*static_labels=*/{"label1"},
          /*dynamic_labels=*/{"label2"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b_multi_dynamic_labels,
      MakeDynamicGraphElementByType(
          type_a_b_c_d, "id", /*static_properties=*/
          {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
          /*dynamic_properties=*/{}, /*static_labels=*/{"label1"},
          /*dynamic_labels=*/{"label3", "label2"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b_c_null_d_null_e_null,
      MakeDynamicElement({"graph_name"}, "id",
                         {{"a", Value::String("v0")},
                          {"b", Value::Int32(1)},
                          {"c", Value::NullBool()},
                          {"d", Value::NullFloat()},
                          {"e", Value::NullInt64()}},
                         {}, {"label1"}, {"label2"}, "ElementTable"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b_dp_c_d,
      MakeDynamicGraphElementByType(
          type_a_b_c_d, "id", /*static_properties=*/
          {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"c", Value::Bool(true)}, {"d", Value::Float(3.14)}},
          /*static_labels=*/{"label1"},
          /*dynamic_labels=*/{"label2"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value dynamic_sp_a_b_dp_c_d_wrong_type,
      MakeDynamicGraphElementByType(
          type_a_b_c_d, "id", /*static_properties=*/
          {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
          /*dynamic_properties=*/
          {{"c", Value::String("v0")}, {"d", Value::String("v0")}},
          /*static_labels=*/{"label1"},
          /*dynamic_labels=*/{"label2"}, "ElementTable"));

  EXPECT_THAT(CastValue(dynamic_sp_a_b, absl::UTCTimeZone(), language_options_,
                        type_a_b_c_d_e),
              IsOkAndHolds(dynamic_sp_a_b_c_null_d_null_e_null));

  // Multi dynamic labels do not change the properties.
  EXPECT_THAT(CastValue(dynamic_sp_a_b_multi_dynamic_labels,
                        absl::UTCTimeZone(), language_options_, type_a_b_c_d_e),
              IsOkAndHolds(dynamic_sp_a_b_c_null_d_null_e_null));
  // Dynamic properties with conflicting names compared to to_type's static
  // properties are dropped.
  EXPECT_THAT(CastValue(dynamic_sp_a_b_dp_c_d, absl::UTCTimeZone(),
                        language_options_, type_a_b_c_d_e),
              IsOkAndHolds(dynamic_sp_a_b_c_null_d_null_e_null));

  // Dynamic properties with conflicting names and conflicting types compared
  // to to_type's static properties are dropped.
  EXPECT_THAT(CastValue(dynamic_sp_a_b_dp_c_d_wrong_type, absl::UTCTimeZone(),
                        language_options_, type_a_b_c_d_e),
              IsOkAndHolds(dynamic_sp_a_b_c_null_d_null_e_null));
}

TEST(GraphCastTests, GraphPathTypeTest) {
  using test_values::MakeGraphPathType;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_node_no_properties,
                       GraphNode({"graph_name"}, "id1", {}, {"label1"},
                                 "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_node_no_properties_different_label,
                       GraphNode({"graph_name"}, "id2", {}, {"label2"},
                                 "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label1"}, "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_null_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::NullString()}, {"b", Value::Int32(1)}},
                {"label1"}, "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_int_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::Int32(10)}, {"b", Value::Int32(1)}}, {"label1"},
                "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_b_c,
      GraphNode({"graph_name"}, "id1",
                {{"b", Value::Int32(1)}, {"c", Value::Int32(1)}}, {"label1"},
                "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value different_graph_node_no_properties,
                       GraphNode({"different_graph_name"}, "id1", {},
                                 {"label1"}, "ElementTable", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_edge_no_properties,
                       GraphEdge({"graph_name"}, "id1", {}, {"label2"},
                                 "ElementTable", "id1", "id2", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_edge_a_b,
      GraphEdge({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label2"}, "ElementTable", "id1", "id2", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_edge_a_null_b_null,
      GraphEdge({"graph_name"}, "id1",
                {{"a", Value::NullString()}, {"b", Value::NullInt32()}},
                {"label2"}, "ElementTable", "id1", "id2", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value different_graph_edge_no_properties,
      GraphEdge({"different_graph_name"}, "id1", {}, {"label1"}, "ElementTable",
                "id1", "id2", type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value path_node_empty_edge_a_b,
      Value::MakeGraphPath(
          MakeGraphPathType(graph_node_no_properties.type()->AsGraphElement(),
                            graph_edge_a_b.type()->AsGraphElement()),
          {graph_node_no_properties, graph_edge_a_b,
           graph_node_no_properties_different_label}));
  const GraphPathType* path_type_node_empty_edge_empty =
      MakeGraphPathType(graph_node_no_properties.type()->AsGraphElement(),
                        graph_edge_no_properties.type()->AsGraphElement());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value path_node_empty_edge_empty,
      Value::MakeGraphPath(path_type_node_empty_edge_empty,
                           {graph_node_no_properties, graph_edge_no_properties,
                            graph_node_no_properties_different_label}));
  EXPECT_THAT(CastValue(path_node_empty_edge_empty, absl::UTCTimeZone(),
                        LanguageOptions(), path_node_empty_edge_a_b.type()),
              IsOkAndHolds(path_node_empty_edge_a_b));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value path_node_empty_edge_a_null_b_null,
      Value::MakeGraphPath(path_node_empty_edge_a_b.type()->AsGraphPath(),
                           {graph_node_no_properties, graph_edge_a_null_b_null,
                            graph_node_no_properties_different_label}));
  EXPECT_THAT(CastValue(path_node_empty_edge_empty, absl::UTCTimeZone(),
                        LanguageOptions(), path_node_empty_edge_a_b.type()),
              IsOkAndHolds(path_node_empty_edge_a_null_b_null));

  const GraphPathType* path_type_node_a_b_edge_empty =
      MakeGraphPathType(graph_node_a_b.type()->AsGraphElement(),
                        graph_edge_no_properties.type()->AsGraphElement());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value path_node_a_b_edge_empty,
                       Value::MakeGraphPath(path_type_node_a_b_edge_empty,
                                            {graph_node_a_null_b}));
  const GraphPathType* path_type_node_a_int_b_edge_empty =
      MakeGraphPathType(graph_node_a_int_b.type()->AsGraphElement(),
                        graph_edge_no_properties.type()->AsGraphElement());
  EXPECT_THAT(CastValue(path_node_a_b_edge_empty, absl::UTCTimeZone(),
                        LanguageOptions(), path_type_node_a_int_b_edge_empty),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("property of the same name must have the "
                                 "same value type")));

  const GraphPathType* different_graph_path_type = MakeGraphPathType(
      different_graph_node_no_properties.type()->AsGraphElement(),
      different_graph_edge_no_properties.type()->AsGraphElement());
  EXPECT_THAT(
      CastValue(path_node_empty_edge_empty, absl::UTCTimeZone(),
                LanguageOptions(), different_graph_path_type),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Cannot cast between graph element types with different "
                    "graph references")));
}

static void ExecuteTest(const QueryParamsWithResult& test_case) {
  ABSL_CHECK_EQ(1, test_case.num_params());
  const Value& from_value = test_case.param(0);
  absl::TimeZone los_angeles;
  absl::LoadTimeZone("America/Los_Angeles", &los_angeles);
  LanguageOptions language_options;
  for (LanguageFeature feature : test_case.required_features()) {
    language_options.EnableLanguageFeature(feature);
  }
  if ((from_value.type()->IsFeatureV12CivilTimeType() ||
       test_case.result().type()->IsFeatureV12CivilTimeType()) &&
      !language_options.LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) {
    return;
  }
  const Type* expected_type = test_case.result().type();
  const absl::StatusOr<Value> status_or_value =
      CastValue(from_value, los_angeles, language_options, expected_type,
                /*catalog=*/nullptr, /*canonicalize_zero=*/true);
  const std::string error_string =
      absl::StrCat("from type: ", from_value.type()->DebugString(),
                   "\nfrom value: ", from_value.FullDebugString(),
                   "\nexpected type: ", expected_type->DebugString(),
                   "\nexpected value: ", test_case.result().FullDebugString());
  if (test_case.status().ok()) {
    GOOGLESQL_ASSERT_OK(status_or_value) << error_string;
    const Value& coerced_value = status_or_value.value();
    EXPECT_EQ(test_case.result(), coerced_value)
        << error_string
        << "\ncoerced value: " << coerced_value.FullDebugString();
  } else {
    EXPECT_FALSE(status_or_value.ok())
        << error_string
        << "\ncoerced value: " << status_or_value.value().FullDebugString();
  }
}

// Some cast behaviors are not dictated by GoogleSQL, particularly casting
// between PROTO and BYTES.  Engines are free to use different implementations,
// with different semantics.  These tests cover the logic for such casting
// in CastStatusOrValue(), but do not belong in compliance tests since different
// engines could behave different ways and still be compliant.
static std::vector<QueryParamsWithResult>
GetProtoAndBytesCastsWithoutValidation() {
  const ProtoType* kitchen_sink_proto_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &kitchen_sink_proto_type));
  const ProtoType* nullable_int_proto_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::NullableInt::descriptor(), &nullable_int_proto_type));

  return {
      // As currently implemented in CastValue(), casting between BYTES and
      // PROTO does no validation so these succeed.
      {{Proto(nullable_int_proto_type, absl::Cord("bunch of invalid stuff"))},
       Bytes("bunch of invalid stuff")},
      {{Bytes("bunch of invalid stuff")},
       Proto(nullable_int_proto_type, absl::Cord("bunch of invalid stuff"))},
      {{Proto(kitchen_sink_proto_type, absl::Cord("bunch of invalid stuff"))},
       Bytes("bunch of invalid stuff")},
      {{Bytes("bunch of invalid stuff")},
       Proto(kitchen_sink_proto_type, absl::Cord("bunch of invalid stuff"))},
  };
}

TEST(VariantCastTest, CastAllTypesToVariant) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);

  auto test_cast_to_variant = [&](const Value& v) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant,
                         CastValue(v, absl::UTCTimeZone(), language_options,
                                   types::VariantType()));
    EXPECT_TRUE(variant.type()->IsVariant());
    EXPECT_FALSE(variant.is_null());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto variant_view, variant.variant_value());
    EXPECT_TRUE(variant_view.is_valid());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_variant, Value::Variant(v));
    EXPECT_TRUE(variant.Equals(expected_variant));
  };

  // Simple types
  test_cast_to_variant(Value::Int32(1));
  test_cast_to_variant(Value::Int64(2));
  test_cast_to_variant(Value::Uint32(3));
  test_cast_to_variant(Value::Uint64(4));
  test_cast_to_variant(Value::Bool(true));
  test_cast_to_variant(Value::Float(5.0f));
  test_cast_to_variant(Value::Double(6.0));
  test_cast_to_variant(Value::String("hello"));
  test_cast_to_variant(Value::Bytes("world"));
  test_cast_to_variant(Value::Date(1000));
  test_cast_to_variant(Value::Timestamp(absl::FromUnixSeconds(1000)));
  test_cast_to_variant(Value::Time(TimeValue::FromHMSAndMicros(1, 2, 3, 4)));
  test_cast_to_variant(Value::Datetime(
      DatetimeValue::FromYMDHMSAndMicros(2025, 1, 2, 3, 4, 5, 6)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto interval_val,
                       IntervalValue::FromMonthsDaysMicros(1, 2, 3));
  test_cast_to_variant(Value::Interval(interval_val));
  test_cast_to_variant(Value::Numeric(NumericValue(123)));
  test_cast_to_variant(Value::BigNumeric(BigNumericValue(456)));

  // JSON
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_obj,
                       JSONValue::ParseJSONString("{\"a\":1}"));
  test_cast_to_variant(Value::Json(std::move(json_obj)));

  // JSON array
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_arr,
                       JSONValue::ParseJSONString("[1, \"two\", false]"));
  test_cast_to_variant(Value::Json(std::move(json_arr)));

  // JSON primitives (number, string, boolean)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_num,
                       JSONValue::ParseJSONString("123.45"));
  test_cast_to_variant(Value::Json(std::move(json_num)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_str,
                       JSONValue::ParseJSONString("\"sample\""));
  test_cast_to_variant(Value::Json(std::move(json_str)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_bool, JSONValue::ParseJSONString("true"));
  test_cast_to_variant(Value::Json(std::move(json_bool)));

  // JSON null primitive wrapped in Variant should act as a Variant Null
  // primitive (IsVariantNull() == true)
  Value json_null = Value::Json(JSONValue());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_json_null,
                       CastValue(json_null, absl::UTCTimeZone(),
                                 language_options, types::VariantType()));
  EXPECT_TRUE(variant_json_null.type()->IsVariant());
  EXPECT_FALSE(variant_json_null.is_null());  // Not a SQL null
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto variant_json_null_view,
                       variant_json_null.variant_value());
  EXPECT_TRUE(variant_json_null_view.IsVariantNull());

  // UUID
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto uuid_val, UuidValue::FromString("9d3da3234c20360fbd9bec54feec54f0"));
  test_cast_to_variant(Value::Uuid(uuid_val));

  // Map
  test_cast_to_variant(Map({{Value::String("a"), Value::Int64(1)}}));

  // Array
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeArrayType(types::Int64Type(), &array_type));
  test_cast_to_variant(
      Value::Array(array_type, {Value::Int64(1), Value::Int64(2)}));

  // Struct
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      type_factory->MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  test_cast_to_variant(Value::Struct(struct_type, {Value::Int64(1)}));

  // Proto
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));
  test_cast_to_variant(Value::Proto(proto_type, absl::Cord("")));

  // Enum
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));
  test_cast_to_variant(Value::Enum(enum_type, 1));
}

TEST(VariantCastTest, CastNULLs) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);

  // Identity cast for SQL NULL Variant (Untyped SQL NULL in Variant context)
  Value null_variant = Value::NullVariant();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value result_null_variant,
                       CastValue(null_variant, absl::UTCTimeZone(),
                                 language_options, types::VariantType()));
  EXPECT_TRUE(result_null_variant.is_null());
  EXPECT_EQ(result_null_variant.type(), types::VariantType());

  auto test_cast_null_to_variant = [&](const Value& null_val) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant,
                         CastValue(null_val, absl::UTCTimeZone(),
                                   language_options, types::VariantType()));
    EXPECT_TRUE(variant.type()->IsVariant());
    EXPECT_TRUE(variant.is_null());
    EXPECT_FALSE(variant.has_content());
    EXPECT_THAT(variant.variant_value(), StatusIs(absl::StatusCode::kInternal));
    EXPECT_TRUE(variant.Equals(Value::NullVariant()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected, Value::Variant(null_val));
    EXPECT_TRUE(variant.Equals(expected));
  };

  // Simple types
  test_cast_null_to_variant(Value::NullInt32());
  test_cast_null_to_variant(Value::NullInt64());
  test_cast_null_to_variant(Value::NullUint32());
  test_cast_null_to_variant(Value::NullUint64());
  test_cast_null_to_variant(Value::NullBool());
  test_cast_null_to_variant(Value::NullFloat());
  test_cast_null_to_variant(Value::NullDouble());
  test_cast_null_to_variant(Value::NullString());
  test_cast_null_to_variant(Value::NullBytes());
  test_cast_null_to_variant(Value::NullDate());
  test_cast_null_to_variant(Value::NullTimestamp());
  test_cast_null_to_variant(Value::NullTime());
  test_cast_null_to_variant(Value::NullDatetime());
  test_cast_null_to_variant(Value::NullInterval());
  test_cast_null_to_variant(Value::NullNumeric());
  test_cast_null_to_variant(Value::NullBigNumeric());
  test_cast_null_to_variant(Value::NullJson());
  test_cast_null_to_variant(Value::NullUuid());

  // Complex types NULLs
  // Array
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeArrayType(types::Int64Type(), &array_type));
  test_cast_null_to_variant(Value::Null(array_type));

  // Struct
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      type_factory->MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  test_cast_null_to_variant(Value::Null(struct_type));

  // Map
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      type_factory->MakeMapType(types::StringType(), types::Int64Type(),
                                language_options));
  test_cast_null_to_variant(Value::Null(map_type));

  // Proto
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));
  test_cast_null_to_variant(Value::Null(proto_type));

  // Enum
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));
  test_cast_null_to_variant(Value::Null(enum_type));
}

TEST(VariantCastTest, CastObjectsAndArraysWithNullsToVariant) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);

  // Struct with a NULL field cast to VARIANT produces a non-null Variant
  // Object whose null field is exposed as a VariantNull.
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}}, &struct_type));
  Value struct_with_null =
      Value::Struct(struct_type, {Value::Int64(1), Value::NullString()});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_struct,
                       CastValue(struct_with_null, absl::UTCTimeZone(),
                                 language_options, types::VariantType()));
  EXPECT_TRUE(variant_struct.type()->IsVariant());
  EXPECT_FALSE(variant_struct.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView struct_view,
                       variant_struct.variant_value());
  EXPECT_TRUE(struct_view.IsObject());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_a,
                       struct_view.GetKeyValueIfExists("a"));
  EXPECT_THAT(field_a.GetPrimitiveValue(), IsOkAndHolds(Value::Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView field_b,
                       struct_view.GetKeyValueIfExists("b"));
  EXPECT_FALSE(field_b.is_null());
  EXPECT_TRUE(field_b.IsVariantNull());

  // Map with a NULL value cast to VARIANT produces a non-null Variant Object
  // whose null value is exposed as a VariantNull.
  Value map_with_null = Map({{Value::String("a"), Value::Int64(1)},
                             {Value::String("b"), Value::NullInt64()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_map,
                       CastValue(map_with_null, absl::UTCTimeZone(),
                                 language_options, types::VariantType()));
  EXPECT_TRUE(variant_map.type()->IsVariant());
  EXPECT_FALSE(variant_map.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView map_view, variant_map.variant_value());
  EXPECT_TRUE(map_view.IsObject());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView map_val_b,
                       map_view.GetKeyValueIfExists("b"));
  EXPECT_FALSE(map_val_b.is_null());
  EXPECT_TRUE(map_val_b.IsVariantNull());

  // JSON object with a null field cast to VARIANT produces a non-null Variant
  // Object whose null field is exposed as a VariantNull.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_obj_with_null,
                       JSONValue::ParseJSONString(R"({"a": 1, "b": null})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value variant_json_obj,
      CastValue(Value::Json(std::move(json_obj_with_null)), absl::UTCTimeZone(),
                language_options, types::VariantType()));
  EXPECT_FALSE(variant_json_obj.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView json_obj_view,
                       variant_json_obj.variant_value());
  EXPECT_TRUE(json_obj_view.IsObject());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView json_field_b,
                       json_obj_view.GetKeyValueIfExists("b"));
  EXPECT_FALSE(json_field_b.is_null());
  EXPECT_TRUE(json_field_b.IsVariantNull());

  // Array with a NULL element cast to VARIANT produces a non-null Variant Array
  // whose null element is exposed as a VariantNull.
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory->MakeArrayType(types::Int64Type(), &array_type));
  Value array_with_null =
      Value::Array(array_type, {Value::Int64(1), Value::NullInt64()});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value variant_array,
                       CastValue(array_with_null, absl::UTCTimeZone(),
                                 language_options, types::VariantType()));
  EXPECT_FALSE(variant_array.is_null());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView array_view,
                       variant_array.variant_value());
  EXPECT_TRUE(array_view.IsArray());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(VariantValueView elem1,
                       array_view.GetElementIfExists(1));
  EXPECT_FALSE(elem1.is_null());
  EXPECT_TRUE(elem1.IsVariantNull());
}

typedef testing::TestWithParam<QueryParamsWithResult> CastTemplateTest;

TEST_P(CastTemplateTest, Testlib) {
  const QueryParamsWithResult& expected = GetParam();
  ExecuteTest(expected);
}

INSTANTIATE_TEST_SUITE_P(
    CastProtoBytes, CastTemplateTest,
    testing::ValuesIn(GetProtoAndBytesCastsWithoutValidation()));

INSTANTIATE_TEST_SUITE_P(CastDateTime, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastDateTime()));

INSTANTIATE_TEST_SUITE_P(CastInterval, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastInterval()));

INSTANTIATE_TEST_SUITE_P(CastNumeric, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastNumeric()));

// TODO add tests for NUMERIC.
INSTANTIATE_TEST_SUITE_P(CastComplex, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastComplex()));

INSTANTIATE_TEST_SUITE_P(CastString, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastString()));

INSTANTIATE_TEST_SUITE_P(
    CastNumericString, CastTemplateTest,
    testing::ValuesIn(GetFunctionTestsCastNumericString()));

INSTANTIATE_TEST_SUITE_P(CastTokenList, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastTokenList()));

INSTANTIATE_TEST_SUITE_P(CastUuid, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastUuid()));

INSTANTIATE_TEST_SUITE_P(CastJson, CastTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCastJson()));

TEST(IsTypeCastableToJsonTest, TypeCastableToJson) {
  LanguageOptions options_enabled;
  options_enabled.EnableLanguageFeature(FEATURE_CAST_TO_JSON_TYPE);

  LanguageOptions options_disabled;

  TypeFactory factory;
  const Type* int64_type = types::Int64Type();
  const Type* json_type = types::JsonType();
  const Type* geography_type = types::GeographyType();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_int64_type,
                       factory.MakeArrayType(int64_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_geography_type,
                       factory.MakeArrayType(geography_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(types::StringType(), int64_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_map_type,
                       factory.MakeArrayType(map_type));

  // Feature disabled
  EXPECT_FALSE(IsTypeCastableToJson(int64_type, options_disabled));
  EXPECT_FALSE(IsTypeCastableToJson(geography_type, options_disabled));
  EXPECT_TRUE(IsTypeCastableToJson(json_type, options_disabled));

  // Feature enabled
  EXPECT_TRUE(IsTypeCastableToJson(int64_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToJson(geography_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToJson(json_type, options_enabled));

  // Cast MAP
  EXPECT_FALSE(IsTypeCastableToJson(map_type, options_enabled));

  // Cast array of supported type
  EXPECT_TRUE(IsTypeCastableToJson(array_int64_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToJson(array_geography_type, options_enabled));

  // Cast array of unsupported type
  EXPECT_FALSE(IsTypeCastableToJson(array_map_type, options_enabled));

  // Cast GRAPH_ELEMENT and GRAPH_PATH
  const GraphElementType* castable_node_type =
      test_values::MakeGraphElementType({"g"},
                                        GraphElementType::ElementKind::kNode,
                                        {{"a", int64_type}}, &factory);
  const GraphElementType* castable_edge_type =
      test_values::MakeGraphElementType({"g"},
                                        GraphElementType::ElementKind::kEdge,
                                        {{"b", geography_type}}, &factory);
  const GraphPathType* castable_path_type = test_values::MakeGraphPathType(
      castable_node_type, castable_edge_type, &factory);
  const GraphElementType* uncastable_node_type =
      test_values::MakeGraphElementType({"g"},
                                        GraphElementType::ElementKind::kNode,
                                        {{"m", map_type}}, &factory);
  const GraphPathType* uncastable_path_type = test_values::MakeGraphPathType(
      uncastable_node_type, castable_edge_type, &factory);

  // Feature disabled
  EXPECT_FALSE(IsTypeCastableToJson(castable_node_type, options_disabled));
  EXPECT_FALSE(IsTypeCastableToJson(castable_path_type, options_disabled));

  // Feature enabled
  EXPECT_TRUE(IsTypeCastableToJson(castable_node_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToJson(castable_path_type, options_enabled));
  EXPECT_FALSE(IsTypeCastableToJson(uncastable_node_type, options_enabled));
  EXPECT_FALSE(IsTypeCastableToJson(uncastable_path_type, options_enabled));

  // TODO: b/542423700 - Support casting composite types containing VARIANT
  // (e.g. ARRAY<VARIANT>, STRUCT<VARIANT>) to JSON once TO_JSON supports
  // TYPE_VARIANT. Casting a top-level VARIANT to JSON requires
  // FEATURE_VARIANT_TYPE, whereas composite types containing VARIANT (e.g.
  // ARRAY<VARIANT>, STRUCT<VARIANT>) cannot be cast to JSON even when both
  // features are enabled.
  const Type* variant_type = types::VariantType();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_variant_type,
                       factory.MakeArrayType(variant_type));
  const StructType* struct_variant_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"a", variant_type}}, &struct_variant_type));
  EXPECT_FALSE(IsTypeCastableToJson(variant_type, options_enabled));
  EXPECT_FALSE(IsTypeCastableToJson(array_variant_type, options_enabled));
  EXPECT_FALSE(IsTypeCastableToJson(struct_variant_type, options_enabled));

  LanguageOptions variant_only_options = options_disabled;
  variant_only_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  EXPECT_TRUE(IsTypeCastableToJson(variant_type, variant_only_options));
  EXPECT_FALSE(IsTypeCastableToJson(array_variant_type, variant_only_options));
  EXPECT_FALSE(IsTypeCastableToJson(struct_variant_type, variant_only_options));

  LanguageOptions variant_and_json_options = options_enabled;
  variant_and_json_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  EXPECT_TRUE(IsTypeCastableToJson(variant_type, variant_and_json_options));
  EXPECT_FALSE(
      IsTypeCastableToJson(array_variant_type, variant_and_json_options));
  EXPECT_FALSE(
      IsTypeCastableToJson(struct_variant_type, variant_and_json_options));
}

TEST(IsTypeCastableFromVariantTest, FeatureDisabled) {
  LanguageOptions options_disabled;
  TypeFactory factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  EXPECT_TRUE(
      IsTypeCastableFromVariant(types::VariantType(), options_disabled));
  EXPECT_FALSE(IsTypeCastableFromVariant(types::Int64Type(), options_disabled));
  EXPECT_FALSE(
      IsTypeCastableFromVariant(types::StringType(), options_disabled));
  EXPECT_FALSE(IsTypeCastableFromVariant(types::JsonType(), options_disabled));
  EXPECT_FALSE(IsTypeCastableFromVariant(struct_type, options_disabled));
}

TEST(IsTypeCastableFromVariantTest, SupportedTargetTypes) {
  LanguageOptions options;
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_int64_type,
                       factory.MakeArrayType(types::Int64Type()));
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}}, &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* non_str_map_type,
      factory.MakeMapType(types::Int64Type(), types::StringType()));
  const RangeType* range_date_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeRangeType(types::DateType(), &range_date_type));
  const EnumType* enum_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(googlesql_test::TestEnum_descriptor(), &enum_type));
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::NullableInt::descriptor(),
                                  &proto_type));
  const GraphElementType* graph_node_type = test_values::MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {}, &factory);
  const GraphElementType* graph_edge_type = test_values::MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge, {}, &factory);
  const GraphPathType* graph_path_type = test_values::MakeGraphPathType(
      graph_node_type, graph_edge_type, &factory);

  std::vector<const Type*> supported_types = {
      types::VariantType(),
      types::Int32Type(),
      types::Int64Type(),
      types::Uint32Type(),
      types::Uint64Type(),
      types::BoolType(),
      types::FloatType(),
      types::DoubleType(),
      types::NumericType(),
      types::BigNumericType(),
      types::StringType(),
      types::BytesType(),
      types::DateType(),
      types::TimeType(),
      types::DatetimeType(),
      types::TimestampType(),
      types::IntervalType(),
      types::JsonType(),
      types::UuidType(),
      types::TokenListType(),
      enum_type,
      proto_type,
      array_int64_type,
      struct_type,
      map_type,
      non_str_map_type,
      range_date_type,
      graph_node_type,
      graph_edge_type,
      graph_path_type,
  };
  for (const Type* to_type : supported_types) {
    EXPECT_TRUE(IsTypeCastableFromVariant(to_type, options))
        << to_type->DebugString();
  }
}

namespace {

class TestExtendedType : public ExtendedType {
 public:
  explicit TestExtendedType(const TypeFactory* factory)
      : ExtendedType(factory) {}

  Value MakeValue(int64_t v) const {
    return Value::Extended(this, ValueContent::Create(v));
  }

  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override {
    return x.GetAs<int64_t>() == y.GetAs<int64_t>();
  }

  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override {
    return x.GetAs<int64_t>() < y.GetAs<int64_t>();
  }

  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override {
    return absl::HashState::combine(std::move(state), value.GetAs<int64_t>());
  }

  std::string TypeName(ProductMode mode) const override {
    return "TestExtendedType";
  }

  absl::HashState HashTypeParameter(absl::HashState state) const override {
    return absl::HashState::combine(std::move(state),
                                    reinterpret_cast<uintptr_t>(this));
  }

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override {
    type_proto->set_type_kind(TYPE_EXTENDED);
    type_proto->set_extended_type_name(TypeName(ProductMode::PRODUCT_EXTERNAL));
    return absl::OkStatus();
  }

  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override {
    return absl::UnimplementedError("Unimplemented");
  }

  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override {
    return absl::UnimplementedError("Unimplemented");
  }

  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

  bool EqualsForSameKind(const Type* that, bool equivalent) const override {
    return this == that;
  }

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override {
    *debug_string = TypeName(ProductMode::PRODUCT_EXTERNAL);
  }

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override {
    return absl::StrCat("TestExtended(", value.GetAs<int64_t>(), ")");
  }
};

}  // namespace

TEST(IsTypeCastableFromVariantTest, UnsupportedTargetTypes) {
  LanguageOptions options;
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       factory.MakeMeasureType(types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_measure_type,
                       factory.MakeArrayType(measure_type));
  const StructType* struct_measure_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"m", measure_type}}, &struct_measure_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_measure_type,
                       factory.MakeMapType(types::StringType(), measure_type));

  EXPECT_FALSE(IsTypeCastableFromVariant(measure_type, options));
  EXPECT_FALSE(IsTypeCastableFromVariant(array_measure_type, options));
  EXPECT_FALSE(IsTypeCastableFromVariant(struct_measure_type, options));
  EXPECT_FALSE(IsTypeCastableFromVariant(map_measure_type, options));

  // TODO: b/554949919 - Support casting from VARIANT to declarative and
  // extended types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* declarative_type,
      factory.MakeDeclarativeType(DeclarativeTypeDescriptor()
                                      .set_type_id({"NS", "DeclInt"})
                                      .set_display_name("DeclInt")
                                      .set_backing_type(factory.get_int64())));
  EXPECT_FALSE(IsTypeCastableFromVariant(declarative_type, options));

  TestExtendedType extended_type(&factory);
  EXPECT_FALSE(IsTypeCastableFromVariant(&extended_type, options));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_declarative_type,
                       factory.MakeArrayType(declarative_type));
  EXPECT_FALSE(IsTypeCastableFromVariant(array_declarative_type, options));

  const StructType* struct_declarative_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"d", declarative_type}},
                                   &struct_declarative_type));
  EXPECT_FALSE(IsTypeCastableFromVariant(struct_declarative_type, options));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_extended_type,
                       factory.MakeArrayType(&extended_type));
  EXPECT_FALSE(IsTypeCastableFromVariant(array_extended_type, options));

  const StructType* struct_extended_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"e", &extended_type}}, &struct_extended_type));
  EXPECT_FALSE(IsTypeCastableFromVariant(struct_extended_type, options));
}

namespace {

template <typename T>
T Unwrap(absl::StatusOr<T> status_or) {
  GOOGLESQL_CHECK_OK(status_or);
  return *std::move(status_or);
}

Value Variant(const Value& value) { return Unwrap(Value::Variant(value)); }

Value ParseJson(absl::string_view json_str) {
  return Json(Unwrap(JSONValue::ParseJSONString(json_str)));
}

// -----------------------------------------------------------------------------
// 1. Successful CAST from VARIANT Suites (Organized by Variant Category)
// -----------------------------------------------------------------------------

struct VariantCastSuccessCase {
  std::string test_name;
  Value from_variant;
  Value expected_value;
};

class CastFromVariantSuccessTest
    : public testing::TestWithParam<VariantCastSuccessCase> {};

TEST_P(CastFromVariantSuccessTest, CastsVariantToTargetType) {
  const VariantCastSuccessCase& test_case = GetParam();
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  EXPECT_THAT(CastValue(test_case.from_variant, absl::UTCTimeZone(), options,
                        test_case.expected_value.type()),
              IsOkAndHolds(test_case.expected_value));
}

std::vector<VariantCastSuccessCase> GetNullAndIdentityCastCases() {
  Value json_null_val = Json(JSONValue());
  Value variant_null_val = Variant(json_null_val);
  const StructType* struct_ab_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}},
      &struct_ab_type));

  return {
      {.test_name = "IdentitySqlNullVariant",
       .from_variant = Value::NullVariant(),
       .expected_value = Value::NullVariant()},
      {.test_name = "IdentityVariantNull",
       .from_variant = variant_null_val,
       .expected_value = variant_null_val},
      {.test_name = "SqlNullVariantToInt64",
       .from_variant = Value::Null(types::VariantType()),
       .expected_value = Value::NullInt64()},
      {.test_name = "SqlNullVariantToJson",
       .from_variant = Value::Null(types::VariantType()),
       .expected_value = Value::NullJson()},
      {.test_name = "SqlNullVariantToStruct",
       .from_variant = Value::NullVariant(),
       .expected_value = Value::Null(struct_ab_type)},
      {.test_name = "CanonicalizedTypedSqlNullVariantToInt64",
       .from_variant = Variant(Value::NullDate()),
       .expected_value = Value::NullInt64()},
      {.test_name = "VariantNullToInt64",
       .from_variant = variant_null_val,
       .expected_value = Value::NullInt64()},
      {.test_name = "VariantNullToString",
       .from_variant = variant_null_val,
       .expected_value = Value::NullString()},
      {.test_name = "VariantNullToStruct",
       .from_variant = Value::VariantNull(),
       .expected_value = Value::Null(struct_ab_type)},
      {.test_name = "VariantNullToJsonPreservesJsonNull",
       .from_variant = variant_null_val,
       .expected_value = json_null_val},
  };
}

INSTANTIATE_TEST_SUITE_P(
    NullAndIdentity, CastFromVariantSuccessTest,
    testing::ValuesIn(GetNullAndIdentityCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetSqlPrimitiveCastCases() {
  const EnumType* enum_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                      &enum_type));

  UuidValue uuid =
      Unwrap(UuidValue::FromString("00010203-0405-0607-0809-0a0b0c0d0e0f"));
  Value uuid_val = Value::Uuid(uuid);
  std::string uuid_bytes = uuid.SerializeAsBytes();

  Value tokenlist_val = TokenListFromStringArray({"hello", "world"});
  Value tokenlist_bytes_val = Bytes(tokenlist_val.tokenlist_value().GetBytes());

  Value range_date_val = Unwrap(
      Value::MakeRange(Date(20089), Date(20090)));  // [2025-01-01, 2025-01-02)

  Value graph_node_empty = Unwrap(GraphNode(
      {"graph_name"}, "id1", {}, {"label1"}, "ElementTable", type_factory));
  Value graph_node_with_null_prop =
      Unwrap(GraphNode({"graph_name"}, "id1", {{"a", Value::NullString()}},
                       {"label1"}, "ElementTable", type_factory));
  Value graph_edge_empty =
      Unwrap(GraphEdge({"graph_name"}, "e1", {}, {"label1"}, "ElementTable",
                       "id1", "id2", type_factory));
  const GraphPathType* path_type_empty =
      test_values::MakeGraphPathType(graph_node_empty.type()->AsGraphElement(),
                                     graph_edge_empty.type()->AsGraphElement());
  const GraphPathType* path_type_with_prop = test_values::MakeGraphPathType(
      graph_node_with_null_prop.type()->AsGraphElement(),
      graph_edge_empty.type()->AsGraphElement());
  Value graph_path_empty =
      Unwrap(Value::MakeGraphPath(path_type_empty, {graph_node_empty}));
  Value graph_path_with_null_prop = Unwrap(
      Value::MakeGraphPath(path_type_with_prop, {graph_node_with_null_prop}));

  return {
      {.test_name = "Int64ToInt64",
       .from_variant = Variant(Int64(123)),
       .expected_value = Int64(123)},
      {.test_name = "Int64ToString",
       .from_variant = Variant(Int64(123)),
       .expected_value = String("123")},
      {.test_name = "StringToInt64",
       .from_variant = Variant(String("456")),
       .expected_value = Int64(456)},
      {.test_name = "BoolToString",
       .from_variant = Variant(Bool(true)),
       .expected_value = String("true")},
      {.test_name = "DoubleToInt64RoundsHalfAwayFromZero",
       .from_variant = Variant(Double(3.6)),
       .expected_value = Int64(4)},
      {.test_name = "EnumToEnum",
       .from_variant = Variant(values::Enum(enum_type, 1)),
       .expected_value = values::Enum(enum_type, 1)},
      {.test_name = "EnumToString",
       .from_variant = Variant(values::Enum(enum_type, 1)),
       .expected_value = String("TESTENUM1")},
      {.test_name = "EnumToInt64",
       .from_variant = Variant(values::Enum(enum_type, 1)),
       .expected_value = Int64(1)},
      {.test_name = "UuidToUuid",
       .from_variant = Variant(uuid_val),
       .expected_value = uuid_val},
      {.test_name = "UuidToString",
       .from_variant = Variant(uuid_val),
       .expected_value = String("00010203-0405-0607-0809-0a0b0c0d0e0f")},
      {.test_name = "UuidToBytes",
       .from_variant = Variant(uuid_val),
       .expected_value = Bytes(uuid_bytes)},
      {.test_name = "StringToUuid",
       .from_variant = Variant(String("00010203-0405-0607-0809-0a0b0c0d0e0f")),
       .expected_value = uuid_val},
      {.test_name = "BytesToUuid",
       .from_variant = Variant(Bytes(uuid_bytes)),
       .expected_value = uuid_val},
      {.test_name = "TokenListToTokenList",
       .from_variant = Variant(tokenlist_val),
       .expected_value = tokenlist_val},
      {.test_name = "TokenListToBytes",
       .from_variant = Variant(tokenlist_val),
       .expected_value = tokenlist_bytes_val},
      {.test_name = "BytesToTokenList",
       .from_variant = Variant(tokenlist_bytes_val),
       .expected_value = tokenlist_val},
      {.test_name = "RangeDateToRangeDate",
       .from_variant = Variant(range_date_val),
       .expected_value = range_date_val},
      {.test_name = "RangeDateToString",
       .from_variant = Variant(range_date_val),
       .expected_value = String("[2025-01-01, 2025-01-02)")},
      {.test_name = "StringToRangeDate",
       .from_variant = Variant(String("[2025-01-01, 2025-01-02)")),
       .expected_value = range_date_val},
      {.test_name = "GraphElementToIdenticalGraphElement",
       .from_variant = Variant(graph_node_empty),
       .expected_value = graph_node_empty},
      {.test_name = "GraphElementToSupertypeGraphElement",
       .from_variant = Variant(graph_node_empty),
       .expected_value = graph_node_with_null_prop},
      {.test_name = "GraphPathToIdenticalGraphPath",
       .from_variant = Variant(graph_path_empty),
       .expected_value = graph_path_empty},
      {.test_name = "GraphPathToSupertypeGraphPath",
       .from_variant = Variant(graph_path_empty),
       .expected_value = graph_path_with_null_prop},
  };
}

INSTANTIATE_TEST_SUITE_P(
    SqlPrimitives, CastFromVariantSuccessTest,
    testing::ValuesIn(GetSqlPrimitiveCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetJsonPrimitiveCastCases() {
  constexpr uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();
  Value json_max_uint64 = Json(JSONValue(kMaxUint64));
  // Use an exact binary floating-point fraction (3.25) so IEEE-754 double
  // converts cleanly to FLOAT, DOUBLE, NUMERIC, BIGNUMERIC, and STRING.
  Value json_double = Json(JSONValue(3.25));

  return {
      {.test_name = "JsonInt64ToInt64",
       .from_variant = Variant(Json(JSONValue(int64_t{-789}))),
       .expected_value = Int64(-789)},
      {.test_name = "JsonInt64ToString",
       .from_variant = Variant(Json(JSONValue(int64_t{-789}))),
       .expected_value = String("-789")},
      {.test_name = "JsonUint64MaxToUint64",
       .from_variant = Variant(json_max_uint64),
       .expected_value = Uint64(kMaxUint64)},
      {.test_name = "JsonUint64MaxToString",
       .from_variant = Variant(json_max_uint64),
       .expected_value = String(absl::StrCat(kMaxUint64))},
      {.test_name = "JsonDoubleToDouble",
       .from_variant = Variant(json_double),
       .expected_value = Double(3.25)},
      {.test_name = "JsonDoubleToFloat",
       .from_variant = Variant(json_double),
       .expected_value = Float(3.25f)},
      {.test_name = "JsonDoubleToNumeric",
       .from_variant = Variant(json_double),
       .expected_value =
           Value::Numeric(Unwrap(NumericValue::FromString("3.25")))},
      {.test_name = "JsonDoubleToBigNumeric",
       .from_variant = Variant(json_double),
       .expected_value =
           Value::BigNumeric(Unwrap(BigNumericValue::FromString("3.25")))},
      {.test_name = "JsonDoubleToInt64",
       .from_variant = Variant(Json(JSONValue(3.6))),
       .expected_value = Int64(4)},
      {.test_name = "JsonDoubleToString",
       .from_variant = Variant(json_double),
       .expected_value = String("3.25")},
      {.test_name = "JsonBoolTrueToBool",
       .from_variant = Variant(Json(JSONValue(true))),
       .expected_value = Bool(true)},
      {.test_name = "JsonBoolFalseToString",
       .from_variant = Variant(Json(JSONValue(false))),
       .expected_value = String("false")},
      {.test_name = "JsonBoolTrueToInt64",
       .from_variant = Variant(Json(JSONValue(true))),
       .expected_value = Int64(1)},
      {.test_name = "JsonStringToString",
       .from_variant = Variant(Json(JSONValue(absl::string_view("abc")))),
       .expected_value = String("abc")},
      {.test_name = "JsonStringToInt64",
       .from_variant = Variant(Json(JSONValue(absl::string_view("123")))),
       .expected_value = Int64(123)},
      {.test_name = "UnvalidatedJsonPrimitiveToInt64",
       .from_variant = Variant(Value::UnvalidatedJsonString("42")),
       .expected_value = Int64(42)},
  };
}

INSTANTIATE_TEST_SUITE_P(
    JsonPrimitives, CastFromVariantSuccessTest,
    testing::ValuesIn(GetJsonPrimitiveCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetObjectsAndMapsCastCases() {
  const StructType* struct_ab_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}},
      &struct_ab_type));
  const StructType* struct_two_int_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}},
      &struct_two_int_type));
  const StructType* struct_dup_and_unnamed_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"", types::Int64Type()},
                                         {"a", types::Int64Type()},
                                         {"a", types::Int64Type()},
                                         {"", types::Int64Type()}},
                                        &struct_dup_and_unnamed_type));

  const Type* map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64Type()));
  const Type* map_str_str_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::StringType()));
  const Type* map_str_variant_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::VariantType()));
  const Type* map_int64_str_type = Unwrap(
      type_factory->MakeMapType(types::Int64Type(), types::StringType()));
  const Type* map_str_int64_arr_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64ArrayType()));
  const Type* map_str_json_type =
      Unwrap(type_factory->MakeMapType(types::StringType(), types::JsonType()));
  const Type* map_str_map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), map_str_int64_type));

  Value struct_ab_val =
      Unwrap(Value::MakeStruct(struct_ab_type, {Int64(42), String("hello")}));
  Value struct_two_int_val =
      Unwrap(Value::MakeStruct(struct_two_int_type, {Int64(1), Int64(2)}));
  Value struct_dup_and_unnamed_val =
      Unwrap(Value::MakeStruct(struct_dup_and_unnamed_type,
                               {Int64(10), Int64(20), Int64(99), Int64(88)}));
  Value json_null_val = Json(JSONValue());
  Value variant_null_val = Variant(json_null_val);

  Value map_ab_val = Unwrap(Value::MakeMap(
      map_str_int64_type, {{String("a"), Int64(1)}, {String("b"), Int64(2)}}));
  Value map_ab_str_val =
      Unwrap(Value::MakeMap(map_str_str_type, {{String("a"), String("1")},
                                               {String("b"), String("2")}}));

  return {
      {.test_name = "StructObjectToMap",
       .from_variant = Variant(struct_two_int_val),
       .expected_value = map_ab_val},
      {.test_name = "StructWithDuplicateAndUnnamedFieldsToMap",
       .from_variant = Variant(struct_dup_and_unnamed_val),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{String(""), Int64(10)}, {String("a"), Int64(20)}}))},
      {.test_name = "StructObjectToMapWithVariantValue",
       .from_variant = Variant(struct_ab_val),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_variant_type, {{String("a"), Variant(Int64(42))},
                                  {String("b"), Variant(String("hello"))}}))},
      {.test_name = "MapObjectToMapWithValueConversion",
       .from_variant = Variant(map_ab_val),
       .expected_value = map_ab_str_val},
      {.test_name = "MapObjectWithNullValueToMap",
       .from_variant = Variant(Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{String("a"), Int64(1)}, {String("b"), Value::NullInt64()}}))),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{String("a"), Int64(1)}, {String("b"), Value::NullInt64()}}))},
      // A string-keyed MAP is exposed as a Variant Object, which omits NULL
      // keys per Variant Object semantics.
      {.test_name = "MapObjectWithNullKeyOmitsNullKeyEntry",
       .from_variant = Variant(Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{Value::NullString(), Int64(99)}, {String("a"), Int64(1)}}))),
       .expected_value = Unwrap(
           Value::MakeMap(map_str_int64_type, {{String("a"), Int64(1)}}))},
      {.test_name = "JsonObjectToMap",
       .from_variant = Variant(ParseJson(R"({"a": 1, "b": 2})")),
       .expected_value = map_ab_val},
      {.test_name = "JsonObjectWithArrayValuesToMapOfArrays",
       .from_variant = Variant(ParseJson(R"({"a": [1, 2], "b": [3]})")),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_arr_type, {{String("a"), Int64Array({1, 2})},
                                    {String("b"), Int64Array({3})}}))},
      {.test_name = "NestedJsonObjectToNestedMap",
       .from_variant = Variant(ParseJson(R"({"outer": {"inner": 42}})")),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_map_str_int64_type,
           {{String("outer"),
             Unwrap(Value::MakeMap(map_str_int64_type,
                                   {{String("inner"), Int64(42)}}))}}))},
      {.test_name = "JsonObjectWithNullToTypedMap",
       .from_variant = Variant(ParseJson(R"({"a": null, "b": 2})")),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{String("a"), Value::NullInt64()}, {String("b"), Int64(2)}}))},
      {.test_name = "JsonObjectWithNullToMapWithJsonValue",
       .from_variant = Variant(ParseJson(R"({"a": 1, "b": null})")),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_json_type, {{String("a"), Json(JSONValue(int64_t{1}))},
                               {String("b"), json_null_val}}))},
      {.test_name = "JsonObjectToMapWithVariantValue",
       .from_variant = Variant(ParseJson(R"({"a": 42, "b": "hello"})")),
       .expected_value = Unwrap(
           Value::MakeMap(map_str_variant_type,
                          {{String("a"), Variant(ParseJson("42"))},
                           {String("b"), Variant(ParseJson(R"("hello")"))}}))},
      {.test_name = "JsonObjectWithNullToMapWithVariantValue",
       .from_variant = Variant(ParseJson(R"({"a": null, "b": "hello"})")),
       .expected_value = Unwrap(
           Value::MakeMap(map_str_variant_type,
                          {{String("a"), variant_null_val},
                           {String("b"), Variant(ParseJson(R"("hello")"))}}))},
      {.test_name = "EmptyJsonObjectToMap",
       .from_variant = Variant(ParseJson("{}")),
       .expected_value = Unwrap(Value::MakeMap(map_str_int64_type, {}))},
      // A non-string-keyed MAP is not a Variant Object and passes through to
      // MAP-to-MAP casting, preserving both NULL keys and NULL values.
      {.test_name = "NonStringKeyedMapWithNullKeyAndValuePassThroughToMap",
       .from_variant = Variant(Unwrap(Value::MakeMap(
           map_int64_str_type, {{Value::NullInt64(), String("10")},
                                {Int64(1), Value::NullString()},
                                {Int64(2), String("20")}}))),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_type, {{Value::NullString(), Int64(10)},
                                {String("1"), Value::NullInt64()},
                                {String("2"), Int64(20)}}))},
  };
}

INSTANTIATE_TEST_SUITE_P(
    ObjectsAndMaps, CastFromVariantSuccessTest,
    testing::ValuesIn(GetObjectsAndMapsCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetProtoCastCases() {
  const ProtoType* nullable_int_proto_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::NullableInt::descriptor(), &nullable_int_proto_type));
  googlesql_test::NullableInt nullable_int;
  nullable_int.set_value(123);
  absl::Cord proto_cord;
  nullable_int.SerializeToString(&proto_cord);
  Value proto_val = values::Proto(nullable_int_proto_type, proto_cord);
  Value unset_proto_val = values::Proto(nullable_int_proto_type, absl::Cord());

  const ProtoType* repeated_int_proto_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::NullableArrayOfInt::descriptor(),
      &repeated_int_proto_type));
  googlesql_test::NullableArrayOfInt repeated_int;
  repeated_int.add_value(10);
  repeated_int.add_value(20);
  absl::Cord repeated_cord;
  repeated_int.SerializeToString(&repeated_cord);
  Value repeated_proto_val =
      values::Proto(repeated_int_proto_type, repeated_cord);

  const StructType* struct_proto_field_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"value", types::Int64Type()}},
                                        &struct_proto_field_type));
  const Type* map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64Type()));
  const Type* map_str_int64_arr_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64ArrayType()));
  const Type* map_str_variant_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::VariantType()));

  return {
      {.test_name = "ProtoObjectToProto",
       .from_variant = Variant(proto_val),
       .expected_value = proto_val},
      {.test_name = "ProtoObjectToString",
       .from_variant = Variant(proto_val),
       .expected_value = String("value: 123")},
      {.test_name = "ProtoObjectToBytes",
       .from_variant = Variant(proto_val),
       .expected_value = Bytes(std::string(proto_cord))},
      {.test_name = "ProtoObjectToJson",
       .from_variant = Variant(proto_val),
       .expected_value = ParseJson(R"({"value":123})")},
      {.test_name = "UnsetProtoObjectToJson",
       .from_variant = Variant(unset_proto_val),
       .expected_value = ParseJson(R"({"value":null})")},
      {.test_name = "ProtoObjectToStruct",
       .from_variant = Variant(proto_val),
       .expected_value =
           Unwrap(Value::MakeStruct(struct_proto_field_type, {Int64(123)}))},
      {.test_name = "UnsetProtoObjectToStruct",
       .from_variant = Variant(unset_proto_val),
       .expected_value = Unwrap(
           Value::MakeStruct(struct_proto_field_type, {Value::NullInt64()}))},
      {.test_name = "ProtoObjectToMap",
       .from_variant = Variant(proto_val),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_type, {{String("value"), Int64(123)}}))},
      {.test_name = "UnsetProtoObjectToMap",
       .from_variant = Variant(unset_proto_val),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_type, {{String("value"), Value::NullInt64()}}))},
      {.test_name = "RepeatedFieldProtoObjectToJson",
       .from_variant = Variant(repeated_proto_val),
       .expected_value = ParseJson(R"({"value":[10,20]})")},
      {.test_name = "RepeatedFieldProtoObjectToTypedMap",
       .from_variant = Variant(repeated_proto_val),
       .expected_value = Unwrap(Value::MakeMap(
           map_str_int64_arr_type, {{String("value"), Int64Array({10, 20})}}))},
      {.test_name = "RepeatedFieldProtoObjectToVariantMap",
       .from_variant = Variant(repeated_proto_val),
       .expected_value = Unwrap(
           Value::MakeMap(map_str_variant_type,
                          {{String("value"), Variant(Int32Array({10, 20}))}}))},
      {.test_name = "StringTextFormatToProto",
       .from_variant = Variant(String("value: 123")),
       .expected_value = proto_val},
      {.test_name = "BytesWireFormatToProto",
       .from_variant = Variant(Bytes(std::string(proto_cord))),
       .expected_value = proto_val},
  };
}

INSTANTIATE_TEST_SUITE_P(
    Protos, CastFromVariantSuccessTest, testing::ValuesIn(GetProtoCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetArrayCastCases() {
  Value json_null_val = Json(JSONValue());
  Value variant_null_val = Variant(json_null_val);
  Value heterogeneous_json_arr = ParseJson(R"([1, "2", true])");
  Value json_arr_with_null_val = ParseJson("[1, null, 3]");
  const Type* map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64Type()));
  const ArrayType* arr_of_map_str_int64_type =
      Unwrap(type_factory->MakeArrayType(map_str_int64_type));

  return {
      {.test_name = "SqlArrayToArrayWithElementConversion",
       .from_variant = Variant(StringArray({"10", "20"})),
       .expected_value = Int64Array({10, 20})},
      {.test_name = "JsonArrayToArray",
       .from_variant = Variant(ParseJson("[1, 2, 3]")),
       .expected_value = Int64Array({1, 2, 3})},
      {.test_name = "JsonArrayOfObjectsToArrayOfMaps",
       .from_variant = Variant(ParseJson(R"([{"a": 1}, {"b": 2}])")),
       .expected_value = Unwrap(Value::MakeArray(
           arr_of_map_str_int64_type,
           {Unwrap(
                Value::MakeMap(map_str_int64_type, {{String("a"), Int64(1)}})),
            Unwrap(Value::MakeMap(map_str_int64_type,
                                  {{String("b"), Int64(2)}}))}))},
      {.test_name = "HeterogeneousJsonArrayToInt64Array",
       .from_variant = Variant(heterogeneous_json_arr),
       .expected_value = Int64Array({1, 2, 1})},
      {.test_name = "HeterogeneousJsonArrayToStringArray",
       .from_variant = Variant(heterogeneous_json_arr),
       .expected_value = StringArray({"1", "2", "true"})},
      {.test_name = "JsonArrayWithNullToInt64Array",
       .from_variant = Variant(json_arr_with_null_val),
       .expected_value = Unwrap(Value::MakeArray(
           types::Int64ArrayType(), {Int64(1), Value::NullInt64(), Int64(3)}))},
      {.test_name = "JsonArrayWithNullToJsonArray",
       .from_variant = Variant(json_arr_with_null_val),
       .expected_value = Unwrap(Value::MakeArray(
           types::JsonArrayType(), {Json(JSONValue(int64_t{1})), json_null_val,
                                    Json(JSONValue(int64_t{3}))}))},
      {.test_name = "NestedJsonArrayWithNullToJsonArray",
       .from_variant = Variant(ParseJson("[[1, null], null]")),
       .expected_value = Unwrap(Value::MakeArray(
           types::JsonArrayType(), {ParseJson("[1, null]"), json_null_val}))},
      {.test_name = "ArrayToVariantArray",
       .from_variant = Variant(Int64Array({1, 2})),
       .expected_value = Unwrap(Value::MakeArray(
           types::VariantArrayType(), {Variant(Int64(1)), Variant(Int64(2))}))},
      {.test_name = "VariantArrayWithSqlNullCanonicalizesToVariantNullElement",
       .from_variant = Variant(Unwrap(Value::MakeArray(
           types::VariantArrayType(),
           {Variant(Int64(1)), Value::Null(types::VariantType()),
            Variant(Int64(3))}))),
       .expected_value = Unwrap(Value::MakeArray(
           types::VariantArrayType(),
           {Variant(Int64(1)), variant_null_val, Variant(Int64(3))}))},
      {.test_name = "EmptyJsonArrayToArray",
       .from_variant = Variant(ParseJson("[]")),
       .expected_value = Unwrap(Value::MakeArray(types::Int64ArrayType(), {}))},
  };
}

INSTANTIATE_TEST_SUITE_P(
    Arrays, CastFromVariantSuccessTest, testing::ValuesIn(GetArrayCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetToJsonCastCases() {
  Value json_null_val = Json(JSONValue());
  Value variant_null_val = Variant(json_null_val);

  const StructType* struct_ab_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}},
      &struct_ab_type));
  const StructType* struct_variant_field_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::VariantType()}, {"b", types::StringType()}},
      &struct_variant_field_type));
  const StructType* struct_timestamp_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"ts", types::TimestampType()}},
                                        &struct_timestamp_type));
  const StructType* struct_dup_unnamed_and_case_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"", types::Int64Type()},
                                         {"a", types::Int64Type()},
                                         {"a", types::Int64Type()},
                                         {"A", types::Int64Type()},
                                         {"", types::Int64Type()}},
                                        &struct_dup_unnamed_and_case_type));

  const Type* map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64Type()));
  const Type* map_str_variant_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::VariantType()));
  const EnumType* enum_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                      &enum_type));

  absl::Time test_timestamp = absl::FromCivil(
      absl::CivilSecond(2026, 10, 6, 12, 34, 56), absl::UTCTimeZone());
  UuidValue uuid =
      Unwrap(UuidValue::FromString("00010203-0405-0607-0809-0a0b0c0d0e0f"));
  Value range_date_val = Unwrap(
      Value::MakeRange(Date(20089), Date(20090)));  // [2025-01-01, 2025-01-02)
  Value graph_node_val =
      Unwrap(GraphNode({"g"}, "n1", {{"a", Value::Int64(1)}}, {"L1"}, "T"));
  Value graph_edge_val = Unwrap(GraphEdge({"g"}, "e1", {{"a", Value::Int64(1)}},
                                          {"L1"}, "T", "n1", "n2"));
  Value graph_node2_val =
      Unwrap(GraphNode({"g"}, "n2", {{"a", Value::Int64(1)}}, {"L1"}, "T"));
  Value graph_path_val = Unwrap(Value::MakeGraphPath(
      test_values::MakeGraphPathType(graph_node_val.type()->AsGraphElement(),
                                     graph_edge_val.type()->AsGraphElement()),
      {graph_node_val, graph_edge_val, graph_node2_val}));

  return {
      {.test_name = "PrimitiveInt64ToJson",
       .from_variant = Variant(Int64(42)),
       .expected_value = Json(JSONValue(int64_t{42}))},
      {.test_name = "PrimitiveEnumToJson",
       .from_variant = Variant(Value::Enum(enum_type, 1)),
       .expected_value = Json(JSONValue(absl::string_view("TESTENUM1")))},
      {.test_name = "PrimitiveTimestampToJson",
       .from_variant = Variant(Value::Timestamp(test_timestamp)),
       .expected_value = ParseJson(R"("2026-10-06T12:34:56Z")")},
      {.test_name = "PrimitiveUuidToJson",
       .from_variant = Variant(Value::Uuid(uuid)),
       .expected_value =
           ParseJson(R"("00010203-0405-0607-0809-0a0b0c0d0e0f")")},
      {.test_name = "PrimitiveRangeDateToJson",
       .from_variant = Variant(range_date_val),
       .expected_value =
           ParseJson(R"({"start":"2025-01-01","end":"2025-01-02"})")},
      {.test_name = "GraphNodeToJson",
       .from_variant = Variant(graph_node_val),
       .expected_value = ParseJson(
           R"({"identifier":"bjE=","kind":"node","labels":["L1"],"properties":{"a":1}})")},
      {.test_name = "GraphPathToJson",
       .from_variant = Variant(graph_path_val),
       .expected_value = ParseJson(
           R"([{"identifier":"bjE=","kind":"node","labels":["L1"],"properties":{"a":1}},{"destination_node_identifier":"bjI=","identifier":"ZTE=","kind":"edge","labels":["L1"],"properties":{"a":1},"source_node_identifier":"bjE="},{"identifier":"bjI=","kind":"node","labels":["L1"],"properties":{"a":1}}])")},
      {.test_name = "StructObjectToJson",
       .from_variant = Variant(Unwrap(
           Value::MakeStruct(struct_ab_type, {Int64(42), String("hello")}))),
       .expected_value = ParseJson(R"({"a": 42, "b": "hello"})")},
      {.test_name = "StructWithDuplicateUnnamedAndCaseDistinctFieldsToJson",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_dup_unnamed_and_case_type,
           {Int64(10), Int64(20), Int64(99), Int64(30), Int64(88)}))),
       .expected_value = ParseJson(R"({"": 10, "a": 20, "A": 30})")},
      {.test_name = "StructObjectWithNullFieldToJson",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_ab_type, {Value::NullInt64(), String("hello")}))),
       .expected_value = ParseJson(R"({"a": null, "b": "hello"})")},
      {.test_name = "StructObjectWithNullVariantFieldToJson",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_variant_field_type,
           {Value::Null(types::VariantType()), String("hello")}))),
       .expected_value = ParseJson(R"({"a": null, "b": "hello"})")},
      {.test_name = "StructWithTimestampObjectToJson",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_timestamp_type, {Value::Timestamp(test_timestamp)}))),
       .expected_value = ParseJson(R"({"ts":"2026-10-06T12:34:56Z"})")},
      {.test_name = "MapObjectToJson",
       .from_variant = Variant(Unwrap(
           Value::MakeMap(map_str_int64_type,
                          {{String("a"), Int64(1)}, {String("b"), Int64(2)}}))),
       .expected_value = ParseJson(R"({"a": 1, "b": 2})")},
      {.test_name = "MapObjectWithNullKeyToJsonOmitsNullKeyEntry",
       .from_variant = Variant(Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{Value::NullString(), Int64(99)}, {String("a"), Int64(1)}}))),
       .expected_value = ParseJson(R"({"a": 1})")},
      {.test_name = "MapObjectWithNullValueToJson",
       .from_variant = Variant(Unwrap(Value::MakeMap(
           map_str_int64_type,
           {{String("a"), Int64(1)}, {String("b"), Value::NullInt64()}}))),
       .expected_value = ParseJson(R"({"a": 1, "b": null})")},
      {.test_name = "MapObjectWithNullVariantValueToJson",
       .from_variant = Variant(Unwrap(
           Value::MakeMap(map_str_variant_type,
                          {{String("a"), Variant(Int64(1))},
                           {String("b"), Value::Null(types::VariantType())}}))),
       .expected_value = ParseJson(R"({"a": 1, "b": null})")},
      {.test_name = "ArrayToJson",
       .from_variant = Variant(Int64Array({1, 2, 3})),
       .expected_value = ParseJson("[1, 2, 3]")},
      {.test_name = "ArrayWithNullToJson",
       .from_variant = Variant(Unwrap(Value::MakeArray(
           types::Int64ArrayType(), {Int64(1), Value::NullInt64(), Int64(3)}))),
       .expected_value = ParseJson("[1, null, 3]")},
      {.test_name = "VariantArrayWithNullToJson",
       .from_variant = Variant(Unwrap(Value::MakeArray(
           types::VariantArrayType(),
           {Variant(Int64(1)), Value::Null(types::VariantType()),
            Variant(Int64(3))}))),
       .expected_value = ParseJson("[1, null, 3]")},
      {.test_name = "JsonArrayToJson",
       .from_variant = Variant(ParseJson("[1, 2, 3]")),
       .expected_value = ParseJson("[1, 2, 3]")},
      {.test_name = "UnvalidatedJsonObjectToJson",
       .from_variant = Variant(Value::UnvalidatedJsonString(R"({"a": 1})")),
       .expected_value = ParseJson(R"({"a": 1})")},
      {.test_name = "NestedJsonObjectToJson",
       .from_variant = Variant(ParseJson(R"({"a":{"b":{"c":123}}})")),
       .expected_value = ParseJson(R"({"a":{"b":{"c":123}}})")},
      {.test_name = "NestedJsonArrayToJson",
       .from_variant = Variant(ParseJson(R"([[1, 2], [3, 4]])")),
       .expected_value = ParseJson(R"([[1, 2], [3, 4]])")},
      {.test_name = "NestedHeterogeneousVariantToJson",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_variant_field_type,
           {Variant(Unwrap(Value::MakeArray(
                types::VariantArrayType(),
                {Variant(Unwrap(Value::MakeMap(
                     map_str_variant_type, {{String("x"), Variant(Int64(1))},
                                            {String("y"), variant_null_val}}))),
                 Variant(ParseJson(R"({"z": [true, "ok"]})"))}))),
            String("root")}))),
       .expected_value = ParseJson(
           R"({"a":[{"x":1,"y":null},{"z":[true,"ok"]}],"b":"root"})")},
  };
}

INSTANTIATE_TEST_SUITE_P(
    ToJson, CastFromVariantSuccessTest, testing::ValuesIn(GetToJsonCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastSuccessCase> GetStructCastCases() {
  const StructType* struct_ab_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}},
      &struct_ab_type));
  const StructType* struct_ba_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"b", types::StringType()}, {"a", types::Int64Type()}},
      &struct_ba_type));
  const StructType* struct_upper_ab_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"A", types::Int64Type()}, {"B", types::StringType()}},
      &struct_upper_ab_type));
  const StructType* struct_json_field_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", types::JsonType()}},
                                        &struct_json_field_type));
  const StructType* struct_variant_field_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::VariantType()}, {"b", types::StringType()}},
      &struct_variant_field_type));
  const StructType* struct_two_int_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}},
      &struct_two_int_type));
  const StructType* struct_a_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", types::Int64Type()}},
                                        &struct_a_type));
  const StructType* struct_b_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"b", types::Int64Type()}},
                                        &struct_b_type));
  const StructType* struct_x_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"x", types::Int64Type()}},
                                        &struct_x_type));
  const StructType* struct_a_nested_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", struct_x_type}},
                                        &struct_a_nested_type));
  const ArrayType* struct_a_array_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(struct_a_type, &struct_a_array_type));
  const StructType* struct_arr_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"arr", types::Int64ArrayType()}},
                                        &struct_arr_type));
  const StructType* struct_dup_source_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"a", types::Int64Type()}},
      &struct_dup_source_type));
  const StructType* struct_unnamed_source_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"", types::Int64Type()}, {"b", types::Int64Type()}},
      &struct_unnamed_source_type));
  const StructType* empty_struct_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({}, &empty_struct_type));

  const Type* map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64Type()));

  Value struct_ab_val =
      Unwrap(Value::MakeStruct(struct_ab_type, {Int64(42), String("hello")}));
  Value struct_ba_val =
      Unwrap(Value::MakeStruct(struct_ba_type, {String("hello"), Int64(42)}));
  Value struct_upper_ab_val = Unwrap(
      Value::MakeStruct(struct_upper_ab_type, {Int64(42), String("hello")}));
  Value struct_two_int_val =
      Unwrap(Value::MakeStruct(struct_two_int_type, {Int64(1), Int64(2)}));
  Value map_ab_val = Unwrap(Value::MakeMap(
      map_str_int64_type, {{String("a"), Int64(1)}, {String("b"), Int64(2)}}));

  return {
      {.test_name = "StructObjectToStructSameOrder",
       .from_variant = Variant(struct_ab_val),
       .expected_value = struct_ab_val},
      {.test_name = "StructObjectToStructReorderedFields",
       .from_variant = Variant(struct_ab_val),
       .expected_value = struct_ba_val},
      {.test_name = "StructObjectToStructCaseInsensitiveFields",
       .from_variant = Variant(struct_ab_val),
       .expected_value = struct_upper_ab_val},
      {.test_name = "StructObjectToStructWithVariantField",
       .from_variant = Variant(struct_ab_val),
       .expected_value = Unwrap(Value::MakeStruct(
           struct_variant_field_type, {Variant(Int64(42)), String("hello")}))},
      {.test_name = "MapObjectToStruct",
       .from_variant = Variant(map_ab_val),
       .expected_value = struct_two_int_val},
      {.test_name = "JsonObjectToStruct",
       .from_variant = Variant(ParseJson(R"({"a": 42, "b": "hello"})")),
       .expected_value = struct_ab_val},
      {.test_name = "JsonObjectWithNullFieldToStruct",
       .from_variant = Variant(ParseJson(R"({"a": null, "b": "hello"})")),
       .expected_value = Unwrap(Value::MakeStruct(
           struct_ab_type, {Value::NullInt64(), String("hello")}))},
      {.test_name = "JsonObjectWithNullFieldToStructWithJsonField",
       .from_variant = Variant(ParseJson(R"({"a": null})")),
       .expected_value = Unwrap(
           Value::MakeStruct(struct_json_field_type, {Json(JSONValue())}))},
      {.test_name = "StructObjectWithNullFieldToStruct",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_ab_type, {Value::NullInt64(), String("hello")}))),
       .expected_value = Unwrap(Value::MakeStruct(
           struct_ab_type, {Value::NullInt64(), String("hello")}))},
      {.test_name = "NestedJsonObjectToNestedStruct",
       .from_variant = Variant(ParseJson(R"({"a": {"x": 10}})")),
       .expected_value = Unwrap(Value::MakeStruct(
           struct_a_nested_type,
           {Unwrap(Value::MakeStruct(struct_x_type, {Int64(10)}))}))},
      {.test_name = "JsonArrayOfObjectsToStructArray",
       .from_variant = Variant(ParseJson(R"([{"a": 1}, {"a": 2}])")),
       .expected_value = Unwrap(Value::MakeArray(
           struct_a_array_type,
           {Unwrap(Value::MakeStruct(struct_a_type, {Int64(1)})),
            Unwrap(Value::MakeStruct(struct_a_type, {Int64(2)}))}))},
      {.test_name = "JsonObjectWithArrayToStructWithArrayField",
       .from_variant = Variant(ParseJson(R"({"arr": [1, 2, 3]})")),
       .expected_value =
           Unwrap(Value::MakeStruct(struct_arr_type, {Int64Array({1, 2, 3})}))},
      {.test_name = "EmptyJsonObjectToStruct",
       .from_variant = Variant(ParseJson("{}")),
       .expected_value =
           Unwrap(Value::MakeStruct(struct_a_type, {Value::NullInt64()}))},
      {.test_name = "StructObjectWithDuplicateFieldsToStruct",
       .from_variant = Variant(Unwrap(
           Value::MakeStruct(struct_dup_source_type, {Int64(1), Int64(2)}))),
       .expected_value = Unwrap(Value::MakeStruct(struct_a_type, {Int64(1)}))},
      {.test_name = "StructObjectWithUnnamedFieldToStruct",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_unnamed_source_type, {Int64(10), Int64(20)}))),
       .expected_value = Unwrap(Value::MakeStruct(struct_b_type, {Int64(20)}))},
      {.test_name = "StructObjectWithUnnamedFieldToUnnamedTargetField",
       .from_variant = Variant(Unwrap(Value::MakeStruct(
           struct_unnamed_source_type, {Int64(10), Int64(20)}))),
       .expected_value = Unwrap(Value::MakeStruct(struct_unnamed_source_type,
                                                  {Int64(10), Int64(20)}))},
      {.test_name = "JsonObjectWithEmptyKeyToUnnamedStructField",
       .from_variant = Variant(ParseJson(R"({"": 10, "b": 20})")),
       .expected_value = Unwrap(Value::MakeStruct(struct_unnamed_source_type,
                                                  {Int64(10), Int64(20)}))},
      {.test_name = "EmptyJsonObjectToEmptyStruct",
       .from_variant = Variant(ParseJson("{}")),
       .expected_value = Unwrap(Value::MakeStruct(empty_struct_type, {}))},
      {.test_name = "ExtraKeysToEmptyStruct",
       .from_variant = Variant(ParseJson(R"({"a": 1})")),
       .expected_value = Unwrap(Value::MakeStruct(empty_struct_type, {}))},
      {.test_name = "MissingFieldEvaluatesToNull",
       .from_variant = Variant(ParseJson(R"({"a": 1})")),
       .expected_value = Unwrap(Value::MakeStruct(
           struct_two_int_type, {Int64(1), Value::NullInt64()}))},
      {.test_name = "ExtraKeyIgnored",
       .from_variant = Variant(ParseJson(R"({"a": 1, "c": 2})")),
       .expected_value = Unwrap(Value::MakeStruct(struct_a_type, {Int64(1)}))},
      {.test_name = "UnaskedCollidingKeysIgnored",
       .from_variant = Variant(ParseJson(R"({"x": 1, "X": 2, "a": 10})")),
       .expected_value = Unwrap(Value::MakeStruct(struct_a_type, {Int64(10)}))},
  };
}

INSTANTIATE_TEST_SUITE_P(
    Structs, CastFromVariantSuccessTest,
    testing::ValuesIn(GetStructCastCases()),
    [](const testing::TestParamInfo<VariantCastSuccessCase>& info) {
      return info.param.test_name;
    });

// -----------------------------------------------------------------------------
// 2. Runtime Evaluation Error Suites (All Return kOutOfRange for SAFE_CAST)
// -----------------------------------------------------------------------------

struct VariantCastErrorCase {
  std::string test_name;
  Value from_variant;
  const Type* to_type;
  std::string expected_error_substr;
};

class CastFromVariantEvalErrorTest
    : public testing::TestWithParam<VariantCastErrorCase> {};

TEST_P(CastFromVariantEvalErrorTest, ReturnsOutOfRangeOnPayloadMismatch) {
  const VariantCastErrorCase& test_case = GetParam();
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  EXPECT_THAT(CastValue(test_case.from_variant, absl::UTCTimeZone(), options,
                        test_case.to_type),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr(test_case.expected_error_substr)));
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  test_case.from_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  test_case.to_type, /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/false),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr(test_case.expected_error_substr)));
}

std::vector<VariantCastErrorCase> GetPrimitivesAndPassThroughErrorCases() {
  const Type* map_int64_int64_type =
      Unwrap(type_factory->MakeMapType(types::Int64Type(), types::Int64Type()));
  const Type* map_uint64_int64_type = Unwrap(
      type_factory->MakeMapType(types::Uint64Type(), types::Int64Type()));
  const Type* map_int64_str_type = Unwrap(
      type_factory->MakeMapType(types::Int64Type(), types::StringType()));
  const RangeType* range_date_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeRangeType(types::DateType(), &range_date_type));
  const RangeType* range_timestamp_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeRangeType(types::TimestampType(),
                                       &range_timestamp_type));

  Value non_str_map_neg_key =
      Unwrap(Value::MakeMap(map_int64_int64_type, {{Int64(-1), Int64(10)}}));
  Value non_str_map_bad_val =
      Unwrap(Value::MakeMap(map_int64_str_type, {{Int64(1), String("bad")}}));
  Value tokenlist_val = TokenListFromStringArray({"hello"});
  Value range_date_val = Unwrap(Value::MakeRange(Date(10), Date(20)));

  return {
      {.test_name = "InvalidStringToInt64",
       .from_variant = Variant(String("not_an_int")),
       .to_type = types::Int64Type(),
       .expected_error_substr = "Bad int64 value"},
      {.test_name = "IncompatibleInt64ToDate",
       .from_variant = Variant(Int64(123)),
       .to_type = types::DateType(),
       .expected_error_substr = "Unsupported cast from INT64 to DATE"},
      {.test_name = "JsonUint64MaxToInt64Overflow",
       .from_variant =
           Variant(Json(JSONValue(std::numeric_limits<uint64_t>::max()))),
       .to_type = types::Int64Type(),
       .expected_error_substr = "int64 out of range"},
      {.test_name = "JsonStringFloatToInt64Fails",
       .from_variant = Variant(ParseJson(R"("3.6")")),
       .to_type = types::Int64Type(),
       .expected_error_substr = "Bad int64 value"},
      {.test_name = "MalformedUnvalidatedJsonToInt64",
       .from_variant = Variant(Value::UnvalidatedJsonString("{bad")),
       .to_type = types::Int64Type(),
       .expected_error_substr = "syntax error while parsing"},
      {.test_name = "StringToArray",
       .from_variant = Variant(String("[1, 2]")),
       .to_type = types::Int64ArrayType(),
       .expected_error_substr = "Unsupported cast from STRING to ARRAY"},
      {.test_name = "InvalidStringToUuid",
       .from_variant = Variant(String("not-a-uuid")),
       .to_type = types::UuidType(),
       .expected_error_substr = "UUID"},
      {.test_name = "InvalidBytesLengthToUuid",
       .from_variant = Variant(Bytes("short")),
       .to_type = types::UuidType(),
       .expected_error_substr = "16 bytes"},
      {.test_name = "TokenListToString",
       .from_variant = Variant(tokenlist_val),
       .to_type = types::StringType(),
       .expected_error_substr = "Unsupported cast from TOKENLIST to STRING"},
      {.test_name = "TokenListToJson",
       .from_variant = Variant(tokenlist_val),
       .to_type = types::JsonType(),
       .expected_error_substr = "Unsupported cast from TOKENLIST to JSON"},
      {.test_name = "MalformedUnvalidatedJsonToJsonFails",
       .from_variant = Variant(Value::UnvalidatedJsonString("{bad")),
       .to_type = types::JsonType(),
       .expected_error_substr = "syntax error while parsing"},
      {.test_name = "NestedNonStringKeyedMapInVariantToJsonFails",
       .from_variant = Variant(Unwrap(Value::MakeArray(
           types::VariantArrayType(),
           {Variant(Unwrap(Value::MakeMap(map_int64_int64_type,
                                          {{Int64(1), Int64(10)}})))}))),
       .to_type = types::JsonType(),
       .expected_error_substr =
           "Unsupported cast from MAP<INT64, INT64> to JSON"},
      {.test_name = "GraphNodeWithUnsupportedPropertyTypeToJsonFails",
       .from_variant = Variant(Unwrap(
           GraphNode({"graph_name"}, "id1", {{"tl", values::NullTokenList()}},
                     {"label1"}, "ElementTable", type_factory))),
       .to_type = types::JsonType(),
       .expected_error_substr = "Unsupported cast from GRAPH_NODE"},
      {.test_name = "NanFloatInVariantToJsonFails",
       .from_variant =
           Variant(Double(std::numeric_limits<double>::quiet_NaN())),
       .to_type = types::JsonType(),
       .expected_error_substr = "NaN is not a valid JSON number"},
      {.test_name = "PositiveInfinityFloatInVariantToJsonFails",
       .from_variant = Variant(Double(std::numeric_limits<double>::infinity())),
       .to_type = types::JsonType(),
       .expected_error_substr = "Infinity is not a valid JSON number"},
      {.test_name = "NegativeInfinityFloatInVariantToJsonFails",
       .from_variant =
           Variant(Double(-std::numeric_limits<double>::infinity())),
       .to_type = types::JsonType(),
       .expected_error_substr = "-Infinity is not a valid JSON number"},
      {.test_name = "NestedNanInVariantObjectToJsonFails",
       .from_variant = Variant(
           Struct({"a"}, {Double(std::numeric_limits<double>::quiet_NaN())})),
       .to_type = types::JsonType(),
       .expected_error_substr = "NaN is not a valid JSON number"},
      {.test_name = "NestedInfinityInVariantArrayToJsonFails",
       .from_variant =
           Variant(DoubleArray({std::numeric_limits<double>::infinity()})),
       .to_type = types::JsonType(),
       .expected_error_substr = "Infinity is not a valid JSON number"},
      {.test_name = "RangeDateToRangeTimestamp",
       .from_variant = Variant(range_date_val),
       .to_type = range_timestamp_type,
       .expected_error_substr =
           "Unsupported cast from RANGE<DATE> to RANGE<TIMESTAMP>"},
      {.test_name = "RangeDateToInt64",
       .from_variant = Variant(range_date_val),
       .to_type = types::Int64Type(),
       .expected_error_substr = "Unsupported cast from RANGE<DATE> to INT64"},
      {.test_name = "InvalidStringToRangeDate",
       .from_variant = Variant(String("not-a-range")),
       .to_type = range_date_type,
       .expected_error_substr = "Failed to parse range"},
      {.test_name = "NonStringKeyedMapKeyCastError",
       .from_variant = Variant(non_str_map_neg_key),
       .to_type = map_uint64_int64_type,
       .expected_error_substr = "Error casting key -1 in MAP"},
      {.test_name = "NonStringKeyedMapValueCastError",
       .from_variant = Variant(non_str_map_bad_val),
       .to_type = map_int64_int64_type,
       .expected_error_substr = "Error casting value for key 1 in MAP"},
      {.test_name = "NonStringKeyedMapToInt64",
       .from_variant = Variant(non_str_map_neg_key),
       .to_type = types::Int64Type(),
       .expected_error_substr =
           "Unsupported cast from MAP<INT64, INT64> to INT64"},
      {.test_name = "NonStringKeyedMapToArray",
       .from_variant = Variant(non_str_map_neg_key),
       .to_type = types::Int64ArrayType(),
       .expected_error_substr =
           "Unsupported cast from MAP<INT64, INT64> to ARRAY<INT64>"},
  };
}

INSTANTIATE_TEST_SUITE_P(
    PrimitivesAndPassThrough, CastFromVariantEvalErrorTest,
    testing::ValuesIn(GetPrimitivesAndPassThroughErrorCases()),
    [](const testing::TestParamInfo<VariantCastErrorCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastErrorCase> GetProtoErrorCases() {
  const ProtoType* nullable_int_proto_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::NullableInt::descriptor(), &nullable_int_proto_type));
  const ProtoType* test_extra_proto_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::TestExtraPB::descriptor(), &test_extra_proto_type));

  googlesql_test::NullableInt nullable_int;
  nullable_int.set_value(123);
  absl::Cord proto_cord;
  nullable_int.SerializeToString(&proto_cord);
  Value proto_val = values::Proto(nullable_int_proto_type, proto_cord);
  // Field 1, wire type 0 (varint), truncated varint continuation byte 0x80.
  Value corrupt_proto_val =
      values::Proto(nullable_int_proto_type, absl::Cord("\x08\x80"));

  return {
      {.test_name = "ProtoToIncompatibleProtoType",
       .from_variant = Variant(proto_val),
       .to_type = test_extra_proto_type,
       .expected_error_substr =
           "Unsupported cast from googlesql_test.NullableInt to "
           "googlesql_test.TestExtraPB"},
      {.test_name = "InvalidTextFormatStringToProto",
       .from_variant = Variant(String("not_a_field: ???")),
       .to_type = nullable_int_proto_type,
       .expected_error_substr = "Error parsing proto"},
      {.test_name = "CorruptWireBytesProtoToString",
       .from_variant = Variant(corrupt_proto_val),
       .to_type = types::StringType(),
       .expected_error_substr = "Invalid cast to string from type"},
      {.test_name = "ProtoObjectToInt64",
       .from_variant = Variant(proto_val),
       .to_type = types::Int64Type(),
       .expected_error_substr =
           "Unsupported cast from googlesql_test.NullableInt to INT64"},
      {.test_name = "JsonObjectToProto",
       .from_variant = Variant(ParseJson(R"({"value": 1})")),
       .to_type = nullable_int_proto_type,
       .expected_error_substr =
           "Cannot cast VARIANT Object to googlesql_test.NullableInt"},
  };
}

INSTANTIATE_TEST_SUITE_P(
    Protos, CastFromVariantEvalErrorTest,
    testing::ValuesIn(GetProtoErrorCases()),
    [](const testing::TestParamInfo<VariantCastErrorCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastErrorCase> GetObjectsAndArraysErrorCases() {
  const Type* map_int64_int64_type =
      Unwrap(type_factory->MakeMapType(types::Int64Type(), types::Int64Type()));
  const Type* map_str_int64_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64Type()));
  const Type* map_str_int64_arr_type = Unwrap(
      type_factory->MakeMapType(types::StringType(), types::Int64ArrayType()));
  Value json_one_field = ParseJson(R"({"a": 1})");

  return {
      {.test_name = "VariantObjectToPrimitive",
       .from_variant = Variant(json_one_field),
       .to_type = types::Int64Type(),
       .expected_error_substr = "Cannot cast VARIANT Object to INT64"},
      {.test_name = "VariantObjectToArray",
       .from_variant = Variant(json_one_field),
       .to_type = types::Int64ArrayType(),
       .expected_error_substr = "Cannot cast VARIANT Object to ARRAY<INT64>"},
      {.test_name = "VariantObjectToMapWithNonStringKey",
       .from_variant = Variant(json_one_field),
       .to_type = map_int64_int64_type,
       .expected_error_substr = "target MAP key type must be STRING"},
      {.test_name = "VariantObjectToMapValueCastError",
       .from_variant = Variant(ParseJson(R"({"a": "bad", "b": 2})")),
       .to_type = map_str_int64_type,
       .expected_error_substr = "Error casting value for key 'a' in MAP"},
      {.test_name = "ChainedNestedMapAndArrayElementCastError",
       .from_variant = Variant(ParseJson(R"({"a": ["bad"]})")),
       .to_type = map_str_int64_arr_type,
       .expected_error_substr =
           "Error casting value for key 'a' in MAP: Error casting element at "
           "index 0 in ARRAY: "},
      {.test_name = "VariantArrayToPrimitive",
       .from_variant = Variant(Int64Array({1, 2})),
       .to_type = types::Int64Type(),
       .expected_error_substr = "Cannot cast VARIANT Array to INT64"},
      {.test_name = "VariantArrayToMap",
       .from_variant = Variant(ParseJson("[1, 2]")),
       .to_type = map_str_int64_type,
       .expected_error_substr =
           "Cannot cast VARIANT Array to MAP<STRING, INT64>"},
      {.test_name = "VariantArrayElementCastErrorAtIndexZero",
       .from_variant = Variant(StringArray({"bad"})),
       .to_type = types::Int64ArrayType(),
       .expected_error_substr = "Error casting element at index 0 in ARRAY"},
      {.test_name = "VariantArrayElementCastErrorAtNonZeroIndex",
       .from_variant = Variant(ParseJson(R"(["1", "bad"])")),
       .to_type = types::Int64ArrayType(),
       .expected_error_substr = "Error casting element at index 1 in ARRAY: "},
  };
}

INSTANTIATE_TEST_SUITE_P(
    ObjectsAndArrays, CastFromVariantEvalErrorTest,
    testing::ValuesIn(GetObjectsAndArraysErrorCases()),
    [](const testing::TestParamInfo<VariantCastErrorCase>& info) {
      return info.param.test_name;
    });

std::vector<VariantCastErrorCase> GetStructErrorCases() {
  const StructType* struct_ab_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::Int64Type()}}, &struct_ab_type));
  const StructType* struct_unnamed_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"", types::Int64Type()}},
                                        &struct_unnamed_type));
  const StructType* struct_two_unnamed_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"", types::Int64Type()}, {"", types::Int64Type()}},
      &struct_two_unnamed_type));
  const StructType* struct_dup_a_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"a", types::Int64Type()}},
      &struct_dup_a_type));
  const StructType* struct_case_dup_a_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", types::Int64Type()}, {"A", types::Int64Type()}},
      &struct_case_dup_a_type));
  const StructType* struct_two_unnamed_with_x_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"", types::Int64Type()},
                                         {"", types::Int64Type()},
                                         {"x", types::StringType()}},
                                        &struct_two_unnamed_with_x_type));
  const StructType* struct_dup_a_with_x_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", types::Int64Type()},
                                         {"a", types::Int64Type()},
                                         {"x", types::StringType()}},
                                        &struct_dup_a_with_x_type));
  const StructType* struct_case_dup_a_with_x_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", types::Int64Type()},
                                         {"A", types::Int64Type()},
                                         {"x", types::StringType()}},
                                        &struct_case_dup_a_with_x_type));

  Value json_one_field = ParseJson(R"({"a": 1})");

  return {
      {.test_name = "VariantObjectToStructAmbiguousCaseKeys",
       .from_variant = Variant(ParseJson(R"({"a": 1, "A": 2})")),
       .to_type = struct_ab_type,
       .expected_error_substr =
           "target struct field 'a' matches multiple VARIANT Object keys "
           "case-insensitively"},
      {.test_name = "VariantObjectToStructMultipleAnonymousFields",
       .from_variant = Variant(ParseJson(R"({"": 1})")),
       .to_type = struct_two_unnamed_type,
       .expected_error_substr =
           "target struct contains multiple anonymous fields"},
      {.test_name = "VariantObjectToStructDuplicateFieldName",
       .from_variant = Variant(json_one_field),
       .to_type = struct_dup_a_type,
       .expected_error_substr =
           "target struct contains duplicate field name 'a'"},
      {.test_name = "VariantObjectToStructCaseCollidingFieldNames",
       .from_variant = Variant(json_one_field),
       .to_type = struct_case_dup_a_type,
       .expected_error_substr =
           "target struct contains duplicate field name 'A'"},
      {.test_name = "VariantObjectToStructUnmatchedMultipleAnonymousFields",
       .from_variant = Variant(ParseJson(R"({"x": "abc"})")),
       .to_type = struct_two_unnamed_with_x_type,
       .expected_error_substr =
           "target struct contains multiple anonymous fields"},
      {.test_name = "VariantObjectToStructUnmatchedDuplicateFieldName",
       .from_variant = Variant(ParseJson(R"({"x": "abc"})")),
       .to_type = struct_dup_a_with_x_type,
       .expected_error_substr =
           "target struct contains duplicate field name 'a'"},
      {.test_name = "VariantObjectToStructUnmatchedCaseCollidingFieldNames",
       .from_variant = Variant(ParseJson(R"({"x": "abc"})")),
       .to_type = struct_case_dup_a_with_x_type,
       .expected_error_substr =
           "target struct contains duplicate field name 'A'"},
      {.test_name = "VariantObjectToStructFieldCastError",
       .from_variant = Variant(ParseJson(R"({"a": "bad", "b": 2})")),
       .to_type = struct_ab_type,
       .expected_error_substr = "Error casting field 'a' in STRUCT"},
      {.test_name = "VariantObjectToStructUnnamedFieldCastError",
       .from_variant = Variant(ParseJson(R"({"": "bad"})")),
       .to_type = struct_unnamed_type,
       .expected_error_substr = "Error casting field at index 0 in STRUCT"},
      {.test_name = "VariantArrayToStructFails",
       .from_variant = Variant(ParseJson("[1, 2]")),
       .to_type = struct_ab_type,
       .expected_error_substr = "Cannot cast VARIANT Array to STRUCT"},
      {.test_name = "VariantPrimitiveStringToStructFails",
       .from_variant = Variant(String(R"({"a": 1})")),
       .to_type = struct_ab_type,
       .expected_error_substr = "Unsupported cast from STRING to STRUCT"},
  };
}

INSTANTIATE_TEST_SUITE_P(
    Structs, CastFromVariantEvalErrorTest,
    testing::ValuesIn(GetStructErrorCases()),
    [](const testing::TestParamInfo<VariantCastErrorCase>& info) {
      return info.param.test_name;
    });

// -----------------------------------------------------------------------------
// 3. Focused Behavioral & Edge-Case Tests for CAST from VARIANT
// -----------------------------------------------------------------------------

TEST(CastFromVariantTest, CastToJsonSucceedsWithoutCastToJsonFeature) {
  LanguageOptions variant_only_options;
  variant_only_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  ASSERT_FALSE(
      variant_only_options.LanguageFeatureEnabled(FEATURE_CAST_TO_JSON_TYPE));

  const EnumType* enum_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value range_date_val,
                       Value::MakeRange(Date(20089), Date(20090)));

  EXPECT_THAT(CastValue(Variant(Int64(42)), absl::UTCTimeZone(),
                        variant_only_options, types::JsonType()),
              IsOkAndHolds(Json(JSONValue(int64_t{42}))));
  EXPECT_THAT(CastValue(Variant(Json(JSONValue())), absl::UTCTimeZone(),
                        variant_only_options, types::JsonType()),
              IsOkAndHolds(Json(JSONValue())));
  EXPECT_THAT(CastValue(Variant(ParseJson(R"({"a": 1})")), absl::UTCTimeZone(),
                        variant_only_options, types::JsonType()),
              IsOkAndHolds(ParseJson(R"({"a": 1})")));
  EXPECT_THAT(CastValue(Variant(Int64Array({1, 2})), absl::UTCTimeZone(),
                        variant_only_options, types::JsonType()),
              IsOkAndHolds(ParseJson("[1, 2]")));
  EXPECT_THAT(CastValue(Variant(Value::Enum(enum_type, 1)), absl::UTCTimeZone(),
                        variant_only_options, types::JsonType()),
              IsOkAndHolds(Json(JSONValue(absl::string_view("TESTENUM1")))));
  EXPECT_THAT(
      CastValue(Variant(range_date_val), absl::UTCTimeZone(),
                variant_only_options, types::JsonType()),
      IsOkAndHolds(ParseJson(R"({"start":"2025-01-01","end":"2025-01-02"})")));
}

TEST(CastFromVariantTest, CastVariantArrayToJsonArray) {
  LanguageOptions array_options;
  array_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  array_options.EnableLanguageFeature(FEATURE_CAST_DIFFERENT_ARRAY_TYPES);
  ASSERT_FALSE(array_options.LanguageFeatureEnabled(FEATURE_CAST_TO_JSON_TYPE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value variant_array,
      Value::MakeArray(types::VariantArrayType(),
                       {Variant(Int64(1)), Variant(Json(JSONValue())),
                        Value::NullVariant()}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_json_array,
      Value::MakeArray(
          types::JsonArrayType(),
          {Json(JSONValue(int64_t{1})), Json(JSONValue()), Value::NullJson()}));
  EXPECT_THAT(CastValue(variant_array, absl::UTCTimeZone(), array_options,
                        types::JsonArrayType()),
              IsOkAndHolds(expected_json_array));
}

TEST(CastFromVariantTest, FormatsNegativeZeroAccordingToFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  // SQL double -0.0
  EXPECT_THAT(CastValue(Variant(Double(-0.0)), absl::UTCTimeZone(), options,
                        types::StringType(), /*catalog=*/nullptr,
                        /*canonicalize_zero=*/false),
              IsOkAndHolds(String("-0")));
  EXPECT_THAT(CastValue(Variant(Double(-0.0)), absl::UTCTimeZone(), options,
                        types::StringType(), /*catalog=*/nullptr,
                        /*canonicalize_zero=*/true),
              IsOkAndHolds(String("0")));

  // JSON double -0.0
  EXPECT_THAT(CastValue(Variant(Json(JSONValue(-0.0))), absl::UTCTimeZone(),
                        options, types::StringType(), /*catalog=*/nullptr,
                        /*canonicalize_zero=*/false),
              IsOkAndHolds(String("-0")));
  EXPECT_THAT(CastValue(Variant(Json(JSONValue(-0.0))), absl::UTCTimeZone(),
                        options, types::StringType(), /*catalog=*/nullptr,
                        /*canonicalize_zero=*/true),
              IsOkAndHolds(String("0")));

  // SQL double -0.0 to JSON
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value json_neg_zero,
      CastValue(Variant(Double(-0.0)), absl::UTCTimeZone(), options,
                types::JsonType(), /*catalog=*/nullptr,
                /*canonicalize_zero=*/false));
  EXPECT_EQ(json_neg_zero.json_value().ToString(), "-0.0");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value json_canonical_zero,
      CastValue(Variant(Double(-0.0)), absl::UTCTimeZone(), options,
                types::JsonType(), /*catalog=*/nullptr,
                /*canonicalize_zero=*/true));
  EXPECT_EQ(json_canonical_zero.json_value().ToString(), "0.0");
}

TEST(CastFromVariantTest,
     NumericPrecisionLossToJsonRespectsStrictNumberParsing) {
  LanguageOptions strict_options = LanguageOptions::MaximumFeatures();
  strict_options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  strict_options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

  Value max_numeric_variant = Variant(Numeric(NumericValue::MaxValue()));
  Value max_bignumeric_variant =
      Variant(BigNumeric(BigNumericValue::MaxValue()));

  EXPECT_THAT(CastValue(max_numeric_variant, absl::UTCTimeZone(),
                        strict_options, types::JsonType()),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Failed to convert type NUMERIC to JSON")));
  EXPECT_THAT(CastValue(max_bignumeric_variant, absl::UTCTimeZone(),
                        strict_options, types::JsonType()),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Failed to convert type BIGNUMERIC to JSON")));

  LanguageOptions non_strict_options = strict_options;
  non_strict_options.DisableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);
  EXPECT_THAT(CastValue(max_numeric_variant, absl::UTCTimeZone(),
                        non_strict_options, types::JsonType()),
              IsOkAndHolds(Json(JSONValue(double{1e+29}))));
  EXPECT_THAT(CastValue(max_bignumeric_variant, absl::UTCTimeZone(),
                        non_strict_options, types::JsonType()),
              IsOkAndHolds(Json(JSONValue(double{5.7896044618658096e+38}))));
}

TEST(CastFromVariantTest, PropagatesNonUtcDefaultTimezoneToNestedContainers) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  absl::TimeZone la_tz;
  ASSERT_TRUE(absl::LoadTimeZone("America/Los_Angeles", &la_tz));
  // 2025-01-01 00:00:00 in America/Los_Angeles (UTC-8) is 2025-01-01 08:00:00
  // UTC.
  Value expected_ts = Value::Timestamp(
      absl::FromCivil(absl::CivilSecond(2025, 1, 1, 0, 0, 0), la_tz));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* ts_array_type,
                       type_factory->MakeArrayType(types::TimestampType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_arr,
                       Value::MakeArray(ts_array_type, {expected_ts}));
  EXPECT_THAT(CastValue(Variant(ParseJson(R"(["2025-01-01 00:00:00"])")), la_tz,
                        options, ts_array_type),
              IsOkAndHolds(expected_arr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_str_ts_type,
      type_factory->MakeMapType(types::StringType(), types::TimestampType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_map,
      Value::MakeMap(map_str_ts_type, {{String("ts"), expected_ts}}));
  EXPECT_THAT(CastValue(Variant(ParseJson(R"({"ts": "2025-01-01 00:00:00"})")),
                        la_tz, options, map_str_ts_type),
              IsOkAndHolds(expected_map));
}

TEST(CastFromVariantTest, ArrayToArrayPreservesOrderKind) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value bag_array,
                       InternalValue::MakeArray(types::StringArrayType(),
                                                InternalValue::kIgnoresOrder,
                                                {String("10"), String("20")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value casted_bag,
                       CastValue(Variant(bag_array), absl::UTCTimeZone(),
                                 options, types::Int64ArrayType()));
  EXPECT_EQ(InternalValue::order_kind(casted_bag),
            InternalValue::kIgnoresOrder);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value ordered_array,
                       InternalValue::MakeArray(types::StringArrayType(),
                                                InternalValue::kPreservesOrder,
                                                {String("10"), String("20")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value casted_ordered,
                       CastValue(Variant(ordered_array), absl::UTCTimeZone(),
                                 options, types::Int64ArrayType()));
  EXPECT_EQ(InternalValue::order_kind(casted_ordered),
            InternalValue::kPreservesOrder);
}

TEST(CastFromVariantTest, FloatToNonFloatSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value float_variant, Value::Variant(Double(1.5)));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  float_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::StringType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(String("1.5")));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest, FloatToFloatDoesNotSetNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value float_variant, Value::Variant(Double(1.5)));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  float_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::DoubleType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(Double(1.5)));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest, VariantObjectWithFloatToMapSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_str_str_type,
      type_factory->MakeMapType(types::StringType(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_map,
      Value::MakeMap(map_str_str_type, {{String("a"), String("1.5")}}));
  Value object_variant = Variant(Struct({"a"}, {Double(1.5)}));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  object_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options, map_str_str_type,
                  /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_map));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     VariantObjectWithFloatToStructSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  const StructType* struct_str_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", types::StringType()}},
                                         &struct_str_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_struct,
                       Value::MakeStruct(struct_str_type, {String("1.5")}));

  Value object_with_matched_float = Variant(Struct({"a"}, {Double(1.5)}));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  object_with_matched_float, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options, struct_str_type,
                  /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_struct));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest, UnprojectedFloatFieldInStructCastIsDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  const StructType* struct_str_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", types::StringType()}},
                                         &struct_str_type));
  Value object_with_unasked_float =
      Variant(Struct({"a", "unasked"}, {Int64(1), Double(1.5)}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_int_str_struct,
                       Value::MakeStruct(struct_str_type, {String("1")}));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  object_with_unasked_float, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options, struct_str_type,
                  /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_int_str_struct));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest, ProtoWithFloatToStringSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  const ProtoType* kitchen_sink_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &kitchen_sink_type));
  googlesql_test::KitchenSinkPB pb;
  pb.set_int64_key_1(1);
  pb.set_int64_key_2(2);
  pb.set_float_val(1.5f);

  bool found_non_deterministic = false;
  EXPECT_THAT(
      internal::CastValueWithoutTypeValidation(
          Variant(Proto(kitchen_sink_type, pb.SerializeAsCord())),
          absl::UTCTimeZone(),
          /*current_timestamp=*/std::nullopt, options, types::StringType(),
          /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
          /*extended_conversion_evaluator=*/nullptr,
          /*canonicalize_zero=*/true, &found_non_deterministic),
      IsOkAndHolds(StringValueMatches(HasSubstr("float_val: 1.5"))));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     NonStringKeyedMapWithFloatToStringSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_double_str_type,
      type_factory->MakeMapType(types::DoubleType(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_str_double_type,
      type_factory->MakeMapType(types::StringType(), types::DoubleType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_double_str_type, {{Double(1.5), String("2.5")}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value map_variant, Value::Variant(map_val));

  // Even though the target type MAP<STRING, DOUBLE> also contains a DOUBLE,
  // pairwise component checking detects that the key is cast from DOUBLE to
  // STRING.
  bool found_non_deterministic = false;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_map,
      Value::MakeMap(map_str_double_type, {{String("1.5"), Double(2.5)}}));
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  map_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  map_str_double_type, /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_map));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     VariantArrayWithFloatToStringArraySetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value array_variant,
                       Value::Variant(values::DoubleArray({1.5})));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::StringArrayType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(StringArray({"1.5"})));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest, NestedFloatToJsonSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value nested_float_variant = Variant(Struct({"a"}, {Array({Double(1.5)})}));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  nested_float_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson(R"({"a":[1.5]})")));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     GraphNodeWithFloatInVariantToJsonSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value node, GraphNode({"g"}, "n1", {{"a", Double(1.5)}}, {"L1"}, "T"));
  bool found_non_deterministic = false;
  EXPECT_THAT(
      internal::CastValueWithoutTypeValidation(
          Variant(node), absl::UTCTimeZone(),
          /*current_timestamp=*/std::nullopt, options, types::JsonType(),
          /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
          /*extended_conversion_evaluator=*/nullptr,
          /*canonicalize_zero=*/true, &found_non_deterministic),
      IsOkAndHolds(ParseJson(
          R"({"identifier":"bjE=","kind":"node","labels":["L1"],"properties":{"a":1.5}})")));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest, GraphNodeWithIntInVariantToJsonIsDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value node,
                       GraphNode({"g"}, "n1", {{"a", Int64(1)}}, {"L1"}, "T"));
  bool found_non_deterministic = false;
  EXPECT_THAT(
      internal::CastValueWithoutTypeValidation(
          Variant(node), absl::UTCTimeZone(),
          /*current_timestamp=*/std::nullopt, options, types::JsonType(),
          /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
          /*extended_conversion_evaluator=*/nullptr,
          /*canonicalize_zero=*/true, &found_non_deterministic),
      IsOkAndHolds(ParseJson(
          R"({"identifier":"bjE=","kind":"node","labels":["L1"],"properties":{"a":1}})")));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest, JsonFloatInVariantToJsonIsDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value json_float_variant = Variant(ParseJson(R"({"a": 1.5})"));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  json_float_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson(R"({"a": 1.5})")));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     UnorderedArrayInVariantToJsonSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value unordered_array_variant =
      Variant(Array({Int64(1), Int64(2)}, kIgnoresOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  unordered_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson("[1, 2]")));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest, OrderedArrayInVariantToJsonIsDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value ordered_array_variant =
      Variant(Array({Int64(1), Int64(2)}, kPreservesOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  ordered_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson("[1, 2]")));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     SingleElementUnorderedArrayInVariantToJsonIsDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value single_elem_array_variant = Variant(Array({Int64(1)}, kIgnoresOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  single_elem_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson("[1]")));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     UnorderedArrayEqualElementsInVariantToJsonIsDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value equal_elems_array_variant =
      Variant(Array({Int64(1), Int64(1)}, kIgnoresOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  equal_elems_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson("[1, 1]")));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     UnorderedStructArrayDifferentFieldNamesToJsonIsNonDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value unordered_array_variant = Variant(Array(
      {Variant(Struct({"a"}, {Int64(1)})), Variant(Struct({"b"}, {Int64(1)}))},
      kIgnoresOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  unordered_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson(R"([{"a":1},{"b":1}])")));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     UnorderedStructArrayCaseFieldNamesToJsonIsNonDeterministic) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value unordered_array_variant = Variant(Array(
      {Variant(Struct({"a"}, {Int64(1)})), Variant(Struct({"A"}, {Int64(1)}))},
      kIgnoresOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  unordered_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(ParseJson(R"([{"a":1},{"A":1}])")));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     FailedUnorderedArrayInVariantToJsonDoesNotSetNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  Value unordered_nan_array_variant = Variant(
      Array({Double(1.5), Double(std::numeric_limits<double>::quiet_NaN())},
            kIgnoresOrder));
  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  unordered_nan_array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::JsonType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     NonStringKeyedMapWithUnorderedArrayToJsonSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_arr_type,
      type_factory->MakeMapType(types::Int64Type(), types::Int64ArrayType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_json_type,
      type_factory->MakeMapType(types::Int64Type(), types::JsonType()));
  Value unordered_array = InternalValue::ArrayNotChecked(
      types::Int64ArrayType(), InternalValue::kIgnoresOrder,
      {Int64(1), Int64(2)});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_int_arr_type, {{Int64(1), unordered_array}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value map_variant, Value::Variant(map_val));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_map,
      Value::MakeMap(map_int_json_type, {{Int64(1), ParseJson("[1, 2]")}}));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  map_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  map_int_json_type, /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_map));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     StructWithVariantFloatToStructWithStringSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  const StructType* struct_variant_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", types::VariantType()}},
                                         &struct_variant_type));
  const StructType* struct_str_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", types::StringType()}},
                                         &struct_str_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_variant_type, {Variant(Double(1.5))}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_struct,
                       Value::MakeStruct(struct_str_type, {String("1.5")}));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  struct_val, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options, struct_str_type,
                  /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_struct));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     ArrayOfVariantFloatToArrayOfStringSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_val,
      Value::MakeArray(types::VariantArrayType(), {Variant(Double(1.5))}));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  array_val, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  types::StringArrayType(), /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(StringArray({"1.5"})));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     MapWithVariantFloatToMapWithStringSetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_variant_type,
      type_factory->MakeMapType(types::Int64Type(), types::VariantType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_str_type,
      type_factory->MakeMapType(types::Int64Type(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val,
      Value::MakeMap(map_int_variant_type, {{Int64(1), Variant(Double(1.5))}}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_map,
      Value::MakeMap(map_int_str_type, {{Int64(1), String("1.5")}}));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  map_val, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options, map_int_str_type,
                  /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_map));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     StructWithVariantFloatToProtoMapEntrySetsNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  options.EnableLanguageFeature(FEATURE_PROTO_MAPS);

  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType(
      {{"key", types::StringType()}, {"value", types::VariantType()}},
      &struct_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value struct_val,
      Value::MakeStruct(struct_type, {String("k"), Variant(Double(1.5))}));

  const google::protobuf::Descriptor* map_entry_desc =
      googlesql_test::MessageWithMapField::descriptor()
          ->FindFieldByName("string_string_map")
          ->message_type();
  const ProtoType* map_entry_proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(map_entry_desc, &map_entry_proto_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value expected_proto,
      CastValue(Struct({"key", "value"}, {String("k"), String("1.5")}),
                absl::UTCTimeZone(), options, map_entry_proto_type));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  struct_val, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options,
                  map_entry_proto_type, /*format=*/std::nullopt,
                  /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/true, &found_non_deterministic),
              IsOkAndHolds(expected_proto));
  EXPECT_TRUE(found_non_deterministic);
}

TEST(CastFromVariantTest,
     IncompatibleEmptyOrNullNonStringKeyedMapReturnsOutOfRange) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_date_type,
      type_factory->MakeMapType(types::Int64Type(), types::DateType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_int_type,
      type_factory->MakeMapType(types::Int64Type(), types::Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_map, Value::MakeMap(map_int_date_type, {}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value null_val_map,
      Value::MakeMap(map_int_date_type, {{Int64(1), values::NullDate()}}));

  for (const Value& map_val : {empty_map, null_val_map}) {
    Value variant_val = Variant(map_val);
    EXPECT_THAT(
        CastValue(variant_val, absl::UTCTimeZone(), options, map_int_int_type),
        StatusIs(
            absl::StatusCode::kOutOfRange,
            HasSubstr("Unsupported cast from MAP<INT64, DATE> to MAP<INT64, "
                      "INT64>")));
    EXPECT_THAT(
        internal::CastValueWithoutTypeValidation(
            variant_val, absl::UTCTimeZone(),
            /*current_timestamp=*/std::nullopt, options, map_int_int_type,
            /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
            /*extended_conversion_evaluator=*/nullptr,
            /*canonicalize_zero=*/false),
        StatusIs(
            absl::StatusCode::kOutOfRange,
            HasSubstr("Unsupported cast from MAP<INT64, DATE> to MAP<INT64, "
                      "INT64>")));
  }
}

TEST(CastFromVariantTest, IncompatibleGraphElementReturnsOutOfRange) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label1"}, "ElementTable", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_int_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::Int32(1)}, {"b", Value::Int32(1)}}, {"label1"},
                "ElementTable", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_edge_a_b,
      GraphEdge({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label2"}, "ElementTable", "src_node_id", "dst_node_id",
                type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value different_graph_node_a_b,
      GraphNode({"new_graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label3"}, "ElementTable", type_factory));

  Value variant_node = Variant(graph_node_a_b);
  for (const Type* target_type :
       {graph_node_a_int_b.type(), graph_edge_a_b.type(),
        different_graph_node_a_b.type()}) {
    EXPECT_THAT(
        CastValue(variant_node, absl::UTCTimeZone(), options, target_type),
        StatusIs(absl::StatusCode::kOutOfRange,
                 HasSubstr("Unsupported cast from GRAPH_NODE")));
    EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                    variant_node, absl::UTCTimeZone(),
                    /*current_timestamp=*/std::nullopt, options, target_type,
                    /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                    /*extended_conversion_evaluator=*/nullptr,
                    /*canonicalize_zero=*/false),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported cast from GRAPH_NODE")));
  }
}

TEST(CastFromVariantTest, IncompatibleGraphPathReturnsOutOfRange) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::String("v0")}, {"b", Value::Int32(1)}},
                {"label1"}, "ElementTable", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value graph_node_a_int_b,
      GraphNode({"graph_name"}, "id1",
                {{"a", Value::Int32(1)}, {"b", Value::Int32(1)}}, {"label1"},
                "ElementTable", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value graph_edge_no_properties,
                       GraphEdge({"graph_name"}, "id1", {}, {"label1"},
                                 "ElementTable", "id1", "id2", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value different_graph_node,
                       GraphNode({"different_graph_name"}, "id1", {},
                                 {"label1"}, "ElementTable", type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value different_graph_edge,
      GraphEdge({"different_graph_name"}, "id1", {}, {"label1"}, "ElementTable",
                "id1", "id2", type_factory));

  const GraphPathType* path_type_node_a_b = test_values::MakeGraphPathType(
      graph_node_a_b.type()->AsGraphElement(),
      graph_edge_no_properties.type()->AsGraphElement());
  const GraphPathType* path_type_node_a_int_b = test_values::MakeGraphPathType(
      graph_node_a_int_b.type()->AsGraphElement(),
      graph_edge_no_properties.type()->AsGraphElement());
  const GraphPathType* different_graph_path_type =
      test_values::MakeGraphPathType(
          different_graph_node.type()->AsGraphElement(),
          different_graph_edge.type()->AsGraphElement());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value path_val, Value::MakeGraphPath(path_type_node_a_b,
                                                            {graph_node_a_b}));
  Value variant_path = Variant(path_val);

  for (const Type* target_type :
       {path_type_node_a_int_b, different_graph_path_type}) {
    EXPECT_THAT(
        CastValue(variant_path, absl::UTCTimeZone(), options, target_type),
        StatusIs(absl::StatusCode::kOutOfRange,
                 HasSubstr("Unsupported cast from PATH<")));
    EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                    variant_path, absl::UTCTimeZone(),
                    /*current_timestamp=*/std::nullopt, options, target_type,
                    /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                    /*extended_conversion_evaluator=*/nullptr,
                    /*canonicalize_zero=*/false),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported cast from PATH<")));
  }
}
TEST(CastFromVariantTest, VariantContainingExtendedTypeReturnsOutOfRange) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TestExtendedType extended_type(type_factory);
  Value variant_extended = Variant(extended_type.MakeValue(42));

  EXPECT_THAT(
      CastValue(variant_extended, absl::UTCTimeZone(), options,
                types::Int64Type()),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported cast from TestExtendedType to INT64")));
  EXPECT_THAT(
      internal::CastValueWithoutTypeValidation(
          variant_extended, absl::UTCTimeZone(),
          /*current_timestamp=*/std::nullopt, options, types::Int64Type(),
          /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
          /*extended_conversion_evaluator=*/nullptr,
          /*canonicalize_zero=*/false),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported cast from TestExtendedType to INT64")));
}

TEST(CastFromVariantTest,
     VariantContainingNestedExtendedTypeInMapReturnsOutOfRange) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TestExtendedType extended_type(type_factory);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_ext_type,
      type_factory->MakeMapType(types::Int64Type(), &extended_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_int_int_type,
      type_factory->MakeMapType(types::Int64Type(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value map_val, Value::MakeMap(map_int_ext_type,
                                    {{Int64(1), extended_type.MakeValue(42)}}));
  Value variant_map = Variant(map_val);

  EXPECT_THAT(
      CastValue(variant_map, absl::UTCTimeZone(), options, map_int_int_type),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported cast from MAP<INT64, TestExtendedType> "
                         "to MAP<INT64, INT64>")));
  EXPECT_THAT(
      internal::CastValueWithoutTypeValidation(
          variant_map, absl::UTCTimeZone(),
          /*current_timestamp=*/std::nullopt, options, map_int_int_type,
          /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
          /*extended_conversion_evaluator=*/nullptr,
          /*canonicalize_zero=*/false),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported cast from MAP<INT64, TestExtendedType> "
                         "to MAP<INT64, INT64>")));
}

TEST(CastFromVariantTest, UnsupportedTargetTypeReturnsInvalidArgument) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       type_factory->MakeMeasureType(types::Int64Type()));
  Value v = Variant(Int64(1));

  EXPECT_THAT(CastValue(v, absl::UTCTimeZone(), options, measure_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported cast from VARIANT to MEASURE")));
}

TEST(CastFromVariantTest, FeatureDisabledReturnsInvalidArgument) {
  LanguageOptions disabled_options;
  EXPECT_THAT(CastValue(Variant(Int64(1)), absl::UTCTimeZone(),
                        disabled_options, types::Int64Type()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported cast from VARIANT to INT64")));
}

TEST(CastFromVariantTest, FormatClauseReturnsInvalidArgument) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  EXPECT_THAT(CastValue(Variant(Date(20162)), absl::UTCTimeZone(), options,
                        types::StringType(), "YYYY/MM/DD"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("FORMAT is not allowed for cast from VARIANT "
                                 "to STRING")));
}

// TODO: b/554973153 - Update once Variant Object extraction supports nested
// proto messages and enum fields.
TEST(CastFromVariantTest, ReturnsUnimplementedForComplexProtoFields) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  const ProtoType* nullable_enum_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
      googlesql_test::NullableEnum::descriptor(), &nullable_enum_type));
  googlesql_test::NullableEnum enum_pb;
  enum_pb.set_value(googlesql_test::TESTENUM1);
  absl::Cord enum_cord;
  enum_pb.SerializeToString(&enum_cord);
  Value enum_proto_val = values::Proto(nullable_enum_type, enum_cord);

  const ProtoType* nested_msg_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
      googlesql_test::RewrappedNullableInt::descriptor(), &nested_msg_type));
  googlesql_test::RewrappedNullableInt nested_pb;
  nested_pb.mutable_value()->set_value(7);
  absl::Cord nested_cord;
  nested_pb.SerializeToString(&nested_cord);
  Value nested_proto_val = values::Proto(nested_msg_type, nested_cord);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_str_variant_type,
      type_factory->MakeMapType(types::StringType(), types::VariantType()));
  const StructType* struct_variant_val_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"value", types::VariantType()}},
                                         &struct_variant_val_type));

  EXPECT_THAT(CastValue(Variant(enum_proto_val), absl::UTCTimeZone(), options,
                        types::JsonType()),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Cannot extract nested field")));
  EXPECT_THAT(CastValue(Variant(enum_proto_val), absl::UTCTimeZone(), options,
                        map_str_variant_type),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Cannot extract nested field")));
  EXPECT_THAT(CastValue(Variant(enum_proto_val), absl::UTCTimeZone(), options,
                        struct_variant_val_type),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Cannot extract nested field")));
  EXPECT_THAT(CastValue(Variant(nested_proto_val), absl::UTCTimeZone(), options,
                        types::JsonType()),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Cannot extract nested field")));
  EXPECT_THAT(CastValue(Variant(nested_proto_val), absl::UTCTimeZone(), options,
                        map_str_variant_type),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Cannot extract nested field")));
  EXPECT_THAT(CastValue(Variant(nested_proto_val), absl::UTCTimeZone(), options,
                        struct_variant_val_type),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Cannot extract nested field")));
}

TEST(CastFromVariantTest,
     FailedArrayVariantCastDoesNotMutateNonDeterministicFlag) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_variant_type,
                       type_factory->MakeArrayType(types::VariantType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_int64_type,
                       type_factory->MakeArrayType(types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_variant,
      Value::MakeArray(array_variant_type, {Variant(Value::Double(1.5)),
                                            Variant(Value::String("bad"))}));

  bool found_non_deterministic = false;
  EXPECT_THAT(internal::CastValueWithoutTypeValidation(
                  array_variant, absl::UTCTimeZone(),
                  /*current_timestamp=*/std::nullopt, options, array_int64_type,
                  /*format=*/std::nullopt, /*time_zone=*/std::nullopt,
                  /*extended_conversion_evaluator=*/nullptr,
                  /*canonicalize_zero=*/false, &found_non_deterministic),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_FALSE(found_non_deterministic);
}

TEST(CastFromVariantTest, ReturnsResourceExhaustedOnDeepNesting) {
  // Set enough_stack_bytes to 16MB to guarantee ThreadHasEnoughStack() returns
  // false once recursion passes kNestingLevelStackCheckThreshold.
  size_t original_bytes = absl::GetFlag(FLAGS_googlesql_enough_stack_bytes);
  absl::Cleanup reset_flag = [original_bytes]() {
    absl::SetFlag(&FLAGS_googlesql_enough_stack_bytes, original_bytes);
  };
  absl::SetFlag(&FLAGS_googlesql_enough_stack_bytes, 16 * 1024 * 1024);

  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  constexpr int kNestingDepth = 15;  // > kNestingLevelStackCheckThreshold (10)

  // 1. Deeply nested JSON object to MAP (recurses) vs. to JSON (direct copy)
  std::string nested_json_obj = "1";
  for (int i = 0; i < kNestingDepth; ++i) {
    nested_json_obj = absl::StrCat(R"({"a":)", nested_json_obj, "}");
  }
  Value nested_json_obj_variant = Variant(ParseJson(nested_json_obj));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_str_variant_type,
      type_factory->MakeMapType(types::StringType(), types::VariantType()));
  const StructType* struct_variant_a_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", types::VariantType()}},
                                         &struct_variant_a_type));
  Value nested_sql_map_variant = Variant(Int64(1));
  Value nested_sql_struct_variant = Variant(Int64(1));
  const Type* nested_target_map_type = types::Int64Type();
  const Type* nested_target_struct_type = types::Int64Type();
  for (int i = 0; i < kNestingDepth; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_sql_map_variant,
        Value::MakeMap(map_str_variant_type,
                       {{String("a"), nested_sql_map_variant}}));
    nested_sql_map_variant = Variant(nested_sql_map_variant);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_sql_struct_variant,
        Value::MakeStruct(struct_variant_a_type, {nested_sql_struct_variant}));
    nested_sql_struct_variant = Variant(nested_sql_struct_variant);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_target_map_type,
        type_factory->MakeMapType(types::StringType(), nested_target_map_type));
    const StructType* next_struct_type = nullptr;
    GOOGLESQL_ASSERT_OK(type_factory->MakeStructType({{"a", nested_target_struct_type}},
                                           &next_struct_type));
    nested_target_struct_type = next_struct_type;
  }

  Value nested_sql_array_variant = Variant(Int64(1));
  for (int i = 0; i < kNestingDepth; ++i) {
    nested_sql_array_variant = Variant(Unwrap(Value::MakeArray(
        types::VariantArrayType(), {nested_sql_array_variant})));
  }

  EXPECT_THAT(CastValue(nested_json_obj_variant, absl::UTCTimeZone(), options,
                        nested_target_map_type),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));
  // Casting a JSON-backed VARIANT to JSON copies the underlying JSONNode
  // directly without recursive traversal.
  EXPECT_THAT(CastValue(nested_json_obj_variant, absl::UTCTimeZone(), options,
                        types::JsonType()),
              IsOkAndHolds(ParseJson(nested_json_obj)));
  EXPECT_THAT(CastValue(nested_json_obj_variant, absl::UTCTimeZone(), options,
                        nested_target_struct_type),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));

  // 2. Deeply nested SQL MAP, SQL ARRAY, and SQL STRUCT to MAP, JSON, and
  // STRUCT
  EXPECT_THAT(CastValue(nested_sql_map_variant, absl::UTCTimeZone(), options,
                        nested_target_map_type),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));
  EXPECT_THAT(CastValue(nested_sql_map_variant, absl::UTCTimeZone(), options,
                        types::JsonType()),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));
  EXPECT_THAT(CastValue(nested_sql_struct_variant, absl::UTCTimeZone(), options,
                        nested_target_struct_type),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));
  EXPECT_THAT(CastValue(nested_sql_array_variant, absl::UTCTimeZone(), options,
                        types::JsonType()),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));

  // 3. Deeply nested alternating ARRAY<MAP> across JSON and SQL containers
  // GoogleSQL disallows direct ARRAY<ARRAY<T>>, so alternate ARRAY<MAP<...>>
  // (depth 16) to test deep ARRAY-target recursion across both JSON and SQL
  // container payloads.
  std::string nested_json_alternating = "1";
  Value nested_sql_alternating_variant = Variant(Int64(1));
  const Type* nested_alternating_target_type = types::Int64Type();
  for (int i = 0; i < 8; ++i) {
    nested_json_alternating =
        absl::StrCat(R"([{"a":)", nested_json_alternating, "}]");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_sql_alternating_variant,
        Value::MakeMap(map_str_variant_type,
                       {{String("a"), nested_sql_alternating_variant}}));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_sql_alternating_variant,
        Value::MakeArray(types::VariantArrayType(),
                         {Variant(nested_sql_alternating_variant)}));
    nested_sql_alternating_variant = Variant(nested_sql_alternating_variant);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_alternating_target_type,
        type_factory->MakeMapType(types::StringType(),
                                  nested_alternating_target_type));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        nested_alternating_target_type,
        type_factory->MakeArrayType(nested_alternating_target_type));
  }
  Value nested_json_alternating_variant =
      Variant(ParseJson(nested_json_alternating));

  EXPECT_THAT(CastValue(nested_json_alternating_variant, absl::UTCTimeZone(),
                        options, nested_alternating_target_type),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));
  EXPECT_THAT(CastValue(nested_sql_alternating_variant, absl::UTCTimeZone(),
                        options, nested_alternating_target_type),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of stack space")));
}

TEST(IsTypeCastableFromVariantTest, StructWithAnonymousOrDuplicateFieldNames) {
  LanguageOptions options;
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  const StructType* valid_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"a", types::Int64Type()}, {"b", types::StringType()}},
      &valid_struct_type));
  EXPECT_TRUE(IsTypeCastableFromVariant(valid_struct_type, options));

  const StructType* anon_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"", types::Int64Type()}}, &anon_struct_type));
  EXPECT_TRUE(IsTypeCastableFromVariant(anon_struct_type, options));

  const StructType* dup_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"a", types::Int64Type()}, {"a", types::Int64Type()}},
      &dup_struct_type));
  EXPECT_TRUE(IsTypeCastableFromVariant(dup_struct_type, options));

  const StructType* case_dup_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"a", types::Int64Type()}, {"A", types::Int64Type()}},
      &case_dup_struct_type));
  EXPECT_TRUE(IsTypeCastableFromVariant(case_dup_struct_type, options));

  const StructType* multiple_anon_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"", types::Int64Type()},
                                    {"", types::Int64Type()},
                                    {"x", types::StringType()}},
                                   &multiple_anon_struct_type));
  EXPECT_TRUE(IsTypeCastableFromVariant(multiple_anon_struct_type, options));
}

TEST(CastFromVariantTest, StructUnicodeFieldCaseInsensitiveMatch) {
  absl::SetFlag(&FLAGS_googlesql_idstring_allow_unicode_characters, true);

  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  const StructType* unicode_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"ä", types::Int64Type()}},
                                   &unicode_struct_type));
  EXPECT_THAT(
      CastValue(Variant(ParseJson(R"({"Ä": 7})")), absl::UTCTimeZone(), options,
                unicode_struct_type),
      IsOkAndHolds(Unwrap(Value::MakeStruct(unicode_struct_type, {Int64(7)}))));
}

TEST(CastFromVariantTest, StructUnicodeDuplicateTargetFieldNamesFails) {
  absl::SetFlag(&FLAGS_googlesql_idstring_allow_unicode_characters, true);

  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  const StructType* dup_unicode_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"ä", types::Int64Type()}, {"Ä", types::Int64Type()}},
      &dup_unicode_struct_type));
  EXPECT_THAT(
      CastValue(Variant(ParseJson(R"({"x": 1})")), absl::UTCTimeZone(), options,
                dup_unicode_struct_type),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("target struct contains duplicate field name 'Ä'")));
}

TEST(CastFromVariantTest, StructUnicodeAmbiguousSourceKeysFails) {
  absl::SetFlag(&FLAGS_googlesql_idstring_allow_unicode_characters, true);

  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  const StructType* unicode_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"ä", types::Int64Type()}},
                                   &unicode_struct_type));
  EXPECT_THAT(
      CastValue(Variant(ParseJson(R"({"ä": 1, "Ä": 2})")), absl::UTCTimeZone(),
                options, unicode_struct_type),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("target struct field 'ä' matches multiple VARIANT "
                         "Object keys case-insensitively")));
}

TEST(CastFromVariantTest, StructCastErrorFollowsTargetFieldOrder) {
  LanguageOptions options = LanguageOptions::MaximumFeatures();
  options.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  const StructType* struct_ba_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType(
      {{"b", types::Int64Type()}, {"a", types::Int64Type()}}, &struct_ba_type));
  EXPECT_THAT(CastValue(Variant(ParseJson(R"({"a": "bad_a", "b": "bad_b"})")),
                        absl::UTCTimeZone(), options, struct_ba_type),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Error casting field 'b' in STRUCT")));
}

}  // namespace

TEST(IsTypeCastableToVariantTest, TypeCastableToVariant) {
  LanguageOptions options_enabled;
  options_enabled.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  LanguageOptions options_disabled;

  TypeFactory factory;
  const Type* int64_type = types::Int64Type();
  const Type* variant_type = types::VariantType();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_int64_type,
                       factory.MakeArrayType(int64_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(types::StringType(), int64_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type,
                       factory.MakeMeasureType(int64_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_measure_type,
                       factory.MakeArrayType(measure_type));

  // Feature disabled
  EXPECT_FALSE(IsTypeCastableToVariant(int64_type, options_disabled));
  EXPECT_TRUE(IsTypeCastableToVariant(variant_type, options_disabled));

  // Feature enabled
  EXPECT_TRUE(IsTypeCastableToVariant(int64_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToVariant(variant_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToVariant(map_type, options_enabled));
  EXPECT_TRUE(IsTypeCastableToVariant(array_int64_type, options_enabled));

  // ROW type
  SimpleTable table("TableName");
  const RowType* row_type;
  GOOGLESQL_ASSERT_OK(factory.MakeRowType(&table, table.FullName(), &row_type));
  EXPECT_FALSE(IsTypeCastableToVariant(row_type, options_enabled));

  // Unsupported types
  EXPECT_FALSE(IsTypeCastableToVariant(measure_type, options_enabled));
  EXPECT_FALSE(IsTypeCastableToVariant(array_measure_type, options_enabled));
  const StructType* struct_measure_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"m", measure_type}}, &struct_measure_type));
  EXPECT_FALSE(IsTypeCastableToVariant(struct_measure_type, options_enabled));
  const Type* map_measure_type;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(map_measure_type,
                       factory.MakeMapType(types::StringType(), measure_type));
  EXPECT_FALSE(IsTypeCastableToVariant(map_measure_type, options_enabled));
}

TEST(IsTypeCastableToVariantTest, ExtendedTypeCastToVariant) {
  LanguageOptions options_enabled;
  options_enabled.EnableLanguageFeature(FEATURE_VARIANT_TYPE);

  TypeFactory factory;
  TestExtendedType extended_type(&factory);
  EXPECT_TRUE(IsTypeCastableToVariant(&extended_type, options_enabled));

  Value extended_val = extended_type.MakeValue(42);
  // CastValue without catalog/evaluator should succeed when casting
  // ExtendedType -> VARIANT.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value variant_val,
      CastValue(extended_val, absl::UTCTimeZone(), options_enabled,
                types::VariantType(), /*catalog=*/nullptr));
  EXPECT_TRUE(variant_val.type()->IsVariant());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_variant, Value::Variant(extended_val));
  EXPECT_TRUE(variant_val.Equals(expected_variant));

  // Casting VARIANT -> ExtendedType should fail with InvalidArgumentError, not
  // FailedPrecondition due to missing catalog/evaluator.
  EXPECT_THAT(CastValue(variant_val, absl::UTCTimeZone(), options_enabled,
                        &extended_type, /*catalog=*/nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IsTypeCastableToVariantTest, ArrayWithNullElementCastToVariantArray) {
  LanguageOptions options_enabled;
  options_enabled.EnableLanguageFeature(FEATURE_VARIANT_TYPE);
  options_enabled.EnableLanguageFeature(FEATURE_CAST_DIFFERENT_ARRAY_TYPES);

  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_int32_type,
                       factory.MakeArrayType(types::Int32Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* array_variant_type,
                       factory.MakeArrayType(types::VariantType()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value array_val,
      Value::MakeArray(array_int32_type,
                       {Value::Int32(1), Value::NullInt32(), Value::Int32(3)}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result_val,
      CastValue(array_val, absl::UTCTimeZone(), options_enabled,
                array_variant_type, /*catalog=*/nullptr));
  ASSERT_EQ(result_val.num_elements(), 3);

  // Element 0: Variant(1)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem0, Value::Variant(Value::Int32(1)));
  EXPECT_TRUE(result_val.element(0).Equals(expected_elem0));

  // Element 1: Canonicalized to SQL NULL Variant (Value::NullVariant()).
  EXPECT_TRUE(result_val.element(1).is_null());
  EXPECT_TRUE(result_val.element(1).Equals(Value::NullVariant()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem1,
                       Value::Variant(Value::NullInt32()));
  EXPECT_TRUE(result_val.element(1).Equals(expected_elem1));

  // Element 2: Variant(3)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value expected_elem2, Value::Variant(Value::Int32(3)));
  EXPECT_TRUE(result_val.element(2).Equals(expected_elem2));
}

}  // namespace googlesql
