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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_TOOL_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_TOOL_H_

#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/options_utils.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/module_factory.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/sql_builder.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"

namespace googlesql {

// Configuration data on how `ExecuteQuery` should behave.
class ExecuteQueryConfig {
 public:
  ExecuteQueryConfig();

  ExecuteQueryConfig(const ExecuteQueryConfig&) = delete;
  ExecuteQueryConfig& operator=(const ExecuteQueryConfig&) = delete;

  enum class ToolMode {
    // Parse the query, and print a debug string of the parsed AST.
    kParse,

    // Parse the query, print an 'unparse' of the input using the parsers
    // Unparse API. Should be semantically equivalent.
    kUnparse,

    // Resolve the query, and print the ResolveAST
    kResolve,

    // Analyze the query, then run 'sqlbuilder' to produce a semantically
    // equivalent sql query (...mostly).
    kUnAnalyze,

    // Prepare the query, and print a representation of the query from the
    // reference implementation.
    kExplain,

    // Execute the query and pretty print the result.
    kExecute,
  };

  enum class SqlMode {
    // Treat sql as a query, the output is a table.
    kQuery,

    // Treat sql as an expression, the output is a single value.
    kExpression,

    // Treat sql as a script, the output is result of multiple queries.
    kScript,
  };

  void clear_tool_modes() { tool_modes_.clear(); }
  void add_tool_mode(ToolMode tool_mode) { tool_modes_.insert(tool_mode); }
  bool has_tool_mode(ToolMode tool_mode) const {
    return tool_modes_.contains(tool_mode);
  }
  const absl::flat_hash_set<ToolMode>& tool_modes() const {
    return tool_modes_;
  }
  void set_tool_modes(const absl::flat_hash_set<ToolMode>& tool_modes) {
    tool_modes_ = tool_modes;
  }

  // Returns the tool mode if the mode string matches one of the tool modes.
  static std::optional<ToolMode> parse_tool_mode(absl::string_view mode);

  // Returns the name of the tool mode.
  static absl::string_view tool_mode_name(ToolMode tool_mode);

  // Returns the sql mode if the mode string matches one of the sql modes.
  static std::optional<SqlMode> parse_sql_mode(absl::string_view mode);

  // Returns the name of the sql mode.
  static absl::string_view sql_mode_name(SqlMode sql_mode);

  void set_sql_mode(SqlMode sql_mode) { sql_mode_ = sql_mode; }

  SqlMode sql_mode() const { return sql_mode_; }

  static std::optional<SQLBuilder::TargetSyntaxMode> parse_target_syntax_mode(
      absl::string_view mode);

  static absl::string_view target_syntax_mode_name(
      SQLBuilder::TargetSyntaxMode target_syntax_mode);

  void set_target_syntax_mode(SQLBuilder::TargetSyntaxMode target_syntax_mode) {
    target_syntax_mode_ = target_syntax_mode;
  }
  SQLBuilder::TargetSyntaxMode target_syntax_mode() const {
    return target_syntax_mode_;
  }

  // If true, Unicode box characters are used for the resolved AST and result
  // tables output. If false, ASCII characters (e.g. "+---+") are used.
  void set_use_box_glyphs(bool value) { use_box_glyphs_ = value; }
  bool use_box_glyphs() const { return use_box_glyphs_; }

  // Defaults matches AnalyzerOptions default.
  const AnalyzerOptions& analyzer_options() const { return analyzer_options_; }
  AnalyzerOptions& mutable_analyzer_options() { return analyzer_options_; }

  const EvaluatorOptions& evaluator_options() const {
    return evaluator_options_;
  }
  EvaluatorOptions& mutable_evaluator_options() { return evaluator_options_; }

  const ParameterValueMap& query_parameter_values() {
    return query_parameter_values_;
  }
  ParameterValueMap& mutable_query_parameter_values() {
    return query_parameter_values_;
  }

  // This is the Catalog to use for lookups.  It's a MultiCatalog containing
  // the wrapper_catalog, base_catalog and builtins_catalog.
  Catalog* catalog() { return catalog_.get(); }

  // Set the base catalog, which is used to find tables, custom functions, etc.
  // It doesn't need to include builtin functions since those are provided by
  // the builtins_catalog.
  // nullptr is allowed if there is no base catalog.
  void SetBaseCatalog(Catalog* catalog);

  // Version of SetBaseCatalog that takes also ownership of the Catalog.
  void SetOwnedBaseCatalog(std::unique_ptr<Catalog> catalog);

  // The set of builtis functions and types is determined by the LanguageOptions
  // passed in.
  void SetBuiltinsCatalogFromLanguageOptions(
      const LanguageOptions& language_options);

  SimpleCatalog* builtins_catalog() { return builtins_catalog_.get(); }
  SimpleCatalog* wrapper_catalog() { return &wrapper_catalog_; }
  SimpleCatalog* imported_modules_catalog() {
    return imported_modules_catalog_.get();
  }

  void SetImportPaths(const std::vector<std::string>& paths);
  ModuleFactory* module_factory() { return module_factory_.get(); }

  // A TypeFactory that can be used for creating tables for this request.
  TypeFactory* type_factory() { return &type_factory_; }

  using ExamineResolvedASTCallback =
      std::function<absl::Status(const ResolvedNode* node)>;

  // If provided, this callback will be invoked before evaluating the query
  // (or whatever action is specified by tool mode).  If an error is returned
  // it will be propagated back as an error in `ExecuteQuery`.
  const ExamineResolvedASTCallback& examine_resolved_ast_callback() const {
    return examine_resolved_ast_callback_;
  }

  void set_examine_resolved_ast_callback(ExamineResolvedASTCallback callback) {
    examine_resolved_ast_callback_ = std::move(callback);
  }

  absl::Status SetCatalogFromString(absl::string_view value);

  absl::Status SetTargetSyntaxModeFromString(
      absl::string_view target_syntax_mode);

  // Set the google::protobuf::DescriptorPool to use when resolving types.
  // The DescriptorPool can only be set once and cannot be changed.
  void SetDescriptorPool(const google::protobuf::DescriptorPool* pool);
  void SetOwnedDescriptorPool(
      std::unique_ptr<const google::protobuf::DescriptorPool> pool);
  void SetOwnedDescriptorDatabase(
      std::unique_ptr<google::protobuf::DescriptorDatabase> db);

  const google::protobuf::DescriptorPool* descriptor_pool() const {
    return descriptor_pool_;
  }

  const parser::macros::MacroCatalog& macro_catalog() const {
    return macro_catalog_;
  }
  parser::macros::MacroCatalog& mutable_macro_catalog() {
    return macro_catalog_;
  }
  const std::list<std::string>& macro_sources() const { return macro_sources_; }
  std::list<std::string>& mutable_macro_sources() { return macro_sources_; }

  ExecuteQueryConfig& AddArtifact(
      std::unique_ptr<const ParserOutput> parser_output) {
    parser_artifacts_.push_back(std::move(parser_output));
    return *this;
  }

  ExecuteQueryConfig& AddArtifact(
      std::unique_ptr<const AnalyzerOutput> analyzer_output) {
    analyzer_artifacts_.push_back(std::move(analyzer_output));
    return *this;
  }

  ExecuteQueryConfig& AddArtifact(
      std::unique_ptr<PreparedStatement> prepared_stmt) {
    prepared_stmts_.push_back(std::move(prepared_stmt));
    return *this;
  }

 private:
  // Rebuild the multi-catalog from the builtin, wrapper and base catalogs.
  void RebuildMultiCatalog();
  void RebuildModuleFactory();

  ExamineResolvedASTCallback examine_resolved_ast_callback_ = nullptr;
  // if no tool modes are added then Execute is the default mode.
  absl::flat_hash_set<ToolMode> tool_modes_ = {ToolMode::kExecute};

  SqlMode sql_mode_ = SqlMode::kQuery;

  // For imported modules.
  std::vector<std::string> import_paths_;
  std::unique_ptr<ModuleFactory> module_factory_;

  // The syntax to use when generating SQL from the resolved AST in
  // UnAnalyze tool mode.
  SQLBuilder::TargetSyntaxMode target_syntax_mode_ =
      SQLBuilder::TargetSyntaxMode::kStandard;

  bool use_box_glyphs_ = true;

  AnalyzerOptions analyzer_options_;

  // The effective Catalog is a MultiCatalog with
  //   wrapper_catalog  - any tables or types added based on flags
  //   base_catalog     - the Catalog of tables, etc from SelectableCatalogs.
  //   builtins_catalog - the Catalog providing built-in functions, set up
  //                      based on LanguageOptions inferred from config.
  //   imported_modules_catalog - any modules loaded via IMPORT MODULE
  std::unique_ptr<SimpleCatalog> builtins_catalog_;
  Catalog* base_catalog_ = nullptr;  // Not owned, may be nullptr.
  std::unique_ptr<Catalog> owned_base_catalog_;
  SimpleCatalog wrapper_catalog_;
  std::unique_ptr<SimpleCatalog> imported_modules_catalog_;
  std::unique_ptr<MultiCatalog> catalog_;

  TypeFactory type_factory_;

  EvaluatorOptions evaluator_options_;
  ParameterValueMap query_parameter_values_;
  const google::protobuf::DescriptorPool* descriptor_pool_ = nullptr;
  std::unique_ptr<const google::protobuf::DescriptorPool> owned_descriptor_pool_;
  std::unique_ptr<google::protobuf::DescriptorDatabase> descriptor_db_;
  parser::macros::MacroCatalog macro_catalog_;
  // std::list, not a vector, because we need stability. The entries in
  // `macro_catalog_` have string_views into these sources.
  std::list<std::string> macro_sources_;

  // These are used to keep parsing and analysis artifacts alive.
  //
  // - `parser_artifacts_`: Resolved ASTs may store references to parse
  //   locations stored in a `ParserOutput`.
  // - `analyzer_artifacts_`: Some DDLs objects, such as `SqlConstant`, store
  //   references to the resolved asts in an `AnalyzerOutput`.
  // - `prepared_stmts_`: A multi-stmt can contain DDLs, so the analyzer outputs
  //   stored in a `PreparedStatement` should stay alive.
  std::vector<std::unique_ptr<const ParserOutput>> parser_artifacts_;
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_artifacts_;
  std::vector<std::unique_ptr<PreparedStatement>> prepared_stmts_;
};

absl::Status SetToolModeFromFlags(ExecuteQueryConfig& config);

absl::Status SetSqlModeFromFlags(ExecuteQueryConfig& config);

absl::Status SetTargetSyntaxModeFromFlags(ExecuteQueryConfig& config);

absl::Status SetDescriptorPoolFromFlags(ExecuteQueryConfig& config);

absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromCsvFile(
    absl::string_view table_name, absl::string_view path);

absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromBinaryProtoFile(
    absl::string_view table_name, absl::string_view path,
    const ProtoType* column_proto_type);

absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromTextProtoFile(
    absl::string_view table_name, absl::string_view path,
    const ProtoType* column_proto_type);

absl::Status AddTablesFromFlags(ExecuteQueryConfig& config);

absl::StatusOr<std::unique_ptr<ExecuteQueryWriter>> MakeWriterFromFlags(
    const ExecuteQueryConfig& config, std::ostream& output);

// Note: Currently this only sets product_mode
// TODO: expand this to support setting other language features
//                  via flag.
absl::Status SetLanguageOptionsFromFlags(ExecuteQueryConfig& config);

absl::Status SetAnalyzerOptionsFromFlags(ExecuteQueryConfig& config);

absl::Status SetEvaluatorOptionsFromFlags(ExecuteQueryConfig& config);

// Set query parameters in analyzer options as well as for use in the evaluator.
absl::Status SetQueryParametersFromFlags(ExecuteQueryConfig& config);

// Initialize an ExecuteQueryConfig with default values and values from flags.
absl::Status InitializeExecuteQueryConfig(ExecuteQueryConfig& config);

// Execute the query according to `config`. `config` is logically const, but due
// to GoogleSQL calling conventions related to Catalog objects, must be
// non-const.
absl::Status ExecuteQuery(absl::string_view sql, ExecuteQueryConfig& config,
                          ExecuteQueryWriter& writer);

}  // namespace googlesql

// Exposed for tests only
ABSL_DECLARE_FLAG(std::vector<std::string>, mode);
ABSL_DECLARE_FLAG(googlesql::internal::EnabledAstRewrites,
                  enabled_ast_rewrites);
ABSL_DECLARE_FLAG(std::string, product_mode);
ABSL_DECLARE_FLAG(std::string, catalog);
ABSL_DECLARE_FLAG(bool, strict_name_resolution_mode);
ABSL_DECLARE_FLAG(bool, fold_literal_cast);
ABSL_DECLARE_FLAG(std::string, sql_mode);
ABSL_DECLARE_FLAG(std::string, target_syntax);
ABSL_DECLARE_FLAG(std::string, table_spec);
ABSL_DECLARE_FLAG(std::string, descriptor_pool);
ABSL_DECLARE_FLAG(std::string, output_mode);
ABSL_DECLARE_FLAG(std::string, parameters);
ABSL_DECLARE_FLAG(int64_t, evaluator_max_value_byte_size);
ABSL_DECLARE_FLAG(int64_t, evaluator_max_intermediate_byte_size);
ABSL_DECLARE_FLAG(int64_t, max_statements_to_execute);
ABSL_DECLARE_FLAG(std::string, parse_location_record_type);
ABSL_DECLARE_FLAG(std::optional<googlesql::internal::EnabledLanguageFeatures>,
                  enabled_language_features);

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_TOOL_H_
