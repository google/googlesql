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

// Tests for TO_JSON functions.
#include "googlesql/public/functions/to_json.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/wrappers.pb.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/unsupported_fields.pb.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "googlesql/base/map_util.h"

namespace googlesql {
namespace functions {
namespace {

constexpr UnsupportedFieldsEnum::UnsupportedFields kUnsupportFieldsDefault =
    UnsupportedFieldsEnum::FAIL;
constexpr absl::StatusCode kUnimplemented = absl::StatusCode::kUnimplemented;

TEST(ToJsonTest, Compliance) {
  const std::vector<FunctionTestCall> tests = GetFunctionTestsToJson();
  const QueryParamsWithResult::FeatureSet default_feature_set = {
      FEATURE_CIVIL_TIME, FEATURE_TIMESTAMP_NANOS};

  for (const FunctionTestCall& test : tests) {
    if (std::any_of(test.params.params().begin(), test.params.params().end(),
                    [](const Value& param) { return param.is_null(); })) {
      continue;
    }
    const Value& input_value = test.params.param(0);
    const bool stringify_wide_numbers = test.params.params().size() == 2
                                            ? test.params.param(1).bool_value()
                                            : false;
    SCOPED_TRACE(absl::Substitute("$0('$1', '$2')", test.function_name,
                                  input_value.ShortDebugString(),
                                  stringify_wide_numbers));
    googlesql::LanguageOptions language_options;
    if (test.params.results().size() == 1 &&
        googlesql_base::ContainsKey(test.params.results().begin()->first,
                         FEATURE_JSON_STRICT_NUMBER_PARSING)) {
      language_options.EnableLanguageFeature(
          FEATURE_JSON_STRICT_NUMBER_PARSING);
    }
    absl::StatusOr<JSONValue> output =
        ToJson(input_value, stringify_wide_numbers, language_options,
               /*canonicalize_zero=*/true);

    // If the test is conditioned on civil time with nanos, use that result.
    // Otherwise just use the default result.
    const QueryParamsWithResult::Result* result =
        googlesql_base::FindOrNull(test.params.results(), default_feature_set);
    const Value expected_result_value =
        result == nullptr ? test.params.results().begin()->second.result
                          : result->result;
    const absl::Status expected_status =
        result == nullptr ? test.params.results().begin()->second.status
                          : result->status;
    FloatMargin margin =
        result == nullptr ? test.params.results().begin()->second.float_margin
                          : result->float_margin;

    if (expected_status.ok()) {
      if (margin.IsExactEquality()) {
        EXPECT_EQ(expected_result_value,
                  values::Json(std::move(output.value())));
      } else {
        // For float and double, allow margin comparison.
        InternalValue::Equals(
            expected_result_value, values::Json(std::move(output.value())),
            ValueEqualityCheckOptions{.float_margin = margin});
      }
    } else {
      EXPECT_EQ(output.status().code(), expected_status.code());
    }
  }
}

TEST(ToJsonTest, LegacyCanonicalizeZeroDouble) {
  googlesql::LanguageOptions language_options;

  absl::StatusOr<JSONValue> output = ToJson(
      Value::Double(-0.0), /*stringify_wide_numbers=*/false, language_options,
      /*canonicalize_zero=*/false, kUnsupportFieldsDefault);
  GOOGLESQL_ASSERT_OK(output);
  EXPECT_EQ(values::Json(std::move(output.value())),
            values::Json(JSONValue(-0.0)));
}

TEST(ToJsonTest, LegacyCanonicalizeZeroFloat) {
  googlesql::LanguageOptions language_options;
  absl::StatusOr<JSONValue> output = ToJson(
      Value::Float(-0.0f), /*stringify_wide_numbers=*/false, language_options,
      /*canonicalize_zero=*/false, kUnsupportFieldsDefault);
  GOOGLESQL_ASSERT_OK(output);
  EXPECT_EQ(values::Json(std::move(output.value())),
            values::Json(JSONValue(-0.0)));
}

TEST(ToJsonTest, GraphNode) {
  googlesql::LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_ELEMENT_DEFINITION_NAME_IN_JSON_RESULT);
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node,
      test_values::GraphNode({"graph_name"}, "id",
                             {{"P0", p0_value}, {"p1", p1_value}},
                             {"label_2", "label_1"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue output,
      ToJson(node, /*stringify_wide_numbers=*/false, language_options,
             /*canonicalize_zero=*/false, kUnsupportFieldsDefault));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expectation,
                       JSONValue::ParseJSONString(absl::Substitute(
                           R"json({
    "element_definition_name":"ElementTable",
    "identifier": "$0",
    "kind": "node",
    "labels": ["label_1", "label_2"],
    "properties": {
      "P0": "v0",
      "p1": 1
    }
  })json",
                           absl::Base64Escape("id"))));
  EXPECT_EQ(values::Json(std::move(output)),
            values::Json(std::move(expectation)));
}

TEST(ToJsonTest, GraphEdge) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_ELEMENT_DEFINITION_NAME_IN_JSON_RESULT);
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge,
      test_values::GraphEdge({"graph_name"}, "id",
                             {{"P0", p0_value}, {"p1", p1_value}},
                             {"label_2", "label_1"}, "ElementTable",
                             "src_node_id", "dst_node_id"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue output,
      ToJson(edge, /*stringify_wide_numbers=*/false, language_options,
             /*canonicalize_zero=*/false, kUnsupportFieldsDefault));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue expectation,
      JSONValue::ParseJSONString(absl::Substitute(
          R"json({
    "element_definition_name":"ElementTable",
    "identifier": "$0",
    "kind": "edge",
    "labels": ["label_1", "label_2"],
    "properties": {
      "P0": "v0",
      "p1": 1
    },
    "source_node_identifier": "$1",
    "destination_node_identifier": "$2"
  })json",
          absl::Base64Escape("id"), absl::Base64Escape("src_node_id"),
          absl::Base64Escape("dst_node_id"))));
  EXPECT_EQ(values::Json(std::move(output)),
            values::Json(std::move(expectation)));
}

TEST(ToJsonTest, DynamicGraphNode) {
  googlesql::LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_ELEMENT_DEFINITION_NAME_IN_JSON_RESULT);
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue dynamic_properties,
                       JSONValue::ParseJSONString(R"json({
    "P1": "random string value",
    "p3": 3
  })json"));
  const Value node = test_values::DynamicGraphNode(
      {"graph_name"}, "id", {{"P0", p0_value}, {"p1", p1_value}},
      dynamic_properties.GetConstRef(), /*static_labels=*/{"label_1"},
      /*dynamic_labels=*/{"label_1", "lABeL_1", "lABeL_2"}, "ElementTable");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue output,
      ToJson(node, /*stringify_wide_numbers=*/false, language_options,
             /*canonicalize_zero=*/false, kUnsupportFieldsDefault));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expectation,
                       JSONValue::ParseJSONString(absl::Substitute(
                           R"json({
    "element_definition_name":"ElementTable",
    "identifier": "$0",
    "kind": "node",
    "labels": ["label_1", "lABeL_2"],
    "properties": {
      "P0": "v0",
      "p1": 1,
      "p3": 3
    }
  })json",
                           absl::Base64Escape("id"))));
  EXPECT_EQ(values::Json(std::move(output)),
            values::Json(std::move(expectation)));
}

TEST(ToJsonTest, DynamicGraphEdge) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_ELEMENT_DEFINITION_NAME_IN_JSON_RESULT);
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue dynamic_properties,
                       JSONValue::ParseJSONString(R"json({
    "P1": "random string value",
    "p3": 3
  })json"));
  const Value edge = test_values::DynamicGraphEdge(
      {"graph_name"}, "id", {{"P0", p0_value}, {"p1", p1_value}},
      dynamic_properties.GetConstRef(),
      /*static_labels=*/{"label_1"},
      /*dynamic_labels=*/{"label_1", "lABeL_1", "lABeL_2"}, "ElementTable",
      "src_node_id", "dst_node_id");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue output,
      ToJson(edge, /*stringify_wide_numbers=*/false, language_options,
             /*canonicalize_zero=*/false, kUnsupportFieldsDefault));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue expectation,
      JSONValue::ParseJSONString(absl::Substitute(
          R"json({
    "element_definition_name":"ElementTable",
    "identifier": "$0",
    "kind": "edge",
    "labels": ["label_1", "lABeL_2"],
    "properties": {
      "P0": "v0",
      "p1": 1,
      "p3": 3
    },
    "source_node_identifier": "$1",
    "destination_node_identifier": "$2"
  })json",
          absl::Base64Escape("id"), absl::Base64Escape("src_node_id"),
          absl::Base64Escape("dst_node_id"))));
  EXPECT_EQ(values::Json(std::move(output)),
            values::Json(std::move(expectation)));
}

TEST(ToJsonTest, GraphPath) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(
      FEATURE_SQL_GRAPH_ELEMENT_DEFINITION_NAME_IN_JSON_RESULT);
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node1,
      test_values::GraphNode({"graph_name"}, "src_node_id",
                             {{"P0", p0_value}, {"p1", p1_value}},
                             {"label_2", "label_1"}, "ElementTable"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value edge,
      test_values::GraphEdge({"graph_name"}, "id",
                             {{"P0", p0_value}, {"p1", p1_value}},
                             {"label_2", "label_1"}, "ElementTable",
                             "src_node_id", "dst_node_id"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value node2,
      test_values::GraphNode({"graph_name"}, "dst_node_id",
                             {{"P0", p0_value}, {"p1", p1_value}},
                             {"label_2", "label_1"}, "ElementTable"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value path,
                       Value::MakeGraphPath(test_values::MakeGraphPathType(
                                                node1.type()->AsGraphElement(),
                                                edge.type()->AsGraphElement()),
                                            {node1, edge, node2}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue output,
      ToJson(path, /*stringify_wide_numbers=*/false, language_options,
             /*canonicalize_zero=*/false, kUnsupportFieldsDefault));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue expectation,
      JSONValue::ParseJSONString(absl::Substitute(
          R"json([
    {
      "element_definition_name":"ElementTable",
      "identifier": "$0",
      "kind": "node",
      "labels": ["label_1", "label_2"],
      "properties": {
        "P0": "v0",
        "p1": 1
      }
    },
    {
      "element_definition_name":"ElementTable",
      "identifier": "$1",
      "kind": "edge",
      "labels": ["label_1", "label_2"],
      "properties": {
        "P0": "v0",
        "p1": 1
      },
      "source_node_identifier": "$2",
      "destination_node_identifier": "$3"
    },
    {
      "element_definition_name":"ElementTable",
      "identifier": "$4",
      "kind": "node",
      "labels": ["label_1", "label_2"],
      "properties": {
        "P0": "v0",
        "p1": 1
      }
    }
  ])json",
          absl::Base64Escape("src_node_id"), absl::Base64Escape("id"),
          absl::Base64Escape("src_node_id"), absl::Base64Escape("dst_node_id"),
          absl::Base64Escape("dst_node_id"))));

  EXPECT_EQ(values::Json(std::move(output)),
            values::Json(std::move(expectation)));
}

struct PicosTestCase {
  std::string input_string;
  std::string expected_string;
};

using PicosTest = ::testing::TestWithParam<PicosTestCase>;

TEST_P(PicosTest, ToJson) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_TIMESTAMP_PICOS);

  const PicosTestCase& test_case = GetParam();

  PicoTime pico_time;
  GOOGLESQL_ASSERT_OK(functions::ConvertStringToTimestamp(
      test_case.input_string, absl::UTCTimeZone(), /*allow_tz_in_str=*/true,
      &pico_time));
  const Value v = Value::Timestamp(TimestampPicosValue(pico_time));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue result,
      ToJson(v, /*stringify_wide_numbers=*/false, language_options,
             /*canonicalize_zero=*/false, kUnsupportFieldsDefault));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue expectation,
                       JSONValue::ParseJSONString(absl::Substitute(
                           R"json("$0")json", test_case.expected_string)));

  EXPECT_EQ(values::Json(std::move(result)),
            values::Json(std::move(expectation)));
}

INSTANTIATE_TEST_SUITE_P(
    PicosTestSuite, PicosTest,
    ::testing::ValuesIn<PicosTestCase>({
        {"2017-06-25 12:34:56.123456", "2017-06-25T12:34:56.123456Z"},

        {"2017-06-25 05:13:00", "2017-06-25T05:13:00Z"},

        {"2017-06-25 23:34:56.123456789123",
         "2017-06-25T23:34:56.123456789123Z"},

        {"2017-06-25 23:34:56.12345678912",
         "2017-06-25T23:34:56.123456789120Z"},

        {"2017-06-25 23:34:56.1234567891", "2017-06-25T23:34:56.123456789100Z"},

        {"2017-06-25 23:34:56.123456789", "2017-06-25T23:34:56.123456789Z"},

        {"2017-06-25 23:34:56.12345678", "2017-06-25T23:34:56.123456780Z"},

        {"2017-06-25 23:34:56.1234567", "2017-06-25T23:34:56.123456700Z"},

        {"2017-06-25 23:34:56.123456", "2017-06-25T23:34:56.123456Z"},

        {"2017-06-25 12:34:56.12345", "2017-06-25T12:34:56.123450Z"},

        {"2017-06-25 12:34:56.123", "2017-06-25T12:34:56.123Z"},

        {"2017-06-25 12:34:56.12", "2017-06-25T12:34:56.120Z"},

        {"2017-06-25 12:34:56.1", "2017-06-25T12:34:56.100Z"},

        {"2017-06-25 12:34:00", "2017-06-25T12:34:00Z"},

        {"2017-06-25 12:00:00", "2017-06-25T12:00:00Z"},

        {"1918-11-11", "1918-11-11T00:00:00Z"},

        {"0001-01-01", "0001-01-01T00:00:00Z"},

        {"9999-12-31 23:59:59.999999999999",
         "9999-12-31T23:59:59.999999999999Z"},
    }));

}  // namespace
}  // namespace functions
}  // namespace googlesql
