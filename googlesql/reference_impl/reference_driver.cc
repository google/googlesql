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

#include "googlesql/reference_impl/reference_driver.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/evaluator_registration_utils.h"
#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/compliance/test_driver.pb.h"
#include "googlesql/proto/script_exception.pb.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/prepared_expression_constant_evaluator.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/algebrizer.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/parameters.h"
#include "googlesql/reference_impl/statement_evaluator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/reference_impl/variable_generator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/scripting/error_helpers.h"
#include "googlesql/scripting/script_executor.h"
#include "googlesql/scripting/script_segment.h"
#include "googlesql/scripting/type_aliases.h"
#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/clock.h"

// Ideally we would rename this to
// --reference_driver_statement_eval_timeout_sec, but that could break existing
// command lines.
ABSL_FLAG(int32_t, reference_driver_query_eval_timeout_sec, 0,
          "Maximum statement evaluation timeout in seconds. A value of 0 "
          "means no maximum timeout is specified.");
ABSL_FLAG(bool, force_reference_product_mode_external, false,
          "If true, ignore the provided product mode setting and force "
          "the reference to use PRODUCT_EXTERNAL.");

ABSL_FLAG(bool, googlesql_reference_driver_default_external_mode, false,
          "If true, sets the ReferenceDriver's 'default' LanguageOptions to "
          "PRODUCT_EXTERNAL. The default LanguageOptions are used when the "
          "ReferenceDriver is acting as the 'engine' in a compliance test.");

// TODO: Remove when googlesql::FEATURE_ANONYMIZATION is no longer
// marked as in_development.
ABSL_FLAG(bool, reference_driver_enable_anonymization, false,
          "If true, enable the GoogleSQL Anonymization feature. See "
          "(broken link).");

// TODO: Remove when googlesql::FEATURE_DIFFERENTIAL_PRIVACY is no
// longer marked as in_development.
ABSL_FLAG(bool, reference_driver_enable_differential_privacy, false,
          "If true, enable the GoogleSQL new Differential Privacy syntax"
          "feature. See (broken link).");

namespace googlesql {

LanguageOptions ReferenceDriver::DefaultLanguageOptions() {
  LanguageOptions options;
  options.EnableMaximumLanguageFeatures();
  absl::Status status = options.EnableReservableKeyword("GRAPH_TABLE");
  status.Update(options.EnableReservableKeyword("MATCH_RECOGNIZE"));
  GOOGLESQL_DCHECK_OK(status);  // This status is not ok if "GRAPH_TABLE" is not a keyword
  options.SetSupportedStatementKinds(Algebrizer::GetSupportedStatementKinds());
  options.set_product_mode(
      absl::GetFlag(FLAGS_googlesql_reference_driver_default_external_mode)
          ? ProductMode::PRODUCT_EXTERNAL
          : ProductMode::PRODUCT_INTERNAL);
  return options;
}

ReferenceDriver::ReferenceDriver(
    LanguageOptions options,
    absl::btree_set<ResolvedASTRewrite> enabled_rewrites)
    : language_options_(std::move(options)),
      enabled_rewrites_(std::move(enabled_rewrites)),
      default_time_zone_(GetDefaultDefaultTimeZone()),
      statement_evaluation_timeout_(absl::Seconds(
          absl::GetFlag(FLAGS_reference_driver_query_eval_timeout_sec))) {
  if (absl::GetFlag(FLAGS_force_reference_product_mode_external) &&
      language_options_.product_mode() != ProductMode::PRODUCT_EXTERNAL) {
    ABSL_LOG(WARNING) << "Overriding requested Reference ProductMode "
                 << ProductMode_Name(language_options_.product_mode())
                 << " with PRODUCT_EXTERNAL.";
    language_options_.set_product_mode(ProductMode::PRODUCT_EXTERNAL);
  }
  if (absl::GetFlag(FLAGS_reference_driver_enable_anonymization)) {
    language_options_.EnableLanguageFeature(googlesql::FEATURE_ANONYMIZATION);
  }
  if (absl::GetFlag(FLAGS_reference_driver_enable_differential_privacy)) {
    // Named arguments are required for Differential_privacy
    language_options_.EnableLanguageFeature(googlesql::FEATURE_NAMED_ARGUMENTS);
    language_options_.EnableLanguageFeature(
        googlesql::FEATURE_DIFFERENTIAL_PRIVACY);
    language_options_.EnableLanguageFeature(
        googlesql::FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS);
  }

  // Optional evaluator features need to be enabled "manually" here since we do
  // not go through the public PreparedExpression/PreparedQuery interface, which
  // normally handles it.
  internal::EnableFullEvaluatorFeatures();
}

ReferenceDriver::~ReferenceDriver() = default;

// static
std::unique_ptr<ReferenceDriver> ReferenceDriver::CreateFromTestDriver(
    TestDriver* test_driver) {
  return std::make_unique<ReferenceDriver>(
      test_driver->GetSupportedLanguageOptions());
}

static bool IsRelationAndUsesUnsupportedType(
    const LanguageOptions& options, const FunctionArgumentType& arg_type,
    const Type** example) {
  if (!arg_type.IsFixedRelation()) {
    return false;
  }
  for (const TVFRelation::Column& column :
       arg_type.options().relation_input_schema().columns()) {
    if (!column.type->IsSupportedType(options)) {
      if (example != nullptr) {
        *example = column.type;
      }
      return true;
    }
  }
  return false;
}

// static
bool ReferenceDriver::UsesUnsupportedType(const LanguageOptions& options,
                                          const ResolvedNode* root,
                                          const Type** example) {
  std::vector<const ResolvedNode*> exprs;
  root->GetDescendantsSatisfying(&ResolvedNode::IsExpression, &exprs);
  for (const ResolvedNode* node : exprs) {
    const ResolvedExpr* expr = node->GetAs<ResolvedExpr>();
    if (!expr->type()->IsSupportedType(options)) {
      if (example != nullptr) {
        *example = expr->type();
      }
      return true;
    }
  }

  std::vector<const ResolvedNode*> create_tvf_stmts;
  root->GetDescendantsWithKinds({RESOLVED_CREATE_TABLE_FUNCTION_STMT},
                                &create_tvf_stmts);
  for (const ResolvedNode* node : create_tvf_stmts) {
    const auto* create_tvf_stmt =
        node->GetAs<ResolvedCreateTableFunctionStmt>();
    const FunctionArgumentType& return_type =
        create_tvf_stmt->signature().result_type();
    if (IsRelationAndUsesUnsupportedType(options, return_type, example)) {
      return true;
    }
    for (const FunctionArgumentType& arg :
         create_tvf_stmt->signature().arguments()) {
      if (IsRelationAndUsesUnsupportedType(options, arg, example)) {
        return true;
      }
    }
  }

  std::vector<const ResolvedNode*> nodes;
  root->GetDescendantsWithKinds({RESOLVED_OUTPUT_COLUMN}, &nodes);
  for (const ResolvedNode* node : nodes) {
    const auto* output_col = node->GetAs<ResolvedOutputColumn>();
    if (!output_col->column().type()->IsSupportedType(options)) {
      if (example != nullptr) {
        *example = output_col->column().type();
      }
      return true;
    }
  }
  return false;
}

absl::Status ReferenceDriver::LoadProtoEnumTypes(
    const std::set<std::string>& filenames,
    const std::set<std::string>& proto_names,
    const std::set<std::string>& enum_names) {
  return catalog_.LoadProtoEnumTypes(filenames, proto_names, enum_names);
}

absl::Status ReferenceDriver::AddTable(const std::string& table_name,
                                       const TestTable& table) {
  GOOGLESQL_RETURN_IF_ERROR(
      catalog_.AddTableWithStatus(table_name, table, language_options_));
  AddTableInternal(table_name, table);
  return absl::OkStatus();
}

void ReferenceDriver::AddTableInternal(const std::string& table_name,
                                       const TestTable& table) {
  const Value& array_value = table.table_as_value;
  ABSL_CHECK(array_value.type()->IsArray())  // Crash OK
      << table_name << " " << array_value.DebugString(true);
  const Table* catalog_table;
  GOOGLESQL_CHECK_OK(catalog_.FindTable({table_name}, &catalog_table));

  TableInfo table_info;
  table_info.table_name = table_name;
  table_info.required_features = table.options.required_features();
  table_info.is_value_table = table.options.is_value_table();
  table_info.table =
      const_cast<SimpleTable*>(dynamic_cast<const SimpleTable*>(catalog_table));
  ABSL_CHECK(table_info.table != nullptr);
  if (!table.measure_column_defs.empty()) {
    // If the test table contains measure columns, `table.table_as_value` does
    // not include the values for the measure columns. We compute them here
    // and create a new value representing the whole table including the measure
    // columns.
    // TODO: b/350555383 - Value tables should be supported. Remove this.
    ABSL_CHECK(!table.options.is_value_table());
    ABSL_CHECK(array_value.type()->IsArray());
    auto element_type = array_value.type()->AsArray()->element_type();
    ABSL_CHECK(element_type->IsStruct());
    absl::StatusOr<Value> new_array_value = UpdateTableRowsWithMeasureValues(
        array_value, table_info.table, table.measure_column_defs,
        table.row_identity_columns, type_factory(), language_options_);
    GOOGLESQL_CHECK_OK(new_array_value);
    table_info.array = *new_array_value;
  } else {
    table_info.array = table.table_as_value;
  }

  // Call `SimpleTable::SetContents()` so that the reference implementation can
  // use the evaluator APIs like `CreateEvaluatorTableIterator`, which can be
  // useful for evaluating functions that do not have a native reference
  // implementation, e.g., engine built-in functions.
  std::vector<std::vector<Value>> data;
  data.reserve(table_info.array.num_elements());
  for (int i = 0; i < table_info.array.num_elements(); ++i) {
    if (table_info.is_value_table) {
      data.push_back({table_info.array.element(i)});
    } else {
      data.push_back(table_info.array.element(i).fields());
    }
  }
  table_info.table->SetContents(data);

  tables_.push_back(table_info);
}

absl::Status ReferenceDriver::CreateDatabase(
    const TestDatabaseProto& test_db_proto) {
  GOOGLESQL_RETURN_IF_ERROR(catalog_.SetTestDatabase(test_db_proto, language_options_));
  GOOGLESQL_ASSIGN_OR_RETURN(TestDatabase test_db,
                   DeserializeTestDatabase(test_db_proto, type_factory(),
                                           descriptor_pool()));
  return CreateDatabaseInternal(test_db);
}

absl::Status ReferenceDriver::CreateDatabaseWithLeakyDescriptors(
    const TestDatabase& test_db) {
  GOOGLESQL_RETURN_IF_ERROR(catalog_.SetTestDatabaseWithLeakyDescriptors(test_db));
  return CreateDatabaseInternal(test_db);
}

absl::Status ReferenceDriver::CreateDatabaseInternal(
    const TestDatabase& test_db) {
  GOOGLESQL_RETURN_IF_ERROR(catalog_.SetLanguageOptions(language_options_));

  // Replace tables to the catalog.
  tables_.clear();
  for (const auto& t : test_db.tables) {
    const std::string& table_name = t.first;
    const TestTable& test_table = t.second;
    AddTableInternal(table_name, test_table);
  }

  AnalyzerOptions analyzer_options(language_options_);
  if (!language_options_.SupportsStatementKind(
          RESOLVED_CREATE_TABLE_FUNCTION_STMT)) {
    language_options_.AddSupportedStatementKind(
        RESOLVED_CREATE_TABLE_FUNCTION_STMT);
  }
  analyzer_options.set_default_time_zone(default_time_zone_);
  for (const auto& [tvf_name, create_stmt] : test_db.tvfs) {
    GOOGLESQL_RETURN_IF_ERROR(AddTVFFromCreateTableFunction(
        create_stmt, analyzer_options, /*allow_persistent=*/true,
        catalog_.sql_object_artifacts().emplace_back(), *catalog_.catalog()));
  }

  for (const auto& [_, graph_ddl] : test_db.property_graph_defs) {
    GOOGLESQL_RETURN_IF_ERROR(AddPropertyGraphs({graph_ddl}));
  }
  return absl::OkStatus();
}

absl::Status ReferenceDriver::SetStatementEvaluationTimeout(
    absl::Duration timeout) {
  GOOGLESQL_RET_CHECK_GE(timeout, absl::ZeroDuration());
  if (absl::GetFlag(FLAGS_reference_driver_query_eval_timeout_sec) > 0 &&
      timeout > absl::Seconds(absl::GetFlag(
                    FLAGS_reference_driver_query_eval_timeout_sec))) {
    return ::googlesql_base::OutOfRangeErrorBuilder()
           << "timeout value " << absl::ToInt64Seconds(timeout)
           << "sec is greater than reference_driver_query_eval_timeout_sec "
           << absl::GetFlag(FLAGS_reference_driver_query_eval_timeout_sec)
           << "secs.";
  }
  statement_evaluation_timeout_ = timeout;
  return absl::OkStatus();
}

void ReferenceDriver::SetLanguageOptions(const LanguageOptions& options) {
  language_options_ = options;
  absl::Status status = catalog_.SetLanguageOptions(options);
  if (!status.ok()) {
    ABSL_LOG(WARNING) << "Failed to set TestDatabaseCatalog language options: "
                 << status;
  }
}

absl::Status ReferenceDriver::AddSqlUdfs(
    absl::Span<const std::string> create_function_stmts) {
  return AddSqlUdfs(create_function_stmts, FunctionOptions());
}

absl::Status ReferenceDriver::AddSqlConstants(
    absl::Span<const std::string> create_constant_stmts) {
  // Ensure the language options used allow CREATE CONSTANT in schema setup
  LanguageOptions language = language_options_;
  language.AddSupportedStatementKind(RESOLVED_CREATE_CONSTANT_STMT);
  AnalyzerOptions analyzer_options(language);
  analyzer_options.set_default_time_zone(default_time_zone_);
  PreparedExpressionConstantEvaluator constant_evaluator(
      EvaluatorOptions{
          .default_time_zone = default_time_zone_,
      },
      language);
  analyzer_options.set_constant_evaluator(&constant_evaluator);
  for (const std::string& create_constant : create_constant_stmts) {
    GOOGLESQL_RETURN_IF_ERROR(AddConstantFromCreateConstant(
        create_constant, analyzer_options,
        catalog_.sql_object_artifacts().emplace_back(), *catalog_.catalog()));
  }
  return absl::OkStatus();
}

absl::Status ReferenceDriver::AddSqlUdfs(
    absl::Span<const std::string> create_function_stmts,
    FunctionOptions function_options) {
  // Ensure the language options used allow CREATE FUNCTION in schema setup
  LanguageOptions language = language_options_;
  language.AddSupportedStatementKind(RESOLVED_CREATE_FUNCTION_STMT);
  language.EnableLanguageFeature(FEATURE_CREATE_AGGREGATE_FUNCTION);
  AnalyzerOptions analyzer_options(language);
  analyzer_options.set_default_time_zone(default_time_zone_);
  analyzer_options.set_enabled_rewrites({});
  for (const std::string& create_function : create_function_stmts) {
    GOOGLESQL_RETURN_IF_ERROR(AddFunctionFromCreateFunction(
        create_function, analyzer_options, /*allow_persistent_function=*/false,
        &function_options, catalog_.sql_object_artifacts().emplace_back(),
        *catalog_.catalog(), *catalog_.catalog()));
  }
  return absl::OkStatus();
}

absl::Status ReferenceDriver::AddViews(
    absl::Span<const std::string> create_view_stmts) {
  // Ensure the language options used allow CREATE VIEW
  LanguageOptions language = language_options_;
  language.AddSupportedStatementKind(RESOLVED_CREATE_VIEW_STMT);
  AnalyzerOptions analyzer_options(language);
  analyzer_options.set_default_time_zone(default_time_zone_);
  // Don't pre-rewrite view bodies.
  // TODO: In RQG mode, apply a random subset of rewriters.
  analyzer_options.set_enabled_rewrites({});
  for (const std::string& create_view : create_view_stmts) {
    GOOGLESQL_RETURN_IF_ERROR(AddViewFromCreateView(
        create_view, analyzer_options,
        /*allow_non_temp=*/false,
        catalog_.sql_object_artifacts().emplace_back(), *catalog_.catalog()));
  }
  return absl::OkStatus();
}

absl::Status ReferenceDriver::AddPropertyGraphs(
    absl::Span<const std::string> create_property_graph_stmts) {
  // Ensure the language options used allow CREATE PROPERTY GRAPH
  LanguageOptions language = language_options_;
  language.AddSupportedStatementKind(RESOLVED_CREATE_PROPERTY_GRAPH_STMT);
  // TODO: remove once the feature is not in development.
  language.EnableLanguageFeature(FEATURE_SQL_GRAPH_DYNAMIC_MULTI_LABEL_NODES);
  AnalyzerOptions analyzer_options(language);
  analyzer_options.set_default_time_zone(default_time_zone_);
  // Don't pre-rewrite the DDL statement.
  // TODO: In RQG mode, apply a random subset of rewriters.
  analyzer_options.set_enabled_rewrites({});
  for (const std::string& create_property_graph : create_property_graph_stmts) {
    GOOGLESQL_RETURN_IF_ERROR(AddPropertyGraphFromCreatePropertyGraphStmt(
        create_property_graph, analyzer_options,
        catalog_.sql_object_artifacts(), *catalog_.catalog()));
  }
  return absl::OkStatus();
}

absl::StatusOr<AnalyzerOptions> ReferenceDriver::GetAnalyzerOptions(
    const std::map<std::string, Value>& parameters,
    std::optional<bool>& uses_unsupported_type) const {
  AnalyzerOptions analyzer_options(language_options_);
  analyzer_options.set_enabled_rewrites(enabled_rewrites_);
  analyzer_options.set_error_message_mode(
      ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET);
  analyzer_options.set_default_time_zone(default_time_zone_);

  for (const auto& p : parameters) {
    if (!p.second.type()->IsSupportedType(language_options_)) {
      // AnalyzerOptions will not let us add this parameter. Signal the caller
      // that the error is due to use of an unsupported type.
      uses_unsupported_type = true;
    }
    GOOGLESQL_RETURN_IF_ERROR(analyzer_options.AddQueryParameter(
        p.first, p.second.type()));  // Parameter names are case-insensitive.
  }
  return analyzer_options;
}

namespace {
// Creates a catalog that includes all symbols in <catalog>, plus script
// variables.
absl::StatusOr<std::unique_ptr<Catalog>> AugmentCatalogForScriptVariables(
    Catalog* catalog, TypeFactory* type_factory,
    const VariableMap& script_variables,
    std::unique_ptr<SimpleCatalog>* internal_catalog) {
  auto variables_catalog =
      std::make_unique<SimpleCatalog>("script_variables", type_factory);
  for (const std::pair<const IdString, Value>& variable : script_variables) {
    std::unique_ptr<SimpleConstant> constant;
    GOOGLESQL_RETURN_IF_ERROR(SimpleConstant::Create({variable.first.ToString()},
                                           variable.second, &constant));
    variables_catalog->AddOwnedConstant(std::move(constant));
  }

  std::unique_ptr<MultiCatalog> combined_catalog;
  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create("combined_catalog",
                                       {variables_catalog.get(), catalog},
                                       &combined_catalog));
  *internal_catalog = std::move(variables_catalog);

  return std::unique_ptr<Catalog>(std::move(combined_catalog));
}

// Executes a CREATE DDL statement, checking for IF NOT EXISTS/OR REPLACE to
// decide whether to create the object, overwrite it, skip it, or give an error.
//
// If the object does need to be added to the catalog, updates <map> and returns
// true. If the creation is skipped, does not update <map> and returns false.
// Returns an error if the object already exists and neither OR REPLACE nor
// IF NOT EXISTS are present.
template <typename Elem>
absl::StatusOr<bool> ApplyCreateDdl(
    const ResolvedCreateStatement* create_stmt,
    absl::flat_hash_map<std::vector<std::string>, Elem>& map, Elem elem,
    absl::string_view object_type) {
  std::vector<std::string> name_lower = create_stmt->name_path();
  for (std::string& part : name_lower) {
    part = absl::AsciiStrToLower(part);
  }

  bool already_exists = map.contains(name_lower);
  switch (create_stmt->create_mode()) {
    case ResolvedCreateTableStmtBase::CREATE_DEFAULT:
      if (already_exists) {
        return googlesql_base::InvalidArgumentErrorBuilder()
               << object_type << " "
               << absl::StrJoin(create_stmt->name_path(), ".")
               << " already exists";
      }
      map[name_lower] = std::move(elem);
      return true;
    case ResolvedCreateTableStmtBase::CREATE_IF_NOT_EXISTS:
      if (already_exists) {
        return false;
      }
      map[name_lower] = std::move(elem);
      return true;
    case ResolvedCreateTableStmtBase::CREATE_OR_REPLACE:
      map[name_lower] = std::move(elem);
      return true;
    default:
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Unexpected create mode: "
             << ResolvedCreateStatementEnums::CreateMode_Name(
                    create_stmt->create_mode());
  }
}

absl::StatusOr<Value> ToSingleResult(const MultiStmtResult& result) {
  GOOGLESQL_RET_CHECK(!result.statement_results.empty());
  if (result.statement_results.size() > 1) {
    return absl::OutOfRangeError(
        "Executed statement returns multiple results, which is not supported "
        "in ExecuteStatement. Use ExecuteGeneralizedStatement instead");
  }
  return result.statement_results[0].result;
}

MultiStmtResult ToMultiResult(absl::StatusOr<Value>&& value) {
  return MultiStmtResult{{StatementResult{.result = std::move(value)}}};
}

// Aggregates a list of StatusOr results from a multi-statement query into a
// single Status. If there are no errors, returns OK. If there is one error,
// returns that error. If there are multiple errors, returns a single status
// that summarizes all of them.
static absl::Status AggregateStatuses(
    const absl::StatusOr<MultiStmtResult>& multi_stmt_result) {
  if (!multi_stmt_result.ok()) {
    return multi_stmt_result.status();
  }
  std::vector<absl::Status> errors;
  bool has_internal_error = false;
  for (const auto& result : multi_stmt_result->statement_results) {
    if (!result.result.ok()) {
      errors.push_back(result.result.status());
      if (result.result.status().code() == absl::StatusCode::kInternal) {
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

}  // namespace

absl::StatusOr<Value> ReferenceDriver::ExecuteStatementForReferenceDriver(
    absl::string_view sql, const std::map<std::string, Value>& parameters,
    const ExecuteStatementOptions& options,
    ExecuteStatementAuxOutput& aux_output, TestDatabase* database) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      AnalyzerOptions analyzer_options,
      GetAnalyzerOptions(parameters, aux_output.uses_unsupported_type));

  GOOGLESQL_ASSIGN_OR_RETURN(MultiStmtResult result,
                   ExecuteStatementForReferenceDriverInternal(
                       sql, analyzer_options, parameters,
                       /*script_variables=*/{},
                       /*system_variables=*/{}, options, database,
                       /*analyzed_input=*/nullptr, aux_output));
  return ToSingleResult(result);
}

absl::StatusOr<MultiStmtResult>
ReferenceDriver::ExecuteGeneralizedStatementForReferenceDriver(
    absl::string_view sql, const std::map<std::string, Value>& parameters,
    const ExecuteStatementOptions& options,
    ExecuteStatementAuxOutput& aux_output, TestDatabase* database) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      AnalyzerOptions analyzer_options,
      GetAnalyzerOptions(parameters, aux_output.uses_unsupported_type));

  return ExecuteStatementForReferenceDriverInternal(
      sql, analyzer_options, parameters,
      /*script_variables=*/{},
      /*system_variables=*/{}, options, database,
      /*analyzed_input=*/nullptr, aux_output);
}

namespace {

absl::Status SetAnnotationMapFromResolvedColumnAnnotations(
    const ResolvedColumnAnnotations* column_annotations,
    AnnotationMap* annotation_map) {
  if (column_annotations == nullptr) {
    return absl::OkStatus();
  }
  if (column_annotations->collation_name() != nullptr) {
    const ResolvedExpr* collation_name = column_annotations->collation_name();
    GOOGLESQL_RET_CHECK(collation_name->node_kind() == RESOLVED_LITERAL);
    GOOGLESQL_RET_CHECK(collation_name->type()->IsString());
    std::string collation =
        collation_name->GetAs<ResolvedLiteral>()->value().string_value();
    annotation_map->SetAnnotation<CollationAnnotation>(
        SimpleValue::String(collation));
  }
  if (annotation_map->IsArrayMap()) {
    if (column_annotations->child_list_size() == 1) {
      GOOGLESQL_RETURN_IF_ERROR(SetAnnotationMapFromResolvedColumnAnnotations(
          column_annotations->child_list(0),
          annotation_map->AsStructMap()->mutable_field(0)));
    }
  } else if (annotation_map->IsStructMap()) {
    StructAnnotationMap* struct_map = annotation_map->AsStructMap();
    if (column_annotations->child_list_size() > 0 &&
        column_annotations->child_list_size() <= struct_map->num_fields()) {
      for (int i = 0; i < column_annotations->child_list_size(); i++) {
        GOOGLESQL_RETURN_IF_ERROR(SetAnnotationMapFromResolvedColumnAnnotations(
            column_annotations->child_list(i), struct_map->mutable_field(i)));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<Value> ReferenceDriver::ExecuteStatement(
    const std::string& sql, const std::map<std::string, Value>& parameters,
    TypeFactory* type_factory) {
  ReferenceDriver::ExecuteStatementOptions options{.primary_key_mode =
                                                       PrimaryKeyMode::DEFAULT};
  GOOGLESQL_RET_CHECK_EQ(type_factory, this->type_factory());
  ExecuteStatementAuxOutput aux_output_ignored;
  // Create a local test database to allow running DDL statements. Note the
  // reference driver does not actually apply side effects.
  TestDatabase database;
  return ExecuteStatementForReferenceDriver(sql, parameters, options,
                                            aux_output_ignored, &database);
}

absl::StatusOr<MultiStmtResult> ReferenceDriver::ExecuteGeneralizedStatement(
    const std::string& sql, const std::map<std::string, Value>& parameters,
    TypeFactory* type_factory) {
  ReferenceDriver::ExecuteStatementOptions options{.primary_key_mode =
                                                       PrimaryKeyMode::DEFAULT};
  ExecuteStatementAuxOutput aux_output_ignored;
  // Create a local test database to allow running DDL statements. Note the
  // reference driver does not actually apply side effects.
  TestDatabase database;
  GOOGLESQL_RET_CHECK_EQ(type_factory, this->type_factory());
  return ExecuteGeneralizedStatementForReferenceDriver(
      sql, parameters, options, aux_output_ignored, &database);
}

absl::StatusOr<MultiStmtResult> ReferenceDriver::ExecuteScript(
    const std::string& sql, const std::map<std::string, Value>& parameters,
    TypeFactory* type_factory) {
  ExecuteStatementOptions options{.primary_key_mode = PrimaryKeyMode::DEFAULT};
  ExecuteScriptAuxOutput aux_output_ignored;
  GOOGLESQL_RET_CHECK_EQ(type_factory, this->type_factory());
  return ExecuteScriptForReferenceDriver(sql, parameters, options,
                                         aux_output_ignored);
}

absl::StatusOr<std::unique_ptr<const AnalyzerOutput>>
ReferenceDriver::AnalyzeStatement(
    absl::string_view sql, TypeFactory* type_factory,
    const std::map<std::string, Value>& parameters, Catalog* catalog,
    const AnalyzerOptions& analyzer_options) {
  std::unique_ptr<const AnalyzerOutput> analyzed;
  GOOGLESQL_RETURN_IF_ERROR(googlesql::AnalyzeStatement(sql, analyzer_options, catalog,
                                              type_factory, &analyzed));
  // TODO: Remove this once the aggregation threshold rewriter and
  // anonymization rewriter are updated to follow the correct pattern.
  if (analyzed->analyzer_output_properties().has_anonymization) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        analyzed, RewriteForAnonymization(analyzed, analyzer_options, catalog,
                                          type_factory));
  }
  return analyzed;
}

static bool HasSuccessfulStatements(const MultiStmtResult& result_list) {
  return absl::c_any_of(
      result_list.statement_results,
      [](const StatementResult& result) { return result.result.ok(); });
}

static absl::StatusOr<Value> HandleCreateTableStatement(
    const ResolvedCreateTableStmtBase* create_table, const Value& output_value,
    TypeFactory* type_factory, TestDatabase* database,
    ReferenceDriver::ExecuteStatementAuxOutput& aux_output) {
  // Insert the table into the database
  GOOGLESQL_RET_CHECK(database != nullptr);
  TestTable new_table;
  new_table.table_as_value = output_value;
  new_table.options.set_is_value_table(create_table->is_value_table());

  std::vector<const AnnotationMap*> column_annotations;
  bool all_empty_annotations = true;
  for (const auto& column_definition : create_table->column_definition_list()) {
    std::unique_ptr<AnnotationMap> new_map =
        AnnotationMap::Create(column_definition->type());
    GOOGLESQL_RETURN_IF_ERROR(SetAnnotationMapFromResolvedColumnAnnotations(
        column_definition->annotations(), new_map.get()));
    if (new_map->Empty()) {
      column_annotations.push_back(nullptr);
    } else {
      all_empty_annotations = false;
      GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* owned,
                       type_factory->TakeOwnership(std::move(new_map)));
      column_annotations.push_back(owned);
    }
  }
  if (!all_empty_annotations) {
    new_table.options.set_column_annotations(std::move(column_annotations));
  }

  for (const auto& option : create_table->option_list()) {
    if (option->name() == "userid_column") {
      if (option->value()->node_kind() != RESOLVED_LITERAL ||
          !option->value()->type()->IsString()) {
        return absl::InvalidArgumentError(
            "userid_column option must be a STRING literal");
      }
      new_table.options.set_userid_column(
          option->value()->GetAs<ResolvedLiteral>()->value().string_value());
    } else {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Unsupported CREATE TABLE option: " << option->name();
    }
  }

  std::string table_name = absl::StrJoin(create_table->name_path(), ".");
  aux_output.created_table_names.push_back(table_name);
  bool already_exists = googlesql_base::ContainsKey(database->tables, table_name);
  switch (create_table->create_mode()) {
    case ResolvedCreateTableStmtBase::CREATE_DEFAULT:
      if (already_exists) {
        return googlesql_base::InvalidArgumentErrorBuilder()
               << "Table " << table_name << " already exists";
      } else {
        database->tables[table_name] = new_table;
      }
      break;
    case ResolvedCreateTableStmtBase::CREATE_IF_NOT_EXISTS:
      if (!already_exists) {
        database->tables[table_name] = new_table;
      }
      break;
    case ResolvedCreateTableStmtBase::CREATE_OR_REPLACE:
      database->tables[table_name] = new_table;
      break;
    default:
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Unexpected create mode: "
             << ResolvedCreateStatementEnums::CreateMode_Name(
                    create_table->create_mode());
  }
  // On success, the DDL statement result is the table itself.
  return output_value;
}

absl::StatusOr<MultiStmtResult>
ReferenceDriver::ExecuteStatementForReferenceDriverInternal(
    absl::string_view sql, const AnalyzerOptions& analyzer_options,
    const std::map<std::string, Value>& parameters,
    const VariableMap& script_variables,
    const SystemVariableValuesMap& system_variables,
    const ExecuteStatementOptions& options, TestDatabase* database,
    // If provide, uses this instead of calling analyzer.
    const AnalyzerOutput* analyzed_input,
    ExecuteStatementAuxOutput& aux_output) {
  absl::Status catalog_status = catalog_.IsInitialized();
  if (absl::IsFailedPrecondition(catalog_status)) {
    return absl::FailedPreconditionError(
        "Call ReferenceDriver::CreateDatabase() first");
  } else if (!catalog_status.ok()) {
    return catalog_status;
  }

  std::unique_ptr<SimpleCatalog> internal_catalog;
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<Catalog> catalog,
      AugmentCatalogForScriptVariables(catalog_.catalog(), type_factory(),
                                       script_variables, &internal_catalog));

  const AnalyzerOutput* analyzed = analyzed_input;
  std::unique_ptr<const AnalyzerOutput> fresh_analyzer_out;
  if (!analyzed) {
    GOOGLESQL_ASSIGN_OR_RETURN(fresh_analyzer_out,
                     AnalyzeStatement(sql, type_factory(), parameters,
                                      catalog.get(), analyzer_options));
    analyzed = fresh_analyzer_out.get();
  }
  aux_output.analyzer_runtime_info = analyzed->runtime_info();
  if (analyzed->resolved_statement()->node_kind() ==
      RESOLVED_CREATE_PROCEDURE_STMT) {
    // This statement is executed directly in the reference driver, rather than
    // through the algebrizer/evaluator.
    const ResolvedCreateProcedureStmt* create_procedure_stmt =
        analyzed->resolved_statement()->GetAs<ResolvedCreateProcedureStmt>();
    std::string name = absl::StrJoin(create_procedure_stmt->name_path(), ".");
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool created,
        ApplyCreateDdl(create_procedure_stmt, procedures_,
                       std::make_unique<ProcedureDefinition>(
                           name, create_procedure_stmt->signature(),
                           create_procedure_stmt->argument_name_list(),
                           create_procedure_stmt->procedure_body()),
                       "Procedure"));
    Value result;
    if (created) {
      result = Value::String(
          absl::StrCat("Procedure ", name, " created successfully"));
    } else {
      result =
          Value::String(absl::StrCat("Skipped creation of procedure ", name));
    }
    return ToMultiResult(std::move(result));
  }

  if (analyzed->resolved_statement()->node_kind() ==
      RESOLVED_CREATE_TABLE_FUNCTION_STMT) {
    // This statement is executed directly in the reference driver, rather than
    // through the algebrizer/evaluator.
    // Insert the table function into the database
    GOOGLESQL_RET_CHECK(database != nullptr);
    const auto* create_tvf_stmt =
        analyzed->resolved_statement()
            ->GetAs<ResolvedCreateTableFunctionStmt>();

    std::string tvf_name = absl::StrJoin(create_tvf_stmt->name_path(), ".");
    aux_output.created_table_names.push_back(tvf_name);
    bool already_exists = googlesql_base::ContainsKey(database->tvfs, tvf_name);
    switch (create_tvf_stmt->create_mode()) {
      case ResolvedCreateStatement::CREATE_DEFAULT:
        if (already_exists) {
          return googlesql_base::InvalidArgumentErrorBuilder()
                 << "TVF " << tvf_name << " already exists";
        } else {
          database->tvfs[tvf_name] = sql;
        }
        break;
      case ResolvedCreateStatement::CREATE_IF_NOT_EXISTS:
        if (!already_exists) {
          database->tvfs[tvf_name] = sql;
        }
        break;
      case ResolvedCreateStatement::CREATE_OR_REPLACE:
        database->tvfs[tvf_name] = sql;
        break;
      default:
        return googlesql_base::InvalidArgumentErrorBuilder()
               << "Unexpected create mode: "
               << ResolvedCreateStatementEnums::CreateMode_Name(
                      create_tvf_stmt->create_mode());
    }
    return ToMultiResult(
        Value::String(absl::StrCat("TVF ", tvf_name, " created successfully")));
  }

  // Don't proceed if any columns referenced within the query have types not
  // supported by the language options.
  const Type* example = nullptr;
  if (UsesUnsupportedType(language_options_, analyzed->resolved_statement(),
                          &example)) {
    aux_output.uses_unsupported_type = true;
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Query references column with unsupported type: "
           << example->DebugString();
  }

  AlgebrizerOptions algebrizer_options;
  algebrizer_options.use_arrays_for_tables = true;
  algebrizer_options.max_seen_column_id =
      std::make_shared<int>(analyzed->max_column_id());

  std::unique_ptr<ValueExpr> algebrized_tree;
  Parameters algebrizer_parameters(ParameterMap{});
  ParameterMap column_map;
  SystemVariablesAlgebrizerMap algebrizer_system_variables;
  GOOGLESQL_RETURN_IF_ERROR(Algebrizer::AlgebrizeStatement(
      analyzer_options.language(), algebrizer_options, type_factory(),
      analyzed->resolved_statement(), &algebrized_tree, &algebrizer_parameters,
      &column_map, &algebrizer_system_variables));
  GOOGLESQL_VLOG(1) << "Algebrized tree:\n"
          << algebrized_tree->DebugString(true /* verbose */);
  GOOGLESQL_RET_CHECK(column_map.empty());

  if (!algebrized_tree->output_type()->IsSupportedType(language_options_)) {
    aux_output.uses_unsupported_type = true;
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Query produces result with unsupported type: "
           << algebrized_tree->output_type()->DebugString();
  }

  EvaluationOptions evaluation_options;
  evaluation_options.emulate_primary_keys =
      (options.primary_key_mode == PrimaryKeyMode::FIRST_COLUMN_IS_PRIMARY_KEY);
  evaluation_options.scramble_undefined_orderings = true;
  evaluation_options.always_use_stable_sort = true;
  // Use a reasonably large value to accommodate all test cases, but also small
  // enough to avoid hard to debug OOM test failures.
  evaluation_options.max_value_byte_size = 256 * 1024 * 1024;  // 256Mb
  evaluation_options.max_intermediate_byte_size =
      std::numeric_limits<int64_t>::max();

  EvaluationContext context(evaluation_options);
  context.SetDefaultTimeZone(default_time_zone_);
  context.SetLanguageOptions(analyzer_options.language());

  for (const TableInfo& table_info : tables_) {
    bool has_all_required_features = true;
    for (const LanguageFeature required_feature :
         table_info.required_features) {
      if (!analyzer_options.language().LanguageFeatureEnabled(
              required_feature)) {
        has_all_required_features = false;
        break;
      }
    }
    if (has_all_required_features) {
      GOOGLESQL_RETURN_IF_ERROR(context.AddTableAsArray(
          table_info.table_name, table_info.is_value_table, table_info.array,
          analyzer_options.language()));
    } else {
      ABSL_LOG(INFO) << "Skipping table " << table_info.table_name
                << " due to some missing features";
    }
  }
  if (statement_evaluation_timeout_ > absl::ZeroDuration()) {
    context.SetStatementEvaluationDeadlineFromNow(
        statement_evaluation_timeout_);
  }

  std::vector<VariableId> param_variables;
  param_variables.reserve(parameters.size());
  std::vector<Value> param_values;
  param_values.reserve(parameters.size());
  for (const auto& p : parameters) {
    // Set the parameter if it appears in the statement, ignore it otherwise.
    // Note that it is ok if some parameters are not referenced.
    const ParameterMap& parameter_map =
        algebrizer_parameters.named_parameters();
    auto it = parameter_map.find(absl::AsciiStrToLower(p.first));
    if (it != parameter_map.end() && it->second.is_valid()) {
      param_variables.push_back(it->second);
      if (!analyzer_options.language().LanguageFeatureEnabled(
              FEATURE_TIMESTAMP_NANOS)) {
        // TODO assert that time related values have no significant
        //     nanos fraction. Supplying parameters with nanos fractions can
        //     lead to wrong results.
      }
      param_values.push_back(p.second);
      GOOGLESQL_VLOG(1) << "Parameter @" << p.first << " (variable " << it->second
              << "): " << p.second.FullDebugString();
    }
  }
  const TupleSchema params_schema(param_variables);
  const TupleData params_data =
      CreateTupleDataFromValues(std::move(param_values));

  std::vector<VariableId> system_var_ids;
  std::vector<Value> system_var_values;
  system_var_ids.reserve(algebrizer_system_variables.size());
  for (const auto& sys_var : algebrizer_system_variables) {
    system_var_ids.push_back(sys_var.second);
    system_var_values.push_back(system_variables.at(sys_var.first));
  }
  const TupleSchema system_vars_schema(system_var_ids);
  const TupleData system_vars_data =
      CreateTupleDataFromValues(std::move(system_var_values));

  GOOGLESQL_RETURN_IF_ERROR(algebrized_tree->SetSchemasForEvaluation(
      {&params_schema, &system_vars_schema}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<StmtResult> results,
      algebrized_tree->EvalMulti({&params_data, &system_vars_data}, &context));

  std::vector<const ResolvedStatement*> sub_stmts;
  const ResolvedStatement* top_level_resolved_stmt =
      analyzed->resolved_statement();

  if (top_level_resolved_stmt->node_kind() == RESOLVED_MULTI_STMT) {
    const ResolvedMultiStmt* multi_stmt_node =
        top_level_resolved_stmt->GetAs<ResolvedMultiStmt>();
    for (const auto& sub_stmt_ptr : multi_stmt_node->statement_list()) {
      sub_stmts.push_back(sub_stmt_ptr.get());
    }
  } else {
    for (size_t i = 0; i < results.size(); ++i) {
      sub_stmts.push_back(top_level_resolved_stmt);
    }
  }

  GOOGLESQL_RET_CHECK(sub_stmts.size() == results.size())
      << "Mismatch between number of resolved statements for evaluation ("
      << sub_stmts.size() << ") and evaluation results (" << results.size()
      << "). SQL: " << sql;

  MultiStmtResult final_result;

  for (size_t i = 0; i < results.size(); ++i) {
    const ResolvedStatement* sub_stmt = sub_stmts[i];
    const StmtResult& result = results[i];

    StatementResult sr;

    if (sub_stmt->node_kind() == RESOLVED_CREATE_WITH_ENTRY_STMT) {
      // ResolvedCreateWithEntryStmt does not output a result. If it fails,
      // its error is propagated to subsequent statements.
      continue;
    }

    if (!result.value.ok()) {
      sr.result = result.value.status();
      final_result.statement_results.push_back(std::move(sr));
      continue;
    }

    const Value& output_value = *result.value;
    sr.result = output_value;

    const Type* output_type = output_value.type();
    switch (sub_stmt->node_kind()) {
      case RESOLVED_QUERY_STMT: {
        GOOGLESQL_RET_CHECK(output_type->IsArray());
        break;
      }
      case RESOLVED_CREATE_WITH_ENTRY_STMT: {
        GOOGLESQL_RET_CHECK(output_type->IsArray());
        break;
      }
      case RESOLVED_DELETE_STMT:
      case RESOLVED_UPDATE_STMT:
      case RESOLVED_INSERT_STMT:
      case RESOLVED_MERGE_STMT: {
        GOOGLESQL_RET_CHECK(output_type->IsStruct());
        const StructType* output_struct_type = output_type->AsStruct();

        int expect_num_fields = output_struct_type->num_fields();
        if (sub_stmt->node_kind() == RESOLVED_MERGE_STMT) {
          GOOGLESQL_RET_CHECK_EQ(expect_num_fields, 2);
        } else {
          GOOGLESQL_RET_CHECK(expect_num_fields == 2 || expect_num_fields == 3);
        }

        const StructField& field1 = output_struct_type->field(0);
        GOOGLESQL_RET_CHECK_EQ(kDMLOutputNumRowsModifiedColumnName, field1.name);
        GOOGLESQL_RET_CHECK(field1.type->IsInt64());

        const StructField& field2 = output_struct_type->field(1);
        GOOGLESQL_RET_CHECK_EQ(kDMLOutputAllRowsColumnName, field2.name);
        GOOGLESQL_RET_CHECK(field2.type->IsArray());

        if (expect_num_fields == 3) {
          const StructField& field3 = output_struct_type->field(2);
          GOOGLESQL_RET_CHECK_EQ(kDMLOutputReturningColumnName, field3.name);
          GOOGLESQL_RET_CHECK(field3.type->IsArray());
        }
        break;
      }
      case RESOLVED_CREATE_TABLE_STMT:
      case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
        // Insert the table into the database
        const ResolvedCreateTableStmtBase* create_table =
            sub_stmt->GetAs<ResolvedCreateTableStmtBase>();
        GOOGLESQL_RET_CHECK(create_table != nullptr);
        sr.result = HandleCreateTableStatement(
            create_table, output_value, type_factory(), database, aux_output);
        break;
      }
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected statement type: "
                         << ResolvedNodeKind_Name(
                                analyzed->resolved_statement()->node_kind());
        break;
    }
    final_result.statement_results.push_back(std::move(sr));
  }

  if (HasSuccessfulStatements(final_result)) {
    // We check `HasSuccessfulStatements` before logging the
    // `is_deterministic_output` bit to preserve the existing behavior, but it
    // may make sense to log the bit even when the statement fails.
    aux_output.is_deterministic_output = context.IsDeterministicOutput();
  }
  return final_result;
}

absl::StatusOr<std::vector<Value>> ReferenceDriver::RepeatExecuteStatement(
    const std::string& sql, const std::map<std::string, Value>& parameters,
    TypeFactory* type_factory, uint64_t times) {
  ExecuteStatementAuxOutput aux_output_ignored;
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto analyzer_options,
      GetAnalyzerOptions(parameters, aux_output_ignored.uses_unsupported_type));

  GOOGLESQL_ASSIGN_OR_RETURN(auto analyzed,
                   AnalyzeStatement(sql, type_factory, parameters, catalog(),
                                    analyzer_options));
  ReferenceDriver::ExecuteStatementOptions options{.primary_key_mode =
                                                       PrimaryKeyMode::DEFAULT};
  std::vector<Value> result(times);
  for (int i = 0; i < times; ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        MultiStmtResult result_list,
        ExecuteStatementForReferenceDriverInternal(
            sql, analyzer_options, parameters,
            /*script_variables=*/{},
            /*system_variables=*/{}, options,
            /*database=*/nullptr, analyzed.get(), aux_output_ignored));
    GOOGLESQL_ASSIGN_OR_RETURN(result[i], ToSingleResult(result_list));
  }
  return result;
}

// StatementEvaluator implementation for compliance tests with the reference
// driver. We use the reference driver to evaluate statements and the default
// StatementEvaluator for everything else.
class ReferenceDriverStatementEvaluator : public StatementEvaluatorImpl {
 public:
  ReferenceDriverStatementEvaluator(
      const AnalyzerOptions& initial_analyzer_options, MultiStmtResult* result,
      const std::map<std::string, Value>* parameters,
      const ReferenceDriver::ExecuteStatementOptions& options,
      TypeFactory* type_factory, ReferenceDriver* driver,
      const EvaluatorOptions& evaluator_options)
      : StatementEvaluatorImpl(initial_analyzer_options, evaluator_options,
                               *parameters, type_factory, driver->catalog(),
                               &evaluator_callback_),
        evaluator_callback_(/*bytes_per_iterator=*/100),
        result_(result),
        parameters_(parameters),
        options_(options),
        type_factory_(type_factory),
        driver_(driver) {}

  // Override ExecuteStatement() to ensure that statement results exactly match
  // up what would be produced by a standalone-statement compliance test.
  absl::Status ExecuteStatement(const ScriptExecutor& executor,
                                const ScriptSegment& segment) override;

  absl::StatusOr<std::unique_ptr<ProcedureDefinition>> LoadProcedure(
      const ScriptExecutor& executor, const absl::Span<const std::string>& path,
      const int64_t num_arguments) override {
    std::vector<std::string> name_lower;
    name_lower.reserve(path.size());
    for (const std::string& part : path) {
      name_lower.push_back(absl::AsciiStrToLower(part));
    }

    auto it = driver_->procedures_.find(name_lower);
    if (it == driver_->procedures_.end()) {
      return googlesql_base::NotFoundErrorBuilder()
             << "Procedure " << absl::StrJoin(path, ".") << " not found";
    }
    return std::make_unique<ProcedureDefinition>(*it->second);
  }

  // TODO: Currently, this is only set to true if a statement uses an
  // unsupported type, and fails to detect cases where a script variable or
  // expression uses an unsupported type.
  bool uses_unsupported_type() const { return uses_unsupported_type_; }

  // Returns false if any call to ExecuteStatement() returned a
  // non-deterministic result.
  bool is_deterministic_output() const { return is_deterministic_output_; }

 private:
  StatementEvaluatorCallback evaluator_callback_;
  MultiStmtResult* result_;
  const std::map<std::string, Value>* parameters_;
  ReferenceDriver::ExecuteStatementOptions options_;
  TypeFactory* type_factory_;
  ReferenceDriver* driver_;
  bool uses_unsupported_type_ = false;
  bool is_deterministic_output_ = true;
};

absl::Status ReferenceDriverStatementEvaluator::ExecuteStatement(
    const ScriptExecutor& executor, const ScriptSegment& segment) {
  ParseLocationTranslator translator(segment.script());
  absl::StatusOr<std::pair<int, int>> line_and_column =
      translator.GetLineAndColumnAfterTabExpansion(segment.range().start());
  // Because the statement can be a multi-stmt producing multiple results, we
  // create a result template with information like line and column numbers
  // that are common to all results produced by the multi-stmt.
  //
  // TODO: It makes sense to have separate line and column numbers for each
  // sub-statement in a multi-stmt.
  StatementResult result_template;
  result_template.procedure_name = executor.GetCurrentProcedureName();
  result_template.line = line_and_column.ok() ? line_and_column->first : 0;
  result_template.column = line_and_column.ok() ? line_and_column->second : 0;

  AnalyzerOptions analyzer_options = initial_analyzer_options();

  // Always execute statements using ERROR_MESSAGE_WITH_PAYLOAD; we need the
  // line and column numbers to be stored explicitly - not embedded in a string
  // - so that we can convert the error locations to be relative to the script,
  // rather than just the single line.
  ErrorMessageOptions orig_error_message_options =
      analyzer_options.error_message_options();

  analyzer_options.set_error_message_mode(ERROR_MESSAGE_WITH_PAYLOAD);
  analyzer_options.set_attach_error_location_payload(true);

  GOOGLESQL_RETURN_IF_ERROR(executor.UpdateAnalyzerOptions(analyzer_options));

  ReferenceDriver::ExecuteStatementAuxOutput aux_output;
  TestDatabase data_base;
  absl::StatusOr<MultiStmtResult> statement_results =
      driver_->ExecuteStatementForReferenceDriverInternal(
          segment.GetSegmentText(), analyzer_options, *parameters_,
          executor.GetCurrentVariables(), executor.GetKnownSystemVariables(),
          options_,
          /*database=*/&data_base,
          /*analyzed_input=*/nullptr, aux_output);

  if (!statement_results.ok()) {
    result_template.result = statement_results.status();
    result_->statement_results.push_back(std::move(result_template));
  } else {
    GOOGLESQL_RET_CHECK(!statement_results->statement_results.empty());
    for (const StatementResult& stmt_result :
         statement_results->statement_results) {
      StatementResult new_result = result_template;
      new_result.result = stmt_result.result;
      result_->statement_results.push_back(std::move(new_result));
    }
  }

  if (aux_output.uses_unsupported_type.has_value()) {
    uses_unsupported_type_ |= *aux_output.uses_unsupported_type;
  }

  if (aux_output.is_deterministic_output.has_value()) {
    is_deterministic_output_ =
        is_deterministic_output_ && *aux_output.is_deterministic_output;
  }

  absl::Status status = AggregateStatuses(statement_results);
  if (!status.ok()) {
    status = MaybeUpdateErrorFromPayload(
        orig_error_message_options, segment.script(),
        absl::Status(googlesql_base::StatusBuilder(status).With(
            ConvertLocalErrorToScriptError(segment))));
  }

  if (!status.ok() && status.code() != absl::StatusCode::kInternal) {
    // Mark this error as handleable
    internal::AttachPayload(&status, ScriptException());
  }
  return status;
}

namespace {
absl::Status ExecuteScriptInternal(ScriptExecutor* executor) {
  while (!executor->IsComplete()) {
    GOOGLESQL_RETURN_IF_ERROR(executor->ExecuteNext());
  }
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<MultiStmtResult>
ReferenceDriver::ExecuteScriptForReferenceDriver(
    absl::string_view sql, const std::map<std::string, Value>& parameters,
    const ExecuteStatementOptions& options,
    ExecuteScriptAuxOutput& aux_output) {
  MultiStmtResult result;
  GOOGLESQL_RETURN_IF_ERROR(ExecuteScriptForReferenceDriverInternal(
      sql, parameters, options, aux_output, &result));
  return result;
}

absl::Status ReferenceDriver::ExecuteScriptForReferenceDriverInternal(
    absl::string_view sql, const std::map<std::string, Value>& parameters,
    const ExecuteStatementOptions& options, ExecuteScriptAuxOutput& aux_output,
    MultiStmtResult* result) {
  std::optional<bool> tmp_uses_unsupported_type;

  procedures_.clear();  // Clear procedures leftover from previous script.
  absl::StatusOr<AnalyzerOptions> analyzer_options =
      GetAnalyzerOptions(parameters, tmp_uses_unsupported_type);
  if (tmp_uses_unsupported_type.has_value()) {
    aux_output.uses_unsupported_type = *tmp_uses_unsupported_type;
  }
  if (!analyzer_options.ok()) {
    return analyzer_options.status();
  }
  ScriptExecutorOptions script_executor_options;
  script_executor_options.PopulateFromAnalyzerOptions(*analyzer_options);
  EvaluatorOptions evaluator_options;
  evaluator_options.type_factory = type_factory();
  evaluator_options.clock = googlesql_base::Clock::RealClock();
  ReferenceDriverStatementEvaluator evaluator(
      *analyzer_options, result, &parameters, options, type_factory(), this,
      evaluator_options);

  // Make table data set up in the [prepare_database] section accessible in
  // evaluation of expressions/queries, which go through the evaluator, rather
  // than ExecuteStatementForReferenceDriver().
  for (const TableInfo& table : tables_) {
    std::vector<std::vector<Value>> data;
    data.reserve(table.array.num_elements());
    for (int i = 0; i < table.array.num_elements(); ++i) {
      data.push_back(table.array.element(i).fields());
    }
    table.table->SetContents(data);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ScriptExecutor> executor,
      ScriptExecutor::Create(sql, script_executor_options, &evaluator));
  absl::Status status = ExecuteScriptInternal(executor.get());
  aux_output.uses_unsupported_type = evaluator.uses_unsupported_type();
  aux_output.is_deterministic_output = evaluator.is_deterministic_output();
  GOOGLESQL_RETURN_IF_ERROR(status);
  return absl::OkStatus();
}

const absl::TimeZone ReferenceDriver::GetDefaultTimeZone() const {
  return default_time_zone_;
}

absl::Status ReferenceDriver::SetDefaultTimeZone(const std::string& time_zone) {
  return googlesql::functions::MakeTimeZone(time_zone, &default_time_zone_);
}

}  // namespace googlesql
