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

#ifndef GOOGLESQL_REFERENCE_IMPL_STATEMENT_EVALUATOR_H_
#define GOOGLESQL_REFERENCE_IMPL_STATEMENT_EVALUATOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/scripting/parsed_script.h"
#include "googlesql/scripting/script_executor.h"
#include "googlesql/scripting/script_segment.h"
#include "googlesql/scripting/type_aliases.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Invoked from ExecuteStatement()/EvaluateScalarExpression() for all calls to
// ExecuteStatement()/EvaluateExpression(), on both success and failure.
//
// In the event of an error, <status_or_result> will have a failed status and
// <resolved_stmt> may be null, depending on how far the analysis got, prior to
// the error.
//
// If status_or_result is ok, the resolved tree is non-null.
//
// The implementation may override the methods here to log or process the
// results.
class StatementEvaluatorCallback {
 public:
  explicit StatementEvaluatorCallback(int64_t bytes_per_iterator)
      : bytes_per_iterator_(bytes_per_iterator) {}

  virtual ~StatementEvaluatorCallback() = default;

  // Invoked on an attempted statement evaluation, whether successful or not.
  virtual void OnStatementResult(
      const ScriptSegment& segment, const ResolvedStatement* resolved_stmt,
      const absl::StatusOr<Value>& status_or_result) {}

  // Invoked on an attempted multi-statement evaluation, whether each
  // sub-statement is successful or not.
  virtual void OnMultiStatementResult(
      const ScriptSegment& segment, const ResolvedStatement* resolved_stmt,
      const std::vector<absl::StatusOr<Value>>& status_or_results) {}

  // Invoked on an attempted expression evaluation, whether successful or not.
  virtual void OnScalarExpressionResult(
      const ScriptSegment& segment, const ResolvedExpr* resolved_expr,
      const absl::StatusOr<Value>& status_or_result) {}

  // Invoked on an attempt to resolve a type name, whether successful or not.
  virtual void OnTypeResult(const ScriptSegment& segment,
                            absl::StatusOr<const Type*> status_or_type) {}

  // Invoked on an attempt to check if variable type is supported.
  // This method may return false to disallow certain variable types.
  virtual bool IsSupportedVariableType(const Type* type) { return true; }

  // Replaces all data in <table> with <rows>.  Each element in <rows>
  // contains one sub-element per column.
  virtual absl::Status SetTableContents(
      const Table* table, const std::vector<std::vector<Value>>& rows) {
    return googlesql_base::UnimplementedErrorBuilder() << "DML not supported.";
  }

  // Invoked on an attempt to load a procedure.
  virtual absl::StatusOr<std::unique_ptr<ProcedureDefinition>> LoadProcedure(
      const absl::Span<const std::string>& path) {
    return absl::NotFoundError("");
  }

  int64_t get_bytes_per_iterator() { return bytes_per_iterator_; }

 private:
  // Fake memory bytes that an iterator takes up, to be returned by
  // StatementEvaluatorImpl::GetIteratorMemoryUsage().
  // Default 0 if not set by test option.
  const int64_t bytes_per_iterator_;
};

class StatementEvaluatorImpl : public StatementEvaluator {
 public:
  // <type_factory>, and <catalog> are mandatory.
  // <callback> is optional.
  //
  // All pointer parameters must remain alive for the duration of the
  // StatementEvaluatorImpl object.
  //
  // The following caveats apply to fields set in <evaluator_options>:
  //   - Queries are run using the timezone in the AnalyzerOptions provided by
  //       the script executor.  The <default_time_zone> field of
  //       EvaluatorOptions is ignored.
  //   - For DML statements, all fields of <evaluator_options> are ignored,
  //       except for <clock>.
  StatementEvaluatorImpl(
      const AnalyzerOptions& initial_analyzer_options,
      const EvaluatorOptions& evaluator_options,
      std::variant<ParameterValueList, ParameterValueMap> parameters,
      TypeFactory* type_factory, Catalog* catalog,
      StatementEvaluatorCallback* callback)
      : initial_analyzer_options_(initial_analyzer_options),
        options_(evaluator_options),
        parameters_(parameters),
        type_factory_(type_factory),
        catalog_(catalog),
        callback_(callback),
        catalog_for_temp_objects_(nullptr) {}

  // If support for DDL statements like CREATE FUNCTION is needed, this function
  // must be called with a catalog that will be used to store temp objects (such
  // as those created within a script need to persist for the duration of the
  // script execution. If not set, statements that create temp objects will
  // fail.
  void EnableCreationOfTempObjects(SimpleCatalog* catalog_for_temp_objects) {
    catalog_for_temp_objects_ = catalog_for_temp_objects;
  }

  absl::Status ExecuteStatement(const ScriptExecutor& executor,
                                const ScriptSegment& segment) override;

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  ExecuteQueryWithResult(const ScriptExecutor& executor,
                         const ScriptSegment& segment) override;

  absl::Status SerializeIterator(const EvaluatorTableIterator& iterator,
                                 google::protobuf::Any& out) override;

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> DeserializeToIterator(
      const google::protobuf::Any& msg, const ScriptExecutor& executor,
      const ParsedScript& parsed_script) override;

  absl::StatusOr<int64_t> GetIteratorMemoryUsage(
      const EvaluatorTableIterator& iterator) override;

  absl::StatusOr<int> EvaluateCaseExpression(
      const ScriptSegment& case_value,
      const std::vector<ScriptSegment>& when_values,
      const ScriptExecutor& executor) override;

  absl::StatusOr<Value> EvaluateScalarExpression(
      const ScriptExecutor& executor, const ScriptSegment& segment,
      const Type* target_type) override;

  absl::StatusOr<TypeWithParameters> ResolveTypeName(
      const ScriptExecutor& executor, const ScriptSegment& segment) override;

  bool IsSupportedVariableType(
      const TypeWithParameters& type_with_params) override;

  absl::Status ApplyTypeParameterConstraints(const TypeParameters& type_params,
                                             Value* value) override;

  absl::StatusOr<std::unique_ptr<ProcedureDefinition>> LoadProcedure(
      const ScriptExecutor& executor, const absl::Span<const std::string>& path,
      int64_t num_arguments) override;

  TypeFactory* type_factory() { return type_factory_; }
  const AnalyzerOptions& initial_analyzer_options() const {
    return initial_analyzer_options_;
  }
  const EvaluatorOptions& options() const { return options_; }
  const std::variant<ParameterValueList, ParameterValueMap>& parameters()
      const {
    return parameters_;
  }

 private:
  // Represents the evaluation of a single statement or expression, using
  // derived classes StatementEvaluation and ExpressionEvaluation, respectively.
  //
  // Makes the resolved tree available to consumers after an evaluation, even in
  // case of failure, if the evaluation advanced far enough to generate the
  // tree.
  class Evaluation {
   public:
    virtual ~Evaluation() = default;

    TypeFactory* type_factory() const { return evaluator_->type_factory(); }

    // Evaluates a single expression or statement.
    // Stores the analyzer output if the evaluation advances far enough to
    // generate them.
    //
    // Evaluate() may only be called once per Evaluation object.
    absl::Status Evaluate(const ScriptExecutor& script_executor,
                          StatementEvaluatorImpl* evaluator,
                          const ScriptSegment& segment);

   protected:
    // Evaluates the statement or expression of text <sql>, storing the result
    // internally.
    //
    // <catalog> represents an externally-owned MultiCatalog including both
    // user-defined tables and script variables.
    virtual absl::Status EvaluateImpl(
        absl::string_view sql, const AnalyzerOptions& analyzer_options,
        Catalog* catalog, const EvaluatorOptions& evaluator_options,
        const SystemVariableValuesMap& system_variables,
        std::variant<ParameterValueList, ParameterValueMap> parameters) = 0;

    absl::Status SetTableContents(const Table* table,
                                  const std::vector<std::vector<Value>>& rows) {
      return evaluator_->callback_->SetTableContents(table, rows);
    }

    StatementEvaluatorImpl* evaluator() const { return evaluator_; }

   private:
    absl::Status EvaluateInternal(const ScriptExecutor& script_executor,
                                  StatementEvaluatorImpl* evaluator,
                                  const ScriptSegment& segment);

    // Set during Evaluate().
    StatementEvaluatorImpl* evaluator_ = nullptr;
    // Used to maintain script variables. This is recreated for every
    // evaluation, and is maintained within the Evaluation object to ensure that
    // the lifetime of the variables is bound to that of the evaluation itself.
    std::unique_ptr<SimpleCatalog> variables_catalog_;
  };

  class StatementEvaluation : public Evaluation {
   public:
    const ResolvedStatement* resolved_statement() const;
    const std::vector<absl::StatusOr<Value>>& results() const {
      return results_;
    }
    // Returns the table iterator from the last executed query statement.
    // The returned iterator must not outlive this object.
    std::unique_ptr<EvaluatorTableIterator> get_table_iterator() {
      return std::move(table_iterator_);
    }
    std::unique_ptr<PreparedStatement> get_prepared_statement() {
      return std::move(prepared_statement_);
    }

   protected:
    absl::Status EvaluateImpl(
        absl::string_view sql, const AnalyzerOptions& analyzer_options,
        Catalog* catalog, const EvaluatorOptions& evaluator_options,
        const SystemVariableValuesMap& system_variables,
        std::variant<ParameterValueList, ParameterValueMap> parameters)
        override;

   private:
    absl::Status Analyze(absl::string_view sql,
                         const AnalyzerOptions& analyzer_options,
                         Catalog* catalog);

    // Evaluates a statement using a PreparedStatement. This handles single
    // statements (queries, DML, CTAS) and multi-statement queries. The results
    // are stored in the `results_` member variable.
    absl::Status EvaluateWithPreparedStatement(
        const AnalyzerOptions& analyzer_options,
        const EvaluatorOptions& evaluator_options,
        const SystemVariableValuesMap& system_variables,
        std::variant<ParameterValueList, ParameterValueMap> parameters);

    // Evaluates the query statement stored prepared by `prepared_statement_`.
    // This is a special case because it needs to
    // produce both a result Value and a table iterator for streaming. It
    // currently executes the query twice to achieve this (not ideal).
    absl::StatusOr<Value> EvaluateQueryStatement(
        const QueryOptions& query_options);

    // Converts the raw StmtResult into a final Value and handles
    // side-effects for DDL and DML statements.
    absl::StatusOr<Value> ProcessSingleStmtResult(
        PreparedStatement::StmtResult stmt_result,
        const EvaluatorOptions& evaluator_options);

    // Invoked after executing a DML statement; updates the modified table to
    // reflect the result. Returns the total number of rows inserted, modified,
    // or removed.
    absl::StatusOr<int> DoDmlSideEffects(
        EvaluatorTableModifyIterator* iterator);

    // Invoked for handling a DDL statement. Creates temp objects and adds them
    // to the catalog for temp objects.
    //
    // The optional `result` parameter should be passed in if applying the
    // side-effects requires evaluating the statement, such as CTAS statements.
    absl::StatusOr<Value> ProcessDdlStatement(
        const ResolvedStatement* statement,
        const EvaluatorOptions& evaluator_options,
        std::optional<PreparedStatement::StmtResult> result);

    // Set from Execute().
    //
    // `results_` is empty if the statement fails at analysis-time.
    //
    // If the statement is executed,
    // - For multi-statement, it contains the results of the sub-statements.
    // - For single statement, it contains one element.
    std::vector<absl::StatusOr<Value>> results_;

    // Set from Execute(), must be kept alive for table_iterator_ to remain
    // valid.
    std::unique_ptr<PreparedStatement> prepared_statement_;

    // Set from Execute().
    std::unique_ptr<EvaluatorTableIterator> table_iterator_;

    // Stores the analyzer output for the statement.
    std::unique_ptr<const AnalyzerOutput> analyzer_output_;

    // This is stored separately from analyzer_output_ because analyzer_output_
    // is moved when creating temp objects.
    const ResolvedStatement* resolved_statement_ = nullptr;
  };

  class ExpressionEvaluation : public Evaluation {
   public:
    explicit ExpressionEvaluation(const Type* target_type)
        : target_type_(target_type) {}

    const ResolvedExpr* resolved_expr() const;
    const Value& result() const { return result_; }

   protected:
    absl::Status EvaluateImpl(
        absl::string_view sql, const AnalyzerOptions& analyzer_options,
        Catalog* catalog, const EvaluatorOptions& evaluator_options,
        const SystemVariableValuesMap& system_variables,
        std::variant<ParameterValueList, ParameterValueMap> parameters)
        override;

   private:
    const Type* target_type_;

    std::unique_ptr<const AnalyzerOutput> analyzer_output_;

    // Set from Execute().
    Value result_;
  };

 private:
  // Analyzer artifacts for temp objects created within a script need to be kept
  // alive for the duration of the script execution. This function is called by
  // StatementEvaluation whenever a new temp object is created. It takes over
  // ownership of the AnalyzerOutput and keeps it alive in the scope of the
  // evaluator.
  void TakeOwnership(std::unique_ptr<const AnalyzerOutput> analyzer_output) {
    analyzer_artifacts_.push_back(std::move(analyzer_output));
  }

  SimpleCatalog* /*absl_nullable*/ catalog_for_temp_objects() const {
    return catalog_for_temp_objects_;
  }

  // AnalyzerOptions, excluding those, such as system variables, that are set by
  // the execution of the script.
  const AnalyzerOptions initial_analyzer_options_;

  EvaluatorOptions options_;
  const std::variant<ParameterValueList, ParameterValueMap> parameters_;
  TypeFactory* type_factory_;
  Catalog* catalog_;
  StatementEvaluatorCallback* callback_;

  // Stores temporary objects created within a SQL script that need to persist
  // for the duration of the script execution.
  SimpleCatalog* /*absl_nullable*/ catalog_for_temp_objects_;

  // These are used to keep analysis artifacts alive for temp objects created
  // within a script.
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_artifacts_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_STATEMENT_EVALUATOR_H_
