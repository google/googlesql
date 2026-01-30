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

#include <string>
#include <vector>

#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

std::vector<FunctionTestCall> GetFunctionTestsParseNumeric() {
  using values::NullNumeric;
  using values::NullBigNumeric;
  using values::NullString;

  // The error that is generated when the input is malformed.
  auto format_error = absl::OutOfRangeError("Invalid input to PARSE_NUMERIC");

  std::vector<FunctionTestCall> numeric_test_cases = {
      // parse_numeric(string) -> numeric
      {"parse_numeric", {NullString()}, NullNumeric()},
      {"parse_numeric",
       {" - 12.34  "},
       NumericValue::FromString("-12.34").value()},
      {"parse_numeric",
       {" 12.34e-1 - "},
       NumericValue::FromString("-12.34e-1").value()},
      {"parse_numeric",
       {" 1,2,3,.45e+10 + "},
       NumericValue::FromString("123.45e10").value()},
      {"parse_numeric",
       {" 1,2.3e-10 + "},
       NumericValue::FromString("12.3e-10").value()},
      {"parse_numeric",
       {" ,1,2,34. + "},
       NumericValue::FromString("1234").value()},
      {"parse_numeric",
       {" .1234 "},
       NumericValue::FromString("0.1234").value()},
      // different whitespaces
      {"parse_numeric",
       {" + \t\v .1 \r\n\f "},
       NumericValue::FromString("0.1").value()},
      {"parse_numeric", {" ,,,,1 "}, NumericValue(1)},
      {"parse_numeric", {" 1,,, "}, NumericValue(1)},
      {"parse_numeric", {" 1,,,2 "}, NumericValue(12)},
      {"parse_numeric", {" 1e0 "}, NumericValue(1)},
      {"parse_numeric", {" 1e+28 "}, NumericValue::FromString("1e28").value()},
      {"parse_numeric", {" 1E+28 "}, NumericValue::FromString("1e28").value()},
      {"parse_numeric", {" 1e-30 "}, NumericValue::FromString("0").value()},

      // invalid inputs: the input is malformed.
      {"parse_numeric", {" . "}, NullNumeric(), format_error},
      {"parse_numeric", {" $ 12.34  "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1 2.34  "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1e2,3  "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1.2.3  "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1..2  "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1.2,3  "}, NullNumeric(), format_error},
      {"parse_numeric", {" ,,,.123 "}, NullNumeric(), format_error},
      {"parse_numeric", {".e1"}, NullNumeric(), format_error},
      {"parse_numeric", {"e1"}, NullNumeric(), format_error},
      {"parse_numeric", {"1 e+28"}, NullNumeric(), format_error},
      {"parse_numeric", {"1 e 10"}, NullNumeric(), format_error},
      {"parse_numeric", {"1 . 10"}, NullNumeric(), format_error},
      {"parse_numeric", {" - 12.3 -"}, NullNumeric(), format_error},
      {"parse_numeric", {"  -- 1"}, NullNumeric(), format_error},
      {"parse_numeric", {"1 ++  "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1 + 2  "}, NullNumeric(), format_error},
      {"parse_numeric", {" + 12 + "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1e--10 "}, NullNumeric(), format_error},
      {"parse_numeric", {" 1e2.3 "}, NullNumeric(), format_error},

      // invalid input: the input is out of range of Numeric.
      {"parse_numeric", {" 1e+29 "}, NullNumeric(), format_error},
  };

  format_error = absl::OutOfRangeError("Invalid input to PARSE_BIGNUMERIC");
  std::vector<FunctionTestCall> bignumeric_test_cases = {
      // parse_bignumeric(string) -> bignumeric
      {"parse_bignumeric", {NullString()}, NullBigNumeric()},
      {"parse_bignumeric",
       {" - 12.34  "},
       BigNumericValue::FromString("-12.34").value()},
      {"parse_bignumeric",
       {" 12.34e-1 - "},
       BigNumericValue::FromString("-12.34e-1").value()},
      {"parse_bignumeric",
       {" 1,2,3,.45e+10 + "},
       BigNumericValue::FromString("123.45e10").value()},
      {"parse_bignumeric",
       {" 1,2.3e-10 + "},
       BigNumericValue::FromString("12.3e-10").value()},
      {"parse_bignumeric",
       {" ,1,2,34. + "},
       BigNumericValue::FromString("1234").value()},
      {"parse_bignumeric",
       {" .1234 "},
       BigNumericValue::FromString("0.1234").value()},
      {"parse_bignumeric",
       {" 1e+28 "},
       BigNumericValue::FromString("1e28").value()},
      // Note that this value is parsed to 0 by parse_numeric.
      {"parse_bignumeric",
       {" 1e-30 "},
       BigNumericValue::FromString("1e-30").value()},
      // Theses inputs are out of range for Numeric, but are fine for BigNumeric
      {"parse_bignumeric",
       {" 1e+29 "},
       BigNumericValue::FromString("1e29").value()},
      {"parse_bignumeric",
       {" 1e+38 "},
       BigNumericValue::FromString("1e38").value()},

      // invalid inputs: the input is malformed.
      {"parse_bignumeric", {" .  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" $ 12.34  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1 2.34  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1e2,3  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1.2.3  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1.2,3  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" ,,,.123 "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {".e1"}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {"e1"}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {"-12.3-"}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" - 12.3 -"}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {"  -- 1"}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {"1 ++  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1 + 2  "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" + 12 + "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1e--10 "}, NullBigNumeric(), format_error},
      {"parse_bignumeric", {" 1e2.3 "}, NullBigNumeric(), format_error},

      // invalid input: the input is out of range of Numeric.
      {"parse_bignumeric", {" 1e+39 "}, NullBigNumeric(), format_error},
  };

  std::vector<FunctionTestCall> test_cases;
  test_cases.reserve(numeric_test_cases.size() + bignumeric_test_cases.size());

  for (const auto& test_case : numeric_test_cases) {
    test_cases.emplace_back(FunctionTestCall(
        test_case.function_name,
        test_case.params.WrapWithFeature(FEATURE_NUMERIC_TYPE)));
  }

  for (const auto& test_case : bignumeric_test_cases) {
    test_cases.emplace_back(FunctionTestCall(
        test_case.function_name,
        test_case.params.WrapWithFeature(FEATURE_BIGNUMERIC_TYPE)));
  }

  return test_cases;
}

}  // namespace googlesql
