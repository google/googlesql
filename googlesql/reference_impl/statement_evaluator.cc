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

#include "googlesql/reference_impl/statement_evaluator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/script_exception.pb.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/sql_procedure.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/reference_impl/evaluator_table_iterator.pb.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/reference_impl/type_parameter_constraints.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/scripting/error_helpers.h"
#include "googlesql/scripting/parsed_script.h"
#include "googlesql/scripting/script_executor.h"
#include "googlesql/scripting/script_segment.h"
#include "googlesql/scripting/type_aliases.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
// Populates <filter_parameters> and only includes values from <parameters> that
// exist in <range>.
void FilterParameters(
    std::variant<ParameterValueList, ParameterValueMap> parameters,
    const ParsedScript* parsed_script, const ParseLocationRange& range,
    std::variant<ParameterValueList, ParameterValueMap>* filtered_parameters) {
  if (std::holds_alternative<ParameterValueList>(parameters)) {
    const ParameterValueList& current_params =
        std::get<ParameterValueList>(parameters);
    PositionalParameterRange pos_params =
        parsed_script->GetPositionalParameters(range);
    int64_t start = pos_params.start_param_index;
    int64_t end = start + pos_params.num_params;
    ParameterValueList filtered;
    for (int64_t i = start; i < current_params.size() && i < end; i++) {
      filtered.push_back(current_params[i]);
    }
    *filtered_parameters = filtered;
  } else {
    const ParameterValueMap& current_params =
        std::get<ParameterValueMap>(parameters);
    ParsedScript::StringSet named_params =
        parsed_script->GetNamedParameters(range);
    ParameterValueMap filtered;
    for (absl::string_view name : named_params) {
      std::string lower_name = absl::AsciiStrToLower(name);
      auto itr = current_params.find(lower_name);
      if (itr != current_params.end()) {
        filtered.insert({itr->first, itr->second});
      }
    }
    *filtered_parameters = filtered;
  }
}

// This class stores the following data which are used
// to serialize/deserialize the iterator along with the rest of
// ScriptExecutor state:
// - ParseLocationPoint of the query node that invoked the creation of this
//   iterator
// - number of times NextRow() was called
class EvaluatorTableIteratorWrapper : public EvaluatorTableIterator {
 public:
  EvaluatorTableIteratorWrapper(
      std::unique_ptr<PreparedStatement> prepared_statement,
      std::unique_ptr<EvaluatorTableIterator> iterator,
      const ParseLocationPoint& location)
      : prepared_statement_(std::move(prepared_statement)),
        iterator_(std::move(iterator)),
        location_(location) {}

  EvaluatorTableIteratorWrapper(const EvaluatorTableIteratorWrapper&) = delete;
  EvaluatorTableIteratorWrapper& operator=(
      const EvaluatorTableIteratorWrapper&) = delete;
  ~EvaluatorTableIteratorWrapper() override = default;

  int NumColumns() const override { return iterator_->NumColumns(); }
  absl::Status Status() const override { return iterator_->Status(); }
  absl::Status Cancel() override { return iterator_->Cancel(); }
  void SetDeadline(absl::Time deadline) override {
    iterator_->SetDeadline(deadline);
  }
  std::string GetColumnName(int i) const override {
    return iterator_->GetColumnName(i);
  }
  const Type* GetColumnType(int i) const override {
    return iterator_->GetColumnType(i);
  }
  absl::Status SetColumnFilterMap(
      absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map)
      override {
    return iterator_->SetColumnFilterMap(std::move(filter_map));
  }
  absl::Status SetReadTime(absl::Time read_time) override {
    return iterator_->SetReadTime(read_time);
  }
  const Value& GetValue(int i) const override { return iterator_->GetValue(i); }

  bool NextRow() override {
    next_row_count_++;
    return iterator_->NextRow();
  }

  const ParseLocationPoint& get_location() const { return location_; }
  int get_next_row_count() const { return next_row_count_; }

 private:
  // Set from ExecuteQueryWithResult(), must be kept alive for iterator_
  // to remain valid.
  std::unique_ptr<PreparedStatement> prepared_statement_;
  std::unique_ptr<EvaluatorTableIterator> iterator_;
  const ParseLocationPoint location_;
  int next_row_count_ = 0;
};
}  // namespace

const ResolvedStatement*
StatementEvaluatorImpl::StatementEvaluation::resolved_statement() const {
  return resolved_statement_;
}

const ResolvedExpr*
StatementEvaluatorImpl::ExpressionEvaluation::resolved_expr() const {
  if (analyzer_output_ == nullptr) {
    return nullptr;
  }
  return analyzer_output_->resolved_expr();
}

absl::Status StatementEvaluatorImpl::Evaluation::Evaluate(
    const ScriptExecutor& script_executor, StatementEvaluatorImpl* evaluator,
    const ScriptSegment& segment) {
  GOOGLESQL_RETURN_IF_ERROR(EvaluateInternal(script_executor, evaluator, segment))
      .With(ConvertLocalErrorToScriptError(segment));
  return absl::OkStatus();
}

absl::Status StatementEvaluatorImpl::Evaluation::EvaluateInternal(
    const ScriptExecutor& script_executor, StatementEvaluatorImpl* evaluator,
    const ScriptSegment& segment) {
  ABSL_CHECK(evaluator != nullptr);
  ABSL_CHECK(evaluator_ == nullptr) << "StatementEvaluatorImpl::Evaluation::"
                                  "Evaluate() called multiple times.";
  evaluator_ = evaluator;
  AnalyzerOptions analyzer_options = evaluator->initial_analyzer_options_;
  GOOGLESQL_RETURN_IF_ERROR(script_executor.UpdateAnalyzerOptions(analyzer_options));

  const SystemVariableValuesMap& system_variables =
      script_executor.GetKnownSystemVariables();
  analyzer_options.CreateDefaultArenasIfNotSet();

  // Create a catalog which supports script variables, alongside whatever
  // user-defined symbols have been provided when the StatementEvaluatorImpl
  // object was created.
  ABSL_DCHECK(variables_catalog_ == nullptr);
  variables_catalog_ = std::make_unique<SimpleCatalog>(
      "script_variables", evaluator->type_factory());
  const VariableMap& variables = script_executor.GetCurrentVariables();
  for (const std::pair<const IdString, Value>& variable : variables) {
    std::unique_ptr<SimpleConstant> constant;
    GOOGLESQL_RETURN_IF_ERROR(SimpleConstant::Create({variable.first.ToString()},
                                           variable.second, &constant));
    variables_catalog_->AddOwnedConstant(std::move(constant));
  }

  std::unique_ptr<MultiCatalog> combined_catalog;
  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create(
      "combined_catalog", {variables_catalog_.get(), evaluator->catalog_},
      &combined_catalog));

  // Force usage of ERROR_MESSAGE_WITH_PAYLOAD so that the script executor can
  // fill in the context of the error, relative to the entire script.
  analyzer_options.set_error_message_mode(ERROR_MESSAGE_WITH_PAYLOAD);

  EvaluatorOptions evaluator_options = evaluator_->options();
  evaluator_options.default_time_zone = analyzer_options.default_time_zone();

  std::variant<ParameterValueList, ParameterValueMap> filtered_parameters;
  FilterParameters(script_executor.GetCurrentParameterValues().value_or(
                       evaluator->parameters()),
                   script_executor.GetCurrentStackFrame()->parsed_script(),
                   segment.range(), &filtered_parameters);
  return EvaluateImpl(segment.GetSegmentText(), analyzer_options,
                      combined_catalog.get(), evaluator_options,
                      system_variables, filtered_parameters);
}

absl::Status StatementEvaluatorImpl::StatementEvaluation::Analyze(
    absl::string_view sql, const AnalyzerOptions& analyzer_options,
    Catalog* catalog) {
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(sql, analyzer_options, catalog,
                                   type_factory(), &analyzer_output_));
  if (analyzer_output_->analyzer_output_properties().has_anonymization) {
    GOOGLESQL_ASSIGN_OR_RETURN(analyzer_output_,
                     RewriteForAnonymization(analyzer_output_, analyzer_options,
                                             catalog, type_factory()));
  }

  resolved_statement_ = analyzer_output_->resolved_statement();
  return absl::OkStatus();
}

namespace {
// Returns the type of a Value that would contain the table whose values are
// provided in <iterator>.
absl::StatusOr<const ArrayType*> GetTableType(
    TypeFactory* type_factory, const EvaluatorTableIterator* iterator,
    bool is_value_table) {
  if (is_value_table) {
    if (iterator->NumColumns() != 1) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "is_value_table specified, but table has "
             << iterator->NumColumns() << " instead of 1";
    }
    const ArrayType* result;
    GOOGLESQL_RETURN_IF_ERROR(
        type_factory->MakeArrayType(iterator->GetColumnType(0), &result));
    return result;
  } else {
    std::vector<StructField> fields;
    fields.reserve(iterator->NumColumns());
    for (int i = 0; i < iterator->NumColumns(); ++i) {
      fields.emplace_back(iterator->GetColumnName(i),
                          iterator->GetColumnType(i));
    }
    const StructType* elem_type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(fields, &elem_type));

    const ArrayType* result;
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(elem_type, &result));
    return result;
  }
}

// Returns a Value which holds the tabledata described in the given iterator:
// - The returned value is an array, with one element per row in the table.
// - If <is_value_table> is true, the table must contain exactly one column.
//      Each element of the array corresponds to a value in the table.
// - Otherwise, each element of the array is a struct, with one field per
//      column.  Each field is the value of the corresponding row/column.
absl::StatusOr<Value> IteratorToValue(TypeFactory* type_factory,
                                      EvaluatorTableIterator* iterator,
                                      bool is_value_table) {
  GOOGLESQL_ASSIGN_OR_RETURN(const ArrayType* type,
                   GetTableType(type_factory, iterator, is_value_table));
  std::vector<Value> values;
  while (iterator->NextRow()) {
    if (is_value_table) {
      ABSL_CHECK_EQ(iterator->NumColumns(), 1);
      values.push_back(iterator->GetValue(0));
      GOOGLESQL_RET_CHECK(iterator->GetValue(0).is_valid());
    } else {
      std::vector<Value> fields;
      fields.reserve(iterator->NumColumns());
      for (int i = 0; i < iterator->NumColumns(); ++i) {
        GOOGLESQL_RET_CHECK(iterator->GetValue(i).is_valid());
        fields.push_back(iterator->GetValue(i));
      }
      values.push_back(Value::UnsafeStruct(
          type->AsArray()->element_type()->AsStruct(), std::move(fields)));
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(iterator->Status());
  return Value::UnsafeArray(type, std::move(values));
}

absl::StatusOr<Value> GetTableContents(const Table* table,
                                       TypeFactory* type_factory) {
  std::vector<int> column_idxs;
  column_idxs.reserve(table->NumColumns());
  for (int i = 0; i < table->NumColumns(); ++i) {
    column_idxs.push_back(i);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<EvaluatorTableIterator> iter,
                   table->CreateEvaluatorTableIterator(column_idxs));
  return IteratorToValue(type_factory, iter.get(), table->IsValueTable());
}

}  // namespace

absl::StatusOr<int>
StatementEvaluatorImpl::StatementEvaluation::DoDmlSideEffects(
    EvaluatorTableModifyIterator* iterator) {
  // Obtain original table, as a vector of rows
  const SimpleTable* table = iterator->table()->GetAs<SimpleTable>();
  TypeFactory type_factory;
  GOOGLESQL_ASSIGN_OR_RETURN(Value original_table_value,
                   GetTableContents(table, &type_factory));
  std::vector<std::vector<Value>> rows;
  for (const Value& row : original_table_value.elements()) {
    if (table->IsValueTable()) {
      rows.push_back({row});
    } else {
      rows.push_back(row.fields());
    }
  }
  // Build up indexes to associate each row with its primary key
  if (!table->PrimaryKey().has_value()) {
    return absl::InvalidArgumentError(
        "GetModifiedTable: table must have primary key");
  }
  std::vector<int> primary_key_column_idx = table->PrimaryKey().value();
  absl::flat_hash_map<std::vector<Value>, int> row_idxs_by_key;
  for (int i = 0; i < rows.size(); ++i) {
    std::vector<Value> key;
    key.reserve(primary_key_column_idx.size());
    for (int idx : primary_key_column_idx) {
      key.push_back(rows[i][idx]);
    }
    row_idxs_by_key[key] = i;
  }
  absl::flat_hash_set<int> deleted_rows;

  bool is_insert = iterator->GetOperation() ==
                   EvaluatorTableModifyIterator::Operation::kInsert;
  bool is_update = iterator->GetOperation() ==
                   EvaluatorTableModifyIterator::Operation::kUpdate;
  bool is_delete = iterator->GetOperation() ==
                   EvaluatorTableModifyIterator::Operation::kDelete;
  int num_rows_modified = 0;
  while (iterator->NextRow()) {
    ++num_rows_modified;
    std::vector<Value> key;
    for (int i = 0; i < table->PrimaryKey().value().size(); ++i) {
      // Even if the evaluation of an INSERT statement succeeds, it is still
      // possible that its side-effect can't be applied if it is a sub-statement
      // in a multi-stmt.
      //
      // For example, consider the following multi-stmt query:
      //
      // <query>
      // |> FORK (
      //   |> INSERT INTO t
      // ), (
      //   |> INSERT INTO t
      // )
      //
      // The evaluation of both the INSERT can succeed, but only one should
      // can be applied successfully.
      //
      // As a result, we use GetColumnValue() instead of GetOriginalKeyValue()
      // to get the key value of the current INSERT statement, and check if
      // it whether it exists in the table already.
      Value value;
      if (is_insert) {
        value = iterator->GetColumnValue(i);
      } else {
        value = iterator->GetOriginalKeyValue(i);
      }
      key.push_back(value);
    }
    auto row_it = row_idxs_by_key.find(key);
    bool row_found = row_it != row_idxs_by_key.end();

    // The error code for update and delete is "InternalError" but it is
    // "OutOfRangeError" for insert. This is because currently only INSERT
    // can be used in a multi-stmt, in which case it is possible that the
    // side effect evaluation succeeds for multiple sub-statements but only one
    // can be applied.
    if ((is_update || is_delete) && !row_found) {
      return absl::InternalError(
          "Unable to find primary key for row in UPDATE/DELETE operation");
    }
    if (is_insert && row_found) {
      return absl::OutOfRangeError("Key already exists for INSERT operation");
    }

    if (is_delete) {
      deleted_rows.insert(row_it->second);
      continue;
    }

    std::vector<Value> columns;
    for (int i = 0; i < table->NumColumns(); ++i) {
      GOOGLESQL_RET_CHECK(iterator->GetColumnValue(i).is_valid());
      columns.push_back(iterator->GetColumnValue(i));
    }
    if (is_update) {
      rows[row_it->second] = std::move(columns);
    } else {
      rows.push_back(std::move(columns));
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(iterator->Status());

  if (is_delete) {
    // Final pass to remove rows that have been deleted
    std::vector<std::vector<Value>> remaining_rows;
    remaining_rows.reserve(rows.size() - deleted_rows.size());
    for (int i = 0; i < rows.size(); ++i) {
      if (!deleted_rows.contains(i)) {
        remaining_rows.push_back(std::move(rows[i]));
      }
    }
    GOOGLESQL_RETURN_IF_ERROR(SetTableContents(table, remaining_rows));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(SetTableContents(table, rows));
  }
  return num_rows_modified;
}

absl::StatusOr<Value>
StatementEvaluatorImpl::StatementEvaluation::ProcessDdlStatement(
    const ResolvedStatement* statement,
    const EvaluatorOptions& evaluator_options,
    std::optional<PreparedStatement::StmtResult> result) {
  if (evaluator()->catalog_for_temp_objects() == nullptr) {
    return googlesql_base::FailedPreconditionErrorBuilder()
           << "DDL statements require the catalog for temp objects to be set "
              "by calling EnableCreationOfTempObjects() first";
  }

  if (statement->node_kind() == RESOLVED_CREATE_TABLE_AS_SELECT_STMT) {
    GOOGLESQL_RET_CHECK(result.has_value());
    const auto* ctas_stmt = statement->GetAs<ResolvedCreateTableAsSelectStmt>();
    if (ctas_stmt->create_mode() !=
        ResolvedCreateTableAsSelectStmt::CREATE_DEFAULT) {
      return absl::UnimplementedError(
          "Only the default create mode is supported");
    }
    if (ctas_stmt->create_scope() !=
        ResolvedCreateTableAsSelectStmt::CREATE_TEMP) {
      return absl::UnimplementedError(
          "Only the temp create scope is supported");
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleTable> table,
                     MakeTableFromCreateTable(*ctas_stmt));

    std::vector<std::vector<Value>> rows;
    if (result->table_iterator != nullptr) {
      while (result->table_iterator->NextRow()) {
        std::vector<Value> row;
        if (ctas_stmt->is_value_table()) {
          row.push_back(result->table_iterator->GetValue(0));
        } else {
          for (int i = 0; i < result->table_iterator->NumColumns(); ++i) {
            row.push_back(result->table_iterator->GetValue(i));
          }
        }
        rows.push_back(std::move(row));
      }
      GOOGLESQL_RETURN_IF_ERROR(result->table_iterator->Status());
    }
    table->SetContents(rows);

    const std::string table_name = table->Name();

    if (!evaluator()->catalog_for_temp_objects()->AddOwnedTableIfNotPresent(
            table_name, std::move(table))) {
      return googlesql_base::OutOfRangeErrorBuilder()
             << "Table " << table_name << " already exists";
    }

    const StructType* struct_type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory()->MakeStructType(
        {{kCreatedObjectType, type_factory()->get_string()},
         {kCreatedObjectName, type_factory()->get_string()}},
        &struct_type));
    return Value::MakeStruct(
        struct_type, {Value::String("Temp Table"), Value::String(table_name)});
  }

  if (statement->node_kind() == RESOLVED_CREATE_CONSTANT_STMT) {
    const ResolvedCreateConstantStmt* stmt =
        statement->GetAs<ResolvedCreateConstantStmt>();
    GOOGLESQL_ASSIGN_OR_RETURN(auto constant, MakeConstantFromCreateConstant(*stmt));
    std::string constant_name = constant->Name();
    // CREATE CONSTANTS doesn't support parameters and System variables.
    // They are passed as parameters to PreparedExpression.Execute call. Once
    // this class has a way to access parameters and System variables, they can
    // be passed to Execute.
    PreparedExpression prepared_expr(stmt->expr(), evaluator_options);
    GOOGLESQL_RETURN_IF_ERROR(constant->SetEvaluationResult(prepared_expr.Execute()));
    if (!evaluator()->catalog_for_temp_objects()->AddOwnedConstantIfNotPresent(
            std::move(constant))) {
      return googlesql_base::OutOfRangeErrorBuilder()
             << "Constant " << constant_name << " already exists";
    }
    const StructType* struct_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory()->MakeStructType(
        {{kCreatedObjectType, type_factory()->get_string()},
         {kCreatedObjectName, type_factory()->get_string()}},
        &struct_type));
    evaluator()->TakeOwnership(std::move(analyzer_output_));
    return Value::MakeStruct(struct_type, {Value::String("Temp Constant"),
                                           Value::String(constant_name)});
  }

  if (statement->node_kind() == RESOLVED_CREATE_FUNCTION_STMT) {
    const ResolvedCreateFunctionStmt* stmt =
        statement->GetAs<ResolvedCreateFunctionStmt>();
    GOOGLESQL_ASSIGN_OR_RETURN(auto function, MakeFunctionFromCreateFunction(
                                        *stmt, /*function_options=*/nullptr));

    std::string function_name = function->Name();
    if (!evaluator()->catalog_for_temp_objects()->AddOwnedFunctionIfNotPresent(
            &function)) {
      return googlesql_base::OutOfRangeErrorBuilder()
             << "Function " << function_name << " already exists";
    }

    const StructType* struct_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory()->MakeStructType(
        {{kCreatedObjectType, type_factory()->get_string()},
         {kCreatedObjectName, type_factory()->get_string()}},
        &struct_type));
    evaluator()->TakeOwnership(std::move(analyzer_output_));
    return Value::MakeStruct(struct_type, {Value::String("Temp Function"),
                                           Value::String(function_name)});
  }

  if (statement->node_kind() == RESOLVED_CREATE_TABLE_FUNCTION_STMT) {
    const ResolvedCreateTableFunctionStmt* stmt =
        statement->GetAs<ResolvedCreateTableFunctionStmt>();
    stmt->create_scope();  // Mark accessed so TEMP can be ignored.
    GOOGLESQL_ASSIGN_OR_RETURN(auto tvf, MakeTVFFromCreateTableFunction(*stmt));

    std::string tvf_name = tvf->Name();
    if (!evaluator()
             ->catalog_for_temp_objects()
             ->AddOwnedTableValuedFunctionIfNotPresent(&tvf)) {
      return googlesql_base::OutOfRangeErrorBuilder()
             << "TVF " << tvf_name << "already exists";
    }

    const StructType* struct_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory()->MakeStructType(
        {{kCreatedObjectType, type_factory()->get_string()},
         {kCreatedObjectName, type_factory()->get_string()}},
        &struct_type));
    evaluator()->TakeOwnership(std::move(analyzer_output_));
    return Value::MakeStruct(
        struct_type,
        {Value::String("Temp Table-valued Function"), Value::String(tvf_name)});
  }

  if (statement->node_kind() == RESOLVED_CREATE_PROCEDURE_STMT) {
    const auto* stmt = statement->GetAs<ResolvedCreateProcedureStmt>();
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SQLProcedure> procedure,
                     SQLProcedure::Create(stmt));
    if (!evaluator()->catalog_for_temp_objects()->AddOwnedProcedureIfNotPresent(
            std::move(procedure))) {
      return googlesql_base::OutOfRangeErrorBuilder()
             << "Procedure " << absl::StrJoin(stmt->name_path(), ".")
             << " already exists";
    }
    const StructType* struct_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory()->MakeStructType(
        {{kCreatedObjectType, type_factory()->get_string()},
         {kCreatedObjectName, type_factory()->get_string()}},
        &struct_type));
    evaluator()->TakeOwnership(std::move(analyzer_output_));
    return Value::MakeStruct(
        struct_type, {Value::String("Temp Procedure"),
                      Value::String(absl::StrJoin(stmt->name_path(), "."))});
  }

  return googlesql_base::UnimplementedErrorBuilder()
         << "Statement type not supported by StatementEvaluator: "
         << statement->node_kind_string();
}

absl::Status StatementEvaluatorImpl::ExpressionEvaluation::EvaluateImpl(
    absl::string_view sql, const AnalyzerOptions& analyzer_options,
    Catalog* catalog, const EvaluatorOptions& evaluator_options,
    const SystemVariableValuesMap& system_variables,
    std::variant<ParameterValueList, ParameterValueMap> parameters) {
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeExpressionForAssignmentToType(
      sql, analyzer_options, catalog, type_factory(), target_type_,
      &analyzer_output_));
  PreparedExpression prepared_expr(resolved_expr(), evaluator_options);
  GOOGLESQL_RETURN_IF_ERROR(prepared_expr.Prepare(analyzer_options, nullptr));
  if (std::holds_alternative<ParameterValueList>(parameters)) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        result_, prepared_expr.ExecuteWithPositionalParams(
                     /*columns=*/{}, std::get<ParameterValueList>(parameters),
                     system_variables));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        result_, prepared_expr.Execute(
                     /*columns=*/{}, std::get<ParameterValueMap>(parameters),
                     system_variables));
  }
  return absl::OkStatus();
}

// Aggregates a list of StatusOr results from a multi-statement query into a
// single Status. If there are no errors, returns OK. If there is one error,
// returns that error. If there are multiple errors, returns a single status
// that summarizes all of them.
static absl::Status AggregateStatuses(
    absl::Span<const absl::StatusOr<Value>> multi_statement_results) {
  std::vector<absl::Status> errors;
  bool has_internal_error = false;
  for (const auto& result : multi_statement_results) {
    if (!result.ok()) {
      errors.push_back(result.status());
      if (result.status().code() == absl::StatusCode::kInternal) {
        has_internal_error = true;
      }
    }
  }

  if (errors.empty()) {
    return absl::OkStatus();
  }

  if (errors.size() == 1) {
    return errors[0];
  }

  std::string aggregated_error_message = absl::StrCat(
      "Multiple errors in multi-statement query (", errors.size(), " total):");
  for (const auto& error : errors) {
    absl::StrAppend(&aggregated_error_message, "\n  ", error.message());
  }

  absl::StatusCode code = has_internal_error ? absl::StatusCode::kInternal
                                             : absl::StatusCode::kOutOfRange;
  return absl::Status(code, aggregated_error_message);
}

static absl::StatusOr<Value> MakeDmlResult(
    int num_rows_modified, Value new_table,
    EvaluatorTableIterator* returning_table_iter, TypeFactory* type_factory) {
  const StructType* struct_type;
  if (returning_table_iter == nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(
        {{kDMLOutputNumRowsModifiedColumnName, type_factory->get_int64()},
         {kDMLOutputAllRowsColumnName, new_table.type()}},
        &struct_type));
    return Value::MakeStruct(
        struct_type, {Value::Int64(num_rows_modified), std::move(new_table)});
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(Value returning_table_value,
                     IteratorToValue(type_factory, returning_table_iter,
                                     /*is_value_table=*/false));

    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(
        {{kDMLOutputNumRowsModifiedColumnName, type_factory->get_int64()},
         {kDMLOutputAllRowsColumnName, new_table.type()},
         {kDMLOutputReturningColumnName, returning_table_value.type()}},
        &struct_type));
    return Value::MakeStruct(
        struct_type, {Value::Int64(num_rows_modified), std::move(new_table),
                      std::move(returning_table_value)});
  }
}

absl::StatusOr<Value>
StatementEvaluatorImpl::StatementEvaluation::ProcessSingleStmtResult(
    PreparedStatement::StmtResult stmt_result,
    const EvaluatorOptions& evaluator_options) {
  const ResolvedStatement* sub_stmt = stmt_result.statement;
  switch (stmt_result.kind) {
    case PreparedStatementBase::StmtKind::kQuery: {
      return IteratorToValue(
          type_factory(), stmt_result.table_iterator.get(),
          sub_stmt->GetAs<ResolvedQueryStmt>()->is_value_table());
    }
    case PreparedStatementBase::StmtKind::kDML: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          int num_rows_modified,
          DoDmlSideEffects(stmt_result.modify_result.table_modify_iter.get()));
      GOOGLESQL_ASSIGN_OR_RETURN(
          Value new_table,
          GetTableContents(stmt_result.modify_result.table_modify_iter->table(),
                           type_factory()));
      return MakeDmlResult(num_rows_modified, std::move(new_table),
                           stmt_result.modify_result.returning_table_iter.get(),
                           type_factory());
    }
    case PreparedStatementBase::StmtKind::kCTAS: {
      return ProcessDdlStatement(sub_stmt, evaluator_options,
                                 std::move(stmt_result));
    }
  }
}

absl::StatusOr<Value>
StatementEvaluatorImpl::StatementEvaluation::EvaluateQueryStatement(
    const QueryOptions& query_options) {
  // Execute twice to get two iterators. One for the result Value, and one
  // to return from ExecuteQueryWithResult.
  GOOGLESQL_ASSIGN_OR_RETURN(auto result1,
                   prepared_statement_->ExecuteAfterPrepare(query_options));
  GOOGLESQL_RET_CHECK_EQ(result1.size(), 1);
  auto& result_or1 = result1[0];
  if (!result_or1.ok()) return result_or1.status();

  GOOGLESQL_ASSIGN_OR_RETURN(auto result2,
                   prepared_statement_->ExecuteAfterPrepare(query_options));
  GOOGLESQL_RET_CHECK_EQ(result2.size(), 1);
  auto& result_or2 = result2[0];
  if (!result_or2.ok()) return result_or2.status();

  table_iterator_ = std::move(result_or2->table_iterator);
  const ResolvedQueryStmt* query_stmt =
      resolved_statement()->GetAs<ResolvedQueryStmt>();
  return IteratorToValue(type_factory(), result_or1->table_iterator.get(),
                         query_stmt->is_value_table());
}

absl::Status
StatementEvaluatorImpl::StatementEvaluation::EvaluateWithPreparedStatement(
    const AnalyzerOptions& analyzer_options,
    const EvaluatorOptions& evaluator_options,
    const SystemVariableValuesMap& system_variables,
    std::variant<ParameterValueList, ParameterValueMap> parameters) {
  prepared_statement_ = std::make_unique<PreparedStatement>(
      resolved_statement(), evaluator_options);
  GOOGLESQL_RETURN_IF_ERROR(prepared_statement_->Prepare(analyzer_options, nullptr));

  QueryOptions query_options;
  if (std::holds_alternative<ParameterValueMap>(parameters)) {
    query_options.parameters = std::get<ParameterValueMap>(parameters);
  } else {
    query_options.ordered_parameters = std::get<ParameterValueList>(parameters);
  }
  query_options.system_variables = system_variables;

  if (resolved_statement()->node_kind() == RESOLVED_QUERY_STMT) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value result, EvaluateQueryStatement(query_options));
    results_.push_back(std::move(result));
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(PreparedStatement::StmtResults multi_stmt_results,
                   prepared_statement_->ExecuteAfterPrepare(query_options));

  if (resolved_statement()->node_kind() != RESOLVED_MULTI_STMT) {
    GOOGLESQL_RET_CHECK_EQ(multi_stmt_results.size(), 1);
  }

  for (absl::StatusOr<PreparedStatement::StmtResult>& result :
       multi_stmt_results) {
    if (!result.ok()) {
      results_.push_back(result.status());
      continue;
    }
    results_.push_back(
        ProcessSingleStmtResult(std::move(*result), evaluator_options));
  }
  return AggregateStatuses(results_);
}

absl::Status StatementEvaluatorImpl::StatementEvaluation::EvaluateImpl(
    absl::string_view sql, const AnalyzerOptions& analyzer_options,
    Catalog* catalog, const EvaluatorOptions& evaluator_options,
    const SystemVariableValuesMap& system_variables,
    std::variant<ParameterValueList, ParameterValueMap> parameters) {
  GOOGLESQL_RETURN_IF_ERROR(Analyze(sql, analyzer_options, catalog));

  switch (resolved_statement()->node_kind()) {
    case RESOLVED_CREATE_FUNCTION_STMT:
    case RESOLVED_CREATE_TABLE_FUNCTION_STMT:
    case RESOLVED_CREATE_CONSTANT_STMT:
    case RESOLVED_CREATE_PROCEDURE_STMT: {
      GOOGLESQL_RET_CHECK(results_.empty());
      results_.push_back(ProcessDdlStatement(resolved_statement(),
                                             evaluator_options,
                                             /*result=*/std::nullopt));
      return results_.back().status();
    }
    case RESOLVED_QUERY_STMT:
    case RESOLVED_INSERT_STMT:
    case RESOLVED_UPDATE_STMT:
    case RESOLVED_DELETE_STMT:
    case RESOLVED_MERGE_STMT:
    case RESOLVED_CREATE_TABLE_AS_SELECT_STMT:
    case RESOLVED_MULTI_STMT:
      return EvaluateWithPreparedStatement(analyzer_options, evaluator_options,
                                           system_variables, parameters);

    case RESOLVED_ASSIGNMENT_STMT: {
      // Assignments to script variables and @@timezone are handled internally
      // within ScriptExecutor.  If we see an assignment statement here, that
      // means we're assigning to something else, which is not supported.
      const ResolvedAssignmentStmt* assign_stmt =
          resolved_statement()->GetAs<ResolvedAssignmentStmt>();
      return MakeEvalError()
             << "Unsupported system variable for assignment: "
             << "@@"
             << absl::StrJoin(assign_stmt->target()
                                  ->GetAs<ResolvedSystemVariable>()
                                  ->name_path(),
                              ".");
    }
    default:
      return googlesql_base::UnimplementedErrorBuilder()
             << "Statement type not supported by StatementEvaluator: "
             << resolved_statement()->node_kind_string();
  }
}

namespace {
bool IsHandleableError(const absl::Status& status) {
  // Consider all errors as handleable, except internal errors.
  return !status.ok() && status.code() != absl::StatusCode::kInternal;
}

absl::Status WrapIfHandleable(const absl::Status& status) {
  if (IsHandleableError(status)) {
    absl::Status new_status(status);
    internal::AttachPayload(&new_status, ScriptException());
    return new_status;
  }
  return status;
}

template <class T>
absl::StatusOr<Value> MakeStatusOrValue(const absl::Status& status,
                                        const T& evaluation) {
  if (status.ok()) {
    return evaluation.result();
  }
  return status;
}

absl::StatusOr<Value> MakeStatusOrValue(
    const absl::Status& status,
    const std::vector<absl::StatusOr<Value>>& results) {
  GOOGLESQL_RET_CHECK_LE(results.size(), 1);
  if (results.size() == 1) {
    return results[0];
  }
  // This can happen, for example, when the statement fails with an analysis
  // error, no statement is executed, and thus `results` is empty.
  return status;
}

}  // namespace

absl::Status StatementEvaluatorImpl::ExecuteStatement(
    const ScriptExecutor& executor, const ScriptSegment& segment) {
  StatementEvaluation evaluation;
  absl::Status status =
      WrapIfHandleable(evaluation.Evaluate(executor, this, segment));
  if (callback_ != nullptr) {
    if (evaluation.resolved_statement() == nullptr) {
      // Analysis-time error.
      callback_->OnStatementResult(segment, nullptr, status);
    } else if (evaluation.resolved_statement()->node_kind() ==
               RESOLVED_MULTI_STMT) {
      // Multi-statement.
      GOOGLESQL_RET_CHECK(!evaluation.results().empty());
      callback_->OnMultiStatementResult(
          segment, evaluation.resolved_statement(), evaluation.results());
    } else {
      // Single statement.
      GOOGLESQL_RET_CHECK_LE(evaluation.results().size(), 1);
      callback_->OnStatementResult(
          segment, evaluation.resolved_statement(),
          MakeStatusOrValue(status, evaluation.results()));
    }
  }
  return status;
}

absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
StatementEvaluatorImpl::ExecuteQueryWithResult(const ScriptExecutor& executor,
                                               const ScriptSegment& segment) {
  if (segment.node()->node_kind() != AST_QUERY_STATEMENT &&
      segment.node()->node_kind() != AST_QUERY) {
    return MakeEvalError() << "ExecuteQueryWithResult used on non query node";
  }
  StatementEvaluation evaluation;
  absl::Status status =
      WrapIfHandleable(evaluation.Evaluate(executor, this, segment));
  GOOGLESQL_RET_CHECK_LE(evaluation.results().size(), 1);
  if (callback_ != nullptr) {
    callback_->OnStatementResult(
        segment, evaluation.resolved_statement(),
        MakeStatusOrValue(status, evaluation.results()));
  }
  if (!status.ok()) {
    return status;
  }

  return std::make_unique<EvaluatorTableIteratorWrapper>(
      evaluation.get_prepared_statement(), evaluation.get_table_iterator(),
      segment.node()->location().start());
}

absl::Status StatementEvaluatorImpl::SerializeIterator(
    const EvaluatorTableIterator& iterator, google::protobuf::Any& out) {
  GOOGLESQL_RET_CHECK(iterator.Status().ok());
  auto wrapper = dynamic_cast<const EvaluatorTableIteratorWrapper*>(&iterator);
  GOOGLESQL_RET_CHECK(wrapper != nullptr);
  EvaluatorTableIteratorProto it_msg;
  it_msg.set_location_byte_offset(wrapper->get_location().GetByteOffset());
  it_msg.set_next_row_count(wrapper->get_next_row_count());

  out.PackFrom(it_msg);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
StatementEvaluatorImpl::DeserializeToIterator(
    const google::protobuf::Any& msg, const ScriptExecutor& executor,
    const ParsedScript& parsed_script) {
  EvaluatorTableIteratorProto it_msg;
  msg.UnpackTo(&it_msg);

  ParseLocationPoint query_position =
      ParseLocationPoint::FromByteOffset(it_msg.location_byte_offset());
  GOOGLESQL_ASSIGN_OR_RETURN(const ASTNode* query_node,
                   parsed_script.FindScriptNodeFromPosition(query_position));
  GOOGLESQL_RET_CHECK_NE(query_node, nullptr)
      << "StatementEvaluatorImpl::DeserializeToIterator: position "
      << query_position.GetByteOffset()
      << " has no associated script node.  Script:\n"
      << parsed_script.script_text();

  ScriptSegment segment =
      ScriptSegment::FromASTNode(parsed_script.script_text(), query_node);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<EvaluatorTableIterator> it,
                   ExecuteQueryWithResult(executor, segment));

  auto wrapper = std::unique_ptr<EvaluatorTableIteratorWrapper>(
      dynamic_cast<EvaluatorTableIteratorWrapper*>(it.release()));

  for (int i = 0; i < it_msg.next_row_count(); i++) {
    if (!wrapper->NextRow()) {
      GOOGLESQL_RET_CHECK(wrapper->Status().ok());
    }
  }
  return wrapper;
}

absl::StatusOr<int64_t> StatementEvaluatorImpl::GetIteratorMemoryUsage(
    const EvaluatorTableIterator& iterator) {
  return callback_->get_bytes_per_iterator();
}

absl::StatusOr<int> StatementEvaluatorImpl::EvaluateCaseExpression(
    const ScriptSegment& case_value,
    const std::vector<ScriptSegment>& when_values,
    const ScriptExecutor& executor) {
  // Generate CASE...WHEN...END expression.
  std::string query_str = absl::StrCat("CASE ", case_value.GetSegmentText());
  // Store byte offset of each expression, so that ErrorLocation can be
  // translated to the corresponding location in the original script text.
  std::vector<size_t> offsets;
  offsets.push_back(query_str.size());
  for (int i = 0; i < when_values.size(); i++) {
    absl::StrAppend(&query_str, " WHEN ", when_values[i].GetSegmentText(),
                    " THEN ", i);
    offsets.push_back(query_str.size());
  }
  absl::StrAppend(&query_str, " ELSE -1 END");
  offsets.push_back(query_str.size());

  ParserOptions parser_options(initial_analyzer_options_.id_string_pool(),
                               initial_analyzer_options_.arena(),
                               initial_analyzer_options_.language());
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_RET_CHECK_OK(ParseExpression(query_str, parser_options, &parser_output));
  ScriptSegment query_segment =
      ScriptSegment::FromASTNode(query_str, parser_output->expression());
  auto status_or_index =
      EvaluateScalarExpression(executor, query_segment, nullptr);

  if (!status_or_index.status().ok()) {
    auto status = status_or_index.status();
    if (absl::StrContains(status.message(), "argument types")) {
      status = googlesql_base::InvalidArgumentErrorBuilder()
               << "CASE...WHEN statement values have incompatible types";
      internal::AttachPayload(&status, ScriptException());
    }
    std::pair<int, int> line_and_column;
    ErrorLocation location;
    ParseLocationTranslator translator(executor.GetScriptText());
    GOOGLESQL_ASSIGN_OR_RETURN(
        line_and_column,
        translator.GetLineAndColumnAfterTabExpansion(
            executor.GetCurrentNode()->ast_node()->location().start()));
    if (internal::HasPayloadWithType<ErrorLocation>(status)) {
      location = internal::GetPayload<ErrorLocation>(status);
      auto it = std::upper_bound(offsets.begin(), offsets.end(),
                                 location.column() - 1);
      int64_t vec_index = it - offsets.begin();
      if (vec_index == 0) {
        GOOGLESQL_ASSIGN_OR_RETURN(line_and_column,
                         translator.GetLineAndColumnAfterTabExpansion(
                             case_value.range().start()));
      } else if (vec_index <= when_values.size()) {
        GOOGLESQL_ASSIGN_OR_RETURN(line_and_column,
                         translator.GetLineAndColumnAfterTabExpansion(
                             when_values[vec_index - 1].range().start()));
      }
    }
    location.set_line(line_and_column.first);
    location.set_column(line_and_column.second);
    internal::AttachPayload(&status, location);
    return status;
  }

  GOOGLESQL_RET_CHECK(status_or_index.value().type()->IsInt64());
  return status_or_index.value().int64_value();
}

absl::StatusOr<Value> StatementEvaluatorImpl::EvaluateScalarExpression(
    const ScriptExecutor& executor, const ScriptSegment& segment,
    const Type* target_type) {
  ExpressionEvaluation evaluation(target_type);
  absl::Status status =
      WrapIfHandleable(evaluation.Evaluate(executor, this, segment));
  const absl::StatusOr<Value> status_or_result =
      MakeStatusOrValue(status, evaluation);
  if (callback_ != nullptr) {
    callback_->OnScalarExpressionResult(segment, evaluation.resolved_expr(),
                                        status_or_result);
  }
  return status_or_result;
}

bool StatementEvaluatorImpl::IsSupportedVariableType(
    const TypeWithParameters& type_with_params) {
  if (callback_ != nullptr) {
    return callback_->IsSupportedVariableType(type_with_params.type);
  }
  return true;
}

absl::Status StatementEvaluatorImpl::ApplyTypeParameterConstraints(
    const TypeParameters& type_params, Value* value) {
  // TODO: Refactor code to store the LanguageOptions in
  // the StatementEvaluator instead of hard-coding PRODUCT_INTERNAL.
  return ApplyConstraints(type_params, PRODUCT_INTERNAL, *value);
}

absl::StatusOr<std::unique_ptr<ProcedureDefinition>>
StatementEvaluatorImpl::LoadProcedure(const ScriptExecutor& executor,
                                      const absl::Span<const std::string>& path,
                                      const int64_t num_arguments) {
  if (callback_ != nullptr) {
    return callback_->LoadProcedure(path);
  }
  return absl::NotFoundError("");
}

namespace {
absl::StatusOr<const Type*> MakeStatusOrType(const ScriptSegment& segment,
                                             const absl::Status& status,
                                             const Type* type) {
  if (status.ok()) {
    return type;
  }
  return ConvertLocalErrorToScriptError(segment)(googlesql_base::StatusBuilder(status));
}
}  // namespace

absl::StatusOr<TypeWithParameters> StatementEvaluatorImpl::ResolveTypeName(
    const ScriptExecutor& executor, const ScriptSegment& segment) {
  AnalyzerOptions analyzer_options = initial_analyzer_options_;
  GOOGLESQL_RETURN_IF_ERROR(executor.UpdateAnalyzerOptions(analyzer_options));
  analyzer_options.CreateDefaultArenasIfNotSet();

  const Type* type;
  TypeModifiers type_modifiers;
  absl::Status status = WrapIfHandleable(
      AnalyzeType(std::string(segment.GetSegmentText()), analyzer_options,
                  catalog_, type_factory_, &type, &type_modifiers));
  TypeWithParameters type_with_params = {type,
                                         type_modifiers.type_parameters()};
  absl::StatusOr<const Type*> status_or_result =
      MakeStatusOrType(segment, status, type_with_params.type);
  if (callback_ != nullptr) {
    callback_->OnTypeResult(segment, status_or_result);
  }

  GOOGLESQL_RETURN_IF_ERROR(status_or_result.status());
  return type_with_params;
}

}  // namespace googlesql
