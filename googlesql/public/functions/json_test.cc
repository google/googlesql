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

// Tests for the GoogleSQL JSON functions.

#include "googlesql/public/functions/json.h"

#include <stddef.h>

#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/functions/json_internal.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"

namespace googlesql {
namespace functions {
namespace {

using json_internal::JsonPathOptions;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNan;
using ::testing::Optional;
using ::testing::Pointwise;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

MATCHER_P(JsonEq, expected, expected.ToString()) {
  *result_listener << arg.ToString();
  return arg.NormalizedEquals(expected);
}

MATCHER(JsonEq, "") {
  JSONValueConstRef actual = std::get<0>(arg);
  JSONValueConstRef expected = std::get<1>(arg);
  *result_listener << "where the actual value is " << actual.ToString()
                   << " and the expected value is " << expected.ToString();
  return actual.NormalizedEquals(expected);
}

// Note that the compliance tests below are more exhaustive.
TEST(JsonTest, StringJsonExtract) {
  const std::string json =
      R"({"a": {"b": [ { "c" : "foo" } ], "d": {"b\"ar": "q\"w"} } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      // This output contains an unescaped key and value because escaping is
      // disabled.
      {"$.a", R"({"b":[{"c":"foo"}],"d":{"b"ar":"q"w"}})"},
      {"$.a.b", R"([{"c":"foo"}])"},
      {"$.a.b[0]", R"({"c":"foo"})"},
      {"$.a.b[0].c", R"("foo")"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')", json,
                                  input_and_output.first));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input_and_output.first,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));
    std::string value;
    bool is_null;
    bool is_warning_called = false;
    GOOGLESQL_ASSERT_OK(evaluator->Extract(
        json, &value, &is_null,
        [&](absl::Status status) { is_warning_called = true; }));
    EXPECT_FALSE(is_null);
    EXPECT_FALSE(is_warning_called);
    EXPECT_EQ(input_and_output.second, value);
  }
}

TEST(JsonTest, StringJsonExtractKeyEscapingDisabled) {
  const std::string json =
      R"({"foo": {"b\"ar": "q\"w"}, "foo_array": [{"b\"ar": "q\"w"}] })";
  {
    // Only enable value escaping and not key escaping. This should result in
    // unescaped keys.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            "$.foo",
            /*sql_standard_mode=*/true,
            /*enable_special_character_escaping_in_values=*/true,
            /*enable_special_character_escaping_in_keys=*/false));
    std::string value;
    bool is_null;
    absl::Status result_status = absl::OkStatus();
    GOOGLESQL_ASSERT_OK(evaluator->Extract(
        json, &value, &is_null,
        [&](absl::Status status) { result_status = status; }));
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(!result_status.ok());
    EXPECT_EQ(R"({"b"ar":"q\"w"})", value);
  }
  {
    // Only enable value escaping and not key escaping. This should result in
    // unescaped keys.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            "$.foo_array",
            /*sql_standard_mode=*/true,
            /*enable_special_character_escaping_in_values=*/true,
            /*enable_special_character_escaping_in_keys=*/false));
    std::vector<std::string> result;
    bool is_null;
    absl::Status result_status = absl::OkStatus();
    GOOGLESQL_ASSERT_OK(evaluator->ExtractArray(
        json, &result, &is_null,
        [&](absl::Status status) { result_status = status; }));
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(!result_status.ok());
    EXPECT_THAT(result, ElementsAre(R"({"b"ar":"q\"w"})"));
  }
}

TEST(JsonTest, StringJsonExtractKeyEscapingEnabledValueDisabled) {
  const std::string json =
      R"({"foo": {"b\"ar": "q\"w"}, "foo_array": [{"b\"ar": "q\"w"}] })";
  {
    // Only enable key escaping. Because value escaping is not enabled there
    // should be no escaping of keys or values.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            "$.foo",
            /*sql_standard_mode=*/true,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/true));
    std::string value;
    bool is_null;
    bool is_warning_called = false;
    GOOGLESQL_ASSERT_OK(evaluator->Extract(
        json, &value, &is_null,
        [&](absl::Status status) { is_warning_called = true; }));
    EXPECT_FALSE(is_null);
    EXPECT_FALSE(is_warning_called);
    GOOGLESQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null,
                                 [&is_warning_called](absl::Status status) {
                                   is_warning_called = true;
                                 }));
    EXPECT_FALSE(is_null);
    EXPECT_FALSE(is_warning_called);
    EXPECT_EQ(value, R"({"b"ar":"q"w"})");
  }
  {
    // Only enable key escaping. Because value escaping is not enabled there
    // should be no escaping of keys or values.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            "$.foo_array",
            /*sql_standard_mode=*/true,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/true));
    std::vector<std::string> result;
    bool is_null;
    bool is_warning_called = false;
    GOOGLESQL_ASSERT_OK(evaluator->ExtractArray(
        json, &result, &is_null, [&is_warning_called](absl::Status status) {
          is_warning_called = true;
        }));
    EXPECT_FALSE(is_null);
    EXPECT_FALSE(is_warning_called);
    EXPECT_THAT(result, ElementsAre(R"({"b"ar":"q"w"})"));
  }
}

TEST(JsonTest, StringJsonExtractKeyAndValueEscapingEnabled) {
  const std::string json =
      R"({"foo": {"b\"ar": "q\"w"}, "foo_array": [{"b\"ar": "q\"w"}] })";
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            "$.foo",
            /*sql_standard_mode=*/true,
            /*enable_special_character_escaping_in_values=*/true,
            /*enable_special_character_escaping_in_keys=*/true));
    std::string value;
    bool is_null;
    bool is_warning_called = false;
    GOOGLESQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null,
                                 [&is_warning_called](absl::Status status) {
                                   is_warning_called = true;
                                 }));
    EXPECT_FALSE(is_null);
    EXPECT_FALSE(is_warning_called);
    EXPECT_EQ(R"({"b\"ar":"q\"w"})", value);
  }
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            "$.foo_array",
            /*sql_standard_mode=*/true,
            /*enable_special_character_escaping_in_values=*/true,
            /*enable_special_character_escaping_in_keys=*/true));
    std::vector<std::string> result;
    bool is_null;
    bool is_warning_called = false;
    GOOGLESQL_ASSERT_OK(evaluator->ExtractArray(
        json, &result, &is_null, [&is_warning_called](absl::Status status) {
          is_warning_called = true;
        }));
    EXPECT_FALSE(is_null);
    EXPECT_FALSE(is_warning_called);
    EXPECT_THAT(result, ElementsAre(R"({"b\"ar":"q\"w"})"));
  }
}

class MockEscapingNeededCallback {
 public:
  MOCK_METHOD(void, Call, (absl::string_view));
};

TEST(JsonTest, JsonEscapingNeededCallback) {
  const std::string json = R"({"a": {"b": [ { "c" : "\t" } ] } })";
  const std::string input = "$.a.b[0].c";
  const std::string output = "\"\t\"";

  SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')", json, input));
  MockEscapingNeededCallback callback;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<JsonPathEvaluator> evaluator,
      JsonPathEvaluator::Create(
          input,
          /*sql_standard_mode=*/false,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));
  evaluator->set_escaping_needed_callback(
      [&](absl::string_view str, bool is_key) {
        callback.Call(str);
        EXPECT_FALSE(is_key);
      });
  EXPECT_CALL(callback, Call("\t"));
  std::string value;
  bool is_null;
  GOOGLESQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null));
  EXPECT_EQ(output, value);
  EXPECT_FALSE(is_null);
}

TEST(JsonTest, JsonKeyEscapingNeededCallback) {
  // This json contains an unescaped key.
  const std::string json = R"({"a": {"b": [ { "c\"ar" : "t" } ] } })";
  const std::string input = "$.a.b[0]";
  // b/265948860: When escaping special characters in keys, this output
  // should be: R"({"c\"ar":"t"})".
  const std::string output = R"({"c"ar":"t"})";

  SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')", json, input));
  MockEscapingNeededCallback callback;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<JsonPathEvaluator> evaluator,
      JsonPathEvaluator::Create(
          input,
          /*sql_standard_mode=*/false,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));
  evaluator->set_escaping_needed_callback(
      [&](absl::string_view str, bool is_key) {
        callback.Call(str);
        EXPECT_TRUE(is_key);
      });
  EXPECT_CALL(callback, Call("c\"ar"));
  std::string value;
  bool is_null;
  GOOGLESQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null));
  EXPECT_EQ(output, value);
  EXPECT_FALSE(is_null);
}

TEST(JsonTest, NativeJsonExtract) {
  const JSONValue json =
      JSONValue::ParseJSONString(R"({"a": {"b": [ { "c" : "foo" } ] } })")
          .value();
  JSONValueConstRef json_ref = json.GetConstRef();
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$", R"({"a":{"b":[{"c":"foo"}]}})"},
      {"$.a", R"({"b":[{"c":"foo"}]})"},
      {"$.a.b", R"([{"c":"foo"}])"},
      {"$.a.b[0]", R"({"c":"foo"})"},
      {"$.a.b[0].c", R"("foo")"}};
  for (const auto& [input, output] : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')",
                                  json_ref.ToString(), input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));

    std::optional<JSONValueConstRef> result = evaluator->Extract(json_ref);
    EXPECT_TRUE(result.has_value());
    if (result.has_value()) {
      EXPECT_THAT(
          result.value(),
          JsonEq(JSONValue::ParseJSONString(output).value().GetConstRef()));
    }
  }
}

TEST(JsonTest, StringJsonExtractScalar) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$", ""},
      {"$.a", ""},
      {"$.a.b", ""},
      {"$.a.b[0]", ""},
      {"$.a.b[0].c", "foo"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_SCALAR('$0', '$1')", json,
                                  input_and_output.first));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input_and_output.first,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));
    std::string value;
    bool is_null;
    GOOGLESQL_ASSERT_OK(evaluator->ExtractScalar(json, &value, &is_null));
    if (!input_and_output.second.empty()) {
      EXPECT_EQ(input_and_output.second, value);
      EXPECT_FALSE(is_null);
    } else {
      EXPECT_TRUE(is_null);
    }
  }
}

TEST(JsonTest, NativeJsonExtractScalar) {
  const JSONValue json =
      JSONValue::ParseJSONString(
          R"({"a": {"b": [ { "c" : "foo" } ], "d": 1, "e": -5, )"
          R"("f": true, "g": 4.2 } })")
          .value();
  JSONValueConstRef json_ref = json.GetConstRef();
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$", ""},       {"$.a", ""},       {"$.a.d", "1"},
      {"$.a.e", "-5"}, {"$.a.f", "true"}, {"$.a.g", "4.2"},
      {"$.a.b", ""},   {"$.a.b[0]", ""},  {"$.a.b[0].c", "foo"}};
  for (const auto& [input, output] : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_SCALAR('$0', '$1')",
                                  json_ref.ToString(), input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));

    std::optional<std::string> result = evaluator->ExtractScalar(json_ref);
    if (!output.empty()) {
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(output, result.value());
    } else {
      EXPECT_FALSE(result.has_value());
    }
  }
}

TEST(JsonTest, NativeJsonExtractJsonArray) {
  auto json_value = JSONValue::ParseJSONString(
      R"({"a": {"b": [ { "c" : "foo" }, 15, null, "bar", )"
      R"([ 20, { "a": "baz" } ] ] } })");
  GOOGLESQL_ASSERT_OK(json_value.status());
  JSONValueConstRef json_ref = json_value->GetConstRef();

  const std::vector<
      std::pair<std::string, std::optional<std::vector<std::string>>>>
      inputs_and_outputs = {{"$", std::nullopt},
                            {"$.a", std::nullopt},
                            {"$.a.b",
                             {{R"({"c":"foo"})", "15", "null", "\"bar\"",
                               R"([20,{"a":"baz"}])"}}},
                            {"$.a.b[0]", std::nullopt},
                            {"$.a.b[4]", {{"20", R"({"a":"baz"})"}}}};
  for (const auto& [input, output] : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_ARRAY('$0', '$1')",
                                  json_ref.ToString(), input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));

    std::optional<std::vector<JSONValueConstRef>> result =
        evaluator->ExtractArray(json_ref);
    if (output.has_value()) {
      ASSERT_TRUE(result.has_value());

      std::vector<JSONValue> json_value_store;
      std::vector<::testing::Matcher<JSONValueConstRef>> expected_result;
      json_value_store.reserve(output->size());
      expected_result.reserve(output->size());
      for (const std::string& string_value : *output) {
        json_value_store.emplace_back();
        GOOGLESQL_ASSERT_OK_AND_ASSIGN(json_value_store.back(),
                             JSONValue::ParseJSONString(string_value));
        expected_result.push_back(
            JsonEq(json_value_store.back().GetConstRef()));
      }

      EXPECT_THAT(*result, ElementsAreArray(expected_result));
    } else {
      EXPECT_FALSE(result.has_value());
    }
  }
}

TEST(JsonTest, NativeJsonExtractStringArray) {
  auto json_value = JSONValue::ParseJSONString(
      R"({"a": {"b": [ { "c" : "foo" }, 15, null, "bar", )"
      R"([ 20, "a", true ] ] } })");
  GOOGLESQL_ASSERT_OK(json_value.status());
  JSONValueConstRef json_ref = json_value->GetConstRef();
  const std::vector<
      std::pair<std::string, std::optional<std::vector<std::string>>>>
      inputs_and_outputs = {{"$", std::nullopt},
                            {"$.a", std::nullopt},
                            {"$.a.b", std::nullopt},
                            {"$.a.b[0]", std::nullopt},
                            {"$.a.b[4]", {{"20", "a", "true"}}}};
  for (const auto& [input, output] : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_STRING_ARRAY('$0', '$1')",
                                  json_ref.ToString(), input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));
    std::optional<std::vector<std::optional<std::string>>> result =
        evaluator->ExtractStringArray(json_ref);
    if (output.has_value()) {
      ASSERT_TRUE(result.has_value());
      EXPECT_THAT(*result, ::testing::Pointwise(::testing::Eq(), *output));
    } else {
      EXPECT_FALSE(result.has_value());
    }
  }
}

void ExpectExtractScalar(absl::string_view json, absl::string_view path,
                         absl::string_view expected) {
  SCOPED_TRACE(absl::Substitute("JSON_EXTRACT_SCALAR('$0', '$1')", json, path));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<JsonPathEvaluator> evaluator,
      JsonPathEvaluator::Create(
          path, /*sql_standard_mode=*/true,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));
  std::string value;
  bool is_null;
  GOOGLESQL_ASSERT_OK(evaluator->ExtractScalar(json, &value, &is_null));
  if (!expected.empty()) {
    EXPECT_EQ(expected, value);
    EXPECT_FALSE(is_null);
  } else {
    EXPECT_TRUE(is_null);
  }
}

TEST(JsonTest, StringJsonExtractScalarBadBehavior) {
  // This is almost certainly an unintentional bug in the implementation. The
  // root cause is that, in general, parsing stops once the scalar is found.
  // Thus what the parser sees is for example '"{"a": 0"<etc>'.  So all manner
  // of terrible stuff can be beyond the parsed string.

  // It is not clear if this is desired behavior, for now, this simply records
  // that this is the _current_ behavior.
  ExpectExtractScalar(R"({"a": 0001})", "$.a", "0");
  ExpectExtractScalar(R"({"a": 123abc})", "$.a", "123");
  ExpectExtractScalar(R"({"a": 1ab\\unicorn\0{{{{{{)", "$.a", "1");
}

TEST(JsonTest, StringJsonExtractScalarExpectVeryLongIntegersPassthrough) {
  std::string long_integer_str(500, '1');
  ABSL_CHECK_EQ(long_integer_str.size(), 500);
  ExpectExtractScalar(absl::StrFormat(R"({"a": %s})", long_integer_str), "$.a",
                      long_integer_str);
}

TEST(JsonTest, StringJsonCompliance) {
  std::vector<std::vector<FunctionTestCall>> all_tests = {
      GetFunctionTestsStringJsonQuery(), GetFunctionTestsStringJsonExtract(),
      GetFunctionTestsStringJsonValue(),
      GetFunctionTestsStringJsonExtractScalar()};
  for (const std::vector<FunctionTestCall>& tests : all_tests) {
    for (const FunctionTestCall& test : tests) {
      if (test.params.params()[0].is_null() ||
          (test.params.params().size() > 1 &&
           test.params.params()[1].is_null())) {
        continue;
      }

      const std::string json = test.params.param(0).string_value();
      const std::string json_path = (test.params.params().size() > 1)
                                        ? test.params.param(1).string_value()
                                        : "$";
      SCOPED_TRACE(absl::Substitute("$0('$1', '$2')", test.function_name, json,
                                    json_path));

      std::string value;
      bool is_null = false;
      absl::Status status;
      bool sql_standard_mode = test.function_name == "json_query" ||
                               test.function_name == "json_value";
      auto evaluator_status = JsonPathEvaluator::Create(
          json_path, sql_standard_mode,
          /*enable_special_character_escaping_in_values=*/true,
          /*enable_special_character_escaping_in_keys=*/true);
      if (evaluator_status.ok()) {
        const std::unique_ptr<JsonPathEvaluator>& evaluator =
            evaluator_status.value();
        if (test.function_name == "json_extract" ||
            test.function_name == "json_query") {
          bool is_warning_called = false;
          status =
              evaluator->Extract(json, &value, &is_null,
                                 [&is_warning_called](absl::Status status) {
                                   is_warning_called = true;
                                 });
          // Because key_escaping is enabled a warning should never be
          // triggered.
          EXPECT_FALSE(is_warning_called);
        } else {
          status = evaluator->ExtractScalar(json, &value, &is_null);
        }
      } else {
        status = evaluator_status.status();
      }
      if (!status.ok() || !test.params.status().ok()) {
        EXPECT_EQ(test.params.status().code(), status.code()) << status;
      } else {
        EXPECT_EQ(test.params.result().is_null(), is_null);
        if (!test.params.result().is_null() && !is_null) {
          EXPECT_EQ(test.params.result().string_value(), value);
        }
      }
    }
  }
}

TEST(JsonTest, NativeJsonCompliance) {
  std::vector<std::vector<FunctionTestCall>> all_tests = {
      GetFunctionTestsNativeJsonQuery(), GetFunctionTestsNativeJsonExtract(),
      GetFunctionTestsNativeJsonValue(),
      GetFunctionTestsNativeJsonExtractScalar()};
  for (const std::vector<FunctionTestCall>& tests : all_tests) {
    for (const FunctionTestCall& test : tests) {
      if (test.params.params()[0].is_null() ||
          (test.params.params().size() > 1 &&
           test.params.params()[1].is_null())) {
        continue;
      }
      if (test.params.param(0).is_unparsed_json()) {
        // Unvalidated JSON will be tested in compliance testing, not in unit
        // tests.
        continue;
      }
      const JSONValueConstRef json = test.params.param(0).json_value();
      const std::string json_path = (test.params.params().size() > 1)
                                        ? test.params.param(1).string_value()
                                        : "$";
      SCOPED_TRACE(absl::Substitute("$0('$1', '$2')", test.function_name,
                                    json.ToString(), json_path));

      absl::Status status;
      bool sql_standard_mode = test.function_name == "json_query" ||
                               test.function_name == "json_value";
      auto evaluator_status = JsonPathEvaluator::Create(
          json_path, sql_standard_mode,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false);
      if (evaluator_status.ok()) {
        const std::unique_ptr<JsonPathEvaluator>& evaluator =
            evaluator_status.value();
        if (test.function_name == "json_extract" ||
            test.function_name == "json_query") {
          std::optional<JSONValueConstRef> result_or = evaluator->Extract(json);
          EXPECT_EQ(test.params.result().is_null(), !result_or.has_value());
          if (!test.params.result().is_null() && result_or.has_value()) {
            EXPECT_THAT(result_or.value(),
                        JsonEq(test.params.result().json_value()));
          }
        } else {
          std::optional<std::string> result_or = evaluator->ExtractScalar(json);
          EXPECT_EQ(test.params.result().is_null(), !result_or.has_value());
          if (!test.params.result().is_null() && result_or.has_value()) {
            EXPECT_EQ(result_or.value(), test.params.result().string_value());
          }
        }
      } else {
        status = evaluator_status.status();
      }
      if (!status.ok() || !test.params.status().ok()) {
        EXPECT_EQ(test.params.status().code(), status.code()) << status;
      }
    }
  }
}

TEST(JsonTest, NativeJsonArrayCompliance) {
  const std::vector<std::vector<FunctionTestCall>> all_tests = {
      GetFunctionTestsNativeJsonQueryArray(),
      GetFunctionTestsNativeJsonExtractArray(),
      GetFunctionTestsNativeJsonValueArray(),
      GetFunctionTestsNativeJsonExtractStringArray()};

  for (const std::vector<FunctionTestCall>& tests : all_tests) {
    for (const FunctionTestCall& test : tests) {
      if (test.params.params()[0].is_null() ||
          test.params.params()[1].is_null()) {
        continue;
      }
      if (!test.params.param(0).is_validated_json()) {
        // Unvalidated JSON will be tested in compliance testing, not in unit
        // tests.
        continue;
      }
      const JSONValueConstRef json = test.params.param(0).json_value();
      const std::string json_path = test.params.param(1).string_value();
      SCOPED_TRACE(absl::Substitute("$0('$1', '$2')", test.function_name,
                                    json.ToString(), json_path));

      absl::Status status;
      bool sql_standard_mode = test.function_name == "json_query_array" ||
                               test.function_name == "json_value_array";
      auto evaluator_status = JsonPathEvaluator::Create(
          json_path, sql_standard_mode,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false);
      if (evaluator_status.ok()) {
        std::unique_ptr<JsonPathEvaluator> evaluator =
            std::move(evaluator_status).value();
        if (test.function_name == "json_extract_array" ||
            test.function_name == "json_query_array") {
          std::optional<std::vector<JSONValueConstRef>> result =
              evaluator->ExtractArray(json);

          EXPECT_EQ(test.params.result().is_null(), !result.has_value());
          if (!test.params.result().is_null() && result.has_value()) {
            std::vector<::testing::Matcher<JSONValueConstRef>> expected_result;
            expected_result.reserve(test.params.result().num_elements());
            for (const Value& value : test.params.result().elements()) {
              expected_result.push_back(JsonEq(value.json_value()));
            }

            EXPECT_THAT(*result, ElementsAreArray(expected_result));
          }
        } else {
          std::optional<std::vector<std::optional<std::string>>> result =
              evaluator->ExtractStringArray(json);
          EXPECT_EQ(test.params.result().is_null(), !result.has_value());
          if (!test.params.result().is_null() && result.has_value()) {
            std::vector<Value> string_array_result;
            string_array_result.reserve(result->size());
            for (const auto& element : *result) {
              string_array_result.push_back(element.has_value()
                                                ? values::String(*element)
                                                : values::NullString());
            }
            EXPECT_EQ(values::UnsafeArray(types::StringArrayType(),
                                          std::move(string_array_result)),
                      test.params.result());
          }
        }
      } else {
        status = evaluator_status.status();
      }
      if (!status.ok() || !test.params.status().ok()) {
        EXPECT_EQ(test.params.status().code(), status.code()) << status;
      }
    }
  }
}

TEST(JsonPathTest, JsonPathEndedWithDotNonStandardMode) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$.", R"({"a":{"b":[{"c":"foo"}]}})"},
      {"$.a.", R"({"b":[{"c":"foo"}]})"},
      {"$.a.b.", R"([{"c":"foo"}])"},
      {"$.a.b[0].", R"({"c":"foo"})"},
      {"$.a.b[0].c.", R"("foo")"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_EXTRACT('$0', '$1')", json,
                                  input_and_output.first));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<JsonPathEvaluator> evaluator,
        JsonPathEvaluator::Create(
            input_and_output.first,
            /*sql_standard_mode=*/false,
            /*enable_special_character_escaping_in_values=*/false,
            /*enable_special_character_escaping_in_keys=*/false));
    std::string value;
    bool is_null;
    GOOGLESQL_ASSERT_OK(evaluator->Extract(json, &value, &is_null));
    EXPECT_EQ(input_and_output.second, value);
    EXPECT_FALSE(is_null);
  }
}

TEST(JsonPathTest, JsonPathEndedWithDotStandardMode) {
  const std::string json = R"({"a": {"b": [ { "c" : "foo" } ] } })";
  const std::vector<std::pair<std::string, std::string>> inputs_and_outputs = {
      {"$.", R"({"a":{"b":[{"c":"foo"}]}})"},
      {"$.a.", R"({"b":[{"c":"foo"}]})"},
      {"$.a.b.", R"([{"c":"foo"}])"},
      {"$.a.b[0].", R"({"c":"foo"})"},
      {"$.a.b[0].c.", R"("foo")"}};
  for (const auto& input_and_output : inputs_and_outputs) {
    SCOPED_TRACE(absl::Substitute("JSON_QUERY('$0', '$1')", json,
                                  input_and_output.first));

    EXPECT_THAT(JsonPathEvaluator::Create(
                    input_and_output.first,
                    /*sql_standard_mode=*/true,
                    /*enable_special_character_escaping_in_values=*/false,
                    /*enable_special_character_escaping_in_keys=*/false),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Invalid token in JSONPath at:")));
  }
}

TEST(JsonTest, ConvertJSONPathToSqlStandardMode) {
  const std::string kInvalidJSONPath = "Invalid JSONPath input";

  std::vector<std::pair<std::string, std::string>> json_paths = {
      {"$", "$"},
      {"$['']", "$.\"\""},
      {"$.a", "$.a"},
      {"$[a]", "$.a"},
      {"$['a']", "$.a"},
      {"$[10]", "$.10"},
      {"$.a_b", "$.a_b"},
      {"$.a:b", "$.a:b"},
      {"$.a  \tb", "$.a  \tb"},
      {"$.a['b.c'].d[0].e", R"($.a."b.c".d.0.e)"},
      {"$['b.c'][d].e['f.g'][3]", R"($."b.c".d.e."f.g".3)"},
      // In non-standard mode, it is allowed for JSONPath to have a trailing "."
      {"$.", "$"},
      {"$.a.", "$.a"},
      {"$.a['b,c'].", R"($.a."b,c")"},
      // Special characters
      {R"($['a\''])", R"($."a'")"},
      {R"($['a,b'])", R"($."a,b")"},
      {R"($['a]'])", R"($."a]")"},
      {R"($['a[\'b\']'])", R"($."a['b']")"},
      {R"($['a"'])", R"($."a\"")"},
      {R"($['\\'])", R"($."\\")"},
      {R"($['a"\''].b['$#9"[\'s""]'])", R"($."a\"'".b."$#9\"['s\"\"]")"},
      // Invalid non-standard JSONPath.
      {R"($."a.b")", kInvalidJSONPath},
      // TODO: Single backslashes are not supported in JSONPath.
      {R"($['\'])", kInvalidJSONPath},
  };

  for (const auto& [non_standard_json_path, standard_json_path] : json_paths) {
    SCOPED_TRACE(absl::Substitute("ConvertJSONPathToSqlStandardMode($0)",
                                  non_standard_json_path));
    if (json_internal::IsValidJSONPath(non_standard_json_path,
                                       /*sql_standard_mode=*/false)
            .ok()) {
      EXPECT_THAT(ConvertJSONPathToSqlStandardMode(non_standard_json_path),
                  IsOkAndHolds(standard_json_path));
      GOOGLESQL_EXPECT_OK(json_internal::IsValidJSONPath(standard_json_path,
                                               /*sql_standard_mode=*/true));
    } else {
      EXPECT_THAT(ConvertJSONPathToSqlStandardMode(non_standard_json_path),
                  StatusIs(absl::StatusCode::kOutOfRange));
      EXPECT_EQ(standard_json_path, kInvalidJSONPath);
    }
  }
}

TEST(JsonTest, ConvertJSONPathTokenToSqlStandardMode) {
  std::vector<std::pair<std::string, std::string>> json_path_tokens = {
      {"a", "a"},
      {"10", "10"},
      {"a_b", "a_b"},
      {"a:b", "a:b"},
      {"a  \tb", "a  \tb"},
      // Special characters
      {"a'", R"("a'")"},
      {"a.b", R"("a.b")"},
      {"a,b", R"("a,b")"},
      {"a]", R"("a]")"},
      {"a['b']", R"("a['b']")"},
      {R"(a")", R"("a\"")"},
      {R"(\\)", R"("\\")"},
  };

  for (const auto& [token, standard_json_path_token] : json_path_tokens) {
    SCOPED_TRACE(
        absl::Substitute("ConvertJSONPathTokenToSqlStandardMode($0)", token));
    EXPECT_EQ(ConvertJSONPathTokenToSqlStandardMode(token),
              standard_json_path_token);
    GOOGLESQL_EXPECT_OK(json_internal::IsValidJSONPath(
        absl::StrCat("$.", standard_json_path_token),
        /*sql_standard_mode=*/true));
  }
}

TEST(JsonTest, MergeJSONPathsIntoSqlStandardMode) {
  const std::string kInvalidJSONPath = "Invalid JSONPath input";

  std::vector<std::pair<std::vector<std::string>, std::string>>
      json_paths_test_cases = {
          {{"$"}, "$"},
          {{"$.a"}, "$.a"},
          {{"$['a']"}, "$.a"},
          {{"$['a']", "$.b"}, "$.a.b"},
          {{"$['a']", "$.b", R"($.c[1]."d.e")"}, R"($.a.b.c[1]."d.e")"},
          {{"$['a']", "$.b", "$.c[1]['d.e']"}, R"($.a.b.c.1."d.e")"},
          {{R"($['a\''])", R"($.b['c[\'d\']'])"}, R"($."a'".b."c['d']")"},
          // In non-standard mode, it is allowed for JSONPath to have a trailing
          // "."
          {{"$.", "$"}, "$"},
          {{"$.a.", "$[0]", "$['b,c']."}, R"($.a[0]."b,c")"},
          {{R"($."a\b")", "$.", "$", "$.a['b,c']."}, R"($."a\b".a."b,c")"},
          // Invalid inputs
          {{}, kInvalidJSONPath},
          {{"$", ".a"}, kInvalidJSONPath},
          {{"$", "$.a'"}, kInvalidJSONPath},
          // Standard mode cannot have a trailing "."
          {{R"($."a,b".)", "$.d"}, kInvalidJSONPath},
      };

  for (const auto& [json_paths, merged_json_path] : json_paths_test_cases) {
    SCOPED_TRACE(absl::Substitute("MergeJSONPathsIntoSqlStandardMode($0)",
                                  absl::StrJoin(json_paths, ", ")));
    if (merged_json_path == kInvalidJSONPath) {
      EXPECT_THAT(MergeJSONPathsIntoSqlStandardMode(json_paths),
                  StatusIs(absl::StatusCode::kOutOfRange));
    } else {
      EXPECT_THAT(MergeJSONPathsIntoSqlStandardMode(json_paths),
                  IsOkAndHolds(merged_json_path));
      GOOGLESQL_EXPECT_OK(json_internal::IsValidJSONPath(merged_json_path,
                                               /*sql_standard_mode=*/true));
    }
  }
}

}  // namespace

namespace json_internal {
namespace {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

// Unit tests for the JSONPathExtractor and ValidJSONPathIterator.
static std::string Normalize(const std::string& in) {
  std::string output;
  std::string::const_iterator in_itr = in.begin();
  for (; in_itr != in.end(); ++in_itr) {
    if (!std::isspace(*in_itr)) {
      output.push_back(*in_itr);
    }
  }
  return output;
}

TEST(JsonPathExtractorTest, ScanTester) {
  std::unique_ptr<ValidJSONPathIterator> iptr;
  {
    std::string non_persisting_path = "$.a.b.c.d";
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        iptr, ValidJSONPathIterator::Create(non_persisting_path,
                                            /*sql_standard_mode=*/true));
    iptr->Scan();
  }
  ValidJSONPathIterator& itr = *iptr;
  ASSERT_TRUE(itr.End());
  itr.Rewind();
  ASSERT_TRUE(!itr.End());

  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "b", "c",
                                                          "d"};
  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }
  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, SimpleValidPath) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create("$.a.b", /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr);

  ASSERT_TRUE(!itr.End());

  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "b"};
  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }
  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, ValidValueAccessEmptyPath) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create("$", /*sql_standard_mode=*/true));
  EXPECT_EQ(iptr->Size(), 1);
  EXPECT_EQ(iptr->GetToken(0), "");
}

TEST(JsonPathExtractorTest, ValidValueAccessObjectTokens) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create("$.a.b.c", /*sql_standard_mode=*/true));
  EXPECT_EQ(iptr->Size(), 4);
  EXPECT_EQ(iptr->GetToken(0), "");
  EXPECT_EQ(iptr->GetToken(1), "a");
  EXPECT_EQ(iptr->GetToken(2), "b");
  EXPECT_EQ(iptr->GetToken(3), "c");
}

TEST(JsonPathExtractorTest, ValidValueAccessArrayAndObjectTokens) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValidJSONPathIterator> iptr,
                       ValidJSONPathIterator::Create(
                           "$[0].a.b[1].c", /*sql_standard_mode=*/true));
  EXPECT_EQ(iptr->Size(), 6);
  EXPECT_EQ(iptr->GetToken(0), "");
  EXPECT_EQ(iptr->GetToken(1), "0");
  EXPECT_EQ(iptr->GetToken(2), "a");
  EXPECT_EQ(iptr->GetToken(3), "b");
  EXPECT_EQ(iptr->GetToken(4), "1");
  EXPECT_EQ(iptr->GetToken(5), "c");
}

TEST(JsonPathExtractorTest, ValidValueAccessNonStandardSql) {
  std::string path = "$['b.c'][d].e['f.g'][3]";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/false));
  EXPECT_EQ(iptr->Size(), 6);
  EXPECT_EQ(iptr->GetToken(0), "");
  EXPECT_EQ(iptr->GetToken(1), "b.c");
  EXPECT_EQ(iptr->GetToken(2), "d");
  EXPECT_EQ(iptr->GetToken(3), "e");
  EXPECT_EQ(iptr->GetToken(4), "f.g");
  EXPECT_EQ(iptr->GetToken(5), "3");
}

class JSONPathIteratorTest
    : public ::testing::TestWithParam<
          std::tuple</*path=*/std::string, /*is_sql_standard_mode=*/bool>> {
 protected:
  absl::string_view GetPath() const { return std::get<0>(GetParam()); }
  bool IsSqlStandardMode() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    JSONPathIteratorSQLModeAgnosticTests, JSONPathIteratorTest,
    ::testing::Combine(::testing::Values("$", "$.a.b", "$[0][12]", "$[0].a",
                                         "$.a.b.c.d.e.f.g.h.i.j"),
                       ::testing::Bool()));

INSTANTIATE_TEST_SUITE_P(
    JSONPathIteratorStandardSQLModeTests, JSONPathIteratorTest,
    ::testing::Combine(::testing::Values(R"($.a."b.c".d.0.e)", R"($."a['b']")"),
                       ::testing::Values(true)));

INSTANTIATE_TEST_SUITE_P(
    JSONPathIteratorNonStandardSQLModeTests, JSONPathIteratorTest,
    ::testing::Combine(::testing::Values(R"($.a['\'\'\\s '].g[1])",
                                         "$['b.c'][d].e['f.g'][3]"),
                       ::testing::Values(false)));

TEST_P(JSONPathIteratorTest, ValidValueAccess) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(GetPath(),
                                    /*sql_standard_mode=*/IsSqlStandardMode()));
  ValidJSONPathIterator& itr = *(iptr);
  for (int i = 0; i < itr.Size(); ++i, ++itr) {
    EXPECT_EQ(itr.GetToken(i), *itr);
  }
}

TEST(JsonPathExtractorTest, BackAndForthIteration) {
  const char* const input = "$.a.b";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(input, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr);

  ++itr;
  EXPECT_EQ(*itr, "a");
  --itr;
  EXPECT_EQ(*itr, "");
  --itr;
  EXPECT_TRUE(itr.End());
  ++itr;
  EXPECT_EQ(*itr, "");
  ++itr;
  EXPECT_EQ(*itr, "a");
  ++itr;
  EXPECT_EQ(*itr, "b");
}

TEST(JsonPathExtractorTest, EscapedPathTokens) {
  std::string esc_text("$.a['\\'\\'\\s '].g[1]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(esc_text, /*sql_standard_mode=*/false));
  ValidJSONPathIterator& itr = *(iptr);
  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "''\\s ",
                                                          "g", "1"};

  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, EscapedPathTokensStandard) {
  std::string esc_text("$.a.\"\\\"\\\"\\s \".g[1]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(esc_text, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr);
  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "\"\"\\s ",
                                                          "g", "1"};

  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, EmptyPathTokens) {
  std::string esc_text("$.a[''].g[1]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iter,
      ValidJSONPathIterator::Create(esc_text, /*sql_standard_mode=*/false));
  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "", "g",
                                                          "1"};

  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !iter->End(); ++(*iter)) {
    tokens.push_back(**iter);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, EmptyPathTokensStandard) {
  std::string esc_text("$.a.\"\".g[1]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iter,
      ValidJSONPathIterator::Create(esc_text, /*sql_standard_mode=*/true));
  const std::vector<ValidJSONPathIterator::Token> gold = {"", "a", "", "g",
                                                          "1"};

  std::vector<ValidJSONPathIterator::Token> tokens;
  for (; !iter->End(); ++(*iter)) {
    tokens.push_back(**iter);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(JsonPathExtractorTest, MixedPathTokens) {
  const char* const input_path =
      "$.a.b[423490].c['d::d'].e['abc\\\\\\'\\'     ']";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  ValidJSONPathIterator& itr = *(iptr);
  const std::vector<ValidJSONPathIterator::Token> gold = {
      "", "a", "b", "423490", "c", "d::d", "e", "abc\\\\''     "};

  std::vector<ValidJSONPathIterator::Token> tokens;
  size_t n = gold.size();
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));

  tokens.clear();

  // Test along the decrement of the iterator.
  --itr;
  EXPECT_FALSE(itr.End());
  for (; !itr.End(); --itr) {
    tokens.push_back(*itr);
  }
  EXPECT_EQ(tokens.size(), n);

  for (size_t i = 0; i < tokens.size(); i++) {
    EXPECT_EQ(gold[(n - 1) - i], tokens[i]);
  }

  // Test along the increment of the iterator.
  tokens.clear();
  EXPECT_TRUE(itr.End());
  ++itr;
  EXPECT_FALSE(itr.End());
  for (; !itr.End(); ++itr) {
    tokens.push_back(*itr);
  }

  EXPECT_THAT(tokens, ElementsAreArray(gold));
}

TEST(RemoveBackSlashFollowedByChar, BasicTests) {
  std::string token = "'abc\\'\\'h'";
  std::string expected_token = "'abc''h'";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);

  token = "";
  expected_token = "";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);

  token = "\\'";
  expected_token = "'";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);

  token = "\\'\\'\\\\'\\'\\'\\f ";
  expected_token = "''\\'''\\f ";
  RemoveBackSlashFollowedByChar(&token, '\'');
  EXPECT_EQ(token, expected_token);
}

TEST(IsValidJSONPathTest, BasicTests) {
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$", /*sql_standard_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a", /*sql_standard_mode=*/true));

  // Escaped a
  EXPECT_THAT(IsValidJSONPath("$['a']", /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$['a']", /*sql_standard_mode=*/false));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.\"a\"", /*sql_standard_mode=*/true));

  // Escaped efgh
  EXPECT_THAT(IsValidJSONPath("$.a.b.c['efgh'].e", /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a.b.c['efgh'].e", /*sql_standard_mode=*/false));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a.b.c.\"efgh\".e", /*sql_standard_mode=*/true));

  // Escaped b.c.d
  EXPECT_THAT(IsValidJSONPath("$.a['b.c.d'].e", /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a['b.c.d'].e", /*sql_standard_mode=*/false));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a.\"b.c.d\".e", /*sql_standard_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.\"b.c.d\".e", /*sql_standard_mode=*/true));

  EXPECT_THAT(
      IsValidJSONPath("$['a']['b']['c']['efgh']", /*sql_standard_mode=*/true),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$['a']['b']['c']['efgh']",
                            /*sql_standard_mode=*/false));

  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a.b.c[0].e.f", /*sql_standard_mode=*/true));

  EXPECT_THAT(IsValidJSONPath("$['a']['b']['c'][0]['e']['f']",
                              /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$['a']['b']['c'][0]['e']['f']",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$['a']['b\\'\\c\\\\d          ef']",
                              /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$['a']['b\\'\\c\\\\d          ef']",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$['a;;;;;\\\\']['b\\'\\c\\\\d          ef']",
                              /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$['a;;;;;\\\\']['b\\'\\c\\\\d          ef']",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$.a['\\'\\'\\'\\'\\'\\\\f '].g[1]",
                              /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a['\\'\\'\\'\\'\\'\\\\f '].g[1]",
                            /*sql_standard_mode=*/false));

  EXPECT_THAT(IsValidJSONPath("$.a.b.c[efgh]", /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at:")));
  GOOGLESQL_EXPECT_OK(IsValidJSONPath("$.a.b.c[efgh]", /*sql_standard_mode=*/false));

  // unsupported @ in the path.
  EXPECT_THAT(
      IsValidJSONPath("$.a.;;;;;;;c[0];;;.@.f", /*sql_standard_mode=*/true),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: @")));
  EXPECT_THAT(
      IsValidJSONPath("$.a.;;;;;;;.c[0].@.f", /*sql_standard_mode=*/true),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: @")));
  EXPECT_THAT(IsValidJSONPath("$..", /*sql_standard_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: ..")));
  EXPECT_THAT(
      IsValidJSONPath("$.a.b.c[f.g.h.i].m.f", /*sql_standard_mode=*/false),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Invalid token in JSONPath at: [f.g.h.i]")));
  EXPECT_THAT(IsValidJSONPath("$.a.b.c['f.g.h.i'].[acdm].f",
                              /*sql_standard_mode=*/false),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: .[acdm]")));

  // Wildcard and descendant operators are rejected in legacy mode.
  EXPECT_THAT(IsValidJSONPath("$.*", /*sql_standard_mode=*/false),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(IsValidJSONPath("$[*]", /*sql_standard_mode=*/false),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(IsValidJSONPath("$..a", /*sql_standard_mode=*/false),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: ..")));
}

TEST(IsValidJSONPathTest, StrictBasicTests) {
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.a"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$[ 0 ]"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict(R"($."a")"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict(R"($.a.b.c."efgh".e)"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict(R"($.a."b.c.d".e)"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict(R"($."b.c.d".e)"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.a.b.c[0].e.f"));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.\"a\tb\"[1]"));
}

TEST(IsValidJSONPathTest, InvalidPathStrictTests) {
  // Invalid cases.
  std::vector<std::string> invalid_paths = {
      "$[0-]", "$[0_]", "$[-1]", "[0]", "$[a]", "$['a']", "$.a.b.c['efgh'].e",
      "$.", ".a", "$[9223372036854775807990]",
      // Lax mode not enabled. Doesn't support lax keywords.
      "lax $", "lax $.a", "lax recursive $", "recursive lax $.a"};

  for (const std::string& invalid_path : invalid_paths) {
    EXPECT_THAT(IsValidJSONPathStrict(invalid_path),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_FALSE(StrictJSONPathIterator::Create(invalid_path).ok());
  }

  // Verify error message for compatibility.
  EXPECT_THAT(
      IsValidJSONPathStrict("a"),
      StatusIs(absl::StatusCode::kOutOfRange, "JSONPath must start with '$'"));
  EXPECT_THAT(
      StrictJSONPathIterator::Create("a"),
      StatusIs(absl::StatusCode::kOutOfRange, "JSONPath must start with '$'"));
}

TEST(IsValidJSONPathTest, ValidLaxJSONPaths) {
  // No keywords.
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.a", /*enable_lax_mode=*/true));
  // lax is part of the path not a keyword.
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.a lax", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.a.recursive", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("$.a recursive", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(
      IsValidJSONPathStrict("$.a lax.recursive", /*enable_lax_mode=*/true));
  // Includes keywords.
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("lax $", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("lax $.a", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(
      IsValidJSONPathStrict("lax recursive  $", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("lax  recursive    $.a.lax",
                                  /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(
      IsValidJSONPathStrict("recursive lax   $", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(
      IsValidJSONPathStrict("recursive lax   $.a", /*enable_lax_mode=*/true));
  // Mixed-case keywords.
  GOOGLESQL_EXPECT_OK(IsValidJSONPathStrict("Recursive LAX $", /*enable_lax_mode=*/true));
  GOOGLESQL_EXPECT_OK(
      IsValidJSONPathStrict("LAx Recursive $.a", /*enable_lax_mode=*/true));
}

TEST(IsValidJSONPathTest, InvalidLaxJSONPaths) {
  // Invalid keyword combinations.
  EXPECT_FALSE(
      IsValidJSONPathStrict("lax lax $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("lax recursive lax $", /*enable_lax_mode=*/true)
          .ok());
  // Case insensitive matching.
  EXPECT_FALSE(
      IsValidJSONPathStrict("lax recursive LAX $", /*enable_lax_mode=*/true)
          .ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("recursive $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(IsValidJSONPathStrict("recursive lax recursive $",
                                     /*enable_lax_mode=*/true)
                   .ok());
  // Case insensitive matching.
  EXPECT_FALSE(IsValidJSONPathStrict("recursive lax RECURSIVE $",
                                     /*enable_lax_mode=*/true)
                   .ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("invalid $.a", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("lax invalid $.a", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("invalid lax $", /*enable_lax_mode=*/true).ok());
  // Invalid whitespace.
  EXPECT_FALSE(IsValidJSONPathStrict("lax$", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("laxrecursive $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict("recursivelax $.a", /*enable_lax_mode=*/true).ok());
  // Doesn't contain "$".
  EXPECT_FALSE(IsValidJSONPathStrict("lax", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(IsValidJSONPathStrict("lax a", /*enable_lax_mode=*/true).ok());
  // No whitespace allowed before first keyword.
  EXPECT_FALSE(IsValidJSONPathStrict(" $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(IsValidJSONPathStrict(" $.a", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(IsValidJSONPathStrict(" lax $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict(" lax recursive $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      IsValidJSONPathStrict(" recursive lax $.a", /*enable_lax_mode=*/true)
          .ok());

  // Verify error message for compatibility.
  EXPECT_THAT(IsValidJSONPathStrict("a", /*enable_lax_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "JSONPath must start with zero or more unique modifiers "
                       "followed by '$'"));
}

TEST(JSONPathTest, StrictPathOptionsTests) {
  auto verify_fn = [](absl::string_view path_str, bool lax, bool recursive) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path_str, true));
    EXPECT_EQ(path_itr->GetJsonPathOptions().lax, lax);
    EXPECT_EQ(path_itr->GetJsonPathOptions().recursive, recursive);
  };
  verify_fn("$", /*lax=*/false, /*recursive=*/false);
  verify_fn("$.a", /*lax=*/false, /*recursive=*/false);
  verify_fn("lax $", /*lax=*/true, /*recursive=*/false);
  verify_fn("lax $.a", /*lax=*/true, /*recursive=*/false);
  verify_fn("lax recursive $", /*lax=*/true, /*recursive=*/true);
  verify_fn("lax   recursive $.a", /*lax=*/true, /*recursive=*/true);
  verify_fn("recursive lax $", /*lax=*/true, /*recursive=*/true);
  verify_fn("recursive lax $.a", /*lax=*/true, /*recursive=*/true);
  // lax is part of the path not a keyword.
  verify_fn("$.a lax", /*lax=*/false, /*recursive=*/false);
  verify_fn("$.a recursive", /*lax=*/false, /*recursive=*/false);
  verify_fn("$.a recursive.lax", /*lax=*/false, /*recursive=*/false);
  // recursive is part of the path not a keyword.
  verify_fn("lax $.recursive", /*lax=*/true, /*recursive=*/false);
}

TEST(JSONPathTest, IsValidAndLaxJSONPath) {
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $"), IsOkAndHolds(true));
  EXPECT_THAT(IsValidAndLaxJSONPath("Recursive lax  $.a"), IsOkAndHolds(true));
  EXPECT_THAT(IsValidAndLaxJSONPath("$"), IsOkAndHolds(false));
  EXPECT_THAT(IsValidAndLaxJSONPath("$.lax"), IsOkAndHolds(false));
  // Invalid JSONPaths.
  EXPECT_FALSE(IsValidAndLaxJSONPath(" lax $").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("recursive $").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("invalid $").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("lax invalid $").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("invalid lax $").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("lax$").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("laxrecursive $").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("recursivelax $").ok());
  // Non-standard sql paths are invalid for StrictJSONPathIterator.
  EXPECT_FALSE(IsValidAndLaxJSONPath("$.").ok());
  EXPECT_FALSE(IsValidAndLaxJSONPath("$$").ok());
}

TEST(JSONPathTest, InvalidLaxJSONPathIterator) {
  // Invalid keyword combinations.
  EXPECT_FALSE(
      StrictJSONPathIterator::Create(" lax $", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("lax a", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("recursive $.a", /*enable_lax_mode=*/true)
          .ok());
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("lax invalid $", /*enable_lax_mode=*/true)
          .ok());
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("lax$", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("laxrecursive $", /*enable_lax_mode=*/true)
          .ok());
  EXPECT_FALSE(StrictJSONPathIterator::Create("recursivelax $.a",
                                              /*enable_lax_mode=*/true)
                   .ok());
  // Non-standard sql path.
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("$.", /*enable_lax_mode=*/true).ok());
  EXPECT_FALSE(
      StrictJSONPathIterator::Create("$$", /*enable_lax_mode=*/true).ok());

  // Verify error message for compatibility.
  EXPECT_THAT(StrictJSONPathIterator::Create("a", /*enable_lax_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "JSONPath must start with zero or more unique modifiers "
                       "followed by '$'"));
}

class StrictJsonPathIteratorTest
    : public ::testing::TestWithParam<
          std::tuple</*enable_lax_mode=*/bool, /*path_prefix=*/std::string>> {
 protected:
  bool EnableLaxMode() const { return std::get<0>(GetParam()); }
  absl::string_view PathPrefix() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    StrictJsonPathIteratorTests, StrictJsonPathIteratorTest,
    ::testing::Values(std::make_tuple(false, ""), std::make_tuple(true, "lax "),
                      std::make_tuple(true, "lax recursive  "),
                      std::make_tuple(true, "Recursive LAX  ")));

TEST_P(StrictJsonPathIteratorTest, Valid) {
  // Test all functions for iterating through a JSON path using
  // StrictJSONPathIterator.
  {
    std::string path = absl::StrCat(PathPrefix(), "$");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path, EnableLaxMode()));
    EXPECT_EQ((**path_itr).MaybeGetArrayIndex(), nullptr);
    EXPECT_EQ((**path_itr).MaybeGetObjectKey(), nullptr);
    EXPECT_FALSE(++(*path_itr));
    EXPECT_TRUE(path_itr->End());
    path_itr->Rewind();
    EXPECT_EQ((**path_itr).MaybeGetArrayIndex(), nullptr);
    EXPECT_EQ((**path_itr).MaybeGetObjectKey(), nullptr);
    EXPECT_TRUE(path_itr->NoSuffixToken());
    EXPECT_FALSE(path_itr->End());
  }
  {
    std::string path = absl::StrCat(PathPrefix(), "$.1 ");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path, EnableLaxMode()));
    // Skip first token.
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), "1 ");
    EXPECT_TRUE(path_itr->NoSuffixToken());
    EXPECT_FALSE(++(*path_itr));
    EXPECT_TRUE(path_itr->End());
  }
  {
    std::string path = absl::StrCat(PathPrefix(), "$[ 0 ]");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path, EnableLaxMode()));
    // Skip first token.
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetArrayIndex(), 0);
    EXPECT_TRUE(path_itr->NoSuffixToken());
    EXPECT_FALSE(++(*path_itr));
    EXPECT_TRUE(path_itr->End());
  }
  {
    std::string path = absl::StrCat(PathPrefix(), R"($."a")");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path, EnableLaxMode()));
    // Skip first token.
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), "a");
    EXPECT_TRUE(path_itr->NoSuffixToken());
    EXPECT_FALSE(++(*path_itr));
    EXPECT_TRUE(path_itr->End());
  }
  {
    // Path escaping.
    std::string path = absl::StrCat(PathPrefix(), R"($."a\"b")");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path, EnableLaxMode()));
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), R"(a\"b)");
    EXPECT_TRUE(path_itr->NoSuffixToken());
    EXPECT_FALSE(++(*path_itr));
    EXPECT_TRUE(path_itr->End());
  }
  {
    // Test iterating and rewind.
    std::string path = absl::StrCat(PathPrefix(), "$.\"b.c.d\"[1].e");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<StrictJSONPathIterator> path_itr,
                         StrictJSONPathIterator::Create(path, EnableLaxMode()));
    // Skip first token.
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), "b.c.d");
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetArrayIndex(), 1);
    EXPECT_TRUE(++(*path_itr));
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), "e");
    EXPECT_TRUE(path_itr->NoSuffixToken());
    EXPECT_FALSE(++(*path_itr));
    EXPECT_TRUE(path_itr->End());
    // No-op as we've already reached the end of the path.
    EXPECT_FALSE(++(*path_itr));
    // Rewind a single token.
    EXPECT_TRUE(--(*path_itr));
    EXPECT_FALSE(path_itr->End());
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), "e");
    // Rewind to the beginning and validate both tokens and type tokens.
    path_itr->Rewind();
    ++(*path_itr);
    EXPECT_EQ(*(**path_itr).MaybeGetObjectKey(), "b.c.d");
    ++(*path_itr);
    EXPECT_EQ(*(**path_itr).MaybeGetArrayIndex(), 1);
  }
}

TEST(JSONPathExtractorTest, BasicParsing) {
  std::string input =
      "{ \"l00\" : { \"l01\" : \"a10\", \"l11\" : \"test\" }, \"l10\" : { "
      "\"l01\" : null }, \"l20\" : \"a5\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(input));
  EXPECT_FALSE(is_null);
}

TEST(JSONPathExtractorTest, MatchingMultipleSuffixes) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "null";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
  EXPECT_TRUE(is_null);
}

TEST(JSONPathExtractorTest, PartiallyMatchingSuffixes) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "";

  // Parsing of JSON was successful however no match.
  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(parser.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, MatchedEmptyStringValue) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : {\"d\" : \"\" } }, \"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  // Parsing of JSON was successful and the value
  // itself is "" so we can use StoppedOnFirstMatch() to
  // distinguish between a matched value which is empty and
  // the case where there is no match. We can also rely on
  // the return value of \"\" however this is more elegant.
  std::string result;
  bool is_null;
  std::string gold = "\"\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractScalar, ValidateScalarResult) {
  std::string input =
      "{ \"a\" : { \"b\" : \"a10\", \"l11\" : \"tes\\\"t\" }, \"a\" : { "
      "\"c\" : {\"d\" : 1.9834 } , \"d\" : [ {\"a\" : \"a5\"}, {\"a\" : "
      "\"a6\"}] , \"quoted_null\" : \"null\" } , \"e\" : null , \"f\" : null}";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));

  JSONPathExtractScalar parser(input_str, path_itr.get());
  std::string scalar_result;
  bool is_null;

  EXPECT_TRUE(parser.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  std::string gold = "1.9834";
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, gold);

  input_path = "$.a.l11";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser1(input_str, path_itr1.get());

  EXPECT_TRUE(parser1.Extract(&scalar_result, &is_null));
  gold = "tes\"t";
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, gold);

  input_path = "$.a.c";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr2,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser2(input_str, path_itr2.get());

  EXPECT_TRUE(parser2.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.a.d";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr3,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser3(input_str, path_itr3.get());

  EXPECT_TRUE(parser3.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.e";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr4,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser4(input_str, path_itr4.get());

  EXPECT_TRUE(parser4.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.a.c.d.e";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr5,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser5(input_str, path_itr5.get());

  EXPECT_TRUE(parser5.Extract(&scalar_result, &is_null));
  EXPECT_FALSE(parser5.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);

  input_path = "$.a.quoted_null";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr6,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser6(input_str, path_itr6.get());

  EXPECT_TRUE(parser6.Extract(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  gold = "null";
  EXPECT_EQ(scalar_result, gold);

  input_path = "$.a.b.c";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr7,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractScalar parser7(input_str, path_itr7.get());

  EXPECT_TRUE(parser7.Extract(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(parser7.StoppedOnFirstMatch());
}

TEST(JSONPathExtractorTest, ReturnJSONObject) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null, \"f\" : { \"g\" : \"h\", \"g\" : [ \"i\", { \"x\" : "
      "\"j\"} ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "{ \"g\" : \"h\", \"g\" : [ \"i\", { \"x\" : \"j\" } ] }";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, Normalize(gold));
}

TEST(JSONPathExtractorTest, StopParserOnFirstMatch) {
  std::string input =
      "{ \"a\" : { \"b\" : { \"c\" : { \"d\" : \"l1\" } } } ,"
      " \"a\" : { \"b\" :  { \"c\" : { \"e\" : \"l2\" } } } ,"
      " \"a\" : { \"b\" : { \"c\" : { \"e\" : \"l3\"} }}}";

  std::string result;
  bool is_null;

  {
    absl::string_view input_str(input);
    absl::string_view input_path("$.a.b.c");

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<ValidJSONPathIterator> path_itr,
        ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
    JSONPathExtractor parser(input_str, path_itr.get());

    std::string gold = "{ \"d\" : \"l1\" }";

    EXPECT_TRUE(parser.Extract(&result, &is_null));
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(parser.StoppedOnFirstMatch());
    EXPECT_EQ(result, Normalize(gold));
  }

  {
    absl::string_view input_str(input);
    absl::string_view input_path("$.a.b.c");

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        const std::unique_ptr<ValidJSONPathIterator> path_itr,
        ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
    JSONPathExtractor parser(input_str, path_itr.get());

    std::string gold = "{ \"d\" : \"l1\" }";
    EXPECT_TRUE(parser.Extract(&result, &is_null));
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(parser.StoppedOnFirstMatch());
    EXPECT_EQ(result, Normalize(gold));
  }
}

TEST(JSONPathExtractorTest, BasicArrayAccess) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" }, \"a\" : { "
      "\"c\" : null, \"f\" : { \"g\" : \"h\", \"g\" : [ \"i\", \"j\" ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "\"j\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, ArrayAccessObjectMultipleSuffixes) {
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" },"
      " \"a\" : { \"f\" : null, "
      "\"f\" : { \"g\" : \"h\", "
      "\"g\" : [ \"i\", \"j\" ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string gold = "\"j\"";
  std::string result;
  bool is_null;

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, EscapedAccessTestStandard) {
  // There are two escapings happening as follows:
  // a. C++ Compiler
  // b. JSON Parser
  //
  // So '4k' (k > = 1) backslashes translate to 'k' backslashes at runtime
  // "\\\\" = "\" at runtime. So "\\\\\\\\s" === "\\s"
  std::string input =
      "{ \"e\" : { \"b\" : \"a10\", \"l11\" : \"test\" },"
      " \"a\" : { \"b\" : null, "
      "\"''\\\\\\\\s \" : { \"g\" : \"h\", "
      "\"g\" : [ \"i\", \"j\" ] } }, "
      "\"a\" : \"a5\", \"a\" : \"a6\" }";
  absl::string_view input_str(input);
  std::string input_path("$.a['\\'\\'\\\\s '].g[1]");
  absl::string_view esc_input_path(input_path);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<ValidJSONPathIterator> path_itr,
                       ValidJSONPathIterator::Create(
                           esc_input_path, /*sql_standard_mode=*/false));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "\"j\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, EscapedAccessTest) {
  std::string input = R"({"a\"b": 1 })";
  absl::string_view input_str(input);
  std::string input_path(R"($."a\"b")");
  absl::string_view esc_input_path(input_path);

  ABSL_LOG(INFO) << input;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<ValidJSONPathIterator> path_itr,
                       ValidJSONPathIterator::Create(
                           esc_input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "1";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, NestedArrayAccess) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1][0][3][3]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "{ \"a\" : \"3\" }";
  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(gold));
  EXPECT_FALSE(is_null);
}

TEST(JSONPathExtractorTest, NegativeNestedArrayAccess) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1]['-0'][3][3]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;

  std::string gold = "{ \"a\" : \"3\" }";
  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, Normalize(gold));

  absl::string_view input_path1("$[1][1]['-5'][3][3]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path1, /*sql_standard_mode=*/false));
  JSONPathExtractor parser1(input_str, path_itr1.get());

  EXPECT_TRUE(parser1.Extract(&result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(parser1.StoppedOnFirstMatch());
  EXPECT_EQ(result, "");
}

TEST(JSONPathExtractorTest, MixedNestedArrayAccess) {
  std::string input =
      "{ \"a\" : [0 , [ [],  { \"b\" : [ 7, [ 1, 4, 8, [2, 1, 0, {\"a\" : { "
      "\"b\" : \"3\"}, \"c\" : \"d\" }, 4 ], 11, 13] ] }, "
      "[], \"a\" ], 2, [] ] }";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a[1][1].b[1][3][3].c");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());
  std::string result;
  bool is_null;
  std::string gold = "\"d\"";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);
}

TEST(JSONPathExtractorTest, QuotedArrayIndex) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$['1'][1][0]['3']['3']");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathExtractor parser(input_str, path_itr.get());

  std::string result;
  bool is_null;
  std::string gold = "{ \"a\" : \"3\" }";

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(gold));
  EXPECT_FALSE(is_null);
}

TEST(JSONPathExtractorTest, TestReuseOfPathIterator) {
  std::string input =
      "[0 , [ [],  [ [ 1, 4, 8, [2, 1, 0, {\"a\" : \"3\"}, 4 ], 11, 13] ] , "
      "[], \"a\" ], 2, [] ]";
  std::string path = "$[1][1][0][3][3]";
  absl::string_view input_str(input);
  std::string gold = "{ \"a\" : \"3\" }";
  std::string result;
  bool is_null;

  // Default with local path_iterator object.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  JSONPathExtractor parser(input_str, path_itr.get());

  EXPECT_TRUE(parser.Extract(&result, &is_null));
  EXPECT_EQ(result, Normalize(gold));
  EXPECT_FALSE(is_null);

  for (size_t i = 0; i < 10; i++) {
    // Reusable token iterator.
    absl::string_view input_str(input);
    JSONPathExtractor parser(input_str, path_itr.get());

    EXPECT_TRUE(parser.Extract(&result, &is_null));
    EXPECT_EQ(result, Normalize(gold));
    EXPECT_FALSE(is_null);
  }
}

template <typename JsonExtractor>
void ExpectJsonExtractionYieldsNull(absl::string_view json_str,
                                    absl::string_view json_path,
                                    bool sql_standard_mode) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(json_path, sql_standard_mode));
  JsonExtractor extractor(json_str, path_itr.get());
  bool is_null = false;
  if constexpr (std::is_same_v<JsonExtractor, JSONPathExtractor> ||
                std::is_same_v<JsonExtractor, JSONPathExtractScalar>) {
    std::string result;
    EXPECT_TRUE(extractor.Extract(&result, &is_null));
    EXPECT_TRUE(is_null);
  } else if constexpr (std::is_same_v<JsonExtractor, JSONPathArrayExtractor>) {
    std::vector<std::string> result;
    EXPECT_TRUE(extractor.ExtractArray(&result, &is_null));
  } else {
    std::vector<std::optional<std::string>> result;
    EXPECT_TRUE(extractor.ExtractStringArray(&result, &is_null));
  }
  EXPECT_TRUE(is_null);
}

class JsonPathExtractionTest : public ::testing::TestWithParam<bool> {
 protected:
  bool IsSqlStandardMode() const { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(JSONPathExtractionSQLModeAgnosticTests,
                         JsonPathExtractionTest,
                         ::testing::Values(false, true));

// Test case for b/326281185, b/326974631.
TEST_P(JsonPathExtractionTest, TestIndexTokenCorruption) {
  // clang-format off
  std::string json_str = R"(
    {
      "a" : [
        {
          "b" : [
            {
              "c": 1,
              "d": 2
            }
          ]
        },
        {
          "b" : [
            {
              "c" : 3,
              "d" : 4
            },
            {
              "c" : 5,
              "d" : 6
            }
          ]
        }
      ]
    }
  )";
  std::string json_str_for_arrays = R"(
    {
      "a" : [
        {
          "b" : [
            {
              "c": [1,2],
              "d": [3,4]
            }
          ]
        },
        {
          "b" : [
            {
              "c" : [5,6],
              "d" : [7,8]
            },
            {
              "c" : [9,10],
              "d" : [10,11]
            }
          ]
        }
      ]
    }
  )";
  // clang-format on
  std::string json_path = "$.a[0].b[1].c";
  ExpectJsonExtractionYieldsNull<JSONPathExtractor>(json_str, json_path,
                                                    IsSqlStandardMode());
  ExpectJsonExtractionYieldsNull<JSONPathExtractScalar>(json_str, json_path,
                                                        IsSqlStandardMode());
  ExpectJsonExtractionYieldsNull<JSONPathArrayExtractor>(
      json_str_for_arrays, json_path, IsSqlStandardMode());
  ExpectJsonExtractionYieldsNull<JSONPathStringArrayExtractor>(
      json_str_for_arrays, json_path, IsSqlStandardMode());
}

TEST(JSONPathArrayExtractorTest, BasicParsing) {
  std::string input =
      "[ {\"l00\" : { \"l01\" : \"a10\", \"l11\" : \"test\" }}, {\"l10\" : { "
      "\"l01\" : null }}, {\"l20\" : \"a5\"} ]";
  absl::string_view input_str(input);
  absl::string_view input_path("$");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  std::vector<std::string> gold(
      {Normalize("{\"l00\": { \"l01\" : \"a10\", \"l11\" : \"test\" }}"),
       Normalize("{\"l10\" : { \"l01\" : null }}"),
       Normalize("{\"l20\" : \"a5\"}")});
  bool is_null;

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);
}

TEST(JSONPathArrayExtractorTest, MatchingMultipleSuffixes) {
  std::string input =
      R"({"a":{"b":"a10","l11":"test"}, "a":{"c":null}, "a":"a5", "a":"a6"})";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  // Matching the leaf while it is not an array
  std::vector<std::string> gold;
  std::vector<std::optional<std::string>> scalar_gold;

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
  EXPECT_TRUE(is_null);

  std::vector<std::optional<std::string>> scalar_result;
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_TRUE(scalar_parser.StoppedOnFirstMatch());
  EXPECT_EQ(scalar_result, scalar_gold);
  EXPECT_TRUE(is_null);
}

TEST(JSONPathArrayExtractorTest, MatchedEmptyArray) {
  std::string input =
      R"({"a":{"b":"a10", "l11":"test"}, "a":{"c":{"d":[]}}, "a":"a5",
      "a":"a6"})";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold;
  std::vector<std::optional<std::string>> scalar_gold;

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_TRUE(scalar_parser.StoppedOnFirstMatch());
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, PartiallyMatchingSuffixes) {
  std::string input =
      R"({"a":{"b":"a10","l11":"test"}, "a":{"c":null}, "a":"a5", "a":"a6"})";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.c.d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold;
  std::vector<std::optional<std::string>> scalar_gold;

  // Parsing of JSON was successful however no match.
  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(parser.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(scalar_parser.StoppedOnFirstMatch());
  EXPECT_TRUE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, ReturnJSONObjectArray) {
  std::string input =
      R"({"e":{"b":"a10", "l11":"test"}, "a":{"c":null, "f":[{"g":"h"},
      {"g":["i", {"x":"j"}]}]}, "a":"a5", "a":"a6"})";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold(
      {Normalize("{ \"g\" : \"h\"}"),
       Normalize("{\"g\" : [ \"i\", { \"x\" : \"j\" } ] }")});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
}

TEST(JSONPathArrayExtractorTest, StopParserOnFirstMatch) {
  std::string input =
      R"({"a":{"b":{"c":{"d":["l1"]}}}, "a":{"b":{"c":{"e":"l2"}}},
      "a":{"b":{"c":{"d":"l3"}}}})";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a.b.c.d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"l1\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold = {"l1"};
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(scalar_parser.StoppedOnFirstMatch());
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, BasicArrayAccess) {
  std::string input =
      R"({"e":{"b":"a10", "l11":"test"},
      "a":{"c":null, "f":{"g":"h", "g":[["i"], ["j", "k"]]}},
      "a":"a5", "a":"a6"})";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"j\"", "\"k\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold = {"j", "k"};
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, AccessObjectInArrayMultipleSuffixes) {
  std::string input =
      R"({"e":{"b" : "a10", "l11":"test"},
      "a":{"f":null, "f":{"g":"h", "g":[["i"], ["j", "k"]]}},
      "a":"a5", "a":"a6"})";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a.f.g[1]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"j\"", "\"k\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold = {"j", "k"};
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, EscapedAccessTestNonSqlStandard) {
  // There are two escapings happening as follows:
  // a. C++ Compiler
  // b. JSON Parser
  //
  // So '4k' (k > = 1) backslashes translate to 'k' backslashes at runtime
  // "\\\\" = "\" at runtime. So "\\\\\\\\s" === "\\s"
  std::string input =
      R"({"e":{"b":"a10", "l11":"test"},
      "a":{"b":null, "''\\\\s ":{"g":"h", "g":["i", ["j", "k"]]}},
      "a":"a5", "a":"a6"})";
  absl::string_view input_str(input);
  std::string input_path("$.a['\\'\\'\\\\s '].g[ 1]");
  absl::string_view esc_input_path(input_path);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<ValidJSONPathIterator> path_itr,
                       ValidJSONPathIterator::Create(
                           esc_input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"j\"", "\"k\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold = {"j", "k"};
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest,
     EscapedAccessTestNonSqlStandardInvalidJsonPath) {
  std::string input_path("$.a.\"\'\'\\\\s \".g[ 1]");
  absl::string_view esc_input_path(input_path);

  absl::Status status =
      ValidJSONPathIterator::Create(esc_input_path, /*sql_standard_mode=*/false)
          .status();
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr(R"(Invalid token in JSONPath at: ."''\\s ".g[ 1])")));
}

TEST(JSONPathArrayExtractorTest, NestedArrayAccess) {
  std::string input =
      R"([0 ,[[], [[1, 4, 8, [2, 1, 0, ["3", "4"], 4], 11, 13]], [], "a"], 2,
      []])";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1][0][3][3]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"3\"", "\"4\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold({"3", "4"});
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, NegativeNestedArrayAccess) {
  std::string input =
      R"([0 ,[[], [[1, 4, 8, [2, 1, 0, ["3", "4"], 4], 11, 13]], [], "a"], 2,
      []])";
  absl::string_view input_str(input);
  absl::string_view input_path("$[1][1]['-0'][3][3]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"\"3\"", "\"4\""});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold = {"3", "4"};
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);

  absl::string_view input_path1("$[1][1]['-5'][3][3]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path1, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser1(input_str, path_itr1.get());

  std::vector<std::string> gold1({});

  EXPECT_TRUE(parser1.ExtractArray(&result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(parser1.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold1);

  scalar_result.clear();
  std::vector<std::optional<std::string>> scalar_gold1;
  JSONPathStringArrayExtractor scalar_parser1(input_str, path_itr1.get());
  EXPECT_TRUE(scalar_parser1.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_TRUE(is_null);
  EXPECT_FALSE(scalar_parser1.StoppedOnFirstMatch());
  EXPECT_EQ(scalar_result, scalar_gold1);
}

TEST(JSONPathArrayExtractorTest, MixedNestedArrayAccess) {
  std::string input =
      R"({"a":[0, [[], {"b":[7, [1, 4, 8, [2, 1, 0, {"a":{"b":"3"},
      "c":[1, 2, 3]}, 4], 11, 13]]}, [], "a"], 2,[]]})";
  absl::string_view input_str(input);
  absl::string_view input_path("$.a[1][1].b[1][3][3].c");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());
  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold({"1", "2", "3"});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(result, gold);

  std::vector<std::optional<std::string>> scalar_result;
  std::vector<std::optional<std::string>> scalar_gold = {"1", "2", "3"};
  JSONPathStringArrayExtractor scalar_parser(input_str, path_itr.get());
  EXPECT_TRUE(scalar_parser.ExtractStringArray(&scalar_result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(scalar_result, scalar_gold);
}

TEST(JSONPathArrayExtractorTest, QuotedArrayIndex) {
  std::string input =
      R"([0, [[], [[1, 4, 8, [2, 1, 0, [{"a":"3"}, {"a":"4"}], 4], 11, 13]], [],
      "a"], 2, []])";
  absl::string_view input_str(input);
  absl::string_view input_path("$['1'][1][0]['3']['3']");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::string> result;
  bool is_null;
  std::vector<std::string> gold(
      {Normalize(R"({"a":"3"})"), Normalize(R"({"a":"4"})")});

  EXPECT_TRUE(parser.ExtractArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);
}

TEST(JSONPathArrayStringExtractorTest, BasicParsing) {
  std::string input = R"(["a", 1, "2"])";
  absl::string_view input_str(input);
  absl::string_view input_path("$");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathStringArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::optional<std::string>> result;
  std::vector<std::optional<std::string>> gold = {"a", "1", "2"};
  bool is_null;

  EXPECT_TRUE(parser.ExtractStringArray(&result, &is_null));
  EXPECT_EQ(result, gold);
  EXPECT_FALSE(is_null);
}

TEST(JSONPathArrayExtractorTest, ValidateScalarResult) {
  std::string input =
      R"({"a":[{"a1":"a11"}, "a2" ],
      "b":["b1", ["b21", "b22"]],
      "c":[[],"c2"],
      "d":["d1", "tes\"t", 1.9834, null, 123]})";

  absl::string_view input_str(input);
  absl::string_view input_path("$.a");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathStringArrayExtractor parser(input_str, path_itr.get());

  std::vector<std::optional<std::string>> result;
  bool is_null;
  EXPECT_TRUE(parser.ExtractStringArray(&result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.b";
  result.clear();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr1,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathStringArrayExtractor parser1(input_str, path_itr1.get());
  EXPECT_TRUE(parser1.ExtractStringArray(&result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.c";
  result.clear();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr2,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathStringArrayExtractor parser2(input_str, path_itr2.get());
  EXPECT_TRUE(parser2.ExtractStringArray(&result, &is_null));
  EXPECT_TRUE(is_null);

  input_path = "$.d";
  result.clear();
  std::vector<std::optional<std::string>> gold = {"d1", "tes\"t", "1.9834",
                                                  std::nullopt, "123"};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<ValidJSONPathIterator> path_itr3,
      ValidJSONPathIterator::Create(input_path, /*sql_standard_mode=*/false));
  JSONPathStringArrayExtractor parser3(input_str, path_itr3.get());
  EXPECT_TRUE(parser3.ExtractStringArray(&result, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_TRUE(parser.StoppedOnFirstMatch());
  EXPECT_EQ(result, gold);
}

TEST(ValidJSONPathIterator, BasicTest) {
  std::string path = "$[1][1][0][3][3]";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr);
  itr.Rewind();
  EXPECT_EQ(*itr, "");
  ++itr;
  EXPECT_EQ(*itr, "1");
  ++itr;
  EXPECT_EQ(*itr, "1");
  ++itr;
  EXPECT_EQ(*itr, "0");
  ++itr;
  EXPECT_EQ(*itr, "3");
  ++itr;
  EXPECT_EQ(*itr, "3");
  ++itr;
  EXPECT_TRUE(itr.End());

  // reverse.
  --itr;
  EXPECT_EQ(*itr, "3");
  --itr;
  EXPECT_EQ(*itr, "3");
  --itr;
  EXPECT_EQ(*itr, "0");
  --itr;
  EXPECT_EQ(*itr, "1");
  --itr;
  EXPECT_EQ(*itr, "1");
  --itr;
  EXPECT_EQ(*itr, "");
  --itr;
  EXPECT_TRUE(itr.End());

  ++itr;
  EXPECT_EQ(*itr, "");
  ++itr;
  EXPECT_EQ(*itr, "1");
}

TEST(ValidJSONPathIterator, DegenerateCases) {
  std::string path = "$";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr = *(iptr);

  EXPECT_FALSE(itr.End());
  EXPECT_EQ(*itr, "");

  path = "$";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValidJSONPathIterator> iptr1,
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true));
  ValidJSONPathIterator& itr1 = *(iptr1);

  EXPECT_FALSE(itr1.End());
  EXPECT_EQ(*itr1, "");
}

TEST(ValidJSONPathIterator, InvalidEmptyJSONPathCreation) {
  std::string path = "$.a.*.b.c";
  absl::Status status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));

  path = "$.@";
  status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: @")));

  path = "$abc";
  status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("Invalid token in JSONPath at: abc")));

  path = "";
  status =
      ValidJSONPathIterator::Create(path, /*sql_standard_mode=*/true).status();
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("JSONPath must start with '$'")));
}

void ExtractArrayOrStringArray(
    JSONPathArrayExtractor* parser,
    std::vector<std::optional<std::string>>* output, bool* is_null,
    std::optional<std::function<void(absl::Status)>> issue_warning) {
  parser->set_special_character_escaping(true);
  parser->set_special_character_key_escaping(true);
  std::vector<std::string> result;
  parser->ExtractArray(&result, is_null, issue_warning);
  output->assign(result.begin(), result.end());
}

void ExtractArrayOrStringArray(
    JSONPathStringArrayExtractor* parser,
    std::vector<std::optional<std::string>>* output, bool* is_null,
    std::optional<std::function<void(absl::Status)>> ignored) {
  parser->ExtractStringArray(output, is_null);
}

template <class ParserClass>
void ComplianceJSONExtractArrayTest(absl::Span<const FunctionTestCall> tests,
                                    bool sql_standard_mode) {
  for (const FunctionTestCall& test : tests) {
    if (test.params.params()[0].is_null() ||
        test.params.params()[1].is_null()) {
      continue;
    }
    const std::string json = test.params.param(0).string_value();
    const std::string json_path = test.params.param(1).string_value();
    const Value& expected_result = test.params.result();

    std::vector<std::optional<std::string>> output;
    std::vector<Value> result_array;
    absl::Status status;
    bool is_null = true;
    auto evaluator_status =
        ValidJSONPathIterator::Create(json_path, sql_standard_mode);
    if (evaluator_status.ok()) {
      bool is_warning_called = false;
      const std::unique_ptr<ValidJSONPathIterator>& path_itr =
          evaluator_status.value();
      ParserClass parser(json, path_itr.get());
      // Because key_escaping is enabled a warning should never be
      // triggered.
      ExtractArrayOrStringArray(&parser, &output, &is_null,
                                [&is_warning_called](absl::Status status) {
                                  is_warning_called = true;
                                });
      EXPECT_FALSE(is_warning_called);
    } else {
      status = evaluator_status.status();
    }

    if (!status.ok() || !test.params.status().ok()) {
      EXPECT_EQ(test.params.status().code(), status.code()) << status;
    } else {
      for (const auto& element : output) {
        result_array.push_back(element.has_value() ? values::String(*element)
                                                   : values::NullString());
      }
      Value result = values::UnsafeArray(types::StringArrayType(),
                                         std::move(result_array));
      EXPECT_EQ(is_null, expected_result.is_null());
      if (!expected_result.is_null()) {
        EXPECT_EQ(result, expected_result);
      }
    }
  }
}

// Compliance Tests on JSON_QUERY_ARRAY
TEST(JSONPathExtractor, ComplianceJSONQueryArray) {
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsStringJsonQueryArray();
  ComplianceJSONExtractArrayTest<JSONPathArrayExtractor>(
      tests, /*sql_standard_mode=*/true);
}

// Compliance Tests on JSON_EXTRACT_ARRAY
TEST(JSONPathExtractor, ComplianceJSONExtractArray) {
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsStringJsonExtractArray();
  ComplianceJSONExtractArrayTest<JSONPathArrayExtractor>(
      tests, /*sql_standard_mode=*/false);
}

// Compliance Tests on JSON_VALUE_ARRAY
TEST(JSONPathExtractor, ComplianceJSONValueArray) {
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsStringJsonValueArray();
  ComplianceJSONExtractArrayTest<JSONPathStringArrayExtractor>(
      tests, /*sql_standard_mode=*/true);
}

// Compliance Tests on JSON_EXTRACT_STRING_ARRAY
TEST(JSONPathExtractor, ComplianceJSONExtractStringArray) {
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsStringJsonExtractStringArray();
  ComplianceJSONExtractArrayTest<JSONPathStringArrayExtractor>(
      tests, /*sql_standard_mode=*/false);
}

TEST(JsonPathEvaluatorTest, ExtractingArrayCloseToLimitSucceeds) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth;
  const std::string nested_array_json(kNestingDepth, '[');
  std::string value;
  std::vector<std::string> array_value;
  std::vector<std::optional<std::string>> scalar_array_value;
  absl::Status status;
  bool is_null = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create(
          "$", /*sql_standard_mode=*/true,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));
  // Extracting should succeed, but the result is null since the arrays are not
  // closed.
  GOOGLESQL_EXPECT_OK(path_evaluator->Extract(nested_array_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator, JsonPathEvaluator::Create(
                          "$", /*sql_standard_mode=*/true,
                          /*enable_special_character_escaping_in_values=*/false,
                          /*enable_special_character_escaping_in_keys=*/false));
  // Extracting should succeed, but the result is null since the arrays are not
  // closed.
  GOOGLESQL_EXPECT_OK(path_evaluator->ExtractScalar(nested_array_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator, JsonPathEvaluator::Create(
                          "$", /*sql_standard_mode=*/false,
                          /*enable_special_character_escaping_in_values=*/false,
                          /*enable_special_character_escaping_in_keys=*/false));
  // Extracting should succeed, but the result is null since the arrays are not
  // closed.
  GOOGLESQL_EXPECT_OK(
      path_evaluator->ExtractArray(nested_array_json, &array_value, &is_null));
  EXPECT_TRUE(is_null);
  GOOGLESQL_EXPECT_OK(path_evaluator->ExtractStringArray(nested_array_json,
                                               &scalar_array_value, &is_null));
  EXPECT_TRUE(is_null);
}

TEST(JsonPathEvaluatorTest, DeeplyNestedArrayCausesFailure) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth + 1;
  const std::string nested_array_json(kNestingDepth, '[');
  std::string json_path = "$";
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&json_path, "[0]");
  }
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create(
          json_path, /*sql_standard_mode=*/true,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));
  std::string value;
  std::vector<std::string> array_value;
  std::vector<std::optional<std::string>> scalar_array_value;
  bool is_null = true;
  EXPECT_THAT(path_evaluator->Extract(nested_array_json, &value, &is_null),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "JSON parsing failed due to deeply nested array/struct. "
                       "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  EXPECT_THAT(
      path_evaluator->ExtractScalar(nested_array_json, &value, &is_null),
      StatusIs(absl::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator, JsonPathEvaluator::Create(
                          json_path, /*sql_standard_mode=*/false,
                          /*enable_special_character_escaping_in_values=*/false,
                          /*enable_special_character_escaping_in_keys=*/false));
  EXPECT_THAT(
      path_evaluator->ExtractArray(nested_array_json, &array_value, &is_null),
      StatusIs(absl::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_THAT(path_evaluator->ExtractStringArray(nested_array_json,
                                                 &scalar_array_value, &is_null),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "JSON parsing failed due to deeply nested array/struct. "
                       "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
}

TEST(JsonPathEvaluatorTest, ExtractingObjectCloseToLimitSucceeds) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth;
  std::string nested_object_json;
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&nested_object_json, "{\"x\":");
  }
  std::string value;
  std::vector<std::string> array_value;
  std::vector<std::optional<std::string>> scalar_array_value;
  absl::Status status;
  bool is_null = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create(
          "$", /*sql_standard_mode=*/true,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));
  // Extracting should succeed, but the result is null since the objects are not
  // closed.
  GOOGLESQL_EXPECT_OK(path_evaluator->Extract(nested_object_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator, JsonPathEvaluator::Create(
                          "$", /*sql_standard_mode=*/true,
                          /*enable_special_character_escaping_in_values=*/false,
                          /*enable_special_character_escaping_in_keys=*/false));
  // Extracting should succeed, but the result is null since the objects are not
  // closed.
  GOOGLESQL_EXPECT_OK(
      path_evaluator->ExtractScalar(nested_object_json, &value, &is_null));
  EXPECT_TRUE(is_null);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator, JsonPathEvaluator::Create(
                          "$", /*sql_standard_mode=*/false,
                          /*enable_special_character_escaping_in_values=*/false,
                          /*enable_special_character_escaping_in_keys=*/false));
  // Extracting should succeed, but the result is null since the objects are not
  // closed.
  GOOGLESQL_EXPECT_OK(
      path_evaluator->ExtractArray(nested_object_json, &array_value, &is_null));
  EXPECT_TRUE(is_null);
  GOOGLESQL_EXPECT_OK(path_evaluator->ExtractStringArray(nested_object_json,
                                               &scalar_array_value, &is_null));
  EXPECT_TRUE(is_null);
}

TEST(JsonPathEvaluatorTest, DeeplyNestedObjectCausesFailure) {
  const int kNestingDepth = JSONPathExtractor::kMaxParsingDepth + 1;
  std::string nested_object_json;
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&nested_object_json, "{\"x\":");
  }
  std::string json_path = "$";
  for (int i = 0; i < kNestingDepth; ++i) {
    absl::StrAppend(&json_path, ".x");
  }
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JsonPathEvaluator> path_evaluator,
      JsonPathEvaluator::Create(
          json_path, /*sql_standard_mode=*/true,
          /*enable_special_character_escaping_in_values=*/false,
          /*enable_special_character_escaping_in_keys=*/false));

  std::string value;
  std::vector<std::string> array_value;
  std::vector<std::optional<std::string>> scalar_array_value;
  bool is_null = true;
  EXPECT_THAT(path_evaluator->Extract(nested_object_json, &value, &is_null),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "JSON parsing failed due to deeply nested array/struct. "
                       "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  EXPECT_THAT(
      path_evaluator->ExtractScalar(nested_object_json, &value, &is_null),
      StatusIs(absl::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      path_evaluator, JsonPathEvaluator::Create(
                          json_path, /*sql_standard_mode=*/false,
                          /*enable_special_character_escaping_in_values=*/false,
                          /*enable_special_character_escaping_in_keys=*/false));
  EXPECT_THAT(
      path_evaluator->ExtractArray(nested_object_json, &array_value, &is_null),
      StatusIs(absl::StatusCode::kOutOfRange,
               "JSON parsing failed due to deeply nested array/struct. "
               "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
  EXPECT_THAT(path_evaluator->ExtractStringArray(nested_object_json,
                                                 &scalar_array_value, &is_null),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "JSON parsing failed due to deeply nested array/struct. "
                       "Maximum nesting depth is 1000"));
  EXPECT_TRUE(is_null);
}

TEST(JsonConversionTest, ConvertJsonToInt64) {
  std::vector<std::pair<JSONValue, std::optional<int64_t>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}), -1);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(10.0), 10);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}),
      std::numeric_limits<int32_t>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::min()),
      std::numeric_limits<int64_t>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::max()),
      std::numeric_limits<int64_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}),
      std::numeric_limits<uint32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()), std::nullopt);
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(1e100), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(1.5), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()), std::nullopt);

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("INT64('$0')", input.GetConstRef().ToString()));

    absl::StatusOr<int64_t> output = ConvertJsonToInt64(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToInt32) {
  std::vector<std::pair<JSONValue, std::optional<int32_t>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}), -1);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(10.0), 10);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}),
      std::numeric_limits<int32_t>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::min()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::max()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}), std::nullopt);
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(1e100), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(1.5), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("INT32('$0')", input.GetConstRef().ToString()));

    absl::StatusOr<int32_t> output = ConvertJsonToInt32(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToUint64) {
  std::vector<std::pair<JSONValue, std::optional<uint64_t>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(10.0), 10);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::min()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::max()}),
      std::numeric_limits<int64_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}),
      std::numeric_limits<uint32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}),
      std::numeric_limits<uint64_t>::max());
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(1e100), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(1.5), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("UINT64('$0')", input.GetConstRef().ToString()));

    absl::StatusOr<uint64_t> output = ConvertJsonToUint64(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToUint32) {
  std::vector<std::pair<JSONValue, std::optional<uint32_t>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(10.0), 10);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::min()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}),
      std::numeric_limits<uint32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}), std::nullopt);
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(1e100), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(1.5), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("UINT32('$0')", input.GetConstRef().ToString()));

    absl::StatusOr<uint32_t> output = ConvertJsonToUint32(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToBool) {
  std::vector<std::pair<JSONValue, std::optional<bool>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(false), false);
  inputs_and_expected_outputs.emplace_back(JSONValue(true), true);
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("BOOL('$0')", input.GetConstRef().ToString()));

    absl::StatusOr<bool> output = ConvertJsonToBool(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToString) {
  std::vector<std::pair<JSONValue, std::optional<std::string>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"test"}),
                                           "test");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"abc123"}),
                                           "abc123");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"TesT"}),
                                           "TesT");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1"}), "1");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{""}), "");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"12¿©?Æ"}),
                                           "12¿©?Æ");
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("STRING('$0')", input.GetConstRef().ToString()));

    absl::StatusOr<std::string> output =
        ConvertJsonToString(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToDouble) {
  std::vector<std::pair<JSONValue, std::optional<double>>>
      inputs_and_expected_outputs;
  // Behavior the same when wide_number_mode is "round" and "exact"
  inputs_and_expected_outputs.emplace_back(JSONValue(1.0), 1.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(-1.0), -1.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), double{1});
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}), double{-1});
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), double{1});
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{-9007199254740992}), double{-9007199254740992});
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{9007199254740992}),
                                           double{9007199254740992});
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<float>::min()),
      std::numeric_limits<float>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<float>::lowest()),
      std::numeric_limits<float>::lowest());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<float>::max()),
      std::numeric_limits<float>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<double>::min()),
      std::numeric_limits<double>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<double>::lowest()),
      std::numeric_limits<double>::lowest());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<double>::max()),
      std::numeric_limits<double>::max());

  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(absl::Substitute("DOUBLE('$0', 'exact')",
                                  input.GetConstRef().ToString()));
    absl::StatusOr<double> output = ConvertJsonToDouble(
        input.GetConstRef(), WideNumberMode::kExact, PRODUCT_INTERNAL);
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
    SCOPED_TRACE(absl::Substitute("DOUBLE('$0', 'round')",
                                  input.GetConstRef().ToString()));
    output = ConvertJsonToDouble(input.GetConstRef(), WideNumberMode::kRound,
                                 PRODUCT_INTERNAL);
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToDoubleFailInExactMode) {
  std::vector<std::pair<JSONValue, double>> inputs_and_expected_outputs;
  // Number too large to round trip
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{18446744073709551615u}),
      double{1.8446744073709552e+19});
  // Number too small to round trip
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{-9007199254740993}), double{-9007199254740992});

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(absl::Substitute("DOUBLE('$0', 'round')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(ConvertJsonToDouble(input.GetConstRef(), WideNumberMode::kRound,
                                    PRODUCT_INTERNAL),
                IsOkAndHolds(expected_output));
    SCOPED_TRACE(absl::Substitute("DOUBLE('$0', 'exact')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(
        ConvertJsonToDouble(input.GetConstRef(), WideNumberMode::kExact,
                            PRODUCT_INTERNAL),
        StatusIs(
            absl::StatusCode::kOutOfRange,
            HasSubstr(
                "cannot be converted to DOUBLE without loss of precision")));
  }
}

TEST(JsonConversionTest, ConvertJsonToDoubleErrorMessage) {
  JSONValue input = JSONValue(uint64_t{18446744073709551615u});
  // Internal mode uses DOUBLE in error message
  SCOPED_TRACE(absl::Substitute("DOUBLE('$0', 'exact')",
                                input.GetConstRef().ToString()));
  EXPECT_THAT(
      ConvertJsonToDouble(input.GetConstRef(), WideNumberMode::kExact,
                          PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("JSON number: 18446744073709551615 cannot be "
                         "converted to DOUBLE without loss of precision")));
  // External mode uses FLOAT64 in error message
  SCOPED_TRACE(absl::Substitute("FLOAT64('$0', 'exact')",
                                input.GetConstRef().ToString()));
  EXPECT_THAT(
      ConvertJsonToDouble(input.GetConstRef(), WideNumberMode::kExact,
                          PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("JSON number: 18446744073709551615 cannot be "
                         "converted to FLOAT64 without loss of precision")));
}

TEST(JsonConversionTest, ConvertJsonToFloat) {
  std::vector<std::pair<JSONValue, std::optional<float>>>
      inputs_and_expected_outputs;
  // Behavior the same when wide_number_mode is "round" and "exact"
  inputs_and_expected_outputs.emplace_back(JSONValue(1.0), 1.0f);
  inputs_and_expected_outputs.emplace_back(JSONValue(-1.0), -1.0f);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), 1.0f);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}), -1.0f);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1.0f);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-16777216}),
                                           float{-16777216});
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{16777216}),
                                           float{16777216});
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<float>::min()),
      std::numeric_limits<float>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<float>::lowest()),
      std::numeric_limits<float>::lowest());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<float>::max()),
      std::numeric_limits<float>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<double>::lowest()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<double>::max()), std::nullopt);
  // Other types should return an error
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), std::nullopt);

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    {
      SCOPED_TRACE(absl::Substitute("FLOAT('$0', 'exact')",
                                    input.GetConstRef().ToString()));
      absl::StatusOr<float> output = ConvertJsonToFloat(
          input.GetConstRef(), WideNumberMode::kExact, PRODUCT_INTERNAL);
      EXPECT_EQ(output.ok(), expected_output.has_value());
      if (output.ok() && expected_output.has_value()) {
        EXPECT_EQ(*output, *expected_output);
      }
    }
    {
      SCOPED_TRACE(absl::Substitute("FLOAT('$0', 'round')",
                                    input.GetConstRef().ToString()));
      absl::StatusOr<float> output = ConvertJsonToFloat(
          input.GetConstRef(), WideNumberMode::kRound, PRODUCT_INTERNAL);
      EXPECT_EQ(output.ok(), expected_output.has_value());
      if (output.ok() && expected_output.has_value()) {
        EXPECT_EQ(*output, *expected_output);
      }
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToFloatFailInExactMode) {
  std::vector<std::pair<JSONValue, float>> inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<double>::min()), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{16777217}),
                                           float{16777216});
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-16777217}),
                                           float{-16777216});

  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(absl::Substitute("FLOAT('$0', 'round')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(ConvertJsonToFloat(input.GetConstRef(), WideNumberMode::kRound,
                                   PRODUCT_INTERNAL),
                IsOkAndHolds(expected_output));
    SCOPED_TRACE(absl::Substitute("FLOAT('$0', 'exact')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(
        ConvertJsonToFloat(input.GetConstRef(), WideNumberMode::kExact,
                           PRODUCT_INTERNAL),
        StatusIs(
            absl::StatusCode::kOutOfRange,
            HasSubstr(
                "cannot be converted to FLOAT without loss of precision")));
  }
}

TEST(JsonConversionTest, ConvertJsonToFloatErrorMessage) {
  JSONValue input = JSONValue(uint64_t{16777217});
  // Internal mode uses FLOAT in error message
  SCOPED_TRACE(
      absl::Substitute("FLOAT('$0', 'exact')", input.GetConstRef().ToString()));
  EXPECT_THAT(
      ConvertJsonToFloat(input.GetConstRef(), WideNumberMode::kExact,
                         PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("JSON number: 16777217 cannot be converted to FLOAT "
                         "without loss of precision")));

  // External mode uses FLOAT32 in error message
  SCOPED_TRACE(absl::Substitute("FLOAT64('$0', 'exact')",
                                input.GetConstRef().ToString()));
  EXPECT_THAT(
      ConvertJsonToFloat(input.GetConstRef(), WideNumberMode::kExact,
                         PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("JSON number: 16777217 cannot be converted to FLOAT32 "
                         "without loss of precision")));
}

TEST(JsonConversionTest, ConvertJsonToInt64Array) {
  std::vector<std::pair<std::string, std::optional<std::vector<int64_t>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<int64_t>{});
  cases.emplace_back(R"([1])", std::vector<int64_t>{1});
  cases.emplace_back(R"([-1])", std::vector<int64_t>{-1});
  cases.emplace_back(R"([10.0])", std::vector<int64_t>{10});
  cases.emplace_back(R"([1, -1, 10.0])", std::vector<int64_t>{1, -1, 10});
  cases.emplace_back(R"([18446744073709551615])", std::nullopt);
  cases.emplace_back(
      R"([4294967295])",
      std::vector<int64_t>{std::numeric_limits<uint32_t>::max()});
  cases.emplace_back(R"([-9223372036854775808])",
                     std::vector<int64_t>{std::numeric_limits<int64_t>::min()});
  cases.emplace_back(R"([9223372036854775807])",
                     std::vector<int64_t>{std::numeric_limits<int64_t>::max()});
  cases.emplace_back(R"([-2147483648])",
                     std::vector<int64_t>{std::numeric_limits<int32_t>::min()});
  cases.emplace_back(R"([2147483647])",
                     std::vector<int64_t>{std::numeric_limits<int32_t>::max()});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"([true])", std::nullopt);
  cases.emplace_back(R"([1.5])", std::nullopt);
  cases.emplace_back(R"([[1]])", std::nullopt);
  cases.emplace_back(R"({"a": 1})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(
        absl::Substitute("INT64_ARRAY('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<int64_t>> output =
        ConvertJsonToInt64Array(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToInt32Array) {
  std::vector<std::pair<std::string, std::optional<std::vector<int32_t>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<int32_t>{});
  cases.emplace_back(R"([1])", std::vector<int32_t>{1});
  cases.emplace_back(R"([-1])", std::vector<int32_t>{-1});
  cases.emplace_back(R"([10.0])", std::vector<int32_t>{10});
  cases.emplace_back(R"([1, -1, 10.0])", std::vector<int32_t>{1, -1, 10});
  cases.emplace_back(R"([18446744073709551615])", std::nullopt);
  cases.emplace_back(R"([4294967295])", std::nullopt);
  cases.emplace_back(R"([-9223372036854775808])", std::nullopt);
  cases.emplace_back(R"([9223372036854775807])", std::nullopt);
  cases.emplace_back(R"([-2147483648])",
                     std::vector<int32_t>{std::numeric_limits<int32_t>::min()});
  cases.emplace_back(R"([2147483647])",
                     std::vector<int32_t>{std::numeric_limits<int32_t>::max()});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"([true])", std::nullopt);
  cases.emplace_back(R"([1.5])", std::nullopt);
  cases.emplace_back(R"([[1]])", std::nullopt);
  cases.emplace_back(R"({"a": 1})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(
        absl::Substitute("INT32_ARRAY('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<int32_t>> output =
        ConvertJsonToInt32Array(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToUint64Array) {
  std::vector<std::pair<std::string, std::optional<std::vector<uint64_t>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<uint64_t>{});
  cases.emplace_back(R"([1])", std::vector<uint64_t>{1});
  cases.emplace_back(R"([10.0])", std::vector<uint64_t>{10});
  cases.emplace_back(R"([1, 10.0])", std::vector<uint64_t>{1, 10});
  cases.emplace_back(R"([-1])", std::nullopt);
  cases.emplace_back(R"([1, -1, 10.0])", std::nullopt);
  cases.emplace_back(
      R"([18446744073709551615])",
      std::vector<uint64_t>{std::numeric_limits<uint64_t>::max()});
  cases.emplace_back(
      R"([4294967295])",
      std::vector<uint64_t>{std::numeric_limits<uint32_t>::max()});
  cases.emplace_back(R"([-9223372036854775808])", std::nullopt);
  cases.emplace_back(
      R"([9223372036854775807])",
      std::vector<uint64_t>{std::numeric_limits<int64_t>::max()});
  cases.emplace_back(R"([-2147483648])", std::nullopt);
  cases.emplace_back(
      R"([2147483647])",
      std::vector<uint64_t>{std::numeric_limits<int32_t>::max()});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"([true])", std::nullopt);
  cases.emplace_back(R"([1.5])", std::nullopt);
  cases.emplace_back(R"([[1]])", std::nullopt);
  cases.emplace_back(R"({"a": 1})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(
        absl::Substitute("UINT64_ARRAY('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<uint64_t>> output =
        ConvertJsonToUint64Array(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToUint32Array) {
  std::vector<std::pair<std::string, std::optional<std::vector<uint32_t>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<uint32_t>{});
  cases.emplace_back(R"([1])", std::vector<uint32_t>{1});
  cases.emplace_back(R"([10.0])", std::vector<uint32_t>{10});
  cases.emplace_back(R"([1, 10.0])", std::vector<uint32_t>{1, 10});
  cases.emplace_back(R"([-1])", std::nullopt);
  cases.emplace_back(R"([1, -1, 10.0])", std::nullopt);
  cases.emplace_back(R"([18446744073709551615])", std::nullopt);
  cases.emplace_back(
      R"([4294967295])",
      std::vector<uint32_t>{std::numeric_limits<uint32_t>::max()});
  cases.emplace_back(R"([-9223372036854775808])", std::nullopt);
  cases.emplace_back(R"([9223372036854775807])", std::nullopt);
  cases.emplace_back(R"([-2147483648])", std::nullopt);
  cases.emplace_back(
      R"([2147483647])",
      std::vector<uint32_t>{std::numeric_limits<int32_t>::max()});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"([true])", std::nullopt);
  cases.emplace_back(R"([1.5])", std::nullopt);
  cases.emplace_back(R"([[1]])", std::nullopt);
  cases.emplace_back(R"({"a": 1})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(
        absl::Substitute("UINT32_ARRAY('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<uint32_t>> output =
        ConvertJsonToUint32Array(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToBoolArray) {
  std::vector<std::pair<std::string, std::optional<std::vector<bool>>>> cases;
  cases.emplace_back(R"([])", std::vector<bool>{});
  cases.emplace_back(R"([false])", std::vector<bool>{false});
  cases.emplace_back(R"([true])", std::vector<bool>{true});
  cases.emplace_back(R"([false, true])", std::vector<bool>{false, true});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"([1])", std::nullopt);
  cases.emplace_back(R"([[false]])", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(
        absl::Substitute("BOOL_ARRAY('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<bool>> output =
        ConvertJsonToBoolArray(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToStringArray) {
  std::vector<std::pair<std::string, std::optional<std::vector<std::string>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::string>{});
  cases.emplace_back(R"([""])", std::vector<std::string>{""});
  cases.emplace_back(R"(["a"])", std::vector<std::string>{"a"});
  cases.emplace_back(R"(["", "a", "null"])",
                     std::vector<std::string>{"", "a", "null"});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::vector<std::string>{"null"});
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"("a")", std::nullopt);
  cases.emplace_back(R"([1])", std::nullopt);
  cases.emplace_back(R"([["a"]])", std::nullopt);
  cases.emplace_back(R"({"a": "b"})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(
        absl::Substitute("STRING_ARRAY('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<std::string>> output =
        ConvertJsonToStringArray(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToDoubleArray) {
  std::vector<std::pair<std::string, std::optional<std::vector<double>>>> cases;
  cases.emplace_back(R"([])", std::vector<double>{});
  cases.emplace_back(R"([1.0])", std::vector<double>{1.0});
  cases.emplace_back(R"([-1.0])", std::vector<double>{-1.0});
  cases.emplace_back(R"([1.0, -1.0])", std::vector<double>{1.0, -1.0});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("1.0")", std::nullopt);
  cases.emplace_back(R"([false])", std::nullopt);
  cases.emplace_back(R"([[1.0]])", std::nullopt);
  cases.emplace_back(R"({"a": 1.0})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("DOUBLE_ARRAY('$0', 'exact')",
                                  input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<double>> output = ConvertJsonToDoubleArray(
        input.GetConstRef(), WideNumberMode::kExact, PRODUCT_INTERNAL);
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
    SCOPED_TRACE(absl::Substitute("DOUBLE_ARRAY('$0', 'round')",
                                  input.GetConstRef().ToString()));
    output = ConvertJsonToDoubleArray(input.GetConstRef(),
                                      WideNumberMode::kRound, PRODUCT_INTERNAL);
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToDoubleArrayFailInExactOnly) {
  std::vector<std::pair<std::string, std::vector<double>>> cases;
  // Number too large to round trip
  cases.emplace_back(R"([18446744073709551615])",
                     std::vector<double>{1.8446744073709552e+19});
  // Number too small to round trip
  cases.emplace_back(R"([-9007199254740993])",
                     std::vector<double>{-9007199254740992});

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("DOUBLE_ARRAY('$0', 'round')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(
        ConvertJsonToDoubleArray(input.GetConstRef(), WideNumberMode::kRound,
                                 PRODUCT_INTERNAL),
        IsOkAndHolds(expected_output));
    SCOPED_TRACE(absl::Substitute("DOUBLE_ARRAY('$0', 'exact')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(
        ConvertJsonToDoubleArray(input.GetConstRef(), WideNumberMode::kExact,
                                 PRODUCT_INTERNAL),
        StatusIs(
            absl::StatusCode::kOutOfRange,
            HasSubstr(
                R"(cannot be converted to DOUBLE without loss of precision)")));
  }
}

TEST(JsonConversionTest, ConvertJsonToFloatArray) {
  std::vector<std::pair<std::string, std::optional<std::vector<float>>>> cases;
  cases.emplace_back(R"([])", std::vector<float>{});
  cases.emplace_back(R"([1.0])", std::vector<float>{1.0});
  cases.emplace_back(R"([-1.0])", std::vector<float>{-1.0});
  cases.emplace_back(R"([1.0, -1.0])", std::vector<float>{1.0, -1.0});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"([null])", std::nullopt);
  cases.emplace_back(R"(["null"])", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("1.0")", std::nullopt);
  cases.emplace_back(R"([false])", std::nullopt);
  cases.emplace_back(R"([[1.0]])", std::nullopt);
  cases.emplace_back(R"({"a": 1.0})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("FLOAT_ARRAY('$0', 'exact')",
                                  input.GetConstRef().ToString()));
    absl::StatusOr<std::vector<float>> output = ConvertJsonToFloatArray(
        input.GetConstRef(), WideNumberMode::kExact, PRODUCT_INTERNAL);
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
    SCOPED_TRACE(absl::Substitute("FLOAT_ARRAY('$0', 'round')",
                                  input.GetConstRef().ToString()));
    output = ConvertJsonToFloatArray(input.GetConstRef(),
                                     WideNumberMode::kRound, PRODUCT_INTERNAL);
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonConversionTest, ConvertJsonToFloatArrayFailInExactOnly) {
  std::vector<std::pair<std::string, std::vector<float>>> cases;
  // Number too large to round trip
  cases.emplace_back(R"([16777217])", std::vector<float>{16777216});
  // Number too small to round trip
  cases.emplace_back(R"([-16777217])", std::vector<float>{-16777216});

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("FLOAT_ARRAY('$0', 'round')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(
        ConvertJsonToFloatArray(input.GetConstRef(), WideNumberMode::kRound,
                                PRODUCT_INTERNAL),
        IsOkAndHolds(expected_output));

    SCOPED_TRACE(absl::Substitute("FLOAT_ARRAY('$0', 'exact')",
                                  input.GetConstRef().ToString()));

    EXPECT_THAT(
        ConvertJsonToFloatArray(input.GetConstRef(), WideNumberMode::kExact,
                                PRODUCT_INTERNAL),
        StatusIs(
            absl::StatusCode::kOutOfRange,
            HasSubstr(
                R"(cannot be converted to FLOAT without loss of precision)")));
  }
}

TEST(JsonConversionTest, GetJsonType) {
  std::vector<std::pair<JSONValue, std::optional<std::string>>>
      inputs_and_expected_outputs;
  inputs_and_expected_outputs.emplace_back(JSONValue(2.0), "number");
  inputs_and_expected_outputs.emplace_back(JSONValue(-1.0), "number");
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{1}), "number");
  inputs_and_expected_outputs.emplace_back(JSONValue(true), "boolean");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}),
                                           "string");
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), "object");
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"([10, 20])").value(), "array");
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("null").value(), "null");
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("TYPE('$0')", input.GetConstRef().ToString()));
    absl::StatusOr<std::string> output = GetJsonType(input.GetConstRef());
    EXPECT_EQ(output.ok(), expected_output.has_value());
    if (output.ok() && expected_output.has_value()) {
      EXPECT_EQ(*output, *expected_output);
    }
  }
}

TEST(JsonLaxConversionTest, Bool) {
  std::vector<std::pair<JSONValue, std::optional<bool>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), true);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), false);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"true"}),
                                           true);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"false"}),
                                           false);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"TRue"}),
                                           true);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"FaLse"}),
                                           false);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), false);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{10}), true);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), true);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::min()}), true);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}), true);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{0.0}), false);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), true);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{-1.1}), true);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}), true);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}), true);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}), true);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("-0").value(), false);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("-0.0").value(), false);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("-0.0e2").value(), false);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[true]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_BOOL($0)", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToBool(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonLaxConversionTest, Int64) {
  std::vector<std::pair<JSONValue, std::optional<int64_t>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), 0);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1"}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1e2"}),
                                           110);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"+1.5"}), 2);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::string{"123456789012345678.0"}), 123456789012345678);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1e100"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{10}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}), -1);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}),
      std::numeric_limits<int32_t>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::min()),
      std::numeric_limits<int64_t>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::max()),
      std::numeric_limits<int64_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}),
      std::numeric_limits<uint32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{3.5}), 4);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1e2}), 110);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{123456789012345678.0}), 123456789012345680);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}), 0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), std::nullopt);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_INT64('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToInt64(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonLaxConversionTest, Int32) {
  std::vector<std::pair<JSONValue, std::optional<int32_t>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), 0);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1"}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1e2"}),
                                           110);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"+1.5"}), 2);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::string{"123456789012345678.0"}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1e100"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{10}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}), -1);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}),
      std::numeric_limits<int32_t>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::min()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::max()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{3.5}), 4);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1e2}), 110);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{123456789012345678.0}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}), 0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), std::nullopt);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_INT32('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToInt32(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonLaxConversionTest, Uint64) {
  std::vector<std::pair<JSONValue, std::optional<uint64_t>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), 0);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1"}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1e2"}),
                                           110);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"+1.5"}), 2);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::string{"123456789012345678.0"}), 123456789012345678);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1e100"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{10}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::min()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::max()),
      std::numeric_limits<int64_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}),
      std::numeric_limits<uint32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()),
      std::numeric_limits<uint64_t>::max());
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{3.5}), 4);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1e2}), 110);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{123456789012345678.0}), 123456789012345680);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}), 0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), std::nullopt);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_UINT64('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToUint64(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonLaxConversionTest, Uint32) {
  std::vector<std::pair<JSONValue, std::optional<int32_t>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), 0);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1"}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1e2"}),
                                           110);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"+1.5"}), 2);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::string{"123456789012345678.0"}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1e100"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{0}), 0);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{10}), 10);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-1}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::min()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int32_t>::max()}),
      std::numeric_limits<int32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::min()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<int64_t>::max()), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint32_t>::max()}),
      std::numeric_limits<uint32_t>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::numeric_limits<uint64_t>::max()), std::nullopt);

  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), 1);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{3.5}), 4);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1e2}), 110);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{123456789012345678.0}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}), 0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), std::nullopt);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_UINT32('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToUint32(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonLaxConversionTest, Double) {
  std::vector<std::pair<JSONValue, std::optional<double>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), std::nullopt);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), 10.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"-10"}),
                                           -10.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1"}), 1.1);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1e2"}),
                                           110.0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::string{"9007199254740993"}), 9007199254740992.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"+1.5"}), 1.5);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-10}), -10);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{9007199254740993}),
                                           9007199254740992);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::min()}),
      static_cast<double>(std::numeric_limits<int64_t>::min()));
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::max()}),
      static_cast<double>(std::numeric_limits<int64_t>::max()));
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}),
      static_cast<double>((std::numeric_limits<uint64_t>::max())));
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), 1.1);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{3.5}), 3.5);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1.1e2").value(), 110);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}),
      std::numeric_limits<double>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}),
      std::numeric_limits<double>::lowest());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}),
      std::numeric_limits<double>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), 1e+100);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_FLOAT64('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToFloat64(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }

  // Special cases.
  EXPECT_THAT(
      LaxConvertJsonToFloat64(JSONValue(std::string{"NaN"}).GetConstRef()),
      IsOkAndHolds(Optional(IsNan())));
  EXPECT_THAT(
      LaxConvertJsonToFloat64(JSONValue(std::string{"Inf"}).GetConstRef()),
      IsOkAndHolds(std::numeric_limits<double>::infinity()));
  EXPECT_THAT(LaxConvertJsonToFloat64(
                  JSONValue(std::string{"-InfiNiTY"}).GetConstRef()),
              IsOkAndHolds(-std::numeric_limits<double>::infinity()));
}

TEST(JsonLaxConversionTest, Float) {
  std::vector<std::pair<JSONValue, std::optional<float>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(false), std::nullopt);
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), 10.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"-10"}),
                                           -10.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1"}), 1.1);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"1.1e2"}),
                                           110.0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(std::string{"9007199254740993"}), 9007199254740992.0);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"+1.5"}), 1.5);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           std::nullopt);
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           std::nullopt);
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-10}), -10);
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{9007199254740993}),
                                           9007199254740992);
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), 1);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::min()}),
      static_cast<float>(std::numeric_limits<int64_t>::min()));
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::max()}),
      static_cast<float>(std::numeric_limits<int64_t>::max()));
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}),
      static_cast<float>((std::numeric_limits<uint64_t>::max())));
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), 1.1);
  inputs_and_expected_outputs.emplace_back(JSONValue(double{3.5}), 3.5);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1.1e2").value(), 110);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<float>::min()}),
      std::numeric_limits<float>::min());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<float>::lowest()}),
      std::numeric_limits<float>::lowest());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<float>::max()}),
      std::numeric_limits<float>::max());
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}), 0.0);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), std::nullopt);
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_FLOAT32('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToFloat32(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }

  // Special cases.
  EXPECT_THAT(
      LaxConvertJsonToFloat32(JSONValue(std::string{"NaN"}).GetConstRef()),
      IsOkAndHolds(Optional(IsNan())));
  EXPECT_THAT(
      LaxConvertJsonToFloat32(JSONValue(std::string{"Inf"}).GetConstRef()),
      IsOkAndHolds(std::numeric_limits<float>::infinity()));
  EXPECT_THAT(LaxConvertJsonToFloat32(
                  JSONValue(std::string{"-InfiNiTY"}).GetConstRef()),
              IsOkAndHolds(-std::numeric_limits<float>::infinity()));
}

TEST(JsonLaxConversionTest, String) {
  std::vector<std::pair<JSONValue, std::optional<std::string>>>
      inputs_and_expected_outputs;
  // Bools
  inputs_and_expected_outputs.emplace_back(JSONValue(true), "true");
  inputs_and_expected_outputs.emplace_back(JSONValue(false), "false");
  // Strings
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"foo"}),
                                           "foo");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"10"}), "10");
  inputs_and_expected_outputs.emplace_back(JSONValue(std::string{"null"}),
                                           "null");
  // Numbers. Note that -inf, inf, and NaN are not valid JSON numeric values.
  inputs_and_expected_outputs.emplace_back(JSONValue(int64_t{-10}), "-10");
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{1}), "1");
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::min()}),
      absl::StrCat(std::numeric_limits<std::int64_t>::min()));
  inputs_and_expected_outputs.emplace_back(
      JSONValue(int64_t{std::numeric_limits<int64_t>::max()}),
      absl::StrCat(std::numeric_limits<int64_t>::max()));
  inputs_and_expected_outputs.emplace_back(JSONValue(uint64_t{10}), "10");
  inputs_and_expected_outputs.emplace_back(
      JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}),
      absl::StrCat(std::numeric_limits<std::uint64_t>::max()));
  inputs_and_expected_outputs.emplace_back(JSONValue(double{1.1}), "1.1");
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::min()}),
      "2.2250738585072014e-308");
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::lowest()}),
      "-1.7976931348623157e+308");
  inputs_and_expected_outputs.emplace_back(
      JSONValue(double{std::numeric_limits<double>::max()}),
      "1.7976931348623157e+308");
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("1e100").value(), "1e+100");
  // Object/Array/Null
  inputs_and_expected_outputs.emplace_back(JSONValue(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString(R"({"a": 1})").value(), std::nullopt);
  inputs_and_expected_outputs.emplace_back(
      JSONValue::ParseJSONString("[1]").value(), std::nullopt);
  for (const auto& [input, expected_output] : inputs_and_expected_outputs) {
    SCOPED_TRACE(
        absl::Substitute("LAX_STRING('$0')", input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToString(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToBoolArray) {
  std::vector<
      std::pair<std::string, std::optional<std::vector<std::optional<bool>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<bool>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<bool>>{std::nullopt});
  cases.emplace_back(R"([false, true])",
                     std::vector<std::optional<bool>>{false, true});
  cases.emplace_back(
      R"(["TRue", "FaLse", "foo", "null", ""])",
      std::vector<std::optional<bool>>{true, false, std::nullopt, std::nullopt,
                                       std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<bool>>{
          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt});
  cases.emplace_back(
      R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
      std::vector<std::optional<bool>>{false, false, false, true, true, true,
                                       true, true, true});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<bool>>{true, true, true, true});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(R"([4294967295, 18446744073709551615, 1e100])",
                     std::vector<std::optional<bool>>{true, true, true});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<bool>>{true, true, true, true, true, true});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<bool>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<bool>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_BOOL_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToBoolArray(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToInt64Array) {
  std::vector<std::pair<std::string,
                        std::optional<std::vector<std::optional<int64_t>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<int64_t>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<int64_t>>{std::nullopt});
  cases.emplace_back(R"([false, true])",
                     std::vector<std::optional<int64_t>>{0, 1});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<int64_t>>{
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<int64_t>>{0, 0, 0, 10, -10, 1, -1, 2, 110});
  cases.emplace_back(
      R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
      std::vector<std::optional<int64_t>>{0, 0, 0, 10, -10, 1, -1, 2, 110});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<int64_t>>{std::numeric_limits<int32_t>::min(),
                                          std::numeric_limits<int32_t>::max(),
                                          std::numeric_limits<int64_t>::min(),
                                          std::numeric_limits<int64_t>::max()});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(
      R"([4294967295, 18446744073709551615, 1e100])",
      std::vector<std::optional<int64_t>>{std::numeric_limits<uint32_t>::max(),
                                          std::nullopt, std::nullopt});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<int64_t>>{std::nullopt, 0, std::nullopt,
                                          std::nullopt, 0, std::nullopt});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<int64_t>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<int64_t>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_INT64_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToInt64Array(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToInt32Array) {
  std::vector<std::pair<std::string,
                        std::optional<std::vector<std::optional<int32_t>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<int32_t>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<int32_t>>{std::nullopt});
  cases.emplace_back(R"([false, true])",
                     std::vector<std::optional<int32_t>>{0, 1});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<int32_t>>{
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<int32_t>>{0, 0, 0, 10, -10, 1, -1, 2, 110});
  cases.emplace_back(
      R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
      std::vector<std::optional<int32_t>>{0, 0, 0, 10, -10, 1, -1, 2, 110});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<int32_t>>{std::numeric_limits<int32_t>::min(),
                                          std::numeric_limits<int32_t>::max(),
                                          std::nullopt, std::nullopt});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(R"([4294967295, 18446744073709551615, 1e100])",
                     std::vector<std::optional<int32_t>>{
                         std::nullopt, std::nullopt, std::nullopt});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<int32_t>>{std::nullopt, 0, std::nullopt,
                                          std::nullopt, 0, std::nullopt});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<int32_t>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<int32_t>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_INT32_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToInt32Array(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToUint64Array) {
  std::vector<std::pair<std::string,
                        std::optional<std::vector<std::optional<uint64_t>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<uint64_t>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<uint64_t>>{std::nullopt});
  cases.emplace_back(R"([false, true])",
                     std::vector<std::optional<uint64_t>>{0, 1});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<uint64_t>>{
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<uint64_t>>{0, 0, 0, 10, std::nullopt, 1,
                                           std::nullopt, 2, 110});
  cases.emplace_back(R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
                     std::vector<std::optional<uint64_t>>{
                         0, 0, 0, 10, std::nullopt, 1, std::nullopt, 2, 110});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<uint64_t>>{
          std::nullopt, std::numeric_limits<int32_t>::max(), std::nullopt,
          std::numeric_limits<int64_t>::max()});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(R"([4294967295, 18446744073709551615, 1e100])",
                     std::vector<std::optional<uint64_t>>{
                         std::numeric_limits<uint32_t>::max(),
                         std::numeric_limits<uint64_t>::max(), std::nullopt});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<uint64_t>>{std::nullopt, 0, std::nullopt,
                                           std::nullopt, 0, std::nullopt});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<uint64_t>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<uint64_t>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_UINT64_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToUint64Array(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToUint32Array) {
  std::vector<std::pair<std::string,
                        std::optional<std::vector<std::optional<uint32_t>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<uint32_t>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<uint32_t>>{std::nullopt});
  cases.emplace_back(R"([false, true])",
                     std::vector<std::optional<uint32_t>>{0, 1});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<uint32_t>>{
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<uint32_t>>{0, 0, 0, 10, std::nullopt, 1,
                                           std::nullopt, 2, 110});
  cases.emplace_back(R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
                     std::vector<std::optional<uint32_t>>{
                         0, 0, 0, 10, std::nullopt, 1, std::nullopt, 2, 110});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<uint32_t>>{std::nullopt,
                                           std::numeric_limits<int32_t>::max(),
                                           std::nullopt, std::nullopt});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(
      R"([4294967295, 18446744073709551615, 1e100])",
      std::vector<std::optional<uint32_t>>{std::numeric_limits<uint32_t>::max(),
                                           std::nullopt, std::nullopt});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<uint32_t>>{std::nullopt, 0, std::nullopt,
                                           std::nullopt, 0, std::nullopt});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<uint32_t>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<uint32_t>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_UINT32_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToUint32Array(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToFloat64Array) {
  std::vector<
      std::pair<std::string, std::optional<std::vector<std::optional<double>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<double>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<double>>{std::nullopt});
  cases.emplace_back(R"([false, true])", std::vector<std::optional<double>>{
                                             std::nullopt, std::nullopt});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<double>>{
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<double>>{0, 0, 0, 10, -10, 1.1, -1.1, 1.5,
                                         110});
  cases.emplace_back(R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
                     std::vector<std::optional<double>>{0, 0, 0, 10, -10, 1.1,
                                                        -1.1, 1.5, 110});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<double>>{std::numeric_limits<int32_t>::min(),
                                         std::numeric_limits<int32_t>::max(),
                                         std::numeric_limits<int64_t>::min(),
                                         std::numeric_limits<int64_t>::max()});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(R"([4294967295, 18446744073709551615, 1e100])",
                     std::vector<std::optional<double>>{
                         std::numeric_limits<uint32_t>::max(),
                         std::numeric_limits<uint64_t>::max(), 1e100});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<double>>{-3.40282e+38, 1.17549e-38, 3.40282e+38,
                                         -1.79769e+308, 2.22507e-308,
                                         1.79769e+308});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<double>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<double>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_FLOAT64_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToFloat64Array(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToFloat32Array) {
  std::vector<
      std::pair<std::string, std::optional<std::vector<std::optional<float>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<float>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<float>>{std::nullopt});
  cases.emplace_back(R"([false, true])", std::vector<std::optional<float>>{
                                             std::nullopt, std::nullopt});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<float>>{
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<float>>{0, 0, 0, 10, -10, 1.1, -1.1, 1.5, 110});
  cases.emplace_back(
      R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
      std::vector<std::optional<float>>{0, 0, 0, 10, -10, 1.1, -1.1, 1.5, 110});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<float>>{std::numeric_limits<int32_t>::min(),
                                        std::numeric_limits<int32_t>::max(),
                                        std::numeric_limits<int64_t>::min(),
                                        std::numeric_limits<int64_t>::max()});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(R"([4294967295, 18446744073709551615, 1e100])",
                     std::vector<std::optional<float>>{
                         std::numeric_limits<uint32_t>::max(),
                         std::numeric_limits<uint64_t>::max(), std::nullopt});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<float>>{-3.40282e+38, 1.17549e-38, 3.40282e+38,
                                        std::nullopt, 2.22507e-308,
                                        std::nullopt});

  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<float>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<float>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_FLOAT32_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToFloat32Array(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonConversionTest, LaxConvertJsonToStringArray) {
  std::vector<std::pair<std::string,
                        std::optional<std::vector<std::optional<std::string>>>>>
      cases;
  cases.emplace_back(R"([])", std::vector<std::optional<std::string>>{});
  cases.emplace_back(R"([null])",
                     std::vector<std::optional<std::string>>{std::nullopt});
  cases.emplace_back(R"([false, true])",
                     std::vector<std::optional<std::string>>{"false", "true"});
  cases.emplace_back(R"(["TRue", "FaLse", "foo", "null", ""])",
                     std::vector<std::optional<std::string>>{
                         "TRue", "FaLse", "foo", "null", ""});
  cases.emplace_back(
      R"(["0", "0.0", "-0.0", "10", "-10", "1.1", "-1.1", "+1.5", "1.1e2"])",
      std::vector<std::optional<std::string>>{"0", "0.0", "-0.0", "10", "-10",
                                              "1.1", "-1.1", "+1.5", "1.1e2"});
  cases.emplace_back(
      R"([0, 0.0, -0.0, 10, -10, 1.1, -1.1, 1.5, 1.1e2])",
      std::vector<std::optional<std::string>>{"0", "0", "0", "10", "-10", "1.1",
                                              "-1.1", "1.5", "110"});
  // int32:min, int32:max, int64:min, int64:max
  cases.emplace_back(
      R"([-2147483648, 2147483647, -9223372036854775808, 9223372036854775807])",
      std::vector<std::optional<std::string>>{"-2147483648", "2147483647",
                                              "-9223372036854775808",
                                              "9223372036854775807"});
  // uint32:max, uint64:max, extremely large number
  cases.emplace_back(R"([4294967295, 18446744073709551615])",
                     std::vector<std::optional<std::string>>{
                         "4294967295", "18446744073709551615"});
  // float:lowest, float:min, float:max, double:lowest, double:min, double:max
  cases.emplace_back(
      R"([-3.40282e+38, 1.17549e-38, 3.40282e+38, -1.79769e+308, 2.22507e-308, 1.79769e+308])",
      std::vector<std::optional<std::string>>{"-3.40282e+38", "1.17549e-38",
                                              "3.40282e+38", "-1.79769e+308",
                                              "2.22507e-308", "1.79769e+308"});
  cases.emplace_back(R"([[false]])",
                     std::vector<std::optional<std::string>>{std::nullopt});
  cases.emplace_back(R"([{"a": false}])",
                     std::vector<std::optional<std::string>>{std::nullopt});
  cases.emplace_back(R"(null)", std::nullopt);
  cases.emplace_back(R"(false)", std::nullopt);
  cases.emplace_back(R"(true)", std::nullopt);
  cases.emplace_back(R"(1)", std::nullopt);
  cases.emplace_back(R"(1.0)", std::nullopt);
  cases.emplace_back(R"("foo")", std::nullopt);
  cases.emplace_back(R"({"a": false})", std::nullopt);

  for (const auto& [json_text, expected_output] : cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue input,
                         JSONValue::ParseJSONString(json_text));
    SCOPED_TRACE(absl::Substitute("LAX_STRING_ARRAY('$0')",
                                  input.GetConstRef().ToString()));
    EXPECT_THAT(LaxConvertJsonToStringArray(input.GetConstRef()),
                IsOkAndHolds(expected_output));
  }
}

TEST(JsonArrayTest, Compliance) {
  const std::vector<FunctionTestCall> tests = GetFunctionTestsJsonArray();

  for (const FunctionTestCall& test : tests) {
    SCOPED_TRACE(absl::Substitute(
        "JSON_ARRAY($0)",
        absl::StrJoin(test.params.params(), ",",
                      [](std::string* out, const Value& value) {
                        absl::StrAppend(out, value.ShortDebugString());
                      })));
    googlesql::LanguageOptions language_options;
    for (const auto& feature : test.params.required_features()) {
      language_options.EnableLanguageFeature(feature);
    }

    absl::StatusOr<JSONValue> output =
        JsonArray(test.params.params(), language_options,
                  /*canonicalize_zero=*/true);

    const Value expected_value = test.params.result();
    const absl::Status expected_status = test.params.status();

    if (expected_status.ok()) {
      GOOGLESQL_ASSERT_OK(output);
      EXPECT_EQ(expected_value, values::Json(*std::move(output)));
    } else {
      EXPECT_EQ(output.status().code(), expected_status.code());
    }
  }
}

std::string JsonObjectDebugString(const FunctionTestCall& test) {
  return absl::Substitute(
      "JSON_OBJECT($0)",
      absl::StrJoin(test.params.params(), ",",
                    [](std::string* out, const Value& value) {
                      absl::StrAppend(out, value.ShortDebugString());
                    }));
}

googlesql::LanguageOptions GetLanguageOptionsFromTest(
    const FunctionTestCall& test) {
  googlesql::LanguageOptions language_options;
  for (const auto& feature : test.params.required_features()) {
    language_options.EnableLanguageFeature(feature);
  }
  return language_options;
}

TEST(JsonObjectTest, VariadicArgs) {
  // NULL keys are not supported by the library function. They will be handled
  // by the SQL function implementation.
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsJsonObject(/*include_null_key_tests=*/false);

  for (const FunctionTestCall& test : tests) {
    SCOPED_TRACE(JsonObjectDebugString(test));

    JsonObjectBuilder builder(GetLanguageOptionsFromTest(test),
                              /*canonicalize_zero=*/true);

    std::vector<absl::string_view> keys;
    std::vector<const Value*> values;

    // Signature: JSON_OBJECT(STRING key, ANY value, ...)
    keys.reserve((test.params.num_params() + 1) / 2);
    values.reserve(test.params.num_params() / 2);
    for (int i = 0; i < test.params.num_params(); i += 2) {
      const Value& key = test.params.param(i);
      ASSERT_TRUE(key.type()->IsString());
      ASSERT_FALSE(key.is_null());
      keys.push_back(key.string_value());
    }
    for (int i = 1; i < test.params.num_params(); i += 2) {
      values.push_back(&test.params.param(i));
    }
    absl::StatusOr<JSONValue> output =
        JsonObject(keys, absl::MakeSpan(values), builder);

    const Value expected_value = test.params.result();
    const absl::Status expected_status = test.params.status();

    if (expected_status.ok()) {
      GOOGLESQL_ASSERT_OK(output);
      EXPECT_EQ(expected_value, values::Json(*std::move(output)));
    } else {
      EXPECT_EQ(output.status().code(), expected_status.code());
    }
  }
}

TEST(JsonObjectTest, TwoArrayArgs) {
  // NULL keys are not supported by the library function. They will be handled
  // by the SQL function implementation.
  const std::vector<FunctionTestCall> tests =
      GetFunctionTestsJsonObjectArrays(/*include_null_key_tests=*/false);

  for (const FunctionTestCall& test : tests) {
    SCOPED_TRACE(JsonObjectDebugString(test));

    JsonObjectBuilder builder(GetLanguageOptionsFromTest(test),
                              /*canonicalize_zero=*/true);

    // Signature: JSON_OBJECT(ARRAY<STRING> keys, ARRAY<ANY> values)
    ASSERT_EQ(test.params.num_params(), 2);
    ASSERT_TRUE(test.params.param(0).type()->IsArray());
    ASSERT_TRUE(test.params.param(1).type()->IsArray());

    std::vector<absl::string_view> keys;
    std::vector<const Value*> values;
    keys.reserve(test.params.param(0).num_elements());
    for (const Value& key : test.params.param(0).elements()) {
      ASSERT_TRUE(key.type()->IsString());
      ASSERT_FALSE(key.is_null());
      keys.push_back(key.string_value());
    }

    for (const Value& value : test.params.param(1).elements()) {
      values.push_back(&value);
    }

    absl::StatusOr<JSONValue> output =
        JsonObject(keys, absl::MakeSpan(values), builder);

    const Value expected_value = test.params.result();
    const absl::Status expected_status = test.params.status();

    if (expected_status.ok()) {
      GOOGLESQL_ASSERT_OK(output);
      EXPECT_EQ(expected_value, values::Json(*std::move(output)));
    } else {
      EXPECT_EQ(output.status().code(), expected_status.code());
    }
  }
}

TEST(JsonObjectTest, ReuseBuilder) {
  JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);

  {
    std::vector<absl::string_view> keys = {"a", "b"};
    std::vector<Value> values_storage = {values::Int64(10),
                                         values::String("foo")};
    std::vector<const Value*> values;
    for (const auto& value : values_storage) {
      values.push_back(&value);
    }
    absl::StatusOr<JSONValue> output =
        JsonObject(keys, absl::MakeSpan(values), builder);
    GOOGLESQL_ASSERT_OK(output);
    EXPECT_THAT(output->GetConstRef(),
                JsonEq(JSONValue::ParseJSONString(R"({"a":10,"b":"foo"})")
                           ->GetConstRef()));
  }
  {
    std::vector<absl::string_view> keys = {"a", "c"};
    std::vector<Value> values_storage = {values::Int64(15), values::Bool(true)};
    std::vector<const Value*> values;
    for (const auto& value : values_storage) {
      values.push_back(&value);
    }
    absl::StatusOr<JSONValue> output =
        JsonObject(keys, absl::MakeSpan(values), builder);
    GOOGLESQL_ASSERT_OK(output);
    EXPECT_THAT(
        output->GetConstRef(),
        JsonEq(
            JSONValue::ParseJSONString(R"({"a":15,"c":true})")->GetConstRef()));
  }
}

TEST(JsonObjectBuilderTest, NoCallsToAdd) {
  JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);
  EXPECT_THAT(builder.Build().GetConstRef(),
              JsonEq(JSONValue::ParseJSONString("{}")->GetConstRef()));
}

TEST(JsonObjectBuilderTest, AddNoDuplicate) {
  JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);
  {
    auto inserted = builder.Add("field", values::Int64(10));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  {
    auto inserted = builder.Add("field2", values::String("foo"));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  EXPECT_THAT(
      builder.Build().GetConstRef(),
      JsonEq(JSONValue::ParseJSONString(R"({"field":10,"field2":"foo"})")
                 ->GetConstRef()));
}

TEST(JsonObjectBuilderTest, AddDuplicate) {
  JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);
  {
    auto inserted = builder.Add("field", values::Int64(10));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  {
    auto inserted = builder.Add("field", values::String("foo"));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_FALSE(*inserted);
  }
  {
    auto inserted = builder.Add("field", values::Int64(20));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_FALSE(*inserted);
  }
  {
    auto inserted = builder.Add("field2", values::Int64(20));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  EXPECT_THAT(builder.Build().GetConstRef(),
              JsonEq(JSONValue::ParseJSONString(R"({"field":10,"field2":20})")
                         ->GetConstRef()));
}

TEST(JsonObjectBuilderTest, ResetBuilder) {
  JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);
  {
    auto inserted = builder.Add("field", values::Int64(10));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  builder.Reset();

  {
    // Same field to test that 'keys_set_' has been reset.
    auto inserted = builder.Add("field", values::String("foo"));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  {
    auto inserted = builder.Add("field2", values::Int64(20));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  EXPECT_THAT(
      builder.Build().GetConstRef(),
      JsonEq(JSONValue::ParseJSONString(R"({"field":"foo","field2":20})")
                 ->GetConstRef()));
}

TEST(JsonObjectBuilderTest, ReuseBuilder) {
  JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);
  {
    auto inserted = builder.Add("field", values::Int64(10));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  EXPECT_THAT(
      builder.Build().GetConstRef(),
      JsonEq(JSONValue::ParseJSONString(R"({"field":10})")->GetConstRef()));

  {
    // Same field to test that 'keys_set_' has been reset.
    auto inserted = builder.Add("field", values::String("foo"));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);
  }
  EXPECT_THAT(
      builder.Build().GetConstRef(),
      JsonEq(JSONValue::ParseJSONString(R"({"field":"foo"})")->GetConstRef()));
}

TEST(JsonObjectBuilderTest, CanonicalizeZero) {
  {
    JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/true);
    auto inserted = builder.Add("field", values::Double(-0.0));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);

    EXPECT_THAT(
        builder.Build().GetConstRef(),
        JsonEq(JSONValue::ParseJSONString(R"({"field":0.0})")->GetConstRef()));
  }
  {
    JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/false);
    auto inserted = builder.Add("field", values::Double(-0.0));
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);

    EXPECT_THAT(
        builder.Build().GetConstRef(),
        JsonEq(JSONValue::ParseJSONString(R"({"field":-0.0})")->GetConstRef()));
  }
}

TEST(JsonObjectBuilderTest, StrictNumberParsing) {
  Value value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());

  {
    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);
    JsonObjectBuilder builder(options, /*canonicalize_zero=*/true);
    auto inserted = builder.Add("field", value);
    EXPECT_THAT(inserted, StatusIs(absl::StatusCode::kOutOfRange));
    builder.Reset();
  }
  {
    JsonObjectBuilder builder(LanguageOptions(), /*canonicalize_zero=*/false);
    auto inserted = builder.Add("field", value);
    GOOGLESQL_ASSERT_OK(inserted);
    EXPECT_TRUE(*inserted);

    EXPECT_THAT(
        builder.Build().GetConstRef(),
        JsonEq(JSONValue::ParseJSONString(R"({"field":1.1111111111111112})")
                   ->GetConstRef()));
  }
}

std::unique_ptr<StrictJSONPathIterator> ParseJSONPath(absl::string_view path,
                                                      bool enable_lax = false) {
  return StrictJSONPathIterator::Create(path, enable_lax).value();
}

TEST(JsonRemoveTest, InvalidJSONPath) {
  JSONValue value;
  JSONValueRef ref = value.GetRef();
  auto path_iter = ParseJSONPath("$");
  EXPECT_THAT(JsonRemove(ref, *path_iter),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("The JSONPath cannot be '$'")));
}

TEST(JsonRemoveTest, NoOp) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true, "b": [1.1, false, []]}, 10])";

  std::vector<absl::string_view> paths = {
      // $ is not an object
      "$.a",
      // $[0] is not an array
      "$[0][0]",
      // Array index out of bound
      "$[10]",
      // Key doesn't exist
      "$[2].c",
      // $[2] doesn't have the 'c' 'key'
      "$[2].c[1]",
      // Array index out of bound in array at $[2].b
      "$[2].b[4].a",
  };

  for (auto path : paths) {
    SCOPED_TRACE(
        absl::Substitute("JsonRemove('$0', '$1')", kInitialValue, path));
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    auto path_iter = ParseJSONPath(path);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool success, JsonRemove(ref, *path_iter));
    EXPECT_FALSE(success);
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
}

TEST(JsonRemoveTest, ValidRemove) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true, "b": [1.1, false, []]}, 10])";

  // Pair of: JSONPath to remove, expected output.
  std::vector<std::pair<absl::string_view, absl::string_view>>
      paths_and_outputs = {
          {"$[0]", R"([null,{"a":true,"b":[1.1,false,[]]},10])"},
          {"$[1]", R"(["foo",{"a":true,"b":[1.1,false,[]]},10])"},
          {"$[2]", R"(["foo",null,10])"},
          {"$[2].a", R"(["foo",null,{"b":[1.1,false,[]]},10])"},
          {"$[2].b", R"(["foo",null,{"a":true},10])"},
          {"$[2].b[2]", R"(["foo",null,{"a":true,"b":[1.1,false]},10])"},
      };

  for (auto [path, output] : paths_and_outputs) {
    SCOPED_TRACE(
        absl::Substitute("JsonRemove('$0', '$1')", kInitialValue, path));
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    auto path_iter = ParseJSONPath(path);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool success, JsonRemove(ref, *path_iter));
    EXPECT_TRUE(success);
    EXPECT_THAT(ref, JsonEq(JSONValue::ParseJSONString(output)->GetConstRef()));
  }
}

TEST(JsonRemoveTest, EmptyObjectAndArrayAreNotCleaned) {
  {
    // JsonRemove results in empty object which remains in the JSON value.
    JSONValue value = JSONValue::ParseJSONString(R"([10, {"a": 10}])").value();
    JSONValueRef ref = value.GetRef();
    auto path_iter = ParseJSONPath("$[1].a");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool success, JsonRemove(ref, *path_iter));
    EXPECT_TRUE(success);
    EXPECT_THAT(ref,
                JsonEq(JSONValue::ParseJSONString("[10, {}]")->GetConstRef()));
  }
  {
    // JsonRemove results in empty array which remains in the JSON value.
    JSONValue value = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    JSONValueRef ref = value.GetRef();
    auto path_iter = ParseJSONPath("$.a[0]");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(bool success, JsonRemove(ref, *path_iter));
    EXPECT_TRUE(success);
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(R"({"a": []})")->GetConstRef()));
  }
}

TEST(JsonInsertArrayTest, NullArrayValues) {
  constexpr absl::string_view kInitialValue = R"(["foo", null])";

  auto test_fn = [&kInitialValue](bool insert_each_element,
                                  absl::string_view expected_output) {
    JSONValue json = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = json.GetRef();
    auto path_iter = ParseJSONPath("$[0]");
    GOOGLESQL_ASSERT_OK(JsonInsertArrayElement(
        ref, *path_iter, Value::Null(types::StringArrayType()),
        LanguageOptions(),
        /*canonicalize_zero=*/true, insert_each_element));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  test_fn(true, kInitialValue);
  test_fn(false, R"([null, "foo", null])");
}

TEST(JsonInsertArrayTest, NoOp) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true, "b": [1.1, false, []]}, 10])";

  auto test_fn = [&kInitialValue](absl::string_view path) {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    auto path_iter = ParseJSONPath(path);
    GOOGLESQL_EXPECT_OK(JsonInsertArrayElement(ref, *path_iter, Value::Int64(10),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  };

  // Last token is not an array index
  test_fn("$.a");
  // Path doesn't exist
  test_fn("$[1][1].a");
  // Path doesn't exist
  test_fn("$.a[0]");
  // Path doesn't exist
  test_fn("$[2].c[0]");
  // Path doesn't point to an array ($[2] is not an array)
  test_fn("$[2][0]");
  // Path doesn't point to an array ($[2] is not an array)
  test_fn("$[2][0][0]");
  // With strict JSONPath, .0 is a member access and cannot be an array
  // index.
  test_fn("$.a.0");
  // Doesn't recursively create array on null.
  test_fn("$[1][2][0]");
}

TEST(JsonInsertArrayTest, ValidInserts) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true, "b": [1.1, false, []]}, 10])";

  auto test_fn = [&kInitialValue](absl::string_view path, const Value& value,
                                  bool insert_each_element,
                                  absl::string_view expected_output) {
    JSONValue json = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = json.GetRef();
    auto path_iter = ParseJSONPath(path);
    GOOGLESQL_ASSERT_OK(JsonInsertArrayElement(ref, *path_iter, value, LanguageOptions(),
                                     /*canonicalize_zero=*/true,
                                     insert_each_element));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  // Inserts into top level array.
  test_fn("$[0]", Value::Int64(1), false,
          R"([1,"foo",null,{"a":true,"b":[1.1,false,[]]},10])");
  // Inserts past the array size.
  test_fn("$[4]", Value::Int64(1), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,[]]},10,1])");
  // Inserts past the array size.
  test_fn("$[7]", Value::Int64(1), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,[]]},10,null,null,null,1])");
  // Inserts into nested array.
  test_fn("$[2].b[1]", Value::String("bar"), false,
          R"(["foo",null,{"a":true,"b":[1.1,"bar",false,[]]},10])");
  // Path points to an array. Does not insert into the array. Inserts
  // before the array.
  test_fn("$[2].b[2]", Value::String("bar"), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,"bar",[]]},10])");
  // Inserts into nested array.
  test_fn("$[2].b[2][0]", Value::String("bar"), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,["bar"]]},10])");
  // Inserts into nested array, past the array size.
  test_fn("$[2].b[2][2]", Value::String("bar"), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,[null,null,"bar"]]},10])");
  // Inserts an array as a single element.
  test_fn("$[2].b[1]", values::StringArray({"a", "b"}), false,
          R"(["foo",null,{"a":true,"b":[1.1,["a","b"],false,[]]},10])");
  // Inserts an array as multiple elements.
  test_fn("$[2].b[1]", values::StringArray({"a", "b"}), true,
          R"(["foo",null,{"a":true,"b":[1.1,"a","b",false,[]]},10])");
  // Inserts an array as multiple elements past the array size.
  test_fn("$[2].b[5]", values::StringArray({"a", "b"}), true,
          R"(["foo",null,{"a":true,"b":[1.1,false,[],null,null,"a","b"]},10])");
  // Inserts into top level array.
  test_fn("$[1]", Value::String("a"), true,
          R"(["foo","a",null,{"a":true,"b":[1.1,false,[]]},10])");
  // Inserts into null.
  test_fn("$[1][0]", Value::String("a"), true,
          R"(["foo",["a"],{"a":true,"b":[1.1,false,[]]},10])");
  // Inserts into null and expands array.
  test_fn("$[1][2]", Value::String("a"), true,
          R"(["foo",[null,null,"a"],{"a":true,"b":[1.1,false,[]]},10])");
  // Inserts 0 element into null. Creates an array.
  test_fn("$[1][2]", values::StringArray(std::vector<std::string>()), true,
          R"(["foo",[null,null,null],{"a":true,"b":[1.1,false,[]]},10])");
  // Inserts multiple elements into null.
  test_fn("$[1][0]", values::Int64Array({1, 5}), true,
          R"(["foo",[1,5],{"a":true,"b":[1.1,false,[]]},10])");
  // Inserts multiple elements into null.
  test_fn("$[1][2]", values::Int64Array({1, 5}), true,
          R"(["foo",[null,null,1,5],{"a":true,"b":[1.1,false,[]]},10])");
}

TEST(JsonInsertArrayTest, FailedConversionComesFirst) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";

  Value big_value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());

  {
    // An error is returned even if the path doesn't exist.
    auto path_iter = ParseJSONPath("$.a");
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonInsertArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
  {
    // Insertion in null would have created an array but failed conversion
    // comes first.
    auto path_iter = ParseJSONPath("$[1][1]");
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonInsertArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
}

TEST(JsonInsertArrayTest, CanonicalizeZero) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";
  auto path_iter = ParseJSONPath("$[1]");
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonInsertArrayElement(ref, *path_iter, Value::Double(-0.0),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(R"(["foo",0.0,null,{"a":true},10])")
                   ->GetConstRef()));
  }

  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonInsertArrayElement(ref, *path_iter, Value::Double(-0.0),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/false));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(R"(["foo",-0.0,null,{"a":true},10])")
                   ->GetConstRef()));
  }
}

TEST(JsonInsertArrayTest, StrictNumberParsing) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";

  Value big_value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());

  auto path_iter = ParseJSONPath("$[1]");
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonInsertArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
  }

  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    GOOGLESQL_ASSERT_OK(JsonInsertArrayElement(ref, *path_iter, big_value,
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/true));
    EXPECT_THAT(ref,
                JsonEq(JSONValue::ParseJSONString(
                           R"(["foo",1.111111111111111111,null,{"a":true},10])")
                           ->GetConstRef()));
  }
}

TEST(JsonInsertArrayTest, MaxArraySizeExceeded) {
  constexpr absl::string_view kInitialValue = R"({"a": null, "b": [10]})";

  {
    auto path_iter =
        ParseJSONPath(absl::Substitute("$$.a[$0]", kJSONMaxArraySize));
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    EXPECT_THAT(JsonInsertArrayElement(ref, *path_iter, Value::String("foo"),
                                       LanguageOptions(),
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }

  {
    auto path_iter =
        ParseJSONPath(absl::Substitute("$$.b[$0]", kJSONMaxArraySize));
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    EXPECT_THAT(JsonInsertArrayElement(ref, *path_iter, Value::String("foo"),
                                       LanguageOptions(),
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
}

TEST(JsonAppendArrayTest, NullArrayValue) {
  constexpr absl::string_view kInitialValue = R"(["foo", null])";

  auto test_fn = [&kInitialValue](bool insert_each_element,
                                  absl::string_view expected_output) {
    JSONValue json = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = json.GetRef();
    auto path_iter = ParseJSONPath("$");
    GOOGLESQL_ASSERT_OK(JsonAppendArrayElement(
        ref, *path_iter, Value::Null(types::DoubleArrayType()),
        LanguageOptions(),
        /*canonicalize_zero=*/true, insert_each_element));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  test_fn(true, kInitialValue);
  test_fn(false, R"(["foo", null, null])");
}

TEST(JsonAppendArrayTest, NoOp) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true, "b": [1.1, false, []]}, 10])";

  auto test_fn = [&kInitialValue](absl::string_view path) {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    auto path_iter = ParseJSONPath(path);
    GOOGLESQL_EXPECT_OK(JsonAppendArrayElement(ref, *path_iter, Value::Int64(10),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  };

  // Path doesn't exist
  test_fn("$.a");
  // Path doesn't exist
  test_fn("$[1][1].a");
  // Path doesn't exist
  test_fn("$[2].c");
  // Path doesn't exist
  test_fn("$[4]");
  // Path doesn't exist
  test_fn("$[2].b[2][0]");
  // Path doesn't point to an array
  test_fn("$[2]");
  // With strict JSONPath); .2 is a member access and cannot be an array
  // index.
  test_fn("$[2].b.2");
  // Doesn't recursively create array on null.
  test_fn("$[1][1]");
}

TEST(JsonAppendArrayTest, ValidInserts) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true, "b": [1.1, false, []]}, 10])";

  auto test_fn = [&kInitialValue](absl::string_view path, const Value& value,
                                  bool insert_each_element,
                                  absl::string_view expected_output) {
    JSONValue json = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = json.GetRef();
    auto path_iter = ParseJSONPath(path);
    GOOGLESQL_ASSERT_OK(JsonAppendArrayElement(ref, *path_iter, value, LanguageOptions(),
                                     /*canonicalize_zero=*/true,
                                     insert_each_element));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  // Appends into top level array.
  test_fn("$", Value::Int64(1), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,[]]},10,1])");
  // Appends into nested array.
  test_fn("$[2].b", Value::String("bar"), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,[],"bar"]},10])");
  // Appends into nested array.
  test_fn("$[2].b[2]", Value::String("bar"), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,["bar"]]},10])");
  // Appends an array as a single element.
  test_fn("$[2].b", values::StringArray({"a", "b"}), false,
          R"(["foo",null,{"a":true,"b":[1.1,false,[],["a","b"]]},10])");
  // Appends an array as multiple elements.
  test_fn("$[2].b", values::StringArray({"a", "b"}), true,
          R"(["foo",null,{"a":true,"b":[1.1,false,[],"a","b"]},10])");
  // Appends into null.
  test_fn("$[1]", Value::Int64(1), false,
          R"(["foo",[1],{"a":true,"b":[1.1,false,[]]},10])");
  // Appends 0 element into null. Creates an array.
  test_fn("$[1]", values::Int64Array({}), true,
          R"(["foo",[],{"a":true,"b":[1.1,false,[]]},10])");
  // Appends multiple elements into null.
  test_fn("$[1]", values::Int64Array({1, 2}), true,
          R"(["foo",[1,2],{"a":true,"b":[1.1,false,[]]},10])");
}

TEST(JsonAppendArrayTest, CanonicalizeZero) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";
  auto path_iter = ParseJSONPath("$");
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonAppendArrayElement(ref, *path_iter, Value::Double(-0.0),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(R"(["foo",null,{"a":true},10,0.0])")
                   ->GetConstRef()));
  }

  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonAppendArrayElement(ref, *path_iter, Value::Double(-0.0),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/false));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(R"(["foo",null,{"a":true},10,-0.0])")
                   ->GetConstRef()));
  }
}

TEST(JsonAppendArrayTest, StrictNumberParsing) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";
  auto path_iter = ParseJSONPath("$");

  Value big_value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());

  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonAppendArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
  }

  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    GOOGLESQL_ASSERT_OK(JsonAppendArrayElement(ref, *path_iter, big_value, options,
                                     /*canonicalize_zero=*/true));
    EXPECT_THAT(ref,
                JsonEq(JSONValue::ParseJSONString(
                           R"(["foo",null,{"a":true},10,1.111111111111111111])")
                           ->GetConstRef()));
  }
}

TEST(JsonAppendArrayTest, FailedConversionComesFirst) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";

  Value big_value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());

  {
    // An error is returned even if the path doesn't exist.
    auto path_iter = ParseJSONPath("$.a[1]");
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonAppendArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
  {
    // Insertion in null would have created an array but failed conversion
    // comes first.
    auto path_iter = ParseJSONPath("$[1]");
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonAppendArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
}

TEST(JsonAppendArrayTest, MaxArraySizeExceeded) {
  constexpr absl::string_view kInitialValue = R"({"a": null, "b": [10]})";

  JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
  JSONValueRef ref = value.GetRef();
  ref.GetMember("b").GetArrayElement(kJSONMaxArraySize - 1);
  auto path_iter = ParseJSONPath("$.b");

  EXPECT_THAT(JsonAppendArrayElement(ref, *path_iter, Value::String("foo"),
                                     LanguageOptions(),
                                     /*canonicalize_zero=*/true),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Exceeded maximum array size")));
  EXPECT_EQ(ref.GetMember("b").GetArraySize(), kJSONMaxArraySize);
}

TEST(JsonSetTest, NoOpTopLevelObject) {
  constexpr absl::string_view kInitialObjectValue =
      R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "f":[2,[],{},[3,4]]})";

  auto test_fn = [&kInitialObjectValue](absl::string_view path) {
    JSONValue value = JSONValue::ParseJSONString(kInitialObjectValue).value();
    JSONValueRef ref = value.GetRef();
    for (bool create_if_missing : {true, false}) {
      std::unique_ptr<StrictJSONPathIterator> path_iterator =
          ParseJSONPath(path);
      GOOGLESQL_EXPECT_OK(JsonSet(ref, *path_iterator, Value::Int64(10),
                        /*create_if_missing=*/create_if_missing,
                        LanguageOptions(),
                        /*canonicalize_zero=*/true));
      EXPECT_THAT(
          ref,
          JsonEq(
              JSONValue::ParseJSONString(kInitialObjectValue)->GetConstRef()));
    }
  };

  // Type mismatch. Path prefix "$" is an object but expected array.
  test_fn("$[0]");
  // Type mismatch. Path prefix "$.b" is a object but expected array.
  test_fn("$.b[0]");
  // Type mismatch. Path prefix "$.c" is an array but expected object.
  test_fn("$.c.d");
  // Type mismatch. Path prefix "$.d.e" is a scalar but expected object.
  test_fn("$.d.e.f");
}

TEST(JsonSetTest, NoOpTopLevelObject2) {
  // Tests when there are suffix tokens left to process and there is a type
  // mismatch.
  constexpr absl::string_view kInitialObjectValue =
      R"({"a":[1],"b":{"c":2},"d":3})";

  auto test_fn = [&kInitialObjectValue](absl::string_view path) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue json = JSONValue::ParseJSONString(kInitialObjectValue).value();
    JSONValueRef ref = json.GetRef();
    for (bool create_if_missing : {true, false}) {
      GOOGLESQL_EXPECT_OK(JsonSet(ref, *path_iterator, Value::Int64(10),
                        create_if_missing, LanguageOptions(),
                        /*canonicalize_zero=*/true));
      EXPECT_THAT(
          ref,
          JsonEq(
              JSONValue::ParseJSONString(kInitialObjectValue)->GetConstRef()));
    }
  };

  test_fn("$.a.b.c");
  test_fn("$.b[1]");
  test_fn("$.d.e");
}

TEST(JsonSetTest, NoOpTopLevelArray) {
  constexpr absl::string_view kInitialArrayValue =
      R"(["foo", null, {"a": true, "b": [1.1, [], [2]], "c": {}},
         {"d": {"e": "f"}}])";

  auto test_fn = [&kInitialArrayValue](absl::string_view path) {
    JSONValue value = JSONValue::ParseJSONString(kInitialArrayValue).value();
    JSONValueRef ref = value.GetRef();
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    for (bool create_if_missing : {true, false}) {
      GOOGLESQL_EXPECT_OK(JsonSet(ref, *path_iterator, Value::Int64(10),
                        create_if_missing, LanguageOptions(),
                        /*canonicalize_zero=*/true));
      EXPECT_THAT(
          ref,
          JsonEq(
              JSONValue::ParseJSONString(kInitialArrayValue)->GetConstRef()));
    }
  };

  // Type mismatch. Path prefix "$" is an array but expected object.
  test_fn("$.a");
  // Type mismatch. Path prefix "$[0]" is a scalar but expected array.
  test_fn("$[0][0]");
  // Type mismatch. Path prefix "$[2].a" is a scalar but expected object.
  test_fn("$[2].a.b");
  // Type mismatch. Path prefix "$[2].b" is an array but expected object.
  test_fn("$[2].b.a");
}

TEST(JsonSetTest, SingleJsonScalarCases) {
  // If output is std::nullopt there should be no change in input. Set
  // operation is a no-op.

  const Value value = Value::Int64(999);
  auto test_fn = [&](absl::string_view path,
                     std::optional<absl::string_view> expected_output) {
    // Test different scalar JSON representations as inputs.
    for (absl::string_view json_string : {"1", "1.1", "false", R"("foo")"}) {
      SCOPED_TRACE(
          absl::Substitute("JsonSet('$0', '$1', 999)", json_string, path));
      JSONValue json = JSONValue::ParseJSONString(json_string).value();
      JSONValueRef ref = json.GetRef();
      std::unique_ptr<StrictJSONPathIterator> path_iterator =
          ParseJSONPath(path);
      GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, value,
                        /*create_if_missing=*/true, LanguageOptions(),
                        /*canonicalize_zero=*/true));
      if (expected_output.has_value()) {
        EXPECT_THAT(
            ref,
            JsonEq(
                JSONValue::ParseJSONString(*expected_output)->GetConstRef()));
      } else {
        // This is an expected no-op.
        EXPECT_THAT(
            ref,
            JsonEq(
                JSONValue::ParseJSONString(json_string).value().GetConstRef()));
      }
    }
  };

  test_fn(/*path=*/"$", "999");
  test_fn(/*path=*/"$.a", std::nullopt);
  test_fn(/*path=*/"$[0]", std::nullopt);
}

TEST(JsonSetTest, ValidTopLevelEmptyObject) {
  const Value value = Value::Int64(999);
  auto test_fn = [&](absl::string_view path,
                     absl::string_view expected_output) {
    JSONValue json;
    JSONValueRef ref = json.GetRef();
    ref.SetToEmptyObject();
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, value, /*create_if_missing=*/true,
                      LanguageOptions(),
                      /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  // Empty path.
  test_fn(/*path=*/"$", /*expected_output=*/"999");
  // Insert single key.
  test_fn(/*path=*/"$.a", /*expected_output=*/R"({"a":999})");
  // Recursive creation of multiple object keys.
  test_fn(/*path=*/"$.a.b", /*expected_output=*/R"({"a":{"b":999}})");
  // Recursive creation of multiple object keys and array index.
  test_fn(/*path=*/"$.a.b[2]",
          /*expected_output=*/R"({"a":{"b":[null, null, 999]}})");
  // Recursive creation of multiple object keys and array index.
  test_fn(/*path=*/"$.a.b[2].c",
          /*expected_output=*/R"({"a":{"b":[null, null, {"c":999}]}})");
}

TEST(JsonSetTest, ValidTopLevelEmptyArray) {
  const Value value = Value::Int64(999);
  auto test_fn = [&](absl::string_view path,
                     absl::string_view expected_output) {
    JSONValue json;
    JSONValueRef ref = json.GetRef();
    ref.SetToEmptyArray();
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, value, /*create_if_missing=*/true,
                      LanguageOptions(),
                      /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  // Empty path.
  test_fn(/*path=*/"$", /*expected_output=*/"999");
  // Set element into first position.
  test_fn(/*path=*/"$[0]", /*expected_output=*/"[999]");
  // Set past the end of the array.
  test_fn(/*path=*/"$[2]", /*expected_output=*/"[null, null, 999]");
  // Recursive creation of nested arrays.
  test_fn(/*path=*/"$[2][1]",
          /*expected_output=*/"[null, null, [null, 999]]");
  // Recursive creation of nested arrays and objects.
  test_fn(/*path=*/"$[2][1].a",
          /*expected_output=*/R"([null, null, [null, {"a":999}]])");
}

TEST(JsonSetTest, ComplexTests) {
  constexpr absl::string_view kInitialObjectValue =
      R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "f":[2, [], {}, [3, 4]]})";
  const Value value = Value::Int64(999);

  auto test_fn = [&](absl::string_view path,
                     absl::string_view expected_output) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue json = JSONValue::ParseJSONString(kInitialObjectValue).value();
    JSONValueRef ref = json.GetRef();
    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, value, /*create_if_missing=*/true,
                      LanguageOptions(),
                      /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  // Replace the entire value.
  test_fn(/*path=*/"$", /*expected_output=*/"999");
  // Insert object into null.
  test_fn(
      /*path=*/"$.a.b.c",
      /*expected_output=*/
      R"({"a":{"b":{"c":999}}, "b":{}, "c":[], "d":{"e":1},
                          "f":[2, [], {}, [3, 4]]})");
  // Insert array into null.
  test_fn(
      /*path=*/"$.a[1][2]",
      /*expected_output=*/R"({"a":[null, [null, null, 999]], "b":{}, "c":[],
                          "d":{"e":1}, "f":[2,[],{},[3,4]]})");
  // Set operation ignored. Type mismatch.
  test_fn(/*path=*/"$.d[1]", /*expected_output=*/kInitialObjectValue);
  // Replace an object key.
  test_fn(/*path=*/"$.f", /*expected_output=*/
          R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "f":999})");
  // Insert key into top level object.
  test_fn(
      /*path=*/"$.g",
      /*expected_output=*/R"({"a":null, "b":{}, "c":[], "d":{"e":1},
                          "f":[2,[],{},[3,4]], "g":999})");
  // Inserts key in nested object.
  test_fn(
      /*path=*/"$.b.c",
      /*expected_output=*/R"({"a":null, "b":{"c":999}, "c":[], "d":{"e":1},
                          "f":[2,[],{},[3,4]]})");
  // Inserts key in empty object with basic recursive creation.
  test_fn(
      /*path=*/"$.b.c.d",
      /*expected_output=*/R"({"a":null, "b":{"c":{"d":999}}, "c":[],
                "d":{"e":1}, "f":[2,[],{},[3,4]]})");
  // Inserts into empty array.
  test_fn(
      /*path=*/"$.c[0]",
      /*expected_output=*/R"({"a":null, "b":{}, "c":[999], "d":{"e":1},
               "f":[2,[],{},[3,4]]})");
  // Inserts into empty array with basic recursive creation.
  test_fn(
      /*path=*/"$.c[0][1]",
      /*expected_output=*/R"({"a":null, "b":{}, "c":[[null, 999]],
               "d":{"e":1}, "f":[2,[],{},[3,4]]})");
  // Inserts into empty array with recursive creation of nested arrays
  // and objects.
  test_fn(
      /*path=*/"$.c[0][1].y",
      /*expected_output=*/R"({"a":null, "b":{}, "c":[[null, {"y":999}]],
               "d":{"e":1}, "f":[2,[],{},[3,4]]})");
  // Replaces specific element in an array.
  test_fn(
      /*path=*/"$.f[1]",
      /*expected_output=*/R"({"a":null, "b":{}, "c":[], "d":{"e":1},
               "f":[2, 999, {}, [3, 4]]})");
  // Inserts past end of an array with recursive creation.
  test_fn(
      /*path=*/"$.f[4].x.y[1]",
      /*expected_output=*/
      R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "f":[2, [], {}, [3, 4],
              {"x":{"y":[null, 999]}}]})");
}

TEST(JsonSetTest, CanonicalizeZero) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";
  std::unique_ptr<StrictJSONPathIterator> path_iterator =
      ParseJSONPath(/*path*/ "$[2]");
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, Value::Double(-0.0),
                      /*create_if_missing=*/true, LanguageOptions(),
                      /*canonicalize_zero=*/true));
    EXPECT_THAT(ref,
                JsonEq(JSONValue::ParseJSONString(R"(["foo", null, 0.0, 10])")
                           ->GetConstRef()));
  }
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, Value::Double(-0.0),
                      /*create_if_missing=*/true, LanguageOptions(),
                      /*canonicalize_zero=*/false));
    EXPECT_THAT(ref,
                JsonEq(JSONValue::ParseJSONString(R"(["foo", null, -0.0, 10])")
                           ->GetConstRef()));
  }
}

TEST(JsonSetTest, StrictNumberParsing) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a":true}, 10])";
  std::unique_ptr<StrictJSONPathIterator> path_iterator =
      ParseJSONPath(/*path*/ "$[2]");
  Value big_value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonSet(ref, *path_iterator, big_value,
                        /*create_if_missing=*/true, options,
                        /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
  }
  {
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, big_value,
                      /*create_if_missing=*/true, LanguageOptions(),
                      /*canonicalize_zero=*/true));
    EXPECT_THAT(ref, JsonEq(JSONValue::ParseJSONString(
                                R"(["foo", null, 1.111111111111111111, 10])")
                                ->GetConstRef()));
  }
}

TEST(JsonSetTest, FailedConversionComesFirst) {
  constexpr absl::string_view kInitialValue =
      R"(["foo", null, {"a": true}, 10])";

  Value big_value = values::BigNumeric(
      BigNumericValue::FromString("1.111111111111111111").value());

  {
    // An error is returned even if the path doesn't exist.
    auto path_iter = ParseJSONPath("$.a[1]");
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonSet(ref, *path_iter, big_value,
                        /*create_if_missing=*/true, options,
                        /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
  {
    // Auto-creation only happens when the conversion succeeds.
    auto path_iter = ParseJSONPath("$[2].b[0]");
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    LanguageOptions options;
    options.EnableLanguageFeature(FEATURE_JSON_STRICT_NUMBER_PARSING);

    ASSERT_THAT(JsonAppendArrayElement(ref, *path_iter, big_value, options,
                                       /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
}

TEST(JsonSetTest, MaxArraySizeExceeded) {
  constexpr absl::string_view kInitialValue = R"({"a": [10]})";

  {
    auto path_iter =
        ParseJSONPath(absl::Substitute("$$.a[$0]", kJSONMaxArraySize));
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    EXPECT_THAT(JsonSet(ref, *path_iter, Value::String("foo"),
                        /*create_if_missing=*/true, LanguageOptions(),
                        /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
  {
    auto path_iter =
        ParseJSONPath(absl::Substitute("$$.b[$0]", kJSONMaxArraySize));
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();

    EXPECT_THAT(JsonSet(ref, *path_iter, Value::String("foo"),
                        /*create_if_missing=*/true, LanguageOptions(),
                        /*canonicalize_zero=*/true),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Exceeded maximum array size")));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  }
}

TEST(JsonSetTest, CreateIfMissingFalse) {
  constexpr absl::string_view kInitialObjectValue =
      R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "f":[2, [], {}, [3, 4]]})";
  const Value value = Value::Int64(999);

  auto test_fn = [&](absl::string_view path,
                     absl::string_view expected_output) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue json = JSONValue::ParseJSONString(kInitialObjectValue).value();
    JSONValueRef ref = json.GetRef();
    GOOGLESQL_ASSERT_OK(JsonSet(ref, *path_iterator, value, /*create_if_missing=*/false,
                      LanguageOptions(),
                      /*canonicalize_zero=*/true));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  // Replace the entire value.
  test_fn(/*path=*/"$", /*expected_output=*/"999");
  // Key doesn't exist in NULL value. Ignore.
  test_fn(
      /*path=*/"$.a.b", kInitialObjectValue);
  // Key doesn't in object. Ignore.
  test_fn(
      /*path=*/"$.b.c", kInitialObjectValue);
  // Array index is larger than size of array. Ignore.
  test_fn(
      /*path=*/"$.c[0]", kInitialObjectValue);
  // Replace nested object key.
  test_fn(
      /*path=*/"$.d.e",
      R"({"a":null, "b":{}, "c":[], "d":{"e":999}, "f":[2, [], {}, [3, 4]]})");
  // Replace array index.
  test_fn(
      /*path=*/"$.f[0]",
      R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "f":[999, [], {}, [3, 4]]})");
}

TEST(JsonSetTest, ErrorOnTypeMismatch) {
  constexpr absl::string_view kInitialValue = R"({"a":1, "b":[], "c":[1, {}]})";
  struct TestCase {
    std::string path;
    JsonMismatchType mismatch_type;
  };
  std::vector<TestCase> test_cases = {
      {.path = "$.a[0]",
       .mismatch_type = JsonMismatchType::kArrayIndexMismatch},
      {.path = "$.b.c",
       .mismatch_type = JsonMismatchType::kObjectFieldMismatch},
      {.path = "$.c[1][0]",
       .mismatch_type = JsonMismatchType::kArrayIndexMismatch},
      {.path = "$.a.b",
       .mismatch_type = JsonMismatchType::kObjectFieldMismatch},
  };

  for (const auto& test_case : test_cases) {
    for (bool create_if_missing : {true, false}) {
      SCOPED_TRACE(absl::Substitute("JsonSet('$0', '$1', 10, $2, true)",
                                    kInitialValue, test_case.path,
                                    create_if_missing));
      JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
      JSONValueRef ref = value.GetRef();
      std::unique_ptr<StrictJSONPathIterator> path_iterator =
          ParseJSONPath(test_case.path);
      JsonSetFailureDetails failure_details;
      EXPECT_THAT(
          JsonSet(ref, *path_iterator, Value::Int64(10), create_if_missing,
                  LanguageOptions(),
                  /*canonicalize_zero=*/true,
                  /*error_on_type_mismatch=*/true, &failure_details),
          StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("Type mismatch")));
      EXPECT_THAT(
          ref,
          JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
      EXPECT_THAT(failure_details.mismatch_type, test_case.mismatch_type);
    }
  }
}

TEST(JsonSetTest, NoErrorOnTypeMismatch) {
  constexpr absl::string_view kInitialValue = R"({"a":1})";
  JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
  JSONValueRef ref = value.GetRef();
  std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath("$.a");
  JsonSetFailureDetails failure_details;
  GOOGLESQL_EXPECT_OK(JsonSet(ref, *path_iterator, Value::Int64(10),
                    /*create_if_missing=*/true, LanguageOptions(),
                    /*canonicalize_zero=*/true,
                    /*error_on_type_mismatch=*/false, &failure_details));
  EXPECT_THAT(ref,
              JsonEq(JSONValue::ParseJSONString(R"({"a":10})")->GetConstRef()));
  EXPECT_THAT(failure_details.mismatch_type, JsonMismatchType::kNone);
}

TEST(JsonStripNullsTest, NoopPathNonexistent) {
  constexpr absl::string_view kInitialValue =
      R"({"a":1, "b":[null, {"c":null}], "d":{"e":[null], "f":[null]}})";

  auto test_fn = [&kInitialValue](absl::string_view path) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonStripNulls(ref, *path_iterator,
                             /*include_arrays=*/true,
                             /*remove_empty=*/true));
    GOOGLESQL_ASSERT_OK(JsonStripNulls(ref, *path_iterator, /*include_arrays=*/true,
                             /*remove_empty=*/true));
    EXPECT_THAT(
        ref, JsonEq(JSONValue::ParseJSONString(kInitialValue)->GetConstRef()));
  };

  // Path suffix ".b" doesn't exist.
  test_fn("$.a.b");
  // Path suffix "[2]" is larger than existing array.
  test_fn("$.b[2]");
  // Type mismatch. Path prefix "$.b" is an array but expected object.
  test_fn("$.b.c");
  // Type mismatch. Path prefix "$.d" is an object but expected array.
  test_fn("$.d[1]");
  // Object "$.d" doesn't contain key "$.z".
  test_fn("$.d.z");
}

TEST(JsonStripNullsTest, SimpleObject) {
  constexpr absl::string_view kInitialValue =
      R"({"a":null, "b":1, "c":[null, true], "d":{}, "e":[null], "f":[]})";

  auto test_fn = [&kInitialValue](absl::string_view path, bool include_arrays,
                                  bool remove_empty,
                                  absl::string_view expected_output) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonStripNulls(ref, *path_iterator,
                             /*include_arrays=*/include_arrays,
                             /*remove_empty=*/remove_empty));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  test_fn("$", /*include_arrays=*/false, /*remove_empty=*/false,
          R"({"b":1, "c":[null, true], "d":{}, "e":[null], "f":[]})");
  test_fn("$", /*include_arrays=*/true, /*remove_empty=*/false,
          R"({"b":1, "c":[true], "d":{}, "e":[], "f":[]})");
  test_fn("$", /*include_arrays=*/false, /*remove_empty=*/true,
          R"({"b":1, "c":[null, true], "e":[null], "f":[]})");
  test_fn("$", /*include_arrays=*/true, /*remove_empty=*/true,
          R"({"b":1, "c":[true]})");
  // Subpath points to a simple type. Does nothing.
  test_fn("$.a", /*include_arrays=*/true, /*remove_empty=*/true, kInitialValue);
  test_fn("$.c", /*include_arrays=*/true, /*remove_empty=*/true,
          R"({"a":null, "b":1, "c":[true], "d":{}, "e":[null],
               "f":[]})");
}

TEST(JsonStripNullsTest, SimpleArray) {
  constexpr absl::string_view kInitialValue =
      R"(["a", null, 1.1, [], [null], [1, null], {}, {"a":null},
         {"b":1, "c":null}])";

  auto test_fn = [&kInitialValue](absl::string_view path, bool include_arrays,
                                  bool remove_empty,
                                  absl::string_view expected_output) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonStripNulls(ref, *path_iterator,
                             /*include_arrays=*/include_arrays,
                             /*remove_empty=*/remove_empty));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  test_fn("$", /*include_arrays=*/false, /*remove_empty=*/false,
          R"(["a", null, 1.1, [], [null], [1, null], {}, {},
                      {"b":1}])");
  test_fn("$", /*include_arrays=*/true, /*remove_empty=*/false,
          R"(["a", 1.1, [], [], [1], {}, {}, {"b":1}])");
  // Because parent of empty OBJECTs is an ARRAY, empty OBJECTs are not
  // removed.
  test_fn("$", /*include_arrays=*/false, /*remove_empty=*/true,
          R"(["a", null, 1.1, [], [null], [1, null], {}, {}, {"b":1}])");
  test_fn("$", /*include_arrays=*/true, /*remove_empty=*/true,
          R"(["a", 1.1, [1], {"b":1}])");
  // Subpath points to an array that is replaced with JSON 'null'.
  test_fn("$[4]", /*include_arrays=*/true, /*remove_empty=*/true,
          R"(["a", null, 1.1, [], null, [1, null], {}, {"a":null},
              {"b":1, "c":null}])");
  // Subpath points to an OBJECT that is replaced with JSON 'null'.
  test_fn("$[7]", /*include_arrays=*/true, /*remove_empty=*/true,
          R"(["a", null, 1.1, [], [null], [1, null], {}, null,
              {"b":1, "c":null}])");
}

TEST(JsonStripNullsTest, AllNullsOrEmptyObject) {
  constexpr absl::string_view kInitialValue =
      R"({"a": {"b":null, "c":null, "d":[[null], null]}, "e":null})";

  auto test_fn = [&kInitialValue](absl::string_view path, bool include_arrays,
                                  bool remove_empty,
                                  absl::string_view expected_output) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonStripNulls(ref, *path_iterator,
                             /*include_arrays=*/include_arrays,
                             /*remove_empty=*/remove_empty));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  test_fn("$", /*include_arrays=*/true, /*remove_empty=*/true, "null");
  // No change. Subpath already JSON 'null'.
  test_fn("$.e", /*include_arrays=*/true, /*remove_empty=*/true, kInitialValue);
  // No change. Subpath points to a nested ARRAY.
  test_fn("$.a.d", /*include_arrays=*/false, /*remove_empty=*/true,
          kInitialValue);
  // Subpath is nested ARRAY and removes JSON 'null's.
  test_fn("$.a.d", /*include_arrays=*/true, /*remove_empty=*/false,
          R"({"a": {"b":null, "c":null, "d":[[]]}, "e":null})");
  // Subpath is nested ARRAY replaced by JSON 'null'.
  test_fn("$.a.d", /*include_arrays=*/true, /*remove_empty=*/true,
          R"({"a": {"b":null, "c":null, "d":null}, "e":null})");
  // Subpath is OBJECT replaced by JSON 'null'.
  test_fn("$.a", /*include_arrays=*/true, /*remove_empty=*/true,
          R"({"a":null, "e":null})");
}

TEST(JsonStripNullsTest, AllNullsOrEmptyArray) {
  constexpr absl::string_view kInitialValue =
      R"([null, {"b":null, "c":null, "d":[[null], null]}, [null, null],
      []])";

  auto test_fn = [&kInitialValue](absl::string_view path, bool include_arrays,
                                  bool remove_empty,
                                  absl::string_view expected_output) {
    std::unique_ptr<StrictJSONPathIterator> path_iterator = ParseJSONPath(path);
    JSONValue value = JSONValue::ParseJSONString(kInitialValue).value();
    JSONValueRef ref = value.GetRef();
    GOOGLESQL_ASSERT_OK(JsonStripNulls(ref, *path_iterator,
                             /*include_arrays=*/include_arrays,
                             /*remove_empty=*/remove_empty));
    EXPECT_THAT(
        ref,
        JsonEq(JSONValue::ParseJSONString(expected_output)->GetConstRef()));
  };

  test_fn("$", /*include_arrays=*/true, /*remove_empty=*/true, "null");
  // Cleanup nested arrays to JSON 'null'.
  test_fn("$[1]", /*include_arrays=*/true, /*remove_empty=*/true,
          "[null, null, [null, null],[]]");
  // Cleanup nested arrays to JSON 'null' but no array cleanup.
  test_fn("$[1]", /*include_arrays=*/false, /*remove_empty=*/true,
          R"([null, {"d":[[null], null]}, [null, null], []])");
  // No change. Subpath points to a nested ARRAY.
  test_fn("$[1].d", /*include_arrays=*/false, /*remove_empty=*/true,
          kInitialValue);
}

TEST(JsonQueryLax, InvalidPathInput) {
  std::unique_ptr<StrictJSONPathIterator> path_iterator =
      ParseJSONPath("$.a", /*enable_lax=*/true);
  JSONValue value;
  // The input is not a lax path.
  EXPECT_FALSE(JsonQueryLax(value.GetRef(), *path_iterator).status().ok());
}

class JsonQueryLaxTest
    : public ::testing::TestWithParam<
          std::tuple</*input=*/std::string, /*path=*/std::string,
                     /*expected_result=*/std::string>> {
 protected:
  absl::string_view GetJSONDoc() const { return std::get<0>(GetParam()); }
  absl::string_view GetPath() const { return std::get<1>(GetParam()); }
  absl::string_view GetExpectedResult() const {
    return std::get<2>(GetParam());
  }
};

INSTANTIATE_TEST_SUITE_P(
    JsonQueryLaxSimpleTests, JsonQueryLaxTest,
    ::testing::Values(
        // We do not test order or capitalization of JSONPath keywords as
        // these are already tested in StrictJSONPathIterator tests.
        //
        // Key doesn't exist
        std::make_tuple(R"({"a":1})", "lax $.A", "[]"),
        std::make_tuple(R"({"a":1})", "lax recursive $.A", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax $.b", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax recursive $.b", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax $.a.c", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax recursive $.a.c", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax $[1]", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax recursive $[1]", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax $[0].b", "[]"),
        std::make_tuple(R"({"a":{"b":1}})", "lax recursive $[0].b", "[]"),
        // NULL JSON input.
        std::make_tuple("null", "lax $", "[null]"),
        std::make_tuple("null", "lax recursive $", "[null]"),
        std::make_tuple("null", "lax $.a", "[]"),
        std::make_tuple("null", "lax recursive $.a", "[]"),
        std::make_tuple("null", "lax $[0]", "[null]"),
        std::make_tuple("null", "lax recursive $[0]", "[null]"),
        std::make_tuple("null", "lax $[1]", "[]"),
        std::make_tuple("null", "lax recursive $[1]", "[]"),
        // Normal object key match with matching types.
        std::make_tuple(R"({"a":null})", "lax $", R"([{"a":null}])"),
        std::make_tuple(R"({"a":null})", "lax recursive $", R"([{"a":null}])"),
        std::make_tuple(R"({"a":null})", "lax $.a", "[null]"),
        std::make_tuple(R"({"a":null})", "lax recursive $.a", "[null]"),
        std::make_tuple(R"({"a":1, "b":2})", "lax $.b", "[2]"),
        std::make_tuple(R"({"a":1, "b":2})", "lax recursive $.b", "[2]"),
        // Normal array index match with matching types.
        std::make_tuple(R"([[null]])", "lax $", "[[[null]]]"),
        std::make_tuple(R"([[null]])", "lax recursive $", "[[[null]]]"),
        std::make_tuple(R"([[null]])", "lax $[0]", "[[null]]"),
        std::make_tuple(R"([[null]])", "lax recursive  $[0]", "[[null]]"),
        // Index larger than array size.
        std::make_tuple(R"([[null], 1])", "lax $[2]", "[]"),
        std::make_tuple(R"([[null], 1])", "lax recursive $[2]", "[]"),
        // Single level of array.
        std::make_tuple(R"([{"a":1}])", "lax $.a", "[1]"),
        std::make_tuple(R"([{"a":1}])", "lax recursive  $.a", "[1]"),
        // 2-level of arrays.
        std::make_tuple(R"([[{"a":1}]])", "lax $.a", "[]"),
        std::make_tuple(R"([{"a":1}])", "lax recursive  $.a", "[1]"),
        // Mix of 1,2,3 levels of arrays.
        std::make_tuple(
            R"([{"a":1}, {"b":2}, [[{"a":3}]],[[{"b":4}]],[[[{"a":5}]]]])",
            "lax $.a", "[1]"),
        std::make_tuple(
            R"([{"a":1}, {"b":2}, [[{"a":3}]],[[{"b":4}]],[[[{"a":5}]]]])",
            "lax recursive $.a", "[1,3,5]"),
        std::make_tuple(
            R"([{"a":1}, {"b":2}, [[{"a":3}]],[[{"b":4}]],[[[{"a":5}]]]])",
            "lax $.b", "[2]"),
        std::make_tuple(
            R"([{"a":1}, {"b":2}, [[{"a":3}]],[[{"b":4}]],[[[{"a":5}]]]])",
            "lax recursive $.b", "[2,4]"),
        // Wrap non-array before match.
        std::make_tuple(R"({"a":1})", "lax $[0].a", "[1]"),
        std::make_tuple(R"({"a":1})", "lax recursive $[0].a", "[1]"),
        std::make_tuple(R"({"a":[1]})", "lax $[0][0].a", "[[1]]"),
        std::make_tuple(R"({"a":[1]})", "lax recursive $[0][0].a", "[[1]]"),
        // Key 'b' doesn't exist in matched JSON subtree.
        std::make_tuple(R"({"a":[1]})", "lax $[0][0].b", "[]"),
        std::make_tuple(R"({"a":[1]})", "lax recursive $[0][0].b", "[]"),
        // Wrap non-array before match. Index larger than array size.
        std::make_tuple("1", "lax $[1]", "[]"),
        std::make_tuple("1", "lax recursive $[1]", "[]"),
        // Second index is larger than size of wrapped array.
        std::make_tuple(R"({"a":1})", "lax $[0][1].a", "[]"),
        std::make_tuple(R"({"a":1})", "lax recursive $[0][1].a", "[]")));

INSTANTIATE_TEST_SUITE_P(
    JsonQueryLaxComplexTests, JsonQueryLaxTest,
    ::testing::Values(
        // 'b' is not included in every nested object.
        std::make_tuple(R"({"a":[{"b":1}, {"c":2}, {"b":3}, [{"b":4}]]})",
                        "lax $.a.b", "[1,3]"),
        std::make_tuple(R"({"a":[{"b":1}, {"c":2}, {"b":3}, [{"b":4}]]})",
                        "lax recursive $.a.b", "[1,3,4]"),
        // 'c' is not included in every nested object.
        std::make_tuple(
            R"({"a":[{"b":1}, {"c":2}, {"b":3}, [{"b":4}], null,
                     [[{"c":5}]]]})",
            "lax $.a.c", "[2]"),
        std::make_tuple(
            R"({"a":[{"b":1}, {"c":2}, {"b":3}, [{"b":4}], null,
                     [[{"c":5}]]]})",
            "lax recursive $.a.c", "[2,5]"),
        // 'a.b' has different levels of nestedness
        std::make_tuple(R"({"a":[1, {"b":2}, [{"b":3}, null, 4,
                          {"b":[5]}]]})",
                        "lax $.a.b", "[2]"),
        std::make_tuple(R"({"a":[1, {"b":2}, [{"b":3}, 4,  null, {"b":[5]}]]})",
                        "lax recursive $.a.b", "[2, 3, [5]]"),
        // Both 'a' and 'a.b' have different levels of nestedness.
        std::make_tuple(
            R"([{"a":[1, {"b":2}, [{"b":3}, [{"b": 4}]]]},
                [{"a":{"b":5}}, null]])",
            "lax $.a.b", "[2]"),
        std::make_tuple(
            R"([{"a":[1, {"b":2}, [{"b":3}, [{"b": 4}]]]},
                [{"a":{"b":5}}, null]])",
            "lax recursive $.a.b", "[2, 3, 4, 5]"),
        // Specific array indices and different levels of nestedness with
        // autowrap.
        std::make_tuple(
            R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
                [{"a":{"b":5}}, null]])",
            "lax $.a[0].b", "[2]"),
        std::make_tuple(
            R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
                [{"a":{"b":5}}, null]])",
            "lax recursive $.a[0].b", "[2, 5]"),
        std::make_tuple(
            R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
                [{"a":{"b":5}}, null]])",
            "lax $[1].a[0].b", "[5]"),
        std::make_tuple(
            R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
                [{"a":{"b":5}}, null]])",
            "lax recursive $[1].a[0][0][0].b", "[5]")));

TEST_P(JsonQueryLaxTest, Success) {
  std::unique_ptr<StrictJSONPathIterator> path_iterator =
      ParseJSONPath(GetPath(), /*enable_lax=*/true);
  // Increment the path iterator to ensure it is correctly reset during
  // execution.
  ++(*path_iterator);
  JSONValue value = JSONValue::ParseJSONString(GetJSONDoc()).value();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue result,
                       JsonQueryLax(value.GetRef(), *path_iterator));
  EXPECT_THAT(
      result.GetConstRef(),
      JsonEq(JSONValue::ParseJSONString(GetExpectedResult())->GetConstRef()));
}

struct JsonContainsTestCase {
  std::string input;
  std::string target;
  bool expected;
  // If no value is given, the test case must pass for both "true" and "false"
  // values of compare_numbers_in_decimal.
  std::optional<bool> compare_numbers_in_decimal = std::nullopt;
};

void TestJsonContainsFn(absl::Span<const JsonContainsTestCase> cases) {
  for (const auto& [input, target, expected, compare_numbers_in_decimal] :
       cases) {
    JSONValue input_value = JSONValue::ParseJSONString(input).value();
    JSONValue target_value = JSONValue::ParseJSONString(target).value();
    bool result =
        JsonContains(input_value.GetConstRef(), target_value.GetConstRef(),
                     compare_numbers_in_decimal.value_or(false));
    EXPECT_EQ(result, expected)
        << "Failed test case: JSON_CONTAINS(" << input << ", " << target
        << ") should return " << !expected;
    if (!compare_numbers_in_decimal.has_value()) {
      EXPECT_EQ(
          JsonContains(input_value.GetConstRef(), target_value.GetConstRef(),
                       /*compare_numbers_in_decimal=*/true),
          result)
          << "Failed test case: JSON_CONTAINS(" << input << ", " << target
          << ") with compare_numbers_in_decimal=true should return " << result;
    }
  }
}

// This is the value 2^55 as a double serialized to JSON. It is interesting
// because it is rounded when converting to decimal. JSON comparison with this
// value are therefore sensitive to compare_numbers_in_decimal.
constexpr const char* kRoundedDouble = R"(3.602879701896397e+16)";
// This is the value of casting said double to an int64. It compares equal to
// the double only when compare_numbers_in_decimal = false;
constexpr const char* kRoundedDoubleToIntBinary = R"(36028797018963968)";
// This is the value of converting the serialized decimal representation of the
// double to an integer. It compares equal to the double only when
// compare_numbers_in_decimal = true;
constexpr const char* kRoundedDoubleToIntDecimal = R"(36028797018963970)";

TEST(JsonContainsTest, JsonScalar) {
  std::vector<JsonContainsTestCase> cases;
  // Test boolean type.
  cases.push_back({R"(true)", R"(true)", true});
  cases.push_back({R"(true)", R"(false)", false});

  // Test numeric type.
  cases.push_back({R"(0)", R"(0)", true});
  cases.push_back({R"(1)", R"(1)", true});
  cases.push_back({R"(0)", R"(-0.0)", true});
  cases.push_back({R"(1)", R"(1.0)", true});
  cases.push_back({R"(1)", R"(1.00)", true});
  cases.push_back({R"(1.0)", R"(1)", true});
  cases.push_back({R"(1.0)", R"(1.00)", true});

  // Numeric cases which are sensitive to compare_numbers_in_decimal.
  cases.push_back({kRoundedDouble, kRoundedDoubleToIntBinary, true,
                   /*compare_numbers_in_decimal=*/false});
  cases.push_back({kRoundedDouble, kRoundedDoubleToIntBinary, false,
                   /*compare_numbers_in_decimal=*/true});
  cases.push_back({kRoundedDouble, kRoundedDoubleToIntDecimal, false,
                   /*compare_numbers_in_decimal=*/false});
  cases.push_back({kRoundedDouble, kRoundedDoubleToIntDecimal, true,
                   /*compare_numbers_in_decimal=*/true});

  // Near-miss test case for compare_numbers_in_decimal. When both values are
  // cast to doubles they are equal, but only because of rounding done during
  // the int64 -> double conversion. These should not be equal in either decimal
  // or binary comparison modes.
  cases.push_back({R"(3.602879701896397e+16)", R"(36028797018963969)", false});

  // Test string type.
  cases.push_back({R"("true")", R"("true")", true});
  cases.push_back({R"("true")", R"("TRUE")", false});
  cases.push_back({R"("true")", R"(true)", false});
  cases.push_back({R"("true")", R"(1)", false});

  // Test null type.
  cases.push_back({R"(null)", R"(null)", true});
  cases.push_back({R"(null)", R"("null")", false});

  TestJsonContainsFn(cases);
}

TEST(JsonContainsTest, JsonArray) {
  std::vector<JsonContainsTestCase> cases;

  //
  // Test JSON array with nested arrays.
  //
  std::string input = R"([1,2,[3,4,[5]],[6,7]])";
  // Test empty array.
  cases.push_back({R"([])", R"([])", true});
  cases.push_back({input, R"([])", true});
  cases.push_back({input, R"([[]])", true});
  cases.push_back({input, R"([[[]]])", true});
  cases.push_back({input, R"([[[[]]]])", false});

  // Test scalar values.
  cases.push_back({input, R"(1)", true});
  cases.push_back({input, R"(2)", true});
  cases.push_back({input, R"(3)", false});
  cases.push_back({input, R"(5)", false});
  cases.push_back({input, R"(6)", false});
  cases.push_back({input, R"(null)", false});

  // Test array values.
  cases.push_back({input, R"([1,1])", true});
  cases.push_back({input, R"([3])", false});
  cases.push_back({input, R"([[3]])", true});
  cases.push_back({input, R"([[3,4,3]])", true});
  cases.push_back({input, R"([[4,3]])", true});
  cases.push_back({input, R"([[3,4,5]])", false});
  cases.push_back({input, R"([[3,4,6]])", false});
  cases.push_back({input, R"([5])", false});
  cases.push_back({input, R"([[5]])", false});
  cases.push_back({input, R"([[[5]]])", true});
  cases.push_back({input, R"([1,[[5]]])", true});
  cases.push_back({input, R"([[3,[5]]])", true});
  cases.push_back({input, R"([[[5],3]])", true});
  cases.push_back({input, R"([[6,[5]]])", false});
  cases.push_back({input, R"([[6],[[5]]])", true});
  cases.push_back({input, R"([[7,4]])", false});
  cases.push_back({input, R"([[7,6]])", true});

  // Test object values.
  cases.push_back({input, R"({})", false});
  cases.push_back({input, R"({"a":1})", false});

  //
  // Test JSON array with nested objects.
  //
  input = R"([{"a":1,"b":2},
              [{"a":1,"b":3},{"c":[3,4]},{"d":5}],
              {"a":1,"b":4,"c":5,"d":{"e":6,"f":7}}
             ])";
  cases.push_back({input, R"({})", false});
  cases.push_back({input, R"([])", true});
  cases.push_back({input, R"([{}])", true});
  cases.push_back({input, R"([[]])", true});

  cases.push_back({input, R"([{"a":1}])", true});
  cases.push_back({input, R"([{"b":2}])", true});
  cases.push_back({input, R"([{"b":2, "c":5}])", false});
  cases.push_back({input, R"([{"b":4, "c":5}])", true});

  // Test nested array.
  cases.push_back({input, R"([[{"a":1}]])", true});
  cases.push_back({input, R"([[{"a":2}]])", false});
  cases.push_back({input, R"([[{"a":1},{"b":3}]])", true});
  cases.push_back({input, R"([[{"a":1,"b":3}]])", true});
  cases.push_back({input, R"([[{"a":1},{"d":5}]])", true});
  cases.push_back({input, R"([[{"a":1,"d":5}]])", false});

  cases.push_back({input, R"([[{"c":3}]])", false});
  cases.push_back({input, R"([[{"c":[]}]])", true});
  cases.push_back({input, R"([[{"d":[]}]])", false});
  cases.push_back({input, R"([[{"c":[3]}]])", true});
  cases.push_back({input, R"([[{"c":[3,3]}]])", true});
  cases.push_back({input, R"([[{"c":[3,5]}]])", false});

  // Test nested objects.
  cases.push_back({input, R"([{"d":[]}])", false});
  cases.push_back({input, R"([{"d":{}}])", true});
  cases.push_back({input, R"([{"d":{"e":6}}])", true});
  cases.push_back({input, R"([{"d":{"e":6, "b":2}}])", false});
  cases.push_back({input, R"([{"d":{"e":6}}, {"b":2}])", true});
  cases.push_back({input, R"([{"d":{"e":6}}, {"b":3}])", false});
  cases.push_back({input, R"([{"d":{"e":6, "b":4}}])", false});
  cases.push_back({input, R"([{"d":{"e":6}, "b":4}])", true});
  cases.push_back({input, R"([{"d":{"e":6}}, {"b":4}])", true});
  cases.push_back({input, R"([{"d":{"e":6,"f":7}}])", true});

  // Numeric cases which are sensitive to compare_numbers_in_decimal.
  auto to_arr = [](const char* s) { return absl::StrCat("[", s, "]"); };
  cases.push_back({to_arr(kRoundedDouble), to_arr(kRoundedDoubleToIntBinary),
                   true, /*compare_numbers_in_decimal=*/false});
  cases.push_back({to_arr(kRoundedDouble), to_arr(kRoundedDoubleToIntBinary),
                   false, /*compare_numbers_in_decimal=*/true});
  cases.push_back({to_arr(kRoundedDouble), to_arr(kRoundedDoubleToIntDecimal),
                   false, /*compare_numbers_in_decimal=*/false});
  cases.push_back({to_arr(kRoundedDouble), to_arr(kRoundedDoubleToIntDecimal),
                   true, /*compare_numbers_in_decimal=*/true});

  TestJsonContainsFn(cases);
}

TEST(JsonContainsTest, JsonObject) {
  std::vector<JsonContainsTestCase> cases;
  //
  // Test basic JSON object cases.
  //
  std::string input = R"({"a":true, "b":null, "c":1, "d": -10, "e":1.1570E2,
                          "f": "1.0", "g": {"a":1, "b":2, "c":{"d":2,"e":3}}
                         })";
  // Test empty object.
  cases.push_back({input, R"({})", true});
  cases.push_back({input, R"([{}])", false});

  // Test boolean values.
  cases.push_back({input, R"({"a":true})", true});
  cases.push_back({input, R"({"a":false})", false});
  cases.push_back({input, R"({"a":1})", false});
  cases.push_back({input, R"({"b":true})", false});

  // Test null values.
  cases.push_back({input, R"({"b":null})", true});
  cases.push_back({input, R"({"z":null})", false});
  cases.push_back({input, R"({"b":2})", false});

  // Test numeric values.
  cases.push_back({input, R"({"c":1})", true});
  cases.push_back({input, R"({"c":1.00})", true});
  cases.push_back({input, R"({"c":1.01})", false});
  cases.push_back({input, R"({"d":-10})", true});
  cases.push_back({input, R"({"d":-10.0})", true});
  cases.push_back({input, R"({"e":115.7})", true});
  cases.push_back({input, R"({"e":115.70})", true});
  cases.push_back({input, R"({"e":1.157E2})", true});

  // Test string values.
  cases.push_back({input, R"({"f":1.0})", false});
  cases.push_back({input, R"({"f":1.00})", false});
  cases.push_back({input, R"({"f":"1.0"})", true});
  cases.push_back({input, R"({"f":"1"})", false});

  // Test nested objects.
  cases.push_back({input, R"({"g":{}})", true});
  cases.push_back({input, R"({"g":{"a":1}})", true});
  cases.push_back({input, R"({"g":{"aa":1}})", false});
  cases.push_back({input, R"({"g":{"b":2.0}})", true});
  cases.push_back({input, R"({"g":{"c":{}}})", true});
  cases.push_back({input, R"({"g":{"c":{"a": 1}}})", false});
  cases.push_back({input, R"({"g":{"c":{"d":2}}})", true});
  cases.push_back({input, R"({"g":{"c":{"e":3, "d":2.0}}})", true});
  cases.push_back({input, R"({"g":{"c":{"e":3, "a":1}}})", false});
  cases.push_back({input, R"({"g":{"c":{"e":3, "d":2.0, "a":1}}})", false});

  // Test multiple containment check.
  cases.push_back({input, R"({"b":null, "f":"1.0"})", true});
  cases.push_back({input, R"({"d":-10.0, "g":{"b":2.0}})", true});
  cases.push_back({input, R"({"d":-10.0, "g":{"b":1}})", false});

  //
  // Test empty keys, values, objects, and nulls.
  //
  input = R"({"":1, "a": "", "b":{}, "c":{"d":2}})";
  cases.push_back({input, R"({"":1})", true});
  cases.push_back({input, R"({"a":1})", false});
  cases.push_back({input, R"({"a":null})", false});
  cases.push_back({input, R"({"a":"null"})", false});
  cases.push_back({input, R"({"a":""})", true});

  cases.push_back({input, R"({})", true});
  cases.push_back({input, R"({"":{}})", false});
  cases.push_back({input, R"({"a":{}})", false});
  cases.push_back({input, R"({"b":null})", false});
  cases.push_back({input, R"({"b":{}})", true});
  cases.push_back({input, R"({"c":{}})", true});
  cases.push_back({input, R"({"c":{"d":{}}})", false});

  cases.push_back({R"({})", R"({})", true});
  cases.push_back({R"({})", R"(null)", false});

  // Numeric cases which are sensitive to compare_numbers_in_decimal.
  auto to_obj = [](const char* s) { return absl::StrCat(R"({"n":)", s, "}"); };
  cases.push_back({to_obj(kRoundedDouble), to_obj(kRoundedDoubleToIntBinary),
                   true, /*compare_numbers_in_decimal=*/false});
  cases.push_back({to_obj(kRoundedDouble), to_obj(kRoundedDoubleToIntBinary),
                   false, /*compare_numbers_in_decimal=*/true});
  cases.push_back({to_obj(kRoundedDouble), to_obj(kRoundedDoubleToIntDecimal),
                   false, /*compare_numbers_in_decimal=*/false});
  cases.push_back({to_obj(kRoundedDouble), to_obj(kRoundedDoubleToIntDecimal),
                   true, /*compare_numbers_in_decimal=*/true});

  TestJsonContainsFn(cases);
}

TEST(JsonContainsTest, JsonObjectWithArrayValue) {
  std::vector<JsonContainsTestCase> cases;
  //
  // Test JSON array with scalar values and nested arrays.
  //
  std::string input = R"({"a":[1,2,[3,4,[5]],[6,7]]})";

  // JSON object needs to match key first.
  cases.push_back({input, R"(1)", false});

  // Test empty array.
  cases.push_back({input, R"({"a":[]})", true});
  cases.push_back({input, R"({"a":[[]]})", true});
  cases.push_back({input, R"({"a":[[[]]]})", true});
  cases.push_back({input, R"({"a":[[[[]]]]})", false});

  // Test scalar values.
  cases.push_back({input, R"({"a":1})", false});
  cases.push_back({input, R"({"a":2})", false});
  cases.push_back({input, R"({"a":3})", false});
  cases.push_back({input, R"({"a":5})", false});
  cases.push_back({input, R"({"a":6})", false});
  cases.push_back({input, R"({"a":null})", false});

  // Test array values.
  cases.push_back({input, R"({"a":[1]})", true});
  cases.push_back({input, R"({"a":[1,1]})", true});
  cases.push_back({input, R"({"a":[3]})", false});
  cases.push_back({input, R"({"a":[[3]]})", true});
  cases.push_back({input, R"({"a":[[3,4,3]]})", true});
  cases.push_back({input, R"({"a":[[3,4,5]]})", false});
  cases.push_back({input, R"({"a":[[3,4,6]]})", false});
  cases.push_back({input, R"({"a":[5]})", false});
  cases.push_back({input, R"({"a":[[5]]})", false});
  cases.push_back({input, R"({"a":[[[5]]]})", true});
  cases.push_back({input, R"({"a":[1,[[5]]]})", true});
  cases.push_back({input, R"({"a":[[3,[5]]]})", true});
  cases.push_back({input, R"({"a":[[6,[5]]]})", false});
  cases.push_back({input, R"({"a":[[6],[[5]]]})", true});
  cases.push_back({input, R"({"a":[[7,4]]})", false});
  cases.push_back({input, R"({"a":[[7,6]]})", true});

  // Test empty object values.
  cases.push_back({input, R"({})", true});
  cases.push_back({input, R"({"a":{}})", false});
  cases.push_back({input, R"({"a":1})", false});

  //
  // Test JSON array with nested objects.
  //
  input = R"({"k":[{"a":1,"b":2},
                   [{"a":1,"b":3},{"c":[3,4]},{"d":5}],
                   {"a":1,"b":4,"c":5,"d":{"e":6,"f":7}}
                  ]})";

  cases.push_back({input, R"({})", true});
  cases.push_back({input, R"([])", false});
  cases.push_back({input, R"({"k":[]})", true});
  cases.push_back({input, R"({"k":[{}]})", true});
  cases.push_back({input, R"({"k":[[]]})", true});

  cases.push_back({input, R"({"k":[{"a":1}]})", true});
  cases.push_back({input, R"({"k":[{"b":2}]})", true});
  cases.push_back({input, R"({"k":[{"b":2, "c":5}]})", false});
  cases.push_back({input, R"({"k":[{"b":4, "c":5}]})", true});

  // Test nested array.
  cases.push_back({input, R"({"k":[[{"a":1}]]})", true});
  cases.push_back({input, R"({"k":[[{"a":2}]]})", false});
  cases.push_back({input, R"({"k":[[{"a":1},{"b":3}]]})", true});
  cases.push_back({input, R"({"k":[[{"a":1,"b":3}]]})", true});
  cases.push_back({input, R"({"k":[[{"a":1},{"d":5}]]})", true});
  cases.push_back({input, R"({"k":[[{"a":1,"d":5}]]})", false});

  cases.push_back({input, R"({"k":[[{"c":3}]]})", false});
  cases.push_back({input, R"({"k":[[{"c":[]}]]})", true});
  cases.push_back({input, R"({"k":[[{"d":[]}]]})", false});
  cases.push_back({input, R"({"k":[[{"c":[3]}]]})", true});
  cases.push_back({input, R"({"k":[[{"c":[3,3]}]]})", true});
  cases.push_back({input, R"({"k":[[{"c":[3,5]}]]})", false});

  // Test nested objects.
  cases.push_back({input, R"({"k":[{"d":[]}]})", false});
  cases.push_back({input, R"({"k":[{"d":{}}]})", true});
  cases.push_back({input, R"({"k":[{"d":{"e":6}}]})", true});
  cases.push_back({input, R"({"k":[{"d":{"e":6, "b":2}}]})", false});
  cases.push_back({input, R"({"k":[{"d":{"e":6}}, {"b":2}]})", true});
  cases.push_back({input, R"({"k":[{"d":{"e":6}}, {"b":3}]})", false});
  cases.push_back({input, R"({"k":[{"d":{"e":6, "b":4}}]})", false});
  cases.push_back({input, R"({"k":[{"d":{"e":6}, "b":4}]})", true});
  cases.push_back({input, R"({"k":[{"d":{"e":6}}, {"b":4}]})", true});
  cases.push_back({input, R"({"k":[{"d":{"e":6,"f":7}}]})", true});

  TestJsonContainsFn(cases);
}

class JsonKeysTest
    : public ::testing::TestWithParam<
          std::tuple<std::tuple<std::string, std::vector<std::string>, int64_t>,
                     JsonPathOptions>> {
 protected:
  absl::string_view GetJSONDoc() const {
    return std::get<0>(std::get<0>(GetParam()));
  }
  std::vector<std::string> GetExpectedResult() const {
    return std::get<1>(std::get<0>(GetParam()));
  }

  int64_t GetMaxDepth() const { return std::get<2>(std::get<0>(GetParam())); }

  JsonPathOptions GetJsonPathOptions() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    JsonKeysObjectTests, JsonKeysTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_tuple("null", std::vector<std::string>(), INT64_MAX),
            std::make_tuple("1", std::vector<std::string>(), INT64_MAX),
            std::make_tuple("[1, 2]", std::vector<std::string>(), INT64_MAX),
            std::make_tuple("[1, [2]]", std::vector<std::string>(), INT64_MAX),
            std::make_tuple(R"({"a": 1})", std::vector<std::string>{"a"},
                            INT64_MAX),
            // Tests escaping key.
            std::make_tuple(R"({"a.b": 1})",
                            std::vector<std::string>{R"("a.b")"}, 1),
            std::make_tuple(R"({"a.b": {"c":1}})",
                            std::vector<std::string>{R"("a.b")", R"("a.b".c)"},
                            2),
            std::make_tuple(
                R"({"a.b": {"c\"":1}})",
                std::vector<std::string>{R"("a.b")", R"("a.b"."c\"")"}, 2),
            std::make_tuple(R"({"a.b": {"c\"":1, "d.e":2}})",
                            std::vector<std::string>{
                                R"("a.b")",
                                R"("a.b"."c\"")",
                                R"("a.b"."d.e")",
                            },
                            2),
            std::make_tuple(R"({"a": {"b":1}})", std::vector<std::string>{"a"},
                            1),
            std::make_tuple(R"({"a": {"b":1}})",
                            std::vector<std::string>{"a", "a.b"}, INT64_MAX),
            std::make_tuple(R"({"a": {"c":1, "b":1}})",
                            std::vector<std::string>{"a", "a.b", "a.c"}, 2),
            // Keys should be returned in sorted order.
            std::make_tuple(
                R"({"a": {"c":{"d":1, "e":1}, "b":1, "a":{"a":1}}})",
                std::vector<std::string>{"a", "a.a", "a.a.a", "a.b", "a.c",
                                         "a.c.d", "a.c.e"},
                INT64_MAX),
            std::make_tuple(
                R"({"a": {"c":{"d":1, "e":1}, "b":1, "a":{"a":1}}})",
                std::vector<std::string>{"a", "a.a", "a.b", "a.c"}, 2)),
        ::testing::Values(JsonPathOptions::kStrict, JsonPathOptions::kLax,
                          JsonPathOptions::kLaxRecursive)));

// Tests arrays are correctly unwrapped when `lax` is enabled.
INSTANTIATE_TEST_SUITE_P(
    JsonKeysSingleArrayOnlyTests, JsonKeysTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_tuple(R"({"a": [{"b":1}, {"c":2}]})",
                            std::vector<std::string>{"a", "a.b", "a.c"},
                            INT64_MAX),
            std::make_tuple(R"({"a": [{"b":1}, {"c":2}]})",
                            std::vector<std::string>{"a"}, 1),
            std::make_tuple(R"([{"a":1}, {"b":1}, {"c":2}, "d"])",
                            std::vector<std::string>({"a", "b", "c"}), 1)),
        ::testing::Values(JsonPathOptions::kLax,
                          JsonPathOptions::kLaxRecursive)));

// Tests nested array are correctly unwrapped when `lax` and `recursive`
// behavior is enabled.
INSTANTIATE_TEST_SUITE_P(
    JsonKeysNestedArrayOnlyTests, JsonKeysTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_tuple(R"([1, {"a": 1}, [{"b": 2}]])",
                            std::vector<std::string>{"a", "b"}, 3),
            std::make_tuple(
                R"({"a": [[{"b":1}, {"b":2}], {"c":2}, {"b":{"c":3}}]})",
                std::vector<std::string>{"a", "a.b", "a.b.c", "a.c"}, 3),
            std::make_tuple(
                R"({"a": [[{"b":1}, {"b":2}], [[[{"c":2}]]], {"b":{"c":3}}]})",
                std::vector<std::string>{"a", "a.b", "a.c"}, 2),
            std::make_tuple(R"({"b":2, "a": [[{"b":1}, 2, "value"], {"c":2}]})",
                            std::vector<std::string>{"a", "a.b", "a.c", "b"},
                            2)),
        ::testing::Values(JsonPathOptions::kLaxRecursive)));

// Tests when `lax` is enabled but `recursive` is not, doesn't unwrap nested
// arrays.
INSTANTIATE_TEST_SUITE_P(
    JsonKeysLaxPositiveRecursiveNegative, JsonKeysTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_tuple(R"([[{"a":1}]])", std::vector<std::string>{},
                            INT64_MAX),
            std::make_tuple(R"([1, {"a": 1}, [{"b": 2}]])",
                            std::vector<std::string>{"a"}, INT64_MAX),
            std::make_tuple(
                R"([1, {"a": 1}, {"c": [{"d":2}, [{"e":3}]]}, [{"b": 2}]])",
                std::vector<std::string>{"a", "c", "c.d"}, INT64_MAX)),
        ::testing::Values(JsonPathOptions::kLax)));

// Tests that when lax behavior isn't enabled doesn't unwrap arrays.
INSTANTIATE_TEST_SUITE_P(
    JsonKeysLaxRecursiveNegative, JsonKeysTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_tuple(R"([{"a":1}])", std::vector<std::string>{},
                            INT64_MAX),
            std::make_tuple(R"([[{"a":1}]])", std::vector<std::string>{},
                            INT64_MAX),
            std::make_tuple(R"([1, [{"a": 1}], [{"c": 1}], [[{"b": 2}]]])",
                            std::vector<std::string>{}, INT64_MAX)),
        ::testing::Values(JsonPathOptions::kStrict)));

TEST_P(JsonKeysTest, Success) {
  JSONValue value = JSONValue::ParseJSONString(GetJSONDoc()).value();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::string> results,
      JsonKeys(value.GetRef(), {.path_options = GetJsonPathOptions(),
                                .max_depth = GetMaxDepth()}));
  EXPECT_THAT(results, ElementsAreArray(GetExpectedResult()));
}

TEST(JsonKeysInvalidTest, Invalid) {
  JSONValue value = JSONValue::ParseJSONString(R"({"a": 1})").value();
  EXPECT_THAT(JsonKeys(value.GetRef(), {.max_depth = -1}),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("max_depth must be positive")));
  EXPECT_THAT(JsonKeys(value.GetRef(), {.max_depth = 0}),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("max_depth must be positive")));
}

TEST(JsonKeysMemoryLimitTest, EnforcesReserveMemoryCallback) {
  const std::string outer_key(1024, 'A');
  std::string json_str = absl::StrCat("{\"", outer_key, "\":[{");
  for (int i = 0; i < 50; ++i) {
    if (i > 0) json_str.push_back(',');
    absl::StrAppend(&json_str, "\"k", i, "\":1");
  }
  json_str.append("}]}");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue value, JSONValue::ParseJSONString(json_str));

  constexpr size_t kSmallBudgetBytes = 8 * 1024;
  for (JsonPathOptions path_options :
       {JsonPathOptions::kLax, JsonPathOptions::kLaxRecursive}) {
    size_t reserved_bytes = 0;
    EXPECT_THAT(
        JsonKeys(value.GetConstRef(),
                 {.path_options = path_options,
                  .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                    if (reserved_bytes + bytes > kSmallBudgetBytes) {
                      return absl::ResourceExhaustedError(
                          "Memory limit exceeded");
                    }
                    reserved_bytes += bytes;
                    return absl::OkStatus();
                  }}),
        StatusIs(absl::StatusCode::kResourceExhausted,
                 HasSubstr("Memory limit exceeded")));
    EXPECT_LE(reserved_bytes, kSmallBudgetBytes);
  }

  size_t reserved_bytes = 0;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::string> keys,
      JsonKeys(value.GetConstRef(),
               {.path_options = JsonPathOptions::kLax,
                .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                  reserved_bytes += bytes;
                  return absl::OkStatus();
                }}));
  EXPECT_EQ(keys.size(), 51);
  EXPECT_GT(reserved_bytes, 50 * 1024);
}

TEST(JsonKeysMemoryLimitTest, SkipsDuplicateKeysAndEmptySubtrees) {
  for (absl::string_view empty_doc : {"{}", "[]"}) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue empty_val,
                         JSONValue::ParseJSONString(empty_doc));
    size_t reserved_bytes = 0;
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::vector<std::string> keys,
        JsonKeys(empty_val.GetConstRef(),
                 {.path_options = JsonPathOptions::kLaxRecursive,
                  .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                    reserved_bytes += bytes;
                    return absl::OkStatus();
                  }}));
    EXPECT_THAT(keys, IsEmpty());
    EXPECT_EQ(reserved_bytes, 0);
  }

  std::string json_str = R"({"items":[{"a":1,"empty_obj":{},"empty_arr":[]})";
  for (int i = 2; i <= 1000; ++i) {
    absl::StrAppend(&json_str, R"(,{"a":)", i, R"(},{},[])");
  }
  json_str.append("]}");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue value, JSONValue::ParseJSONString(json_str));

  // In kStrict mode, "items" is not unwrapped and only reserves a single key
  // entry (plus the root stack element).
  size_t strict_reserved_bytes = 0;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::string> strict_keys,
      JsonKeys(value.GetConstRef(),
               {.path_options = JsonPathOptions::kStrict,
                .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                  strict_reserved_bytes += bytes;
                  return absl::OkStatus();
                }}));
  EXPECT_THAT(strict_keys, ElementsAre("items"));
  EXPECT_LT(strict_reserved_bytes, 256);

  // In kLaxRecursive mode, "items.a" is inserted into `keys_` only once across
  // 1,000 array elements, and empty objects/arrays are not pushed onto the
  // stack.
  size_t single_element_bytes = 0;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue single_value,
      JSONValue::ParseJSONString(
          R"({"items":[{"a":1,"empty_obj":{},"empty_arr":[]}]})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::string> single_keys,
      JsonKeys(single_value.GetConstRef(),
               {.path_options = JsonPathOptions::kLaxRecursive,
                .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                  single_element_bytes += bytes;
                  return absl::OkStatus();
                }}));
  EXPECT_THAT(single_keys, ElementsAre("items", "items.a", "items.empty_arr",
                                       "items.empty_obj"));

  size_t without_empty_children_bytes = 0;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue without_empty_children,
                       JSONValue::ParseJSONString(R"({"items":[{"a":1}]})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::string> without_empty_keys,
      JsonKeys(without_empty_children.GetConstRef(),
               {.path_options = JsonPathOptions::kLaxRecursive,
                .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                  without_empty_children_bytes += bytes;
                  return absl::OkStatus();
                }}));
  // "items.empty_obj" and "items.empty_arr" only reserve `keys_` entries (2 *
  // (sizeof(std::string) + 15)), not StackElement entries.
  EXPECT_EQ(single_element_bytes - without_empty_children_bytes,
            2 * (sizeof(std::string) + 15));

  // With 1,000 elements where 999 repeat {"a": i} (plus 999 empty {} and []),
  // only the 999 non-empty object StackElements are reserved beyond the first
  // element; "items.a" is not re-reserved in `keys_`.
  size_t lax_reserved_bytes = 0;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::string> lax_keys,
      JsonKeys(value.GetConstRef(),
               {.path_options = JsonPathOptions::kLaxRecursive,
                .reserve_memory_callback = [&](size_t bytes) -> absl::Status {
                  lax_reserved_bytes += bytes;
                  return absl::OkStatus();
                }}));
  EXPECT_THAT(lax_keys, ElementsAre("items", "items.a", "items.empty_arr",
                                    "items.empty_obj"));
  EXPECT_LT(lax_reserved_bytes - single_element_bytes,
            999 * (sizeof(std::string) + 64));
}

class JsonFlattenTest : public ::testing::TestWithParam<
                            std::pair<std::string, std::vector<std::string>>> {
 protected:
  // Create a vector of `JSONValueConstRef`s from a vector of `JSONValue`s.
  // The original values must outlive the references.
  static std::vector<JSONValueConstRef> VectorOfRefs(
      absl::Span<const JSONValue> values) {
    std::vector<JSONValueConstRef> refs;
    refs.reserve(values.size());
    for (const JSONValue& value : values) {
      refs.push_back(value.GetConstRef());
    }
    return refs;
  }

  JsonFlattenTest() {
    input_ = JSONValue::ParseJSONString(GetParam().first).value();
    expected_.reserve(GetParam().second.size());
    for (absl::string_view s : GetParam().second) {
      expected_.push_back(JSONValue::ParseJSONString(s).value());
    }
  }

  JSONValueConstRef GetJsonInput() const { return input_.GetConstRef(); }

  std::vector<JSONValueConstRef> GetExpected() const {
    return VectorOfRefs(expected_);
  }

 private:
  JSONValue input_;
  std::vector<JSONValue> expected_;
};

INSTANTIATE_TEST_SUITE_P(
    JsonFlattenTestParameterized, JsonFlattenTest,
    ::testing::Values(
        std::make_tuple("null", std::vector<std::string>{"null"}),
        std::make_tuple("[]", std::vector<std::string>{}),
        std::make_tuple("[[]]", std::vector<std::string>{}),
        std::make_tuple("10", std::vector<std::string>{"10"}),
        std::make_tuple(R"({"foo": 10})",
                        std::vector<std::string>{R"({"foo":10})"}),
        std::make_tuple("[10, null]", std::vector<std::string>{"10", "null"}),
        std::make_tuple("[10, 20, 30]",
                        std::vector<std::string>{"10", "20", "30"}),
        std::make_tuple(R"([[11, "foo", {"f": 123}], true])",
                        std::vector<std::string>{R"(11)", R"("foo")",
                                                 R"({"f":123})", "true"}),
        std::make_tuple(R"([[[1, 2], 3], 4])",
                        std::vector<std::string>{"1", "2", "3", "4"}),
        std::make_tuple(R"([[10, 20], {"foo": [20, 30]}])",
                        std::vector<std::string>{"10", "20",
                                                 R"({"foo":[20,30]})"}),
        std::make_tuple(R"([[[[1, 2], 3], 4], {"a": 10}, ["foo", true]])",
                        std::vector<std::string>{"1", "2", "3", "4",
                                                 R"({"a":10})", R"("foo")",
                                                 "true"})));

TEST_P(JsonFlattenTest, Success) {
  EXPECT_THAT(JsonFlatten(GetJsonInput()), Pointwise(JsonEq(), GetExpected()));
}

absl::StatusOr<std::unique_ptr<JsonPathEvaluator>> CreateJsonPathEvaluator(
    absl::string_view json_path) {
  return JsonPathEvaluator::Create(
      json_path, /*sql_standard_mode=*/true,
      /*enable_special_character_escaping_in_values=*/true,
      /*enable_special_character_escaping_in_keys=*/true);
}

TEST(JsonExistsTest, Basic) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue val,
      JSONValue::ParseJSONString(R"({"a": 1, "b": null, "c": [1, 2]})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_a, CreateJsonPathEvaluator("$.a"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_b, CreateJsonPathEvaluator("$.b"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_c0, CreateJsonPathEvaluator("$.c[0]"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_d, CreateJsonPathEvaluator("$.d"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_c2, CreateJsonPathEvaluator("$.c[2]"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_root, CreateJsonPathEvaluator("$"));

  // Existing keys
  EXPECT_THAT(JsonExists(val.GetConstRef(), *eval_a), IsOkAndHolds(true));

  // Existing key with null value
  EXPECT_THAT(JsonExists(val.GetConstRef(), *eval_b), IsOkAndHolds(true));

  // Existing array index
  EXPECT_THAT(JsonExists(val.GetConstRef(), *eval_c0), IsOkAndHolds(true));

  // Non-existing key
  EXPECT_THAT(JsonExists(val.GetConstRef(), *eval_d), IsOkAndHolds(false));

  // Non-existing array index
  EXPECT_THAT(JsonExists(val.GetConstRef(), *eval_c2), IsOkAndHolds(false));

  // Root path
  EXPECT_THAT(JsonExists(val.GetConstRef(), *eval_root), IsOkAndHolds(true));

  // Root path on JSON null
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue null_val, JSONValue::ParseJSONString("null"));
  EXPECT_THAT(JsonExists(null_val.GetConstRef(), *eval_root),
              IsOkAndHolds(true));

  // Invalid JSONPath syntax returns error during evaluator creation
  EXPECT_FALSE(CreateJsonPathEvaluator("invalid").ok());
  EXPECT_FALSE(CreateJsonPathEvaluator("").ok());
}

TEST(JsonExistsAnyTest, Basic) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue val,
      JSONValue::ParseJSONString(R"({"a": 1, "b": null, "c": [1, 2]})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_a, CreateJsonPathEvaluator("$.a"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_b, CreateJsonPathEvaluator("$.b"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_d, CreateJsonPathEvaluator("$.d"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_e, CreateJsonPathEvaluator("$.e"));

  // Any of [existing, non-existing] -> true
  std::vector<const JsonPathEvaluator*> evals1 = {eval_a.get(), eval_d.get()};
  EXPECT_THAT(JsonExistsAny(val.GetConstRef(), evals1), IsOkAndHolds(true));

  // Any of [non-existing, non-existing] -> false
  std::vector<const JsonPathEvaluator*> evals2 = {eval_d.get(), eval_e.get()};
  EXPECT_THAT(JsonExistsAny(val.GetConstRef(), evals2), IsOkAndHolds(false));

  // Any of [existing (null value)] -> true
  std::vector<const JsonPathEvaluator*> evals3 = {eval_b.get()};
  EXPECT_THAT(JsonExistsAny(val.GetConstRef(), evals3), IsOkAndHolds(true));

  // Empty evaluators -> false
  std::vector<const JsonPathEvaluator*> evals_empty;
  EXPECT_THAT(JsonExistsAny(val.GetConstRef(), evals_empty),
              IsOkAndHolds(false));
}

TEST(JsonExistsAllTest, Basic) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue val,
      JSONValue::ParseJSONString(R"({"a": 1, "b": null, "c": [1, 2]})"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_a, CreateJsonPathEvaluator("$.a"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_b, CreateJsonPathEvaluator("$.b"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_d, CreateJsonPathEvaluator("$.d"));

  // All of [existing, existing] -> true
  std::vector<const JsonPathEvaluator*> evals1 = {eval_a.get(), eval_b.get()};
  EXPECT_THAT(JsonExistsAll(val.GetConstRef(), evals1), IsOkAndHolds(true));

  // All of [existing, non-existing] -> false
  std::vector<const JsonPathEvaluator*> evals2 = {eval_a.get(), eval_d.get()};
  EXPECT_THAT(JsonExistsAll(val.GetConstRef(), evals2), IsOkAndHolds(false));

  // Empty evaluators -> true
  std::vector<const JsonPathEvaluator*> evals_empty;
  EXPECT_THAT(JsonExistsAll(val.GetConstRef(), evals_empty),
              IsOkAndHolds(true));
}

absl::StatusOr<ParsedJsonPath> ParseStandardJsonPathWithWildcards(
    absl::string_view path) {
  return ParseJsonPath(path, /*sql_standard_mode=*/true,
                       /*enable_wildcard_and_descendant=*/true);
}

absl::Status IsValidStandardJsonPathWithWildcards(absl::string_view path) {
  return IsValidJSONPath(path, /*sql_standard_mode=*/true,
                         /*enable_wildcard_and_descendant=*/true);
}

absl::Status IsValidLegacyJsonPath(
    absl::string_view path, bool enable_wildcard_and_descendant = false) {
  return IsValidJSONPath(path, /*sql_standard_mode=*/false,
                         enable_wildcard_and_descendant);
}

absl::StatusOr<std::unique_ptr<JsonPathEvaluator>>
CreateStandardEvaluatorWithWildcards(absl::string_view path) {
  return JsonPathEvaluator::Create(
      path, /*sql_standard_mode=*/true,
      /*enable_special_character_escaping_in_values=*/false,
      /*enable_special_character_escaping_in_keys=*/false,
      /*enable_wildcard_and_descendant=*/true);
}

TEST(JsonWildcardsAndDescendantsTest, ArrayAndObjectWildcards) {
  const std::string json_str =
      R"({"store": {"book": [{"price": 8.95, "title": "A"}, )"
      R"({"price": 12.99, "title": "B"}], "bicycle": {"price": 19.95}}})";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto json, JSONValue::ParseJSONString(json_str));
  JSONValueConstRef ref = json.GetConstRef();

  // Multi-match traversal via EvaluateJsonPath (k=3
  // matches: 19.95, 8.95, 12.99)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_price,
                       ParseStandardJsonPathWithWildcards("$..price"));
  std::vector<JSONValueConstRef> matches_price =
      EvaluateJsonPath(path_price, ref);
  ASSERT_EQ(matches_price.size(), 3);
  EXPECT_EQ(matches_price[0].ToString(), "19.95");
  EXPECT_EQ(matches_price[1].ToString(), "8.95");
  EXPECT_EQ(matches_price[2].ToString(), "12.99");

  // Predicate evaluation via ExtractExists and JsonExists (k=3 >= 1 -> true)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_price,
                       CreateStandardEvaluatorWithWildcards("$..price"));
  EXPECT_TRUE(eval_price->ExtractExists(ref));
  EXPECT_THAT(JsonExists(ref, *eval_price), IsOkAndHolds(true));
  std::vector<const JsonPathEvaluator*> evals = {eval_price.get()};
  EXPECT_THAT(JsonExistsAny(ref, evals), IsOkAndHolds(true));
  EXPECT_THAT(JsonExistsAll(ref, evals), IsOkAndHolds(true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_missing,
                       CreateStandardEvaluatorWithWildcards("$..isbn"));
  EXPECT_FALSE(eval_missing->ExtractExists(ref));
  EXPECT_THAT(JsonExists(ref, *eval_missing), IsOkAndHolds(false));

  // Default behavior (NULL ON ERROR) when k > 1 matches for JSON_QUERY* styles:
  // Extract(JSONValueConstRef) methods return std::nullopt.
  EXPECT_FALSE(eval_price->Extract(ref).has_value());
  EXPECT_FALSE(eval_price->ExtractScalar(ref).has_value());
  EXPECT_FALSE(eval_price->ExtractArray(ref).has_value());
  EXPECT_FALSE(eval_price->ExtractStringArray(ref).has_value());

  // For k = 0, Extract(JSONValueConstRef) methods return std::nullopt.
  EXPECT_FALSE(eval_missing->Extract(ref).has_value());
  EXPECT_FALSE(eval_missing->ExtractScalar(ref).has_value());
  EXPECT_FALSE(eval_missing->ExtractArray(ref).has_value());
  EXPECT_FALSE(eval_missing->ExtractStringArray(ref).has_value());

  // JSON_QUERY with $.store.bicycle (k=1 -> {"price": 19.95})
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_bicycle,
                       CreateStandardEvaluatorWithWildcards("$.store.bicycle"));
  auto res_bicycle = eval_bicycle->Extract(ref);
  ASSERT_TRUE(res_bicycle.has_value());
  EXPECT_EQ(res_bicycle->ToString(), "{\"price\":19.95}");

  // Single-match across hierarchy: $..bicycle.price (k=1 -> 19.95)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_single, CreateStandardEvaluatorWithWildcards(
                                             "$..bicycle.price"));
  auto res_single = eval_single->Extract(ref);
  ASSERT_TRUE(res_single.has_value());
  EXPECT_EQ(res_single->ToString(), "19.95");

  auto res_single_val = eval_single->ExtractScalar(ref);
  ASSERT_TRUE(res_single_val.has_value());
  EXPECT_EQ(*res_single_val, "19.95");

  // Single JSON array extraction via descendant path: $..book (k=1 array -> 2
  // elements).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_books,
                       CreateStandardEvaluatorWithWildcards("$..book"));
  auto books_arr = eval_books->ExtractArray(ref);
  ASSERT_TRUE(books_arr.has_value());
  EXPECT_EQ(books_arr->size(), 2);
  EXPECT_EQ((*books_arr)[0].ToString(), R"({"price":8.95,"title":"A"})");
  EXPECT_EQ((*books_arr)[1].ToString(), R"({"price":12.99,"title":"B"})");

  // Single array of scalars extraction via descendant path: $..tags (k=1 -> 2
  // elements).
  const std::string tags_json_str = R"({"tags": ["fiction", "bestseller"]})";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto tags_json,
                       JSONValue::ParseJSONString(tags_json_str));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_tags,
                       CreateStandardEvaluatorWithWildcards("$..tags"));
  auto tags_str_arr = eval_tags->ExtractStringArray(tags_json.GetConstRef());
  ASSERT_TRUE(tags_str_arr.has_value());
  EXPECT_THAT(*tags_str_arr,
              ::testing::ElementsAre(std::optional("fiction"),
                                     std::optional("bestseller")));
}

TEST(JsonWildcardsAndDescendantsTest, WildcardContainerSpecificity) {
  // Object wildcard $.* on {"a": 1, "b": 2} (k=2 > 1 -> default NULL ON ERROR,
  // exists true)
  const std::string simple_obj_str = R"({"a": 1, "b": 2})";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto simple_obj,
                       JSONValue::ParseJSONString(simple_obj_str));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_obj_wild,
                       CreateStandardEvaluatorWithWildcards("$.*"));
  EXPECT_FALSE(eval_obj_wild->Extract(simple_obj.GetConstRef()).has_value());
  EXPECT_FALSE(
      eval_obj_wild->ExtractScalar(simple_obj.GetConstRef()).has_value());
  EXPECT_FALSE(
      eval_obj_wild->ExtractArray(simple_obj.GetConstRef()).has_value());
  EXPECT_FALSE(
      eval_obj_wild->ExtractStringArray(simple_obj.GetConstRef()).has_value());
  EXPECT_TRUE(eval_obj_wild->ExtractExists(simple_obj.GetConstRef()));

  // Array wildcard $[*] on [10, 20, 30] (k=3 > 1 -> default NULL ON ERROR,
  // exists true)
  const std::string simple_arr_str = R"([10, 20, 30])";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto simple_arr,
                       JSONValue::ParseJSONString(simple_arr_str));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_arr_wild,
                       CreateStandardEvaluatorWithWildcards("$[*]"));
  EXPECT_FALSE(eval_arr_wild->Extract(simple_arr.GetConstRef()).has_value());
  EXPECT_FALSE(
      eval_arr_wild->ExtractScalar(simple_arr.GetConstRef()).has_value());
  EXPECT_FALSE(
      eval_arr_wild->ExtractArray(simple_arr.GetConstRef()).has_value());
  EXPECT_FALSE(
      eval_arr_wild->ExtractStringArray(simple_arr.GetConstRef()).has_value());
  EXPECT_TRUE(eval_arr_wild->ExtractExists(simple_arr.GetConstRef()));

  // Container specificity: $[*] does NOT match object members, and $.* does NOT
  // match array elements.
  EXPECT_FALSE(eval_arr_wild->ExtractExists(simple_obj.GetConstRef()));
  EXPECT_FALSE(eval_obj_wild->ExtractExists(simple_arr.GetConstRef()));
}

TEST(JsonWildcardsAndDescendantsTest, StringInputIsNotSupported) {
  // Wildcard and recursive descendant operators are only supported for JSON
  // inputs. For STRING inputs, all `Extract*` methods return OUT_OF_RANGE,
  // whether the JSONPath has no match, a single match or multiple matches, and
  // even if the input is malformed or nested too deeply.
  const auto string_input_error =
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("JSONPath wildcard and recursive descendant operators "
                         "are not supported for STRING input"));
  const std::string deeply_nested(JSONPathExtractor::kMaxParsingDepth + 1, '[');
  for (absl::string_view path : {"$.*", "$[*]", "$..a", "$.a[*]"}) {
    SCOPED_TRACE(path);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval, CreateStandardEvaluatorWithWildcards(path));
    for (absl::string_view json : std::vector<absl::string_view>{
             R"({"a": [1]})", "[1, 2]", "{invalid_json", deeply_nested}) {
      std::string value;
      std::vector<std::string> array_value;
      std::vector<std::optional<std::string>> string_array_value;
      bool is_null;
      EXPECT_THAT(eval->Extract(json, &value, &is_null), string_input_error);
      EXPECT_THAT(eval->ExtractScalar(json, &value, &is_null),
                  string_input_error);
      EXPECT_THAT(eval->ExtractArray(json, &array_value, &is_null),
                  string_input_error);
      EXPECT_THAT(eval->ExtractStringArray(json, &string_array_value, &is_null),
                  string_input_error);
    }
  }

  // JSONPaths without these operators still support STRING inputs.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval, CreateStandardEvaluatorWithWildcards("$.a"));
  std::string value;
  bool is_null = true;
  GOOGLESQL_ASSERT_OK(eval->Extract(R"({"a": 1.10})", &value, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(value, "1.10");
}

TEST(JsonWildcardsAndDescendantsTest, LaxModeInteractions) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto json,
                       JSONValue::ParseJSONString(
                           R"({"arr": [{"a": 1}, {"a": 2}], "scalar": 10})"));
  JSONValueConstRef ref = json.GetConstRef();

  // $.arr.* (default mode) -> 0 matches (arr is array, dot wildcard expects
  // object)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_default_dot,
                       ParseStandardJsonPathWithWildcards("$.arr.*"));
  std::vector<JSONValueConstRef> res_default_dot =
      EvaluateJsonPath(path_default_dot, ref);
  EXPECT_TRUE(res_default_dot.empty());

  // Negative tests: Lax mode does NOT support wildcard or descendant operators.
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $.arr.*", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $.scalar[*]", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $.*", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $[*]", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $..a", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax recursive $.*", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax recursive $[*]", true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax recursive $..b", true),
              StatusIs(absl::StatusCode::kOutOfRange));

  // Negative tests via IsValidJSONPathStrict
  EXPECT_THAT(IsValidJSONPathStrict("lax $.*", /*enable_lax_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(IsValidJSONPathStrict("lax $..a", /*enable_lax_mode=*/true),
              StatusIs(absl::StatusCode::kOutOfRange));

  // Negative tests via StrictJSONPathIterator
  EXPECT_THAT(
      StrictJSONPathIterator::Create("lax $.*", /*enable_lax_mode=*/true),
      StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(
      StrictJSONPathIterator::Create("lax $..a", /*enable_lax_mode=*/true),
      StatusIs(absl::StatusCode::kOutOfRange));

  // Explicit 'strict' keyword is not recognized and is rejected
  EXPECT_THAT(IsValidAndLaxJSONPath("strict $.arr.*", true),
              StatusIs(absl::StatusCode::kOutOfRange));

  // Valid lax path without wildcards/descendants succeeds
  EXPECT_THAT(IsValidAndLaxJSONPath("lax $.arr", true), IsOkAndHolds(true));
  EXPECT_THAT(IsValidAndLaxJSONPath("lax recursive $.arr", true),
              IsOkAndHolds(true));
  EXPECT_THAT(IsValidAndLaxJSONPath("$.arr.*", true), IsOkAndHolds(false));
  EXPECT_THAT(IsValidAndLaxJSONPath("$..a", true), IsOkAndHolds(false));
}

TEST(JsonWildcardsAndDescendantsTest, RecursiveDescendantVariants) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto json, JSONValue::ParseJSONString(
                     R"({"a": {"b": [10, 20]}, "c": [{"b": [30, 40]}]})"));
  JSONValueConstRef ref = json.GetConstRef();

  // $..b (k=2 matches: [10, 20] and [30, 40])
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_b, ParseStandardJsonPathWithWildcards("$..b"));
  std::vector<JSONValueConstRef> matches_b = EvaluateJsonPath(path_b, ref);
  ASSERT_EQ(matches_b.size(), 2);
  EXPECT_EQ(matches_b[0].ToString(), "[10,20]");
  EXPECT_EQ(matches_b[1].ToString(), "[30,40]");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_b,
                       CreateStandardEvaluatorWithWildcards("$..b"));
  EXPECT_FALSE(eval_b->Extract(ref).has_value());
  EXPECT_FALSE(eval_b->ExtractArray(ref).has_value());
  EXPECT_TRUE(eval_b->ExtractExists(ref));

  // $..b[0] (k=2 matches: 10 and 30)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_b0,
                       ParseStandardJsonPathWithWildcards("$..b[0]"));
  std::vector<JSONValueConstRef> matches_b0 = EvaluateJsonPath(path_b0, ref);
  ASSERT_EQ(matches_b0.size(), 2);
  EXPECT_EQ(matches_b0[0].ToString(), "10");
  EXPECT_EQ(matches_b0[1].ToString(), "30");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_b0,
                       CreateStandardEvaluatorWithWildcards("$..b[0]"));
  EXPECT_FALSE(eval_b0->Extract(ref).has_value());
  EXPECT_FALSE(eval_b0->ExtractScalar(ref).has_value());
  EXPECT_TRUE(eval_b0->ExtractExists(ref));

  // $..[0] (k=3 matches: 10, {"b": [30, 40]}, 30)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_0,
                       ParseStandardJsonPathWithWildcards("$..[0]"));
  std::vector<JSONValueConstRef> matches_0 = EvaluateJsonPath(path_0, ref);
  ASSERT_EQ(matches_0.size(), 3);
  EXPECT_EQ(matches_0[0].ToString(), "10");
  EXPECT_EQ(matches_0[1].ToString(), R"({"b":[30,40]})");
  EXPECT_EQ(matches_0[2].ToString(), "30");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_0,
                       CreateStandardEvaluatorWithWildcards("$..[0]"));
  EXPECT_FALSE(eval_0->Extract(ref).has_value());
  EXPECT_FALSE(eval_0->ExtractArray(ref).has_value());
  EXPECT_TRUE(eval_0->ExtractExists(ref));

  // $..* (object wildcard descendant) matches 4 object member values:
  // {"b":[10,20]}, [{"b":[30,40]}], [10,20], [30,40]
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_obj_wild,
                       ParseStandardJsonPathWithWildcards("$..*"));
  std::vector<JSONValueConstRef> matches_obj_wild =
      EvaluateJsonPath(path_obj_wild, ref);
  ASSERT_EQ(matches_obj_wild.size(), 4);
  EXPECT_EQ(matches_obj_wild[0].ToString(), R"({"b":[10,20]})");
  EXPECT_EQ(matches_obj_wild[1].ToString(), R"([{"b":[30,40]}])");
  EXPECT_EQ(matches_obj_wild[2].ToString(), "[10,20]");
  EXPECT_EQ(matches_obj_wild[3].ToString(), "[30,40]");

  // $..[*] (array wildcard descendant) matches 5 array elements:
  // 10, 20, {"b":[30,40]}, 30, 40
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto path_sub_wild,
                       ParseStandardJsonPathWithWildcards("$..[*]"));
  std::vector<JSONValueConstRef> matches_sub_wild =
      EvaluateJsonPath(path_sub_wild, ref);
  ASSERT_EQ(matches_sub_wild.size(), 5);
  EXPECT_EQ(matches_sub_wild[0].ToString(), "10");
  EXPECT_EQ(matches_sub_wild[1].ToString(), "20");
  EXPECT_EQ(matches_sub_wild[2].ToString(), R"({"b":[30,40]})");
  EXPECT_EQ(matches_sub_wild[3].ToString(), "30");
  EXPECT_EQ(matches_sub_wild[4].ToString(), "40");

  // Container specificity: $..[*] on {"user": {"tags": []}} matches 0 elements
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto empty_tags_json,
                       JSONValue::ParseJSONString(R"({"user": {"tags": []}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_sub_wild,
                       CreateStandardEvaluatorWithWildcards("$..[*]"));
  EXPECT_FALSE(eval_sub_wild->ExtractExists(empty_tags_json.GetConstRef()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_any_wild,
                       CreateStandardEvaluatorWithWildcards("$..*"));
  EXPECT_FALSE(eval_any_wild->Extract(ref).has_value());
  EXPECT_FALSE(eval_any_wild->ExtractArray(ref).has_value());
  EXPECT_TRUE(eval_any_wild->ExtractExists(ref));
}

TEST(JsonWildcardsAndDescendantsTest,
     NestedNullSortedMemberOrderAndEmptyContainers) {
  // 1. Nested match: `$..a` on `{"a": {"a": 1}}` visits outer match first,
  // then inner match in depth-first pre-order.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue nested_a_json,
                       JSONValue::ParseJSONString(R"({"a": {"a": 1}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath path_desc_a,
                       ParseStandardJsonPathWithWildcards("$..a"));
  std::vector<JSONValueConstRef> nested_matches =
      EvaluateJsonPath(path_desc_a, nested_a_json.GetConstRef());
  ASSERT_EQ(nested_matches.size(), 2);
  EXPECT_EQ(nested_matches[0].ToString(), R"({"a":1})");
  EXPECT_EQ(nested_matches[1].ToString(), "1");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_desc_a,
                       CreateStandardEvaluatorWithWildcards("$..a"));
  EXPECT_TRUE(eval_desc_a->ExtractExists(nested_a_json.GetConstRef()));
  EXPECT_FALSE(eval_desc_a->Extract(nested_a_json.GetConstRef()).has_value());

  // 2. JSON null match: `$..a` on `{"a": null}` matches the JSON `null`
  // literal (`ExtractExists` is true, `Extract` returns JSON null, and
  // `ExtractScalar` returns `std::nullopt`).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue null_a_json,
                       JSONValue::ParseJSONString(R"({"a": null})"));
  std::vector<JSONValueConstRef> null_matches =
      EvaluateJsonPath(path_desc_a, null_a_json.GetConstRef());
  ASSERT_EQ(null_matches.size(), 1);
  EXPECT_TRUE(null_matches[0].IsNull());
  EXPECT_EQ(null_matches[0].ToString(), "null");

  EXPECT_TRUE(eval_desc_a->ExtractExists(null_a_json.GetConstRef()));
  std::optional<JSONValueConstRef> extracted_null =
      eval_desc_a->Extract(null_a_json.GetConstRef());
  ASSERT_TRUE(extracted_null.has_value());
  EXPECT_TRUE(extracted_null->IsNull());
  EXPECT_FALSE(
      eval_desc_a->ExtractScalar(null_a_json.GetConstRef()).has_value());

  // 3. Member order: `$.*` on `{"b": 1, "a": 2}` visits object members in
  // sorted key order (`"a"` -> 2, then `"b"` -> 1).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue unsorted_obj_json,
                       JSONValue::ParseJSONString(R"({"b": 1, "a": 2})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath path_obj_wild,
                       ParseStandardJsonPathWithWildcards("$.*"));
  std::vector<JSONValueConstRef> sorted_member_matches =
      EvaluateJsonPath(path_obj_wild, unsorted_obj_json.GetConstRef());
  ASSERT_EQ(sorted_member_matches.size(), 2);
  EXPECT_EQ(sorted_member_matches[0].ToString(), "2");
  EXPECT_EQ(sorted_member_matches[1].ToString(), "1");

  // 4. Empty containers: `$.a.*` on `{"a": {}}` and `$.a[*]` on `{"a": []}`
  // yield 0 matches.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue empty_obj_json,
                       JSONValue::ParseJSONString(R"({"a": {}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath path_a_obj_wild,
                       ParseStandardJsonPathWithWildcards("$.a.*"));
  EXPECT_TRUE(
      EvaluateJsonPath(path_a_obj_wild, empty_obj_json.GetConstRef()).empty());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_a_obj_wild,
                       CreateStandardEvaluatorWithWildcards("$.a.*"));
  EXPECT_FALSE(eval_a_obj_wild->ExtractExists(empty_obj_json.GetConstRef()));
  EXPECT_FALSE(
      eval_a_obj_wild->Extract(empty_obj_json.GetConstRef()).has_value());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue empty_arr_json,
                       JSONValue::ParseJSONString(R"({"a": []})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath path_a_arr_wild,
                       ParseStandardJsonPathWithWildcards("$.a[*]"));
  EXPECT_TRUE(
      EvaluateJsonPath(path_a_arr_wild, empty_arr_json.GetConstRef()).empty());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_a_arr_wild,
                       CreateStandardEvaluatorWithWildcards("$.a[*]"));
  EXPECT_FALSE(eval_a_arr_wild->ExtractExists(empty_arr_json.GetConstRef()));
  EXPECT_FALSE(
      eval_a_arr_wild->Extract(empty_arr_json.GetConstRef()).has_value());
}

TEST(JsonWildcardsAndDescendantsTest, EvaluateJsonPathMaxMatches) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue doc,
      JSONValue::ParseJSONString(R"({"a": [10, 20], "b": {"a": 30}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath path,
                       ParseStandardJsonPathWithWildcards("$..*"));
  // Unbounded evaluation returns all 3 object member values in depth-first
  // pre-order: [10, 20] (from "a"), {"a": 30} (from "b"), and 30 (from "b.a").
  const std::vector<JSONValueConstRef> all_matches =
      EvaluateJsonPath(path, doc.GetConstRef());
  ASSERT_EQ(all_matches.size(), 3);
  EXPECT_EQ(all_matches[0].ToString(), "[10,20]");
  EXPECT_EQ(all_matches[1].ToString(), R"({"a":30})");
  EXPECT_EQ(all_matches[2].ToString(), "30");

  // max_matches = 0 returns empty immediately without traversing.
  EXPECT_TRUE(
      EvaluateJsonPath(path, doc.GetConstRef(), /*max_matches=*/0).empty());

  // max_matches = 1 returns only the first match in depth-first pre-order.
  const std::vector<JSONValueConstRef> one_match =
      EvaluateJsonPath(path, doc.GetConstRef(), /*max_matches=*/1);
  ASSERT_EQ(one_match.size(), 1);
  EXPECT_EQ(one_match[0].ToString(), "[10,20]");

  // max_matches = 2 returns the first two matches in depth-first pre-order.
  const std::vector<JSONValueConstRef> two_matches =
      EvaluateJsonPath(path, doc.GetConstRef(), /*max_matches=*/2);
  ASSERT_EQ(two_matches.size(), 2);
  EXPECT_EQ(two_matches[0].ToString(), "[10,20]");
  EXPECT_EQ(two_matches[1].ToString(), R"({"a":30})");

  // max_matches greater than total matches returns all matches.
  const std::vector<JSONValueConstRef> capped_ten =
      EvaluateJsonPath(path, doc.GetConstRef(), /*max_matches=*/10);
  ASSERT_EQ(capped_ten.size(), 3);
  EXPECT_EQ(capped_ten[0].ToString(), "[10,20]");
  EXPECT_EQ(capped_ten[1].ToString(), R"({"a":30})");
  EXPECT_EQ(capped_ten[2].ToString(), "30");
}

TEST(JsonWildcardsAndDescendantsTest, DeeplyNestedJsonSucceeds) {
  // Construct a deeply nested JSON document exceeding `kMaxParsingDepth` levels
  // and verify that explicit-stack evaluation in `EvaluateJsonPath` succeeds
  // without stack overflow or disagreement between `Extract` and
  // `ExtractExists` (matching `JsonTreeWalker` and linear `JSONValueConstRef`
  // paths).
  constexpr int kDeepDepth = JSONPathExtractor::kMaxParsingDepth + 5;
  std::string nested = "1";
  for (int i = 0; i < kDeepDepth; ++i) {
    nested = absl::StrCat("{\"a\": ", nested, "}");
  }

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue deep_json,
      JSONValue::ParseJSONString(
          nested, JSONParsingOptions{.max_nesting = kDeepDepth + 5}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath path_desc_a,
                       ParseStandardJsonPathWithWildcards("$..a"));
  std::vector<JSONValueConstRef> matches =
      EvaluateJsonPath(path_desc_a, deep_json.GetConstRef());
  ASSERT_EQ(matches.size(), kDeepDepth);
  EXPECT_EQ(matches.back().ToString(), "1");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_desc_a,
                       CreateStandardEvaluatorWithWildcards("$..a"));
  EXPECT_TRUE(eval_desc_a->ExtractExists(deep_json.GetConstRef()));
  EXPECT_FALSE(eval_desc_a->Extract(deep_json.GetConstRef()).has_value());

  // Also verify a single-match deeply nested array path (`$[*][0]...[0]`)
  // succeeds for both `Extract` and `ExtractExists`.
  std::string nested_arr = "42";
  std::string arr_path = "$[*]";
  for (int i = 0; i < kDeepDepth; ++i) {
    nested_arr = absl::StrCat("[", nested_arr, "]");
    if (i > 0) {
      absl::StrAppend(&arr_path, "[0]");
    }
  }
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue deep_arr_json,
      JSONValue::ParseJSONString(
          nested_arr, JSONParsingOptions{.max_nesting = kDeepDepth + 5}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_arr,
                       CreateStandardEvaluatorWithWildcards(arr_path));
  EXPECT_TRUE(eval_arr->ExtractExists(deep_arr_json.GetConstRef()));
  std::optional<JSONValueConstRef> deep_arr_res =
      eval_arr->Extract(deep_arr_json.GetConstRef());
  ASSERT_TRUE(deep_arr_res.has_value());
  EXPECT_EQ(deep_arr_res->ToString(), "42");
}

TEST(JsonWildcardsAndDescendantsTest, ParseJsonPathAndTokens) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ParsedJsonPath parsed,
      ParseStandardJsonPathWithWildcards("$.store..book[*].author.*"));
  EXPECT_TRUE(parsed.has_wildcard_or_descendant());
  ASSERT_EQ(parsed.steps().size(), 6);
  EXPECT_TRUE(parsed.steps()[0].is_root());
  EXPECT_TRUE(parsed.steps()[1].is_member());
  EXPECT_EQ(parsed.steps()[1].member_name(), "store");
  EXPECT_FALSE(parsed.steps()[1].is_descendant());
  EXPECT_TRUE(parsed.steps()[2].is_member());
  EXPECT_EQ(parsed.steps()[2].member_name(), "book");
  EXPECT_TRUE(parsed.steps()[2].is_descendant());
  EXPECT_EQ(parsed.steps()[3].kind(), JsonPathStepKind::kSubscriptWildcard);
  EXPECT_FALSE(parsed.steps()[3].is_descendant());
  EXPECT_TRUE(parsed.steps()[4].is_member());
  EXPECT_EQ(parsed.steps()[4].member_name(), "author");
  EXPECT_FALSE(parsed.steps()[4].is_descendant());
  EXPECT_EQ(parsed.steps()[5].kind(), JsonPathStepKind::kObjectWildcard);
  EXPECT_FALSE(parsed.steps()[5].is_descendant());

  // Descendant variants: ..*, ..[*], [ * ], ..[2], and .."a.b"
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ParsedJsonPath desc_parsed,
      ParseStandardJsonPathWithWildcards(R"($..*..[*][ * ]..[2].."a.b")"));
  EXPECT_TRUE(desc_parsed.has_wildcard_or_descendant());
  ASSERT_EQ(desc_parsed.steps().size(), 6);
  EXPECT_TRUE(desc_parsed.steps()[0].is_root());
  EXPECT_EQ(desc_parsed.steps()[1].kind(), JsonPathStepKind::kObjectWildcard);
  EXPECT_TRUE(desc_parsed.steps()[1].is_descendant());
  EXPECT_EQ(desc_parsed.steps()[2].kind(),
            JsonPathStepKind::kSubscriptWildcard);
  EXPECT_TRUE(desc_parsed.steps()[2].is_descendant());
  EXPECT_EQ(desc_parsed.steps()[3].kind(),
            JsonPathStepKind::kSubscriptWildcard);
  EXPECT_FALSE(desc_parsed.steps()[3].is_descendant());
  EXPECT_TRUE(desc_parsed.steps()[4].is_index());
  EXPECT_EQ(desc_parsed.steps()[4].index(), 2);
  EXPECT_TRUE(desc_parsed.steps()[4].is_descendant());
  EXPECT_TRUE(desc_parsed.steps()[5].is_member());
  EXPECT_EQ(desc_parsed.steps()[5].member_name(), "a.b");
  EXPECT_TRUE(desc_parsed.steps()[5].is_descendant());

  // Simple path produces legacy tokens and has_wildcard_or_descendant() ==
  // false
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath simple,
                       ParseStandardJsonPathWithWildcards("$.a[0].\"b.c\""));
  EXPECT_FALSE(simple.has_wildcard_or_descendant());
  EXPECT_THAT(simple.ToLegacyTokens(),
              IsOkAndHolds(ElementsAre("", "a", "0", "b.c")));
  GOOGLESQL_EXPECT_OK(ValidJSONPathIterator::Create(simple));
  EXPECT_THAT(ParsedJsonPath().ToLegacyTokens().status(),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ValidJSONPathIterator::Create(ParsedJsonPath()).status(),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(parsed.ToLegacyTokens().status(),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(desc_parsed.ToLegacyTokens().status(),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ValidJSONPathIterator::Create(parsed).status(),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ValidJSONPathIterator::Create(desc_parsed).status(),
              StatusIs(absl::StatusCode::kInternal));

  // Quoted asterisk `$."*"` parses as a literal `Member("*")` step with
  // `has_wildcard_or_descendant() == false`.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath quoted_star,
                       ParseStandardJsonPathWithWildcards(R"($."*")"));
  EXPECT_FALSE(quoted_star.has_wildcard_or_descendant());
  ASSERT_EQ(quoted_star.steps().size(), 2);
  EXPECT_TRUE(quoted_star.steps()[0].is_root());
  EXPECT_TRUE(quoted_star.steps()[1].is_member());
  EXPECT_EQ(quoted_star.steps()[1].member_name(), "*");
  EXPECT_FALSE(quoted_star.steps()[1].is_descendant());
  EXPECT_THAT(quoted_star.ToLegacyTokens(), IsOkAndHolds(ElementsAre("", "*")));

  // Verify that non-canonical bracket tokens preserve their exact raw string
  // in ToLegacyTokens() while populating index() as parsed by SimpleAtoi.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedJsonPath non_canonical,
                       ParseStandardJsonPathWithWildcards(
                           "$[007][-0][1_2][99999999999999999999]"));
  EXPECT_FALSE(non_canonical.has_wildcard_or_descendant());
  ASSERT_EQ(non_canonical.steps().size(), 5);
  EXPECT_EQ(non_canonical.steps()[1].index(), 7);
  EXPECT_EQ(non_canonical.steps()[1].raw_token(), "007");
  EXPECT_EQ(non_canonical.steps()[2].index(), 0);
  EXPECT_EQ(non_canonical.steps()[2].raw_token(), "-0");
  EXPECT_EQ(non_canonical.steps()[3].index(), -1);
  EXPECT_EQ(non_canonical.steps()[3].raw_token(), "1_2");
  EXPECT_EQ(non_canonical.steps()[4].index(), -1);
  EXPECT_EQ(non_canonical.steps()[4].raw_token(), "99999999999999999999");
  EXPECT_THAT(non_canonical.ToLegacyTokens(),
              IsOkAndHolds(
                  ElementsAre("", "007", "-0", "1_2", "99999999999999999999")));

  // Verify end-to-end extraction on JSON and STRING inputs for non-canonical
  // bracket tokens.
  constexpr absl::string_view kNonCanonicalDoc =
      R"({"007": 1, "0": 2, "-0": 3, "1_2": 4, "-1": "x"})";
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue non_canonical_json,
                       JSONValue::ParseJSONString(kNonCanonicalDoc));
  auto verify_non_canonical_extract =
      [&](absl::string_view path, std::optional<absl::string_view> expected) {
        SCOPED_TRACE(path);
        GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval, CreateJsonPathEvaluator(path));
        std::optional<JSONValueConstRef> json_res =
            eval->Extract(non_canonical_json.GetConstRef());
        std::string str_res;
        bool is_null = false;
        GOOGLESQL_ASSERT_OK(eval->Extract(kNonCanonicalDoc, &str_res, &is_null));
        if (expected.has_value()) {
          ASSERT_TRUE(json_res.has_value());
          EXPECT_EQ(json_res->ToString(), *expected);
          EXPECT_FALSE(is_null);
          EXPECT_EQ(str_res, *expected);
        } else {
          EXPECT_FALSE(json_res.has_value());
          EXPECT_TRUE(is_null);
        }
      };
  verify_non_canonical_extract("$[007]", "1");
  verify_non_canonical_extract("$[-0]", "3");
  verify_non_canonical_extract("$[1_2]", "4");
  verify_non_canonical_extract("$[-1]", R"("x")");
  verify_non_canonical_extract("$[99999999999999999999]", std::nullopt);
}

TEST(JsonWildcardsAndDescendantsTest, SyntaxAndMutatorGuards) {
  // Trailing .. reports a syntax error (not "Unsupported operator")
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.."),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: ..")));

  // Invalid 3-dot wildcard $...* is rejected
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$...*"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: ...*")));

  // Empty brackets $[] and $..[] are rejected
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$[]"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: []")));
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$..[]"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: []")));

  // Substring globbing $.foo* and $.*foo are rejected
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.foo*"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: *")));
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.*foo"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: *foo")));

  // Unsupported '@' operator after '..' still reports "Unsupported operator"
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$..@"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: @")));

  // Unquoted whitespace, colons, and hyphens in `.key` / `..key`, and
  // backslashes in quoted `."key"` / `.."key"`:
  //   1. Rejected when used with wildcard or descendant operators (`$.. a`,
  //      `$.. `, `$.. [0]`, `$.. *`, `$..a-b`, `$..a:b`, `$.*.a-b`,
  //      `$.a-b.*`, `$.a:b..c`, `$.a b[*]`, `$. .*`, `$.."a\b"`,
  //      `$.."a\\b"`, `$.."a\"b"`, `$.*."a\b"`, `$."a\b".*`, `$."a\"b"..c`).
  //   2. Quoted keys without backslashes are accepted (`$.."a b:c-d"`,
  //      `$.." a"`, `$."a-b".*`).
  //   3. Linear paths preserve legacy behavior for backward compatibility
  //      (`$.a-b`, `$.a:b`, `$.a b`, `$."a\b"`, `$."a\\b"`, `$."a\"b"`).
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.. a"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: .. a")));
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.. "),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: .. ")));
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.. [0]"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: .. [0]")));
  EXPECT_THAT(IsValidStandardJsonPathWithWildcards("$.. *"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid token in JSONPath at: .. *")));
  for (absl::string_view bad_key_path :
       {"$..a-b", "$..a:b", "$..a b", "$.*. a", "$.*.a-b", "$.*.a:b", "$.a-b.*",
        "$.a:b..c", "$.a b[*]", "$. .*", R"($.."a\b")", R"($.."a\\b")",
        R"($.."a\"b")", R"($.*."a\b")", R"($.*."a\"b")", R"($."a\b".*)",
        R"($."a\"b"..c)", R"($."a\\b"[*])"}) {
    EXPECT_THAT(IsValidStandardJsonPathWithWildcards(bad_key_path),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Invalid token in JSONPath at:")))
        << bad_key_path;
  }
  for (absl::string_view valid_quoted_or_linear_path :
       {R"($.."a b:c-d")", R"($.." a")", R"($."a-b".*)", "$.a-b", "$.a:b",
        "$.a b", R"($."a\b")", R"($."a\\b")", R"($."a\"b")"}) {
    GOOGLESQL_EXPECT_OK(IsValidStandardJsonPathWithWildcards(valid_quoted_or_linear_path))
        << valid_quoted_or_linear_path;
  }

  // Comparison of permissive bracket tokens (`[-1]`, `[-0]`, `[1_2]`, `[0-]`,
  // `[0_]`) and overflowing integer indices (`[99999999999999999999]`):
  //   1. Non-wildcard linear paths accept them in `ParseJsonPath` for legacy
  //      `ValidJSONPathIterator` backward compatibility (while
  //      `StrictJSONPathIterator` rejects them).
  //   2. Wildcard and descendant paths reject them with `OUT_OF_RANGE`,
  //      matching `StrictJSONPathIterator`.
  for (absl::string_view token : {"-1", "-0", "1_2", "0-", "0_"}) {
    const std::string linear_path = absl::StrCat("$[", token, "]");
    GOOGLESQL_EXPECT_OK(IsValidStandardJsonPathWithWildcards(linear_path)) << linear_path;
    EXPECT_THAT(IsValidJSONPathStrict(linear_path),
                StatusIs(absl::StatusCode::kOutOfRange))
        << linear_path;

    for (const std::string& wildcard_or_descendant_path :
         {absl::StrCat("$.*[", token, "]"), absl::StrCat("$[*][", token, "]"),
          absl::StrCat("$[", token, "].*"), absl::StrCat("$[", token, "]..a"),
          absl::StrCat("$..[", token, "]")}) {
      EXPECT_THAT(
          IsValidStandardJsonPathWithWildcards(wildcard_or_descendant_path),
          StatusIs(absl::StatusCode::kOutOfRange,
                   HasSubstr(absl::StrCat("Invalid token in JSONPath at: [",
                                          token, "]"))))
          << wildcard_or_descendant_path;
    }
  }

  constexpr absl::string_view kOverflowIndex = "99999999999999999999";
  const std::string linear_overflow_path =
      absl::StrCat("$[", kOverflowIndex, "]");
  GOOGLESQL_EXPECT_OK(IsValidStandardJsonPathWithWildcards(linear_overflow_path));
  EXPECT_THAT(IsValidJSONPathStrict(linear_overflow_path),
              StatusIs(absl::StatusCode::kOutOfRange));
  for (const std::string& wildcard_overflow_path :
       {absl::StrCat("$.*[", kOverflowIndex, "]"),
        absl::StrCat("$[", kOverflowIndex, "].*"),
        absl::StrCat("$..[", kOverflowIndex, "]")}) {
    EXPECT_THAT(IsValidStandardJsonPathWithWildcards(wildcard_overflow_path),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr(absl::StrCat(
                             "JSONPath contains invalid array index: ",
                             kOverflowIndex))))
        << wildcard_overflow_path;
  }

  // Mutator StrictJSONPathIterator and lax mode reject wildcards and
  // descendant operators even when enable_wildcard_and_descendant is true
  EXPECT_THAT(StrictJSONPathIterator::Create("$.*").status(),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(StrictJSONPathIterator::Create("$..a").status(),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(StrictJSONPathIterator::Create("$[*]").status(),
              StatusIs(absl::StatusCode::kOutOfRange));
  for (absl::string_view lax_wildcard_or_descendant_path :
       {"lax $.*", "lax $[*]", "lax $..a", "lax recursive $.*",
        "lax recursive $..a"}) {
    EXPECT_THAT(IsValidAndLaxJSONPath(lax_wildcard_or_descendant_path,
                                      /*enable_wildcard_and_descendant=*/true)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange))
        << lax_wildcard_or_descendant_path;
    EXPECT_THAT(IsValidJSONPathStrict(lax_wildcard_or_descendant_path,
                                      /*enable_lax_mode=*/true),
                StatusIs(absl::StatusCode::kOutOfRange))
        << lax_wildcard_or_descendant_path;
    EXPECT_THAT(StrictJSONPathIterator::Create(lax_wildcard_or_descendant_path,
                                               /*enable_lax_mode=*/true)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange))
        << lax_wildcard_or_descendant_path;
  }

  // Legacy JSONPath (sql_standard_mode=false) rejects wildcards and descendant
  // operators even when enable_wildcard_and_descendant is true
  EXPECT_THAT(
      IsValidLegacyJsonPath("$.*", /*enable_wildcard_and_descendant=*/true),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(
      IsValidLegacyJsonPath("$[*]", /*enable_wildcard_and_descendant=*/true),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(
      IsValidLegacyJsonPath("$..a", /*enable_wildcard_and_descendant=*/true),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Unsupported operator in JSONPath: ..")));
  EXPECT_THAT(JsonPathEvaluator::Create("$.*", /*sql_standard_mode=*/false,
                                        false, false,
                                        /*enable_wildcard_and_descendant=*/true)
                  .status(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(JsonPathEvaluator::Create("$[*]", /*sql_standard_mode=*/false,
                                        false, false,
                                        /*enable_wildcard_and_descendant=*/true)
                  .status(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(JsonPathEvaluator::Create("$..a", /*sql_standard_mode=*/false,
                                        false, false,
                                        /*enable_wildcard_and_descendant=*/true)
                  .status(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: ..")));
}

TEST(JsonWildcardsAndDescendantsTest,
     DisabledFeatureFlagRejectsWildcardsAndDescendants) {
  // When enable_wildcard_and_descendant is false (the default when
  // FEATURE_JSONPATH_WILDCARDS_AND_DESCENDANT is not enabled), all wildcard
  // and descendant operators must be rejected with OUT_OF_RANGE and
  // "Unsupported operator in JSONPath: *" / "..".
  for (absl::string_view wildcard_path : {"$.*", "$[*]", "$.a.*", "$.a[*]"}) {
    EXPECT_THAT(IsValidJSONPath(wildcard_path, /*sql_standard_mode=*/true,
                                /*enable_wildcard_and_descendant=*/false),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: *")));
    EXPECT_THAT(ParseJsonPath(wildcard_path, /*sql_standard_mode=*/true,
                              /*enable_wildcard_and_descendant=*/false)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: *")));
    EXPECT_THAT(
        ValidJSONPathIterator::Create(wildcard_path, /*sql_standard_mode=*/true)
            .status(),
        StatusIs(absl::StatusCode::kOutOfRange,
                 HasSubstr("Unsupported operator in JSONPath: *")));
    EXPECT_THAT(IsValidAndLaxJSONPath(wildcard_path,
                                      /*enable_wildcard_and_descendant=*/false)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: *")));
    EXPECT_THAT(JsonPathEvaluator::Create(
                    wildcard_path, /*sql_standard_mode=*/true, false, false,
                    /*enable_wildcard_and_descendant=*/false)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: *")));
  }

  for (absl::string_view descendant_path :
       {"$..a", "$..1", "$..*", "$..[*]", "$..[0]", R"($.."a")", "$.a.b..c"}) {
    EXPECT_THAT(IsValidJSONPath(descendant_path, /*sql_standard_mode=*/true,
                                /*enable_wildcard_and_descendant=*/false),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: ..")));
    EXPECT_THAT(ParseJsonPath(descendant_path, /*sql_standard_mode=*/true,
                              /*enable_wildcard_and_descendant=*/false)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: ..")));
    EXPECT_THAT(ValidJSONPathIterator::Create(descendant_path,
                                              /*sql_standard_mode=*/true)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: ..")));
    EXPECT_THAT(IsValidAndLaxJSONPath(descendant_path,
                                      /*enable_wildcard_and_descendant=*/false)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: ..")));
    EXPECT_THAT(JsonPathEvaluator::Create(
                    descendant_path, /*sql_standard_mode=*/true, false, false,
                    /*enable_wildcard_and_descendant=*/false)
                    .status(),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("Unsupported operator in JSONPath: ..")));
  }
}

TEST(JsonWildcardsAndDescendantsTest, DefaultStructuralFilterSemantics) {
  // 1. Subscript wildcard ([*]) with heterogeneous objects (Proposal Section
  // 2.5 `$.store.books[*].author` example):
  // In [{"a":1}, {"b":2}], element 0 matches `1` and element 1 lacks "a"
  // (yielding 0 matches for that branch without poisoning element 0).
  // Since total matches == 1, JSON_EXISTS is true and JSON_QUERY returns `1`.
  // In [{"a":1}, {"a":2}], both elements match (k = 2), so JSON_EXISTS is true
  // and JSON_QUERY returns nullopt due to multi-match cardinality (k > 1).
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto het_arr,
                       JSONValue::ParseJSONString(R"([{"a": 1}, {"b": 2}])"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto hom_arr,
                       JSONValue::ParseJSONString(R"([{"a": 1}, {"a": 2}])"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_arr_a,
                       CreateStandardEvaluatorWithWildcards("$[*].a"));
  EXPECT_TRUE(eval_arr_a->ExtractExists(het_arr.GetConstRef()));
  auto het_arr_res = eval_arr_a->Extract(het_arr.GetConstRef());
  ASSERT_TRUE(het_arr_res.has_value());
  EXPECT_EQ(het_arr_res->ToString(), "1");
  EXPECT_TRUE(eval_arr_a->ExtractExists(hom_arr.GetConstRef()));
  EXPECT_FALSE(eval_arr_a->Extract(hom_arr.GetConstRef()).has_value());

  // 2. Object wildcard (.*) with heterogeneous member objects
  // (e.g. TOKENIZE_DOCUMENT / JSON_QUERY on `$.*.a`):
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto het_obj, JSONValue::ParseJSONString(
                                         R"({"x": {"a": 1}, "y": {"b": 2}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto hom_obj, JSONValue::ParseJSONString(
                                         R"({"x": {"a": 1}, "y": {"a": 2}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_obj_a,
                       CreateStandardEvaluatorWithWildcards("$.*.a"));
  EXPECT_TRUE(eval_obj_a->ExtractExists(het_obj.GetConstRef()));
  auto het_obj_res = eval_obj_a->Extract(het_obj.GetConstRef());
  ASSERT_TRUE(het_obj_res.has_value());
  EXPECT_EQ(het_obj_res->ToString(), "1");
  EXPECT_TRUE(eval_obj_a->ExtractExists(hom_obj.GetConstRef()));
  EXPECT_FALSE(eval_obj_a->Extract(hom_obj.GetConstRef()).has_value());

  // 3. Structural container matching is still strictly enforced (no implicit
  // array unwrapping or non-array wrapping like `lax` mode):
  // On {"a": [{"b": 1}]}, `$.*.b` selects the array `[{"b": 1}]`, and `.b`
  // does not unwrap the array without `[*]`.
  // On {"a": {"b": 1}}, `$.*[0].b` and `$.*[*].b` do not wrap the object
  // `{"b": 1}` into a single-element array.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto nested_arr_doc,
                       JSONValue::ParseJSONString(R"({"a": [{"b": 1}]})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_no_unwrap,
                       CreateStandardEvaluatorWithWildcards("$.*.b"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_explicit_unwrap,
                       CreateStandardEvaluatorWithWildcards("$.*[*].b"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_no_wrap_idx,
                       CreateStandardEvaluatorWithWildcards("$.*[0].b"));
  EXPECT_FALSE(eval_no_unwrap->ExtractExists(nested_arr_doc.GetConstRef()));
  EXPECT_FALSE(
      eval_no_unwrap->Extract(nested_arr_doc.GetConstRef()).has_value());
  EXPECT_TRUE(
      eval_explicit_unwrap->ExtractExists(nested_arr_doc.GetConstRef()));
  EXPECT_FALSE(eval_no_wrap_idx->ExtractExists(het_obj.GetConstRef()));
  EXPECT_FALSE(eval_explicit_unwrap->ExtractExists(het_obj.GetConstRef()));

  // 4. Mixed-type containers and ragged array bounds do not poison valid
  // sibling branches in ExtractExists or Extract:
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto mixed_arr_doc,
      JSONValue::ParseJSONString(
          R"([1, "str", null, [10], {"other": 2}, {"a": 99}])"));
  EXPECT_TRUE(eval_arr_a->ExtractExists(mixed_arr_doc.GetConstRef()));
  auto mixed_arr_res = eval_arr_a->Extract(mixed_arr_doc.GetConstRef());
  ASSERT_TRUE(mixed_arr_res.has_value());
  EXPECT_EQ(mixed_arr_res->ToString(), "99");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto ragged_2d_doc,
      JSONValue::ParseJSONString(R"([[10], "not_array", [20, 30]])"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_ragged_idx,
                       CreateStandardEvaluatorWithWildcards("$[*][1]"));
  EXPECT_TRUE(eval_ragged_idx->ExtractExists(ragged_2d_doc.GetConstRef()));
  auto ragged_res = eval_ragged_idx->Extract(ragged_2d_doc.GetConstRef());
  ASSERT_TRUE(ragged_res.has_value());
  EXPECT_EQ(ragged_res->ToString(), "30");

  // 5. Both `$..b.c` and `$.*.b.c` filter out non-matching branch "x" (`1` is a
  // scalar) and match branch "y" (`"y".b.c = 2`):
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto mixed_doc,
      JSONValue::ParseJSONString(R"({"x": {"b": 1}, "y": {"b": {"c": 2}}})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_desc_bc,
                       CreateStandardEvaluatorWithWildcards("$..b.c"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_wild_bc,
                       CreateStandardEvaluatorWithWildcards("$.*.b.c"));
  EXPECT_TRUE(eval_desc_bc->ExtractExists(mixed_doc.GetConstRef()));
  auto desc_bc_res = eval_desc_bc->Extract(mixed_doc.GetConstRef());
  ASSERT_TRUE(desc_bc_res.has_value());
  EXPECT_EQ(desc_bc_res->ToString(), "2");

  EXPECT_TRUE(eval_wild_bc->ExtractExists(mixed_doc.GetConstRef()));
  auto wild_bc_res = eval_wild_bc->Extract(mixed_doc.GetConstRef());
  ASSERT_TRUE(wild_bc_res.has_value());
  EXPECT_EQ(wild_bc_res->ToString(), "2");

  // 6. Linear paths (`!has_wildcard_or_descendant()`) preserve legacy
  // `ValidJSONPathIterator` behavior (e.g., `$[0]` on `{"0": 1}` and `$.0` on
  // `[1]`), whereas wildcard/descendant paths enforce strict container matching
  // (`.key` is object-only; `[i]` is array-only):
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto obj_with_num_key,
                       JSONValue::ParseJSONString(R"({"0": 1})"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_idx_0,
                       CreateStandardEvaluatorWithWildcards("$[0]"));
  EXPECT_TRUE(eval_idx_0->ExtractExists(obj_with_num_key.GetConstRef()));
  auto idx_0_res = eval_idx_0->Extract(obj_with_num_key.GetConstRef());
  ASSERT_TRUE(idx_0_res.has_value());
  EXPECT_EQ(idx_0_res->ToString(), "1");
  std::string str_idx_0_res;
  bool is_null = false;
  GOOGLESQL_ASSERT_OK(eval_idx_0->Extract(R"({"0": 1})", &str_idx_0_res, &is_null));
  EXPECT_FALSE(is_null);
  EXPECT_EQ(str_idx_0_res, "1");

  // In a wildcard/descendant path (`$.*[0].b`), `[0]` strictly requires a JSON
  // array and does NOT match object member `"0"`, whereas `$.*."0".b` (and
  // `$.*.0.b`) matches object member `"0"` and does NOT index into a JSON
  // array:
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nested_obj_with_num_key,
      JSONValue::ParseJSONString(R"({"a": {"0": {"b": 42}}, "x": {"b": 1}})"));
  EXPECT_FALSE(
      eval_no_wrap_idx->ExtractExists(nested_obj_with_num_key.GetConstRef()));
  EXPECT_FALSE(eval_no_wrap_idx->Extract(nested_obj_with_num_key.GetConstRef())
                   .has_value());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto eval_wild_dot_num,
                       CreateStandardEvaluatorWithWildcards(R"($.*."0".b)"));
  EXPECT_TRUE(
      eval_wild_dot_num->ExtractExists(nested_obj_with_num_key.GetConstRef()));
  auto nested_num_key_res =
      eval_wild_dot_num->Extract(nested_obj_with_num_key.GetConstRef());
  ASSERT_TRUE(nested_num_key_res.has_value());
  EXPECT_EQ(nested_num_key_res->ToString(), "42");

  // Conversely, on `{"a": [{"b": 1}]}`, `$.*."0".b` does not index into the
  // array, whereas `$.*[0].b` does:
  EXPECT_FALSE(eval_wild_dot_num->ExtractExists(nested_arr_doc.GetConstRef()));
  EXPECT_FALSE(
      eval_wild_dot_num->Extract(nested_arr_doc.GetConstRef()).has_value());
  EXPECT_TRUE(eval_no_wrap_idx->ExtractExists(nested_arr_doc.GetConstRef()));
  auto nested_arr_idx_res =
      eval_no_wrap_idx->Extract(nested_arr_doc.GetConstRef());
  ASSERT_TRUE(nested_arr_idx_res.has_value());
  EXPECT_EQ(nested_arr_idx_res->ToString(), "1");
}

}  // namespace

}  // namespace json_internal
}  // namespace functions
}  // namespace googlesql
