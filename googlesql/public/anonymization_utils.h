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

#ifndef GOOGLESQL_PUBLIC_ANONYMIZATION_UTILS_H_
#define GOOGLESQL_PUBLIC_ANONYMIZATION_UTILS_H_

#include <memory>
#include <optional>

#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"

namespace googlesql {
namespace anonymization {

// We are rewriting ANON_VAR_POP, ANON_STDDEV_POP, ANON_PERCENTILE_CONT, and
// ANON_QUANTILES, to per-user aggregation
// ARRAY_AGG(expr IGNORE NULLS ORDER BY rand() LIMIT 5).
// The limit of 5 is proposed to be consistent with the current
// implementation, and at some point we may want to make this configurable.
// For more information, see (broken link).
static constexpr int kPerUserArrayAggLimit = 5;

// Computes the threshold for Laplace partition selection from anonymization
// option delta.
//
// epsilon_value and delta_value must be a valid
// Value, while max_groups_contributed_value is optional (as indicated by an
// invalid Value).
//
// A valid max_groups_contributed_value implies contributions are being bounded
// across GROUP BY groups, while an invalid max_groups_contributed_value implies
// the opposite.
//
// Returns an error if the inputs are not logically valid.
absl::StatusOr<Value> ComputeLaplaceThresholdFromDelta(
    Value epsilon_value, Value delta_value, Value max_groups_contributed_value);

// Computes anonymization option delta from the threshold for Laplace
// partition selection.
//
// epsilon_value and laplace_threshold_value must be valid Values, while
// max_groups_contributed_value is optional (as indicated by an invalid Value).
//
// A valid max_groups_contributed_value implies contributions are being bounded
// across GROUP BY groups, while an invalid max_groups_contributed_value implies
// the opposite.
//
// Returns an error if the inputs are not logically valid, or if the computed
// delta is outside the valid range [0.0, 1.0).
absl::StatusOr<Value> ComputeDeltaFromLaplaceThreshold(
    Value epsilon_value, Value laplace_threshold_value,
    Value max_groups_contributed_value);

// Provides epsilon for each aggregate function call in a differential privacy
// aggregate scan. Use this class for
// 1. consistent behavior across engines,
// 2. to avoid re-implementation of critical logic, and
// 3. to reduce the surface for privacy audits.
class FunctionEpsilonAssigner {
 public:
  // Creates a new epsilon assigner based on the options and aggregations in the
  // scan. Returns an error in case the sum of all `epsilon` named arguments
  // exceeds the `epsilon` option of the scan. Must be a rewritten scan, so that
  // additional aggregations for group selection might already have been added.
  //
  // The evaluator must be provided and cannot be null.  It is used to evaluate
  // DP options and named-arguments on DP aggregation functions.
  static absl::StatusOr<std::unique_ptr<FunctionEpsilonAssigner>>
  CreateFromScan(const ResolvedDifferentialPrivacyAggregateScan* scan,
                 std::unique_ptr<ConstantEvaluator> evaluator);

  // Deprecated function that will be removed once all engines are migrated to
  // the new factory function that also accepts an evaluator (see above).
  [[deprecated("Use the function that accepts an evaluator instead.")]]
  static absl::StatusOr<std::unique_ptr<FunctionEpsilonAssigner>>
  CreateFromScan(const ResolvedDifferentialPrivacyAggregateScan* scan);

  // Returns the epsilon value for the differential privacy aggregation function
  // in the context of the scan that has been used to create this object.
  //
  // In particular, the returned value depends on two classes of aggregate
  // functions in the scan:
  // 1. Aggregate functions with non-null `epsilon` named argument.
  // 2. Aggregate functions with null `epsilon` named argument or for which no
  //    `epsilon` named argument is supported.
  //
  // For 1, the value of the `epsilon` named argument will be returned. For 2,
  // the returned value will be the `epsilon` option **on the scan** minus the
  // sum of all `epsilon` named arguments of functions in 1, equally split among
  // the number of aggregate functions in 2.
  virtual absl::StatusOr<double> GetEpsilonForFunction(
      const ResolvedFunctionCallBase* function_call) = 0;

  // Returns the total epsilon of the scan that has been used to create this
  // object. This includes the epsilon budget for aggregations as well as for
  // group selection.
  //
  // In case all aggregations of the scan have an `epsilon` parameter, the sum
  // of those parameters is returned.  Otherwise, the `epsilon` of the scan is
  // returned.
  //
  // CAVEAT: The `epsilon` on a DP scan is ONLY optional in case that all
  // containing aggregate functions have an `epsilon` parameter.
  virtual double GetTotalEpsilon() = 0;

  // Returns the epsilon used in the query for group selection.  Returns nullopt
  // in case the query does not use group selection (e.g., because it uses
  // public groups).
  virtual std::optional<double> GetGroupSelectionEpsilon() = 0;

  virtual ~FunctionEpsilonAssigner() = default;
};

}  // namespace anonymization
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANONYMIZATION_UTILS_H_
