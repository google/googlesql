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

#ifndef GOOGLESQL_SCRIPTING_ERROR_HELPERS_H_
#define GOOGLESQL_SCRIPTING_ERROR_HELPERS_H_

#include <functional>

#include "googlesql/common/errors.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/proto/script_exception.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/scripting/script_segment.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Returns a StatusBuilder adapter, which converts an ErrorLocation relative
// to a particular script segment, to one relative to the entire script.
//
// For example, in the script "SELECT 3; SELECT garbage;", this function
// would convert an ErrorLocation local to that particular statement (line 1,
// column 8) to an equivelant ErrorLocation relative to the entire script (line
// 1, column 18).
//
// Example usage:
//  GOOGLESQL_RETURN_IF_ERROR(DoSomethingWithScriptSegment).With(
//    AdjustErrorLocation(segment));
//
// If the status's ErrorLocation payload refers to a line/column number outside
// the bounds of the script, the returned status is a generic::internal
// with no ErrorLocation payload.
std::function<::googlesql_base::StatusBuilder(::googlesql_base::StatusBuilder)>
ConvertLocalErrorToScriptError(const ScriptSegment& segment);

// Similar to the above function, but consumes and returns an ErrorLocation,
// rather than a status.  Returns a generic::internal status if
// <error_location_in> is outside the bounds of the script.
absl::StatusOr<ErrorLocation> ConvertLocalErrorToScriptError(
    const ScriptSegment& segment, const ErrorLocation& error_location_in);

// Helper functions for evaluator and native procedure implementations to mark
// errors as handleable.
//
// TODO: Add optional parameters to these functions, allowing the
// engine to specify the value of engine-defined system variables, once system
// variable support is implemented.

// Creates a handleable script error tied to the location of <node>.
inline googlesql_base::StatusBuilder MakeScriptExceptionAt(const ASTNode* node) {
  return MakeSqlErrorAt(node).AttachPayload(ScriptException());
}

// Creates a handleable script error not tied to any particular location.
inline googlesql_base::StatusBuilder MakeScriptException() {
  return MakeEvalError().AttachPayload(ScriptException());
}

inline googlesql_base::StatusBuilder MakeScriptException(const ScriptException& ex) {
  return MakeEvalError().AttachPayload(ex);
}

// Returns an error status representing an assignment to a read-only system
// variable, using <location> to generate the line and column number of the
// error message.
inline googlesql_base::StatusBuilder AssignmentToReadOnlySystemVariable(
    const ASTSystemVariableAssignment* assignment) {
  return MakeScriptExceptionAt(assignment->system_variable())
         << "Assignment to read-only system variable @@"
         << assignment->system_variable()->path()->ToIdentifierPathString();
}

// Returns a handleable error indicating that a script variable does not exist.
// <ast_var> denotes both the variable name, which will appear in the error
// message and the location.
inline absl::Status MakeUndeclaredVariableError(const ASTIdentifier* ast_var) {
  return MakeScriptExceptionAt(ast_var)
         << "Undeclared variable: " << ast_var->GetAsString();
}

inline absl::Status MakeUnknownSystemVariableError(
    const ASTSystemVariableExpr* expr) {
  return MakeScriptExceptionAt(expr) << "System variable not found: @@"
                                     << expr->path()->ToIdentifierPathString();
}

// Returns OK if <call_statement> contains <num_expected_arguments> arguments.
// Otherwise, returns an error status indicating that the argument count is
// incorrect.
inline absl::Status CheckProcedureArgumentCount(
    const ASTCallStatement* call_statement, int num_expected_arguments) {
  if (call_statement->arguments().size() != num_expected_arguments) {
    return MakeScriptExceptionAt(call_statement->procedure_name())
           << absl::Substitute(
                  "Procedure $0 expects $1 argument(s), $2 provided",
                  call_statement->procedure_name()->ToIdentifierPathString(),
                  num_expected_arguments, call_statement->arguments().size());
  }
  return absl::OkStatus();
}

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_ERROR_HELPERS_H_
