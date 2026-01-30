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

#ifndef GOOGLESQL_COMMON_LAZY_RESOLUTION_CATALOG_H_
#define GOOGLESQL_COMMON_LAZY_RESOLUTION_CATALOG_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// There are a few unused includes below that are extensively depended upon by
// users of this header.

#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/remote_tvf_factory.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/sql_function.h"  
#include "googlesql/public/sql_procedure.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"  
#include "googlesql/public/templated_sql_tvf.h"  
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/case.h"
#include "gtest/gtest_prod.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace googlesql {

class LazyResolutionConstant;
class LazyResolutionFunction;
class LazyResolutionTableFunction;
class LazyResolutionTemplatedSQLTVF;
class LazyResolutionView;
class LazyResolutionProcedure;

// LazyResolutionCatalog is a Catalog implementation where SQL objects can be
// added to the catalog with only the ParserOutput of their associated CREATE
// statement.  Since the object's statement is initially parsed but unresolved,
// the object's resolved properties (like Type or FunctionSignature) are
// unknown.  When a Find*() function is invoked on the object, then the
// object's ParserOutput is resolved, and the object's Type and
// FunctionSignatures are determined.
//
// Functions and other objects in the catalog can reference builtin functions
// as provided by the caller, and other objects in this catalog.
//
// Objects in the LazyResolutionCatalog are lazy resolution objects (e.g.,
// LazyResolutionFunction).  These objects manage the lazy resolution process,
// and upon resolution they create separate Catalog objects that are returned
// from Catalog lookups (e.g., upon resolution the LazyResolutionFunction
// creates a related SQLFunction or TemplatedSQLFunction).
//
// LazyResolutionCatalog is thread-safe after initial setup.
class LazyResolutionCatalog : public Catalog {
 public:
  // Create a LazyResolutionCatalog with the given <analyzer_options>,
  // <module_details> and <type_factory>. <source_filename> is only used for
  // error messaging. <module_details> contains details about the module that
  // owns this Catalog.
  static absl::Status Create(
      absl::string_view source_filename, ModuleDetails module_details,
      const AnalyzerOptions& analyzer_options, TypeFactory* type_factory,
      std::unique_ptr<LazyResolutionCatalog>* lazy_resolution_catalog);

  LazyResolutionCatalog(const LazyResolutionCatalog&) = delete;
  LazyResolutionCatalog& operator=(const LazyResolutionCatalog&) = delete;

  ~LazyResolutionCatalog() override = default;

  std::string FullName() const override {
    return std::string(module_details_.module_fullname());
  }

  // Adds a LazyResolutionConstant to the LazyResolutionCatalog, using its own
  // name. Returns an error if names are not unique (case-insensitively).
  absl::Status AddLazyResolutionConstant(
      std::unique_ptr<LazyResolutionConstant> constant);

  // Adds a LazyResolutionFunction to the LazyResolutionCatalog, using its own
  // name. Returns an error if names are not unique (case-insensitively).
  absl::Status AddLazyResolutionFunction(
      std::unique_ptr<LazyResolutionFunction> function);

  // Adds a LazyResolutionTableFunction to the LazyResolutionCatalog, using its
  // own name. Returns an error if names are not unique (case-insensitively).
  absl::Status AddLazyResolutionTableFunction(
      std::unique_ptr<LazyResolutionTableFunction> table_function);

  // Adds a LazyResolutionView to the LazyResolutionCatalog, using its own name.
  // Returns an error if names are not unique (case-insensitively).
  absl::Status AddLazyResolutionView(std::unique_ptr<LazyResolutionView> view);

  // Adds a LazyResolutionProcedure to the LazyResolutionCatalog, using its own
  // name. Returns an error if names are not unique (case-insensitively).
  absl::Status AddLazyResolutionProcedure(
      std::unique_ptr<LazyResolutionProcedure> procedure);

  // Appends <catalog> to <resolution_catalog_>, which allows <catalog> names
  // to be referenced by objects (functions) in this LazyResolutionCatalog.
  // So for example, resolution of CREATE FUNCTION statements allow function
  // expression bodies to reference functions defined in <catalog>.
  // If <catalog> is NULL then this function is a no-op.
  // Does not take ownership of <catalog>.
  absl::Status AppendResolutionCatalog(Catalog* catalog);

  // The LazyResolutionCatalog currently only overrides the Find*() functions
  // for functions, table functions, procedures, and constants and returns
  // NOT_FOUND for other Find*() functions.
  //
  // These Find*() functions look up the object from the <simple_catalog_>,
  // resolve the object statement if necessary (if it has not been resolved
  // yet), then returns the object.  If an error occurred during resolution,
  // that resolution error status is returned.
  absl::Status FindFunction(const absl::Span<const std::string>& name_path,
                            const Function** function,
                            const Catalog::FindOptions& options) override;
  absl::Status FindTableValuedFunction(
      const absl::Span<const std::string>& name_path,
      const TableValuedFunction** table_function,
      const Catalog::FindOptions& options) override;
  absl::Status FindConstantWithPathPrefix(
      absl::Span<const std::string> name_path, int* num_names_consumed,
      const Constant** constant, const Catalog::FindOptions& options) override;
  absl::Status FindTable(const absl::Span<const std::string>& name_path,
                         const Table** view,
                         const Catalog::FindOptions& options) override;
  absl::Status FindProcedure(const absl::Span<const std::string>& name_path,
                             const Procedure** procedure,
                             const Catalog::FindOptions& options) override;

  // Returns whether or not this catalog contains the object with the
  // specified name_path.
  bool ContainsConstant(absl::string_view name) const;
  bool ContainsFunction(absl::string_view name) const;
  bool ContainsTableFunction(absl::string_view name) const;
  bool ContainsView(absl::string_view name) const;
  bool ContainsProcedure(absl::string_view name) const;

  // Accessors for reading objects in this LazyResolutionCatalog.
  // These methods are primarily intended for tests.
  std::vector<const LazyResolutionFunction*> functions() const;
  std::vector<const LazyResolutionTableFunction*> table_valued_functions()
      const;
  std::vector<const LazyResolutionConstant*> constants() const;
  std::vector<const LazyResolutionView*> views() const;
  std::vector<const LazyResolutionProcedure*> procedures() const;

  // Prints a debug string for the LazyResolutionCatalog objects (Functions,
  // TableValuedFunctions, etc.).  Respects <verbose> for Functions - to
  // include function signatures and full error Status strings vs. only
  // Status error messages.
  std::string ObjectsDebugString(bool verbose = false) const;

 private:
  // Construct a new uninitialized LazyResolutionCatalog.
  // <module_details> contains details about the module that owns this function.
  // <analyzer_options> and <type_factory> are used when resolving CREATE
  // statements.
  LazyResolutionCatalog(absl::string_view source_filename,
                        ModuleDetails module_details,
                        const AnalyzerOptions& analyzer_options,
                        TypeFactory* type_factory);

  template <class LazyResolutionObjectType>
  using LookupObjectMethod = std::function<absl::Status(
      const std::string&, const LazyResolutionObjectType**,
      const FindOptions&)>;

  // Internal helper function for Find*() calls.
  // The <ObjectType> represents the Catalog object type.  Must be Function,
  // TableValuedFunction or Constant.
  // The <LazyResolutionObjectType> is the corresponding lazy version of
  // <ObjectType>.
  template <class ObjectType, class LazyResolutionObjectType>
  absl::Status FindObject(
      absl::Span<const std::string> name_path, const ObjectType** object,
      LookupObjectMethod<LazyResolutionObjectType> lookup_object_method,
      absl::string_view object_type_name, const FindOptions& options);

  // Look up a LazyResolutionFunction (without resolving it).  If the
  // LazyResolutionFunction is found then returns OK (regardless of whether or
  // not the function is valid).  Only returns error Status if the function is
  // not found.
  absl::Status LookupLazyResolutionFunctionUnresolved(
      absl::string_view name, const LazyResolutionFunction** function,
      const FindOptions& options = FindOptions());
  // Similar to the previous method, but for looking up TVFs.
  absl::Status LookupLazyResolutionTableFunctionUnresolved(
      absl::string_view name,
      const LazyResolutionTableFunction** table_function,
      const FindOptions& options = FindOptions());
  // Similar to the previous functions, but for looking up Constants.
  absl::Status LookupLazyResolutionConstantUnresolved(
      absl::string_view name, const LazyResolutionConstant** constant,
      const FindOptions& options = FindOptions());
  // Similar to the previous functions, but for looking up Views.
  absl::Status LookupLazyResolutionViewUnresolved(
      absl::string_view name, const LazyResolutionView** view,
      const FindOptions& options = FindOptions());
  // Similar to the previous functions, but for looking up Procedures.
  absl::Status LookupLazyResolutionProcedureUnresolved(
      absl::string_view name, const LazyResolutionProcedure** procedure,
      const FindOptions& options = FindOptions());

  // Prints a debug string for each class of objects in the catalog.
  std::string FunctionsDebugString(bool verbose = false) const;
  std::string TableFunctionsDebugString() const;
  std::string ConstantsDebugString(bool verbose) const;
  std::string TypesDebugString() const;
  std::string ProceduresDebugString() const;

  // The source filename that this LazyResolutionCatalog was created from,
  // if applicable (can be empty).  This is used for adding additional error
  // source context to error messages.
  const std::string source_filename_;

  // Details about the module that owns this object.
  const ModuleDetails module_details_;

  // AnalyzerOptions to be used when resolving LazyResolutionFunctions during
  // FindFunction() calls.
  AnalyzerOptions analyzer_options_;

  // Owned LazyResolutionFunctions.
  absl::flat_hash_map<std::string, std::unique_ptr<LazyResolutionFunction>,
                      googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      functions_;
  // Owned LazyResolutionTableFunctions.
  absl::flat_hash_map<std::string, std::unique_ptr<LazyResolutionTableFunction>,
                      googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      table_functions_;
  // Owned LazyResolutionConstants.
  absl::flat_hash_map<std::string, std::unique_ptr<LazyResolutionConstant>,
                      googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      constants_;
  // Owned LazyResolutionViews.
  absl::flat_hash_map<std::string, std::unique_ptr<LazyResolutionView>,
                      googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      views_;
  // Owned LazyResolutionProcedures.
  absl::flat_hash_map<std::string, std::unique_ptr<LazyResolutionProcedure>,
                      googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      procedures_;

  // This is the catalog to use when resolving statements for objects in this
  // catalog.  It includes this LazyResolutionCatalog (so statements in this
  // catalog can reference other objects created in this catalog), as well as
  // other catalogs added via AddResolutionCatalog().
  //
  // This Catalog must only contain functions, not tables, types, etc.
  std::unique_ptr<MultiCatalog> resolution_catalog_;

  // The TypeFactory to use for any required Types during statement
  // processing.  Not owned.  The <type_factory_> must outlive this
  // class.
  TypeFactory* type_factory_ = nullptr;

  friend class LazyResolutionCatalogTest;
};

// LazyResolutionObject contains common logic for the lazy resolution of
// SQL objects, including SQL Functions, SQL TVFs, SQL named constants,
// SQL views, etc.  Each LazyResolutionObject is related to a single CREATE
// statement.  Each of LazyResolution{Function,TableFunction,Constant}
// includes a LazyResolutionObject as a member, holding the generic
// statement-related objects.
//
// Creating a LazyResolutionObject requires a parse tree and status.  If a
// non-OK <object_status> is passed into the constructor then the object is
// already resolved to an error.  If an OK <object_status> is passed into the
// constructor (this is the normal case), then the object is not resolved yet,
// and it will be resolved lazily from <parser_output> the first time
// AnalyzeStatementIfNeeded() is called.  Once an object is resolved, it
// becomes invariant (ignoring mutex acquisition) - the statement will not
// be resolved again and the object status will not change thereafter.
//
// Note that AnalyzeStatementIfNeeded() is the only method that can update
// the object state after creation, and it can only update the object state
// once.
//
// All other methods are effectively const getters for fetching object
// properties.  Note that some getters only return meaningful results
// after the object is resolved (such as ResolvedStatement()).
//
// This class is thread-safe.  Specifically, two threads can call
// AnalyzeStatementIfNeeded() on the same object at the same time and
// the result is as if one thread resolved the object and the other
// thread read the resolved object.
class LazyResolutionObject {
 public:
  // Typically, <parse_resume_location> is the entire module file with an offset
  // into it for the CREATE statement related to this object.  If not OK,
  // <object_status> identifies an error related to the object, which is
  // wrapped in an 'invalid object' error that will be returned whenever
  // the object is looked up in the catalog.  The status will also be
  // updated based on <mode>, as necessary.
  LazyResolutionObject(const ASTIdentifier* object_name,
                       const ParseResumeLocation& parse_resume_location,
                       std::unique_ptr<ParserOutput> parser_output,
                       absl::Status object_status,
                       ErrorMessageOptions error_message_options);
  ~LazyResolutionObject() = default;

  // Returns true if the object was created by CREATE PRIVATE <object>.
  bool IsPrivate() const;

  // Returns the <sql_> content passed into the constructor, that includes
  // the CREATE statement for this LazyResolutionObject and possibly
  // surrounding statements (this is typically the entire module file).
  absl::string_view sql() const { return parse_resume_location_.input(); }

  // Returns the location indicating where parsing began for the CREATE
  // statement with respect to the input <sql_> string, which is immediately
  // following the semicolon from the previous statement (or at the start of
  // the sql contents for the first statement in the sql string).  This
  // includes any whitespace and comments that appear before the actual
  // (CREATE) statement.
  const ParseResumeLocation& parse_resume_location() const {
    return parse_resume_location_;
  }

  // Helper to return the start ParseLocationPoint for the CREATE statement.
  ParseLocationPoint StartParseLocationPoint() const;

  const ParserOutput* parser_output() const { return parser_output_.get(); }

  const ASTIdentifier* name() const { return object_name_; }

  // Returns the object type name as implied by the parser AST.
  std::string TypeName(bool capitalized = false) const;

  // Returns a string that was used when parsing the CREATE statement
  // related to this object.  If <include_prefix>, then all whitespace
  // and comments starting immediately after the end of the previous
  // statement are included.  Otherwise, the returned string begins with
  // the CREATE token/keyword.
  // TODO: Redefine this a bit.  In general, leading comments
  // seem mostly helpful to preserve, and leading spaces are not useful
  // and could be stripped.  For debugging purposes, we may want
  // to provide a way to get the full string where parsing started,
  // immediately after the end of the previous statement (if any).
  absl::string_view GetCreateStatement(bool include_prefix) const;

  // Returns whether or not the object needs resolution.  It needs resolution
  // if it does not have an error <status_> and does not have any analyzer
  // output.
  bool NeedsResolution() const ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // Returns the resolved create statement, if the statement analyzed
  // successfully.  Otherwise returns NULL.
  const ResolvedCreateStatement* ResolvedStatement() const
      ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // If the object was resolved successfully, returns the ResolvedStatement
  // DebugString().  Otherwise returns an empty string.  If <include_sql>
  // then prepends the CREATE statement SQL string.
  std::string GetResolvedStatementDebugStringIfPresent(bool include_sql) const
      ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // Returns object status.  If an error, then Catalog::Find*() lookups return
  // this status (rather than NOT_FOUND or OK).  This status can be set during
  // LazyResolutionCatalog creation (for errors detected after parsing), or
  // during statement resolution (in Find*() calls).
  absl::Status status() const ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // Sets the object's Status.
  void set_status(absl::Status status) ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // Returns a status that, if the Status is non-OK, will be produced when
  // resolution is attempted.
  absl::Status status_when_resolution_attempted() const
      ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // Set a Status that, if the Status is non-OK, will be produced when
  // resolution is attempted. This is used to store an error status that is
  // detected before resolution, but which shouldn't be visible until
  // resolution is attempted.
  void set_status_when_resolution_attempted(absl::Status status)
      ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // If the statement resolved successfully, returns the deprecation warnings
  // from the analyzer. Otherwise returns NULL. Useful for tests.
  const std::vector<absl::Status>* AnalyzerDeprecationWarnings() const
      ABSL_LOCKS_EXCLUDED(resolution_mutex_);

  // If the object's <parser_output_> has already been analyzed, then
  // returns status().  Otherwise, analyzes the <parser_output_> and returns
  // the resolution status.
  absl::Status AnalyzeStatementIfNeeded(const AnalyzerOptions& analyzer_options,
                                        Catalog* catalog,
                                        TypeFactory* type_factory)
      ABSL_LOCKS_EXCLUDED(resolution_mutex_);

 private:
  // Guards the class members that are related to lazy statement resolution.
  // This is only held while reading/updating class members, and is not
  // held during actual resolution or calling out to anything else (which is
  // how we avoid deadlocks).
  mutable absl::Mutex resolution_mutex_;

  // Implementation of NeedsResolution(), that requires that the
  // <resolution_mutex_> is already held.
  bool NeedsResolutionLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(resolution_mutex_);

  // Helper function that takes an error Status from the analyzer and returns
  // a new Status indicating that the function is invalid, with the
  // <analyzer_status> converted to an ErrorSource.  The returned status also
  // preserves the ErrorSource payload on <analyzer_status>, if present.
  // TODO: Remove ErrorMessageOptions, once we consistently always save
  // these status objects with payload, and only produce the mode-versioned
  // status when fetched through FindXXX() calls.
  absl::Status MakeInvalidObjectStatus(
      const absl::Status& analyzer_status,
      ErrorMessageOptions error_message_options);

  const ASTIdentifier* object_name_;

  const ParseResumeLocation parse_resume_location_;

  const ParseLocationRange parse_location_range_;

  // The original parse tree AST for the CREATE statement.
  // It is currently only used to determine if the object is PUBLIC or
  // PRIVATE.
  std::unique_ptr<ParserOutput> parser_output_;

  // If resolution succeeds, this contains the resolver output for the
  // CREATE statement.  The ResolvedCreateStatement can be retrieved using:
  //   analyzer_output_->resolved_statement()->
  //       GetAs<const ResolvedCreateStatement>();
  std::unique_ptr<const AnalyzerOutput> analyzer_output_
      ABSL_GUARDED_BY(resolution_mutex_);

  absl::Status status_ ABSL_GUARDED_BY(resolution_mutex_);

  // An error status which will be set into the main `status_` and returned when
  // resolution is attempted on this object. This is used to store an error
  // status that is detected before resolution, but which shouldn't be visible
  // until resolution is attempted.
  absl::Status status_when_resolution_attempted_
      ABSL_GUARDED_BY(resolution_mutex_);
};

// TODO: LazyResolution{Function,TableFunction,Constant} have
// a lot of common functionality/methods.  Consider making a superclass, or
// possibly one templated class.
//
// LazyResolutionFunction provides lazy resolution for SQL Functions in
// a LazyResolutionCatalog.  This class is used for lazily resolving
// CREATE FUNCTION statements for both templated and non-templated Functions.
// Once a function is resolved, a new SQLFunction or TemplatedSQLFunction
// is created for it and is stored in <function_>.  LazyResolutionCatalog
// lookups return the created <function_> (not this LazyResolutionFunction).
class LazyResolutionFunction {
 public:
  // Creates a LazyResolutionFunction with the specified <parser_output>
  // and <function_status>.  Returns an error if <parser_output> is not an
  // ASTCreateFunctionStatement, if the ASTCreateFunctionStatement name path
  // does not have exactly one name, or if the function is not public or
  // private.
  //
  // The function name is derived from the <parser_output>.  <function_status>
  // can be an error, in which case attempts to resolve this function or
  // lookup this function from a Catalog will return <function_status>.
  //
  // <parse_resume_location> indicates the origin of the SQL statement and is
  // used for error messaging.
  //
  // <module_details> contains details about the module that owns this function.
  static absl::StatusOr<std::unique_ptr<LazyResolutionFunction>> Create(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
      ErrorMessageOptions error_message_options,
      FunctionEnums::Mode function_mode, ModuleDetails module_details);
  // Similar to previous, but for templated UDFs.  The
  // <templated_expression_resume_location> indicates the UDF's SQL function
  // body that gets resolved every time the function is called (because the
  // arguments can be different on each call, the expression must be resolved
  // for each call). <module_details> contains details about the module that
  // owns this function.
  static absl::StatusOr<std::unique_ptr<LazyResolutionFunction>>
  CreateTemplatedFunction(
      const ParseResumeLocation& parse_resume_location,
      const ParseResumeLocation& templated_expression_resume_location,
      std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
      ErrorMessageOptions error_message_options,
      FunctionEnums::Mode function_mode, ModuleDetails module_details);

  LazyResolutionFunction(const LazyResolutionFunction&) = delete;
  LazyResolutionFunction& operator=(const LazyResolutionFunction&) = delete;

  ~LazyResolutionFunction() = default;

  // Returns the function's name (derived from <lazy_resolution_object_>).
  std::string Name() const;

  // Returns the resolution status if the function has already been resolved.
  // If the function has not been resolved yet, then:
  // 1) resolves the parser AST (via <lazy_resolution_object_>)
  // 2) sets the resolution status based on the result of analysis
  // 3) returns the resolution status.
  absl::Status ResolveAndUpdateIfNeeded(const AnalyzerOptions& analyzer_options,
                                        Catalog* catalog,
                                        TypeFactory* type_factory);

  // Returns the SQL passed into the Create() method (via
  // <parse_resume_location>). Used for error messaging, to construct the error
  // string with caret payload.
  absl::string_view SQL() const;

  // Indicates the location of the Name identifier in the SQL passed to the
  // Create() method (via <parse_resume_location>). Used for error messaging, to
  // identify an error location for this function.
  const ASTIdentifier* NameIdentifier() const;

  // Returns the resolved Function (if this LazyResolutionFunction resolved
  // successfully).  Otherwise returns nullptr (if this LazyResolutionFunction
  // has not been resolved yet or if it had a resolution error).
  const Function* ResolvedObject() const { return function_.get(); }

  // Returns the function name and other relevant information (invoking
  // DebugString() on the related ResolvedObject() if applicable).
  // Optionally includes the ResolvedAST and/or SQL CREATE statement.
  std::string DebugString(bool verbose, bool include_ast = false,
                          bool include_sql = false) const;

  // For LazyResolutionFunctions, errors might have been found during
  // validation or analysis, for example:
  // 1) invalid TEMP modifier found during Create()
  // 2) resolver errors found during expression analysis
  absl::Status resolution_status() const;

  // Set a Status that, if the Status is non-OK, will be produced when
  // resolution is attempted. This is used to store an error status that is
  // detected before resolution, but which shouldn't be visible until
  // resolution is attempted.
  void set_status_when_resolution_attempted(absl::Status status);

  // Returns whether or not the function needs resolution.  It needs resolution
  // if resolution_status() is OK and it does not have any analyzer output.
  bool NeedsResolution() const;

  // If the statement resolved successfully, returns the deprecation warnings
  // from the analyzer. Otherwise returns NULL. Useful for tests.
  const std::vector<absl::Status>* AnalyzerDeprecationWarnings() const;

  // If the statement resolved successfully, returns the resolved statement.
  // Otherwise returns NULL.
  const ResolvedCreateFunctionStmt* ResolvedStatement() const;

  void set_statement_context(StatementContext context) {
    statement_context_ = context;
  }
  StatementContext statement_context() const { return statement_context_; }

 private:
  static absl::StatusOr<std::unique_ptr<LazyResolutionFunction>> CreateImpl(
      const ParseResumeLocation& parse_resume_location,
      const std::optional<ParseResumeLocation>&
          templated_expression_resume_location,
      std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
      ErrorMessageOptions error_message_options,
      FunctionEnums::Mode function_mode, ModuleDetails module_details);
  // Private constructor, called by Create().
  LazyResolutionFunction(const ASTIdentifier* function_name,
                         const ParseResumeLocation& parse_resume_location,
                         const std::optional<ParseResumeLocation>&
                             templated_expression_resume_location,
                         std::unique_ptr<ParserOutput> parser_output,
                         absl::Status function_status,
                         ErrorMessageOptions error_message_options,
                         FunctionEnums::Mode mode,
                         ModuleDetails module_details);

  // Returns true if the function is a templated UDF.
  bool IsTemplated() const;

  // If the statement resolved successfully, returns the resolved function
  // expression.  Otherwise returns NULL.
  const ResolvedExpr* FunctionExpression() const;

  // Returns the function's argument names.
  std::vector<std::string> ArgumentNames() const;

  // Returns the function's aggregate expression list (which is only non-empty
  // for UDAs).
  const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
  AggregateExpressionList() const;

  // Indicates whether the function is SCALAR or AGGREGATE.
  FunctionEnums::Mode mode_;

  // If the function is templated, stores the ParseResumeLocation for the
  // function's SQL expression body.  Otherwise is empty.  If present, this
  // ParseResumeLocation includes a string_view whose start and end indicate
  // the full SQL body, with a byte_offset of 0.  The string_view itself
  // points into the full module contents string that is owned by the related
  // LazyResolutionCatalog.  It's important that the string_view indicate only
  // the expression and not a larger string, since parsing will fail if there
  // is extra cruft after the expression.
  std::optional<ParseResumeLocation> templated_expression_resume_location_;

  // The lazy resolution object for this function, including all the
  // SQL, parser, and resolver logic related to the function.
  // TODO: To support multiple signatures for this function, this
  // will need to be a vector.
  LazyResolutionObject lazy_resolution_object_;

  // Details about the module that owns this object.
  const ModuleDetails module_details_;

  // The resolved Function.  This may be a SQLFunction or a
  // TemplatedSQLFunction.
  std::unique_ptr<Function> function_;

  // The GoogleSQL statement context in which this object was defined,
  // indicating the context in which it was analyzed or needs to be analyzed.
  // Specifically, this indicates if the object came from a module, which may
  // have some different resolution behavior.
  StatementContext statement_context_ = CONTEXT_DEFAULT;
};

// LazyResolutionTableFunction represents lazily resolved SQL
// TableValuedFunctions.  This is the basic class used for lazily resolving
// CREATE TABLE FUNCTION statements for both templated and non-templated TVFs.
// For non-templated TVFs, an instance of this class is returned as a result
// of FindTableValuedFunction() calls on the related LazyResolutionCatalog.
// For templated TVFs, the FindTableValuedFunction() call results in the
// creation of a LazyResolutionTemplatedSQLTVF that is owned by this class,
// and that LazyResolutionTemplatedSQLTVF is returned as the result of
// the FindTableValuedFunction() call.
class LazyResolutionTableFunction {
 public:
  // Creates a LazyResolutionTableFunction with the specified <function_group>,
  // <parser_output>, and <function_status>.  Returns an error if
  // <parser_output> is not an ASTCreateTableFunctionStatement, or if the
  // ASTCreateTableFunctionStatement name path does not have exactly one name,
  // or if the function is not public or private.
  //
  // The function name is derived from the <parser_output>.  <function_status>
  // can be an error, in which case attempts to resolve this table function or
  // lookup this table function from a Catalog will return <function_status>.
  //
  // <parse_resume_location> indicates the origin of the SQL statement and is
  // used for error messaging, and potentially for re-parsing if necessary.
  static absl::StatusOr<std::unique_ptr<LazyResolutionTableFunction>> Create(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output, absl::Status function_status,
      ErrorMessageOptions error_message_options,
      RemoteTvfFactory* remote_tvf_factory, ModuleDetails module_details);
  // Similar to the previous, but for templated TVFs.  The
  // <templated_expression_resume_location> indicates the TVF's SQL function
  // body that gets resolved every time the function is called (because the
  // arguments can be different on each call, the expression must be resolved
  // for each call).
  static absl::StatusOr<std::unique_ptr<LazyResolutionTableFunction>>
  CreateTemplatedTableFunction(const ParseResumeLocation& parse_resume_location,
                               const std::optional<ParseResumeLocation>&
                                   templated_expression_resume_location,
                               std::unique_ptr<ParserOutput> parser_output,
                               absl::Status function_status,
                               RemoteTvfFactory* remote_tvf_factory,
                               ModuleDetails module_details,
                               ErrorMessageOptions error_message_options);

  LazyResolutionTableFunction(const LazyResolutionTableFunction&) = delete;
  LazyResolutionTableFunction& operator=(const LazyResolutionTableFunction&) =
      delete;

  ~LazyResolutionTableFunction() = default;

  // Returns the table function's name (derived from <lazy_resolution_object_>).
  std::string Name() const;

  // Returns the resolution status if the table function has already been
  // resolved.
  // If the function has not been resolved yet, then:
  // 1) resolves the parser AST (via <lazy_resolution_object_>)
  // 2) sets the resolution status based on the result of analysis
  // 3) returns the resolution status.
  absl::Status ResolveAndUpdateIfNeeded(const AnalyzerOptions& analyzer_options,
                                        Catalog* catalog,
                                        TypeFactory* type_factory);

  // Returns the SQL passed into the Create() method (via
  // <parse_resume_location>). Used for error messaging, to construct the error
  // string with caret payload.
  absl::string_view SQL() const;

  // Indicates the location of the NameIdentifier in the SQL passed to the
  // Create() method.  Used for error messaging, to identify an error location
  // for this table function.
  const ASTIdentifier* NameIdentifier() const;

  // Returns the resolved TableValuedFunction (if this
  // LazyResolutionTableFunction resolved successfully).  Otherwise returns
  // nullptr (if this LazyResolutionTableFunction has not been resolved yet or
  // if it had a resolution error).
  const TableValuedFunction* ResolvedObject() const {
    return table_function_.get();
  }

  // TODO: Try to refactor [Full]DebugString().
  std::string DebugString(bool verbose = false) const;

  // A debug string that includes the resolved create function statement.
  // If <include_sql>, then the result also includes the original
  // GoogleSQL statement (possibly including whitespaces and comments after
  // the end of the previous statement and before the CREATE token, if
  // applicable).
  std::string FullDebugString(bool include_sql = false) const;

  // For LazyResolutionTableFunctions, errors might have been found during
  // validation or resolution, for example:
  // 1) invalid TEMP modifier found during Create()
  // 2) resolver errors found during expression analysis
  absl::Status resolution_status() const;

  // Set a Status that, if the Status is non-OK, will be produced when
  // resolution is attempted. This is used to store an error status that is
  // detected before resolution, but which shouldn't be visible until
  // resolution is attempted.
  void set_status_when_resolution_attempted(absl::Status status);

  // Returns whether or not the table function needs resolution.  It needs
  // resolution if its resolution_status() is OK and it does not have any
  // analyzer output yet.
  bool NeedsResolution() const;

  // If the statement resolved successfully, returns the deprecation warnings
  // from the analyzer. Otherwise returns NULL. Useful for tests.
  const std::vector<absl::Status>* AnalyzerDeprecationWarnings() const;

  // If the statement resolved successfully, returns the resolved statement.
  // Otherwise returns NULL.
  const ResolvedCreateTableFunctionStmt* ResolvedStatement() const;

  void set_statement_context(StatementContext context) {
    statement_context_ = context;
  }
  StatementContext statement_context() const { return statement_context_; }

 private:
  static absl::StatusOr<std::unique_ptr<LazyResolutionTableFunction>>
  CreateImpl(const ParseResumeLocation& parse_resume_location,
             const std::optional<ParseResumeLocation>&
                 templated_expression_resume_location,
             std::unique_ptr<ParserOutput> parser_output,
             absl::Status function_status, RemoteTvfFactory* remote_tvf_factory,
             ModuleDetails module_details,
             ErrorMessageOptions error_message_options);
  // Private constructor, called by Create().
  LazyResolutionTableFunction(const ASTIdentifier* table_function_name,
                              const ParseResumeLocation& parse_resume_location,
                              const std::optional<ParseResumeLocation>&
                                  templated_expression_resume_location,
                              std::unique_ptr<ParserOutput> parser_output,
                              absl::Status function_status,
                              RemoteTvfFactory* remote_tvf_factory,
                              ModuleDetails module_details,
                              ErrorMessageOptions error_message_options);

  // Returns true if the TVF is a templated TVF.
  bool IsTemplated() const;

  // The factory to create remote TVF. Remote TVF is disabled if it is nullptr.
  RemoteTvfFactory* const remote_tvf_factory_ = nullptr;

  // Details about the containing GoogleSQL module.
  const ModuleDetails module_details_;

  // If the TVF is templated, stores the ParseResumeLocation for the function's
  // SQL expression body.  Otherwise is empty.  If present, this
  // ParseResumeLocation includes a string_view whose start and end indicate the
  // full SQL body, with a byte_offset of 0.  The string_view itself points into
  // the full module contents string that is owned by the related
  // LazyResolutionCatalog.  It's important that the string_view indicate only
  // the expression and not a larger string, since parsing will fail if there is
  // extra cruft after the expression.
  std::optional<ParseResumeLocation> templated_expression_resume_location_;

  // The lazy resolution object for this function, including all the
  // SQL, parser, and resolver logic related to the function.
  // TODO: To support multiple signatures for this TVF, this
  // will need to be a vector.
  LazyResolutionObject lazy_resolution_object_;

  // The resolved TableValuedFunction.  This may be a SQLTableValuedFunction
  // or a TemplatedSQLTVF.
  std::unique_ptr<TableValuedFunction> table_function_;

  // The GoogleSQL statement context in which this object was defined,
  // indicating the context in which it was analyzed or needs to be analyzed.
  // Specifically, this indicates if the object came from a module, which may
  // have some different resolution behavior.
  StatementContext statement_context_ = CONTEXT_DEFAULT;
};

// LazyResolutionConstant represents lazily resolved named constants.
//
// This class is used by the ModuleCatalog to represent named constants at three
// different stages of query analysis:
// 1) After the CREATE CONSTANT statement has been parsed;
// 2) After the CREATE CONSTANT statement has been resolved; and
// 3) After the constant expression in the definition (i.e., the RHS in the
//    CREATE CONSTANT statement) has been evaluated.
//
// Note that constant expressions are evaluated using an engine callback as part
// of the catalog lookup (i.e., by ModuleCatalog::FindConstant()), not during
// analysis.
//
// See (broken link) for details, in particular, the
// "Constant Evaluation" section which explains the engine callback.
class LazyResolutionConstant {
 public:
  // Creates a LazyResolutionConstant with the specified <parser_output> and
  // <constant_status>. Returns an error if <parser_output> is not an
  // ASTCreateConstantStatement, if the ASTCreateConstantStatement name path
  // does not have exactly one name, or if the named constant is not public or
  // private.
  //
  // The constant name is derived from the <parser_output>. <constant_status>
  // can be an error, in which case attempts to resolve this constant or
  // lookup this constant from a Catalog will return <constant_status>.
  // <parse_resume_location> indicates the origin of the SQL statement and is
  // used for error messaging.
  static absl::StatusOr<std::unique_ptr<LazyResolutionConstant>> Create(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output, absl::Status constant_status,
      ErrorMessageOptions error_message_options);

  ~LazyResolutionConstant() = default;

  // This class is neither copyable nor assignable.
  LazyResolutionConstant(
      const LazyResolutionConstant& other_lazy_resolution_constant) = delete;
  LazyResolutionConstant& operator=(
      const LazyResolutionConstant& other_lazy_resolution_constant) = delete;

  // Returns the function's name (derived from <lazy_resolution_object_>).
  std::string Name() const;

  // Sets the value of this named constant. Used to save the result of
  // evaluating the constant expression in the definition.  Returns an error
  // if the value cannot be set (for example, if the LazyResolutionConstant
  // has no ResolvedExpr, the Value Type and ResolvedExpr Type do not
  // match, or the constant does not need evaluation).
  absl::Status SetValue(const Value& value);

  // Returns the named constant's resolved SQL expression, or nullptr if
  // the constant has not been successfully resolved (not resolved yet or
  // resolution failed).
  const ResolvedExpr* constant_expression() const;

  // Returns the resolution status if the named constant has already been
  // resolved. If the named constant has not been resolved yet, then:
  // 1) Resolves the parser AST (via <lazy_resolution_object_>);
  // 2) Sets the resolution status based on the result of the analysis;
  // 3) Returns the resolution status.
  absl::Status ResolveAndUpdateIfNeeded(const AnalyzerOptions& analyzer_options,
                                        Catalog* catalog,
                                        TypeFactory* type_factory);

  // Returns whether or not the named constant needs resolution.  It needs
  // resolution if its resolution_status() is OK and it does not have any
  // analyzer output.
  bool NeedsResolution() const;

  // Returns whether or not the named constant needs evaluation.  It needs
  // evaluation if it does not have a valid value.
  bool NeedsEvaluation() const;

  // Returns the SQL passed into the Create() method (via
  // parse_resume_location).
  absl::string_view SQL() const;

  // Returns the named constant identifier used in the declaration.
  const ASTIdentifier* NameIdentifier() const;

  // Returns the SQLConstant (if the LazyResolutionConstant resolved
  // successfully).  Otherwise returns nullptr (if this LazyResolutionConstant
  // has not been resolved yet or if it had a resolution error).
  const Constant* ResolvedObject() const { return sql_constant_.get(); }

  // TODO: Try to refactor [Full]DebugString().
  std::string DebugString(bool verbose = false) const;

  // A debug string that includes the resolved CREATE CONSTANT statement.
  // If <include_sql>, then the result also includes the original
  // GoogleSQL statement (possibly including whitespaces and comments after
  // the end of the previous statement and before the CREATE token, if
  // applicable).
  std::string FullDebugString(bool include_sql = false) const;

  // Returns the combined resolution and evaluation status of this named
  // constant. If there are resolution errors, these errors are returned;
  // otherwise any evaluation errors are returned.
  absl::Status resolution_or_evaluation_status() const;

  // Returns the resolution status of this named constant.
  //
  // Updated by ResolveAndUpdateIfNeeded(). For LazyResolutionConstants, errors
  // might have been found during validation or resolution, for example:
  // 1) Invalid TEMP modifier found during Create();
  // 2) Resolver errors found during expression analysis.
  //
  // Note that this method ignores the outcome of evaluating the constant
  // expression. Use resolution_or_evaluation_status() to check the Constant
  // for the overall status, including both resolution and evaluation.
  absl::Status resolution_status() const;

  // Sets the evaluation Status of this named constant.  Used to save the result
  // of evaluating the constant expression in the definition.  Returns an error
  // if the value cannot be set (for example, the <status> must be an error
  // Status).
  absl::Status set_evaluation_status(const absl::Status& status);

  // If the statement resolved successfully, returns the resolved statement.
  // Otherwise returns null.
  const ResolvedCreateConstantStmt* ResolvedStatement() const;

 private:
  // Creates a named constant with <constant_name>. Crashes if <constant_name>
  // is empty.
  LazyResolutionConstant(const ASTIdentifier* constant_name,
                         const ParseResumeLocation& parse_resume_location,
                         std::unique_ptr<ParserOutput> parser_output,
                         absl::Status constant_status,
                         ErrorMessageOptions error_message_options);

  // If the Constant has been resolved, returns its resolved type.  Otherwise
  // returns nullptr.
  const Type* ResolvedType() const;

  // The lazy resolution object for this named constant, including all the
  // SQL, parser, and resolver logic related to the constant.
  LazyResolutionObject lazy_resolution_object_;

  // Indicates the Constant's evaluation Status.  If the constant expression
  // has not been evaluated yet then it is OK.
  absl::Status evaluation_status_;

  // The resolved Constant.  The Constant may or may not have been evaluated
  // yet.  Is initialized to a Constant with an invalid value upon resolution
  // of this Constant.  Before resolution, is empty.
  std::unique_ptr<SQLConstant> sql_constant_;

  FRIEND_TEST(LazyResolutionConstantTest,
              ResolutionSetsResolvedASTAndTypeButNotValue);
};

// LazyResolutionView provides lazy resolution for SQL Views in a
// LazyResolutionCatalog. This class is used for lazily resolving CREATE VIEW
// statements. Once the view is resolved, a new SQLView is created and is
// stored in `view_`. LazyResolutionCatalog lookups will return the created
// `view_` (not this LazyResolutionView).
class LazyResolutionView {
 public:
  // Creates a LazyResolutionView with the specified `parser_output` and
  // `view_status`. Returns an error if `parser_output` is not an
  // ASTCreateViewStatement, if the ASTCreateViewStatement name path does not
  // have exactly one name, or if the view is not public or private.
  //
  // The view name is derived from the `parser_output`. `view_status` can be an
  // error, in which case attempts to resolve this view or lookup this view from
  // a Catalog will return `view_status`.
  static absl::StatusOr<std::unique_ptr<LazyResolutionView>> Create(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output, absl::Status view_status,
      ErrorMessageOptions error_message_options);

  LazyResolutionView(const LazyResolutionView&) = delete;
  LazyResolutionView& operator=(const LazyResolutionView&) = delete;

  ~LazyResolutionView() = default;

  // Returns the view's name (derived from `lazy_resolution_object_`).
  std::string Name() const;

  std::string DebugString() const;

  // For LazyResolutionViews, errors might have been found during validation or
  // resolution, for example:
  // 1) invalid TEMP modifier found during Create()
  // 2) resolver errors found during analysis
  absl::Status resolution_status() const;

  // Returns whether the view needs resolution. It needs resolution if the
  // resolution_status() is OK and it does not have any analyzer output.
  bool NeedsResolution() const;

  // Returns the SQL passed into the Create() method (via
  // <parse_resume_location>). Used for error messaging, to construct the error
  // string with caret payload.
  absl::string_view SQL() const;

  // Indicates the location of the NameIdentifier in the SQL passed to the
  // Create() method. Used for error messaging, to identify an error location
  // for this view.
  const ASTIdentifier* NameIdentifier() const;

  // Returns the resolution status if the view has already been resolved.
  // If the view has not been resolved yet, then:
  // 1) resolves the parser AST (via <lazy_resolution_object_>)
  // 2) sets the resolution status based on the result of analysis
  // 3) returns the resolution status.
  absl::Status ResolveAndUpdateIfNeeded(const AnalyzerOptions& analyzer_options,
                                        Catalog* catalog,
                                        TypeFactory* type_factory);

  // Returns the resolved view (if this LazyResolutionView resolved
  // successfully). Otherwise returns nullptr (if this LazyResolutionView
  // has not been resolved yet or if it had a resolution error).
  const SQLView* ResolvedObject() const { return view_.get(); }

  // If the statement resolved successfully, returns the resolved statement.
  // Otherwise returns nullptr.
  const ResolvedCreateViewStmt* ResolvedStatement() const;

  // Set a Status that, if the Status is non-OK, will be produced when
  // resolution is attempted. This is used to store an error status that is
  // detected before resolution, but which shouldn't be visible until resolution
  // is attempted.
  void set_status_when_resolution_attempted(absl::Status status);

  // If the statement resolved successfully, returns the deprecation warnings
  // from the analyzer. Otherwise returns NULL. Useful for tests.
  const std::vector<absl::Status>* AnalyzerDeprecationWarnings() const {
    return lazy_resolution_object_.AnalyzerDeprecationWarnings();
  };

 private:
  LazyResolutionView(const ASTIdentifier* view_name,
                     const ParseResumeLocation& parse_resume_location,
                     std::unique_ptr<ParserOutput> parser_output,
                     absl::Status view_status,
                     ErrorMessageOptions error_message_options);

  // The lazy resolution object for this view, including all the SQL, parser
  // and resolver logic related to the view.
  LazyResolutionObject lazy_resolution_object_;

  // Resolved view
  std::unique_ptr<SQLView> view_;
};

// LazyResolutionProcedure provides lazy resolution for SQL Procedures in a
// LazyResolutionCatalog. This class is used for lazily resolving CREATE
// PROCEDURE statements. Once the procedure is resolved, a new Procedure is
// created and is stored in `procedure_`. LazyResolutionCatalog lookups will
// return the created `procedure_`.
class LazyResolutionProcedure {
 public:
  // Creates a LazyResolutionProcedure with the specified <parser_output>
  // and <procedure_status>.  Returns an error if <parser_output> is not an
  // ASTCreateProcedureStatement, if the ASTCreateProcedureStatement name path
  // does not have exactly one name, or if the procedure is not public or
  // private.
  //
  // The procedure name is derived from the <parser_output>.  <procedure_status>
  // can be an error, in which case attempts to resolve this procedure or
  // lookup this procedure from a Catalog will return <procedure_status>.
  //
  // <parse_resume_location> indicates the origin of the SQL statement and is
  // used for error messaging.
  static absl::StatusOr<std::unique_ptr<LazyResolutionProcedure>> Create(
      const ParseResumeLocation& parse_resume_location,
      std::unique_ptr<ParserOutput> parser_output,
      absl::Status procedure_status, ErrorMessageOptions error_message_options,
      ModuleDetails module_details);

  LazyResolutionProcedure(const LazyResolutionProcedure&) = delete;
  LazyResolutionProcedure& operator=(const LazyResolutionProcedure&) = delete;

  ~LazyResolutionProcedure() = default;

  // Returns the procedure's name (derived from <lazy_resolution_object_>).
  std::string Name() const;

  // Returns the resolution status if the procedure has already been resolved.
  // If the procedure has not been resolved yet, then:
  // 1) resolves the parser AST (via <lazy_resolution_object_>).
  // 2) sets the resolution status based on the result of analysis.
  // 3) returns the resolution status.
  absl::Status ResolveAndUpdateIfNeeded(const AnalyzerOptions& analyzer_options,
                                        Catalog* catalog,
                                        TypeFactory* type_factory);

  // Returns the SQL passed into the Create() method (via
  // <parse_resume_location>). Used for error messaging, to construct the error
  // string with caret payload.
  std::string_view SQL() const;

  // Indicates the location of the Name identifier in the SQL passed to the
  // Create() method (via <parse_resume_location>). Used for error messaging, to
  // identify an error location for this procedure.
  const ASTIdentifier* NameIdentifier() const;

  // Returns the resolved Procedure (if this LazyResolutionProcedure resolved
  // successfully).  Otherwise returns nullptr (if this LazyResolutionProcedure
  // has not been resolved yet or if it had a resolution error).
  const SQLProcedure* ResolvedObject() const { return procedure_.get(); }

  std::string DebugString() const;

  // For LazyResolutionProcedures, errors might have been found during
  // validation or resolution, for example:
  // 1) Invalid TEMP modifier found.
  // 2) Resolver errors found during analysis.
  absl::Status resolution_status() const;

  // Set a Status that, if the Status is non-OK, will be produced when
  // resolution is attempted. This is used to store an error status that is
  // detected before resolution, but which shouldn't be visible until
  // resolution is attempted.
  void set_status_when_resolution_attempted(absl::Status status);

  // Returns whether or not the procedure needs resolution. It needs resolution
  // if resolution_status() is OK and it does not have any analyzer output.
  bool NeedsResolution() const;

  // If the statement resolved successfully, returns the deprecation warnings
  // from the analyzer. Otherwise returns NULL. Useful for tests.
  const std::vector<absl::Status>* AnalyzerDeprecationWarnings() const;

  // If the statement resolved successfully, returns the resolved statement.
  // Otherwise returns NULL.
  const ResolvedCreateProcedureStmt* ResolvedStatement() const;

  // Returns a string that was used when parsing the CREATE PROCEDURE statement.
  // If <include_prefix> is true, then all whitespace and comments starting
  // immediately after the end of the previous statement are included.
  // Otherwise, the returned string begins with the CREATE token/keyword.
  absl::string_view GetCreateStatement(bool include_prefix) const {
    return lazy_resolution_object_.GetCreateStatement(include_prefix);
  }

 private:
  // Private constructor, called by Create().
  LazyResolutionProcedure(const ASTIdentifier* procedure_name,
                          const ParseResumeLocation& parse_resume_location,
                          std::unique_ptr<ParserOutput> parser_output,
                          absl::Status procedure_status,
                          ErrorMessageOptions error_message_options,
                          ModuleDetails module_details);

  // Returns the procedure's argument names.
  std::vector<std::string> ArgumentNames() const;

  // The lazy resolution object for this procedure, including all the
  // SQL, parser, and resolver logic related to the procedure.
  LazyResolutionObject lazy_resolution_object_;

  // The resolved Procedure.
  std::unique_ptr<SQLProcedure> procedure_;

  // Details about the containing GoogleSQL module.
  const ModuleDetails module_details_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_LAZY_RESOLUTION_CATALOG_H_
