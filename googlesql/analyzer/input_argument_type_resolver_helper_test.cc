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

#include "googlesql/analyzer/input_argument_type_resolver_helper.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/prepared_expression_constant_evaluator.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "gtest/gtest.h"

namespace googlesql {

class InputArgumentTypeResolverHelperTest : public ::testing::Test {
 protected:
  InputArgumentTypeResolverHelperTest() = default;
  InputArgumentTypeResolverHelperTest(
      const InputArgumentTypeResolverHelperTest&) = delete;
  InputArgumentTypeResolverHelperTest& operator=(
      const InputArgumentTypeResolverHelperTest&) = delete;
  ~InputArgumentTypeResolverHelperTest() override = default;

  void SetUp() override {
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
    analyzer_options_.mutable_language()
        ->EnableMaximumLanguageFeaturesForDevelopment();
    analyzer_options_.CreateDefaultArenasIfNotSet();

    constant_evaluator_ = std::make_unique<PreparedExpressionConstantEvaluator>(
        /*options=*/EvaluatorOptions{});
    analyzer_options_.set_constant_evaluator(constant_evaluator_.get());
  }

  void TearDown() override {}
  AnalyzerOptions analyzer_options_;
  std::unique_ptr<PreparedExpressionConstantEvaluator> constant_evaluator_;
};

TEST_F(InputArgumentTypeResolverHelperTest, LambdaInputArgumentType) {
  ASTLambda astLambda;
  std::vector<const ASTNode*> arg_ast_nodes;
  arg_ast_nodes.push_back(&astLambda);
  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.push_back(nullptr);

  ASTIntLiteral int_literal;
  arg_ast_nodes.push_back(&int_literal);
  arguments.push_back(MakeResolvedLiteral(Value::Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<InputArgumentType> input_arguments,
      GetInputArgumentTypesForGenericArgumentList(
          arg_ast_nodes, arguments,
          /*pick_default_type_for_untyped_expr=*/false, analyzer_options_));
  ASSERT_EQ(input_arguments.size(), 2);
  ASSERT_TRUE(input_arguments[0].is_lambda());
  ASSERT_EQ(input_arguments[0].type(), nullptr);

  ASSERT_FALSE(input_arguments[1].is_lambda());
  ASSERT_TRUE(input_arguments[1].is_literal());
  EXPECT_TRUE(input_arguments[1].type()->IsInt64());
}

TEST_F(InputArgumentTypeResolverHelperTest, LambdaInputArgumentTypeWithNull) {
  ASTLambda astLambda;
  std::vector<const ASTNode*> arg_ast_nodes;
  arg_ast_nodes.push_back(&astLambda);
  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.push_back(nullptr);

  ASTIntLiteral int_literal;
  arg_ast_nodes.push_back(&int_literal);
  ASTNullLiteral null_literal;
  arg_ast_nodes.push_back(&null_literal);
  arguments.push_back(MakeResolvedLiteral(Value::Int64(1)));
  arguments.push_back(MakeResolvedLiteral(Value::NullFloat()));

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::vector<InputArgumentType> input_arguments,
        GetInputArgumentTypesForGenericArgumentList(
            arg_ast_nodes, arguments,
            /*pick_default_type_for_untyped_expr=*/false, analyzer_options_));
    ASSERT_EQ(input_arguments.size(), 3);
    ASSERT_TRUE(input_arguments[0].is_lambda());
    ASSERT_EQ(input_arguments[0].type(), nullptr);

    ASSERT_FALSE(input_arguments[1].is_lambda());
    ASSERT_TRUE(input_arguments[1].is_literal());
    EXPECT_TRUE(input_arguments[1].type()->IsInt64());

    ASSERT_FALSE(input_arguments[2].is_lambda());
    ASSERT_TRUE(input_arguments[2].is_untyped_null());
    EXPECT_TRUE(input_arguments[2].type()->IsInt64());
  }

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::vector<InputArgumentType> input_arguments,
        GetInputArgumentTypesForGenericArgumentList(
            arg_ast_nodes, arguments,
            /*pick_default_type_for_untyped_expr=*/true, analyzer_options_));
    ASSERT_EQ(input_arguments.size(), 3);
    ASSERT_TRUE(input_arguments[0].is_lambda());
    ASSERT_EQ(input_arguments[0].type(), nullptr);

    ASSERT_FALSE(input_arguments[1].is_lambda());
    ASSERT_TRUE(input_arguments[1].is_literal());
    EXPECT_TRUE(input_arguments[1].type()->IsInt64());

    ASSERT_FALSE(input_arguments[2].is_lambda());
    ASSERT_FALSE(input_arguments[2].is_untyped_null());
    ASSERT_TRUE(input_arguments[2].is_literal());
    EXPECT_TRUE(input_arguments[2].type()->IsFloat());
  }
}

TEST_F(InputArgumentTypeResolverHelperTest, ConstantInputArgumentType) {
  ASTIdentifier astIdentifier;
  std::vector<const ASTNode*> arg_ast_nodes;
  arg_ast_nodes.push_back(&astIdentifier);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedCreateConstantStmt> stmt,
                       ResolvedCreateConstantStmtBuilder()
                           .set_name_path({"foo"})
                           .set_expr(MakeResolvedLiteral(Value::Int64(1)))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SQLConstant> sql_constant,
                       MakeConstantFromCreateConstant(*stmt));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedConstant> constant,
                       ResolvedConstantBuilder()
                           .set_constant(sql_constant.get())
                           .set_type(types::Int64Type())
                           .Build());

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.push_back(std::move(constant));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<InputArgumentType> input_arguments,
      GetInputArgumentTypesForGenericArgumentList(
          arg_ast_nodes, arguments,
          /*pick_default_type_for_untyped_expr=*/false, analyzer_options_));
  EXPECT_EQ(input_arguments.size(), 1);
  EXPECT_EQ(input_arguments[0].type(), types::Int64Type());
  EXPECT_TRUE(input_arguments[0].is_analysis_time_constant());
  EXPECT_THAT(input_arguments[0].GetAnalysisTimeConstantValue(),
              absl_testing::IsOkAndHolds(Value::Int64(1)));
}

TEST_F(InputArgumentTypeResolverHelperTest,
       UninitializedConstantInputArgumentTypeLanguageFeatureDisabled) {
  analyzer_options_.mutable_language()->DisableLanguageFeature(
      FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT);
  ASTIdentifier identifier;
  std::vector<const ASTNode*> arg_ast_nodes;
  arg_ast_nodes.push_back(&identifier);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedCreateConstantStmt> stmt,
                       ResolvedCreateConstantStmtBuilder()
                           .set_name_path({"foo"})
                           .set_expr(MakeResolvedLiteral(Value::Int64(1)))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SQLConstant> uninitialized_constant,
                       MakeConstantFromCreateConstant(*stmt));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedConstant> uninitialized_resolved_constant,
      ResolvedConstantBuilder()
          .set_constant(uninitialized_constant.get())
          .set_type(types::Int64Type())
          .Build());

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.push_back(std::move(uninitialized_resolved_constant));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<InputArgumentType> input_arguments,
      GetInputArgumentTypesForGenericArgumentList(
          arg_ast_nodes, arguments,
          /*pick_default_type_for_untyped_expr=*/false, analyzer_options_));
  EXPECT_EQ(input_arguments.size(), 1);
  EXPECT_EQ(input_arguments[0].type(), types::Int64Type());

  // When FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT is disabled and the
  // created constant is not initialized, InputArgumentType object is not
  // considered as an analysis time constant.
  EXPECT_FALSE(input_arguments[0].is_analysis_time_constant());
}

TEST_F(InputArgumentTypeResolverHelperTest,
       InitializedConstantInputArgumentTypeLanguageFeatureDisabled) {
  analyzer_options_.mutable_language()->DisableLanguageFeature(
      FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT);
  ASTIdentifier identifier;
  std::vector<const ASTNode*> arg_ast_nodes;
  arg_ast_nodes.push_back(&identifier);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedCreateConstantStmt> stmt,
                       ResolvedCreateConstantStmtBuilder()
                           .set_name_path({"foo"})
                           .set_expr(MakeResolvedLiteral(Value::Int64(1)))
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SQLConstant> constant,
                       MakeConstantFromCreateConstant(*stmt));
  GOOGLESQL_ASSERT_OK(constant->SetEvaluationResult(Value::Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedConstant> initialized_resolved_constant,
      ResolvedConstantBuilder()
          .set_constant(constant.get())
          .set_type(types::Int64Type())
          .Build());

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.push_back(std::move(initialized_resolved_constant));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<InputArgumentType> input_arguments,
      GetInputArgumentTypesForGenericArgumentList(
          arg_ast_nodes, arguments,
          /*pick_default_type_for_untyped_expr=*/false, analyzer_options_));
  EXPECT_EQ(input_arguments.size(), 1);
  EXPECT_EQ(input_arguments[0].type(), types::Int64Type());

  // When FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT is disabled and the
  // created constant is initialized, InputArgumentType object is NOT
  // considered as an analysis time constant.
  EXPECT_FALSE(input_arguments[0].is_analysis_time_constant());
}

}  // namespace googlesql
