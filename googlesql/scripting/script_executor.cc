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

#include "googlesql/scripting/script_executor.h"

#include <cstdint>
#include <memory>
#include <optional>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/scripting/error_helpers.h"
#include "googlesql/scripting/parsed_script.h"
#include "googlesql/scripting/script_executor_impl.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
ParsedScript::QueryParameters GetQueryParameters(
    const AnalyzerOptions& analyzer_options) {
  switch (analyzer_options.parameter_mode()) {
    case PARAMETER_NAMED: {
      ParsedScript::StringSet parameter_names;
      for (auto itr = analyzer_options.query_parameters().begin();
           itr != analyzer_options.query_parameters().end(); ++itr) {
        parameter_names.insert(itr->first);
      }
      return parameter_names;
    }
    case PARAMETER_POSITIONAL:
      return static_cast<int64_t>(
          analyzer_options.positional_query_parameters().size());
    default:
      return std::nullopt;
  }
}
}  // namespace

void ScriptExecutorOptions::PopulateFromAnalyzerOptions(
    const AnalyzerOptions& analyzer_options) {
  default_time_zone_ = analyzer_options.default_time_zone();
  language_options_ = analyzer_options.language();
  engine_owned_system_variables_ = analyzer_options.system_variables();
  query_parameters_ = GetQueryParameters(analyzer_options);
  error_message_options_ = analyzer_options.error_message_options();
}

absl::StatusOr<std::unique_ptr<ScriptExecutor>> ScriptExecutor::Create(
    absl::string_view script, const ScriptExecutorOptions& options,
    StatementEvaluator* statement_evaluator) {
  return ScriptExecutorImpl::Create(script, /*ast_script=*/nullptr, options,
                                    statement_evaluator);
}

absl::StatusOr<std::unique_ptr<ScriptExecutor>> ScriptExecutor::CreateFromAST(
    absl::string_view script, const ASTScript* ast_script,
    const ScriptExecutorOptions& options,
    StatementEvaluator* statement_evaluator) {
  return ScriptExecutorImpl::Create(script, ast_script, options,
                                    statement_evaluator);
}

absl::Status StatementEvaluator::AssignSystemVariable(
    ScriptExecutor* executor, const ASTSystemVariableAssignment* ast_assignment,
    const Value& value) {
  GOOGLESQL_ASSIGN_OR_RETURN(bool handled, executor->DefaultAssignSystemVariable(
                                     ast_assignment, value));
  if (handled) {
    return absl::OkStatus();
  } else {
    // By default, assume all engine-owned system variables are read-only.
    return AssignmentToReadOnlySystemVariable(ast_assignment);
  }
}

}  // namespace googlesql
