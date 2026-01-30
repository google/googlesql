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

#include "googlesql/public/sql_function.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

// TODO: Change this to 'SQL_function', rather than
// 'Lazy_resolution_function'.  This is currently 'Lazy_resolution_function'
// in order to minimize test diffs during a major refactoring.  Once the
// refactoring is submitted, change this to 'SQL_function' and update
// all the tests.
//
// static
const char SQLFunction::kSQLFunctionGroup[] = "Lazy_resolution_function";

SQLFunction::SQLFunction(
    std::vector<std::string> name_path, Mode mode,
    std::vector<FunctionSignature> function_signatures,
    FunctionOptions function_options, const ResolvedExpr* function_expression,
    std::vector<std::string> argument_names,
    std::optional<ParseResumeLocation> parse_resume_location,
    const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        aggregate_expression_list)
    : SQLFunctionInterface(std::move(name_path), kSQLFunctionGroup, mode,
                           std::move(function_signatures),
                           std::move(function_options)),
      function_expression_(std::move(function_expression)),
      argument_names_(std::move(argument_names)),
      parse_resume_location_(parse_resume_location),
      aggregate_expression_list_(aggregate_expression_list) {}

absl::StatusOr<std::unique_ptr<SQLFunction>> SQLFunction::Create(
    std::vector<std::string> name_path, Mode mode,
    FunctionSignature function_signature, FunctionOptions function_options,
    const ResolvedExpr* function_expression,
    std::vector<std::string> argument_names,
    const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        aggregate_expression_list,
    std::optional<ParseResumeLocation> parse_resume_location) {
  GOOGLESQL_RET_CHECK(function_expression != nullptr);
  if (mode == FunctionEnums::AGGREGATE) {
    GOOGLESQL_RET_CHECK(aggregate_expression_list != nullptr);
  }
  std::vector<FunctionSignature> signatures;
  signatures.reserve(1);
  signatures.emplace_back(std::move(function_signature));

  return absl::WrapUnique(
      new SQLFunction(std::move(name_path), mode, std::move(signatures),
                      std::move(function_options), function_expression,
                      std::move(argument_names), parse_resume_location,
                      aggregate_expression_list));
}

absl::Status SQLFunction::Create(
    absl::string_view name, Mode mode,
    std::vector<FunctionSignature> function_signatures,
    FunctionOptions function_options, const ResolvedExpr* function_expression,
    std::vector<std::string> argument_names,
    const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        aggregate_expression_list,
    std::optional<ParseResumeLocation> parse_resume_location,
    std::unique_ptr<SQLFunction>* sql_function) {
  if (parse_resume_location.has_value()) {
    GOOGLESQL_RET_CHECK_EQ(function_signatures.size(), 1);
  }
  GOOGLESQL_RET_CHECK(function_expression != nullptr);
  for (const FunctionSignature& function_signature : function_signatures) {
    GOOGLESQL_RET_CHECK_EQ(argument_names.size(),
                 function_signature.arguments().size())
        << function_signature.DebugString();
  }
  if (mode == FunctionEnums::AGGREGATE) {
    GOOGLESQL_RET_CHECK(aggregate_expression_list != nullptr);
  }
  sql_function->reset(
      new SQLFunction({std::string(name)}, mode, std::move(function_signatures),
                      std::move(function_options), function_expression,
                      std::move(argument_names), parse_resume_location,
                      aggregate_expression_list));
  return absl::OkStatus();
}

std::string SQLFunction::FullDebugString() const {
  std::string full_debug_string = DebugString(/*verbose=*/true);
  // TODO: The current debug string prints the function signature
  // and argument names separately.  It would be better to embed the argument
  // names in the signature strings.
  if (!GetArgumentNames().empty()) {
    absl::StrAppend(&full_debug_string, "\nargument names (",
                    absl::StrJoin(GetArgumentNames(), ", "), ")");
  }
  if (function_expression_ != nullptr) {
    absl::StrAppend(&full_debug_string, "\n",
                    function_expression_->DebugString());
  }
  return full_debug_string;
}

}  // namespace googlesql
