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

#ifndef GOOGLESQL_PUBLIC_NON_SQL_FUNCTION_H_
#define GOOGLESQL_PUBLIC_NON_SQL_FUNCTION_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"

namespace googlesql {

class ResolvedComputedColumn;
class ResolvedExpr;

// Represents an instance of a non-SQL function which stores the resolved
// function expression.
class NonSqlFunction : public Function {
 public:
  // The Function group name for SQLFunctions.
  static const char kNonSqlFunctionGroup[];

  // Creates a NonSqlFunction from <resolved_create_function_statement>.
  // Returns an error if the NonSqlFunction could not be successfully
  // created.
  //
  // Returns an error if:
  //   1) <mode> is AGGREGATE and <aggregate_expression_list> is NULL
  //   2) the size of <argument_names> does not match the number of arguments
  //      in all of the <function_signatures>
  //
  // Does not take ownership of <aggregate_expression_list> or
  // <resolved_create_function_statement>, both of which must outlive
  // this class.
  //
  // Optional field <parse_resume_location> identifies the start of the
  // CREATE statement associated with this NonSqlFunction, if applicable.  Must
  // only be set if there is a single FunctionSignature.
  static absl::Status Create(
      absl::string_view name, Mode mode,
      const std::vector<FunctionSignature>& function_signatures,
      const FunctionOptions& function_options, ModuleDetails module_details,
      const ResolvedCreateFunctionStmt* resolved_create_function_statement,
      const std::vector<std::string>& argument_names,
      const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          aggregate_expression_list,
      std::optional<ParseResumeLocation> parse_resume_location,
      std::unique_ptr<NonSqlFunction>* sql_function);

  const ResolvedCreateFunctionStmt& resolved_create_function_statement() const {
    return *resolved_create_function_statement_;
  }

  const ModuleDetails& module_details() const { return module_details_; }

  // Returns a debug string that includes the <function_expression_> string,
  // if present.
  std::string FullDebugString() const;

 private:
  // Constructor for valid functions.
  NonSqlFunction(
      absl::string_view name, Mode mode,
      const std::vector<FunctionSignature>& function_signatures,
      const FunctionOptions& function_options, ModuleDetails module_details,
      const ResolvedCreateFunctionStmt* resolved_create_function_statement,
      const std::vector<std::string>& argument_names,
      std::optional<ParseResumeLocation> parse_resume_location,
      const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          aggregate_expression_list);

  const ModuleDetails module_details_;
  const ResolvedCreateFunctionStmt* resolved_create_function_statement_;
  const std::vector<std::string> argument_names_;
  std::optional<ParseResumeLocation> parse_resume_location_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_NON_SQL_FUNCTION_H_
