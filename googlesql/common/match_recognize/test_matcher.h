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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_TEST_MATCHER_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_TEST_MATCHER_H_

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/match_test_result.pb.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql::functions::match_recognize {

// Helper class to store names and values of query parameters.
class QueryParameterData {
 public:
  using NamedQueryParams = absl::flat_hash_map<std::string, Value>;
  using PositionalQueryParams = std::vector<Value>;

  // No query parameters by default.
  QueryParameterData() = default;

  static QueryParameterData CreateNamed() {
    return QueryParameterData(NamedQueryParams{});
  }

  static QueryParameterData CreatePositional() {
    return QueryParameterData(PositionalQueryParams{});
  }

  explicit QueryParameterData(const NamedQueryParams& named_params)
      : query_params_(named_params) {}

  explicit QueryParameterData(const PositionalQueryParams& positional_params)
      : query_params_(positional_params) {}

  QueryParameterData AddNamedParam(absl::string_view name, const Value& value) {
    ABSL_CHECK(HasNamedQueryParams());
    ABSL_CHECK(!HasNamedQueryParam(name));
    std::get<NamedQueryParams>(query_params_)[name] = value;
    return *this;
  }

  QueryParameterData AddPositionalParam(const Value& value) {
    ABSL_CHECK(HasPositionalQueryParams());
    std::get<PositionalQueryParams>(query_params_).push_back(value);
    return *this;
  }

  bool HasNamedQueryParams() const {
    return std::holds_alternative<NamedQueryParams>(query_params_);
  }

  bool HasPositionalQueryParams() const {
    return std::holds_alternative<PositionalQueryParams>(query_params_);
  }

  bool HasQueryParams() const {
    return HasNamedQueryParams() || HasPositionalQueryParams();
  }

  const NamedQueryParams& GetNamedQueryParams() const {
    return std::get<NamedQueryParams>(query_params_);
  }

  const Value& GetNamedQueryParam(absl::string_view param_name) const {
    return GetNamedQueryParams().at(param_name);
  }

  bool HasNamedQueryParam(absl::string_view param_name) const {
    return GetNamedQueryParams().contains(param_name);
  }

  const PositionalQueryParams& GetPositionalQueryParams() const {
    return std::get<PositionalQueryParams>(query_params_);
  }

  const Value& GetPositionalQueryParam(int index) const {
    return GetPositionalQueryParams()[index];
  }

 private:
  std::variant<std::monostate,  // No query parameters (default)
               absl::flat_hash_map<std::string, Value>,  // Named parameters
               std::vector<Value>  // Positional parameters
               >
      query_params_;
};

struct TestMatchOptions {
  PatternOptions pattern_options;
  MatchOptions match_options;

  // The match_id field on each match will be included in the test results only
  // if true. False by default to reduce clutter.
  bool show_match_ids = false;

  // Logs generated queries containing MATCH_RECOGNIZE to INFO, for debugging
  // purposes. This should generally be true in unit tests, but false in
  // benchmarks to avoid noise, as Match() is called repeatedly there with the
  // same patterns.
  bool log_generated_queries = true;

  // The values of all query parameters, which can be used as quantifier bounds.
  QueryParameterData query_parameters;

  // True to defer query parameters to runtime, false to make them available at
  // compile-time.
  bool defer_query_parameters = false;

  // Overlapping vs. non-overlapping mode.
  ResolvedMatchRecognizeScanEnums::AfterMatchSkipMode after_match_skip_mode =
      ResolvedMatchRecognizeScanEnums::END_OF_MATCH;

  // SQL expression whose results indicates whether longest match mode is
  // enabled. Must be of type BOOL and either a literal or query parameter.
  std::string longest_match_mode_sql;

  // If true, we will run matching on the result of serializing and
  // deserializing the CompiledPattern, rather than the original
  // CompiledPattern. This ensures that round-tripping the CompiledPattern
  // through serialization does not impact match results.
  bool round_trip_serialization = true;
};

absl::StatusOr<std::unique_ptr<const CompiledPattern>> CreateCompiledPattern(
    absl::string_view pattern, const TestMatchOptions& options = {});

absl::StatusOr<std::unique_ptr<MatchPartition>> CreateMatchPartition(
    absl::string_view pattern, const TestMatchOptions& options = {});

// Tests pattern matching using the public interface against the given pattern,
// options, and RowData. Returns the result of the matching as a proto so it
// can be easily compared with an expected value using proto matchers.
absl::StatusOr<MatchPartitionResultProto> Match(
    absl::string_view pattern, absl::Span<const std::vector<int>> rows,
    const TestMatchOptions& options = {});
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_TEST_MATCHER_H_
