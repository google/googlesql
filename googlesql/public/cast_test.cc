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

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/function.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

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

}  // namespace googlesql
