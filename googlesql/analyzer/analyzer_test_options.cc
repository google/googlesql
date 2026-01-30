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

#include "googlesql/analyzer/analyzer_test_options.h"

#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/options_utils.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/testing/test_case_options_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "file_based_test_driver/test_case_options.h"
#include "google/protobuf/text_format.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

const char* const kModeOption = "mode";
const char* const kUseSharedIdSequence = "use_shared_column_id_sequence_number";
const char* const kExpectErrorLocation = "expect_error_location";
const char* const kAllowInternalErrorTodoBug = "allow_internal_error_todo_bug";
const char* const kAllowUndeclaredParameters = "allow_undeclared_parameters";
const char* const kParameterMode = "parameter_mode";
const char* const kPositionalParameters = "positional_parameter_types";
const char* const kTestExtractTableNames = "test_extract_table_names";
const char* const kTestListSelectExpressions = "test_list_select_expressions";
const char* const kShowExtractedTableNames = "show_extracted_table_names";
const char* const kShowTableResolutionTime = "show_table_resolution_time";
const char* const kShowResolvedAST = "show_resolved_ast";
const char* const kShowStrictMode = "show_strict_mode";
const char* const kParseLocationRecordType = "parse_location_record_type";
const char* const kDoNotShowReplacedLiterals = "do_not_show_replaced_literals";
const char* const kCreateNewColumnForEachProjectedOutput =
    "create_new_column_for_each_projected_output";
const char* const kParseMultiple = "parse_multiple";
const char* const kDefaultTimezone = "default_timezone";
const char* const kDefaultAnonKappaValue = "default_anon_kappa_value";
const char* const kRunSqlBuilder = "run_sqlbuilder";
const char* const kSqlBuilderPositionalParameterMode =
    "sqlbuilder_positional_parameter_mode";
const char* const kShowSqlBuilderOutput = "show_sqlbuilder_output";
const char* const kShowSqlBuilderResolvedASTDiff =
    "show_sqlbuilder_resolved_ast_diff";
const char* const kSqlBuilderTargetSyntaxMapMode =
    "sqlbuilder_target_syntax_map_mode";
const char* const kInScopeExpressionColumnName =
    "in_scope_expression_column_name";
const char* const kInScopeExpressionColumnType =
    "in_scope_expression_column_type";
const char* const kAllowAggregateStandaloneExpression =
    "allow_aggregate_standalone_expression";
const char* const kCoercedQueryOutputTypes = "coerced_query_output_types";
const char* const kStatementContext = "statement_context";
const char* const kDefaultTableForSubpipelineStmt =
    "default_table_for_subpipeline_stmt";
const char* const kProductMode = "product_mode";
const char* const kUseHintsAllowlist = "use_hints_allowlist";
const char* const kRunInJava = "java";
const char* const kSupportedStatementKinds = "supported_statement_kinds";
const char* const kUseDatabase = "use_database";
const char* const kPrepareDatabase = "prepare_database";
const char* const kRunDeserializer = "run_deserializer";
const char* const kEnableLiteralReplacement = "enable_literal_replacement";
const char* const kErrorMessageMode = "error_message_mode";
const char* const kDdlPseudoColumnMode = "ddl_pseudo_column_mode";
const char* const kPreserveColumnAliases = "preserve_column_aliases";
const char* const kSupportedGenericEntityTypes =
    "supported_generic_entity_types";
const char* const kSupportedGenericSubEntityTypes =
    "supported_generic_sub_entity_types";
const char* const kEnabledASTRewrites = "enabled_ast_rewrites";
const char* const kCreateTableLikeNotScanned = "create_table_like_not_scanned";
const char* const kPrivilegeRestrictionTableNotScanned =
    "privilege_restriction_table_not_scanned";
const char* const kPreserveUnnecessaryCast = "preserve_unnecessary_cast";
const char* const kEnableSampleAnnotation = "enable_sample_annotation";
const char* const kAdditionalAllowedAnonymizationOptions =
    "additional_allowed_anonymization_options";
const char* const kSuppressFunctions = "suppress_functions";
const char* const kOptionNamesToIgnoreInLiteralReplacement =
    "option_names_to_ignore_in_literal_replacement";
const char* const kScrubLimitOffsetInLiteralReplacement =
    "scrub_limit_offset_in_literal_replacement";
const char* const kSetFlag = "set_flag";
const char* const kReplaceTableNotFoundErrorWithTvfErrorIfApplicable =
    "replace_table_not_found_error_with_tvf_error_if_applicable";
const char* const kIdStringAllowUnicodeCharacters =
    "googlesql_idstring_allow_unicode_characters";
const char* const kDisallowDuplicateOptions = "disallow_duplicate_options";
const char* const kRewriteOptions = "rewrite_options";
const char* const kShowReferencedPropertyGraphs =
    "show_referenced_property_graphs";
const char* const kPruneUnusedColumns = "prune_unused_columns";
const char* const kEnhancedErrorRedaction = "enhanced_error_redaction";
const char* const kSqlBuilderTargetSyntaxMode =
    "sql_builder_target_syntax_mode";
const char* const kSqlBuilderTargetSyntaxModePipe = "pipe";
const char* const kSqlBuilderTargetSyntaxModeStandard = "standard";
const char* const kSqlBuilderTargetSyntaxModeBoth = "both";
const char* const kUseConstantEvaluator = "use_constant_evaluator";
const char* const kTableForMeasureExprAnalysis =
    "table_for_measure_expr_analysis";
const char* const kLogImpactOfLateralColumnReferences =
    "log_impact_of_lateral_column_references";

void RegisterAnalyzerTestOptions(
    file_based_test_driver::TestCaseOptions* test_case_options) {
  std::string rewrite_options_string;
  google::protobuf::TextFormat::PrintToString(RewriteOptions::default_instance(),
                                    &rewrite_options_string);
  test_case_options->RegisterString(kModeOption, "statement");
  test_case_options->RegisterBool(kUseSharedIdSequence, false);
  test_case_options->RegisterBool(kExpectErrorLocation, true);
  test_case_options->RegisterString(kAllowInternalErrorTodoBug, "");
  test_case_options->RegisterBool(kAllowUndeclaredParameters, false);
  test_case_options->RegisterString(kParameterMode, "named");
  test_case_options->RegisterString(kPositionalParameters, "");
  test_case_options->RegisterBool(kTestExtractTableNames, true);
  test_case_options->RegisterBool(kTestListSelectExpressions, true);
  test_case_options->RegisterBool(kShowExtractedTableNames, false);
  test_case_options->RegisterBool(kShowTableResolutionTime, false);
  test_case_options->RegisterBool(kShowResolvedAST, true);
  test_case_options->RegisterBool(kShowStrictMode, false);
  test_case_options->RegisterString(kParseLocationRecordType, "");
  test_case_options->RegisterBool(kDoNotShowReplacedLiterals, false);
  test_case_options->RegisterBool(kCreateNewColumnForEachProjectedOutput,
                                  false);
  test_case_options->RegisterBool(kEnableSampleAnnotation, false);
  test_case_options->RegisterBool(kParseMultiple, false);
  test_case_options->RegisterString(kDefaultTimezone, "");
  test_case_options->RegisterInt64(kDefaultAnonKappaValue, 0);
  test_case_options->RegisterBool(kRunSqlBuilder, true);
  test_case_options->RegisterString(kSqlBuilderPositionalParameterMode,
                                    "question_mark");
  test_case_options->RegisterBool(kShowSqlBuilderOutput, false);
  test_case_options->RegisterBool(kShowSqlBuilderResolvedASTDiff, false);
  test_case_options->RegisterString(kSqlBuilderTargetSyntaxMapMode, "");
  test_case_options->RegisterString(kLanguageFeatures, "MAXIMUM");
  test_case_options->RegisterString(kInScopeExpressionColumnName, "");
  test_case_options->RegisterString(kInScopeExpressionColumnType,
                                    "`googlesql_test.KitchenSinkPB`");
  test_case_options->RegisterString(kCoercedQueryOutputTypes, "");
  test_case_options->RegisterString(kStatementContext, "");
  test_case_options->RegisterString(kDefaultTableForSubpipelineStmt, "");
  test_case_options->RegisterString(kProductMode, "");
  test_case_options->RegisterBool(kUseHintsAllowlist, false);
  test_case_options->RegisterBool(kRunInJava, true);
  test_case_options->RegisterString(kSupportedStatementKinds, "");
  test_case_options->RegisterString(kUseDatabase, "SampleCatalog");
  test_case_options->RegisterString(kPrepareDatabase, "");
  test_case_options->RegisterBool(kRunDeserializer, true);
  test_case_options->RegisterBool(kEnableLiteralReplacement, true);
  test_case_options->RegisterString(kErrorMessageMode, "");
  test_case_options->RegisterString(kDdlPseudoColumnMode, "");
  test_case_options->RegisterBool(kPreserveColumnAliases, true);
  test_case_options->RegisterString(kSupportedGenericEntityTypes, "");
  test_case_options->RegisterString(kSupportedGenericSubEntityTypes, "");
  test_case_options->RegisterString(kEnabledASTRewrites, "");
  test_case_options->RegisterBool(kCreateTableLikeNotScanned, false);
  test_case_options->RegisterBool(kPrivilegeRestrictionTableNotScanned, false);
  test_case_options->RegisterBool(kPreserveUnnecessaryCast, false);
  test_case_options->RegisterString(kAdditionalAllowedAnonymizationOptions, "");
  test_case_options->RegisterString(kSuppressFunctions, "");
  test_case_options->RegisterString(kOptionNamesToIgnoreInLiteralReplacement,
                                    "");
  test_case_options->RegisterBool(kScrubLimitOffsetInLiteralReplacement, true);
  test_case_options->RegisterString(kSetFlag, "");
  test_case_options->RegisterBool(
      kReplaceTableNotFoundErrorWithTvfErrorIfApplicable, true);
  test_case_options->RegisterBool(kIdStringAllowUnicodeCharacters, false);
  test_case_options->RegisterBool(kDisallowDuplicateOptions, false);
  test_case_options->RegisterString(kRewriteOptions, rewrite_options_string);
  test_case_options->RegisterBool(kShowReferencedPropertyGraphs, false);
  // Prune unused columns by default.  We intend to make this the default
  // once all engines are updated.
  test_case_options->RegisterBool(kPruneUnusedColumns, true);
  test_case_options->RegisterBool(kAllowAggregateStandaloneExpression, false);
  test_case_options->RegisterBool(kEnhancedErrorRedaction, false);
  test_case_options->RegisterString(kSqlBuilderTargetSyntaxMode,
                                    kSqlBuilderTargetSyntaxModeBoth);
  test_case_options->RegisterBool(kUseConstantEvaluator, false);
  test_case_options->RegisterString(kTableForMeasureExprAnalysis, "");

  // For analyzer tests, we turn this on to get the maximum coverage for the
  // blast radius of lateral column references.
  test_case_options->RegisterBool(kLogImpactOfLateralColumnReferences, true);
}

std::vector<std::pair<std::string, const googlesql::Type*>> GetQueryParameters(
    TypeFactory* type_factory) {
  const googlesql::Type* array_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(type_factory->get_int32(), &array_type));

  const googlesql::Type* array_int64_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(type_factory->get_int64(),
                                       &array_int64_type));

  const googlesql::Type* array_double_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(type_factory->get_double(),
                                       &array_double_type));

  const googlesql::Type* array_string_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(type_factory->get_string(),
                                       &array_string_type));

  const googlesql::Type* struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"a", type_factory->get_int32()}, {"b", type_factory->get_string()}},
      &struct_type));

  const googlesql::Type* empty_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({}, &empty_struct_type));

  const googlesql::Type* struct_two_int64_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"", type_factory->get_int64()}, {"", type_factory->get_int64()}},
      &struct_two_int64_type));

  const googlesql::Type* proto_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  const googlesql::Type* approx_distance_function_options_proto_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::TestApproxDistanceFunctionOptionsProto::descriptor(),
      &approx_distance_function_options_proto_type));

  const googlesql::Type* enum_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                      &enum_type));

  const googlesql::Type* array_enum_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(enum_type, &array_enum_type));

  const googlesql::Type* struct_int64_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"", type_factory->get_int64()}},
                                        &struct_int64_type));

  const googlesql::Type* array_struct_int64_type;
  GOOGLESQL_CHECK_OK(
      type_factory->MakeArrayType(struct_int64_type, &array_struct_int64_type));

  return std::vector<std::pair<std::string, const googlesql::Type*>>{
      {"test_param_bool", type_factory->get_bool()},
      {"test_param_int32", type_factory->get_int32()},
      {"test_param_int64", type_factory->get_int64()},
      {"test_param_uint32", type_factory->get_uint32()},
      {"test_param_uint64", type_factory->get_uint64()},
      {"test_param_float", type_factory->get_float()},
      {"test_param_double", type_factory->get_double()},
      {"test_param_numeric", type_factory->get_numeric()},
      {"test_param_bignumeric", type_factory->get_bignumeric()},
      {"test_param_bytes", type_factory->get_bytes()},
      {"test_param_string", type_factory->get_string()},
      {"test_param_timestamp", type_factory->get_timestamp()},
      {"test_param_MixEdCaSe", type_factory->get_string()},
      {"test_param_proto", proto_type},
      {"test_param_approx_distance_function_options_proto",
       approx_distance_function_options_proto_type},
      {"test_param_struct", struct_type},
      {"test_param_empty_struct", empty_struct_type},
      {"test_param_struct_two_int64", struct_two_int64_type},
      {"test_param_json", type_factory->get_json()},
      {"test_param_enum", enum_type},
      {"test_param_array", array_type},
      {"test_param_array_int64", array_int64_type},
      {"test_param_array_double", array_double_type},
      {"test_param_array_string", array_string_type},
      {"test_param_array_enum", array_enum_type},
      {"test_param_array_struct_int64", array_struct_int64_type},
      // Parameter names that are reserved keywords are supported.
      {"select", type_factory->get_bool()},
      {"proto", proto_type},
      // used in parse_locations.test
      {"_p3_StrinG", type_factory->get_string()},
      {"_P4_string", type_factory->get_string()},
  };
}

absl::StatusOr<AnalyzerTestRewriteGroups> GetEnabledRewrites(
    const file_based_test_driver::TestCaseOptions& test_case_options) {
  AnalyzerTestRewriteGroups rewrite_groups;
  absl::flat_hash_set<std::string> seen_rewrite_group_keys;
  const std::string& raw_rewrites =
      test_case_options.GetString(kEnabledASTRewrites);
  if (raw_rewrites.empty()) {
    return rewrite_groups;
  }
  for (absl::string_view raw_group_view : absl::StrSplit(raw_rewrites, '|')) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        internal::EnumOptionsEntry<ResolvedASTRewrite> option_entry,
        internal::ParseEnabledAstRewrites(raw_group_view));
    if (!option_entry.options.empty()) {
      GOOGLESQL_RET_CHECK(seen_rewrite_group_keys.insert(option_entry.description).second)
          << "Multiple rewrite groups canonicalize to: "
          << option_entry.description;
      rewrite_groups.push_back(
          {option_entry.description, option_entry.options});
    }
  }

  return rewrite_groups;
}

}  // namespace googlesql
