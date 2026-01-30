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
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "google/protobuf/wrappers.pb.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/interval_value_test_util.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "re2/re2.h"

namespace googlesql {

namespace {

constexpr absl::StatusCode kOutOfRange = absl::StatusCode::kOutOfRange;

// A helper function to add test cases for type Array, Struct, Proto to the
// result test set <all_tests>. If <is_to_json> is true, adds TO_JSON test
// cases. Otherwise adds TO_JSON_STRING test cases.
void AddArrayStructAndProtoTestCases(bool is_to_json,
                                     std::vector<FunctionTestCall>& all_tests) {
  std::string function_name = is_to_json ? "to_json" : "to_json_string";
  auto json_parse_constructor = [](absl::string_view input) {
    return values::Json(JSONValue::ParseJSONString(input).value());
  };

  std::vector<std::pair<Value, std::string>> common_test_cases = {
      // ARRAY
      // Array elements are comma-separated and enclosed in brackets. Pretty-
      // printing results in newlines with two spaces of indentation between
      // elements.
      {Int64Array({-10000}), "[-10000]"},
      {Array({Int64(1), NullInt64(), Int64(-10)}), "[1,null,-10]"},
      {StringArray({"foo", "", "bar"}), R"(["foo","","bar"])"},

      // STRUCT
      // Struct fields are rendered as comma-separated
      // <field_name>:<field_value> pairs enclosed in braces. The same escaping
      // rules for string values apply to field names.
      {Struct({}, {}), "{}"},
      {Struct({"x"}, {Int64(5)}), R"({"x":5})"},
      {Struct({"x"}, {Double(3.14)}), R"({"x":3.14})"},
      {Struct({"x", "y", "z"}, {Double(double_pos_inf), Float(float_neg_inf),
                                Double(double_nan)}),
       R"({"x":"Infinity","y":"-Infinity","z":"NaN"})"},
      {Struct({"x", "", "foo", "x"},
              {Int64(5), Bool(true), String("bar"), DateFromStr("2017-03-28")}),
       R"({"x":5,"":true,"foo":"bar","x":"2017-03-28"})"},

      // Arrays within structs
      {Struct(
           {"a", "b", "c", "x", "y", "z"},
           {Int64Array({10, 11, 12}), Int64Array({}), Null(EmptyStructType()),
            Struct({}, {}),
            Struct({"d", "e", "f"},
                   {Int64(20), StringArray({"foo", "bar", "baz"}),
                    Int64Array({})}),
            Array({Struct({"g"}, {Array({Struct(
                                     {"h"}, {Int64Array({30, 31, 32})})})})})}),
       R"json({"a":[10,11,12],"b":[],"c":null,"x":{},)json"
       R"json("y":{"d":20,"e":["foo","bar","baz"],"f":[]},)json"
       R"json("z":[{"g":[{"h":[30,31,32]}]}]})json"},
  };
  for (const auto& common_test_case : common_test_cases) {
    if (!is_to_json) {
      all_tests.push_back({function_name,
                           {common_test_case.first},
                           String(common_test_case.second)});
    } else {
      all_tests.push_back({function_name,
                           {common_test_case.first},
                           json_parse_constructor(common_test_case.second)});
    }
  }

  // Additional pretty print test cases for to_json_string.
  if (!is_to_json) {
    std::vector<FunctionTestCall> pretty_print_test_cases = {
        {"to_json_string",
         {Int64Array({-10000}), Bool(true)},  // pretty-print
         String("[\n  -10000\n]")},
        {"to_json_string",
         {Array({Int64(1), NullInt64(), Int64(-10)}),
          Bool(true)},  // pretty-print
         String("[\n  1,\n  null,\n  -10\n]")},
        {"to_json_string", {Int64Array({}), Bool(false)}, String("[]")},
        {"to_json_string", {Int64Array({}), Bool(true)}, String("[]")},
        {"to_json_string",
         {Array({Int64(1), NullInt64(), Int64(-10)}),
          Bool(true)},  // pretty-print
         String("[\n  1,\n  null,\n  -10\n]")},

        {"to_json_string", {Struct({}, {}), Bool(true)}, String("{}")},
        {"to_json_string",
         {Struct({"x"}, {Int64(5)}), Bool(true)},
         String("{\n  \"x\": 5\n}")},
        {"to_json_string",
         {Struct({"x", "", "foo", "x"}, {Int64(5), Bool(true), String("bar"),
                                         DateFromStr("2017-03-28")}),
          Bool(true)},
         String(R"({
  "x": 5,
  "": true,
  "foo": "bar",
  "x": "2017-03-28"
})")},

        {"to_json_string",
         {Struct({"a", "b", "c", "x", "y", "z"},
                 {Int64Array({10, 11, 12}), Int64Array({}),
                  Null(EmptyStructType()), Struct({}, {}),
                  Struct({"d", "e", "f"},
                         {Int64(20), StringArray({"foo", "bar", "baz"}),
                          Int64Array({})}),
                  Array({Struct(
                      {"g"},
                      {Array({Struct({"h"}, {Int64Array({30, 31, 32})})})})})}),
          Bool(true)},
         String(R"json({
  "a": [
    10,
    11,
    12
  ],
  "b": [],
  "c": null,
  "x": {},
  "y": {
    "d": 20,
    "e": [
      "foo",
      "bar",
      "baz"
    ],
    "f": []
  },
  "z": [
    {
      "g": [
        {
          "h": [
            30,
            31,
            32
          ]
        }
      ]
    }
  ]
})json")},
    };
    for (const auto& pretty_print_test_case : pretty_print_test_cases) {
      all_tests.push_back(pretty_print_test_case);
    }
  }

  // Additional stringify test cases.
  if (is_to_json) {
    std::vector<FunctionTestCall> additional_stringify_test_cases = {
        {"to_json",
         QueryParamsWithResult(
             {Struct({"x"}, {Int64(9007199254740993)}), false},
             json_parse_constructor(R"({"x":9007199254740993})"))
             .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
        {"to_json",
         QueryParamsWithResult({Struct({"x"}, {Int64(9007199254740993)}), true},
                               values::Json(JSONValue::ParseJSONString(
                                                R"({"x":"9007199254740993"})")
                                                .value()))
             .WrapWithFeatureSet(
                 {FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})}};
    for (const auto& stringify_test_case : additional_stringify_test_cases) {
      all_tests.push_back(stringify_test_case);
    }
  }

  // Test cases differs in minor behavior.
  if (is_to_json) {
    // Disable this case due to a limitation at Value::EqualElementMultiSet
    // all_test_cases.push_back({function_name,
    //                           {Struct({"x"}, {Float(3.14f)})},
    //                           json_parse_constructor(R"({"x":3.14})"),
    //                           kDefaultFloatMargin});
  } else {
    all_tests.push_back(
        {function_name, {Struct({"x"}, {Float(3.14)})}, R"({"x":3.14})"});
  }
}

// A helper function to add test cases for Civil and Nano to the result test set
// `all_tests`. If is_to_json is true, adds TO_JSON test cases. Otherwise adds
// TO_JSON_STRING test cases.
void AddCivilAndNanoTestCases(bool is_to_json,
                              std::vector<FunctionTestCall>& all_tests) {
  std::vector<std::pair<Value, std::string>> civil_time_test_cases = {
      // DATETIME
      // datetime with a T separator.
      {DatetimeFromStr("2017-06-25 12:34:56.123456"),
       "2017-06-25T12:34:56.123456"},
      {DatetimeFromStr("2017-06-25 05:13:00"), "2017-06-25T05:13:00"},
      {DatetimeFromStr("2017-06-25 23:34:56.123456"),
       "2017-06-25T23:34:56.123456"},

      {DatetimeFromStr("2017-06-25 12:00:00"), "2017-06-25T12:00:00"},
      {DatetimeFromStr("1918-11-11"), "1918-11-11T00:00:00"},
      {{DatetimeFromStr("0001-01-01")}, "0001-01-01T00:00:00"},
      {DatetimeFromStr("9999-12-31 23:59:59.999999"),
       "9999-12-31T23:59:59.999999"},

      // TIME
      {TimeFromStr("12:00:00"), "12:00:00"},
      {TimeFromStr("00:00:00"), "00:00:00"},
      {TimeFromStr("23:59:59.999999"), "23:59:59.999999"}};

  for (const auto& test_case : civil_time_test_cases) {
    if (is_to_json) {
      AddTestCaseWithWrappedResultForCivilTimeAndNanos(
          CivilTimeTestCase({test_case.first},
                            values::Json(JSONValue(test_case.second)),
                            /*output_type=*/nullptr, {FEATURE_JSON_TYPE}),
          "to_json", &all_tests);
    } else {
      AddTestCaseWithWrappedResultForCivilTimeAndNanos(
          CivilTimeTestCase({test_case.first},
                            String(absl::StrCat("\"", test_case.second, "\""))),
          "to_json_string", &all_tests);
    }
  }

  // Time, and datetimes with nanosecond precision.
  std::vector<std::pair<Value, std::string>> nanos_civil_time_test_cases = {
      {{DatetimeFromStr("2017-06-25 12:34:56.123456789",
                        functions::kNanoseconds)},
       "2017-06-25T12:34:56.123456789"},
      {{DatetimeFromStr("2017-06-25 12:34:56.1234567",
                        functions::kNanoseconds)},
       "2017-06-25T12:34:56.123456700"},
      {{TimeFromStr("12:34:56.123456789", functions::kNanoseconds)},
       "12:34:56.123456789"},
  };
  for (const auto& test_case : nanos_civil_time_test_cases) {
    if (is_to_json) {
      AddTestCaseWithWrappedResultForCivilTimeAndNanos(
          CivilTimeTestCase({test_case.first},
                            values::Json(JSONValue(test_case.second)),
                            /*output_type=*/nullptr,
                            {FEATURE_JSON_TYPE, FEATURE_TIMESTAMP_NANOS}),
          "to_json", &all_tests);
    } else {
      AddTestCaseWithWrappedResultForCivilTimeAndNanos(
          CivilTimeTestCase({test_case.first},
                            String(absl::StrCat("\"", test_case.second, "\"")),
                            /*output_type=*/nullptr, {FEATURE_TIMESTAMP_NANOS}),
          "to_json_string", &all_tests);
    }
  }

  // Timestamp with nanosecond precision.
  std::vector<std::pair<Value, std::string>> test_cases = {
      {{TimestampFromStr("2017-06-25 12:34:56.123456789",
                         functions::kNanoseconds)},
       "2017-06-25T12:34:56.123456789Z"},
      {{TimestampFromStr("2017-06-25T12:34:56.1234567",
                         functions::kNanoseconds)},
       "2017-06-25T12:34:56.123456700Z"},
  };

  for (const auto& test_case : test_cases) {
    if (is_to_json) {
      all_tests.emplace_back(
          "to_json",
          QueryParamsWithResult({test_case.first},
                                values::Json(JSONValue(test_case.second)))
              .AddRequiredFeature(FEATURE_JSON_TYPE)
              .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS));
    } else {
      all_tests.emplace_back(
          "to_json_string",
          QueryParamsWithResult(
              {test_case.first},
              String(absl::StrCat("\"", test_case.second, "\"")))
              .AddRequiredFeature(FEATURE_TIMESTAMP_NANOS));
    }
  }
}

// A helper function to add test cases for type String, Byte, Enum, Date and
// Timestamp to the result test set <all_tests>. If <is_to_json> is true,
// adds TO_JSON test cases. Otherwise adds TO_JSON_STRING test cases.
void AddStringByteEnumDateTimestampTestCases(
    bool is_to_json, std::vector<FunctionTestCall>& all_tests) {
  const Value test_enum0 = Value::Enum(TestEnumType(), 0);
  const Value test_enum_negative = Value::Enum(TestEnumType(), -1);

  std::vector<std::pair<Value, std::string>> common_test_cases = {
      // STRING
      // ", \, and characters with Unicode values from U+0000 to U+001F are
      // escaped.
      {String("foo"), "foo"},
      {String(""), ""},
      {String("Моша_öá5ホバークラフト鰻鰻"), "Моша_öá5ホバークラフト鰻鰻"},

      // BYTES
      // Contents are base64-escaped and quoted.
      {Bytes(""), ""},
      {Bytes(" "), "IA=="},
      {Bytes("abcABC"), "YWJjQUJD"},
      {Bytes("abcABCжщфЖЩФ"), "YWJjQUJD0LbRidGE0JbQqdCk"},
      {Bytes("Ḋ"), "4biK"},
      {Bytes("abca\0b\0c\0"), "YWJjYQBiAGMA"},

      // ENUM
      // Quoted enum names.
      {test_enum0, "TESTENUM0"},
      {test_enum_negative, "TESTENUMNEGATIVE"},

      // DATE
      // Quoted date.
      {DateFromStr("2017-06-25"), "2017-06-25"},
      {DateFromStr("1918-11-11"), "1918-11-11"},
      {DateFromStr("0001-01-01"), "0001-01-01"},
      {DateFromStr("9999-12-31"), "9999-12-31"},

      // TIMESTAMP
      // Quoted timestamp with a T separator and Z timezone suffix.
      {TimestampFromStr("2017-06-25 12:34:56.123456"),
       "2017-06-25T12:34:56.123456Z"},
      {TimestampFromStr("2017-06-25 05:13:00"), "2017-06-25T05:13:00Z"},
      {TimestampFromStr("2017-06-25 23:34:56.123456"),
       "2017-06-25T23:34:56.123456Z"},
      {TimestampFromStr("2017-06-25 12:34:56.12345"),
       "2017-06-25T12:34:56.123450Z"},
      {TimestampFromStr("2017-06-25 12:34:56.123"), "2017-06-25T12:34:56.123Z"},
      {TimestampFromStr("2017-06-25 12:34:56.12"), "2017-06-25T12:34:56.120Z"},
      {TimestampFromStr("2017-06-25 12:34:56.1"), "2017-06-25T12:34:56.100Z"},
      {TimestampFromStr("2017-06-25 12:34:00"), "2017-06-25T12:34:00Z"},
      {TimestampFromStr("2017-06-25 12:00:00"), "2017-06-25T12:00:00Z"},
      {TimestampFromStr("1918-11-11"), "1918-11-11T00:00:00Z"},
      {TimestampFromStr("0001-01-01"), "0001-01-01T00:00:00Z"},
      {TimestampFromStr("9999-12-31 23:59:59.999999"),
       "9999-12-31T23:59:59.999999Z"},
  };

  for (const auto& test_case : common_test_cases) {
    if (is_to_json) {
      all_tests.push_back({"to_json",
                           {test_case.first},
                           values::Json(JSONValue(test_case.second))});

    } else {
      all_tests.push_back({"to_json_string",
                           {test_case.first},
                           String(absl::StrCat("\"", test_case.second, "\""))});
    }
  }

  if (is_to_json) {
    all_tests.push_back(
        {"to_json", {Null(test_enum0.type())}, Value::Json(JSONValue())});
    all_tests.push_back(
        {"to_json",
         {String(R"(a"in"between\slashes\\)")},
         values::Json(JSONValue(std::string(R"(a"in"between\slashes\\)")))});
    all_tests.push_back({"to_json",
                         {String("abca\x00\x01\x1A\x1F\x20")},
                         values::Json(JSONValue::ParseJSONString(
                                          R"("abca\u0000\u0001\u001a\u001f ")")
                                          .value())});
  } else {
    all_tests.push_back(
        {"to_json_string", {Null(test_enum0.type())}, String("null")});
    all_tests.push_back({"to_json_string",
                         {String(R"(a"in"between\slashes\\)")},
                         String(R"("a\"in\"between\\slashes\\\\")")});
    all_tests.push_back({"to_json_string",
                         {String("abca\x00\x01\x1A\x1F\x20")},
                         String(R"("abca\u0000\u0001\u001a\u001f ")")});
  }
}

// A helper function to add test cases for type IntervalValue to the result test
// set <all_tests>. If <is_to_json> is true, adds TO_JSON test cases. Otherwise
// adds TO_JSON_STRING test cases.
void AddIntervalValueTestCases(bool is_to_json,
                               std::vector<FunctionTestCall>& all_tests) {
  using interval_testing::Days;
  using interval_testing::Hours;
  using interval_testing::Micros;
  using interval_testing::Minutes;
  using interval_testing::Months;
  using interval_testing::Nanos;
  using interval_testing::Seconds;
  using interval_testing::Years;

  const std::vector<std::pair<IntervalValue, std::string>> interval_test_cases =
      {
          {Years(0), "P0Y"},
          {Years(1), "P1Y"},
          {Years(-1), "P-1Y"},
          {Years(10000), "P10000Y"},
          {Years(-10000), "P-10000Y"},
          {Months(11), "P11M"},
          {Months(-11), "P-11M"},
          {Days(40), "P40D"},
          {Days(-40), "P-40D"},
          {Days(3660000), "P3660000D"},
          {Days(-3660000), "P-3660000D"},
          {Hours(23), "PT23H"},
          {Hours(-23), "PT-23H"},
          {Hours(789), "PT789H"},
          {Hours(-789), "PT-789H"},
          {Minutes(1), "PT1M"},
          {Minutes(-1), "PT-1M"},
          {Minutes(59), "PT59M"},
          {Minutes(-59), "PT-59M"},
          {Seconds(1), "PT1S"},
          {Seconds(-1), "PT-1S"},
          {Seconds(59), "PT59S"},
          {Seconds(-59), "PT-59S"},
          {Nanos(100000000), "PT0.1S"},
          {Nanos(100000), "PT0.0001S"},
          {Nanos(100), "PT0.0000001S"},
          {Nanos(1), "PT0.000000001S"},
          {Nanos(-200000000), "PT-0.2S"},
          {Nanos(-200000), "PT-0.0002S"},
          {Nanos(-200), "PT-0.0000002S"},
          {Nanos(-2), "PT-0.000000002S"},
          {Nanos(987654321), "PT0.987654321S"},
          {Nanos(987654), "PT0.000987654S"},
          {Nanos(987), "PT0.000000987S"},
          {Nanos(-987654321), "PT-0.987654321S"},
          {Nanos(-987654), "PT-0.000987654S"},
          {Nanos(-987), "PT-0.000000987S"},
          {IntervalValue::MinValue(), "P-10000Y-3660000DT-87840000H"},
          {IntervalValue::MaxValue(), "P10000Y3660000DT87840000H"},
          {*IntervalValue::ParseFromString("1-2 -3 4:5:6",
                                           /*allow_nanos=*/true),
           "P1Y2M-3DT4H5M6S"},
          {*IntervalValue::ParseFromString("1-2 3 -4:5:6",
                                           /*allow_nanos=*/true),
           "P1Y2M3DT-4H-5M-6S"},
          {*IntervalValue::ParseFromString("1-2 3 4:5:6.123456789",
                                           /*allow_nanos=*/true),
           "P1Y2M3DT4H5M6.123456789S"},
          {*IntervalValue::ParseFromString("-1-2 -3 -4:5:6.123456789",
                                           /*allow_nanos=*/true),
           "P-1Y-2M-3DT-4H-5M-6.123456789S"},
          {*IntervalValue::ParseFromString("12:34:56.789",
                                           /*allow_nanos=*/true),
           "PT12H34M56.789S"},
          {*IntervalValue::ParseFromString("-12:34:56.789",
                                           /*allow_nanos=*/true),
           "PT-12H-34M-56.789S"},
          {*IntervalValue::ParseFromString("1-0 0 0:0:2", /*allow_nanos=*/true),
           "P1YT2S"},
          {*IntervalValue::ParseFromString("-0-1 0 -0:2:0",
                                           /*allow_nanos=*/true),
           "P-1MT-2M"},
          {*IntervalValue::ParseFromString("10000-0 0 0:0:0.000001",
                                           /*allow_nanos=*/true),
           "P10000YT0.000001S"},
          {*IntervalValue::ParseFromString("-10000-0 0 -0:0:0.000001",
                                           /*allow_nanos=*/true),
           "P-10000YT-0.000001S"},
      };

  for (const auto& test_case : interval_test_cases) {
    QueryParamsWithResult::FeatureSet required_features{FEATURE_INTERVAL_TYPE};
    if (test_case.first.get_nano_fractions() != 0) {
      required_features.insert(FEATURE_TIMESTAMP_NANOS);
    }
    if (is_to_json) {
      all_tests.emplace_back(
          "to_json",
          QueryParamsWithResult({Value::Interval(test_case.first)},
                                values::Json(JSONValue(test_case.second)))
              .AddRequiredFeatures(required_features)
              .AddRequiredFeature(FEATURE_JSON_TYPE));
    } else {
      all_tests.emplace_back(
          "to_json_string",
          QueryParamsWithResult(
              {Value::Interval(test_case.first)},
              String(absl::StrCat("\"", test_case.second, "\"")))
              .AddRequiredFeatures(required_features));
    }
  }

  if (is_to_json) {
    all_tests.emplace_back(
        "to_json",
        QueryParamsWithResult({Value::NullInterval()},
                              Value::Value::Json(JSONValue()))
            .WrapWithFeatureSet({FEATURE_JSON_TYPE, FEATURE_INTERVAL_TYPE}));
  } else {
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({Value::NullInterval()}, String("null"))
            .AddRequiredFeature(FEATURE_INTERVAL_TYPE));
  }
}

// A helper function to add test cases for type Range to the result test
// set <all_tests>. If <is_to_json> is true, adds TO_JSON test cases. Otherwise
// adds TO_JSON_STRING test cases.
void AddRangeTestCases(bool is_to_json,
                       std::vector<FunctionTestCall>& all_tests) {
  Value datetime_start =
      Datetime(DatetimeValue::FromYMDHMSAndMicros(2024, 4, 23, 10, 00, 00, 99));
  Value datetime_end =
      Datetime(DatetimeValue::FromYMDHMSAndMicros(2024, 4, 24, 10, 00, 00, 99));
  const std::vector<std::tuple<Value, std::string, std::string>>
      range_test_cases = {
          {Range(Date(1), Date(2)), "1970-01-02", "1970-01-03"},
          {Range(NullDate(), Date(2)), "null", "1970-01-03"},
          {Range(Date(1), NullDate()), "1970-01-02", "null"},
          {Range(NullDate(), NullDate()), "null", "null"},
          {Range(datetime_start, datetime_end), "2024-04-23T10:00:00.000099",
           "2024-04-24T10:00:00.000099"},
          {Range(NullDatetime(), datetime_end), "null",
           "2024-04-24T10:00:00.000099"},
          {Range(datetime_start, NullDatetime()), "2024-04-23T10:00:00.000099",
           "null"},
          {Range(NullDatetime(), NullDatetime()), "null", "null"},
          {Range(Timestamp(1), Timestamp(2)), "1970-01-01T00:00:00.000001Z",
           "1970-01-01T00:00:00.000002Z"},
          {Range(NullTimestamp(), Timestamp(2)), "null",
           "1970-01-01T00:00:00.000002Z"},
          {Range(Timestamp(1), NullTimestamp()), "1970-01-01T00:00:00.000001Z",
           "null"},
          {Range(NullTimestamp(), NullTimestamp()), "null", "null"},
      };

  for (const auto& test_case : range_test_cases) {
    QueryParamsWithResult::FeatureSet required_features{FEATURE_RANGE_TYPE};
    const Value value = std::get<0>(test_case);
    std::string start = std::get<1>(test_case);
    if (start != "null") start = absl::StrCat("\"", start, "\"");
    std::string end = std::get<2>(test_case);
    if (end != "null") end = absl::StrCat("\"", end, "\"");
    if (value.type()->AsRange()->element_type()->kind() ==
        TypeKind::TYPE_DATETIME) {
      required_features.insert(FEATURE_CIVIL_TIME);
    }
    if (is_to_json) {
      all_tests.emplace_back(
          "to_json",
          QueryParamsWithResult(
              {value}, values::Json(*JSONValue::ParseJSONString(absl::StrCat(
                           R"({"end":)", end, R"(,"start":)", start, "}"))))
              .AddRequiredFeatures(required_features)
              .AddRequiredFeature(FEATURE_JSON_TYPE));
    } else {
      all_tests.emplace_back(
          "to_json_string",
          QueryParamsWithResult(
              {value}, String(absl::StrCat(R"({"start":)", start, R"(,"end":)",
                                           end, "}")))
              .AddRequiredFeatures(required_features));
    }
  }

  if (is_to_json) {
    all_tests.emplace_back(
        "to_json",
        QueryParamsWithResult({Value::Null(types::DateRangeType())},
                              Value::Value::Json(JSONValue()))
            .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_RANGE_TYPE}));
    all_tests.emplace_back(
        "to_json",
        QueryParamsWithResult({Value::Null(types::DatetimeRangeType())},
                              Value::Value::Json(JSONValue()))
            .AddRequiredFeatures(
                {FEATURE_JSON_TYPE, FEATURE_RANGE_TYPE, FEATURE_CIVIL_TIME}));
    all_tests.emplace_back(
        "to_json",
        QueryParamsWithResult({Value::Null(types::TimestampRangeType())},
                              Value::Value::Json(JSONValue()))
            .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_RANGE_TYPE}));
  } else {
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({Value::Null(types::DateRangeType())},
                              String("null"))
            .AddRequiredFeatures({
                FEATURE_RANGE_TYPE,
            }));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({Value::Null(types::DatetimeRangeType())},
                              String("null"))
            .AddRequiredFeatures({FEATURE_RANGE_TYPE, FEATURE_CIVIL_TIME}));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({Value::Null(types::TimestampRangeType())},
                              String("null"))
            .AddRequiredFeatures({FEATURE_RANGE_TYPE}));
  }
}

void AddUuidTestCases(bool is_to_json,
                      std::vector<FunctionTestCall>& all_tests) {
  struct UuidStringTestData {
    absl::string_view uuid_str;
    absl::string_view json_str;
  };
  constexpr UuidStringTestData kUuidTestCases[] = {
      {"00000000-0000-4000-8000-000000000000",
       "00000000-0000-4000-8000-000000000000"},
      {"9d3da323-4c20-360f-bd9b-ec54feec54f0",
       "9d3da323-4c20-360f-bd9b-ec54feec54f0"},
      {"ffffffff-ffff-4fff-8fff-ffffffffffff",
       "ffffffff-ffff-4fff-8fff-ffffffffffff"},
  };

  for (const auto& [uuid_str, json_str] : kUuidTestCases) {
    QueryParamsWithResult::FeatureSet required_features{FEATURE_UUID_TYPE};
    UuidValue uuid_value = UuidValue::FromString(uuid_str).value();
    if (is_to_json) {
      all_tests.emplace_back(
          "to_json", QueryParamsWithResult({Value::Uuid(uuid_value)},
                                           values::Json(JSONValue(json_str)))
                         .AddRequiredFeatures(required_features)
                         .AddRequiredFeature(FEATURE_JSON_TYPE));
    } else {
      all_tests.emplace_back(
          "to_json_string",
          QueryParamsWithResult({Value::Uuid(uuid_value)},
                                String(absl::StrCat("\"", json_str, "\"")))
              .AddRequiredFeatures(required_features));
    }
  }

  if (is_to_json) {
    all_tests.emplace_back(
        "to_json", QueryParamsWithResult({Value::NullUuid()},
                                         Value::Value::Json(JSONValue()))
                       .AddRequiredFeature(FEATURE_UUID_TYPE)
                       .AddRequiredFeature(FEATURE_JSON_TYPE));
  } else {
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({Value::NullUuid()}, String("null"))
            .AddRequiredFeature(FEATURE_UUID_TYPE));
  }
}

void AddJsonTestCases(bool is_to_json,
                      std::vector<FunctionTestCall>& all_tests) {
  constexpr absl::string_view kJsonValueString =
      R"({
  "array_val": [
    null,
    3,
    "world"
  ],
  "bool_val": true,
  "int64_val_1": 1,
  "int64_val_2": 2,
  "json_val": {
    "bool_val": false,
    "string_value": "hello"
  },
  "string_val": "foo"
})";
  const Value json_value =
      Json(JSONValue::ParseJSONString(kJsonValueString).value());
  // Clean up the string to make it the same as FORMAT %p output.
  std::string json_value_str(kJsonValueString);
  // Remove all spaces.
  RE2::GlobalReplace(&json_value_str, " *", "");
  // Remove all newlines.
  RE2::GlobalReplace(&json_value_str, "\n", "");

  // Tests for different escaping characters and embedded quotes.
  constexpr absl::string_view kEscapeCharsJsonValueString =
      R"({"int64_val'_1":1,)"
      R"("string_val_1":"foo'bar",)"
      R"("string_val_2":"foo''bar",)"
      R"("string_val_3":"foo`bar",)"
      R"("string_val_4":"foo``bar",)"
      R"("string_val_5":"foo\"bar",)"
      R"("string_val_6":"foo\\bar"})";
  const Value escape_chars_json_value =
      Json(JSONValue::ParseJSONString(kEscapeCharsJsonValueString).value());
  const std::string escape_chars_json_str(kEscapeCharsJsonValueString);

  const std::vector<std::pair<Value, std::string>> json_test_cases = {
      {values::Json(JSONValue(int64_t{1})), "1"},
      {values::Json(JSONValue(uint64_t{123})), "123"},
      {values::Json(JSONValue(std::string("string"))), "\"string\""},
      {json_value, json_value_str},
      {escape_chars_json_value, escape_chars_json_str}};

  for (const auto& test_case : json_test_cases) {
    all_tests.emplace_back(
        is_to_json ? "to_json" : "to_json_string",
        QueryParamsWithResult(
            {test_case.first},
            is_to_json ? test_case.first : values::String(test_case.second))
            .WrapWithFeatureSet({FEATURE_JSON_TYPE}));
  }

  constexpr absl::string_view kStructJson =
      R"({"x":"foo","y":10,"z":{"a":15,"b":[true,10]}})";
  const Value struct_json_value =
      Struct({"x", "y", "z"},
             {Json(JSONValue(std::string("foo"))), Int64(10),
              Json(JSONValue::ParseJSONString("{\"a\": 15, \"b\": [true, 10]}")
                       .value())});

  if (is_to_json) {
    all_tests.emplace_back(
        "to_json", QueryParamsWithResult(
                       {struct_json_value},
                       Json(JSONValue::ParseJSONString(kStructJson).value()))
                       .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json", QueryParamsWithResult(
                       {Value::UnvalidatedJsonString(R"({"a": 10})")},
                       Json(JSONValue::ParseJSONString(R"({"a": 10})").value()))
                       .WrapWithFeatureSet({FEATURE_JSON_TYPE}));
  } else {
    // Tests for TO_JSON_STRING with pretty_print argument.
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({values::Json(JSONValue(std::string("string"))),
                               values::Bool(true)},
                              values::String("\"string\""))
            .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult(
            {values::Json(JSONValue(std::string("hello\nworld"))),
             values::Bool(true)},
            values::String(R"("hello\nworld")"))
            .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({json_value, values::Bool(true)},
                              values::String(kJsonValueString))
            .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({struct_json_value, Bool(false)},
                              String(kStructJson))
            .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({struct_json_value, Bool(true)}, String(R"({
  "x": "foo",
  "y": 10,
  "z": {
    "a": 15,
    "b": [
      true,
      10
    ]
  }
})"))
            .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult({Array({Json(JSONValue(std::string("foo"))),
                                      Json(JSONValue::ParseJSONString(
                                               "{\"a\": 15, \"b\": [true, 10]}")
                                               .value())}),
                               Bool(true)},
                              String(R"([
  "foo",
  {
    "a": 15,
    "b": [
      true,
      10
    ]
  }
])"))
            .WrapWithFeature(FEATURE_JSON_TYPE));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult(
            {Value::UnvalidatedJsonString(R"({"a": 10})"), Bool(false)},
            String(R"({"a":10})"))
            .WrapWithFeatureSet({FEATURE_JSON_TYPE}));
    all_tests.emplace_back(
        "to_json_string",
        QueryParamsWithResult(
            {Value::UnvalidatedJsonString(R"({"a": 10})"), Bool(true)},
            String(R"({
  "a": 10
})"))
            .WrapWithFeatureSet({FEATURE_JSON_TYPE}));
  }
}

}  // namespace

// Gets TO_JSON_STRING test cases, including nano_timestamp test cases.
std::vector<FunctionTestCall> GetFunctionTestsToJsonString() {
  // These tests follow the order of the table in (broken link).
  std::vector<FunctionTestCall> all_tests = {
      {"to_json_string", {NullInt64()}, String("null")},
      {"to_json_string", {Int64(0), NullBool()}, NullString()},
      {"to_json_string", {NullInt64(), NullBool()}, NullString()},
      {"to_json_string", {NullString()}, String("null")},
      {"to_json_string", {NullDouble()}, String("null")},
      {"to_json_string", {NullTimestamp()}, String("null")},
      {"to_json_string", {NullDate()}, String("null")},
      {"to_json_string", {Null(Int64ArrayType())}, String("null")},
      {"to_json_string", {Null(EmptyStructType())}, String("null")},

      // BOOL
      {"to_json_string", {Bool(true)}, String("true")},
      {"to_json_string", {Bool(false)}, String("false")},

      // INT32, UINT32
      {"to_json_string", {Int32(0)}, String("0")},
      {"to_json_string", {Int32(-11)}, String("-11")},
      {"to_json_string", {Int32(132)}, String("132")},
      {"to_json_string", {Int32(int32min)}, String("-2147483648")},
      {"to_json_string", {Int32(int32max)}, String("2147483647")},
      {"to_json_string", {Uint32(0)}, String("0")},
      {"to_json_string", {Uint32(132)}, String("132")},
      {"to_json_string", {Uint32(uint32max)}, String("4294967295")},

      // INT64, UINT64
      // Values in [-9007199254740992, 9007199254740992] are not quoted.
      {"to_json_string", {Int64(0)}, String("0")},
      {"to_json_string", {Int64(10), Bool(true)}, String("10")},
      {"to_json_string", {Int64(10), Bool(false)}, String("10")},
      {"to_json_string", {Int64(-11)}, String("-11")},
      {"to_json_string", {Int64(132)}, String("132")},
      {"to_json_string", {Int64(int32min)}, String("-2147483648")},
      {"to_json_string",
       {Int64(int64_t{-9007199254740991})},
       String("-9007199254740991")},
      {"to_json_string",
       {Int64(int64_t{-9007199254740992})},
       String("-9007199254740992")},
      // Integers in the range of [-2^53, 2^53] can be represented losslessly as
      // a double-precision floating point number. Integers outside that range
      // are quoted.
      {"to_json_string",
       {Int64(int64_t{-9007199254740993})},
       String("\"-9007199254740993\"")},
      {"to_json_string",
       {Int64(int64_t{-12345678901234567})},
       String("\"-12345678901234567\"")},
      {"to_json_string", {Int64(int64min)}, String("\"-9223372036854775808\"")},
      {"to_json_string",
       {Int64(int64_t{9007199254740991})},
       String("9007199254740991")},
      {"to_json_string",
       {Int64(int64_t{9007199254740992})},
       String("9007199254740992")},
      {"to_json_string",
       {Int64(int64_t{9007199254740993})},
       String("\"9007199254740993\"")},
      {"to_json_string",
       {Int64(int64_t{12345678901234567})},
       String("\"12345678901234567\"")},
      {"to_json_string", {Int64(int64max)}, String("\"9223372036854775807\"")},
      {"to_json_string", {Uint64(0)}, String("0")},
      {"to_json_string", {Uint64(132)}, String("132")},
      {"to_json_string",
       {Uint64(uint64_t{9007199254740991})},
       String("9007199254740991")},
      {"to_json_string",
       {Uint64(uint64_t{9007199254740992})},
       String("9007199254740992")},
      {"to_json_string",
       {Uint64(uint64_t{9007199254740993})},
       String("\"9007199254740993\"")},
      {"to_json_string",
       {Uint64(uint64_t{12345678901234567})},
       String("\"12345678901234567\"")},
      {"to_json_string",
       {Uint64(uint64max)},
       String("\"18446744073709551615\"")},

      // FLOAT, DOUBLE
      // +Infinity, -Infinity, and Nan are represented as strings.
      {"to_json_string", {Float(0.0f)}, String("0")},
      {"to_json_string", {Float(0.0f), Bool(true)}, String("0")},
      {"to_json_string", {Float(0.0f), Bool(false)}, String("0")},
      {"to_json_string", {Float(-0.0f)}, String("0")},
      {"to_json_string", {Float(3.14f)}, String("3.14")},
      {"to_json_string", {Float(1.618034f)}, String("1.618034")},
      {"to_json_string", {Float(floatmin)}, String("-3.4028235e+38")},
      {"to_json_string", {Float(floatmax)}, String("3.4028235e+38")},
      {"to_json_string", {Float(floatminpositive)}, String("1.1754944e-38")},
      {"to_json_string", {Float(float_pos_inf)}, String("\"Infinity\"")},
      {"to_json_string", {Float(float_neg_inf)}, String("\"-Infinity\"")},
      {"to_json_string", {Float(float_nan)}, String("\"NaN\"")},
      {"to_json_string", {Float(float_neg_nan)}, String("\"NaN\"")},
      {"to_json_string", {Double(0.0)}, String("0")},
      {"to_json_string", {Double(-0.0)}, String("0")},
      {"to_json_string", {Double(3.14)}, String("3.14")},
      {"to_json_string",
       {Double(1.61803398874989)},
       String("1.61803398874989")},
      {"to_json_string",
       {Double(doublemin)},
       String("-1.7976931348623157e+308")},
      {"to_json_string",
       {Double(doublemax)},
       String("1.7976931348623157e+308")},
      {"to_json_string",
       {Double(doubleminpositive)},
       String("2.2250738585072014e-308")},
      {"to_json_string", {Double(double_pos_inf)}, String("\"Infinity\"")},
      {"to_json_string", {Double(double_neg_inf)}, String("\"-Infinity\"")},
      {"to_json_string", {Double(double_nan)}, String("\"NaN\"")},
      {"to_json_string", {Double(double_neg_nan)}, String("\"NaN\"")},
  };

  AddStringByteEnumDateTimestampTestCases(/*is_to_json=*/false, all_tests);
  AddArrayStructAndProtoTestCases(/*is_to_json=*/false, all_tests);
  AddCivilAndNanoTestCases(/*is_to_json=*/false, all_tests);
  AddIntervalValueTestCases(/*is_to_json=*/false, all_tests);
  AddJsonTestCases(/*is_to_json=*/false, all_tests);
  AddRangeTestCases(/*is_to_json=*/false, all_tests);
  AddUuidTestCases(/*is_to_json=*/false, all_tests);
  return all_tests;
}

// TODO: Unify primitive test cases when to_json_string supports
// stringify_wide_numbers if it keeps readability and reduces duplication.
// Gets TO_JSON test cases, including nano_timestamp test cases.
std::vector<FunctionTestCall> GetFunctionTestsToJson() {
  std::vector<FunctionTestCall> all_tests = {
      // NULL value
      {"to_json", {NullInt64()}, Value::Json(JSONValue())},
      {"to_json", {NullString()}, Value::Json(JSONValue())},
      {"to_json", {NullDouble()}, Value::Json(JSONValue())},
      {"to_json", {NullTimestamp()}, Value::Json(JSONValue())},
      {"to_json", {NullDate()}, Value::Json(JSONValue())},
      {"to_json", {Null(Int64ArrayType())}, Value::Json(JSONValue())},
      {"to_json", {Null(EmptyStructType())}, Value::Json(JSONValue())},

      {"to_json",
       QueryParamsWithResult({Int64(0), NullBool()}, NullJson())
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       QueryParamsWithResult({NullInt64(), NullBool()}, NullJson())
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},

      // BOOL
      {"to_json", {Bool(true)}, values::Json(JSONValue(true))},
      {"to_json", {Bool(false)}, values::Json(JSONValue(false))},

      // INT32, UINT32
      {"to_json", {Int32(0)}, values::Json(JSONValue(int64_t{0}))},
      {"to_json", {Int32(-11)}, values::Json(JSONValue(int64_t{-11}))},
      {"to_json", {Int32(132)}, values::Json(JSONValue(int64_t{132}))},
      {"to_json",
       {Int32(int32min)},
       values::Json(JSONValue(int64_t{-2147483648}))},
      {"to_json",
       {Int32(int32max)},
       values::Json(JSONValue(int64_t{2147483647}))},
      {"to_json", {Uint32(0)}, values::Json(JSONValue(int64_t{0}))},
      {"to_json", {Uint32(132)}, values::Json(JSONValue(int64_t{132}))},
      {"to_json",
       {Uint32(uint32max)},
       values::Json(JSONValue(int64_t{4294967295}))},

      // INT64, UINT64
      // Integers in the range of [-2^53, 2^53] can be represented
      // losslessly as a double-precision floating point number.
      // Integers outside that range are stored as string if
      // stringify_wide_number is true.
      {"to_json", {Int64(0)}, values::Json(JSONValue(int64_t{0}))},
      {"to_json",
       QueryParamsWithResult({Int64(10), Bool(true)},
                             values::Json(JSONValue(int64_t{10})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json", {Int64(10)}, values::Json(JSONValue(int64_t{10}))},
      {"to_json",
       {Int64(-2147483648)},
       values::Json(JSONValue(int64_t{-2147483648}))},
      {"to_json",
       {Int64(int64_t{-9007199254740991})},
       values::Json(JSONValue(int64_t{-9007199254740991}))},
      {"to_json",
       {Int64(int64_t{-9007199254740992})},
       values::Json(JSONValue(int64_t{-9007199254740992}))},
      {"to_json",
       QueryParamsWithResult({Int64(int64_t{9007199254740991}), true},
                             values::Json(JSONValue(int64_t{9007199254740991})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       QueryParamsWithResult({Int64(int64_t{9007199254740992}), true},
                             values::Json(JSONValue(int64_t{9007199254740992})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json", {Uint64(0)}, values::Json(JSONValue(uint64_t{0}))},
      {"to_json", {Uint64(132)}, values::Json(JSONValue(uint64_t{132}))},
      {"to_json",
       QueryParamsWithResult(
           {Uint64(uint64_t{9007199254740991}), true},
           values::Json(JSONValue(uint64_t{9007199254740991})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       QueryParamsWithResult(
           {Uint64(uint64_t{9007199254740992}), true},
           values::Json(JSONValue(uint64_t{9007199254740992})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},

      // Cases in pair where results differ with different stringify value.
      {"to_json",
       QueryParamsWithResult(
           {Int64(int64_t{-9007199254740993}), true},
           values::Json(JSONValue(std::string{"-9007199254740993"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       {Int64(int64_t{-9007199254740993})},
       values::Json(JSONValue(int64_t{-9007199254740993}))},

      {"to_json",
       QueryParamsWithResult(
           {Int64(int64_t{-12345678901234567}), true},
           values::Json(JSONValue(std::string{"-12345678901234567"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       {Int64(int64_t{-12345678901234567})},
       values::Json(JSONValue(int64_t{-12345678901234567}))},

      {"to_json",
       QueryParamsWithResult(
           {Int64(int64_t{12345678901234567}), true},
           values::Json(JSONValue(std::string{"12345678901234567"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       {Int64(int64_t{12345678901234567})},
       values::Json(JSONValue(int64_t{12345678901234567}))},

      {"to_json",
       QueryParamsWithResult(
           {Int64(int64min), true},
           values::Json(JSONValue(std::string{"-9223372036854775808"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json", {Int64(int64min)}, values::Json(JSONValue(int64min))},

      {"to_json",
       QueryParamsWithResult(
           {Int64(int64_t{9007199254740993}), true},
           values::Json(JSONValue(std::string{"9007199254740993"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       QueryParamsWithResult(
           {Int64(int64max), true},
           values::Json(JSONValue(std::string{"9223372036854775807"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json", {Int64(int64max)}, values::Json(JSONValue(int64max))},

      {"to_json",
       QueryParamsWithResult(
           {Uint64(uint64_t{9007199254740993}), true},
           values::Json(JSONValue(std::string{"9007199254740993"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       {Uint64(uint64_t{9007199254740993})},
       values::Json(JSONValue(uint64_t{9007199254740993}))},

      {"to_json",
       QueryParamsWithResult(
           {Uint64(uint64_t{12345678901234567}), true},
           values::Json(JSONValue(std::string{"12345678901234567"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json",
       {Uint64(uint64_t{12345678901234567})},
       values::Json(JSONValue(uint64_t{12345678901234567}))},

      {"to_json",
       QueryParamsWithResult(
           {Uint64(uint64max), true},
           values::Json(JSONValue(std::string{"18446744073709551615"})))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json", {Uint64(uint64max)}, values::Json(JSONValue(uint64max))},

      // FLOAT, DOUBLE
      // +Infinity, -Infinity, and Nan are stored as corresponding strings in
      // JsonValue.
      {"to_json", {Float(0.0f)}, values::Json(JSONValue(0.0))},
      {"to_json",
       QueryParamsWithResult({Float(0.0f), Bool(true)},
                             values::Json(JSONValue(0.0)))
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})},
      {"to_json", {Float(0.0f)}, values::Json(JSONValue(0.0))},
      {"to_json", {Float(-0.0f)}, values::Json(JSONValue(0.0))},
      {"to_json",
       {Float(3.14f)},
       values::Json(JSONValue(3.14f)),
       kDefaultFloatMargin},
      {"to_json",
       {Float(1.618034f)},
       values::Json(JSONValue(1.618034f)),
       kDefaultFloatMargin},
      {"to_json", {Float(floatmin)}, values::Json(JSONValue(floatmin))},
      {"to_json", {Float(floatmax)}, values::Json(JSONValue(floatmax))},
      {"to_json",
       {Float(float_pos_inf)},
       values::Json(JSONValue(std::string("Infinity")))},
      {"to_json",
       {Float(float_neg_inf)},
       values::Json(JSONValue(std::string("-Infinity")))},
      {"to_json",
       {Float(float_nan)},
       values::Json(JSONValue(std::string("NaN")))},
      {"to_json",
       {Float(float_neg_nan)},
       values::Json(JSONValue(std::string("NaN")))},

      {"to_json", {Double(0.0)}, values::Json(JSONValue(0.0))},
      {"to_json", {Double(-0.0)}, values::Json(JSONValue(0.0))},
      {"to_json",
       {Double(3.14)},
       values::Json(JSONValue(3.14)),
       kDefaultFloatMargin},
      {"to_json",
       {Double(1.61803398874989)},
       values::Json(JSONValue(1.61803398874989)),
       kDefaultFloatMargin},
      {"to_json",
       {Double(doublemin)},
       values::Json(JSONValue(doublemin)),
       kDefaultFloatMargin},
      {"to_json",
       {Double(doublemax)},
       values::Json(JSONValue(doublemax)),
       kDefaultFloatMargin},
      {"to_json",
       {Double(doubleminpositive)},
       values::Json(JSONValue(doubleminpositive)),
       kDefaultFloatMargin},
      {"to_json",
       {Double(double_pos_inf)},
       values::Json(JSONValue(std::string("Infinity")))},
      {"to_json",
       {Double(double_neg_inf)},
       values::Json(JSONValue(std::string("-Infinity")))},
      {"to_json",
       {Double(double_nan)},
       values::Json(JSONValue(std::string("NaN")))},
      {"to_json",
       {Double(double_neg_nan)},
       values::Json(JSONValue(std::string("NaN")))},

      // STRING, BYTE, ENUM, TIMESTAMP tests are defined below.
      // DATETIME and TIME tests are defined below.

      // JSON
      {"to_json", {NullJson()}, Value::Json(JSONValue())},
      {"to_json",
       {values::Json(JSONValue(true))},
       values::Json(JSONValue(true))},
      {"to_json",
       {values::Json(JSONValue(int64_t{123456}))},
       values::Json(JSONValue(int64_t{123456}))},
      {"to_json",
       {values::Json(JSONValue(std::string{"abc"}))},
       values::Json(JSONValue(std::string{"abc"}))},
      {"to_json",
       {values::Json(
           JSONValue::ParseJSONString(R"(["foo","","bar"])").value())},
       values::Json(JSONValue::ParseJSONString(R"(["foo","","bar"])").value())},
  };

  AddStringByteEnumDateTimestampTestCases(/*is_to_json=*/true, all_tests);
  AddArrayStructAndProtoTestCases(/*is_to_json=*/true, all_tests);
  AddCivilAndNanoTestCases(/*is_to_json=*/true, all_tests);
  AddIntervalValueTestCases(/*is_to_json=*/true, all_tests);
  AddJsonTestCases(/*is_to_json=*/true, all_tests);
  AddRangeTestCases(/*is_to_json=*/true, all_tests);
  AddUuidTestCases(/*is_to_json=*/true, all_tests);

  return all_tests;
}

std::vector<FunctionTestCall> GetFunctionTestsSafeToJson() {
  std::vector<FunctionTestCall> all_tests;
  for (auto& test : GetFunctionTestsToJson()) {
    const bool stringify_wide_numbers = test.params.num_params() == 2 &&
                                        !test.params.param(1).is_null() &&
                                        test.params.param(1).bool_value();
    // SAFE_TO_JSON internally sets stringify_wide_numbers=>true
    if (!stringify_wide_numbers) {
      continue;
    }
    test.function_name = "safe_to_json";
    test.params.RemoveRequiredFeature(FEATURE_NAMED_ARGUMENTS);
    all_tests.push_back(std::move(test));
  }
  ABSL_DCHECK_GE(all_tests.size(), 1);
  return all_tests;
}

}  // namespace googlesql
