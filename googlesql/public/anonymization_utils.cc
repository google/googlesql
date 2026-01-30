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

#include "googlesql/public/anonymization_utils.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "algorithms/partition-selection.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace anonymization {
namespace {
constexpr absl::string_view kArgumentNameEpsilon = "epsilon";
constexpr absl::string_view kOptionGroupSelectionEpsilon =
    "group_selection_epsilon";
constexpr absl::string_view kOptionGroupSelectionStrategy =
    "group_selection_strategy";
constexpr absl::string_view kOptionMinPrivacyUnitsPerGroup =
    "min_privacy_units_per_group";

// The minimum epsilon for even budget splitting.  If the remaining epsilon
// after subtracting the epsilon parameters is less than this value, then the
// query will fail.  A value of 1e-12 means that we would add Laplace noise with
// at least variance ~1.4e12 to the raw value (assuming sensitivity of 1), which
// leads to very bad utility.  Users can still specify an `epsilon` named
// argument on the aggregation functions to get around this.
constexpr double kMinEpsilonForEvenSplitting = 1e-12;
}  // namespace

absl::StatusOr<Value> ComputeLaplaceThresholdFromDelta(
    Value epsilon_value, Value delta_value,
    Value max_groups_contributed_value) {
  if (!max_groups_contributed_value.is_valid()) {
    // Invalid max_groups_contributed_value indicates unset.  Unset
    // max_groups_contributed_value implies contributions are not being bounded
    // across GROUP BY groups.  Setting max_groups_contributed_value to 1 is
    // equivalent to removing it from the threshold calculation.
    max_groups_contributed_value = Value::Int64(1);
  }
  GOOGLESQL_RET_CHECK_EQ(epsilon_value.type_kind(), TYPE_DOUBLE);
  GOOGLESQL_RET_CHECK_EQ(delta_value.type_kind(), TYPE_DOUBLE);
  GOOGLESQL_RET_CHECK_EQ(max_groups_contributed_value.type_kind(), TYPE_INT64);

  const double epsilon = epsilon_value.double_value();
  const double delta = delta_value.double_value();
  const int64_t max_groups_contributed =
      max_groups_contributed_value.int64_value();

  // The caller should have verified that epsilon and delta are not infinity
  // or nan.  The caller should also have verified that epsilon > 0, and that
  // delta is in the range [0, 1]. max_groups_contributed must also be > 0.
  GOOGLESQL_RET_CHECK(!std::isnan(epsilon));
  GOOGLESQL_RET_CHECK(!std::isnan(delta));
  GOOGLESQL_RET_CHECK(!std::isinf(epsilon));
  GOOGLESQL_RET_CHECK(!std::isinf(delta));
  GOOGLESQL_RET_CHECK_GT(epsilon, 0);
  GOOGLESQL_RET_CHECK_GE(delta, 0);
  GOOGLESQL_RET_CHECK_LE(delta, 1);
  GOOGLESQL_RET_CHECK_GT(max_groups_contributed, 0);

  GOOGLESQL_ASSIGN_OR_RETURN(
      double double_laplace_threshold,
      differential_privacy::LaplacePartitionSelection::CalculateThreshold(
          epsilon, delta, max_groups_contributed));

  double_laplace_threshold = ceil(double_laplace_threshold);
  int64_t laplace_threshold;
  if (double_laplace_threshold >=
      static_cast<double>(std::numeric_limits<int64_t>::max())) {
    laplace_threshold = std::numeric_limits<int64_t>::max();
  } else if (double_laplace_threshold <=
             std::numeric_limits<int64_t>::lowest()) {
    laplace_threshold = std::numeric_limits<int64_t>::lowest();
  } else {
    laplace_threshold = static_cast<int64_t>(double_laplace_threshold);
  }

  return Value::Int64(laplace_threshold);
}

absl::StatusOr<Value> ComputeDeltaFromLaplaceThreshold(
    Value epsilon_value, Value laplace_threshold_value,
    Value max_groups_contributed_value) {
  if (!max_groups_contributed_value.is_valid()) {
    // Invalid max_groups_contributed_value indicates unset.  Unset
    // max_groups_contributed_value implies contributions are not being bounded
    // across GROUP BY groups.  Setting max_groups_contributed_value to 1 is
    // equivalent to removing it from the delta calculation.
    max_groups_contributed_value = Value::Int64(1);
  }
  GOOGLESQL_RET_CHECK_EQ(epsilon_value.type_kind(), TYPE_DOUBLE);
  GOOGLESQL_RET_CHECK_EQ(laplace_threshold_value.type_kind(), TYPE_INT64);
  GOOGLESQL_RET_CHECK_EQ(max_groups_contributed_value.type_kind(), TYPE_INT64);

  const double epsilon = epsilon_value.double_value();
  const double laplace_threshold = laplace_threshold_value.int64_value();
  const int64_t max_groups_contributed =
      max_groups_contributed_value.int64_value();

  // The caller should have verified that epsilon is not infinity or nan.
  // The caller should have also verified that epsilon is greater than 0.
  // Note that laplace_threshold is allowed to be negative.
  // max_groups_contributed must also be greater than 0.
  GOOGLESQL_RET_CHECK(!std::isnan(epsilon));
  GOOGLESQL_RET_CHECK(!std::isinf(epsilon));
  GOOGLESQL_RET_CHECK_GT(epsilon, 0);
  GOOGLESQL_RET_CHECK_GT(max_groups_contributed, 0);

  GOOGLESQL_ASSIGN_OR_RETURN(
      double delta,
      differential_privacy::LaplacePartitionSelection::CalculateDelta(
          epsilon, laplace_threshold, max_groups_contributed));

  if (std::isnan(delta) || delta < 0.0 || delta > 1.0) {
    GOOGLESQL_RET_CHECK_FAIL() << "Invalid computed delta; delta: " << delta
                     << ", epsilon: " << epsilon
                     << ", laplace_threshold: " << laplace_threshold
                     << ", max_groups_contributed: " << max_groups_contributed;
  }

  return Value::Double(delta);
}

namespace {

std::optional<int> GetEpsilonArgumentIndex(const FunctionSignature& signature) {
  int index = 0;
  for (const FunctionArgumentType& arg : signature.arguments()) {
    if (arg.has_argument_name() &&
        googlesql_base::CaseEqual(arg.argument_name(), kArgumentNameEpsilon)) {
      return index;
    }
    ++index;
  }
  return std::nullopt;
}

const ResolvedExpr* GetEpsilonArgumentExpr(
    const ResolvedFunctionCallBase* call) {
  std::optional epsilon_index = GetEpsilonArgumentIndex(call->signature());
  if (!epsilon_index.has_value()) {
    return nullptr;
  }
  return call->argument_list(epsilon_index.value());
}

class FunctionEpsilonAssignerImpl : public FunctionEpsilonAssigner {
 public:
  explicit FunctionEpsilonAssignerImpl(
      double split_remainder_epsilon, double total_epsilon,
      std::optional<double> group_selection_epsilon,
      std::unique_ptr<ConstantEvaluator> evaluator)
      : split_remainder_epsilon_(split_remainder_epsilon),
        total_epsilon_(total_epsilon),
        group_selection_epsilon_(group_selection_epsilon),
        evaluator_(std::move(evaluator)) {}

  absl::StatusOr<double> GetEpsilonForFunction(
      const ResolvedFunctionCallBase* function_call) override {
    const ResolvedExpr* epsilon_expr = GetEpsilonArgumentExpr(function_call);
    if (epsilon_expr == nullptr) {
      return split_remainder_epsilon_;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        Value epsilon_value, evaluator_->Evaluate(*epsilon_expr),
        _.SetPrepend()
            << "Error evaluating `epsilon` named-argument expression: ");
    if (epsilon_value.is_null()) {
      return split_remainder_epsilon_;
    }
    GOOGLESQL_RET_CHECK(epsilon_value.type()->IsDouble());
    return epsilon_value.double_value();
  }

  double GetTotalEpsilon() override { return total_epsilon_; }

  std::optional<double> GetGroupSelectionEpsilon() override {
    return group_selection_epsilon_;
  }

 private:
  // The equally split epsilon value from the scan options, divided by the
  // number of aggregate functions having a null `epsilon` named argument or do
  // not support such a named argument.
  const double split_remainder_epsilon_;

  // The total epsilon of the query.
  //
  // In case some aggregate functions have no `epsilon` named argument, this is
  // the `epsilon` of the `OPTIONS`.  Otherwise, this is the sum of all
  // `epsilon` named arguments.
  const double total_epsilon_;

  // The epsilon for group selection.  In case this is a nullopt, no group
  // selection is used in this query.
  const std::optional<double> group_selection_epsilon_;

  // The evaluator for evaluating non-literals that are constant expressions.
  std::unique_ptr<ConstantEvaluator> evaluator_;
};

class EpsilonParameterCollectingVisitor : public ResolvedASTVisitor {
 public:
  explicit EpsilonParameterCollectingVisitor(ConstantEvaluator* evaluator)
      : evaluator_(evaluator) {}

  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override {
    GOOGLESQL_RET_CHECK(!epsilon_parameter_.has_value());
    std::optional<int> epsilon_index =
        GetEpsilonArgumentIndex(node->signature());
    if (!epsilon_index.has_value()) {
      return DefaultVisit(node);
    }
    const ResolvedExpr* epsilon_expr =
        node->argument_list(epsilon_index.value());
    if (epsilon_expr == nullptr) {
      // Epsilon not set.
      return absl::OkStatus();
    }
    // epsilon named argument
    GOOGLESQL_ASSIGN_OR_RETURN(
        Value epsilon_value, evaluator_->Evaluate(*epsilon_expr),
        _.SetPrepend()
            << "Error evaluating `epsilon` named-argument expression: ");
    if (!epsilon_value.is_null()) {
      GOOGLESQL_RET_CHECK(epsilon_value.type()->IsDouble());
      epsilon_parameter_ = epsilon_value.double_value();
    }
    return absl::OkStatus();
  }

  std::optional<double> GetEpsilonParameterIfPresent() const {
    return epsilon_parameter_;
  }

  absl::Status VisitResolvedDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* node) override {
    return absl::UnimplementedError(
        "Nested differential privacy scans are not supported");
  }

 private:
  std::optional<double> epsilon_parameter_;
  ConstantEvaluator* evaluator_;  // not owned
};

const ResolvedExpr* GetExprOfOptionOrNull(
    absl::Span<const std::unique_ptr<const ResolvedOption>> option_list,
    absl::string_view option_name) {
  for (const std::unique_ptr<const ResolvedOption>& option : option_list) {
    if (googlesql_base::CaseEqual(option->name(), option_name)) {
      return option->value();
    }
  }
  return nullptr;
}

absl::StatusOr<std::optional<double>> GetDoubleValuedOptionIfPresent(
    const ResolvedDifferentialPrivacyAggregateScan* scan,
    absl::string_view option_name, ConstantEvaluator* evaluator) {
  const ResolvedExpr* expr =
      GetExprOfOptionOrNull(scan->option_list(), option_name);
  if (expr == nullptr) {
    return std::nullopt;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(Value value, evaluator->Evaluate(*expr),
                   _.SetPrepend()
                       << "Error evaluating DP OPTION " << option_name << ": ");
  if (value.is_null()) {
    return std::nullopt;
  }
  GOOGLESQL_RET_CHECK(expr->type()->IsDouble());
  return value.double_value();
}

absl::StatusOr<bool> HasNonNullMinPrivacyUnitsPerGroupOption(
    const ResolvedDifferentialPrivacyAggregateScan* scan,
    ConstantEvaluator* evaluator) {
  const ResolvedExpr* min_privacy_units_per_group = GetExprOfOptionOrNull(
      scan->option_list(), kOptionMinPrivacyUnitsPerGroup);
  if (min_privacy_units_per_group == nullptr) {
    return false;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(Value value,
                   evaluator->Evaluate(*min_privacy_units_per_group),
                   _.SetPrepend() << "Error evaluating DP OPTION "
                                  << kOptionMinPrivacyUnitsPerGroup << ": ");
  return !value.is_null();
}

absl::Status CheckEpsilonOption(double scan_epsilon,
                                double parameter_epsilon_sum) {
  if (!std::isfinite(scan_epsilon) || scan_epsilon <= 0) {
    // Backwards compatible error message.
    return absl::InvalidArgumentError(absl::StrCat(
        "Epsilon must be finite and positive, but is ", scan_epsilon));
  }
  const double remaining_epsilon = scan_epsilon - parameter_epsilon_sum;
  if (remaining_epsilon + std::abs(remaining_epsilon) * 1e-8 < 0) {
    return absl::OutOfRangeError(
        "Epsilon overconsumption: the sum of all `epsilon` arguments cannot "
        "exceed the `epsilon` option in SELECT WITH DIFFERENTIAL_PRIVACY "
        "scans");
  }
  return absl::OkStatus();
}

class ThresholdExprColumnIdCollector : public ResolvedASTVisitor {
 public:
  explicit ThresholdExprColumnIdCollector(
      absl::flat_hash_set<int>& referenced_column_ids)
      : referenced_column_ids_(referenced_column_ids) {}

  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    referenced_column_ids_.insert(node->column().column_id());
    return absl::OkStatus();
  }

 private:
  absl::flat_hash_set<int>& referenced_column_ids_;  // Not owned.
};

// Returns the epsilon for group selection.  In case this function returns a
// nullopt, no group selection is used in this query.  In case this function
// returns a NullDouble, no epsilon has been set for the group selection, and
// even budget splitting should be used.
absl::StatusOr<std::optional<Value>> ExtractGroupSelectionEpsilon(
    const ResolvedDifferentialPrivacyAggregateScan* scan,
    ConstantEvaluator* evaluator) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::optional<double> options_group_selection_epsilon,
                   GetDoubleValuedOptionIfPresent(
                       scan, kOptionGroupSelectionEpsilon, evaluator));
  if (options_group_selection_epsilon.has_value()) {
    return Value::Double(options_group_selection_epsilon.value());
  }

  // `group_selection_epsilon` was not set in `OPTIONS`.  In this case, we need
  // to identify the aggregation for the group selection and returns its
  // `epsilon` argument.  We identify the aggregation for the group selection
  // via the `group_selection_threshold_expr`, which contains a `ColumnRef`
  // referring to the group selection aggregation.
  if (scan->group_selection_threshold_expr() == nullptr) {
    // No threshold set, we must use public groups.
    const ResolvedExpr* group_selection_strategy = GetExprOfOptionOrNull(
        scan->option_list(), kOptionGroupSelectionStrategy);
    GOOGLESQL_RET_CHECK(group_selection_strategy != nullptr);
    GOOGLESQL_RET_CHECK_EQ(group_selection_strategy->GetAs<ResolvedLiteral>()
                     ->value()
                     .enum_value(),
                 functions::DifferentialPrivacyEnums::PUBLIC_GROUPS)
        << scan->DebugString();
    return std::nullopt;
  }
  // The `group_selection_threshold_expr` is either a ColumnRef or a
  // FunctionCall.  The latter is the case when `min_privacy_units_per_group`
  // is set in the `OPTIONS`.  In both cases, we can follow the ColumnRef column
  // to find the aggregation that computes it.  There must be at most one
  // aggregation with an `epsilon` argument.  This `epsilon` argument is the
  // epsilon for group selection.
  absl::flat_hash_set<int> referenced_column_ids;
  ThresholdExprColumnIdCollector collector(referenced_column_ids);
  GOOGLESQL_RETURN_IF_ERROR(scan->group_selection_threshold_expr()->Accept(&collector));
  GOOGLESQL_RET_CHECK(!referenced_column_ids.empty());

  // Check aggregations with the columns linked in the
  // `group_selection_threshold_expr`.
  const ResolvedExpr* group_selection_epsilon_expr = nullptr;
  for (const std::unique_ptr<const ResolvedComputedColumnBase>& aggregate :
       scan->aggregate_list()) {
    if (referenced_column_ids.contains(aggregate->column().column_id())) {
      GOOGLESQL_RET_CHECK(aggregate->expr()->Is<ResolvedFunctionCallBase>());
      const ResolvedExpr* epsilon_expr = GetEpsilonArgumentExpr(
          aggregate->expr()->GetAs<ResolvedFunctionCallBase>());
      if (epsilon_expr != nullptr) {
        GOOGLESQL_RET_CHECK(group_selection_epsilon_expr == nullptr)
            << "Expected at most one group selection aggregation with an "
               "epsilon argument "
            << scan->DebugString();
        group_selection_epsilon_expr = epsilon_expr;
      }
    }
  }
  if (group_selection_epsilon_expr == nullptr) {
    // No aggregation with `epsilon` found.  This is okay, e.g., when the
    // per-aggregation epsilon feature is unset.
    return Value::NullDouble();
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      Value value, evaluator->Evaluate(*group_selection_epsilon_expr),
      _.SetPrepend() << "Error evaluating epsilon for group selection: ");
  return value;
}

}  // namespace

absl::StatusOr<std::unique_ptr<FunctionEpsilonAssigner>>
FunctionEpsilonAssigner::CreateFromScan(
    const ResolvedDifferentialPrivacyAggregateScan* scan,
    std::unique_ptr<ConstantEvaluator> evaluator) {
  GOOGLESQL_RET_CHECK(evaluator != nullptr);
  GOOGLESQL_RET_CHECK(scan != nullptr);

  double parameter_epsilon_sum = 0;
  int num_functions_even_splitting = scan->aggregate_list_size();
  bool has_any_function_with_epsilon_parameter = false;

  // If the `min_privacy_units_per_group` option is set, then the rewriter added
  // a column to the aggregate list counting the exact number of distinct users
  // per group/partition. Thus, in that case, the `aggregate_list` contains
  // an additional item without a parameter named `epsilon`.
  GOOGLESQL_ASSIGN_OR_RETURN(
      bool has_non_null_min_privacy_units_per_group_option,
      HasNonNullMinPrivacyUnitsPerGroupOption(scan, evaluator.get()));
  if (has_non_null_min_privacy_units_per_group_option) {
    --num_functions_even_splitting;
  }
  GOOGLESQL_RET_CHECK_GE(num_functions_even_splitting, 0)
      << "This indicates that the query was not rewritten by the anonymization "
         "rewriter";

  for (const std::unique_ptr<const ResolvedComputedColumnBase>& aggregate :
       scan->aggregate_list()) {
    EpsilonParameterCollectingVisitor visitor(evaluator.get());
    GOOGLESQL_RET_CHECK_OK(aggregate->Accept(&visitor));
    std::optional<double> epsilon_parameter =
        visitor.GetEpsilonParameterIfPresent();
    if (epsilon_parameter.has_value()) {
      parameter_epsilon_sum += epsilon_parameter.value();
      --num_functions_even_splitting;
      has_any_function_with_epsilon_parameter = true;
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::optional<double> scan_epsilon,
                   GetDoubleValuedOptionIfPresent(scan, kArgumentNameEpsilon,
                                                  evaluator.get()));
  if (scan_epsilon.has_value()) {
    GOOGLESQL_RETURN_IF_ERROR(
        CheckEpsilonOption(scan_epsilon.value(), parameter_epsilon_sum));

    if (num_functions_even_splitting > 0) {
      const double remaining_epsilon =
          scan_epsilon.value() - parameter_epsilon_sum;
      if (remaining_epsilon <= kMinEpsilonForEvenSplitting) {
        return absl::OutOfRangeError(absl::StrCat(
            "Epsilon overconsumption: The minimum epsilon for even budget "
            "splitting is ",
            kMinEpsilonForEvenSplitting, ", but only ", remaining_epsilon,
            " was remaining. In case you want to use less epsilon budget, "
            "please use the `epsilon` named argument on the aggregation "
            "functions or `group_selection_epsilon` in the `OPTIONS`."));
      }
      const double split_remainder_epsilon =
          remaining_epsilon / num_functions_even_splitting;
      GOOGLESQL_ASSIGN_OR_RETURN(std::optional<Value> group_selection_epsilon_value,
                       ExtractGroupSelectionEpsilon(scan, evaluator.get()));
      std::optional<double> group_selection_epsilon;
      if (group_selection_epsilon_value.has_value()) {
        if (group_selection_epsilon_value.value().is_null()) {
          group_selection_epsilon = split_remainder_epsilon;
        } else {
          group_selection_epsilon =
              group_selection_epsilon_value.value().double_value();
        }
      }
      return std::make_unique<FunctionEpsilonAssignerImpl>(
          split_remainder_epsilon, scan_epsilon.value(),
          group_selection_epsilon, std::move(evaluator));
    }
  }

  if (num_functions_even_splitting == 0) {
    // All aggregates have an epsilon parameter.  Epsilon in the OPTIONS is
    // optional in this case.
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<Value> group_selection_epsilon_value,
                     ExtractGroupSelectionEpsilon(scan, evaluator.get()));
    std::optional<double> group_selection_epsilon;
    if (group_selection_epsilon_value.has_value()) {
      GOOGLESQL_RET_CHECK(!group_selection_epsilon_value.value().is_null());
      group_selection_epsilon =
          group_selection_epsilon_value.value().double_value();
    }
    return std::make_unique<FunctionEpsilonAssignerImpl>(
        0, parameter_epsilon_sum, group_selection_epsilon,
        std::move(evaluator));
  }

  GOOGLESQL_RET_CHECK(!scan_epsilon.has_value());
  if (has_any_function_with_epsilon_parameter) {
    return absl::OutOfRangeError(
        "Differential privacy option EPSILON must be set and non-NULL if some "
        "aggregate functions have no EPSILON argument, or, if group selection "
        "is required, the option GROUP_SELECTION_EPSILON is unset or NULL");
  } else {
    // Backwards compatible error message.
    return absl::InvalidArgumentError(
        "Differential privacy option EPSILON must be set and non-NULL");
  }
}

absl::StatusOr<std::unique_ptr<FunctionEpsilonAssigner>>
FunctionEpsilonAssigner::CreateFromScan(
    const ResolvedDifferentialPrivacyAggregateScan* scan) {
  class LiteralOnlyEvaluator : public ConstantEvaluator {
   public:
    absl::StatusOr<Value> Evaluate(const ResolvedExpr& expr) override {
      GOOGLESQL_RET_CHECK_EQ(expr.node_kind(), RESOLVED_LITERAL) << expr.DebugString();
      return expr.GetAs<ResolvedLiteral>()->value();
    }
  };
  return FunctionEpsilonAssigner::CreateFromScan(
      scan, std::make_unique<LiteralOnlyEvaluator>());
}

}  // namespace anonymization
}  // namespace googlesql
