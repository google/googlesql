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

#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/proto/anon_output_with_report.pb.h"
#include "googlesql/public/anon_function.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static std::string DPCountStarSQL(const std::vector<std::string>& inputs) {
  if (inputs.empty()) {
    return "COUNT(*)";
  }
  return absl::StrCat("COUNT(*, ", absl::StrJoin(inputs, ", "), ")");
}

static std::string SupportedSignaturesForDPCountStar(
    const LanguageOptions& language_options, const Function& function) {
  if (!language_options.LanguageFeatureEnabled(FEATURE_DIFFERENTIAL_PRIVACY)) {
    return "";
  }
  if (!language_options.LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS)) {
    return "COUNT(* [, contribution_bounds_per_group => STRUCT<INT64, "
           "INT64>])";
  }
  return "COUNT(* [, contribution_bounds_per_group => STRUCT<INT64, INT64>] "
         "[, report_format => DIFFERENTIAL_PRIVACY_REPORT_FORMAT])";
}

void GetAnonFunctions(TypeFactory* type_factory,
                      const GoogleSQLBuiltinFunctionOptions& options,
                      NameToFunctionMap* functions) {
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* double_type = type_factory->get_double();
  const Type* numeric_type = type_factory->get_numeric();
  const Type* double_array_type = types::DoubleArrayType();
  const Type* json_type = types::JsonType();
  const Type* anon_output_with_report_proto_type = nullptr;
  GOOGLESQL_CHECK_OK(
      type_factory->MakeProtoType(googlesql::AnonOutputWithReport::descriptor(),
                                  &anon_output_with_report_proto_type));
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionSignatureOptions has_numeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);

  FunctionOptions anon_options =
      FunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_ANONYMIZATION)
          .set_supports_over_clause(false)
          .set_supports_distinct_modifier(false)
          .set_supports_having_modifier(false)
          .set_supports_clamped_between_modifier(true)
          .set_volatility(FunctionEnums::VOLATILE);

  const FunctionArgumentTypeOptions optional_const_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_must_be_non_null()
          .set_cardinality(OPTIONAL);
  // TODO: Replace these required CLAMPED BETWEEN arguments with
  //   optional_const_arg_options once Quantiles can support automatic/implicit
  //   bounds.
  const FunctionArgumentTypeOptions required_const_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_must_be_non_null();
  const FunctionArgumentTypeOptions percentile_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_must_be_non_null()
          .set_min_value(0)
          .set_max_value(1);
  const FunctionArgumentTypeOptions quantiles_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_must_be_non_null()
          .set_min_value(1);

  // TODO: Fix this HACK - the CLAMPED BETWEEN lower and upper bounds
  // are optional, as are the privacy_budget weight and uid.  However,
  // the syntax and spec allows privacy_budget_weight (and uid) to be specified
  // but upper/lower bound to be unspecified, but that is not possible to
  // represent in a GoogleSQL FunctionSignature.  In the short term, the
  // resolver will guarantee that the privacy_budget_weight and uid are not
  // specified if the CLAMP are not, but longer term we must remove the
  // privacy_budget_weight and uid arguments as per the updated GoogleSQL
  // privacy language spec.
  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_count", Function::kGoogleSQLFunctionGroupName,
          {{int64_type,
            {/*expr=*/ARG_TYPE_ANY_2,
             /*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_COUNT}},
          anon_options, "count"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_sum", Function::kGoogleSQLFunctionGroupName,
          {{int64_type,
            {/*expr=*/int64_type,
             /*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_SUM_INT64},
           {uint64_type,
            {/*expr=*/uint64_type,
             /*lower_bound=*/{uint64_type, optional_const_arg_options},
             /*upper_bound=*/{uint64_type, optional_const_arg_options}},
            FN_ANON_SUM_UINT64},
           {double_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_SUM_DOUBLE},
           {numeric_type,
            {/*expr=*/numeric_type,
             /*lower_bound=*/{numeric_type, optional_const_arg_options},
             /*upper_bound=*/{numeric_type, optional_const_arg_options}},
            FN_ANON_SUM_NUMERIC,
            has_numeric_type_argument}},
          anon_options, "sum"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_avg", Function::kGoogleSQLFunctionGroupName,
          {{double_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_AVG_DOUBLE},
           {numeric_type,
            {/*expr=*/numeric_type,
             /*lower_bound=*/{numeric_type, optional_const_arg_options},
             /*upper_bound=*/{numeric_type, optional_const_arg_options}},
            FN_ANON_AVG_NUMERIC,
            has_numeric_type_argument}},
          anon_options, "avg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_count_star", Function::kGoogleSQLFunctionGroupName,
          {{int64_type,
            {/*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_COUNT_STAR}},
          anon_options.Copy()
              .set_sql_name("anon_count(*)")
              .set_get_sql_callback(&AnonCountStarFunctionSQL)
              .set_signature_text_callback(
                  &SignatureTextForAnonCountStarFunction)
              .set_bad_argument_error_prefix_callback(
                  &AnonCountStarBadArgumentErrorPrefix),
          // TODO: internal function names shouldn't be resolvable,
          // an alternative way to look up COUNT(*) will be needed to fix the
          // linked bug.
          "$count_star"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_var_pop", Function::kGoogleSQLFunctionGroupName,
          {{double_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_VAR_POP_DOUBLE},
           {double_type,
            {/*expr=*/double_array_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_VAR_POP_DOUBLE_ARRAY,
            FunctionSignatureOptions().set_is_internal(true)}},
          anon_options, "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_stddev_pop", Function::kGoogleSQLFunctionGroupName,
          {{double_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_STDDEV_POP_DOUBLE},
           {double_type,
            {/*expr=*/double_array_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_STDDEV_POP_DOUBLE_ARRAY,
            FunctionSignatureOptions().set_is_internal(true)}},
          anon_options, "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_percentile_cont", Function::kGoogleSQLFunctionGroupName,
          {{double_type,
            {/*expr=*/double_type,
             /*percentile=*/{double_type, percentile_arg_options},
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_PERCENTILE_CONT_DOUBLE},
           // This is an internal signature that is only used post-anon-rewrite,
           // and is not available in the external SQL language.
           {double_type,
            {/*expr=*/double_array_type,
             /*percentile=*/{double_type, percentile_arg_options},
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_PERCENTILE_CONT_DOUBLE_ARRAY,
            FunctionSignatureOptions().set_is_internal(true)}},
          anon_options, "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "anon_quantiles", Function::kGoogleSQLFunctionGroupName,
          {{double_array_type,
            {/*expr=*/double_type,
             /*quantiles=*/{int64_type, quantiles_arg_options},
             /*lower_bound=*/{double_type, required_const_arg_options},
             /*upper_bound=*/{double_type, required_const_arg_options}},
            FN_ANON_QUANTILES_DOUBLE},
           // This is an internal signature that is only used post-anon-rewrite,
           // and is not available in the external SQL language.
           {double_array_type,
            {/*expr=*/double_array_type,
             /*quantiles=*/{int64_type, quantiles_arg_options},
             /*lower_bound=*/{double_type, required_const_arg_options},
             /*upper_bound=*/{double_type, required_const_arg_options}},
            FN_ANON_QUANTILES_DOUBLE_ARRAY,
            FunctionSignatureOptions().set_is_internal(true)}},
          anon_options, "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_quantiles_with_report_json",
          Function::kGoogleSQLFunctionGroupName,
          {{json_type,
            {/*expr=*/double_type,
             /*quantiles=*/{int64_type, quantiles_arg_options},
             /*lower_bound=*/{double_type, required_const_arg_options},
             /*upper_bound=*/{double_type, required_const_arg_options}},
            FN_ANON_QUANTILES_DOUBLE_WITH_REPORT_JSON},
           // This is an internal signature that is only used post-anon-rewrite,
           // and is not available in the external SQL language.
           {json_type,
            {/*expr=*/double_array_type,
             /*quantiles=*/{int64_type, quantiles_arg_options},
             /*lower_bound=*/{double_type, required_const_arg_options},
             /*upper_bound=*/{double_type, required_const_arg_options}},
            FN_ANON_QUANTILES_DOUBLE_ARRAY_WITH_REPORT_JSON,
            FunctionSignatureOptions().set_is_internal(true)}},
          anon_options.Copy()
              .set_sql_name("anon_quantiles")
              .set_get_sql_callback(&AnonQuantilesWithReportJsonFunctionSQL)
              .set_signature_text_callback(absl::bind_front(
                  &SignatureTextForAnonQuantilesWithReportFunction,
                  /*report_format=*/"JSON")),
          "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_quantiles_with_report_proto",
          Function::kGoogleSQLFunctionGroupName,
          {{anon_output_with_report_proto_type,
            {/*expr=*/double_type,
             /*quantiles=*/{int64_type, quantiles_arg_options},
             /*lower_bound=*/{double_type, required_const_arg_options},
             /*upper_bound=*/{double_type, required_const_arg_options}},
            FN_ANON_QUANTILES_DOUBLE_WITH_REPORT_PROTO},
           // This is an internal signature that is only used post-anon-rewrite,
           // and is not available in the external SQL language.
           {anon_output_with_report_proto_type,
            {/*expr=*/double_array_type,
             /*quantiles=*/{int64_type, quantiles_arg_options},
             /*lower_bound=*/{double_type, required_const_arg_options},
             /*upper_bound=*/{double_type, required_const_arg_options}},
            FN_ANON_QUANTILES_DOUBLE_ARRAY_WITH_REPORT_PROTO,
            FunctionSignatureOptions().set_is_internal(true)}},
          anon_options.Copy()
              .set_sql_name("anon_quantiles")
              .set_get_sql_callback(&AnonQuantilesWithReportProtoFunctionSQL)
              .set_signature_text_callback(absl::bind_front(
                  &SignatureTextForAnonQuantilesWithReportFunction,
                  /*report_format=*/"PROTO")),
          "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_count_with_report_json", Function::kGoogleSQLFunctionGroupName,
          {{json_type,
            {/*expr=*/ARG_TYPE_ANY_2,
             /*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_COUNT_WITH_REPORT_JSON}},
          anon_options.Copy()
              .set_sql_name("anon_count")
              .set_get_sql_callback(&AnonCountWithReportJsonFunctionSQL),
          "count"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_count_with_report_proto",
          Function::kGoogleSQLFunctionGroupName,
          {{anon_output_with_report_proto_type,
            {/*expr=*/ARG_TYPE_ANY_2,
             /*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_COUNT_WITH_REPORT_PROTO}},
          anon_options.Copy()
              .set_sql_name("anon_count")
              .set_get_sql_callback(&AnonCountWithReportProtoFunctionSQL),
          "count"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_count_star_with_report_json",
          Function::kGoogleSQLFunctionGroupName,
          {{json_type,
            {/*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_COUNT_STAR_WITH_REPORT_JSON}},
          anon_options.Copy()
              .set_sql_name("anon_count(*)")
              .set_get_sql_callback(&AnonCountStarWithReportJsonFunctionSQL)
              .set_signature_text_callback(absl::bind_front(
                  &SignatureTextForAnonCountStarWithReportFunction,
                  /*report_format=*/"JSON"))
              .set_bad_argument_error_prefix_callback(
                  &AnonCountStarBadArgumentErrorPrefix),
          "$count_star"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_count_star_with_report_proto",
          Function::kGoogleSQLFunctionGroupName,
          {{anon_output_with_report_proto_type,
            {/*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_COUNT_STAR_WITH_REPORT_PROTO}},
          anon_options.Copy()
              .set_sql_name("anon_count(*)")
              .set_get_sql_callback(&AnonCountStarWithReportProtoFunctionSQL)
              .set_signature_text_callback(absl::bind_front(
                  &SignatureTextForAnonCountStarWithReportFunction,
                  /*report_format=*/"PROTO"))
              .set_bad_argument_error_prefix_callback(
                  &AnonCountStarBadArgumentErrorPrefix),
          "$count_star"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_sum_with_report_json", Function::kGoogleSQLFunctionGroupName,
          {{json_type,
            {/*expr=*/int64_type,
             /*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_SUM_WITH_REPORT_JSON_INT64},
           {json_type,
            {/*expr=*/uint64_type,
             /*lower_bound=*/{uint64_type, optional_const_arg_options},
             /*upper_bound=*/{uint64_type, optional_const_arg_options}},
            FN_ANON_SUM_WITH_REPORT_JSON_UINT64},
           {json_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_SUM_WITH_REPORT_JSON_DOUBLE}},
          anon_options.Copy()
              .set_sql_name("anon_sum")
              .set_get_sql_callback(&AnonSumWithReportJsonFunctionSQL),
          "sum"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_sum_with_report_proto", Function::kGoogleSQLFunctionGroupName,
          {{anon_output_with_report_proto_type,
            {/*expr=*/int64_type,
             /*lower_bound=*/{int64_type, optional_const_arg_options},
             /*upper_bound=*/{int64_type, optional_const_arg_options}},
            FN_ANON_SUM_WITH_REPORT_PROTO_INT64},
           {anon_output_with_report_proto_type,
            {/*expr=*/uint64_type,
             /*lower_bound=*/{uint64_type, optional_const_arg_options},
             /*upper_bound=*/{uint64_type, optional_const_arg_options}},
            FN_ANON_SUM_WITH_REPORT_PROTO_UINT64},
           {anon_output_with_report_proto_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_SUM_WITH_REPORT_PROTO_DOUBLE}},
          anon_options.Copy()
              .set_sql_name("anon_sum")
              .set_get_sql_callback(&AnonSumWithReportProtoFunctionSQL),
          "sum"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_avg_with_report_json", Function::kGoogleSQLFunctionGroupName,
          {{json_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_AVG_DOUBLE_WITH_REPORT_JSON}},
          anon_options.Copy()
              .set_sql_name("anon_avg")
              .set_get_sql_callback(&AnonAvgWithReportJsonFunctionSQL),
          "avg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$anon_avg_with_report_proto", Function::kGoogleSQLFunctionGroupName,
          {{anon_output_with_report_proto_type,
            {/*expr=*/double_type,
             /*lower_bound=*/{double_type, optional_const_arg_options},
             /*upper_bound=*/{double_type, optional_const_arg_options}},
            FN_ANON_AVG_DOUBLE_WITH_REPORT_PROTO}},
          anon_options.Copy()
              .set_sql_name("anon_avg")
              .set_get_sql_callback(&AnonAvgWithReportProtoFunctionSQL),
          "avg"));
}

static std::string DpSignatureTextCallback(
    const LanguageOptions& language_options, const Function& function,
    const FunctionSignature& signature) {
  std::vector<std::string> argument_texts;
  for (const FunctionArgumentType& argument : signature.arguments()) {
    if (!argument.has_argument_name() ||
        argument.argument_name() != "report_format") {
      argument_texts.push_back(argument.UserFacingNameWithCardinality(
          language_options.product_mode(),
          FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
          /*print_template_details=*/true));
    } else {
      // Includes the return result type as the two signatures accept the same
      // arguments and result type is distinguished based on value of
      // `report_format` argument.
      const std::string report_suffix =
          signature.result_type().type()->IsJsonType()
              ? "/*with value \"JSON\"*/"
              : (signature.result_type().type()->IsProto()
                     ? "/*with value \"PROTO\"*/"
                     : "");
      argument_texts.push_back(absl::StrCat(
          argument.UserFacingNameWithCardinality(
              language_options.product_mode(),
              FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
              /*print_template_details=*/true),
          report_suffix));
    }
  }
  return absl::StrCat(function.GetSQL(argument_texts), " -> ",
                      signature.result_type().UserFacingNameWithCardinality(
                          language_options.product_mode(),
                          FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
                          /*print_template_details=*/true));
}

// Creates a signature for DP function returning a report. This signature will
// only be matched if the argument at the 0-indexed `report_arg_position` has
// constant value that is equal to `report_format`.
static FunctionSignatureOptions DpReportSignatureOptions(
    bool analysis_constant_feature_enabled,
    functions::DifferentialPrivacyEnums::ReportFormat report_format,
    int report_arg_position) {
  auto dp_report_constraint =
      [analysis_constant_feature_enabled, report_arg_position, report_format](
          const FunctionSignature& concrete_signature,
          absl::Span<const InputArgumentType> arguments) -> std::string {
    if (arguments.size() <= report_arg_position) {
      return absl::StrCat("at most ", report_arg_position,
                          " argument(s) can be provided");
    }
    const Value* value = nullptr;
    absl::StatusOr<Value> value_or_status;
    if (arguments.at(report_arg_position).is_analysis_time_constant() &&
        analysis_constant_feature_enabled) {
      value_or_status =
          arguments.at(report_arg_position).GetAnalysisTimeConstantValue();
      if (!value_or_status.ok()) {
        return absl::StrCat(
            "Argument ", report_arg_position + 1,
            ": Signature requires an analysis time constant value for "
            "report_format, but the value is not available: ",
            value_or_status.status().message());
      }
      value = &value_or_status.value();
    } else {
      value = arguments.at(report_arg_position).literal_value();
    }
    if (value == nullptr || !value->is_valid()) {
      if (analysis_constant_feature_enabled) {
        return absl::StrCat(
            "Argument ", report_arg_position + 1,
            ": Invalid analysis time constant value for report_format");
      }
      return absl::StrCat("literal value is required at ",
                          report_arg_position + 1);
    }
    const Value expected_value = Value::Enum(
        types::DifferentialPrivacyReportFormatEnumType(), report_format);
    // If we encounter string we have to create enum type out of it to
    // be able to compare against expected enum value.
    if (value->type()->IsString()) {
      auto enum_value =
          Value::Enum(types::DifferentialPrivacyReportFormatEnumType(),
                      value->string_value());
      if (!enum_value.is_valid()) {
        return absl::StrCat("Invalid enum value: ", value->string_value());
      }
      if (enum_value.Equals(expected_value)) {
        return "";
      }
      return absl::StrCat("Found: ", enum_value.EnumDisplayName(),
                          " expecting: ", expected_value.EnumDisplayName());
    }
    if (value->Equals(expected_value)) {
      return std::string("");
    }
    absl::StatusOr<absl::string_view> enum_name = value->EnumName();
    if (!enum_name.ok()) {
      return absl::StrCat("Invalid value for report_format argument: ",
                          enum_name.status().message());
    }
    return absl::StrCat("Found: ", enum_name.value(),
                        " expecting: ", expected_value.EnumDisplayName());
  };
  return FunctionSignatureOptions()
      .set_constraints(dp_report_constraint)
      .AddRequiredLanguageFeature(
          FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS);
};

static std::vector<FunctionSignature> MaybeAddEpsilonArgumentToSignatures(
    const LanguageOptions& language_options,
    absl::Span<const FunctionSignature> function_signatures) {
  if (!language_options.LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_PER_AGGREGATION_BUDGET)) {
    return std::vector<FunctionSignature>(function_signatures.begin(),
                                          function_signatures.end());
  }
  std::vector<FunctionSignature> result;
  for (const FunctionSignature& signature : function_signatures) {
    FunctionArgumentTypeList arguments_copy = signature.arguments();
    arguments_copy.push_back(FunctionArgumentType(
        types::DoubleType(),
        FunctionArgumentTypeOptions()
            .set_argument_name("epsilon", FunctionEnums::NAMED_ONLY)
            .set_cardinality(FunctionEnums::OPTIONAL)));
    result.push_back(FunctionSignature(signature.result_type(), arguments_copy,
                                       signature.context_id(),
                                       signature.options()));
  }
  return result;
}

absl::Status GetDifferentialPrivacyFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions, NameToTypeMap* types) {
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* double_type = type_factory->get_double();
  const Type* numeric_type = type_factory->get_numeric();
  const Type* double_array_type = types::DoubleArrayType();
  const Type* bytes_type = type_factory->get_bytes();
  const Type* json_type = types::JsonType();
  const Type* report_proto_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeProtoType(
      functions::DifferentialPrivacyOutputWithReport::descriptor(),
      &report_proto_type));
  const Type* report_format_type =
      types::DifferentialPrivacyReportFormatEnumType();
  GOOGLESQL_RETURN_IF_ERROR(InsertType(types, options, report_format_type));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* contribution_bounding_strategy_type,
      types::
          DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType());  // NOLINT
  GOOGLESQL_RETURN_IF_ERROR(
      InsertType(types, options, contribution_bounding_strategy_type));

  // Creates a pair of same types for contribution bounds. First field is lower
  // bound and second is upper bound. Struct field name is omitted intentionally
  // because the user syntax for struct constructor does not allow "AS
  // field_name" and the struct is not usable in actual query.
  auto make_pair_type =
      [&type_factory](const Type* t) -> absl::StatusOr<const Type*> {
    const std::vector<StructField> pair_fields{{"", t}, {"", t}};
    const Type* pair_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(pair_fields, &pair_type));
    return pair_type;
  };

  GOOGLESQL_ASSIGN_OR_RETURN(const Type* int64_pair_type, make_pair_type(int64_type));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* uint64_pair_type, make_pair_type(uint64_type));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* double_pair_type, make_pair_type(double_type));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* numeric_pair_type, make_pair_type(numeric_type));

  FunctionSignatureOptions has_numeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);

  auto no_matching_signature_callback =
      [](absl::string_view qualified_function_name,
         absl::Span<const InputArgumentType> arguments,
         ProductMode product_mode) {
        return absl::StrCat(
            "No matching signature for ", qualified_function_name,
            " in DIFFERENTIAL_PRIVACY context",
            (arguments.empty()
                 ? " with no arguments"
                 : absl::StrCat(" for argument types: ",
                                InputArgumentType::ArgumentsToString(
                                    arguments, product_mode))));
      };

  const FunctionOptions dp_options =
      FunctionOptions()
          .set_supports_over_clause(false)
          .set_supports_distinct_modifier(false)
          .set_supports_having_modifier(false)
          .set_volatility(FunctionEnums::VOLATILE)
          .set_no_matching_signature_callback(no_matching_signature_callback)
          .AddRequiredLanguageFeature(FEATURE_DIFFERENTIAL_PRIVACY);

  const FunctionArgumentTypeOptions percentile_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_must_be_non_null()
          .set_min_value(0)
          .set_max_value(1);

  const FunctionArgumentTypeOptions quantiles_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_must_be_non_null()
          .set_min_value(1)
          .set_cardinality(FunctionEnums::REQUIRED);

  const FunctionArgumentTypeOptions
      optional_contribution_bounds_per_group_arg_options =
          FunctionArgumentTypeOptions()
              .set_must_be_constant()
              .set_argument_name("contribution_bounds_per_group",
                                 FunctionEnums::NAMED_ONLY)
              .set_cardinality(FunctionEnums::OPTIONAL);

  const FunctionArgumentTypeOptions
      optional_contribution_bounds_per_row_arg_options =
          FunctionArgumentTypeOptions()
              .set_must_be_constant()
              .set_argument_name("contribution_bounds_per_row",
                                 FunctionEnums::NAMED_ONLY)
              .set_cardinality(FunctionEnums::OPTIONAL);

  const FunctionArgumentTypeOptions
      required_contribution_bounds_per_row_arg_options =
          FunctionArgumentTypeOptions(
              optional_contribution_bounds_per_row_arg_options)
              .set_cardinality(FunctionEnums::REQUIRED);

  const FunctionArgumentTypeOptions default_required_argument =
      FunctionArgumentTypeOptions().set_cardinality(FunctionEnums::REQUIRED);

  // TODO: b/277365877 - Deprecate `set_must_be_constant` and use
  // `set_must_be_analysis_constant` instead.
  const FunctionArgumentTypeOptions report_arg_options =
      FunctionArgumentTypeOptions().set_must_be_constant().set_argument_name(
          "report_format", FunctionEnums::NAMED_ONLY);

  // TODO: internal function names shouldn't be resolvable,
  // an alternative way to look up COUNT(*) will be needed to fix the
  // linked bug.

  auto get_sql_callback_for_function = [](absl::string_view user_facing_name) {
    return [user_facing_name](const std::vector<std::string>& inputs) {
      return absl::StrCat(user_facing_name, "(", absl::StrJoin(inputs, ", "),
                          ")");
    };
  };

  bool analysis_constant_feature_enabled =
      options.language_options.LanguageFeatureEnabled(
          FEATURE_REPORT_FORMAT_CONSTANT_ARGUMENT) &&
      options.language_options.LanguageFeatureEnabled(
          FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT);

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_count", Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {int64_type,
                   {
                       /*expr=*/ARG_TYPE_ANY_2,
                       /*contribution_bounds_per_group=*/
                       {int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_COUNT},
                  {json_type,
                   {
                       /*expr=*/ARG_TYPE_ANY_2,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_JSON,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 1)},
                  {report_proto_type,
                   {
                       /*expr=*/ARG_TYPE_ANY_2,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_COUNT_REPORT_PROTO,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 1)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function("COUNT"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("count"),
          "count"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_count_star",
          Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {{int64_type,
                {
                    /*contribution_bounds_per_group=*/{
                        int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                },
                FN_DIFFERENTIAL_PRIVACY_COUNT_STAR},
               {json_type,
                {
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_group=*/
                    {int64_pair_type,
                     optional_contribution_bounds_per_group_arg_options},
                },
                FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_JSON,
                DpReportSignatureOptions(
                    analysis_constant_feature_enabled,
                    functions::DifferentialPrivacyEnums::JSON, 0)},
               {report_proto_type,
                {
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_group=*/
                    {int64_pair_type,
                     optional_contribution_bounds_per_group_arg_options},
                },
                FN_DIFFERENTIAL_PRIVACY_COUNT_STAR_REPORT_PROTO,
                DpReportSignatureOptions(
                    analysis_constant_feature_enabled,
                    functions::DifferentialPrivacyEnums::PROTO, 0)}}),
          dp_options.Copy()
              .set_get_sql_callback(&DPCountStarSQL)
              // TODO: Showing 3 signatures is very long and
              // repetitive for this function. Remove after engines switch to
              // showing detailed mismatch errors.
              .set_supported_signatures_callback(
                  &SupportedSignaturesForDPCountStar)
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("count(*)"),
          "$count_star"));

  std::vector<FunctionSignature> args;
  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_sum", Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {int64_type,
                   {
                       /*expr=*/int64_type,
                       /*contribution_bounds_per_group=*/
                       {int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_INT64},
                  {uint64_type,
                   {
                       /*expr=*/uint64_type,
                       /*contribution_bounds_per_group=*/
                       {uint64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_UINT64},
                  {double_type,
                   {
                       /*expr=*/double_type,
                       /*contribution_bounds_per_group=*/
                       {double_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_DOUBLE},
                  {numeric_type,
                   {
                       /*expr=*/numeric_type,
                       /*contribution_bounds_per_group=*/
                       {numeric_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_NUMERIC,
                   has_numeric_type_argument},
                  {json_type,
                   {
                       /*expr=*/int64_type,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_JSON_INT64,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 1)},
                  {json_type,
                   {
                       /*expr=*/double_type,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {double_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_JSON_DOUBLE,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 1)},
                  {json_type,
                   {
                       /*expr=*/uint64_type,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {uint64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_JSON_UINT64,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 1)},
                  {report_proto_type,
                   {
                       /*expr=*/int64_type,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {int64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_PROTO_INT64,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 1)},
                  {report_proto_type,
                   {
                       /*expr=*/double_type,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {double_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_PROTO_DOUBLE,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 1)},
                  {report_proto_type,
                   {
                       /*expr=*/uint64_type,
                       /*report_format=*/
                       {report_format_type, report_arg_options},
                       /*contribution_bounds_per_group=*/
                       {uint64_pair_type,
                        optional_contribution_bounds_per_group_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_SUM_REPORT_PROTO_UINT64,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 1)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function("SUM"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("sum"),
          "sum"));
  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_avg", Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {double_type,
                   {/*expr=*/double_type,
                    /*contribution_bounds_per_group=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_group_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_AVG_DOUBLE},
                  {numeric_type,
                   {/*expr=*/numeric_type,
                    /*contribution_bounds_per_group=*/
                    {numeric_pair_type,
                     optional_contribution_bounds_per_group_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_AVG_NUMERIC,
                   has_numeric_type_argument},
                  {json_type,
                   {/*expr=*/double_type,
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_group=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_group_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_AVG_DOUBLE_REPORT_JSON,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 1)},
                  {report_proto_type,
                   {/*expr=*/double_type,
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_group=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_group_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_AVG_DOUBLE_REPORT_PROTO,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 1)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function("AVG"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("avg"),
          "avg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_var_pop",
          Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {double_type,
                   {/*expr=*/double_type,
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_VAR_POP_DOUBLE},
                  {double_type,
                   {/*expr=*/double_array_type,
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_VAR_POP_DOUBLE_ARRAY,
                   FunctionSignatureOptions().set_is_internal(true)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function("VAR_POP"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("var_pop"),
          "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_stddev_pop",
          Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {double_type,
                   {/*expr=*/double_type,
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_STDDEV_POP_DOUBLE},
                  {double_type,
                   {/*expr=*/double_array_type,
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_STDDEV_POP_DOUBLE_ARRAY,
                   FunctionSignatureOptions().set_is_internal(true)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function("STDDEV_POP"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("stddev_pop"),
          "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_percentile_cont",
          Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {double_type,
                   {/*expr=*/double_type,
                    /*percentile=*/{double_type, percentile_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     optional_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_PERCENTILE_CONT_DOUBLE},
                  // This is an internal signature that is only used
                  // post-dp-rewrite, and is not available in the external SQL
                  // language.
                  {double_type,
                   {
                       /*expr=*/double_array_type,
                       /*percentile=*/{double_type, percentile_arg_options},
                       /*contribution_bounds_per_row=*/
                       {double_pair_type,
                        optional_contribution_bounds_per_row_arg_options},
                   },
                   FN_DIFFERENTIAL_PRIVACY_PERCENTILE_CONT_DOUBLE_ARRAY,
                   FunctionSignatureOptions().set_is_internal(true)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(
                  get_sql_callback_for_function("PERCENTILE_CONT"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("percentile_cont"),
          "array_agg"));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          "$differential_privacy_approx_quantiles",
          Function::kGoogleSQLFunctionGroupName,
          MaybeAddEpsilonArgumentToSignatures(
              options.language_options,
              {
                  {double_array_type,
                   {/*expr=*/double_type,
                    /*quantiles=*/{int64_type, quantiles_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     required_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE},
                  // This is an internal signature that is only used
                  // post-dp-rewrite, and is not available in the external SQL
                  // language.
                  {double_array_type,
                   {/*expr=*/double_array_type,
                    /*quantiles=*/{int64_type, quantiles_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     required_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_ARRAY,
                   FunctionSignatureOptions().set_is_internal(true)},
                  {json_type,
                   {/*expr=*/double_type,
                    /*quantiles=*/{int64_type, quantiles_arg_options},
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     required_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_REPORT_JSON,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 2)},
                  // This is an internal signature that is only used
                  // post-dp-rewrite, and is not available in the external SQL
                  // language.
                  {json_type,
                   {/*expr=*/double_array_type,
                    /*quantiles=*/{int64_type, quantiles_arg_options},
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     required_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_ARRAY_REPORT_JSON,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::JSON, 2)
                       .set_is_internal(true)},
                  {report_proto_type,
                   {/*expr=*/double_type,
                    /*quantiles=*/{int64_type, quantiles_arg_options},
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     required_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_REPORT_PROTO,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 2)},
                  // This is an internal signature that is only used
                  // post-dp-rewrite, and is not available in the external SQL
                  // language.
                  {report_proto_type,
                   {/*expr=*/double_array_type,
                    /*quantiles=*/{int64_type, quantiles_arg_options},
                    /*report_format=*/{report_format_type, report_arg_options},
                    /*contribution_bounds_per_row=*/
                    {double_pair_type,
                     required_contribution_bounds_per_row_arg_options}},
                   FN_DIFFERENTIAL_PRIVACY_QUANTILES_DOUBLE_ARRAY_REPORT_PROTO,
                   DpReportSignatureOptions(
                       analysis_constant_feature_enabled,
                       functions::DifferentialPrivacyEnums::PROTO, 2)
                       .set_is_internal(true)},
              }),
          dp_options.Copy()
              .set_get_sql_callback(
                  get_sql_callback_for_function("APPROX_QUANTILES"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("approx_quantiles"),
          "array_agg"));

  static constexpr absl::string_view kApproxCountDistinct =
      "$differential_privacy_approx_count_distinct";
  static constexpr absl::string_view kInitForDpApproxCountDistinct =
      "$differential_privacy_init_for_dp_approx_count_distinct";
  static constexpr absl::string_view kPartialMergeForDpApproxCountDistinct =
      "$differential_privacy_merge_partial_for_dp_approx_count_distinct";
  static constexpr absl::string_view kExtractForDpApproxCountDistinct =
      "$differential_privacy_extract_for_dp_approx_count_distinct";

  const FunctionArgumentTypeOptions
      optional_max_contributions_per_group_arg_options =
          FunctionArgumentTypeOptions()
              .set_must_be_constant()
              .set_argument_name("max_contributions_per_group",
                                 FunctionEnums::NAMED_ONLY)
              .set_cardinality(FunctionEnums::OPTIONAL);

  const FunctionArgumentTypeOptions contribution_bounding_strategy_arg_options =
      FunctionArgumentTypeOptions()
          .set_must_be_constant()
          .set_argument_name("contribution_bounding_strategy",
                             FunctionEnums::NAMED_ONLY)
          .set_cardinality(FunctionEnums::OPTIONAL);

  const FunctionArgumentTypeOptions supports_grouping =
      FunctionArgumentTypeOptions().set_must_support_grouping();

  const FunctionSignatureOptions internal_collation_rejecting_option =
      FunctionSignatureOptions().set_is_internal(true).set_rejects_collation();

  InsertCreatedFunction(
      functions, options,
      new Function(kInitForDpApproxCountDistinct,
                   Function::kGoogleSQLFunctionGroupName, Function::AGGREGATE,
                   /*function_signatures*/
                   {{bytes_type,
                     {/*expr=*/{ARG_TYPE_ANY_2, supports_grouping}},
                     FN_DIFFERENTIAL_PRIVACY_INIT_FOR_DP_APPROX_COUNT_DISTINCT,
                     internal_collation_rejecting_option}},
                   dp_options.Copy()
                       .set_get_sql_callback(get_sql_callback_for_function(
                           "INIT_FOR_DP_APPROX_COUNT_DISTINCT"))
                       .set_signature_text_callback(DpSignatureTextCallback)
                       .set_sql_name("init_for_dp_approx_count_distinct")));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          kApproxCountDistinct, Function::kGoogleSQLFunctionGroupName,
          {{int64_type,
            {/*expr=*/{ARG_TYPE_ANY_2, supports_grouping},
             /*contribution_bounding_strategy=*/
             {contribution_bounding_strategy_type,
              contribution_bounding_strategy_arg_options},
             /*max_contributions_per_group=*/
             {int64_type, optional_max_contributions_per_group_arg_options}},
            FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT,
            FunctionSignatureOptions().set_rejects_collation()},
           {json_type,
            {/*expr=*/{ARG_TYPE_ANY_2, supports_grouping},
             /*report_format=*/{report_format_type, report_arg_options},
             /*contribution_bounding_strategy=*/
             {contribution_bounding_strategy_type,
              contribution_bounding_strategy_arg_options},
             /*max_contributions_per_group=*/
             {int64_type, optional_max_contributions_per_group_arg_options}},
            FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_JSON,
            DpReportSignatureOptions(analysis_constant_feature_enabled,
                                     functions::DifferentialPrivacyEnums::JSON,
                                     1)
                .set_rejects_collation()},
           {report_proto_type,
            {/*expr=*/{ARG_TYPE_ANY_2, supports_grouping},
             /*report_format=*/{report_format_type, report_arg_options},
             /*contribution_bounding_strategy=*/
             {contribution_bounding_strategy_type,
              contribution_bounding_strategy_arg_options},
             /*max_contributions_per_group=*/
             {int64_type, optional_max_contributions_per_group_arg_options}},
            FN_DIFFERENTIAL_PRIVACY_APPROX_COUNT_DISTINCT_REPORT_PROTO,
            DpReportSignatureOptions(analysis_constant_feature_enabled,
                                     functions::DifferentialPrivacyEnums::PROTO,
                                     1)
                .set_rejects_collation()}},
          dp_options.Copy()
              .set_get_sql_callback(
                  get_sql_callback_for_function("APPROX_COUNT_DISTINCT"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("approx_count_distinct"),
          std::string(kInitForDpApproxCountDistinct)));

  InsertCreatedFunction(
      functions, options,
      new Function(
          kExtractForDpApproxCountDistinct,
          Function::kGoogleSQLFunctionGroupName, Function::SCALAR,
          {{int64_type,
            {/*expr=*/{bytes_type, default_required_argument},
             /*noisy_count_distinct_privacy_ids=*/{int64_type,
                                                   default_required_argument}},
            FN_DIFFERENTIAL_PRIVACY_EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT,
            internal_collation_rejecting_option},
           {report_proto_type,
            {/*expr=*/{bytes_type, default_required_argument},
             /*noisy_count_distinct_privacy_ids=*/{int64_type,
                                                   default_required_argument}},
            FN_DIFFERENTIAL_PRIVACY_EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT_REPORT_PROTO,  // NOLINT
            internal_collation_rejecting_option},
           {json_type,
            {/*expr=*/{bytes_type, default_required_argument},
             /*noisy_count_distinct_privacy_ids=*/{int64_type,
                                                   default_required_argument}},
            FN_DIFFERENTIAL_PRIVACY_EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT_REPORT_JSON,  // NOLINT
            internal_collation_rejecting_option}},
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function(
                  "EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("extract_for_dp_approx_count_distinct")));

  InsertCreatedFunction(
      functions, options,
      new AnonFunction(
          kPartialMergeForDpApproxCountDistinct,
          Function::kGoogleSQLFunctionGroupName,
          {{bytes_type,
            {/*expr=*/bytes_type,
             /*contribution_bounding_strategy=*/
             {contribution_bounding_strategy_type,
              contribution_bounding_strategy_arg_options},
             /*max_contributions_per_group=*/
             {int64_type, optional_max_contributions_per_group_arg_options}},
            FN_DIFFERENTIAL_PRIVACY_MERGE_PARTIAL_FOR_DP_APPROX_COUNT_DISTINCT,  // NOLINT
            internal_collation_rejecting_option}},
          dp_options.Copy()
              .set_get_sql_callback(get_sql_callback_for_function(
                  "MERGE_PARTIAL_FOR_DP_APPROX_COUNT_DISTINCT"))
              .set_signature_text_callback(DpSignatureTextCallback)
              .set_sql_name("merge_partial_for_dp_approx_count_distinct"),
          // The partial merge function cannot be called directly. It is only
          // used as part of the approx_count_distinct rewrite. The approx count
          // distinct rewrite happens always after the inner rewrite into a
          // per-user aggregation happened. Thus the partial_aggregate_name
          // provided here has no effect. Function class information might be
          // used by the engine.
          ""));
  return absl::OkStatus();
}  // NOLINT(readability/fn_size)

}  // namespace googlesql
