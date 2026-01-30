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

#include "googlesql/analyzer/resolver.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/testdata/error_catalog.h"
#include "googlesql/testdata/sample_catalog.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using testing::_;
using testing::HasSubstr;
using testing::IsNull;
using testing::NotNull;
using absl_testing::StatusIs;

class ResolverTest : public ::testing::Test {
 public:
  // Used by benchmarks, this wrapper is to access a private member in Resolver.
  static absl::Status ResolveScalarExpr(
      Resolver* resolver,
      const ASTExpression* ast_expr,
      const NameScope* name_scope,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
    return resolver->ResolveScalarExpr(ast_expr, name_scope,
                                       "ResolveScalarExpr", resolved_expr_out);
  }

 protected:
  IdStringPool id_string_pool_;

  ResolverTest() = default;
  ResolverTest(const ResolverTest&) = delete;
  ResolverTest& operator=(const ResolverTest&) = delete;
  ~ResolverTest() override = default;

  void SetUp() override {
    InitializeQueryParameters();
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
    analyzer_options_.mutable_language()->EnableMaximumLanguageFeatures();
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_ANONYMIZATION);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_AGGREGATION_THRESHOLD);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_INTERVAL_TYPE);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_PARAMETERIZED_TYPES);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_RANGE_TYPE);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_UUID_TYPE);
    analyzer_options_.CreateDefaultArenasIfNotSet();
    sample_catalog_ = std::make_unique<SampleCatalog>(
        analyzer_options_.language(), &type_factory_);
    resolver_ = std::make_unique<Resolver>(sample_catalog_->catalog(),
                                           &type_factory_, &analyzer_options_);
    // Initialize the resolver state, which is necessary because the tests do
    // not necessarily call the public methods that call Reset().
    resolver_->Reset("" /* sql */);
  }

  void TearDown() override {}

  // Resets 'resolver_' with a new Catalog.  Does not take ownership of
  // 'catalog'.
  void ResetResolver(Catalog* catalog) {
    resolver_ =
        std::make_unique<Resolver>(catalog, &type_factory_, &analyzer_options_);
    resolver_->Reset("" /* sql */);
  }

  void ResolveSimpleTypeName(absl::string_view name,
                             const Type* expected_type) {
    const Type* type;
    GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName(name, &type)) << name;
    EXPECT_THAT(type, NotNull()) << name;
    EXPECT_TRUE(type->Equals(expected_type))
        << "type: " << type->DebugString()
        << "expected type: " << expected_type->DebugString();

    // Round trip it and re-resolve.
    const Type* round_trip_type;
    std::string round_trip_name = type->DebugString();
    GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName(round_trip_name, &round_trip_type))
        << round_trip_name;
    EXPECT_THAT(round_trip_type, NotNull()) << round_trip_name;
    EXPECT_TRUE(round_trip_type->Equals(type))
        << "type: " << type->DebugString()
        << "round trip type: " << round_trip_type->DebugString();
  }

  absl::Status ResolveExpr(
      const ASTExpression* expression,
      std::unique_ptr<const ResolvedExpr>* resolved_expression,
      bool aggregation_allowed = false) {
    if (aggregation_allowed) {
      QueryResolutionInfo query_resolution_info(resolver_.get());
      ExprResolutionInfo with_aggregation(
          &query_resolution_info, name_scope_.get(),
          {.allows_aggregation = aggregation_allowed,
           .allows_analytic = false});
      return resolver_->ResolveExpr(expression, &with_aggregation,
                                    resolved_expression);
    }
    return resolver_->ResolveScalarExpr(expression, name_scope_.get(),
                                        "ResolveScalarExpr",
                                        resolved_expression);
  }

  absl::Status FindFieldDescriptors(
      absl::Span<const ASTIdentifier* const> path_vector,
      const ProtoType* root_type,
      std::vector<const google::protobuf::FieldDescriptor*>* field_descriptors) {
    return resolver_->FindFieldDescriptors(path_vector, root_type,
                                           field_descriptors);
  }

  // Test that <cast_expression> successfully resolves to an expression
  // of the expected type.
  void TestCastExpression(absl::string_view cast_expression,
                          const Type* expected_cast_type) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(cast_expression, ParserOptions(), &parser_output))
        << cast_expression;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull()) << cast_expression;
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expression))
        << cast_expression;
    ASSERT_THAT(resolved_expression.get(), NotNull()) << cast_expression;
    EXPECT_TRUE(expected_cast_type->Equals(resolved_expression->type()))
        << cast_expression;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveAndCoerce(
      const std::string& query, const Type* target_type,
      Resolver::CoercionMode mode, absl::string_view error_template) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_CHECK_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    GOOGLESQL_CHECK_OK(ResolveExpr(parsed_expression, &resolved_expression))
        << "Query: " << query
        << "\nParsed/Unparsed expression: " << Unparse(parsed_expression);
    if (error_template.empty()) {
      GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
          parsed_expression, target_type, mode, &resolved_expression));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(
          resolver_->CoerceExprToType(parsed_expression, target_type, mode,
                                      error_template, &resolved_expression));
    }
    return resolved_expression;
  }

  void TestCaseExpression(absl::string_view query,
                          absl::string_view expected_case_function_name) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    if (parsed_expression->node_kind() == AST_CASE_VALUE_EXPRESSION) {
      const auto& arguments =
          parsed_expression->GetAsOrDie<ASTCaseValueExpression>()->arguments();
      EXPECT_GE(arguments.size(), 3) << query;
    }
    if (parsed_expression->node_kind() == AST_CASE_NO_VALUE_EXPRESSION) {
      const auto& arguments =
          parsed_expression->GetAsOrDie<ASTCaseNoValueExpression>()
              ->arguments();
      EXPECT_GE(arguments.size(), 2) << query;
    }
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expression))
        << "Query: " << query
        << "\nParsed/Unparsed expression: " << Unparse(parsed_expression);
    EXPECT_THAT(resolved_expression.get(), NotNull());
    const ResolvedFunctionCall* resolved_function_call =
        resolved_expression->GetAs<ResolvedFunctionCall>();
    ASSERT_THAT(resolved_function_call, NotNull());
    EXPECT_EQ(expected_case_function_name,
              resolved_function_call->function()->FullName());
  }

  void ParseAndResolveFunction(absl::string_view query,
                               absl::string_view expected_function_name,
                               bool is_aggregate_function = false,
                               bool aggregation_allowed = false) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expression,
                          aggregation_allowed))
        << "Query: " << query
        << "\nParsed expression: " << Unparse(parsed_expression);
    ASSERT_THAT(resolved_expression.get(), NotNull())
        << "Query: " << query
        << "\nParsed expression: " << Unparse(parsed_expression);
    if (is_aggregate_function) {
      // Aggregate functions resolve to a column reference.
      ASSERT_TRUE(resolved_expression->node_kind() == RESOLVED_COLUMN_REF)
          << resolved_expression->DebugString();
      const ResolvedColumnRef* resolved_column_ref =
          resolved_expression->GetAs<ResolvedColumnRef>();
      ASSERT_THAT(resolved_column_ref, NotNull());
      // TODO: Need to have a better check that we got what we
      // expected, in particular we should somewhere find the
      // <expected_function_name>.
      EXPECT_EQ("$agg1", resolved_column_ref->column().name());
    } else {
      ASSERT_TRUE(resolved_expression->node_kind() == RESOLVED_FUNCTION_CALL)
          << resolved_expression->DebugString();
      const ResolvedFunctionCall* resolved_function_call =
          resolved_expression->GetAs<ResolvedFunctionCall>();
      ASSERT_THAT(resolved_function_call, NotNull());
      EXPECT_EQ(expected_function_name,
              resolved_function_call->function()->FullName());
    }
  }

  void ResolveFunctionFails(absl::string_view query,
                            absl::string_view expected_error_substr,
                            bool aggregation_allowed = false) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    // Parsing should succeed.
    GOOGLESQL_ASSERT_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    EXPECT_THAT(ResolveExpr(parsed_expression, &resolved_expression,
                            aggregation_allowed),
                StatusIs(_, HasSubstr(expected_error_substr)))
        << "Query: " << query
        << "\nParsed expression: " << Unparse(parsed_expression);
    EXPECT_THAT(resolved_expression.get(), IsNull())
        << resolved_expression->DebugString();
  }

  void ParseFunctionFails(absl::string_view query,
                          absl::string_view expected_error_substr) {
    std::unique_ptr<ParserOutput> parser_output;

    // Parsing should fail.
    EXPECT_THAT(ParseExpression(query, ParserOptions(), &parser_output),
                StatusIs(_, HasSubstr(expected_error_substr)))
        << "Query: " << query;
  }

  void TestResolveParameterExprSuccess(absl::string_view query,
                                       const Type* expected_type) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    // Parsing should succeed.
    GOOGLESQL_ASSERT_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expr))
        << "Query: " << query
        << "\nParsed expression: " << Unparse(parsed_expression);
    EXPECT_THAT(resolved_expr.get(), NotNull());
    EXPECT_TRUE(resolved_expr->type()->Equals(expected_type));
  }

  void TestResolveParameterExprFails(absl::string_view query,
                                     absl::string_view expected_error_substr) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    // Parsing should succeed.
    GOOGLESQL_ASSERT_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    EXPECT_THAT(ResolveExpr(parsed_expression, &resolved_expr),
                StatusIs(_, HasSubstr(expected_error_substr)))
        << "Query: " << query
        << "\nParsed expression: " << Unparse(parsed_expression);
    EXPECT_THAT(resolved_expr.get(), IsNull());
  }

  // 'path_expression' must parse to a ASTPathExpression or ASTIdentifier.
  void TestFindFieldDescriptorsSuccess(absl::string_view path_expression,
                                       const ProtoType* root_type) {
    std::unique_ptr<ParserOutput> parser_output;
    GOOGLESQL_ASSERT_OK(ParseExpression(path_expression, ParserOptions(), &parser_output))
        << path_expression;
    const ASTExpression* parsed_expression = parser_output->expression();
    EXPECT_TRUE(parsed_expression->node_kind() == AST_IDENTIFIER ||
                parsed_expression->node_kind() == AST_PATH_EXPRESSION);
    absl::Span<const ASTIdentifier* const> path_vector;
    // Separate vector to avoid lifetime issues due to use-after-free.
    std::vector<const ASTIdentifier*> path_sequence;
    if (parsed_expression->node_kind() == AST_IDENTIFIER) {
      path_sequence.push_back(parsed_expression->GetAsOrDie<ASTIdentifier>());
      path_vector = path_sequence;
    } else {
      path_vector = parsed_expression->GetAsOrDie<ASTPathExpression>()->names();
    }
    std::vector<const google::protobuf::FieldDescriptor*> field_descriptors;
    GOOGLESQL_ASSERT_OK(FindFieldDescriptors(path_vector, root_type, &field_descriptors));

    // Ensure that the field path is valid by checking field containment.
    absl::string_view containing_proto_name =
        root_type->descriptor()->full_name();
    EXPECT_EQ(path_vector.size(), field_descriptors.size());
    for (int i = 0; i < field_descriptors.size(); ++i) {
      if (!field_descriptors[i]->is_extension()) {
        EXPECT_EQ(path_vector[i]->GetAsString(), field_descriptors[i]->name());
      }
      EXPECT_EQ(containing_proto_name,
                field_descriptors[i]->containing_type()->full_name())
          << "Mismatched proto message " << containing_proto_name
          << " and field " << field_descriptors[i]->full_name();
      if (field_descriptors[i]->message_type() != nullptr) {
        containing_proto_name =
            field_descriptors[i]->message_type()->full_name();
      }
    }
  }

  // 'path_expression' must parse to a ASTPathExpression or ASTIdentifier.
  void TestFindFieldDescriptorsFail(absl::string_view path_expression,
                                    const ProtoType* root_type,
                                    absl::string_view expected_error_substr) {
    std::unique_ptr<ParserOutput> parser_output;
    GOOGLESQL_ASSERT_OK(ParseExpression(path_expression, ParserOptions(), &parser_output))
        << path_expression;
    const ASTExpression* parsed_expression = parser_output->expression();
    EXPECT_TRUE(parsed_expression->node_kind() == AST_IDENTIFIER ||
                parsed_expression->node_kind() == AST_PATH_EXPRESSION);
    absl::Span<const ASTIdentifier* const> path_vector;
    // Separate vector to avoid lifetime issues due to use-after-free.
    std::vector<const ASTIdentifier*> path_sequence;
    if (parsed_expression->node_kind() == AST_IDENTIFIER) {
      path_sequence.push_back(parsed_expression->GetAsOrDie<ASTIdentifier>());
      path_vector = path_sequence;
    } else {
      path_vector = parsed_expression->GetAsOrDie<ASTPathExpression>()->names();
    }
    std::vector<const google::protobuf::FieldDescriptor*> field_descriptors;
    EXPECT_THAT(
        FindFieldDescriptors(path_vector, root_type, &field_descriptors),
        StatusIs(_, HasSubstr(expected_error_substr)));
  }

  void TestResolverOK(absl::string_view query) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    // Parsing should succeed.
    GOOGLESQL_ASSERT_OK(ParseExpression(query, ParserOptions(), &parser_output)) << query;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expression));
    EXPECT_THAT(resolved_expression.get(), NotNull())
        << resolved_expression->DebugString();
  }

  void InitializeQueryParameters() {
    GOOGLESQL_ASSERT_OK(analyzer_options_.AddQueryParameter(
        "param_BOOL", type_factory_.get_bool()));
    GOOGLESQL_ASSERT_OK(analyzer_options_.AddQueryParameter(
        "param_BYTES", type_factory_.get_bytes()));
    GOOGLESQL_ASSERT_OK(analyzer_options_.AddQueryParameter(
        "param_INT32", type_factory_.get_int32()));
    GOOGLESQL_ASSERT_OK(analyzer_options_.AddQueryParameter(
        "PARAM_string", type_factory_.get_string()));
    GOOGLESQL_ASSERT_OK(analyzer_options_.AddQueryParameter(
        "pArAm_mIxEdcaSe", type_factory_.get_string()));
  }

  void TestIntervalLiteral(absl::string_view expected,
                           absl::string_view input) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(input, ParserOptions(), &parser_output)) << input;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    EXPECT_EQ(parsed_expression->node_kind(), AST_INTERVAL_EXPR);
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expression))
        << "Input: " << input
        << "\nParsed/Unparsed expression: " << Unparse(parsed_expression);
    EXPECT_THAT(resolved_expression.get(), NotNull());
    const ResolvedLiteral* resolved_literal =
        resolved_expression->GetAs<ResolvedLiteral>();
    ASSERT_THAT(resolved_literal, NotNull());
    EXPECT_TRUE(resolved_literal->value().type()->IsInterval());
    EXPECT_EQ(expected, resolved_literal->value().DebugString());
  }

  void TestIntervalLiteralError(absl::string_view input) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(input, ParserOptions(), &parser_output)) << input;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull()) << input;
    EXPECT_EQ(parsed_expression->node_kind(), AST_INTERVAL_EXPR) << input;
    EXPECT_THAT(ResolveExpr(parsed_expression, &resolved_expression),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << input;
  }

  void TestRangeLiteral(absl::string_view expected, absl::string_view input) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(input, ParserOptions(), &parser_output)) << input;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull());
    EXPECT_EQ(parsed_expression->node_kind(), AST_RANGE_LITERAL);
    GOOGLESQL_EXPECT_OK(ResolveExpr(parsed_expression, &resolved_expression))
        << "Input: " << input
        << "\nParsed/Unparsed expression: " << Unparse(parsed_expression);
    EXPECT_THAT(resolved_expression.get(), NotNull());
    const ResolvedLiteral* resolved_literal =
        resolved_expression->GetAs<ResolvedLiteral>();
    ASSERT_THAT(resolved_literal, NotNull());
    EXPECT_TRUE(resolved_literal->value().type()->IsRange());
    EXPECT_EQ(expected, resolved_literal->value().DebugString());
  }

  void TestRangeLiteralError(absl::string_view input) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(input, ParserOptions(), &parser_output)) << input;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull()) << input;
    EXPECT_EQ(parsed_expression->node_kind(), AST_RANGE_LITERAL) << input;
    EXPECT_THAT(ResolveExpr(parsed_expression, &resolved_expression),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << input;
  }

  void TestExpressionVolatility(absl::string_view input,
                                bool expected_volatile) {
    std::unique_ptr<ParserOutput> parser_output;
    std::unique_ptr<const ResolvedExpr> resolved_expression;
    GOOGLESQL_ASSERT_OK(ParseExpression(input, ParserOptions(), &parser_output)) << input;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_THAT(parsed_expression, NotNull()) << input;

    QueryResolutionInfo query_resolution_info(resolver_.get());
    ExprResolutionInfo expr_resolution_info(
        &query_resolution_info, name_scope_.get(),
        {.allows_aggregation = true, .allows_analytic = true});
    GOOGLESQL_ASSERT_OK(resolver_->ResolveExpr(parsed_expression, &expr_resolution_info,
                                     &resolved_expression));

    EXPECT_EQ(expr_resolution_info.findings.has_volatile, expected_volatile);
  }

  TypeFactory type_factory_;
  std::unique_ptr<SampleCatalog> sample_catalog_;
  AnalyzerOptions analyzer_options_;
  std::unique_ptr<Resolver> resolver_;
  std::unique_ptr<const NameScope> name_scope_;
  // Save the parser output so that memories are not freed when we use them in
  // resolver outputs.
  std::unique_ptr<ParserOutput> parser_output_;
};

TEST_F(ResolverTest, TestResolveTypeName) {
  ResolveSimpleTypeName("BOOL", type_factory_.get_bool());
  ResolveSimpleTypeName("int32", type_factory_.get_int32());
  ResolveSimpleTypeName("int64", type_factory_.get_int64());
  ResolveSimpleTypeName("uint32", type_factory_.get_uint32());
  ResolveSimpleTypeName("uint64", type_factory_.get_uint64());
  ResolveSimpleTypeName("float", type_factory_.get_float());
  ResolveSimpleTypeName("double", type_factory_.get_double());
  ResolveSimpleTypeName("string", type_factory_.get_string());
  ResolveSimpleTypeName("bytes", type_factory_.get_bytes());
  ResolveSimpleTypeName("date", type_factory_.get_date());
  ResolveSimpleTypeName("timestamp", type_factory_.get_timestamp());
  ResolveSimpleTypeName("uuid", type_factory_.get_uuid());

  const Type* type;
  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("ARRAY<INT32>", &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsArray()) << type->DebugString();

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("STRUCT<>", &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsStruct()) << type->DebugString();

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("STRUCT<A INT32, B ARRAY<STRING>>",
                                       &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsStruct()) << type->DebugString();

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName(
      "struct<a INT32, a INT64>",
      &type));
  EXPECT_EQ("STRUCT<a INT32, a INT64>", type->DebugString());

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName(
      "struct<INT32, x INT64, double>",
       &type));
  EXPECT_EQ("STRUCT<INT32, x INT64, DOUBLE>", type->DebugString());

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("RANGE<DATE>", &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsRange()) << type->DebugString();

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("`googlesql_test.KitchenSinkPB`",
                                       &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsProto()) << type->DebugString();

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("googlesql_test.KitchenSinkPB.Nested",
                                       &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsProto()) << type->DebugString();

  EXPECT_THAT(
      resolver_->ResolveTypeName("googlesql_test.KitchenSinkPBXXX", &type),
      StatusIs(_,
               HasSubstr("Type not found: googlesql_test.KitchenSinkPBXXX")));

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("`googlesql_test.TestEnum`",
                                       &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsEnum()) << type->DebugString();

  GOOGLESQL_EXPECT_OK(resolver_->ResolveTypeName("googlesql_test.TestEnum",
                                       &type));
  EXPECT_THAT(type, NotNull());
  EXPECT_TRUE(type->IsEnum()) << type->DebugString();

  EXPECT_THAT(
      resolver_->ResolveTypeName("`googlesql_TEST.TeSTeNum`", &type),
      StatusIs(_, HasSubstr("Type not found: `googlesql_TEST.TeSTeNum`")));
  EXPECT_EQ(type, nullptr);
}

TEST_F(ResolverTest, ResolveTypeInvalidTypeNameTests) {
  const Type* type;
  EXPECT_THAT(
      resolver_->ResolveTypeName("Array<ARRAY<INT32>>", &type),
      StatusIs(_, HasSubstr("Arrays of arrays are not supported")));

  EXPECT_THAT(resolver_->ResolveTypeName("blahblahblah", &type),
              StatusIs(_, HasSubstr("Type not found: blahblahblah")));

  EXPECT_THAT(resolver_->ResolveTypeName("CONCAT", &type),
              StatusIs(_, HasSubstr("Type not found: CONCAT")));

  EXPECT_THAT(
      resolver_->ResolveTypeName("timestamp(1)", &type),
      StatusIs(_, HasSubstr("TIMESTAMP precision must be 0, 3, 6, 9, or 12")));

  EXPECT_THAT(
      resolver_->ResolveTypeName("string collate 'abc'", &type),
      StatusIs(_, HasSubstr("Type with collation name is not supported")));
}

TEST_F(ResolverTest, TestErrorCatalogNameTests) {
  std::unique_ptr<ErrorCatalog> error_catalog;
  GOOGLESQL_ASSERT_OK(
      ErrorCatalog::Create(absl::StatusCode::kInvalidArgument, &error_catalog));
  ResetResolver(error_catalog.get());

  // Name lookups into the error_catalog return an error (INVALID_ARGUMENT)
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_ast;

  // Table
  const std::string query_with_table = "SELECT * FROM T";
  GOOGLESQL_ASSERT_OK(ParseStatement(query_with_table, ParserOptions(), &parser_output));
  EXPECT_THAT(resolver_->ResolveStatement(
                  query_with_table, parser_output->statement(), &resolved_ast),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("FindTable error")));

  // Type
  const std::string query_with_type = "SELECT cast(1 as foo_type)";
  GOOGLESQL_ASSERT_OK(ParseStatement(query_with_type, ParserOptions(), &parser_output));
  EXPECT_THAT(resolver_->ResolveStatement(
                  query_with_type, parser_output->statement(), &resolved_ast),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("FindType error")));

  // Procedure
  const std::string query_with_procedure = "CALL foo()";
  GOOGLESQL_ASSERT_OK(
      ParseStatement(query_with_procedure, ParserOptions(), &parser_output));
  EXPECT_THAT(
      resolver_->ResolveStatement(query_with_procedure,
                                  parser_output->statement(), &resolved_ast),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("FindProcedure error")));

  // Constant
  const std::string query_with_constant = "SELECT constant";
  GOOGLESQL_ASSERT_OK(
      ParseStatement(query_with_constant, ParserOptions(), &parser_output));
  EXPECT_THAT(
      resolver_->ResolveStatement(query_with_constant,
                                  parser_output->statement(), &resolved_ast),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("FindConstant error")));

  // Function
  const std::string query_with_function = "SELECT foo()";
  GOOGLESQL_ASSERT_OK(
      ParseStatement(query_with_function, ParserOptions(), &parser_output));
  EXPECT_THAT(
      resolver_->ResolveStatement(query_with_function,
                                  parser_output->statement(), &resolved_ast),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid function foo")));

  // TableValuedFunction
  const std::string query_with_tvf = "SELECT * from foo()";
  GOOGLESQL_ASSERT_OK(ParseStatement(query_with_tvf, ParserOptions(), &parser_output));
  EXPECT_THAT(resolver_->ResolveStatement(
                  query_with_tvf, parser_output->statement(), &resolved_ast),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid table-valued function foo")));
}

TEST_F(ResolverTest, TestResolveCastExpression) {
  TestCastExpression("CAST(true as BOoL)", types::BoolType());
  TestCastExpression("CAST(False as Boolean)", types::BoolType());
  TestCastExpression("CAST(1 as INT32)", types::Int32Type());
  TestCastExpression("CAST(1 as INT64)", types::Int64Type());
  TestCastExpression("CAST(1 as UINT32)", types::Uint32Type());
  TestCastExpression("CAST(1 as UINT64)", types::Uint64Type());
  TestCastExpression("CAST(1 as FLOAT)", types::FloatType());
  TestCastExpression("CAST(1 as DOUBLE)", types::DoubleType());
  TestCastExpression("CAST('x' as STRING)", types::StringType());
  TestCastExpression("CAST('x' as bytes)", types::BytesType());

  TestCastExpression("cast('2013-11-26' as date)", types::DateType());

  TestCastExpression("cast('2013-11-26 12:23:34' as timestamp)",
                     types::TimestampType());

  TestCastExpression("cast(timestamp('2013-11-26 12:23:34') as timestamp)",
                     types::TimestampType());

  TestCastExpression("CAST(true as STRING)", types::StringType());
  TestCastExpression("CAST(false as STRING)", types::StringType());

  TestCastExpression("CAST('true' as Bool)", types::BoolType());
  TestCastExpression("CAST('FaLsE' as Boolean)", types::BoolType());
  TestCastExpression("CAST(1 as BOOL)", types::BoolType());

  TestCastExpression("CAST('1' as INT32)", types::Int32Type());

  TestCastExpression("CAST('1' as INT64)", types::Int64Type());

  TestCastExpression("CAST('1' as UINT32)", types::Uint32Type());
  TestCastExpression("CAST(true as UINT32)", types::Uint32Type());

  TestCastExpression("CAST('1' as UINT64)", types::Uint64Type());
  TestCastExpression("CAST(true as UINT64)", types::Uint64Type());

  TestCastExpression("CAST('1' as FLOAT)", types::FloatType());
  TestCastExpression("CAST('1' as DOUBLE)", types::DoubleType());
  TestCastExpression("CAST(1 as STRING)", types::StringType());
  TestCastExpression("CAST(1.0 as STRING)", types::StringType());

  TestCastExpression(
      "CAST(CAST('TESTENUM1' as `googlesql_test.TestEnum`) as STRING)",
      types::StringType());

  // Enum value names are case-sensitive, 'TESTENUM1' is good, 'TestEnum1' is
  // not. However, this is delayed to runtime, since we do not let
  // constant folding fail compilation. See `cast_function.test` in compliance
  // tests.
  TestCastExpression(
      "CAST(CAST('TestEnum1' as `googlesql_test.TestEnum`) as STRING)",
      types::StringType());

  TestCastExpression("CAST(CAST(1 as `googlesql_test.TestEnum`) as INT32)",
                     types::Int32Type());
  TestCastExpression("CAST(CAST(1 as `googlesql_test.TestEnum`) as INT64)",
                     types::Int64Type());
  TestCastExpression("CAST(CAST(1 as `googlesql_test.TestEnum`) as UINT32)",
                     types::Uint32Type());
  TestCastExpression("CAST(CAST(1 as `googlesql_test.TestEnum`) as UINT64)",
                     types::Uint64Type());
  TestCastExpression("CAST('00000000-0000-4000-8000-000000000000' as UUID)",
                     types::UuidType());
  TestCastExpression("CAST('0102030405060708090a0b0c0d0e0f10' as UUID)",
                     types::UuidType());

  // TODO: Add basic CAST resolution tests for ENUM, PROTO, STRUCT,
  // ARRAY (some will be errors - to be added in TestResolverErrors).

  // Cast shorthands do not work.
  ResolveFunctionFails("INT64(1)", "No matching signature for function INT64");
  ResolveFunctionFails("INT32(1)", "No matching signature for function INT32");

  // Casts disallowed between date/time and integer.
  ResolveFunctionFails("cast(cast(1 as INT32) as date)",
                       "Invalid cast from INT32 to DATE");
  ResolveFunctionFails("cast(cast(1 as INT64) as date)",
                       "Invalid cast from INT64 to DATE");

  ResolveFunctionFails("cast(cast('2013-11-26' as date) as INT32)",
                       "Invalid cast from DATE to INT32");
  ResolveFunctionFails("cast(cast('2013-11-26' as date) as INT64)",
                       "Invalid cast from DATE to INT64");

  ResolveFunctionFails("CAST(10000000000 as timestamp)",
                       "Invalid cast from INT64 to TIMESTAMP");
  ResolveFunctionFails("CAST(CAST(1 as INT64) as timestamp)",
                       "Invalid cast from INT64 to TIMESTAMP");

  ResolveFunctionFails(
      "cast(cast('2013-11-26 12:23:34' as timestamp) as INT64)",
      "Invalid cast from TIMESTAMP to INT64");

  ResolveFunctionFails("CAST(1 as blah)", "Type not found: blah");
  ResolveFunctionFails("CAST(1.0 as bool)",
                       "Invalid cast from DOUBLE to BOOL");
  ResolveFunctionFails("CAST(true as float)",
                       "Invalid cast from BOOL to FLOAT");
  ResolveFunctionFails("CAST(true as double)",
                       "Invalid cast from BOOL to DOUBLE");
  ResolveFunctionFails("CAST(true AS UUID)", "Invalid cast from BOOL to UUID");
  ResolveFunctionFails("CAST(1 AS UUID)", "Invalid cast from INT64 to UUID");

  // Invalid type names
  ResolveFunctionFails("CAST(1 as blah)", "Type not found: blah");

  // Type names in SQL Standard which are explicitly not supported by GoogleSQL
  ResolveFunctionFails("CAST(1 as INTEGER)", "Type not found: INTEGER");
  ResolveFunctionFails("CAST(1 as BIGINT)", "Type not found: BIGINT");
  ResolveFunctionFails("CAST(1 as SMALLINT)", "Type not found: SMALLINT");
  ResolveFunctionFails("CAST(1 as real)", "Type not found: real");
  ResolveFunctionFails("CAST(1 as NUMBER)", "Type not found: NUMBER");
  ResolveFunctionFails("CAST(b'0' as binary)", "Type not found: binary");
  ResolveFunctionFails("CAST(b'0' as BLOB)", "Type not found: BLOB");
  ResolveFunctionFails("CAST('foo' as CHAR)", "Type not found: CHAR");
  ResolveFunctionFails("CAST('foo' AS VARCHAR(5))", "Type not found: VARCHAR");

  // SQL Standard type names which are not even parsable in GoogleSQL
  ParseFunctionFails(
      "CAST(1 as DOUBLE PRECISION)",
      R"error(Expected ")" or keyword FORMAT but got identifier "PRECISION")error");
  ParseFunctionFails(
      "CAST('foo' as CHAR VARYING(10))",
      R"error(Expected ")" or keyword FORMAT but got identifier "VARYING")error");
}

TEST_F(ResolverTest, TestCoerceToBoolSuccess) {
  for (Resolver::CoercionMode mode :
       {Resolver::kImplicitAssignment, Resolver::kImplicitCoercion,
        Resolver::kExplicitCoercion}) {
    std::unique_ptr<const ResolvedExpr> resolved;
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        resolved, ResolveAndCoerce("TRUE", type_factory_.get_bool(), mode,
                                   "error message"));
    EXPECT_THAT(resolved->node_kind(), RESOLVED_LITERAL)
        << resolved->DebugString();
  }
}

TEST_F(ResolverTest, TestCoerceToBoolFail) {
  for (Resolver::CoercionMode mode :
       {Resolver::kImplicitAssignment, Resolver::kImplicitCoercion}) {
    EXPECT_THAT(ResolveAndCoerce("5", type_factory_.get_bool(), mode,
                                 "error message $0 $1"),
                StatusIs(_, HasSubstr("error message BOOL INT64")));
  }
  GOOGLESQL_EXPECT_OK(ResolveAndCoerce("5", type_factory_.get_bool(),
                             Resolver::kExplicitCoercion,
                             "error message $0 $1"));
}

TEST_F(ResolverTest, TestCoerceToBoolFailNoErrorMsgTemplate) {
  for (Resolver::CoercionMode mode :
       {Resolver::kImplicitAssignment, Resolver::kImplicitCoercion}) {
    EXPECT_THAT(
        ResolveAndCoerce("5", type_factory_.get_bool(), mode,
                         /*error_template=*/""),
        StatusIs(_, ::testing::AllOf(HasSubstr("INT64"), HasSubstr("BOOL"))));
  }
}

TEST_F(ResolverTest, TestImplicitCoerceInt64LiteralToInt32Succeed) {
  std::unique_ptr<const ResolvedExpr> resolved;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      resolved, ResolveAndCoerce("5", type_factory_.get_int32(),
                                 Resolver::kImplicitCoercion, "error message"));
  EXPECT_THAT(resolved->node_kind(), RESOLVED_LITERAL)
      << resolved->DebugString();
}

TEST_F(ResolverTest, TestImplicitCoerceInt64ExprToInt32Fail) {
  EXPECT_THAT(
      ResolveAndCoerce("TestConstantInt64 + 1", type_factory_.get_int32(),
                       Resolver::kImplicitCoercion, "error message $0 $1"),
      StatusIs(_, HasSubstr("error message INT32 INT64")));
}

TEST_F(ResolverTest, TestAssignmentCoerceInt64ExprToInt32Succeed) {
  std::unique_ptr<const ResolvedExpr> resolved;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      resolved,
      ResolveAndCoerce("TestConstantInt64 + 1", type_factory_.get_int32(),
                       Resolver::kImplicitAssignment, "error message"));
  EXPECT_THAT(resolved->node_kind(), RESOLVED_CAST) << resolved->DebugString();
}

TEST_F(ResolverTest, TestResolveCaseExpressions) {
  TestCaseExpression("CASE WHEN 2=1 THEN 'a' END", "GoogleSQL:$case_no_value");
  TestCaseExpression("CASE WHEN 2=1 THEN 'a' ELSE 'b' END",
                     "GoogleSQL:$case_no_value");
  TestCaseExpression("CASE WHEN 2=1 THEN 'a' WHEN 3 = 4 THEN 'c' END",
                     "GoogleSQL:$case_no_value");
  TestCaseExpression("CASE WHEN 2=1 THEN 'a' WHEN 3 = 4 THEN 'c' ELSE 'b' END",
                     "GoogleSQL:$case_no_value");

  TestCaseExpression("CASE 1 WHEN 1 THEN 'a' END",
                     "GoogleSQL:$case_with_value");
  TestCaseExpression("CASE 1 WHEN 1 THEN 'a' ELSE 'b' END",
                     "GoogleSQL:$case_with_value");
  TestCaseExpression("CASE 1 WHEN 1 THEN 'a' WHEN 3 THEN 'c' END",
                     "GoogleSQL:$case_with_value");
  TestCaseExpression("CASE 1 WHEN 1 THEN 'a' WHEN 3 THEN 'c' ELSE 'b' END",
                     "GoogleSQL:$case_with_value");

  ResolveFunctionFails("CASE 1 WHEN 1 THEN 2 WHEN a THEN b END",
                       "Unrecognized name: a");
  ResolveFunctionFails("CASE 1 WHEN '1' THEN 2 END",
                       "No matching signature for operator CASE");
}

TEST_F(ResolverTest, TestResolveFunctions) {
  ParseAndResolveFunction("CURRENT_TIMESTAMP()",
                          "GoogleSQL:current_timestamp");
  ParseAndResolveFunction("CONCAT('a', 'b', 'c', 'd')", "GoogleSQL:concat");

  ResolveFunctionFails("sqrt(distinct 49)", "cannot be called with DISTINCT");
  ResolveFunctionFails("CONCAT('a',a)", "Unrecognized name: a");
  ResolveFunctionFails("sqrt(49, 81)",
                       "No matching signature for function SQRT");
}

TEST_F(ResolverTest, TestResolveAggregateExpressions) {
  ParseAndResolveFunction("Count(*)", "GoogleSQL:sum",
                          true /* is aggregation function */,
                          true /* aggregation allowed */);
  ParseAndResolveFunction("Sum(8)", "GoogleSQL:sum",
                          true /* is aggregation function */,
                          true /* aggregation allowed */);
  ParseAndResolveFunction("CASE WHEN 5 = 4 THEN Sum(6) END",
                          "GoogleSQL:$case_no_value",
                          false /* is not aggregation function */,
                          true /* aggregation allowed */);

  ResolveFunctionFails("sum(8)", "Aggregate function SUM not allowed in "
                       "ResolveScalarExpr");
  ResolveFunctionFails(
      "sum(sum(8))",
      "Multi-level aggregation requires the enclosing aggregate function to "
      "have one or more GROUP BY modifiers",
      true);
  ResolveFunctionFails("count(distinct *)",
                       "COUNT(*) cannot be used with DISTINCT", true);
}

TEST_F(ResolverTest, ResolvingSafeScalarFunctionsSucceed) {
  ParseAndResolveFunction("SAFE.CURRENT_TIMESTAMP()",
                          "GoogleSQL:current_timestamp");
  ParseAndResolveFunction("SAFE.CONCAT('a', 'b', 'c', 'd')",
                          "GoogleSQL:concat");
  ParseAndResolveFunction(
      "safe.timestamp_add(TIMESTAMP \"2017-09-15 23:59:59.999999 UTC\", "
      "INTERVAL 10 DAY)",
      "GoogleSQL:timestamp_add");
  ParseAndResolveFunction("SAFE.GENERATE_ARRAY(11, 33, 2)",
                          "GoogleSQL:generate_array");
}

TEST_F(ResolverTest, ResolvingSafeAggregateFucntionsSucceed) {
  // Aggregate functions
  ParseAndResolveFunction("SAFE.Count(*)", "GoogleSQL:sum",
                          true /* is aggregation function */,
                          true /* aggregation allowed */);
  ParseAndResolveFunction("SAFE.Sum(8)", "GoogleSQL:sum",
                          true /* is aggregation function */,
                          true /* aggregation allowed */);
}

TEST_F(ResolverTest, ResolvingBuiltinFucntionsFail) {
  // Builtin functions
  ParseFunctionFails("SAFE.~(b\"\")", "Syntax error: Unexpected \"~\"");

  ParseFunctionFails(
      "SAFE.EXTRACT(HOUR FROM DATETIME(2008, 12, 25, 15, 30, 00))",
      "Syntax error: Expected \")\" but got keyword FROM");

  ParseFunctionFails("SAFE.(CAST(3152862397390174577 AS UINT64))",
                     "Syntax error: Unexpected keyword CAST");

  ParseFunctionFails("SAFE.ARRAY<DOUBLE>[1, 2, 3]",
                     "Syntax error: Unexpected \">\"");

  ParseFunctionFails(
      "SAFE.('a' IN ('a', 'b', 'c'))",
      "Syntax error: Unexpected string "
      "literal 'a'");

  ParseFunctionFails(
      "SAFE.(case when KitchenSink.int64_key_1 = 0 then NULL else "
      "KitchenSink end)",
      "Syntax error: Unexpected keyword CASE");
}

TEST_F(ResolverTest, TestFindFieldDescriptorsSuccess) {
  TypeFactory factory;
  const googlesql::ProtoType* root_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &root_type));
  TestFindFieldDescriptorsSuccess("int64_key_1", root_type);
  TestFindFieldDescriptorsSuccess("int32_val", root_type);
  TestFindFieldDescriptorsSuccess("repeated_int32_val", root_type);
  TestFindFieldDescriptorsSuccess("date64", root_type);
  TestFindFieldDescriptorsSuccess("nested_value.nested_int64", root_type);
  TestFindFieldDescriptorsSuccess("nested_value.nested_repeated_int64",
                                  root_type);

  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::RecursivePB::descriptor(),
                                  &root_type));
  TestFindFieldDescriptorsSuccess("recursive_pb.recursive_pb.int64_val",
                                  root_type);
}

TEST_F(ResolverTest, TestFindFieldDescriptorsFail) {
  TypeFactory factory;
  const googlesql::ProtoType* root_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                  &root_type));
  const std::string& does_not_have_field = "does not have a field named ";
  TestFindFieldDescriptorsFail(
      "invalid_field", root_type,
      absl::StrCat(does_not_have_field, "invalid_field"));
  TestFindFieldDescriptorsFail(
      "nested_value.invalid_field", root_type,
      absl::StrCat(does_not_have_field, "invalid_field"));
  const std::string& cannot_access_field = "Cannot access field ";
  TestFindFieldDescriptorsFail(
      "int32_val.invalid_field", root_type,
      absl::StrCat(cannot_access_field, "invalid_field"));
  TestFindFieldDescriptorsFail(
      "nested_value.nested_int64.invalid_field", root_type,
      absl::StrCat(cannot_access_field, "invalid_field"));
}

TEST_F(ResolverTest, TestResolveParameterExpr) {
  for (const auto& param : analyzer_options_.query_parameters()) {
    TestResolveParameterExprSuccess(absl::StrCat("@", param.first),
                                    param.second);
  }
  TestResolveParameterExprFails("@InvalidParam",
                                "Query parameter 'InvalidParam' not found");
  // Checks parameters case insensitivity.
  TestResolveParameterExprSuccess(
      "@param_MiXeDcAsE", type_factory_.get_string());
  TestResolveParameterExprSuccess(
      "@param_MIXEDCASE", type_factory_.get_string());
  TestResolveParameterExprSuccess(
      "@param_mixedCASE", type_factory_.get_string());
}

TEST_F(ResolverTest, TestResolveLiteralAsNumericTarget) {
  {
    const std::string numeric_string =
        "99999999999999999999999999999.999999999";
    std::unique_ptr<const AnalyzerOutput> analyzer_output;
    GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
        numeric_string, analyzer_options_, sample_catalog_->catalog(),
        &type_factory_, type_factory_.get_numeric(), &analyzer_output));
    ASSERT_EQ(analyzer_output->resolved_expr()->node_kind(), RESOLVED_LITERAL);
    EXPECT_TRUE(
        analyzer_output->resolved_expr()
            ->GetAs<ResolvedLiteral>()
            ->value()
            .Equals(values::Numeric(
                NumericValue::FromStringStrict(numeric_string).value())));
  }

  {
    const std::string numeric_string = "9999999999999999999";
    std::unique_ptr<const AnalyzerOutput> analyzer_output;
    GOOGLESQL_ASSERT_OK(AnalyzeExpressionForAssignmentToType(
        numeric_string, analyzer_options_, sample_catalog_->catalog(),
        &type_factory_, type_factory_.get_numeric(), &analyzer_output));
    ASSERT_EQ(analyzer_output->resolved_expr()->node_kind(), RESOLVED_LITERAL);
    EXPECT_TRUE(
        analyzer_output->resolved_expr()
            ->GetAs<ResolvedLiteral>()
            ->value()
            .Equals(values::Numeric(
                NumericValue::FromStringStrict(numeric_string).value())));
  }
}

TEST_F(ResolverTest, TestBytesAndStringLiteralComparison) {
  // FEATURE_IMPLICIT_COERCION_STRING_LITERAL_TO_BYTES is enabled, so
  // these statements succeed.
  TestResolverOK("'abc' < b'abd'");
  TestResolverOK("'abc' <= b'abd'");
  TestResolverOK("'abc' > b'abd'");
  TestResolverOK("'abc' >= b'abd'");
  TestResolverOK("'abc' = b'abd'");
  TestResolverOK("'abc' != b'abd'");
  TestResolverOK("b'abc' < 'abd'");
  TestResolverOK("b'abc' <= 'abd'");
  TestResolverOK("b'abc' > 'abd'");
  TestResolverOK("b'abc' >= 'abd'");
  TestResolverOK("b'abc' = 'abd'");
  TestResolverOK("b'abc' != 'abd'");
  TestResolverOK("b'abc' IN ('abc', 'abd')");
  TestResolverOK("'abc' IN (b'abc', b'abd')");
  TestResolverOK("'abc' IN UNNEST([b'abc', b'abd'])");
  TestResolverOK("b'abc' IN UNNEST(['abc', 'abd'])");
  TestResolverOK("b'abc' LIKE 'ab%'");
  TestResolverOK("'abc' LIKE b'ab%'");
}

TEST_F(ResolverTest, ReturnsErrorWhenRequestedToOrderByZero) {
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_ast;

  const std::string query = "SELECT '' FROM UNNEST([]) ORDER BY 0";
  GOOGLESQL_ASSERT_OK(ParseStatement(query, ParserOptions(), &parser_output));
  EXPECT_THAT(
      resolver_->ResolveStatement(query, parser_output->statement(),
                                  &resolved_ast),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("ORDER BY column number item is out of range.")));
}

TEST_F(ResolverTest, ReturnsErrorWhenRequestedToOrderByNegativeNumber) {
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_ast;

  const std::string query = "SELECT '' FROM UNNEST([]) ORDER BY -1";
  GOOGLESQL_ASSERT_OK(ParseStatement(query, ParserOptions(), &parser_output));
  EXPECT_THAT(
      resolver_->ResolveStatement(query, parser_output->statement(),
                                  &resolved_ast),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("ORDER BY column number item is out of range.")));
}

TEST_F(ResolverTest, TestHasAnonymization) {
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_statement;
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  std::string sql;

  // Test a statement without anonymization
  sql = "SELECT * FROM KeyValue";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_FALSE(resolver_->analyzer_output_properties().has_anonymization);

  // Test a statement with anonymization
  sql = "SELECT WITH ANONYMIZATION key FROM KeyValue GROUP BY key";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  sql = "SELECT ANON_COUNT(*) FROM KeyValue";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  // Test a statement with anonymization in a table subquery
  sql = "SELECT * FROM (SELECT ANON_COUNT(*) FROM KeyValue)";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  // Test a statement with anonymization in an expression subquery
  sql = "SELECT (SELECT ANON_COUNT(*) FROM KeyValue) FROM KeyValue";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  // Test a statement with anonymization in an expression subquery
  sql = "SELECT * FROM KeyValue "
        "WHERE key IN (SELECT ANON_COUNT(*) FROM KeyValue)";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  // Test a statement with anonymization, but resolution fails
  sql = "SELECT ANON_COUNT(*) FROM KeyValue "
        "UNION ALL "
        "SELECT 'string_literal'";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  EXPECT_FALSE(resolver_->ResolveStatement(sql, parser_output->statement(),
                                           &resolved_statement).ok());
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  // Test an expression without anonymization
  sql = "CONCAT('a', 'b')";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseExpression(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(ResolveExpr(parser_output->expression(), &resolved_expr));
  EXPECT_FALSE(resolver_->analyzer_output_properties().has_anonymization);

  // Test an expression with anonymization
  sql = "ANON_COUNT(*)";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseExpression(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(ResolveExpr(parser_output->expression(), &resolved_expr,
                        /*aggregation_allowed=*/true));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);

  // Test an expression with anonymization in a subquery expression
  sql = "5 IN (SELECT ANON_COUNT(*) FROM KeyValue)";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseExpression(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(ResolveExpr(parser_output->expression(), &resolved_expr,
                        /*aggregation_allowed=*/true));
  EXPECT_TRUE(resolver_->analyzer_output_properties().has_anonymization);
}

TEST_F(ResolverTest, TestHasAggregationThreshold) {
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_statement;
  std::string sql;
  // Test that a statement with aggregation thresholding uses the new rewriter.
  sql = "SELECT WITH AGGREGATION_THRESHOLD key FROM KeyValue GROUP BY key";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql,
                           ParserOptions(analyzer_options_.GetParserOptions()),
                           &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  // Aggregation threshold rewriter should be present, anonymization rewriter
  // should not be present.
  EXPECT_FALSE(resolver_->analyzer_output_properties().has_anonymization);
}

TEST_F(ResolverTest, TestDoesNotHaveAggregationThreshold) {
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_statement;
  std::string sql;

  // Test a statement without aggregation thresholding.
  sql = "SELECT * FROM KeyValue";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql,
                           ParserOptions(analyzer_options_.GetParserOptions()),
                           &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
  EXPECT_FALSE(resolver_->analyzer_output_properties().has_anonymization);
}

TEST_F(ResolverTest, FlattenInCatalogButFeatureOff) {
  analyzer_options_.mutable_language()->DisableAllLanguageFeatures();
  ResetResolver(sample_catalog_->catalog());
  ResolveFunctionFails("FLATTEN([0, 1, 2])", "Function not found: FLATTEN");
}

TEST_F(ResolverTest, ReturnsErrorWhenSpannerDdlModeEnabled) {
  analyzer_options_.mutable_language()->EnableLanguageFeature(
      FEATURE_SPANNER_LEGACY_DDL);
  ResetResolver(sample_catalog_->catalog());

  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_ast;

  const std::string stmt = "CREATE INDEX idx ON t(id)";
  GOOGLESQL_ASSERT_OK(ParseStatement(stmt, ParserOptions(), &parser_output));
  EXPECT_THAT(resolver_->ResolveStatement(stmt, parser_output->statement(),
                                          &resolved_ast),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Spanner DDL statements are not supported "
                                 "when resolving statements.")));
}

TEST_F(ResolverTest, TestIntervalLiteral) {
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' YEAR");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0' YEAR");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '+0' YEAR");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '000' YEAR");
  TestIntervalLiteral("9-0 0 0:0:0", "INTERVAL '009' YEAR");
  TestIntervalLiteral("-9-0 0 0:0:0", "INTERVAL '-009' YEAR");
  TestIntervalLiteral("123-0 0 0:0:0", "INTERVAL '123' YEAR");
  TestIntervalLiteral("-123-0 0 0:0:0", "INTERVAL '-123' YEAR");
  TestIntervalLiteral("123-0 0 0:0:0", "INTERVAL '+123' YEAR");
  TestIntervalLiteral("10000-0 0 0:0:0", "INTERVAL '10000' YEAR");
  TestIntervalLiteral("-10000-0 0 0:0:0", "INTERVAL '-10000' YEAR");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' QUARTER");
  TestIntervalLiteral("0-9 0 0:0:0", "INTERVAL '3' QUARTER");
  TestIntervalLiteral("-0-9 0 0:0:0", "INTERVAL '-3' QUARTER");
  TestIntervalLiteral("2-6 0 0:0:0", "INTERVAL '10' QUARTER");
  TestIntervalLiteral("-2-6 0 0:0:0", "INTERVAL '-10' QUARTER");
  TestIntervalLiteral("10000-0 0 0:0:0", "INTERVAL '40000' QUARTER");
  TestIntervalLiteral("-10000-0 0 0:0:0", "INTERVAL '-40000' QUARTER");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' MONTH");
  TestIntervalLiteral("0-6 0 0:0:0", "INTERVAL '6' MONTH");
  TestIntervalLiteral("-0-6 0 0:0:0", "INTERVAL '-6' MONTH");
  TestIntervalLiteral("40-5 0 0:0:0", "INTERVAL '485' MONTH");
  TestIntervalLiteral("-40-5 0 0:0:0", "INTERVAL '-485' MONTH");
  TestIntervalLiteral("10000-0 0 0:0:0", "INTERVAL '120000' MONTH");
  TestIntervalLiteral("-10000-0 0 0:0:0", "INTERVAL '-120000' MONTH");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' WEEK");
  TestIntervalLiteral("0-0 7 0:0:0", "INTERVAL '1' WEEK");
  TestIntervalLiteral("0-0 -7 0:0:0", "INTERVAL '-1' WEEK");
  TestIntervalLiteral("0-0 140 0:0:0", "INTERVAL '20' WEEK");
  TestIntervalLiteral("0-0 -140 0:0:0", "INTERVAL '-20' WEEK");
  TestIntervalLiteral("0-0 3659999 0:0:0", "INTERVAL '522857' WEEK");
  TestIntervalLiteral("0-0 -3659999 0:0:0", "INTERVAL '-522857' WEEK");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' DAY");
  TestIntervalLiteral("0-0 371 0:0:0", "INTERVAL '371' DAY");
  TestIntervalLiteral("0-0 -371 0:0:0", "INTERVAL '-371' DAY");
  TestIntervalLiteral("0-0 3660000 0:0:0", "INTERVAL '3660000' DAY");
  TestIntervalLiteral("0-0 -3660000 0:0:0", "INTERVAL '-3660000' DAY");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' HOUR");
  TestIntervalLiteral("0-0 0 25:0:0", "INTERVAL '25' HOUR");
  TestIntervalLiteral("0-0 0 -25:0:0", "INTERVAL '-25' HOUR");
  TestIntervalLiteral("0-0 0 87840000:0:0", "INTERVAL '87840000' HOUR");
  TestIntervalLiteral("0-0 0 -87840000:0:0", "INTERVAL '-87840000' HOUR");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' MINUTE");
  TestIntervalLiteral("0-0 0 0:3:0", "INTERVAL '3' MINUTE");
  TestIntervalLiteral("0-0 0 -0:3:0", "INTERVAL '-3' MINUTE");
  TestIntervalLiteral("0-0 0 1:12:0", "INTERVAL '72' MINUTE");
  TestIntervalLiteral("0-0 0 -1:12:0", "INTERVAL '-72' MINUTE");
  TestIntervalLiteral("0-0 0 87840000:0:0", "INTERVAL '5270400000' MINUTE");
  TestIntervalLiteral("0-0 0 -87840000:0:0", "INTERVAL '-5270400000' MINUTE");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '+0' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0.0' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0.0' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '+0.0' SECOND");
  TestIntervalLiteral("0-0 0 0:0:1", "INTERVAL '1' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1", "INTERVAL '-1' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100", "INTERVAL '0.1' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.100", "INTERVAL '-0.1' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.120", "INTERVAL '+.12' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.120", "INTERVAL '.12' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.120", "INTERVAL '-.12' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100", "INTERVAL '+0.1' SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.200", "INTERVAL '1.2' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.200", "INTERVAL '-1.2' SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.230", "INTERVAL '1.23' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.230", "INTERVAL '-1.23' SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234", "INTERVAL '1.23400' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234", "INTERVAL '-1.23400' SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234560", "INTERVAL '1.23456' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234560", "INTERVAL '-1.23456' SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.123456789", "INTERVAL '0.123456789' SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.123456789",
                      "INTERVAL '-0.123456789' SECOND");
  TestIntervalLiteral("0-0 0 27777777:46:39", "INTERVAL '99999999999' SECOND");
  TestIntervalLiteral("0-0 0 -27777777:46:39",
                      "INTERVAL '-99999999999' SECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0", "INTERVAL '316224000000' SECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0", "INTERVAL '-316224000000' SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' MILLISECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0' MILLISECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '+0' MILLISECOND");
  TestIntervalLiteral("0-0 0 27777777:46:39.999",
                      "INTERVAL '99999999999999' MILLISECOND");
  TestIntervalLiteral("0-0 0 -27777777:46:39.999",
                      "INTERVAL '-99999999999999' MILLISECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '316224000000000' MILLISECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '-316224000000000' MILLISECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0' MICROSECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0' MICROSECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '+0' MICROSECOND");
  TestIntervalLiteral("0-0 0 27777777:46:39.999999",
                      "INTERVAL '99999999999999999' MICROSECOND");
  TestIntervalLiteral("0-0 0 -27777777:46:39.999999",
                      "INTERVAL '-99999999999999999' MICROSECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '316224000000000000' MICROSECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '-316224000000000000' MICROSECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0' YEAR TO MONTH");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0-0' YEAR TO MONTH");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '+0-0' YEAR TO MONTH");
  TestIntervalLiteral("1-0 0 0:0:0", "INTERVAL '1-0' YEAR TO MONTH");
  TestIntervalLiteral("1-0 0 0:0:0", "INTERVAL '+1-0' YEAR TO MONTH");
  TestIntervalLiteral("-1-0 0 0:0:0", "INTERVAL '-1-0' YEAR TO MONTH");
  TestIntervalLiteral("0-1 0 0:0:0", "INTERVAL '0-1' YEAR TO MONTH");
  TestIntervalLiteral("0-1 0 0:0:0", "INTERVAL '+0-1' YEAR TO MONTH");
  TestIntervalLiteral("-0-1 0 0:0:0", "INTERVAL '-0-1' YEAR TO MONTH");
  TestIntervalLiteral("1-0 0 0:0:0", "INTERVAL '0-12' YEAR TO MONTH");
  TestIntervalLiteral("-1-0 0 0:0:0", "INTERVAL '-0-12' YEAR TO MONTH");
  TestIntervalLiteral("1-8 0 0:0:0", "INTERVAL '0-20' YEAR TO MONTH");
  TestIntervalLiteral("-1-8 0 0:0:0", "INTERVAL '-0-20' YEAR TO MONTH");
  TestIntervalLiteral("10000-0 0 0:0:0", "INTERVAL '9999-12' YEAR TO MONTH");
  TestIntervalLiteral("-10000-0 0 0:0:0", "INTERVAL '-9999-12' YEAR TO MONTH");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0 0' YEAR TO DAY");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0 -0' YEAR TO DAY");
  TestIntervalLiteral("0-0 7 0:0:0", "INTERVAL '0-0 7' YEAR TO DAY");
  TestIntervalLiteral("0-0 -7 0:0:0", "INTERVAL '0-0 -7' YEAR TO DAY");
  TestIntervalLiteral("0-0 7 0:0:0", "INTERVAL '-0-0 +7' YEAR TO DAY");
  TestIntervalLiteral("11-8 30 0:0:0", "INTERVAL '10-20 30' YEAR TO DAY");
  TestIntervalLiteral("11-8 -30 0:0:0", "INTERVAL '10-20 -30' YEAR TO DAY");
  TestIntervalLiteral("-11-8 -30 0:0:0", "INTERVAL '-10-20 -30' YEAR TO DAY");
  TestIntervalLiteral("0-0 3660000 0:0:0",
                      "INTERVAL '0-0 3660000' YEAR TO DAY");
  TestIntervalLiteral("0-0 -3660000 0:0:0",
                      "INTERVAL '0-0 -3660000' YEAR TO DAY");
  TestIntervalLiteral("10000-0 3660000 0:0:0",
                      "INTERVAL '10000-0 3660000' YEAR TO DAY");
  TestIntervalLiteral("-10000-0 -3660000 0:0:0",
                      "INTERVAL '-10000-0 -3660000' YEAR TO DAY");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0 0 0' YEAR TO HOUR");
  TestIntervalLiteral("0-0 0 24:0:0", "INTERVAL '0-0 0 24' YEAR TO HOUR");
  TestIntervalLiteral("0-0 0 -24:0:0", "INTERVAL '0-0 0 -24' YEAR TO HOUR");
  TestIntervalLiteral("0-0 0 24:0:0", "INTERVAL '0-0 0 +24' YEAR TO HOUR");
  TestIntervalLiteral("1-2 3 4:0:0", "INTERVAL '1-2 3 4' YEAR TO HOUR");
  TestIntervalLiteral("-1-2 -3 -4:0:0", "INTERVAL '-1-2 -3 -4' YEAR TO HOUR");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0-0 0 87840000' YEAR TO HOUR");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0-0 0 -87840000' YEAR TO HOUR");
  TestIntervalLiteral("10000-0 3660000 87840000:0:0",
                      "INTERVAL '10000-0 3660000 87840000' YEAR TO HOUR");
  TestIntervalLiteral("-10000-0 -3660000 -87840000:0:0",
                      "INTERVAL '-10000-0 -3660000 -87840000' YEAR TO HOUR");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0 0 0:0' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '0-0 0 12:34' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 -12:34:0",
                      "INTERVAL '0-0 0 -12:34' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0",
                      "INTERVAL '0-0 0 +12:34' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 101:40:0",
                      "INTERVAL '0-0 0 100:100' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 -101:40:0",
                      "INTERVAL '0-0 0 -100:100' YEAR TO MINUTE");
  TestIntervalLiteral("10-2 30 43:21:0",
                      "INTERVAL '10-2 30 43:21' YEAR TO MINUTE");
  TestIntervalLiteral("10-2 30 -43:21:0",
                      "INTERVAL '10-2 30 -43:21' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0-0 0 0:5270400000' YEAR TO MINUTE");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0-0 0 -0:5270400000' YEAR TO MINUTE");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0 0 0:0:0' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0-0 0 0:0:9' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '0-0 0 -0:0:9' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0-0 0 0:0:09' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9",
                      "INTERVAL '0-0 0 -0:0:09' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:59", "INTERVAL '0-0 0 0:0:59' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:59",
                      "INTERVAL '0-0 0 -0:0:59' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '0-0 0 0:0:123' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3",
                      "INTERVAL '0-0 0 -0:0:123' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '0-0 0 1:2:3' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3", "INTERVAL '0-0 0 -1:2:3' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3",
                      "INTERVAL '0-0 0 01:02:03' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3",
                      "INTERVAL '0-0 0 -01:02:03' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56",
                      "INTERVAL '0-0 0 12:34:56' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -12:34:56",
                      "INTERVAL '0-0 0 -12:34:56' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56",
                      "INTERVAL '0-0 0 +12:34:56' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 101:41:40",
                      "INTERVAL '0-0 0 100:100:100' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -101:41:40",
                      "INTERVAL '0-0 0 -100:100:100' YEAR TO SECOND");
  TestIntervalLiteral("10-2 30 4:56:7",
                      "INTERVAL '10-2 30 4:56:7' YEAR TO SECOND");
  TestIntervalLiteral("10-2 30 -4:56:7",
                      "INTERVAL '10-2 30 -4:56:7' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0-0 0 0:0:316224000000' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0-0 0 -0:0:316224000000' YEAR TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0-0 0 0:0:0.0' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0",
                      "INTERVAL '0-0 0 -0:0:0.0000' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100",
                      "INTERVAL '0-0 0 0:0:0.1' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.100",
                      "INTERVAL '0-0 0 -0:0:0.1' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234500",
                      "INTERVAL '0-0 0 0:0:1.2345' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234500",
                      "INTERVAL '0-0 0 -0:0:1.2345' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 0:1:2.345678",
                      "INTERVAL '0-0 0 0:1:2.345678' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:1:2.345678",
                      "INTERVAL '0-0 0 -0:1:2.345678' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3.000456789",
                      "INTERVAL '0-0 0 1:2:3.000456789' YEAR TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3.000456789",
                      "INTERVAL '0-0 0 -1:2:3.000456789' YEAR TO SECOND");
  TestIntervalLiteral("10-2 30 4:56:7.891234500",
                      "INTERVAL '10-2 30 4:56:7.8912345' YEAR TO SECOND");
  TestIntervalLiteral("10-2 30 -4:56:7.891234500",
                      "INTERVAL '10-2 30 -4:56:7.8912345' YEAR TO SECOND");
  TestIntervalLiteral(
      "0-0 0 87839999:59:1.999999999",
      "INTERVAL '0-0 0 0:0:316223999941.999999999' YEAR TO SECOND");
  TestIntervalLiteral(
      "0-0 0 -87839999:59:1.999999999",
      "INTERVAL '0-0 0 -0:0:316223999941.999999999' YEAR TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0' MONTH TO DAY");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 -0' MONTH TO DAY");
  TestIntervalLiteral("0-0 7 0:0:0", "INTERVAL '0 7' MONTH TO DAY");
  TestIntervalLiteral("0-0 -7 0:0:0", "INTERVAL '0 -7' MONTH TO DAY");
  TestIntervalLiteral("0-0 7 0:0:0", "INTERVAL '-0 +7' MONTH TO DAY");
  TestIntervalLiteral("11-8 30 0:0:0", "INTERVAL '140 30' MONTH TO DAY");
  TestIntervalLiteral("11-8 -30 0:0:0", "INTERVAL '140 -30' MONTH TO DAY");
  TestIntervalLiteral("-11-8 -30 0:0:0", "INTERVAL '-140 -30' MONTH TO DAY");
  TestIntervalLiteral("0-0 3660000 0:0:0", "INTERVAL '0 3660000' MONTH TO DAY");
  TestIntervalLiteral("0-0 -3660000 0:0:0",
                      "INTERVAL '0 -3660000' MONTH TO DAY");
  TestIntervalLiteral("10000-0 3660000 0:0:0",
                      "INTERVAL '120000 3660000' MONTH TO DAY");
  TestIntervalLiteral("-10000-0 -3660000 0:0:0",
                      "INTERVAL '-120000 -3660000' MONTH TO DAY");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0 0' MONTH TO HOUR");
  TestIntervalLiteral("0-0 0 24:0:0", "INTERVAL '0 0 24' MONTH TO HOUR");
  TestIntervalLiteral("0-0 0 -24:0:0", "INTERVAL '0 0 -24' MONTH TO HOUR");
  TestIntervalLiteral("0-0 0 24:0:0", "INTERVAL '0 0 +24' MONTH TO HOUR");
  TestIntervalLiteral("1-0 3 4:0:0", "INTERVAL '12 3 4' MONTH TO HOUR");
  TestIntervalLiteral("-1-0 -3 -4:0:0", "INTERVAL '-12 -3 -4' MONTH TO HOUR");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0 0 87840000' MONTH TO HOUR");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0 0 -87840000' MONTH TO HOUR");
  TestIntervalLiteral("10000-0 3660000 87840000:0:0",
                      "INTERVAL '120000 3660000 87840000' MONTH TO HOUR");
  TestIntervalLiteral("-10000-0 -3660000 -87840000:0:0",
                      "INTERVAL '-120000 -3660000 -87840000' MONTH TO HOUR");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0 0:0' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '0 0 12:34' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 -12:34:0",
                      "INTERVAL '0 0 -12:34' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '0 0 +12:34' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 101:40:0",
                      "INTERVAL '0 0 100:100' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 -101:40:0",
                      "INTERVAL '0 0 -100:100' MONTH TO MINUTE");
  TestIntervalLiteral("10-2 30 43:21:0",
                      "INTERVAL '122 30 43:21' MONTH TO MINUTE");
  TestIntervalLiteral("10-2 30 -43:21:0",
                      "INTERVAL '122 30 -43:21' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0 0 0:5270400000' MONTH TO MINUTE");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0 0 -0:5270400000' MONTH TO MINUTE");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0 0:0:0' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0 0 0:0:9' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '0 0 -0:0:9' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0 0 0:0:09' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '0 0 -0:0:09' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:59", "INTERVAL '0 0 0:0:59' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:59",
                      "INTERVAL '0 0 -0:0:59' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '0 0 0:0:123' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3",
                      "INTERVAL '0 0 -0:0:123' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '0 0 1:2:3' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3", "INTERVAL '0 0 -1:2:3' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '0 0 01:02:03' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3",
                      "INTERVAL '0 0 -01:02:03' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56",
                      "INTERVAL '0 0 12:34:56' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -12:34:56",
                      "INTERVAL '0 0 -12:34:56' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56",
                      "INTERVAL '0 0 +12:34:56' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 101:41:40",
                      "INTERVAL '0 0 100:100:100' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -101:41:40",
                      "INTERVAL '0 0 -100:100:100' MONTH TO SECOND");
  TestIntervalLiteral("1-8 30 4:56:7",
                      "INTERVAL '20 30 4:56:7' MONTH TO SECOND");
  TestIntervalLiteral("1-8 30 -4:56:7",
                      "INTERVAL '20 30 -4:56:7' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0 0 0:0:316224000000' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0 0 -0:0:316224000000' MONTH TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0 0:0:0.0' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0",
                      "INTERVAL '0 0 -0:0:0.0000' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100",
                      "INTERVAL '0 0 0:0:0.1' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.100",
                      "INTERVAL '0 0 -0:0:0.1' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234500",
                      "INTERVAL '0 0 0:0:1.2345' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234500",
                      "INTERVAL '0 0 -0:0:1.2345' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 0:1:2.345678",
                      "INTERVAL '0 0 0:1:2.345678' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -0:1:2.345678",
                      "INTERVAL '0 0 -0:1:2.345678' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3.000456789",
                      "INTERVAL '0 0 1:2:3.000456789' MONTH TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3.000456789",
                      "INTERVAL '0 0 -1:2:3.000456789' MONTH TO SECOND");
  TestIntervalLiteral("1-8 30 4:56:7.891234500",
                      "INTERVAL '20 30 4:56:7.8912345' MONTH TO SECOND");
  TestIntervalLiteral("1-8 30 -4:56:7.891234500",
                      "INTERVAL '20 30 -4:56:7.8912345' MONTH TO SECOND");
  TestIntervalLiteral(
      "0-0 0 87839999:59:1.999999999",
      "INTERVAL '0 0 0:0:316223999941.999999999' MONTH TO SECOND");
  TestIntervalLiteral(
      "0-0 0 -87839999:59:1.999999999",
      "INTERVAL '0 0 -0:0:316223999941.999999999' MONTH TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0' DAY TO HOUR");
  TestIntervalLiteral("0-0 0 24:0:0", "INTERVAL '0 24' DAY TO HOUR");
  TestIntervalLiteral("0-0 0 -24:0:0", "INTERVAL '0 -24' DAY TO HOUR");
  TestIntervalLiteral("0-0 0 24:0:0", "INTERVAL '0 +24' DAY TO HOUR");
  TestIntervalLiteral("0-0 3 4:0:0", "INTERVAL '3 4' DAY TO HOUR");
  TestIntervalLiteral("0-0 -3 -4:0:0", "INTERVAL '-3 -4' DAY TO HOUR");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0 87840000' DAY TO HOUR");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0 -87840000' DAY TO HOUR");
  TestIntervalLiteral("0-0 3660000 87840000:0:0",
                      "INTERVAL '3660000 87840000' DAY TO HOUR");
  TestIntervalLiteral("0-0 -3660000 -87840000:0:0",
                      "INTERVAL '-3660000 -87840000' DAY TO HOUR");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0:0' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '0 12:34' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 -12:34:0", "INTERVAL '0 -12:34' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '0 +12:34' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 101:40:0", "INTERVAL '0 100:100' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 -101:40:0", "INTERVAL '0 -100:100' DAY TO MINUTE");
  TestIntervalLiteral("0-0 30 43:21:0", "INTERVAL '30 43:21' DAY TO MINUTE");
  TestIntervalLiteral("0-0 30 -43:21:0", "INTERVAL '30 -43:21' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0 0:5270400000' DAY TO MINUTE");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0 -0:5270400000' DAY TO MINUTE");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0:0:0' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0 0:0:9' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '0 -0:0:9' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0 0:0:09' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '0 -0:0:09' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:59", "INTERVAL '0 0:0:59' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:59", "INTERVAL '0 -0:0:59' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '0 0:0:123' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3", "INTERVAL '0 -0:0:123' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '0 1:2:3' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3", "INTERVAL '0 -1:2:3' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '0 01:02:03' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3", "INTERVAL '0 -01:02:03' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56", "INTERVAL '0 12:34:56' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -12:34:56",
                      "INTERVAL '0 -12:34:56' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56", "INTERVAL '0 +12:34:56' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 101:41:40",
                      "INTERVAL '0 100:100:100' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -101:41:40",
                      "INTERVAL '0 -100:100:100' DAY TO SECOND");
  TestIntervalLiteral("0-0 30 4:56:7", "INTERVAL '30 4:56:7' DAY TO SECOND");
  TestIntervalLiteral("0-0 30 -4:56:7", "INTERVAL '30 -4:56:7' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0 0:0:316224000000' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '0 -0:0:316224000000' DAY TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 0:0:0.0' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0 -0:0:0.0000' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100", "INTERVAL '0 0:0:0.1' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.100",
                      "INTERVAL '0 -0:0:0.1' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234500",
                      "INTERVAL '0 0:0:1.2345' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234500",
                      "INTERVAL '0 -0:0:1.2345' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 0:1:2.345678",
                      "INTERVAL '0 0:1:2.345678' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -0:1:2.345678",
                      "INTERVAL '0 -0:1:2.345678' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3.000456789",
                      "INTERVAL '0 1:2:3.000456789' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3.000456789",
                      "INTERVAL '0 -1:2:3.000456789' DAY TO SECOND");
  TestIntervalLiteral("0-0 30 4:56:7.891234500",
                      "INTERVAL '30 4:56:7.8912345' DAY TO SECOND");
  TestIntervalLiteral("0-0 30 -4:56:7.891234500",
                      "INTERVAL '30 -4:56:7.8912345' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 87839999:59:1.999999999",
                      "INTERVAL '0 0:0:316223999941.999999999' DAY TO SECOND");
  TestIntervalLiteral("0-0 0 -87839999:59:1.999999999",
                      "INTERVAL '0 -0:0:316223999941.999999999' DAY TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0:0' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '12:34' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 -12:34:0", "INTERVAL '-12:34' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 12:34:0", "INTERVAL '+12:34' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 101:40:0", "INTERVAL '100:100' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 -101:40:0", "INTERVAL '-100:100' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0:5270400000' HOUR TO MINUTE");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '-0:5270400000' HOUR TO MINUTE");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0:0:0' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0:0:9' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '-0:0:9' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0:0:09' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '-0:0:09' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:59", "INTERVAL '0:0:59' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:59", "INTERVAL '-0:0:59' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '0:0:123' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3", "INTERVAL '-0:0:123' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '1:2:3' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3", "INTERVAL '-1:2:3' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3", "INTERVAL '01:02:03' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3", "INTERVAL '-01:02:03' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56", "INTERVAL '12:34:56' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -12:34:56", "INTERVAL '-12:34:56' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 12:34:56", "INTERVAL '+12:34:56' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 101:41:40",
                      "INTERVAL '100:100:100' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -101:41:40",
                      "INTERVAL '-100:100:100' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0:0:316224000000' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '-0:0:316224000000' HOUR TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0:0:0.0' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0:0:0.0000' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100", "INTERVAL '0:0:0.1' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.100", "INTERVAL '-0:0:0.1' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234500",
                      "INTERVAL '0:0:1.2345' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234500",
                      "INTERVAL '-0:0:1.2345' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 0:1:2.345678",
                      "INTERVAL '0:1:2.345678' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -0:1:2.345678",
                      "INTERVAL '-0:1:2.345678' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 1:2:3.000456789",
                      "INTERVAL '1:2:3.000456789' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -1:2:3.000456789",
                      "INTERVAL '-1:2:3.000456789' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 87839999:59:1.999999999",
                      "INTERVAL '0:0:316223999941.999999999' HOUR TO SECOND");
  TestIntervalLiteral("0-0 0 -87839999:59:1.999999999",
                      "INTERVAL '-0:0:316223999941.999999999' HOUR TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0:0' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0:9' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '-0:9' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:9", "INTERVAL '0:09' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:9", "INTERVAL '-0:09' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:59", "INTERVAL '0:59' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:59", "INTERVAL '-0:59' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '0:123' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3", "INTERVAL '-0:123' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '2:3' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3", "INTERVAL '-2:3' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:2:3", "INTERVAL '02:03' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:2:3", "INTERVAL '-02:03' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 20:34:56", "INTERVAL '1234:56' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -20:34:56",
                      "INTERVAL '-1234:56' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 20:34:56", "INTERVAL '+1234:56' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 87840000:0:0",
                      "INTERVAL '0:316224000000' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -87840000:0:0",
                      "INTERVAL '-0:316224000000' MINUTE TO SECOND");

  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '0:0.0' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0", "INTERVAL '-0:0.0000' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:0.100", "INTERVAL '0:0.1' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:0.100", "INTERVAL '-0:0.1' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:0:1.234500",
                      "INTERVAL '0:1.2345' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:0:1.234500",
                      "INTERVAL '-0:1.2345' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 0:1:2.345678",
                      "INTERVAL '1:2.345678' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -0:1:2.345678",
                      "INTERVAL '-1:2.345678' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 2:0:3.000456789",
                      "INTERVAL '120:3.000456789' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -2:0:3.000456789",
                      "INTERVAL '-120:3.000456789' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 87839999:59:1.999999999",
                      "INTERVAL '0:316223999941.999999999' MINUTE TO SECOND");
  TestIntervalLiteral("0-0 0 -87839999:59:1.999999999",
                      "INTERVAL '-0:316223999941.999999999' MINUTE TO SECOND");

  // Non-string literals
  TestIntervalLiteralError("INTERVAL b'1' YEAR");

  TestIntervalLiteralError("INTERVAL '' YEAR");
  TestIntervalLiteralError("INTERVAL ' 1' YEAR");
  TestIntervalLiteralError("INTERVAL '-1 ' YEAR");
  TestIntervalLiteralError("INTERVAL '- 1' YEAR");
  TestIntervalLiteralError("INTERVAL '\t1' YEAR");
  TestIntervalLiteralError("INTERVAL '1\t' YEAR");
  TestIntervalLiteralError("INTERVAL '\\n1' YEAR");
  TestIntervalLiteralError("INTERVAL '1\\n' YEAR");
  // invalid formatting
  TestIntervalLiteralError("INTERVAL '--1' YEAR");
  TestIntervalLiteralError("INTERVAL '1.0' YEAR");
  TestIntervalLiteralError("INTERVAL '123 0' YEAR");
  // exceeds max number of months
  TestIntervalLiteralError("INTERVAL '10001' YEAR");
  TestIntervalLiteralError("INTERVAL '-10001' YEAR");
  // overflow during multiplication
  TestIntervalLiteralError("INTERVAL '9223372036854775807' YEAR");
  TestIntervalLiteralError("INTERVAL '-9223372036854775808' YEAR");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' YEAR");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' YEAR");

  // exceeds max number of months
  TestIntervalLiteralError("INTERVAL '40001' QUARTER");
  TestIntervalLiteralError("INTERVAL '-40001' QUARTER");
  // overflow during multiplication
  TestIntervalLiteralError("INTERVAL '9223372036854775807' QUARTER");
  TestIntervalLiteralError("INTERVAL '-9223372036854775808' QUARTER");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' QUARTER");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' QUARTER");

  // exceeds max number of months
  TestIntervalLiteralError("INTERVAL '120001' MONTH");
  TestIntervalLiteralError("INTERVAL '-120001' MONTH");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' MONTH");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' MONTH");

  // exceeds max number of days
  TestIntervalLiteralError("INTERVAL '522858' WEEK");
  TestIntervalLiteralError("INTERVAL '-522858' WEEK");
  // overflow during multiplication
  TestIntervalLiteralError("INTERVAL '9223372036854775807' WEEK");
  TestIntervalLiteralError("INTERVAL '-9223372036854775808' WEEK");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' WEEK");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' WEEK");

  // exceeds max number of days
  TestIntervalLiteralError("INTERVAL '3660001' DAY");
  TestIntervalLiteralError("INTERVAL '-3660001' DAY");

  // exceeds max number of micros
  TestIntervalLiteralError("INTERVAL '87840001' HOUR");
  TestIntervalLiteralError("INTERVAL '-87840001' HOUR");

  // exceeds max number of micros
  TestIntervalLiteralError("INTERVAL '5270400001' MINUTE");
  TestIntervalLiteralError("INTERVAL '-5270400001' MINUTE");

  TestIntervalLiteralError("INTERVAL '' SECOND");
  TestIntervalLiteralError("INTERVAL ' 1' SECOND");
  TestIntervalLiteralError("INTERVAL '1 ' SECOND");
  TestIntervalLiteralError("INTERVAL ' 1.1' SECOND");
  TestIntervalLiteralError("INTERVAL '1.1 ' SECOND");
  TestIntervalLiteralError("INTERVAL '.' SECOND");
  TestIntervalLiteralError("INTERVAL '1. 2' SECOND");
  TestIntervalLiteralError("INTERVAL '1.' SECOND");
  TestIntervalLiteralError("INTERVAL '-1.' SECOND");
  TestIntervalLiteralError("INTERVAL '+1.' SECOND");
  TestIntervalLiteralError("INTERVAL '\t1.1' SECOND");
  TestIntervalLiteralError("INTERVAL '1.1\t' SECOND");
  TestIntervalLiteralError("INTERVAL '\\n1.1' SECOND");
  TestIntervalLiteralError("INTERVAL '1.1\\n' SECOND");
  // more than 9 fractional digits
  TestIntervalLiteralError("INTERVAL '0.1234567890' SECOND");
  // exceeds max number of seconds
  TestIntervalLiteralError("INTERVAL '316224000000.000001' SECOND");
  TestIntervalLiteralError("INTERVAL '-316224000000.000001' SECOND");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' SECOND");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' SECOND");

  TestIntervalLiteralError("INTERVAL '' MILLISECOND");
  TestIntervalLiteralError("INTERVAL ' 1' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '1 ' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '.' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '1.' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '-1.' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '+1.' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '\t1' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '1\t' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '\\n1' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '1\\n' MILLISECOND");
  // fractional digits
  TestIntervalLiteralError("INTERVAL '0.1' MILLISECOND");
  // exceeds max number of milliseconds
  TestIntervalLiteralError("INTERVAL '316224000000001' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '-316224000000001' MILLISECOND");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' MILLISECOND");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' MILLISECOND");

  TestIntervalLiteralError("INTERVAL '' MICROSECOND");
  TestIntervalLiteralError("INTERVAL ' 1' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '1 ' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '.' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '1.' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '-1.' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '+1.' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '\t1' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '1\t' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '\\n1' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '1\\n' MICROSECOND");
  // fractional digits
  TestIntervalLiteralError("INTERVAL '0.1' MICROSECOND");
  // exceeds max number of microseconds
  TestIntervalLiteralError("INTERVAL '316224000000000001' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '-316224000000000001' MICROSECOND");
  // overflow fitting into int64 at SimpleAtoi
  TestIntervalLiteralError("INTERVAL '9223372036854775808' MICROSECOND");
  TestIntervalLiteralError("INTERVAL '-9223372036854775809' MICROSECOND");

  // Unsupported dateparts
  TestIntervalLiteralError("INTERVAL '0' DAYOFWEEK");
  TestIntervalLiteralError("INTERVAL '0' DAYOFYEAR");
  TestIntervalLiteralError("INTERVAL '0' NANOSECOND");
  TestIntervalLiteralError("INTERVAL '0' DATE");
  TestIntervalLiteralError("INTERVAL '0' DATETIME");
  TestIntervalLiteralError("INTERVAL '0' TIME");
  TestIntervalLiteralError("INTERVAL '0' ISOYEAR");
  TestIntervalLiteralError("INTERVAL '0' ISOWEEK");
  TestIntervalLiteralError("INTERVAL '0' WEEK_MONDAY");
  TestIntervalLiteralError("INTERVAL '0' WEEK_TUESDAY");
  TestIntervalLiteralError("INTERVAL '0' WEEK_WEDNESDAY");
  TestIntervalLiteralError("INTERVAL '0' WEEK_THURSDAY");
  TestIntervalLiteralError("INTERVAL '0' WEEK_FRIDAY");
  TestIntervalLiteralError("INTERVAL '0' WEEK_SATURDAY");

  // Not matching format
  TestIntervalLiteralError("INTERVAL '' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0-0' YEAR TO DAY");
  TestIntervalLiteralError("INTERVAL '0' MONTH TO DAY");
  TestIntervalLiteralError("INTERVAL '0:0:0' HOUR TO MINUTE");
  TestIntervalLiteralError("INTERVAL '0:0' HOUR TO SECOND");

  // Whitespace padding
  TestIntervalLiteralError("INTERVAL ' 0-0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0-0 ' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '\t0-0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0-0\t' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0- 0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '- 0-0' YEAR TO MONTH");

  // Exceeds maximum allowed value
  TestIntervalLiteralError("INTERVAL '10001-0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '-10001-0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0-120001' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '-0-120001' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '10000-1' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '-10000-1' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0 3660001' MONTH TO DAY");
  TestIntervalLiteralError("INTERVAL '0 -3660001' MONTH TO DAY");
  TestIntervalLiteralError("INTERVAL '0 87840001:0:0' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 -87840001:0:0' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 0:5270400001:0' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 -0:5270400001:0' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 0:0:316224000001' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 -0:0:316224000001' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 0:0:316224000000.000000001' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 -0:0:316224000000.000000001' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 87840000:0:0.000000001' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 -87840000:0:0.000000001' DAY TO SECOND");

  // Numbers too large to fit into int64
  TestIntervalLiteralError("INTERVAL '9223372036854775808-0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '-9223372036854775808-0' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0-9223372036854775808' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '-0-9223372036854775808' YEAR TO MONTH");
  TestIntervalLiteralError("INTERVAL '0 9223372036854775808' MONTH TO DAY");
  TestIntervalLiteralError("INTERVAL '0 -9223372036854775808' MONTH TO DAY");
  TestIntervalLiteralError(
      "INTERVAL '0 9223372036854775808:0:0' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 -9223372036854775808:0:0' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 0:9223372036854775808:0' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 -0:9223372036854775808:0' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 0:0:9223372036854775808' DAY TO SECOND");
  TestIntervalLiteralError(
      "INTERVAL '0 -0:0:9223372036854775808' DAY TO SECOND");

  // Too many fractional digits
  TestIntervalLiteralError("INTERVAL '0-0 0 0:0:0.0000000000' YEAR TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 0 0:0:0.0000000000' MONTH TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 0:0:0.0000000000' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0:0:0.0000000000' HOUR TO SECOND");
  TestIntervalLiteralError("INTERVAL '0:0.0000000000' MINUTE TO SECOND");

  // Trailing dot
  TestIntervalLiteralError("INTERVAL '0-0 0 0:0:0.' YEAR TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 0 0:0:0.' MONTH TO SECOND");
  TestIntervalLiteralError("INTERVAL '0 0:0:0.' DAY TO SECOND");
  TestIntervalLiteralError("INTERVAL '0:0:0.' HOUR TO SECOND");
  TestIntervalLiteralError("INTERVAL '0:0.' MINUTE TO SECOND");

  // Unsupported combinations of dateparts
  TestIntervalLiteralError("INTERVAL '0' YEAR TO YEAR");
  TestIntervalLiteralError("INTERVAL '0-0' MONTH TO YEAR");
  TestIntervalLiteralError("INTERVAL '0:0' MINUTE TO HOUR");
  TestIntervalLiteralError("INTERVAL '0:0:0' SECOND TO HOUR");
}

TEST_F(ResolverTest, TestRangeLiteral) {
  // Test ranges of supported types
  TestRangeLiteral("[2022-01-01, 2022-02-02)",
                   R"sql(RANGE<DATE> "[2022-01-01, 2022-02-02)")sql");
  TestRangeLiteral(
      "[2022-09-13 16:36:11.000000001, 2022-09-13 16:37:11.000000001)",
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, 2022-09-13 16:37:11.000000001)")sql");
  TestRangeLiteral(
      "[0001-01-01 00:00:00+00, 9999-12-31 23:59:59.999999999+00)",
      R"sql(RANGE<TIMESTAMP> "[0001-01-01 00:00:00+00, 9999-12-31 23:59:59.999999999+00)")sql");

  // Test ranges with UNBOUNDED
  TestRangeLiteral("[NULL, 2022-02-02)",
                   R"sql(RANGE<DATE> "[UNBOUNDED, 2022-02-02)")sql");
  TestRangeLiteral(
      "[2022-09-13 16:36:11.000000001, NULL)",
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, unbounded)")sql");
  TestRangeLiteral("[NULL, NULL)",
                   R"sql(RANGE<TIMESTAMP> "[Unbounded, Unbounded)")sql");

  // Test ranges with NULL. NULL could be used instead of UNBOUNDED
  TestRangeLiteral("[NULL, 2022-02-02)",
                   R"sql(RANGE<DATE> "[NULL, 2022-02-02)")sql");
  TestRangeLiteral(
      "[2022-09-13 16:36:11.000000001, NULL)",
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, null)")sql");
  TestRangeLiteral("[NULL, NULL)", R"sql(RANGE<TIMESTAMP> "[Null, Null)")sql");

  // Test invalid ranges
  // Supplied start and end values have type different than specified
  TestRangeLiteralError(
      R"sql(RANGE<DATE> "[0001-01-01 00:00:00+00, 9999-12-31 23:59:59.999999999+00)")sql");
  // Supplied start and end values have different types
  TestRangeLiteralError(
      R"sql(RANGE<DATE> "[2022-01-01, 2022-09-13 16:37:11.000000001)")sql");
  // Unsupported type
  TestRangeLiteralError(R"sql(RANGE<INT64> "[1, 100)")sql");
  // Literal with more than two parts
  TestRangeLiteralError(
      R"sql(RANGE<DATE> "[2022-01-01, 2022-02-02, 2022-03-02)")sql");
  // Literal with less than two parts
  TestRangeLiteralError(R"sql(RANGE<DATE> "[)")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "[2022-01-01)")sql");
  // Literal with incorrect values
  TestRangeLiteralError(R"sql(RANGE<DATE> "[01/01/2022, 02/02/2022)")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "[,)")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "[2022-01-01, )")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "[, 2022-01-01)")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "[0000-01-01, 0000-01-02)")sql");
  // Literal with invalid brackets
  TestRangeLiteralError(R"sql(RANGE<DATE> "[2022-01-01, 2022-02-02]")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "(2022-01-01, 2022-02-02]")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "(2022-01-01, 2022-02-02)")sql");
  // Start <= end
  TestRangeLiteralError(R"sql(RANGE<DATE> "[2022-01-02, 2022-01-01)")sql");
  TestRangeLiteralError(
      R"sql(RANGE<DATETIME> "[2022-09-19 00:01:00.0, 2022-09-19 00:00:00.0)")sql");
  TestRangeLiteralError(
      R"sql(RANGE<TIMESTAMP> "[0001-01-01 00:00:00+00, 0001-01-01 00:00:00+00)")sql");
  // Range literal must have exactly one space after , and no other spaces
  TestRangeLiteralError(R"sql(RANGE<DATE> "[2022-01-01,2022-02-02)")sql");
  TestRangeLiteralError(R"sql(RANGE<DATE> "[ 2022-01-01 , 2022-02-02 )")sql");
}

TEST_F(ResolverTest, TestSpecialFunctionName) {
  std::unique_ptr<ParserOutput> parser_output;
  std::unique_ptr<const ResolvedStatement> resolved_statement;
  std::string sql = "SELECT nested_catalog.udf.timestamp_add(1,2,3)";
  ResetResolver(sample_catalog_->catalog());
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  GOOGLESQL_EXPECT_OK(resolver_->ResolveStatement(sql, parser_output->statement(),
                                        &resolved_statement));
}

TEST_F(ResolverTest, TestGetFunctionNameAndArguments) {
  for (auto& [sql_in, expected_function_name_path,
              expected_function_arguments] : {
           std::make_tuple("COUNT(*)", std::vector<std::string>{"$count_star"},
                           std::vector<std::string>{}),
           std::make_tuple("CURRENT_DATE()",
                           std::vector<std::string>{"CURRENT_DATE"},
                           std::vector<std::string>{}),
           std::make_tuple("COUNT(DISTINCT x)",
                           std::vector<std::string>{"COUNT"},
                           std::vector<std::string>{"x\n"}),
           std::make_tuple("udfs.normalize(*)",
                           std::vector<std::string>{"udfs", "normalize"},
                           std::vector<std::string>{"*\n"}),
           std::make_tuple(
               "my_dataset.udfs.count(*)",
               std::vector<std::string>{"my_dataset", "udfs", "count"},
               std::vector<std::string>{"*\n"}),
       }) {
    SCOPED_TRACE(sql_in);

    // Parse String query to ASTFunctionCall
    std::unique_ptr<ParserOutput> parser_output;
    GOOGLESQL_ASSERT_OK(ParseExpression(sql_in, ParserOptions(), &parser_output))
        << sql_in;
    const ASTExpression* parsed_expression = parser_output->expression();
    ASSERT_EQ(parsed_expression->node_kind(), AST_FUNCTION_CALL);
    const ASTFunctionCall* function_call =
        parsed_expression->GetAsOrDie<ASTFunctionCall>();

    // Use ASTFunctionCall to get function name and arguments
    std::vector<std::string> function_name_path;
    std::vector<const ASTExpression*> function_arguments;
    std::map<int, Resolver::SpecialArgumentType> argument_option_map;
    QueryResolutionInfo query_resolution_info(resolver_.get());
    GOOGLESQL_EXPECT_OK(resolver_->GetFunctionNameAndArguments(
        function_call, &function_name_path, &function_arguments,
        &argument_option_map, &query_resolution_info));
    EXPECT_THAT(function_name_path,
                testing::ElementsAreArray(expected_function_name_path));

    EXPECT_EQ(expected_function_arguments.size(), function_arguments.size());
    for (int i = 0; i < expected_function_arguments.size(); ++i) {
      EXPECT_EQ(expected_function_arguments[i], Unparse(function_arguments[i]));
    }
  }
}

TEST_F(ResolverTest, TestExpressionVolatility) {
  TestExpressionVolatility(R"sql(volatile_function(1))sql",
                           /*expected_volatile=*/true);
  TestExpressionVolatility(R"sql(stable_function(1))sql",
                           /*expected_volatile=*/false);
  TestExpressionVolatility(R"sql(stable_function(volatile_function(1)))sql",
                           /*expected_volatile=*/true);
}

TEST(FunctionArgumentInfoTest, BasicUse) {
  FunctionArgumentInfo info;
  IdString name1 = IdString::MakeGlobal("name1");
  EXPECT_FALSE(info.HasArg(name1));
  EXPECT_EQ(info.FindTableArg(name1), nullptr);
  EXPECT_EQ(info.FindScalarArg(name1), nullptr);
  EXPECT_FALSE(info.contains_templated_arguments());
  EXPECT_THAT(info.ArgumentNames(), testing::ElementsAre());
  EXPECT_THAT(info.SignatureArguments(), testing::ElementsAre());

  GOOGLESQL_ASSERT_OK(info.AddScalarArg(name1, ResolvedArgumentDef::SCALAR,
                              FunctionArgumentType(types::Int64Type()),
                              /*annotation_map=*/nullptr));
  EXPECT_TRUE(info.HasArg(name1));
  EXPECT_EQ(info.FindTableArg(name1), nullptr);
  EXPECT_NE(info.FindScalarArg(name1), nullptr);

  // Test adding a concrete scalar argument and observing its details.
  {
    IdString name1_caps = IdString::MakeGlobal("NAME1");
    EXPECT_TRUE(info.HasArg(name1_caps));
    EXPECT_EQ(info.FindTableArg(name1), nullptr);

    const FunctionArgumentInfo::ArgumentDetails* details1 =
        info.FindScalarArg(name1);
    const FunctionArgumentInfo::ArgumentDetails* details2 =
        info.FindArg(name1_caps);
    EXPECT_NE(details1, nullptr);
    EXPECT_EQ(details1, details2);
    EXPECT_EQ(details1->name.ToStringView(), "name1");
    EXPECT_EQ(details1->arg_kind, ResolvedArgumentDef::SCALAR);
    EXPECT_TRUE(details1->arg_type.type()->IsInt64());
  }

  // Do not accept duplicate argument names.
  EXPECT_THAT(info.AddScalarArg(name1, ResolvedArgumentDef::SCALAR,
                                FunctionArgumentType(types::Int64Type()),
                                /*annotation_map=*/nullptr),
              absl_testing::StatusIs(absl::StatusCode::kInternal));

  // Test adding a concrete relation argument and observing its details.
  {
    IdString name2 = IdString::MakeGlobal("name2");
    TVFRelation schema({TVFSchemaColumn("col1", types::StringType())});
    FunctionArgumentTypeOptions options(
        schema, /*extra_relation_input_columns_allowed=*/true);
    GOOGLESQL_ASSERT_OK(info.AddRelationArg(
        name2, FunctionArgumentType(SignatureArgumentKind::ARG_TYPE_RELATION,
                                    options)));
    EXPECT_TRUE(info.HasArg(name2));
    EXPECT_EQ(info.FindScalarArg(name2), nullptr);
    EXPECT_FALSE(info.contains_templated_arguments());

    const FunctionArgumentInfo::ArgumentDetails* details1 =
        info.FindTableArg(name2);
    const FunctionArgumentInfo::ArgumentDetails* details2 = info.FindArg(name2);
    EXPECT_NE(details1, nullptr);
    EXPECT_EQ(details1, details2);
    EXPECT_EQ(details1->name.ToStringView(), "name2");
    EXPECT_TRUE(details1->arg_type.IsRelation());
  }

  // Test adding a sclar template argument
  {
    IdString name3 = IdString::MakeGlobal("name3");
    GOOGLESQL_ASSERT_OK(info.AddScalarArg(
        name3, ResolvedArgumentDef::SCALAR,
        FunctionArgumentType(SignatureArgumentKind::ARG_TYPE_ARBITRARY,
                             /*num_occurrences=*/1),
        /*annotation_map=*/nullptr));
    EXPECT_TRUE(info.contains_templated_arguments());
    EXPECT_TRUE(info.HasArg(name3));
    EXPECT_EQ(info.FindTableArg(name3), nullptr);
    EXPECT_NE(info.FindScalarArg(name3), nullptr);
  }

  // Testa adding a relation template argument
  {
    IdString name4 = IdString::MakeGlobal("name4");
    FunctionArgumentTypeOptions empty_options;
    GOOGLESQL_ASSERT_OK(info.AddRelationArg(
        name4, FunctionArgumentType(SignatureArgumentKind::ARG_TYPE_RELATION,
                                    empty_options,
                                    /*num_occurrences=*/1)));
    EXPECT_TRUE(info.HasArg(name4));
    EXPECT_NE(info.FindTableArg(name4), nullptr);
    EXPECT_EQ(info.FindScalarArg(name4), nullptr);
    EXPECT_TRUE(info.contains_templated_arguments());
  }

  // Make sure the collection outputs have the right number of entries.
  EXPECT_THAT(info.ArgumentNames(),
              testing::ElementsAre("name1", "name2", "name3", "name4"));
  EXPECT_EQ(info.SignatureArguments().size(), 4);
}

}  // namespace googlesql
