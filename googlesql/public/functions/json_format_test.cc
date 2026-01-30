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

// Tests for JSON formatting functions.

#include "googlesql/public/functions/json_format.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "googlesql/base/map_util.h"

namespace googlesql {
namespace functions {
namespace {

using ::absl_testing::StatusIs;

TEST(JsonFormatTest, Compliance) {
  const std::vector<FunctionTestCall> tests = GetFunctionTestsToJsonString();

  for (const FunctionTestCall& test : tests) {
    if (std::any_of(test.params.params().begin(), test.params.params().end(),
                    [](const Value& param) { return param.is_null(); })) {
      continue;
    }
    const Value input_value = test.params.param(0);
    const bool pretty_print = test.params.params().size() == 2
                                  ? test.params.param(1).bool_value()
                                  : false;
    SCOPED_TRACE(absl::Substitute("$0('$1', '$2')", test.function_name,
                                  input_value.ShortDebugString(),
                                  pretty_print));

    JsonPrettyPrinter pretty_printer(pretty_print, PRODUCT_INTERNAL);
    JSONParsingOptions json_parsing_options =
        JSONParsingOptions{.canonicalize_zero = true};
    if (googlesql_base::ContainsKey(test.params.required_features(),
                         FEATURE_JSON_STRICT_NUMBER_PARSING)) {
      json_parsing_options.wide_number_mode =
          JSONParsingOptions::WideNumberMode::kExact;
    }
    std::string actual_output;
    auto actual_status = JsonFromValue(input_value, &pretty_printer,
                                       &actual_output, json_parsing_options);

    if (test.params.status().ok()) {
      GOOGLESQL_ASSERT_OK(actual_status);
      EXPECT_EQ(test.params.result().string_value(), actual_output);

      actual_output = "dummy.prefix";
      GOOGLESQL_ASSERT_OK(JsonFromValue(input_value, &pretty_printer, &actual_output,
                              json_parsing_options));
      EXPECT_EQ("dummy.prefix" + test.params.result().string_value(),
                actual_output);
    } else {
      EXPECT_EQ(test.params.status().code(), actual_status.code());
    }
  }
}

TEST(JsonFormatTest, LargeOutput) {
  // Output larger than pretty_printer.max_json_output_size_bytes() is
  // disallowed by JsonFromValue. The output size includes quotes around
  // strings.
  JsonPrettyPrinter pretty_printer(/*pretty_print=*/false, PRODUCT_INTERNAL);
  EXPECT_EQ(kDefaultMaxJsonStringSizeBytes,
            pretty_printer.max_json_output_size_bytes());

  const int64_t max_json_output_size = 10;
  pretty_printer.set_max_json_output_size_bytes(max_json_output_size);
  EXPECT_EQ(max_json_output_size, pretty_printer.max_json_output_size_bytes());

  {
    std::string tmp;
    GOOGLESQL_EXPECT_OK(JsonFromValue(
        values::String(std::string(max_json_output_size - 2, '.')),
        &pretty_printer, &tmp));
  }

  {
    std::string tmp;
    EXPECT_THAT(JsonFromValue(
                    values::String(std::string(max_json_output_size - 1, '.')),
                    &pretty_printer, &tmp),
                StatusIs(absl::StatusCode::kOutOfRange));
  }

  {
    std::string tmp;
    EXPECT_THAT(
        JsonFromValue(
            values::Array(
                types::StringArrayType(),
                {values::String(std::string(max_json_output_size / 2, '.')),
                 values::String(std::string(max_json_output_size / 2, '.'))}),
            &pretty_printer, &tmp),
        StatusIs(absl::StatusCode::kOutOfRange));
  }

  {
    std::string tmp = "....";
    EXPECT_THAT(JsonFromValue(values::String("foobar"), &pretty_printer, &tmp),
                StatusIs(absl::StatusCode::kOutOfRange));

    pretty_printer.set_max_json_output_size_bytes(max_json_output_size * 2);
    tmp = "....";
    GOOGLESQL_EXPECT_OK(JsonFromValue(values::String("foobar"), &pretty_printer, &tmp));
  }
}

void CanonicalizeZeroLegacyTest(Value input_value,
                                const std::string& expected_result_value) {
  JsonPrettyPrinter pretty_printer(/*pretty_print=*/false, PRODUCT_INTERNAL);
  JSONParsingOptions json_parsing_options =
      JSONParsingOptions{.canonicalize_zero = false};
  std::string actual_output;
  auto actual_status = JsonFromValue(input_value, &pretty_printer,
                                     &actual_output, json_parsing_options);

  GOOGLESQL_ASSERT_OK(actual_status);
  actual_output = "dummy.prefix";
  GOOGLESQL_ASSERT_OK(JsonFromValue(input_value, &pretty_printer, &actual_output,
                          json_parsing_options));
  EXPECT_EQ("dummy.prefix" + expected_result_value, actual_output);
}

TEST(JsonFormatTest, CanonicalizeZeroLegacyFloat0Test) {
  CanonicalizeZeroLegacyTest(values::Float(-0.0), "-0");
}

TEST(JsonFormatTest, CanonicalizeZeroLegacyFloatNonZeroTest) {
  CanonicalizeZeroLegacyTest(values::Float(5), "5");
}

TEST(JsonFormatTest, CanonicalizeZeroLegacyDouble0Test) {
  CanonicalizeZeroLegacyTest(values::Double(-0.0), "-0");
}

TEST(JsonFormatTest, CanonicalizeZeroLegacyDoubleNonZeroTest) {
  CanonicalizeZeroLegacyTest(values::Double(5), "5");
}

struct PicosTestCase {
  TimestampPicosValue input;
  std::string expected_output;
};

using PicosTest = ::testing::TestWithParam<PicosTestCase>;

TEST_P(PicosTest, JsonFromTimestamp) {
  const PicosTestCase& test_case = GetParam();
  std::string actual_output;
  auto status = JsonFromTimestamp(test_case.input, &actual_output,
                                  /*quote_output_string=*/false);
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(actual_output, test_case.expected_output);

  // Test the case when quote_output_string=true.
  actual_output.clear();
  status = JsonFromTimestamp(test_case.input, &actual_output,
                             /*quote_output_string=*/true);
  GOOGLESQL_EXPECT_OK(status);
  std::string expected_quoted_output =
      absl::StrCat("\"", test_case.expected_output, "\"");
  EXPECT_EQ(actual_output, expected_quoted_output);
}

TimestampPicosValue TimestampPicosFromStr(absl::string_view str) {
  // Test cases use consistently same timezone.
  PicoTime pico_time;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      str, absl::UTCTimeZone(), /* allow_tz_in_str= */ true, &pico_time));
  return TimestampPicosValue(pico_time);
}

INSTANTIATE_TEST_SUITE_P(
    PicosTestSuite, PicosTest,
    ::testing::ValuesIn<PicosTestCase>({
        {TimestampPicosFromStr("2017-06-25 12:34:56.123456"),
         "2017-06-25T12:34:56.123456Z"},
        {TimestampPicosFromStr("2017-06-25 05:13:00"), "2017-06-25T05:13:00Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.123456789123"),
         "2017-06-25T23:34:56.123456789123Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.12345678912"),
         "2017-06-25T23:34:56.123456789120Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.1234567891"),
         "2017-06-25T23:34:56.123456789100Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.123456789"),
         "2017-06-25T23:34:56.123456789Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.12345678"),
         "2017-06-25T23:34:56.123456780Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.1234567"),
         "2017-06-25T23:34:56.123456700Z"},
        {TimestampPicosFromStr("2017-06-25 23:34:56.123456"),
         "2017-06-25T23:34:56.123456Z"},
        {TimestampPicosFromStr("2017-06-25 12:34:56.12345"),
         "2017-06-25T12:34:56.123450Z"},
        {TimestampPicosFromStr("2017-06-25 12:34:56.123"),
         "2017-06-25T12:34:56.123Z"},
        {TimestampPicosFromStr("2017-06-25 12:34:56.12"),
         "2017-06-25T12:34:56.120Z"},
        {TimestampPicosFromStr("2017-06-25 12:34:56.1"),
         "2017-06-25T12:34:56.100Z"},
        {TimestampPicosFromStr("2017-06-25 12:34:00"), "2017-06-25T12:34:00Z"},
        {TimestampPicosFromStr("2017-06-25 12:00:00"), "2017-06-25T12:00:00Z"},
        {TimestampPicosFromStr("1918-11-11"), "1918-11-11T00:00:00Z"},
        {TimestampPicosFromStr("0001-01-01"), "0001-01-01T00:00:00Z"},
        {TimestampPicosFromStr("9999-12-31 23:59:59.999999999999"),
         "9999-12-31T23:59:59.999999999999Z"},
    }));

}  // namespace
}  // namespace functions
}  // namespace googlesql
