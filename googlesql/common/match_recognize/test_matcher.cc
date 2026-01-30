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

#include "googlesql/common/match_recognize/test_matcher.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/match_test_result.pb.h"
#include "googlesql/common/match_recognize/test_pattern_resolver.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.pb.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {
using QueryParametersValueMap = absl::flat_hash_map<std::string, Value>;

static std::vector<bool> GetPatternVarBooleans(
    std::vector<int> pattern_vars_for_row, size_t num_pattern_vars) {
  std::vector<bool> result;
  result.resize(num_pattern_vars);
  for (int id : pattern_vars_for_row) {
    ABSL_CHECK_GE(id, 0);
    ABSL_CHECK_LT(id, num_pattern_vars);
    result[id] = true;
  }
  return result;
}

std::string GetMatchTestOutput(const struct Match& match,
                               const ResolvedMatchRecognizeScan& scan,
                               const TestMatchOptions& options) {
  std::string result;
  int match_length = static_cast<int>(match.pattern_vars_by_row.size());
  absl::StrAppend(&result, "(position ", match.start_row_index);
  if (options.show_match_ids) {
    absl::StrAppend(&result, ", id ", match.match_id);
  }
  if (match_length == 0) {
    absl::StrAppend(&result, ", empty");
  } else {
    absl::StrAppend(&result, ", length ", match_length);
  }
  absl::StrAppend(&result, ")");
  if (match_length != 0) {
    absl::StrAppend(
        &result, ": ",
        absl::StrJoin(
            match.pattern_vars_by_row, ", ",
            [&scan](std::string* out, int pattern_var_id) {
              absl::StrAppend(
                  out, scan.pattern_variable_definition_list(pattern_var_id)
                           ->name());
            }));
  }
  return result;
}

static MatchResultProto ToProto(const MatchResult& match_result,
                                const ResolvedMatchRecognizeScan& scan,
                                const TestMatchOptions& options) {
  MatchResultProto match_result_proto;
  match_result_proto.mutable_match()->Reserve(match_result.new_matches.size());
  for (const struct Match& new_match : match_result.new_matches) {
    match_result_proto.add_match(GetMatchTestOutput(new_match, scan, options));
  }
  return match_result_proto;
}

static MatchRecognizeScanGenerationOptions
GetMatchRecognizeScanGenerationOptions(const TestMatchOptions& test_options) {
  MatchRecognizeScanGenerationOptions scan_generation_options;
  if (test_options.query_parameters.HasNamedQueryParams()) {
    QueryParametersMap map;
    for (const auto& [name, value] :
         test_options.query_parameters.GetNamedQueryParams()) {
      map[name] = value.type();
    }
    scan_generation_options.parameters = map;
  } else if (test_options.query_parameters.HasPositionalQueryParams()) {
    std::vector<const Type*> positional_param_types;
    positional_param_types.reserve(
        test_options.query_parameters.GetPositionalQueryParams().size());
    for (const Value& positional_param :
         test_options.query_parameters.GetPositionalQueryParams()) {
      positional_param_types.push_back(positional_param.type());
    }
    scan_generation_options.parameters = positional_param_types;
  }

  scan_generation_options.after_match_skip_mode =
      test_options.after_match_skip_mode;
  scan_generation_options.longest_match_mode_sql =
      test_options.longest_match_mode_sql;
  return scan_generation_options;
}

static absl::StatusOr<Value> EvaluateQueryParameter(
    const TestMatchOptions& options,
    std::variant<int, absl::string_view> param) {
  if (!options.query_parameters.HasQueryParams()) {
    return absl::OutOfRangeError("Query parameters not provided");
  }
  if (options.query_parameters.HasNamedQueryParams()) {
    const QueryParameterData::NamedQueryParams& map =
        options.query_parameters.GetNamedQueryParams();
    absl::string_view param_name = std::get<absl::string_view>(param);
    auto it = map.find(param_name);
    if (it != map.end()) {
      return it->second;
    }
    return absl::OutOfRangeError(
        absl::StrCat("Query parameter `", param_name, "` not found"));
  }
  int param_position = std::get<int>(param);
  const std::vector<Value>& positional_params =
      options.query_parameters.GetPositionalQueryParams();
  if (param_position <= positional_params.size()) {
    // Note: Query parameter indexes in the resolved tree are one-based.
    return positional_params[param_position - 1];
  }
  return absl::OutOfRangeError(absl::StrCat("Query parameter ", param_position,
                                            " has out-of-bounds position"));
}

static PatternOptions GetPatternOptions(const TestMatchOptions& options) {
  PatternOptions pattern_options = options.pattern_options;
  if (!options.defer_query_parameters) {
    pattern_options.parameter_evaluator =
        [options](std::variant<int, absl::string_view> param) {
          return EvaluateQueryParameter(options, param);
        };
  }
  return pattern_options;
}

absl::StatusOr<std::unique_ptr<const CompiledPattern>> CreateCompiledPattern(
    absl::string_view pattern, const TestMatchOptions& options) {
  TestPatternResolver resolver(options.log_generated_queries);
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ResolvedMatchRecognizeScan* scan,
      resolver.ResolvePattern(pattern,
                              GetMatchRecognizeScanGenerationOptions(options)));
  return CompiledPattern::Create(*scan, GetPatternOptions(options));
}

absl::StatusOr<std::unique_ptr<MatchPartition>> CreateMatchPartition(
    absl::string_view pattern, const TestMatchOptions& options) {
  TestPatternResolver resolver(options.log_generated_queries);
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ResolvedMatchRecognizeScan* scan,
      resolver.ResolvePattern(pattern,
                              GetMatchRecognizeScanGenerationOptions(options)));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const CompiledPattern> compiled_pattern,
                   CompiledPattern::Create(*scan, GetPatternOptions(options)));
  return compiled_pattern->CreateMatchPartition(options.match_options);
}

absl::StatusOr<MatchPartitionResultProto> Match(
    absl::string_view pattern, absl::Span<const std::vector<int>> rows,
    const TestMatchOptions& options) {
  TestPatternResolver resolver(options.log_generated_queries);
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ResolvedMatchRecognizeScan* scan,
      resolver.ResolvePattern(pattern,
                              GetMatchRecognizeScanGenerationOptions(options)));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const CompiledPattern> compiled_pattern,
                   CompiledPattern::Create(*scan, GetPatternOptions(options)));
  if (options.round_trip_serialization) {
    CompiledPatternProto serialized_pattern = compiled_pattern->Serialize();
    GOOGLESQL_ASSIGN_OR_RETURN(compiled_pattern,
                     CompiledPattern::Deserialize(serialized_pattern));
  }

  MatchOptions match_options = options.match_options;
  if (options.defer_query_parameters) {
    match_options.parameter_evaluator =
        [options](std::variant<int, absl::string_view> param) {
          return EvaluateQueryParameter(options, param);
        };
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<MatchPartition> match_partition,
                   compiled_pattern->CreateMatchPartition(match_options));
  MatchPartitionResultProto partition_result;

  std::optional<MatchResult> last_match_result;
  for (const std::vector<int>& row : rows) {
    std::vector<bool> arg = GetPatternVarBooleans(
        row, scan->pattern_variable_definition_list_size());
    GOOGLESQL_ASSIGN_OR_RETURN(MatchResult match_result, match_partition->AddRow(arg));

    // Collapse consecutive rows with no results into one proto entry.
    if (last_match_result.has_value() &&
        last_match_result->new_matches.empty() &&
        match_result.new_matches.empty()) {
      MatchResultProto& last_match_result_proto =
          *partition_result.mutable_add_row(partition_result.add_row_size() -
                                            1);
      if (last_match_result_proto.has_rep_count()) {
        last_match_result_proto.set_rep_count(
            last_match_result_proto.rep_count() + 1);
      } else {
        last_match_result_proto.set_rep_count(2);
      }
      continue;
    }
    last_match_result = match_result;
    *partition_result.add_add_row() = ToProto(match_result, *scan, options);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(MatchResult match_result, match_partition->Finalize());
  *partition_result.mutable_finalize() = ToProto(match_result, *scan, options);

  return partition_result;
}
}  // namespace googlesql::functions::match_recognize
