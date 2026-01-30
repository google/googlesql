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

#ifndef GOOGLESQL_REFERENCE_IMPL_FUNCTIONS_LIKE_H_
#define GOOGLESQL_REFERENCE_IMPL_FUNCTIONS_LIKE_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "re2/re2.h"

namespace googlesql {

// Parameters required for evaluation of like and quantified like operators.
// Format of like operator:
//    search_value LIKE pattern_element
// Format of quantified like operator:
//    search_value [NOT] LIKE ANY|ALL (pattern_element1, pattern_element2, ...)
//    search_value [NOT] LIKE ANY|ALL UNNEST([pattern_element1, ...])
struct QuantifiedLikeEvaluationParams {
  // Type of operation - like, like_any, like_all
  enum OperationType { kLike, kLikeAny, kLikeAll };
  // The LHS value of like and quantified like operators.
  const Value& search_value;
  // The RHS pattern elements of like and quantified like operators.
  // pattern_elements should be non-empty.
  const absl::Span<const Value> pattern_elements;
  // The RHS pattern regex of like and quantified like operators.
  // pattern_regex is used for comparison for cases without collation.
  // pattern_regex should be null when collation is specified. If collation_str
  // is empty (collation not specified), then pattern_regex and pattern_elements
  // must contain same number of elements.
  const std::vector<std::unique_ptr<RE2>>* pattern_regex;
  const OperationType operation_type;
  // Indicates whether quantified LIKE is has a preceding NOT operator.
  const bool is_not;
  // The collation string of like and quantified like operators.
  // If collation is not specified, collation_str is empty.
  const std::string collation_str;

  QuantifiedLikeEvaluationParams() = delete;
  QuantifiedLikeEvaluationParams(
      const Value& search_value, absl::Span<const Value> pattern_elements,
      const std::vector<std::unique_ptr<RE2>>* pattern_regex,
      OperationType operation_type, bool is_not)
      : search_value(search_value),
        pattern_elements(pattern_elements),
        pattern_regex(pattern_regex),
        operation_type(operation_type),
        is_not(is_not) {}

  QuantifiedLikeEvaluationParams(const Value& search_value,
                                 absl::Span<const Value> pattern_elements,
                                 OperationType operation_type, bool is_not,
                                 const std::string& collation_str)
      : search_value(search_value),
        pattern_elements(pattern_elements),
        pattern_regex(nullptr),
        operation_type(operation_type),
        is_not(is_not),
        collation_str(collation_str) {}
};

// Evaluates the following like expressions with and without collation:
//    search_value LIKE pattern
//    search_value [NOT] LIKE ANY (pattern1, pattern2, ...)
//    search_value [NOT] LIKE ALL (pattern1, pattern2, ...)
//    search_value [NOT] LIKE ANY UNNEST([pattern1, pattern2, ...])
//    search_value [NOT] LIKE ALL UNNEST([pattern1, pattern2, ...])
absl::StatusOr<Value> EvaluateQuantifiedLike(
    const QuantifiedLikeEvaluationParams& params);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_FUNCTIONS_LIKE_H_
