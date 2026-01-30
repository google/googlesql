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

#ifndef GOOGLESQL_PARSER_PARSE_TREE_ERRORS_H_
#define GOOGLESQL_PARSER_PARSE_TREE_ERRORS_H_

// This header creates common parse-related error status factory functions for
// GoogleSQL. The factory functions return googlesql_base::StatusBuilder so that the error
// message can be constructed using << operators.
//
// We use util::INVALID_ARGUMENT for all SQL parsing and analysis errors.
// We use util::OUT_OF_RANGE for all runtime evaluation errors.
//
// GoogleSQL uses ErrorLocation and InternalErrorLocation as payloads on
// Status.  ErrorLocations are exposed through the public GoogleSQL interfaces,
// while InternalErrorLocations are used internally.  InternalErrorLocations
// are converted to ErrorLocations before returning them through public apis.
//
// ErrorLocations/InternalErrorLocations can contain a list of ErrorSources.
// An ErrorSource identifies a source error which caused the current error.
// The ErrorSource list is ordered, identifying a chain of dependencies
// between errors (i.e., error1 caused error2, which caused error3, etc.).
//
// These helpers are defined as follows:
//
//   // Return an error with code util::INVALID_ARGUMENT, and add an
//   // InternalErrorLocation payload pointing at ast_node's start location.
//   return MakeSqlErrorAt(ast_node) << "Message";
//
//   // Same as previous, but points at <ast_node>'s local location rather than
//   // the start of its leftmost child.  For 'abc=def', this returns a pointer
//   // to '=' rather than to 'a'.
//   return MakeSqlErrorAtLocalNode(ast_node) << "Message";
//
//   // Same as previous two, but decide whether to use the local location
//   // or the leftmost location depending on <use_local_node>.
//   return MakeSqlErrorAtNode(ast_node, use_local_node) << "Message";
//
// See also googlesql/common/errors.h for non parser-dependent error helpers.

#include <string>
#include <string_view>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_generated.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Returns a ParseLocationPoint pointing at an ASTNode's start location.
//
// If `include_leftmost_child`, then this always gets the leftmost position
// for the full expression.
//
// If `include_leftmost_child` is false, then:
// (1) For infix operators, this was intended to get the location for the
//     operator token.  For example, for "abc=def", this should point at '='
//     rather than 'a'.  But this seems to have never worked.
// (2) For chained function calls like `(expression).function(...)`, this
//     points at the function name rather than the start of the expression.
//
// If <use_end_location>, then it will use the end location instead of the start
// location for <ast_node> as well as any child nodes if
// <include_leftmost_child> is true.
ParseLocationPoint GetErrorLocationPoint(const ASTNode* ast_node,
                                         bool include_leftmost_child,
                                         bool use_end_location = false);

// Returns MakeSqlError() annotated with the error location for <ast_node>.
// See GetErrorLocationPoint() for the meaning of <include_leftmost_child>.
inline ::googlesql_base::StatusBuilder MakeSqlErrorAtNode(const ASTNode* ast_node,
                                                bool include_leftmost_child,
                                                bool use_end_location = false) {
  return MakeSqlError().AttachPayload(
      GetErrorLocationPoint(ast_node, include_leftmost_child, use_end_location)
          .ToInternalErrorLocation());
}

// Returns MakeSqlError() annotated with the error location for <ast_node>
// itself, by calling GetErrorLocationPoint without `include_leftmost_child`.
//
// For chained function calls like `(expression).function(...)`, this
// gives an error pointing at the function name rather than the start of the
// expression.
//
// Because of the specializations below, MakeSqlErrorAt on ASTFunctionCall
// will do this automatically.  Calling this function explicitly is necessary
// when the argument type is a generic `ASTNode*` but the inner location
// is still needed when that node is a function call.
inline ::googlesql_base::StatusBuilder MakeSqlErrorAtLocalNode(const ASTNode* ast_node) {
  return MakeSqlErrorAtNode(ast_node, /*include_leftmost_child=*/false);
}

// Returns MakeSqlError() annotated with the error location given by the
// leftmost child of <ast_node>. See GetErrorLocationPoint() for the meaning
// of "leftmost child".
inline ::googlesql_base::StatusBuilder MakeSqlErrorAt(const ASTNode* ast_node) {
  return MakeSqlErrorAtNode(ast_node, /*include_leftmost_child=*/true);
}
inline ::googlesql_base::StatusBuilder MakeSqlErrorAtEnd(const ASTNode* ast_node) {
  return MakeSqlErrorAtNode(ast_node, /*include_leftmost_child=*/true,
                            /*use_end_location=*/true);
}
inline ::googlesql_base::StatusBuilder MakeSqlErrorAt(const ASTNode& ast_node) {
  return MakeSqlErrorAt(&ast_node);
}

// Return an error at `ast_node` if `ast_node` is non-NULL.
// Use like:
//   GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_node)) << "Message";
::absl::Status MakeSqlErrorIfPresent(const ASTNode* ast_node);

// Specialize MakeSqlErrorAt for ASTFunctionCall and related types so that
// those automatically make the error at the LocalNode.
//
// If we are generating errors on an ASTFunctionCall, we assume they must be
// errors about the function call itself, not the whole expression around it.
// For chained function calls like `(input).function(...)`, the error location
// should point at the function name, not the input argument.
inline ::googlesql_base::StatusBuilder MakeSqlErrorAt(const ASTFunctionCall* ast_node) {
  return MakeSqlErrorAtLocalNode(ast_node);
}
inline ::googlesql_base::StatusBuilder MakeSqlErrorAt(
    const ASTAnalyticFunctionCall* ast_node) {
  return MakeSqlErrorAtLocalNode(ast_node);
}

// Returns a team policy that attaches a parse location to an error status
// without changing the error message or code. If the error status already has a
// location, that will be overridden by this one.
//
// Example:
//
//  GOOGLESQL_RETURN_IF_ERROR(DoAThing()).With(LocationOverride(ast_node));
//
inline auto LocationOverride(const ASTNode* node) {
  return [node](googlesql_base::StatusBuilder error) -> googlesql_base::StatusBuilder {
    return error.AttachPayload(
        GetErrorLocationPoint(node, /*include_leftmost_child=*/true)
            .ToInternalErrorLocation());
  };
}

// If <status> is an error, returns a absl::Status with the
// InternalErrorLocation of the ErrorLocationPoint from <ast_node> attached.
// Otherwise, just returns <status>.
//
// Can be used in
//   GOOGLESQL_RETURN_IF_ERROR(StatusWithInternalErrorLocation(SomeFunction(),
//                                                   ast_location));
// to add ErrorLocations into error return paths that don't have locations.
absl::Status StatusWithInternalErrorLocation(
    const absl::Status& status, const ASTNode* ast_node,
    bool include_leftmost_child = true);

// Makes a new Status from <code> and <message> with an external ErrorLocation.
absl::Status MakeStatusWithErrorLocation(absl::StatusCode code,
                                         absl::string_view message,
                                         absl::string_view filename,
                                         absl::string_view query,
                                         const ASTNode* ast_node,
                                         bool include_leftmost_child = true);

// Returns an InternalErrorLocation pointing at ASTNode's start location,
// including the given <filename> (if present).
// See GetErrorLocationPoint() for the meaning of <include_leftmost_child>.
InternalErrorLocation MakeInternalErrorLocation(
    const ASTNode* ast_node, absl::string_view filename = "",
    bool include_leftmost_child = true);

// Creates and returns a new Status based on <ast_location> and
// <error_message>.  The returned Status has an InternalErrorLocation
// payload derived from <ast_location>, and that InternalErrorLocation
// payload contains a new ErrorSource that wraps the <input_status>
// based on <error_source_mode>.
absl::Status WrapNestedErrorStatus(const ASTNode* ast_location,
                                   absl::string_view error_message,
                                   const absl::Status& input_status,
                                   ErrorMessageMode error_source_mode);

// Mutates and returns <status> by adding a single edit fix suggestion.
absl::Status& AddFixSuggestionToStatus(absl::Status& status,
                                       absl::string_view title,
                                       const ParseLocationPoint& start_location,
                                       const ParseLocationPoint& end_location,
                                       absl::string_view new_text);

}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_PARSE_TREE_ERRORS_H_
