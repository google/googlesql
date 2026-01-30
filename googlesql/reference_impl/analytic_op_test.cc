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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_value.h"
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
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/status_macros.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

static const auto DEFAULT_ERROR_MODE =
    ResolvedFunctionCallBase::DEFAULT_ERROR_MODE;

// For readability.
std::vector<const TupleSchema*> EmptyParamsSchemas() { return {}; }
std::vector<const TupleData*> EmptyParams() { return {}; }

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

// Determinism of the output of an analytic function call.
// When it is AnalyticOutputDeterminism::kDeterministic, then the output
// of the analytic function call does not change when the input is in
// a different total order while still in the associated
// window order.
enum class AnalyticOutputDeterminism {
  kDeterministic,
  kNonDeterministic
};

// Contains a TupleComparator with its order keys and collators to
// determine whether two tuples are peers in a window ordering.
struct WindowOrderingComparatorInfo {
  // Takes ownership of all pointers.
  WindowOrderingComparatorInfo(std::unique_ptr<TupleComparator> comparator_in,
                               std::vector<const KeyArg*> order_keys_in)
      : comparator(std::move(comparator_in)),
        order_keys(std::move(order_keys_in)),
        order_keys_deleter(&order_keys) {}

  const std::unique_ptr<TupleComparator> comparator;

  std::vector<const KeyArg*> order_keys;  // Owned.
  googlesql_base::ElementDeleter order_keys_deleter;
};

struct AnalyticFunctionTestCase {
  // This constructor leaves null_handling_modifier as the default value.
  AnalyticFunctionTestCase(
      FunctionKind function_kind_in,
      const std::vector<VariableId>& input_variables_in,
      const std::vector<TupleData>& input_tuples_in,
      const std::vector<std::vector<Value>>& arguments_in,
      const std::vector<AnalyticWindow>& windows_in,
      const std::shared_ptr<WindowOrderingComparatorInfo>&
          window_ordering_comparator_info_in,
      const std::vector<Value>& expected_result_in,
      AnalyticOutputDeterminism output_determinism_in,
      const absl::Status& expected_status_in = absl::Status())
      : function_kind(function_kind_in),
        input_variables(input_variables_in),
        input_tuples(input_tuples_in),
        arguments(arguments_in),
        windows(windows_in),
        window_ordering_comparator_info(window_ordering_comparator_info_in),
        expected_result(expected_result_in),
        output_determinism(output_determinism_in),
        expected_status(expected_status_in) {}
  AnalyticFunctionTestCase(
      FunctionKind function_kind_in,
      const std::vector<VariableId>& input_variables_in,
      const std::vector<TupleData>& input_tuples_in,
      const std::vector<std::vector<Value>>& arguments_in,
      ResolvedAnalyticFunctionCall::NullHandlingModifier
          null_handling_modifier_in,
      const std::vector<AnalyticWindow>& windows_in,
      const std::shared_ptr<WindowOrderingComparatorInfo>&
          window_ordering_comparator_info_in,
      const std::vector<Value>& expected_result_in,
      AnalyticOutputDeterminism output_determinism_in,
      const absl::Status& expected_status_in = absl::Status())
      : function_kind(function_kind_in),
        input_variables(input_variables_in),
        input_tuples(input_tuples_in),
        arguments(arguments_in),
        null_handling_modifier(null_handling_modifier_in),
        windows(windows_in),
        window_ordering_comparator_info(window_ordering_comparator_info_in),
        expected_result(expected_result_in),
        output_determinism(output_determinism_in),
        expected_status(expected_status_in) {}

  FunctionKind function_kind;
  std::vector<VariableId> input_variables;
  std::vector<TupleData> input_tuples;
  // Has the same requirements as the argument to AnalyticFunctionBody::Eval().
  // Each element in <arguments> is a vector giving the result of an argument
  // expression. If the argument expression must be constant according to the
  // function specification, the element has a single value that is
  // equal to the constant argument value. Otherwise, the element must be
  // a vector matching 1:1 with <input_tuples>, with each value being the
  // result of the argument expression on a tuple in <input_tuples>.
  std::vector<std::vector<Value>> arguments;
  // Copied from ResolvedAnalyticFunctionCall::null_handling_modifier().
  ResolvedAnalyticFunctionCall::NullHandlingModifier null_handling_modifier =
      ResolvedAnalyticFunctionCall::DEFAULT_NULL_HANDLING;
  // If the function supports the window frame clause, it cannot be
  // empty and must match 1:1 with <input_tuples>.
  std::vector<AnalyticWindow> windows;
  // Cannot be nullptr if the corresponding RequireTupleComparator()
  // for the analytic function returns true.
  std::shared_ptr<WindowOrderingComparatorInfo> window_ordering_comparator_info;

  std::vector<Value> expected_result;  // 1:1 matches with <input_tuples>.
  // Indicates whether the result is expected to be deterministic.
  AnalyticOutputDeterminism output_determinism;
  absl::Status expected_status;
};

std::ostream& operator<<(std::ostream& out,
                         const AnalyticFunctionTestCase& test_case) {
  const TupleSchema tuple_schema(test_case.input_variables);
  std::vector<std::string> tuples_strs;
  for (const TupleData& tuple : test_case.input_tuples) {
    Tuple full_tuple(&tuple_schema, &tuple);
    tuples_strs.push_back(full_tuple.DebugString(true /* verbose */));
  }

  std::vector<std::string> arguments_strs;
  for (const std::vector<Value>& argument_values : test_case.arguments) {
    std::vector<std::string> value_strs;
    for (const Value& argument_value : argument_values) {
      value_strs.push_back(argument_value.DebugString(true /* verbose */));
    }
    arguments_strs.push_back(
        absl::StrCat("{", absl::StrJoin(value_strs, ","), "}"));
  }

  std::vector<std::string> window_strs;
  for (const AnalyticWindow& window : test_case.windows) {
    window_strs.push_back(
        absl::StrCat("[", window.start_tuple_id, ", ", window.num_tuples, "]"));
  }

  std::vector<std::string> order_keys_strs;
  if (test_case.window_ordering_comparator_info != nullptr) {
    for (const KeyArg* order_key :
         test_case.window_ordering_comparator_info->comparator->keys()) {
      order_keys_strs.push_back(
          order_key->DebugInternal("" /* indent */, true /* verbose */));
    }
  }

  return out << absl::StrCat(
             BuiltinFunctionCatalog::GetDebugNameByKind(
                 test_case.function_kind),
             "[input_tuples={", absl::StrJoin(tuples_strs, ","),
             "}, arguments={", absl::StrJoin(arguments_strs, ","),
             "}, windows={", absl::StrJoin(window_strs, ","),
             "}, order_keys={" + absl::StrJoin(order_keys_strs, ","),
             "}, is_deterministic=",
             test_case.output_determinism ==
                 AnalyticOutputDeterminism::kDeterministic,
             "]");
}

std::vector<const TupleData*> GetTupleDataPtrs(
    absl::Span<const TupleData> tuples) {
  std::vector<const TupleData*> ptrs;
  ptrs.reserve(tuples.size());
  for (const TupleData& tuple : tuples) {
    ptrs.push_back(&tuple);
  }
  return ptrs;
}

AnalyticFunctionBody* GenerateAnalyticFunction(
    const AnalyticFunctionTestCase& test_case) {
  switch (test_case.function_kind) {
    case FunctionKind::kDenseRank:
      return new DenseRankFunction();
    case FunctionKind::kRank:
      return new RankFunction();
    case FunctionKind::kRowNumber:
      return new RowNumberFunction();
    case FunctionKind::kIsFirst:
      return new IsFirstFunction();
    case FunctionKind::kIsLast:
      return new IsLastFunction();
    case FunctionKind::kPercentRank:
      return new PercentRankFunction();
    case FunctionKind::kCumeDist:
      return new CumeDistFunction();
    case FunctionKind::kNtile:
      return new NtileFunction();
    case FunctionKind::kFirstValue: {
      if (test_case.expected_result.empty()) {
        // Choose an arbitrary output type.
        return new FirstValueFunction(Int64Type(),
                                      test_case.null_handling_modifier);
      }
      return new FirstValueFunction(
          test_case.expected_result[0].type(),
          test_case.null_handling_modifier);
    }
    case FunctionKind::kLastValue: {
      if (test_case.expected_result.empty()) {
        // Choose an arbitrary output type.
        return new LastValueFunction(Int64Type(),
                                     test_case.null_handling_modifier);
      }
      return new LastValueFunction(
          test_case.expected_result[0].type(),
          test_case.null_handling_modifier);
    }
    case FunctionKind::kNthValue: {
      if (test_case.expected_result.empty()) {
        // Choose an arbitrary output type.
        return new NthValueFunction(Int64Type(),
                                    test_case.null_handling_modifier);
      }
      return new NthValueFunction(
          test_case.expected_result[0].type(),
          test_case.null_handling_modifier);
    }
    case FunctionKind::kLead: {
      ABSL_CHECK_EQ(3, test_case.arguments.size());
      // Infer the output type from the third argument (i.e. default value).
      return new LeadFunction(test_case.arguments[2][0].type());
    }
    case FunctionKind::kLag: {
      ABSL_CHECK_EQ(3, test_case.arguments.size());
      // Infer the output type from the third argument (i.e. default value).
      return new LagFunction(test_case.arguments[2][0].type());
    }
    default:
      ABSL_LOG(FATAL) << "Unsupported analytic function "
                 << BuiltinFunctionCatalog::GetDebugNameByKind(
                        test_case.function_kind);
  }
}

typedef TestWithParam<AnalyticFunctionTestCase> AnalyticFunctionTest;

TEST_P(AnalyticFunctionTest, AnalyticFunctionTest) {
  const AnalyticFunctionTestCase& test_case = GetParam();
  std::unique_ptr<AnalyticFunctionBody> analytic_function(
      GenerateAnalyticFunction(test_case));

  const TupleComparator* window_ordering_comparator = nullptr;
  if (test_case.window_ordering_comparator_info != nullptr) {
    window_ordering_comparator =
        test_case.window_ordering_comparator_info->comparator.get();
  }

  EvaluationContext context((EvaluationOptions()));
  std::vector<Value> actual_result;
  EXPECT_EQ(test_case.expected_status,
            analytic_function->Eval(
                TupleSchema(test_case.input_variables),
                GetTupleDataPtrs(test_case.input_tuples), test_case.arguments,
                test_case.windows, window_ordering_comparator,
                DEFAULT_ERROR_MODE, &context, &actual_result))
      << test_case;

  EXPECT_EQ((test_case.output_determinism ==
             AnalyticOutputDeterminism::kDeterministic),
            context.IsDeterministicOutput())
      << test_case;
  if (test_case.expected_status.ok()) {
    EXPECT_EQ(test_case.expected_result, actual_result)
        << test_case;
  }
}

// Returns a WindowOrderingComparatorInfo that contains a TupleComparator
// to compare tuples by a variable 'var'.
static std::shared_ptr<WindowOrderingComparatorInfo>
GenerateWindowOrderingComparatorBySlot(absl::Span<const VariableId> vars,
                                       int slot_idx) {
  const VariableId var = vars[slot_idx];
  std::vector<const KeyArg*> order_keys{
      new KeyArg(var, DerefExpr::Create(var, types::Int64Type()).value(),
                 KeyArg::kAscending)};
  googlesql_base::ElementDeleter order_keys_deleter(&order_keys);

  // Create() doesn't store 'context'.
  EvaluationContext context((EvaluationOptions()));
  std::unique_ptr<TupleComparator> comparator =
      TupleComparator::Create(order_keys, {slot_idx}, EmptyParams(), &context)
          .value();

  return std::make_shared<WindowOrderingComparatorInfo>(std::move(comparator),
                                                        std::move(order_keys));
}

std::vector<AnalyticFunctionTestCase> GetNumberingFunctionTestCases() {
  VariableId x("x"), y("y");
  const std::vector<VariableId> input_variables = {x, y};

  // Generate a tuple comparator to compare tuples by x and y.
  std::vector<const KeyArg*> order_keys{
      new KeyArg(x, DerefExpr::Create(x, types::Int64Type()).value(),
                 KeyArg::kAscending),
      new KeyArg(y, DerefExpr::Create(y, types::DoubleType()).value(),
                 KeyArg::kDescending)};
  googlesql_base::ElementDeleter order_keys_deleter(&order_keys);

  EvaluationContext context((EvaluationOptions()));
  std::unique_ptr<TupleComparator> comparator_by_x_y =
      TupleComparator::Create(order_keys, /*slots_for_keys=*/{0, 1},
                              EmptyParams(), &context)
          .value();
  const std::shared_ptr<WindowOrderingComparatorInfo> comparator_info_by_x_y =
      std::make_shared<WindowOrderingComparatorInfo>(
          std::move(comparator_by_x_y), std::move(order_keys));

  // Generate a tuple comparator to compare tuples only by x.
  const std::shared_ptr<WindowOrderingComparatorInfo> comparator_info_by_x =
      GenerateWindowOrderingComparatorBySlot(input_variables, 0);

  const std::vector<std::vector<Value>> empty_arguments;
  const std::vector<AnalyticWindow> empty_windows;

  const absl::Status non_positive_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The N value (number of buckets) for the NTILE function "
         "must be positive";
  const absl::Status null_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The N value (number of buckets) for the NTILE function "
         "must not be NULL";

  // Input(x, y): (null, 10), (null, 10), (1, 9), (1, 8), (2, 7), (2, 7),
  //              (2, 6), (3, 5), (3, null), (3, null), (4, null).
  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{NullInt64(), Double(10)},
                            {NullInt64(), Double(10)},
                            {Int64(1), Double(9)},
                            {Int64(1), Double(8)},
                            {Int64(2), Double(7)},
                            {Int64(2), Double(7)},
                            {Int64(2), Double(6)},
                            {Int64(3), Double(5)},
                            {Int64(3), NullDouble()},
                            {Int64(3), NullDouble()},
                            {Int64(4), NullDouble()}});

  return {
      // {function_kind, input_variables, input_tuples, arguments, windows,
      //  comparator, expected_result, output_determinism}

      // DENSE_RANK() OVER (ORDER BY x ASC, y DESC).
      {FunctionKind::kDenseRank,
       input_variables,
       input_tuples,
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {Int64(1), Int64(1), Int64(2), Int64(3), Int64(4), Int64(4), Int64(5),
        Int64(6), Int64(7), Int64(7), Int64(8)},
       AnalyticOutputDeterminism::kDeterministic},

      // RANK() OVER (ORDER BY x ASC, y DESC).
      {FunctionKind::kRank,
       input_variables,
       input_tuples,
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {Int64(1), Int64(1), Int64(3), Int64(4), Int64(5), Int64(5), Int64(7),
        Int64(8), Int64(9), Int64(9), Int64(11)},
       AnalyticOutputDeterminism::kDeterministic},

      // ROW_NUMBER() OVER ().
      {FunctionKind::kRowNumber,
       input_variables,
       input_tuples,
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {Int64(1), Int64(2), Int64(3), Int64(4), Int64(5), Int64(6), Int64(7),
        Int64(8), Int64(9), Int64(10), Int64(11)},
       AnalyticOutputDeterminism::kDeterministic},

      // IS_FIRST(4) OVER (ORDER BY x ASC, y DESC).
      {FunctionKind::kIsFirst,
       input_variables,
       input_tuples,
       {std::vector<Value>(input_tuples.size(), Int64(4))},
       empty_windows,
       comparator_info_by_x_y,
       {Bool(true), Bool(true), Bool(true), Bool(true), Bool(false),
        Bool(false), Bool(false), Bool(false), Bool(false), Bool(false),
        Bool(false)},
       AnalyticOutputDeterminism::kDeterministic},

      // IS_LAST(4) OVER (ORDER BY x ASC, y DESC).
      {FunctionKind::kIsLast,
       input_variables,
       input_tuples,
       {std::vector<Value>(input_tuples.size(), Int64(4))},
       empty_windows,
       comparator_info_by_x_y,
       {Bool(false), Bool(false), Bool(false), Bool(false), Bool(false),
        Bool(false), Bool(false), Bool(true), Bool(true), Bool(true),
        Bool(true)},
       AnalyticOutputDeterminism::kDeterministic},

      // PERCENT_RANK() OVER (ORDER BY x ASC, y DESC).
      {FunctionKind::kPercentRank,
       input_variables,
       input_tuples,
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {Double(0), Double(0), Double(0.2), Double(0.3), Double(0.4),
        Double(0.4), Double(0.6), Double(0.7), Double(0.8), Double(0.8),
        Double(1)},
       AnalyticOutputDeterminism::kDeterministic},
      // PERCENT_RANK on a partition with a single tuple.
      {FunctionKind::kPercentRank,
       input_variables,
       std::vector<TupleData>(1, input_tuples[0]),
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {Double(0)},
       AnalyticOutputDeterminism::kDeterministic},

      // CUME_DIST() OVER (ORDER BY x ASC, y DESC) on 10 tuples.
      {FunctionKind::kCumeDist,
       input_variables,
       std::vector<TupleData>(input_tuples.begin(), input_tuples.begin() + 10),
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {Double(0.2), Double(0.2), Double(0.3), Double(0.4), Double(0.6),
        Double(0.6), Double(0.7), Double(0.8), Double(1), Double(1)},
       AnalyticOutputDeterminism::kDeterministic},
      // CUME_DIST on an empty partition.
      {FunctionKind::kCumeDist,
       input_variables,
       {},
       empty_arguments,
       empty_windows,
       comparator_info_by_x_y,
       {},
       AnalyticOutputDeterminism::kDeterministic},

      // NTILE(n) OVER (ORDER BY x ASC)
      // Empty partition.
      {FunctionKind::kNtile,
       input_variables,
       {},
       {{Int64(2)}},
       empty_windows,
       comparator_info_by_x,
       {},
       AnalyticOutputDeterminism::kDeterministic},
      // n = 1.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(1)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(1), Int64(1), Int64(1), Int64(1), Int64(1), Int64(1),
        Int64(1), Int64(1), Int64(1), Int64(1)},
       AnalyticOutputDeterminism::kDeterministic},
      // n = 2.
      // Buckets (each enclosed in []):
      //   [(null, 10)] [(null, 10)]
      {FunctionKind::kNtile,
       input_variables,
       std::vector<TupleData>(input_tuples.begin(), input_tuples.begin() + 2),
       {{Int64(2)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(2)},
       AnalyticOutputDeterminism::kDeterministic},
      // n = 3.
      // Buckets (each enclosed in []):
      //   [(null, 10), (null, 10), (1, 9), (1, 8)]
      //   [(2, 7), (2, 7), (2, 6), (3, 5)]
      //   [(3, null), (3, null), (4, null)]
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(3)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(1), Int64(1), Int64(1), Int64(2), Int64(2), Int64(2),
        Int64(2), Int64(3), Int64(3), Int64(3)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // n = 3.
      // Buckets (each enclosed in []):
      //   [(null, 10), (null, 10), (1, 9), (1, 8)]
      //   [(2, 7), (2, 7), (2, 6)]
      //   [(3, 5), (3, null), (3, null)]
      {FunctionKind::kNtile,
       input_variables,
       std::vector<TupleData>(input_tuples.begin(), input_tuples.begin() + 10),
       {{Int64(3)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(1), Int64(1), Int64(1), Int64(2), Int64(2), Int64(2),
        Int64(3), Int64(3), Int64(3)},
       AnalyticOutputDeterminism::kDeterministic},
      // n = 4.
      // Buckets (each enclosed in []):
      //   [(null, 10), (null, 10), (1, 9)] [(1, 8), (2, 7), (2, 6)],
      //  [(2, 6), (3, 5), (3, null)], [(3, null), (4, null)]
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(4)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(1), Int64(1), Int64(2), Int64(2), Int64(2), Int64(3),
        Int64(3), Int64(3), Int64(4), Int64(4)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // n = 10.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(10)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(1), Int64(2), Int64(3), Int64(4), Int64(5), Int64(6),
        Int64(7), Int64(8), Int64(9), Int64(10)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // n = 11.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(11)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(2), Int64(3), Int64(4), Int64(5), Int64(6), Int64(7),
        Int64(8), Int64(9), Int64(10), Int64(11)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // n = 9223372036854775807.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(9223372036854775807)}},
       empty_windows,
       comparator_info_by_x,
       {Int64(1), Int64(2), Int64(3), Int64(4), Int64(5), Int64(6), Int64(7),
        Int64(8), Int64(9), Int64(10), Int64(11)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // n = -1.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(-1)}},
       empty_windows,
       comparator_info_by_x,
       {},
       AnalyticOutputDeterminism::kDeterministic,
       non_positive_error},
      // n = 0.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{Int64(0)}},
       empty_windows,
       comparator_info_by_x,
       {},
       AnalyticOutputDeterminism::kDeterministic,
       non_positive_error},
      // n = nullptr.
      {FunctionKind::kNtile,
       input_variables,
       input_tuples,
       {{NullInt64()}},
       empty_windows,
       comparator_info_by_x,
       {},
       AnalyticOutputDeterminism::kDeterministic,
       null_error},
  };
}

std::vector<AnalyticFunctionTestCase> GetFirstLastValueTestCases() {
  VariableId x("x"), y("y");
  const std::vector<VariableId> input_variables = {x, y};

  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{NullInt64(), Double(10)},
                            {NullInt64(), Double(9)},
                            {Int64(1), Double(9)},
                            {Int64(2), Double(7)},
                            {Int64(2), Double(7)},
                            {Int64(3), Double(7)}});

  const std::vector<Value> input_values_0{Int64(0), NullInt64(), Int64(2),
                                          NullInt64(), Int64(4), Int64(5)};
  const std::vector<Value> input_values_1{Int64(1), Int64(1), NullInt64(),
                                          Int64(4), Int64(4), NullInt64()};

  const AnalyticWindow empty_window;
  const std::vector<AnalyticWindow> windows{
      // {start_tuple_id, num_tuples}
      empty_window, {0, 1}, {0, 2}, {1, 1}, {2, 2}, {3, 3}};

  const std::shared_ptr<WindowOrderingComparatorInfo> comparator_info_by_x =
      GenerateWindowOrderingComparatorBySlot(input_variables, 0);

  return {
      // {function_kind, input_variables, input_tuples, arguments, windows,
      //  comparator, expected_result, output_determinism}

      //    x     y    argument    window ([start, end])
      //   ----------------------------------------------
      //   null  10      0         empty
      //   null   9     null       [0, 0]
      //   1      9      2         [0, 1]  <- non-deterministic if respect nulls
      //   2      7     null       [1, 1]
      //   2      7      4         [2, 3]
      //   3      7      5         [3, 5]  <- non-deterministic if respect nulls

      // Default null handling (respect nulls).
      {FunctionKind::kFirstValue,
       input_variables,
       input_tuples,
       {input_values_0},
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), Int64(2), NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Respect nulls.
      {FunctionKind::kFirstValue,
       input_variables,
       input_tuples,
       {input_values_0},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), Int64(2), NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Ignore nulls.
      {FunctionKind::kFirstValue,
       input_variables,
       input_tuples,
       {input_values_0},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), Int64(2), Int64(4)},
       AnalyticOutputDeterminism::kDeterministic},

      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      1          empty
      //   null   9      1          [0, 0]
      //   1      9     null        [0, 1]
      //   2      7      4          [1, 1]
      //   2      7      4          [2, 3]
      //   3      7     null        [3, 5]

      // Default null handling (respect nulls).
      {FunctionKind::kFirstValue,
       input_variables,
       input_tuples,
       {input_values_1},
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(1), Int64(1), Int64(1), NullInt64(), Int64(4)},
       AnalyticOutputDeterminism::kDeterministic},
      // Respect nulls.
      {FunctionKind::kFirstValue,
       input_variables,
       input_tuples,
       {input_values_1},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(1), Int64(1), Int64(1), NullInt64(), Int64(4)},
       AnalyticOutputDeterminism::kDeterministic},
      // Ignore nulls.
      {FunctionKind::kFirstValue,
       input_variables,
       input_tuples,
       {input_values_1},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(1), Int64(1), Int64(1), Int64(4), Int64(4)},
       AnalyticOutputDeterminism::kDeterministic},

      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      0          empty
      //   null   9     null        [0, 0]
      //   1      9      2          [0, 1] <- non-deterministic if respect nulls
      //   2      7     null        [1, 1]
      //   2      7      4          [2, 3]
      //   3      7      5          [3, 5]

      // Default null handling (respect nulls).
      {FunctionKind::kLastValue,
       input_variables,
       input_tuples,
       {input_values_0},
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), NullInt64(), NullInt64(), NullInt64(), Int64(5)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Respect nulls.
      {FunctionKind::kLastValue,
       input_variables,
       input_tuples,
       {input_values_0},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), NullInt64(), NullInt64(), NullInt64(), Int64(5)},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Ignore nulls.
      {FunctionKind::kLastValue,
       input_variables,
       input_tuples,
       {input_values_0},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), Int64(2), Int64(5)},
       AnalyticOutputDeterminism::kDeterministic},

      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      1          empty
      //   null   9      1          [0, 0]
      //   1      9     null        [0, 1]
      //   2      7      4          [1, 1]
      //   2      7      4          [2, 3]
      //   3      7     null        [3, 5]

      // Default null handling (respect nulls).
      {FunctionKind::kLastValue,
       input_variables,
       input_tuples,
       {input_values_1},
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(1), Int64(1), Int64(1), Int64(4), NullInt64()},
       AnalyticOutputDeterminism::kDeterministic},
      // Respect nulls.
      {FunctionKind::kLastValue,
       input_variables,
       input_tuples,
       {input_values_1},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(1), Int64(1), Int64(1), Int64(4), NullInt64()},
       AnalyticOutputDeterminism::kDeterministic},
      // Ignore nulls.
      {FunctionKind::kLastValue,
       input_variables,
       input_tuples,
       {input_values_1},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(1), Int64(1), Int64(1), Int64(4), Int64(4)},
       AnalyticOutputDeterminism::kDeterministic},

      // Empty input.

      // Default null handling (respect nulls).
      {FunctionKind::kFirstValue,
       input_variables,
       {} /* input_tuples */,
       {{}} /* arguments */,
       {} /* windows */,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic},
      // Respect nulls.
      {FunctionKind::kFirstValue,
       input_variables,
       {} /* input_tuples */,
       {{}} /* arguments */,
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       {} /* windows */,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic},
      // Ignore nulls.
      {FunctionKind::kFirstValue,
       input_variables,
       {} /* input_tuples */,
       {{}} /* arguments */,
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       {} /* windows */,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic},

      // Default null handling (respect nulls).
      {FunctionKind::kLastValue,
       input_variables,
       {} /* input_tuples */,
       {{}} /* arguments */,
       {} /* windows */,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic},
      // Respect nulls.
      {FunctionKind::kLastValue,
       input_variables,
       {} /* input_tuples */,
       {{}} /* arguments */,
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       {} /* windows */,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic},
      // Ignore nulls.
      {FunctionKind::kLastValue,
       input_variables,
       {} /* input_tuples */,
       {{}} /* arguments */,
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       {} /* windows */,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic},
  };
}

std::vector<AnalyticFunctionTestCase> GetNthValueTestCases() {
  VariableId x("x"), y("y");
  const std::vector<VariableId> input_variables = {x, y};

  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{NullInt64(), Double(10)},
                            {NullInt64(), Double(9)},
                            {Int64(1), Double(9)},
                            {Int64(2), Double(7)},
                            {Int64(3), Double(8)},
                            {Int64(3), Double(7)}});

  const std::vector<Value> input_values_0{Int64(0), NullInt64(), Int64(2),
                                          NullInt64(), Int64(4), Int64(5)};
  const std::vector<Value> input_values_1{Int64(0),    Int64(0), Int64(2),
                                          NullInt64(), Int64(4), Int64(5)};

  const AnalyticWindow empty_window;
  const std::vector<AnalyticWindow> windows{
      // {start_tuple_id, num_tuples}
      empty_window, {0, 1}, {0, 2}, {1, 1}, {3, 3}, empty_window};

  const std::shared_ptr<WindowOrderingComparatorInfo> comparator_info_by_x =
      GenerateWindowOrderingComparatorBySlot(input_variables, 0);

  const absl::Status non_positive_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The N value for the NthValue function must be positive";
  const absl::Status null_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The N value for the NthValue function must not be NULL";

  return {
      // {function_kind, input_variables, input_tuples, arguments, windows,
      //  comparator, expected_result, output_determinism, expected_status}

      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      0          empty
      //   null   9     null        [0, 0]
      //   1      9      2          [0, 1]
      //   2      7     null        [1, 1]
      //   3      8      4          [3, 5]
      //   3      7      5          empty

      // N = 0
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(0)}},
       windows,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic,
       non_positive_error},

      // N = 1
      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      0          empty
      //   null   9     null        [0, 0]
      //   1      9      2          [0, 1] <- non-deterministic if respect nulls
      //   2      7     null        [1, 1]
      //   3      8      4          [3, 5] <- non-deterministic if ignore nulls
      //   3      7      5          empty

      // Default null handling (respect nulls).
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(1)}},
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), NullInt64(), NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Respect nulls.
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(1)}},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), NullInt64(), NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Ignore nulls.
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(1)}},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), NullInt64(), Int64(4), NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},

      // N = 3
      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      0          empty
      //   null   9     null        [0, 0]
      //   1      9      2          [0, 1]
      //   2      7     null        [1, 1]
      //   3      8      4          [3, 5] <- non-deterministic if repsect
      //   nulls
      //   3      7      5          empty

      // Default null handling (respect nulls).
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(3)}},
       windows,
       comparator_info_by_x,
       {NullInt64(), NullInt64(), NullInt64(), NullInt64(), Int64(5),
        NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Respect nulls.
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(3)}},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), NullInt64(), NullInt64(), NullInt64(), Int64(5),
        NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},
      // Ignore nulls.
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(3)}},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), NullInt64(), NullInt64(), NullInt64(), NullInt64(),
        NullInt64()},
       AnalyticOutputDeterminism::kDeterministic},

      //    x     y    argument     window ([start, end])
      //   -----------------------------------------------
      //   null  10      0          empty
      //   null   9      0          [0, 0]
      //   1      9      2          [0, 1]
      //   2      7     null        [1, 1]
      //   3      8      4          [3, 5]  <- non-deterministic if ignore nulls
      //   3      7      5          empty
      // N = 1

      // Default null handling (respect nulls).
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_1, {Int64(1)}},
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), Int64(0), NullInt64(), NullInt64()},
       AnalyticOutputDeterminism::kDeterministic},
      // Respect nulls.
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_1, {Int64(1)}},
       ResolvedAnalyticFunctionCall::RESPECT_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), Int64(0), NullInt64(), NullInt64()},
       AnalyticOutputDeterminism::kDeterministic},
      // Ignore nulls.
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_1, {Int64(1)}},
       ResolvedAnalyticFunctionCall::IGNORE_NULLS,
       windows,
       comparator_info_by_x,
       {NullInt64(), Int64(0), Int64(0), Int64(0), Int64(4), NullInt64()},
       AnalyticOutputDeterminism::kNonDeterministic},

      // N = -1
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {Int64(-1)}},
       windows,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic,
       non_positive_error},
      // N = NULL
      {FunctionKind::kNthValue,
       input_variables,
       input_tuples,
       {input_values_0, {NullInt64()}},
       windows,
       comparator_info_by_x,
       {} /* expected_result */,
       AnalyticOutputDeterminism::kDeterministic,
       null_error}};
}

std::vector<AnalyticFunctionTestCase> GetLeadLagTestCases() {
  const std::vector<AnalyticWindow> empty_windows;

  VariableId x("x"), y("y");
  const std::vector<VariableId> input_variables = {x, y};

  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{NullInt64(), Double(10)},
                            {NullInt64(), Double(9)},
                            {Int64(1), Double(9)},
                            {Int64(2), Double(7)},
                            {Int64(2), Double(7)},
                            {Int64(3), Double(7)}});

  const std::vector<Value> input_values_0{Int64(0),    Int64(1), Int64(2),
                                          NullInt64(), Int64(4), Int64(5)};
  const std::vector<Value> input_values_1{Int64(1), Int64(1), Int64(1),
                                          Int64(4), Int64(4), Int64(4)};

  const std::shared_ptr<WindowOrderingComparatorInfo> comparator_info_by_x =
      GenerateWindowOrderingComparatorBySlot(input_variables, 0);

  const absl::Status null_offset_lead_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The offset to the function LEAD must not be null";
  const absl::Status negative_offset_lead_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The offset to the function LEAD must not be negative";

  const absl::Status null_offset_lag_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The offset to the function LAG must not be null";
  const absl::Status negative_offset_lag_error =
      ::googlesql_base::OutOfRangeErrorBuilder()
      << "The offset to the function LAG must not be negative";

  return {// {function_kind, input_variables, input_tuples, arguments, windows,
          //  comparator, expected_result, output_determinism, expected_status}

          // LEAD with an empty input.
          {FunctionKind::kLead,
           input_variables,
           {} /* input_tuples */,
           {{} /* values */,
            {Int64(1)} /* offset */,
            {NullInt64()} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          //    x     y    argument
          //   ----------------------
          //   null  10      0
          //   null   9      1
          //   1      9      2
          //   2      7     null
          //   2      7      4
          //   3      7      5

          // LEAD with offset 0.
          {FunctionKind::kLead,
           input_variables,
           input_tuples,
           {input_values_0,
            {Int64(0)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(0), Int64(1), Int64(2), NullInt64(), Int64(4),
            Int64(5)} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          // LEAD with offset 1.
          {FunctionKind::kLead,
           input_variables,
           input_tuples,
           {input_values_0,
            {Int64(1)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(1), Int64(2), NullInt64(), Int64(4), Int64(5),
            Int64(3)} /* expected_result */,
           AnalyticOutputDeterminism::kNonDeterministic},

          // LEAD with offset 2.
          {FunctionKind::kLead,
           input_variables,
           input_tuples,
           {input_values_0 /* values */,
            {Int64(2)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(2), NullInt64(), Int64(4), Int64(5), Int64(3),
            Int64(3)} /* expected_result */,
           AnalyticOutputDeterminism::kNonDeterministic},

          // LEAD with offset 6.
          {FunctionKind::kLead,
           input_variables,
           input_tuples,
           {input_values_0 /* values */,
            {Int64(6)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(3), Int64(3), Int64(3), Int64(3), Int64(3),
            Int64(3)} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          // LEAD with offset -1.
          {FunctionKind::kLead,
           input_variables,
           {} /* input_tuples */,
           {{} /* values */,
            {Int64(-1)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic,
           negative_offset_lead_error},

          // LEAD with a null offset.
          {FunctionKind::kLead,
           input_variables,
           {} /* input_tuples */,
           {{} /* values */,
            {NullInt64()} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic,
           null_offset_lead_error},

          //    x     y    argument
          //   ----------------------
          //   null  10      1
          //   null   9      1
          //   1      9      1
          //   2      7      4
          //   2      7      4
          //   3      7      4

          // LEAD with offset 1 and deterministic outputs.
          {FunctionKind::kLead,
           input_variables,
           input_tuples,
           {input_values_1,
            {Int64(1)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(1), Int64(1), Int64(4), Int64(4), Int64(4),
            Int64(3)} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          // LAG with an empty input.
          {FunctionKind::kLag,
           input_variables,
           {} /* input_tuples */,
           {{} /* values */,
            {Int64(1)} /* offset */,
            {NullInt64()} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          //    x     y    argument
          //   ----------------------
          //   null  10      0
          //   null   9      1
          //   1      9      2
          //   2      7     null
          //   2      7      4
          //   3      7      5

          // LAG with offset 0.
          {FunctionKind::kLag,
           input_variables,
           input_tuples,
           {input_values_0 /* values */,
            {Int64(0)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(0), Int64(1), Int64(2), NullInt64(), Int64(4),
            Int64(5)} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          // LAG with offset 1.
          {FunctionKind::kLag,
           input_variables,
           input_tuples,
           {input_values_0 /* values */,
            {Int64(1)} /* offset */,
            {Int64(3)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(3), Int64(0), Int64(1), Int64(2), NullInt64(),
            Int64(4)} /* expected_result */,
           AnalyticOutputDeterminism::kNonDeterministic},

          // LAG with offset 2.
          {FunctionKind::kLag,
           input_variables,
           input_tuples,
           {input_values_0 /* values */,
            {Int64(2)} /* offset */,
            {Int64(1)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(1), Int64(1), Int64(0), Int64(1), Int64(2),
            NullInt64()} /* expected_result */,
           AnalyticOutputDeterminism::kNonDeterministic},

          // LAG with offset 6.
          {FunctionKind::kLag,
           input_variables,
           input_tuples,
           {input_values_0 /* values */,
            {Int64(6)} /* offset */,
            {Int64(1)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(1), Int64(1), Int64(1), Int64(1), Int64(1),
            Int64(1)} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic},

          // LAG with offset -1.
          {FunctionKind::kLag,
           input_variables,
           {} /* input_tuples */,
           {{} /* values */,
            {Int64(-1)} /* offset */,
            {NullInt64()} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic,
           negative_offset_lag_error},

          // LAG with a null offset.
          {FunctionKind::kLag,
           input_variables,
           {} /* input_tuples */,
           {{} /* values */,
            {NullInt64()} /* offset */,
            {NullInt64()} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic,
           null_offset_lag_error},

          //    x     y    argument
          //   ----------------------
          //   null  10      1
          //   null   9      1
          //   1      9      1
          //   2      7      4
          //   2      7      4
          //   3      7      4

          // LAG with offset 1 and deterministic outputs.
          {FunctionKind::kLag,
           input_variables,
           input_tuples,
           {input_values_1,
            {Int64(1)} /* offset */,
            {Int64(1)} /* default_value */},
           empty_windows,
           comparator_info_by_x,
           {Int64(1), Int64(1), Int64(1), Int64(1), Int64(4),
            Int64(4)} /* expected_result */,
           AnalyticOutputDeterminism::kDeterministic}};
}

INSTANTIATE_TEST_SUITE_P(NumberingFunction, AnalyticFunctionTest,
                         ValuesIn(GetNumberingFunctionTestCases()));

INSTANTIATE_TEST_SUITE_P(FirstLastValueFunction, AnalyticFunctionTest,
                         ValuesIn(GetFirstLastValueTestCases()));

INSTANTIATE_TEST_SUITE_P(NthValueFunction, AnalyticFunctionTest,
                         ValuesIn(GetNthValueTestCases()));

INSTANTIATE_TEST_SUITE_P(LeadAndLagFunction, AnalyticFunctionTest,
                         ValuesIn(GetLeadLagTestCases()));

struct AnalyticWindowFrameParam {
  AnalyticWindowFrameParam(
      WindowFrameArg::WindowFrameType window_frame_type_in,
      WindowFrameBoundaryArg::BoundaryType start_boundary_type_in,
      WindowFrameBoundaryArg::BoundaryType end_boundary_type_in,
      int start_offset_value_in,
      int end_offset_value_in,
      TypeKind offset_value_type_in)
      : window_frame_type(window_frame_type_in),
        start_boundary_type(start_boundary_type_in),
        end_boundary_type(end_boundary_type_in),
        start_offset_value(start_offset_value_in),
        end_offset_value(end_offset_value_in),
        offset_value_type(offset_value_type_in) {
    ABSL_CHECK(window_frame_type == WindowFrameArg::kRows ||
          offset_value_type != TYPE_UNKNOWN);
  }

  // Reverses the two boundaries.
  // The reverse is done by two steps.
  //   1) FOLLOWING is changed to PRECEDING. PRECEDING is changed to FOLLOWING.
  //   2) Switch the two boundary types.
  void Reverse();

  // Changes a PRECEDING boundary type to the opposite FOLLOWING boundary type.
  static WindowFrameBoundaryArg::BoundaryType GetOppositeBoundaryType(
      WindowFrameBoundaryArg::BoundaryType boundary_type);

  WindowFrameArg::WindowFrameType window_frame_type;
  WindowFrameBoundaryArg::BoundaryType start_boundary_type;
  WindowFrameBoundaryArg::BoundaryType end_boundary_type;

  // Only valid when <start_boundary_type> is kOffsetPreceding or
  // kOffsetFollowing.
  int start_offset_value;

  // Only valid when <end_boundary_type> is kOffsetPreceding or
  // kOffsetFollowing.
  int end_offset_value;

  // TypeKind of the data type for the order key and the offset expressions
  // if any. Only valid for a RANGE window frame.
  TypeKind offset_value_type;
};

WindowFrameBoundaryArg::BoundaryType
AnalyticWindowFrameParam::GetOppositeBoundaryType(
    WindowFrameBoundaryArg::BoundaryType boundary_type) {
  switch (boundary_type) {
    case WindowFrameBoundaryArg::kUnboundedPreceding:
      return WindowFrameBoundaryArg::kUnboundedFollowing;
    case WindowFrameBoundaryArg::kOffsetPreceding:
      return WindowFrameBoundaryArg::kOffsetFollowing;
    case WindowFrameBoundaryArg::kCurrentRow:
      return WindowFrameBoundaryArg::kCurrentRow;
    case WindowFrameBoundaryArg::kOffsetFollowing:
      return WindowFrameBoundaryArg::kOffsetPreceding;
    case WindowFrameBoundaryArg::kUnboundedFollowing:
      return WindowFrameBoundaryArg::kUnboundedPreceding;
  }
}

void AnalyticWindowFrameParam::Reverse() {
  start_boundary_type = GetOppositeBoundaryType(start_boundary_type);
  end_boundary_type = GetOppositeBoundaryType(end_boundary_type);
  using std::swap;
  swap(start_offset_value, end_offset_value);
  swap(start_boundary_type, end_boundary_type);
}

struct AnalyticWindowTestCase {
  AnalyticWindowTestCase(const std::vector<VariableId>& input_variables_in,
                         const std::vector<TupleData>& input_tuples_in,
                         AnalyticWindowFrameParam window_frame_param_in,
                         const std::vector<AnalyticWindow>& expected_windows_in,
                         bool window_frame_is_deterministic_in,
                         bool order_asc_in = true)
      : input_variables(input_variables_in),
        input_tuples(input_tuples_in),
        window_frame_param(window_frame_param_in),
        expected_windows(expected_windows_in),
        window_frame_is_deterministic(window_frame_is_deterministic_in),
        order_asc(order_asc_in) {}

  std::vector<VariableId> input_variables;
  std::vector<TupleData> input_tuples;
  AnalyticWindowFrameParam window_frame_param;
  std::vector<AnalyticWindow> expected_windows;
  bool window_frame_is_deterministic;
  // Whether the order keys are in a ascending order. Only relevant for a
  // RANGE window frame.
  bool order_asc;
};

class AnalyticWindowTest
    : public ::testing::TestWithParam<AnalyticWindowTestCase> {
 public:
  AnalyticWindowTest() = default;
  AnalyticWindowTest(const AnalyticWindowTest&) = delete;
  AnalyticWindowTest& operator=(const AnalyticWindowTest&) = delete;

  static AnalyticWindowFrameParam CreateUnboundedPrecedingOffsetPreceding(
      WindowFrameArg::WindowFrameType window_frame_type, int offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kUnboundedPreceding,
        WindowFrameBoundaryArg::kOffsetPreceding,
        -1 /* start_offset_value */, offset, offset_value_type);
  }

  static AnalyticWindowFrameParam CreateUnboundedPrecedingCurrentRow(
      WindowFrameArg::WindowFrameType window_frame_type,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kUnboundedPreceding,
        WindowFrameBoundaryArg::kCurrentRow,
        -1 /* start_offset_value */,
        -1 /* end_offset_value */,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateUnboundedPrecedingOffsetFollowing(
      WindowFrameArg::WindowFrameType window_frame_type, int offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kUnboundedPreceding,
        WindowFrameBoundaryArg::kOffsetFollowing,
        -1 /* start_offset_value */, offset, offset_value_type);
  }

  static AnalyticWindowFrameParam
  CreateUnboundedPrecedingUnboundedFollowing(
      WindowFrameArg::WindowFrameType window_frame_type,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kUnboundedPreceding,
        WindowFrameBoundaryArg::kUnboundedFollowing,
        -1 /* start_offset_value */,
        -1 /* end_offset_value */,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateOffsetPrecedingOffsetPreceding(
      WindowFrameArg::WindowFrameType window_frame_type,
      int start_offset, int end_offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kOffsetPreceding,
        WindowFrameBoundaryArg::kOffsetPreceding,
        start_offset, end_offset,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateOffsetPrecedingCurrentRow(
      WindowFrameArg::WindowFrameType window_frame_type, int offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kOffsetPreceding,
        WindowFrameBoundaryArg::kCurrentRow,
        offset, -1 /* end_offset_value */,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateOffsetPrecedingOffsetFollowing(
      WindowFrameArg::WindowFrameType window_frame_type,
      int start_offset, int end_offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    ABSL_CHECK(window_frame_type == WindowFrameArg::kRows ||
          offset_value_type != TYPE_UNKNOWN);
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kOffsetPreceding,
        WindowFrameBoundaryArg::kOffsetFollowing,
        start_offset, end_offset,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateOffsetPrecedingUnboundedFollowing(
      WindowFrameArg::WindowFrameType window_frame_type, int offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kOffsetPreceding,
        WindowFrameBoundaryArg::kUnboundedFollowing,
        offset, -1 /* end_offset_value */, offset_value_type);
  }

  static AnalyticWindowFrameParam CreateCurrentRowCurrentRow(
      WindowFrameArg::WindowFrameType window_frame_type,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kCurrentRow,
        WindowFrameBoundaryArg::kCurrentRow,
        -1 /* start_offset_value */,
        -1 /* end_offset_value */,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateCurrentRowOffsetFollowing(
      WindowFrameArg::WindowFrameType window_frame_type, int offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kCurrentRow,
        WindowFrameBoundaryArg::kOffsetFollowing,
        -1 /* start_offset_value */, offset,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateCurrentRowUnboundedFollowing(
      WindowFrameArg::WindowFrameType window_frame_type,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kCurrentRow,
        WindowFrameBoundaryArg::kUnboundedFollowing,
        -1 /* start_offset_value */,
        -1 /* end_offset_value */,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateOffsetFollowingOffsetFollowing(
      WindowFrameArg::WindowFrameType window_frame_type,
      int start_offset, int end_offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kOffsetFollowing,
        WindowFrameBoundaryArg::kOffsetFollowing,
        start_offset, end_offset,
        offset_value_type);
  }

  static AnalyticWindowFrameParam CreateOffsetFollowingUnboundedFollowing(
      WindowFrameArg::WindowFrameType window_frame_type, int offset,
      TypeKind offset_value_type = TYPE_UNKNOWN) {
    return AnalyticWindowFrameParam(
        window_frame_type,
        WindowFrameBoundaryArg::kOffsetFollowing,
        WindowFrameBoundaryArg::kUnboundedFollowing,
        offset, -1 /* end_offset_value*/,
        offset_value_type);
  }

  static std::unique_ptr<WindowFrameBoundaryArg> CreateWindowFrameBoundary(
      WindowFrameBoundaryArg::BoundaryType boundary_type, int offset,
      TypeKind offset_value_type) {
    switch (boundary_type) {
      case WindowFrameBoundaryArg::kUnboundedPreceding:
        return CreateUnboundedPrecedingBoundary();
      case WindowFrameBoundaryArg::kOffsetPreceding:
        return CreateOffsetPrecedingBoundary(offset_value_type, offset);
      case WindowFrameBoundaryArg::kCurrentRow:
        return CreateCurrentRows();
      case WindowFrameBoundaryArg::kOffsetFollowing:
        return CreateOffsetFollowingBoundary(offset_value_type, offset);
      case WindowFrameBoundaryArg::kUnboundedFollowing:
        return CreateUnboundedFollowingBoundary();
    }
  }

  static std::unique_ptr<WindowFrameArg> CreateWindowFrameFromParam(
      const AnalyticWindowFrameParam& frame_param) {
    if (frame_param.window_frame_type == WindowFrameArg::kRows) {
      return WindowFrameArg::Create(
                 WindowFrameArg::kRows,
                 CreateWindowFrameBoundary(frame_param.start_boundary_type,
                                           frame_param.start_offset_value,
                                           TYPE_INT64 /* offset_type */),
                 CreateWindowFrameBoundary(frame_param.end_boundary_type,
                                           frame_param.end_offset_value,
                                           TYPE_INT64 /* offset_type */))
          .value();
    } else {
      ABSL_CHECK_EQ(frame_param.window_frame_type, WindowFrameArg::kRange);
      return WindowFrameArg::Create(
                 WindowFrameArg::kRange,
                 CreateWindowFrameBoundary(frame_param.start_boundary_type,
                                           frame_param.start_offset_value,
                                           frame_param.offset_value_type),
                 CreateWindowFrameBoundary(frame_param.end_boundary_type,
                                           frame_param.end_offset_value,
                                           frame_param.offset_value_type))
          .value();
    }
  }

  static std::vector<AnalyticWindowTestCase> GetAllRowsWindowTestCases();

  // Creates RANGE window tests cases.

  // <type_kind> specifies the data type of the order keys as well as the offset
  // expressions if any.
  //
  // <order_asc> gives the ordering direction of the order keys.

  // <null_min_max_bits> and <inf_nan_bits> are two bit vectors that control
  // whether tuples with particular order key values should be included.
  // For <null_min_max_bits>, from the least to the more significant bit, each
  // bit corresponds to null values, regular values, values close to min,
  // values close to max. If a bit is on, the test relation will contain
  // tuples with corresponding values. Only 4 bits are used.
  // For <inf_nan_bits>, from the least to the more significant bit, each
  // bit corresponds to nan, negative infinity, positive infinity. If a bit
  // is on, the test relation will contain tuples with corresponding values.
  static std::vector<AnalyticWindowTestCase> GetRangeWindowTestsWithOptions(
      TypeKind type_kind, bool order_asc, int8_t null_min_max_bits,
      int8_t inf_nan_bits);

  static std::vector<AnalyticWindowTestCase> GetIntegerRangeWindowTests();

  static std::vector<AnalyticWindowTestCase> GetFloatingPointRangeWindowTests();

  // Creates a Value of the given <type_kind> with a value equal to <value>.
  static Value CreateValueFromInt(TypeKind type_kind, int value);

  // Creates a NULL value of the given <type_kind>
  static Value CreateNullValue(TypeKind type_kind);

  // Creates a Value of the given <type_kind> with a value equal to
  // the maximum value of the data type subtracted by <operand>.
  static Value SubtractMaxValue(TypeKind type_kind,
                                int operand);

  // Creates a Value of the given <type_kind> with a value equal to
  // the minimum value of the data type added by <operand>.
  static Value AddMinValue(TypeKind type_kind,
                           int operand);

  // Creates a Value of the given <type_kind> with a value equal to the positive
  // infinity. For integer types, returns a Value of 0.
  static Value CreatePosInfOrZero(TypeKind type_kind);

  // Creates a Value of the given <type_kind> with a value equal to the negative
  // infinity. For integer types, returns a Value of 0.
  static Value CreateNegInfOrZero(TypeKind type_kind);

  // Creates a Value of the given <type_kind> with a value equal to NaN.
  // For integer types, returns a Value of 0.
  static Value CreateNaNOrZero(TypeKind type_kind);

 protected:
  static std::unique_ptr<WindowFrameBoundaryArg>
  CreateUnboundedPrecedingBoundary() {
    return WindowFrameBoundaryArg::Create(
               WindowFrameBoundaryArg::kUnboundedPreceding, nullptr /* expr */)
        .value();
  }

  static std::unique_ptr<WindowFrameBoundaryArg>
  CreateUnboundedFollowingBoundary() {
    return WindowFrameBoundaryArg::Create(
               WindowFrameBoundaryArg::kUnboundedFollowing, nullptr /* expr */)
        .value();
  }

  static std::unique_ptr<WindowFrameBoundaryArg> CreateOffsetPrecedingBoundary(
      TypeKind offset_type, int offset) {
    return WindowFrameBoundaryArg::Create(
               WindowFrameBoundaryArg::kOffsetPreceding,
               ConstExpr::Create(CreateValueFromInt(offset_type, offset))
                   .value())
        .value();
  }

  static std::unique_ptr<WindowFrameBoundaryArg> CreateOffsetFollowingBoundary(
      TypeKind offset_type, int offset) {
    return WindowFrameBoundaryArg::Create(
               WindowFrameBoundaryArg::kOffsetFollowing,
               ConstExpr::Create(CreateValueFromInt(offset_type, offset))
                   .value())
        .value();
  }

  static std::unique_ptr<WindowFrameBoundaryArg> CreateCurrentRows() {
    return WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kCurrentRow,
                                          nullptr /* expr */)
        .value();
  }

  // Adjusts <window> by removing tuples with ids in <ids_to_remove>.
  // The tuple ids in <ids_to_remove> must be in the ascending order.
  static AnalyticWindow RemoveFromWindow(absl::Span<const int> ids_to_remove,
                                         const AnalyticWindow& window);

  static std::string ToString(absl::Span<const AnalyticWindow> windows) {
    std::string ret("{");
    for (const AnalyticWindow& window : windows) {
      absl::StrAppend(&ret, "[", window.start_tuple_id, ",", window.num_tuples,
                      "]", " ");
    }
    absl::StrAppend(&ret, "}");
    return ret;
  }

  std::vector<AnalyticWindow> actual_windows_;
};

Value AnalyticWindowTest::CreateValueFromInt(TypeKind type_kind, int value) {
  switch (type_kind) {
    case TYPE_INT32:
      return Int32(static_cast<int32_t>(value));
    case TYPE_INT64:
      return Int64(static_cast<int64_t>(value));
    case TYPE_UINT32:
      ABSL_CHECK_GE(value, 0);
      return Uint32(static_cast<uint32_t>(value));
    case TYPE_UINT64:
      ABSL_CHECK_GE(value, 0);
      return Uint64(static_cast<uint64_t>(value));
    case TYPE_FLOAT:
      return Float(static_cast<float>(value));
    case TYPE_DOUBLE:
      return Double(static_cast<double>(value));
    case TYPE_NUMERIC:
      return Numeric(NumericValue(static_cast<int64_t>(value)));
    case TYPE_BIGNUMERIC:
      return BigNumeric(BigNumericValue(static_cast<int64_t>(value)));
    default:
      ABSL_LOG(FATAL) << TypeKind_Name(type_kind) << " not supported";
  }
}

Value AnalyticWindowTest::CreateNullValue(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_INT32:
      return NullInt32();
    case TYPE_INT64:
      return NullInt64();
    case TYPE_UINT32:
      return NullUint32();
    case TYPE_UINT64:
      return NullUint64();
    case TYPE_FLOAT:
      return NullFloat();
    case TYPE_DOUBLE:
      return NullDouble();
    case TYPE_NUMERIC:
      return NullNumeric();
    case TYPE_BIGNUMERIC:
      return NullBigNumeric();
    default:
      ABSL_LOG(FATAL) << TypeKind_Name(type_kind) << " not supported";
  }
}

Value AnalyticWindowTest::SubtractMaxValue(TypeKind type_kind,
                                           int operand) {
  switch (type_kind) {
    case TYPE_INT32:
      return Int32(std::numeric_limits<int32_t>::max() -
                   static_cast<int32_t>(operand));
    case TYPE_INT64:
      return Int64(std::numeric_limits<int64_t>::max() -
                   static_cast<int64_t>(operand));
    case TYPE_UINT32:
      ABSL_CHECK_GE(operand, 0);
      return Uint32(std::numeric_limits<uint32_t>::max() -
                    static_cast<uint32_t>(operand));
    case TYPE_UINT64:
      ABSL_CHECK_GE(operand, 0);
      return Uint64(std::numeric_limits<uint64_t>::max() -
                    static_cast<uint64_t>(operand));
    case TYPE_NUMERIC:
      ABSL_CHECK_GE(operand, 0);
      return Numeric(
          NumericValue::MaxValue().Subtract(NumericValue(operand)).value());
    case TYPE_BIGNUMERIC:
      ABSL_CHECK_GE(operand, 0);
      return BigNumeric(BigNumericValue::MaxValue()
                            .Subtract(BigNumericValue(operand))
                            .value());
    case TYPE_FLOAT:
      return Float(std::numeric_limits<float>::max() -
                   static_cast<float>(operand));
    case TYPE_DOUBLE:
      return Double(std::numeric_limits<double>::max() -
                    static_cast<float>(operand));
    default:
      ABSL_LOG(FATAL) << TypeKind_Name(type_kind) << " not supported";
  }
}

Value AnalyticWindowTest::CreatePosInfOrZero(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_FLOAT:
      return Float(std::numeric_limits<float>::infinity());
    case TYPE_DOUBLE:
      return Double(std::numeric_limits<double>::infinity());
    default:
      return CreateValueFromInt(type_kind, 0);
  }
}

Value AnalyticWindowTest::CreateNegInfOrZero(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_FLOAT:
      return Float(-std::numeric_limits<float>::infinity());
    case TYPE_DOUBLE:
      return Double(-std::numeric_limits<double>::infinity());
    default:
      return CreateValueFromInt(type_kind, 0);
  }
}

Value AnalyticWindowTest::CreateNaNOrZero(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_FLOAT:
      return Float(std::numeric_limits<float>::quiet_NaN());
    case TYPE_DOUBLE:
      return Double(std::numeric_limits<double>::quiet_NaN());
    default:
      return CreateValueFromInt(type_kind, 0);
  }
}

AnalyticWindow AnalyticWindowTest::RemoveFromWindow(
    absl::Span<const int> ids_to_remove, const AnalyticWindow& window) {
  if (window.num_tuples == 0) {
    return window;
  }

  int vec_index = 0;
  int new_start_tuple_id = window.start_tuple_id;
  int new_num_tuples = window.num_tuples;

  // The removed tuples that are before the first tuple of the window.
  while (vec_index < ids_to_remove.size() &&
         ids_to_remove[vec_index] < window.start_tuple_id) {
    --new_start_tuple_id;
    ++vec_index;
  }

  // Other removed tuples that are within the window.
  while (vec_index < ids_to_remove.size() &&
         ids_to_remove[vec_index] < window.start_tuple_id + window.num_tuples) {
    ++vec_index;
    --new_num_tuples;
  }

  if (new_num_tuples == 0) {
    return AnalyticWindow();
  }
  return AnalyticWindow(new_start_tuple_id, new_num_tuples);
}

Value AnalyticWindowTest::AddMinValue(TypeKind type_kind,
                                      int operand) {
  switch (type_kind) {
    case TYPE_INT32:
      return Int32(std::numeric_limits<int32_t>::lowest() +
                   static_cast<int32_t>(operand));
    case TYPE_INT64:
      return Int64(std::numeric_limits<int64_t>::lowest() +
                   static_cast<int64_t>(operand));
    case TYPE_UINT32:
      ABSL_CHECK_GE(operand, 0);
      return Uint32(std::numeric_limits<uint32_t>::lowest() +
                    static_cast<uint32_t>(operand));
    case TYPE_UINT64:
      ABSL_CHECK_GE(operand, 0);
      return Uint64(std::numeric_limits<uint64_t>::lowest() +
                    static_cast<uint32_t>(operand));
    case TYPE_NUMERIC:
      ABSL_CHECK_GE(operand, 0);
      return Numeric(
          NumericValue::MinValue().Add(NumericValue(operand)).value());
    case TYPE_BIGNUMERIC:
      ABSL_CHECK_GE(operand, 0);
      return BigNumeric(
          BigNumericValue::MinValue().Add(BigNumericValue(operand)).value());
    case TYPE_FLOAT:
      return Float(std::numeric_limits<float>::lowest() +
                   static_cast<float>(operand));
    case TYPE_DOUBLE:
      return Double(std::numeric_limits<double>::lowest() +
                    static_cast<float>(operand));
    default:
      ABSL_LOG(FATAL) << TypeKind_Name(type_kind) << " not supported";
  }
}

std::vector<AnalyticWindowTestCase>
AnalyticWindowTest::GetAllRowsWindowTestCases() {
  const VariableId var("var");
  const std::vector<VariableId> input_variables = {var};
  // Contains 10 distinct tuples.
  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{Int64(1)},
                            {Int64(2)},
                            {Int64(3)},
                            {Int64(4)},
                            {Int64(5)},
                            {Int64(6)},
                            {Int64(7)},
                            {Int64(8)},
                            {Int64(9)},
                            {Int64(10)}});
  // Contains 10 identical tuples.
  const std::vector<TupleData> identical_tuples =
      CreateTestTupleDatas({{Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)},
                            {Int64(10)}});

  const AnalyticWindow empty_window;
  const AnalyticWindow unbound_window(0, 10);
  std::vector<AnalyticWindowTestCase> test_cases{
      // For each case,
      //   1) the first argument is the input schema;
      //   2) the second argument is the 10 input tuples;
      //   3) the third argument is a window frame to test;
      //   4) the fourth argument is a list of windows for the input relation,
      //      matching 1:1 with the 10 tuples;
      //   5) the fifth argument specifies whether the window frame is
      //      deterministic.
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingOffsetPreceding(WindowFrameArg::kRows, 2),
       {empty_window,
        empty_window,
        {0, 1},
        {0, 2},
        {0, 3},
        {0, 4},
        {0, 5},
        {0, 6},
        {0, 7},
        {0, 8}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingCurrentRow(WindowFrameArg::kRows),
       {{0, 1},
        {0, 2},
        {0, 3},
        {0, 4},
        {0, 5},
        {0, 6},
        {0, 7},
        {0, 8},
        {0, 9},
        {0, 10}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingOffsetFollowing(WindowFrameArg::kRows, 2),
       {{0, 3},
        {0, 4},
        {0, 5},
        {0, 6},
        {0, 7},
        {0, 8},
        {0, 9},
        {0, 10},
        {0, 10},
        {0, 10}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRows,
                                            std::numeric_limits<int>::max(), 2),
       {10, empty_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingUnboundedFollowing(WindowFrameArg::kRows),
       {10, unbound_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetFollowing(WindowFrameArg::kRows,
                                            std::numeric_limits<int>::max(),
                                            std::numeric_limits<int>::max()),
       {10, unbound_window},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRows, 3, 1),
       {empty_window,
        {0, 1},
        {0, 2},
        {0, 3},
        {1, 3},
        {2, 3},
        {3, 3},
        {4, 3},
        {5, 3},
        {6, 3}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       identical_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRows, 3, 1),
       {empty_window,
        {0, 1},
        {0, 2},
        {0, 3},
        {1, 3},
        {2, 3},
        {3, 3},
        {4, 3},
        {5, 3},
        {6, 3}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingCurrentRow(WindowFrameArg::kRows, 2),
       {{0, 1},
        {0, 2},
        {0, 3},
        {1, 3},
        {2, 3},
        {3, 3},
        {4, 3},
        {5, 3},
        {6, 3},
        {7, 3}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetFollowing(WindowFrameArg::kRows, 2, 2),
       {{0, 3},
        {0, 4},
        {0, 5},
        {1, 5},
        {2, 5},
        {3, 5},
        {4, 5},
        {5, 5},
        {6, 4},
        {7, 3}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       identical_tuples,
       CreateOffsetPrecedingOffsetFollowing(WindowFrameArg::kRows, 2, 2),
       {{0, 3},
        {0, 4},
        {0, 5},
        {1, 5},
        {2, 5},
        {3, 5},
        {4, 5},
        {5, 5},
        {6, 4},
        {7, 3}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingUnboundedFollowing(WindowFrameArg::kRows, 2),
       {{0, 10},
        {0, 10},
        {0, 10},
        {1, 9},
        {2, 8},
        {3, 7},
        {4, 6},
        {5, 5},
        {6, 4},
        {7, 3}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRows, 2,
                                            std::numeric_limits<int>::max()),
       {10, empty_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateCurrentRowCurrentRow(WindowFrameArg::kRows),
       {{0, 1},
        {1, 1},
        {2, 1},
        {3, 1},
        {4, 1},
        {5, 1},
        {6, 1},
        {7, 1},
        {8, 1},
        {9, 1}},
       true /* window_frame_is_deterministic */},
      // Boundary offset can be 0 for PRECEDING and FOLLOWING.
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetFollowing(WindowFrameArg::kRows, 0, 0),
       {{0, 1},
        {1, 1},
        {2, 1},
        {3, 1},
        {4, 1},
        {5, 1},
        {6, 1},
        {7, 1},
        {8, 1},
        {9, 1}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateCurrentRowOffsetFollowing(WindowFrameArg::kRows, 1),
       {{0, 2},
        {1, 2},
        {2, 2},
        {3, 2},
        {4, 2},
        {5, 2},
        {6, 2},
        {7, 2},
        {8, 2},
        {9, 1}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateCurrentRowUnboundedFollowing(WindowFrameArg::kRows),
       {{0, 10},
        {1, 9},
        {2, 8},
        {3, 7},
        {4, 6},
        {5, 5},
        {6, 4},
        {7, 3},
        {8, 2},
        {9, 1}},
       false /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRows, 1, 2),
       {{1, 2},
        {2, 2},
        {3, 2},
        {4, 2},
        {5, 2},
        {6, 2},
        {7, 2},
        {8, 2},
        {9, 1},
        empty_window},
       false /* window_frame_is_deterministic */},
      {input_variables,
       identical_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRows, 1, 2),
       {{1, 2},
        {2, 2},
        {3, 2},
        {4, 2},
        {5, 2},
        {6, 2},
        {7, 2},
        {8, 2},
        {9, 1},
        empty_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingUnboundedFollowing(WindowFrameArg::kRows, 1),
       {{1, 9},
        {2, 8},
        {3, 7},
        {4, 6},
        {5, 5},
        {6, 4},
        {7, 3},
        {8, 2},
        {9, 1},
        empty_window},
       false /* window_frame_is_deterministic */},
      {input_variables,
       identical_tuples,
       CreateOffsetFollowingUnboundedFollowing(WindowFrameArg::kRows, 1),
       {{1, 9},
        {2, 8},
        {3, 7},
        {4, 6},
        {5, 5},
        {6, 4},
        {7, 3},
        {8, 2},
        {9, 1},
        empty_window},
       true /* window_frame_is_deterministic */},
      // Empty windows for all tuples.
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRows, 1, 2),
       {10, empty_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRows, 2, 1),
       {10, empty_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRows, 11, 10),
       {10, empty_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRows, 10, 11),
       {10, empty_window},
       true /* window_frame_is_deterministic */}};

  return test_cases;
}

std::vector<AnalyticWindowTestCase>
AnalyticWindowTest::GetRangeWindowTestsWithOptions(TypeKind type_kind,
                                                   bool order_asc,
                                                   int8_t null_min_max_bits,
                                                   int8_t inf_nan_bits) {
  const VariableId var("var");
  const std::vector<VariableId> input_variables = {var};
  const Value neg_inf = CreateNegInfOrZero(type_kind);
  const Value pos_inf = CreatePosInfOrZero(type_kind);
  const Value nan = CreateNaNOrZero(type_kind);

  // Not all values are valid for <type_kind>. They will be removed later.
  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{CreateNullValue(type_kind)},
                            {CreateNullValue(type_kind)},
                            {nan},
                            {nan},
                            {neg_inf},
                            {neg_inf},
                            {AddMinValue(type_kind, 0)} /* min */,
                            {AddMinValue(type_kind, 0)} /* min */,
                            {AddMinValue(type_kind, 1)} /* min + 1 */,
                            {AddMinValue(type_kind, 3)} /* min + 3 */,
                            {CreateValueFromInt(type_kind, 10)},
                            {CreateValueFromInt(type_kind, 11)},
                            {CreateValueFromInt(type_kind, 11)},
                            {CreateValueFromInt(type_kind, 12)},
                            {CreateValueFromInt(type_kind, 12)},
                            {CreateValueFromInt(type_kind, 14)},
                            {CreateValueFromInt(type_kind, 15)},
                            {CreateValueFromInt(type_kind, 16)},
                            {CreateValueFromInt(type_kind, 16)},
                            {SubtractMaxValue(type_kind, 3)} /* max - 3 */,
                            {SubtractMaxValue(type_kind, 1)} /* max - 1 */,
                            {SubtractMaxValue(type_kind, 0)} /* max */,
                            {SubtractMaxValue(type_kind, 0)} /* max */,
                            {pos_inf},
                            {pos_inf}});

  const AnalyticWindow empty_window;
  const AnalyticWindow unbound_window(0, input_tuples.size());
  // Input tuple([id, value]):
  //   [0, null], [1, null], [2, nan], [3, nan], [4, neg_inf], [5, neg_inf],
  //   [6, min], [7, min], [8, min+1], [9, min+3], [10, 10], [11, 11], [12, 11],
  //   [13, 12], [14, 12], [15, 14], [16, 15], [17, 16], [18, 16], [19, max-3],
  //   [20, max-1], [21, max], [22, max], [23, pos_inf], [24, pos_inf].
  //
  // For each case,
  //   1) The first argument is the input schema.
  //   2) The second argument is the list of tuples given above.
  //   3) The third argument is a window frame to test.
  //   4) The fourth argument is a list of windows for the input relation,
  //      matching 1:1 with the input tuples.
  //      The format of each window is {start_tuple_id, num_tuples}.
  //
  // When giving the expected windows, we *assume* all tuples are in the input
  // relation and *assume* that all values are valid regardless of the data
  // type of the order keys. We will prune the test relation and shrink the
  // windows accordingly based on the options specified by <null_min_max_bits>
  // and <inf_nan_bits>.
  std::vector<AnalyticWindowTestCase> test_cases{
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingOffsetPreceding(WindowFrameArg::kRange, 2,
                                               type_kind),
       {{0, 2},  {0, 2},  {0, 4},  {0, 4},  {0, 6},  {0, 6},  {0, 6},
        {0, 6},  {0, 6},  {0, 9},  {0, 10}, {0, 10}, {0, 10}, {0, 11},
        {0, 11}, {0, 15}, {0, 15}, {0, 16}, {0, 16}, {0, 19}, {0, 20},
        {0, 20}, {0, 20}, {0, 25}, {0, 25}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingCurrentRow(WindowFrameArg::kRange, type_kind),
       {{0, 2},  {0, 2},  {0, 4},  {0, 4},  {0, 6},  {0, 6},  {0, 8},
        {0, 8},  {0, 9},  {0, 10}, {0, 11}, {0, 13}, {0, 13}, {0, 15},
        {0, 15}, {0, 16}, {0, 17}, {0, 19}, {0, 19}, {0, 20}, {0, 21},
        {0, 23}, {0, 23}, {0, 25}, {0, 25}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingOffsetFollowing(WindowFrameArg::kRange, 2,
                                               type_kind),
       {{0, 2},  {0, 2},  {0, 4},  {0, 4},  {0, 6},  {0, 6},  {0, 9},
        {0, 9},  {0, 10}, {0, 10}, {0, 15}, {0, 15}, {0, 15}, {0, 16},
        {0, 16}, {0, 19}, {0, 19}, {0, 19}, {0, 19}, {0, 21}, {0, 23},
        {0, 23}, {0, 23}, {0, 25}, {0, 25}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateUnboundedPrecedingUnboundedFollowing(WindowFrameArg::kRange,
                                                  type_kind),
       {25, unbound_window},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRange, 2, 1,
                                            type_kind),
       {{0, 2},       {0, 2},       {2, 2},       {2, 2},  {4, 2},
        {4, 2},       empty_window, empty_window, {6, 2},  {8, 1},
        empty_window, {10, 1},      {10, 1},      {10, 3}, {10, 3},
        {13, 2},      {15, 1},      {15, 2},      {15, 2}, empty_window,
        {19, 1},      {20, 1},      {20, 1},      {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingCurrentRow(WindowFrameArg::kRange, 2, type_kind),
       {{0, 2},  {0, 2},  {2, 2},  {2, 2},  {4, 2},  {4, 2},  {6, 2},
        {6, 2},  {6, 3},  {8, 2},  {10, 1}, {10, 3}, {10, 3}, {10, 5},
        {10, 5}, {13, 3}, {15, 2}, {15, 4}, {15, 4}, {19, 1}, {19, 2},
        {20, 3}, {20, 3}, {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRange, 2, 0,
                                            type_kind),
       {{0, 2},  {0, 2},  {2, 2},  {2, 2},  {4, 2},  {4, 2},  {6, 2},
        {6, 2},  {6, 3},  {8, 2},  {10, 1}, {10, 3}, {10, 3}, {10, 5},
        {10, 5}, {13, 3}, {15, 2}, {15, 4}, {15, 4}, {19, 1}, {19, 2},
        {20, 3}, {20, 3}, {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetFollowing(WindowFrameArg::kRange, 2, 2,
                                            type_kind),
       {{0, 2},  {0, 2},  {2, 2},  {2, 2},  {4, 2},  {4, 2},  {6, 3},
        {6, 3},  {6, 4},  {8, 2},  {10, 5}, {10, 5}, {10, 5}, {10, 6},
        {10, 6}, {13, 6}, {15, 4}, {15, 4}, {15, 4}, {19, 2}, {19, 4},
        {20, 3}, {20, 3}, {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingUnboundedFollowing(WindowFrameArg::kRange, 2,
                                               type_kind),
       {{0, 25},  {0, 25},  {2, 23},  {2, 23},  {4, 21},  {4, 21},  {6, 19},
        {6, 19},  {6, 19},  {8, 17},  {10, 15}, {10, 15}, {10, 15}, {10, 15},
        {10, 15}, {13, 12}, {15, 10}, {15, 10}, {15, 10}, {19, 6},  {19, 6},
        {20, 5},  {20, 5},  {23, 2},  {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateCurrentRowCurrentRow(WindowFrameArg::kRange, type_kind),
       {{0, 2},  {0, 2},  {2, 2},  {2, 2},  {4, 2},  {4, 2},  {6, 2},
        {6, 2},  {8, 1},  {9, 1},  {10, 1}, {11, 2}, {11, 2}, {13, 2},
        {13, 2}, {15, 1}, {16, 1}, {17, 2}, {17, 2}, {19, 1}, {20, 1},
        {21, 2}, {21, 2}, {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetFollowing(WindowFrameArg::kRange, 0, 0,
                                            type_kind),
       {{0, 2},  {0, 2},  {2, 2},  {2, 2},  {4, 2},  {4, 2},  {6, 2},
        {6, 2},  {8, 1},  {9, 1},  {10, 1}, {11, 2}, {11, 2}, {13, 2},
        {13, 2}, {15, 1}, {16, 1}, {17, 2}, {17, 2}, {19, 1}, {20, 1},
        {21, 2}, {21, 2}, {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateCurrentRowOffsetFollowing(WindowFrameArg::kRange, 2, type_kind),
       {{0, 2},  {0, 2},  {2, 2},  {2, 2},  {4, 2},  {4, 2},  {6, 3},
        {6, 3},  {8, 2},  {9, 1},  {10, 5}, {11, 4}, {11, 4}, {13, 3},
        {13, 3}, {15, 4}, {16, 3}, {17, 2}, {17, 2}, {19, 2}, {20, 3},
        {21, 2}, {21, 2}, {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateCurrentRowUnboundedFollowing(WindowFrameArg::kRange, type_kind),
       {{0, 25},  {0, 25},  {2, 23}, {2, 23},  {4, 21},  {4, 21},  {6, 19},
        {6, 19},  {8, 17},  {9, 16}, {10, 15}, {11, 14}, {11, 14}, {13, 12},
        {13, 12}, {15, 10}, {16, 9}, {17, 8},  {17, 8},  {19, 6},  {20, 5},
        {21, 4},  {21, 4},  {23, 2}, {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRange, 1, 2,
                                            type_kind),
       {{0, 2},  {0, 2},       {2, 2},       {2, 2},       {4, 2},
        {4, 2},  {8, 1},       {8, 1},       {9, 1},       empty_window,
        {11, 4}, {13, 2},      {13, 2},      {15, 1},      {15, 1},
        {16, 3}, {17, 2},      empty_window, empty_window, {20, 1},
        {21, 2}, empty_window, empty_window, {23, 2},      {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingUnboundedFollowing(WindowFrameArg::kRange, 2,
                                               type_kind),
       {{0, 25},  {0, 25}, {2, 23},  {2, 23},  {4, 21},  {4, 21},  {9, 16},
        {9, 16},  {9, 16}, {10, 15}, {13, 12}, {15, 10}, {15, 10}, {15, 10},
        {15, 10}, {17, 8}, {19, 6},  {19, 6},  {19, 6},  {20, 5},  {23, 2},
        {23, 2},  {23, 2}, {23, 2},  {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetPrecedingOffsetPreceding(WindowFrameArg::kRange, 1, 2,
                                            type_kind),
       {{0, 2},       {0, 2},       {2, 2},       {2, 2},       {4, 2},
        {4, 2},       empty_window, empty_window, empty_window, empty_window,
        empty_window, empty_window, empty_window, empty_window, empty_window,
        empty_window, empty_window, empty_window, empty_window, empty_window,
        empty_window, empty_window, empty_window, {23, 2},      {23, 2}},
       true /* window_frame_is_deterministic */},
      {input_variables,
       input_tuples,
       CreateOffsetFollowingOffsetFollowing(WindowFrameArg::kRange, 2, 1,
                                            type_kind),
       {{0, 2},       {0, 2},       {2, 2},       {2, 2},       {4, 2},
        {4, 2},       empty_window, empty_window, empty_window, empty_window,
        empty_window, empty_window, empty_window, empty_window, empty_window,
        empty_window, empty_window, empty_window, empty_window, empty_window,
        empty_window, empty_window, empty_window, {23, 2},      {23, 2}},
       true /* window_frame_is_deterministic */}};

  // Next, we remove and reorder tuples, and also adjust the windows
  // according to the three options: order_asc, null_min_max_bits and
  // inf_nan_bits.

  // Check if the provided flags are valid.
  switch (type_kind) {
    case TYPE_INT32:
    case TYPE_INT64:
    case TYPE_UINT32:
    case TYPE_UINT64:
    case TYPE_NUMERIC:
    case TYPE_BIGNUMERIC:
      if (inf_nan_bits != 0) {
        ABSL_LOG(FATAL) << "Integer types do not have infinity and NaN values";
      }
      break;
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
      if (null_min_max_bits & 12) {
        ABSL_LOG(FATAL) << "Min and max values must be removed";
      }
      break;
    default:
      ABSL_LOG(FATAL) << "Unsupported type kind " << TypeKind_Name(type_kind);
  }

  std::vector<int> tuple_ids_to_remove;
  // Starting from the least significant bit of null_min_max_bits, each bit
  // indicates whether a group of values should be included: null, regular, min,
  // max.
  if ((null_min_max_bits & 1) == 0) {
    // Remove tuples with null keys.
    tuple_ids_to_remove.emplace_back(0);
    tuple_ids_to_remove.emplace_back(1);
  }
  if ((null_min_max_bits & 2) == 0) {
    // Remove tuples with "regular" key values.
    for (int id = 10; id < 19; ++id) {
      tuple_ids_to_remove.emplace_back(id);
    }
  }
  if ((null_min_max_bits & 4) == 0) {
    // Remove tuples with key values close to min.
    tuple_ids_to_remove.insert(tuple_ids_to_remove.end(),
                               {6, 7, 8, 9});
  }
  if ((null_min_max_bits & 8) == 0) {
    // Remove tuples with key values close to max.
    tuple_ids_to_remove.insert(tuple_ids_to_remove.end(),
                               {19, 20, 21, 22});
  }

  // Starting from the least significant bit of inf_nan_mask, each bit
  // indicates whether a group of values should be included: nan, negative
  // infinity, positive infinity.
  if ((inf_nan_bits & 1) == 0) {
    // Remove tuples with nan keys.
    tuple_ids_to_remove.emplace_back(2);
    tuple_ids_to_remove.emplace_back(3);
  }
  if ((inf_nan_bits & 2) == 0) {
    // Remove tuples with negative infinity keys.
    tuple_ids_to_remove.emplace_back(4);
    tuple_ids_to_remove.emplace_back(5);
  }
  if ((inf_nan_bits & 3) == 0) {
    // Remove tuples with positive infinity keys.
    tuple_ids_to_remove.emplace_back(23);
    tuple_ids_to_remove.emplace_back(24);
  }

  // Sort the ids in <tuple_ids_to_remove> as required by STLSetDifference and
  // the later call of RemoveFromWindow.
  std::sort(tuple_ids_to_remove.begin(), tuple_ids_to_remove.end());

  std::vector<int> orig_ids(25);
  std::iota(orig_ids.begin(), orig_ids.end(), 0);

  // The tuple ids that need to be kept in the test relation.
  // Use set to ensure the ordering of the ids so that the new relation
  // follows the same order as the original relation.
  absl::btree_set<int> left_ids;
  googlesql_base::STLSetDifference(orig_ids, tuple_ids_to_remove, &left_ids);

  // Generate the new relation by removing those in <tuple_ids_to_remove>.
  std::vector<TupleData> new_input_tuples;
  for (int left_id : left_ids) {
    new_input_tuples.push_back(input_tuples[left_id]);
  }

  // Reverse the ordering of the new relation.
  if (!order_asc) {
    std::reverse(new_input_tuples.begin(), new_input_tuples.end());
  }

  // Compute the windows for the new relation.
  for (AnalyticWindowTestCase& test_case : test_cases) {
    std::vector<AnalyticWindow> new_windows;
    for (int left_id : left_ids) {
      new_windows.push_back(
          RemoveFromWindow(tuple_ids_to_remove,
                           test_case.expected_windows[left_id]));
    }
    test_case.expected_windows = new_windows;
    test_case.input_tuples = new_input_tuples;
  }

  // "Reverse" the window frame and the expected windows if the order is
  // descending.
  if (!order_asc) {
    for (AnalyticWindowTestCase& test_case : test_cases) {
      test_case.window_frame_param.Reverse();
      test_case.order_asc = false;
      std::reverse(test_case.expected_windows.begin(),
                   test_case.expected_windows.end());
      for (AnalyticWindow& expected_window : test_case.expected_windows) {
        if (expected_window.num_tuples > 0) {
          // A window starting at x is changed to a window starting at
          // the symmetric position of the last tuple in the original window,
          // which is (num_tuples - x - window_size).
          expected_window.start_tuple_id = new_input_tuples.size() -
                                           expected_window.start_tuple_id -
                                           expected_window.num_tuples;
        }
      }
    }
  }

  return test_cases;
}

std::vector<AnalyticWindowTestCase>
AnalyticWindowTest::GetIntegerRangeWindowTests() {
  std::vector<int8_t> null_min_max_options(15);
  std::iota(null_min_max_options.begin(), null_min_max_options.end(), 1);

  std::vector<TypeKind> integer_types{TYPE_INT32,   TYPE_INT64,
                                      TYPE_UINT32,  TYPE_UINT64,
                                      TYPE_NUMERIC, TYPE_BIGNUMERIC};

  std::vector<AnalyticWindowTestCase> test_cases;
  for (TypeKind integer_type : integer_types) {
    for (int8_t null_min_max_option : null_min_max_options) {
      // Disable tests for infinity and NaN as integers do not have them.
      const std::vector<AnalyticWindowTestCase> test_cases_asc =
          GetRangeWindowTestsWithOptions(integer_type, true /* order_asc */,
                                         null_min_max_option,
                                         0 /* inf_nan_mask */);
      test_cases.insert(test_cases.end(),
                        test_cases_asc.begin(),
                        test_cases_asc.end());

      const std::vector<AnalyticWindowTestCase> test_cases_desc =
          GetRangeWindowTestsWithOptions(integer_type, false /* order_asc */,
                                         null_min_max_option,
                                         0 /* inf_nan_mask */);
      test_cases.insert(test_cases.end(),
                        test_cases_desc.begin(),
                        test_cases_desc.end());
    }
  }

  return test_cases;
}

std::vector<AnalyticWindowTestCase>
AnalyticWindowTest::GetFloatingPointRangeWindowTests() {
  // Disable tests for min and max, because floating-point arithmetic operations
  // are not accurate on large and small values.
  std::vector<int8_t> null_min_max_options{0, 1, 2, 3};
  std::vector<int8_t> inf_nan_options{0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<TypeKind> integer_types{TYPE_FLOAT, TYPE_DOUBLE};

  std::vector<AnalyticWindowTestCase> test_cases;
  for (TypeKind integer_type : integer_types) {
    for (int8_t null_min_max_option : null_min_max_options) {
      for (int8_t inf_nan_option : inf_nan_options) {
        if (null_min_max_option == 0 && inf_nan_option == 0) {
          continue;
        }

        const std::vector<AnalyticWindowTestCase> test_cases_asc =
            GetRangeWindowTestsWithOptions(integer_type, true /* order_asc */,
                                           null_min_max_option, inf_nan_option);
        test_cases.insert(test_cases.end(),
                          test_cases_asc.begin(),
                          test_cases_asc.end());

        const std::vector<AnalyticWindowTestCase> test_cases_desc =
            GetRangeWindowTestsWithOptions(integer_type, false /* order_asc */,
                                           null_min_max_option, inf_nan_option);
        test_cases.insert(test_cases.end(),
                          test_cases_desc.begin(),
                          test_cases_desc.end());
      }
    }
  }

  return test_cases;
}

TEST_P(AnalyticWindowTest, AnalyticWindowTest) {
  const AnalyticWindowTestCase& test_case = GetParam();
  const TupleSchema schema(test_case.input_variables);

  std::vector<const TupleData*> input_tuples;
  input_tuples.reserve(test_case.input_tuples.size());
  for (const TupleData& tuple : test_case.input_tuples) {
    input_tuples.push_back(&tuple);
  }

  const AnalyticWindowFrameParam& window_frame_param =
      test_case.window_frame_param;
  std::unique_ptr<WindowFrameArg> window_frame(
      CreateWindowFrameFromParam(window_frame_param));

  TypeFactory type_factory;
  const VariableId var("var");
  std::unique_ptr<KeyArg> order_key;
  EvaluationContext context((EvaluationOptions()));
  bool window_frame_is_deterministic;
  if (window_frame_param.window_frame_type == WindowFrameArg::kRange) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto deref,
        DerefExpr::Create(var, type_factory.MakeSimpleType(
                                   window_frame_param.offset_value_type)));
    order_key = std::make_unique<KeyArg>(
        var, std::move(deref),
        (test_case.order_asc ? KeyArg::kAscending : KeyArg::kDescending));
    GOOGLESQL_ASSERT_OK(window_frame->GetWindows(
        schema, input_tuples, {order_key.get()}, EmptyParams(), &context,
        &actual_windows_, &window_frame_is_deterministic));
  } else {
    GOOGLESQL_ASSERT_OK(window_frame->GetWindows(
        schema, input_tuples, /*order_keys=*/{}, EmptyParams(), &context,
        &actual_windows_, &window_frame_is_deterministic));
  }

  EXPECT_EQ(test_case.window_frame_is_deterministic,
            window_frame_is_deterministic);

  std::vector<std::string> tuple_strs;
  tuple_strs.reserve(input_tuples.size());
  for (int i = 0; i < input_tuples.size(); ++i) {
    const TupleData* tuple = input_tuples[i];
    const std::string str = Tuple(&schema, tuple).DebugString();
    tuple_strs.push_back(str);
  }

  EXPECT_EQ(test_case.expected_windows, actual_windows_)
      << "Input: " << absl::StrJoin(tuple_strs, ",") << "\n"
      << "Window frame: " << window_frame->DebugString(true /* verbose */)
      << "\nExpected: " << ToString(test_case.expected_windows)
      << "\nActual: " << ToString(actual_windows_);
}

INSTANTIATE_TEST_SUITE_P(
    AnalyticRowsWindowTest, AnalyticWindowTest,
    ValuesIn(AnalyticWindowTest::GetAllRowsWindowTestCases()));

INSTANTIATE_TEST_SUITE_P(
    AnalyticIntegerRangeWindowTest, AnalyticWindowTest,
    ValuesIn(AnalyticWindowTest::GetIntegerRangeWindowTests()));

INSTANTIATE_TEST_SUITE_P(
    AnalyticFloatingPointRangeWindowTest, AnalyticWindowTest,
    ValuesIn(AnalyticWindowTest::GetFloatingPointRangeWindowTests()));

TEST_F(AnalyticWindowTest, ValidateWindowNullOffsetValues) {
  const VariableId offset_var("offset_var");

  const TupleSchema params_schema({offset_var});
  const TupleData params_data = CreateTestTupleData({NullInt64()});

  const std::string expected_error_message(
      "The boundary offset value cannot be null");
  std::vector<int> window_boundaries;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset_var,
                       DerefExpr::Create(offset_var, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> preceding_boundary,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetPreceding,
                                     std::move(deref_offset_var)));
  GOOGLESQL_ASSERT_OK(preceding_boundary->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  absl::Status start_preceding_status =
      preceding_boundary->GetRowsBasedWindowBoundaries(
          false /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, start_preceding_status.message());

  absl::Status end_preceding_status =
      preceding_boundary->GetRowsBasedWindowBoundaries(
          true /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, end_preceding_status.message());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset_var_again,
                       DerefExpr::Create(offset_var, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> following_boundary,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetFollowing,
                                     std::move(deref_offset_var_again)));
  GOOGLESQL_ASSERT_OK(following_boundary->SetSchemasForEvaluation({&params_schema}));

  absl::Status start_following_status =
      following_boundary->GetRowsBasedWindowBoundaries(
          false /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, start_following_status.message());

  absl::Status end_following_status =
      following_boundary->GetRowsBasedWindowBoundaries(
          true /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, end_following_status.message());
}

TEST_F(AnalyticWindowTest, ValidateWindowNegativeOffsetValues) {
  const VariableId offset_var("offset_var");

  const TupleSchema params_schema({offset_var});
  const TupleData params_data = CreateTestTupleData({Int64(-5)});

  const std::string expected_error_message(
      "Window frame offset for PRECEDING or FOLLOWING must be non-negative, "
      "but was -5");
  std::vector<int> window_boundaries;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset_var,
                       DerefExpr::Create(offset_var, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> preceding_boundary,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetPreceding,
                                     std::move(deref_offset_var)));
  GOOGLESQL_ASSERT_OK(preceding_boundary->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  absl::Status start_preceding_status =
      preceding_boundary->GetRowsBasedWindowBoundaries(
          false /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, start_preceding_status.message());

  absl::Status end_preceding_status =
      preceding_boundary->GetRowsBasedWindowBoundaries(
          true /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, end_preceding_status.message());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset_var_again,
                       DerefExpr::Create(offset_var, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> following_boundary,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetFollowing,
                                     std::move(deref_offset_var_again)));
  GOOGLESQL_ASSERT_OK(following_boundary->SetSchemasForEvaluation({&params_schema}));

  absl::Status start_following_status =
      following_boundary->GetRowsBasedWindowBoundaries(
          false /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, start_following_status.message());

  absl::Status end_following_status =
      following_boundary->GetRowsBasedWindowBoundaries(
          true /* is_end_boundary */, 10 /* partition_size */, {&params_data},
          &context, &window_boundaries);
  EXPECT_EQ(expected_error_message, end_following_status.message());
}

typedef TestWithParam<TypeKind> AnalyticWindowInfinityOffsetTest;

TEST_P(AnalyticWindowInfinityOffsetTest, AnalyticWindowTest) {
  const TypeKind type_kind = GetParam();
  const VariableId var("var"), offset_var("offset_var");
  const Value neg_inf = AnalyticWindowTest::CreateNegInfOrZero(type_kind);
  const Value pos_inf = AnalyticWindowTest::CreatePosInfOrZero(type_kind);
  const Value nan = AnalyticWindowTest::CreateNaNOrZero(type_kind);

  const TupleSchema input_schema({var});
  const std::vector<TupleData> input_tuples = CreateTestTupleDatas(
      {{AnalyticWindowTest::CreateNullValue(type_kind)},
       {AnalyticWindowTest::CreateNullValue(type_kind)},
       {nan},
       {nan},
       {neg_inf},
       {neg_inf},
       {AnalyticWindowTest::AddMinValue(type_kind, 0)},
       {AnalyticWindowTest::AddMinValue(type_kind, 0)},
       {AnalyticWindowTest::AddMinValue(type_kind, 1)},
       {AnalyticWindowTest::CreateValueFromInt(type_kind, 10)},
       {AnalyticWindowTest::CreateValueFromInt(type_kind, 10)},
       {AnalyticWindowTest::CreateValueFromInt(type_kind, 11)},
       {AnalyticWindowTest::SubtractMaxValue(type_kind, 1)},
       {AnalyticWindowTest::SubtractMaxValue(type_kind, 0)},
       {AnalyticWindowTest::SubtractMaxValue(type_kind, 0)},
       {pos_inf},
       {pos_inf}});

  std::vector<TupleData> asc_tuples_without_pos_inf = input_tuples;
  asc_tuples_without_pos_inf.pop_back();
  asc_tuples_without_pos_inf.pop_back();

  std::vector<TupleData> asc_tuples_without_neg_inf = input_tuples;
  asc_tuples_without_neg_inf.erase(asc_tuples_without_neg_inf.begin() + 4,
                                   asc_tuples_without_neg_inf.begin() + 6);

  std::vector<TupleData> desc_tuples_without_pos_inf(
      asc_tuples_without_pos_inf.rbegin(), asc_tuples_without_pos_inf.rend());
  std::vector<TupleData> desc_tuples_without_neg_inf(
      asc_tuples_without_neg_inf.rbegin(), asc_tuples_without_neg_inf.rend());

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_var,
      DerefExpr::Create(var, type_factory.MakeSimpleType(type_kind)));
  auto asc_key =
      std::make_unique<KeyArg>(var, std::move(deref_var), KeyArg::kAscending);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_var_again,
      DerefExpr::Create(var, type_factory.MakeSimpleType(type_kind)));

  auto desc_key = std::make_unique<KeyArg>(var, std::move(deref_var_again),
                                           KeyArg::kDescending);

  const TupleSchema params_schema({offset_var});
  const TupleData params_data =
      CreateTestTupleData({AnalyticWindowTest::CreatePosInfOrZero(type_kind)});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_offset,
      DerefExpr::Create(offset_var, type_factory.MakeSimpleType(type_kind)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> inf_preceding,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetPreceding,
                                     std::move(deref_offset)));
  GOOGLESQL_ASSERT_OK(inf_preceding->SetSchemasForEvaluation({&params_schema}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_offset_again,
      DerefExpr::Create(offset_var, type_factory.MakeSimpleType(type_kind)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> inf_following,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetFollowing,
                                     std::move(deref_offset_again)));
  GOOGLESQL_ASSERT_OK(inf_following->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  {
    // Infinity preceding boundary on an ascending partition:
    //   (null, null, nan, nan, neg inf, neg inf, min, min, min + 1,
    //    10, 10, 11, max - 1, max, max).
    const std::vector<int> expected_start_window_boundaries{
        0, 0, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
    const std::vector<int> expected_end_window_boundaries{
        1, 1, 3, 3, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};

    // Tuple ids of the start boundaries.
    std::vector<int> actual_start_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_preceding->GetRangeBasedWindowBoundaries(
        false /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(asc_tuples_without_pos_inf), {asc_key.get()},
        {&params_data}, &context, &actual_start_window_boundaries));

    // Tuple ids of the end boundaries.
    std::vector<int> actual_end_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_preceding->GetRangeBasedWindowBoundaries(
        true /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(asc_tuples_without_pos_inf), {asc_key.get()},
        {&params_data}, &context, &actual_end_window_boundaries));

    EXPECT_EQ(expected_start_window_boundaries, actual_start_window_boundaries);
    EXPECT_EQ(expected_end_window_boundaries, actual_end_window_boundaries);
  }

  {
    // Infinity preceding boundary on a descending partition:
    //   (pos inf, pos inf, max, max, max - 1, 11, 10, 10, min + 1, min, min,
    //    nan, nan, null, null).
    const std::vector<int> expected_start_window_boundaries{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 11, 13, 13};
    const std::vector<int> expected_end_window_boundaries{
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 12, 12, 14, 14};

    // Tuple ids of the start boundaries.
    std::vector<int> actual_start_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_preceding->GetRangeBasedWindowBoundaries(
        false /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(desc_tuples_without_neg_inf), {desc_key.get()},
        {&params_data}, &context, &actual_start_window_boundaries));

    // Tuple ids of the end boundaries.
    std::vector<int> actual_end_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_preceding->GetRangeBasedWindowBoundaries(
        true /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(desc_tuples_without_neg_inf), {desc_key.get()},
        {&params_data}, &context, &actual_end_window_boundaries));

    EXPECT_EQ(expected_start_window_boundaries, actual_start_window_boundaries);
    EXPECT_EQ(expected_end_window_boundaries, actual_end_window_boundaries);
  }

  {
    // Infinity following boundary on an ascending partition:
    //   (null, null, nan, nan, min, min, min + 1, 10, 10, 11,
    //    max - 1, max, max, pos inf, pos inf).
    const std::vector<int> expected_start_window_boundaries{
        0, 0, 2, 2, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13};
    const std::vector<int> expected_end_window_boundaries{
        1, 1, 3, 3, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14};

    // Tuple ids of the start boundaries.
    std::vector<int> actual_start_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_following->GetRangeBasedWindowBoundaries(
        false /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(asc_tuples_without_neg_inf), {asc_key.get()},
        {&params_data}, &context, &actual_start_window_boundaries));

    // Tuple ids of the end boundaries.
    std::vector<int> actual_end_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_following->GetRangeBasedWindowBoundaries(
        true /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(asc_tuples_without_neg_inf), {asc_key.get()},
        {&params_data}, &context, &actual_end_window_boundaries));

    EXPECT_EQ(expected_start_window_boundaries, actual_start_window_boundaries);
    EXPECT_EQ(expected_end_window_boundaries, actual_end_window_boundaries);
  }

  {
    // Infinity following boundary on a descending partition:
    //   (max, max, max - 1, 11, 10, 10, min + 1, min, min, neg inf, neg inf,
    //    nan, nan, null, null).
    const std::vector<int> expected_start_window_boundaries{
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 11, 11, 13, 13};
    const std::vector<int> expected_end_window_boundaries{
        10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 12, 12, 14, 14};

    // Tuple ids of the start boundaries.
    std::vector<int> actual_start_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_following->GetRangeBasedWindowBoundaries(
        false /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(desc_tuples_without_pos_inf), {desc_key.get()},
        {&params_data}, &context, &actual_start_window_boundaries));

    // Tuple ids of the end boundaries.
    std::vector<int> actual_end_window_boundaries;
    GOOGLESQL_ASSERT_OK(inf_following->GetRangeBasedWindowBoundaries(
        true /* is_end_boundary */, input_schema,
        GetTupleDataPtrs(desc_tuples_without_pos_inf), {desc_key.get()},
        {&params_data}, &context, &actual_end_window_boundaries));

    EXPECT_EQ(expected_start_window_boundaries, actual_start_window_boundaries);
    EXPECT_EQ(expected_end_window_boundaries, actual_end_window_boundaries);
  }
}

TEST_P(AnalyticWindowInfinityOffsetTest, PosInfMinusPosInfError) {
  const TypeKind type_kind = GetParam();

  VariableId var("var"), offset_var("offset_var");

  const TupleSchema input_schema({var});
  const std::vector<TupleData> asc_tuples = CreateTestTupleDatas(
      {{AnalyticWindowTest::CreateNegInfOrZero(type_kind)},
       {AnalyticWindowTest::CreateValueFromInt(type_kind, 1)},
       {AnalyticWindowTest::CreatePosInfOrZero(type_kind)}});
  const std::vector<TupleData> desc_tuples(asc_tuples.rbegin(),
                                           asc_tuples.rend());

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_var,
      DerefExpr::Create(var, type_factory.MakeSimpleType(type_kind)));
  auto asc_key =
      std::make_unique<KeyArg>(var, std::move(deref_var), KeyArg::kAscending);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_var_again,
      DerefExpr::Create(var, type_factory.MakeSimpleType(type_kind)));

  auto desc_key = std::make_unique<KeyArg>(var, std::move(deref_var_again),
                                           KeyArg::kDescending);

  const TupleSchema params_schema({offset_var});
  const TupleData params_data =
      CreateTestTupleData({AnalyticWindowTest::CreatePosInfOrZero(type_kind)});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_offset,
      DerefExpr::Create(offset_var, type_factory.MakeSimpleType(type_kind)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> inf_preceding,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetPreceding,
                                     std::move(deref_offset)));
  GOOGLESQL_ASSERT_OK(inf_preceding->SetSchemasForEvaluation({&params_schema}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto deref_offset_again,
      DerefExpr::Create(offset_var, type_factory.MakeSimpleType(type_kind)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WindowFrameBoundaryArg> inf_following,
      WindowFrameBoundaryArg::Create(WindowFrameBoundaryArg::kOffsetFollowing,
                                     std::move(deref_offset_again)));
  GOOGLESQL_ASSERT_OK(inf_following->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  std::vector<int> window_boundaries;
  {
    absl::Status start_boundary_stauts =
        inf_preceding->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/false, input_schema,
            GetTupleDataPtrs(asc_tuples), {asc_key.get()}, {&params_data},
            &context, &window_boundaries);
    window_boundaries.clear();
    absl::Status end_boundary_stauts =
        inf_preceding->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/true, input_schema,
            GetTupleDataPtrs(asc_tuples), {asc_key.get()}, {&params_data},
            &context, &window_boundaries);
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists "
        "a positive infinity order key for an offset PRECEDING on "
        "an ascending partition",
        start_boundary_stauts.message());
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists "
        "a positive infinity order key for an offset PRECEDING on "
        "an ascending partition",
        end_boundary_stauts.message());
  }

  {
    window_boundaries.clear();
    absl::Status start_boundary_stauts =
        inf_preceding->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/false, input_schema,
            GetTupleDataPtrs(desc_tuples), {desc_key.get()}, {&params_data},
            &context, &window_boundaries);
    window_boundaries.clear();
    absl::Status end_boundary_stauts =
        inf_preceding->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/true, input_schema,
            GetTupleDataPtrs(desc_tuples), {desc_key.get()}, {&params_data},
            &context, &window_boundaries);
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists "
        "a negative infinity order key for an offset PRECEDING on "
        "a descending partition",
        start_boundary_stauts.message());
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists "
        "a negative infinity order key for an offset PRECEDING on "
        "a descending partition",
        end_boundary_stauts.message());
  }

  {
    window_boundaries.clear();
    absl::Status start_boundary_stauts =
        inf_following->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/false, input_schema,
            GetTupleDataPtrs(asc_tuples), {asc_key.get()}, {&params_data},
            &context, &window_boundaries);
    window_boundaries.clear();
    absl::Status end_boundary_stauts =
        inf_following->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/true, input_schema,
            GetTupleDataPtrs(asc_tuples), {asc_key.get()}, {&params_data},
            &context, &window_boundaries);
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists a "
        "negative infinity order key for an offset FOLLOWING on "
        "an ascending partition",
        start_boundary_stauts.message());
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists a "
        "negative infinity order key for an offset FOLLOWING on "
        "an ascending partition",
        end_boundary_stauts.message());
  }

  {
    window_boundaries.clear();
    absl::Status start_boundary_status =
        inf_following->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/false, input_schema,
            GetTupleDataPtrs(desc_tuples), {desc_key.get()}, {&params_data},
            &context, &window_boundaries);
    window_boundaries.clear();
    absl::Status end_boundary_status =
        inf_following->GetRangeBasedWindowBoundaries(
            /*is_end_boundary=*/true, input_schema,
            GetTupleDataPtrs(desc_tuples), {desc_key.get()}, {&params_data},
            &context, &window_boundaries);
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists "
        "a positive infinity order key for an offset FOLLOWING on "
        "a descending partition",
        start_boundary_status.message());
    EXPECT_EQ(
        "Offset value cannot be positive infinity when there exists "
        "a positive infinity order key for an offset FOLLOWING on "
        "a descending partition",
        end_boundary_status.message());
  }
}

INSTANTIATE_TEST_SUITE_P(AnalyticWindowInfinityOffsetTest,
                         AnalyticWindowInfinityOffsetTest,
                         ValuesIn(std::vector<TypeKind>{TYPE_FLOAT,
                                                        TYPE_DOUBLE}));

// Appends a new <var> column to <vars> and to the end of each tuple.
// <column_values> provides the values of the new column, and
// corresponds 1:1 with <tuples>.
void AddColumn(const VariableId& var, absl::Span<const Value> column_values,
               std::vector<VariableId>* vars, std::vector<TupleData>* tuples) {
  vars->push_back(var);
  ABSL_CHECK_EQ(tuples->size(), column_values.size());
  for (int tuple_id = 0; tuple_id < tuples->size(); ++tuple_id) {
    TupleData& tuple = (*tuples)[tuple_id];
    const int new_slot_idx = tuple.num_slots();
    tuple.AddSlots(1);
    tuple.mutable_slot(new_slot_idx)->SetValue(column_values[tuple_id]);
  }
}

class AnalyticOpTest : public ::testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(AnalyticOpTests, AnalyticOpTest, ::testing::Bool());

TEST_P(AnalyticOpTest, AnalyticOpTest) {
  const bool preserves_order = GetParam();

  VariableId a("a"), b("b"), c("c");
  VariableId count_dist("count_dist"), sum("sum");
  VariableId rank("rank"), lead("lead"), nth_value("nth_value");
  const std::vector<VariableId> input_variables = {a, b, c};
  const std::vector<TupleData> input_tuples =
      CreateTestTupleDatas({{Int64(0), NullInt64(), Int64(1)},
                            {Int64(0), NullInt64(), Int64(2)},
                            {Int64(0), Int64(1), Int64(1)},
                            {Int64(0), Int64(1), NullInt64()},
                            {Int64(1), Int64(1), NullInt64()},
                            {Int64(1), Int64(2), Int64(1)},
                            {Int64(1), Int64(2), Int64(2)},
                            {Int64(1), Int64(3), Int64(1)}});
  auto input_op = std::make_unique<TestRelationalOp>(
      input_variables, input_tuples, /*preserves_order=*/true);
  TypeFactory type_factory;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c1, DerefExpr::Create(c, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args1;
  args1.push_back(std::move(deref_c1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg1,
      AggregateArg::Create(count_dist,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kCount, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(args1), AggregateArg::kDistinct));

  // COUNT(distinct c) OVER (PARTITION BY a ORDER BY b
  //                         ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto analytic1,
      AggregateAnalyticArg::Create(
          AnalyticWindowTest::CreateWindowFrameFromParam(
              AnalyticWindowTest::CreateUnboundedPrecedingCurrentRow(
                  WindowFrameArg::kRows)),
          std::move(agg1), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c2, DerefExpr::Create(c, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args2;
  args2.push_back(std::move(deref_c2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg2,
      AggregateArg::Create(sum,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kSum, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(args2)));

  // SUM(c) OVER (PARTITION BY a ORDER BY b
  //              RANGE BETWEEN 1 PRECEDING AND CURRENT ROW)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto analytic2,
      AggregateAnalyticArg::Create(
          AnalyticWindowTest::CreateWindowFrameFromParam(
              AnalyticWindowTest::CreateOffsetPrecedingCurrentRow(
                  WindowFrameArg::kRange, 1, TYPE_INT64)),
          std::move(agg2), DEFAULT_ERROR_MODE));

  // RANK() OVER (PARTITION BY a ORDER BY b)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto analytic3,
      NonAggregateAnalyticArg::Create(
          rank, nullptr /* window_frame */, std::make_unique<RankFunction>(),
          {} /* non_const_arguments */, {} /* const_arguments */,
          DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c4, DerefExpr::Create(c, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args4;
  args4.push_back(std::move(deref_c4));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto two, ConstExpr::Create(Int64(2)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto negative_one, ConstExpr::Create(Int64(-1)));

  std::vector<std::unique_ptr<ValueExpr>> args4_2;
  args4_2.push_back(std::move(two));
  args4_2.push_back(std::move(negative_one));

  // LEAD(c, 2, -1) OVER (PARTITION BY a ORDER BY b)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto analytic4,
      NonAggregateAnalyticArg::Create(
          lead, nullptr /* window_frame */,
          std::make_unique<LeadFunction>(Int64Type()), std::move(args4),
          std::move(args4_2), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c5, DerefExpr::Create(c, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args5;
  args5.push_back(std::move(deref_c5));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_two, ConstExpr::Create(Int64(2)));

  std::vector<std::unique_ptr<ValueExpr>> args5_2;
  args5_2.push_back(std::move(const_two));

  // NTH_VALUE(c, 2) OVER (PARTITION BY a ORDER BY b
  //                       RANGE BETWEEN 1 FOLLOWING AND 2 FOLLOWING)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto analytic5,
      NonAggregateAnalyticArg::Create(
          nth_value,
          AnalyticWindowTest::CreateWindowFrameFromParam(
              AnalyticWindowTest::CreateOffsetFollowingOffsetFollowing(
                  WindowFrameArg::kRange, 1, 2, TYPE_INT64)),
          std::make_unique<NthValueFunction>(
              Int64Type(), ResolvedAnalyticFunctionCall::DEFAULT_NULL_HANDLING),
          std::move(args5), std::move(args5_2), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> partition_keys;
  partition_keys.emplace_back(
      std::make_unique<KeyArg>(a, std::move(deref_a), KeyArg::kNotApplicable));

  std::vector<std::unique_ptr<KeyArg>> order_keys;
  order_keys.emplace_back(
      std::make_unique<KeyArg>(b, std::move(deref_b), KeyArg::kAscending));

  std::vector<std::unique_ptr<AnalyticArg>> analytic_args;
  analytic_args.push_back(std::move(analytic1));
  analytic_args.push_back(std::move(analytic2));
  analytic_args.push_back(std::move(analytic3));
  analytic_args.push_back(std::move(analytic4));
  analytic_args.push_back(std::move(analytic5));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto analytic_op,
      AnalyticOp::Create(std::move(partition_keys), std::move(order_keys),
                         std::move(analytic_args), std::move(input_op),
                         preserves_order));
  GOOGLESQL_ASSERT_OK(analytic_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EXPECT_EQ(
      "AnalyticOp(\n"
      "+-partition_keys: {\n"
      "| +-$a := $a},\n"
      "+-order_keys: {\n"
      "| +-$b := $b ASC},\n"
      "+-analytic_args: {\n"
      "| +-AggregateAnalyticArg(WindowFrame(\n"
      "|   frame_unit=ROWS,\n"
      "|   start=WindowFrameBoundary(\n"
      "|     boundary_type=UNBOUNDED PRECEDING\n"
      "|   ),\n"
      "|   end=WindowFrameBoundary(\n"
      "|     boundary_type=CURRENT ROW\n"
      "|   )\n"
      "| ), $count_dist := DISTINCT Count($c)),\n"
      "| +-AggregateAnalyticArg(WindowFrame(\n"
      "|   frame_unit=RANGE,\n"
      "|   start=WindowFrameBoundary(\n"
      "|     boundary_type=OFFSET PRECEDING,\n"
      "|     boundary_offset_expr=ConstExpr(1)\n"
      "|   ),\n"
      "|   end=WindowFrameBoundary(\n"
      "|     boundary_type=CURRENT ROW\n"
      "|   )\n"
      "| ), $sum := Sum($c)),\n"
      "| +-NonAggregateAnalyticArg($rank := Rank()),\n"
      "| +-NonAggregateAnalyticArg($lead := Lead($c, ConstExpr(2), "
      "ConstExpr(-1))),\n"
      "| +-NonAggregateAnalyticArg(WindowFrame(\n"
      "|   frame_unit=RANGE,\n"
      "|   start=WindowFrameBoundary(\n"
      "|     boundary_type=OFFSET FOLLOWING,\n"
      "|     boundary_offset_expr=ConstExpr(1)\n"
      "|   ),\n"
      "|   end=WindowFrameBoundary(\n"
      "|     boundary_type=OFFSET FOLLOWING,\n"
      "|     boundary_offset_expr=ConstExpr(2)\n"
      "|   )\n"
      "| ), $nth_value := Nth_value($c, ConstExpr(2)))},\n"
      "+-input: TestRelationalOp)",
      analytic_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      analytic_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(a, b, c, count_dist, sum, rank, lead, nth_value));

  std::vector<VariableId> expected_variables = input_variables;
  std::vector<TupleData> expected_tuples = input_tuples;

  // In the following comments, we use "{" and "}" to indicate the start
  // and the end boundary for each partition.

  // count(distinct c)
  //   -c: {1, 2, 1, null}, {null, 1, 2, 1}
  //   -window:   ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
  //   -expected: {1, 2, 2, 2}, {0, 1, 2, 2}
  AddColumn(count_dist,
            {Int64(1), Int64(2), Int64(2), Int64(2), Int64(0), Int64(1),
             Int64(2), Int64(2)},
            &expected_variables, &expected_tuples);

  // sum(c)
  //   -b: {null, null, 1, 1}, {1, 2, 2, 3}
  //   -c: {1, 2, 1, null}, {null, 1, 2, 1}
  //   -window:   RANGE BETWEEN 1 PRECEDING AND CURRENT ROW
  //   -expected: {3, 3, 1, 1}, {null, 3, 3, 4}
  AddColumn(sum,
            {Int64(3), Int64(3), Int64(1), Int64(1), NullInt64(), Int64(3),
             Int64(3), Int64(4)},
            &expected_variables, &expected_tuples);

  // rank()
  //   -order key: {null, null, 1, 1}, {1, 2, 2, 3}
  //   -expected:  {1, 1, 3, 3}, {1, 2, 2, 4}
  AddColumn(rank,
            {Int64(1), Int64(1), Int64(3), Int64(3), Int64(1), Int64(2),
             Int64(2), Int64(4)},
            &expected_variables, &expected_tuples);

  // lead(c, 2, -1)
  //   -c: {1, 2, 1, null}, {null, 1, 2, 1}
  //   -expected: {1, null, -1, -1}, {2, 1, -1, -1}
  AddColumn(lead,
            {Int64(1), NullInt64(), Int64(-1), Int64(-1), Int64(2), Int64(1),
             Int64(-1), Int64(-1)},
            &expected_variables, &expected_tuples);

  // nth_value(c, 2)
  //   -b: {null, null, 1, 1}, {1, 2, 2, 3}
  //   -c: {1, 2, 1, null], {null, 1, 2, 1}
  //   -window:   RANGE BETWEEN 1 FOLLOWING AND 2 FOLLOWING
  //   -expected: {2, 2, null, null}, {2, null, null, null}
  AddColumn(nth_value,
            {Int64(2), Int64(2), NullInt64(), NullInt64(), Int64(2),
             NullInt64(), NullInt64(), NullInt64()},
            &expected_variables, &expected_tuples);

  const TupleSchema expected_output_schema(expected_variables);

  EvaluationContext context((EvaluationOptions()));

  // Create an iterator and test it.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      analytic_op->CreateIterator(EmptyParams(),
                                  /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "AnalyticTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), expected_tuples.size());
  for (int i = 0; i < expected_tuples.size(); ++i) {
    EXPECT_EQ(
        Tuple(&expected_output_schema, &data[i]).DebugString(),
        Tuple(&expected_output_schema, &expected_tuples[i]).DebugString());
    // Check for the extra slot.
    EXPECT_EQ(data[i].num_slots(), expected_output_schema.num_variables() + 1);
  }

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, analytic_op->CreateIterator(EmptyParams(),
                                        /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works, but only if preserves_order is false.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, analytic_op->CreateIterator(EmptyParams(),
                                                         /*num_extra_slots=*/1,
                                                         &scramble_context));
  if (preserves_order) {
    EXPECT_EQ(iter->DebugString(), "AnalyticTupleIterator(TestTupleIterator)");
    EXPECT_TRUE(iter->PreservesOrder());
  } else {
    EXPECT_EQ(iter->DebugString(),
              "ReorderingTupleIterator(AnalyticTupleIterator("
              "TestTupleIterator))");
    EXPECT_FALSE(iter->PreservesOrder());
  }
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), expected_tuples.size());

  // Check that if the memory bound is too low, we return an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/1000));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> memory_iter,
      analytic_op->CreateIterator(EmptyParams(),
                                  /*num_extra_slots=*/1, &memory_context));
  EXPECT_THAT(ReadFromTupleIterator(memory_iter.get()),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

}  // namespace
}  // namespace googlesql
