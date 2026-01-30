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

#include "googlesql/reference_impl/functions/like.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/collator.h"
#include "googlesql/public/functions/like.h"
#include "googlesql/public/functions/string_with_collation.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<Value> LikeImpl(const Value& lhs, const Value& rhs,
                               const RE2* regexp) {
  if (lhs.is_null() || rhs.is_null()) {
    return Value::Null(types::BoolType());
  }

  const std::string& text =
      lhs.type_kind() == TYPE_STRING ? lhs.string_value() : lhs.bytes_value();

  if (regexp != nullptr) {
    // Regexp is precompiled
    return Value::Bool(RE2::FullMatch(text, *regexp));
  } else {
    // Regexp is not precompiled, compile it on the fly
    const std::string& pattern =
        rhs.type_kind() == TYPE_STRING ? rhs.string_value() : rhs.bytes_value();
    std::unique_ptr<RE2> regexp;
    GOOGLESQL_RETURN_IF_ERROR(
        functions::CreateLikeRegexp(pattern, lhs.type_kind(), &regexp));
    return Value::Bool(RE2::FullMatch(text, *regexp));
  }
}

bool IsTrue(const Value& value) {
  return !value.is_null() && value.bool_value();
}

bool IsFalse(const Value& value) {
  return !value.is_null() && !value.bool_value();
}

// Performs a logical AND of elements in `values`
// Returns NULL for a NULL element in `values` if all other non-null
// elements are false.
// Returns false if there is at least one false in element in `values`.
// Returns true for all other cases.
Value LogicalAnd(absl::Span<const Value> values) {
  bool found_null = false;
  for (const auto& value : values) {
    if (value.is_null()) {
      found_null = true;
    } else if (IsFalse(value)) {
      return Value::Bool(false);
    }
  }
  return found_null ? Value::NullBool() : Value::Bool(true);
}

// Performs a logical OR of elements in `values`
// Returns NULL for a NULL element in `values` if all other non-null
// elements are false.
// Returns true if there is at least one true in element in `values`.
// Returns false for all other cases.
Value LogicalOr(absl::Span<const Value> values) {
  bool found_null = false;
  for (const auto& value : values) {
    if (value.is_null()) {
      found_null = true;
    } else if (IsTrue(value)) {
      return Value::Bool(true);
    }
  }
  return found_null ? Value::NullBool() : Value::Bool(false);
}

Value LogicalNot(const Value& value) {
  if (value.is_null()) {
    return value;
  }
  return IsTrue(value) ? Value::Bool(false) : Value::Bool(true);
}

absl::Status ValidateQuantifiedLikeEvaluationParams(
    const QuantifiedLikeEvaluationParams& params) {
  if (params.collation_str.empty()) {
    GOOGLESQL_RET_CHECK(params.pattern_regex != nullptr) << "Pattern regex is null";
    // For cases with pattern is a subquery expression creating an ARRAY, the
    // number of regexps will be less than the number of elements and the regexp
    GOOGLESQL_RET_CHECK_LE(params.pattern_regex->size(), params.pattern_elements.size())
        << "Number of regexps is greater than the number of elements";
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> BuildCollator(
    absl::string_view collation_str) {
  if (collation_str.empty()) {
    return nullptr;
  }
  return googlesql::MakeSqlCollator(collation_str);
}

absl::StatusOr<Value> EvaluateQuantifiedLike(
    const QuantifiedLikeEvaluationParams& params) {
  GOOGLESQL_RET_CHECK_OK(ValidateQuantifiedLikeEvaluationParams(params));
  if (params.search_value.is_null()) {
    return Value::NullBool();
  }

  if (params.pattern_elements.empty()) {
    return Value::Bool(false);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const GoogleSqlCollator> collator,
                   BuildCollator(params.collation_str));
  if (!params.collation_str.empty()) {
    GOOGLESQL_RET_CHECK(collator != nullptr);
  }

  Value local_result;
  std::vector<Value> result_values;
  result_values.reserve(params.pattern_elements.size());

  for (int i = 0; i < params.pattern_elements.size(); ++i) {
    auto& pattern_element = params.pattern_elements.at(i);
    if (pattern_element.is_null()) {
      result_values.push_back(Value::NullBool());
      continue;
    }

    if (collator != nullptr) {
      // If collator is present, invoke like with collation
      GOOGLESQL_ASSIGN_OR_RETURN(bool result,
                       functions::LikeUtf8WithCollation(
                           params.search_value.string_value(),
                           pattern_element.string_value(), *collator));
      local_result = Value::Bool(result);
    } else {
      // If collator is absent, invoke like without collation
      const RE2* current_regexp = i < params.pattern_regex->size()
                                      ? params.pattern_regex->at(i).get()
                                      : nullptr;
      GOOGLESQL_ASSIGN_OR_RETURN(local_result, LikeImpl(params.search_value,
                                              pattern_element, current_regexp));
    }

    // If NOT LIKE ANY/ALL, flip the result.
    if (params.is_not) {
      local_result = LogicalNot(local_result);
    }
    result_values.push_back(local_result);
  }

  switch (params.operation_type) {
    case QuantifiedLikeEvaluationParams::kLike:
      GOOGLESQL_RET_CHECK_EQ(result_values.size(), 1);
      return std::move(result_values[0]);
    case QuantifiedLikeEvaluationParams::kLikeAny:
      return LogicalOr(result_values);
    case QuantifiedLikeEvaluationParams::kLikeAll:
      return LogicalAnd(result_values);
    default:
      return absl::OutOfRangeError("Unknown operation type");
  }
}

}  // namespace googlesql
