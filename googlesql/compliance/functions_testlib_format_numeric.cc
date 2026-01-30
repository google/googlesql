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
#include <vector>

#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/strings/string_view.h"

namespace googlesql {

static googlesql::Value NumericFromString(absl::string_view numeric_str) {
  return Value::Numeric(NumericValue::FromStringStrict(numeric_str).value());
}

static googlesql::Value BigNumericFromString(absl::string_view numeric_str) {
  return Value::BigNumeric(
      (BigNumericValue::FromStringStrict(numeric_str).value()));
}

std::vector<FunctionTestCall> GetFunctionTestsFormatNumeric() {
  std::vector<FunctionTestCall> test_cases;
  // Common test data for NUMERIC and BIGNUMERIC.
  struct NumericTestData {
    absl::string_view format_spec;
    absl::string_view numeric_value;
    absl::string_view expected_output;
  };
  constexpr absl::string_view kMinNumericValueStr =
      "-99999999999999999999999999999.999999999";
  constexpr absl::string_view kMaxNumericValueStr =
      "99999999999999999999999999999.999999999";
  static constexpr NumericTestData kNumericTestData[] = {
      {"%t", kMaxNumericValueStr, "99999999999999999999999999999.999999999"},
      {"%t", "3.14", "3.14"},
      {"%t", "-3.14", "-3.14"},
      {"%f", "0", "0.000000"},
      {"%.0f", "0", "0"},
      {"%.1f", "0", "0.0"},
      {"%f", "1.5", "1.500000"},
      {"%F", "1.5", "1.500000"},
      {"%3.7f", "1.5", "1.5000000"},
      {"%10f", "1.5", "  1.500000"},
      {"%10.3f", "1.5", "     1.500"},
      {"%10.0f", "1.5", "         2"},
      {"%10.0f", "-1.5", "        -2"},
      {"%10.6f", "1.5", "  1.500000"},
      {"%10.6F", "1.5", "  1.500000"},
      {"%.6f", "1.5", "1.500000"},
      {"%f", "1.999999999", "2.000000"},
      {"%10f", "1.999999999", "  2.000000"},
      {"%10.0f", "1.999999999", "         2"},
      {"%10.9f", "1.999999999", "1.999999999"},
      {"%10.7f", "1.999999999", " 2.0000000"},
      {"%10.7f", "-1.999999999", "-2.0000000"},
      {"%10.10f", "1.999999999", "1.9999999990"},
      {"%14.10f", "1.999999999", "  1.9999999990"},
      {"%14.10f", "-1.999999999", " -1.9999999990"},
      {"%.9f", kMaxNumericValueStr, "99999999999999999999999999999.999999999"},
      {"%.11f", kMaxNumericValueStr,
       "99999999999999999999999999999.99999999900"},
      {"%45.11f", kMaxNumericValueStr,
       "    99999999999999999999999999999.99999999900"},
      {"%40.11f", kMaxNumericValueStr,
       "99999999999999999999999999999.99999999900"},
      {"%010.3f", "1.5", "000001.500"},
      {"%+.3f", "1.5", "+1.500"},
      {"%+.3f", "-1.5", "-1.500"},
      {"% .3f", "1.5", " 1.500"},
      {"% .3f", "-1.5", "-1.500"},
      {"% 6.3f", "1.5", " 1.500"},
      {"% 7.3f", "1.5", "  1.500"},
      {"%-6.0f", "2", "2     "},
      {"%-9.4f", "2.53", "2.5300   "},
      {"%#.0f", "1", "1."},
      {"%0#+4.0f", "1", "+01."},
      {"%#+'.0f", "12345678", "+12,345,678."},
      {"%f", kMaxNumericValueStr, "100000000000000000000000000000.000000"},
      {"%.1f", kMaxNumericValueStr, "100000000000000000000000000000.0"},
      {"%.0f", kMaxNumericValueStr, "100000000000000000000000000000"},
      {"%f", kMinNumericValueStr, "-100000000000000000000000000000.000000"},
      {"%.1f", kMinNumericValueStr, "-100000000000000000000000000000.0"},
      {"%.0f", kMinNumericValueStr, "-100000000000000000000000000000"},
      {"%'.6f", kMinNumericValueStr,
       "-100,000,000,000,000,000,000,000,000,000.000000"},
      {"%45.6f", kMinNumericValueStr,
       "       -100000000000000000000000000000.000000"},
      {"%'f", "0.1", "0.100000"},
      {"%'.5f", "0.1", "0.10000"},
      {"%'010.5f", "0.1", "0000.10000"},
      {"%'010.5f", "0.00100025", "0000.00100"},
      {"%' 010.5f", "0.1", " 000.10000"},
      {"%' 010.5f", "-0.1", "-000.10000"},
      {"%'+010.5f", "0.1", "+000.10000"},

      {"%e", "0", "0.000000e+00"},
      {"%.0e", "0", "0e+00"},
      {"%.1e", "0", "0.0e+00"},
      {"%e", "1.5", "1.500000e+00"},
      {"%E", "1.5", "1.500000E+00"},
      {"%e", "1500", "1.500000e+03"},
      {"%E", "1500", "1.500000E+03"},
      {"%e", "0.00015", "1.500000e-04"},
      {"%14e", "0.00015", "  1.500000e-04"},
      {"%e", "0.000000001", "1.000000e-09"},
      {"%e", "0.000000009", "9.000000e-09"},
      {"%.37e", kMaxNumericValueStr,
       "9.9999999999999999999999999999999999999e+28"},
      {"%.37e", kMinNumericValueStr,
       "-9.9999999999999999999999999999999999999e+28"},
      {"%.40e", kMaxNumericValueStr,
       "9.9999999999999999999999999999999999999000e+28"},
      {"%.40e", kMinNumericValueStr,
       "-9.9999999999999999999999999999999999999000e+28"},
      {"%.0e", "0.00015", "2e-04"},
      {"%.3e", "0.00099995", "1.000e-03"},
      {"%.3e", "0.00059995", "6.000e-04"},
      {"%-12.3e", "0.00059995", "6.000e-04   "},
      {"%.2e", "0.00059995", "6.00e-04"},
      {"%.0e", "-0.00015555", "-2e-04"},
      {"%-10.0e", "-0.00015555", "-2e-04    "},
      {"%.0e", "0.00015555", "2e-04"},
      {"%.1e", "-0.00015555", "-1.6e-04"},
      {"%.1e", "0.00015555", "1.6e-04"},
      {"%.0e", "-0.00015", "-2e-04"},
      {"%.0e", "0.00015", "2e-04"},
      {"%.0e", "0.00015", "2e-04"},
      {"%.6e", "0.00015", "1.500000e-04"},
      {"%.0e", "0.00095555", "1e-03"},
      {"%.0e", "-0.00095555", "-1e-03"},
      {"%.3e", "0.00095555", "9.556e-04"},
      {"%.3e", "-0.00095555", "-9.556e-04"},
      {"%.3e", "-0.00095554", "-9.555e-04"},
      {"%e", kMaxNumericValueStr, "1.000000e+29"},
      {"%e", kMinNumericValueStr, "-1.000000e+29"},
      {"%.3e", "0.99999999", "1.000e+00"},
      {"%.3e", "-0.99999999", "-1.000e+00"},
      {"%010.3e", "1.5", "01.500e+00"},
      {"%+.3e", "1.5", "+1.500e+00"},
      {"%+.3e", "-1.5", "-1.500e+00"},
      {"% .3e", "1.5", " 1.500e+00"},
      {"% .3e", "-1.5", "-1.500e+00"},
      {"% 10.3e", "1.5", " 1.500e+00"},
      {"% 11.3e", "1.5", "  1.500e+00"},
      {"%#.0e", "1", "1.e+00"},
      {"%0#+9.0e", "1", "+001.e+00"},
      {"%#+'.10e", "12345678", "+1.2345678000e+07"},

      {"%.0g", "0", "0"},
      {"%.0G", "0", "0"},
      {"%g", "0", "0"},
      {"%3G", "0", "  0"},
      {"%3.2147483647G", "0", "  0"},
      {"%.0g", "1e-9", "1e-09"},
      {"%.0G", "-1e-9", "-1E-09"},
      {"%7.0g", "1e-9", "  1e-09"},
      {"%7.0G", "-1e-9", " -1E-09"},
      {"%g", "-1.5e-5", "-1.5e-05"},
      {"%G", "1.5e-5", "1.5E-05"},
      {"%g", "-0.000099999", "-9.9999e-05"},
      {"%G", "0.000099999", "9.9999E-05"},
      {"%.7g", "-0.000099999", "-9.9999e-05"},
      {"%.7G", "0.000099999", "9.9999E-05"},
      {"%.2147483647g", "-0.000099999", "-9.9999e-05"},
      {"%.2147483647G", "0.000099999", "9.9999E-05"},
      {"%g", "-1e-4", "-0.0001"},
      {"%G", "1e-4", "0.0001"},
      {"%.7g", "-1e-4", "-0.0001"},
      {"%.7G", "1e-4", "0.0001"},
      {"%.2147483647g", "-1e-4", "-0.0001"},
      {"%.2147483647G", "1e-4", "0.0001"},
      {"%g", "-1.5e-4", "-0.00015"},
      {"%G", "1.5e-4", "0.00015"},
      {"%8g", "-1.5e-4", "-0.00015"},
      {"%8G", "1.5e-4", " 0.00015"},
      {"%g", "-1", "-1"},
      {"%G", "1", "1"},
      {"%.2147483647g", "-1", "-1"},
      {"%.2147483647G", "1", "1"},
      {"%g", "1.5", "1.5"},
      {"%G", "-1.5", "-1.5"},
      {"%g", "1500", "1500"},
      {"%G", "-1500", "-1500"},
      {"%g", "999999", "999999"},
      {"%G", "-999999", "-999999"},
      {"%g", "1000000", "1e+06"},
      {"%G", "-1000000", "-1E+06"},
      {"%.7g", "1000000", "1000000"},
      {"%.7G", "-1000000", "-1000000"},
      {"%.2147483647g", "1000000", "1000000"},
      {"%.2147483647G", "-1000000", "-1000000"},
      {"%g", "1234560", "1.23456e+06"},
      {"%G", "-1234560", "-1.23456E+06"},
      {"%.7g", "1234560", "1234560"},
      {"%.7G", "-1234560", "-1234560"},
      {"%.2147483647g", "1234560", "1234560"},
      {"%.2147483647G", "-1234560", "-1234560"},
      {"%12g", "1234560", " 1.23456e+06"},
      {"%12G", "-1234560", "-1.23456E+06"},
      {"%12.7g", "1234560", "     1234560"},
      {"%12.7G", "-1234560", "    -1234560"},
      {"%12.2147483647g", "1234560", "     1234560"},
      {"%12.2147483647G", "-1234560", "    -1234560"},
      {"%g", "-1230000", "-1.23e+06"},
      {"%G", "1230000", "1.23E+06"},
      {"%10g", "-1230000", " -1.23e+06"},
      {"%10G", "1230000", "  1.23E+06"},
      {"%.38g", kMaxNumericValueStr, kMaxNumericValueStr},
      {"%.38G", kMinNumericValueStr, kMinNumericValueStr},
      {"%.40g", kMaxNumericValueStr, kMaxNumericValueStr},
      {"%.40G", kMinNumericValueStr, kMinNumericValueStr},
      {"%.2147483647g", kMaxNumericValueStr, kMaxNumericValueStr},
      {"%.2147483647G", kMinNumericValueStr, kMinNumericValueStr},
      {"%.0g", "-0.000014999", "-1e-05"},
      {"%.1G", "0.000014999", "1E-05"},
      {"%.2g", "0.000014999", "1.5e-05"},
      {"%.3G", "-0.000014999", "-1.5E-05"},
      {"%.0g", "0.000015", "2e-05"},
      {"%.1G", "-0.000015", "-2E-05"},
      {"%.2g", "-0.000015", "-1.5e-05"},
      {"%.3G", "0.000015", "1.5E-05"},
      {"%.0g", "-0.000094999", "-9e-05"},
      {"%.1G", "0.000094999", "9E-05"},
      {"%.2g", "-0.000094999", "-9.5e-05"},
      {"%.3G", "0.000094999", "9.5E-05"},
      {"%.0g", "0.000095", "0.0001"},
      {"%.1G", "-0.000095", "-0.0001"},
      {"%.2g", "0.000095", "9.5e-05"},
      {"%.3G", "-0.000095", "-9.5E-05"},
      {"%.0g", "-0.000149999", "-0.0001"},
      {"%.1G", "0.000149999", "0.0001"},
      {"%.2g", "-0.000149999", "-0.00015"},
      {"%.3G", "0.000149999", "0.00015"},
      {"%.0g", "-0.00015", "-0.0002"},
      {"%.1G", "0.00015", "0.0002"},
      {"%.2g", "0.00015", "0.00015"},
      {"%.3G", "-0.00015", "-0.00015"},
      {"%.0g", "1.5", "2"},
      {"%.1G", "-1.5", "-2"},
      {"%.2g", "-1.5", "-1.5"},
      {"%.3G", "1.5", "1.5"},
      {"%.0g", "15", "2e+01"},
      {"%.1G", "-15", "-2E+01"},
      {"%.2g", "15", "15"},
      {"%.3G", "-15", "-15"},
      {"%'.9G", "-1595", "-1,595"},
      {"%g", kMaxNumericValueStr, "1e+29"},
      {"%G", kMinNumericValueStr, "-1E+29"},
      {"%.37g", kMaxNumericValueStr, "100000000000000000000000000000"},
      {"%'.37g", kMaxNumericValueStr,
       "100,000,000,000,000,000,000,000,000,000"},
      {"%.37G", kMinNumericValueStr, "-100000000000000000000000000000"},
      {"%04.1g", "1.5", "0002"},
      {"%04.1g", "-1.5", "-002"},
      {"%04.2g", "1.5", "01.5"},
      {"%04.2g", "-1.5", "-1.5"},
      {"%09.0g", "0.000095", "0000.0001"},
      {"%09.1G", "-0.000095", "-000.0001"},
      {"%09.2g", "-0.000095", "-09.5e-05"},
      {"%09.2G", "0.000095", "009.5E-05"},
      {"%+9.0g", "0.000095", "  +0.0001"},
      {"%+9.1G", "-0.000095", "  -0.0001"},
      {"%+9.2g", "-0.000095", " -9.5e-05"},
      {"%+9.3G", "0.000095", " +9.5E-05"},
      {"% .0g", "0.000095", " 0.0001"},
      {"% .1G", "-0.000095", "-0.0001"},
      {"% .2g", "-0.000095", "-9.5e-05"},
      {"% .3G", "0.000095", " 9.5E-05"},
      {"%#.1g", "10", "1.e+01"},
      {"%#.1G", "-10", "-1.E+01"},
      {"%#.2g", "10", "10."},
      {"%#.2G", "-10", "-10."},
      {"%#.3g", "10", "10.0"},
      {"%#.3G", "-10", "-10.0"},
      {"%#.3g", "1", "1.00"},
      {"%#.3G", "-1", "-1.00"},
      {"%#.3g", "1e-4", "0.000100"},
      {"%#.3G", "-1e-4", "-0.000100"},
      {"%#.3g", "1e-5", "1.00e-05"},
      {"%#.3G", "-1e-5", "-1.00E-05"},
      {"%0#+9.1g", "10", "+001.e+01"},
      {"%0# 9.2g", "10", " 0000010."},
      {"%0#+9.3g", "10", "+000010.0"},
      {"%0# 9.4g", "10", " 00010.00"},
      {"%0#+'16.7g", "12345", "+00000012,345.00"},
      {"%0#+'16.7g", "1234567", "+000001,234,567."},
      {"%0#+'16.7g", "12345678", "+0001.234568e+07"},
  };
  for (const NumericTestData& test_data : kNumericTestData) {
    test_cases.emplace_back(
        "format",
        QueryParamsWithResult(
            {test_data.format_spec, NumericFromString(test_data.numeric_value)},
            test_data.expected_output)
            .WrapWithFeature(FEATURE_NUMERIC_TYPE));
    test_cases.emplace_back(
        "format",
        QueryParamsWithResult({test_data.format_spec,
                               BigNumericFromString(test_data.numeric_value)},
                              test_data.expected_output)
            .WrapWithFeature(FEATURE_BIGNUMERIC_TYPE));
  }

  // More NUMERIC cases not covered in kNumericTestData.
  const std::vector<QueryParamsWithResult> numeric_test_cases = {
      {{"%t", NullNumeric()}, "NULL"},
      {{"%T", NullNumeric()}, "NULL"},
      {{"%f", NullNumeric()}, NullString()},
      {{"%10.20f", NullNumeric()}, NullString()},
      {{"%e", NullNumeric()}, NullString()},
      {{"%10.20e", NullNumeric()}, NullString()},
      {{"%g", NullNumeric()}, NullString()},
      {{"%10.20g", NullNumeric()}, NullString()},

      {{"%T", NumericFromString("3.14")}, "NUMERIC \"3.14\""},
      {{"%T", NumericFromString("-3.14")}, "NUMERIC \"-3.14\""},
      {{"%T", Numeric(NumericValue::MaxValue())},
       "NUMERIC \"99999999999999999999999999999.999999999\""},
      {{"%T", Numeric(NumericValue::MinValue())},
       "NUMERIC \"-99999999999999999999999999999.999999999\""},

      {{"%10.*f", Int32(3), NumericFromString("1.5")}, "     1.500"},
      {{"%*.3f", Int32(10), NumericFromString("1.5")}, "     1.500"},
      {{"%*.*f", Int32(10), Int32(3), NumericFromString("1.5")}, "     1.500"},
      // negative precision means 'ignore; use default of 6'
      {{"%*.*f", Int32(10), Int32(-10), NumericFromString("1.5")},
       "  1.500000"},
      // negative width means 'left just; use positive width'
      {{"%*.*f", Int32(-10), Int32(-2), NumericFromString("1.5")},
       "1.500000  "},

      {{"%10.*e", Int32(3), NumericFromString("1.5")}, " 1.500e+00"},
      {{"%*.3E", Int32(10), NumericFromString("1.5")}, " 1.500E+00"},
      {{"%*.*e", Int32(10), Int32(3), NumericFromString("1.5")}, " 1.500e+00"},
      {{"%*.*E", Int32(15), Int32(-10), NumericFromString("1.5")},
       "   1.500000E+00"},
      {{"%*.*e", Int32(-10), Int32(-2), NumericFromString("1.5")},
       "1.500000e+00"},

      {{"%#10.*g", Int32(3), NumericFromString("0.000095")}, "  9.50e-05"},
      {{"%#*.3G", Int32(10), NumericFromString("0.000095")}, "  9.50E-05"},
      {{"%#*.*g", Int32(10), Int32(3), NumericFromString("0.000095")},
       "  9.50e-05"},
      {{"%#*.*G", Int32(15), Int32(-10), NumericFromString("0.000095")},
       "    9.50000E-05"},
      {{"%#*.*g", Int32(-10), Int32(-2), NumericFromString("0.000095")},
       "9.50000e-05"},
  };
  for (const QueryParamsWithResult& test_case : numeric_test_cases) {
    test_cases.emplace_back("format",
                            test_case.WrapWithFeature(FEATURE_NUMERIC_TYPE));
  }

  // More BIGNUMERIC cases not covered in kNumericTestData.
  const std::vector<QueryParamsWithResult> bignumeric_test_cases = {
      {{"%t", NullBigNumeric()}, "NULL"},
      {{"%T", NullBigNumeric()}, "NULL"},
      {{"%f", NullBigNumeric()}, NullString()},
      {{"%10.20f", NullBigNumeric()}, NullString()},
      {{"%e", NullBigNumeric()}, NullString()},
      {{"%10.20e", NullBigNumeric()}, NullString()},
      {{"%g", NullBigNumeric()}, NullString()},
      {{"%10.20g", NullBigNumeric()}, NullString()},

      {{"%T", BigNumericFromString("3.14")}, "BIGNUMERIC \"3.14\""},
      {{"%T", BigNumericFromString("-3.14")}, "BIGNUMERIC \"-3.14\""},
      {{"%T", BigNumeric(BigNumericValue::MaxValue())},
       "BIGNUMERIC \"578960446186580977117854925043439539266."
       "34992332820282019728792003956564819967\""},
      {{"%T", BigNumeric(BigNumericValue::MinValue())},
       "BIGNUMERIC \"-578960446186580977117854925043439539266."
       "34992332820282019728792003956564819968\""},

      {{"%10.*f", Int32(3), BigNumericFromString("1.5")}, "     1.500"},
      {{"%*.3f", Int32(10), BigNumericFromString("1.5")}, "     1.500"},
      {{"%*.*f", Int32(10), Int32(3), BigNumericFromString("1.5")},
       "     1.500"},
      // negative precision means 'ignore; use default of 6'
      {{"%*.*f", Int32(10), Int32(-10), BigNumericFromString("1.5")},
       "  1.500000"},
      // negative width means 'left just; use positive width'
      {{"%*.*f", Int32(-10), Int32(-2), BigNumericFromString("1.5")},
       "1.500000  "},

      {{"%10.*e", Int32(3), BigNumericFromString("1.5")}, " 1.500e+00"},
      {{"%*.3E", Int32(10), BigNumericFromString("1.5")}, " 1.500E+00"},
      {{"%*.*e", Int32(10), Int32(3), BigNumericFromString("1.5")},
       " 1.500e+00"},
      {{"%*.*E", Int32(15), Int32(-10), BigNumericFromString("1.5")},
       "   1.500000E+00"},
      {{"%*.*e", Int32(-10), Int32(-2), BigNumericFromString("1.5")},
       "1.500000e+00"},

      {{"%#10.*g", Int32(3), BigNumericFromString("0.000095")}, "  9.50e-05"},
      {{"%#*.3G", Int32(10), BigNumericFromString("0.000095")}, "  9.50E-05"},
      {{"%#*.*g", Int32(10), Int32(3), BigNumericFromString("0.000095")},
       "  9.50e-05"},
      {{"%#*.*G", Int32(15), Int32(-10), BigNumericFromString("0.000095")},
       "    9.50000E-05"},
      {{"%#*.*g", Int32(-10), Int32(-2), BigNumericFromString("0.000095")},
       "9.50000e-05"},

      {{"%f", BigNumericFromString("1.99999999999999999999999999999999999999")},
       "2.000000"},
      {{"%10f",
        BigNumericFromString("1.99999999999999999999999999999999999999")},
       "  2.000000"},
      {{"%10.0f",
        BigNumericFromString("1.99999999999999999999999999999999999999")},
       "         2"},
      {{"%40.38f",
        BigNumericFromString("1.99999999999999999999999999999999999999")},
       "1.99999999999999999999999999999999999999"},
      {{"%40.37f",
        BigNumericFromString("1.99999999999999999999999999999999999999")},
       " 2.0000000000000000000000000000000000000"},
      {{"%40.37f",
        BigNumericFromString("-1.99999999999999999999999999999999999999")},
       "-2.0000000000000000000000000000000000000"},
      {{"%41.39f",
        BigNumericFromString("1.99999999999999999999999999999999999999")},
       "1.999999999999999999999999999999999999990"},
      {{"%43.39f",
        BigNumericFromString("1.99999999999999999999999999999999999999")},
       "  1.999999999999999999999999999999999999990"},
      {{"%43.39f",
        BigNumericFromString("-1.99999999999999999999999999999999999999")},
       " -1.999999999999999999999999999999999999990"},
      {{"%.38f", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564819967"},
      {{"%.40f", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481996700"},
      {{"%.0f", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266"},
      {{"%82.40f", BigNumeric(BigNumericValue::MaxValue())},
       "  "
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481996700"},
      {{"%80.40f", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481996700"},

      // %e and %E produce mantissa/exponent notation.
      {{"%e", BigNumeric(BigNumericValue::MaxValue())}, "5.789604e+38"},
      {{"%e", BigNumeric(BigNumericValue::MinValue())}, "-5.789604e+38"},
      {{"%.76e", BigNumeric(BigNumericValue::MaxValue())},
       "5.78960446186580977117854925043439539266"
       "34992332820282019728792003956564819967e+38"},
      {{"%.76e", BigNumeric(BigNumericValue::MinValue())},
       "-5.78960446186580977117854925043439539266"
       "34992332820282019728792003956564819968e+38"},
      {{"%.80e", BigNumeric(BigNumericValue::MaxValue())},
       "5.78960446186580977117854925043439539266"
       "349923328202820197287920039565648199670000e+38"},
      {{"%.80e", BigNumeric(BigNumericValue::MinValue())},
       "-5.78960446186580977117854925043439539266"
       "349923328202820197287920039565648199680000e+38"},
      {{"%.75e", BigNumeric(BigNumericValue::MaxValue())},
       "5.78960446186580977117854925043439539266"
       "3499233282028201972879200395656481997e+38"},
      {{"%.75e", BigNumeric(BigNumericValue::MinValue())},
       "-5.78960446186580977117854925043439539266"
       "3499233282028201972879200395656481997e+38"},
      {{"%.25e", BigNumeric(BigNumericValue::MaxValue())},
       "5.7896044618658097711785493e+38"},
      {{"%.25e", BigNumeric(BigNumericValue::MinValue())},
       "-5.7896044618658097711785493e+38"},
      {{"%.22e", BigNumeric(BigNumericValue::MaxValue())},
       "5.7896044618658097711785e+38"},
      {{"%.22e", BigNumeric(BigNumericValue::MinValue())},
       "-5.7896044618658097711785e+38"},
      {{"%.5e", BigNumeric(BigNumericValue::MaxValue())}, "5.78960e+38"},
      {{"%.5e", BigNumeric(BigNumericValue::MinValue())}, "-5.78960e+38"},
      {{"%.0e", BigNumeric(BigNumericValue::MaxValue())}, "6e+38"},
      {{"%.0e", BigNumeric(BigNumericValue::MinValue())}, "-6e+38"},

      {{"%g", BigNumeric(BigNumericValue::MaxValue())}, "5.7896e+38"},
      {{"%G", BigNumeric(BigNumericValue::MinValue())}, "-5.7896E+38"},
      {{"%.76g", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "3499233282028201972879200395656481997"},
      {{"%.76G", BigNumeric(BigNumericValue::MinValue())},
       "-578960446186580977117854925043439539266."
       "3499233282028201972879200395656481997"},
      {{"%.77g", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564819967"},
      {{"%.77G", BigNumeric(BigNumericValue::MinValue())},
       "-578960446186580977117854925043439539266."
       "34992332820282019728792003956564819968"},
      {{"%.80g", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564819967"},
      {{"%.80G", BigNumeric(BigNumericValue::MinValue())},
       "-578960446186580977117854925043439539266."
       "34992332820282019728792003956564819968"},
      {{"%.2147483647g", BigNumeric(BigNumericValue::MaxValue())},
       "578960446186580977117854925043439539266."
       "34992332820282019728792003956564819967"},
      {{"%.2147483647G", BigNumeric(BigNumericValue::MinValue())},
       "-578960446186580977117854925043439539266."
       "34992332820282019728792003956564819968"},
  };
  for (const QueryParamsWithResult& test_case : bignumeric_test_cases) {
    test_cases.emplace_back("format",
                            test_case.WrapWithFeature(FEATURE_BIGNUMERIC_TYPE));
  }

  return test_cases;
}

}  // namespace googlesql
