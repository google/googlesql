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

#ifndef GOOGLESQL_PUBLIC_SIMPLE_CATALOG_UTIL_H_
#define GOOGLESQL_PUBLIC_SIMPLE_CATALOG_UTIL_H_

// This file contains helper methods for handling SQL CREATE statements (or
// their resolved forms) and creating objects that can be loaded into a
// SimpleCatalog (or other Catalogs).

#include <memory>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Compiles `create_sql_stmt` and adds the resulting function to `catalog`.
//
// `create_sql_stmt`: Must be a CREATE FUNCTION statement.
// `analyzer_options`: Analyzer options used to analyze `create_sql_stmt`.
// `allow_persistent_function`: Unless this is set to true, the utility is
//     restricted to CREATE TEMP FUNCTION. Use with caution, SimpleCatalog does
//     not fully support a distinction between temp and persistent functions.
// `function_options`: FunctionOptions to be applied to the created function.
//     If not supplied a reasonable default for user-defined functions is used.
// `analyzer_output`: Analyzer outputs from compiling `create_sql_stmt`. The
//     lifetime of `analyzer_output` must exceed the lifetime of `catalog`.
//     The language options must support RESOLVED_CREATE_FUNCTION_STMT.
// `resolving_catalog`: The catalog to use for resolving names found during
//     analysis. This can be the same catalog as `catalog`.
// `catalog`: A SimpleCatalog that will own the created SQLFunction* object.
absl::Status AddFunctionFromCreateFunction(
    absl::string_view create_sql_stmt, const AnalyzerOptions& analyzer_options,
    bool allow_persistent_function,
    const FunctionOptions* /*absl_nullable*/ function_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    Catalog& resolving_catalog, SimpleCatalog& catalog);

// Creates a Function object from a ResolvedCreateFunctionStmt.
// `create_function_stmt` must outlive the returned Function.
// `function_options` - if provided will be used to construct the Function.
absl::StatusOr<std::unique_ptr<Function>> MakeFunctionFromCreateFunction(
    const ResolvedCreateFunctionStmt& create_function_stmt,
    const FunctionOptions* /*absl_nullable*/ function_options);

// Compiles `create_tvf_stmt` and adds the resulting TVF to `catalog`.
//
// `create_tvf_stmt`: Must be a CREATE TABLE FUNCTION statement, with
//    the restrictions described on `MakeTVFFromCreateTableFunction` below.
// `analyzer_options`: Analyzer options used to analyze the statement.
//     The language options must support RESOLVED_CREATE_TABLE_FUNCTION_STMT.
//     FEATURE_CREATE_TABLE_FUNCTION is always required, and
//     FEATURE_TEMPLATE_FUNCTIONS is for templated TVFs.
// `allow_persistent`: Unless this is set to true, the utility is
//     restricted to CREATE TEMP. Use with caution, SimpleCatalog does
//     not fully support a distinction between temp and persistent functions.
// `analyzer_output`: Analyzer outputs from compililing the statement. The
//     lifetime of `analyzer_output` must exceed the lifetime of `catalog`.
// `catalog`: A SimpleCatalog that will own the created TVF.
absl::Status AddTVFFromCreateTableFunction(
    absl::string_view create_tvf_stmt, const AnalyzerOptions& analyzer_options,
    bool allow_persistent,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    SimpleCatalog& catalog);

// Creates a TableValuedFunction object from a ResolvedCreateTableFunctionStmt.
// `stmt` must outlive the returned object.
// This supports:
//   Non-templated SQL TVFs, returning SQLTableValuedFunction.
//   Templated SQL TVFs, returning TemplatedSQLTVF.
//   Non-SQL TVFs with fixed output schemas, returning FixedOutputSchemaTVF.
// Other TVFs are not handled since they require code to resolve the
// output schema.
absl::StatusOr<std::unique_ptr<TableValuedFunction>>
MakeTVFFromCreateTableFunction(const ResolvedCreateTableFunctionStmt& stmt);

// Adds a `Table` object to `catalog` for the view defined by
// `create_view_stmt`.
//
// `create_view_stmt`: Must be a CREATE VIEW statement.
// `analyzer_options`: Analyzer options used to analyze `create_view_stmt`.
// `allow_non_temp`: If false, require statements to specify `TEMP` views.
// `analyzer_output`: Analyzer outputs from compiling `create_view_stmt`. The
//     lifetime of `analyzer_output` must exceed the lifetime of `catalog`.
//     `analyzer_options.language()` must support
//     `RESOLVED_CREATE_VIEW_STMT`.
// `catalog`: A SimpleCatalog that will own the created SQLView* object.
absl::Status AddViewFromCreateView(
    absl::string_view create_view_stmt, const AnalyzerOptions& analyzer_options,
    bool allow_non_temp, std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    SimpleCatalog& catalog);

// Adds a `Table` object to `catalog` for the table defined by
// `create_table_stmt`.
//
// `create_table_stmt`: Must be a CREATE TABLE statement (without AS SELECT).
// `analyzer_options`: Analyzer options used to analyze `create_table_stmt`.
//     `analyzer_options.language()` must support `RESOLVED_CREATE_TABLE_STMT`.
// `allow_non_temp`: If false, require `CREATE TEMP`.
// `analyzer_output`: Analyzer outputs from compiling `create_table_stmt`. The
//     lifetime of `analyzer_output` must exceed the lifetime of `catalog`.
// `table`: The table created inside `catalog`.
// `catalog`: A SimpleCatalog that will own the created SQLView* object.
absl::Status AddTableFromCreateTable(
    absl::string_view create_table_stmt,
    const AnalyzerOptions& analyzer_options, bool allow_non_temp,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output, SimpleTable*& table,
    SimpleCatalog& catalog);

// Construct a SimpleTable from a ResolvedCreateTableStmtBase.
// This works for ResolvedCreateTableStmt and ResolvedCreateTableAsSelectStmt
// but doesn't process the query or set up table contents for either of them.
absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromCreateTable(
    const ResolvedCreateTableStmtBase& create_table_stmt);

// Creates a SQLConstant from a ResolvedCreateConstantStmt.
// `stmt` must outlive the returned object.
// The returned SQLConstant will not be evaluated so will not have a Value yet.
// It's the caller's responsibility to evaluate the expression and set a Value.
absl::StatusOr<std::unique_ptr<SQLConstant>> MakeConstantFromCreateConstant(
    const ResolvedCreateConstantStmt& stmt);

// Adds a `PropertyGraph` object to `catalog` for the property graph defined by
// `create_property_graph_stmt`.
//
// `create_property_graph_stmt`: Must be a CREATE PROPERTY GRAPH statement.
// `analyzer_options`: Analyzer options used to analyze
// `create_property_graph_stmt`.
// `analyzer_output`: Analyzer outputs from
// compiling `create_property_graph_stmt`. The
//     lifetime of `analyzer_output` must exceed the lifetime of `catalog`.
//     `analyzer_options.language()` must support
//     `RESOLVED_CREATE_PROPERTY_GRAPH_STMT`.
// `catalog`: A SimpleCatalog that will own the created PropertyGraph* object.
absl::Status AddPropertyGraphFromCreatePropertyGraphStmt(
    absl::string_view create_property_graph_stmt,
    const AnalyzerOptions& analyzer_options,
    std::vector<std::unique_ptr<const AnalyzerOutput>>& artifacts,
    SimpleCatalog& catalog);

// Adds a `Constant` object to `catalog` for the constant defined by
// `create_constant_stmt`.
//
// `create_constant_stmt`: Must be a CREATE CONSTANT statement.
// `analyzer_options`: Analyzer options used to analyze `create_constant_stmt`.
//     `analyzer_options.constant_evaluator()` will be used to evaluate the
//     constant expression and must be supplied by the caller.
// `analyzer_output`: Analyzer outputs from compiling `create_constant_stmt`.
//     The lifetime of `analyzer_output` must exceed the lifetime of `catalog`.
//     `analyzer_options.language()` must support
//     `RESOLVED_CREATE_CONSTANT_STMT`.
// `catalog`: A SimpleCatalog that will own the created SqlConstant* object.
absl::Status AddConstantFromCreateConstant(
    absl::string_view create_constant_stmt,
    const AnalyzerOptions& analyzer_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    SimpleCatalog& catalog);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SIMPLE_CATALOG_UTIL_H_
