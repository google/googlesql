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

#ifndef GOOGLESQL_PUBLIC_EVALUATOR_BASE_H_
#define GOOGLESQL_PUBLIC_EVALUATOR_BASE_H_


//
// GoogleSQL in-memory expression or query evaluation using the reference
// implementation.
//
// These are abstract base classes; you should choose either
// PreparedExpression/PreparedQuery (in evaluator.h) or
// PreparedExpressionLite/PreparedQueryLite (in evaluator_lite.h). You probably
// want the full version (evaluator.h), not the "lite" version, which is
// optimized for executable size at the expense of some features. See
// evaluator_lite.h for more information.
//
// evaluator_base.h  <- abstract base class and documentation
// evaluator.h       <- entry point for the full evaluator
// evaluator_lite.h  <- entry point for the "lite" evaluator
//
// Evaluating Expressions
// ----------------------
//
// When evaluating an expression, callers can provide
//   - A set of expression columns - column names usable in the expression.
//   - Optionally, one in-scope expression column - a column, possibly named,
//        that is implicitly in scope when evaluating the expression, so its
//        fields can be accessed directly without any qualifiers.
//   - A set of parameters - parameters usable in the expression as @param.
//
// Examples:
//
//   PreparedExpression expr("1 + 2");
//   Value result = expr.Execute().value();  // Value::Int64(3)
//
//   PreparedExpression expr("(@param1 + @param2) * col");
//   Value result = expr.Execute(
//     {{"col", Value::Int64(5)}},
//     {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}}).value();
//   // result = Value::Int64(15)
//
// The above expression could also be set up as follows:
//
//   PreparedExpression expr("(@param1 + @param2) * col");
//   AnalyzerOptions options;
//   GOOGLESQL_CHECK_OK(options.AddExpressionColumn("col", types::Int64Type()));
//   GOOGLESQL_CHECK_OK(options.AddQueryParameter("param1", types::Int64Type()));
//   GOOGLESQL_CHECK_OK(options.AddQueryParameter("param2", types::Int64Type()));
//   GOOGLESQL_CHECK_OK(expr.Prepare(options));
//   ABSL_CHECK(types::Int64Type()->Equals(expr.output_type()));
//   Value result = expr.Execute(
//     {{"col", Value::Int64(5)}},
//     {{"param1", Value::Int64(1)}, {"param2", Value::Int64(2)}}).value();
//
// An in-scope expression column can be used as follows:
//
//   TypeFactory type_factory;  // NOTE: Must outlive the PreparedExpression.
//   const ProtoType* proto_type;
//   GOOGLESQL_CHECK_OK(type_factory.MakeProtoType(MyProto::descriptor(), &proto_type));
//
//   AnalyzerOptions options;
//   GOOGLESQL_CHECK_OK(options.SetInScopeExpressionColumn("value", proto_type));
//
//   PreparedExpression expr("field1 + value.field2");
//   GOOGLESQL_CHECK_OK(expr.Prepare(options));
//
//   Value result = expr.Execute(
//     {{"value", values::Proto(proto_type, my_proto_value)}}).value();
//
// User-defined functions can be used in expressions as follows:
//
//   FunctionOptions function_options;
//   function_options.set_evaluator(
//       [](const absl::Span<const Value>& args) {
//         // Returns string length as int64.
//         ABSL_DCHECK_EQ(args.size(), 1);
//         ABSL_DCHECK(args[0].type()->Equals(googlesql::types::StringType()));
//         return Value::Int64(args[0].string_value().size());
//       });
//
//   AnalyzerOptions options;
//   SimpleCatalog catalog{"udf_catalog"};
//   GOOGLESQL_CHECK_OK(catalog.AddGoogleSQLFunctionsAndTypes(options.language()));
//   catalog.AddOwnedFunction(new Function(
//       "MyStrLen", "udf", googlesql::Function::SCALAR,
//       {{googlesql::types::Int64Type(), {googlesql::types::StringType()},
//         5000}},  // some function id
//       function_options));
//
//   PreparedExpression expr("1 + mystrlen('foo')");
//   GOOGLESQL_CHECK_OK(expr.Prepare(options, &catalog));
//   Value result = expr.Execute().value();  // returns 4
//
// For more examples, see googlesql/public/evaluator_test.cc
//
// Parameters are passed as a map of strings to googlesql::Value. Multiple
// invocations of Execute() must use identical parameter names and types.
//
// The values returned by the Execute() method must not be accessed after
// destroying PreparedExpression that returned them.
//
// Thread safety: PreparedExpression is thread-safe. The recommended way to use
// PreparedExpression from multiple threads is to call Prepare() once and then
// call ExecuteAfterPrepare() in parallel from multiple threads for concurrent
// evaluations. (It is also possible to call Execute() in parallel multiple
// times, but in that case, each of the executions have to consider whether to
// call Prepare(), and there is some serialization there.)
//
// Evaluating Queries
// ------------------
// Queries can be evaluated using PreparedQuery.  This works similarly to
// PreparedExpression as described above, including the support for
// parameters and user-defined functions.
//
// User-defined tables can be used in queries (and even expressions) by
// implementing the EvaluatorTableIterator interface documented in
// evaluator_table_iter.h as follows:
//
//   std::unique_ptr<Table> table =
//   ... Create a Table that overrides
//       Table::CreateEvaluatorTableIterator() ...
//   SimpleCatalog catalog;
//   catalog.AddTable(table->Name(), table.get());
//   PreparedQuery query("select * from <table>");
//   GOOGLESQL_CHECK_OK(query.Prepare(AnalyzerOptions(), &catalog));
//   std::unique_ptr<EvaluatorTableIterator> result =
//     query.Execute().value();
//   ... Iterate over 'result' ...
//
// Once a query is successfully prepared, the output schema can be retrieved
// using num_columns(), column_name(), column_type(), etc.  After Execute(), the
// schema is also available from the EvaluatorTableIterator.
//
// Evaluating DML statements
// ------------------
// DML statements can be evaluated using PreparedModify. This works
// similarly to PreparedQuery as described above.
//
// User-defined tables can be used by implementing the EvaluatorTableIterator
// interface documented in evaluator_table_iter.h as follows:
//
//   std::unique_ptr<Table> table =
//   ... Create a Table that overrides
//       Table::CreateEvaluatorTableIterator() ...
//   SimpleCatalog catalog;
//   catalog.AddTable(table->Name(), table.get());
//   PreparedModify statement("delete from <table> where true");
//   GOOGLESQL_CHECK_OK(statement.Prepare(AnalyzerOptions(), &catalog));
//   std::unique_ptr<EvaluatorTableModifyIterator> result =
//     statement.Execute().value();
//   ... Iterate over `result` (which lists deleted rows) ...

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/clock.h"

namespace googlesql {

class EvaluationContext;
class ResolvedExpr;
class ResolvedQueryStmt;

using ParameterValueMap = std::map<std::string, Value>;
using ParameterValueList = std::vector<Value>;

namespace internal {
class Evaluator;
}  // namespace internal

struct EvaluatorOptions {
 public:
  // If 'type_factory' is provided, the return value's Type will
  // be allocated from 'type_factory'.  Otherwise, the returned Value is
  // allocated using an internal TypeFactory and is only valid for the lifetime
  // of the PreparedExpression.
  // Does not take ownership.
  TypeFactory* type_factory = nullptr;

  // Functions which rely on the current timestamp/date will be evaluated based
  // on the time returned by this clock. By default this is the normal system
  // clock but it can (optionally) be overridden.
  // Does not take ownership.
  googlesql_base::Clock* clock = googlesql_base::Clock::RealClock();

  // The default time zone to use. If not set, the default time zone is
  // "America/Los_Angeles".
  std::optional<absl::TimeZone> default_time_zone;

  // If true, evaluation will scramble the order of relations whose order is not
  // defined by GoogleSQL. This requires some extra processing, so should only
  // be enabled in tests.
  bool scramble_undefined_orderings = false;

  // Limit on the maximum number of in-memory bytes used by an individual Value
  // that is constructed during evaluation. This bound applies to all Value
  // types, including variable-sized types like STRING, BYTES, ARRAY, and
  // STRUCT. Exceeding this limit results in an error. See the implementation of
  // Value::physical_byte_size for more details.
  int64_t max_value_byte_size = 1024 * 1024;

  // The limit on the maximum number of in-memory bytes that can be used for
  // storing accumulated rows (e.g., during an ORDER BY query). Exceeding this
  // limit results in an error.
  //
  // This is not a hard bound on total memory usage. Some additional memory will
  // be used proportional to query complexity, individual row sizes, etc. This
  // limit bounds memory that can be used by all operators that buffer multiple
  // rows of data (which could otherwise use unbounded memory, depending on
  // data size).
  //
  // It is also possible for the memory accounting to overestimate the amount of
  // memory being used. One way this can happen is if Values are copied between
  // rows during a query (e.g., while evaluating an array join). Large Values
  // use reference counting to share the same underlying memory, but the memory
  // accounting charges each of them individually. In some cases, it is
  // necessary to set this option to a very large value.
  int64_t max_intermediate_byte_size = 128 * 1024 * 1024;

  // Insert ignore DML statements return all the rows that were processed,
  // including the rows that were not inserted.
  bool return_all_insert_rows_insert_ignore_dml = false;

  // If true, the reference implementation will account for the memory used by
  // function arguments and thus limit the cumulative memory used by function
  // arguments to the value of 'max_intermediate_byte_size'.
  // TODO: Remove this option once the feature is fully rolled out.
  bool enable_function_args_memory_accounting = false;
};

class PreparedExpressionBase {
 public:
  // Legacy constructor.
  // Prefer using the constructor which takes EvaluatorOptions (below).
  //
  // If 'type_factory' is provided, the return value's Type will
  // be allocated from 'type_factory'.  Otherwise, the returned Value is
  // allocated using an internal TypeFactory and is only valid for the lifetime
  // of the PreparedExpression.
  explicit PreparedExpressionBase(absl::string_view sql,
                                  TypeFactory* type_factory = nullptr);

  // Constructor. Additional options can be provided by filling out the
  // EvaluatorOptions struct.
  PreparedExpressionBase(absl::string_view sql,
                         const EvaluatorOptions& options);

  // Constructs a PreparedExpression using a ResolvedExpr directly. Does not
  // take ownership of <expression>. <expression> must outlive this.
  //
  // This is useful if you have an expression from some other source than
  // directly from raw SQL. For example, if you serialized a ResolvedExpr and
  // sent it to a server for execution, you could pass the deserialized node
  // directly to this, without first converting it to SQL.
  //
  // The AST must validate successfully with ValidateStandaloneResolvedExpr.
  // Otherwise, the program may crash in Prepare or Execute.
  PreparedExpressionBase(const ResolvedExpr* expression,
                         const EvaluatorOptions& options);
  PreparedExpressionBase(const PreparedExpressionBase&) = delete;
  PreparedExpressionBase& operator=(const PreparedExpressionBase&) = delete;

  virtual ~PreparedExpressionBase() = 0;

  // This method can optionally be called before Execute() to set analyzer
  // options and to return parsing and analysis errors, if any. If Prepare() is
  // used, the names and types of query parameters and expression columns must
  // be set in 'options'. (We also force 'options.prune_unused_columns' since
  // that would ideally be the default.)
  //
  // If 'catalog' is set, it will be used to resolve tables and functions
  // occurring in the expression. Passing a custom 'catalog' allows defining
  // user-defined functions with custom evaluation specified via
  // FunctionOptions, as well as user-defined tables (see the file comment for
  // details). Calling any user-defined function that does not provide an
  // evaluator returns an error. 'catalog' must outlive Execute() and
  // output_type() calls.  'catalog' should contain GoogleSQL built-in functions
  // added by calling AddGoogleSQLFunctionsAndTypes with 'options.language'.
  //
  // If a ResolvedExpr was already supplied to the PreparedExpression
  // constructor, 'catalog' is ignored.
  absl::Status Prepare(const AnalyzerOptions& options,
                       Catalog* catalog = nullptr);

  // Get the list of column names referenced in this expression. The columns
  // will be returned in lower case, as column expressions are case-insensitive
  // when evaluated.
  //
  // This can be used for efficiency, in the case where there are
  // many possible columns in a datastore but only a small fraction
  // of them are referenced in a query. The list of columns returned
  // from this method is the minimal set that must be provided to
  // Execute().
  //
  // Example:
  //   PreparedExpression expr("col > 1");
  //   options.AddExpressionColumn("col", types::Int64Type());
  //   options.AddExpressionColumn("extra_col", types::Int64Type());
  //   GOOGLESQL_CHECK_OK(expr.Prepare(options, &catalog));
  //   ...
  //   const std::vector<string> columns =
  //              expr.GetReferencedColumns().value();
  //   ParameterValueMap col_map;
  //   for (const string& col : columns) {
  //     col_map[col] = datastore.GetValueForColumn(col);
  //   }
  //   auto result = expr.Execute(col_map);
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<std::vector<std::string>> GetReferencedColumns() const;

  // Get the list of parameters referenced in this expression.
  //
  // This method is similar to GetReferencedColumns(), but for parameters
  // instead. This returns the minimal set of parameters that must be provided
  // to Execute(). Named and positional parameters are mutually exclusive, so
  // this will return an empty list if GetPositionalParameterCount() returns a
  // non-zero number.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<std::vector<std::string>> GetReferencedParameters() const;

  // Gets the number of positional parameters in this expression.
  //
  // This returns the number of positional parameters that must be provided to
  // Execute(). Any extra positional parameters are ignored. Named and
  // positional parameters are mutually exclusive, so this will return 0 if
  // GetReferencedParameters() returns a non-empty list.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<int> GetPositionalParameterCount() const;

  // Options struct for Execute() and ExecuteAfterPrepare() function calls.
  struct ExpressionOptions {
    ExpressionOptions() : deadline(absl::InfiniteFuture()) {};

    // Columns for the expression. Represented as a map or unordered list.
    // At most one of these can be specified.
    //
    // If using an in-scope expression column, <columns> should have an entry
    // storing the Value for that column with its registered name (possibly
    // ""). For the implicit Prepare case, an entry in <columns> with an empty
    // name will be treated as an anonymous in-scope expression column.
    std::optional<ParameterValueMap> columns;
    // Allows for a more efficient evaluation by requiring for the <columns> and
    // <parameters> values to be passed in a particular order. It is intended
    // for users that want to repeatedly evaluate an expression with different
    // values of (hardcoded) parameters. In that case, it is more efficient than
    // the other forms of Execute(), whose implementations involve map
    // operations on column and/or named query parameters. <columns> must be in
    // the order returned by GetReferencedColumns. If positional parameters are
    // used, they are passed in <parameters>. If named parameters are used, they
    // are passed in <parameters> in the order returned by
    // GetReferencedParameters.
    // REQUIRES: To be called via ExecuteAfterPrepare().
    std::optional<ParameterValueList> ordered_columns;

    // Parameters for the expression. Represented as a map or unordered list.
    // At most one of these can be specified.
    std::optional<ParameterValueMap> parameters;
    std::optional<ParameterValueList> ordered_parameters;

    // Optional system variables for all variants of Execute.
    SystemVariableValuesMap system_variables;

    // Optional deadline for the expression evaluation. Deadline is checked
    // every time a ValueExpr is evaluated (e.g: IF, ARRAY, LIKE).
    absl::Time deadline;

    // Optional session user for the expression evaluation. Session user is used
    // to evaluate the current user (e.g. in the SESSION_USER function).
    std::optional<std::string> session_user;
  };

  // Execute the expression.
  //
  // If Prepare has not been called, the first call to Execute will call
  // Prepare with implicitly constructed AnalyzerOptions using the
  // names and types from <columns> and <parameters>.
  //
  // NOTE: The returned Value is only valid for the lifetime of this
  // PreparedExpression unless an external TypeFactory was passed to the
  // constructor.
  absl::StatusOr<Value> Execute(
      ExpressionOptions options = ExpressionOptions());

  // Shorthand for calling Execute, filling the options using maps.
  absl::StatusOr<Value> Execute(ParameterValueMap columns,
                                ParameterValueMap parameters = {},
                                SystemVariableValuesMap system_variables = {});

  // Shorthand for calling Execute, filling the options positionally.
  absl::StatusOr<Value> ExecuteWithPositionalParams(
      ParameterValueMap columns, ParameterValueList positional_parameters,
      SystemVariableValuesMap system_variables = {});

  // This is the same as Execute, but is a const method, and requires that
  // Prepare has already been called. See the description of Execute for details
  // about the arguments and return value.
  //
  // Thread safe. Multiple evaluations can proceed in parallel.
  // REQUIRES: Prepare() has been called successfully.
  absl::StatusOr<Value> ExecuteAfterPrepare(
      ExpressionOptions options = ExpressionOptions()) const;

  // Shorthand for calling ExecuteAfterPrepare, filling the options using maps.
  absl::StatusOr<Value> ExecuteAfterPrepare(
      ParameterValueMap columns, ParameterValueMap parameters = {},
      SystemVariableValuesMap system_variables = {}) const;

  // Shorthand for calling ExecuteAfterPrepare, filling the options
  // positionally.
  absl::StatusOr<Value> ExecuteAfterPrepareWithPositionalParams(
      ParameterValueMap columns, ParameterValueList positional_parameters,
      SystemVariableValuesMap system_variables = {}) const;

  // Shorthand for calling ExecuteAfterPrepare, filling the options
  // positionally.
  absl::StatusOr<Value> ExecuteAfterPrepareWithOrderedParams(
      ParameterValueList columns, ParameterValueList parameters,
      SystemVariableValuesMap system_variables = {}) const;

  // Returns a human-readable representation of how this expression would
  // actually be executed. Do not try to interpret this string with code, as the
  // format can change at any time. Requires that Prepare has already been
  // called.
  absl::StatusOr<std::string> ExplainAfterPrepare() const;

  // REQUIRES: Prepare() or Execute() must be called first.
  const Type* output_type() const;

 private:
  std::unique_ptr<internal::Evaluator> evaluator_;
};

struct QueryOptions {
  // Parameters for the expression. Represented as a map or unordered list.
  // At most one of these can be specified.
  std::optional<ParameterValueMap> parameters;

  // Allows for a more efficient evaluation by requiring for the <parameters>
  // to be passed in a particular order.
  std::optional<ParameterValueList> ordered_parameters;

  // Optional system variables for all variants of Execute.
  SystemVariableValuesMap system_variables;
};

// See evaluator_base.h for the full interface and usage instructions.
class PreparedQueryBase {
 public:
  // Constructor. Additional options can be provided by filling out the
  // EvaluatorOptions struct.
  PreparedQueryBase(absl::string_view sql, const EvaluatorOptions& options);

  // Constructs a PreparedQuery using a ResolvedQueryStmt directly. Does not
  // take ownership of <stmt>. <stmt> must outlive this object.
  //
  // This is useful if you have a query from some other source than directly
  // from raw SQL. For example, if you serialized a ResolvedQueryStmt and sent
  // it to a server for execution, you could pass the deserialized node directly
  // to this, without first converting it to SQL.
  //
  // The AST must validate successfully with ValidateResolvedStatement.
  // Otherwise, the program may crash in Prepare or Execute.
  PreparedQueryBase(const ResolvedQueryStmt* stmt,
                    const EvaluatorOptions& options);

  PreparedQueryBase(const PreparedQueryBase&) = delete;
  PreparedQueryBase& operator=(const PreparedQueryBase&) = delete;

  // Crashes if any iterator returned by Execute() has not yet been destroyed.
  virtual ~PreparedQueryBase() = 0;

  // This method can optionally be called before Execute() to set analyzer
  // options and to return parsing and analysis errors, if any. If Prepare() is
  // used, the names and types of query parameters must be set in 'options'. (We
  // also force 'options.prune_unused_columns' since that would ideally be the
  // default.)
  //
  // If 'catalog' is set, it will be used to resolve tables and functions
  // occurring in the query. Passing a custom 'catalog' allows defining
  // user-defined functions with custom evaluation specified via
  // FunctionOptions, as well as user-defined tables (see the file comment for
  // details). Calling any user-defined function that does not provide an
  // evaluator returns an error. 'catalog' must outlive Execute() and
  // output_type() calls.  'catalog' should contain GoogleSQL built-in functions
  // added by calling AddGoogleSQLFunctionsAndTypes with 'options.language'.
  //
  // If a ResolvedQueryStmt was already supplied to the PreparedQuery
  // constructor, 'catalog' is ignored.
  absl::Status Prepare(const AnalyzerOptions& options,
                       Catalog* catalog = nullptr);

  // Get the list of parameters referenced in this query.
  //
  // This returns the minimal set of parameters that must be provided
  // to Execute().
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<std::vector<std::string>> GetReferencedParameters() const;

  // Gets the number of positional parameters in this query.
  //
  // This returns the exact number of positional parameters that must be
  // provided to Execute(). Named and positional parameters are mutually
  // exclusive, so this will return 0 if GetReferencedParameters() returns a
  // non-empty list.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<int> GetPositionalParameterCount() const;

  // Kept for compatibility with existing code. We can't use the
  // "[[deprecated]]" annotation here until c++ 23.
  using QueryOptions = googlesql::QueryOptions;

  // Execute the query. This object must outlive the return value.
  //
  // If Prepare() has not been called, the first call to Execute will call
  // Prepare with implicitly constructed AnalyzerOptions using the
  // names and types from <parameters>.
  //
  // This method is thread safe. Multiple executions can proceed in parallel,
  // each using a different iterator.
  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> Execute(
      QueryOptions options = QueryOptions());

  // Shorthand for calling Execute, filling the options using maps.
  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> Execute(
      ParameterValueMap parameters,
      SystemVariableValuesMap system_variables = {});

  // Shorthand for calling Execute, filling the options positionally.
  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  ExecuteWithPositionalParams(ParameterValueList positional_parameters,
                              SystemVariableValuesMap system_variables = {});

  // This is the same as Execute, but is a const method, and requires that
  // Prepare has already been called. See the description of Execute for details
  // about the arguments and return value.
  //
  // If positional parameters are passed in, a more efficient form of Execute is
  // invoked.
  //
  // Thread safe. Multiple evaluations can proceed in parallel.
  // REQUIRES: Prepare() has been called successfully.
  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> ExecuteAfterPrepare(
      QueryOptions options = QueryOptions()) const;

  // Shorthand for calling ExecuteAfterPrepare, filling the options
  // positionally.
  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> ExecuteAfterPrepare(
      ParameterValueList parameters,
      SystemVariableValuesMap system_variables = {}) const;

  // Returns a human-readable representation of how this query would actually
  // be executed. Do not try to interpret this string with code, as the
  // format can change at any time. Requires that Prepare has already been
  // called.
  absl::StatusOr<std::string> ExplainAfterPrepare() const;

  // Get the schema of the output table of this query. Anonymous column names
  // are empty. (There may be more than one column with the same name.)
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  //
  // NOTE: The returned Types are only valid for the lifetime of this
  // PreparedQuery unless an external TypeFactory was passed to the
  // constructor.
  int num_columns() const;
  std::string column_name(int i) const;
  const Type* column_type(int i) const;
  using NameAndType = std::pair<std::string, const Type*>;
  std::vector<NameAndType> GetColumns() const;

  // Returns whether the output is a value table.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  bool is_value_table() const {
    return resolved_query_stmt()->is_value_table();
  }

  // Get the ResolvedQueryStmt for this query.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  const ResolvedQueryStmt* resolved_query_stmt() const;

 private:
  friend class PreparedQueryTest;

  // For unit tests, it is possible to set a callback that is invoked every time
  // an EvaluationContext is created.
  void SetCreateEvaluationCallbackTestOnly(
      std::function<void(EvaluationContext*)> cb);

  std::unique_ptr<internal::Evaluator> evaluator_;
};

// Represents modifications to multiple rows in a single table. Each row can
// have a different type of DML operation.
//
// Example:
//   PreparedModify statement(... build a PreparedModify ...)
//   GOOGLESQL_ASSIGN_OR_RETURN(
//     std::unique_ptr<EvaluatorTableModifyIterator> iter,
//     statement->Execute(parameters));
//   Table table = iter->table();
//   while (true) {
//     if (!iter->NextRow()) {
//       GOOGLESQL_RETURN_IF_ERROR(iter->Status());
//     }
//     ... Do something with `iter->GetOperation()`, `iter->GetColumnValue(...)`
//     and `iter->GetOriginalKeyValue()` ...
//   }
class EvaluatorTableModifyIterator {
 public:
  enum class Operation { kInsert, kDelete, kUpdate };

  virtual ~EvaluatorTableModifyIterator() = default;

  // The table to be modified. This is constant over all rows for this iterator.
  virtual const Table* table() const = 0;

  // Returns the type of DML operation on the current row.
  virtual Operation GetOperation() const = 0;

  // Returns the *modified* value of the i-th column of the current row.
  // - if GetOperation() == kInsert, the content of a new row to be inserted
  // - if GetOperation() == kDelete, always Value::Invalid()
  // - if GetOperation() == kUpdate, the new content for an existing row to be
  //   updated
  //
  // `i` must be a valid column index of table(), i.e. 0 <= i <
  // table()->NumColumns();
  //
  // NextRow() must have been called at least once and the last call must have
  // returned true.
  virtual const Value& GetColumnValue(int i) const = 0;

  // Returns the *original* value of the i-th key column of the current row.
  // This can be used to identify the modified row.
  //
  // `i` must be a valid index in table()->PrimaryKey(), i.e. 0 <= i <
  // table()->PrimaryKey()->size(). If the table doesn't specify PrimaryKey(),
  // then there will be no valid input to call this function.
  //
  // NextRow() must have been called at least once and the last call must have
  // returned true.
  virtual const Value& GetOriginalKeyValue(int i) const = 0;

  // Returns false if there is no next row. The caller must then check Status().
  virtual bool NextRow() = 0;

  // Returns OK unless the last call to NextRow() returned false because of an
  // error.
  virtual absl::Status Status() const = 0;
};

// Executes a DML statement and returns an EvaluatorTableModifyIterator.
// Currently, INSERT, DELETE, and UPDATE are supported.
// To support DML statements with THEN RETURN, an optional `returning_iterator`
// can be passed to the Execute* methods. This argument will get the THEN RETURN
// clause result or NULL when THEN RETURN clause is not present.
// If `returning_iterator` is not provided, these Execute* methods will continue
// to work, but the THEN RETURN clause results are ignored.
// Note that results are fully buffered before Execute returns, so the iterators
// can be consumed in arbitrary order.

// FEATURE_DISALLOW_PRIMARY_KEY_UPDATES is implicitly enabled because primary
// key updates are currently not supported.
//
// The underlying implementation is not optimized for performance.
class PreparedModifyBase {
 public:
  // Constructs using a ResolvedStatement directly. Does not take ownership of
  // `stmt`. `stmt` must outlive this object.
  //
  // The AST must validate successfully with ValidateResolvedStatement.
  // Otherwise, the program may crash in Prepare or Execute.
  //
  // In addition, the child ResolvedTableScan must include all columns in the
  // table (e.g. generated by the analyzer with
  // AnalyzerOptions::set_prune_unused_columns(false)).
  PreparedModifyBase(const ResolvedStatement* stmt,
                     const EvaluatorOptions& options);
  PreparedModifyBase(absl::string_view sql, const EvaluatorOptions& options);
  PreparedModifyBase(const PreparedModifyBase&) = delete;
  PreparedModifyBase& operator=(const PreparedModifyBase&) = delete;

  virtual ~PreparedModifyBase() = 0;

  // This method can optionally be called before Execute() to set analyzer
  // options and to return parsing and analysis errors, if any. If Prepare() is
  // used, the names and types of query parameters must be set in `options`. (We
  // also force `options.prune_unused_columns` since that would ideally be the
  // default.)
  //
  // If `catalog` is set, it will be used to resolve tables and functions
  // occurring in the query. Passing a custom `catalog` allows defining
  // user-defined functions with custom evaluation specified via
  // FunctionOptions, as well as user-defined tables (see the file comment for
  // details). Calling any user-defined function that does not provide an
  // evaluator returns an error. `catalog` must outlive Execute() and
  // output_type() calls.  `catalog` should contain GoogleSQL built-in functions
  // added by calling AddGoogleSQLFunctionsAndTypes with `options.language`.
  //
  // If a ResolvedStatement was already supplied to the PreparedModifyBase
  // constructor, `catalog` is ignored.
  absl::Status Prepare(const AnalyzerOptions& options,
                       Catalog* catalog = nullptr);

  // Executes the statement. This object must outlive the return value.
  //
  // If Prepare() has not been called, the first call to Execute will call
  // Prepare with implicitly constructed AnalyzerOptions using the
  // names and types from `parameters`.
  //
  // This method is thread safe. Multiple executions can proceed in parallel,
  // each using a different iterator.
  //
  // To support DML statements with THEN RETURN, an optional
  // `returning_iterator` can be passed to the Execute* methods. This argument
  // will get the THEN RETURN clause result or NULL when THEN RETURN clause is
  // not present. If `returning_iterator` is not provided, these Execute*
  // methods will continue to work, but the THEN RETURN clause results are
  // ignored. Note that results are fully buffered before Execute returns, so
  // the iterators can be consumed in arbitrary order.
  absl::StatusOr<std::unique_ptr<EvaluatorTableModifyIterator>> Execute(
      ParameterValueMap parameters = {},
      SystemVariableValuesMap system_variables = {},
      std::unique_ptr<EvaluatorTableIterator>* returning_iterator = nullptr);

  // Same as 'Execute', but uses positional instead of named parameters.
  absl::StatusOr<std::unique_ptr<EvaluatorTableModifyIterator>>
  ExecuteWithPositionalParams(
      ParameterValueList positional_parameters,
      SystemVariableValuesMap system_variables = {},
      std::unique_ptr<EvaluatorTableIterator>* returning_iterator = nullptr);

  // More efficient form of Execute that requires parameter values to be passed
  // in a particular order. If positional parameters are used, they are passed
  // in `parameters`. If named parameters are used, they are passed in
  // `parameters` in the order returned by GetReferencedParameters.
  //
  // REQUIRES: Prepare() has been called successfully.
  absl::StatusOr<std::unique_ptr<EvaluatorTableModifyIterator>>
  ExecuteAfterPrepareWithOrderedParams(
      ParameterValueList parameters,
      SystemVariableValuesMap system_variables = {},
      std::unique_ptr<EvaluatorTableIterator>* returning_iterator =
          nullptr) const;

  // This is the same as Execute, but is a const method, and requires that
  // Prepare has already been called. See the description of Execute for details
  // about the arguments and return value.
  absl::StatusOr<std::unique_ptr<EvaluatorTableModifyIterator>>
  ExecuteAfterPrepare(ParameterValueMap parameters,
                      SystemVariableValuesMap system_variables = {},
                      std::unique_ptr<EvaluatorTableIterator>*
                          returning_iterator = nullptr) const;

  // Returns a human-readable representation of how this statement would
  // actually be executed. Do not try to interpret this string with code, as the
  // format can change at any time. Requires that Prepare has already been
  // called.
  absl::StatusOr<std::string> ExplainAfterPrepare() const;

  // Get the list of parameters referenced in this statement.
  //
  // This method is similar to GetReferencedColumns(), but for parameters
  // instead. This returns the minimal set of parameters that must be provided
  // to Execute(). Named and positional parameters are mutually exclusive, so
  // this will return an empty list if GetPositionalParameterCount() returns a
  // non-zero number.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<std::vector<std::string>> GetReferencedParameters() const;

  // Gets the number of positional parameters in this statement.
  //
  // This returns the number of positional parameters that must be provided to
  // Execute(). Any extra positional parameters are ignored. Named and
  // positional parameters are mutually exclusive, so this will return 0 if
  // GetReferencedParameters() returns a non-empty list.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  absl::StatusOr<int> GetPositionalParameterCount() const;

  // Gets the resolved statement.
  //
  // REQUIRES: Prepare() or Execute() has been called successfully.
  const ResolvedStatement* resolved_statement() const;

 private:
  std::unique_ptr<internal::Evaluator> evaluator_;
};

// Represents DML statement results, which contains the row modifications and
// the THEN RETURN clause results, if present.
//
// It is used by the `Execute*` methods of `PreparedStatement`, where the return
// result can contain an `EvaluatorModifyResult`, if the statement is a DML
// statement or a multi-statement that contains a DML statement.
//
// PreparedModifyBase uses this internally but doesn't expose it.  The result
// iterators are returned directly from `Execute*` methods.
struct EvaluatorModifyResult {
  EvaluatorModifyResult() = default;

  EvaluatorModifyResult(
      std::unique_ptr<EvaluatorTableModifyIterator> table_modify_iter,
      std::unique_ptr<EvaluatorTableIterator> returning_table_iter)
      : table_modify_iter(std::move(table_modify_iter)),
        returning_table_iter(std::move(returning_table_iter)) {}

  // Represents modifications to the rows in a single table.
  std::unique_ptr<EvaluatorTableModifyIterator> table_modify_iter;

  // Represent the table result for THEN RETURN clauses. It is null if the DML
  // statement does not have a THEN RETURN clause.
  std::unique_ptr<EvaluatorTableIterator> returning_table_iter;
};

// Prepares and executes a SQL statement.  This API supports queries, DML, DDL,
// and multi-statements (see (broken link)).
//
// The Execute* methods return a vector of StmtResult, where each result
// is an error if the statement (or sub-statement in the multi-statement) fails,
// or an execution result. See the comment of `StmtResult` for more details.
//
// **Usage Example:**
//
// ```cc
//   const std::string sql = R"sql(
//     SELECT 1 AS x, 2 AS y
//     |> FORK (
//       |> SELECT x
//     ), (
//       |> INSERT INTO t(x, y)
//     ), (
//       |> CREATE TABLE MyNewTable
//     )
//   )sql";
//
//   SimpleCatalog catalog(...);
//   AnalyzerOptions options(...);
//   // ... set up catalog and options ...
//
//   PreparedStatementBase prepared_stmt(sql, EvaluatorOptions());
//   GOOGLESQL_RETURN_IF_ERROR(prepared_stmt.Prepare(options, &catalog));
//   GOOGLESQL_ASSIGN_OR_RETURN(PreparedStatementBase::StmtResults results,
//                    prepared_stmt.Execute());
//
//   // results.size() will be 3, one for each result output:
//   // 1. The `SELECT x FROM $fork_cte` statement for `|> SELECT x`.
//   // 2. The `INSERT INTO t(x, y) FROM $fork_cte` statement for
//   //    `|> INSERT INTO t(x, y)`.
//   // 3. The `CREATE TABLE MyNewTable FROM $fork_cte` statement for
//   //    `|> CREATE TABLE MyNewTable`.
//   //
//   // $fork_cte is a CTE defined for the pipe input, corresponding to the
//   // `SELECT 1 AS x, 2 AS y` query.
//
//   // Iterate through the results:
//
//   for (const auto& result : results) {
//     if (!result.ok()) {
//       // ... handle error ...
//       continue;
//     }
//     switch (result->kind) {
//       case PreparedStatementBase::StmtKind::kQuery:
//         GOOGLESQL_RET_CHECK(result.table_iterator != nullptr);
//         // Access `table_iterator` to get back the results.
//         ...
//         break;
//       case PreparedStatementBase::StmtKind::kDML:
//         // Access `modify_result` to get back the modifications.
//         GOOGLESQL_RET_CHECK(result.modify_result.table_modify_iter != nullptr);
//         ...
//         if (result.modify_result.returning_table_iter != nullptr) {
//           // ... process the THEN RETURN clause result ...
//         }
//         break;
//       case PreparedStatementBase::StmtKind::kCTAS:
//         GOOGLESQL_RET_CHECK(result.table_iterator != nullptr);
//         // Access `table_iterator` to get back the contents of the table.
//         ...
//         break;
//     }
//   }
// ```
//
// **Execution Semantics for Multi-statements:**
// - All statements in a multi-statement query operate on the same initial
// snapshot of the database.
// - The side effects of one statement (e.g., from DML or DDL) are NOT visible
// to subsequent statements within the same `Execute` call.
//
// TODO: Currently the evaluation of multi-statements uses `ValueExpr`s instead
// `RelationalOp`s, meaning it loads all the rows into memory directly.
// We should update the algebrizer to produce `RelationalOp`s for multi-stmts
// to support more efficient evaluation.
class PreparedStatementBase {
 public:
  enum class StmtKind {
    // A query statement.
    kQuery,
    // A CREATE TABLE AS SELECT statement.
    kCTAS,
    // A DML statement, e.g. INSERT, UPDATE, DELETE.
    kDML,
  };

  struct StmtResult {
    // The `kind` field indicates the type of result:
    // 1. `kQuery`: A query statement that returns a result set.
    //   `table_iterator` is populated.
    // 2. `kCTAS`: A `CREATE TABLE AS SELECT` statement. `table_iterator` is
    //   populated with the contents of the new table. The metadata information,
    //   including table name and schema, is stored in the resolved AST,
    //   accessible via `statement`, which is a
    //   `ResolvedCreateTableAsSelectStmt`.
    // 3. `kDML`: A DML statement. `modify_result` is populated with a
    //   description of the row modifications. If the DML statement also has a
    //   `THEN RETURN` clause, `table_iterator` will be populated with the
    //   returned rows.
    StmtKind kind;

    // Populated if the statement returns a table, for example a
    // `ResolvedQueryStmt`.
    std::unique_ptr<EvaluatorTableIterator> table_iterator;

    // Populated if the statement is a DML statement.
    EvaluatorModifyResult modify_result;

    // The resolved statement for this result.
    const ResolvedStatement* statement;
  };

  using StmtResults = std::vector<absl::StatusOr<StmtResult>>;

  PreparedStatementBase(absl::string_view sql, const EvaluatorOptions& options);

  // Note: This API does not support `ResolvedGeneralizedQueryStmt` directly.
  // It assumes those were rewritten to `ResolvedMultiStmt` already using the
  // REWRITE_GENERALIZED_QUERY_STMT rewriter (which is enabled in the default
  // analyzer options). See (broken link).
  PreparedStatementBase(const ResolvedStatement* stmt,
                        const EvaluatorOptions& options);

  PreparedStatementBase(const PreparedStatementBase&) = delete;
  PreparedStatementBase& operator=(const PreparedStatementBase&) = delete;

  virtual ~PreparedStatementBase() = 0;

  // This method can optionally be called before Execute() to set analyzer
  // options and to return parsing and analysis errors, if any.
  absl::Status Prepare(const AnalyzerOptions& options,
                       Catalog* catalog = nullptr);

  // Executes the multi-statement query.
  //
  // If Prepare() has not been called, the first call to Execute will call
  // Prepare with implicitly constructed AnalyzerOptions.
  absl::StatusOr<StmtResults> Execute(QueryOptions options = QueryOptions());

  // Same as 'Execute', but uses positional instead of named parameters.
  absl::StatusOr<StmtResults> ExecuteWithPositionalParams(
      ParameterValueList positional_parameters,
      SystemVariableValuesMap system_variables = {});

  // This is the same as Execute, but is a const method, and requires that
  // Prepare has already been called.
  absl::StatusOr<StmtResults> ExecuteAfterPrepare(
      QueryOptions options = QueryOptions()) const;

  // Same as 'ExecuteAfterPrepare', but uses positional instead of named
  // parameters.
  absl::StatusOr<StmtResults> ExecuteAfterPrepareWithPositionalParams(
      ParameterValueList positional_parameters,
      SystemVariableValuesMap system_variables = {}) const;

  // Returns a human-readable representation of how this multi-statement query
  // would actually be executed.
  absl::StatusOr<std::string> ExplainAfterPrepare() const;

  // Gets the resolved statement.
  const ResolvedStatement* resolved_statement() const;

 private:
  std::unique_ptr<internal::Evaluator> evaluator_;
  std::vector<const ResolvedStatement*> sub_statements_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_EVALUATOR_BASE_H_
