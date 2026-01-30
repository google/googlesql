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

#include "googlesql/reference_impl/expected_errors.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/matchers.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"

ABSL_FLAG(bool,
          googlesql_add_expected_error_for_invalid_query_with_pivot_clause,
          false, "Add expected error for invalid query with PIVOT clause.");

ABSL_FLAG(
    bool,
    googlesql_add_expected_error_for_MLAs_in_order_by_and_limit_agg_rewriter,
    false,
    "Add expected error for multi-level aggregates in order by and limit in "
    "aggregate rewriter.");

ABSL_FLAG(
    bool, add_error_for_agg_filtering_in_order_by_and_limit_agg_rewriter, false,
    "Add expected error for multi-level aggregates in order by and limit in "
    "aggregate rewriter.");

namespace googlesql {

std::unique_ptr<MatcherCollection<absl::Status>> ReferenceExpectedErrorMatcher(
    std::string matcher_name) {
  std::vector<std::unique_ptr<MatcherBase<absl::Status>>> error_matchers;
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported built-in function: (st_accum|st_askml|st_geogfromkml"
      "|st_unaryunion)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kResourceExhausted,
      "The statement has been aborted because the statement deadline (.+) was "
      "exceeded\\."));
  // TABLESAMPLE is not supported by the reference implementation.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument, "TABLESAMPLE not supported"));
  // The reference implementation does not support KMS and AEAD envelope
  // encryption functions since they depend on an external service.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported built-in function: kms.*"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported built-in function: aead\\.envelope.*"));
  // The reference implementation does not support KEYS.KEYSET_CHAIN function.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported built-in function: keys\\.keyset_chain"));
  // b/111212209
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Checking the presence of scalar field .* is not supported for proto3"));
  // The RQG can produce assignments to repeated proto values that contain
  // NULL.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot store a NULL element in repeated proto field"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported built-in function: \\$(?:safe_)?proto_map_at_key"));
  // b/160778032
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInternal,
      "functions::IsValidDate(decoded_date_value) Invalid date"));
  // b/173659202
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot execute a nested (DELETE|INSERT|UPDATE) statement on a "
      "NULL array value"));
  // b/182819630
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Attempted to modify an array element with multiple nested UPDATE "
      "statements"));

  // Moved over from the PrepareQuery expected errors as these occur in the
  // normal random query tests as well.
  // TODO: Figure out if it's possible to avoid generating invalid
  // PIVOT queries in RQG.
  error_matchers.emplace_back(
      new StatusRegexMatcher(absl::StatusCode::kUnimplemented,
                             "as (?:a )?PIVOT expression is not supported"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "SQL-defined aggregate functions are not supported in PIVOT"));

  // TODO: RQG should not generate proto expressions for protos in
  // googlesql.functions.* as they are often "special" (e.g. only allowed as
  // arguments to particular functions during analysis). However, at the moment
  // we do generate these things.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Type not found: `googlesql.functions."));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Type not found: UNSUPPORTED_FIELDS"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      ".*(Unrecognized name|Function not found): "
      "(YEAR|MONTH|DAY|DAYOFWEEK|DAYOFYEAR|QUARTER|HOUR|MINUTE|SECOND|"
      "MILLISECOND|MICROSECOND|NANOSECOND|DATE|WEEK|DATETIME|TIME"
      "ISOWEEK|ISOYEAR|NFC|NFKC|NFD|NFKD|FIRST|LAST).*"));

  // TODO: Certain expressions have a built-in proto type that
  // isn't necessarily in the catalog. Right now, the STRUCT builder includes
  // that built-in type from google.protobuf. in the STRUCT declaration, e.x.
  // STRUCT< Field_1 `google.protobuf.Int64Value`, ... > (TO_PROTO(...),...) The
  // standard catalog setups do not include these types by default, so this
  // leads to a "Type not found: 'google.protobuf.Int64Value'" error. For now,
  // we allowlist this case. We should either figure out how to support this or
  // avoid generating it.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument, "Type not found: `google.protobuf."));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument, "Type not found: `google.type."));

  // D3A_* calls can be generated in a way that returns an invalid argument
  // from the evaluator/executor.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      ".*Unexpected aggregator type:.*Expected D3A.*"));

  // RQG can generate various casts / proto accesses that will create floating
  // point errors.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Floating point error in function.*"));

  // Aggregate values can overflow in large queries. However, this might not be
  // the behavior other engines would expect.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kResourceExhausted,
      "Aggregate values are limited to "));

  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kResourceExhausted, "Arrays are limited to "));

  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot construct array Value larger than "));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot construct struct Value larger than "));

  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kResourceExhausted,
      "Out of memory for MemoryAccountant"));

  // Expected errors for JSON_OBJECT.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid input to JSON_OBJECT: The (keys|values) array cannot be NULL"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid input to JSON_OBJECT: A key cannot be NULL"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid input to JSON_OBJECT: The number of keys and values must "
      "match"));

  // We intentionally allow RQG to generate a small number of queries with
  // failed assertions to test the ASSERT logic.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Assert failed:"));

  // WITH on subquery does not work with DML statement yet.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "WITH clauses under subquery in DML statement is not supported yet."));

  if (absl::GetFlag(
          FLAGS_googlesql_add_expected_error_for_invalid_query_with_pivot_clause)) {
    // TODO: Reference implementation can not parse the query with
    // PIVOT clause.
    error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
        absl::StatusCode::kInvalidArgument, "Unrecognized name"));
  }

  if (absl::GetFlag(
          FLAGS_googlesql_add_expected_error_for_MLAs_in_order_by_and_limit_agg_rewriter)) {
    error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
        absl::StatusCode::kUnimplemented,
        "Aggregate functions with GROUP BY modifiers are not supported in "
        "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter"));
  }

  if (absl::GetFlag(
          FLAGS_add_error_for_agg_filtering_in_order_by_and_limit_agg_rewriter)) {
    error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
        absl::StatusCode::kUnimplemented,
        "Aggregate functions with a WHERE filter modifier are not supported in "
        "ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter"));
    error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
        absl::StatusCode::kUnimplemented,
        "Aggregate functions with a HAVING filter modifier are not supported "
        "in ORDER_BY_AND_LIMIT_IN_AGGREGATE rewriter"));
  }

  // TODO: Reference implementation does not support TOKENLIST
  // for TO_JSON_STRING.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported argument type TOKENLIST for TO_JSON_STRING"));

  // TODO: Reference implementation does not support TOKENLIST
  // for TO_JSON.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported argument type TOKENLIST for TO_JSON"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Unsupported argument type TOKENLIST for TO_JSON"));

  // TODO: b/322857409 - Reference implementation does not support MAP for
  // TO_JSON functions.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported argument type MAP.* for TO_JSON"));
  // JSON_OBJECT and JSON_ARRAY generate kOutOfRange. These are harder to
  // address.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "(JSON_OBJECT|JSON_ARRAY).*Unsupported argument type MAP.* for TO_JSON"));

  return std::make_unique<MatcherCollection<absl::Status>>(
      matcher_name, std::move(error_matchers));
}

}  // namespace googlesql
