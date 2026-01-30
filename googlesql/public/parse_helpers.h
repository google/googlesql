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

#ifndef GOOGLESQL_PUBLIC_PARSE_HELPERS_H_
#define GOOGLESQL_PUBLIC_PARSE_HELPERS_H_

#include <string>
#include <vector>

#include "googlesql/parser/ast_node_kind.h"  
#include "googlesql/parser/parser.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class ParseResumeLocation;

// Parses the <sql> statement and returns OK if the statement is valid
// GoogleSQL syntax.  Otherwise returns the parser error, formatted according
// to <error_message_mode>.  Scripting statements are not supported.
absl::Status IsValidStatementSyntax(
    absl::string_view sql, ErrorMessageMode error_message_mode,
    const LanguageOptions& language_options = LanguageOptions());

// Similar to the previous, but checks the validity of the next statement
// starting from <resume_location>.  If the syntax is valid then returns OK
// and sets <at_end_of_input> and updates <resume_location> to indicate the
// start of the next statement (if not at end of input).
//
// This method can be invoked iteratively on a multi-statement string to
// validate that all the statements in the string are valid syntax.
absl::Status IsValidNextStatementSyntax(
    ParseResumeLocation* resume_location, ErrorMessageMode error_message_mode,
    bool* at_end_of_input,
    const LanguageOptions& language_options = LanguageOptions());

// Parse the first few keywords from <input> (ignoring whitespace, comments
// and hints) to determine what kind of statement it is (if it is valid).
// Scripting statements are not supported.
//
// Returns a ResolvedNodeKind enum for one of the subclasses of
// ResolvedStatement.  e.g. RESOLVED_QUERY_STMT or RESOLVED_EXPLAIN_STMT.
//
// For a subpipeline (starting with "|>"), returns RESOLVED_SUBPIPELINE_STMT.
//
// If <input> cannot be any known statement type, returns RESOLVED_LITERAL
// (a non-statement) as a sentinel.
ResolvedNodeKind GetStatementKind(
    absl::string_view sql,
    const LanguageOptions& language_options = LanguageOptions());

// Converts ASTNodeKind to ResolvedNodeKind.
ResolvedNodeKind GetStatementKind(ASTNodeKind node_kind);

// Same as GetStatementKind, but determines the statement kind for the next
// statement starting from <resume_location>.
//
// TODO: Make LanguageOptions non-optional, since they control some
// parser behaviors.
ResolvedNodeKind GetNextStatementKind(
    const ParseResumeLocation& resume_location,
    const LanguageOptions& language_options = LanguageOptions());

// Properties of a statement, currently including the statement kind, the
// statement category (DDL, DML, etc.), statement level hints, and whether
// the statement creates a TEMP object.
//
// Note that script statements are not currently supported.
struct StatementProperties {
  enum StatementCategory {
    // Statement category unknown, for cases where we cannot determine the
    // statement type (e.g., invalid statement syntax)
    UNKNOWN = 0,  // Statement category unknown
    SELECT = 1,   // SELECT (including WITH)
    DML = 2,      // INSERT, UPDATE, DELETE, MERGE
    DDL = 3,      // CREATE (including CTAS), DROP, ALTER
    // The OTHER category currently contains a wide range of statement
    // kinds that do not clearly fit into the categories above, and may
    // be broken down into more categories in the future.
    OTHER = 4
  };

  // There is no UNKNOWN or similar ResolvedNodeKind, so default to
  // RESOLVED_LITERAL (which is also what GetStatementKind() defaults to for
  // unrecognized statement kinds).
  ResolvedNodeKind node_kind = RESOLVED_LITERAL;
  StatementCategory statement_category = StatementCategory::UNKNOWN;

  // Only applies if <statement_type> is DDL (and the statement kind is
  // RESOLVED_CREATE_*).
  bool is_create_temporary_object = false;

  // Statement level hints, mapping from hint name to hint expression.
  // The mapping is on the actual SQL text indicated by the ParseLocation
  // of the left and right sides of the hint assignment.  For example, for
  // the following statement level hint:
  //
  //   @{ a.b = concat("a", "b")} SELECT...
  //
  // The mapping is from 'a.b' -> 'concat("a", "b")'.
  absl::flat_hash_map<std::string, std::string> statement_level_hints;
};

// Returns the statement properties, including ResolvedNodeKind, statement
// type (SELECT, DDL, DML, etc.), and others.  Currently fetches properties
// that do not require parsing the entire statement.  Can be extended to
// return additional properties over time, as needed.
//
// <language_options> impacts parsing behavior.
//
// Returns OK for invalid syntax, with an UNKNOWN statement node kind.  Only
// returns internal errors.
absl::Status GetStatementProperties(absl::string_view input,
                                    const LanguageOptions& language_options,
                                    StatementProperties* statement_properties);

// Same as GetStatementProperties, but determines the statement properties for
// the next statement starting from <resume_location>.
absl::Status GetNextStatementProperties(
    const ParseResumeLocation& resume_location,
    const LanguageOptions& language_options,
    StatementProperties* statement_properties);

// Skips a statement without parsing it, even if it's invalid.
//
// Statements are separated by semicolons.  A final semicolon is not required
// to end the last statement.  If only whitespace and comments follow a
// statement, `*at_end_of_input` will be set to true and the byte offset will
// point to the end of the input.  Otherwise, `*at_end_of_input` will be set
// to false and the byte offset will point to the character after the semicolon.
//
// Note: This function identifies statement boundaries primarily by semicolons.
// Procedural statements (e.g., BEGIN...END blocks) can internally contain
// multiple statements, each terminated by a semicolon. Using SkipNextStatement
// on a string containing such a procedural statement may result in the
// skipping logic concluding prematurely at the first encountered semicolon
// within the block, rather than at the end of the entire procedural statement.
// Therefore, it is not recommended to use this function within procedural
// statements, since the skip result might not be a real statement boundary.
absl::Status SkipNextStatement(ParseResumeLocation* resume_location,
                               bool* at_end_of_input);

// Returns the target table name of a DDL statement as an identifier path.
//
// `language_options` impacts parsing behavior. Returns an error if the
// statement does not parse or is not a supported DDL statement kind.
//
// Currently the only supported DDL statement kind is CREATE TABLE.
absl::StatusOr<std::vector<std::string> > GetTopLevelTableNameFromDDLStatement(
    absl::string_view sql, const LanguageOptions& language_options);

// Same as GetTopLevelTableNameFromDDLStatement, but determines the top level
// table name for the next statement starting from `resume_location`.
// If successful, updates both `at_end_of_input` and `resume_location`.
absl::StatusOr<std::vector<std::string> >
GetTopLevelTableNameFromNextDDLStatement(
    absl::string_view sql, ParseResumeLocation& resume_location,
    bool* at_end_of_input, const LanguageOptions& language_options);

// Returns a list substrings from the sql input containing the expressions
// defining the output columns from the outermost SELECT clause. Expressions are
// returned in the SELECT-list order so that indexes in the output vector
// corresponds to the index of the output columns. This function provides a
// best-effort extraction and has significant limitations. The output should be
// treated as unsuitable for any semantic interpretation.
//
// WARNING: The returned expressions are purely syntactic and do not carry
// semantic information. This makes the output unsuitable for interpreting
// expressions that contain column names or aliases, as resolving these names
// requires the context of the entire query (e.g., FROM, WITH, and JOIN
// clauses), which this function does not provide. For example:
//   - In `SELECT t.a as col FROM t`, the extracted expression is `t.a`, but its
//     meaning depends on the definition of `t`.
//   - An expression might not correspond to a column from a table, even if its
//     name matches. The expression could be a literal, a function call, or a
//     complex expression.
//
// The function may also fail to parse or return an error for query
// constructs where there is not a simple, outermost SELECT list. For example :
//   - Queries with '*' operations (e.g., `SELECT *`, `SELECT t.*`) are not
//     supported and will return an error.
//   - Queries with set operations (e.g., `UNION`, `INTERSECT`, `EXCEPT`) are
//     not supported and will return an error.
//   - For queries with PIPE operators, the expressions are returned only if the
//     query ends with a PIPE_SELECT statement. Otherwise, an error is
//     returned.
//
// `language_options` impacts parsing behavior.
absl::StatusOr<std::vector<absl::string_view> >
ListSelectColumnExpressionsFromFinalSelectClause(
    absl::string_view sql, const LanguageOptions& language_options);

#ifndef SWIG
// Returns function names referenced in the given SQL statement.
// Performs a lightweight and best-effort extraction of things that look like
// function names from the given SQL statement. There may be some
// false-negatives or false-positives due to details which cannot be determined
// without fully analyzing the query; if correctness is a requirement, then code
// should build a Catalog and call the Analyzer APIs instead. This function will
// filter out some false-positives due to language features that look like
// functions in the syntax (e.g. `FLATTEN(...)`), but which are actually not
// functions.
// The returned set contains the function name parts as lower case in a vector,
// e.g. {{"foo"}, {"bar", "baz"}, {"z", "fn", "qux"}}.
absl::StatusOr<absl::flat_hash_set<std::vector<std::string>>>
ListReferencedFunctionsInStatement(absl::string_view sql,
                                   const LanguageOptions& language_options);
#endif  // SWIG

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_PARSE_HELPERS_H_
