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

#include <memory>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/time/time.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Callback to compute the result type of the TUMBLE and HOP TVF.
absl::StatusOr<std::shared_ptr<TVFSignature>> TimeSeriesComputeResultType(
    Catalog* /*catalog*/, TypeFactory* /*type_factory*/,
    const FunctionSignature& signature,
    const std::vector<TVFInputArgumentType>& actual_arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK(!actual_arguments.empty());
  // When 'timestamp_column' is present, it must be a string literal.
  if ((signature.context_id() == FN_TUMBLE ||
       signature.context_id() == FN_HOP) &&
      !actual_arguments[1].GetScalarArgType()->is_literal_for_constness()) {
    return absl::InvalidArgumentError(
        "Argument 'timestamp_column' must be a string literal");
  }
  const TVFRelation& input_relation = actual_arguments[0].relation();

  // Construct the output schema: all input columns, excluding any existing
  // "window_start" or "window_end", plus new "WINDOW_START", "WINDOW_END".
  TVFRelation::ColumnList output_columns;
  for (const TVFRelation::Column& col : input_relation.columns()) {
    if (googlesql_base::CaseEqual(col.name, "window_start") ||
        googlesql_base::CaseEqual(col.name, "window_end")) {
      continue;
    }
    output_columns.push_back(col);
  }

  output_columns.emplace_back("WINDOW_START", types::TimestampType());
  output_columns.emplace_back("WINDOW_END", types::TimestampType());

  TVFRelation result_schema(output_columns);
  TVFSignatureOptions tvf_signature_options;
  tvf_signature_options.additional_deprecation_warnings =
      signature.AdditionalDeprecationWarnings();

  return TVFSignature::Create(actual_arguments, result_schema,
                              tvf_signature_options);
}

}  // namespace

absl::Status GetTimeSeriesTableValuedFunctions(
    TypeFactory* /*type_factory*/,
    const GoogleSQLBuiltinFunctionOptions& options,
    NameToTableValuedFunctionMap* table_valued_functions) {
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  TableValuedFunctionOptions options_time_series_tvf =
      TableValuedFunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_TUMBLE_HOP_TVFS)
          .set_compute_result_type_callback(&TimeSeriesComputeResultType);

  FunctionSignatureOptions options_time_series_tvf_no_timestamp_col;
  options_time_series_tvf_no_timestamp_col.AddRequiredLanguageFeature(
      FEATURE_TUMBLE_HOP_TVFS_NO_TIMESTAMP_COL);

  std::vector<FunctionSignatureOnHeap> tumble_signatures;
  // Note: 'timestamp_column' is not made an optional argument because
  // GoogleSQL does not allow positional arguments to follow optional arguments.
  tumble_signatures.push_back({
      /*result_type=*/ARG_TYPE_RELATION,
      /*arguments=*/
      {FunctionArgumentType(ARG_TYPE_RELATION,
                            FunctionArgumentTypeOptions().set_argument_name(
                                "table_expr", kPositionalOnly)),
       FunctionArgumentType(types::StringType(),
                            FunctionArgumentTypeOptions().set_argument_name(
                                "timestamp_column", kPositionalOnly)),
       FunctionArgumentType(types::IntervalType(),
                            FunctionArgumentTypeOptions().set_argument_name(
                                "window_size", kPositionalOnly)),
       FunctionArgumentType(
           types::TimestampType(),
           FunctionArgumentTypeOptions(OPTIONAL)
               .set_argument_name("origin", kNamedOnly)
               .set_default(Value::Timestamp(absl::UnixEpoch())))},
      FN_TUMBLE,
  });

  tumble_signatures.push_back({
      /*result_type=*/ARG_TYPE_RELATION,
      /*arguments=*/
      {FunctionArgumentType(ARG_TYPE_RELATION,
                            FunctionArgumentTypeOptions().set_argument_name(
                                "table_expr", kPositionalOnly)),
       FunctionArgumentType(types::IntervalType(),
                            FunctionArgumentTypeOptions().set_argument_name(
                                "window_size", kPositionalOnly)),
       FunctionArgumentType(
           types::TimestampType(),
           FunctionArgumentTypeOptions(OPTIONAL)
               .set_argument_name("origin", kNamedOnly)
               .set_default(Value::Timestamp(absl::UnixEpoch())))},
      FN_TUMBLE_NO_TIMESTAMP_COL,
      options_time_series_tvf_no_timestamp_col,
  });

  GOOGLESQL_RETURN_IF_ERROR(InsertSimpleTableValuedFunction(
      table_valued_functions, options, "tumble", tumble_signatures,
      options_time_series_tvf));

  std::vector<FunctionSignatureOnHeap> hop_signatures;
  // Note: 'timestamp_column' is not made an optional argument because
  // GoogleSQL does not allow positional arguments to follow optional arguments.
  hop_signatures.push_back({
      /*result_type=*/ARG_TYPE_RELATION,
      /*arguments=*/
      {
          FunctionArgumentType(ARG_TYPE_RELATION,
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "table_expr", kPositionalOnly)),
          FunctionArgumentType(types::StringType(),
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "timestamp_column", kPositionalOnly)),
          FunctionArgumentType(types::IntervalType(),
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "window_size", kPositionalOnly)),
          FunctionArgumentType(types::IntervalType(),
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "step_size", kPositionalOnly)),
          FunctionArgumentType(
              types::TimestampType(),
              FunctionArgumentTypeOptions()
                  .set_argument_name("origin", kNamedOnly)
                  .set_cardinality(OPTIONAL)
                  .set_default(Value::Timestamp(absl::UnixEpoch()))),
      },
      FN_HOP,
  });

  hop_signatures.push_back({
      /*result_type=*/ARG_TYPE_RELATION,
      /*arguments=*/
      {
          FunctionArgumentType(ARG_TYPE_RELATION,
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "table_expr", kPositionalOnly)),
          FunctionArgumentType(types::IntervalType(),
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "window_size", kPositionalOnly)),
          FunctionArgumentType(types::IntervalType(),
                               FunctionArgumentTypeOptions().set_argument_name(
                                   "step_size", kPositionalOnly)),
          FunctionArgumentType(
              types::TimestampType(),
              FunctionArgumentTypeOptions()
                  .set_argument_name("origin", kNamedOnly)
                  .set_cardinality(OPTIONAL)
                  .set_default(Value::Timestamp(absl::UnixEpoch()))),
      },
      FN_HOP_NO_TIMESTAMP_COL,
      options_time_series_tvf_no_timestamp_col,
  });

  GOOGLESQL_RETURN_IF_ERROR(
      InsertSimpleTableValuedFunction(table_valued_functions, options, "hop",
                                      hop_signatures, options_time_series_tvf));

  return absl::OkStatus();
}

}  // namespace googlesql
