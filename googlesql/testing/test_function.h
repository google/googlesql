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

#ifndef GOOGLESQL_TESTING_TEST_FUNCTION_H_
#define GOOGLESQL_TESTING_TEST_FUNCTION_H_

#include <stddef.h>

#include <array>
#include <functional>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status.h"

namespace googlesql {

class ValueConstructor;

// Represents the inputs and expected output of a query. If the expected
// 'result' is a value, 'status' must be set to absl::Status::OK. Otherwise,
// 'result' must be set to the NULL value of the proper result type.
// 'status' should be set to OUT_OF_RANGE to indicate execution errors, or
// INVALID_ARGUMENT to indicate analysis errors.
//
// Note: The error status and NULL result type are used in AddSafeFunctionCalls
// in compliance_test_cases.cc to automatically add SAFE-mode versions of all
// cases that return errors.  In SAFE mode, functions return NULL instead
// of errors.
//
// Optionally a query may have multiple results, with each mapping to a
// different feature set.
class QueryParamsWithResult {
 public:
  using NamedValueConstructor = std::pair<absl::string_view, ValueConstructor>;

  struct Result {
    Value result;
    absl::Status status;
    FloatMargin float_margin = kExactFloatMargin;

    // Note: if not set via SetResult() the `result` in invalid.
    explicit Result() = default;

    explicit Result(const ValueConstructor& result_in);

    Result(const ValueConstructor& result_in, FloatMargin float_margin_in);

    Result(const ValueConstructor& result_in, const absl::Status& status,
           FloatMargin float_margin_in);

    Result(const ValueConstructor& result_in, absl::StatusCode code);

    Result(const ValueConstructor& result_in, const absl::Status& status_in);
  };

  // Constructs an instance that contains a single result.
  QueryParamsWithResult(absl::Span<const ValueConstructor> arguments,
                        const ValueConstructor& result,
                        absl::Status status = absl::OkStatus());

  QueryParamsWithResult(absl::Span<const ValueConstructor> arguments,
                        const ValueConstructor& result,
                        FloatMargin float_margin_arg,
                        absl::Status status = absl::OkStatus());

  QueryParamsWithResult(absl::Span<const ValueConstructor> arguments,
                        const ValueConstructor& result, absl::StatusCode code);

  QueryParamsWithResult(absl::Span<const ValueConstructor> arguments,
                        const ValueConstructor& result,
                        absl::string_view error_substring);

  QueryParamsWithResult(absl::Span<const ValueConstructor> params,
                        absl::StatusOr<Value>, const Type* output_type);

  QueryParamsWithResult() = default;

  // Setter functions.
  //
  // Clients can either use the constructors or directly set values via the
  // setter functions below.
  QueryParamsWithResult& SetOrdinalArguments(
      absl::Span<const ValueConstructor> ordinal_args);

  QueryParamsWithResult& SetNamedValueArguments(
      absl::Span<const NamedValueConstructor> named_value_constructor_params);

  QueryParamsWithResult& SetResult(
      const ValueConstructor& result, absl::Status status = absl::OkStatus(),
      FloatMargin float_margin = kExactFloatMargin) {
    result_ = Result(result, status, float_margin);
    return *this;
  }

  // If the instance contains multiple results, results are keyed on
  // FeatureSets.
  typedef std::set<LanguageFeature> FeatureSet;
  typedef std::map<FeatureSet, Result> ResultMap;

  // Returns a copy of this test case with the result value inverted.  The
  // result type must be bool and the required features must be empty.
  QueryParamsWithResult CopyWithInvertedResult() const;

  // Returns a copy of this test case with 'feature' made a required feature.
  ABSL_DEPRECATED("AddRequiredFeature is more efficient and more idiomatic.")
  QueryParamsWithResult WrapWithFeature(LanguageFeature feature) const;

  // Returns a copy of this test case with 'feature_set' added to required
  // features.
  ABSL_DEPRECATED("AddRequiredFeatures is more efficient and more idiomatic.")
  QueryParamsWithResult WrapWithFeatureSet(FeatureSet feature_set) const;

  // Adds a required feature to this test in-place and returns a reference
  // to the object so that calls to this function can be chained. Required
  // features are applied to all existing results in the test. Tests that have
  // required features applied will not run when test drivers do not enable the
  // LanguageFeature.
  QueryParamsWithResult& AddRequiredFeature(LanguageFeature feature);
  QueryParamsWithResult& AddRequiredFeatures(const FeatureSet& feature);

  // Removes a required feature to this test in-place and returns a reference
  // to the object so that calls to this function can be chained. Required
  // features are applied to all existing results in the test. Tests that have
  // required features applied will not run when test drivers do not enable the
  // LanguageFeature.
  QueryParamsWithResult& RemoveRequiredFeature(LanguageFeature feature);
  QueryParamsWithResult& RemoveRequiredFeatures(const FeatureSet& feature);

  // Adds a prohibited feature to this test in-place and returns a reference
  // to the object so that calls to this function can be chained. Prohibited
  // features are applied to all existing results in the test. Tests that have
  // prohibited features will not run when test drivers enable the
  // LanguageFeature.
  QueryParamsWithResult& AddProhibitedFeature(LanguageFeature feature);
  QueryParamsWithResult& AddProhibitedFeatures(const FeatureSet& feature);

  // Returns the list of parameters. This is combined list of ordinal &
  // named-value arguments. Named-value arguments are specified after ordinal
  // parameters as this is an invariant of function signatures.
  const std::vector<Value>& params() const { return params_; }

  absl::Span<const Value> ordinal_params() const {
    return absl::MakeSpan(params_).subspan(0, ordinal_param_size_);
  }

  absl::Span<const std::string> named_value_params_names() const {
    return absl::MakeSpan(named_value_params_);
  }
  absl::Span<const Value> named_value_params_values() const {
    return absl::MakeSpan(params_).subspan(ordinal_param_size_);
  }

  // Returns the i-th parameter.
  const Value& param(size_t i) const { return params_[i]; }
  Value* mutable_param(size_t i) { return &params_[i]; }

  // Returns the number of parameters.
  size_t num_params() const { return params_.size(); }

  // Accessors for the common case where there is only one feature set and it is
  // empty. Otherwise, it is a fatal error to call these accessors.
  const Value& result() const { return result_.result; }
  const absl::Status& status() const { return result_.status; }
  const FloatMargin& float_margin() const { return result_.float_margin; }

  // Applies a mutation to all result values.
  void MutateResultValue(std::function<void(Value&)> result_mutator) {
    result_mutator(result_.result);
  }
  void MutateResult(std::function<void(Result&)> result_mutator) {
    result_mutator(result_);
  }

  // Accessor/setter for the ResultMap. We do not allow mutating the ResultMap
  // directly because we need to ensure that it can never be empty.
  ABSL_DEPRECATED("Access result directly.")
  ResultMap results() const { return {{required_features(), result_}}; }

  // Returns the set of features that must be enabled for the test statement to
  // return correct results.
  const FeatureSet& required_features() const { return required_features_; }

  // Returns the set of features that, if enabled, we expect the test to not
  // return correct results.
  const FeatureSet& prohibited_features() const { return prohibited_features_; }

 private:
  std::vector<Value> params_;
  std::vector<std::string> named_value_params_;
  size_t ordinal_param_size_ = 0;
  Result result_;
  FeatureSet required_features_;
  FeatureSet prohibited_features_;
  // Copyable.
};

// Return a vector of test cases with boolean results inverted, as in
// CopyWithInvertedResult above.
std::vector<QueryParamsWithResult> InvertResults(
    absl::Span<const QueryParamsWithResult> tests);

struct FunctionTestCall {
  std::string function_name;
  QueryParamsWithResult params;

  FunctionTestCall(absl::string_view function_name,
                   absl::Span<const ValueConstructor> arguments,
                   const ValueConstructor& result,
                   FloatMargin float_margin = kExactFloatMargin);

  FunctionTestCall(absl::string_view function_name,
                   absl::Span<const ValueConstructor> arguments,
                   const ValueConstructor& result, absl::StatusCode code);

  FunctionTestCall(absl::string_view function_name,
                   absl::Span<const ValueConstructor> arguments,
                   const ValueConstructor& result, absl::Status status);

  FunctionTestCall(absl::string_view function_name_in,
                   const QueryParamsWithResult& params_in);
};

std::ostream& operator<<(std::ostream& out,
                         const QueryParamsWithResult::FeatureSet& f);
std::ostream& operator<<(std::ostream& out, const FunctionTestCall& f);
std::ostream& operator<<(std::ostream& out, const QueryParamsWithResult& p);

}  // namespace googlesql

#endif  // GOOGLESQL_TESTING_TEST_FUNCTION_H_
