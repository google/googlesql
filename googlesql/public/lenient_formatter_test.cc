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

#include "googlesql/public/lenient_formatter.h"

#include <algorithm>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/formatter_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "unicode/uchar.h"
#include "unicode/unistr.h"

namespace googlesql {

namespace {

TEST(LenientFormatterTest, TestOptions) {
  const std::string input = R"(# Test query
    SELECT "a long" + " expression" + " that" + " fits" + " in"
    + " default line_length" FROM Table
    WHERE condition_that_makes_the_entire_query > default_line_length;
  )";
  std::string result;

  // Formatting with default options.
  GOOGLESQL_EXPECT_OK(LenientFormatSql(input, &result));
  EXPECT_EQ(result, R"(# Test query
SELECT
  "a long" + " expression" + " that" + " fits" + " in" + " default line_length"
FROM Table
WHERE condition_that_makes_the_entire_query > default_line_length;
)") << result;

  FormatterOptions options;
  options.SetIndentationSpaces(4);
  options.SetLineLengthLimit(40);
  // A nonesense new line separator, but is very visible in the expected result
  // below.
  options.SetNewLineType("\n|");

  GOOGLESQL_EXPECT_OK(LenientFormatSql(input, &result, options));
  EXPECT_EQ(result, R"(# Test query
|SELECT
|    "a long" + " expression" + " that"
|    + " fits" + " in"
|    + " default line_length"
|FROM Table
|WHERE
|    condition_that_makes_the_entire_query
|    > default_line_length;
|)") << result;
}

}  // namespace
}  // namespace googlesql
