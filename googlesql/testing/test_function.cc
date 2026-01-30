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

#include "googlesql/testing/test_function.h"

#include <iosfwd>
#include <iterator>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

QueryParamsWithResult::QueryParamsWithResult(
    absl::Span<const ValueConstructor> arguments,
    const ValueConstructor& result, absl::Status status)
    : params_(ValueConstructor::ToValues(arguments)),
      ordinal_param_size_(arguments.size()),
      result_(result, status) {}

QueryParamsWithResult::QueryParamsWithResult(
    absl::Span<const ValueConstructor> arguments,
    const ValueConstructor& result, FloatMargin float_margin_arg,
    absl::Status status)
    : params_(ValueConstructor::ToValues(arguments)),
      ordinal_param_size_(arguments.size()),
      result_(result, status, float_margin_arg) {}

QueryParamsWithResult::QueryParamsWithResult(
    absl::Span<const ValueConstructor> arguments,
    const ValueConstructor& result, absl::string_view error_substring)
    : params_(ValueConstructor::ToValues(arguments)),
      ordinal_param_size_(arguments.size()),
      result_(result,
              error_substring.empty()
                  ? absl::OkStatus()
                  : absl::Status(absl::StatusCode::kUnknown, error_substring)) {
}

QueryParamsWithResult::QueryParamsWithResult(
    absl::Span<const ValueConstructor> arguments,
    const ValueConstructor& result, absl::StatusCode code)
    : params_(ValueConstructor::ToValues(arguments)),
      ordinal_param_size_(arguments.size()),
      result_(result, code) {}

QueryParamsWithResult::QueryParamsWithResult(
    absl::Span<const ValueConstructor> params,
    absl::StatusOr<Value> status_or_result, const Type* output_type)
    : params_(ValueConstructor::ToValues(params)),
      ordinal_param_size_(params.size()),
      result_(status_or_result.ok() ? status_or_result.value()
                                    : Value::Null(output_type),
              status_or_result.status()) {}

QueryParamsWithResult& QueryParamsWithResult::SetOrdinalArguments(
    absl::Span<const ValueConstructor> ordinal_args) {
  std::vector<Value> new_params;
  new_params.reserve(ordinal_args.size() + named_value_params_.size());
  // Ordinal parameters are always placed before named-value args. This is
  // an invariant of GoogleSQL function signatures.
  new_params = ValueConstructor::ToValues(ordinal_args);
  // Copy named-value params after ordinal params are inserted.
  new_params.insert(
      new_params.end(),
      std::make_move_iterator(named_value_params_values().begin()),
      std::make_move_iterator(named_value_params_values().end()));
  ordinal_param_size_ = ordinal_args.size();
  params_ = std::move(new_params);
  return *this;
}

QueryParamsWithResult& QueryParamsWithResult::SetNamedValueArguments(
    absl::Span<const NamedValueConstructor> named_value_constructor_params) {
  std::vector<Value> new_params;
  new_params.reserve(ordinal_param_size_ +
                     named_value_constructor_params.size());
  // Copy ordinal params.
  new_params.insert(new_params.end(),
                    std::make_move_iterator(ordinal_params().begin()),
                    std::make_move_iterator(ordinal_params().end()));
  // Add named-value arguments.
  named_value_params_.clear();
  for (const auto& [key, value] : named_value_constructor_params) {
    named_value_params_.push_back(std::string(key));
    new_params.push_back(value.get());
  }
  params_ = std::move(new_params);
  return *this;
}

QueryParamsWithResult::Result::Result(const ValueConstructor& result_in)
    : result(result_in.get()), status() {}

QueryParamsWithResult::Result::Result(const ValueConstructor& result_in,
                                      FloatMargin float_margin_in)
    : result(result_in.get()), status(), float_margin(float_margin_in) {}

QueryParamsWithResult::Result::Result(const ValueConstructor& result_in,
                                      const absl::Status& status_in,
                                      FloatMargin float_margin_in)
    : result(result_in.get()),
      status(status_in),
      float_margin(float_margin_in) {}

QueryParamsWithResult::Result::Result(const ValueConstructor& result_in,
                                      absl::StatusCode code)
    : result(result_in.get()), status(code, "") {}

QueryParamsWithResult::Result::Result(const ValueConstructor& result_in,
                                      const absl::Status& status_in)
    : result(result_in.get()), status(status_in) {}

QueryParamsWithResult QueryParamsWithResult::CopyWithInvertedResult() const {
  ABSL_CHECK(required_features().empty()) << *this;
  const Value& value = result();
  ABSL_CHECK_EQ(value.type_kind(), TYPE_BOOL);
  return QueryParamsWithResult(
      std::vector<ValueConstructor>(params_.begin(), params_.end()),
      value.is_null() ? Value::NullBool() : Value::Bool(!value.bool_value()),
      status());
}

QueryParamsWithResult QueryParamsWithResult::WrapWithFeature(
    LanguageFeature feature) const {
  QueryParamsWithResult copy = *this;
  return copy.AddRequiredFeature(feature);
}

QueryParamsWithResult QueryParamsWithResult::WrapWithFeatureSet(
    FeatureSet feature_set) const {
  QueryParamsWithResult copy = *this;
  return copy.AddRequiredFeatures(feature_set);
}

QueryParamsWithResult& QueryParamsWithResult::AddRequiredFeature(
    LanguageFeature feature) {
  return AddRequiredFeatures({feature});
}

QueryParamsWithResult& QueryParamsWithResult::AddRequiredFeatures(
    const FeatureSet& features) {
  for (LanguageFeature feature : features) {
    ABSL_DCHECK(!googlesql_base::ContainsKey(prohibited_features_, feature));
  }
  required_features_.insert(features.begin(), features.end());
  return *this;
}

QueryParamsWithResult& QueryParamsWithResult::RemoveRequiredFeature(
    LanguageFeature feature) {
  return RemoveRequiredFeatures({feature});
}

QueryParamsWithResult& QueryParamsWithResult::RemoveRequiredFeatures(
    const FeatureSet& features) {
  for (LanguageFeature feature : features) {
    required_features_.erase(feature);
  }
  return *this;
}

QueryParamsWithResult& QueryParamsWithResult::AddProhibitedFeature(
    LanguageFeature feature) {
  ABSL_DCHECK(!googlesql_base::ContainsKey(required_features_, feature));
  prohibited_features_.insert(feature);
  return *this;
}

QueryParamsWithResult& QueryParamsWithResult::AddProhibitedFeatures(
    const FeatureSet& features) {
  for (LanguageFeature feature : features) {
    ABSL_DCHECK(!googlesql_base::ContainsKey(prohibited_features_, feature));
  }
  prohibited_features_.insert(features.begin(), features.end());
  return *this;
}

std::vector<QueryParamsWithResult> InvertResults(
    absl::Span<const QueryParamsWithResult> tests) {
  std::vector<QueryParamsWithResult> new_tests;
  new_tests.reserve(tests.size());
  for (const QueryParamsWithResult& test : tests) {
    new_tests.push_back(test.CopyWithInvertedResult());
  }
  return new_tests;
}

FunctionTestCall::FunctionTestCall(absl::string_view function_name,
                                   absl::Span<const ValueConstructor> arguments,
                                   const ValueConstructor& result,
                                   FloatMargin float_margin)
    : function_name(function_name), params(arguments, result, float_margin) {}

FunctionTestCall::FunctionTestCall(absl::string_view function_name,
                                   absl::Span<const ValueConstructor> arguments,
                                   const ValueConstructor& result,
                                   absl::StatusCode code)
    : function_name(function_name), params(arguments, result, code) {}

FunctionTestCall::FunctionTestCall(absl::string_view function_name,
                                   absl::Span<const ValueConstructor> arguments,
                                   const ValueConstructor& result,
                                   absl::Status status)
    : function_name(function_name), params(arguments, result, status) {}

FunctionTestCall::FunctionTestCall(absl::string_view function_name_in,
                                   const QueryParamsWithResult& params_in)
    : function_name(function_name_in), params(params_in) {}

std::ostream& operator<<(std::ostream& out,
                         const QueryParamsWithResult::FeatureSet& f) {
  std::vector<std::string> features;
  for (const LanguageFeature feature : f) {
    features.push_back(LanguageFeature_Name(feature));
  }
  return out << "{" << absl::StrJoin(features, ", ") << "}";
}

std::ostream& operator<<(std::ostream& out, const FunctionTestCall& f) {
  return out << "FunctionTestCall[function_name: " << f.function_name
             << ", params: " << f.params << "]";
}

std::ostream& operator<<(std::ostream& out, const QueryParamsWithResult& p) {
  std::vector<std::string> arguments;
  arguments.reserve(p.params().size());
  for (int i = 0; i < p.params().size(); ++i) {
    std::string param_string = p.param(i).FullDebugString();
    arguments.push_back(param_string);
  }

  out << "QueryParamsWithResult[params: {" << absl::StrJoin(arguments, ", ")
      << "}, ";
  if (!p.required_features().empty()) {
    out << " with features: " << p.required_features() << ", ";
  }
  out << "result: " << p.result().FullDebugString()
      << ", float_margin: " << p.float_margin() << "  status: " << p.status()
      << "]";
  return out;
}

}  // namespace googlesql
