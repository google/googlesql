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

#include "googlesql/public/modules.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/lazy_resolution_catalog.h"
#include "googlesql/common/resolution_scope.h"
#include "googlesql/common/scope_error_catalog.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/module_factory.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// static
const char ModuleCatalog::kUninitializedModuleName[] = "<unnamed module>";

absl::Status ModuleCatalog::Create(
    const std::vector<std::string>& module_name_from_import,
    const ModuleFactoryOptions& module_factory_options,
    const std::string& module_filename, const std::string& module_contents,
    const AnalyzerOptions& analyzer_options, ModuleFactory* module_factory,
    Catalog* builtin_function_catalog, Catalog* global_scope_catalog,
    TypeFactory* type_factory, std::unique_ptr<ModuleCatalog>* module_catalog) {
  module_catalog->reset(new ModuleCatalog(
      module_name_from_import, module_factory_options, module_filename,
      module_contents, analyzer_options, module_factory,
      builtin_function_catalog, global_scope_catalog, type_factory));
  const absl::Status status = (*module_catalog)->Init();
  if (!status.ok()) {
    module_catalog->reset();
    return status;
  }

  return absl::OkStatus();
}

ModuleCatalog::ModuleCatalog(
    const std::vector<std::string>& module_name_from_import,
    const ModuleFactoryOptions& module_factory_options,
    const std::string& module_filename, const std::string& module_contents,
    const AnalyzerOptions& analyzer_options, ModuleFactory* module_factory,
    Catalog* builtin_function_catalog, Catalog* global_scope_catalog,
    TypeFactory* type_factory)
    : module_name_from_import_(module_name_from_import),
      module_factory_options_(module_factory_options),
      module_filename_(module_filename),
      module_contents_(module_contents),
      analyzer_options_(analyzer_options),
      builtin_function_catalog_(builtin_function_catalog),
      global_scope_catalog_(global_scope_catalog),
      type_factory_(type_factory),
      module_factory_(module_factory) {
  analyzer_options_.set_statement_context(CONTEXT_MODULE);
}

ModuleCatalog::~ModuleCatalog() = default;

absl::Status ModuleCatalog::UpdateStatusForModeAndFilename(
    const absl::Status& status) {
  absl::Status updated_status =
      UpdateErrorLocationPayloadWithFilenameIfNotPresent(status,
                                                         module_filename_);
  return MaybeUpdateErrorFromPayload(analyzer_options_.error_message_options(),
                                     module_contents_, updated_status);
}

absl::Status ModuleCatalog::UpdateAndRegisterError(
    const absl::Status& status, std::vector<absl::Status>* errors) {
  const absl::Status updated_status = UpdateStatusForModeAndFilename(status);
  if (errors != nullptr) {
    errors->push_back(updated_status);
  }
  return updated_status;
}

absl::Status ModuleCatalog::UpdateAndRegisterError(
    const absl::Status& status, const ParseLocationPoint& location,
    std::vector<absl::Status>* errors) {
  absl::Status updated_status =
      StatusWithInternalErrorLocation(status, location);
  updated_status =
      ConvertInternalErrorPayloadsToExternal(updated_status, module_contents_);
  return UpdateAndRegisterError(updated_status, errors);
}

absl::Status ModuleCatalog::MakeAndRegisterError(
    absl::string_view error_string, const ParseLocationPoint& location,
    std::vector<absl::Status>* errors) {
  return UpdateAndRegisterError(MakeSqlError() << error_string, location,
                                errors);
}

void ModuleCatalog::MakeAndRegisterErrorIgnored(
    absl::string_view error_string, const ParseLocationPoint& location,
    std::vector<absl::Status>* errors) {
  MakeAndRegisterError(error_string, location, errors).IgnoreError();
}

absl::Status ModuleCatalog::MakeAndRegisterStatementError(
    absl::string_view error_string, const ASTNode* node,
    std::vector<absl::Status>* errors) {
  ParseLocationPoint location =
      GetErrorLocationPoint(node, /*include_leftmost_child=*/true);
  return MakeAndRegisterError(error_string, location, errors);
}

static std::string UnsupportedOverloadsErrorString(
    absl::string_view object_type, absl::string_view name) {
  return absl::StrCat("Modules do not support ", object_type,
                      " overloads yet but found multiple CREATE ",
                      absl::AsciiStrToUpper(object_type), " statements for ",
                      object_type, " ", name);
}

absl::Status ModuleCatalog::PerformCommonCreateStatementValidation(
    absl::string_view object_type,
    const ASTCreateStatement* ast_create_statement, absl::string_view name) {
  absl::Status status;
  if (!ast_create_statement->is_public() &&
      !ast_create_statement->is_private()) {
    status = MakeAndRegisterStatementError(
        "CREATE statements within modules require the PUBLIC or PRIVATE "
        "modifier",
        ast_create_statement, /*errors=*/nullptr);
  }

  if (ast_create_statement->is_or_replace()) {
    status = MakeAndRegisterStatementError(
        absl::StrCat("Modules do not support CREATE OR REPLACE for CREATE ",
                     absl::AsciiStrToUpper(object_type)),
        ast_create_statement, /*errors=*/nullptr);
  }

  if (ast_create_statement->is_if_not_exists()) {
    status = MakeAndRegisterStatementError(
        absl::StrCat("Modules do not support IF NOT EXISTS for CREATE ",
                     absl::AsciiStrToUpper(object_type)),
        ast_create_statement, /*errors=*/nullptr);
  }
  return status;
}

absl::Status ModuleCatalog::PerformCreateProcedureStatementValidation(
    const ASTCreateProcedureStatement* create_procedure_ast) {
  // Only LANGUAGE REMOTE is supported for procedures inside modules.
  if (create_procedure_ast->language() != nullptr) {
    const absl::string_view language =
        create_procedure_ast->language()->GetAsStringView();
    if (!googlesql_base::CaseEqual(language, "REMOTE")) {
      return MakeAndRegisterStatementError(
          absl::Substitute(
              "Invalid LANGUAGE: $0. Only LANGUAGE REMOTE is supported for "
              "procedures inside modules",
              language),
          create_procedure_ast->language(), /*errors=*/nullptr);
    }
  }

  // Procedures must not have a WITH CONNECTION clause.
  if (create_procedure_ast->with_connection_clause() != nullptr) {
    return MakeAndRegisterStatementError(
        "Procedures inside modules must not have a WITH CONNECTION clause",
        create_procedure_ast->with_connection_clause(), /*errors=*/nullptr);
  }
  return absl::OkStatus();
}

static ParseResumeLocation ComputeExpressionResumeLocation(
    absl::string_view module_filename, const absl::string_view sql,
    const ASTNode* sql_body) {
  absl::string_view trimmed_sql = sql;
  if (sql_body != nullptr) {
    const int expression_end_byte_offset =
        sql_body->location().end().GetByteOffset();
    trimmed_sql = trimmed_sql.substr(0, expression_end_byte_offset);
  }
  ParseResumeLocation expression_resume_location =
      ParseResumeLocation::FromStringView(module_filename, trimmed_sql);
  if (sql_body != nullptr) {
    const int expression_start_byte_offset =
        sql_body->location().start().GetByteOffset();
    expression_resume_location.set_byte_position(expression_start_byte_offset);
  }
  return expression_resume_location;
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromCreateTableFunctionStatement(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output) {
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_TABLE_FUNCTION_STATEMENT,
               parser_output->statement()->node_kind());
  const ASTCreateTableFunctionStatement* create_table_function_ast =
      parser_output->statement()->GetAs<ASTCreateTableFunctionStatement>();
  GOOGLESQL_RET_CHECK_NE(nullptr, create_table_function_ast);

  const std::string object_type_name = "table function";
  const ASTPathExpression* table_function_name_path =
      create_table_function_ast->function_declaration()->name();
  const std::string table_function_name =
      table_function_name_path->ToIdentifierPathString();
  if (table_function_name_path->num_names() > 1) {
    // The object has a multi-part name.  Normally when we have an object with
    // an error, we insert the object into the catalog and store the error
    // with the object.  However, for objects with multi-part names we do
    // not build out the subcatalog(s) needed to insert the object into.  So
    // we just register the error into <module_errors_> and return, since we
    // are finished processing this object.
    MakeAndRegisterStatementError(
        absl::StrCat("Modules do not support creating ", object_type_name,
                     "s with multi-part names or in nested catalogs"),
        table_function_name_path, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  // Generic CREATE statement validation.
  absl::Status table_function_status = PerformCommonCreateStatementValidation(
      object_type_name, create_table_function_ast, table_function_name);

  // CREATE TABLE FUNCTION specific validation.
  if (create_table_function_ast->language() != nullptr) {
    const absl::string_view language =
        create_table_function_ast->language()->GetAsStringView();
    if (!googlesql_base::CaseEqual(language, "REMOTE")) {
      table_function_status = MakeAndRegisterStatementError(
          "Unsupported language type for SQL CREATE TABLE FUNCTION statements "
          "in modules, supported types are [SQL, REMOTE]",
          create_table_function_ast, /*errors=*/nullptr);
    } else if (module_factory_options_.remote_tvf_factory == nullptr) {
      table_function_status = MakeAndRegisterStatementError(
          "REMOTE table function is disabled, please set remote_tvf_factory in "
          "ModuleFactoryOptions",
          create_table_function_ast, /*errors=*/nullptr);
    }
  } else if (create_table_function_ast->code() != nullptr) {
    table_function_status = MakeAndRegisterStatementError(
        "Unsupported language type for SQL CREATE TABLE FUNCTION statements "
        "in modules, supported types are [SQL, REMOTE]",
        create_table_function_ast, /*errors=*/nullptr);
  } else {
    GOOGLESQL_RET_CHECK(create_table_function_ast->query() != nullptr);
  }

  StatusAndResolutionScope status_and_scope =
      GetStatementResolutionScope(create_table_function_ast);
  ResolutionScope resolution_scope = status_and_scope.resolution_scope;
  if (!status_and_scope.status.ok() && table_function_status.ok()) {
    // Above errors are higher priority, so only set if no other errors.
    table_function_status = status_and_scope.status;
  }

  // If the table function already exists, it is currently an error.  SQL
  // TVFs do not currently support overloading.
  //
  // TODO: Allow table function overloading for CREATE TABLE FUNCTION
  // inside a module.  If the table function name already exists then add a
  // new signature (and corresponding SQL) for that function.  If a function
  // is defined as both/ PUBLIC and PRIVATE, it should be an error (functions
  // are not allowed to have both private and public signatures).  For the
  // first cut we can disallow overloading of templated table functions.
  GOOGLESQL_RET_CHECK(internal_catalogs_initialized_);

  // We don't use FindTableFunction() here since that would perform function
  // resolution.  We just want to see if the catalog already contains
  // this function_name.
  if (public_builtin_catalog_->ContainsTableFunction({table_function_name}) ||
      public_global_catalog_->ContainsTableFunction({table_function_name}) ||
      private_builtin_catalog_->ContainsTableFunction({table_function_name}) ||
      private_global_catalog_->ContainsTableFunction({table_function_name})) {
    MakeAndRegisterStatementError(
        UnsupportedOverloadsErrorString(object_type_name, table_function_name),
        create_table_function_ast, &module_errors_)
        .IgnoreError();
    // Return OK rather than <table_function_status>, effectively ignoring this
    // statement.  TODO: Alternatively, if it is more user friendly
    // we could update the TableValuedFunction in the Catalog with this error
    // status.  It is not clear if it is nicer to keep the first working
    // signature, or whether the table function should be completely invalid.
    // The final behavior should be consistent across functions, table functions
    // and named constants.
    return absl::OkStatus();
  }

  // If there was an error or the TVF is not templated, then we create a
  // lazy table function object and add it to the public or private Catalog
  // with the related status.  Note that TVFs with errors are implemented as
  // lazy TVFs internally regardless of whether or not they are templated.
  std::unique_ptr<LazyResolutionTableFunction> lazy_resolution_table_function;
  if (!table_function_status.ok() ||
      !create_table_function_ast->function_declaration()->IsTemplated()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        lazy_resolution_table_function,
        LazyResolutionTableFunction::Create(
            parse_resume_location, std::move(parser_output),
            table_function_status, analyzer_options_.error_message_options(),
            module_factory_options_.remote_tvf_factory, module_details_));
  } else {
    const ParseResumeLocation templated_expression_resume_location =
        ComputeExpressionResumeLocation(parse_resume_location.filename(),
                                        parse_resume_location.input(),
                                        create_table_function_ast->query());
    GOOGLESQL_ASSIGN_OR_RETURN(
        lazy_resolution_table_function,
        LazyResolutionTableFunction::CreateTemplatedTableFunction(
            parse_resume_location, templated_expression_resume_location,
            std::move(parser_output), table_function_status,
            module_factory_options_.remote_tvf_factory, module_details_,
            analyzer_options_.error_message_options()));
  }
  lazy_resolution_table_function->set_statement_context(CONTEXT_MODULE);
  if (resolution_scope == ResolutionScope::kGlobal &&
      global_scope_catalog_ == nullptr) {
    lazy_resolution_table_function->set_status_when_resolution_attempted(
        MakeSqlError() << "Global references are not supported by this "
                          "execution environment");
  }

  // Note: PRIVATE TVFs are added to the private catalogs. All other TVFs
  // are added to the public catalogs (including error TVFs that are neither
  // PUBLIC nor PRIVATE).
  LazyResolutionCatalog* catalog_for_insert = GetLazyResolutionCatalogForInsert(
      create_table_function_ast->is_private(), resolution_scope);
  return catalog_for_insert->AddLazyResolutionTableFunction(
      std::move(lazy_resolution_table_function));
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromCreateFunctionStatement(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output) {
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_FUNCTION_STATEMENT,
               parser_output->statement()->node_kind());
  const ASTCreateFunctionStatement* create_function_ast =
      parser_output->statement()->GetAs<const ASTCreateFunctionStatement>();
  GOOGLESQL_RET_CHECK_NE(nullptr, create_function_ast);

  const std::string object_type_name = "function";
  const ASTPathExpression* function_name_path =
      create_function_ast->function_declaration()->name();
  const std::string function_name =
      function_name_path->ToIdentifierPathString();
  if (function_name_path->num_names() > 1) {
    // The object has a multi-part name.  Normally when we have an object with
    // an error, we insert the object into the catalog and store the error
    // with the object.  However, for objects with multi-part names we do
    // not build out the subcatalog(s) needed to insert the object into.  So
    // we just register the error into <module_errors_> and return, since we
    // are finished processing this object.
    MakeAndRegisterStatementError(
        absl::StrCat("Modules do not support creating ", object_type_name,
                     "s with multi-part names or in nested catalogs"),
        function_name_path, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  // Generic CREATE statement validation.
  absl::Status function_status = PerformCommonCreateStatementValidation(
      object_type_name, create_function_ast, function_name);

  StatusAndResolutionScope status_and_scope =
      GetStatementResolutionScope(create_function_ast);
  ResolutionScope resolution_scope = status_and_scope.resolution_scope;
  if (!status_and_scope.status.ok() && function_status.ok()) {
    // Above errors are higher priority, so only set if no other errors.
    function_status = status_and_scope.status;
  }

  // If the function already exists, it is currently an error.
  //
  // TODO: Allow function overloading for CREATE FUNCTION inside a
  // module.  If the function name already exists then add a new signature
  // for that function.  If a function is defined as both PUBLIC and PRIVATE,
  // it should be an error (functions are not allowed to have both
  // private and public signatures).
  GOOGLESQL_RET_CHECK(internal_catalogs_initialized_);
  if (public_builtin_catalog_->ContainsFunction(function_name) ||
      public_global_catalog_->ContainsFunction(function_name) ||
      private_builtin_catalog_->ContainsFunction(function_name) ||
      private_global_catalog_->ContainsFunction(function_name)) {
    MakeAndRegisterStatementError(
        UnsupportedOverloadsErrorString(object_type_name, function_name),
        create_function_ast, &module_errors_)
        .IgnoreError();
    // Return OK rather than <function_status>, effectively ignoring this
    // statement.  TODO: Alternatively, if it is more user friendly
    // we could update the Function in the Catalog with this error status.
    // It is not clear if it is nicer to keep the first working signature,
    // or whether the function is completely invalid.
    // The final behavior should be consistent across functions, table functions
    // and named constants.
    return absl::OkStatus();
  }

  // If there was an error then we create a lazy function object and add it
  // to the public or private Catalog with the related status.
  const FunctionEnums::Mode function_mode = create_function_ast->is_aggregate()
                                                ? FunctionEnums::AGGREGATE
                                                : FunctionEnums::SCALAR;
  std::unique_ptr<LazyResolutionFunction> lazy_resolution_function;
  if (function_status.ok() &&
      create_function_ast->function_declaration()->IsTemplated()) {
    const ParseResumeLocation templated_expression_resume_location =
        ComputeExpressionResumeLocation(
            parse_resume_location.filename(), parse_resume_location.input(),
            create_function_ast->sql_function_body());
    GOOGLESQL_ASSIGN_OR_RETURN(
        lazy_resolution_function,
        LazyResolutionFunction::CreateTemplatedFunction(
            parse_resume_location, templated_expression_resume_location,
            std::move(parser_output), function_status,
            analyzer_options_.error_message_options(), function_mode,
            module_details_));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        lazy_resolution_function,
        LazyResolutionFunction::Create(
            parse_resume_location, std::move(parser_output), function_status,
            analyzer_options_.error_message_options(), function_mode,
            module_details_));
  }
  lazy_resolution_function->set_statement_context(CONTEXT_MODULE);
  if (resolution_scope == ResolutionScope::kGlobal &&
      global_scope_catalog_ == nullptr) {
    lazy_resolution_function->set_status_when_resolution_attempted(
        MakeSqlError() << "Global references are not supported by this "
                          "execution environment");
  }

  // Note: PRIVATE functions are added to the private catalogs. All other
  // functions are added to the public catalogs (including error functions
  // that are neither PUBLIC nor PRIVATE).
  LazyResolutionCatalog* catalog_for_insert = GetLazyResolutionCatalogForInsert(
      create_function_ast->is_private(), resolution_scope);
  return catalog_for_insert->AddLazyResolutionFunction(
      std::move(lazy_resolution_function));
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromCreateConstantStatement(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output) {
  const ASTCreateConstantStatement* create_constant_ast =
      parser_output->statement()->GetAs<const ASTCreateConstantStatement>();
  GOOGLESQL_RET_CHECK_NE(nullptr, create_constant_ast);

  const std::string object_type_name = "constant";
  const ASTPathExpression* constant_name_path = create_constant_ast->name();
  const std::string constant_name =
      constant_name_path->ToIdentifierPathString();
  if (constant_name_path->num_names() > 1) {
    // The object has a multi-part name.  Normally when we have an object with
    // an error, we insert the object into the catalog and store the error
    // with the object.  However, for objects with multi-part names we do
    // not build out the subcatalog(s) needed to insert the object into.  So
    // we just register the error into <module_errors_> and return, since we
    // are finished processing this object.
    MakeAndRegisterStatementError(
        absl::StrCat("Modules do not support creating ", object_type_name,
                     "s with multi-part names or in nested catalogs"),
        constant_name_path, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  // Generic CREATE statement validation.
  absl::Status constant_status = PerformCommonCreateStatementValidation(
      object_type_name, create_constant_ast, constant_name);

  // It is an error if <constant_name> equals the alias of an imported module.
  // See (broken link).
  if (HasImportedModule(constant_name)) {
    MakeAndRegisterStatementError(
        absl::StrCat("Named constant '", constant_name,
                     "' conflicts with an imported module with the same alias"),
        constant_name_path, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  // It is an error if the constant already exists. Constants can only have
  // builtin scope.
  GOOGLESQL_RET_CHECK(internal_catalogs_initialized_);
  if (public_builtin_catalog_->ContainsConstant(constant_name) ||
      private_builtin_catalog_->ContainsConstant(constant_name)) {
    MakeAndRegisterStatementError(
        absl::StrCat(
            "Constants must have unique names, but found duplicate name ",
            constant_name),
        create_constant_ast, &module_errors_)
        .IgnoreError();
    // Return OK rather than <constant_status>, effectively ignoring this
    // statement.  TODO: Alternatively, if it is more user friendly
    // we could update the Constant in the Catalog with this error status.
    // It is not clear if it is nicer to keep the first working definition,
    // or whether the constant is completely invalid.
    // The final behavior should be consistent across functions, table functions
    // and named constants.
    return absl::OkStatus();
  }

  // The constant does not exist in this Catalog. Create the
  // LazyResolutionConstant and add it to the public or private Catalog as
  // appropriate. Constants can only have builtin scope.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<LazyResolutionConstant> lazy_resolution_constant,
      LazyResolutionConstant::Create(
          parse_resume_location, std::move(parser_output), constant_status,
          analyzer_options_.error_message_options()));

  // Note: PRIVATE constants are added to the private catalog.  All other
  // constants are added to the public catalog (including error constants
  // that are neither PUBLIC nor PRIVATE).
  if (create_constant_ast->is_private()) {
    return private_builtin_catalog_->AddLazyResolutionConstant(
        std::move(lazy_resolution_constant));
  } else {
    return public_builtin_catalog_->AddLazyResolutionConstant(
        std::move(lazy_resolution_constant));
  }
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromCreateViewStatement(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output) {
  const ASTCreateViewStatement* create_view_ast =
      parser_output->statement()->GetAs<const ASTCreateViewStatement>();
  GOOGLESQL_RET_CHECK_NE(nullptr, create_view_ast);

  const ASTPathExpression* view_name_path = create_view_ast->name();
  const std::string view_name = view_name_path->ToIdentifierPathString();
  const std::string object_type = "view";
  if (view_name_path->num_names() > 1) {
    // The object has a multi-part name.  Normally when we have an object with
    // an error, we insert the object into the catalog and store the error
    // with the object. However, for objects with multi-part names we do
    // not build out the subcatalog(s) needed to insert the object into. So
    // we just register the error into <module_errors_> and return, since we
    // are finished processing this object.
    MakeAndRegisterStatementError(
        absl::StrCat("Modules do not support creating ", object_type,
                     "s with multi-part names or in nested catalogs"),
        view_name_path, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  // Generic CREATE statement validation.
  absl::Status view_status = PerformCommonCreateStatementValidation(
      object_type, create_view_ast, view_name);

  // It is an error if the view already exists.
  GOOGLESQL_RET_CHECK(internal_catalogs_initialized_);
  if (public_builtin_catalog_->ContainsView(view_name) ||
      private_builtin_catalog_->ContainsView(view_name) ||
      public_global_catalog_->ContainsView(view_name) ||
      private_global_catalog_->ContainsView(view_name)) {
    MakeAndRegisterStatementError(
        absl::StrCat("Views must have unique names, but found duplicate name ",
                     view_name),
        create_view_ast, &module_errors_)
        .IgnoreError();
    // Return OK rather than `view_status`, effectively ignoring this
    // statement. Alternatively, we could update the View in the Catalog with
    // this error status. It is not clear if it is nicer to keep the first
    // working definition, or whether the view is completely invalid.
    // The final behavior should be consistent across all module defined
    // objects.
    return absl::OkStatus();
  }

  StatusAndResolutionScope status_and_scope =
      GetStatementResolutionScope(create_view_ast);
  ResolutionScope resolution_scope = status_and_scope.resolution_scope;
  // Above errors are higher priority, so only update if there are no other
  // errors.
  if (!status_and_scope.status.ok() && view_status.ok()) {
    view_status = status_and_scope.status;
  }

  // The view does not exist in this Catalog. Create the LazyResolutionView
  // and add it to the appropriate Catalog.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<LazyResolutionView> lazy_resolution_view,
                   LazyResolutionView::Create(
                       parse_resume_location, std::move(parser_output),
                       view_status, analyzer_options_.error_message_options()));

  // Errors that should be surfaced when resolution is attempted are checked
  // based on their relative priority.
  if (!analyzer_options_.language().LanguageFeatureEnabled(
          FEATURE_VIEWS_IN_MODULES)) {
    lazy_resolution_view->set_status_when_resolution_attempted(
        MakeSqlError()
        << "CREATE VIEW statements are not supported in modules");
  } else if (resolution_scope == ResolutionScope::kGlobal &&
             global_scope_catalog_ == nullptr) {
    lazy_resolution_view->set_status_when_resolution_attempted(
        MakeSqlError() << "Global references are not supported by this "
                          "execution environment");
  }

  // Note: PRIVATE Views are added to the private catalogs. All other Views
  // are added to the public catalogs (including error Views that are neither
  // PUBLIC nor PRIVATE).
  LazyResolutionCatalog* catalog_for_insert = GetLazyResolutionCatalogForInsert(
      create_view_ast->is_private(), resolution_scope);
  return catalog_for_insert->AddLazyResolutionView(
      std::move(lazy_resolution_view));
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromCreateProcedureStatement(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output) {
  const ASTCreateProcedureStatement* create_procedure_ast =
      parser_output->statement()->GetAs<const ASTCreateProcedureStatement>();
  GOOGLESQL_RET_CHECK_NE(nullptr, create_procedure_ast);

  const ASTPathExpression* procedure_name_path = create_procedure_ast->name();
  const std::string procedure_name =
      procedure_name_path->ToIdentifierPathString();
  const std::string object_type = "procedure";

  // It is an error if the procedure already exists.
  GOOGLESQL_RET_CHECK(internal_catalogs_initialized_);
  if (public_builtin_catalog_->ContainsProcedure(procedure_name) ||
      private_builtin_catalog_->ContainsProcedure(procedure_name) ||
      public_global_catalog_->ContainsProcedure(procedure_name) ||
      private_global_catalog_->ContainsProcedure(procedure_name)) {
    MakeAndRegisterStatementError(
        absl::StrCat(
            "Procedures must have unique names, but found duplicate name ",
            procedure_name),
        create_procedure_ast->name(), &module_errors_)
        .IgnoreError();
    // Return OK rather than an error status, effectively ignoring this
    // statement. The error has been added to module errors.
    return absl::OkStatus();
  }

  // Generic CREATE statement validation.
  absl::Status procedure_status = PerformCommonCreateStatementValidation(
      object_type, create_procedure_ast, procedure_name);

  // Procedure-specific validation. Above errors are higher priority, so only
  // check if there are no other errors.
  if (procedure_status.ok()) {
    procedure_status =
        PerformCreateProcedureStatementValidation(create_procedure_ast);
  }

  StatusAndResolutionScope status_and_scope =
      GetStatementResolutionScope(create_procedure_ast);
  ResolutionScope resolution_scope = status_and_scope.resolution_scope;
  // Above errors are higher priority, so only use if there are no other errors.
  if (!status_and_scope.status.ok() && procedure_status.ok()) {
    procedure_status = status_and_scope.status;
  }

  // Procedures must effectively have `allowed_references = 'GLOBAL'`.
  if (resolution_scope != ResolutionScope::kGlobal) {
    absl::Status status = MakeAndRegisterStatementError(
        "Procedures defined inside modules must have an effective "
        "'allowed_references' value of 'GLOBAL', set via OPTIONS on the CREATE "
        "PROCEDURE or MODULE statement.",
        create_procedure_ast, /*errors=*/nullptr);
    if (procedure_status.ok()) {
      procedure_status = status;
    }
  }

  // The procedure does not exist in this Catalog. Create the
  // LazyResolutionProcedure and add it to the appropriate Catalog.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<LazyResolutionProcedure> lazy_resolution_procedure,
      LazyResolutionProcedure::Create(
          parse_resume_location, std::move(parser_output), procedure_status,
          analyzer_options_.error_message_options(), module_details_));

  if (global_scope_catalog_ == nullptr) {
    lazy_resolution_procedure->set_status_when_resolution_attempted(
        MakeSqlError() << "Global references are not supported by this "
                          "execution environment");
  }

  // Use language feature to control the rollout of procedures in modules.
  if (!analyzer_options_.language().LanguageFeatureEnabled(
          FEATURE_PROCEDURES_IN_MODULES)) {
    lazy_resolution_procedure->set_status_when_resolution_attempted(
        MakeSqlError()
        << "CREATE PROCEDURE statements are not supported in modules");
  }

  // Note: PRIVATE Procedures are added to the private catalogs. All other
  // Procedures are added to the public catalogs (including error Procedures
  // that are neither PUBLIC nor PRIVATE).
  LazyResolutionCatalog* catalog_for_insert = GetLazyResolutionCatalogForInsert(
      create_procedure_ast->is_private(), resolution_scope);
  return catalog_for_insert->AddLazyResolutionProcedure(
      std::move(lazy_resolution_procedure));
}

ConstantEvaluator* ModuleCatalog::constant_evaluator() const {
  GOOGLESQL_DCHECK_OK(ValidateConstantEvaluatorPrecondition());
  if (analyzer_options_.constant_evaluator() != nullptr) {
    return analyzer_options_.constant_evaluator();
  }
  return module_factory_options_.constant_evaluator;
}

absl::Status ModuleCatalog::ValidateConstantEvaluatorPrecondition() const {
  if (analyzer_options_.constant_evaluator() !=
          module_factory_options_.constant_evaluator &&
      analyzer_options_.constant_evaluator() != nullptr &&
      module_factory_options_.constant_evaluator != nullptr) {
    return absl::InternalError(
        "constant_evaluator in analyzer_options and module_factory_options "
        "must not both be set, or must both point to the same object.");
  }
  return absl::OkStatus();
}

absl::Status ModuleCatalog::InitInternalCatalogs() {
  // We create internal catalogs with the same name as this ModuleCatalog, so
  // that any error message from either public/private catalog indicates this
  // module catalog name.
  GOOGLESQL_RET_CHECK_NE(kUninitializedModuleName, FullName());

  if (resolved_module_stmt() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        module_details_,
        ModuleDetails::Create(FullName(), resolved_module_stmt()->option_list(),
                              constant_evaluator(),
                              module_factory_options_.module_options_overrides,
                              module_name_from_import_));
  }
  GOOGLESQL_RET_CHECK_OK(LazyResolutionCatalog::Create(module_filename_, module_details_,
                                             analyzer_options_, type_factory_,
                                             &public_builtin_catalog_));
  GOOGLESQL_RET_CHECK_OK(LazyResolutionCatalog::Create(module_filename_, module_details_,
                                             analyzer_options_, type_factory_,
                                             &public_global_catalog_));
  GOOGLESQL_RET_CHECK_OK(LazyResolutionCatalog::Create(module_filename_, module_details_,
                                             analyzer_options_, type_factory_,
                                             &private_builtin_catalog_));
  GOOGLESQL_RET_CHECK_OK(LazyResolutionCatalog::Create(module_filename_, module_details_,
                                             analyzer_options_, type_factory_,
                                             &private_global_catalog_));

  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create(
      FullName(), {public_builtin_catalog_.get(), public_global_catalog_.get()},
      &public_catalog_));

  type_import_catalog_ =
      std::make_unique<SimpleCatalog>(FullName(), type_factory_);
  module_import_global_catalog_ = std::make_unique<SimpleCatalog>(FullName());
  module_import_builtin_catalog_ = std::make_unique<SimpleCatalog>(FullName());

  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create(
      FullName(),
      {public_builtin_catalog_.get(), private_builtin_catalog_.get(),
       type_import_catalog_.get(), module_import_builtin_catalog_.get(),
       builtin_function_catalog_},
      &resolution_catalog_builtin_));

  // If global scope catalog is provided, global-scope module objects can
  // reference its contents during resolution. If global scope catalog is not
  // provided, global-scope module objects will produce an error when lookup is
  // attempted, so the contents of `resolution_catalog_global_` are never
  // used for resolution. However, functions such as `ResolveAllStatements()`
  // still reference `resoluction_catalog_global_` for convenience to Find
  // module objects in order to produce those errors, so it needs to be defined.
  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create(
      FullName(),
      {public_builtin_catalog_.get(), public_global_catalog_.get(),
       private_builtin_catalog_.get(), private_global_catalog_.get(),
       type_import_catalog_.get(), module_import_global_catalog_.get()},
      &resolution_catalog_global_));
  if (global_scope_catalog_ != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        resolution_catalog_global_->AppendCatalog(global_scope_catalog_));
  }

  // `scope_error_catalog_` resolves against `resolution_catalog_builtin_`, but
  // uses `resolution_catalog_global_` to produce better error messages when a
  // builtin-scope object attempts to reference a global-scope object.
  GOOGLESQL_ASSIGN_OR_RETURN(
      scope_error_catalog_,
      ScopeErrorCatalog::Create(
          FullName(),
          /*global_references_supported=*/global_scope_catalog_ != nullptr,
          resolution_catalog_builtin_.get(), resolution_catalog_global_.get()));
  GOOGLESQL_RETURN_IF_ERROR(public_builtin_catalog_->AppendResolutionCatalog(
      scope_error_catalog_.get()));
  GOOGLESQL_RETURN_IF_ERROR(private_builtin_catalog_->AppendResolutionCatalog(
      scope_error_catalog_.get()));

  GOOGLESQL_RETURN_IF_ERROR(public_global_catalog_->AppendResolutionCatalog(
      resolution_catalog_global_.get()));
  GOOGLESQL_RETURN_IF_ERROR(private_global_catalog_->AppendResolutionCatalog(
      resolution_catalog_global_.get()));

  internal_catalogs_initialized_ = true;

  return absl::OkStatus();
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromImportStatement(
    std::unique_ptr<ParserOutput> parser_output) {
  GOOGLESQL_RET_CHECK(module_factory_ != nullptr);

  const auto* import_ast =
      parser_output->statement()->GetAs<ASTImportStatement>();

  // We use the builtin_function_catalog_ for resolving IMPORT statement
  // options.
  std::unique_ptr<const AnalyzerOutput> import_statement_analyzer_output;
  absl::Status analyze_status = AnalyzeStatementFromParserOutputUnowned(
      &parser_output, analyzer_options_, module_contents_,
      builtin_function_catalog_, type_factory_,
      &import_statement_analyzer_output);
  if (!analyze_status.ok()) {
    // We parsed the IMPORT statement correctly, but something failed during
    // analysis.  Update the resolver error location, and add the error to
    // <module_errors_>.  Then return OK so that we continue processing the
    // next module statement.
    analyze_status = UpdateStatusForModeAndFilename(analyze_status);
    module_errors_.push_back(analyze_status);
    return absl::OkStatus();
  }

  const ResolvedImportStmt* import_statement =
      import_statement_analyzer_output->resolved_statement()
          ->GetAs<ResolvedImportStmt>();

  switch (import_statement->import_kind()) {
    case ResolvedImportStmt::MODULE:
      GOOGLESQL_RETURN_IF_ERROR(MaybeUpdateCatalogFromImportModuleStatement(
          std::move(parser_output), import_statement));
      break;
    case ResolvedImportStmt::PROTO:
      GOOGLESQL_RETURN_IF_ERROR(MaybeUpdateCatalogFromImportProtoStatement(
          std::move(parser_output), import_statement));
      break;
    default:
      MakeAndRegisterErrorIgnored(
          "Modules only support IMPORT MODULE and IMPORT PROTO, and do not "
          "support importing other object types",
          import_ast->location().start(), &module_errors_);
      break;
  }

  if (module_factory_options_.store_import_stmt_analyzer_output) {
    import_stmt_analyzer_outputs_.push_back(
        std::move(import_statement_analyzer_output));
  }
  return absl::OkStatus();
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromImportModuleStatement(
    std::unique_ptr<ParserOutput> parser_output,
    const ResolvedImportStmt* import_statement) {
  GOOGLESQL_RET_CHECK_EQ(ResolvedImportStmt::MODULE, import_statement->import_kind());

  const auto* import_ast =
      parser_output->statement()->GetAs<ASTImportStatement>();

  // The IMPORT MODULE statement requires the alias path to be exactly size 1.
  // If the alias was not explicitly set in the IMPORT statement, then
  // the resolver provided a default alias that is exactly size 1.
  GOOGLESQL_RET_CHECK_EQ(import_statement->alias_path_size(), 1);

  const std::string& module_alias = import_statement->alias_path().back();
  const std::vector<std::string>& module_name = import_statement->name_path();

  // It is an error if <module_alias> equals the alias of a named constant in
  // scope. See (broken link). Constants can only be builtin-scope
  GOOGLESQL_RET_CHECK(internal_catalogs_initialized_);
  if (public_builtin_catalog_->ContainsConstant(module_alias) ||
      private_builtin_catalog_->ContainsConstant(module_alias)) {
    MakeAndRegisterStatementError(
        absl::StrCat("Module name '", module_alias,
                     "' conflicts with a named constant with the same name"),
        import_ast, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  Catalog* existing_subcatalog;
  // If a subcatalog with this alias exists, provide an error.
  absl::Status get_subcatalog_status =
      module_import_global_catalog_->GetCatalog(
          module_alias, &existing_subcatalog, FindOptions());

  if (existing_subcatalog != nullptr) {
    MakeAndRegisterErrorIgnored(
        absl::StrCat(
            "IMPORT MODULE statements must have unique aliases, but found "
            "duplicate alias ",
            absl::StrJoin(import_statement->alias_path(), ".")),
        import_ast->location().start(), &module_errors_);
    return absl::OkStatus();
  }

  ModuleCatalog* imported_catalog = nullptr;
  absl::Status module_catalog_status =
      module_factory_->CreateOrReturnModuleCatalog(module_name,
                                                   &imported_catalog);
  // If Catalog creation fails for the imported module, then register the
  // error and return OK.
  if (!module_catalog_status.ok()) {
    // TODO: There are several places in this file where we call
    // MakeAndRegisterErrorIgnored(), and use an 'message()' from a
    // absl::Status.  If the original source error has a location, the current
    // code patterns lose it.  In these cases, we should replace usage of
    // 'message()' by building an error with an error source that is the
    // original Status instead.  Alternatively, consider updating the original
    // Status error location info (based on mode and module filename), or add
    // error location info to the Status if not present, and then insert the
    // Status directly into <module_errors_>.
    //
    // Note that for this specific call, the <module_catalog_status> error
    // does not contain a location, so we really do need to add a location
    // here.  This suggests that the code patterns around handling these
    // errors needs additional refactoring.
    module_catalog_status =
        UpdateStatusForModeAndFilename(module_catalog_status);
    MakeAndRegisterErrorIgnored(
        absl::StrCat(
            "IMPORT MODULE ",
            absl::StrJoin(import_statement->alias_path(), "."),
            " failed resolution with error: ", module_catalog_status.message()),
        import_ast->location().start(), &module_errors_);
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(imported_catalog != nullptr);

  // Global-scope objects from this module can reference any public object in
  // the imported module.
  module_import_global_catalog_->AddCatalog(module_alias, imported_catalog);
  // Builtin-scope objects from this module can only reference builtin-scope
  // public objects in the imported module.
  module_import_builtin_catalog_->AddCatalog(
      module_alias, imported_catalog->public_builtin_catalog());
  imported_modules_by_alias_.emplace(
      module_alias,
      ImportModuleInfo(std::move(parser_output), imported_catalog));

  GOOGLESQL_VLOG(1) << "Module " << FullName() << " loaded subcatalog " << module_alias
          << " from module " << imported_catalog->FullName();
  return absl::OkStatus();
}

void ModuleCatalog::AddEnumTypeToCatalog(
    absl::string_view import_file_path, const ParseLocationPoint& location,
    const google::protobuf::EnumDescriptor* enum_descriptor) {
  const EnumType* enum_type;
  // Try to make the enum Type and add it to the Catalog.  If either fails,
  // then register the error in <module_errors_> and continue importing the
  // next enum.
  const absl::Status status =
      type_factory_->MakeEnumType(enum_descriptor, &enum_type);
  if (!status.ok()) {
    MakeAndRegisterErrorIgnored(
        absl::StrCat("During IMPORT PROTO ", import_file_path,
                     ", the creation of enum type ",
                     enum_descriptor->full_name(),
                     " failed with error: ", status.message()),
        location, &module_errors_);
    return;
  }
  // Add the enum type to the <type_import_catalog_>, since this Type can be
  // referenced by objects from either the public or private catalog.
  if (!type_import_catalog_->AddTypeIfNotPresent(enum_descriptor->full_name(),
                                                 enum_type)) {
    MakeAndRegisterErrorIgnored(
        absl::StrCat("During IMPORT PROTO ", import_file_path,
                     ", failed to add enum type ", enum_descriptor->full_name(),
                     " into module catalog ", FullName(),
                     ", with error: ", status.message()),
        location, &module_errors_);
  }
}

void ModuleCatalog::AddProtoTypeToCatalog(
    absl::string_view import_file_path, const ParseLocationPoint& location,
    const google::protobuf::Descriptor* proto_descriptor) {
  const ProtoType* proto_type;
  // Try to make the proto Type and add it to the Catalog.  If either fails,
  // then register the error in <module_errors_> and continue importing the
  // next proto.
  const absl::Status status =
      type_factory_->MakeProtoType(proto_descriptor, &proto_type);
  if (!status.ok()) {
    MakeAndRegisterErrorIgnored(
        absl::StrCat("During IMPORT PROTO ", import_file_path,
                     ", the creation of proto type ",
                     proto_descriptor->full_name(),
                     " failed with error: ", status.message()),
        location, &module_errors_);
    return;
  }
  // Add the proto type to the <type_import_catalog_>, since this Type can be
  // referenced by objects from either the public or private catalog.
  if (!type_import_catalog_->AddTypeIfNotPresent(proto_descriptor->full_name(),
                                                 proto_type)) {
    MakeAndRegisterErrorIgnored(
        absl::StrCat("During IMPORT PROTO ", import_file_path,
                     ", failed to add proto type ",
                     proto_descriptor->full_name(), " into module catalog ",
                     FullName(), ", with error: ", status.message()),
        location, &module_errors_);
  }
}

void ModuleCatalog::AddProtoTypeAndNestedTypesToCatalog(
    const std::string& import_file_path, const ParseLocationPoint& location,
    const google::protobuf::Descriptor* proto_descriptor) {
  AddProtoTypeToCatalog(import_file_path, location, proto_descriptor);
  for (int idx = 0; idx < proto_descriptor->enum_type_count(); ++idx) {
    AddEnumTypeToCatalog(import_file_path, location,
                         proto_descriptor->enum_type(idx));
  }
  for (int idx = 0; idx < proto_descriptor->nested_type_count(); ++idx) {
    AddProtoTypeAndNestedTypesToCatalog(import_file_path, location,
                                        proto_descriptor->nested_type(idx));
  }
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromImportProtoStatement(
    std::unique_ptr<ParserOutput> parser_output,
    const ResolvedImportStmt* import_statement) {
  GOOGLESQL_RET_CHECK_EQ(ResolvedImportStmt::PROTO, import_statement->import_kind());
  const ASTImportStatement* import_ast =
      parser_output->statement()->GetAs<ASTImportStatement>();

  // Inside a module, the IMPORT PROTO statement does not allow an alias since
  // the imported proto type(s) are imported into this module catalog.
  if (import_ast->alias() != nullptr || import_ast->into_alias() != nullptr) {
    std::string alias_string;
    const ASTNode* alias_location = nullptr;
    if (import_ast->alias() != nullptr) {
      alias_string = import_ast->alias()->GetAsString();
      alias_location = import_ast->alias();
    } else {
      alias_string = import_ast->into_alias()->GetAsString();
      alias_location = import_ast->into_alias();
    }
    MakeAndRegisterErrorIgnored(
        absl::StrCat(
            "IMPORT PROTO statements inside modules do not allow AS or "
            "INTO alias, but found alias ",
            alias_string),
        alias_location->location().start(), &module_errors_);
    return absl::OkStatus();
  }

  const google::protobuf::FileDescriptor* proto_file_descriptor;
  absl::Status proto_fetch_status = module_factory_->GetProtoFileDescriptor(
      import_statement->file_path(), &proto_file_descriptor);
  if (!proto_fetch_status.ok()) {
    MakeAndRegisterErrorIgnored(
        absl::StrCat("IMPORT PROTO ", import_statement->file_path(),
                     " failed with error: ", proto_fetch_status.message()),
        import_ast->location().start(), &module_errors_);
    return absl::OkStatus();
  }

  // We have a FileDescriptor.  Get all the top-level enum and message types
  // and create GoogleSQL Types for them.  This currently does not include
  // nested types (from inside messages), or transitively imported types.
  for (int idx = 0; idx < proto_file_descriptor->enum_type_count(); ++idx) {
    AddEnumTypeToCatalog(import_statement->file_path(),
                         import_ast->location().start(),
                         proto_file_descriptor->enum_type(idx));
  }

  for (int idx = 0; idx < proto_file_descriptor->message_type_count(); ++idx) {
    AddProtoTypeAndNestedTypesToCatalog(
        import_statement->file_path(), import_ast->location().start(),
        proto_file_descriptor->message_type(idx));
  }

  return absl::OkStatus();
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromModuleStatement(
    std::unique_ptr<ParserOutput> parser_output) {
  const ASTModuleStatement* module_ast =
      parser_output->statement()->GetAs<const ASTModuleStatement>();
  GOOGLESQL_RET_CHECK_NE(nullptr, module_ast);

  if (module_statement_analyzer_output_ != nullptr) {
    // Register the error and then return OK to continue processing
    // the next statement.
    MakeAndRegisterStatementError("Found more than one MODULE statement",
                                  module_ast, &module_errors_)
        .IgnoreError();
    return absl::OkStatus();
  }

  // We use the builtin_function_catalog for resolving MODULE statement
  // options.
  absl::Status analyze_status = AnalyzeStatementFromParserOutputOwnedOnSuccess(
      &parser_output, analyzer_options_, module_contents_,
      builtin_function_catalog_, type_factory_,
      &module_statement_analyzer_output_);
  if (!analyze_status.ok()) {
    // We parsed the MODULE statement correctly, but something failed
    // during analysis.  Make a new error for the module statement, and
    // include the resolver error information.  Then fall through and
    // return OK so that we continue processing the next statement.
    analyze_status = UpdateStatusForModeAndFilename(analyze_status);
    MakeAndRegisterErrorIgnored(
        absl::StrCat(
            "The MODULE statement in module ",
            absl::StrJoin(module_ast->name()->ToIdentifierVector(), "."),
            " failed resolution with error: ", analyze_status.message()),
        GetErrorLocationPoint(module_ast, /*include_leftmost_child=*/true),
        &module_errors_);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(InitInternalCatalogs());
  }

  return absl::OkStatus();
}

absl::Status ModuleCatalog::MaybeUpdateCatalogFromStatement(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output) {
  if (!internal_catalogs_initialized_ &&
      parser_output->statement()->node_kind() != AST_MODULE_STATEMENT) {
    // If the catalogs are uninitialized then we have not processed a MODULE
    // statement yet.  The MODULE statement must be the first statement in a
    // module, so if this statement is not a MODULE statement then return an
    // error.
    return MakeSqlError()
           << "The first statement in a module must be a MODULE statement";
  }
  switch (parser_output->statement()->node_kind()) {
    case AST_CREATE_CONSTANT_STATEMENT: {
      return MaybeUpdateCatalogFromCreateConstantStatement(
          parse_resume_location, std::move(parser_output));
    }
    case AST_CREATE_FUNCTION_STATEMENT: {
      return MaybeUpdateCatalogFromCreateFunctionStatement(
          parse_resume_location, std::move(parser_output));
    }
    case AST_CREATE_TABLE_FUNCTION_STATEMENT: {
      return MaybeUpdateCatalogFromCreateTableFunctionStatement(
          parse_resume_location, std::move(parser_output));
    }
    case AST_CREATE_VIEW_STATEMENT: {
      return MaybeUpdateCatalogFromCreateViewStatement(
          parse_resume_location, std::move(parser_output));
    }
    case AST_CREATE_PROCEDURE_STATEMENT: {
      return MaybeUpdateCatalogFromCreateProcedureStatement(
          parse_resume_location, std::move(parser_output));
    }
    case AST_MODULE_STATEMENT: {
      return MaybeUpdateCatalogFromModuleStatement(std::move(parser_output));
    }
    case AST_IMPORT_STATEMENT: {
      return MaybeUpdateCatalogFromImportStatement(std::move(parser_output));
    }
    default:
      break;
  }
  // This is an unsupported statement kind.
  MakeAndRegisterStatementError(
      absl::StrCat("Unsupported statement kind: ",
                   parser_output->statement()->GetNodeKindString()),
      parser_output->statement(), &module_errors_)
      .IgnoreError();
  // We ignore <status> and return OK, since we correctly handled this
  // error statement and the error was appropriately added to <module_errors_>.
  return absl::OkStatus();
}

absl::Status ModuleCatalog::Init() {
  GOOGLESQL_RET_CHECK_OK(ValidateConstantEvaluatorPrecondition());
  ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromStringView(module_filename_, module_contents_);
  ParserOptions parser_options = analyzer_options_.GetParserOptions();
  bool is_end_of_input = false;
  while (!is_end_of_input) {
    std::unique_ptr<ParserOutput> parser_output;
    const ParseResumeLocation this_parse_resume_location =
        parse_resume_location;
    absl::Status parse_status =
        ParseNextStatement(&parse_resume_location, parser_options,
                           &parser_output, &is_end_of_input);
    if (!parse_status.ok()) {
      // Parsing failed, so we stop processing this module file because we
      // do not know where the next statement begins.  Initialization has
      // finished successfully so return OK.
      // TODO: Try to find the next semicolon and start re-parsing
      // from there.  We must also consider whether the next semicolon
      // is inside a string or back-ticked identifier.
      UpdateAndRegisterError(parse_status, &module_errors_).IgnoreError();
      // We break here, to fall through and do the check that there is
      // a valid MODULE statement present.
      break;
    }

    GOOGLESQL_RET_CHECK_EQ(module_contents_, this_parse_resume_location.input());

    GOOGLESQL_RETURN_IF_ERROR(MaybeUpdateCatalogFromStatement(this_parse_resume_location,
                                                    std::move(parser_output)));
  }

  if (module_statement_analyzer_output() == nullptr) {
    const absl::Status status =
        MakeSqlError() << "A module must contain exactly one MODULE statement";
    UpdateAndRegisterError(status, &module_errors_).IgnoreError();
  }

  return absl::OkStatus();
}

std::string ModuleCatalog::FullName() const {
  if (resolved_module_stmt() == nullptr) {
    return kUninitializedModuleName;
  }
  return absl::StrJoin(resolved_module_stmt()->name_path(), ".");
}

static Catalog::FindOptions CopyOptionsAndSetCycleDetector(
    const Catalog::FindOptions& options, CycleDetector* cycle_detector) {
  Catalog::FindOptions new_find_options(options);
  new_find_options.set_cycle_detector(cycle_detector);
  return new_find_options;
}

template <class ObjectType, class LazyResolutionObjectType,
          typename FindObjectMethod>
absl::Status ModuleCatalog::FindObject(const absl::Span<const std::string> path,
                                       const ObjectType** object,
                                       FindObjectMethod find_object_method,
                                       const FindOptions& options) {
  if (!internal_catalogs_initialized_) {
    return ObjectNotFoundError<ObjectType>(path);
  }
  *object = nullptr;
  ModuleCatalog::LocalCycleDetector local_cycle_detector(options);
  return find_object_method(
      CopyOptionsAndSetCycleDetector(options, local_cycle_detector.get()));
}

absl::Status ModuleCatalog::FindTable(const absl::Span<const std::string>& path,
                                      const Table** table,
                                      const FindOptions& options) {
  if (path.size() > 1) {
    // A ModuleCatalog never exposes nested objects through Find*() calls.
    // Only objects in this ModuleCatalog's namespace are available for lookup.
    return ObjectNotFoundError<Table>(path);
  }
  // ModuleCatalogs do not contains any other table (or subclass) objects apart
  // from views.
  return FindObject<Table, LazyResolutionView>(
      path, table,
      absl::bind_front(&MultiCatalog::FindTable, public_catalog_.get(), path,
                       table),
      options);
}

absl::Status ModuleCatalog::FindFunction(
    const absl::Span<const std::string>& path, const Function** function,
    const FindOptions& options) {
  if (path.size() > 1) {
    // A ModuleCatalog never exposes nested objects through Find*() calls.
    // Only objects in this ModuleCatalog's namespace are available for lookup.
    return ObjectNotFoundError<Function>(path);
  }
  return FindObject<Function, LazyResolutionFunction>(
      path, function,
      absl::bind_front(&MultiCatalog::FindFunction, public_catalog_.get(), path,
                       function),
      options);
}

absl::Status ModuleCatalog::FindTableValuedFunction(
    const absl::Span<const std::string>& path,
    const TableValuedFunction** function, const FindOptions& options) {
  if (path.size() > 1) {
    // A ModuleCatalog never exposes nested objects through Find*() calls.
    // Only objects in this ModuleCatalog's namespace are available for lookup.
    return ObjectNotFoundError<TableValuedFunction>(path);
  }
  return FindObject<TableValuedFunction, LazyResolutionTableFunction>(
      path, function,
      absl::bind_front(&MultiCatalog::FindTableValuedFunction,
                       public_catalog_.get(), path, function),
      options);
}

absl::Status ModuleCatalog::FindProcedure(
    const absl::Span<const std::string>& path, const Procedure** procedure,
    const FindOptions& options) {
  if (!internal_catalogs_initialized_) {
    return ProcedureNotFoundError(path);
  }
  ModuleCatalog::LocalCycleDetector local_cycle_detector(options);
  return public_catalog_->FindProcedure(
      path, procedure,
      CopyOptionsAndSetCycleDetector(options, local_cycle_detector.get()));
}

absl::Status ModuleCatalog::FindType(const absl::Span<const std::string>& path,
                                     const Type** type,
                                     const FindOptions& options) {
  // Currently, the only way to add types into the Catalog is through the
  // IMPORT PROTO statement inside a module.  All such types are only
  // available to other objects inside the same module, and are not
  // available outside of the module catalog.  So we currently always return
  // NOT_FOUND here.
  //
  // TODO: If we add support for defining types inside the module
  // or exporting imported types, then we will need to call FindType() on
  // the <public_catalog_>.  Currently, all Types are effectively 'private'.
  *type = nullptr;
  return TypeNotFoundError(path);
}

absl::Status ModuleCatalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const Catalog::FindOptions& options) {
  // Prepare for short-circuiting in case of an error.
  *constant = nullptr;
  // Look up the constant.
  // Use a local variable for the Status because the compiler rejects
  // GOOGLESQL_RETURN_IF_ERROR(FindObject...).
  const Constant* local_constant = nullptr;
  const absl::Status find_object_status =
      FindObject<Constant, LazyResolutionConstant>(
          path, &local_constant,
          absl::bind_front(&MultiCatalog::FindConstantWithPathPrefix,
                           public_catalog_.get(), path, num_names_consumed,
                           &local_constant),
          options);
  GOOGLESQL_RETURN_IF_ERROR(find_object_status);

  GOOGLESQL_RET_CHECK(local_constant->template Is<SQLConstant>())
      << local_constant->DebugString();
  const SQLConstant* const_sql_constant =
      local_constant->template GetAs<SQLConstant>();

  if (!const_sql_constant->needs_evaluation()) {
    // If the Constant has an error evaluation Status, then return that Status.
    GOOGLESQL_RETURN_IF_ERROR(const_sql_constant->evaluation_result().status());
    // Otherwise return the SQLConstant, which is guaranteed to have a valid
    // Value.
    GOOGLESQL_RET_CHECK(const_sql_constant->evaluation_result().value().is_valid());
    *constant = const_sql_constant;
    return absl::OkStatus();
  }

  // Check if the constant expression can be evaluated.  If not, return this
  // constant unevaluated.
  // TODO: Consider removing this, and simply GOOGLESQL_RET_CHECK that
  // the constant_evaluator is not NULL.  Returning OK when the Constant
  // is unevaluated no longer seems useful (as it was when initially
  // developing this feature).
  if (constant_evaluator() == nullptr) {
    *constant = const_sql_constant;
    return absl::OkStatus();
  }

  // Evaluate the constant expression, store the result (or error) in the
  // constant and return its status.
  const absl::StatusOr<Value> constant_status_or_value =
      constant_evaluator()->Evaluate(
          *const_sql_constant->constant_expression());
  SQLConstant* sql_constant = const_cast<SQLConstant*>(const_sql_constant);
  GOOGLESQL_RETURN_IF_ERROR(sql_constant->SetEvaluationResult(constant_status_or_value));
  if (constant_status_or_value.ok()) {
    *constant = sql_constant;
  }
  return sql_constant->evaluation_result().status();
}

bool ModuleCatalog::HasFunctions() const {
  if (!internal_catalogs_initialized_) {
    return false;
  }
  return !public_builtin_catalog_->functions().empty() ||
         !public_global_catalog_->functions().empty() ||
         !private_builtin_catalog_->functions().empty() ||
         !private_global_catalog_->functions().empty();
}

bool ModuleCatalog::HasTableFunctions() const {
  if (!internal_catalogs_initialized_) {
    return false;
  }
  return !public_builtin_catalog_->table_valued_functions().empty() ||
         !public_global_catalog_->table_valued_functions().empty() ||
         !private_builtin_catalog_->table_valued_functions().empty() ||
         !private_global_catalog_->table_valued_functions().empty();
}

bool ModuleCatalog::HasViews() const {
  if (!internal_catalogs_initialized_) {
    return false;
  }
  return !public_builtin_catalog_->views().empty() ||
         !public_global_catalog_->views().empty() ||
         !private_builtin_catalog_->views().empty() ||
         !private_global_catalog_->views().empty();
}

bool ModuleCatalog::HasProcedures() const {
  if (!internal_catalogs_initialized_) {
    return false;
  }
  return !public_builtin_catalog_->procedures().empty() ||
         !public_global_catalog_->procedures().empty() ||
         !private_builtin_catalog_->procedures().empty() ||
         !private_global_catalog_->procedures().empty();
}

bool ModuleCatalog::HasConstants() const {
  if (!internal_catalogs_initialized_) {
    return false;
  }
  // Check only the builtin catalogs as constants can only have builtin scope.
  return (!public_builtin_catalog_->constants().empty() ||
          !private_builtin_catalog_->constants().empty());
}

bool ModuleCatalog::HasTypes() const {
  return (internal_catalogs_initialized_ &&
          !type_import_catalog_->types().empty());
}

bool ModuleCatalog::HasImportedModule(const std::string& alias) const {
  return googlesql_base::ContainsKey(imported_modules_by_alias_, alias);
}

bool ModuleCatalog::HasGlobalScopeObjects() const {
  if (!internal_catalogs_initialized_) {
    return false;
  }
  return (!public_global_catalog_->functions().empty() ||
          !private_global_catalog_->functions().empty() ||
          !public_global_catalog_->table_valued_functions().empty() ||
          !private_global_catalog_->table_valued_functions().empty());
}

std::string ModuleCatalog::DebugString(bool include_module_contents) const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "Module '", FullName(), "'\n");
  if (include_module_contents) {
    absl::StrAppend(&debug_string, "Module contents:\n", module_contents_);
  }
  if (HasFunctions()) {
    absl::StrAppend(&debug_string, "\nInitialized functions:\n");
    absl::StrAppend(&debug_string, FunctionsDebugString(/*full=*/false,
                                                        /*verbose=*/true));
  }
  if (HasTableFunctions()) {
    absl::StrAppend(&debug_string, "\nInitialized table functions:\n");
    absl::StrAppend(&debug_string, TableFunctionsDebugString());
  }
  if (HasViews()) {
    absl::StrAppend(&debug_string, "\nInitialized views:\n");
    absl::StrAppend(&debug_string, ViewsDebugString());
  }
  if (HasProcedures()) {
    absl::StrAppend(&debug_string, "\nInitialized procedures:\n");
    absl::StrAppend(&debug_string, ProceduresDebugString());
  }
  if (HasConstants()) {
    absl::StrAppend(&debug_string, "\nInitialized constants:\n");
    absl::StrAppend(&debug_string, ConstantsDebugString(/*verbose=*/true));
  }
  if (HasTypes()) {
    absl::StrAppend(&debug_string, "\nImported Types:\n");
    absl::StrAppend(&debug_string, TypesDebugString());
  }
  absl::StrAppend(&debug_string, "\nInitialization errors:\n",
                  ModuleErrorsDebugString());
  if (!imported_modules_by_alias_.empty()) {
    absl::StrAppend(&debug_string, "\n\nImported module info:\n",
                    ImportedModulesDebugString(include_module_contents));
  }
  return debug_string;
}

std::string ModuleCatalog::ImportedModulesDebugString(
    bool include_module_contents) const {
  std::string debug_string;
  for (const auto& imported_module_entry : imported_modules_by_alias_) {
    const std::string& imported_module_alias = imported_module_entry.first;
    const ImportModuleInfo& imported_module = imported_module_entry.second;
    std::string imported_module_debug_string =
        imported_module.module_catalog->DebugString(include_module_contents);
    if (!debug_string.empty()) {
      absl::StrAppend(&debug_string, "\n\n");
    }
    absl::StrAppend(&debug_string, "MODULE ",
                    imported_module.module_catalog->FullName(), " imported AS ",
                    imported_module_alias, " by MODULE ", FullName());
    absl::StrAppend(&debug_string, "\n", imported_module_debug_string);
  }
  return debug_string;
}

std::string ModuleCatalog::ObjectsDebugString(bool full, bool verbose,
                                              bool include_types) const {
  std::string debug_string;
  {
    const std::string functions_string = FunctionsDebugString(full, verbose);
    if (!functions_string.empty()) {
      absl::StrAppend(&debug_string, functions_string);
    }
  }
  {
    const std::string table_functions_string = TableFunctionsDebugString();
    if (!table_functions_string.empty()) {
      absl::StrAppend(&debug_string, (!debug_string.empty() ? "\n" : ""),
                      table_functions_string);
    }
  }
  {
    const std::string views_string = ViewsDebugString();
    if (!views_string.empty()) {
      absl::StrAppend(&debug_string, (!debug_string.empty() ? "\n" : ""),
                      views_string);
    }
  }
  {
    const std::string procedures_string = ProceduresDebugString();
    if (!procedures_string.empty()) {
      absl::StrAppend(&debug_string, (!debug_string.empty() ? "\n" : ""),
                      procedures_string);
    }
  }
  {
    const std::string constants_string = ConstantsDebugString(verbose);
    if (!constants_string.empty()) {
      absl::StrAppend(&debug_string, (!debug_string.empty() ? "\n" : ""),
                      constants_string);
    }
  }
  if (include_types) {
    const std::string types_string = TypesDebugString();
    if (!types_string.empty()) {
      absl::StrAppend(&debug_string, (!debug_string.empty() ? "\n" : ""),
                      types_string);
    }
  }
  return debug_string;
}

std::string ModuleCatalog::FunctionsDebugString(bool full, bool verbose) const {
  std::string debug_string;
  // We create an ordered map so that the debug string will be deterministic
  // and can be used in test benchmarks.
  OrderedLazyResolutionFunctionMap ordered_function_map;
  GetOrderedPublicAndPrivateFunctionMap(&ordered_function_map);
  for (const auto& function_entry : ordered_function_map) {
    std::string function_debug_string;
    if (full) {
      function_debug_string = function_entry.second->DebugString(
          /*verbose=*/true, /*include_ast=*/true, /*include_sql=*/verbose);
    } else {
      function_debug_string = function_entry.second->DebugString(verbose);
    }
    absl::StrAppend(&debug_string, (debug_string.empty() ? "" : "\n"),
                    function_debug_string, "\n");
  }
  return debug_string;
}

std::string ModuleCatalog::TableFunctionsDebugString() const {
  std::string debug_string;
  // We create an ordered map so that the debug string will be deterministic
  // and can be used in test benchmarks.
  OrderedLazyResolutionTableFunctionMap ordered_table_function_map;
  GetOrderedPublicAndPrivateTableFunctionMap(&ordered_table_function_map);
  for (const auto& table_function_entry : ordered_table_function_map) {
    absl::StrAppend(&debug_string, (debug_string.empty() ? "" : "\n"),
                    table_function_entry.second->DebugString(), "\n");
  }
  return debug_string;
}

std::string ModuleCatalog::ConstantsDebugString(bool verbose) const {
  std::string debug_string;
  // We create an ordered map so that the debug string will be deterministic
  // and can be used in test benchmarks.
  OrderedLazyResolutionConstantMap ordered_constant_map;
  GetOrderedPublicAndPrivateConstantMap(&ordered_constant_map);
  for (const auto& entry : ordered_constant_map) {
    const std::string constant_debug_string =
        entry.second->DebugString(verbose);
    absl::StrAppend(&debug_string, (debug_string.empty() ? "" : "\n"),
                    constant_debug_string, "\n");
  }
  return debug_string;
}

std::string ModuleCatalog::ViewsDebugString() const {
  std::string debug_string;
  // We create an ordered map so that the debug string will be deterministic
  // and can be used in test benchmarks.
  OrderedLazyResolutionViewMap ordered_view_map;
  GetOrderedPublicAndPrivateViewMap(/*include_global_scope_objects=*/true,
                                    &ordered_view_map);
  for (const auto& entry : ordered_view_map) {
    absl::StrAppend(&debug_string, (debug_string.empty() ? "" : "\n"),
                    entry.second->DebugString(), "\n");
  }
  return debug_string;
}

std::string ModuleCatalog::ProceduresDebugString() const {
  std::string debug_string;
  // We create an ordered map so that the debug string will be deterministic
  // and can be used in test benchmarks.
  OrderedLazyResolutionProcedureMap ordered_procedure_map;
  GetOrderedPublicAndPrivateProcedureMap(&ordered_procedure_map);
  for (const auto& entry : ordered_procedure_map) {
    absl::StrAppend(&debug_string, (debug_string.empty() ? "" : "\n"),
                    entry.second->DebugString(), "\n");
  }
  return debug_string;
}

std::string ModuleCatalog::TypesDebugString() const {
  if (!internal_catalogs_initialized_) {
    return "";
  }
  std::string debug_string;
  // We create an ordered map so that the debug string will be deterministic
  // and can be used in test benchmarks.
  OrderedTypeMap ordered_type_map;
  for (const Type* type : type_import_catalog_->types()) {
    googlesql_base::InsertIfNotPresent(
        &ordered_type_map,
        type->TypeName(analyzer_options_.language().product_mode()), type);
  }
  for (const auto& entry : ordered_type_map) {
    absl::StrAppend(&debug_string, (debug_string.empty() ? "" : "\n"),
                    entry.second->DebugString(), "\n");
  }
  return debug_string;
}

static void StatusFormatter(std::string* out, const absl::Status& status) {
  absl::StrAppend(out, status.ToString());
}

static void ErrorMessageFormatter(std::string* out,
                                  const absl::Status& status) {
  absl::StrAppend(out, status.message());
}

std::string ModuleCatalog::ModuleErrorsDebugString(
    bool include_nested_module_errors,
    bool include_catalog_object_errors) const {
  std::vector<absl::Status> errors;
  AppendModuleErrors(include_nested_module_errors,
                     include_catalog_object_errors, &errors);
  const std::string debug_string =
      absl::StrJoin(errors, "\n\n", StatusFormatter);
  return (debug_string.empty() ? "<no errors>" : debug_string);
}

std::string ModuleCatalog::ModuleErrorsDebugString() const {
  return ModuleErrorsDebugString(/*include_nested_module_errors=*/false,
                                 /*include_catalog_object_errors=*/false);
}

std::string ModuleCatalog::ModuleErrorMessageString() const {
  std::vector<absl::Status> errors;
  AppendModuleErrors(/*include_nested_module_errors=*/false,
                     /*include_catalog_object_errors=*/false, &errors);
  const std::string error_message_string =
      absl::StrJoin(errors, "\n\n", ErrorMessageFormatter);
  return (error_message_string.empty() ? "no errors" : error_message_string);
}

const ResolvedModuleStmt* ModuleCatalog::resolved_module_stmt() const {
  if (module_statement_analyzer_output() != nullptr &&
      module_statement_analyzer_output()->resolved_statement() != nullptr) {
    return module_statement_analyzer_output()
        ->resolved_statement()
        ->GetAs<ResolvedModuleStmt>();
  }
  return nullptr;
}

void ModuleCatalog::ResolveAllStatements(bool include_global_scope_objects) {
  // Resolve all statements by getting the list of module objects and calling
  // the `Find*` function for each of them. The `resolution_catalog_global_` is
  // guaranteed to contain all of the module objects, so perform the lookup
  // there.
  OrderedLazyResolutionFunctionMap ordered_function_map;
  GetOrderedPublicAndPrivateFunctionMap(include_global_scope_objects,
                                        &ordered_function_map);
  for (const auto& function_entry : ordered_function_map) {
    const Function* found_function = nullptr;
    CycleDetector cycle_detector;
    FindOptions find_options(&cycle_detector);
    resolution_catalog_global_
        ->FindFunction({function_entry.first}, &found_function, find_options)
        .IgnoreError();
  }

  OrderedLazyResolutionTableFunctionMap ordered_table_function_map;
  GetOrderedPublicAndPrivateTableFunctionMap(include_global_scope_objects,
                                             &ordered_table_function_map);
  for (const auto& table_function_entry : ordered_table_function_map) {
    const TableValuedFunction* found_table_function = nullptr;
    CycleDetector cycle_detector;
    FindOptions find_options(&cycle_detector);
    resolution_catalog_global_
        ->FindTableValuedFunction({table_function_entry.first},
                                  &found_table_function, find_options)
        .IgnoreError();
  }

  OrderedLazyResolutionViewMap ordered_view_map;
  GetOrderedPublicAndPrivateViewMap(include_global_scope_objects,
                                    &ordered_view_map);
  for (const auto& view_entry : ordered_view_map) {
    const Table* found_view = nullptr;
    CycleDetector cycle_detector;
    FindOptions find_options(&cycle_detector);
    resolution_catalog_global_
        ->FindTable({view_entry.first}, &found_view, find_options)
        .IgnoreError();
  }

  OrderedLazyResolutionProcedureMap ordered_procedure_map;
  GetOrderedPublicAndPrivateProcedureMap(include_global_scope_objects,
                                         &ordered_procedure_map);
  for (const auto& procedure_entry : ordered_procedure_map) {
    const Procedure* found_procedure = nullptr;
    CycleDetector cycle_detector;
    FindOptions find_options(&cycle_detector);
    resolution_catalog_global_
        ->FindProcedure({procedure_entry.first}, &found_procedure, find_options)
        .IgnoreError();
  }

  OrderedLazyResolutionConstantMap ordered_constant_map;
  GetOrderedPublicAndPrivateConstantMap(&ordered_constant_map);
  for (const auto& constant_entry : ordered_constant_map) {
    const Constant* found_constant = nullptr;
    CycleDetector cycle_detector;
    FindOptions find_options(&cycle_detector);
    resolution_catalog_global_
        ->FindConstant({constant_entry.first}, &found_constant, find_options)
        .IgnoreError();
  }

  for (const auto& imported_module_entry : imported_modules_by_alias_) {
    const ImportModuleInfo& imported_module = imported_module_entry.second;
    imported_module.module_catalog->ResolveAllStatements(
        include_global_scope_objects);
  }
}

// Ignores all constants of type other than LazyResolutionConstant. Performs
// evaluation at most once for a given constant and adds the value or any
// evaluation error to the LazyResolutionConstant.
void ModuleCatalog::EvaluateAllConstants() {
  if (constant_evaluator() == nullptr) {
    return;
  }

  OrderedLazyResolutionConstantMap ordered_constant_map;
  GetOrderedPublicAndPrivateConstantMap(&ordered_constant_map);
  // Evaluate each named constant in this catalog, updating its value and
  // status.
  for (const auto& constant_entry : ordered_constant_map) {
    const LazyResolutionConstant* constant = constant_entry.second;
    auto lazy_resolution_constant =
        const_cast<LazyResolutionConstant*>(constant);
    if (!lazy_resolution_constant->resolution_status().ok()) continue;
    if (!lazy_resolution_constant->NeedsEvaluation()) continue;

    // Evaluate the constant expression and update the constant's value and
    // status.
    const absl::StatusOr<Value> constant_status_or_value =
        constant_evaluator()->Evaluate(
            *lazy_resolution_constant->constant_expression());
    if (constant_status_or_value.ok()) {
      // ABSL_CHECK validated: SetValue() will succeed because NeedsEvaluation()
      // is true (checked above).
      // TODO: Do something to remove this last ABSL_CHECK.
      GOOGLESQL_CHECK_OK(lazy_resolution_constant->SetValue(*constant_status_or_value));
    } else {
      // Add the location of the CREATE CONSTANT statement as the error
      // location. The ConstantEvaluator did not add an error location.
      const absl::Status updated_constant_status = MakeStatusWithErrorLocation(
          constant_status_or_value.status().code(),
          constant_status_or_value.status().message(), module_filename_,
          module_contents_, lazy_resolution_constant->NameIdentifier(),
          /*include_leftmost_child=*/true);
      // ABSL_CHECK validated: set_evaluation_status() will succeed because
      // NeedsEvaluation() is true (checked above).
      // TODO: Do something to remove this ABSL_CHECK.
      GOOGLESQL_CHECK_OK(lazy_resolution_constant->set_evaluation_status(
          UpdateStatusForModeAndFilename(updated_constant_status)));
    }
  }

  for (const auto& imported_module_entry : imported_modules_by_alias_) {
    const ImportModuleInfo& imported_module = imported_module_entry.second;
    imported_module.module_catalog->EvaluateAllConstants();
  }
}

bool ModuleCatalog::HasErrors(bool include_nested_module_errors,
                              bool include_catalog_object_errors) const {
  // TODO: Make this more efficient.  We can detect errors without
  // building the entire list (and return true early when we find the first
  // error).  We could also have a flag that we turn on once we first notice
  // an error, so that once we found an error we never have to recalculate it
  // (but if the flag is false we still need to recalculate, since an object
  // could have been added to the catalog that introduces an error since the
  // flag was last set to false).
  std::vector<absl::Status> errors;
  AppendModuleErrors(include_nested_module_errors,
                     include_catalog_object_errors, &errors);
  if (!errors.empty()) {
    return true;
  }
  return false;
}

void ModuleCatalog::AppendModuleErrors(
    bool include_nested_module_errors, bool include_catalog_object_errors,
    std::vector<absl::Status>* errors) const {
  std::set<const ModuleCatalog*> appended_modules;
  AppendModuleErrorsImpl(include_nested_module_errors,
                         include_catalog_object_errors, &appended_modules,
                         errors);
}

void ModuleCatalog::GetOrderedPublicAndPrivateFunctionMap(
    bool include_global_scope_objects,
    OrderedLazyResolutionFunctionMap* ordered_function_map) const {
  if (!internal_catalogs_initialized_) {
    return;
  }
  std::vector<const LazyResolutionCatalog*> catalogs = {
      public_builtin_catalog_.get(), private_builtin_catalog_.get()};
  if (include_global_scope_objects) {
    catalogs.push_back(public_global_catalog_.get());
    catalogs.push_back(private_global_catalog_.get());
  }
  for (const LazyResolutionCatalog* catalog : catalogs) {
    for (const LazyResolutionFunction* function : catalog->functions()) {
      googlesql_base::InsertIfNotPresent(ordered_function_map, function->Name(), function);
    }
  }
}

void ModuleCatalog::GetOrderedPublicAndPrivateTableFunctionMap(
    bool include_global_scope_objects,
    OrderedLazyResolutionTableFunctionMap* ordered_table_function_map) const {
  if (!internal_catalogs_initialized_) {
    return;
  }
  std::vector<const LazyResolutionCatalog*> catalogs = {
      public_builtin_catalog_.get(), private_builtin_catalog_.get()};
  if (include_global_scope_objects) {
    catalogs.push_back(public_global_catalog_.get());
    catalogs.push_back(private_global_catalog_.get());
  }
  for (const LazyResolutionCatalog* catalog : catalogs) {
    for (const LazyResolutionTableFunction* table_function :
         catalog->table_valued_functions()) {
      googlesql_base::InsertIfNotPresent(ordered_table_function_map,
                              table_function->Name(), table_function);
    }
  }
}

void ModuleCatalog::GetOrderedPublicAndPrivateConstantMap(
    OrderedLazyResolutionConstantMap* ordered_constant_map) const {
  if (!internal_catalogs_initialized_) {
    return;
  }
  // Check only the builtin catalogs as constants can only have builtin scope.
  for (const LazyResolutionCatalog* catalog :
       {public_builtin_catalog_.get(), private_builtin_catalog_.get()}) {
    for (const LazyResolutionConstant* constant : catalog->constants()) {
      googlesql_base::InsertIfNotPresent(ordered_constant_map, constant->Name(), constant);
    }
  }
}

void ModuleCatalog::GetOrderedPublicAndPrivateViewMap(
    bool include_global_scope_objects,
    OrderedLazyResolutionViewMap* ordered_view_map) const {
  if (!internal_catalogs_initialized_) {
    return;
  }
  std::vector<const LazyResolutionCatalog*> catalogs = {
      public_builtin_catalog_.get(), private_builtin_catalog_.get()};
  if (include_global_scope_objects) {
    catalogs.push_back(public_global_catalog_.get());
    catalogs.push_back(private_global_catalog_.get());
  }
  for (const LazyResolutionCatalog* catalog : catalogs) {
    for (const LazyResolutionView* view : catalog->views()) {
      googlesql_base::InsertIfNotPresent(ordered_view_map, view->Name(), view);
    }
  }
}

void ModuleCatalog::GetOrderedPublicAndPrivateProcedureMap(
    bool include_global_scope_objects,
    OrderedLazyResolutionProcedureMap* ordered_procedure_map) const {
  if (!internal_catalogs_initialized_) {
    return;
  }
  std::vector<const LazyResolutionCatalog*> catalogs = {
      public_builtin_catalog_.get(), private_builtin_catalog_.get()};
  if (include_global_scope_objects) {
    catalogs.push_back(public_global_catalog_.get());
    catalogs.push_back(private_global_catalog_.get());
  }

  for (const LazyResolutionCatalog* catalog : catalogs) {
    for (const LazyResolutionProcedure* procedure : catalog->procedures()) {
      googlesql_base::InsertIfNotPresent(ordered_procedure_map, procedure->Name(),
                              procedure);
    }
  }
}

void ModuleCatalog::AppendModuleErrorsImpl(
    bool include_nested_module_errors, bool include_catalog_object_errors,
    std::set<const ModuleCatalog*>* appended_modules,
    std::vector<absl::Status>* errors) const {
  for (const absl::Status& error : module_errors()) {
    errors->push_back(error);
  }
  if (include_catalog_object_errors) {
    OrderedLazyResolutionFunctionMap ordered_function_map;
    GetOrderedPublicAndPrivateFunctionMap(&ordered_function_map);
    for (const auto& function_entry : ordered_function_map) {
      if (!function_entry.second->resolution_status().ok()) {
        // TODO: Remove these status updates, they should no longer
        // be necessary when we proactively update the payload inside the
        // object status itself.
        //
        // Note that an error Status associated with a lazy object has
        // an ErrorLocation payload, so we update the status based
        // on our error message options if necessary before adding the
        // Status to <errors>.
        absl::Status updated_status = MaybeUpdateErrorFromPayload(
            analyzer_options_.error_message_options(), module_contents_,
            function_entry.second->resolution_status());
        errors->push_back(updated_status);
      }
    }
    OrderedLazyResolutionTableFunctionMap ordered_table_function_map;
    GetOrderedPublicAndPrivateTableFunctionMap(&ordered_table_function_map);
    for (const auto& tvf_entry : ordered_table_function_map) {
      if (!tvf_entry.second->resolution_status().ok()) {
        absl::Status updated_status = MaybeUpdateErrorFromPayload(
            analyzer_options_.error_message_options(), module_contents_,
            tvf_entry.second->resolution_status());
        errors->push_back(updated_status);
      }
    }
    OrderedLazyResolutionConstantMap ordered_constant_map;
    GetOrderedPublicAndPrivateConstantMap(&ordered_constant_map);
    for (const auto& constant_entry : ordered_constant_map) {
      if (!constant_entry.second->resolution_or_evaluation_status().ok()) {
        const absl::Status updated_status = MaybeUpdateErrorFromPayload(
            analyzer_options_.error_message_options(), module_contents_,
            constant_entry.second->resolution_or_evaluation_status());
        errors->push_back(updated_status);
      }
    }
    OrderedLazyResolutionProcedureMap ordered_procedure_map;
    GetOrderedPublicAndPrivateProcedureMap(&ordered_procedure_map);
    for (const auto& procedure_entry : ordered_procedure_map) {
      if (!procedure_entry.second->resolution_status().ok()) {
        absl::Status updated_status = MaybeUpdateErrorFromPayload(
            analyzer_options_.error_message_options(), module_contents_,
            procedure_entry.second->resolution_status());
        errors->push_back(updated_status);
      }
    }
  }
  if (include_nested_module_errors) {
    for (const auto& imported_module_entry : imported_modules_by_alias_) {
      const ImportModuleInfo& module_info = imported_module_entry.second;
      if (!googlesql_base::ContainsKey(*appended_modules, module_info.module_catalog)) {
        appended_modules->insert(module_info.module_catalog);
        module_info.module_catalog->AppendModuleErrorsImpl(
            include_nested_module_errors, include_catalog_object_errors,
            appended_modules, errors);
      }
    }
  }
}

ModuleCatalog::StatusAndResolutionScope
ModuleCatalog::GetStatementResolutionScope(const ASTStatement* stmt_ast) const {
  absl::StatusOr<ResolutionScope> scope = GetResolutionScopeOption(
      stmt_ast, module_details_.default_resolution_scope());
  if (!scope.ok()) {
    return {ConvertInternalErrorPayloadsToExternal(scope.status(),
                                                   module_contents_),
            // If there is an error getting the resolution scope, use builtin so
            // error will be surfaced when lookup occurs.
            ResolutionScope::kBuiltin};
  }
  return {scope.status(), *scope};
}

LazyResolutionCatalog* ModuleCatalog::GetLazyResolutionCatalogForInsert(
    bool is_private, ResolutionScope resolution_scope) const {
  if (is_private && resolution_scope == ResolutionScope::kGlobal) {
    return private_global_catalog_.get();
  } else if (is_private && resolution_scope == ResolutionScope::kBuiltin) {
    return private_builtin_catalog_.get();
  } else if (resolution_scope == ResolutionScope::kGlobal) {
    return public_global_catalog_.get();
  } else {
    return public_builtin_catalog_.get();
  }
}

ModuleCatalog::ImportModuleInfo::ImportModuleInfo(
    std::unique_ptr<ParserOutput> import_parser_output_in,
    ModuleCatalog* module_catalog_in)
    : import_parser_output(std::move(import_parser_output_in)),
      module_catalog(module_catalog_in) {}

}  // namespace googlesql
