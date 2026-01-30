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

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/strings/substitute.h"
#include "re2/re2.h"

namespace googlesql {

std::vector<FunctionTestCall> GetFunctionTestsFormatNulls() {
  const Value enum_value = Value::Enum(TestEnumType(), 1);
  const Value enum_null = Value::Null(TestEnumType());
  const Value enum_array_value = Value::Array(
      MakeArrayType(TestEnumType()), {enum_value, enum_null, enum_value});

  const std::string kProtoValueString =
      R"(
        int64_key_1: 1
        int64_key_2: 2
        string_val: "foo"
        repeated_int32_val: 3
        repeated_string_val: "hello"
        repeated_string_val: "world"
        repeated_bool_val: true
        repeated_bool_val: true
        nested_value {
          nested_int64: 5
        }
        nested_repeated_value {
        }
        nested_repeated_value {
          nested_int64: 6
        }
        nested_repeated_value {
          nested_repeated_int64: 7
          nested_repeated_int64: 8
        }
        test_enum: TESTENUM2147483647
        repeated_test_enum: TESTENUM1
        repeated_test_enum: TESTENUM2
        date: 9
        timestamp_seconds: 10
        timestamp_seconds_format: 11)";
  // Clean up the string to make it the same as FORMAT %p output.
  std::string proto_value_str(kProtoValueString);
  // Remove first newline.
  RE2::Replace(&proto_value_str, "\n", "");
  // Remove multispaces (leave single spaces only).
  RE2::GlobalReplace(&proto_value_str, " [ ]+", "");
  // Convert all newlines to single space.
  RE2::GlobalReplace(&proto_value_str, "\n", " ");

  const Value proto_value = KitchenSink(kProtoValueString);
  const Value proto_null = Value::Null(KitchenSinkProtoType());
  const Value proto_array_value =
      Value::Array(MakeArrayType(KitchenSinkProtoType()),
                   {proto_value, proto_null, proto_value});

  return std::vector<FunctionTestCall>({
      // NULLs.
      {"format", {"%d", NullInt64()}, NullString()},
      {"format", {"%i", NullInt64()}, NullString()},
      {"format", {"%u", NullUint64()}, NullString()},
      {"format", {"%o", NullUint64()}, NullString()},
      {"format", {"%x", NullUint64()}, NullString()},
      {"format", {"%f", NullDouble()}, NullString()},
      {"format", {"%g", NullDouble()}, NullString()},
      {"format", {"%e", NullDouble()}, NullString()},
      {"format", {"%s", NullString()}, NullString()},
      // NULLs with apostrophe.
      {"format", {"%'d", NullInt64()}, NullString()},
      {"format", {"%'i", NullInt64()}, NullString()},
      {"format", {"%'u", NullUint64()}, NullString()},
      {"format", {"%'o", NullUint64()}, NullString()},
      {"format", {"%'x", NullUint64()}, NullString()},
      {"format", {"%'f", NullDouble()}, NullString()},
      {"format", {"%'g", NullDouble()}, NullString()},
      {"format", {"%'e", NullDouble()}, NullString()},
      {"format", {"%'s", NullString()}, NullString()},
      // NULL width.
      {"format", {"%*d", NullInt64(), 5}, NullString()},
      {"format", {"%*i", NullInt64(), 5}, NullString()},
      {"format", {"%*u", NullInt64(), Uint64(5)}, NullString()},
      {"format", {"%*o", NullInt64(), Uint64(5)}, NullString()},
      {"format", {"%*x", NullInt64(), Uint64(5)}, NullString()},
      {"format", {"%*f", NullInt64(), 1.5}, NullString()},
      {"format", {"%*g", NullInt64(), 1.5}, NullString()},
      {"format", {"%*e", NullInt64(), 1.5}, NullString()},
      {"format", {"%*s", NullInt64(), "abc"}, NullString()},
      // NULL precision.
      {"format", {"%.*d", NullInt64(), 5}, NullString()},
      {"format", {"%.*i", NullInt64(), 5}, NullString()},
      {"format", {"%.*u", NullInt64(), Uint64(5)}, NullString()},
      {"format", {"%.*o", NullInt64(), Uint64(5)}, NullString()},
      {"format", {"%.*x", NullInt64(), Uint64(5)}, NullString()},
      {"format", {"%.*f", NullInt64(), 1.5}, NullString()},
      {"format", {"%.*g", NullInt64(), 1.5}, NullString()},
      {"format", {"%.*e", NullInt64(), 1.5}, NullString()},
      {"format", {"%.*s", NullInt64(), "abc"}, NullString()},

      // %t and %T with arrays containing NULLs.
      {"format",
       {"%t", values::Array(types::Int32ArrayType(),
                            {Int32(5), NullInt32(), Int32(-6)})},
       "[5, NULL, -6]"},
      {"format",
       {"%T", values::Array(types::Int32ArrayType(),
                            {Int32(5), NullInt32(), Int32(-6)})},
       "[5, NULL, -6]"},
      {"format",
       {"%t", values::Array(types::FloatArrayType(),
                            {Float(5), NullFloat(), Float(-6)})},
       "[5.0, NULL, -6.0]"},
      {"format",
       {"%T", values::Array(types::FloatArrayType(),
                            {Float(5), NullFloat(), Float(-6)})},
       "[5.0, NULL, -6.0]"},
      {"format", {"%t", enum_array_value}, "[TESTENUM1, NULL, TESTENUM1]"},
      {"format",
       {"%T", enum_array_value},
       R"(["TESTENUM1", NULL, "TESTENUM1"])"},
      {"format",
       {"%t", proto_array_value},
       absl::Substitute("[$0, NULL, $0]", proto_value_str)},
      {"format",
       {"%T", proto_array_value},
       absl::Substitute("['$0', NULL, '$0']", proto_value_str)},
  });
}

}  // namespace googlesql
