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

#include "googlesql/analyzer/expr_resolver_helper.h"

#include <memory>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testdata/sample_catalog.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

TEST(ResolvedTVFArgTest, GetScan) {
  ResolvedTVFArg arg;
  EXPECT_FALSE(arg.IsScan());
  EXPECT_THAT(arg.GetScan(), StatusIs(absl::StatusCode::kInternal));
}

TEST(GetAliasForExpression, ASTIdentifier) {
  ASTIdentifier identifier_node;
  IdString identifier = IdString::MakeGlobal("foo");
  identifier_node.SetIdentifier(identifier);
  EXPECT_EQ(GetAliasForExpression(&identifier_node).ToString(), "foo");
}

TEST(GetAliasForExpression, ASTPathExpression) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(
      ParseExpression("a.b", ParserOptions{LanguageOptions{}}, &parser_output));
  const ASTPathExpression* path =
      parser_output->expression()->GetAsOrDie<ASTPathExpression>();
  EXPECT_EQ(GetAliasForExpression(path).ToString(), "b");
}

TEST(GetAliasForExpression, ASTDotIdentifier) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseExpression("foo[3].array", ParserOptions{LanguageOptions{}},
                            &parser_output));
  const ASTDotIdentifier* dot_identifier =
      parser_output->expression()->GetAsOrDie<ASTDotIdentifier>();
  EXPECT_EQ(GetAliasForExpression(dot_identifier).ToString(), "array");
}

// `GetAliasForExpression` does not know how to assign aliases, so an empty
// IdString will be returned.
TEST(GetAliasForExpression, OtherASTNodeTypes) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseExpression("(SELECT 1)", ParserOptions{LanguageOptions{}},
                            &parser_output));
  EXPECT_EQ(GetAliasForExpression(parser_output->expression()).ToString(), "");
}

class IsConstantTest : public ::testing::Test {
 protected:
  IsConstantTest() {
    options_.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
  }

  absl::Status Analyze(absl::string_view sql) {
    return AnalyzeExpression(sql, options_, catalog_.catalog(), &type_factory_,
                             &output_);
  }

  const ResolvedExpr* result() { return output_->resolved_expr(); }

  std::unique_ptr<const AnalyzerOutput> output_;
  TypeFactory type_factory_;
  SampleCatalog catalog_;
  AnalyzerOptions options_;
};

TEST_F(IsConstantTest, StackOCasts) {
  GOOGLESQL_ASSERT_OK(Analyze(
      "CAST(CAST(CAST(CAST(CAST('apples' AS BYTES) AS STRING) AS BYTES) AS "
      "STRING) AS BYTES)"));
  EXPECT_THAT(IsConstantExpression(result()), IsOkAndHolds(true));
  EXPECT_THAT(IsConstantFunctionArg(result()), IsOkAndHolds(true));
  EXPECT_THAT(IsNonAggregateFunctionArg(result()), IsOkAndHolds(true));
}

TEST_F(IsConstantTest, VolatileExpr) {
  GOOGLESQL_ASSERT_OK(Analyze("IF(RAND() > 0.5, 'bananas', 'avocados')"));
  EXPECT_THAT(IsConstantExpression(result()), IsOkAndHolds(false));
  EXPECT_THAT(IsConstantFunctionArg(result()), IsOkAndHolds(false));
  EXPECT_THAT(IsNonAggregateFunctionArg(result()), IsOkAndHolds(false));
}

TEST_F(IsConstantTest, CastWithFormat) {
  GOOGLESQL_ASSERT_OK(
      Analyze("CAST('apples' AS BYTES "
              "FORMAT IF(RAND() > 0.5, 'bananas', 'avocados'))"));
  // TODO: These results should be 'false'.
  EXPECT_THAT(IsConstantExpression(result()), IsOkAndHolds(true));
  EXPECT_THAT(IsConstantFunctionArg(result()), IsOkAndHolds(true));
  EXPECT_THAT(IsNonAggregateFunctionArg(result()), IsOkAndHolds(true));
}

}  // namespace googlesql
