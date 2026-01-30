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

#ifndef GOOGLESQL_PUBLIC_MODULES_H_
#define GOOGLESQL_PUBLIC_MODULES_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/common/lazy_resolution_catalog.h"
#include "googlesql/common/resolution_scope.h"
#include "googlesql/common/scope_error_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/function.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/procedure.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/case.h"  
#include "absl/base/macros.h"
#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

class ModuleFactory;
struct ModuleFactoryOptions;

typedef absl::btree_map<std::string, const LazyResolutionFunction*,
                        googlesql_base::CaseLess>
    OrderedLazyResolutionFunctionMap;
typedef absl::btree_map<std::string, const LazyResolutionTableFunction*,
                        googlesql_base::CaseLess>
    OrderedLazyResolutionTableFunctionMap;
typedef absl::btree_map<std::string, const LazyResolutionConstant*,
                        googlesql_base::CaseLess>
    OrderedLazyResolutionConstantMap;
typedef absl::btree_map<std::string, const LazyResolutionView*,
                        googlesql_base::CaseLess>
    OrderedLazyResolutionViewMap;
typedef absl::btree_map<std::string, const LazyResolutionProcedure*,
                        googlesql_base::CaseLess>
    OrderedLazyResolutionProcedureMap;
typedef absl::btree_map<std::string, const Type*,
                        googlesql_base::CaseLess>
    OrderedTypeMap;

// ModuleCatalog represents all the objects defined in a module file.
//
// Module file contents are used to initialize the catalog.  During
// initialization, the contents (multiple SQL statements) are parsed and
// validated, and errors are collected.
//
// Once the catalog is initialized, public catalog objects are exposed to the
// caller via the Catalog interface.  Private objects are not available outside
// of this ModuleCatalog.  Private objects can only be referenced by other
// objects defined in the same module file.
//
// Statements in a module can reference 'builtin objects' (such as functions and
// named constants) as provided by the caller. The module catalog enforces name
// scoping in the sense that objects defined in the module itself (or in an
// imported module) take precedence over builtin objects.
//
// Module files can import other module files, and reference public
// functions defined in those module files.
//
// The ModuleCatalog provides 'lazy' resolution of module statements.
// During initialization all statements are parsed, but no statements
// are resolved until a related FindFunction() call is received.
//
// See (broken link) for more information on GoogleSQL
// modules.
//
// TODO: Add more public apis for things that engines will need.
// This likely includes things like fetching all the public and/or private
// functions.  These will be added as we find specific requirements for
// them.
class ModuleCatalog : public Catalog {
 public:
  static const char kUninitializedModuleName[];
  ~ModuleCatalog() override;

  // Not copyable or movable.
  ModuleCatalog(const ModuleCatalog&) = delete;
  ModuleCatalog& operator=(const ModuleCatalog&) = delete;

  // Invokes <module_factory> on module name <module_name_from_import>
  // to fetch module contents and store them into <module_contents_>.  Then
  // creates and initializes a new ModuleCatalog from <module_contents_>, and
  // returns ownership of the created ModuleCatalog in <module_catalog>.
  //
  // TODO: We may want to add more information about the source of
  // the module, for instance where the module comes from (i.e., file
  // location, datascape entity, etc.).  This information could be added to
  // any error messages to provide better source location for the user to
  // troubleshoot problems.
  //
  // During initialization, each semicolon-separated statement in
  // <module_contents_> is parsed and validated.  Valid, supported statements
  // result in ModuleFunctions added to the catalog.  For unsupported/invalid
  // statements, all errors are collected and can be fetched via errors().
  //
  // <module_filename> is just used for error messaging to indicate the
  //     source of the error.
  // <analyzer_options> are used for both initialization (defining the
  //     error message mode) and module statement resolution.
  // <builtin_function_catalog> is the catalog of builtin functions that
  //     can be referenced by module statements.  It should not contain
  //     tables, types, or anything other than builtin functions (no
  //     UDFs, etc.).
  // <type_factory> is used to allocate any Types needed for this catalog.
  //
  // Returns OK if creation succeeds, in which case <module_catalog> is
  // returned.  Creation succeeds as long as all statements are either:
  // 1) valid and added into the Catalog
  // 2) invalid or unsupported, with errors registered and/or functions
  //    added to the Catalog as appropriate.
  //
  // Note - returning OK means that ModuleCatalog creation succeeded
  // and a non-NULL <module_catalog> is returned.  This implies that the
  // module IMPORT succeeded, but there could still be statements inside
  // the module with errors (which the caller could treat as warnings or
  // errors, for instance if it supports strict and non-strict modes).
  // To check if a module imported cleanly with no errors, the caller
  // must check if module_errors() is empty.  But note that with lazy
  // statement resolution, the module could import cleanly but some errors
  // might not be detected until functions are resolved.
  //
  // TODO:  If the caller wants to get all possible errors, both
  // during Create() and statement resolution, then the caller should first
  // invoke ResolveAllStatements().  Then we need to add a method that
  // returns all modules errors, both those detected during Create() as
  // well as those detected during statement resolution (this method does
  // not exist yet).
  //
  // Returns an error if the first statement in <module_contents_> is not
  // a MODULE statement, or if some unexpected CREATE statement error
  // occurred or an error could not be appropriately handled.  If an error
  // is returned, then the returned ModuleCatalog is set to null.
  //
  // Returns an error if both `module_factory_options` and `analyzer_options`
  // provide a distinct, non-null `ConstantEvaluator`.
  // TODO: b/277365877 - Deprecate and remove
  // `ModuleFactoryOptions::constant_evaluator`.
  static absl::Status Create(
      const std::vector<std::string>& module_name_from_import,
      const ModuleFactoryOptions& module_factory_options,
      const std::string& module_filename, const std::string& module_contents,
      const AnalyzerOptions& analyzer_options, ModuleFactory* module_factory,
      Catalog* builtin_function_catalog, Catalog* global_scope_catalog,
      TypeFactory* type_factory,
      std::unique_ptr<ModuleCatalog>* module_catalog);

  // The same as above, with `nullptr` for `global_scope_catalog`.
  ABSL_DEPRECATED("Inline me!")
  static absl::Status Create(
      const std::vector<std::string>& module_name_from_import,
      const ModuleFactoryOptions& module_factory_options,
      const std::string& module_filename, const std::string& module_contents,
      const AnalyzerOptions& analyzer_options, ModuleFactory* module_factory,
      Catalog* builtin_function_catalog, TypeFactory* type_factory,
      std::unique_ptr<ModuleCatalog>* module_catalog) {
    return ModuleCatalog::Create(
        module_name_from_import, module_factory_options, module_filename,
        module_contents, analyzer_options, module_factory,
        builtin_function_catalog,
        /*global_scope_catalog=*/nullptr, type_factory, module_catalog);
  };

  // Returns the module name as indicated by the MODULE statement in the
  // module contents, i.e., the following MODULE statement in a module
  // defines the module's full name as a.b.c:
  //   MODULE a.b.c;
  //
  // If the module contents do not contain a valid MODULE statement then
  // returns "<unnamed module>".
  //
  // If the module contains multiple MODULE statements, the first MODULE
  // statement defines the module's full name and an error is collected
  // for the subsequent MODULE statements.
  std::string FullName() const override;

  // Returns detailed information about this module.
  const ModuleDetails& module_details() const { return module_details_; }

  // Returns the import path of the module, i.e., if the IMPORT statement is:
  //   IMPORT MODULE x.y.z AS w;
  //
  // ...then the <module_name_from_import_> is a vector of {x, y, z};
  //
  // This is different than FullName(), in that this import path is derived
  // from the IMPORT statement, while FullName() is defined by the MODULE
  // statement (and these two paths are not required to be the same).
  const std::vector<std::string>& module_name_from_import() const {
    return module_name_from_import_;
  }

  // Returns the file path this catalog was loaded from.
  const std::string& GetModuleFilename() const { return module_filename_; }

  const std::string& GetModuleContents() const { return module_contents_; }

  // Catalog interface functions.  These functions generally forward the
  // request to the <public_catalog_>, and return an error if the object was
  // not successfully initialized (there was a parse error or invalid
  // statements in the module file, etc.).
  // When forwarding the request to the embedded <public_catalog_>, the
  // object's statement will be resolved if applicable and necessary (if the
  // statement has not been resolved yet).
  absl::Status FindTable(
      const absl::Span<const std::string>& path, const Table** table,
      const Catalog::FindOptions& options = Catalog::FindOptions()) override;
  absl::Status FindProcedure(
      const absl::Span<const std::string>& path, const Procedure** procedure,
      const Catalog::FindOptions& options = Catalog::FindOptions()) override;
  absl::Status FindType(
      const absl::Span<const std::string>& path, const Type** type,
      const Catalog::FindOptions& options = Catalog::FindOptions()) override;
  absl::Status FindFunction(
      const absl::Span<const std::string>& path, const Function** function,
      const Catalog::FindOptions& options = Catalog::FindOptions()) override;
  absl::Status FindTableValuedFunction(
      const absl::Span<const std::string>& path,
      const TableValuedFunction** function,
      const Catalog::FindOptions& options = Catalog::FindOptions()) override;

  // Extends the contract of Catalog::FindConstantWithPathPrefix() as follows:
  // - If <constant> is a LazyResolutionConstant, resolves the constant
  //   expression and adds any resolution errors to the constant.
  // - If resolution is successful and a ConstantEvaluator is available,
  //   evaluates the resolved constant expression and adds the value or any
  //   evaluation error to the constant.
  // - Performs resolution and evaluation at most once for any given constant.
  // - Returns any resolution or evaluation errors that might have occurred.
  absl::Status FindConstantWithPathPrefix(
      absl::Span<const std::string> path, int* num_names_consumed,
      const Constant** constant, const Catalog::FindOptions& options) override;

  // Resolves all of the valid module statements.  If object resolution produces
  // an error, then any lookup of that object from the catalog returns this
  // resolution error.
  //
  // If `include_global_scope_objects` is false, then global-scope objects are
  // not resolved. This is useful for in contexts where a full global catalog
  // may not be available, but resolution of builtin-scope objects is desired.
  //
  // AppendModuleErrors() can be invoked after this method to return the
  // module's errors, which will include any resolution errors.
  void ResolveAllStatements(bool include_global_scope_objects = true);

  // Gets an ordered map of functions (by function name) from both the public
  // and private catalogs. If `include_global_scope_objects` is false, then
  // global-scope objects are not added to the function map.
  void GetOrderedPublicAndPrivateFunctionMap(
      bool include_global_scope_objects,
      OrderedLazyResolutionFunctionMap* ordered_function_map) const;
  // Same as above, but with `include_global_scope_objects`=true.
  void GetOrderedPublicAndPrivateFunctionMap(
      OrderedLazyResolutionFunctionMap* ordered_function_map) const {
    GetOrderedPublicAndPrivateFunctionMap(
        /*include_global_scope_objects=*/true, ordered_function_map);
  }
  // Gets an ordered map of table functions (by tvf name) from both the public
  // and private catalogs. If `include_global_scope_objects` is false, then
  // global-scope objects are not added to the function map.
  void GetOrderedPublicAndPrivateTableFunctionMap(
      bool include_global_scope_objects,
      OrderedLazyResolutionTableFunctionMap* ordered_table_function_map) const;
  // Same as above, but with `include_global_scope_objects`=true.
  void GetOrderedPublicAndPrivateTableFunctionMap(
      OrderedLazyResolutionTableFunctionMap* ordered_table_function_map) const {
    GetOrderedPublicAndPrivateTableFunctionMap(
        /*include_global_scope_objects=*/true, ordered_table_function_map);
  }
  // Gets an ordered map of constants (by constant name) from both the public
  // and private catalogs.
  void GetOrderedPublicAndPrivateConstantMap(
      OrderedLazyResolutionConstantMap* ordered_constant_map) const;

  // Gets an ordered map of views (by view name) from both the public and
  // private catalogs. If `include_global_scope_objects` is false, then
  // global-scope objects are not added to the view map.
  void GetOrderedPublicAndPrivateViewMap(
      bool include_global_scope_objects,
      OrderedLazyResolutionViewMap* ordered_view_map) const;

  // Gets an ordered map of procedures (by procedure name) from both the
  // public and private catalogs. Only global-scope procedures are supported.
  void GetOrderedPublicAndPrivateProcedureMap(
      OrderedLazyResolutionProcedureMap* ordered_procedure_map) const {
    GetOrderedPublicAndPrivateProcedureMap(
        /*include_global_scope_objects=*/true, ordered_procedure_map);
  }

  // Evaluates all of the valid module constants that are not evaluated yet,
  // from both the public and private catalogs.  If constant evaluation
  // produces an error, then any lookup of that constant from the catalog
  // returns this evaluation error.
  //
  // No-op if no evaluator was provided with the ModuleFactoryOptions when this
  // ModuleCatalog was created.
  //
  // AppendModuleErrors() can be invoked after this method to return the
  // module's errors, which will include all resolution and evaluation errors.
  void EvaluateAllConstants();

  // Returns the list of errors not associated with a created catalog object.
  // For example, this includes parse errors, invalid statements, and other
  // semantic errors such as a module that is missing the MODULE statement.
  // Errors associated with catalog objects are in the catalog objects
  // themselves.
  // TODO: The functionality associated with this method is subsumed
  // by AppendModuleErrors(), so this method is deprecated and should be
  // removed.
  const std::vector<absl::Status>& module_errors() const {
    return module_errors_;
  }

  // Appends requested module errors to <errors>.
  //
  // Errors include:
  // 1) import-time errors which never change after module initialization,
  //    for example a missing MODULE statement, failed IMPORT statements,
  //    parser errors, etc.
  // 2) if <include_catalog_object_errors> then also includes object analysis
  //    errors, which can be added as more objects are lazily resolved.
  //
  // If <include_nested_module_errors> then also appends errors from all
  // transitively imported modules.
  void AppendModuleErrors(bool include_nested_module_errors,
                          bool include_catalog_object_errors,
                          std::vector<absl::Status>* errors) const;

  // Returns whether or not this module has a known error, matching the
  // semantics of AppendModuleErrors().
  bool HasErrors(bool include_nested_module_errors,
                 bool include_catalog_object_errors) const;

  // Returns whether or not this module imported another module.
  bool HasImportedModules() const {
    return !imported_modules_by_alias_.empty();
  }

  // Returns whether or not this module imported another module with <alias>.
  bool HasImportedModule(const std::string& alias) const;

  // Returns whether the module contains any global-scope objects (those
  // defined with the `allowed_references="GLOBAL"` option).
  bool HasGlobalScopeObjects() const;

  // Returns a debug string for the ModuleCatalog, including the module name,
  // functions/objects, and initialization errors.  Respects <verbose> when
  // printing functions (to include signatures or not) and errors (full Status
  // strings vs. only Status error messages).  If <include_module_contents>
  // then also includes <module_contents_> in the output.
  std::string DebugString(bool include_module_contents = false) const;

  // Returns a debug string for all the objects in the ModuleCatalog, forwarding
  // `full` and/or `verbose` for each object type as appropriate. If
  // `include_types` is false, then type details are excluded from the result.
  std::string ObjectsDebugString(bool full, bool verbose,
                                 bool include_types) const;

  // Returns a debug string for all the functions in the ModuleCatalog.
  // If <full>, then Function::FullDebugString(<verbose>) is used,
  // otherwise Function::DebugString(<verbose>) is invoked.
  // TODO: Simplify the debug string variations... we shouldn't
  // need both full and verbose.  Why do we have Function::DebugString()
  // and Function::FullDebugString()?  Why not just
  // Function::DebugString(<verbose>)?  If we simplify the variations of
  // debug string functions and make them consistent across objects, then
  // we don't need these class specific helpers (for instance, maybe
  // Function, Table, etc. can implement a 'CatalogItem' interface that
  // declares a common DebugString(<verbose>) function).
  std::string FunctionsDebugString(bool full, bool verbose) const;

  // Returns a debug string for all the table functions in the ModuleCatalog.
  std::string TableFunctionsDebugString() const;

  // Returns a debug string for all the named constants in the ModuleCatalog.
  // Constant::DebugString(<verbose>) is invoked on each constant in the
  // catalog.
  std::string ConstantsDebugString(bool verbose) const;

  // Returns a debug string for all the views in the ModuleCatalog.
  std::string ViewsDebugString() const;

  // Returns a debug string for all the procedures in the ModuleCatalog.
  std::string ProceduresDebugString() const;

  // Returns a debug string for all the types in the ModuleCatalog.
  std::string TypesDebugString() const;

  // Returns a debug string of errors encountered in the module, including
  // initialization errors and optionally including errors from transitively
  // imported modules and analysis errors related to objects in this catalog.
  // See AppendModuleErrors() for details about <include_nested_module_errors>
  // and <include_catalog_object_errors>.
  std::string ModuleErrorsDebugString(bool include_nested_module_errors,
                                      bool include_catalog_object_errors) const;
  // Overload that invokes the previous function while excluding
  // both nested module errors and catalog object errors.
  std::string ModuleErrorsDebugString() const;

  // Returns a user facing string of error messages encountered in the module,
  // excluding initialization errors and errors from transitively imported
  // modules and analysis errors related to objects in this catalog.
  std::string ModuleErrorMessageString() const;

  // Returns a debug string for all imported modules (recursively), including
  // contents.  If <include_module_contents> then the module contents
  // are included (i.e., DebugString(<include_module_contents>) is invoked on
  // each recursively imported module).
  std::string ImportedModulesDebugString(bool include_module_contents) const;

  // Returns the related ResolvedModuleStmt if there is one, otherwise
  // returns nullptr.
  // Retains ownership of the ResolvedModuleStmt.
  const ResolvedModuleStmt* resolved_module_stmt() const;

 private:
  // The ModuleCatalog must be initialized (via Init()) before it is used.
  // The Create() function implicitly calls the constructor, followed by
  // Init().
  ModuleCatalog(const std::vector<std::string>& module_name_from_import,
                const ModuleFactoryOptions& module_factory_options,
                const std::string& module_filename,
                const std::string& module_contents,
                const AnalyzerOptions& analyzer_options,
                ModuleFactory* module_factory,
                Catalog* builtin_function_catalog,
                Catalog* global_scope_catalog, TypeFactory* type_factory);

  // Initializes the ModuleCatalog.  This must be called exactly once before
  // attempting to use the catalog, and is implicitly invoked in Create()
  // after ModuleCatalog construction.
  //
  // The <module_contents> should be a semicolon separated string of
  // GoogleSQL statements (e.g., CREATE statements), otherwise the
  // ModuleCatalog may fail initialization, or initialize with errors.
  //
  // Parses and validates each statement in <module_contents>, inserting
  // related (parsed) ModuleFunctions into either the <private_catalog_> or
  // <public_catalog_> as appropriate for all valid statements.
  // Collects errors encountered during statement parsing and validation
  // into <module_errors_> or the resolved object, as appropriate.
  //
  // Returns whether or not initialization succeeds.  Initialization
  // succeeds as long as all statements are either:
  // 1) valid and added into the Catalog
  // 2) invalid or unsupported, with errors registered and/or functions
  //    added to the catalog as appropriate.
  //
  // Initialization fails if some unexpected error occurred or was
  // not appropriately handled.
  //
  // Returns an error if both `module_factory_options` and `analyzer_options`
  // provide a distinct, non-null `ConstantEvaluator`.
  // TODO: b/277365877 - Deprecate and remove
  // `ModuleFactoryOptions::constant_evaluator`.
  absl::Status Init();

  // Initializes the <public_catalog_> and <private_catalog_> after obtaining
  // the module name from the MODULE statement in the module contents.
  absl::Status InitInternalCatalogs();

  // Internal common helper function for Find*() calls.
  // The <ObjectType> represents the Catalog object type.  Must be
  // Function, TableValuedFunction or Constant.
  // The <LazyResolutionObjectType> is the corresponding lazy version of
  // <ObjectType>.
  // The <FindObjectMethod> is the function that is invoked to look up the
  // object name in the <public_catalog_> (i.e., FindFunction() for
  // Function, etc.).
  template <class ObjectType, class LazyResolutionObjectType,
            typename FindObjectMethod>
  absl::Status FindObject(absl::Span<const std::string> path,
                          const ObjectType** object,
                          FindObjectMethod find_object_method,
                          const FindOptions& options);

  // Gets an ordered map of procedures (by procedure name) from both the
  // public and private catalogs. Only global-scope procedures are supported.
  // If `include_global_scope_objects` is false, no procedures are added to the
  // procedure map.
  void GetOrderedPublicAndPrivateProcedureMap(
      bool include_global_scope_objects,
      OrderedLazyResolutionProcedureMap* ordered_procedure_map) const;

  // Updates the catalog from the statement owned by <parser_output> with the
  // created name/object as appropriate.  For completely invalid or unsupported
  // statements, <module_errors_> is updated with the appropriate status but
  // the catalog is not updated.  The returned Status indicates if the
  // statement was handled properly (not whether the statement was valid and
  // supported).
  absl::Status MaybeUpdateCatalogFromStatement(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output);

  // Similar to the above, but where <parser_output> is known to represent
  // an ASTCreateFunctionStatement.
  absl::Status MaybeUpdateCatalogFromCreateFunctionStatement(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output);

  // Similar to the above, but where <parser_output> is known to represent
  // an ASTCreateTableFunctionStatement.
  absl::Status MaybeUpdateCatalogFromCreateTableFunctionStatement(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output);

  // Similar to the above, but where <parser_output> is known to represent
  // an ASTCreateConstantStatement.
  absl::Status MaybeUpdateCatalogFromCreateConstantStatement(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output);

  // Similar to the above, but where `parser_output` is known to represent an
  // ASTCreateViewStatement.
  absl::Status MaybeUpdateCatalogFromCreateViewStatement(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output);

  // Similar to the above, but where `parser_output` is known to represent an
  // ASTCreateProcedureStatement.
  absl::Status MaybeUpdateCatalogFromCreateProcedureStatement(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output);

  // Shared helper function that performs common validation of the
  // <ast_create_statement>, including verification that:
  // 1) the CREATE statement must have the PUBLIC or PRIVATE modifier
  // 2) the CREATE statement must not be OR REPLACE or IF NOT EXISTS
  //
  // The <object_type> is used for error messages.
  absl::Status PerformCommonCreateStatementValidation(
      absl::string_view object_type,
      const ASTCreateStatement* ast_create_statement, absl::string_view name);

  // Performs validation specific to CREATE PROCEDURE statements. If any
  // validation fails, the error is returned and validation stops. Validation
  // checks that:
  // - If LANGUAGE is specified, it must be REMOTE.
  // - The WITH CONNECTION clause is not present.
  absl::Status PerformCreateProcedureStatementValidation(
      const ASTCreateProcedureStatement* create_procedure_ast);

  // Similar to the above, but where <parser_output> is known to represent
  // an ASTModuleStatement.
  absl::Status MaybeUpdateCatalogFromModuleStatement(
      std::unique_ptr<ParserOutput> parser_output);

  // Similar to the above, but where <parser_output> is known to represent
  // an ASTImportStatement.  Performs analysis and common validation, then
  // invokes import type specific methods for IMPORT MODULE or IMPORT PROTO.
  // If the IMPORT statement is invalid then the catalog is not updated.
  absl::Status MaybeUpdateCatalogFromImportStatement(
      std::unique_ptr<ParserOutput> parser_output);

  // Invokes the <module_factory_> to build and/or return a ModuleCatalog for
  // the imported module based on the module name, and then creates a
  // subcatalog in this Catalog with the imported module alias.
  // If the IMPORT statement is invalid then the catalog is not updated.
  absl::Status MaybeUpdateCatalogFromImportModuleStatement(
      std::unique_ptr<ParserOutput> parser_output,
      const ResolvedImportStmt* import_statement);

  // Fetches the google::protobuf::FileDescriptor for the specified IMPORT PROTO file
  // name from <module_factory_>, and then adds all enum and proto types in
  // that FileDescriptor into this ModuleCatalog as named GoogleSQL Types.
  // If the IMPORT statement is invalid then the catalog is not updated.
  absl::Status MaybeUpdateCatalogFromImportProtoStatement(
      std::unique_ptr<ParserOutput> parser_output,
      const ResolvedImportStmt* import_statement);

  // Creates an EnumType based on <enum_descriptor> and adds it to the Catalog.
  // If there is any kind of error, that error is registered in <module_errors_>
  // and this function returns without adding the EnumType to the Catalog.
  // The 'import_file_path' and 'location' are only used for error messages.
  void AddEnumTypeToCatalog(absl::string_view import_file_path,
                            const ParseLocationPoint& location,
                            const google::protobuf::EnumDescriptor* enum_descriptor);

  // Creates a ProtoType based on <proto_descriptor> and adds it to the Catalog.
  // If there is any kind of error, that error is registered in <module_errors_>
  // and this function returns without adding the ProtoType to the Catalog.
  // The 'import_file_path' and 'location' are only used for error messages.
  void AddProtoTypeToCatalog(absl::string_view import_file_path,
                             const ParseLocationPoint& location,
                             const google::protobuf::Descriptor* proto_descriptor);

  // Creates a ProtoType based on <proto_descriptor> and adds it to the Catalog.
  // Recurses into <proto_descriptor>, also adding nested Proto and Enum Types
  // into the Catalog.  If there is any kind of error, that error is registered
  // in <module_errors_> and processing continues for any remaining nested
  // types.  The 'import_file_path' and 'location' are only used for error
  // messages.
  void AddProtoTypeAndNestedTypesToCatalog(
      const std::string& import_file_path, const ParseLocationPoint& location,
      const google::protobuf::Descriptor* proto_descriptor);

  // Helper functions that return whether or not the catalog contains
  // objects of the specified type.
  bool HasFunctions() const;
  bool HasTableFunctions() const;
  bool HasViews() const;
  bool HasProcedures() const;
  bool HasConstants() const;
  bool HasTypes() const;

  // Helper functions for handling status errors.  These functions take
  // an error status or string, and:
  // 1) create and/or update a new status with an updated error location
  // 2) if <errors> is non-NULL, add this updated status to <errors>
  // 3) return the new/updated status
  absl::Status UpdateAndRegisterError(const absl::Status& status,
                                      std::vector<absl::Status>* errors);
  absl::Status UpdateAndRegisterError(const absl::Status& status,
                                      const ParseLocationPoint& location,
                                      std::vector<absl::Status>* errors);
  absl::Status MakeAndRegisterError(absl::string_view error_string,
                                    const ParseLocationPoint& location,
                                    std::vector<absl::Status>* errors);
  // Same as the previous method but does not return Status.
  void MakeAndRegisterErrorIgnored(absl::string_view error_string,
                                   const ParseLocationPoint& location,
                                   std::vector<absl::Status>* errors);
  // Prepends the module FullName() and IMPORT information to the error.
  absl::Status MakeAndRegisterStatementError(absl::string_view error_string,
                                             const ASTNode* node,
                                             std::vector<absl::Status>* errors);

  // Updates <status> by attaching the module filename to the ErrorLocation
  // as needed, and then updates the <status> based on <error_message_mode_>
  // and returns the updated Status.
  absl::Status UpdateStatusForModeAndFilename(const absl::Status& status);

  // Internal implementation for AppendModuleErrors, which keeps track of
  // what modules have already been processed to avoid adding redundant errors
  // to <errors> if a module is imported multiple times.
  void AppendModuleErrorsImpl(bool include_nested_module_errors,
                              bool include_catalog_object_errors,
                              std::set<const ModuleCatalog*>* appended_modules,
                              std::vector<absl::Status>* errors) const;

  // Returns the AnalyzerOutput for the MODULE statement.
  const AnalyzerOutput* module_statement_analyzer_output() const {
    return module_statement_analyzer_output_.get();
  }

  // Stores a Status and a ResolutionScope, so that both can be returned from a
  // single function. This is required because LazyResolution objects in modules
  // store their errors (which are produced when the function is looked up), so
  // we need to continue execution even if an error happens.
  struct StatusAndResolutionScope {
    absl::Status status;
    ResolutionScope resolution_scope;
  };

  StatusAndResolutionScope GetStatementResolutionScope(
      const ASTStatement* stmt_ast) const;

  // Return a pointer to the LazyResolution Catalog that an object with the
  // given visibility and resolution scope should be inserted into.
  LazyResolutionCatalog* GetLazyResolutionCatalogForInsert(
      bool is_private, ResolutionScope resolution_scope) const;

  // Returns a pointer to this module catalog's public+builtin catalog, which
  // can be used to resolve builtin-scope objects in modules which import this
  // module.
  Catalog* public_builtin_catalog() const {
    return public_builtin_catalog_.get();
  }

  // Returns the ConstantEvaluator to use in this module.
  ConstantEvaluator* constant_evaluator() const;

  // Validates precondition that `analyzer_options_` and
  // `module_factory_options_` cannot both have a constant evaluator set, unless
  // they point to the same object.
  // TODO: b/277365877 - Remove this once
  // `ModuleFactoryOptions::constant_evaluator` is deprecated and removed.
  absl::Status ValidateConstantEvaluatorPrecondition() const;

  class LocalCycleDetector {
   public:
    explicit LocalCycleDetector(const FindOptions& options) {
      if (options.cycle_detector() != nullptr) {
        cycle_detector_ = options.cycle_detector();
      } else {
        local_cycle_detector_ = std::make_unique<CycleDetector>();
        cycle_detector_ = local_cycle_detector_.get();
      }
    }
    ~LocalCycleDetector() = default;

    // Not copyable or movable.
    LocalCycleDetector(const LocalCycleDetector&) = delete;
    LocalCycleDetector& operator=(const LocalCycleDetector&) = delete;

    CycleDetector* get() { return cycle_detector_; }

   private:
    CycleDetector* cycle_detector_;
    std::unique_ptr<CycleDetector> local_cycle_detector_;
  };

  // The module name path of corresponding the IMPORT MODULE statement.
  std::vector<std::string> module_name_from_import_;

  // Configures the module behavior. Includes the engine callback for evaluating
  // constant expressions.
  const ModuleFactoryOptions& module_factory_options_;

  // The ResolvedAST for the MODULE statement.  It defines the module's
  // full name, and identifies its options (if any).
  std::unique_ptr<const AnalyzerOutput> module_statement_analyzer_output_;

  // The module file name, indicating the source file of the module contents.
  // Used in error messages related to invalid module objects, to indicate
  // the original source of the object definition.
  std::string module_filename_;

  // A semicolon-separated sequence of module statements.
  // Currently, modules only support CREATE FUNCTION statements.
  std::string module_contents_;

  // AnalyzerOptions to be used when resolving the <module_contents_>.
  AnalyzerOptions analyzer_options_;

  // Whether or not InitInternalCatalogs() has been called and successfully
  // initialized the internal catalogs.
  bool internal_catalogs_initialized_ = false;

  // Module objects (e.g. functions, table functions, etc.) are separated on two
  // dimensions: visibility (public vs. private) and resolution scope (builtin
  // vs. global):
  //   - Public objects are available outside of this catalog, private objects
  //     are only visible to other objects within this catalog.
  //   - Builtin-scope objects can only refer to other builtin-scope objects,
  //     global-scope objects can refer to both builtin-scope and global-scope
  //     objects.
  //
  // The names in these catalogs must be distinct, there cannot exist an object
  // in multiple catalogs with the same name.
  std::unique_ptr<LazyResolutionCatalog> public_builtin_catalog_;
  std::unique_ptr<LazyResolutionCatalog> public_global_catalog_;
  std::unique_ptr<LazyResolutionCatalog> private_builtin_catalog_;
  std::unique_ptr<LazyResolutionCatalog> private_global_catalog_;

  // Contains all public objects created in this module. When Find*() calls are
  // evaluated for this ModuleCatalog, they are effectively forwarded to the
  // `public_catalog_` and only public objects are returned.
  std::unique_ptr<MultiCatalog> public_catalog_;

  // Contains all types imported into this Catalog (via IMPORT PROTO
  // statements). Can be referenced from objects in the public and private
  // catalogs in this module, but are not visible outside of this Catalog.
  std::unique_ptr<SimpleCatalog> type_import_catalog_;

  // Contains all public objects (builtin- and global-scope) from the module
  // subcatalogs imported into this catalog (via IMPORT MODULE statements).
  // Contents can be referenced by global-scope objects in this module.
  std::unique_ptr<SimpleCatalog> module_import_global_catalog_;

  // Contains all public and builtin-scope objects from the module subcatalogs
  // imported into this catalog (via IMPORT MODULE statements). Contents can be
  // referenced by builtin-scope objects in this module.
  std::unique_ptr<SimpleCatalog> module_import_builtin_catalog_;

  // Contains builtin functions from the calling engine, that can be
  // referenced by builtin-scope statements in this module.  Not owned.
  //
  // This Catalog must only contain functions, not tables, types, etc.
  Catalog* builtin_function_catalog_;

  // Contains global-scope objects, such as tables, from the calling engine,
  // that can be referenced by global-scope statements in this module. If not
  // provided by the caller, all module statements which declare
  // `allowed_references=global` will produce a relevant error that global-scope
  // references are not supported.
  Catalog* global_scope_catalog_;

  // The Catalog to use when resolving statements for objects in this
  // ModuleCatalog with "builtin" resolution scope. Such objects can reference
  // the contents of:
  //   - `public_builtin_catalog_` and `private_builtin_catalog_` - Pubic and
  //     private builtin-scope objects in this module.
  //   - `type_import_catalog_` - Types imported by this module.
  //   - `module_import_builtin_catalog_` - Public builtin-scope objects from
  //     modules imported by this module.
  //   - `builtin_function_catalog_` - Builtin functions provided by the caller.
  std::unique_ptr<MultiCatalog> resolution_catalog_builtin_;

  // The Catalog to use when resolving statements for objects in this
  // ModuleCatalog with "global" resolution scope. Such objects can reference
  // the contents of:
  //   - `public_builtin_catalog_`, `private_builtin_catalog_`,
  //     `ublic_global_catalog_`, and `private_global_catalog_` - Pubic and
  //     private, builtin-scope and gllobal-scope objects in this module.
  //   - `type_import_catalog_` - Types imported by this module.
  //   - `module_import_global_catalog_` - Public builtin-scope and global-scope
  //     objects from modules imported by this module.
  //   - <global_scope_catalog_> - Global-scope objects provided by the caller.
  std::unique_ptr<MultiCatalog> resolution_catalog_global_;

  // A catalog which is used for resolving builtin-scope module objects to
  // give better error messages when a builtin-scope object references a
  // global-scope object. Lookups produce objects from
  // `resolution_catalog_builtin_`, but if a lookup results in kNotFound, then
  // the lookup is attempted in `resolution_catalog_global_` to produce a
  // relevant error that a builtin object cannot reference a global object.
  std::unique_ptr<ScopeErrorCatalog> scope_error_catalog_;

  // The TypeFactory to use for any required Types during statement
  // processing.  Not owned.  The <type_factory_> must outlive this
  // class.
  TypeFactory* type_factory_ = nullptr;

  // List of errors that were encountered during ModuleCatalog processing.
  std::vector<absl::Status> module_errors_;

  // This interface provides a callback for the ModuleCatalog to fetch
  // module contents and construct a catalog for this module, as well as
  // for any IMPORT MODULE statements in this module.  Also provides a
  // callback to fetch proto FileDescriptors related to IMPORT PROTO
  // statements.  Can be nullptr, in which case any IMPORT MODULE or
  // IMPORT PROTO statement in the module will fail with an error.
  // Not owned.
  ModuleFactory* module_factory_ = nullptr;

  // Contains information related to an IMPORT (MODULE) statement.
  struct ImportModuleInfo {
    // Takes ownership of <import_parser_output_in>.
    // Does not take ownership of <module_catalog_in>.
    ImportModuleInfo(std::unique_ptr<ParserOutput> import_parser_output_in,
                     ModuleCatalog* module_catalog_in);

    // TODO: Consider removing <import_parser_output>, it is not
    // currently used anywhere but it is here in case we need it at some
    // point.
    std::unique_ptr<ParserOutput> import_parser_output;
    ModuleCatalog* module_catalog;  // Not owned.
  };

  // A list of imported module information.
  std::map<std::string, ImportModuleInfo> imported_modules_by_alias_;

  // List of analyzer outputs for import statements in this module. This list is
  // populated only if <store_import_stmt_analyzer_output> is true in
  // <module_factory_options_>.
  std::vector<std::unique_ptr<const AnalyzerOutput>>
      import_stmt_analyzer_outputs_;

  // Details about this module.
  ModuleDetails module_details_ = ModuleDetails::CreateEmpty();

  friend class ModuleTest;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_MODULES_H_
