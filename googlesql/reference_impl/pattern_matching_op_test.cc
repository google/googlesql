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

// Tests of analytic function code.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/match_recognize/test_pattern_resolver.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/test_relational_op.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/tuple_test_util.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

using googlesql::values::Bool;
using googlesql::values::Int64;
using googlesql::values::NullInt64;
using googlesql::values::NullString;
using googlesql::values::String;

namespace googlesql {
namespace {

EvaluationOptions GetScramblingEvaluationOptions() {
  EvaluationOptions options;
  options.scramble_undefined_orderings = true;
  return options;
}

EvaluationOptions GetIntermediateMemoryEvaluationOptions(int64_t total_bytes) {
  EvaluationOptions options;
  options.max_intermediate_byte_size = total_bytes;
  return options;
}

class PatternMatchingOpTest : public ::testing::Test {
 protected:
  absl::StatusOr<std::unique_ptr<PatternMatchingOp>> CreatePatternMatchingOp() {
    VariableId a("a"), b("b"), c("c");
    VariableId match_id("match_id"), row_number("row_number"),
        assigned_label("assigned_label"), is_sentinel("is_sentinel");

    // PatternMatchingOp expects the input to be sorted by the partition keys,
    // followed by the order keys.
    auto input_op =
        std::make_unique<TestRelationalOp>(input_variables_, input_tuples_,
                                           /*preserves_order=*/true);

    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_a, DerefExpr::Create(a, types::Int64Type()));
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_b, DerefExpr::Create(b, types::Int64Type()));

    std::vector<std::unique_ptr<KeyArg>> partition_keys;
    partition_keys.emplace_back(
        std::make_unique<KeyArg>(a, std::move(deref_a), KeyArg::kAscending));

    std::vector<std::unique_ptr<KeyArg>> order_keys;
    order_keys.emplace_back(
        std::make_unique<KeyArg>(b, std::move(deref_b), KeyArg::kAscending));

    functions::match_recognize::TestPatternResolver pattern_resolver;

    // Intentionally choosing a pattern with a query parameter so we can test
    // code paths in PatternMatchingOp that plumb the param value to the
    // CompiledPattern library.
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto resolved_pattern,
        pattern_resolver.ResolvePattern(
            "p q{@n}",
            {.parameters = QueryParametersMap{{"n", types::Int64Type()}}}));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto pattern,
        functions::match_recognize::CompiledPattern::Create(
            *resolved_pattern, functions::match_recognize::PatternOptions{}));

    std::vector<std::unique_ptr<ValueExpr>> predicates;

    LanguageOptions language_options;
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_c1, DerefExpr::Create(c, types::Int64Type()));
    std::vector<std::unique_ptr<ValueExpr>> c1_arg;
    c1_arg.push_back(std::move(deref_c1));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto c_is_null,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kIsNull, language_options, types::BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(c1_arg)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    GOOGLESQL_ASSIGN_OR_RETURN(auto always_true, ConstExpr::Create(Bool(true)));

    predicates.emplace_back(std::move(c_is_null));
    predicates.emplace_back(std::move(always_true));

    std::vector<std::string> param_keys = {"n"};
    std::vector<std::unique_ptr<ValueExpr>> param_values;
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_n,
                     DerefExpr::Create(VariableId("n"), types::Int64Type()));
    std::vector<std::unique_ptr<ValueExpr>> n_arg;
    n_arg.push_back(std::move(deref_n));

    return PatternMatchingOp::Create(
        std::move(partition_keys), std::move(order_keys),
        /*match_result_variables=*/
        {match_id, row_number, assigned_label, is_sentinel},
        /*pattern_variable_names=*/{"p", "q"}, std::move(predicates),
        std::move(pattern), param_keys,
        /*query_parameter_values=*/std::move(n_arg), std::move(input_op));
  }

  void SetUp() override {
    params_variables_ = std::make_unique<std::vector<VariableId>>();
    params_variables_->push_back(VariableId("n"));
    params_schema_ = std::make_unique<TupleSchema>(*params_variables_);
  }

  // For readability.
  std::vector<const TupleSchema*> ParamsSchemas() {
    return {params_schema_.get()};
  }
  std::vector<const TupleData*> ParamsData() { return {&params_data_[0]}; }

  functions::match_recognize::TestPatternResolver pattern_resolver_;

  std::unique_ptr<std::vector<VariableId>> params_variables_;
  std::unique_ptr<TupleSchema> params_schema_;
  const std::vector<TupleData> params_data_ =
      CreateTestTupleDatas({{Int64(1)}});

  const std::vector<VariableId> input_variables_ = {
      VariableId("a"), VariableId("b"), VariableId("c")};
  const std::vector<TupleData> input_tuples_ =
      CreateTestTupleDatas({{Int64(0), NullInt64(), Int64(1)},
                            {Int64(0), NullInt64(), NullInt64()},
                            {Int64(0), Int64(1), Int64(1)},
                            {Int64(0), Int64(1), NullInt64()},
                            {Int64(1), Int64(1), NullInt64()},
                            {Int64(1), Int64(2), Int64(1)},
                            {Int64(1), Int64(2), Int64(2)},
                            {Int64(1), Int64(3), Int64(1)}});
};

TEST_F(PatternMatchingOpTest, CorrectlyReturnsMatches) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<PatternMatchingOp> pattern_matching_op,
                       CreatePatternMatchingOp());

  GOOGLESQL_ASSERT_OK(pattern_matching_op->SetSchemasForEvaluation(ParamsSchemas()));

  std::unique_ptr<TupleSchema> output_schema =
      pattern_matching_op->CreateOutputSchema();
  EXPECT_THAT(
      output_schema->variables(),
      ElementsAre(VariableId("a"), VariableId("b"), VariableId("c"),
                  VariableId("match_id"), VariableId("row_number"),
                  VariableId("assigned_label"), VariableId("is_sentinel")));

  EvaluationContext context((EvaluationOptions()));

  // Create an iterator and test it.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      pattern_matching_op->CreateIterator(ParamsData(),
                                          /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "PatternMatchingTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));

  const std::vector<TupleData> expected_tuples = CreateTestTupleDatas({
      // 1st match is from the 1st partition, with a sentinel row.
      {Int64(0), NullInt64(), NullInt64(), Int64(1), Int64(2), String("p"),
       Bool(false)},
      {Int64(0), Int64(1), Int64(1), Int64(1), Int64(3), String("q"),
       Bool(false)},
      // Sentinel row. Partition keys must be set, as well as `match_id` and
      // `is_sentinel`.
      {Int64(0), NullInt64(), Int64(1), Int64(1), NullInt64(), NullString(),
       Bool(true)},
      // 2nd match is from the 2nd partition, with a sentinel row
      {Int64(1), Int64(1), NullInt64(), Int64(1), Int64(1), String("p"),
       Bool(false)},
      {Int64(1), Int64(2), Int64(1), Int64(1), Int64(2), String("q"),
       Bool(false)},
      // Sentinel row. Partition keys must be set, as well as `match_id` and
      // `is_sentinel`.
      {Int64(1), Int64(1), NullInt64(), Int64(1), NullInt64(), NullString(),
       Bool(true)},
  });

  ASSERT_EQ(data.size(), expected_tuples.size());
  for (int i = 0; i < expected_tuples.size(); ++i) {
    EXPECT_EQ(Tuple(output_schema.get(), &data[i]).DebugString(),
              Tuple(output_schema.get(), &expected_tuples[i]).DebugString());
    // Check for the extra slot.
    EXPECT_EQ(data[i].num_slots(), output_schema->num_variables());
  }

  // Check that scrambling works
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, pattern_matching_op->CreateIterator(
                                 ParamsData(),
                                 /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(PatternMatchingTupleIterator("
            "TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), expected_tuples.size());
}

TEST_F(PatternMatchingOpTest, Cancellation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<PatternMatchingOp> pattern_matching_op,
                       CreatePatternMatchingOp());

  GOOGLESQL_ASSERT_OK(pattern_matching_op->SetSchemasForEvaluation(ParamsSchemas()));

  std::unique_ptr<TupleSchema> output_schema =
      pattern_matching_op->CreateOutputSchema();
  EXPECT_THAT(
      output_schema->variables(),
      ElementsAre(VariableId("a"), VariableId("b"), VariableId("c"),
                  VariableId("match_id"), VariableId("row_number"),
                  VariableId("assigned_label"), VariableId("is_sentinel")));

  EvaluationContext context((EvaluationOptions()));
  context.ClearDeadlineAndCancellationState();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      pattern_matching_op->CreateIterator(ParamsData(),
                                          /*num_extra_slots=*/1, &context));

  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  std::vector<TupleData> data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));
}

TEST_F(PatternMatchingOpTest, ReturnsErrorIfMemorIsTooLow) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<PatternMatchingOp> pattern_matching_op,
                       CreatePatternMatchingOp());

  GOOGLESQL_ASSERT_OK(pattern_matching_op->SetSchemasForEvaluation(ParamsSchemas()));

  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/1000));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> memory_iter,
                       pattern_matching_op->CreateIterator(
                           ParamsData(),
                           /*num_extra_slots=*/1, &memory_context));
  EXPECT_THAT(ReadFromTupleIterator(memory_iter.get()),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

}  // namespace
}  // namespace googlesql
