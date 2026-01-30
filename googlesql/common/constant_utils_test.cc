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

#include "googlesql/common/constant_utils.h"

#include <memory>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/testdata/sample_catalog.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class IsConstantTest : public ::testing::Test {
 protected:
  IsConstantTest() {
    options_.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
    options_.mutable_language()->SetSupportedStatementKinds(
        {googlesql::RESOLVED_CREATE_FUNCTION_STMT,
         googlesql::RESOLVED_CREATE_TABLE_FUNCTION_STMT,
         googlesql::RESOLVED_QUERY_STMT});
    GOOGLESQL_CHECK_OK(
        options_.AddQueryParameter("param_int64", type_factory_.get_int64()));
    GOOGLESQL_CHECK_OK(options_.AddSystemVariable({"sysvar_int64"},
                                        type_factory_.get_int64()));
    GOOGLESQL_CHECK_OK(type_factory_.MakeStructType(
        {{"struct_int64", type_factory_.get_int64()}}, &struct_type_int64_));
    GOOGLESQL_CHECK_OK(
        options_.AddQueryParameter("param_struct_int64", struct_type_int64_));
    GOOGLESQL_CHECK_OK(type_factory_.MakeStructType({{"a", type_factory_.get_int64()}},
                                          &struct_type_a_));
    GOOGLESQL_CHECK_OK(type_factory_.MakeProtoType(
        googlesql_test::KitchenSinkPB::descriptor(), &proto_type_ks_));
    GOOGLESQL_CHECK_OK(options_.AddQueryParameter("param_proto_ks", proto_type_ks_));
    json_type_ = type_factory_.get_json();
    GOOGLESQL_CHECK_OK(options_.AddQueryParameter("param_json", json_type_));

    catalog_.catalog()->AddOwnedFunction(
        new Function("StableFunc", "Test", Function::SCALAR,
                     {{type_factory_.get_int64(), {}, 1}},
                     FunctionOptions().set_volatility(FunctionEnums::STABLE)));
    catalog_.catalog()->AddOwnedFunction(new Function(
        "StableFuncWithArg", "Test", Function::SCALAR,
        {{type_factory_.get_int64(), {type_factory_.get_int64()}, 1}},
        FunctionOptions().set_volatility(FunctionEnums::STABLE)));
    catalog_.catalog()->AddOwnedFunction(new Function(
        "ImmutableFuncWithArg", "Test", Function::SCALAR,
        {{type_factory_.get_int64(), {type_factory_.get_int64()}, 1}},
        FunctionOptions().set_volatility(FunctionEnums::IMMUTABLE)));
  }

  absl::Status AnalyzeExpr(absl::string_view sql) {
    return AnalyzeExpression(sql, options_, catalog_.catalog(), &type_factory_,
                             &output_);
  }

  const ResolvedExpr* result_expr() { return output_->resolved_expr(); }

  absl::Status AnalyzeStmt(absl::string_view sql) {
    return AnalyzeStatement(sql, options_, catalog_.catalog(), &type_factory_,
                            &output_);
  }

  const ResolvedStatement* result_stmt() {
    return output_->resolved_statement();
  }

  std::unique_ptr<const AnalyzerOutput> output_;
  TypeFactory type_factory_;
  SampleCatalog catalog_;
  AnalyzerOptions options_;
  const Type* struct_type_int64_ = nullptr;
  const Type* struct_type_a_ = nullptr;
  const Type* proto_type_ks_ = nullptr;
  const Type* json_type_ = nullptr;
};

TEST_F(IsConstantTest, IsAnalysisConst) {
  GOOGLESQL_ASSERT_OK(AnalyzeExpr("10000"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("ARRAY<STRING>['a', 'b']"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("10000 + 2 * 10"));
  EXPECT_FALSE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CAST(10000 AS STRING)"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("STRUCT(1 AS key, 2 AS value)"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("STRUCT(1 AS a).a"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr(
      "NEW `googlesql_test.KeyValueStruct`('1' as key, 1 as value)"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr(
      "STRUCT(NEW `googlesql_test.KeyValueStruct`('1' as key, 1 as value))"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(
      AnalyzeExpr("STRUCT('1' as key, ARRAY_CONCAT([1, 2], [3, 4]) as value)"));
  EXPECT_FALSE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr(
      "NEW googlesql_test.KitchenSinkPB(1 AS int64_key_1, 2 AS int64_key_2, 3 "
      "AS int64_val).int64_val"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("JSON '{\"a\":1}'.a"));
  EXPECT_TRUE(IsAnalysisConstant(result_expr()));
}

TEST_F(IsConstantTest, IsImmutableConst) {
  GOOGLESQL_ASSERT_OK(AnalyzeExpr("1"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CAST(1 AS INT64)"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CONCAT('a', 'b')"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("STRUCT(1, 2)"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("[1, 2, 3]"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("RAND()"));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CURRENT_TIMESTAMP()"));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("10000 + 2 * 10"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("ARRAY_CONCAT([1], [2])"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  // We set value as an arithmetic expression so that it isn't folded during
  // analysis.
  GOOGLESQL_ASSERT_OK(AnalyzeExpr(
      "NEW `googlesql_test.KeyValueStruct`('1' as key, 1 + 0 as value)"));
  EXPECT_FALSE(IsAnalysisConstant(result_expr()));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(
      AnalyzeExpr("STRUCT('1' as key, ARRAY_CONCAT([1, 2], [3, 4]) as value)"));
  EXPECT_FALSE(IsAnalysisConstant(result_expr()));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT 1)"));
  EXPECT_FALSE(IsAnalysisConstant(result_expr()));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT CONCAT('a', 'b'))"));
  EXPECT_FALSE(IsAnalysisConstant(result_expr()));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT RAND())"));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT CURRENT_TIMESTAMP())"));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CAST(CONCAT('a', 'b') AS BYTES)"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("ImmutableFuncWithArg(1)"));
  EXPECT_TRUE(IsImmutableConstant(result_expr()));
}

TEST_F(IsConstantTest, IsStableConst) {
  GOOGLESQL_ASSERT_OK(AnalyzeExpr("1"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CAST(1 AS INT64)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CONCAT('a', 'b')"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("STRUCT(1, 2)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("RAND()"));
  EXPECT_FALSE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CURRENT_TIMESTAMP()"));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CURRENT_DATE()"));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  // We set value as an arithmetic expression so that it isn't folded during
  // analysis.
  GOOGLESQL_ASSERT_OK(AnalyzeExpr(
      "NEW `googlesql_test.KeyValueStruct`('1' as key, 1 + 0 as value)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(
      AnalyzeExpr("STRUCT('1' as key, ARRAY_CONCAT([1, 2], [3, 4]) as value)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT 1)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT CONCAT('a', 'b'))"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT RAND())"));
  EXPECT_FALSE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT CURRENT_TIMESTAMP())"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT CURRENT_DATE())"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@param_int64"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@@sysvar_int64"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("1 + @param_int64"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("2 * @@sysvar_int64"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT @param_int64)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("(SELECT @@sysvar_int64)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(
      AnalyzeExpr("(SELECT TIMESTAMP_ADD(CURRENT_TIMESTAMP(), INTERVAL "
                  "@param_int64 HOUR))"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@param_struct_int64.struct_int64"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@param_proto_ks.int64_val"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CAST(@param_int64 AS STRING)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@param_struct_int64.struct_int64 + 1"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CONCAT(CAST(@param_int64 AS STRING), 'test')"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@param_json.name"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("StableFunc()"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("StableFuncWithArg(1)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("ImmutableFuncWithArg(@param_int64)"));
  EXPECT_TRUE(IsStableConstant(result_expr()));
  EXPECT_FALSE(IsImmutableConstant(result_expr()));
}

TEST_F(IsConstantTest, IsQueryConst) {
  GOOGLESQL_ASSERT_OK(AnalyzeExpr("1"));
  EXPECT_TRUE(IsQueryConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("1+1"));
  EXPECT_TRUE(IsQueryConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CONCAT('a', 'b')"));
  EXPECT_TRUE(IsQueryConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("CURRENT_TIMESTAMP()"));
  EXPECT_TRUE(IsQueryConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("RAND()"));
  EXPECT_FALSE(IsQueryConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@param_int64"));
  EXPECT_TRUE(IsQueryConstant(result_expr()));

  GOOGLESQL_ASSERT_OK(AnalyzeExpr("@@sysvar_int64"));
  EXPECT_TRUE(IsQueryConstant(result_expr()));
}

TEST_F(IsConstantTest, IsQueryConstant_ArgumentRefInUDF) {
  GOOGLESQL_ASSERT_OK(AnalyzeStmt("CREATE FUNCTION Func(arg INT64) AS (arg)"));
  const ResolvedStatement* arg_ref_stmt = result_stmt();
  ASSERT_TRUE(arg_ref_stmt->Is<ResolvedCreateFunctionStmt>());
  const auto* func_body =
      arg_ref_stmt->GetAs<ResolvedCreateFunctionStmt>()->function_expression();
  ASSERT_TRUE(func_body != nullptr);
  ASSERT_EQ(func_body->node_kind(), RESOLVED_ARGUMENT_REF);
  EXPECT_FALSE(IsStableConstant(func_body));
  EXPECT_TRUE(IsQueryConstant(func_body));
}

TEST_F(IsConstantTest, IsQueryConstant_ArgumentRefInTVF) {
  GOOGLESQL_ASSERT_OK(AnalyzeStmt("CREATE TABLE FUNCTION Func(arg INT64) AS SELECT arg"));
  const ResolvedStatement* arg_ref_stmt = result_stmt();
  ASSERT_TRUE(arg_ref_stmt->Is<ResolvedCreateTableFunctionStmt>());
  std::vector<const ResolvedNode*> arg_refs;
  arg_ref_stmt->GetDescendantsWithKinds({RESOLVED_ARGUMENT_REF}, &arg_refs);
  ASSERT_EQ(arg_refs.size(), 1);
  EXPECT_FALSE(IsStableConstant(arg_refs[0]->GetAs<ResolvedExpr>()));
  EXPECT_TRUE(IsQueryConstant(arg_refs[0]->GetAs<ResolvedExpr>()));
}

TEST_F(IsConstantTest, IsQueryConstant_ArgumentRefInUDA) {
  GOOGLESQL_ASSERT_OK(
      AnalyzeStmt("CREATE AGGREGATE FUNCTION Func(arg INT64) AS (SUM(arg))"));
  const ResolvedStatement* arg_ref_stmt = result_stmt();
  ASSERT_TRUE(
      arg_ref_stmt->Is<ResolvedCreateFunctionStmt>() &&
      arg_ref_stmt->GetAs<ResolvedCreateFunctionStmt>()->is_aggregate());
  std::vector<const ResolvedNode*> arg_refs;
  arg_ref_stmt->GetDescendantsWithKinds({RESOLVED_ARGUMENT_REF}, &arg_refs);
  ASSERT_EQ(arg_refs.size(), 1);
  EXPECT_FALSE(IsStableConstant(arg_refs[0]->GetAs<ResolvedExpr>()));
  EXPECT_TRUE(IsQueryConstant(arg_refs[0]->GetAs<ResolvedExpr>()));
}

TEST_F(IsConstantTest, IsQueryConstant_CorrelatedColumnRef) {
  GOOGLESQL_ASSERT_OK(
      AnalyzeStmt("SELECT (SELECT t.col + 1) "
                  "FROM (SELECT 1 AS col) AS t"));

  std::vector<const ResolvedNode*> column_refs;
  result_stmt()->GetDescendantsWithKinds({RESOLVED_COLUMN_REF}, &column_refs);

  const ResolvedColumnRef* correlated_ref = nullptr;
  for (const auto* node : column_refs) {
    const auto* ref = node->GetAs<ResolvedColumnRef>();
    if (ref->is_correlated()) {  // Focus test on the correlated column ref.
      ASSERT_TRUE(correlated_ref == nullptr);  // There can only be one.
      correlated_ref = ref;
    }
  }
  ASSERT_TRUE(correlated_ref != nullptr);
  ASSERT_EQ(correlated_ref->column().name(), "col");
  EXPECT_FALSE(IsStableConstant(correlated_ref));
  EXPECT_TRUE(IsQueryConstant(correlated_ref));
}

TEST_F(IsConstantTest, IsQueryConstant_NonCorrelatedColumnRef) {
  GOOGLESQL_ASSERT_OK(
      AnalyzeStmt("SELECT t.col + 1 "
                  "FROM (SELECT 1 AS col) AS t"));

  std::vector<const ResolvedNode*> column_refs;
  result_stmt()->GetDescendantsWithKinds({RESOLVED_COLUMN_REF}, &column_refs);

  ASSERT_EQ(column_refs.size(), 1);
  // t.col is the non-correlated column reference we are testing against.
  const auto* ref = column_refs[0]->GetAs<ResolvedColumnRef>();

  ASSERT_EQ(ref->column().name(), "col");
  EXPECT_FALSE(ref->is_correlated());
  EXPECT_FALSE(IsStableConstant(ref));
  EXPECT_FALSE(IsQueryConstant(ref));
}

TEST_F(IsConstantTest, IsQueryConstant_CustomFunctionPropagation_FunctionCall) {
  GOOGLESQL_ASSERT_OK(AnalyzeStmt("CREATE FUNCTION Func(arg INT64) AS (arg + 1)"));
  const ResolvedStatement* stmt = result_stmt();
  ASSERT_TRUE(stmt->Is<ResolvedCreateFunctionStmt>());
  const auto* func_body =
      stmt->GetAs<ResolvedCreateFunctionStmt>()->function_expression();
  ASSERT_TRUE(func_body != nullptr);
  EXPECT_FALSE(IsStableConstant(func_body));
  EXPECT_TRUE(IsQueryConstant(func_body));
}

TEST_F(IsConstantTest, IsQueryConstant_CustomFunctionPropagation_Cast) {
  GOOGLESQL_ASSERT_OK(
      AnalyzeStmt("CREATE FUNCTION Func(arg INT64) AS (CAST(arg AS STRING))"));
  const ResolvedStatement* stmt = result_stmt();
  ASSERT_TRUE(stmt->Is<ResolvedCreateFunctionStmt>());
  const auto* func_body =
      stmt->GetAs<ResolvedCreateFunctionStmt>()->function_expression();
  ASSERT_TRUE(func_body != nullptr);
  EXPECT_FALSE(IsStableConstant(func_body));
  EXPECT_TRUE(IsQueryConstant(func_body));
}

TEST_F(IsConstantTest, IsQueryConstant_CustomFunctionPropagation_FieldAccess) {
  GOOGLESQL_ASSERT_OK(
      AnalyzeStmt("CREATE FUNCTION Func(arg STRUCT<a INT64>) AS (arg.a)"));
  const ResolvedStatement* stmt = result_stmt();
  ASSERT_TRUE(stmt->Is<ResolvedCreateFunctionStmt>());
  const auto* func_body =
      stmt->GetAs<ResolvedCreateFunctionStmt>()->function_expression();
  ASSERT_TRUE(func_body != nullptr);
  EXPECT_FALSE(IsStableConstant(func_body));
  EXPECT_TRUE(IsQueryConstant(func_body));
}

TEST_F(IsConstantTest, IsQueryConstant_CustomFunctionPropagation_Json) {
  GOOGLESQL_ASSERT_OK(AnalyzeStmt("CREATE FUNCTION Func(arg JSON) AS (arg.key)"));
  const ResolvedStatement* stmt = result_stmt();
  ASSERT_TRUE(stmt->Is<ResolvedCreateFunctionStmt>());
  const auto* func_body =
      stmt->GetAs<ResolvedCreateFunctionStmt>()->function_expression();
  ASSERT_TRUE(func_body != nullptr);
  EXPECT_FALSE(IsStableConstant(func_body));
  EXPECT_TRUE(IsQueryConstant(func_body));
}

TEST_F(IsConstantTest, IsQueryConstant_CustomFunctionPropagation_Subquery) {
  GOOGLESQL_ASSERT_OK(AnalyzeStmt("CREATE FUNCTION Func(arg INT64) AS ((SELECT arg))"));
  const ResolvedStatement* stmt = result_stmt();
  ASSERT_TRUE(stmt->Is<ResolvedCreateFunctionStmt>());
  const auto* func_body =
      stmt->GetAs<ResolvedCreateFunctionStmt>()->function_expression();
  ASSERT_TRUE(func_body != nullptr);
  EXPECT_FALSE(IsStableConstant(func_body));
  EXPECT_TRUE(IsQueryConstant(func_body));
}

// A correlated subquery expression must be re-evaluated for each row of the
// outer query, so it is not constant in the outer query's scope, even if the
// correlated column reference itself is QUERY_CONST within the subquery.
TEST_F(IsConstantTest, IsQueryConstant_SubqueryExprWithCorrelatedColumnRef) {
  GOOGLESQL_ASSERT_OK(
      AnalyzeStmt("SELECT (SELECT t.col) "
                  "FROM (SELECT 1 AS col) AS t"));

  std::vector<const ResolvedNode*> subquery_exprs;
  result_stmt()->GetDescendantsWithKinds({RESOLVED_SUBQUERY_EXPR},
                                         &subquery_exprs);
  ASSERT_GE(subquery_exprs.size(), 1);
  const ResolvedExpr* subquery_expr_with_corr_ref = nullptr;
  for (const auto* node : subquery_exprs) {
    std::vector<const ResolvedNode*> columns_refs;
    node->GetDescendantsWithKinds({RESOLVED_COLUMN_REF}, &columns_refs);
    for (const auto* col_node : columns_refs) {
      if (col_node->GetAs<ResolvedColumnRef>()->is_correlated()) {
        subquery_expr_with_corr_ref = node->GetAs<ResolvedExpr>();
        break;
      }
    }
    if (subquery_expr_with_corr_ref != nullptr) break;
  }
  ASSERT_NE(subquery_expr_with_corr_ref, nullptr);
  EXPECT_FALSE(IsQueryConstant(subquery_expr_with_corr_ref));
  EXPECT_FALSE(IsStableConstant(subquery_expr_with_corr_ref));
}

TEST_F(IsConstantTest, StatementInput) {
  GOOGLESQL_ASSERT_OK(AnalyzeStatement("SELECT 1", options_, catalog_.catalog(),
                             &type_factory_, &output_));
  EXPECT_FALSE(IsAnalysisConstant(output_->resolved_statement()));
  EXPECT_FALSE(IsImmutableConstant(output_->resolved_statement()));
  EXPECT_FALSE(IsStableConstant(output_->resolved_statement()));
  EXPECT_FALSE(IsQueryConstant(output_->resolved_statement()));
}

}  // namespace googlesql
