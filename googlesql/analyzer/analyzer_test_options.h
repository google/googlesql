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

#ifndef GOOGLESQL_ANALYZER_ANALYZER_TEST_OPTIONS_H_
#define GOOGLESQL_ANALYZER_ANALYZER_TEST_OPTIONS_H_

// This file defines the test_case_options that are supported by
// run_analyzer_test, the set of expected query parameters.
// This is written in a separate file so tests under compliance can
// consume the same tests using appropriate options.

#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/type.h"
#include "absl/status/statusor.h"
#include "file_based_test_driver/test_case_options.h"

namespace googlesql {

class AnalyzerTestCase;

/*
Valid options in the case cases:
  mode
      "statement" (default), "expression", "type" or "measure_expression"
  use_custom_id_sequence
      Use a shared column_id allocator sequence. See
      AnalyzerOptions::column_id_sequence_number.
  expect_error_location
      If true (default), errors must have a location.
  allow_internal_error_todo_bug
      Set to a string in the form "b/123 - Description" in order to allow the
      test case to produce internal errors, and to redact GOOGLESQL_RET_CHECK locations
      in the test output. If value is "" (default), then internal errors will
      cause the test to fail, and GOOGLESQL_RET_CHECK errors will not be redacted in the
      test output. Example:
      [allow_internal_error_todo_bug=b/123 - Pending support for XYZ]
  allow_undeclared_parameters
      If true (default is false) allows parameters whose type has not been
      specified explicitly
  parameter_mode
      Either "named" or "positional"
  positional_parameter_types
      Comma-separated list of type names to use for positional parameters
  test_extract_table_names
      If true (default), test that ExtractTableNames returns names matching the
      actual scans in the resolved query, when possible.
  test_list_select_expressions
      If true (default), test that
      ListSelectColumnExpressionsFromFinalSelectClause returns the same number
      of column expressions matching actual scans in the resolved query, when
      possible.
  show_extracted_table_names
      Show the table names from ExtractTableNames before the query result.
  show_temporal_table_references
      Show temporal table references from ExtractTemporalTableReferences before
      the query result.
  show_resolved_ast
      If true (default), show the resolved AST.
  show_strict_mode
      Also show strict mode resolver output if it differs. By default, we run
      strict mode but don't show output.
  parse_location_record_type
      What parse location to record in Resolved AST nodes.
  do_not_show_replaced_literals
      The test framework assumes that if parse_location is recorded, then it
      should test the literal replacer. For tests that need parse locations but
      not literal replacement, this flag suppresses some noisy golden file
      output.
  parse_multiple
      If true, use AnalyzeNextStatement to analyze a sequence of statements from
      the same string.
  default_timezone
      Default timezone to use for analysis.
  run_sqlbuilder
      If true (default), runs the SQLBuilder on the resolved AST. Displays a
      warning message if the SQLBuilder output is not valid (produces an error
      during re-analysis).
  sqlbuilder_positional_parameter_mode
      If "question_mark" (default), SQLBuilder outputs positional parameters as
      the ? character. If "named", SQLBuilder outputs positional parameters as
      @param1, @param2, etc. in accordance with their positions. Has no effect
      if run_sqlbuilder is false.
  show_sqlbuilder_output
      If true, shows the SQLBuilder output of the resolved AST. Has no effect if
      run_sqlbuilder is false.
  show_sqlbuilder_resolved_ast_diff
      If true, shows both the original resolved AST and the resolved AST
      produced by re-analyzing the output of SQLBuilder, if they are different.
      Has no effect if run_sqlbuilder is false.
  sqlbuilder_target_syntax_map_mode
      The value is one of the enums from SQLBuildTargetSyntax.  This target
      syntax override will be applied to relevant resolved nodes in the
      TargetSyntaxMap while testing the SQLBuilder.
  language_features
      Comma-separated list of LanguageFeature enum names, indicating enabled
      features, without the FEATURE_ prefix.
  enabled_ast_rewrites
      Specify a list of rewriters to run against this test statement. Each list
      should start with one of the following:
          - NONE: starts from an empty set of rewrites
          - ALL: starts from all known rewrites
          - DEFAULTS: starts from AnalyzerOptions::DefaultRewrites
      Following the initial set keyword, a comma separated list of rewriter
      names is accepted that can either be additions or subtractions from the
      initial set. The writer names are the names from the ResolvedASTRewrite
      enum without the REWRITE_ prefix. Addition cases are prefixed by + and
      subtraction cases are prefixed by -. If the final set of rewrites is
      empty, or if 'enabled_ast_rewrites' is not set, then no ResolvedAST
      rewrite is printed in the test file.
  in_scope_expression_column_name
      With mode=expression, add an in_scope_expression column with this name in
      addition to the default set of named expression columns.
  in_scope_expression_column_type
      With mode=expression and in_scope_expression_column_name, use this type
      for an in_scope_expression_column in addition to the default set of named
      expression columns.
  allow_aggregate_standalone_expression
      If true, allow aggregate standalone expression in the expression mode.
  product_mode
      "external" or "internal". Maps to ProductMode analyzer option.
  statement_context
      "default" or "module". Maps to StatementContext analyzer option.
  default_table_for_subpipeline_stmt
      A table name from the catalog to use as the subpipeline input table
  use_hints_allowlist
      If true, fill in AllowedHintsAndOptions in AnalyzerOptions with a
        set of allowed hint/option names. (False by default)
  use_database
      Must be one of:
          - "SampleCatalog" [default]: The standard catalog, which includes
            builtin functions, as well as many additional catalog objects.
            Modified by 'language_features' and 'product_mode'. Defined in
            https://github.com/google/googlesql/blob/master/googlesql/testdata/sample_catalog.cc
          - "SpecialCatalog": A hardcoded catalog for testing differential
            privacy. Defined in
            https://github.com/google/googlesql/blob/master/googlesql/testdata/special_catalog.h
          - "TpchCatalog": The TPCH catalog, with semantic graph columns, from
            https://github.com/google/googlesql/blob/master/googlesql/examples/tpch/catalog/tpch_catalog.h
          - <database_name>: The name of a catalog previously created with
            [prepare_database].
//
//   table_for_measure_expr_analysis
//       - A table name from the catalog to use when analyzing measure
//         expressions. Used only when mode is "measure_expression".
  prepare_database
      This allows a DDL statement to be evaluated and added to the named
      database. That named database can then be referenced in a subsequent
      'use_database' option. Example:
          ==
          [prepare_database=database1]
          CREATE FUNCTION Add2(x int64) as (x + 2);
          --
          <Resulting Resolved AST>
          ==
          [use_database=database1]
          select Add2(5);
          --
          <resulting resolved ast>
          ==
      'use_database' may be used in combination with prepare_database in a
      single statement to change the 'base' catalog used in the new database.
      As stated above, the base database may be modified by language options.
      Example:
          ==
          [prepare_database=database2]
          [use_database=database1]
          [language_features]
          CREATE FUNCTION Add4(x int64) as Add2(Add2(x));
          --
          <Resulting Resolved AST>
          ==
          [use_database=database2]
          select Add2(5), Add4(7);
          --
          <resulting resolved ast>
          ==
      Multiple statements may use the same prepare_database to add multiple
      objects to the same catalog. However, such statements must be sequentially
      adjacent, and only the first may be contain a 'use_database' option.

      The following statements are currently supported:
        CREATE [AGGREGATE] FUNCTION
        CREATE TABLE FUNCTION
        CREATE TABLE
  error_message_mode
      "with_payload", "one_line", or "multi_line_with_caret". Maps to
      ErrorMessageMode analyzer option.
  ddl_pseudo_column_mode
      Either "callback" or "list". Only "list" pseudo-columns are available in
      Java-based analyzer tests. "list" is the default.
  rewrite_options
      A text proto string for RewriteOptions proto message, the default value is
      an empty RewriteOptions string. The parsed RewriteOptions is used for
      googlesql resolved ast rewriters.
  kShowReferencedPropertyGraphs
      If true (default is false) show the PropertyGraphs referenced in the
      original query before pruning or rewrite occur.
*/
extern const char* const kAllowInternalErrorTodoBug;
extern const char* const kAllowUndeclaredParameters;
extern const char* const kDefaultAnonKappaValue;
extern const char* const kDefaultTimezone;
extern const char* const kExpectErrorLocation;
extern const char* const kInScopeExpressionColumnName;
extern const char* const kInScopeExpressionColumnType;
extern const char* const kAllowAggregateStandaloneExpression;
extern const char* const kCoercedQueryOutputTypes;
extern const char* const kModeOption;
extern const char* const kParameterMode;
extern const char* const kParseMultiple;
extern const char* const kPositionalParameters;
extern const char* const kProductMode;
extern const char* const kParseLocationRecordType;
extern const char* const kDoNotShowReplacedLiterals;
extern const char* const kCreateNewColumnForEachProjectedOutput;
extern const char* const kRunInJava;
extern const char* const kRunSqlBuilder;
extern const char* const kShowExtractedTableNames;
extern const char* const kShowTableResolutionTime;
extern const char* const kShowResolvedAST;
extern const char* const kShowStrictMode;
extern const char* const kShowSqlBuilderOutput;
extern const char* const kShowSqlBuilderResolvedASTDiff;
extern const char* const kSqlBuilderTargetSyntaxMapMode;
extern const char* const kStatementContext;
extern const char* const kDefaultTableForSubpipelineStmt;
extern const char* const kSupportedStatementKinds;
extern const char* const kTestExtractTableNames;
extern const char* const kTestListSelectExpressions;
extern const char* const kSqlBuilderPositionalParameterMode;
extern const char* const kUseDatabase;
extern const char* const kPrepareDatabase;
extern const char* const kRunDeserializer;
extern const char* const kUseHintsAllowlist;
extern const char* const kUseSharedIdSequence;
extern const char* const kEnableLiteralReplacement;
extern const char* const kErrorMessageMode;
extern const char* const kDdlPseudoColumnMode;
extern const char* const kPreserveColumnAliases;
extern const char* const kSupportedGenericEntityTypes;
extern const char* const kSupportedGenericSubEntityTypes;
extern const char* const kEnabledASTRewrites;
extern const char* const kCreateTableLikeNotScanned;
extern const char* const kPrivilegeRestrictionTableNotScanned;
extern const char* const kPreserveUnnecessaryCast;
extern const char* const kEnableSampleAnnotation;
extern const char* const kAdditionalAllowedAnonymizationOptions;
extern const char* const kSuppressFunctions;
extern const char* const kOptionNamesToIgnoreInLiteralReplacement;
extern const char* const kScrubLimitOffsetInLiteralReplacement;
extern const char* const kReplaceTableNotFoundErrorWithTvfErrorIfApplicable;
extern const char* const kIdStringAllowUnicodeCharacters;
extern const char* const kDisallowDuplicateOptions;
extern const char* const kRewriteOptions;
extern const char* const kShowReferencedPropertyGraphs;
extern const char* const kPruneUnusedColumns;
extern const char* const kEnhancedErrorRedaction;
extern const char* const kSqlBuilderTargetSyntaxMode;
extern const char* const kSqlBuilderTargetSyntaxModePipe;
extern const char* const kSqlBuilderTargetSyntaxModeStandard;
extern const char* const kSqlBuilderTargetSyntaxModeBoth;
extern const char* const kUseConstantEvaluator;
extern const char* const kTableForMeasureExprAnalysis;
extern const char* const kLogImpactOfLateralColumnReferences;

// set_flag
// Causes a command line flag to be set to a particular value during the run
// of a given analyzer test. Each flag is set as though by
// SetCommandLineOption(key, value);
//
// Format:
// [set_flag=flag1=value1,flag2=value2,...]
extern const char* const kSetFlag;

void RegisterAnalyzerTestOptions(
    file_based_test_driver::TestCaseOptions* test_case_options);

// Return a set of known parameters used in the analyzer tests.
std::vector<std::pair<std::string, const googlesql::Type*>> GetQueryParameters(
    TypeFactory* type_factory);

// A map-like type where keys are a canonicalized version of the string that
// apperas in the kEnabledASTRewewrites and ASTRewriteSet is the set of rewrites
// implied by that string. We use a vector to preserve insertion order. A
// linked_hash_map would be ideal, but the absl container libraries lack that
// particular data structure.
using AnalyzerTestRewriteGroups =
    std::vector<std::pair<std::string, AnalyzerOptions::ASTRewriteSet>>;

// Returns a list of rewrite rule sets to enable accoring to the provided
// 'test_case_options'. The result of applying each set is printed in the test
// file. If 'enabled_ast_rewrites' is not set or set to empty string, the
// returned vector is empty. A 'NONE' group, or any sets of rules specified by
// 'test_case_options' that are empty, is excluded from the returned groups.
// Thus [enabled_ast_rewrites=NONE] is equivalent to not setting the option.
// [enabled_ast_rewrites=NONE|ALL] is equivalent to [enabled_ast_rewrites=ALL].
// The NONE group is most useful when modified as in "NONE,+ANONYMIZATION". All
// errors are returned as StatusCode::kInternal because this this a testonly
// function.
absl::StatusOr<AnalyzerTestRewriteGroups> GetEnabledRewrites(
    const file_based_test_driver::TestCaseOptions& test_case_options);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_ANALYZER_TEST_OPTIONS_H_
