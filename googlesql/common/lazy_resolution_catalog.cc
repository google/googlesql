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

#include "googlesql/common/lazy_resolution_catalog.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/parsed_templated_sql_function.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/proto/internal_fix_suggestion.pb.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/fix_suggestion.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/non_sql_function.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/remote_tvf_factory.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_procedure.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::Status LazyResolutionCatalog::Create(
    absl::string_view source_filename, ModuleDetails module_details,
    const AnalyzerOptions& analyzer_options, TypeFactory* type_factory,
    std::unique_ptr<LazyResolutionCatalog>* lazy_resolution_catalog) {
  std::unique_ptr<LazyResolutionCatalog> new_catalog;
  new_catalog.reset(new LazyResolutionCatalog(source_filename,
                                              std::move(module_details),
                                              analyzer_options, type_factory));
  std::unique_ptr<MultiCatalog> multi_catalog;
  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create(new_catalog->FullName(),
                                       {new_catalog.get()}, &multi_catalog));
  new_catalog->resolution_catalog_ = std::move(multi_catalog);
  *lazy_resolution_catalog = std::move(new_catalog);
  return absl::OkStatus();
}

LazyResolutionCatalog::LazyResolutionCatalog(
    absl::string_view source_filename, ModuleDetails module_details,
    const AnalyzerOptions& analyzer_options, TypeFactory* type_factory)
    : source_filename_(source_filename),
      module_details_(std::move(module_details)),
      analyzer_options_(analyzer_options),
      type_factory_(type_factory) {}

std::string InsertSameNameMultipleTimesErrorMessage(
    absl::string_view type_name, absl::string_view object_name) {
  return absl::StrCat("Inserting the same ", type_name, " ", object_name,
                      " multiple times is not supported");
}

absl::Status LazyResolutionCatalog::AddLazyResolutionConstant(
    std::unique_ptr<LazyResolutionConstant> constant) {
  const std::string constant_name = constant->Name();
  GOOGLESQL_RET_CHECK(constants_.try_emplace(constant_name, std::move(constant)).second)
      << InsertSameNameMultipleTimesErrorMessage("constant", constant_name);
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::AddLazyResolutionFunction(
    std::unique_ptr<LazyResolutionFunction> function) {
  const std::string function_name = function->Name();
  GOOGLESQL_RET_CHECK(functions_.try_emplace(function_name, std::move(function)).second)
      << InsertSameNameMultipleTimesErrorMessage("function", function_name);
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::AddLazyResolutionTableFunction(
    std::unique_ptr<LazyResolutionTableFunction> table_function) {
  const std::string table_function_name = table_function->Name();
  GOOGLESQL_RET_CHECK(table_functions_
                .try_emplace(table_function_name, std::move(table_function))
                .second)
      << InsertSameNameMultipleTimesErrorMessage("table function",
                                                 table_function_name);
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::AddLazyResolutionView(
    std::unique_ptr<LazyResolutionView> view) {
  const std::string view_name = view->Name();
  GOOGLESQL_RET_CHECK(views_.try_emplace(view_name, std::move(view)).second)
      << InsertSameNameMultipleTimesErrorMessage("view", view_name);
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::AddLazyResolutionProcedure(
    std::unique_ptr<LazyResolutionProcedure> procedure) {
  const std::string procedure_name = procedure->Name();
  GOOGLESQL_RET_CHECK(
      procedures_.try_emplace(procedure_name, std::move(procedure)).second)
      << InsertSameNameMultipleTimesErrorMessage("procedure", procedure_name);
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::AppendResolutionCatalog(Catalog* catalog) {
  return resolution_catalog_->AppendCatalog(catalog);
}

bool LazyResolutionCatalog::ContainsConstant(absl::string_view name) const {
  return constants_.contains(name);
}

bool LazyResolutionCatalog::ContainsFunction(absl::string_view name) const {
  return functions_.contains(name);
}

bool LazyResolutionCatalog::ContainsTableFunction(
    absl::string_view name) const {
  return table_functions_.contains(name);
}

bool LazyResolutionCatalog::ContainsView(absl::string_view name) const {
  return views_.contains(name);
}

bool LazyResolutionCatalog::ContainsProcedure(absl::string_view name) const {
  return procedures_.contains(name);
}

// TODO: Determine if we can derive <LazyResolutionObjectType> from
// from <ObjectType> (or vice versa) and eliminate a template argument.
// This may or may not be possible since both types are used in the FindObject
// signature.  A side effect would be that call sites would no longer need to
// specify any template argument since it could be inferred from the <object>
// argument.
//
// Note that this function only returns Catalog interface objects, *not*
// LazyResolution* objects.
template <class ObjectType, class LazyResolutionObjectType>
absl::Status LazyResolutionCatalog::FindObject(
    const absl::Span<const std::string> name_path, const ObjectType** object,
    LookupObjectMethod<LazyResolutionObjectType> lookup_object_method,
    absl::string_view object_type_name, const FindOptions& options) {
  *object = nullptr;
  GOOGLESQL_RET_CHECK_NE(nullptr, options.cycle_detector());
  const LazyResolutionObjectType* local_lazy_object;

  // Note that these errors match those in the default Catalog implementation.
  if (name_path.empty()) {
    return EmptyNamePathInternalError<ObjectType>();
  }

  if (name_path.size() > 1) {
    // LazyResolutionCatalogs cannot have sub-catalogs, so return not found.
    return ObjectNotFoundError<ObjectType>(name_path);
  }
  GOOGLESQL_RETURN_IF_ERROR(
      lookup_object_method(name_path.back(), &local_lazy_object, options));
  // We found the object.  If it's not valid then return.
  GOOGLESQL_RETURN_IF_ERROR(local_lazy_object->resolution_status());
  if (!local_lazy_object->NeedsResolution()) {
    *object = local_lazy_object->ResolvedObject();
    GOOGLESQL_RET_CHECK(*object != nullptr);
    return local_lazy_object->resolution_status();
  }

  // The object has not been resolved yet, so get a mutable version of it that
  // we can resolve.
  LazyResolutionObjectType* lazy_resolution_object =
      const_cast<LazyResolutionObjectType*>(local_lazy_object);

  absl::Status resolution_status;
  // Add this object to the CycleDetector, and if a cycle is detected then
  // return an error.
  {
    const std::string object_name =
        absl::StrCat(FullName(), ".", lazy_resolution_object->Name());

    // TODO: For functions, cycle detection could actually be based
    // on function signatures.  For example, consider function F1 with
    // two signatures where the implementation of the first signature
    // calls the second signature.  Or for another example, consider function
    // F2 signature A references function F3 signature B, and function F3
    // signature C references function F2 signature D, in which case there is
    // no actual cycle.
    CycleDetector::ObjectInfo cycle_detector_object(
        object_name, lazy_resolution_object, options.cycle_detector());
    const absl::Status cycle_status =
        cycle_detector_object.DetectCycle(object_type_name);

    if (!cycle_status.ok()) {
      GOOGLESQL_RET_CHECK_EQ(absl::StatusCode::kInvalidArgument, cycle_status.code());
      return MakeStatusWithErrorLocation(
          cycle_status.code(), cycle_status.message(), source_filename_,
          lazy_resolution_object->SQL(),
          lazy_resolution_object->NameIdentifier());
    }

    AnalyzerOptions analyzer_options(analyzer_options_);
    analyzer_options.set_find_options(options);
    resolution_status = lazy_resolution_object->ResolveAndUpdateIfNeeded(
        analyzer_options, resolution_catalog_.get(), type_factory_);

    // We have fully resolved this object (either successfully or with
    // an error).  As we leave this block, the object will get popped
    // from the cycle detector.
  }
  if (resolution_status.ok()) {
    *object = lazy_resolution_object->ResolvedObject();
    GOOGLESQL_RET_CHECK(*object != nullptr);
  } else {
    GOOGLESQL_RET_CHECK(*object == nullptr);
  }
  return resolution_status;
}

absl::Status LazyResolutionCatalog::LookupLazyResolutionFunctionUnresolved(
    absl::string_view name, const LazyResolutionFunction** function,
    const FindOptions& options) {
  const std::unique_ptr<LazyResolutionFunction>* function_ptr =
      googlesql_base::FindOrNull(functions_, name);
  if (function_ptr == nullptr) {
    return FunctionNotFoundError({std::string(name)});
  }
  *function = function_ptr->get();
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::LookupLazyResolutionTableFunctionUnresolved(
    absl::string_view name, const LazyResolutionTableFunction** table_function,
    const FindOptions& options) {
  const std::unique_ptr<LazyResolutionTableFunction>* table_function_ptr =
      googlesql_base::FindOrNull(table_functions_, name);
  if (table_function_ptr == nullptr) {
    return TableValuedFunctionNotFoundError({std::string(name)});
  }
  *table_function = table_function_ptr->get();
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::LookupLazyResolutionConstantUnresolved(
    absl::string_view name, const LazyResolutionConstant** constant,
    const FindOptions& options) {
  const std::unique_ptr<LazyResolutionConstant>* constant_ptr =
      googlesql_base::FindOrNull(constants_, name);
  if (constant_ptr == nullptr) {
    return ConstantNotFoundError({std::string(name)});
  }
  *constant = constant_ptr->get();
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::LookupLazyResolutionViewUnresolved(
    absl::string_view name, const LazyResolutionView** view,
    const FindOptions& options) {
  const std::unique_ptr<LazyResolutionView>* view_ptr =
      googlesql_base::FindOrNull(views_, name);
  if (view_ptr == nullptr) {
    return TableNotFoundError({std::string(name)});
  }
  *view = view_ptr->get();
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::LookupLazyResolutionProcedureUnresolved(
    absl::string_view name, const LazyResolutionProcedure** procedure,
    const FindOptions& options) {
  const std::unique_ptr<LazyResolutionProcedure>* procedure_ptr =
      googlesql_base::FindOrNull(procedures_, name);
  if (procedure_ptr == nullptr) {
    return ProcedureNotFoundError({std::string(name)});
  }
  *procedure = procedure_ptr->get();
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::FindFunction(
    const absl::Span<const std::string>& name_path, const Function** function,
    const FindOptions& options) {
  return FindObject<Function, LazyResolutionFunction>(
      name_path, function,
      absl::bind_front(
          &LazyResolutionCatalog::LookupLazyResolutionFunctionUnresolved, this),
      "function", options);
}

absl::Status LazyResolutionCatalog::FindTableValuedFunction(
    const absl::Span<const std::string>& name_path,
    const TableValuedFunction** table_function, const FindOptions& options) {
  return FindObject<TableValuedFunction, LazyResolutionTableFunction>(
      name_path, table_function,
      absl::bind_front(
          &LazyResolutionCatalog::LookupLazyResolutionTableFunctionUnresolved,
          this),
      "table function", options);
}

absl::Status LazyResolutionCatalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> name_path, int* num_names_consumed,
    const Constant** constant, const Catalog::FindOptions& options) {
  const absl::Span<const std::string> path_prefix = name_path.subspan(0, 1);
  const absl::Status resolution_status =
      FindObject<Constant, LazyResolutionConstant>(
          path_prefix, constant,
          absl::bind_front(
              &LazyResolutionCatalog::LookupLazyResolutionConstantUnresolved,
              this),
          "constant", options);
  GOOGLESQL_RETURN_IF_ERROR(resolution_status);
  *num_names_consumed += 1;
  GOOGLESQL_RET_CHECK(*constant != nullptr);
  return absl::OkStatus();
}

absl::Status LazyResolutionCatalog::FindTable(
    const absl::Span<const std::string>& name_path, const Table** view,
    const FindOptions& options) {
  // Looking for a LazyResolutionView is sufficient as LazyResolutionCatalogs
  // do not contain any other table (or subclass) object.
  return FindObject<Table, LazyResolutionView>(
      name_path, view,
      absl::bind_front(
          &LazyResolutionCatalog::LookupLazyResolutionViewUnresolved, this),
      "view", options);
}

absl::Status LazyResolutionCatalog::FindProcedure(
    const absl::Span<const std::string>& name_path, const Procedure** procedure,
    const FindOptions& options) {
  return FindObject<Procedure, LazyResolutionProcedure>(
      name_path, procedure,
      absl::bind_front(
          &LazyResolutionCatalog::LookupLazyResolutionProcedureUnresolved,
          this),
      "procedure", options);
}

std::string LazyResolutionCatalog::ObjectsDebugString(bool verbose) const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "LazyResolutionCatalog '", FullName(), "'\n");
  absl::StrAppend(&debug_string, FunctionsDebugString(verbose));
  absl::StrAppend(&debug_string, TableFunctionsDebugString());
  absl::StrAppend(&debug_string, ConstantsDebugString(verbose));
  absl::StrAppend(&debug_string, ProceduresDebugString());
  return debug_string;
}

std::string LazyResolutionCatalog::FunctionsDebugString(bool verbose) const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "LazyResolutionFunctions:\n");
  for (const auto& function : functions_) {
    absl::StrAppend(&debug_string, function.first, ": ",
                    function.second->DebugString(verbose), "\n");
  }
  return debug_string;
}

std::string LazyResolutionCatalog::TableFunctionsDebugString() const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "LazyResolutionTableFunctions:\n");
  for (const auto& table_function : table_functions_) {
    absl::StrAppend(&debug_string, table_function.first, ": ",
                    table_function.second->DebugString(), "\n");
  }
  return debug_string;
}

std::string LazyResolutionCatalog::ConstantsDebugString(bool verbose) const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "LazyResolutionConstants:\n");
  for (const auto& constant : constants_) {
    absl::StrAppend(&debug_string, constant.first, ": ",
                    constant.second->DebugString(verbose), "\n");
  }
  return debug_string;
}

std::string LazyResolutionCatalog::ProceduresDebugString() const {
  std::string debug_string;
  absl::StrAppend(&debug_string, "LazyResolutionProcedures:\n");
  for (const auto& procedure : procedures_) {
    absl::StrAppend(&debug_string, procedure.first, ": ",
                    procedure.second->DebugString(), "\n");
  }
  return debug_string;
}

std::vector<const LazyResolutionFunction*> LazyResolutionCatalog::functions()
    const {
  std::vector<const LazyResolutionFunction*> functions;
  functions.reserve(functions_.size());
  for (const auto& function : functions_) {
    functions.push_back(function.second.get());
  }
  return functions;
}

std::vector<const LazyResolutionTableFunction*>
LazyResolutionCatalog::table_valued_functions() const {
  std::vector<const LazyResolutionTableFunction*> table_functions;
  table_functions.reserve(table_functions_.size());
  for (const auto& table_function : table_functions_) {
    table_functions.push_back(table_function.second.get());
  }
  return table_functions;
}

std::vector<const LazyResolutionConstant*> LazyResolutionCatalog::constants()
    const {
  std::vector<const LazyResolutionConstant*> constants;
  constants.reserve(constants_.size());
  for (const auto& constant : constants_) {
    constants.push_back(constant.second.get());
  }
  return constants;
}

std::vector<const LazyResolutionView*> LazyResolutionCatalog::views() const {
  std::vector<const LazyResolutionView*> views;
  views.reserve(views_.size());
  for (const auto& view : views_) {
    views.push_back(view.second.get());
  }
  return views;
}

std::vector<const LazyResolutionProcedure*> LazyResolutionCatalog::procedures()
    const {
  std::vector<const LazyResolutionProcedure*> procedures;
  procedures.reserve(procedures_.size());
  for (const auto& procedure : procedures_) {
    procedures.push_back(procedure.second.get());
  }
  return procedures;
}

LazyResolutionObject::LazyResolutionObject(
    const ASTIdentifier* object_name,
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status object_status,
    ErrorMessageOptions error_message_options)
    : object_name_(object_name),
      parse_resume_location_(parse_resume_location),
      parse_location_range_(parser_output->statement()->location()),
      parser_output_(std::move(parser_output)) {
  if (object_status.ok()) {
    status_ = std::move(object_status);
  } else {
    status_ = MakeInvalidObjectStatus(object_status, error_message_options);
  }
}

bool LazyResolutionObject::IsPrivate() const {
  const ASTCreateStatement* ast_create_statement =
      parser_output_->statement()->GetAs<ASTCreateStatement>();
  return ast_create_statement->scope() == ASTCreateStatement::PRIVATE;
}

ParseLocationPoint LazyResolutionObject::StartParseLocationPoint() const {
  return ParseLocationPoint::FromByteOffset(
      parse_resume_location_.filename(),
      parse_resume_location_.byte_position());
}

std::string LazyResolutionObject::TypeName(bool capitalized) const {
  std::string type_name = "<unsupported type>";
  if (parser_output_->statement()->node_kind() ==
      AST_CREATE_CONSTANT_STATEMENT) {
    if (capitalized) {
      type_name = "Constant";
    } else {
      type_name = "constant";
    }
  } else if (parser_output_->statement()->node_kind() ==
             AST_CREATE_FUNCTION_STATEMENT) {
    if (capitalized) {
      type_name = "Function";
    } else {
      type_name = "function";
    }
  } else if (parser_output_->statement()->node_kind() ==
             AST_CREATE_TABLE_FUNCTION_STATEMENT) {
    if (capitalized) {
      type_name = "Table function";
    } else {
      type_name = "table function";
    }
  } else if (parser_output_->statement()->node_kind() ==
             AST_CREATE_VIEW_STATEMENT) {
    if (capitalized) {
      type_name = "View";
    } else {
      type_name = "view";
    }
  } else if (parser_output_->statement()->node_kind() ==
             AST_CREATE_PROCEDURE_STATEMENT) {
    if (capitalized) {
      type_name = "Procedure";
    } else {
      type_name = "procedure";
    }
  } else {
    ABSL_DCHECK(false) << "Unsupported statement kind: "
                  << parser_output_->statement()->DebugString();
  }
  return type_name;
}

bool LazyResolutionObject::NeedsResolutionLocked() const {
  return status_.ok() && analyzer_output_ == nullptr;
}

bool LazyResolutionObject::NeedsResolution() const {
  absl::MutexLock lock(&resolution_mutex_);
  return NeedsResolutionLocked();
}

absl::Status LazyResolutionObject::status() const {
  absl::MutexLock lock(&resolution_mutex_);
  return status_;
}

void LazyResolutionObject::set_status(absl::Status status) {
  absl::MutexLock lock(&resolution_mutex_);
  status_ = std::move(status);
}

absl::Status LazyResolutionObject::status_when_resolution_attempted() const {
  absl::MutexLock lock(&resolution_mutex_);
  return status_when_resolution_attempted_;
}

void LazyResolutionObject::set_status_when_resolution_attempted(
    absl::Status status) {
  absl::MutexLock lock(&resolution_mutex_);
  status_when_resolution_attempted_ = std::move(status);
}

const ResolvedCreateStatement* LazyResolutionObject::ResolvedStatement() const {
  absl::MutexLock lock(&resolution_mutex_);
  if (analyzer_output_ == nullptr ||
      analyzer_output_->resolved_statement() == nullptr) {
    return nullptr;
  }
  return analyzer_output_->resolved_statement()
      ->GetAs<const ResolvedCreateStatement>();
}

const std::vector<absl::Status>*
LazyResolutionObject::AnalyzerDeprecationWarnings() const {
  absl::MutexLock lock(&resolution_mutex_);
  if (analyzer_output_ == nullptr) {
    return nullptr;
  }
  return &analyzer_output_->deprecation_warnings();
}

absl::Status LazyResolutionObject::MakeInvalidObjectStatus(
    const absl::Status& analyzer_status,
    ErrorMessageOptions error_message_options) {
  InternalErrorLocation new_error_location = SetErrorSourcesFromStatus(
      MakeInternalErrorLocation(object_name_,
                                parse_resume_location_.filename()),
      analyzer_status, error_message_options.mode, sql());
  absl::Status status = ::googlesql_base::StatusBuilder(analyzer_status.code())
                            .AttachPayload(new_error_location)
                        << TypeName(/*capitalized=*/true) << " "
                        << object_name_->GetAsString() << " is invalid";
  if (internal::HasPayloadWithType<googlesql::InternalErrorFixSuggestions>(
          analyzer_status)) {
    internal::AttachPayload(
        &status,
        internal::GetPayload<InternalErrorFixSuggestions>(analyzer_status));
  }
  if (internal::HasPayloadWithType<googlesql::ErrorFixSuggestions>(
          analyzer_status)) {
    internal::AttachPayload(
        &status, internal::GetPayload<ErrorFixSuggestions>(analyzer_status));
  }
  return MaybeUpdateErrorFromPayload(
      error_message_options, sql(),
      UpdateErrorLocationPayloadWithFilenameIfNotPresent(
          ConvertInternalErrorPayloadsToExternal(status, sql()),
          parse_resume_location_.filename()));
}

absl::Status LazyResolutionObject::AnalyzeStatementIfNeeded(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  if (!NeedsResolution()) {
    return status();
  }
  if (!status_when_resolution_attempted().ok()) {
    set_status(
        MakeInvalidObjectStatus(status_when_resolution_attempted(),
                                analyzer_options.error_message_options()));
    return status();
  }
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  absl::Status analyzer_status = AnalyzeStatementFromParserOutputUnowned(
      &parser_output_, analyzer_options, sql(), catalog, type_factory,
      &analyzer_output);
  absl::MutexLock lock(&resolution_mutex_);
  if (NeedsResolutionLocked()) {
    // If the object still needs resolution (another thread hasn't
    // finished resolution and updated state while we were resolving), then
    // update the <lazy_resolution_object_>'s <status_> and <analyzer_output_>.
    if (analyzer_status.ok()) {
      status_ = std::move(analyzer_status);
      analyzer_output_ = std::move(analyzer_output);
    } else {
      // Ensure that the error message location is properly updated as per
      // the specified Mode.
      status_ = MakeInvalidObjectStatus(
          analyzer_status, analyzer_options.error_message_options());
    }
  } else {
    // Another thread finished resolution and updated the function, so we
    // can discard our work and simply return the function's updated status.
    // When leaving this function, the local <analyzer_output> will be freed.
  }
  return status_;
}

std::string LazyResolutionObject::GetResolvedStatementDebugStringIfPresent(
    bool include_sql) const {
  std::string output;
  if (include_sql) {
    absl::StrAppend(&output, "Statement:\n",
                    GetCreateStatement(/*include_prefix=*/true));
  }
  absl::MutexLock lock(&resolution_mutex_);
  if (analyzer_output_ != nullptr) {
    absl::StrAppend(&output, (!output.empty() ? "\n" : ""),
                    analyzer_output_->resolved_statement()->DebugString());
  }
  return output;
}

absl::string_view LazyResolutionObject::GetCreateStatement(
    bool include_prefix) const {
  ParseLocationRange location_range = parse_location_range_;
  if (include_prefix) {
    // Update the location range start to be the original parse location point
    // where parsing of this statement started.
    location_range.set_start(StartParseLocationPoint());
  }
  return sql().substr(location_range.start().GetByteOffset(),
                      location_range.end().GetByteOffset() -
                          location_range.start().GetByteOffset());
}

absl::StatusOr<std::unique_ptr<LazyResolutionFunction>>
LazyResolutionFunction::Create(const ParseResumeLocation& parse_resume_location,
                               std::unique_ptr<ParserOutput> parser_output,
                               absl::Status function_status,
                               ErrorMessageOptions error_message_options,
                               FunctionEnums::Mode function_mode,
                               ModuleDetails module_details) {
  return CreateImpl(parse_resume_location, std::optional<ParseResumeLocation>(),
                    std::move(parser_output), std::move(function_status),
                    error_message_options, function_mode,
                    std::move(module_details));
}

absl::StatusOr<std::unique_ptr<LazyResolutionFunction>>
LazyResolutionFunction::CreateTemplatedFunction(
    const ParseResumeLocation& parse_resume_location,
    const ParseResumeLocation& templated_expression_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    ErrorMessageOptions error_message_options,
    FunctionEnums::Mode function_mode, ModuleDetails module_details) {
  return CreateImpl(parse_resume_location, templated_expression_resume_location,
                    std::move(parser_output), std::move(function_status),
                    error_message_options, function_mode,
                    std::move(module_details));
}

absl::StatusOr<std::unique_ptr<LazyResolutionFunction>>
LazyResolutionFunction::CreateImpl(
    const ParseResumeLocation& parse_resume_location,
    const std::optional<ParseResumeLocation>&
        templated_expression_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    ErrorMessageOptions error_message_options,
    FunctionEnums::Mode function_mode, ModuleDetails module_details) {
  GOOGLESQL_RET_CHECK_NE(parser_output->statement(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_FUNCTION_STATEMENT,
               parser_output->statement()->node_kind())
      << "LazyResolutionFunctions only support ASTCreateFunctionStatements";
  const ASTCreateFunctionStatement* ast_create_function_statement =
      parser_output->statement()->GetAs<ASTCreateFunctionStatement>();

  GOOGLESQL_RET_CHECK_EQ(1, ast_create_function_statement->function_declaration()
                      ->name()
                      ->num_names())
      << "LazyResolutionFunctions only support functions with single-part "
      << "function names, but found: "
      << ast_create_function_statement->function_declaration()
             ->name()
             ->ToIdentifierPathString();

  const ASTIdentifier* function_name =
      ast_create_function_statement->function_declaration()
          ->name()
          ->first_name();

  return absl::WrapUnique(new LazyResolutionFunction(
      function_name, parse_resume_location,
      templated_expression_resume_location, std::move(parser_output),
      std::move(function_status), error_message_options, function_mode,
      std::move(module_details)));
}

LazyResolutionFunction::LazyResolutionFunction(
    const ASTIdentifier* function_name,
    const ParseResumeLocation& parse_resume_location,
    const std::optional<ParseResumeLocation>&
        templated_expression_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    ErrorMessageOptions error_message_options, FunctionEnums::Mode mode,
    ModuleDetails module_details)
    : mode_(mode),
      templated_expression_resume_location_(
          templated_expression_resume_location),
      lazy_resolution_object_(
          function_name, parse_resume_location, std::move(parser_output),
          std::move(function_status), error_message_options),
      module_details_(std::move(module_details)) {}

std::string LazyResolutionFunction::Name() const {
  return lazy_resolution_object_.name()->GetAsString();
}

absl::Status LazyResolutionFunction::ResolveAndUpdateIfNeeded(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
      "Out of stack space due to deeply nested query during function "
      "resolution");
  GOOGLESQL_RETURN_IF_ERROR(lazy_resolution_object_.AnalyzeStatementIfNeeded(
      analyzer_options, catalog, type_factory));

  auto function_options = std::make_unique<FunctionOptions>();
  // User-defined functions often use CamelCase. Upper casing makes it
  // unreadable.
  function_options->set_uses_upper_case_sql_name(false);
  function_options->set_module_name_from_import(
      module_details_.module_name_from_import());
  if (!this->ResolvedStatement()->language().empty() &&
      absl::AsciiStrToUpper(this->ResolvedStatement()->language()) != "SQL") {
    // If we got to here, then resolution_status() must be ok.
    GOOGLESQL_RET_CHECK_OK(resolution_status());
    function_options->set_sql_name(Name());
    std::unique_ptr<NonSqlFunction> non_sql_function;
    GOOGLESQL_RETURN_IF_ERROR(NonSqlFunction::Create(
        Name(), mode_, {this->ResolvedStatement()->signature()},
        *function_options, module_details_, this->ResolvedStatement(),
        ArgumentNames(), AggregateExpressionList(),
        lazy_resolution_object_.parse_resume_location(), &non_sql_function));
    function_ = std::move(non_sql_function);
  } else if (IsTemplated()) {
    GOOGLESQL_RET_CHECK(templated_expression_resume_location_.has_value());
    auto templated_sql_function = std::make_unique<ParsedTemplatedSQLFunction>(
        this->ResolvedStatement()->name_path(),
        this->ResolvedStatement()->signature(),
        this->ResolvedStatement()->argument_name_list(),
        templated_expression_resume_location_.value(),
        (this->ResolvedStatement()->is_aggregate() ? Function::AGGREGATE
                                                   : Function::SCALAR),
        *function_options, lazy_resolution_object_.parser_output());
    templated_sql_function->set_resolution_catalog(catalog);
    function_ = std::move(templated_sql_function);
  } else {
    // If we got to here, then resolution_status() must be ok.
    GOOGLESQL_RET_CHECK_OK(resolution_status());
    function_options->set_sql_name(Name());
    std::unique_ptr<SQLFunction> sql_function;
    GOOGLESQL_RETURN_IF_ERROR(SQLFunction::Create(
        Name(), mode_, {this->ResolvedStatement()->signature()},
        *function_options, FunctionExpression(), ArgumentNames(),
        AggregateExpressionList(),
        lazy_resolution_object_.parse_resume_location(), &sql_function));
    function_ = std::move(sql_function);
  }
  function_->set_statement_context(statement_context());
  return absl::OkStatus();
}

bool LazyResolutionFunction::NeedsResolution() const {
  return lazy_resolution_object_.NeedsResolution();
}

absl::string_view LazyResolutionFunction::SQL() const {
  return lazy_resolution_object_.sql();
}

const ASTIdentifier* LazyResolutionFunction::NameIdentifier() const {
  return lazy_resolution_object_.name();
}

bool LazyResolutionFunction::IsTemplated() const {
  return templated_expression_resume_location_.has_value();
}

std::string LazyResolutionFunction::DebugString(bool verbose, bool include_ast,
                                                bool include_sql) const {
  // TODO: Remove the reference to kSQLFunctionGroup in
  // this debug output.  It is only here temporarily during refactoring to
  // minimize test differences, and should be removed in a subsequent CL.
  std::string debug_string;
  absl::StrAppend(
      &debug_string, lazy_resolution_object_.IsPrivate() ? "PRIVATE " : "",
      ((function_ != nullptr)
           ? function_->DebugString(verbose)
           : absl::StrCat(
                 SQLFunction::kSQLFunctionGroup, ":", Name(),
                 (resolution_status().ok()
                      ? ""
                      : absl::StrCat("\nERROR: ",
                                     FormatError(resolution_status()))))));

  // TODO: The flags here don't work quite right, since if
  // !<include_ast> then <include_sql> is completely ignored.  Fix this, or
  // rework the flags somehow.
  if (include_ast) {
    absl::StrAppend(
        &debug_string, "\n",
        lazy_resolution_object_.GetResolvedStatementDebugStringIfPresent(
            include_sql));
  }
  const std::vector<absl::Status>* deprecation_warnings =
      AnalyzerDeprecationWarnings();
  if (deprecation_warnings != nullptr) {
    for (const absl::Status& warning : *deprecation_warnings) {
      absl::StrAppend(&debug_string, "\nDEPRECATION WARNING:\n",
                      FormatError(warning));
    }
  }
  return debug_string;
}

absl::Status LazyResolutionFunction::resolution_status() const {
  return lazy_resolution_object_.status();
}

void LazyResolutionFunction::set_status_when_resolution_attempted(
    absl::Status status) {
  lazy_resolution_object_.set_status_when_resolution_attempted(
      std::move(status));
}

const ResolvedCreateFunctionStmt* LazyResolutionFunction::ResolvedStatement()
    const {
  return lazy_resolution_object_.ResolvedStatement()
      ->GetAs<const ResolvedCreateFunctionStmt>();
}

const ResolvedExpr* LazyResolutionFunction::FunctionExpression() const {
  if (ResolvedStatement() == nullptr) {
    return nullptr;
  }
  return ResolvedStatement()->function_expression();
}

std::vector<std::string> LazyResolutionFunction::ArgumentNames() const {
  const ParserOutput* parser_output = lazy_resolution_object_.parser_output();
  const ASTCreateFunctionStatement* ast_create_function_statement =
      parser_output->statement()->GetAs<ASTCreateFunctionStatement>();

  const absl::Span<const ASTFunctionParameter* const>& parameters =
      ast_create_function_statement->function_declaration()
          ->parameters()
          ->parameter_entries();
  std::vector<std::string> argument_names(parameters.size());
  int i = 0;
  for (const ASTFunctionParameter* parameter : parameters) {
    argument_names[i] = parameter->name()->GetAsString();
    ++i;
  }
  return argument_names;
}

const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
LazyResolutionFunction::AggregateExpressionList() const {
  if (mode_ == FunctionEnums::AGGREGATE && ResolvedStatement() != nullptr) {
    return &ResolvedStatement()->aggregate_expression_list();
  }
  return nullptr;
}

const std::vector<absl::Status>*
LazyResolutionFunction::AnalyzerDeprecationWarnings() const {
  return lazy_resolution_object_.AnalyzerDeprecationWarnings();
}

absl::StatusOr<std::unique_ptr<LazyResolutionTableFunction>>
LazyResolutionTableFunction::Create(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    ErrorMessageOptions error_message_options,
    RemoteTvfFactory* remote_tvf_factory, ModuleDetails module_details) {
  return CreateImpl(parse_resume_location, std::optional<ParseResumeLocation>(),
                    std::move(parser_output), std::move(function_status),
                    remote_tvf_factory, std::move(module_details),
                    error_message_options);
}

std::string LazyResolutionTableFunction::Name() const {
  return lazy_resolution_object_.name()->GetAsString();
}

absl::StatusOr<std::unique_ptr<LazyResolutionTableFunction>>
LazyResolutionTableFunction::CreateTemplatedTableFunction(
    const ParseResumeLocation& parse_resume_location,
    const std::optional<ParseResumeLocation>&
        templated_expression_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    RemoteTvfFactory* remote_tvf_factory, ModuleDetails module_details,
    ErrorMessageOptions error_message_options) {
  return CreateImpl(parse_resume_location, templated_expression_resume_location,
                    std::move(parser_output), std::move(function_status),
                    remote_tvf_factory, std::move(module_details),
                    error_message_options);
}

absl::StatusOr<std::unique_ptr<LazyResolutionTableFunction>>
LazyResolutionTableFunction::CreateImpl(
    const ParseResumeLocation& parse_resume_location,
    const std::optional<ParseResumeLocation>&
        templated_expression_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    RemoteTvfFactory* remote_tvf_factory, ModuleDetails module_details,
    ErrorMessageOptions error_message_options) {
  GOOGLESQL_RET_CHECK_NE(parser_output->statement(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_TABLE_FUNCTION_STATEMENT,
               parser_output->statement()->node_kind())
      << "LazyResolutionTableFunctions only support "
      << "ASTCreateTableFunctionStatements";
  const ASTCreateTableFunctionStatement* ast_create_table_function_statement =
      parser_output->statement()->GetAs<ASTCreateTableFunctionStatement>();

  GOOGLESQL_RET_CHECK_EQ(1, ast_create_table_function_statement->function_declaration()
                      ->name()
                      ->num_names())
      << "LazyResolutionTableFunctions only support table functions with "
      << "single-part function names";

  const ASTIdentifier* table_function_name =
      ast_create_table_function_statement->function_declaration()
          ->name()
          ->first_name();
  return absl::WrapUnique(new LazyResolutionTableFunction(
      table_function_name, parse_resume_location,
      templated_expression_resume_location, std::move(parser_output),
      std::move(function_status), remote_tvf_factory, std::move(module_details),
      error_message_options));
}

LazyResolutionTableFunction::LazyResolutionTableFunction(
    const ASTIdentifier* table_function_name,
    const ParseResumeLocation& parse_resume_location,
    const std::optional<ParseResumeLocation>&
        templated_expression_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
    RemoteTvfFactory* remote_tvf_factory, ModuleDetails module_details,
    ErrorMessageOptions error_message_options)
    : remote_tvf_factory_(remote_tvf_factory),
      module_details_(std::move(module_details)),
      templated_expression_resume_location_(
          templated_expression_resume_location),
      lazy_resolution_object_(
          table_function_name, parse_resume_location, std::move(parser_output),
          std::move(function_status), error_message_options) {}

absl::Status LazyResolutionTableFunction::ResolveAndUpdateIfNeeded(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  GOOGLESQL_RETURN_IF_ERROR(lazy_resolution_object_.AnalyzeStatementIfNeeded(
      analyzer_options, catalog, type_factory));
  if (googlesql_base::CaseEqual(ResolvedStatement()->language(), "REMOTE")) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        table_function_,
        remote_tvf_factory_->CreateRemoteTVF(*ResolvedStatement(),
                                             module_details_, catalog),
        _.SetCode(absl::StatusCode::kInvalidArgument).SetPrepend()
            << absl::Substitute(
                   "Failed to create remote TVF $0: ",
                   absl::StrJoin(ResolvedStatement()->name_path(), ".")));
  } else {
    GOOGLESQL_RET_CHECK(googlesql_base::CaseEqual(ResolvedStatement()->language(), "SQL"));
    if (IsTemplated()) {
      GOOGLESQL_RET_CHECK(templated_expression_resume_location_.has_value());
      auto templated_sql_tvf = std::make_unique<TemplatedSQLTVF>(
          this->ResolvedStatement()->name_path(),
          this->ResolvedStatement()->signature(),
          this->ResolvedStatement()->argument_name_list(),
          templated_expression_resume_location_.value());
      templated_sql_tvf->set_resolution_catalog(catalog);
      table_function_ = std::move(templated_sql_tvf);
    } else if (ResolvedStatement()->query()) {
      std::unique_ptr<SQLTableValuedFunction> sql_tvf;
      // TODO: What should we do here if this has an error?
      // Same with all the places here?  Should we set the object's
      // status to error, or return the error?  What is the contract?
      GOOGLESQL_RETURN_IF_ERROR(
          SQLTableValuedFunction::Create(this->ResolvedStatement(), &sql_tvf));
      table_function_ = std::move(sql_tvf);
    }
  }
  // User-defined tvfs often use CamelCase. Upper casing makes it
  // unreadable.
  table_function_->mutable_tvf_options().set_uses_upper_case_sql_name(false);
  table_function_->set_statement_context(statement_context());
  return absl::OkStatus();
}

bool LazyResolutionTableFunction::NeedsResolution() const {
  return lazy_resolution_object_.NeedsResolution();
}

absl::string_view LazyResolutionTableFunction::SQL() const {
  return lazy_resolution_object_.sql();
}

const ASTIdentifier* LazyResolutionTableFunction::NameIdentifier() const {
  return lazy_resolution_object_.name();
}

bool LazyResolutionTableFunction::IsTemplated() const {
  return templated_expression_resume_location_.has_value();
}

std::string LazyResolutionTableFunction::DebugString(bool verbose) const {
  // TODO: Remove 'TVF:' from this debug output.  It is only here
  // temporarily during refactoring to minimize test differences, and should
  // be removed in a subsequent CL.
  std::string debug_string;
  absl::StrAppend(
      &debug_string, lazy_resolution_object_.IsPrivate() ? "PRIVATE " : "",
      "TVF:",
      (table_function_ != nullptr
           ? table_function_->DebugString()
           : absl::StrCat(
                 Name(),
                 (resolution_status().ok()
                      ? ""
                      : absl::StrCat("\nERROR: ",
                                     FormatError(resolution_status()))))));

  const std::vector<absl::Status>* deprecation_warnings =
      AnalyzerDeprecationWarnings();
  if (deprecation_warnings != nullptr) {
    for (const absl::Status& warning : *deprecation_warnings) {
      absl::StrAppend(&debug_string, "\nDEPRECATION WARNING:\n",
                      FormatError(warning));
    }
  }

  return debug_string;
}

std::string LazyResolutionTableFunction::FullDebugString(
    bool include_sql) const {
  const std::string suffix =
      lazy_resolution_object_.GetResolvedStatementDebugStringIfPresent(
          include_sql);
  return absl::StrCat(DebugString(), (!suffix.empty() ? "\n" : ""), suffix);
}

absl::Status LazyResolutionTableFunction::resolution_status() const {
  return lazy_resolution_object_.status();
}

void LazyResolutionTableFunction::set_status_when_resolution_attempted(
    absl::Status status) {
  lazy_resolution_object_.set_status_when_resolution_attempted(
      std::move(status));
}

const ResolvedCreateTableFunctionStmt*
LazyResolutionTableFunction::ResolvedStatement() const {
  return lazy_resolution_object_.ResolvedStatement()
      ->GetAs<const ResolvedCreateTableFunctionStmt>();
}

const std::vector<absl::Status>*
LazyResolutionTableFunction::AnalyzerDeprecationWarnings() const {
  return lazy_resolution_object_.AnalyzerDeprecationWarnings();
}

absl::StatusOr<std::unique_ptr<LazyResolutionConstant>>
LazyResolutionConstant::Create(const ParseResumeLocation& parse_resume_location,
                               std::unique_ptr<ParserOutput> parser_output,
                               absl::Status constant_status,
                               ErrorMessageOptions error_message_options) {
  GOOGLESQL_RET_CHECK_NE(parser_output->statement(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_CONSTANT_STATEMENT,
               parser_output->statement()->node_kind())
      << "LazyResolutionConstants only support ASTCreateConstantStatements";
  const ASTCreateConstantStatement* ast_create_constant_statement =
      parser_output->statement()->GetAs<ASTCreateConstantStatement>();

  GOOGLESQL_RET_CHECK_EQ(1, ast_create_constant_statement->name()->num_names())
      << "LazyResolutionConstants only support named constants with "
         "single-part names";

  const ASTIdentifier* constant_name =
      ast_create_constant_statement->name()->first_name();
  return absl::WrapUnique(new LazyResolutionConstant(
      constant_name, parse_resume_location, std::move(parser_output),
      std::move(constant_status), error_message_options));
}

LazyResolutionConstant::LazyResolutionConstant(
    const ASTIdentifier* constant_name,
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status constant_status,
    ErrorMessageOptions error_message_options)
    : lazy_resolution_object_(
          constant_name, parse_resume_location, std::move(parser_output),
          std::move(constant_status), error_message_options) {}

std::string LazyResolutionConstant::Name() const {
  return lazy_resolution_object_.name()->GetAsString();
}

absl::Status LazyResolutionConstant::SetValue(const Value& value) {
  GOOGLESQL_RET_CHECK_NE(sql_constant_.get(), nullptr);
  GOOGLESQL_RET_CHECK(sql_constant_->needs_evaluation());
  return sql_constant_->SetEvaluationResult(value);
}

const ResolvedExpr* LazyResolutionConstant::constant_expression() const {
  const ResolvedCreateStatement* resolved_create_stmt =
      lazy_resolution_object_.ResolvedStatement();
  if (resolved_create_stmt == nullptr) return nullptr;
  const ResolvedCreateConstantStmt* resolved_create_constant_stmt =
      resolved_create_stmt->GetAs<const ResolvedCreateConstantStmt>();
  if (resolved_create_constant_stmt == nullptr) return nullptr;
  return resolved_create_constant_stmt->expr();
}

absl::Status LazyResolutionConstant::ResolveAndUpdateIfNeeded(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  GOOGLESQL_RETURN_IF_ERROR(lazy_resolution_object_.AnalyzeStatementIfNeeded(
      analyzer_options, catalog, type_factory));
  return SQLConstant::Create(this->ResolvedStatement(), &sql_constant_);
}

const ResolvedCreateConstantStmt* LazyResolutionConstant::ResolvedStatement()
    const {
  const ResolvedCreateStatement* resolved_statement =
      lazy_resolution_object_.ResolvedStatement();
  if (resolved_statement == nullptr) {
    return nullptr;
  }
  return resolved_statement->GetAs<const ResolvedCreateConstantStmt>();
}

bool LazyResolutionConstant::NeedsResolution() const {
  return lazy_resolution_object_.NeedsResolution();
}

bool LazyResolutionConstant::NeedsEvaluation() const {
  // The Constant needs evaluation if it has not been resolved yet, or if it
  // has been resolved successfully but not evaluated yet.
  //
  // Note: As per the LazyResolutionConstant contract, if this Constant
  // has already been resolved then <sql_constant_> is guaranteed to be
  // non-NULL.
  return NeedsResolution() || sql_constant_->needs_evaluation();
}

absl::string_view LazyResolutionConstant::SQL() const {
  return lazy_resolution_object_.sql();
}

const ASTIdentifier* LazyResolutionConstant::NameIdentifier() const {
  return lazy_resolution_object_.name();
}

const Type* LazyResolutionConstant::ResolvedType() const {
  if (constant_expression() != nullptr) {
    return constant_expression()->type();
  }
  return nullptr;
}

std::string LazyResolutionConstant::DebugString(bool verbose) const {
  std::string debug_string =
      absl::StrCat(lazy_resolution_object_.IsPrivate() ? "PRIVATE " : "",
                   "CONSTANT ", Name());
  if (verbose) {
    if (sql_constant_ != nullptr && !sql_constant_->needs_evaluation()) {
      std::string constant_value_string = "Uninitialized value";
      if (sql_constant_->evaluation_result().ok()) {
        constant_value_string =
            sql_constant_->evaluation_result().value().DebugString(verbose);
      }
      absl::StrAppend(&debug_string, "=", constant_value_string);
    } else {
      absl::StrAppend(&debug_string, "=Uninitialized value");
    }
  }
  absl::StrAppend(&debug_string, " (",
                  ResolvedType() == nullptr ? "unknown type"
                                            : ResolvedType()->DebugString(),
                  ")");
  const absl::Status status = resolution_or_evaluation_status();
  if (!status.ok()) {
    if (!resolution_status().ok()) {
      absl::StrAppend(&debug_string, "\nERROR during resolution: ");
    } else {
      absl::StrAppend(&debug_string, "\nERROR during evaluation: ");
    }
    absl::StrAppend(&debug_string, FormatError(MaybeUpdateErrorFromPayload(
                                       ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                                       /*keep_error_location_payload=*/false,
                                       lazy_resolution_object_.sql(), status)));
  }
  return debug_string;
}

std::string LazyResolutionConstant::FullDebugString(bool include_sql) const {
  const std::string suffix =
      lazy_resolution_object_.GetResolvedStatementDebugStringIfPresent(
          include_sql);
  return absl::StrCat(DebugString(/*verbose=*/true),
                      (!suffix.empty() ? "\n" : ""), suffix);
}

absl::Status LazyResolutionConstant::resolution_or_evaluation_status() const {
  GOOGLESQL_RETURN_IF_ERROR(resolution_status());
  if (sql_constant_ != nullptr && !sql_constant_->needs_evaluation()) {
    return sql_constant_->evaluation_result().status();
  }
  return absl::OkStatus();
}

absl::Status LazyResolutionConstant::resolution_status() const {
  return lazy_resolution_object_.status();
}

absl::Status LazyResolutionConstant::set_evaluation_status(
    const absl::Status& status) {
  GOOGLESQL_RET_CHECK_NE(sql_constant_.get(), nullptr);
  GOOGLESQL_RET_CHECK(!status.ok());
  return sql_constant_->SetEvaluationResult(status);
}

absl::StatusOr<std::unique_ptr<LazyResolutionView>> LazyResolutionView::Create(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status view_status,
    ErrorMessageOptions error_message_options) {
  GOOGLESQL_RET_CHECK_NE(parser_output->statement(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_VIEW_STATEMENT,
               parser_output->statement()->node_kind())
      << "LazyResolutionViews only support ASTCreateViewStatements";

  const ASTCreateViewStatement* ast_create_view_statement =
      parser_output->statement()->GetAs<ASTCreateViewStatement>();

  GOOGLESQL_RET_CHECK_EQ(1, ast_create_view_statement->name()->num_names())
      << "LazyResolutionViews only support views with a single-part view name, "
      << "but found: "
      << ast_create_view_statement->name()->ToIdentifierPathString();

  const ASTIdentifier* view_name =
      ast_create_view_statement->name()->first_name();
  return absl::WrapUnique(new LazyResolutionView(
      view_name, parse_resume_location, std::move(parser_output),
      std::move(view_status), error_message_options));
}

LazyResolutionView::LazyResolutionView(
    const ASTIdentifier* view_name,
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status view_status,
    ErrorMessageOptions error_message_options)
    : lazy_resolution_object_(view_name, parse_resume_location,
                              std::move(parser_output), std::move(view_status),
                              error_message_options) {}

std::string LazyResolutionView::Name() const {
  return lazy_resolution_object_.name()->GetAsString();
}

std::string LazyResolutionView::DebugString() const {
  std::string debug_string = absl::StrCat(
      lazy_resolution_object_.IsPrivate() ? "PRIVATE " : "", "VIEW ", Name(),
      (resolution_status().ok()
           ? ""
           : absl::StrCat("\nERROR: ", FormatError(resolution_status()))));
  const std::vector<absl::Status>* deprecation_warnings =
      AnalyzerDeprecationWarnings();
  if (deprecation_warnings != nullptr) {
    for (const absl::Status& warning : *deprecation_warnings) {
      absl::StrAppend(&debug_string, "\nDEPRECATION WARNING:\n",
                      FormatError(warning));
    }
  }
  return debug_string;
}

absl::Status LazyResolutionView::resolution_status() const {
  return lazy_resolution_object_.status();
}

bool LazyResolutionView::NeedsResolution() const {
  return lazy_resolution_object_.NeedsResolution();
}

absl::string_view LazyResolutionView::SQL() const {
  return lazy_resolution_object_.sql();
}

const ASTIdentifier* LazyResolutionView::NameIdentifier() const {
  return lazy_resolution_object_.name();
}

absl::Status LazyResolutionView::ResolveAndUpdateIfNeeded(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  GOOGLESQL_RETURN_IF_ERROR(lazy_resolution_object_.AnalyzeStatementIfNeeded(
      analyzer_options, catalog, type_factory));

  // TODO: Check if using SimpleSQLView suffices or if we need a
  // separate concrete implementation of SQLView interface defined outside of
  // the SimpleCatalog.
  const ResolvedCreateViewStmt* stmt = this->ResolvedStatement();
  std::vector<SimpleSQLView::NameAndType> columns;
  for (const auto& col : stmt->output_column_list()) {
    columns.push_back({.name = col->name(), .type = col->column().type()});
  }
  // TODO: Add validation for SQL security clause for CREATE VIEW
  // statements, either within the analyzer (similar to functions) or while
  // creating lazy resolution view objects.
  // As views created within modules are effectively temporary, module defined
  // views can only have INVOKER rights.
  SimpleSQLView::SqlSecurity security = SQLView::kSecurityInvoker;
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<SimpleSQLView> sql_view,
      SimpleSQLView::Create(Name(), columns, security, stmt->is_value_table(),
                            stmt->query()));
  view_ = std::move(sql_view);
  return absl::OkStatus();
}

const ResolvedCreateViewStmt* LazyResolutionView::ResolvedStatement() const {
  return lazy_resolution_object_.ResolvedStatement()
      ->GetAs<const ResolvedCreateViewStmt>();
}

void LazyResolutionView::set_status_when_resolution_attempted(
    absl::Status status) {
  lazy_resolution_object_.set_status_when_resolution_attempted(
      std::move(status));
}

absl::StatusOr<std::unique_ptr<LazyResolutionProcedure>>
LazyResolutionProcedure::Create(
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status procedure_status,
    ErrorMessageOptions error_message_options, ModuleDetails module_details) {
  GOOGLESQL_RET_CHECK_NE(parser_output->statement(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(AST_CREATE_PROCEDURE_STATEMENT,
               parser_output->statement()->node_kind())
      << "LazyResolutionProcedures only support ASTCreateProcedureStatements";
  const ASTCreateProcedureStatement* ast_create_procedure_statement =
      parser_output->statement()->GetAs<ASTCreateProcedureStatement>();

  GOOGLESQL_RET_CHECK_EQ(1, ast_create_procedure_statement->name()->num_names())
      << "LazyResolutionProcedures only support procedures with single-part "
      << "names";

  const ASTIdentifier* procedure_name =
      ast_create_procedure_statement->name()->first_name();

  return absl::WrapUnique(new LazyResolutionProcedure(
      procedure_name, parse_resume_location, std::move(parser_output),
      std::move(procedure_status), error_message_options,
      std::move(module_details)));
}

LazyResolutionProcedure::LazyResolutionProcedure(
    const ASTIdentifier* procedure_name,
    const ParseResumeLocation& parse_resume_location,
    std::unique_ptr<ParserOutput> parser_output, absl::Status procedure_status,
    ErrorMessageOptions error_message_options, ModuleDetails module_details)
    : lazy_resolution_object_(
          procedure_name, parse_resume_location, std::move(parser_output),
          std::move(procedure_status), error_message_options),
      module_details_(std::move(module_details)) {}

std::string LazyResolutionProcedure::Name() const {
  return lazy_resolution_object_.name()->GetAsString();
}

absl::Status LazyResolutionProcedure::ResolveAndUpdateIfNeeded(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory) {
  GOOGLESQL_RETURN_IF_ERROR(lazy_resolution_object_.AnalyzeStatementIfNeeded(
      analyzer_options, catalog, type_factory));

  const ResolvedCreateProcedureStmt* stmt = this->ResolvedStatement();

  GOOGLESQL_ASSIGN_OR_RETURN(procedure_, SQLProcedure::Create(stmt, module_details_));
  return absl::OkStatus();
}

bool LazyResolutionProcedure::NeedsResolution() const {
  return lazy_resolution_object_.NeedsResolution();
}

std::string_view LazyResolutionProcedure::SQL() const {
  return lazy_resolution_object_.sql();
}

const ASTIdentifier* LazyResolutionProcedure::NameIdentifier() const {
  return lazy_resolution_object_.name();
}

std::string LazyResolutionProcedure::DebugString() const {
  std::string debug_string = absl::StrCat(
      lazy_resolution_object_.IsPrivate() ? "PRIVATE " : "", "PROCEDURE ",
      Name(),
      (resolution_status().ok()
           ? ""
           : absl::StrCat("\nERROR: ", FormatError(resolution_status()))));
  return debug_string;
}

absl::Status LazyResolutionProcedure::resolution_status() const {
  return lazy_resolution_object_.status();
}

const ResolvedCreateProcedureStmt* LazyResolutionProcedure::ResolvedStatement()
    const {
  return lazy_resolution_object_.ResolvedStatement()
      ->GetAs<const ResolvedCreateProcedureStmt>();
}

void LazyResolutionProcedure::set_status_when_resolution_attempted(
    absl::Status status) {
  lazy_resolution_object_.set_status_when_resolution_attempted(
      std::move(status));
}

}  // namespace googlesql
