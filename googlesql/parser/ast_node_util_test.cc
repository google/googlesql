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

#include "googlesql/parser/ast_node_util.h"

#include <memory>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parser.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace googlesql {
namespace {
using ::absl_testing::IsOkAndHolds;

TEST(GetMaxParseTreeDepth, Simple) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_CHECK_OK(ParseExpression("1 + (2 + 3)", ParserOptions(), &parser_output));
  ABSL_CHECK_EQ(
      absl::StripAsciiWhitespace(parser_output->expression()->DebugString()),
      absl::StripAsciiWhitespace(
          R"(
BinaryExpression(+) [0-11]
  IntLiteral(1) [0-1]
  BinaryExpression(+) [5-10]
    IntLiteral(2) [5-6]
    IntLiteral(3) [9-10]
           )"));

  const ASTExpression* expr = parser_output->expression();
  EXPECT_THAT(GetMaxParseTreeDepth(expr), IsOkAndHolds(3));
  EXPECT_THAT(GetMaxParseTreeDepth(expr->child(0)), IsOkAndHolds(1));
  EXPECT_THAT(GetMaxParseTreeDepth(expr->child(1)), IsOkAndHolds(2));
  EXPECT_THAT(GetMaxParseTreeDepth(expr->child(1)->child(0)), IsOkAndHolds(1));
  EXPECT_THAT(GetMaxParseTreeDepth(expr->child(1)->child(1)), IsOkAndHolds(1));
}

TEST(GetMaxParseTreeDepth, DeepTree) {
  // Make sure no stack overflow occurs when calling GetMaxParseTreeDepth() on
  // a very deep parse tree.
  std::string expr_text;
  for (int i = 0; i < 10000; ++i) {
    absl::StrAppend(&expr_text, "1 + ");
  }
  absl::StrAppend(&expr_text, 1);

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_CHECK_OK(ParseExpression(expr_text, ParserOptions(), &parser_output));

  EXPECT_THAT(GetMaxParseTreeDepth(parser_output->expression()),
              IsOkAndHolds(10001));
}
}  // namespace
}  // namespace googlesql
