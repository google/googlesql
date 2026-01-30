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

#ifndef GOOGLESQL_PUBLIC_ANALYZER_H_
#define GOOGLESQL_PUBLIC_ANALYZER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/value.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class ParseResumeLocation;

// Associates each system variable with its current value.
using SystemVariableValuesMap =
    std::map<std::vector<std::string>, Value, StringVectorCaseLess>;

// Analyze a GoogleSQL statement.
//
// This can return errors that point at a location in the input. How this
// location is reported is given by <options.error_message_mode()>.
absl::Status AnalyzeStatement(
    absl::string_view sql, const AnalyzerOptions& options_in,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// Analyze one statement from a string that may contain multiple statements.
// This can be called in a loop with the same <resume_location> to parse
// all statements from a string.
//
// On successful return,
// <*at_end_of_input> is true if parsing reached the end of the string.
// <*output> contains the next statement found.
//
// Statements are separated by semicolons.  A final semicolon is not required
// on the last statement.  If only whitespace and comments follow the
// semicolon, <*at_end_of_input> will be set.
//
// This can return errors that point at a location in the input. How this
// location is reported is given by <options.error_message_mode()>.
//
// After an error, <resume_location> may not be updated and analyzing further
// statements is not supported.
absl::Status AnalyzeNextStatement(
    ParseResumeLocation* /*absl_nonnull*/ resume_location,
    const AnalyzerOptions& options_in, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output,
    bool* /*absl_nonnull*/ at_end_of_input);

// Same as AnalyzeStatement(), but analyze from the parsed AST contained in a
// ParserOutput instead of raw SQL string. For projects which are allowed to use
// the parser directly, using this may save double parsing. If the
// AnalyzerOptions does not specify arena() or id_string_pool(), this will reuse
// the arenas from the ParserOutput for analysis.
//
// On success, the <*statement_parser_output> is moved to be owned by <*output>.
// On failure, the ownership of <*statement_parser_output> remains unchanged.
// The statement contained within remains valid.
absl::Status AnalyzeStatementFromParserOutputOwnedOnSuccess(
    std::unique_ptr<ParserOutput>* /*absl_nonnull*/ statement_parser_output,
    const AnalyzerOptions& options, absl::string_view sql,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);
// Similar to the previous function, but does *not* change ownership of
// <statement_parser_output>.
absl::Status AnalyzeStatementFromParserOutputUnowned(
    std::unique_ptr<ParserOutput>* /*absl_nonnull*/ statement_parser_output,
    const AnalyzerOptions& options, absl::string_view sql,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// Similar to the previous function, but uses a pre-existing ASTStatement,
// without a ParserOutput.
//
// If 'statement' belongs to a script, 'sql' should represent the entire
// script used to generate the AST to which 'statement' belongs to; any errors
// returned will use line/column numbers relative to the script.
absl::Status AnalyzeStatementFromParserAST(
    const ASTStatement& statement, const AnalyzerOptions& options,
    absl::string_view sql, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// Analyze a GoogleSQL expression.  The expression may include query
// parameters, subqueries, and any other valid expression syntax.
//
// The Catalog provides functions and named data types as usual.  If it
// includes Tables, those tables will be queryable in subqueries inside the
// expression.
//
// Column names added to <options> with AddExpressionColumn will be usable
// in the expression, and will show up as ResolvedExpressionColumn nodes in
// the resolved output.
//
// Can return errors that point at a location in the input. This location can be
// reported in multiple ways depending on <options.error_message_mode()>.
absl::Status AnalyzeExpression(
    absl::string_view sql, const AnalyzerOptions& options,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// Similar to the above, but coerces the expression to <target_type>.
// The conversion is performed using assignment semantics.
// For details, see Coercer::AssignableTo() in
// .../public/coercer.h.  If the conversion is not possible, an
// error is issued, with a location attached corresponding to the start of the
// expression.
//
// If <target_type> is nullptr, behaves the same as AnalyzeExpression().
// TODO: Deprecate this method and use AnalyzerOptions
// target_column_types_ to trigger expression coercion to the target type.
absl::Status AnalyzeExpressionForAssignmentToType(
    absl::string_view sql, const AnalyzerOptions& options,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    const Type* /*absl_nullable*/ target_type,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// TODO: Take a ParserOutput instead of ASTExpression; also make the
// constructor of ParserOutput take an unowned ASTExpression.
// Resolves a standalone AST expression.
absl::Status AnalyzeExpressionFromParserAST(
    const ASTExpression& ast_expression, const AnalyzerOptions& options_in,
    absl::string_view sql, TypeFactory* /*absl_nonnull*/ type_factory,
    Catalog* /*absl_nonnull*/ catalog,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// Similar to the above, but coerces the expression to <target_type>.
// The conversion is performed using assignment semantics.
// For details, see Coercer::AssignableTo() in
// .../public/coercer.h.  If the conversion is not possible, an
// error is issued, with a location attached corresponding to the start of the
// expression.
//
// If <target_type> is nullptr, behaves the same as
// AnalyzeExpressionFromParserAST.
// TODO: Take a ParserOutput instead of ASTExpression (similar to
// AnalyzeExpressionFromParserAST()).
// TODO: Deprecate this method and use AnalyzerOptions
// target_column_types_ to trigger expression coercion to the target type
// (similar to AnalyzeExpressionForAssignmentToType()).
absl::Status AnalyzeExpressionFromParserASTForAssignmentToType(
    const ASTExpression& ast_expression, const AnalyzerOptions& options_in,
    absl::string_view sql, TypeFactory* /*absl_nonnull*/ type_factory,
    Catalog* /*absl_nonnull*/ catalog, const Type* /*absl_nullable*/ target_type,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output);

// Parse and analyze a GoogleSQL type name with optional type parameters.
// The type may reference type names from <catalog>. If type parameters are
// specified in <type_name>, then the parameters will be parsed and analyzed as
// well but are otherwise ignored. To return parameters, use the overload below.
// Returns a type in <output_type> on success.
//
// This can return errors that point at a location in the input. How this
// location is reported is given by <options.error_message_mode()>.
absl::Status AnalyzeType(const std::string& type_name,
                         const AnalyzerOptions& options_in,
                         Catalog* /*absl_nonnull*/ catalog,
                         TypeFactory* /*absl_nonnull*/ type_factory,
                         const Type** /*absl_nonnull*/ output_type);

// Same as above function, but also returns a TypeModifiers object in
// <output_type_modifiers> on success.
absl::Status AnalyzeType(const std::string& type_name,
                         const AnalyzerOptions& options_in,
                         Catalog* /*absl_nonnull*/ catalog,
                         TypeFactory* /*absl_nonnull*/ type_factory,
                         const Type** /*absl_nonnull*/ output_type,
                         TypeModifiers* /*absl_nonnull*/ output_type_modifiers);

// A set of table names found is returned in <*table_names>, where each
// table name is an identifier path stored as a vector<string>.
// The identifiers will be in their as-written case.
// There can be duplicates that differ only in case.
typedef std::set<std::vector<std::string>> TableNamesSet;

// Perform lightweight analysis of a SQL statement and extract the set
// of referenced table names.
//
// This analysis is done without any Catalog, so it has no knowledge of
// which tables or functions actually exist, and knows nothing about data types.
// This just extracts the identifier paths that look syntactically like they
// should be table names.
//
// This will fail on parse errors and on some analysis errors.
// If this passes, it does not mean the query is valid.
// This can return errors that point at a location in the input. How this
// location is reported is given by <options.error_message_mode()>.
absl::Status ExtractTableNamesFromStatement(
    absl::string_view sql, const AnalyzerOptions& options_in,
    TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names = nullptr);

// Same as ExtractTableNamesFromStatement(), but extracts table names from one
// SQL statement from a string. The string may contain multiple statements, so
// this can be called in a loop with the same <resume_location> to parse all
// statements from a string.
//
// On successful return,
// <*at_end_of_input> is true if parsing reached the end of the string.
// <*table_names> contains table names referenced in the next statement.
//
// Statements are separated by semicolons. A final semicolon is not required
// on the last statement. If only whitespace and comments follow the
// semicolon, <*at_end_of_input> will be set.
//
// After an error, <resume_location> may not be updated and analyzing further
// statements is not supported.
absl::Status ExtractTableNamesFromNextStatement(
    ParseResumeLocation* /*absl_nonnull*/ resume_location,
    const AnalyzerOptions& options_in, TableNamesSet* /*absl_nonnull*/ table_names,
    bool* /*absl_nonnull*/ at_end_of_input,
    TableNamesSet* /*absl_nullable*/ tvf_names = nullptr);

// Same as ExtractTableNamesFromStatement(), but extracts table names from the
// parsed AST instead of a raw SQL string. For projects which are allowed to use
// the parser directly, using this may save double parsing.
//
// On successful return,
// <*table_names> contains table names referenced in the AST statement.
absl::Status ExtractTableNamesFromASTStatement(
    const ASTStatement& ast_statement, const AnalyzerOptions& options_in,
    absl::string_view sql, TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names = nullptr);

// Extract the set of referenced table names from a script.
//
// This analysis is done without a Catalog, so it has no knowledge of
// which tables or functions actually exist, and knows nothing about data types.
// This just extracts the identifier paths that look syntactically like they
// should be table names.
//
// This will fail on parse errors.
//
// If this passes, it does not mean the query is valid.
// This can return errors that point at a location in the input. How this
// location is reported is given by <options.error_message_mode()>.
absl::Status ExtractTableNamesFromScript(
    absl::string_view sql, const AnalyzerOptions& options_in,
    TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names = nullptr);

// Same as ExtractTableNamesFromScript(), but extracts table names from
// the parsed AST script. For projects which are allowed to use the parser
// directly, using this may save double parsing.
//
// On successful return,
// <*table_names> contains table names referenced in the AST statement.
absl::Status ExtractTableNamesFromASTScript(
    const ASTScript& ast_script, const AnalyzerOptions& options_in,
    absl::string_view sql, TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names = nullptr);

// Resolved "FOR SYSTEM_TIME AS OF" expression.
struct TableResolutionTimeExpr {
  const ASTExpression* ast_expr;
  // Only id_string_pool, arena, and resolved_expr are populated.
  std::unique_ptr<const AnalyzerOutput> analyzer_output_with_expr;
};

// Resolution time info of a table in a query.
struct TableResolutionTimeInfo {
  // A list of resolved "FOR SYSTEM_TIME AS OF" expressions for the table.
  std::vector<TableResolutionTimeExpr> exprs;
  // True means the table is also referenced without "FOR SYSTEM_TIME AS OF".
  bool has_default_resolution_time = false;
};

// A mapping of identifier paths to resolved "FOR SYSTEM_TIME AS OF"
// expressions. The paths are in their original case, and duplicates
// that differ only in case may show up.
typedef std::map<std::vector<std::string>, TableResolutionTimeInfo>
    TableResolutionTimeInfoMap;

// Similarly to ExtractTableNamesFromStatement, perform lightweight analysis
// of a SQL statement and extract the set of referenced table names.
// In addition, for every referenced table, extracts all resolved temporal
// expressions as specified by "FOR SYSTEM_TIME AS OF" clauses. For table
// references w/o an explicit "FOR SYSTEM_TIME AS OF" temporal expression,
// a placeholder value (nullptr) is returned.
//
// Note that the set of temporal reference expressions for a table may contain
// multiple temporal expressions, either equivalent (semantically or possibly
// even syntactically) or not.
//
// Examples:
//
// [Input]  SELECT 1 FROM KeyValue
// [Output] KeyValue => {
//   exprs = []
//   has_default_resolution_time = true
// }
//
// [Input]  SELECT 1 FROM KeyValue FOR SYSTEM_TIME AS OF CURRENT_TIMESTAMP
// [Output] KeyValue => {
//   exprs = [FunctionCall(GoogleSQL:current_timestamp() -> TIMESTAMP)]
//   has_default_resolution_time = false
// }
//
// [Input]  SELECT 0 FROM KeyValue FOR SYSTEM_TIME AS OF CURRENT_TIMESTAMP
//          GROUP BY (SELECT 1 FROM KeyValue FOR SYSTEM_TIME AS OF
//                        TIMESTAMP_SUB(CURRENT_TIMESTAMP, INTERVAL 1 DAY))
// [Output] KeyValue => {
//   exprs = [
//     FunctionCall(GoogleSQL:current_timestamp() -> TIMESTAMP),
//
//     FunctionCall(GoogleSQL:timestamp_sub(
//         TIMESTAMP,
//         INT64,
//         ENUM<googlesql.functions.DateTimestampPart>) -> TIMESTAMP)
//     +-FunctionCall(GoogleSQL:current_timestamp() -> TIMESTAMP)
//     +-Literal(type=INT64, value=1)
//     +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=DAY)
//   ]
//
//   has_default_resolution_time = false
// }
//
// [Input]  SELECT *
//          FROM KeyValue kv1,
//               KeyValue kv2 FOR SYSTEM_TIME AS OF
//                   TIMESTAMP_ADD(CURRENT_TIMESTAMP, INTERVAL 0 DAY)
// [Output] KeyValue => {
//   exprs = [
//     FunctionCall(GoogleSQL:timestamp_add(
//         TIMESTAMP,
//         INT64,
//         ENUM<googlesql.functions.DateTimestampPart>) -> TIMESTAMP)
//     +-FunctionCall(GoogleSQL:current_timestamp() -> TIMESTAMP)
//     +-Literal(type=INT64, value=1)
//     +-Literal(type=ENUM<googlesql.functions.DateTimestampPart>, value=DAY)
//   ]
//
//   has_default_resolution_time = true
// }
//
// If <catalog> is non-null then <type_factory> must also be non-null, and
// the temporal reference expression will be analyzed with the analyzer
// output stored in <table_resolution_time_info_map>; <catalog> and
// <type_factory> must outlive <*table_resolution_time_info_map>.
//
// TableResolutionTimeExpr.ast_expr in <*table_resolution_time_info_map> will
// point to the elements in <*parser_output>.
//
// This doesn't impose any restriction on the contents of the temporal
// expressions other than that they resolve to timestamp type.
absl::Status ExtractTableResolutionTimeFromStatement(
    absl::string_view sql, const AnalyzerOptions& options_in,
    TypeFactory* /*absl_nullable*/ type_factory, Catalog* /*absl_nullable*/ catalog,
    TableResolutionTimeInfoMap* /*absl_nonnull*/ table_resolution_time_info_map,
    std::unique_ptr<ParserOutput>* /*absl_nonnull*/ parser_output);

// Same as ExtractTableResolutionTimeFromStatement(), but extracts table
// resolution time from the parsed AST instead of a raw SQL string.
// For projects which are allowed to use the parser directly, using this
// may save double parsing.
//
// On successful return,
// <*table_resolution_time_info_map> contains resolution time for tables
// appearing in the AST statement. TableResolutionTimeExpr.ast_expr in
// <*table_resolution_time_info_map> will point to the elements in
// <ast_statement>.
absl::Status ExtractTableResolutionTimeFromASTStatement(
    const ASTStatement& ast_statement, const AnalyzerOptions& options_in,
    absl::string_view sql, TypeFactory* /*absl_nullable*/ type_factory,
    Catalog* /*absl_nullable*/ catalog,
    TableResolutionTimeInfoMap* /*absl_nonnull*/ table_resolution_time_info_map);

// Given an AnalyzerOutput that contains a ResolvedStatement, transforms that
// AST to reflect the semantics defined in (broken link)
// (the AnalyzerOutput's ResolvedStatement is replaced).  This method also
// does some additional validation, for example enforcing that only anonymized
// aggregation functions are used.
// TODO: perform all validation in the initial analyzer call
//
// Resolving an AST containing ANON_* functions with differential privacy
// semantics in the initial analysis pass is complex.  Instead, during
// initial query analysis the resolver returns a 'normal' resolved AST
// containing a ResolvedAnonymizedAggregateScan and ANON_* aggregate
// function calls, similar to other aggregate function calls and scans.
//
// In order to conform to GoogleSQL semantics for ANON_* functions, an engine
// must execute the query in a differentially private way.  To facilitate
// conformance, the engine can pass the originally resolved AST to this
// rewriter, which performs several tasks:
//
//   1) injects per-user aggregation into the query
//   2) adds noisy k-threshold filtering
//   3) adds kappa filtering (see (broken link))
//   4) validates that uid columns from tables are preserved from the related
//      TableScans up to the AnonymizedAggregateScan - including through joins,
//      subqueries, and aggregations
//   5) validates that joins between tables with uid columns include the
//      uid in the join predicate (joins must always be per-user)
//
// The rewritten AST that includes these transformations is one way of
// providing differentially private results, and is relatively easy for
// engines to consume.  Therefore this rewriter relieves engines of much of
// the semantic complexity while allowing engines to leverage this logic
// and conform to GoogleSQL semantics.
//
// Calls to RewriteForAnonymization are not currently idempotent - so you
// should not call RewriteForAnonymization() on AnalyzerOutput from a previous
// RewriteForAnonymization() call since you will not get semantically
// equivalent results.
// TODO: Add a state enum to ResolvedAnonymizedAggregateScan to
// track rewrite status and provide idempotence.
//
// Does not take ownership of <catalog> or <type_factory>.
absl::StatusOr<std::unique_ptr<const AnalyzerOutput>> RewriteForAnonymization(
    const std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    const AnalyzerOptions& analyzer_options, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory);

absl::StatusOr<std::unique_ptr<const AnalyzerOutput>> RewriteForAnonymization(
    const AnalyzerOutput& analyzer_output,
    const AnalyzerOptions& analyzer_options, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory);

// Performs resolved AST rewrites as requested with the enabled rewrites in
// `analyzer_options`.
//
// Note that rewrites enabled in the AnalyzerOptions used for Analyzing are
// already applied, so this should only be explicitly called for an engine that
// wants rewrites to happen after analyzing or which wants to apply more
// rewrites.
//
// The `analyzer_output` does not include the extra runtime in overall time, or
// finalize warnings. We should consider revisiting this if a use-case wants
// those outputs.
//
// *WARNING* On error, `analyzer_output` may be in an inconsistent state with
// some rewrites applied (or even partially applied).
absl::Status RewriteResolvedAst(const AnalyzerOptions& analyzer_options,
                                absl::string_view sql,
                                Catalog* /*absl_nonnull*/ catalog,
                                TypeFactory* /*absl_nonnull*/ type_factory,
                                AnalyzerOutput& analyzer_output);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANALYZER_H_
