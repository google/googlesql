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

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;

using ::googlesql::values::UnvalidatedJsonStringArray;

Value ParseJson(absl::string_view json) {
  return Json(JSONValue::ParseJSONString(json).value());
}

// Note: not enclosed in {}.
constexpr absl::string_view kDeepJsonString = R"(
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

constexpr absl::string_view kWideJsonString = R"(
  "a" : null, "b" : "bar", "c" : false, "d" : [4, 5], "e" : 0.123, "f" : "345",
  "g" : null, "h" : "baz", "i" : true, "j" : [-3, 0], "k" : 0.321, "l" : "678"
  )";

// 'sql_standard_mode': if true, uses the SQL2016 standard for JSON function
// names (e.g. JSON_QUERY instead of JSON_EXTRACT).
// 'scalar_test_cases': if true, returns the JSON_VALUE/JSON_EXTRACT_SCALAR test
// cases. Otherwise, returns the JSON_QUERY/JSON_EXTRACT test cases.
// 'json_constructor': function used for constructing json values.
std::vector<FunctionTestCall> GetJsonTestsCommon(
    bool sql_standard_mode, bool scalar_test_cases,
    const std::function<Value(std::optional<absl::string_view>)>&
        json_constructor) {
  std::string query_fn_name;
  std::string value_fn_name;
  if (sql_standard_mode) {
    query_fn_name = "json_query";
    value_fn_name = "json_value";
  } else {
    query_fn_name = "json_extract";
    value_fn_name = "json_extract_scalar";
  }

  const Value json1 = json_constructor(
      R"({"a":{"b":[{"c" : "foo", "d": 1.23, "f":null }], "e": true}})");
  const Value json2 = json_constructor(
      R"({"x": [1, 2, 3, 4, 5], "y": [{"a": "bar"}, {"b":"baz"}] })");
  const Value json3 = json_constructor(R"({"a.b.c": 5})");
  const Value json4 = json_constructor(R"({"longer_field_name": []})");
  const Value json5 = json_constructor("true");
  const Value json6 =
      json_constructor(R"({"a":[{"b": [{"c": [{"d": [3]}]}]}]})");
  const Value json7 = json_constructor(R"({"a":{"b": {"c": {"d": 3}}}})");
  const Value json8 =
      json_constructor(R"({"x" : [    ], "y"    :[1,2       ,      5,3  ,4]})");
  const Value json9 = json_constructor(R"({"a": 1, "": [5, "foo"]})");

  const Value deep_json =
      json_constructor(absl::StrCat("{", kDeepJsonString, "}"));
  const Value wide_json =
      json_constructor(absl::StrCat("{", kWideJsonString, "}"));

  const int kArrayElements = 20;
  std::vector<int> indexes(kArrayElements);
  std::iota(indexes.begin(), indexes.end(), 0);
  const Value array_of_wide_json = json_constructor(absl::Substitute(
      R"({"arr" : [$0]})",
      absl::StrJoin(indexes, ",", [](std::string* out, int index) {
        absl::StrAppend(out, absl::Substitute(R"({"index" : $0, $1})", index,
                                              kWideJsonString));
      })));
  const Value array_of_deep_json = json_constructor(absl::Substitute(
      R"({"arr" : [$0]})",
      absl::StrJoin(indexes, ",", [](std::string* out, int index) {
        absl::StrAppend(out, absl::Substitute(R"({"index" : $0, $1})", index,
                                              kDeepJsonString));
      })));

  std::vector<FunctionTestCall> all_tests;
  if (scalar_test_cases) {
    all_tests = {
        {value_fn_name, {json1}, NullString()},
        {value_fn_name, {json1, String("$.a.b[0]")}, NullString()},
        {value_fn_name, {json1, String("$.a.b[0].c")}, String("foo")},
        {value_fn_name, {json1, String("$.a.b[0].d")}, String("1.23")},
        {value_fn_name, {json1, String("$.a.b[0].f")}, NullString()},
        {value_fn_name, {json1, String("$.a.e")}, String("true")},
        {value_fn_name, {json2, String("$.y[0].a")}, String("bar")},
        {value_fn_name,
         {json2, absl::StrCat("$.y[0]", EscapeKey(sql_standard_mode, "a"))},
         String("bar")},
        {value_fn_name, {json2, String("$.y[1].b")}, String("baz")},
        {value_fn_name, {json5, String("$")}, String("true")},
        {value_fn_name, {json7, String("$.a.b.c.d")}, String("3")},
        {value_fn_name,
         {json7, absl::StrCat("$", EscapeKey(sql_standard_mode, "a"),
                              EscapeKey(sql_standard_mode, "b"), ".c",
                              EscapeKey(sql_standard_mode, "d"))},
         String("3")},
        {value_fn_name, {json8, String("$.y[3]")}, String("3")},
        // Deep/wide json.
        {value_fn_name,
         {deep_json, String("$.a.b.c.d.e.f.g.h.i.j.k.l.m.x")},
         String("foo")},
        {value_fn_name, {wide_json, String("$.j[1]")}, String("0")},
        {value_fn_name,
         {array_of_deep_json, String("$.arr[13].a.b.c.d.e.f.g.h.i.j.k.l.m.x")},
         String("foo")},
        {value_fn_name,
         {array_of_wide_json, String("$.arr[12].index")},
         String("12")},
        {value_fn_name,
         {array_of_wide_json, String("$.arr[12].h")},
         String("baz")},
        // Non-ASCII UTF-8 and special cases.
        {value_fn_name,
         {json_constructor(R"({"Моша_öá5ホバークラフト鰻鰻" : "x"})"),
          String("$.Моша_öá5ホバークラフト鰻鰻")},
         String("x")},
        {value_fn_name,
         {json_constructor(R"({"1" : 2})"),
          absl::StrCat("$", EscapeKey(sql_standard_mode, "1"))},
         String("2")},
        {value_fn_name,
         {json_constructor(R"({"1" : 2})"), String("$.1")},
         String("2")},
        // Unsupported/unimplemented JSONPath features.
        {value_fn_name,
         {json6, String("$.a[0].b[(@.length-1)].c[(@.length-1)].d")},
         NullString(),
         OUT_OF_RANGE},
        {value_fn_name, {json9, String("$..1")}, NullString(), OUT_OF_RANGE},
        // Optional json_path argument
        {value_fn_name,
         {json_constructor("\"hello my friend\"")},
         String("hello my friend")},
        {value_fn_name, {json_constructor("-4.58295")}, String("-4.58295")},
    };
    if (sql_standard_mode) {
      all_tests.push_back({value_fn_name,
                           {json3, String("$['a.b.c']")},
                           NullString(),
                           OUT_OF_RANGE});
      all_tests.push_back(
          {value_fn_name, {json9, String("$.\"\"[1]")}, String("foo")});
    } else {
      all_tests.push_back({value_fn_name,
                           {json3, String("$.\"a.b.c\"")},
                           NullString(),
                           OUT_OF_RANGE});
      all_tests.push_back(
          {value_fn_name, {json9, String("$[''][1]")}, String("foo")});
      all_tests.push_back({value_fn_name,
                           {json9, String("$[][1]")},
                           NullString(),
                           OUT_OF_RANGE});
    }
  } else {
    all_tests = {
        {query_fn_name,
         {json_constructor(std::nullopt), NullString()},
         json_constructor(std::nullopt)},
        {query_fn_name, {json1, NullString()}, json_constructor(std::nullopt)},
        {query_fn_name,
         {json_constructor(std::nullopt), "$"},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json1, "$"},
         json_constructor(
             R"({"a":{"b":[{"c":"foo","d":1.23,"f":null}],"e":true}})")},
        {query_fn_name,
         {json1, String("$.a")},
         json_constructor(R"({"b":[{"c":"foo","d":1.23,"f":null}],"e":true})")},
        {query_fn_name,
         {json1, absl::StrCat("$", EscapeKey(sql_standard_mode, "a"))},
         json_constructor(R"({"b":[{"c":"foo","d":1.23,"f":null}],"e":true})")},
        {query_fn_name,
         {json1, String("$.a.b")},
         json_constructor(R"([{"c":"foo","d":1.23,"f":null}])")},
        {query_fn_name,
         {json1, String("$.a.b[0]")},
         json_constructor(R"({"c":"foo","d":1.23,"f":null})")},
        {query_fn_name,
         {json1, String("$.a.b[0].c")},
         json_constructor(R"("foo")")},
        {query_fn_name,
         {json1, String("$.a.b[0].d")},
         json_constructor("1.23")},
        {query_fn_name,
         {json1, String("$.a.b[0].g")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json1, String("$.a.b[1]")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json1, String("$.a.x[0]")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json1, String("$.a.x")},
         json_constructor(std::nullopt)},
        {query_fn_name, {json1, String("$.a.e")}, json_constructor("true")},
        {query_fn_name,
         {json1, String("abc")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json1, String("")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.x")},
         json_constructor("[1,2,3,4,5]")},
        {query_fn_name, {json2, String("$.x[1]")}, json_constructor("2")},
        {query_fn_name,
         {json2, absl::StrCat("$", EscapeKey(sql_standard_mode, "x"), "[1]")},
         json_constructor("2")},
        {query_fn_name,
         {json2, String("$.x[-1]")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json2, absl::StrCat("$.x", EscapeKey(sql_standard_mode, "a"))},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json2, absl::StrCat("$.x", EscapeKey(sql_standard_mode, "1"))},
         json_constructor("2")},
        {query_fn_name, {json2, String("$.x[ 1]")}, json_constructor("2")},
        {query_fn_name,
         {json2, String("$.x[10]")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json2, String("$.y.a")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json2, String("$.y[0].a")},
         json_constructor(R"("bar")")},
        {query_fn_name,
         {json2, absl::StrCat("$.y[0]", EscapeKey(sql_standard_mode, "a"))},
         json_constructor(R"("bar")")},
        {query_fn_name,
         {json2, String("$.y[0].b")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json2, String("$.y[1].a")},
         json_constructor(std::nullopt)},
        {query_fn_name,
         {json2, String("$.y[1].b")},
         json_constructor(R"("baz")")},
        {query_fn_name,
         {json3, String("$")},
         json_constructor(R"({"a.b.c":5})")},
        // Query with dots in the middle of the key
        {query_fn_name,
         {json3, absl::StrCat("$", EscapeKey(sql_standard_mode, "a.b.c"))},
         json_constructor("5")},
        // If $[a.b.c] is a valid path then it should return "5"
        // Else it should return OUT_OF_RANGE current code fails to
        // handle this correctly.
        //
        // {query_fn_name, {json3, String("$[a.b.c]")}, NullString(),
        //  OUT_OF_RANGE},
        {query_fn_name,
         {json4, String("$.longer_field_name")},
         json_constructor("[]")},
        {query_fn_name, {json5, String("$")}, json_constructor("true")},
        {query_fn_name,
         {json7, String("$")},
         json_constructor(R"({"a":{"b":{"c":{"d":3}}}})")},
        {query_fn_name,
         {json8, String("$")},
         json_constructor(R"({"x":[],"y":[1,2,5,3,4]})")},
        {query_fn_name, {json8, String("$.x")}, json_constructor(R"([])")},
        // Deep/wide json.
        {query_fn_name,
         {deep_json, String("$.a.b.c.d.e.f.g.h.i.j")},
         json_constructor(
             R"({"k":{"l":{"m":{"x":"foo","y":10,"z":[1,2,3]}}}})")},
        {query_fn_name,
         {deep_json, String("$.a.b.c.d.e.f.g.h.i.j.k.l.m")},
         json_constructor(R"({"x":"foo","y":10,"z":[1,2,3]})")},
        {query_fn_name,
         {deep_json, String("$.a.b.c.d.e.f.g.h.i.j.k.l.m.z[1]")},
         json_constructor("2")},
        {query_fn_name,
         {deep_json, String("$.a.b.c.d.e.f.g.h.i.j.k.l.m.x")},
         json_constructor(R"("foo")")},
        {query_fn_name, {wide_json, String("$.j")}, json_constructor("[-3,0]")},
        {query_fn_name,
         {array_of_deep_json, String("$.arr[13].index")},
         json_constructor("13")},
        {query_fn_name,
         {array_of_deep_json,
          String("$.arr[13].a.b.c.d.e.f.g.h.i.j.k.l.m.z[1]")},
         json_constructor("2")},
        {query_fn_name,
         {array_of_wide_json, String("$.arr[17].index")},
         json_constructor("17")},
        {query_fn_name,
         {array_of_wide_json, String("$.arr[17].k")},
         json_constructor("0.321")},
        {query_fn_name,
         {array_of_wide_json, String("$.arr[14]")},
         json_constructor(
             R"({"index":14,"a":null,"b":"bar","c":false,"d":[4,5],"e":0.123,)"
             R"("f":"345","g":null,"h":"baz","i":true,"j":[-3,0],"k":0.321,)"
             R"("l":"678"})")},
        // Non-ASCII UTF-8 and special cases.
        {query_fn_name,
         {json_constructor(R"({"Моша_öá5ホバークラフト鰻鰻" : "x"})"),
          String("$")},
         json_constructor(R"({"Моша_öá5ホバークラフト鰻鰻":"x"})")},
        // Special character in object key. Regression test for b/265948860
        {query_fn_name,
         {json_constructor(R"({"foo":{"b\"ar":"q\"w"}})"), String("$.foo")},
         json_constructor(R"({"b\"ar":"q\"w"})")},
        // Unsupported/unimplemented JSONPath features.
        {query_fn_name,
         {json1, String("$.a.*")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json1, String("$.a.b..c")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.x[(@.length-1)]")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.x[-1:]")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.x[0:4:2]")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.x[:2]")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.x[0,1]")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String("$.y[?(@.a)]")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json2, String(R"($.y[?(@.a==='bar')])")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json9, String("$..1")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json9, String("lax $.a")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
        {query_fn_name,
         {json9, String("lax recursive $.a")},
         json_constructor(std::nullopt),
         OUT_OF_RANGE},
    };
    if (sql_standard_mode) {
      all_tests.push_back({query_fn_name,
                           {json3, String("$['a.b.c']")},
                           json_constructor(std::nullopt),
                           OUT_OF_RANGE});
      all_tests.push_back({query_fn_name,
                           {json9, String("$.\"\"")},
                           json_constructor(R"([5,"foo"])")});
      all_tests.push_back({query_fn_name,
                           {json9, String("$.\"\"[1]")},
                           json_constructor(R"("foo")")});
    } else {
      all_tests.push_back({query_fn_name,
                           {json3, String("$.\"a.b.c\"")},
                           json_constructor(std::nullopt),
                           OUT_OF_RANGE});
      all_tests.push_back({query_fn_name,
                           {json9, String("$['']")},
                           json_constructor(R"([5,"foo"])")});
      all_tests.push_back({query_fn_name,
                           {json9, String("$[''][1]")},
                           json_constructor(R"("foo")")});
      all_tests.push_back({query_fn_name,
                           {json9, String("$[][1]")},
                           json_constructor(std::nullopt),
                           OUT_OF_RANGE});
    }
  }
  return all_tests;
}

std::vector<FunctionTestCall> GetStringJsonTests(bool sql_standard_mode,
                                                 bool scalar_test_cases) {
  std::string query_fn_name;
  std::string value_fn_name;
  if (sql_standard_mode) {
    query_fn_name = "json_query";
    value_fn_name = "json_value";
  } else {
    query_fn_name = "json_extract";
    value_fn_name = "json_extract_scalar";
  }
  // Wide numbers
  const Value json_with_wide_numbers = String(
      R"({
           "x":11111111111111111111,
           "y":3.14e314,
           "z":123456789012345678901234567890,
           "a":true,
           "s":"foo"
         })");

  std::vector<FunctionTestCall> tests =
      GetJsonTestsCommon(sql_standard_mode, scalar_test_cases,
                         [](std::optional<absl::string_view> input) {
                           if (input.has_value()) return String(input.value());
                           return NullString();
                         });

  if (scalar_test_cases) {
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.x")},
                     String("11111111111111111111")});
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.y")},
                     String("3.14e314")});
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.z")},
                     String("123456789012345678901234567890")});
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.s")},
                     String("foo")});
    // Characters should _not_ be escaped in JSON_EXTRACT_SCALAR.
    tests.push_back(
        {value_fn_name,
         {String(R"({"a": "foo\t\\t\\\t\n\\nbar\ \"baz\\"})"), String("$.a")},
         String("foo\t\\t\\\t\n\\nbar \"baz\\")});
  } else {
    tests.push_back(
        {query_fn_name,
         {String(
              R"({"a":{"b":[{"c" : "foo", "d": 1.23, "f":null }], "e": true}})"),
          String("$.a.b[0].f")},
         NullString()});
    // Malformed JSON.
    tests.push_back(
        {query_fn_name, {String(R"({"a": )"), String("$")}, NullString()});
    // Wide numbers
    tests.push_back(
        {query_fn_name,
         {json_with_wide_numbers, String("$")},
         String(R"({"x":11111111111111111111,"y":3.14e314,)"
                R"("z":123456789012345678901234567890,"a":true,"s":"foo"})")});
    // Characters should be escaped in JSON_EXTRACT.
    tests.push_back(
        {query_fn_name,
         {String(R"({"a": "foo\t\\t\\\t\n\\nbar\ \"baz\\"})"), String("$.a")},
         String(R"("foo\t\\t\\\t\n\\nbar \"baz\\")")});
  }
  return tests;
}

std::vector<FunctionTestCall> GetNativeJsonTests(bool sql_standard_mode,
                                                 bool scalar_test_cases) {
  std::string query_fn_name;
  std::string value_fn_name;
  if (sql_standard_mode) {
    query_fn_name = "json_query";
    value_fn_name = "json_value";
  } else {
    query_fn_name = "json_extract";
    value_fn_name = "json_extract_scalar";
  }

  // Wide numbers
  const Value json_with_wide_numbers =
      Json(JSONValue::ParseJSONString(R"({"x":11111111111111111111, )"
                                      R"("z":123456789012345678901234567890, )"
                                      R"("a":true, "s":"foo"})")
               .value());

  std::vector<FunctionTestCall> tests = GetJsonTestsCommon(
      sql_standard_mode, scalar_test_cases,
      [](std::optional<absl::string_view> input) {
        if (input.has_value())
          return Json(JSONValue::ParseJSONString(input.value()).value());
        return NullJson();
      });

  if (scalar_test_cases) {
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.x")},
                     String("11111111111111111111")});
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.z")},
                     String("1.2345678901234568e+29")});
    tests.push_back({value_fn_name,
                     {json_with_wide_numbers, String("$.s")},
                     String("foo")});
    // Characters should _not_ be escaped in JSON_EXTRACT_SCALAR.
    tests.push_back({value_fn_name,
                     {Json(JSONValue::ParseJSONString(
                               R"({"a": "foo\t\\t\\\t\n\\nbar \"baz\\"})")
                               .value()),
                      String("$.a")},
                     String("foo\t\\t\\\t\n\\nbar \"baz\\")});
  } else {
    JSONValue null_json_value;
    // This case differs from the string version of STRING_EXTRACT/QUERY. The
    // result is a JSON 'null' which is different from a null JSON.
    tests.push_back(
        {query_fn_name,
         {Json(JSONValue::ParseJSONString(
                   R"({"a":{"b":[{"c" : "foo", "d": 1.23, "f":null }], )"
                   R"("e": true}})")
                   .value()),
          String("$.a.b[0].f")},
         Json(std::move(null_json_value))});
    tests.push_back(
        {query_fn_name,
         {json_with_wide_numbers, String("$")},
         Json(JSONValue::ParseJSONString(
                  R"({"x":11111111111111111111,)"
                  R"("z":123456789012345678901234567890,"a":true,"s":"foo"})")
                  .value())});
    // Characters should be escaped in JSON_EXTRACT.
    tests.push_back(
        {query_fn_name,
         {Json(JSONValue::ParseJSONString(
                   R"({"a": "foo\t\\t\\\t\n\\nbar \"baz\\"})")
                   .value()),
          String("$.a")},
         Json(JSONValue::ParseJSONString(R"("foo\t\\t\\\t\n\\nbar \"baz\\")")
                  .value())});
  }

  return tests;
}

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsStringJsonQuery() {
  return GetStringJsonTests(/*sql_standard_mode=*/true,
                            /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsStringJsonExtract() {
  return GetStringJsonTests(/*sql_standard_mode=*/false,
                            /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsStringJsonValue() {
  return GetStringJsonTests(/*sql_standard_mode=*/true,
                            /*scalar_test_cases=*/true);
}

std::vector<FunctionTestCall> GetFunctionTestsStringJsonExtractScalar() {
  return GetStringJsonTests(/*sql_standard_mode=*/false,
                            /*scalar_test_cases=*/true);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonQuery() {
  return GetNativeJsonTests(/*sql_standard_mode=*/true,
                            /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonExtract() {
  return GetNativeJsonTests(/*sql_standard_mode=*/false,
                            /*scalar_test_cases=*/false);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonValue() {
  return GetNativeJsonTests(/*sql_standard_mode=*/true,
                            /*scalar_test_cases=*/true);
}

std::vector<FunctionTestCall> GetFunctionTestsNativeJsonExtractScalar() {
  return GetNativeJsonTests(/*sql_standard_mode=*/false,
                            /*scalar_test_cases=*/true);
}

std::vector<QueryParamsWithResult> GetFunctionTestsJsonIsNull() {
  std::vector<QueryParamsWithResult> v = {
      {{NullJson()}, True()},
      {{Json(JSONValue(1.1))}, False()},
      {{Json(JSONValue(std::string{"null"}))}, False()},
      {{Value::Null(types::JsonArrayType())}, True()},
  };
  return v;
}

// Add test cases when "lax" and "lax recursive" have the same result.
void JsonQueryLaxRecursiveSameResultTest(absl::string_view json_input,
                                         absl::string_view json_path,
                                         absl::string_view result,
                                         std::vector<FunctionTestCall>& tests) {
  const Value input_json = ParseJson(json_input);
  const Value result_json = ParseJson(result);
  tests.push_back({"json_query",
                   {input_json, String(absl::StrCat("lax ", json_path))},
                   result_json});
  tests.push_back(
      {"json_query",
       {input_json, String(absl::StrCat("lax recursive ", json_path))},
       result_json});
  tests.push_back(
      {"json_query",
       {input_json, String(absl::StrCat("Recursive LAX ", json_path))},
       result_json});
}

// Add test cases when "lax" and "lax recursive" have different result.
void JsonQueryLaxRecursiveDiffResultTest(absl::string_view json_input,
                                         absl::string_view json_path,
                                         absl::string_view lax_result,
                                         absl::string_view lax_recursive_result,
                                         std::vector<FunctionTestCall>& tests) {
  const Value input_json = ParseJson(json_input);
  tests.push_back({"json_query",
                   {input_json, String(absl::StrCat("lax ", json_path))},
                   ParseJson(lax_result)});
  const Value lax_recursive_result_json = ParseJson(lax_recursive_result);
  tests.push_back(
      {"json_query",
       {input_json, String(absl::StrCat("lax recursive ", json_path))},
       lax_recursive_result_json});
  tests.push_back(
      {"json_query",
       {input_json, String(absl::StrCat("recursive lax ", json_path))},
       lax_recursive_result_json});

  tests.push_back(
      {"json_query",
       {input_json, String(absl::StrCat("Recursive LAX ", json_path))},
       lax_recursive_result_json});
}

std::vector<FunctionTestCall> GetFunctionTestsJsonQueryLax() {
  std::vector<FunctionTestCall> tests;
  // Basic cases.
  //
  // Key doesn't exist.
  JsonQueryLaxRecursiveSameResultTest(R"({"a":{"b":1}})", "$.A", "[]", tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":{"b":1}})", "$.b", "[]", tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":{"b":1}})", "$.a.c", "[]", tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":{"b":1}})", "$[1]", "[]", tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":{"b":1}})", "$[0].b", "[]",
                                      tests);
  // NULL JSON value input.
  JsonQueryLaxRecursiveSameResultTest("null", "$", "[null]", tests);
  JsonQueryLaxRecursiveSameResultTest("null", "$.a", "[]", tests);
  JsonQueryLaxRecursiveSameResultTest("null", "$[0]", "[null]", tests);
  JsonQueryLaxRecursiveSameResultTest("null", "$[1]", "[]", tests);
  // Simple object match with matching types.
  JsonQueryLaxRecursiveSameResultTest(R"({"a":null})", "$", R"([{"a":null}])",
                                      tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":null})", "$.a", "[null]", tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":1, "b":2})", "$.b", "[2]", tests);
  // Simple array match.
  JsonQueryLaxRecursiveSameResultTest("[[null]]", "$", "[[[null]]]", tests);
  JsonQueryLaxRecursiveSameResultTest("[[null]]", "$[0]", "[[null]]", tests);
  // Index larger than array size.
  JsonQueryLaxRecursiveSameResultTest("[[null], 1]", "$[2]", "[]", tests);
  // Single level of array.
  JsonQueryLaxRecursiveSameResultTest(R"([{"a":1}])", "$.a", "[1]", tests);
  // 2-levels of arrays.
  JsonQueryLaxRecursiveDiffResultTest(R"([[{"a":1}]])", "$.a", "[]", "[1]",
                                      tests);
  // Mix of 1,2,3 levels of arrays.
  JsonQueryLaxRecursiveDiffResultTest(R"([{"a":1},[[{"a":2}]],[[[{"a":3}]]]])",
                                      "$.a", "[1]", "[1,2,3]", tests);
  JsonQueryLaxRecursiveDiffResultTest(
      R"([{"a":1}, {"b":2}, [[{"a":3}]],[[{"b":4}]],[[[{"a":5}]]]])", "$.b",
      "[2]", "[2,4]", tests);
  // Wrap non-array before match.
  JsonQueryLaxRecursiveSameResultTest(R"({"a":1})", "$[0].a", "[1]", tests);
  JsonQueryLaxRecursiveSameResultTest(R"({"a":1})", "$[0][0].a", "[1]", tests);
  // Key 'b' doesn't exist in matched JSON subtree.
  JsonQueryLaxRecursiveSameResultTest(R"({"a":1})", "$[0][0].b", "[]", tests);
  // Second index is larger than size of wrapped array.
  JsonQueryLaxRecursiveSameResultTest(R"({"a":1})", "$[0][1].a", "[]", tests);
  // Complex Cases
  //
  // 'b' is not included in every nested object.
  JsonQueryLaxRecursiveDiffResultTest(
      R"({"a":[{"b":1}, {"c":2}, {"b":3}, [{"b":4}]]})", "$.a.b", "[1,3]",
      "[1,3,4]", tests);
  // 'c' is not included in every nested object.
  JsonQueryLaxRecursiveDiffResultTest(
      R"({"a":[{"b":1}, {"c":2}, {"b":3}, [{"b":4}], null,
                     [[{"c":5}]]]})",
      "$.a.c", "[2]", "[2,5]", tests);
  // 'a.b' has different levels of nestedness
  JsonQueryLaxRecursiveDiffResultTest(
      R"({"a":[1, {"b":2}, [{"b":3}, null, 4, {"b":[5]}]]})", "$.a.b", "[2]",
      "[2, 3, [5]]", tests);
  // Both 'a' and 'a.b' have different levels of nestedness.
  JsonQueryLaxRecursiveDiffResultTest(
      R"([{"a":[1, {"b":2}, [{"b":3}, [{"b": 4}]]]},
           [{"a":{"b":5}}, null]])",
      "$.a.b", "[2]", "[2, 3, 4, 5]", tests);
  JsonQueryLaxRecursiveDiffResultTest(
      R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
               [{"a":{"b":5}}, null]])",
      "$.a[0].b", "[2]", "[2, 5]", tests);
  // Specific array indices and different levels of nestedness with
  // autowrap.
  JsonQueryLaxRecursiveSameResultTest(
      R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
               [{"a":{"b":5}}, null]])",
      "$[1].a[0].b", "[5]", tests);
  JsonQueryLaxRecursiveSameResultTest(
      R"([{"a":[{"b":2}, [{"b":3}, [{"b": 4}]]]},
               [{"a":{"b":5}}, null]])",
      "$[1].a[0][0][0].b", "[5]", tests);

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsParseJson() {
  // TODO: Currently these tests only verify if PARSE_JSON produces
  // the same output as JSONValue::ParseJsonString. A better test would need
  // to evaluate the output of PARSE_JSON either through JSONValueConstRef
  // accessors or through the JSON_VALUE SQL function.
  enum WideNumberMode { kExact = 0x1, kRound = 0x2 };
  struct ParseJsonTestCase {
    std::string json_to_parse;
    // Bit flag representing the wide number parsing modes supported by the
    // test. 'json_to_parse' will be parsed in all supported modes.
    uint8_t wide_number_mode_flag;
  };
  std::vector<ParseJsonTestCase> valid_json_tests = {
      // 'exact' or 'round' mode
      {"123", kExact | kRound},
      {"\"string\"", kExact | kRound},
      {"12.3", kExact | kRound},
      {"true", kExact | kRound},
      {"null", kExact | kRound},
      {"[1, true, null]", kExact | kRound},
      {"{\"a\" : [ {\"b\": \"c\"}, {\"b\": false}]}", kExact | kRound},
      {absl::StrCat("{", kDeepJsonString, "}"), kExact | kRound},
      {absl::StrCat("{", kWideJsonString, "}"), kExact | kRound},
      {"11111111111111111111", kExact | kRound},
      {"1.2345678901234568e+29", kExact | kRound},
      {R"("foo\t\\t\\\t\n\\nbar \"baz\\")", kExact | kRound},
      {"[[1, 2, 3], [\"a\", \"b\", \"c\"], [[true, false]]]", kExact | kRound},
      {"\"\u005C\u005C\u0301\u263a\u2028\"", kExact | kRound},
      {"\"你好\"", kExact | kRound},
      {"{\"%2526%7C%2B\": null}", kExact | kRound},
      // 'round' mode only
      {"123456789012345678901234567890", kRound},
      {R"({"x":11111111111111111111, "z":123456789012345678901234567890})",
       kRound},
      {"-0.0000002414214151379150123", kRound},
      {"989124899124.1241251252125121285", kRound},
      {"9891248991241241251252125121285021782188712512512", kRound}};
  std::vector<ParseJsonTestCase> invalid_json_tests = {
      // Invalid regardless of wide number mode
      {"{\"foo\": 12", kExact | kRound},
      {"\"", kExact | kRound},
      {"string", kExact | kRound},
      {"[1, 2", kExact | kRound},
      {"[\"a\" \"b\"]", kExact | kRound},
      {"'foo'", kExact | kRound},
      {"True", kExact | kRound},
      {"FALSE", kExact | kRound},
      {"NULL", kExact | kRound},
      {"nan", kExact | kRound},
      {R"("\")", kExact | kRound},
      {".3", kExact | kRound},
      {"4.", kExact | kRound},
      {"0x3", kExact | kRound},
      {"1.e", kExact | kRound},
      {R"("f1":"v1")", kExact | kRound},
      {R"("":"v1")", kExact | kRound},
      {"3]", kExact | kRound},
      {"5}", kExact | kRound},
      {"-", kExact | kRound},
      {"", kExact | kRound},
      {" ", kExact | kRound},
      // Fails parsing in both 'exact' and 'round'. Intentionally kept separate
      // from cases above to allow for handling `kStringify` when implemented.
      {"-1.79769313486232e+309", kExact | kRound},
      {"1.79769313486232e+309", kExact | kRound},
      // Fails parsing in 'exact'
      {"123456789012345678901234567890", kExact},
      {R"({"x":11111111111111111111, "z":123456789012345678901234567890})",
       kExact},
      {"-0.0000002414214151379150123", kExact},
      {"989124899124.1241251252125121285", kExact},
      {"9891248991241241251252125121285021782188712512512", kExact}};
  std::vector<FunctionTestCall> v;
  v.reserve(valid_json_tests.size() * 3 + invalid_json_tests.size() * 3 + 6);
  for (const ParseJsonTestCase& test : valid_json_tests) {
    if (test.wide_number_mode_flag & kExact) {
      // Add both the case where mode is specified and mode is not specified.
      v.push_back(
          {"parse_json",
           QueryParamsWithResult(
               {test.json_to_parse},
               Json(JSONValue::ParseJSONString(
                        test.json_to_parse,
                        {.wide_number_mode =
                             JSONParsingOptions::WideNumberMode::kExact})
                        .value()))
               .WrapWithFeature(FEATURE_JSON_TYPE)});
      v.push_back(
          {"parse_json",
           QueryParamsWithResult(
               {test.json_to_parse, "exact"},
               Json(JSONValue::ParseJSONString(
                        test.json_to_parse,
                        {.wide_number_mode =
                             JSONParsingOptions::WideNumberMode::kExact})
                        .value()))
               .WrapWithFeatureSet(
                   {FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
    }
    if (test.wide_number_mode_flag & kRound) {
      v.push_back(
          {"parse_json",
           QueryParamsWithResult(
               {test.json_to_parse, "round"},
               Json(JSONValue::ParseJSONString(test.json_to_parse).value()))
               .WrapWithFeatureSet(
                   {FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
    }
  }
  for (const ParseJsonTestCase& test : invalid_json_tests) {
    if (test.wide_number_mode_flag & kExact) {
      // Add both the case where mode is specified and mode is not specified.
      v.push_back({"parse_json", QueryParamsWithResult({test.json_to_parse},
                                                       NullJson(), OUT_OF_RANGE)
                                     .WrapWithFeature(FEATURE_JSON_TYPE)});
      v.push_back(
          {"parse_json", QueryParamsWithResult({test.json_to_parse, "exact"},
                                               NullJson(), OUT_OF_RANGE)
                             .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS,
                                                  FEATURE_JSON_TYPE})});
    }
    if (test.wide_number_mode_flag & kRound) {
      v.push_back(
          {"parse_json", QueryParamsWithResult({test.json_to_parse, "round"},
                                               NullJson(), OUT_OF_RANGE)
                             .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS,
                                                  FEATURE_JSON_TYPE})});
    }
  }
  // Fail if invalid 'wide_number_mode' specified. 'wide_number_mode' is
  // case-sensitive.
  v.push_back(
      {"parse_json",
       QueryParamsWithResult({"2.5", "junk"}, NullJson(), OUT_OF_RANGE)
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
  v.push_back(
      {"parse_json",
       QueryParamsWithResult({"2.5", "EXACT"}, NullJson(), OUT_OF_RANGE)
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
  v.push_back(
      {"parse_json",
       QueryParamsWithResult({"2.5", "Round"}, NullJson(), OUT_OF_RANGE)
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
  // Return NULL if either argument is NULL.
  v.push_back({"parse_json", QueryParamsWithResult({NullString()}, NullJson())
                                 .WrapWithFeatureSet({FEATURE_JSON_TYPE})});
  v.push_back(
      {"parse_json",
       QueryParamsWithResult({NullString(), "wide"}, NullJson())
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
  v.push_back(
      {"parse_json",
       QueryParamsWithResult({"25", NullString()}, NullJson())
           .WrapWithFeatureSet({FEATURE_NAMED_ARGUMENTS, FEATURE_JSON_TYPE})});
  v.push_back({"parse_json", QueryParamsWithResult({NullString()}, NullJson())
                                 .WrapWithFeature(FEATURE_JSON_TYPE)});
  return v;
}

std::vector<FunctionTestCall> GetFunctionTestsConvertJson() {
  std::vector<FunctionTestCall> tests = {
      // INT64
      {"int64", {NullJson()}, NullInt64()},
      {"int64", {Json(JSONValue(int64_t{123}))}, Int64(123)},
      {"int64", {Json(JSONValue(int64_t{-123}))}, Int64(-123)},
      {"int64", {Json(JSONValue(10.0))}, Int64(10)},
      {"int64",
       {Json(JSONValue(std::numeric_limits<int64_t>::min()))},
       Int64(std::numeric_limits<int64_t>::min())},
      {"int64",
       {Json(JSONValue(std::numeric_limits<int64_t>::max()))},
       Int64(std::numeric_limits<int64_t>::max())},
      {"int64", {Json(JSONValue(1e100))}, NullInt64(), OUT_OF_RANGE},
      {"int64",
       {Json(JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}))},
       NullInt64(),
       OUT_OF_RANGE},
      {"int64",
       {Json(JSONValue(std::string{"123"}))},
       NullInt64(),
       OUT_OF_RANGE},
      {"int64",
       QueryParamsWithResult({Json(*JSONValue::ParseJSONString("123"))},
                             Int64(123))
           .WrapWithFeatureSet(
               {FEATURE_JSON_TYPE, FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS})},
      {"int64", {Json(JSONValue(10.1))}, NullInt64(), OUT_OF_RANGE},
      // BOOL
      {"bool", {NullJson()}, NullBool()},
      {"bool", {Json(JSONValue(false))}, Value::Bool(false)},
      {"bool", {Json(JSONValue(true))}, Value::Bool(true)},
      // STRING
      {"string", {NullJson()}, NullString()},
      {"string", {Json(JSONValue(std::string{""}))}, ""},
      {"string", {Json(JSONValue(std::string{"TesT"}))}, "TesT"},
      {"string", {Json(JSONValue(std::string{"1"}))}, "1"},
      {"string", {Json(JSONValue(std::string{"abc123"}))}, "abc123"},
      {"string", {Json(JSONValue(std::string{"12¿©?Æ"}))}, "12¿©?Æ"},
      // TYPE
      {"json_type", {NullJson()}, NullString()},
      {"json_type", {Json(JSONValue())}, Value::String("null")},
      {"json_type", {Json(JSONValue(std::string{"10"}))}, "string"},
      {"json_type", {Json(JSONValue(int64_t{1}))}, "number"},
      {"json_type", {Json(JSONValue(2.0))}, "number"},
      {"json_type", {Json(JSONValue(-1.0))}, "number"},
      {"json_type",
       {Json(JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}))},
       "number"},
      {"json_type", {Json(JSONValue(true))}, "boolean"},
      {"json_type",
       {Json(JSONValue::ParseJSONString(R"({"a": 1})").value())},
       "object"},
      {"json_type",
       {Json(JSONValue::ParseJSONString(R"([10,20])").value())},
       "array"},
      {"json_type",
       {Json(JSONValue::ParseJSONString(R"({"a": {"b": {"c": 1}}, "d": 4})")
                 .value())},

       "object"},
      {"json_type",
       {Json(JSONValue::ParseJSONString(
                 R"([{"a": {"b": {"c": 1}}, "d": 4}, {"e": 1}])")
                 .value())},
       "array"},
  };
  std::vector<QueryParamsWithResult> float64_tests = {
      {{NullJson()}, NullDouble()},
      QueryParamsWithResult({Json(JSONValue(1.0)), NullString()}, NullDouble())
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      // Passing
      {{Json(JSONValue(1.0))}, Double(1.0)},
      {{Json(JSONValue(-1.0))}, Double(-1.0)},
      {{Json(JSONValue(std::numeric_limits<double>::min()))},
       std::numeric_limits<double>::min()},
      {{Json(JSONValue(std::numeric_limits<double>::max()))},
       std::numeric_limits<double>::max()},
      QueryParamsWithResult({Json(JSONValue(1.0)), "round"}, Double(1.0))
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      QueryParamsWithResult({Json(JSONValue(1.0)), "exact"}, Double(1.0))
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      // Fails because value is case-sensitive and not in ["exact", "round"]
      QueryParamsWithResult({Json(JSONValue(1.0)), "EXACT"}, NullDouble(),
                            OUT_OF_RANGE)
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),

      QueryParamsWithResult(
          {Json(JSONValue(std::numeric_limits<uint64_t>::max())), "exact"},
          NullDouble(), OUT_OF_RANGE)
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      QueryParamsWithResult(
          {Json(JSONValue(std::numeric_limits<uint64_t>::max())), "round"},
          Double(1.8446744073709552e+19))
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      QueryParamsWithResult({Json(JSONValue(1.0)), NullString()}, NullDouble())
          .AddRequiredFeatures({FEATURE_JSON_TYPE, FEATURE_NAMED_ARGUMENTS,
                                FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      // Other types should return an error
      {{Json(JSONValue(std::string{"TesT"}))}, NullDouble(), OUT_OF_RANGE},
      {{Json(JSONValue(std::string{"3"}))}, NullDouble(), OUT_OF_RANGE},
      {{Json(JSONValue(false))}, NullDouble(), OUT_OF_RANGE},
      QueryParamsWithResult(
          {Json(JSONValue::ParseJSONString(R"({"a": 1})").value())},
          NullDouble(), OUT_OF_RANGE)
          .AddRequiredFeatures(
              {FEATURE_JSON_TYPE, FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
      QueryParamsWithResult(
          {Json(JSONValue::ParseJSONString(R"([10, 20])").value())},
          NullDouble(), OUT_OF_RANGE)
          .AddRequiredFeatures(
              {FEATURE_JSON_TYPE, FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS}),
  };
  for (const auto& test : float64_tests) {
    tests.push_back({"float64", test});
    tests.push_back({"double", test});
  }
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsConvertJsonIncompatibleTypes() {
  std::vector<std::string> type_functions = {"int64", "float64", "string",
                                             "bool"};
  std::map<std::string, ValueConstructor> null_lookup = {
      {"int64", NullInt64()},
      {"float64", NullDouble()},
      {"string", NullString()},
      {"bool", NullBool()}};
  std::map<std::string, ValueConstructor> input_type_value_lookup = {
      {"object", Json(JSONValue::ParseJSONString(R"({"a": 2})").value())},
      {"array", Json(JSONValue::ParseJSONString(R"([20, 30])").value())},
      {"bool", Json(JSONValue(true))},
      {"float64",
       {Json(JSONValue(
           2.1))}},  // incompatible with int64 due to its fractional part
      {"int64", Json(JSONValue(int64_t{321}))},
      {"string", Json(JSONValue(std::string{"abc321"}))}};

  std::vector<FunctionTestCall> tests;
  for (const std::string& type_function : type_functions) {
    for (const auto& [input_type, input_value] : input_type_value_lookup) {
      if (type_function != input_type &&
          // float64(int64) is compatible
          !(type_function == "float64" && input_type == "int64")) {
        std::vector<ValueConstructor> input = {input_value};
        tests.emplace_back(type_function, input,
                           null_lookup.find(type_function)->second,
                           OUT_OF_RANGE);
      }
    }
  }
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsConvertJsonLaxBool() {
  std::vector<FunctionTestCall> tests = {
      // BOOL
      {"lax_bool", {Json(JSONValue(true))}, Value::Bool(true)},
      {"lax_bool", {Json(JSONValue(false))}, Value::Bool(false)},
      // STRINGS
      {"lax_bool", {Json(JSONValue(std::string{"TRue"}))}, Value::Bool(true)},
      {"lax_bool", {Json(JSONValue(std::string{"false"}))}, Value::Bool(false)},
      {"lax_bool", {Json(JSONValue(std::string{"foo"}))}, NullBool()},
      // NUMERIC. Note that -inf, inf, and NaN are not valid JSON numeric
      // values.
      {"lax_bool", {Json(JSONValue(int64_t{0}))}, Value::Bool(false)},
      {"lax_bool", {Json(JSONValue(int64_t{-1}))}, Value::Bool(true)},
      {"lax_bool",
       {Json(JSONValue(int64_t{std::numeric_limits<int64_t>::min()}))},
       Value::Bool(true)},
      {"lax_bool",
       {Json(JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}))},
       Value::Bool(true)},
      {"lax_bool", {Json(JSONValue(double{1.1}))}, Value::Bool(true)},
      {"lax_bool", {Json(JSONValue(double{-1.1}))}, Value::Bool(true)},
      {"lax_bool",
       {Json(JSONValue::ParseJSONString("-0.0e2").value())},
       Value::Bool(false)},
      // Object/Array/Null
      {"lax_bool", {Json(JSONValue())}, NullBool()},
      {"lax_bool",
       {Json(JSONValue::ParseJSONString(R"({"a": 1})").value())},
       NullBool()},
      {"lax_bool",
       {Json(JSONValue::ParseJSONString(R"([1])").value())},
       NullBool()},
      {"lax_bool", {NullJson()}, NullBool()}};
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsConvertJsonLaxInt64() {
  std::vector<FunctionTestCall> tests = {
      // BOOLS
      {"lax_int64", {Json(JSONValue(true))}, Value::Int64(1)},
      {"lax_int64", {Json(JSONValue(false))}, Value::Int64(0)},
      // STRINGS
      {"lax_int64", {Json(JSONValue(std::string{"10"}))}, Value::Int64(10)},
      {"lax_int64", {Json(JSONValue(std::string{"1.1"}))}, Value::Int64(1)},
      {"lax_int64", {Json(JSONValue(std::string{"1.1e2"}))}, Value::Int64(110)},
      {"lax_int64", {Json(JSONValue(std::string{"+1.5"}))}, Value::Int64(2)},
      {"lax_int64",
       {Json(JSONValue(std::string{"123456789012345678.0"}))},
       Value::Int64(123456789012345678)},
      {"lax_int64", {Json(JSONValue(std::string{"foo"}))}, NullInt64()},
      {"lax_int64", {Json(JSONValue(std::string{"1e100"}))}, NullInt64()},
      // NUMERIC. Note that -inf, inf, and NaN are not valid JSON numeric
      // values.
      {"lax_int64", {Json(JSONValue(int64_t{-10}))}, Value::Int64(-10)},
      {"lax_int64",
       {Json(JSONValue(int64_t{std::numeric_limits<int64_t>::min()}))},
       Value::Int64(std::numeric_limits<int64_t>::min())},
      {"lax_int64",
       {Json(JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}))},
       NullInt64()},
      {"lax_int64", {Json(JSONValue(double{1.1}))}, Value::Int64(1)},
      {"lax_int64", {Json(JSONValue(double{1.1e2}))}, Value::Int64(110)},
      {"lax_int64",
       {Json(JSONValue(double{123456789012345678.0}))},
       Value::Int64(123456789012345680)},
      {"lax_int64",
       {Json(JSONValue(double{std::numeric_limits<double>::lowest()}))},
       Value::NullInt64()},
      {"lax_int64",
       {Json(JSONValue(double{std::numeric_limits<double>::max()}))},
       Value::NullInt64()},
      // Object/Array/Null
      {"lax_int64", {Json(JSONValue())}, NullInt64()},
      {"lax_int64",
       {Json(JSONValue::ParseJSONString(R"({"a": 1})").value())},
       NullInt64()},
      {"lax_int64",
       {Json(JSONValue::ParseJSONString(R"([1])").value())},
       NullInt64()},
      {"lax_int64", {NullJson()}, NullInt64()}};
  return tests;
}

namespace {

std::vector<QueryParamsWithResult> GetTestsForConvertJsonLaxDouble() {
  std::vector<QueryParamsWithResult> tests = {
      // BOOLS
      {{Json(JSONValue(true))}, NullDouble()},
      {{Json(JSONValue(false))}, NullDouble()},
      // STRING
      {{Json(JSONValue(std::string("10")))}, Value::Double(10.0)},
      {{Json(JSONValue(std::string("-10")))}, Value::Double(-10.0)},
      {{Json(JSONValue(std::string("1.1")))}, Value::Double(1.1)},
      {{Json(JSONValue(std::string("1.1e2")))}, Value::Double(110.0)},
      {{Json(JSONValue(std::string("-10")))}, Value::Double(-10.0)},
      {{Json(JSONValue(std::string("9007199254740993")))},
       Value::Double(9007199254740992.0)},
      {{Json(JSONValue(std::string("foo")))}, NullDouble()},
      {{Json(JSONValue(std::string("NaN")))}, Value::Double(std::nan(""))},
      {{Json(JSONValue(std::string("inf")))},
       Value::Double(std::numeric_limits<double>::infinity())},
      {{Json(JSONValue(std::string("-inf")))},
       Value::Double(-1 * std::numeric_limits<double>::infinity())},
      // NUMBERS. Note that -inf, inf, and NaN are not valid JSON numeric
      // values.
      {{Json(JSONValue(int64_t{-10}))}, Value::Double(-10.0)},
      {{Json(JSONValue(int64_t{9007199254740993}))},
       Value::Double(9007199254740992)},
      {{Json(JSONValue(int64_t{std::numeric_limits<int64_t>::min()}))},
       Value::Double(std::numeric_limits<int64_t>::min())},
      {{Json(JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}))},
       Value::Double(std::numeric_limits<uint64_t>::max())},
      {{Json(JSONValue(double{1.1}))}, Value::Double(1.1)},
      {{Json(JSONValue(double{-1.1}))}, Value::Double(-1.1)},
      {{Json(JSONValue(double{std::numeric_limits<double>::max()}))},
       Value::Double(std::numeric_limits<double>::max())},
      // Object/Array/Null
      {{Json(JSONValue())}, NullDouble()},
      {{Json(JSONValue::ParseJSONString(R"({"a": 1})").value())}, NullDouble()},
      {{Json(JSONValue::ParseJSONString(R"([1])").value())}, NullDouble()},
      {{NullJson()}, NullDouble()}};
  return tests;
}

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsConvertJsonLaxFloat64() {
  std::vector<FunctionTestCall> tests;
  for (auto& test : GetTestsForConvertJsonLaxDouble()) {
    tests.push_back({"lax_float64", std::move(test)});
  }
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsConvertJsonLaxDouble() {
  std::vector<FunctionTestCall> tests;
  for (auto& test : GetTestsForConvertJsonLaxDouble()) {
    tests.push_back({"lax_double", std::move(test)});
  }
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsConvertJsonLaxString() {
  std::vector<FunctionTestCall> tests = {
      // BOOLS
      {"lax_string", {Json(JSONValue(true))}, Value::String("true")},
      {"lax_string", {Json(JSONValue(false))}, Value::String("false")},
      // STRINGS
      {"lax_string",
       {Json(JSONValue(std::string{"foo"}))},
       Value::String("foo")},
      {"lax_string", {Json(JSONValue(std::string{"10"}))}, Value::String("10")},
      // NUMERIC. Note that -inf, inf, and NaN are not valid JSON numeric
      // values.
      {"lax_string", {Json(JSONValue(int64_t{-10}))}, Value::String("-10")},
      {"lax_string", {Json(JSONValue(int64_t{0}))}, Value::String("0")},
      {"lax_string", {Json(JSONValue(int64_t{-0}))}, Value::String("0")},
      {"lax_string",
       {Json(JSONValue(int64_t{std::numeric_limits<int64_t>::min()}))},
       Value::String(absl::StrCat(std::numeric_limits<int64_t>::min()))},
      {"lax_string",
       {Json(JSONValue(int64_t{std::numeric_limits<int64_t>::max()}))},
       Value::String(absl::StrCat(std::numeric_limits<int64_t>::max()))},
      {"lax_string",
       {Json(JSONValue(uint64_t{std::numeric_limits<uint64_t>::max()}))},
       Value::String(absl::StrCat(std::numeric_limits<uint64_t>::max()))},
      {"lax_string", {Json(JSONValue(double{0.0}))}, Value::String("0")},
      {"lax_string", {Json(JSONValue(double{-0.0}))}, Value::String("0")},
      {"lax_string", {Json(JSONValue(double{1.1}))}, Value::String("1.1")},
      {"lax_string",
       {Json(JSONValue(double{std::numeric_limits<double>::min()}))},
       Value::String("2.2250738585072014e-308")},
      {"lax_string",
       {Json(JSONValue(double{std::numeric_limits<double>::lowest()}))},
       Value::String("-1.7976931348623157e+308")},
      {"lax_string",
       {Json(JSONValue(double{std::numeric_limits<double>::max()}))},
       Value::String("1.7976931348623157e+308")},
      {"lax_string",
       {Json(JSONValue::ParseJSONString("1e100").value())},
       Value::String("1e+100")},
      // Object/Array/Null
      {"lax_string", {Json(JSONValue())}, NullString()},
      {"lax_string",
       {Json(JSONValue::ParseJSONString(R"({"a": 1})").value())},
       NullString()},
      {"lax_string",
       {Json(JSONValue::ParseJSONString(R"([1])").value())},
       NullString()},
      {"lax_string", {NullJson()}, NullString()}};
  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsJsonArray() {
  std::vector<FunctionTestCall> tests;
  // One argument to JSON_ARRAY. Test cases from TO_JSON to make sure JSON_ARRAY
  // applies TO_JSON semantics to arguments.
  for (FunctionTestCall& test : GetFunctionTestsToJson()) {
    if (test.params.num_params() == 2) {
      if (test.params.param(1).is_null() || test.params.param(1).bool_value()) {
        // No stringify mode in JSON_ARRAY.
        continue;
      }
    }

    auto features_set = test.params.required_features();
    features_set.erase(FEATURE_NAMED_ARGUMENTS);

    if (test.params.status().ok()) {
      googlesql::JSONValue json_result;
      json_result.GetRef().GetArrayElement(0).Set(
          JSONValue::CopyFrom(test.params.result().json_value()));
      Value result = Json(std::move(json_result));
      tests.push_back(
          {"json_array",
           QueryParamsWithResult({std::move(test.params.param(0))}, result)
               .AddRequiredFeatures(features_set)});
    } else {
      tests.push_back({"json_array",
                       QueryParamsWithResult({std::move(test.params.param(0))},
                                             NullJson(), test.params.status())
                           .AddRequiredFeatures(features_set)});
    }
  }

  // 0 argument
  tests.push_back(
      {"json_array", QueryParamsWithResult(
                         {}, Json(JSONValue::ParseJSONString("[]").value()))});
  // 1 argument
  tests.push_back(
      {"json_array", QueryParamsWithResult(
                         {Int64Array({})},
                         Json(JSONValue::ParseJSONString("[[]]").value()))});
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {Int64Array({10, -123, 156243})},
           Json(JSONValue::ParseJSONString("[[10,-123,156243]]").value()))});
  tests.push_back(
      {"json_array", QueryParamsWithResult(
                         {Struct({}, {})},
                         Json(JSONValue::ParseJSONString("[{}]").value()))});
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {Struct({"x", "y", "欢迎"}, {Double(10.126), String("hello"),
                                        Int64Array({10, 230, -12})})},
           Json(JSONValue::ParseJSONString(
                    R"([{"欢迎":[10,230,-12],"x":10.126,"y":"hello"}])")
                    .value()))});
  // 2+ arguments
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {NullInt64(), NullString()},
           Json(JSONValue::ParseJSONString("[null,null]").value()))});
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {10, "foo"},
           Json(JSONValue::ParseJSONString(R"([10,"foo"])").value()))});
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {Int64Array({10, -123, 156243}), NullInt64(), "foo", BoolArray({}),
            StringArray({"test", "", "bar"})},
           Json(JSONValue::ParseJSONString(
                    R"([[10,-123,156243],null,"foo",[],["test","","bar"]])")
                    .value()))});
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {10, "foo",
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           Json(JSONValue::ParseJSONString(R"([10,"foo",123.1234569817232])")
                    .value()))
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)});
  // Strict number parsing
  tests.push_back(
      {"json_array",
       QueryParamsWithResult(
           {10, "foo",
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           NullJson(), OUT_OF_RANGE)
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)
           .AddRequiredFeature(FEATURE_JSON_STRICT_NUMBER_PARSING)});
  tests.push_back({"json_array",
                   QueryParamsWithResult(
                       {10, NullString(),
                        Json(JSONValue::ParseJSONString(
                                 R"({"a": 123, "b": {"dd": [null, true]}})")
                                 .value())},
                       Json(JSONValue::ParseJSONString(
                                R"([10,null,{"a":123,"b":{"dd":[null,true]}}])")
                                .value()))});
  return tests;
}

// Signature: JSON_OBJECT(STRING key, ANY value, ...)
std::vector<FunctionTestCall> GetFunctionTestsJsonObject(
    bool include_null_key_tests) {
  std::vector<FunctionTestCall> tests;
  // Test cases from TO_JSON to make sure JSON_OBJECT applies TO_JSON semantics
  // to arguments.
  for (FunctionTestCall& test : GetFunctionTestsToJson()) {
    if (test.params.num_params() == 2) {
      if (test.params.param(1).is_null() || test.params.param(1).bool_value()) {
        // No stringify mode in JSON_OBJECT.
        continue;
      }
    }
    auto features_set = test.params.required_features();
    features_set.erase(FEATURE_NAMED_ARGUMENTS);

    if (test.params.status().ok()) {
      googlesql::JSONValue json_result;
      json_result.GetRef().GetMember("field").Set(
          JSONValue::CopyFrom(test.params.result().json_value()));
      Value result = Json(std::move(json_result));
      tests.push_back(
          {"json_object", QueryParamsWithResult(
                              {String("field"), test.params.param(0)}, result)
                              .AddRequiredFeatures(features_set)});
    } else {
      tests.push_back({"json_object",
                       QueryParamsWithResult(
                           {String("field"), std::move(test.params.param(0))},
                           NullJson(), test.params.status())
                           .AddRequiredFeatures(features_set)});
    }
  }

  // Error: NULL key
  if (include_null_key_tests) {
    tests.push_back(
        {"json_object",
         QueryParamsWithResult({NullString(), 10}, NullJson(), OUT_OF_RANGE)});
    tests.push_back(
        {"json_object", QueryParamsWithResult({"a", 10, NullString(), 10},
                                              NullJson(), OUT_OF_RANGE)});
  }
  // Note: Different number of keys and values with the JSON_OBJECT(STRING, ANY,
  // ...) signature is tested in json_queries.test, as it would result in no
  // signature matched in this test.

  // 0 argument
  tests.push_back(
      {"json_object", QueryParamsWithResult(
                          {}, Json(JSONValue::ParseJSONString("{}").value()))});

  // SQL NULL values
  tests.push_back(
      {"json_object",
       QueryParamsWithResult(
           {"a", 10, "b", NullInt64(), "c", NullJson()},
           Json(JSONValue::ParseJSONString(R"({"a":10,"b":null,"c":null})")
                    .value()))});

  // Duplicate keys
  tests.push_back(
      {"json_object",
       QueryParamsWithResult(
           {"a", 10, "b", NullInt64(), "b", "foo", "a", true, "a", 2, "c",
            1.23},
           Json(JSONValue::ParseJSONString(R"({"a":10,"b":null,"c":1.23})")
                    .value()))});

  // Complex values and UTF-8 keys
  tests.push_back(
      {"json_object",
       QueryParamsWithResult(
           {"!@#<>{}", Int64Array({10, -123, 156243}), "Œuf",
            Json(JSONValue::ParseJSONString(
                     R"({"1‰": [true, null, {"b": "foo"}]})")
                     .value()),
            "Çζ", "β"},
           Json(JSONValue::ParseJSONString(R"({"!@#<>{}":[10,-123,156243],
           "Œuf":{"1‰":[true,null,{"b":"foo"}]},"Çζ":"β"})")
                    .value()))});

  return tests;
}

// Signature: JSON_OBJECT(ARRAY<STRING> keys, ARRAY<ANY> values)
std::vector<FunctionTestCall> GetFunctionTestsJsonObjectArrays(
    bool include_null_key_tests) {
  std::vector<FunctionTestCall> tests;
  // Test cases from TO_JSON to make sure JSON_OBJECT applies TO_JSON semantics
  // to arguments.
  for (FunctionTestCall& test : GetFunctionTestsToJson()) {
    if (test.params.num_params() == 2) {
      if (test.params.param(1).is_null() || test.params.param(1).bool_value()) {
        // No stringify mode in JSON_OBJECT.
        continue;
      }
    }
    auto features_set = test.params.required_features();
    features_set.erase(FEATURE_NAMED_ARGUMENTS);

    const ArrayType* array_type;
    if (auto status = test_values::static_type_factory()->MakeArrayType(
            test.params.param(0).type(), &array_type);
        !status.ok()) {
      continue;
    }

    if (test.params.status().ok()) {
      googlesql::JSONValue json_result;
      json_result.GetRef().GetMember("field").Set(
          JSONValue::CopyFrom(test.params.result().json_value()));
      Value result = Json(std::move(json_result));
      tests.push_back(
          {"json_object",
           QueryParamsWithResult(
               {StringArray({"field"}),
                values::Array(array_type, {std::move(test.params.param(0))})},
               result)
               .AddRequiredFeatures(features_set)});
    } else {
      tests.push_back(
          {"json_object",
           QueryParamsWithResult(
               {StringArray({"field"}),
                values::Array(array_type, {std::move(test.params.param(0))})},
               NullJson(), test.params.status())
               .AddRequiredFeatures(features_set)});
    }
  }

  // Error: NULL key
  if (include_null_key_tests) {
    tests.push_back(
        {"json_object", QueryParamsWithResult(
                            {values::Array(StringArrayType(), {NullString()}),
                             Int64Array({10})},
                            NullJson(), OUT_OF_RANGE)});
    tests.push_back(
        {"json_object",
         QueryParamsWithResult(
             {values::Array(StringArrayType(), {String("field"), NullString()}),
              Int64Array({10, 20})},
             NullJson(), OUT_OF_RANGE)});
    tests.push_back(
        {"json_object",
         QueryParamsWithResult(
             {values::Array(StringArrayType(), {String("field"), NullString()}),
              Int64Array({10})},
             NullJson(), OUT_OF_RANGE)});
  }

  // Error : keys and values array size differ.
  tests.push_back(
      {"json_object",
       QueryParamsWithResult({StringArray({"a", "b"}), Int64Array({10})},
                             NullJson(), OUT_OF_RANGE)});
  tests.push_back(
      {"json_object", QueryParamsWithResult(
                          {StringArray({"a", "b"}), Int64Array({10, 20, 30})},
                          NullJson(), OUT_OF_RANGE)});

  // 0 argument
  {
    std::vector<std::string> keys;
    tests.push_back(
        {"json_object", QueryParamsWithResult(
                            {StringArray(keys), Int64Array({})},
                            Json(JSONValue::ParseJSONString("{}").value()))});
  }

  // SQL NULL values
  tests.push_back(
      {"json_object",
       QueryParamsWithResult(
           {StringArray({"a", "b", "c"}),
            values::Array(StringArrayType(),
                          {NullString(), String("test"), NullString()})},
           Json(JSONValue::ParseJSONString(R"({"a":null,"b":"test","c":null})")
                    .value()))});

  // Duplicate keys
  tests.push_back(
      {"json_object",
       QueryParamsWithResult(
           {StringArray({"a", "a", "b", "a", "c"}),
            values::Array(StringArrayType(),
                          {NullString(), String("test"), String("hi"),
                           String("foo"), String("bar")})},
           Json(JSONValue::ParseJSONString(R"({"a":null,"b":"hi","c":"bar"})")
                    .value()))});

  // UTF-8 keys
  tests.push_back(
      {"json_object",
       QueryParamsWithResult({StringArray({"!@#<>{}", "Œuf", "Çζ"}),
                              BoolArray({true, false, true})},
                             Json(JSONValue::ParseJSONString(R"({"!@#<>{}":true,
           "Œuf":false,"Çζ":true})")
                                      .value()))});

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsJsonRemove() {
  absl::string_view json_string =
      R"({"a": 10, "b": [true, ["foo", null, "bar"], {"c": [20]}]})";
  std::vector<FunctionTestCall> tests = {
      // All NULLs.
      {"json_remove", {NullJson(), NullString()}, NullJson()},
      // NULL JSON input.
      {"json_remove", {NullJson(), String("$.a")}, NullJson()},
      // NULL JSONPath. Ignore Operation.
      {"json_remove",
       {ParseJson(json_string), NullString()},
       ParseJson(json_string)},
      // Mix of both NULL and Non-NULL JSONPath.
      {"json_remove",
       {ParseJson(json_string), String("$.a"), NullString()},
       ParseJson(R"({"b": [true, ["foo", null, "bar"], {"c": [20]}]})")},
      {"json_remove",
       {ParseJson(json_string), NullString(), String("$.a")},
       ParseJson(R"({"b": [true, ["foo", null, "bar"], {"c": [20]}]})")},
      // Invalid JSONPath.
      {"json_remove",
       {ParseJson(R"({"a": 10})"), String("$")},
       NullJson(),
       OUT_OF_RANGE},
      {"json_remove",
       {ParseJson(R"({"a": 10})"), String("$a")},
       NullJson(),
       OUT_OF_RANGE},
      {"json_remove",
       {ParseJson(R"({"a": 10})"), String("$.a"), String("$")},
       NullJson(),
       OUT_OF_RANGE},
      // Invalid and NULL JSONPath.
      {"json_remove",
       {ParseJson(R"({"a": 10})"), NullString(), String("$")},
       NullJson(),
       OUT_OF_RANGE},
      // Invalid JSONPath and NULL input JSON.
      {"json_remove",
       {NullJson(), NullString(), String("$$")},
       NullJson(),
       OUT_OF_RANGE},
      // 1 JSONPath
      // Member doesn't exist
      {"json_remove",
       {ParseJson(json_string), String("$.c")},
       ParseJson(json_string)},
      // Member on the path doesn't exist
      {"json_remove",
       {ParseJson(json_string), String("$.b[3].a.b")},
       ParseJson(json_string)},
      // Path '$.b.a' is not an object
      {"json_remove",
       {ParseJson(json_string), String("$.b.a[0]")},
       ParseJson(json_string)},
      // Array index doesn't exist
      {"json_remove",
       {ParseJson(json_string), String("$.b[4]")},
       ParseJson(json_string)},
      // Array index on the path doesn't exist
      {"json_remove",
       {ParseJson(json_string), String("$.b[4].c")},
       ParseJson(json_string)},
      // Path '$.a[0]' is not an array
      {"json_remove",
       {ParseJson(json_string), String("$.a[0].b")},
       ParseJson(json_string)},
      // Valid member removal
      {"json_remove",
       {ParseJson(json_string), String("$.a")},
       ParseJson(R"({"b":[true,["foo",null,"bar"],{"c":[20]}]})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b")},
       ParseJson(R"({"a":10})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b[2].c")},
       ParseJson(R"({"a":10,"b":[true,["foo",null,"bar"],{}]})")},
      // Valid array index removal
      {"json_remove",
       {ParseJson(json_string), String("$.b[0]")},
       ParseJson(R"({"a":10,"b":[["foo",null,"bar"],{"c":[20]}]})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b[2].c[0]")},
       ParseJson(R"({"a":10,"b":[true,["foo",null,"bar"],{"c":[]}]})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b[1]")},
       ParseJson(R"({"a":10,"b":[true,{"c":[20]}]})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b[1][0]")},
       ParseJson(R"({"a":10,"b":[true,[null,"bar"],{"c":[20]}]})")},
      // 2+ JSONPaths
      {"json_remove",
       {ParseJson(json_string), String("$.a"), String("$.b")},
       ParseJson(R"({})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b[0]"), String("$.b[0]")},
       ParseJson(R"({"a":10,"b":[{"c":[20]}]})")},
      {"json_remove",
       {ParseJson(json_string), String("$.b[2].c"), String("$.b[2].c[0]")},
       ParseJson(R"({"a":10,"b":[true,["foo",null,"bar"],{}]})")},
      {"json_remove",
       {ParseJson(json_string), String("$.a"), String("$.b[0]"),
        String("$.b[0][1]")},
       ParseJson(R"({"b":[["foo","bar"],{"c":[20]}]})")},
  };

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsJsonArrayInsert() {
  std::vector<FunctionTestCall> tests;
  constexpr absl::string_view kInsertEachElement = "insert_each_element";
  // One argument to JSON_ARRAY_INSERT. Test cases from TO_JSON to make sure
  // JSON_ARRAY_INSERT applies TO_JSON semantics to arguments.
  for (FunctionTestCall& test : GetFunctionTestsToJson()) {
    if (test.params.num_params() == 2) {
      if (test.params.param(1).is_null() || test.params.param(1).bool_value()) {
        // No stringify mode in JSON_ARRAY_INSERT.
        continue;
      }
    }

    auto features_set = test.params.required_features();
    features_set.insert(FEATURE_NAMED_ARGUMENTS);

    absl::string_view json_string = R"({"a": [1, "foo"]})";
    Value input = ParseJson(json_string);
    absl::string_view json_path = "$.a[1]";

    if (test.params.status().ok()) {
      googlesql::JSONValue json_result =
          googlesql::JSONValue::ParseJSONString(json_string).value();
      GOOGLESQL_CHECK_OK(json_result.GetRef().GetMember("a").InsertArrayElement(
          JSONValue::CopyFrom(test.params.result().json_value()), 1));
      Value result = Json(std::move(json_result));
      tests.push_back(
          {"json_array_insert",
           QueryParamsWithResult({std::move(input), String(json_path),
                                  std::move(test.params.param(0))},
                                 result)
               .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
               .AddRequiredFeatures(features_set)});
    } else {
      tests.push_back(
          {"json_array_insert",
           QueryParamsWithResult({std::move(input), String(json_path),
                                  std::move(test.params.param(0))},
                                 NullJson(), test.params.status())
               .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
               .AddRequiredFeatures(features_set)});
    }
  }

  absl::string_view json_string =
      R"([[1, 2, 3], true, {"a": [1.1,[["foo"]]]}])";

  // NULL input JSON.
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({NullJson(), String("$[0]"), Int64(1)},
                                         NullJson())});
  // NULL JSONPath.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(json_string), NullString(), Int64(1)},
                             ParseJson(json_string))});
  // NULL input JSON and NULL JSONPath.
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({NullJson(), NullString(), Int64(1)},
                                         NullJson())});
  // Both NULL and non-null JSONPath.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64(1), NullString(),
            Int64(2)},
           ParseJson(R"([1, [1, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), NullString(), Int64(2), String("$[0]"),
            Int64(1)},
           ParseJson(R"([1, [1, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  // Both NULL and non-null JSONPath with INSERT_EACH_ELEMENT.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64Array({1, 2}),
            NullString(), Int64(2)},
           ParseJson(R"([1, 2, [1, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL insert_each_element.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(json_string), String("$[0]"), Int64(1)},
                             ParseJson(json_string))
           .SetNamedValueArguments({{kInsertEachElement, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL input JSON and NULL insert_each_element.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({NullJson(), String("$[0]"), Int64(1)}, NullJson())
           .SetNamedValueArguments({{kInsertEachElement, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Invalid JSONPath.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(json_string), String("$$"), Int64(1)},
                             NullJson(), OUT_OF_RANGE)});
  // Invalid JSONPath with NULL input JSON.
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({NullJson(), String("$$"), Int64(1)},
                                         NullJson(), OUT_OF_RANGE)});
  // Invalid second JSONPath with NULL input JSON.
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({NullJson(), String("$.a"), Int64(2),
                                          String("$$"), Int64(1)},
                                         NullJson(), OUT_OF_RANGE)});
  // Invalid JSONPath with NULL insert_each_element.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {NullJson(), String("$"), Int64(1), String("$$"), Int64(2)},
           NullJson(), OUT_OF_RANGE)
           .SetNamedValueArguments({{kInsertEachElement, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL array value for insertion. The insertion is ignored because
  // `insert_each_element` = true.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(json_string), String("$[0]"),
                              Value::Null(types::StringArrayType())},
                             ParseJson(json_string))
           .SetNamedValueArguments({{kInsertEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Successful NULL array value insertion.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            Value::Null(types::StringArrayType())},
           ParseJson(R"([null, [1, 2, 3], true, {"a": [1.1,[["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Two inserts. The first inserts a NULL array which is ignored because
  // `insert_each_element` = true, and the second insertion is successful.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            Value::Null(types::StringArrayType()), String("$[0][1]"),
            Int64Array({10, 20})},
           ParseJson(R"([[1, 10, 20, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Two inserts with a NULL array value. Both are successful inserts.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(json_string), String("$[0]"),
                              Value::Null(types::StringArrayType()),
                              String("$[0][1]"), Int64Array({10, 20})},
                             ParseJson(
                                 R"([[null, [10, 20]], [1, 2, 3], true,
                              {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  // 1 insertion
  // Negative indexing is not supported.
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[0][-1]"), Bool(false)},
                                         NullJson(), OUT_OF_RANGE)});
  // Negative indexing is not supported.
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[-1][0]"), Bool(false)},
                                         NullJson(), OUT_OF_RANGE)});
  // Path doesn't exist
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$.b[0]"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't exist
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[2].b[0]"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't point to an array
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[2][0][0]"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't point to an array
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[0][1][2]"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't point to an array
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[2][0]"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't point to an array
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[1][0]"), String("bar")},
                                         ParseJson(json_string))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64(-1)},
           ParseJson(R"([-1, [1, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[1]"), String("bar")},
           ParseJson(R"([[1, 2, 3], "bar", true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0][2]"), Bool(false)},
           ParseJson(R"([[1, 2, false, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult(
                       {ParseJson(json_string), String("$[0][5]"), Bool(false)},
                       ParseJson(R"([[1, 2, 3, null, null, false], true,
                         {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1]"), NullJson()},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1, null, [["foo"]]]}])"))});
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[2].a[1][2]"), Int64(10)},
                                         ParseJson(R"([[1, 2, 3], true,
                         {"a": [1.1, [["foo"], null, 10]]}])"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1][0][0]"), Int64(10)},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1, [[10, "foo"]]]}])"))});

  // Insertion into null
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult(
                       {ParseJson(R"({"a": null, "b": [null]})"),
                        String("$.a[2]"), Int64(10)},
                       ParseJson(R"({"a": [null, null, 10], "b": [null]})"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(R"({"a": null, "b": [null]})"), String("$.a[2]"),
            Int64Array({})},
           ParseJson(R"({"a": [null, null, null], "b": [null]})"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(R"({"a": null, "b": [null]})"),
                              String("$.a[1]"), Int64Array({})},
                             ParseJson(R"({"a": [null, []], "b": [null]})"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back({"json_array_insert",
                   QueryParamsWithResult(
                       {ParseJson(R"({"a": null, "b": [null]})"),
                        String("$.b[2]"), Int64Array({})},
                       ParseJson(R"({"a": null, "b": [null, null, null]})"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(R"({"a": null, "b": [null]})"),
                              String("$.b[2][0]"), Int64(10)},
                             ParseJson(R"({"a": null, "b": [null]})"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(R"({"a": null, "b": [null]})"),
                              String("$.b[0][0]"), Int64(10)},
                             ParseJson(R"({"a": null, "b": [[10]]})"))});

  // Insertion of arrays
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0][1]"), Int64Array({10, 20})},
           ParseJson(
               R"([[1, 10, 20, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0][1]"), Int64Array({10, 20})},
           ParseJson(R"([[1, 10, 20, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0][1]"), Int64Array({10, 20})},
           ParseJson(R"([[1, [10, 20], 2, 3], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1]"),
            values::Array(StringArrayType(), {String("foo"), NullString()})},
           ParseJson(
               R"([[1, 2, 3], true, {"a": [1.1, "foo", null, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1]"),
            values::Array(StringArrayType(), {String("foo"), NullString()})},
           ParseJson(
               R"([[1, 2, 3], true, {"a": [1.1, ["foo", null], [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0][0]"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           ParseJson(R"([[123.1234569817232, 1, 2, 3], true,
                         {"a": [1.1, [["foo"]]]}])"))
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)});
  // Strict number parsing
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0][0]"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           NullJson(), OUT_OF_RANGE)
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)
           .AddRequiredFeature(FEATURE_JSON_STRICT_NUMBER_PARSING)});

  // 2+ Insertions
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64(10), String("$[0]"),
            Bool(true)},
           ParseJson(
               R"([true, 10, [1, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  // After the first insertion, the object moved to $[3], so the second
  // path doesn't exist and is a no-op.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64(10),
            String("$[2].a[0]"), Bool(true)},
           ParseJson(R"([10, [1, 2, 3], true, {"a": [1.1, [["foo"]]]}])"))});
  // The second path points to an array created by the first path.
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[1]"), StringArray({"a", "b"}),
            String("$[1][1]"), Bool(true)},
           ParseJson(R"([[1, 2, 3], ["a", true, "b"], true,
                         {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kInsertEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_insert",
       QueryParamsWithResult({ParseJson(json_string), String("$[2].a[1][0]"),
                              String("a"), String("$[0][4]"), Bool(true),
                              String("$[2].a[1][1][0]"), NullInt64()},
                             ParseJson(R"([[1, 2, 3, null, true], true,
                                     {"a": [1.1, ["a", [null, "foo"]]]}])"))});
  // Max array size
  {
    // Array is already larger than max size. Fails if an element is added.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize);
    tests.push_back({"json_array_insert",
                     QueryParamsWithResult({Json(std::move(input_json)),
                                            String("$.a[1]"), Int64(10)},
                                           NullJson(), OUT_OF_RANGE)});
  }
  {
    // Array is already larger than max size. Fails if an element is added.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize);
    tests.push_back(
        {"json_array_insert",
         QueryParamsWithResult({Json(std::move(input_json)), String("$.a[1]"),
                                Int64Array({10, 20})},
                               NullJson(), OUT_OF_RANGE)});
  }
  {
    // Array is already larger than max size. Ok if no element is added.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize);
    JSONValue expected_result = JSONValue::CopyFrom(input_json.GetConstRef());
    tests.push_back({"json_array_insert",
                     QueryParamsWithResult({Json(std::move(input_json)),
                                            String("$.a[0]"), Int64Array({})},
                                           Json(std::move(expected_result)))});
  }
  {
    // Array is at max size. Adding an element fails.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize - 1);
    tests.push_back({"json_array_insert",
                     QueryParamsWithResult({Json(std::move(input_json)),
                                            String("$.a[10]"), Int64(10)},
                                           NullJson(), OUT_OF_RANGE)});
  }
  {
    // Array is at max size - 1. Adding 2 elements fails.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize - 2);
    tests.push_back(
        {"json_array_insert",
         QueryParamsWithResult({Json(std::move(input_json)), String("$.a[1]"),
                                Int64Array({10, 20})},
                               NullJson(), OUT_OF_RANGE)});
  }
  {
    // Insertion past array size and fills with null. Array is at max size.
    // Succeeds.
    JSONValue expected_result;
    JSONValueRef ref = expected_result.GetRef();
    ref.GetMember("a").GetArrayElement(0).SetInt64(10);
    ref.GetMember("a").GetArrayElement(kJSONMaxArraySize - 1).SetString("foo");

    tests.push_back(
        {"json_array_insert",
         QueryParamsWithResult(
             {ParseJson(R"({"a": [10]})"),
              String(absl::Substitute("$$.a[$0]", kJSONMaxArraySize - 1)),
              String("foo")},
             Json(std::move(expected_result)))});
  }
  {
    // Insertion past array size and fills with null. Array is oversized. Fails.
    tests.push_back({"json_array_insert",
                     QueryParamsWithResult({ParseJson(R"({"a": [10]})"),
                                            String(absl::Substitute(
                                                "$$.a[$0]", kJSONMaxArraySize)),
                                            Int64(10)},
                                           NullJson(), OUT_OF_RANGE)});
  }
  {
    // Insertion past array size and fills with null. Array is at max size.
    // Succeeds.
    JSONValue expected_result;
    JSONValueRef ref = expected_result.GetRef();
    ref.GetMember("a").GetArrayElement(0).SetInt64(10);
    ref.GetMember("a").GetArrayElement(kJSONMaxArraySize - 1);

    tests.push_back(
        {"json_array_insert",
         QueryParamsWithResult(
             {ParseJson(R"({"a": [10]})"),
              String(absl::Substitute("$$.a[$0]", kJSONMaxArraySize - 1)),
              Int64Array({})},
             Json(std::move(expected_result)))});
  }

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsJsonArrayAppend() {
  std::vector<FunctionTestCall> tests;
  constexpr absl::string_view kAppendEachElement = "append_each_element";
  // One argument to JSON_ARRAY_APPEND. Test cases from TO_JSON to make sure
  // JSON_ARRAY_APPEND applies TO_JSON semantics to arguments.
  for (FunctionTestCall& test : GetFunctionTestsToJson()) {
    if (test.params.num_params() == 2) {
      if (test.params.param(1).is_null() || test.params.param(1).bool_value()) {
        // No stringify mode in JSON_ARRAY_APPEND.
        continue;
      }
    }

    auto features_set = test.params.required_features();
    features_set.insert(FEATURE_NAMED_ARGUMENTS);

    absl::string_view json_string = R"({"a": [1, "foo"]})";
    Value input = ParseJson(json_string);
    absl::string_view json_path = "$.a";

    if (test.params.status().ok()) {
      JSONValue json_result = JSONValue::ParseJSONString(json_string).value();
      GOOGLESQL_CHECK_OK(json_result.GetRef().GetMember("a").AppendArrayElement(
          JSONValue::CopyFrom(test.params.result().json_value())));
      Value result = Json(std::move(json_result));
      tests.push_back(
          {"json_array_append",
           QueryParamsWithResult({std::move(input), String(json_path),
                                  std::move(test.params.param(0))},
                                 result)
               .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
               .AddRequiredFeatures(features_set)});
    } else {
      tests.push_back(
          {"json_array_append",
           QueryParamsWithResult({std::move(input), String(json_path),
                                  std::move(test.params.param(0))},
                                 NullJson(), test.params.status())
               .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
               .AddRequiredFeatures(features_set)});
    }
  }

  absl::string_view json_string =
      R"([[1, 2, 3], true, {"a": [1.1,[["foo"]]]}])";

  // NULL input JSON.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({NullJson(), String("$"), Int64(1)}, NullJson())});
  // NULL JSONPath.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(json_string), NullString(), Int64(1)},
                             ParseJson(json_string))});
  // Mix of NULL and Non-NULL JSONPath.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$"), Int64(1), NullString(),
            Int64(2)},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1,[["foo"]]]}, 1])"))});
  // Mix of NULL and Non-NULL JSONPath.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), NullString(), Int64(1), String("$"),
            Int64(2)},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1,[["foo"]]]}, 2])"))});
  // NULL append_each_element.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(json_string), String("$"), Int64(1),
                              String("$"), Int64(2)},
                             ParseJson(json_string))
           .SetNamedValueArguments({{kAppendEachElement, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL input json and NULL append_each_element.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {NullJson(), String("$"), Int64(1), String("$"), Int64(2)},
           NullJson())
           .SetNamedValueArguments({{kAppendEachElement, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  // Invalid JSONPath.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(json_string), String("$$"), Int64(1)},
                             NullJson(), OUT_OF_RANGE)});
  // Invalid JSONPath with NULL input JSON.
  tests.push_back({"json_array_append",
                   QueryParamsWithResult({NullJson(), String("$$"), Int64(1)},
                                         NullJson(), OUT_OF_RANGE)});
  // Invalid second JSONPath with NULL input JSON.
  tests.push_back({"json_array_append",
                   QueryParamsWithResult({NullJson(), String("$.a"), Int64(2),
                                          String("$$"), Int64(1)},
                                         NullJson(), OUT_OF_RANGE)});
  // Invalid JSONPath with NULL append_each_element.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {NullJson(), String("$"), Int64(1), String("$$"), Int64(2)},
           NullJson(), OUT_OF_RANGE)
           .SetNamedValueArguments({{kAppendEachElement, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // 1 append
  // Negative indexing is not supported.
  tests.push_back({"json_array_append",
                   QueryParamsWithResult(
                       {ParseJson(json_string), String("$[-1]"), Bool(false)},
                       NullJson(), OUT_OF_RANGE)});
  // Path doesn't exist
  tests.push_back({"json_array_append",
                   QueryParamsWithResult(
                       {ParseJson(json_string), String("$.b"), String("bar")},
                       ParseJson(json_string))});
  // Path doesn't exist
  tests.push_back({"json_array_append",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[2].b"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't exist
  tests.push_back({"json_array_append",
                   QueryParamsWithResult({ParseJson(json_string),
                                          String("$[0][1][2]"), String("bar")},
                                         ParseJson(json_string))});
  // Path doesn't point to an array
  tests.push_back({"json_array_append",
                   QueryParamsWithResult(
                       {ParseJson(json_string), String("$[2]"), String("bar")},
                       ParseJson(json_string))});
  // Path doesn't point to an array
  tests.push_back({"json_array_append",
                   QueryParamsWithResult(
                       {ParseJson(json_string), String("$[1]"), String("bar")},
                       ParseJson(json_string))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$"), Int64(-1)},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1, [["foo"]]]}, -1])"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Bool(false)},
           ParseJson(R"([[1, 2, 3, false], true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a"), NullJson()},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1, [["foo"]], null]}])"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1]"), Int64(10)},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1, [["foo"], 10]]}])"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1][0]"), Int64(10)},
           ParseJson(R"([[1, 2, 3], true, {"a": [1.1, [["foo", 10]]]}])"))});

  // Insertion into null
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(R"({"a": null, "b": [null]})"), String("$.a"), Int64(10)},
           ParseJson(R"({"a": [10], "b": [null]})"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(R"({"a": null, "b": [null]})"),
                              String("$.a"), Int64Array({})},
                             ParseJson(R"({"a": [], "b": [null]})"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(R"({"a": null, "b": [null]})"),
                              String("$.a"), Int64Array({})},
                             ParseJson(R"({"a": [[]], "b": [null]})"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(R"({"a": null, "b": [null]})"),
                              String("$.b[0]"), Int64(10)},
                             ParseJson(R"({"a": null, "b": [[10]]})"))});

  // Insertion of arrays
  //
  // NULL array value for appending. The operation is ignored because
  // `append_each_element` = true.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult({ParseJson(json_string), String("$[0]"),
                              Value::Null(types::DoubleArrayType())},
                             ParseJson(json_string))
           .SetNamedValueArguments({{kAppendEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Successful NULL array value append operation.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            Value::Null(types::StringArrayType())},
           ParseJson(R"([[1, 2, 3, null], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Two appends. The first append with a NULL array value is ignored because
  // `append_each_element` = true, and the second operation is successful.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            Value::Null(types::StringArrayType()), String("$[0]"),
            Int64Array({10, 20})},
           ParseJson(R"([[1, 2, 3, 10, 20], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Two appends with a NULL array value. Both are successful operations.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            Value::Null(types::BoolArrayType()), String("$[0]"),
            Int64Array({10, 20})},
           ParseJson(
               R"([[1, 2, 3, null, [10, 20]], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64Array({10, 20})},
           ParseJson(
               R"([[1, 2, 3, 10, 20], true, {"a": [1.1, [["foo"]]]}])"))});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64Array({10, 20})},
           ParseJson(R"([[1, 2, 3, 10, 20], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"), Int64Array({10, 20})},
           ParseJson(R"([[1, 2, 3, [10, 20]], true, {"a": [1.1, [["foo"]]]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a"),
            values::Array(StringArrayType(), {String("foo"), NullString()})},
           ParseJson(
               R"([[1, 2, 3], true, {"a": [1.1, [["foo"]], "foo", null]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a"),
            values::Array(StringArrayType(), {String("foo"), NullString()})},
           ParseJson(
               R"([[1, 2, 3], true, {"a": [1.1, [["foo"]], ["foo", null]]}])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           ParseJson(R"([[1, 2, 3, 123.1234569817232], true,
                         {"a": [1.1, [["foo"]]]}])"))
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)});
  // Strict number parsing
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[0]"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           NullJson(), OUT_OF_RANGE)
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)
           .AddRequiredFeature(FEATURE_JSON_STRICT_NUMBER_PARSING)});

  // 2+ Insertions
  tests.push_back({"json_array_append",
                   QueryParamsWithResult({ParseJson(json_string), String("$"),
                                          Int64(10), String("$"), Bool(true)},
                                         ParseJson(R"([[1, 2, 3], true,
                         {"a": [1.1, [["foo"]]]}, 10, true])"))});
  // The second path points to an array created by the first path.
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$"), StringArray({"a", "b"}),
            String("$[3]"), Bool(true)},
           ParseJson(R"([[1, 2, 3], true,
                         {"a": [1.1, [["foo"]]]}, ["a", "b", true]])"))
           .SetNamedValueArguments({{kAppendEachElement, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_array_append",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[2].a[1]"), String("a"),
            String("$[0]"), Bool(true), String("$[2].a[1][0]"), NullInt64()},
           ParseJson(R"([[1, 2, 3, true], true,
                                     {"a": [1.1, [["foo", null], "a"]]}])"))});

  // Max array size
  {
    // Array is already larger than max size. Fails if an element is added.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize);
    tests.push_back({"json_array_append",
                     QueryParamsWithResult({Json(std::move(input_json)),
                                            String("$.a"), Int64(10)},
                                           NullJson(), OUT_OF_RANGE)});
  }
  {
    // Array is already larger than max size. Fails if an element is added.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize);
    tests.push_back(
        {"json_array_append",
         QueryParamsWithResult(
             {Json(std::move(input_json)), String("$.a"), Int64Array({10, 20})},
             NullJson(), OUT_OF_RANGE)});
  }
  {
    // Array is already larger than max size. Ok if no element is added.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize);
    JSONValue expected_result = JSONValue::CopyFrom(input_json.GetConstRef());
    tests.push_back({"json_array_append",
                     QueryParamsWithResult({Json(std::move(input_json)),
                                            String("$.a"), Int64Array({})},
                                           Json(std::move(expected_result)))});
  }
  {
    // Array is at max size. Adding an element fails.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize - 1);
    tests.push_back({"json_array_append",
                     QueryParamsWithResult({Json(std::move(input_json)),
                                            String("$.a"), Int64(10)},
                                           NullJson(), OUT_OF_RANGE)});
  }
  {
    // Array is at max size - 2. Adding 2 elements succeeds.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize - 3);
    JSONValue expected_result = JSONValue::CopyFrom(input_json.GetConstRef());
    expected_result.GetRef()
        .GetMember("a")
        .GetArrayElement(kJSONMaxArraySize - 2)
        .SetInt64(10);
    expected_result.GetRef()
        .GetMember("a")
        .GetArrayElement(kJSONMaxArraySize - 1)
        .SetInt64(20);
    tests.push_back(
        {"json_array_append",
         QueryParamsWithResult(
             {Json(std::move(input_json)), String("$.a"), Int64Array({10, 20})},
             Json(std::move(expected_result)))});
  }
  {
    // Array is at max size - 1. Adding 2 elements fails.
    JSONValue input_json = JSONValue::ParseJSONString(R"({"a": [10]})").value();
    input_json.GetRef().GetMember("a").GetArrayElement(kJSONMaxArraySize - 2);
    tests.push_back(
        {"json_array_append",
         QueryParamsWithResult(
             {Json(std::move(input_json)), String("$.a"), Int64Array({10, 20})},
             NullJson(), OUT_OF_RANGE)});
  }

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsJsonSet() {
  std::vector<FunctionTestCall> tests;
  // Test cases from TO_JSON to make sure JSON_SET applies TO_JSON semantics
  // to arguments.
  for (FunctionTestCall& test : GetFunctionTestsToJson()) {
    if (test.params.num_params() == 2) {
      if (test.params.param(1).is_null() || test.params.param(1).bool_value()) {
        // No stringify mode in JSON_OBJECT.
        continue;
      }
    }
    auto features_set = test.params.required_features();
    features_set.erase(FEATURE_NAMED_ARGUMENTS);

    absl::string_view json_string = R"({"a": 10})";
    if (test.params.status().ok()) {
      auto json_result = JSONValue::ParseJSONString(json_string).value();
      json_result.GetRef().GetMember("a").Set(
          JSONValue::CopyFrom(test.params.result().json_value()));
      Value result = Json(std::move(json_result));
      tests.push_back({"json_set", QueryParamsWithResult(
                                       {ParseJson(json_string), String("$.a"),
                                        test.params.param(0)},
                                       result)
                                       .AddRequiredFeatures(features_set)});
    } else {
      tests.push_back({"json_set", QueryParamsWithResult(
                                       {ParseJson(json_string), String("$.a"),
                                        test.params.param(0)},
                                       NullJson(), test.params.status())
                                       .AddRequiredFeatures(features_set)});
    }
  }

  constexpr absl::string_view kCreateIfMissingName = "create_if_missing";
  absl::string_view json_string =
      R"({"a":null, "b":{}, "c":[], "d":{"e": 1}, "f":["foo", [], {}, [3,4]]})";

  // NULL input and NULL JSONPath.
  tests.push_back(
      {"json_set", QueryParamsWithResult({NullJson(), NullString(), Bool(true)},
                                         NullJson())});
  // NULL input with non-null JSONPath.
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {NullJson(), String("$.a"), Bool(true)}, NullJson())});
  // NULL create_if_missing.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.a"), Bool(true)},
           ParseJson(json_string))
           .SetNamedValueArguments({{kCreateIfMissingName, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Non-null input and single NULL JSONPath.
  tests.push_back({"json_set", QueryParamsWithResult({ParseJson(json_string),
                                                      NullString(), Bool(true)},
                                                     ParseJson(json_string))});
  // Ignore first NULL path operation but continue with other SET operation
  // pair.
  tests.push_back({"json_set", QueryParamsWithResult(
                                   {ParseJson(json_string), NullString(),
                                    Bool(true), String("$.a"), Int64(10)},
                                   ParseJson(
                                       R"({"a":10, "b":{}, "c":[], "d":{"e": 1},
                                          "f":["foo", [], {}, [3,4]]})"))});
  // Ignore middle NULL path operation but continue with other SET operation
  // pairs.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult({ParseJson(json_string), String("$.a"), Bool(true),
                              NullString(), Int64(10), String("$.b"), Int64(5)},
                             ParseJson(
                                 R"({"a":true, "b":5, "c":[], "d":{"e": 1},
                                    "f":["foo", [], {}, [3,4]]})"))});

  // Invalid JSONPath
  tests.push_back(
      {"json_set",
       QueryParamsWithResult({ParseJson(json_string), String("$a"), Int64(10)},
                             NullJson(), OUT_OF_RANGE)});

  tests.push_back(
      {"json_set", QueryParamsWithResult({ParseJson(json_string), String("$.a"),
                                          Int64(10), String("a.b"), Int64(20)},
                                         NullJson(), OUT_OF_RANGE)});

  // Invalid and NULL JSONPaths. An error takes precedence over NULLs.
  tests.push_back(
      {"json_set", QueryParamsWithResult({ParseJson(json_string), NullString(),
                                          Int64(10), String("a.b"), Int64(20)},
                                         NullJson(), OUT_OF_RANGE)});

  tests.push_back(
      {"json_set", QueryParamsWithResult({ParseJson(json_string), String("$a"),
                                          Int64(10), NullString(), Int64(20)},
                                         NullJson(), OUT_OF_RANGE)});

  // Type mismatch
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$[1]"), Bool(true)},
                       ParseJson(json_string))});

  // Type mismatch with create_if_missing = false.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[1]"), Bool(true)},
           ParseJson(json_string))
           .SetNamedValueArguments({{kCreateIfMissingName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.f.a"), Bool(true)},
                       ParseJson(json_string))});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.d.e[0]"), Bool(true)},
                       ParseJson(json_string))});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.d.e.a"), Bool(true)},
                       ParseJson(json_string))});
  tests.push_back(
      {"json_set", QueryParamsWithResult({ParseJson(json_string),
                                          String("$.d.e.a.b"), Bool(true)},
                                         ParseJson(json_string))});
  tests.push_back(
      {"json_set", QueryParamsWithResult({ParseJson(json_string),
                                          String("$.d.e.a[1]"), Bool(true)},
                                         ParseJson(json_string))});

  // Entire JSON
  tests.push_back({"json_set", QueryParamsWithResult({ParseJson(json_string),
                                                      String("$"), Bool(true)},
                                                     ParseJson("true"))});

  // Replace entire JSON with create_if_missing = false.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult({ParseJson(json_string), String("$"), Bool(true)},
                             ParseJson("true"))
           .SetNamedValueArguments({{kCreateIfMissingName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_set", QueryParamsWithResult({ParseJson(json_string), String("$"),
                                          Bool(true), String("$"), Int64(10)},
                                         ParseJson("10"))});
  // Replace scalar values
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.a"), Int64(-10)},
                       ParseJson(R"({"a":-10, "b":{}, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.d.e"), Int64(-10)},
                       ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":-10},
                                     "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.f[0]"), Int64(-10)},
                       ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1},
                                     "f":[-10, [], {}, [3, 4]]})"))});
  // Replace object
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.b"), NullInt64()},
                       ParseJson(R"({"a":null, "b":null, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult({ParseJson(json_string), String("$.d"), Int64(5)},
                             ParseJson(R"({"a":null, "b":{}, "c":[], "d":5,
                                     "f":["foo", [], {}, [3, 4]]})"))});

  // Multiple sets with create_if_missing = false. Second set operation ignored.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult({ParseJson(json_string), String("$.a"), Int64(777),
                              String("$.b.c"), Int64(888)},
                             ParseJson(R"({"a":777, "b":{}, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3, 4]]})"))
           .SetNamedValueArguments({{kCreateIfMissingName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  // Multiple sets with create_if_missing = false. First set operation ignored.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.a.b"), Int64(777), String("$.b"),
            Int64(888)},
           ParseJson(R"({"a":null, "b":888, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3, 4]]})"))
           .SetNamedValueArguments({{kCreateIfMissingName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  // Insert into NULL with create_if_missing false. Set operation ignored.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.a.b"), Bool(true)},
           ParseJson(json_string))
           .SetNamedValueArguments({{kCreateIfMissingName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.f[3]"), Int64(5)},
                       ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, 5]})"))});

  // Recursive creation starting from object
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.e"), Int64(5)},
                       ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1},
                                     "e": 5, "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.e[0]"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "e":[5],
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.e.a"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1}, "e":{"a":5},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.b.e"), Int64(5)},
                       ParseJson(R"({"a":null, "b":{"e":5}, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.b.a[1]"), Int64(5)},
           ParseJson(R"({"a":null, "b":{"a":[null,5]}, "c":[], "d":{"e":1},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.d.a"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"a":5, "e":1},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.d.a[1].e"), Int64(5)},
           ParseJson(
               R"({"a":null, "b":{}, "c":[], "d":{"a":[null, {"e":5}] ,"e":1},
                   "f":["foo", [], {}, [3, 4]]})"))});

  // Recursive creation starting from array
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.c[2]"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[null, null, 5], "d":{"e":1},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.c[1].b"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[null, {"b":5}], "d":{"e":1},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.c[1][1]"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[null, [null, 5]], "d":{"e":1},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.f[1][0].a"), Int64(5)},
           ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1},
                         "f":["foo", [{"a":5}], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.f[5]"), Int64(5)},
                       ParseJson(R"({"a":null, "b":{}, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3,4], null, 5]})"))});

  // Recursive creation starting from null
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string), String("$.a[0]"), Bool(true)},
                       ParseJson(R"({"a":[true], "b":{}, "c":[], "d":{"e":1},
                                     "f":["foo", [], {}, [3,4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.a.b"), Bool(true)},
           ParseJson(R"({"a":{"b":true}, "b":{}, "c":[], "d":{"e":1},
                         "f":["foo", [], {}, [3,4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.a[1].b"), Int64(10)},
           ParseJson(R"({"a":[null, {"b": 10}], "b":{}, "c":[], "d":{"e":1},
                         "f":["foo", [], {}, [3,4]]})"))});

  // Multiple updates
  tests.push_back(
      {"json_set",
       QueryParamsWithResult({ParseJson(json_string), String("$.a"), Int64(5),
                              String("$.c.a"), String("foo")},
                             ParseJson(R"({"a":5, "b":{}, "c":[], "d":{"e":1},
                                           "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.b.a"), Int64(5), String("$.b.a"),
            Bool(true)},
           ParseJson(R"({"a":null, "b":{"a":true}, "c":[], "d":{"e":1},
                         "f":["foo", [], {}, [3, 4]]})"))});
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$.c[2].a"),
            StringArray({"foo", "bar"}), String("$.c[2].a[0]"), Bool(false),
            String("$.c[0]"), Int64(5), String("$.c[1].d"), Int64(-1)},
           ParseJson(
               R"({"a":null, "b":{}, "c":[5, {"d":-1}, {"a": [false, "bar"]}],
                   "d":{"e":1}, "f":["foo", [], {}, [3, 4]]})"))});

  // Strict number parsing disabled
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson("[10, null]"), String("$[1]"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           Json(JSONValue::ParseJSONString(R"([10, 123.1234569817232])")
                    .value()))
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)});

  // Strict number parsing enabled
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson("[10, null]"), String("$[1]"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           NullJson(), OUT_OF_RANGE)
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)
           .AddRequiredFeature(FEATURE_JSON_STRICT_NUMBER_PARSING)});

  // Failing conversion is prioritized over path mismatch.
  tests.push_back(
      {"json_set",
       QueryParamsWithResult(
           {ParseJson(json_string), String("$[1].a"),
            BigNumericValue::FromStringStrict("123.123456981723189237198273")
                .value()},
           NullJson(), OUT_OF_RANGE)
           .AddRequiredFeature(FEATURE_BIGNUMERIC_TYPE)
           .AddRequiredFeature(FEATURE_JSON_STRICT_NUMBER_PARSING)});

  // Exceeding max array size.
  tests.push_back(
      {"json_set", QueryParamsWithResult(
                       {ParseJson(json_string),
                        String(absl::Substitute("$$.a[$0]", kJSONMaxArraySize)),
                        String("foo")},
                       NullJson(), OUT_OF_RANGE)});

  return tests;
}

std::vector<FunctionTestCall> GetFunctionTestsJsonStripNulls() {
  std::vector<FunctionTestCall> tests;
  constexpr absl::string_view kInitialSimpleObjectValue =
      R"({"a":null, "b":1, "c":[null, true], "d":{}, "e":[null], "f":[]})";
  constexpr absl::string_view kIncludeArraysName = "include_arrays";
  constexpr absl::string_view kRemoveEmptyName = "remove_empty";
  // NULL input JSON.
  tests.push_back(
      {"json_strip_nulls", QueryParamsWithResult({NullJson()}, NullJson())});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult({NullJson(), String("$.a")}, NullJson())
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Invalid JSONPath with NULL input JSON.
  tests.push_back(
      {"json_strip_nulls", QueryParamsWithResult({NullJson(), String("$a")},
                                                 NullJson(), OUT_OF_RANGE)});

  // Non-null input with NULL path.
  tests.push_back({"json_strip_nulls",
                   QueryParamsWithResult(
                       {ParseJson(kInitialSimpleObjectValue), NullString()},
                       ParseJson(kInitialSimpleObjectValue))});
  // Non-null input with NULL include_arrays.
  tests.push_back({"json_strip_nulls",
                   QueryParamsWithResult(
                       {ParseJson(kInitialSimpleObjectValue), String("$.a")},
                       ParseJson(kInitialSimpleObjectValue))
                       .SetNamedValueArguments({
                           {kIncludeArraysName, NullBool()},
                       })
                       .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Non-null input with NULL remove_empty.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$.a")},
           ParseJson(kInitialSimpleObjectValue))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Invalid JSONPath.
  tests.push_back({"json_strip_nulls",
                   QueryParamsWithResult(
                       {ParseJson(kInitialSimpleObjectValue), String("$a")},
                       NullJson(), OUT_OF_RANGE)});
  // Invalid JSONPath with NULL input JSON.
  tests.push_back(
      {"json_strip_nulls", QueryParamsWithResult({NullJson(), String("$a")},
                                                 NullJson(), OUT_OF_RANGE)});
  // Invalid JSONPath with NULL include_arrays.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$a")}, NullJson(),
           OUT_OF_RANGE)
           .SetNamedValueArguments({{kIncludeArraysName, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Invalid JSONPath with NULL remove_empty.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$a")}, NullJson(),
           OUT_OF_RANGE)
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, NullBool()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Valid cases.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$")},
           ParseJson(
               R"({"b":1, "c":[null, true], "d":{}, "e":[null], "f":[]})"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$")},
           ParseJson(R"({"b":1, "c":[true], "d":{}, "e":[], "f":[]})"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$")},
           ParseJson(R"({"b":1, "c":[null, true], "e":[null], "f":[]})"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleObjectValue), String("$")},
           ParseJson(R"({"b":1, "c":[true]})"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  constexpr absl::string_view kInitialComplexObjectValue =
      R"({"a": {"b":null, "c":null, "d":[[null], null]}, "e":null})";
  // No change. Type mismatch.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$.a[0]")},
           ParseJson(kInitialComplexObjectValue))});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$.a.d.e")},
           ParseJson(kInitialComplexObjectValue))});
  // Path suffix "[2]" is larger than existing array.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$.a.d[2]")},
           ParseJson(kInitialComplexObjectValue))});
  // Removes all JSON 'null'.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$")},
           ParseJson("null"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // No change. Subpath points to a nested ARRAY.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$.a.d")},
           ParseJson(kInitialComplexObjectValue))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Subpath is nested ARRAY and removes JSON 'null's.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$.a.d")},
           ParseJson(R"({"a": {"b":null, "c":null, "d":[[]]}, "e":null})"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Subpath is nested ARRAY replaced by JSON 'null'.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexObjectValue), String("$.a")},
           ParseJson(R"({"a":null, "e":null})"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  // Valid Cases.
  constexpr absl::string_view kInitialSimpleArrayValue =
      R"(["a", null, 1.1, [], [null], [1, null], {}, {"a":null},
         {"b":1, "c":null}])";
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleArrayValue), String("$")},
           ParseJson(R"(["a", null, 1.1, [], [null], [1, null], {}, {},
                      {"b":1}])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleArrayValue), String("$")},
           ParseJson(R"(["a", 1.1, [], [], [1], {}, {}, {"b":1}])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(false)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Because parent of empty OBJECTs is an ARRAY, empty OBJECTs are not
  // removed.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleArrayValue), String("$")},
           ParseJson(
               R"(["a", null, 1.1, [], [null], [1, null], {}, {}, {"b":1}])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult({ParseJson(kInitialSimpleArrayValue), String("$")},
                             ParseJson(R"(["a", 1.1, [1], {"b":1}])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Subpath points to an array that is replaced with JSON 'null'.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleArrayValue), String("$[4]")},
           ParseJson(R"(["a", null, 1.1, [], null, [1, null], {}, {"a":null},
              {"b":1, "c":null}])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Subpath points to an OBJECT that is replaced with JSON 'null'.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialSimpleArrayValue), String("$[7]")},
           ParseJson(R"(["a", null, 1.1, [], [null], [1, null], {}, null,
              {"b":1, "c":null}])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  constexpr absl::string_view kInitialComplexArrayValue =
      R"([null, {"b":null, "c":null, "d":[[null], null]}, [null, null],
      []])";
  // Removes all JSON 'null'.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexArrayValue), String("$")},
           ParseJson("null"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Cleanup nested arrays to JSON 'null'.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexArrayValue), String("$[1]")},
           ParseJson("[null, null, [null, null],[]]"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(true)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Cleanup nested arrays to JSON 'null' but no array cleanup.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexArrayValue), String("$[1]")},
           ParseJson(R"([null, {"d":[[null], null]}, [null, null], []])"))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // No change. Subpath points to a nested ARRAY.
  tests.push_back(
      {"json_strip_nulls",
       QueryParamsWithResult(
           {ParseJson(kInitialComplexArrayValue), String("$[1].d")},
           ParseJson(kInitialComplexArrayValue))
           .SetNamedValueArguments({{kIncludeArraysName, Bool(false)},
                                    {kRemoveEmptyName, Bool(true)}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});

  return tests;
}

void JsonKeysSameResultForModes(absl::string_view json_input,
                                std::optional<int64_t> max_depth,
                                std::vector<std::string> results,
                                absl::Span<const absl::string_view> modes,
                                std::vector<FunctionTestCall>& tests) {
  constexpr absl::string_view kModeArgumentName = "mode";
  for (absl::string_view mode : modes) {
    if (max_depth.has_value()) {
      tests.push_back(
          {"json_keys",
           QueryParamsWithResult({ParseJson(json_input), Int64(*max_depth)},
                                 values::StringArray(results))
               .SetNamedValueArguments({{kModeArgumentName, String(mode)}})
               .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
    } else {
      tests.push_back(
          {"json_keys",
           QueryParamsWithResult({ParseJson(json_input)},
                                 values::StringArray(results))
               .SetNamedValueArguments({{kModeArgumentName, String(mode)}})
               .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
    }
  }
}

std::vector<FunctionTestCall> GetFunctionTestsJsonKeys() {
  // Function signature:
  // JSON_KEYS(JSON json_doc[, INT64 max_depth][,
  // STRING mode=>{"strict", "lax", "lax recursive"}]) -> Array<STRING>
  constexpr absl::string_view kModeArgumentName = "mode";
  constexpr absl::string_view kStrictMode = "strict";
  constexpr absl::string_view kLaxMode = "lax";
  constexpr absl::string_view kLaxRecursiveMode = "lax recursive";

  std::vector<FunctionTestCall> tests;
  // NULL `json_doc`.
  tests.push_back(
      {"json_keys", QueryParamsWithResult(
                        {NullJson()}, Value::Null(types::StringArrayType()))});
  // NULL `json_doc` and `max_depth`.
  tests.push_back({"json_keys", QueryParamsWithResult(
                                    {NullJson(), NullInt64()},
                                    Value::Null(types::StringArrayType()))});
  // NULL `json_doc`, `max_depth`, and `mode`.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({NullJson(), NullInt64()},
                             Value::Null(types::StringArrayType()))
           .SetNamedValueArguments({{kModeArgumentName, NullString()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL `max_depth` and non-null `json_doc`.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"({"a":1})"), NullInt64()},
                             values::Array(StringArrayType(), {String("a")}))});
  // NULL `mode` and non-null `json_doc`.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"({"a":1})")},
                             Value::Null(types::StringArrayType()))
           .SetNamedValueArguments({{kModeArgumentName, NullString()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL `json_doc` and non-null `max_depth` and `mode`.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({NullJson(), Int64(1)},
                             Value::Null(types::StringArrayType()))
           .SetNamedValueArguments({{kModeArgumentName, String("lax")}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // NULL `json_doc` with invalid `mode` throws error.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({NullJson(), Int64(1)},
                             Value::Null(types::StringArrayType()),
                             OUT_OF_RANGE)
           .SetNamedValueArguments({{kModeArgumentName, String("invalid")}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Non-null `json_doc` with invalid `mode` throws error.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"({"a":1})"), Int64(1)},
                             Value::Null(types::StringArrayType()),
                             OUT_OF_RANGE)
           .SetNamedValueArguments({{kModeArgumentName, String("invalid")}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Non-null `json_doc`, NULL `mode`, invalid `max_depth` throws error.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"({"a":1})"), Int64(0)},
                             Value::Null(types::StringArrayType()),
                             OUT_OF_RANGE)
           .SetNamedValueArguments({{kModeArgumentName, NullString()}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Non-null `json_doc`, NULL `max_depth`, invalid `mode` throws error.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"({"a":1})"), NullInt64()},
                             Value::Null(types::StringArrayType()),
                             OUT_OF_RANGE)
           .SetNamedValueArguments({{kModeArgumentName, String("invalid")}})
           .AddRequiredFeature(FEATURE_NAMED_ARGUMENTS)});
  // Non-positive `max_depth` throws an error.
  tests.push_back(
      {"json_keys", QueryParamsWithResult({NullJson(), Int64(-1)},
                                          Value::Null(types::StringArrayType()),
                                          OUT_OF_RANGE)});
  tests.push_back(
      {"json_keys", QueryParamsWithResult({NullJson(), Int64(0)},
                                          Value::Null(types::StringArrayType()),
                                          OUT_OF_RANGE)});

  // Valid cases
  //
  // Mode "strict", "lax", and "lax recursive" same result.
  tests.push_back({"json_keys", QueryParamsWithResult(
                                    {ParseJson("null")},
                                    values::Array(StringArrayType(), {}))});
  JsonKeysSameResultForModes(R"({"a":1})", std::nullopt, {"a"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  // Special characters for JSON keys.
  JsonKeysSameResultForModes(R"({"a.b":1})", std::nullopt, {R"("a.b")"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"({"a.b":{"c":1}})", std::nullopt,
                             {R"("a.b")", R"("a.b".c)"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"({"a.b": {"c\"":1}})", std::nullopt,
                             {R"("a.b")", R"("a.b"."c\"")"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"({"a.b": {"c\"":1, "d.e":2}})", std::nullopt,
                             {R"("a.b")", R"("a.b"."c\"")", R"("a.b"."d.e")"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  // Testing `max_depth`.
  JsonKeysSameResultForModes(R"({"a":1})", 1, {"a"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"({"a": {"b":1}})", INT64_MAX, {"a", "a.b"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"({"a": {"b":1}})", 1, {"a"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"({"a": {"c":1, "b":1}})", 2, {"a", "a.b", "a.c"},
                             {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  // Keys should be returned in alphabetically sorted order.
  JsonKeysSameResultForModes(
      R"({"a": {"c":{"d":1, "e":1}, "b":1, "a":{"a":1}}})", std::nullopt,
      {"a", "a.a", "a.a.a", "a.b", "a.c", "a.c.d", "a.c.e"},
      {kStrictMode, kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(
      R"({"a": {"c":{"d":1, "e":1}, "b":1, "a":{"a":1}}})", 2,
      {"a", "a.a", "a.b", "a.c"}, {kStrictMode, kLaxMode, kLaxRecursiveMode},
      tests);

  // Tests arrays are correctly unwrapped when "lax" is enabled.
  // Mode "lax", and "lax recursive" same result.
  JsonKeysSameResultForModes(R"({"a": [{"b":1}, {"c":2}]})", std::nullopt,
                             {"a", "a.b", "a.c"}, {kLaxMode, kLaxRecursiveMode},
                             tests);
  JsonKeysSameResultForModes(R"({"a": [{"b":1}, {"c":2}]})", 1, {"a"},
                             {kLaxMode, kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(R"([{"a":1}, {"b":1}, {"c":2}, "d"])", 1,
                             {"a", "b", "c"}, {kLaxMode, kLaxRecursiveMode},
                             tests);

  // Tests nested array are correctly unwrapped when "lax" and "recursive"
  // behavior is enabled.
  JsonKeysSameResultForModes(R"([1, {"a": 1}, [{"b": 2}]])", 3, {"a", "b"},
                             {kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(
      R"({"a": [[{"b":1}, {"b":2}], {"c":2}, {"b":{"c":3}}]})", 3,
      {"a", "a.b", "a.b.c", "a.c"}, {kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(
      R"({"a": [[{"b":1}, {"b":2}], [[[{"c":2}]]], {"b":{"c":3}}]})", 2,
      {"a", "a.b", "a.c"}, {kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(
      R"({"a": [[{"b":1}, {"b":2}], [[[{"c":2}]]], {"b":{"c":3}}]})", 1, {"a"},
      {kLaxRecursiveMode}, tests);
  JsonKeysSameResultForModes(
      R"({"b":2, "a": [[{"b":1}, 2, "value"], {"c":2}]})", 2,
      {"a", "a.b", "a.c", "b"}, {kLaxRecursiveMode}, tests);

  // Tests when "lax" is enabled but "recursive" is not, doesn't unwrap nested
  // arrays.
  JsonKeysSameResultForModes(R"([[{"a":1}]])", std::nullopt, {}, {kLaxMode},
                             tests);
  JsonKeysSameResultForModes(R"([1, {"a": 1}, [{"b": 2}]])", std::nullopt,
                             {"a"}, {kLaxMode}, tests);
  JsonKeysSameResultForModes(
      R"([1, {"a": 1}, {"c": [{"d":2}, [{"e":3}]]}, [{"b": 2}]])", std::nullopt,
      {"a", "c", "c.d"}, {kLaxMode}, tests);

  // Tests that when lax behavior isn't enabled doesn't unwrap arrays.
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"([{"a":1}])")},
                             values::StringArray(std::vector<std::string>()))});
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult({ParseJson(R"([[{"a":1}]])")},
                             values::StringArray(std::vector<std::string>()))});
  tests.push_back(
      {"json_keys",
       QueryParamsWithResult(
           {ParseJson(R"([1, [{"a": 1}], [{"c": 1}], [[{"b": 2}]]])")},
           values::StringArray(std::vector<std::string>()))});

  for (auto& test : tests) {
    test.params.AddRequiredFeature(FEATURE_JSON_TYPE);
    test.params.AddRequiredFeature(FEATURE_JSON_KEYS_FUNCTION);
  }
  return tests;
}

void AddJsonFlattenTest(absl::string_view input,
                        absl::Span<const std::string> output,
                        std::vector<FunctionTestCall>& tests) {
  tests.push_back({"json_flatten",
                   QueryParamsWithResult({ParseJson(input)},
                                         UnvalidatedJsonStringArray(output))});
}

std::vector<FunctionTestCall> GetFunctionTestsJsonFlatten() {
  std::vector<FunctionTestCall> tests;
  AddJsonFlattenTest(R"([1, 2, 3])", {"1", "2", "3"}, tests);
  AddJsonFlattenTest(R"([[[1, 2], 3], 4])", {"1", "2", "3", "4"}, tests);
  AddJsonFlattenTest(R"([[[[1, 2], 3], 4], {"a":10}, ["foo", true]])",
                     {"1", "2", "3", "4", R"({"a":10})", R"("foo")", "true"},
                     tests);
  AddJsonFlattenTest(R"([])", {}, tests);
  AddJsonFlattenTest(R"([[[], []]])", {}, tests);
  tests.push_back(
      {"json_flatten", QueryParamsWithResult(
                           {NullJson()}, Value::Null(types::JsonArrayType()))});
  for (FunctionTestCall& test : tests) {
    test.params.AddRequiredFeature(FEATURE_JSON_TYPE);
    test.params.AddRequiredFeature(FEATURE_JSON_FLATTEN_FUNCTION);
  }
  return tests;
}
}  // namespace googlesql
