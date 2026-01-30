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

#include "googlesql/resolved_ast/resolved_ast_builder.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
using ::absl_testing::StatusIs;

class BuilderTest : public ::testing::Test {
 protected:
  const SimpleTable t1_{"t1"};
  const SimpleTable v1_{"v1"};
};

TEST(Builders, CanChainBuild) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> literal,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Double(-2.0))
                           .set_has_explicit_type(true)
                           .set_type(types::DoubleType())
                           .Build());
  EXPECT_EQ(-2.0, literal->value().double_value());
  EXPECT_TRUE(literal->has_explicit_type());
}

TEST(Builders, AreMoveConstructible) {
  ResolvedLiteralBuilder builder1;
  builder1.set_value(Value::Double(-2.0))
      .set_type(types::DoubleType())
      .set_has_explicit_type(true);

  ResolvedLiteralBuilder builder2(std::move(builder1));
  // Note that we do not get any missing field errors
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> literal,
                       std::move(builder2).Build());

  EXPECT_EQ(-2.0, literal->value().double_value());
  EXPECT_TRUE(literal->has_explicit_type());

  ResolvedFunctionCallBuilder incomplete_call_builder;
  incomplete_call_builder.add_argument_list(ResolvedLiteralBuilder());

  // Inner status about the literal is carried over.
  ResolvedFunctionCallBuilder call_builder(std::move(incomplete_call_builder));
  call_builder.set_type(types::BoolType()).set_function(nullptr);
  EXPECT_THAT(
      std::move(call_builder).Build(),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("ResolvedLiteral::type was not set on the builder; "
                    "ResolvedLiteral::value was not set on the builder; ")));
}

TEST(Builders, AreMoveAssignable) {
  ResolvedLiteralBuilder builder1;
  builder1.set_value(Value::Double(-2.0))
      .set_type(types::DoubleType())
      .set_has_explicit_type(true);

  ResolvedLiteralBuilder builder2;
  builder2 = std::move(builder1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> literal,
                       std::move(builder2).Build());
  EXPECT_EQ(-2.0, literal->value().double_value());
  EXPECT_TRUE(literal->has_explicit_type());

  ResolvedFunctionCallBuilder incomplete_call_builder;
  incomplete_call_builder.add_argument_list(ResolvedLiteralBuilder());

  // Inner status about the literal is carried over.
  ResolvedFunctionCallBuilder call_builder;

  call_builder = std::move(incomplete_call_builder);
  call_builder.set_type(types::BoolType()).set_function(nullptr);
  EXPECT_THAT(
      std::move(call_builder).Build(),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("ResolvedLiteral::type was not set on the builder; "
                    "ResolvedLiteral::value was not set on the builder; ")));
}

TEST(Builders, CanBuildFromScratchAsStackObject) {
  ResolvedLiteralBuilder builder;
  builder.set_value(Value::Double(-2.0)).set_type(types::DoubleType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> literal,
                       std::move(builder).Build());
  EXPECT_EQ(-2.0, literal->value().double_value());
}

TEST(Builders, ModifiesNodeInPlace) {
  std::unique_ptr<const ResolvedLiteral> literal =
      MakeResolvedLiteral(Value::Double(-2.0));
  const ResolvedLiteral* const old_address = literal.get();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedLiteral> modified_literal,
      ToBuilder(std::move(literal)).set_value(Value::Double(10.0)).Build());
  EXPECT_EQ(old_address, modified_literal.get());
}

TEST_F(BuilderTest, CanBuildNodesAndVectorsFromScratch) {
  ResolvedColumn column;
  auto input_scan = MakeResolvedTableScan({column}, &t1_, nullptr, "a1");
  const ResolvedTableScan* const input_scan_old_address = input_scan.get();

  auto column_ref =
      MakeResolvedColumnRef(types::BoolType(), column, /*is_correlated*/ false);
  const ResolvedColumnRef* const column_ref_old_address = column_ref.get();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const auto filter_scan,
                       ResolvedFilterScanBuilder()
                           .set_input_scan(std::move(input_scan))
                           .add_column_list(column)
                           .set_filter_expr(std::move(column_ref))
                           .Build());

  EXPECT_EQ(input_scan_old_address, filter_scan->input_scan());
  EXPECT_EQ(&t1_,
            filter_scan->input_scan()->GetAs<ResolvedTableScan>()->table());
  EXPECT_EQ(column_ref_old_address, filter_scan->filter_expr());
  EXPECT_EQ(column,
            filter_scan->filter_expr()->GetAs<ResolvedColumnRef>()->column());
  EXPECT_EQ(std::vector<ResolvedColumn>{column}, filter_scan->column_list());
}

TEST(Builders, BuildResetsTheAccessedBit) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> literal,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Double(-2.0))
                           .set_type(types::DoubleType())
                           .Build());
  GOOGLESQL_ASSERT_OK(literal->CheckNoFieldsAccessed());

  ResolvedLiteralBuilder builder = ToBuilder(std::move(literal));
  builder.set_value(Value::Double(builder.value().double_value() + 1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedLiteral> incremented_literal,
      std::move(builder).Build());

  GOOGLESQL_EXPECT_OK(incremented_literal->CheckNoFieldsAccessed());
}

TEST_F(BuilderTest, CanModifydNodesAndVectors) {
  ResolvedColumn column;
  auto input_scan = MakeResolvedTableScan({column}, &t1_, nullptr, "a1");
  const ResolvedTableScan* const input_scan_old_address = input_scan.get();

  auto column_ref =
      MakeResolvedColumnRef(types::BoolType(), column, /*is_correlated*/ false);

  auto filter_scan = MakeResolvedFilterScan({column}, std::move(input_scan),
                                            std::move(column_ref));
  EXPECT_EQ(1, filter_scan->column_list_size());

  ResolvedTableScanBuilder input_scan_builder = ToBuilder(absl::WrapUnique(
      filter_scan->release_input_scan().release()->GetAs<ResolvedTableScan>()));
  ASSERT_EQ(&t1_, input_scan_builder.table());

  input_scan_builder.set_table(&v1_);
  ASSERT_EQ(&v1_, input_scan_builder.table());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto modified_input_scan,
                       std::move(input_scan_builder).Build());
  auto filter_builder = ToBuilder(std::move(filter_scan));
  filter_builder.set_input_scan(std::move(modified_input_scan))
      .add_column_list(column);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto modified_filter, std::move(filter_builder).Build());

  EXPECT_EQ(input_scan_old_address, modified_filter->input_scan());
  EXPECT_EQ(&v1_,
            modified_filter->input_scan()->GetAs<ResolvedTableScan>()->table());
  EXPECT_EQ((std::vector<ResolvedColumn>{column, column}),
            modified_filter->column_list());
}

TEST_F(BuilderTest, CanBuildFunctionCallsIncrementally) {
  ResolvedFunctionCallBuilder builder;  // Zero-arg ctor
  builder.set_type(types::BoolType());

  FunctionSignature signature(types::BoolType(), {}, /*context id*/ nullptr);
  Function function("fn1", Function::kGoogleSQLFunctionGroupName,
                    Function::SCALAR);
  builder.set_function(&function).set_signature(signature);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto function_call, std::move(builder).Build());

  EXPECT_EQ(signature.result_type().type(),
            function_call->signature().result_type().type());

  EXPECT_EQ(signature.arguments().size(),
            function_call->signature().arguments().size());

  EXPECT_EQ(signature.context_id(), function_call->signature().context_id());

  EXPECT_NE(&signature, &function_call->signature())
      << "FunctionSignature should be copied by value";
}

TEST_F(BuilderTest, SetterAcceptsBuilderAndStatusOr) {
  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> literal_status_or =
      ResolvedLiteralBuilder()
          .set_value(Value::Bool(false))
          .set_type(types::BoolType())
          .Build();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto filter_scan,
      ResolvedFilterScanBuilder()
          .set_input_scan(ResolvedTableScanBuilder()
                              .set_table(&t1_)
                              .set_for_system_time_expr(nullptr))
          .set_filter_expr(std::move(literal_status_or))
          .Build());

  EXPECT_EQ(&t1_,
            filter_scan->input_scan()->GetAs<ResolvedTableScan>()->table());
}

TEST_F(BuilderTest, AdderAcceptsBuilderAndStatusOr) {
  absl::StatusOr<std::unique_ptr<const ResolvedColumnRef>>
      column_ref1_status_or = ResolvedColumnRefBuilder()
                                  .set_column(ResolvedColumn{})
                                  .set_is_correlated(false)
                                  .set_type(types::StringType())
                                  .Build();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto lambda_arg,
      ResolvedInlineLambdaBuilder()
          .add_parameter_list(std::move(column_ref1_status_or))
          .add_parameter_list(ResolvedColumnRefBuilder()
                                  .set_column(ResolvedColumn{})
                                  .set_is_correlated(false)
                                  .set_type(types::StringType()))
          .set_body(ResolvedLiteralBuilder()
                        .set_value(Value::Bool(false))
                        .set_type(types::BoolType()))
          .Build());

  EXPECT_EQ(types::StringType(), lambda_arg->parameter_list(0)->type());
}

TEST_F(BuilderTest, ReturnsErrorStatusOnMissingRequiredField) {
  // Inner status is reported in the message. Notice the prefixes on the names
  // of missing fields.
  EXPECT_THAT(
      ResolvedFunctionCallBuilder()
          .add_argument_list(ResolvedLiteralBuilder())
          .Build(),
      StatusIs(
          absl::StatusCode::kInternal,
          AllOf(
              HasSubstr("ResolvedLiteral::value was not set on the builder"),
              // The outer missing places are also reported in the message.
              HasSubstr(
                  "ResolvedFunctionCall::type was not set on the builder"),
              HasSubstr(
                  "ResolvedFunctionCall::function was not set on the builder"),
              HasSubstr(
                  "ResolvedFunctionCall::signature was not set on the builder"),
              // ERROR_MODE is an enum so it is not required as it is not
              // explicitly marked as such.
              Not(HasSubstr("ResolvedFunctionCall::error_mode was not set on "
                            "the builder")),
              // argument_list is a vector so it is not required as it is not
              // explicitly marked as such.
              Not(HasSubstr("ResolvedFunctionCall::argument_list "
                            "was not set on the builder")))));
}

TEST_F(BuilderTest, RequiredFieldsAreSatisfiedEvenWithABadBuilder) {
  EXPECT_THAT(
      ResolvedInlineLambdaBuilder().set_body(ResolvedLiteralBuilder()).Build(),
      StatusIs(
          absl::StatusCode::kInternal,
          AllOf(
              HasSubstr("ResolvedLiteral::value was not set on the builder"),
              // Body was set, even if the value turned out to be bad and we
              // we couldn't build it.
              Not(HasSubstr(
                  "ResolvedInlineLambda::body was not set on the builder")))));
}

TEST_F(BuilderTest, RequiredFieldsAreSatisfiedEvenWithABadInputStatus) {
  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> bad_literal =
      absl::InternalError("Some GOOGLESQL_RET_CHECK() failure");

  EXPECT_THAT(
      ResolvedInlineLambdaBuilder().set_body(std::move(bad_literal)).Build(),
      StatusIs(absl::StatusCode::kInternal,
               AllOf(HasSubstr("Some GOOGLESQL_RET_CHECK() failure"),
                     // Body was set, even if the value turned out to be bad and
                     // we we couldn't build it.
                     Not(HasSubstr("ResolvedInlineLambda::body was not set on "
                                   "the builder")))));
}

}  // namespace
}  // namespace googlesql
