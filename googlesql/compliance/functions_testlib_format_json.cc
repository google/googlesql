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
#include <string>
#include <vector>

#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "re2/re2.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsFormatJson() {
  const std::string kJsonValueString =
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

  // Clean up the string to make it the same as FORMAT %P output.
  std::string json_value_multiline_str(kJsonValueString);
  // Remove leading 8 spaces (but no more!).
  RE2::GlobalReplace(&json_value_multiline_str, "[ ]{8}", "");

  const Value json_null = Value::Null(types::JsonType());

  // Tests for different escaping characters and embedded quotes.
  const std::string kEscapeCharsJsonValueString =
      R"({"int64_val'_1":1,)"
      R"("string_val_1":"foo'bar",)"
      R"("string_val_2":"foo''bar",)"
      R"("string_val_3":"foo`bar",)"
      R"("string_val_4":"foo``bar",)"
      R"("string_val_5":"foo\"bar",)"
      R"("string_val_6":"foo\\bar"})";
  const Value escape_chars_json_value =
      Json(JSONValue::ParseJSONString(kEscapeCharsJsonValueString).value());
  const std::string& escape_chars_json_str = kEscapeCharsJsonValueString;
  const std::string escape_chars_json_str_multiline =
      R"({
  "int64_val'_1": 1,
  "string_val_1": "foo'bar",
  "string_val_2": "foo''bar",
  "string_val_3": "foo`bar",
  "string_val_4": "foo``bar",
  "string_val_5": "foo\"bar",
  "string_val_6": "foo\\bar"
})";
  const std::string escape_chars_json_sql_literal =
      R"(JSON "{\"int64_val'_1\":1,)"
      R"(\"string_val_1\":\"foo'bar\",)"
      R"(\"string_val_2\":\"foo''bar\",)"
      R"(\"string_val_3\":\"foo`bar\",)"
      R"(\"string_val_4\":\"foo``bar\",)"
      R"(\"string_val_5\":\"foo\\\"bar\",)"
      R"(\"string_val_6\":\"foo\\\\bar\"}")";

  std::vector<FunctionTestCall> test_cases = {
      // Invalid formats for JSON.
      {"format", {"%d", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%i", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%x", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%o", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", json_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", json_value}, NullString(), OUT_OF_RANGE},
      // Valid formats for JSON.
      {"format", {"%t", json_value}, json_value_str},
      {"format",
       {"%T", json_value},
       absl::StrCat("JSON '", json_value_str, "'")},
      {"format", {"%p", json_value}, json_value_str},
      {"format", {"%P", json_value}, json_value_multiline_str},
      {"format", {"%t", json_null}, "NULL"},
      {"format", {"%T", json_null}, "NULL"},
      {"format", {"%p", json_null}, NullString()},
      {"format", {"%P", json_null}, NullString()},
      // Formatting jsons with strings containing escape characters
      {"format", {"%t", escape_chars_json_value}, escape_chars_json_str},
      {"format",
       {"%T", escape_chars_json_value},
       escape_chars_json_sql_literal},
      {"format", {"%p", escape_chars_json_value}, escape_chars_json_str},
      {"format",
       {"%P", escape_chars_json_value},
       escape_chars_json_str_multiline},
  };

  for (FunctionTestCall& test_case : test_cases) {
    test_case.params = test_case.params.WrapWithFeature(FEATURE_JSON_TYPE);
  }

  return test_cases;
}

}  // namespace googlesql
