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

#ifndef GOOGLESQL_SCRIPTING_SCRIPT_EXECUTOR_H_
#define GOOGLESQL_SCRIPTING_SCRIPT_EXECUTOR_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/scripting/control_flow_graph.h"
#include "googlesql/scripting/parsed_script.h"
#include "googlesql/scripting/procedure_extension.pb.h"
#include "googlesql/scripting/script_executor_state.pb.h"
#include "googlesql/scripting/script_segment.h"
#include "googlesql/scripting/stack_frame.h"
#include "googlesql/scripting/type_aliases.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/status.h"

namespace googlesql {

class ScriptExecutorOptions;
class StatementEvaluator;

// Interface for native procedure.
using NativeProcedureFn =
    std::function<absl::Status(VariableMap* argument_list)>;

class ProcedureDefinition {
 public:
  // If <native_function> is not empty. This procedure is native and <body> is
  // ignored.
  // TODO: hide constructor and expose ForNativeProcedure() and
  // ForSQLProcedure() to create a ProcedureDefinition.
  ProcedureDefinition(absl::string_view name,
                      const FunctionSignature& signature,
                      std::vector<std::string> argument_name_list,
                      absl::string_view body,
                      NativeProcedureFn native_function = nullptr,
                      std::unique_ptr<ProcedureExtension> extension = nullptr)
      : name_(name),
        signature_(signature),
        argument_name_list_(std::move(argument_name_list)),
        body_(body),
        native_function_(std::move(native_function)),
        extension_(std::move(extension)),
        is_dynamic_sql_(false) {}

  // Construct a Procedure for the purposes of executing Dynamic SQL
  ProcedureDefinition(const FunctionSignature& signature,
                      absl::string_view body)
      : name_("Dynamic SQL"),
        signature_(signature),
        body_(body),
        is_dynamic_sql_(true) {}

  ProcedureDefinition(const ProcedureDefinition& other)
      : name_(other.name()),
        signature_(other.signature()),
        argument_name_list_(other.argument_name_list()),
        body_(other.body()),
        native_function_(other.native_function()),
        is_dynamic_sql_(other.is_dynamic_sql()) {
    extension_ = other.extension() != nullptr
                     ? std::make_unique<ProcedureExtension>(*other.extension())
                     : nullptr;
  }

  ProcedureDefinition(ProcedureDefinition&&) = default;
  ProcedureDefinition& operator=(const ProcedureDefinition&) = default;

  const std::string& name() const { return name_; }
  const FunctionSignature& signature() const { return signature_; }
  const std::vector<std::string>& argument_name_list() const {
    return argument_name_list_;
  }
  const std::string& body() const { return body_; }
  const NativeProcedureFn& native_function() const { return native_function_; }
  // Additional information that the query engine needs about procedures in
  // the current stack (if any).
  const ProcedureExtension* extension() const {
    return extension_ ? extension_.get() : nullptr;
  }
  bool is_dynamic_sql() const { return is_dynamic_sql_; }

 private:
  // Procedure name.
  std::string name_;
  // Procedure signature.
  FunctionSignature signature_;
  // Procedure argument name list.
  std::vector<std::string> argument_name_list_;
  // Procedure body. It is the SQL text within:
  //   CREATE PROCEDURE ... BEGIN <body> END.
  std::string body_;
  // If not empty, the procedure is native and the function is called when
  // invoking the procedure.
  NativeProcedureFn native_function_;
  std::unique_ptr<ProcedureExtension> extension_;
  const bool is_dynamic_sql_;
};

// A ScriptExecutor executes a GoogleSQL script.  Script execution is performed
// via the ExecuteNext() method, which advances the script by exactly one
// statement or execution evaluation.  A typical client will call ExecuteNext()
// in a loop, as follows:
//
//   while (!script_executor->IsComplete()) {
//     GOOGLESQL_RETURN_IF_ERROR(script_executor->ExecuteNext());
//   }
//
// Execution of underlying expressions or statements is implemented in a
// StatementEvaluator callback, which is owned by the engine.  The
// StatementEvaluator is invoked from inside ExecuteNext().
//
// The engine receives each statement or expression to evaluate as a
// ParseLocationRange, which can be passed to AnalyzeNextStatement(),
// using ScriptExecutor::GetScriptText(), to obtain the resolved tree.
class ScriptExecutor {
 public:
  struct StackFrameTrace {
    // Current statement start line in script or procedure.
    int start_line = 0;
    // Current statement start column in script or procedure.
    int start_column = 0;
    // Current statement end line in script or procedure.
    int end_line = 0;
    // Current statement end column in script or procedure.
    int end_column = 0;
    // Empty for main script.
    std::string procedure_name;
    // Current statement full text. ScriptExecutor owns the string. Valid until
    // the next call to ExecuteNext(), Reset(), SetState(), or
    // SetStateWithStack().
    absl::string_view current_statement;
    // Procedure definition of the current statement. Null for the main script.
    // This pointer will be alive as long as the current statement is in the
    // script stack.
    const ProcedureDefinition* procedure_definition = nullptr;
  };

  ScriptExecutor() = default;
  ScriptExecutor(const ScriptExecutor&) = delete;
  ScriptExecutor& operator=(const ScriptExecutor&) = delete;
  virtual ~ScriptExecutor() = default;

  // Creates a ScriptExecutor object to execute a given script.
  //
  // Initially, execution is set to the start of the script.
  //
  // Does not take ownership of parameters <script> and <statement_evaluator>,
  // which must be kept alive for the duration of the returned ScriptExecutor.
  //
  // In the event that a script contains a syntax error which can be diagnosed
  // at parse-time, a failed Status will be returned, containing the location
  // and error message.  Otherwise, the parse will succeed, even if the script
  // references an invalid column, table name, etc.  Such errors will appear
  // only during execution, and only if the statement causing the error is
  // reached.
  static absl::StatusOr<std::unique_ptr<ScriptExecutor>> Create(
      absl::string_view script, const ScriptExecutorOptions& options,
      StatementEvaluator* statement_evaluator);

  // Similar to the above function, but uses an existing, externally-owned
  // AST, rather than parsing the script text.
  static absl::StatusOr<std::unique_ptr<ScriptExecutor>> CreateFromAST(
      absl::string_view script, const ASTScript* ast_script,
      const ScriptExecutorOptions& options,
      StatementEvaluator* statement_evaluator);

  // Returns true if the script execution completed successfully.
  virtual bool IsComplete() const = 0;

  // Advances the script execution by one statement, expression evaluation,
  // or control-flow operation.
  //
  // If the next statement is a scripting statement which contains inner
  // statements (e.g. IF, LOOP), ExecuteNext() will execute the minimum fragment
  // to transfer control to the next location in the script.
  //
  // If the script is already complete (IsComplete() is true), an error is
  // returned.
  //
  // Typically, ExecuteNext() is called in a loop, like the following:
  //   while (!script_executor->IsComplete()) {
  //     GOOGLESQL_RETURN_IF_ERROR(script_executor->ExecuteNext());
  //   }
  //
  // Errors are propagated as follows:
  // - If an error is handled within the script (via an EXCEPTION clause),
  //      transfers control to the start of the exception handler and returns
  //      OK.  The original error is swallowed.
  // - If an error is not handled within the script, its status is returned.
  //
  // Errors from the StatementEvaluator are handleable only if the returned
  //     status contains a ScriptException payload.
  virtual absl::Status ExecuteNext() = 0;

  // Returns the text of the original script, as passed to Create().
  virtual absl::string_view GetScriptText() const = 0;

  // Returns the current time zone. The initial value is determined from the
  // ScriptExecutorOptions, but can change as the script executes via
  // assignments to @@time_zone.
  virtual absl::TimeZone time_zone() const = 0;

  virtual int64_t stack_depth() const = 0;

  // Updates <analyzer_options> to reflect the current state of the script:
  // - Registers all system variables owned by ScriptExecutor. (Returns an error
  //     if any of these system variables already exist).
  // - Sets the default time zone according to @@time_zone
  // - If the current stack frame uses custom query parameters, updates the
  //    query parameters accordingly. (This is currently possible only through
  //    dynamic SQL).
  // Other attributes of <analyzer_options> remain unchanged.
  virtual absl::Status UpdateAnalyzerOptions(
      AnalyzerOptions& analyzer_options) const = 0;

  // Returns current stack frame's query parameters
  virtual const std::optional<
      std::variant<ParameterValueList, ParameterValueMap>>&
  GetCurrentParameterValues() const = 0;

  // Returns the next statement or clause to be executed. Nullptr if the script
  // has finished.
  virtual const ControlFlowNode* GetCurrentNode() const = 0;

  // Returns a string representation state of the execution of the script.
  //
  // The returned string may contain multiple lines of text, and will always end
  // in a terminating newline.
  std::string DebugString() const { return DebugString(false); }

  // Similar to the previous function, but if <verbose> is true, the returned
  // string will include a copy of the statement text about to execute, rather
  // than just the line and column number.
  virtual std::string DebugString(bool verbose) const = 0;

  // Gets stack trace of current execution stack. Vector index 0 is the current
  // StackFrame that is being executed and on the top of callstack. Returns
  // empty vector if script has completed.
  virtual absl::StatusOr<std::vector<StackFrameTrace>> StackTrace() const = 0;

  // Returns the currently executing stack frame. Returns null is the script
  // has completed.
  virtual const StackFrame* const GetCurrentStackFrame() const = 0;

  // Returns the name of the currently executing procedure, or an empty string
  // if we are inside the main script, or if the script has completed.
  // If Dynamic SQL is currently being executed the name of the first
  // non Dynamic SQL stack frame is returned.
  virtual absl::string_view GetCurrentProcedureName() const = 0;

  // Returns the name of the currently executing stack frame, or an empty string
  // if we are inside the main script, or if the script has completed.
  virtual absl::string_view GetCurrentStackFrameName() const = 0;

  // Returns the names and values of all script variables currently in scope.
  //
  // Calling this is not threadsafe if other threads are calling ExecuteNext().
  virtual const VariableMap& GetCurrentVariables() const = 0;

  // Returns the map of variable names to their type parameters.
  //
  // Calling this is not threadsafe if other threads are calling ExecuteNext().
  virtual const VariableTypeParametersMap& GetCurrentVariableTypeParameters()
      const = 0;

  // Returns the list of system variables known to the script executor.
  // Currently, includes system variables related to exception handling.
  //
  // An engine may choose to support additional system variables by adding their
  // names and types to the AnalyzerOptions and implementing the
  // AssignSystemVariable() callback.
  //
  // Keys in the map include the "@@" prefix.
  virtual const SystemVariableValuesMap& GetKnownSystemVariables() const = 0;

  // Default implementation of AssignSystemVariable() for variables owned by the
  // ScriptExecutor.  Returns true if assigned successfully, or false if the
  // variable is not known to the script executor, in which case, the engine
  // should perform the assignment.
  //
  // Returns an error if <value> is incompatible with the variable being
  // assigned, for example, assigning @@time_zone to NULL or an invalid time
  // zone string.
  //
  // <ast_assignment> represents the AST node of the assignment statement, and
  // should used to determine error locations.
  virtual absl::StatusOr<bool> DefaultAssignSystemVariable(
      const ASTSystemVariableAssignment* ast_assignment,
      const Value& value) = 0;

  // Gets a snapshot of state of currently executing script. See
  // ScriptExecutorStateProto definition for detail.
  virtual absl::StatusOr<ScriptExecutorStateProto> GetState() const = 0;

  // Sets the state of the currently executing script, including full call
  // stack. See ScriptExecutorStateProto definition for detail.
  virtual absl::Status SetState(const ScriptExecutorStateProto& state) = 0;

  // Get the predefined variables that were created before script run and exist
  // outside of the script scope.
  virtual VariableSet GetPredefinedVariableNames() const = 0;

  // Returns the options used to create the script executor.
  virtual const ScriptExecutorOptions& GetOptions() const = 0;

  // Returns true if the script execution completed due to a RETURN statement in
  // the main script.
  virtual bool returned_from_script() const = 0;
};

// Interface implemented by the engine to evaluate individual statements or
// expressions.
//
// For all methods, the evaluator should include line/column numbers in error
// messages, with line 1 column mapping to the start of the entire script, not
// the particular statement/expression being evaluated.
//
// If the error is to be handleable via an EXCEPTION clause, the evaluator
// must attach a ScriptException payload to the status; without the payload,
// the error is treated as fatal, and will always go unhandled.
//
// It is suggested that evaluators convert analyzer errors specific to the
// statement/expression into script-relative positions by using the following
// pattern:
//   #include "googlesql/scripting/error_helpers.h"
//   ...
//
//   GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(...))
//      .With(ConvertLocalErrorToScriptError(segment));
//
class StatementEvaluator {
 public:
  struct VariableChange {
    IdString var_name;
    Value value;
    TypeParameters type_params;
  };

  virtual ~StatementEvaluator() = default;

  // Executes a single GoogleSQL statement.
  //
  // During dry runs, (executor.GetOptions().dry_run()), the implementation
  // should validate the statement, but not actually execute it.  During dry
  // runs, this function should be quick and have no side effects.
  virtual absl::Status ExecuteStatement(const ScriptExecutor& executor,
                                        const ScriptSegment& segment) = 0;

  // Executes a query and returns the results.
  // <segment> should have node type of:
  // - AST_QUERY_STATEMENT
  // - AST_QUERY
  // Should not be called in dry run.
  virtual absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  ExecuteQueryWithResult(const ScriptExecutor& executor,
                         const ScriptSegment& segment) = 0;

  // Serializes iterator to an Any message.
  virtual absl::Status SerializeIterator(const EvaluatorTableIterator& iterator,
                                         google::protobuf::Any& out) = 0;

  // Deserializes an Any message to EvaluatorTableIterator.
  virtual absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  DeserializeToIterator(const google::protobuf::Any& msg,
                        const ScriptExecutor& executor,
                        const ParsedScript& parsed_script) = 0;

  // Evaluates the following expression:
  //  CASE <case_value>
  //     WHEN <when_conditions[0]> 0
  //     WHEN <when_conditions[1]> 1
  //     ...
  //  ELSE -1
  //
  // Used for CASE...WHEN statements to determine which CASE branch
  // to take.
  virtual absl::StatusOr<int> EvaluateCaseExpression(
      const ScriptSegment& case_value,
      const std::vector<ScriptSegment>& when_values,
      const ScriptExecutor& executor) = 0;

  // Returns the number of bytes of memory used by <iterator> that was
  // previously returned by ExecuteQueryWithResult() or
  // DeserializeToIterator(). This is used to enforce memory
  // limits when running a script.
  virtual absl::StatusOr<int64_t> GetIteratorMemoryUsage(
      const EvaluatorTableIterator& iterator) = 0;

  // Determines if a statement is legal as dynamic SQL
  virtual bool IsAllowedAsDynamicSql(const ASTStatement* stmt) { return true; }

  // Evaluates a scalar expression.  Coerces the expression result to
  // <target_type>, using assignment semantics (Coercer::AssignableTo()), and
  // returns the value of the expression, converted to <target_type>.  If
  // conversion is not possible, the result is a failed status.
  //
  // If <target_type> is nullptr, the evaluator should simply return the result
  // of the expression, without any type coercion.  For literal expressions,
  // the result type should be same as that of the generated table column
  // in the DDL statement "CREATE TABLE t AS (SELECT <value>)".  For example,
  // literal NULL should have type INT64.
  //
  // During dry runs (executor.GetOptions().dry_run()), the implementation
  // should validate the expression, and return a NULL value if the expression
  // is valid, but cannot be evaluated quickly.
  virtual absl::StatusOr<Value> EvaluateScalarExpression(
      const ScriptExecutor& executor, const ScriptSegment& segment,
      const Type* target_type) = 0;

  // Parses and resolves the type named specified by the text
  // <segment.GetSegmentText()>.  The type must be owned by a type factory that
  // remains alive throughout the execution of the script.
  virtual absl::StatusOr<TypeWithParameters> ResolveTypeName(
      const ScriptExecutor& executor, const ScriptSegment& segment) = 0;

  // Returns whether the engine supports the specified type as a variable type
  // and the specified type parameter values.
  virtual bool IsSupportedVariableType(
      const TypeWithParameters& type_with_params) = 0;

  // Checks that the input <value> adheres to the type parameter constraints
  // within <type_params>. Depending on the type of <value>, this expression
  // may possibly mutate the value to be compliant with the given type parameter
  // constraints. Specific enforcement behavior is determined by the engine.
  // If the returned error is an internal error the script will be aborted. All
  // other returned error types should be user-facing and will be handled.
  virtual absl::Status ApplyTypeParameterConstraints(
      const TypeParameters& type_params, Value* value) = 0;

  // Returns whether CALL to <path> should execute via ExecuteStatement(),
  // instead of as a script procedure.
  virtual bool ShouldExecuteProcedureAsStatement(
      const ScriptExecutor& executor,
      const absl::Span<const std::string>& path) {
    return false;
  }

  // Loads definition for procedure in <path>. Returns Status with
  // StatusCode::kNotFound if no procedure can be found in <path>.
  // In dry run, the returned pointer may be null. This indicates that
  // the respective CALL is assumed to exist with unknown signature
  // and will result in argument validation only. <num_arguments> indicates
  // the number of arguments passed into the body of the procedure.
  virtual absl::StatusOr<std::unique_ptr<ProcedureDefinition>> LoadProcedure(
      const ScriptExecutor& executor, const absl::Span<const std::string>& path,
      const int64_t num_arguments) {
    return absl::UnimplementedError("LoadProcedure is not supported");
  }

  // Performs any engine-specific additional actions when the procedure is
  // entered.
  //
  // This function is called whenever a procedure frame is pushed onto the
  // call stack. This can be due to a CALL statement, or due to EXECUTE
  // IMMEDIATE.
  //
  // This function is NOT called:
  // - If ShouldExecuteProcedureAsStatement() returns true for the procedure.
  // - In dry-run mode.
  virtual absl::Status OnProcedureEntered(
      const ScriptExecutor& executor,
      const absl::Span<const std::string>& path) {
    // By default no additional work is needed.
    return absl::OkStatus();
  }

  // Performs any engine-specific cleanup when the procedure is exited.
  //
  // This function is called whenever a procedure frame is popped off the
  // call stack, either via normal exit, or via stack unwinding due to an
  // exception that is handled in a calling procedure.
  // If a procedure terminates due to an unhandled exception, script
  // execution aborts, and this function will NOT be called.
  //
  // This function is NOT called when OnProcedureEntered() is not called,
  // specifically:
  // - If ShouldExecuteProcedureAsStatement() returns true for the procedure.
  // - In dry-run mode.
  virtual absl::Status OnProcedureExited(
      const ScriptExecutor& executor,
      const absl::Span<const std::string>& path) {
    // By default no additional work is needed.
    return absl::OkStatus();
  }

  // Assigns a system variable to a value.
  //
  // This function should be implemented by engines which define their own
  // system variables which support assignment, or need to perform a custom
  // action when a ScriptExecutor-managed system variable is assigned to.
  //
  // Implementations should invoke executor->DefaultAssignSystemVariable() to
  // handle executor-owned variables, with optional engine-specific logic after
  // the call returns.
  //
  // If the variable is read-only, engines should call
  // AssignmentToReadOnlySystemVariable() to generate the error.
  //
  // The engine does not need to handle non-existent variables or values of an
  // incorrect type; this is enforced by the script executor prior to the call,
  // using the AnalyzerOptions; all engine-owned system variables must be
  // registered in the AnalyzerOptions in order for this callback to be invoked.
  //
  // For system variables of struct type, ScriptExecutor does not support
  // assignment to individual fields; the entire variable must be assigned to
  // at once.
  //
  // If not overridden, the default implementation defers to
  // executor->DefaultAssignSystemVariable(), treating all engine-owned system
  // as read-only.
  virtual absl::Status AssignSystemVariable(
      ScriptExecutor* executor,
      const ASTSystemVariableAssignment* ast_assignment, const Value& value);

  // Perform any engine-specific additional actions when the variable changes
  // (i.e. declaration or updates). Not intended for when the variable is
  // destroyed (i.e. when a block ends or procedure exits).
  // The input node is the current ast node for statement changing the
  // variables.
  virtual absl::Status OnVariablesChanged(
      const ScriptExecutor& executor, const googlesql::ASTNode* current_node,
      const StackFrame& var_declaration_stack_frame,
      const std::vector<VariableChange>& variable_changes) {
    // By default no additional work is needed.
    return absl::OkStatus();
  }

  // Performs any engine-specific cleanup when the variable is destroyed.
  // The input node is the current ast node for statement destroying the
  // variables.
  // destroyed_ids may contain variables that are not called with
  // OnVariablesChanged. This may happen when a DECLARE  statement did not
  // execute due to an exception
  virtual absl::Status OnVariablesDestroyed(
      const ScriptExecutor& executor, const googlesql::ASTNode* current_node,
      const StackFrame& var_declaration_stack_frame,
      const std::vector<IdString>& destroyed_ids) {
    // By default no additional work is needed.
    return absl::OkStatus();
  }

  // Notifies that the validation is terminated. Validation refers to
  // dry_run=true.
  // This method is called for the following cases:
  // - When a conditional branch is encountered.
  // - When a loop control goes back to the beginning of the loop.
  // - When a query result is required to continue validation.
  virtual absl::Status OnValidationTerminated(
      const ScriptExecutor& executor, const googlesql::ASTNode* current_node) {
    return absl::OkStatus();
  }
};

class MemoryLimitOptions {
 public:
  static MemoryLimitOptions Unlimited() {
    return MemoryLimitOptions{INT64_MAX, INT64_MAX};
  }

  MemoryLimitOptions(int64_t per_variable_size_limit,
                     int64_t total_memory_limit)
      : per_variable_size_limit_(per_variable_size_limit),
        total_memory_limit_(total_memory_limit) {}

  MemoryLimitOptions(const MemoryLimitOptions&) = default;
  MemoryLimitOptions& operator=(const MemoryLimitOptions&) = default;

  int64_t per_variable_size_limit() const { return per_variable_size_limit_; }
  int64_t total_memory_limit() const { return total_memory_limit_; }

 private:
  // Limit on per-variable sizes. Exceeding this limit will cause the
  // ScriptExecutor to return an error.
  int64_t per_variable_size_limit_;
  // Limit on total memory size. Includes memory used by variables and
  // iterators.
  // Exceeding this limit will cause the ScriptExecutor to return an error.
  int64_t total_memory_limit_;
};

// Options when creating a ScriptExecutor.
class ScriptExecutorOptions {
 public:
  constexpr static int kDefaultMaximumStackDepth = 50;

  ScriptExecutorOptions() = default;
  ScriptExecutorOptions(const ScriptExecutorOptions&) = default;
  ScriptExecutorOptions& operator=(const ScriptExecutorOptions&) = default;

  void set_maximum_stack_height(int height) { maximum_stack_depth_ = height; }

  void set_variable_size_limit_options(const MemoryLimitOptions& options) {
    variable_size_limit_options_ = options;
  }

  void set_type_factory(TypeFactory* type_factory) {
    type_factory_ = type_factory;
  }

  void set_dry_run(bool dry_run) { dry_run_ = dry_run; }

  absl::TimeZone default_time_zone() const { return default_time_zone_; }
  const LanguageOptions& language_options() const { return language_options_; }
  const SystemVariablesMap& engine_owned_system_variables() const {
    return engine_owned_system_variables_;
  }
  void set_engine_owned_system_variables(
      SystemVariablesMap engine_owned_system_variables) {
    engine_owned_system_variables_ = std::move(engine_owned_system_variables);
  }

  const ParsedScript::QueryParameters& query_parameters() const {
    return query_parameters_;
  }

  const ParsedScriptOptions& parsed_script_options() const {
    return parsed_script_options_;
  }
  void set_parsed_script_options(ParsedScriptOptions options) {
    parsed_script_options_ = std::move(options);
  }

  ErrorMessageOptions error_message_options() const {
    return error_message_options_;
  }

  const MemoryLimitOptions& variable_size_limit_options() const {
    return variable_size_limit_options_;
  }
  int maximum_stack_depth() const { return maximum_stack_depth_; }
  TypeFactory* type_factory() const { return type_factory_; }
  bool dry_run() const { return dry_run_; }

  // Propagates fields from <analyzer_options> that have corresponding fields in
  // ScriptExecutorOptions.
  //
  // Note: All system variables in <analyzer_options> will be propagated to
  // <engine_owned_system_variables_>. It is assumed that all system variables
  // present are engine-owned.
  void PopulateFromAnalyzerOptions(const AnalyzerOptions& analyzer_options);

  void set_query_parameters_check_strict_mode(
      bool query_parameters_strict_mode) {
    query_parameters_check_strict_mode_ = query_parameters_strict_mode;
  }

  bool query_parameters_check_strict_mode() const {
    return query_parameters_check_strict_mode_;
  }

 private:
  absl::TimeZone default_time_zone_;
  LanguageOptions language_options_;
  SystemVariablesMap engine_owned_system_variables_;
  ParsedScript::QueryParameters query_parameters_;
  ParsedScriptOptions parsed_script_options_;
  ErrorMessageOptions error_message_options_{.mode = ERROR_MESSAGE_ONE_LINE};

  MemoryLimitOptions variable_size_limit_options_ =
      MemoryLimitOptions::Unlimited();
  // Maximum stack depth that ScriptExecutor may execute a script with.
  int maximum_stack_depth_ = kDefaultMaximumStackDepth;
  // This TypeFactory is used by ScriptExecutor to create types for variables.
  // There may be type coupling with other TypeFactory, say the TypeFactory in
  // certain implementation of StatementEvaluator. In such case, a shared
  // TypeFactory should be passed-in as part of ScriptExecutorOptions.
  TypeFactory* type_factory_ = nullptr;

  // If true, enables "dry run" semantics.  In dry run mode, the normal rules of
  // control flow are ignored, and control simply flows sequentially within the
  // script, so that every statement/expression is "executed".
  // - When a control statements, such as IF and WHILE, execution terminates
  //   as soon as a control-flow decision needs to be made based upon a
  //   condition value; all remaining statements in the script are skipped.
  // - CALL statements validate that the procedure exists and has a signature
  //     that matches the argument list, but do not execute the procedure.
  // - When EvaluateScalarExpression() is called in a dry run, the
  //     implementation should return a NULL value whenever computing it would
  //     require a time-consuming operation, which is not appropriate for dry
  //     runs.
  // - When ExecuteStatement() is called, the implementation should simply
  //     validate the statement, rather than execute it.
  //
  // Note: This option is likely to be deprecated in the future, in favor of
  // the ScriptValidator class, once ScriptValidator is feature-complete enough
  // to replace it.
  bool dry_run_ = false;

  // This is applicable only when there are no query parameters available.
  // If true, an error is returned if a query parameter is referenced in the
  // script. Otherwise, error can happen while executing the script.
  bool query_parameters_check_strict_mode_ = false;
};

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_SCRIPT_EXECUTOR_H_
