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

#include <functional>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "absl/strings/substitute.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;

// 'sql_standard_mode': if true, uses the SQL2016 standard for JSON function
// names (e.g. JSON_QUERY_ARRAY instead of JSON_EXTRACT_ARRAY).
// 'scalar_test_cases': if true, returns the
// JSON_VALUE_ARRAY/JSON_EXTRACT_STRING_ARRAY test cases. Otherwise, returns the
// JSON_QUERY_ARRAY/JSON_EXTRACT_ARRAY test cases.
// 'from_json': function used for constructing json values (can be either STRING
// or JSON type). 'from_array': function used for constructing array of output
// values (can be either array of STRINGs or JSONs).
std::vector<FunctionTestCall> GetJsonArrayTestsCommon(
    bool sql_standard_mode, bool scalar_test_cases,
    const std::function<Value(std::optional<absl::string_view>)>& from_json,
    const std::function<Value(std::optional<std::vector<std::string>>)>&
        from_array) {
  std::string function_name;
  if (sql_standard_mode) {
    function_name = scalar_test_cases ? "json_value_array" : "json_query_array";
  } else {
    function_name =
        scalar_test_cases ? "json_extract_string_array" : "json_extract_array";
  }

  const Value json1 = from_json(
      R"({"a":{"b":[{"c" : "foo", "d": 1.23, "f":null }], "e": true},
      "b": [10, "", "foo"]})");
  const Value json2 =
      from_json(R"({"x": [1, 2, 3, 4, 5], "y": [{"a": "bar"}, {"b":"baz"}] })");
  const Value json3 = from_json(R"({"a":[{"b": [{"c": [{"d": [3]}]}]}]})");
  const Value json4 =
      from_json(R"({"a": ["foo", "bar", "baz"], "b": [0.123, 4.567, 8.901]})");
  const Value json5 = from_json(R"({"a": [[1, 2, 3], [3, 2, 1]]})");
  const Value json6 = from_json(R"({"d.e.f": [1, 2, 3]})");
  const Value json7 = from_json(R"({"longer_field_name": [7, 8, 9]})");
  const Value json8 =
      from_json(R"({"x" : [    ], "y"    :[1,2       ,      5,3  ,4]})");
  const Value json9 = from_json(R"([{"a": "foo"}, {"b": [0.1, 0.2]}])");
  const Value json10 = from_json(R"({"a": ["foo", null, []], "b": [[[1]]]})");
  // Note: not enclosed in {}.
  const std::string deep_json_string = R"(
  "a" : {
    "b" : {
      "c" : {
        "d" : {
          "e" : {
            "f" : {
              "g" : {
                "h" : {
                  "i" : {
                    "j" : {
                      "k" : {
                        "l" : {
                          "m" : {
                            "x" : "foo",
                            "y" : 10,
                            "z" : [1, 2, 3]
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  )";
  const std::string wide_json_string = R"(
  "a" : null, "b" : "bar", "c" : false, "d" : [4, 5], "e" : 0.123, "f" : "345",
  "g" : null, "h" : "baz", "i" : true, "j" : [-3, 0], "k" : 0.321, "l" : "678"
  )";
  const Value deep_json = from_json(absl::StrCat("{", deep_json_string, "}"));
  const Value wide_json = from_json(absl::StrCat("{", wide_json_string, "}"));

  const int kArrayElements = 20;
  std::vector<int> indexes(kArrayElements);
  std::iota(indexes.begin(), indexes.end(), 0);
  const Value array_of_wide_json = from_json(absl::Substitute(
      R"({"arr" : [$0]})",
      absl::StrJoin(
          indexes, ",", [&wide_json_string](std::string* out, int index) {
            absl::StrAppend(out, absl::Substitute(R"({"index" : $0, $1})",
                                                  index, wide_json_string));
          })));
  const Value array_of_deep_json = from_json(absl::Substitute(
      R"({"arr" : [$0]})",
      absl::StrJoin(
          indexes, ",", [&deep_json_string](std::string* out, int index) {
            absl::StrAppend(out, absl::Substitute(R"({"index" : $0, $1})",
                                                  index, deep_json_string));
          })));

  std::vector<FunctionTestCall> test_cases = {
      // Null inputs
      {function_name,
       {from_json(std::nullopt), NullString()},
       from_array(std::nullopt)},
      {function_name, {json1, NullString()}, from_array(std::nullopt)},
      {function_name,
       {from_json(std::nullopt), String("$")},
       from_array(std::nullopt)},
      // Non-array object
      {function_name, {json1, String("$.a")}, from_array(std::nullopt)},
      // Key missing
      {function_name, {json1, String("$.g")}, from_array(std::nullopt)},
      // Cases with inputs
      // - Integer array
      {function_name,
       {json2, String("$.x")},
       from_array({{"1", "2", "3", "4", "5"}})},
      // - Decimal array
      {function_name,
       {json4, String("$.b")},
       from_array({{"0.123", "4.567", "8.901"}})},
      // - Nested array
      {function_name,
       {json3, String("$.a[0].b[0].c[0].d")},
       from_array({{"3"}})},
      {function_name,
       {json3, String("$.a[0].b[0].c[1].d")},
       from_array(std::nullopt)},
      {function_name, {json5, String("$.a[1]")}, from_array({{"3", "2", "1"}})},
      {function_name, {json5, String("$.a[2]")}, from_array(std::nullopt)},
      {function_name, {json10, String("$.b[0][0]")}, from_array({{"1"}})},
      // Deep JSON
      {function_name,
       {deep_json, String("$.a.b.c.d.e.f.g.h.i.j.k.l.m.z")},
       from_array({{"1", "2", "3"}})},
      // Deep JSON error cases
      {function_name,
       {deep_json, String("$.a.b.c.d.e.f.g.h.i.j.k.l.m.x")},
       from_array(std::nullopt)},
      {function_name, {wide_json, String("$.j")}, from_array({{"-3", "0"}})},
      {function_name, {wide_json, String("$.k")}, from_array(std::nullopt)},
      {function_name, {wide_json, String("$.e[0]")}, from_array(std::nullopt)},
      // Invalid JSONPath syntax
      {function_name,
       {json1, String("abc")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json1, String("")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name, {json2, String("$.x[-1]")}, from_array(std::nullopt)},
      {function_name, {json2, String("$.y.a")}, from_array(std::nullopt)},
      {function_name,
       {json3, String("$[a.b.c]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json7, String("$.longer_field_name")},
       from_array({{"7", "8", "9"}})},
      {function_name,
       {json8, String("$.y")},
       from_array({{"1", "2", "5", "3", "4"}})},
      {function_name,
       {json8, String("$.x")},
       from_array({std::vector<std::string>()})},
      {function_name,
       {array_of_deep_json, String("$.arr[13]")},
       from_array(std::nullopt)},
      {function_name,
       {array_of_deep_json, String("$.arr[13].a.b.c.d.e.f.g.h.i.j.k.l.m.z")},
       from_array({{"1", "2", "3"}})},
      {function_name,
       {array_of_wide_json, String("$.arr[14].d")},
       from_array({{"4", "5"}})},
      {function_name, {json9, String("$[1].b")}, from_array({{"0.1", "0.2"}})},
      // Non-ASCII UTF-8 and special cases.
      {function_name,
       {from_json(R"({"Моша_öá5ホバークラフト鰻鰻" : [1, 2, 3]})"),
        String("$.Моша_öá5ホバークラフト鰻鰻")},
       from_array({{"1", "2", "3"}})},
      // Unsupported/unimplemented JSONPath features.
      {function_name,
       {json1, String("$.a.*")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json1, String("$.a.b..c")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String("$.x[(@.length-1)]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String("$.x[-1:]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String("$.x[0:4:2]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String("$.x[:2]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String("$.x[0,1]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String("$.y[?(@.a)]")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json2, String(R"($.y[?(@.a==='bar')])")},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      // Tests of which results vary by sql_standard_mode;
      // Bracket/Dot notation for children/sub-trees
      {function_name,
       {json2, sql_standard_mode ? String("$.x") : String("$['x']")},
       from_array({{"1", "2", "3", "4", "5"}})},
      {function_name,
       {json2, sql_standard_mode ? String("$.x.a") : String("$.x['a']")},
       from_array(std::nullopt)},
      // Query with dots in the middle of the key
      {function_name,
       {json6, absl::StrCat("$", EscapeKey(!sql_standard_mode, "d.e.f"))},
       from_array(std::nullopt),
       OUT_OF_RANGE},
      {function_name,
       {json6, absl::StrCat("$", EscapeKey(sql_standard_mode, "d.e.f"))},
       from_array({{"1", "2", "3"}})},
  };
  if (scalar_test_cases) {
    // Not scalar array - object array
    test_cases.push_back(
        {function_name, {json2, String("$.y")}, from_array(std::nullopt)});
    // Not scalar array - nested array
    test_cases.push_back(
        {function_name, {json5, String("$.a")}, from_array(std::nullopt)});
    test_cases.push_back(
        {function_name, {json10, String("$.b")}, from_array(std::nullopt)});
    test_cases.push_back(
        {function_name, {json10, String("$.b[0]")}, from_array(std::nullopt)});
    // String array
    test_cases.push_back({function_name,
                          {json4, String("$.a")},
                          from_array({{"foo", "bar", "baz"}})});
    // Empty string
    test_cases.push_back({function_name,
                          {json1, String("$.b")},
                          from_array({{"10", "", "foo"}})});
    // String array - Non-ASCII UTF-8 and special cases.
    test_cases.push_back(
        {function_name,
         {from_json(R"(["Моша_öá5ホバークラフト鰻鰻"])"), String("$")},
         from_array({{"Моша_öá5ホバークラフト鰻鰻"}})});
    test_cases.push_back(
        {function_name,
         {from_json(R"({"a": ["foo\t\\t\\\t\n\\nbar \"baz\\"]})"),
          String("$.a")},
         from_array({{"foo\t\\t\\\t\n\\nbar \"baz\\"}})});
  } else {
    // Object array
    test_cases.push_back(
        {function_name,
         {json2, String("$.y")},
         from_array({{"{\"a\":\"bar\"}", "{\"b\":\"baz\"}"}})});
    // Special character in object key. Regression test for b/265948860
    test_cases.push_back(
        {function_name,
         {from_json(R"({"foo": [{"b\"ar":"q\"w"}, {"b\"\"ar":"q\"w"}]})"),
          String("$.foo")},
         from_array({{R"({"b\"ar":"q\"w"})", R"({"b\"\"ar":"q\"w"})"}})});
    // Not scalar array - nested array
    test_cases.push_back({function_name,
                          {json5, String("$.a")},
                          from_array({{"[1,2,3]", "[3,2,1]"}})});
    test_cases.push_back(
        {function_name, {json10, String("$.b")}, from_array({{"[[1]]"}})});
    test_cases.push_back(
        {function_name, {json10, String("$.b[0]")}, from_array({{"[1]"}})});
    // String array
    test_cases.push_back({function_name,
                          {json4, String("$.a")},
                          from_array({{"\"foo\"", "\"bar\"", "\"baz\""}})});
    // Empty string
    test_cases.push_back({function_name,
                          {json1, String("$.b")},
                          from_array({{"10", "\"\"", "\"foo\""}})});
    // String array - Non-ASCII UTF-8 and special cases.
    test_cases.push_back(
        {function_name,
         {from_json(R"(["Моша_öá5ホバークラフト鰻鰻"])"), String("$")},
         from_array({{R"("Моша_öá5ホバークラフト鰻鰻")"}})});
    test_cases.push_back(
        {function_name,
         {from_json(R"({"a": ["foo\t\\t\\\t\n\\nbar \"baz\\"]})"),
          String("$.a")},
         from_array({{R"("foo\t\\t\\\t\n\\nbar \"baz\\")"}})});
  }
  return test_cases;
}

std::vector<FunctionTestCall> GetStringJsonArrayTests(bool sql_standard_mode,
                                                      bool scalar_test_cases) {
  std::vector<FunctionTestCall> tests = GetJsonArrayTestsCommon(
      sql_standard_mode, scalar_test_cases,
      /*from_json=*/
      [](std::optional<absl::string_view> input) {
        if (input.has_value()) return String(input.value());
        return NullString();
      },
      /*from_array=*/
      [](std::optional<std::vector<std::string>> inputs) {
        if (!inputs.has_value()) return Null(StringArrayType());
        return StringArray(*inputs);
      });

  std::string function_name;
  if (sql_standard_mode) {
    function_name = scalar_test_cases ? "json_value_array" : "json_query_array";
  } else {
    function_name =
        scalar_test_cases ? "json_extract_string_array" : "json_extract_array";
  }

  // Malformed JSON.
  tests.push_back({function_name,
                   {String(R"({"a": )"), String("$")},
                   Null(StringArrayType())});
  // Wide numbers.
  const Value json_array_with_wide_numbers = String(
      R"([1111111111111111111111111,
          123456789012345678901234567890])");
  tests.push_back({function_name,
                   {json_array_with_wide_numbers, String("$")},
                   StringArray({"1111111111111111111111111",
                                "123456789012345678901234567890"})});

  const Value array_with_null = String(R"({"a": [1, null, "foo"]})");
  if (scalar_test_cases) {
    // Array with null element and string value.
    tests.push_back({function_name,
                     {array_with_null, String("$.a")},
                     Array({String("1"), NullString(), String("foo")})});
  } else {
    // Array with null element and string value.
    tests.push_back({function_name,
                     {array_with_null, String("$.a")},
                     StringArray({"1", "null", "\"foo\""})});
  }

  for (auto& test : tests) {
    // All but one of these functions requires this feature.
    if (test.function_name != "json_extract_array") {
      test.params.AddRequiredFeature(FEATURE_JSON_ARRAY_FUNCTIONS);
    }
  }

  return tests;
}

std::vector<FunctionTestCall> GetNativeJsonArrayTests(bool sql_standard_mode,
                                                      bool scalar_test_cases) {
  auto from_json = [](std::optional<absl::string_view> input) {
    if (!input.has_value()) return NullJson();
    return Json(JSONValue::ParseJSONString(input.value()).value());
  };
  auto from_array = scalar_test_cases ?
      [](std::optional<std::vector<std::string>> inputs) {
        if (!inputs.has_value()) return Null(StringArrayType());
        return StringArray(*inputs);
      } :
      [](std::optional<std::vector<std::string>> inputs) {
    if (!inputs.has_value()) return Null(JsonArrayType());
    std::vector<JSONValue> json_inputs;
    for (const std::string& input : *inputs) {
      json_inputs.emplace_back(JSONValue::ParseJSONString(input).value());
    }
    return JsonArray(json_inputs);
  };

  std::vector<FunctionTestCall> tests = GetJsonArrayTestsCommon(
      sql_standard_mode, scalar_test_cases, from_json, from_array);

  std::string function_name;
  if (sql_standard_mode) {
    function_name = scalar_test_cases ? "json_value_array" : "json_query_array";
  } else {
    function_name =
        scalar_test_cases ? "json_extract_string_array" : "json_extract_array";
  }

  const Value json_array_with_wide_numbers = from_json(
      R"([ 1111111111111111111111111, 123456789012345678901234567890 ])");

  const Value array_with_null = from_json(R"({"a": [1, null, "foo"]})");

  if (scalar_test_cases) {
    // Wide numbers.
    tests.push_back(
        {function_name,
         {json_array_with_wide_numbers, String("$")},
         from_array({{"1.1111111111111111e+24", "1.2345678901234568e+29"}})});
    // Array with null element and string value.
    tests.push_back({function_name,
                     {array_with_null, String("$.a")},
                     Array({String("1"), NullString(), String("foo")})});
  } else {
    // Wide numbers.
    tests.push_back({function_name,
                     {json_array_with_wide_numbers, String("$")},
                     from_array({{"1111111111111111111111111",
                                  "123456789012345678901234567899"}})});
    // Array with null element and string value.
    tests.push_back({function_name,
                     {array_with_null, String("$.a")},
                     from_array({{"1", "null", "\"foo\""}})});
  }

  for (auto& test : tests) {
    // All but one of these functions requires this feature.
    if (test.function_name != "json_extract_array") {
      test.params.AddRequiredFeature(FEATURE_JSON_ARRAY_FUNCTIONS);
    }
  }

  return tests;
}

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsStringJsonExtractArray() {
  return GetStringJsonArrayTests(/*sql_standard_mode=*/false,
                                 /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsStringJsonExtractStringArray() {
  return GetStringJsonArrayTests(/*sql_standard_mode=*/false,
                                 /*scalar_test_cases=*/true);
}

std::vector<FunctionTestCall> GetFunctionTestsStringJsonQueryArray() {
  return GetStringJsonArrayTests(/*sql_standard_mode=*/true,
                                 /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsStringJsonValueArray() {
  return GetStringJsonArrayTests(/*sql_standard_mode=*/true,
                                 /*scalar_test_cases=*/true);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonExtractArray() {
  return GetNativeJsonArrayTests(/*sql_standard_mode=*/false,
                                 /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonExtractStringArray() {
  return GetNativeJsonArrayTests(/*sql_standard_mode=*/false,
                                 /*scalar_test_cases=*/true);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonQueryArray() {
  return GetNativeJsonArrayTests(/*sql_standard_mode=*/true,
                                 /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonValueArray() {
  return GetNativeJsonArrayTests(/*sql_standard_mode=*/true,
                                 /*scalar_test_cases=*/true);
}

}  // namespace googlesql
