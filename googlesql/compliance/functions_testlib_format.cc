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
#include <iterator>
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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/map_util.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

// These helper functions are defined in separate .cc files and compiled
// separately to avoid blowing up compile time for sanitizer builds.
extern std::vector<FunctionTestCall> GetFunctionTestsFormatFloatingPoint();
extern std::vector<FunctionTestCall> GetFunctionTestsFormatIntegral();
extern std::vector<FunctionTestCall> GetFunctionTestsFormatNulls();
extern std::vector<FunctionTestCall> GetFunctionTestsFormatStrings();
extern std::vector<FunctionTestCall> GetFunctionTestsFormatNumeric();
extern std::vector<FunctionTestCall> GetFunctionTestsFormatJson();
extern std::vector<FunctionTestCall> GetFunctionTestsFormatInterval();

static std::string Zeros(absl::string_view fmt, int zeros) {
  return absl::Substitute(fmt, std::string(zeros, '0'));
}

std::vector<FunctionTestCall> GetFunctionTestsFormat() {
  const StructType* struct0_type;
  const StructType* struct1_type;
  const StructType* struct2_type;
  const StructType* struct_anonymous1_type;
  const StructType* struct_anonymous2_type;
  const StructType* struct_anonymous3_type;
  const StructType* struct3_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType({{}}, &struct0_type));
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}}, &struct1_type));
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}, {"b", Int32Type()}}, &struct2_type));
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"", StringType()}}, &struct_anonymous1_type));
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}, {"", Int32Type()}}, &struct_anonymous2_type));
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"", StringType()}, {"", Int32Type()}}, &struct_anonymous3_type));
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}, {"b", DatetimeType()}, {"c", TimeType()}},
      &struct3_type));

  const Value datetime_micros = DatetimeMicros(2016, 4, 26, 15, 23, 27, 123456);
  const Value time_micros = TimeMicros(15, 23, 27, 123456);

  const Value struct0 =
      Value::Struct(struct0_type, {});
  const Value struct1 =
      Value::Struct(struct1_type, {String("foo")});
  const Value struct2 =
      Value::Struct(struct2_type, {String("foo"), Int32(0)});
  const Value struct2_null = Value::Null(struct2_type);
  const Value struct3_civil_time = Value::Struct(
      struct3_type, {String("bar"), datetime_micros, time_micros});

  const Value struct_anonymous1 =
      Value::Struct(struct_anonymous1_type, {String("foo")});
  const Value struct_anonymous2 =
      Value::Struct(struct_anonymous2_type, {String("foo"), Int32(0)});
  const Value struct_anonymous3 =
      Value::Struct(struct_anonymous3_type, {String("foo"), Int32(0)});

  const Value array_value =
      Value::Array(Int64ArrayType(), {Int64(0), Int64(1)});
  const Value array_null = Value::Null(Int64ArrayType());
  const Value array_datetime =
      Value::Array(types::DatetimeArrayType(),
                   {DatetimeMicros(1970, 1, 1, 0, 0, 0, 0), datetime_micros});
  const Value array_time = Value::Array(types::TimeArrayType(),
                                        {TimeMicros(0, 0, 0, 0), time_micros});

  const Value enum_value = Value::Enum(TestEnumType(), 1);
  const Value enum_null = Value::Null(TestEnumType());

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
  const Value proto_value = KitchenSink(kProtoValueString);
  // Clean up the string to make it the same as FORMAT %p output.
  std::string proto_value_str(kProtoValueString);
  // Remove first newline.
  RE2::Replace(&proto_value_str, "\n", "");
  // Remove multispaces (leave single spaces only).
  RE2::GlobalReplace(&proto_value_str, " [ ]+", "");
  // Convert all newlines to single space.
  RE2::GlobalReplace(&proto_value_str, "\n", " ");

  // Clean up the string to make it the same as FORMAT %P output.
  std::string proto_value_multiline_str(kProtoValueString);
  // Remove first newline.
  RE2::Replace(&proto_value_multiline_str, "\n", "");
  // Remove leading 8 spaces (but no more!).
  RE2::GlobalReplace(&proto_value_multiline_str, "[ ]{8}", "");
  // Add trailing newline.
  proto_value_multiline_str += "\n";

  const Value proto_null = Value::Null(KitchenSinkProtoType());
  const std::string civil_time_proto_str = absl::StrCat(
      "time_micros: ", time_micros.ToPacked64TimeMicros(),
      " datetime_micros: ", datetime_micros.ToPacked64DatetimeMicros(),
      " int64_key_1: 123");
  const std::string civil_time_proto_str_multiline = absl::StrCat(
      "time_micros: ", time_micros.ToPacked64TimeMicros(), "\n",
      "datetime_micros: ", datetime_micros.ToPacked64DatetimeMicros(), "\n",
      "int64_key_1: 123", "\n");
  const Value proto_civil_time_value = CivilTimeTypesSink(civil_time_proto_str);

  // Tests for different escaping characters and embedded quotes.
  // These tests do look tricky since they mix C++, proto and GoogleSQL
  // escaping/quoting rules.
  const std::string kEscapeCharsProtoValueString = R"(
    int64_key_1: 1
    int64_key_2: 2
    repeated_string_val: "foo'bar"
    repeated_string_val: "foo''bar"
    repeated_string_val: "foo`bar"
    repeated_string_val: "foo``bar"
    repeated_string_val: "foo\"bar"
    repeated_string_val: "foo\\bar"
  )";
  const Value escape_chars_proto_value =
      KitchenSink(kEscapeCharsProtoValueString);
  const std::string escape_chars_proto_str =
      R"(int64_key_1: 1 int64_key_2: 2 )"
      R"(repeated_string_val: "foo\'bar" )"
      R"(repeated_string_val: "foo\'\'bar" )"
      R"(repeated_string_val: "foo`bar" )"
      R"(repeated_string_val: "foo``bar" )"
      R"(repeated_string_val: "foo\"bar" )"
      R"(repeated_string_val: "foo\\bar")";
  const std::string escape_chars_proto_str_multiline =
      R"(int64_key_1: 1)"
      "\n"
      R"(int64_key_2: 2)"
      "\n"
      R"(repeated_string_val: "foo\'bar")"
      "\n"
      R"(repeated_string_val: "foo\'\'bar")"
      "\n"
      R"(repeated_string_val: "foo`bar")"
      "\n"
      R"(repeated_string_val: "foo``bar")"
      "\n"
      R"(repeated_string_val: "foo\"bar")"
      "\n"
      R"(repeated_string_val: "foo\\bar")"
      "\n";
  const std::string escape_chars_proto_sql_literal =
      R"("int64_key_1: 1 int64_key_2: 2 )"
      R"(repeated_string_val: \"foo\\'bar\" )"
      R"(repeated_string_val: \"foo\\'\\'bar\" )"
      R"(repeated_string_val: \"foo`bar\" )"
      R"(repeated_string_val: \"foo``bar\" )"
      R"(repeated_string_val: \"foo\\\"bar\" )"
      R"(repeated_string_val: \"foo\\\\bar\"")";

  const Value date = DateFromStr("2001-05-21");
  const Value date_null = Value::Null(types::DateType());
  const Value timestamp = TimestampFromStr("2001-05-21 13:51:36");
  const Value timestamp_null = Value::Null(types::TimestampType());

  const Value enum_array_value = Value::Array(
      MakeArrayType(TestEnumType()), {enum_value, enum_null, enum_value});
  const Value proto_array_value =
      Value::Array(MakeArrayType(KitchenSinkProtoType()),
                   {proto_value, proto_null, proto_value});

  std::vector<FunctionTestCall> test_cases({
      // Bad patterns.
      {"format", {"%Z", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%5", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%a", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%A", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%n", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%1$d", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%lld", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%Lf", 1.5}, NullString(), OUT_OF_RANGE},
      {"format", {"%"}, NullString(), OUT_OF_RANGE},
      {"format", {"%d%", 1}, NullString(), OUT_OF_RANGE},
      // Two, three, and four byte specifiers.
      {"format", {"%ωd", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%’d", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%🐱d", 1}, NullString(), OUT_OF_RANGE},

      // Bad argument count.
      {"format", {"%d"}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", 1, 2}, NullString(), OUT_OF_RANGE},
      {"format", {"abc", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%%", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%*d", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%*d", 1, 2, 3}, NullString(), OUT_OF_RANGE},
      {"format", {"%.*d", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%.*d", 1, 2, 3}, NullString(), OUT_OF_RANGE},
      {"format", {"%*.*d"}, NullString(), OUT_OF_RANGE},
      {"format", {"%*.*d", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%*.*d", 1, 2}, NullString(), OUT_OF_RANGE},
      {"format", {"%*.*d", 1, 2, 3, 4}, NullString(), OUT_OF_RANGE},
      {"format", {"%d%d", 1}, NullString(), OUT_OF_RANGE},
      {"format", {"%d%d", 1, 2, 3}, NullString(), OUT_OF_RANGE},

      // Bad width/position argument type.
      {"format", {"%*s", "abc", "def"}, NullString(), OUT_OF_RANGE},
      {"format", {"%.*s", "abc", "def"}, NullString(), OUT_OF_RANGE},
      {"format", {"%*.*s", "abc", "def", "ghi"}, NullString(), OUT_OF_RANGE},
      {"format", {"%*s", Uint32(10), "def"}, NullString(), OUT_OF_RANGE},
      {"format", {"%.*s", Uint64(10), "def"}, NullString(), OUT_OF_RANGE},
      {"format", {"%*s", 10.5, "def"}, NullString(), OUT_OF_RANGE},
      {"format", {"%.*s", Float(10), "def"}, NullString(), OUT_OF_RANGE},
      {"format", {"%*s", Int64(4), "ab"}, "  ab"},
      {"format", {"%*s", Int32(4), "ab"}, "  ab"},
      {"format",
       {"%*i", Int32(-2147483648), Int64(10)},
       NullString(),
       OUT_OF_RANGE},
      {"format",
       {"%*i", Int64(2147483648), Int64(10)},
       NullString(),
       OUT_OF_RANGE},
      {"format",
       {"%.*f", Int64(2147483648), Double(5)},
       NullString(),
       OUT_OF_RANGE},

      // All combinations of argument types.
      {"format", {"%d", Int32(5)}, "5"},
      {"format", {"%d", Int64(5)}, "5"},
      {"format", {"%d", Uint32(5)}, "5"},
      {"format", {"%d", Uint64(5)}, "5"},
      {"format", {"%d", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%i", Int32(5)}, "5"},
      {"format", {"%i", Int64(5)}, "5"},
      {"format", {"%i", Uint32(5)}, "5"},
      {"format", {"%i", Uint64(5)}, "5"},
      {"format", {"%i", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%i", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%i", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%i", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%i", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%i", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%u", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", Uint32(5)}, "5"},
      {"format", {"%u", Uint64(5)}, "5"},
      {"format", {"%u", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%u", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%x", Int32(30)}, "1e"},
      {"format", {"%x", Int64(30)}, "1e"},
      {"format", {"%x", Int32(-30)}, "-1e"},
      {"format", {"%x", Int64(-30)}, "-1e"},
      {"format",
       {"%x", Int64(std::numeric_limits<int64_t>::lowest())},
       "-8000000000000000"},
      {"format", {"%x", Uint32(30)}, "1e"},
      {"format", {"%x", Uint64(30)}, "1e"},
      {"format", {"%x", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%x", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%x", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%x", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%x", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%x", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%o", Int32(30)}, "36"},
      {"format", {"%o", Int64(30)}, "36"},
      {"format", {"%o", Int32(-30)}, "-36"},
      {"format", {"%o", Int64(-30)}, "-36"},
      {"format",
       {"%o", Int64(std::numeric_limits<int64_t>::lowest())},
       "-1000000000000000000000"},
      {"format", {"%o", Uint32(30)}, "36"},
      {"format", {"%o", Uint64(30)}, "36"},
      {"format", {"%o", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%o", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%o", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%o", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%o", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%o", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%f", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", Uint32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", Float(5)}, "5.000000"},
      {"format", {"%f", Double(5)}, "5.000000"},
      {"format", {"%f", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%f", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%g", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", Uint32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", Float(5)}, "5"},
      {"format", {"%g", Double(5)}, "5"},
      {"format", {"%g", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%g", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%e", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", Uint32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", Float(5)}, "5.000000e+00"},
      {"format", {"%e", Double(5)}, "5.000000e+00"},
      {"format", {"%e", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%e", proto_value}, NullString(), OUT_OF_RANGE},

      {"format", {"%p", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", Uint32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%p", proto_value}, proto_value_str},

      {"format", {"%P", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", Uint32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", String("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%P", proto_value}, proto_value_multiline_str},

      {"format", {"%s", Int32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Int64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Uint32(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Float(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Double(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", String("ab")}, "ab"},
      {"format", {"%s", Bytes("ab")}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", enum_value}, NullString(), OUT_OF_RANGE},

      // Huge widths, which should fail because of
      // --googlesql_format_max_output_width.
      {"format", {"%*d", 1000000000, 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%'*d", 1000000000, 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%*i", 1000000000, 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%'*i", 1000000000, 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%*u", 1000000000, Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%'*u", 1000000000, Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%*o", 1000000000, Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%'*o", 1000000000, Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%*x", 1000000000, Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%'*x", 1000000000, Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%*f", 1000000000, 1.5}, NullString(), OUT_OF_RANGE},
      {"format", {"%*g", 1000000000, 1.5}, NullString(), OUT_OF_RANGE},
      {"format", {"%*e", 1000000000, 1.5}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000d", 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%'1000000000d", 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000i", 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%'1000000000i", 5}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000u", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%'1000000000u", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000o", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%'1000000000o", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000x", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%'1000000000x", Uint64(5)}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000f", 1.5}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000g", 1.5}, NullString(), OUT_OF_RANGE},
      {"format", {"%1000000000e", 1.5}, NullString(), OUT_OF_RANGE},

      // Dates
      {"format", {"%t", date}, "2001-05-21"},
      {"format", {"%t", date_null}, "NULL"},
      {"format", {"%T", date}, "DATE \"2001-05-21\""},
      {"format", {"%T", date_null}, "NULL"},

      // Timestamps
      {"format", {"%t", timestamp}, "2001-05-21 13:51:36+00"},
      {"format", {"%t", timestamp_null}, "NULL"},
      {"format", {"%T", timestamp}, "TIMESTAMP \"2001-05-21 13:51:36+00\""},
      {"format", {"%T", timestamp_null}, "NULL"},

      // Enums
      {"format", {"%t", enum_value}, "TESTENUM1"},
      {"format", {"%t", enum_null}, "NULL"},
      {"format", {"%T", enum_value}, "\"TESTENUM1\""},
      {"format", {"%T", enum_null}, "NULL"},

      // Structs
      {"format", {"%t", struct0}, "()"},
      {"format", {"%t", struct1}, "(foo)"},
      {"format", {"%t", struct2}, "(foo, 0)"},
      {"format", {"%t", struct2_null}, "NULL"},
      {"format", {"%T", struct0}, "STRUCT()"},
      {"format", {"%T", struct1}, "STRUCT(\"foo\")"},
      {"format", {"%T", struct2}, "(\"foo\", 0)"},
      {"format", {"%T", struct2_null}, "NULL"},
      {"format", {"%T", struct_anonymous1}, "STRUCT(\"foo\")"},
      {"format", {"%T", struct_anonymous2}, "(\"foo\", 0)"},
      {"format", {"%T", struct_anonymous3}, "(\"foo\", 0)"},

      // Arrays
      {"format", {"%t", array_value}, "[0, 1]"},
      {"format", {"%t", array_null}, "NULL"},
      {"format", {"%T", array_value}, "[0, 1]"},
      {"format", {"%T", array_null}, "NULL"},

      // Protos
      {"format", {"%t", proto_value}, proto_value_str},
      {"format",
       {"%T", proto_value},
       absl::StrCat("\'", proto_value_str, "\'")},
      {"format", {"%p", proto_value}, proto_value_str},
      {"format", {"%P", proto_value}, proto_value_multiline_str},
      {"format", {"%t", proto_null}, "NULL"},
      {"format", {"%T", proto_null}, "NULL"},
      {"format", {"%p", proto_null}, NullString()},
      {"format", {"%P", proto_null}, NullString()},
      // Protos with fields annotated as datetime and time are always valid
      // regardless whether FEATURE_CIVIL_TIME is enabled or not, and they
      // will always be formatted as numeric values instead of being interpreted
      // as datetime or time.
      {"format", {"%t", proto_civil_time_value}, civil_time_proto_str},
      {"format",
       {"%T", proto_civil_time_value},
       absl::StrCat("\"", civil_time_proto_str, "\"")},
      {"format", {"%p", proto_civil_time_value}, civil_time_proto_str},
      {"format",
       {"%P", proto_civil_time_value},
       civil_time_proto_str_multiline},
      // Formatting protos with strings containing escape characters
      {"format", {"%t", escape_chars_proto_value}, escape_chars_proto_str},
      {"format",
       {"%T", escape_chars_proto_value},
       escape_chars_proto_sql_literal},
      {"format", {"%p", escape_chars_proto_value}, escape_chars_proto_str},
      {"format",
       {"%P", escape_chars_proto_value},
       escape_chars_proto_str_multiline},

      // Simple types with %t and %T.
      {"format", {"%t", Int64(15)}, "15"},
      {"format", {"%t", Int32(15)}, "15"},
      {"format", {"%t", Uint64(15)}, "15"},
      {"format", {"%t", Uint32(15)}, "15"},
      {"format", {"%t", Float(15.5)}, "15.5"},
      {"format", {"%t", Double(15.5)}, "15.5"},
      {"format", {"%t", Float(15)}, "15.0"},
      {"format", {"%t", Double(15)}, "15.0"},
      {"format", {"%t", String("abc")}, "abc"},
      {"format", {"%t", Bytes("abc")}, "abc"},
      {"format", {"%T", Int64(15)}, "15"},
      {"format", {"%T", Int32(15)}, "15"},
      {"format", {"%T", Uint64(15)}, "15"},
      {"format", {"%T", Uint32(15)}, "15"},
      {"format", {"%T", Float(15.5)}, "15.5"},
      {"format", {"%T", Double(15.5)}, "15.5"},
      {"format", {"%T", Float(15.0)}, "15.0"},
      {"format", {"%T", Double(15.0)}, "15.0"},
      {"format", {"%T", String("abc")}, "\"abc\""},
      {"format", {"%T", Bytes("abc")}, "b\"abc\""},

      // Width formatting on %t follows same rules as %s.
      {"format", {"%12t", enum_value}, "   TESTENUM1"},
      {"format", {"%-12t", enum_value}, "TESTENUM1   "},
      {"format", {"%6t", enum_value}, "TESTENUM1"},
      {"format", {"%-6t", enum_value}, "TESTENUM1"},
      {"format", {"%.3t", enum_value}, "TES"},
      {"format", {"%-.3t", enum_value}, "TES"},
      {"format", {"%6.3t", enum_value}, "   TES"},
      {"format", {"%-6.3t", enum_value}, "TES   "},
      {"format", {"%+#06.3t", enum_value}, "   TES"},
      {"format", {"%*.*t", 6, 3, enum_value}, "   TES"},
      {"format", {"%*.*T", 6, 3, enum_value}, "   \"TE"},
      // Same applies even for integers formatted with %t.
      {"format", {"%6.3t", 10000}, "   100"},
      {"format", {"%6.3d", 10000}, " 10000"},

      // Types that only work with %t don't work with other formats.
      {"format", {"%s", date}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", timestamp}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", proto_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", array_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", struct2}, NullString(), OUT_OF_RANGE},
      {"format", {"%s", Value::Bytes("abc")}, NullString(), OUT_OF_RANGE},

      {"format", {"%d", date}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", timestamp}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", enum_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", proto_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", array_value}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", struct2}, NullString(), OUT_OF_RANGE},
      {"format", {"%d", Value::Bytes("abc")}, NullString(), OUT_OF_RANGE},
  });

  std::vector<FunctionTestCall> fp_test_cases =
      GetFunctionTestsFormatFloatingPoint();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(fp_test_cases.begin()),
                    std::make_move_iterator(fp_test_cases.end()));

  std::vector<FunctionTestCall> integral_test_cases =
      GetFunctionTestsFormatIntegral();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(integral_test_cases.begin()),
                    std::make_move_iterator(integral_test_cases.end()));

  std::vector<FunctionTestCall> null_test_cases = GetFunctionTestsFormatNulls();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(null_test_cases.begin()),
                    std::make_move_iterator(null_test_cases.end()));

  std::vector<FunctionTestCall> string_test_cases =
      GetFunctionTestsFormatStrings();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(string_test_cases.begin()),
                    std::make_move_iterator(string_test_cases.end()));

  std::vector<FunctionTestCall> numeric_test_cases =
      GetFunctionTestsFormatNumeric();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(numeric_test_cases.begin()),
                    std::make_move_iterator(numeric_test_cases.end()));

  std::vector<FunctionTestCall> json_test_cases = GetFunctionTestsFormatJson();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(json_test_cases.begin()),
                    std::make_move_iterator(json_test_cases.end()));

  std::vector<FunctionTestCall> interval_test_cases =
      GetFunctionTestsFormatInterval();
  test_cases.insert(test_cases.end(),
                    std::make_move_iterator(interval_test_cases.begin()),
                    std::make_move_iterator(interval_test_cases.end()));

  const std::vector<CivilTimeTestCase> civil_time_test_cases({
      {{{"%t", datetime_micros}}, String("2016-04-26 15:23:27.123456")},
      {{{"%T", datetime_micros}},
       String("DATETIME \"2016-04-26 15:23:27.123456\"")},
      {{{"%t", Value::NullDatetime()}}, String("NULL")},
      {{{"%T", Value::NullDatetime()}}, String("NULL")},

      {{{"%t", time_micros}}, String("15:23:27.123456")},
      {{{"%T", time_micros}}, String("TIME \"15:23:27.123456\"")},
      {{{"%t", Value::NullTime()}}, String("NULL")},
      {{{"%T", Value::NullTime()}}, String("NULL")},

      {{{"%t", struct3_civil_time}},
       String("(bar, 2016-04-26 15:23:27.123456, 15:23:27.123456)")},
      {{{"%T", struct3_civil_time}},
       String("(\"bar\", DATETIME \"2016-04-26 15:23:27.123456\", "
              "TIME \"15:23:27.123456\")")},

      {{{"%t", array_datetime}},
       String("[1970-01-01 00:00:00, 2016-04-26 15:23:27.123456]")},
      {{{"%T", array_datetime}},
       String("[DATETIME \"1970-01-01 00:00:00\","
              " DATETIME \"2016-04-26 15:23:27.123456\"]")},
      {{{"%t", array_time}}, String("[00:00:00, 15:23:27.123456]")},
      {{{"%T", array_time}},
       String("[TIME \"00:00:00\", TIME \"15:23:27.123456\"]")},
  });
  for (const auto& each : civil_time_test_cases) {
    AddTestCaseWithWrappedResultForCivilTimeAndNanos(each, "format",
                                                     &test_cases);
  }

  return test_cases;
}

}  // namespace googlesql
