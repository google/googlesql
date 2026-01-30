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

#ifndef GOOGLESQL_SCRIPTING_PARSED_SCRIPT_H_
#define GOOGLESQL_SCRIPTING_PARSED_SCRIPT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/type.h"
#include "googlesql/scripting/control_flow_graph.h"
#include "googlesql/scripting/type_aliases.h"
#include "googlesql/base/case.h"
#include "absl/base/macros.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

// Flag which controls the maximum supported nesting of script statements within
// a GoogleSQL script.
ABSL_DECLARE_FLAG(int, googlesql_scripting_max_nesting_level);

namespace googlesql {

struct ParsedScriptOptions {
  // Script variables declared before the script starts. For example, when a
  // script runs as part of a session, the script will inherit the session
  // variables.
  VariableWithTypeParameterMap predefined_variables = {};
  // System variables that are allowed to be assigned before DECLARE statements.
  std::vector<std::string> system_variables_allowed_before_declare = {};
};

struct PositionalParameterRange {
  // Start index for the positional query parameters for a given segment.
  int64_t start_param_index;

  // Number of the positional query parameters for a given segment.
  int64_t num_params;
};

class ParsedScript {
 public:
  // Maps an ASTNode pointer to the child index of that node, relative
  // to its parent.  For each statement s, s->parent()->child(map[s]) == s.
  using NodeIndexMap = absl::flat_hash_map<const ASTNode*, int>;

  // Mapping of active variable names to the ASTScriptStatement which creates
  // the variable.
  // For now, the two ASTScriptStatement's that create variables are:
  // - ASTVariableDeclaration
  // - ASTForInStatement
  using VariableCreationMap =
      absl::flat_hash_map<IdString, const ASTScriptStatement*, IdStringCaseHash,
                          IdStringCaseEqualFunc>;

  // Mapping of argument name to googlesql Type.
  using ArgumentTypeMap =
      absl::flat_hash_map<IdString, const Type*, IdStringCaseHash,
                          IdStringCaseEqualFunc>;

  // Mapping of locations to query parameters.
  using NamedQueryParameterMap = std::map<ParseLocationPoint, IdString>;

  // Case-insensitive set of strings. Strings are not owned by the set.
  using StringSet =
      absl::btree_set<absl::string_view, googlesql_base::CaseLess>;

  // Either a map of named parameters or the number of positional parameters.
  using QueryParameters = std::optional<std::variant<StringSet, int64_t>>;

  // Performs preliminary analysis on the parse tree for a googlesql script
  // before execution.  Currently, this includes the following:
  // - Verify that variable declarations occur only at the start of a block,
  //    or the start of the entire script.
  // - Verify that a variable is not declared which shadows a routine argument
  //    or another variable of the same name in either the same scope, or any
  //    enclosing scope.
  // - Verify that BREAK and CONTINUE statements occur only inside of a loop.
  // - Obtain a mapping associating each statement to the index of that
  //    statement in the statement list of its parent node.
  // - Obtain a mapping associating each BREAK and CONTINUE statement with a
  //    pointer to the enclosing loop, plus a list of blocks whose variables
  //    must go out of scope when the corresponding BREAK/CONTINUE statement
  //    executes.
  //
  // All results are stored in the returned ParsedScript object.
  static absl::StatusOr<std::unique_ptr<ParsedScript>> Create(
      absl::string_view script_string, const ParserOptions& parser_options,
      ErrorMessageOptions error_message_options,
      const ParsedScriptOptions& options = {});

  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<std::unique_ptr<ParsedScript>> Create(
      absl::string_view script_string, const ParserOptions& parser_options,
      ErrorMessageMode error_message_mode,
      const ParsedScriptOptions& options = {}) {
    return Create(script_string, parser_options,
                  ErrorMessageOptions{
                      .mode = error_message_mode,
                      .attach_error_location_payload =
                          (error_message_mode == ERROR_MESSAGE_WITH_PAYLOAD),
                      .stability = GetDefaultErrorMessageStability()},
                  options);
  }

  // Similar to the above function, but uses an existing, externally-owned
  // AST instead of parsing the script.  <ast_script> must be kept alive for
  // the lifetime of the returned ParsedScript.
  static absl::StatusOr<std::unique_ptr<ParsedScript>> Create(
      absl::string_view script_string, const ASTScript* ast_script,
      ErrorMessageOptions error_message_options,
      const ParsedScriptOptions& options = {});

  // Similar to above function, but also passes arguments for a routine, i.e. a
  // function or stored procedure whose body is a script.
  static absl::StatusOr<std::unique_ptr<ParsedScript>> CreateForRoutine(
      absl::string_view script_string, const ParserOptions& parser_options,
      ErrorMessageOptions error_message_options,
      ArgumentTypeMap routine_arguments,
      const ParsedScriptOptions& options = {});

  // Similar to the above functions, but allows the caller to provide an AST
  // node when the script is contained with a larger script, for example,
  // a CREATE PROCEDURE statement.
  static absl::StatusOr<std::unique_ptr<ParsedScript>> CreateForRoutine(
      absl::string_view script_string, const ASTScript* ast_script,
      ErrorMessageOptions error_message_options,
      ArgumentTypeMap routine_arguments,
      const ParsedScriptOptions& options = {});

  const ASTScript* script() const { return ast_script_; }
  absl::string_view script_text() const { return script_string_; }
  ErrorMessageOptions error_message_options() const {
    return error_message_options_;
  }
  const ArgumentTypeMap& routine_arguments() const {
    return routine_arguments_;
  }

  const ControlFlowGraph& control_flow_graph() const {
    return *control_flow_graph_;
  }

  StringSet GetNamedParameters(const ParseLocationRange& range) const;

  // Returns the start index and length for the number of positional parameters
  // in the given range.
  //
  // If no positional parameters are within the range, returns 0 for the length.
  PositionalParameterRange GetPositionalParameters(
      const ParseLocationRange& range) const;

  // Returns the node in the script which starts at the given position,
  // or nullptr if no such node exists.
  // Note: since this function finds non-statement nodes as well, caller
  // should ensure that there is only one ASTNode starting at <start_pos>.
  absl::StatusOr<const ASTNode*> FindScriptNodeFromPosition(
      const ParseLocationPoint& start_pos) const;

  // Returns a map of all variables in scope immediately prior to the execution
  // of <node>.
  absl::StatusOr<VariableCreationMap> GetVariablesInScopeAtNode(
      const ControlFlowNode* node) const;

  // Validates the query parameters (e.g. no missing ones, not mixing named and
  // positional parameters).
  // If `is_strict_mode` is false, then OkStatus is returned if `parameters` is
  // empty.
  absl::Status CheckQueryParameters(const QueryParameters& parameters,
                                    bool is_strict_mode = false) const;

  bool IsProcedure() const { return is_procedure_; }

 private:
  static absl::StatusOr<std::unique_ptr<ParsedScript>> CreateInternal(
      absl::string_view script_string, const ParserOptions& parser_options,
      ErrorMessageOptions error_message_options,
      ArgumentTypeMap routine_arguments, bool is_procedure,
      const ParsedScriptOptions& options);

  // script_string: The string of the entire script for which parse locations
  //   within <ast_script> are based off of. Owned externally.
  //
  // ast_script: The parse tree for the current script or procedure body.
  //   Owned by <parser_output> if <parser_output> is non-null; otherwise,
  //   owned externally.
  //
  // parser_output: If non-null, owns the lifetime of <ast_script>.
  //
  // error_message_mode: Indicates how errors should be returned when validating
  //   the script or procedure body.
  //
  // routine_arguments: If <ast_script> is a procedure body, specifies the
  //   name and type of each argument. This is used in validation, for example,
  //   to ensure that script variables do not shadow arguments. When
  //   <ast_script> represents a top-level script, this should be empty.
  ParsedScript(absl::string_view script_string, const ASTScript* ast_script,
               std::unique_ptr<ParserOutput> parser_output,
               ErrorMessageOptions error_message_options,
               ArgumentTypeMap routine_arguments, bool is_procedure);

  // Called from Create() to walk the parse tree and perform non-trivial work
  // to initialize fields.
  absl::Status GatherInformationAndRunChecks(
      const ParsedScriptOptions& options);

  // Helper function called by GatherInformationAndRunChecks().  Returns errors
  // with an InternalErrorLocation, which the caller then converts to an
  // external ErrorLocation.
  absl::Status GatherInformationAndRunChecksInternal(
      const ParsedScriptOptions& options);

  // Populates <named_query_parameters_> and <positional_query_parameters_> with
  // parameter locations. Returns an error if both positional and named query
  // parameters are present.
  absl::Status PopulateQueryParameters();
  absl::Status CheckQueryParametersInternal(const QueryParameters& parameters,
                                            bool is_strict_mode) const;
  // Returns all named parameters in the script.
  StringSet GetAllNamedParameters() const;

  // Controls the lifetime of the script AST, if it is owned by us.  Otherwise,
  // nullptr.
  std::unique_ptr<ParserOutput> parser_output_;

  // Points to the script AST.  This is either parsed_output_->script(), if
  // we own the script, or an externally-owned AST if parser_output_ is nullptr.
  const ASTScript* ast_script_;

  // The text of the script.  Externally owned.
  absl::string_view script_string_;

  // How to report error messages in GatherInformationAndRunChecks().
  ErrorMessageOptions error_message_options_;

  // Routine arguments existing from the beginning the script.
  ArgumentTypeMap routine_arguments_;

  NamedQueryParameterMap named_query_parameters_;
  std::map<ParseLocationPoint, int64_t> positional_query_parameters_;

  std::unique_ptr<const ControlFlowGraph> control_flow_graph_;

  bool is_procedure_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_PARSED_SCRIPT_H_
