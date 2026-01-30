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

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/format_max_output_width.h"
#include "googlesql/public/functions/string_format.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/status/statusor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
namespace functions {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

// Type of an implementation of Format, used to abstract out the actual
// implementation of format.
using FormatF = std::function<absl::Status(
    absl::string_view format_string, absl::Span<const Value> values,
    ProductMode product_mode, std::string* output, bool* is_null,
    bool canonicalize_zero, bool use_external_float32)>;

// Call `FormatFunction` and abstract error and null results into a
// unified string representation. Also ensures error codes are of an
// allowed type.
static std::string TestFormatFunction(const FormatF& FormatFunction,
                                      ProductMode product_mode,
                                      bool canonicalize_zero,
                                      bool use_external_float32,
                                      absl::string_view format,
                                      absl::Span<const Value> values) {
  std::string output;
  bool is_null;
  const absl::Status status =
      FormatFunction(format, values, product_mode, &output, &is_null,
                     canonicalize_zero, use_external_float32);
  if (status.ok()) {
    return is_null ? "<null>" : output;
  } else {
    EXPECT_THAT(status.code(),
                testing::AnyOf(testing::Eq(absl::StatusCode::kInvalidArgument),
                               testing::Eq(absl::StatusCode::kOutOfRange)));
    return absl::StrCat("ERROR: ", status.message());
  }
}

// Type of `TestFormatFunction` bound to a particular implementation of format.
using TestFormatF = std::function<std::string(
    absl::string_view, const std::vector<Value>& values)>;

// Wrapper used to deal with abstracting the implementation of Format,
// especially in parameterized tests.
struct FormatFunctionParam {
  std::string scope_label;
  FormatF FormatFunction;

  // Binds `FormatFunction` to TestFormatFunction.
  TestFormatF WrapTestFormat(ProductMode product_mode,
                             bool use_external_float32) const {
    return absl::bind_front(TestFormatFunction, FormatFunction, product_mode,
                            /*canonicalize_zero=*/true, use_external_float32);
  }
  // String suitable for use in SCOPE-TRACE to aid in debugging failing tests.
  std::string ScopeLabel() const {
    // Try to make the scope really pop while debugging.
    return absl::StrCat("\n\nFormat Function: ", scope_label, "\n");
  }
};

std::vector<FormatFunctionParam> GetFormatFunctionParams() {
  return {
          FormatFunctionParam{"StringFormatUtf8", StringFormatUtf8}};
}

class FormatFunctionTests
    : public ::testing::TestWithParam<FormatFunctionParam> {};

TEST_P(FormatFunctionTests, Test) {
  SCOPED_TRACE(GetParam().ScopeLabel());
  TestFormatF TestFormat = GetParam().WrapTestFormat(
      PRODUCT_INTERNAL, /*use_external_float32=*/false);

  using values::Int32;
  using values::Int64;
  using values::Uint32;
  using values::Uint64;
  using values::Float;
  using values::Double;
  using values::String;
  using values::Bytes;

  EXPECT_EQ("", TestFormat("", {}));
  EXPECT_EQ("abc", TestFormat("abc", {}));
  EXPECT_EQ("%", TestFormat("%%", {}));
  EXPECT_EQ("5", TestFormat("%d", {Int64(5)}));
  EXPECT_EQ("5", TestFormat("%d", {Int32(5)}));
  EXPECT_EQ("dd5d", TestFormat("dd%dd", {Int64(5)}));
  EXPECT_EQ("dd5-5d", TestFormat("dd%d%dd", {Int64(5), Int64(-5)}));
  EXPECT_EQ("  5, -5", TestFormat("%3d,%3d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("005,-05", TestFormat("%03d,%03d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("5  ,-5 ", TestFormat("%-3d,%-3d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("5  ,-5 ", TestFormat("%-03d,%-03d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("5  ,-5 ", TestFormat("%0-3d,%0-3d", {Int64(5), Int64(-5)}));

  EXPECT_EQ("9223372036854775807",
            TestFormat("%d", {Int64(std::numeric_limits<int64_t>::max())}));
  EXPECT_EQ("-9223372036854775808",
            TestFormat("%d", {Int64(std::numeric_limits<int64_t>::lowest())}));
  EXPECT_EQ("18446744073709551615",
            TestFormat("%d", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ("9223372036854775807",
            TestFormat("%i", {Int64(std::numeric_limits<int64_t>::max())}));
  EXPECT_EQ("-9223372036854775808",
            TestFormat("%i", {Int64(std::numeric_limits<int64_t>::lowest())}));
  EXPECT_EQ("18446744073709551615",
            TestFormat("%i", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ("18446744073709551615",
            TestFormat("%u", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ("18446744073709551615",
            TestFormat("%u", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ("ffffffffffffffff",
            TestFormat("%x", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ("0XFFFFFFFFFFFFFFFF",
            TestFormat("%#X", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ("1777777777777777777777",
            TestFormat("%o", {Uint64(std::numeric_limits<uint64_t>::max())}));

  // Test for invalid formats
  EXPECT_EQ(
      "ERROR: Invalid format specifier character \"l\" in "
      "FORMAT string: \"%lld\"",
      TestFormat("%lld", {Int64(0)}));
  EXPECT_EQ(
      "ERROR: Invalid format specifier character \"L\" in "
      "FORMAT string: \"%Lf\"",
      TestFormat("%Lf", {Int64(0)}));

  EXPECT_EQ("ERROR: Invalid FORMAT string: \"%\"", TestFormat("%", {Int64(0)}));
  EXPECT_EQ("ERROR: Invalid FORMAT string: \"a%d%\"",
            TestFormat("a%d%", {Int64(0)}));
  EXPECT_EQ(
      "ERROR: Invalid format specifier character \"Z\" in "
      "FORMAT string: \"%Z\"",
      TestFormat("%Z", {Int64(0)}));
  EXPECT_EQ(
      "ERROR: Invalid format specifier character \"(\" in "
      "FORMAT string: \"%(\"",
      TestFormat("%(", {Int64(0)}));

  EXPECT_EQ(
      "ERROR: Invalid format specifier character \"’\" in FORMAT string: "
      "\"%’d\"",
      TestFormat("%’d", {Int64(0)}));

  // These formats are valid in some sprintf implementations.
  EXPECT_EQ(
      "ERROR: Invalid format specifier character \"$\" in "
      "FORMAT string: \"%1$d\"",
      TestFormat("%1$d", {Int64(0)}));

  // Tests for incorrect number of arguments (see below for specific cases
  // involving '*.*').
  //  Too many arguments
  EXPECT_EQ(
      "ERROR: Too many arguments to FORMAT for pattern \"abc\"; Expected 1; "
      "Got 2",
      TestFormat("abc", {Int64(0)}));
  EXPECT_EQ(
      "ERROR: Too many arguments to FORMAT for pattern \"abc\"; Expected 1; "
      "Got 3",
      TestFormat("abc", {Int64(0), Int64(0)}));
  EXPECT_EQ(
      "ERROR: Too many arguments to FORMAT for pattern \"%d%s\"; Expected 3; "
      "Got 4",
      TestFormat("%d%s", {Int64(0), String(""), Int64(0)}));

  // Too Few
  EXPECT_EQ(
      "ERROR: Too few arguments to FORMAT for pattern \"abc%d\"; Expected 2; "
      "Got 1",
      TestFormat("abc%d", {}));
  EXPECT_EQ(
      "ERROR: Too few arguments to FORMAT for pattern \"abc%d%s\"; Expected 3; "
      "Got 2",
      TestFormat("abc%d%s", {Int64(0)}));
  EXPECT_EQ(
      "ERROR: Too few arguments to FORMAT for pattern \"abc%s%d\"; Expected 3; "
      "Got 2",
      TestFormat("abc%s%d", {String("abc")}));

  // Multiple errors types (invalid format _and_ wrong number of arguments),
  // _which_ error we see doesn't matter.
  EXPECT_THAT(TestFormat("%lld", {}),
              testing::AnyOf("ERROR: Invalid format specifier character \"l\" "
                             "in FORMAT string: \"%lld\"",
                             "ERROR: Too few arguments to FORMAT for pattern "
                             "\"%lld\"; Expected 2; Got 1"));

  EXPECT_THAT(
      TestFormat("%Lf", {}),
      testing::AnyOf("ERROR: Invalid format specifier character \"L\" in "
                     "FORMAT string: \"%Lf\"",
                     "ERROR: Too few arguments to FORMAT for pattern \"%Lf\"; "
                     "Expected 2; Got 1"));

  EXPECT_THAT(TestFormat("%", {}),
              testing::AnyOf("ERROR: Invalid FORMAT string: \"%\"",
                             "ERROR: Too few arguments to FORMAT for pattern "
                             "\"%Lf\"; Expected 2; Got 1"));

  EXPECT_THAT(TestFormat("a%d%", {}),
              testing::AnyOf("ERROR: Invalid FORMAT string: \"a%d%\"",
                             "ERROR: Too few arguments to FORMAT for pattern "
                             "\"a%d%\"; Expected 2; Got 1"));
  EXPECT_THAT(
      TestFormat("%Z", {}),
      testing::AnyOf("ERROR: Invalid format specifier character \"Z\" in "
                     "FORMAT string: \"%Z\"",
                     "ERROR: Too few arguments to FORMAT for pattern \"%Z\"; "
                     "Expected 2; Got 1"));
  EXPECT_THAT(
      TestFormat("%(", {}),
      testing::AnyOf("ERROR: Invalid format specifier character \"(\" in "
                     "FORMAT string: \"%(\"",
                     "ERROR: Too few arguments to FORMAT for pattern \"%(\"; "
                     "Expected 2; Got 1"));

  EXPECT_EQ("5", TestFormat("%*d", {Int64(1), Int64(5)}));
  EXPECT_EQ(" 5", TestFormat("%*d", {Int64(2), Int64(5)}));
  EXPECT_EQ("005", TestFormat("%0*d", {Int64(3), Int64(5)}));

  // Test non-positive width.
  EXPECT_EQ("5", TestFormat("%*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("5   ", TestFormat("%*d", {Int64(-4), Int64(5)}));
  EXPECT_EQ("5   ", TestFormat("%-*d", {Int64(-4), Int64(5)}));
  EXPECT_EQ("5   ", TestFormat("%-*d", {Int64(4), Int64(5)}));

  // Test non-positive precision.
  EXPECT_EQ("   5", TestFormat("%4.*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("   5", TestFormat("%4.*d", {Int64(-2), Int64(5)}));
  EXPECT_EQ("   5", TestFormat("%4.*d", {Int64(-6), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%.*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%.*d", {Int64(-2), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%.*d", {Int64(-6), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%-.*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%-.*d", {Int64(-2), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%-.*d", {Int64(-6), Int64(5)}));

  EXPECT_EQ(" 05", TestFormat("%3.2d", {Int64(5)}));
  EXPECT_EQ(" 05", TestFormat("%*.2d", {Int64(3), Int64(5)}));
  EXPECT_EQ(" 05", TestFormat("%3.*d", {Int64(2), Int64(5)}));
  EXPECT_EQ(" 05", TestFormat("%*.*d", {Int64(3), Int64(2), Int64(5)}));

  EXPECT_EQ("5,-5", TestFormat("%#d,%#d", {Int64(5), Int64(-5)}));

  EXPECT_EQ("ERROR: Too many arguments to FORMAT for pattern \"abc\"; "
            "Expected 1; Got 2",
            TestFormat("abc", {Int64(5)}));

  EXPECT_EQ("ERROR: Too few arguments to FORMAT for pattern \"%d\"; "
            "Expected 2; Got 1",
            TestFormat("%d", {}));
  EXPECT_EQ("ERROR: Too few arguments to FORMAT for pattern \"%*d\"; "
            "Expected 3; Got 2",
            TestFormat("%*d", {Int64(5)}));
  EXPECT_EQ("ERROR: Too few arguments to FORMAT for pattern \"%.*d\"; "
            "Expected 3; Got 2",
            TestFormat("%.*d", {Int64(5)}));
  EXPECT_EQ("ERROR: Too few arguments to FORMAT for pattern \"%*.*d\"; "
            "Expected 4; Got 3",
            TestFormat("%*.*d", {Int64(5), Int64(5)}));

  EXPECT_EQ("1.500000", TestFormat("%f", {Float(1.5)}));
  EXPECT_EQ("1.500000", TestFormat("%f", {Double(1.5)}));
  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected DOUBLE; Got INT64",
            TestFormat("%f", {Int64(1)}));
  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected DOUBLE; Got UINT64",
            TestFormat("%f", {Uint64(1)}));

  // Precision is lost in floats, so just check few first digits.
  EXPECT_THAT(TestFormat("%f", {Float(1.5e20)}),
              testing::StartsWith("150000003006131"));
  EXPECT_THAT(TestFormat("%F", {Float(1.5e20)}),
              testing::StartsWith("150000003006131"));
  EXPECT_EQ("1.500000e+20", TestFormat("%e", {Float(1.5e20)}));
  EXPECT_EQ("1.500000E+20", TestFormat("%E", {Float(1.5e20)}));
  EXPECT_EQ("1.5e+20", TestFormat("%g", {Float(1.5e20)}));
  EXPECT_EQ("1.5E+20", TestFormat("%G", {Float(1.5e20)}));

  EXPECT_EQ("1500", TestFormat("%u", {Uint64(1500)}));
  EXPECT_EQ("1500", TestFormat("%u", {Uint32(1500)}));
  EXPECT_EQ("5dc", TestFormat("%x", {Uint64(1500)}));
  EXPECT_EQ("5DC", TestFormat("%X", {Uint32(1500)}));
  EXPECT_EQ("0x5dc", TestFormat("%#x", {Uint64(1500)}));
  EXPECT_EQ("0X5DC", TestFormat("%#X", {Uint32(1500)}));
  EXPECT_EQ("5dc", TestFormat("%x", {Int64(1500)}));
  EXPECT_EQ("5DC", TestFormat("%X", {Int32(1500)}));
  EXPECT_EQ("0x5dc", TestFormat("%#x", {Int64(1500)}));
  EXPECT_EQ("0X5DC", TestFormat("%#X", {Int32(1500)}));

  EXPECT_EQ("-2734", TestFormat("%o", {Int32(-1500)}));
  EXPECT_EQ("-5dc", TestFormat("%x", {Int32(-1500)}));
  EXPECT_EQ("-5DC", TestFormat("%X", {Int32(-1500)}));

  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected UINT; Got INT64",
            TestFormat("%u", {Int64(1500)}));
  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected UINT; Got INT32",
            TestFormat("%u", {Int32(1500)}));

  EXPECT_EQ("2734", TestFormat("%o", {Uint64(1500)}));
  EXPECT_EQ("02734", TestFormat("%#o", {Uint64(1500)}));
  EXPECT_EQ("  2734", TestFormat("%6o", {Uint64(1500)}));
  EXPECT_EQ(" 02734", TestFormat("%#6o", {Uint64(1500)}));
  EXPECT_EQ("002734", TestFormat("%06o", {Uint64(1500)}));
  EXPECT_EQ("02734", TestFormat("%#03o", {Uint64(1500)}));
  EXPECT_EQ("002734", TestFormat("%#06o", {Uint64(1500)}));
  EXPECT_EQ("2734", TestFormat("%o", {Int64(1500)}));
  EXPECT_EQ("02734", TestFormat("%#o", {Int64(1500)}));
  EXPECT_EQ("  2734", TestFormat("%6o", {Int64(1500)}));
  EXPECT_EQ(" 02734", TestFormat("%#6o", {Int64(1500)}));
  EXPECT_EQ("002734", TestFormat("%06o", {Int64(1500)}));
  EXPECT_EQ("02734", TestFormat("%#03o", {Int64(1500)}));
  EXPECT_EQ("002734", TestFormat("%#06o", {Int64(1500)}));

  EXPECT_EQ("", TestFormat("%s", {String("")}));
  EXPECT_EQ("abc", TestFormat("%s", {String("abc")}));
  EXPECT_EQ("abc", TestFormat("%1s", {String("abc")}));
  EXPECT_EQ("  abc", TestFormat("%5s", {String("abc")}));
  EXPECT_EQ("abc  ", TestFormat("%-5s", {String("abc")}));
  EXPECT_EQ("  abc", TestFormat("%*s", {Int64(5), String("abc")}));
  EXPECT_EQ("abc  ", TestFormat("%*s", {Int64(-5), String("abc")}));
  EXPECT_EQ("abc  ", TestFormat("%-*s", {Int64(5), String("abc")}));
  EXPECT_EQ("abc  ", TestFormat("%-*s", {Int64(-5), String("abc")}));
  EXPECT_EQ("  abc", TestFormat("%5.4s", {String("abc")}));
  EXPECT_EQ("    a", TestFormat("%5.1s", {String("abc")}));
  EXPECT_EQ("   ab", TestFormat("%5.*s", {Int64(2), String("abc")}));
  EXPECT_EQ("ab", TestFormat("%.2s", {String("abc")}));
  EXPECT_EQ("abc", TestFormat("%2.4s", {String("abc")}));
  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected STRING; Got INT64",
            TestFormat("%s", {Int64(1500)}));
  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected STRING; Got BYTES",
            TestFormat("%s", {Bytes("abc")}));
  EXPECT_EQ("ERROR: Invalid type for argument 3 to FORMAT; "
            "Expected STRING; Got BYTES",
            TestFormat("%*s", {Int64(12), Bytes("abc")}));

  // These return a NULL value, not the string "NULL".
  EXPECT_EQ("<null>", TestFormat("%d", {values::NullInt64()}));
  EXPECT_EQ("<null>", TestFormat("%s", {values::NullString()}));
  EXPECT_EQ("<null>", TestFormat("%u", {values::NullUint32()}));
  EXPECT_EQ("<null>", TestFormat("%f", {values::NullDouble()}));
  EXPECT_EQ("<null>", TestFormat("%*d", {values::NullInt64(), Int64(10)}));
  EXPECT_EQ("<null>", TestFormat("%.*d", {values::NullInt64(), Int64(10)}));
  EXPECT_EQ("<null>", TestFormat("%d %d %d",
                                 {Int64(1), values::NullInt64(), Int64(3)}));
  EXPECT_EQ("ERROR: Invalid type for argument 2 to FORMAT; "
            "Expected integer; Got STRING",
            TestFormat("%d", {values::NullString()}));

  // Apostrophe flag.
  EXPECT_EQ("1,234,567", TestFormat("%'d", {Int64(1234567)}));
  EXPECT_EQ("1,234,567", TestFormat("%'i", {Int64(1234567)}));
  EXPECT_EQ("1,234,567", TestFormat("%'u", {Uint64(1234567)}));
  EXPECT_EQ("455,3207",  TestFormat("%'o", {Uint64(1234567)}));
  EXPECT_EQ("12:d687",   TestFormat("%'x", {Uint64(1234567)}));
  EXPECT_EQ("12:D687",   TestFormat("%'X", {Uint64(1234567)}));
  EXPECT_EQ("1,234,567.000000", TestFormat("%'f", {Float(1234567)}));
  EXPECT_EQ("1,234,567.000000", TestFormat("%'F", {Float(1234567)}));
  EXPECT_EQ("1.234567e+06",   TestFormat("%'e", {Float(1234567)}));
  EXPECT_EQ("1.234567E+06",   TestFormat("%'E", {Float(1234567)}));
  EXPECT_EQ("1.23456e+08",    TestFormat("%'g", {Float(123456000)}));
  EXPECT_EQ("1.23456E+08",    TestFormat("%'G", {Float(123456000)}));
  // Apostrophe is ignored for all other flags (including %t and %T)
  EXPECT_EQ("1234567", TestFormat("%'t", {Int64(1234567)}));
  EXPECT_EQ("1234567", TestFormat("%'T", {Int64(1234567)}));
  EXPECT_EQ("1234567", TestFormat("%'s", {String("1234567")}));

  // Apostrophe in combination with other flags/width/precision.
  EXPECT_EQ("  5, -5", TestFormat("%'3d,%'3d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("005,-05", TestFormat("%'03d,%'03d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("5  ,-5 ", TestFormat("%'-3d,%'-3d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("5  ,-5 ", TestFormat("%'-03d,%'-03d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("5  ,-5 ", TestFormat("%'0-3d,%'0-3d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("0XFFFF:FFFF:FFFF:FFFF",
            TestFormat("%'#X", {Uint64(std::numeric_limits<uint64_t>::max())}));
  EXPECT_EQ(" 05", TestFormat("%'3.2d", {Int64(5)}));
  EXPECT_EQ("5,-5", TestFormat("%'#d,%'#d", {Int64(5), Int64(-5)}));
  EXPECT_EQ("0x5dc", TestFormat("%'#x", {Uint64(1500)}));
  EXPECT_EQ("0X5DC", TestFormat("%'#X", {Uint32(1500)}));
  EXPECT_EQ("02734", TestFormat("%'#o", {Uint64(1500)}));
  EXPECT_EQ("  2734", TestFormat("%'6o", {Uint64(1500)}));
  EXPECT_EQ(" 02734", TestFormat("%'#6o", {Uint64(1500)}));
  EXPECT_EQ("002734", TestFormat("%'06o", {Uint64(1500)}));
  EXPECT_EQ("02734", TestFormat("%'#03o", {Uint64(1500)}));
  EXPECT_EQ("002734", TestFormat("%'#06o", {Uint64(1500)}));
  EXPECT_EQ("5", TestFormat("%'*d", {Int64(1), Int64(5)}));
  EXPECT_EQ(" 5", TestFormat("%'*d", {Int64(2), Int64(5)}));
  EXPECT_EQ("005", TestFormat("%'0*d", {Int64(3), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("5   ", TestFormat("%'*d", {Int64(-4), Int64(5)}));
  EXPECT_EQ("5   ", TestFormat("%'-*d", {Int64(-4), Int64(5)}));
  EXPECT_EQ("5   ", TestFormat("%'-*d", {Int64(4), Int64(5)}));
  EXPECT_EQ("   5", TestFormat("%'4.*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("   5", TestFormat("%'4.*d", {Int64(-2), Int64(5)}));
  EXPECT_EQ("   5", TestFormat("%'4.*d", {Int64(-6), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'.*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'.*d", {Int64(-2), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'.*d", {Int64(-6), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'-.*d", {Int64(0), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'-.*d", {Int64(-2), Int64(5)}));
  EXPECT_EQ("5", TestFormat("%'-.*d", {Int64(-6), Int64(5)}));
  EXPECT_EQ(" 05", TestFormat("%'*.2d", {Int64(3), Int64(5)}));
  EXPECT_EQ(" 05", TestFormat("%'3.*d", {Int64(2), Int64(5)}));
  EXPECT_EQ(" 05", TestFormat("%'*.*d", {Int64(3), Int64(2), Int64(5)}));
  EXPECT_EQ("abc 12,345 12345", TestFormat("%s %'d %d", {String("abc"),
                                                         Int64(12345),
                                                         Int64(12345)}));

  // For %t and %T, we get the string "NULL", not a NULL value.
  EXPECT_EQ("x NULL", TestFormat("x %t", {values::NullInt64()}));
  EXPECT_EQ("x NULL", TestFormat("x %t", {values::NullString()}));
  EXPECT_EQ("x NULL", TestFormat("x %t", {values::NullBytes()}));
  EXPECT_EQ("x NULL", TestFormat("x %t", {values::NullTimestamp()}));
  EXPECT_EQ("x NULL", TestFormat("x %T", {values::NullInt64()}));
  EXPECT_EQ("x NULL", TestFormat("x %T", {values::NullString()}));
  EXPECT_EQ("x NULL", TestFormat("x %T", {values::NullBytes()}));
  EXPECT_EQ("x NULL", TestFormat("x %T", {values::NullTimestamp()}));

  const Value date = values::Date(12345);
  const Value timestamp =
      values::TimestampFromUnixMicros(1429322158123456);
  const Value datetime = Value::Datetime(
      DatetimeValue::FromYMDHMSAndMicros(2016, 4, 26, 14, 53, 38, 123456));
  const Value time =
      Value::Time(TimeValue::FromHMSAndNanos(15, 12, 47, 987654123));

  TypeFactory type_factory;
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                      &enum_type));
  const Value enum_value = Value::Enum(enum_type, 2);

  EXPECT_EQ("5", TestFormat("%t", {values::Int64(5)}));
  EXPECT_EQ("5", TestFormat("%t", {values::Int32(5)}));
  EXPECT_EQ("true", TestFormat("%t", {values::Bool(true)}));
  EXPECT_EQ("abc", TestFormat("%t", {values::String("abc")}));
  EXPECT_EQ("abc", TestFormat("%t", {values::Bytes("abc")}));
  EXPECT_EQ("2003-10-20", TestFormat("%t", {date}));
  EXPECT_EQ("2016-04-26 14:53:38.123456", TestFormat("%t", {datetime}));
  EXPECT_EQ("15:12:47.987654123", TestFormat("%t", {time}));
  EXPECT_EQ("2015-04-18 01:55:58.123456+00", TestFormat("%t", {timestamp}));
  EXPECT_EQ("TESTENUM2", TestFormat("%t", {enum_value}));

  EXPECT_EQ("-5", TestFormat("%T", {values::Int64(-5)}));
  EXPECT_EQ("-5", TestFormat("%T", {values::Int32(-5)}));
  EXPECT_EQ("5", TestFormat("%T", {values::Uint64(5)}));
  EXPECT_EQ("5", TestFormat("%T", {values::Uint32(5)}));
  EXPECT_EQ("0.1", TestFormat("%T", {values::Float(0.1)}));
  EXPECT_EQ("0.1", TestFormat("%T", {values::Double(0.1)}));

  // Tests to check that positive and negative NaNs are formatted as "nan".
  const float kNegativeFloatNan = absl::bit_cast<float>(0xffc00000u);
  const double kNegativeDoubleNan =
      absl::bit_cast<double>(0xfff8000000000000ul);
  EXPECT_EQ(
      "CAST(\"nan\" AS FLOAT32)",
      TestFormat("%T",
                 {values::Float(std::numeric_limits<float>::quiet_NaN())}));
  EXPECT_EQ(
      "nan",
      TestFormat("%t",
                 {values::Float(std::numeric_limits<float>::quiet_NaN())}));
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)",
            TestFormat("%T", {values::Float(kNegativeFloatNan)}));
  EXPECT_EQ("nan", TestFormat("%t", {values::Float(kNegativeFloatNan)}));
  EXPECT_EQ(
      "CAST(\"nan\" AS FLOAT64)",
      TestFormat("%T",
                 {values::Double(std::numeric_limits<double>::quiet_NaN())}));
  EXPECT_EQ(
      "nan",
      TestFormat("%t",
                 {values::Double(std::numeric_limits<double>::quiet_NaN())}));
  EXPECT_EQ("CAST(\"nan\" AS FLOAT64)",
            TestFormat("%T", {values::Double(kNegativeDoubleNan)}));
  EXPECT_EQ("nan", TestFormat("%t", {values::Double(kNegativeDoubleNan)}));

  EXPECT_EQ("true", TestFormat("%T", {values::Bool(true)}));
  EXPECT_EQ("\"abc\"", TestFormat("%T", {values::String("abc")}));
  EXPECT_EQ("b\"abc\"", TestFormat("%T", {values::Bytes("abc")}));
  EXPECT_EQ("DATE \"2003-10-20\"", TestFormat("%T", {date}));
  EXPECT_EQ("DATETIME \"2016-04-26 14:53:38.123456\"",
            TestFormat("%T", {datetime}));
  EXPECT_EQ("TIME \"15:12:47.987654123\"", TestFormat("%T", {time}));
  EXPECT_EQ("TIMESTAMP \"2015-04-18 01:55:58.123456+00\"",
            TestFormat("%T", {timestamp}));
  EXPECT_EQ("\"TESTENUM2\"", TestFormat("%T", {enum_value}));

  // Width and precision and modifiers for %t and %T act like %s.
  EXPECT_EQ("2003-10-20", TestFormat("%4t", {date}));
  EXPECT_EQ("  2003-10-20", TestFormat("%12t", {date}));
  EXPECT_EQ("          20", TestFormat("%12.2t", {date}));
  EXPECT_EQ("  2003-10-20", TestFormat("%12.15t", {date}));
  EXPECT_EQ("2003-10-20  ", TestFormat("%-12t", {date}));
  EXPECT_EQ("  2003-10-20", TestFormat("%*t", {Int64(12), date}));
  EXPECT_EQ("2003-10-20  ", TestFormat("%*t", {Int64(-12), date}));
  EXPECT_EQ("2003-10-20  ", TestFormat("%-*t", {Int64(12), date}));
  EXPECT_EQ("2003-10-20  ", TestFormat("%-*t", {Int64(-12), date}));
  EXPECT_EQ("   \"abc\"", TestFormat("%8T", {values::String("abc")}));
  EXPECT_EQ("\"abc\"   ", TestFormat("%-8T", {values::String("abc")}));
  EXPECT_EQ("  NULL", TestFormat("%6T", {values::NullString()}));
  EXPECT_EQ("    NU", TestFormat("%6.2t", {values::NullString()}));
  EXPECT_EQ("NULL", TestFormat("%3t", {values::NullString()}));
  EXPECT_EQ("NUL", TestFormat("%.3T", {values::NullString()}));
  EXPECT_EQ("NUL", TestFormat("%1.3t", {values::NullString()}));

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"f1", types::StringType()}, {"", types::Int32Type()}}, &struct_type));
  const Value struct_value = Value::Struct(
      struct_type, {Value::String("abc"), Value::Int32(123)});

  const ArrayType* array_type = types::Int64ArrayType();

  const Value array_value = values::Int64Array({1, 2, 3});

  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::TestExtraPB::descriptor(), &proto_type));

  googlesql_test::TestExtraPB proto_pb;
  proto_pb.set_int32_val1(123);
  proto_pb.add_str_value("foo");
  proto_pb.add_str_value("bar");
  const Value proto_value = values::Proto(proto_type, proto_pb);

  const Value json_value = values::Json(
      JSONValue::ParseJSONString(
          R"({"foo":[1, null, "bar"], "foo2": "hello", "foo3": true})")
          .value());

  EXPECT_EQ("(abc, 123)", TestFormat("%t", {struct_value}));
  EXPECT_EQ("[1, 2, 3]", TestFormat("%t", {array_value}));
  EXPECT_EQ("(\"abc\", 123)", TestFormat("%T", {struct_value}));
  EXPECT_EQ("[1, 2, 3]", TestFormat("%T", {array_value}));
  EXPECT_EQ(R"(int32_val1: 123 str_value: "foo" str_value: "bar")",
            TestFormat("%t", {proto_value}));
  EXPECT_EQ(R"('int32_val1: 123 str_value: "foo" str_value: "bar"')",
            TestFormat("%T", {proto_value}));
  EXPECT_EQ(R"(int32_val1: 123 str_value: "foo" str_value: "bar")",
            TestFormat("%p", {proto_value}));
  EXPECT_EQ(
R"(int32_val1: 123
str_value: "foo"
str_value: "bar"
)",
            TestFormat("%P", {proto_value}));

  EXPECT_EQ("{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}",
            TestFormat("%t", {json_value}));
  EXPECT_EQ(
      "JSON '{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}'",
      TestFormat("%T", {json_value}));
  EXPECT_EQ("{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}",
            TestFormat("%p", {json_value}));
  EXPECT_EQ(
      R"({
  "foo": [
    1,
    null,
    "bar"
  ],
  "foo2": "hello",
  "foo3": true
})",
      TestFormat("%P", {json_value}));

  const Value bad_json_value = Value::UnvalidatedJsonString(R"({"a": 12)");

  EXPECT_THAT(TestFormat("%t", {bad_json_value}),
              HasSubstr("syntax error while parsing object"));
  EXPECT_THAT(TestFormat("%T", {bad_json_value}),
              HasSubstr("syntax error while parsing object"));
  EXPECT_THAT(TestFormat("%p", {bad_json_value}),
              HasSubstr("syntax error while parsing object"));
  EXPECT_THAT(TestFormat("%P", {bad_json_value}),
              HasSubstr("syntax error while parsing object"));

  const StructType* struct_of_arrays_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"a1", array_type}, {"", array_type}},
                                        &struct_of_arrays_type));
  Value struct_of_arrays_value =
      values::Struct(struct_of_arrays_type, {array_value, array_value});
  EXPECT_EQ("([1, 2, 3], [1, 2, 3])",
            TestFormat("%t", {struct_of_arrays_value}));
  EXPECT_EQ("([1, 2, 3], [1, 2, 3])",
            TestFormat("%T", {struct_of_arrays_value}));

  const StructType* struct_of_proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"p1", proto_type}, {"", proto_type}},
                                        &struct_of_proto_type));
  Value struct_of_proto_value =
      values::Struct(struct_of_proto_type, {proto_value, proto_value});
  EXPECT_EQ(
      "(int32_val1: 123 str_value: \"foo\" str_value: \"bar\", int32_val1: 123 "
      "str_value: \"foo\" str_value: \"bar\")",
      TestFormat("%t", {struct_of_proto_value}));
  EXPECT_EQ(
      "('int32_val1: 123 str_value: \"foo\" str_value: \"bar\"', 'int32_val1: "
      "123 str_value: \"foo\" str_value: \"bar\"')",
      TestFormat("%T", {struct_of_proto_value}));

  const StructType* struct_of_json_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"p1", types::JsonType()}, {"", types::JsonType()}},
      &struct_of_json_type));
  Value struct_of_json_value =
      values::Struct(struct_of_json_type, {json_value, json_value});

  EXPECT_EQ(
      "({\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}, "
      "{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true})",
      TestFormat("%t", {struct_of_json_value}));
  EXPECT_EQ(
      "(JSON '{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}', "
      "JSON '{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}')",
      TestFormat("%T", {struct_of_json_value}));

  const StructType* struct_of_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"s1", struct_type}, {"", struct_type}}, &struct_of_struct_type));
  Value struct_of_struct_value =
      values::Struct(struct_of_struct_type, {struct_value, struct_value});
  EXPECT_EQ("((abc, 123), (abc, 123))",
            TestFormat("%t", {struct_of_struct_value}));
  EXPECT_EQ("((\"abc\", 123), (\"abc\", 123))",
            TestFormat("%T", {struct_of_struct_value}));

  const StructType* struct_of_enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"e1", enum_type}, {"", enum_type}},
                                        &struct_of_enum_type));
  Value struct_of_enum_value =
      values::Struct(struct_of_enum_type, {enum_value, enum_value});
  EXPECT_EQ("(TESTENUM2, TESTENUM2)", TestFormat("%t", {struct_of_enum_value}));
  EXPECT_EQ("(\"TESTENUM2\", \"TESTENUM2\")",
            TestFormat("%T", {struct_of_enum_value}));

  const ArrayType* array_of_proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(proto_type, &array_of_proto_type));
  Value array_of_proto_value =
      values::Array(array_of_proto_type, {proto_value, proto_value});
  EXPECT_EQ(
      "[int32_val1: 123 str_value: \"foo\" str_value: \"bar\", int32_val1: 123 "
      "str_value: \"foo\" str_value: \"bar\"]",
      TestFormat("%t", {array_of_proto_value}));
  EXPECT_EQ(
      "['int32_val1: 123 str_value: \"foo\" str_value: \"bar\"', 'int32_val1: "
      "123 str_value: \"foo\" str_value: \"bar\"']",
      TestFormat("%T", {array_of_proto_value}));

  Value array_of_json_value =
      values::Array(types::JsonArrayType(), {json_value, json_value});
  EXPECT_EQ(
      "[{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}, "
      "{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}]",
      TestFormat("%t", {array_of_json_value}));
  EXPECT_EQ(
      "[JSON '{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}', "
      "JSON '{\"foo\":[1,null,\"bar\"],\"foo2\":\"hello\",\"foo3\":true}']",
      TestFormat("%T", {array_of_json_value}));

  const ArrayType* array_of_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(struct_type, &array_of_struct_type));
  Value array_of_struct_value =
      values::Array(array_of_struct_type, {struct_value, struct_value});
  EXPECT_EQ("[(abc, 123), (abc, 123)]",
            TestFormat("%t", {array_of_struct_value}));
  EXPECT_EQ("[(\"abc\", 123), (\"abc\", 123)]",
            TestFormat("%T", {array_of_struct_value}));

  const ArrayType* array_of_enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(enum_type, &array_of_enum_type));
  Value array_of_enum_value =
      values::Array(array_of_enum_type, {enum_value, enum_value});
  EXPECT_EQ("[TESTENUM2, TESTENUM2]", TestFormat("%t", {array_of_enum_value}));
  EXPECT_EQ("[\"TESTENUM2\", \"TESTENUM2\"]",
            TestFormat("%T", {array_of_enum_value}));

  // Size errors.  Default limit in the flag is 1MB = 1048576.
  const std::string kTooWide =
      "ERROR: Width 1048577 exceeds the maximum supported output width of "
      "FORMAT. Limit: 1048576";
  const std::string kLength =
      "ERROR: Output string too long while evaluating FORMAT; limit 1048576";
  // Single arg width specifier too long.
  EXPECT_EQ(1048576, TestFormat("%1048576s", {values::String("")}).size());
  EXPECT_EQ(kTooWide, TestFormat("%1048577s", {values::String("")}));
  // Pattern too long with no args.
  EXPECT_EQ(1048576, TestFormat(std::string(1048576, 'x'), {}).size());
  EXPECT_EQ(kLength, TestFormat(std::string(1048577, 'x'), {}));
  // Single arg width from %* too long.
  EXPECT_EQ(1048576, TestFormat("%*s", {values::Int32(1048576),
                                        values::String("")}).size());
  EXPECT_EQ(kLength, TestFormat("%*s", {values::Int32(1048577),
                                        values::String("")}));
  // Arg too long after some previous characters in the pattern.
  EXPECT_EQ(1048576, TestFormat("xx%*s", {values::Int32(1048574),
                                          values::String("")}).size());
  EXPECT_EQ(kLength, TestFormat("xx%*s", {values::Int32(1048575),
                                          values::String("")}));
  // Arg too long after some previous format elements in the pattern.
  EXPECT_EQ(1048576, TestFormat("%d%*s", {values::Int32(10),
                                          values::Int32(1048574),
                                          values::String("")}).size());
  EXPECT_EQ(kLength, TestFormat("%d%*s", {values::Int32(10),
                                          values::Int32(1048575),
                                          values::String("")}));
  // Arg too long in the short case near the end of the string (for coverage).
  EXPECT_EQ(1048576, TestFormat("%*s%d", {values::Int32(1048574),
                                          values::String(""),
                                          values::Int32(10)}).size());
  EXPECT_EQ(kLength, TestFormat("%*s%d", {values::Int32(1048575),
                                          values::String(""),
                                          values::Int32(10)}));
  EXPECT_EQ(kLength, TestFormat("%'d%*s", {values::Int32(1234),
                                           values::Int32(1048575),
                                           values::String("")}));

  // Because we use printf, we'll lose string value contents after \0 chars.
  const std::string kStringWithZeros = std::string("abc\0def", 7);
  EXPECT_EQ("abcxabcy", TestFormat("%sx%sy",
                                   {values::String(kStringWithZeros),
                                    values::String(kStringWithZeros)}));
  // Patterns will not get truncated after \0 because we process each part
  // individually.
  const std::string kPatternWithZeros = std::string("abc\0def%d", 9);
  const std::string kOutputWithZeros = std::string("abc\0def10", 9);
  EXPECT_EQ(kOutputWithZeros, TestFormat(kPatternWithZeros,
                                         {values::Int32(10)}));

  // UTF-8
  std::string long_string_1 =
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好好你好你好你好你好你好你好你好你好你好你好你好"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好全部结束";
  EXPECT_EQ("\"" + long_string_1 + "\"",
            TestFormat("%T", {values::String(long_string_1)}));

  std::string long_string_2 =
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好全部结束";
  EXPECT_EQ("\"" + long_string_2 + "\"",
            TestFormat("%T", {values::String(long_string_2)}));

  std::string long_string_3 =
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你好你"
      "好你好你好你好你好你好你好全部结束";
  EXPECT_EQ("\"" + long_string_3 + "\"",
            TestFormat("%T", {values::String(long_string_3)}));

  TestFormatF TestFormatLegacy = absl::bind_front(
      TestFormatFunction, GetParam().FormatFunction, PRODUCT_INTERNAL,
      /*canonicalize_zero=*/false, /*use_external_float32=*/false);
  EXPECT_EQ("-0.000000", TestFormatLegacy("%+f", {Double(-0.0)}));
  EXPECT_EQ("-0.000000", TestFormatLegacy("%+f", {Float(-0.0)}));
  EXPECT_EQ("-0.000000", TestFormatLegacy("%f", {Double(-0.0)}));
  EXPECT_EQ("-0.000000", TestFormatLegacy("%f", {Float(-0.0)}));

  EXPECT_EQ("-0.000000e+00", TestFormatLegacy("%+e", {Double(-0.0)}));
  EXPECT_EQ("-0.000000e+00", TestFormatLegacy("%+e", {Float(-0.0)}));
  EXPECT_EQ("-0.000000e+00", TestFormatLegacy("%e", {Double(-0.0)}));
  EXPECT_EQ("-0.000000e+00", TestFormatLegacy("%e", {Float(-0.0)}));

  EXPECT_EQ("-0", TestFormatLegacy("%+g", {Double(-0.0)}));
  EXPECT_EQ("-0", TestFormatLegacy("%+g", {Float(-0.0)}));
  EXPECT_EQ("-0", TestFormatLegacy("%g", {Double(-0.0)}));
  EXPECT_EQ("-0", TestFormatLegacy("%g", {Float(-0.0)}));
}

TEST_P(FormatFunctionTests, FloatingPointInInternalModeWithFloat32) {
  SCOPED_TRACE(GetParam().ScopeLabel());
  TestFormatF TestFormat = GetParam().WrapTestFormat(
      PRODUCT_INTERNAL, /*use_external_float32=*/true);

  // Even when the product mode is PRODUCT_INTERNAL, if `use_external_float32`
  // is true, it will return FLOAT32.
  // Reason for this behavior: Format functions are currently calling
  // GetSQLLiteral with PRODUCT_EXTERNAL, which we cannot change without
  // breaking backward compatibility.
  EXPECT_EQ(
      "CAST(\"nan\" AS FLOAT32)",
      TestFormat("%T",
                 {values::Float(std::numeric_limits<float>::quiet_NaN())}));

  const float kNegativeFloatNan = -std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)",
            TestFormat("%T", {values::Float(kNegativeFloatNan)}));
}

TEST_P(FormatFunctionTests, FloatingPointInExternalModeWithoutFloat32) {
  using values::Double;
  using values::DoubleArray;
  using values::Float;
  using values::FloatArray;
  using values::Int64;

  SCOPED_TRACE(GetParam().ScopeLabel());
  TestFormatF TestFormat = GetParam().WrapTestFormat(
      PRODUCT_EXTERNAL, /*use_external_float32=*/false);

  EXPECT_EQ(
      "ERROR: Invalid type for argument 2 to FORMAT; "
      "Expected FLOAT64; Got INT64",
      TestFormat("%f", {Int64(1)}));

  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)",
            TestFormat("%T", {Float(std::numeric_limits<float>::quiet_NaN())}));
  EXPECT_EQ(
      "[4.0, -2.5, CAST(\"nan\" AS FLOAT32)]",
      TestFormat(
          "%T",
          {FloatArray({4, -2.5, std::numeric_limits<float>::quiet_NaN()})}));
  EXPECT_EQ("nan",
            TestFormat("%t", {Float(std::numeric_limits<float>::quiet_NaN())}));

  const float kNegativeFloatNan = -std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)",
            TestFormat("%T", {Float(kNegativeFloatNan)}));
  EXPECT_EQ(
      "[-4.0, CAST(\"-inf\" AS FLOAT64)]",
      TestFormat(
          "%T", {DoubleArray({-4, -std::numeric_limits<double>::infinity()})}));
  EXPECT_EQ("nan", TestFormat("%t", {Float(kNegativeFloatNan)}));

  EXPECT_EQ(
      "CAST(\"nan\" AS FLOAT64)",
      TestFormat("%T", {Double(std::numeric_limits<double>::quiet_NaN())}));
  EXPECT_EQ(
      "nan",
      TestFormat("%t", {Double(std::numeric_limits<double>::quiet_NaN())}));

  const double kNegativeDoubleNan = -std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ("CAST(\"nan\" AS FLOAT64)",
            TestFormat("%T", {Double(kNegativeDoubleNan)}));
  EXPECT_EQ("nan", TestFormat("%t", {Double(kNegativeDoubleNan)}));
}

TEST_P(FormatFunctionTests, FloatingPointInExternalModeWithFloat32) {
  using values::Double;
  using values::Float;
  using values::Int64;

  SCOPED_TRACE(GetParam().ScopeLabel());
  TestFormatF TestFormat = GetParam().WrapTestFormat(
      PRODUCT_EXTERNAL, /*use_external_float32=*/true);

  EXPECT_EQ(
      "ERROR: Invalid type for argument 2 to FORMAT; "
      "Expected FLOAT64; Got INT64",
      TestFormat("%f", {Int64(1)}));

  // Tests to check that positive and negative NaNs are formatted as "nan".
  EXPECT_EQ(
      "CAST(\"nan\" AS FLOAT32)",
      TestFormat("%T",
                 {values::Float(std::numeric_limits<float>::quiet_NaN())}));
  EXPECT_EQ(
      "nan",
      TestFormat("%t",
                 {values::Float(std::numeric_limits<float>::quiet_NaN())}));

  const float kNegativeFloatNan = -std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)",
            TestFormat("%T", {values::Float(kNegativeFloatNan)}));
  EXPECT_EQ("nan", TestFormat("%t", {values::Float(kNegativeFloatNan)}));

  EXPECT_EQ(
      "CAST(\"nan\" AS FLOAT64)",
      TestFormat("%T",
                 {values::Double(std::numeric_limits<double>::quiet_NaN())}));
  EXPECT_EQ(
      "nan",
      TestFormat("%t",
                 {values::Double(std::numeric_limits<double>::quiet_NaN())}));

  const double kNegativeDoubleNan = -std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ("CAST(\"nan\" AS FLOAT64)",
            TestFormat("%T", {values::Double(kNegativeDoubleNan)}));
  EXPECT_EQ("nan", TestFormat("%t", {values::Double(kNegativeDoubleNan)}));
}

static absl::Status TestCheckFormat(absl::string_view format_string,
                                    const std::vector<const Type*>& types) {
  const absl::Status status = CheckStringFormatUtf8ArgumentTypes(
      format_string, types, ProductMode::PRODUCT_INTERNAL);
  EXPECT_THAT(status.code(),
              testing::AnyOf(testing::Eq(absl::StatusCode::kInvalidArgument),
                             testing::Eq(absl::StatusCode::kOutOfRange),
                             testing::Eq(absl::StatusCode::kOk)));

  return status;
}

// We don't test exhaustively, the code behind the scenes largely matches
// with 'FORMAT'.
TEST(FormatTest, TestCheckFormatArgumentTypes) {
  GOOGLESQL_EXPECT_OK(TestCheckFormat("", {}));
  GOOGLESQL_EXPECT_OK(TestCheckFormat("abc", {}));
  GOOGLESQL_EXPECT_OK(TestCheckFormat("abc", {}));
  GOOGLESQL_EXPECT_OK(TestCheckFormat("dd%dd", {types::Int64Type()}));
  GOOGLESQL_EXPECT_OK(TestCheckFormat("dd%sd", {types::StringType()}));
  GOOGLESQL_EXPECT_OK(TestCheckFormat(
      "%*.*s", {types::Int32Type(), types::Int64Type(), types::StringType()}));
  GOOGLESQL_EXPECT_OK(TestCheckFormat(
      "%d%d%d", {types::Int32Type(), types::Uint64Type(), types::Int64Type()}));

  EXPECT_THAT(TestCheckFormat("%s", {}),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(TestCheckFormat("%s%d", {types::StringType()}),
              StatusIs(absl::StatusCode::kOutOfRange));
}

static Value NumericFromString(absl::string_view str) {
  return values::Numeric(NumericValue::FromStringStrict(str).value());
}

static Value BigNumericFromString(absl::string_view str) {
  return values::BigNumeric(BigNumericValue::FromStringStrict(str).value());
}

// Normal cases are tested in TEST_P(FormatComplianceTests, Test).
TEST_P(FormatFunctionTests, NumericFormat_Errors) {
  SCOPED_TRACE(GetParam().ScopeLabel());
  TestFormatF TestFormat = GetParam().WrapTestFormat(
      PRODUCT_INTERNAL, /*use_external_float32=*/false);

  using values::Int32;
  using values::Numeric;
  using values::NullNumeric;

  // Ensure that %a and %A (hex format) trigger an error.
  EXPECT_THAT(TestFormat("%a", {NumericFromString("1.5")}),
              HasSubstr("Invalid format specifier character"));
  EXPECT_THAT(TestFormat("%A", {NumericFromString("1.5")}),
              HasSubstr("Invalid format specifier character"));

  // Exceeds maximum output length.
  EXPECT_THAT(TestFormat("%*f", {Int32(std::numeric_limits<int32_t>::max()),
                                 Numeric(NumericValue::MaxValue())}),
              HasSubstr("Output string too long while evaluating FORMAT"));

  auto flag_resetter = absl::MakeCleanup(absl::bind_front(
      absl::SetFlag<int32_t>, &FLAGS_googlesql_format_max_output_width,
      absl::GetFlag(FLAGS_googlesql_format_max_output_width)));
  // No minimum output size, but string ends up being too long.
  absl::SetFlag(&FLAGS_googlesql_format_max_output_width, 20);
  EXPECT_THAT(TestFormat("%.9f", {Numeric(NumericValue::MaxValue())}),
              HasSubstr("Output string too long while evaluating FORMAT"));
  absl::SetFlag(&FLAGS_googlesql_format_max_output_width, 12);
  EXPECT_THAT(TestFormat("%.9e", {NumericFromString("3.141592653")}),
              HasSubstr("Output string too long while evaluating FORMAT"));
}

TEST_P(FormatFunctionTests, BigNumericFormat_Errors) {
  SCOPED_TRACE(GetParam().ScopeLabel());

  TestFormatF TestFormat = GetParam().WrapTestFormat(
      PRODUCT_INTERNAL, /*use_external_float32=*/false);

  using values::BigNumeric;
  using values::Int32;
  using values::NullBigNumeric;

  // Ensure that %a and %A (hex format) trigger an error.
  EXPECT_THAT(TestFormat("%a", {BigNumericFromString("1.5")}),
              HasSubstr("Invalid format specifier character"));
  EXPECT_THAT(TestFormat("%A", {BigNumericFromString("1.5")}),
              HasSubstr("Invalid format specifier character"));

  // Exceeds maximum output length.
  EXPECT_THAT(TestFormat("%*f", {Int32(std::numeric_limits<int32_t>::max()),
                                 BigNumeric(BigNumericValue::MaxValue())}),
              HasSubstr("Output string too long while evaluating FORMAT"));

  auto flag_resetter = absl::MakeCleanup(absl::bind_front(
      absl::SetFlag<int32_t>, &FLAGS_googlesql_format_max_output_width,
      absl::GetFlag(FLAGS_googlesql_format_max_output_width)));
  // No minimum output size, but string ends up being too long.
  absl::SetFlag(&FLAGS_googlesql_format_max_output_width, 20);
  EXPECT_THAT(TestFormat("%.9f", {BigNumeric(BigNumericValue::MaxValue())}),
              HasSubstr("Output string too long while evaluating FORMAT"));
  absl::SetFlag(&FLAGS_googlesql_format_max_output_width, 12);
  EXPECT_THAT(TestFormat("%.9e", {BigNumericFromString("3.141592653")}),
              HasSubstr("Output string too long while evaluating FORMAT"));
}

INSTANTIATE_TEST_SUITE_P(FormatTests, FormatFunctionTests,
                         testing::ValuesIn(GetFormatFunctionParams()));

std::string RemoveLastDigit(std::string str,
                            bool remove_trailing_zeros_after_decimal_point) {
  size_t pos = str.size() - 1;
  if (str.size() >= 4 &&
      (str[str.size() - 4] == 'e' || str[str.size() - 4] == 'E')) {
    pos = str.size() - 5;
  }
  pos -= (str[pos] == '.');
  size_t num_digits_to_remove = 1;
  if (remove_trailing_zeros_after_decimal_point) {
    auto decimal_point_pos = str.find('.');
    if (decimal_point_pos != std::string::npos) {
      while (pos >= decimal_point_pos &&
             (str[pos - 1] == '0' || str[pos - 1] == '.')) {
        ++num_digits_to_remove;
        --pos;
      }
    }
  }
  str.erase(pos, num_digits_to_remove);
  return str;
}

void CompareFormattedStrings(const std::string& formatted_double,
                             const std::string& formatted_numeric,
                             bool remove_trailing_zeros_after_decimal_point) {
  bool match = (formatted_double == formatted_numeric);
  if (!match) {
    // When the first truncated digit is 5, formatted_double is very often
    // rounded down because of computational error. Example:
    // value = 1.25, formatted_double = "1.2", formatted_numeric = "1.3"
    // Therefore we have to ignore the last digit.
    // If remove_trailing_zeros_after_decimal_point is true, we also need to
    // remove trailing zeros and pad with spaces. Example:
    // value = 1.2005, formatted_double = "  1.2", formatted_numeric = "1.201"
    if (remove_trailing_zeros_after_decimal_point) {
      std::string trimmed = RemoveLastDigit(formatted_numeric, true);
      if (trimmed.size() < formatted_double.size()) {
        trimmed.insert(0, formatted_double.size() - trimmed.size(), ' ');
      }
      match = (formatted_double == trimmed);
    }
    if (!match && formatted_double.size() == formatted_numeric.size()) {
      match = (RemoveLastDigit(formatted_double, false) ==
               RemoveLastDigit(formatted_numeric, false));
    }
  }
  EXPECT_TRUE(match) << "double=\"" << formatted_double << "\" vs "
                     << "numeric=\"" << formatted_numeric << "\" "
                     << "remove_trailing_zeros= "
                     << remove_trailing_zeros_after_decimal_point;
}

template <typename T>  // T must be NumericValue or BigNumericValue.
void TestFormatNumericWithRandomData(FormatF FormatFunction) {
  auto TestFormat =
      absl::bind_front(TestFormatFunction, FormatFunction, PRODUCT_INTERNAL,
                       /*canonicalize_zero=*/true,
                       /*use_external_float32=*/false);
  using values::Int32;
  absl::BitGen random;
  for (int i = 0; i < 20000; ++i) {
    // Generate a random double value that can be losslessly converted to T.
    int64_t mantissa =
        absl::Uniform<int64_t>(random, 1 - (1LL << 53), (1LL << 53));
    constexpr double kLog2_10 = 3.321928095;
    constexpr int kMaxIntegerBits =
        static_cast<int>(T::kMaxIntegerDigits * kLog2_10);
    // The lower bound of the bits is not mutiplied by kLog2_10, to ensure that
    // the result has at most T::kMaxFractionalDigits fractional digits in
    // decimal format.
    int exponent_bits = absl::Uniform<int>(random, -T::kMaxFractionalDigits,
                                           kMaxIntegerBits - 53);
    double double_value = std::ldexp(mantissa, exponent_bits);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(T numeric_value, T::FromDouble(double_value));
    ASSERT_EQ(double_value, numeric_value.ToDouble())
        << numeric_value.ToString();
    Value value = ValueConstructor(numeric_value).get();

    std::vector<Value> single_numeric_value{value};
    CompareFormattedStrings(absl::StrFormat("%f", double_value),
                            TestFormat("%f", single_numeric_value), false);
    CompareFormattedStrings(absl::StrFormat("%#f", double_value),
                            TestFormat("%#f", single_numeric_value), false);
    CompareFormattedStrings(absl::StrFormat("%e", double_value),
                            TestFormat("%e", single_numeric_value), false);
    CompareFormattedStrings(absl::StrFormat("%#e", double_value),
                            TestFormat("%#e", single_numeric_value), false);
    CompareFormattedStrings(absl::StrFormat("%g", double_value),
                            TestFormat("%g", single_numeric_value), true);
    CompareFormattedStrings(absl::StrFormat("%#g", double_value),
                            TestFormat("%#g", single_numeric_value), false);

    constexpr uint kMaxDigits = T::kMaxIntegerDigits + T::kMaxFractionalDigits;
    uint32_t size1 = absl::Uniform<uint32_t>(random, 0, kMaxDigits + 10);
    uint32_t size2 = absl::Uniform<uint32_t>(random, 0, kMaxDigits + 3);
    std::vector<Value> value_with_sizes{Int32(size1), Int32(size2), value};
    CompareFormattedStrings(
        absl::StrFormat("%*.*f", size1, size2, double_value),
        TestFormat("%*.*f", value_with_sizes), false);
    CompareFormattedStrings(
        absl::StrFormat("%#*.*f", size1, size2, double_value),
        TestFormat("%#*.*f", value_with_sizes), false);
    CompareFormattedStrings(
        absl::StrFormat("%*.*e", size1, size2, double_value),
        TestFormat("%*.*e", value_with_sizes), false);
    CompareFormattedStrings(
        absl::StrFormat("%#*.*e", size1, size2, double_value),
        TestFormat("%#*.*e", value_with_sizes), false);
    CompareFormattedStrings(
        absl::StrFormat("%*.*g", size1, size2, double_value),
        TestFormat("%*.*g", value_with_sizes), true);
    CompareFormattedStrings(
        absl::StrFormat("%#*.*g", size1, size2, double_value),
        TestFormat("%#*.*g", value_with_sizes), false);
  }
}

TEST(FormatTest, NumericFormat_Random_StringFormat) {
  TestFormatNumericWithRandomData<NumericValue>(StringFormatUtf8);
}

TEST(FormatTest, BigNumericFormat_Random_StringFormat) {
  TestFormatNumericWithRandomData<BigNumericValue>(StringFormatUtf8);
}

class FormatComplianceTests
    : public ::testing::TestWithParam<
          std::tuple<FunctionTestCall, FormatFunctionParam>> {};

// There are several more tests in ../compliance/functions_testlib.cc.
// Run those here.
TEST_P(FormatComplianceTests, Test) {
  FunctionTestCall test = std::get<0>(GetParam());
  FormatFunctionParam named_format_function = std::get<1>(GetParam());

  // Try to make the scope really pop while debugging.
  SCOPED_TRACE(named_format_function.ScopeLabel());
  FormatF FormatFunction = named_format_function.FormatFunction;

  ABSL_DCHECK_GE(test.params.num_params(), 1);

  ABSL_DCHECK(test.params.param(0).type()->IsString());
  if (test.params.param(0).is_null()) {
    // This is handled outside the library.
    return;
  }
  const std::string pattern = test.params.param(0).string_value();
  ABSL_LOG(INFO) << "pattern: " << pattern;
  std::vector<Value> args;
  bool using_any_civil_time_values = false;
  for (int i = 1; i < test.params.num_params(); ++i) {
    const Value& arg = test.params.param(i);
    ABSL_LOG(INFO) << "arg " << (i - 1) << ": "
              << arg.FullDebugString();
    if (arg.type()->UsingFeatureV12CivilTimeType()) {
      using_any_civil_time_values = true;
    }
    args.push_back(arg);
  }

  std::string actual;
  bool is_null = false;
  const absl::Status status = FormatFunction(
      pattern, args, ProductMode::PRODUCT_INTERNAL, &actual, &is_null,
      /*canonicalize_zero=*/true, /*use_external_float32=*/false);

  if (using_any_civil_time_values &&
      !googlesql_base::ContainsKey(test.params.required_features(), FEATURE_CIVIL_TIME)) {
    // The test case in compliance test is expecting an error in this case.
    EXPECT_FALSE(test.params.status().ok());
    return;
  }

  if (test.params.status().ok()) {
    EXPECT_TRUE(test.params.result().type()->IsString());
    GOOGLESQL_EXPECT_OK(status);
    EXPECT_EQ(test.params.result().is_null(), is_null);
    if (!test.params.result().is_null() && !is_null) {
      EXPECT_EQ(test.params.result().string_value(), actual);
    }
  } else {
    EXPECT_FALSE(status.ok()) << "Should have failed";
  }
}

INSTANTIATE_TEST_SUITE_P(
    ComplianceTests, FormatComplianceTests,
    ::testing::Combine(::testing::ValuesIn(GetFunctionTestsFormat()),
                       ::testing::ValuesIn(GetFormatFunctionParams())));

}  // namespace functions
}  // namespace googlesql
