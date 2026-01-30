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

#include <limits>
#include <vector>

#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT

namespace googlesql {

std::vector<FunctionTestCall> GetFunctionTestsFormatFloatingPoint() {
  const double kNan = std::numeric_limits<double>::quiet_NaN();
  const double kNegNan = absl::bit_cast<double>(0xfff8000000000000ull);
  const double kPosInf = std::numeric_limits<double>::infinity();
  const double kNegInf = -std::numeric_limits<double>::infinity();
  const float kFloatNan = std::numeric_limits<float>::quiet_NaN();
  const float kFloatNegNan = absl::bit_cast<float>(0xffc00000u);
  const float kFloatPosInf = std::numeric_limits<float>::infinity();
  const float kFloatNegInf = -std::numeric_limits<float>::infinity();

  return std::vector<FunctionTestCall>({
      // Floating point.
      {"format", {"%f", Float(0)}, "0.000000"},
      {"format", {"%f", Float(-10.25)}, "-10.250000"},
      {"format", {"%f", kFloatNan}, "nan"},
      {"format", {"%f", kFloatNegNan}, "nan"},
      {"format", {"%f", kFloatPosInf}, "inf"},
      {"format", {"%f", kFloatNegInf}, "-inf"},
      {"format", {"%F", kFloatNan}, "NAN"},
      {"format", {"%F", kFloatNegNan}, "NAN"},
      {"format", {"%F", kFloatPosInf}, "INF"},
      {"format", {"%F", kFloatNegInf}, "-INF"},

      {"format", {"%f", 0.0}, "0.000000"},
      {"format", {"%f", -10.25}, "-10.250000"},
      {"format", {"%f", kNan}, "nan"},
      {"format", {"%f", kNegNan}, "nan"},
      {"format", {"%f", kPosInf}, "inf"},
      {"format", {"%f", kNegInf}, "-inf"},
      {"format", {"%F", kNan}, "NAN"},
      {"format", {"%F", kNegNan}, "NAN"},
      {"format", {"%F", kPosInf}, "INF"},
      {"format", {"%F", kNegInf}, "-INF"},

      {"format", {"%+f", 0.0}, "+0.000000"},
      {"format", {"%+f", +0.0}, "+0.000000"},
      {"format", {"%+f", -0.0}, "+0.000000"},
      {"format", {"%f", -0.0}, "0.000000"},
      {"format", {"%+e", 0.0}, "+0.000000e+00"},
      {"format", {"%+e", +0.0}, "+0.000000e+00"},
      {"format", {"%+e", -0.0}, "+0.000000e+00"},
      {"format", {"%e", -0.0}, "0.000000e+00"},
      {"format", {"%+g", 0.0}, "+0"},
      {"format", {"%+g", +0.0}, "+0"},
      {"format", {"%+g", -0.0}, "+0"},
      {"format", {"%g", -0.0}, "0"},

      {"format", {"%f", 0.0}, "0.000000"},
      {"format", {"%'f", 0.1}, "0.100000"},
      {"format", {"%'.5f", 0.1}, "0.10000"},
      {"format", {"%'010.5f", 0.1}, "0000.10000"},
      {"format", {"%'010.5f", 0.00100025}, "0000.00100"},
      {"format", {"%' 010.5f", 0.1}, " 000.10000"},
      {"format", {"%' 010.5f", -0.1}, "-000.10000"},
      {"format", {"%'+010.5f", 0.1}, "+000.10000"},
      {"format", {"%f", -10.25}, "-10.250000"},
      {"format", {"%f", 1e20}, "100000000000000000000.000000"},
      {"format", {"%'f", 1e20}, "100,000,000,000,000,000,000.000000"},
      {"format", {"%F", 0.0}, "0.000000"},
      {"format", {"%F", -10.25}, "-10.250000"},
      {"format", {"%F", 1e20}, "100000000000000000000.000000"},
      {"format", {"%'F", 1e20}, "100,000,000,000,000,000,000.000000"},
      {"format", {"%g", 0.0}, "0"},
      {"format", {"%g", -10.25}, "-10.25"},
      {"format", {"%g", 1e20}, "1e+20"},
      {"format", {"%'g", 1e20}, "1e+20"},
      {"format", {"%G", 0.0}, "0"},
      {"format", {"%G", -10.25}, "-10.25"},
      {"format", {"%G", 1e20}, "1E+20"},
      {"format", {"%'G", 1e20}, "1E+20"},
      {"format", {"%e", 0.0}, "0.000000e+00"},
      {"format", {"%e", -10.25}, "-1.025000e+01"},
      {"format", {"%e", 1e20}, "1.000000e+20"},
      {"format", {"%'e", 1e20}, "1.000000e+20"},
      {"format", {"%E", 0.0}, "0.000000E+00"},
      {"format", {"%E", -10.25}, "-1.025000E+01"},
      {"format", {"%E", 1e20}, "1.000000E+20"},
      {"format", {"%'E", 1e20}, "1.000000E+20"},

      // This case was a repro for a bug in util::format: b/36557737.
      {"format", {"%0# 15.5g", -158.49559295148902}, "-00000000158.50"},

      {"format", {"%6.2f", 1.1}, "  1.10"},
      {"format", {"%+6.2f", 1.1}, " +1.10"},
      {"format", {"%-6.2f", 1.1}, "1.10  "},
      {"format", {"%+-6.2f", 1.1}, "+1.10 "},
      {"format", {"%#6.2f", 1.1}, "  1.10"},
      {"format", {"%06.2f", 1.1}, "001.10"},
      {"format", {"%-06.2f", 1.1}, "1.10  "},
      {"format", {"%*.*f", 6, 2, 1.1}, "  1.10"},
      // negative precision means 'ignore, use default of 6'
      {"format", {"%*.*f", 10, -2, 1.1}, "  1.100000"},
      {"format", {"%.*f", -2147483649, Double(5)}, "5.000000"},
      // negative width means 'left justify; use positive width'
      {"format", {"%*.*f", -10, -2, 1.1}, "1.100000  "},
      {"format", {"%'*.*f", 10, 2, 12345.6789}, " 12,345.68"},

      {"format", {"%.*g", 1000000000, 1.5}, "1.5"},
      {"format", {"%.1000000000g", 2.5}, "2.5"},

      // Some cases to show padding and grouping working together on floats
      {"format", {"%'015.7f", Double(1234567)}, "1,234,567.0000000"},
      {"format", {"%'015.2f", Double(1234567)}, "0001,234,567.00"},
      {"format", {"%'015.2g", Double(1234567)}, "000000001.2e+06"},
      {"format", {"%'015.2e", Double(1234567)}, "00000001.23e+06"},
      {"format", {"%+'015.7f", Double(1234567)}, "+1,234,567.0000000"},
      {"format", {"%+'015.2f", Double(1234567)}, "+001,234,567.00"},
      {"format", {"%+'015.2g", Double(1234567)}, "+00000001.2e+06"},
      {"format", {"%+'015.2e", Double(1234567)}, "+0000001.23e+06"},
      {"format", {"% '015.7f", Double(1234567)}, " 1,234,567.0000000"},
      {"format", {"% '015.2f", Double(1234567)}, " 001,234,567.00"},
      {"format", {"% '015.2g", Double(1234567)}, " 00000001.2e+06"},
      {"format", {"% '015.2e", Double(1234567)}, " 0000001.23e+06"},
      {"format", {"%'015.7f", Double(-1234567)}, "-1,234,567.0000000"},
      {"format", {"%'015.2f", Double(-1234567)}, "-001,234,567.00"},
      {"format", {"%'015.2g", Double(-1234567)}, "-00000001.2e+06"},
      {"format", {"%'015.2e", Double(-1234567)}, "-0000001.23e+06"},
      {"format", {"%'015.0g", Double(1234567)}, "00000000001e+06"},
      {"format", {"%'015.0e", Double(1234567)}, "00000000001e+06"},
      {"format", {"%'015.0G", Double(1234567)}, "00000000001E+06"},
      {"format", {"%'015.0E", Double(1234567)}, "00000000001E+06"},
      {"format", {"%'15.7f", Double(1234567)}, "1,234,567.0000000"},
      {"format", {"%'15.2f", Double(1234567)}, "   1,234,567.00"},
      {"format", {"%'15.2g", Double(1234567)}, "        1.2e+06"},
      {"format", {"%'15.2e", Double(1234567)}, "       1.23e+06"},
      {"format", {"%'15.7f", Double(-1234567)}, "-1,234,567.0000000"},
      {"format", {"%'15.2f", Double(-1234567)}, "  -1,234,567.00"},
      {"format", {"%'15.2g", Double(-1234567)}, "       -1.2e+06"},
      {"format", {"%'15.2e", Double(-1234567)}, "      -1.23e+06"},
      {"format", {"%+'15.7f", Double(1234567)}, "+1,234,567.0000000"},
      {"format", {"%+'15.2f", Double(1234567)}, "  +1,234,567.00"},
      {"format", {"%+'15.2g", Double(1234567)}, "       +1.2e+06"},
      {"format", {"%+'15.2e", Double(1234567)}, "      +1.23e+06"},
      {"format", {"% '15.7f", Double(1234567)}, " 1,234,567.0000000"},
      {"format", {"% '15.2f", Double(1234567)}, "   1,234,567.00"},
      {"format", {"% '15.2g", Double(1234567)}, "        1.2e+06"},
      {"format", {"% '15.2e", Double(1234567)}, "       1.23e+06"},
      {"format", {"% '15.0f", Double(1234567)}, "      1,234,567"},
      {"format", {"% '15.0g", Double(1234567)}, "          1e+06"},
      {"format", {"% '15.0e", Double(1234567)}, "          1e+06"},
      {"format", {"% '15.0G", Double(1234567)}, "          1E+06"},
      {"format", {"% '15.0E", Double(1234567)}, "          1E+06"},
      {"format", {"%-'15.7f", Double(1234567)}, "1,234,567.0000000"},
      {"format", {"%-'15.2f", Double(1234567)}, "1,234,567.00   "},
      {"format", {"%-'15.2g", Double(1234567)}, "1.2e+06        "},
      {"format", {"%-'15.2e", Double(1234567)}, "1.23e+06       "},
      {"format", {"%-'15.7f", Double(-1234567)}, "-1,234,567.0000000"},
      {"format", {"%-'15.2f", Double(-1234567)}, "-1,234,567.00  "},
      {"format", {"%-'15.2g", Double(-1234567)}, "-1.2e+06       "},
      {"format", {"%-'15.2e", Double(-1234567)}, "-1.23e+06      "},
      {"format", {"%-+'15.7f", Double(1234567)}, "+1,234,567.0000000"},
      {"format", {"%-+'15.2f", Double(1234567)}, "+1,234,567.00  "},
      {"format", {"%-+'15.2g", Double(1234567)}, "+1.2e+06       "},
      {"format", {"%-+'15.2e", Double(1234567)}, "+1.23e+06      "},
      {"format", {"%- '15.7f", Double(1234567)}, " 1,234,567.0000000"},
      {"format", {"%- '15.2f", Double(1234567)}, " 1,234,567.00  "},
      {"format", {"%- '15.2g", Double(1234567)}, " 1.2e+06       "},
      {"format", {"%- '15.2e", Double(1234567)}, " 1.23e+06      "},
      {"format", {"%- '15.0f", Double(1234567)}, " 1,234,567     "},
      {"format", {"%- '15.0g", Double(1234567)}, " 1e+06         "},
      {"format", {"%- '15.0e", Double(1234567)}, " 1e+06         "},
      {"format", {"%- '15.0G", Double(1234567)}, " 1E+06         "},
      {"format", {"%- '15.0E", Double(1234567)}, " 1E+06         "},
      {"format", {"%'f", Double(1234567)}, "1,234,567.000000"},
      {"format", {"%'g", Double(1234567)}, "1.23457e+06"},
      {"format", {"%'e", Double(1234567)}, "1.234567e+06"},
      {"format", {"%'f", Double(-1234567)}, "-1,234,567.000000"},
      {"format", {"%'g", Double(-1234567)}, "-1.23457e+06"},
      {"format", {"%'e", Double(-1234567)}, "-1.234567e+06"},
      {"format", {"%+'f", Double(1234567)}, "+1,234,567.000000"},
      {"format", {"%+'g", Double(1234567)}, "+1.23457e+06"},
      {"format", {"%+'e", Double(1234567)}, "+1.234567e+06"},
      {"format", {"% 'f", Double(1234567)}, " 1,234,567.000000"},
      {"format", {"% 'g", Double(1234567)}, " 1.23457e+06"},
      {"format", {"% 'e", Double(1234567)}, " 1.234567e+06"},
      {"format", {"%015.2e", kNan}, "            nan"},
      {"format", {"%015.2e", kNegNan}, "            nan"},
      {"format", {"%015.2f", kPosInf}, "            inf"},
      {"format", {"%015.2g", kNegInf}, "           -inf"},
      {"format", {"%'15.2e", kNan}, "            nan"},
      {"format", {"%'15.2e", kNegNan}, "            nan"},
      {"format", {"%'15.2f", kPosInf}, "            inf"},
      {"format", {"%'15.2g", kNegInf}, "           -inf"},
      {"format", {"%-'15.2e", kNan}, "nan            "},
      {"format", {"%-'15.2e", kNegNan}, "nan            "},
      {"format", {"%-'15.2f", kPosInf}, "inf            "},
      {"format", {"%-'15.2g", kNegInf}, "-inf           "},
      // These are cases where %g/G takes the %f style format with grouping.
      {"format", {"%'g", Double(1000)}, "1,000"},
      {"format", {"%'G", Double(1000)}, "1,000"},

      // Float and double special values with %t and %T.
      {"format", {"%t", kFloatNan}, "nan"},
      {"format", {"%t", kFloatNegNan}, "nan"},
      {"format", {"%t", kFloatPosInf}, "inf"},
      {"format", {"%t", kFloatNegInf}, "-inf"},
      {"format", {"%t", kNan}, "nan"},
      {"format", {"%t", kNegNan}, "nan"},
      {"format", {"%t", kPosInf}, "inf"},
      {"format", {"%t", kNegInf}, "-inf"},
      // Note that these always produce a cast to FLOAT64/FLOAT32, in both
      // INTERNAL and EXTERNAL ProductMode.  We could have made the cast to
      // DOUBLE/FLOAT in the INTERNAL mode case, but then the same query could
      // get different results in INTERNAL vs. EXTERNAL mode and that seems bad.
      {"format", {"%T", kFloatNan}, "CAST(\"nan\" AS FLOAT32)"},
      {"format", {"%T", kFloatNegNan}, "CAST(\"nan\" AS FLOAT32)"},
      {"format", {"%T", kFloatPosInf}, "CAST(\"inf\" AS FLOAT32)"},
      {"format", {"%T", kFloatNegInf}, "CAST(\"-inf\" AS FLOAT32)"},
      {"format", {"%T", kNan}, "CAST(\"nan\" AS FLOAT64)"},
      {"format", {"%T", kNegNan}, "CAST(\"nan\" AS FLOAT64)"},
      {"format", {"%T", kPosInf}, "CAST(\"inf\" AS FLOAT64)"},
      {"format", {"%T", kNegInf}, "CAST(\"-inf\" AS FLOAT64)"},

      // Non-finite values inside arrays.
      {"format",
       {"%t",
        values::FloatArray({4, -2.5, std::numeric_limits<float>::quiet_NaN()})},
       "[4.0, -2.5, nan]"},
      {"format",
       {"%T",
        values::FloatArray({4, -2.5, std::numeric_limits<float>::quiet_NaN()})},
       "[4.0, -2.5, CAST(\"nan\" AS FLOAT32)]"},
      {"format",
       {"%t",
        values::DoubleArray({-4, std::numeric_limits<double>::infinity()})},
       "[-4.0, inf]"},
      {"format",
       {"%T",
        values::DoubleArray({-4, -std::numeric_limits<double>::infinity()})},
       "[-4.0, CAST(\"-inf\" AS FLOAT64)]"},
  });
}

}  // namespace googlesql
